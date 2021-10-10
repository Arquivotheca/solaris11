/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * CUPS support for the SMB and SPOOLSS print services.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/list.h>
#include <cups/cups.h>
#include <assert.h>
#include <strings.h>
#include <signal.h>
#include <synch.h>
#include <dlfcn.h>
#include <errno.h>
#include <smb/smb.h>
#include <smbsrv/smb_share.h>
#include "smbd.h"

#define	SMBD_FN_PREFIX			"cifsprintjob-"
#define	SMBD_CUPS_SPOOL_DIR		"//var//spool//cups"
#define	SMBD_PRINTSHR_COMMENT		"Print Share"
#define	SMBD_PRINT_SERVICE		"print service"

#define	SMBD_SPOOL_MAGIC		0x53504F4C	/* 'SPOL' */

typedef struct smbd_spool {
	mutex_t		sp_mutex;
	cond_t		sp_cv;
	list_t		sp_list;
} smbd_spool_t;

typedef struct smbd_spoolent {
	uint32_t	se_magic;
	list_node_t	se_lnd;
	smb_spooldoc_t	se_doc;
} smbd_spoolent_t;

typedef struct smbd_printjob {
	pid_t		pj_pid;
	int		pj_sysjob;
	int		pj_fd;
	time_t		pj_start_time;
	int		pj_status;
	size_t		pj_size;
	int		pj_page_count;
	boolean_t	pj_isspooled;
	boolean_t	pj_jobnum;
	char		pj_filename[MAXNAMELEN];
	char		pj_jobname[MAXNAMELEN];
	char		pj_username[MAXNAMELEN];
	char		pj_queuename[MAXNAMELEN];
} smbd_printjob_t;

typedef struct smbd_cups_ops {
	void		*cups_hdl;
	cups_lang_t	*(*cupsLangDefault)();
	const char	*(*cupsLangEncoding)(cups_lang_t *);
	void		(*cupsLangFree)(cups_lang_t *);
	ipp_status_t	(*cupsLastError)();
	int		(*cupsGetDests)(cups_dest_t **);
	void		(*cupsFreeDests)(int, cups_dest_t *);
	ipp_t		*(*cupsDoFileRequest)(http_t *, ipp_t *,
	    const char *, const char *);
	ipp_t		*(*ippNew)();
	void		(*ippDelete)();
	char		*(*ippErrorString)();
	ipp_attribute_t	*(*ippAddString)();
	void		(*httpClose)(http_t *);
	http_t		*(*httpConnect)(const char *, int);
} smbd_cups_ops_t;

static uint32_t smbd_cups_jobnum = 1;
static smbd_cups_ops_t smb_cups;
static mutex_t smbd_cups_mutex;
static smbd_spool_t smbd_spool;

static void *smbd_spool_monitor(void *);
static void smbd_spool_file(const smb_spooldoc_t *);
static int smbd_cups_init(void);
static void smbd_cups_fini(void);
static smbd_cups_ops_t *smbd_cups_ops(void);
static char *smbd_print_share_comment(cups_dest_t *);
static void *smbd_share_printers(void *);

void
smbd_spool_init(void)
{
	list_create(&smbd_spool.sp_list, sizeof (smbd_spoolent_t),
	    offsetof(smbd_spoolent_t, se_lnd));

	(void) smbd_thread_create(SMBD_PRINT_SERVICE, smbd_spool_monitor, NULL);
}

void
smbd_spool_fini(void)
{
	smbd_thread_kill(SMBD_PRINT_SERVICE, SIGTERM);
}

/*
 * All requests to print files should arrive here.  Requests are placed
 * on the smbd_spool queue and the monitor is notified.
 *
 * Windows Vista and Windows 7 use SMB requests while other versions of
 * Windows use the SPOOLSS service.  Versions that use the SPOOLSS
 * service create a zero length file, which is removed by smbd_spool_file.
 */
void
smbd_spool_document(const smb_spooldoc_t *doc)
{
	smbd_spoolent_t	*entry;
	smb_spooldoc_t	*sp;

	if ((entry = malloc(sizeof (smbd_spoolent_t))) == NULL) {
		smbd_log(LOG_NOTICE, "smbd_spool_document: %s",
		    strerror(errno));
		return;
	}

	entry->se_magic = SMBD_SPOOL_MAGIC;
	sp = &entry->se_doc;
	(void) memcpy(&sp->sd_ipaddr, &doc->sd_ipaddr, sizeof (smb_inaddr_t));
	(void) strlcpy(sp->sd_username, doc->sd_username, MAXNAMELEN);
	(void) strlcpy(sp->sd_docname, doc->sd_docname, MAXNAMELEN);
	(void) strlcpy(sp->sd_path, doc->sd_path, MAXPATHLEN);
	(void) strlcpy(sp->sd_printer, doc->sd_printer, MAXNAMELEN);

	(void) mutex_lock(&smbd_spool.sp_mutex);
	list_insert_tail(&smbd_spool.sp_list, entry);
	(void) cond_broadcast(&smbd_spool.sp_cv);
	(void) mutex_unlock(&smbd_spool.sp_mutex);
}

/*
 * This monitor thread blocks waiting for files to be printed.
 * Requests are received on the global smbd_spool list and
 * transferred to a local list before processing to minimize
 * the time that the global list is kept locked.
 *
 * To simplify error handling, we process requests even if the
 * libcups initialization fails.  Requests will be dropped in
 * smbd_spool_file() when if is unable to obtain the cups ops.
 *
 * It is unlikely that any print requests will be received if
 * libcups initialization has failed because we require a cups
 * handle in order to publish print shares.
 */
/*ARGSUSED*/
static void *
smbd_spool_monitor(void *arg)
{
	list_t		local;
	smbd_spoolent_t	*entry;

	if (smbd_cups_init() != 0)
		smbd_log(LOG_DEBUG, "print service offline");

	list_create(&local, sizeof (smbd_spoolent_t),
	    offsetof(smbd_spoolent_t, se_lnd));

	smbd_online_wait(SMBD_PRINT_SERVICE);
	spoolss_initialize();

	while (smbd_online()) {
		(void) mutex_lock(&smbd_spool.sp_mutex);

		while (list_is_empty(&smbd_spool.sp_list))
			(void) cond_wait(&smbd_spool.sp_cv,
			    &smbd_spool.sp_mutex);

		list_move_tail(&local, &smbd_spool.sp_list);
		(void) mutex_unlock(&smbd_spool.sp_mutex);

		while ((entry = list_remove_head(&local)) != NULL) {
			assert(entry->se_magic == SMBD_SPOOL_MAGIC);
			entry->se_magic = (uint32_t)~SMBD_SPOOL_MAGIC;

			smbd_spool_file(&entry->se_doc);
			free(entry);
		}
	}
	(void) mutex_lock(&smbd_spool.sp_mutex);
	list_move_tail(&local, &smbd_spool.sp_list);
	(void) mutex_unlock(&smbd_spool.sp_mutex);

	while ((entry = list_remove_head(&local)) != NULL)
		free(entry);

	spoolss_finalize();
	smbd_cups_fini();
	smbd_thread_exit();
	return (NULL);
}

/*
 * Spool files to a printer via the cups interface.
 */
static void
smbd_spool_file(const smb_spooldoc_t *sp)
{
	smbd_cups_ops_t	*cups;
	http_t		*http = NULL;		/* HTTP connection to server */
	ipp_t		*request = NULL;	/* IPP Request */
	ipp_t		*response = NULL;	/* IPP Response */
	cups_lang_t	*language = NULL;	/* Default language */
	char		uri[HTTP_MAX_URI];	/* printer-uri attribute */
	char		new_jobname[MAXNAMELEN];
	smbd_printjob_t	pjob;
	char 		clientname[INET6_ADDRSTRLEN];
	struct stat 	sbuf;
	int		rc = 1;
	char		*last_pname = NULL;
	char		*pname;
	char		tbuf[MAXPATHLEN];

	if (stat(sp->sd_path, &sbuf)) {
		smbd_log(LOG_INFO, "smbd_spool_file: %s: %s",
		    sp->sd_path, strerror(errno));
		return;
	}

	/*
	 * Remove zero size files and return; these are inadvertantly created
	 * by some versions of Windows, such as Windows 2000 or Windows XP.
	 */
	if (sbuf.st_size == 0) {
		if (remove(sp->sd_path) != 0)
			smbd_log(LOG_INFO,
			    "smbd_spool_file: cannot remove %s: %s",
			    sp->sd_path, strerror(errno));
		return;
	}

	if ((cups = smbd_cups_ops()) == NULL)
		return;

	if ((http = cups->httpConnect("localhost", 631)) == NULL) {
		smbd_log(LOG_INFO, "smbd_spool_file: cupsd not running");
		return;
	}

	if ((request = cups->ippNew()) == NULL) {
		smbd_log(LOG_INFO, "smbd_spool_file: ipp not running");
		return;
	}

	request->request.op.operation_id = IPP_PRINT_JOB;
	request->request.op.request_id = 1;
	language = cups->cupsLangDefault();

	cups->ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
	    "attributes-charset", NULL, cups->cupsLangEncoding(language));

	cups->ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
	    "attributes-natural-language", NULL, language->language);

	(void) strlcpy(tbuf, sp->sd_printer, MAXPATHLEN);
	pname = strtok(tbuf, "\\");
	while (pname = strtok(NULL, "\\"))
		last_pname = pname;
	if (last_pname == NULL)
		last_pname = SMBD_PRINTER_NAME_DEFAULT;

	(void) snprintf(uri, sizeof (uri), "ipp://localhost/printers/%s",
	    last_pname);
	pjob.pj_pid = pthread_self();
	pjob.pj_sysjob = 10;
	(void) strlcpy(pjob.pj_filename, sp->sd_path, MAXNAMELEN);
	pjob.pj_start_time = time(NULL);
	pjob.pj_status = 2;
	pjob.pj_size = sbuf.st_blocks * 512;
	pjob.pj_page_count = 1;
	pjob.pj_isspooled = B_TRUE;
	pjob.pj_jobnum = smbd_cups_jobnum;

	(void) strlcpy(pjob.pj_jobname, sp->sd_docname, MAXNAMELEN);
	(void) strlcpy(pjob.pj_username, sp->sd_username, MAXNAMELEN);
	(void) strlcpy(pjob.pj_queuename, SMBD_CUPS_SPOOL_DIR, MAXNAMELEN);

	cups->ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
	    "printer-uri", NULL, uri);

	cups->ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
	    "requesting-user-name", NULL, pjob.pj_username);

	if (smb_inet_ntop(&sp->sd_ipaddr, clientname,
	    SMB_IPSTRLEN(sp->sd_ipaddr.a_family)) == NULL) {
		smbd_log(LOG_INFO, "smbd_spool_file: %s: unknown client",
		    clientname);
		goto out;
	}

	cups->ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
	    "job-originating-host-name", NULL, clientname);

	(void) snprintf(new_jobname, MAXNAMELEN, "%s%d",
	    SMBD_FN_PREFIX, pjob.pj_jobnum);
	cups->ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
	    "job-name", NULL, new_jobname);

	(void) snprintf(uri, sizeof (uri) - 1, "/printers/%s", last_pname);

	response = cups->cupsDoFileRequest(http, request, uri,
	    pjob.pj_filename);
	if (response != NULL) {
		if (response->request.status.status_code >= IPP_OK_CONFLICT) {
			smbd_log(LOG_ERR, "smbd_spool_file: printer %s: %s",
			    last_pname,
			    cups->ippErrorString(cups->cupsLastError()));
		} else {
			atomic_inc_32(&smbd_cups_jobnum);
			rc = 0;
		}
	} else {
		smbd_log(LOG_ERR, "smbd_spool_file: unable to print to %s",
		    cups->ippErrorString(cups->cupsLastError()));
	}

	if (rc == 0)
		(void) unlink(pjob.pj_filename);

out:
	if (response)
		cups->ippDelete(response);

	if (language)
		cups->cupsLangFree(language);

	if (http)
		cups->httpClose(http);
}

static int
smbd_cups_init(void)
{
	(void) mutex_lock(&smbd_cups_mutex);

	if (smb_cups.cups_hdl != NULL) {
		(void) mutex_unlock(&smbd_cups_mutex);
		return (0);
	}

	if ((smb_cups.cups_hdl = dlopen("libcups.so.2", RTLD_NOW)) == NULL) {
		(void) mutex_unlock(&smbd_cups_mutex);
		smbd_log(LOG_DEBUG, "smbd_cups_init: cannot open libcups");
		return (ENOENT);
	}

	smb_cups.cupsLangDefault =
	    (cups_lang_t *(*)())dlsym(smb_cups.cups_hdl, "cupsLangDefault");
	smb_cups.cupsLangEncoding = (const char *(*)(cups_lang_t *))
	    dlsym(smb_cups.cups_hdl, "cupsLangEncoding");
	smb_cups.cupsDoFileRequest =
	    (ipp_t *(*)(http_t *, ipp_t *, const char *, const char *))
	    dlsym(smb_cups.cups_hdl, "cupsDoFileRequest");
	smb_cups.cupsLastError = (ipp_status_t (*)())
	    dlsym(smb_cups.cups_hdl, "cupsLastError");
	smb_cups.cupsLangFree = (void (*)(cups_lang_t *))
	    dlsym(smb_cups.cups_hdl, "cupsLangFree");
	smb_cups.cupsGetDests = (int (*)(cups_dest_t **))
	    dlsym(smb_cups.cups_hdl, "cupsGetDests");
	smb_cups.cupsFreeDests = (void (*)(int, cups_dest_t *))
	    dlsym(smb_cups.cups_hdl, "cupsFreeDests");

	smb_cups.httpClose = (void (*)(http_t *))
	    dlsym(smb_cups.cups_hdl, "httpClose");
	smb_cups.httpConnect = (http_t *(*)(const char *, int))
	    dlsym(smb_cups.cups_hdl, "httpConnect");

	smb_cups.ippNew = (ipp_t *(*)())dlsym(smb_cups.cups_hdl, "ippNew");
	smb_cups.ippDelete = (void (*)())dlsym(smb_cups.cups_hdl, "ippDelete");
	smb_cups.ippErrorString = (char *(*)())
	    dlsym(smb_cups.cups_hdl, "ippErrorString");
	smb_cups.ippAddString = (ipp_attribute_t *(*)())
	    dlsym(smb_cups.cups_hdl, "ippAddString");

	if (smb_cups.cupsLangDefault == NULL ||
	    smb_cups.cupsLangEncoding == NULL ||
	    smb_cups.cupsDoFileRequest == NULL ||
	    smb_cups.cupsLastError == NULL ||
	    smb_cups.cupsLangFree == NULL ||
	    smb_cups.cupsGetDests == NULL ||
	    smb_cups.cupsFreeDests == NULL ||
	    smb_cups.ippNew == NULL ||
	    smb_cups.httpClose == NULL ||
	    smb_cups.httpConnect == NULL ||
	    smb_cups.ippDelete == NULL ||
	    smb_cups.ippErrorString == NULL ||
	    smb_cups.ippAddString == NULL) {
		(void) dlclose(smb_cups.cups_hdl);
		smb_cups.cups_hdl = NULL;
		(void) mutex_unlock(&smbd_cups_mutex);
		smbd_log(LOG_DEBUG, "smbd_cups_init: cannot load libcups");
		return (ENOENT);
	}

	(void) mutex_unlock(&smbd_cups_mutex);
	return (0);
}

static void
smbd_cups_fini(void)
{
	(void) mutex_lock(&smbd_cups_mutex);

	if (smb_cups.cups_hdl != NULL) {
		(void) dlclose(smb_cups.cups_hdl);
		smb_cups.cups_hdl = NULL;
	}

	(void) mutex_unlock(&smbd_cups_mutex);
}

static smbd_cups_ops_t *
smbd_cups_ops(void)
{
	if (smb_cups.cups_hdl == NULL)
		return (NULL);

	return (&smb_cups);
}

void
smbd_load_printers(void)
{
	if (!smb_config_getbool(SMB_CI_PRINT_ENABLE))
		return;

	(void) smbd_thread_run("printer shares", smbd_share_printers, NULL);
}

/*
 * All print shares use the path from print$.
 */
/*ARGSUSED*/
static void *
smbd_share_printers(void *arg)
{
	cups_dest_t	*dests;
	cups_dest_t	*dest;
	smbd_cups_ops_t	*cups;
	smb_share_t	si;
	uint32_t	nerr;
	int		num_dests;
	int		i;

	if (!smb_config_getbool(SMB_CI_PRINT_ENABLE))
		return (NULL);

	if (smb_share_lookup(SMB_SHARE_PRINT, &si) != NERR_Success) {
		smbd_log(LOG_DEBUG, "smbd_share_printers: unable to load %s",
		    SMB_SHARE_PRINT);
		return (NULL);
	}

	(void) mutex_lock(&smbd_cups_mutex);

	if ((cups = smbd_cups_ops()) == NULL) {
		(void) mutex_unlock(&smbd_cups_mutex);
		return (NULL);
	}

	si.shr_drive = 'C';
	si.shr_type = STYPE_PRINTQ;
	/*
	 * Mark print shares as transient
	 */
	si.shr_flags &= ~SMB_SHRF_PERM;
	si.shr_flags |= SMB_SHRF_TRANS;

	num_dests = cups->cupsGetDests(&dests);

	for (i = num_dests, dest = dests; i > 0; i--, dest++) {
		if (dest->instance != NULL)
			continue;

		free(si.shr_name);
		free(si.shr_cmnt);
		si.shr_name = strdup(dest->name);
		si.shr_cmnt = strdup(smbd_print_share_comment(dest));

		if (si.shr_name == NULL || si.shr_cmnt == NULL) {
			smbd_log(LOG_DEBUG,
			    "smbd_share_printers: unable to add share %s"
			    " (not enough memory)", dest->name);
			break;
		}

		nerr = smb_share_add(&si);
		if (nerr == NERR_Success || nerr == NERR_DuplicateShare)
			smbd_log(LOG_DEBUG, "shared printer: %s", si.shr_name);
		else
			smbd_log(LOG_DEBUG,
			    "smbd_share_printers: unable to add share %s: %u",
			    si.shr_name, nerr);
	}

	smb_share_free(&si);
	cups->cupsFreeDests(num_dests, dests);

	(void) mutex_unlock(&smbd_cups_mutex);
	return (NULL);
}

static char *
smbd_print_share_comment(cups_dest_t *dest)
{
	cups_option_t	*options;
	char		*comment;
	char		*name;
	char		*value;
	int		i;

	comment = SMBD_PRINTSHR_COMMENT;

	if ((options = dest->options) == NULL)
		return (comment);

	for (i = 0; i < dest->num_options; ++i) {
		name = options[i].name;
		value = options[i].value;

		if (name == NULL || value == NULL ||
		    *name == '\0' || *value == '\0')
			continue;

		if (strcasecmp(name, "printer-info") == 0) {
			comment = value;
			break;
		}
	}

	return (comment);
}

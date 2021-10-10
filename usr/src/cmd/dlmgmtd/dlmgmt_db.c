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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <zone.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stropts.h>
#include <sys/conf.h>
#include <pthread.h>
#include <unistd.h>
#include <wait.h>
#include <libcontract.h>
#include <libcontract_priv.h>
#include <libgen.h>
#include <libdlflow.h>
#include <sys/contract/process.h>
#include "dlmgmt_impl.h"

typedef enum dlmgmt_db_op {
	DLMGMT_DB_OP_WRITE,
	DLMGMT_DB_OP_DELETE,
	DLMGMT_DB_OP_READ
} dlmgmt_db_op_t;

typedef struct dlmgmt_db_req_s {
	struct dlmgmt_db_req_s	*ls_next;
	dlmgmt_db_op_t		ls_op;
	char			ls_name[MAXFLOWNAMELEN];
	char			ls_root[MAXPATHLEN];
	datalink_id_t		ls_linkid;
	zoneid_t		ls_owner_zoneid;
	zoneid_t		ls_zoneid;
	dlmgmt_obj_type_t	ls_type;	/* a request for flow or link */
	uint32_t		ls_flags;	/* Either DLMGMT_ACTIVE or   */
						/* DLMGMT_PERSIST, not both. */
} dlmgmt_db_req_t;

typedef struct dlmgmt_flowprop_s {
	char				propname[MAXLINKATTRLEN];
	uint64_t			propval;
	struct dlmgmt_flowprop_s	*propnext;
} dlmgmt_flowprop_t;

/*
 * List of pending db updates (e.g., because of a read-only filesystem).
 */
static dlmgmt_db_req_t	*dlmgmt_db_req_head = NULL;
static dlmgmt_db_req_t	*dlmgmt_db_req_tail = NULL;

/*
 * rewrite_needed is set to B_TRUE by process_obj_line() if it encounters a
 * line with an old format.  This will cause the file being read to be
 * re-written with the current format.
 */
static boolean_t	rewrite_needed;

static int		dlmgmt_db_update(dlmgmt_db_op_t, const char *,
			    dlmgmt_obj_t *, uint32_t, const char *);
static int		dlmgmt_process_db_req(dlmgmt_db_req_t *);
static int		dlmgmt_process_db_onereq(dlmgmt_db_req_t *, boolean_t);
static void		*dlmgmt_db_update_thread(void *);
static boolean_t	process_obj_line(char *, dlmgmt_obj_t *);
static boolean_t	process_obj_line_req(char *, dlmgmt_obj_t *,
			    dlmgmt_db_req_t *);
static int		process_db_write(dlmgmt_db_req_t *, FILE *, FILE *);
static int		process_db_read(dlmgmt_db_req_t *, FILE *);
static void		generate_obj_line(dlmgmt_obj_t *, boolean_t, char *);
static dladm_status_t	dlmgmt_flow_db_upgrade(FILE *, FILE *, zoneid_t);
static int		dlmgmt_flowprop_parse_old_db(char *, zoneid_t);
static dladm_status_t	dlmgmt_flow_parse_old_db(char *);

#define	BUFLEN(lim, ptr)	(((lim) > (ptr)) ? ((lim) - (ptr)) : 0)
#define	DLADM_FLOW_OLD_DB	"/etc/dladm/flowadm.conf"
#define	DLADM_FLOW_TMP		DLADM_FLOW_OLD_DB ".tmp"
#define	DLADM_FLOW_OLD_PROP_DB	"/etc/dladm/flowprop.conf"
#define	DLADM_FLOW_PROP_TMP	DLADM_FLOW_OLD_PROP_DB ".tmp"
#define	DLADM_FLOW_OLD_DB_WARNING	"#\n# THIS FILE IS NO LONGER USED BY" \
					" THE SYSTEM."

typedef void db_walk_func_t(dlmgmt_link_t *);

/*
 * Translator functions to go from dladm_datatype_t to character strings.
 * Each function takes a pointer to a buffer, the size of the buffer,
 * the name of the attribute, and the value to be written.  The functions
 * return the number of bytes written to the buffer.  If the buffer is not big
 * enough to hold the string representing the value, then nothing is written
 * and 0 is returned.
 */
typedef size_t write_func_t(char *, size_t, char *, void *);

/*
 * Translator functions to read from a NULL terminated string buffer into
 * something of the given DLADM_TYPE_*.  The functions each return the number
 * of bytes read from the string buffer.  If there is an error reading data
 * from the buffer, then 0 is returned.  It is the caller's responsibility
 * to free the data allocated by these functions.
 */
typedef size_t read_func_t(char *, void **);

typedef struct translator_s {
	const char	*type_name;
	write_func_t	*write_func;
	read_func_t	*read_func;
} translator_t;

/*
 * Translator functions, defined later but declared here so that
 * the translator table can be defined.
 */
static write_func_t	write_str, write_boolean, write_uint64;
static read_func_t	read_str, read_boolean, read_int64;

/*
 * Translator table, indexed by dladm_datatype_t.
 */
static translator_t translators[] = {
	{ "string",	write_str,	read_str	},
	{ "boolean",	write_boolean,	read_boolean	},
	{ "int",	write_uint64,	read_int64	}
};

static size_t ntranslators = sizeof (translators) / sizeof (translator_t);

#define	LINK_PROPERTY_DELIMINATOR	";"
#define	LINK_PROPERTY_TYPE_VALUE_SEP	","
#define	BASE_PROPERTY_LENGTH(t, n) (strlen(translators[(t)].type_name) +\
				    strlen(LINK_PROPERTY_TYPE_VALUE_SEP) +\
				    strlen(LINK_PROPERTY_DELIMINATOR) +\
				    strlen((n)))
#define	GENERATE_PROPERTY_STRING(buf, length, conv, name, type, val) \
	    (snprintf((buf), (length), "%s=%s%s" conv "%s", (name), \
	    translators[(type)].type_name, \
	    LINK_PROPERTY_TYPE_VALUE_SEP, (val), LINK_PROPERTY_DELIMINATOR))

/*
 * Name of the cache file to keep the active <link name, linkid> mapping
 */
char	cachefile[MAXPATHLEN];

/* Global variable to indicate whether old flow configuration is converted */
boolean_t	flow_converted;

#define	DLMGMT_CFG_COOKIE		"#FLOW CONFIGURE\n"
#define	DLMGMT_PERSISTENT_DB_PATH	"/etc/dladm/datalink.conf"
#define	DLMGMT_MAKE_FILE_DB_PATH(buffer, persistent)	\
	(void) snprintf((buffer), MAXPATHLEN, "%s", \
	(persistent) ? DLMGMT_PERSISTENT_DB_PATH : cachefile);

typedef struct zopen_arg {
	const char	*zopen_modestr;
	int		*zopen_pipe;
	int		zopen_fd;
} zopen_arg_t;

typedef struct zrename_arg {
	const char	*zrename_newname;
} zrename_arg_t;

typedef union zfoparg {
	zopen_arg_t	zfop_openarg;
	zrename_arg_t	zfop_renamearg;
} zfoparg_t;

typedef struct zfcbarg {
	boolean_t	zfarg_inglobalzone; /* is callback in global zone? */
	zoneid_t	zfarg_finglobalzone; /* is file in global zone? */
	const char	*zfarg_filename;
	zfoparg_t	*zfarg_oparg;
} zfarg_t;
#define	zfarg_openarg	zfarg_oparg->zfop_openarg
#define	zfarg_renamearg	zfarg_oparg->zfop_renamearg

/* zone file callback */
typedef int zfcb_t(zfarg_t *);

/*
 * Execute an operation on filename relative to zoneid's zone root.  If the
 * file is in the global zone, then the zfcb() callback will simply be called
 * directly.  If the file is in a non-global zone, then zfcb() will be called
 * both from the global zone's context, and from the non-global zone's context
 * (from a fork()'ed child that has entered the non-global zone).  This is
 * done to allow the callback to communicate with itself if needed (e.g. to
 * pass back the file descriptor of an opened file).
 */
static int
dlmgmt_zfop(const char *filename, zoneid_t zoneid, zfcb_t *zfcb,
    zfoparg_t *zfoparg)
{
	int		ctfd;
	int		err;
	pid_t		childpid;
	siginfo_t	info;
	zfarg_t		zfarg;
	ctid_t		ct;

	if (zoneid != GLOBAL_ZONEID) {
		/*
		 * We need to access a file that isn't in the global zone.
		 * Accessing non-global zone files from the global zone is
		 * unsafe (due to symlink attacks), we'll need to fork a child
		 * that enters the zone in question and executes the callback
		 * that will operate on the file.
		 *
		 * Before we proceed with this zone tango, we need to create a
		 * new process contract for the child, as required by
		 * zone_enter().
		 */
		errno = 0;
		ctfd = open64("/system/contract/process/template", O_RDWR);
		if (ctfd == -1)
			return (errno);
		if ((err = ct_tmpl_set_critical(ctfd, 0)) != 0 ||
		    (err = ct_tmpl_set_informative(ctfd, 0)) != 0 ||
		    (err = ct_pr_tmpl_set_fatal(ctfd, CT_PR_EV_HWERR)) != 0 ||
		    (err = ct_pr_tmpl_set_param(ctfd, CT_PR_PGRPONLY)) != 0 ||
		    (err = ct_tmpl_activate(ctfd)) != 0) {
			(void) close(ctfd);
			return (err);
		}
		childpid = fork();
		switch (childpid) {
		case -1:
			(void) ct_tmpl_clear(ctfd);
			(void) close(ctfd);
			return (err);
		case 0:
			(void) ct_tmpl_clear(ctfd);
			(void) close(ctfd);
			/*
			 * Elevate our privileges as zone_enter() requires all
			 * privileges.
			 */
			if ((err = dlmgmt_elevate_privileges()) != 0)
				_exit(err);
			if (zone_enter(zoneid) == -1)
				_exit(errno);
			if ((err = dlmgmt_drop_privileges()) != 0)
				_exit(err);
			break;
		default:
			if (contract_latest(&ct) == -1)
				ct = -1;
			(void) ct_tmpl_clear(ctfd);
			(void) close(ctfd);
			if (waitid(P_PID, childpid, &info, WEXITED) == -1) {
				(void) contract_abandon_id(ct);
				return (errno);
			}
			(void) contract_abandon_id(ct);
			if (info.si_status != 0)
				return (info.si_status);
		}
	}

	zfarg.zfarg_inglobalzone = (zoneid == GLOBAL_ZONEID || childpid != 0);
	zfarg.zfarg_finglobalzone = (zoneid == GLOBAL_ZONEID);
	zfarg.zfarg_filename = filename;
	zfarg.zfarg_oparg = zfoparg;
	err = zfcb(&zfarg);
	if (!zfarg.zfarg_inglobalzone)
		_exit(err);
	return (err);
}

static int
dlmgmt_zopen_cb(zfarg_t *zfarg)
{
	struct strrecvfd recvfd;
	boolean_t	newfile = B_FALSE;
	boolean_t	inglobalzone = zfarg->zfarg_inglobalzone;
	zoneid_t	finglobalzone = zfarg->zfarg_finglobalzone;
	const char	*filename = zfarg->zfarg_filename;
	const char	*modestr = zfarg->zfarg_openarg.zopen_modestr;
	int		*p = zfarg->zfarg_openarg.zopen_pipe;
	struct stat	statbuf;
	int		oflags;
	mode_t		mode;
	int		fd = -1;
	int		err;

	/* We only ever open a file for reading or writing, not both. */
	oflags = (modestr[0] == 'r') ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC;
	mode = (modestr[0] == 'r') ? 0 : S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	/* Open the file if we're in the same zone as the file. */
	if (inglobalzone == finglobalzone) {
		/*
		 * First determine if we will be creating the file as part of
		 * opening it.  If so, then we'll need to ensure that it has
		 * the proper ownership after having opened it.
		 */
		if (oflags & O_CREAT) {
			if (stat(filename, &statbuf) == -1) {
				if (errno == ENOENT)
					newfile = B_TRUE;
				else
					return (errno);
			}
		}
		if ((fd = open(filename, oflags, mode)) == -1)
			return (errno);
		if (newfile) {
			if (chown(filename, UID_DLADM, GID_NETADM) == -1) {
				err = errno;
				(void) close(fd);
				return (err);
			}
		}
	}

	/*
	 * If we're not in the global zone, send the file-descriptor back to
	 * our parent in the global zone.
	 */
	if (!inglobalzone) {
		assert(!finglobalzone);
		assert(fd != -1);
		return (ioctl(p[1], I_SENDFD, fd) == -1 ? errno : 0);
	}

	/*
	 * At this point, we know we're in the global zone.  If the file was
	 * in a non-global zone, receive the file-descriptor from our child in
	 * the non-global zone.
	 */
	if (!finglobalzone) {
		if (ioctl(p[0], I_RECVFD, &recvfd) == -1)
			return (errno);
		fd = recvfd.fd;
	}

	zfarg->zfarg_openarg.zopen_fd = fd;
	return (0);
}

static int
dlmgmt_zunlink_cb(zfarg_t *zfarg)
{
	if (zfarg->zfarg_inglobalzone != zfarg->zfarg_finglobalzone)
		return (0);
	return (unlink(zfarg->zfarg_filename) == 0 ? 0 : errno);
}

static int
dlmgmt_zrename_cb(zfarg_t *zfarg)
{
	if (zfarg->zfarg_inglobalzone != zfarg->zfarg_finglobalzone)
		return (0);
	return (rename(zfarg->zfarg_filename,
	    zfarg->zfarg_renamearg.zrename_newname) == 0 ? 0 : errno);
}

/*
 * Same as fopen(3C), except that it opens the file relative to zoneid's zone
 * root.
 */
static FILE *
dlmgmt_zfopen(const char *filename, const char *modestr, zoneid_t zoneid,
    int *err)
{
	int		p[2];
	zfoparg_t	zfoparg;
	FILE		*fp = NULL;

	if (zoneid != GLOBAL_ZONEID && pipe(p) == -1) {
		*err = errno;
		return (NULL);
	}

	zfoparg.zfop_openarg.zopen_modestr = modestr;
	zfoparg.zfop_openarg.zopen_pipe = p;
	*err = dlmgmt_zfop(filename, zoneid, dlmgmt_zopen_cb, &zfoparg);
	if (zoneid != GLOBAL_ZONEID) {
		(void) close(p[0]);
		(void) close(p[1]);
	}
	if (*err == 0) {
		fp = fdopen(zfoparg.zfop_openarg.zopen_fd, modestr);
		if (fp == NULL) {
			*err = errno;
			(void) close(zfoparg.zfop_openarg.zopen_fd);
		}
	}
	return (fp);
}

/*
 * Same as rename(2), except that old and new are relative to zoneid's zone
 * root.
 */
static int
dlmgmt_zrename(const char *old, const char *new, zoneid_t zoneid)
{
	zfoparg_t zfoparg;

	zfoparg.zfop_renamearg.zrename_newname = new;
	return (dlmgmt_zfop(old, zoneid, dlmgmt_zrename_cb, &zfoparg));
}

/*
 * Same as unlink(2), except that filename is relative to zoneid's zone root.
 */
static int
dlmgmt_zunlink(const char *filename, zoneid_t zoneid)
{
	return (dlmgmt_zfop(filename, zoneid, dlmgmt_zunlink_cb, NULL));
}

static size_t
write_str(char *buffer, size_t buffer_length, char *name, void *value)
{
	char	*ptr = value;
	size_t	data_length = strnlen(ptr, buffer_length);

	/*
	 * Strings are assumed to be NULL terminated.  In order to fit in
	 * the buffer, the string's length must be less then buffer_length.
	 * If the value is empty, there's no point in writing it, in fact,
	 * we shouldn't even see that case.
	 */
	if (data_length + BASE_PROPERTY_LENGTH(DLADM_TYPE_STR, name) ==
	    buffer_length || data_length == 0)
		return (0);

	/*
	 * Since we know the string will fit in the buffer, snprintf will
	 * always return less than buffer_length, so we can just return
	 * whatever snprintf returns.
	 */
	return (GENERATE_PROPERTY_STRING(buffer, buffer_length, "%s",
	    name, DLADM_TYPE_STR, ptr));
}

static size_t
write_boolean(char *buffer, size_t buffer_length, char *name, void *value)
{
	boolean_t	*ptr = value;

	/*
	 * Booleans are either zero or one, so we only need room for two
	 * characters in the buffer.
	 */
	if (buffer_length <= 1 + BASE_PROPERTY_LENGTH(DLADM_TYPE_BOOLEAN, name))
		return (0);

	return (GENERATE_PROPERTY_STRING(buffer, buffer_length, "%d",
	    name, DLADM_TYPE_BOOLEAN, *ptr));
}

static size_t
write_uint64(char *buffer, size_t buffer_length, char *name, void *value)
{
	uint64_t	*ptr = value;

	/*
	 * Limit checking for uint64_t is a little trickier.
	 */
	if (snprintf(NULL, 0, "%lld", *ptr)  +
	    BASE_PROPERTY_LENGTH(DLADM_TYPE_UINT64, name) >= buffer_length)
		return (0);

	return (GENERATE_PROPERTY_STRING(buffer, buffer_length, "%lld",
	    name, DLADM_TYPE_UINT64, *ptr));
}

static size_t
read_str(char *buffer, void **value)
{
	char		*ptr = calloc(MAXLINKATTRVALLEN, sizeof (char));
	ssize_t		len;

	if (ptr == NULL || (len = strlcpy(ptr, buffer, MAXLINKATTRVALLEN))
	    >= MAXLINKATTRVALLEN) {
		free(ptr);
		return (0);
	}

	*(char **)value = ptr;

	/* Account for NULL terminator */
	return (len + 1);
}

static size_t
read_boolean(char *buffer, void **value)
{
	boolean_t	*ptr = calloc(1, sizeof (boolean_t));

	if (ptr == NULL)
		return (0);

	*ptr = atoi(buffer);
	*(boolean_t **)value = ptr;

	return (sizeof (boolean_t));
}

static size_t
read_int64(char *buffer, void **value)
{
	int64_t	*ptr = calloc(1, sizeof (int64_t));

	if (ptr == NULL)
		return (0);

	*ptr = (int64_t)atoll(buffer);
	*(int64_t **)value = ptr;

	return (sizeof (int64_t));
}

static dlmgmt_db_req_t *
dlmgmt_db_req_alloc(dlmgmt_db_op_t op, const char *linkname,
    datalink_id_t linkid, zoneid_t owner_zoneid, zoneid_t zoneid,
    uint32_t flags, dlmgmt_obj_type_t type, const char *root, int *err)
{
	dlmgmt_db_req_t *req;

	if ((req = calloc(1, sizeof (dlmgmt_db_req_t))) == NULL) {
		*err = errno;
	} else {
		req->ls_op = op;
		if (linkname != NULL)
			(void) strlcpy(req->ls_name, linkname, MAXLINKNAMELEN);
		req->ls_linkid = linkid;
		req->ls_owner_zoneid = owner_zoneid;
		req->ls_zoneid = zoneid;
		req->ls_flags = flags;
		req->ls_type = type;
		if (root != NULL)
			(void) strlcpy(req->ls_root, root, MAXPATHLEN);
		else
			(void) bzero(req->ls_root, MAXPATHLEN);
	}
	return (req);
}

/*
 * Update the db entry with name "entryname" using information from "objp".
 */
static int
dlmgmt_db_update(dlmgmt_db_op_t op, const char *entryname, dlmgmt_obj_t *objp,
    uint32_t flags, const char *root)
{
	dlmgmt_db_req_t	*req;
	int		err;
	zoneid_t	zoneid, owner_zoneid;
	datalink_id_t	linkid;

	dlmgmt_log(LOG_DEBUG, "dlmgmt_db_update: entryname %s, type = %d",
	    entryname, objp->otype);
	/* It is either a persistent request or an active request, not both. */
	assert((flags == DLMGMT_PERSIST) || (flags == DLMGMT_ACTIVE));

	linkid = (objp->otype == DLMGMT_OBJ_FLOW) ? objp->oflow->ld_linkid:
	    objp->olink->ll_linkid;
	zoneid = (objp->otype == DLMGMT_OBJ_FLOW) ? objp->oflow->ld_zoneid :
	    objp->olink->ll_zoneid;

	/*
	 * Get the owner zoneid for links. Flows don't need a separate
	 * owner field at the moment because they cannot be assigned from
	 * one zone to another.
	 */
	owner_zoneid = (objp->otype == DLMGMT_OBJ_FLOW) ?
	    zoneid : objp->olink->ll_owner_zoneid;

	if ((req = dlmgmt_db_req_alloc(op, entryname, linkid,
	    owner_zoneid, zoneid, flags, objp->otype, root, &err)) == NULL)
		return (err);

	/*
	 * If the return error is EINPROGRESS, this request is handled
	 * asynchronously; return success.
	 */
	err = dlmgmt_process_db_req(req);
	if (err != EINPROGRESS)
		free(req);
	else
		err = 0;
	return (err);
}

#define	DLMGMT_DB_OP_STR(op)					\
	(((op) == DLMGMT_DB_OP_READ) ? "read" :			\
	(((op) == DLMGMT_DB_OP_WRITE) ? "write" : "delete"))

#define	DLMGMT_DB_CONF_STR(flag)				\
	(((flag) == DLMGMT_ACTIVE) ? "active" :			\
	(((flag) == DLMGMT_PERSIST) ? "persistent" : ""))

static int
dlmgmt_process_db_req(dlmgmt_db_req_t *req)
{
	pthread_t	tid;
	boolean_t	writeop;
	int		err;

	/*
	 * If there are already pending "write" requests, queue this request in
	 * the pending list.  Note that this function is called while the
	 * dlmgmt_rw_lock is held, so it is safe to access the global variables.
	 */
	writeop = (req->ls_op != DLMGMT_DB_OP_READ);
	if (writeop && (req->ls_flags == DLMGMT_PERSIST) &&
	    (dlmgmt_db_req_head != NULL)) {
		dlmgmt_db_req_tail->ls_next = req;
		dlmgmt_db_req_tail = req;
		return (EINPROGRESS);
	}

	err = dlmgmt_process_db_onereq(req, writeop);
	if (err != EINPROGRESS && err != 0 && err != ENOENT) {
		/*
		 * Log the error unless the request processing is still in
		 * progress or if the configuration file hasn't been created
		 * yet (ENOENT).
		 */
		dlmgmt_log(LOG_WARNING, "dlmgmt_process_db_onereq: %s "
		    "operation on %s configuration failed: %s",
		    DLMGMT_DB_OP_STR(req->ls_op),
		    DLMGMT_DB_CONF_STR(req->ls_flags), strerror(err));
	}

	if (err == EINPROGRESS) {
		assert(req->ls_flags == DLMGMT_PERSIST);
		assert(writeop && dlmgmt_db_req_head == NULL);
		dlmgmt_db_req_tail = dlmgmt_db_req_head = req;
		err = pthread_create(&tid, NULL, dlmgmt_db_update_thread, NULL);
		if (err == 0)
			return (EINPROGRESS);
	}
	return (err);
}

/*
 * The datalink information must be stored in the owner's cachefile
 * and not the target zone's cachefile for reasons outlined below.
 *
 * (1) zoneadmd sets up networking for a zone when it is getting the
 *     zone ready. The NGZ's cachefile, if created at this point, will
 *     not exist after the zone boots because a different dataset (swapfs)
 *     will be mounted at the same place during boot. Therefore, if dlmgmtd
 *     restarts after NGZ is booted then it (dlmgmtd) will not be able
 *     find and recreate autovnic entries.
 *
 * (2) If datalink entries are stored in the NGZ's cachefile then NGZ can
 *     modify them and affect dlmgmtd running in GZ (if dlmgmtd in GZ is
 *     restarted).
 *
 *  The owner's cachefile will now have entries for links with the same
 *  name. These name conflicts are handled as follows:
 *
 * (1) dlmgmtd distinguishes between autovnics with the same name but in
 *     different zones by storing the target zoneid in the cachefile.
 * (2) Due to the nature of key comparison function used by the AVL name
 *     database, the zoneid is part of the linkname. Therefore, links with
 *     the same name but different zoneids are treated as separate entries
 *     even after they have been read from the cachefile and added to the
 *     dlmgmtd's name AVL tree.
 * (3) Links created in GZ and assigned to NGZ don't have target zoneid
 *     stored in the cachefile. These links are also added to the onloan
 *     AVL tree by dlmgmtd (autovnics are not added to the onloan tree).
 */

static int
dlmgmt_process_db_onereq(dlmgmt_db_req_t *req, boolean_t writeop)
{
	int	err = 0;
	FILE	*fp, *nfp = NULL;
	char	file[MAXPATHLEN];
	char	newfile[MAXPATHLEN];

	DLMGMT_MAKE_FILE_DB_PATH(file, (req->ls_flags == DLMGMT_PERSIST));
	if (req->ls_root[0] != '\0')
		(void) snprintf(file, MAXPATHLEN, "%s/%s", req->ls_root, file);

	fp = dlmgmt_zfopen(file, "r", req->ls_owner_zoneid, &err);

	dlmgmt_log(LOG_DEBUG, "dlmgmt_process_db_onereq: file = %s", file);

	/*
	 * Note that it is not an error if the file doesn't exist.  If we're
	 * reading, we treat this case the same way as an empty file.  If
	 * we're writing, the file will be created when we open the file for
	 * writing below.
	 */
	if (fp == NULL && !writeop)
		return (err);

	if (writeop) {
		(void) snprintf(newfile, MAXPATHLEN, "%s.new", file);
		nfp = dlmgmt_zfopen(newfile, "w", req->ls_owner_zoneid, &err);
		if (nfp == NULL) {
			/*
			 * EROFS can happen at boot when the file system is
			 * read-only.  Return EINPROGRESS so that the caller
			 * can add this request to the pending request list
			 * and start a retry thread.
			 */
			err = (errno == EROFS ? EINPROGRESS : errno);
			goto done;
		}
		if ((err = process_db_write(req, fp, nfp)) == 0)
			err = dlmgmt_zrename(newfile, file,
			    req->ls_owner_zoneid);
	} else {
		err = process_db_read(req, fp);
	}

done:
	if (nfp != NULL) {
		(void) fclose(nfp);
		if (err != 0)
			(void) dlmgmt_zunlink(newfile, req->ls_zoneid);
	}
	(void) fclose(fp);
	return (err);
}

/*ARGSUSED*/
static void *
dlmgmt_db_update_thread(void *arg)
{
	dlmgmt_db_req_t	*req;

	dlmgmt_table_lock(B_TRUE);

	assert(dlmgmt_db_req_head != NULL);
	while ((req = dlmgmt_db_req_head) != NULL) {
		assert(req->ls_flags == DLMGMT_PERSIST);
		if (dlmgmt_process_db_onereq(req, B_TRUE) == EINPROGRESS) {
			/*
			 * The filesystem is still read only. Go to sleep and
			 * try again.
			 */
			dlmgmt_table_unlock();
			(void) sleep(5);
			dlmgmt_table_lock(B_TRUE);
			continue;
		}

		/*
		 * The filesystem is no longer read only. Continue processing
		 * and remove the request from the pending list.
		 */
		dlmgmt_db_req_head = req->ls_next;
		if (dlmgmt_db_req_tail == req) {
			assert(dlmgmt_db_req_head == NULL);
			dlmgmt_db_req_tail = NULL;
		}
		free(req);
	}
	dlmgmt_table_unlock();
	return (NULL);
}

static int
parse_objprops(char *buf, dlmgmt_obj_t *objp)
{
	boolean_t		found_type = B_FALSE;
	dladm_datatype_t	type = DLADM_TYPE_STR;
	int			i, len;
	char			*curr;
	char			attr_name[MAXLINKATTRLEN];
	size_t			attr_buf_len = 0;
	void			*attr_buf = NULL;
	dlmgmt_link_t		*linkp = NULL;
	dlmgmt_flowconf_t	*flowp = NULL;

	if (objp->otype == DLMGMT_OBJ_LINK)
		linkp = objp->olink;
	else
		flowp = objp->oflow;

	curr = buf;
	len = strlen(buf);
	attr_name[0] = '\0';
	for (i = 0; i < len; i++) {
		char		c = buf[i];
		boolean_t	match = (c == '=' ||
		    (c == ',' && !found_type) || c == ';');

		/*
		 * Move to the next character if there is no match and
		 * if we have not reached the last character.
		 */
		if (!match && i != len - 1)
			continue;

		if (match) {
			/*
			 * NUL-terminate the string pointed to by 'curr'.
			 */
			buf[i] = '\0';
			if (*curr == '\0')
				goto parse_fail;
		}
		if (attr_name[0] != '\0' && found_type) {
			/*
			 * We get here after we have processed the "<prop>="
			 * pattern. The pattern we are now interested in is
			 * "<val>;".
			 */
			if (c == '=')
				goto parse_fail;

			if (strcmp(attr_name, "linkid") == 0) {
				if (read_int64(curr, &attr_buf) == 0)
					goto parse_fail;
				if (linkp != NULL)
					linkp->ll_linkid = (datalink_class_t)*
					    (int64_t *)attr_buf;
			} else if (strcmp(attr_name, "name") == 0) {
				if (read_str(curr, &attr_buf) == 0)
					goto parse_fail;
				if (linkp != NULL)
					(void) snprintf(linkp->ll_link,
					    MAXLINKNAMELEN, "%s", attr_buf);
			} else if (strcmp(attr_name, "class") == 0) {
				if (read_int64(curr, &attr_buf) == 0)
					goto parse_fail;
				if (linkp != NULL)
					linkp->ll_class = (datalink_class_t)*
					    (int64_t *)attr_buf;
			} else if (strcmp(attr_name, "media") == 0) {
				if (read_int64(curr, &attr_buf) == 0)
					goto parse_fail;
				if (linkp != NULL)
					linkp->ll_media =
					    (uint32_t)*(int64_t *)attr_buf;
			} else if (strcmp(attr_name, "zoneid") == 0) {
				if (read_int64(curr, &attr_buf) == 0)
					goto parse_fail;
				if (linkp != NULL)
					linkp->ll_zoneid =
					    (uint32_t)*(int64_t *)attr_buf;
			} else {
				int err;

				attr_buf_len = translators[type].read_func(curr,
				    &attr_buf);
				if (attr_buf_len == 0)
					goto parse_fail;
				if (linkp != NULL) {
					err = objattr_set(&(linkp->ll_head),
					    attr_name, attr_buf, attr_buf_len,
					    type);
				} else {
					err = objattr_set(&(flowp->ld_head),
					    attr_name, attr_buf, attr_buf_len,
					    type);
				}
				if (err != 0)
					goto parse_fail;
			}

			free(attr_buf);
			attr_name[0] = '\0';
			found_type = B_FALSE;
		} else if (attr_name[0] != '\0') {
			/*
			 * Non-zero length attr_name and found_type of false
			 * indicates that we have not found the type for this
			 * attribute.  The pattern now is "<type>,<val>;", we
			 * want the <type> part of the pattern.
			 */
			for (type = 0; type < ntranslators; type++) {
				if (strcmp(curr,
				    translators[type].type_name) == 0) {
					found_type = B_TRUE;
					break;
				}
			}

			if (!found_type)
				goto parse_fail;
		} else {
			/*
			 * A zero length attr_name indicates we are looking
			 * at the beginning of a link attribute.
			 */
			if (c != '=')
				goto parse_fail;

			(void) snprintf(attr_name, MAXLINKATTRLEN, "%s", curr);
		}
		curr = buf + i + 1;
	}

	/* Correct any erroneous IPTUN datalink class constant in the file */
	if ((linkp != NULL) && (linkp->ll_class == 0x60)) {
		linkp->ll_class = DATALINK_CLASS_IPTUN;
		rewrite_needed = B_TRUE;
	}

	return (0);

parse_fail:
	if (attr_buf != NULL)
		free(attr_buf);
	/*
	 * Free linkp->ll_head (link attribute list)
	 */
	linkattr_destroy(linkp);
	return (-1);
}

/*
 * Parse a line from DB, it can be a line for flow or link.
 * if DLMGMT_OBJ_FLOW, the line contains information for a flow
 * otherwise, it contains information for a link
 */
static boolean_t
process_obj_line(char *buf, dlmgmt_obj_t *objp)
{
	int			i, len, llen;
	char			*str, *lasts;
	char			tmpbuf[MAXLINELEN];
	dlmgmt_flowconf_t	*flowp = NULL;
	dlmgmt_link_t		*linkp = NULL;
	dlmgmt_obj_type_t	type = objp->otype;

	if (type == DLMGMT_OBJ_FLOW) {
		flowp = objp->oflow;
		bzero(flowp, sizeof (dlmgmt_flowconf_t));
	} else {
		linkp = objp->olink;
		bzero(linkp, sizeof (dlmgmt_link_t));
		linkp->ll_linkid = DATALINK_INVALID_LINKID;
		linkp->ll_zoneid = ALL_ZONES;
	}

	/*
	 * Use a copy of buf for parsing so that we can do whatever we want.
	 */
	(void) strlcpy(tmpbuf, buf, MAXLINELEN);

	/*
	 * Skip leading spaces, blank lines, and comments.
	 */
	len = strlen(tmpbuf);
	for (i = 0; i < len; i++) {
		if (!isspace(tmpbuf[i]))
			break;
	}
	if (i == len || tmpbuf[i] == '#')
		return (B_TRUE);

	str = tmpbuf + i;
	/*
	 * Find the object name and assign it to the object structure.
	 */
	if (strtok_r(str, " \n\t", &lasts) == NULL)
		goto fail;

	llen = strlen(str);
	/*
	 * Note that a previous version of the persistent datalink.conf file
	 * stored the linkid as the first field.  In that case, the name will
	 * be obtained through parse_linkprops from a property with the format
	 * "name=<linkname>".  If we encounter such a format, we set
	 * rewrite_needed so that dlmgmt_db_init() can rewrite the file with
	 * the new format after it's done reading in the data.
	 */
	if (type == DLMGMT_OBJ_LINK) {
		if (isdigit(str[0])) {
			linkp->ll_linkid = atoi(str);
			rewrite_needed = B_TRUE;
		} else {
			if (strlcpy(linkp->ll_link, str,
			    sizeof (linkp->ll_link)) >= sizeof (linkp->ll_link))
				goto fail;
		}
	} else {
		if (strlcpy(flowp->ld_flow, str, sizeof (flowp->ld_flow))
		    >= sizeof (flowp->ld_flow))
			goto fail;
	}

	str += llen + 1;
	if (str >= tmpbuf + len)
		goto fail;

	/*
	 * Now find the list of link or flow properties.
	 */
	if ((str = strtok_r(str, " \n\t", &lasts)) == NULL)
		goto fail;

	if (parse_objprops(str, objp) < 0)
		goto fail;

	return (B_TRUE);

fail:
	/*
	 * Delete corrupted line.
	 */
	buf[0] = '\0';
	return (B_FALSE);
}

static boolean_t
process_obj_line_req(char *buf, dlmgmt_obj_t *objp, dlmgmt_db_req_t *req)
{
	dlmgmt_link_t	*link_in_db;

	/*
	 * buf contains the line that we just read from the
	 * datalink configuration file (or cachefile).
	 * Extact information from this line and store it in
	 * objp
	 */
	if (!process_obj_line(buf, objp))
		return (B_FALSE);

	if (req == NULL || objp->otype != DLMGMT_OBJ_LINK)
		return (B_TRUE);

	/*
	 * If the line represents a link then we need to set
	 * the link's owner zoneid and the current zoneid fields
	 * inside objp.
	 */

	/*
	 * The owner of the link is the zone in which the
	 * current configuration file (or cachefile) exists.
	 * The current configuration file was opened from
	 * the zone specified in req->ls_owner_zoneid.
	 */
	objp->olink->ll_owner_zoneid = req->ls_owner_zoneid;

	/*
	 * For links such as autovnics the current zoneid will
	 * be found in the file (i.e. cachefile).
	 */
	if (objp->olink->ll_zoneid != ALL_ZONES)
		return (B_TRUE);

	/*
	 * For other links wherein the zoneid is not explicitly
	 * specified, we initialize the link's current zoneid
	 * to its owner. If this link was assigned to a NGZ then
	 * link_activate() will detect this, update this link's
	 * current zoneid (i.e. objp->olink->ll_zoneid) and add
	 * this link to both the name and onloan AVL trees.
	 */
	objp->olink->ll_zoneid = req->ls_owner_zoneid;

	/*
	 * If this is READ request then we are done. This is
	 * the case where information is retrieved from the
	 * cachefile (possibly due to dlmgmtd restart) inorder
	 * to recreate the AVL trees.
	 */
	if (req->ls_op == DLMGMT_DB_OP_READ)
		return (B_TRUE);

	/*
	 * Next, we try to find the link in the AVL name database.
	 * Note that due to the nature of key comparison function
	 * used by the AVL name dasebase, the zoneid is part of
	 * the linkname. Therefore two links with the same name but
	 * different zoneids are treated as separate entries.
	 */
	link_in_db = link_by_name(objp->olink->ll_link,
	    objp->olink->ll_owner_zoneid);
	if (link_in_db != NULL) {
		/*
		 * If found then link_in_db->ll_zoneid will
		 * have the current zoneid for this link.
		 */
		objp->olink->ll_zoneid = link_in_db->ll_zoneid;
	}
	return (B_TRUE);
}

/*
 * Find any properties in linkp that refer to "old", and rename to "new".
 * Return B_TRUE if any renaming occurred.
 */
static int
dlmgmt_attr_rename(dlmgmt_link_t *linkp, const char *old, const char *new,
    boolean_t *renamed)
{
	dlmgmt_objattr_t	*attrp;
	char			*newval = NULL, *pname;
	char			valcp[MAXLINKATTRVALLEN];
	size_t			newsize;

	*renamed = B_FALSE;

	if ((attrp = objattr_find(linkp->ll_head, "linkover")) != NULL ||
	    (attrp = objattr_find(linkp->ll_head, "simnetpeer")) != NULL) {
		if (strcmp(old, (char *)attrp->lp_val) == 0) {
			newsize = strlen(new) + 1;
			if ((newval = malloc(newsize)) == NULL)
				return (errno);
			(void) strcpy(newval, new);
			free(attrp->lp_val);
			attrp->lp_val = newval;
			attrp->lp_sz = newsize;
			*renamed = B_TRUE;
		}
		return (0);
	}

	if ((attrp = objattr_find(linkp->ll_head, "portnames")) == NULL)
		return (0);

	/* <linkname>:[<linkname>:]... */
	if ((newval = calloc(MAXLINKATTRVALLEN, sizeof (char))) == NULL)
		return (errno);

	bcopy(attrp->lp_val, valcp, sizeof (valcp));
	pname = strtok(valcp, ":");
	while (pname != NULL) {
		if (strcmp(pname, old) == 0) {
			(void) strcat(newval, new);
			*renamed = B_TRUE;
		} else {
			(void) strcat(newval, pname);
		}
		(void) strcat(newval, ":");
		pname = strtok(NULL, ":");
	}
	if (*renamed) {
		free(attrp->lp_val);
		attrp->lp_val = newval;
		attrp->lp_sz = strlen(newval) + 1;
	} else {
		free(newval);
	}
	return (0);
}

/* Update the existing line if flow entry already exists in db */
static int
process_flow_write(dlmgmt_obj_t *objp, char *buf_input, boolean_t *found,
    FILE *nfp)
{
	dlmgmt_flowconf_t	flow_input;
	dlmgmt_obj_t		tmp;

	dlmgmt_log(LOG_DEBUG, "process_flow_write: flow name %s",
	    objp->oflow->ld_flow);

	tmp.otype = DLMGMT_OBJ_FLOW;
	tmp.oflow = &flow_input;
	(void) process_obj_line(buf_input, &tmp);
	if (strcmp(flow_input.ld_flow, objp->oflow->ld_flow) == 0) {
		*found = B_TRUE;

		/* replace existing line */
		return ((fputs(buf_input, nfp) == EOF) ? errno : 0);
	}

	return (0);
}

/*
 * Check if the link found in the configuration file (or cachefile)
 * matches what we are looking for. We use both i.e. zoneid and linkname
 * when comparing because the cachefile can have multiple links with
 * the same name but different zoneid.
 */
static boolean_t
is_link_match(dlmgmt_link_t *linkp, dlmgmt_db_req_t *req)
{
	assert(req->ls_zoneid >= 0 && linkp->ll_zoneid >= 0 &&
	    req->ls_op != DLMGMT_DB_OP_READ);
	if (strcmp(req->ls_name, linkp->ll_link) == 0 &&
	    linkp->ll_zoneid == req->ls_zoneid)
		return (B_TRUE);
	return (B_FALSE);
}

/*
 * datalink.conf file is separated into two sections, datalink section followed
 * by flow section. We use the cookie "#FLOW SECTION" to separate these two
 * sections.
 */
static int
process_db_write(dlmgmt_db_req_t *req, FILE *fp, FILE *nfp)
{
	boolean_t		done = B_FALSE, found = B_FALSE;
	int			err = 0;
	dlmgmt_link_t		link_in_file, *linkp = NULL, *dblinkp;
	dlmgmt_flowconf_t	flowconf, *flowp, *preflowp;
	dlmgmt_obj_t		objp;
	boolean_t		writeall, is_flowsec, is_flow, attr_renamed;
	boolean_t		persist = (req->ls_flags == DLMGMT_PERSIST);
	boolean_t		rename = B_FALSE, flow_renamed = B_FALSE;
	boolean_t		match;
	char			buf1[MAXLINELEN], buf2[MAXLINELEN];

	writeall = (req->ls_linkid == DATALINK_ALL_LINKID);
	is_flow = (req->ls_type == DLMGMT_OBJ_FLOW);

	/* Find linkp or flowp in memory, generate link or flow line */
	if (is_flow) {
		dlmgmt_flowavl_t	*f_avlp, f_avl;

		f_avl.la_zoneid = req->ls_zoneid;
		f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
		if (f_avlp == NULL)
			return (ENOENT);

		(void) strlcpy(flowconf.ld_flow, req->ls_name, MAXFLOWNAMELEN);
		flowp = avl_find(&f_avlp->la_flowtree, &flowconf, NULL);
		if (flowp == NULL)
			return (ENOENT);
		objp.otype = DLMGMT_OBJ_FLOW;
		objp.oflow = flowp;
		generate_obj_line(&objp, persist, buf1);
	} else {
		if (!writeall) {
			linkp = link_by_id(req->ls_linkid, req->ls_zoneid);
			if (linkp == NULL ||
			    ((linkp->ll_flags & req->ls_flags) == 0 &&
			    req->ls_op == DLMGMT_DB_OP_WRITE)) {
				/*
				 * This link has already been changed. This
				 * could happen if the request is pending
				 * because of read-only file-system. If so, we
				 * are done.
				 */
				return (0);
			}

			/*
			 * In the case of a rename, linkp's name has been
			 * updated to the new name, and req->ls_name is the old
			 * link name.
			 */
			rename = (strcmp(req->ls_name, linkp->ll_link) != 0);
			objp.otype = DLMGMT_OBJ_LINK;
			objp.olink = linkp;
			generate_obj_line(&objp, persist, buf1);
		}
	}

	/*
	 * fp can be NULL if the file didn't initially exist and we're
	 * creating it as part of this write operation.
	 */
	if (fp == NULL && !writeall) {
		if (!is_flow)
			return ((fputs(buf1, nfp) == EOF) ? errno : 0);
		return (0);
	}

	is_flowsec = B_FALSE;
	while (err == 0 && fgets(buf2, sizeof (buf2), fp) != NULL) {

		if (strcmp(buf2, DLMGMT_CFG_COOKIE) == 0) {
			/*
			 * We reached the end of datalink section. If the req is
			 * to write an info of a no-exist datalink, we write the
			 * link info first, then starts copying the rest of the
			 * file. If a datalink is renamed and there are flows on
			 * top of the datalink, we stop after the datalink sec
			 * is done, bail out and dump all flow entries from
			 * memory to file. Note the flow entries in memory has
			 * already been updated by now.
			 */
			if (!done && req->ls_op == DLMGMT_DB_OP_WRITE &&
			    req->ls_type == DLMGMT_OBJ_LINK && !rename) {
				if (fputs(buf1, nfp) == EOF)
					return (errno);
				done = B_TRUE;
			}

			if (fputs(buf2, nfp) == EOF)
				return (errno);

			is_flowsec = B_TRUE;
			if (flow_renamed)
				break;
			continue;
		}

		/*
		 * If already in flow section and req is for datalinks, or if
		 * in data section but req is for flows, just copy lines to new
		 * file
		 */
		if (persist &&
		    ((is_flowsec && !is_flow) || (!is_flowsec && is_flow))) {
			if (fputs(buf2, nfp) == EOF)
				err = errno;
			continue;
		}

		objp.otype = req->ls_type;
		if (!is_flow)
			objp.olink = &link_in_file;
		else
			objp.oflow = &flowconf;
		if (!process_obj_line_req(buf2, &objp, req))
			break;

		if (!is_flow) {
			/*
			 * Only the link name is needed. Free the memory
			 * allocated for the link attributes list of
			 * link_in_file.
			 */
			linkattr_destroy(&link_in_file);

			/*
			 * this is a comment line or we are done updating the
			 * line for the specified link, write the rest of
			 * lines out.
			 */
			if ((link_in_file.ll_link[0] == '\0' && !is_flowsec) ||
			    done) {
				if (fputs(buf2, nfp) == EOF)
					err = errno;
				continue;
			}
		}

		switch (req->ls_op) {
		case DLMGMT_DB_OP_WRITE:
			if (is_flow) {
				err = process_flow_write(&objp, buf1,
				    &found, nfp);
				if (!found) {
					err = (fputs(buf2, nfp) == EOF) ?
					    errno : 0;
				} else {
					done = B_TRUE;
				}
				break;
			}

			/*
			 * For write operations, we generate a new output line
			 * if we're either writing all links (writeall) or if
			 * the name of the link in the file matches the one
			 * we're looking for.  Otherwise, we write out the
			 * buffer as-is.
			 *
			 * If we're doing a rename operation, ensure that any
			 * references to the link being renamed in link
			 * properties are also updated before we write
			 * anything. Make sure the flows that are on top of the
			 * being-renamed link get its link name updated as well.
			 */
			if (writeall) {
				linkp = link_by_name(link_in_file.ll_link,
				    req->ls_zoneid);
			}

			if (writeall ||
			    is_link_match(&link_in_file, req)) {
				objp.otype = DLMGMT_OBJ_LINK;
				objp.olink = linkp;
				generate_obj_line(&objp, persist, buf2);
				if (!writeall && !rename)
					done = B_TRUE;
			} else if (rename && persist) {
				dblinkp = link_by_name(link_in_file.ll_link,
				    req->ls_zoneid);
				err = dlmgmt_attr_rename(dblinkp, req->ls_name,
				    linkp->ll_link, &attr_renamed);
				if (err != 0)
					break;
				if (attr_renamed) {
					objp.otype = DLMGMT_OBJ_LINK;
					objp.olink = dblinkp;
					generate_obj_line(&objp, persist,
					    buf2);
				}
				/*
				 * The datalink has been renamed. If there are
				 * flows on top of this link, we update the flow
				 * entries in memory, and set flow_renamed to
				 * B_TRUE. We cannot put flow lines now because
				 * the file stream indicator is pointing the
				 * datalink line.
				 */
				flowp = flow_by_linkid(req->ls_linkid,
				    req->ls_zoneid, NULL);
				flow_renamed = (flowp == NULL) ? B_FALSE :
				    B_TRUE;
				while (flowp != NULL) {
					preflowp = flowp;
					(void) objattr_set(&flowp->ld_head,
					    "linkover", linkp->ll_link,
					    MAXLINKNAMELEN, DLADM_TYPE_STR);
					flowp = flow_by_linkid(req->ls_linkid,
					    req->ls_zoneid, preflowp);
				}
			}
			if (fputs(buf2, nfp) == EOF)
				err = errno;
			break;
		case DLMGMT_DB_OP_DELETE:
			if (req->ls_type == DLMGMT_OBJ_LINK)
				match = is_link_match(&link_in_file,
				    req);
			else
				match = strcmp(req->ls_name,
				    flowconf.ld_flow) == 0;
			/*
			 * Delete is simple.  If buf does not represent the
			 * link we're deleting, write it out.
			 */
			if (!match) {
				if (fputs(buf2, nfp) == EOF)
					err = errno;
			} else {
				done = B_TRUE;
			}
			break;
		case DLMGMT_DB_OP_READ:
		default:
			err = EINVAL;
			break;
		}
	}

	/*
	 * We bailed out from the while loop. There can be three situations:
	 * 1. We reached the end of the file, the entry to be written is a new
	 *    flow entry, we write the COOKIE if there is no COOKIE in file,
	 *    then the flow entry.
	 * 2. We reached the end of the file, the entry is a new link entry but
	 *    no COOKIE present, we write the entry entry then the COOKIE;
	 * 3. We reached the end of the datalink section and some flow entry's
	 *    datalink has been renamed, we dump out all flow entries in memory
	 *    the flow section of the file.
	 */
	if (req->ls_op == DLMGMT_DB_OP_WRITE) {
		if (req->ls_type == DLMGMT_OBJ_FLOW) {
			if (!is_flowsec && persist)
				if (fputs(DLMGMT_CFG_COOKIE, nfp) == EOF)
					return (errno);
			if (!done) {
				if (fputs(buf1, nfp) == EOF)
					return (errno);
			}
		} else {
			if (!done && !rename) {
				if (fputs(buf1, nfp) == EOF)
					return (errno);
			}
			if (flow_renamed) {
				dlmgmt_flowavl_t	f_avl, *f_avlp;

				/*
				 * The file stream indicator must be pointing to
				 * the start of the flow section.
				 */
				assert(strcmp(buf2, DLMGMT_CFG_COOKIE) == 0);

				f_avl.la_zoneid = req->ls_zoneid;
				f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl,
				    NULL);
				flowp = avl_first(&f_avlp->la_flowtree);
				for (; flowp != NULL; flowp =
				    AVL_NEXT(&f_avlp->la_flowtree, flowp)) {
					objp.otype = DLMGMT_OBJ_FLOW;
					objp.oflow = flowp;
					generate_obj_line(&objp, persist, buf1);
					if (fputs(buf1, nfp)  == EOF)
						return (errno);
				}
			}
		}
	}

	return (err);
}

static int
process_db_read(dlmgmt_db_req_t *req, FILE *fp)
{
	avl_index_t	name_where, id_where, flow_where;
	dlmgmt_link_t	link_in_file, *newlink, *link_in_db;
	dlmgmt_obj_t	objp;
	char		buf[MAXLINELEN];
	int		err = 0;
	boolean_t	is_flowline = B_FALSE;

	/*
	 * This loop processes each line of the configuration file.
	 */
	while (fgets(buf, MAXLINELEN, fp) != NULL) {
		if (strcmp(buf, DLMGMT_CFG_COOKIE) == 0) {
			flow_converted = B_TRUE;
			is_flowline = B_TRUE;
			continue;
		}

		if ((req->ls_type == DLMGMT_OBJ_FLOW && !is_flowline) ||
		    (req->ls_type != DLMGMT_OBJ_FLOW && is_flowline))
			continue;

		if (req->ls_type == DLMGMT_OBJ_FLOW) {
			dlmgmt_flowconf_t	flow_in_file, *new_flow;
			dlmgmt_flowavl_t	f_avl, *f_avlp;
			dlmgmt_link_t		*linkconfp = NULL;
			dlmgmt_objattr_t	*attrp;

			bzero(&flow_in_file, sizeof (flow_in_file));
			objp.otype = DLMGMT_OBJ_FLOW;
			objp.oflow = &flow_in_file;
			if (!process_obj_line(buf, &objp)) {
				err = EINVAL;
				break;
			}
			flow_in_file.ld_zoneid = req->ls_zoneid;

			/* find out linkid by link name */
			attrp = objattr_find(flow_in_file.ld_head, "linkover");
			if (attrp != NULL)
				linkconfp = link_by_name(attrp->lp_val,
				    req->ls_zoneid);

			if (linkconfp != NULL) {
				flow_in_file.ld_linkid = linkconfp->ll_linkid;
			} else {
				flow_in_file.ld_linkid =
				    DATALINK_INVALID_LINKID;
			}

			f_avl.la_zoneid = req->ls_zoneid;
			f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
			if (f_avlp == NULL) {
				err = ENOENT;
				break;
			}

			if (avl_find(&f_avlp->la_flowtree, &flow_in_file,
			    &flow_where) == NULL) {
				new_flow = calloc(1, sizeof (*new_flow));
				if (new_flow == NULL) {
					err = ENOMEM;
					break;
				}
				bcopy(&flow_in_file, new_flow,
				    sizeof (*new_flow));
				avl_add(&f_avlp->la_flowtree, new_flow);
			}
			continue;
		}

		objp.otype = DLMGMT_OBJ_LINK;
		objp.olink = &link_in_file;
		if (!process_obj_line_req(buf, &objp, req)) {
			err = EINVAL;
			break;
		}

		/*
		 * Skip the comment line.
		 */
		if (link_in_file.ll_link[0] == '\0') {
			linkattr_destroy(&link_in_file);
			continue;
		}

		if ((req->ls_flags & DLMGMT_ACTIVE) &&
		    link_in_file.ll_linkid == DATALINK_INVALID_LINKID) {
			linkattr_destroy(&link_in_file);
			continue;
		}

		link_in_db = link_by_name(link_in_file.ll_link,
		    link_in_file.ll_zoneid);
		if (link_in_db != NULL) {
			/*
			 * If the link in the database already has the flag
			 * for this request set, then the entry is a
			 * duplicate.  If it's not a duplicate, then simply
			 * turn on the appropriate flag on the existing link.
			 */
			if (link_in_db->ll_flags & req->ls_flags) {
				dlmgmt_log(LOG_WARNING, "Duplicate links "
				    "in the repository: %s",
				    link_in_file.ll_link);
				linkattr_destroy(&link_in_file);
			} else {
				if (req->ls_flags & DLMGMT_PERSIST) {
					/*
					 * Save the newly read properties into
					 * the existing link.
					 */
					assert(link_in_db->ll_head == NULL);
					link_in_db->ll_head =
					    link_in_file.ll_head;
				} else {
					linkattr_destroy(&link_in_file);
				}
				link_in_db->ll_flags |= req->ls_flags;
			}
		} else {
			/*
			 * This is a new link.  Allocate a new dlmgmt_link_t
			 * and add it to the trees.
			 */
			newlink = calloc(1, sizeof (*newlink));
			if (newlink == NULL) {
				dlmgmt_log(LOG_WARNING, "Unable to allocate "
				    "memory to create new link %s",
				    link_in_file.ll_link);
				linkattr_destroy(&link_in_file);
				continue;
			}
			bcopy(&link_in_file, newlink, sizeof (*newlink));

			if (newlink->ll_linkid == DATALINK_INVALID_LINKID)
				newlink->ll_linkid = dlmgmt_nextlinkid;
			if (avl_find(&dlmgmt_id_avl, newlink, &id_where) !=
			    NULL) {
				dlmgmt_log(LOG_WARNING, "Link ID %d is already"
				    " in use, destroying link %s",
				    newlink->ll_linkid, newlink->ll_link);
				link_destroy(newlink);
				continue;
			}

			if ((req->ls_flags & DLMGMT_ACTIVE) &&
			    link_activate(newlink) != 0) {
				dlmgmt_log(LOG_WARNING, "Unable to activate %s",
				    newlink->ll_link);
				link_destroy(newlink);
				continue;
			}

			avl_insert(&dlmgmt_id_avl, newlink, id_where);
			/*
			 * link_activate call above can insert newlink in
			 * dlmgmt_name_avl tree when activating a link that is
			 * assigned to a NGZ.
			 */
			if (avl_find(&dlmgmt_name_avl, newlink,
			    &name_where) == NULL)
				avl_insert(&dlmgmt_name_avl, newlink,
				    name_where);

			dlmgmt_advance(newlink);
			newlink->ll_flags |= req->ls_flags;
		}
	}

	return (err);
}

/*
 * Generate an entry in the database.
 * Each entry has this format:
 * <link or flow name>	<prop0>=<type>,<val>;...;<propn>=<type>,<val>;
 */
static void
generate_obj_line(dlmgmt_obj_t *objp, boolean_t persist, char *buf)
{
	char			tmpbuf[MAXLINELEN];
	char			*name;
	char			*ptr = tmpbuf;
				/* leave 2 chars for term and CR */
	char			*lim = tmpbuf + MAXLINELEN - 2;
	dlmgmt_objattr_t	*cur_p = NULL;
	uint64_t		u64;
	dlmgmt_obj_type_t	type;
	dlmgmt_link_t		*linkp = NULL;
	dlmgmt_flowconf_t	*flowp = NULL;

	type = objp->otype;
	if (type == DLMGMT_OBJ_FLOW) {
		flowp = objp->oflow;
		name = flowp->ld_flow;
		ptr += snprintf(ptr, BUFLEN(lim, ptr), "%s\t", name);
	} else {
		linkp = objp->olink;
		name = linkp->ll_link;
		ptr += snprintf(ptr, BUFLEN(lim, ptr), "%s\t", name);
		if (!persist) {
			/*
			 * We store the linkid in the active database so that
			 * dlmgmtd can recover in the event that it is
			 * restarted.
			 */
			u64 = linkp->ll_linkid;
			ptr += write_uint64(ptr, BUFLEN(lim, ptr), "linkid",
			    &u64);

			/*
			 * If the owner zoneid != target zoneid then we
			 * store the target zoneid in the cachefile.
			 * This zoneid field helps us distinguish
			 * between links created by owner for some
			 * other zone and links which are onloan to
			 * other zones. We add entries for both type of
			 * links in the owner's active cachefile instead
			 * of the target zone's cachefile to prevent the
			 * target zone from manipulating them.
			 */
			if (linkp->ll_zoneid != linkp->ll_owner_zoneid &&
			    linkp->ll_onloan == B_FALSE) {
				u64 = linkp->ll_zoneid;
				ptr += write_uint64(ptr, BUFLEN(lim, ptr),
				    "zoneid", &u64);
			}
		}
		u64 = linkp->ll_class;
		ptr += write_uint64(ptr, BUFLEN(lim, ptr), "class", &u64);
		u64 = linkp->ll_media;
		ptr += write_uint64(ptr, BUFLEN(lim, ptr), "media", &u64);
	}

	cur_p = type == DLMGMT_OBJ_LINK ? linkp->ll_head : flowp->ld_head;

	/*
	 * The daemon does not keep any active link or flow attribute. Only
	 * store the attributes if this request is for persistent configuration,
	 */
	if (persist) {
		char	scratch[MAXLINELEN];
		int	len;

		for (; cur_p != NULL; cur_p = cur_p->lp_next) {
			len = translators[cur_p->lp_type].write_func(scratch,
			    MAXLINELEN, cur_p->lp_name, cur_p->lp_val);
			if (len < BUFLEN(lim, ptr)) {
				(void) strlcat(tmpbuf, scratch, MAXLINELEN);
				ptr += len;
			} else {
				dlmgmt_log(LOG_WARNING, "generate_obj_line: "
				    "line for entry %s is longer than %d "
				    "characters", name, MAXLINELEN);
				break;
			}
		}
	}
	(void) snprintf(buf, MAXLINELEN, "%s\n", tmpbuf);
}

int
dlmgmt_delete_db_entry(dlmgmt_obj_t *objp, uint32_t flags, const char *root)
{
	char name[MAXFLOWNAMELEN];

	if (objp->otype == DLMGMT_OBJ_FLOW)
		(void) strlcpy(name, objp->oflow->ld_flow, MAXFLOWNAMELEN);
	else
		(void) strlcpy(name, objp->olink->ll_link, MAXLINKNAMELEN);

	return (dlmgmt_db_update(DLMGMT_DB_OP_DELETE, name, objp, flags, root));
}

/* write a link or flow entry */
int
dlmgmt_write_db_entry(const char *entryname, dlmgmt_obj_t *objp, uint32_t flags,
    const char *root)
{
	int err;

	dlmgmt_log(LOG_DEBUG, "dlmgmt_write_db_entry: entry name %s",
	    entryname);

	if (flags & DLMGMT_PERSIST) {
		if ((err = dlmgmt_db_update(DLMGMT_DB_OP_WRITE, entryname,
		    objp, DLMGMT_PERSIST, root)) != 0) {
			return (err);
		}
	}

	if (flags & DLMGMT_ACTIVE) {
		if (((err = dlmgmt_db_update(DLMGMT_DB_OP_WRITE, entryname,
		    objp, DLMGMT_ACTIVE, root)) != 0) &&
		    (flags & DLMGMT_PERSIST)) {
			(void) dlmgmt_db_update(DLMGMT_DB_OP_DELETE, entryname,
			    objp, DLMGMT_PERSIST, root);
			return (err);
		}
	}

	return (0);
}

/*
 * Upgrade properties that have link IDs as values to link names.  Because '.'
 * is a valid linkname character, the port separater for link aggregations
 * must be changed to ':'.
 */
static void
linkattr_upgrade(dlmgmt_objattr_t *attrp)
{
	datalink_id_t	linkid;
	char		*portidstr;
	char		portname[MAXLINKNAMELEN + 1];
	dlmgmt_link_t	*linkp;
	char		*new_attr_val;
	size_t		new_attr_sz;
	boolean_t	upgraded = B_FALSE;

	if (strcmp(attrp->lp_name, "linkover") == 0 ||
	    strcmp(attrp->lp_name, "simnetpeer") == 0) {
		if (attrp->lp_type == DLADM_TYPE_UINT64) {
			linkid = (datalink_id_t)*(uint64_t *)attrp->lp_val;
			if ((linkp = link_by_id(linkid, GLOBAL_ZONEID)) == NULL)
				return;
			new_attr_sz = strlen(linkp->ll_link) + 1;
			if ((new_attr_val = malloc(new_attr_sz)) == NULL)
				return;
			(void) strcpy(new_attr_val, linkp->ll_link);
			upgraded = B_TRUE;
		}
	} else if (strcmp(attrp->lp_name, "portnames") == 0) {
		/*
		 * The old format for "portnames" was
		 * "<linkid>.[<linkid>.]...".  The new format is
		 * "<linkname>:[<linkname>:]...".
		 */
		if (!isdigit(((char *)attrp->lp_val)[0]))
			return;
		new_attr_val = calloc(MAXLINKATTRVALLEN, sizeof (char));
		if (new_attr_val == NULL)
			return;
		portidstr = (char *)attrp->lp_val;
		while (*portidstr != '\0') {
			errno = 0;
			linkid = strtol(portidstr, &portidstr, 10);
			if (linkid == 0 || *portidstr != '.' ||
			    (linkp = link_by_id(linkid, GLOBAL_ZONEID)) ==
			    NULL) {
				free(new_attr_val);
				return;
			}
			(void) snprintf(portname, sizeof (portname), "%s:",
			    linkp->ll_link);
			if (strlcat(new_attr_val, portname,
			    MAXLINKATTRVALLEN) >= MAXLINKATTRVALLEN) {
				free(new_attr_val);
				return;
			}
			/* skip the '.' delimiter */
			portidstr++;
		}
		new_attr_sz = strlen(new_attr_val) + 1;
		upgraded = B_TRUE;
	}

	if (upgraded) {
		attrp->lp_type = DLADM_TYPE_STR;
		attrp->lp_sz = new_attr_sz;
		free(attrp->lp_val);
		attrp->lp_val = new_attr_val;
	}
}

static void
dlmgmt_db_upgrade(dlmgmt_link_t *linkp)
{
	dlmgmt_objattr_t	*attrp;

	for (attrp = linkp->ll_head; attrp != NULL; attrp = attrp->lp_next)
		linkattr_upgrade(attrp);
}

static void
dlmgmt_db_phys_activate(dlmgmt_link_t *linkp)
{
	dlmgmt_obj_t	objp;

	objp.otype = DLMGMT_OBJ_LINK;
	objp.olink = linkp;

	linkp->ll_flags |= DLMGMT_ACTIVE;
	(void) dlmgmt_write_db_entry(linkp->ll_link, &objp, DLMGMT_ACTIVE,
	    NULL);
}

static void
dlmgmt_db_walk(zoneid_t zoneid, datalink_class_t class, db_walk_func_t *func)
{
	dlmgmt_link_t	*linkp;

	for (linkp = avl_first(&dlmgmt_id_avl); linkp != NULL;
	    linkp = AVL_NEXT(&dlmgmt_id_avl, linkp)) {
		if (linkp->ll_zoneid == zoneid && (linkp->ll_class & class))
			func(linkp);
	}
}

static boolean_t
link_over_phys(dlmgmt_link_t *linkp, zoneid_t zoneid)
{
	dlmgmt_link_t		*underp;
	dlmgmt_objattr_t	*attrp;
	char			attrstr[MAXLINKATTRVALLEN];
	char			*pname;

	/* Handle aggregation portnames (separator is ':'). */
	if ((attrp = objattr_find(linkp->ll_head, "portnames")) != NULL) {
		(void) strlcpy(attrstr, attrp->lp_val, MAXLINKATTRVALLEN);
		pname = strtok(attrstr, ":");
		while (pname != NULL) {
			if ((underp = link_by_name(pname, zoneid)) != NULL &&
			    underp->ll_class == DATALINK_CLASS_PHYS)
				return (B_TRUE);
			pname = strtok(NULL, ":");
		}
	}
	if ((attrp = objattr_find(linkp->ll_head, "linkover")) != NULL) {
		(void) strlcpy(attrstr, attrp->lp_val, MAXLINKATTRVALLEN);
		if ((underp = link_by_name(attrstr, zoneid)) != NULL &&
		    underp->ll_class == DATALINK_CLASS_PHYS)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*ARGSUSED*/
static void *
dlmgmt_linkname_policy_update_thread(void *arg)
{
	int err = 0;
	boolean_t iscsiboot = dlmgmt_is_iscsiboot();

	for (;;) {
		(void) sleep(5);
		if (dlmgmt_elevate_privileges() != 0)
			continue;
		dlmgmt_log(LOG_DEBUG, "dlmgmt_linkname_policy_update_thread: "
		    "setting linkname-policy/initialized property");
		err = dlmgmt_smf_set_boolean_property(DLMGMT_FMRI,
		    DLMGMT_LNP_PG, DLMGMT_LNP_INITIALIZED_PROP, B_TRUE);
		/* Set linkname policy to disable vanity naming if iSCSI boot */
		if (err == 0 && iscsiboot) {
			err = dlmgmt_smf_set_string_property(DLMGMT_FMRI,
			    DLMGMT_LNP_PG, DLMGMT_LNP_PHYS_PREFIX_PROP, "");
		}
		(void) dlmgmt_drop_privileges();
		if (err == 0)
			break;
	}
	dlmgmt_log(LOG_DEBUG, "dlmgmt_linkname_policy_update_thread: set "
	    "linkname-policy/initialized");
	return (NULL);
}

/*
 * Set or reset linkname policy.  Linkname policy is set the first time dlmgmtd
 * is run with linkname policy support (when linkname-poliy/initialized is
 * false), and can be reset by issuing "dladm reinit-phys".  "newpolicy"
 * is non-NULL for reset.
 */
int
dlmgmt_set_linkname_policy(char *newpolicy, boolean_t booting)
{
	dlmgmt_link_t *linkp;
	boolean_t phys_links = B_FALSE;
	boolean_t initialized = B_TRUE;
	int err = 0;
	pthread_t tid;

	/*
	 * For reinitialization, it does not matter if there are existing
	 * physical links, as long as there are not additional non-physical
	 * links over them - we reject reinitialization for such complex
	 * configuration.
	 */
	if (newpolicy != NULL) {
		if (strcmp(newpolicy, dlmgmt_phys_prefix) == 0)
			return (0);

		for (linkp = avl_first(&dlmgmt_id_avl); linkp != NULL;
		    linkp = AVL_NEXT(&dlmgmt_id_avl, linkp)) {
			if (linkp->ll_class == DATALINK_CLASS_PHYS)
				continue;
			if (link_over_phys(linkp, linkp->ll_owner_zoneid)) {
				dlmgmt_log(LOG_DEBUG,
				    "dlmgmt_set_linkname_policy: "
				    "found non-physical link %s with physical "
				    "link underneath, reinit phys-prefix "
				    "to %s prevented", linkp->ll_link,
				    newpolicy);
				return (EINVAL);
			}
		}
		err = dlmgmt_smf_set_string_property(DLMGMT_FMRI,
		    DLMGMT_LNP_PG, DLMGMT_LNP_PHYS_PREFIX_PROP, newpolicy);
		if (err == 0) {
			(void) strlcpy(dlmgmt_phys_prefix, newpolicy,
			    MAXLINKNAMELEN);
		}
		return (err);
	}
	/*
	 * For netboot, avoid using vanity names since network device is
	 * already plumbed.
	 */
	if (booting && dlmgmt_is_netboot()) {
		reconfigure = B_TRUE;
		dlmgmt_phys_prefix[0] = '\0';
		dlmgmt_log(LOG_DEBUG, "dlmgmt_set_linkname_policy: "
		    "in netboot environment");
		(void) pthread_create(&tid, NULL,
		    dlmgmt_linkname_policy_update_thread, NULL);
		(void) pthread_detach(tid);
		return (0);
	}

	/*
	 * If there are no physical links in persistent config on
	 * initialization, we can use default vanity naming policy (phys-prefix
	 * is already assigned "net" so there is nothing to do there).
	 * We have to be careful however, because in the live media environment
	 * post-install we will have the vanity-named links that were
	 * created on live media boot (live media install copies configuration
	 * from the preinstall environment).  So if we find physical links
	 * with a linkname policy (a prefix) that does not match the current
	 * policy, disable vanity naming by setting phys-prefix to the empty
	 * string - traditional driver-based names will be used.  Finally, set
	 * linkname-policy/initialized to true.
	 */
	if (dlmgmt_smf_get_boolean_property(DLMGMT_FMRI, DLMGMT_LNP_PG,
	    DLMGMT_LNP_INITIALIZED_PROP, &initialized) == 0 &&
	    initialized == B_FALSE) {
		if (dlmgmt_smf_get_string_property(DLMGMT_FMRI,
		    DLMGMT_LNP_PG, DLMGMT_LNP_PHYS_PREFIX_PROP,
		    dlmgmt_phys_prefix, MAXLINKNAMELEN) != 0) {
			dlmgmt_phys_prefix[0] = '\0';
		}

		for (linkp = avl_first(&dlmgmt_id_avl); linkp != NULL;
		    linkp = AVL_NEXT(&dlmgmt_id_avl, linkp)) {
			if (linkp->ll_class != DATALINK_CLASS_PHYS)
				continue;
			if (strlen(dlmgmt_phys_prefix) > 0 &&
			    strncmp(dlmgmt_phys_prefix, linkp->ll_link,
			    strlen(dlmgmt_phys_prefix)) != 0) {
				phys_links = B_TRUE;
				break;
			}
		}
		if (phys_links) {
			dlmgmt_log(LOG_DEBUG,
			    "dlmgmt_set_linkname_policy: found existing "
			    "physical links, disable vanity naming");
			(void) dlmgmt_smf_set_string_property(DLMGMT_FMRI,
			    DLMGMT_LNP_PG, DLMGMT_LNP_PHYS_PREFIX_PROP, "");
			dlmgmt_phys_prefix[0] = '\0';
		}
		reconfigure = B_TRUE;
		err = dlmgmt_smf_set_boolean_property(DLMGMT_FMRI,
		    DLMGMT_LNP_PG, DLMGMT_LNP_INITIALIZED_PROP, B_TRUE);
		if (err != 0) {
			/*
			 * Live media boot - root is readonly.  Spawn a thread
			 * to set linkname policy properties when root becomes
			 * read/write.
			 */
			dlmgmt_log(LOG_DEBUG, "dlmgmt_set_linkname_policy: "
			    "live media boot");
			(void) pthread_create(&tid, NULL,
			    dlmgmt_linkname_policy_update_thread, NULL);
			(void) pthread_detach(tid);
		}
		return (0);
	}
	if (dlmgmt_smf_get_string_property(DLMGMT_FMRI, DLMGMT_LNP_PG,
	    DLMGMT_LNP_PHYS_PREFIX_PROP, dlmgmt_phys_prefix, MAXLINKNAMELEN)
	    != 0) {
		dlmgmt_phys_prefix[0] = '\0';
	}
	return (0);
}

/*
 * Initialize the datalink <link name, linkid> mapping and the link's
 * attributes list, and flow configuration based on the configuration file
 * /etc/dladm/datalink.conf and the active configuration cache file
 * _PATH_SYSVOL/dladm/datalink-management:default.cache.
 */
int
dlmgmt_db_init(zoneid_t zoneid)
{
	dlmgmt_db_req_t		*req;
	int			err = 0;
	boolean_t		boot = B_FALSE;
	dlmgmt_flowavl_t	f_avl, *f_avlp;
	FILE			*flowdb_fp, *propdb_fp;

	/* Datalinks need to be handled before flows can be processed */
	if ((req = dlmgmt_db_req_alloc(DLMGMT_DB_OP_READ, NULL,
	    DATALINK_INVALID_LINKID, zoneid, zoneid, DLMGMT_ACTIVE,
	    DLMGMT_OBJ_LINK, NULL, &err)) == NULL)
		return (err);

	if ((err = dlmgmt_process_db_req(req)) != 0) {
		/*
		 * If we get back ENOENT, that means that the active
		 * configuration file doesn't exist yet, and is not an error.
		 * We'll create it down below after we've loaded the
		 * persistent configuration.
		 */
		if (err != ENOENT)
			goto done;
		boot = B_TRUE;
	}

	req->ls_flags = DLMGMT_PERSIST;
	err = dlmgmt_process_db_req(req);
	if (err != 0 && err != ENOENT)
		goto done;
	err = 0;
	if (rewrite_needed) {
		/*
		 * First update links in memory, then dump the entire db to
		 * disk.
		 */
		dlmgmt_db_walk(zoneid, DATALINK_CLASS_ALL, dlmgmt_db_upgrade);
		req->ls_op = DLMGMT_DB_OP_WRITE;
		req->ls_linkid = DATALINK_ALL_LINKID;
		if ((err = dlmgmt_process_db_req(req)) != 0 &&
		    err != EINPROGRESS)
			goto done;
	}
	if (boot) {
		dlmgmt_db_walk(zoneid, DATALINK_CLASS_PHYS,
		    dlmgmt_db_phys_activate);
	}

	if (zoneid == GLOBAL_ZONEID)
		(void) dlmgmt_set_linkname_policy(NULL, boot);
	/*
	 * Now that datalinks are done, process flows. First do system upgrade
	 * if flowadm.conf and flowprop.conf exist. Zones should not have any
	 * flow conf files, so we only need to care about the global zone.
	 */
	if (!flow_converted) {
		int	error;

		flowdb_fp = dlmgmt_zfopen(DLADM_FLOW_OLD_DB, "r", zoneid,
		    &error);
		propdb_fp = dlmgmt_zfopen(DLADM_FLOW_OLD_PROP_DB, "r", zoneid,
		    &error);

		(void) dlmgmt_flow_db_upgrade(flowdb_fp, propdb_fp, zoneid);
	} else {
		err = 0;
		req->ls_type = DLMGMT_OBJ_FLOW;
		req->ls_op = DLMGMT_DB_OP_READ;

		f_avl.la_zoneid = zoneid;
		f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
		if (f_avlp == NULL) {
			(void) flowconf_avl_create(zoneid, &f_avlp);
			avl_add(&dlmgmt_flowconf_avl, f_avlp);
		}
		(void) dlmgmt_process_db_req(req);
	}

done:
	if (err == EINPROGRESS)
		err = 0;
	else
		free(req);
	return (err);
}

/*
 * Remove all links in the given zoneid.
 */
void
dlmgmt_db_fini(zoneid_t zoneid)
{
	dlmgmt_link_t	*linkp = avl_first(&dlmgmt_name_avl), *next_linkp;

	while (linkp != NULL) {
		next_linkp = AVL_NEXT(&dlmgmt_name_avl, linkp);
		if (linkp->ll_zoneid == zoneid) {
			(void) dlmgmt_destroy_common(linkp,
			    DLMGMT_ACTIVE | DLMGMT_PERSIST);
		}
		linkp = next_linkp;
	}
}

/* Read configuration from flowadm.conf and flowprop.conf to memory */
static dladm_status_t
dlmgmt_flow_db_upgrade(FILE *flowdb_fp, FILE *propdb_fp, zoneid_t zoneid)
{
	char			line[MAXLINELEN], errmsg[DLADM_STRSIZE];
	dladm_status_t		status = DLADM_STATUS_OK;
	dlmgmt_flowavl_t	f_avl, *f_avlp;
	dlmgmt_flowconf_t	*flowp;
	dlmgmt_db_req_t		*req = NULL;
	int			err = 0;
	FILE			*fp;

	dlmgmt_log(LOG_DEBUG, "dlmgmt_flow_db_update, zoneid %d", zoneid);

	if (flowdb_fp == NULL)
		goto done;

	/*
	 * ngz also has flowadm.conf and flowprop.conf, but they should be empty
	 */
	if (zoneid != GLOBAL_ZONEID) {
		err = fclose(flowdb_fp);
		if (err != 0) {
			dlmgmt_log(LOG_WARNING, "dlmgmt_flow_db_upgrade: "
			    "%s; failed to close %s file for zone %d",
			    strerror(err), DLADM_FLOW_OLD_DB, zoneid);
		}

		err = fclose(propdb_fp);
		if (err != 0) {
			dlmgmt_log(LOG_WARNING, "dlmgmt_flow_db_upgrade: "
			    "%s; failed to close %s file for zone %d",
			    strerror(err), DLADM_FLOW_OLD_PROP_DB, zoneid);
		}
		goto bail;
	}

	while (fgets(line, MAXLINELEN, flowdb_fp) != NULL) {
		/* skip comments */
		if ((line[0] == '\0') || (line[0] == '\n') || (line[0] == '#'))
			continue;

		(void) strtok(line, " \n");
		/*
		 * Even if we encounter some error in one line, it should not
		 * stop us from converting the rest of the db contents.
		 * So we just log the error and continue to the next line.
		 */
		status = dlmgmt_flow_parse_old_db(line);
		if (status != DLADM_STATUS_OK) {
			dlmgmt_log(LOG_WARNING, "dlmgmt_flow_db_upgrade: "
			    "%s; failed to parse flowadm.conf",
			    dladm_status2str(status, errmsg));
		}
	}

	if (propdb_fp == NULL)
		goto done;
	while (fgets(line, MAXLINELEN, propdb_fp) != NULL) {
		/* skip comments */
		if ((line[0] == '\0') || (line[0] == '\n') || (line[0] == '#'))
			continue;

		status = dlmgmt_flowprop_parse_old_db(line, GLOBAL_ZONEID);
		if (status != DLADM_STATUS_OK) {
			dlmgmt_log(LOG_WARNING, "dlmgmt_flow_db_upgrade: "
			    "failed to parse flowprop.conf: %s.",
			    dladm_status2str(status, errmsg));
		}
	}

	/* Write all flows to datalink.conf */
	f_avl.la_zoneid = GLOBAL_ZONEID;
	f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
	if (f_avlp == NULL) {
		(void) flowconf_avl_create(GLOBAL_ZONEID, &f_avlp);
		avl_add(&dlmgmt_flowconf_avl, f_avlp);
		goto done;
	}

	status = DLADM_STATUS_OK;
	for (flowp = avl_first(&f_avlp->la_flowtree); flowp != NULL;
	    flowp = AVL_NEXT(&f_avlp->la_flowtree, flowp)) {
		if ((req = dlmgmt_db_req_alloc(DLMGMT_DB_OP_WRITE,
		    flowp->ld_flow, DATALINK_INVALID_LINKID,
		    GLOBAL_ZONEID, GLOBAL_ZONEID,
		    DLMGMT_PERSIST, DLMGMT_OBJ_FLOW, NULL, &err)) == NULL) {
			status = DLADM_STATUS_FLOW_DB_ERR;
			goto done;
		}

		flow_converted = B_TRUE;
		err = dlmgmt_process_db_req(req);
		if (err != 0)
			status = DLADM_STATUS_FLOW_DB_ERR;
	}

done:
	/*
	 * The flow configuration is now in datalink.conf, but we cannot delete
	 * the old flow configuration files, as pkg verify would fail. We use
	 * DLMGMT_CFG_COOKIE in datalink.conf to indicate that the conversion
	 * has been done so that we don't repeatedly attempt to convert the
	 * contents of these files
	 */
	(void) fclose(flowdb_fp);
	(void) fclose(propdb_fp);

	/*
	 * Replace the contents of flowadm.conf and flowprop.conf with one
	 * comment "THIS FILE IS NO LONGER USED BY THE SYSTEM."
	 */
	fp = fopen(DLADM_FLOW_OLD_DB, "w");
	if (fp == NULL) {
		dlmgmt_log(LOG_WARNING, "dlmgmt_flow_db_upgrade: %s; failed to "
		    " open %s file", strerror(err), DLADM_FLOW_OLD_DB);
	} else {
		err = fputs(DLADM_FLOW_OLD_DB_WARNING, fp);
		if (err != 0) {
			dlmgmt_log(LOG_WARNING, "dlmgmt_flow_db_upgrade: %s; "
			    "failed to replace %s.", strerror(err),
			    DLADM_FLOW_OLD_DB);
		}

		err = fclose(fp);
		if (err != 0) {
			dlmgmt_log(LOG_WARNING, "dlmgmt_flow_db_upgrade: %s; "
			    "failed close %s file", strerror(err),
			    DLADM_FLOW_OLD_DB);
		}
	}

	fp = fopen(DLADM_FLOW_OLD_PROP_DB, "w");
	if (fp == NULL) {
		dlmgmt_log(LOG_WARNING, "dlmgmt_flow_db_upgrade: failed to "
		    "open %s file.", DLADM_FLOW_OLD_PROP_DB);
	} else {
		err = fputs(DLADM_FLOW_OLD_DB_WARNING, fp);
		if (err != 0) {
			dlmgmt_log(LOG_WARNING, "dlmgmt_flow_db_upgrade: "
			    "failed to replace %s file.",
			    DLADM_FLOW_OLD_PROP_DB);
		}

		err = fclose(fp);
		if (err != 0) {
			dlmgmt_log(LOG_WARNING, "dlmgmt_flow_db_upgrade: %s; "
			    "failed to close %s file", strerror(err),
			    DLADM_FLOW_OLD_PROP_DB);
		}
	}

	/*
	 * If flow_converted is not set, this means that flowadm.conf is empty.
	 * Neither flow entries nor DLMGMT_CFG_COOKIE have been written to
	 * datalink.conf. However the flowadm.conf and flowprop.conf have
	 * already been converted. To avoid dlmgmtd repeatly open these files,
	 * we append DLMGMT_CFG_COOKIE at the end of datalink.conf file.
	 */
	if (!flow_converted) {
		fp = fopen(DLMGMT_PERSISTENT_DB_PATH, "a");
		if (fp == NULL) {
			dlmgmt_log(LOG_WARNING, "dlmgmt_flow_db_upgrade: "
			    "failed to open %s file",
			    DLMGMT_PERSISTENT_DB_PATH);
		} else {
			err = fputs(DLMGMT_CFG_COOKIE, fp);
			if (err != 0) {
				dlmgmt_log(LOG_WARNING,
				    "dlmgmt_flow_db_upgrade: failed to write "
				    "%s to %s", DLMGMT_CFG_COOKIE,
				    DLMGMT_PERSISTENT_DB_PATH);
			}

			err = fclose(fp);
			if (err != 0) {
				dlmgmt_log(LOG_WARNING,
				    "dlmgmt_flow_db_upgrade: %s; failed to "
				    "close %s file", strerror(err),
				    DLMGMT_PERSISTENT_DB_PATH);
			}
		}
		flow_converted = B_TRUE;
	}
bail:
	if (req != NULL)
		free(req);
	return (status);
}

static int
dlmgmt_parse_flowproplist(char *str, dlmgmt_flowprop_t **proplist, int *propnum)
{
	char			tmpstr[MAXLINELEN];
	char			*last, *token, *propstr, *val;
	dlmgmt_flowprop_t	*list, *head = NULL;
	int			num = 0, err = 0;

	dlmgmt_log(LOG_DEBUG, "dlmgmt_parse_flowproplist: %s", str);

	if (str == NULL || str[0] == '\0')
		return (DLADM_STATUS_BADVAL);

	(void) strlcpy(tmpstr, str, MAXLINELEN);
	token = strtok_r(tmpstr, ";", &last);
	while (token != NULL) {
		if ((propstr = strdup(token)) == NULL)
			return (errno);

		list = malloc(sizeof (dlmgmt_flowprop_t));
		if (list == NULL) {
			err = errno;
			goto done;
		}
		(void) strtok(propstr, "=");
		(void) strlcpy(list->propname, propstr, MAXLINKATTRLEN);
		val = strtok(NULL, "=");

		if (strcmp(list->propname, "maxbw") == 0)
			list->propval = atoi(val) * 1000000;
		else
			(void) dladm_str2pri(val,
			    (mac_priority_level_t *)&list->propval);

		list->propnext = head;
		head = list;
		num++;
		token = strtok_r(NULL, ";", &last);

		if (token[0] == '\n' || token[0] == '\0')
			token = NULL;
	}
done:
	free(propstr);

	if (err == 0) {
		*proplist = head;
		*propnum = num;
	}
	return (err);
}

static void
dlmgmt_free_flowproplist(dlmgmt_flowprop_t *proplist)
{
	dlmgmt_flowprop_t	*tmp1 = proplist, *tmp2;

	while (tmp1 != NULL) {
		tmp2 = tmp1->propnext;
		free(tmp1);
		tmp1 = tmp2;
	}
}

static int
dlmgmt_flowprop_parse_old_db(char *line, zoneid_t zoneid)
{
	int	err, num = 0;
	char	tmpline[MAXLINELEN], *lasts;
	char	flowname[MAXFLOWNAMELEN];
	dlmgmt_flowconf_t	flowconf, *flowconfp;
	dlmgmt_flowavl_t	f_avl, *f_avlp;
	dlmgmt_flowprop_t	*proplist, *proptmp;

	dlmgmt_log(LOG_DEBUG, "dlmgmt_flowprop_parse_old_db: line %s, "
	    "zoneid %d", line, zoneid);

	(void) strlcpy(tmpline, line, MAXLINELEN);

	if (strtok_r(tmpline, " \n\t", &lasts) == NULL)
		return (-1);
	(void) strlcpy(flowname, tmpline, MAXFLOWNAMELEN);

	err = dlmgmt_parse_flowproplist(lasts, &proplist, &num);
	if (err != 0) {
		dlmgmt_free_flowproplist(proplist);
		return (err);
	}

	f_avl.la_zoneid = zoneid;
	dlmgmt_flowconf_table_lock(B_FALSE);
	f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
	if (f_avlp == NULL) {
		err = ENOENT;
		goto done;
	}

	(void) strlcpy(flowconf.ld_flow, flowname, MAXFLOWNAMELEN);
	flowconfp = avl_find(&f_avlp->la_flowtree, &flowconf, NULL);
	if (flowconfp == NULL) {
		err = ENOENT;
		goto done;
	}

	proptmp = proplist;
	while (proptmp != NULL) {
		err = objattr_set(&(flowconfp->ld_head),
		    proptmp->propname, &proptmp->propval,
		    sizeof (uint64_t), DLADM_TYPE_UINT64);
		proptmp = proptmp->propnext;
	}

done:
	dlmgmt_flowconf_table_unlock();
	dlmgmt_free_flowproplist(proplist);
	return (err);
}

/*
 * Parse one line of the link flowadm DB
 * Returns -1 on failure, 0 on success.
 */
static dladm_status_t
dlmgmt_flow_parse_old_db(char *line)
{
	char			*token, tmpline[MAXLINELEN];
	char			*value, *attrstr = NULL;
	char			flowname[MAXFLOWNAMELEN];
	char			*lasts = NULL;
	dladm_status_t		status = DLADM_STATUS_OK;
	dld_flowinfo_t		attr;
	dlmgmt_flowconf_t	*flowconfp;
	dlmgmt_flowavl_t	*f_avlp, f_avl;
	dlmgmt_link_t		*linkconf;
	int			err;

	dlmgmt_log(LOG_DEBUG, "dlmgmt_flow_parse_old_db: line %s",
	    line);

	(void) strlcpy(tmpline, line, MAXLINELEN);

	/* flow name */
	if ((token = strtok_r(line, " \t", &lasts)) == NULL) {
		status = DLADM_STATUS_FLOW_DB_PARSE_ERR;
		goto done;
	}

	if (strlcpy(flowname, token, MAXFLOWNAMELEN) >= MAXFLOWNAMELEN) {
		status = DLADM_STATUS_FLOW_DB_PARSE_ERR;
		goto done;
	}

	/* get linkid first so we can create flowconf */
	while ((token = strtok_r(NULL, " \t", &lasts)) != NULL) {
		if ((attrstr = strdup(token)) == NULL) {
			status = DLADM_STATUS_FLOW_DB_PARSE_ERR;
			goto done;
		}

		(void) strtok(attrstr, "=");
		value = strtok(NULL, "=");
		if (value == NULL) {
			free(attrstr);
			return (DLADM_STATUS_FLOW_DB_PARSE_ERR);
		}

		if (strcmp(attrstr, "linkid") == 0) {
			attr.fi_linkid = (uint32_t)strtol(value, NULL, 10);
			if (attr.fi_linkid == DATALINK_INVALID_LINKID) {
				free(attrstr);
				return (DLADM_STATUS_FLOW_DB_PARSE_ERR);
			}
		}
	}

	f_avl.la_zoneid = GLOBAL_ZONEID;
	dlmgmt_flowconf_table_lock(B_TRUE);
	f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
	if (f_avlp == NULL) {
		err = flowconf_avl_create(GLOBAL_ZONEID, &f_avlp);
		if (err != 0) {
			status = DLADM_STATUS_FLOW_DB_PARSE_ERR;
			goto done;
		}
		avl_add(&dlmgmt_flowconf_avl, f_avlp);
	}

	(void) flowconf_create(flowname, attr.fi_linkid, GLOBAL_ZONEID,
	    &flowconfp);

	linkconf = link_by_id(attr.fi_linkid, GLOBAL_ZONEID);
	if (linkconf != NULL) {
		(void) objattr_set(&flowconfp->ld_head, "linkover",
		    linkconf->ll_link, MAXLINKNAMELEN, DLADM_TYPE_STR);
	} else {
		dlmgmt_log(LOG_WARNING, "dlmgmt_flow_parse_old_db: cannot "
		    "find link whose id is %d, skip flow %s", attr.fi_linkid,
		    flowname);
		flowconf_destroy(flowconfp);
		return (DLADM_STATUS_FLOW_DB_PARSE_ERR);
	}

	avl_add(&f_avlp->la_flowtree, flowconfp);

	if ((token = strtok_r(tmpline, " \t", &lasts)) == NULL) {
		status = DLADM_STATUS_FLOW_DB_PARSE_ERR;
		goto done;
	}

	/* resource control and flow descriptor parameters */
	while ((token = strtok_r(NULL, " \t", &lasts)) != NULL) {
		if ((attrstr = strdup(token)) == NULL) {
			status = DLADM_STATUS_FLOW_DB_PARSE_ERR;
			goto done;
		}

		(void) strtok(attrstr, "=");
		value = strtok(NULL, "=");
		if (value == NULL) {
			status = DLADM_STATUS_FLOW_DB_PARSE_ERR;
			goto done;
		}

		if ((strcmp(attrstr, "dsfield") == 0) ||
		    (strcmp(attrstr, "local_port") == 0) ||
		    (strcmp(attrstr, "transport") == 0) ||
		    (strcmp(attrstr, "remote_port") == 0)) {
			uint64_t	intval = atoi(value);

			if (strcmp(attrstr, "local_port") == 0 ||
			    strcmp(attrstr, "remote_port") == 0)
				intval = htons((uint16_t)intval);

			(void) objattr_set(&(flowconfp->ld_head), attrstr,
			    &intval, sizeof (uint64_t), DLADM_TYPE_UINT64);
		} else if ((strcmp(attrstr, "local_ip") == 0) ||
		    (strcmp(attrstr, "remote_ip") == 0)) {
			(void) objattr_set(&(flowconfp->ld_head), attrstr,
			    value, strlen(value), DLADM_TYPE_STR);
		}
	}

done:
	dlmgmt_flowconf_table_unlock();
	free(attrstr);
	return (status);
}

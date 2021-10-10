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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/list.h>
#include <assert.h>
#include <alloca.h>
#include <door.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <synch.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <smbsrv/smb_door.h>
#include <smbsrv/smb_xdr.h>
#include <smbsrv/smb_token.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/libsmbns.h>
#include "smbd.h"

#define	SMBD_ARG_MAGIC		0x53415247	/* 'SARG' */

/*
 * Parameter for door operations.
 */
typedef struct smbd_arg {
	uint32_t	magic;
	list_node_t	lnd;
	smb_doorhdr_t	hdr;
	const char	*opname;
	char		*data;
	size_t		datalen;
	char		*rbuf;
	size_t		rsize;
	boolean_t	response_ready;
	boolean_t	response_abort;
} smbd_arg_t;

/*
 * The list contains asynchronous requests that have been initiated
 * but have not yet been collected (via smbd_dop_async_response).
 */
typedef struct smbd_doorsvc {
	mutex_t		sd_mutex;
	cond_t		sd_cv;
	list_t		sd_async_list;
	uint32_t	sd_async_count;
} smbd_doorsvc_t;

static int smbd_dop_null(smbd_arg_t *);
static int smbd_dop_async_response(smbd_arg_t *);
static int smbd_dop_user_auth(smbd_arg_t *);
static int smbd_dop_user_logoff(smbd_arg_t *);
static int smbd_dop_lookup_sid(smbd_arg_t *);
static int smbd_dop_lookup_name(smbd_arg_t *);
static int smbd_dop_locate_dc(smbd_arg_t *);
static int smbd_dop_join(smbd_arg_t *);
static int smbd_dop_get_dcinfo(smbd_arg_t *);
static int smbd_dop_get_fqdn(smbd_arg_t *);
static int smbd_dop_vss_get_count(smbd_arg_t *);
static int smbd_dop_vss_get_snapshots(smbd_arg_t *);
static int smbd_dop_vss_map_gmttoken(smbd_arg_t *);
static int smbd_dop_ads_find_host(smbd_arg_t *);
static int smbd_dop_quota_query(smbd_arg_t *);
static int smbd_dop_quota_set(smbd_arg_t *);
static int smbd_dop_dfs_get_referrals(smbd_arg_t *);
static int smbd_dop_shr_hostaccess(smbd_arg_t *);
static int smbd_dop_shr_exec(smbd_arg_t *);
static int smbd_dop_shr_notify(smbd_arg_t *);
static int smbd_dop_shr_publish_admin(smbd_arg_t *);
static int smbd_dop_spooldoc(smbd_arg_t *);
static int smbd_dop_session_create(smbd_arg_t *);
static int smbd_dop_session_destroy(smbd_arg_t *);
static int smbd_dop_get_domains_info(smbd_arg_t *);

typedef int (*smbd_dop_t)(smbd_arg_t *);

typedef struct smbd_doorop {
	smb_dopcode_t	opcode;
	smbd_dop_t	op;
} smbd_doorop_t;

smbd_doorop_t smbd_doorops[] = {
	{ SMB_DR_NULL,			smbd_dop_null },
	{ SMB_DR_ASYNC_RESPONSE,	smbd_dop_async_response },
	{ SMB_DR_USER_AUTH,		smbd_dop_user_auth},
	{ SMB_DR_USER_LOGOFF,		smbd_dop_user_logoff },
	{ SMB_DR_LOOKUP_SID,		smbd_dop_lookup_sid },
	{ SMB_DR_LOOKUP_NAME,		smbd_dop_lookup_name },
	{ SMB_DR_LOCATE_DC,		smbd_dop_locate_dc },
	{ SMB_DR_JOIN,			smbd_dop_join },
	{ SMB_DR_GET_DCINFO,		smbd_dop_get_dcinfo },
	{ SMB_DR_GET_FQDN,		smbd_dop_get_fqdn },
	{ SMB_DR_VSS_GET_COUNT,		smbd_dop_vss_get_count },
	{ SMB_DR_VSS_GET_SNAPSHOTS,	smbd_dop_vss_get_snapshots },
	{ SMB_DR_VSS_MAP_GMTTOKEN,	smbd_dop_vss_map_gmttoken },
	{ SMB_DR_ADS_FIND_HOST,		smbd_dop_ads_find_host },
	{ SMB_DR_QUOTA_QUERY,		smbd_dop_quota_query },
	{ SMB_DR_QUOTA_SET,		smbd_dop_quota_set },
	{ SMB_DR_DFS_GET_REFERRALS,	smbd_dop_dfs_get_referrals },
	{ SMB_DR_SHR_HOSTACCESS,	smbd_dop_shr_hostaccess },
	{ SMB_DR_SHR_EXEC,		smbd_dop_shr_exec },
	{ SMB_DR_SHR_NOTIFY,		smbd_dop_shr_notify },
	{ SMB_DR_SHR_PUBLISH_ADMIN,	smbd_dop_shr_publish_admin },
	{ SMB_DR_SPOOLDOC,		smbd_dop_spooldoc },
	{ SMB_DR_SESSION_CREATE,	smbd_dop_session_create },
	{ SMB_DR_SESSION_DESTROY,	smbd_dop_session_destroy },
	{ SMB_DR_GET_DOMAINS_INFO,	smbd_dop_get_domains_info }
};

static int smbd_ndoorop = (sizeof (smbd_doorops) / sizeof (smbd_doorops[0]));

static smbd_doorsvc_t smbd_doorsvc;
static int smbd_door_fd = -1;
static int smbd_door_cookie = 0x534D4244;	/* SMBD */
static smbd_door_t smbd_door_sdh;

static void smbd_door_dispatch(void *, char *, size_t, door_desc_t *, uint_t);
static int smbd_door_dispatch_async(smbd_arg_t *);
static void smbd_door_release_async(smbd_arg_t *);
static void *smbd_door_dispatch_op(void *);

/*
 * Start the smbd door service.  Create and bind to a door.
 * Returns 0 on success. Otherwise, -1.
 */
int
smbd_door_start(void)
{
	int	newfd;

	(void) mutex_lock(&smbd_doorsvc.sd_mutex);

	if (smbd_door_fd != -1) {
		(void) fprintf(stderr, "smb_doorsrv_start: already started");
		(void) mutex_unlock(&smbd_doorsvc.sd_mutex);
		return (-1);
	}

	smbd_door_init(&smbd_door_sdh, "doorsrv");

	list_create(&smbd_doorsvc.sd_async_list, sizeof (smbd_arg_t),
	    offsetof(smbd_arg_t, lnd));
	smbd_doorsvc.sd_async_count = 0;

	if ((smbd_door_fd = door_create(smbd_door_dispatch,
	    &smbd_door_cookie, DOOR_UNREF)) < 0) {
		(void) fprintf(stderr, "smb_doorsrv_start: door_create: %s",
		    strerror(errno));
		smbd_door_fd = -1;
		(void) mutex_unlock(&smbd_doorsvc.sd_mutex);
		return (-1);
	}

	(void) unlink(SMBD_DOOR_NAME);

	if ((newfd = creat(SMBD_DOOR_NAME, 0644)) < 0) {
		(void) fprintf(stderr, "smb_doorsrv_start: open: %s",
		    strerror(errno));
		(void) door_revoke(smbd_door_fd);
		smbd_door_fd = -1;
		(void) mutex_unlock(&smbd_doorsvc.sd_mutex);
		return (-1);
	}

	(void) close(newfd);
	(void) fdetach(SMBD_DOOR_NAME);

	if (fattach(smbd_door_fd, SMBD_DOOR_NAME) < 0) {
		(void) fprintf(stderr, "smb_doorsrv_start: fattach: %s",
		    strerror(errno));
		(void) door_revoke(smbd_door_fd);
		smbd_door_fd = -1;
		(void) mutex_unlock(&smbd_doorsvc.sd_mutex);
		return (-1);
	}

	(void) mutex_unlock(&smbd_doorsvc.sd_mutex);
	return (smbd_door_fd);
}

/*
 * Stop the smbd door service.
 */
void
smbd_door_stop(void)
{
	(void) mutex_lock(&smbd_doorsvc.sd_mutex);

	smbd_door_fini(&smbd_door_sdh);

	if (smbd_door_fd != -1) {
		(void) fdetach(SMBD_DOOR_NAME);
		(void) door_revoke(smbd_door_fd);
		smbd_door_fd = -1;
	}

	(void) mutex_unlock(&smbd_doorsvc.sd_mutex);
}

/*ARGSUSED*/
static void
smbd_door_dispatch(void *cookie, char *argp, size_t arg_size, door_desc_t *dp,
    uint_t n_desc)
{
	smbd_arg_t	dop_arg;
	smb_doorhdr_t	*hdr;
	size_t		hdr_size;
	char		*rbuf = NULL;

	smbd_door_enter(&smbd_door_sdh);

	if (!smbd_online())
		smbd_door_return(&smbd_door_sdh, NULL, 0, NULL, 0);

	bzero(&dop_arg, sizeof (smbd_arg_t));
	hdr = &dop_arg.hdr;
	hdr_size = xdr_sizeof(smb_doorhdr_xdr, hdr);

	if ((cookie != &smbd_door_cookie) || (argp == NULL) ||
	    (arg_size < hdr_size)) {
		smbd_door_return(&smbd_door_sdh, NULL, 0, NULL, 0);
	}

	if (smb_doorhdr_decode(hdr, (uint8_t *)argp, hdr_size) == -1) {
		smbd_log(LOG_DEBUG, "smbd_door_dispatch: header decode failed");
		smbd_door_return(&smbd_door_sdh, NULL, 0, NULL, 0);
	}

	if ((hdr->dh_magic != SMB_DOOR_HDR_MAGIC) || (hdr->dh_txid == 0)) {
		smbd_log(LOG_DEBUG, "smbd_door_dispatch: invalid header");
		smbd_door_return(&smbd_door_sdh, NULL, 0, NULL, 0);
	}

	dop_arg.opname = smb_doorhdr_opname(hdr->dh_op);
	dop_arg.data = argp + hdr_size;
	dop_arg.datalen = hdr->dh_datalen;

	if (hdr->dh_op == SMB_DR_ASYNC_RESPONSE) {
		/*
		 * ASYNC_RESPONSE is used to collect the response
		 * to an async call; it cannot be an async call.
		 */
		hdr->dh_flags &= ~SMB_DF_ASYNC;
	}

	if (hdr->dh_flags & SMB_DF_ASYNC) {
		if (smbd_door_dispatch_async(&dop_arg) == 0)
			hdr->dh_door_rc = SMB_DOP_SUCCESS;
		else
			hdr->dh_door_rc = SMB_DOP_NOT_CALLED;
	} else {
		(void) smbd_door_dispatch_op(&dop_arg);
	}

	if ((rbuf = (char *)alloca(dop_arg.rsize + hdr_size)) == NULL) {
		errno = ENOMEM;
		smbd_log(LOG_DEBUG, "smbd_door_dispatch[%s]: alloca failed",
		    dop_arg.opname);
		smbd_door_return(&smbd_door_sdh, NULL, 0, NULL, 0);
	}

	if (dop_arg.rbuf != NULL) {
		(void) memcpy(rbuf + hdr_size, dop_arg.rbuf, dop_arg.rsize);
		free(dop_arg.rbuf);
	}

	hdr->dh_datalen = dop_arg.rsize;
	(void) smb_doorhdr_encode(hdr, (uint8_t *)rbuf, hdr_size);
	dop_arg.rsize += hdr_size;

	smbd_door_return(&smbd_door_sdh, rbuf, dop_arg.rsize, NULL, 0);
	/*NOTREACHED*/
}

/*
 * Launch a thread to process an asynchronous door call.
 */
static int
smbd_door_dispatch_async(smbd_arg_t *req_arg)
{
	smbd_arg_t	*arg = NULL;
	char		*data = NULL;
	int		rc;

	if ((req_arg->hdr.dh_flags & SMB_DF_ASYNC) == 0) {
		errno = EINVAL;
		return (-1);
	}

	if ((arg = malloc(sizeof (smbd_arg_t))) == NULL) {
		smbd_log(LOG_DEBUG, "smbd_door_dispatch_async[%s]: %s",
		    req_arg->opname, strerror(errno));
		return (-1);
	}

	(void) memcpy(arg, req_arg, sizeof (smbd_arg_t));
	arg->data = NULL;

	if (req_arg->datalen != 0) {
		if ((data = malloc(req_arg->datalen)) == NULL) {
			free(arg);
			smbd_log(LOG_DEBUG, "smbd_door_dispatch_async[%s]: %s",
			    req_arg->opname, strerror(errno));
			return (-1);
		}

		(void) memcpy(data, req_arg->data, req_arg->datalen);
		arg->data = data;
	}

	(void) mutex_lock(&smbd_doorsvc.sd_mutex);
	arg->magic = SMBD_ARG_MAGIC;
	list_insert_tail(&smbd_doorsvc.sd_async_list, arg);
	++smbd_doorsvc.sd_async_count;
	(void) mutex_unlock(&smbd_doorsvc.sd_mutex);

	rc = smbd_thread_run("smbd_door_dispatch_async",
	    smbd_door_dispatch_op, arg);
	if (rc != 0) {
		(void) mutex_lock(&smbd_doorsvc.sd_mutex);
		smbd_door_release_async(arg);
		(void) mutex_unlock(&smbd_doorsvc.sd_mutex);
	}

	return (rc);
}

/*
 * Remove an entry from the async response pending list and free
 * the arg and associated data.
 *
 * Must only be called while holding the smbd_doorsvc mutex.
 */
static void
smbd_door_release_async(smbd_arg_t *arg)
{
	if (arg != NULL) {
		assert(arg->magic == SMBD_ARG_MAGIC);
		arg->magic = (uint32_t)~SMBD_ARG_MAGIC;

		list_remove(&smbd_doorsvc.sd_async_list, arg);
		--smbd_doorsvc.sd_async_count;
		free(arg->data);
		arg->data = NULL;
		free(arg);
	}
}

/*
 * All door calls are processed here: synchronous or asynchronous:
 * - synchronous calls are invoked by direct function call
 * - asynchronous calls are invoked from a launched thread
 *
 * If the kernel has attempted to collect a response before the op
 * has completed, the arg will have been marked as response_abort
 * and we can discard the response data and release the arg.
 *
 * We send a notification when asynchronous (ASYNC) door calls
 * from the kernel (SYSSPACE) have completed.
 */
static void *
smbd_door_dispatch_op(void *thread_arg)
{
	smbd_arg_t	*arg = (smbd_arg_t *)thread_arg;
	smbd_doorop_t	*doorop;
	smb_doorhdr_t	*hdr;
	int		i;

	if ((!smbd_online()) || arg == NULL)
		return (NULL);

	hdr = &arg->hdr;
	arg->opname = smb_doorhdr_opname(hdr->dh_op);

	for (i = 0; i < smbd_ndoorop; ++i) {
		doorop = &smbd_doorops[i];

		if (hdr->dh_op == doorop->opcode) {
			hdr->dh_door_rc = doorop->op(arg);

			if ((hdr->dh_flags & SMB_DF_SYSSPACE) &&
			    (hdr->dh_flags & SMB_DF_ASYNC)) {
				assert(hdr->dh_op != SMB_DR_ASYNC_RESPONSE);

				(void) mutex_lock(&smbd_doorsvc.sd_mutex);
				if (arg->response_abort) {
					free(arg->rbuf);
					arg->rbuf = NULL;
					smbd_door_release_async(arg);
				} else {
					arg->response_ready = B_TRUE;
				}
				(void) mutex_unlock(&smbd_doorsvc.sd_mutex);

				(void) smb_kmod_event_notify(hdr->dh_txid);
			}

			return (NULL);
		}
	}

	smbd_log(LOG_ERR, "smbd_door_dispatch_op[%s]: invalid op %u",
	    arg->opname, hdr->dh_op);
	return (NULL);
}

/*
 * Wrapper for door_return.  smbd_door_enter() increments a reference count
 * when a door call is dispatched and smbd_door_return() decrements the
 * reference count when it completes.
 *
 * The reference counting is used in smbd_door_fini() to wait for active
 * calls to complete before closing the door.
 */
void
smbd_door_init(smbd_door_t *sdh, const char *name)
{
	(void) strlcpy(sdh->sd_name, name, SMBD_DOOR_NAMESZ);
}

void
smbd_door_enter(smbd_door_t *sdh)
{
	(void) mutex_lock(&sdh->sd_mutex);
	++sdh->sd_ncalls;
	(void) mutex_unlock(&sdh->sd_mutex);
}

/*
 * We have two calls to door_return because the first call (with data)
 * can fail, which can leave the door call blocked here.  The second
 * call (with NULL) is guaranteed to unblock and return to the caller.
 */
void
smbd_door_return(smbd_door_t *sdh, char *data_ptr, size_t data_size,
    door_desc_t *desc_ptr, uint_t num_desc)
{
	(void) mutex_lock(&sdh->sd_mutex);

	if (sdh->sd_ncalls == 0)
		smbd_log(LOG_ERR, "smbd_door_return[%s]: unexpected count=0",
		    sdh->sd_name);
	else
		--sdh->sd_ncalls;

	(void) cond_broadcast(&sdh->sd_cv);
	(void) mutex_unlock(&sdh->sd_mutex);

	(void) door_return(data_ptr, data_size, desc_ptr, num_desc);
	(void) door_return(NULL, 0, NULL, 0);
	/* NOTREACHED */
}

/*
 * A door service is about to terminate.
 * Give active requests a small grace period to complete.
 */
void
smbd_door_fini(smbd_door_t *sdh)
{
	timestruc_t	delay;
	int		rc = 0;

	(void) mutex_lock(&sdh->sd_mutex);

	while (rc != ETIME && sdh->sd_ncalls != 0) {
		delay.tv_sec = 1;
		delay.tv_nsec = 0;
		rc = cond_reltimedwait(&sdh->sd_cv, &sdh->sd_mutex, &delay);
	}

	if (sdh->sd_ncalls != 0)
		smbd_log(LOG_NOTICE, "smbd_door_fini[%s]: %d remaining",
		    sdh->sd_name, sdh->sd_ncalls);

	(void) mutex_unlock(&sdh->sd_mutex);
}

/*
 * Null door operation: always returns success.
 * Assumes no request or response data.
 */
/*ARGSUSED*/
static int
smbd_dop_null(smbd_arg_t *arg)
{
	return (SMB_DOP_SUCCESS);
}

/*
 * Async response handler: setup the rbuf and rsize for the specified
 * transaction.  This function is used by the kernel to collect the
 * response half of an asynchronous door call.
 *
 * If a door client attempts to collect a response before the op has
 * completed (!response_ready), mark the arg as response_abort and
 * set an error.  The response will be discarded when the op completes.
 */
static int
smbd_dop_async_response(smbd_arg_t *rsp_arg)
{
	list_t		*arg_list = &smbd_doorsvc.sd_async_list;
	smbd_arg_t	*arg;
	int		rc;

	(void) mutex_lock(&smbd_doorsvc.sd_mutex);
	arg = list_head(arg_list);

	while (arg != NULL) {
		assert(arg->magic == SMBD_ARG_MAGIC);

		if (arg->hdr.dh_txid == rsp_arg->hdr.dh_txid) {
			if (!arg->response_ready) {
				arg->response_abort = B_TRUE;
				rc = SMB_DOP_NOT_CALLED;
				smbd_log(LOG_DEBUG, "doorsvc[%s]: %u not ready",
				    arg->opname, arg->hdr.dh_txid);
				break;
			}

			rc = arg->hdr.dh_door_rc;
			rsp_arg->rbuf = arg->rbuf;
			rsp_arg->rsize = arg->rsize;
			arg->rbuf = NULL;
			arg->rsize = 0;
			smbd_door_release_async(arg);
			break;
		}

		arg = list_next(arg_list, arg);
	}

	(void) mutex_unlock(&smbd_doorsvc.sd_mutex);
	return (rc);
}

/*
 * Upon encode error, the newly created smbd_session_t should be cleaned up.
 */
static int
smbd_dop_session_create(smbd_arg_t *arg)
{
	smb_sessionreq_t	request;
	smb_sessionrsp_t	response;

	bzero(&request, sizeof (smb_sessionreq_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_sessionreq_xdr, &request) != 0)
		return (SMB_DOP_DECODE_ERROR);

	smbd_session_connect(&request, &response);
	arg->rbuf = smb_common_encode(&response, smb_sessionrsp_xdr,
	    &arg->rsize);
	free(response.s_secblob.val);

	if (arg->rbuf == NULL) {
		smbd_session_disconnect(request.s_session_id);
		return (SMB_DOP_ENCODE_ERROR);
	}

	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_session_destroy(smbd_arg_t *arg)
{
	uint64_t session_id = 0;

	if (smb_common_decode(arg->data, arg->datalen,
	    xdr_uint64_t, &session_id) != 0)
		return (SMB_DOP_DECODE_ERROR);

	smbd_session_disconnect(session_id);
	return (SMB_DOP_SUCCESS);
}

/*
 * Upon encode error, the newly created smbd_user_t object should be cleaned
 * up.
 */
static int
smbd_dop_user_auth(smbd_arg_t *arg)
{
	smb_authreq_t	*authreq;
	smb_authrsp_t	authrsp;
	uint64_t	session_id;
	uint16_t	user_id;

	bzero(&authrsp, sizeof (smb_authrsp_t));

	authreq = smbd_authreq_decode((uint8_t *)arg->data,
	    arg->datalen);
	if (authreq == NULL)
		return (SMB_DOP_DECODE_ERROR);

	smbd_user_auth(authreq, &authrsp);
	arg->rbuf = (char *)smbd_authrsp_encode(&authrsp, &arg->rsize);
	session_id = authreq->au_session_id;
	user_id = authreq->au_user_id;
	smbd_authreq_free(authreq);
	smbd_authrsp_free(&authrsp);

	if (arg->rbuf == NULL) {
		smbd_user_cleanup(session_id, user_id);
		return (SMB_DOP_ENCODE_ERROR);
	}

	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_user_logoff(smbd_arg_t *arg)
{
	smb_logoff_t	logoff;

	bzero(&logoff, sizeof (smb_logoff_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_logoff_xdr, &logoff) != 0)
		return (SMB_DOP_DECODE_ERROR);

	smbd_user_logoff(&logoff);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_lookup_name(smbd_arg_t *arg)
{
	smb_domain_t	dinfo;
	smb_account_t	ainfo;
	lsa_account_t	acct;
	char		buf[MAXNAMELEN];

	bzero(&acct, sizeof (lsa_account_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    lsa_account_xdr, &acct) != 0)
		return (SMB_DOP_DECODE_ERROR);

	if (*acct.a_domain == '\0')
		(void) snprintf(buf, MAXNAMELEN, "%s", acct.a_name);
	else if (strchr(acct.a_domain, '.') != NULL)
		(void) snprintf(buf, MAXNAMELEN, "%s@%s", acct.a_name,
		    acct.a_domain);
	else
		(void) snprintf(buf, MAXNAMELEN, "%s\\%s", acct.a_domain,
		    acct.a_name);

	acct.a_status = lsa_lookup_name(buf, acct.a_sidtype, &ainfo);
	if (acct.a_status == NT_STATUS_SUCCESS) {
		acct.a_sidtype = ainfo.a_type;
		smb_sid_tostr(ainfo.a_sid, acct.a_sid);
		(void) strlcpy(acct.a_name, ainfo.a_name, MAXNAMELEN);

		if (smb_domain_lookup_name(ainfo.a_domain, &dinfo))
			(void) strlcpy(acct.a_domain, dinfo.di_fqname,
			    MAXNAMELEN);
		else
			(void) strlcpy(acct.a_domain, ainfo.a_domain,
			    MAXNAMELEN);
		smb_account_free(&ainfo);
	}

	arg->rbuf = smb_common_encode(&acct, lsa_account_xdr, &arg->rsize);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_lookup_sid(smbd_arg_t *arg)
{
	smb_domain_t	dinfo;
	smb_account_t	ainfo;
	lsa_account_t	acct;
	smb_sid_t	*sid;

	bzero(&acct, sizeof (lsa_account_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    lsa_account_xdr, &acct) != 0)
		return (SMB_DOP_DECODE_ERROR);

	sid = smb_sid_fromstr(acct.a_sid);
	acct.a_status = lsa_lookup_sid(sid, &ainfo);
	smb_sid_free(sid);

	if (acct.a_status == NT_STATUS_SUCCESS) {
		acct.a_sidtype = ainfo.a_type;
		smb_sid_tostr(ainfo.a_sid, acct.a_sid);
		(void) strlcpy(acct.a_name, ainfo.a_name, MAXNAMELEN);

		if (smb_domain_lookup_name(ainfo.a_domain, &dinfo))
			(void) strlcpy(acct.a_domain, dinfo.di_fqname,
			    MAXNAMELEN);
		else
			(void) strlcpy(acct.a_domain, ainfo.a_domain,
			    MAXNAMELEN);

		smb_account_free(&ainfo);
	}

	arg->rbuf = smb_common_encode(&acct, lsa_account_xdr, &arg->rsize);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

/* Trigger a DC discovery */
static int
smbd_dop_locate_dc(smbd_arg_t *arg)
{
	smb_joininfo_t	jdi;
	uint32_t	status;

	bzero(&jdi, sizeof (smb_joininfo_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_joininfo_xdr, &jdi) != 0)
		return (SMB_DOP_DECODE_ERROR);

	status = smbd_discover_dc(&jdi);

	arg->rbuf = smb_common_encode(&status, xdr_uint32_t, &arg->rsize);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_join(smbd_arg_t *arg)
{
	smb_joininfo_t	jdi;
	uint32_t	status;

	bzero(&jdi, sizeof (smb_joininfo_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_joininfo_xdr, &jdi) != 0)
		return (SMB_DOP_DECODE_ERROR);

	status = smbd_join(&jdi);

	arg->rbuf = smb_common_encode(&status, xdr_uint32_t, &arg->rsize);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_get_dcinfo(smbd_arg_t *arg)
{
	smb_domainex_t	dxi;

	if (!smb_domain_getinfo(&dxi))
		return (SMB_DOP_EMPTYBUF);

	arg->rbuf = smb_string_encode(dxi.d_dc, &arg->rsize);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_get_fqdn(smbd_arg_t *arg)
{
	smb_domainex_t	dxi;

	if (!smb_domain_getinfo(&dxi))
		return (SMB_DOP_EMPTYBUF);

	arg->rbuf = smb_string_encode(dxi.d_primary.di_fqname, &arg->rsize);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

/*
 * Return the number of snapshots for a dataset
 */
static int
smbd_dop_vss_get_count(smbd_arg_t *arg)
{
	smb_string_t	path;
	uint32_t	count;

	bzero(&path, sizeof (smb_string_t));
	arg->rbuf = NULL;

	if (smb_string_decode(&path, arg->data, arg->datalen) != 0)
		return (SMB_DOP_DECODE_ERROR);

	if (smbd_vss_get_count(path.buf, &count) == 0)
		arg->rbuf = smb_common_encode(&count, xdr_uint32_t,
		    &arg->rsize);

	xdr_free(smb_string_xdr, (char *)&path);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

/*
 * Return the count and list of snapshots.
 * The list is in @GMT token format.
 */
static int
smbd_dop_vss_get_snapshots(smbd_arg_t *arg)
{
	char				**gmtp;
	smb_gmttoken_query_t		request;
	smb_gmttoken_response_t		reply;
	uint_t				i;

	bzero(&request, sizeof (smb_gmttoken_query_t));
	bzero(&reply, sizeof (smb_gmttoken_response_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_gmttoken_query_xdr, &request) != 0)
		return (SMB_DOP_DECODE_ERROR);

	reply.gtr_gmttokens.gtr_gmttokens_val = malloc(request.gtq_count *
	    sizeof (char *));
	bzero(reply.gtr_gmttokens.gtr_gmttokens_val, request.gtq_count *
	    sizeof (char *));

	if (reply.gtr_gmttokens.gtr_gmttokens_val == NULL) {
		xdr_free(smb_gmttoken_query_xdr, (char *)&request);
		return (SMB_DOP_EMPTYBUF);
	}

	smbd_vss_get_snapshots(request.gtq_path, request.gtq_count,
	    &reply.gtr_count,
	    &reply.gtr_gmttokens.gtr_gmttokens_len,
	    reply.gtr_gmttokens.gtr_gmttokens_val);

	arg->rbuf = smb_common_encode(&reply, smb_gmttoken_response_xdr,
	    &arg->rsize);
	if (arg->rbuf == NULL) {
		xdr_free(smb_gmttoken_query_xdr, (char *)&request);
		return (SMB_DOP_ENCODE_ERROR);
	}

	for (i = 0, gmtp = reply.gtr_gmttokens.gtr_gmttokens_val;
	    (i < request.gtq_count); i++) {
		if (*gmtp)
			free(*gmtp);
		gmtp++;
	}

	free(reply.gtr_gmttokens.gtr_gmttokens_val);
	xdr_free(smb_gmttoken_query_xdr, (char *)&request);
	return (SMB_DOP_SUCCESS);
}

/*
 * Return the name of the snapshot that matches the dataset path
 * and @GMT token.
 */
static int
smbd_dop_vss_map_gmttoken(smbd_arg_t *arg)
{
	char			*snapname;
	smb_gmttoken_snapname_t	request;

	bzero(&request, sizeof (smb_gmttoken_snapname_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_gmttoken_snapname_xdr, &request) != 0) {
		return (SMB_DOP_DECODE_ERROR);
	}

	if ((snapname = malloc(MAXPATHLEN)) == NULL) {
		xdr_free(smb_gmttoken_snapname_xdr, (char *)&request);
		return (NULL);
	}

	if ((smbd_vss_map_gmttoken(request.gts_path, request.gts_gmttoken,
	    snapname) != 0)) {
		*snapname = '\0';
	}

	arg->rbuf = smb_string_encode(snapname, &arg->rsize);
	xdr_free(smb_gmttoken_snapname_xdr, (char *)&request);
	free(snapname);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_ads_find_host(smbd_arg_t *arg)
{
	smb_ads_host_info_t	*hinfo = NULL;
	char			*hostname = "";
	smb_string_t		fqdn;

	bzero(&fqdn, sizeof (smb_string_t));

	if (smb_string_decode(&fqdn, arg->data, arg->datalen) != 0)
		return (SMB_DOP_DECODE_ERROR);

	if ((hinfo = smb_ads_find_host(fqdn.buf, NULL)) != NULL)
		hostname = hinfo->name;

	xdr_free(smb_string_xdr, (char *)&fqdn);

	arg->rbuf = smb_string_encode(hostname, &arg->rsize);
	free(hinfo);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

/*
 * Query the list of user/group quota entries for a given filesystem.
 */
static int
smbd_dop_quota_query(smbd_arg_t *arg)
{
	smb_quota_query_t	request;
	smb_quota_response_t	reply;
	uint32_t		status;

	bzero(&request, sizeof (smb_quota_query_t));
	bzero(&reply, sizeof (smb_quota_response_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_quota_query_xdr, &request) != 0)
		return (SMB_DOP_DECODE_ERROR);

	status = smbd_quota_query(&request, &reply);
	reply.qr_status = status;

	arg->rbuf = smb_common_encode(&reply, smb_quota_response_xdr,
	    &arg->rsize);

	xdr_free(smb_quota_query_xdr, (char *)&request);
	smbd_quota_free(&reply);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

/*
 * Set a list of user/group quota entries for a given filesystem.
 */
static int
smbd_dop_quota_set(smbd_arg_t *arg)
{
	smb_quota_set_t	request;
	uint32_t	status = 0;

	bzero(&request, sizeof (smb_quota_set_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_quota_set_xdr, &request) != 0)
		return (SMB_DOP_DECODE_ERROR);

	status = smbd_quota_set(&request);

	arg->rbuf = smb_common_encode(&status, xdr_uint32_t, &arg->rsize);
	xdr_free(smb_quota_set_xdr, (char *)&request);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_dfs_get_referrals(smbd_arg_t *arg)
{
	dfs_referral_query_t	request;
	dfs_referral_response_t	reply;

	bzero(&request, sizeof (request));
	bzero(&reply, sizeof (reply));

	if (smb_common_decode(arg->data, arg->datalen,
	    dfs_referral_query_xdr, &request) != 0)
		return (SMB_DOP_DECODE_ERROR);

	reply.rp_status = dfs_get_referrals((const char *)request.rq_path,
	    request.rq_type, &reply.rp_referrals);

	if (reply.rp_status != ERROR_SUCCESS)
		bzero(&reply.rp_referrals, sizeof (dfs_info_t));

	arg->rbuf = smb_common_encode(&reply, dfs_referral_response_xdr,
	    &arg->rsize);

	if (reply.rp_status == ERROR_SUCCESS)
		dfs_info_free(&reply.rp_referrals);

	xdr_free(dfs_referral_query_xdr, (char *)&request);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_shr_hostaccess(smbd_arg_t *arg)
{
	smb_shr_hostaccess_query_t request;
	uint32_t reply;

	bzero(&request, sizeof (request));
	bzero(&reply, sizeof (reply));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_shr_hostaccess_query_xdr, &request) != 0)
		return (SMB_DOP_DECODE_ERROR);

	reply = smbd_share_hostaccess(&request.shq_ipaddr, request.shq_none,
	    request.shq_ro, request.shq_rw, request.shq_flag);

	arg->rbuf = smb_common_encode(&reply, xdr_uint32_t, &arg->rsize);

	xdr_free(smb_shr_hostaccess_query_xdr, (char *)&request);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_shr_exec(smbd_arg_t *arg)
{
	smb_shr_execinfo_t request;
	int reply;

	bzero(&request, sizeof (request));
	bzero(&reply, sizeof (reply));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_shr_execinfo_xdr, &request) != 0)
		return (SMB_DOP_DECODE_ERROR);

	reply = smbd_share_exec(&request);

	if (reply != 0)
		smbd_log(LOG_NOTICE, "Failed to execute %s command",
		    (request.e_type == SMB_EXEC_MAP) ? "map" : "unmap");

	arg->rbuf = smb_common_encode(&reply, xdr_int, &arg->rsize);

	xdr_free(smb_shr_execinfo_xdr, (char *)&request);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_shr_notify(smbd_arg_t *arg)
{
	smb_shr_notify_t request;

	bzero(&request, sizeof (smb_shr_notify_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_shr_notify_xdr, &request) != 0)
		return (SMB_DOP_DECODE_ERROR);

	switch (request.sn_op) {
	case SMB_SHARE_NOP_PUBLISH:
		smbd_share_publish(&request);
		break;
	case SMB_SHARE_NOP_UNPUBLISH:
		smbd_share_unpublish(&request);
		break;
	case SMB_SHARE_NOP_REPUBLISH:
		smbd_share_republish(&request);
		break;
	default:
		break;
	}

	xdr_free(smb_shr_notify_xdr, (char *)&request);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_shr_publish_admin(smbd_arg_t *arg)
{
	smb_string_t	mntpnt;
	uint32_t	status;

	bzero(&mntpnt, sizeof (smb_string_t));

	if (smb_string_decode(&mntpnt, arg->data, arg->datalen) != 0)
		return (SMB_DOP_DECODE_ERROR);

	status = smbd_share_publish_admin(mntpnt.buf);

	xdr_free(smb_string_xdr, (char *)&mntpnt);

	arg->rbuf = smb_common_encode(&status, xdr_uint32_t, &arg->rsize);
	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

static int
smbd_dop_spooldoc(smbd_arg_t *arg)
{
	smb_spooldoc_t	request;

	bzero(&request, sizeof (smb_spooldoc_t));

	if (smb_common_decode(arg->data, arg->datalen,
	    smb_spooldoc_xdr, &request) != 0)
		return (SMB_DOP_DECODE_ERROR);

	smbd_spool_document(&request);

	xdr_free(smb_spooldoc_xdr, (char *)&request);
	return (SMB_DOP_SUCCESS);
}

/*
 * Get a list of domains from the domain cache and SMB_CI_DC_SELECTED
 * property if the primary domain exists in the cache.
 */
static int
smbd_dop_get_domains_info(smbd_arg_t *arg)
{
	smb_domains_info_t	domains_info;

	bzero(&domains_info, sizeof (smb_domains_info_t));

	domains_info.d_status =
	    smb_domains_getinfo(&domains_info);

	arg->rbuf = smb_common_encode(&domains_info, smb_domains_info_xdr,
	    &arg->rsize);

	smb_domains_info_free(&domains_info.d_domain_list);

	if (arg->rbuf == NULL)
		return (SMB_DOP_ENCODE_ERROR);
	return (SMB_DOP_SUCCESS);
}

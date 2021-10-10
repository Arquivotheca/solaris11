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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * SMBFS I/O Daemon (Per-user IOD)
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/note.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <synch.h>
#include <time.h>
#include <unistd.h>
#include <ucred.h>
#include <err.h>
#include <door.h>
#include <libscf.h>
#include <locale.h>
#include <thread.h>

#include <netsmb/smb_lib.h>

#define	DPRINT(...)	do \
{ \
	if (smb_debug) \
		(void) fprintf(stderr, __VA_ARGS__); \
	_NOTE(CONSTCOND) \
} while (0)

mutex_t	iod_mutex = DEFAULTMUTEX;
int iod_thr_count;	/* threads, excluding main */
int iod_terminating;
int iod_alarm_time = 30; /* sec. */

void iod_dispatch(void *cookie, char *argp, size_t argsz,
    door_desc_t *dp, uint_t n_desc);
int iod_newvc(smb_iod_ssn_t *clnt_ssn);
void * iod_work(void *arg);

/*ARGSUSED*/
int
main(int argc, char *argv[])
{
	sigset_t oldmask, tmpmask;
	char *env, *door_path = NULL;
	int door_fd = -1;
	int err, sig;
	int rc = SMF_EXIT_ERR_FATAL;
	boolean_t attached = B_FALSE;

	/* set locale and text domain for i18n */
	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	/* Debugging support. */
	if ((env = getenv("SMBFS_DEBUG")) != NULL) {
		smb_debug = atoi(env);
		if (smb_debug < 1)
			smb_debug = 1;
		iod_alarm_time = 300;
	}

	/*
	 * If a user runs this command (i.e. by accident)
	 * don't interfere with any already running IOD.
	 */
	err = smb_iod_open_door(&door_fd);
	if (err == 0) {
		(void) close(door_fd);
		door_fd = -1;
		DPRINT("%s: already running\n", argv[0]);
		exit(SMF_EXIT_OK);
	}

	/*
	 * Want all signals blocked, as we're doing
	 * synchronous delivery via sigwait below.
	 */
	(void) sigfillset(&tmpmask);
	(void) sigprocmask(SIG_BLOCK, &tmpmask, &oldmask);

	/* Setup the door service. */
	door_path = smb_iod_door_path();
	door_fd = door_create(iod_dispatch, NULL,
	    DOOR_REFUSE_DESC | DOOR_NO_CANCEL);
	if (door_fd == -1) {
		perror("iod door_create");
		goto out;
	}
	(void) fdetach(door_path);
	if (fattach(door_fd, door_path) < 0) {
		(void) fprintf(stderr, "%s: fattach failed, %s\n",
		    door_path, strerror(errno));
		goto out;
	}
	attached = B_TRUE;

	/* Initializations done. */
	rc = SMF_EXIT_OK;

	/*
	 * Post the initial alarm, and then just
	 * wait for signals.
	 */
	(void) alarm(iod_alarm_time);
again:
	sig = sigwait(&tmpmask);
	DPRINT("main: sig=%d\n", sig);
	switch (sig) {
	case SIGCONT:
		goto again;

	case SIGALRM:
		/* No threads active for a while. */
		(void) mutex_lock(&iod_mutex);
		if (iod_thr_count > 0) {
			/*
			 * Door call thread creation raced with
			 * the alarm.  Ignore this alaram.
			 */
			(void) mutex_unlock(&iod_mutex);
			goto again;
		}
		/* Prevent a race with iod_thr_count */
		iod_terminating = 1;
		(void) mutex_unlock(&iod_mutex);
		break;

	case SIGINT:
	case SIGTERM:
		break;	/* normal termination */

	default:
		/* Unexpected signal. */
		(void) fprintf(stderr, "iod_main: unexpected sig=%d\n", sig);
		break;
	}

out:
	iod_terminating = 1;
	if (attached)
		(void) fdetach(door_path);
	if (door_fd != -1)
		(void) door_revoke(door_fd);

	/*
	 * We need a reference in -lumem to satisfy check_rtime,
	 * else we get build hoise.  This is sufficient.
	 */
	free(NULL);

	return (rc);
}

/*ARGSUSED*/
void
iod_dispatch(void *cookie, char *argp, size_t argsz,
    door_desc_t *dp, uint_t n_desc)
{
	smb_iod_ssn_t *ssn;
	ucred_t *ucred;
	uid_t cl_uid;
	int rc;

	/*
	 * Verify that the calling process has the same UID.
	 * Paranoia:  The door we created has mode 0600, so
	 * this check is probably redundant.
	 */
	ucred = NULL;
	if (door_ucred(&ucred) != 0) {
		rc = EACCES;
		goto out;
	}
	cl_uid = ucred_getruid(ucred);
	ucred_free(ucred);
	ucred = NULL;
	if (cl_uid != getuid()) {
		DPRINT("iod_dispatch: wrong UID\n");
		rc = EACCES;
		goto out;
	}

	/*
	 * The library uses a NULL arg call to check if
	 * the daemon is running.  Just return zero.
	 */
	if (argp == NULL) {
		rc = 0;
		goto out;
	}

	/*
	 * Otherwise, the arg must be the (fixed size)
	 * smb_iod_ssn_t
	 */
	if (argsz != sizeof (*ssn)) {
		rc = EINVAL;
		goto out;
	}

	(void) mutex_lock(&iod_mutex);
	if (iod_terminating) {
		(void) mutex_unlock(&iod_mutex);
		DPRINT("iod_dispatch: terminating\n");
		rc = EINTR;
		goto out;
	}
	if (iod_thr_count++ == 0) {
		(void) alarm(0);
		DPRINT("iod_dispatch: cancelled alarm\n");
	}
	(void) mutex_unlock(&iod_mutex);

	ssn = (void *) argp;
	rc = iod_newvc(ssn);

	(void) mutex_lock(&iod_mutex);
	if (--iod_thr_count == 0) {
		DPRINT("iod_dispatch: schedule alarm\n");
		(void) alarm(iod_alarm_time);
	}
	(void) mutex_unlock(&iod_mutex);

out:
	(void) door_return((void *)&rc, sizeof (rc), NULL, 0);
	(void) door_return(NULL, 0, NULL, 0);
	/* NOTREACHED */
}

/*
 * Try making a connection with the server described by
 * the info in the smb_iod_ssn_t arg.  If successful,
 * start an IOD thread to service it, then return to
 * the client side of the door.
 */
int
iod_newvc(smb_iod_ssn_t *clnt_ssn)
{
	smb_ctx_t *ctx;
	thread_t tid;
	int err;


	/*
	 * This needs to essentially "clone" the smb_ctx_t
	 * from the client side of the door, or at least
	 * as much of it as we need while creating a VC.
	 */
	err = smb_ctx_alloc(&ctx);
	if (err)
		return (err);
	bcopy(clnt_ssn, &ctx->ct_iod_ssn, sizeof (ctx->ct_iod_ssn));

	/*
	 * Do the initial connection setup here, so we can
	 * report the outcome to the door client.
	 */
	err = smb_iod_connect(ctx);
	if (err != 0)
		goto out;

	/*
	 * Create the driver session now, so we don't
	 * race with the door client findvc call.
	 */
	if ((err = smb_ctx_gethandle(ctx)) != 0)
		goto out;
	if (ioctl(ctx->ct_dev_fd, SMBIOC_SSN_CREATE, &ctx->ct_ssn) < 0) {
		err = errno;
		goto out;
	}

	/* The rest happens in the iod_work thread. */
	err = thr_create(NULL, 0, iod_work, ctx, THR_DETACHED, &tid);
	if (err == 0) {
		/*
		 * Given to the new thread.
		 * free at end of iod_work
		 */
		ctx = NULL;
	}

out:
	if (ctx)
		smb_ctx_free(ctx);

	return (err);
}

/*
 * Be the reader thread for some VC.
 *
 * This is started by a door call thread, which means
 * this is always at least the 2nd thread, therefore
 * it should never see thr_count==0 or terminating.
 */
void *
iod_work(void *arg)
{
	smb_ctx_t *ctx = arg;

	(void) mutex_lock(&iod_mutex);
	if (iod_thr_count++ == 0) {
		(void) alarm(0);
		DPRINT("iod_work: cancelled alarm\n");
	}
	(void) mutex_unlock(&iod_mutex);

	(void) smb_iod_work(ctx);

	(void) mutex_lock(&iod_mutex);
	if (--iod_thr_count == 0) {
		DPRINT("iod_work: schedule alarm\n");
		(void) alarm(iod_alarm_time);
	}
	(void) mutex_unlock(&iod_mutex);

	smb_ctx_free(ctx);
	return (NULL);
}

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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * SMBFS I/O Daemon (SMF service)
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/note.h>
#include <sys/queue.h>

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
#include <wait.h>
#include <priv_utils.h>
#include <err.h>
#include <door.h>
#include <libscf.h>
#include <locale.h>
#include <thread.h>
#include <assert.h>
#include <syslog.h>

#include <netsmb/smb_dev.h>
#include <netsmb/smb_lib.h>
#include <smbsrv/libsmb.h>

#define	SMBFS_DAEMON_NAME	"smbfs"
#define	SMBFS_SMF_BUF_LEN	1024

static boolean_t d_flag = B_FALSE;

/* Keep a list of child processes. */
typedef struct _child {
	LIST_ENTRY(_child) list;
	pid_t pid;
	uid_t uid;
} child_t;
static LIST_HEAD(, _child) child_list = { 0 };
mutex_t	cl_mutex = DEFAULTMUTEX;

static const char smbiod_path[] = "/usr/lib/smbfs/smbiod";
static const char door_path[] = SMBIOD_SVC_DOOR;

void svc_dispatch(void *cookie, char *argp, size_t argsz,
    door_desc_t *dp, uint_t n_desc);
static int cmd_start(uid_t uid, gid_t gid);
static int new_child(uid_t uid, gid_t gid);
static void svc_sigchld(void);
static void child_gone(uid_t, pid_t, int);
static void svc_cleanup(void);

static int smb_move_props(void);
static int smb_prop_copy(smb_scfhandle_t *, smb_cfg_id_t, scf_property_t *,
    scf_value_t *);
static int smb_move_pgrp(smb_scfhandle_t *, const char *);
static smb_scfhandle_t *smb_scf_init(void);
static void smb_scf_fini(smb_scfhandle_t *);

static void smb_upgrade_props(void);
static int smb_prop_del(smb_cfg_id_t);
static int smb_prop_getbool(smb_cfg_id_t, boolean_t *);

static child_t *
child_find_by_pid(pid_t pid)
{
	child_t *cp;

	assert(MUTEX_HELD(&cl_mutex));
	LIST_FOREACH(cp, &child_list, list) {
		if (cp->pid == pid)
			return (cp);
	}
	return (NULL);
}

static child_t *
child_find_by_uid(uid_t uid)
{
	child_t *cp;

	assert(MUTEX_HELD(&cl_mutex));
	LIST_FOREACH(cp, &child_list, list) {
		if (cp->uid == uid)
			return (cp);
	}
	return (NULL);
}

/*
 * Find out if the service is already running.
 * Return: true, false.
 */
static boolean_t
already_running(void)
{
	door_info_t info;
	int fd, rc;

	if ((fd = open(door_path, O_RDONLY)) < 0)
		return (B_FALSE);

	rc = door_info(fd, &info);
	(void) close(fd);
	if (rc < 0)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * This function will fork off a child process,
 * from which only the child will return.
 *
 * The parent exit status is taken as the SMF start method
 * success or failure, so the parent waits (via pipe read)
 * for the child to finish initialization before it exits.
 * Use SMF error codes only on exit.
 */
static int
daemonize_init(void)
{
	int pid, st;
	int pfds[2];

	(void) chdir("/");

	if (pipe(pfds) < 0) {
		perror("pipe");
		exit(SMF_EXIT_ERR_FATAL);
	}
	if ((pid = fork1()) == -1) {
		perror("fork");
		exit(SMF_EXIT_ERR_FATAL);
	}

	/*
	 * If we're the parent process, wait for either the child to send us
	 * the appropriate exit status over the pipe or for the read to fail
	 * (presumably with 0 for EOF if our child terminated abnormally).
	 * If the read fails, exit with either the child's exit status if it
	 * exited or with SMF_EXIT_ERR_FATAL if it died from a fatal signal.
	 */
	if (pid != 0) {
		/* parent */
		(void) close(pfds[1]);
		if (read(pfds[0], &st, sizeof (st)) == sizeof (st))
			_exit(st);
		if (waitpid(pid, &st, 0) == pid && WIFEXITED(st))
			_exit(WEXITSTATUS(st));
		_exit(SMF_EXIT_ERR_FATAL);
	}

	/* child */
	(void) close(pfds[0]);

	return (pfds[1]);
}

static void
daemonize_fini(int pfd, int rc)
{
	/* Tell parent we're ready. */
	(void) write(pfd, &rc, sizeof (rc));
	(void) close(pfd);
}

int
main(int argc, char **argv)
{
	sigset_t oldmask, tmpmask;
	struct sigaction sa;
	struct rlimit rl;
	int door_fd = -1, tmp_fd = -1, pfd = -1;
	int c, sig;
	int rc = SMF_EXIT_ERR_FATAL;
	boolean_t created = B_FALSE, attached = B_FALSE;

	/* set locale and text domain for i18n */
	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	openlog(SMBFS_DAEMON_NAME, LOG_PID | LOG_NOWAIT, LOG_DAEMON);

	while ((c = getopt(argc, argv, "d")) != -1) {
		switch (c) {
		case 'd':
			/* Do debug messages. */
			d_flag = B_TRUE;
			break;
		default:
			(void) fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
			return (SMF_EXIT_ERR_CONFIG);
		}
	}

	if (already_running()) {
		(void) fprintf(stderr, "%s: already running", argv[0]);
		return (rc);
	}

	/*
	 * Raise the fd limit to max
	 * errors here are non-fatal
	 */
	if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
		(void) fprintf(stderr, "getrlimit failed, err %d\n", errno);
	} else if (rl.rlim_cur < rl.rlim_max) {
		rl.rlim_cur = rl.rlim_max;
		if (setrlimit(RLIMIT_NOFILE, &rl) != 0)
			(void) fprintf(stderr, "setrlimit "
			    "RLIMIT_NOFILE %d, err %d",
			    (int)rl.rlim_cur, errno);
	}

	/*
	 * Want all signals blocked, as we're doing
	 * synchronous delivery via sigwait below.
	 */
	(void) sigfillset(&tmpmask);
	(void) sigprocmask(SIG_BLOCK, &tmpmask, &oldmask);

	/*
	 * Do want SIGCHLD, and will waitpid().
	 */
	sa.sa_flags = SA_NOCLDSTOP;
	sa.sa_handler = SIG_DFL;
	(void) sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);

	/*
	 * Daemonize, unless debugging.
	 */
	if (d_flag) {
		/* debug: run in foregound (not a service) */
		(void) putenv("SMBFS_DEBUG=1");
	} else {
		/* Non-debug: start daemon in the background. */
		pfd = daemonize_init();
	}

	/*
	 * Create directory for all smbiod doors.
	 */
	if ((mkdir(SMBIOD_RUNDIR, 0755) < 0) && errno != EEXIST) {
		perror(SMBIOD_RUNDIR);
		goto out;
	}

	/*
	 * Create a file for the main service door.
	 */
	(void) unlink(door_path);
	tmp_fd = open(door_path, O_RDWR|O_CREAT|O_EXCL, 0644);
	if (tmp_fd < 0) {
		perror(door_path);
		goto out;
	}
	(void) close(tmp_fd);
	tmp_fd = -1;
	created = B_TRUE;

	/* Setup the door service. */
	door_fd = door_create(svc_dispatch, NULL,
	    DOOR_REFUSE_DESC | DOOR_NO_CANCEL);
	if (door_fd == -1) {
		perror("svc door_create");
		goto out;
	}
	(void) fdetach(door_path);
	if (fattach(door_fd, door_path) < 0) {
		(void) fprintf(stderr, "%s: fattach failed, %s\n",
		    door_path, strerror(errno));
		goto out;
	}
	attached = B_TRUE;

	if (smb_revision_cmp(SMB_CLNT_REV, SMB_CI_CLNT_REV) > 0) {

		(void) smb_config_setstr(SMB_CI_CLNT_REV, SMB_CLNT_REV);

		if (smb_move_props() != 0) {

			syslog(LOG_ERR, "Failed to migrate all properties"
			    " successfully.");
			syslog(LOG_ERR, "Please manually migrate all failed"
			    " properties before restarting smb/client."
			    "  Failing to do so, would mean smb/client and"
			    " smb/server services will use the system default"
			    " value for such properties as automatic migration"
			    " will not be attempted again.");

			(void) fprintf(stderr, "Property migration failed.\n");
			(void) fprintf(stderr, "Please see syslog for"
			    " details.\n");

			(void) smf_maintain_instance(
			    SMBFS_DEFAULT_INSTANCE_FMRI, 0);
		}
	}

	smb_upgrade_props();

	if (smbfs_pwd_loadkeychain() != 0)
		syslog(LOG_NOTICE, "failed to load entries into password"
		    " keychain for %s", SMBIOD_PWDFILE);

	/*
	 * Initializations done.  Tell start method we're up.
	 */
	rc = SMF_EXIT_OK;
	if (pfd != -1) {
		daemonize_fini(pfd, rc);
		pfd = -1;
	}

	/*
	 * Main thread just waits for signals.
	 */
again:
	sig = sigwait(&tmpmask);
	if (d_flag)
		(void) fprintf(stderr, "main: sig=%d\n", sig);
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		/*
		 * The whole process contract gets a SIGTERM
		 * at once.  Give children a chance to exit
		 * so we can do normal SIGCHLD cleanup.
		 * Prevent new door_open calls.
		 */
		(void) fdetach(door_path);
		attached = B_FALSE;
		(void) alarm(2);
		goto again;
	case SIGALRM:
		break;	/* normal termination */
	case SIGCHLD:
		svc_sigchld();
		goto again;
	case SIGCONT:
		goto again;
	default:
		/* Unexpected signal. */
		(void) fprintf(stderr, "svc_main: unexpected sig=%d\n", sig);
		break;
	}

out:
	if (attached)
		(void) fdetach(door_path);
	if (door_fd != -1)
		(void) door_revoke(door_fd);
	if (created)
		(void) unlink(door_path);

	/* NB: door threads gone now. */
	svc_cleanup();

	/* If startup error, report to parent. */
	if (pfd != -1)
		daemonize_fini(pfd, rc);

	return (rc);
}

/*ARGSUSED*/
void
svc_dispatch(void *cookie, char *argp, size_t argsz,
    door_desc_t *dp, uint_t n_desc)
{
	ucred_t *ucred = NULL;
	uid_t uid;
	gid_t gid;
	int32_t cmd, rc = 0;
	smbfs_passwd_t pwdinfo;

	/*
	 * Allow a NULL arg call to check if this
	 * daemon is running.  Just return zero.
	 */
	if (argp == NULL)
		goto out;

	/*
	 * Get the caller's credentials.
	 * (from client side of door)
	 */
	if (door_ucred(&ucred) != 0) {
		rc = EACCES;
		goto out;
	}
	uid = ucred_getruid(ucred);
	gid = ucred_getrgid(ucred);

	if (smbfs_door_decode(argp, argsz, &cmd, &pwdinfo) != 0) {
		rc = EINVAL;
		goto out;
	}
	switch (cmd) {
	case SMBIOD_START:
		rc = cmd_start(uid, gid);
		break;
	case SMBIOD_PWDFILE_ADD:
		pwdinfo.pw_uid = uid;
		rc = smbfs_pwd_add(&pwdinfo);
		break;
	case SMBIOD_PWDFILE_DEL:
		pwdinfo.pw_uid = uid;
		rc = smbfs_pwd_del(&pwdinfo, B_FALSE);
		break;
	case SMBIOD_PWDFILE_DELALL:
		pwdinfo.pw_uid = uid;
		rc = smbfs_pwd_del(&pwdinfo, B_TRUE);
		break;
	default:
		rc = EINVAL;
		goto out;
	}

out:
	if (ucred != NULL)
		ucred_free(ucred);

	(void) door_return((void *)&rc, sizeof (rc), NULL, 0);
	(void) door_return(NULL, 0, NULL, 0);
	/* NOTREACHED */
}

/*
 * Start a per-user smbiod, if not already running.
 */
int
cmd_start(uid_t uid, gid_t gid)
{
	char door_file[64];
	child_t *cp;
	int pid, fd = -1;

	(void) mutex_lock(&cl_mutex);
	cp = child_find_by_uid(uid);
	if (cp != NULL) {
		/* This UID already has an IOD. */
		(void) mutex_unlock(&cl_mutex);
		if (d_flag) {
			(void) fprintf(stderr, "cmd_start: uid %d"
			    " already has an iod\n", uid);
		}
		return (0);
	}

	/*
	 * OK, create a new child.
	 */
	cp = malloc(sizeof (*cp));
	if (cp == NULL) {
		(void) mutex_unlock(&cl_mutex);
		return (ENOMEM);
	}
	cp->pid = 0; /* update below */
	cp->uid = uid;
	LIST_INSERT_HEAD(&child_list, cp, list);
	(void) mutex_unlock(&cl_mutex);

	/*
	 * The child will not have permission to create or
	 * destroy files in SMBIOD_RUNDIR so do that here.
	 */
	(void) snprintf(door_file, sizeof (door_file),
	    SMBIOD_USR_DOOR, cp->uid);
	(void) unlink(door_file);
	fd = open(door_file, O_RDWR|O_CREAT|O_EXCL, 0600);
	if (fd < 0) {
		perror(door_file);
		goto errout;
	}
	if (fchown(fd, uid, gid) < 0) {
		perror(door_file);
		goto errout;
	}
	(void) close(fd);
	fd = -1;

	if ((pid = fork1()) == -1) {
		perror("fork");
		goto errout;
	}
	if (pid == 0) {
		(void) new_child(uid, gid);
		_exit(1);
	}
	/* parent */
	cp->pid = pid;

	if (d_flag) {
		(void) fprintf(stderr, "cmd_start: uid %d new iod, pid %d\n",
		    uid, pid);
	}

	return (0);

errout:
	if (fd != -1)
		(void) close(fd);
	(void) mutex_lock(&cl_mutex);
	LIST_REMOVE(cp, list);
	(void) mutex_unlock(&cl_mutex);
	free(cp);
	return (errno);
}

/*
 * Assume the passed credentials (from the door client),
 * drop any extra privileges, and exec the per-user iod.
 */
static int
new_child(uid_t uid, gid_t gid)
{
	char *argv[2];
	int flags, rc;

	flags = PU_RESETGROUPS | PU_LIMITPRIVS | PU_INHERITPRIVS;
	rc = __init_daemon_priv(flags, uid, gid, PRIV_NET_ACCESS, NULL);
	if (rc != 0)
		return (errno);

	argv[0] = "smbiod";
	argv[1] = NULL;
	(void) execv(smbiod_path, argv);
	return (errno);
}

static void
svc_sigchld(void)
{
	child_t *cp;
	pid_t pid;
	int err, status, found = 0;

	(void) mutex_lock(&cl_mutex);

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {

		found++;
		if (d_flag)
			(void) fprintf(stderr, "svc_sigchld: pid %d\n",
			    (int)pid);

		cp = child_find_by_pid(pid);
		if (cp == NULL) {
			(void) fprintf(stderr, "Unknown pid %d\n", (int)pid);
			continue;
		}
		child_gone(cp->uid, cp->pid, status);
		LIST_REMOVE(cp, list);
		free(cp);
	}
	err = errno;

	(void) mutex_unlock(&cl_mutex);

	/* ECHILD is the normal end of loop. */
	if (pid < 0 && err != ECHILD)
		(void) fprintf(stderr, "svc_sigchld: waitpid err %d\n", err);
	if (found == 0)
		(void) fprintf(stderr, "svc_sigchld: no children?\n");
}

static void
child_gone(uid_t uid, pid_t pid, int status)
{
	char door_file[64];
	int x;

	if (d_flag)
		(void) fprintf(stderr, "child_gone: uid %d pid %d\n",
		    uid, (int)pid);

	(void) snprintf(door_file, sizeof (door_file),
	    SMBIOD_RUNDIR "/%d", uid);
	(void) unlink(door_file);

	if (WIFEXITED(status)) {
		x = WEXITSTATUS(status);
		if (x != 0) {
			(void) fprintf(stderr, "uid %d, pid %d exit %d",
			    uid, (int)pid, x);
		}
	}
	if (WIFSIGNALED(status)) {
		x = WTERMSIG(status);
		(void) fprintf(stderr, "uid %d, pid %d signal %d",
		    uid, (int)pid, x);
	}
}

/*
 * Final cleanup before exit.  Unlink child doors, etc.
 * Called while single threaded, so no locks needed here.
 * The list is normally empty by now due to svc_sigchld
 * calls during shutdown.  But in case there were any
 * straglers, do cleanup here.  Don't bother freeing any
 * list elements here, as we're exiting.
 */
static void
svc_cleanup(void)
{
	child_t *cp;

	LIST_FOREACH(cp, &child_list, list) {
		child_gone(cp->uid, cp->pid, 0);
	}
}

/*
 * Copy the value of a given config param in server service to new SMB
 * service, network/smb.  The property will be created if it doesn't exist
 * in the new service.
 */
static int
smb_prop_copy(smb_scfhandle_t *hd, smb_cfg_id_t id, scf_property_t *prop,
    scf_value_t *value)
{
	char buf[SMBFS_SMF_BUF_LEN];
	uint8_t bool;
	int64_t num;
	int type;

	if (scf_pg_get_property(hd->scf_pg, smb_config_getname(id), prop)
	    != 0) {
		if (scf_error() == SCF_ERROR_NOT_FOUND)
			return (0);

		return (-1);
	}

	if (scf_property_get_value(prop, value) != 0)
		return (-1);

	type = scf_value_type(value);

	switch (type) {
	case SCF_TYPE_ASTRING:
		if (scf_value_get_astring(value, buf, sizeof (buf)) < 0)
			return (-1);
		if (smb_config_setstr(id, buf) != 0)
			return (-1);
		break;
	case SCF_TYPE_INTEGER:
		if (scf_value_get_integer(value, &num) != 0)
			return (-1);
		if (smb_config_setnum(id, num) != 0)
			return (-1);
		break;
	case SCF_TYPE_BOOLEAN:
		if (scf_value_get_boolean(value, &bool) != 0)
			return (-1);
		if (smb_config_setbool(id, bool) != 0)
			return (-1);
		break;
	default:
		return (-1);
	}

	return (0);
}

/*
 * Copy config params for given property group from server service
 * to new SMB service, nework/smb.  The entire property group and
 * its properties are deleted if all properties are copied to new
 * service.
 */
static int
smb_move_pgrp(smb_scfhandle_t *hd, const char *pg)
{
	scf_value_t	*value = NULL;
	scf_property_t	*prop = NULL;
	int		rc, err = 0, id;
	scf_error_t	scf_err;
	boolean_t	issmbd_pg, isexec_pg, isexec;

	issmbd_pg = isexec_pg = B_FALSE;

	if (strcmp(pg, SMBD_PG_NAME) == 0)
		issmbd_pg = B_TRUE;
	else if (strcmp(pg, SMBD_EXEC_PG_NAME) == 0)
		isexec_pg = B_TRUE;
	else
		return (-1);

	rc = scf_service_get_pg(hd->scf_service, pg, hd->scf_pg);
	if (rc != 0) {
		scf_err = scf_error();
		if (scf_err == SCF_ERROR_NOT_FOUND)
			return (0);

		syslog(LOG_ERR, "Unable to access property group %s for"
		    " migration: %s.", pg, scf_strerror(scf_err));
		return (-1);
	}

	value = scf_value_create(hd->scf_handle);
	prop = scf_property_create(hd->scf_handle);

	if (prop == NULL || value == NULL) {
		if (prop)
			scf_property_destroy(prop);
		if (value)
			scf_value_destroy(value);
		return (-1);
	}

	for (id = 0; id < SMB_CI_MAX; id++) {

		if (id == SMB_CI_MACHINE_PASSWD)
			continue;

		isexec = smb_config_isexec(id);

		if ((issmbd_pg && isexec) || (isexec_pg && !isexec))
			continue;

		if (smb_prop_copy(hd, id, prop, value) != 0) {
			syslog(LOG_ERR, "Failed to migrate %s/%s property.",
			    pg, smb_config_getname(id));
			err = 1;
		}
	}

	scf_value_destroy(value);
	scf_property_destroy(prop);

	if ((err == 0) && (scf_pg_delete(hd->scf_pg)) != 0) {
		syslog(LOG_NOTICE, "Failed to remove property"
		    " group %s.", pg);
		syslog(LOG_NOTICE, "Please remove property group %s in"
		    " svc:/%s service.", pg, SMBD_FMRI_PREFIX);
	}

	return (err == 0 ? 0 : -1);
}

/*
 * Migrate properties from smb/server SMF service to new network/smb service
 * and delete the smbd and exec (share scripting) properties group from
 * smb/server.
 */
static int
smb_move_props()
{
	smb_scfhandle_t	*hd;
	int		err = 0;
	scf_error_t	scf_err;

	if ((hd = smb_scf_init()) == NULL)
		return (-1);

	if (scf_scope_get_service(hd->scf_scope, SMBD_FMRI_PREFIX,
	    hd->scf_service) != 0) {
		scf_err = scf_error();
		smb_scf_fini(hd);
		if (scf_err == SCF_ERROR_NOT_FOUND)
			return (0);

		syslog(LOG_ERR, "Unable to access the svc:/%s service for"
		    " property migration: %s.", SMBD_FMRI_PREFIX,
		    scf_strerror(scf_err));
		return (-1);
	}

	syslog(LOG_NOTICE, "Attempting to migrate property groups %s & %s in"
	    " svc:/%s to svc:/%s.", SMBD_PG_NAME, SMBD_EXEC_PG_NAME,
	    SMBD_FMRI_PREFIX, SMB_FMRI_PREFIX);

	if (smb_move_pgrp(hd, SMBD_PG_NAME) != 0)
		err = 1;

	if (smb_move_pgrp(hd, SMBD_EXEC_PG_NAME) != 0)
		err = 1;

	smb_scf_fini(hd);

	return (err == 0 ? 0 : -1);
}

/*
 * Allocate and initialize a handle to query SMF properties.
 */
static smb_scfhandle_t *
smb_scf_init(void)
{
	smb_scfhandle_t *hd;
	int		rc;

	if ((hd = calloc(1, sizeof (smb_scfhandle_t))) == NULL)
		return (NULL);

	hd->scf_state = SCH_STATE_INITIALIZING;

	if ((hd->scf_handle = scf_handle_create(SCF_VERSION)) == NULL) {
		free(hd);
		return (NULL);
	}

	if (scf_handle_bind(hd->scf_handle) != SCF_SUCCESS)
		goto scf_init_error;

	if ((hd->scf_scope = scf_scope_create(hd->scf_handle)) == NULL)
		goto scf_init_error;

	rc = scf_handle_get_local_scope(hd->scf_handle, hd->scf_scope);
	if (rc != SCF_SUCCESS)
		goto scf_init_error;

	if ((hd->scf_service = scf_service_create(hd->scf_handle)) == NULL)
		goto scf_init_error;

	if ((hd->scf_pg = scf_pg_create(hd->scf_handle)) == NULL)
		goto scf_init_error;

	hd->scf_state = SCH_STATE_INIT;
	return (hd);

scf_init_error:
	smb_scf_fini(hd);
	return (NULL);
}

/*
 * Destroy and deallocate an SMF handle.
 */
static void
smb_scf_fini(smb_scfhandle_t *hd)
{
	if (hd == NULL)
		return;

	scf_iter_destroy(hd->scf_pg_iter);
	scf_iter_destroy(hd->scf_inst_iter);
	scf_scope_destroy(hd->scf_scope);
	scf_instance_destroy(hd->scf_instance);
	scf_service_destroy(hd->scf_service);
	scf_pg_destroy(hd->scf_pg);

	hd->scf_state = SCH_STATE_UNINIT;

	(void) scf_handle_unbind(hd->scf_handle);
	scf_handle_destroy(hd->scf_handle);
	free(hd);
}

/*
 * The value of any obsolete SMF properties will be copied to the new SMF
 * properties during startup and will then be deleted.
 */
static void
smb_upgrade_props(void)
{
	char nb_domain[NETBIOS_NAME_SZ];
	char ad_domain[MAXHOSTNAMELEN];
	boolean_t signing;
	int64_t lm_level;
	int rc;

	rc = smb_config_getstr(SMB_CI_DOMAIN_NAME, nb_domain, NETBIOS_NAME_SZ);
	if (rc == SMBD_SMF_OK) {
		rc = smb_config_setstr(SMB_CI_DOMAIN_NB, nb_domain);
		if (rc == SMBD_SMF_OK)
			if (smb_prop_del(SMB_CI_DOMAIN_NAME) != 0)
				syslog(LOG_NOTICE, "unable to remove"
				    " smb/%s property",
				    smb_config_getname(SMB_CI_DOMAIN_NAME));
	}

	rc = smb_config_getstr(SMB_CI_DOMAIN_FQDN, ad_domain, MAXHOSTNAMELEN);
	if (rc == SMBD_SMF_OK) {
		rc = smb_config_setstr(SMB_CI_DOMAIN_AD, ad_domain);
		if (rc == SMBD_SMF_OK)
			if (smb_prop_del(SMB_CI_DOMAIN_FQDN) != 0)
				syslog(LOG_NOTICE, "unable to remove"
				    " smb/%s property",
				    smb_config_getname(SMB_CI_DOMAIN_FQDN));
	}

	if (smb_prop_getbool(SMB_CI_SIGNING_ENABLE, &signing) == 0) {
		rc = smb_config_setbool(SMB_CI_SVR_SIGNING_ENABLE, signing);
		if (rc == SMBD_SMF_OK)
			if (smb_prop_del(SMB_CI_SIGNING_ENABLE) != 0)
				syslog(LOG_NOTICE, "unable to remove"
				    " smb/%s property",
				    smb_config_getname(SMB_CI_SIGNING_ENABLE));
	}

	if (smb_prop_getbool(SMB_CI_SIGNING_REQD, &signing) == 0) {
		rc = smb_config_setbool(SMB_CI_SVR_SIGNING_REQD, signing);
		if (rc == SMBD_SMF_OK)
			if (smb_prop_del(SMB_CI_SIGNING_REQD) != 0)
				syslog(LOG_NOTICE, "unable to remove"
				    " smb/%s property",
				    smb_config_getname(SMB_CI_SIGNING_REQD));
	}

	if (smb_config_getnum(SMB_CI_LM_LEVEL, &lm_level) == SMBD_SMF_OK) {
		rc = smb_config_setnum(SMB_CI_SVR_LM_LEVEL, lm_level);
		if (rc == SMBD_SMF_OK)
			if (smb_prop_del(SMB_CI_LM_LEVEL) != 0)
				syslog(LOG_NOTICE, "unable to remove"
				    " smb/%s property",
				    smb_config_getname(SMB_CI_LM_LEVEL));
	}
}

/*
 * Remove config param from SMF.
 */
static int
smb_prop_del(smb_cfg_id_t id)
{
	smb_scfhandle_t *handle;
	int rc;

	if ((handle = smb_smf_scf_init(SMB_FMRI_PREFIX)) == NULL)
		return (-1);

	rc = smb_smf_create_service_pgroup(handle, SMB_PG_NAME);
	if (rc != SMBD_SMF_OK) {
		smb_smf_scf_fini(handle);
		return (-1);
	}

	if ((rc = smb_smf_start_transaction(handle)) != SMBD_SMF_OK) {
		smb_smf_scf_fini(handle);
		return (-1);
	}

	rc = smb_smf_delete_property(handle, smb_config_getname(id));
	if (rc == SMBD_SMF_NOT_FOUND)
		rc = SMBD_SMF_OK;

	(void) smb_smf_end_transaction(handle);

	smb_smf_scf_fini(handle);

	return ((rc == SMBD_SMF_OK) ? 0 : -1);
}

/*
 * Get the value of a boolean config param.
 *
 * Returns 0 on success, otherwise -1.
 */
static int
smb_prop_getbool(smb_cfg_id_t id, boolean_t *bool)
{
	smb_scfhandle_t *handle;
	uint8_t vbool;
	int rc = SMBD_SMF_OK;

	if ((handle = smb_smf_scf_init(SMB_FMRI_PREFIX)) == NULL)
		return (-1);

	rc = smb_smf_create_service_pgroup(handle, SMB_PG_NAME);
	if (rc == SMBD_SMF_OK)
		rc = smb_smf_get_boolean_property(handle,
		    smb_config_getname(id), &vbool);

	smb_smf_scf_fini(handle);

	*bool = (vbool == 1) ? B_TRUE : B_FALSE;

	return ((rc == SMBD_SMF_OK) ? 0 : -1);
}

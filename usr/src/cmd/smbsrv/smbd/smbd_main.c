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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioccom.h>
#include <sys/corectl.h>
#include <sys/tzfile.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <wait.h>
#include <signal.h>
#include <atomic.h>
#include <libscf.h>
#include <limits.h>
#include <priv_utils.h>
#include <door.h>
#include <errno.h>
#include <time.h>
#include <libscf.h>
#include <zone.h>
#include <libgen.h>
#include <pwd.h>
#include <grp.h>

#include <smbsrv/smb_door.h>
#include <smbsrv/smb_ioctl.h>
#include <smbsrv/string.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libsmbns.h>
#include <smbsrv/libntsvcs.h>
#include "smbd.h"

#define	SMBD_ONLINE_WAIT_INTERVAL	5			/* seconds */
#define	SMBD_REFRESH_INTERVAL		10			/* seconds */
#define	SMBD_BUFLEN			512
#define	DRV_DEVICE_PATH			"/devices/pseudo/smbsrv@0:smbsrv"

static int smbd_daemonize_init(void);
static void smbd_daemonize_fini(int, int);
static int smb_init_daemon_priv(int, uid_t, gid_t);

static int smbd_kernel_bind(void);
static void smbd_kernel_unbind(void);
static void smbd_kernel_online(void);
static int smbd_already_running(void);

static int smbd_service_init(void);
static void smbd_service_fini(void);
static void smbd_thread_list(void);
static int smbd_thread_enter(smbd_thread_t *);
static void smbd_auth_upgrade(int);

static int smbd_setup_options(int argc, char *argv[]);
static void smbd_usage(FILE *fp);
static void smbd_sig_handler(int sig);

static int32_t smbd_gmtoff(void);
static void *smbd_time_monitor(void *);
static void *smbd_refresh_monitor(void *);

static int smbd_kernel_start(void);

static pthread_cond_t refresh_cond;
static pthread_mutex_t refresh_mutex;

/*
 * Mutex to ensure that smbd_service_fini() and smbd_service_init()
 * are atomic w.r.t. one another.  Otherwise, if a shutdown begins
 * before initialization is complete, resources can get deallocated
 * while initialization threads are still using them.
 */
static mutex_t smbd_service_mutex;
static cond_t smbd_service_cv;

smbd_t smbd;

/*
 * Use SMF error codes only on return or exit.
 */
int
main(int argc, char *argv[])
{
	struct sigaction	act;
	sigset_t		set;
	uid_t			uid;
	int			pfd = -1;
	uint_t			sigval;
	struct rlimit		rl;
	int			orig_limit;

	smbd.s_pname = basename(argv[0]);
	openlog(smbd.s_pname, LOG_PID | LOG_NOWAIT, LOG_DAEMON);

	if (smbd_setup_options(argc, argv) != 0)
		return (SMF_EXIT_ERR_FATAL);

	if ((uid = getuid()) != smbd.s_uid) {
		smbd_log(LOG_NOTICE, "user %d: %s", uid, strerror(EPERM));
		return (SMF_EXIT_ERR_FATAL);
	}

	if (getzoneid() != GLOBAL_ZONEID) {
		smbd_log(LOG_NOTICE, "non-global zones are not supported");
		return (SMF_EXIT_ERR_FATAL);
	}

	if (is_system_labeled()) {
		smbd_log(LOG_NOTICE, "Trusted Extensions not supported");
		return (SMF_EXIT_ERR_FATAL);
	}

	if (smbd_already_running())
		return (SMF_EXIT_OK);

	/*
	 * Raise the file descriptor limit to accommodate simultaneous user
	 * authentications/file access.
	 */
	if ((getrlimit(RLIMIT_NOFILE, &rl) == 0) &&
	    (rl.rlim_cur < rl.rlim_max)) {
		orig_limit = rl.rlim_cur;
		rl.rlim_cur = rl.rlim_max;
		if (setrlimit(RLIMIT_NOFILE, &rl) != 0)
			smbd_log(LOG_NOTICE,
			    "Cannot change file descriptor limit from %d to %d",
			    orig_limit, rl.rlim_cur);
	}

	(void) sigfillset(&set);
	(void) sigdelset(&set, SIGABRT);

	(void) sigfillset(&act.sa_mask);
	act.sa_handler = smbd_sig_handler;
	act.sa_flags = 0;

	(void) sigaction(SIGABRT, &act, NULL);
	(void) sigaction(SIGTERM, &act, NULL);
	(void) sigaction(SIGHUP, &act, NULL);
	(void) sigaction(SIGINT, &act, NULL);
	(void) sigaction(SIGPIPE, &act, NULL);
	(void) sigaction(SIGUSR1, &act, NULL);

	(void) sigdelset(&set, SIGTERM);
	(void) sigdelset(&set, SIGHUP);
	(void) sigdelset(&set, SIGINT);
	(void) sigdelset(&set, SIGPIPE);
	(void) sigdelset(&set, SIGUSR1);

	if (smbd.s_fg) {
		(void) sigdelset(&set, SIGTSTP);
		(void) sigdelset(&set, SIGTTIN);
		(void) sigdelset(&set, SIGTTOU);

		if (smbd_service_init() != 0) {
			smbd_log(LOG_NOTICE, "service initialization failed");
			exit(SMF_EXIT_ERR_FATAL);
		}
	} else {
		/*
		 * "pfd" is a pipe descriptor -- any fatal errors
		 * during subsequent initialization of the child
		 * process should be written to this pipe and the
		 * parent will report this error as the exit status.
		 */
		pfd = smbd_daemonize_init();

		if (smbd_service_init() != 0) {
			smbd_log(LOG_NOTICE, "daemon initialization failed");
			exit(SMF_EXIT_ERR_FATAL);
		}

		smbd_daemonize_fini(pfd, SMF_EXIT_OK);
	}

	(void) atexit(smb_kmod_stop);

	while (!smbd.s_shutting_down) {
		if (smbd.s_sigval == 0 && smbd.s_refreshes == 0)
			(void) sigsuspend(&set);

		sigval = atomic_swap_uint(&smbd.s_sigval, 0);

		switch (sigval) {
		case 0:
		case SIGPIPE:
		case SIGABRT:
			break;

		case SIGHUP:
			smbd_log(LOG_DEBUG, "refresh requested");
			(void) pthread_cond_signal(&refresh_cond);
			break;

		case SIGUSR1:
			smbd_thread_list();
			smb_log_dumpall();
			break;

		default:
			/*
			 * Typically SIGINT or SIGTERM.
			 */
			smbd.s_shutting_down = B_TRUE;
			break;
		}
	}

	smbd_service_fini();
	closelog();
	return (SMF_EXIT_OK);
}

/*
 * This function will fork off a child process,
 * from which only the child will return.
 *
 * Use SMF error codes only on exit.
 */
static int
smbd_daemonize_init(void)
{
	int status, pfds[2];
	sigset_t set, oset;
	pid_t pid;
	int rc;

	/*
	 * Reset privileges to the minimum set required. We continue
	 * to run as root to create and access files in /var.
	 */
	rc = smb_init_daemon_priv(PU_RESETGROUPS, smbd.s_uid, smbd.s_gid);

	if (rc != 0) {
		smbd_log(LOG_NOTICE, "insufficient privileges");
		exit(SMF_EXIT_ERR_FATAL);
	}

	/*
	 * Block all signals prior to the fork and leave them blocked in the
	 * parent so we don't get in a situation where the parent gets SIGINT
	 * and returns non-zero exit status and the child is actually running.
	 * In the child, restore the signal mask once we've done our setsid().
	 */
	(void) sigfillset(&set);
	(void) sigdelset(&set, SIGABRT);
	(void) sigprocmask(SIG_BLOCK, &set, &oset);

	if (pipe(pfds) == -1) {
		smbd_log(LOG_NOTICE, "unable to create pipe");
		exit(SMF_EXIT_ERR_FATAL);
	}

	closelog();

	if ((pid = fork()) == -1) {
		openlog(smbd.s_pname, LOG_PID | LOG_NOWAIT, LOG_DAEMON);
		smbd_log(LOG_NOTICE, "unable to fork");
		closelog();
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
		(void) close(pfds[1]);

		if (read(pfds[0], &status, sizeof (status)) == sizeof (status))
			_exit(status);

		if (waitpid(pid, &status, 0) == pid && WIFEXITED(status))
			_exit(WEXITSTATUS(status));

		_exit(SMF_EXIT_ERR_FATAL);
	}

	openlog(smbd.s_pname, LOG_PID | LOG_NOWAIT, LOG_DAEMON);
	(void) setsid();
	(void) sigprocmask(SIG_SETMASK, &oset, NULL);
	(void) chdir("/");
	(void) umask(022);
	(void) close(pfds[0]);

	return (pfds[1]);
}

/*
 * This function is based on __init_daemon_priv() and replaces
 * __init_daemon_priv() since we want smbd to have all privileges so that it
 * can execute map/unmap commands with all privileges during share
 * connection/disconnection.  Unused privileges are disabled until command
 * execution.  The permitted and the limit set contains all privileges.  The
 * inheritable set contains no privileges.
 */

static const char root_cp[] = "/core.%f.%t";
static const char daemon_cp[] = "/var/tmp/core.%f.%t";

static int
smb_init_daemon_priv(int flags, uid_t uid, gid_t gid)
{
	priv_set_t *perm = NULL;
	int ret = -1;
	char buf[1024];

	/*
	 * This is not a significant failure: it allows us to start programs
	 * with sufficient privileges and with the proper uid.   We don't
	 * care enough about the extra groups in that case.
	 */
	if (flags & PU_RESETGROUPS)
		(void) setgroups(0, NULL);

	if (gid != (gid_t)-1 && setgid(gid) != 0)
		goto end;

	perm = priv_allocset();
	if (perm == NULL)
		goto end;

	/* E = P */
	(void) getppriv(PRIV_PERMITTED, perm);
	(void) setppriv(PRIV_SET, PRIV_EFFECTIVE, perm);

	/* Now reset suid and euid */
	if (uid != (uid_t)-1 && setreuid(uid, uid) != 0)
		goto end;

	/* I = 0 */
	priv_emptyset(perm);
	ret = setppriv(PRIV_SET, PRIV_INHERITABLE, perm);
end:
	priv_freeset(perm);

	if (core_get_process_path(buf, sizeof (buf), getpid()) == 0 &&
	    strcmp(buf, "core") == 0) {

		if ((uid == (uid_t)-1 ? geteuid() : uid) == 0) {
			(void) core_set_process_path(root_cp, sizeof (root_cp),
			    getpid());
		} else {
			(void) core_set_process_path(daemon_cp,
			    sizeof (daemon_cp), getpid());
		}
	}
	(void) setpflags(__PROC_PROTECT, 0);

	return (ret);
}

/*
 * Most privileges, except the ones that are required for smbd, are turn off
 * in the effective set.  They will be turn on when needed for command
 * execution during share connection/disconnection.
 */
static void
smbd_daemonize_fini(int fd, int exit_status)
{
	priv_set_t *pset;

	/*
	 * Now that we're running, if a pipe fd was specified, write an exit
	 * status to it to indicate that our parent process can safely detach.
	 * Then proceed to loading the remaining non-built-in modules.
	 */
	if (fd >= 0)
		(void) write(fd, &exit_status, sizeof (exit_status));

	(void) close(fd);

	pset = priv_allocset();
	if (pset == NULL)
		return;

	priv_basicset(pset);

	/* list of privileges for smbd */
	(void) priv_addset(pset, PRIV_NET_MAC_AWARE);
	(void) priv_addset(pset, PRIV_NET_PRIVADDR);
	(void) priv_addset(pset, PRIV_PROC_AUDIT);
	(void) priv_addset(pset, PRIV_SYS_DEVICES);
	(void) priv_addset(pset, PRIV_SYS_SMB);
	(void) priv_addset(pset, PRIV_SYS_MOUNT);
	(void) priv_addset(pset, PRIV_SYS_CONFIG);
	(void) priv_addset(pset, PRIV_SYS_SHARE);

	priv_inverse(pset);

	/* turn off unneeded privileges */
	(void) setppriv(PRIV_OFF, PRIV_EFFECTIVE, pset);

	priv_freeset(pset);

	/* reenable core dumps */
	__fini_daemon_priv(NULL);
}

/*
 * smbd_service_init
 */
static int
smbd_service_init(void)
{
	static struct dir {
		char	*name;
		int	perm;
	} dir[] = {
		{ SMB_VARSMB_DIR,	0700 },
		{ SMB_VARRUN_DIR,	0700 },
		{ SMB_CVOL,		0755 },
		{ SMB_SYSROOT,		0755 },
		{ SMB_SYSTEM32,		0755 },
		{ SMB_VSS,		0755 }
	};
	int	i;
	smbd_thread_list_t	*tl = &smbd.s_tlist;

	(void) mutex_lock(&smbd_service_mutex);

	(void) pthread_mutex_init(&tl->tl_mutex, NULL);
	(void) pthread_cond_init(&tl->tl_cv, NULL);
	list_create(&tl->tl_list, sizeof (smbd_thread_t),
	    offsetof(smbd_thread_t, st_lnd));
	tl->tl_count = 0;

	(void) smbd_thread_create("main", NULL, NULL);

	smbd.s_pid = getpid();

	for (i = 0; i < sizeof (dir)/sizeof (dir[0]); ++i) {
		if ((mkdir(dir[i].name, dir[i].perm) < 0) &&
		    (errno != EEXIST)) {
			smbd_log(LOG_NOTICE, "mkdir %s: %s", dir[i].name,
			    strerror(errno));
			(void) mutex_unlock(&smbd_service_mutex);
			return (-1);
		}
	}

	if (smb_ccache_init() != 0) {
		smbd_log(LOG_NOTICE, "ccache initialization failed");
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	smb_codepage_init();
	(void) smb_oemcpg_init();
	smbd_spool_init();

	if (smbd_nicmon_start(SMBD_DEFAULT_INSTANCE_FMRI) != 0)
		smbd_log(LOG_NOTICE, "NIC monitor failed to start");

	smbd_dyndns_start();
	smbd.s_secmode = smb_config_get_secmode();

	/* Initialize all security support providers */
	smbd_ntlm_init();
	smbd_krb5_init();

	ntsvcs_init();

	if (smb_netbios_start() != 0)
		smbd_log(LOG_NOTICE, "NetBIOS services failed to start");
	else
		smbd_log(LOG_DEBUG, "NetBIOS services started");

	if (smbd_dc_monitor_init() != 0) {
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	if (smb_revision_cmp(SMB_SVR_REV_EXTSEC, SMB_CI_SVR_REV) > 0)
		smbd_auth_upgrade(smbd.s_secmode);

	if (smbd_session_avl_init() != 0) {
		smbd_log(LOG_NOTICE, "Unable to create session AVL tree: %s",
		    strerror(errno));
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	smbd.s_door_srv = smbd_door_start();
	smbd.s_door_opipe = smbd_opipe_start();
	if (smbd.s_door_srv < 0 || smbd.s_door_opipe < 0) {
		smbd_log(LOG_NOTICE, "door initialization failed %s",
		    strerror(errno));
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	if (smbd_thread_create("refresh monitor", smbd_refresh_monitor,
	    NULL) != 0) {
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	smbd_dyndns_update();
	(void) smbd_thread_create("time monitor", smbd_time_monitor, NULL);
	(void) smb_lgrp_start();
	smb_pwd_init(B_TRUE);
	smbd_share_start();

	if (smbd_kernel_bind() != 0) {
		(void) mutex_unlock(&smbd_service_mutex);
		return (-1);
	}

	smbd_share_load_execinfo();
	smbd_load_printers();

	smbd.s_initialized = B_TRUE;
	(void) smb_config_setstr(SMB_CI_SVR_REV, SMB_SVR_REV);
	smbd_log(LOG_NOTICE, "service initialized");
	(void) cond_signal(&smbd_service_cv);
	(void) mutex_unlock(&smbd_service_mutex);

	smbd_kernel_online();
	return (0);
}

/*
 * Shutdown smbd and smbsrv kernel services.
 *
 * Shutdown will not begin until initialization has completed.
 * Only one thread is allowed to perform the shutdown.  Other
 * threads will be blocked on fini_in_progress until the process
 * has exited.
 */
static void
smbd_service_fini(void)
{
	(void) mutex_lock(&smbd_service_mutex);

	while (!smbd.s_initialized)
		(void) cond_wait(&smbd_service_cv, &smbd_service_mutex);

	smbd.s_service_fini = B_TRUE;
	smbd_log(LOG_NOTICE, "service shutting down");

	smb_kmod_stop();
	netr_logon_abort();
	smb_lgrp_stop();
	smbd_opipe_stop();
	smbd_door_stop();
	smbd_kernel_unbind();
	smbd_share_stop();
	smbd_dyndns_stop();
	smbd_nicmon_stop();
	smb_ccache_remove();
	smb_pwd_fini();
	smb_domain_fini();
	ntsvcs_fini();
	smb_netbios_stop();
	smbd_spool_fini();

	smbd.s_initialized = B_FALSE;
	smbd_log(LOG_NOTICE, "service terminated");
	(void) mutex_unlock(&smbd_service_mutex);
	exit(SMF_EXIT_OK);
}

/*
 * Launch a transient thread to a perform a single-shot, asynchronous
 * operation and exit.  Transient threads are expected to run to
 * completion rather than sit in a loop.
 */
int
smbd_thread_run(const char *name, smbd_launch_t func, void *arg)
{
	pthread_t		tid;
	pthread_attr_t		attr;
	int			rc;

	bzero(&attr, sizeof (pthread_attr_t));
	rc = pthread_attr_init(&attr);
	if (rc == 0) {
		rc = pthread_attr_setdetachstate(&attr,
		    PTHREAD_CREATE_DETACHED);
		if (rc == 0)
			rc = pthread_create(&tid, &attr, func, arg);
		(void) pthread_attr_destroy(&attr);
	}

	if (rc != 0)
		smbd_log(LOG_ERR, "%s: create failed: %s", name, strerror(rc));
	return (rc);
}

/*
 * Create and register a non-transient thread.  Non-transient or long running
 * threads typically sit in a loop.
 */
int
smbd_thread_create(const char *name, smbd_launch_t func, void *arg)
{
	smbd_thread_t		*tp;
	pthread_attr_t		attr;
	int			rc;

	if ((tp = calloc(1, sizeof (smbd_thread_t))) == NULL) {
		smbd_log(LOG_ERR, "unable to start %s: %s", name,
		    strerror(errno));
		return (errno);
	}

	tp->st_arg = arg;

	if ((tp->st_name = strdup(name)) == NULL) {
		free(tp);
		smbd_log(LOG_ERR, "unable to start %s: %s", name,
		    strerror(errno));
		return (errno);
	}

	if (strcmp(name, "main") == 0) {
		tp->st_tid = pthread_self();
		rc = 0;
	} else {
		bzero(&attr, sizeof (pthread_attr_t));
		rc = pthread_attr_init(&attr);
		if (rc == 0) {
			rc = pthread_attr_setdetachstate(&attr,
			    PTHREAD_CREATE_DETACHED);
			if (rc == 0)
				rc = pthread_create(&tp->st_tid, &attr,
				    func, arg);
			(void) pthread_attr_destroy(&attr);
		}
	}

	if (rc == 0)
		rc = smbd_thread_enter(tp);

	if (rc != 0) {
		smbd_log(LOG_ERR, "unable to start %s: %s", name, strerror(rc));
		free(tp->st_name);
		free(tp);
		return (rc);
	}

	smbd_log(LOG_DEBUG, "%s started (id=%d)", name, tp->st_tid);
	return (0);
}

static int
smbd_thread_enter(smbd_thread_t *new_tp)
{
	smbd_thread_list_t	*tl = &smbd.s_tlist;
	smbd_thread_t		*tp;

	(void) pthread_mutex_lock(&tl->tl_mutex);
	tp = list_head(&tl->tl_list);

	while (tp != NULL) {
		if (strcmp(new_tp->st_name, tp->st_name) == 0) {
			(void) pthread_mutex_unlock(&tl->tl_mutex);
			return (EEXIST);
		}

		tp = list_next(&tl->tl_list, tp);
	}


	list_insert_tail(&tl->tl_list, new_tp);
	++tl->tl_count;

	(void) pthread_mutex_unlock(&tl->tl_mutex);
	return (0);
}

/*
 * Threads may exit when smbd goes offline (s_shutting_down is B_TRUE).
 * If a thread, other than main, has handled a termination signal, we
 * notify the main thread to ensure that it wakes up from sigsuspend.
 */
void
smbd_thread_exit(void)
{
	smbd_thread_list_t	*tl = &smbd.s_tlist;
	smbd_thread_t		*tp;

	(void) pthread_mutex_lock(&tl->tl_mutex);
	tp = list_head(&tl->tl_list);

	while (tp != NULL) {
		if (tp->st_tid == pthread_self()) {
			smbd_log(LOG_DEBUG, "%s exit (id=%d)", tp->st_name,
			    tp->st_tid);
			assert(tl->tl_count);
			--tl->tl_count;
			tp->st_tid = 0;
			list_remove(&tl->tl_list, tp);
			free(tp->st_name);
			free(tp);
			break;
		}

		tp = list_next(&tl->tl_list, tp);
	}

	(void) pthread_mutex_unlock(&tl->tl_mutex);

	if (smbd.s_shutting_down && !smbd.s_service_fini)
		smbd_thread_kill("main", SIGTERM);
}

void
smbd_thread_kill(const char *name, int sig)
{
	smbd_thread_list_t	*tl = &smbd.s_tlist;
	smbd_thread_t		*tp;

	(void) pthread_mutex_lock(&tl->tl_mutex);
	tp = list_head(&tl->tl_list);

	while (tp != NULL) {
		if ((strcmp(name, tp->st_name) == 0) &&
		    (tp->st_tid != pthread_self()) &&
		    (tp->st_tid != 0)) {
			(void) pthread_kill(tp->st_tid, sig);
			break;
		}

		tp = list_next(&tl->tl_list, tp);
	}

	(void) pthread_mutex_unlock(&tl->tl_mutex);
}

static void
smbd_thread_list(void)
{
	smbd_thread_list_t	*tl = &smbd.s_tlist;
	smbd_thread_t		*tp;

	(void) pthread_mutex_lock(&tl->tl_mutex);
	tp = list_head(&tl->tl_list);

	while (tp != NULL) {
		smbd_log(LOG_DEBUG, "%s running (id=%d)", tp->st_name,
		    tp->st_tid);
		tp = list_next(&tl->tl_list, tp);
	}

	(void) pthread_mutex_unlock(&tl->tl_mutex);
}

/*
 * Wait for refresh events.  When woken up, update the smbd configuration
 * from SMF and check for changes that require service reconfiguration.
 * Throttling is applied to coallesce multiple refresh events when the
 * service is being refreshed repeatedly.
 */
/*ARGSUSED*/
static void *
smbd_refresh_monitor(void *arg)
{
	smbd_online_wait("smbd_refresh_monitor");

	while (smbd_online()) {
		(void) sleep(SMBD_REFRESH_INTERVAL);

		(void) pthread_mutex_lock(&refresh_mutex);
		while ((atomic_swap_uint(&smbd.s_refreshes, 0) == 0) &&
		    (!smbd.s_shutting_down))
			(void) pthread_cond_wait(&refresh_cond, &refresh_mutex);
		(void) pthread_mutex_unlock(&refresh_mutex);

		if (smbd.s_shutting_down)
			break;

		(void) mutex_lock(&smbd_service_mutex);

		smbd_dc_monitor_refresh();
		smb_ccache_remove();

		/*
		 * Clear the DNS zones for the existing interfaces
		 * before updating the NIC interface list.
		 */
		smbd_dyndns_clear();

		if (smbd_nicmon_refresh() != 0)
			smbd_log(LOG_NOTICE, "NIC monitor refresh failed");

		smb_netbios_name_reconfig();
		smb_browser_reconfig();
		smbd_dyndns_update();
		if (smbd_kernel_bind() == 0) {
			smbd_kernel_online();
			smbd_share_load_execinfo();
			smbd_load_printers();
		}

		(void) mutex_unlock(&smbd_service_mutex);
	}

	smbd_thread_exit();
	return (NULL);
}

void
smbd_set_secmode(int secmode)
{
	switch (secmode) {
	case SMB_SECMODE_WORKGRP:
	case SMB_SECMODE_DOMAIN:
		(void) smb_config_set_secmode(secmode);
		smbd.s_secmode = secmode;
		break;

	default:
		smbd_log(LOG_ERR, "invalid security mode: %d", secmode);
		smbd_log(LOG_ERR, "entering maintenance mode");
		(void) smb_smf_maintenance_mode();
	}
}

/*
 * The service is online if initialization is complete and shutdown
 * has not begun.
 */
boolean_t
smbd_online(void)
{
	return (smbd.s_initialized && !smbd.s_shutting_down);
}

/*
 * Wait until the service is online.  Provided for threads that
 * should wait until the service has been fully initialized before
 * they start performing operations.
 */
void
smbd_online_wait(const char *text)
{
	while (!smbd_online())
		(void) sleep(SMBD_ONLINE_WAIT_INTERVAL);

	if (text != NULL) {
		smbd_log(LOG_DEBUG, "%s: online", text);
		(void) fprintf(stderr, "smbd: %s online\n", text);
	}
}

/*
 * If the door has already been opened by another process (non-zero pid
 * in target), we assume that another smbd is already running.  If there
 * is a race here, it will be caught later when smbsrv is opened because
 * only one process is allowed to open the device at a time.
 */
static int
smbd_already_running(void)
{
	door_info_t info;
	int door;

	if ((door = open(SMBD_DOOR_NAME, O_RDONLY)) < 0)
		return (0);

	if (door_info(door, &info) < 0)
		return (0);

	if (info.di_target > 0) {
		smbd_log(LOG_NOTICE, "already running: pid %ld\n",
		    info.di_target);
		(void) close(door);
		return (1);
	}

	(void) close(door);
	return (0);
}

/*
 * smbd_kernel_bind
 *
 * If smbsrv is already bound, reload the configuration and update smbsrv.
 * Otherwise, open the smbsrv device and start the kernel service.
 */
static int
smbd_kernel_bind(void)
{
	smb_kmod_cfg_t	cfg;
	int		rc;

	if (smbd.s_kbound) {
		smb_load_kconfig(&cfg);
		rc = smb_kmod_setcfg(&cfg);
		if (rc < 0)
			smbd_log(LOG_NOTICE,
			    "kernel configuration update failed: %s",
			    strerror(errno));
		return (rc);
	}

	if (smb_kmod_isbound())
		smbd_kernel_unbind();

	if ((rc = smb_kmod_bind()) == 0) {
		rc = smbd_kernel_start();
		if (rc != 0)
			smb_kmod_unbind();
		else
			smbd.s_kbound = B_TRUE;
	}

	if (rc != 0)
		smbd_log(LOG_NOTICE, "kernel bind error: %s", strerror(errno));
	return (rc);
}

static int
smbd_kernel_start(void)
{
	smb_kmod_cfg_t	cfg;
	int		rc;

	smb_load_kconfig(&cfg);
	rc = smb_kmod_setcfg(&cfg);
	if (rc != 0)
		return (rc);

	rc = smb_kmod_setgmtoff(smbd_gmtoff());
	if (rc != 0)
		return (rc);

	rc = smb_kmod_start(smbd.s_door_opipe, smbd.s_door_srv);
	return (rc);
}

/*
 * smbd_kernel_unbind
 */
static void
smbd_kernel_unbind(void)
{
	smb_kmod_unbind();
	smbd.s_kbound = B_FALSE;
}

/*
 * Notifies kernel that smbd initialization is done
 * which means door servers are ready to take calls.
 */
static void
smbd_kernel_online(void)
{
	smb_kmod_online();
}

/*
 * Send local gmtoff to the kernel module one time at startup and each
 * time it changes (up to twice a year).  Note that some timezones are
 * aligned on half and quarter hour boundaries.
 *
 * Check for clock skew against the domain controller in domain mode
 * every 10 minutes, which also stops the DC dropping the connection
 * due to inactivity.
 */
/*ARGSUSED*/
static void *
smbd_time_monitor(void *arg)
{
	smb_domainex_t	di;
	struct tm	local_tm;
	time_t		secs;
	int32_t		gmtoff, last_gmtoff = -1;
	int		timeout;
	int		error;

	smbd_online_wait("time monitor");

	while (smbd_online()) {
		gmtoff = smbd_gmtoff();

		if ((last_gmtoff != gmtoff) && smbd.s_kbound) {
			error = smb_kmod_setgmtoff(gmtoff);
			if (error != 0)
				smbd_log(LOG_NOTICE,
				    "localtime update failed: %s",
				    strerror(error));
		}

		/*
		 * Align the next iteration on a 10 minute boundary.
		 */
		secs = time(0);
		(void) localtime_r(&secs, &local_tm);
		timeout = ((10 - (local_tm.tm_min % 10)) * SECSPERMIN);
		(void) sleep(timeout);

		last_gmtoff = gmtoff;

		if (smbd.s_secmode == SMB_SECMODE_DOMAIN) {
			if (!smb_domain_getinfo(&di))
				continue;

			srvsvc_timecheck(di.d_dc, di.d_primary.di_nbname);
		}
	}

	smbd_thread_exit();
	return (NULL);
}

/*
 * smbd_gmtoff
 *
 * Determine offset from GMT. If daylight saving time use altzone,
 * otherwise use timezone.
 */
static int32_t
smbd_gmtoff(void)
{
	time_t clock_val;
	struct tm *atm;
	int32_t gmtoff;

	(void) time(&clock_val);
	atm = localtime(&clock_val);

	gmtoff = (atm->tm_isdst) ? altzone : timezone;

	return (gmtoff);
}

static void
smbd_sig_handler(int sigval)
{
	if (smbd.s_sigval == 0)
		(void) atomic_swap_uint(&smbd.s_sigval, sigval);

	if (sigval == SIGHUP) {
		atomic_inc_uint(&smbd.s_refreshes);
		(void) pthread_cond_signal(&refresh_cond);
	}

	if (sigval == SIGINT || sigval == SIGTERM) {
		smbd.s_shutting_down = B_TRUE;
		(void) pthread_cond_signal(&refresh_cond);
	}
}

/*
 * Set up configuration options and parse the command line.
 * This function will determine if we will run as a daemon
 * or in the foreground.
 *
 * Failure to find a uid or gid results in using the default (0).
 */
static int
smbd_setup_options(int argc, char *argv[])
{
	struct passwd *pwd;
	struct group *grp;
	int c;

	if ((pwd = getpwnam("root")) != NULL)
		smbd.s_uid = pwd->pw_uid;

	if ((grp = getgrnam("sys")) != NULL)
		smbd.s_gid = grp->gr_gid;

	while ((c = getopt(argc, argv, ":f")) != -1) {
		switch (c) {
		case 'f':
			smbd.s_fg = 1;
			break;

		case ':':
		case '?':
		default:
			smbd_usage(stderr);
			return (-1);
		}
	}

	return (0);
}

static void
smbd_usage(FILE *fp)
{
	static char *help[] = {
		"-f  run program in foreground"
	};

	int i;

	(void) fprintf(fp, "Usage: %s [-f]\n", smbd.s_pname);

	for (i = 0; i < sizeof (help)/sizeof (help[0]); ++i)
		(void) fprintf(fp, "    %s\n", help[i]);
}

void
smbd_log(int priority, const char *fmt, ...)
{
	char buf[SMBD_BUFLEN];
	va_list ap;

	if (fmt == NULL)
		return;

	va_start(ap, fmt);
	(void) vsnprintf(buf, SMBD_BUFLEN, fmt, ap);
	va_end(ap);

	if (priority != LOG_DEBUG)
		(void) fprintf(stderr, "smbd: %s\n", buf);

	if (smbd.s_loghd == 0)
		smbd.s_loghd = smb_log_create(SMBD_LOGSIZE, SMBD_LOGNAME);

	smb_log(smbd.s_loghd, priority, "%s", buf);
}

/*
 * Authentication upgrade
 *
 * If the system is configured to be a domain member prior to upgrading
 * to a release that supports Kerberos user authentication, launch a transient
 * thread to handle the upgrade.
 */
static void
smbd_auth_upgrade(int secmode)
{
	if ((!smb_config_getbool(SMB_CI_SVR_EXTSEC)) ||
	    (secmode != SMB_SECMODE_DOMAIN))
		return;

	(void) smbd_thread_run("authentication upgrade", smb_ads_upgrade, NULL);
}

/*
 * Enable libumem debugging by default on DEBUG builds.
 */
#ifdef DEBUG
const char *
_umem_debug_init(void)
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}
#endif

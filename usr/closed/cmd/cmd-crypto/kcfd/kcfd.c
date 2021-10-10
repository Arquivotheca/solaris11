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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * The global zone kcfd daemon helps in managing the CPU usage by
 * cryptographic operations done in software. The system utilization
 * associated with these operations is charged to the kcfd process.
 *
 * kcfd also provides the ELF signature verification service for a zone.
 * Each zone has its own cryptographic signature verification process.
 *
 * Some of the code for thread creation is similar to the nfsd
 * thread creation library code.
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <synch.h>
#include <thread.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/priocntl.h>
#include <sys/fxpriocntl.h>
#include <sys/wait.h>
#include <priv_utils.h>
#include <zone.h>

#include <cryptoutil.h>
#include <sys/crypto/ioctladmin.h>

#include "kcfd.h"

static int got_signal = 0;
static int cafd = -1;
static boolean_t ksvc_active = B_FALSE;
static mutex_t ksvc_active_lock = DEFAULTMUTEX;
static boolean_t in_global_zone;

static void sig_cleanup();
static int daemonize_start();
static void daemonize_ready();

/*ARGSUSED*/
int
main(int argc, char **argv)
{
	struct sigaction action;
	int	ret = KCFD_EXIT_OKAY;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if (argc > 1) {
		(void) fprintf(stderr, "usage: kcfd\n");
		return (KCFD_EXIT_INVALID_ARG);
	}

	(void) sigemptyset(&action.sa_mask);
	action.sa_handler = sig_cleanup;
	action.sa_flags = 0;

	if (sigaction(SIGHUP, &action, NULL) < 0) {
		(void) fprintf(stderr,
		    "kcfd: sigaction SIGHUP failed: %s, Exiting",
		    strerror(errno));
		return (KCFD_EXIT_SYS_ERR);
	}

	if (sigaction(SIGTERM, &action, NULL) < 0) {
		(void) fprintf(stderr,
		    "kcfd: sigaction SIGTERM failed: %s, Exiting",
		    strerror(errno));
		return (KCFD_EXIT_SYS_ERR);
	}

	if (sigaction(SIGINT, &action, NULL) < 0) {
		(void) fprintf(stderr,
		    "kcfd: sigaction SIGINT failed: %s, Exiting",
		    strerror(errno));
		return (KCFD_EXIT_SYS_ERR);
	}


	if ((ret = daemonize_start()) < 0)
		return (ret);

	in_global_zone = (getzoneid() == GLOBAL_ZONEID);
	/*
	 * The cryptodebug call must be in the child because it has
	 * global state in libcryptoutil
	 */
	cryptodebug_init("kcfd");

	if ((cafd = open("/dev/cryptoadm", O_RDWR)) == -1) {
		cryptoerror(LOG_ERR,
		    "kcfd: /dev/cryptoadm open failed: %s, Exiting",
		    strerror(errno));
		return (KCFD_EXIT_SYS_ERR);
	}

	ret = kcfd_modverify(cafd, in_global_zone);
	if (ret != KCFD_EXIT_OKAY)
		return (ret);

	/*
	 * In the global zone we pass the door to the kernel, and
	 * retain proc_priocntl, sys_devices privileges for the
	 * thread pool managment and file_owner to fdetach the door.
	 *
	 * In a non global zone we just retain file_owner to fdetach
	 * the door on exit.
	 */
	if (in_global_zone) {
		if (__init_daemon_priv(PU_RESETGROUPS|PU_CLEARLIMITSET,
		    DAEMON_UID, DAEMON_GID,
		    PRIV_FILE_OWNER, PRIV_PROC_PRIOCNTL, PRIV_SYS_DEVICES,
		    (char *)NULL) == -1) {
			cryptoerror(LOG_ERR, "failed to drop privileges");
			return (KCFD_EXIT_SYS_ERR);
		}
	} else {
		if (__init_daemon_priv(PU_RESETGROUPS|PU_CLEARLIMITSET,
		    DAEMON_UID, DAEMON_GID,
		    PRIV_FILE_OWNER, (char *)NULL) == -1) {
			cryptoerror(LOG_ERR, "failed to drop privileges");
			return (KCFD_EXIT_SYS_ERR);
		}
	}

	__fini_daemon_priv(PRIV_PROC_FORK, PRIV_PROC_EXEC, PRIV_FILE_LINK_ANY,
	    PRIV_PROC_SESSION, PRIV_PROC_INFO, (char *)NULL);

	daemonize_ready();

	/*
	 * Forget about any signals we got up until now but anything else
	 * causes us to cleanup and exit.
	 */
	got_signal = 0;
	while (!got_signal)
		(void) pause();

	/*
	 * Shutdown the Cryptoservices:
	 * 1. Close out our connection to /dev/cryptoadm
	 * 2. Shutdown the signature validation
	 */
	ret = KCFD_EXIT_OKAY;
	if (cafd != -1)
		(void) close(cafd);
	ret = kcfd_modverify_exit();

	return (ret);
}


/*
 * Thread to call into the kernel and do work on behalf of
 * kernel cryptographic framework (KCF).
 */
static void *
kcf_svcstart(void *arg)
{
	int fd = (int)arg;

	while (ioctl(fd, CRYPTO_POOL_RUN) == -1) {
		/*
		 * Interrupted by a signal while in the kernel.
		 * this process is still alive, try again.
		 */
		if (errno == EINTR)
			continue;
		else
			break;
	}

	/*
	 * If we weren't interrupted by a signal, but did
	 * return from the kernel, this thread's work is done,
	 * and it should exit.
	 */
	thr_exit(NULL);
	return (NULL);
}

/*
 * User-space "creator" thread. This thread blocks in the kernel
 * until new worker threads need to be created for the service
 * pool. On return to userspace, if there is no error, create a
 * new thread for the service pool.
 */
static void *
kcf_svcblock(void *arg)
{
	int fd = (int)arg;
	thread_t tid;

	/*
	 * Have a worker thread always start to make sure we don't
	 * use the failover thread once kcfd comes up. We ignore
	 * any error here as the thread creation might succeed later.
	 */
	(void) thr_create(NULL, THR_MIN_STACK, kcf_svcstart, (void *)fd,
	    THR_BOUND | THR_DETACHED, &tid);

	/* CONSTCOND */
	while (1) {
		int nthrs = 0;

		/*
		 * Call into the kernel, and hang out there
		 * until a thread needs to be created.
		 */
		if (ioctl(fd, CRYPTO_POOL_WAIT, &nthrs) == -1) {
			/*
			 * Interrupted by a signal while in the kernel.
			 * this process is still alive, try again.
			 */
			if (errno == EINTR)
				continue;
			else
				break;
		}

		/*
		 * nthrs is the number of threads that need to be created.
		 * We avoid the overhead of creating them one by one, by
		 * having KCF set this value on return from the ioctl.
		 *
		 * User portion of the thread does no real work since
		 * the svcpool threads actually spend their entire
		 * lives in the kernel. So, user portion of the thread
		 * should have the smallest stack possible.
		 */
		while (nthrs-- > 0) {
			/*
			 * We ignore any error here as thread creation
			 * might still succeed later.
			 */
			(void) thr_create(NULL, THR_MIN_STACK, kcf_svcstart,
			    (void *)fd, THR_BOUND | THR_DETACHED, &tid);
		}
	}

	thr_exit(NULL);
	return (NULL);
}

void
kcf_svcinit()
{
	thread_t tid;
	char	pc_clname[PC_CLNMSZ];
	pcinfo_t pcinfo;
	pri_t maxupri;

	(void) mutex_lock(&ksvc_active_lock);
	if (!in_global_zone || ksvc_active) {
		(void) mutex_unlock(&ksvc_active_lock);
		return;
	}

	cryptodebug("Creating kcf thread pool service");
	/*
	 * Userland threads can't be in SYS, but they can be given a
	 * higher priority by default.
	 *
	 * By default, all kcfd threads should be part of the FX scheduler.
	 * This is done as we want to closely approximate the SYS class
	 * behavior to assure good KCF performance even under heavy load.
	 * This priocntl(...,PC_SETXPARMS,...) operation
	 * still renders kcfd managable by an admin by utilizing commands to
	 * change scheduling manually, or by using resource management tools
	 * such as pools to associate them with a different scheduling class
	 * and segregate the workload.
	 *
	 * However if the admin has used priocntl(1) or dispadm(1m) to change
	 * the default scheduling class we need to respect that.
	 * As such we only force FX if we started up in the TS or IA classes.
	 *
	 * We set the threads' priority to the upper bound for priorities
	 * in FX. This should be 60, but since the desired action is to
	 * make kcfd more important than TS threads, we bow to the
	 * system's knowledge rather than setting it manually. Since we want
	 * to approximate the SYS class, we use an "infinite" quantum (SYS class
	 * doesn't timeslice). If anything fails, just log the failure and let
	 * the daemon default to the system default.
	 *
	 * The change of scheduling class is expected to fail in a non-global
	 * zone, so we avoid worrying the zone administrator unnecessarily.
	 */
	if (priocntl(P_PID, P_MYID, PC_GETXPARMS, NULL,
	    PC_KY_CLNAME, pc_clname, 0) == -1) {
		cryptoerror(LOG_ERR,
		    "Unable to determine scheduling class using default");
	}
	if ((strcmp("TS", pc_clname) == 0) ||
	    (strcmp("IA", pc_clname) == 0)) {
		cryptodebug("current sched class is TS/IA");
		(void) strcpy(pcinfo.pc_clname, "FX");
		if (priocntl(0, 0, PC_GETCID, (caddr_t)&pcinfo) != -1) {
			maxupri = ((fxinfo_t *)pcinfo.pc_clinfo)->fx_maxupri;
			if (priocntl(P_LWPID, P_MYID, PC_SETXPARMS, "FX",
			    FX_KY_UPRILIM, maxupri, FX_KY_UPRI, maxupri,
			    FX_KY_TQNSECS, FX_TQINF, NULL) != 0) {
				cryptoerror(LOG_ERR,
				    "Unable to use FX scheduler: "
				    "%s. Using system default scheduler.",
				    strerror(errno));
			}
		} else {
			cryptoerror(LOG_ERR,
			    "Unable to determine parameters for FX scheduler. "
			    "Using system default scheduler.");
		}
	}

	/*
	 * Set up blocked thread to do LWP creation on behalf of the kernel.
	 */
	if (ioctl(cafd, CRYPTO_POOL_CREATE) == -1) {
		cryptoerror(LOG_ERR, "Can't set up kernel crypto service: %s.",
		    strerror(errno));
		(void) mutex_unlock(&ksvc_active_lock);
		return;
	}

	/*
	 * Create a bound thread to wait for kernel LWPs that
	 * need to be created. This thread also has little need
	 * of stackspace, so should be created with that in mind.
	 */
	if (thr_create(NULL, THR_MIN_STACK * 2, kcf_svcblock, (void *)cafd,
	    THR_BOUND | THR_DETACHED, &tid)) {
		cryptoerror(LOG_ERR, "Can't set up crypto pool creator: %s.",
		    strerror(errno));
		(void) mutex_unlock(&ksvc_active_lock);
		return;
	}

	ksvc_active = B_TRUE;
	(void) mutex_unlock(&ksvc_active_lock);
}

/*ARGSUSED*/
static void
sig_cleanup(int sig)
{
	got_signal = 1;
}


static int pipe_fd = -1;

static int
daemonize_start(void)
{
	struct sigaction action;
	pid_t	pid;
	int status;
	int filedes[2];

	closelog();
	closefrom(0);
	if (open("/dev/null", O_RDONLY) == -1)
		return (KCFD_EXIT_SYS_ERR);
	if (open("/dev/null", O_WRONLY) == -1)
		return (KCFD_EXIT_SYS_ERR);
	if (dup(1) == -1)
		return (KCFD_EXIT_SYS_ERR);

	if (pipe(filedes) < 0)
		return (-1);

	pid = fork1();
	if (pid < 0) {
		return (KCFD_EXIT_SYS_ERR);
	}
	if (pid != 0) {
		char data;

		/*
		 * parent waits until the child tells us the service
		 * is actually ready
		 */
		action.sa_sigaction = SIG_DFL;
		(void) sigemptyset(&action.sa_mask);
		action.sa_flags = 0;
		(void) sigaction(SIGPIPE, &action, NULL); /* ignore SIGPIPE */

		(void) close(filedes[1]);
		if (read(filedes[0], &data, 1) == 1) {
			_exit(KCFD_EXIT_OKAY);
		}

		status = -1;
		(void) wait4(pid, &status, 0, NULL);
		if (WIFEXITED(status))
			_exit(WEXITSTATUS(status));
		else
			_exit(KCFD_EXIT_SYS_ERR);
	}

	pipe_fd = filedes[1];
	(void) close(filedes[0]);

	(void) chdir("/");
	(void) umask(0);

	if (setsid() == -1) {
		return (KCFD_EXIT_SYS_ERR);
	}

	return (0);

}

static void
daemonize_ready()
{
	char data = '\0';

	/* wake the parent */
	(void) write(pipe_fd, &data, 1);
	(void) close(pipe_fd);
}

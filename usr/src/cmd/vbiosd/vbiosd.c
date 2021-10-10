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
 * vbiosd - Emulate Real Mode BIOS Calls.
 *
 * This daemon is currently used to execute VESA and VGA BIOS calls on behalf
 * of the kernel graphic/coherent console sub-system.
 * The daemon implements a door server that waits for kernel calls. Each call
 * specifies a command to execute. At the current state, the only command
 * requested by the kernel is SETMODE, which sets a specific VESA (or VGA)
 * mode. This is necessary for the proper working of VESA consoles (hi-res
 * bit-mapped consoles) because on some cards (ex. some ATI cards) X.org fails,
 * on exit or console switch, to properly restore their state correctly.
 *
 * The execution of a BIOS command is performed by emulating the VBIOS code
 * of the video card. Emulation is performed by Scitech's x86emu library.
 * The kernel communicates with this deamon through a door interface. The
 * kernel is meant to be the *only* consumer of the provided interfaces.
 * Moreover, kernel code should always use the interfaces exposed by
 * usr/src/uts/i86pc/os/vbios.c, which is responsbile of the serializing of
 * VBIOS (INT 10h) requests.
 * This design should make easier the transition (if ever performed) to an
 * in-kernel BIOS emulator.
 *
 * This code uses the  SchiTech's emulation library (whose license is contained
 * in the LICENSE.x86emu file) x86emu, contained in the x86emu prefixed
 * files and in the x86emu directory.
 *
 * The number of supported commands has been volountarly kept small and will
 * expand if and when new functionalities will be needed by the kernel (f.e.
 * to provide runtime console mode switching). If other subsystems will need
 * to execute BIOS interrupts, this code can be leveraged to fulfill their
 * needs, too (the general interfaces should be easy enough to extend to
 * accomodate the changes and the kernel entry point, vbios.c, already offers
 * a queueing + discard-on-timeout mechanism).
 *
 * Copyright notes:
 * - for x86emu (c) SciTech Software, Inc (see LICENSE.x86emu)
 */

#include <sys/flock.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/zone.h>
#include <sys/fbio.h>
#include <sys/wait.h>
#include <sys/kd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ucred.h>
#include <fcntl.h>
#include <synch.h>
#include <unistd.h>
#include <zone.h>
#include <strings.h>
#include <libscf.h>
#include <pthread.h>
#include <atomic.h>

#include "vbiosd.h"

boolean_t		vbiosd_is_daemon = B_FALSE;
boolean_t		vbiosd_debug_report = B_FALSE;
boolean_t		vbiosd_accept_uland = B_FALSE;
int			vbiosd_console_fd = -1;
static int		vbiosd_lock_fd = -1;
static boolean_t	vbiosd_do_gdmcheck = B_FALSE;
static boolean_t	vbiosd_force_reset = B_FALSE;
static boolean_t	vbiosd_daemonize = B_TRUE;

static void vbiosd_exit(int);
extern void vbiosd_x86emu_cleanup();

#define	FB_DEVICE	"/dev/fb"
#define	FMRI_GDM 	"svc:/application/graphical-login/gdm:default"
#define	FMRI_VBIOSD	"svc:/system/vbiosd:default"

/*
 * Acquire a lock on 'lockfile'. While the door mechanism would give a way
 * by itself to check exclusive running of our daemon, we do it explicitly here.
 */
static int
vbiosd_acquire_flock(const char *lockfile)
{
	int		ret;
	char		pidbuf[8];
	struct flock	lock;

	vbiosd_lock_fd = open(lockfile, O_WRONLY|O_CREAT, 0600);
	if (vbiosd_lock_fd < 0) {
		vbiosd_debug(LOG_ERRNO, "unable to open %s", lockfile);
		return (VBIOSD_ERROR);
	}

	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_end = 0;

	/*
	 * Try to acquire the lock and check if another instance is already
	 * running.
	 */
	ret = fcntl(vbiosd_lock_fd, F_SETLK, &lock);
	if (ret == -1) {
		if (errno == EACCES || errno == EAGAIN) {
			vbiosd_abort(NO_ERRNO, "Another instance of %s is"
			    " already running", MYNAME);
		} else {
			vbiosd_debug(LOG_ERRNO, "unable to acquire lock");
			return (VBIOSD_ERROR);
		}
	}

	/*
	 * Lock acquired. Log the deamon PID in the file. Only log debug
	 * messages if we fail here.
	 */
	(void) ftruncate(vbiosd_lock_fd, 0);
	ret = snprintf(pidbuf, sizeof (pidbuf), "%d", getpid());
	if (write(vbiosd_lock_fd, pidbuf, ret) == -1)
		vbiosd_debug(LOG_ERRNO, "write to %s failed", lockfile);

	/* Set close-on-exec on the lock filedescriptor. */
	ret = fcntl(vbiosd_lock_fd, F_GETFD, 0);
	if (ret == -1)
		vbiosd_debug(LOG_ERRNO, "fcntl failed");
	ret |= FD_CLOEXEC;
	ret = fcntl(vbiosd_lock_fd, F_SETFD, ret);
	if (ret == -1)
		vbiosd_debug(LOG_ERRNO, "fcntl failed");

	return (VBIOSD_SUCCESS);
}

/* Release a previously acquired lock. */
static void
vbiosd_release_flock()
{
	struct flock 	lock;

	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;

	(void) fcntl(vbiosd_lock_fd, F_SETLK, &lock);
	(void) close(vbiosd_lock_fd);
	vbiosd_lock_fd = -1;
}

static void
vbiosd_exit(int error_code)
{
	if (vbiosd_lock_fd < 0)
		vbiosd_release_flock();

	vbiosd_release_door();
	vbiosd_x86emu_cleanup();
	closelog();
	exit(error_code);
}

static int
is_gdm_running()
{
	char	*state;

	if ((state = smf_get_state(FMRI_GDM)) == NULL)
		return (0);

	if (strcmp(state, SCF_STATE_STRING_ONLINE) == 0) {
		free(state);
		return (1);
	}

	free(state);
	return (0);
}

static int
do_gdm_check()
{
	int	ret = 0;

	if (vbiosd_console_fd == -1) {
		return (-1);
	}

	if (is_gdm_running() == 0 || vbiosd_force_reset == B_TRUE) {
		if (ioctl(vbiosd_console_fd, KDSETMODE, KD_RESETTEXT) == -1) {
			vbiosd_error(LOG_ERRNO, "unable to send RESETTEXT "
			    "command");
			ret = -1;
		}
	}

	(void) close(vbiosd_console_fd);
	return (ret);
}

int
vbiosd_blob_res_alloc(vbiosd_res_blob_t *blob, uint32_t	size)
{
	vbios_call_results_t	*res;

	res = malloc(size);
	if (res == NULL)
		/* Allocation failed, bail out */
		return (VBIOSD_ERROR);

	bzero(res, size);
	blob->res = res;
	blob->to_free = B_TRUE;
	blob->res_size = size;
	return (VBIOSD_SUCCESS);
}

void
vbiosd_blob_res_free(vbiosd_res_blob_t *blob)
{
	if (blob->to_free == B_TRUE)
		free(blob->res);
}

/*
 * Daemon signal handlers.
 * Catch SIGTERM for a clean exit.
 */
void
vbiosd_term_hndlr()
{
	vbiosd_debug(NO_ERRNO, "termination signal received, shutting down");
	vbiosd_exit(EXIT_SUCCESS);
}

/*
 * Synchronization signal between the child and the parent to indicate a
 * successful startup of the vbiosd daemon.
 */
void
vbiosd_sigusr_hndlr()
{
	exit(EXIT_SUCCESS);
}

static void
vbiosd_usage(char *progname)
{
	(void) fprintf(stderr, "Usage: %s [-v|d|h]\n", progname);
	(void) fprintf(stderr, "\t -v -- verbose mode [extended debug"
	    " information]\n");
	(void) fprintf(stderr, "\t -d -- do not daemonize [-d does not "
	    "imply -v]\n");
	(void) fprintf(stderr, "\t -g -- test for gdm and restore the console "
	    "if necessary\n");
	(void) fprintf(stderr, "\t -F -- (to be used with -g) skip the gdm "
	    "check\n");
	(void) fprintf(stderr, "\t -h -- prints this information and exits\n");
}

/*
 * Pars command parameter.
 * -v : verbose output.
 * -d : run without daemonize in the background.
 * -g : check if gdm is running and restore the console if not
 */
static void
vbiosd_parse_args(int argc, char **argv)
{
	int		param;
	extern int	opterr;
	char		*name = argv[0];

	/* Block getopt error reporting. */
	opterr = 0;

	while ((param = getopt(argc, argv, "gdvuFh")) != -1) {
		switch (param) {
		case 'd':
			vbiosd_daemonize = B_FALSE;
			break;
		case 'v':
			vbiosd_debug_report = B_TRUE;
			break;
		case 'g':
			/* GDM check. */
			vbiosd_do_gdmcheck = B_TRUE;
			vbiosd_daemonize = B_FALSE;
			break;
		case 'F':
			/* Force KD_RESETTEXT regardless of GDM state. */
			vbiosd_force_reset = B_TRUE;
			break;
		case 'u':
			/*
			 * Hidden debugging option. Makes vbiosd accept
			 * commands also from the userland.
			 */
			vbiosd_accept_uland = B_TRUE;
			break;
		case 'h':
			vbiosd_usage(name);
			vbiosd_exit(EXIT_SUCCESS);
		default:
			(void) fprintf(stderr, "unknown parameter passed.."
			    " abort\n");
			vbiosd_usage(name);
			vbiosd_exit(EXIT_FAILURE);
		}
	}

	if (vbiosd_force_reset == B_TRUE && vbiosd_do_gdmcheck == B_FALSE) {
		(void) fprintf(stderr, "-F and -g need to be used together\n");
		vbiosd_usage(name);
		vbiosd_exit(EXIT_FAILURE);
	}
}

static int
vbiosd_daemonize_me()
{
	pid_t		pid;
	int		fd;
	char		*dbg_msg;

	pid = fork();
	if (pid == -1) {
		dbg_msg = "unable to fork";
		goto out;
	} else if (pid > 0) {
		int	status;
		/* Hang around until the child either succeeds or fails */
		vbiosd_setup_sighandler(SIGUSR1, 0, vbiosd_sigusr_hndlr);
		vbiosd_setup_sighandler(SIGCHLD, 0, SIG_DFL);

		if (waitpid(pid, &status, 0) != pid)
			exit(EXIT_SUCCESS);

		(void) fprintf(stderr, "%s: start-up process failed\n", MYNAME);
		exit(EXIT_FAILURE);
	}

	/* Create a new session. */
	(void) setsid();
	/* Sanitize 0,1 and 2 and close the rest. */
	if (vbiosd_debug_report == B_FALSE) {
		closefrom(0);
		fd = open("/dev/null", O_RDWR);
		if (fd == -1) {
			dbg_msg = "unable to open /dev/null";
			goto out;
		}

		if (dup2(fd, 1) == -1 || dup2(fd, 2) == -1) {
			dbg_msg = "unable to sanitize 0,1 and 2 descriptors";
			goto out;
		}
	}
	return (VBIOSD_SUCCESS);

out:
	vbiosd_debug(LOG_ERRNO, dbg_msg);
	return (VBIOSD_ERROR);
}

static void
vbiosd_sanitize_env()
{
	/* Do not depend on caller umask. */
	(void) umask(022);

	/* Move to / */
	if (chdir("/") == -1)
		vbiosd_debug(LOG_ERRNO, "unable to chdir to /");
}

static void
vbiosd_block_signals()
{
	sigset_t	set;

	(void) sigfillset(&set);
	(void) sigprocmask(SIG_BLOCK, &set, NULL);
}

/*
 * Set 'handler' as the signal handler for 'sig'.
 */
void
vbiosd_setup_sighandler(int sig, int flags, void (*handler)())
{
	struct sigaction	sig_act;
	sigset_t		set;

	/* Do not nest signals. */
	(void) sigfillset(&sig_act.sa_mask);
	sig_act.sa_flags = flags;
	sig_act.sa_handler = handler;
	sigaction(sig, &sig_act, NULL);

	/* Unblock the signal in the thread. */
	(void) sigemptyset(&set);
	if (sigaddset(&set, sig) == -1) {
		vbiosd_debug(LOG_ERRNO, "sigaddset for %d failed", sig);
		return;
	}

	(void) sigprocmask(SIG_UNBLOCK, &set, NULL);
}

/*
 * SIGTHAW handler to trigger the restore of the console after resuming
 * from a suspended state.
 */
static void
vbiosd_thaw_hndlr()
{
	int	ret;

	ret = ioctl(vbiosd_console_fd, KDSETMODE, KD_RESUME);
	if (ret == -1)
		vbiosd_error(LOG_ERRNO, "unable to send KD_RESUME command");
}

/* ARGSUSED */
static void *
vbiosd_sigthaw_helper(void *unused)
{
	sigset_t	mask;

	vbiosd_block_signals();
	vbiosd_setup_sighandler(SIGTHAW, 0, vbiosd_thaw_hndlr);

	for (;;) {
		(void) sigprocmask(0, NULL, &mask);
		(void) sigsuspend(&mask);
	}
	/* NOTREACHED */
}

static int
vbiosd_thaw_helper_init()
{
	char			*prop_name = "do_call_on_resume";
	scf_simple_prop_t 	*call_on_resume = NULL;
	uint8_t			*val;
	pthread_t		tid;
	pthread_attr_t		attr;

	if ((call_on_resume = scf_simple_prop_get(NULL, FMRI_VBIOSD, "config",
	    prop_name)) == NULL) {
		vbiosd_debug(NO_ERRNO, "unable to get vbiosd %s property",
		    prop_name);
		return (1);
	}

	val = scf_simple_prop_next_boolean(call_on_resume);
	if (val == NULL || *val == 0) {
		scf_simple_prop_free(call_on_resume);
		return (1);
	}
	scf_simple_prop_free(call_on_resume);

	(void) pthread_attr_init(&attr);
	(void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&tid, &attr, vbiosd_sigthaw_helper, NULL) != 0) {
		vbiosd_debug(LOG_ERRNO, "unable to create SIGTHAW helper "
		    "thread");
		return (1);
	}

	return (0);
}

static void
vbiosd_debug_params()
{
	vbiosd_debug(NO_ERRNO, "running in debug mode");

	if (vbiosd_do_gdmcheck)
		vbiosd_debug(NO_ERRNO, "performing gdm check/console reset");

	if (vbiosd_daemonize)
		vbiosd_debug(NO_ERRNO, "running in daemon mode");
	else
		vbiosd_debug(NO_ERRNO, "not forking to the background");
}

int
main(int argc, char **argv)
{
	sigset_t	mask;

	vbiosd_parse_args(argc, argv);

	if (vbiosd_debug_report)
		vbiosd_debug_params();

	/* Mandatory checks. */
	if (geteuid() != 0)
		vbiosd_abort(NO_ERRNO, "must be root");

	if (getzoneid() != GLOBAL_ZONEID)
		vbiosd_abort(NO_ERRNO, "not running inside a global zone");

	/*
	 * Disable signals so that we do not have to worry about EINTR.
	 * Since we are there, setup handlers for common signals too.
	 */
	vbiosd_block_signals();
	vbiosd_setup_sighandler(SIGTERM, 0, vbiosd_term_hndlr);
	vbiosd_setup_sighandler(SIGINT, 0, vbiosd_term_hndlr);

	/* Should we go in the background ? */
	if (vbiosd_daemonize) {
		if (vbiosd_daemonize_me() == VBIOSD_ERROR)
			vbiosd_abort(NO_ERRNO, "unable to go in daemon mode");
		else
			vbiosd_is_daemon = B_TRUE;
	}

	/* Open the console device. */
	vbiosd_console_fd = open(FB_DEVICE, O_RDONLY);
	if (vbiosd_console_fd == -1) {
		vbiosd_abort(LOG_ERRNO, "unable to open %s", FB_DEVICE);
	}


	if (vbiosd_do_gdmcheck) {
		if (do_gdm_check() != 0) {
			vbiosd_abort(NO_ERRNO, "Unable to reset console");
		}
		/* gdmcheck is a self-contained command. Get out. */
		exit(EXIT_SUCCESS);
	}

	vbiosd_setup_log();

	vbiosd_debug(NO_ERRNO, "attempting to acquire lock on %s",
	    VBIOSD_LOCK_FILE);

	/* Check for other running instances. */
	if (vbiosd_acquire_flock(VBIOSD_LOCK_FILE) == VBIOSD_ERROR)
		vbiosd_abort(NO_ERRNO, "error acquiring exclusive lock");

	vbiosd_debug(NO_ERRNO, "lock acquired");

	/* Extra-sanitizing operation. */
	vbiosd_sanitize_env();

	/* Initialize the x86emu engine. */
	if (vbiosd_x86emu_setup() == VBIOSD_ERROR)
		vbiosd_abort(NO_ERRNO, "error initializing the emulation"
		    " engine");

	/* Setup the door server. */
	if (vbiosd_upcall_setup() == VBIOSD_ERROR)
		vbiosd_abort(NO_ERRNO, "setting up door server failed");

	if (vbiosd_is_daemon) {
		if (kill(getppid(), SIGUSR1) != 0)
			vbiosd_abort(NO_ERRNO, "unable to signal success to the"
			    " parent");
	}

	/* Start the SIGTHAW helper thread. */
	if (vbiosd_thaw_helper_init() != 0) {
		vbiosd_debug(NO_ERRNO, "SIGTHAW helper not running: no vbios "
		    "call will be made on resume");
	} else {
		vbiosd_debug(NO_ERRNO, "SIGTHAW helper thread up and running");
	}

	vbiosd_debug(NO_ERRNO, "waiting for commands");

	/* Forever is such a long time. */
	for (;;) {
		(void) sigprocmask(0, NULL, &mask);
		(void) sigsuspend(&mask);
	}
}

/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * BSD 3 Clause License
 *
 * Copyright (c) 2007, The Storage Networking Industry Association.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 	- Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *
 * 	- Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in
 *	  the documentation and/or other materials provided with the
 *	  distribution.
 *
 *	- Neither the name of The Storage Networking Industry Association (SNIA)
 *	  nor the names of its contributors may be used to endorse or promote
 *	  products derived from this software without specific prior written
 *	  permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/* Copyright (c) 1996, 1997 PDC, Network Appliance. All Rights Reserved */

#include <errno.h>
#include <signal.h>
#include <libgen.h>
#include <libscf.h>
#include <libintl.h>
#include <sys/wait.h>
#include <zone.h>
#include <tsol/label.h>
#include <dlfcn.h>
#include "ndmpd.h"
#include "ndmpd_common.h"

/* zfs library handle & mutex */
libzfs_handle_t *zlibh;
mutex_t	zlib_mtx;
void *mod_plp;

pthread_key_t thread_data_key = 0; /* Key to access per-thread data store */

static void ndmpd_sig_handler(int sig);

typedef struct ndmpd {
	int s_shutdown_flag;	/* Fields for shutdown control */
	int s_sigval;
} ndmpd_t;

ndmpd_t	ndmpd;


/*
 * Load and initialize the plug-in module
 */
static int
mod_init()
{
	char *plname;
	ndmp_plugin_t *(*plugin_init)(int);

	ndmp_pl = NULL;
	if ((plname = ndmpd_get_prop(NDMP_PLUGIN_PATH)) == NULL)
		return (0);

	if ((mod_plp = dlopen(plname, RTLD_LOCAL | RTLD_NOW)) == NULL) {
		syslog(LOG_ERR, "Error loading the plug-in %s", plname);
		return (0);
	}

	plugin_init = (ndmp_plugin_t *(*)(int))dlsym(mod_plp, "_ndmp_init");
	if (plugin_init == NULL) {
		(void) dlclose(mod_plp);
		return (0);
	}
	if ((ndmp_pl = plugin_init(NDMP_PLUGIN_VERSION)) == NULL) {
		syslog(LOG_ERR, "Error loading the plug-in %s", plname);
		return (-1);
	}
	return (0);
}

/*
 * Unload
 */
static void
mod_fini()
{
	if (ndmp_pl == NULL)
		return;

	void (*plugin_fini)(ndmp_plugin_t *);

	plugin_fini = (void (*)(ndmp_plugin_t *))dlsym(mod_plp, "_ndmp_fini");
	if (plugin_fini == NULL) {
		(void) dlclose(mod_plp);
		return;
	}
	plugin_fini(ndmp_pl);
	(void) dlclose(mod_plp);
}

static void
daemonize_init(char *arg)
{
	sigset_t set, oset;
	pid_t pid;
	priv_set_t *pset = priv_allocset();

	/*
	 * Set effective sets privileges to 'least' required. If fails, send
	 * error messages to log file and proceed.
	 */
	if (pset != NULL) {
		priv_basicset(pset);
		(void) priv_addset(pset, PRIV_PROC_AUDIT);
		(void) priv_addset(pset, PRIV_PROC_SETID);
		(void) priv_addset(pset, PRIV_PROC_OWNER);
		(void) priv_addset(pset, PRIV_FILE_CHOWN);
		(void) priv_addset(pset, PRIV_FILE_CHOWN_SELF);
		(void) priv_addset(pset, PRIV_FILE_DAC_READ);
		(void) priv_addset(pset, PRIV_FILE_DAC_SEARCH);
		(void) priv_addset(pset, PRIV_FILE_DAC_WRITE);
		(void) priv_addset(pset, PRIV_FILE_OWNER);
		(void) priv_addset(pset, PRIV_FILE_SETID);
		(void) priv_addset(pset, PRIV_SYS_LINKDIR);
		(void) priv_addset(pset, PRIV_SYS_DEVICES);
		(void) priv_addset(pset, PRIV_SYS_MOUNT);
		(void) priv_addset(pset, PRIV_SYS_CONFIG);
	}

	if (pset == NULL || setppriv(PRIV_SET, PRIV_EFFECTIVE, pset) != 0) {
		syslog(LOG_ERR, "Failed to set least required privileges to "
		    "the service.");
	}
	priv_freeset(pset);

	/*
	 * Block all signals prior to the fork and leave them blocked in the
	 * parent so we don't get in a situation where the parent gets SIGINT
	 * and returns non-zero exit status and the child is actually running.
	 * In the child, restore the signal mask once we've done our setsid().
	 */
	(void) sigfillset(&set);
	(void) sigdelset(&set, SIGABRT);
	(void) sigprocmask(SIG_BLOCK, &set, &oset);

	if ((pid = fork()) == -1) {
		openlog(arg, LOG_PID | LOG_NDELAY, LOG_DAEMON);
		syslog(LOG_ERR, "Failed to start process in background.");
		exit(SMF_EXIT_ERR_CONFIG);
	}

	/* If we're the parent process, exit. */
	if (pid != 0) {
		_exit(0);
	}
	(void) setsid();
	(void) sigprocmask(SIG_SETMASK, &oset, NULL);
	(void) chdir("/");
	(void) umask(0);
}

static void
daemonize_fini(void)
{
	int fd;

	if ((fd = open("/dev/null", O_RDWR)) >= 0) {
		(void) fcntl(fd, F_DUP2FD, STDIN_FILENO);
		(void) fcntl(fd, F_DUP2FD, STDOUT_FILENO);
		(void) fcntl(fd, F_DUP2FD, STDERR_FILENO);
		(void) close(fd);
	}
}

/*
 * free_thread_data is automatically called to free up a thread_data allocation
 * upon thread termination.  This is done automatically, regardless of the
 * mechanism by which the thread is terminated.
 */
void
free_thread_data(void *data_ptr)
{
	free(data_ptr);
}

/*
 * main
 *
 * The main NDMP daemon function
 *
 * Parameters:
 *   argc (input) - the argument count
 *   argv (input) - command line options
 *
 * Returns:
 *   0
 */
int
/* LINTED argument unused in function */
main(int argc, char *argv[])
{
	struct sigaction act;
	sigset_t set;
	void *arg = 0;

	openlog(argv[0], LOG_PID | LOG_NDELAY, LOG_DAEMON);

	/*
	 * Check for existing ndmpd door server (make sure ndmpd is not already
	 * running)
	 */
	if (ndmp_door_check()) {
		/* ndmpd is already running, exit. */
		return (0);
	}

	/* load ENVs */
	if (ndmpd_load_prop()) {
		syslog(LOG_ERR,
		    "%s SMF properties initialization failed.", argv[0]);
		exit(SMF_EXIT_ERR_CONFIG);
	}

	/* Global zone check */
	if (getzoneid() != GLOBAL_ZONEID) {
		syslog(LOG_ERR, "Local zone not supported.");
		exit(SMF_EXIT_ERR_FATAL);
	}

	/* Trusted Solaris check */
	if (is_system_labeled()) {
		syslog(LOG_ERR, "Trusted Solaris not supported.");
		exit(SMF_EXIT_ERR_FATAL);
	}

	opterr = 0;
	closelog();

	/*
	 * close any open file descriptors which are greater
	 * than STDERR_FILENO
	 */
	closefrom(STDERR_FILENO + 1);

	/* set up signal handler */
	(void) sigfillset(&set);
	(void) sigdelset(&set, SIGABRT); /* always unblocked for ASSERT() */
	(void) sigfillset(&act.sa_mask);
	act.sa_handler = ndmpd_sig_handler;
	act.sa_flags = 0;

	(void) sigaction(SIGTERM, &act, NULL);
	(void) sigaction(SIGHUP, &act, NULL);
	(void) sigaction(SIGINT, &act, NULL);
	(void) sigaction(SIGUSR1, &act, NULL);
	(void) sigaction(SIGPIPE, &act, NULL);
	(void) sigdelset(&set, SIGTERM);
	(void) sigdelset(&set, SIGHUP);
	(void) sigdelset(&set, SIGINT);
	(void) sigdelset(&set, SIGUSR1);
	(void) sigdelset(&set, SIGPIPE);

	(void) daemonize_init(argv[0]);

	openlog(argv[0], LOG_PID | LOG_NDELAY, LOG_DAEMON);
	(void) mutex_init(&log_lock, 0, NULL);
	(void) mutex_init(&ndmpd_zfs_fd_lock, 0, NULL);

	ndmpd_cpu_init();

	if (mod_init() != 0) {
		syslog(LOG_ERR, "Failed to load the plugin module.");
		exit(SMF_EXIT_ERR_CONFIG);
	}

	/* libzfs init */
	if ((zlibh = libzfs_init()) == NULL) {
		syslog(LOG_ERR, "Failed to initialize ZFS library.");
		exit(SMF_EXIT_ERR_CONFIG);
	}

	/* initialize and start the door server */
	if (ndmp_door_init()) {
		syslog(LOG_ERR, "Can not start ndmpd door server.");
		exit(SMF_EXIT_ERR_CONFIG);
	}

	if (tlm_init() == -1) {
		syslog(LOG_ERR, "Failed to initialize tape manager.");
		exit(SMF_EXIT_ERR_CONFIG);
	}

	/* Set up the per-thread data store mechanism */

	(void) pthread_key_create(&thread_data_key, &free_thread_data);

	/*
	 * Prior to this point, we are single-threaded. We will be
	 * multi-threaded from this point on.
	 */
	(void) pthread_create(NULL, NULL, (funct_t)ndmpd_main, (void *)&arg);

	while (!ndmpd.s_shutdown_flag) {
		(void) sigsuspend(&set);

		switch (ndmpd.s_sigval) {
		case 0:
			break;

		case SIGPIPE:
			break;

		case SIGHUP:
			/* Refresh SMF properties */
			if (ndmpd_load_prop())
				syslog(LOG_ERR,
				    "Service properties initialization "
				    "failed.");
			break;

		default:
			/*
			 * Typically SIGINT or SIGTERM.
			 */
			ndmpd.s_shutdown_flag = 1;
			break;
		}

		ndmpd.s_sigval = 0;
	}

	ndmpd_cpu_fini();

	(void) mutex_destroy(&ndmpd_zfs_fd_lock);
	(void) mutex_destroy(&log_lock);
	libzfs_fini(zlibh);
	mod_fini();
	ndmp_door_fini();
	daemonize_fini();
	return (SMF_EXIT_OK);
}

static void
ndmpd_sig_handler(int sig)
{
	if (ndmpd.s_sigval == 0)
		ndmpd.s_sigval = sig;
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

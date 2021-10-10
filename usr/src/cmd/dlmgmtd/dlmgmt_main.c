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

/*
 * The dlmgmtd daemon is started by the datalink-management SMF service.
 * This daemon is used to manage <link name, linkid> mapping and the
 * persistent datalink configuration.
 *
 * Today, the <link name, linkid> mapping and the persistent configuration
 * of datalinks is kept in /etc/dladm/datalink.conf, and the daemon keeps
 * a copy of the datalinks in the memory (see dlmgmt_id_avl and
 * dlmgmt_name_avl). The active <link name, linkid> mapping is kept in
 * _PATH_SYSVOL/dladm cache file, so that the mapping can be recovered
 * when dlmgmtd exits for some reason (e.g., when dlmgmtd is accidentally
 * killed).
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <priv.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <syslog.h>
#include <zone.h>
#include <sys/dld.h>
#include <sys/dld_ioc.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libscf.h>
#include <libdladm_impl.h>
#include <libdlmgmt.h>
#include "dlmgmt_impl.h"

const char		*progname;
boolean_t		fg = B_FALSE;
boolean_t		debug = B_FALSE;
/* Take initial topological snapshot. */
boolean_t		reconfigure = B_FALSE;
static int		pfds[2];
/*
 * This file descriptor to DLMGMT_DOOR cannot be in the libdladm
 * handle because the door isn't created when the handle is created.
 */
static int		dlmgmt_door_fd = -1;

/*
 * This libdladm handle is global so that dlmgmt_upcall_linkprop_init() can
 * pass to libdladm.  The handle is opened with "ALL" privileges, before
 * privileges are dropped in dlmgmt_drop_privileges().  It is not able to open
 * DLMGMT_DOOR at that time as it hasn't been created yet.  This door in the
 * handle is opened in the first call to dladm_door_fd().
 */
dladm_handle_t		dld_handle = NULL;

/* Used to debug in miniroot environment */
int 			msglog_fd = -1;
FILE			*msglog_fp = NULL;

/*
 * The privs mutex is used to protect privilege escalation and deescalation,
 * and the privs_refcnt tracks the number of threads that have requested priv
 * escalation.  Since privs are escalated/dropped on a per-process basis,
 * we do not want to drop privs until the refcnt is 0 - otherwise actions in
 * threads that assume they are running with elevated privs can fail.
 */
pthread_mutex_t		dlmgmt_privs_mutex = PTHREAD_MUTEX_INITIALIZER;
int			dlmgmt_privs_refcnt = 0;

static void		dlmgmtd_exit(int);
static int		dlmgmt_init(void);
static void		dlmgmt_fini();
static int		dlmgmt_set_privileges();

static int
dlmgmt_set_doorfd(boolean_t start)
{
	dld_ioc_door_t did;
	int err = 0;

	assert(dld_handle != NULL);

	did.did_start_door = start;

	if (ioctl(dladm_dld_fd(dld_handle), DLDIOC_DOORSERVER, &did) == -1)
		err = errno;

	return (err);
}

static int
dlmgmt_door_init(void)
{
	int err = 0;

	if ((dlmgmt_door_fd = door_create(dlmgmt_handler, NULL,
	    DOOR_REFUSE_DESC | DOOR_NO_CANCEL)) == -1) {
		err = errno;
		dlmgmt_log(LOG_ERR, "door_create() failed: %s",
		    strerror(err));
		return (err);
	}
	return (err);
}

static void
dlmgmt_door_fini(void)
{
	if (dlmgmt_door_fd == -1)
		return;

	if (door_revoke(dlmgmt_door_fd) == -1) {
		dlmgmt_log(LOG_WARNING, "door_revoke(%s) failed: %s",
		    DLMGMT_DOOR, strerror(errno));
	}
	(void) dlmgmt_set_doorfd(B_FALSE);
	dlmgmt_door_fd = -1;
}

int
dlmgmt_door_attach(zoneid_t zoneid, char *rootdir)
{
	int	fd;
	int	err = 0;
	char	doorpath[MAXPATHLEN];

	(void) snprintf(doorpath, sizeof (doorpath), "%s%s", rootdir,
	    DLMGMT_DOOR);

	/*
	 * Create the door file for dlmgmtd.
	 */
	if ((fd = open(doorpath, O_CREAT|O_RDONLY, 0644)) == -1) {
		err = errno;
		dlmgmt_log(LOG_ERR, "open(%s) failed: %s", doorpath,
		    strerror(err));
		return (err);
	}
	(void) close(fd);
	if (chown(doorpath, UID_DLADM, GID_NETADM) == -1)
		return (errno);

	/*
	 * fdetach first in case a previous daemon instance exited
	 * ungracefully.
	 */
	(void) fdetach(doorpath);
	if (fattach(dlmgmt_door_fd, doorpath) != 0) {
		err = errno;
		dlmgmt_log(LOG_ERR, "fattach(%s) failed: %s", doorpath,
		    strerror(err));
	} else if (zoneid == GLOBAL_ZONEID) {
		if ((err = dlmgmt_set_doorfd(B_TRUE)) != 0) {
			dlmgmt_log(LOG_ERR, "cannot set kernel doorfd: %s",
			    strerror(err));
		}
	}

	return (err);
}

/*
 * Create the _PATH_SYSVOL/dladm directory if it doesn't exist, load the
 * datalink.conf data for this zone, and create/attach the door rendezvous
 * file.
 */
int
dlmgmt_zone_init(zoneid_t zoneid)
{
	char	rootdir[MAXPATHLEN], tmpfsdir[MAXPATHLEN];
	int	err;
	struct stat statbuf;

	if (zoneid == GLOBAL_ZONEID) {
		rootdir[0] = '\0';
	} else if (zone_getattr(zoneid, ZONE_ATTR_ROOT, rootdir,
	    sizeof (rootdir)) < 0) {
		return (errno);
	}

	/*
	 * Create the DLMGMT_TMPFS_DIR directory.
	 */
	(void) snprintf(tmpfsdir, sizeof (tmpfsdir), "%s%s", rootdir,
	    DLMGMT_TMPFS_DIR);
	if (stat(tmpfsdir, &statbuf) < 0) {
		if (mkdir(tmpfsdir, (mode_t)0755) < 0)
			return (errno);
	} else if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
		return (ENOTDIR);
	}

	if ((chmod(tmpfsdir, 0755) < 0) ||
	    (chown(tmpfsdir, UID_DLADM, GID_NETADM) < 0)) {
		return (EPERM);
	}

	if ((err = dlmgmt_db_init(zoneid)) != 0)
		return (err);
	/*
	 * In reconfigure scenarios, force location information retrieval
	 * before informing the parent process to exit.  This ensures that
	 * time-consuming snapshot retrieval occurs before datalink-management
	 * is moved online.  This prevents timeouts for network interface
	 * configuration in network/physical.  Boot time performance will
	 * only suffer on reconfiguration boots.
	 */
	if (zoneid == GLOBAL_ZONEID && reconfigure)
		dlmgmt_physloc_init();

	return (dlmgmt_door_attach(zoneid, rootdir));
}

/*
 * Initialize each running zone.
 */
static int
dlmgmt_allzones_init(void)
{
	int		err, i;
	zoneid_t	*zids = NULL;
	uint_t		nzids;

	if (zone_get_zoneids(&zids, &nzids) != 0)
		return (errno);

	for (i = 0; i < nzids; i++) {
		if ((err = dlmgmt_zone_init(zids[i])) != 0)
			break;
	}
	free(zids);
	return (err);
}

static int
dlmgmt_init(void)
{
	int	err;
	char	*fmri, *c;
	char	filename[MAXPATHLEN];

	if (dladm_open(&dld_handle) != DLADM_STATUS_OK) {
		dlmgmt_log(LOG_ERR, "dladm_open() failed");
		return (EPERM);
	}

	if (signal(SIGTERM, dlmgmtd_exit) == SIG_ERR ||
	    signal(SIGINT, dlmgmtd_exit) == SIG_ERR) {
		err = errno;
		dlmgmt_log(LOG_ERR, "signal() for SIGTERM/INT failed: %s",
		    strerror(err));
		return (err);
	}

	/*
	 * First derive the name of the cache file from the FMRI name. This
	 * cache name is used to keep active datalink configuration.
	 */
	if (fg) {
		(void) snprintf(cachefile, MAXPATHLEN, "%s/%s%s",
		    DLMGMT_TMPFS_DIR, progname, ".debug.cache");
	} else {
		if ((fmri = getenv("SMF_FMRI")) == NULL) {
			dlmgmt_log(LOG_ERR, "dlmgmtd is an smf(5) managed "
			    "service and should not be run from the command "
			    "line.");
			return (EINVAL);
		}

		/*
		 * The FMRI name is in the form of
		 * svc:/service/service:instance.  We need to remove the
		 * prefix "svc:/" and replace '/' with '-'.  The cache file
		 * name is in the form of "service:instance.cache".
		 */
		if ((c = strchr(fmri, '/')) != NULL)
			c++;
		else
			c = fmri;
		(void) snprintf(filename, MAXPATHLEN, "%s.cache", c);
		c = filename;
		while ((c = strchr(c, '/')) != NULL)
			*c = '-';

		(void) snprintf(cachefile, MAXPATHLEN, "%s/%s",
		    DLMGMT_TMPFS_DIR, filename);
	}

	dlmgmt_linktable_init();

	/*
	 * Bump privs refcnt to ensure that door threads that elevate privs
	 * (in particular those resulting from datalink create upcalls)
	 * do not drop privs on completion, as this would cause
	 * dlmgmt_set_privileges() to fail.  These door calls can occur once
	 * the door has been created, so can (and do) happen prior to calling
	 * dlmgmt_set_privileges().  The latter function will reduce this
	 * reference count on completion by calling dlmgmt_drop_privileges().
	 */
	(void) pthread_mutex_lock(&dlmgmt_privs_mutex);
	dlmgmt_privs_refcnt = 1;
	(void) pthread_mutex_unlock(&dlmgmt_privs_mutex);

	if ((err = dlmgmt_door_init()) != 0)
		goto done;

	/*
	 * Load datalink configuration and create dlmgmtd door files for all
	 * currently running zones.
	 */
	if ((err = dlmgmt_allzones_init()) != 0) {
		dlmgmt_door_fini();
		goto done;
	}

	if ((err = dlmgmt_set_privileges()) != 0) {
		dlmgmt_log(LOG_ERR, "unable to set daemon privileges: %s",
		    strerror(err));
	}
done:
	if (err != 0)
		dlmgmt_linktable_fini();
	return (err);
}

static void
dlmgmt_fini(void)
{
	dlmgmt_door_fini();
	dlmgmt_linktable_fini();
	if (dld_handle != NULL) {
		dladm_close(dld_handle);
		dld_handle = NULL;
	}
}

/*
 * This is called by the child process to inform the parent process to
 * exit with the given return value.
 */
static void
dlmgmt_inform_parent_exit(int rv)
{
	if (fg)
		return;

	if (write(pfds[1], &rv, sizeof (int)) != sizeof (int)) {
		dlmgmt_log(LOG_WARNING,
		    "dlmgmt_inform_parent_exit() failed: %s", strerror(errno));
		(void) close(pfds[1]);
		exit(EXIT_FAILURE);
	}
	(void) close(pfds[1]);
}

/*ARGSUSED*/
static void
dlmgmtd_exit(int signo)
{
	(void) close(pfds[1]);
	dlmgmt_fini();
	exit(EXIT_FAILURE);
}

static void
usage(void)
{
	(void) fprintf(stderr, "Usage: %s [-fd]\n", progname);
	exit(EXIT_FAILURE);
}

/*
 * Restrict privileges to only those needed.
 */
int
dlmgmt_drop_privileges(void)
{
	priv_set_t	*pset;
	priv_ptype_t	ptype;
	zoneid_t	zoneid = getzoneid();
	int		err = 0;

	/* If another thread still has elevated privs, do not drop. */
	(void) pthread_mutex_lock(&dlmgmt_privs_mutex);
	if (--dlmgmt_privs_refcnt > 0) {
		(void) pthread_mutex_unlock(&dlmgmt_privs_mutex);
		return (0);
	}

	if ((pset = priv_allocset()) == NULL) {
		err = errno;
		goto done;
	}

	/*
	 * The global zone needs PRIV_PROC_FORK so that it can fork() when it
	 * issues db ops in non-global zones, PRIV_SYS_CONFIG to post
	 * sysevents, and PRIV_SYS_DL_CONFIG to initialize link properties in
	 * dlmgmt_upcall_linkprop_init().
	 *
	 * We remove non-basic privileges from the permitted (and thus
	 * effective) set.  When executing in a non-global zone, dlmgmtd
	 * only needs to read and write to files that it already owns.
	 */
	priv_basicset(pset);
	(void) priv_delset(pset, PRIV_PROC_EXEC);
	(void) priv_delset(pset, PRIV_PROC_INFO);
	(void) priv_delset(pset, PRIV_PROC_SESSION);
	(void) priv_delset(pset, PRIV_FILE_LINK_ANY);
	if (zoneid == GLOBAL_ZONEID) {
		ptype = PRIV_EFFECTIVE;
		if (priv_addset(pset, PRIV_SYS_CONFIG) == -1 ||
		    priv_addset(pset, PRIV_SYS_DL_CONFIG) == -1)
			err = errno;
	} else {
		(void) priv_delset(pset, PRIV_PROC_FORK);
		ptype = PRIV_PERMITTED;
	}
	if (err == 0 && setppriv(PRIV_SET, ptype, pset) == -1)
		err = errno;
done:
	priv_freeset(pset);

	(void) pthread_mutex_unlock(&dlmgmt_privs_mutex);
	return (err);
}

int
dlmgmt_elevate_privileges(void)
{
	priv_set_t	*privset;
	int		err = 0;

	/* If another thread still has elevated privs, do not re-elevate. */
	(void) pthread_mutex_lock(&dlmgmt_privs_mutex);
	if (++dlmgmt_privs_refcnt > 1) {
		(void) pthread_mutex_unlock(&dlmgmt_privs_mutex);
		return (0);
	}

	if ((privset = priv_str_to_set("zone", ",", NULL)) == NULL) {
		err = errno;
		goto done;
	}
	/*
	 * Need to add PRIV_SYS_DEVICES in globalzone in order to take device
	 * tree/libtopo snapshots.
	 */
	if (getzoneid() == GLOBAL_ZONEID &&
	    priv_addset(privset, PRIV_SYS_DEVICES) == -1) {
		priv_freeset(privset);
		err = errno;
		goto done;
	}

	if (setppriv(PRIV_SET, PRIV_EFFECTIVE, privset) == -1)
		err = errno;
	priv_freeset(privset);
done:
	if (err != 0)
		dlmgmt_privs_refcnt--;
	(void) pthread_mutex_unlock(&dlmgmt_privs_mutex);
	return (err);
}

/*
 * Set the uid of this daemon to the "dladm" user and drop privileges to only
 * those needed.
 */
static int
dlmgmt_set_privileges(void)
{
	int err = 0;

	(void) setgroups(0, NULL);
	if (setegid(GID_NETADM) == -1 || seteuid(UID_DLADM) == -1) {
		err = errno;
		(void) dlmgmt_drop_privileges();
	} else {
		err = dlmgmt_drop_privileges();
	}
done:
	return (err);
}

/*
 * Keep the pfds fd (and stderr if running in fg) open, close other fds.
 */
/*ARGSUSED*/
static int
closefunc(void *arg, int fd)
{
	if (fd != pfds[1] && (!fg || fd != STDERR_FILENO) &&
	    (msglog_fd == 0 || fd != STDERR_FILENO) && fd != msglog_fd)
		(void) close(fd);
	return (0);
}

static boolean_t
dlmgmt_daemonize(void)
{
	pid_t pid;
	int rv;
	int err;

	if (pipe(pfds) < 0) {
		err = errno;
		(void) fprintf(stderr, "%s: pipe() failed: %s\n",
		    progname, strerror(err));
		dlmgmt_log(LOG_ERR, "pipe failed: %s", strerror(err));
		exit(EXIT_FAILURE);
	}

	if ((pid = fork()) == -1) {
		err = errno;
		(void) fprintf(stderr, "%s: fork() failed: %s\n",
		    progname, strerror(err));
		dlmgmt_log(LOG_ERR, "fork failed: %s", strerror(err));
		exit(EXIT_FAILURE);
	} else if (pid > 0) { /* Parent */
		(void) close(pfds[1]);

		/*
		 * Read the child process's return value from the pfds.
		 * If the child process exits unexpected, read() returns -1.
		 */
		if (read(pfds[0], &rv, sizeof (int)) != sizeof (int)) {
			(void) kill(pid, SIGKILL);
			(void) fprintf(stderr, "got unexpected child error");
			dlmgmt_log(LOG_ERR, "got unexpected child error");
			rv = EXIT_FAILURE;
		}

		(void) close(pfds[0]);
		exit(rv);
	}

	/* Child */
	(void) close(pfds[0]);
	(void) setsid();

	/*
	 * Close all files except pfds[1].
	 */
	(void) fdwalk(closefunc, NULL);
	(void) chdir("/");
	openlog(progname, LOG_PID | LOG_NOWAIT, LOG_DAEMON);

	return (B_TRUE);
}

int
main(int argc, char *argv[])
{
	int opt, err;
	boolean_t reconfigureprop;
	char debugprop[MAXNAMELEN];

	progname = strrchr(argv[0], '/');
	if (progname != NULL)
		progname++;
	else
		progname = argv[0];

	/*
	 * debug property can be set to "disabled", "enabled" and "msglog".
	 * The latter option is useful during net/live media boot.
	 */
	if (dlmgmt_smf_get_string_property(DLMGMT_FMRI, DLMGMT_CFG_PG,
	    DLMGMT_CFG_DEBUG_PROP, debugprop, MAXNAMELEN) == 0) {
		if (strcmp(debugprop, "disabled") != 0)
			debug = B_TRUE;
		if (strcmp(debugprop, "msglog") == 0) {
			msglog_fd = open("/dev/msglog", O_WRONLY);
			msglog_fp = fdopen(msglog_fd, "a");
		}
	}
	if (dlmgmt_smf_get_boolean_property(RESTARTER_FMRI, SYSTEM_PG,
	    RECONFIGURE_PROP, &reconfigureprop) == 0)
		reconfigure = reconfigureprop;

	/*
	 * Process options.
	 */
	while ((opt = getopt(argc, argv, "df")) != EOF) {
		switch (opt) {
		case 'd':
			debug = B_TRUE;
			break;
		case 'f':
			fg = B_TRUE;
			break;
		default:
			usage();
		}
	}

	if (!fg && !dlmgmt_daemonize())
		return (EXIT_FAILURE);

	if ((err = dlmgmt_init()) != 0) {
		dlmgmt_log(LOG_ERR, "unable to initialize daemon: %s",
		    strerror(err));
		goto child_out;
	}

	/*
	 * Inform the parent process that it can successfully exit.
	 */
	dlmgmt_inform_parent_exit(EXIT_SUCCESS);

	for (;;)
		(void) pause();

child_out:
	/* return from main() forcibly exits an MT process */
	dlmgmt_inform_parent_exit(EXIT_FAILURE);
	return (EXIT_FAILURE);
}

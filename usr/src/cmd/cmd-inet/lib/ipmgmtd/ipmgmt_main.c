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
 * The ipmgmtd daemon is started by ip-interface-management SMF service. This
 * daemon is used to manage, mapping of 'address object' to 'interface name' and
 * 'logical interface number', on which the address is created. It also provides
 * a means to update the ipadm persistent data-store.
 *
 * The daemon tracks the <addrobj, lifname> mapping in-memory using a linked
 * list `aobjmap'. Access to this list is synchronized using a readers-writers
 * lock. The active <addrobj, lifname> mapping is kept in
 * _PATH_SYSVOL/ipadm/aobjmap.conf cache file, so that the mapping can be
 * recovered when ipmgmtd exits for some reason (e.g., when ipmgmtd is restarted
 * using svcadm or accidentally killed).
 *
 * Today, the persistent configuration of interfaces, addresses and protocol
 * properties is kept in /etc/ipadm/ipadm.conf. The access to the persistent
 * data store is synchronized using reader-writers lock `ipmgmt_dbconf_lock'.
 *
 * The communication between the library, libipadm.so and the daemon, is through
 * doors RPC. The library interacts with the daemon using the commands defined
 * by `ipmgmt_door_cmd_type_t'. Further any 'write' operation would require
 * the `NETWORK_INTERFACE_CONFIG_AUTH' authorization.
 *
 * On reboot, the aforementioned SMF service starts the daemon before any other
 * networking service that configures network IP interfaces is started.
 * Afterwards, the network/physical SMF script instantiates the persisted
 * network interfaces, interface properties and addresses.
 */

#include <errno.h>
#include <fcntl.h>
#include <priv_utils.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ipmgmt_impl.h"
#include <zone.h>
#include <libipadm.h>
#include <libdladm.h>
#include <libdllink.h>
#include <net/route.h>
#include <ipadm_ipmgmt.h>
#include <sys/brand.h>
#include <s10_brand.h>

const char		*progname;

/* signal handling */
static sigset_t		sigwaitset;

/* readers-writers lock for reading/writing daemon data store */
pthread_rwlock_t	ipmgmt_dbconf_lock = PTHREAD_RWLOCK_INITIALIZER;

/* tracks address object to {ifname|logical number|interface id} mapping */
ipmgmt_aobjmap_list_t	aobjmap;

/* used to communicate failure to parent process, which spawned the daemon */
static int		pfds[2];

/* file descriptor to IPMGMT_DOOR */
static int		ipmgmt_door_fd = -1;

static void		ipmgmt_exit();
static int		ipmgmt_init();
static int		ipmgmt_init_privileges();
static void		ipmgmt_ngz_persist_if();

static ipadm_handle_t iph;
typedef struct ipmgmt_pif_s {
	struct ipmgmt_pif_s	*pif_next;
	char			pif_ifname[LIFNAMSIZ];
	boolean_t		pif_v4;
	boolean_t		pif_v6;
} ipmgmt_pif_t;

static ipmgmt_pif_t *ngz_pifs;

static int
ipmgmt_db_init()
{
	int		fd, err, scferr;
	scf_resources_t	res;
	boolean_t	upgrade = B_TRUE;

	/*
	 * Check to see if we need to upgrade the data-store. We need to
	 * upgrade, if the version of the data-store does not match with
	 * IPADM_DB_VERSION. Further, if we cannot determine the current
	 * version of the data-store, we always err on the side of caution
	 * and upgrade the data-store to current version.
	 */
	if ((scferr = ipmgmt_create_scf_resources(IPMGMTD_FMRI, &res)) == 0)
		upgrade = ipmgmt_needs_upgrade(&res);
	if (upgrade) {
		err = ipmgmt_db_walk(ipmgmt_db_upgrade, NULL, IPADM_DB_WRITE);
		if (err != 0) {
			ipmgmt_log(LOG_ERR, "could not upgrade the "
			    "ipadm data-store: %s", strerror(err));
			err = 0;
		} else {
			/*
			 * upgrade was success, let's update SCF with the
			 * current data-store version number.
			 */
			if (scferr == 0)
				ipmgmt_update_dbver(&res);
		}
	}
	if (scferr == 0)
		ipmgmt_release_scf_resources(&res);

	/* creates the address object data store, if it doesn't exist */
	if ((fd = open(ADDROBJ_MAPPING_DB_FILE, O_CREAT|O_RDONLY,
	    IPADM_FILE_MODE)) == -1) {
		err = errno;
		ipmgmt_log(LOG_ERR, "could not open %s: %s",
		    ADDROBJ_MAPPING_DB_FILE, strerror(err));
		return (err);
	}
	(void) close(fd);

	aobjmap.aobjmap_head = NULL;
	(void) pthread_rwlock_init(&aobjmap.aobjmap_rwlock, NULL);

	/*
	 * If the daemon is recovering from a crash or restart, read the
	 * address object to logical interface mapping and build an in-memory
	 * representation of the mapping. That is, build `aobjmap' structure
	 * from address object data store.
	 */
	if ((err = ipadm_rw_db(ipmgmt_aobjmap_init, NULL,
	    ADDROBJ_MAPPING_DB_FILE, 0, IPADM_DB_READ)) != 0) {
		/* if there was nothing to initialize, it's fine */
		if (err != ENOENT)
			return (err);
		err = 0;
	}

	ipmgmt_ngz_persist_if(); /* create persistent interface info for NGZ */

	return (err);
}

static int
ipmgmt_door_init()
{
	int fd;
	int err;

	/* create the door file for ipmgmtd */
	if ((fd = open(IPMGMT_DOOR, O_CREAT|O_RDONLY, IPADM_FILE_MODE)) == -1) {
		err = errno;
		ipmgmt_log(LOG_ERR, "could not open %s: %s",
		    IPMGMT_DOOR, strerror(err));
		return (err);
	}
	(void) close(fd);

	if ((ipmgmt_door_fd = door_create(ipmgmt_handler, NULL,
	    DOOR_REFUSE_DESC | DOOR_NO_CANCEL)) == -1) {
		err = errno;
		ipmgmt_log(LOG_ERR, "failed to create door: %s", strerror(err));
		return (err);
	}
	/*
	 * fdetach first in case a previous daemon instance exited
	 * ungracefully.
	 */
	(void) fdetach(IPMGMT_DOOR);
	if (fattach(ipmgmt_door_fd, IPMGMT_DOOR) != 0) {
		err = errno;
		ipmgmt_log(LOG_ERR, "failed to attach door to %s: %s",
		    IPMGMT_DOOR, strerror(err));
		goto fail;
	}
	return (0);
fail:
	(void) door_revoke(ipmgmt_door_fd);
	ipmgmt_door_fd = -1;
	return (err);
}

static void
ipmgmt_door_fini()
{
	if (ipmgmt_door_fd == -1)
		return;

	(void) fdetach(IPMGMT_DOOR);
	if (door_revoke(ipmgmt_door_fd) == -1) {
		ipmgmt_log(LOG_ERR, "failed to revoke access to door %s: %s",
		    IPMGMT_DOOR, strerror(errno));
	}
}

static int
ipmgmt_init()
{
	int err;

	if ((err = ipmgmt_db_init()) != 0 || (err = ipmgmt_door_init()) != 0)
		return (err);
	return (0);
}

/*
 * This is called by the child process to inform the parent process to
 * exit with the given return value.
 */
static void
ipmgmt_inform_parent_exit(int rv)
{
	if (write(pfds[1], &rv, sizeof (int)) != sizeof (int)) {
		ipmgmt_log(LOG_WARNING,
		    "failed to inform parent process of status: %s",
		    strerror(errno));
		(void) close(pfds[1]);
		exit(EXIT_FAILURE);
	}
	(void) close(pfds[1]);
}

static void
ipmgmt_exit()
{
	(void) close(pfds[1]);
	ipmgmt_door_fini();
	exit(EXIT_FAILURE);
}

/*
 * ipmgmt_persist_if_cb() is used in non-global zones to track the interfaces
 * that have IP address configuration assignments from the global zone.
 * Persistent configuration for the interfaces is created by ipmgmtd, and the
 * addresses assigned to the interfaces by the GZ will be subsequently
 * configured when the interface is enabled.  Note that ipmgmt_persist_if_cb()
 * only sets up a list of interfaces that need to be persisted- the actual
 * update of the ipadm data-store happens in ipmgmt_persist_if() after the
 * appropriate privs/uid state has been set up.
 */
static void
ipmgmt_persist_if_cb(char *ifname, boolean_t v4, boolean_t v6)
{
	ipmgmt_pif_t *pif;

	pif = calloc(1, sizeof (*pif));
	if (pif == NULL) {
		ipmgmt_log(LOG_WARNING,
		    "Could not allocate memory to configure %s", ifname);
		return;
	}
	(void) strlcpy(pif->pif_ifname, ifname, sizeof (pif->pif_ifname));
	pif->pif_v4 = v4;
	pif->pif_v6 = v6;
	pif->pif_next = ngz_pifs;
	ngz_pifs = pif;
}

/*
 * ipmgmt_ngz_init() initializes exclusive-IP stack non-global zones by
 * extracting configuration that has been saved in the kernel and applying
 * it at zone boot.
 */
static void
ipmgmt_ngz_init()
{
	zoneid_t zoneid;
	boolean_t s10c = B_FALSE;
	char brand[MAXNAMELEN];
	ipadm_status_t ipstatus;

	zoneid = getzoneid();
	if (zoneid != GLOBAL_ZONEID) {

		if (zone_getattr(zoneid, ZONE_ATTR_BRAND, brand,
		    sizeof (brand)) < 0) {
			ipmgmt_log(LOG_ERR, "Could not get brand name");
			return;
		}

		if (strcmp(brand, S10_BRANDNAME) == 0)
			s10c = B_TRUE;

		ipstatus = ipadm_open(&iph, IH_IPMGMTD);
		if (ipstatus != IPADM_SUCCESS) {
			ipmgmt_log(LOG_ERR, "could not open ipadm handle",
			    ipadm_status2str(ipstatus));
			return;
		}

		/*
		 * Only pass down the callback to persist the interface
		 * for solaris brand zones.
		 */
		(void) ipadm_init_net_from_gz(iph, NULL,
		    (s10c ? NULL : ipmgmt_persist_if_cb));
		ipadm_close(iph);
	}
}

/*
 * Set the uid of this daemon to the "netadm" user. Finish the following
 * operations before setuid() because they need root privileges:
 *
 *    - create the _PATH_SYSVOL/ipadm directory;
 *    - change its uid/gid to "netadm"/"netadm";
 */
static int
ipmgmt_init_privileges()
{
	struct stat	statbuf;
	int		err;

	/* create the IPADM_TMPFS_DIR directory */
	if (stat(IPADM_TMPFS_DIR, &statbuf) < 0) {
		if (mkdir(IPADM_TMPFS_DIR, (mode_t)0755) < 0) {
			err = errno;
			goto fail;
		}
	} else {
		if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
			err = ENOTDIR;
			goto fail;
		}
	}

	if ((chmod(IPADM_TMPFS_DIR, 0755) < 0) ||
	    (chown(IPADM_TMPFS_DIR, UID_NETADM, GID_NETADM) < 0)) {
		err = errno;
		goto fail;
	}

	/*
	 * initialize any NGZ specific network information before dropping
	 * privileges. We need these privileges to plumb IP interfaces handed
	 * down from the GZ (for dlpi_open() etc.) and also to configure the
	 * address itself (for any IPI_PRIV ioctls like SLIFADDR)
	 */
	ipmgmt_ngz_init();

	/*
	 * Apply all protocol module properties. We need to apply all protocol
	 * properties before we drop root privileges.
	 */
	ipmgmt_init_prop();

	/*
	 * Limit the privileges of this daemon and set the uid of this
	 * daemon to UID_NETADM.
	 *
	 * PRIV_SYS_IP_CONFIG is needed to reapply properties on SIGHUP.
	 */
	if (__init_daemon_priv(PU_RESETGROUPS|PU_CLEARLIMITSET, UID_NETADM,
	    GID_NETADM, PRIV_SYS_IP_CONFIG, NULL) == -1) {
		err = EPERM;
		goto fail;
	}

	return (0);
fail:
	(void) ipmgmt_log(LOG_ERR, "failed to initialize the daemon: %s",
	    strerror(err));
	return (err);
}

/*
 * Keep the pfds fd open, close other fds.
 */
/*ARGSUSED*/
static int
closefunc(void *arg, int fd)
{
	if (fd != pfds[1])
		(void) close(fd);
	return (0);
}

/*
 * We cannot use libc's daemon() because the door we create is associated with
 * the process ID. If we create the door before the call to daemon(), it will
 * be associated with the parent and it's incorrect. On the other hand if we
 * create the door later, after the call to daemon(), parent process exits
 * early and gives a false notion to SMF that 'ipmgmtd' is up and running,
 * which is incorrect. So, we have our own daemon() equivalent.
 */
static boolean_t
ipmgmt_daemonize(void)
{
	pid_t pid;
	int rv;

	if (pipe(pfds) < 0) {
		(void) fprintf(stderr, "%s: pipe() failed: %s\n",
		    progname, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if ((pid = fork()) == -1) {
		(void) fprintf(stderr, "%s: fork() failed: %s\n",
		    progname, strerror(errno));
		exit(EXIT_FAILURE);
	} else if (pid > 0) { /* Parent */
		(void) close(pfds[1]);

		/*
		 * Parent should not exit early, it should wait for the child
		 * to return Success/Failure. If the parent exits early, then
		 * SMF will think 'ipmgmtd' is up and would start all the
		 * depended services.
		 *
		 * If the child process exits unexpectedly, read() returns -1.
		 */
		if (read(pfds[0], &rv, sizeof (int)) != sizeof (int)) {
			(void) kill(pid, SIGKILL);
			rv = EXIT_FAILURE;
		}

		(void) close(pfds[0]);
		exit(rv);
	}

	/* Child */
	(void) close(pfds[0]);
	(void) setsid();

	/* close all files except pfds[1] */
	(void) fdwalk(closefunc, NULL);
	(void) chdir("/");
	openlog(progname, LOG_PID, LOG_DAEMON);
	return (B_TRUE);
}

static void
ipmgmt_refresh(void)
{
	ipmgmt_refresh_prop();
}

/* ARGSUSED */
static void *
sighandler(void *arg)
{
	int sig;

	for (;;) {
		sig = sigwait(&sigwaitset);

		switch (sig) {
		case SIGHUP:
			ipmgmt_refresh();
			break;
		case SIGTERM:
		case SIGINT:
			ipmgmt_exit();
			break;
		default:
			break;
		}
	}
	/* NOTREACHED */
	return (0);
}

static int
init_signalhandling(void)
{
	pthread_attr_t attr;
	pthread_t sighand;
	int err;

	/*
	 * Construct the set of signals that we explicitly want to deal with.
	 * We block these while we're still single-threaded; this block will
	 * be inherited by all the threads we create. The signal handling
	 * thread will sigwait() this same set of signals, and will thus
	 * receive and handle any that are sent to the process.
	 */
	(void) sigemptyset(&sigwaitset);
	(void) sigaddset(&sigwaitset, SIGHUP);
	(void) sigaddset(&sigwaitset, SIGTERM);
	(void) sigaddset(&sigwaitset, SIGINT);
	(void) pthread_sigmask(SIG_BLOCK, &sigwaitset, NULL);

	/*
	 * Create the signal handling thread.
	 */
	(void) pthread_attr_init(&attr);
	(void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	err = pthread_create(&sighand, &attr, sighandler, NULL);
	(void) pthread_attr_destroy(&attr);

	return (err);
}

int
main(int argc, char *argv[])
{
	int opt;
	boolean_t fg = B_FALSE;

	progname = strrchr(argv[0], '/');
	if (progname != NULL)
		progname++;
	else
		progname = argv[0];

	/* Process options */
	while ((opt = getopt(argc, argv, "f")) != EOF) {
		switch (opt) {
		case 'f':
			fg = B_TRUE;
			break;
		default:
			(void) fprintf(stderr, "Usage: %s [-f]\n", progname);
			return (EXIT_FAILURE);
		}
	}

	if (!fg && getenv("SMF_FMRI") == NULL) {
		(void) fprintf(stderr,
		    "ipmgmtd is a smf(5) managed service and cannot be run "
		    "from the command line.\n");
		return (EINVAL);
	}

	if (!fg && !ipmgmt_daemonize())
		return (EXIT_FAILURE);

	if (init_signalhandling() != 0)
		return (EXIT_FAILURE);

	if (ipmgmt_init_privileges() != 0)
		goto child_out;

	if (ipmgmt_init() != 0)
		goto child_out;

	ipmgmt_init_watcher();

	/* Inform the parent process that it can successfully exit */
	if (!fg)
		ipmgmt_inform_parent_exit(EXIT_SUCCESS);

	for (;;)
		(void) pause();

child_out:
	/* return from main() forcibly exits an MT process */
	if (!fg)
		ipmgmt_inform_parent_exit(EXIT_FAILURE);
	return (EXIT_FAILURE);
}

/*
 * Return TRUE if `ifname' has persistent configuration for the `af' address
 * family in the datastore
 */
static boolean_t
ipmgmt_persist_if_exists(char *ifname, sa_family_t af)
{
	ipmgmt_if_cbarg_t cbarg;

	bzero(&cbarg, sizeof (cbarg));
	cbarg.cb_ifname = ifname;
	(void) ipmgmt_db_walk(ipmgmt_db_searchif, &cbarg, IPADM_DB_READ);
	if ((af == AF_INET && cbarg.cb_hasv4) ||
	    (af == AF_INET6 && cbarg.cb_hasv6)) {
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Persist any NGZ interfaces assigned to us from the global zone if they do
 * not already exist in the persistent db. We need to
 * do this before any calls to ipadm_enable_if() can succeed (i.e.,
 * before opening up for door_calls), and after setuid to 'netadm' so that
 * the persistent db is created with the right permissions.
 */
static void
ipmgmt_ngz_persist_if()
{
	ipmgmt_pif_t *pif, *next;
	ipmgmt_setif_arg_t ifarg;

	for (pif = ngz_pifs; pif != NULL; pif = next) {
		next = pif->pif_next;
		bzero(&ifarg, sizeof (ifarg));
		(void) strlcpy(ifarg.ia_ifname, pif->pif_ifname,
		    sizeof (ifarg.ia_ifname));
		ifarg.ia_flags = IPMGMT_PERSIST;
		ifarg.ia_class = IPADMIF_CLASS_IP;
		if (pif->pif_v4 &&
		    !ipmgmt_persist_if_exists(pif->pif_ifname, AF_INET)) {
			ifarg.ia_family = AF_INET;
			(void) ipmgmt_persist_if(&ifarg);
		}
		if (pif->pif_v6 &&
		    !ipmgmt_persist_if_exists(pif->pif_ifname, AF_INET6)) {
			ifarg.ia_family = AF_INET6;
			(void) ipmgmt_persist_if(&ifarg);
		}
		free(pif);
	}
	ngz_pifs = NULL; /* no red herrings */
}

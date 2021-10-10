/*
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/* Copyright 1991 NCR Corporation - Dayton, Ohio, USA */

/* LINTLIBRARY */
/* PROTOLIB1 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

/* NLM server */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <tiuser.h>
#include <rpc/rpc.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/file.h>
#include <nfs/nfs.h>
#include <nfs/lm_nlm.h>
#include <nfs/nfssys.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <signal.h>
#include <netconfig.h>
#include <netdir.h>
#include <string.h>
#include <unistd.h>
#include <stropts.h>
#include <sys/tihdr.h>
#include <poll.h>
#include <sys/tiuser.h>
#include <netinet/tcp.h>
#include <libintl.h>
#include <libscf.h>
#include <thread.h>
#include <deflt.h>
#include <priv_utils.h>
#include <rpcsvc/daemon_utils.h>
#include "nfs_tbind.h"
#include "thrpool.h"
#include "smfcfg.h"

/*
 * Define the default maximum number of lock manager threads (servers).
 * This value needs to be large enough so that standard tests using
 * blocking locks don't fail.
 */
#define	NLM_SERVERS	20

#define	DEBUGVALUE	0
#define	TIMOUT		300	/* One-way RPC connections valid for 5 min. */
#define	RETX_TIMOUT	5	/* Retransmit RPC requests every 5 seconds. */
#define	GRACE		90	/* We have a 90-second grace period. */
#define	RET_OK		0	/* return code for no error */
#define	RET_ERR		33	/* return code for error(s) */
#define	MIN_LISTEN_BACKLOG	32	/* min value for listen_backlog */

uid_t	daemon_uid = DAEMON_UID;
gid_t	daemon_gid = DAEMON_GID;

static	int	nlmsvc(int fd, struct netbuf addrmask,
			struct netconfig *nconf);
static int nlmsvcpool(int maxservers);
static	int	convert_nconf_to_knconf(struct netconfig *,
			struct knetconfig *);
static	void	usage(void);

extern	int	_nfssys(int, void *);

static	char	*MyName;
static	struct lm_svc_args lsa;
static  NETSELDECL(defaultproviders)[] = { "/dev/tcp6", "/dev/tcp", "/dev/udp",
					"/dev/udp6", NULL };
/*
 * The following are all globals used by routines in nfs_tbind.c.
 */
size_t	end_listen_fds;		/* used by conn_close_oldest() */
size_t	num_fds = 0;		/* used by multiple routines */
int	listen_backlog = MIN_LISTEN_BACKLOG;
				/* used by bind_to_{provider,proto}() */
int	(*Mysvc)(int fd, struct netbuf,	struct netconfig *) = nlmsvc;
				/* used by cots_listen_event() */
int	max_conns_allowed = -1; /* used by cots_listen_event() */

int
main(int ac, char **av)
{
	char *dir = "/";
	int maxservers = NLM_SERVERS;
	int maxservers_set = 0;
	int logmaxservers = 0;
	int pid;
	int i;
	int grace_override = 0;
	char *provider = (char *)NULL;
	struct protob *protobp;
	NETSELPDECL(providerp);
	char defval[6];
	int ret, bufsz;

	MyName = *av;

	lsa.version = LM_SVC_CUR_VERS;
	lsa.debug = DEBUGVALUE;
	lsa.timout = TIMOUT;
	lsa.grace = GRACE;
	lsa.retransmittimeout = RETX_TIMOUT;

	/*
	 * Read in the values from SMF  first before we check
	 * commandline options so the options override the file.
	 */
	/*
	 * GRACE_PERIOD overrides "-g" cmdline option.
	 */
	bufsz = 6;
	ret = nfs_smf_get_prop("grace_period", defval, DEFAULT_INSTANCE,
	    SCF_TYPE_INTEGER, LOCKD, &bufsz);
	if (ret == 0) {
		grace_override = 1;
		errno = 0;
		lsa.grace = strtol(defval, (char **)NULL, 10);
		if (errno != 0) {
			lsa.grace = -1;
		}
	}

	bufsz = 6;
	ret = nfs_smf_get_prop("lockd_retransmit_timeout", defval,
	    DEFAULT_INSTANCE, SCF_TYPE_INTEGER, LOCKD, &bufsz);
	if (ret == 0) {
		errno = 0;
		lsa.retransmittimeout =
		    strtol(defval, (char **)NULL, 10);
		if (errno != 0) {
			lsa.retransmittimeout = -1;
		}
	}

	bufsz = 6;
	ret = nfs_smf_get_prop("lockd_servers", defval, DEFAULT_INSTANCE,
	    SCF_TYPE_INTEGER, LOCKD, &bufsz);
	if (ret == 0) {
		errno = 0;
		maxservers = strtol(defval, (char **)NULL, 10);
		if (errno != 0) {
			maxservers = NLM_SERVERS;
		} else {
			maxservers_set = 1;
		}
	}

	bufsz = 6;
	ret = nfs_smf_get_prop("lockd_listen_backlog", defval, DEFAULT_INSTANCE,
	    SCF_TYPE_INTEGER, LOCKD, &bufsz);
	if (ret == 0) {
		errno = 0;
		listen_backlog = strtol(defval, (char **)NULL, 10);
		if (errno != 0) {
			listen_backlog = -1;
		}
	}

	/*
	 * Set max_conns_allowed to -1.
	 */
	max_conns_allowed = -1;

	while ((i = getopt(ac, av, "d:G:g:T:t:l:U:")) != EOF)
		switch (i) {
		case 'd':
			(void) sscanf(optarg, "%d", &lsa.debug);
			break;
		case 'g':
			if (grace_override)
				fprintf(stderr, gettext(
				    "grace_period set in NFS SMF,"
				    "\"-g\" ignored\n"));
			else
				(void) sscanf(optarg, "%d", &lsa.grace);
			break;
		case 'T':
			(void) sscanf(optarg, "%d", &lsa.timout);
			break;
		case 't':
			/* set retransmissions timeout value */
			(void) sscanf(optarg, "%d", &lsa.retransmittimeout);
			break;
		case 'l':
			(void) sscanf(optarg, "%d", &listen_backlog);
			break;
		case 'U':
			(void) sscanf(optarg, "%d", &daemon_uid);
			break;
		case 'G':
			(void) sscanf(optarg, "%d", &daemon_gid);
			break;
		default:
			usage();
			/* NOTREACHED */
		}

	/*
	 * If there is exactly one more argument, it is the maximum
	 * number of servers (kernel threads).
	 */
	if (optind == ac - 1) {
		errno = 0;
		maxservers = strtol(av[optind], (char **)NULL, 10);
		if (errno != 0) {
			maxservers = NLM_SERVERS;
		} else {
			maxservers_set = 1;
		}
	}
	/*
	 * If there are two or more arguments, then this is a usage error.
	 */
	else if (optind < ac - 1)
		usage();
	/*
	 * There are no additional arguments, we use a default number of
	 * servers.  We will log this.
	 */
	else if (maxservers_set == 0)
		logmaxservers = 1;

	/*
	 * Basic sanity checks on options.
	 *
	 * If any of the options are negative then replace them with
	 * the default values and say this to stderr.
	 * The exception is the listen_backlog which has a non zero minval
	 */
	if (lsa.grace < 0) {
		fprintf(stderr, gettext(
		"Invalid value for grace_period (-g), %d replaced "
		"with %d\n"), lsa.grace, GRACE);
		lsa.grace = GRACE;
	}
	if (lsa.timout < 0) {
		fprintf(stderr, gettext(
		"Invalid value for lockd_timeout (-T), %d replaced with %d\n"),
		    lsa.timout, TIMOUT);
		lsa.timout = TIMOUT;
	}
	if (lsa.retransmittimeout < 0) {
		fprintf(stderr, gettext(
		"Invalid value for lockd_retransmit_timeout (-t), %d replaced "
		"with %d\n"), lsa.retransmittimeout, RETX_TIMOUT);
		lsa.retransmittimeout = RETX_TIMOUT;
	}
	if (listen_backlog < MIN_LISTEN_BACKLOG) {
		fprintf(stderr, gettext(
		"Invalid value for lockd_listen_backlog (-l), %d replaced "
		"with %d\n"), listen_backlog, MIN_LISTEN_BACKLOG);
		listen_backlog = MIN_LISTEN_BACKLOG;
	}
	if (maxservers < 0) {
		fprintf(stderr, gettext(
		"Invalid value for lockd_servers, %d replaced with %d\n"),
		    maxservers, NLM_SERVERS);
		maxservers = NLM_SERVERS;
	}

	if (lsa.debug >= 1) {
		printf("%s: debug= %d, timout= %d, retrans= %d, grace= %d, "
		    "nthreads= %d, listen_backlog= %d\n\n",
		    MyName, lsa.debug, lsa.timout, lsa.retransmittimeout,
		    lsa.grace, maxservers, listen_backlog);
	}

	/*
	 * Set current dir to server root
	 */
	if (chdir(dir) < 0) {
		(void) fprintf(stderr, "%s:  ", MyName);
		perror(dir);
		exit(1);
	}

#ifndef DEBUG
	/*
	 * Background
	 */
	if (lsa.debug == 0) {
		pid = fork();
		if (pid < 0) {
			perror("lockd: fork");
			exit(1);
		}
		if (pid != 0)
			exit(0);

		/*
		 * Close existing file descriptors, open "/dev/null" as
		 * standard input, output, and error, and detach from
		 * controlling terminal.
		 */
		closefrom(0);
		(void) open("/dev/null", O_RDONLY);
		(void) open("/dev/null", O_WRONLY);
		(void) dup(1);
		(void) setsid();
	}
#endif

	(void) _create_daemon_lock(LOCKD, daemon_uid, daemon_gid);

	svcsetprio();

	(void) enable_extended_FILE_stdio(-1, -1);

	if (__init_daemon_priv(PU_RESETGROUPS|PU_CLEARLIMITSET,
	    daemon_uid, daemon_gid, PRIV_SYS_NFS, (char *)NULL) == -1) {
		(void) fprintf(stderr, "%s must be run with sufficient"
		    " privileges\n", av[0]);
		exit(1);
	}
	/* Basic privileges we don't need, remove from E/P. */
	(void) priv_set(PRIV_OFF, PRIV_PERMITTED, PRIV_PROC_EXEC,
	    PRIV_PROC_INFO, PRIV_FILE_LINK_ANY, PRIV_PROC_SESSION,
	    (char *)NULL);

	openlog(MyName, LOG_PID | LOG_NDELAY, LOG_DAEMON);

	/*
	 * establish our lock on the lock file and write our pid to it.
	 * exit if some other process holds the lock, or if there's any
	 * error in writing/locking the file.
	 */
	pid = _enter_daemon_lock(LOCKD);
	switch (pid) {
	case 0:
		break;
	case -1:
		syslog(LOG_ERR, "error locking for %s: %s", LOCKD,
		    strerror(errno));
		exit(2);
	default:
		/* daemon was already running */
		exit(0);
	}

	if (logmaxservers) {
		(void) syslog(LOG_INFO,
		    "Number of servers not specified. Using default of %d.",
		    maxservers);
	}

	/*
	 * Set up kernel RPC thread pool for the NLM server.
	 */
	if (nlmsvcpool(maxservers)) {
		(void) syslog(LOG_ERR,
		    "Can't set up kernel NLM service: %m. Exiting");
		exit(1);
	}

	/*
	 * Set up blocked thread to do LWP creation on behalf of the kernel.
	 */
	if (svcwait(NLM_SVCPOOL_ID)) {
		(void) syslog(LOG_ERR,
		    "Can't set up NLM pool creator: %m, Exiting");
		exit(1);
	}

	/*
	 * Build a protocol block list for registration.
	 */
	protobp = (struct protob *)malloc(sizeof (struct protob));
	protobp->serv = "NLM";
	protobp->versmin = NLM_VERS;
	protobp->versmax = NLM4_VERS;
	protobp->program = NLM_PROG;
	protobp->next = (struct protob *)NULL;

	if (do_all(protobp, nlmsvc) == -1) {
		for (providerp = defaultproviders;
		    *providerp != NULL; providerp++) {
			provider = *providerp;
			do_one(provider, NULL, protobp, nlmsvc);
		}
	}

	free(protobp);

	if (num_fds == 0) {
		(void) syslog(LOG_ERR,
		"Could not start NLM service for any protocol. Exiting.");
		exit(1);
	}

	end_listen_fds = num_fds;

	/*
	 * Remove privileges no longer needed.
	 */
	__fini_daemon_priv(PRIV_PROC_FORK, (char *)NULL);

	/*
	 * Poll for non-data control events on the transport descriptors.
	 */
	poll_for_action();

	/*
	 * If we get here, something failed in poll_for_action().
	 */
	return (1);
}

static int
nlmsvcpool(int maxservers)
{
	struct svcpool_args npa;

	npa.id = NLM_SVCPOOL_ID;
	npa.maxthreads = maxservers;
	npa.redline = 0;
	npa.qsize = 0;
	npa.timeout = 0;
	npa.stksize = 0;
	npa.max_same_xprt = 0;
	return (_nfssys(SVCPOOL_CREATE, &npa));
}

/*
 * Establish NLM service thread.
 */
static int
nlmsvc(int fd, struct netbuf addrmask, struct netconfig *nconf)
{
	struct knetconfig knconf;

	lsa.fd = fd;
	if ((strcmp(nconf->nc_protofmly, NC_INET) == 0))
		lsa.n_fmly = LM_INET;
	else if ((strcmp(nconf->nc_protofmly, NC_INET6) == 0))
		lsa.n_fmly = LM_INET6;
	else
		lsa.n_fmly = LM_LOOPBACK;
#ifdef LOOPBACK_LOCKING
	if (lsa.n_fmly == LM_LOOPBACK) {
		lsa.n_proto = LM_NOPROTO;	/* need to add this */
	} else
#endif
	lsa.n_proto = strcmp(nconf->nc_proto, NC_TCP) == 0 ?
	    LM_TCP : LM_UDP;
	(void) convert_nconf_to_knconf(nconf, &knconf);
	lsa.n_rdev = knconf.knc_rdev;

	return (_nfssys(LM_SVC, &lsa));
}

static int
convert_nconf_to_knconf(struct netconfig *nconf, struct knetconfig *knconf)
{
	struct stat sb;

	if (stat(nconf->nc_device, &sb) < 0) {
		(void) syslog(LOG_ERR, "can't find device for transport %s\n",
		    nconf->nc_device);
		return (RET_ERR);
	}

	knconf->knc_semantics = nconf->nc_semantics;
	knconf->knc_protofmly = nconf->nc_protofmly;
	knconf->knc_proto = nconf->nc_proto;
	knconf->knc_rdev = sb.st_rdev;

	return (RET_OK);
}

static void
usage(void)
{
	(void) fprintf(stderr,
gettext("usage: %s [-t timeout] [-g graceperiod] [-l listen_backlog]\n"),
	    MyName);
	exit(1);
}

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
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

/*
 * Disk quota reporting program.
 */
#include <stdio.h>
#include <sys/mnttab.h>
#include <ctype.h>
#include <pwd.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/mntent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/fs/ufs_quota.h>
#include <priv_utils.h>
#include <locale.h>
#include <rpc/rpc.h>
#include <netdb.h>
#include <rpcsvc/rquota.h>
#include <zone.h>
#include "../nfs/lib/replica.h"
#include <dlfcn.h>
#include <libzfs.h>
#include <s10_brand.h>

extern int	optind;
extern char	*optarg;

#define	QFNAME	"quotas"

#if DEV_BSIZE < 1024
#define	kb(x)	((x) / (1024 / DEV_BSIZE))
#else
#define	kb(x)	((x) * (DEV_BSIZE / 1024))
#endif

#if	!defined(TEXT_DOMAIN)   /* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"  /* Use this only if it weren't */
#endif

static void zexit(int);
static int getzfsquota(const char *, const char *, struct dqblk *);
static int getnfsquota(const char *, char *, uid_t, struct dqblk *);
static void showuid(uid_t);
static void showquotas(uid_t, const char *);
static void warn(struct mnttab *, struct dqblk *);
static int showname(const char *name);
static void heading(uid_t, const char *);
static void prquota(struct mnttab *, struct dqblk *);
static void fmttime(char *, long);
static int alldigits(const char *s);
static int hasopt(char *, char *);
static int callaurpc(const char *, int, int, int, xdrproc_t,
	struct getquota_args *, xdrproc_t, struct getquota_rslt *);

static int ufs_open_qfile(const char *);
static int ufs_getdiskquota(struct mnttab *, uid_t, struct dqblk *);
static int ufs_quotactl(int, const char *, uid_t, struct dqblk *);
static void ufs_print_priv_error(void);

static libzfs_handle_t *(*_libzfs_init)(void);
static void (*_libzfs_fini)(libzfs_handle_t *);
static zfs_handle_t *(*_zfs_open)(libzfs_handle_t *, const char *, int);
static void (*_zfs_close)(zfs_handle_t *);
static int (*_zfs_prop_get_userquota_int)(zfs_handle_t *, const char *,
    uint64_t *);

static boolean_t is_solaris10_brand(zoneid_t);

static libzfs_handle_t *g_zfs = NULL;
static int vflag;
static zoneid_t my_zoneid;
static boolean_t ufs_no_quota = B_FALSE;
static boolean_t ufs_no_privs = B_FALSE;

/*
 * Dynamically check for libzfs, in case the user hasn't installed the SUNWzfs
 * packages.  'quota' utility supports zfs as an option.
 */
static void
load_libzfs(void)
{
	void *hdl;

	if (g_zfs != NULL)
		return;

	if ((hdl = dlopen("libzfs.so", RTLD_LAZY)) != NULL) {
		_libzfs_init = (libzfs_handle_t *(*)(void))dlsym(hdl,
		    "libzfs_init");
		_libzfs_fini = (void (*)())dlsym(hdl, "libzfs_fini");
		_zfs_open = (zfs_handle_t *(*)())dlsym(hdl, "zfs_open");
		_zfs_close = (void (*)())dlsym(hdl, "zfs_close");
		_zfs_prop_get_userquota_int = (int (*)())
		    dlsym(hdl, "zfs_prop_get_userquota_int");

		if (_libzfs_init && _libzfs_fini && _zfs_open &&
		    _zfs_close && _zfs_prop_get_userquota_int)
			g_zfs = _libzfs_init();
	}
}

static boolean_t
is_solaris10_brand(zoneid_t zid)
{
	char brand[MAXNAMELEN];

	if (zone_getattr(zid, ZONE_ATTR_BRAND, brand, sizeof (brand)) < 0)
		return (B_FALSE);
	return (strcmp(brand, S10_BRANDNAME) == 0);
}

static int
ufs_open_qfile(const char *qfile)
{
	int fd;

	(void) __priv_bracket(PRIV_ON);
	fd = open64(qfile, O_RDONLY);
	(void) __priv_bracket(PRIV_OFF);

	return (fd);
}

/*
 * We only complain if quota was invoked with -v since by default
 * quota does only possibly warn about usage over quota. Since
 * we do ignore UFS file systems entirely if ufs_no_priv=B_TRUE
 * we will not warn and hence we omit this error message by default.
 * The UFS kernel module however will always issue proper over quota
 * warnings to the user for the various VOPs.
 */
static void
ufs_print_priv_error(void)
{
	if (!vflag)
		return;
	if (is_solaris10_brand(my_zoneid))
		(void) fprintf(stderr, gettext("quota: can not work "
		    "with ufs(7fs) file systems in solaris10(5) branded "
		    "zones\nfor unprivileged users.\n"));
	else
		(void) fprintf(stderr, gettext("quota: insufficient "
		    "privileges to determine UFS file system quotas.\n"
		    "quota(1m) must be set-uid root or have "
		    "file_dac_read privileges.\n"));
}

int
main(int argc, char **argv)
{
	int	opt, i;
	int	status = 0;

	/*
	 * UFS only:
	 * PRIV_FILE_DAC_READ is needed to read the QFNAME file
	 * which is owned by root with mode 0600.
	 * Clear all other privileges from the limited set, and add
	 * the required privilege to the bracketed set.
	 * We'll complain later if the program has insufficient
	 * privileges to deal with UFS file system quotas.
	 * (eg. the set-uid bit has been stripped)
	 * NB: reducing our privileges has to be the very first action
	 * here, even before getopt() and locale processing.
	 */
	if (__init_suid_priv(PU_CLEARLIMITSET, PRIV_FILE_DAC_READ,
	    (char *)NULL) == -1)
		ufs_no_privs = B_TRUE;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	while ((opt = getopt(argc, argv, "vV")) != EOF) {
		switch (opt) {
		case 'v':
			vflag++;
			break;
		case 'V':		/* Print command line */
			{
			char	*opt_text;
			int	opt_count;

			(void) fprintf(stdout, "quota -F UFS ");
			for (opt_count = 1; opt_count < argc; opt_count++) {
				opt_text = argv[opt_count];
				if (opt_text)
					(void) fprintf(stdout, " %s ",
					    opt_text);
			}
			(void) fprintf(stdout, "\n");
			}
			break;
		case '?':
			(void) fprintf(stderr, "usage: quota [-v] "
			    "[username]\n");
			zexit(32);
		}
	}

	load_libzfs();

	my_zoneid = getzoneid();

	if (ufs_quotactl(Q_ALLSYNC, NULL, (uid_t)0, NULL) < 0 &&
	    errno == EINVAL) {
		if (vflag)
			(void) fprintf(stderr, "There are no UFS quotas on "
			    "this system\n");
		ufs_no_quota = B_TRUE;
	}
	if (argc == optind) {
		showuid(getuid());
		zexit(0);
	}
	for (i = optind; i < argc; i++) {
		if (alldigits(argv[i]))
			showuid((uid_t)atoi(argv[i]));
		else
			status |= showname(argv[i]);
	}
	if (!ufs_no_privs)
		__priv_relinquish();

	return (status);
}

static void
showuid(uid_t uid)
{
	struct passwd *pwd = getpwuid(uid);

	if (uid == 0) {
		if (vflag)
			(void) printf("no disk quota for uid 0\n");
		return;
	}
	if (pwd == NULL)
		showquotas(uid, "(no account)");
	else
		showquotas(uid, pwd->pw_name);
}

static int
showname(const char *name)
{
	struct passwd *pwd = getpwnam(name);

	if (pwd == NULL) {
		(void) fprintf(stderr, "quota: %s: unknown user\n", name);
		return (32);
	}
	if (pwd->pw_uid == 0) {
		if (vflag)
			(void) printf("no disk quota for %s (uid 0)\n", name);
		return (0);
	}
	showquotas(pwd->pw_uid, name);
	return (0);
}

static void
showquotas(uid_t uid, const char *name)
{
	struct mnttab mnt;
	FILE *mtab;
	struct dqblk dqblk;
	uid_t myuid;
	struct failed_srv {
		char *serv_name;
		struct failed_srv *next;
	};
	struct failed_srv *failed_srv_list = NULL;
	int	rc;
	char	my_zonename[ZONENAME_MAX];

	myuid = getuid();
	if (uid != myuid && myuid != 0) {
		(void) fprintf(stderr, "quota: %s (uid %d): permission "
		    "denied\n", name, uid);
		zexit(32);
	}

	(void) memset(my_zonename, '\0', ZONENAME_MAX);
	(void) getzonenamebyid(my_zoneid, my_zonename, ZONENAME_MAX);

	if (vflag)
		heading(uid, name);
	if ((mtab = fopen(MNTTAB, "r")) == NULL) {
		(void) fprintf(stderr, "quota: failed to open %s: "
		    "%s\n", MNTTAB, strerror(errno));
		zexit(32);
	}

	while (getmntent(mtab, &mnt) == NULL) {
		if (strcmp(mnt.mnt_fstype, MNTTYPE_ZFS) == 0) {
			bzero(&dqblk, sizeof (dqblk));
			if (getzfsquota(name, mnt.mnt_special, &dqblk))
				continue;
		} else if (strcmp(mnt.mnt_fstype, MNTTYPE_UFS) == 0) {
			if (ufs_no_quota ||
			    (ufs_quotactl(Q_GETQUOTA,
			    mnt.mnt_mountp, uid, &dqblk) != 0 &&
			    !(vflag &&
			    ufs_getdiskquota(&mnt, uid, &dqblk))))
				continue;
		} else if (strcmp(mnt.mnt_fstype, MNTTYPE_NFS) == 0) {

			struct replica *rl;
			int count;
			char *mntopt = NULL;

			/*
			 * Skip checking quotas for file systems mounted
			 * in other zones. Zone names will be passed in
			 * following format from hasmntopt():
			 * "zone=<zone-name>,<mnt options...>"
			 */
			if ((mntopt = hasmntopt(&mnt, MNTOPT_ZONE)) != NULL &&
			    (my_zonename[0] != '\0')) {
				mntopt += strcspn(mntopt, "=") + 1;
				if (strncmp(mntopt, my_zonename,
				    strcspn(mntopt, ",")) != 0)
					continue;
			}

			if (hasopt(MNTOPT_NOQUOTA, mnt.mnt_mntopts))
				continue;

			/*
			 * Skip quota processing if mounted with public
			 * option. We are not likely to be able to pierce
			 * a fire wall to contact the quota server.
			 */
			if (hasopt(MNTOPT_PUBLIC, mnt.mnt_mntopts))
				continue;

			rl = parse_replica(mnt.mnt_special, &count);

			if (rl == NULL) {
				if (count < 0)
					(void) fprintf(stderr, "cannot find "
					    "hostname and/or pathname for %s\n",
					    mnt.mnt_mountp);
				else
					(void) fprintf(stderr, "no memory to "
					    "parse mnttab entry for %s\n",
					    mnt.mnt_mountp);
				continue;
			}

			/*
			 * We skip quota reporting on mounts with replicas
			 * for the following reasons:
			 *
			 * (1) Very little point in reporting quotas on
			 * a set of read-only replicas ... how will the
			 * user correct the problem?
			 *
			 * (2) Which replica would we report the quota
			 * for? If we pick the current replica, what
			 * happens when a fail over event occurs? The
			 * next time quota is run, the quota will look
			 * all different, or there won't even be one.
			 * This has the potential to break scripts.
			 *
			 * If we prnt quouta for all replicas, how do
			 * we present the output without breaking scripts?
			 */

			if (count > 1) {
				free_replica(rl, count);
				continue;
			}

			/*
			 * Skip file systems mounted using public fh.
			 * We are not likely to be able to pierce
			 * a fire wall to contact the quota server.
			 */
			if (strcmp(rl[0].host, MNTTYPE_NFS) == 0 &&
			    strncmp(rl[0].path, "//", 2) == 0) {
				free_replica(rl, count);
				continue;
			}

			/*
			 * Skip getting quotas from failing servers
			 */
			if (failed_srv_list != NULL) {
				struct failed_srv *tmp_list;
				int found_failed = 0;
				size_t len = strlen(rl[0].host);

				tmp_list = failed_srv_list;
				do {
					if (strncasecmp(rl[0].host,
					    tmp_list->serv_name, len) == 0) {
						found_failed = 1;
						break;
					}
				} while ((tmp_list = tmp_list->next) != NULL);
				if (found_failed) {
					free_replica(rl, count);
					continue;
				}
			}

			rc = getnfsquota(rl[0].host, rl[0].path, uid, &dqblk);
			if (rc != RPC_SUCCESS) {
				size_t len;
				struct failed_srv *tmp_srv;

				/*
				 * Failed to get quota from this server. Add
				 * this server to failed_srv_list and skip
				 * getting quotas for other mounted filesystems
				 * from this server.
				 */
				if (rc == RPC_TIMEDOUT || rc == RPC_CANTSEND) {
					len = strlen(rl[0].host);
					tmp_srv = (struct failed_srv *)malloc(
					    sizeof (struct failed_srv));
					tmp_srv->serv_name = (char *)malloc(
					    len * sizeof (char) + 1);
					(void) strncpy(tmp_srv->serv_name,
					    rl[0].host, len);
					tmp_srv->serv_name[len] = '\0';
					tmp_srv->next = failed_srv_list;
					failed_srv_list = tmp_srv;
				}

				free_replica(rl, count);
				continue;
			}

			free_replica(rl, count);
		} else {
			continue;
		}
		if (dqblk.dqb_bsoftlimit == 0 && dqblk.dqb_bhardlimit == 0 &&
		    dqblk.dqb_fsoftlimit == 0 && dqblk.dqb_fhardlimit == 0)
			continue;
		if (vflag)
			prquota(&mnt, &dqblk);
		else
			warn(&mnt, &dqblk);
	}

	/*
	 * Free list of failed servers
	 */
	while (failed_srv_list != NULL) {
		struct failed_srv *tmp_srv = failed_srv_list;

		failed_srv_list = failed_srv_list->next;
		free(tmp_srv->serv_name);
		free(tmp_srv);
	}

	(void) fclose(mtab);
}

static void
warn(struct mnttab *mntp, struct dqblk *dqp)
{
	struct timeval tv;

	(void) time(&(tv.tv_sec));
	tv.tv_usec = 0;
	if (dqp->dqb_bhardlimit &&
	    dqp->dqb_curblocks >= dqp->dqb_bhardlimit) {
		(void) printf("Block limit reached on %s\n", mntp->mnt_mountp);
	} else if (dqp->dqb_bsoftlimit &&
	    dqp->dqb_curblocks >= dqp->dqb_bsoftlimit) {
		if (dqp->dqb_btimelimit == 0) {
			(void) printf("Over disk quota on %s, remove %uK\n",
			    mntp->mnt_mountp,
			    kb(dqp->dqb_curblocks - dqp->dqb_bsoftlimit + 1));
		} else if (dqp->dqb_btimelimit > tv.tv_sec) {
			char btimeleft[80];

			fmttime(btimeleft, dqp->dqb_btimelimit - tv.tv_sec);
			(void) printf("Over disk quota on %s, remove %uK "
			    "within  %s\n", mntp->mnt_mountp,
			    kb(dqp->dqb_curblocks - dqp->dqb_bsoftlimit + 1),
			    btimeleft);
		} else {
			(void) printf(
		"Over disk quota on %s, time limit has expired, remove %uK\n",
			    mntp->mnt_mountp,
			    kb(dqp->dqb_curblocks - dqp->dqb_bsoftlimit + 1));
		}
	}
	if (dqp->dqb_fhardlimit &&
	    dqp->dqb_curfiles >= dqp->dqb_fhardlimit) {
		(void) printf("File count limit reached on %s\n",
		    mntp->mnt_mountp);
	} else if (dqp->dqb_fsoftlimit &&
	    dqp->dqb_curfiles >= dqp->dqb_fsoftlimit) {
		if (dqp->dqb_ftimelimit == 0) {
			(void) printf("Over file quota on %s, remove %u file"
			    "%s\n", mntp->mnt_mountp,
			    dqp->dqb_curfiles - dqp->dqb_fsoftlimit + 1,
			    ((dqp->dqb_curfiles - dqp->dqb_fsoftlimit + 1) > 1 ?
			    "s" : ""));
		} else if (dqp->dqb_ftimelimit > tv.tv_sec) {
			char ftimeleft[80];

			fmttime(ftimeleft, dqp->dqb_ftimelimit - tv.tv_sec);
			(void) printf(
"Over file quota on %s, remove %u file%s within %s\n",
			    mntp->mnt_mountp,
			    dqp->dqb_curfiles - dqp->dqb_fsoftlimit + 1,
			    ((dqp->dqb_curfiles - dqp->dqb_fsoftlimit + 1) > 1 ?
			    "s" : ""), ftimeleft);
		} else {
			(void) printf(
"Over file quota on %s, time limit has expired, remove %u file%s\n",
			    mntp->mnt_mountp,
			    dqp->dqb_curfiles - dqp->dqb_fsoftlimit + 1,
			    ((dqp->dqb_curfiles - dqp->dqb_fsoftlimit + 1) > 1 ?
			    "s" : ""));
		}
	}
}

static void
heading(uid_t uid, const char *name)
{
	(void) printf("Disk quotas for %s (uid %ld):\n", name, (long)uid);
	(void) printf("%-12s %7s%7s%7s%12s%7s%7s%7s%12s\n",
	    "Filesystem",
	    "usage",
	    "quota",
	    "limit",
	    "timeleft",
	    "files",
	    "quota",
	    "limit",
	    "timeleft");
}

static void
prquota(struct mnttab *mntp, struct dqblk *dqp)
{
	struct timeval tv;
	char ftimeleft[80], btimeleft[80];
	char *cp;

	(void) time(&(tv.tv_sec));
	tv.tv_usec = 0;
	if (dqp->dqb_bsoftlimit && dqp->dqb_curblocks >= dqp->dqb_bsoftlimit) {
		if (dqp->dqb_btimelimit == 0) {
			(void) strlcpy(btimeleft, "NOT STARTED",
			    sizeof (btimeleft));
		} else if (dqp->dqb_btimelimit > tv.tv_sec) {
			fmttime(btimeleft, dqp->dqb_btimelimit - tv.tv_sec);
		} else {
			(void) strlcpy(btimeleft, "EXPIRED",
			    sizeof (btimeleft));
		}
	} else {
		btimeleft[0] = '\0';
	}
	if (dqp->dqb_fsoftlimit && dqp->dqb_curfiles >= dqp->dqb_fsoftlimit) {
		if (dqp->dqb_ftimelimit == 0) {
			(void) strlcpy(ftimeleft, "NOT STARTED",
			    sizeof (ftimeleft));
		} else if (dqp->dqb_ftimelimit > tv.tv_sec) {
			fmttime(ftimeleft, dqp->dqb_ftimelimit - tv.tv_sec);
		} else {
			(void) strlcpy(ftimeleft, "EXPIRED",
			    sizeof (ftimeleft));
		}
	} else {
		ftimeleft[0] = '\0';
	}
	if (strlen(mntp->mnt_mountp) > 12) {
		(void) printf("%s\n", mntp->mnt_mountp);
		cp = "";
	} else {
		cp = mntp->mnt_mountp;
	}

	if (dqp->dqb_curfiles == 0 &&
	    dqp->dqb_fsoftlimit == 0 && dqp->dqb_fhardlimit == 0) {
		(void) printf("%-12.12s %7d %6d %6d %11s %6s %6s %6s %11s\n",
		    cp,
		    kb(dqp->dqb_curblocks),
		    kb(dqp->dqb_bsoftlimit),
		    kb(dqp->dqb_bhardlimit),
		    "-",
		    "-",
		    "-",
		    "-",
		    "-");
	} else {
		(void) printf("%-12.12s %7d %6d %6d %11s %6d %6d %6d %11s\n",
		    cp,
		    kb(dqp->dqb_curblocks),
		    kb(dqp->dqb_bsoftlimit),
		    kb(dqp->dqb_bhardlimit),
		    btimeleft,
		    dqp->dqb_curfiles,
		    dqp->dqb_fsoftlimit,
		    dqp->dqb_fhardlimit,
		    ftimeleft);
	}
}

static void
fmttime(char *buf, long time)
{
	int i;
	static struct {
		int c_secs;		/* conversion units in secs */
		char *c_str;		/* unit string */
	} cunits [] = {
		{60*60*24*28, "months"},
		{60*60*24*7, "weeks"},
		{60*60*24, "days"},
		{60*60, "hours"},
		{60, "mins"},
		{1, "secs"}
	};

	if (time <= 0) {
		(void) strlcpy(buf, "EXPIRED", sizeof (*buf));
		return;
	}
	for (i = 0; i < sizeof (cunits)/sizeof (cunits[0]); i++) {
		if (time >= cunits[i].c_secs)
			break;
	}
	(void) snprintf(buf, sizeof (*buf), "%.1f %s",
	    (double)time/cunits[i].c_secs, cunits[i].c_str);
}

static int
alldigits(const char *s)
{
	int c;

	if (*s == '\0')
		return (0);
	while ((c = *s++) != '\0') {
		if (!isdigit(c))
			return (0);
	}
	return (1);
}

static int
ufs_getdiskquota(struct mnttab *mntp, uid_t uid, struct dqblk *dqp)
{
	int fd;
	dev_t fsdev;
	struct stat64 statb;
	char qfilename[MAXPATHLEN];

	if (stat64(mntp->mnt_special, &statb) < 0 ||
	    (statb.st_mode & S_IFMT) != S_IFBLK)
		return (0);

	fsdev = statb.st_rdev;
	(void) snprintf(qfilename, sizeof (qfilename), "%s/%s",
	    mntp->mnt_mountp, QFNAME);

	if (stat64(qfilename, &statb) < 0 || statb.st_dev != fsdev)
		return (0);
	if ((fd = ufs_open_qfile(qfilename)) < 0)
		return (0);
	(void) llseek(fd, (offset_t)dqoff(uid), L_SET);
	switch (read(fd, dqp, sizeof (struct dqblk))) {
	case 0:				/* EOF */
		/*
		 * Convert implicit 0 quota (EOF)
		 * into an explicit one (zero'ed dqblk).
		 */
		(void) memset((caddr_t)dqp, 0, sizeof (struct dqblk));
		break;

	case sizeof (struct dqblk):	/* OK */
		break;

	default:			/* ERROR */
		(void) close(fd);
		return (0);
	}
	(void) close(fd);
	return (1);
}

static int
ufs_quotactl(int cmd, const char *mountp, uid_t uid, struct dqblk *dqp)
{
	int		fd = -1;
	int		status;
	struct quotctl	quota;
	char		qfile[MAXPATHLEN];
	FILE		*fstab;
	struct mnttab	mnt;

	if ((mountp == NULL) && (cmd == Q_ALLSYNC)) {
	/*
	 * Find the mount point of any mounted file system. This is
	 * because the ioctl that implements the quotactl call has
	 * to go to a real file, and not to the block device.
	 */
		if ((fstab = fopen(MNTTAB, "r")) == NULL) {
			(void) fprintf(stderr, "quota: failed to open "
			    "%s: %s\n", MNTTAB, strerror(errno));
			zexit(32);
		}
		while ((status = getmntent(fstab, &mnt)) == NULL) {
			if (strcmp(mnt.mnt_fstype, MNTTYPE_UFS) != 0 ||
			    hasopt(MNTOPT_RO, mnt.mnt_mntopts))
				continue;
			/*
			 * First time we found a UFS file system, check
			 * if we have appropriate privileges to deal with
			 * it. If not, complain here for 1 time and
			 * prevent any future attempts to deal with UFS
			 * file systems.
			 */
			if (ufs_no_privs) {
				ufs_no_quota = B_TRUE;
				ufs_print_priv_error();
				return (-1);
			}
			if ((strlcpy(qfile, mnt.mnt_mountp,
			    sizeof (qfile)) >= sizeof (qfile)) ||
			    (strlcat(qfile, "/" QFNAME, sizeof (qfile)) >=
			    sizeof (qfile))) {
				continue;
			}
			if ((fd = ufs_open_qfile(qfile)) < 0)
				break;
		}
		(void) fclose(fstab);
		if (fd == -1) {
			errno = ENOENT;
			return (-1);
		}
	} else {
		if (mountp == NULL || mountp[0] == '\0') {
			errno = ENOENT;
			return (-1);
		}
		if ((strlcpy(qfile, mountp, sizeof (qfile)) >= sizeof
		    (qfile)) ||
		    (strlcat(qfile, "/" QFNAME, sizeof (qfile)) >= sizeof
		    (qfile))) {
			errno = ENOENT;
			return (-1);
		}
		if ((fd = ufs_open_qfile(qfile)) < 0)
			return (-1);
	}	/* else */
	quota.op = cmd;
	quota.uid = uid;
	quota.addr = (caddr_t)dqp;
	status = ioctl(fd, Q_QUOTACTL, &quota);
	if (fd != 0)
		(void) close(fd);
	return (status);
}


/*
 * Return 1 if opt appears in optlist
 */
static int
hasopt(char *opt, char *optlist)
{
	char *value;
	char *opts[2];

	opts[0] = opt;
	opts[1] = NULL;

	if (optlist == NULL)
		return (0);
	while (*optlist != '\0') {
		if (getsubopt(&optlist, opts, &value) == 0)
			return (1);
	}
	return (0);
}

/*
 * If there are no quotas available, then getnfsquota() returns
 * RPC_SYSTEMERROR to caller.
 */
static int
getnfsquota(const char *hostp, char *path, uid_t uid, struct dqblk *dqp)
{
	struct getquota_args gq_args;
	struct getquota_rslt gq_rslt;
	struct rquota *rquota;
	int	rpc_err;

	gq_args.gqa_pathp = path;
	gq_args.gqa_uid = uid;
	rpc_err = callaurpc(hostp, RQUOTAPROG, RQUOTAVERS,
	    (vflag? RQUOTAPROC_GETQUOTA: RQUOTAPROC_GETACTIVEQUOTA),
	    xdr_getquota_args, &gq_args, xdr_getquota_rslt, &gq_rslt);
	if (rpc_err != RPC_SUCCESS)
		return (rpc_err);

	switch (gq_rslt.status) {
	case Q_OK:
		{
		struct timeval tv;
		u_longlong_t limit;

		rquota = &gq_rslt.getquota_rslt_u.gqr_rquota;

		if (!vflag && rquota->rq_active == FALSE)
			return (RPC_SYSTEMERROR);
		(void) gettimeofday(&tv, NULL);
		limit = (u_longlong_t)(rquota->rq_bhardlimit) *
		    rquota->rq_bsize / DEV_BSIZE;
		dqp->dqb_bhardlimit = limit;
		limit = (u_longlong_t)(rquota->rq_bsoftlimit) *
		    rquota->rq_bsize / DEV_BSIZE;
		dqp->dqb_bsoftlimit = limit;
		limit = (u_longlong_t)(rquota->rq_curblocks) *
		    rquota->rq_bsize / DEV_BSIZE;
		dqp->dqb_curblocks = limit;
		dqp->dqb_fhardlimit = rquota->rq_fhardlimit;
		dqp->dqb_fsoftlimit = rquota->rq_fsoftlimit;
		dqp->dqb_curfiles = rquota->rq_curfiles;
		dqp->dqb_btimelimit =
		    tv.tv_sec + rquota->rq_btimeleft;
		dqp->dqb_ftimelimit =
		    tv.tv_sec + rquota->rq_ftimeleft;
		return (RPC_SUCCESS);
		}

	case Q_NOQUOTA:
		return (RPC_SYSTEMERROR);

	case Q_EPERM:
		(void) fprintf(stderr, "quota permission error, host: "
		    "%s\n", hostp);
		return (RPC_AUTHERROR);

	default:
		(void) fprintf(stderr, "bad rpc result, host: %s\n",  hostp);
		return (RPC_CANTDECODEARGS);
	}

	/* NOTREACHED */
}

static int
callaurpc(const char *host, int prognum, int versnum, int procnum,
		xdrproc_t inproc, struct getquota_args *in,
		xdrproc_t outproc, struct getquota_rslt *out)
{
	static enum clnt_stat clnt_stat;
	struct timeval tottimeout = {20, 0};

	static CLIENT *cl = NULL;
	static int oldprognum, oldversnum;
	static char oldhost[MAXHOSTNAMELEN+1];

	/*
	 * Cache the client handle in case there are lots
	 * of entries in the /etc/mnttab for the same
	 * server. If the server returns an error, don't
	 * make further calls.
	 */
	if (cl == NULL || oldprognum != prognum || oldversnum != versnum ||
	    strcmp(oldhost, host) != 0) {
		if (cl) {
			clnt_destroy(cl);
			cl = NULL;
		}
		cl = clnt_create_timed(host, prognum, versnum, "udp",
		    &tottimeout);
		if (cl == NULL)
			return ((int)RPC_TIMEDOUT);
		if ((cl->cl_auth = authunix_create_default()) == NULL) {
			clnt_destroy(cl);
			return (RPC_CANTSEND);
		}
		oldprognum = prognum;
		oldversnum = versnum;
		(void) strlcpy(oldhost, host, sizeof (oldhost));
		clnt_stat = RPC_SUCCESS;
	}

	if (clnt_stat != RPC_SUCCESS)
		return ((int)clnt_stat);	/* don't bother retrying */

	clnt_stat = clnt_call(cl, procnum, inproc, (caddr_t)in,
	    outproc, (caddr_t)out, tottimeout);

	return ((int)clnt_stat);
}

static int
getzfsquota(const char *user, const char *dataset, struct dqblk *zq)
{
	zfs_handle_t *zhp = NULL;
	char propname[ZFS_MAXPROPLEN];
	uint64_t userquota, userused;

	if (g_zfs == NULL)
		return (1);

	if ((zhp = _zfs_open(g_zfs, dataset, ZFS_TYPE_DATASET)) == NULL)
		return (1);

	(void) snprintf(propname, sizeof (propname), "userquota@%s", user);
	if (_zfs_prop_get_userquota_int(zhp, propname, &userquota) != 0) {
		_zfs_close(zhp);
		return (1);
	}

	(void) snprintf(propname, sizeof (propname), "userused@%s", user);
	if (_zfs_prop_get_userquota_int(zhp, propname, &userused) != 0) {
		_zfs_close(zhp);
		return (1);
	}

	zq->dqb_bhardlimit = userquota / DEV_BSIZE;
	zq->dqb_bsoftlimit = userquota / DEV_BSIZE;
	zq->dqb_curblocks = userused / DEV_BSIZE;
	_zfs_close(zhp);
	return (0);
}

static void
zexit(int n)
{
	if (g_zfs != NULL)
		_libzfs_fini(g_zfs);
	if (!ufs_no_privs)
		__priv_relinquish();
	exit(n);
}

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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/fs/sdev_impl.h>

#define	READFLAG_RO	1
#define	READFLAG_RW	2


extern int	optind;
extern char	*optarg;

static char	typename[64], *myname;
static char	fstype[] = MNTTYPE_DEV;

static int	readflag;
static int	overlay;
static int	remount;

static char	*special;
static char	*mountpt;
static char	*attrdir;

static char optbuf[MAX_MNTOPT_STR];

static char	*myopts[] = {
#define	SUBOPT_READONLY		0
	MNTOPT_RO,
#define	SUBOPT_READWRITE	1
	MNTOPT_RW,
#define	SUBOPT_REMOUNT		2
	MNTOPT_REMOUNT,
#define	SUBOPT_ATTRDIR		3
	MNTOPT_SDEV_ATTRDIR,
	NULL
};


static void
usage(void)
{
	(void) fprintf(stderr, gettext(
	    "%s usage:\n%s [-F %s] [-r] [-o specific_options]"
	    " {special | mount_point}\n%s [-F %s] [-r] [-o specific_options]"
	    " special mount_point\n"), fstype, myname, fstype, myname, fstype);
	exit(1);
}


static int
do_mount(void)
{
	int	flags = MS_OPTIONSTR;

	if (readflag == READFLAG_RO)
		flags |= MS_RDONLY;
	if (overlay)
		flags |= MS_OVERLAY;
	if (remount)
		flags |= MS_REMOUNT;

	if (mount(special, mountpt, flags, fstype, NULL, 0,
	    optbuf, sizeof (optbuf))) {
		switch (errno) {
		case EPERM:
			(void) fprintf(stderr, gettext("%s: not super user\n"),
			    typename);
			break;
		case ENXIO:
			(void) fprintf(stderr, gettext("%s: %s no such "
			    "device\n"), typename, special);
			break;
		case ENOTDIR:
			(void) fprintf(stderr, gettext("%s: %s "
			    "not a directory\n"
			    "\tor a component of %s is not a directory\n"),
			    typename, mountpt, special);
			break;
		case ENOENT:
			(void) fprintf(stderr, gettext("%s: %s or %s, no such "
			    "file or directory\n"),
			    typename, special, mountpt);
			break;
		case EINVAL:
			(void) fprintf(stderr, gettext("%s: %s is not this "
			    "filesystem type.\n"), typename, special);
			break;
		case EBUSY:
			(void) fprintf(stderr, gettext("%s: %s "
			    "is already mounted, %s is busy,\n"
			    "\tor allowable number of mount points exceeded\n"),
			    typename, special, mountpt);
			break;
		case ENOTBLK:
			(void) fprintf(stderr, gettext("%s: %s not a block "
			    "device\n"), typename, special);
			break;
		case EROFS:
			(void) fprintf(stderr, gettext("%s: %s read-only "
			    "filesystem\n"), typename, special);
			break;
		case ENOSPC:
			(void) fprintf(stderr, gettext("%s: the state of %s "
			    "is not okay\n"
			    "\tand read/write mount was attempted\n"),
			    typename, special);
			break;
		default:
			(void) fprintf(stderr, gettext("%s: cannot mount %s: "
			    "%s\n"), typename, special, strerror(errno));
			break;
		}
		return (-1);
	}
	return (0);
}

/*
 * Make sure the path truly is a directory
 */
static int
verify_is_dir(const char *path)
{
	struct stat st;

	if (stat(path, &st) < 0) {
		(void) fprintf(stderr, gettext("%s: can't stat %s: %s\n"),
		    typename, path, strerror(errno));
		return (-1);
	}

	if (!S_ISDIR(st.st_mode)) {
		(void) fprintf(stderr, gettext("%s: %s is not a "
		    "directory\n"), typename, path);
		return (-1);
	}
	return (0);
}


/*
 * Wraper around realpath()
 */
static char *
do_realpath(const char *path, char *resolved_path)
{
	char	*ret;

	ret = realpath(path, resolved_path);
	if (ret == NULL) {
		(void) fprintf(stderr, gettext("%s: realpath %s failed: %s\n"),
		    typename, path, strerror(errno));
	}
	return (ret);
}

static int
parse_subopts(char *subopts)
{
	char	*value;
	char	path[PATH_MAX + 1];

	while (*subopts != '\0') {
		switch (getsubopt(&subopts, myopts, &value)) {
		case SUBOPT_READONLY:
			if (readflag == READFLAG_RW) {
				(void) fprintf(stderr, gettext("%s: both "
				    "read-only and read-write options "
				    "specified\n"), typename);
				return (-1);
			}
			readflag = READFLAG_RO;
			break;

		case SUBOPT_READWRITE:
			if (readflag == READFLAG_RO) {
				(void) fprintf(stderr, gettext("%s: both "
				    "read-only and read-write options "
				    "specified\n"), typename);
				return (-1);
			}
			readflag = READFLAG_RW;
			break;

		case SUBOPT_ATTRDIR:
			if (value == NULL || *value == '\0') {
				(void) fprintf(stderr, gettext("%s: no "
				    "attribute directory specified\n"),
				    typename);
				return (-1);
			}
			if (do_realpath(value, path) == NULL)
				return (-1);
			if (verify_is_dir(path) != 0)
				return (-1);
			attrdir = path;
			break;

		case SUBOPT_REMOUNT:
			remount = 1;
			break;

		default:
			(void) fprintf(stderr, gettext("%s: illegal -o "
			    "suboption: %s\n"), typename, value);
			return (-1);
		}
	}
	return (0);
}


int
main(int argc, char **argv)
{
	char		mntpath[PATH_MAX + 1];
	int		cc;

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	if (myname = strrchr(argv[0], '/'))
		myname++;
	else
		myname = argv[0];
	(void) snprintf(typename, sizeof (typename), "%s %s", fstype, myname);
	argv[0] = typename;

	while ((cc = getopt(argc, argv, "?o:rmO")) != -1) {
		switch (cc) {
		case 'r':
			if (readflag == READFLAG_RW) {
				(void) fprintf(stderr, gettext("%s: both "
				    "read-only and read-write options "
				    "specified\n"), typename);
				return (1);
			}
			readflag = READFLAG_RO;
			break;

		case 'O':
			overlay = 1;
			break;

		case 'o':
			if (strlcpy(optbuf, optarg, sizeof (optbuf)) >=
			    sizeof (optbuf)) {
				(void) fprintf(stderr, gettext("%s: option "
				    "string is too long\n"), typename);
				return (1);
			}
			if (parse_subopts(optarg))
				return (1);
			break;

		default:
			usage();
			break;
		}
	}

	/*
	 * There must be at least 2 more arguments, the
	 * special file and the directory.
	 */
	if ((argc - optind) != 2)
		usage();

	special = argv[optind++];

	if (do_realpath(argv[optind++], mntpath) == NULL)
		return (1);
	mountpt = mntpath;

	if (verify_is_dir(mountpt) != 0)
		return (1);

	/* Remount of /dev requires an attribute directory */
	if (remount && attrdir == NULL) {
		(void) fprintf(stderr, gettext("%s: missing attribute "
		    "directory\n"), typename);
		return (1);
	}

	(void) signal(SIGHUP,  SIG_IGN);
	(void) signal(SIGQUIT, SIG_IGN);
	(void) signal(SIGINT,  SIG_IGN);

	/* Perform the mount  */
	if (do_mount())
		return (1);

	return (0);
}

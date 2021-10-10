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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <libuutil.h>
#include <libnvpair.h>
#include <locale.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <zone.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/mkdev.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/fs/zfs.h>
#include <sys/types.h>
#include <time.h>
#include <libshare.h>
#include <wait.h>

#include <libzfs.h>
#include <libuutil.h>

#include "zfs_iter.h"
#include "zfs_util.h"
#include "zfs_comutil.h"
#include "zfs_prop.h"

libzfs_handle_t *g_zfs;

static FILE *mnttab_file;
static char history_str[HIS_MAX_RECORD_LEN];
const char *pypath = "/usr/lib/zfs/pyzfs.py";
static char *progname;

static int zfs_do_clone(int argc, char **argv);
static int zfs_do_create(int argc, char **argv);
static int zfs_do_destroy(int argc, char **argv);
static int zfs_do_get(int argc, char **argv);
static int zfs_do_inherit(int argc, char **argv);
static int zfs_do_list(int argc, char **argv);
static int zfs_do_mount(int argc, char **argv);
static int zfs_do_rename(int argc, char **argv);
static int zfs_do_rollback(int argc, char **argv);
static int zfs_do_set(int argc, char **argv);
static int zfs_do_upgrade(int argc, char **argv);
static int zfs_do_snapshot(int argc, char **argv);
static int zfs_do_unmount(int argc, char **argv);
static int zfs_do_share(int argc, char **argv);
static int zfs_do_unshare(int argc, char **argv);
static int zfs_do_send(int argc, char **argv);
static int zfs_do_receive(int argc, char **argv);
static int zfs_do_promote(int argc, char **argv);
static int zfs_do_userspace(int argc, char **argv);
static int zfs_do_python(int argc, char **argv);
static int zfs_do_hold(int argc, char **argv);
static int zfs_do_release(int argc, char **argv);
static int zfs_do_diff(int argc, char **argv);
static int zfs_do_key(int argc, char **argv);
static int zfs_do_help(int argc, char **argv);

/*
 * Enable a reasonable set of defaults for libumem debugging on DEBUG builds.
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

typedef enum {
	HELP_CLONE,
	HELP_CREATE,
	HELP_DESTROY,
	HELP_GET,
	HELP_INHERIT,
	HELP_UPGRADE,
	HELP_LIST,
	HELP_MOUNT,
	HELP_PROMOTE,
	HELP_RECEIVE,
	HELP_RENAME,
	HELP_ROLLBACK,
	HELP_SEND,
	HELP_SET,
	HELP_SHARE,
	HELP_SNAPSHOT,
	HELP_UNMOUNT,
	HELP_UNSHARE,
	HELP_ALLOW,
	HELP_UNALLOW,
	HELP_USERSPACE,
	HELP_GROUPSPACE,
	HELP_HOLD,
	HELP_HOLDS,
	HELP_RELEASE,
	HELP_DIFF,
	HELP_KEY,
	HELP_HELP
} zfs_help_t;

/*
 * Master command table.  Each ZFS command has a name, associated function, and
 * usage message.  The usage messages need to be internationalized, so we have
 * to have a function to return the usage message based on a command index.
 */
static zfs_command_t command_table[] = {
	{ "create",	zfs_do_create,		HELP_CREATE		},
	{ "destroy",	zfs_do_destroy,		HELP_DESTROY		},
	{ "snapshot",	zfs_do_snapshot,	HELP_SNAPSHOT		},
	{ "rollback",	zfs_do_rollback,	HELP_ROLLBACK		},
	{ "clone",	zfs_do_clone,		HELP_CLONE		},
	{ "promote",	zfs_do_promote,		HELP_PROMOTE		},
	{ "rename",	zfs_do_rename,		HELP_RENAME		},
	{ "list",	zfs_do_list,		HELP_LIST		},
	{ "set",	zfs_do_set,		HELP_SET		},
	{ "get",	zfs_do_get,		HELP_GET		},
	{ "inherit",	zfs_do_inherit,		HELP_INHERIT		},
	{ "upgrade",	zfs_do_upgrade,		HELP_UPGRADE		},
	{ "userspace",	zfs_do_userspace,	HELP_USERSPACE		},
	{ "groupspace",	zfs_do_userspace,	HELP_GROUPSPACE		},
	{ "mount",	zfs_do_mount,		HELP_MOUNT		},
	{ "unmount",	zfs_do_unmount,		HELP_UNMOUNT		},
	{ "share",	zfs_do_share,		HELP_SHARE		},
	{ "unshare",	zfs_do_unshare,		HELP_UNSHARE		},
	{ "send",	zfs_do_send,		HELP_SEND		},
	{ "receive",	zfs_do_receive,		HELP_RECEIVE		},
	{ "allow",	zfs_do_python,		HELP_ALLOW		},
	{ "unallow",	zfs_do_python,		HELP_UNALLOW		},
	{ "hold",	zfs_do_hold,		HELP_HOLD		},
	{ "holds",	zfs_do_python,		HELP_HOLDS		},
	{ "release",	zfs_do_release,		HELP_RELEASE		},
	{ "diff",	zfs_do_diff,		HELP_DIFF		},
	{ "key",	zfs_do_key,		HELP_KEY		},
	{ "help",	zfs_do_help,		HELP_HELP		},
	{ NULL },
};

zfs_command_t *current_command;

static const char *
get_usage(zfs_help_t idx)
{
	switch (idx) {
	case HELP_CLONE:
		return (gettext("\tclone [-p] [-K] [-o property=value] ... "
		    "<snapshot> <filesystem|volume>\n"));
	case HELP_CREATE:
		return (gettext("\tcreate [-p] [-o property=value] ... "
		    "<filesystem>\n"
		    "\tcreate [-ps] [-b blocksize] [-o property=value] ... "
		    "-V <size> <volume>\n"));
	case HELP_DESTROY:
		return (gettext("\tdestroy [-rRf] <filesystem|volume>\n"
		    "\tdestroy [-rRd] <snapshot>\n"));
	case HELP_GET:
		return (gettext("\tget [-rHp] [-d max] "
		    "[-o \"all\" | field[,...]] [-s source[,...]]\n"
		    "\t    <\"all\" | property[,...]> "
		    "[filesystem|volume|snapshot] ...\n"));
	case HELP_INHERIT:
		return (gettext("\tinherit [-rS] <property> "
		    "<filesystem|volume|snapshot> ...\n"));
	case HELP_UPGRADE:
		return (gettext("\tupgrade [-v]\n"
		    "\tupgrade [-r] [-V version] <-a | filesystem ...>\n"));
	case HELP_LIST:
		return (gettext("\tlist [-rH][-d max] "
		    "[-o property[,...]] [-t type[,...]] [-s property] ...\n"
		    "\t    [-S property] ... "
		    "[filesystem|volume|snapshot] ...\n"));
	case HELP_MOUNT:
		return (gettext("\tmount\n"
		    "\tmount [-vOS] [-o opts] <-a | filesystem>\n"));
	case HELP_PROMOTE:
		return (gettext("\tpromote <clone-filesystem>\n"));
	case HELP_RECEIVE:
		return (gettext("\treceive [-vnFu] [[-o property=value] | "
		    "[-x property]] ... <filesystem|volume|snapshot>\n"
		    "\treceive [-vnFu] [[-o property=value] | "
		    "[-x property]] ... [-d | -e] <filesystem>\n"));
	case HELP_RENAME:
		return (gettext("\trename <filesystem|volume|snapshot> "
		    "<filesystem|volume|snapshot>\n"
		    "\trename -p <filesystem|volume> <filesystem|volume>\n"
		    "\trename -r <snapshot> <snapshot>\n"));
	case HELP_ROLLBACK:
		return (gettext("\trollback [-rRf] <snapshot>\n"));
	case HELP_SEND:
		return (gettext("\tsend [-RDpb] [-[iI] snapshot] <snapshot>\n"
		    "\tsend -r [-Dpbc] [-i snapshot] <snapshot>\n"));
	case HELP_SET:
		return (gettext("\tset [-r | -c] <property=value> "
		    "<filesystem|volume|snapshot> ...\n"));
	case HELP_SHARE:
		return (gettext("\tshare <-a | filesystem>\n"));
	case HELP_SNAPSHOT:
		return (gettext("\tsnapshot [-r] [-o property=value] ... "
		    "<filesystem@snapname|volume@snapname>\n"));
	case HELP_UNMOUNT:
		return (gettext("\tunmount [-f] "
		    "<-a | filesystem|mountpoint>\n"));
	case HELP_UNSHARE:
		return (gettext("\tunshare "
		    "<-a | filesystem|mountpoint>\n"));
	case HELP_ALLOW:
		return (gettext("\tallow <filesystem|volume>\n"
		    "\tallow [-ldug] "
		    "<\"everyone\"|user|group>[,...] <perm|@setname>[,...]\n"
		    "\t    <filesystem|volume>\n"
		    "\tallow [-ld] -e <perm|@setname>[,...] "
		    "<filesystem|volume>\n"
		    "\tallow -c <perm|@setname>[,...] <filesystem|volume>\n"
		    "\tallow -s @setname <perm|@setname>[,...] "
		    "<filesystem|volume>\n"));
	case HELP_UNALLOW:
		return (gettext("\tunallow [-rldug] "
		    "<\"everyone\"|user|group>[,...]\n"
		    "\t    [<perm|@setname>[,...]] <filesystem|volume>\n"
		    "\tunallow [-rld] -e [<perm|@setname>[,...]] "
		    "<filesystem|volume>\n"
		    "\tunallow [-r] -c [<perm|@setname>[,...]] "
		    "<filesystem|volume>\n"
		    "\tunallow [-r] -s @setname [<perm|@setname>[,...]] "
		    "<filesystem|volume>\n"));
	case HELP_USERSPACE:
		return (gettext("\tuserspace [-hniHp] [-o field[,...]] "
		    "[-sS field] ... [-t type[,...]]\n"
		    "\t    <filesystem|snapshot>\n"));
	case HELP_GROUPSPACE:
		return (gettext("\tgroupspace [-hniHpU] [-o field[,...]] "
		    "[-sS field] ... [-t type[,...]]\n"
		    "\t    <filesystem|snapshot>\n"));
	case HELP_HOLD:
		return (gettext("\thold [-r] <tag> <snapshot> ...\n"));
	case HELP_HOLDS:
		return (gettext("\tholds [-r] <snapshot> ...\n"));
	case HELP_RELEASE:
		return (gettext("\trelease [-r] <tag> <snapshot> ...\n"));
	case HELP_DIFF:
		return (gettext("\tdiff [-FHte] [-o field] <snapshot> "
		    "[snapshot|filesystem]\n"
		    "\tdiff -E [-FHt] [-o field] snapshot|filesystem\n"));
	case HELP_KEY:
		return (gettext(
		    "\tkey -l <-a | [-r] filesystem|volume>\n"
		    "\tkey -u [-f] <-a | [-r] filesystem|volume>\n"
		    "\tkey -c [ -o <keysource=value>]"
		    " <-a | [-r] filesystem|volume>\n"
		    "\tkey -K <-a | [-r] filesystem|volume>\n"));
	case HELP_HELP:
		return (gettext(
		    "\thelp\n"
		    "\thelp <command>\n"
		    "\thelp permission\n"
		    "\thelp property\n"
		    "\thelp [property] <property>\n"));
	}

	abort();
	/* NOTREACHED */
}

void
nomem(void)
{
	(void) fprintf(stderr, gettext("internal error: out of memory\n"));
	exit(1);
}

/*
 * Utility function to guarantee malloc() success.
 */

void *
safe_malloc(size_t size)
{
	void *data;

	if ((data = calloc(1, size)) == NULL)
		nomem();

	return (data);
}

static char *
safe_strdup(const char *str)
{
	char *dupstr = strdup(str);

	if (dupstr == NULL)
		nomem();

	return (dupstr);
}

static int
usage_prop(char *name, FILE *fp)
{
	zfs_prop_t prop = zfs_name_to_prop(name);

	if (prop != ZPROP_INVAL) {
		zprop_describe_prop(fp, prop, ZFS_TYPE_FILESYSTEM);
	} else {
		(void) fprintf(stderr, gettext("invalid property '%s'\n"),
		    name);
		zfs_command_help(progname, "property");
		return (-1);
	}

	return (0);
}

/*
 * Display usage message.  If we're inside a command, display only the usage for
 * that command.  Otherwise, iterate over the entire command table and display
 * a complete usage message.
 */
static void
usage(void)
{
	zfs_command_help(progname,
	    (current_command == NULL) ? "" : current_command->name);

	/*
	 * See comments at end of main().
	 */
	if (getenv("ZFS_ABORT") != NULL) {
		(void) printf("dumping core by request\n");
		abort();
	}

	exit(2);
}

static int
parseprop(nvlist_t *props)
{
	char *propname = optarg;
	char *propval;

	if ((propval = strchr(propname, '=')) == NULL) {
		(void) fprintf(stderr, gettext("missing "
		    "'=' for -o option\n"));
		return (-1);
	}
	*propval = '\0';
	propval++;
	if (nvlist_exists(props, propname)) {
		(void) fprintf(stderr, gettext("property '%s' "
		    "specified multiple times\n"), propname);
		return (-1);
	}
	if (nvlist_add_string(props, propname, propval) != 0)
		nomem();
	return (0);
}

static int
readpropname(nvlist_t *props)
{
	char *propname = optarg;

	if (strchr(propname, '=') != NULL) {
		(void) fprintf(stderr, gettext(
		    "'=' not allowed with -x option\n"));
		return (-1);
	}
	if (nvlist_exists(props, propname)) {
		(void) fprintf(stderr, gettext("property '%s' "
		    "specified multiple times\n"), propname);
		return (-1);
	}
	if (nvlist_add_boolean(props, propname) != 0)
		nomem();
	return (0);
}

static int
parse_depth(char *opt, int *flags)
{
	char *tmp;
	int depth;

	depth = (int)strtol(opt, &tmp, 0);
	if (*tmp) {
		(void) fprintf(stderr,
		    gettext("%s is not an integer\n"), optarg);
		usage();
	}
	if (depth < 0) {
		(void) fprintf(stderr,
		    gettext("Depth can not be negative.\n"));
		usage();
	}
	*flags |= (ZFS_ITER_DEPTH_LIMIT|ZFS_ITER_RECURSE);
	return (depth);
}

#define	PROGRESS_DELAY 2		/* seconds */

static char *pt_reverse = "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b";
static time_t pt_begin;
static char *pt_header = NULL;
static boolean_t pt_shown;

static void
start_progress_timer(void)
{
	pt_begin = time(NULL) + PROGRESS_DELAY;
	pt_shown = B_FALSE;
}

static void
set_progress_header(char *header)
{
	assert(pt_header == NULL);
	pt_header = safe_strdup(header);
	if (pt_shown) {
		(void) printf("%s: ", header);
		(void) fflush(stdout);
	}
}

static void
update_progress(char *update)
{
	if (!pt_shown && time(NULL) > pt_begin) {
		int len = strlen(update);

		(void) printf("%s: %s%*.*s", pt_header, update, len, len,
		    pt_reverse);
		(void) fflush(stdout);
		pt_shown = B_TRUE;
	} else if (pt_shown) {
		int len = strlen(update);

		(void) printf("%s%*.*s", update, len, len, pt_reverse);
		(void) fflush(stdout);
	}
}

static void
finish_progress(char *done)
{
	if (pt_shown) {
		(void) printf("%s\n", done);
		(void) fflush(stdout);
	}
	free(pt_header);
	pt_header = NULL;
}

#define	CLEAN_SIGCHLD(i)	((i).si_signo == SIGCHLD && \
				(i).si_code == CLD_EXITED && \
				(i).si_status == 0)

/*
 * zfs help
 * zfs help <command>
 * zfs help permission
 * zfs help property
 * zfs help [property] <property>
 */
static int
zfs_do_help(int argc, char **argv)
{
	int rc = 0;
	int idx;

	assert(argc != 0);

	if (argc > 3) {
		/* error */
		(void) fprintf(stderr, gettext("too many arguments\n"));
		zfs_command_help(progname, "help");
		return (-1);
	}

	if (argc == 1) {
		/* zfs help */
		zfs_command_list(stdout, progname, command_table);
		return (0);
	}

	if ((strcmp(argv[1], "property") == 0) ||
	    (strcmp(argv[1], "properties") == 0)) {
		if (argc == 2) {
			/* zfs help property */
			(void) fprintf(stdout, gettext("The following "
			    "dataset properties are supported:\n"));
			zprop_print_list(g_zfs, ZFS_TYPE_DATASET, stdout,
			    B_FALSE);
			(void) fprintf(stdout, gettext("\nThe following "
			    "quota properties are supported:\n"));
			zprop_print_quota(g_zfs, stdout);
			(void) fprintf(stdout, gettext(
			    "\nUser-defined properties are specified by using "
			    "a name containing a colon (:).\n\n"));
			zfs_command_help(progname, "[property] <property>");
		} else {
			/* zfs help property <property> */
			rc = usage_prop(argv[2], stdout);
		}
		return (rc);
	}

	if ((strcmp(argv[1], "permission") == 0) ||
	    (strcmp(argv[1], "permissions") == 0)) {
		if (argc == 2) {
			/* zfs help permission */
			pid_t pid;
			siginfo_t info;
			pid = fork();
			if (pid < 0) {
				(void) fprintf(stderr,
				    gettext("fork failed\n"));
				rc = -1;
			} else if (pid == 0) {
				(void) execv(pypath, argv-1);
				(void) fprintf(stderr, gettext(
				    "internal error: "
				    "%s not found\n"), pypath);
				rc = -1;
			} else {
				rc = waitid(P_PID, pid, &info, WEXITED);
				if (rc == 0 && CLEAN_SIGCHLD(info)) {
					(void) fprintf(stdout, gettext(
					    "\nThe following properties can "
					    "have delegated permissions "
					    "applied:\n"));
					zprop_print_list(g_zfs,
					    ZFS_TYPE_DATASET, stdout, B_TRUE);
				}
			}
		} else {
			/* error */
			(void) fprintf(stderr, gettext("too many arguments\n"));
			zfs_command_help(progname, "help");
			rc = -1;
		}
		return (rc);
	}

	if (argc == 3) {
		/* error */
		(void) fprintf(stderr, gettext("too many arguments\n"));
		zfs_command_help(progname, "help");
		return (-1);
	}

	if (zfs_command_find(argv[1], command_table, &idx) == 0) {
		/* zfs help <command> */
		(void) fprintf(stdout, gettext("usage:\n"));
		(void) fprintf(stdout, "%s",
		    get_usage(command_table[idx].usage));
	} else {
		/* zfs help <property> */
		zfs_prop_t prop = zfs_name_to_prop(argv[1]);
		if (prop != ZPROP_INVAL) {
			rc = usage_prop(argv[1], stdout);
		} else if (zfs_prop_userquota(argv[1])) {
			zprop_describe_quota(stdout, argv[1]);
		} else {
			/* error */
			(void) fprintf(stderr, gettext("invalid command "
			    "'%s'\n"), argv[1]);
			zfs_command_help(progname, "");
			rc = -1;
		}
	}

	return (rc);
}

/*
 * zfs clone [-p] [-o prop=value] ... <snap> <fs | vol>
 *
 * Given an existing dataset, create a writable copy whose initial contents
 * are the same as the source.  The newly created dataset maintains a
 * dependency on the original; the original cannot be destroyed so long as
 * the clone exists.
 *
 * The '-p' flag creates all the non-existing ancestors of the target first.
 */
static int
zfs_do_clone(int argc, char **argv)
{
	zfs_handle_t *zhp = NULL;
	boolean_t parents = B_FALSE, clonenewkey = B_FALSE;
	nvlist_t *props;
	int ret;
	int c;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	/* check options */
	while ((c = getopt(argc, argv, "o:pK")) != -1) {
		switch (c) {
		case 'o':
			if (parseprop(props))
				return (1);
			break;
		case 'p':
			parents = B_TRUE;
			break;
		case 'K':
			clonenewkey = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing source dataset "
		    "argument\n"));
		goto usage;
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing target dataset "
		    "argument\n"));
		goto usage;
	}
	if (argc > 2) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		goto usage;
	}

	/* open the source dataset */
	if ((zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_SNAPSHOT)) == NULL)
		return (1);

	if (parents && zfs_name_valid(argv[1], ZFS_TYPE_FILESYSTEM |
	    ZFS_TYPE_VOLUME)) {
		/*
		 * Now create the ancestors of the target dataset.  If the
		 * target already exists and '-p' option was used we should not
		 * complain.
		 */
		if (zfs_dataset_exists(g_zfs, argv[1], ZFS_TYPE_FILESYSTEM |
		    ZFS_TYPE_VOLUME))
			return (0);
		if (zfs_create_ancestors(g_zfs, argv[1]) != 0)
			return (1);
	}

	/* Pass on clonenewkey CLI argument */
	if (clonenewkey) {
		zfs_crypto_set_clone_newkey(zhp);
	}

	/* pass to libzfs */
	ret = zfs_clone(zhp, argv[1], props);

	/* create the mountpoint if necessary */
	if (ret == 0) {
		zfs_handle_t *clone;

		clone = zfs_open(g_zfs, argv[1], ZFS_TYPE_DATASET);
		if (clone != NULL) {
			if (zfs_get_type(clone) != ZFS_TYPE_VOLUME)
				if ((ret = zfs_mount(clone, NULL, 0)) == 0)
					ret = zfs_share(clone);
			zfs_close(clone);
		}
	}

	zfs_close(zhp);
	nvlist_free(props);

	return (!!ret);

usage:
	if (zhp)
		zfs_close(zhp);
	nvlist_free(props);
	usage();
	return (-1);
}

/*
 * zfs create [-p] [-o prop=value] ... fs
 * zfs create [-ps] [-b blocksize] [-o prop=value] ... -V vol size
 *
 * Create a new dataset.  This command can be used to create filesystems
 * and volumes.  Snapshot creation is handled by 'zfs snapshot'.
 * For volumes, the user must specify a size to be used.
 *
 * The '-s' flag applies only to volumes, and indicates that we should not try
 * to set the reservation for this volume.  By default we set a reservation
 * equal to the size for any volume.  For pools with SPA_VERSION >=
 * SPA_VERSION_REFRESERVATION, we set a refreservation instead.
 *
 * The '-p' flag creates all the non-existing ancestors of the target first.
 */
static int
zfs_do_create(int argc, char **argv)
{
	zfs_type_t type = ZFS_TYPE_FILESYSTEM;
	zfs_handle_t *zhp = NULL;
	uint64_t volsize;
	int c;
	boolean_t noreserve = B_FALSE;
	boolean_t bflag = B_FALSE;
	boolean_t parents = B_FALSE;
	int ret = 1;
	nvlist_t *props;
	uint64_t intval;
	int canmount = ZFS_CANMOUNT_OFF;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	/* check options */
	while ((c = getopt(argc, argv, ":V:b:so:p")) != -1) {
		switch (c) {
		case 'V':
			type = ZFS_TYPE_VOLUME;
			if (zfs_nicestrtonum(g_zfs, optarg, &intval) != 0) {
				(void) fprintf(stderr, gettext("bad volume "
				    "size '%s': %s\n"), optarg,
				    libzfs_error_description(g_zfs));
				goto error;
			}

			if (nvlist_add_uint64(props,
			    zfs_prop_to_name(ZFS_PROP_VOLSIZE), intval) != 0)
				nomem();
			volsize = intval;
			break;
		case 'p':
			parents = B_TRUE;
			break;
		case 'b':
			bflag = B_TRUE;
			if (zfs_nicestrtonum(g_zfs, optarg, &intval) != 0) {
				(void) fprintf(stderr, gettext("bad volume "
				    "block size '%s': %s\n"), optarg,
				    libzfs_error_description(g_zfs));
				goto error;
			}

			if (nvlist_add_uint64(props,
			    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
			    intval) != 0)
				nomem();
			break;
		case 'o':
			if (parseprop(props))
				goto error;
			break;
		case 's':
			noreserve = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing size "
			    "argument\n"));
			goto badusage;
			/* NOTREACHED */
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto badusage;
		}
	}

	if ((bflag || noreserve) && type != ZFS_TYPE_VOLUME) {
		(void) fprintf(stderr, gettext("'-s' and '-b' can only be "
		    "used when creating a volume\n"));
		goto badusage;
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc == 0) {
		(void) fprintf(stderr, gettext("missing %s argument\n"),
		    zfs_type_to_name(type));
		goto badusage;
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		goto badusage;
	}

	if (type == ZFS_TYPE_VOLUME && !noreserve) {
		zpool_handle_t *zpool_handle;
		uint64_t spa_version;
		char *p;
		zfs_prop_t resv_prop;
		char *strval;

		if (p = strchr(argv[0], '/'))
			*p = '\0';
		zpool_handle = zpool_open(g_zfs, argv[0]);
		if (p != NULL)
			*p = '/';
		if (zpool_handle == NULL)
			goto error;
		spa_version = zpool_get_prop_int(zpool_handle,
		    ZPOOL_PROP_VERSION, NULL);
		zpool_close(zpool_handle);
		if (spa_version >= SPA_VERSION_REFRESERVATION)
			resv_prop = ZFS_PROP_REFRESERVATION;
		else
			resv_prop = ZFS_PROP_RESERVATION;
		volsize = zvol_volsize_to_reservation(volsize, props);

		if (nvlist_lookup_string(props, zfs_prop_to_name(resv_prop),
		    &strval) != 0) {
			if (nvlist_add_uint64(props,
			    zfs_prop_to_name(resv_prop), volsize) != 0) {
				nvlist_free(props);
				nomem();
			}
		}
	}

	if (parents && zfs_name_valid(argv[0], type)) {
		/*
		 * Now create the ancestors of target dataset.  If the target
		 * already exists and '-p' option was used we should not
		 * complain.
		 */
		if (zfs_dataset_exists(g_zfs, argv[0], type)) {
			ret = 0;
			goto error;
		}
		if (zfs_create_ancestors(g_zfs, argv[0]) != 0)
			goto error;
	}

	/* pass to libzfs */
	if (zfs_create(g_zfs, argv[0], type, props) != 0)
		goto error;

	if ((zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_DATASET)) == NULL)
		goto error;

	ret = 0;
	/*
	 * if the user doesn't want the dataset automatically mounted,
	 * then skip the mount/share step
	 */
	if (zfs_prop_valid_for_type(ZFS_PROP_CANMOUNT, type))
		canmount = zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT);

	/*
	 * Mount and/or share the new filesystem as appropriate.  We provide a
	 * verbose error message to let the user know that their filesystem was
	 * in fact created, even if we failed to mount or share it.
	 */
	if (canmount == ZFS_CANMOUNT_ON) {
		if (zfs_mount(zhp, NULL, 0) != 0) {
			(void) fprintf(stderr, gettext("filesystem "
			    "successfully created, but not mounted\n"));
			ret = 1;
		} else if (zfs_share(zhp) != 0) {
			(void) fprintf(stderr, gettext("filesystem "
			    "successfully created, but not shared\n"));
			ret = 1;
		}
	}

error:
	if (zhp)
		zfs_close(zhp);
	nvlist_free(props);
	return (ret);
badusage:
	nvlist_free(props);
	usage();
	return (2);
}

/*
 * zfs destroy [-rRf] <fs, vol>
 * zfs destroy [-rRd] <snap>
 *
 *	-r	Recursively destroy all children
 *	-R	Recursively destroy all dependents, including clones
 *	-f	Force unmounting of any dependents
 *	-d	If we can't destroy now, mark for deferred destruction
 *
 * Destroys the given dataset.  By default, it will unmount any filesystems,
 * and refuse to destroy a dataset that has any dependents.  A dependent can
 * either be a child, or a clone of a child.
 */
typedef struct destroy_cbdata {
	boolean_t	cb_first;
	int		cb_force;
	int		cb_recurse;
	int		cb_error;
	int		cb_needforce;
	int		cb_doclones;
	boolean_t	cb_closezhp;
	zfs_handle_t	*cb_target;
	char		*cb_snapname;
	boolean_t	cb_defer_destroy;
} destroy_cbdata_t;

/*
 * Check for any dependents based on the '-r' or '-R' flags.
 */
static int
destroy_check_dependent(zfs_handle_t *zhp, void *data)
{
	destroy_cbdata_t *cbp = data;
	const char *tname = zfs_get_name(cbp->cb_target);
	const char *name = zfs_get_name(zhp);

	if (strncmp(tname, name, strlen(tname)) == 0 &&
	    (name[strlen(tname)] == '/' || name[strlen(tname)] == '@')) {
		/*
		 * This is a direct descendant, not a clone somewhere else in
		 * the hierarchy.
		 */
		if (cbp->cb_recurse)
			goto out;

		if (cbp->cb_first) {
			(void) fprintf(stderr, gettext("cannot destroy '%s': "
			    "%s has children\n"),
			    zfs_get_name(cbp->cb_target),
			    zfs_type_to_name(zfs_get_type(cbp->cb_target)));
			(void) fprintf(stderr, gettext("use '-r' to destroy "
			    "the following datasets:\n"));
			cbp->cb_first = B_FALSE;
			cbp->cb_error = 1;
		}

		(void) fprintf(stderr, "%s\n", zfs_get_name(zhp));
	} else {
		/*
		 * This is a clone.  We only want to report this if the '-r'
		 * wasn't specified, or the target is a snapshot.
		 */
		if (!cbp->cb_recurse &&
		    zfs_get_type(cbp->cb_target) != ZFS_TYPE_SNAPSHOT)
			goto out;

		if (cbp->cb_first) {
			(void) fprintf(stderr, gettext("cannot destroy '%s': "
			    "%s has dependent clones\n"),
			    zfs_get_name(cbp->cb_target),
			    zfs_type_to_name(zfs_get_type(cbp->cb_target)));
			(void) fprintf(stderr, gettext("use '-R' to destroy "
			    "the following datasets:\n"));
			cbp->cb_first = B_FALSE;
			cbp->cb_error = 1;
		}

		(void) fprintf(stderr, "%s\n", zfs_get_name(zhp));
	}

out:
	zfs_close(zhp);
	return (0);
}

static int
destroy_callback(zfs_handle_t *zhp, void *data)
{
	destroy_cbdata_t *cbp = data;

	/*
	 * Ignore pools (which we've already flagged as an error before getting
	 * here).
	 */
	if (strchr(zfs_get_name(zhp), '/') == NULL &&
	    zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) {
		zfs_close(zhp);
		return (0);
	}

	/*
	 * Bail out on the first error.
	 */
	if (zfs_unmount(zhp, NULL, cbp->cb_force ? MS_FORCE : 0) != 0 ||
	    zfs_destroy(zhp, cbp->cb_defer_destroy) != 0) {
		zfs_close(zhp);
		return (-1);
	}

	zfs_close(zhp);
	return (0);
}

static int
destroy_snap_clones(zfs_handle_t *zhp, void *arg)
{
	destroy_cbdata_t *cbp = arg;
	char thissnap[MAXPATHLEN];
	zfs_handle_t *szhp;
	boolean_t closezhp = cbp->cb_closezhp;
	int rv;

	(void) snprintf(thissnap, sizeof (thissnap),
	    "%s@%s", zfs_get_name(zhp), cbp->cb_snapname);

	libzfs_print_on_error(g_zfs, B_FALSE);
	szhp = zfs_open(g_zfs, thissnap, ZFS_TYPE_SNAPSHOT);
	libzfs_print_on_error(g_zfs, B_TRUE);
	if (szhp) {
		/*
		 * Destroy any clones of this snapshot
		 */
		if (zfs_iter_dependents(szhp, B_FALSE, destroy_callback,
		    cbp) != 0) {
			zfs_close(szhp);
			if (closezhp)
				zfs_close(zhp);
			return (-1);
		}
		zfs_close(szhp);
	}

	cbp->cb_closezhp = B_TRUE;
	rv = zfs_iter_filesystems(zhp, destroy_snap_clones, arg);
	if (closezhp)
		zfs_close(zhp);
	return (rv);
}

static int
zfs_do_destroy(int argc, char **argv)
{
	destroy_cbdata_t cb = { 0 };
	int c;
	zfs_handle_t *zhp;
	char *cp;
	zfs_type_t type = ZFS_TYPE_DATASET;

	/* check options */
	while ((c = getopt(argc, argv, "dfrR")) != -1) {
		switch (c) {
		case 'd':
			cb.cb_defer_destroy = B_TRUE;
			type = ZFS_TYPE_SNAPSHOT;
			break;
		case 'f':
			cb.cb_force = 1;
			break;
		case 'r':
			cb.cb_recurse = 1;
			break;
		case 'R':
			cb.cb_recurse = 1;
			cb.cb_doclones = 1;
			break;
		case '?':
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc == 0) {
		(void) fprintf(stderr, gettext("missing path argument\n"));
		usage();
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage();
	}

	/*
	 * If we are doing recursive destroy of a snapshot, then the
	 * named snapshot may not exist.  Go straight to libzfs.
	 */
	if (cb.cb_recurse && (cp = strchr(argv[0], '@'))) {
		int ret;

		*cp = '\0';
		if ((zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_DATASET)) == NULL)
			return (1);
		*cp = '@';
		cp++;

		if (cb.cb_doclones) {
			boolean_t defer = cb.cb_defer_destroy;

			/*
			 * Temporarily ignore the defer_destroy setting since
			 * it's not supported for clones.
			 */
			cb.cb_defer_destroy = B_FALSE;
			cb.cb_snapname = cp;
			if (destroy_snap_clones(zhp, &cb) != 0) {
				zfs_close(zhp);
				return (1);
			}
			cb.cb_defer_destroy = defer;
		}

		ret = zfs_destroy_snaps(zhp, cp, cb.cb_defer_destroy);
		zfs_close(zhp);
		if (ret) {
			(void) fprintf(stderr,
			    gettext("no snapshots destroyed\n"));
		}
		return (ret != 0);
	}

	/* Open the given dataset */
	if ((zhp = zfs_open(g_zfs, argv[0], type)) == NULL)
		return (1);

	cb.cb_target = zhp;

	/*
	 * Perform an explicit check for pools before going any further.
	 */
	if (!cb.cb_recurse && strchr(zfs_get_name(zhp), '/') == NULL &&
	    zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) {
		(void) fprintf(stderr, gettext("cannot destroy '%s': "
		    "operation does not apply to pools\n"),
		    zfs_get_name(zhp));
		(void) fprintf(stderr, gettext("use 'zfs destroy -r "
		    "%s' to destroy all datasets in the pool\n"),
		    zfs_get_name(zhp));
		(void) fprintf(stderr, gettext("use 'zpool destroy %s' "
		    "to destroy the pool itself\n"), zfs_get_name(zhp));
		zfs_close(zhp);
		return (1);
	}

	/*
	 * Check for any dependents and/or clones.
	 */
	cb.cb_first = B_TRUE;
	if (!cb.cb_doclones && !cb.cb_defer_destroy &&
	    zfs_iter_dependents(zhp, B_TRUE, destroy_check_dependent,
	    &cb) != 0) {
		zfs_close(zhp);
		return (1);
	}

	if (cb.cb_error || (!cb.cb_defer_destroy &&
	    (zfs_iter_dependents(zhp, B_FALSE, destroy_callback, &cb) != 0))) {
		zfs_close(zhp);
		return (1);
	}

	/*
	 * Do the real thing.  The callback will close the handle regardless of
	 * whether it succeeds or not.
	 */

	if (destroy_callback(zhp, &cb) != 0)
		return (1);

	return (0);
}

static boolean_t
is_recvd_column(zprop_get_cbdata_t *cbp)
{
	int i;
	zfs_get_column_t col;

	for (i = 0; i < ZFS_GET_NCOLS &&
	    (col = cbp->cb_columns[i]) != GET_COL_NONE; i++)
		if (col == GET_COL_RECVD)
			return (B_TRUE);
	return (B_FALSE);
}

static int
pack_security(nvlist_t *sec_nvl, const char *sec_name, char *buf,
    size_t buflen)
{
	nvpair_t *nvp;
	char *propname;
	char *propval;
	size_t cnt = 0;

	cnt += snprintf(&buf[cnt], buflen - cnt, ",sec=%s", sec_name);
	if (cnt >= buflen)
		return (cnt);

	for (nvp = nvlist_next_nvpair(sec_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(sec_nvl, nvp)) {

		if (nvpair_type(nvp) != DATA_TYPE_STRING)
			continue;
		if (nvpair_value_string(nvp, &propval) != 0)
			continue;
		propname = nvpair_name(nvp);

		cnt += snprintf(&buf[cnt], buflen - cnt, ",%s=%s",
		    propname, propval);
		if (cnt >= buflen)
			return (cnt);
	}

	return (cnt);
}

static int
pack_protocol(nvlist_t *prot_nvl, const char *prot, char *buf,
    size_t buflen)
{
	nvpair_t *nvp;
	nvlist_t *sec_nvl;
	char *propname;
	char *propval;
	size_t cnt = 0;

	cnt += snprintf(&buf[cnt], buflen - cnt, ",prot=%s", prot);
	if (cnt >= buflen)
		return (cnt);

	/*
	 * first print out all global protocol properties
	 */
	for (nvp = nvlist_next_nvpair(prot_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(prot_nvl, nvp)) {

		if (nvpair_type(nvp) != DATA_TYPE_STRING)
			continue;
		if (nvpair_value_string(nvp, &propval) != 0)
			continue;
		propname = nvpair_name(nvp);

		cnt += snprintf(&buf[cnt], buflen - cnt, ",%s=%s",
		    propname, propval);
		if (cnt >= buflen)
			return (cnt);
	}

	/*
	 * now print out security properties
	 */
	for (nvp = nvlist_next_nvpair(prot_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(prot_nvl, nvp)) {

		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;
		if (nvpair_value_nvlist(nvp, &sec_nvl) != 0)
			continue;
		propname = nvpair_name(nvp);

		cnt += pack_security(sec_nvl, propname, &buf[cnt],
		    buflen - cnt);
		if (cnt >= buflen)
			return (cnt);
	}

	return (cnt);
}

static int
pack_share(nvlist_t *nvl, char *buf, size_t buflen)
{
	char *utf8_name;
	char *propname;
	char *propval;
	char *dispval;
	nvpair_t *nvp;
	nvlist_t *prot_nvl = 0;
	size_t cnt = 0;

	/*
	 * The share name is stored in UTF-8, so convert
	 * to current locale before displaying
	 */
	verify((utf8_name = sa_share_get_name(nvl)) != NULL);

	if (sa_utf8_to_locale(utf8_name, &dispval) != SA_OK)
		dispval = safe_strdup(utf8_name);

	cnt += snprintf(&buf[cnt], buflen - cnt, "name=%s", dispval);

	free(dispval);
	dispval = NULL;

	if (cnt >= buflen)
		return (cnt);

	/*
	 * first print out all global properties
	 */
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {

		if (nvpair_type(nvp) != DATA_TYPE_STRING)
			continue;

		propname = nvpair_name(nvp);
		if (strcasecmp(propname, SA_PROP_NAME) == 0)
			continue;

		/* ignore 'mntpnt' property */
		if (strcasecmp(propname, SA_PROP_MNTPNT) == 0)
			continue;

		if (nvpair_value_string(nvp, &propval) != 0)
			continue;

		/*
		 * The description is stored in UTF-8 so convert
		 * to current locale before displaying
		 */
		if (strcasecmp(propname, SA_PROP_DESC) == 0) {
			if (sa_utf8_to_locale(propval, &dispval) != SA_OK)
				dispval = safe_strdup(propval);
			propval = dispval;
		}

		cnt += snprintf(&buf[cnt], buflen - cnt, ",%s=%s",
		    propname, propval);

		if (dispval) {
			free(dispval);
			dispval = NULL;
		}

		if (cnt >= buflen)
			return (cnt);
	}

	/*
	 * now print out protocol properties
	 */
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {

		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;
		if (nvpair_value_nvlist(nvp, &prot_nvl) != 0)
			continue;
		propname = nvpair_name(nvp);

		if (strcasecmp(propname, "nfs") == 0 ||
		    strcasecmp(propname, "smb") == 0) {
			cnt += pack_protocol(prot_nvl, propname, &buf[cnt],
			    buflen - cnt);
		} else {
			cnt += snprintf(&buf[cnt], buflen - cnt,
			    ",prot=%s", propname);
		}
		if (cnt >= buflen)
			return (cnt);
	}

	return (cnt);
}

static char *
share_to_str(nvlist_t *share)
{
	size_t buflen;
	char *share_buf;

	verify(nvlist_size(share, &buflen, NV_ENCODE_NATIVE) == 0);
	share_buf = safe_malloc(buflen);
	share_buf[0] = '\0';

	(void) pack_share(share, share_buf, buflen);

	return (share_buf);
}

/*
 * zfs get share dataset
 *
 * Read all shares from zhp and print the share
 */
static int
do_get_shares(zfs_handle_t *zhp, zprop_list_t *pl, zprop_get_cbdata_t *cbp)
{
	void *hdl;
	nvlist_t *share;
	char *strval;
	char mountpoint[ZFS_MAXPROPLEN];

	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) != 0)
		return (0);

	if (strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) == 0)
		return (0);

	if (strcmp(mountpoint, ZFS_MOUNTPOINT_NONE) == 0)
		return (0);

	if (!sa_path_in_current_zone(mountpoint))
		return (0);

	if (zfs_share_read_init(zhp, &hdl) != 0)
		return (0);

	while (zfs_share_read_next(zhp, hdl, &share) == 0) {
		strval = share_to_str(share);
		if (strval != NULL) {
			zprop_print_one_property(zfs_get_name(zhp), cbp,
			    pl->pl_user_prop, strval, ZPROP_SRC_LOCAL,
			    "", NULL);
			free(strval);
		}
		sa_share_free(share);
	}

	zfs_share_read_fini(zhp, hdl);

	return (0);
}

/*
 * zfs get [-rHp] [-o all | field[,field]...] [-s source[,source]...]
 *	< all | property[,property]... > < fs | snap | vol > ...
 *
 *	-r	recurse over any child datasets
 *	-H	scripted mode.  Headers are stripped, and fields are separated
 *		by tabs instead of spaces.
 *	-o	Set of fields to display.  One of "name,property,value,
 *		received,source". Default is "name,property,value,source".
 *		"all" is an alias for all five.
 *	-s	Set of sources to allow.  One of
 *		"local,default,inherited,received,temporary,none".  Default is
 *		all six.
 *	-p	Display values in parsable (literal) format.
 *
 *  Prints properties for the given datasets.  The user can control which
 *  columns to display as well as which property types to allow.
 */

/*
 * Invoked to display the properties for a single dataset.
 */
/* ARGSUSED */
static int
get_callback(zfs_handle_t *zhp, int depth, void *data)
{
	char buf[ZFS_MAXPROPLEN];
	char rbuf[ZFS_MAXPROPLEN];
	zprop_source_t sourcetype;
	char source[ZFS_MAXNAMELEN];
	zprop_get_cbdata_t *cbp = data;
	nvlist_t *user_props = zfs_get_user_props(zhp);
	zprop_list_t *pl = cbp->cb_proplist;
	nvlist_t *propval;
	char *strval;
	char *sourceval;
	boolean_t received = is_recvd_column(cbp);

	for (; pl != NULL; pl = pl->pl_next) {
		char *recvdval = NULL;
		/*
		 * Skip the special fake placeholder.  This will also skip over
		 * the name property when 'all' is specified.
		 */
		if (pl->pl_prop == ZFS_PROP_NAME &&
		    pl == cbp->cb_proplist)
			continue;

		if (pl->pl_prop != ZPROP_INVAL) {
			if (zfs_prop_get(zhp, pl->pl_prop, buf,
			    sizeof (buf), &sourcetype, source,
			    sizeof (source),
			    cbp->cb_literal) != 0) {
				if (pl->pl_all)
					continue;
				if (!zfs_prop_valid_for_type(pl->pl_prop,
				    ZFS_TYPE_DATASET)) {
					(void) fprintf(stderr,
					    gettext("No such property '%s'\n"),
					    zfs_prop_to_name(pl->pl_prop));
					continue;
				}
				sourcetype = ZPROP_SRC_NONE;
				(void) strlcpy(buf, "-", sizeof (buf));
			}

			if (received && (zfs_prop_get_recvd(zhp,
			    zfs_prop_to_name(pl->pl_prop), rbuf, sizeof (rbuf),
			    cbp->cb_literal) == 0))
				recvdval = rbuf;

			zprop_print_one_property(zfs_get_name(zhp), cbp,
			    zfs_prop_to_name(pl->pl_prop),
			    buf, sourcetype, source, recvdval);
		} else if (zfs_prop_userquota(pl->pl_user_prop)) {
			sourcetype = ZPROP_SRC_LOCAL;

			if (zfs_prop_get_userquota(zhp, pl->pl_user_prop,
			    buf, sizeof (buf), cbp->cb_literal) != 0) {
				sourcetype = ZPROP_SRC_NONE;
				(void) strlcpy(buf, "-", sizeof (buf));
			}

			zprop_print_one_property(zfs_get_name(zhp), cbp,
			    pl->pl_user_prop, buf, sourcetype, source, NULL);
		} else if (strcmp(pl->pl_user_prop, "share") == 0) {
			/* display shares */
			(void) do_get_shares(zhp, pl, cbp);
		} else {
			if (nvlist_lookup_nvlist(user_props,
			    pl->pl_user_prop, &propval) != 0) {
				if (pl->pl_all)
					continue;
				sourcetype = ZPROP_SRC_NONE;
				strval = "-";
			} else {
				verify(nvlist_lookup_string(propval,
				    ZPROP_VALUE, &strval) == 0);
				verify(nvlist_lookup_string(propval,
				    ZPROP_SOURCE, &sourceval) == 0);

				if (strcmp(sourceval,
				    zfs_get_name(zhp)) == 0) {
					sourcetype = ZPROP_SRC_LOCAL;
				} else if (strcmp(sourceval,
				    ZPROP_SOURCE_VAL_RECVD) == 0) {
					sourcetype = ZPROP_SRC_RECEIVED;
				} else {
					sourcetype = ZPROP_SRC_INHERITED;
					(void) strlcpy(source,
					    sourceval, sizeof (source));
				}
			}

			if (received && (zfs_prop_get_recvd(zhp,
			    pl->pl_user_prop, rbuf, sizeof (rbuf),
			    cbp->cb_literal) == 0))
				recvdval = rbuf;

			zprop_print_one_property(zfs_get_name(zhp), cbp,
			    pl->pl_user_prop, strval, sourcetype,
			    source, recvdval);
		}
	}

	return (0);
}

static int
zfs_do_get(int argc, char **argv)
{
	zprop_get_cbdata_t cb = { 0 };
	int i, c, flags = 0;
	char *value, *fields;
	int ret;
	int limit = 0;
	zprop_list_t fake_name = { 0 };

	/*
	 * Set up default columns and sources.
	 */
	cb.cb_sources = ZPROP_SRC_ALL;
	cb.cb_columns[0] = GET_COL_NAME;
	cb.cb_columns[1] = GET_COL_PROPERTY;
	cb.cb_columns[2] = GET_COL_VALUE;
	cb.cb_columns[3] = GET_COL_SOURCE;
	cb.cb_type = ZFS_TYPE_DATASET;

	/* check options */
	while ((c = getopt(argc, argv, ":d:o:s:rHp")) != -1) {
		switch (c) {
		case 'p':
			cb.cb_literal = B_TRUE;
			break;
		case 'd':
			limit = parse_depth(optarg, &flags);
			break;
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			break;
		case 'H':
			cb.cb_scripted = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage();
			break;
		case 'o':
			/*
			 * Process the set of columns to display.  We zero out
			 * the structure to give us a blank slate.
			 */
			bzero(&cb.cb_columns, sizeof (cb.cb_columns));
			i = 0;
			while (*optarg != '\0') {
				static char *col_subopts[] =
				    { "name", "property", "value", "received",
				    "source", "all", NULL };

				if (i == ZFS_GET_NCOLS) {
					(void) fprintf(stderr, gettext("too "
					    "many fields given to -o "
					    "option\n"));
					usage();
				}

				switch (getsubopt(&optarg, col_subopts,
				    &value)) {
				case 0:
					cb.cb_columns[i++] = GET_COL_NAME;
					break;
				case 1:
					cb.cb_columns[i++] = GET_COL_PROPERTY;
					break;
				case 2:
					cb.cb_columns[i++] = GET_COL_VALUE;
					break;
				case 3:
					cb.cb_columns[i++] = GET_COL_RECVD;
					flags |= ZFS_ITER_RECVD_PROPS;
					break;
				case 4:
					cb.cb_columns[i++] = GET_COL_SOURCE;
					break;
				case 5:
					if (i > 0) {
						(void) fprintf(stderr,
						    gettext("\"all\" conflicts "
						    "with specific fields "
						    "given to -o option\n"));
						usage();
					}
					cb.cb_columns[0] = GET_COL_NAME;
					cb.cb_columns[1] = GET_COL_PROPERTY;
					cb.cb_columns[2] = GET_COL_VALUE;
					cb.cb_columns[3] = GET_COL_RECVD;
					cb.cb_columns[4] = GET_COL_SOURCE;
					flags |= ZFS_ITER_RECVD_PROPS;
					i = ZFS_GET_NCOLS;
					break;
				default:
					(void) fprintf(stderr,
					    gettext("invalid column name "
					    "'%s'\n"), value);
					usage();
				}
			}
			break;

		case 's':
			cb.cb_sources = 0;
			while (*optarg != '\0') {
				static char *source_subopts[] = {
					"local", "default", "inherited",
					"received", "temporary", "none",
					NULL };

				switch (getsubopt(&optarg, source_subopts,
				    &value)) {
				case 0:
					cb.cb_sources |= ZPROP_SRC_LOCAL;
					break;
				case 1:
					cb.cb_sources |= ZPROP_SRC_DEFAULT;
					break;
				case 2:
					cb.cb_sources |= ZPROP_SRC_INHERITED;
					break;
				case 3:
					cb.cb_sources |= ZPROP_SRC_RECEIVED;
					break;
				case 4:
					cb.cb_sources |= ZPROP_SRC_TEMPORARY;
					break;
				case 5:
					cb.cb_sources |= ZPROP_SRC_NONE;
					break;
				default:
					(void) fprintf(stderr,
					    gettext("invalid source "
					    "'%s'\n"), value);
					usage();
				}
			}
			break;

		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing property "
		    "argument\n"));
		usage();
	}

	fields = argv[0];

	if (zprop_get_list(g_zfs, fields, &cb.cb_proplist, ZFS_TYPE_DATASET)
	    != 0)
		usage();

	argc--;
	argv++;

	/*
	 * As part of zfs_expand_proplist(), we keep track of the maximum column
	 * width for each property.  For the 'NAME' (and 'SOURCE') columns, we
	 * need to know the maximum name length.  However, the user likely did
	 * not specify 'name' as one of the properties to fetch, so we need to
	 * make sure we always include at least this property for
	 * print_get_headers() to work properly.
	 */
	if (cb.cb_proplist != NULL) {
		fake_name.pl_prop = ZFS_PROP_NAME;
		fake_name.pl_width = strlen(gettext("NAME"));
		fake_name.pl_next = cb.cb_proplist;
		cb.cb_proplist = &fake_name;
	}

	cb.cb_first = B_TRUE;

	/* run for each object */
	ret = zfs_for_each(argc, argv, flags, ZFS_TYPE_DATASET, NULL,
	    &cb.cb_proplist, limit, get_callback, &cb);

	if (cb.cb_proplist == &fake_name)
		zprop_free_list(fake_name.pl_next);
	else
		zprop_free_list(cb.cb_proplist);

	return (ret);
}

/*
 * inherit [-rS] <property> <fs|vol> ...
 *
 *	-r	Recurse over all children
 *	-S	Revert to received value, if any
 *
 * For each dataset specified on the command line, inherit the given property
 * from its parent.  Inheriting a property at the pool level will cause it to
 * use the default value.  The '-r' flag will recurse over all children, and is
 * useful for setting a property on a hierarchy-wide basis, regardless of any
 * local modifications for each dataset.
 */

typedef struct inherit_cbdata {
	const char *cb_propname;
	boolean_t cb_received;
} inherit_cbdata_t;

/* ARGSUSED */
static int
inherit_cb(zfs_handle_t *zhp, int depth, void *data)
{
	inherit_cbdata_t *cb = data;

	return (zfs_prop_inherit(zhp, cb->cb_propname, cb->cb_received) != 0);
}

static int
inherit_recurse_cb(zfs_handle_t *zhp, int depth, void *data)
{
	inherit_cbdata_t *cb = data;
	zfs_prop_t prop = zfs_name_to_prop(cb->cb_propname);

	/*
	 * If we're doing it recursively, then ignore properties that
	 * are not valid for this type of dataset.
	 */
	if (prop != ZPROP_INVAL &&
	    !zfs_prop_valid_for_type(prop, zfs_get_type(zhp)))
		return (0);

	return (inherit_cb(zhp, depth, data));
}

static int
zfs_do_inherit(int argc, char **argv)
{
	int c;
	zfs_prop_t prop;
	zfs_iter_cb iter_cb;
	inherit_cbdata_t cb = { 0 };
	char *propname;
	int ret;
	int flags = 0;
	boolean_t received = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, "rS")) != -1) {
		switch (c) {
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			break;
		case 'S':
			received = B_TRUE;
			break;
		case '?':
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing property argument\n"));
		usage();
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing dataset argument\n"));
		usage();
	}

	propname = argv[0];
	argc--;
	argv++;

	if ((prop = zfs_name_to_prop(propname)) != ZPROP_INVAL) {
		if (zfs_prop_readonly(prop)) {
			(void) fprintf(stderr, gettext(
			    "%s property is read-only\n"),
			    propname);
			return (1);
		}
		if (!zfs_prop_inheritable(prop) && !received) {
			(void) fprintf(stderr, gettext("'%s' property cannot "
			    "be inherited\n"), propname);
			if (prop == ZFS_PROP_QUOTA ||
			    prop == ZFS_PROP_RESERVATION ||
			    prop == ZFS_PROP_REFQUOTA ||
			    prop == ZFS_PROP_REFRESERVATION)
				(void) fprintf(stderr, gettext("use 'zfs set "
				    "%s=none' to clear\n"), propname);
			return (1);
		}
		if (received && (prop == ZFS_PROP_VOLSIZE ||
		    prop == ZFS_PROP_VERSION)) {
			(void) fprintf(stderr, gettext("'%s' property cannot "
			    "be reverted to a received value\n"), propname);
			return (1);
		}
	} else if (!zfs_prop_user(propname)) {
		(void) fprintf(stderr, gettext("invalid property '%s'\n"),
		    propname);
		usage();
	}

	cb.cb_propname = propname;
	cb.cb_received = received;

	iter_cb = ((flags & ZFS_ITER_RECURSE) ? inherit_recurse_cb :
	    inherit_cb);
	ret = zfs_for_each(argc, argv, flags, ZFS_TYPE_DATASET, NULL, NULL, 0,
	    iter_cb, &cb);

	return (ret);
}

typedef struct upgrade_cbdata {
	uint64_t cb_numupgraded;
	uint64_t cb_numsamegraded;
	uint64_t cb_numfailed;
	uint64_t cb_version;
	boolean_t cb_newer;
	boolean_t cb_foundone;
	char cb_lastfs[ZFS_MAXNAMELEN];
} upgrade_cbdata_t;

static int
same_pool(zfs_handle_t *zhp, const char *name)
{
	int len1 = strcspn(name, "/@");
	const char *zhname = zfs_get_name(zhp);
	int len2 = strcspn(zhname, "/@");

	if (len1 != len2)
		return (B_FALSE);
	return (strncmp(name, zhname, len1) == 0);
}

/* ARGSUSED */
static int
upgrade_list_callback(zfs_handle_t *zhp, int depth, void *data)
{
	upgrade_cbdata_t *cb = data;
	int version = zfs_prop_get_int(zhp, ZFS_PROP_VERSION);

	/* list if it's old/new */
	if ((!cb->cb_newer && version < ZPL_VERSION) ||
	    (cb->cb_newer && version > ZPL_VERSION)) {
		char *str;
		if (cb->cb_newer) {
			str = gettext("The following filesystems are "
			    "formatted using a newer software version and\n"
			    "cannot be accessed on the current system.\n\n");
		} else {
			str = gettext("The following filesystems are "
			    "out of date, and can be upgraded.  After being\n"
			    "upgraded, these filesystems (and any 'zfs send' "
			    "streams generated from\n"
			    "subsequent snapshots) will no longer be "
			    "accessible by older software versions.\n\n");
		}

		if (!cb->cb_foundone) {
			(void) puts(str);
			(void) printf(gettext("VER  FILESYSTEM\n"));
			(void) printf(gettext("---  ------------\n"));
			cb->cb_foundone = B_TRUE;
		}

		(void) printf("%2u   %s\n", version, zfs_get_name(zhp));
	}

	return (0);
}

/* ARGSUSED */
static int
upgrade_set_callback(zfs_handle_t *zhp, int depth, void *data)
{
	upgrade_cbdata_t *cb = data;
	int version = zfs_prop_get_int(zhp, ZFS_PROP_VERSION);
	int needed_spa_version;
	int spa_version;

	if (zfs_spa_version(zhp, &spa_version) < 0)
		return (-1);

	needed_spa_version = zfs_spa_version_map(cb->cb_version);

	if (needed_spa_version < 0)
		return (-1);

	if (spa_version < needed_spa_version) {
		/* can't upgrade */
		(void) printf(gettext("%s: can not be "
		    "upgraded; the pool version needs to first "
		    "be upgraded\nto version %d\n\n"),
		    zfs_get_name(zhp), needed_spa_version);
		cb->cb_numfailed++;
		return (0);
	}

	/* upgrade */
	if (version < cb->cb_version) {
		char verstr[16];
		(void) snprintf(verstr, sizeof (verstr),
		    "%llu", cb->cb_version);
		if (cb->cb_lastfs[0] && !same_pool(zhp, cb->cb_lastfs)) {
			/*
			 * If they did "zfs upgrade -a", then we could
			 * be doing ioctls to different pools.  We need
			 * to log this history once to each pool.
			 */
			verify(zpool_stage_history(g_zfs, history_str) == 0);
		}
		if (zfs_prop_set(zhp, "version", verstr) == 0)
			cb->cb_numupgraded++;
		else
			cb->cb_numfailed++;
		(void) strcpy(cb->cb_lastfs, zfs_get_name(zhp));
	} else if (version > cb->cb_version) {
		/* can't downgrade */
		(void) printf(gettext("%s: can not be downgraded; "
		    "it is already at version %u\n"),
		    zfs_get_name(zhp), version);
		cb->cb_numfailed++;
	} else {
		cb->cb_numsamegraded++;
	}
	return (0);
}

/*
 * zfs upgrade
 * zfs upgrade -v
 * zfs upgrade [-r] [-V <version>] <-a | filesystem>
 */
static int
zfs_do_upgrade(int argc, char **argv)
{
	boolean_t all = B_FALSE;
	boolean_t showversions = B_FALSE;
	int ret;
	upgrade_cbdata_t cb = { 0 };
	char c;
	int flags = ZFS_ITER_ARGS_CAN_BE_PATHS;

	/* check options */
	while ((c = getopt(argc, argv, "rvV:a")) != -1) {
		switch (c) {
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			break;
		case 'v':
			showversions = B_TRUE;
			break;
		case 'V':
			if (zfs_prop_string_to_index(ZFS_PROP_VERSION,
			    optarg, &cb.cb_version) != 0) {
				(void) fprintf(stderr,
				    gettext("invalid version %s\n"), optarg);
				usage();
			}
			break;
		case 'a':
			all = B_TRUE;
			break;
		case '?':
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if ((!all && !argc) && ((flags & ZFS_ITER_RECURSE) | cb.cb_version))
		usage();
	if (showversions && (flags & ZFS_ITER_RECURSE || all ||
	    cb.cb_version || argc))
		usage();
	if ((all || argc) && (showversions))
		usage();
	if (all && argc)
		usage();

	if (showversions) {
		/* Show info on available versions. */
		(void) printf(gettext("The following filesystem versions are "
		    "supported:\n\n"));
		(void) printf(gettext("VER  DESCRIPTION\n"));
		(void) printf("---  -----------------------------------------"
		    "---------------\n");
		(void) printf(gettext(" 1   Initial ZFS filesystem version\n"));
		(void) printf(gettext(" 2   Enhanced directory entries\n"));
		(void) printf(gettext(" 3   Case insensitive and SMB "
		    "credentials support\n"));
		(void) printf(gettext(" 4   userquota, groupquota "
		    "properties\n"));
		(void) printf(gettext(" 5   System attributes\n"));
		(void) printf(gettext("\nFor more information on a particular "
		    "version, including supported releases,\n"));
		(void) printf("see the ZFS Administration Guide.\n\n");
		ret = 0;
	} else if (argc || all) {
		/* Upgrade filesystems */
		if (cb.cb_version == 0)
			cb.cb_version = ZPL_VERSION;
		ret = zfs_for_each(argc, argv, flags, ZFS_TYPE_FILESYSTEM,
		    NULL, NULL, 0, upgrade_set_callback, &cb);
		(void) printf(gettext("%llu filesystems upgraded\n"),
		    cb.cb_numupgraded);
		if (cb.cb_numsamegraded) {
			(void) printf(gettext("%llu filesystems already at "
			    "this version\n"),
			    cb.cb_numsamegraded);
		}
		if (cb.cb_numfailed != 0)
			ret = 1;
	} else {
		/* List old-version filesytems */
		boolean_t found;
		(void) printf(gettext("This system is currently running "
		    "ZFS filesystem version %llu.\n\n"), ZPL_VERSION);

		flags |= ZFS_ITER_RECURSE;
		ret = zfs_for_each(0, NULL, flags, ZFS_TYPE_FILESYSTEM,
		    NULL, NULL, 0, upgrade_list_callback, &cb);

		found = cb.cb_foundone;
		cb.cb_foundone = B_FALSE;
		cb.cb_newer = B_TRUE;

		ret = zfs_for_each(0, NULL, flags, ZFS_TYPE_FILESYSTEM,
		    NULL, NULL, 0, upgrade_list_callback, &cb);

		if (!cb.cb_foundone && !found) {
			(void) printf(gettext("All filesystems are "
			    "formatted with the current version.\n"));
		}
	}

	return (ret);
}

/*
 * zfs userspace
 */
static int
userspace_cb(void *arg, const char *domain, uid_t rid, uint64_t space)
{
	zfs_userquota_prop_t *typep = arg;
	zfs_userquota_prop_t p = *typep;
	char *name = NULL;
	char *ug, *propname;
	char namebuf[32];
	char sizebuf[32];

	if (domain == NULL || domain[0] == '\0') {
		if (p == ZFS_PROP_GROUPUSED || p == ZFS_PROP_GROUPQUOTA) {
			struct group *g = getgrgid(rid);
			if (g)
				name = g->gr_name;
		} else {
			struct passwd *p = getpwuid(rid);
			if (p)
				name = p->pw_name;
		}
	}

	if (p == ZFS_PROP_GROUPUSED || p == ZFS_PROP_GROUPQUOTA)
		ug = "group";
	else
		ug = "user";

	if (p == ZFS_PROP_USERUSED || p == ZFS_PROP_GROUPUSED)
		propname = "used";
	else
		propname = "quota";

	if (name == NULL) {
		(void) snprintf(namebuf, sizeof (namebuf),
		    "%llu", (longlong_t)rid);
		name = namebuf;
	}
	zfs_nicenum(space, sizebuf, sizeof (sizebuf));

	(void) printf("%s %s %s%c%s %s\n", propname, ug, domain,
	    domain[0] ? '-' : ' ', name, sizebuf);

	return (0);
}

static int
zfs_do_userspace(int argc, char **argv)
{
	zfs_handle_t *zhp;
	zfs_userquota_prop_t p;
	int error;

	/*
	 * Try the python version.  If the execv fails, we'll continue
	 * and do a simplistic implementation.
	 */
	(void) execv(pypath, argv-1);

	(void) printf("internal error: %s not found\n"
	    "falling back on built-in implementation, "
	    "some features will not work\n", pypath);

	if ((zhp = zfs_open(g_zfs, argv[argc-1], ZFS_TYPE_DATASET)) == NULL)
		return (1);

	(void) printf("PROP TYPE NAME VALUE\n");

	for (p = 0; p < ZFS_NUM_USERQUOTA_PROPS; p++) {
		error = zfs_userspace(zhp, p, userspace_cb, &p);
		if (error)
			break;
	}
	return (error);
}

/*
 * list [-r][-d max] [-H] [-o property[,property]...] [-t type[,type]...]
 *      [-s property [-s property]...] [-S property [-S property]...]
 *      <dataset> ...
 *
 *	-r	Recurse over all children
 *	-d	Limit recursion by depth.
 *	-H	Scripted mode; elide headers and separate columns by tabs
 *	-o	Control which fields to display.
 *	-t	Control which object types to display.
 *	-s	Specify sort columns, descending order.
 *	-S	Specify sort columns, ascending order.
 *
 * When given no arguments, lists all filesystems in the system.
 * Otherwise, list the specified datasets, optionally recursing down them if
 * '-r' is specified.
 */
typedef struct list_cbdata {
	boolean_t	cb_first;
	boolean_t	cb_scripted;
	zprop_list_t	*cb_proplist;
} list_cbdata_t;

/*
 * Given a list of columns to display, output appropriate headers for each one.
 */
static void
print_header(zprop_list_t *pl)
{
	char headerbuf[ZFS_MAXPROPLEN];
	const char *header;
	int i;
	boolean_t first = B_TRUE;
	boolean_t right_justify;

	for (; pl != NULL; pl = pl->pl_next) {
		if (!first) {
			(void) printf("  ");
		} else {
			first = B_FALSE;
		}

		right_justify = B_FALSE;
		if (pl->pl_prop != ZPROP_INVAL) {
			header = zfs_prop_column_name(pl->pl_prop);
			right_justify = zfs_prop_align_right(pl->pl_prop);
		} else {
			for (i = 0; pl->pl_user_prop[i] != '\0'; i++)
				headerbuf[i] = toupper(pl->pl_user_prop[i]);
			headerbuf[i] = '\0';
			header = headerbuf;
		}

		if (pl->pl_next == NULL && !right_justify)
			(void) printf("%s", header);
		else if (right_justify)
			(void) printf("%*s", pl->pl_width, header);
		else
			(void) printf("%-*s", pl->pl_width, header);
	}

	(void) printf("\n");
}

/*
 * Given a dataset and a list of fields, print out all the properties according
 * to the described layout.
 */
static void
print_dataset(zfs_handle_t *zhp, zprop_list_t *pl, boolean_t scripted)
{
	boolean_t first = B_TRUE;
	char property[ZFS_MAXPROPLEN];
	nvlist_t *userprops = zfs_get_user_props(zhp);
	nvlist_t *propval;
	char *propstr;
	boolean_t right_justify;
	int width;

	for (; pl != NULL; pl = pl->pl_next) {
		if (!first) {
			if (scripted)
				(void) printf("\t");
			else
				(void) printf("  ");
		} else {
			first = B_FALSE;
		}

		if (pl->pl_prop != ZPROP_INVAL) {
			if (zfs_prop_get(zhp, pl->pl_prop, property,
			    sizeof (property), NULL, NULL, 0, B_FALSE) != 0)
				propstr = "-";
			else
				propstr = property;

			right_justify = zfs_prop_align_right(pl->pl_prop);
		} else if (zfs_prop_userquota(pl->pl_user_prop)) {
			if (zfs_prop_get_userquota(zhp, pl->pl_user_prop,
			    property, sizeof (property), B_FALSE) != 0)
				propstr = "-";
			else
				propstr = property;
			right_justify = B_TRUE;
		} else {
			if (nvlist_lookup_nvlist(userprops,
			    pl->pl_user_prop, &propval) != 0)
				propstr = "-";
			else
				verify(nvlist_lookup_string(propval,
				    ZPROP_VALUE, &propstr) == 0);
			right_justify = B_FALSE;
		}

		width = pl->pl_width;

		/*
		 * If this is being called in scripted mode, or if this is the
		 * last column and it is left-justified, don't include a width
		 * format specifier.
		 */
		if (scripted || (pl->pl_next == NULL && !right_justify))
			(void) printf("%s", propstr);
		else if (right_justify)
			(void) printf("%*s", width, propstr);
		else
			(void) printf("%-*s", width, propstr);
	}

	(void) printf("\n");
}

/*
 * Generic callback function to list a dataset or snapshot.
 */
/* ARGSUSED */
static int
list_callback(zfs_handle_t *zhp, int depth, void *data)
{
	list_cbdata_t *cbp = data;

	if (cbp->cb_first) {
		if (!cbp->cb_scripted)
			print_header(cbp->cb_proplist);
		cbp->cb_first = B_FALSE;
	}

	print_dataset(zhp, cbp->cb_proplist, cbp->cb_scripted);

	return (0);
}

static int
zfs_do_list(int argc, char **argv)
{
	int c;
	boolean_t scripted = B_FALSE;
	static char default_fields[] =
	    "name,used,available,referenced,mountpoint";
	int types = ZFS_TYPE_DATASET;
	boolean_t types_specified = B_FALSE;
	char *fields = NULL;
	list_cbdata_t cb = { 0 };
	char *value;
	int limit = 0;
	int ret;
	zfs_sort_column_t *sortcol = NULL;
	int flags = ZFS_ITER_PROP_LISTSNAPS | ZFS_ITER_ARGS_CAN_BE_PATHS;

	/* check options */
	while ((c = getopt(argc, argv, ":d:o:rt:Hs:S:")) != -1) {
		switch (c) {
		case 'o':
			fields = optarg;
			break;
		case 'd':
			limit = parse_depth(optarg, &flags);
			break;
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			break;
		case 'H':
			scripted = B_TRUE;
			break;
		case 's':
			if (zfs_add_sort_column(&sortcol, optarg,
			    B_FALSE) != 0) {
				(void) fprintf(stderr,
				    gettext("invalid property '%s'\n"), optarg);
				usage();
			}
			break;
		case 'S':
			if (zfs_add_sort_column(&sortcol, optarg,
			    B_TRUE) != 0) {
				(void) fprintf(stderr,
				    gettext("invalid property '%s'\n"), optarg);
				usage();
			}
			break;
		case 't':
			types = 0;
			types_specified = B_TRUE;
			flags &= ~ZFS_ITER_PROP_LISTSNAPS;
			while (*optarg != '\0') {
				static char *type_subopts[] = { "filesystem",
				    "fs", "volume", "vol", "snapshot", "snap",
				    "all", NULL };

				switch (getsubopt(&optarg, type_subopts,
				    &value)) {
				case 0:
				case 1:
					types |= ZFS_TYPE_FILESYSTEM;
					break;
				case 2:
				case 3:
					types |= ZFS_TYPE_VOLUME;
					break;
				case 4:
				case 5:
					types |= ZFS_TYPE_SNAPSHOT;
					break;
				case 6:
					types = ZFS_TYPE_DATASET;
					break;

				default:
					(void) fprintf(stderr,
					    gettext("invalid type '%s'\n"),
					    value);
					usage();
				}
			}
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage();
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (fields == NULL)
		fields = default_fields;

	/*
	 * If "-o space" and no types were specified, don't display snapshots.
	 */
	if (strcmp(fields, "space") == 0 && types_specified == B_FALSE)
		types &= ~ZFS_TYPE_SNAPSHOT;

	/*
	 * If the user specifies '-o all', the zprop_get_list() doesn't
	 * normally include the name of the dataset.  For 'zfs list', we always
	 * want this property to be first.
	 */
	if (zprop_get_list(g_zfs, fields, &cb.cb_proplist, ZFS_TYPE_DATASET)
	    != 0)
		usage();

	cb.cb_scripted = scripted;
	cb.cb_first = B_TRUE;

	ret = zfs_for_each(argc, argv, flags, types, sortcol, &cb.cb_proplist,
	    limit, list_callback, &cb);

	zprop_free_list(cb.cb_proplist);
	zfs_free_sort_columns(sortcol);

	if (ret == 0 && cb.cb_first && !cb.cb_scripted)
		(void) printf(gettext("no datasets available\n"));

	return (ret);
}

/*
 * zfs rename <fs | snap | vol> <fs | snap | vol>
 * zfs rename -p <fs | vol> <fs | vol>
 * zfs rename -r <snap> <snap>
 *
 * Renames the given dataset to another of the same type.
 *
 * The '-p' flag creates all the non-existing ancestors of the target first.
 */
/* ARGSUSED */
static int
zfs_do_rename(int argc, char **argv)
{
	zfs_handle_t *zhp;
	int c;
	int ret;
	boolean_t recurse = B_FALSE;
	boolean_t parents = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, "pr")) != -1) {
		switch (c) {
		case 'p':
			parents = B_TRUE;
			break;
		case 'r':
			recurse = B_TRUE;
			break;
		case '?':
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing source dataset "
		    "argument\n"));
		usage();
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing target dataset "
		    "argument\n"));
		usage();
	}
	if (argc > 2) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage();
	}

	if (recurse && parents) {
		(void) fprintf(stderr, gettext("-p and -r options are mutually "
		    "exclusive\n"));
		usage();
	}

	if (recurse && strchr(argv[0], '@') == 0) {
		(void) fprintf(stderr, gettext("source dataset for recursive "
		    "rename must be a snapshot\n"));
		usage();
	}

	if ((zhp = zfs_open(g_zfs, argv[0], parents ? ZFS_TYPE_FILESYSTEM |
	    ZFS_TYPE_VOLUME : ZFS_TYPE_DATASET)) == NULL)
		return (1);

	/* If we were asked and the name looks good, try to create ancestors. */
	if (parents && zfs_name_valid(argv[1], zfs_get_type(zhp)) &&
	    zfs_create_ancestors(g_zfs, argv[1]) != 0) {
		zfs_close(zhp);
		return (1);
	}

	ret = (zfs_rename(zhp, argv[1], recurse) != 0);

	zfs_close(zhp);
	return (ret);
}

/*
 * zfs promote <fs>
 *
 * Promotes the given clone fs to be the parent
 */
/* ARGSUSED */
static int
zfs_do_promote(int argc, char **argv)
{
	zfs_handle_t *zhp;
	int ret;

	/* check options */
	if (argc > 1 && argv[1][0] == '-') {
		(void) fprintf(stderr, gettext("invalid option '%c'\n"),
		    argv[1][1]);
		usage();
	}

	/* check number of arguments */
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing clone filesystem"
		    " argument\n"));
		usage();
	}
	if (argc > 2) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage();
	}

	zhp = zfs_open(g_zfs, argv[1], ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zhp == NULL)
		return (1);

	ret = (zfs_promote(zhp) != 0);


	zfs_close(zhp);
	return (ret);
}

/*
 * zfs rollback [-rRf] <snapshot>
 *
 *	-r	Delete any intervening snapshots before doing rollback
 *	-R	Delete any snapshots and their clones
 *	-f	ignored for backwards compatibility
 *
 * Given a filesystem, rollback to a specific snapshot, discarding any changes
 * since then and making it the active dataset.  If more recent snapshots exist,
 * the command will complain unless the '-r' flag is given.
 */
typedef struct rollback_cbdata {
	uint64_t	cb_create;
	boolean_t	cb_first;
	int		cb_doclones;
	char		*cb_target;
	int		cb_error;
	boolean_t	cb_recurse;
	boolean_t	cb_dependent;
} rollback_cbdata_t;

/*
 * Report any snapshots more recent than the one specified.  Used when '-r' is
 * not specified.  We reuse this same callback for the snapshot dependents - if
 * 'cb_dependent' is set, then this is a dependent and we should report it
 * without checking the transaction group.
 */
static int
rollback_check(zfs_handle_t *zhp, void *data)
{
	rollback_cbdata_t *cbp = data;

	if (cbp->cb_doclones) {
		zfs_close(zhp);
		return (0);
	}

	if (!cbp->cb_dependent) {
		if (strcmp(zfs_get_name(zhp), cbp->cb_target) != 0 &&
		    zfs_get_type(zhp) == ZFS_TYPE_SNAPSHOT &&
		    zfs_prop_get_int(zhp, ZFS_PROP_CREATETXG) >
		    cbp->cb_create) {

			if (cbp->cb_first && !cbp->cb_recurse) {
				(void) fprintf(stderr, gettext("cannot "
				    "rollback to '%s': more recent snapshots "
				    "exist\n"),
				    cbp->cb_target);
				(void) fprintf(stderr, gettext("use '-r' to "
				    "force deletion of the following "
				    "snapshots:\n"));
				cbp->cb_first = 0;
				cbp->cb_error = 1;
			}

			if (cbp->cb_recurse) {
				cbp->cb_dependent = B_TRUE;
				if (zfs_iter_dependents(zhp, B_TRUE,
				    rollback_check, cbp) != 0) {
					zfs_close(zhp);
					return (-1);
				}
				cbp->cb_dependent = B_FALSE;
			} else {
				(void) fprintf(stderr, "%s\n",
				    zfs_get_name(zhp));
			}
		}
	} else {
		if (cbp->cb_first && cbp->cb_recurse) {
			(void) fprintf(stderr, gettext("cannot rollback to "
			    "'%s': clones of previous snapshots exist\n"),
			    cbp->cb_target);
			(void) fprintf(stderr, gettext("use '-R' to "
			    "force deletion of the following clones and "
			    "dependents:\n"));
			cbp->cb_first = 0;
			cbp->cb_error = 1;
		}

		(void) fprintf(stderr, "%s\n", zfs_get_name(zhp));
	}

	zfs_close(zhp);
	return (0);
}

static int
zfs_do_rollback(int argc, char **argv)
{
	int ret;
	int c;
	boolean_t force = B_FALSE;
	rollback_cbdata_t cb = { 0 };
	zfs_handle_t *zhp, *snap;
	char parentname[ZFS_MAXNAMELEN];
	char *delim;

	/* check options */
	while ((c = getopt(argc, argv, "rRf")) != -1) {
		switch (c) {
		case 'r':
			cb.cb_recurse = 1;
			break;
		case 'R':
			cb.cb_recurse = 1;
			cb.cb_doclones = 1;
			break;
		case 'f':
			force = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing dataset argument\n"));
		usage();
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage();
	}

	/* open the snapshot */
	if ((snap = zfs_open(g_zfs, argv[0], ZFS_TYPE_SNAPSHOT)) == NULL)
		return (1);

	/* open the parent dataset */
	(void) strlcpy(parentname, argv[0], sizeof (parentname));
	verify((delim = strrchr(parentname, '@')) != NULL);
	*delim = '\0';
	if ((zhp = zfs_open(g_zfs, parentname, ZFS_TYPE_DATASET)) == NULL) {
		zfs_close(snap);
		return (1);
	}

	/*
	 * Check for more recent snapshots and/or clones based on the presence
	 * of '-r' and '-R'.
	 */
	cb.cb_target = argv[0];
	cb.cb_create = zfs_prop_get_int(snap, ZFS_PROP_CREATETXG);
	cb.cb_first = B_TRUE;
	cb.cb_error = 0;
	if ((ret = zfs_iter_children(zhp, rollback_check, &cb)) != 0)
		goto out;

	if ((ret = cb.cb_error) != 0)
		goto out;

	/*
	 * Rollback parent to the given snapshot.
	 */
	ret = zfs_rollback(zhp, snap, force);

out:
	zfs_close(snap);
	zfs_close(zhp);

	if (ret == 0)
		return (0);
	else
		return (1);
}

#define	SET_CB_UNSET	1

typedef struct set_cbdata {
	char		*cb_propname;
	char		*cb_value;
	uint_t		cb_flags;
} set_cbdata_t;

/*
 * returns B_FALSE if there are any properties other than "name"
 */
static boolean_t
share_has_name_only(nvlist_t *share)
{
	nvpair_t *nvp;

	for (nvp = nvlist_next_nvpair(share, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(share, nvp)) {
		if (strcmp(nvpair_name(nvp), SA_PROP_NAME) != 0)
			return (B_FALSE);
	}

	return (B_TRUE);
}

static void
print_share(nvlist_t *share)
{
	char *share_str;

	share_str = share_to_str(share);
	if (share_str != NULL) {
		(void) printf("%s\n", share_str);
		free(share_str);
	}
}

/*
 * zfs set [-c] share={property list} dataset
 *
 * INPUTS:
 *   zhp       - handle to zfs dataset
 *   share_val - share property string
 *   unset     - TRUE if -c flag is found
 */
int
do_set_share(zfs_handle_t *zhp, const char *share_val, int unset)
{
	char *sh_name;
	char *utf8_name;
	boolean_t new;
	nvlist_t *cur_share;
	nvlist_t *new_share;
	char mountpoint[ZFS_MAXPROPLEN];
	char errbuf[1024];

	if (zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) {
		(void) fprintf(stderr, gettext("not a filesystem\n"));
		return (1);
	}

	verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) == 0);

	if (strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) == 0) {
		(void) fprintf(stderr, gettext("cannot create shares on '%s': "
		    "legacy mountpoint\n"), zfs_get_name(zhp));
		(void) fprintf(stderr, gettext("use share(1M) to "
		    "share this filesystem\n"));
		return (1);
	}

	if (strcmp(mountpoint, ZFS_MOUNTPOINT_NONE) == 0) {
		(void) fprintf(stderr, gettext("cannot create shares on '%s': "
		    "no mountpoint set\n"), zfs_get_name(zhp));
		return (1);
	}

	if (!sa_path_in_current_zone(mountpoint)) {
		(void) fprintf(stderr, "%s: %s\n",
		    gettext("permission denied"),
		    sa_strerror(SA_SHARE_OTHERZONE));
		return (1);
	}

	/*
	 * parse properties into nvpairs/nvlists
	 * This will call libshare to parse the property list
	 */
	if (zfs_share_parse(zhp, share_val, unset, &new_share) != 0)
		return (1);

	if ((utf8_name = sa_share_get_name(new_share)) == NULL) {
		char *sh_path;

		/*
		 * The name property was not found, if the path is present
		 * then create a default name from the path and use that.
		 */
		if ((sh_path = sa_share_get_path(new_share)) == NULL) {
			(void) fprintf(stderr,
			    gettext("missing 'name' and 'path' properties\n"));
			sa_share_free(new_share);
			return (1);
		}

		/* must be absolute path */
		if (*sh_path != '/') {
			(void) fprintf(stderr, gettext("invalid path: %s\n"),
			    sh_path);
			sa_share_free(new_share);
			return (1);
		}

		/* create the default name */
		sh_name = safe_strdup(sh_path);
		sa_path_to_shr_name(sh_name);

		if (sa_locale_to_utf8(sh_name, &utf8_name) != SA_OK)
			utf8_name = safe_strdup(sh_name);
		/* add the default name to new_share */
		if (sa_share_set_name(new_share, utf8_name) != SA_OK) {
			(void) fprintf(stderr, gettext("internal error:"
			    " out of memory\n"));
			free(sh_name);
			free(utf8_name);
			exit(1);
		}
		free(sh_name);
		free(utf8_name);

		/* now get the share name from new_share */
		verify((utf8_name = sa_share_get_name(new_share)) != NULL);
	}

	if (sa_utf8_to_locale(utf8_name, &sh_name) != SA_OK)
		sh_name = safe_strdup(utf8_name);

	/*
	 * set -c share=name=shname
	 *
	 * With no other properties, this is a remove share command
	 */
	if (unset) {
		if (sa_share_get_path(new_share)) {
			/*
			 * If the unset flag is set, then remove the path from
			 * new_share. If the path were to remain in new_share,
			 * then this would become a request to remove the path
			 * from the disk share, which is not what we want.
			 */
			verify(nvlist_remove(new_share, SA_PROP_PATH,
			    DATA_TYPE_STRING) == 0);
		}

		if (share_has_name_only(new_share)) {
			if (zfs_share_remove(zhp, utf8_name) != 0) {
				sa_share_free(new_share);
				free(sh_name);
				return (1);
			} else {
				(void) printf(
				    gettext("share '%s' was removed.\n"),
				    sh_name);
				sa_share_free(new_share);
				free(sh_name);
				return (0);
			}
		}
	}

	/* does the share already exist? */
	if (zfs_share_read(zhp, utf8_name, &cur_share) == 0) {
		/*
		 * A share with utf8_name already exists
		 * merge properties prior to validating
		 */
		if (zfs_share_merge(zhp, cur_share, new_share, unset)
		    != 0) {
			sa_share_free(cur_share);
			sa_share_free(new_share);
			free(sh_name);
			return (1);
		}
		sa_share_free(new_share);
		new_share = cur_share;
		cur_share = NULL;
		new = B_FALSE;
	} else {
		/* share does not exist, nothing to unset */
		if (unset) {
			(void) fprintf(stderr,
			    gettext("share %s does not exist.\n"), sh_name);
			sa_share_free(new_share);
			free(sh_name);
			return (1);
		}
		new = B_TRUE;
	}
	free(sh_name);

	if (zfs_share_validate(zhp, new_share, new, errbuf,
	    sizeof (errbuf)) != 0) {
		(void) fprintf(stderr,
		    gettext("error validating share: %s\n"), errbuf);
		sa_share_free(new_share);
		return (1);
	}

	if (zfs_share_write(zhp, new_share) != 0) {
		sa_share_free(new_share);
		return (1);
	}

	print_share(new_share);

	sa_share_free(new_share);

	/*
	 * zfs_share_write will publish share if needed
	 */
	return (0);

}

static int
set_cb(zfs_handle_t *zhp, int depth, void *data)
{
	set_cbdata_t *cbp = data;
	zprop_setflags_t flags;

	if (strcmp(cbp->cb_propname, "share") == 0) {
		return (do_set_share(zhp, cbp->cb_value,
		    cbp->cb_flags &  SET_CB_UNSET));
	}

	flags = (depth > 0 ? ZPROP_SET_DESCENDANT : 0);

	if (zfs_prop_set_extended(zhp, cbp->cb_propname, cbp->cb_value,
	    flags) != 0) {
		switch (libzfs_errno(g_zfs)) {
		case EZFS_MOUNTFAILED:
			(void) fprintf(stderr, gettext("property may be set "
			    "but unable to remount filesystem\n"));
			break;
		case EZFS_SHAREFAILED:
			(void) fprintf(stderr, gettext("property may be set "
			    "but unable to reshare filesystem\n"));
			break;
		}
		return (1);
	}
	return (0);
}

static int
set_recurse_cb(zfs_handle_t *zhp, int depth, void *data)
{
	set_cbdata_t *cbp = data;
	zfs_prop_t prop = zfs_name_to_prop(cbp->cb_propname);

	/*
	 * If we're doing it recursively, then ignore properties that
	 * are not valid for this type of dataset.
	 */
	if (prop != ZPROP_INVAL &&
	    !zfs_prop_valid_for_type(prop, zfs_get_type(zhp)))
		return (0);

	return (set_cb(zhp, depth, data));
}

/*
 * zfs set [-r | -c] property=value { fs | snap | vol } ...
 *
 * Sets the given property for all datasets specified on the command line.
 * The -c option can only be used with share property.
 */
static int
zfs_do_set(int argc, char **argv)
{
	int c;
	zfs_iter_cb iter_cb;
	set_cbdata_t cb = { 0 };
	int ret;
	int flags = 0;

	/* check options */
	while ((c = getopt(argc, argv, "cr")) != -1) {
		switch (c) {
		case 'c':
			cb.cb_flags |= SET_CB_UNSET;
			break;
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			break;
		case '?':
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing property=value "
		    "argument\n"));
		usage();
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing dataset name\n"));
		usage();
	}

	/* validate property=value argument */
	cb.cb_propname = argv[0];
	if (((cb.cb_value = strchr(cb.cb_propname, '=')) == NULL) ||
	    (cb.cb_value[1] == '\0')) {
		(void) fprintf(stderr, gettext("missing value in "
		    "property=value argument\n"));
		usage();
	}

	*cb.cb_value = '\0';
	cb.cb_value++;

	if (*cb.cb_propname == '\0') {
		(void) fprintf(stderr,
		    gettext("missing property in property=value argument\n"));
		usage();
	}

	/*
	 * -r not valid with 'share'
	 */
	if ((flags & ZFS_ITER_RECURSE) &&
	    strcmp(cb.cb_propname, "share") == 0) {
		(void) fprintf(stderr, gettext("invalid option 'r'\n"));
		usage();
	}

	/*
	 * -c only valid with 'share'
	 */
	if ((cb.cb_flags & SET_CB_UNSET) &&
	    strcmp(cb.cb_propname, "share") != 0) {
		(void) fprintf(stderr, gettext("invalid option 'c'\n"));
		usage();
	}

	argc -= 1;
	argv += 1;

	iter_cb = ((flags & ZFS_ITER_RECURSE) ? set_recurse_cb : set_cb);
	ret = zfs_for_each(argc, argv, flags, ZFS_TYPE_DATASET, NULL, NULL, 0,
	    iter_cb, &cb);

	return (ret);
}

/*
 * zfs snapshot [-r] [-o prop=value] ... <fs@snap>
 *
 * Creates a snapshot with the given name.  While functionally equivalent to
 * 'zfs create', it is a separate command to differentiate intent.
 */
static int
zfs_do_snapshot(int argc, char **argv)
{
	boolean_t recursive = B_FALSE;
	int ret;
	char c;
	nvlist_t *props;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	/* check options */
	while ((c = getopt(argc, argv, "ro:")) != -1) {
		switch (c) {
		case 'o':
			if (parseprop(props))
				return (1);
			break;
		case 'r':
			recursive = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing snapshot argument\n"));
		goto usage;
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		goto usage;
	}

	ret = zfs_snapshot(g_zfs, argv[0], recursive, props);
	nvlist_free(props);
	if (ret && recursive)
		(void) fprintf(stderr, gettext("no snapshots were created\n"));
	return (ret != 0);

usage:
	nvlist_free(props);
	usage();
	return (-1);
}

/*
 * zfs send [-vDpb] -R [-i|-I <@snap>] <fs@snap>
 * zfs send [-vDpbc] -r [-i <@snap>] <fs@snap>
 * zfs send [-vDpb] [-i|-I <@snap>] <fs@snap>
 *
 * Send a backup stream to stdout.
 */
static int
zfs_do_send(int argc, char **argv)
{
	char *fromname = NULL;
	char *toname = NULL;
	char *cp;
	zfs_handle_t *zhp;
	sendflags_t flags = { 0 };
	int c, err;
	nvlist_t *dbgnv;
	boolean_t extraverbose = B_FALSE;
	boolean_t recursive = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, ":i:I:RDpbcrv")) != -1) {
		switch (c) {
		case 'i':
			if (fromname)
				usage();
			fromname = optarg;
			break;
		case 'I':
			if (fromname)
				usage();
			fromname = optarg;
			flags.doall = B_TRUE;
			break;
		case 'R':
			flags.replicate = B_TRUE;
			break;
		case 'r':
			recursive = B_TRUE;
			break;
		case 'p':
			flags.props = B_TRUE;
			break;
		case 'b':
			flags.received = B_TRUE;
			break;
		case 'c':
			flags.selfcont = B_TRUE;
			break;
		case 'v':
			if (flags.verbose)
				extraverbose = B_TRUE;
			flags.verbose = B_TRUE;
			break;
		case 'D':
			flags.dedup = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage();
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	/* Sanity check compatibility of -c with other options */
	if (flags.selfcont && !recursive) {
		/*
		 * NB: Do not make -c imply -r, as a future enhancement may
		 * make -Rc possible.
		 */
		(void) fprintf(stderr, gettext("option -c is only valid with "
		    "-r\n"));
		usage();
	}
	if (flags.selfcont && flags.doall) {
		(void) fprintf(stderr, gettext("option -I cannot be used with "
		    "-c\n"));
		usage();
	}
	if (recursive && flags.doall) {
		(void) fprintf(stderr, gettext("option -I cannot be used with "
		    "-r\n"));
		usage();
	}
	if (flags.replicate && recursive) {
		(void) fprintf(stderr, gettext("options -R and -r cannot be "
		    "used together\n"));
		usage();
	}

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing snapshot argument\n"));
		usage();
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage();
	}

	if (isatty(STDOUT_FILENO)) {
		(void) fprintf(stderr,
		    gettext("Error: Stream can not be written to a terminal.\n"
		    "You must redirect standard output.\n"));
		return (1);
	}

	cp = strchr(argv[0], '@');
	if (cp == NULL) {
		(void) fprintf(stderr,
		    gettext("argument must be a snapshot\n"));
		usage();
	}
	*cp = '\0';
	toname = cp + 1;
	zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zhp == NULL)
		return (1);

	/*
	 * If they specified the full path to the snapshot, chop off
	 * everything except the short name of the snapshot, but special
	 * case if they specify the origin.
	 */
	if (fromname && (cp = strchr(fromname, '@')) != NULL) {
		char origin[ZFS_MAXNAMELEN];
		zprop_source_t src;

		(void) zfs_prop_get(zhp, ZFS_PROP_ORIGIN,
		    origin, sizeof (origin), &src, NULL, 0, B_FALSE);

		if (strcmp(origin, fromname) == 0) {
			fromname = NULL;
			flags.fromorigin = B_TRUE;
		} else {
			*cp = '\0';
			if (cp != fromname && strcmp(argv[0], fromname)) {
				(void) fprintf(stderr,
				    gettext("incremental source must be "
				    "in same filesystem\n"));
				usage();
			}
			fromname = cp + 1;
			if (strchr(fromname, '@') || strchr(fromname, '/')) {
				(void) fprintf(stderr,
				    gettext("invalid incremental source\n"));
				usage();
			}
		}
	}

	if (flags.replicate && fromname == NULL)
		flags.doall = B_TRUE;
	else if (recursive)
		flags.replicate = B_TRUE;

	err = zfs_send(zhp, fromname, toname, flags, STDOUT_FILENO, NULL, 0,
	    extraverbose ? &dbgnv : NULL);

	if (extraverbose) {
		/*
		 * dump_nvlist prints to stdout, but that's been
		 * redirected to a file.  Make it print to stderr
		 * instead.
		 */
		(void) dup2(STDERR_FILENO, STDOUT_FILENO);
		dump_nvlist(dbgnv, 0);
		nvlist_free(dbgnv);
	}
	zfs_close(zhp);

	return (err != 0);
}

/*
 * zfs receive [-vnFu] [[-o prop=value] | [-x prop]] ... [-d | -e] <fs@snap>
 *
 * Restore a backup stream from stdin.
 */
static int
zfs_do_receive(int argc, char **argv)
{
	int c;
	recvflags_t flags = { 0 };
	nvlist_t *props;
	int err = 1;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	/* check options */
	while ((c = getopt(argc, argv, ":denuvFo:x:")) != -1) {
		switch (c) {
		case 'd':
			flags.isprefix = B_TRUE;
			break;
		case 'e':
			flags.isprefix = B_TRUE;
			flags.istail = B_TRUE;
			break;
		case 'n':
			flags.dryrun = B_TRUE;
			break;
		case 'u':
			flags.nomount = B_TRUE;
			break;
		case 'v':
			flags.verbose = B_TRUE;
			break;
		case 'F':
			flags.force = B_TRUE;
			break;
		case 'o':
			if (parseprop(props) != 0)
				goto out;
			break;
		case 'x':
			if (readpropname(props) != 0)
				goto out;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			goto usage;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing snapshot argument\n"));
		goto usage;
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		goto usage;
	}

	if (isatty(STDIN_FILENO)) {
		(void) fprintf(stderr,
		    gettext("Error: Backup stream can not be read "
		    "from a terminal.\n"
		    "You must redirect standard input.\n"));
		goto out;
	}

	err = zfs_receive(g_zfs, argv[0], flags, props, STDIN_FILENO, NULL);
out:
	nvlist_free(props);
	return (err != 0);
usage:
	nvlist_free(props);
	usage();
	return (-1);
}

static int
zfs_do_hold_rele_impl(int argc, char **argv, boolean_t holding)
{
	int errors = 0;
	int i;
	const char *tag;
	boolean_t recursive = B_FALSE;
	boolean_t temphold = B_FALSE;
	boolean_t allowdot = B_FALSE;
	const char *opts = holding ? "rtf" : "r";
	int c;

	/* check options */
	while ((c = getopt(argc, argv, opts)) != -1) {
		switch (c) {
		case 'f':
			allowdot = B_TRUE;
			break;
		case 'r':
			recursive = B_TRUE;
			break;
		case 't':
			temphold = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 2)
		usage();

	tag = argv[0];
	--argc;
	++argv;

	if (holding && tag[0] == '.' && !allowdot) {
		/* tags starting with '.' are reserved for libzfs */
		(void) fprintf(stderr, gettext("tag may not start with '.'\n"));
		usage();
	}

	for (i = 0; i < argc; ++i) {
		zfs_handle_t *zhp;
		char parent[ZFS_MAXNAMELEN];
		const char *delim;
		char *path = argv[i];

		delim = strchr(path, '@');
		if (delim == NULL) {
			(void) fprintf(stderr,
			    gettext("'%s' is not a snapshot\n"), path);
			++errors;
			continue;
		}
		(void) strncpy(parent, path, delim - path);
		parent[delim - path] = '\0';

		zhp = zfs_open(g_zfs, parent,
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
		if (zhp == NULL) {
			++errors;
			continue;
		}
		if (holding) {
			if (zfs_hold(zhp, delim+1, tag, recursive,
			    temphold, B_FALSE, -1, 0, 0) != 0)
				++errors;
		} else {
			if (zfs_release(zhp, delim+1, tag, recursive) != 0)
				++errors;
		}
		zfs_close(zhp);
	}

	return (errors != 0);
}

/*
 * zfs hold [-r] [-t] <tag> <snap> ...
 *
 *	-r	Recursively hold
 *	-t	Temporary hold (hidden option)
 *	-f	Allow holds beginning with '.' (hidden option)
 *
 * Apply a user-hold with the given tag to the list of snapshots.
 */
static int
zfs_do_hold(int argc, char **argv)
{
	return (zfs_do_hold_rele_impl(argc, argv, B_TRUE));
}

/*
 * zfs release [-r] <tag> <snap> ...
 *
 *	-r	Recursively release
 *
 * Release a user-hold with the given tag from the list of snapshots.
 */
static int
zfs_do_release(int argc, char **argv)
{
	return (zfs_do_hold_rele_impl(argc, argv, B_FALSE));
}

#define	CHECK_SPINNER 30
#define	SPINNER_TIME 3		/* seconds */
#define	MOUNT_TIME 5		/* seconds */

static int
get_one_dataset(zfs_handle_t *zhp, void *data)
{
	static char *spin[] = { "-", "\\", "|", "/" };
	static int spinval = 0;
	static int spincheck = 0;
	static time_t last_spin_time = (time_t)0;
	get_all_cb_t *cbp = data;
	zfs_type_t type = zfs_get_type(zhp);

	if (cbp->cb_verbose) {
		if (--spincheck < 0) {
			time_t now = time(NULL);
			if (last_spin_time + SPINNER_TIME < now) {
				update_progress(spin[spinval++ % 4]);
				last_spin_time = now;
			}
			spincheck = CHECK_SPINNER;
		}
	}

	/*
	 * Interate over any nested datasets.
	 */
	if (zfs_iter_filesystems(zhp, get_one_dataset, data) != 0) {
		zfs_close(zhp);
		return (1);
	}

	/*
	 * Skip any datasets whose type does not match.
	 */
	if ((type & ZFS_TYPE_FILESYSTEM) == 0) {
		zfs_close(zhp);
		return (0);
	}
	libzfs_add_handle(cbp, zhp);
	assert(cbp->cb_used <= cbp->cb_alloc);

	return (0);
}

static void
get_all_datasets(zfs_handle_t ***dslist, size_t *count, boolean_t verbose)
{
	get_all_cb_t cb = { 0 };
	cb.cb_verbose = verbose;
	cb.cb_getone = get_one_dataset;

	if (verbose)
		set_progress_header(gettext("Reading ZFS config"));
	(void) zfs_iter_root(g_zfs, get_one_dataset, &cb);

	*dslist = cb.cb_handles;
	*count = cb.cb_used;

	if (verbose)
		finish_progress(gettext("done."));
}

/*
 * Generic callback for sharing or mounting filesystems.  Because the code is so
 * similar, we have a common function with an extra parameter to determine which
 * mode we are using.
 */
#define	OP_SHARE	0x1
#define	OP_MOUNT	0x2

/*
 * Share or mount a dataset.
 */
static int
share_mount_one(zfs_handle_t *zhp, int op, int flags, char *protocol,
    boolean_t explicit, const char *options)
{
	char mountpoint[ZFS_MAXPROPLEN];
	char shareopts[ZFS_MAXPROPLEN];
	char smbshareopts[ZFS_MAXPROPLEN];
	const char *cmdname = op == OP_SHARE ? "share" : "mount";
	struct mnttab mnt;
	uint64_t zoned, canmount;
	boolean_t shared_nfs, shared_smb;
	boolean_t is_tmpmount = B_FALSE;
	char *tmpopts, *opts;

	assert(zfs_get_type(zhp) & ZFS_TYPE_FILESYSTEM);

	if (options != NULL) {
		tmpopts = strdup(options);
		opts = tmpopts;
		while (*opts != '\0') {
			static char *type_subopts[] = { "mountpoint", NULL };
			char *value;

			switch (getsubopt(&opts, type_subopts, &value)) {
				case 0:
					is_tmpmount = B_TRUE;
					break;
				}
		}
		free(tmpopts);
	}

	/*
	 * Check to make sure we can mount/share this dataset.  If we
	 * are in the global zone and the filesystem is exported to a
	 * local zone, or if we are in a local zone and the
	 * filesystem is not exported, then it is an error.
	 */
	zoned = zfs_prop_get_int(zhp, ZFS_PROP_ZONED);

	if (!is_tmpmount && zoned && getzoneid() == GLOBAL_ZONEID) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "dataset is exported to a local zone\n"), cmdname,
		    zfs_get_name(zhp));
		return (1);

	} else if (!zoned && getzoneid() != GLOBAL_ZONEID) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "permission denied\n"), cmdname,
		    zfs_get_name(zhp));
		return (1);
	}

	/*
	 * Ignore any filesystems which don't apply to us. This
	 * includes those with a legacy mountpoint, or those with
	 * legacy share options. We also have to ignore those that
	 * are encrypted that don't currently have their key available
	 * (but that check is done in zfs_is_mountable(), called from
	 * zfs_mount() below).
	 */

	verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) == 0);
	verify(zfs_prop_get(zhp, ZFS_PROP_SHARENFS, shareopts,
	    sizeof (shareopts), NULL, NULL, 0, B_FALSE) == 0);
	verify(zfs_prop_get(zhp, ZFS_PROP_SHARESMB, smbshareopts,
	    sizeof (smbshareopts), NULL, NULL, 0, B_FALSE) == 0);

	/*
	 * We cannot share or mount legacy filesystems. If the
	 * shareopts is non-legacy but the mountpoint is legacy, we
	 * treat it as a legacy share.
	 */
	if (strcmp(mountpoint, "legacy") == 0) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "legacy mountpoint\n"), cmdname, zfs_get_name(zhp));
		(void) fprintf(stderr, gettext("use %s(1M) to "
		    "%s this filesystem\n"), cmdname, cmdname);
		return (1);
	}

	if (op == OP_SHARE && strcmp(shareopts, "off") == 0 &&
	    strcmp(smbshareopts, "off") == 0) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot share '%s': "
		    "legacy share\n"), zfs_get_name(zhp));
		(void) fprintf(stderr, gettext("use share(1M) to "
		    "share this filesystem, or\n"));
		(void) fprintf(stderr, gettext("set the 'share' property and "
		    "set [sharenfs|sharesmb] property on\n"));
		return (1);
	}

	if (!is_tmpmount && strcmp(mountpoint, "none") == 0) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': no "
		    "mountpoint set\n"), cmdname, zfs_get_name(zhp));
		return (1);
	}

	/*
	 * canmount	explicit	outcome
	 * --------	--------	-------
	 * on		no		pass through
	 * on		yes		pass through
	 * off		no		return 0
	 * off		yes		display error, return 1
	 * noauto	no		return 0
	 * noauto	yes		pass through
	 */
	canmount = zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT);
	if (canmount == ZFS_CANMOUNT_OFF) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "'canmount' property is set to 'off'\n"), cmdname,
		    zfs_get_name(zhp));
		return (1);
	} else if (canmount == ZFS_CANMOUNT_NOAUTO && !explicit) {
		return (0);
	}

	/*
	 * At this point, we have verified that the mountpoint and/or
	 * shareopts are appropriate for auto management. If the
	 * filesystem is already mounted, return (failing
	 * for explicit requests); otherwise mount or share the
	 * filesystem.
	 */
	switch (op) {
	case OP_SHARE:

		shared_nfs = zfs_is_shared_nfs(zhp, NULL);
		shared_smb = zfs_is_shared_smb(zhp, NULL);

		if (shared_nfs && shared_smb ||
		    (shared_nfs && strcmp(shareopts, "off") != 0 &&
		    strcmp(smbshareopts, "off") == 0) ||
		    (shared_smb && strcmp(smbshareopts, "off") != 0 &&
		    strcmp(shareopts, "off") == 0)) {
			if (!explicit)
				return (0);

			(void) fprintf(stderr, gettext("cannot share "
			    "'%s': filesystem already shared\n"),
			    zfs_get_name(zhp));
			return (1);
		}

		if (!zfs_is_mounted(zhp, NULL) &&
		    zfs_mount(zhp, NULL, 0) != 0)
			return (1);

		if (protocol == NULL) {
			if (zfs_shareall(zhp) != 0)
				return (1);
		} else if (strcmp(protocol, "nfs") == 0) {
			if (zfs_share_nfs(zhp))
				return (1);
		} else if (strcmp(protocol, "smb") == 0) {
			if (zfs_share_smb(zhp))
				return (1);
		} else {
			(void) fprintf(stderr, gettext("cannot share "
			    "'%s': invalid share type '%s' "
			    "specified\n"),
			    zfs_get_name(zhp), protocol);
			return (1);
		}

		break;

	case OP_MOUNT:
		if (options == NULL)
			mnt.mnt_mntopts = "";
		else
			mnt.mnt_mntopts = (char *)options;

		if (!hasmntopt(&mnt, MNTOPT_REMOUNT) &&
		    zfs_is_mounted(zhp, NULL)) {
			if (!explicit)
				return (0);

			(void) fprintf(stderr, gettext("cannot mount "
			    "'%s': filesystem already mounted\n"),
			    zfs_get_name(zhp));
			return (1);
		}

		if (zfs_mount(zhp, options, flags) != 0)
			return (1);
		break;
	}

	return (0);
}

/*
 * Reports progress in the form "(current/total)".  Not thread-safe.
 */
static void
report_mount_progress(int current, int total)
{
	static time_t last_progress_time = 0;
	time_t now = time(NULL);
	char info[32];

	/* report 1..n instead of 0..n-1 */
	++current;

	/* display header if we're here for the first time */
	if (current == 1) {
		set_progress_header(gettext("Mounting ZFS filesystems"));
	} else if (current != total && last_progress_time + MOUNT_TIME >= now) {
		/* too soon to report again */
		return;
	}

	last_progress_time = now;

	(void) sprintf(info, "(%d/%d)", current, total);

	if (current == total)
		finish_progress(info);
	else
		update_progress(info);
}

static void
append_options(char *mntopts, char *newopts)
{
	int len = strlen(mntopts);

	/* original length plus new string to append plus 1 for the comma */
	if (len + 1 + strlen(newopts) >= MNT_LINE_MAX) {
		(void) fprintf(stderr, gettext("the opts argument for "
		    "'%c' option is too long (more than %d chars)\n"),
		    "-o", MNT_LINE_MAX);
		usage();
	}

	if (*mntopts)
		mntopts[len++] = ',';

	(void) strcpy(&mntopts[len], newopts);
}

static int
share_mount(int op, int argc, char **argv)
{
	int do_all = 0;
	boolean_t verbose = B_FALSE;
	boolean_t standby = B_FALSE;
	int c, ret = 0;
	char *options = NULL;
	int flags = 0;

	/* check options */
	while ((c = getopt(argc, argv, op == OP_MOUNT ? ":avo:OS" : "a"))
	    != -1) {
		switch (c) {
		case 'a':
			do_all = 1;
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		case 'o':
			if (*optarg == '\0') {
				(void) fprintf(stderr, gettext("empty mount "
				    "options (-o) specified\n"));
				usage();
			}

			if (options == NULL)
				options = safe_malloc(MNT_LINE_MAX + 1);

			/* option validation is done later */
			append_options(options, optarg);
			break;

		case 'O':
			flags |= MS_OVERLAY;
			break;
		case 'S':
			standby = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage();
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (standby) {
		if (options == NULL)
			options = safe_malloc(MNT_LINE_MAX + 1);
		append_options(options, "shadow=standby");
	}

	/* check number of arguments */
	if (do_all) {
		zfs_handle_t **dslist = NULL;
		size_t i, count = 0;
		char *protocol = NULL;

		if (op == OP_SHARE && argc > 0) {
			if (strcmp(argv[0], "nfs") != 0 &&
			    strcmp(argv[0], "smb") != 0) {
				(void) fprintf(stderr, gettext("share type "
				    "must be 'nfs' or 'smb'\n"));
				usage();
			}
			protocol = argv[0];
			argc--;
			argv++;
		}

		if (argc != 0) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage();
		}

		start_progress_timer();
		get_all_datasets(&dslist, &count, verbose);

		if (count == 0)
			return (0);

		qsort(dslist, count, sizeof (void *), libzfs_dataset_cmp);

		for (i = 0; i < count; i++) {
			if (verbose)
				report_mount_progress(i, count);

			if (share_mount_one(dslist[i], op, flags, protocol,
			    B_FALSE, options) != 0)
				ret = 1;
			zfs_close(dslist[i]);
		}

		free(dslist);
	} else if (argc == 0) {
		struct mnttab entry;

		if ((op == OP_SHARE) || (options != NULL)) {
			(void) fprintf(stderr, gettext("missing filesystem "
			    "argument (specify -a for all)\n"));
			usage();
		}

		/*
		 * When mount is given no arguments, go through /etc/mnttab and
		 * display any active ZFS mounts.  We hide any snapshots, since
		 * they are controlled automatically.
		 */
		rewind(mnttab_file);
		while (getmntent(mnttab_file, &entry) == 0) {
			if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0 ||
			    strchr(entry.mnt_special, '@') != NULL)
				continue;

			(void) printf("%-30s  %s\n", entry.mnt_special,
			    entry.mnt_mountp);
		}

	} else {
		zfs_handle_t *zhp;

		if (argc > 1) {
			(void) fprintf(stderr,
			    gettext("too many arguments\n"));
			usage();
		}

		if ((zhp = zfs_open(g_zfs, argv[0],
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			ret = 1;
		} else {
			ret = share_mount_one(zhp, op, flags, NULL, B_TRUE,
			    options);
			zfs_close(zhp);
		}
	}

	return (ret);
}

/*
 * zfs mount -a [nfs]
 * zfs mount filesystem
 *
 * Mount all filesystems, or mount the given filesystem.
 */
static int
zfs_do_mount(int argc, char **argv)
{
	return (share_mount(OP_MOUNT, argc, argv));
}

/*
 * zfs share -a [nfs | smb]
 * zfs share filesystem
 *
 * Share all filesystems, or share the given filesystem.
 */
static int
zfs_do_share(int argc, char **argv)
{
	return (share_mount(OP_SHARE, argc, argv));
}

typedef struct unshare_unmount_node {
	zfs_handle_t	*un_zhp;
	char		*un_mountp;
	uu_avl_node_t	un_avlnode;
} unshare_unmount_node_t;

/* ARGSUSED */
static int
unshare_unmount_compare(const void *larg, const void *rarg, void *unused)
{
	const unshare_unmount_node_t *l = larg;
	const unshare_unmount_node_t *r = rarg;

	return (strcmp(l->un_mountp, r->un_mountp));
}

/*
 * Convenience routine used by zfs_do_umount() and manual_unmount().  Given an
 * absolute path, find the entry /etc/mnttab, verify that its a ZFS filesystem,
 * and unmount it appropriately.
 */
static int
unshare_unmount_path(int op, char *path, int flags, boolean_t is_manual)
{
	zfs_handle_t *zhp;
	int ret;
	struct stat64 statbuf;
	struct extmnttab entry;
	const char *cmdname = (op == OP_SHARE) ? "unshare" : "unmount";
	ino_t path_inode;

	/*
	 * Search for the path in /etc/mnttab.  Rather than looking for the
	 * specific path, which can be fooled by non-standard paths (i.e. ".."
	 * or "//"), we stat() the path and search for the corresponding
	 * (major,minor) device pair.
	 */
	if (stat64(path, &statbuf) != 0) {
		(void) fprintf(stderr, gettext("cannot %s '%s': %s\n"),
		    cmdname, path, strerror(errno));
		return (1);
	}
	path_inode = statbuf.st_ino;

	/*
	 * Search for the given (major,minor) pair in the mount table.
	 */
	rewind(mnttab_file);
	while ((ret = getextmntent(mnttab_file, &entry, 0)) == 0) {
		if (entry.mnt_major == major(statbuf.st_dev) &&
		    entry.mnt_minor == minor(statbuf.st_dev))
			break;
	}
	if (ret != 0) {
		if (op == OP_SHARE) {
			(void) fprintf(stderr, gettext("cannot %s '%s': not "
			    "currently mounted\n"), cmdname, path);
			return (1);
		}
		(void) fprintf(stderr, gettext("warning: %s not in mnttab\n"),
		    path);
		if ((ret = umount2(path, flags)) != 0)
			(void) fprintf(stderr, gettext("%s: %s\n"), path,
			    strerror(errno));
		return (ret != 0);
	}

	if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0) {
		(void) fprintf(stderr, gettext("cannot %s '%s': not a ZFS "
		    "filesystem\n"), cmdname, path);
		return (1);
	}

	if ((zhp = zfs_open(g_zfs, entry.mnt_special,
	    ZFS_TYPE_FILESYSTEM)) == NULL)
		return (1);

	ret = 1;
	if (stat64(entry.mnt_mountp, &statbuf) != 0) {
		(void) fprintf(stderr, gettext("cannot %s '%s': %s\n"),
		    cmdname, path, strerror(errno));
		goto out;
	} else if (statbuf.st_ino != path_inode) {
		(void) fprintf(stderr, gettext("cannot "
		    "%s '%s': not a mountpoint\n"), cmdname, path);
		goto out;
	}

	if (op == OP_SHARE) {
		char nfsshare_prop[ZFS_MAXPROPLEN];
		char smbshare_prop[ZFS_MAXPROPLEN];

		verify(zfs_prop_get(zhp, ZFS_PROP_SHARENFS, nfsshare_prop,
		    sizeof (nfsshare_prop), NULL, NULL, 0, B_FALSE) == 0);
		verify(zfs_prop_get(zhp, ZFS_PROP_SHARESMB, smbshare_prop,
		    sizeof (smbshare_prop), NULL, NULL, 0, B_FALSE) == 0);

		if (strcmp(nfsshare_prop, "off") == 0 &&
		    strcmp(smbshare_prop, "off") == 0) {
			(void) fprintf(stderr, gettext("cannot unshare "
			    "'%s': legacy share\n"), path);
			(void) fprintf(stderr, gettext("use "
			    "unshare(1M) to unshare this filesystem\n"));
		} else if (!zfs_is_shared(zhp)) {
			(void) fprintf(stderr, gettext("cannot unshare "
			    "'%s': not currently shared\n"), path);
		} else {
			ret = zfs_unshareall_bypath(zhp, path);
		}
	} else {
		char mtpt_prop[ZFS_MAXPROPLEN];

		verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mtpt_prop,
		    sizeof (mtpt_prop), NULL, NULL, 0, B_FALSE) == 0);

		if (is_manual) {
			ret = zfs_unmount(zhp, NULL, flags);
		} else if (strcmp(mtpt_prop, "legacy") == 0) {
			(void) fprintf(stderr, gettext("cannot unmount "
			    "'%s': legacy mountpoint\n"),
			    zfs_get_name(zhp));
			(void) fprintf(stderr, gettext("use umount(1M) "
			    "to unmount this filesystem\n"));
		} else {
			ret = zfs_unmountall(zhp, flags);
		}
	}

out:
	zfs_close(zhp);

	return (ret != 0);
}

/*
 * Generic callback for unsharing or unmounting a filesystem.
 */
static int
unshare_unmount(int op, int argc, char **argv)
{
	int do_all = 0;
	int flags = 0;
	int ret = 0;
	int c;
	zfs_handle_t *zhp;
	char nfs_mnt_prop[ZFS_MAXPROPLEN];
	char sharesmb[ZFS_MAXPROPLEN];

	/* check options */
	while ((c = getopt(argc, argv, op == OP_SHARE ? "a" : "af")) != -1) {
		switch (c) {
		case 'a':
			do_all = 1;
			break;
		case 'f':
			flags = MS_FORCE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (do_all) {
		/*
		 * We could make use of zfs_for_each() to walk all datasets in
		 * the system, but this would be very inefficient, especially
		 * since we would have to linearly search /etc/mnttab for each
		 * one.  Instead, do one pass through /etc/mnttab looking for
		 * zfs entries and call zfs_unmount() for each one.
		 *
		 * Things get a little tricky if the administrator has created
		 * mountpoints beneath other ZFS filesystems.  In this case, we
		 * have to unmount the deepest filesystems first.  To accomplish
		 * this, we place all the mountpoints in an AVL tree sorted by
		 * the special type (dataset name), and walk the result in
		 * reverse to make sure to get any snapshots first.
		 */
		struct mnttab entry;
		uu_avl_pool_t *pool;
		uu_avl_t *tree;
		unshare_unmount_node_t *node;
		uu_avl_index_t idx;
		uu_avl_walk_t *walk;

		if (argc != 0) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage();
		}

		if (((pool = uu_avl_pool_create("unmount_pool",
		    sizeof (unshare_unmount_node_t),
		    offsetof(unshare_unmount_node_t, un_avlnode),
		    unshare_unmount_compare, UU_DEFAULT)) == NULL) ||
		    ((tree = uu_avl_create(pool, NULL, UU_DEFAULT)) == NULL))
			nomem();

		rewind(mnttab_file);
		while (getmntent(mnttab_file, &entry) == 0) {
			char *mountp;

			/* ignore non-ZFS entries */
			if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
				continue;

			/* ignore snapshots */
			if (strchr(entry.mnt_special, '@') != NULL)
				continue;

			if ((zhp = zfs_open(g_zfs, entry.mnt_special,
			    ZFS_TYPE_FILESYSTEM)) == NULL) {
				ret = 1;
				continue;
			}

			/*
			 * mnt_mountp points to a static area that gets
			 * overwritten on the next call to getmntent.
			 * Make a copy of the string before calling zfs_prop_get
			 * which may call getmntent.
			 */
			mountp = safe_strdup(entry.mnt_mountp);

			switch (op) {
			case OP_SHARE:
				verify(zfs_prop_get(zhp, ZFS_PROP_SHARENFS,
				    nfs_mnt_prop,
				    sizeof (nfs_mnt_prop),
				    NULL, NULL, 0, B_FALSE) == 0);
				if (strcmp(nfs_mnt_prop, "off") != 0)
					break;
				verify(zfs_prop_get(zhp, ZFS_PROP_SHARESMB,
				    nfs_mnt_prop,
				    sizeof (nfs_mnt_prop),
				    NULL, NULL, 0, B_FALSE) == 0);
				if (strcmp(nfs_mnt_prop, "off") == 0) {
					free(mountp);
					continue;
				}
				break;
			case OP_MOUNT:
				/* Ignore legacy mounts */
				verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT,
				    nfs_mnt_prop,
				    sizeof (nfs_mnt_prop),
				    NULL, NULL, 0, B_FALSE) == 0);
				if (strcmp(nfs_mnt_prop, "legacy") == 0) {
					free(mountp);
					continue;
				}
				/* Ignore canmount=noauto mounts */
				if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) ==
				    ZFS_CANMOUNT_NOAUTO) {
					free(mountp);
					continue;
				}
			default:
				break;
			}

			node = safe_malloc(sizeof (unshare_unmount_node_t));
			node->un_zhp = zhp;
			node->un_mountp = mountp;

			uu_avl_node_init(node, &node->un_avlnode, pool);

			if (uu_avl_find(tree, node, NULL, &idx) == NULL) {
				uu_avl_insert(tree, node, idx);
			} else {
				zfs_close(node->un_zhp);
				free(node->un_mountp);
				free(node);
			}
		}

		/*
		 * Walk the AVL tree in reverse, unmounting each filesystem and
		 * removing it from the AVL tree in the process.
		 */
		if ((walk = uu_avl_walk_start(tree,
		    UU_WALK_REVERSE | UU_WALK_ROBUST)) == NULL)
			nomem();

		while ((node = uu_avl_walk_next(walk)) != NULL) {
			uu_avl_remove(tree, node);

			switch (op) {
			case OP_SHARE:
				if (zfs_unshareall_bypath(node->un_zhp,
				    node->un_mountp) != 0)
					ret = 1;
				break;

			case OP_MOUNT:
				if (zfs_unmount(node->un_zhp,
				    node->un_mountp, flags) != 0)
					ret = 1;
				break;
			}

			zfs_close(node->un_zhp);
			free(node->un_mountp);
			free(node);
		}

		uu_avl_walk_end(walk);
		uu_avl_destroy(tree);
		uu_avl_pool_destroy(pool);

	} else {
		if (argc != 1) {
			if (argc == 0)
				(void) fprintf(stderr,
				    gettext("missing filesystem argument\n"));
			else
				(void) fprintf(stderr,
				    gettext("too many arguments\n"));
			usage();
		}

		/*
		 * We have an argument, but it may be a full path or a ZFS
		 * filesystem.  Pass full paths off to unmount_path() (shared by
		 * manual_unmount), otherwise open the filesystem and pass to
		 * zfs_unmount().
		 */
		if (argv[0][0] == '/')
			return (unshare_unmount_path(op, argv[0],
			    flags, B_FALSE));

		if ((zhp = zfs_open(g_zfs, argv[0],
		    ZFS_TYPE_FILESYSTEM)) == NULL)
			return (1);

		switch (op) {
		case OP_SHARE:
			verify(zfs_prop_get(zhp, ZFS_PROP_SHARENFS,
			    nfs_mnt_prop, sizeof (nfs_mnt_prop),
			    NULL, NULL, 0, B_FALSE) == 0);
			verify(zfs_prop_get(zhp, ZFS_PROP_SHARESMB,
			    sharesmb, sizeof (sharesmb), NULL, NULL,
			    0, B_FALSE) == 0);

			if (strcmp(nfs_mnt_prop, "off") == 0 &&
			    strcmp(sharesmb, "off") == 0) {
				(void) fprintf(stderr, gettext("cannot "
				    "unshare '%s': legacy share\n"),
				    zfs_get_name(zhp));
				(void) fprintf(stderr, gettext("use "
				    "unshare(1M) to unshare this "
				    "filesystem\n"));
				ret = 1;
			} else if (!zfs_is_shared(zhp)) {
				(void) fprintf(stderr, gettext("cannot "
				    "unshare '%s': not currently "
				    "shared\n"), zfs_get_name(zhp));
				ret = 1;
			} else if (zfs_unshareall(zhp) != 0) {
				ret = 1;
			}
			break;

		case OP_MOUNT:
			verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT,
			    nfs_mnt_prop, sizeof (nfs_mnt_prop),
			    NULL, NULL, 0, B_FALSE) == 0);

			if (strcmp(nfs_mnt_prop, "legacy") == 0) {
				(void) fprintf(stderr, gettext("cannot "
				    "unmount '%s': legacy "
				    "mountpoint\n"), zfs_get_name(zhp));
				(void) fprintf(stderr, gettext("use "
				    "umount(1M) to unmount this "
				    "filesystem\n"));
				ret = 1;
			} else if (!zfs_is_mounted(zhp, NULL)) {
				(void) fprintf(stderr, gettext("cannot "
				    "unmount '%s': not currently "
				    "mounted\n"),
				    zfs_get_name(zhp));
				ret = 1;
			} else if (zfs_unmountall(zhp, flags) != 0) {
				ret = 1;
			}
			break;
		}

		zfs_close(zhp);
	}

	return (ret);
}

/*
 * zfs unmount -a
 * zfs unmount filesystem
 *
 * Unmount all filesystems, or a specific ZFS filesystem.
 */
static int
zfs_do_unmount(int argc, char **argv)
{
	return (unshare_unmount(OP_MOUNT, argc, argv));
}

/*
 * zfs unshare -a
 * zfs unshare filesystem
 *
 * Unshare all filesystems, or a specific ZFS filesystem.
 */
static int
zfs_do_unshare(int argc, char **argv)
{
	return (unshare_unmount(OP_SHARE, argc, argv));
}

/* ARGSUSED */
static int
zfs_do_python(int argc, char **argv)
{
	(void) execv(pypath, argv-1);
	(void) printf("internal error: %s not found\n", pypath);
	return (-1);
}

/*
 * Called when invoked as /etc/fs/zfs/mount.  Do the mount if the mountpoint is
 * 'legacy'.  Otherwise, complain that use should be using 'zfs mount'.
 */
static int
manual_mount(int argc, char **argv)
{
	zfs_handle_t *zhp;
	char mountpoint[ZFS_MAXPROPLEN];
	char mntopts[MNT_LINE_MAX] = { '\0' };
	int ret;
	int c;
	int flags = 0;
	char *dataset, *path;

	/* check options */
	while ((c = getopt(argc, argv, ":mo:Oq")) != -1) {
		switch (c) {
		case 'o':
			(void) strlcpy(mntopts, optarg, sizeof (mntopts));
			break;
		case 'O':
			flags |= MS_OVERLAY;
			break;
		case 'm':
			flags |= MS_NOMNTTAB;
			break;
		/*
		 * Ignore "quiet" flag, since this function doesn't
		 * complain about invalid -o options.
		 */
		case 'q':
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage();
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			(void) fprintf(stderr, gettext("usage: mount [-o opts] "
			    "<path>\n"));
			return (2);
		}
	}

	argc -= optind;
	argv += optind;

	/* check that we only have two arguments */
	if (argc != 2) {
		if (argc == 0)
			(void) fprintf(stderr, gettext("missing dataset "
			    "argument\n"));
		else if (argc == 1)
			(void) fprintf(stderr,
			    gettext("missing mountpoint argument\n"));
		else
			(void) fprintf(stderr, gettext("too many arguments\n"));
		(void) fprintf(stderr, "usage: mount <dataset> <mountpoint>\n");
		return (2);
	}

	dataset = argv[0];
	path = argv[1];

	/* try to open the dataset */
	if ((zhp = zfs_open(g_zfs, dataset, ZFS_TYPE_FILESYSTEM)) == NULL)
		return (1);

	(void) zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE);

	/* check for legacy mountpoint and complain appropriately */
	ret = 0;
	if (strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) == 0) {
		if (mount(dataset, path, MS_OPTIONSTR | flags, MNTTYPE_ZFS,
		    NULL, 0, mntopts, sizeof (mntopts)) != 0) {
			(void) fprintf(stderr, gettext("mount failed: %s\n"),
			    strerror(errno));
			ret = 1;
		}
	} else {
		(void) fprintf(stderr, gettext("filesystem '%s' cannot be "
		    "mounted using 'mount -F zfs'\n"), dataset);
		(void) fprintf(stderr, gettext("Use 'zfs set mountpoint=%s' "
		    "instead.\n"), path);
		(void) fprintf(stderr, gettext("If you must use 'mount -F zfs' "
		    "or /etc/vfstab, use 'zfs set mountpoint=legacy'.\n"));
		(void) fprintf(stderr, gettext("See zfs(1M) for more "
		    "information.\n"));
		ret = 1;
	}

	return (ret);
}

/*
 * Called when invoked as /etc/fs/zfs/umount.  Unlike a manual mount, we allow
 * unmounts of non-legacy filesystems, as this is the dominant administrative
 * interface.
 */
static int
manual_unmount(int argc, char **argv)
{
	int flags = 0;
	int c;

	/* check options */
	while ((c = getopt(argc, argv, "f")) != -1) {
		switch (c) {
		case 'f':
			flags = MS_FORCE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			(void) fprintf(stderr, gettext("usage: unmount [-f] "
			    "<path>\n"));
			return (2);
		}
	}

	argc -= optind;
	argv += optind;

	/* check arguments */
	if (argc != 1) {
		if (argc == 0)
			(void) fprintf(stderr, gettext("missing path "
			    "argument\n"));
		else
			(void) fprintf(stderr, gettext("too many arguments\n"));
		(void) fprintf(stderr, gettext("usage: unmount [-f] <path>\n"));
		return (2);
	}

	return (unshare_unmount_path(OP_MOUNT, argv[0], flags, B_TRUE));
}


enum keycmd_e {
	KEY_NONE = 0,
	KEY_LOAD,
	KEY_UNLOAD,
	KEY_CHANGE,
	KEY_NEW
};

typedef struct key_cbdata {
	enum keycmd_e	keycmd;
	boolean_t	force;
	boolean_t	recurse;
	nvlist_t	*props;
} key_cbdata_t;

/*ARGSUSED*/
static int
zfs_key_callback(zfs_handle_t *zhp, int depth, void *data)
{
	key_cbdata_t *cb = data;
	int ret;

	/*
	 * Skip over non encrypted datasets when recursing, but other
	 * wise attempt the command so we get the correct error message.
	 */
	if (!zfs_is_encrypted(zhp) && cb->recurse) {
		return (0);
	}

	switch (cb->keycmd) {
	case KEY_LOAD:
		ret = zfs_key_load(zhp, B_TRUE, B_TRUE, cb->recurse);
		break;
	case KEY_UNLOAD:
		ret = zfs_key_unload(zhp, cb->force);
		break;
	case KEY_CHANGE:
		ret = zfs_key_change(zhp, cb->recurse, cb->props);
		break;
	case KEY_NEW:
		ret = zfs_key_new(zhp);
		break;
	default:
		abort();
	}

	if (cb->recurse && ret != 0) {
		return (0);
	}

	return (ret);
}

int
zfs_do_key(int argc, char **argv)
{
	int error = 1, options = 0;
	key_cbdata_t cb = { 0 };
	int flags = ZFS_ITER_ARGS_CAN_BE_PATHS;
	char c, *propname, *propval = NULL;
	zfs_prop_t zprop;
	boolean_t all = B_FALSE, cmdset = B_FALSE;
	char *strval;
	zfs_type_t types = ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME;

	while ((c = getopt(argc, argv, "arflucKo:")) != -1) {
		switch (c) {
		case 'a':
			all = B_TRUE;
			cb.recurse = B_TRUE;
			break;
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			cb.recurse = B_TRUE;
			break;
		case 'f':
			cb.force = B_TRUE;
			break;
		case 'l':
			if (cmdset)
				usage();
			cb.keycmd = KEY_LOAD;
			cmdset = B_TRUE;
			break;
		case 'u':
			if (cmdset)
				usage();
			cb.keycmd = KEY_UNLOAD;
			cmdset = B_TRUE;
			/*
			 * unload needs to get rid of the "auto"loaded
			 * snapshot keyrings as well.
			 */
			types = ZFS_TYPE_DATASET;
			break;
		case 'c':
			if (cmdset)
				usage();
			cb.keycmd = KEY_CHANGE;
			cmdset = B_TRUE;
			break;
		case 'K':
			if (cmdset)
				usage();
			cb.keycmd = KEY_NEW;
			cmdset = B_TRUE;
			break;
		case 'o':
			propname = optarg;
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) fprintf(stderr, gettext("missing "
				    "'=' for -o option\n"));
				goto error;
			}

			*propval = '\0';
			propval++;

			zprop = zfs_name_to_prop(propname);
			if (zprop != ZFS_PROP_KEYSOURCE) {
				(void) fprintf(stderr, gettext("Invalid "
				    "property for key operation: '%s'\n"),
				    propname);
				goto error;
			}

			if (cb.props == NULL &&
			    nvlist_alloc(&cb.props, NV_UNIQUE_NAME, 0) != 0) {
				(void) fprintf(stderr, gettext("internal "
				    "error: out of memory\n"));
				goto error;
			}

			if (nvlist_lookup_string(cb.props, propname,
			    &strval) == 0) {
				(void) fprintf(stderr, gettext("property '%s' "
				    "specified multiple times\n"), propname);
				goto error;
			}
			if (nvlist_add_string(cb.props, propname,
			    propval) != 0) {
				(void) fprintf(stderr, gettext("internal "
				    "error: out of memory\n"));
				goto error;
			}

			options += 2;
			break;

		case '?':
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if ((!all && !argc)) {
		usage();
		goto error;
	}

	if (all && argc) {
		usage();
		goto error;
	}

	if (cb.keycmd == KEY_NONE) {
		usage();
		goto error;
	}
	/* Key change is the only command that allows options */
	if (cb.keycmd != KEY_CHANGE && cb.props != NULL) {
		(void) fprintf(stderr, gettext(
		    "Property options only allowed during key change.\n"));
			usage();
			goto error;
	}

	if (cb.force && cb.keycmd != KEY_UNLOAD) {
		(void) fprintf(stderr,
		    gettext("Force flag only applies to key unload.\n"));
		usage();
		goto error;
	}

	error = zfs_for_each(argc, argv, flags, types, NULL, NULL, 0,
	    zfs_key_callback, &cb);

error:
	if (cb.props != NULL) {
		nvlist_free(cb.props);
	}
	return (error);
}

static int
parsediffprop(nvlist_t *props, char *propname)
{
	if (strchr(propname, '=') != NULL) {
		(void) fprintf(stderr, gettext(
		    "'=' not allowed with -o option\n"));
		return (-1);
	}
	if (nvlist_exists(props, propname)) {
		(void) fprintf(stderr, gettext("property '%s' "
		    "specified multiple times\n"), propname);
		return (-1);
	}
	if (nvlist_add_boolean(props, propname) != 0)
		nomem();
	return (0);
}

static int
zfs_do_diff(int argc, char **argv)
{
	zfs_handle_t *zhp;
	int flags = 0;
	char *tosnap = NULL;
	char *fromsnap = NULL;
	char *atp, *copy;
	nvlist_t *props = NULL;
	int err;
	int c;

	while ((c = getopt(argc, argv, "EeFHo:t")) != -1) {
		switch (c) {
		case 'E':
			flags |= ZFS_DIFF_BASE;
			break;
		case 'e':
			flags |= ZFS_DIFF_ENUMERATE;
			break;
		case 'F':
			flags |= ZFS_DIFF_CLASSIFY;
			break;
		case 'H':
			flags |= ZFS_DIFF_PARSEABLE;
			break;
		case 'o':
			if (props == NULL) {
				if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0)
				    != 0)
					nomem();
			}
			if (parsediffprop(props, optarg))
				return (1);
			break;
		case 't':
			flags |= ZFS_DIFF_TIMESTAMP;
			break;
		default:
			(void) fprintf(stderr,
			    gettext("invalid option '%c'\n"), optopt);
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr,
		gettext("must provide at least one snapshot name\n"));
		usage();
	}

	if (argc > 2 || ((flags & ZFS_DIFF_BASE) && argc > 1)) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage();
	}

	copy = NULL;
	if (flags & ZFS_DIFF_BASE) {
		tosnap = argv[0];
		if (*tosnap != '@')
			copy = strdup(tosnap);
	} else {
		fromsnap = argv[0];
		tosnap = (argc == 2) ? argv[1] : NULL;
		if (*fromsnap != '@')
			copy = strdup(fromsnap);
		else if (tosnap && *tosnap != '@')
			copy = strdup(tosnap);
	}
	if (copy == NULL)
		usage();
	if (fromsnap && tosnap && strcmp(fromsnap, tosnap) == 0) {
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
		    "No difference between a dataset and itself.\n"));
		free(copy);
		return (0);
	}

	if (atp = strchr(copy, '@'))
		*atp = '\0';

	zhp = zfs_open(g_zfs, copy, ZFS_TYPE_FILESYSTEM);
	free(copy);
	if (zhp == NULL)
		return (1);

	/*
	 * Ignore SIGPIPE so that the library can give us
	 * information on any failure
	 */
	(void) sigignore(SIGPIPE);

	err = zfs_show_diffs(zhp, STDOUT_FILENO, fromsnap, tosnap, props,
	    flags);

	zfs_close(zhp);

	return (err != 0);
}

int
main(int argc, char **argv)
{
	int ret;
	int i;
	char *cmdname;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	opterr = 0;

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, gettext("internal error: failed to "
		    "initialize ZFS library\n"));
		return (1);
	}

	zpool_set_history_str("zfs", argc, argv, history_str);
	verify(zpool_stage_history(g_zfs, history_str) == 0);

	libzfs_print_on_error(g_zfs, B_TRUE);

	if ((mnttab_file = fopen(MNTTAB, "r")) == NULL) {
		(void) fprintf(stderr, gettext("internal error: unable to "
		    "open %s\n"), MNTTAB);
		return (1);
	}

	/*
	 * This command also doubles as the /etc/fs mount and unmount program.
	 * Determine if we should take this behavior based on argv[0].
	 */
	progname = basename(argv[0]);
	if (strcmp(progname, "mount") == 0) {
		ret = manual_mount(argc, argv);
	} else if (strcmp(progname, "umount") == 0) {
		ret = manual_unmount(argc, argv);
	} else {
		/*
		 * Make sure the user has specified some command.
		 */
		if (argc < 2) {
			(void) fprintf(stderr, gettext("missing command\n"));
			usage();
		}

		cmdname = argv[1];

		/*
		 * Convert aliases and the special help command to the actual
		 * command names.
		 */
		if (strcmp(cmdname, "recv") == 0)
			cmdname = "receive";
		else if (strcmp(cmdname, "snap") == 0)
			cmdname = "snapshot";
		else if (strcmp(cmdname, "umount") == 0)
			cmdname = "unmount";
		else if (strcmp(cmdname, "-?") == 0)
			cmdname = "help";

		/*
		 * Run the appropriate command.
		 */
		libzfs_mnttab_cache(g_zfs, B_TRUE);
		if (zfs_command_find(cmdname, command_table, &i) == 0) {
			current_command = &command_table[i];
			ret = command_table[i].func(argc - 1, argv + 1);
		} else if (strchr(cmdname, '=') != NULL) {
			verify(zfs_command_find("set", command_table, &i) == 0);
			current_command = &command_table[i];
			ret = command_table[i].func(argc, argv);
		} else {
			(void) fprintf(stderr, gettext("unrecognized "
			    "command '%s'\n"), cmdname);
			usage();
		}
		libzfs_mnttab_cache(g_zfs, B_FALSE);
	}

	(void) fclose(mnttab_file);

	libzfs_fini(g_zfs);

	/*
	 * The 'ZFS_ABORT' environment variable causes us to dump core on exit
	 * for the purposes of running ::findleaks.
	 */
	if (getenv("ZFS_ABORT") != NULL) {
		(void) printf("dumping core by request\n");
		abort();
	}

	return (ret);
}

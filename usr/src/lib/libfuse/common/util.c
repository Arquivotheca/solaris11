/*
 * Fuse: Filesystem in Userspace
 *
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB
 */

#include "fuse.h"
#include "fuse_opt.h"
#include <libuvfs.h>
#include "fuse_impl.h"

#include <libintl.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <priv.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
	KEY_MOUNT_OPT,
};

static const struct fuse_opt fuse_mount_opts[] = {
	FUSE_OPT_KEY(MNTOPT_RO,			KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_RW,			KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_DEVICES,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NODEVICES,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_EXEC,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NOEXEC,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NBMAND,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NONBMAND,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_SETUID,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NOSETUID,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_SUID,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NOSUID,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_RSTCHOWN,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NORSTCHOWN,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_XATTR,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NOXATTR,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_ATIME,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NOATIME,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NOCTO,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_FORCEDIRECTIO,	KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NOFORCEDIRECTIO,	KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_DIRECTIO,		KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_DEFAULT_PERMS,	KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_ALLOW_OTHER,	KEY_MOUNT_OPT),
	FUSE_OPT_KEY(MNTOPT_NOALLOW_OTHER,	KEY_MOUNT_OPT),
	FUSE_OPT_KEY("max_read=",		KEY_MOUNT_OPT),
	FUSE_OPT_KEY("max_write=",		KEY_MOUNT_OPT),
	FUSE_OPT_KEY("max_dthreads=",		KEY_MOUNT_OPT),
	FUSE_OPT_END
};

#define	PACKAGE_VERSION	"2.7.4"

enum  {
	KEY_HELP,
	KEY_VERSION,
};

struct helper_opts {
	char *mountpoint;
};

#define	FUSE_HELPER_OPT(t, p) { t, offsetof(struct helper_opts, p), 1 }

static const struct fuse_opt fuse_helper_opts[] = {
	FUSE_OPT_KEY("-h",		KEY_HELP),
	FUSE_OPT_KEY("--help",		KEY_HELP),
	FUSE_OPT_KEY("-V",		KEY_VERSION),
	FUSE_OPT_KEY("--version",	KEY_VERSION),
	FUSE_OPT_END
};

/*
 * Non-failing strdup; see umem_alloc(3C).  Note that the nofail callback is
 * set in libfuse.
 */

char *
libfuse_strdup(const char *str)
{
	int size = strlen(str) + 1;
	char *rc;

	rc = umem_alloc(size, UMEM_NOFAIL);

	(void) strlcpy(rc, str, size);

	return (rc);
}

/*
 * Counterpart to libfuse_strdup().
 */

void
libfuse_strfree(char *str)
{
	int size = strlen(str) + 1;

	umem_free(str, size);
}

/*
 * Append an option to a buffer containing a comma-separated list of
 * options.  Return -1 on overflow, or 0 on success.
 */

static int
append_mount_opt(char *buffer, int bufsize, const char *opt)
{
	if ((buffer[0] != '\0') && (strlcat(buffer, ",", bufsize) >= bufsize))
		return (-1);

	if (strlcat(buffer, opt, bufsize) >= bufsize)
		return (-1);

	return (0);
}

/*
 * Process a mount option for fuse.  Returns zero if something is wrong,
 * one otherwise.  Called via the fuse_opt_parse framework.
 */

/*ARGSUSED*/
static int
fuse_mount_opt_proc(void *data, const char *arg, int key,
    struct fuse_args *outargs)
{
	const char *optionstr = arg;
	int rc;

	if (key != KEY_MOUNT_OPT)
		return (1);

	/*
	 * Convert "direct_io" into "forcedirectio"
	 */

	if (strcmp(optionstr, MNTOPT_DIRECTIO) == 0)
		optionstr = MNTOPT_FORCEDIRECTIO;

	rc = append_mount_opt(data, MAX_MNTOPT_STR, optionstr);
	if (rc == -1)
		return (0);

	return (1);
}

/*
 * Process argument according to fuse_opt_parse() and the fuse_mount_opts.
 */

int
fuse_mount_option_process(struct fuse_args *args, char *mount_opts)
{
	return (fuse_opt_parse(args, mount_opts, fuse_mount_opts,
	    fuse_mount_opt_proc));
}

static void
usage(const char *progname)
{
	(void) fprintf(stderr, gettext("usage: %s mountpoint [opts]\n\n"),
	    progname);
	(void) fprintf(stderr, gettext("general options:\n"));
	(void) fprintf(stderr, gettext("    -o opt,[opt...]        "
	    "mount options\n"));
	(void) fprintf(stderr, gettext("    -h   --help            "
	    "print help\n"));
	(void) fprintf(stderr, gettext("    -V   --version         "
	    "print version\n"));
	(void) fprintf(stderr, "\n");
}

static void
helper_version(void)
{
	(void) fprintf(stderr, gettext("FUSE library version: %s\n"),
	    PACKAGE_VERSION);
}

/*
 * Callback function for parsing the helper options via fuse_opt_parse().
 */

static int
fuse_helper_opt_proc(void *data, const char *arg, int key,
    struct fuse_args *outargs)
{
	struct helper_opts *hopts = data;

	switch (key) {
	case KEY_HELP:
		usage(outargs->argv[0]);
		return (fuse_opt_add_arg(outargs, "-h"));

	case KEY_VERSION:
		helper_version();
		return (1);

	case FUSE_OPT_KEY_NONOPT:
		if (!hopts->mountpoint) {
			char mountpoint[PATH_MAX];
			if (realpath(arg, mountpoint) == NULL) {
				(void) fprintf(stderr,
				    gettext("bad mount point "));
				(void) fprintf(stderr, gettext("%s: %s\n"), arg,
				    strerror(errno));
				return (-1);
			}
			return (fuse_opt_add_opt(&hopts->mountpoint,
			    mountpoint));
		} else {
			(void) fprintf(stderr, gettext("invalid arg %s\n"),
			    arg);
			return (-1);
		}

	default:
		return (1);
	}
}

/*
 * Parse the command line according to fuse_helper_opts.
 */

/*ARGSUSED*/
int
fuse_parse_cmdline(struct fuse_args *args, char **mountpoint,
    int *multithreaded, int *foreground)
{
	struct helper_opts hopts;
	int rc;

	(void) memset(&hopts, 0, sizeof (hopts));
	rc = fuse_opt_parse(args, &hopts, fuse_helper_opts,
	    fuse_helper_opt_proc);
	if (rc == -1)
		return (-1);

	if (mountpoint)
		*mountpoint = hopts.mountpoint;
	else
		libfuse_strfree(hopts.mountpoint);

	return (0);

err:
	libfuse_strfree(hopts.mountpoint);
	return (-1);
}

/*
 * Create the fuse_chan structure, containing the libuvfs_fs_t.
 */

struct fuse_chan *
fuse_uvfs_fs_create(const char *mountp, boolean_t is_smf)
{
	struct fuse_chan *fuse_chan;
	uint64_t fsid;

	if (is_smf)
		fsid = LIBUVFS_FSID_SVC;
	else {
		fsid = libuvfs_get_fsid(mountp);
		if (fsid == 0)
			return (0);
	}

	fuse_chan = umem_zalloc(sizeof (struct fuse_chan), UMEM_NOFAIL);
	fuse_chan->fuse_uvfs_fs = libuvfs_create_fs(LIBUVFS_VERSION, fsid);

	return (fuse_chan);
}

/*
 * Free a fuse_chan, releasing its underlying libuvfs_fs_t.
 */

static void
fuse_free_chan(struct fuse_chan *chan)
{
	libuvfs_destroy_fs(chan->fuse_uvfs_fs);
	umem_free(chan, sizeof (*chan));
}

/*
 * Perform another round of fuse_opt_parse, and call the mount(2) system
 * call with the derived options.  Returns a pointer to a fuse_chan
 * structure on success, or a NULL on error.
 */

struct fuse_chan *
fuse_mount(const char *mountp, struct fuse_args *args)
{
	struct fuse_chan *fuse_chan;
	char opts[BUFSIZ];

	opts[0] = '\0';

	if (fuse_mount_option_process(args, opts))
		return (NULL);

	if (!priv_ineffect(PRIV_SYS_MOUNT)) {
		(void) fprintf(stderr, gettext("insufficient privilege\n"));
		return (NULL);
	}

	if (mount(args->argv[0], mountp, MS_OPTIONSTR, "uvfs", NULL, 0,
	    opts, MAX_MNTOPT_STR) != 0)
		return (NULL);

	fuse_chan = fuse_uvfs_fs_create(mountp, 0);

	return (fuse_chan);
}

/*
 * Unmount, and free the fuse_chan structure.
 */

void
fuse_unmount(const char *mountpoint, struct fuse_chan *chan)
{
	fuse_free_chan(chan);
	(void) umount2(mountpoint, 0);
}

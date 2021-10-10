/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <limits.h>
#include <locale.h>
#include <fcntl.h>
#include <priv.h>
#include <errno.h>
#include <stdlib.h>
#include <libzfs.h>
#include <libnvpair.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mnttab.h>
#include <sys/time.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#include <sys/fs_reparse.h>
#include "libmig.h"

/*
 * MAKE_FSID64() yields a 64 bit number:
 * - High 32 bits contain 0x00FFFF00
 * - Low 32 bits contain the number passed in from user
 *
 * After zfs_mount() "cooks" the FSID, we get:
 * - High 32 bits contain the number passed in from user
 * - Low 32 bits contain 0xFFFF0000 ORed with the index into the vfssw[]
 *   table entry for zfs (which winds up in the low bytes)
 *
 * This makes it easy to see and keeps it unique from the typical zfs
 * FSID generation.
 */
#define	FSID_MAGIC		(uint64_t)0x00FFFF0000000000LL
#define	MAKE_FSID64(fsid32)	(FSID_MAGIC | (uint64_t)fsid32)


#define	DEBUG

extern	int	_nfssys(int, void *);
int
dest_clear(char *fs_path, char *dataset_name, mig_errsig_t *err_sig)
{
	char cmd[MAXPATHLEN];
	char buf[MAXPATHLEN];
	FILE *f = NULL;
	int err = 0;

	/* Check if the fs is shared at the destination */
	(void) snprintf(cmd, sizeof (cmd), "share | egrep ' %s '", fs_path);
	err = popen_cmd(cmd, "r", &f);
	if (err != 0) {
		(void) fprintf(stderr,
		    gettext("(%s) failed\n"), cmd);
		err_sig->mes_liberr = LIBERR_SHARECMD;
		goto out;
	}

	/* Unshare the fs at the destination  if shared */
	if (fgets(buf, sizeof (buf), f) != NULL) {

		/* set sharenfs=off on the dataset */
		(void) snprintf(cmd, sizeof (cmd), "zfs set sharenfs=off %s",
		    dataset_name);
		(void) printf(gettext("%s\n"), cmd);
		err = system(cmd);
		if (err != 0) {
			(void) fprintf(stderr,
			    gettext("(%s) failed\n"), cmd);
			err_sig->mes_liberr = LIBERR_UNSHARECMD;
			goto out;
		}
	}
	err = close_file(&f);
	if (err != 0) {
		err_sig->mes_liberr = LIBERR_FCLOSE;
		return (err);
	}

	/* Check if the fs is mounted at the destination */
	f = NULL;
	(void) snprintf(cmd, sizeof (cmd),
	    "zfs mount | egrep ' %s$'", fs_path);
	err = popen_cmd(cmd, "r", &f);
	if (err != 0) {
		(void) fprintf(stderr,
		    gettext("(%s) failed\n"), cmd);
		err_sig->mes_liberr = LIBERR_MOUNTCMD;
		return (err);
	}

	/* Unmount the fs at the destination */
	if (fgets(buf, sizeof (buf), f) != NULL) {
		(void) snprintf(cmd, sizeof (cmd), "umount %s", fs_path);
		(void) printf(gettext("%s\n"), cmd);
		err = system(cmd);
		if (err != 0) {
			(void) fprintf(stderr,
			    gettext("(%s) failed\n"), cmd);
			err_sig->mes_liberr = LIBERR_UMOUNTCMD;
			goto out;
		}
	}
out:
	if (f) {
		(void) close_file(&f);
	}

	return (err);
}

/*
 * Create a ZFS filesystem with a fixed FSID.  Also sets the "sharenfs"
 * to "on" and mounts/shares the filesystem.
 */
static int
zfscreate(char *fs_path, uint64_t fsid)
{
	libzfs_handle_t	*g_zfs;
	nvlist_t	*props = NULL;
	zfs_handle_t	*zhp = NULL;
	int		ret = 0;

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, gettext("internal error: failed to "
		    "initialize ZFS library\n"));
		ret = 1;
		goto out;
	}

	libzfs_print_on_error(g_zfs, B_TRUE);

	/* Set the "sharenfs" property "on" and pass it to zfs_create() */
	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_string(props,
	    zfs_prop_to_name(ZFS_PROP_SHARENFS), "on") != 0) {
		(void) fprintf(stderr, gettext("zfscreate: can't set "
		    "'sharenfs' property\n"));
		ret = 1;
		goto cleanup;
	}

	ret = zfs_create(g_zfs, fs_path, ZFS_TYPE_FILESYSTEM, props);
	if (ret)
		goto cleanup;

	ret = zfs_set_fsid(g_zfs, fs_path, fsid);
	if (ret)
		goto cleanup;

	if ((zhp = zfs_open(g_zfs, fs_path, ZFS_TYPE_DATASET)) == NULL)
		goto cleanup;

	/* Mount using only the defaults */
	if (zfs_mount(zhp, NULL, 0) != 0) {
		(void) fprintf(stderr, gettext("Filesystem successfully "
		    "created but not mounted\n"));
		ret = 1;
		goto cleanup;
	}

	/* Share the filesystem */
	if (zfs_share(zhp) != 0) {
		(void) fprintf(stderr, gettext("Filesystem successfully "
		    "created and mounted but not shared\n"));
		ret = 1;
	}

cleanup:
	if (zhp)
		zfs_close(zhp);

	if (props)
		nvlist_free(props);

	libzfs_fini(g_zfs);
out:
	return (ret);
}

/*
 * Format a junction which may be bound for a symlink or an xattr.
 * Returns a char * from reparse_unparse() to be freed by caller.
 */
char *
format_junction(char *destination, char *root, mig_errsig_t *err_sig)
{
	nvlist_t *nvl = NULL;
	char location[MAXPATHLEN], *text = NULL;
	int err;

	nvl = reparse_init();
	if (nvl == NULL) {
		err_sig->mes_liberr = LIBERR_REPARSE;
		return (NULL);
	}

	(void) snprintf(location, sizeof (location), "%s:%s", destination,
	    root);
	err = reparse_add(nvl, "nfs-basic", location);
	if (err) {
		reparse_free(nvl);
		err_sig->mes_liberr = LIBERR_REPARSE;
		return (NULL);
	}

	(void) reparse_unparse(nvl, &text);
	reparse_free(nvl);

	return (text);
}

/*
 * Create a referral at 'refer', pointing to 'server:/root'.
 * A referral present at 'refer' will be replaced atomically
 * via a rename(2).
 */
int
make_referral(char *refer, char *server, char *root, mig_errsig_t *err_sig)
{
	int ret = 0;
	char *text, tmpname[MAXPATHLEN];

	text = format_junction(server, root, err_sig);
	if (text == NULL) {
		(void) fprintf(stderr, gettext("can't format a referral\n"));
		return (1);
	}
	(void) snprintf(tmpname, sizeof (tmpname), "%sXXXXXX", refer);
	(void) mktemp(tmpname);
	ret = symlink(text, tmpname);
	free(text);
	if (ret != 0) {
		perror(gettext("can't create a referral"));
		err_sig->mes_syserr = errno;
		return (1);
	}
	ret = rename(tmpname, refer);
	if (ret != 0) {
		perror(gettext("can't rename a referral"));
		err_sig->mes_syserr = errno;
		(void) unlink(tmpname);
		return (1);
	}
	(void) fprintf(stderr, gettext("created a referral at %s\n"), root);

	return (0);
}

/*
 * Create a ZFS filesystem; the pool must exist.
 */
int
provision(char *root, char *fsidstr, mig_errsig_t *err_sig)
{
	char fspath[MAXPATHLEN], *s;
	char *endptr;
	FILE *f;
	struct mnttab m1, m2;
	int err = 0;
	uint64_t fsid64 = 0LL;

	s = strrchr(root, '/');
	if (s == NULL) {
		(void) fprintf(stderr, gettext("%s needs a path component\n"),
		    root);
		err_sig->mes_liberr = LIBERR_PATHNAME;
		return (1);
	}
	*s = '\0';

	if ((f = fopen("/etc/mnttab", "r")) == NULL) {
		perror(gettext("fopen /etc/mnttab failed"));
		err_sig->mes_syserr = errno;
		return (1);
	}

	bzero(&m1, sizeof (m1));
	m1.mnt_mountp = root;
	if (getmntany(f, &m2, &m1) != 0) {
		(void) fprintf(stderr, gettext("%s is not a mount point\n"),
		    root);
		(void) fclose(f);
		err_sig->mes_liberr = LIBERR_NOTMNTPT;
		return (1);
	}
	(void) fclose(f);

	if (strcmp(m2.mnt_fstype, "zfs") != 0) {
		(void) fprintf(stderr, gettext("%s is not a zfs file system\n"),
		    root);
		err_sig->mes_liberr = LIBERR_NOTZFS;
		return (1);
	}

	if (fsidstr != NULL) {
		uint32_t fsid32 = (uint32_t)strtoul(fsidstr, &endptr, 0);
		if (*endptr != (char)0) {
			(void) fprintf(stderr, gettext(
			    "Bad fsid '%s' - aborting\n"), fsidstr);
			err_sig->mes_liberr = LIBERR_BADFSID;
			return (1);
		}

		/*
		 * Take 32 bit fsid "seed" and add some flavor before
		 * passing it to zfscreate().  See comments for MAKE_FSID64()
		 * above.
		 */
		fsid64 = MAKE_FSID64(fsid32);
	}

	(void) snprintf(fspath,
	    sizeof (fspath), "%s/%s", m2.mnt_special, s + 1);

#ifdef DEBUG
	(void) printf(gettext(
	    "Creating (and sharing) zfs at %s with fsid: 0x%llx\n"),
	    fspath, fsid64);
#endif

	err = zfscreate(fspath, fsid64);
	if (err != 0) {
		err_sig->mes_liberr = LIBERR_ZFSCREATE;
		err = 1;
	}

	/* At some point, 'provision' will do more than simply create a zfs */
	return (err);
}

int
setfsid(char *fsname, uint64_t fsid, mig_errsig_t *err_sig)
{
	int err = 0;
	libzfs_handle_t	*g_zfs;

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, gettext("internal error: failed to "
		    "initialize ZFS library\n"));
		err = 1;
		goto out;
	}

	libzfs_print_on_error(g_zfs, B_TRUE);
	err = zfs_set_fsid(g_zfs, fsname, fsid);
	if (err)
		(void) fprintf(stderr, gettext(
		    "nfsmig/setfsid: libzfs error %d, \"%s\"\n"),
		    libzfs_errno(g_zfs), libzfs_error_description(g_zfs));
	libzfs_fini(g_zfs);
out:

	if (err != 0) {
		err_sig->mes_liberr = LIBERR_SETFSID;
		err = 1;
	}

	return (err);
}

int
resetfsid(char *fsname, uint64_t fsid, mig_errsig_t *err_sig)
{
	int err = 0;
	libzfs_handle_t	*g_zfs;

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, gettext("internal error: failed to "
		    "initialize ZFS library\n"));
		err = 1;
		goto out;
	}

	libzfs_print_on_error(g_zfs, B_TRUE);
	err = zfs_reset_fsid(g_zfs, fsname, fsid);
	if (err)
		(void) fprintf(stderr, gettext(
		    "nfsmig/resetfsid: libzfs error %d, \"%s\"\n"),
		    libzfs_errno(g_zfs), libzfs_error_description(g_zfs));
	libzfs_fini(g_zfs);
out:

	if (err != 0) {
		err_sig->mes_liberr = LIBERR_RESETFSID;
		err = 1;
	}

	return (err);
}

int
getfsid(char *fsname, uint64_t *fsidp, mig_errsig_t *err_sig)
{
	int err = 0;
	libzfs_handle_t	*g_zfs;

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, gettext("internal error: failed to "
		    "initialize ZFS library\n"));
		err = 1;
		goto out;
	}

	libzfs_print_on_error(g_zfs, B_TRUE);
	err = zfs_get_fsid(g_zfs, fsname, fsidp);
	if (err)
		(void) fprintf(stderr, gettext(
		    "nfsmig/getfsid: libzfs error %d, \"%s\"\n"),
		    libzfs_errno(g_zfs), libzfs_error_description(g_zfs));
	libzfs_fini(g_zfs);
out:

	if (err != 0) {
		err_sig->mes_liberr = LIBERR_GETFSID;
		err = 1;
	}

	return (err);
}

int
removefsid(char *fsname, uint64_t *fsidp, mig_errsig_t *err_sig)
{
	int err = 0;
	libzfs_handle_t	*g_zfs;

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, gettext("internal error: failed to "
		    "initialize ZFS library\n"));
		err = 1;
		goto out;
	}

	libzfs_print_on_error(g_zfs, B_TRUE);
	err = zfs_remove_fsid(g_zfs, fsname, fsidp);
	if (err)
		(void) fprintf(stderr, gettext(
		    "nfsmig/removefsid: libzfs error %d, \"%s\"\n"),
		    libzfs_errno(g_zfs), libzfs_error_description(g_zfs));
	libzfs_fini(g_zfs);
out:

	if (err != 0) {
		err_sig->mes_liberr = LIBERR_REMOVEFSID;
		err = 1;
	}

	return (err);
}

/*
 * Arrange to put a filesystem in a 'frozen' state to return DELAY
 */
int
freeze(char *root, mig_errsig_t *err_sig)
{
	int err = 0;
	struct nfs4mig_args ma;

	ma.root_name = root;
	ma.migerr =  &err_sig->mes_migerr;
	ma.mig_fsstat = &err_sig->mes_fsstat;

	err = _nfssys(NFS4_MIG_FREEZE, &ma);
	if (err != 0) {
		perror(gettext("nfssys failed"));
		err_sig->mes_syserr = errno;
		return (1);
	}
	if (err_sig->mes_migerr != MIG_OK) {
		return (1);
	}

	return (0);
}

/*
 * Arrange to put a file system into 'grace' permit state recovery
 */
int
grace(char *root, mig_errsig_t *err_sig)
{
	int err = 0;
	struct nfs4mig_args ma;

	ma.root_name = root;
	ma.migerr = &err_sig->mes_migerr;
	ma.mig_fsstat = &err_sig->mes_fsstat;

	err = _nfssys(NFS4_MIG_GRACE, &ma);
	if (err != 0) {
		perror(gettext("nfssys failed"));
		err_sig->mes_syserr = errno;
		return (1);
	}
	if (err_sig->mes_migerr != MIG_OK) {
		return (1);
	}

	return (0);
}

/*
 * Arrange to thaw a file system that was 'frozen'
 */
int
thaw(char *root, mig_errsig_t *err_sig)
{
	int err = 0;
	struct nfs4mig_args ma;

	ma.root_name = root;
	ma.migerr = &err_sig->mes_migerr;
	ma.mig_fsstat = &err_sig->mes_fsstat;

	err = _nfssys(NFS4_MIG_THAW, &ma);
	if (err != 0) {
		perror(gettext("nfssys failed"));
		err_sig->mes_syserr = errno;
		return (1);
	}
	if (err_sig->mes_migerr != MIG_OK) {
		return (1);
	}

	return (0);
}

/*
 * Arrange to harvest file system state
 */
int
harvest(char *root, mig_errsig_t *err_sig)
{
	int err = 0;
	struct nfs4mig_args ma;

	ma.root_name = root;
	ma.migerr = &err_sig->mes_migerr;
	ma.mig_fsstat = &err_sig->mes_fsstat;

	err = _nfssys(NFS4_MIG_HARVEST, &ma);
	if (err != 0) {
		perror(gettext("nfssys failed"));
		err_sig->mes_syserr = errno;
		return (1);
	}
	if (err_sig->mes_migerr != MIG_OK) {
		return (1);
	}

	return (0);
}

/*
 * Arrange to rehydrate harvested filesystem state
 */
int
hydrate(char *root, mig_errsig_t *err_sig)
{
	int err = 0;
	struct nfs4mig_args ma;

	ma.root_name = root;
	ma.migerr = &err_sig->mes_migerr;
	ma.mig_fsstat = &err_sig->mes_fsstat;

	err = _nfssys(NFS4_MIG_HYDRATE, &ma);
	if (err != 0) {
		perror(gettext("nfssys failed"));
		err_sig->mes_syserr = errno;
		return (1);
	}
	if (err_sig->mes_migerr != MIG_OK) {
		return (1);
	}

	return (0);
}

/*
 * Arrange to convert a source filesystem into a 'husk'
 */
int
convert(char *root, char *destination, mig_errsig_t *err_sig)
{
	int err = 0, fd = -1;
	char *text;
	struct nfs4mig_args ma;

	text = format_junction(destination, root, err_sig);
	if (text == NULL) {
		return (1);
	}

	fd = attropen(root, REFERRAL_EA, O_RDWR|O_CREAT, 0644);
	if (fd == -1) {
		perror(gettext("attropen failed"));
		err_sig->mes_syserr = errno;
		err = 1;
		goto out;
	}

	err = ftruncate(fd, 0);
	if (err == -1) {
		perror(gettext("ftruncate failed"));
		err_sig->mes_syserr = errno;
		err = 1;
		goto out;
	}

	err = write(fd, text, strlen(text));
	if (err == -1) {
		perror(gettext("write failed"));
		err_sig->mes_syserr = errno;
		err = 1;
		goto out;
	}

	ma.root_name = root;
	ma.migerr = &err_sig->mes_migerr;
	ma.mig_fsstat = &err_sig->mes_fsstat;

	err = _nfssys(NFS4_MIG_CONVERT, &ma);
	if (err != 0) {
		perror(gettext("nfssys failed"));
		err_sig->mes_syserr = errno;
		goto out;
	}
	if (err_sig->mes_migerr != MIG_OK) {
		goto out;
	}

out:
	if (text != NULL) {
		free(text);
		text = NULL;
	}
	if (fd != -1) {
		(void) close(fd);
	}
	return (err);
}

/*
 * Arrange to convert a source filesystem from a 'husk' back to normal
 */
int
unconvert(char *root, mig_errsig_t *err_sig)
{
	int fd, err = 0;
	struct nfs4mig_args ma;

	ma.root_name = root;
	ma.migerr = &err_sig->mes_migerr;
	ma.mig_fsstat = &err_sig->mes_fsstat;

	err = _nfssys(NFS4_MIG_UNCONVERT, &ma);
	if (err != 0) {
		perror(gettext("nfssys failed"));
		err_sig->mes_syserr = errno;
		return (1);
	}
	if (err_sig->mes_migerr != MIG_OK) {
		return (1);
	}

	fd = attropen(root, ".", O_RDONLY);
	if (fd == -1) {
		perror(gettext("attropen failed"));
		err_sig->mes_syserr = errno;
		return (1);
	}

	err = unlinkat(fd, REFERRAL_EA, 0);
	if (err != 0) {
		perror(gettext("unlinkat failed"));
		err_sig->mes_syserr = errno;
		err = 1;
	}
	(void) close(fd);

	return (err);
}

/*
 * Update location server (if we're acting from there).  Do
 * nothing if we're being run from the source server.
 * Limitation: uses 'root' for both data server and location server paths.
 */
int
update(char *root, char *destination, mig_errsig_t *err_sig)
{
	int ret;

	ret = make_referral(root, destination, root, err_sig);
	if (ret != 0) {
		return (1);
	}
	return (0);
}

int
clear(char *root, int dst_clup, mig_errsig_t *err_sig)
{
	uint32_t mig_fsstat = 0;
	int err = 0;
	char cmd[MAXPATHLEN];
	char buf[MAXPATHLEN];
	FILE *f = NULL;
	char *dataset_name = NULL, *tab = NULL;

	/*
	 * For destination cleanup, check if the file system exists at the
	 * destination. If not, then just bail out, no cleanup required.
	 */
	if (dst_clup == 1) {
		(void) snprintf(cmd, sizeof (cmd),
		    "/usr/sbin/zfs list -H | /usr/xpg4/bin/egrep "
		    "'[[:blank:]]%s$'", root);
		err = popen_cmd(cmd, "r", &f);
		if (err != 0) {
			err_sig->mes_liberr = LIBERR_ZFSLISTCMD;
			goto out;
		}
		if (fgets(buf, sizeof (buf), f) == NULL) {
			goto out;
		}

		/* Obtain the snapshot name from the zfs list output */
		if ((tab = strchr(buf, '\t')) == NULL) {
			err_sig->mes_liberr = LIBERR_PARSE;
			err = 1;
			goto out;
		}
		*tab = '\0';
		dataset_name = malloc(sizeof (buf));
		(void) strncpy(dataset_name, buf, sizeof (buf));

		err = close_file(&f);
		if (err != 0) {
			err_sig->mes_liberr = LIBERR_FCLOSE;
			goto out;
		}
	}

	/*
	 * Cleanup code common to both the source and the destination server.
	 * This involves thawing the file system if frozen.
	 */
	err = status(root, &mig_fsstat, err_sig);
	if (err != 0) {
		return (1);
	}

	if (mig_fsstat & FS_FROZEN) {
		(void) printf(gettext("thawing %s\n"), root);
		err = thaw(root, err_sig);
		if (err != 0) {
			return (1);
		}
	}

	/* Cleanup exclusive to the source server */
	if (dst_clup == 0) {

		/* Unconditional unconvert; ignore all errors */
		(void) printf(gettext("unconverting %s\n"), root);
		(void) unconvert(root, err_sig);
		err_sig->mes_migerr = 0;
		goto out;
	}

	/* Cleanup exclusive to the destination server */
	if (dst_clup == 1) {
		err = dest_clear(root, dataset_name, err_sig);
		if (err != 0) {
			goto out;
		}
	}
out:
	if (f) {
		(void) close_file(&f);
	}
	if (dataset_name != NULL) {
		free(dataset_name);
	}

	return (err);
}

int
status(char *root, uint32_t *mig_fsstat, mig_errsig_t *err_sig)
{
	struct nfs4mig_args ma;
	int err  = 0;

	ma.root_name = root;
	ma.migerr = &err_sig->mes_migerr;
	ma.mig_fsstat = &err_sig->mes_fsstat;

	err = _nfssys(NFS4_MIG_STATUS, &ma);
	if (err != 0) {
		perror(gettext("nfssys failed"));
		err_sig->mes_syserr = errno;
		return (1);
	}
	if (err_sig->mes_migerr != MIG_OK) {
		return (1);
	}

	*mig_fsstat = *ma.mig_fsstat;

	return (0);
}

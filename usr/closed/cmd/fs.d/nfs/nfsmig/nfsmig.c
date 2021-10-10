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
#include <assert.h>
#include <priv.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mnttab.h>
#include <sys/time.h>
#include <sys/list.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#include <sys/fs_reparse.h>
#include <sys/systeminfo.h>
#include <sys/utsname.h>
#include "libmig.h"

char snapname[MAXPATHLEN];
char hostname[MAXPATHLEN];
#define	RETRY_PERIOD	45
#define	MAX_TSM_RETRIES	3

#ifndef TEXT_DOMAIN
#define	TEXT_DOMAIN	"SUNW_OST_OSCMD"
#endif /* TEXT_DOMAIN */

#define	SNAP_DELIMITER	'@'

#define	DEBUG


int debug;
int dst = 0;

void
usage(void)
{
	(void) fprintf(stderr, gettext("Usage error:\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig provision [-f fsid] [source:]filesystem-path\n"));
	(void) fprintf(stderr,
	    gettext(
	    "nfsmig setfsid [-f fsid] [source:]{dataset|filesystem-path}\n"));
	(void) fprintf(stderr,
	    gettext(
	    "nfsmig resetfsid [-f fsid] [source:]{dataset|filesystem-path}\n"));
	(void) fprintf(stderr,
	    gettext(
	    "nfsmig getfsid [source:]{dataset|filesystem-path}\n"));
	(void) fprintf(stderr,
	    gettext(
	    "nfsmig removefsid [source:]{dataset|filesystem-path}\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig migrate [-T] [source:]filesystem-path destination"
	    " [locserver]\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig snap [source:]filesystem-path\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig send [-m] [source:]filesystem-path destination\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig freeze [source:]filesystem-path\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig grace [server:]filesystem-path\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig thaw [server:]filesystem-path\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig harvest [-T] [source:]filesystem-path\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig hydrate [-T] [destination:]filesystem-path"
	    " filesystem-name\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig convert [source:]filesystem-path destination\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig unconvert [source:]filesystem-path\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig update [source:]filesystem-path destination\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig clear [source:]filesystem-path [destination]"
	    " [locserver]\n"));
	(void) fprintf(stderr,
	    gettext("nfsmig status [src/dest:]filesystem-path\n"));
	exit(1);
}

/*
 * Send an nfsmig command to a remote server
 */
int
remote_cmd(char *server, char *command, char *opts, char *root,
    char *dest, char *loc, mig_errsig_t *err_sig)
{
	char cmd[MAXPATHLEN];
	int err = 0;

	(void) snprintf(cmd, sizeof (cmd),
	    "ssh -A root@%s nfsmig %s %s %s %s %s",
	    server, command, opts, root, dest, loc ? loc : "");

	(void) printf(gettext("sending %s request to host %s\n"),
	    command, server);
#ifdef DEBUG
	(void) printf("= %s\n", cmd);
#endif

	err = system(cmd);
	if (err != 0) {
		(void) fprintf(stderr, gettext("(%s) failed\n"), cmd);
		err_sig->mes_liberr = LIBERR_RECMDEXEC;
	}

	return (err);
}

int
popen_cmd(char *cmd, char *mode, FILE **f)
{
	int err = 0;
	char buf[MAXPATHLEN];

	if ((*f = popen(cmd, mode)) == NULL) {
		if (errno != 0) {
			(void) snprintf(buf, MAXPATHLEN,
			    "popen of %s failed", cmd);
			perror(gettext(buf));
		} else {
			(void) fprintf(stderr,
			    gettext("popen of %s failed\n"), cmd);
		}
		err = 1;
	}

	return (err);
}

int
close_file(FILE **f)
{
	int rc = 0;
	if (*f != NULL) {
		rc = pclose(*f);
		if (rc == -1) {
			perror(gettext("pclose failed"));
			return (1);
		}
		*f = NULL;
	}

	return (0);
}

/*
 * Given a mountpoint, retrieve the ZFS mnttab entry
 * Returns 0 on success, non-zero otherwise and may set err_sig->mes_liberr
 */
int
find_zfs_mntent(char *mntpt, struct mnttab *rmntp, mig_errsig_t *err_sig)
{
	struct mnttab mnt;
	FILE *f = NULL;
	int err = 0;

	if (rmntp == NULL) {
		(void) fprintf(stderr,
		    gettext("find_zfs_mntent: Null rmntp pointer\n"));
		err = 1;
		goto out;
	}

	if ((f = fopen("/etc/mnttab", "r")) == NULL) {
		perror(gettext("fopen /etc/mnttab failed"));
		err = 1;
		goto out;
	}

	bzero(&mnt, sizeof (mnt));
	mnt.mnt_mountp = mntpt;
	if (getmntany(f, rmntp, &mnt) != 0) {
		(void) fprintf(stderr, gettext("%s is not a mount point\n"),
		    mntpt);
		(void) fclose(f);
		err_sig->mes_liberr = LIBERR_NOTMNTPT;
		err = 1;
		goto out;
	}
	(void) fclose(f);

	if (strcmp(rmntp->mnt_fstype, "zfs") != 0) {
		(void) fprintf(stderr, gettext("%s is not a zfs file system\n"),
		    mntpt);
		err_sig->mes_liberr = LIBERR_NOTZFS;
		err = 1;
		goto out;
	}

out:
	return (err);
}

/*
 * Make an initial or subsequent snapshot
 */
int
zsnap(char *server, char *root, mig_errsig_t *err_sig)
{
	struct mnttab mnt;
	char cmd[MAXPATHLEN];

	if (server != NULL)
		return (remote_cmd(server, "snap", "", root, "", "", err_sig));

	if (find_zfs_mntent(root, &mnt, err_sig))
		return (1);

	(void) snprintf(snapname, sizeof (snapname),
	    "%s@nfsmigsnap-XXXXXX", mnt.mnt_special);

	(void) mktemp(snapname);
	(void) printf(gettext("creating snapshot %s\n"), snapname);

	(void) snprintf(cmd, sizeof (cmd), "zfs snapshot %s", snapname);
	if (system(cmd)) {
		(void) fprintf(stderr, gettext("(%s) failed\n"), cmd);
		return (1);
	}
	return (0);
}

typedef struct snap_list {
	list_t		sl_list;
	int		sl_count;
} snap_list_t;

typedef struct snap_node {
	list_node_t	sn_node;
	char		sn_name[MAXPATHLEN];
} snap_node_t;

static void
create_snap_list(snap_list_t *slp)
{
	list_create(&slp->sl_list, sizeof (snap_node_t), 0);
	slp->sl_count = 0;
}

static void
destroy_snap_list(snap_list_t *slp)
{
	snap_node_t *snp;

	while ((snp = list_head(&slp->sl_list)) != NULL) {
		list_remove(&slp->sl_list, snp);
		free(snp);
	}
	list_destroy(&slp->sl_list);
}

static int
get_snap_list(char *want, FILE *f, snap_list_t *slp)
{
	char buf[MAXPATHLEN];
	char *tab, *cp;
	snap_node_t *nu;
	int notmatch;

	/*
	 * We are looking for snapshots of the name
	 * POOLNAME/fsname@nfsmigsnap-TMPNAM, "want" is
	 * the zfs dataset name as returned from the mnttab.
	 */

#ifdef	DEBUG
	if (debug)
		(void) fprintf(stderr, "want: %s\n", want);
#endif

	while (fgets(buf, sizeof (buf), f)) {
		if ((tab = strchr(buf, '\t')) == NULL)
			continue;

		/* bash the tab, what remains is the name of the snapshot */
		*tab = '\0';

		/*
		 * Strip out the snapshot name from 'buf'
		 * to get just the dataset name.
		 */
		if ((cp = strchr(buf, SNAP_DELIMITER)) == NULL)
			continue;
		*cp = NULL;

		/*
		 * Compare the dataset name component of the snapshot
		 * against the zfs dataset name returned from mnttab.
		 */
		notmatch = strcmp(buf, want);
		*cp = SNAP_DELIMITER;	/* restore to full snapshot name */
		if (notmatch != 0) {
#ifdef	DEBUG
			if (debug)
				(void) fprintf(stderr,
				    "skipping snapshot %s\n", buf);
#endif
			continue;
		}

#ifdef	DEBUG
		if (debug)
			(void) fprintf(stderr, "found snapshot %s\n", buf);
#endif

		nu = calloc(1, sizeof (snap_node_t));
		if (nu == NULL) {
			perror(gettext("malloc failed"));
			return (1);
		}
		(void) strncpy(nu->sn_name, buf, sizeof (buf));
		list_insert_tail(&slp->sl_list, nu);
		slp->sl_count++;
	}
	return (0);
}

snap_node_t *
reconcile_snaplist(snap_list_t *llp, snap_node_t *prev, snap_list_t *rlp)
{
	snap_node_t *snp;

	/*
	 * The goal is to find a common ancestor present in both
	 * snapshot trains so that we can send an incremental between
	 * that one and the most recent snapshot.  We find that one
	 * by walking the list backward from prev, until we find one
	 * that is common.  For example, if the source has { foo, bar, baz}
	 * and the destination has { foo }, then we now want to send
	 * the incremental (foo->baz).
	 */
	do {
		for (snp = list_tail(&rlp->sl_list); snp != NULL;
		    snp = list_next(&rlp->sl_list, snp))
			if (strcmp(prev->sn_name, snp->sn_name) == 0)
				return (snp);
	} while ((prev = list_prev(&llp->sl_list, prev)) != NULL);
	return (NULL);
}

/*
 * Send an existing snapshot to a remote server
 */
int
zsend(char *server, char *root, char *destination, mig_errsig_t *err_sig)
{
	struct mnttab mnt;
	FILE *f;
	char cmd[MAXPATHLEN];
	char *zname;
	int error;
	snap_list_t local_snap_list, remote_snap_list;
	snap_node_t *tail, *prev, *remote_tail;
	time_t t0, t1;

	if (server != NULL)
		return (remote_cmd(server, "send", "",
		    root, destination, "", err_sig));

	if (find_zfs_mntent(root, &mnt, err_sig))
		return (1);

	zname = mnt.mnt_special;

#ifdef	DEBUG
	if (debug)
		(void) fprintf(stderr, "collecting local snapshot list\n");
#endif

	(void) snprintf(cmd, sizeof (cmd), "zfs list -Ht snapshot");
	if (popen_cmd(cmd, "r", &f)) {
		return (1);
	}
	create_snap_list(&local_snap_list);
	if (get_snap_list(zname, f, &local_snap_list)) {
		destroy_snap_list(&local_snap_list);
		(void) close_file(&f);
		return (1);
	}
	if ((close_file(&f)) != 0) {
		return (1);
	}

#ifdef	DEBUG
	if (debug)
		(void) fprintf(stderr, "collecting remote snapshot list\n");
#endif

	(void) snprintf(cmd, sizeof (cmd),
	    "ssh root@%s zfs list -Ht snapshot", destination);
	if (popen_cmd(cmd, "r", &f)) {
		err_sig->mes_liberr = LIBERR_RECMDEXEC;
		destroy_snap_list(&local_snap_list);
		return (1);
	}
	create_snap_list(&remote_snap_list);
	if (get_snap_list(zname, f, &remote_snap_list)) {
		destroy_snap_list(&local_snap_list);
		destroy_snap_list(&remote_snap_list);
		(void) close_file(&f);
		return (1);
	}
	if ((close_file(&f)) != 0) {
		return (1);
	}

	if (local_snap_list.sl_count == 0) {
		(void) fprintf(stderr, gettext("No snapshots detected in %s\n"),
		    root);
		destroy_snap_list(&local_snap_list);
		destroy_snap_list(&remote_snap_list);
		err_sig->mes_liberr = LIBERR_NOSNAPS;
		return (1);
	}

	if (local_snap_list.sl_count == 1 ||
	    remote_snap_list.sl_count == 0) {
		/*
		 * Only one snapshot at the source or no
		 * snapshots exist at the destination, so send
		 * a "full" stream.
		 * Use -p to propagate the attributes.
		 * Use -R to send all the user snapshots that exist
		 * prior our snapshot.
		 *
		 * zfs send -p snapname |
		 * ssh root@destination zfs receive -u filesystem"
		 */
		tail = list_tail(&local_snap_list.sl_list);
		(void) snprintf(cmd, sizeof (cmd),
		    "zfs send -Rp %s | ssh root@%s zfs receive -Fu %s",
		    tail->sn_name, destination, zname);

		(void) printf(gettext("sending snapshot %s\n"),
		    tail->sn_name);
	} else {
		/*
		 * Multiple snapshots, send the last one as an
		 * incremental to the previous one.
		 */
		tail = list_tail(&local_snap_list.sl_list);
		prev = list_prev(&local_snap_list.sl_list, tail);
		remote_tail = list_tail(&remote_snap_list.sl_list);

		/*
		 * Check to see that the previous snapshot on which
		 * the incremental was based exists at the server.  If
		 * not, we need to do a full send or resynchronize the
		 * incrementals.
		 */
		if (strcmp(prev->sn_name, remote_tail->sn_name)) {
#ifdef	DEBUG
			if (debug)
				(void) fprintf(stderr,
				    "Missing incremental %s at destination\n",
				    prev->sn_name);
#endif

			prev = reconcile_snaplist(&local_snap_list, prev,
			    &remote_snap_list);

			if (prev == NULL) {
				(void) fprintf(stderr,
				    gettext("Unable to reconcile snap list\n"));

				/*
				 * Wow, none of the snapshots anywhere in
				 * the list could be found at the destination,
				 * but some "nfsmig" snapshots exist there.
				 * This could happen if the source file system
				 * was deleted, but the destination was not.
				 * In this case, all we can do is delete
				 * the target file system and do a full
				 * send of the latest snapshot.
				 */

				destroy_snap_list(&local_snap_list);
				destroy_snap_list(&remote_snap_list);
				err_sig->mes_liberr = LIBERR_NOSNAPREC;
				return (1);
			}
		}

		(void) snprintf(cmd, sizeof (cmd),
		    "zfs send -pI %s %s | ssh root@%s zfs receive -Fu %s",
		    prev->sn_name, tail->sn_name, destination, zname);

		(void) printf(gettext("sending incremental snapshot %s\n"),
		    tail->sn_name);
	}

	/*
	 * send the snapshot to the destination
	 * Ideally, the user has their ssh key set in
	 * /root/.ssh/authorized_keys
	 */

#ifdef DEBUG
	(void) printf("= %s\n", cmd);
#endif
	t0 = time(0);
	if (system(cmd)) {
		(void) fprintf(stderr, gettext("%s failed\n"), cmd);
		error = 1;
	} else {
		t1 = time(0);
		(void) printf(gettext("%ld seconds\n"), t1-t0);
		error = 0;
	}
	destroy_snap_list(&local_snap_list);
	destroy_snap_list(&remote_snap_list);
	return (error);
}

int
tsm_set(bool_t tsm)
{
	char cmd[MAXPATHLEN];
	time_t t0, t1;

	(void) snprintf(cmd, sizeof (cmd),
	    "echo rfs4_do_tsm/W %d | mdb -kw", tsm);

#ifdef DEBUG
	(void) printf("= %s\n", cmd);
#endif
	t0 = time(0);
	if (system(cmd)) {
		(void) fprintf(stderr, "%s failed\n", cmd);
		return (1);
	}
	t1 = time(0);
	(void) printf("%ld seconds\n", t1-t0);
	return (0);
}

/*
 * Create a ZFS filesystem; the pool must exist.
 * Will update a local path with a referral if run on location server.
 * Limitation: uses 'root' for both data server and location server paths.
 */
int
provision2(char *server, char *root, char *fsid, mig_errsig_t *err_sig)
{
	if (server != NULL) {
		int ret;
		char fsidopt[32];	/* '-f {UINT32_MAX}' < 32 bytes */

		if (fsid != NULL)
			(void) snprintf(fsidopt, sizeof (fsidopt),
			    "-f %s", fsid);
		else
			fsidopt[0] = (char)0;	/* Empty string */

		ret = remote_cmd(server, "provision", fsidopt,
		    root, "", "", err_sig);
		if (ret != 0)
			return (1);
		ret = make_referral(root, server, root, err_sig);
		if (ret != 0)
			return (1);
		return (0);
	}

	return (provision(root, fsid, err_sig));
}

/*
 * Set a (new!) fixed FSID on a ZFS filesystem.
 */
int
setfsid2(char *server, char *path, char *fsidstr, mig_errsig_t *err_sig)
{
	uint64_t fsid = 0LL;
	char *dsname;		/* Name of ZFS dataset */
	struct mnttab m;
	struct utsname uts;
	extern int errno;

	if (server != NULL) {
		int ret;
		char fsidopt[32];	/* '-f {UINT32_MAX}' < 32 bytes */

		if (fsidstr)
			(void) snprintf(fsidopt, sizeof (fsidopt),
			    "-f %s", fsidstr);
		else
			fsidopt[0] = (char)0;	/* Empty string */

		ret = remote_cmd(server, "setfsid", fsidopt,
		    path, "", "", err_sig);
		if (ret != 0)
			goto err;
		return (0);
	}

	/* If necessary, convert mountpoint into dataset name */
	if (path[0] == '/') {
		if (find_zfs_mntent(path, &m, err_sig)) {
			goto err;
		}
		dsname = m.mnt_special;
	} else {
		dsname = path;
	}

	if (fsidstr) {
		errno = 0;
		fsid = strtoll(fsidstr, (char **)NULL, 0);
		if (fsid == 0LL) {
			if (errno == EINVAL) {
				(void) fprintf(stderr, gettext(
				    "fsid must be a non-zero integer\n"));
				goto err;
			} else if (errno != 0) {
				perror("strtoll");
				goto err;
			}
		}
	}

	if (uname(&uts) < 0) {
		perror("uname");
		goto err;
	}

	if (fsid == 0) {
		(void) printf(gettext("Using default FSID for %s on %s\n"),
		    dsname, uts.nodename);
	} else {
		(void) printf(gettext("Setting FSID for %s on %s: 0x%llx\n"),
		    dsname, uts.nodename, fsid);
	}

	return (setfsid(dsname, fsid, err_sig));
err:
	return (1);
}

/*
 * Reset an existing fixed FSID on a ZFS filesystem.
 */
int
resetfsid2(char *server, char *path, char *fsidstr, mig_errsig_t *err_sig)
{
	uint64_t fsid = 0LL;
	char *dsname;
	struct mnttab m;
	struct utsname uts;
	extern int errno;

	if (server != NULL) {
		int ret;
		char fsidopt[32];	/* '-f {UINT32_MAX}' < 32 bytes */

		if (fsidstr)
			(void) snprintf(fsidopt, sizeof (fsidopt),
			    "-f %s", fsidstr);
		else
			fsidopt[0] = (char)0;	/* Empty string */

		ret = remote_cmd(server, "resetfsid", fsidopt,
		    path, "", "", err_sig);
		if (ret != 0)
			goto err;
		return (0);
	}

	errno = 0;

	if (fsidstr) {
		fsid = strtoll(fsidstr, (char **)NULL, 0);
		if (fsid == 0LL) {
			if (errno == EINVAL)
				(void) fprintf(stderr,
				    gettext("fsid must be non-zero\n"));
			else
				perror("strtoll");
			goto err;
		}
	}

	/* If necessary, convert mountpoint into dataset name */
	if (path[0] == '/') {
		if (find_zfs_mntent(path, &m, err_sig)) {
			goto err;
		}
		dsname = m.mnt_special;
	} else {
		dsname = path;
	}

	if (uname(&uts) < 0) {
		perror("uname");
		goto err;
	}

	if (fsid)
		(void) printf(gettext(
		    "Resetting FSID for %s on %s: 0x%llx\n"),
		    dsname, uts.nodename, fsid);
	else
		(void) printf(gettext(
		    "Resetting FSID for %s on %s with system-selected FSID\n"),
		    dsname, uts.nodename);

	return (resetfsid(dsname, fsid, err_sig));
err:
	return (1);
}

/*
 * Retrieve the fixed FSID from a ZFS filesystem.
 */
int
getfsid2(char *svr, char *path, mig_errsig_t *err_sig)
{
	uint64_t fsid;
	char *dsname;
	struct mnttab m;
	struct utsname uts;
	int err = 0;

	if (svr != NULL) {
		int ret;

		ret = remote_cmd(svr, "getfsid", "", path, "", "", err_sig);
		if (ret != 0) {
			err = 1;
		}
		goto out;
	}

	/* If necessary, convert mountpoint into dataset name */
	if (path[0] == '/') {
		if (find_zfs_mntent(path, &m, err_sig)) {
			goto out;
		}
		dsname = m.mnt_special;
	} else {
		dsname = path;
	}

	if (getfsid(dsname, &fsid, err_sig) != 0) {
		err = 1;
		goto out;
	}

	if (uname(&uts) < 0) {
		perror("uname");
		err = 1;
		goto out;
	}

	(void) printf(gettext("Retrieved FSID for %s on %s: 0x%llx\n"),
	    dsname, uts.nodename, fsid);
out:
	return (err);
}

/*
 * Remove the fixed FSID from a ZFS filesystem.
 */
int
removefsid2(char *svr, char *path, mig_errsig_t *err_sig)
{
	uint64_t fsid;
	char *dsname;
	struct mnttab m;
	struct utsname uts;
	int err = 0;

	if (svr != NULL) {
		int ret;

		ret = remote_cmd(svr, "removefsid", "", path, "", "", err_sig);
		if (ret != 0) {
			err = 1;
		}
		goto out;
	}

	/* If necessary, convert mountpoint into dataset name */
	if (path[0] == '/') {
		if (find_zfs_mntent(path, &m, err_sig)) {
			goto out;
		}
		dsname = m.mnt_special;
	} else {
		dsname = path;
	}

	if (removefsid(dsname, &fsid, err_sig) != 0) {
		err = 1;
		goto out;
	}

	if (uname(&uts) < 0) {
		perror("uname");
		err = 1;
		goto out;
	}

	(void) printf(gettext("Removed FSID (0x%llx) for %s on %s \n"),
	    fsid, dsname, uts.nodename);
out:
	return (err);
}

/*
 * Arrange to put a file system in a 'frozen' state to return DELAY
 */
int
freeze2(char *src, char *root, mig_errsig_t *err_sig)
{
	if (src != NULL)
		return (remote_cmd(src, "freeze", "", root, "", "", err_sig));

	return (freeze(root, err_sig));
}

/*
 * Arrange to put a file system into 'grace' permit state recovery
 */
int
grace2(char *server, char *root, mig_errsig_t *err_sig)
{
	if (server != NULL)
		return (remote_cmd(server, "grace", "", root, "", "", err_sig));

	return (grace(root, err_sig));
}

/*
 * Arrange to thaw a file system that was 'frozen'
 */
int
thaw2(char *server, char *root, mig_errsig_t *err_sig)
{
	if (server != NULL)
		return (remote_cmd(server, "thaw", "", root, "", "", err_sig));

	return (thaw(root, err_sig));
}

/*
 * Arrange to harvest file system state
 */
int
harvest2(char *src, int tsm, char *root, mig_errsig_t *err_sig)
{
	if (src != NULL)
		return (remote_cmd(src, "harvest",
		    tsm == TRUE ? "" : "-T", root, "", "", err_sig));

	if (!tsm) {
		if ((tsm_set(FALSE)) != 0) {
			return (1);
		}
	} else {
		if ((tsm_set(TRUE)) != 0) {
			return (1);
		}
	}

	return (harvest(root, err_sig));
}

int
status2(char *server, char *root, uint32_t *mig_fsstat,
    mig_errsig_t *err_sig)
{
	if (server != NULL)
		return (remote_cmd(server, "status", "",
		    root, "", "", err_sig));

	return (status(root, mig_fsstat, err_sig));
}

static char *
get_kern_arch()
{
	char *buf = NULL;
	size_t bufsize = 1024;
	long ret;

	if ((buf = malloc(bufsize)) == NULL) {
		perror(gettext("malloc failed"));
		return (NULL);
	}

	ret = sysinfo(SI_ARCHITECTURE_K, buf, bufsize);
	if (ret == -1) {
		perror(gettext("sysinfo failed"));
		free(buf);
		return (NULL);
	}

	return (buf);
}



/*
 * Arrange to rehydrate harvested filesystem state
 */
int
hydrate2(char *dest, int tsm, char *root, char *fs_name, mig_errsig_t *err_sig)
{
	struct mnttab m1, m2;
	FILE *f;
	char *kern_arch = NULL;

	char cmd[MAXPATHLEN];
	time_t t0, t1;

	if (dest != NULL)
		return (remote_cmd(dest, "hydrate",
		    tsm == TRUE ? "" : "-T", root, fs_name, "", err_sig));

	/*
	 * Make sure it is mounted.
	 */
	if ((f = fopen("/etc/mnttab", "r")) == NULL) {
		perror(gettext("fopen /etc/mnttab failed"));
		return (1);
	}

	bzero(&m1, sizeof (m1));
	m1.mnt_mountp = root;
	if (getmntany(f, &m2, &m1) != 0) {
		(void) fclose(f);

		/*
		 * Not already mounted, so mount it!
		 */
		(void) snprintf(cmd, sizeof (cmd), "zfs mount %s", fs_name);
		(void) printf(gettext("mounting filesystem %s\n"), fs_name);
#ifdef DEBUG
		(void) printf(gettext("= %s\n"), cmd);
#endif

		t0 = time(0);
		if (system(cmd)) {
			(void) fprintf(stderr, gettext("%s failed\n"), cmd);
			return (1);
		}
		t1 = time(0);
		(void) printf(gettext("%ld seconds\n"), t1-t0);
	} else if (strcmp(m2.mnt_fstype, "zfs") != 0) {
		(void) fprintf(stderr, gettext("%s is not a zfs file system\n"),
		    root);
		(void) fclose(f);
		err_sig->mes_liberr = LIBERR_NOTZFS;
		return (1);
	} else
		(void) fclose(f);

	/*
	 * Load nfssrv module before calling freeze
	 */
	kern_arch = get_kern_arch();
	assert(kern_arch != NULL);
	if (kern_arch == NULL) {
		(void) fprintf(stderr,
		    gettext("Failed to obtain kernel ISA architecture\n"));
		return (1);
	}
	if (strcmp(kern_arch, "amd64") == 0)
		(void) snprintf(cmd, sizeof (cmd),
		    "modload -p misc/amd64/nfssrv");
	else if (strcmp(kern_arch, "i386") == 0)
		(void) snprintf(cmd, sizeof (cmd),
		    "modload -p misc/nfssrv");
	else if (strcmp(kern_arch, "sparcv9") == 0)
		(void) snprintf(cmd, sizeof (cmd),
		    "modload -p misc/sparcv9/nfssrv");
	else {
		(void) fprintf(stderr,
		    gettext("Unable to determine kernel ISA architecture\n"));
		free(kern_arch);
		err_sig->mes_liberr = LIBERR_UNDEFARCH;
		return (1);
	}
	free(kern_arch);

#ifdef DEBUG
	(void) printf("= %s\n", cmd);
#endif
	if (system(cmd)) {
		(void) fprintf(stderr, gettext("%s failed\n"), cmd);
		return (1);
	}

	/*
	 * Freeze it
	 */
	(void) printf(gettext("Freezing the file system before sharing\n"));
	if (freeze(root, err_sig))
		return (1);

	/*
	 * Inherit the sharenfs property from the receive stream
	 */
	(void) snprintf(cmd, sizeof (cmd), "zfs inherit -S sharenfs %s",
	    fs_name);
	(void) printf(gettext("Inheriting the sharenfs property"
	    " from the received stream %s\n"), fs_name);
#ifdef DEBUG
	(void) printf("= %s\n", cmd);
#endif
	t0 = time(0);
	if (system(cmd)) {
		(void) fprintf(stderr, gettext("%s failed\n"), cmd);
		return (1);
	}
	t1 = time(0);
	(void) printf(gettext("%ld seconds\n"), t1-t0);

	if (!tsm) {
		if ((tsm_set(FALSE)) != 0) {
			return (1);
		}
	} else {
		if ((tsm_set(TRUE)) != 0) {
			return (1);
		}
	}

	(void) printf(gettext("Doing the actual state hydrate\n"));
	return (hydrate(root, err_sig));
}

/*
 * thaw the fs if frozen
 */
int
thaw_if_frozen(char *server, char *fs_path, mig_errsig_t *err_sig)
{
	uint32_t mig_fsstat = 0;
	int err = 0;

	err = status2(server, fs_path, &mig_fsstat, err_sig);
	if (err == 0) {
		if (mig_fsstat & FS_FROZEN) {
			(void) printf(gettext("thawing %s:%s\n"),
			    server ? server : hostname, fs_path);
			err = thaw2(server, fs_path, err_sig);
			if (err == 0) {
				(void) printf(gettext("thaw done\n"));
			} else {
				(void) printf(gettext("thaw failed\n"));
				return (err);
			}
		} else {
			(void) printf(gettext("fs not frozen, "
			    "thaw not required\n"));
		}
	} else {
		(void) printf(gettext("Failed to obtain fs status "
		    "while retrying\n"));
		return (err);
	}

	return (err);
}

int
retry_tsm(char *command, mig_errsig_t *err_sig, int tsm,
    char *server, char *fs_path, char *dest)
{
	int tsm_retry_cnt = 0;
	int err = 0;

	while (err_sig->mes_migerr == MIGERR_INGRACE &&
	    (tsm_retry_cnt < MAX_TSM_RETRIES)) {
		(void) printf(gettext("Server in grace, will retry operation"
		    " in %d seconds...\n"), RETRY_PERIOD);
		(void) sleep(RETRY_PERIOD);

		if (strcmp(command, "harvest") == 0) {
			(void) printf(gettext("Retrying %s...\n"), command);
			err = harvest2(server, tsm, fs_path, err_sig);
		} else if (strcmp(command, "hydrate") == 0) {
			(void) printf(gettext("Retrying %s...\n"), command);

			/* thaw the fs at the destination and retry */
			if ((err =
			    thaw_if_frozen(server, fs_path, err_sig)) != 0) {
				return (err);
			}
			err = hydrate2(server, tsm, fs_path, dest, err_sig);
		} else {
			(void) printf(gettext("Unknown operation\n"));
			return (1);
		}
		tsm_retry_cnt++;
	}

	if (err != 0 && tsm_retry_cnt >= MAX_TSM_RETRIES) {
		(void) printf(gettext("Exceeded the number of retries"));
		return (1);
	}

	return (err);
}

/*
 * This can only be called from inside migrate() with server == NULL,
 * so it is always a local command.
 */
int
migrate_hydrate2(char *dest, int tsm, char *root, mig_errsig_t *err_sig)
{
	struct mnttab mnt;
	char *zname;
	int err = 0;

	if (find_zfs_mntent(root, &mnt, err_sig))
		return (1);

	zname = mnt.mnt_special;

	err = hydrate2(dest, tsm, root, zname, err_sig);
	if (err_sig->mes_migerr == MIGERR_INGRACE) {
		err = retry_tsm("hydrate", err_sig, tsm, root, zname, dest);
	}

	return (err);
}

/*
 * Arrange to convert a source filesystem into a 'husk'
 */
int
convert2(char *server, char *root, char *destination, mig_errsig_t *err_sig)
{
	if (server != NULL)
		return (remote_cmd(server, "convert", "",
		    root, destination, "", err_sig));

	return (convert(root, destination, err_sig));
}

/*
 * Arrange to convert a source filesystem from a 'husk' back to normal
 */
int
unconvert2(char *server, char *root, mig_errsig_t *err_sig)
{
	if (server != NULL)
		return (remote_cmd(server, "unconvert", "",
		    root, "", "", err_sig));

	return (unconvert(root, err_sig));
}


/*
 * Update location server with the correct location info.
 */
int
update2(char *server, char *root, char *destination, mig_errsig_t *err_sig)
{
	if (server != NULL)
		return (remote_cmd(server, "update", "",
		    root, destination, "", err_sig));

	return (update(root, destination, err_sig));
}

int
clear2(char *server, char *root, int src_or_dest, mig_errsig_t *err_sig)
{
	if (server != NULL && src_or_dest == DEST)
		return (remote_cmd(server, "clear", "-D",
		    root, "", "", err_sig));

	if (server != NULL)
		return (remote_cmd(server, "clear", "",
		    root, "", "", err_sig));

	return (clear(root, dst, err_sig));
}

char *
migerr_to_str(migerr_t error)
{
	switch (error) {
		case MIG_OK:
			return ("MIG_OK");
		case MIGERR_FSNOENT:
			return ("MIGERR_FSNOENT");
		case MIGERR_FSINVAL:
			return ("MIGERR_FSINVAL");
		case MIGERR_FSNOTFROZEN:
			return ("MIGERR_FSNOTFROZEN");
		case MIGERR_FSFROZEN:
			return ("MIGERR_FSFROZEN");
		case MIGERR_FSMOVED:
			return ("MIGERR_FSMOVED");
		case MIGERR_NOSTATE:
			return ("MIGERR_NOSTATE");
		case MIGERR_INGRACE:
			return ("MIGERR_INGRACE");
		case MIGERR_TSMFAIL:
			return ("MIGERR_TSMFAIL");
		case MIGERR_REPARSE:
			return ("MIGERR_REPARSE");
		case MIGERR_ALREADY:
			return ("MIGERR_ALREADY");
		case MIGERR_NGZONE:
			return ("MIGERR_NGZONE");
		case MIGERR_NONFSINST:
			return ("MIGERR_NONFSINST");
		case MIGERR_OP_ILLEGAL:
			return ("MIGERR_OP_ILLEGAL");
		default:
			return ("Unknown error");
	}
}

char *
liberr_to_str(liberr_t error)
{
	switch (error) {
		case LIB_OK:
			return ("LIB_OK");
		case LIBERR_PARSE:
			return ("LIBERR_PARSE");
		case LIBERR_ZFSCREATE:
			return ("LIBERR_ZFSCREATE");
		case LIBERR_SETFSID:
			return ("LIBERR_SETFSID");
		case LIBERR_RESETFSID:
			return ("LIBERR_RESETFSID");
		case LIBERR_GETFSID:
			return ("LIBERR_GETFSID");
		case LIBERR_REMOVEFSID:
			return ("LIBERR_REMOVEFSID");
		case LIBERR_NOTZFS:
			return ("LIBERR_NOTZFS");
		case LIBERR_REPARSE:
			return ("LIBERR_REPARSE");
		case LIBERR_NOTMNTPT:
			return ("LIBERR_NOTMNTPT");
		case LIBERR_NOSNAPS:
			return ("LIBERR_NOSNAPS");
		case LIBERR_NOSNAPREC:
			return ("LIBERR_NOSNAPREC");
		case LIBERR_UNDEFARCH:
			return ("LIBERR_UNDEFARCH");
		case LIBERR_PATHNAME:
			return ("LIBERR_PATHNAME");
		case LIBERR_BADFSID:
			return ("LIBERR_BADFSID");
		case LIBERR_RECMDEXEC:
			return ("LIBERR_RECMDEXEC");
		case LIBERR_ZFSLISTCMD:
			return ("LIBERR_ZFSLISTCMDEXEC");
		case LIBERR_SHARECMD:
			return ("LIBERR_SHARECMDEXEC");
		case LIBERR_UNSHARECMD:
			return ("LIBERR_UNSHARECMDEXEC");
		case LIBERR_MOUNTCMD:
			return ("LIBERR_UMOUNTCMDEXEC");
		case LIBERR_UMOUNTCMD:
			return ("LIBERR_UMOUNTCMDEXEC");
		case LIBERR_FCLOSE:
			return ("LIBERR_FCLOSE");
		case LIBERR_NESTED:
			return ("LIBERR_NESTED");
		default:
			return ("Unknown error");
	}
}

void
check_error(mig_errsig_t *err_sig)
{
	if (err_sig->mes_migerr != 0 ||
	    err_sig->mes_liberr != 0) {
		if (err_sig->mes_liberr == LIBERR_RECMDEXEC) {
			(void) printf(gettext("Failure on the destination\n"));
			if (!err_sig->mes_migerr) {
				return;
			}
		} else {
			(void) printf(gettext("FAILED\n"));
		}

		if (err_sig->mes_migerr != 0) {
			(void) printf(gettext("Migration module error: %s\n"),
			    migerr_to_str(err_sig->mes_migerr));
		}

		if (err_sig->mes_liberr != 0) {
			(void) printf(gettext("Migration library error: %s\n"),
			    liberr_to_str(err_sig->mes_liberr));
		}

		if (err_sig->mes_hint &&
		    (strnlen(err_sig->mes_hint, MAXPATHLEN) > 0)) {
			(void) printf(gettext("Hint: %s\n"),
			    err_sig->mes_hint);
		}
	}
}

int
check_nested_mounts(char *fs, mig_errsig_t *err_sig)
{
	struct mnttab mt;
	FILE *f;
	size_t len;
	int r = 0;

	if ((f = fopen("/etc/mnttab", "r")) == NULL) {
		perror(gettext("fopen /etc/mnttab failed"));
		return (1);
	}

	len = strlen(fs);
	while (getmntent(f, &mt) == 0)
		if (strncmp(fs, mt.mnt_mountp, len) == 0) {
			/*
			 * Ok, near match.  We have to differentiate
			 * these cases:
			 * fs: "/export/junk"
			 * mt: "/export/junk" - OK
			 * mt: "/export/junkx" - OK
			 * mt: "/export/junk/stuff" - BAD
			 */
			if (mt.mnt_mountp[len] == '/') {
				(void) fprintf(stderr, gettext(
				    "nested mounts detected: %s\n"),
				    mt.mnt_mountp);
				err_sig->mes_liberr = LIBERR_NESTED;
				r = 1;
				break;
			}
		}

	(void) fclose(f);
	return (r);
}

/*
 * Arrange a full migration sequence for a filesystem
 */
int
migrate(char *src, int tsm, char *root, char *dest,
    char *loc, mig_errsig_t *err_sig)
{
	/*
	 * migrate a file system to a new host, the game plan is:
	 * snap, send, freeze, harvest, snap, send, rehydrate, convert
	 * thaw local, thaw remote.  For now, don't iterate over the
	 * snap/send phase.
	 */
	int err = 0;

	if (src != NULL)
		return (remote_cmd(src, "migrate",
		    tsm == TRUE ? "" : "-T", root, dest, loc, err_sig));

	if ((err = check_nested_mounts(root, err_sig)) != 0)
		goto out;

	/* make an initial snapshot */
	(void) printf(gettext("initial snap of %s:%s\n"),
	    src ? src : hostname, root);
	err = zsnap(src, root, err_sig);
	if (err != 0) {
		(void) strncpy(err_sig->mes_hint, "zsnap  failed", MAXPATHLEN);
		goto out;
	}

	/* send it to the remote host */
	(void) printf(gettext("initial send of %s:%s->%s\n"),
	    src ? src : hostname, root, dest);
	err = zsend(src, root, dest, err_sig);
	if (err != 0) {
		(void) strncpy(err_sig->mes_hint, "zsend  failed", MAXPATHLEN);
		goto out;
	}

	(void) printf(gettext("freezing %s:%s\n"),
	    src ? src : hostname, root);
	err = freeze2(src, root, err_sig);
	if (err != 0) {
		(void) strncpy(err_sig->mes_hint, "freeze  failed", MAXPATHLEN);
		goto out;
	}

	(void) printf(gettext("harvest of %s:%s\n"),
	    src ? src : hostname, root);
	err = harvest2(src, tsm, root, err_sig);
	if (err_sig->mes_migerr == MIGERR_INGRACE) {
		err = retry_tsm("harvest", err_sig, tsm, src, root, NULL);
	}
	if (err != 0) {
		(void) strncpy(err_sig->mes_hint, "harvest  failed",
		    MAXPATHLEN);
		goto out;
	}

	/* take another snapshot */
	(void) printf(gettext("second snap of %s:%s\n"),
	    src ? src : hostname, root);
	err = zsnap(src, root, err_sig);
	if (err != 0) {
		(void) strncpy(err_sig->mes_hint, "zsnap  failed", MAXPATHLEN);
		goto out;
	}

	/* send it */
	(void) printf(gettext("second send of %s:%s->%s\n"),
	    src ? src : hostname, root, dest);
	err = zsend(src, root, dest, err_sig);
	if (err != 0) {
		(void) strncpy(err_sig->mes_hint, "zsend  failed", MAXPATHLEN);
		goto out;
	}

	/* rehydrate the state at the remote site */
	(void) printf(gettext("hydrate of %s:%s\n"), dest, root);
	err = migrate_hydrate2(dest, tsm, root, err_sig);
	if (err != 0) {
		(void) strncpy(err_sig->mes_hint, "hydrate failed", MAXPATHLEN);
		goto out;
	}

	(void) printf(gettext("convert %s:%s to husk\n"),
	    src ? src : hostname, root);
	err = convert2(src, root, dest, err_sig);
	if (err != 0) {
		(void) strncpy(err_sig->mes_hint, "convert failed", MAXPATHLEN);
		goto out;
	}

	(void) printf(gettext("thawing %s:%s\n"),
	    src ? src : hostname, root);
	err = thaw2(src, root, err_sig);
	if (err != 0) {
		(void) strncpy(err_sig->mes_hint, "thaw failed", MAXPATHLEN);
		goto out;
	}

	if (tsm != TRUE) {
		(void) printf(gettext("setting grace on %s:%s\n"), dest, root);
		err = grace2(dest, root, err_sig);
		if (err != 0) {
			(void) strncpy(err_sig->mes_hint, "grace failed",
			    MAXPATHLEN);
			goto out;
		}
	}

	(void) printf(gettext("thawing %s:%s\n"), dest, root);
	err = thaw2(dest, root, err_sig);
	if (err != 0) {
		(void) strncpy(err_sig->mes_hint, "thaw failed", MAXPATHLEN);
		goto out;
	}

out:
	return (err);
}



void
print_fsstatus(uint32_t mig_fsstat)
{
	if (mig_fsstat & FS_FROZEN) {
		if (mig_fsstat & FS_CONVERTED) {
			(void) printf(gettext("FROZEN and CONVERTED\n"));
		} else {
			(void) printf(gettext("FROZEN and UNCONVERTED\n"));
		}
	} else {
		/* File system is available */
		if (mig_fsstat & FS_CONVERTED) {
			(void) printf(gettext("AVAILABLE and CONVERTED\n"));
		} else {
			(void) printf(gettext("AVAILABLE and UNCONVERTED\n"));
		}
	}
}

int
main(int argc, char *argv[])
{
	char *command, *fs_path, *argument, *server, *s;
	char *dest = NULL;
	char *loc = NULL;
	char *updates = NULL;
	char *fsid = NULL;
	int err = 0, clup_err = 0;
	mig_errsig_t err_sig;
	uint32_t mig_fsstat = 0;
	int no_update = 0;
	int tsm = TRUE;
	int c;
	FILE *f = NULL;

	bzero(&err_sig, sizeof (mig_errsig_t));
	err_sig.mes_hint = calloc(1, MAXPATHLEN);
	if (err_sig.mes_hint == NULL) {
		perror(gettext("malloc failed"));
		err = 1;
		goto out;
	}

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing command\n"));
		usage();
	} else if (argc < 3) {
		(void) fprintf(stderr, gettext("too few arguments\n"));
		usage();
	}

	/* Consume program name */
	argv++;
	argc--;

	command = argv[0];

	/*
	 * check options, allow the -d to be used even in non-DEBUG
	 * mode so that scripts may use it without having to know.
	 * In non-DEBUG mode, -d produces no extra output.
	 */
	while ((c = getopt(argc, argv, "dDTf:")) != -1) {
		switch (c) {
		case 'd':
			debug = 1;
			break;
		case 'D':
			dst = 1;
			break;
		case 'T':
			tsm = FALSE;
			break;

		case 'f':
			if (strcmp(command, "provision") != 0 &&
			    strcmp(command, "setfsid") != 0 &&
			    strcmp(command, "resetfsid") != 0) {
				(void) fprintf(stderr,
				    gettext("'-f' option only valid for "
				    "provision, setfsid, and resetfsid "
				    "subcommands\n"));
				usage();
			}

			fsid = optarg;
			break;

		case '?':
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	(void) sysinfo(SI_HOSTNAME, hostname, sizeof (hostname) - 1);

	/*
	 * Some sanity testing that is generic to all commands
	 */
	if (argc == 0) {
		(void) fprintf(stderr, gettext("not enough arguments\n"));
		usage();
	} else if (argc > 3) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage();
	} else if (argc > 0) {
		dest = argv[1];

		if (argc > 1)
			loc = argv[2];
	}

	/*
	 * We now point to the argument to the command.
	 */
	argument = argv[0];

	/*
	 * Are we running locally or remotely?
	 * Remote means a first argument like "srchost:/path"
	 * All subcommands are capable of executing remotely.
	 */
	s = strchr(argument, ':');
	if (s != NULL) {
		*s = '\0';
		server = strdup(argument);
		fs_path = s + 1;
		*s = ':';
	} else {
		server = NULL;
		fs_path = argument;
	}

	(void) printf("server = %s and path = %s\n", server, fs_path);

	if (strcmp(command, "provision") == 0) {

		if (dest != NULL)
			usage();

		(void) printf(gettext("%s provisioning %s\n"),
		    hostname, argument);
		err = provision2(server, fs_path, fsid, &err_sig);

	} else if (strcmp(command, "setfsid") == 0) {

		if (dest != NULL)
			usage();

		if (fsid == NULL)
			(void) printf(gettext(
			    "%s setting default fsid on dataset %s\n"),
			    hostname, fs_path);
		else
			(void) printf(gettext(
			    "%s setting fsid %s on dataset %s\n"),
			    hostname, fsid, fs_path);

		err = setfsid2(server, fs_path, fsid, &err_sig);

	} else if (strcmp(command, "resetfsid") == 0) {

		if (dest != NULL)
			usage();

		if (fsid == NULL)
			(void) printf(gettext(
			    "%s setting system-selected fsid on dataset %s\n"),
			    hostname, fs_path);
		else
			(void) printf(gettext(
			    "%s setting fsid %s on dataset %s\n"),
			    hostname, fsid, fs_path);

		err = resetfsid2(server, fs_path, fsid, &err_sig);

	} else if (strcmp(command, "getfsid") == 0) {

		if (dest != NULL)
			usage();

		/* getfsid2() will do the printing */
		err = getfsid2(server, fs_path, &err_sig);

	} else if (strcmp(command, "removefsid") == 0) {

		if (dest != NULL)
			usage();

		/* removefsid2() will do the printing */
		err = removefsid2(server, fs_path, &err_sig);

	} else if (strcmp(command, "freeze") == 0) {

		if (dest != NULL)
			usage();

		(void) printf(gettext("%s freezing %s\n"), hostname, argument);
		err = freeze2(server, fs_path, &err_sig);

	} else if (strcmp(command, "grace") == 0) {

		if (dest != NULL)
			usage();

		(void) printf(gettext("%s gracing %s\n"), hostname, argument);
		err = grace2(server, fs_path, &err_sig);

	} else if (strcmp(command, "thaw") == 0) {

		if (dest != NULL)
			usage();

		(void) printf(gettext("%s thawing %s\n"), hostname, argument);
		err = thaw2(server, fs_path, &err_sig);

	} else if (strcmp(command, "harvest") == 0) {

		if (dest != NULL)
			usage();

		(void) printf(gettext("%s harvesting %s\n"),
		    hostname, argument);
		err = harvest2(server, tsm, fs_path, &err_sig);

		if (err_sig.mes_migerr == MIGERR_INGRACE) {
			err = retry_tsm(command, &err_sig, tsm, server,
			    fs_path, NULL);
		}

	} else if (strcmp(command, "hydrate") == 0) {

		if (dest == NULL || loc != NULL)
			usage();

		(void) printf(gettext("%s hydrating %s\n"), hostname, argument);

		/*
		 * For hydrate, dest == fs_name.
		 */
		err = hydrate2(server, tsm, fs_path, dest, &err_sig);

		if (err_sig.mes_migerr == MIGERR_INGRACE) {
			err = retry_tsm(command, &err_sig, tsm, server,
			    fs_path, dest);
		}

	} else if (strcmp(command, "convert") == 0) {

		if (dest == NULL || loc != NULL)
			usage();

		(void) printf(gettext("%s converting %s\n"),
		    hostname, argument);
		err = convert2(server, fs_path, dest, &err_sig);

	} else if (strcmp(command, "unconvert") == 0) {
		if (loc != NULL)
			usage();

		(void) printf(gettext("%s unconverting %s\n"),
		    hostname, argument);
		err = unconvert2(server, fs_path, &err_sig);

	} else if (strcmp(command, "snap") == 0) {

		if (dest != NULL)
			usage();

		(void) printf(gettext("%s snapping %s\n"), hostname, argument);
		err = zsnap(server, fs_path, &err_sig);

	} else if (strcmp(command, "send") == 0) {

		if (dest == NULL || loc != NULL)
			usage();

		(void) printf(gettext("%s sending %s\n"), hostname, argument);
		err = zsend(server, fs_path, dest, &err_sig);

	} else if (strcmp(command, "update") == 0) {

		if (dest == NULL || loc != NULL)
			usage();

		(void) printf(gettext("%s updating %s\n"), hostname, argument);
		err = update2(server, fs_path, dest, &err_sig);

	} else if (strcmp(command, "migrate") == 0) {

		/*
		 * We need to think about a location server to update (loc).
		 *
		 * "nfsmig migrate /fs dest" - local mig, no update
		 * (since you're running from the source server and are
		 * not pointed to the location server).  This is how a
		 * remote call from a location server looks.
		 *
		 * "nfsmig migrate src:/fs dest" - remote mig, local update
		 * (since you're running from the location server)
		 *
		 * "nfsmig migrate /fs dest loc" - local mig, remote update
		 * (since you're running from the source server and are
		 * pointed to the location server)
		 *
		 * "nfsmig migrate src:/fs dest loc" is a syntax error
		 */
		if (argc == 2) {
			if (server == NULL) {
				no_update = 1;
			} else {
				updates = hostname;
			}
		} else {
			if (server != NULL)
				usage();
			updates = loc;
		}

		/*
		 * Before migrate, ensure that the file system is available for
		 * migration. Actually, need a more exhaustive check of
		 * preconditions before proceeding with the migration.
		 */
		err = status2(server, fs_path, &mig_fsstat, &err_sig);
		if (err != 0) {
			goto out;
		}

		if ((mig_fsstat & FS_FROZEN) ||
		    (mig_fsstat & FS_CONVERTED)) {
			(void) printf(
			    gettext("File system not ready for migration\n"));
			print_fsstatus(mig_fsstat);
			err = 1;
			goto out;
		}

		(void) printf(gettext("%s migrating %s and updating %s\n"),
		    hostname, argument, updates);

		err = migrate(server, tsm, fs_path, dest, loc, &err_sig);
		if (err != 0) {
			goto out;
		}

		if (no_update)
			goto out;

		err = update2(loc, fs_path, dest, &err_sig);

	} else if (strcmp(command, "clear") == 0) {

		/*
		 * Similar to migrate but with some differences:
		 * "nfsmig clear /fs" - local clear, no update
		 *
		 * "nfsmig clear /fs dest" - local and remote clear, no update
		 * (execution from the source)
		 *
		 * "nfsmig clear src:/fs dest" - local and remote clear, local
		 * update (execution from the location server)
		 *
		 * "nfsmig clear /fs dest loc" - local and remote clear, remote
		 * update (execution from the source)
		 *
		 * We play with the destination differently because we're
		 * falling back to something known instead of using something
		 * new.
		 */

		if (argc == 1) {
			if (server != NULL)
				usage();
			no_update = 1;
		} else if (argc == 2) {
			if (server == NULL) {
				no_update = 1;
			} else {
				updates = hostname;
			}
		} else {
			if (server != NULL)
				usage();
			updates = loc;
		}

		(void) printf(gettext("%s clearing %s and updating %s\n"),
		    hostname, argument, updates);

		/* Clear the source */
		err = clear2(NULL, fs_path, SRC, &err_sig);
		if (err != 0) {
			goto out;
		}

		/* Clear the destination, if specified */
		if (dest != NULL) {
			err = clear2(dest, fs_path, DEST, &err_sig);
			if (err != 0) {
				goto out;
			}
		}

		if (no_update)
			goto out;

		if (server != NULL) {
			err = update2(loc, fs_path, server, &err_sig);
		} else {
			err = update2(loc, fs_path, hostname, &err_sig);
		}

	} else if (strcmp(command, "status") == 0) {
		if (dest != NULL)
			usage();

		(void) printf(gettext("%s status %s\n"), hostname, argument);
		err = status2(server, fs_path, &mig_fsstat, &err_sig);
		if (err != 0) {
			goto out;
		}
		print_fsstatus(mig_fsstat);
	} else
		usage();

out:
	if (f != NULL) {
		(void) close_file(&f);
	}
	check_error(&err_sig);

	/*
	 * On encountering an error during migration, we attempt to thaw the
	 * file systems. For now, we have chosen to thaw on the source, if the
	 * error happens on the source, as well as the destination, if the
	 * error happens on the destination.
	 */
	if (err) {
		if (debug) {
			(void) printf(gettext("Encountered error during"
			    "migration...attempting to thaw the fs at %s..\n"),
			    hostname);
		}
		clup_err = status2(server, fs_path, &mig_fsstat, &err_sig);
		if (clup_err == 0) {
			clup_err = thaw_if_frozen(server, fs_path, &err_sig);
			if (clup_err != 0) {
				check_error(&err_sig);
			}
		}
	}
	if (server) {
		free(server);
		server = NULL;
	}
	if (err_sig.mes_hint) {
		free(err_sig.mes_hint);
		err_sig.mes_hint = NULL;
	}

	if (err != 0) {
		return (1);
	} else {
		return (0);
	}
}

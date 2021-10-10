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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <userdefs.h>
#include <errno.h>
#include <strings.h>
#include "messages.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <nssec.h>
#include <sys/param.h>
#include <stdlib.h>
#include <libintl.h>
#include <netdb.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <libzfs.h>
#include <sys/nvpair.h>
#include "users.h"
#include <sys/mount.h>

#define	SBUFSZ 2048
/* buffer for system call */
static char cmdbuf[PATH_MAX + 1024];

static const char *zfs_allow_cmd =
	"/usr/sbin/zfs allow -u %s snapshot,create,mount %s 2>/dev/null";
static const char *zfs_set_mount_cmd =
	"/usr/sbin/zfs set mountpoint=%s %s 2>/dev/null";

static int check_zfs_dataset(const char *, char *);
static int zfs_create_dataset(char *, char *, char *);

static libzfs_handle_t *g_zfs = NULL;
extern int rm_files(char *);

/*
 * Create a home directory and populate with files from skeleton directory.
 * arguments:
 * username of new user
 * home directory to create
 * skel directory to copy if indicated
 * uid of new user
 * group id of new user
 * flag - mkdir flag
 */
int
create_home_dir(char *user, char *homedir, char *skeldir, uid_t uid, gid_t gid)
{
	struct stat statbuf;

	if (stat(homedir, &statbuf) == 0) {
		if (statbuf.st_uid  != uid)  {
			errmsg(M_HOME_CREATE, strerror(errno));
			exit(EX_HOMEDIR);
		}
		if (statbuf.st_gid  != gid)  {
			errmsg(M_HOME_CREATE, strerror(errno));
			exit(EX_HOMEDIR);
		}
	} else {
		/*
		 * check if parent directory is a mounted dataset
		 */
		char dataset[MAXPATHLEN + 1];
		char check_mount[MAXPATHLEN + 1];
		char *slash;

		(void) strlcpy(check_mount, homedir, MAXPATHLEN);
		if (slash = strrchr(check_mount, '/')) {
			*slash = '\0';
		} else {
			errmsg(M_HOME_CREATE, strerror(errno));
			exit(EX_HOMEDIR);
		}

		if (check_zfs_dataset(check_mount, dataset) == 0) {
			/*
			 * create a new dataset for the homedir
			 */
			if (zfs_create_dataset(user, homedir, dataset) < 0) {
				errmsg(M_HOME_CREATE, strerror(errno));
				exit(EX_HOMEDIR);
			}
		} else if (mkdir(homedir, 0775) != 0) {
			errmsg(M_HOME_CREATE, strerror(errno));
			exit(EX_HOMEDIR);
		}
	}

	if (chown(homedir, uid, gid) != 0) {
		errmsg(M_HOME_OWNER, strerror(errno));
		return (EX_HOMEDIR);
	}

	if (skeldir) {
		/* copy the skel_dir into the home directory */
		(void) snprintf(cmdbuf, sizeof (cmdbuf),
		    "cd %s && find . -print | cpio -pdR %s %s",
		    skeldir, user, homedir);

		if (execute_cmd_str(cmdbuf, NULL, 0) > 0) {
			errmsg(M_COPY_SKELETON, strerror(errno));
			return (EX_HOMEDIR);
		}
	}
	return (EX_SUCCESS);
}


static int compare_hosts(char *remote, char *local)
{
	struct hostent *local_mchne;
	struct hostent *server_mchne;
	int error_num = 0;
	int rc = 0;

	if (local == NULL || remote == NULL)
		return (-1);

	if ((local_mchne =
	    getipnodebyname(local, AF_INET, 0, &error_num)) == NULL) {
		errmsg(M_HOSTNAME_RESOLVE, local);
		return (-1);
	}
	if ((server_mchne =
	    getipnodebyname(remote, AF_INET, 0, &error_num)) == NULL) {
		errmsg(M_HOSTNAME_RESOLVE, remote);
		return (-1);
	}
	rc = memcmp(local_mchne->h_addr, server_mchne->h_addr,
	    server_mchne->h_length);
	freehostent(local_mchne);
	freehostent(server_mchne);
	return (rc);
}

/*
 * validate the -d option
 * dir can be a path on local machine or
 * server:path
 * autohome contains the autohome entry
 * if the path is a valid one.
 * this function checks to see if dir is
 * on the same machine, if so it returns
 * 1 to create a dir on the same machine,
 * 0 to not create a dir on the same machine,
 * -1 is invalid path.
 * It is the callers responsibility to free
 * the autohome entry returned.
 */
int
valid_dir_input(char *dir, char **autohome)
{
	char *path = NULL;
	char hostname[MAXHOSTNAMELEN];
	char nodename[MAXHOSTNAMELEN];
	int create_local_home = 0;
	int len = 0, maxlen;

	if (gethostname(nodename, MAXHOSTNAMELEN) != 0) {
		errmsg(M_HOSTNAME, strerror(errno));
		return (-1);
	}
	if (nodename[0] == '\0') {
		(void) sprintf(nodename, "localhost");
	}

	path = strchr(dir, ':');
	if (path) {
		len = abs(dir - path) + 1;
		path++;
		if (*path != '/') {
			return (-1);
		}
		if (len >= MAXHOSTNAMELEN) {
			len = MAXHOSTNAMELEN - 1;
		}
		(void) strlcpy(hostname, dir, len);
		if (strlen(path) >= MAXPATHLEN)
			maxlen = len + MAXPATHLEN - 1 + 2;
		else
			maxlen = len + strlen(path) + 2;

		*autohome = (char *)malloc(maxlen);
		if (*autohome == NULL) {
			errmsg(M_MEM_ALLOCATE);
			return (-1);
		}

		strlcpy(*autohome, dir, maxlen);

		if (strcmp(hostname, "localhost") == 0) {
			create_local_home = 1;
		} else if (compare_hosts(hostname, nodename) == 0) {
			create_local_home = 1;
		}
	} else {
		maxlen = strlen(dir) + 2;
		if (strlen(dir) > MAXPATHLEN + 2) {
			maxlen = MAXPATHLEN + 2;
		}
		if (*dir != '/')
			return (-1);
		if (strncmp(dir, "/home/", 6) == 0) {
			errmsg(M_PATH_HOME);
			return (-1);
		} else if (strncmp(dir, "/net/", 5) == 0) {
			char *end;
			char *server;
			end = strchr(dir+5, '/');

			/* if path is not /net/a/b then it is invalid. */
			if (end != NULL && *(end+1) != '\0') {
				len = abs(dir + 5 - end - 1);
				path = (char *)calloc(1, maxlen);
				if (path) {
					(void) strncpy(path, (dir + 5), len-1);
					server = strdup(path);
					if (server == NULL) {
						errmsg(M_MEM_ALLOCATE);
						return (-1);
					}
					(void) strcat(path, ":");
					(void) strlcat(path, end, maxlen);
					*autohome = path;
					if (strcmp(server,
					    "localhost") == 0) {
						create_local_home = 1;
					} else if (compare_hosts(server,
					    nodename) == 0) {
						create_local_home = 1;
					}
					free(server);
				} else {
					errmsg(M_MEM_ALLOCATE);
					return (-1);
				}
			} else {
				return (-1);
			}
		} else {
			len = maxlen + strlen("localhost") + 2;
			path = (char *)malloc(len);
			if (path) {
				snprintf(path, len, "%s:%s", "localhost", dir);
				*autohome = path;
				create_local_home = 1;
			} else {
				errmsg(M_MEM_ALLOCATE);
				return (-1);
			}
		}

	}
	return (create_local_home);
}

/*
 * This function returns the dataset path corresponding
 * to the mountpath, if it exists.
 */
static int
check_zfs_dataset(const char *mountpoint, char *dataset)
{
	FILE *finp;
	struct mnttab mget;
	struct mnttab mref;
	int ret;

	if (mountpoint == NULL || mountpoint[0] == '\0') {
		errmsg(M_MOUNTPOINT_INVALID);
		return (-1);
	}
	dataset[0] = '\0';
	finp = fopen(MNTTAB, "r");
	if (finp) {
		mntnull(&mref);
		mref.mnt_mountp = (char *)mountpoint;
		mref.mnt_fstype = "zfs";
		ret = getmntany(finp, &mget, &mref);
		if (ret == 0 && mget.mnt_special != NULL) {
			strncpy(dataset, mget.mnt_special, MAXPATHLEN);
			(void) fclose(finp);
			return (0);
		} else {
			fclose(finp);
			return (-1);
		}
	}
	return (-1);
}

/*
 * create zfs dataset and set delegate operations.
 */
static int
zfs_create_dataset(char *user, char *mountpoint, char *dataset)
{
	nvlist_t *props = NULL;
	zfs_handle_t *zhp = NULL;
	char userdataset[MAXPATHLEN + 1];

	if (g_zfs == NULL && (g_zfs = libzfs_init()) == NULL) {
		errmsg(M_ZFS_CREATE, dataset);
		return (-1);
	}

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0) {
		errmsg(M_MEM_ALLOCATE);
		return (-1);
	}
	if (nvlist_add_string(props, "mountpoint", mountpoint) != 0) {
		errmsg(M_ZFS_CREATE, dataset);
		return (-1);
	}
	(void) snprintf(userdataset, sizeof (userdataset), "%s/%s",
	    dataset, user);

	if (zfs_create(g_zfs, userdataset, ZFS_TYPE_FILESYSTEM, props) != 0) {
		errmsg(M_ZFS_CREATE, dataset);
		return (-1);
	}
	nvlist_free(props);

	if ((zhp = zfs_open(g_zfs, userdataset, ZFS_TYPE_FILESYSTEM)) == NULL) {
		errmsg(M_ZFS_CREATE, dataset);
		return (-1);
	}

	if (zfs_mount(zhp, NULL, 0) != 0) {
		errmsg(M_ZFS_MOUNT);
		zfs_close(zhp);
		return (-1);
	}
	zfs_close(zhp);

	snprintf(cmdbuf, sizeof (cmdbuf), zfs_allow_cmd, user, userdataset);

	if (execute_cmd_str(cmdbuf, NULL, 0) > 0) {
		errmsg(M_ZFS_DELEGATE, dataset);
		return (-1);
	}
	return (0);
}

int
try_remount(char *mountpoint, char *new_mount)
{
	char dataset[MAXPATHLEN +1];
	char err_mesg[SBUFSZ];

	if (mountpoint == NULL) {
		errmsg(M_MOUNTPOINT_INVALID);
		return (-1);
	}
	if (new_mount == NULL) {
		errmsg(M_MOUNTPOINT_INVALID);
		return (-1);
	}

	if (check_zfs_dataset(mountpoint, dataset) == 0) {
		snprintf(cmdbuf, sizeof (cmdbuf),
		    zfs_set_mount_cmd, new_mount, dataset);
		err_mesg[0] = '\0';
		if (execute_cmd_str(cmdbuf, err_mesg,
		    sizeof (err_mesg)) > 0) {
			if (err_mesg[0] != '\0')
				(void) fprintf(stderr, "%s", err_mesg);
			return (-1);
		}
	} else {
		if (rename(mountpoint, new_mount) == -1) {
			return (-1);
		}
	}
	return (0);
}

int
remove_zfs_dataset(char *mountpoint, char *zfs_name)
{
	zfs_handle_t *zhp = NULL;

	if (g_zfs == NULL && (g_zfs = libzfs_init()) == NULL) {
		errmsg(M_ZFS_DESTROY, mountpoint);
		return (-1);
	}

	if ((zhp = zfs_open(g_zfs, zfs_name, ZFS_TYPE_FILESYSTEM)) == NULL) {
		errmsg(M_ZFS_DESTROY, mountpoint);
		return (-1);
	}

	if (zfs_unmount(zhp, mountpoint, 0) != 0) {
		errmsg(M_ZFS_UMOUNT, mountpoint);
		errmsg(M_ZFS_DESTROY, mountpoint);
		zfs_close(zhp);
		return (-1);
	}
	if (zfs_destroy(zhp, NULL) != 0) {
		errmsg(M_ZFS_DESTROY, mountpoint);
		zfs_close(zhp);
		return (-1);
	}
	zfs_close(zhp);
	return (0);
}

int
remove_home_dir(char *user, uid_t uid, gid_t gid, sec_repository_t *rep)
{
	/*
	 * check the auto_home entry for the user
	 * if it has the same server name as this machine then
	 * delete the homedir.
	 */
	struct stat statbuf;
	char *ptr;
	char *mountpoint;
	char *server;
	char path[MAXPATHLEN + MAXHOSTNAMELEN + 2];
	char dataset[MAXPATHLEN +1];
	char hostname[MAXHOSTNAMELEN +1];

	path[0] = '\0';
	if (rep->rops->get_autohome(user, path) != SEC_REP_SUCCESS) {
		errmsg(M_AUTOHOME);
		return (-1);
	}

	if (gethostname(hostname, sizeof (hostname)) != 0) {
		errmsg(M_HOSTNAME, strerror(errno));
		return (-1);
	}
	if (path[0] == '\0') {
		(void) fprintf(stderr, "%s",
		    gettext("Autohome entry does not exist."
		    "Cannot remove home directory.\n"));
		return (-1);
	}
	/* check is there is a remote host in the auto_home entry. */
	mountpoint = path;
	if ((ptr = strchr(path, ':')) != NULL) {
		server = strdup(path);
		if (server == NULL) {
			errmsg(M_MEM_ALLOCATE);
			return (-1);
		}
		mountpoint = ptr++;
		ptr = strchr(server, ':');
		*ptr = '\0';
		if (strcmp(server, "localhost") != 0)
			if (compare_hosts(server, hostname) != 0)
				return (EX_SUCCESS);
		free(server);
		mountpoint++;
	}

	if (mountpoint != NULL) {
		char homedir[MAXPATHLEN +1];

		if (stat(mountpoint, &statbuf) != 0) {
			errmsg(M_RMHOME, strerror(errno));
			return (-1);
		}

		if (check_perm(statbuf, uid,
		    gid, S_IWOTH | S_IXOTH) != 0) {
			errmsg(M_NO_PERM, user, mountpoint);
			return (-1);
		}
		/* try unmounting the automount trigger. */
		(void) snprintf(homedir, sizeof (homedir), "/home/%s",
		    user);
		(void) umount(homedir);
		/* check if zfs dataset */
		if (check_zfs_dataset(mountpoint, dataset) == 0) {
			if (remove_zfs_dataset(mountpoint, dataset) != 0) {
				return (-1);
			}
			(void) rmdir(mountpoint);
			return (EX_SUCCESS);
		} else {
			return (rm_files(mountpoint));
		}

	} else {
		errmsg(M_RMHOME, "");
		return (-1);
	}
}

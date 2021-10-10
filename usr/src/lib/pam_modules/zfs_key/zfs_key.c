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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <libzfs.h>
#include <libzfs_impl.h>
#include <sys/zfs_ioctl.h>

#include <libgen.h>
#include <nss_dbdefs.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>
#include <sys/mount.h>
#include <libintl.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mntent.h>

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_impl.h>

#define	_PATH_EXPORT_HOME	"/export/home"
#define	DEFAULT_HOMES		"rpool/export/home"
#define	DEFAULT_ENCRYPTION	"on"

/*ARGSUSED*/
int
pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	return (PAM_IGNORE);
}

/*PRINTFLIKE3*/
static void
display_msg(pam_handle_t *pamh, int msg_style, char *fmt, ...)
{
	va_list ap;
	char messages[1][PAM_MAX_MSG_SIZE];

	va_start(ap, fmt);
	(void) vsnprintf(messages[0], sizeof (messages[0]), fmt, ap);
	(void) __pam_display_msg(pamh, msg_style, 1, messages, NULL);
	va_end(ap);
}

static void
prompt_for_passphrase(zfs_handle_t *zhp, pam_handle_t *pamh)
{
	char *passphrase;
	char prompt[PAM_MAX_MSG_SIZE];

	(void) snprintf(prompt, sizeof (prompt),
	    dgettext(TEXT_DOMAIN, "Enter passphrase for ZFS filesystem %s:"),
	    zfs_get_name(zhp));
	if (__pam_get_authtok(pamh, PAM_PROMPT, 0, prompt,
	    &passphrase) != PAM_SUCCESS)
		return;

	zfs_crypto_set_key(zhp, passphrase, strlen(passphrase));
}

static int
set_delegation(zfs_handle_t *zhp, uid_t uid)
{
	int ret;
	nvlist_t *fsaclnv;
	nvlist_t *acenv;
	size_t nvsz;
	char *nvbuf;
	zfs_cmd_t zc = { 0 };
	char acewho[13];

	(void) nvlist_alloc(&fsaclnv, NV_UNIQUE_NAME, 0);
	(void) nvlist_alloc(&acenv, NV_UNIQUE_NAME, 0);

	(void) nvlist_add_boolean_value(acenv, "key", B_TRUE);
	(void) nvlist_add_boolean_value(acenv, "keychange", B_TRUE);
	(void) nvlist_add_boolean_value(acenv, "mount", B_TRUE);

	(void) snprintf(acewho, sizeof (acewho), "ul$%u", uid);
	(void) nvlist_add_nvlist(fsaclnv, acewho, acenv);
	(void) snprintf(acewho, sizeof (acewho), "ud$%u", uid);
	(void) nvlist_add_nvlist(fsaclnv, acewho, acenv);

	(void) nvlist_size(fsaclnv, &nvsz, NV_ENCODE_NATIVE);
	nvbuf = malloc(nvsz);
	(void) nvlist_pack(fsaclnv, &nvbuf, &nvsz, NV_ENCODE_NATIVE, 0);
	(void) strlcpy(zc.zc_name, zfs_get_name(zhp), sizeof (zc.zc_name));
	zc.zc_nvlist_src_size = nvsz;
	zc.zc_nvlist_src = (uintptr_t)nvbuf;
	zc.zc_perm_action = 0;

	ret = ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_SET_FSACL, &zc);

	return (ret);
}

static void
get_default_homes(char *homes, size_t homes_len)
{
	FILE *mtab;
	struct mnttab mget;
	struct mnttab mref;
	int err;

	mtab = fopen(MNTTAB, "r");
	if (mtab == NULL) {
		(void) strlcpy(homes, DEFAULT_HOMES, homes_len);
		return;
	}

	mntnull(&mref);
	mref.mnt_mountp = (char *)_PATH_EXPORT_HOME;
	mref.mnt_fstype = MNTTYPE_ZFS;
	err = getmntany(mtab, &mget, &mref);
	(void) fclose(mtab);
	if (err == 0 && mget.mnt_special != NULL) {
		(void) strlcpy(homes, mget.mnt_special, homes_len);
	} else {
		(void) strlcpy(homes, DEFAULT_HOMES, homes_len);
	}
}

/*
 *	zfs_key - pam_sm_setcred
 *
 *	Entry flags = 	PAM_ESTABLISH_CRED,	load key
 *			PAM_DELETE_CRED, 	unload key
 *			PAM_REINITIALIZE_CRED	NOOP
 *			PAM_REFRESH_CRED	NOOP
 *			PAM_SILENT, print no messages to user.
 *
 *	Returns	PAM_SUCCESS, if all successful.
 *		PAM_CRED_ERR, if unable to set credentials.
 *		PAM_USER_UNKNOWN, if PAM_USER not set, or unable to find
 *			user in databases.
 *		PAM_SYSTEM_ERR, if no valid flag, or unable to get/set
 *			user's audit state.
 */

int
pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	int	i;
	boolean_t debug = B_FALSE;
	boolean_t force = B_FALSE;
	boolean_t create = B_FALSE;
	boolean_t nowarn = (flags & PAM_SILENT) == PAM_SILENT;
	int	ret = PAM_SUCCESS;
	int	err = 0;
	char	*user;
	char	pwbuf[NSS_BUFLEN_PASSWD];
	struct passwd pw;
	libzfs_handle_t	*g_zfs = NULL;
	zfs_handle_t	*zhp = NULL;
	char	homes[ZFS_MAXNAMELEN];
	char	dataset[ZFS_MAXNAMELEN];
	char	encryption[ZFS_MAXNAMELEN];
	int keystatus;
	char 	keysource[ZFS_MAXNAMELEN];
	char	propsrc[ZFS_MAXNAMELEN];
	zprop_source_t propsrctype;
	char *authtok;

	get_default_homes(homes, sizeof (homes));
	(void) strlcpy(encryption, DEFAULT_ENCRYPTION, sizeof (encryption));
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "debug") == 0) {
			debug = B_TRUE;
		} else if (strcmp(argv[i], "nowarn") == 0) {
			nowarn = B_TRUE;
		} else if (strncmp(argv[i], "homes=", 6) == 0) {
			(void) strlcpy(homes,
			    &argv[i][6], sizeof (homes));
			if (strlen(homes) == 0) {
				display_msg(pamh, PAM_ERROR_MSG,
				    dgettext(TEXT_DOMAIN,
				    "pam_zfs_key invalid configuration 'homes='"
				    " can not be empty"));
				return (PAM_SERVICE_ERR);
			}
		} else if (strcmp(argv[i], "force") == 0) {
			force = B_TRUE;
		} else if (strcmp(argv[i], "create") == 0) {
			create = B_TRUE;
		} else if (strncmp(argv[i], "encryption=", 11) == 0) {
			(void) strlcpy(encryption,
			    &argv[i][11], sizeof (encryption));
		} else {
			display_msg(pamh, PAM_ERROR_MSG, dgettext(TEXT_DOMAIN,
			    "pam_zfs_key unknown option '%s'"), argv[i]);
			return (PAM_SERVICE_ERR);
		}
	}

	if (debug)
		__pam_log(LOG_AUTH | LOG_DEBUG,
		    "pam_zfs_key: pam_sm_setcred(flags = %x, argc= %d)",
		    flags, argc);

	(void) pam_get_item(pamh, PAM_USER, (void **)&user);
	if (user == NULL || *user == '\0' ||
	    (getpwnam_r(user, &pw, pwbuf, sizeof (pwbuf)) == NULL)) {
		__pam_log(LOG_AUTH | LOG_ERR,
		    "pam_zfs_key: USER NULL or empty!\n");
		return (PAM_USER_UNKNOWN);
	}
	/* validate flags */
	switch (flags & (PAM_ESTABLISH_CRED | PAM_DELETE_CRED |
	    PAM_REINITIALIZE_CRED | PAM_REFRESH_CRED)) {
	case 0:
		/* set default flag */
		flags |= PAM_ESTABLISH_CRED;
		break;
	case PAM_REINITIALIZE_CRED:
	case PAM_REFRESH_CRED:
		return (PAM_IGNORE);
	case PAM_ESTABLISH_CRED:
	case PAM_DELETE_CRED:
		break;
	default:
		__pam_log(LOG_AUTH | LOG_ERR,
		    "pam_zfs_key: invalid flags %x", flags);
		return (PAM_SYSTEM_ERR);
	}

	(void) pam_get_item(pamh, PAM_AUTHTOK, (void **)&authtok);

	g_zfs = libzfs_init();
	(void) snprintf(dataset, sizeof (dataset), "%s/%s", homes, user);

	zhp = zfs_open(g_zfs, dataset, ZFS_TYPE_FILESYSTEM);
	if (zhp == NULL && (!create || flags & PAM_DELETE_CRED ||
	    (strncmp(pw.pw_dir, "/home", strlen("/home")) != 0))) {
		libzfs_fini(g_zfs);
		return (PAM_IGNORE);
	} else if (zhp == NULL && create && (flags & PAM_ESTABLISH_CRED)) {
		nvlist_t *props;
		char mountpoint[MAXPATHLEN];
		if (!nowarn) {
			display_msg(pamh, PAM_TEXT_INFO,
			    dgettext(TEXT_DOMAIN,
			    "Creating home directory with encryption=%s.\n"
			    "Your login password will be used as the "
			    "wrapping key."), encryption);
		}
		(void) nvlist_alloc(&props, NV_UNIQUE_NAME, 0);
		(void) nvlist_add_string(props,
		    zfs_prop_to_name(ZFS_PROP_ENCRYPTION), encryption);
		(void) nvlist_add_string(props,
		    zfs_prop_to_name(ZFS_PROP_KEYSOURCE), "passphrase,prompt");
		libzfs_crypto_set_key(g_zfs, authtok, strlen(authtok));
		err = zfs_create(g_zfs, dataset, ZFS_TYPE_FILESYSTEM, props);
		if (err != 0) {
			if (!nowarn)
				display_msg(pamh, PAM_ERROR_MSG,
				    dgettext(TEXT_DOMAIN,
				    "creating home directory failed: %s"),
				    libzfs_error_description(g_zfs));
			return (PAM_CRED_ERR);
		}
		zhp = zfs_open(g_zfs, dataset, ZFS_TYPE_FILESYSTEM);
		if (zhp == NULL) {
			err = -1;
			goto out;
		}
		err = set_delegation(zhp, pw.pw_uid);
		if (err != 0) {
			goto out;
		}
		err = zfs_mount(zhp, NULL, 0);
		if (err != 0) {
			goto out;
		}
		err = zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
		    sizeof (mountpoint), NULL, NULL, 0, B_FALSE);
		if (err != 0) {
			goto out;
		}
		err = chown(mountpoint, pw.pw_uid, pw.pw_gid);
		goto out;
	}

	keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);
	/*
	 * Checking keystatus of none means we don't need to
	 * check the value of the encryption property since
	 * datasets with encryption=off always have an undefined
	 * keystatus.
	 */
	if (keystatus == ZFS_CRYPT_KEY_NONE) {
		__pam_log(LOG_AUTH | LOG_DEBUG,
		    "home dir %s for %s is not encrytped", dataset, user);
		ret = PAM_IGNORE;
		goto out;
	}

	(void) zfs_prop_get(zhp, ZFS_PROP_KEYSOURCE, keysource,
	    ZFS_MAXNAMELEN, &propsrctype, propsrc, sizeof (propsrc), B_FALSE);

	if (propsrctype != ZPROP_SRC_LOCAL ||
	    (strcmp(keysource, "passphrase,prompt") != 0)) {
		__pam_log(LOG_AUTH | LOG_DEBUG,
		    "home dir %s for %s has incompatible keysource %s",
		    dataset, user, keysource);
		ret = PAM_IGNORE;
		goto out;
	}

	if (keystatus == ZFS_CRYPT_KEY_UNAVAILABLE &&
	    (flags & PAM_ESTABLISH_CRED)) {
		struct statvfs sbuf;
		char *parent = strdup(pw.pw_dir);
		/*
		 * First try an unmount of pw_dir if it is lofs
		 * in case automounter already attempted to
		 * mount up the pw_dir.
		 * Earlier checks ensure that pw_dir is some form
		 * of /home/....
		 */
		if (parent == NULL) {
			display_msg(pamh, PAM_ERROR_MSG,
			    dgettext(TEXT_DOMAIN,
			    "pam_zfs_key strdup failed: %m"));
			ret = PAM_BUF_ERR;
			goto out;
		} else {
			parent = dirname(parent);
		}
		if (statvfs(parent, &sbuf) == 0 &&
		    strcmp(sbuf.f_basetype, "autofs") == 0) {
			(void) umount(pw.pw_dir);
		}
		free(parent);

		if (authtok != NULL)
			zfs_crypto_set_key(zhp, authtok, strlen(authtok));
		if (zfs_key_load(zhp, B_TRUE, B_TRUE, B_TRUE) != 0) {
			if (!nowarn) {
				display_msg(pamh, PAM_ERROR_MSG,
				    dgettext(TEXT_DOMAIN,
				    "ZFS Key load failed for %s: %s"),
				    dataset, libzfs_error_description(g_zfs));
			}
			prompt_for_passphrase(zhp, pamh);
			if (zfs_key_load(zhp, B_TRUE, B_TRUE, B_TRUE) != 0) {
				ret = PAM_CRED_ERR;
				if (!nowarn) {
					display_msg(pamh, PAM_ERROR_MSG,
					    dgettext(TEXT_DOMAIN,
					    "ZFS Key load failed for %s: %s"),
					    dataset,
					    libzfs_error_description(g_zfs));
				}
			}
		}
	} else if (keystatus == ZFS_CRYPT_KEY_AVAILABLE &&
	    (flags & PAM_ESTABLISH_CRED)) {
		__pam_log(LOG_AUTH | LOG_DEBUG,
		    "ZFS home directory key already present", dataset, user);
		ret = PAM_IGNORE;
		goto out;
	} else if (keystatus == ZFS_CRYPT_KEY_AVAILABLE &&
	    (flags & PAM_DELETE_CRED)) {
		/*
		 * Don't fail on the unmount just in case the module
		 * isn't running with all privs.  If this is
		 * the automount point it will just end up stale and timeout,
		 * if the underlying real home dir does end up unmounted.
		 */
		(void) chdir("/");
		if (umount(pw.pw_dir) != 0 && force) {
			(void) umount2(pw.pw_dir, MS_FORCE);
		}
		if (zfs_key_unload(zhp, force) != 0) {
			ret = PAM_SYSTEM_ERR;
			if (!nowarn) {
				display_msg(pamh, PAM_ERROR_MSG,
				    dgettext(TEXT_DOMAIN,
				    "ZFS Key unload for %s failed: %s "),
				    dataset, libzfs_error_description(g_zfs));
			}
		}
		/* Try again to remove the possibly automounted dir */
		if (umount(pw.pw_dir) != 0 && force) {
			(void) umount2(pw.pw_dir, MS_FORCE);
		}
	}

out:
	if (zhp != NULL)
		zfs_close(zhp);
	if (g_zfs != NULL)
		libzfs_fini(g_zfs);

	if (err != 0 && ret == PAM_SUCCESS) {
		ret = PAM_CRED_ERR;
	}

	return (ret);
}

int
pam_sm_chauthtok(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	boolean_t nowarn = (flags & PAM_SILENT) == PAM_SILENT;
	int	ret = PAM_SUCCESS;
	int	err;
	char	*user;
	char	pwbuf[NSS_BUFLEN_PASSWD];
	struct passwd pw;
	libzfs_handle_t	*g_zfs = NULL;
	zfs_handle_t	*zhp = NULL;
	char	homes[ZFS_MAXNAMELEN];
	char	dataset[ZFS_MAXNAMELEN];
	char 	keysource[ZFS_MAXNAMELEN];
	char	propsrc[ZFS_MAXNAMELEN];
	char *authtok, *oldauthtok;
	int keystatus;
	zprop_source_t propsrctype;
	pid_t pid;
	int i;

	get_default_homes(homes, sizeof (homes));
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "nowarn") == 0) {
			nowarn = B_TRUE;
		} else if (strncmp(argv[i], "homes=", 6) == 0) {
			(void) strlcpy(homes,
			    &argv[i][6], sizeof (homes));
		}
	}

	if ((flags & PAM_PRELIM_CHECK) != 0)
		return (PAM_IGNORE);

	if ((flags & PAM_UPDATE_AUTHTOK) == 0)
		return (PAM_SYSTEM_ERR);

	(void) pam_get_item(pamh, PAM_USER, (void **)&user);
	if (user == NULL || *user == '\0' ||
	    (getpwnam_r(user, &pw, pwbuf, sizeof (pwbuf)) == NULL)) {
		__pam_log(LOG_AUTH | LOG_ERR,
		    "pam_zfs_key: USER NULL or empty!\n");
		return (PAM_USER_UNKNOWN);
	}

	g_zfs = libzfs_init();
	(void) snprintf(dataset, sizeof (dataset), "%s/%s", homes, user);

	zhp = zfs_open(g_zfs, dataset, ZFS_TYPE_FILESYSTEM);
	if (zhp == NULL) {
		libzfs_fini(g_zfs);
		return (PAM_IGNORE);
	}

	(void) zfs_prop_get(zhp, ZFS_PROP_KEYSOURCE, keysource,
	    ZFS_MAXNAMELEN, &propsrctype, propsrc, sizeof (propsrc), B_FALSE);

	if (propsrctype != ZPROP_SRC_LOCAL ||
	    (strcmp(keysource, "passphrase,prompt") != 0)) {
		ret = PAM_IGNORE;
		goto out;
	}

	keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);
	/*
	 * Checking keystatus for undefined means we don't need to
	 * check the value of the encryption property since
	 * datasets with encryption=off always have an undefined
	 * keystatus.
	 */
	if (keystatus == ZFS_CRYPT_KEY_NONE) {
		ret = PAM_IGNORE;
		goto out;
	}

	if (keystatus == ZFS_CRYPT_KEY_UNAVAILABLE) {
		(void) pam_get_item(pamh, PAM_OLDAUTHTOK, (void **)&oldauthtok);
		if (oldauthtok == NULL) {
			if (!nowarn)
				display_msg(pamh, PAM_ERROR_MSG,
				    dgettext(TEXT_DOMAIN, "ZFS Key load failed"
				    " for %s: old passphrase required"),
				    dataset);
			ret = PAM_AUTHTOK_ERR;
			goto out;
		}
		zfs_crypto_set_key(zhp, oldauthtok, strlen(oldauthtok));
		err = zfs_key_load(zhp, B_TRUE, B_TRUE, B_TRUE);
		if (err != 0) {
			prompt_for_passphrase(zhp, pamh);
			err = zfs_key_load(zhp, B_TRUE, B_TRUE, B_TRUE) != 0;
			if (err != 0) {
				ret = PAM_AUTHTOK_ERR;
				if (!nowarn) {
					display_msg(pamh, PAM_ERROR_MSG,
					    dgettext(TEXT_DOMAIN,
					    "ZFS Key load failed for %s: %s"),
					    dataset,
					    libzfs_error_description(g_zfs));
				}
				goto out;
			}
		}
	}

	(void) pam_get_item(pamh, PAM_AUTHTOK, (void **)&authtok);
	zfs_crypto_set_key(zhp, authtok, strlen(authtok));

	/*
	 * Temporarily switch over euid to ruid so that the kernel
	 * side of ZFS checks the 'keychange' delgation of the real user
	 * changing their password and it isn't bypassed because
	 * passwd(1) is setuid.
	 */
	if (pw.pw_uid != geteuid()) {
		pid = fork();
		if (pid == 0) {
			(void) setreuid(-1, pw.pw_uid);
			(void) setregid(-1, pw.pw_gid);
			err = zfs_key_change(zhp, B_FALSE, NULL);
			_exit(err);
		} else if (pid == -1) {
			ret = PAM_SYSTEM_ERR;
			goto out;
		} else {
			pid = waitpid(pid, &err, WCONTINUED | WSTOPPED);
		}
	} else {
		err = zfs_key_change(zhp, B_FALSE, NULL);
	}

	if (err != 0) {
		ret = PAM_AUTHTOK_ERR;
		if (!nowarn) {
			display_msg(pamh, PAM_ERROR_MSG, dgettext(TEXT_DOMAIN,
			    "ZFS Key change failed for %s: %s"), dataset,
			    libzfs_error_description(g_zfs));
		}
	} else if (!nowarn) {
		display_msg(pamh, PAM_TEXT_INFO, dgettext(TEXT_DOMAIN,
		    "ZFS Key change for %s succesful"), dataset);
	}


out:
	if (zhp != NULL)
		zfs_close(zhp);
	if (g_zfs != NULL)
		libzfs_fini(g_zfs);

	if (err != 0 && ret == PAM_SUCCESS)
		ret = PAM_AUTHTOK_ERR;

	return (ret);
}

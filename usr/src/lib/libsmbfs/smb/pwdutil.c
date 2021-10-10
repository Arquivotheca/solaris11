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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include <smbsrv/libsmb.h>
#include <netsmb/smb_lib.h>
#include <netsmb/smb_keychain.h>

#define	SMBFS_PASSWD	SMBIOD_PWDFILE
#define	SMBFS_OPASSWD	"/var/smb/osmbfspasswd"
#define	SMBFS_PASSTEMP	"/var/smb/smbfsptmp"
#define	SMBFS_PASSLCK	"/var/smb/.smbfspwd.lock"

#define	SMBFS_PWD_BUFSIZE 1024

typedef enum {
	SMBFS_PWD_UID = 0,
	SMBFS_PWD_DOMAIN,
	SMBFS_PWD_USER,
	SMBFS_PWD_LMHASH,
	SMBFS_PWD_NTHASH,
	SMBFS_PWD_NARG
} smbfs_pwdarg_t;

static smb_lockinfo_t lockinfo = {{0, 0, 0, 0, 0, 0}, 0, 0, -1, DEFAULTMUTEX};

static int smbfs_pwd_lock(void);
static int smbfs_pwd_unlock(void);

/*
 * buffer structure used by smbfs_pwd_fgetent/smbfs_pwd_fputent
 */
typedef struct smbfs_pwbuf {
	char		pw_buf[SMBFS_PWD_BUFSIZE];
	smbfs_passwd_t	*pw_pwd;
} smbfs_pwbuf_t;

static smbfs_pwbuf_t *smbfs_pwd_fgetent(FILE *, smbfs_pwbuf_t *);
static int smbfs_pwd_fputent(FILE *, const smbfs_pwbuf_t *);

/*
 * Loads the smbfspasswd entries into the password keychain.
 */
int
smbfs_pwd_loadkeychain(void)
{
	smbfs_pwbuf_t pwbuf;
	smbfs_passwd_t smbpw;
	FILE *fp;
	int err;

	err = smbfs_pwd_lock();
	if (err != SMB_PWE_SUCCESS)
		return (err);

	if ((fp = fopen(SMBFS_PASSWD, "rF")) == NULL) {
		(void) smbfs_pwd_unlock();
		return (err);
	}

	pwbuf.pw_pwd = &smbpw;

	while (smbfs_pwd_fgetent(fp, &pwbuf) != NULL) {
		err = smbfs_keychain_addhash(smbpw.pw_uid, smbpw.pw_dom,
		    smbpw.pw_usr, smbpw.pw_lmhash, smbpw.pw_nthash);

		if (err != 0) {
			(void) fclose(fp);
			(void) smbfs_pwd_unlock();
			return (err);
		}
	}

	(void) fclose(fp);
	(void) smbfs_pwd_unlock();

	return (0);
}

/*
 * Updates the password hashes of the given password info if the entry already
 * exists, otherwise it'll add an entry with given password information.
 *
 * The entries are added to the file based on the uid in ascending order.
 */
int
smbfs_pwd_add(smbfs_passwd_t *newpw)
{
	struct stat64 stbuf;
	FILE *src, *dst;
	int tempfd;
	int err = SMB_PWE_SUCCESS;
	smbfs_pwbuf_t pwbuf, newpwbuf;
	smbfs_passwd_t smbpw;
	boolean_t doinsert = B_FALSE;

	err = smbfs_pwd_lock();
	if (err != SMB_PWE_SUCCESS)
		return (err);

	if (stat64(SMBFS_PASSWD, &stbuf) < 0) {
		err = SMB_PWE_STAT_FAILED;
		goto passwd_exit;
	}

	if ((tempfd =
	    open(SMBFS_PASSTEMP, O_WRONLY|O_CREAT|O_TRUNC, 0600)) < 0) {
		err = SMB_PWE_OPEN_FAILED;
		goto passwd_exit;
	}

	if ((dst = fdopen(tempfd, "wF")) == NULL) {
		err = SMB_PWE_OPEN_FAILED;
		goto passwd_exit;
	}

	if ((src = fopen(SMBFS_PASSWD, "rF")) == NULL) {
		err = SMB_PWE_OPEN_FAILED;
		(void) fclose(dst);
		(void) unlink(SMBFS_PASSTEMP);
		goto passwd_exit;
	}

	pwbuf.pw_pwd = &smbpw;
	newpwbuf.pw_pwd = newpw;

	/*
	 * copy old password entries to temporary file while replacing
	 * the entry that matches uid, domain, and user
	 */
	while (smbfs_pwd_fgetent(src, &pwbuf) != NULL) {
		if (newpw->pw_uid < smbpw.pw_uid) {
			doinsert = B_TRUE;
			break;
		} else if (newpw->pw_uid == smbpw.pw_uid) {
			if ((smb_strcasecmp(
			    smbpw.pw_dom, newpw->pw_dom, 0) == 0) &&
			    (smb_strcasecmp(
			    smbpw.pw_usr, newpw->pw_usr, 0) == 0))
				break;
		}

		err = smbfs_pwd_fputent(dst, &pwbuf);

		if (err != SMB_PWE_SUCCESS) {
			(void) fclose(src);
			(void) fclose(dst);
			goto passwd_exit;
		}
	}

	err = smbfs_pwd_fputent(dst, &newpwbuf);

	if (err != SMB_PWE_SUCCESS) {
		(void) fclose(src);
		(void) fclose(dst);
		goto passwd_exit;
	}

	if (doinsert) {
		/*
		 * a new entry was inserted before the old entry
		 * so now copy the old entry to the temporary
		 * file
		 */
		err = smbfs_pwd_fputent(dst, &pwbuf);

		if (err != SMB_PWE_SUCCESS) {
			(void) fclose(src);
			(void) fclose(dst);
			goto passwd_exit;
		}
	}

	/*
	 * copy any remaining old password entries to temporary file
	 */
	while (smbfs_pwd_fgetent(src, &pwbuf) != NULL) {
		err = smbfs_pwd_fputent(dst, &pwbuf);

		if (err != SMB_PWE_SUCCESS) {
			(void) fclose(src);
			(void) fclose(dst);
			goto passwd_exit;
		}
	}

	(void) fclose(src);
	if (fclose(dst) != 0) {
		err = SMB_PWE_CLOSE_FAILED;
		goto passwd_exit; /* Don't trust the temporary file */
	}

	/* Rename temp to passwd */
	if (unlink(SMBFS_OPASSWD) && access(SMBFS_OPASSWD, 0) == 0) {
		err = SMB_PWE_UPDATE_FAILED;
		(void) unlink(SMBFS_PASSTEMP);
		goto passwd_exit;
	}

	if (link(SMBFS_PASSWD, SMBFS_OPASSWD) == -1) {
		err = SMB_PWE_UPDATE_FAILED;
		(void) unlink(SMBFS_PASSTEMP);
		goto passwd_exit;
	}

	if (rename(SMBFS_PASSTEMP, SMBFS_PASSWD) == -1) {
		err = SMB_PWE_UPDATE_FAILED;
		(void) unlink(SMBFS_PASSTEMP);
		goto passwd_exit;
	}

	(void) chmod(SMBFS_PASSWD, 0400);

passwd_exit:
	(void) smbfs_pwd_unlock();

	return (err);
}

int
smbfs_pwd_del(smbfs_passwd_t *newpw, boolean_t delete_all)
{
	struct stat64 stbuf;
	FILE *src, *dst;
	int tempfd;
	int err = SMB_PWE_SUCCESS;
	smbfs_pwbuf_t pwbuf;
	smbfs_passwd_t smbpw;

	err = smbfs_pwd_lock();
	if (err != SMB_PWE_SUCCESS)
		return (err);

	if (stat64(SMBFS_PASSWD, &stbuf) < 0) {
		err = SMB_PWE_STAT_FAILED;
		goto passwd_exit;
	}

	if ((tempfd =
	    open(SMBFS_PASSTEMP, O_WRONLY|O_CREAT|O_TRUNC, 0600)) < 0) {
		err = SMB_PWE_OPEN_FAILED;
		goto passwd_exit;
	}

	if ((dst = fdopen(tempfd, "wF")) == NULL) {
		err = SMB_PWE_OPEN_FAILED;
		goto passwd_exit;
	}

	if ((src = fopen(SMBFS_PASSWD, "rF")) == NULL) {
		err = SMB_PWE_OPEN_FAILED;
		(void) fclose(dst);
		(void) unlink(SMBFS_PASSTEMP);
		goto passwd_exit;
	}

	pwbuf.pw_pwd = &smbpw;

	/*
	 * copy password entries to temporary file while skipping
	 * the entry that matches uid, domain, and user
	 */
	while (smbfs_pwd_fgetent(src, &pwbuf) != NULL) {
		if (newpw->pw_uid == smbpw.pw_uid) {

			if (delete_all)
				continue;

			if ((smb_strcasecmp(
			    smbpw.pw_dom, newpw->pw_dom, 0) == 0) &&
			    (smb_strcasecmp(
			    smbpw.pw_usr, newpw->pw_usr, 0) == 0))
				continue;
		}

		err = smbfs_pwd_fputent(dst, &pwbuf);

		if (err != SMB_PWE_SUCCESS) {
			(void) fclose(src);
			(void) fclose(dst);
			goto passwd_exit;
		}
	}


	(void) fclose(src);
	if (fclose(dst) != 0) {
		err = SMB_PWE_CLOSE_FAILED;
		goto passwd_exit; /* Don't trust the temporary file */
	}

	/* Rename temp to passwd */
	if (unlink(SMBFS_OPASSWD) && access(SMBFS_OPASSWD, 0) == 0) {
		err = SMB_PWE_UPDATE_FAILED;
		(void) unlink(SMBFS_PASSTEMP);
		goto passwd_exit;
	}

	if (link(SMBFS_PASSWD, SMBFS_OPASSWD) == -1) {
		err = SMB_PWE_UPDATE_FAILED;
		(void) unlink(SMBFS_PASSTEMP);
		goto passwd_exit;
	}

	if (rename(SMBFS_PASSTEMP, SMBFS_PASSWD) == -1) {
		err = SMB_PWE_UPDATE_FAILED;
		(void) unlink(SMBFS_PASSTEMP);
		goto passwd_exit;
	}

	(void) chmod(SMBFS_PASSWD, 0400);

passwd_exit:
	(void) smbfs_pwd_unlock();

	return (err);
}

/*
 * Parse the buffer in the passed pwbuf and fill in the
 * smbfs password structure to point to the parsed information.
 * The entry format is:
 *
 *	<user-id>:<domain>:<user-name>:<LM hash>:<NTLM hash>
 *
 * Returns a pointer to the passed pwbuf structure on success,
 * otherwise returns NULL.
 */
static smbfs_pwbuf_t *
smbfs_pwd_fgetent(FILE *fp, smbfs_pwbuf_t *pwbuf)
{
	char *argv[SMBFS_PWD_NARG];
	char *pwentry;
	smbfs_passwd_t *pw;
	smbfs_pwdarg_t i;
	int lm_len, nt_len;

	pwentry = pwbuf->pw_buf;
	if (fgets(pwentry, SMBFS_PWD_BUFSIZE, fp) == NULL)
		return (NULL);
	(void) trim_whitespace(pwentry);

	for (i = 0; i < SMBFS_PWD_NARG; ++i) {
		if ((argv[i] = strsep((char **)&pwentry, ":")) == NULL)
			return (NULL);
	}

	if ((*argv[SMBFS_PWD_UID] == '\0') ||
	    (*argv[SMBFS_PWD_DOMAIN] == '\0') ||
	    (*argv[SMBFS_PWD_USER] == '\0'))
		return (NULL);

	pw = pwbuf->pw_pwd;
	bzero(pw, sizeof (smbfs_passwd_t));
	pw->pw_uid = strtoul(argv[SMBFS_PWD_UID], 0, 10);
	(void) strlcpy(pw->pw_dom, argv[SMBFS_PWD_DOMAIN],
	    sizeof (pw->pw_dom));
	(void) strlcpy(pw->pw_usr, argv[SMBFS_PWD_USER], sizeof (pw->pw_usr));

	lm_len = strlen(argv[SMBFS_PWD_LMHASH]);
	if (lm_len == SMBAUTH_HEXHASH_SZ) {
		(void) hextobin(argv[SMBFS_PWD_LMHASH], SMBAUTH_HEXHASH_SZ,
		    (char *)pw->pw_lmhash, SMBAUTH_HASH_SZ);
	} else if (lm_len != 0) {
		return (NULL);
	}

	nt_len = strlen(argv[SMBFS_PWD_NTHASH]);
	if (nt_len == SMBAUTH_HEXHASH_SZ) {
		(void) hextobin(argv[SMBFS_PWD_NTHASH], SMBAUTH_HEXHASH_SZ,
		    (char *)pw->pw_nthash, SMBAUTH_HASH_SZ);
	} else if (nt_len != 0) {
		return (NULL);
	}

	return (pwbuf);
}

/*
 * Converts LM/NTLM hash to hex string and write them along with user's name,
 * domain, and Id to the smbfspasswd file.
 */
static int
smbfs_pwd_fputent(FILE *fp, const smbfs_pwbuf_t *pwbuf)
{
	smbfs_passwd_t *pw = pwbuf->pw_pwd;
	char hex_nthash[SMBAUTH_HEXHASH_SZ+1];
	char hex_lmhash[SMBAUTH_HEXHASH_SZ+1];
	int rc;

	(void) bintohex((char *)pw->pw_lmhash, SMBAUTH_HASH_SZ,
	    hex_lmhash, SMBAUTH_HEXHASH_SZ);
	hex_lmhash[SMBAUTH_HEXHASH_SZ] = '\0';

	(void) bintohex((char *)pw->pw_nthash, SMBAUTH_HASH_SZ,
	    hex_nthash, SMBAUTH_HEXHASH_SZ);
	hex_nthash[SMBAUTH_HEXHASH_SZ] = '\0';

	rc = fprintf(fp, "%u:%s:%s:%s:%s\n", pw->pw_uid, pw->pw_dom, pw->pw_usr,
	    hex_lmhash, hex_nthash);

	if (rc <= 0)
		return (SMB_PWE_WRITE_FAILED);

	return (SMB_PWE_SUCCESS);
}

/*
 * A wrapper around smb_file_lock() which locks smbfs password
 * file so that only one thread at a time is operational.
 */
static int
smbfs_pwd_lock(void)
{
	int res;

	if (smb_file_lock(SMBFS_PASSLCK, &lockinfo)) {
		switch (errno) {
		case EINTR:
			res = SMB_PWE_BUSY;
			break;
		case EACCES:
			res = SMB_PWE_DENIED;
			break;
		case 0:
			res = SMB_PWE_SUCCESS;
			break;
		}
	} else
		res = SMB_PWE_SUCCESS;

	return (res);
}

/*
 * A wrapper around smb_file_unlock() which unlocks
 * smbfs password file.
 */
static int
smbfs_pwd_unlock(void)
{
	if (smb_file_unlock(&lockinfo))
		return (SMB_PWE_SYSTEM_ERROR);

	return (SMB_PWE_SUCCESS);
}

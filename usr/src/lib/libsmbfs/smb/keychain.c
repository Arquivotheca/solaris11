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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * External interface to the libsmbfs/netsmb keychain
 * storage mechanism.  This interface is consumed by
 * the "smbutil" commands: login, logout, ...
 * and by the SMBFS PAM module.
 */

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libintl.h>

#include <cflib.h>
#include <netsmb/smb_dev.h>
#include <netsmb/smb_lib.h>
#include <netsmb/smb_keychain.h>

#include "charsets.h"
#include "private.h"
#include "ntlm.h"

/* common func. for add/del/chk */
static int
smbfs_keychain_cmn(
	int cmd,
	uid_t uid,
	const char *dom,
	const char *usr,
	uchar_t *lmhash,
	uchar_t *nthash)
{
	smbioc_pk_t pk;
	int err, fd, sz;

	memset(&pk, 0, sizeof (pk));
	pk.pk_uid = uid;
	err = 0;
	fd = -1;

	switch (cmd) {

	case SMBIOC_PK_ADD:
		/*
		 * Add password hashes to the keychain.
		 */
		if (lmhash == NULL || nthash == NULL) {
			err = SMB_KEYCHAIN_BADPASSWD;
			goto out;
		}
		memcpy(pk.pk_lmhash, lmhash, SMBIOC_HASH_SZ);
		memcpy(pk.pk_nthash, nthash, SMBIOC_HASH_SZ);
		/* FALLTHROUGH */

	case SMBIOC_PK_CHK:
	case SMBIOC_PK_DEL:
		/*
		 * Copy domain and user.
		 */
		if (dom == NULL) {
			err = SMB_KEYCHAIN_BADDOMAIN;
			goto out;
		}
		sz = sizeof (pk.pk_dom);
		if (strlcpy(pk.pk_dom, dom, sz) >= sz) {
			err = SMB_KEYCHAIN_BADDOMAIN;
			goto out;
		}
		if (usr == NULL) {
			err = SMB_KEYCHAIN_BADUSER;
			goto out;
		}
		sz = sizeof (pk.pk_usr);
		if (strlcpy(pk.pk_usr, usr, sz) >= sz) {
			err = SMB_KEYCHAIN_BADUSER;
			goto out;
		}
		break;

	case SMBIOC_PK_DEL_OWNER:	/* all owned by the caller */
	case SMBIOC_PK_DEL_EVERYONE:	/* all owned by everyone */
		/*
		 * These two do not copyin any args, but we'll
		 * pass pk here anyway just so we can use the
		 * common code path below.
		 */
		break;

	default:
		err = SMB_KEYCHAIN_UNKNOWN;
		goto out;
	}

	fd = smb_open_driver();
	if (fd < 0) {
		err = SMB_KEYCHAIN_NODRIVER;
		goto out;
	}

	err = 0;
	if (ioctl(fd, cmd, &pk) < 0) {
		err = errno;
		goto out;
	}

	if (cmd == SMBIOC_PK_CHK) {
		if (lmhash != NULL)
			memcpy(lmhash, pk.pk_lmhash, SMBIOC_HASH_SZ);
		if (nthash != NULL)
			memcpy(nthash, pk.pk_nthash, SMBIOC_HASH_SZ);
	}

out:
	if (fd != -1)
		close(fd);

	return (err);
}

/*
 * Add a password to the keychain.
 *
 * Note: pass is a cleartext password.
 * We use it here to compute the LM hash and NT hash,
 * and then store ONLY the hashes.
 */
int
smbfs_keychain_add(uid_t uid, const char *dom, const char *usr,
	const char *pass)
{
	uchar_t lmhash[SMBIOC_HASH_SZ];
	uchar_t nthash[SMBIOC_HASH_SZ];
	int err, cmd = SMBIOC_PK_ADD;
	smbfs_passwd_t pwdinfo;

	if (pass == NULL)
		return (SMB_KEYCHAIN_BADPASSWD);

	if ((err = ntlm_compute_lm_hash(lmhash, pass)) != 0)
		return (err);
	if ((err = ntlm_compute_nt_hash(nthash, pass)) != 0)
		return (err);

	if ((err = smbfs_keychain_cmn(cmd, uid, dom, usr, lmhash, nthash))
	    != 0)
		return (err);

	(void) strlcpy(pwdinfo.pw_dom, dom, sizeof (pwdinfo.pw_dom));
	(void) strlcpy(pwdinfo.pw_usr, usr, sizeof (pwdinfo.pw_usr));
	memcpy(pwdinfo.pw_lmhash, lmhash, SMBIOC_HASH_SZ);
	memcpy(pwdinfo.pw_nthash, nthash, SMBIOC_HASH_SZ);

	(void) smbfs_iod_pwdadd(&pwdinfo);

	return (0);
}

/*
 * Variant of the above that takes LM and NT hashes.
 * The parameter for LM hash or NT hash can be NULL.
 */
int
smbfs_keychain_addhash(uid_t uid, const char *dom, const char *usr,
	const uchar_t *lmhash, const uchar_t *nthash)
{
	static const uchar_t hash_zeros[SMBIOC_HASH_SZ] = { 0 };
	int err, cmd = SMBIOC_PK_ADD;

	if (lmhash == NULL)
		lmhash = hash_zeros;

	if (nthash == NULL)
		nthash = hash_zeros;

	err = smbfs_keychain_cmn(cmd, uid, dom, usr,
	    (uchar_t *)lmhash, (uchar_t *)nthash);
	return (err);
}

/* Delete a password from the keychain. */
int
smbfs_keychain_del(uid_t uid, const char *dom, const char *usr)
{
	smbfs_passwd_t pwdinfo;
	int err;

	if ((err =
	    smbfs_keychain_cmn(SMBIOC_PK_DEL, uid, dom, usr, NULL, NULL)) != 0)
		return (err);

	(void) strlcpy(pwdinfo.pw_dom, dom, sizeof (pwdinfo.pw_dom));
	(void) strlcpy(pwdinfo.pw_usr, usr, sizeof (pwdinfo.pw_usr));

	(void) smbfs_iod_pwddel(&pwdinfo);

	return (0);
}

/*
 * Check for existence of a keychain entry.
 * Returns 0 if it exists, else ENOENT.
 */
int
smbfs_keychain_chk(const char *dom, const char *usr)
{
	uid_t uid = (uid_t)-1;
	return (smbfs_keychain_cmn(SMBIOC_PK_CHK, uid, dom, usr, NULL, NULL));
}

/*
 * Get the stored hashes
 */
int
smbfs_keychain_get(const char *dom, const char *usr,
		uchar_t *lmhash, uchar_t *nthash)
{
	uid_t uid = (uid_t)-1;
	int err, cmd = SMBIOC_PK_CHK;

	err = smbfs_keychain_cmn(cmd, uid, dom, usr, lmhash, nthash);
	return (err);
}

/*
 * Delete all keychain entries owned by the caller.
 */
int
smbfs_keychain_del_owner()
{
	int err, cmd = SMBIOC_PK_DEL_OWNER;
	uid_t uid = getuid();
	if ((err = smbfs_keychain_cmn(cmd, uid, NULL, NULL, NULL, NULL)) != 0)
		return (err);

	(void) smbfs_iod_pwddel(NULL);

	return (0);
}

/*
 * Delete all keychain entries (regardless of onwer).
 * Requires super-user privliege.
 */
int
smbfs_keychain_del_everyone()
{
	int cmd = SMBIOC_PK_DEL_EVERYONE;
	uid_t uid = getuid();
	return (smbfs_keychain_cmn(cmd, uid, NULL, NULL, NULL, NULL));
}

/*
 * Private function to get keychain p/w hashes.
 */
int
smb_get_keychain(struct smb_ctx *ctx)
{
	int err;

	if (ctx->ct_fullserver == NULL) {
		DPRINT("ct_fullserver == NULL");
		return (EINVAL);
	}

	/*
	 * 1st: try lookup using system name
	 */
	err = smbfs_keychain_get(ctx->ct_fullserver, ctx->ct_user,
	    ctx->ct_lmhash, ctx->ct_nthash);
	if (!err) {
		ctx->ct_flags |= SMBCF_KCFOUND;
		DPRINT("found keychain entry for"
		    " server/user: %s/%s\n",
		    ctx->ct_fullserver, ctx->ct_user);
		return (0);
	}

	/*
	 * 2nd: try lookup using domain name
	 */
	err = smbfs_keychain_get(ctx->ct_domain, ctx->ct_user,
	    ctx->ct_lmhash, ctx->ct_nthash);
	if (!err) {
		ctx->ct_flags |= (SMBCF_KCFOUND | SMBCF_KCDOMAIN);
		DPRINT("found keychain entry for"
		    " domain/user: %s/%s\n",
		    ctx->ct_domain, ctx->ct_user);
		return (0);
	}

	return (err);
}


/*
 * This is not really part of the keychain library,
 * but is typically needed in code that wants to
 * provide (editable) defaults for domain/user
 *
 * Get default domain and user names
 */
int
smbfs_default_dom_usr(char *dom, int maxdom, char *usr, int maxusr)
{
	struct smb_ctx  *ctx;
	int err;

	err = smb_ctx_alloc(&ctx);
	if (err)
		return (err);

	if (dom)
		strlcpy(dom, ctx->ct_domain, maxdom);

	if (usr)
		strlcpy(usr, ctx->ct_user, maxusr);

	smb_ctx_free(ctx);
	return (err);
}

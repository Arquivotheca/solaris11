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

#include <sys/types.h>
#include <sys/stat.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <libnvpair.h>
#include <door.h>
#include <errno.h>
#include <sys/mman.h>
#include <alloca.h>

#include <netsmb/smb_dev.h>
#include <netsmb/smb_lib.h>

static int smbfs_door_encode(int, smbfs_passwd_t *, char **, size_t *);
static int smbfs_door_call(char *, size_t, door_arg_t *);

/*
 * Request the creation of our per-user smbiod
 * via door call to the "main" IOD service.
 */
int
smbfs_iod_start(void)
{
	door_arg_t da;
	char *buf = NULL;
	size_t buflen;
	int err;
	int32_t rc;

	if ((err = smbfs_door_encode(SMBIOD_START, NULL, &buf, &buflen)) != 0)
		return (err);

	bzero(&da, sizeof (door_arg_t));
	da.rbuf = (void *) &rc;
	da.rsize = sizeof (rc);

	if ((err = smbfs_door_call(buf, buflen, &da)) != 0) {
		free(buf);
		return (err);
	}
	free(buf);

	return (rc);
}

/*
 * Adds passwd info to the smbfspasswd file.
 */
int
smbfs_iod_pwdadd(smbfs_passwd_t *pwdinfo)
{
	door_arg_t da;
	char *buf = NULL;
	size_t buflen;
	int err;
	int32_t rc;

	if (pwdinfo == NULL)
		return (EINVAL);

	if ((err = smbfs_door_encode(SMBIOD_PWDFILE_ADD, pwdinfo,
	    &buf, &buflen)) != 0)
		return (err);

	bzero(&da, sizeof (door_arg_t));
	da.rbuf = (void *) &rc;
	da.rsize = sizeof (rc);

	if ((err = smbfs_door_call(buf, buflen, &da)) != 0) {
		free(buf);
		return (err);
	}
	free(buf);

	return (rc);
}

/*
 * Removes passwd info(s) from the smbfspasswd file.  If pwdinfo is NULL,
 * removes all passwd info for the same user ID.
 */
int
smbfs_iod_pwddel(smbfs_passwd_t *pwdinfo)
{
	door_arg_t da;
	char *buf = NULL;
	size_t buflen;
	int err, cmd;
	int32_t rc;

	if (pwdinfo == NULL)
		cmd = SMBIOD_PWDFILE_DELALL;
	else
		cmd = SMBIOD_PWDFILE_DEL;

	if ((err = smbfs_door_encode(cmd, pwdinfo, &buf, &buflen)) != 0)
		return (err);

	bzero(&da, sizeof (door_arg_t));
	da.rbuf = (void *) &rc;
	da.rsize = sizeof (rc);

	if ((err = smbfs_door_call(buf, buflen, &da)) != 0) {
		free(buf);
		return (err);
	}
	free(buf);

	return (rc);
}

/*
 * Decodes the door call arguments which is encoded in a buffer via
 * nvlist_pack().  The packed data contains the command and may contain
 * password info.
 *
 * Returns the command and password info if a buffer is supplied to store
 * the password info.
 */
int
smbfs_door_decode(char *buf, size_t buflen, int *cmd, smbfs_passwd_t *pwdinfo)
{
	nvlist_t *nvl;
	uint8_t *lm, *nt;
	uint_t lm_sz, nt_sz;
	char *dom, *usr;
	int err;

	if ((err = nvlist_unpack(buf, buflen, &nvl, 0)) != 0)
		return (err);

	if ((err = nvlist_lookup_int32(nvl, "cmd", cmd)) != 0) {
		nvlist_free(nvl);
		return (err);
	}

	if (pwdinfo != NULL) {
		if ((nvlist_lookup_string(nvl, "dom", &dom) != 0) ||
		    (nvlist_lookup_string(nvl, "usr", &usr) != 0) ||
		    (nvlist_lookup_uint8_array(nvl, "lm", &lm, &lm_sz) != 0) ||
		    (nvlist_lookup_uint8_array(nvl, "nt", &nt, &nt_sz) != 0)) {
			nvlist_free(nvl);
			return (EINVAL);
		}

		(void) strlcpy(pwdinfo->pw_dom, dom, sizeof (pwdinfo->pw_dom));
		(void) strlcpy(pwdinfo->pw_usr, usr, sizeof (pwdinfo->pw_usr));
		(void) bcopy(lm, pwdinfo->pw_lmhash, lm_sz);
		(void) bcopy(nt, pwdinfo->pw_nthash, nt_sz);
	}

	nvlist_free(nvl);
	return (0);
}

/*
 * Encodes the door arguments, the command and password info, if supplied.
 *
 * To have the nvpair library allocate memory for the pack data, the caller
 * should set the buf pointer to NULL.  The memory then should be freed by
 * the caller.
 */
static int
smbfs_door_encode(int cmd, smbfs_passwd_t *pwdinfo, char **buf, size_t *buflen)
{
	nvlist_t *nvl;
	int err;
	smbfs_passwd_t newpwd, *pwdinfop;

	if ((err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0)
		return (err);

	if (pwdinfo == NULL) {
		bzero(&newpwd, sizeof (newpwd));
		pwdinfop = &newpwd;
	} else {
		pwdinfop = pwdinfo;
	}

	if ((nvlist_add_int32(nvl, "cmd", cmd) != 0) ||
	    (nvlist_add_string(nvl, "dom", pwdinfop->pw_dom) != 0) ||
	    (nvlist_add_string(nvl, "usr", pwdinfop->pw_usr) != 0) ||
	    (nvlist_add_uint8_array(nvl, "lm", pwdinfop->pw_lmhash,
	    SMBIOC_HASH_SZ) != 0) ||
	    (nvlist_add_uint8_array(nvl, "nt", pwdinfop->pw_nthash,
	    SMBIOC_HASH_SZ) != 0)) {
		nvlist_free(nvl);
		return (EINVAL);
	}

	if ((err = nvlist_size(nvl, buflen, NV_ENCODE_XDR)) != 0) {
		nvlist_free(nvl);
		return (err);
	}

	err = nvlist_pack(nvl, buf, buflen, NV_ENCODE_XDR, 0);

	nvlist_free(nvl);
	return (err);
}

static int
smbfs_door_call(char *buf, size_t buflen, door_arg_t *da)
{
	const char *svc_door = SMBIOD_SVC_DOOR;
	int32_t err;
	int fd, rc;

	fd = open(svc_door, O_RDONLY, 0);
	if (fd < 0) {
		err = errno;
		return (err);
	}
	da->data_ptr = (void *) buf;
	da->data_size = buflen;
	rc = door_call(fd, da);
	close(fd);
	if (rc < 0) {
		err = errno;
		return (err);
	}

	return (0);
}

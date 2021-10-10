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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <errno.h>
#include <libshare.h>
#include <sharefs/share.h>

#include <smbsrv/smb_share.h>
#include <smbsrv/libsmb.h>

static struct {
	char *value;
	uint32_t flag;
} cscopt[] = {
	{ "disabled",	SMB_SHRF_CSC_DISABLED },
	{ "manual",	SMB_SHRF_CSC_MANUAL },
	{ "auto",	SMB_SHRF_CSC_AUTO },
	{ "vdo",	SMB_SHRF_CSC_VDO }
};

static uint32_t smb_share_mklist(const smb_share_t *, nvlist_t **);

/*
 * Sends the given share definition to libshare
 * to be exported.
 */
uint32_t
smb_share_add(const smb_share_t *si)
{
	nvlist_t *share;
	int status;
	int rc;

	if ((status = smb_name_validate_share(si->shr_name)) != ERROR_SUCCESS)
		return (status);

	if ((status = smb_share_mklist(si, &share)) != ERROR_SUCCESS)
		return (status);

	rc = sa_share_publish(share, SA_PROT_SMB, 0);
	sa_share_free(share);
	return (smb_share_lmerr(rc));
}

/*
 * Asks smbsrv to remove the specified share
 */
uint32_t
smb_share_remove(const char *share_name)
{
	smb_share_t si;
	nvlist_t *share;
	uint32_t status;
	int rc;

	if (share_name == NULL)
		return (NERR_NetNameNotFound);

	bzero(&si, sizeof (si));
	if ((si.shr_name = strdup(share_name)) == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	if ((status = smb_share_mklist(&si, &share)) != ERROR_SUCCESS) {
		free(si.shr_name);
		return (status);
	}

	rc = sa_share_unpublish(share, SA_PROT_SMB, 0);

	sa_share_free(share);
	free(si.shr_name);

	return (smb_share_lmerr(rc));
}

uint32_t
smb_share_count(void)
{
	return (smb_kmod_share_count(SMB_SHARENUM_FLAG_ALL));
}

/*
 * Return B_TRUE if the share exists. Otherwise return B_FALSE.
 */
boolean_t
smb_share_exists(const char *sharename)
{
	return ((smb_share_lookup(sharename, NULL) == NERR_Success));
}

/*
 * Lookup a share by path.  The path can be the empty string.
 *
 * If the share-info pointer is non-null, the share data is returned in si.
 *
 * If the share-info is null, the share is still looked up and the result
 * indicates whether or not the path is shared.
 */
uint32_t
smb_share_check(const char *path, smb_share_t *si)
{
	if (path == NULL)
		return (NERR_NetNameNotFound);

	return (smb_kmod_share_lookup(path, SMB_SHRKEY_WINPATH, si));
}

/*
 * Lookup a share by name.
 *
 * If the share-info pointer is non-null, the share data is returned in si.
 *
 * If the share-info is null, the share is still looked up and the result
 * indicates whether or not a share with the specified name exists.
 */
uint32_t
smb_share_lookup(const char *sharename, smb_share_t *si)
{
	if (sharename == NULL || *sharename == '\0')
		return (NERR_NetNameNotFound);

	if (smb_name_validate_share(sharename) != NERR_Success)
		return (NERR_NetNameNotFound);

	return (smb_kmod_share_lookup(sharename, SMB_SHRKEY_NAME, si));
}

void
smb_share_free(smb_share_t *si)
{
	if (si == NULL)
		return;

	free(si->shr_name);
	free(si->shr_path);
	free(si->shr_cmnt);
	free(si->shr_container);
	free(si->shr_winpath);
	free(si->shr_access_none);
	free(si->shr_access_ro);
	free(si->shr_access_rw);
}

/*
 * Return the option name for the first CSC flag (there should be only
 * one) encountered in the share flags.
 */
char *
smb_share_csc_name(const smb_share_t *si)
{
	int i;

	for (i = 0; i < (sizeof (cscopt) / sizeof (cscopt[0])); ++i) {
		if (si->shr_flags & cscopt[i].flag)
			return (cscopt[i].value);
	}

	return (NULL);
}

/*
 * Map a client-side caching (CSC) option to the appropriate share
 * flag.  Only one option is allowed; an error will be logged if
 * multiple options have been specified.  We don't need to do anything
 * about multiple values here because the SRVSVC will not recognize
 * a value containing multiple flags and will return the default value.
 *
 * If the option value is not recognized, it will be ignored: invalid
 * values will typically be caught and rejected by sharemgr.
 */
void
smb_share_csc_option(const char *value, smb_share_t *si)
{
	int i;

	for (i = 0; i < (sizeof (cscopt) / sizeof (cscopt[0])); ++i) {
		if (strcasecmp(value, cscopt[i].value) == 0) {
			si->shr_flags |= cscopt[i].flag;
			break;
		}
	}

	switch (si->shr_flags & SMB_SHRF_CSC_MASK) {
	case 0:
	case SMB_SHRF_CSC_DISABLED:
	case SMB_SHRF_CSC_MANUAL:
	case SMB_SHRF_CSC_AUTO:
	case SMB_SHRF_CSC_VDO:
		break;

	default:
		syslog(LOG_INFO, "csc option conflict: 0x%08x",
		    si->shr_flags & SMB_SHRF_CSC_MASK);
		break;
	}
}

/*
 * Check that the client-side caching (CSC) option value is valid.
 */
boolean_t
smb_share_csc_valid(const char *value)
{
	int i;

	for (i = 0; i < (sizeof (cscopt) / sizeof (cscopt[0])); ++i) {
		if (strcasecmp(value, cscopt[i].value) == 0)
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Converts libshare error codes to Win32 error codes.
 */
uint32_t
smb_share_lmerr(int sa_err)
{
	switch (sa_err) {
	case SA_OK:
		return (NERR_Success);
	case SA_SHARE_NOT_FOUND:
		return (NERR_NetNameNotFound);
	case SA_NO_MEMORY:
		return (ERROR_NOT_ENOUGH_MEMORY);
	case SA_DUPLICATE_NAME:
		return (NERR_DuplicateShare);
	default:
		break;
	}

	return (NERR_InternalError);
}

/*
 * This function converts the given smb_share_t
 * structure to the nvlist share format.
 */
static uint32_t
smb_share_mklist(const smb_share_t *si, nvlist_t **ret_share)
{
	nvlist_t *share;
	nvlist_t *props;
	char *csc;
	int rc = 0;
	char *mntpnt;

	*ret_share = NULL;

	if ((share = sa_share_alloc(si->shr_name, si->shr_path)) == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	if (si->shr_path != NULL) {
		/*
		 * add global share properties to share nvlist
		 * start with share description
		 */
		if (si->shr_cmnt) {
			if ((rc = sa_share_set_desc(share, si->shr_cmnt))
			    != SA_OK) {
				sa_share_free(share);
				return (smb_share_lmerr(rc));
			}
		}

		/*
		 * Now add the mount point, derived from path
		 */
		if ((mntpnt = malloc(MAXPATHLEN)) == NULL) {
			sa_share_free(share);
			return (ERROR_NOT_ENOUGH_MEMORY);
		}

		if ((rc = sa_get_mntpnt_for_path(si->shr_path, mntpnt,
		    MAXPATHLEN, NULL, 0, NULL, 0)) != SA_OK) {
			/* could not resolve mntpnt use "" */
			free(mntpnt);
			if ((mntpnt = strdup("")) == NULL) {
				sa_share_free(share);
				return (ERROR_NOT_ENOUGH_MEMORY);
			}
		}

		rc = sa_share_set_mntpnt(share, mntpnt);
		free(mntpnt);
		if (rc != SA_OK) {
			sa_share_free(share);
			return (ERROR_NOT_ENOUGH_MEMORY);
		}

		/*
		 * Now add protocol specific properties
		 * These are stored in an embedded nvlist
		 */
		if ((rc = sa_share_set_def_proto(share, SA_PROT_SMB))
		    != SA_OK) {
			sa_share_free(share);
			return (smb_share_lmerr(rc));
		}

		if ((props = sa_share_get_proto(share, SA_PROT_SMB)) == NULL) {
			sa_share_free(share);
			return (NERR_InternalError);
		}

		if (si->shr_container)
			rc |= sa_share_set_prop(props, SHOPT_AD_CONTAINER,
			    si->shr_container);
		if (si->shr_access_none)
			rc |= sa_share_set_prop(props, SHOPT_NONE,
			    si->shr_access_none);
		if (si->shr_access_ro)
			rc |= sa_share_set_prop(props, SHOPT_RO,
			    si->shr_access_ro);
		if (si->shr_access_rw)
			rc |= sa_share_set_prop(props, SHOPT_RW,
			    si->shr_access_rw);

		if ((si->shr_flags & SMB_SHRF_TRANS) != 0)
			rc |= sa_share_set_transient(share);
		if ((si->shr_flags & SMB_SHRF_ABE) != 0)
			rc |= sa_share_set_prop(props, SHOPT_ABE, "true");
		if ((si->shr_flags & SMB_SHRF_CATIA) != 0)
			rc |= sa_share_set_prop(props, SHOPT_CATIA, "true");
		if ((si->shr_flags & SMB_SHRF_GUEST_OK) != 0)
			rc |= sa_share_set_prop(props, SHOPT_GUEST, "true");
		if ((si->shr_flags & SMB_SHRF_DFSROOT) != 0)
			rc |= sa_share_set_prop(props, SHOPT_DFSROOT, "true");

		if ((si->shr_flags & SMB_SHRF_AUTOHOME) != 0) {
			rc |= sa_share_set_prop(props, "Autohome", "true");
			rc |= nvlist_add_uint32(props, "uid", si->shr_uid);
			rc |= nvlist_add_uint32(props, "gid", si->shr_gid);
		}

		rc |= nvlist_add_byte(props, "drive", si->shr_drive);

		if ((csc = smb_share_csc_name(si)) != NULL)
			rc |= sa_share_set_prop(props, SHOPT_CSC, csc);

		rc |= nvlist_add_uint32(props, "type", si->shr_type);

		if (rc != 0) {
			sa_share_free(share);
			return (ERROR_NOT_ENOUGH_MEMORY);
		}
	}

	*ret_share = share;
	return (ERROR_SUCCESS);
}

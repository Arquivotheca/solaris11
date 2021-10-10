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

/*
 * Routines to manage ZFS shares
 *
 * zfs_share_write
 * zfs_share_read
 * zfs_share_read_init
 * zfs_share_read_get
 * zfs_share_read_fini
 * zfs_share_remove
 * zfs_share_parse
 * zfs_share_merge
 * zfs_share_validate
 * zfs_share_publish
 *
 */

#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libzfs.h>

#include "libzfs_impl.h"

#define	ZFS_ERRBUF_LEN	1024

boolean_t
zfs_is_shareable(zfs_handle_t *zhp, char *mntpnt, size_t mp_len)
{
	if (!zfs_prop_valid_for_type(ZFS_PROP_MOUNTPOINT, zhp->zfs_type))
		return (B_FALSE);

	verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mntpnt, mp_len,
	    NULL, NULL, 0, B_FALSE) == 0);

	if ((strcmp(mntpnt, ZFS_MOUNTPOINT_NONE) == 0) ||
	    strcmp(mntpnt, ZFS_MOUNTPOINT_LEGACY) == 0)
		return (B_FALSE);

	return (sa_path_in_current_zone(mntpnt));
}

/*
 * zfs_share_create_defaults
 *
 * Search the dataset for shares for the specified protocols.
 * If no share exists, create a default share.
 *
 * Returns a libshare error code.
 */
int
zfs_share_create_defaults(zfs_handle_t *zhp, sa_proto_t protos,
    const char *mntpnt)
{
	void *hdl;
	boolean_t found_nfs = B_FALSE;
	boolean_t found_smb = B_FALSE;
	nvlist_t *share;
	char nfsopts[ZFS_MAXPROPLEN];
	char smbopts[ZFS_MAXPROPLEN];
	char *nfsopts_p = NULL;
	char *smbopts_p = NULL;
	zprop_source_t sourcetype;

	/*
	 * iterate through the shares on this dataset
	 * looking for shares for existing shares.
	 */
	if (zfs_share_read_init(zhp, &hdl) == SA_OK) {
		while ((!found_nfs || !found_smb) &&
		    zfs_share_read_next(zhp, hdl, &share) == 0) {
			if (!found_nfs &&
			    sa_share_get_proto(share, SA_PROT_NFS) != NULL) {
				found_nfs = B_TRUE;
			}

			if (!found_smb &&
			    sa_share_get_proto(share, SA_PROT_SMB) != NULL) {
				found_smb = B_TRUE;
			}
			nvlist_free(share);
		}

		zfs_share_read_fini(zhp, hdl);
	}

	/*
	 * if SMB share does not exist,
	 * create default share based on value of sharesmb property.
	 */
	if (!found_smb) {
		if (zfs_prop_get(zhp, ZFS_PROP_SHARESMB,
		    smbopts, sizeof (smbopts), &sourcetype,
		    NULL, 0, B_FALSE) == 0) {
			smbopts_p = smbopts;
		}
	}

	/*
	 * if NFS share does not exist,
	 * create default share based on value of sharenfs property.
	 */
	if (!found_nfs) {
		if (zfs_prop_get(zhp, ZFS_PROP_SHARENFS,
		    nfsopts, sizeof (nfsopts), &sourcetype,
		    NULL, 0, B_FALSE) == 0) {
			nfsopts_p = nfsopts;
		}
	}

	return (sa_share_create_defaults(mntpnt, protos,
	    nfsopts_p, smbopts_p));
}

/*
 * zfs_share_read
 *
 * read a share (packed nvlist) named 'sh_name'
 * It is the callers responsibility to free the nvlist.
 */
int
zfs_share_read(zfs_handle_t *zhp, const char *sh_name, nvlist_t **share)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	struct mnttab entry;
	char errbuf[ZFS_ERRBUF_LEN];
	int rc;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot read share '%s' from '%s'"),
	    sh_name, zfs_get_name(zhp));

	if ((zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) ||
	    libzfs_mnttab_find(hdl, zfs_get_name(zhp), &entry) != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "cannot acquire mountpoint"));
		(void) zfs_error(hdl, EZFS_NOENT, errbuf);
		return (-1);
	}

	rc = sa_share_read(entry.mnt_mountp, sh_name, share);

	if (rc != SA_OK) {
		if (rc != SA_SHARE_NOT_FOUND) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "%s"), sa_strerror(rc));
			(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
		}
		return (-1);
	}

	return (0);
}

int
zfs_share_read_init(zfs_handle_t *zhp, void **hdl)
{
	libzfs_handle_t *zfs_hdl = zhp->zfs_hdl;
	struct mnttab entry;
	char errbuf[ZFS_ERRBUF_LEN];
	int rc;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot read shares for '%s'"),
	    zfs_get_name(zhp));

	if ((zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) ||
	    libzfs_mnttab_find(zfs_hdl, zfs_get_name(zhp), &entry) != 0)
		return (-1);

	rc = sa_share_read_init(entry.mnt_mountp, hdl);

	if (rc != SA_OK) {
		if (rc != SA_SHARE_NOT_FOUND) {
			zfs_error_aux(zfs_hdl, dgettext(TEXT_DOMAIN,
			    "%s"), sa_strerror(rc));
			(void) zfs_error(zfs_hdl, EZFS_BADPROP, errbuf);
		}
		return (-1);
	}

	return (0);
}

int
zfs_share_read_next(zfs_handle_t *zhp, void *hdl, nvlist_t **share)
{
	libzfs_handle_t *zfs_hdl = zhp->zfs_hdl;
	char errbuf[1024];
	int rc;

	if ((rc = sa_share_read_next(hdl, share)) != SA_OK) {
		if (rc != SA_SHARE_NOT_FOUND) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    dgettext(TEXT_DOMAIN, "cannot read share"));
			zfs_error_aux(zfs_hdl, dgettext(TEXT_DOMAIN,
			    "%s"), sa_strerror(rc));
			(void) zfs_error(zfs_hdl, EZFS_BADPROP, errbuf);
		}
		return (-1);
	}

	return (0);
}

/*ARGSUSED*/
void
zfs_share_read_fini(zfs_handle_t *zhp, void *hdl)
{
	sa_share_read_fini(hdl);
}

/*
 * write share to disk, and publish
 */
int
zfs_share_write(zfs_handle_t *zhp, nvlist_t *share)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char errbuf[ZFS_ERRBUF_LEN];
	int rc;

	if ((rc = sa_share_write(share)) != SA_OK) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot write share for '%s'"),
		    zfs_get_name(zhp));

		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "%s"), sa_strerror(rc));
		(void) zfs_error(hdl, EZFS_BADPROP, errbuf);
		return (-1);
	}
	return (0);
}

int
zfs_share_remove(zfs_handle_t *zhp, const char *sh_name)
{
	int rc;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	struct mnttab entry;
	char errbuf[ZFS_ERRBUF_LEN];

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot remove share '%s' from '%s'"),
	    sh_name, zfs_get_name(zhp));

	if ((zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) ||
	    libzfs_mnttab_find(hdl, zfs_get_name(zhp), &entry) != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "cannot acquire mountpoint"));
		(void) zfs_error(hdl, EZFS_NOENT, errbuf);
		return (-1);
	}

	rc = sa_share_remove(sh_name, entry.mnt_mountp);

	if (rc != SA_OK) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "%s"), sa_strerror(rc));
		(void) zfs_error(hdl, EZFS_NOENT, errbuf);
		return (-1);
	}

	return (0);
}

int
zfs_share_parse(zfs_handle_t *zhp, const char *propstr, int unset,
    nvlist_t **share)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	int ret;
	char errbuf[ZFS_ERRBUF_LEN];

	ret = sa_share_parse(propstr, unset, share, errbuf, sizeof (errbuf));
	if (ret != SA_OK) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "%s"), errbuf);
		(void) zfs_error(hdl, EZFS_BADPROP, dgettext(
		    TEXT_DOMAIN, "error parsing share"));
		return (-1);
	} else {
		return (0);
	}
}

/*
 * zfs_share_validate
 *
 * share: share nvlist to validate
 * new:   if non zero, indicates this is a new share and name must
 *        be verified for uniqueness.
 */
/*ARGSUSED*/
int
zfs_share_validate(zfs_handle_t *zhp, nvlist_t *share, boolean_t new,
    char *errbuf, int buflen)
{
	char *sh_path;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zfs_handle_t *sh_zhp;

	if (sa_share_validate(share, new, errbuf, buflen) != SA_OK) {
		return (-1);
	}

	/*
	 * make sure path is located on this dataset
	 */
	if ((sh_path = sa_share_get_path(share)) == NULL) {
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "%s"),
		    sa_strerror(SA_NO_SHARE_PATH));
		return (-1);
	}

	sh_zhp = zfs_path_to_zhandle(hdl, sh_path, ZFS_TYPE_FILESYSTEM);
	if (sh_zhp == NULL) {
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "invalid path"));
		return (-1);
	}

	if (strcmp(zfs_get_name(sh_zhp), zfs_get_name(zhp)) != 0) {
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN,
		    "share path '%s' is not in dataset '%s'"),
		    sh_path, zfs_get_name(zhp));
		zfs_close(sh_zhp);
		return (-1);
	}
	zfs_close(sh_zhp);

	return (0);
}

int
zfs_share_merge(zfs_handle_t *zhp, nvlist_t *dst_share, nvlist_t *src_share,
    int unset)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	int ret;
	char errbuf[ZFS_ERRBUF_LEN];

	ret = sa_share_merge(dst_share, src_share, unset,
	    errbuf, sizeof (errbuf));
	if (ret != SA_OK) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "%s"), errbuf);
		(void) zfs_error(hdl, EZFS_BADPROP, dgettext(
		    TEXT_DOMAIN, "error updating share properties"));
		return (-1);
	} else {
		return (0);
	}
}

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
#include <unistd.h>
#include <strings.h>
#include <fcntl.h>
#include <errno.h>
#include <note.h>
#include <libshare.h>
#include <libshare_impl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sharefs/share.h>

#define	SHAREFS_DEV_PATH	"/devices/pseudo/sharefs@0:sharefs"
#define	SHRBUF_SIZE		8 * 1024

/*
 * The cache plugin library interface with the share cache in
 * sharetab via sharefs ioctl call.
 *
 */
static int cache_pi_init(void);
static void cache_pi_fini(void);
static int sa_cache_init(void *);
static void sa_cache_fini(void *);
static int sa_cache_share_add(nvlist_t *);
static int sa_cache_share_update(nvlist_t *);
static int sa_cache_share_remove(const char *);
static int sa_cache_flush(void);
static int sa_cache_lookup(const char *, const char *, sa_proto_t,
    nvlist_t **);
static int sa_cache_find_init(const char *, sa_proto_t, void **);
static int sa_cache_find_next(void *, nvlist_t **);
static int sa_cache_find_fini(void *);

static int sa_cache_validate_name(const char *, boolean_t);
static int sa_cache_open_sharetab(void);
static void sa_cache_close_sharetab(void);
static int sa_cache_cvt_err(int);

sa_cache_ops_t sa_plugin_ops = {
	.sac_hdr = {
		.pi_ptype = SA_PLUGIN_CACHE,
		.pi_type = 0,
		.pi_name = "shared",
		.pi_version = SA_LIBSHARE_VERSION,
		.pi_flags = 0,
		.pi_init = cache_pi_init,
		.pi_fini = cache_pi_fini
	},
	.sac_init = sa_cache_init,
	.sac_fini = sa_cache_fini,
	.sac_share_add = sa_cache_share_add,
	.sac_share_update = sa_cache_share_update,
	.sac_share_remove = sa_cache_share_remove,
	.sac_flush = sa_cache_flush,
	.sac_share_lookup = sa_cache_lookup,
	.sac_share_find_init = sa_cache_find_init,
	.sac_share_find_next = sa_cache_find_next,
	.sac_share_find_fini = sa_cache_find_fini,
	.sac_share_ds_find_init = NULL,
	.sac_share_ds_find_get = NULL,
	.sac_share_ds_find_fini = NULL,
	.sac_share_validate_name = sa_cache_validate_name,
};

FILE *sharefs_fp = NULL;
int sharefs_fd = -1;

static int
cache_pi_init(void)
{
	return (sa_cache_open_sharetab());
}

static void
cache_pi_fini(void)
{
	sa_cache_close_sharetab();
}

static int
sa_cache_init(void *hdl)
{
	NOTE(ARGUNUSED(hdl))

	return (SA_OK);
}

static void
sa_cache_fini(void *hdl)
{
	NOTE(ARGUNUSED(hdl))
}

static int
sa_cache_share_add(nvlist_t *share)
{
	NOTE(ARGUNUSED(share))

	return (SA_OK);
}

static int
sa_cache_share_update(nvlist_t *share)
{
	NOTE(ARGUNUSED(share))

	return (SA_OK);
}

static int
sa_cache_share_remove(const char *sh_name)
{
	NOTE(ARGUNUSED(sh_name))

	return (SA_OK);
}

static int
sa_cache_flush(void)
{
	return (SA_OK);
}

static int
sa_cache_lookup(const char *sh_name, const char *sh_path, sa_proto_t proto,
    nvlist_t **share)
{
	uint32_t ioclen;
	sharefs_ioc_lookup_t *ioc;
	int rc;

	if (sh_name == NULL)
		return (SA_INVALID_SHARE_NAME);

	if (share == NULL)
		return (SA_INVALID_SHARE);

	ioclen = sizeof (sharefs_ioc_lookup_t) + SHRBUF_SIZE;
	if ((ioc = calloc(1, ioclen)) == NULL)
		return (SA_NO_MEMORY);

	(void) strlcpy(ioc->sh_name, sh_name, SHAREFS_SH_NAME_MAX);
	if (sh_path != NULL)
		(void) strlcpy(ioc->sh_path, sh_path, MAXPATHLEN);
	ioc->proto = proto;
	ioc->shrlen = SHRBUF_SIZE;

	ioc->hdr.version = SHAREFS_IOC_VERSION;
	ioc->hdr.cmd = SHAREFS_IOC_LOOKUP;
	ioc->hdr.len = ioclen;
	ioc->hdr.crc = 0;
	ioc->hdr.crc = sa_crc_gen((uint8_t *)ioc,
	    sizeof (sharefs_ioc_hdr_t));

	if (ioctl(sharefs_fd, SHAREFS_IOC_LOOKUP, ioc) < 0) {
		rc = sa_cache_cvt_err(errno);
	} else {
		int err;

		err = nvlist_unpack(ioc->share, ioc->shrlen, share, 0);
		if (err != 0) {
			switch (err) {
			case EFAULT:
			case ENOTSUP:
				rc = SA_XDR_DECODE_ERR;
				break;
			default:
				rc = sa_cache_cvt_err(err);
				break;
			}
		} else {
			rc = SA_OK;
		}
	}

	if (ioc != NULL)
		free(ioc);

	return (rc);
}

static int
sa_cache_find_init(const char *mntpnt, sa_proto_t proto, void **hdl)
{
	int32_t ioclen;
	sharefs_ioc_find_init_t *ioc;
	int rc;

	if (hdl == NULL)
		return (SA_INTERNAL_ERR);

	ioclen = sizeof (sharefs_ioc_find_init_t);
	if ((ioc = calloc(1, ioclen)) == NULL)
		return (SA_NO_MEMORY);

	if (mntpnt != NULL)
		(void) strlcpy(ioc->mntpnt, mntpnt, MAXPATHLEN);
	ioc->proto = (uint32_t)proto;

	ioc->hdr.version = SHAREFS_IOC_VERSION;
	ioc->hdr.cmd = SHAREFS_IOC_FIND_INIT;
	ioc->hdr.len = ioclen;
	ioc->hdr.crc = 0;
	ioc->hdr.crc = sa_crc_gen((uint8_t *)ioc,
	    sizeof (sharefs_ioc_hdr_t));

	if (ioctl(sharefs_fd, SHAREFS_IOC_FIND_INIT, ioc) < 0) {
		rc = sa_cache_cvt_err(errno);
	} else {
		*hdl = calloc(1, sizeof (sharefs_find_hdl_t));
		if (*hdl == NULL)
			rc = SA_NO_MEMORY;
		else {
			/*
			 * memory will be freed in find_fini
			 */
			bcopy(&ioc->hdl, *hdl, sizeof (sharefs_find_hdl_t));
			rc = SA_OK;
		}
	}

	if (ioc != NULL)
		free(ioc);

	return (rc);
}

static int
sa_cache_find_next(void *hdl, nvlist_t **share)
{
	uint32_t ioclen;
	sharefs_ioc_find_next_t *ioc;
	int rc;

	if (hdl == NULL || share == NULL)
		return (SA_INTERNAL_ERR);

	ioclen = sizeof (sharefs_ioc_find_next_t) + SHRBUF_SIZE;
	if ((ioc = calloc(1, ioclen)) == NULL)
		return (SA_NO_MEMORY);

	bcopy(hdl, &ioc->hdl, sizeof (sharefs_find_hdl_t));

	ioc->shrlen = SHRBUF_SIZE;

	ioc->hdr.version = SHAREFS_IOC_VERSION;
	ioc->hdr.cmd = SHAREFS_IOC_FIND_NEXT;
	ioc->hdr.len = ioclen;
	ioc->hdr.crc = 0;
	ioc->hdr.crc = sa_crc_gen((uint8_t *)ioc,
	    sizeof (sharefs_ioc_hdr_t));

	if (ioctl(sharefs_fd, SHAREFS_IOC_FIND_NEXT, ioc) < 0) {
		rc = sa_cache_cvt_err(errno);
	} else {
		int err;

		err = nvlist_unpack(ioc->share, ioc->shrlen, share, 0);
		if (err != 0) {
			switch (err) {
			case EFAULT:
			case ENOTSUP:
				rc = SA_XDR_DECODE_ERR;
				break;
			default:
				rc = sa_cache_cvt_err(err);
				break;
			}
		} else {
			bcopy(&ioc->hdl, hdl, sizeof (sharefs_find_hdl_t));
			rc = SA_OK;
		}
	}

	if (ioc != NULL)
		free(ioc);

	return (rc);
}

static int
sa_cache_find_fini(void *hdl)
{
	int32_t ioclen;
	sharefs_ioc_find_fini_t *ioc;
	int rc;

	ioclen = sizeof (sharefs_ioc_find_fini_t);
	if ((ioc = calloc(1, ioclen)) == NULL)
		return (SA_NO_MEMORY);

	bcopy(hdl, &ioc->hdl, sizeof (sharefs_find_hdl_t));

	ioc->hdr.version = SHAREFS_IOC_VERSION;
	ioc->hdr.cmd = SHAREFS_IOC_FIND_FINI;
	ioc->hdr.len = ioclen;
	ioc->hdr.crc = 0;
	ioc->hdr.crc = sa_crc_gen((uint8_t *)ioc,
	    sizeof (sharefs_ioc_hdr_t));

	if (ioctl(sharefs_fd, SHAREFS_IOC_FIND_FINI, ioc) < 0) {
		rc = sa_cache_cvt_err(errno);
	} else {
		rc = SA_OK;
	}

	free(hdl);

	if (ioc != NULL)
		free(ioc);

	return (rc);
}

static int
sa_cache_validate_name(const char *sh_name, boolean_t new)
{
	NOTE(ARGUNUSED(sh_name))
	NOTE(ARGUNUSED(new))

	return (SA_NOT_IMPLEMENTED);
}

static int
sa_cache_open_sharetab(void)
{
	sa_cache_close_sharetab();

	sharefs_fp = fopen(SHARETAB, "r");
	if (sharefs_fp == NULL) {
		salog_error(0, "libshare_cache: Error opening %s: %s",
		    SHARETAB, strerror(errno));
		return (SA_SYSTEM_ERR);
	}

	sharefs_fd = fileno(sharefs_fp);

	return (SA_OK);
}

static void
sa_cache_close_sharetab(void)
{
	if (sharefs_fp != NULL) {
		(void) fclose(sharefs_fp);
		sharefs_fd = -1;
		sharefs_fp = NULL;
	}
}

static int
sa_cache_cvt_err(int err)
{
	int rc;

	switch (err) {
	case ENOENT:
		rc = SA_SHARE_NOT_FOUND;
		break;
	case EFAULT:
		rc = SA_INTERNAL_ERR;
		break;
	case ENOTTY:
		rc = SA_NOT_SUPPORTED;
		break;
	case EINVAL:
		rc = SA_INVALID_SHARE;
		break;
	case EAGAIN:
		rc = SA_BUSY;
		break;
	case ENOMEM:
		rc = SA_NO_MEMORY;
		break;
	case ESTALE:
		rc = SA_STALE_HANDLE;
		break;
	default:
		rc = SA_SYSTEM_ERR;
		break;
	}

	return (rc);
}

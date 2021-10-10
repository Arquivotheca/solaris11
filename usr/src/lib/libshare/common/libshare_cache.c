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

#include <assert.h>
#include <fcntl.h>
#include <libnvpair.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libshare.h"
#include "libshare_impl.h"

int
sacache_init(void *hdl)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_init == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_init(hdl));
}

void
sacache_fini(void *hdl)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if (ops != NULL && ops->sac_fini != NULL)
		ops->sac_fini(hdl);
}

/*
 * Add share to cache
 *
 * INPUTS:
 *   share : pointer to nvlist containing share properties
 *
 * RETURNS:
 *	SA_OK             : share added successfully
 *	SA_DUPLICATE_NAME : share with same name already exists in cache
 *	SA_INVALID_SHARE  :
 *	SA_NOT_IMPLEMENTED: routine not implemented
 */
int
sacache_share_add(nvlist_t *share)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_share_add == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_share_add(share));
}

/*
 * Update a share in the cache
 *
 * INPUTS:
 *   share : pointer to nvlist containing share properties
 *
 * RETURNS:
 *	SA_OK             : share added successfully
 *	SA_INVALID_SHARE  :
 *	SA_SHARE_NOT_FOUND: share is not in cache
 *	SA_NOT_IMPLEMENTED: routine not implemented
 */
int
sacache_share_update(nvlist_t *share)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_share_update == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_share_update(share));
}

/*
 * Remove share from cache
 *
 * INPUTS:
 *   sh_name : name of share to remove
 *
 * RETURNS:
 *   SA_OK             : share was removed successfully
 *   SA_SHARE_NOT_FOUND: share not found in cache
 *   SA_NOT_IMPLEMENTED: routine not implemented
 */
int
sacache_share_remove(const char *sh_name)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_share_remove == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_share_remove(sh_name));
}

/*
 * remove all entries from the cache
 *
 * RETURNS:
 *	SA_OK :
 *	SA_NOT_IMPLEMENTED: routine not implemented
 */
int
sacache_flush(void)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_flush == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_flush());
}

/*
 * find share in cache
 *
 * INPUTS:
 *   sh_name : name of share, cannot be NULL
 *   sh_path : path or dataset, can be NULL
 *   proto   : protocol type
 *   share   : place holder for returned share
 *
 * OUTPUTS
 *   share : pointer to found share, NULL if not found
 *
 * RETURNS:
 *   SA_OK : share found and returned in 'share'
 *   SA_SHARE_NOT_FOUND: share not found in cache
 *   SA_NOT_IMPLEMENTED: routine not implemented
 *
 * NOTES
 *   Search the cache for a share that matches input parameters.
 *   If 'sh_path' is non NULL, search entire cache, otherwise only search
 *   shares for the specified dataset/file system.
 *   'proto' must be one of SA_PROT_NFS, SA_PROT_SMB or SA_PROT_ANY
 */
int
sacache_share_lookup(const char *sh_name, const char *sh_path, sa_proto_t proto,
    nvlist_t **share)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_share_lookup == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_share_lookup(sh_name, sh_path, proto, share));
}

/*
 * routine to prepare for retrieving shares from the cache.
 *
 * INPUTS:
 *   sh_path : path or dataset, can be NULL
 *   proto   : protocol type of share to retrieve
 *   hdl     : place holder for returned find handle
 *
 * OUTPUTS
 *   share : pointer to newly allocated handle
 *
 * RETURNS:
 *   SA_OK : init was successful, returned hdl is valid
 *   SA_NO_MEMORY: memory allocation error
 *   SA_NOT_IMPLEMENTED: routine not implemented
 *
 * NOTES
 *   If 'sh_path' is non NULL, search entire cache, otherwise only search
 *   shares for the specified dataset/file system.
 *   The returned 'hdl' is passed to subsequent calls to
 *   sacache_share_find_get() and must be freed by calling
 *   sacache_share_find_fini()
 */
int
sacache_share_find_init(const char *sh_path, sa_proto_t proto, void **hdl)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_share_find_init == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_share_find_init(sh_path, proto, hdl));
}

/*
 * routine to retrieve shares from the cache.
 *
 * INPUTS:
 *   hdl : find handle returned from previous call to
 *         sacache_share_find_init.
 *   share : place holder for returned share
 *
 * OUTPUTS
 *   share : pointer to newly allocated share
 *
 * RETURNS:
 *   SA_OK : share was returned successfully.
 *   SA_NO_MEMORY: memory allocation error
 *   SA_NOT_IMPLEMENTED: routine not implemented
 *
 * NOTES
 *   'proto' must be one of SA_PROT_NFS, SA_PROT_SMB or SA_PROT_ANY.
 *   It is the responsibility of the caller to free the share memory
 *   by calling sa_share_free().
 */
int
sacache_share_find_next(void *hdl, nvlist_t **share)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_share_find_next == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_share_find_next(hdl, share));
}

/*
 * routine to cleanup after retrieving shares from the cache.
 *
 * INPUTS:
 *   hdl : find handle returned from previous call to
 *         sacache_share_find_init.
 *
 * RETURNS:
 *   SA_OK : hdl was cleaned up successfully.
 *   SA_NO_MEMORY: memory allocation error
 *   SA_NOT_IMPLEMENTED: routine not implemented
 *
 */
int
sacache_share_find_fini(void *hdl)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_share_find_fini == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_share_find_fini(hdl));
}

/*
 * routine to prepare for retrieving shares from the cache by dataset name.
 *
 * INPUTS:
 *   sh_path : dataset name
 *   proto   : protocol type of share to retrieve
 *   hdl     : place holder for returned find handle
 *
 * OUTPUTS
 *   share : pointer to newly allocated handle
 *
 * RETURNS:
 *   SA_OK : init was successful, returned hdl is valid
 *   SA_NO_MEMORY: memory allocation error
 *   SA_NOT_IMPLEMENTED: routine not implemented
 *
 * NOTES
 *   The returned 'hdl' is passed to subsequent calls to
 *   sacache_share_ds_find_get() and must be freed by calling
 *   sacache_share_ds_find_fini()
 */
int
sacache_share_ds_find_init(const char *dataset, sa_proto_t proto, void **hdl)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_share_ds_find_init == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_share_ds_find_init(dataset, proto, hdl));
}

/*
 * routine to retrieve shares from the cache.
 *
 * INPUTS:
 *   hdl : find handle returned from previous call to
 *         sacache_share_ds_find_init.
 *   share : place holder for returned share
 *
 * OUTPUTS
 *   share : pointer to newly allocated share
 *
 * RETURNS:
 *   SA_OK : share was returned successfully.
 *   SA_NO_MEMORY: memory allocation error
 *   SA_NOT_IMPLEMENTED: routine not implemented
 *
 * NOTES
 *   It is the responsibility of the caller to free the share memory
 *   by calling sa_share_free().
 */
int
sacache_share_ds_find_get(void *hdl, nvlist_t **share)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_share_ds_find_get == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_share_ds_find_get(hdl, share));
}

/*
 * routine to cleanup after retrieving shares from the cache.
 *
 * INPUTS:
 *   hdl : find handle returned from previous call to
 *         sacache_share_ds_find_init.
 *
 * RETURNS:
 *   SA_OK : hdl was cleaned up successfully.
 *   SA_NO_MEMORY: memory allocation error
 *   SA_NOT_IMPLEMENTED: routine not implemented
 *
 */
int
sacache_share_ds_find_fini(void *hdl)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_share_ds_find_fini == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_share_ds_find_fini(hdl));
}

int
sacache_share_validate_name(const char *sh_name, boolean_t new)
{
	sa_cache_ops_t *ops;

	ops = (sa_cache_ops_t *)saplugin_find_ops(SA_PLUGIN_CACHE, 0);
	if ((ops == NULL) || (ops->sac_share_validate_name == NULL))
		return (SA_NOT_SUPPORTED);

	return (ops->sac_share_validate_name(sh_name, new));
}

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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <libnvpair.h>
#include <sys/devfm.h>
#include <sys/fm/protocol.h>
#include <fmd_agent_impl.h>

static int
cleanup_set_errno(fmd_agent_hdl_t *hdl, nvlist_t *innvl, nvlist_t *outnvl,
    int err)
{
	if (innvl != NULL)
		nvlist_free(innvl);
	if (outnvl != NULL)
		nvlist_free(outnvl);
	return (fmd_agent_seterrno(hdl, err));
}

static int
fmd_agent_cacheop_v1(fmd_agent_hdl_t *hdl, int cmd, uint32_t strandid,
    nvlist_t *fmri, int *statusp)
{
	int err;
	nvlist_t *nvl = NULL, *outnvl = NULL;
	nvlist_t *hcsp;
	uint64_t cache, cachetype, cachelevel, cacheindex, cacheway;

	if ((err = nvlist_lookup_nvlist(fmri, FM_FMRI_HC_SPECIFIC, &hcsp))
	    != 0 || (err = nvlist_alloc(&nvl, NV_UNIQUE_NAME_TYPE, 0)) != 0)
		return (cleanup_set_errno(hdl, NULL, NULL, err));

	/* Get the cache type and level */
	if (nvlist_lookup_uint64(hcsp, FM_FMRI_HC_SPECIFIC_L2CACHE, &cache)
	    == 0) {
		cachelevel = FM_CLR_L2_CACHE;
		cachetype = FM_CLR_UNIFIED_CACHE;
	} else if (nvlist_lookup_uint64(hcsp, FM_FMRI_HC_SPECIFIC_L3CACHE,
	    &cache) == 0) {
		cachelevel = FM_CLR_L3_CACHE;
		cachetype = FM_CLR_UNIFIED_CACHE;
	} else {
		return (cleanup_set_errno(hdl, nvl, NULL, ENOTSUP));
	}

	/* Set strandid, cachetype, and cachelevel */
	if ((err = nvlist_add_uint64(nvl, FM_CLR_STRAND_ID, strandid)) != 0 ||
	    (err = nvlist_add_uint64(nvl, FM_CLR_CACHE_TYPE, cachetype)) != 0 ||
	    (err = nvlist_add_uint64(nvl, FM_CLR_CACHE_LEVEL, cachelevel)) != 0)
		return (cleanup_set_errno(hdl, nvl, NULL, err));

	/* And cacheindex, cacheway */
	if (nvlist_lookup_uint64(hcsp, FM_FMRI_HC_SPECIFIC_CACHEINDEX,
	    &cacheindex) == 0 &&
	    ((err = nvlist_add_uint64(nvl, FM_CLR_CACHE_INDEX,
	    cacheindex)) != 0))
		return (cleanup_set_errno(hdl, nvl, NULL, err));

	if (nvlist_lookup_uint64(hcsp, FM_FMRI_HC_SPECIFIC_CACHEWAY,
	    &cacheway) == 0 &&
	    ((err = nvlist_add_uint64(nvl, FM_CLR_CACHE_WAY,
	    cacheway)) != 0))
		return (cleanup_set_errno(hdl, nvl, NULL, err));

	if ((err = fmd_agent_nvl_ioctl(hdl, cmd, 1, nvl,
	    statusp != NULL ? &outnvl : NULL)) != 0)
		return (cleanup_set_errno(hdl, nvl, NULL, err));

	nvlist_free(nvl);
	if (outnvl != NULL) {
		if (statusp != NULL)
			(void) nvlist_lookup_int32(outnvl,
			    FM_CLR_STATUS, statusp);
		nvlist_free(outnvl);
	}

	return (0);
}

static int
fmd_agent_cacheop(fmd_agent_hdl_t *hdl, int cmd, uint32_t strandid,
    nvlist_t *fmri, int *status)
{
	uint32_t ver;

	if (fmd_agent_version(hdl, FM_CACHE_OP_VERSION, &ver) == -1)
		return (cleanup_set_errno(hdl, NULL, NULL, errno));

	switch (ver) {
	case 1:
		return (fmd_agent_cacheop_v1(hdl, cmd, strandid, fmri, status));

	default:
		return (fmd_agent_seterrno(hdl, ENOTSUP));
	}
}

int
fmd_agent_cache_retire(fmd_agent_hdl_t *hdl, uint32_t strandid, nvlist_t *fmri)
{
	int ret;

	ret = fmd_agent_cacheop(hdl, FM_IOC_CACHE_RETIRE, strandid, fmri, NULL);

	if (ret == 0)
		return (FMD_AGENT_RETIRE_DONE);
	else if (errno == EAGAIN)
		return (FMD_AGENT_RETIRE_ASYNC);
	else
		return (FMD_AGENT_RETIRE_FAIL);
}

int
fmd_agent_cache_unretire(fmd_agent_hdl_t *hdl, uint32_t strandid,
    nvlist_t *fmri)
{
	int ret;

	ret = fmd_agent_cacheop(hdl, FM_IOC_CACHE_UNRETIRE,
	    strandid, fmri, NULL);

	return (ret == 0 ? FMD_AGENT_RETIRE_DONE : FMD_AGENT_RETIRE_FAIL);
}

int
fmd_agent_cache_isretired(fmd_agent_hdl_t *hdl, uint32_t strandid,
    nvlist_t *fmri)
{
	int status;

	if (fmd_agent_cacheop(hdl, FM_IOC_CACHE_STATUS,
	    strandid, fmri, &status) != 0)
		return (-1);

	return (status == FM_CLR_STATUS_RETIRED ? FMD_AGENT_RETIRE_DONE :
	    FMD_AGENT_RETIRE_FAIL);
}

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

#include <cma.h>
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <fm/fmd_api.h>
#include <sys/fm/protocol.h>
#include <fm/fmd_agent.h>

#ifndef sun4v
/* cache way retire for Panther */
#include <sys/mem_cache.h>

int
/* ARGSUSED 4 */
cma_cache_way_retire(fmd_hdl_t *hdl, nvlist_t *nvl, nvlist_t *asru,
    const char *uuid, boolean_t repair)
{
	uint_t 		cpuid;
	uint32_t 	way;
	uint32_t	index;
	uint16_t	bit = 0;
	uint8_t		type;
	cache_info_t    cache_info;
	int ret, fd;
	int	err;

	fmd_hdl_debug(hdl, "cpu cache *line* fault processing\n");
	fmd_hdl_debug(hdl, "asru %lx\n", asru);

	if (nvlist_lookup_uint32(asru, FM_FMRI_CPU_ID, &cpuid) != 0) {
		fmd_hdl_debug(hdl, "cpu cache fault missing '%s'\n",
		    FM_FMRI_CPU_ID);
		cma_stats.bad_flts.fmds_value.ui64++;
		return (CMA_RA_FAILURE);
	}

	if (nvlist_lookup_uint32(asru, FM_FMRI_CPU_CACHE_INDEX, &index) != 0) {
		fmd_hdl_debug(hdl, "cpu cache fault missing '%s'\n",
		    FM_FMRI_CPU_CACHE_INDEX);
		cma_stats.bad_flts.fmds_value.ui64++;
		return (CMA_RA_FAILURE);
	}

	if (nvlist_lookup_uint32(asru, FM_FMRI_CPU_CACHE_WAY, &way) != 0) {
		fmd_hdl_debug(hdl, "cpu cache fault missing '%s'\n",
		    FM_FMRI_CPU_CACHE_WAY);
		cma_stats.bad_flts.fmds_value.ui64++;
		return (CMA_RA_FAILURE);
	}

	if (nvlist_lookup_uint8(asru, FM_FMRI_CPU_CACHE_TYPE, &type) != 0) {
		fmd_hdl_debug(hdl, "cpu cache fault missing '%s'\n",
		    FM_FMRI_CPU_CACHE_TYPE);
		cma_stats.bad_flts.fmds_value.ui64++;
		return (CMA_RA_FAILURE);
	}

	/*
	 * Tag faults will use it to set the bit to a stable state
	 */

	if (nvlist_lookup_uint16(asru, FM_FMRI_CPU_CACHE_BIT, &bit) != 0) {
		fmd_hdl_debug(hdl, "cpu cache fault missing '%s'\n",
		    FM_FMRI_CPU_CACHE_BIT);
		cma_stats.bad_flts.fmds_value.ui64++;
		return (CMA_RA_FAILURE);
	}
	if (repair) {
		fmd_hdl_debug(hdl,
		    "cpu %d: Unretire for index %d, way %d\n bit %d"
		    " type 0x%02x is being called now. We will not unretire"
		    " cacheline. This message is for information.\n",
		    cpuid, index, way, bit, type);
		/*
		 * The DE will do the actual unretire.
		 * The agent is called because the DE informs the fmd
		 * that the resource is repaired.
		 */
		return (CMA_RA_SUCCESS);
	}
	fmd_hdl_debug(hdl,
	    "cpu %d: Retiring index %d, way %d\n bit %d"
	    " type 0x%02x", cpuid, index, way, bit, type);
	fd = open(mem_cache_device, O_RDWR);
	if (fd == -1) {
		fmd_hdl_debug(hdl, "Driver open failed\n");
		return (CMA_RA_FAILURE);
	}
	cache_info.cpu_id = cpuid;
	cache_info.way = way;
	cache_info.bit = bit;
	cache_info.index = index;
	cache_info.cache = type == FM_FMRI_CPU_CACHE_TYPE_L3 ?
	    L3_CACHE_DATA : L2_CACHE_DATA;

	ret = ioctl(fd, MEM_CACHE_RETIRE, &cache_info);
	/*
	 * save errno before we call close.
	 * Need errno to display the error if ioctl fails.
	 */
	err = errno;
	(void) close(fd);
	if (ret == -1) {
		fmd_hdl_debug(hdl, "Driver call failed errno = %d\n", err);
		return (CMA_RA_FAILURE);
	}
	return (CMA_RA_SUCCESS);
}

#endif /* !sun4v */

/* ARGSUSED */
int
cma_cacheline_retire(fmd_hdl_t *hdl, nvlist_t *nvl, nvlist_t *asru,
    const char *uuid, boolean_t repair)
{
	char fmristr[256] = "(unknown)";
	char *action = repair ? "unretire" : "retire";
	cma_cacheline_t *clp;
	int rc;

	(void) fmd_nvl_fmri_nvl2str(hdl, asru, fmristr, sizeof (fmristr));

	if ((repair && !cma.cma_cacheline_dounretire) ||
	    (!repair && !cma.cma_cacheline_doretire)) {
		fmd_hdl_debug(hdl,
		    "suppressed %s of cacheline %s\n", action, fmristr);
		cma_stats.cacheline_supp.fmds_value.ui64++;
		return (CMA_RA_FAILURE);
	}

	fmd_hdl_debug(hdl, "starting %s of %s\n", action, fmristr);

	rc = repair ? fmd_nvl_fmri_unretire(hdl, asru) :
	    fmd_nvl_fmri_retire(hdl, asru);

	if (rc == FMD_AGENT_RETIRE_DONE) {
		fmd_hdl_debug(hdl, "%sd cacheline %s\n", action, fmristr);
		if (repair)
			cma_stats.cacheline_repairs.fmds_value.ui64++;
		else
			cma_stats.cacheline_flts.fmds_value.ui64++;
		return (CMA_RA_SUCCESS);
	} else if (repair || rc != FMD_AGENT_RETIRE_ASYNC) {
		fmd_hdl_debug(hdl, "%s of cacheline %s failed, will not "
		    "retry: %s\n", action, fmristr, strerror(errno));

		cma_stats.cacheline_fails.fmds_value.ui64++;
		return (CMA_RA_FAILURE);
	}

	/*
	 * The cacheline didn't immediately retire.  We'll need to periodically
	 * check to see if it has been retired.
	 */
	fmd_hdl_debug(hdl, "cacheline didn't retire - sleeping\n");

	clp = fmd_hdl_zalloc(hdl, sizeof (cma_cacheline_t), FMD_SLEEP);
	if (nvlist_dup(asru, &clp->cl_fmri, 0) !=  0) {
		fmd_hdl_debug(hdl, "failed to duplicate nvlist\n");
		fmd_hdl_free(hdl, clp, sizeof (cma_cacheline_t));
		return (CMA_RA_FAILURE);
	}
	clp->cl_fmristr = fmd_hdl_strdup(hdl, fmristr, FMD_SLEEP);
	if (uuid != NULL)
		clp->cl_uuid = fmd_hdl_strdup(hdl, uuid, FMD_SLEEP);

	clp->cl_next = cma.cma_cachelines;
	cma.cma_cachelines = clp;

	if (cma.cma_page_timerid != 0)
		fmd_timer_remove(hdl, cma.cma_page_timerid);

	cma.cma_cacheline_curdelay = cma.cma_cacheline_mindelay;

	cma.cma_cacheline_timerid =
	    fmd_timer_install(hdl, NULL, NULL, cma.cma_cacheline_curdelay);

	return (CMA_RA_FAILURE);
}

static int
cacheline_retry(fmd_hdl_t *hdl, cma_cacheline_t *clp)
{
	int rc;

	if (!fmd_nvl_fmri_has_fault(hdl, clp->cl_fmri, FMD_HAS_FAULT_ASRU |
	    FMD_HAS_FAULT_RETIRE, NULL)) {
		fmd_hdl_debug(hdl, "cacheline retire overtaken by events");
		cma_stats.cacheline_nonent.fmds_value.ui64++;

		if (clp->cl_uuid != NULL)
			fmd_case_uuclose(hdl, clp->cl_uuid);
		return (1); /* no longer a cacheline to retire */
	}

	rc = fmd_nvl_fmri_retire(hdl, clp->cl_fmri);

	if (rc == FMD_AGENT_RETIRE_DONE) {
		fmd_hdl_debug(hdl, "retired cacheline %s on retry %u\n",
		    clp->cl_fmristr, clp->cl_nretries);
		cma_stats.cacheline_flts.fmds_value.ui64++;

		if (clp->cl_uuid != NULL)
			fmd_case_uuclose(hdl, clp->cl_uuid);
		return (1); /* cacheline retired */
	}

	if (rc == FMD_AGENT_RETIRE_FAIL) {
		fmd_hdl_debug(hdl, "failed to retry cacheline %s: %s\n",
		    clp->cl_fmristr, strerror(errno));

		cma_stats.cacheline_fails.fmds_value.ui64++;
		return (1); /* give up */
	}

	return (0);
}

static void
cma_cacheline_free(fmd_hdl_t *hdl, cma_cacheline_t *clp)
{
	nvlist_free(clp->cl_fmri);
	fmd_hdl_strfree(hdl, clp->cl_fmristr);
	fmd_hdl_strfree(hdl, clp->cl_uuid);
	fmd_hdl_free(hdl, clp, sizeof (cma_cacheline_t));
}

void
cma_cacheline_retry(fmd_hdl_t *hdl)
{
	cma_cacheline_t **clpp;

	cma.cma_cacheline_timerid = 0;

	fmd_hdl_debug(hdl, "cma_cacheline_retry: timer fired\n");

	clpp = &cma.cma_cachelines;
	while (*clpp != NULL) {
		cma_cacheline_t *clp = *clpp;

		if (cacheline_retry(hdl, clp)) {
			/*
			 * Successful retry or we're giving up - remove from
			 * the list
			 */
			*clpp = clp->cl_next;
			cma_cacheline_free(hdl, clp);
		} else {
			clp->cl_nretries++;
			clpp = &clp->cl_next;
		}
	}

	if (cma.cma_cachelines == NULL)
		return; /* no more retirements */

	/*
	 * We still have retirements that haven't completed.  Back the delay
	 * off, and schedule a retry.
	 */
	cma.cma_cacheline_curdelay = MIN(cma.cma_cacheline_curdelay * 2,
	    cma.cma_cacheline_maxdelay);

	fmd_hdl_debug(hdl,
	    "scheduled cacheline retirement retry for %llu secs\n",
	    (u_longlong_t)(cma.cma_cacheline_curdelay / NANOSEC));

	cma.cma_cacheline_timerid =
	    fmd_timer_install(hdl, NULL, NULL, cma.cma_cacheline_curdelay);
}

void
cma_cacheline_fini(fmd_hdl_t *hdl)
{
	cma_cacheline_t *clp;

	while ((clp = cma.cma_cachelines) != NULL) {
		cma.cma_cachelines = clp->cl_next;
		cma_cacheline_free(hdl, clp);
	}
}

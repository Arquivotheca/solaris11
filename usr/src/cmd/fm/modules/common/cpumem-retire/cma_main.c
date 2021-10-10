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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <cma.h>

#include <unistd.h>
#include <fcntl.h>
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <fm/fmd_api.h>
#include <sys/fm/protocol.h>
#include <sys/systeminfo.h>
#include <sys/utsname.h>

#ifdef sun4v
#include <sys/fm/ldom.h>

static fmd_hdl_t *init_hdl;
ldom_hdl_t *cma_lhp;
#endif

extern const char *fmd_fmri_get_platform();

cma_t cma;

cma_stats_t cma_stats = {
	{ "cpu_flts", FMD_TYPE_UINT64, "cpu faults resolved" },
	{ "cpu_repairs", FMD_TYPE_UINT64, "cpu faults repaired" },
	{ "cpu_fails", FMD_TYPE_UINT64, "cpu faults unresolveable" },
	{ "cpu_blfails", FMD_TYPE_UINT64, "failed cpu blacklists" },
	{ "cpu_supp", FMD_TYPE_UINT64, "cpu offlines suppressed" },
	{ "cpu_blsupp", FMD_TYPE_UINT64, "cpu blacklists suppressed" },
	{ "page_flts", FMD_TYPE_UINT64, "page faults resolved" },
	{ "page_repairs", FMD_TYPE_UINT64, "page faults repaired" },
	{ "page_fails", FMD_TYPE_UINT64, "page faults unresolveable" },
	{ "page_supp", FMD_TYPE_UINT64, "page retires suppressed" },
	{ "page_nonent", FMD_TYPE_UINT64, "retires for non-existent fmris" },
	{ "cacheline_flts", FMD_TYPE_UINT64, "cacheline faults resolved" },
	{ "cacheline_repairs", FMD_TYPE_UINT64, "cacheline faults repaired" },
	{ "cacheline_fails", FMD_TYPE_UINT64, "cacheline faults unresolveable"},
	{ "cacheline_supp", FMD_TYPE_UINT64, "cacheline offlines suppressed" },
	{ "cacheline_nonent", FMD_TYPE_UINT64, "non-existent retires" },
	{ "bad_flts", FMD_TYPE_UINT64, "invalid fault events received" },
	{ "nop_flts", FMD_TYPE_UINT64, "inapplicable fault events received" },
	{ "auto_flts", FMD_TYPE_UINT64, "auto-close faults received" }
};

typedef struct cma_subscriber {
	const char *subr_class;
	const char *subr_sname;
	uint_t subr_svers;
	int (*subr_func)(fmd_hdl_t *, nvlist_t *, nvlist_t *, const char *,
	    boolean_t);
} cma_subscriber_t;

static const cma_subscriber_t cma_subrs[] = {
#if defined(i386)
	/*
	 * On x86, the ASRUs are expected to be in hc scheme.  When
	 * cpumem-retire wants to retire a cpu or mem page, it calls the
	 * methods registered in the topo node to do that.  The topo
	 * enumerator, which necessarily knows all the config info that
	 * we'd ever need in deciding what/how to retire etc.  This takes
	 * away much of that complexity from the agent into the entity
	 * that knows all config/topo information.
	 */
	{ "fault.memory.page", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    cma_page_retire },
	{ "fault.memory.page_sb", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    cma_page_retire },
	{ "fault.memory.page_ck", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    cma_page_retire },
	{ "fault.memory.page_ue", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    cma_page_retire },
	{ "fault.memory.generic-x86.page_ce", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, cma_page_retire },
	{ "fault.memory.generic-x86.page_ue", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, cma_page_retire },
	{ "fault.memory.intel.page_ce", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, cma_page_retire },
	{ "fault.memory.intel.page_ue", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, cma_page_retire },
	{ "fault.memory.dimm", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm_sb", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm_ck", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm_ue", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.generic-x86.dimm_ce", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.generic-x86.dimm_ue", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.intel.dimm_ce", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.intel.dimm_ue", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.intel.fbd.*", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.dimm_testfail", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.bank", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.datapath", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.cpu.intel.quickpath.mem_scrubbing", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, cma_page_retire },
	{ "fault.cpu.intel.quickpath.*", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.cpu.generic-x86.mc", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.cpu.intel.dma", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.cpu.intel.dma", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },

	/*
	 * The ASRU for cpu faults are in cpu scheme on native and in hc
	 * scheme on xpv.  So each cpu fault class needs to be listed twice.
	 */

	/*
	 * The following faults do NOT retire a cpu thread,
	 * and therefore must be intercepted before
	 * the default "fault.cpu.*" dispatch to cma_cpu_hc_retire.
	 */
	{ "fault.cpu.amd.dramchannel", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.cpu.amd.dramchannel", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.generic-x86.bus_interconnect_memory", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.cpu.generic-x86.bus_interconnect_memory", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.generic-x86.bus_interconnect_io", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.cpu.generic-x86.bus_interconnect_io", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.generic-x86.bus_interconnect", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.cpu.generic-x86.bus_interconnect", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.intel.bus_interconnect_memory", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.cpu.intel.bus_interconnect_memory", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.intel.bus_interconnect_io", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.cpu.intel.bus_interconnect_io", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.intel.bus_interconnect", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.cpu.intel.bus_interconnect", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.intel.nb.*", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.cpu.intel.nb.*", FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    NULL },
	{ "fault.cpu.intel.dma", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.cpu.intel.dma", FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    NULL },
	{ "fault.cpu.*", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    cma_cpu_hc_retire },
	{ "fault.cpu.*", FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    cma_cpu_hc_retire },
#elif defined(sun4v)
	/*
	 * The following are PI sun4v faults
	 */
	{ "fault.memory.memlink", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.memlink-uc", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.memlink-failover", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.dimm-ue-imminent", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.dram-ue-imminent", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.dimm-page-retires-excessive", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.memory.page", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    cma_page_retire },
	{ "fault.memory.dimm", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm_sb", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm_ck", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm_ue", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm-page-retires-excessive", FM_FMRI_SCHEME_MEM,
	    FM_MEM_SCHEME_VERSION, NULL },
	{ "fault.memory.dimm-ue-imminent", FM_FMRI_SCHEME_MEM,
	    FM_MEM_SCHEME_VERSION, NULL },
	{ "fault.memory.dram-ue-imminent", FM_FMRI_SCHEME_MEM,
	    FM_MEM_SCHEME_VERSION, NULL },
	{ "fault.memory.bank", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.datapath", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.datapath", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.link-c", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.link-u", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.link-f", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.link-c", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.link-u", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.link-f", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },

	/*
	 * The following ultraSPARC-T1/T2 faults do NOT retire a cpu thread,
	 * and therefore must be intercepted before
	 * the default "fault.cpu.*" dispatch to cma_cpu_hc_retire.
	 */
	{ "fault.cpu.*.l2cachedata", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.*.l2cachetag", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.*.l2cachectl", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.*.l2data-c", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.*.l2data-u", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.*.mau", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.*.lfu-u", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.*.lfu-f", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.*.lfu-p", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.ultraSPARC-T1.freg", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.ultraSPARC-T1.l2cachedata", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.ultraSPARC-T1.l2cachetag", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.ultraSPARC-T1.l2cachectl", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.ultraSPARC-T1.mau", FM_FMRI_SCHEME_CPU,
	    FM_CPU_SCHEME_VERSION, NULL },
	{ "fault.cpu.ultraSPARC-T2plus.chip", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, NULL },
	{ "fault.cpu.generic-sparc.cacheline", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, cma_cacheline_retire },
	{ "fault.cpu.generic-sparc.cacheline-uc", FM_FMRI_SCHEME_HC,
	    FM_HC_SCHEME_VERSION, cma_cacheline_retire },
	{ "fault.cpu.*", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    cma_cpu_hc_retire },
	{ "fault.cpu.*", FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    cma_cpu_hc_retire },
#elif defined(opl)
	{ "fault.memory.page", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    cma_page_retire },
	{ "fault.memory.dimm", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm-page-retires-excessive", FM_FMRI_SCHEME_MEM,
	    FM_MEM_SCHEME_VERSION, NULL },
	{ "fault.memory.dimm-ue-imminent", FM_FMRI_SCHEME_MEM,
	    FM_MEM_SCHEME_VERSION, NULL },
	{ "fault.memory.dram-ue-imminent", FM_FMRI_SCHEME_MEM,
	    FM_MEM_SCHEME_VERSION, NULL },
	{ "fault.memory.bank", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.cpu.SPARC64-VI.*", FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    cma_cpu_cpu_retire },
	{ "fault.cpu.SPARC64-VII.*", FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    cma_cpu_cpu_retire },
	{ "fault.chassis.SPARC-Enterprise.cpu.SPARC64-VI.core.se",
		FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION, cma_cpu_hc_retire },
	{ "fault.chassis.SPARC-Enterprise.cpu.SPARC64-VI.core.se-offlinereq",
		FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION, cma_cpu_hc_retire },
	{ "fault.chassis.SPARC-Enterprise.cpu.SPARC64-VI.core.ce",
		FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION, cma_cpu_hc_retire },
	{ "fault.chassis.SPARC-Enterprise.cpu.SPARC64-VI.core.ce-offlinereq",
		FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION, cma_cpu_hc_retire },
	{ "fault.chassis.SPARC-Enterprise.cpu.SPARC64-VII.core.se",
		FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION, cma_cpu_hc_retire },
	{ "fault.chassis.SPARC-Enterprise.cpu.SPARC64-VII.core.se-offlinereq",
		FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION, cma_cpu_hc_retire },
	{ "fault.chassis.SPARC-Enterprise.cpu.SPARC64-VII.core.ce",
		FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION, cma_cpu_hc_retire },
	{ "fault.chassis.SPARC-Enterprise.cpu.SPARC64-VII.core.ce-offlinereq",
		FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION, cma_cpu_hc_retire },
#else
	/*
	 * For platforms excluding i386, sun4v and opl.
	 */
	{ "fault.memory.page", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    cma_page_retire },
	{ "fault.memory.page_sb", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    cma_page_retire },
	{ "fault.memory.page_ck", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    cma_page_retire },
	{ "fault.memory.page_ue", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    cma_page_retire },
	{ "fault.memory.dimm", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm_sb", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm_ck", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm_ue", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.dimm-page-retires-excessive", FM_FMRI_SCHEME_MEM,
	    FM_MEM_SCHEME_VERSION, NULL },
	{ "fault.memory.dimm-ue-imminent", FM_FMRI_SCHEME_MEM,
	    FM_MEM_SCHEME_VERSION, NULL },
	{ "fault.memory.dram-ue-imminent", FM_FMRI_SCHEME_MEM,
	    FM_MEM_SCHEME_VERSION, NULL },
	{ "fault.memory.dimm_testfail", FM_FMRI_SCHEME_MEM,
	    FM_MEM_SCHEME_VERSION, NULL },
	{ "fault.memory.bank", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.datapath", FM_FMRI_SCHEME_MEM, FM_MEM_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.datapath", FM_FMRI_SCHEME_HC, FM_HC_SCHEME_VERSION,
	    NULL },
	{ "fault.memory.datapath", FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    NULL },

	/*
	 * The following faults do NOT retire a cpu thread,
	 * and therefore must be intercepted before
	 * the default "fault.cpu.*" dispatch to cma_cpu_cpu_retire.
	 */
	{ "fault.cpu.ultraSPARC-IVplus.l2cachedata-line",
	    FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    cma_cache_way_retire },
	{ "fault.cpu.ultraSPARC-IVplus.l3cachedata-line",
	    FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    cma_cache_way_retire },
	{ "fault.cpu.ultraSPARC-IVplus.l2cachetag-line",
	    FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    cma_cache_way_retire },
	{ "fault.cpu.ultraSPARC-IVplus.l3cachetag-line",
	    FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    cma_cache_way_retire },

	/*
	 * Default "fault.cpu.*" for "cpu" scheme ASRU dispatch.
	 */
	{ "fault.cpu.*", FM_FMRI_SCHEME_CPU, FM_CPU_SCHEME_VERSION,
	    cma_cpu_cpu_retire },
#endif
	{ NULL, NULL, 0, NULL }
};

static const cma_subscriber_t *
nvl2subr(fmd_hdl_t *hdl, nvlist_t *nvl, nvlist_t **asrup)
{
	const cma_subscriber_t *sp;
	nvlist_t *asru;
	char *scheme;
	uint8_t version;
	boolean_t retire;

	if (nvlist_lookup_boolean_value(nvl, FM_SUSPECT_RETIRE, &retire) == 0 &&
	    retire == 0) {
		fmd_hdl_debug(hdl, "cma_recv: retire suppressed");
		return (NULL);
	}

	if (nvlist_lookup_nvlist(nvl, FM_FAULT_ASRU, &asru) != 0 ||
	    nvlist_lookup_string(asru, FM_FMRI_SCHEME, &scheme) != 0 ||
	    nvlist_lookup_uint8(asru, FM_VERSION, &version) != 0) {
		cma_stats.bad_flts.fmds_value.ui64++;
		return (NULL);
	}

	for (sp = cma_subrs; sp->subr_class != NULL; sp++) {
		if (fmd_nvl_class_match(hdl, nvl, sp->subr_class) &&
		    strcmp(scheme, sp->subr_sname) == 0 &&
		    version <= sp->subr_svers) {
			*asrup = asru;
			return (sp);
		}
	}

	cma_stats.nop_flts.fmds_value.ui64++;
	return (NULL);
}

static void
cma_recv_list(fmd_hdl_t *hdl, nvlist_t *nvl, const char *class)
{
	char *uuid = NULL;
	nvlist_t **nva, **save_nva;
	uint_t nvc = 0, save_nvc;
	uint_t keepopen;
	int err = 0;
	nvlist_t *asru = NULL;
	uint32_t index;

	err |= nvlist_lookup_string(nvl, FM_SUSPECT_UUID, &uuid);
	err |= nvlist_lookup_nvlist_array(nvl, FM_SUSPECT_FAULT_LIST,
	    &nva, &nvc);
	if (err != 0) {
		cma_stats.bad_flts.fmds_value.ui64++;
		return;
	}

	save_nvc = keepopen = nvc;
	save_nva = nva;
	while (nvc-- != 0 && (strcmp(class, FM_LIST_SUSPECT_CLASS) != 0 ||
	    !fmd_case_uuclosed(hdl, uuid))) {
		nvlist_t *nvl = *nva++;
		const cma_subscriber_t *subr;
		int has_fault;

		if ((subr = nvl2subr(hdl, nvl, &asru)) == NULL)
			continue;

		/*
		 * A handler returns CMA_RA_SUCCESS to indicate that
		 * from this suspects  point-of-view the case may be
		 * closed, CMA_RA_FAILURE otherwise.
		 * A handler must not close the case itself.
		 */
		if (subr->subr_func != NULL) {
			has_fault = fmd_nvl_fmri_has_fault(hdl, asru,
			    FMD_HAS_FAULT_ASRU | FMD_HAS_FAULT_RETIRE, NULL);
			if (strcmp(class, FM_LIST_SUSPECT_CLASS) == 0) {
				if (has_fault == 1)
					err = subr->subr_func(hdl, nvl, asru,
					    uuid, 0);
			} else {
				if (has_fault == 0)
					err = subr->subr_func(hdl, nvl, asru,
					    uuid, 1);
			}
			if (err == CMA_RA_SUCCESS)
				keepopen--;
		}
	}

	/*
	 * Run though again to catch any new faults in list.updated.
	 */
	while (save_nvc-- != 0 && (strcmp(class, FM_LIST_UPDATED_CLASS) == 0)) {
		nvlist_t *nvl = *save_nva++;
		const cma_subscriber_t *subr;
		int has_fault;

		if ((subr = nvl2subr(hdl, nvl, &asru)) == NULL)
			continue;
		if (subr->subr_func != NULL) {
			has_fault = fmd_nvl_fmri_has_fault(hdl, asru,
			    FMD_HAS_FAULT_ASRU | FMD_HAS_FAULT_RETIRE, NULL);
			if (has_fault == 1)
				err = subr->subr_func(hdl, nvl, asru, uuid, 0);
		}
	}

	/*
	 * Do not close the case if we are handling Panther cache faults.
	 */
	if (asru != NULL) {
		if (nvlist_lookup_uint32(asru, FM_FMRI_CPU_CACHE_INDEX,
		    &index) != 0) {
			if (!keepopen && strcmp(class,
			    FM_LIST_SUSPECT_CLASS) == 0) {
				fmd_case_uuclose(hdl, uuid);
			}
		}
	}

	if (!keepopen && strcmp(class, FM_LIST_REPAIRED_CLASS) == 0)
		fmd_case_uuresolved(hdl, uuid);
}

static void
cma_recv_one(fmd_hdl_t *hdl, nvlist_t *nvl)
{
	const cma_subscriber_t *subr;
	nvlist_t *asru;

	if ((subr = nvl2subr(hdl, nvl, &asru)) == NULL)
		return;

	if (subr->subr_func != NULL) {
		if (fmd_nvl_fmri_has_fault(hdl, asru,
		    FMD_HAS_FAULT_ASRU | FMD_HAS_FAULT_RETIRE, NULL) == 1)
			(void) subr->subr_func(hdl, nvl, asru, NULL, 0);
	}
}

/*ARGSUSED*/
static void
cma_recv(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl, const char *class)
{
	fmd_hdl_debug(hdl, "received %s\n", class);

	if (strcmp(class, FM_LIST_RESOLVED_CLASS) == 0)
		return;

	if (strcmp(class, FM_LIST_SUSPECT_CLASS) == 0 ||
	    strcmp(class, FM_LIST_REPAIRED_CLASS) == 0 ||
	    strcmp(class, FM_LIST_UPDATED_CLASS) == 0)
		cma_recv_list(hdl, nvl, class);
	else
		cma_recv_one(hdl, nvl);
}

/*ARGSUSED*/
static void
cma_timeout(fmd_hdl_t *hdl, id_t id, void *arg)
{
	if (id == cma.cma_page_timerid)
		cma_page_retry(hdl);
#ifdef sun4v
	else if (id == cma.cma_cacheline_timerid)
		cma_cacheline_retry(hdl);
	/*
	 * cpu offline/online needs to be retried on sun4v because
	 * ldom request can be asynchronous.
	 */
	else if (id == cma.cma_cpu_timerid)
		cma_cpu_retry(hdl);
#endif
}

#ifdef sun4v
static void *
cma_init_alloc(size_t size)
{
	return (fmd_hdl_alloc(init_hdl, size, FMD_SLEEP));
}

static void
cma_init_free(void *addr, size_t size)
{
	fmd_hdl_free(init_hdl, addr, size);
}
#endif

static const fmd_hdl_ops_t fmd_ops = {
	cma_recv,	/* fmdo_recv */
	cma_timeout,	/* fmdo_timeout */
	NULL,		/* fmdo_close */
	NULL,		/* fmdo_stats */
	NULL,		/* fmdo_gc */
};

static const fmd_prop_t fmd_props[] = {
	{ "cpu_tries", FMD_TYPE_UINT32, "10" },
	{ "cpu_delay", FMD_TYPE_TIME, "1sec" },
#ifdef sun4v
	{ "cpu_ret_mindelay", FMD_TYPE_TIME, "5sec" },
	{ "cpu_ret_maxdelay", FMD_TYPE_TIME, "5min" },
	{ "cacheline_retire_enable", FMD_TYPE_BOOL, "true" },
	{ "cacheline_unretire_enable", FMD_TYPE_BOOL, "true" },
	{ "cacheline_ret_mindelay", FMD_TYPE_TIME, "1sec" },
	{ "cacheline_ret_maxdelay", FMD_TYPE_TIME, "5min" },
#endif /* sun4v */
	{ "cpu_offline_enable", FMD_TYPE_BOOL, "true" },
	{ "cpu_online_enable", FMD_TYPE_BOOL, "true" },
	{ "cpu_forced_offline", FMD_TYPE_BOOL, "true" },
#ifdef opl
	{ "cpu_blacklist_enable", FMD_TYPE_BOOL, "false" },
	{ "cpu_unblacklist_enable", FMD_TYPE_BOOL, "false" },
#else
	{ "cpu_blacklist_enable", FMD_TYPE_BOOL, "true" },
	{ "cpu_unblacklist_enable", FMD_TYPE_BOOL, "true" },
#endif /* opl */
	{ "page_ret_mindelay", FMD_TYPE_TIME, "1sec" },
	{ "page_ret_maxdelay", FMD_TYPE_TIME, "5min" },
	{ "page_retire_enable", FMD_TYPE_BOOL, "true" },
	{ "page_unretire_enable", FMD_TYPE_BOOL, "true" },
	{ NULL, 0, NULL }
};

static const fmd_hdl_info_t fmd_info = {
	"CPU/Memory Retire Agent", CMA_VERSION, &fmd_ops, fmd_props
};

void
_fmd_init(fmd_hdl_t *hdl)
{
	hrtime_t nsec;
#ifdef i386
	char buf[BUFSIZ];

	/*
	 * Abort the cpumem-retire module if Solaris is running under DomU.
	 */
	if (sysinfo(SI_PLATFORM, buf, sizeof (buf)) == -1)
		return;

	if (strncmp(buf, "i86xpv", sizeof (buf)) == 0)
		return;
#endif /* i386 */

	if (fmd_hdl_register(hdl, FMD_API_VERSION, &fmd_info) != 0)
		return; /* invalid data in configuration file */

	fmd_hdl_subscribe(hdl, "fault.cpu.*");
	fmd_hdl_subscribe(hdl, "fault.memory.*");
#ifdef opl
	fmd_hdl_subscribe(hdl, "fault.chassis.SPARC-Enterprise.cpu.*");
#endif

	(void) fmd_stat_create(hdl, FMD_STAT_NOALLOC, sizeof (cma_stats) /
	    sizeof (fmd_stat_t), (fmd_stat_t *)&cma_stats);

	cma.cma_cpu_tries = fmd_prop_get_int32(hdl, "cpu_tries");

	nsec = fmd_prop_get_int64(hdl, "cpu_delay");
	cma.cma_cpu_delay.tv_sec = nsec / NANOSEC;
	cma.cma_cpu_delay.tv_nsec = nsec % NANOSEC;

	cma.cma_page_mindelay = fmd_prop_get_int64(hdl, "page_ret_mindelay");
	cma.cma_page_maxdelay = fmd_prop_get_int64(hdl, "page_ret_maxdelay");

#ifdef sun4v
	cma.cma_cpu_mindelay = fmd_prop_get_int64(hdl, "cpu_ret_mindelay");
	cma.cma_cpu_maxdelay = fmd_prop_get_int64(hdl, "cpu_ret_maxdelay");
	cma.cma_cacheline_doretire = fmd_prop_get_int32(hdl,
	    "cacheline_retire_enable");
	cma.cma_cacheline_dounretire = fmd_prop_get_int32(hdl,
	    "cacheline_unretire_enable");
	cma.cma_cacheline_mindelay = fmd_prop_get_int64(hdl,
	    "cacheline_ret_mindelay");
	cma.cma_cacheline_maxdelay = fmd_prop_get_int64(hdl,
	    "cacheline_ret_maxdelay");
#endif

	cma.cma_cpu_dooffline = fmd_prop_get_int32(hdl, "cpu_offline_enable");
	cma.cma_cpu_forcedoffline = fmd_prop_get_int32(hdl,
	    "cpu_forced_offline");
	cma.cma_cpu_doonline = fmd_prop_get_int32(hdl, "cpu_online_enable");
	cma.cma_cpu_doblacklist = fmd_prop_get_int32(hdl,
	    "cpu_blacklist_enable");
	cma.cma_cpu_dounblacklist = fmd_prop_get_int32(hdl,
	    "cpu_unblacklist_enable");
	cma.cma_page_doretire = fmd_prop_get_int32(hdl, "page_retire_enable");
	cma.cma_page_dounretire = fmd_prop_get_int32(hdl,
	    "page_unretire_enable");

	if (cma.cma_page_maxdelay < cma.cma_page_mindelay)
		fmd_hdl_abort(hdl, "page retirement delays conflict\n");
	if (cma.cma_cacheline_maxdelay < cma.cma_cacheline_mindelay)
		fmd_hdl_abort(hdl, "cacheline retirement delays conflict\n");

#ifdef sun4v
	init_hdl = hdl;
	cma_lhp = ldom_init(cma_init_alloc, cma_init_free);
#endif
}

void
_fmd_fini(fmd_hdl_t *hdl)
{
#ifdef sun4v
	ldom_fini(cma_lhp);
	cma_cpu_fini(hdl);
	cma_cacheline_fini(hdl);
#endif
	cma_page_fini(hdl);
}

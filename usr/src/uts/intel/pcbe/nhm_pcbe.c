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
 * This file contains preset event names from the Performance Application
 * Programming Interface v3.5 which included the following notice:
 *
 *                             Copyright (c) 2005,6
 *                           Innovative Computing Labs
 *                         Computer Science Department,
 *                            University of Tennessee,
 *                                 Knoxville, TN.
 *                              All Rights Reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the University of Tennessee nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * This open source software license conforms to the BSD License template.
 */


/*
 * Performance Counter Back-End for Intel processors supporting Architectural
 * Performance Monitoring.
 */

#include <sys/cpuvar.h>
#include <sys/param.h>
#include <sys/cpc_impl.h>
#include <sys/cpc_pcbe.h>
#include <sys/modctl.h>
#include <sys/inttypes.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/x86_archext.h>
#include <sys/sdt.h>
#include <sys/archsystm.h>
#include <sys/privregs.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cred.h>
#include <sys/policy.h>
#include "nhm_pcbe.h"

static int core_pcbe_init(void);
static uint_t core_pcbe_ncounters(void);
static const char *core_pcbe_impl_name(void);
static const char *core_pcbe_cpuref(void);
static char *core_pcbe_list_events(uint_t picnum);
static char *core_pcbe_list_attrs(void);
static uint64_t core_pcbe_event_coverage(char *event);
static uint64_t core_pcbe_overflow_bitmap(void);
static int core_pcbe_configure(uint_t picnum, char *event, uint64_t preset,
    uint32_t flags, uint_t nattrs, kcpc_attr_t *attrs, void **data,
    void *token);
static void core_pcbe_program(void *token);
static void core_pcbe_allstop(void);
static void core_pcbe_sample(void *token);
static void core_pcbe_free(void *config);

pcbe_ops_t core_pcbe_ops = {
	PCBE_VER_1,			/* pcbe_ver */
	CPC_CAP_OVERFLOW_INTERRUPT | CPC_CAP_OVERFLOW_PRECISE,	/* pcbe_caps */
	core_pcbe_ncounters,		/* pcbe_ncounters */
	core_pcbe_impl_name,		/* pcbe_impl_name */
	core_pcbe_cpuref,		/* pcbe_cpuref */
	core_pcbe_list_events,		/* pcbe_list_events */
	core_pcbe_list_attrs,		/* pcbe_list_attrs */
	core_pcbe_event_coverage,	/* pcbe_event_coverage */
	core_pcbe_overflow_bitmap,	/* pcbe_overflow_bitmap */
	core_pcbe_configure,		/* pcbe_configure */
	core_pcbe_program,		/* pcbe_program */
	core_pcbe_allstop,		/* pcbe_allstop */
	core_pcbe_sample,		/* pcbe_sample */
	core_pcbe_free			/* pcbe_free */
};

static char	**ffc_names = NULL;
static char	**ffc_allnames = NULL;
static char	**gpc_names = NULL;
static uint32_t	versionid;
static uint64_t	num_gpc;
static uint64_t	width_gpc;
static uint64_t	mask_gpc;
static uint64_t	num_ffc;
static uint64_t	width_ffc;
static uint64_t	mask_ffc;
static uint_t	total_pmc;
static uint64_t	control_ffc;
static uint64_t	control_gpc;
static uint64_t	control_mask;
static uint32_t	arch_events_vector;
static const struct events_table_t *arch_events_table = NULL;
static const struct events_table_t *events_table = NULL;
static const struct msr_events *msr_events_table = NULL;
static uint64_t known_arch_events;
static uint64_t known_ffc_num;

#define	IMPL_NAME_LEN 100
static char core_impl_name[IMPL_NAME_LEN];

static char *nhm_attrs = "edge,inv,umask,cmask,anythr,msr_offcore";

static const char *core_cpuref =
	"See Appendix A of the \"Intel 64 and IA-32 Architectures Software" \
	" Developer's Manual Volume 3B: System Programming Guide, Part 2\"" \
	" Order Number: 253669-039US, May 2011";

/* FFC entries must be in order */
static char *ffc_names_non_htt[] = {
	"instr_retired.any",
	"cpu_clk_unhalted.core",
	"cpu_clk_unhalted.ref",
	NULL
};

static char *ffc_names_htt[] = {
	"instr_retired.any",
	"cpu_clk_unhalted.thread",
	"cpu_clk_unhalted.ref",
	NULL
};

static char *ffc_genericnames[] = {
	"PAPI_tot_ins",
	"PAPI_tot_cyc",
	"",
	NULL
};

static const struct events_table_t arch_events_table_non_htt[] = {
	{ 0x3c, 0x00, C_ALL, "cpu_clk_unhalted.core" },
	ARCH_EVENTS_COMMON
};

static const struct events_table_t arch_events_table_htt[] = {
	{ 0x3c, 0x00, C_ALL, "cpu_clk_unhalted.thread_p" },
	ARCH_EVENTS_COMMON
};

static char *arch_genevents_table[] = {
	"PAPI_tot_cyc", /* cpu_clk_unhalted.thread_p/core */
	"PAPI_tot_ins", /* inst_retired.any_p		  */
	"",		/* cpu_clk_unhalted.ref_p	  */
	"",		/* longest_lat_cache.reference	  */
	"",		/* longest_lat_cache.miss	  */
	"",		/* br_inst_retired.all_branches	  */
	"",		/* br_misp_retired.all_branches	  */
};

/* Statistics on erratum behavior */
static uint64_t intr_ctr_ovf_nclrd = 0;
static uint64_t intr_ctr_ffc_ovf = 0;
static uint64_t intr_ctr_gpc_ovf = 0;
static uint64_t intr_ctr_ovf_fail = 0;

/* Documented Nehalem-EP/EX max values */
#define	NHM_MAX_FFC	3
#define	NHM_MAX_GPC	8

const struct events_table_t events_fam6_nhm[] = {
	GENERICEVENTS_FAM6_NHM,
	EVENTS_FAM6_NHM,
	EVENTS_FAM6_NHM_EP_ONLY,
	{ NT_END, 0, 0, "" }
};

const struct events_table_t events_fam6_nhm_ex[] = {
	GENERICEVENTS_FAM6_NHM,
	EVENTS_FAM6_NHM,
	{ NT_END, 0, 0, "" }
};

const struct msr_events events_fam6_nhm_msr[] = {
	EVENTS_FAM6_NHM_MSR,
	{{ NT_END, 0, 0, "" }, 0, 0}
};

const struct events_table_t events_fam6_wm[] = {
	GENERICEVENTS_FAM6_NHM,
	EVENTS_FAM6_WM,
	EVENTS_FAM6_WM_EP_ONLY,
	{ NT_END, 0, 0, "" }
};

const struct events_table_t events_fam6_wm_ex[] = {
	GENERICEVENTS_FAM6_NHM,
	EVENTS_FAM6_WM,
	{ NT_END, 0, 0, "" }
};

const struct msr_events events_fam6_wm_msr[] = {
	EVENTS_FAM6_WM_MSR,
	{{ NT_END, 0, 0, "" }, 0, 0}
};

static int
core_pcbe_init(void)
{
	struct cpuid_regs	cp;
	size_t			size;
	uint64_t		i;
	uint64_t		j;
	uint64_t		arch_events_vector_length;
	size_t			arch_events_string_length;
	uint_t			family;
	uint_t			model;

	if (cpuid_getvendor(CPU) != X86_VENDOR_Intel)
		return (-1);

	/* Obtain Basic CPUID information */
	cp.cp_eax = 0x0;
	(void) __cpuid_insn(&cp);

	/* No Architectural Performance Monitoring Leaf returned by CPUID */
	if (cp.cp_eax < 0xa) {
		return (-1);
	}

	/* Obtain the Architectural Performance Monitoring Leaf */
	cp.cp_eax = 0xa;
	(void) __cpuid_insn(&cp);

	versionid = cp.cp_eax & 0xFF;
	if (versionid < 3)
		return (-1);

	/*
	 * Fixed-Function Counters (FFC)
	 */
	num_ffc = cp.cp_edx & 0x1F;
	width_ffc = (cp.cp_edx >> 5) & 0xFF;

	if (num_ffc >= 64)
		return (-1);

	/* Set HTT-specific names of architectural & FFC events */
	if (is_x86_feature(x86_featureset, X86FSET_HTT)) {
		ffc_names = ffc_names_htt;
		arch_events_table = arch_events_table_htt;
		known_arch_events =
		    sizeof (arch_events_table_htt) /
		    sizeof (struct events_table_t);
		known_ffc_num =
		    sizeof (ffc_names_htt) / sizeof (char *);
	} else {
		ffc_names = ffc_names_non_htt;
		arch_events_table = arch_events_table_non_htt;
		known_arch_events =
		    sizeof (arch_events_table_non_htt) /
		    sizeof (struct events_table_t);
		known_ffc_num =
		    sizeof (ffc_names_non_htt) / sizeof (char *);
	}

	if (num_ffc >= known_ffc_num) {
		/*
		 * The system seems to have more fixed-function counters than
		 * what this PCBE is able to handle correctly.  Default to the
		 * maximum number of fixed-function counters that this driver
		 * is aware of.
		 */
		num_ffc = known_ffc_num - 1;
	}

	mask_ffc = BITMASK_XBITS(width_ffc);
	control_ffc = BITMASK_XBITS(num_ffc);

	/*
	 * General Purpose Counters (GPC)
	 */
	num_gpc = (cp.cp_eax >> 8) & 0xFF;
	width_gpc = (cp.cp_eax >> 16) & 0xFF;

	if (num_gpc >= 64)
		return (-1);

	mask_gpc = BITMASK_XBITS(width_gpc);

	control_gpc = BITMASK_XBITS(num_gpc);

	control_mask = (control_ffc << 32) | control_gpc;

	total_pmc = num_gpc + num_ffc;
	if (total_pmc > 64) {
		/* Too wide for the overflow bitmap */
		return (-1);
	}

	/* FFC names */
	ffc_allnames = kmem_alloc(num_ffc * sizeof (char *), KM_SLEEP);
	for (i = 0; i < num_ffc; i++) {
		ffc_allnames[i] = kmem_alloc(
		    strlen(ffc_names[i]) + strlen(ffc_genericnames[i]) + 2,
		    KM_SLEEP);

		ffc_allnames[i][0] = '\0';
		(void) strcat(ffc_allnames[i], ffc_names[i]);

		/* Check if this ffc has a generic name */
		if (strcmp(ffc_genericnames[i], "") != 0) {
			(void) strcat(ffc_allnames[i], ",");
			(void) strcat(ffc_allnames[i], ffc_genericnames[i]);
		}
	}

	family = cpuid_getfamily(CPU);
	model = cpuid_getmodel(CPU);
	(void) snprintf(core_impl_name, IMPL_NAME_LEN,
	    "Intel Arch PerfMon v%d on Family %d Model %d",
	    versionid, family, model);

	/*
	 * Architectural events
	 */
	arch_events_vector_length = (cp.cp_eax >> 24) & 0xFF;

	ASSERT(known_arch_events == arch_events_vector_length);

	/*
	 * To handle the case where a new performance monitoring setup is run
	 * on a non-debug kernel
	 */
	if (known_arch_events > arch_events_vector_length) {
		known_arch_events = arch_events_vector_length;
	} else {
		arch_events_vector_length = known_arch_events;
	}

	arch_events_vector = cp.cp_ebx &
	    BITMASK_XBITS(arch_events_vector_length);

	/*
	 * Support for platforms that only have FFC registers
	 */
	if (num_gpc == 0)
		return (0);

	/*
	 * Process architectural and non-architectural events using GPC
	 */
	gpc_names = kmem_alloc(num_gpc * sizeof (char *), KM_SLEEP);

	/* Calculate space required for the architectural gpc events */
	arch_events_string_length = 0;
	for (i = 0; i < known_arch_events; i++) {
		if (((1U << i) & arch_events_vector) == 0) {
			arch_events_string_length +=
			    strlen(arch_events_table[i].name) + 1;
			if (strcmp(arch_genevents_table[i], "") != 0) {
				arch_events_string_length +=
				    strlen(arch_genevents_table[i]) + 1;
			}
		}
	}

	/* Non-architectural events list */
	switch (model) {
		/* Westmere */
		case 37:
		case 44:
			events_table = events_fam6_wm;
			msr_events_table = events_fam6_wm_msr;
			break;
		/* Westmere-EX */
		case 47:
			events_table = events_fam6_wm_ex;
			msr_events_table = events_fam6_wm_msr;
			break;
		/* Nehalem */
		case 26:
		case 30:
		case 31:
			events_table = events_fam6_nhm;
			msr_events_table = events_fam6_nhm_msr;
			break;
		/* Nehalem-EX */
		case 46:
			events_table = events_fam6_nhm_ex;
			msr_events_table = events_fam6_nhm_msr;
			break;
	}

	for (i = 0; i < num_gpc; i++) {

		/*
		 * Determine length of all supported event names
		 * (architectural + non-architectural + msr_offcore)
		 */
		size = arch_events_string_length;
		for (j = 0; events_table != NULL &&
		    events_table[j].eventselect != NT_END; j++) {
			if (C(i) & events_table[j].supported_counters) {
				size += strlen(events_table[j].name) + 1;
			}
		}
		for (j = 0; msr_events_table != NULL &&
		    msr_events_table[j].event.eventselect != NT_END; j++) {
			if (C(i) &
			    msr_events_table[j].event.supported_counters) {
				size +=
				    strlen(msr_events_table[j].event.name) + 1;
			}
		}

		/* Allocate memory for this pics list */
		gpc_names[i] = kmem_alloc(size + 1, KM_SLEEP);
		gpc_names[i][0] = '\0';
		if (size == 0) {
			continue;
		}

		/*
		 * Create the list of all supported events
		 * (architectural + non-architectural + msr_offcore)
		 */
		for (j = 0; j < known_arch_events; j++) {
			if (((1U << j) & arch_events_vector) == 0) {
				(void) strcat(gpc_names[i],
				    arch_events_table[j].name);
				(void) strcat(gpc_names[i], ",");
				if (strcmp(arch_genevents_table[j], "") != 0) {
					(void) strcat(gpc_names[i],
					    arch_genevents_table[j]);
					(void) strcat(gpc_names[i], ",");
				}
			}
		}

		for (j = 0; events_table != NULL &&
		    events_table[j].eventselect != NT_END; j++) {
			if (C(i) & events_table[j].supported_counters) {
				(void) strcat(gpc_names[i],
				    events_table[j].name);
				(void) strcat(gpc_names[i], ",");
			}
		}
		for (j = 0; msr_events_table != NULL &&
		    msr_events_table[j].event.eventselect != NT_END; j++) {
			if (C(i) &
			    msr_events_table[j].event.supported_counters) {
				(void) strcat(gpc_names[i],
				    msr_events_table[j].event.name);
				(void) strcat(gpc_names[i], ",");
			}
		}

		/* Remove trailing comma */
		gpc_names[i][size - 1] = '\0';
	}

	return (0);
}

static uint_t core_pcbe_ncounters()
{
	return (total_pmc);
}

static const char *core_pcbe_impl_name(void)
{
	return (core_impl_name);
}

static const char *core_pcbe_cpuref(void)
{
	return (core_cpuref);
}

static char *core_pcbe_list_events(uint_t picnum)
{
	ASSERT(picnum < cpc_ncounters);

	if (picnum < num_gpc) {
		return (gpc_names[picnum]);
	} else {
		return (ffc_allnames[picnum - num_gpc]);
	}
}

static char *core_pcbe_list_attrs(void)
{
	return (nhm_attrs);
}

static int
find_gpcevent(char *name, struct msr_events *ne)
{
	int i;

	/* Search architectural events */
	for (i = 0; i < known_arch_events; i++) {
		if (strcmp(name, arch_events_table[i].name) == 0 ||
		    strcmp(name, arch_genevents_table[i]) == 0) {
			if (((1U << i) & arch_events_vector) == 0) {
				ne->event = arch_events_table[i];
				return (1);
			}
		}
	}

	/* Search non-architectural events */
	if (events_table != NULL) {
		for (i = 0; events_table[i].eventselect != NT_END; i++) {
			if (strcmp(name, events_table[i].name) == 0) {
				ne->event = events_table[i];
				return (1);
			}
		}
	}

	/* Search non-architectural MSR events */
	for (i = 0; msr_events_table != NULL &&
	    msr_events_table[i].event.eventselect != NT_END; i++) {
		if (strcmp(name, msr_events_table[i].event.name) == 0) {
			ne->event = msr_events_table[i].event;
			ne->msr_adr = msr_events_table[i].msr_adr;
			ne->msr_val = msr_events_table[i].msr_val;
			return (1);
		}
	}

	return (0);
}

static uint64_t
core_pcbe_event_coverage(char *event)
{
	uint64_t bitmap;
	uint64_t bitmask;
	struct msr_events ne = {0};
	int i, found;

	bitmap = 0;

	/* Is it an event that a GPC can track? */
	found = find_gpcevent(event, &ne);
	if (found) {
		bitmap |= (ne.event.supported_counters &
		    BITMASK_XBITS(num_gpc));
	}

	/* Check if the event can be counted in the fixed-function counters */
	if (num_ffc > 0) {
		bitmask = 1ULL << num_gpc;
		for (i = 0; i < num_ffc; i++) {
			if (strcmp(event, ffc_names[i]) == 0) {
				bitmap |= bitmask;
			} else if (strcmp(event, ffc_genericnames[i]) == 0) {
				bitmap |= bitmask;
			}
			bitmask = bitmask << 1;
		}
	}

	return (bitmap);
}

/* Workaround for Intel Errata AAK157 partially provided by Intel. */
static void
nhm_wm_clear_overflow(uint64_t first_ovf_bitmap)
{
	uint64_t interrupt_status;
	uint64_t int_status;
	uint64_t intrbits_ffc;
	uint64_t intrbits_gpc;
	uint64_t overflow_bitmap;
	uint64_t ovf_bitmap;
	uint64_t core_pmc;
	uint64_t save_ffc[NHM_MAX_FFC];
	uint64_t save_gpc[NHM_MAX_GPC];
	uint64_t j;
	int i;

	/*
	 * First, check to see if we are hitting the erratum.
	 * Just rereading the status register appears to be capable of
	 * forcing the data commit to the ctl reg in nearly all cases.
	 */
	RDMSR(PERF_GLOBAL_STATUS, int_status);
	interrupt_status = int_status & control_mask;
	intrbits_ffc = (interrupt_status >> 32) & control_ffc;
	intrbits_gpc = interrupt_status & control_gpc;
	overflow_bitmap = (intrbits_ffc << num_gpc) | intrbits_gpc;
	if (overflow_bitmap == first_ovf_bitmap)
		intr_ctr_ovf_nclrd++;

	/*
	 * If the overflow is not cleared, then we need to
	 * save the data and follow the workaround steps.
	 */
	if (overflow_bitmap != 0) {
		ASSERT(num_ffc <= NHM_MAX_FFC);
		ASSERT(num_gpc <= NHM_MAX_GPC);

		/*
		 * Step 1: Save the overflow counter data, and clear
		 * any overflowed counters prior to disabling them.
		 */
		for (i = 0, j = 1; i < num_ffc; i++, j = j << 1) {
			if (j & intrbits_ffc) {
				core_pmc = (uint64_t)(FFC_BASE_PMC + i);
				RDMSR(core_pmc, save_ffc[i]);
				WRMSR(core_pmc, 0);
				intr_ctr_ffc_ovf++;
			}
		}
		for (i = 0, j = 1; i < num_gpc; i++, j = j << 1) {
			if (j & intrbits_gpc) {
				core_pmc = (uint64_t)(GPC_BASE_PMC + i);
				RDMSR(core_pmc, save_gpc[i]);
				WRMSR(core_pmc, 0);
				intr_ctr_gpc_ovf++;
			}
		}

		/* Step 2: Disable counters after counter value cleared. */
		WRMSR(PERF_GLOBAL_CTRL, ALL_STOPPED);
		WRMSR(PERF_FIXED_CTR_CTRL, ALL_STOPPED);
		/* Step 3: Then clear any overflow status indication bit. */
		WRMSR(PERF_GLOBAL_OVF_CTRL, int_status);

		/* Step 4: Write back counter data for later consumers. */
		for (i = 0, j = 1; i < num_ffc; i++, j = j << 1) {
			if (j & intrbits_ffc) {
				core_pmc = (uint64_t)(FFC_BASE_PMC + i);
				WRMSR(core_pmc, save_ffc[i]);
			}
		}
		for (i = 0, j = 1; i < num_gpc; i++, j = j << 1) {
			if (j & intrbits_gpc) {
				core_pmc = (uint64_t)(GPC_BASE_PMC + i);
				WRMSR(core_pmc, save_gpc[i]);
			}
		}

		/* Step 5: Check if overflow bit(s) actually cleared. */
		RDMSR(PERF_GLOBAL_STATUS, interrupt_status);
		interrupt_status = interrupt_status & control_mask;
		intrbits_ffc = (interrupt_status >> 32) & control_ffc;
		intrbits_gpc = interrupt_status & control_gpc;
		ovf_bitmap = (intrbits_ffc << num_gpc) | intrbits_gpc;
		if (ovf_bitmap != 0) { /* workaround failed */
			intr_ctr_ovf_fail++;
		}
	} else {
		/*
		 * Leave counter registers disabled, regardless of
		 * whether or not the overflow erratum happened.
		 */
		WRMSR(PERF_GLOBAL_CTRL, ALL_STOPPED);
		WRMSR(PERF_FIXED_CTR_CTRL, ALL_STOPPED);
	}
}

static uint64_t
core_pcbe_overflow_bitmap(void)
{
	uint64_t interrupt_status;
	uint64_t intrbits_ffc;
	uint64_t intrbits_gpc;
	extern int kcpc_hw_overflow_intr_installed;
	uint64_t overflow_bitmap;

	RDMSR(PERF_GLOBAL_STATUS, interrupt_status);
	WRMSR(PERF_GLOBAL_OVF_CTRL, interrupt_status);

	interrupt_status = interrupt_status & control_mask;
	intrbits_ffc = (interrupt_status >> 32) & control_ffc;
	intrbits_gpc = interrupt_status & control_gpc;
	overflow_bitmap = (intrbits_ffc << num_gpc) | intrbits_gpc;

	nhm_wm_clear_overflow(overflow_bitmap);

	ASSERT(kcpc_hw_overflow_intr_installed);
	(*kcpc_hw_enable_cpc_intr)();

	return (overflow_bitmap);
}

static int
configure_gpc(uint_t picnum, char *event, uint64_t preset, uint32_t flags,
    uint_t nattrs, kcpc_attr_t *attrs, void **data)
{
	nhm_pcbe_config_t	conf = {0};
	int			i, found;
	long			event_num;
	struct msr_events	ne = {0};
	int			msr_event = 0;

	if (((preset & BITS_EXTENDED_FROM_31) != 0) &&
	    ((preset & BITS_EXTENDED_FROM_31) !=
	    BITS_EXTENDED_FROM_31)) {

		/*
		 * Bits beyond bit-31 in the general-purpose counters can only
		 * be written to by extension of bit 31.  We cannot preset
		 * these bits to any value other than all 1s or all 0s.
		 */
		return (CPC_ATTRIBUTE_OUT_OF_RANGE);
	}

	found = find_gpcevent(event, &ne);
	if (found) {
		if ((C(picnum) & ne.event.supported_counters) == 0) {
			return (CPC_PIC_NOT_CAPABLE);
		}
		if (nattrs > 0 && (strncmp("PAPI_", event, 5) == 0)) {
			return (CPC_ATTRIBUTE_OUT_OF_RANGE);
		}
		conf.core_ctl = ne.event.eventselect;
		conf.core_ctl |= ne.event.unitmask << CORE_UMASK_SHIFT;
		conf.msr_adr = (uint64_t)ne.msr_adr;
		conf.msr_val = (uint64_t)ne.msr_val;
		if (ne.msr_adr != 0) {
			msr_event = 1;
		}
	} else {
		/* Event specified as raw event code */
		if (ddi_strtol(event, NULL, 0, &event_num) != 0) {
			return (CPC_INVALID_EVENT);
		}
		conf.core_ctl = event_num & 0xFF;
		/* check if it's an event that requires MSR programming */
		for (i = 0; msr_events_table != NULL &&
		    msr_events_table[i].event.eventselect != NT_END; i++) {
			if (msr_events_table[i].event.eventselect ==
			    (uint8_t)conf.core_ctl) {
				conf.msr_adr = msr_events_table[i].msr_adr;
				conf.msr_val = msr_events_table[i].msr_val;
				msr_event = 1;
				break;
			}
		}
	}

	conf.core_picno = picnum;
	conf.core_pictype = CORE_GPC;
	conf.core_rawpic = preset & mask_gpc;

	conf.core_pes = GPC_BASE_PES + picnum;
	conf.core_pmc = GPC_BASE_PMC + picnum;

	for (i = 0; i < nattrs; i++) {
		if (strncmp(attrs[i].ka_name, "umask", 6) == 0) {
			if ((attrs[i].ka_val | CORE_UMASK_MASK) !=
			    CORE_UMASK_MASK) {
				return (CPC_ATTRIBUTE_OUT_OF_RANGE);
			}
			/* Clear out the default umask */
			conf.core_ctl &= ~ (CORE_UMASK_MASK <<
			    CORE_UMASK_SHIFT);
			/* Use the user provided umask */
			conf.core_ctl |= attrs[i].ka_val <<
			    CORE_UMASK_SHIFT;
		} else  if (strncmp(attrs[i].ka_name, "edge", 6) == 0) {
			if (attrs[i].ka_val != 0)
				conf.core_ctl |= CORE_EDGE;
		} else if (strncmp(attrs[i].ka_name, "inv", 4) == 0) {
			if (attrs[i].ka_val != 0)
				conf.core_ctl |= CORE_INV;
		} else if (strncmp(attrs[i].ka_name, "cmask", 6) == 0) {
			if ((attrs[i].ka_val | CORE_CMASK_MASK) !=
			    CORE_CMASK_MASK) {
				return (CPC_ATTRIBUTE_OUT_OF_RANGE);
			}
			conf.core_ctl |= attrs[i].ka_val <<
			    CORE_CMASK_SHIFT;
		} else if (strncmp(attrs[i].ka_name, "anythr", 7) ==
		    0) {
			if (secpolicy_cpc_cpu(CRED()) != 0) {
				return (CPC_ATTR_REQUIRES_PRIVILEGE);
			}
			if (attrs[i].ka_val != 0)
				conf.core_ctl |= CORE_ANYTHR;
		} else if (strncmp(attrs[i].ka_name, "msr_offcore", 12) == 0) {
			if (msr_event) {
				/* Mask and check for valid values */
				conf.msr_val = attrs[i].ka_val &
				    NHM_OFFCORE_MASK;
				if ((conf.msr_val & NHM_OFFCORE_REQ_MASK)
				    == 0) {
					return (CPC_ATTRIBUTE_OUT_OF_RANGE);
				} else if ((conf.msr_val & NHM_OFFCORE_RSP_MASK)
				    == 0) {
					return (CPC_ATTRIBUTE_OUT_OF_RANGE);
				}
			} else {
				return (CPC_INVALID_ATTRIBUTE);
			}
		} else {
			return (CPC_INVALID_ATTRIBUTE);
		}
	}

	if (flags & CPC_COUNT_USER)
		conf.core_ctl |= CORE_USR;
	if (flags & CPC_COUNT_SYSTEM)
		conf.core_ctl |= CORE_OS;
	if (flags & CPC_OVF_NOTIFY_EMT)
		conf.core_ctl |= CORE_INT;
	conf.core_ctl |= CORE_EN;

	*data = kmem_alloc(sizeof (nhm_pcbe_config_t), KM_SLEEP);
	*((nhm_pcbe_config_t *)*data) = conf;

	return (0);
}

static int
configure_ffc(uint_t picnum, char *event, uint64_t preset, uint32_t flags,
    uint_t nattrs, kcpc_attr_t *attrs, void **data)
{
	nhm_pcbe_config_t	*conf;
	uint_t			i;

	if (picnum - num_gpc >= num_ffc) {
		return (CPC_INVALID_PICNUM);
	}

	if ((strcmp(ffc_names[picnum-num_gpc], event) != 0) &&
	    (strcmp(ffc_genericnames[picnum-num_gpc], event) != 0)) {
		return (CPC_INVALID_EVENT);
	}

	conf = kmem_alloc(sizeof (nhm_pcbe_config_t), KM_SLEEP);
	conf->core_ctl = 0;

	for (i = 0; i < nattrs; i++) {
		if (strncmp(attrs[i].ka_name, "anythr", 7) == 0) {
			if (secpolicy_cpc_cpu(CRED()) != 0) {
				kmem_free(conf, sizeof (nhm_pcbe_config_t));
				return (CPC_ATTR_REQUIRES_PRIVILEGE);
			}
			if (attrs[i].ka_val != 0) {
				conf->core_ctl |= CORE_FFC_ANYTHR;
			}
		} else {
			kmem_free(conf, sizeof (nhm_pcbe_config_t));
			return (CPC_INVALID_ATTRIBUTE);
		}
	}

	conf->core_picno = picnum;
	conf->core_pictype = CORE_FFC;
	conf->core_rawpic = preset & mask_ffc;
	conf->core_pmc = FFC_BASE_PMC + (picnum - num_gpc);

	/* All fixed-function counters have the same control register */
	conf->core_pes = PERF_FIXED_CTR_CTRL;

	if (flags & CPC_COUNT_USER)
		conf->core_ctl |= CORE_FFC_USR_EN;
	if (flags & CPC_COUNT_SYSTEM)
		conf->core_ctl |= CORE_FFC_OS_EN;
	if (flags & CPC_OVF_NOTIFY_EMT)
		conf->core_ctl |= CORE_FFC_PMI;

	*data = conf;
	return (0);
}

/*ARGSUSED*/
static int
core_pcbe_configure(uint_t picnum, char *event, uint64_t preset,
    uint32_t flags, uint_t nattrs, kcpc_attr_t *attrs, void **data,
    void *token)
{
	int			ret;
	nhm_pcbe_config_t	*conf;

	/*
	 * If we've been handed an existing configuration, we need only preset
	 * the counter value.
	 */
	if (*data != NULL) {
		conf = *data;
		ASSERT(conf->core_pictype == CORE_GPC ||
		    conf->core_pictype == CORE_FFC);
		if (conf->core_pictype == CORE_GPC)
			conf->core_rawpic = preset & mask_gpc;
		else /* CORE_FFC */
			conf->core_rawpic = preset & mask_ffc;
		return (0);
	}

	if (picnum >= total_pmc) {
		return (CPC_INVALID_PICNUM);
	}

	if (picnum < num_gpc) {
		ret = configure_gpc(picnum, event, preset, flags,
		    nattrs, attrs, data);
	} else {
		ret = configure_ffc(picnum, event, preset, flags,
		    nattrs, attrs, data);
	}
	return (ret);
}

static void
core_pcbe_program(void *token)
{
	nhm_pcbe_config_t	*cfg;
	uint64_t		perf_global_ctrl;
	uint64_t		perf_fixed_ctr_ctrl;
	uint64_t		curcr4;

	core_pcbe_allstop();

	curcr4 = getcr4();
	if (kcpc_allow_nonpriv(token))
		/* Allow RDPMC at any ring level */
		setcr4(curcr4 | CR4_PCE);
	else
		/* Allow RDPMC only at ring 0 */
		setcr4(curcr4 & ~CR4_PCE);

	/* Clear any overflow indicators before programming the counters */
	WRMSR(PERF_GLOBAL_OVF_CTRL, MASK_CONDCHGD_OVFBUFFER | control_mask);

	cfg = NULL;
	perf_global_ctrl = 0;
	perf_fixed_ctr_ctrl = 0;
	cfg = (nhm_pcbe_config_t *)kcpc_next_config(token, cfg, NULL);
	while (cfg != NULL) {
		ASSERT(cfg->core_pictype == CORE_GPC ||
		    cfg->core_pictype == CORE_FFC);

		if (cfg->core_pictype == CORE_GPC) {
			/*
			 * General-purpose counter registers have write
			 * restrictions where only the lower 32-bits can be
			 * written to.  The rest of the relevant bits are
			 * written to by extension from bit 31 (all ZEROS if
			 * bit-31 is ZERO and all ONE if bit-31 is ONE).  This
			 * makes it possible to write to the counter register
			 * only values that have all ONEs or all ZEROs in the
			 * higher bits.
			 */
			if (((cfg->core_rawpic & BITS_EXTENDED_FROM_31) == 0) ||
			    ((cfg->core_rawpic & BITS_EXTENDED_FROM_31) ==
			    BITS_EXTENDED_FROM_31)) {
				/*
				 * Straighforward case where the higher bits
				 * are all ZEROs or all ONEs.
				 */
				WRMSR(cfg->core_pmc,
				    (cfg->core_rawpic & mask_gpc));
			} else {
				/*
				 * The high order bits are not all the same.
				 * We save what is currently in the registers
				 * and do not write to it.  When we want to do
				 * a read from this register later (in
				 * core_pcbe_sample()), we subtract the value
				 * we save here to get the actual event count.
				 *
				 * NOTE: As a result, we will not get overflow
				 * interrupts as expected.
				 */
				RDMSR(cfg->core_pmc, cfg->core_rawpic);
				cfg->core_rawpic = cfg->core_rawpic & mask_gpc;
			}
			if (cfg->msr_adr != 0)
				WRMSR(cfg->msr_adr, cfg->msr_val);
			WRMSR(cfg->core_pes, cfg->core_ctl);
			perf_global_ctrl |= 1ull << cfg->core_picno;
		} else {
			/*
			 * Unlike the general-purpose counters, all relevant
			 * bits of fixed-function counters can be written to.
			 */
			WRMSR(cfg->core_pmc, cfg->core_rawpic & mask_ffc);

			/*
			 * Collect the control bits for all the
			 * fixed-function counters and write it at one shot
			 * later in this function
			 */
			perf_fixed_ctr_ctrl |= cfg->core_ctl <<
			    ((cfg->core_picno - num_gpc) * CORE_FFC_ATTR_SIZE);
			perf_global_ctrl |=
			    1ull << (cfg->core_picno - num_gpc + 32);
		}

		cfg = (nhm_pcbe_config_t *)
		    kcpc_next_config(token, cfg, NULL);
	}

	/* Enable all the counters */
	WRMSR(PERF_FIXED_CTR_CTRL, perf_fixed_ctr_ctrl);
	WRMSR(PERF_GLOBAL_CTRL, perf_global_ctrl);
}

static void
core_pcbe_allstop(void)
{
	/* Disable all the counters together */
	WRMSR(PERF_GLOBAL_CTRL, ALL_STOPPED);

	setcr4(getcr4() & ~CR4_PCE);
}

static void
core_pcbe_sample(void *token)
{
	uint64_t		*daddr;
	uint64_t		curpic;
	nhm_pcbe_config_t	*cfg;
	uint64_t		counter_mask;

	cfg = (nhm_pcbe_config_t *)kcpc_next_config(token, NULL, &daddr);
	while (cfg != NULL) {
		ASSERT(cfg->core_pictype == CORE_GPC ||
		    cfg->core_pictype == CORE_FFC);

		curpic = rdmsr(cfg->core_pmc);

		DTRACE_PROBE4(core__pcbe__sample,
		    uint64_t, cfg->core_pmc,
		    uint64_t, curpic,
		    uint64_t, cfg->core_rawpic,
		    uint64_t, *daddr);

		if (cfg->core_pictype == CORE_GPC) {
			counter_mask = mask_gpc;
		} else {
			counter_mask = mask_ffc;
		}
		curpic = curpic & counter_mask;
		if (curpic >= cfg->core_rawpic) {
			*daddr += curpic - cfg->core_rawpic;
		} else {
			/* Counter overflowed since our last sample */
			*daddr += counter_mask - (cfg->core_rawpic - curpic) +
			    1;
		}
		cfg->core_rawpic = *daddr & counter_mask;

		cfg =
		    (nhm_pcbe_config_t *)kcpc_next_config(token, cfg, &daddr);
	}
}

static void
core_pcbe_free(void *config)
{
	kmem_free(config, sizeof (nhm_pcbe_config_t));
}

static struct modlpcbe core_modlpcbe = {
	&mod_pcbeops,
	"Nehalem/Westmere Performance Counters",
	&core_pcbe_ops
};

static struct modlinkage core_modl = {
	MODREV_1,
	&core_modlpcbe,
};

int
_init(void)
{
	if (core_pcbe_init() != 0) {
		return (ENOTSUP);
	}
	return (mod_install(&core_modl));
}

int
_fini(void)
{
	return (mod_remove(&core_modl));
}

int
_info(struct modinfo *mi)
{
	return (mod_info(&core_modl, mi));
}

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
 * SPARC T4 Performance Counter Backend
 */

#include <sys/cpuvar.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/cmn_err.h>
#include <sys/cpc_impl.h>
#include <sys/cpc_pcbe.h>
#include <sys/modctl.h>
#include <sys/machsystm.h>
#include <sys/sdt.h>
#include <sys/niagara2regs.h>
#include <sys/hsvc.h>
#include <sys/hypervisor_api.h>
#include <sys/disp.h>

/*LINTLIBRARY*/
static int vt_pcbe_init(void);
static uint_t vt_pcbe_ncounters(void);
static const char *vt_pcbe_impl_name(void);
static const char *vt_pcbe_cpuref(void);
static char *vt_pcbe_list_events(uint_t picnum);
static char *vt_pcbe_list_attrs(void);
static uint64_t vt_pcbe_event_coverage(char *event);
static uint64_t vt_pcbe_overflow_bitmap(void);
static int vt_pcbe_configure(uint_t picnum, char *event, uint64_t preset,
    uint32_t flags, uint_t nattrs, kcpc_attr_t *attrs, void **data,
    void *token);
static void vt_pcbe_program(void *token);
static void vt_pcbe_allstop(void);
static void vt_pcbe_sample(void *token);
static void vt_pcbe_free(void *config);
extern void vt_setpic(uint64_t, uint64_t);
extern uint64_t vt_getpic(uint64_t);
extern uint64_t ultra_gettick(void);
extern char cpu_module_name[];

pcbe_ops_t vt_pcbe_ops = {
	PCBE_VER_1,
	CPC_CAP_OVERFLOW_INTERRUPT | CPC_CAP_OVERFLOW_PRECISE,
	vt_pcbe_ncounters,
	vt_pcbe_impl_name,
	vt_pcbe_cpuref,
	vt_pcbe_list_events,
	vt_pcbe_list_attrs,
	vt_pcbe_event_coverage,
	vt_pcbe_overflow_bitmap,
	vt_pcbe_configure,
	vt_pcbe_program,
	vt_pcbe_allstop,
	vt_pcbe_sample,
	vt_pcbe_free
};

typedef struct vt_pcbe_config {
	uint_t		pcbe_picno;	/* pic0-3 */
	uint32_t	pcbe_evsel;	/* %pcr event code unshifted */
	uint32_t	pcbe_flags;	/* hpriv/user/system/priv */
	uint32_t	pcbe_pic;	/* unshifted raw %pic value */
} vt_pcbe_config_t;

typedef struct vt_event {
	const char	*name;
	const uint32_t	emask;		/* mask[10:5] sl[15:11] */
	const uint32_t	emask_valid;	/* Mask of unreserved MASK bits */
} vt_event_t;

typedef struct vt_generic_event {
	char *name;
	char *event;
} vt_generic_event_t;

#define	EV_END {NULL, 0, 0}
#define	GEN_EV_END {NULL, NULL}

#define	CPC_VT_NPIC	4

static const uint64_t	allstopped = 0;

static vt_event_t vt_events[] = {
	{ "Sel_pipe_drain_cycles",		0x041, 0x1f },
	{ "Sel_0_wait",				0x042, 0x1f },
	{ "Sel_0_ready",			0x044, 0x1f },
	{ "Sel_1",				0x048, 0x1f },
	{ "Sel_2",				0x050, 0x1f },
	{ "Pick_0",				0x081, 0x0f },
	{ "Pick_1",				0x082, 0x0f },
	{ "Pick_2",				0x084, 0x0f },
	{ "Pick_3",				0x088, 0x0f },
	{ "Pick_any",				0x08e, 0x0f },
	{ "Branches",				0x0c1, 0x3f },
	{ "Instr_FGU_crypto",			0x0c2, 0x3f },
	{ "Instr_ld",				0x0c4, 0x3f },
	{ "Instr_st",				0x0c8, 0x3f },
	{ "SPR_ring_ops",			0x0d0, 0x3f },
	{ "Instr_other",			0x0e0, 0x3f },
	{ "Instr_all",				0x0ff, 0x3f },
	{ "Br_taken",				0x102, 0x3f },
	{ "Sw_count_intr",			0x104, 0x3f },
	{ "Atomics",				0x118, 0x3f },
	{ "SW_prefetch",			0x110, 0x3f },
	{ "Block_ld_st",			0x120, 0x3f },
	{ "IC_miss_L2_L3_hit_nospec",		0x141, 0x1f },
	{ "IC_miss_local_hit_nospec",		0x142, 0x1f },
	{ "IC_miss_remote_hit_nospec",		0x144, 0x1f },
	{ "IC_miss_nospec",			0x147, 0x1f },
	{ "BTC_miss",				0x148, 0x1f },
	{ "ITLB_miss",				0x150, 0x1f },
	{ "ITLB_fill_8KB",			0x181, 0x3f },
	{ "ITLB_fill_64KB",			0x182, 0x3f },
	{ "ITLB_fill_4MB",			0x184, 0x3f },
	{ "ITLB_fill_256MB",			0x188, 0x3f },
	{ "ITLB_fill_2GB",			0x190, 0x3f },
	{ "ITLB_fill_trap",			0x1a0, 0x3f },
	{ "ITLB_miss_asynch",			0x1bf, 0x3f },
	{ "IC_mtag_miss",			0x1c1, 0x0f },
	{ "IC_mtag_miss_ptag_hit",		0x1c2, 0x0f },
	{ "IC_mtag_miss_ptag_miss",		0x1c4, 0x0f },
	{ "IC_mtag_miss_ptag_hit_way_mismatch",	0x1c8, 0x0f },
	{ "Fetch_0",				0x201, 0x0f },
	{ "Fetch_0_all",			0x202, 0x0f },
	{ "Instr_buffer_full",			0x204, 0x0f },
	{ "BTC_targ_incorrect",			0x208, 0x0f },
	{ "PQ_tag_wait",			0x241, 0x1f },
	{ "ROB_tag_wait",			0x242, 0x1f },
	{ "LB_tag_wait",			0x244, 0x1f },
	{ "ROB_LB_tag_wait",			0x246, 0x1f },
	{ "SB_tag_wait",			0x248, 0x1f },
	{ "ROB_SB_tag_wait",			0x24a, 0x1f },
	{ "LB_SB_tag_wait",			0x24c, 0x1f },
	{ "RB_LB_SB_tag_wait",			0x24e, 0x1f },
	{ "DTLB_miss_tag_wait",			0x250, 0x1f },
	{ "ITLB_HWTW_L2_hit",			0x281, 0x3f },
	{ "ITLB_HWTW_L3_hit",			0x282, 0x3f },
	{ "ITLB_HWTW_L3_miss",			0x284, 0x3f },
	{ "ITLB_HWTW_all",			0x287, 0x3f },
	{ "DTLB_HWTW_L2_hit",			0x288, 0x3f },
	{ "DTLB_HWTW_L3_hit",			0x290, 0x3f },
	{ "DTLB_HWTW_L3_miss",			0x2a0, 0x3f },
	{ "DTLB_HWTW_all",			0x2b8, 0x3f },
	{ "IC_miss_L2_L3_hit",			0x2c1, 0x03 },
	{ "IC_miss_local_remote_remL3_hit",	0x2c2, 0x03 },
	{ "IC_miss",				0x2c3, 0x03 },
	{ "DC_miss_L2_L3_hit_nospec",		0x401, 0x07 },
	{ "DC_miss_local_hit_nospec",		0x402, 0x07 },
	{ "DC_miss_remote_L3_hit_nospec",	0x404, 0x07 },
	{ "DC_miss_nospec",			0x407, 0x07 },
	{ "DTLB_fill_8KB",			0x441, 0x3f },
	{ "DTLB_fill_64KB",			0x442, 0x3f },
	{ "DTLB_fill_4MB",			0x444, 0x3f },
	{ "DTLB_fill_256MB",			0x448, 0x3f },
	{ "DTLB_fill_2GB",			0x450, 0x3f },
	{ "DTLB_fill_trap",			0x460, 0x3f },
	{ "DTLB_miss_asynch",			0x47f, 0x3f },
	{ "DC_pref_drop_DC_hit",		0x481, 0x07 },
	{ "SW_pref_drop_DC_hit",		0x482, 0x07 },
	{ "SW_pref_drop_buffer_full",		0x484, 0x07 },
	{ "SW_pref_drop",			0x486, 0x07 },
	{ "Full_RAW_hit_st_buf",		0x4c1, 0x0f },
	{ "Partial_RAW_hit_st_buf",		0x4c2, 0x0f },
	{ "RAW_hit_st_buf",			0x4c3, 0x0f },
	{ "Full_RAW_hit_st_q",			0x4c4, 0x0f },
	{ "Partial_RAW_hit_st_q",		0x4c8, 0x0f },
	{ "RAW_hit_st_q",			0x4cc, 0x0f },
	{ "IC_evict_invalid",			0x501, 0x07 },
	{ "IC_snoop_invalid",			0x502, 0x07 },
	{ "IC_invalid_all",			0x503, 0x07 },
	{ "DC_evict_invalid",			0x504, 0x07 },
	{ "DC_snoop_invalid",			0x508, 0x07 },
	{ "DC_invalid_all",			0x50c, 0x07 },
	{ "L1_snoop_invalid",			0x50a, 0x07 },
	{ "L1_invalid_all",			0x50f, 0x07 },
	{ "St_q_tag_wait",			0x510, 0x07 },
	{ "Data_pref_hit_L2",			0x541, 0x3f },
	{ "Data_pref_drop_L2",			0x542, 0x3f },
	{ "Data_pref_hit_L3",			0x544, 0x3f },
	{ "Data_pref_hit_local",		0x548, 0x3f },
	{ "Data_pref_hit_remote",		0x550, 0x3f },
	{ "Data_pref_drop_L3",			0x560, 0x3f },
	{ "St_hit_L2",				0x581, 0x3f },
	{ "St_hit_L3",				0x582, 0x3f },
	{ "St_L2_local_C2C",			0x584, 0x3f },
	{ "St_L2_remote_C2C",			0x588, 0x3f },
	{ "St_local",				0x590, 0x3f },
	{ "St_remote",				0x5a0, 0x3f },
	{ "DC_miss_L2_L3_hit",			0x5c1, 0x07 },
	{ "DC_miss_local_hit",			0x5c2, 0x07 },
	{ "DC_miss_remote_L3_hit",		0x5c4, 0x07 },
	{ "DC_miss",				0x5c7, 0x07 },
	{ "L2_clean_evict",			0x601, 0x3f },
	{ "L2_dirty_evict",			0x602, 0x3f },
	{ "L2_fill_buf_full",			0x604, 0x3f },
	{ "L2_wb_buf_full",			0x608, 0x3f },
	{ "L2_miss_buf_full",			0x610, 0x3f },
	{ "L2_pipe_stall",			0x620, 0x3f },
	{ "Br_dir_mispred",			0x641, 0x0f },
	{ "Br_trg_mispred_far_tbl",		0x642, 0x0f },
	{ "Br_trg_mispred_indir_tbl",		0x644, 0x0f },
	{ "Br_trg_mispred_ret_stk",		0x648, 0x0f },
	{ "Br_trg_mispred",			0x64e, 0x0f },
	{ "Br_mispred",				0x64f, 0x0f },
	{ "Cycles_user",			0x680, 0x07 },
	{ "Commit_0",				0x701, 0x0f },
	{ "Commit_0_all",			0x702, 0x0f },
	{ "Commit_1",				0x704, 0x0f },
	{ "Commit_2",				0x708, 0x0f },
	{ "Commit_1_or_2",			0x70c, 0x0f },
	EV_END
};

static vt_generic_event_t vt_generic_events[] = {
	{ "PAPI_br_cn",		"Branches" },
	{ "PAPI_br_ins",	"Br_taken" },
	{ "PAPI_br_msp",	"Br_mispred" },
	{ "PAPI_btac_m",	"BTC_miss" },
	{ "PAPI_fp_ops",	"Instr_FGU_crypto" },
	{ "PAPI_fp_ins",	"Instr_FGU_crypto" },
	{ "PAPI_tot_ins",	"Instr_all" },
	{ "PAPI_l1_dcm",	"DC_miss" },
	{ "PAPI_l1_icm",	"IC_miss" },
	{ "PAPI_ld_ins",	"Instr_ld" },
	{ "PAPI_sr_ins",	"Instr_st" },
	{ "PAPI_tlb_im",	"ITLB_miss" },
	{ "PAPI_tlb_dm",	"DTLB_miss_asynch" },
	GEN_EV_END
};

static char		*evlist;
static size_t		evlist_sz;

static const char	*cpu_impl_name = "SPARC T4";
static const char *cpu_pcbe_ref = "See the \"SPARC T4 Supplement to "
			"Oracle SPARC Architecture 2011 User's Manual\" "
			"for descriptions of these events.";

static int
vt_pcbe_init(void)
{
	vt_event_t		*evp;
	vt_generic_event_t	*gevp;
	int			status;
	uint64_t		cpu_hsvc_major;
	uint64_t		cpu_hsvc_minor;
	uint64_t		hsvc_cpu_group = HSVC_GROUP_VT_CPU;
	uint64_t		hsvc_cpu_major = VT_HSVC_MAJOR;

	/*
	 * Validate API version for VT specific hypervisor services
	 */
	status = hsvc_version(hsvc_cpu_group, &cpu_hsvc_major,
	    &cpu_hsvc_minor);
	if ((status != 0) || (cpu_hsvc_major != hsvc_cpu_major)) {
		cmn_err(CE_WARN, "hypervisor services not negotiated "
		    "or unsupported major number: group: 0x%lx major: 0x%lx "
		    "minor: 0x%lx errno: %d", hsvc_cpu_group,
		    cpu_hsvc_major, cpu_hsvc_minor, status);
		return (-1);
	}

	/*
	 * Construct event list.
	 *
	 * First pass:  Calculate size needed. We'll need an additional byte
	 *		for the NULL pointer during the last strcat.
	 *
	 * Second pass: Copy strings.
	 */
	for (evp = vt_events; evp->name != NULL; evp++)
		evlist_sz += strlen(evp->name) + 1;

	for (gevp = vt_generic_events; gevp->name != NULL; gevp++)
		evlist_sz += strlen(gevp->name) + 1;

	evlist = kmem_alloc(evlist_sz + 1, KM_SLEEP);
	evlist[0] = '\0';

	for (evp = vt_events; evp->name != NULL; evp++) {
		(void) strcat(evlist, evp->name);
		(void) strcat(evlist, ",");
	}

	for (gevp = vt_generic_events; gevp->name != NULL; gevp++) {
		(void) strcat(evlist, gevp->name);
		(void) strcat(evlist, ",");
	}

	/*
	 * Remove trailing comma.
	 */
	evlist[evlist_sz - 1] = '\0';

	return (0);
}

static uint_t
vt_pcbe_ncounters(void)
{
	return (CPC_VT_NPIC);
}

static const char *
vt_pcbe_impl_name(void)
{
	return (cpu_impl_name);
}

static const char *
vt_pcbe_cpuref(void)
{
	return (cpu_pcbe_ref);
}

static char *
vt_pcbe_list_events(uint_t picnum)
{
	ASSERT(picnum < cpc_ncounters);

	return (evlist);
}

static char *
vt_pcbe_list_attrs(void)
{
	return ("hpriv,emask");
}

static vt_generic_event_t *
find_generic_event(char *name)
{
	vt_generic_event_t	*gevp;

	for (gevp = vt_generic_events; gevp->name != NULL; gevp++) {
		if (strcmp(name, gevp->name) == 0)
			return (gevp);
	}

	return (NULL);
}

static vt_event_t *
find_event(char *name)
{
	vt_event_t		*evp;

	for (evp = vt_events; evp->name != NULL; evp++)
		if (strcmp(name, evp->name) == 0)
			return (evp);

	return (NULL);
}

/*ARGSUSED*/
static uint64_t
vt_pcbe_event_coverage(char *event)
{
	/*
	 * Check whether counter event is supported
	 */
	if (find_event(event) == NULL && find_generic_event(event) == NULL)
		return (0);

	/*
	 * All 4 counters can count all events.
	 */
	return (0xf);
}

static uint64_t
vt_pcbe_overflow_bitmap(void)
{
	uint64_t	pcr, overflow;
	int		i;
	uint8_t		bitmap = 0;

	ASSERT(getpil() >= DISP_LEVEL);

	for (i = 0; i < CPC_VT_NPIC; i++) {
		(void) hv_niagara_getperf((uint64_t)i, &pcr);
		DTRACE_PROBE1(hv__vt_getperf, uint64_t, pcr);
		overflow =  (pcr & CPC_PCR_OV_MASK) >>
		    CPC_PCR_OV_SHIFT;

		if (overflow) {
			bitmap |= (uint8_t)(1 << i);
			pcr &= ~CPC_PCR_OV_MASK;
			DTRACE_PROBE1(hv__vt_setperf, uint64_t, pcr);
			(void) hv_niagara_setperf((uint64_t)i, pcr);
		}
	}

	return ((uint64_t)bitmap);
}

/*ARGSUSED*/
static int
vt_pcbe_configure(uint_t picnum, char *event, uint64_t preset, uint32_t flags,
    uint_t nattrs, kcpc_attr_t *attrs, void **data, void *token)
{
	vt_pcbe_config_t	*cfg;
	vt_pcbe_config_t	*other_config;
	vt_event_t		*evp;
	vt_generic_event_t	*gevp;
	int			i;
	uint32_t		evsel;

	/*
	 * If we've been handed an existing configuration, we need only preset
	 * the counter value.
	 */
	if (*data != NULL) {
		cfg = *data;
		cfg->pcbe_pic = (uint32_t)preset;
		return (0);
	}

	if (picnum > CPC_VT_NPIC)
		return (CPC_INVALID_PICNUM);


	if ((evp = find_event(event)) == NULL) {
		if ((gevp = find_generic_event(event)) != NULL) {
			evp = find_event(gevp->event);
			ASSERT(evp != NULL);

			if (nattrs > 0)
				return (CPC_ATTRIBUTE_OUT_OF_RANGE);
		} else {
			return (CPC_INVALID_EVENT);
		}
	}

	evsel = evp->emask;

	for (i = 0; i < nattrs; i++) {
		if (strcmp(attrs[i].ka_name, "hpriv") == 0) {
			if (attrs[i].ka_val != 0)
				flags |= CPC_COUNT_HV;
		} else if (strcmp(attrs[i].ka_name, "emask") == 0) {
			if ((attrs[i].ka_val | evp->emask_valid) !=
			    evp->emask_valid)
				return (CPC_ATTRIBUTE_OUT_OF_RANGE);
			evsel |= attrs[i].ka_val;
		} else
			return (CPC_INVALID_ATTRIBUTE);
	}

	/*
	 * Find other requests that will be programmed with this one, and ensure
	 * the flags don't conflict.
	 */
	if (((other_config = kcpc_next_config(token, NULL, NULL)) != NULL) &&
	    (other_config->pcbe_flags != flags))
		return (CPC_CONFLICTING_REQS);

	cfg = kmem_alloc(sizeof (*cfg), KM_SLEEP);

	cfg->pcbe_picno = picnum;
	cfg->pcbe_evsel = evsel;
	cfg->pcbe_flags = flags;
	cfg->pcbe_pic = (uint32_t)preset;

	*data = cfg;
	return (0);
}

static void
vt_pcbe_program(void *token)
{
	vt_pcbe_config_t	*pic[CPC_VT_NPIC];
	vt_pcbe_config_t	*tmp;
	vt_pcbe_config_t	nullcfg = { 0, 0, 0, 0 };
	uint64_t		pcr;
	uint64_t		curpic;
	uint8_t			bitmap = 0;
	uint64_t		toe[CPC_VT_NPIC];
	int			i;

	tmp = (vt_pcbe_config_t *)kcpc_next_config(token, NULL, NULL);

	while (tmp != NULL) {
		ASSERT(tmp->pcbe_picno < CPC_VT_NPIC);
		pic[tmp->pcbe_picno] = tmp;
		bitmap |= (uint8_t)(1 << tmp->pcbe_picno);
		tmp = (vt_pcbe_config_t *)kcpc_next_config(token, tmp, NULL);
	}

	if (bitmap == 0)
		panic("vt_pcbe: token %p has no configs", token);

	/* Fill in unused pic config */
	for (i = 0; i < CPC_VT_NPIC; i++) {
		if (bitmap & (1 << i)) {
			toe[i] = 1;
			continue;
		}

		pic[i] = &nullcfg;
		pic[i]->pcbe_picno = i;
		toe[i] = 0;
	}

	/*
	 * For each counter, initialize event settings and
	 * counter values.
	 */
	for (i = 0; i < CPC_VT_NPIC; i++) {
		if ((bitmap & (1 << i)) == 0)
			continue;

		(void) hv_niagara_setperf((uint64_t)i, allstopped);
		vt_setpic((uint64_t)i, (uint64_t)pic[i]->pcbe_pic);

		pcr = allstopped;
		pcr |= ((uint64_t)pic[i]->pcbe_evsel & CPC_PCR_EVSEL_MASK) <<
		    CPC_PCR_MASK_SHIFT;

		if (pic[i]->pcbe_flags & CPC_COUNT_USER)
			pcr |= (1ull << CPC_PCR_UT_SHIFT);
		if (pic[i]->pcbe_flags & CPC_COUNT_SYSTEM)
			pcr |= (1ull << CPC_PCR_ST_SHIFT);
		if (pic[i]->pcbe_flags & CPC_COUNT_HV)
			pcr |= (1ull << CPC_PCR_HT_SHIFT);
		pcr |= toe[i] << CPC_PCR_TOE_SHIFT;

		DTRACE_PROBE1(vt__setpcr, uint64_t, pcr);
		if (hv_niagara_setperf((uint64_t)i, pcr) != H_EOK) {
			kcpc_invalidate_config(token);
			return;
		}
	}

	/*
	 * On UltraSPARC, only read-to-read counts are accurate. We cannot
	 * expect the value we wrote into the PIC, above, to be there after
	 * starting the counter. We must sample the counter value now and use
	 * that as the baseline for future samples.
	 */
	for (i = 0; i < CPC_VT_NPIC; i++) {
		if ((bitmap & (1 << i)) == 0)
			continue;

		curpic = vt_getpic((uint64_t)i);
		pic[i]->pcbe_pic = (uint32_t)(curpic & PIC_MASK);

		DTRACE_PROBE1(vt__newpic, uint64_t, curpic);
	}
}

static void
vt_pcbe_allstop(void)
{
	for (int i = 0; i < CPC_VT_NPIC; i++)
		(void) hv_niagara_setperf((uint64_t)i, allstopped);
}

static void
vt_pcbe_sample(void *token)
{
	uint64_t		curpic;
	int64_t			diff;
	uint64_t		*pic_data[CPC_VT_NPIC];
	vt_pcbe_config_t	*pic[CPC_VT_NPIC];
	vt_pcbe_config_t	*ctmp = NULL;

	for (int i = 0; i < CPC_VT_NPIC; i++) {
		if ((pic[i] = kcpc_next_config(token, ctmp, &pic_data[i]))
		    != NULL) {
			curpic = vt_getpic((uint64_t)pic[i]->pcbe_picno);
			DTRACE_PROBE1(vt__getpic, uint64_t, curpic);

			diff = curpic - (uint64_t)pic[i]->pcbe_pic;
			if (diff < 0)
				diff += (1ll << 32);
			*pic_data[i] += diff;

			pic[i]->pcbe_pic = (uint32_t)curpic;
			ctmp = pic[i];
		}
	}
}

static void
vt_pcbe_free(void *config)
{
	kmem_free(config, sizeof (vt_pcbe_config_t));
}

static struct modlpcbe modlpcbe = {
	&mod_pcbeops,
	"SPARC T4 Performance Counters",
	&vt_pcbe_ops
};

static struct modlinkage modl = {
	MODREV_1,
	&modlpcbe,
};

int
_init(void)
{
	if (vt_pcbe_init() != 0)
		return (ENOTSUP);
	return (mod_install(&modl));
}

int
_fini(void)
{
	return (mod_remove(&modl));
}

int
_info(struct modinfo *mi)
{
	return (mod_info(&modl, mi));
}

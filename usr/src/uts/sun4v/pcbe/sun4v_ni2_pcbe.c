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
 * T2/T2+/T3 (a.k.a. N2, VF, KT) Performance Counter Backend support
 * for the sun4v_pcbe module.
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
#include <sys/sun4v_pcbe.h>

/*LINTLIBRARY*/
int ni2_pcbe_init(void);
void ni2_pcbe_fini(void);
static uint_t ni2_pcbe_ncounters(void);
static const char *ni2_pcbe_impl_name(void);
static const char *ni2_pcbe_cpuref(void);
static char *ni2_pcbe_list_events(uint_t picnum);
static char *ni2_pcbe_list_attrs(void);
static uint64_t ni2_pcbe_event_coverage(char *event);
static uint64_t ni2_pcbe_overflow_bitmap(void);
static int ni2_pcbe_configure(uint_t picnum, char *event, uint64_t preset,
    uint32_t flags, uint_t nattrs, kcpc_attr_t *attrs, void **data,
    void *token);
static void ni2_pcbe_program(void *token);
static void ni2_pcbe_allstop(void);
static void ni2_pcbe_sample(void *token);
static void ni2_pcbe_free(void *config);

extern void ultra_setpcr(uint64_t);
extern uint64_t ultra_getpcr(void);
extern void ultra_setpic(uint64_t);
extern uint64_t ultra_getpic(void);
extern uint64_t ultra_gettick(void);
extern char cpu_module_name[];
extern int hv_getperf_func_num;
extern int hv_setperf_func_num;

#define	L2_dmiss_ld_event	0x320
int pcbe_l2dmiss_ovtrap_enable = 0;

pcbe_ops_t ni2_pcbe_ops = {
	PCBE_VER_1,
	CPC_CAP_OVERFLOW_INTERRUPT | CPC_CAP_OVERFLOW_PRECISE,
	ni2_pcbe_ncounters,
	ni2_pcbe_impl_name,
	ni2_pcbe_cpuref,
	ni2_pcbe_list_events,
	ni2_pcbe_list_attrs,
	ni2_pcbe_event_coverage,
	ni2_pcbe_overflow_bitmap,
	ni2_pcbe_configure,
	ni2_pcbe_program,
	ni2_pcbe_allstop,
	ni2_pcbe_sample,
	ni2_pcbe_free
};

#define	ULTRA_PCR_PRIVPIC	(UINT64_C(1) << CPC_PCR_PRIV_SHIFT)

static const uint64_t	allstopped = (ULTRA_PCR_PRIVPIC |
	CPC_PCR_HOLDOV0 | CPC_PCR_HOLDOV1);

/*
 * We update this array in the program and allstop routine. The array
 * is checked in the sample routine to allow us to only perform the
 * PCR.ht bit check when counting is in progress.
 */
static boolean_t ni2_cpc_counting[NCPU];

static pcbe_event_t ni2_events[] = {
	{ "Idle_strands",			0x000, 0x00 },
	{ "Br_completed",			0x201, 0xff },
	{ "Br_taken",				0x202, 0xff },
	{ "Instr_FGU_arithmetic",		0x204, 0xff },
	{ "Instr_ld",				0x208, 0xff },
	{ "Instr_st",				0x210, 0xff },
	{ "Instr_sw",				0x220, 0xff },
	{ "Instr_other",			0x240, 0xff },
	{ "Atomics",				0x280, 0xff },
	{ "Instr_cnt",				0x2fd, 0xff },
	{ "IC_miss",				0x301, 0x33 },
	{ "DC_miss",				0x302, 0x33 },
	{ "L2_imiss",				0x310, 0x33 },
	{ "L2_dmiss_ld",			0x320, 0x33 },
	{ "ITLB_HWTW_ref_L2",			0x404, 0x3c },
	{ "DTLB_HWTW_ref_L2",			0x408, 0x3c },
	{ "ITLB_HWTW_miss_L2",			0x410, 0x3c },
	{ "DTLB_HWTW_miss_L2",			0x420, 0x3c },
	{ "Stream_ld_to_PCX",			0x501, 0x3f },
	{ "Stream_st_to_PCX",			0x502, 0x3f },
	{ "CPU_ld_to_PCX",			0x504, 0x3f },
	{ "CPU_ifetch_to_PCX",			0x508, 0x3f },
	{ "CPU_st_to_PCX",			0x510, 0x3f },
	{ "MMU_ld_to_PCX",			0x520, 0x3f },
	{ "DES_3DES_op",			0x601, 0x3f },
	{ "AES_op",				0x602, 0x3f },
	{ "RC4_op",				0x604, 0x3f },
	{ "MD5_SHA-1_SHA-256_op",		0x608, 0x3f },
	{ "MA_op",				0x610, 0x3f },
	{ "CRC_TCPIP_cksum",			0x620, 0x3f },
	{ "DES_3DES_busy_cycle",		0x701, 0x3f },
	{ "AES_busy_cycle",			0x702, 0x3f },
	{ "RC4_busy_cycle",			0x704, 0x3f },
	{ "MD5_SHA-1_SHA-256_busy_cycle",	0x708, 0x3f },
	{ "MA_busy_cycle",			0x710, 0x3f },
	{ "CRC_MPA_cksum",			0x720, 0x3f },
	{ "ITLB_miss",				0xb04, 0x0c },
	{ "DTLB_miss",				0xb08, 0x0c },
	{ "TLB_miss",				0xb0c, 0x0c },
	PCBE_EV_END
};

static pcbe_event_t vf_events[] = {
	{ "Idle_strands",			0x000, 0x00 },
	{ "Br_completed",			0x201, 0xff },
	{ "Br_taken",				0x202, 0xff },
	{ "Instr_FGU_arithmetic",		0x204, 0xff },
	{ "Instr_ld",				0x208, 0xff },
	{ "Instr_st",				0x210, 0xff },
	{ "Instr_sw",				0x220, 0xff },
	{ "Instr_other",			0x240, 0xff },
	{ "Atomics",				0x280, 0xff },
	{ "Instr_cnt",				0x2fd, 0xff },
	{ "IC_miss",				0x301, 0x33 },
	{ "DC_miss",				0x302, 0x33 },
	{ "L2_imiss",				0x310, 0x33 },
	{ "L2_dmiss_ld",			0x320, 0x33 },
	{ "ITLB_HWTW_ref_L2",			0x404, 0x3c },
	{ "DTLB_HWTW_ref_L2",			0x408, 0x3c },
	{ "ITLB_HWTW_miss_L2",			0x410, 0x3c },
	{ "DTLB_HWTW_miss_L2",			0x420, 0x3c },
	{ "Stream_ld_to_PCX",			0x501, 0x3f },
	{ "Stream_st_to_PCX",			0x502, 0x3f },
	{ "CPU_ld_to_PCX",			0x504, 0x3f },
	{ "CPU_ifetch_to_PCX",			0x508, 0x3f },
	{ "CPU_st_to_PCX",			0x510, 0x3f },
	{ "MMU_ld_to_PCX",			0x520, 0x3f },
	{ "DES_3DES_op",			0x601, 0x3f },
	{ "AES_op",				0x602, 0x3f },
	{ "RC4_op",				0x604, 0x3f },
	{ "MD5_SHA-1_SHA-256_op",		0x608, 0x3f },
	{ "MA_op",				0x610, 0x3f },
	{ "CRC_TCPIP_cksum",			0x620, 0x3f },
	{ "DES_3DES_busy_cycle",		0x701, 0x3f },
	{ "AES_busy_cycle",			0x702, 0x3f },
	{ "RC4_busy_cycle",			0x704, 0x3f },
	{ "MD5_SHA-1_SHA-256_busy_cycle",	0x708, 0x3f },
	{ "MA_busy_cycle",			0x710, 0x3f },
	{ "CRC_MPA_cksum",			0x720, 0x3f },
	{ "ITLB_miss",				0xb04, 0x0c },
	{ "DTLB_miss",				0xb08, 0x0c },
	{ "TLB_miss",				0xb0c, 0x0c },
	PCBE_EV_END
};

static pcbe_event_t kt_events[] = {
	{ "Idle_strands",			0x000, 0x00 },
	{ "Br_completed",			0x201, 0xff },
	{ "Br_taken",				0x202, 0xff },
	{ "Instr_FGU_arithmetic",		0x204, 0xff },
	{ "Instr_ld",				0x208, 0xff },
	{ "Instr_st",				0x210, 0xff },
	{ "Instr_sw",				0x220, 0xff },
	{ "Instr_other",			0x240, 0xff },
	{ "Atomics",				0x280, 0xff },
	{ "Instr_cnt",				0x2fd, 0xff },
	{ "IC_miss",				0x301, 0x33 },
	{ "DC_miss",				0x302, 0x33 },
	{ "L2_imiss",				0x310, 0x33 },
	{ "L2_dmiss_ld",			0x320, 0x33 },
	{ "ITLB_HWTW_ref_L2",			0x404, 0x3c },
	{ "DTLB_HWTW_ref_L2",			0x408, 0x3c },
	{ "ITLB_HWTW_miss_L2",			0x410, 0x3c },
	{ "DTLB_HWTW_miss_L2",			0x420, 0x3c },
	{ "Stream_ld_to_PCX",			0x501, 0x3f },
	{ "Stream_st_to_PCX",			0x502, 0x3f },
	{ "CPU_ld_to_PCX",			0x504, 0x3f },
	{ "CPU_ifetch_to_PCX",			0x508, 0x3f },
	{ "CPU_st_to_PCX",			0x510, 0x3f },
	{ "MMU_ld_to_PCX",			0x520, 0x3f },
	{ "DES_3DES_op",			0x601, 0xff },
	{ "AES_op",				0x602, 0xff },
	{ "Kasumi_op",				0x604, 0xff },
	{ "MD5_SHA-1_SHA-256_op",		0x608, 0xff },
	{ "MA_op",				0x610, 0xff },
	{ "CRC_TCPIP_cksum",			0x620, 0xff },
	{ "DES_3DES_busy_cycle",		0x701, 0xff },
	{ "AES_busy_cycle",			0x702, 0xff },
	{ "Kasumi_busy_cycle",			0x704, 0xff },
	{ "MD5_SHA-1_SHA-256_busy_cycle",	0x708, 0xff },
	{ "MA_busy_cycle",			0x710, 0xff },
	{ "CRC_MPA_cksum",			0x720, 0xff },
	{ "ITLB_miss",				0xb04, 0x0c },
	{ "DTLB_miss",				0xb08, 0x0c },
	{ "TLB_miss",				0xb0c, 0x0c },
	PCBE_EV_END
};

/*
 * T2/T2+/T3 generic events
 */
static pcbe_generic_event_t ni2_pcbe_generic_events[] = {
	{ "PAPI_tot_ins",	"Instr_cnt" },
	{ "PAPI_l1_dcm",	"DC_miss" },
	{ "PAPI_l1_icm",	"IC_miss" },
	{ "PAPI_l2_icm",	"L2_imiss" },
	{ "PAPI_l2_ldm",	"L2_dmiss_ld" },
	{ "PAPI_tlb_dm",	"DTLB_miss" },
	{ "PAPI_tlb_im",	"ITLB_miss" },
	{ "PAPI_tlb_tm",	"TLB_miss" },
	{ "PAPI_br_tkn",	"Br_taken" },
	{ "PAPI_br_ins",	"Br_completed" },
	{ "PAPI_ld_ins",	"Instr_ld" },
	{ "PAPI_sr_ins",	"Instr_st" },
	{ "PAPI_fp_ops",	"Instr_FGU_arithmetic" },
	{ "PAPI_fp_ins",	"Instr_FGU_arithmetic" },
	PCBE_GEN_EV_END
};

static uint16_t 	pcr_pic0_mask;
static uint16_t 	pcr_pic1_mask;

typedef struct	n2_cpu_pcbe {
	hsvc_info_t	cpu_hsvc;
	char		*cpu_impl_name;
} n2_pcbe_t;

n2_pcbe_t	n2_pcbe_hv_apis[] = {
	{
		{
		    HSVC_REV_1, NULL, HSVC_GROUP_KT_CPU,
		    KT_HSVC_MAJOR, KT_HSVC_MINOR,
		    "SPARC-T3 PCBE"
		},
		"SPARC T3"
	},
	{
		{
		    HSVC_REV_1, NULL, HSVC_GROUP_VFALLS_CPU,
		    VFALLS_HSVC_MAJOR, VFALLS_HSVC_MINOR,
		    "SUNW,UltraSPARC-T2+ PCBE"
		},
		"UltraSPARC T2+"
	},
	{
		{
		    HSVC_REV_1, NULL, HSVC_GROUP_NIAGARA2_CPU,
		    NIAGARA2_HSVC_MAJOR, NIAGARA2_HSVC_MINOR,
		    "SUNW,UltraSPARC-T2 PCBE"
		},
		"UltraSPARC T2"
	}
};

typedef enum n2_pcbe_type {
	KT_CPU = 0,
	VF_CPU,
	N2_CPU
} n2_pcbe_type_t;

static n2_pcbe_type_t	pcbe_type;

#define	MAX_N2_PCBES	(sizeof (n2_pcbe_hv_apis) / sizeof (n2_pcbe_t))

static const char	*pcbe_cpu_impl_name;
static const char	*pcbe_cpu_pcbe_ref;

static const char	*n2_cpu_pcbe_ref = "See the \"UltraSPARC T2 "
			"Supplement to UltraSPARC "
			"Architecture 2007 User's Manual\" "
			"for descriptions of these events.";

static const char	*n2plus_cpu_pcbe_ref = "See the \"UltraSPARC T2 "
			"Supplement to UltraSPARC "
			"Architecture 2007 User's Manual\" "
			"for descriptions of these events.";

static const char	*kt_cpu_pcbe_ref = "See the \"SPARC T3 "
			"Supplement to UltraSPARC "
			"Architecture 2007 User's Manual\" "
			"for descriptions of these events.";
int
ni2_pcbe_init(void)
{
	int			status;
	uint64_t		cpu_hsvc_minor;
	int			i;

	pcr_pic0_mask = CPC_PCR_PIC0_MASK;
	pcr_pic1_mask = CPC_PCR_PIC1_MASK;

	/*
	 * Look for and register a supported HV API.
	 */
	for (i = 0; i < MAX_N2_PCBES; i++) {
		status = hsvc_register(&n2_pcbe_hv_apis[i].cpu_hsvc,
		    &cpu_hsvc_minor);
		if (status == 0)
			break;
	}

	if (i >= MAX_N2_PCBES) {
		return (-1);
	}

	pcbe_cpu_impl_name = n2_pcbe_hv_apis[i].cpu_impl_name;

	pcbe_type = (n2_pcbe_type_t)i;

	switch (pcbe_type) {
	case KT_CPU:
		hv_getperf_func_num = HV_KT_GETPERF;
		hv_setperf_func_num = HV_KT_SETPERF;
		pcbe_eventsp = kt_events;
		pcbe_cpu_pcbe_ref = kt_cpu_pcbe_ref;
		break;

	case VF_CPU:
		hv_getperf_func_num = HV_VFALLS_GETPERF;
		hv_setperf_func_num = HV_VFALLS_SETPERF;
		pcbe_eventsp = vf_events;
		pcbe_cpu_pcbe_ref = n2plus_cpu_pcbe_ref;
		break;

	case N2_CPU:
		hv_getperf_func_num = HV_NIAGARA2_GETPERF;
		hv_setperf_func_num = HV_NIAGARA2_SETPERF;
		pcbe_eventsp = ni2_events;
		pcbe_cpu_pcbe_ref = n2_cpu_pcbe_ref;
		break;
	default:
		return (-1);
	}

	pcbe_generic_eventsp = ni2_pcbe_generic_events;

	return (0);
}

void
ni2_pcbe_fini(void)
{
	hsvc_info_t	*hsvcp;
	int		status;

	if (pcbe_type != KT_CPU && pcbe_type != VF_CPU && pcbe_type != N2_CPU)
		cmn_err(CE_WARN, "bogus pcbe_type %d", (int)pcbe_type);

	hsvcp = &n2_pcbe_hv_apis[pcbe_type].cpu_hsvc;

	status =  hsvc_unregister(hsvcp);
	if (status == EINVAL) {
		cmn_err(CE_WARN, "%s Perf HV API not previously registered",
		    n2_pcbe_hv_apis[pcbe_type].cpu_impl_name);
		return;
	}
}

static uint_t
ni2_pcbe_ncounters(void)
{
	return (2);
}

static const char *
ni2_pcbe_impl_name(void)
{
	return (pcbe_cpu_impl_name);
}

static const char *
ni2_pcbe_cpuref(void)
{
	return (pcbe_cpu_pcbe_ref);
}

static char *
ni2_pcbe_list_events(uint_t picnum)
{
	ASSERT(picnum < cpc_ncounters);

	return (pcbe_evlist);
}

static char *
ni2_pcbe_list_attrs(void)
{
	switch (pcbe_type) {
	case N2_CPU:
		return ("hpriv,emask");
	case VF_CPU:
		return ("hpriv,l2ctl,emask");
	case KT_CPU:
		return ("hpriv,l2ctl,emask,sample");
	default:
		ASSERT(0);	/* This should never happen */
		return (NULL);
	}
}

static pcbe_generic_event_t *
find_generic_event(char *name)
{
	pcbe_generic_event_t	*gevp;

	for (gevp = ni2_pcbe_generic_events; gevp->name != NULL; gevp++) {
		if (strcmp(name, gevp->name) == 0)
			return (gevp);
	}

	return (NULL);
}

static pcbe_event_t *
find_event(char *name)
{
	pcbe_event_t		*evp;

	for (evp = pcbe_eventsp; evp->name != NULL; evp++)
		if (strcmp(name, evp->name) == 0)
			return (evp);

	return (NULL);
}

/*ARGSUSED*/
static uint64_t
ni2_pcbe_event_coverage(char *event)
{
	/*
	 * Check whether counter event is supported
	 */
	if (find_event(event) == NULL && find_generic_event(event) == NULL)
		return (0);

	/*
	 * Fortunately, both pic0 and pic1 can count all events.
	 */
	return (0x3);
}

static uint64_t
ni2_pcbe_overflow_bitmap(void)
{
	uint64_t	pcr, overflow;
	uint64_t	pic;
	uint32_t	pic0, pic1;
	boolean_t	update_pic = B_FALSE;
	boolean_t	pic_inrange = B_FALSE;

	ASSERT(getpil() >= DISP_LEVEL);
	pcr = ultra_getpcr();
	DTRACE_PROBE1(niagara2__getpcr, uint64_t, pcr);
	overflow =  (pcr & CPC_PCR_OV0_MASK) >>
	    CPC_PCR_OV0_SHIFT;
	overflow |=  (pcr & CPC_PCR_OV1_MASK) >>
	    CPC_PCR_OV1_SHIFT;

	pic = ultra_getpic();
	pic0 = (uint32_t)(pic & PIC0_MASK);
	pic1 = (uint32_t)((pic >> PIC1_SHIFT) & PIC0_MASK);

	pcr |= (CPC_PCR_HOLDOV0 | CPC_PCR_HOLDOV1);

	if (overflow & 0x1) {
		pcr &= ~(CPC_PCR_OV0_MASK |
		    CPC_PCR_HOLDOV0);
		pic_inrange = PIC_IN_OV_RANGE(pic0);

		if (pcbe_type == KT_CPU && (pcr & CPC_PCR_SAMPLE_MODE_MASK))
			pic_inrange = SAMPLE_PIC_IN_OV_RANGE(pic0);

		if (pic_inrange) {
			pic0 = 0;
			update_pic = B_TRUE;
		}
	}

	if (overflow & 0x2) {
		pcr &= ~(CPC_PCR_OV1_MASK |
		    CPC_PCR_HOLDOV1);
		pic_inrange = PIC_IN_OV_RANGE(pic1);

		if (pcbe_type == KT_CPU && (pcr & CPC_PCR_SAMPLE_MODE_MASK))
			pic_inrange = SAMPLE_PIC_IN_OV_RANGE(pic1);

		if (pic_inrange) {
			pic1 = 0;
			update_pic = B_TRUE;
		}
	}

	if (update_pic)
		ultra_setpic(((uint64_t)pic1 << PIC1_SHIFT) | pic0);

	/*
	 * The HV interface does not need to be used here because we are
	 * only resetting the OV bits and do not need to set the HT bit.
	 */
	DTRACE_PROBE1(niagara2__setpcr, uint64_t, pcr);
	ultra_setpcr(pcr);

	return (overflow);
}

/*ARGSUSED*/
static int
ni2_pcbe_configure(uint_t picnum, char *event, uint64_t preset, uint32_t flags,
    uint_t nattrs, kcpc_attr_t *attrs, void **data, void *token)
{
	pcbe_config_t	*cfg;
	pcbe_config_t	*other_config;
	pcbe_event_t		*evp;
	pcbe_generic_event_t	*gevp;
	int			i;
	uint32_t		evsel;
	uint64_t		l2ctl = 0;

	/*
	 * If we've been handed an existing configuration, we need only preset
	 * the counter value.
	 */
	if (*data != NULL) {
		cfg = *data;
		cfg->pcbe_pic = (uint32_t)preset;
		return (0);
	}

	if (picnum > 1)
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
		} else if ((pcbe_type == VF_CPU || pcbe_type == KT_CPU) &&
		    strcmp(attrs[i].ka_name, "l2ctl") == 0) {
			if ((attrs[i].ka_val | L2_CTL_MASK) !=
			    L2_CTL_MASK)
				return (CPC_ATTRIBUTE_OUT_OF_RANGE);
			/*
			 * Set PERF_CONTROL bits in L2_CONTROL_REG
			 * only when events have SL bits equal to 3.
			 */
			if ((evsel & SL3_MASK) == SL3_MASK) {
				if ((hv_setperf(HV_L2_CTL,
				    attrs[i].ka_val)) != 0)
					return (CPC_HV_NO_ACCESS);
			}
		} else if (pcbe_type == KT_CPU &&
		    strcmp(attrs[i].ka_name, "sample") == 0) {
			if (attrs[i].ka_val != 0)
				flags |= CPC_COUNT_SAMPLE_MODE;
		} else
			return (CPC_INVALID_ATTRIBUTE);
	}

	if (pcbe_type == VF_CPU || pcbe_type == KT_CPU) {
		/*
		 * Set PERF_CONTROL bits in L2_CONTROL_REG only when events have
		 * SL bits equal to 3.
		 */
		if ((evsel & SL_MASK) == SL3_MASK) {
			if ((hv_setperf(HV_L2_CTL, l2ctl)) != 0)
				return (CPC_HV_NO_ACCESS);
		}
	}

	/*
	 * Find other requests that will be programmed with this one, and ensure
	 * the flags don't conflict.
	 */
	if (((other_config = kcpc_next_config(token, NULL, NULL)) != NULL) &&
	    (other_config->pcbe_flags != flags))
		return (CPC_CONFLICTING_REQS);

	/*
	 * If the hpriv attribute is present, make sure we have
	 * access to hyperprivileged events before continuing with
	 * this configuration. If we can set the ht bit in the PCR
	 * successfully, we must have access to hyperprivileged
	 * events.
	 *
	 * If this is a static per-CPU configuration, the CPC
	 * driver ensures there can not be more than one for this
	 * CPU. If this is a per-LWP configuration, the driver
	 * ensures no static per-CPU counting is ongoing and that
	 * the target LWP is not already being monitored.
	 */
	if (flags & CPC_COUNT_HV) {
		kpreempt_disable();

		DTRACE_PROBE1(niagara2__setpcr, uint64_t,
		    allstopped | CPC_PCR_HT);
		if (hv_setperf(HV_SPARC_CTL,
		    allstopped | CPC_PCR_HT) != H_EOK) {
			kpreempt_enable();
			return (CPC_HV_NO_ACCESS);
		}

		DTRACE_PROBE1(niagara2__setpcr, uint64_t, allstopped);
		(void) hv_setperf(HV_SPARC_CTL, allstopped);

		kpreempt_enable();
	}

	cfg = kmem_alloc(sizeof (*cfg), KM_SLEEP);

	cfg->pcbe_picno = picnum;
	cfg->pcbe_evsel = evsel;
	cfg->pcbe_flags = flags;
	cfg->pcbe_pic = (uint32_t)preset;

	*data = cfg;
	return (0);
}

static void
ni2_pcbe_program(void *token)
{
	pcbe_config_t	*pic0;
	pcbe_config_t	*pic1;
	pcbe_config_t	*tmp;
	pcbe_config_t	nullcfg = { 1, 0, 0, 0 };
	uint64_t		pcr;
	uint64_t		curpic;
	uint64_t		toe;

	/* enable trap-on-event for pic0 and pic1 */
	toe = (CPC_PCR_TOE0 | CPC_PCR_TOE1);

	if ((pic0 = (pcbe_config_t *)kcpc_next_config(token, NULL, NULL)) ==
	    NULL)
		panic("ni2_pcbe: token %p has no configs", token);

	if ((pic1 = kcpc_next_config(token, pic0, NULL)) == NULL) {
		pic1 = &nullcfg;
		nullcfg.pcbe_flags = pic0->pcbe_flags;
		toe = CPC_PCR_TOE0; /* enable trap-on-event for pic0 */
	}

	if (pic0->pcbe_picno != 0) {
		/*
		 * pic0 is counter 1, so if we need the null config it should
		 * be counter 0.
		 */
		nullcfg.pcbe_picno = 0;
		tmp = pic0;
		pic0 = pic1;
		pic1 = tmp;
		toe = CPC_PCR_TOE1; /* enable trap-on-event for pic1 */
	}

	if (pic0->pcbe_picno != 0 || pic1->pcbe_picno != 1) {
		panic("%s: bad config on token %p\n", pcbe_cpu_impl_name,
		    token);
	}

	/*
	 * UltraSPARC does not allow pic0 to be configured differently
	 * from pic1. If the flags on these two configurations are
	 * different, they are incompatible. This condition should be
	 * caught at configure time.
	 */
	ASSERT(pic0->pcbe_flags == pic1->pcbe_flags);

	ni2_pcbe_allstop();

	ultra_setpic(((uint64_t)pic1->pcbe_pic << PIC1_SHIFT) |
	    (uint64_t)pic0->pcbe_pic);

	pcr = (pic0->pcbe_evsel & pcr_pic0_mask) << CPC_PCR_PIC0_SHIFT;
	pcr |= (pic1->pcbe_evsel & pcr_pic1_mask) <<
	    CPC_PCR_PIC1_SHIFT;

	if (pic0->pcbe_flags & CPC_COUNT_USER)
		pcr |= (1ull << CPC_PCR_UT_SHIFT);
	if (pic0->pcbe_flags & CPC_COUNT_SYSTEM)
		pcr |= (1ull << CPC_PCR_ST_SHIFT);
	if (pic0->pcbe_flags & CPC_COUNT_HV)
		pcr |= (1ull << CPC_PCR_HT_SHIFT);

	if (pcbe_type == KT_CPU && (pic0->pcbe_flags & CPC_COUNT_SAMPLE_MODE)) {
		pcr |= (1ull << CPC_PCR_SAMPLE_MODE_SHIFT);
	}

	if (pcbe_l2dmiss_ovtrap_enable == 0) {
		/*
		 * SW workaround for HW Erratum 108.
		 * Disable overflow interrupts when L2_dmiss_ld event is
		 * selected.
		 */
		if ((pic0->pcbe_evsel & L2_dmiss_ld_event) == L2_dmiss_ld_event)
			toe &= ~CPC_PCR_TOE0;

		if ((pic1->pcbe_evsel & L2_dmiss_ld_event) == L2_dmiss_ld_event)
			toe &= ~CPC_PCR_TOE1;
	}
	pcr |= toe;

	DTRACE_PROBE1(niagara2__setpcr, uint64_t, pcr);

	if (pic0->pcbe_flags & CPC_COUNT_HV) {
		/*
		 * The ht bit in the PCR is only writable in
		 * hyperprivileged mode. So if we are counting
		 * hpriv events, we must use the HV interface
		 * hv_setperf to set the PCR. If this
		 * fails, assume we no longer have access to
		 * hpriv events.
		 */
		if (hv_setperf(HV_SPARC_CTL, pcr) != H_EOK) {
			kcpc_invalidate_config(token);
			return;
		}
	} else
		/* Set the PCR with no hpriv event counting enabled. */
		ultra_setpcr(pcr);

	ni2_cpc_counting[CPU->cpu_id] = B_TRUE;

	/*
	 * On UltraSPARC, only read-to-read counts are accurate. We cannot
	 * expect the value we wrote into the PIC, above, to be there after
	 * starting the counter. We must sample the counter value now and use
	 * that as the baseline for future samples.
	 */
	curpic = ultra_getpic();
	pic0->pcbe_pic = (uint32_t)(curpic & PIC0_MASK);
	pic1->pcbe_pic = (uint32_t)(curpic >> PIC1_SHIFT);

	DTRACE_PROBE1(niagara2__newpic, uint64_t, curpic);
}

static void
ni2_pcbe_allstop(void)
{
	/*
	 * We use the HV interface here because if we were counting
	 * hyperprivileged events, we must reset the PCR.ht bit to stop
	 * the counting. In the event that this HV call fails, we fall
	 * back on ultra_setpcr which does not have write access to the
	 * ht bit.
	 */
	if (hv_setperf(HV_SPARC_CTL, allstopped) != H_EOK)
		ultra_setpcr(allstopped);

	ni2_cpc_counting[CPU->cpu_id] = B_FALSE;
}

static void
ni2_pcbe_sample(void *token)
{
	uint64_t		curpic;
	int64_t			diff;
	uint64_t		*pic0_data;
	uint64_t		*pic1_data;
	uint64_t		*dtmp;
	uint64_t		tmp;
	uint64_t		pcr;
	pcbe_config_t	*pic0;
	pcbe_config_t	*pic1;
	pcbe_config_t	nullcfg = { 1, 0, 0, 0 };
	pcbe_config_t	*ctmp;

	curpic = ultra_getpic();
	DTRACE_PROBE1(niagara2__getpic, uint64_t, curpic);

	if ((pic0 = kcpc_next_config(token, NULL, &pic0_data)) == NULL)
		panic("%s: token %p has no configs", pcbe_cpu_impl_name, token);

	if ((pic1 = kcpc_next_config(token, pic0, &pic1_data)) == NULL) {
		pic1 = &nullcfg;
		pic1_data = &tmp;
	}

	if (pic0->pcbe_picno != 0) {
		nullcfg.pcbe_picno = 0;
		ctmp = pic0;
		pic0 = pic1;
		pic1 = ctmp;
		dtmp = pic0_data;
		pic0_data = pic1_data;
		pic1_data = dtmp;
	}

	if (pic0->pcbe_picno != 0 || pic1->pcbe_picno != 1) {
		panic("%s: bad config on token %p\n", pcbe_cpu_impl_name,
		    token);
	}

	if (pic0->pcbe_flags & CPC_COUNT_HV) {
		/*
		 * If the hpriv attribute is present, but the HT bit
		 * is not set in the PCR, access to hyperprivileged
		 * events must have been revoked. Only perform this
		 * check if counting is not stopped.
		 */
		pcr = ultra_getpcr();
		DTRACE_PROBE1(niagara2__getpcr, uint64_t, pcr);
		if (ni2_cpc_counting[CPU->cpu_id] &&
		    !(pcr & CPC_PCR_HT)) {
			kcpc_invalidate_config(token);
			return;
		}
	}

	diff = (curpic & PIC0_MASK) - (uint64_t)pic0->pcbe_pic;
	if (diff < 0)
		diff += (1ll << 32);
	*pic0_data += diff;

	diff = (curpic >> 32) - (uint64_t)pic1->pcbe_pic;
	if (diff < 0)
		diff += (1ll << 32);
	*pic1_data += diff;

	pic0->pcbe_pic = (uint32_t)(curpic & PIC0_MASK);
	pic1->pcbe_pic = (uint32_t)(curpic >> PIC1_SHIFT);
}

static void
ni2_pcbe_free(void *config)
{
	kmem_free(config, sizeof (pcbe_config_t));
}

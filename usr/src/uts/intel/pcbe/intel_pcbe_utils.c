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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
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
#include "pcbe_utils.h"
#include "intel_pcbe_utils.h"

#define	IPCBE_ATTRS IPCBE_ATTR_EDGE "," IPCBE_ATTR_INV "," IPCBE_ATTR_UMASK \
	"," IPCBE_ATTR_CMASK "," IPCBE_ATTR_ANY
static char *ipcbe_attrs = NULL;

#define	IMPL_NAME_LEN	100
static char ipcbe_impl_name[IMPL_NAME_LEN];
static uint8_t	ipcbe_version_id;

/* Fixed Function Counter variables */
static uint8_t	ipcbe_ffc_num = 0;	/* Number of FFC */
static uint64_t	ipcbe_ffc_ctrl_mask;	/* ie. num=0x4 mask=0xf */
static uint8_t	ipcbe_ffc_width;	/* Counter width */
static uint64_t	ipcbe_ffc_mask;		/* mask based on counter width */
#define	IPCBE_PIC2FFC(pic) (pic - ipcbe_gpc_num)

/* General Purpose Counter variables */
static uint8_t	ipcbe_gpc_num = 0;	/* Number of GPC */
static uint64_t	ipcbe_gpc_ctrl_mask;	/* ie. num=0x4 mask=0xf */
static uint8_t	ipcbe_gpc_width;	/* Counter width */
static uint64_t	ipcbe_gpc_mask;		/* mask based on counter width */
static boolean_t ipcbe_gpc_fw_width = B_FALSE; /* Full Write width cap */

/* Architectural Event variables */
static uint8_t	ipcbe_arch_num;		/* Number of arch events */

/*
 * Mask for IA32_PERF_GLOBAL_* MSR FFC and GPC bits
 * - IA32_PERF_GLOBAL_CTRL
 * - IA32_PERF_GLOBAL_STATUS
 * - IA32_PERF_GLOBAL_OVR_CTRL
 * The lower 32 bits are associated with GPC, while the upper 32 are for FFCs
 */
static uint64_t	ipcbe_global_mask;

/* Counter type initialization function */
static void ipcbe_ffc_events_init(struct cpuid_regs *cp);
static void ipcbe_arch_events_init(struct cpuid_regs *cp);
static void ipcbe_gpc_events_init(struct cpuid_regs *cp);

static ipcbe_events_table_t *ipcbe_arch_find_event(char *event);

int
ipcbe_version_get()
{
	return (ipcbe_version_id);
}

/* Part of pcbe_impl_name */
const char *
ipcbe_impl_name_get()
{
	return (ipcbe_impl_name);
}

uint8_t
ipcbe_gpc_num_get()
{
	return (ipcbe_gpc_num);
}

uint64_t
ipcbe_gpc_ctrl_mask_get()
{
	return (ipcbe_gpc_ctrl_mask);
}

uint64_t
ipcbe_gpc_mask_get()
{
	return (ipcbe_gpc_mask);
}

uint64_t
ipcbe_global_mask_get()
{
	return (ipcbe_global_mask);
}

/* Part of pcbe_ncounters */
uint_t
ipcbe_ncounters_get()
{
	return (ipcbe_ffc_num + ipcbe_gpc_num);
}


int
ipcbe_init()
{
	struct cpuid_regs	reg;
	struct cpuid_regs	*cp = &reg;

	/* We only support Intel processors */
	if (cpuid_getvendor(CPU) != X86_VENDOR_Intel)
		goto fail;

	/* Make sure CPU supports Architectural Performance Monitoring */
	cp->cp_eax = CPUID_FUNC_BASIC;
	(void) __cpuid_insn(cp);

	if (cp->cp_eax < CPUID_FUNC_PERF)
		goto fail;

	/* Check to see if we support full write width of PMCi */
	cp->cp_eax = CPUID_FUNC_FEATURE;
	(void) __cpuid_insn(cp);

	if (RECX(CPUID_FEATURE_ECX_PDCM)) {
		uint64_t tmp;
		ia32_perf_capabilities_t *cap;

		cap = (ia32_perf_capabilities_t *)&tmp;
		RDMSR(IA32_PERF_CAPABILITIES, tmp);
		ipcbe_gpc_fw_width = cap->fw_write;
	}

	/* Gather data from Architectural Performance Monitoring Leaf */
	cp->cp_eax = CPUID_FUNC_PERF;
	(void) __cpuid_insn(cp);

	/* We currently only support version 3 */
	ipcbe_version_id = REAX(CPUID_PERF_EAX_VERSION_ID);
	if (ipcbe_version_id != 3)
		goto fail;

	ipcbe_ffc_events_init(cp);
	ipcbe_gpc_events_init(cp);
	ipcbe_arch_events_init(cp);

	ipcbe_global_mask = (ipcbe_ffc_ctrl_mask << 32) | ipcbe_gpc_ctrl_mask;

	(void) snprintf(ipcbe_impl_name, IMPL_NAME_LEN,
	    "Intel Arch PerfMon v%d on Family %d Model %d",
	    ipcbe_version_id, pcbe_family_get(), pcbe_model_get());

	ipcbe_attrs = kmem_alloc(sizeof (IPCBE_ATTRS), KM_SLEEP);
	(void) strcpy(ipcbe_attrs, IPCBE_ATTRS);

	return (0);
fail:
	return (-1);
}

/*
 * Initialize the fixed function event counter tables.  The following ffc table
 * entries must be in order.  Platform names are followed by the generic names
 * with a comma.  If there is no generic name, there is no comma.  ie <platform
 * name>,<generic name>
 *
 * Unlike GPCs, there is no "supported counter" field.  The location of the
 * event relative to the array specifies which counter the FFC is supported in.
 *
 * From section "Additional Architectural Performance Monitoring Extensions" in
 * the Vol 3B spec, 3 of the architectural performance events are counted using
 * 3 fixed function MSRs.  The table below is taken directly from Table
 * "Association of Fixed-Function Performance Counters with Architectural
 * Performance Events" in section "Fixed-function Performance Counters" of the
 * Vol 3B spec.
 */
static char *ipcbe_ffc_name_tbl[] = {
	"instr_retired.any,PAPI_tot_ins",
	"cpu_clk_unhalted.thread,PAPI_tot_cyc",
	"cpu_clk_unhalted.ref"
};
#define	ipcbe_ffc_keys PCBE_TABLE_SIZE(ipcbe_ffc_name_tbl)

static void
ipcbe_ffc_events_init(struct cpuid_regs *cp)
{
	ipcbe_ffc_num = REDX(CPUID_PERF_EDX_FFC_NUM);
	ipcbe_ffc_width = REDX(CPUID_PERF_EDX_FFC_WIDTH);

	/*
	 * The system seems to have more fixed-function counters than what this
	 * PCBE is able to handle correctly.  Default to the maximum number of
	 * fixed-function counters that this driver is aware of.
	 */
	if (ipcbe_ffc_num > ipcbe_ffc_keys)
		ipcbe_ffc_num = ipcbe_ffc_keys;

	ipcbe_ffc_ctrl_mask = BITMASK_XBITS(ipcbe_ffc_num);
	ipcbe_ffc_mask = BITMASK_XBITS(ipcbe_ffc_width);
}



/* Part of pcbe_configure, used to update the preset value. */
void
ipcbe_update_configure(uint64_t preset, void **data)
{
	intel_pcbe_config_t *conf = (intel_pcbe_config_t *)*data;

	if (*data == NULL)
		return;

	if (PCBE_CONF_TYPE(conf) == PCBE_FFC) {
		PCBE_CONF_PRESET(conf) = preset & ipcbe_ffc_mask;
	} else if (IPCBE_IS_GPC_TYPE(PCBE_CONF_TYPE(conf))) {
		PCBE_CONF_PRESET(conf) = preset & ipcbe_gpc_mask;
	}
}

/*
 * Part of pcbe_list_events, which passes in the "pic_num" argument.
 *
 * pic_num refers to a performance counter, where zero starts with general
 * performance counter followed by the fixed function counters.  If the system
 * had 20 GPCs and 3 FFCs, then a pic_num of [0 to 19] would refer to GPCs, and
 * [20 to 22] would refer to FFCs.
 */
char *
ipcbe_ffc_events_name(uint_t pic_num)
{
	uint_t ffc_num = IPCBE_PIC2FFC(pic_num);

	if (ffc_num < ipcbe_ffc_num)
		return (ipcbe_ffc_name_tbl[IPCBE_PIC2FFC(pic_num)]);

	return (NULL);
}


/*
 * Part of pcbe_event_coverage.
 *
 * Based on the "event" name given, find which counters support the event.
 *
 * This function returns a bitmap specifying which counters support the given
 * event.  If there are 5 GPCs and 3 FFCs, then the bitmap would look like:
 * 0bfffggggg
 */
uint64_t
ipcbe_ffc_events_coverage(char *event)
{
	int i;
	uint64_t bitmap = 0;

	for (i = 0; i < ipcbe_ffc_num; i++) {
		if (pcbe_name_compare(ipcbe_ffc_name_tbl[i], event) == 0)
			bitmap |= 1 << (i + ipcbe_gpc_num);
	}

	return (bitmap);
}


/* Part of pcbe_configure */
int
ipcbe_ffc_configure(uint_t pic_num, char *event, uint64_t preset,
    uint32_t flags, uint_t nattrs, kcpc_attr_t *attrs, void **data)
{
	intel_pcbe_config_t	*conf;
	uint8_t			ffc_ctrl = 0;

	if ((IPCBE_PIC2FFC(pic_num)) >= ipcbe_ffc_num)
		return (CPC_INVALID_PICNUM);

	if (!ipcbe_ffc_events_coverage(event))
		return (CPC_INVALID_EVENT);

	/*
	 * Gather IA32_FIXED_CTR_CTRL values.
	 *
	 * The only valid attribute acceptable is "anythr". If more than 1
	 * attribute was passed in, or the attribute is not "anythr", then fail.
	 * Plus check for privileges.
	 */
	if (nattrs) {
		if (nattrs > 1 || !ATTR_CMP(attrs[0].ka_name, IPCBE_ATTR_ANY))
			return (CPC_INVALID_ATTRIBUTE);

		if (attrs[0].ka_val) {
			if (secpolicy_cpc_cpu(CRED()) != 0)
				return (CPC_ATTR_REQUIRES_PRIVILEGE);

			ffc_ctrl |= IA32_FIXED_CTR_CTRL_ANYTHR;
		}
	}

	if (flags & CPC_COUNT_USER)
		ffc_ctrl |= IA32_FIXED_CTR_CTRL_USR_EN;
	if (flags & CPC_COUNT_SYSTEM)
		ffc_ctrl |= IA32_FIXED_CTR_CTRL_OS_EN;
	if (flags & CPC_OVF_NOTIFY_EMT)
		ffc_ctrl |= IA32_FIXED_CTR_CTRL_PMI;


	conf = kmem_alloc(sizeof (intel_pcbe_config_t), KM_SLEEP);
	PCBE_CONF_TYPE(conf) = PCBE_FFC;
	PCBE_CONF_NUM(conf) = IPCBE_PIC2FFC(pic_num);
	PCBE_CONF_PRESET(conf) = preset & ipcbe_ffc_mask;
	PCBE_CONF_FFC_CTRL(conf) = ffc_ctrl;

	*data = conf;
	return (0);
}

/* Part of pcbe_program */
uint64_t
ipcbe_ffc_program(void *token)
{
	intel_pcbe_config_t	*conf;
	uint64_t		fixed_ctr_ctrl = 0;
	uint64_t		perf_global_ctrl = 0;
	uint_t			pic_num;

	for (conf = kcpc_next_config(token, NULL, NULL);
	    conf != NULL;
	    conf = kcpc_next_config(token, conf, NULL)) {

		if (PCBE_CONF_TYPE(conf) != PCBE_FFC)
			continue;

		pic_num = PCBE_CONF_NUM(conf);

		WRMSR(IA32_FIXED_CTR(pic_num), PCBE_CONF_PRESET(conf));

		fixed_ctr_ctrl |= PCBE_CONF_FFC_CTRL(conf) <<
		    (pic_num * IA32_FIXED_CTR_CTRL_ATTR_SIZE);

		perf_global_ctrl |= 1ull << (pic_num + 32);
	}

	/* Set the FFC Control Attributes */
	WRMSR(IA32_FIXED_CTR_CTRL, fixed_ctr_ctrl);

	return (perf_global_ctrl);
}

/*
 * Initialize the general purpose event counter tables
 */
static void
ipcbe_gpc_events_init(struct cpuid_regs *cp)
{
	ipcbe_gpc_num = REAX(CPUID_PERF_EAX_GPC_NUM);
	ipcbe_gpc_width = REAX(CPUID_PERF_EAX_GPC_WIDTH);

	ipcbe_gpc_mask = BITMASK_XBITS(ipcbe_gpc_width);
	ipcbe_gpc_ctrl_mask = BITMASK_XBITS(ipcbe_gpc_num);
}

/*
 * part of pcbe_configure
 *
 * This function configures the common parts of GPC events, such as decoding the
 * attributes.  It also checks if the event is an architectural one or a "raw"
 * event.
 *
 * If event_info is NULL then check to see if it is an architectural or raw
 * event.
 */
int
ipcbe_gpc_configure(uint_t pic_num, char *event, uint64_t preset,
    uint32_t flags, uint_t nattrs, kcpc_attr_t *attrs, void **data,
    ipcbe_events_table_t *event_info)
{
	intel_pcbe_config_t conf = { 0 };
	intel_pcbe_config_t *conf_p = &conf;
	long event_num;
	int i;
	char *name;
	uint64_t val;

	/*
	 * Bits beyond bit-31 in the general-purpose counters can only be
	 * written to by extension of bit 31.  We cannot preset these bits to
	 * any value other than all 1s or all 0s.
	 */
	if (!ipcbe_gpc_fw_width && (preset >> 31) &&
	    ((preset & ipcbe_gpc_mask) >> 31) != (ipcbe_gpc_mask >> 31)) {
		return (CPC_ATTRIBUTE_OUT_OF_RANGE);
	}

	/* Check if this is an architectural event */
	if (event_info) {
		PCBE_CONF_TYPE(conf_p) = PCBE_GPC;
	} else {
		PCBE_CONF_TYPE(conf_p) = PCBE_ARCH;
		event_info = ipcbe_arch_find_event(event);
	}

	/*
	 * If event is found and it is a PAPI event no attributes are allowed.
	 * Else try to treat it as a "raw" event.
	 */
	if (event_info) {
		if (nattrs > 0 && (strncmp("PAPI_", event, 5) == 0))
			return (CPC_ATTRIBUTE_OUT_OF_RANGE);
		PCBE_CONF_EVENT(conf_p) = event_info->event_select;
		PCBE_CONF_UMASK(conf_p) = event_info->unit_mask;
	} else {
		if (ddi_strtol(event, NULL, 0, &event_num) != 0)
			return (CPC_INVALID_EVENT);
		PCBE_CONF_TYPE(conf_p) = PCBE_RAW;
		PCBE_CONF_EVENT(conf_p) = event_num;
	}

	PCBE_CONF_NUM(conf_p) = pic_num;
	PCBE_CONF_PRESET(conf_p) = preset & ipcbe_gpc_mask;

	for (i = 0; i < nattrs; i++) {
		name = attrs[i].ka_name;
		val = attrs[i].ka_val;

		if (ATTR_CMP(name, IPCBE_ATTR_UMASK)) {
			if ((val | IA32_PERFEVTSEL_UMASK_MASK) !=
			    IA32_PERFEVTSEL_UMASK_MASK) {
				return (CPC_ATTRIBUTE_OUT_OF_RANGE);
			}
			PCBE_CONF_UMASK(conf_p) = (uint8_t)val;
		} else  if (ATTR_CMP(name, IPCBE_ATTR_EDGE)) {
			PCBE_CONF_EDGE(conf_p) = (val) ? 1 : 0;
		} else if (ATTR_CMP(name, IPCBE_ATTR_INV)) {
			PCBE_CONF_INV(conf_p) = (val) ? 1 : 0;
		} else if (ATTR_CMP(name, IPCBE_ATTR_CMASK)) {
			if ((attrs[i].ka_val | IA32_PERFEVTSEL_CMASK_MASK) !=
			    IA32_PERFEVTSEL_CMASK_MASK) {
				return (CPC_ATTRIBUTE_OUT_OF_RANGE);
			}
			PCBE_CONF_CMASK(conf_p) = (uint8_t)val;
		} else if (ATTR_CMP(name, IPCBE_ATTR_ANY)) {
			PCBE_CONF_ANY(conf_p) = (val) ? 1 : 0;
		} else if (pcbe_name_compare(ipcbe_attrs, name) != 0) {
			return (CPC_INVALID_ATTRIBUTE);
		}
	}

	if (flags & CPC_COUNT_USER)
		PCBE_CONF_USR(conf_p) = 1;
	if (flags & CPC_COUNT_SYSTEM)
		PCBE_CONF_OS(conf_p) = 1;
	if (flags & CPC_OVF_NOTIFY_EMT)
		PCBE_CONF_INTR(conf_p) = 1;
	PCBE_CONF_EN(conf_p) = 1;

	*data = kmem_alloc(sizeof (intel_pcbe_config_t), KM_SLEEP);
	*((intel_pcbe_config_t *)*data) = conf;

	return (0);
}

/*
 * Write to PMCi based on chip capability. If chip supports full 64 bit writes
 * always use the 64 bit msr, otherwise use the 32 bit msr.
 *
 * If the data passed does not fit into the 32 bit msr, write zero instead.
 * This scenario should theoretically never occur, since the data is checked
 * during the "configure" phase.
 */
uint64_t
ipcbe_pmc_write(uint64_t data, uint_t pic_num)
{
	/* If full writes are supported, just write to alternate PMC */
	if (ipcbe_gpc_fw_width) {
		WRMSR(IA32_A_PMC(pic_num), data);
		return (data);
	}

	/*
	 * General-purpose counter registers have write restrictions where only
	 * the lower 32-bits can be written to.  The rest of the relevant bits
	 * are written to by extension from bit 31 (all ZEROS if bit-31 is ZERO
	 * and all ONEs if bit-31 is ONE).  This makes it possible to write to
	 * the counter register values that only have all ONEs or all ZEROs in
	 * the higher bits.
	 */
	if ((data >> 31 == 0) ||
	    ((data & ipcbe_gpc_mask) >> 31 == (ipcbe_gpc_mask >> 31))) {
		WRMSR(IA32_PMC(pic_num), data);
		return (data);
	}

	WRMSR(IA32_PMC(pic_num), 0);
	return (0);
}

/*
 * Initialize the architectural event counter tables.  The following arch table
 * entries must be in order.  Generic names are followed by the platform names
 * with a comma.  If there is no generic name, there is no comma.  ie <platform
 * name>,<generic name>
 *
 * Order matters, see section "Pre-defined Architectural Performance
 * Events" of the Vol 3B spec for definition of these predefined events.
 */
static ipcbe_events_table_t ipcbe_arch_events_tbl[] = {
	{ 0x3c, 0x00, C_ALL, "cpu_clk_unhalted.thread_p,PAPI_tot_cyc" },
	{ 0xc0, 0x00, C_ALL, "inst_retired.any_p,PAPI_tot_ins" },
	{ 0x3c, 0x01, C_ALL, "cpu_clk_unhalted.ref_p" },
	{ 0x2e, 0x4f, C_ALL, "longest_lat_cache.reference" },
	{ 0x2e, 0x41, C_ALL, "longest_lat_cache.miss" },
	{ 0xc4, 0x00, C_ALL, "br_inst_retired.all_branches" },
	{ 0xc5, 0x00, C_ALL, "br_misp_retired.all_branches" }
};
#define	ipcbe_arch_events_keys PCBE_TABLE_SIZE(ipcbe_arch_events_tbl)

static void
ipcbe_arch_events_init(struct cpuid_regs *cp)
{
	int keys;
	uint64_t arch_events_vector;


	ipcbe_arch_num = REAX(CPUID_PERF_EAX_EBX_LENGTH);

	/* Only support the known arch numbers */
	ipcbe_arch_num = (ipcbe_arch_events_keys < ipcbe_arch_num) ?
	    ipcbe_arch_events_keys : ipcbe_arch_num;

	/*
	 * Mask off any architectural events that are not supported by this
	 * model as defined in the CPUID.A EBX register.
	 */
	arch_events_vector = cp->cp_ebx;
	for (keys = 0; keys < ipcbe_arch_num; keys++) {
		if (C(keys) & arch_events_vector)
			ipcbe_arch_events_tbl[keys].supported_counters = 0;
	}
}

/*
 * Part of pcbe_event_coverage.
 *
 * See comments in pcbe_ffc_events_coverage.  Architectural events are
 * considered part of GPCs.
 */
uint64_t
ipcbe_arch_events_coverage(char *event)
{
	int i;
	char *name;
	uint64_t bitmap = 0;

	for (i = 0; i < ipcbe_arch_num; i++) {
		name = ipcbe_arch_events_tbl[i].name;
		if (pcbe_name_compare(name, event) != 0)
			continue;

		bitmap |= ipcbe_arch_events_tbl[i].supported_counters;
		break;
	}

	return (bitmap & ipcbe_gpc_ctrl_mask);
}

static ipcbe_events_table_t *
ipcbe_arch_find_event(char *event) {
	int i;
	char *names;

	for (i = 0; i < ipcbe_arch_num; i++) {
		names = ipcbe_arch_events_tbl[i].name;
		if (pcbe_name_compare(names, event) == 0)
			return (&ipcbe_arch_events_tbl[i]);
	}

	return (NULL);
}

/*
 * Utility function to take in a table and create a name list.
 */
char *
ipcbe_create_name_list(ipcbe_counter_support_t cb, int pic_num)
{
	char	*name, *list;
	int	length = 0, i;
	uint_t	supported_counters;

	/* Get the length of the arch events +1 for comma */
	for (i = 0; i < ipcbe_arch_num; i++) {
		if (C(pic_num) & ipcbe_arch_events_tbl[i].supported_counters) {
			length += strlen(ipcbe_arch_events_tbl[i].name) + 1;
		}
	}

	/* Get the length of the GPCs in the cb */
	for (i = 0, name = cb(i, &supported_counters);
	    name;
	    name = cb(++i, &supported_counters)) {
		if (C(pic_num) & supported_counters) {
			length += strlen(name) + 1;
		}
	}

	if (!length)
		return (NULL);

	/* Allocate memory and concat all the names */
	list = kmem_alloc(length, KM_SLEEP);
	list[0] = '\0';

	for (i = 0; i < ipcbe_arch_num; i++) {
		if (C(pic_num) & ipcbe_arch_events_tbl[i].supported_counters) {
			(void) strcat(list, ipcbe_arch_events_tbl[i].name);
			(void) strcat(list, ",");
		}
	}

	for (i = 0, name = cb(i, &supported_counters);
	    name;
	    name = cb(++i, &supported_counters)) {
		if (C(pic_num) & supported_counters) {
			(void) strcat(list, name);
			(void) strcat(list, ",");
		}
	}

	/* Remove trailing comma */
	list[length - 1] = '\0';

	return (list);
}

/* pcbe_overflow_bitmap ops */
uint64_t
ipcbe_overflow_bitmap(void)
{
	uint64_t interrupt_status;
	uint64_t intrbits_ffc;
	uint64_t intrbits_gpc;
	extern int kcpc_hw_overflow_intr_installed;
	uint64_t overflow_bitmap;

	RDMSR(IA32_PERF_GLOBAL_STATUS, interrupt_status);
	WRMSR(IA32_PERF_GLOBAL_OVF_CTRL, interrupt_status);

	/* Mask out only the ffc and gpc interrupts */
	intrbits_ffc = (interrupt_status >> 32) & ipcbe_ffc_mask;
	intrbits_gpc = interrupt_status & ipcbe_gpc_mask;
	overflow_bitmap = (intrbits_ffc << ipcbe_gpc_num) | intrbits_gpc;

	ASSERT(kcpc_hw_overflow_intr_installed);
	(*kcpc_hw_enable_cpc_intr)();

	return (overflow_bitmap);
}

/* Part of pcbe_allstop */
void
ipcbe_allstop(void)
{
	/* Disable all the counters together */
	WRMSR(IA32_PERF_GLOBAL_CTRL, 0x0);

	setcr4(getcr4() & ~CR4_PCE);
}

/* Part of pcbe_list_attrs */
char *
ipcbe_attrs_list()
{
	return (ipcbe_attrs);
}

void
ipcbe_attrs_add(char *attr)
{
	char *temp_attrs;
	size_t attrs_size = strlen(ipcbe_attrs);

	/* Check to see if the attr is already in the attr list */
	if (pcbe_name_compare(ipcbe_attrs, attr) == 0)
		return;

	/* Allocate new memory, +1 for NULL, +1 for comma */
	temp_attrs = kmem_alloc(attrs_size + strlen(attr) + 2, KM_SLEEP);
	(void) strcpy(temp_attrs, ipcbe_attrs);
	(void) strcat(temp_attrs, ",");
	(void) strcat(temp_attrs, attr);
	kmem_free(ipcbe_attrs, attrs_size + 1);
	ipcbe_attrs = temp_attrs;
}

/* Part of pcbe_sample */
void
ipcbe_sample(void *token)
{
	intel_pcbe_config_t	*conf;
	uint64_t		value, *data, mask;
	uint_t			pic_num;

	for (conf = kcpc_next_config(token, NULL, &data);
	    conf != NULL;
	    conf = kcpc_next_config(token, conf, &data)) {
		pic_num = conf->pic_num;

		if (PCBE_CONF_TYPE(conf) == PCBE_FFC) {
			RDMSR(IA32_FIXED_CTR(pic_num), value);
			mask = ipcbe_ffc_mask;
		} else if (IPCBE_IS_GPC_TYPE(PCBE_CONF_TYPE(conf))) {
			RDMSR(IA32_PMC(pic_num), value);
			mask = ipcbe_gpc_mask;
		} else {
			continue;
		}

		DTRACE_PROBE3(ipcbe_sample_loop,
		    loop, conf,
		    uint64_t, value,
		    uint64_t, *data);

		value &= mask;

		/* Adjust result if counter overflow since our last sample */
		if (value < PCBE_CONF_PRESET(conf)) {
			*data += mask + 1;
		}
		*data += value - PCBE_CONF_PRESET(conf);

		/* Reset the preset value for next sample */
		PCBE_CONF_PRESET(conf) = value;
	}
}

/* Part of pcbe_free */
void
ipcbe_free(void *config)
{

	intel_pcbe_config_t *conf = (intel_pcbe_config_t *)config;

	kmem_free(conf, sizeof (intel_pcbe_config_t));
}

boolean_t
ipcbe_is_gpc(uint_t pic_num)
{
	return (pic_num < ipcbe_gpc_num);
}

boolean_t
ipcbe_is_ffc(uint_t pic_num)
{
	if ((pic_num >= ipcbe_gpc_num) &&
	    (pic_num < (ipcbe_gpc_num + ipcbe_ffc_num)))
		return (B_TRUE);
	return (B_FALSE);
}

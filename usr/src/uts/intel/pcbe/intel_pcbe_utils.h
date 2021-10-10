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

#ifndef _SYS_INTEL_PCBE_UTILS_H
#define	_SYS_INTEL_PCBE_UTILS_H

#ifdef	__cplusplus
extern "C" {
#endif

/* Core 2 Models */
#define	FAM6_MOD_CORE2_0F	0x0F	/* 15 */
#define	FAM6_MOD_CORE2_17	0x17	/* 23 */
#define	FAM6_MOD_CORE2_1D	0x1D	/* 29 */

/* Atom Models */
#define	FAM6_MOD_ATOM_1C	0x1C	/* 12 */

/* Nehelem Models */
#define	FAM6_MOD_NHM_1A		0x1A	/* 26 */
#define	FAM6_MOD_NHM_1E		0x1E	/* 30 */
#define	FAM6_MOD_NHM_1F		0x1F	/* 31 */
#define	FAM6_MOD_NHM_EX_2E	0x2E	/* 46 - Nehelem EX */

/* Westmere Models */
#define	FAM6_MOD_WSM_25		0x25	/* 37 */
#define	FAM6_MOD_WSM_2C		0x2c	/* 44 */
#define	FAM6_MOD_WSM_EX_2F	0x2F	/* 47 */

/* Sandy Bridge Models */
#define	FAM6_MOD_SND_BDG_2A	0x2A	/* 42 */
#define	FAM6_MOD_SND_BDG_2D	0x2D	/* 45 */

/* Intel CPUID Function Numbers */
#define	CPUID_FUNC_FEATURE	0x1	/* Feature Information */
#define	CPUID_FUNC_PERF		0xA	/* Arch Perf Monitoring Leaf */

/*
 * Feature Information ECX
 *
 * For PCBE code we are only interested in the PDCM bit
 */
#define	CPUID_FEATURE_ECX_PDCM		15	/* Perf/Debug Cap MSR */
#define	CPUID_FEATURE_ECX_PDCM_MASK	0x1

/*
 * Architectural Performance Monitoring EAX
 * - Version ID of architectural performance monitoring
 * - Number of general purpose performance monitoring counters
 * - Width of general purpose performance monitoring counter
 * - Length of EBX bit vector used to enumerate perf events
 */
#define	CPUID_PERF_EAX_VERSION_ID	0
#define	CPUID_PERF_EAX_VERSION_ID_MASK	0xFF
#define	CPUID_PERF_EAX_GPC_NUM		8
#define	CPUID_PERF_EAX_GPC_NUM_MASK	0xFF
#define	CPUID_PERF_EAX_GPC_WIDTH	16
#define	CPUID_PERF_EAX_GPC_WIDTH_MASK	0xFF
#define	CPUID_PERF_EAX_EBX_LENGTH	24
#define	CPUID_PERF_EAX_EBX_LENGTH_MASK	0xFF

/*
 * Architectural Performance Monitoring EBX
 * - See lengths specified by CPUID_PERF_EAX_EBX_LENGTH
 *
 * Known architecturally defined events (see ipcbe_arch_events_tbl)
 * - Core Cycles
 * - Instruction Retired
 * - Reference Cycles
 * - Last-level Cache Reference
 * - Last-level Cache Misses
 * - Branch Instructions Retired
 * - Branch Miss-predicts Retired
 */

/*
 * Architectural Performance Monitoring EDX
 * - Number of fixed-function performance counters (if Ver > 0x1)
 * - Width of fixed-function performance counters (if Ver > 0x1)
 */
#define	CPUID_PERF_EDX_FFC_NUM		0
#define	CPUID_PERF_EDX_FFC_NUM_MASK	0x1F
#define	CPUID_PERF_EDX_FFC_WIDTH	5
#define	CPUID_PERF_EDX_FFC_WIDTH_MASK	0xFF

typedef struct ipcbe_events_table {
	uint8_t		event_select;
	uint8_t		unit_mask;
	uint32_t	supported_counters;
	char		*name;
} ipcbe_events_table_t;

/* Counter types that use the PMC vs FFC or UNCORE registers */
#define	IPCBE_IS_GPC_TYPE(t) \
	((t == PCBE_ARCH) || (t == PCBE_RAW) || (t == PCBE_GPC))

/*
 * The following MSR offsets register definitions and their names were taken
 * from the Vol 3B spec, Appendix B.
 */
#define	IA32_PMC0			0x0c1	/* 1st GPC (PERFCTR0) */
#define	IA32_PMC(pic) (IA32_PMC0 + pic)		/* has counter values */
#define	IA32_A_PMC0			0x4c1	/* 64bit version of PMC */
#define	IA32_A_PMC(pic) (IA32_A_PMC0 + pic)
#define	IA32_PERFEVTSEL0		0x186	/* 1st GPC Event Select reg */
#define	IA32_PERFEVTSEL(pic) (IA32_PERFEVTSEL0 + pic)
#define	IA32_PERFEVTSEL_EVENT		0
#define	IA32_PERFEVTSEL_EVENT_MASK	0xFF
#define	IA32_PERFEVTSEL_UMASK		8	/* See starting section 30.3 */
#define	IA32_PERFEVTSEL_UMASK_MASK	0xFF	/* for umask definitions */
#define	IA32_PERFEVTSEL_CMASK		24
#define	IA32_PERFEVTSEL_CMASK_MASK	0xFF
typedef struct ia32_perfevtsel {
	uint64_t event		: 8, /* Event Select */
		umask		: 8, /* Unit Mask */
		usr		: 1, /* User Mode */
		os		: 1, /* Operating system mode */
		edge   		: 1, /* Edge detect */
		pc		: 1, /* Pin control */
		intr		: 1, /* APIC interrupt enable */
		any		: 1, /* Any thread */
		en		: 1, /* Enable counters */
		inv		: 1, /* Invert counter mask */
		cmask		: 8, /* Counter Mask */
		reserved	: 32;
} ia32_perfevtsel_t;

/*
 * Valid user attributes in ia32_perfevtsel.
 * usr, os, intr are controlled by kcpc not pcbe.
 *
 * Pin Control[PC] is specifically not part of the
 * attrs as there is currently no use case for it
 */
#define	IPCBE_ATTR_EDGE		"edge"
#define	IPCBE_ATTR_INV		"inv"
#define	IPCBE_ATTR_UMASK	"umask"
#define	IPCBE_ATTR_CMASK	"cmask"
#define	IPCBE_ATTR_ANY		"anythr"
#define	IPCBE_ATTR_MSR_OFFCORE	"msr_offcore"

#define	IA32_MISC_ENABLE		0x1a0
#define	OFFCORE_RSP_0			0x1a6
#define	OFFCORE_RSP_1			0x1a7
#define	LBR_SELECT			0x1c8
#define	MSR_LASTBRANCH_TOS		0x1c9
#define	IA32_DEBUGCTL			0x1d9

#define	IA32_FIXED_CTR0			0x309	/* 1st FFC */
#define	IA32_FIXED_CTR(pic) (IA32_FIXED_CTR0 + pic)

#define	IA32_PERF_CAPABILITIES		0x345
typedef struct ia32_perf_capabilities {
	uint64_t lbr_fmt	: 6,
		pebs_trap	: 1,
		pebs_arch_reg	: 1,
		pebs_rec_fmt	: 4,
		smm_frz		: 1,
		fw_write	: 1,
		reserved	: 50;
} ia32_perf_capabilities_t;

#define	IA32_FIXED_CTR_CTRL		0x38d	/* Used to ena/dis FFCs */
#define	IA32_FIXED_CTR_CTRL_ATTR_SIZE	4

/* Fixed-function counter attributes */
#define	IA32_FIXED_CTR_CTRL_OS_EN	(1ULL << 0) /* ring 0 */
#define	IA32_FIXED_CTR_CTRL_USR_EN	(1ULL << 1) /* ring 1 */
#define	IA32_FIXED_CTR_CTRL_ANYTHR	(1ULL << 2) /* any thread on core */
#define	IA32_FIXED_CTR_CTRL_PMI		(1ULL << 3) /* interrupt on overflow */

#define	IA32_PERF_GLOBAL_STATUS		0x38e	/* Overflow status register */
#define	IA32_PERF_GLOBAL_CTRL		0x38f	/* Used to ena/dis counting */
#define	IA32_PERF_GLOBAL_OVF_CTRL	0x390	/* Used to clear ovf status */

#define	PEBS_LD_LAT_THRESHOLD		0x3f6
#define	IA32_DS_AREA			0x600
#define	MSR_LASTBRANCH_x_FROM_ID	0x680
#define	MSR_LASTBRANCH_x_TO_ID		0x6c0

typedef struct intel_pcbe_config {
	pcbe_type_t	pic_type;
	uint_t		pic_num;	/* Counter number */
	uint64_t	pic_preset;	/* Counter preset value */
	void		*cookie;	/* Platform-specific info */
	union {
		ia32_perfevtsel_t	ia32_perfevtsel;
		uint8_t			ia32_fixed_ctr_ctrl;
		uint32_t		data;
	} reg;
} intel_pcbe_config_t;
#define	PCBE_CONF_TYPE(c)	(c->pic_type)
#define	PCBE_CONF_NUM(c)	(c->pic_num)
#define	PCBE_CONF_PRESET(c)	(c->pic_preset)
#define	PCBE_CONF_COOKIE(c)	(c->cookie)
#define	PCBE_CONF_EVENT(c)	(c->reg.ia32_perfevtsel.event)
#define	PCBE_CONF_UMASK(c)	(c->reg.ia32_perfevtsel.umask)
#define	PCBE_CONF_USR(c)	(c->reg.ia32_perfevtsel.usr)
#define	PCBE_CONF_OS(c)		(c->reg.ia32_perfevtsel.os)
#define	PCBE_CONF_EDGE(c)	(c->reg.ia32_perfevtsel.edge)
#define	PCBE_CONF_PC(c)		(c->reg.ia32_perfevtsel.pc)
#define	PCBE_CONF_INTR(c)	(c->reg.ia32_perfevtsel.intr)
#define	PCBE_CONF_ANY(c)	(c->reg.ia32_perfevtsel.any)
#define	PCBE_CONF_EN(c)		(c->reg.ia32_perfevtsel.en)
#define	PCBE_CONF_INV(c)	(c->reg.ia32_perfevtsel.inv)
#define	PCBE_CONF_CMASK(c)	(c->reg.ia32_perfevtsel.cmask)
#define	PCBE_CONF_GPC_CTRL(c)	(c->reg.data)
#define	PCBE_CONF_FFC_CTRL(c)	(c->reg.ia32_fixed_ctr_ctrl)


/* Generic PCBE info functions */
int ipcbe_version_get();
const char *ipcbe_impl_name_get();
uint8_t ipcbe_gpc_num_get();
uint64_t ipcbe_gpc_ctrl_mask_get();
uint64_t ipcbe_gpc_mask_get();
uint64_t ipcbe_global_mask_get();
uint_t ipcbe_ncounters_get();
boolean_t ipcbe_is_gpc(uint_t picnum);
boolean_t ipcbe_is_ffc(uint_t picnum);

/* Common counter functions */
int ipcbe_init();
typedef char *(*ipcbe_counter_support_t)(int row, uint_t *counter);
char *ipcbe_create_name_list(ipcbe_counter_support_t cb, int counter);
void ipcbe_update_configure(uint64_t preset, void **data);
uint64_t ipcbe_overflow_bitmap();
char *ipcbe_attrs_list();
void ipcbe_attrs_add(char *attr);
void ipcbe_sample(void *token);
void ipcbe_allstop();
void ipcbe_free(void *config);

/* Fixed function counter event functions */
char *ipcbe_ffc_events_name(uint_t picnum);
uint64_t ipcbe_ffc_events_coverage(char *event);
int ipcbe_ffc_configure(uint_t picnum, char *event, uint64_t preset,
    uint32_t flags, uint_t nattrs, kcpc_attr_t *attrs, void **data);
uint64_t ipcbe_ffc_program(void *token);

/* Generic General Purpose Counter Event Functions */
int ipcbe_gpc_configure(uint_t picnum, char *event, uint64_t preset,
    uint32_t flags, uint_t nattrs, kcpc_attr_t *attrs, void **data,
    ipcbe_events_table_t *event_info);
uint64_t ipcbe_pmc_write(uint64_t data, uint_t pic_num);

/* Architectural Event (GPC) Functions */
uint64_t ipcbe_arch_events_coverage(char *event);


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_INTEL_PCBE_UTILS_H */

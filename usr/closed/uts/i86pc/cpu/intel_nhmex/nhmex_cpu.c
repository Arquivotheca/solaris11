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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Intel model-specific support for Nehalem-ex.
 */

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/modctl.h>
#include <sys/mca_x86.h>
#include <sys/cpu_module_ms_impl.h>
#include <sys/mc_intel.h>
#include <sys/fastboot_impl.h>
#include <sys/pci_cfgspace.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/fm/smb/fmsmb.h>
#include <sys/spl.h>
#include <io/intel_nhmex/nhmex_mem_addr.h>
#include "nhmex_cpu.h"

extern int x86gentopo_legacy;

int nhmex_ms_support_disable = 0;
int nhmex_error_action_return = 0;
int nhmex_ms_unconstrained = 0;

int max_bus_number = 0xff;

static int disable_fastreboot;
static int mc_cap_recover = 0;

#define	SOCKET_BUS(cpu)	(max_bus_number - (cpu))

#define	MC_COR_ECC_CNT(chipid, reg)	(*pci_getl_func)(SOCKET_BUS(chipid), \
    NEHALEM_EP_MEMORY_CONTROLLER_DEV, NEHALEM_EP_MEMORY_CONTROLLER_FUNC, \
    0x80 + (reg) * 4)
/* nhmex memory payload item */
#define	NHMEX_MEM_ERR_LOG(chipid, i, j)	(*pci_getl_func)(SOCKET_BUS(chipid), \
    NHMEX_MEM_CONTROLLER_DEV + (i) * 2, \
    NHMEX_MEM_CONTROLLER_FUNC, 0x94 + (j) * 4)

/* NHMEX bank number */
#define	NHMEX_BANK_RBOX0	0
#define	NHMEX_BANK_RBOX1	1
#define	NHMEX_BANK_UBOX		6
#define	NHMEX_BANK_MBOX0	8
#define	NHMEX_BANK_MBOX1	9
#define	NHMEX_BANK_BBOX0	10
#define	NHMEX_BANK_BBOX1	11
#define	NHMEX_BANK_SBOX0	12
#define	NHMEX_BANK_SBOX1	13
#define	NHMEX_BANK_CBOX0	14
#define	NHMEX_BANK_CBOX1	15
#define	NHMEX_BANK_CBOX2	16
#define	NHMEX_BANK_CBOX3	17
#define	NHMEX_BANK_CBOX4	18
#define	NHMEX_BANK_CBOX5	19
#define	NHMEX_BANK_CBOX6	20
#define	NHMEX_BANK_CBOX7	21

#define	MSCOD_MEM_ECC_READ	0x1
#define	MSCOD_MEM_ECC_SCRUB	0x2
#define	MSCOD_MEM_WR_PARITY	0x4
#define	MSCOD_MEM_REDUNDANT_MEM	0x8
#define	MSCOD_MEM_SPARE_MEM	0x10
#define	MSCOD_MEM_ILLEGAL_ADDR	0x20
#define	MSCOD_MEM_BAD_ID	0x40
#define	MSCOD_MEM_ADDR_PARITY	0x80
#define	MSCOD_MEM_BYTE_PARITY	0x100

/* MBOX MSCOD for NHMEX */
#define	MSCOD_M_LNKTRNS		0x1
#define	MSCOD_M_LNKPERS		0x2
#define	MSCOD_M_LNKUNCOR	0x4
#define	MSCOD_M_SBFBDLINKERR	0x8
#define	MSCOD_M_NBFBDLNKERR	0x10
#define	MSCOD_M_LNKCRCVLD	0x20
#define	MSCOD_M_PTRL_FSM_ERR	0x40
#define	MSCOD_M_ERRFLW_FSM_FAIL	0x80
#define	MSCOD_M_MCPAR_FSMERR	0x100
#define	MSCOD_M_VBERR		0x200
#define	MSCOD_M_FBERR		0x400
#define	MSCOD_M_MEMECCERR	0x800
#define	MSCOD_M_FBDFRMPAR	0x1000
#define	MSCOD_M_FAILOVER_TO_MIR	0x2000
#define	MSCOD_M_SYNVLD		0x8000

/* RBOX MSCOD for NHMEX */
#define	MSCOD_R_OPR_SINGLE_ECC		0x1
#define	MSCOD_R_IPR_CRC			0x2
#define	MSCOD_R_OPR_RETRY_ABORT		0x4
#define	MSCOD_R_IPR_LINK_INIT		0x8
#define	MSCOD_R_OPR_ERROR		0x10
#define	MSCOD_R_EOT_PARITY		0x20
#define	MSCOD_R_RTA_PARITY		0x40
#define	MSCOD_R_IPR_BAD_SBU_ROUTE	0x80
#define	MSCOD_R_IPR_BAD_MSG		0x100
#define	MSCOD_R_IPR_BAD_VN_CREDIT	0x200
#define	MSCOD_R_OPR_HDR_DOUBLD_ECC	0x400
#define	MSCOD_R_OPR_LINK_RETRY_ERR	0x800
#define	MSCOD_R_POISON			0x8000
#define	MSCOD_R_OPR_POISON_ERROR	(MSCOD_R_POISON | MSCOD_R_OPR_ERROR)

/* SBox MSCOD for NHMEX */
#define	MSCOD_S_MASK		0x1f
#define	MSCOD_S_POISON		0x8000

/* BBox MSCOD for NHMEX */
#define	MSCOD_B_MASK		0xffff

/* UBox MSCOD for NHMEX */
#define	MSCOD_U_MASK		0x1fff
#define	MSCOD_U_CFA		0x2000

#define	NHMEX_ERROR_MEM	0x1000
#define	NHMEX_ERROR_QUICKPATH	0x2000
#define	NHMEX_ERROR_M_BOX	0x4000
#define	NHMEX_ERROR_R_BOX	0x8000
#define	NHMEX_ERROR_B_BOX	0x10000
#define	NHMEX_ERROR_S_BOX	0x20000
#define	NHMEX_ERROR_U_BOX	0x40000
#define	NHMEX_ERROR_C_BOX	0x80000

#define	NHMEX_ERR_SPARE_MEM		(NHMEX_ERROR_MEM | 1)
#define	NHMEX_ERR_MEM_UE		(NHMEX_ERROR_MEM | 2)
#define	NHMEX_ERR_MEM_CE		(NHMEX_ERROR_MEM | 3)
#define	NHMEX_ERR_MEM_PARITY		(NHMEX_ERROR_MEM | 4)
#define	NHMEX_ERR_MEM_ADDR_PARITY	(NHMEX_ERROR_MEM | 5)
#define	NHMEX_ERR_MEM_REDUNDANT 	(NHMEX_ERROR_MEM | 6)
#define	NHMEX_ERR_MEM_BAD_ADDR		(NHMEX_ERROR_MEM | 7)
#define	NHMEX_ERR_MEM_BAD_ID		(NHMEX_ERROR_MEM | 8)

#define	NHMEX_ERR_MEM_LNKTRNS		(NHMEX_ERROR_M_BOX | 1)
#define	NHMEX_ERR_MEM_LNKPERS		(NHMEX_ERROR_M_BOX | 2)
#define	NHMEX_ERR_MEM_LNKPERS_UC	(NHMEX_ERROR_M_BOX | 3)
#define	NHMEX_ERR_MEM_LINKUNCORR	(NHMEX_ERROR_M_BOX | 4)
#define	NHMEX_ERR_MEM_SBFBDLINKERR	(NHMEX_ERROR_M_BOX | 5)
#define	NHMEX_ERR_MEM_SBFBDLINKERR_UC	(NHMEX_ERROR_M_BOX | 6)
#define	NHMEX_ERR_MEM_NBFBDLNKERR	(NHMEX_ERROR_M_BOX | 7)
#define	NHMEX_ERR_MEM_NBFBDLNKERR_UC	(NHMEX_ERROR_M_BOX | 8)
#define	NHMEX_ERR_MEM_LNKCRCVLD		(NHMEX_ERROR_M_BOX | 9)
#define	NHMEX_ERR_MEM_LNKCRCVLD_UC	(NHMEX_ERROR_M_BOX | 0xa)
#define	NHMEX_ERR_MEM_PTRL_FMS_FAIL	(NHMEX_ERROR_M_BOX | 0xb)
#define	NHMEX_ERR_MEM_PTRL_FMS_FAIL_UC	(NHMEX_ERROR_M_BOX | 0xc)
#define	NHMEX_ERR_MEM_ERRFLW_FAIL	(NHMEX_ERROR_M_BOX | 0xd)
#define	NHMEX_ERR_MEM_ERRFLW_FAIL_UC	(NHMEX_ERROR_M_BOX | 0xe)
#define	NHMEX_ERR_MEM_MCPAR_FSMERR	(NHMEX_ERROR_M_BOX | 0xf)
#define	NHMEX_ERR_MEM_VBERR		(NHMEX_ERROR_M_BOX | 0x10)
#define	NHMEX_ERR_MEM_VBERR_UC		(NHMEX_ERROR_M_BOX | 0x11)
#define	NHMEX_ERR_MEM_FBERR		(NHMEX_ERROR_M_BOX | 0x12)
#define	NHMEX_ERR_MEM_ECC		(NHMEX_ERROR_M_BOX | 0x13)
#define	NHMEX_ERR_MEM_ECC_UC		(NHMEX_ERROR_M_BOX | 0x14)
#define	NHMEX_ERR_MEM_FBDFRMPAR	(NHMEX_ERROR_M_BOX | 0x15)
#define	NHMEX_ERR_MEM_FBDFRMPAR_UC	(NHMEX_ERROR_M_BOX | 0x16)
#define	NHMEX_ERR_MEM_MIR_FAIL		(NHMEX_ERROR_M_BOX | 0x17)
#define	NHMEX_ERR_MEM_SCRUB		(NHMEX_ERROR_M_BOX | 0x18)
#define	NHMEX_ERR_MEM_UNKNOWN		(NHMEX_ERROR_M_BOX | 0xfff)

#define	NHMEX_ERR_BUS_SECC		(NHMEX_ERROR_R_BOX | 1)
#define	NHMEX_ERR_BUS_IPR_CRC		(NHMEX_ERROR_R_BOX | 2)
#define	NHMEX_ERR_BUS_IPR_CRC_EX	(NHMEX_ERROR_R_BOX | 3)
#define	NHMEX_ERR_BUS_OPR_POISON	(NHMEX_ERROR_R_BOX | 4)
#define	NHMEX_ERR_BUS_OPR_POISON_EX	(NHMEX_ERROR_R_BOX | 5)
#define	NHMEX_ERR_BUS_OPR_RA		(NHMEX_ERROR_R_BOX | 6)
#define	NHMEX_ERR_BUS_IPR_LI		(NHMEX_ERROR_R_BOX | 7)
#define	NHMEX_ERR_BUS_EOT_P		(NHMEX_ERROR_R_BOX | 8)
#define	NHMEX_ERR_BUS_EOT_P_EX		(NHMEX_ERROR_R_BOX | 9)
#define	NHMEX_ERR_BUS_RTA_P		(NHMEX_ERROR_R_BOX | 0x10)
#define	NHMEX_ERR_BUS_RTA_P_EX		(NHMEX_ERROR_R_BOX | 0x11)
#define	NHMEX_ERR_BUS_BAD_SBU		(NHMEX_ERROR_R_BOX | 0x12)
#define	NHMEX_ERR_BUS_BAD_SBU_EX	(NHMEX_ERROR_R_BOX | 0x13)
#define	NHMEX_ERR_BUS_BAD_MSG		(NHMEX_ERROR_R_BOX | 0x14)
#define	NHMEX_ERR_BUS_BAD_MSG_EX	(NHMEX_ERROR_R_BOX | 0x15)
#define	NHMEX_ERR_BUS_BAD_VN		(NHMEX_ERROR_R_BOX | 0x16)
#define	NHMEX_ERR_BUS_BAD_VN_EX		(NHMEX_ERROR_R_BOX | 0x17)
#define	NHMEX_ERR_BUS_DECC		(NHMEX_ERROR_R_BOX | 0x18)
#define	NHMEX_ERR_BUS_DECC_EX		(NHMEX_ERROR_R_BOX | 0x19)
#define	NHMEX_ERR_BUS_LRETRY		(NHMEX_ERROR_R_BOX | 0x20)
#define	NHMEX_ERR_BUS_LRETRY_EX		(NHMEX_ERROR_R_BOX | 0x21)
#define	NHMEX_ERR_BUS_CACHE_UC		(NHMEX_ERROR_S_BOX | 0x01)
#define	NHMEX_ERR_BUS_HOME_UC		(NHMEX_ERROR_B_BOX | 0x01)
#define	NHMEX_ERR_BUS_CFG_UC		(NHMEX_ERROR_U_BOX | 0x01)
#define	NHMEX_ERR_BUS_CFA_ECC		(NHMEX_ERROR_U_BOX | 0x02)
#define	NHMEX_ERR_BUS_LLC_EWB		(NHMEX_ERROR_C_BOX | 0x01)


#define	MSR_MC_MISC_MEM_CHANNEL_MASK	0x00000000000c0000ULL
#define	MSR_MC_MISC_MEM_CHANNEL_SHIFT	18
#define	MSR_MC_MISC_MEM_DIMM_MASK	0x0000000000030000ULL
#define	MSR_MC_MISC_MEM_DIMM_SHIFT	16
#define	MSR_MC_MISC_MEM_SYNDROME_MASK	0xffffffff00000000ULL
#define	MSR_MC_MISC_MEM_SYNDROME_SHIFT	32
#define	MSR_MC_MISC_RBOX_EXTERNAL_MASK	0x3
#define	MSR_MC_MISC_RBOX_INTERNAL_MASK	0xc
#define	CPU_GENERATION_DONT_CARE	0
#define	CPU_GENERATION_NEHALEM_EP	1
#define	CPU_GENERATION_NEHALEM_EX	2

#define	NEHALEM_EP_MEMORY_CONTROLLER_DEV	0x3
#define	NEHALEM_EP_MEMORY_CONTROLLER_FUNC	0x2

#define	NHMEX_MEM_CONTROLLER_DEV	0x5
#define	NHMEX_MEM_CONTROLLER_FUNC	0x4

#define	IS_SW_RECOVERY_P(cap)	(((cap) & INTEL_MCA_SW_RECOVERY_PRESENT) != 0)
#define	ERRCODE_IS_OVERFLOW(code)	((code) & 0x4000000000000000)
#define	ERRCODE_IS_MEMSCRUB(code)	(((code) & 0xf0) == 0xc0)
#define	ERRCODE_IS_LLCEWB(code)	((code) == 0x17a)

#define	INTEL_MS_CLASS_FLAG_UC	1
#define	NHMEX_MS_CLASS_FLAG_EXTERNAL	2

typedef struct mscookie_class {
	uintptr_t cookie;
	const char *leafclass;
	uint16_t mscode;
	uint16_t msmask;
	int flag;
} mscookie_class_t;

static mscookie_class_t nhmex_mscookie_class[] = {
	/* NHMEX quick path MS errors */
	{ NHMEX_ERR_BUS_SECC, "quickpath.bus_single_ecc", \
	    MSCOD_R_OPR_SINGLE_ECC, NULL, NULL },
	{ NHMEX_ERR_BUS_IPR_CRC, "quickpath.bus_crc_flit", \
	    MSCOD_R_IPR_CRC, NULL, NULL },
	{ NHMEX_ERR_BUS_IPR_CRC_EX, "quickpath.bus_crc_flit_external", \
	    MSCOD_R_IPR_CRC, NULL, NHMEX_MS_CLASS_FLAG_EXTERNAL },
	{ NHMEX_ERR_BUS_OPR_RA, "quickpath.bus_retry_abort", \
	    MSCOD_R_OPR_RETRY_ABORT, NULL, NULL },
	{ NHMEX_ERR_BUS_IPR_LI, "quickpath.bus_link_init_ce", \
	    MSCOD_R_IPR_LINK_INIT, NULL, NULL },
	{ NHMEX_ERR_BUS_OPR_POISON, "quickpath.bus_opr_poison_err", \
	    MSCOD_R_OPR_POISON_ERROR, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_OPR_POISON_EX,\
	    "quickpath.bus_opr_poison_err_external", \
	    MSCOD_R_OPR_POISON_ERROR, NULL, NHMEX_MS_CLASS_FLAG_EXTERNAL |\
	    INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_EOT_P, "quickpath.bus_eot_parity", \
	    MSCOD_R_EOT_PARITY, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_EOT_P_EX, "quickpath.bus_eot_parity_external", \
	    MSCOD_R_EOT_PARITY, NULL, NHMEX_MS_CLASS_FLAG_EXTERNAL |\
	    INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_RTA_P, "quickpath.bus_rta_parity", \
	    MSCOD_R_RTA_PARITY, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_RTA_P_EX, "quickpath.bus_rta_parity_external", \
	    MSCOD_R_RTA_PARITY, NULL, NHMEX_MS_CLASS_FLAG_EXTERNAL |\
	    INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_BAD_SBU, "quickpath.bus_bad_sbu_route", \
	    MSCOD_R_IPR_BAD_SBU_ROUTE, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_BAD_SBU_EX, "quickpath.bus_bad_sbu_route_external", \
	    MSCOD_R_IPR_BAD_SBU_ROUTE, NULL, NHMEX_MS_CLASS_FLAG_EXTERNAL |\
	    INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_BAD_MSG, "quickpath.bus_bad_msg", \
	    MSCOD_R_IPR_BAD_MSG, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_BAD_MSG_EX, "quickpath.bus_bad_msg_external", \
	    MSCOD_R_IPR_BAD_MSG, NULL, NHMEX_MS_CLASS_FLAG_EXTERNAL |\
	    INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_BAD_VN, "quickpath.bus_bad_vn_credit", \
	    MSCOD_R_IPR_BAD_VN_CREDIT, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_BAD_VN_EX, "quickpath.bus_bad_vn_credit_external", \
	    MSCOD_R_IPR_BAD_VN_CREDIT, NULL, NHMEX_MS_CLASS_FLAG_EXTERNAL |\
	    INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_DECC, "quickpath.bus_hdr_double_ecc", \
	    MSCOD_R_OPR_HDR_DOUBLD_ECC, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_DECC_EX, "quickpath.bus_hdr_double_ecc_external", \
	    MSCOD_R_OPR_HDR_DOUBLD_ECC, NULL, NHMEX_MS_CLASS_FLAG_EXTERNAL |\
	    INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_LRETRY, "quickpath.bus_link_retry_err", \
	    MSCOD_R_OPR_LINK_RETRY_ERR, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_LRETRY_EX, "quickpath.bus_link_retry_err_external", \
	    MSCOD_R_OPR_LINK_RETRY_ERR, NULL, INTEL_MS_CLASS_FLAG_UC |\
	    NHMEX_MS_CLASS_FLAG_EXTERNAL },
	/* NHMEX memory MS errors */
	{ NHMEX_ERR_MEM_LNKTRNS, "quickpath.mem_lnktrns", \
	    MSCOD_M_LNKTRNS, NULL, NULL },
	{ NHMEX_ERR_MEM_LNKPERS, "quickpath.mem_lnkpers", MSCOD_M_LNKPERS, \
	    MSCOD_M_LNKPERS | MSCOD_M_SBFBDLINKERR | MSCOD_M_NBFBDLNKERR | \
	    MSCOD_M_LNKCRCVLD, NULL },
	{ NHMEX_ERR_MEM_LNKPERS_UC, "quickpath.mem_lnkpers_uc", \
	    MSCOD_M_LNKPERS, MSCOD_M_LNKPERS | MSCOD_M_SBFBDLINKERR | \
	    MSCOD_M_NBFBDLNKERR | MSCOD_M_LNKCRCVLD, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_LINKUNCORR, "quickpath.mem_lnkuncorr_uc", \
	    MSCOD_M_LNKUNCOR, MSCOD_M_LNKUNCOR | MSCOD_M_SBFBDLINKERR | \
	    MSCOD_M_NBFBDLNKERR | MSCOD_M_LNKCRCVLD, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_SBFBDLINKERR, "quickpath.mem_sbfbdlinkerr", \
	    MSCOD_M_SBFBDLINKERR, \
	    MSCOD_M_SBFBDLINKERR | MSCOD_M_LNKTRNS | MSCOD_M_LNKCRCVLD, NULL },
	{ NHMEX_ERR_MEM_SBFBDLINKERR_UC, "quickpath.mem_sbfbdlinkerr_uc", \
	    MSCOD_M_SBFBDLINKERR, MSCOD_M_SBFBDLINKERR | MSCOD_M_LNKCRCVLD, \
	    INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_NBFBDLNKERR, "quickpath.mem_nbfbdlnkerr", \
	    MSCOD_M_NBFBDLNKERR, \
	    MSCOD_M_NBFBDLNKERR | MSCOD_M_LNKTRNS | MSCOD_M_LNKCRCVLD, NULL },
	{ NHMEX_ERR_MEM_NBFBDLNKERR_UC, "quickpath.mem_nbfbdlnkerr_uc", \
	    MSCOD_M_NBFBDLNKERR, MSCOD_M_NBFBDLNKERR | MSCOD_M_LNKCRCVLD, \
	    INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_LNKCRCVLD, "quickpath.mem_lnkcrcvld", \
	    MSCOD_M_LNKCRCVLD, NULL, NULL },
	{ NHMEX_ERR_MEM_LNKCRCVLD_UC, "quickpath.mem_lnkcrcvld_uc", \
	    MSCOD_M_LNKCRCVLD, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_PTRL_FMS_FAIL, "quickpath.mem_ptrl_fsm_err", \
	    MSCOD_M_PTRL_FSM_ERR, NULL, NULL },
	{ NHMEX_ERR_MEM_PTRL_FMS_FAIL_UC, "quickpath.mem_ptrl_fsm_err_uc", \
	    MSCOD_M_PTRL_FSM_ERR, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_ERRFLW_FAIL, "quickpath.mem_errflw_fsm_fail", \
	    MSCOD_M_ERRFLW_FSM_FAIL, NULL, NULL },
	{ NHMEX_ERR_MEM_ERRFLW_FAIL_UC, "quickpath.mem_errflw_fsm_fail_uc", \
	    MSCOD_M_ERRFLW_FSM_FAIL, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_MCPAR_FSMERR, "quickpath.mem_mcpar_fsmerr_uc", \
	    MSCOD_M_MCPAR_FSMERR, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_VBERR, "quickpath.mem_vberr", \
	    MSCOD_M_VBERR, NULL, NULL },
	{ NHMEX_ERR_MEM_VBERR_UC, "quickpath.mem_vberr_uc", \
	    MSCOD_M_VBERR, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_FBERR, "quickpath.mem_fberr_uc", \
	    MSCOD_M_FBERR, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_ECC, "quickpath.mem_ecc", \
	    MSCOD_M_MEMECCERR, NULL, NULL },
	{ NHMEX_ERR_MEM_ECC_UC, "quickpath.mem_ecc_uc", \
	    MSCOD_M_MEMECCERR, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_FBDFRMPAR, "quickpath.mem_even_parity", \
	    MSCOD_M_FBDFRMPAR, NULL, NULL },
	{ NHMEX_ERR_MEM_FBDFRMPAR_UC, "quickpath.mem_even_parity_uc", \
	    MSCOD_M_FBDFRMPAR, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_MIR_FAIL, "quickpath.mem_failover_mir", \
	    MSCOD_M_FAILOVER_TO_MIR, NULL, INTEL_MS_CLASS_FLAG_UC },
	/* NHMEX other MS errors */
	{ NHMEX_ERR_BUS_CACHE_UC, "quickpath.system_cache_uc", \
	    MSCOD_S_MASK, MSCOD_S_MASK, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_HOME_UC, "quickpath.home_agent_uc", \
	    MSCOD_B_MASK, MSCOD_B_MASK, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_CFG_UC, "quickpath.sys_cfg_uc", \
	    MSCOD_U_MASK, MSCOD_U_MASK, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_BUS_CFA_ECC, "quickpath.sys_cfg_cfa_ecc", \
	    MSCOD_U_CFA, NULL, NULL },
	/* Intel ucr errors */
	{ NHMEX_ERR_BUS_LLC_EWB, "quickpath.llc_ewb_uc", \
	    NULL, NULL, INTEL_MS_CLASS_FLAG_UC },
	{ NHMEX_ERR_MEM_SCRUB, "quickpath.mem_scrubbing_uc", \
	    NULL, NULL, INTEL_MS_CLASS_FLAG_UC },

	{NULL, NULL, NULL, NULL},
};

/*ARGSUSED*/
int
nhmex_init(cmi_hdl_t hdl, void **datap)
{
	uint64_t cap;

	if (nhmex_ms_support_disable)
		return (ENOTSUP);

	if (!is_x86_feature(x86_featureset, X86FSET_MCA))
		return (ENOTSUP);
	if (cmi_hdl_rdmsr(hdl, IA32_MSR_MCG_CAP, &cap)
	    == CMI_SUCCESS && IS_SW_RECOVERY_P(cap)) {
		mc_cap_recover = 1;
	}

	return (0);
}

/*ARGSUSED*/
uint32_t
nhmex_error_action(cmi_hdl_t hdl, int ismc, int bank,
    uint64_t status, uint64_t addr, uint64_t misc, void *mslogout)
{
	uint32_t ar = MCAX86_ARCODE(status);

	if (ismc && (status & MSR_MC_STATUS_PCC)) {
		/*
		 * the context is broken, force panic
		 */
		return (nhmex_error_action_return | CMS_ERRSCOPE_FORCE_FATAL);

	} else if (ismc && (status & MSR_MC_STATUS_UC)) {
		if (mc_cap_recover == 0) {
			/*
			 * reset the system if the CPU doesn't support recovery
			 */
			return (nhmex_error_action_return |
			    CMS_ERRSCOPE_FORCE_FATAL);
		} else {
			/*
			 * it is a UCR error
			 */
			if ((ar == 1) && ERRCODE_IS_OVERFLOW(status)) {
				return (nhmex_error_action_return |
				    CMS_ERRSCOPE_FORCE_FATAL);
			} else if (ar == 0) {
				/*
				 * it is an SRAO error, mark it as poisoned
				 * and asynchronous
				 */
				return (nhmex_error_action_return |
				    CMS_ERRSCOPE_POISONED | CMS_ERRSCOPE_ASYNC);
			} else {
				/*
				 * Let's try to recover the error here.
				 */
				return (nhmex_error_action_return);
			}
		}
	}
	return (nhmex_error_action_return);
}

static cms_cookie_t
nhmex_ucr_check(uint32_t mcacode)
{
	cms_cookie_t rt = (cms_cookie_t)NULL;

	if (mc_cap_recover) {
		if (ERRCODE_IS_MEMSCRUB(mcacode)) {
			rt = (cms_cookie_t)NHMEX_ERR_MEM_SCRUB;
		} else if (ERRCODE_IS_LLCEWB(mcacode)) {
			rt = (cms_cookie_t)NHMEX_ERR_BUS_LLC_EWB;
		}
	}
	return (rt);
}

static boolean_t
nhmex_cookie_lookup(int index, uint16_t flag, uint32_t err_box)
{
	if ((nhmex_mscookie_class[index].cookie & err_box) &&
	    (flag == nhmex_mscookie_class[index].flag)) {
		return (B_TRUE);
	} else {
		return (B_FALSE);
	}
}

cms_cookie_t
nhmex_error_match(int bank, uint64_t status, uint64_t misc, uint16_t mscode)
{
	int i;
	uint16_t flag = 0;
	cms_cookie_t rt = (cms_cookie_t)NULL;
	uint16_t local_ms;
	uint32_t unit;

	if (status & MSR_MC_STATUS_UC)
		flag |= INTEL_MS_CLASS_FLAG_UC;

	local_ms = mscode;
	switch (bank) {
	case NHMEX_BANK_RBOX0:
	case NHMEX_BANK_RBOX1:
		if (misc & MSR_MC_MISC_RBOX_EXTERNAL_MASK)
			flag |= NHMEX_MS_CLASS_FLAG_EXTERNAL;
		unit = NHMEX_ERROR_R_BOX;
		break;

	case NHMEX_BANK_MBOX0:
	case NHMEX_BANK_MBOX1:
		unit = NHMEX_ERROR_M_BOX;
		break;

	case NHMEX_BANK_SBOX0:
	case NHMEX_BANK_SBOX1:
		unit = NHMEX_ERROR_S_BOX;
		/*
		 * poison bit is not included during Sbox error matching
		 */
		local_ms = local_ms & (~MSCOD_S_POISON);
		if ((local_ms == 0) && (status & MSR_MC_STATUS_UC)) {
			rt = (cms_cookie_t)NHMEX_ERR_BUS_CACHE_UC;
			return (rt);
		}
		break;

	case NHMEX_BANK_BBOX0:
	case NHMEX_BANK_BBOX1:
		unit = NHMEX_ERROR_B_BOX;
		if ((local_ms == 0) && (status & MSR_MC_STATUS_UC)) {
			rt = (cms_cookie_t)NHMEX_ERR_BUS_HOME_UC;
			return (rt);
		}
		break;

	case NHMEX_BANK_UBOX:
		unit = NHMEX_ERROR_U_BOX;
		break;

	default:
		return (NULL);
	}

	for (i = 0; nhmex_mscookie_class[i].cookie != NULL; i++) {
		if ((local_ms != NULL) &&
		    ((local_ms == nhmex_mscookie_class[i].mscode) ||
		    ((local_ms & nhmex_mscookie_class[i].msmask) &&
		    !(local_ms & ~nhmex_mscookie_class[i].msmask)))) {
			if (nhmex_cookie_lookup(i, flag, unit)) {
				rt = (cms_cookie_t)
				    nhmex_mscookie_class[i].cookie;
				break;
			}
		}
	}
	return (rt);
}

/*ARGSUSED*/
cms_cookie_t
nhmex_disp_match(cmi_hdl_t hdl, int ismc, int bank, uint64_t status,
    uint64_t addr, uint64_t misc, void *mslogout)
{
	cms_cookie_t rt = (cms_cookie_t)NULL;
	uint16_t mcacode = MCAX86_ERRCODE(status);
	uint16_t mscode = MCAX86_MSERRCODE(status);

	rt = nhmex_ucr_check(mcacode);
	if (rt != (cms_cookie_t)NULL)
		return (rt);
	rt = nhmex_error_match(bank, status, misc, mscode);
	return (rt);
}

/*ARGSUSED*/
void
nhmex_ereport_class(cmi_hdl_t hdl, cms_cookie_t mscookie,
    const char **cpuclsp, const char **leafclsp)
{
	int i;
	*cpuclsp = FM_EREPORT_CPU_INTEL;

	for (i = 0; nhmex_mscookie_class[i].cookie != NULL; i++) {
		if ((uintptr_t)mscookie == nhmex_mscookie_class[i].cookie)
			break;
	}

	if (nhmex_mscookie_class[i].cookie == NULL)
		*leafclsp = NULL;
	else
		*leafclsp = nhmex_mscookie_class[i].leafclass;
}

static nvlist_t *
nhmex_gentopo_ereport_detector(cmi_hdl_t hdl, int bankno, cms_cookie_t mscookie,
    nv_alloc_t *nva)
{
	nvlist_t *nvl = (nvlist_t *)NULL;
	nvlist_t *board_list = (nvlist_t *)NULL;
	int value;

	if (mscookie) {
		board_list = cmi_hdl_smb_bboard(hdl);

		if (board_list == NULL)
			return (NULL);

		if ((nvl = fm_nvlist_create(nva)) == NULL)
			return (NULL);

		if ((bankno == 8) || (bankno == 9)) {
			value = bankno % 8;
			fm_fmri_hc_create(nvl, FM_HC_SCHEME_VERSION,
			    NULL, NULL, board_list, 2,
			    "chip", cmi_hdl_smb_chipid(hdl),
			    "memory-controller", value);
		} else {
			fm_fmri_hc_create(nvl, FM_HC_SCHEME_VERSION,
			    NULL, NULL, board_list, 1,
			    "chip", cmi_hdl_smb_chipid(hdl));
		}
	}
	return (nvl);
}

nvlist_t *
nhmex_ereport_detector(cmi_hdl_t hdl, int bankno, cms_cookie_t mscookie,
    nv_alloc_t *nva)
{
	nvlist_t *nvl = (nvlist_t *)NULL;
	int value;

	if (!x86gentopo_legacy) {
		nvl = nhmex_gentopo_ereport_detector(hdl, bankno, mscookie,
		    nva);
		return (nvl);
	}

	if (mscookie) {
		if ((nvl = fm_nvlist_create(nva)) == NULL)
			return (NULL);
		if ((bankno == 8) || (bankno == 9)) {
			value = bankno % 8;
			fm_fmri_hc_set(nvl, FM_HC_SCHEME_VERSION,
			    NULL, NULL, 3,
			    "motherboard", 0,
			    "chip", cmi_hdl_chipid(hdl),
			    "memory-controller", value);
		} else {
			fm_fmri_hc_set(nvl, FM_HC_SCHEME_VERSION,
			    NULL, NULL, 2,
			    "motherboard", 0,
			    "chip", cmi_hdl_chipid(hdl));
		}
	}
	return (nvl);
}

static nvlist_t *
nhmex_gentopo_ereport_create_resource_elem(cmi_hdl_t hdl, nv_alloc_t *nva,
    mc_unum_t *unump)
{
	nvlist_t *nvl, *snvl;
	nvlist_t *board_list = NULL;

	board_list = cmi_hdl_smb_bboard(hdl);
	if (board_list == NULL) {
		return (NULL);
	}

	if ((nvl = fm_nvlist_create(nva)) == NULL)	/* freed by caller */
		return (NULL);

	if ((snvl = fm_nvlist_create(nva)) == NULL) {
		fm_nvlist_destroy(nvl, nva ? FM_NVA_RETAIN : FM_NVA_FREE);
		return (NULL);
	}

	(void) nvlist_add_uint64(snvl, FM_FMRI_HC_SPECIFIC_OFFSET,
	    unump->unum_offset);

	if (unump->unum_chan == -1) {
		fm_fmri_hc_create(nvl, FM_HC_SCHEME_VERSION, NULL, snvl,
		    board_list, 2,
		    "chip", cmi_hdl_smb_chipid(hdl),
		    "memory-controller", unump->unum_mc);
	} else if (unump->unum_cs == -1) {
		fm_fmri_hc_create(nvl, FM_HC_SCHEME_VERSION, NULL, snvl,
		    board_list, 3,
		    "chip", cmi_hdl_smb_chipid(hdl),
		    "memory-controller", unump->unum_mc,
		    "dram-channel", unump->unum_chan);
	} else if (unump->unum_rank == -1) {
		fm_fmri_hc_create(nvl, FM_HC_SCHEME_VERSION, NULL, snvl,
		    board_list, 4,
		    "chip", cmi_hdl_smb_chipid(hdl),
		    "memory-controller", unump->unum_mc,
		    "dram-channel", unump->unum_chan,
		    "dimm", unump->unum_cs);
	} else {
		fm_fmri_hc_create(nvl, FM_HC_SCHEME_VERSION, NULL, snvl,
		    board_list, 5,
		    "chip", cmi_hdl_smb_chipid(hdl),
		    "memory-controller", unump->unum_mc,
		    "dram-channel", unump->unum_chan,
		    "dimm", unump->unum_cs,
		    "rank", unump->unum_rank);
	}

	fm_nvlist_destroy(snvl, nva ? FM_NVA_RETAIN : FM_NVA_FREE);

	return (nvl);
}

static nvlist_t *
nhmex_ereport_create_resource_elem(nv_alloc_t *nva, mc_unum_t *unump)
{
	nvlist_t *nvl, *snvl;

	if ((nvl = fm_nvlist_create(nva)) == NULL)	/* freed by caller */
		return (NULL);

	if ((snvl = fm_nvlist_create(nva)) == NULL) {
		fm_nvlist_destroy(nvl, nva ? FM_NVA_RETAIN : FM_NVA_FREE);
		return (NULL);
	}

	(void) nvlist_add_uint64(snvl, FM_FMRI_HC_SPECIFIC_OFFSET,
	    unump->unum_offset);

	if (unump->unum_chan == -1) {
		fm_fmri_hc_set(nvl, FM_HC_SCHEME_VERSION, NULL, snvl, 3,
		    "motherboard", unump->unum_board,
		    "chip", unump->unum_chip,
		    "memory-controller", unump->unum_mc);
	} else if (unump->unum_cs == -1) {
		fm_fmri_hc_set(nvl, FM_HC_SCHEME_VERSION, NULL, snvl, 4,
		    "motherboard", unump->unum_board,
		    "chip", unump->unum_chip,
		    "memory-controller", unump->unum_mc,
		    "dram-channel", unump->unum_chan);
	} else if (unump->unum_rank == -1) {
		fm_fmri_hc_set(nvl, FM_HC_SCHEME_VERSION, NULL, snvl, 5,
		    "motherboard", unump->unum_board,
		    "chip", unump->unum_chip,
		    "memory-controller", unump->unum_mc,
		    "dram-channel", unump->unum_chan,
		    "dimm", unump->unum_cs);
	} else {
		fm_fmri_hc_set(nvl, FM_HC_SCHEME_VERSION, NULL, snvl, 6,
		    "motherboard", unump->unum_board,
		    "chip", unump->unum_chip,
		    "memory-controller", unump->unum_mc,
		    "dram-channel", unump->unum_chan,
		    "dimm", unump->unum_cs,
		    "rank", unump->unum_rank);
	}

	fm_nvlist_destroy(snvl, nva ? FM_NVA_RETAIN : FM_NVA_FREE);

	return (nvl);
}

/*ARGSUSED*/
void
nhmex_ereport_add_logout(cmi_hdl_t hdl, nvlist_t *ereport,
    nv_alloc_t *nva, int banknum, uint64_t status, uint64_t addr,
    uint64_t misc, void *mslogout, cms_cookie_t mscookie)
{
	mc_unum_t unum;
	nvlist_t *resource;
	uint32_t synd = 0, value;
	int  chan = MCAX86_ERRCODE_CCCC(status);
	boolean_t poison, s, ar;
	int chipid;

	if (chan == 0xf)
		chan = -1;
	if ((uintptr_t)mscookie & NHMEX_ERROR_R_BOX) {
		poison = (boolean_t)((status >> 31) & 0x1);
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_POISON,
		    DATA_TYPE_BOOLEAN_VALUE, poison, 0);

		if (banknum == 0)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_LQPI_PORT_NUM,
			    DATA_TYPE_UINT8, misc, 0);
		else if (banknum == 1)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_RQPI_PORT_NUM,
			    DATA_TYPE_UINT8, misc, 0);

	} else if (((uintptr_t)mscookie &  NHMEX_ERROR_M_BOX) ||
	    ((uintptr_t)mscookie &  NHMEX_ERROR_C_BOX)) {
		unum.unum_board = 0;
		unum.unum_chip = cmi_hdl_chipid(hdl);
		if (banknum == 8)
			unum.unum_mc = 0;
		else if (banknum == 9)
			unum.unum_mc = 1;
		unum.unum_chan = chan;
		unum.unum_cs = -1;
		unum.unum_rank = -1;
		unum.unum_offset = -1ULL;
		chipid = unum.unum_chip;
		if (status & MSR_MC_STATUS_ADDRV) {
			fm_payload_set(ereport, FM_FMRI_MEM_PHYSADDR,
			    DATA_TYPE_UINT64, addr, NULL);
			(void) cmi_mc_patounum(addr, 0, 0,
			    synd, 0, &unum);
		}

		if (!x86gentopo_legacy)
			resource = nhmex_gentopo_ereport_create_resource_elem(
			    hdl, nva, &unum);
		else
			resource = nhmex_ereport_create_resource_elem(nva,
			    &unum);

		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_NAME_RESOURCE,
		    DATA_TYPE_NVLIST_ARRAY, 1, &resource, NULL);
		fm_nvlist_destroy(resource, nva ?
		    FM_NVA_RETAIN:FM_NVA_FREE);

		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_NHMEX_OFFSET,
		    DATA_TYPE_UINT64, unum.unum_offset, 0);

		value = TCODE_OFFSET_BANK(unum.unum_offset);
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_NHMEX_MBANK,
		    DATA_TYPE_UINT32, value, 0);

		value = unum.unum_rank;
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_NAME_RANK,
		    DATA_TYPE_UINT32, value, 0);

		value = TCODE_OFFSET_CAS(unum.unum_offset);
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_NAME_CAS,
		    DATA_TYPE_UINT32, value, 0);

		value = TCODE_OFFSET_RAS(unum.unum_offset);
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_NAME_RAS,
		    DATA_TYPE_UINT32, value, 0);
		if (banknum == NHMEX_BANK_MBOX0) {
			value = NHMEX_MEM_ERR_LOG(chipid, 0, 0);
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_NHMEX_M0ERR_RNK0,
			    DATA_TYPE_UINT32, value, 0);
			value = NHMEX_MEM_ERR_LOG(chipid, 0, 1);
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_NHMEX_M0ERR_RNK1,
			    DATA_TYPE_UINT32, value, 0);
			value = NHMEX_MEM_ERR_LOG(chipid, 0, 2);
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_NHMEX_M0ERR_RNK2,
			    DATA_TYPE_UINT32, value, 0);
			value = NHMEX_MEM_ERR_LOG(chipid, 0, 3);
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_NHMEX_M0ERR_RNK3,
			    DATA_TYPE_UINT32, value, 0);
		} else if (banknum == NHMEX_BANK_MBOX1) {
			value = NHMEX_MEM_ERR_LOG(chipid, 1, 0);
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_NHMEX_M1ERR_RNK0,
			    DATA_TYPE_UINT32, value, 0);
			value = NHMEX_MEM_ERR_LOG(chipid, 1, 1);
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_NHMEX_M1ERR_RNK1,
			    DATA_TYPE_UINT32, value, 0);
			value = NHMEX_MEM_ERR_LOG(chipid, 1, 2);
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_NHMEX_M1ERR_RNK2,
			    DATA_TYPE_UINT32, value, 0);
			value = NHMEX_MEM_ERR_LOG(chipid, 1, 3);
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_NHMEX_M1ERR_RNK3,
			    DATA_TYPE_UINT32, value, 0);
		}
		if ((uintptr_t)mscookie == NHMEX_ERR_BUS_LLC_EWB ||
		    (uintptr_t)mscookie == NHMEX_ERR_MEM_UE ||
		    (uintptr_t)mscookie == NHMEX_ERR_MEM_SCRUB ||
		    (uintptr_t)mscookie == NHMEX_ERR_MEM_ECC_UC) {
			/*
			 * We retire page here to try to stop
			 * consumption of poison data in the case of llc_ewb
			 * the page will not be faulted as it is not memory
			 * that is bad
			 */
			(void) page_retire(addr, PR_UE);
			if (disable_fastreboot == 0) {
				fastreboot_disable(FBNS_FMAHWERR);
				disable_fastreboot = 1;
			}
		}
	}
	if (((uintptr_t)mscookie &  NHMEX_ERROR_M_BOX) ||
	    ((uintptr_t)mscookie &  NHMEX_ERROR_C_BOX) ||
	    ((uintptr_t)mscookie &  NHMEX_ERROR_S_BOX) ||
	    ((uintptr_t)mscookie &  NHMEX_ERROR_B_BOX) ||
	    ((uintptr_t)mscookie &  NHMEX_ERROR_U_BOX)) {
		poison = (boolean_t)((status >> 31) & 0x1);
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_POISON,
		    DATA_TYPE_BOOLEAN_VALUE, poison, 0);
	}
	if (mc_cap_recover) {
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_REC_CAP,
		    DATA_TYPE_BOOLEAN_VALUE, 1, 0);

		s = (boolean_t)MCAX86_SCODE(status);
		ar = (boolean_t)MCAX86_ARCODE(status);
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_SIGNAL_MCE,
		    DATA_TYPE_BOOLEAN_VALUE, s, 0);
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ATT_REC,
		    DATA_TYPE_BOOLEAN_VALUE, ar, 0);
	}
}

/*ARGSUSED*/
boolean_t
nhmex_bankctl_skipinit(cmi_hdl_t hdl, int banknum)
{
	return (0);
}

cms_api_ver_t _cms_api_version = CMS_API_VERSION_2;

const cms_ops_t _cms_ops = {
	nhmex_init,		/* cms_init */
	NULL,			/* cms_post_startup */
	NULL,			/* cms_post_mpstartup */
	NULL,			/* cms_logout_size */
	NULL,			/* cms_mcgctl_val */
	nhmex_bankctl_skipinit, /* cms_bankctl_skipinit */
	NULL,			/* cms_bankctl_val */
	NULL,			/* cms_bankstatus_skipinit */
	NULL,			/* cms_bankstatus_val */
	NULL,			/* cms_mca_init */
	NULL,			/* cms_poll_ownermask */
	NULL,			/* cms_bank_logout */
	nhmex_error_action,	/* cms_error_action */
	nhmex_disp_match,	/* cms_disp_match */
	nhmex_ereport_class,	/* cms_ereport_class */
	nhmex_ereport_detector,	/* cms_ereport_detector */
	NULL,			/* cms_ereport_includestack */
	nhmex_ereport_add_logout,	/* cms_ereport_add_logout */
	NULL,			/* cms_msrinject */
	NULL,			/* cms_fini */
};

static struct modlcpu modlcpu = {
	&mod_cpuops,
	"Intel model-specific MCA"
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modlcpu,
	NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

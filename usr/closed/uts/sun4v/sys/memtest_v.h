/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MEMTEST_V_H
#define	_MEMTEST_V_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * CPU/Memory error injector sun4v driver header file.
 */

#include <sys/async.h>
#include <sys/error.h>
#include <sys/hsvc.h>
#include <sys/memtest.h>

/*
 * Required sun4v definitions not found in any kernel header file.
 */

/* Hypervisor version check (for DIAG funcs ra2pa and hpriv) definitions. */
#define	MEMTEST_HYP_VER_OK	0
#define	MEMTEST_HYP_VER_UNINIT	1
#define	MEMTEST_HYP_VER_BAD	2

/* Register file test assembly routine stride definitions. */
#define	IREG_STRIDE_SIZE	24
#define	FREG_STRIDE_SIZE	24
#define	IREG_MAX_OFFSET		4
#define	FREG_MAX_OFFSET		3
#define	REG_DEFAULT_OFFSET	2

/* sun4v SPARC Error Inject Register (internal errors) definitions */
#define	REG_INJECTION_ENABLE	0x80000000	/* bit 31 */
#define	REG_SSHOT_ENABLE	0x40000000	/* Niagara-I HW only */
#define	REG_IMD_ENABLE		0x20000000	/* iTLB data parity */
#define	REG_IMT_ENABLE		0x10000000	/* iTLB tag parity */
#define	REG_DMD_ENABLE		0x8000000	/* dTLB data parity */
#define	REG_DMT_ENABLE		0x4000000	/* dTLB tag parity */

#define	REG_IRC_ENABLE		0x2000000	/* Int-reg ecc */
#define	REG_FRC_ENABLE		0x1000000	/* FP reg ecc */

#define	REG_SCA_ENABLE		0x800000	/* Scratchpad Array ecc */
#define	REG_TCA_ENABLE		0x400000	/* Tick Compare Array ecc */
#define	REG_TSA_ENABLE		0x200000	/* Trap Stack Array ecc */
#define	REG_MRA_ENABLE		0x100000	/* MMU Reg Array parity */
#define	REG_STA_ENABLE		0x80000		/* Store Buffer CAM parity */
#define	REG_RESERVED_1		0x40000		/* Reserved */
#define	REG_STD_ENABLE		0x20000		/* Store Buffer data parity */
#define	REG_ECC_MASK		0xff		/* Mask for ECC bits */

#define	REG_ERR_STRIDE		24

/* Common MA definitions */
#define	MA_OP_LOAD		0	/* from memory/L2 to MA */
#define	MA_OP_STORE		1	/* from MA to memory/L2 */
#define	MA_OP_MULT		2
#define	MA_OP_REDUCT		3
#define	MA_OP_EXP		4

/*
 * Macros for calling the sun4v operation vector (opsvec) routines.
 *
 * The common opsvec table is defined in memtest.h.
 */
#define	OP_ACCESS_CWQ(mdatap, acc_type, intr_flag) \
		((mdatap)->m_sopvp->op_access_cwq) \
		((mdatap), (acc_type), (intr_flag))

#define	OP_ACCESS_MA(mdatap, acc_type, intr_flag) \
		((mdatap)->m_sopvp->op_access_ma) \
		((mdatap), (acc_type), (intr_flag))

#define	OP_CHECK_L2_IDX_MODE(mdatap, paddr, idx_paddr) \
		((mdatap)->m_sopvp->op_check_l2_idx_mode)((paddr), (idx_paddr))

#define	OP_FLUSHALL_L2_HVMODE(mdatap) \
		((mdatap)->m_sopvp->op_flushall_l2_hvmode)((mdatap)->m_cip)

#define	OP_FLUSH_L2_ENTRY_HVMODE(mdatap, addr) \
		((mdatap)->m_sopvp->op_flush_l2_entry_hvmode) \
		(((mdatap)->m_cip), (addr))

#define	OP_INJECT_HVDCACHE(mdatap) \
		((mdatap)->m_sopvp->op_inject_hvdcache)((mdatap))

#define	OP_INJECT_HVICACHE(mdatap) \
		((mdatap)->m_sopvp->op_inject_hvicache)((mdatap))

#define	OP_INJECT_L2DIR(mdatap) \
		((mdatap)->m_sopvp->op_inject_l2dir)((mdatap))

#define	OP_INJECT_L2ND(mdatap) \
		((mdatap)->m_sopvp->op_inject_l2nd)((mdatap))

#define	OP_INJECT_L2VAD(mdatap) \
		((mdatap)->m_sopvp->op_inject_l2vad)((mdatap))

#define	OP_INJECT_MA(mdatap) \
		((mdatap)->m_sopvp->op_inject_ma_memory)((mdatap))

#define	OP_INJECT_MEMND(mdatap) \
		((mdatap)->m_sopvp->op_inject_memnd)((mdatap))

#define	OP_INJECT_VMEMORY(mdatap) \
		((mdatap)->m_sopvp->op_inject_memory)((mdatap))

/*
 * Test routines located in memtest_v.c.
 */
extern	int		memtest_h_dc_err(mdata_t *);
extern	int		memtest_h_ic_err(mdata_t *);
extern	int		memtest_h_l2_err(mdata_t *);
extern	int		memtest_h_l2buf_err(mdata_t *);
extern	int		memtest_h_l2vad_err(mdata_t *);
extern	int		memtest_h_l2wb_err(mdata_t *);
extern	int		memtest_h_ma_err(mdata_t *);
extern	int		memtest_h_mem_err(mdata_t *);
extern	int		memtest_h_reg_err(mdata_t *);
extern	int		memtest_h_spfail(mdata_t *);

extern	int		memtest_k_l2dir_err(mdata_t *);
extern	int		memtest_k_l2nd_err(mdata_t *);
extern	int		memtest_k_l2vad_err(mdata_t *);
extern	int		memtest_k_nd_err(mdata_t *);
extern	int		memtest_k_reg_err(mdata_t *);

extern	int		memtest_l2dir_phys(mdata_t *);
extern	int		memtest_l2vad_phys(mdata_t *);

extern	int		memtest_u_l2dir_err(mdata_t *);
extern	int		memtest_u_l2vad_err(mdata_t *);
extern	int		memtest_u_nd_err(mdata_t *);
extern	int		memtest_u_tlb_err(mdata_t *);

/*
 * Support routines located in memtest_v.c.
 *
 * NOTE: most support protos are listed in the common file memtest.h because
 *	 they are routines that have equivalent sun4u and sun4v versions.
 */
extern	int		memtest_asi_peekpoke(mdata_t *);
extern	int		memtest_check_misc(uint64_t);
extern	uint64_t	memtest_get_dram_bank_offset(mdata_t *);
extern	uint64_t	memtest_hv_inject_error(char *, void *, uint64_t,
				uint64_t, uint64_t, uint64_t);
extern	int		memtest_hv_mpeekpoke(mdata_t *);
extern	uint64_t	memtest_hv_util(char *, void *, uint64_t, uint64_t,
				uint64_t, uint64_t);
extern	uint64_t	memtest_kva_to_ra(void *);

/*
 * Routines located in memtest_v_asm.s.
 */
extern	int		hv_asi_load8(int, uint64_t, uint64_t *);
extern	int		hv_asi_store8(int, uint64_t, uint8_t, uint64_t *);
extern	int		hv_asi_load16(int, uint64_t, uint64_t *);
extern	int		hv_asi_store16(int, uint64_t, uint16_t, uint64_t *);
extern	int		hv_asi_load32(int, uint64_t, uint64_t *);
extern	int		hv_asi_store32(int, uint64_t, uint32_t, uint64_t *);
extern	int		hv_asi_load64(int, uint64_t, uint64_t *);
extern	int		hv_asi_store64(int, uint64_t, uint64_t, uint64_t *);

extern	int		hv_paddr_load8(uint64_t, uint64_t *);
extern	int		hv_paddr_store8(uint64_t, uint8_t, uint64_t *);
extern	int		hv_paddr_load16(uint64_t, uint64_t *);
extern	int		hv_paddr_store16(uint64_t, uint16_t, uint64_t *);
extern	int		hv_paddr_load32(uint64_t, uint64_t *);
extern	int		hv_paddr_store32(uint64_t, uint32_t, uint64_t *);
extern	int		hv_paddr_load64(uint64_t, uint64_t *);
extern	int		hv_paddr_store64(uint64_t, uint64_t, uint64_t *);

extern	int		hv_queue_resumable_epkt(uint64_t, uint64_t);

extern	uint64_t	memtest_get_cpu_ver_asm(void);
extern	void		memtest_hv_asm_access(void);
extern	uint64_t	memtest_hv_trap_check_asm(uint64_t, uint64_t, uint64_t,
				uint64_t);
extern	uint64_t	memtest_ra_to_pa(uint64_t);
extern	uint64_t	memtest_run_hpriv(uint64_t, uint64_t, uint64_t,
				uint64_t, uint64_t);

/*
 * Sun4v generic error types (commands supported by most sun4v cpus).
 */
static cmd_t sun4v_generic_cmds[] = {
	/* Memory (DRAM) errors injected by address. */
	G4V_UMVIRT,	memtest_u_mvirt,	"memtest_u_mvirt",
	G4V_KMVIRT,	memtest_k_mvirt,	"memtest_k_mvirt",
	G4V_MREAL,	memtest_mphys,		"memtest_mphys",
	G4V_MPHYS,	memtest_mphys,		"memtest_mphys",

	G4V_KVPEEK,	memtest_k_vpeekpoke,	"memtest_k_vpeekpoke",
	G4V_KVPOKE,	memtest_k_vpeekpoke,	"memtest_k_vpeekpoke",
	G4V_KMPEEK,	memtest_k_mpeekpoke,	"memtest_k_mpeekpoke",
	G4V_KMPOKE,	memtest_k_mpeekpoke,	"memtest_k_mpeekpoke",
	G4V_HVPEEK,	memtest_hv_mpeekpoke,	"memtest_hv_mpeekpoke",
	G4V_HVPOKE,	memtest_hv_mpeekpoke,	"memtest_hv_mpeekpoke",
	G4V_ASIPEEK,	memtest_asi_peekpoke,	"memtest_asi_peekpoke",
	G4V_ASIPOKE,	memtest_asi_peekpoke,	"memtest_asi_peekpoke",

	G4V_SPFAIL,	memtest_h_spfail,	"memtest_h_spfail",

	NULL,		NULL,			NULL,
};

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_V_H */

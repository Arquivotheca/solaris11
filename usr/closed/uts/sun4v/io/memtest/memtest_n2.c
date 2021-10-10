/*
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains Niagara-II (UltraSPARC-T2) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_v.h>
#include <sys/memtestio_ni.h>
#include <sys/memtestio_n2.h>
#include <sys/memtest_v.h>
#include <sys/memtest_ni.h>
#include <sys/memtest_n2.h>
#include <sys/memtest_v_asm.h>
#include <sys/memtest_n2_asm.h>
#include <sys/memtest_vf_asm.h>
#include <sys/memtest_kt_asm.h>

/*
 * Debug buffer passed to some assembly routines.  Must be the PA for
 * routines which run in hyperprivileged mode.
 */
uint64_t	n2_debug_buf_va[DEBUG_BUF_SIZE];
uint64_t	n2_debug_buf_pa;

/*
 * Enable/disable the storing of DEBUG values into the debug mem buffer
 * for different test classes.
 */
/* #define	MEM_DEBUG_BUFFER	1 */
/* #define	L1_DEBUG_BUFFER		1 */
/* #define	L2_DEBUG_BUFFER		1 */

/*
 * In order to share code externs from KT are required in this file.
 */
extern	int	kt_inj_ireg_file(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	kt_inj_ireg_hvfile_global(uint64_t, uint64_t, uint64_t,
			uint64_t);
extern	int	kt_inj_ireg_hvfile_in(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	kt_inj_ireg_hvfile_local(uint64_t, uint64_t, uint64_t,
			uint64_t);
extern	int	kt_inj_ireg_hvfile_out(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	kt_inj_sb_io(uint64_t, uint64_t, uint64_t, uint64_t);

/*
 * Niagara-II operations vector tables.
 */
static opsvec_v_t niagara2_vops = {
	/* sun4v injection ops vectors */
	n2_inject_hvdcache,	/* corrupt d$ data or tag in hv mode */
	n2_inject_hvicache,	/* corrupt i$ data or tag in hv mode */
	n2_inject_l2dir,	/* corrupt l2$ directory at raddr */
	n2_inject_l2nd,		/* install l2$ NotData at raddr */
	n2_inject_l2vad,	/* corrupt l2$ V(U)AD bits at raddr or offset */
	n2_inject_mamem,	/* corrupt MA memory parity */
	notsup,			/* no install memory NotData */
	n2_inject_memory,	/* corrupt local memory */

	/* sun4v support ops vectors */
	n2_access_cwq,		/* access via CWQ op */
	n2_access_mamem,	/* access MA memory */
	n2_check_l2_idx_mode,	/* check/convert addresses for index hashing */
	n2_flushall_l2_hvmode,	/* flush all l2$ (inclusive) in hv mode */
	n2_flush_l2_entry_hvmode, /* flush single l2$ entry in hv mode */
};

static opsvec_c_t niagara2_cops = {
	/* common injection ops vectors */
	n2_inject_dcache,	/* corrupt d$ data or tag at raddr */
	n2_inject_dphys,	/* corrupt d$ data or tag at offset */
	n2_inject_freg_file,	/* corrupt FP register file */
	n2_inject_icache,	/* corrupt i$ data or tag at raddr */
	notsup,			/* no corrupt internal */
	n2_inject_iphys,	/* corrupt i$ data or tag at offset */
	n2_inject_ireg_file,	/* corrupt integer register file */
	n2_inject_l2cache,	/* corrupt l2$ data or tag at raddr */
	n2_inject_l2phys,	/* corrupt l2$ data or tag at offset */
	notsup,			/* no corrupt l3$ data or tag at raddr */
	notsup,			/* no corrupt l3$ data or tag at offset */
	n2_inject_tlb,		/* I-D TLB parity errors */

	/* common support ops vectors */
	ni_access_freg_file,	/* access FP register file (using N1) */
	ni_access_ireg_file,	/* access integer register file (using N1) */
	notimp,			/* check ESRs */
	n2_enable_errors,	/* enable AFT errors */
	n2_control_scrub,	/* enable/disable L2 or memory scrubbers */
	n2_get_cpu_info,	/* put cpu info into struct */
	n2_flushall_caches,	/* flush all caches in hv mode */
	n2_clearall_dcache,	/* clear (not just flush) all d$ in hv mode */
	n2_clearall_icache,	/* clear (not just flush) all i$ in hv mode */
	n2_flushall_l2_kmode,	/* flush all l2$ (inclusive) in kern mode */
	notsup,			/* no flush all l3$ */
	notsup,			/* no flush single d$ entry */
	notsup,			/* no flush single i$ entry */
	n2_flush_l2_entry_kmode, /* flush single l2$ entry in kern mode */
	notsup,			/* no flush single l3$ entry */
};

/*
 * These Niagara-II error commands are grouped according to the definitions
 * in the memtestio_ni.h and memtestio_n2.h header files.
 *
 * This is a complete list of the commands available on Niagara-II, so
 * no reference needs to be made to the matching Niagara-I command table.
 */
cmd_t niagara2_cmds[] = {
	/* Memory (DRAM) uncorrectable errors. */
	NI_HD_DAU,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_HI_DAU,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_KD_DAU,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_HD_DAUMA,	memtest_h_mem_err,	"memtest_h_mem_err",
	N2_HD_DAUCWQ,	memtest_h_mem_err,	"memtest_h_mem_err",
	N2_KD_DAUDTLB,	n2_inject_tlb_mem_miss,	"n2_inject_tlb_mem_miss",
	N2_KI_DAUITLB,	n2_inject_tlb_mem_miss,	"n2_inject_tlb_mem_miss",
	NI_KD_DAUTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KD_DAUPR,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KI_DAU,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KI_DAUTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_UD_DAU,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_UI_DAU,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_KD_DSU,	n2_inject_memory_scrub,	"n2_inject_memory_scrub",
	NI_KD_DBU,	ni_inject_memory_range,	"ni_inject_memory_range",
	NI_IO_DRU,	memtest_u_mem_err,	"memtest_u_mem_err",

	/* Memory (DRAM) correctable errors. */
	NI_HD_DAC,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_HI_DAC,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_KD_DAC,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_HD_DACMA,	memtest_h_mem_err,	"memtest_h_mem_err",
	N2_HD_DACCWQ,	memtest_h_mem_err,	"memtest_h_mem_err",
	N2_KD_DACDTLB,	n2_inject_tlb_mem_miss,	"n2_inject_tlb_mem_miss",
	N2_KI_DACITLB,	n2_inject_tlb_mem_miss,	"n2_inject_tlb_mem_miss",
	NI_KD_DACTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KD_DACPR,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KD_DACSTORM,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KI_DAC,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KI_DACTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_UD_DAC,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_UI_DAC,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_KD_DSC,	n2_inject_memory_scrub,	"n2_inject_memory_scrub",
	NI_IO_DRC,	memtest_u_mem_err,	"memtest_u_mem_err",

	/* L2 cache data and tag uncorrectable errors. */
	NI_HD_LDAU,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_HI_LDAU,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_KD_LDAU,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_LDAUCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	NI_HD_LDAUMA,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HD_LDAUCWQ,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_KD_LDAUDTLB,	n2_inject_tlb_l2_miss,	"n2_inject_tlb_l2_miss",
	N2_KI_LDAUITLB,	n2_inject_tlb_l2_miss,	"n2_inject_tlb_l2_miss",
	NI_KD_LDAUTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KD_LDAUPR,	memtest_k_l2_err,	"memtest_k_l2_err",
	N2_HD_LDAUPRI,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_OBP_LDAU,	memtest_obp_err,	"memtest_obp_err",
	NI_KI_LDAU,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KI_LDAUTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_UD_LDAU,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_UI_LDAU,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_KD_LDSU,	n2_inject_l2_scrub,	"n2_inject_l2_scrub",
	NI_IO_LDRU,	memtest_u_l2_err,	"memtest_u_l2_err",

	/* L2 cache data and tag correctable errors. */
	NI_HD_LDAC,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_HI_LDAC,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_KD_LDAC,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_LDACCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	NI_HD_LDACMA,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HD_LDACCWQ,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_KD_LDACDTLB,	n2_inject_tlb_l2_miss,	"n2_inject_tlb_l2_miss",
	N2_KI_LDACITLB,	n2_inject_tlb_l2_miss,	"n2_inject_tlb_l2_miss",
	NI_KD_LDACTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KD_LDACPR,	memtest_k_l2_err,	"memtest_k_l2_err",
	N2_HD_LDACPRI,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_OBP_LDAC,	memtest_obp_err,	"memtest_obp_err",
	NI_KI_LDAC,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KI_LDACTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_UD_LDAC,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_UI_LDAC,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_KD_LDSC,	n2_inject_l2_scrub,	"n2_inject_l2_scrub",
	NI_IO_LDRC,	memtest_u_l2_err,	"memtest_u_l2_err",

	NI_HD_LDTC,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_HI_LDTC,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_KD_LDTC,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KD_LDTCTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KI_LDTC,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KI_LDTCTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_UD_LDTC,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_UI_LDTC,	memtest_u_l2_err,	"memtest_u_l2_err",

	/* L2 cache data and tag errors injected by address. */
	NI_L2PHYS,	memtest_l2phys,		"memtest_l2phys",
	NI_L2TPHYS,	memtest_l2phys,		"memtest_l2phys",
	NI_L2SCRUBPHYS,	n2_inject_l2_scrub,	"n2_inject_l2_scrub",
	NI_K_L2VIRT,	memtest_k_l2virt,	"memtest_k_l2virt",
	NI_U_L2VIRT,	memtest_k_l2virt,	"memtest_k_l2virt",

	/* L2 cache NotData errors. */
	N2_HD_L2ND,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HD_L2NDMA,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HD_L2NDCWQ,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HI_L2ND,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_KD_L2NDDTLB,	n2_inject_tlb_l2_miss,	"n2_inject_tlb_l2_miss",
	N2_KI_L2NDITLB,	n2_inject_tlb_l2_miss,	"n2_inject_tlb_l2_miss",
	N2_KD_L2ND,	memtest_k_l2nd_err,	"memtest_k_l2nd_err",
/*	N2_L2NDCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err", */
	N2_KD_L2NDTL1,	memtest_k_l2nd_err,	"memtest_k_l2nd_err",
	N2_KD_L2NDPR,	memtest_k_l2nd_err,	"memtest_k_l2nd_err",
	N2_HD_L2NDPRI,	memtest_h_l2_err,	"memtest_h_l2_err",
/*	N2_OBP_L2ND,	memtest_obp_err,	"memtest_obp_err", */
	N2_KI_L2ND,	memtest_k_l2nd_err,	"memtest_k_l2nd_err",
	N2_KI_L2NDTL1,	memtest_k_l2nd_err,	"memtest_k_l2nd_err",
	N2_UD_L2ND,	memtest_u_nd_err,	"memtest_u_nd_err",
	N2_UI_L2ND,	memtest_u_nd_err,	"memtest_u_nd_err",

	N2_HD_L2NDWB,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	N2_HI_L2NDWB,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	N2_IO_L2ND,	memtest_u_nd_err,	"memtest_u_nd_err",
/*	N2_L2NDPHYS,	memtest_l2phys,		"memtest_l2phys", */

	/* L2 cache write-back errors. */
	NI_HD_LDWU,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_HI_LDWU,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	N2_HD_LDWUPRI,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	N2_HI_LDWUPRI,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_KD_LDWU,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_KI_LDWU,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_UD_LDWU,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_UI_LDWU,	memtest_u_l2_err,	"memtest_u_l2_err",

	NI_HD_LDWC,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_HI_LDWC,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	N2_HD_LDWCPRI,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	N2_HI_LDWCPRI,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_KD_LDWC,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_KI_LDWC,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_UD_LDWC,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_UI_LDWC,	memtest_u_l2_err,	"memtest_u_l2_err",

	/* L2 cache V(U)AD uncorrectable (fatal) errors. */
	N2_HD_LVF_VD,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_HI_LVF_VD,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_KD_LVF_VD,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_KI_LVF_VD,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_UD_LVF_VD,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	N2_UI_LVF_VD,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	N2_HD_LVF_UA,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_HI_LVF_UA,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_KD_LVF_UA,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_KI_LVF_UA,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_UD_LVF_UA,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	N2_UI_LVF_UA,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	/* L2 cache V(U)AD correctable errors. */
	N2_HD_LVC_VD,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_HI_LVC_VD,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_KD_LVC_VD,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_KI_LVC_VD,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_UD_LVC_VD,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	N2_UI_LVC_VD,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	N2_HD_LVC_UA,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_HI_LVC_UA,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_KD_LVC_UA,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_KI_LVC_UA,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_UD_LVC_UA,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	N2_UI_LVC_UA,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	/* L2 cache V(U)AD errors injected by address. */
	NI_L2VDPHYS,	memtest_l2vad_phys,	"memtest_l2vad_phys",
	NI_L2UAPHYS,	memtest_l2vad_phys,	"memtest_l2vad_phys",

	/* L2 cache directory errors. */
	NI_KD_LRU,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",
	NI_KI_LRU,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",
	NI_UD_LRU,	memtest_u_l2dir_err,	"memtest_u_l2dir_err",
	NI_UI_LRU,	memtest_u_l2dir_err,	"memtest_u_l2dir_err",

	NI_L2DIRPHYS,	memtest_l2dir_phys,	"memtest_l2dir_phys",

	/* L1 data cache data and tag correctable errors. */
	NI_HD_DDC,	memtest_h_dc_err, 	"memtest_h_dc_err",
	NI_KD_DDC,	memtest_k_dc_err, 	"memtest_k_dc_err",
	NI_KD_DDCTL1,	memtest_k_dc_err, 	"memtest_k_dc_err",

	NI_HD_DTC,	memtest_h_dc_err, 	"memtest_h_dc_err",
	NI_KD_DTC,	memtest_k_dc_err, 	"memtest_k_dc_err",
	NI_KD_DTCTL1,	memtest_k_dc_err, 	"memtest_k_dc_err",

	N2_HD_DCVP,	memtest_h_dc_err, 	"memtest_h_dc_err",
	N2_KD_DCVP,	memtest_k_dc_err, 	"memtest_k_dc_err",
	N2_KD_DCVPTL1,	memtest_k_dc_err, 	"memtest_k_dc_err",

	N2_HD_DCTM,	memtest_h_dc_err, 	"memtest_h_dc_err",
	N2_KD_DCTM,	memtest_k_dc_err, 	"memtest_k_dc_err",
	N2_KD_DCTMTL1,	memtest_k_dc_err, 	"memtest_k_dc_err",

	NI_DPHYS,	memtest_dphys,		"memtest_dphys",
	NI_DTPHYS,	memtest_dphys,		"memtest_dphys",
	N2_DVPHYS,	memtest_dphys,		"memtest_dphys",
	N2_DMPHYS,	memtest_dphys,		"memtest_dphys",

	/* L1 instruction cache data and tag correctable errors. */
	NI_HI_IDC,	memtest_h_ic_err, 	"memtest_h_ic_err",
	NI_KI_IDC,	memtest_k_ic_err, 	"memtest_k_ic_err",
	NI_KI_IDCTL1,	memtest_k_ic_err, 	"memtest_k_ic_err",

	NI_HI_ITC,	memtest_h_ic_err, 	"memtest_h_ic_err",
	NI_KI_ITC,	memtest_k_ic_err, 	"memtest_k_ic_err",
	NI_KI_ITCTL1,	memtest_k_ic_err, 	"memtest_k_ic_err",

	N2_HI_ICVP,	memtest_h_ic_err, 	"memtest_h_ic_err",
	N2_KI_ICVP,	memtest_k_ic_err, 	"memtest_k_ic_err",
	N2_KI_ICVPTL1,	memtest_k_ic_err, 	"memtest_k_ic_err",

	N2_HI_ICTM,	memtest_h_ic_err, 	"memtest_h_ic_err",
	N2_KI_ICTM,	memtest_k_ic_err, 	"memtest_k_ic_err",
	N2_KI_ICTMTL1,	memtest_k_ic_err, 	"memtest_k_ic_err",

	NI_IPHYS,	memtest_iphys,		"memtest_iphys",
	NI_ITPHYS,	memtest_iphys,		"memtest_iphys",
	N2_IVPHYS,	memtest_iphys,		"memtest_iphys",
	N2_IMPHYS,	memtest_iphys,		"memtest_iphys",

	/* Instruction and data TLB data and tag (CAM) errors. */
	N2_KD_DTDP,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	N2_KD_DTDPV,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	N2_KD_DTTP,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	NI_UD_DMDU,	memtest_u_tlb_err,	"memtest_u_tlb_err",
	NI_UD_DMTU,	memtest_u_tlb_err,	"memtest_u_tlb_err",

	N2_KD_DTTM,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	N2_KD_DTMU,	memtest_k_tlb_err,	"memtest_k_tlb_err",

	NI_DMDURAND,	ni_inject_tlb_random,	"ni_inject_tlb_random",
	NI_DMTURAND,	ni_inject_tlb_random,	"ni_inject_tlb_random",

	N2_KI_ITDP,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	N2_KI_ITDPV,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	N2_KI_ITTP,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	NI_UI_IMDU,	memtest_u_tlb_err,	"memtest_u_tlb_err",
	NI_UI_IMTU,	memtest_u_tlb_err,	"memtest_u_tlb_err",

	N2_KI_ITTM,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	N2_KI_ITMU,	memtest_k_tlb_err,	"memtest_k_tlb_err",

	NI_IMDURAND,	ni_inject_tlb_random,	"ni_inject_tlb_random",
	NI_IMTURAND,	ni_inject_tlb_random,	"ni_inject_tlb_random",

	/* Integer register (SPARC Internal) errors. */
	NI_HD_IRUL,	memtest_h_reg_err,	"memtest_h_reg_err",
	NI_HD_IRUS,	memtest_h_reg_err,	"memtest_h_reg_err",
	NI_HD_IRUO,	memtest_h_reg_err,	"memtest_h_reg_err",

	NI_KD_IRUL,	memtest_k_reg_err,	"memtest_k_reg_err",
	NI_KD_IRUS,	memtest_k_reg_err,	"memtest_k_reg_err",
	NI_KD_IRUO,	memtest_k_reg_err,	"memtest_k_reg_err",

	NI_HD_IRCL,	memtest_h_reg_err,	"memtest_h_reg_err",
	NI_HD_IRCS,	memtest_h_reg_err,	"memtest_h_reg_err",
	NI_HD_IRCO,	memtest_h_reg_err,	"memtest_h_reg_err",

	NI_KD_IRCL,	memtest_k_reg_err,	"memtest_k_reg_err",
	NI_KD_IRCS,	memtest_k_reg_err,	"memtest_k_reg_err",
	NI_KD_IRCO,	memtest_k_reg_err,	"memtest_k_reg_err",

	/* Floating-point register file (SPARC Internal) errors. */
	NI_HD_FRUL,	memtest_h_reg_err,	"memtest_h_reg_err",
	NI_HD_FRUS,	memtest_h_reg_err,	"memtest_h_reg_err",
	NI_HD_FRUO,	memtest_h_reg_err,	"memtest_h_reg_err",

	NI_KD_FRUL,	memtest_k_reg_err,	"memtest_k_reg_err",
	NI_KD_FRUS,	memtest_k_reg_err,	"memtest_k_reg_err",
	NI_KD_FRUO,	memtest_k_reg_err,	"memtest_k_reg_err",

	NI_HD_FRCL,	memtest_h_reg_err,	"memtest_h_reg_err",
	NI_HD_FRCS,	memtest_h_reg_err,	"memtest_h_reg_err",
	NI_HD_FRCO,	memtest_h_reg_err,	"memtest_h_reg_err",

	NI_KD_FRCL,	memtest_k_reg_err,	"memtest_k_reg_err",
	NI_KD_FRCS,	memtest_k_reg_err,	"memtest_k_reg_err",
	NI_KD_FRCO,	memtest_k_reg_err,	"memtest_k_reg_err",

	/* Store Buffer (SPARC Internal) errors. */
	N2_HD_SBDLU,	n2_inject_sb,		"n2_inject_sb",
	N2_HD_SBDPU,	n2_inject_sb,		"n2_inject_sb",
	N2_HD_SBDPUASI,	n2_inject_sb,		"n2_inject_sb",
	N2_KD_SBDLU,	n2_inject_sb,		"n2_inject_sb",
	N2_KD_SBDPU,	n2_inject_sb,		"n2_inject_sb",

	N2_HD_SBAPP,	n2_inject_sb,		"n2_inject_sb",
	N2_HD_SBAPPASI,	n2_inject_sb,		"n2_inject_sb",
	N2_KD_SBAPP,	n2_inject_sb,		"n2_inject_sb",
	N2_KD_SBAPPASI,	n2_inject_sb,		"n2_inject_sb",

	N2_IO_SBDIOU,	n2_inject_sb,		"n2_inject_sb",
	N2_IO_SBDIOUASI, n2_inject_sb,		"n2_inject_sb",

	N2_HD_SBDLC,	n2_inject_sb,		"n2_inject_sb",
	N2_HD_SBDPC,	n2_inject_sb,		"n2_inject_sb",
	N2_HD_SBDPCASI,	n2_inject_sb,		"n2_inject_sb",
	N2_KD_SBDLC,	n2_inject_sb,		"n2_inject_sb",
	N2_KD_SBDPC,	n2_inject_sb,		"n2_inject_sb",

	N2_IO_SBDPC,	n2_inject_sb,		"n2_inject_sb",
	N2_IO_SBDPCASI,	n2_inject_sb,		"n2_inject_sb",

	/* Internal register array (SPARC Internal) errors. */
	N2_HD_SCAU,	n2_inject_int_array,	"n2_inject_int_array",
	N2_HD_SCAC,	n2_inject_int_array,	"n2_inject_int_array",
	N2_KD_SCAU,	n2_inject_int_array,	"n2_inject_int_array",
	N2_KD_SCAC,	n2_inject_int_array,	"n2_inject_int_array",

	N2_HD_TCUP,	n2_inject_int_array,	"n2_inject_int_array",
	N2_HD_TCCP,	n2_inject_int_array,	"n2_inject_int_array",
	N2_HD_TCUD,	n2_inject_int_array,	"n2_inject_int_array",
	N2_HD_TCCD,	n2_inject_int_array,	"n2_inject_int_array",

	N2_HD_TSAU,	n2_inject_int_array,	"n2_inject_int_array",
	N2_HD_TSAC,	n2_inject_int_array,	"n2_inject_int_array",

	N2_HD_MRAU,	n2_inject_int_array,	"n2_inject_int_array",
	N2_HD_MRAUASI,	n2_inject_int_array,	"n2_inject_int_array",

	/* Modular Arithmetic Unit and CWQ (SPARC Internal) errors. */
	NI_HD_MAUL,	memtest_h_ma_err,	"memtest_h_ma_err",
	NI_HD_MAUS,	memtest_h_ma_err,	"memtest_h_ma_err",
	NI_HD_MAUO,	memtest_h_ma_err,	"memtest_h_ma_err",
	N2_HD_CWQP,	n2_inject_cwq,		"n2_inject_cwq",

	/* System on Chip (SOC) MCU errors. */
	N2_HD_MCUECC,	n2_inject_soc_mcu,	"n2_inject_soc_mcu",
	N2_HD_MCUFBU,	n2_inject_soc_mcu,	"n2_inject_soc_mcu",
	N2_HD_MCUFBR,	n2_inject_soc_mcu,	"n2_inject_soc_mcu",

	/* System on Chip (SOC) Internal errors. */
	N2_IO_NIUDPAR,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NIUCTAGUE,	n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NIUCTAGCE,	n2_inject_soc_int,	"n2_inject_soc_int",

	N2_IO_SIOCTAGUE,	n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_SIOCTAGCE,	n2_inject_soc_int,	"n2_inject_soc_int",

	N2_IO_NCUDMUC,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NCUCTAGUE,	n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NCUCTAGCE,	n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NCUDMUUE,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NCUCPXUE,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NCUPCXUE,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NCUPCXD,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NCUINT,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NCUMONDOF,	n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NCUMONDOT,	n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_NCUDPAR,		n2_inject_soc_int,	"n2_inject_soc_int",

	N2_IO_DMUDPAR,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_DMUSIIC,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_DMUCTAGUE,	n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_DMUCTAGCE,	n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_DMUNCUC,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_DMUINT,		n2_inject_soc_int,	"n2_inject_soc_int",

	N2_IO_SIIDMUAP,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_SIIDMUDP,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_SIINIUAP,		n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_SIINIUDP,		n2_inject_soc_int,	"n2_inject_soc_int",

	N2_IO_SIIDMUCTU,	n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_SIIDMUCTC,	n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_SIINIUCTU,	n2_inject_soc_int,	"n2_inject_soc_int",
	N2_IO_SIINIUCTC,	n2_inject_soc_int,	"n2_inject_soc_int",

	/* SSI (bootROM interface) errors. */
	NI_HD_SSITO,	ni_k_ssi_err,		"ni_k_ssi_err",
	NI_HD_SSITOS,	ni_k_ssi_err,		"ni_k_ssi_err",
	NI_PRINT_SSI,	ni_k_ssi_err,		"ni_k_ssi_err",

	/* DEBUG test case(s) to get processor specific information from HV. */
	N2_TEST,	n2_inject_test_case, 	"n2_inject_test_case",
	N2_PRINT_ESRS,	n2_debug_print_esrs, 	"n2_debug_print_esrs",
	N2_CLEAR_ESRS,	n2_debug_clear_esrs, 	"n2_debug_clear_esrs",

	NULL,		NULL,			NULL,
};

static cmd_t *commands[] = {
	niagara2_cmds,
	sun4v_generic_cmds,
	NULL
};

void
n2_debug_init()
{
	int	i;

	for (i = 0; i < DEBUG_BUF_SIZE; i++)
		n2_debug_buf_va[i] = 0xeccdeb46eccdeb46;
}

void
n2_debug_dump()
{
	int	i;

	for (i = 0; i < DEBUG_BUF_SIZE; i++) {
		DPRINTF(0, "n2_debug_dump: n2_debug_buf[0x%2x]=0x%llx\n",
		    i*8, n2_debug_buf_va[i]);
	}
}

/*
 * *****************************************************************
 * The following block of routines are the Niagara-II test routines.
 * *****************************************************************
 */

/*
 * This routine produces a Control Word Queue (CWQ) protocol error by
 * requesting an invalid operation type.
 *
 * NOTE: This routine can be modified to add the capability of injecting
 *	 errors into the queue itself since the access routine can place
 *	 the queue into the EI buffer.  All we need to do is inject an
 *	 error into a region that maps to the queue AFTER it is setup.
 */
int
n2_inject_cwq(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		cwq_acc_type = CWQ_OP_INVALID;
	uint64_t	cwq_status_value;
	int		ret;
	char		*fname = "n2_inject_cwq";

	DPRINTF(3, "%s: iocp=0x%p, cwq_op=0x%llx\n", fname, iocp, cwq_acc_type);

	/*
	 * Perform the invalid operation (using polled mode).
	 */
	ret = OP_ACCESS_CWQ(mdatap, cwq_acc_type, (uint_t)0);

	/*
	 * Check to see if the CWQCSR.protocol_error bit (bit2=0x4) got set.
	 */
	cwq_status_value = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_STREAM, ASI_CWQ_CSR_REG,
	    (uint64_t)n2_debug_buf_pa, NULL);
	if (cwq_status_value & 0x4) {
		DPRINTF(4, "%s: CWQ status CSR=0x%llx (error set correctly)\n",
		    fname, cwq_status_value);
	} else {
		DPRINTF(0, "%s: CWQ status CSR=0x%llx (error not set!)\n",
		    fname, cwq_status_value);
	}

	return (ret);
}

/*
 * This routine inserts an error into the data cache parity bits protecting
 * the data or the tags at a location specified by the physical address mdata
 * struct member.
 *
 * Valid xorpat values are:
 * 	Data:     [63:0]
 * 	Data par: [20:13]
 * 	Tag:      [30:2]
 * 	Tag val:  [1] (master)
 * 	Tag par:  [13]
 * 	Tag vals: [14] (slave)
 *
 * NOTE: valid bit injections are performed using the tag routine because
 *	 the command definition is of subclass TAG and the valid bits
 *	 are part of the tag itself.
 *
 * NOTE: KT uses this routine as-is, the only difference is that the
 *	 tag field is extended to [34:2] due to the larger address space.
 */
int
n2_inject_dcache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	int		ret;
	char		*fname = "n2_inject_dcache";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	/*
	 * If the IOCTL specified the tag, corrupt the tag parity.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

#ifdef	L1_DEBUG_BUFFER
	n2_debug_init();
#endif	/* L1_DEBUG_BUFFER */

		ret = memtest_hv_inject_error("n2_inj_dcache_tag",
		    (void *)n2_inj_dcache_tag, (uint64_t)paddr,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)n2_debug_buf_pa);
#ifdef	L1_DEBUG_BUFFER
	n2_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_dcache_tag "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else if (ERR_SUBCLASS_ISMH(iocp->ioc_command)) {

#ifdef	L1_DEBUG_BUFFER
	n2_debug_init();
#endif	/* L1_DEBUG_BUFFER */

		ret = memtest_hv_inject_error("n2_inj_dcache_mult",
		    (void *)n2_inj_dcache_mult, (uint64_t)paddr,
		    IOC_XORPAT(iocp), (uint64_t)n2_debug_buf_pa, NULL);
#ifdef	L1_DEBUG_BUFFER
	n2_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_dcache_mult "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Otherwise corrupt the dcache data parity.
		 */

#ifdef	L1_DEBUG_BUFFER
	n2_debug_init();
#endif	/* L1_DEBUG_BUFFER */

		ret = memtest_hv_inject_error("n2_inj_dcache_data",
		    (void *)n2_inj_dcache_data, (uint64_t)paddr,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)n2_debug_buf_pa);
#ifdef	L1_DEBUG_BUFFER
	n2_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_dcache_data "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}
	return (0);
}

/*
 * This routine inserts an error into the data cache parity bits protecting
 * the data or the tags at a location specified by the byte offset in the
 * ioc_addr member of the mdata struct.
 *
 * This routine is similar to the above n2_inject_dcache() routine.
 *
 * NOTE: valid bit injections are performed using the tag routine because
 *	 the command definition is of subclass TAG and the valid bits
 *	 are part of the tag itself.
 */
int
n2_inject_dphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	int		ret;
	char		*fname = "n2_inject_dphys";

	DPRINTF(3, "%s: iocp=0x%p, offset=0x%lx\n", fname, iocp, offset);

	/*
	 * If the IOCTL specified the tag, corrupt the tag parity.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("n2_inj_dphys_tag",
		    (void *)n2_inj_dphys_tag, offset,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)n2_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_dphys_tag "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else if (ERR_SUBCLASS_ISMH(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("n2_inj_dphys_mult",
		    (void *)n2_inj_dphys_mult, offset,
		    IOC_XORPAT(iocp), (uint64_t)n2_debug_buf_pa, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_dphys_mult"
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Otherwise corrupt the dcache data parity.
		 */
		ret = memtest_hv_inject_error("n2_inj_dphys_data",
		    (void *)n2_inj_dphys_data, offset,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)n2_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_dphys_data "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}
	return (0);
}

/*
 * This routine inserts an ecc error into a floating point register using a
 * register chosen by an offset.
 *
 * Valid xorpat/eccmask values are bits[7:0].
 *
 * NOTE: Niagara-II only implements one set of floating point registers per
 *	 physical core, so unlike the integer register file tests there are
 *	 no issues in regard to register windows.
 */
int
n2_inject_freg_file(mdata_t *mdatap, uint64_t offset)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	enable;
	uint64_t	eccmask;
	int		ret;
	char		*fname = "n2_inject_freg_file";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx, offset=%d\n",
	    fname, iocp, paddr, offset);

	/*
	 * Get the mask to use for the ECC bit corruption.
	 */
	eccmask = IOC_XORPAT(iocp);

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode.
	 */
	enable = (REG_FRC_ENABLE | REG_INJECTION_ENABLE | REG_SSHOT_ENABLE);
	if (F_INF_INJECT(iocp))
		enable &= ~((uint64_t)REG_SSHOT_ENABLE);

	ret = memtest_hv_inject_error("n2_inj_freg_file",
	    (void *)n2_inj_freg_file, paddr, enable, eccmask,
	    (offset * REG_ERR_STRIDE));

	DPRINTF(3, "%s: ret = 0x%x\n", fname, ret);
	return (0);
}

/*
 * This routine inserts an ecc error into an integer register using a register
 * chosen by an offset.  Injected errors are accessed in either hyperpiv or
 * kernel mode.  Because the injection register is per core (not per strand)
 * the kernel mode tests are less deterministic then the hyperpriv versions.
 *
 * Valid xorpat/eccmask values are bits[7:0].
 */
int
n2_inject_ireg_file(mdata_t *mdatap, uint64_t misc1)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	enable;
	uint64_t	eccmask;
	uint64_t	offset = misc1;
	int		ret;
	char		*fname = "n2_inject_ireg_file";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx, offset=%d\n",
	    fname, iocp, paddr, offset);

	/*
	 * Get the mask to use for the ECC bit corruption.
	 */
	eccmask = IOC_XORPAT(iocp);

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode (applicable only to kernel mode commands).
	 */
	enable = (REG_IRC_ENABLE | REG_INJECTION_ENABLE | REG_SSHOT_ENABLE);
	if (F_INF_INJECT(iocp))
		enable &= ~((uint64_t)REG_SSHOT_ENABLE);

	/*
	 * Perform the access in hyperpriv mode for integer register file
	 * errors.  Choose the access type by overloading bits in the
	 * enable inpar which the asm rountine uses to switch on.
	 */
	if (ERR_MODE_ISHYPR(iocp->ioc_command)) {
		if (F_NOERR(iocp)) {
			enable |= EI_REG_NOERR_BIT;
		} else if (ERR_ACC_ISOP(iocp->ioc_command)) {
			enable |= EI_REG_ACC_OP_BIT;
		} else if (ERR_ACC_ISLOAD(iocp->ioc_command)) {
			enable |= EI_REG_ACC_LOAD_BIT;
		}

		DPRINTF(3, "%s: eccmask=0x%llx, enable=0x%llx\n",
		    fname, eccmask, enable);

		/*
		 * The hyperpriv routines use the misc options differently
		 * than the other register file commands.  This was done in
		 * a manner that allows the different versions of the register
		 * file routines to share common routines.  As follows:
		 *	misc1 = register type (global, in, local, or out)
		 *	misc2 = register offset (reg 0 through reg 7)
		 */
		offset = (F_MISC2(iocp) ? (iocp->ioc_misc2) + 1 :
		    REG_DEFAULT_OFFSET);
		if ((offset <= 0) || (offset > 8)) {
			DPRINTF(0, "%s: unsupported MISC2 argument to choose "
			    "register, using default register %%r%d\n",
			    fname, REG_DEFAULT_OFFSET - 1);
			offset = REG_DEFAULT_OFFSET;
		}

		/*
		 * The misc1 user option is used to determine the type
		 * of register file that the error should be injected
		 * into: in, local, out, or global.  There are separate
		 * hyperpriv assembler routines for each type.
		 */
		if (CPU_ISKT(mdatap->m_cip)) {
			if (misc1 == N2_HPRIV_IRF_GLOBAL) {
				DPRINTF(4, "%s: calling HV global routine\n",
				    fname);
				ret = memtest_hv_inject_error(
				    "kt_inj_ireg_hvfile_global", (void *)
				    kt_inj_ireg_hvfile_global, paddr, enable,
				    eccmask, (offset * N2_IRF_STRIDE_SIZE));
			} else if (misc1 == N2_HPRIV_IRF_IN) {
				DPRINTF(4, "%s: calling HV in routine\n",
				    fname);
				ret = memtest_hv_inject_error(
				    "kt_inj_ireg_hvfile_in", (void *)
				    kt_inj_ireg_hvfile_in, paddr, enable,
				    eccmask, (offset * N2_IRF_STRIDE_SIZE));
			} else if (misc1 == N2_HPRIV_IRF_LOCAL) {
				DPRINTF(4, "%s: calling HV local routine\n",
				    fname);
				ret = memtest_hv_inject_error(
				    "kt_inj_ireg_hvfile_local", (void *)
				    kt_inj_ireg_hvfile_local, paddr, enable,
				    eccmask, (offset * N2_IRF_STRIDE_SIZE));
			} else if (misc1 == N2_HPRIV_IRF_OUT) {
				DPRINTF(4, "%s: calling HV out routine\n",
				    fname);
				ret = memtest_hv_inject_error(
				    "kt_inj_ireg_hvfile_out", (void *)
				    kt_inj_ireg_hvfile_out, paddr, enable,
				    eccmask, (offset * N2_IRF_STRIDE_SIZE));
			} else {
				DPRINTF(0, "%s: unsupported MISC1 field for "
				    "integer register type, cmd=0x%x\n",
				    fname, iocp->ioc_command);
			}

		} else {
			if (misc1 == N2_HPRIV_IRF_GLOBAL) {
				DPRINTF(4, "%s: calling HV global routine\n",
				    fname);
				ret = memtest_hv_inject_error(
				    "n2_inj_ireg_hvfile_global", (void *)
				    n2_inj_ireg_hvfile_global, paddr, enable,
				    eccmask, (offset * N2_IRF_STRIDE_SIZE));
			} else if (misc1 == N2_HPRIV_IRF_IN) {
				DPRINTF(4, "%s: calling HV in routine\n",
				    fname);
				ret = memtest_hv_inject_error(
				    "n2_inj_ireg_hvfile_in", (void *)
				    n2_inj_ireg_hvfile_in, paddr, enable,
				    eccmask, (offset * N2_IRF_STRIDE_SIZE));
			} else if (misc1 == N2_HPRIV_IRF_LOCAL) {
				DPRINTF(4, "%s: calling HV local routine\n",
				    fname);
				ret = memtest_hv_inject_error(
				    "n2_inj_ireg_hvfile_local", (void *)
				    n2_inj_ireg_hvfile_local, paddr, enable,
				    eccmask, (offset * N2_IRF_STRIDE_SIZE));
			} else if (misc1 == N2_HPRIV_IRF_OUT) {
				DPRINTF(4, "%s: calling HV out routine\n",
				    fname);
				ret = memtest_hv_inject_error(
				    "n2_inj_ireg_hvfile_out", (void *)
				    n2_inj_ireg_hvfile_out, paddr, enable,
				    eccmask, (offset * N2_IRF_STRIDE_SIZE));
			} else {
				DPRINTF(0, "%s: unsupported MISC1 field for "
				    "integer register type, cmd=0x%x\n",
				    fname, iocp->ioc_command);
			}
		}
	} else {
		if (CPU_ISKT(mdatap->m_cip)) {
			ret = ni_k_inject_ireg(mdatap, "kt_inj_ireg_file",
			    (void *)kt_inj_ireg_file, paddr, enable, eccmask,
			    (offset * IREG_STRIDE_SIZE));
		} else {
			ret = ni_k_inject_ireg(mdatap, "n2_inj_ireg_file",
			    (void *)n2_inj_ireg_file, paddr, enable, eccmask,
			    (offset * IREG_STRIDE_SIZE));
		}
	}

	DPRINTF(3, "%s: ret = 0x%x\n", fname, ret);
	return (0);
}

/*
 * This routine inserts an ecc or parity error into one of the internal
 * register arrays using the HW provided injection mechanism (one register).
 *
 * Valid xorpat/eccmask values are bits[7:0].
 *
 * The register arrays which are covered by this routine are:
 *	- MMU registers
 *	- Trap Stack registers
 *	- Scratchpad registers
 *	- Tick Compare registers
 */
int
n2_inject_int_array(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	enable;
	uint64_t	eccmask;
	uint64_t	offset;
	int		ret;
	char		*fname = "n2_inject_int_array";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	/*
	 * Get the mask to use for the ECC bit corruption.
	 */
	eccmask = IOC_XORPAT(iocp);

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode.
	 */
	enable = (REG_INJECTION_ENABLE | REG_SSHOT_ENABLE);
	if (F_INF_INJECT(iocp))
		enable &= ~((uint64_t)REG_SSHOT_ENABLE);

	if (F_NOERR(iocp))
		enable |= EI_REG_NOERR_BIT;

	/*
	 * Determine which type of error to inject and call the appropriate
	 * asm level routine.
	 *
	 * NOTE: can enhance this MMU case by using a misc arg to choose the
	 *	 offset value so that specific registers can be used.
	 */
	if (ERR_PROT_ISPE(iocp->ioc_command)) {
		/*
		 * Inject an error into the MMU register array.
		 * These commands are defined with PE as the protection field,
		 * and no others can be without modifying this routine.
		 */
		enable |= REG_MRA_ENABLE;
		offset = MMU_ZERO_CTX_TSB_CFG_0;

		if (!ERR_ACC_ISASI(iocp->ioc_command))
			/* allow HW to hit error, tablewalks must be enabled */
			enable |= EI_REG_NOERR_BIT;

		ret = memtest_hv_inject_error("n2_inj_mra",
		    (void *)n2_inj_mra, offset, enable, eccmask, NULL);

	} else if (ERR_SUBCLASS_ISTSA(iocp->ioc_command)) {
		/*
		 * Inject an error into the Trap Stack register array.
		 */
		enable |= REG_TSA_ENABLE;
		offset = 0x0;

		ret = memtest_hv_inject_error("n2_inj_tsa",
		    (void *)n2_inj_tsa, offset, enable, eccmask, NULL);

	} else if (ERR_SUBCLASS_ISSCR(iocp->ioc_command)) {
		/*
		 * Inject an error into the Scratchpad register array.
		 * Note that the valid set of offsets are 0x0-0x38 (step = 8).
		 * The default value is 0x10 to make it non-trivial (not 0x0).
		 */
		enable |= REG_SCA_ENABLE;
		offset = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 0x10);
		offset &= ~((uint64_t)0x3);
		if ((offset < 0) || (offset > 0x38)) {
			DPRINTF(0, "%s: invalid scratchpad register offset "
			    "argument 0x%x, using default of 0x10\n",
			    fname, offset);
			offset = 0x10;
		}

		if (ERR_MODE_ISKERN(iocp->ioc_command))
			enable |= EI_REG_NOERR_BIT;

		ret = memtest_hv_inject_error("n2_inj_sca",
		    (void *)n2_inj_sca, offset, enable, eccmask, NULL);

		/*
		 * Access the ASI register in kernel mode.
		 */
		if (ERR_MODE_ISKERN(iocp->ioc_command) && !F_NOERR(iocp)) {
			ret = peek_asi64(ASI_SCRATCHPAD, offset);
		}
	} else if (ERR_SUBCLASS_ISTCA(iocp->ioc_command)) {
		/*
		 * Inject an error into the Tick Compare register array.
		 *
		 * A MISC1 argument to this function will determine which
		 * (s)tick register is used for the injection.
		 *
		 * NOTE: could create KERN mode inject and access routines for
		 *	 these errors because two of the registers are PRIV.
		 *	 Currently everything is done in hyperpriv mode.
		 */
		enable |= REG_TCA_ENABLE;

		offset = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 0);
		if ((offset < 0) || (offset > 2)) {
			DPRINTF(0, "%s: invalid misc argument using default "
			    "= %d\n", fname, 0);
			offset = 0;
		}

		if (!ERR_ACC_ISASR(iocp->ioc_command))
			/* allow HW to hit error (different error type) */
			enable |= EI_REG_NOERR_BIT;

		ret = memtest_hv_inject_error("n2_inj_tca",
		    (void *)n2_inj_tca, offset, enable, eccmask, NULL);
	} else {
		DPRINTF(0, "%s: unknown internal error type! "
		    "Exiting without injecting error.\n", fname);
		return (ENOTSUP);
	}

	DPRINTF(3, "%s: ret = 0x%x\n", fname, ret);
	return (0);
}

/*
 * This routine inserts an error into the parity bits protecting the
 * L1 data cache data or L1 the data cache tags, or inserts multiple
 * copies of a tag at a location(s) determined by the physical address
 * mdata struct member.
 *
 * This routine is similar to the n2_inject_dcache() routine except that
 * the corrupted paddr will be accessed while in hyperpriv mode so there is
 * no opportunity to use the NO_ERR flag.
 */
int
n2_inject_hvdcache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint_t		access_type = 0;
	int		ret;
	char		*fname = "n2_inject_hvdcache";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	if ((ERR_ACC(iocp->ioc_command) == ERR_ACC_STORE)) {
		access_type = ERR_ACC_STORE;
	}

	/*
	 * If the IOCTL specified the tag, corrupt the tag parity.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("n2_inj_dcache_hvtag",
		    (void *)n2_inj_dcache_hvtag, paddr,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)access_type);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_dcache_hvtag "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else if (ERR_SUBCLASS_ISMH(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("n2_inj_dcache_hvmult",
		    (void *)n2_inj_dcache_hvmult, paddr,
		    IOC_XORPAT(iocp), (uint64_t)access_type, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_dcache_hvmult"
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Otherwise corrupt the dcache data parity.
		 */
		ret = memtest_hv_inject_error("n2_inj_dcache_hvdata",
		    (void *)n2_inj_dcache_hvdata, paddr,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)access_type);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_dcache_hvdata "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}
	return (0);
}

/*
 * This routine inserts an error into the parity bits protecting the
 * instruction cache data or the instruction cache tags at a location
 * that corresponds to the location of the special access routine for
 * this test.  The access routine must be mapped into memory for the
 * test to work so it is placed in such a way as to always be on the same
 * page as the injection routine.
 *
 * NOTE: the data, tag, and mult asm routines are using the same access
 *	 routine, this worked during testing.  But it is possible that the
 *	 access routine can get too far away from the injection in later
 *	 builds.  So if errors are encountered, can copy the current routine
 *	 to a number of separate ones.
 *
 * NOTE: the corrupted hyperpriv routine will be run while in hyperpriv
 *	 mode so there is no opportunity to use the NO_ERR flag.
 */
int
n2_inject_hvicache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	caddr_t		kvaddr;
	uint64_t	raddr, paddr;
	int		ret;
	char		*fname = "n2_inject_hvicache";

	/*
	 * Find the raddr and paddr of asm routine to corrupt from it's kvaddr.
	 */
	kvaddr = (caddr_t)n2_ic_hvaccess;
	raddr = memtest_kva_to_ra((void *)kvaddr);

	if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
		DPRINTF(0, "%s: ra to pa translation failed "
		    "for raddr = 0x%llx\n", fname, raddr);
		return ((int)paddr);
	}

	DPRINTF(3, "%s: iocp=0x%p, function kvaddr=0x%llx, "
	    "raddr=0x%llx, paddr=0x%llx\n", fname, iocp, kvaddr,
	    raddr, paddr);

	/*
	 * If the IOCTL specified the tag, corrupt the tag parity.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("n2_inj_icache_hvtag",
		    (void *)n2_inj_icache_hvtag, paddr, IOC_XORPAT(iocp),
		    (uint64_t)n2_debug_buf_pa, NULL);

		if (ret == 0xded) {
			DPRINTF(0, "%s: n2_inj_icache_hvtag FAILED to find "
			    "matching tag in icache,\n\t\tlikely it has "
			    "been evicted, ret=0x%x\n", fname, ret);
			return (-1);
		}
	} else if (ERR_SUBCLASS_ISMH(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("n2_inj_icache_hvmult",
		    (void *)n2_inj_icache_hvmult, paddr, IOC_XORPAT(iocp),
		    (uint64_t)n2_debug_buf_pa, NULL);

		if (ret == 0xded) {
			DPRINTF(0, "%s: n2_inj_icache_hvmult FAILED to find "
			    "matching tag in icache,\n\t\tlikely it has "
			    "been evicted, ret=0x%x\n", fname, ret);
			return (-1);
		}
	} else {
		/*
		 * Otherwise corrupt the icache instr parity.
		 */
		ret = memtest_hv_inject_error("n2_inj_icache_hvinstr",
		    (void *)n2_inj_icache_hvinstr, paddr, IOC_XORPAT(iocp),
		    (uint64_t)n2_debug_buf_pa, NULL);

		if (ret == 0xded) {
			DPRINTF(0, "%s: n2_inj_icache_hvinstr FAILED to find "
			    "matching tag in icache,\n\t\tlikely it has "
			    "been evicted, ret=0x%x\n", fname, ret);
			return (-1);
		}
	}
	return (0);
}

/*
 * This routine inserts an error into the parity bits protecting the
 * instruction cache data or the instruction cache tags at a location
 * determined by the function members of the mdata struct.
 *
 * Valid xorpat values are:
 * 	Instr:     [31:0]
 * 	Instr par: [32]
 * 	Tag:       [30:2]
 * 	Tag val:   [1] (master)
 * 	Tag par:   [16]
 * 	Tag vals:  [15] (slave)
 *
 * NOTE: valid bit injections are performed using the tag routine because
 *	 the command definition is of subclass TAG and the valid bits
 *	 are part of the tag itself.
 */
int
n2_inject_icache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	vaddr, raddr, paddr;
	void		(*func_vaddr)(caddr_t);
	uint_t		myid;
	int		ret;
	char		*fname = "n2_inject_icache";

	DPRINTF(3, "%s: iocp=0x%p\n", fname, iocp);
	myid = getprocessorid();

	/*
	 * Determine which routine will be used for the corruption based on
	 * the command definition.
	 */
	if (ERR_MISC_ISTL1(iocp->ioc_command)) {
		vaddr = (uint64_t)(mdatap->m_asmld_tl1);
	} else {
		if (ERR_ACC_ISPFETCH(iocp->ioc_command))
			/*
			 * NOTE: the below line uses the pcrel routine,
			 *	 b/c there is no iprefetch routine yet.
			 */
			vaddr = (uint64_t)(mdatap->m_pcrel);
		else
			vaddr = (uint64_t)(mdatap->m_asmld);
	}

	/*
	 * Cast the chosen routines vaddr for below call(s).
	 */
	func_vaddr = (void (*)(caddr_t))vaddr;

	/*
	 * Find the corruption routines raddr from it's (kernel) vaddr.
	 */
	if ((raddr = memtest_kva_to_ra((void *)vaddr)) == -1) {
		return ((int)raddr);
	}

	/*
	 * Translate the raddr of routine to paddr via hypervisor.
	 */
	if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
		DPRINTF(0, "%s: ra to pa translation failed for "
		    "raddr = 0x%llx\n", fname, raddr);
		return ((int)paddr);
	}

	DPRINTF(2, "%s: vaddr of routine to corrupt = 0x%llx, raddr = 0x%llx, "
	    "paddr = 0x%llx\n", fname, vaddr, raddr, paddr);

#ifdef	L1_DEBUG_BUFFER
	n2_debug_init();
#endif	/* L1_DEBUG_BUFFER */

	/*
	 * If the IOCTL specified the tag, corrupt the tag parity or the
	 * valid bit parity which is part of the tag.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		/*
		 * Cause the target instruction(s) to be loaded into the i$.
		 */
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)(mdatap->m_asmld_tl1),
			    (uint64_t)mdatap->m_kvaddr_c, (uint64_t)0);
		} else {
			(*func_vaddr)((caddr_t)mdatap->m_kvaddr_c);
		}

		ret = memtest_hv_inject_error("n2_inj_icache_tag",
		    (void *)n2_inj_icache_tag, paddr, IOC_XORPAT(iocp),
		    (uint64_t)n2_debug_buf_pa, NULL);

		/*
		 * Trigger error by running routine as close to injection
		 * as possible.
		 */
		if (!F_NOERR(iocp) && (ret != 0xded)) {
			if (ERR_MISC_ISTL1(iocp->ioc_command)) {
				xt_one(myid, (xcfunc_t *)(mdatap->m_asmld_tl1),
				    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
			} else {
				(*func_vaddr)((caddr_t)mdatap->m_kvaddr_a);
			}
		} else if (ret == 0xded) {
			DPRINTF(0, "%s: n2_inj_icache_tag FAILED to find "
			    "matching tag in icache,\n\t\tlikely it has "
			    "been evicted, ret=0x%x\n", fname, ret);
#ifdef	L1_DEBUG_BUFFER
	n2_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

			return (-1);
		}
	} else if (ERR_SUBCLASS_ISMH(iocp->ioc_command)) {

		/*
		 * Cause the target instruction(s) to be loaded into the i$.
		 */
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)(mdatap->m_asmld_tl1),
			    (uint64_t)mdatap->m_kvaddr_c, (uint64_t)0);
		} else {
			(*func_vaddr)((caddr_t)mdatap->m_kvaddr_c);
		}

		ret = memtest_hv_inject_error("n2_inj_icache_mult",
		    (void *)n2_inj_icache_mult, paddr, IOC_XORPAT(iocp),
		    (uint64_t)n2_debug_buf_pa, NULL);

		/*
		 * Trigger error by running routine as close to injection
		 * as possible.
		 */
		if (!F_NOERR(iocp) && (ret != 0xded)) {
			if (ERR_MISC_ISTL1(iocp->ioc_command)) {
				xt_one(myid, (xcfunc_t *)(mdatap->m_asmld_tl1),
				    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
			} else {
				(*func_vaddr)((caddr_t)mdatap->m_kvaddr_a);
			}
		} else if (ret == 0xded) {
			DPRINTF(0, "%s: n2_inj_icache_mult FAILED to find "
			    "matching tag in icache,\n\t\tlikely it has "
			    "been evicted, ret=0x%x\n", fname, ret);
#ifdef	L1_DEBUG_BUFFER
	n2_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

			return (-1);
		}
	} else {
		/*
		 * Otherwise corrupt the icache instr parity.
		 *
		 * Cause the target instruction(s) to be loaded into the i$.
		 */
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)(mdatap->m_asmld_tl1),
			    (uint64_t)mdatap->m_kvaddr_c, (uint64_t)0);
		} else {
			(*func_vaddr)((caddr_t)mdatap->m_kvaddr_c);
		}

		ret = memtest_hv_inject_error("n2_inj_icache_instr",
		    (void *)n2_inj_icache_instr, paddr, IOC_XORPAT(iocp),
		    (uint64_t)n2_debug_buf_pa, NULL);

		/*
		 * Trigger error by running routine as close to injection
		 * as possible.
		 */
		if (!F_NOERR(iocp) && (ret != 0xded)) {
			if (ERR_MISC_ISTL1(iocp->ioc_command)) {
				xt_one(myid, (xcfunc_t *)(mdatap->m_asmld_tl1),
				    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
			} else {
				(*func_vaddr)((caddr_t)mdatap->m_kvaddr_a);
			}
		} else if (ret == 0xded) {
			DPRINTF(0, "%s: n2_inj_icache_instr FAILED to find "
			    "matching tag in icache,\n\t\tlikely it has "
			    "been evicted, ret=0x%x\n", fname, ret);
#ifdef	L1_DEBUG_BUFFER
	n2_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

			return (-1);
		}
	}

#ifdef	L1_DEBUG_BUFFER
	n2_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

	return (0);
}

/*
 * This routine inserts an error into the parity bits protecting the
 * instruction cache data or the instruction cache tags at a location
 * specified by the byte offset in the ioc_addr member of the ioc struct.
 *
 * This routine is similar to the above n2_inject_icache() routine except
 * that it uses a cache offset to choose the line to corrupt.
 *
 * Valid byte offset values are in the range: 0x0 - 0x3ff8 (16KB cache)
 * and note that the HV routine will align the offset for the asi accesses.
 *
 * NOTE: valid bit injections are performed using the tag routine because
 *	 the command definition is of subclass TAG and the valid bits
 *	 are part of the tag itself.
 */
int
n2_inject_iphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	int		ret;
	char		*fname = "n2_inject_iphys";

	DPRINTF(3, "%s: iocp=0x%p, offset=0x%llx\n", fname, iocp, offset);

	/*
	 * If the IOCTL specified the tag, corrupt the tag parity.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("n2_inj_iphys_tag",
		    (void *)n2_inj_iphys_tag, offset, IOC_XORPAT(iocp),
		    (uint64_t)n2_debug_buf_pa, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_iphys_tag FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}
	} else if (ERR_SUBCLASS_ISMH(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("n2_inj_iphys_mult",
		    (void *)n2_inj_iphys_mult, offset, IOC_XORPAT(iocp),
		    (uint64_t)n2_debug_buf_pa, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_icache_mult FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Otherwise corrupt the icache instr parity.
		 */
		ret = memtest_hv_inject_error("n2_inj_iphys_instr",
		    (void *)n2_inj_iphys_instr, offset, IOC_XORPAT(iocp),
		    (uint64_t)n2_debug_buf_pa, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_iphys_instr FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}
	}
	return (0);
}

/*
 * This routine inserts an error into the L2 cache data, the L2 cache tags,
 * or the ECC protecting one of the data or the tags at a location determined
 * by the physical address member of the mdata structure.
 *
 * Because L2 cache data is read/written as a 32-bit half word only the
 * upper 32 OR the lower 32 bits of a value can be corrupted at one time.
 *
 * To allow corruption in the upper 32 bits the xorpat values for this func
 * must be zero aligned.  The asm routine determines the half word to
 * corrupt based on the physical address and the xorpat which allows either
 * half of the 64-bit xorpat to be used.  Valid xorpat values are:
 * 	Data:     [63:0]
 * 	Data ecc: [6:0]
 * 	Tag:      [21:0]
 * 	Tag ecc:  [5:0]
 *
 * The L2 cache has a Writeback buffer, a Fill buffer, and a DMA buffer.
 * "Errors in these buffers are indistinguishable from L2 cache errors."
 * Writing to the L2 config registers performs a flush of these buffers,
 * Therefore the asm routine writes to/from DMMODE flush the buffers.
 *
 * NOTE: only correctable tag errors (LTC) are defined on Niagara-II, even
 *	 multi-bit tag errors will produce an LTC error (as on Niagara-I).
 */
int
n2_inject_l2cache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		myid = getprocessorid();
	uint64_t	raddr = mdatap->m_raddr_c;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	idx_paddr, tmp_paddr, tag_paddr;
	uint64_t	l2cr_addr;
	uint64_t	l2cr_value;
	uint64_t	l2cr_saved;
	uint64_t	l2_bank_mask;
	int		ret;
	char		*fname = "n2_inject_l2cache";

	DPRINTF(3, "%s: iocp=0x%p, raddr=0x%llx, paddr=0x%llx\n",
	    fname, iocp, raddr, paddr);

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 *
	 * NOTE: the L2 cache asm routines use a fixed bitmask for the bank
	 *	 (is PA[8:6]) even if some banks are disabled.  This should
	 *	 still work since the HW is supposed to automatically mask
	 *	 the register addresses when in reduced bank modes.
	 */
	l2_bank_mask = n2_check_l2_bank_mode();

	/*
	 * Determine the L2 cache index hashing (IDX) address to use, if
	 * IDX mode is disabled the idx_paddr will match the original paddr.
	 */
	(void) n2_check_l2_idx_mode(paddr, &idx_paddr);

	/*
	 * Because the tag diagnostic ASI addressing is affected by the
	 * number of enabled L2-cache banks, modify the idx'd paddr (which
	 * will be used by the asm routine only for the tag ASI) to account
	 * for the supported reduced bank modes.
	 */
	if (l2_bank_mask == N2_L2_2BANK_MASK) {
		/*
		 * Set tag ASI address bits 17:9 to bits 15:7 from paddr.
		 */
		tmp_paddr = (idx_paddr & 0xff80) << 2;
		tag_paddr = idx_paddr & ~(0xff80 << 2);
		tag_paddr |= tmp_paddr;

		DPRINTF(3, "%s: 2 banks idx'd paddr = 0x%llx, tag ASI paddr "
		    "= 0x%llx\n", fname, idx_paddr, tag_paddr);

	} else if (l2_bank_mask == N2_L2_4BANK_MASK) {
		/*
		 * Set tag ASI address bits 17:9 to bits 16:8 from paddr.
		 */
		tmp_paddr = (idx_paddr & 0x1ff00) << 1;
		tag_paddr = idx_paddr & ~(0x1ff00 << 1);
		tag_paddr |= tmp_paddr;

		DPRINTF(3, "%s: 4 banks idx'd paddr = 0x%llx, tag ASI paddr "
		    "= 0x%llx\n", fname, idx_paddr, tag_paddr);

	} else if (l2_bank_mask == N2_L2_8BANK_MASK) {
		/*
		 * Set tag ASI address bits to idx'd paddr.
		 */
		tag_paddr = idx_paddr;

		DPRINTF(3, "%s: 8 banks, tag ASI paddr = idx'd paddr = "
		    "0x%llx\n", fname, idx_paddr);
	} else {
		DPRINTF(0, "%s: unsupported number of L2 banks enabled, "
		    "L2 bank mask = 0x%lx\n", fname, l2_bank_mask);
		return (-1);
	}

	/*
	 * Determine addr and read contents of the L2 control register.
	 */
	l2cr_addr = N2_L2_CTL_REG + (paddr & l2_bank_mask);
	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	l2cr_saved = l2cr_value;

	/*
	 * Set the L2$ into DM mode prior to the call to HV so the
	 * data/instructions can be installed into L2$ by this routine
	 * and the line changed to modified if req'd.
	 */
	l2cr_value |= N2_L2CR_DMMODE;
	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, l2cr_addr, l2cr_value, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 for DM"
		    " mode FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	l2cr_value = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, l2cr_addr, NULL, NULL, NULL);
	DPRINTF(3, "%s: L2CR(at 0x%lx) set to = 0x%lx\n",
	    fname, l2cr_addr, l2cr_value);

	/*
	 * Place the data in the cache into the modified (M) state for all
	 * non-instruction L2 errors (this is required for write-back and
	 * scrub errors, and is preferable for normal L2$ errors).
	 * Also honor the user options which can force a cacheline state.
	 */
	if (F_CACHE_DIRTY(iocp)) {
		stphys(raddr, ldphys(raddr));
	} else if (F_CACHE_CLN(iocp)) {
		DPRINTF(1, "%s: not changing L2 cache line state "
		    "to modified due to user option\n", fname);
	} else if ((ERR_ACC_ISFETCH(iocp->ioc_command) ||
	    ERR_ACC_ISPFETCH(iocp->ioc_command)) &&
	    !ERR_CLASS_ISL2WB(iocp->ioc_command)) {
		DPRINTF(1, "%s: not changing L2 cache line state "
		    "to modified due to test definition\n", fname);
	} else {
		stphys(raddr, ldphys(raddr));
	}

	/*
	 * Instruction and data accesses are treated separately because the
	 * required L1 cache flushing is dependant on the access type.
	 */
	if (ERR_ACC_ISFETCH(iocp->ioc_command)) {

		/*
		 * If this is a kernel test cause target instruction(s) to
		 * be loaded into caches.  User code has already been loaded.
		 */
		if (ERR_MODE_ISHYPR(iocp->ioc_command)) {
			(void) memtest_hv_util("hv_paddr_load64",
			    (void *)mdatap->m_asmld, mdatap->m_paddr_a, NULL,
			    NULL, NULL);
		} else if (ERR_MODE_ISKERN(iocp->ioc_command)) {
			if (ERR_MISC_ISTL1(iocp->ioc_command)) {
				xt_one(myid, (xcfunc_t *)(mdatap->m_asmld_tl1),
				    (uint64_t)mdatap->m_kvaddr_c, (uint64_t)0);
			} else if (ERR_MISC_ISPCR(iocp->ioc_command)) {
				(mdatap->m_pcrel)();
			} else {
				(mdatap->m_asmld)(mdatap->m_kvaddr_c);
			}
		} else {
			DPRINTF(0, "%s: unknown L2 instruction error mode! "
			    "Exiting without injecting error.\n", fname);
			return (ENOTSUP);
		}

		/*
		 * If the IOCTL specified the tag, corrupt the tag or the
		 * tag ECC bits.
		 */
		if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

			ret = memtest_hv_inject_error(
			    "n2_inj_l2cache_instr_tag",
			    (void *)n2_inj_l2cache_instr_tag,
			    paddr, IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), tag_paddr);

			if (ret == -1) {
				DPRINTF(0, "%s: n2_inj_l2cache_instr_tag "
				    "FAILED, ret=0x%x\n", fname, ret);
				return (ret);
			}
		} else {
			/*
			 * Otherwise corrupt the l2cache instr or the instr ECC.
			 */
			ret = memtest_hv_inject_error(
			    "n2_inj_l2cache_instr_data",
			    (void *)n2_inj_l2cache_instr_data,
			    paddr, IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), idx_paddr);

			if (ret == -1) {
				DPRINTF(0, "%s: n2_inj_l2cache_instr_data "
				    "FAILED, ret=0x%x\n", fname, ret);
				return (ret);
			}
		}
	} else {	/* Data tests */
		/*
		 * If the IOCTL specified the tag, corrupt the tag or the
		 * tag ECC bits.
		 */
		if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

#ifdef	L2_DEBUG_BUFFER
			/*
			 * DEBUG - replace CHECK_FLAG arg with debug buf.
			 *	Requires that L2_DEBUG_BUFFER is also def'd
			 *	in the asm routine to match.
			 */
			n2_debug_init();

			ret = memtest_hv_inject_error("n2_inj_l2cache_tag",
			    (void *)n2_inj_l2cache_tag, paddr,
			    IOC_XORPAT(iocp),
			    (uint64_t)n2_debug_buf_pa, tag_paddr);

			n2_debug_dump();
#else	/* L2_DEBUG_BUFFER */
			ret = memtest_hv_inject_error("n2_inj_l2cache_tag",
			    (void *)n2_inj_l2cache_tag, paddr,
			    IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), tag_paddr);

#endif	/* L2_DEBUG_BUFFER */

			if (ret == -1) {
				DPRINTF(0, "%s: n2_inj_l2cache_tag FAILED, "
				    "ret=0x%x\n", fname, ret);
				return (ret);
			}
		} else {
			/*
			 * Otherwise corrupt the l2cache data or the data ECC.
			 */
#ifdef	L2_DEBUG_BUFFER
			/*
			 * DEBUG - replace CHECK_FLAG arg with debug buf.
			 *	Requires that L2_DEBUG_BUFFER is also def'd
			 *	in the asm routine to match.
			 */
			n2_debug_init();

			ret = memtest_hv_inject_error("n2_inj_l2cache_data",
			    (void *)n2_inj_l2cache_data, paddr,
			    IOC_XORPAT(iocp),
			    (uint64_t)n2_debug_buf_pa, idx_paddr);

			n2_debug_dump();
#else	/* L2_DEBUG_BUFFER */

			ret = memtest_hv_inject_error("n2_inj_l2cache_data",
			    (void *)n2_inj_l2cache_data, paddr,
			    IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), idx_paddr);

#endif	/* L2_DEBUG_BUFFER */

			if (ret == -1) {
				DPRINTF(0, "%s: n2_inj_l2cache_data FAILED, "
				    "ret=0x%x\n", fname, ret);
				return (ret);
			}
		}
	}

	/*
	 * Return the L2 cache bank to it's previous state.
	 */
	if (l2cr_value != l2cr_saved) {
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, l2cr_addr,
		    l2cr_saved, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 disable DM mode "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	return (0);
}

/*
 * This routine inserts one or more parity errors into the L2 directory
 * bits at a location determined by the mdata struct member mdatap->m_paddr_c
 * or for PHYS tests at the byte offset in the iocp->ioc_addr member.
 */
int
n2_inject_l2dir(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	l2_bank_mask;
	uint64_t	enable;
	uint_t		data_flag = 1;
	int		ret;
	char		*fname = "n2_inject_l2dir";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 *
	 * NOTE: the L2 cache asm routines ase a fixed bitmask for the bank
	 *	 (is PA[8:6]) even if some banks are disabled.  This should
	 *	 still work since the HW is supposed to automatically mask
	 *	 the register addresses when in reduced bank modes.
	 */
	l2_bank_mask = n2_check_l2_bank_mode();

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode.
	 */
	enable = (NI_L2_SSHOT_ENABLE | NI_L2_DIR_INJ_ENABLE);
	if (F_INF_INJECT(iocp))
		enable &= ~((uint64_t)NI_L2_SSHOT_ENABLE);

	/*
	 * If the error is of instruction type, ensure data is not used.
	 */
	if (ERR_ACC_ISFETCH(iocp->ioc_command)) {
		data_flag = 0;
	}

	/*
	 * Determine which asm routine to use for injection.
	 */
	if (ERR_MISC_ISPHYS(iocp->ioc_command)) {

		/*
		 * Corrupt the L2 cache directory parity using offset.
		 */
		ret = memtest_hv_inject_error("n2_inj_l2dir_phys",
		    (void *)n2_inj_l2dir_phys, offset, enable,
		    (uint64_t)n2_debug_buf_pa, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_l2dir_phys FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Corrupt the L2 cache directory parity at physical address.
		 */
#ifdef	L2_DEBUG_BUFFER
		/*
		 * DEBUG - replace data_flag arg with debug buf.
		 *	Requires that L2_DEBUG_BUFFER is also def'd
		 *	in the asm routine to match.
		 */
		n2_debug_init();

		ret = memtest_hv_inject_error("n2_inj_l2dir",
		    (void *)n2_inj_l2dir, paddr, enable,
		    (uint64_t)n2_debug_buf_pa, l2_bank_mask);

		n2_debug_dump();
#else	/* L2_DEBUG_BUFFER */

		ret = memtest_hv_inject_error("n2_inj_l2dir",
		    (void *)n2_inj_l2dir, paddr, enable,
		    (uint64_t)data_flag, l2_bank_mask);

#endif	/* L2_DEBUG_BUFFER */

		/*
		 * For instruction errors, bring instr(s) into the dir
		 * with corrupt parity.  Data was brought in by asm routine.
		 */
		mdatap->m_asmld(mdatap->m_kvaddr_a);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_l2dir FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}
	}
	return (0);
}

/*
 * This routine inserts NotData into the L2 cache data at a location
 * determined by the physical address member of the mdata structure,
 * or for PHYS tests at the byte offset in the iocp->ioc_addr member.
 *
 * This routine is similar to the above n2_inject_l2cache() routine,
 * note however that the xorpat is ignored by the asm routines.
 */
int
n2_inject_l2nd(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		myid = getprocessorid();
	uint64_t	raddr = mdatap->m_raddr_c;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	idx_paddr;
	uint64_t	l2cr_addr;
	uint64_t	l2cr_value;
	uint64_t	l2cr_saved;
	uint64_t	l2_bank_mask;
	int		ret;
	char		*fname = "n2_inject_l2nd";

	DPRINTF(3, "%s: iocp=0x%p, raddr=0x%llx, paddr=0x%llx\n",
	    fname, iocp, raddr, paddr);

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 *
	 * NOTE: the L2 cache asm routines ase a fixed bitmask for the bank
	 *	 (is PA[8:6]) even if some banks are disabled.  This should
	 *	 still work since the HW is supposed to automatically mask
	 *	 the register addresses when in reduced bank modes.
	 */
	l2_bank_mask = n2_check_l2_bank_mode();

	/*
	 * Determine the L2 cache index hashing (IDX) address to use, if
	 * IDX mode is disabled the idx_paddr will match the original paddr.
	 */
	(void) n2_check_l2_idx_mode(paddr, &idx_paddr);

	/*
	 * First check for the PHYS version of the test, if so do not
	 * change any cache/reg settings at all.
	 */
	if (ERR_MISC_ISPHYS(iocp->ioc_command)) {
		/*
		 * Install NotData into L2 cache at offset.
		 */
		ret = memtest_hv_inject_error("n2_inj_l2nd_phys",
		    (void *)n2_inj_l2nd_phys, offset, IOC_XORPAT(iocp),
		    (uint64_t)n2_debug_buf_pa, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_l2nd_phys "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
		return (0);
	}

	/*
	 * Determine addr and read contents of the L2 control register.
	 */
	l2cr_addr = N2_L2_CTL_REG + (paddr & l2_bank_mask);
	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	l2cr_saved = l2cr_value;

	/*
	 * Set the L2$ into DM mode prior to the call to HV so the
	 * data/instructions can be installed into L2$ by this routine
	 * and the line changed to modified if req'd.
	 */
	l2cr_value |= N2_L2CR_DMMODE;
	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, l2cr_addr, l2cr_value, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 for DM"
		    " mode FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	l2cr_value = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, l2cr_addr, NULL, NULL, NULL);
	DPRINTF(3, "%s: L2CR(at 0x%lx) set to = 0x%lx\n",
	    fname, l2cr_addr, l2cr_value);

	/*
	 * Place the data in the cache into the modified (M) state for all
	 * non-instruction L2 errors (this is required for write-back and
	 * scrub errors, and is preferable for normal L2$ errors).
	 * Also honor the user options which can force a cacheline state.
	 */
	if ((ERR_ACC_ISFETCH(iocp->ioc_command) ||
	    ERR_ACC_ISPFETCH(iocp->ioc_command)) &&
	    !ERR_CLASS_ISL2WB(iocp->ioc_command) &&
	    !F_CACHE_DIRTY(iocp)) {
		DPRINTF(1, "%s: not changing L2 cache line state "
		    "to modified due to test definition\n", fname);
	} else if (!F_CACHE_CLN(iocp)) {
		stphys(raddr, ldphys(raddr));
	} else {
		DPRINTF(1, "%s: not changing L2 cache line state "
		    "to modified due to user option\n", fname);
	}

	/*
	 * Instruction and data accesses are treated separately because the
	 * required L1 cache flushing is dependant on the access type.
	 */
	if (ERR_ACC_ISFETCH(iocp->ioc_command)) {

		/*
		 * If this is a kernel test cause target instruction(s) to
		 * be loaded into caches.  User code has already been loaded.
		 */
		if (ERR_MODE_ISKERN(iocp->ioc_command)) {
			if (ERR_MISC_ISTL1(iocp->ioc_command)) {
				xt_one(myid, (xcfunc_t *)(mdatap->m_asmld_tl1),
				    (uint64_t)mdatap->m_kvaddr_c, (uint64_t)0);
			} else if (ERR_MISC_ISPCR(iocp->ioc_command)) {
				(mdatap->m_pcrel)();
			} else {
				(mdatap->m_asmld)(mdatap->m_kvaddr_c);
			}
		}

		/*
		 * Install NotData into L2 cache instruction data.
		 */
		ret = memtest_hv_inject_error("n2_inj_l2nd_instr",
		    (void *)n2_inj_l2nd_instr, paddr,
		    idx_paddr, (uint64_t)n2_debug_buf_pa, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_l2nd_instr "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {	/* Data tests */
		/*
		 * Install NotData into L2 cache data.
		 */
#ifdef	L2_DEBUG_BUFFER
		n2_debug_init();
#endif
		ret = memtest_hv_inject_error("n2_inj_l2nd",
		    (void *)n2_inj_l2nd, paddr,
		    idx_paddr, (uint64_t)n2_debug_buf_pa, NULL);

#ifdef	L2_DEBUG_BUFFER
		n2_debug_dump();
#endif
		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_l2nd FAILED, "
			    "ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	/*
	 * Return the L2 cache bank to it's previous state.
	 */
	if (l2cr_value != l2cr_saved) {
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, l2cr_addr,
		    l2cr_saved, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 disable DM mode "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	return (0);
}

/*
 * This routine inserts an error into the L2 cache data, the L2 cache tags,
 * or the ECC protecting one of the data or the tags in a location specified
 * by the byte offset in the ioc_addr member of the ioc struct.
 *
 * This routine is similar to the above n2_inject_l2cache() routine.
 */
int
n2_inject_l2phys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	int		ret;
	char		*fname = "n2_inject_l2phys";

	DPRINTF(3, "%s: iocp=0x%p, offset=0x%llx\n", fname, iocp, offset);

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 *
	 * NOTE: the L2 cache asm routines ase a fixed bitmask for the bank
	 *	 (is PA[8:6]) even if some banks are disabled.  This should
	 *	 still work since the HW is supposed to automatically mask
	 *	 the register addresses when in reduced bank modes.
	 *
	 *	 So this routine is not checking for a reduced bank mode.
	 */

	/*
	 * If the IOCTL specified the tag, corrupt the tag or the tag ECC.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("n2_inj_l2phys_tag",
		    (void *)n2_inj_l2phys_tag, offset,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)n2_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_l2phys_tag "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Otherwise corrupt the L2 cache data or the data ECC.
		 */
		ret = memtest_hv_inject_error("n2_inj_l2phys_data",
		    (void *)n2_inj_l2phys_data, offset,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)n2_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_l2phys_data "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}
	return (0);
}

/*
 * This routine inserts an error into the L2 cache VUAD or the VUAD parity
 * bits at a location determined by the physical address member of the mdata
 * structure.
 *
 * The PHYS version of the test expects a byte offset in the iocp->ioc_addr
 * struct member instead of an address.
 *
 * Because the VAD bits of all 16 ways are checked for each L2 cache access
 * these errors can be detected even without the explicit access by the driver.
 *
 * Valid xorpat values are combinations of:
 * 	VD ECC:	[38:32] (computed across both V and D)
 * 	Valid:	[31:16]
 * 	Dirty:	[15:0]
 * 	---
 * 	UA ECC:	[38:32] (computed across both U and A)
 * 	Used:	[31:16] (may not produce an error)
 * 	Alloc:	[15:0]
 */
int
n2_inject_l2vad(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	idx_paddr;
	uint64_t	VDflag = 0;
	int		ret;
	char		*fname = "n2_inject_l2vad";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 *
	 * NOTE: the L2 cache asm routines ase a fixed bitmask for the bank
	 *	 (is PA[8:6]) even if some banks are disabled.  This should
	 *	 still work since the HW is supposed to automatically mask
	 *	 the register addresses when in reduced bank modes.
	 *
	 *	 So this routine is not checking for a reduced bank mode.
	 */

	/*
	 * Determine the L2 cache index hashing (IDX) address to use, if
	 * IDX mode is disabled the idx_paddr will match the original paddr.
	 */
	(void) n2_check_l2_idx_mode(paddr, &idx_paddr);

	/*
	 * If the IOCTL specified VD, corrupt the VD or the VD parity.
	 */
	if (ERR_SUBCLASS_ISVD(iocp->ioc_command))
		VDflag = 0x400000;

	/*
	 * First check for the PHYS version of the test.
	 */
	if (ERR_MISC_ISPHYS(iocp->ioc_command)) {
		/*
		 * Corrupt the L2 cache VAD or the VAD parity.
		 */
		ret = memtest_hv_inject_error("n2_inj_l2vad_phys",
		    (void *)n2_inj_l2vad_phys, offset, IOC_XORPAT(iocp),
		    VDflag, (uint64_t)n2_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_l2vad_phys "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
		return (0);
	}

	/*
	 * Instruction and data accesses are treated separately because the
	 * required L1 cache flushing is dependant on the access type.
	 */
	if (ERR_ACC_ISFETCH(iocp->ioc_command)) {
		/*
		 * Corrupt the L2 cache VAD or the VAD parity.
		 *
		 * Bring the instructions into the cache for the corruption.
		 */
		mdatap->m_asmld(mdatap->m_kvaddr_a);

		ret = memtest_hv_inject_error("n2_inj_l2vad_instr",
		    (void *)n2_inj_l2vad_instr, paddr,
		    IOC_XORPAT(iocp), VDflag, idx_paddr);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_l2vad_instr "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Corrupt the L2 cache VAD or the VAD parity.
		 */
		ret = memtest_hv_inject_error("n2_inj_l2vad",
		    (void *)n2_inj_l2vad, paddr,
		    IOC_XORPAT(iocp), VDflag, idx_paddr);

		if (ret == -1) {
			DPRINTF(0, "%s: n2_inj_l2cache_vad "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}
	return (0);
}

/*
 * This routine inserts a parity error into Modular Arithmetic memory (MAMEM)
 * using the specified real address as the data/operands to be loaded into MA.
 *
 * NOTE: loads to MA do not generate an error.
 *
 * NOTE: similar to Niagara-I the n2_inj_mamem() routine currently uses a
 *	 value of 3 8-byte words as the operand/result length with a spacing
 *	 in memory of 4 8-byte words between them.
 */
int
n2_inject_mamem(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	operation;
	caddr_t		buffer;
	int		ret;
	char		*fname = "n2_inject_mamem";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	/*
	 * Determine the type of operation to use for error injection, note
	 * that a STORE will not inject an error according to PRM definition.
	 *
	 * XXX	find a way to allow the different types of operations
	 *	(such as mult) to be used for the injection. The access
	 *	field in the command is for the access NOT the injection.
	 */
	operation = N2_MA_OP_LOAD;

	/*
	 * Preload the memory buffer for load and mult operations assuming
	 * address offsets of 4 8-byte words (equals 0x20 bytes).  These
	 * values will be used for the ma access.
	 *
	 * XXX	perhaps make addr reg offset settings via an inpar.
	 * 	Which would allow the values to be read from the
	 * 	existing MA regs (must check if they are valid first).
	 *
	 *	   OP =	Reduc	Mult		Exp
	 *	   ------------------------------------
	 * 	ADDR0 = source	multiplier 	base
	 * 	ADDR1 = modulus	multiplicand	temp
	 * 	ADDR2 = result	modulus		modulus
	 * 	ADDR3 = -	temp		result
	 * 	ADDR4 = -	result		exp
	 * 	ADDR5 = -	-		exp sz
	 *
	 * NOTE: The paddr passed in MUST be for mdatap->m_databuf plus
	 *	 optional offset for the exact location to corrupt.
	 */
	buffer = mdatap->m_databuf;
	*((uint32_t *)buffer + 0x00) = 0x12345678;
	*((uint32_t *)buffer + 0x20) = 0x87654321;
	*((uint32_t *)buffer + 0x40) = 0x47;

	/*
	 * Inject the parity error at location = buffer + offset.
	 */
	ret = memtest_hv_inject_error("n2_inj_mamem", (void *)n2_inj_mamem,
	    paddr, operation, IOC_XORPAT(iocp), NULL);
	if (ret == -1) {
		DPRINTF(0, "%s: MA parity inject "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine inserts an error into main memory (DRAM) at a location
 * determined by the physical address member of the mdata struct.  The
 * physical address must be in local memory.
 *
 * The DRAM uses QEC/OED ECC.  This means that any number of errors within
 * a single nibble are correctable, and any errors (up to 8) within two nibbles
 * are detectable as uncorrectable.
 *
 * This routine has to determine if the DRAM bank that will be used for the
 * injection is enabled or disabled, if it is disabled then the active bank
 * that is handling the address range must be used.  There are only three
 * valid configurations on Niagara-II since the L2 banks and the DRAM banks
 * are hard coded to each other.
 *
 * 	4 DRAM banks, 8 L2 banks, 8 cores (fully enabled)
 * 	2 DRAM banks, 4 L2 banks, 4 cores (half enabled)
 * 	1 DRAM banks, 2 L2 banks, 2 cores (quarter enabled)
 *
 * Valid xorpat values for the DRAM errors are:
 * 	ECC mask: [15:0]
 */
int
n2_inject_memory(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	eccmask, l2_bank_mask;
	uint64_t	dram_bank_offset;
	int		ret;
	char		*fname = "n2_inject_memory";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx xor=0x%x\n",
	    fname, iocp, paddr, IOC_XORPAT(iocp));

	/*
	 * Get the mask to use for the ECC bit corruption.
	 */
	eccmask = IOC_XORPAT(iocp);
	eccmask |= (N2_DRAM_INJECTION_ENABLE | N2_DRAM_SSHOT_ENABLE);

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode.
	 */
	if (F_INF_INJECT(iocp))
		eccmask &= ~((uint64_t)N2_DRAM_SSHOT_ENABLE);

	/*
	 * Determine the DRAM bank offset.
	 */
	dram_bank_offset = memtest_get_dram_bank_offset(mdatap);

	/*
	 * Flush the L2$ to ensure data lookup does not hit prior to setting
	 * cache into DM mode.
	 *
	 * NOTE: this may flush existing errors from L2 producing write-back
	 *	 error(s) before the DRAM error is even injected.
	 */
	if (!F_FLUSH_DIS(iocp)) {
		(void) OP_FLUSHALL_L2_HVMODE(mdatap);
	}

#ifdef	MEM_DEBUG_BUFFER
	n2_debug_init();

	ret = memtest_hv_inject_error("n2_inj_memory_debug",
	    (void *)n2_inj_memory_debug, paddr, eccmask, dram_bank_offset,
	    (uint64_t)n2_debug_buf_pa);

	n2_debug_dump();
#else	/* MEM_DEBUG_BUFFER */

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 */
	l2_bank_mask = n2_check_l2_bank_mode();
	DPRINTF(2, "%s: the L2 bank mask is being set to %lx\n",
	    fname, l2_bank_mask);

	ret = memtest_hv_inject_error("n2_inj_memory", (void *)n2_inj_memory,
	    paddr, eccmask, dram_bank_offset, l2_bank_mask);
#endif	/* MEM_DEBUG_BUFFER */

	if (ret == -1) {
		DPRINTF(0, "%s: n2_inj_memory FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	DPRINTF(3, "%s: ret = 0x%x\n", fname, ret);
	return (0);
}

/*
 * This routine inserts an error into the internal Store Buffer data or tag
 * for either privileged (kernel) or non-privileged (user) mode.
 *
 * The data is protected by 6-bit ECC and the tag (CAM) is protected by
 * parity.  The injection mode simply flips the parity bit for the stores
 * that occur while the enable bit is set.
 *
 * The different operations that make use of the Store Buffer are:
 *	- data loads to check for RAW (Read After Write) hits
 *	- PCX reads (crossbar between the processor and the external arrays)
 *	- ASI Ring stores (ASI stores that go over the ASI ring)
 *	- NOTE that diagnostic reads will not invoke ecc or parity errors
 *
 * Although this routine contains an access section it is not expected that
 * kernel (or user) mode errors will be possible to inject and invoke due
 * to the small size of the store buffer and the number of reads required
 * to exit hyperpriv mode (to perform the injection).
 *
 * NOTE: Niagara-II does not check tag (CAM) parity on normal load accesses.
 *
 * NOTE: the specific ASI used for the ASI commands is the third SCRATCHPAD
 *	 register since it is available to both priv and hyperpriv modes.
 *
 * NOTE: this routine is ignoring the access addresses in the mdatap struct
 *	 and only uses the corruption addresses for both corruption and access.
 */
int
n2_inject_sb(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	raddr = mdatap->m_raddr_c;
	caddr_t		vaddr = mdatap->m_kvaddr_c;
	uint64_t	asi_kvaddr = ASI_SCRATCHPAD;
	uint64_t	asi_hvaddr = ASI_HSCRATCHPAD;
	uint64_t	vaddr_for_asi = HSCRATCH2; /* third scratchpad reg */
	uint64_t	eccmask = IOC_XORPAT(iocp);
	uint64_t	enable;
	uint64_t	count = 1;
	uint64_t	ret;
	char		*fname = "n2_inject_sb";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	/*
	 * Set appropriate fields to use in error injection register.
	 */
	enable = (REG_INJECTION_ENABLE | REG_SSHOT_ENABLE);

	/*
	 * Inject error into either the data or the tag.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {
		enable |= REG_STA_ENABLE;
	} else {
		enable |= REG_STD_ENABLE;
	}

	/*
	 * Do not invoke the error in the asm routine if the access is to be
	 * done in kernel mode (access is performed below for KERN).
	 *
	 * If the IOCTL has the infinite injection flag set, perform multiple
	 * injections (to produce errors with the ME bit set).  Since the trap
	 * handler will disable the injection register, there is no real way
	 * to do infinite injection.  Also the count variable only has
	 * three different modes:
	 *		count = 0:	inject one error, but do not invoke it
	 *		count = 1:	inject one error, then invoke it
	 *		count > 1:	inject four errors, then invoke them
	 */
	if (F_NOERR(iocp) || ERR_MODE_ISKERN(iocp->ioc_command))
		count = 0;

	if (F_INF_INJECT(iocp))
		count = 4;

	/*
	 * Normal memory store buffer UE errors will also produce a
	 * data_access_exception if the data is not in the D$ when the
	 * load is replayed.  This can be surpressed if the caches are
	 * not flushed prior to injection, however the asm routine may
	 * bring the line back into the caches.
	 */
	if (!F_FLUSH_DIS(mdatap->m_iocp))
		OP_FLUSHALL_CACHES(mdatap);

	/*
	 * Determine which type of Store Buffer error to inject/invoke and
	 * call the appropriate asm level routine.
	 */
	if (ERR_MODE_ISDMA(iocp->ioc_command)) {
		/*
		 * Inject an error via an IO address store.
		 * First access the IOMMU address to install iotte.
		 */
		if (CPU_ISKT(mdatap->m_cip)) {
			ret = peek_asi64(ASI_REAL_IO, raddr);
			ret = memtest_hv_inject_error("kt_inj_sb_io",
			    (void *)kt_inj_sb_io, raddr, enable, eccmask,
			    count);
		} else {
			ret = peek_asi64(ASI_REAL_IO, raddr);
			ret = memtest_hv_inject_error("n2_inj_sb_io",
			    (void *)n2_inj_sb_io, raddr, enable, eccmask,
			    count);
		}

	} else if (ERR_ACC_ISASI(iocp->ioc_command)) {
		/*
		 * Inject an error via an ASI address store.
		 *
		 * NOTE: can enhance asi routine by using a misc arg to choose
		 *	 the asi value so that different registers can be used.
		 */
		ret = memtest_hv_inject_error("n2_inj_sb_asi",
		    (void *)n2_inj_sb_asi, asi_hvaddr, enable,
		    eccmask, vaddr_for_asi);
	} else if (ERR_ACC_ISPCX(iocp->ioc_command)) {
		/*
		 * Inject an error via a normal (non-asi) store and access
		 * using a PCX read (via a membar #Sync).
		 */
		ret = memtest_hv_inject_error("n2_inj_sb_pcx",
		    (void *)n2_inj_sb_pcx, paddr, enable, eccmask, count);
	} else {
		/*
		 * Inject an error via a normal (non-asi) store and access
		 * using a normal load.
		 */
		ret = memtest_hv_inject_error("n2_inj_sb_load",
		    (void *)n2_inj_sb_load, paddr, enable, eccmask, count);
	}

	/*
	 * Invoke the errors here for kernel mode commands.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (0);
	}

	/*
	 * This access section is in the same order as the above
	 * injection section.
	 *
	 * XXX	right now there is an issue with the DMA (IO) tests, since
	 *	they are defined as DMA and not KERN they will have an
	 *	access done both in the asm routine and later here.  I think
	 *	a new kernel mode IO buffer needs to be available, and we
	 *	need DMA, KDMA, and HDMA test types.  An RFE will be opened
	 *	to address this lack of functionality.
	 */
	if (ERR_MODE_ISDMA(iocp->ioc_command)) {
		ret = peek_asi64(ASI_REAL_IO, raddr);
	} else if (ERR_ACC_ISASI(iocp->ioc_command)) {
		poke_asi64(asi_kvaddr, vaddr_for_asi, 0xeccbeef);
	} else if (ERR_ACC_ISPCX(iocp->ioc_command)) {
		membar_sync();
	} else {
		ret = *vaddr;
	}

	DPRINTF(3, "%s: ret = 0x%x\n", fname, ret);
	return (0);
}

/*
 * This routine inserts an error into the System-on-Chip (SOC) subsystem of
 * the Niagara-II processor via the HW provided error injection register
 * which allows for all detectable SOC error types to be injected.
 *
 * The bit to set in the injection register is set in the command definition
 * (in userland) in order to keep this routine simple.
 *
 * NOTE: most SOC Internal error types are detectable via DMA access, the
 *	 only exceptions to this are the DMU internal, and DMU credit
 *	 parity errors which do not require an explicit access, and all of
 *	 the NIU tests (there are seven of these with "niu" in the name).
 *
 * NOTE: all error types require actual traffic pass through the SOC and
 *	 therefore the NIU errors will require that a network card (such as
 *	 the XAUI) is installed in the system with a working driver.  Right
 *	 now the EI does not send any explcit traffic over this interface
 *	 (since I have no idea how to do it and don't have access to a card).
 */
int
n2_inject_soc_int(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	raddr = mdatap->m_raddr_c;
	uint64_t	iodata_val;
	char		*fname = "n2_inject_soc_int";

	DPRINTF(3, "%s: iocp=0x%p, raddr=0x%llx\n", fname, iocp, raddr);

	/*
	 * Access the IO address to read the data at that location.
	 */
	if (!F_NOERR(iocp)) {
		iodata_val = peek_asi64(ASI_REAL_IO, raddr);
	}

	/*
	 * Inject the specified error simply by setting the appropriate bit
	 * in the SOC error injection register.  This routine expects that
	 * enough background activity is occuring on the PIU and NIU
	 * interfaces for errors to be injected and detected.
	 */
	(void) memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, N2_SOC_ERR_INJ_REG,
	    IOC_XORPAT(iocp), (uint64_t)n2_debug_buf_pa, NULL);

	/*
	 * If we do not want to invoke the error then return now,
	 * otherwise invoke the error for the types that require it.
	 * Also clear the injection register before exit unless "infinite"
	 * injection mode specified.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		if (!F_INF_INJECT(iocp)) {
			DELAY(iocp->ioc_delay * MICROSEC);
			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, N2_SOC_ERR_INJ_REG,
			    0x0, (uint64_t)n2_debug_buf_pa, NULL);
		}
		return (0);
	}

	/*
	 * If the command specified PIO access or the user specified
	 * PIO access via the MISC1 option then use PIO access to invoke
	 * the error, however if MISC2 (only) is set then force DMA access
	 * by clearing the PIO bit from the command.
	 *
	 * If PIO access is to be used then the access is done in this
	 * routine, otherwise for DMA access the userland routine will
	 * access the address to invoke the error via an msync command.
	 */
	if (F_MISC1(iocp)) {
		iocp->ioc_command |= MPIO;
	} else if (F_MISC2(iocp)) {
		iocp->ioc_command &= ~(MPIO);
	}

	if (ERR_MISC_ISPIO(iocp->ioc_command)) {
		/*
		 * Set the NOERR flag so that the caller does not
		 * perform another access after the access here.
		 */
		IOC_FLAGS(iocp) |= FLAGS_NOERR;

		/*
		 * A few NCU error types are detectable via PIO store access.
		 * However all SOC error will be allowed to fall through here,
		 * and only a warning message will be printed if unsupported.
		 * The same method is used for PIO load accesses.
		 */
		if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
			if (!(SOC_SUBCLASS_ISNCUD(iocp->ioc_command) ||
			    SOC_SUBCLASS_ISNCUC(iocp->ioc_command) ||
			    SOC_SUBCLASS_ISNCUP(iocp->ioc_command))) {
				cmn_err(CE_WARN, "%s: using a PIO store access"
				    " for non-PIO error type\n", fname);
			}
			poke_asi64(ASI_REAL_IO, raddr, iodata_val);

		} else {	/* assume a LOAD access */
			if (!(SOC_SUBCLASS_ISNCU(iocp->ioc_command) ||
			    SOC_SUBCLASS_ISSIID(iocp->ioc_command))) {
				cmn_err(CE_WARN, "%s: using a PIO load access"
				    " for non-PIO error type\n", fname);
			}
			(void) peek_asi64(ASI_REAL_IO, raddr);
		}
	}

	/*
	 * Clear the SOC error injection register since it defaults to
	 * continuous injection mode unless infinite injection mode specified.
	 *
	 * NOTE: the injection is bit is being left on for a finite amount
	 *	 of time.  Even if the user sets the delay to zero an
	 *	 error may occur since the above code takes time to execute.
	 *	 However there is no way as yet to ensure a single error
	 *	 because the handler and the injector are in very different
	 *	 chunks of code.
	 */
	if (!F_INF_INJECT(iocp)) {
		DELAY(iocp->ioc_delay * MICROSEC);
		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, N2_SOC_ERR_INJ_REG,
		    0x0, (uint64_t)n2_debug_buf_pa, NULL);
	}

	return (0);
}

/*
 * This routine inserts an error into one of the System-on-Chip (SOC) MCU
 * error types of the Niagara-II processor via the HW provided error
 * injection register which allows for all detectable SOC error types to
 * be injected.
 *
 * The bit to set in the injection register is set in the command definition
 * (in userland) in order to keep this routine simple.
 *
 * NOTE: The specified branches error inject bit in the SOC error inject
 *	 register is used even if that branch is disabled, however the
 *	 fallback (enabled) branches DRAM registers are updated.
 *
 * NOTE: The FBU and FBR errors require that the status regs are cleared
 *	 before the next error will be detected, so even if "infinite" mode
 *	 is set below, it is dependent on the error hanlding if multiple
 *	 errors will be detected and reported.
 */
int
n2_inject_soc_mcu(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		err_src;
	uint64_t	fbr_cnt_reg_val;
	uint64_t	dram_bank_offset;
	uint64_t	num_dram_branches, dram_branch_inc;
	uint64_t	ret = 0;
	uint64_t	i, read_val, offset;
	char		*fname = "n2_inject_soc_mcu";

	DPRINTF(3, "%s: iocp=0x%p\n", fname, iocp);

	/*
	 * Determine the DRAM bank offset.
	 */
	dram_bank_offset = memtest_get_dram_bank_offset(mdatap);

	/*
	 * Prior to setting the injection register, set the associated
	 * FBDIMM error source register to specify the type of error
	 * that was detected from the following values:
	 *	0 -> CRC error
	 *	1 -> Alert frame error
	 *	2 -> Alert asserted
	 *	3 -> Status frame parity error
	 *
	 * A MISC1 argument to this function will determine which error
	 * source is used for the injection.
	 */
	err_src = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 0);
	if ((err_src < 0) || (err_src > 3)) {
		DPRINTF(0, "%s: invalid misc argument using default %d\n",
		    fname, 0);
		err_src = 0;
	}

	(void) memtest_hv_util("hv_paddr_store64", (void *)hv_paddr_store64,
	    N2_DRAM_CSR_BASE + N2_DRAM_FBD_INJ_ERROR_SRC_REG +
	    dram_bank_offset, err_src, (uint64_t)n2_debug_buf_pa, NULL);

	/*
	 * If MISC2 is set then set the FBDIMM recoverable error count reg
	 * to send an error indication on every FBR error (do not count).
	 */
	fbr_cnt_reg_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_DRAM_CSR_BASE +
	    N2_DRAM_FBR_COUNT_REG + dram_bank_offset,
	    NULL, NULL, NULL);

	if (F_MISC2(iocp)) {
		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, N2_DRAM_CSR_BASE +
		    N2_DRAM_FBR_COUNT_REG + dram_bank_offset,
		    N2_DRAM_FBR_COUNT_DISABLE,
		    (uint64_t)n2_debug_buf_pa, NULL);
	}

	/*
	 * If debug set print out the contents of all of the count regs.
	 * Note that VF has only two regs while N2 has four.
	 */
	DPRINTF(3, "%s: FBR_COUNT_REG 0x%08x.%08x with contents 0x%08x.%08x\n",
	    fname, PRTF_64_TO_32(N2_DRAM_CSR_BASE + N2_DRAM_FBR_COUNT_REG +
	    dram_bank_offset), PRTF_64_TO_32(fbr_cnt_reg_val));

	if (CPU_ISVFALLS(mdatap->m_cip)) {
		num_dram_branches = VF_NUM_DRAM_BRANCHES;
		dram_branch_inc = VF_DRAM_BRANCH_OFFSET;
	} else {
		num_dram_branches = N2_NUM_DRAM_BRANCHES;
		dram_branch_inc = N2_DRAM_BRANCH_OFFSET;
	}

	for (i = 0, offset = 0; i < num_dram_branches; i++,
	    offset += dram_branch_inc) {

		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, N2_DRAM_CSR_BASE +
		    N2_DRAM_FBR_COUNT_REG + offset,
		    NULL, NULL, NULL);

		DPRINTF(3, "%s: FBR_COUNT_REG %d = 0x%08x.%08x "
		    "contents = 0x%08x.%08x\n",
		    fname, i, PRTF_64_TO_32(N2_DRAM_CSR_BASE +
		    N2_DRAM_FBR_COUNT_REG + offset),
		    PRTF_64_TO_32(read_val));
	}

	(void) memtest_hv_inject_error("n2_inj_soc_mcu",
	    (void *)n2_inj_soc_mcu, mdatap->m_paddr_a,
	    N2_SOC_ERR_INJ_REG, IOC_XORPAT(iocp), 0);

	/*
	 * Restore the FBR count register before exit.
	 */
	if (F_MISC2(iocp)) {
		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, N2_DRAM_CSR_BASE +
		    N2_DRAM_FBR_COUNT_REG + dram_bank_offset,
		    fbr_cnt_reg_val, (uint64_t)n2_debug_buf_pa, NULL);
	}

	return (ret);
}

/*
 * This routine inserts an error into the data or instruction TLB data or
 * tag for either privileged (kernel) or non-privileged (user) mode.
 *
 * The TLBs are protected by parity and the injection mode simply flips the
 * parity bit for the next (or all subsequent) entry load(s) for either the
 * tag or the data.
 *
 * The priority of DTLB errors is DTTM -> DTTP -> DTDP -> DTMU.
 *
 * The priority of ITLB errors is ITTM -> ITTP -> ITDP -> ITMU.
 *
 * NOTE: Niagara-II exclusively uses the sun4v TTE format, unlike Niagara-I
 *	 which could use either format (using internal translations).
 *
 * NOTE: TLB data and CAM (tag) errors are not detected via an ASI load access
 *	 as was the case on Niagara-I.  These now use a normal translation.
 *
 * NOTE: the USER TLB tests which are triggered via a normal (userland) access
 *	 are defined as not implemented b/c they were found to be unreliable.
 */
int
n2_inject_tlb(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	vaddr;
	uint64_t	enable;
	uint64_t	offset;
	struct hat 	*sfmmup;
	uint32_t	ctxnum;
	uint32_t	uval32;
	tte_t		tte;
	int		i, count = 1;
	uint64_t	ret;
	char		*fname = "n2_inject_tlb";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	/*
	 * Set appropriate fields to use in error injection register.
	 */
	enable = (REG_INJECTION_ENABLE | REG_SSHOT_ENABLE);

	/*
	 * Inject error into either the iTLB or the dTLB.
	 */
	if (ERR_CLASS_ISITLB(iocp->ioc_command)) {
		if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {
			enable |= REG_IMT_ENABLE;
		} else if (ERR_SUBCLASS_ISDATA(iocp->ioc_command)) {
			enable |= REG_IMD_ENABLE;
		} else if (ERR_SUBCLASS_ISMMU(iocp->ioc_command)) {
			/*
			 * For the MMU register error, inject into MRA.
			 */
			enable |= REG_MRA_ENABLE;
			offset = MMU_ZERO_CTX_TSB_CFG_0;
		} else {
			/*
			 * Tag Multi-hit, no error bit or offset required.
			 */
			enable = 0;
			offset = 0;
		}
	} else {
		if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {
			enable |= REG_DMT_ENABLE;
		} else if (ERR_SUBCLASS_ISDATA(iocp->ioc_command)) {
			enable |= REG_DMD_ENABLE;
		} else if (ERR_SUBCLASS_ISMMU(iocp->ioc_command)) {
			/*
			 * Same as the ITLB version above.
			 */
			enable |= REG_MRA_ENABLE;
			offset = MMU_ZERO_CTX_TSB_CFG_0;
		} else {
			/*
			 * Same as the ITLB version above.
			 */
			enable = 0;
			offset = 0;
		}
	}

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode.
	 */
	if (F_INF_INJECT(iocp))
		enable &= ~((uint64_t)REG_SSHOT_ENABLE);
	else {
		/*
		 * If a count supplied, inject multiple tlb errors.
		 */
		count = (F_I_COUNT(iocp) ? (iocp->ioc_i_count) : 1);
		DPRINTF(3, "%s: error inject count=0x%lx\n", fname, count);
	}

	/*
	 * The user level tests require the context for the TTE entry,
	 * this can be found in the global hat structure (KCONTEXT = 0).
	 */
	if (ERR_MODE_ISKERN(iocp->ioc_command) ||
	    ERR_MODE_ISHYPR(iocp->ioc_command)) {
		vaddr = (uint64_t)mdatap->m_kvaddr_c;
		sfmmup = ksfmmup;
		ctxnum = KCONTEXT;
	} else {	/* USER */
		vaddr = (uint64_t)mdatap->m_uvaddr_c;
		sfmmup = curproc->p_as->a_hat;
		ctxnum = sfmmup->sfmmu_ctxs[CPU_MMU_IDX(CPU)].cnum;

		/*
		 * Touch the user vaddr from driver via special call to make
		 * sure the TTE is actually in the TLB before it is accessed.
		 */
		uval32 = memtest_get_uval(vaddr);
		DPRINTF(3, "%s: uvaddr=0x%llx, uval32=0x%llx\n", fname,
		    vaddr, uval32);
	}

	/*
	 * Align the vaddr to a pagesize boundary,
	 * this code expects that the PAGESIZE is 8k, and note
	 * that the kernel defines MMU_PAGEOFFSET as a mask.
	 */
	vaddr = (vaddr & (uint64_t)(~MMU_PAGEOFFSET));

	/*
	 * Use kernel tte building function to allow for a differnt
	 * method of injecting the TTE error.
	 *
	 * NOTE: when the debug level is set appropriately to print out
	 *	 the tte often there is no actual error due to print
	 *	 latency issues and a non quieced system interferring
	 *	 with the size limited TLBs.
	 */
	if (memtest_get_tte(sfmmup, (caddr_t)vaddr, &tte) != 0) {
		DPRINTF(0, "%s: Could not get tte for vaddr=0x%llx\n",
		    fname, vaddr);
		return (-1);
	}

	/*
	 * Swap out the "PA" field in the built TTE since it may
	 * still be an RA (real address) with the physical address.
	 */
	if (CPU_ISKT(mdatap->m_cip)) {
		tte.ll &= ~(KT_TTE4V_PA_MASK);
		tte.ll |= (paddr & KT_TTE4V_PA_MASK);
	} else {
		tte.ll &= ~(N2_TTE4V_PA_MASK);
		tte.ll |= (paddr & N2_TTE4V_PA_MASK);
	}

	DPRINTF(3, "%s: tte=0x%llx, paddr=0x%llx, vaddr=0x%llx\n",
	    fname, tte.ll, paddr, vaddr);

	/*
	 * If the IOCTL specified kernel corruption, use kvaddr translations.
	 * Note that the injection routines for iTLB and dTLB are separate.
	 */
	if (ERR_CLASS_ISITLB(iocp->ioc_command)) {

		/*
		 * If the test specified ORPHAN use the tte that was built
		 * by the above kernel function for the injection.
		 */
		if (ERR_MISC_ISORPHAN(iocp->ioc_command)) {
			if (tte.ll != NULL) {
				ret = memtest_hv_inject_error("n2_inj_itlb_v",
				    (void *)n2_inj_itlb_v, paddr, vaddr,
				    enable, tte.ll);
			} else {
				DPRINTF(0, "%s: memtest_get_tte() returned "
				    "a NULL tte\n");
			}
		} else if (ERR_SUBCLASS_ISMH(iocp->ioc_command)) {
			/*
			 * Multiiple tag hit function.
			 */
			if (tte.ll != NULL) {
#ifdef	L2_DEBUG_BUFFER
				n2_debug_init();
#endif
				ret = memtest_hv_inject_error(
				    "n2_inj_itlb_mult",
				    (void *)n2_inj_itlb_mult, paddr, vaddr,
				    (uint64_t)n2_debug_buf_pa, tte.ll);
#ifdef	L2_DEBUG_BUFFER
				n2_debug_dump();
#endif
			} else {
				DPRINTF(0, "%s: memtest_get_tte() returned a "
				    "NULL tte\n");
			}

			/*
			 * If req'd perform the access right here (near inj).
			 */
			if (F_NOERR(iocp)) {
				DPRINTF(2, "%s: not invoking error\n", fname);
				return (0);
			}

			/*
			 * Otherwise set the NOERR flag so that the caller does
			 * not access the VA again after the access here.
			 */
			IOC_FLAGS(iocp) |= FLAGS_NOERR;

			/*
			 * Invoke error by accessing the original VA which will
			 * hit in the space for which the overlapping ttes
			 * were installed in the TLB.
			 */
			DPRINTF(3, "%s: MH access_va=0x%llx\n", fname,
			    (uint64_t)mdatap->m_kvaddr_a);

			mdatap->m_asmld((caddr_t)mdatap->m_kvaddr_a);

		} else if (ERR_SUBCLASS_ISMMU(iocp->ioc_command)) {
			/*
			 * MMU register error, use MRA inject bit, ensure
			 * that HW tablewalks are enabled correctly,
			 * and inject the error.  The access in caller routine
			 * will invoke the error (likely will be accessed
			 * by another miss before the actual invoke).
			 */
			ret = memtest_hv_inject_error("n2_inj_tlb_mmu",
			    (void *)n2_inj_tlb_mmu, vaddr, enable,
			    (uint64_t)0x0, offset);
		} else {
			/*
			 * Use regular injection routine (build own tte).
			 */
			ret = memtest_hv_inject_error("n2_inj_itlb",
			    (void *)n2_inj_itlb, paddr,
			    vaddr, enable, (uint64_t)ctxnum);

			if (ret == -1) {
				DPRINTF(0, "%s: n2_inj_itlb FAILED, "
				    "ret=0x%x\n", fname, ret);
				return ((int)ret);
			} else {
				DPRINTF(3, "%s: n2_inj_itlb returned value "
				    "0x%llx\n", fname, ret);
			}
		}
	} else {	/* dTLB test */

		/*
		 * If the test specified ORPHAN use the tte that was built
		 * by the above kernel function for the injection.
		 */
		if (ERR_MISC_ISORPHAN(iocp->ioc_command)) {
			if (tte.ll != NULL) {
				ret = memtest_hv_inject_error("n2_inj_dtlb_v",
				    (void *)n2_inj_dtlb_v, paddr, vaddr, enable,
				    tte.ll);
			} else {
				DPRINTF(0, "%s: memtest_get_tte() returned a "
				    "NULL tte\n");
			}
		} else if (ERR_SUBCLASS_ISMH(iocp->ioc_command)) {
			/*
			 * Multiiple tag hit function.
			 */
			if (tte.ll != NULL) {
#ifdef	L2_DEBUG_BUFFER
				n2_debug_init();
#endif
				ret = memtest_hv_inject_error(
				    "n2_inj_dtlb_mult",
				    (void *)n2_inj_dtlb_mult, paddr, vaddr,
				    (uint64_t)n2_debug_buf_pa, tte.ll);
#ifdef	L2_DEBUG_BUFFER
				n2_debug_dump();
#endif
			} else {
				DPRINTF(0, "%s: memtest_get_tte() returned a "
				    "NULL tte\n");
			}

			/*
			 * If req'd perform the access right here (near inj).
			 */
			if (F_NOERR(iocp)) {
				DPRINTF(2, "%s: not invoking error\n", fname);
				return (0);
			}

			/*
			 * Otherwise set the NOERR flag so that the caller does
			 * not access the VA again after the access here.
			 */
			IOC_FLAGS(iocp) |= FLAGS_NOERR;

			/*
			 * Invoke error by accessing the original VA which will
			 * hit in the space for which the overlapping ttes
			 * were installed in the TLB.
			 */
			DPRINTF(3, "%s: MH access_va=0x%llx\n", fname,
			    (uint64_t)mdatap->m_kvaddr_a);

			switch (ERR_ACC(iocp->ioc_command)) {
			case ERR_ACC_LOAD:
				mdatap->m_asmld((caddr_t)mdatap->m_kvaddr_a);
				break;
			case ERR_ACC_STORE:
				*(caddr_t)mdatap->m_kvaddr_a = (uchar_t)0xff;
				membar_sync();
				break;
			default:
				DPRINTF(0, "%s: unknown TLB MH access type! "
				    "Exiting without invoking error.\n", fname);
				return (ENOTSUP);
			}
		} else if (ERR_SUBCLASS_ISMMU(iocp->ioc_command)) {
			/*
			 * MMU register error, same as the above iTLB case.
			 */
			ret = memtest_hv_inject_error("n2_inj_tlb_mmu",
			    (void *)n2_inj_tlb_mmu, vaddr, enable,
			    (uint64_t)0x1, offset);
		} else {
			/*
			 * Otherwise build the tte from scratch.
			 */
			for (i = 0; i < count; i++) {
				ret = memtest_hv_inject_error("n2_inj_dtlb",
				    (void *)n2_inj_dtlb, paddr, vaddr, enable,
				    (uint64_t)ctxnum);

				if (ret == -1) {
					DPRINTF(0, "%s: n2_inj_dtlb FAILED, "
					    "ret=0x%x\n", fname, ret);
					return ((int)ret);
				} else {
					DPRINTF(3, "%s: n2_inj_dtlb returned "
					    "value 0x%llx\n", fname, ret);
				}
			}
		}
	}

	/*
	 * Access certain error types here and if so set the NOERR flag.
	 * Note that the MMU register arry errors always show up as ITMU
	 * if an access is used, and if not they are DTMU (system trips
	 * over them), this is because the access routine (asmld) is
	 * actually in the allocated data buffer.
	 */
	if (ERR_SUBCLASS_ISMMU(iocp->ioc_command)) {
		if (ERR_CLASS_ISDTLB(iocp->ioc_command)) {
			DPRINTF(2, "%s: D-TLB MRA errors do not use an EI "
			    "access\n", fname);
			IOC_FLAGS(iocp) |= FLAGS_NOERR;
		}
	}

	return (0);
}

/*
 * This routine inserts an error into the TSB (data or instruction) in
 * the L2 cache to be detected on a TSB fill (miss).
 *
 * The errors are detected by a HW tablewalk that is triggered by accessing
 * a kvaddr (in priv mode below) which causes a TLB miss, the miss will
 * attempt a lookup in the TSB which will hit the error placed in the TSB
 * entry that corresponds to the kvaddr (the allocated injection buffer).
 *
 * NOTE: this routine does not check or set the MMU Tablewalk Pending Control
 *	 Register, but this should not matter since nothing is being brought
 *	 into the TLB.
 *
 * NOTE: the TSBs contain both data and instruction information, with
 *	 separate pointer regs for data and instruction TSB entries.
 */
int
n2_inject_tlb_l2_miss(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	tsb_base_raddr;
	uint64_t	tsb_size;
	uint64_t	tsbe_tag;
	uint64_t	buf_vaddr;
	volatile caddr_t kvaddr_c, kvaddr_a;
	uint64_t	paddr, idx_paddr;
	uint64_t	raddr;
	uint_t		data_flag = 1;
	uint64_t	l2cr_addr;
	uint64_t	l2cr_value;
	uint64_t	l2cr_saved;
	uint64_t	l2_bank_mask;
	struct hat 	*sfmmup;
	tte_t		tte;
	struct tsbe	*tsbe_raddr;
	struct tsbe 	*(*sfmmu_get_tsbe_func)(uint64_t, caddr_t, int, int);
	void		(*sfmmu_load_tsbe_func)(struct tsbe *, uint64_t, \
	    tte_t *, int);
	uint64_t	ret = 0;
	char		*fname = "n2_inject_tlb_l2_miss";

	/*
	 * Set the kvaddr depending if this is a DTLB or an ITLB test.
	 * This is not done in the pre-test routine for this type of
	 * test because of the way these commands are defined
	 * (access types of DTAC and ITAC instead of LOAD and FETCH).
	 */
	if (ERR_ACC_ISITLB(iocp->ioc_command)) {
		data_flag = 0;
		kvaddr_c = kvaddr_a = (caddr_t)(mdatap->m_asmld);
	} else {	/* Data test */
		kvaddr_c = kvaddr_a = mdatap->m_databuf;
	}

	/*
	 * Keep the buffer address (buf_vaddr) unaligned and as the
	 * address before any offset is applied.  The offset will be
	 * added later after the raddr/paddr is determined.
	 */
	buf_vaddr = (uint64_t)kvaddr_c;
	kvaddr_a += memtest_get_a_offset(iocp);

	DPRINTF(3, "%s: iocp=0x%p, buffer kvaddr_c=0x%llx, buffer kvaddr_a="
	    "0x%llx\n", fname, iocp, kvaddr_c, kvaddr_a);

	/*
	 * Find the correct sfmmu struct and TSB to use.
	 *
	 * NOTE: the kernel MMU code uses tsbinfop = sfmmup->sfmmu_tsb,
	 *	 and that the TSB vaddr is also available:
	 *	 kernel = tsb_base_vaddr = (uint64_t)ktsb_base;
	 *	 user = tsb_base_vaddr = (uint64_t)sfmmup->sfmmu_tsb->tsb_va;
	 */
	if (ERR_MODE_ISKERN(iocp->ioc_command) ||
	    ERR_MODE_ISHYPR(iocp->ioc_command)) {
		sfmmup = ksfmmup;
		tsb_base_raddr = (uint64_t)ktsb_pbase;
		tsb_size = ktsb_szcode;
	} else {	/* USER */
		sfmmup = curproc->p_as->a_hat;
		tsb_base_raddr = (uint64_t)sfmmup->sfmmu_tsb->tsb_pa;
		tsb_size = sfmmup->sfmmu_tsb->tsb_szc;
	}

	DPRINTF(3, "%s: TSB base raddr=0x%llx, TSB size=0x%llx\n", fname,
	    tsb_base_raddr, tsb_size);

	/*
	 * Use the following method to access private (static) MMU
	 * routines from here to reduce code duplication.  The routines
	 * that are needed for this type of injection are:
	 *	sfmmu_get_tsbe(uint64_t tsbeptr, caddr_t vaddr, int vpshift,
	 *		int tsb_szc);
	 *	sfmmu_load_tsbe(struct tsbe *tsbep, uint64_t vaddr,
	 *		tte_t *ttep, int phys);
	 */
	sfmmu_get_tsbe_func = (struct tsbe *(*)(uint64_t, caddr_t, int, int))
	    kobj_getsymvalue("sfmmu_get_tsbe", 1);

	if (sfmmu_get_tsbe_func == NULL) {
		DPRINTF(0, "%s: kobj_getsymvalue for sfmmu_get_tsbe "
		    "FAILED!\n", fname);
		return (-1);
	}

	sfmmu_load_tsbe_func = (void(*)(struct tsbe *, uint64_t, tte_t *, int))
	    kobj_getsymvalue("sfmmu_load_tsbe", 1);

	if (sfmmu_load_tsbe_func == NULL) {
		DPRINTF(0, "%s: kobj_getsymvalue for sfmmu_load_tsbe "
		    "FAILED!\n", fname);
		return (-1);
	}

	/*
	 * Find the raddr of the actual TTE in the TSB that corresponds
	 * to our buffer address.
	 */
	if (memtest_get_tte(sfmmup, (caddr_t)buf_vaddr, &tte) != 0) {
		DPRINTF(0, "%s: Could not get tte for buffer vaddr=0x%llx\n",
		    fname, buf_vaddr);
		return (-1);
	}

	DPRINTF(3, "%s: tte=0x%llx, buffer vaddr=0x%llx\n", fname, tte.ll,
	    buf_vaddr);

	tsbe_raddr = sfmmu_get_tsbe_func(tsb_base_raddr, (caddr_t)buf_vaddr,
	    (int)MMU_PAGESHIFT, tsb_size);

	tsbe_tag = buf_vaddr >> TTARGET_VA_SHIFT;
	DPRINTF(3, "%s: sfmmu_get_tsbe_func() tsbe tag=0x%llx, "
	    "tsbe raddr=0x%llx\n", fname, tsbe_tag, tsbe_raddr);

	sfmmu_load_tsbe_func(tsbe_raddr, tsbe_tag, &(tte), /* phys */1);

	/*
	 * NOTE: for L2 cache errors the line in cache (in the TSB) should
	 *	 be able to be put the modified (M) state so that an error
	 *	 can propagate.
	 *
	 * NOTE: Also since the TSB is holding tsbe structs, and the
	 *	 tag is first in the struct (tag, then data) using
	 *	 injection offsets are potentially useful.
	 */
	raddr = (uint64_t)tsbe_raddr + memtest_get_c_offset(iocp);

	if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
		DPRINTF(0, "%s: ra to pa translation failed "
		    "for raddr = 0x%llx\n", fname, raddr);
		return ((int)paddr);
	}

	/*
	 * Print out the address being used for the test.
	 */
	if (F_VERBOSE(iocp) || (memtest_debug > 0)) {
		cmn_err(CE_NOTE, "%s: TSB injection address is: "
		    "raddr=0x%08x.%08x, paddr=0x%08x.%08x\n",
		    fname, PRTF_64_TO_32(raddr), PRTF_64_TO_32(paddr));
	}

	/*
	 * Determine the L2 cache index hashing (IDX) address to use, if
	 * IDX mode is disabled the idx_paddr will match the original paddr.
	 */
	(void) n2_check_l2_idx_mode(paddr, &idx_paddr);

	/*
	 * Flush the L2$ to ensure data lookup does not hit an existing
	 * entry prior to setting cache into DM mode.
	 */
	if (!F_FLUSH_DIS(iocp)) {
		(void) OP_FLUSHALL_L2_HVMODE(mdatap);
	}

	/*
	 * Set the L2$ into DM mode prior to the call to HV so the
	 * TSB entry can be installed into L2$ by this routine.
	 */
	l2_bank_mask = n2_check_l2_bank_mode();
	l2cr_addr = N2_L2_CTL_REG + (paddr & l2_bank_mask);
	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	l2cr_saved = l2cr_value;

	l2cr_value |= N2_L2CR_DMMODE;
	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, l2cr_addr, l2cr_value, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 for DM mode FAILED, "
		    "ret=0x%x\n", fname, ret);
		return (ret);
	}

	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	DPRINTF(3, "%s: L2CR(at 0x%lx) set to = 0x%lx\n", fname, l2cr_addr,
	    l2cr_value);

	/*
	 * Reload TSB entry as close to the injection as possible.
	 */
	sfmmu_load_tsbe_func(tsbe_raddr, tsbe_tag, &(tte), /* phys */1);

	/*
	 * Inject the error by calling streamlined assembly routine,
	 * using the paddr of the TSB entry as the address to corrupt.
	 *
	 * NOTE: the injection is of DATA type which is what is desired
	 *	 (since the TSB is a data struct) even for ITLB fills.
	 */
	if (ERR_PROT_ISND(iocp->ioc_command)) {
		ret = memtest_hv_inject_error("n2_inj_l2nd_quick",
		    (void *)n2_inj_l2nd_quick, paddr, idx_paddr,
		    (uint64_t)F_CHKBIT(iocp), NULL);
	} else {
		ret = memtest_hv_inject_error("n2_inj_l2cache_data_quick",
		    (void *)n2_inj_l2cache_data_quick, paddr, IOC_XORPAT(iocp),
		    (uint64_t)F_CHKBIT(iocp), idx_paddr);
	}

	if (ret == -1) {
		DPRINTF(0, "%s: n2_inj_l2cache_data_quick or "
		    "n2_inj_l2nd_quick FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * Clear the data or the instruction TLB of the entry which
	 * corresponds to the EI buffer address so the access will miss
	 * in the TLB and will start reading the TSB entries searching
	 * for a match, this should hit the error injected above.
	 *
	 * NOTE: could also do a clearall of the TLB, though for the
	 *	 instruction TLB that might cause a lot of misses when
	 *	 the code returns to kernel mode after the clear.
	 *
	 * NOTE: if the correct TLB entry is not cleared or if there
	 *	 is another entry that can satisfy the translation
	 *	 then the error in L2 will not be hit and likely other
	 *	 code will run over it causing a write-back.  Also if
	 *	 the line in the cache is displaced from the L2 before
	 *	 the TLB fill acess then only a write-back will occur.
	 */
	if (memtest_hv_util("n2_clear_tlb_entry",
	    (void *)n2_clear_tlb_entry, buf_vaddr, (uint64_t)data_flag,
	    NULL, NULL) == -1) {
		DPRINTF(0, "%s: trap to n2_clear_tlb_entry() FAILED!\n", fname);
		return (-1);
	}

	/*
	 * If we do not want to invoke the error then return now,
	 * otherwise invoke the planted data or instruction error with
	 * an access to the EI buffer (which will miss in the TLB and
	 * then be looked up in the TSB).
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
	} else if (ERR_ACC_ISITLB(iocp->ioc_command)) {
		mdatap->m_asmld(kvaddr_a);
	} else {
		ret = *kvaddr_a;
	}

	/*
	 * Return the L2 cache bank to it's previous state.
	 */
	if (l2cr_value != l2cr_saved) {
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, l2cr_addr,
		    l2cr_saved, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 disable DM mode "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	return (0);
}

/*
 * This routine inserts an error into the TSB (data or instruction) in
 * the DRAM to be detected on a TSB fill (miss).
 *
 * The errors are detected by a HW tablewalk that is triggered by accessing
 * a kvaddr (in priv mode below) which causes a TLB miss, the miss will
 * attempt a lookup in the TSB which will hit the error placed in the TSB
 * entry that corresponds to the kvaddr (the allocated injection buffer).
 *
 * NOTE: this routine does not check or set the MMU Tablewalk Pending Control
 *	 Register, but this should not matter since nothing is being brought
 *	 into the TLB.
 *
 * NOTE: the TSBs contain both data and instruction information, with
 *	 separate pointer regs for data and instruction TSB entries.
 */
int
n2_inject_tlb_mem_miss(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	tsb_base_raddr;
	uint64_t	tsb_size;
	uint64_t	buf_vaddr;
	volatile caddr_t kvaddr_c, kvaddr_a;
	uint64_t	paddr;
	uint64_t	raddr;
	uint_t		data_flag = 1;
	uint64_t	l2cr_addr;
	uint64_t	l2cr_value;
	uint64_t	l2cr_saved;
	uint64_t	l2_bank_mask;
	uint64_t	dram_bank_offset;
	uint64_t	tsbe_tag;
	struct hat 	*sfmmup;
	tte_t		tte;
	struct tsbe	*tsbe_raddr;
	struct tsbe 	*(*sfmmu_get_tsbe_func)(uint64_t, caddr_t, int, int);
	void		(*sfmmu_load_tsbe_func)(struct tsbe *, uint64_t, \
	    tte_t *, int);
	uint64_t	ret = 0;
	char		*fname = "n2_inject_tlb_mem_miss";

	/*
	 * Set the kvaddr depending if this is a DTLB or an ITLB test.
	 * This is not done in the pre-test routine for this type of
	 * test because of the way these commands are defined
	 * (access types of DTAC and ITAC instead of LOAD and FETCH).
	 */
	if (ERR_ACC_ISITLB(iocp->ioc_command)) {
		data_flag = 0;
		kvaddr_c = kvaddr_a = (caddr_t)(mdatap->m_asmld);
	} else {	/* Data test */
		kvaddr_c = kvaddr_a = mdatap->m_databuf;
	}

	/*
	 * Keep the buffer address (buf_vaddr) unaligned and as the
	 * address before any offset is applied.  The offset will be
	 * added later after the raddr/paddr is determined.
	 */
	buf_vaddr = (uint64_t)kvaddr_c;
	kvaddr_a += memtest_get_a_offset(iocp);

	DPRINTF(3, "%s: iocp=0x%p, buffer kvaddr_c=0x%llx, buffer kvaddr_a="
	    "0x%llx\n", fname, iocp, kvaddr_c, kvaddr_a);

	/*
	 * Find the correct sfmmu struct and TSB to use.
	 *
	 * NOTE: the kernel MMU code uses tsbinfop = sfmmup->sfmmu_tsb,
	 *	 and that the TSB vaddr is also available:
	 *	 kernel = tsb_base_vaddr = (uint64_t)ktsb_base;
	 *	 user = tsb_base_vaddr = (uint64_t)sfmmup->sfmmu_tsb->tsb_va;
	 */
	if (ERR_MODE_ISKERN(iocp->ioc_command) ||
	    ERR_MODE_ISHYPR(iocp->ioc_command)) {
		sfmmup = ksfmmup;
		tsb_base_raddr = (uint64_t)ktsb_pbase;
		tsb_size = ktsb_szcode;
	} else {	/* USER */
		sfmmup = curproc->p_as->a_hat;
		tsb_base_raddr = (uint64_t)sfmmup->sfmmu_tsb->tsb_pa;
		tsb_size = sfmmup->sfmmu_tsb->tsb_szc;
	}

	DPRINTF(3, "%s: TSB base raddr=0x%llx, TSB size=0x%llx\n", fname,
	    tsb_base_raddr, tsb_size);

	/*
	 * Use the following method to access private (static) MMU
	 * routines from here to reduce code duplication.  The routines
	 * that are needed for this type of injection are:
	 *	sfmmu_get_tsbe(uint64_t tsbeptr, caddr_t vaddr, int vpshift,
	 *		int tsb_szc);
	 *	sfmmu_load_tsbe(struct tsbe *tsbep, uint64_t vaddr,
	 *		tte_t *ttep, int phys);
	 */
	sfmmu_get_tsbe_func = (struct tsbe *(*)(uint64_t, caddr_t, int, int))
	    kobj_getsymvalue("sfmmu_get_tsbe", 1);

	if (sfmmu_get_tsbe_func == NULL) {
		DPRINTF(0, "%s: kobj_getsymvalue for sfmmu_get_tsbe FAILED!\n",
		    fname);
		return (-1);
	}

	sfmmu_load_tsbe_func = (void(*)(struct tsbe *, uint64_t, tte_t *, int))
	    kobj_getsymvalue("sfmmu_load_tsbe", 1);

	if (sfmmu_load_tsbe_func == NULL) {
		DPRINTF(0, "%s: kobj_getsymvalue for sfmmu_load_tsbe FAILED!\n",
		    fname);
		return (-1);
	}

	/*
	 * Find the paddr of the actual TTE in the TSB that corresponds
	 * to our buffer address.
	 */
	if (memtest_get_tte(sfmmup, (caddr_t)buf_vaddr, &tte) != 0) {
		DPRINTF(0, "%s: Could not get tte for buffer vaddr=0x%llx\n",
		    fname, buf_vaddr);
		return (-1);
	}

	DPRINTF(3, "%s: tte=0x%llx, buffer vaddr=0x%llx\n", fname, tte.ll,
	    buf_vaddr);

	tsbe_raddr = sfmmu_get_tsbe_func(tsb_base_raddr, (caddr_t)buf_vaddr,
	    (int)MMU_PAGESHIFT, tsb_size);

	tsbe_tag = buf_vaddr >> TTARGET_VA_SHIFT;
	DPRINTF(3, "%s: sfmmu_get_tsbe_func() tsbe tag=0x%llx, "
	    "tsbe raddr=0x%llx\n", fname, tsbe_tag, tsbe_raddr);

	sfmmu_load_tsbe_func(tsbe_raddr, tsbe_tag, &(tte), /* phys */1);

	/*
	 * Determine the raddr and paddr to use for the injection.
	 *
	 * NOTE: Also since the TSB is holding tsbe structs, and the
	 *	 tag is first in the struct (tag, then data) using
	 *	 injection offsets are potentially useful.
	 */
	raddr = (uint64_t)tsbe_raddr + memtest_get_c_offset(iocp);

	if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
		DPRINTF(0, "%s: ra to pa translation failed "
		    "for raddr = 0x%llx\n", fname, raddr);
		return ((int)paddr);
	}

	/*
	 * Print out the address being used for the test.
	 */
	if (F_VERBOSE(iocp) || (memtest_debug > 0)) {
		cmn_err(CE_NOTE, "%s: TSB injection address is: "
		    "raddr=0x%08x.%08x, paddr=0x%08x.%08x\n",
		    fname, PRTF_64_TO_32(raddr), PRTF_64_TO_32(paddr));
	}

	/*
	 * Determine the DRAM bank offset that corresponds to the paddr.
	 *
	 * Note: the paddr of the TSB entry needs to be placed into the
	 *	 mdata struct in order for the bank offset to be accurate.
	 */
	mdatap->m_paddr_c = paddr;
	dram_bank_offset = memtest_get_dram_bank_offset(mdatap);

	/*
	 * Flush the L2$ to ensure data lookup does not hit prior to setting
	 * cache into DM mode.
	 *
	 * NOTE: this may flush existing errors from L2 producing write-back
	 *	 error(s) before the DRAM error is even injected.
	 */
	if (!F_FLUSH_DIS(iocp)) {
		(void) OP_FLUSHALL_L2_HVMODE(mdatap);
	}

	/*
	 * Set the L2$ into DM mode prior to the call to HV so the
	 * TSB entry can be installed into L2$ by this routine.
	 */
	l2_bank_mask = n2_check_l2_bank_mode();
	l2cr_addr = N2_L2_CTL_REG + (paddr & l2_bank_mask);
	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	l2cr_saved = l2cr_value;

	l2cr_value |= N2_L2CR_DMMODE;
	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, l2cr_addr, l2cr_value, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 for DM mode FAILED, "
		    "ret=0x%x\n", fname, ret);
		return (ret);
	}

	l2cr_value = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, l2cr_addr, NULL, NULL, NULL);
	DPRINTF(3, "%s: L2CR(at 0x%lx) set to = 0x%lx\n", fname, l2cr_addr,
	    l2cr_value);

	/*
	 * Reload TSB entry as close to the injection as possible.
	 */
	sfmmu_load_tsbe_func(tsbe_raddr, tsbe_tag, &(tte), /* phys */1);

	/*
	 * Inject the error by calling streamlined assembly routine,
	 * using the paddr of the TSB entry as the address to corrupt.
	 *
	 * NOTE: the injection is of DATA type which is what is desired
	 *	 (since the TSB is a data struct) even for ITLB fills.
	 */
	ret = memtest_hv_inject_error("n2_inj_memory_quick",
	    (void *)n2_inj_memory_quick, paddr, IOC_XORPAT(iocp) |
	    N2_DRAM_INJECTION_ENABLE | N2_DRAM_SSHOT_ENABLE,
	    dram_bank_offset, l2_bank_mask);

	if (ret == -1) {
		DPRINTF(0, "%s: n2_inj_memory_quick FAILED, ret=0x%x\n",
		    fname, ret);
		return (ret);
	}

	/*
	 * Clear the data or the instruction TLB of the entry which
	 * corresponds to the EI buffer address so the access will miss
	 * in the TLB and will start reading the TSB entries searching
	 * for a match, this should hit the error injected above.
	 *
	 * NOTE: could also do a clearall of the TLB, though for the
	 *	 instruction TLB that might cause a lot of misses when
	 *	 the code returns to kernel mode after the clear.
	 */
	if (memtest_hv_util("n2_clear_tlb_entry",
	    (void *)n2_clear_tlb_entry, buf_vaddr, (uint64_t)data_flag,
	    NULL, NULL) == -1) {
		DPRINTF(0, "%s: trap to n2_clear_tlb_entry() FAILED!\n",
		    fname);
		return (-1);
	}

	/*
	 * If we do not want to invoke the error then return now,
	 * otherwise invoke the planted data or instruction error with
	 * an access to the EI buffer (which will miss in the TLB and
	 * then be looked up in the TSB).
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
	} else if (ERR_ACC_ISITLB(iocp->ioc_command)) {
		mdatap->m_asmld(kvaddr_a);
	} else {
		ret = *kvaddr_a;
	}

	/*
	 * Return the L2 cache bank to it's previous state.
	 */
	if (l2cr_value != l2cr_saved) {
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, l2cr_addr,
		    l2cr_saved, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 disable DM mode "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	return (0);
}

/*
 * This routine inserts an error into the L2 cache at a location determined
 * by the real address member of the mdata structure and left to be discovered
 * by the HW provided L2 cache scrubber (enabled here).
 *
 * NOTE: this routine does not need to restore the scrubber because the
 *	 pre_test and post_test functions handle the restore.
 *
 * NOTE: unlike the n2_inject_l2cache() routine, the line is not put into
 *	 the modified (M) state by default.  This would require that this
 *	 C-level routine place the cache into DM mode prior to the access.
 *	 The line should not have to be in the modified state to be detected.
 *
 * NOTE: this routine is called directly from the Niagara-II command list.
 */
int
n2_inject_l2_scrub(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset;
	uint64_t	idx_paddr;
	uint64_t	l2cr_addr;
	uint64_t	l2cr_value;
	uint64_t	l2cr_saved;
	uint64_t	l2_bank_mask;
	uint64_t	scrub_interval;
	uint_t		ret;
	char		*fname = "n2_inject_l2_scrub";

	if (ERR_MISC_ISPHYS(iocp->ioc_command)) {
		offset = iocp->ioc_addr;
	} else {
		offset = mdatap->m_paddr_c;
	}

	DPRINTF(3, "%s: address/offset=0x%llx\n", fname, offset);

	/*
	 * Determine the bank to set the scrubber running on by checking
	 * what the L2 bank mask should be for this system.
	 */
	l2_bank_mask = n2_check_l2_bank_mode();
	l2cr_addr = N2_L2_CTL_REG + (offset & l2_bank_mask);
	l2cr_value = l2cr_saved = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, l2cr_addr, NULL, NULL, NULL);
	DPRINTF(3, "%s: before setting L2CR at 0x%llx = 0x%llx\n", fname,
	    l2cr_addr, l2cr_value);

	/*
	 * Determine the L2 cache index hashing (IDX) address to use, if
	 * IDX mode is disabled the idx_paddr will match the original paddr.
	 */
	(void) n2_check_l2_idx_mode(offset, &idx_paddr);

	/*
	 * Set the scrub interval to a low (default) value and enable
	 * the L2 cache scrubber (scrub interval is bits 14:3) before injection.
	 * MISC2 is used by this test to support a custom scrub interval value,
	 * the default of 0x1f0 should be ok, but 0xf0 is more reliable.  The
	 * HW team warned not to set the value to low as an error could cause
	 * the chip to make very slow forward progress (or none at all).
	 */
	if (F_MISC2(iocp)) {
		scrub_interval = (iocp->ioc_misc2) & NI_L2_SCRUB_INTERVAL_MASK;
	} else {
		scrub_interval = NI_L2_SCRUB_INTERVAL_DFLT;
	}

	l2cr_value &= ~((uint64_t)NI_L2_SCRUB_INTERVAL_MASK);
	l2cr_value |= (N2_L2CR_SCRUBEN | scrub_interval);

	/*
	 * If the error is to be injected into a dirty (modified) cache line
	 * the cache must be placed into DM mode here in the C routine.
	 */
	if (F_CACHE_DIRTY(iocp)) {
		l2cr_value |= N2_L2CR_DMMODE;
	}

	if ((ret = memtest_hv_util("hv_paddr_store64", (void *)hv_paddr_store64,
	    l2cr_addr, l2cr_value, NULL, NULL)) == -1) {
		return (ret);
	}

	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	DPRINTF(3, "%s: L2CR at 0x%llx set to = 0x%llx\n", fname,
	    l2cr_addr, l2cr_value);

	/*
	 * Inject the L2 error using standard method (phys or normal).
	 */
	if (ERR_MISC_ISPHYS(iocp->ioc_command)) {
		if ((ret = OP_INJECT_L2PHYS(mdatap)) == -1) {
			DPRINTF(0, "%s: n2_inject_l2phys FAILED, ret = 0x%x\n",
			    fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Inject a data error, and place the cache line into the
		 * modified (M) state unless clean was specified.  This will
		 * only work if the cache has been placed into DM mode already.
		 */
		if (F_CACHE_DIRTY(iocp)) {
			stphys(mdatap->m_raddr_c, ldphys(mdatap->m_raddr_c));
		}

		if ((ret = memtest_hv_inject_error("n2_inj_l2cache_data",
		    (void *)n2_inj_l2cache_data, offset, IOC_XORPAT(iocp),
		    (uint64_t)F_CHKBIT(iocp), idx_paddr)) == -1) {
			DPRINTF(0, "%s: n2_inject_l2cache FAILED, ret = 0x%x\n",
			    fname, ret);
			return (ret);
		}
	}

	DPRINTF(3, "%s: n2_inject_l2cache/phys ret = 0x%x\n", fname, ret);

	/*
	 * Wait a finite amount of time for the scrubber to hit the error.
	 */
	DELAY(iocp->ioc_delay * MICROSEC);

	/*
	 * Although the pre and post test routines set and restore the
	 * scrubbers, if the "asis" option is used they will not be touched
	 * so restore the L2 CR on this bank here to be safe.
	 */
	if ((ret = memtest_hv_util("hv_paddr_store64", (void *)hv_paddr_store64,
	    l2cr_addr, l2cr_saved, NULL, NULL)) == -1) {
		return (ret);
	}

	return (0);
}

/*
 * This routine inserts an error into main memory (DRAM) at the specified
 * physical address to be discovered by the memory scrubber which is also
 * set in this routine.
 *
 * NOTE: this routine does not need to restore the scrubber because the
 *	 pre_test and post_test functions handle the restore.
 *
 * NOTE: this routine is called directly from the Niagara-II command list.
 */
int
n2_inject_memory_scrub(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	freq_csr;
	uint64_t	scrub_csr;
	uint64_t	scrub_interval;
	uint64_t	dram_bank_offset;
	uint_t		ret;
	char		*fname = "n2_inject_memory_scrub";

	DPRINTF(3, "%s: paddr=0x%llx\n", fname, paddr);

	/*
	 * Determine the DRAM bank offset.
	 *
	 * Note that if the normal (4-way mode) channel is disabled,
	 * then the scrubber on the channel actually used must be set.
	 * See the comments in the n2_inject_memory() routine for
	 * more information on reduced channel register settings.
	 */
	dram_bank_offset = memtest_get_dram_bank_offset(mdatap);

	/*
	 * Now the values for the freq and scrub CSRs can be determined.
	 */
	freq_csr = N2_DRAM_CSR_BASE + N2_DRAM_SCRUB_FREQ_REG + dram_bank_offset;
	scrub_csr = N2_DRAM_CSR_BASE + N2_DRAM_SCRUB_ENABLE_REG +
	    dram_bank_offset;

	/*
	 * Set the scrub frequency to the fastest safe value (0xff) using
	 * NI_DRAM_SCRUB_INTERVAL_DFLT.
	 *
	 * With this setting all of DRAM will be scrubbed in 81 seconds
	 * for the typical 16 GB config.
	 *
	 * For the correct running system value of 0xfff the estimated time
	 * is 46 minutes for 64 GB, 23 for 32 GB, 11.5 for 16 GB.
	 */
	if (F_MISC2(iocp)) {
		scrub_interval = (iocp->ioc_misc2) &
		    NI_DRAM_SCRUB_INTERVAL_MASK;
	} else {
		scrub_interval = NI_DRAM_SCRUB_INTERVAL_DFLT;
	}

	/*
	 * Inject the DRAM error using standard Niagara-II function.
	 */
	if ((ret = n2_inject_memory(mdatap)) == -1) {
		DPRINTF(0, "%s: n2_inject_memory FAILED, ret = 0x%x\n",
		    fname, ret);
		return (ret);
	}

	DPRINTF(3, "%s: n2_inject_memory ret = 0x%x\n", fname, ret);

	/*
	 * Enable the correct banks DRAM scrubber(s) after the error injection,
	 * first the freqency then the enable register.
	 *
	 * NOTE: the reading back of the regs will take a while and can be
	 *	 removed if we need to streamline this routine.
	 */
	if ((ret = memtest_hv_util("hv_paddr_store64", (void *)hv_paddr_store64,
	    freq_csr, scrub_interval, NULL, NULL)) == -1) {
		return (ret);
	}

	ret = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    freq_csr, NULL, NULL, NULL);
	DPRINTF(3, "%s: DRAM freq_csr = 0x%lx\n", fname, ret);

	if ((ret = memtest_hv_util("hv_paddr_store64", (void *)hv_paddr_store64,
	    scrub_csr, 1, NULL, NULL)) == -1) {
		return (ret);
	}

	ret = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    scrub_csr, NULL, NULL, NULL);
	DPRINTF(3, "%s: DRAM scrub_csr = 0x%lx\n", fname, ret);

	/*
	 * Wait a finite amount of time for the scrubber to find the error.
	 */
	DELAY(iocp->ioc_delay * MICROSEC);

	return (0);
}

/*
 * ********************************************************************
 * The following block of routines are the Niagara-II support routines.
 * ********************************************************************
 */

/*
 * This routine traps to hypervisor to run an assembly routine that performs
 * a Control Word Queue (CWQ) operation to invoke a previously injected error
 * in the L2 cache, or DRAM memory.
 *
 * The asm routine expects that the operation type will be COPY, and
 * this routine uses the access paddr as the source paddr and uses a
 * specific offset into the data buffer for the destination paddr.  Recall
 * that the data buffer is MIN_DATABUF_SIZE = 8192 Bytes in size and half
 * way into this buffer are the access asm routines.
 *
 * NOTE: The mode of the operation (interrupt or polled) determines if the
 *	 error is resumable or non-resumable because the type of trap will be
 *	 precise or disrupting respectively.
 */
int
n2_access_cwq(mdata_t *mdatap, uint_t acc_type, uint_t intr_flag)
{
	uint64_t	cwq_status_value;
	int		ret;
	char		*fname = "n2_access_cwq";

	DPRINTF(3, "%s: paddr=0x%llx, acc_type=0x%lx\n", fname,
	    mdatap->m_paddr_a, acc_type);

	/*
	 * Check if the CWQ unit is enabled and/or busy.  Note that
	 * the asm routine will disable and drain the CWQ before injection
	 * and will leave the CWQ enabled after it is finished.
	 */
	cwq_status_value = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_STREAM, ASI_CWQ_CSR_REG,
	    (uint64_t)n2_debug_buf_pa, NULL);
	DPRINTF(3, "%s: CWQ status CSR=0x%llx (bit1=busy, "
	    "bit0=enable)\n", fname, cwq_status_value);

#ifdef	MEM_DEBUG_BUFFER
	n2_debug_init();

	ret = memtest_hv_util("n2_acc_cwq", (void *)n2_acc_cwq,
	    mdatap->m_paddr_a, mdatap->m_paddr_a + (MIN_DATABUF_SIZE / 8),
	    (uint64_t)acc_type, (uint64_t)n2_debug_buf_pa);

	n2_debug_dump();
#else	/* MEM_DEBUG_BUFFER */

	ret = memtest_hv_util("n2_acc_cwq", (void *)n2_acc_cwq,
	    mdatap->m_paddr_a, mdatap->m_paddr_a + (MIN_DATABUF_SIZE / 8),
	    (uint64_t)acc_type, intr_flag);

#endif	/* MEM_DEBUG_BUFFER */

	if (ret == -1) {
		DPRINTF(0, "%s: HV mode CWQ access "
		    "routine FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine traps to hypervisor to run an assembly routine that performs
 * a Modular Arithmetic operation or sync to invoke a previously injected error
 * in the MA memory, L2 cache, or DRAM memory.
 *
 * NOTE: MA memory errors require a store or arithmetic operation to be
 *	 invoked, loads to MA memory will not generate an error.
 *
 * NOTE: The mode of the operation (interrupt or polled) determines if the
 *	 error is resumable or non-resumable because the type of trap will be
 *	 precise or disrupting respectively.
 *
 * NOTE: this routine is used for KT/RF since the only notable difference
 *	 is that the ASI_MA_PA_REG (aka ASI_MPA_REG) is different because
 *	 KT/RF uses 44-bit addressing.  This code can handle that change.
 */
int
n2_access_mamem(mdata_t *mdatap, uint_t acc_type, uint_t intr_flag)
{
	int		ret;
	char		*fname = "n2_access_mamem";

	/*
	 * Shift the access type for the N2 specific register.
	 */
	acc_type = acc_type << N2_MA_OP_SHIFT;

	ret = memtest_hv_util("n2_acc_mamem", (void *)n2_acc_mamem,
	    mdatap->m_paddr_a, (uint64_t)acc_type,
	    (uint64_t)intr_flag, NULL);
	if (ret == -1) {
		DPRINTF(0, "%s: HV mode MA parity access "
		    "routine FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine checks the Niagara-II L2 cache enable status. It is possible
 * for the L2 cache to be running in reduced bank modes.  The only valid
 * L2 bank/DRAM modes are:
 *		8 L2 banks, 4 DRAM branches, 8 cores
 *		4 L2 banks, 2 DRAM branches, 4 cores
 *		2 L2 banks, 1 DRAM branches, 2 cores
 *
 * Also note that the L2 banks are hard connected to specific DRAM branches.
 *		L2 banks 0,1	-> DRAM branch 0
 *		L2 banks 2,3	-> DRAM branch 1
 *		L2 banks 4,5	-> DRAM branch 2
 *		L2 banks 6,7	-> DRAM branch 3
 *
 * No Solaris code should be disabling cache banks so this routine is used
 * as a check only.  The value of the BANK_ENABLE_STATUS (0x1028) register
 * is used to determine if the cache is in a partial mode.
 *
 * The return value is the bitmask to use on a paddr to determine the set
 * of bank registers to use for the low-level routines.
 */
uint64_t
n2_check_l2_bank_mode(void)
{
	uint64_t	l2_bank_mask;
	int		count;

	l2_bank_mask = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_L2_BANK_EN_STATUS_FULL,
	    NULL, NULL, NULL);

	if (((l2_bank_mask & N2_L2_BANK_EN_STATUS_ALLEN) ==
	    N2_L2_BANK_EN_STATUS_ALLEN) &&
	    ((l2_bank_mask & N2_L2_BANK_EN_STATUS_PM) == 0)) {
		return (N2_L2_8BANK_MASK);
	} else {
		l2_bank_mask &= N2_L2_BANK_EN_STATUS_ALLEN;

		/*
		 * Count the set bits (memory banks) in the status reg.
		 */
		for (count = 0; l2_bank_mask; l2_bank_mask >>= 1) {
			count += l2_bank_mask & 1;
		}

		if (count == 4) {
			l2_bank_mask = N2_L2_8BANK_MASK; /* all enabled */
		} else if (count == 2) {
			l2_bank_mask = N2_L2_4BANK_MASK; /* four enabled */
		} else if (count == 1) {
			l2_bank_mask = N2_L2_2BANK_MASK; /* two enabled */
		} else {
			cmn_err(CE_WARN, "n2_check_l2_bank_mode: L2 cache "
			    "on this chip is in an unsupported reduced "
			    "bank mode!\n");
			l2_bank_mask = N2_L2_8BANK_MASK;
		}

		return (l2_bank_mask);
	}
}

/*
 * This routine checks the Niagara-II L2 cache index hashing (IDX) status
 * and determines the hashed address for the provided address.
 *
 * Because the IDX mode is only set after a warm reset the injector cannot
 * disable the L2 cache index hashing prior to injecting an L2 error. So
 * the address values used to index into the L2 cache must be adjusted for
 * IDX mode if it is set.
 *
 * The index hashing performs the following operations on the address:
 *	bits[17:13]	= PA[32:28] xor PA[17:13]
 *	bits[12:11]	= PA[19:18] xor PA[12:11]
 *	bits[10:9]	= PA[10:9]
 *
 * The hashed address is returned if index hashing is enabled, and the orginal
 * address is returned if it is disabled since this will index correctly.
 */
int
n2_check_l2_idx_mode(uint64_t addr, uint64_t *idx_addr_ptr)
{
	uint64_t	idx_addr;
	int		idx_enabled;
	char		*fname = "n2_check_l2_idx_mode";

	/*
	 * Check if L2 cache has index hashing (IDX) enabled.
	 */
	idx_enabled = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_L2_IDX_HASH_EN_STATUS_FULL,
	    NULL, NULL, NULL);

	if (idx_enabled & 0x1) {
		/*
		 * L2 cache index hashing (IDX) is enabled, so transform the
		 * addr inpar to it's hashed version.
		 */
		DPRINTF(2, "%s: L2 cache IDX mode is enabled\n", fname);

		idx_addr = ((addr & N2_L2_IDX_TOPBITS) >> 15) ^ addr;
		idx_addr = ((addr & N2_L2_IDX_BOTBITS) >> 7) ^ idx_addr;
	} else {
		/*
		 * L2 cache index hashing (IDX) is disabled, so just return
		 * the unaltered addr as the correct addr to use.
		 */
		DPRINTF(2, "%s: L2 cache IDX mode is disabled\n", fname);
		idx_addr = addr;
	}

	*idx_addr_ptr = idx_addr;
	DPRINTF(2, "%s: original addr=0x%llx, IDX addr=0x%llx\n", fname,
	    addr, idx_addr);
	return (idx_enabled & 0x1);
}

/*
 * This routine allows the Niagara-II data cache to be flushed via
 * the opsvec table.  Note that the routine called (in hypervisor mode) clears
 * the entire d-cache, and does not just invalidate the tags.
 */
/* ARGSUSED */
int
n2_clearall_dcache(cpu_info_t *cip)
{
	int	ret;

	if ((ret = memtest_hv_util("n2_dcache_clear", (void *)n2_dcache_clear,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "n2_clearall_dcache: trap to n2_dcache_clear"
		    "FAILED, ret=0x%x\n", ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine allows the Niagara-II instruction cache to be flushed via
 * the opsvec table.  Note that the routine called (in hypervisor mode)
 * clears the entire i-cache, and does not just invalidate the tags.
 */
/* ARGSUSED */
int
n2_clearall_icache(cpu_info_t *cip)
{
	int	ret;

	if ((ret = memtest_hv_util("n2_icache_clear", (void *)n2_icache_clear,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "n2_clearall_icache: trap to n2_icache_clear"
		    "FAILED, ret=0x%x\n", ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine enables or disables the memory (DRAM) or L2 cache scrubber
 * on Niagara-II and Victoria Falls systems using the scrub_info struct.
 *
 * By default this routine only modifies the scrub settings on the DRAM or
 * L2 bank that is involved in the test (determined by the paddr).  If the
 * user has specified that scrubbers should be enabled or disabled then the
 * scrubbers on every DRAM and/or L2 bank will be enabled or disabled.
 *
 * NOTE: the s_l2_offset member is set in the memtest_pre_test()
 *	 routine to be equal to mdatap->m_paddr_c.
 */
int
n2_control_scrub(mdata_t *mdatap, uint64_t flags)
{
	scrub_info_t	*scrubp = mdatap->m_scrubp;
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	check_value;
	uint64_t	temp_value;
	uint64_t	temp_fvalue;
	uint64_t	l2_bank_mask, l2_bank_enabled_mask;
	uint64_t	dram_bank_offset, l2_bank_offset;
	uint64_t	bank_compare, l2_bank_enabled_val;
	uint64_t	dram_bank_max, dram_bank_inc;
	uint_t		ret;
	char		*fname = "n2_control_scrub";

	DPRINTF(3, "%s: changing L2 and/or mem scrub settings "
	    "on cpuid=%d, paddr=0x%08x.%08x\n", fname,
	    getprocessorid(), PRTF_64_TO_32(mdatap->m_paddr_c));

	if (flags & MDATA_SCRUB_L2) {
		/*
		 * If the user specified that the L2$ scrubbers should be set;
		 * the scrubbers on all eight channels must be
		 * disabled/restored.  Otherwise the correct register to
		 * use is determined from the physical corruption address
		 * and the number of enabled L2 banks.
		 */
		l2_bank_mask = n2_check_l2_bank_mode();
		if (F_L2SCRUB_EN(iocp) || F_L2SCRUB_DIS(iocp)) {
			scrubp->s_l2cr_addr = N2_L2_CTL_REG;
		} else {
			scrubp->s_l2cr_addr = N2_L2_CTL_REG +
			    (scrubp->s_l2_offset & l2_bank_mask);
		}

		/*
		 * If not RESTORE save the current L2CR scrub register value.
		 */
		if (!(flags & MDATA_SCRUB_RESTORE)) {
			scrubp->s_l2cr_value = memtest_hv_util(
			    "hv_paddr_load64",
			    (void *)hv_paddr_load64,
			    scrubp->s_l2cr_addr, NULL, NULL, NULL);
			DPRINTF(3, "%s: L2CR at 0x%llx value = 0x%llx\n",
			    fname, scrubp->s_l2cr_addr,
			    scrubp->s_l2cr_value);
		}

		/*
		 * Determine what action to take on the L2 scrubber.
		 * Valid operations are ENABLE, DISABLE, and RESTORE.
		 */
		if (flags & MDATA_SCRUB_ENABLE) {
			temp_value = scrubp->s_l2cr_value | N2_L2CR_SCRUBEN;
		} else if (flags & MDATA_SCRUB_DISABLE) {
			temp_value = scrubp->s_l2cr_value & ~(N2_L2CR_SCRUBEN);
		} else {	/* restore previously saved value */
			temp_value = scrubp->s_l2cr_value;
		}

		/*
		 * Store the appropriate value to L2CR.
		 * Set all eight registers if the user specified that
		 * all of the L2 cache scrubbers should be set.
		 */
		if (F_L2SCRUB_EN(iocp) || F_L2SCRUB_DIS(iocp)) {
			for (l2_bank_offset = 0; l2_bank_offset <=
			    N2_L2_BANK_MASK; l2_bank_offset +=
			    N2_L2_BANK_OFFSET) {

				if ((ret = memtest_hv_util("hv_paddr_store64",
				    (void *)hv_paddr_store64,
				    scrubp->s_l2cr_addr + l2_bank_offset,
				    temp_value, NULL, NULL)) == -1) {
					DPRINTF(0, "%s: unable to set L2CR!\n",
					    fname);
					return (ret);
				}
			}
		} else {
			/*
			 * Otherwise disable only the specific channel.
			 */
			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, scrubp->s_l2cr_addr,
			    temp_value, NULL, NULL)) == -1) {
				DPRINTF(0, "%s: unable to set L2CR!\n", fname);
				return (ret);
			}

			/*
			 * Check the value to ensure that it was set correctly.
			 */
			if ((check_value = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, scrubp->s_l2cr_addr,
			    NULL, NULL, NULL)) != temp_value) {
				DPRINTF(0, "%s: L2CR not set properly, value "
				    "is 0x%llx\n", fname, check_value);
				return (-1);
			} else {
				DPRINTF(3, "%s: L2CR set to 0x%llx\n", fname,
				    temp_value);
			}
		}
	}

	if (flags & MDATA_SCRUB_DRAM) {
		/*
		 * Determine the bank to set scrubber on by finding the
		 * DRAM bank offset to use below.
		 */
		dram_bank_offset = memtest_get_dram_bank_offset(mdatap);

		scrubp->s_memcr_addr = N2_DRAM_CSR_BASE +
		    N2_DRAM_SCRUB_ENABLE_REG + dram_bank_offset;
		scrubp->s_memfcr_addr = N2_DRAM_CSR_BASE +
		    N2_DRAM_SCRUB_FREQ_REG + dram_bank_offset;

		l2_bank_enabled_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, N2_L2_BANK_EN_STATUS_FULL,
		    NULL, NULL, NULL);
		l2_bank_enabled_val &= N2_L2_BANK_EN_STATUS_ALLEN;

		/*
		 * If not RESTORE then save the current DRAMCR scrub reg vals.
		 * This only needs to be done once even in 2-channel mode since
		 * the register set of the associated channels must match.
		 */
		if (!(flags & MDATA_SCRUB_RESTORE)) {
			scrubp->s_memcr_value = memtest_hv_util(
			    "hv_paddr_load64",
			    (void *)hv_paddr_load64,
			    scrubp->s_memcr_addr,
			    NULL, NULL, NULL);
			DPRINTF(3, "%s: DRAMCR at 0x%llx value = 0x%llx\n",
			    fname, scrubp->s_memcr_addr,
			    scrubp->s_memcr_value);

			scrubp->s_memfcr_value = memtest_hv_util(
			    "hv_paddr_load64",
			    (void *)hv_paddr_load64,
			    scrubp->s_memfcr_addr,
			    NULL, NULL, NULL);
			DPRINTF(3, "%s: DRAMFCR at 0x%llx value = 0x%llx\n",
			    fname, scrubp->s_memfcr_addr,
			    scrubp->s_memfcr_value);
		}

		/*
		 * Determine what action to take on the DRAM scrubber.
		 * Valid operations are ENABLE, DISABLE, and RESTORE.
		 */
		temp_fvalue = scrubp->s_memfcr_value;
		if (flags & MDATA_SCRUB_ENABLE) {
			temp_value = scrubp->s_memcr_value | 0x1;
		} else if (flags & MDATA_SCRUB_DISABLE) {
			temp_value = scrubp->s_memcr_value & ~(0x1);
		} else {	/* restore previously saved value */
			temp_value = scrubp->s_memcr_value;
		}

		/*
		 * If this is a STORM test or if the user specified that the
		 * DRAM scrubbers should be set; the scrubbers on all four
		 * channels must be disabled/restored.  Otherwise the correct
		 * register to use is determined from an offset derived from
		 * the physical corruption address (above).  In either case
		 * only channels that are enabled can be modified.
		 *
		 * Note that the values used for VF and N2 are different.
		 */
		if (ERR_MISC_ISSTORM(iocp->ioc_command) ||
		    F_MEMSCRUB_EN(iocp) || F_MEMSCRUB_DIS(iocp)) {
			if (CPU_ISVFALLS(mdatap->m_cip)) {
				l2_bank_enabled_mask = 0x3;
				bank_compare = dram_bank_offset >> 11;
				dram_bank_max = dram_bank_inc =
				    VF_DRAM_BRANCH_MASK <<
				    VF_DRAM_BRANCH_PA_SHIFT;
			} else {
				l2_bank_enabled_mask = 0x1;
				bank_compare = dram_bank_offset >> 12;
				dram_bank_max = N2_DRAM_BRANCH_MASK <<
				    N2_DRAM_BRANCH_PA_SHIFT;
				dram_bank_inc = N2_DRAM_PADDR_OFFSET <<
				    N2_DRAM_BRANCH_PA_SHIFT;
			}

			for (dram_bank_offset = 0; dram_bank_offset <=
			    dram_bank_max; dram_bank_offset += dram_bank_inc) {
				/*
				 * Only touch the regs of enabled channels.
				 */
				if ((l2_bank_enabled_val >> bank_compare) &
				    l2_bank_enabled_mask) {
					if ((ret = memtest_hv_util(
					    "hv_paddr_store64",
					    (void *)hv_paddr_store64,
					    scrubp->s_memcr_addr +
					    dram_bank_offset, temp_value,
					    NULL, NULL)) == -1) {
						DPRINTF(0, "%s: unable to set "
						    "DRAMCR!\n", fname);
						return (ret);
					}

					if ((ret = memtest_hv_util(
					    "hv_paddr_store64",
					    (void *)hv_paddr_store64,
					    scrubp->s_memfcr_addr +
					    dram_bank_offset, temp_fvalue,
					    NULL, NULL)) == -1) {
						DPRINTF(0, "%s: unable to set "
						    "DRAMFCR!\n", fname);
						return (ret);
					}
				}
			}

			/*
			 * Do not check the values if all four were set.
			 * Is ok to exit here since the L2 scrubbers were
			 * already set.
			 */
			return (0);
		}

		/*
		 * Otherwise disable only the specific channel (bank offset
		 * already determined above).
		 */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, scrubp->s_memcr_addr,
		    temp_value, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: unable to set DRAMCR!\n", fname);
			return (ret);
		}

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, scrubp->s_memfcr_addr,
		    temp_fvalue, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: unable to set DRAMFCR!\n", fname);
			return (ret);
		}

		/*
		 * Check the values to ensure they were set correctly.
		 */
		if ((check_value = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, scrubp->s_memcr_addr,
		    NULL, NULL, NULL)) != temp_value) {
			DPRINTF(0, "%s: DRAMCR not set properly, value "
			    "is 0x%llx\n", fname, check_value);
			return (-1);
		} else {
			DPRINTF(3, "%s: DRAMCR set to 0x%llx\n",
			    fname, check_value);
		}

		if ((check_value = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, scrubp->s_memfcr_addr,
		    NULL, NULL, NULL)) != temp_fvalue) {
			DPRINTF(0, "%s: DRAMFCR not set properly, value "
			    "is 0x%llx\n", fname, check_value);
			return (-1);
		} else {
			DPRINTF(3, "%s: DRAMFCR set to 0x%llx\n",
			    fname, check_value);
		}
	}

	return (0);
}

/*
 * This routine allows the Niagara-II error status registers (ESRs) to
 * be cleared from userland.  This is needed because of persistent
 * FW/HV bugs that leave bits set in certain ESRs after errors are
 * handled.
 *
 * NOTE: a silicon bug causes the chip to hang if DRAM registers are
 *	 accessed from a disabled DRAM bank, this is why the below routine
 *	 is checking for disabled L2$ banks (which will match the DRAM banks).
 *
 * NOTE: when L2$ banks are disabled the values are mirrored in the disabled
 *	 registers.  This routine always prints all the L2$ registers in
 *	 order to be as transparent as possible to system configuration.
 *
 * NOTE: for best results this command/routine should be used with the "-N"
 *	 user option to avoid changing the systems error enable registers.
 */
/*ARGSUSED*/
int
n2_debug_clear_esrs(mdata_t *mdatap)
{
	uint64_t	offset;
	uint64_t	read_val;
	uint64_t	l2_bank_enabled_val;
	uint64_t	bank_compare;
	int		i, ret;
	char		*fname = "n2_debug_clear_esrs";

	/*
	 * Clear the L2$ ESRs first.  Note that certain fields in the
	 * registers are W1C and others are not, so two writes are performed.
	 */
	for (i = 0, offset = 0; i < N2_NUM_L2_BANKS; i++,
	    offset += N2_L2_BANK_OFFSET) {
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (N2_L2_ERR_STS_REG + offset),
		    NULL, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (N2_L2_ERR_STS_REG + offset),
		    read_val, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to N2_L2_ERR_STS_REG"
			    " FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (N2_L2_ERR_STS_REG + offset),
		    0ULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to N2_L2_ERR_STS_REG"
			    " FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		/*
		 * Also clear the L2 NotData registers (at 0xAE.0000.0000).
		 */
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (0xae00000000 + offset),
		    NULL, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (0xae00000000 + offset),
		    read_val, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to N2_L2_ND_STS_REG"
			    " FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (0xae00000000 + offset),
		    0ULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to N2_L2_ND_STS_REG"
			    " FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	/*
	 * Clear the (enabled) DRAM ESRs next.
	 */
	l2_bank_enabled_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_L2_BANK_EN_STATUS_FULL,
	    NULL, NULL, NULL);
	l2_bank_enabled_val &= N2_L2_BANK_EN_STATUS_ALLEN;

	for (i = 0, offset = 0; i < N2_NUM_DRAM_BRANCHES; i++,
	    offset += N2_DRAM_BRANCH_OFFSET) {
		bank_compare = offset >> 12;
		if (((l2_bank_enabled_val >> bank_compare) & 0x1) != 0) {
			read_val = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, (N2_DRAM_CSR_BASE +
			    N2_DRAM_ERROR_STATUS_REG + offset),
			    NULL, NULL, NULL);

			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, (N2_DRAM_CSR_BASE +
			    N2_DRAM_ERROR_STATUS_REG + offset), read_val,
			    NULL, NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 to "
				    "N2_DRAM_ERROR_STATUS_REG FAILED, "
				    " ret=0x%x\n", fname, ret);
				return (ret);
			}

			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, (N2_DRAM_CSR_BASE +
			    N2_DRAM_ERROR_STATUS_REG + offset), 0ULL, NULL,
			    NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 to "
				    "N2_DRAM_ERROR_STATUS_REG FAILED, "
				    "ret=0x%x\n", fname, ret);
				return (ret);
			}

			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, (N2_DRAM_CSR_BASE +
			    N2_DRAM_ERROR_ADDR_REG + offset), 0ULL, NULL,
			    NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 to "
				    "N2_DRAM_ERROR_ADDR_REG FAILED, "
				    "ret=0x%x\n", fname, ret);
				return (ret);
			}

			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, (N2_DRAM_CSR_BASE +
			    N2_DRAM_ERROR_LOC_REG + offset), 0ULL, NULL,
			    NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 to "
				    "N2_DRAM_ERROR_LOC_REG FAILED, "
				    "ret=0x%x\n", fname, ret);
				return (ret);
			}

			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, (N2_DRAM_CSR_BASE +
			    N2_DRAM_ERROR_RETRY_REG + offset), 0ULL, NULL,
			    NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 to "
				    "N2_DRAM_ERROR_RETRY_REG FAILED, "
				    "ret=0x%x\n", fname, ret);
				return (ret);
			}

			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, (N2_DRAM_CSR_BASE +
			    N2_DRAM_FBD_ERROR_SYND_REG + offset), 0ULL, NULL,
			    NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 to "
				    "N2_DRAM_FBD_ERROR_SYND_REG FAILED, "
				    "ret=0x%x\n", fname, ret);
				return (ret);
			}
		}
	}

	/*
	 * Skip the internal ESRs since they do not seem to have any
	 * issues with bits remaining set.  But clear the SOC ESRs.
	 * All fields in the SOC ESRs are RW so no read is required.
	 */
	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, N2_SOC_ERR_STS_REG,
	    0ULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to N2_SOC_ERR_STS_REG"
		    " FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, N2_SOC_ERR_PND_REG,
	    0ULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to N2_SOC_ERR_PND_REG"
		    " FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine allows the Niagara-II error status registers (ESRs) to
 * be printed out to the console.  This is useful for determining if
 * errors have been detected by the HW but not acted upon.
 *
 * NOTE: a silicon bug causes the chip to hang if DRAM registers are
 *	 accessed from a disabled DRAM bank, this is why the below routine
 *	 is checking for disabled L2$ banks (which will match the DRAM banks).
 *
 * NOTE: when L2$ banks are disabled the values are mirrored in the disabled
 *	 registers.  This routine always prints all the L2$ registers in
 *	 order to be as transparent as possible to system configuration.
 *
 * NOTE: for best results this command/routine should be used with the "-N"
 *	 user option to avoid changing the systems error enable registers.
 */
/*ARGSUSED*/
int
n2_debug_print_esrs(mdata_t *mdatap)
{
	uint64_t	offset;
	uint64_t	read_val1, read_val2;
	uint64_t	l2_bank_enabled_val;
	uint64_t	bank_compare;
	int		i;
	char		*fname = "n2_debug_print_esrs";

	cmn_err(CE_CONT, "%s: the error registers for cpuid = %d are:",
	    fname, mdatap->m_cip->c_cpuid);

	/*
	 * Print the L2$ ESRs first.
	 */
	for (i = 0, offset = 0; i < N2_NUM_L2_BANKS; i++,
	    offset += N2_L2_BANK_OFFSET) {
		read_val1 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (N2_L2_ERR_STS_REG + offset),
		    NULL, NULL, NULL);

		read_val2 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (N2_L2_ERR_ENB_REG + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "L2_ESR %x = 0x%08x.%08x, "
		    "L2_EER %x = 0x%08x.%08x\n", i,
		    PRTF_64_TO_32(read_val1), i, PRTF_64_TO_32(read_val2));

		/*
		 * Also print the L2 NotData registers (at 0xAE.0000.0000).
		 */
		read_val2 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (0xae00000000 + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "L2ND_ESR %x = 0x%08x.%08x\n", i,
		    PRTF_64_TO_32(read_val2));
	}

	/*
	 * Print the (enabled) DRAM ESRs next.
	 */
	l2_bank_enabled_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_L2_BANK_EN_STATUS_FULL,
	    NULL, NULL, NULL);
	l2_bank_enabled_val &= N2_L2_BANK_EN_STATUS_ALLEN;

	for (i = 0, offset = 0; i < N2_NUM_DRAM_BRANCHES; i++,
	    offset += N2_DRAM_BRANCH_OFFSET) {
		bank_compare = offset >> 12;
		if (((l2_bank_enabled_val >> bank_compare) & 0x1) != 0) {
			read_val1 = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, (N2_DRAM_CSR_BASE +
			    N2_DRAM_ERROR_STATUS_REG + offset),
			    NULL, NULL, NULL);

			read_val2 = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, (N2_DRAM_CSR_BASE +
			    N2_DRAM_FBD_ERROR_SYND_REG + offset),
			    NULL, NULL, NULL);

			cmn_err(CE_CONT, "DRAM_ESR %d = 0x%08x.%08x "
			    "DRAM_FBD %d = 0x%08x.%08x\n",
			    i, PRTF_64_TO_32(read_val1),
			    i, PRTF_64_TO_32(read_val2));
		}
	}

	/*
	 * Print the internal error regs (CERER, and SETER) next.
	 */
	read_val1 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, 0x10, NULL, NULL);

	read_val2 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, 0x18, NULL, NULL);

	cmn_err(CE_CONT, "CERER = 0x%08x.%08x, SETER = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1), PRTF_64_TO_32(read_val2));

	/*
	 * Print the internal MMU regs (DESR, I-SFSR, and D-SFSR/SFAR) next.
	 */
	read_val1 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, 0x0, NULL, NULL);

	read_val2 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_IMMU, 0x18, NULL, NULL);

	cmn_err(CE_CONT, "DESR = 0x%08x.%08x, ISFSR = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1), PRTF_64_TO_32(read_val2));

	read_val1 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_DMMU, 0x18, NULL, NULL);

	read_val2 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_DMMU, 0x20, NULL, NULL);

	cmn_err(CE_CONT, "DSFSR = 0x%08x.%08x, DSFAR = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1), PRTF_64_TO_32(read_val2));

	/*
	 * Print the SOC ESRs last.
	 */
	read_val1 = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    N2_SOC_ERR_STS_REG, NULL, NULL, NULL);

	read_val2 = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    N2_SOC_ERR_PND_REG, NULL, NULL, NULL);

	cmn_err(CE_CONT, "SOC ESR = 0x%08x.%08x, SOC PESR = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1), PRTF_64_TO_32(read_val2));

	read_val1 = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    N2_SOC_INT_ENB_REG, NULL, NULL, NULL);

	read_val2 = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    N2_SOC_FTL_ENB_REG, NULL, NULL, NULL);

	cmn_err(CE_CONT, "SOC INT = 0x%08x.%08x, SOC FATAL = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1), PRTF_64_TO_32(read_val2));

	/*
	 * Determine the physical address base (range) which the hypervisor
	 * is using for it's displacement flush area (print it out).
	 */
	if ((read_val1 = memtest_hv_util("n2_l2_get_flushbase",
	    (void *)n2_l2_get_flushbase, NULL, NULL, NULL, NULL)) == -1) {
		cmn_err(CE_CONT, "trap to n2_l2_get_flushbase "
		    "FAILED, ret=0x%x\n", (int)read_val1);
		return ((int)read_val1);
	}

	cmn_err(CE_CONT, "HV disp. flushbase = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1));

	return (0);
}

/*
 * This routine enables error traps/reporting and cache ecc/parity checking.
 *
 * Because Niagara-II is a CMT and to minimize system impact, by default the
 * injector will only the set the error checking on the strand that is expected
 * to see the error (this will be determined by higher level framework code).
 *
 * Niagara-II has the following error enable registers:
 *	CERER	- SPARC 8 per processor (one per physical core)
 *	SETER	- SPARC 8 per core (one per strand)
 *	L2$	- 8 per processor (one for each bank)
 *	Fatal	- 1 per processor (enables reset for fatal L2$ errs)
 *	SOC Log	- 1 per processor (enables SOC error logging)
 *	SOC Int	- 1 per processor (enables SOC error interrupts)
 *	SOC Fat	- 1 per processor (enables SOC Fatal errors)
 *	SSI	- 1 per processor (same as Niagara-I)
 *
 * NOTE: the SPARC error enable registers seem to be only available to the
 *	 strand that owns it.  This means each strand involved in the test
 *	 must enable it's own registers separately via the xcall code.
 *
 * NOTE: this routine is NOT setting or checking the PIU error enable
 *	 registers since these are related to the IO errors.
 */
int
n2_enable_errors(mdata_t *mdatap)
{
	uint64_t	exp_l2_set[N2_NUM_L2_BANKS], exp_l2fat_set;
	uint64_t	exp_cerer_set, exp_seter_set, exp_ssi_set;
	uint64_t	exp_soclog_set, exp_socint_set, exp_socfat_set;

	uint64_t	exp_l2_clr[N2_NUM_L2_BANKS], exp_l2fat_clr;
	uint64_t	exp_cerer_clr, exp_seter_clr, exp_ssi_clr;
	uint64_t	exp_soclog_clr, exp_socint_clr, exp_socfat_clr;

	uint64_t	obs_l2_val[N2_NUM_L2_BANKS], obs_l2fat_val;
	uint64_t	obs_cerer_val, obs_seter_val, obs_ssi_val;
	uint64_t	obs_soclog_val, obs_socint_val, obs_socfat_val;

	uint64_t	set_l2_val[N2_NUM_L2_BANKS], set_l2fat_val;
	uint64_t	set_cerer_val, set_seter_val, set_ssi_val;
	uint64_t	set_soclog_val, set_socint_val, set_socfat_val;

	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset;
	int		i;
	char		*fname = "n2_enable_errors";

	/*
	 * Define default (expected) register settings.
	 */
	for (i = 0; i < N2_NUM_L2_BANKS; i++) {
		exp_l2_set[i] = CEEN | NCEEN;
		exp_l2_clr[i] = 0;
	}
	exp_l2fat_set	= N2_L2_FTL_RST_ERREN;
	exp_l2fat_clr	= 0;

	exp_cerer_set	= N2_CERER_ERREN;
	exp_seter_set	= N2_SETER_ERREN;
	exp_ssi_set	= N2_SSI_ERR_CFG_ERREN;
	exp_cerer_clr	= 0;
	exp_seter_clr	= 0;
	exp_ssi_clr	= 0;

	exp_socfat_set	= N2_SOC_FAT_ERREN;
	exp_soclog_set	= exp_socint_set = N2_SOC_ERREN;
	exp_soclog_clr	= exp_socint_clr = exp_socfat_clr = 0;

	DPRINTF(2, "%s: exp_l2_set=0x%llx, exp_cerer_set=0x%llx, "
	    "exp_seter_set=0x%llx, exp_ssi_set=0x%llx, exp_soc_set=0x%llx"
	    "\n", fname, exp_l2_set[0], exp_cerer_set, exp_seter_set,
	    exp_ssi_set, exp_soclog_set);

	/*
	 * Get the current value of each of the L2$ registers.
	 */
	for (i = 0, offset = 0; i < N2_NUM_L2_BANKS; i++,
	    offset += N2_L2_BANK_OFFSET) {
		obs_l2_val[i] = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (N2_L2_ERR_ENB_REG + offset),
		    (uint64_t)n2_debug_buf_pa, NULL, NULL);
	}

	obs_l2fat_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_L2_FTL_RST_REG,
	    (uint64_t)n2_debug_buf_pa, NULL, NULL);

	/*
	 * Get the contents of the other enable registers.
	 */
	obs_cerer_val = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, N2_CERER_REG,
	    (uint64_t)n2_debug_buf_pa, NULL);

	obs_seter_val = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, N2_SETER_REG,
	    (uint64_t)n2_debug_buf_pa, NULL);

	obs_ssi_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_SSI_TIMEOUT,
	    (uint64_t)n2_debug_buf_pa, NULL, NULL);

	obs_soclog_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_SOC_LOG_ENB_REG,
	    (uint64_t)n2_debug_buf_pa, NULL, NULL);

	obs_socint_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_SOC_INT_ENB_REG,
	    (uint64_t)n2_debug_buf_pa, NULL, NULL);

	obs_socfat_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_SOC_FTL_ENB_REG,
	    (uint64_t)n2_debug_buf_pa, NULL, NULL);

	DPRINTF(2, "%s: obs_l2_val=0x%llx, obs_l2fat_val=0x%llx, "
	    "obs_cerer_val=0x%llx, obs_seter_val=0x0x%llx, "
	    "obs_ssi_val=0x%llx, obs_soclog_val=0x%llx "
	    "obs_socint_val=0x%llx, obs_socfat_val=0x%llx\n", fname,
	    obs_l2_val[0], obs_l2fat_val, obs_cerer_val, obs_seter_val,
	    obs_ssi_val, obs_soclog_val, obs_socint_val, obs_socfat_val);

	/*
	 * Determine the register values either specified via command line
	 * options or using a combination of the existing values plus the
	 * bits required to be set minus the bits required to be clear.
	 *
	 * NOTE: Niagara-II has no fields that need to be cleared.
	 */
	for (i = 0, offset = 0; i < N2_NUM_L2_BANKS; i++,
	    offset += N2_L2_BANK_OFFSET) {
		set_l2_val[i] = (obs_l2_val[i] | exp_l2_set[i])
		    & ~exp_l2_clr[i];
	}
	set_l2fat_val	= (obs_l2fat_val | exp_l2fat_set) & ~exp_l2fat_clr;

	set_cerer_val	= (obs_cerer_val | exp_cerer_set) & ~exp_cerer_clr;
	set_seter_val	= (obs_seter_val | exp_seter_set) & ~exp_seter_clr;
	set_ssi_val	= (obs_ssi_val | exp_ssi_set) & ~exp_ssi_clr;

	set_soclog_val	= (obs_soclog_val | exp_soclog_set) & ~exp_soclog_clr;
	set_socint_val	= (obs_socint_val | exp_socint_set) & ~exp_socint_clr;
	set_socfat_val	= (obs_socfat_val | exp_socfat_set) & ~exp_socfat_clr;

	DPRINTF(2, "%s: set_l2_val=0x%llx, set_l2fat_val=0x%llx, "
	    "set_cerer_val=0x%llx, set_seter_val=0x0x%llx, "
	    "set_ssi_val=0x%llx, set_soclog_val=0x%llx "
	    "set_socint_val=0x%llx, set_socfat_val=0x%llx\n", fname,
	    set_l2_val[0], set_l2fat_val, set_cerer_val, set_seter_val,
	    set_ssi_val, set_soclog_val, set_socint_val, set_socfat_val);

	/*
	 * Set and verify the L2 register settings if required.
	 */
	for (i = 0, offset = 0; i < N2_NUM_L2_BANKS; i++,
	    offset += N2_L2_BANK_OFFSET) {
		if (obs_l2_val[i] != set_l2_val[i]) {
			if (F_VERBOSE(iocp)) {
				cmn_err(CE_NOTE, "setting L2 EEN reg %d to "
				    "new value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)\n", i,
				    PRTF_64_TO_32(obs_l2_val[i]),
				    PRTF_64_TO_32(set_l2_val[i]));
			}

			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64,
			    (N2_L2_ERR_ENB_REG + offset), set_l2_val[i],
			    (uint64_t)n2_debug_buf_pa, NULL);
			/*
			 * Verify that the value was set correctly.
			 */
			obs_l2_val[i] = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64,
			    (N2_L2_ERR_ENB_REG + offset),
			    (uint64_t)n2_debug_buf_pa, NULL, NULL);
			if (obs_l2_val[i] != set_l2_val[i]) {
				cmn_err(CE_WARN, "couldn't set L2 reg at offset"
				    " 0x%lx to desired value "
				    "(obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)!\n", offset,
				    PRTF_64_TO_32(obs_l2_val[i]),
				    PRTF_64_TO_32(set_l2_val[i]));
				return (-1);
			}
		}
	}

	/*
	 * Set and verify the L2 fatal register settings if required.
	 */
	if (obs_l2fat_val != set_l2fat_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting L2 fatal register to new "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_l2fat_val),
			    PRTF_64_TO_32(set_l2fat_val));
		}

		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, N2_L2_FTL_RST_REG,
		    set_l2fat_val, (uint64_t)n2_debug_buf_pa, NULL);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_l2fat_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, N2_L2_FTL_RST_REG,
		    (uint64_t)n2_debug_buf_pa, NULL, NULL);
		if (obs_l2fat_val != set_l2fat_val) {
			cmn_err(CE_WARN, "couldn't set L2 fatal reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_l2fat_val),
			    PRTF_64_TO_32(set_l2fat_val));
			return (-1);
		}
	}

	/*
	 * Set and verify the SPARC register settings if required.
	 */
	if (obs_cerer_val != set_cerer_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting CERER register to new value "
			    "(obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_cerer_val),
			    PRTF_64_TO_32(set_cerer_val));
		}

		(void) memtest_hv_util("hv_asi_store64",
		    (void *)hv_asi_store64, ASI_ERROR_STATUS, N2_CERER_REG,
		    set_cerer_val, (uint64_t)n2_debug_buf_pa);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_cerer_val = memtest_hv_util("hv_asi_load64",
		    (void *)hv_asi_load64, ASI_ERROR_STATUS, N2_CERER_REG,
		    (uint64_t)n2_debug_buf_pa, NULL);
		if (obs_cerer_val != set_cerer_val) {
			cmn_err(CE_WARN, "couldn't set CERER reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_cerer_val),
			    PRTF_64_TO_32(set_cerer_val));
			return (-1);
		}
	}

	if (obs_seter_val != set_seter_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting SETER register to new value "
			    "(obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_seter_val),
			    PRTF_64_TO_32(set_seter_val));
		}

		(void) memtest_hv_util("hv_asi_store64",
		    (void *)hv_asi_store64, ASI_ERROR_STATUS, N2_SETER_REG,
		    set_seter_val, (uint64_t)n2_debug_buf_pa);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_seter_val = memtest_hv_util("hv_asi_load64",
		    (void *)hv_asi_load64, ASI_ERROR_STATUS, N2_SETER_REG,
		    (uint64_t)n2_debug_buf_pa, NULL);
		if (obs_seter_val != set_seter_val) {
			cmn_err(CE_WARN, "couldn't set SETER reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_seter_val),
			    PRTF_64_TO_32(set_seter_val));
			return (-1);
		}
	}

	/*
	 * Set and verify the SSI register settings if required.
	 */
	if (obs_ssi_val != set_ssi_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting SSI register to new value "
			    "(obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_ssi_val),
			    PRTF_64_TO_32(set_ssi_val));
		}

		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, N2_SSI_TIMEOUT,
		    set_ssi_val, (uint64_t)n2_debug_buf_pa, NULL);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_ssi_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, N2_SSI_TIMEOUT,
		    (uint64_t)n2_debug_buf_pa, NULL, NULL);
		if (obs_ssi_val != set_ssi_val) {
			cmn_err(CE_WARN, "couldn't set SSI reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_ssi_val),
			    PRTF_64_TO_32(set_ssi_val));
			return (-1);
		}
	}

	/*
	 * Set and verify the SOC register settings if required.
	 */
	if (obs_soclog_val != set_soclog_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting SOC log register to new "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_soclog_val),
			    PRTF_64_TO_32(set_soclog_val));
		}

		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, N2_SOC_LOG_ENB_REG,
		    set_soclog_val, (uint64_t)n2_debug_buf_pa, NULL);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_soclog_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, N2_SOC_LOG_ENB_REG,
		    (uint64_t)n2_debug_buf_pa, NULL, NULL);
		if (obs_soclog_val != set_soclog_val) {
			cmn_err(CE_WARN, "couldn't set SOC log reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_soclog_val),
			    PRTF_64_TO_32(set_soclog_val));
			return (-1);
		}
	}

	if (obs_socint_val != set_socint_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting SOC int register to new "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_socint_val),
			    PRTF_64_TO_32(set_socint_val));
		}

		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, N2_SOC_INT_ENB_REG,
		    set_socint_val, (uint64_t)n2_debug_buf_pa, NULL);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_socint_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, N2_SOC_INT_ENB_REG,
		    (uint64_t)n2_debug_buf_pa, NULL, NULL);
		if (obs_socint_val != set_socint_val) {
			cmn_err(CE_WARN, "couldn't set SOC int reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_socint_val),
			    PRTF_64_TO_32(set_socint_val));
			return (-1);
		}
	}

	if (obs_socfat_val != set_socfat_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting SOC fatal register to new "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_socfat_val),
			    PRTF_64_TO_32(set_socfat_val));
		}

		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, N2_SOC_FTL_ENB_REG,
		    set_socfat_val, (uint64_t)n2_debug_buf_pa, NULL);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_socfat_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, N2_SOC_FTL_ENB_REG,
		    (uint64_t)n2_debug_buf_pa, NULL, NULL);
		if (obs_socfat_val != set_socfat_val) {
			cmn_err(CE_WARN, "couldn't set SOC fatal reg to desired"
			    " value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_socfat_val),
			    PRTF_64_TO_32(set_socfat_val));
			return (-1);
		}
	}
	return (0);
}

/*
 * This routine allows the Niagara-II caches to be flushed from a call through
 * the opsvec table.  Flushing the L2 cache also flushes the L1 caches since
 * the Niagara-II caches are inclusive (as were Niagara-I).
 *
 * NOTE: this routine is NOT doing a displacement flush as was done on
 *	 Niagara-I, it's using the new prefetch-ICE instruction as this
 *	 is the preferred method to use as the default flush routine.
 */
/* ARGSUSED */
int
n2_flushall_l2_hvmode(cpu_info_t *cip)
{
	int	ret;

	if ((ret = memtest_hv_util("n2_l2_flushall_ice",
	    (void *)n2_l2_flushall_ice, NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "n2_flushall_caches: trap to n2_l2_flushall_ice "
		    "FAILED, ret=0x%x\n", ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine allows the Niagara-II caches to be flushed while remaining in
 * kernel mode (for the actual displacements).  This is used for write-back
 * tests that require the flush/displacement to occur in kernel mode.
 *
 * Flushing the L2 cache also flushes the L1 caches since the Niagara caches
 * are inclusive.
 *
 * Note that the flush address used is the displacement region used by
 * the kernel.  It is a real address (treated as a physical address by
 * sun4u) and so must be accessed via ASI 0x14 (ASI_MEM).
 *
 * WARNING: this routine can cause panics because Niagara-II does not
 *	    support locking TTEs in the TLB.  Cache flushing should be
 *	    performed with one of the hyperpriv routines, OR the injector
 *	    option "-Q pause" can be used to avoid the panics.
 *
 * NOTE: the TTE format has changed between Niagara-I and Niagara-II, so the
 *	 routine to install the tte had to change to accomodate Niagara-II.
 *
 * NOTE: mapping the flush region using the hcall hv_mmu_map_perm_addr()
 *	 does not work because it can't do REAL address mappings and there
 *	 is no virtual mapping for the ecache_flushaddr.
 */
/* ARGSUSED */
int
n2_flushall_l2_kmode(cpu_info_t *cip)
{
	caddr_t		kern_disp_addr = (caddr_t)ecache_flushaddr;
	uint64_t	hv_disp_addr;
	int		ret;
	char		*fname = "n2_flushall_l2_kmode";

	DPRINTF(3, "%s: doing L2 flush (DM mode) with "
	    "displacement flush raddr=0x%llx, cachesize=0x%x, "
	    "sublinesize=0x%x\n", fname, kern_disp_addr,
	    cip->c_l2_flushsize, cip->c_l2_sublinesize);

	/*
	 * Determine the physical address base (range) which the hypervisor
	 * is using for it's displacement flush area.
	 */
	if ((hv_disp_addr = memtest_hv_util("n2_l2_get_flushbase",
	    (void *)n2_l2_get_flushbase, NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to n2_l2_get_flushbase "
		    "FAILED, ret=0x%x\n", fname, hv_disp_addr);
		return (hv_disp_addr);
	}

	if ((ret = memtest_hv_util("n2_l2_enable_DM", (void *)n2_l2_enable_DM,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to n2_l2_enable_DM "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * Unfortunately in order for the kernel to use a real address
	 * directly the MMU has to be involved (even for ASI 0x14).  So
	 * a dTLB RA->PA entry is installed for the displacement flush area.
	 */
	if ((ret = memtest_hv_util("n2_install_tlb_entry",
	    (void *)n2_install_tlb_entry, (uint64_t)kern_disp_addr,
	    hv_disp_addr, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to n2_install_tlb_entry "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * Call asm routine to do the actual flush.
	 */
	n2_l2_flushall_kmode_asm(kern_disp_addr, cip->c_l2_flushsize,
	    cip->c_l2_sublinesize);

	if ((ret = memtest_hv_util("n2_l2_disable_DM", (void *)n2_l2_disable_DM,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to n2_l2_disable_DM "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine allows a single Niagara-II L2$ cache entry to be flushed from
 * a call through the opsvec table using a displacement flush.  The flush is
 * performed in HV mode.
 *
 * NOTE: the trap to determine if index hashing (IDX) is enabled uses
 *	 instructions that will affect the caches.  Perhaps the IDX state
 *	 can be determined as part of the pre-test setup so this does not
 *	 need to be done here.
 */
/* ARGSUSED */
int
n2_flush_l2_entry_hvmode(cpu_info_t *cip, caddr_t paddr)
{
	uint64_t	idx_paddr;
	int		ret;
	char		*fname = "n2_flush_l2_entry_hvmode";

	DPRINTF(3, "%s: doing L2 entry displacement flush, "
	    "HV mode\n", fname);

	/*
	 * Determine if L2 cache index hashing (IDX) is enabled and
	 * call the appropriate routine.
	 */
	if (n2_check_l2_idx_mode((uint64_t)paddr, &idx_paddr) == 0) {
		if ((ret = memtest_hv_util("n2_l2_flushentry",
		    (void *)n2_l2_flushentry, (uint64_t)paddr,
		    NULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: trap to n2_l2_flushentry "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		if ((ret = memtest_hv_util("n2_l2_flushidx",
		    (void *)n2_l2_flushidx, idx_paddr,
		    NULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: trap to n2_l2_flushidx "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	return (0);
}

/*
 * This routine allows a single Niagara-II L2$ cache entry to be flushed
 * using the prefectch-ICE (Invalidate L2 Cache Entry) instruction.  The
 * index to send to the instruction is affected by L2$ index hashing
 * which is determined here.  The flush is performed in HV mode.
 *
 * NOTE: the trap to determine if index hashing (IDX) is enabled uses
 *	 instructions that will affect the caches.  Perhaps the IDX state
 *	 can be determined as part of the pre-test setup so this does not
 *	 need to be done here.
 */
/* ARGSUSED */
int
n2_flush_l2_entry_ice(cpu_info_t *cip, caddr_t paddr)
{
	uint64_t	idx_paddr;
	int		ret;
	char		*fname = "n2_flush_l2_entry_ice";

	DPRINTF(3, "%s: doing L2 entry cache invalidate flush, "
	    "HV mode\n", fname);

	/*
	 * Determine if L2 cache index hashing (IDX) is enabled, note
	 * that both routines use the non hashed address regardless.
	 */
	if (n2_check_l2_idx_mode((uint64_t)paddr, &idx_paddr) == 0) {
		if ((ret = memtest_hv_util("n2_l2_flushentry_ice",
		    (void *)n2_l2_flushentry_ice, (uint64_t)paddr,
		    NULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: trap to n2_l2_flushentry_ice "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
#ifdef	L2_DEBUG_BUFFER
		n2_debug_init();
#endif
		if ((ret = memtest_hv_util("n2_l2_flushidx_ice",
		    (void *)n2_l2_flushidx_ice, (uint64_t)paddr,
		    NULL, NULL, (uint64_t)n2_debug_buf_pa)) == -1) {
			DPRINTF(0, "%s: trap to n2_l2_flushidx_ice "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
#ifdef	L2_DEBUG_BUFFER
		n2_debug_dump();
#endif
	}

	return (0);
}

/*
 * This routine allows a single Niagara-II L2$ cache entry to be flushed from
 * a call through the opsvec table using a displacement flush.  The flush is
 * performed in kernel (not hyperpriv) mode.
 *
 * NOTE: this routine makes use of the fact that the injector installs
 *	 data in the L2-cache while the L2-cache is in DM mode.  This means
 *	 instead of flushing all 16 ways in the cache only the one location
 *	 is flushed for either the IDX'd or non-IDX'd address.
 *
 * NOTE: a number of lines of code are being run before the actual flush
 *	 so there's a high probability that the data in the cache will be
 *	 knocked out before reaching the load instruction.
 */
/* ARGSUSED */
int
n2_flush_l2_entry_kmode(cpu_info_t *cip, caddr_t orig_raddr)
{
	caddr_t		kern_disp_addr = (caddr_t)ecache_flushaddr;
	uint64_t	hv_disp_addr;
	uint64_t	raddr = (uint64_t)orig_raddr;
	uint64_t	idx_raddr;
	int		ret;
	char		*fname = "n2_flush_l2_entry_kmode";

	DPRINTF(3, "%s: doing L2 entry displacement flush, kernel mode "
	    "with flush raddr=0x%llx\n", fname, kern_disp_addr);

	/*
	 * Determine the physical address base (range) which the hypervisor
	 * is using for its displacement flush area.
	 */
	if ((hv_disp_addr = memtest_hv_util("n2_l2_get_flushbase",
	    (void *)n2_l2_get_flushbase, NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to n2_l2_get_flushbase "
		    "FAILED, ret=0x%x\n", fname, hv_disp_addr);
		return (hv_disp_addr);
	}

	if ((ret = memtest_hv_util("n2_l2_enable_DM", (void *)n2_l2_enable_DM,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to n2_l2_enable_DM "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * Unfortunately in order for the kernel to use a real address
	 * directly the MMU has to be involved (even for ASI 0x14).  So
	 * a dTLB RA->PA entry is installed for the displacement flush area.
	 */
	if ((ret = memtest_hv_util("n2_install_tlb_entry",
	    (void *)n2_install_tlb_entry, (uint64_t)kern_disp_addr,
	    hv_disp_addr, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to n2_install_tlb_entry "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * Do the actual flush of either the IDX'd or the original address,
	 * by masking the address (retaining PA[21:6] and adding the flushbase.
	 * Note that the ldphys() routine uses ASI_REAL_MEM for the flush.
	 */
	if (n2_check_l2_idx_mode((uint64_t)raddr, &idx_raddr) == 0) {
		raddr = (raddr & 0x3fffc0) + (uint64_t)kern_disp_addr;
		(void) ldphys(raddr);
	} else {
		idx_raddr = (idx_raddr & 0x3fffc0) + (uint64_t)kern_disp_addr;
		(void) n2_check_l2_idx_mode(idx_raddr, &idx_raddr);
		(void) ldphys(idx_raddr);
	}

	if ((ret = memtest_hv_util("n2_l2_disable_DM", (void *)n2_l2_disable_DM,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to n2_l2_disable_DM "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	return (0);
}

/*
 * Initialize the cpu_info struct with the processor specific data.
 *
 * The initialization is done locally instead of using global kernel
 * variables and structures to reduce the dependence on specific kernel
 * versions.
 */
int
n2_get_cpu_info(cpu_info_t *cip)
{
	cip->c_dc_size = 8 * KB;
	cip->c_dc_linesize = 16;
	cip->c_dc_assoc = 4;

	cip->c_ic_size = 16 * KB;
	cip->c_ic_linesize = 32;
	cip->c_ic_assoc = 8;

	cip->c_l2_size = 4 * MB;
	cip->c_l2_sublinesize = 64;
	cip->c_l2_linesize = cip->c_l2_sublinesize *
	    (cip->c_l2_size / (1 * MB));
	cip->c_l2_assoc = 16;
	cip->c_l2_flushsize = cip->c_l2_size; /* thanks to on the fly DM mode */
	cip->c_mem_flags = 0;

	/*
	 * XXX	since the cpuid is per virtual core on sun4v, this may no
	 *	longer be the way the physical memory is separated (is using
	 *	raddrs). However the N1 code works reliably as-is below.
	 *
	 * Note that bit 39 determines mem/io, and bits 38:37 are set to 0
	 * for cpu requests to memory.  So bit 36 should be correct.
	 */
	cip->c_mem_start = (uint64_t)cip->c_cpuid << 36;
	cip->c_mem_size = 1ULL << 36;

	return (0);
}

void
n2_init(mdata_t *mdatap)
{
	mdatap->m_sopvp = &niagara2_vops;
	mdatap->m_copvp = &niagara2_cops;
	mdatap->m_cmdpp = commands;

	/*
	 * Determine the paddr of the n2_debug_buf to pass into the asm
	 * injection routines which run in hyperpriv mode.  Note that the
	 * first translation produces the raddr.
	 */
	n2_debug_buf_pa = memtest_kva_to_ra((void *)n2_debug_buf_va);
	n2_debug_buf_pa = memtest_ra_to_pa((uint64_t)n2_debug_buf_pa);
}

/*
 * This routine traps to the hypervisor test case to ensure argument
 * passing works correctly on the system using the hyperprivileged API.
 */
/*ARGSUSED*/
int
n2_inject_test_case(mdata_t *mdatap)
{
	int	ret;
	char	*fname = "n2_inject_test_case";

	/*
	 * Trap to hypervisor.
	 */
	if ((ret = memtest_hv_inject_error("memtest_hv_trap_check_asm",
	    (void *)memtest_hv_trap_check_asm, 0xa, 0xb, 0xc, 0xd)) == -1) {
		DPRINTF(0, "\n\t%s: The trap to hypervisor mode FAILED,"
		    "\n\tthe most likely cause is that the"
		    "\n\thypervisor API services are not enabled,"
		    "\n\tthese are required by the error injector."
		    "\n\tPlease contact the FW or EI groups"
		    "\n\tfor more information.", fname);
		return (ret);
	}

	if (ret == 0xa55) {
		DPRINTF(0, "%s PASSED: ret = 0x%x\n", fname, ret);
	} else {
		DPRINTF(0, "\n\t%s: Hypervisor argument passing FAILED,"
		    "\n\tthe most likely cause is that the"
		    "\n\thypervisor API services are not enabled,"
		    "\n\tthese are required by the error injector."
		    "\n\tPlease contact the FW or EI groups"
		    "\n\tfor more information.", fname);
	}

	return (0);
}

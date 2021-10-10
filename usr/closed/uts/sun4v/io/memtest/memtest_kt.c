/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains Rainbow Falls (UltraSPARC-T3 aka KT) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_v.h>
#include <sys/memtestio_ni.h>
#include <sys/memtestio_n2.h>
#include <sys/memtestio_vf.h>
#include <sys/memtestio_kt.h>
#include <sys/memtest_v.h>
#include <sys/memtest_ni.h>
#include <sys/memtest_n2.h>
#include <sys/memtest_vf.h>
#include <sys/memtest_kt.h>
#include <sys/memtest_v_asm.h>
#include <sys/memtest_n2_asm.h>
#include <sys/memtest_vf_asm.h>
#include <sys/memtest_kt_asm.h>

/*
 * Static routines located in this file (for foreign/remote errors).
 */
static int	kt_pc_err(mdata_t *);
static void	kt_producer(mdata_t *);
static int	kt_consumer(mdata_t *);

static int	kt_l2_producer(mdata_t *);
static int	kt_l2_consumer(mdata_t *);

static int	kt_mem_producer(mdata_t *);
static int	kt_mem_consumer(mdata_t *);

/*
 * Debug buffer passed to some assembly routines.  Must be the PA for
 * routines which run in hyperprivileged mode.
 */
uint64_t	kt_debug_buf_va[DEBUG_BUF_SIZE];
uint64_t	kt_debug_buf_pa;

/*
 * Enable/disable the storing of DEBUG values into the debug mem buffer
 * for different test classes.
 */
/* #define	MEM_DEBUG_BUFFER	1 */
/* #define	L1_DEBUG_BUFFER		1 */
/* #define	L2_DEBUG_BUFFER		1 */

/*
 * K/T operations vector tables.
 */
static opsvec_v_t kt_vops = {
	/* sun4v injection ops vectors */
	n2_inject_hvdcache,	/* corrupt d$ data or tag in hv mode (N2) */
	kt_inject_hvicache,	/* corrupt i$ data or tag in hv mode */
	kt_inject_l2buf,	/* corrupt l2$ directory/buffer at raddr */
	kt_inject_l2nd,		/* install l2$ NotData at raddr */
	kt_inject_l2vads,	/* corrupt l2$ V(U)AD bits at raddr or offset */
	n2_inject_mamem,	/* corrupt MA memory parity (N2) */
	kt_inject_memory,	/* install memory NotData at raddr */
	kt_inject_memory,	/* corrupt local memory */

	/* sun4v support ops vectors */
	n2_access_cwq,		/* access via CWQ op (N2) */
	n2_access_mamem,	/* access MA memory (N2) */
	kt_check_l2_idx_mode,	/* check/convert addresses for index hashing */
	kt_flushall_l2_hvmode,	/* flush all l2$ (inclusive) in hv mode */
	kt_flush_l2_entry_hvmode, /* disp flush single l2$ entry in hv mode */
};

static opsvec_c_t kt_cops = {
	/* common injection ops vectors */
	n2_inject_dcache,	/* corrupt d$ data or tag at raddr (N2) */
	n2_inject_dphys,	/* corrupt d$ data or tag at offset (N2) */
	kt_inject_freg_file,	/* corrupt FP register file */
	kt_inject_icache,	/* corrupt i$ data or tag at raddr */
	notsup,			/* no corrupt internal */
	n2_inject_iphys,	/* corrupt i$ data or tag at offset (N2) */
	n2_inject_ireg_file,	/* corrupt integer register file (N2) */
	kt_inject_l2cache,	/* corrupt l2$ data or tag at raddr */
	kt_inject_l2phys,	/* corrupt l2$ data or tag at offset */
	notsup,			/* no corrupt l3$ data or tag at raddr */
	notsup,			/* no corrupt l3$ data or tag at offset */
	n2_inject_tlb,		/* I-D TLB parity errors (N2) */

	/* common support ops vectors */
	notimp,			/* FP access performed by injection routine */
	ni_access_ireg_file,	/* access integer register file (using N1) */
	notimp,			/* check ESRs */
	kt_enable_errors,	/* enable AFT errors */
	kt_control_scrub,	/* enable/disable memory scrubbers */
	kt_get_cpu_info,	/* put cpu info into struct */
	kt_flushall_caches,	/* flush all caches in hv mode */
	n2_clearall_dcache,	/* clear (not just flush) all d$ in hv mode */
	n2_clearall_icache,	/* clear (not just flush) all i$ in hv mode */
	kt_flushall_l2_kmode,	/* flush all l2$ (inclusive) in kern mode */
	notsup,			/* no flush all l3$ */
	notsup,			/* no flush single d$ entry */
	notsup,			/* no flush single i$ entry */
	kt_flush_l2_entry_kmode, /* disp flush single l2$ entry in kern mode */
	notsup,			/* no flush single l3$ entry */
};

/*
 * These K/T error commands are grouped according to the definitions
 * in the memtestio_kt.h and memtestio_n2.h header files.
 *
 * This is a complete list of the commands available on K/T, so no
 * reference needs to be made to the command table of any other processor.
 */
cmd_t kt_cmds[] = {
	/* Memory (DRAM) uncorrectable errors. */
	NI_HD_DAU,	memtest_h_mem_err,	"memtest_h_mem_err",
	KT_HD_DAUINT,	kt_inject_memory_int,	"kt_inject_memory_int",
	NI_HD_DAUMA,	memtest_h_mem_err,	"memtest_h_mem_err",
	N2_HD_DAUCWQ,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_HI_DAU,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_KD_DAU,	memtest_k_mem_err,	"memtest_k_mem_err",
	N2_KD_DAUDTLB,	kt_inject_tlb_mem_miss,	"kt_inject_tlb_mem_miss",
	N2_KI_DAUITLB,	kt_inject_tlb_mem_miss,	"kt_inject_tlb_mem_miss",
	NI_KD_DAUTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
/*	NI_KD_DAUPR,	memtest_k_mem_err,	"memtest_k_mem_err", */
	NI_KI_DAU,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KI_DAUTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_UD_DAU,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_UI_DAU,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_KD_DSU,	kt_inject_memory_scrub,	"kt_inject_memory_scrub",
	NI_KD_DBU,	ni_inject_memory_range,	"ni_inject_memory_range",
	NI_IO_DRU,	memtest_u_mem_err,	"memtest_u_mem_err",

	/* Memory (DRAM) correctable errors. */
	NI_HD_DAC,	memtest_h_mem_err,	"memtest_h_mem_err",
	KT_HD_DACINT,	kt_inject_memory_int,	"kt_inject_memory_int",
	NI_HD_DACMA,	memtest_h_mem_err,	"memtest_h_mem_err",
	N2_HD_DACCWQ,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_HI_DAC,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_KD_DAC,	memtest_k_mem_err,	"memtest_k_mem_err",
	N2_KD_DACDTLB,	kt_inject_tlb_mem_miss,	"kt_inject_tlb_mem_miss",
	N2_KI_DACITLB,	kt_inject_tlb_mem_miss,	"kt_inject_tlb_mem_miss",
	NI_KD_DACTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
/*	NI_KD_DACPR,	memtest_k_mem_err,	"memtest_k_mem_err", */
	NI_KD_DACSTORM,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KI_DAC,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KI_DACTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_UD_DAC,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_UI_DAC,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_KD_DSC,	kt_inject_memory_scrub,	"kt_inject_memory_scrub",
	NI_IO_DRC,	memtest_u_mem_err,	"memtest_u_mem_err",

	/* Memory (DRAM) NotData errors. */
	KT_HD_MEMND,	memtest_h_mem_err,	"memtest_h_mem_err",
	KT_HD_MEMNDMA,	memtest_h_mem_err,	"memtest_h_mem_err",
	KT_HD_MEMNDCWQ,	memtest_h_mem_err,	"memtest_h_mem_err",
	KT_HI_MEMND,	memtest_h_mem_err,	"memtest_h_mem_err",
	KT_KD_MEMNDDTLB, kt_inject_tlb_mem_miss, "kt_inject_tlb_mem_miss",
	KT_KI_MEMNDITLB, kt_inject_tlb_mem_miss, "kt_inject_tlb_mem_miss",
	KT_KD_MEMND,	memtest_k_nd_err,	"memtest_k_nd_err",
	KT_KD_MEMNDTL1,	memtest_k_nd_err,	"memtest_k_nd_err",
	KT_KI_MEMND,	memtest_k_nd_err,	"memtest_k_nd_err",
	KT_KI_MEMNDTL1,	memtest_k_nd_err,	"memtest_k_nd_err",
	KT_UD_MEMND,	memtest_u_nd_err,	"memtest_u_nd_err",
	KT_UI_MEMND,	memtest_u_nd_err,	"memtest_u_nd_err",
	KT_KD_MEMNDSC,	kt_inject_memory_scrub,	"kt_inject_memory_scrub",
	KT_IO_MEMND,	memtest_u_nd_err,	"memtest_u_nd_err",
	KT_MNDPHYS,	memtest_mphys,		"memtest_mphys",

	/* Remote memory (DRAM) uncorrectable errors */
	VF_HD_FDAU,	kt_pc_err,		"kt_pc_err",
	VF_HD_FDAUMA,	kt_pc_err,		"kt_pc_err",
	VF_HD_FDAUCWQ,	kt_pc_err,		"kt_pc_err",
	VF_KD_FDAU,	kt_pc_err,		"kt_pc_err",
	VF_KD_FDAUTL1,	kt_pc_err,		"kt_pc_err",
	VF_KI_FDAU,	kt_pc_err,		"kt_pc_err",
	VF_KI_FDAUTL1,	kt_pc_err,		"kt_pc_err",

	/* Remote memory (DRAM) correctable errors */
	VF_HD_FDAC,	kt_pc_err,		"kt_pc_err",
	VF_HD_FDACMA,	kt_pc_err,		"kt_pc_err",
	VF_HD_FDACCWQ,	kt_pc_err,		"kt_pc_err",
	VF_KD_FDAC,	kt_pc_err,		"kt_pc_err",
	VF_KD_FDACTL1,	kt_pc_err,		"kt_pc_err",
	VF_KD_FDACSTORM, kt_pc_err,		"kt_pc_err",
	VF_KI_FDAC,	kt_pc_err,		"kt_pc_err",
	VF_KI_FDACTL1,	kt_pc_err,		"kt_pc_err",

	/* Remote memory (DRAM) "NotData" errors */
	KT_HD_MFRND,	kt_pc_err,		"kt_pc_err",
	KT_HD_MFRNDMA,	kt_pc_err,		"kt_pc_err",
	KT_HD_MFRNDCWQ,	kt_pc_err,		"kt_pc_err",
	KT_KD_MFRND,	kt_pc_err,		"kt_pc_err",
	KT_KD_MFRNDTL1,	kt_pc_err,		"kt_pc_err",
	KT_KI_MFRND,	kt_pc_err,		"kt_pc_err",
	KT_KI_MFRNDTL1,	kt_pc_err,		"kt_pc_err",

	/* L2 cache data uncorrectable errors. */
	/*
	 * NOTE: KT does not have a HW L2 scrubber,
	 *	 KT does not detect L2 error on prefetch access,
	 *	 IO transactions do not allocate in the KT L2.
	 */
	NI_HD_LDAU,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_HD_LDAUMA,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HD_LDAUCWQ,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_HI_LDAU,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_KD_LDAU,	memtest_k_l2_err,	"memtest_k_l2_err",
	N2_KD_LDAUDTLB,	kt_inject_tlb_l2_miss,	"kt_inject_tlb_l2_miss",
	N2_KI_LDAUITLB,	kt_inject_tlb_l2_miss,	"kt_inject_tlb_l2_miss",
	NI_LDAUCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	NI_KD_LDAUTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
/*	KT NI_KD_LDAUPR, memtest_k_l2_err,	"memtest_k_l2_err", */
	N2_HD_LDAUPRI,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_OBP_LDAU,	memtest_obp_err,	"memtest_obp_err",
	NI_KI_LDAU,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KI_LDAUTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_UD_LDAU,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_UI_LDAU,	memtest_u_l2_err,	"memtest_u_l2_err",
/*	NI_KD_LDSU,	n2_inject_l2_scrub,	"n2_inject_l2_scrub", */
/*	NI_IO_LDRU,	memtest_u_l2_err,	"memtest_u_l2_err", */

	/* L2 cache data correctable errors. */
	NI_HD_LDAC,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_HD_LDACMA,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HD_LDACCWQ,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_HI_LDAC,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_KD_LDAC,	memtest_k_l2_err,	"memtest_k_l2_err",
	N2_KD_LDACDTLB,	kt_inject_tlb_l2_miss,	"kt_inject_tlb_l2_miss",
	N2_KI_LDACITLB,	kt_inject_tlb_l2_miss,	"kt_inject_tlb_l2_miss",
	NI_LDACCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	NI_KD_LDACTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
/*	KT NI_KD_LDACPR, memtest_k_l2_err,	"memtest_k_l2_err", */
	N2_HD_LDACPRI,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_OBP_LDAC,	memtest_obp_err,	"memtest_obp_err",
	NI_KI_LDAC,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KI_LDACTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_UD_LDAC,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_UI_LDAC,	memtest_u_l2_err,	"memtest_u_l2_err",
/*	NI_KD_LDSC,	n2_inject_l2_scrub,	"n2_inject_l2_scrub", */
/*	NI_IO_LDRC,	memtest_u_l2_err,	"memtest_u_l2_err", */

	/* L2 cache tag fatal and correctable errors. */
	KT_HD_LDTF,	memtest_h_l2_err,	"memtest_h_l2_err",
	KT_HI_LDTF,	memtest_h_l2_err,	"memtest_h_l2_err",
	KT_KD_LDTF,	memtest_k_l2_err,	"memtest_k_l2_err",
	KT_KD_LDTFTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	KT_KI_LDTF,	memtest_k_l2_err,	"memtest_k_l2_err",
	KT_KI_LDTFTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	KT_UD_LDTF,	memtest_u_l2_err,	"memtest_u_l2_err",
	KT_UI_LDTF,	memtest_u_l2_err,	"memtest_u_l2_err",

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
/*	NI_L2SCRUBPHYS,	n2_inject_l2_scrub,	"n2_inject_l2_scrub", */
	NI_K_L2VIRT,	memtest_k_l2virt,	"memtest_k_l2virt",
	NI_U_L2VIRT,	memtest_k_l2virt,	"memtest_k_l2virt",

	/* L2 cache NotData errors. */
	N2_HD_L2ND,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HD_L2NDMA,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HD_L2NDCWQ,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HI_L2ND,	memtest_h_l2_err,	"memtest_k_l2_err",
	N2_KD_L2ND,	memtest_k_l2nd_err,	"memtest_k_l2nd_err",
	N2_KD_L2NDDTLB,	kt_inject_tlb_l2_miss,	"kt_inject_tlb_l2_miss",
	N2_KI_L2NDITLB,	kt_inject_tlb_l2_miss,	"kt_inject_tlb_l2_miss",
/*	N2_L2NDCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err", */
	N2_KD_L2NDTL1,	memtest_k_l2nd_err,	"memtest_k_l2nd_err",
/*	N2_KD_L2NDPR,	memtest_k_l2nd_err,	"memtest_k_l2nd_err", */
	N2_HD_L2NDPRI,	memtest_h_l2_err,	"memtest_h_l2_err",
/*	N2_OBP_L2ND,	memtest_obp_err,	"memtest_obp_err", */
	N2_KI_L2ND,	memtest_k_l2nd_err,	"memtest_k_l2nd_err",
	N2_KI_L2NDTL1,	memtest_k_l2nd_err,	"memtest_k_l2nd_err",
	N2_UD_L2ND,	memtest_u_nd_err,	"memtest_u_nd_err",
	N2_UI_L2ND,	memtest_u_nd_err,	"memtest_u_nd_err",

	N2_HD_L2NDWB,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	N2_HI_L2NDWB,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	N2_IO_L2ND,	memtest_u_nd_err,	"memtest_u_nd_err",
	N2_L2NDPHYS,	kt_inject_l2nd,		"kt_inject_l2nd",

	/* Remote L2 cache data uncorrectable errors. */
	VF_HD_L2CBU,	kt_pc_err,		"kt_pc_err",
	VF_HD_L2CBUMA,	kt_pc_err,		"kt_pc_err",
	VF_HD_L2CBUCWQ,	kt_pc_err,		"kt_pc_err",
	VF_HD_L2CBUPRI,	kt_pc_err,		"kt_pc_err",
	VF_KD_L2CBU,	kt_pc_err,		"kt_pc_err",
	VF_KD_L2CBUTL1,	kt_pc_err,		"kt_pc_err",
	VF_KI_L2CBU,	kt_pc_err,		"kt_pc_err",
	VF_KI_L2CBUTL1,	kt_pc_err,		"kt_pc_err",

	/* Remote L2 cache data correctable errors. */
	VF_HD_L2CBC,	kt_pc_err,		"kt_pc_err",
	VF_HD_L2CBCMA,	kt_pc_err,		"kt_pc_err",
	VF_HD_L2CBCCWQ,	kt_pc_err,		"kt_pc_err",
	VF_HD_L2CBCPRI,	kt_pc_err,		"kt_pc_err",
	VF_KD_L2CBC,	kt_pc_err,		"kt_pc_err",
	VF_KD_L2CBCTL1,	kt_pc_err,		"kt_pc_err",
	VF_KI_L2CBC,	kt_pc_err,		"kt_pc_err",
	VF_KI_L2CBCTL1,	kt_pc_err,		"kt_pc_err",

	/* Remote L2 cache data "NotData" errors. */
	KT_HD_L2FRND,	 kt_pc_err,		"kt_pc_err",
	KT_HD_L2FRNDMA,	 kt_pc_err,		"kt_pc_err",
	KT_HD_L2FRNDCWQ, kt_pc_err,		"kt_pc_err",
	KT_HD_L2FRNDPRI, kt_pc_err,		"kt_pc_err",
	KT_KD_L2FRND,	 kt_pc_err,		"kt_pc_err",
	KT_KD_L2FRNDTL1, kt_pc_err,		"kt_pc_err",
	KT_KI_L2FRND,	 kt_pc_err,		"kt_pc_err",
	KT_KI_L2FRNDTL1, kt_pc_err,		"kt_pc_err",

	/* Remote L2 cache write-back uncorrectable errors. */
	VF_HD_LWBU,	kt_pc_err,		"kt_pc_err",
	VF_HI_LWBU,	kt_pc_err,		"kt_pc_err",
	VF_KD_LWBU,	kt_pc_err,		"kt_pc_err",
	VF_KI_LWBU,	kt_pc_err,		"kt_pc_err",

	/* L2 cache V(U)AD uncorrectable (fatal) errors. */
	N2_HD_LVF_VD,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_HI_LVF_VD,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_KD_LVF_VD,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_KI_LVF_VD,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_UD_LVF_VD,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	N2_UI_LVF_VD,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	KT_HD_LVF_D,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	KT_HI_LVF_D,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	KT_KD_LVF_D,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	KT_KI_LVF_D,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	KT_UD_LVF_D,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	KT_UI_LVF_D,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	N2_HD_LVF_UA,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_HI_LVF_UA,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_KD_LVF_UA,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_KI_LVF_UA,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_UD_LVF_UA,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	N2_UI_LVF_UA,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	KT_HD_LVF_S,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	KT_HI_LVF_S,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	KT_KD_LVF_S,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	KT_KI_LVF_S,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	KT_UD_LVF_S,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	KT_UI_LVF_S,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	/* L2 cache V(U)AD correctable errors. */
	N2_HD_LVC_VD,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_HI_LVC_VD,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_KD_LVC_VD,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_KI_LVC_VD,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_UD_LVC_VD,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	N2_UI_LVC_VD,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	KT_HD_LVC_D,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	KT_HI_LVC_D,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	KT_KD_LVC_D,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	KT_KI_LVC_D,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	KT_UD_LVC_D,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	KT_UI_LVC_D,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	N2_HD_LVC_UA,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_HI_LVC_UA,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	N2_KD_LVC_UA,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_KI_LVC_UA,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	N2_UD_LVC_UA,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	N2_UI_LVC_UA,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	KT_HD_LVC_S,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	KT_HI_LVC_S,	memtest_h_l2vad_err,	"memtest_h_l2vad_err",
	KT_KD_LVC_S,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	KT_KI_LVC_S,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	KT_UD_LVC_S,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	KT_UI_LVC_S,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	/* L2 cache V(U)AD errors injected by address. */
	NI_L2VDPHYS,	memtest_l2vad_phys,	"memtest_l2vad_phys",
	NI_L2UAPHYS,	memtest_l2vad_phys,	"memtest_l2vad_phys",
	KT_L2LVCDPHYS,	memtest_l2vad_phys,	"memtest_l2vad_phys",
	KT_L2LVCSPHYS,	memtest_l2vad_phys,	"memtest_l2vad_phys",

	/* L2 cache directory, fill, miss, and write buffer errors. */
	KT_HD_LDC,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_HI_LDC,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_KD_LDC,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",
	KT_KI_LDC,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",
	KT_UD_LDC,	memtest_u_l2dir_err,	"memtest_u_l2dir_err",
	KT_UI_LDC,	memtest_u_l2dir_err,	"memtest_u_l2dir_err",

	KT_HD_FBDC,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_HI_FBDC,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_KD_FBDC,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",
	KT_KI_FBDC,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",
	KT_HD_FBDU,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_HI_FBDU,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_KD_FBDU,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",
	KT_KI_FBDU,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",

	KT_HD_MBDU,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_HI_MBDU,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_KD_MBDU,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",
	KT_KI_MBDU,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",

	KT_HD_LDWBC,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_HI_LDWBC,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_KD_LDWBC,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",
	KT_KI_LDWBC,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",
	KT_HD_LDWBU,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_HI_LDWBU,	memtest_h_l2buf_err,	"memtest_h_l2buf_err",
	KT_KD_LDWBU,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",
	KT_KI_LDWBU,	memtest_k_l2dir_err,	"memtest_k_l2dir_err",

	KT_L2DIRPHYS,	memtest_l2dir_phys,	"memtest_l2dir_phys",
	KT_L2FBUFPHYS,	memtest_l2dir_phys,	"memtest_l2dir_phys",
	KT_L2MBUFPHYS,	memtest_l2dir_phys,	"memtest_l2dir_phys",
	KT_L2WBUFPHYS,	memtest_l2dir_phys,	"memtest_l2dir_phys",

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

	/* NCU errors */
	VF_IO_NCXFDRTO,	kt_inject_ncu_err,	"kt_inject_ncu_err",
	VF_IO_NCXFSRTO,	kt_inject_ncu_err,	"kt_inject_ncu_err",

	/* LFU errors */
	VF_LFU_MLF,	kt_inject_lfu_lf_err,	"kt_inject_lfu_lf_err",
	VF_LFU_SLF,	kt_inject_lfu_lf_err,	"kt_inject_lfu_lf_err",
	VF_LFU_RTF,	kt_inject_lfu_rtf_err,	"kt_inject_lfu_rtf_err",
	VF_LFU_TTO,	kt_inject_lfu_to_err,	"kt_inject_lfu_to_err",
	VF_LFU_CTO,	kt_inject_lfu_to_err,	"kt_inject_lfu_to_err",

	/* System on Chip (SOC) MCU FB DIMM Link errors. */
	N2_HD_MCUFBR,	kt_inject_mcu_fbd,	"kt_inject_mcu_fbd",
	N2_HD_MCUFBU,	kt_inject_mcu_fbd,	"kt_inject_mcu_fbd",
	VF_HD_MCUFBRF,	kt_inject_fbd_failover, "kt_inject_fbd_failover",

	/* System on Chip (SOC) Internal errors. */
	N2_IO_NIUDPAR,		kt_inject_soc_int,	"kt_inject_soc_int",
	N2_IO_NIUCTAGUE,	kt_inject_soc_int,	"kt_inject_soc_int",
	N2_IO_NIUCTAGCE,	kt_inject_soc_int,	"kt_inject_soc_int",

	KT_IO_SIU_ERR,	kt_inject_soc_int,	"kt_inject_soc_int",

	KT_IO_SOC_CBDU,	kt_inject_soc_int,	"kt_inject_soc_int",
	KT_IO_SOC_CBDC,	kt_inject_soc_int,	"kt_inject_soc_int",
	KT_IO_SOC_CBAP,	kt_inject_soc_int,	"kt_inject_soc_int",
	KT_IO_SOC_CBHP,	kt_inject_soc_int,	"kt_inject_soc_int",

	KT_IO_SOC_SBDU,	kt_inject_soc_int,	"kt_inject_soc_int",
	KT_IO_SOC_SBDC,	kt_inject_soc_int,	"kt_inject_soc_int",
	KT_IO_SOC_SBHP,	kt_inject_soc_int,	"kt_inject_soc_int",

	KT_IO_SOC_DBDU,	kt_inject_soc_int,	"kt_inject_soc_int",
	KT_IO_SOC_DBDC,	kt_inject_soc_int,	"kt_inject_soc_int",
	KT_IO_SOC_DBAP,	kt_inject_soc_int,	"kt_inject_soc_int",
	KT_IO_SOC_DBHP,	kt_inject_soc_int,	"kt_inject_soc_int",
	KT_IO_SOC_IBHP,	kt_inject_soc_int,	"kt_inject_soc_int",

	/* SSI (bootROM interface) errors. */
	NI_HD_SSITO,	kt_k_ssi_err,		"kt_k_ssi_err",
	NI_HD_SSITOS,	kt_k_ssi_err,		"kt_k_ssi_err",
	NI_PRINT_SSI,	kt_k_ssi_err,		"kt_k_ssi_err",

	/* DEBUG test case(s) to get processor specific information from HV. */
	KT_TEST,	n2_inject_test_case, 	"n2_inject_test_case",
	KT_PRINT_ESRS,	kt_debug_print_esrs, 	"kt_debug_print_esrs",
	KT_CLEAR_ESRS,	kt_debug_clear_esrs, 	"kt_debug_clear_esrs",

	NULL,		NULL,			NULL,
};

static cmd_t *commands[] = {
	kt_cmds,
	sun4v_generic_cmds,
	NULL
};

void
kt_debug_init()
{
	int	i;

	for (i = 0; i < DEBUG_BUF_SIZE; i++)
		kt_debug_buf_va[i] = 0xeccdeb46eccdeb46;
}

void
kt_debug_dump()
{
	int	i;

	for (i = 0; i < DEBUG_BUF_SIZE; i++) {
		DPRINTF(0, "kt_debug_dump: kt_debug_buf[0x%2x]=0x%llx\n",
		    i*8, kt_debug_buf_va[i]);
	}
}

/*
 * *****************************************************************
 * The following block of routines are the KT/RF test routines.
 * *****************************************************************
 */

/*
 * This routine inserts an ecc error into a floating point register using a
 * register chosen by an offset.
 *
 * Valid xorpat/eccmask values are bits[7:0].
 *
 * NOTE: KT only implements one set of floating point registers per
 *	 physical core, so unlike the integer register file tests there are
 *	 no issues in regard to register windows.
 *
 * NOTE: the offset inpar is not actually used, but is overwritten with
 *	 a value that determines which register will be used as the
 *	 injection target.
 */
int
kt_inject_freg_file(mdata_t *mdatap, uint64_t offset)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	enable;
	uint64_t	eccmask;
	int		ret;
	char		*fname = "kt_inject_freg_file";

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

	/*
	 * The injection register allows injections to either the
	 * even or the odd FP registers, to make use of this feature
	 * a specific target register can be chosen by the user.
	 * Injections will use either double width operations (64-bit)
	 * or single width operations (32-bit) depending on the value
	 * of the MISC2 option.  Valid values are between 0 and 63,
	 * with 0 to 31 being single width, and 32 to 63 double.
	 *
	 * Note that MISC1 is already used (in the calling routine)
	 * to choose the offset which determines the target register
	 * in previous sun4v FP injection routines (so is unused here).
	 */
	offset = (F_MISC2(iocp) ? (iocp->ioc_misc2) : 48);
	if ((offset >= 64) || (offset < 0)) {
		DPRINTF(0, "%s: invalid target register offset "
		    "argument %d, using default of 48 for %%f32\n",
		    fname, offset);
		offset = 48;
	}

	/*
	 * Perform the access in hyperpriv mode for FP register file
	 * errors.  Choose the access type by overloading bits in the
	 * enable inpar which the asm rountine uses to switch on.
	 *
	 * Note that for kernel mode errors the NOERR bit is set so
	 * that the register will not be accesssed in HV mode.
	 */
	if (F_NOERR(iocp) || ERR_MODE_ISKERN(iocp->ioc_command)) {
		enable |= EI_REG_NOERR_BIT;
	} else if (ERR_ACC_ISOP(iocp->ioc_command)) {
		enable |= EI_REG_ACC_OP_BIT;
	} else if (ERR_ACC_ISLOAD(iocp->ioc_command)) {
		enable |= EI_REG_ACC_LOAD_BIT;
	}

	DPRINTF(3, "%s: enable=0x%p, paddr=0x%llx, eccmask=0x%llx, "
	    "offset=%d\n", fname, enable, paddr, eccmask, offset);

	/* Inject the error */
	ret = memtest_hv_inject_error("kt_inj_freg_file",
	    (void *)kt_inj_freg_file, paddr, enable, eccmask, offset);

	/*
	 * For kernel mode errors call an appropriate asm access routine
	 * that is run in kernel mode, HV mode errors are triggered in
	 * the asm injection routine.
	 */
	if (ERR_MODE_ISKERN(iocp->ioc_command) && !(F_NOERR(iocp))) {
		if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
			(void) kt_k_freg_store((uint64_t)mdatap->m_kvaddr_a,
			    offset);
		} else if (ERR_ACC_ISOP(iocp->ioc_command)) {
			(void) kt_k_freg_op(offset);
		} else {
			(void) kt_k_freg_load((uint64_t)mdatap->m_kvaddr_a,
			    offset);
		}
	}

	/*
	 * Set the NOERR flag now so that the sun4v common calling routine
	 * (memtest_h_reg_err or memtest_k_reg_err) will not try to invoke
	 * the error.
	 */
	IOC_FLAGS(iocp) |= FLAGS_NOERR;

	DPRINTF(3, "%s: ret = 0x%x\n", fname, ret);
	return (0);
}

/*
 * This routine injects FBD link error due to failover which is caused
 * by writing to the FBDIMM TS3 config register.
 *
 * The value to be written to the failover reg is set via the xorpat
 * option.  The default is to set all lane bits (0xffff).
 */
int
kt_inject_fbd_failover(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	dram_bank_offset;
	uint64_t	lane_select = IOC_XORPAT(iocp);
	uint64_t	cpu_node_id;
	char		*fname = "kt_inject_fbd_failover";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	/*
	 * The DRAM branch to use for injection can be selected
	 * with the misc1 option.  By default the branch (MCU)
	 * to use is the one which contains the EI buffer (it
	 * is known to exist).  Note that this routine allows a
	 * non-existant MCU to be chosen for troubleshooting purposes.
	 */
	dram_bank_offset = memtest_get_dram_bank_offset(mdatap);

	if (F_MISC1(iocp)) {
		dram_bank_offset = iocp->ioc_misc1;

		if ((dram_bank_offset < 0) ||
		    (dram_bank_offset >= KT_NUM_DRAM_BRANCHES)) {
			DPRINTF(0, "%s: misc1 value for MCU select out "
			    "of bounds!\n", fname);
			return (EIO);
		}

		dram_bank_offset *= KT_DRAM_BRANCH_OFFSET;
	}

	/*
	 * Add the cpu_node_id to the offset so that it will also
	 * be part of the injection register address used.
	 */
	dram_bank_offset |= (cpu_node_id << KT_CPU_NODE_ID_SHIFT);

	/*
	 * Write to the chosen MCUs failover register.
	 */
	(void) memtest_hv_util("hv_paddr_store64", (void *)hv_paddr_store64,
	    KT_DRAM_CSR_BASE + KT_DRAM_TS3_FAILOVER_CONFIG_REG +
	    dram_bank_offset, lane_select, NULL, NULL);

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
kt_inject_hvicache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	caddr_t		kvaddr;
	uint64_t	raddr, paddr;
	char		*fname = "kt_inject_hvicache";

	/*
	 * Find the raddr and paddr of asm routine to corrupt from it's kvaddr.
	 */
	kvaddr = (caddr_t)kt_ic_hvaccess;
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

		(void) memtest_hv_inject_error("kt_inj_icache_hvtag",
		    (void *)kt_inj_icache_hvtag, paddr, IOC_XORPAT(iocp),
		    (uint64_t)kt_debug_buf_pa, NULL);

	} else if (ERR_SUBCLASS_ISMH(iocp->ioc_command)) {

		(void) memtest_hv_inject_error("kt_inj_icache_hvmult",
		    (void *)kt_inj_icache_hvmult, paddr, IOC_XORPAT(iocp),
		    (uint64_t)kt_debug_buf_pa, NULL);

	} else {
		/*
		 * Otherwise corrupt the icache instr parity.
		 */
		(void) memtest_hv_inject_error("kt_inj_icache_hvinstr",
		    (void *)kt_inj_icache_hvinstr, paddr, IOC_XORPAT(iocp),
		    (uint64_t)kt_debug_buf_pa, NULL);
	}

	return (0);
}

/*
 * This routine inserts an error into the parity bits protecting the
 * instruction cache data or the instruction cache tags at a location
 * determined by the function members of the mdata struct.
 *
 * Valid xorpat values are:
 *	Instr:     [31:0]
 *	Instr par: [32]
 *
 *	Tag:       [34:2] -> PA[43:11]
 *	Tag par:   [16]
 *	Tag vals:  [15] (slave)
 *	Tag val:   [1] (master)
 *
 * NOTE: valid bit injections are performed using the tag routine because
 *	 the command definition is of subclass TAG and the valid bits
 *	 are part of the tag itself.
 */
int
kt_inject_icache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		myid = getprocessorid();
	uint64_t	paddr = mdatap->m_paddr_c;
	int		ret;
	char		*fname = "kt_inject_icache";

	DPRINTF(3, "%s: iocp=0x%p\n", fname, iocp);

#ifdef	L1_DEBUG_BUFFER
	kt_debug_init();
#endif	/* L1_DEBUG_BUFFER */

	/*
	 * Can disable DM mode again after the access since all i$ errors
	 * are CE and recoverable on KT/RF.
	 */
	(void) memtest_hv_util("kt_icache_enable_DM",
	    (void *)kt_icache_enable_DM, NULL, NULL, NULL, NULL);

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
			(mdatap->m_asmld)(mdatap->m_kvaddr_c);
		}

		ret = memtest_hv_inject_error("kt_inj_icache_tag",
		    (void *)kt_inj_icache_tag, paddr, IOC_XORPAT(iocp),
		    (uint64_t)kt_debug_buf_pa, NULL);

		/*
		 * Trigger error by running routine as close to injection
		 * as possible.
		 */
		if (!F_NOERR(iocp) && (ret != KT_DATA_NOT_FOUND)) {
			if (ERR_MISC_ISTL1(iocp->ioc_command)) {
				xt_one(myid, (xcfunc_t *)(mdatap->m_asmld_tl1),
				    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
			} else {
				(mdatap->m_asmld)(mdatap->m_kvaddr_c);
			}
		} else if (ret == KT_DATA_NOT_FOUND) {
			DPRINTF(0, "%s: kt_inj_icache_tag FAILED to find "
			    "matching tag in icache,\n\t\tlikely it has "
			    "been evicted, ret=0x%x\n", fname, ret);

			(void) memtest_hv_util("kt_icache_disable_DM",
			    (void *)kt_icache_disable_DM, NULL, NULL,
			    NULL, NULL);

#ifdef	L1_DEBUG_BUFFER
			kt_debug_dump();
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
			(mdatap->m_asmld)(mdatap->m_kvaddr_c);
		}

		ret = memtest_hv_inject_error("kt_inj_icache_mult",
		    (void *)kt_inj_icache_mult, paddr, IOC_XORPAT(iocp),
		    (uint64_t)kt_debug_buf_pa, NULL);

		/*
		 * Trigger error by running routine as close to injection
		 * as possible.
		 */
		if (!F_NOERR(iocp) && (ret != KT_DATA_NOT_FOUND)) {
			if (ERR_MISC_ISTL1(iocp->ioc_command)) {
				xt_one(myid, (xcfunc_t *)(mdatap->m_asmld_tl1),
				    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
			} else {
				(mdatap->m_asmld)(mdatap->m_kvaddr_c);
			}
		} else if (ret == KT_DATA_NOT_FOUND) {
			DPRINTF(0, "%s: kt_inj_icache_mult FAILED to find "
			    "matching tag in icache,\n\t\tlikely it has "
			    "been evicted, ret=0x%x\n", fname, ret);

			(void) memtest_hv_util("kt_icache_disable_DM",
			    (void *)kt_icache_disable_DM, NULL, NULL,
			    NULL, NULL);

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
			(mdatap->m_asmld)(mdatap->m_kvaddr_c);
		}

		(void) memtest_hv_inject_error("kt_inj_icache_instr",
		    (void *)kt_inj_icache_instr, paddr, IOC_XORPAT(iocp),
		    (uint64_t)kt_debug_buf_pa, NULL);

		/*
		 * Trigger error by running routine as close to injection
		 * as possible.
		 */
		if (!F_NOERR(iocp)) {
			if (ERR_MISC_ISTL1(iocp->ioc_command)) {
				xt_one(myid, (xcfunc_t *)(mdatap->m_asmld_tl1),
				    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
			} else {
				(mdatap->m_asmld)(mdatap->m_kvaddr_c);
			}
		}
	}

	(void) memtest_hv_util("kt_icache_disable_DM",
	    (void *)kt_icache_disable_DM, NULL, NULL, NULL, NULL);

#ifdef	L1_DEBUG_BUFFER
	kt_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

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
 * 	Tag:      [25:0] (bits 31:6 in reg, note N1/N2/VF had a tag of [21:0])
 * 	Tag ecc:  [5:0]
 *
 * The L2 cache has a Writeback buffer, a Fill buffer, and a DMA buffer.
 * "Errors in these buffers are indistinguishable from L2 cache errors."
 * Writing to the L2 config registers performs a flush of these buffers,
 * Therefore the asm routine writes to/from DMMODE to flush the buffers.
 *
 * NOTE: HW guys might be changing the partial store UE case to be fatal
 *	 because of an issue with coherency which could cause data
 *	 corruption.  We can test this now BUT we may need to use NotData
 *	 then have more options for the access width (ldb, ldw, ld, ldx).
 *	 Actually they may make the whole dword be ND instead.
 *
 * NOTE: other cores can only read from each others L2-caches, they do not
 *	 place data into them so other cores will not overwrite anything
 *	 in a local cache.  Snoop traffic can of course invalidate lines.
 */
int
kt_inject_l2cache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		myid = getprocessorid();
	uint64_t	raddr = mdatap->m_raddr_c;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	idx_paddr, tmp_paddr, asi_paddr;
	uint64_t	l2cr_addr;
	uint64_t	l2cr_value;
	uint64_t	l2cr_saved;
	uint64_t	l2_bank_mask;
	int		ret;
	char		*fname = "kt_inject_l2cache";

	DPRINTF(3, "%s: iocp=0x%p, raddr=0x%llx, paddr=0x%llx\n",
	    fname, iocp, raddr, paddr);

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 *
	 * NOTE: the L2 cache asm routines use a fixed bitmask for the bank
	 *	 (is PA[9:6]) even if some banks are disabled.  This should
	 *	 still work since the HW is supposed to automatically mask
	 *	 the register addresses when in reduced bank modes.
	 */
	l2_bank_mask = kt_check_l2_bank_mode();

	/*
	 * Determine the L2 cache index hashing (IDX) address to use, if
	 * IDX mode is disabled the idx_paddr will match the original paddr.
	 */
	(void) kt_check_l2_idx_mode(paddr, &idx_paddr);

	/*
	 * Because the tag diagnostic ASI addressing is affected by the
	 * number of enabled L2-cache banks, modify the paddr (which
	 * will be used by the asm routine only for the tag ASI) to
	 * account for the supported reduced bank modes.
	 *
	 * NOTE: for KT (see programming notes in the PRM under table 23-36)
	 *	 these shifts may ALSO be required for the Prefetch-ICE
	 *	 instructions and the L2 DATA ASIs as well.  Turns out the
	 *	 PRM is incorrect and the shifts are NOT required for the
	 *	 DATA ASIs (also note the little arrows it uses are confusing).
	 */
	if (l2_bank_mask == KT_L2_4BANK_MASK) {
		/*
		 * Set ASI address bits 17:10 to bits 15:8 from paddr.
		 */
		tmp_paddr = (paddr & 0xff00) << 2;
		asi_paddr = paddr & ~(0xff00 << 2);
		asi_paddr |= tmp_paddr;

		DPRINTF(3, "%s: 4 banks paddr = 0x%llx, ASI paddr "
		    "= 0x%llx\n", fname, paddr, asi_paddr);

	} else if (l2_bank_mask == KT_L2_8BANK_MASK) {
		/*
		 * Set ASI address bits 17:10 to bits 16:9 from paddr.
		 */
		tmp_paddr = (paddr & 0x1fe00) << 1;
		asi_paddr = paddr & ~(0x1fe00 << 1);
		asi_paddr |= tmp_paddr;

		DPRINTF(3, "%s: 8 banks paddr = 0x%llx, ASI paddr "
		    "= 0x%llx\n", fname, paddr, asi_paddr);

	} else if (l2_bank_mask == KT_L2_16BANK_MASK) {
		/*
		 * Set ASI address bits to idx'd paddr.
		 */
		asi_paddr = idx_paddr;

		DPRINTF(3, "%s: 16 banks, ASI paddr = idx'd paddr = "
		    "0x%llx\n", fname, idx_paddr);
	} else {
		DPRINTF(0, "%s: unsupported number of L2 banks enabled, "
		    "L2 bank mask = 0x%lx\n", fname, l2_bank_mask);
		return (-1);
	}

	/*
	 * Determine addr and read contents of the L2 control register.
	 */
	l2cr_addr = KT_L2_CTL_REG + (paddr & l2_bank_mask);
	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	l2cr_saved = l2cr_value;

	/*
	 * Set the L2$ into DM mode prior to the call to HV so the
	 * data/instructions can be installed into L2$ by this routine
	 * and the line changed to modified if req'd.
	 */
	l2cr_value |= KT_L2CR_DMMODE;
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
	 * non-instruction L2 errors (this is required for write-back
	 * errors, and is preferable for normal L2$ errors).
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
		 * tag ECC bits.  This is a bit different than N2/VF
		 * because now only accesses to the specific way will detect
		 * errors.
		 */
		if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

			ret = memtest_hv_inject_error(
			    "kt_inj_l2cache_instr_tag",
			    (void *)kt_inj_l2cache_instr_tag,
			    paddr, IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), asi_paddr);

			if (ret == -1) {
				DPRINTF(0, "%s: kt_inj_l2cache_instr_tag "
				    "FAILED, ret=0x%x\n", fname, ret);
				return (ret);
			}
		} else {
			/*
			 * Otherwise corrupt the l2cache instr or the instr ECC.
			 */
			ret = memtest_hv_inject_error(
			    "kt_inj_l2cache_instr_data",
			    (void *)kt_inj_l2cache_instr_data,
			    paddr, IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), idx_paddr);

			if (ret == -1) {
				DPRINTF(0, "%s: kt_inj_l2cache_instr_data "
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
			kt_debug_init();

			ret = memtest_hv_inject_error("kt_inj_l2cache_tag",
			    (void *)kt_inj_l2cache_tag, paddr,
			    IOC_XORPAT(iocp),
			    (uint64_t)kt_debug_buf_pa, asi_paddr);

			kt_debug_dump();
#else	/* L2_DEBUG_BUFFER */
			ret = memtest_hv_inject_error("kt_inj_l2cache_tag",
			    (void *)kt_inj_l2cache_tag, paddr,
			    IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), asi_paddr);

#endif	/* L2_DEBUG_BUFFER */

			if (ret == -1) {
				DPRINTF(0, "%s: kt_inj_l2cache_tag FAILED, "
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
			kt_debug_init();

			ret = memtest_hv_inject_error("kt_inj_l2cache_data",
			    (void *)kt_inj_l2cache_data, paddr,
			    IOC_XORPAT(iocp),
			    (uint64_t)kt_debug_buf_pa, idx_paddr);

			kt_debug_dump();
#else	/* L2_DEBUG_BUFFER */

			ret = memtest_hv_inject_error("kt_inj_l2cache_data",
			    (void *)kt_inj_l2cache_data, paddr,
			    IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), idx_paddr);

#endif	/* L2_DEBUG_BUFFER */

			if (ret == -1) {
				DPRINTF(0, "%s: kt_inj_l2cache_data FAILED, "
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
 * This routine inserts one or more errors into one of the L2 buffers
 * which is available through the L2_ERROR_INJECT register.  These
 * buffers incude:
 *	L2 directory (parity protection)
 *	L2 fill buffer (ecc protection) COU/DRAM -> L2
 *	L2 miss buffer (parity protection) core -> L2
 *	L2 writeback/copyback buffer (ecc protection) L2 -> COU/DRAM
 *
 * The error is injected at a location determined by the mdata struct
 * member mdatap->m_paddr_c or for PHYS tests at the byte offset in the
 * iocp->ioc_addr member.
 *
 * A full L2-cache flush is performed prior to calling this routine so
 * stale data will not be in the cache.
 *
 * NOTE: there are no kernel mode instruction versions for these errors
 *	 because the L2 buffers are very small and the errors will be
 *	 displaced before they can be accessed in kernal mode.
 */
int
kt_inject_l2buf(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	caddr_t		kvaddr = mdatap->m_kvaddr_c;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	enable;
	uint_t		hv_flag = 0;
	uint64_t	l2_bank_mask;
	uint64_t	chunk_value;
	int		ret;
	char		*fname = "kt_inject_l2buf";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 *
	 * NOTE: the L2 cache asm routines use a fixed bitmask for the bank
	 *	 (is PA[9:6]) even if some banks are disabled.  This should
	 *	 still work since the HW is supposed to automatically mask
	 *	 the register addresses when in reduced bank modes.
	 */
	l2_bank_mask = kt_check_l2_bank_mode();

	if (ERR_SUBCLASS_ISDIR(iocp->ioc_command)) {
		enable = (KT_L2_INJ_ENABLE | KT_L2_INJ_DIR);
	} else if (ERR_SUBCLASS_ISFBUF(iocp->ioc_command)) {

		/*
		 * Choose which 4-byte word to target within the
		 * 16-byte entry.
		 */
		chunk_value = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 1);
		if ((chunk_value < 0) || (chunk_value > 3)) {
			DPRINTF(0, "%s: invalid misc1 argument for chunk "
			    "select using default %d\n", fname, 1);
			chunk_value = 1;
		}

		enable = (KT_L2_INJ_ENABLE | KT_L2_INJ_FBUF | chunk_value);
	} else if (ERR_SUBCLASS_ISMBUF(iocp->ioc_command)) {
		enable = (KT_L2_INJ_ENABLE | KT_L2_INJ_MBUF);
	} else if (ERR_SUBCLASS_ISWBUF(iocp->ioc_command)) {

		/*
		 * Choose which 8-byte word to target within the
		 * 16-byte entry.
		 */
		chunk_value = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 1);
		if ((chunk_value < 0) || (chunk_value > 1)) {
			DPRINTF(0, "%s: invalid misc1 argument for chunk "
			    "select using default %d\n", fname, 1);
			chunk_value = 1;
		}

		enable = (KT_L2_INJ_ENABLE | KT_L2_INJ_WBUF | chunk_value);
	} else {
		DPRINTF(0, "%s: unsupported type of L2 buffer error selected in"
		    " command 0x%llx, exiting...\n", fname, iocp->ioc_command);
		return (ENOTSUP);
	}

	/*
	 * If the injection target uses ecc protection (the miss buffer
	 * and the directory use parity) then also set the ecc mask
	 * based on the xorpat.
	 */
	if (ERR_SUBCLASS_ISFBUF(iocp->ioc_command) ||
	    ERR_SUBCLASS_ISWBUF(iocp->ioc_command)) {
		enable |= (IOC_XORPAT(iocp) << KT_L2_INJ_ECC_SHIFT);
	}

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode (default to single-shot).
	 */
	if (!(F_INF_INJECT(iocp))) {
		enable |= KT_L2_INJ_SSHOT;
	}

	/*
	 * If the error specifies HV mode, set a flag for the asm routine.
	 */
	if (ERR_MODE_ISHYPR(iocp->ioc_command)) {
		hv_flag = 1;
	}

	/*
	 * Determine which asm routine to use for injection.
	 */
	if (ERR_MISC_ISPHYS(iocp->ioc_command)) {

		/*
		 * Corrupt the L2 cache directory/buffer parity using offset.
		 */
		ret = memtest_hv_inject_error("kt_inj_l2buf_phys",
		    (void *)kt_inj_l2buf_phys, offset, enable,
		    (uint64_t)kt_debug_buf_pa, l2_bank_mask);

		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2buf_phys FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}

	} else if (ERR_SUBCLASS_ISWBUF(iocp->ioc_command)) {

		/*
		 * The write buffer injections use a different asm
		 * routine because the data must be in the cache
		 * and put into the modified state so that it can
		 * go through the write buffer (unlike the others).
		 *
		 * Also instruction errors are not supported for the
		 * writeback buffer since in general instructions
		 * are not modified during runtime.
		 */
		ret = memtest_hv_inject_error("kt_inj_l2wbuf",
		    (void *)kt_inj_l2wbuf, paddr, enable,
		    (uint64_t)hv_flag, l2_bank_mask);

		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2wbuf FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}

		/*
		 * Although it's unlikely that the error will last long
		 * enough for the code to get back here due to the small
		 * size of the write buffer the address is flushed here
		 * in kernel mode if kernel mode was specified.
		 */
		if (ERR_MODE_ISKERN(iocp->ioc_command)) {
			OP_FLUSH_L2_ENTRY(mdatap, (caddr_t)mdatap->m_raddr_a);
		}

		return (0);

	} else if (ERR_ACC_ISFETCH(iocp->ioc_command)) {

		/*
		 * Only HV mode instruction errors are supported because
		 * the L2 buffers are too small to support the injection
		 * of errors using an instruction buffer run in kernel mode.
		 *
		 * In order to not completely fill any of the L2 buffers
		 * with instructions the instruction buffer to use for the
		 * injection is modified so that it will return early.
		 * A return that is compatible with HV code calls is used
		 * so that it can be called to install the line(s) then
		 * called again to access the injected error.
		 *
		 * The 5th and 6th (offset 16 and 20) instructions become:
		 *	jmp	%g7 + 4		! = jmpl %g7, %g0 (+ imm of 0x4)
		 *				!	= 0x81c1.e004
		 *	  nop			! = 0x0100.0000
		 *
		 * By convention the PC is stored in %g7 for HV mode calls.
		 * The 5th and 6th are used because the entries are 16-bytes
		 * in the fill buffer and that allows two lines to be installed
		 * one that will be corrupted and one with the return instrs.
		 */
		if (ERR_MODE_ISHYPR(iocp->ioc_command)) {
			*(uint64_t *)(kvaddr + 16) = 0x81c1e00401000000;
			membar_sync();
		}

		ret = memtest_hv_inject_error("kt_inj_l2buf_instr",
		    (void *)kt_inj_l2buf_instr, paddr, enable,
		    (uint64_t)hv_flag, l2_bank_mask);

		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2buf_instr FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Corrupt the L2 cache buffer data at physical address.
		 */
#ifdef	L2_DEBUG_BUFFER
		/*
		 * DEBUG - replace hv_flag arg with debug buf.
		 *	Requires that L2_DEBUG_BUFFER is also def'd
		 *	in the asm routine to match.
		 */
		kt_debug_init();

		ret = memtest_hv_inject_error("kt_inj_l2buf",
		    (void *)kt_inj_l2buf, paddr, enable,
		    (uint64_t)kt_debug_buf_pa, l2_bank_mask);

		kt_debug_dump();
#else	/* L2_DEBUG_BUFFER */

		ret = memtest_hv_inject_error("kt_inj_l2buf",
		    (void *)kt_inj_l2buf, paddr, enable,
		    (uint64_t)hv_flag, l2_bank_mask);

#endif	/* L2_DEBUG_BUFFER */

		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2buf FAILED, ret=0x%x\n",
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
 * This routine is similar to the above kt_inject_l2cache() routine,
 * note however that the xorpat must be set to specific values to
 * indicate NotData.
 *	signalling sND xor = 0x7d (injected if "-c" option is used)
 *	quiet qND xor = 0x7e or 0x7f (0x7e used by default)
 */
int
kt_inject_l2nd(mdata_t *mdatap)
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
	char		*fname = "kt_inject_l2nd";

	DPRINTF(3, "%s: iocp=0x%p, raddr=0x%llx, paddr=0x%llx\n",
	    fname, iocp, raddr, paddr);

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 *
	 * NOTE: the L2 cache asm routines use a fixed bitmask for the bank
	 *	 (is PA[9:6]) even if some banks are disabled.  This should
	 *	 still work since the HW is supposed to automatically mask
	 *	 the register addresses when in reduced bank modes.
	 */
	l2_bank_mask = kt_check_l2_bank_mode();

	/*
	 * Determine the L2 cache index hashing (IDX) address to use, if
	 * IDX mode is disabled the idx_paddr will match the original paddr.
	 */
	(void) kt_check_l2_idx_mode(paddr, &idx_paddr);

	/*
	 * First check for the PHYS version of the test, if so do not
	 * change any cache/reg settings at all.
	 */
	if (ERR_MISC_ISPHYS(iocp->ioc_command)) {
		/*
		 * Install NotData into L2 cache at offset.
		 */
		ret = memtest_hv_inject_error("kt_inj_l2nd_phys",
		    (void *)kt_inj_l2nd_phys, offset, IOC_XORPAT(iocp),
		    (uint64_t)kt_debug_buf_pa, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2nd_phys "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
		return (0);
	}

	/*
	 * Determine addr and read contents of the L2 control register.
	 */
	l2cr_addr = KT_L2_CTL_REG + (paddr & l2_bank_mask);
	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	l2cr_saved = l2cr_value;

	/*
	 * Set the L2$ into DM mode prior to the call to HV so the
	 * data/instructions can be installed into L2$ by this routine
	 * and the line changed to modified if req'd.
	 */
	l2cr_value |= KT_L2CR_DMMODE;
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
		ret = memtest_hv_inject_error("kt_inj_l2nd_instr",
		    (void *)kt_inj_l2nd_instr, paddr, IOC_XORPAT(iocp),
		    idx_paddr, (uint64_t)kt_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2nd_instr "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {	/* Data tests */
		/*
		 * Install NotData into L2 cache data.
		 */
#ifdef	L2_DEBUG_BUFFER
		kt_debug_init();
#endif
		ret = memtest_hv_inject_error("kt_inj_l2nd",
		    (void *)kt_inj_l2nd, paddr, IOC_XORPAT(iocp),
		    idx_paddr, (uint64_t)kt_debug_buf_pa);

#ifdef	L2_DEBUG_BUFFER
		kt_debug_dump();
#endif
		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2nd FAILED, "
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
 * This routine is similar to the above kt_inject_l2cache() routine.
 */
int
kt_inject_l2phys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	int		ret;
	char		*fname = "kt_inject_l2phys";

	DPRINTF(3, "%s: iocp=0x%p, offset=0x%llx\n", fname, iocp, offset);

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 *
	 * NOTE: the L2 cache asm routines use a fixed bitmask for the bank
	 *	 (is PA[9:6]) even if some banks are disabled.  This should
	 *	 still work since the HW is supposed to automatically mask
	 *	 the register addresses when in reduced bank modes.
	 *
	 *	 So this routine is not checking for a reduced bank mode.
	 */

	/*
	 * If the IOCTL specified the tag, corrupt the tag or the tag ECC.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("kt_inj_l2phys_tag",
		    (void *)kt_inj_l2phys_tag, offset,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)kt_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2phys_tag "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Otherwise corrupt the L2 cache data or the data ECC.
		 */
		ret = memtest_hv_inject_error("kt_inj_l2phys_data",
		    (void *)kt_inj_l2phys_data, offset,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)kt_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2phys_data "
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
 * NOTE: On N1/N2/VF the VADS bits of all L2 ways were checked for each
 *	 L2 cache access, but this is no longer the case on KT.
 *
 * NOTE: the Used (U) bits in the CSA (Control Status Array) bits are not
 *	 protected because an error in these bits will not cause problems.
 */
int
kt_inject_l2vads(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	raddr = mdatap->m_raddr_c;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	idx_paddr;
	uint64_t	csa_select;
	uint64_t	l2cr_addr;
	uint64_t	l2cr_value;
	uint64_t	l2cr_saved;
	uint64_t	l2_bank_mask;
	int		ret;
	char		*fname = "kt_inject_l2vads";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 *
	 * NOTE: the L2 cache asm routines use a fixed bitmask for the bank
	 *	 (is PA[9:6]) even if some banks are disabled.  This should
	 *	 still work since the HW is supposed to automatically mask
	 *	 the register addresses when in reduced bank modes.
	 */
	l2_bank_mask = kt_check_l2_bank_mode();

	/*
	 * Determine the L2 cache index hashing (IDX) address to use, if
	 * IDX mode is disabled the idx_paddr will match the original paddr.
	 */
	(void) kt_check_l2_idx_mode(paddr, &idx_paddr);

	/*
	 * Determine the target for the injection among the available
	 * CSA bits: Valid, Alloc, Dirty, or Shared (no errors on Used bits).
	 */
	if (ERR_SUBCLASS_ISVD(iocp->ioc_command)) {
		csa_select = 0;	/* valid */
	} else if (ERR_SUBCLASS_ISUA(iocp->ioc_command)) {
		csa_select = 1;	/* alloc */
	} else if (ERR_SUBCLASS_ISDIRT(iocp->ioc_command)) {
		csa_select = 2;	/* dirty */
	} else if (ERR_SUBCLASS_ISSHRD(iocp->ioc_command)) {
		csa_select = 3;	/* shared */
	} else {
		DPRINTF(0, "%s: unsupported type of L2 CSA error selected in "
		    "command 0x%llx, exiting...\n", fname, iocp->ioc_command);
		return (ENOTSUP);
	}

	/*
	 * Determine addr and read contents of the L2 control register.
	 */
	l2cr_addr = KT_L2_CTL_REG + (paddr & l2_bank_mask);
	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	l2cr_saved = l2cr_value;

	/*
	 * Set the L2$ into DM mode prior to the call to HV so the
	 * data/instructions can be installed into L2$ by this routine
	 * and the line changed to modified if req'd.
	 */
	l2cr_value |= KT_L2CR_DMMODE;
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
	 * First check for the PHYS version of the test.
	 */
	if (ERR_MISC_ISPHYS(iocp->ioc_command)) {
		/*
		 * Corrupt the L2 cache VADS bits or the VADS ecc bits.
		 */
		ret = memtest_hv_inject_error("kt_inj_l2vads_phys",
		    (void *)kt_inj_l2vads_phys, offset, IOC_XORPAT(iocp),
		    csa_select, (uint64_t)kt_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2vads_phys "
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
		 * Corrupt the L2 cache VADS bits or the VADS ecc bits.
		 *
		 * Bring the instructions into the cache for the corruption.
		 */
		if (ERR_MODE_ISHYPR(iocp->ioc_command)) {
			(void) memtest_hv_util("hv_paddr_load64",
			    (void *)mdatap->m_asmld, mdatap->m_paddr_a, NULL,
			    NULL, NULL);
		} else if (ERR_MODE_ISKERN(iocp->ioc_command)) {
			mdatap->m_asmld(mdatap->m_kvaddr_c);
		}

		ret = memtest_hv_inject_error("kt_inj_l2vads_instr",
		    (void *)kt_inj_l2vads_instr, paddr,
		    IOC_XORPAT(iocp), csa_select, idx_paddr);

		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2vads_instr "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Corrupt the L2 cache VADS bits or the VADS ecc bits
		 * using a data access.
		 */
		ret = memtest_hv_inject_error("kt_inj_l2vads",
		    (void *)kt_inj_l2vads, paddr,
		    IOC_XORPAT(iocp), csa_select, idx_paddr);

		if (ret == -1) {
			DPRINTF(0, "%s: kt_inj_l2vads "
			    "FAILED, ret=0x%x\n", fname, ret);
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
 * Inject an LFU single-lane or multi-lane failure.
 *
 * Lane failures are generated by writing a mask of the lanes selected
 * to fail to the LFU SERDES Transmitter and Receiver Differential
 * Pair Inversion Register (similar to VF).
 *
 * Lane selection for the failed lane is done via the xorpat option.
 */
int
kt_inject_lfu_lf_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	regaddr, regval;
	int		lfu_unit;
	uint32_t	lane_select = IOC_XORPAT(iocp);
	uint64_t	cpu_node_id;
	int		ret;
	char		*fname = "kt_inject_lfu_lf_err";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	/*
	 * The LFU unit to use for injection can be selected
	 * with the misc1 option.  Default is LFU0.
	 */
	lfu_unit = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 0);
	if ((lfu_unit < 0) || (lfu_unit >= KT_LFU_NUM_UNITS)) {
		DPRINTF(0, "%s: misc1 value for LFU select out of bounds!\n",
		    fname);
		return (EIO);
	}

	/*
	 * The misc2 option can be used as a different way to specify
	 * the lane to fail for single-lane failure injections.
	 */
	if (F_MISC2(iocp)) {
		if (LFU_SUBCLASS_ISSLF(iocp->ioc_command)) {
			lane_select = 1 << iocp->ioc_misc2;
			lane_select |= (lane_select << 14);
		}
	}

	regaddr = KT_LFU_SERDES_INVP_REG + (KT_LFU_REG_STEP * lfu_unit) +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
	regval = lane_select;

	/*
	 * Don't inject if the -n flag was specified.
	 */
	if (F_NOERR(mdatap->m_iocp)) {
		regval = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, regaddr, NULL, NULL, NULL);
		DPRINTF(2, "%s: current value of the SERDES lane inversion "
		    "register = 0x%llx, not injecting error\n", fname,
		    regval);
		return (0);
	}

	/*
	 * Now cause the lane failure(s).
	 */
	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, regaddr, regval, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to 0x%llx FAILED, "
		    "ret=0x%x\n", fname, regaddr, ret);
		return (ret);
	}

	return (0);
}

/*
 * Inject an LFU "retrain failed" error or increment replay count.
 *
 * If the number of errors is over the error threshold then a fatal
 * retrain failed error is generated, otherwise the replay count in the
 * LFU ESR is incremented.
 *
 * NOTE: incrementing the replay count does not generate an error trap.
 */
int
kt_inject_lfu_rtf_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	reg1addr, reg2addr, reg3addr;
	uint64_t	reg1val, reg2val, reg3val;
	uint64_t	inj_count, inj_period;
	uint64_t	cpu_node_id;
	uint_t		lfu_unit;
	int		ret;
	char		*fname = "kt_inject_lfu_rtf_err";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	/*
	 * The LFU unit to use for injection can be selected
	 * with the misc1 option.  Default is LFU0.
	 */
	lfu_unit = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 0);
	if ((lfu_unit < 0) || (lfu_unit >= KT_LFU_NUM_UNITS)) {
		DPRINTF(0, "%s: misc1 value for LFU select out of bounds!\n",
		    fname);
		return (EIO);
	}

	reg1addr = KT_LFU_ERR_INJ1_REG + (KT_LFU_REG_STEP * lfu_unit) +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
	reg2addr = KT_LFU_ERR_INJ2_REG + (KT_LFU_REG_STEP * lfu_unit) +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
	reg3addr = KT_LFU_ERR_INJ3_REG + (KT_LFU_REG_STEP * lfu_unit) +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT);

	/*
	 * Set the values to use with the LFU injection registers.
	 * The count, period, and sshot fields are set based on user options.
	 * The max values for count = 7 (3 bits), period = 0x7ff (11 bits).
	 */
	reg1val = KT_LFU_INJECTION_ENABLE | KT_LFU_SSHOT_ENABLE;

	if (F_INF_INJECT(iocp)) {
		reg1val &= ~((uint64_t)KT_LFU_SSHOT_ENABLE);
		inj_count = 0;
	} else {
		inj_count = (F_I_COUNT(iocp) ? (iocp->ioc_i_count) : 1);
	}
	inj_period = (F_I_STRIDE(iocp) ? (iocp->ioc_i_stride) : 2);

	reg1val |= ((inj_count & KT_LFU_COUNT_MASK) << KT_LFU_COUNT_SHIFT) |
	    ((inj_period & KT_LFU_PERIOD_MASK) << KT_LFU_PERIOD_SHIFT);

	/*
	 * The VF code had: reg3val = KT_LFU_LANE_MASK << 24;
	 *
	 * The -c (checkbit) option is used to specify that the second
	 * injection register be used for the xorpat (default is reg3).
	 */
	reg2val = 0;
	reg3val = 0;

	if (F_CHKBIT(iocp)) {
		reg2val = IOC_XORPAT(iocp);
	} else {
		reg3val = IOC_XORPAT(iocp);
	}

	/*
	 * Write the values to the three injection regs.
	 */
	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, reg3addr, reg3val, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to 0x%llx FAILED, "
		    "ret=0x%x\n", fname, reg3addr, ret);
		return (ret);
	}

	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, reg2addr, reg2val, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to 0x%llx FAILED, "
		    "ret=0x%x\n", fname, reg2addr, ret);
		return (ret);
	}

	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, reg1addr, reg1val, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to 0x%llx FAILED, "
		    "ret=0x%x\n", fname, reg1addr, ret);
		return (ret);
	}

	/*
	 * If infinite injection was specified, execution should not make it
	 * this far since the expected outcome is a fatal reset.
	 */
	if (F_INF_INJECT(iocp)) {
		DPRINTF(0, "%s: unexpected continued system operation!\n",
		    fname);
		return (EIO);
	}

	return (0);
}

/*
 * Inject either a LFU training timeout or LFU config timeout error.
 *
 * A training timeout error is generated by first setting the training
 * timeout value to zero and then causing a multi-lane failure.  The
 * training state will not complete because of the multi-lane failure
 * and the training timeout will fire.
 *
 * A config timeout error is generated by first setting the config
 * timeout value to zero and then causing a single-lane failure. In
 * this case the training state does complete because only a single lane
 * has failed.  However, the config timeout will then immediately fire.
 *
 * Lane selection for the failed lane is done via the xorpat option,
 * the value for the timeout is set with the misc2 option.
 */
int
kt_inject_lfu_to_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	to_regaddr, regaddr;
	uint64_t	to_regval, regval;
	int		lfu_unit;
	uint32_t	lane_select = IOC_XORPAT(iocp);
	uint64_t	cpu_node_id;
	int		ret;
	char		*fname = "kt_inject_lfu_to_err";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	/*
	 * The LFU unit to use for injection can be selected
	 * with the misc1 option.  Default is LFU0.
	 */
	lfu_unit = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 0);
	if ((lfu_unit < 0) || (lfu_unit >= KT_LFU_NUM_UNITS)) {
		DPRINTF(0, "%s: misc1 value for LFU select out of bounds!\n",
		    fname);
		return (EIO);
	}

	/*
	 * The value to write to the TO register can be specified with
	 * the misc2 option.  The default is zero.
	 */
	to_regval = (F_MISC2(iocp) ? (iocp->ioc_misc2) : 0);
	if ((to_regval < 0) || (to_regval >= KT_LFU_TO_MAX)) {
		DPRINTF(0, "%s: misc2 value for TO value out of bounds!\n",
		    fname);
		return (EIO);
	}


	/*
	 * The training state TO register contains two fields, one for
	 * the lanefail TO and one for the master TO.  The master TO
	 * must be set 16 above the lanefail.  So the minimum value is 16.
	 */
	if (LFU_SUBCLASS_ISTTO(iocp->ioc_command)) {
		to_regaddr = KT_LFU_TRAN_ST_TO_REG +
		    (KT_LFU_REG_STEP * lfu_unit) +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
		to_regval = (to_regval << 8) | (to_regval + 0x10);
	} else {	/* set the config TO regster */
		to_regaddr = KT_LFU_CFG_ST_TO_REG +
		    (KT_LFU_REG_STEP * lfu_unit) +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
	}

	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, to_regaddr, to_regval, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to 0x%llx FAILED, "
		    "ret=0x%x\n", fname, to_regaddr, ret);
		return (ret);
	}

	/*
	 * Now cause the lane failure.
	 */
	regaddr = KT_LFU_SERDES_INVP_REG + (KT_LFU_REG_STEP * lfu_unit) +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
	regval = lane_select | (lane_select << 14);

	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, regaddr, regval, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to 0x%llx FAILED, "
		    "ret=0x%x\n", fname, regaddr, ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine inserts an error into main memory (DRAM) at a location
 * determined by the physical address member of the mdata struct.  The
 * physical address must be in local memory.
 *
 * The DRAM uses GF[16] Extended ECC.  This means that any error within
 * 16-bits is correctable, and any two errors within two chips are
 * detectable as uncorrectable.
 *
 * This routine has to determine if the DRAM bank/MCU to be used for the
 * injection is enabled or disabled, if it is disabled then the active bank
 * that is handling the address range must be used.  There are only four
 * valid configurations on KT/RF since the L2 banks and the DRAM banks
 * are hard coded to each other.  See PRM section 23.15 for a table that
 * describes the mappings.
 *
 * 	2 DRAM banks, 16 L2 banks, 16 cores (fully enabled, all MCUs and COUs)
 * 	2 DRAM banks, 8  L2 banks, 8 cores (half enabled, COU0 on both MCUs)
 * 	1 DRAM banks, 8  L2 banks, 8 cores (half enabled, both COUs on one MCU)
 * 	1 DRAM banks, 4  L2 banks, 2 cores (quarter enabled, one COU, one MCU)
 *
 * Valid xorpat values for the DRAM errors are:
 * 	ECC mask: [31:0]  (note that N2/VF was [15:0])
 */
int
kt_inject_memory(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	eccmask, l2_bank_mask;
	uint64_t	dram_bank_offset;
	uint64_t	chunk_value;
	uint64_t	cpu_node_id;
	int		ret;
	char		*fname = "kt_inject_memory";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	DPRINTF(3, "%s: iocp=0x%p, cpu_node_id=%d, paddr=0x%llx xor=0x%x\n",
	    fname, iocp, cpu_node_id, paddr, IOC_XORPAT(iocp));

	/*
	 * Get the mask to use for the ECC bit corruption.
	 */
	eccmask = IOC_XORPAT(iocp);
	eccmask |= (KT_DRAM_INJECTION_ENABLE | KT_DRAM_INJ_SSHOT_ENABLE);

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode.
	 */
	if (F_INF_INJECT(iocp))
		eccmask &= ~((uint64_t)KT_DRAM_INJ_SSHOT_ENABLE);

	/*
	 * KT/RF adds a "chunk" field in the DRAM error inject register
	 * to choose between one of two 32-byte chunks to inject the error
	 * into.  This is not based on the address, but injects into either
	 * the non-critical or critical "chunk" (or both).
	 *	01b = 1 = non-critical 32B
	 *	10b = 2 = critical 32B
	 *	11b = 3 = both
	 *
	 * NOTE: KT/RF has a new feature "direction" where the error is present
	 *	 on the write or only the read.  This *should* allow us to
	 *	 inject errors that look intermittent.  This feature is
	 *	 used in new routines since the asm code needed to change
	 *	 see the below kt_inject_memory_int() routine for details.
	 */
	if (ERR_PROT_ISND(iocp->ioc_command)) {
		chunk_value = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 3);
		if ((chunk_value < 0) || (chunk_value > 3)) {
			DPRINTF(0, "%s: invalid misc1 argument for chunk "
			    "select, using default %d\n", fname, 3);
			chunk_value = 3;
		}
	} else {
		chunk_value = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 2);
		if ((chunk_value < 0) || (chunk_value > 3)) {
			DPRINTF(0, "%s: invalid misc1 argument for chunk "
			    "select, using default %d\n", fname, 2);
			chunk_value = 2;
		}
	}

	eccmask |= (chunk_value << KT_DRAM_INJ_CHUNK_SHIFT);

	/*
	 * Determine the DRAM bank offset.
	 */
	dram_bank_offset = memtest_get_dram_bank_offset(mdatap);

	/*
	 * Add the cpu_node_id to the offset so that it will also
	 * be part of the injection register address built by the
	 * asm routine.
	 */
	dram_bank_offset |= (cpu_node_id << KT_CPU_NODE_ID_SHIFT);

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
	/*
	 * DEBUG: to speed up debug different variations of the asm
	 *	  injection routines are used depending on specific
	 *	  user options.
	 */
	kt_debug_init();

	l2_bank_mask = kt_check_l2_bank_mode();
	DPRINTF(0, "%s: the L2 bank mask is being set to %lx\n",
	    fname, l2_bank_mask);

	/*
	 * Place the l2_bank_mask into the first debug buf
	 * address so that the debug asm code can use it.
	 */
	kt_debug_buf_va[0] = l2_bank_mask;

	if (F_CHKBIT(iocp)) {
		/*
		 * Use bank_avail adjusted prefetch-ICE for flush.
		 */
		ret = memtest_hv_inject_error("kt_inj_memory_debug_ice",
		    (void *)kt_inj_memory_debug_ice, paddr,
		    eccmask, dram_bank_offset, (uint64_t)kt_debug_buf_pa);
	} else if (F_MISC2(iocp)) {
		/*
		 * Use a displacement flush instead of the prefetch-ICE
		 * since the prefetch instruction is having issues on
		 * reduced bank systems.
		 */
		ret = memtest_hv_inject_error("kt_inj_memory_debug_disp",
		    (void *)kt_inj_memory_debug_disp, paddr,
		    eccmask, dram_bank_offset, (uint64_t)kt_debug_buf_pa);
	} else {
		/*
		 * Use original debug routine, no use of l2_bank_mask and
		 * use prefetch-ICE flush but unadjusted for bank_avail.
		 */
		ret = memtest_hv_inject_error("kt_inj_memory_debug",
		    (void *)kt_inj_memory_debug, paddr,
		    eccmask, dram_bank_offset, (uint64_t)kt_debug_buf_pa);
	}

	kt_debug_dump();
#else	/* MEM_DEBUG_BUFFER */

	/*
	 * Determine the L2 bank mask to use based on system configuration.
	 */
	l2_bank_mask = kt_check_l2_bank_mode();
	DPRINTF(2, "%s: the L2 bank mask is being set to %lx\n",
	    fname, l2_bank_mask);

	ret = memtest_hv_inject_error("kt_inj_memory", (void *)kt_inj_memory,
	    paddr, eccmask, dram_bank_offset, l2_bank_mask);
#endif	/* MEM_DEBUG_BUFFER */

	if (ret == -1) {
		DPRINTF(0, "%s: kt_inj_memory FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	DPRINTF(3, "%s: ret = 0x%x\n", fname, ret);
	return (0);
}

/*
 * This routine inserts an error into main memory (DRAM) at a location
 * determined by the physical address member of the mdata struct.  The
 * physical address must be in local memory.  KT/RF provides a direction
 * bit in the DRAM injection register which allows read accesses to
 * see an error, this is used to produce an error which appears intermittent.
 *
 * This routine is similar to the above kt_inject_memory() routine.
 *
 * Valid xorpat values for the DRAM errors are:
 * 	ECC mask: [31:0]
 *
 * For the read direction the ECC algorithm is different (64+8 SEC/DED) and
 * the 32-bit xormask is broken up into four 8-bit chunks where the high
 * chunk maps to the ECC of the lowest dword of the data (so it's in reverse).
 */
int
kt_inject_memory_int(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	eccmask;
	uint64_t	dram_bank_offset;
	uint64_t	chunk_value;
	uint64_t	cpu_node_id;
	int		ret;
	char		*fname = "kt_inject_memory_int";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	DPRINTF(3, "%s: iocp=0x%p, cpu_node_id=%d, paddr=0x%llx xor=0x%x\n",
	    fname, iocp, cpu_node_id, paddr, IOC_XORPAT(iocp));

	/*
	 * Get the mask to use for the ECC bit corruption.
	 */
	eccmask = IOC_XORPAT(iocp);
	eccmask |= (KT_DRAM_INJECTION_ENABLE | KT_DRAM_INJ_SSHOT_ENABLE);

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode.
	 */
	if (F_INF_INJECT(iocp))
		eccmask &= ~((uint64_t)KT_DRAM_INJ_SSHOT_ENABLE);

	/*
	 * Set the direction bit to indicate error injection on read.
	 */
	eccmask |= KT_DRAM_INJ_DIRECTION;

	/*
	 * KT/RF adds a "chunk" feild in the DRAM error inject register
	 * to choose between one of two 32-byte chunks to inject the error
	 * into.  This is not based on the address, but injects into either
	 * the non-critical or critical "chunk" (or both).
	 *	01b = 1 = non-critical 32B
	 *	10b = 2 = critical 32B
	 *	11b = 3 = both
	 */
	chunk_value = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 1);
	if ((chunk_value < 0) || (chunk_value > 3)) {
		DPRINTF(0, "%s: invalid misc1 argument for chunk select "
		    "using default %d\n", fname, 1);
		chunk_value = 1;
	}

	eccmask |= (chunk_value  << KT_DRAM_INJ_CHUNK_SHIFT);

	/*
	 * Determine the DRAM bank offset.
	 */
	dram_bank_offset = memtest_get_dram_bank_offset(mdatap);

	/*
	 * Add the cpu_node_id to the offset so that it will also
	 * be part of the injection register address built by the
	 * asm routine.
	 */
	dram_bank_offset |= (cpu_node_id << KT_CPU_NODE_ID_SHIFT);

#ifdef	MEM_DEBUG_BUFFER
	kt_debug_init();
#endif	/* MEM_DEBUG_BUFFER */

	/*
	 * Flush the L2$ to ensure data lookup does not hit in the cache
	 * since we need a reliable DRAM read in order to produce an error.
	 *
	 * NOTE: this may flush existing errors from L2 producing write-back
	 *	 error(s) before the DRAM error is even injected.
	 */
	if (!F_FLUSH_DIS(iocp)) {
		(void) OP_FLUSHALL_L2_HVMODE(mdatap);
	}

	ret = memtest_hv_inject_error("kt_inj_memory_int",
	    (void *)kt_inj_memory_int, paddr, eccmask, dram_bank_offset,
	    (uint64_t)kt_debug_buf_pa);

	if (ret == -1) {
		DPRINTF(0, "%s: kt_inj_memory_int FAILED, ret=0x%x\n",
		    fname, ret);
		return (ret);
	}

#ifdef	MEM_DEBUG_BUFFER
	kt_debug_dump();
#endif	/* MEM_DEBUG_BUFFER */

	DPRINTF(3, "%s: ret = 0x%x\n", fname, ret);
	return (0);
}

/*
 * This routine generates a fatal NCU protocol error.
 * Currently the only error that can be triggered is a timeout error.
 *
 * A timeout is generated by writing a very low timeout value to the NCU
 * Error Enable Timeout field [31:10] which has a default value of all "f"s
 * (0x3fffff).
 */
int
kt_inject_ncu_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	temp_val, to_val;
	uint64_t	cpu_node_id;
	int		ret = 0;
	char		*fname = "kt_inject_ncu_err";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	/*
	 * If the user specified a misc1 option write that value to
	 * the timeout register.
	 */
	to_val = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 3);
	if ((to_val >= 0x3fffff) || (to_val < 0)) {
		DPRINTF(0, "%s: invalid NCU timeout value of "
		    "%d, using default of %d\n", fname, to_val);
		to_val = 3;
	}

	/*
	 * Read the NCU register and clear the TO field.
	 * Since this error is fatal it does not need to be restored.
	 */
	if ((temp_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_NCU_SIG_ENB_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
	    (uint64_t)kt_debug_buf_pa, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_load64 for SSI_SIG_ENB reg "
		    "FAILED, ret=0x%x\n", fname, temp_val);
		return (temp_val);
	}

	/* Set SSI timeout to very low value (0x10) */
	temp_val = ((temp_val & ~KT_NCU_TO_MASK) + (to_val << 10));

	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, KT_NCU_SIG_ENB_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT), temp_val,
	    (uint64_t)kt_debug_buf_pa, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 for "
		    "SSI_SIG_ENB TO FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * XXX	could figure out how to do a PIO read and write
	 *	so the distinct read and write versions of the error
	 *	can predictably be produced.
	 */
	DELAY(10 * MICROSEC);

	return (ret);
}

/*
 * This routine inserts an error into the System-on-Chip (SOC) subsystem of
 * the KT/RF processor via the HW provided error injection register
 * which allows for many of the detectable SOC error types to be injected.
 *
 * For the non-ECC errors the bit to set in the injection register is set
 * in the command definition (in userland) in order to simplify this routine.
 *
 * NOTE: all error types require actual traffic pass through the SOC and
 *	 therefore the NIU errors will require that a network card (such as
 *	 the XAUI) is installed in the system with a working driver.  Right
 *	 now the EI does not send any explcit traffic over this interface
 *	 (since I have no idea how to do it and don't have access to a card).
 */
int
kt_inject_soc_int(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	raddr = mdatap->m_raddr_c;
	uint64_t	soc_inj_val;
	uint64_t	iodata_val;
	uint64_t	cpu_node_id;
	char		*fname = "kt_inject_soc_int";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	DPRINTF(3, "%s: iocp=0x%p, raddr=0x%llx, cpu_node_id=%d\n", fname,
	    iocp, raddr, cpu_node_id);

	soc_inj_val = (KT_SOC_REG_INJ_ENABLE | KT_SOC_REG_SSHOT);

	if (F_INF_INJECT(iocp))
		soc_inj_val &= ~((uint64_t)KT_SOC_REG_SSHOT);

	/*
	 * Some error types use bit fields in the SOC injection register
	 * to indicate an xormask (arrays with ECC protection) or a
	 * specific type of error injection.
	 */
	if (SOC_SUBCLASS_ISSIU(iocp->ioc_command)) {
		/*
		 * Note that the xorpat was already masked in userland.
		 */
		soc_inj_val |= (IOC_XORPAT(iocp) << KT_SOC_SIU_ERROR_SHIFT);
	/*
	 * The arrays with ECC protection allow specific check bits
	 * to be flipped (the xorpat was already masked for these).
	 */
	} else if (SOC_SUBCLASS_ISCPUB(iocp->ioc_command) &&
	    !ERR_PROT_ISPE(iocp->ioc_command)) {
		soc_inj_val |= ((1 << KT_SOC_CBD_SHIFT) | IOC_XORPAT(iocp));

	} else if (SOC_SUBCLASS_ISSIUB(iocp->ioc_command) &&
	    !ERR_PROT_ISPE(iocp->ioc_command)) {
		soc_inj_val |= ((1 << KT_SOC_SBD_SHIFT) | IOC_XORPAT(iocp));

	} else if (SOC_SUBCLASS_ISDMUB(iocp->ioc_command) &&
	    !ERR_PROT_ISPE(iocp->ioc_command)) {
		soc_inj_val |= ((1 << KT_SOC_DBD_SHIFT) | IOC_XORPAT(iocp));

	} else {
		/*
		 * The majority of the error types inject the specified error
		 * simply by setting the appropriate bit in the SOC error
		 * injection register.
		 *
		 * This routine expects that enough background activity
		 * is occuring on the PIU and NIU interfaces for errors to be
		 * injected and detected.
		 */
		soc_inj_val |= IOC_XORPAT(iocp);
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

	/*
	 * Access the IO address to read the data at that location.
	 * This is to be used with any STORE access commands or if the
	 * STORE user option is set to issue a PIO write.
	 */
	if (!F_NOERR(iocp) && (ERR_MISC_ISPIO(iocp->ioc_command) &&
	    (ERR_ACC_ISSTORE(iocp->ioc_command) || F_STORE(iocp)))) {
		iodata_val = peek_asi64(ASI_REAL_IO, raddr);
	}

	/*
	 * For the majority of the erorr types inject the specified error
	 * simply by setting the appropriate bit in the SOC error injection
	 * register.  This routine expects that enough background activity
	 * is occuring on the PIU and NIU interfaces for errors to be
	 * injected and detected.
	 */
	(void) memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, KT_SOC_ERR_INJ_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
	    soc_inj_val, (uint64_t)kt_debug_buf_pa, NULL);

	/*
	 * If we do not want to explicitly invoke the error then return now,
	 * otherwise invoke the error for the types that require it.
	 *
	 * NOTE: to determine which injections require an access can test
	 *	 using the MISC options to force accesses.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (0);
	}

	if (ERR_MISC_ISPIO(iocp->ioc_command)) {
		/*
		 * Set the NOERR flag so that the caller does not
		 * perform another access after the access here.
		 */
		IOC_FLAGS(iocp) |= FLAGS_NOERR;

		if (ERR_ACC_ISSTORE(iocp->ioc_command) || F_STORE(iocp)) {
			poke_asi64(ASI_REAL_IO, raddr, iodata_val);

		} else {	/* default to LOAD access */
			(void) peek_asi64(ASI_REAL_IO, raddr);
		}
	}

	return (0);
}

/*
 * This routine inserts an error into one of the System-on-Chip (SOC) MCU
 * FB DIMM branches of the KT/RF processor.
 *
 * NOTE: the target MCU (branch) is the one which the target EI buffer
 *	 address belongs to.  MCU0 or MCU1 can be selected by using
 *	 offset options since MCU selection is based on PA[6].
 */
int
kt_inject_mcu_fbd(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		err_direction;
	uint_t		err_link;
	uint64_t	fbd_inj_reg_val;
	uint64_t	fbr_cnt_reg_val;
	uint64_t	dram_bank_offset;
	uint64_t	cpu_node_id;
	uint64_t	ret = 0;
	uint64_t	i, read_val, offset;
	char		*fname = "kt_inject_mcu_fbd";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	DPRINTF(3, "%s: iocp=0x%p, cpu_node_id=%d\n", fname, iocp, cpu_node_id);

	/*
	 * Determine the DRAM bank offset.
	 */
	dram_bank_offset = memtest_get_dram_bank_offset(mdatap);

	/*
	 * Add the cpu_node_id to the offset so that it will also
	 * be part of the injection register address used.
	 */
	dram_bank_offset |= (cpu_node_id << KT_CPU_NODE_ID_SHIFT);

	/*
	 * Prior to arming the injection register, determine what value
	 * to write to the "direction" field based on the misc1 argument.
	 *	0 = outgoing -> Alert frame error
	 *	1 = incoming -> CRC error (default)
	 *	n/a -> Alert asserted
	 *	n/a -> Status frame parity error
	 */
	err_direction = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 1);
	if ((err_direction < 0) || (err_direction > 1)) {
		err_direction = 1;
		DPRINTF(0, "%s: invalid misc argument using default %d\n",
		    fname, err_direction);
	}

	/*
	 * If misc2 is set then use that value for the link field.
	 * Each MCU has two links and either or both can be set as targets.
	 */
	err_link = (F_MISC2(iocp) ? (iocp->ioc_misc2) : 1);
	if ((err_link < 0) || (err_link > 1)) {
		err_link = 1;
		DPRINTF(0, "%s: invalid misc argument using default %d\n",
		    fname, err_link);
	}

	/*
	 * Compile the complete value for the injection register.
	 */
	fbd_inj_reg_val = (KT_DRAM_FBD_INJ_ENABLE | IOC_XORPAT(iocp));

	if (!F_INF_INJECT(iocp)) {
		fbd_inj_reg_val |= KT_DRAM_FBD_SSHOT_ENABLE;
	}

	fbd_inj_reg_val |= ((err_direction << KT_DRAM_FBD_DIRECTION_SHIFT) |
	    (err_link << KT_DRAM_FBD_LINK_SHIFT));

	/*
	 * Save the FBR count register (which also contains count enables)
	 * and clear the count enable bits so an error is generated each time.
	 */
	fbr_cnt_reg_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_DRAM_CSR_BASE +
	    KT_DRAM_FBR_COUNT_REG + dram_bank_offset,
	    NULL, NULL, NULL);

	(void) memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, KT_DRAM_CSR_BASE +
	    KT_DRAM_FBR_COUNT_REG + dram_bank_offset,
	    (fbr_cnt_reg_val & ~(uint64_t)KT_DRAM_FBD_COUNT_ENABLE_BITS),
	    (uint64_t)kt_debug_buf_pa, NULL);

	/*
	 * If debug set print out the contents of all of the count regs.
	 */
	DPRINTF(3, "%s: FBR_COUNT_REG 0x%08x.%08x with contents 0x%08x.%08x\n",
	    fname, PRTF_64_TO_32(KT_DRAM_CSR_BASE + KT_DRAM_FBR_COUNT_REG +
	    dram_bank_offset), PRTF_64_TO_32(fbr_cnt_reg_val));

	for (i = 0, offset = 0; i < KT_NUM_DRAM_BRANCHES; i++,
	    offset += KT_DRAM_BRANCH_OFFSET) {

		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, KT_DRAM_CSR_BASE +
		    KT_DRAM_FBR_COUNT_REG + offset,
		    NULL, NULL, NULL);

		DPRINTF(3, "%s: FBR_COUNT_REG %d = 0x%08x.%08x "
		    "contents = 0x%08x.%08x\n",
		    fname, i, PRTF_64_TO_32(KT_DRAM_CSR_BASE +
		    KT_DRAM_FBR_COUNT_REG + offset),
		    PRTF_64_TO_32(read_val));
	}

	/*
	 * Write to injection register to inject error(s).
	 */
	(void) memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, KT_DRAM_CSR_BASE +
	    KT_DRAM_FBD_INJ_ERROR_REG + dram_bank_offset,
	    fbd_inj_reg_val, (uint64_t)kt_debug_buf_pa, NULL);

	/*
	 * Restore the FBR count register before exit.
	 */
	(void) memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, KT_DRAM_CSR_BASE +
	    KT_DRAM_FBR_COUNT_REG + dram_bank_offset,
	    fbr_cnt_reg_val, (uint64_t)kt_debug_buf_pa, NULL);

	return (ret);
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
kt_inject_tlb_l2_miss(mdata_t *mdatap)
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
	char		*fname = "kt_inject_tlb_l2_miss";

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
	 *	 and note that the TSB vaddr is also available:
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
	 * NOTE: also since the TSB is holding tsbe structs, and the
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
	(void) kt_check_l2_idx_mode(paddr, &idx_paddr);

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
	l2_bank_mask = kt_check_l2_bank_mode();
	l2cr_addr = KT_L2_CTL_REG + (paddr & l2_bank_mask);
	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	l2cr_saved = l2cr_value;

	l2cr_value |= KT_L2CR_DMMODE;
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
		ret = memtest_hv_inject_error("kt_inj_l2nd_quick",
		    (void *)kt_inj_l2nd_quick, paddr, IOC_XORPAT(iocp),
		    idx_paddr, NULL);
	} else {
		ret = memtest_hv_inject_error("kt_inj_l2cache_data_quick",
		    (void *)kt_inj_l2cache_data_quick, paddr, IOC_XORPAT(iocp),
		    (uint64_t)F_CHKBIT(iocp), idx_paddr);
	}

	if (ret == -1) {
		DPRINTF(0, "%s: kt_inj_l2cache_data_quick or "
		    "kt_inj_l2nd_quick FAILED, ret=0x%x\n", fname, ret);
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
	 *
	 * NOTE: the N2 clear routine will work for KT.
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
kt_inject_tlb_mem_miss(mdata_t *mdatap)
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
	uint64_t	cpu_node_id;
	uint64_t	eccmask, chunk_value;
	uint64_t	tsbe_tag;
	struct hat 	*sfmmup;
	tte_t		tte;
	struct tsbe	*tsbe_raddr;
	struct tsbe 	*(*sfmmu_get_tsbe_func)(uint64_t, caddr_t, int, int);
	void		(*sfmmu_load_tsbe_func)(struct tsbe *, uint64_t, \
	    tte_t *, int);
	uint64_t	ret = 0;
	char		*fname = "kt_inject_tlb_mem_miss";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

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
	 * Get the mask to use for the ECC bit corruption.
	 */
	eccmask = IOC_XORPAT(iocp);
	eccmask |= (KT_DRAM_INJECTION_ENABLE | KT_DRAM_INJ_SSHOT_ENABLE);

	/*
	 * KT/RF adds a "chunk" field in the DRAM error inject register
	 * to choose between one of two 32-byte chunks to inject the error
	 * into.  This is not based on the address, but injects into either
	 * the non-critical or critical "chunk" (or both).
	 *	01b = 1 = non-critical 32B
	 *	10b = 2 = critical 32B
	 *	11b = 3 = both
	 */
	chunk_value = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 2);
	if ((chunk_value < 0) || (chunk_value > 3)) {
		DPRINTF(0, "%s: invalid misc1 argument for chunk select "
		    "using default %d\n", fname, 2);
		chunk_value = 2;
	}

	eccmask |= (chunk_value << KT_DRAM_INJ_CHUNK_SHIFT);

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
	 * Add the cpu_node_id to the offset so that it will also
	 * be part of the injection register address used.
	 */
	dram_bank_offset |= (cpu_node_id << KT_CPU_NODE_ID_SHIFT);

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
	l2_bank_mask = kt_check_l2_bank_mode();
	l2cr_addr = KT_L2_CTL_REG + (paddr & l2_bank_mask);
	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	l2cr_saved = l2cr_value;

	l2cr_value |= KT_L2CR_DMMODE;
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
	ret = memtest_hv_inject_error("kt_inj_memory_quick",
	    (void *)kt_inj_memory_quick, paddr, eccmask,
	    dram_bank_offset, l2_bank_mask);

	if (ret == -1) {
		DPRINTF(0, "%s: kt_inj_memory_quick FAILED, ret=0x%x\n",
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
	 *
	 * NOTE: the N2 clear routine will work for KT.
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
 * This routine inserts an error into main memory (DRAM) at the specified
 * physical address to be discovered by the memory scrubber which is also
 * set in this routine.
 *
 * NOTE: this routine does not need to restore the scrubber because the
 *	 pre_test and post_test functions handle the restore.
 *
 * NOTE: this routine is called directly from the K/T command list.
 */
int
kt_inject_memory_scrub(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	scrub_freq_csr;
	uint64_t	scrub_ctl_csr;
	uint64_t	scrub_interval;
	uint64_t	ctl_reg_value;
	uint64_t	dram_bank_offset;
	uint64_t	cpu_node_id;
	uint_t		ret;
	char		*fname = "kt_inject_memory_scrub";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	DPRINTF(3, "%s: paddr=0x%llx, cpu_node_id=%d\n", fname,
	    paddr, cpu_node_id);

	/*
	 * Determine the DRAM bank offset.
	 *
	 * Note that if the normal channel/branch/MCU is disabled,
	 * then the scrubber on the channel actually used must be set.
	 * See the comments in the kt_inject_memory() routine for
	 * more information on reduced channel register settings.
	 */
	dram_bank_offset = memtest_get_dram_bank_offset(mdatap);

	/*
	 * Now the values for the freq and scrub CSRs can be determined.
	 */
	scrub_freq_csr = KT_DRAM_CSR_BASE + KT_DRAM_SCRUB_FREQ_REG +
	    dram_bank_offset + (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
	scrub_ctl_csr = KT_DRAM_CSR_BASE + KT_DRAM_SCHD_CTL_REG +
	    dram_bank_offset + (cpu_node_id << KT_CPU_NODE_ID_SHIFT);

	/*
	 * Set the scrub frequency to the fastest safe value (0xff) using
	 * NI_DRAM_SCRUB_INTERVAL_DFLT.
	 *
	 * With this setting all of DRAM will be scrubbed in 81 seconds
	 * for the typical 16 GB config.
	 */
	if (F_MISC2(iocp)) {
		scrub_interval = (iocp->ioc_misc2) &
		    KT_DRAM_SCRUB_INTERVAL_MASK;
	} else {
		scrub_interval = NI_DRAM_SCRUB_INTERVAL_DFLT;	/* 0xff */
	}

	/*
	 * K/T 2.0 adds fields in the frequency register to enable the
	 * scrubber on a DIMM basis.  DIMM0 through DIMM3 in bits[19:16].
	 *
	 * Currently all scrubbers are set to enabled, would be cleaner
	 * if only the DIMM that corresponds to the target address was
	 * enabled.  May need to know the DA (DIMM address) of the target
	 * though which is not a trivial calculation.
	 */
	scrub_interval |= 0xf0000;

	/*
	 * Inject the DRAM error using the normal KT/RF function.
	 */
	if ((ret = kt_inject_memory(mdatap)) == -1) {
		DPRINTF(0, "%s: kt_inject_memory FAILED\n", fname);
		return (ret);
	}

	DPRINTF(3, "%s: kt_inject_memory ret = 0x%x\n", fname, ret);

	/*
	 * Enable the correct banks DRAM scrubber(s) after the error injection,
	 * first the freqency then the enable register.
	 *
	 * NOTE: the reading back of the regs will take a while and can be
	 *	 removed if we need to streamline this routine.
	 */
	if ((ret = memtest_hv_util("hv_paddr_store64", (void *)hv_paddr_store64,
	    scrub_freq_csr, scrub_interval, NULL, NULL)) == -1) {
		return (ret);
	}

	ret = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    scrub_freq_csr, NULL, NULL, NULL);
	DPRINTF(3, "%s: DRAM scrub_freq_csr = 0x%lx\n", fname, ret);

	ctl_reg_value = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, scrub_ctl_csr, NULL, NULL, NULL);
	ctl_reg_value |= KT_DRAM_SCRUB_ENABLE_BIT;

	if ((ret = memtest_hv_util("hv_paddr_store64", (void *)hv_paddr_store64,
	    scrub_ctl_csr, ctl_reg_value, NULL, NULL)) == -1) {
		return (ret);
	}

	ret = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    scrub_ctl_csr, NULL, NULL, NULL);
	DPRINTF(3, "%s: DRAM scrub_ctl_csr = 0x%lx\n", fname, ret);

	/*
	 * Wait a finite amount of time for the scrubber to find the error.
	 */
	DELAY(iocp->ioc_delay * MICROSEC);

	return (0);
}

/*
 * This routine generates SSI (BootROM interface) errors on K/T.
 *
 * Currently the only supported SSI error type is the interface timeout error.
 *
 * K/T can address SSI devices by using the following addresses:
 * Note: instruction fetches from SSI ignore PA[31:27]
 *	fff.f000.0000 SSI base
 *	fff.f080.0000 SSI addressable base (BootROM)
 *
 * Types of detectable SSI errors on K/T:
 *	- SSI Timeout on Load (SSITO) enabled by NERER.ssito
 *	- SSI Timeout on Store (SSITO) enabled by NERER.ssito
 *	- SSI Error on Load (SSIERR) enabled by NERER.ssierr
 *	- SSI Error on Store (SSIERR) enabled by NERER.ssierr
 *	- SSI Protocol Error on Load (SSIPROT) enabled by NERER.ssiprot
 *	- SSI Protocol Error on Store (SSIPROT) enabled by NERER.ssiprot
 *
 * From K/T PRM: SSI stores are "posted". This means that NCU sends a
 * store-ack to the requesting strand before sending out the transaction
 * on the SSI bus. That is why it is not possible to signal a store error
 * to the requester.
 *
 * Since the SSI ERR and protocol errors require a specific error packet
 * to be received by KT/RF the EI can not inject these error types.
 *
 * For the TO errors this routine uses the SSI Timeout Register
 * NCU_SSI_TIMEOUT (0x800.0000.0050).
 *
 * NOTE: writing to random bootrom locations is a very bad idea.
 *	 This is obviously a dangerous practice and is not recommended
 *	 though it can be performed by this routine.
 */
int
kt_k_ssi_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	ssito_reg_val, ioaddr;
	uint64_t	temp_reg, temp_val;
	uint64_t	cpu_node_id;
	int		ret = 0;
	char		*fname = "kt_k_ssi_err";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	/*
	 * Truncate the offset to ensure a valid BootROM address is used.
	 */
	offset &= 0xfff8;

	/*
	 * If NOERR clear the enable bits the for all errors in the NCU
	 * Error Signal Enable reg (NESER), so the NCU status register
	 * must be checked manually for the recorded error(s).
	 *
	 * The value will not be restored by this routine but these
	 * errors will be restored by the enable_errors() routine on a
	 * subsequent error injection.
	 */
	if (F_NOERR(iocp)) {
		/*
		 * Get the NESER value and clear the enable bits.
		 */
		if ((temp_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, KT_NCU_SIG_ENB_REG +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_load64 for SSI_SIG_ENB reg "
			    "FAILED, ret=0x%x\n", fname, temp_val);
			return (temp_val);
		}

		temp_val &= ~(CEEN | NCEEN);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, KT_NCU_SIG_ENB_REG +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT), temp_val,
		    (uint64_t)kt_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for "
			    "SSI_SIG_ENB FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	switch (iocp->ioc_command) {
	case NI_HD_SSITO:
	case NI_HD_SSITOS:
		/*
		 * To generate an error the value of the SSI timer is
		 * set to a low value in the NCU SSI Timeout Register
		 * then an otherwise normal transaction issued.
		 */
		if ((ssito_reg_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, KT_NCU_SSI_TIMEOUT +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_load64 for SSI_TIMEOUT reg "
			    "FAILED, ret=0x%x\n", fname, ssito_reg_val);
			return (ssito_reg_val);
		}

		/* Choose an address within the SSI range */
		ioaddr = KT_SSI_PA_BASE + KT_SSI_BOOT_BASE + offset;

		/* Read and save the data at that location for STORE */
		temp_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, ioaddr,
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);

		/* Set SSI timeout to very low value (0x10) */
		temp_reg = (ssito_reg_val & ~((uint64_t)KT_SSI_TO_MASK)) + 0x10;
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, KT_NCU_SSI_TIMEOUT +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT), temp_reg,
		    (uint64_t)kt_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for "
			    "SSI_TIMEOUT FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* Send an SSI IO transaction out via HV access */
		DELAY(1 * MICROSEC);

		if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, ioaddr, temp_val,
			    (uint64_t)kt_debug_buf_pa, NULL);
		} else { /* LOAD */
			temp_val = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, ioaddr,
			    (uint64_t)kt_debug_buf_pa, NULL, NULL);
		}

		/* Restore the SSI_TIMEOUT timeout value */
		if ((ret = memtest_hv_util("hv_paddr_store64", (void *)
		    hv_paddr_store64, KT_NCU_SSI_TIMEOUT +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT), ssito_reg_val,
		    (uint64_t)kt_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for "
			    "SSI_TIMEOUT FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		/*
		 * RESULTS section.
		 */
		ret = 0;
		break;
	case NI_PRINT_SSI:
		temp_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, KT_NCU_SSI_TIMEOUT +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);
		DPRINTF(0, "SSI_TIMEOUT = 0x%llx\n", temp_val);

		temp_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, KT_NCU_ERR_STS_REG +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);
		DPRINTF(0, "NESR (SSI are bits 50-52) = 0x%llx\n", temp_val);
		ret = 0;
		break;
	default:
		DPRINTF(0, "%s: unsupported SSI command 0x%llx\n",
		    fname, iocp->ioc_command);
		ret = ENOTSUP;
		break;
	}

	return (ret);
}

/*
 * ***************************************************************
 * The following block of routines are the KT/RF support routines.
 * ***************************************************************
 */

/*
 * Determine whether a given paddr (note NOT an raddr) is within the
 * local range of the currently bound cpuid.  The factors involved
 * include the number of nodes in the system and the memory interleave.
 *
 * Note that the plane_flip bit only affects addresses within a node.
 *
 *	2-node, fine interleave		PA[10]
 *	4-node, fine interleave		PA[11:10]
 *	8-node, fine interleave		???
 *	2-node, course interleave	PA[33]
 *	4-node, course interleave	PA[34:33]
 *	8-node, course interleave	???
 *
 * Note that 3-mode systems are a special case of 4-mode where
 * only coarse interleave is supported.
 *
 * See table 9-1 of the version 0.4 PRM for more information.
 */
int
kt_check_is_local_mem(mdata_t *mdatap, uint64_t paddr)
{
	cpu_info_t	*cip = mdatap->m_cip;
	uint_t		cpu_node_id;
	uint_t		mem_node_id;
	uint_t		cpu_mode;
	uint_t		num_nodes;
	char		*fname = "kt_check_is_local_mem";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(cip->c_sys_mode);

	num_nodes = ((cip->c_sys_mode & KT_SYS_MODE_COUNT_MASK) >>
	    KT_SYS_MODE_COUNT_SHIFT) + 1;

	/*
	 * For some reason the PRM mostly only talks about systems up
	 * to four nodes (which is the max for glueless systems)
	 * in most places so this routine likely does not handle the 8
	 * node case properly.
	 */
	cpu_mode = KT_SYS_MODE_GET_MODE(cip->c_sys_mode);

	switch (cpu_mode) {
	case KT_SYS_MODE_1MODE:
		mem_node_id = 0;
		break;

	case KT_SYS_MODE_2MODE:
		if (KT_IS_FINE_INTERLEAVE(cip->c_l2_ctl, paddr)) {
			mem_node_id = KT_2NODE_FINE_PADDR_NODEID(paddr);
		} else {
			mem_node_id = KT_2NODE_COARSE_PADDR_NODEID(paddr);
		}
		break;

	case KT_SYS_MODE_4MODE:
		if ((KT_IS_FINE_INTERLEAVE(cip->c_l2_ctl, paddr)) &&
		    (num_nodes != 3)) {
			mem_node_id = KT_4NODE_FINE_PADDR_NODEID(paddr);
		} else {
			mem_node_id = KT_4NODE_COARSE_PADDR_NODEID(paddr);
		}
		break;

	case KT_SYS_MODE_8MODE:
		/*
		 * XXX The PRM does not describe this case though it could
		 * occur on future systems.  Setting it to no interleave
		 * so that testing on such systems will not be blocked.
		 */
		DPRINTF(0, "%s: 8 mode value read from SYS_MODE "
		    "reg = 0x%x, is not officially supported, using no "
		    "interleave (value = 0)\n", fname, cpu_mode);
		mem_node_id = 0;
		break;

	default:
		DPRINTF(0, "%s: unsupported mode value read from SYS_MODE "
		    "reg = 0x%x\n", fname, cpu_mode);
		return (-1);
	}

	DPRINTF(2, "%s: cpu_node_id=0x%x, mem_node_id=0x%x for "
	    "paddr=0x%llx", fname, cpu_node_id, mem_node_id, paddr);

	if (cpu_node_id == mem_node_id) {
		return (1);
	} else {
		return (0);
	}
}

/*
 * This routine checks the KT/RF L2 cache enable status. It is possible
 * for the L2 cache to be running in reduced bank modes.  The only valid
 * L2 bank/MCU modes are:
 *		16 L2 banks, 2 MCUs (all 4 COUs), 16 cores
 *		8 L2 banks,  2 MCUs (1 COU each), 8 cores
 *		8 L2 banks,  1 MCU  (both COUs),  8 cores
 *		4 L2 banks,  1 MCU  (one COU),    4 cores
 *
 * The banks are enabled or disabled in pairs (called L2T pairs):
 *		L2T0 -> banks 0,8
 *		L2T1 -> banks 1,9
 *		L2T2 -> banks 2,a
 *		L2T3 -> banks 3,b
 *		L2T4 -> banks 4,c
 *		L2T5 -> banks 5,d
 *		L2T6 -> banks 6,e
 *		L2T7 -> banks 7,f
 *
 * Also note that the L2 banks are hard connected to specific DRAM branches.
 *		even L2 banks (0,2, ... 14) -> MCU 0
 *		odd L2 banks (1,3,... 15)   -> MCU 1
 *
 * See table 23-36 in the KT/RF PRM (version 0.4) and the notes on the
 * following page for more details.
 *
 * The value of the L2_BANK_ENABLE (0x1020) register is used to determine
 * if the cache is in a partial mode.
 *
 * The return value is the bitmask to use on a paddr to determine the set
 * of bank registers to use for the low-level routines.  This works
 * because KT presents only the availabhle regs always counting up from
 * offset zero (can be looked at as if the address mask shrinks).
 */
uint64_t
kt_check_l2_bank_mode(void)
{
	uint64_t	l2_bank_mask;
	int		count;

	l2_bank_mask = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_L2_BANK_EN_FULL,
	    NULL, NULL, NULL);

	if (((l2_bank_mask & KT_L2_BANK_EN_ALLEN) ==
	    KT_L2_BANK_EN_ALLEN)) {
		return (KT_L2_16BANK_MASK);
	} else {
		l2_bank_mask &= KT_L2_BANK_EN_ALLEN;

		/*
		 * Count the set bits (L2 bank pairs) in the enable reg.
		 *
		 * Note that this reg contains the number of enabled
		 * L2T blocks (which contain two L2D blocks each).
		 */
		for (count = 0; l2_bank_mask; l2_bank_mask >>= 1) {
			count += (l2_bank_mask & 1);
		}

		if (count == 8) {
			l2_bank_mask = KT_L2_16BANK_MASK; /* all 16 enabled */
		} else if (count == 4) {
			l2_bank_mask = KT_L2_8BANK_MASK; /* eight enabled */
		} else if (count == 2) {
			l2_bank_mask = KT_L2_4BANK_MASK; /* four enabled */
		} else {
			cmn_err(CE_WARN, "kt_check_l2_bank_mode: L2 cache "
			    "on this chip is in an unsupported reduced "
			    "bank mode!\n");
			l2_bank_mask = KT_L2_16BANK_MASK;
		}

		DPRINTF(2, "kt_check_l2_bank_mode: "
		    "will use l2_bank_mask=0x%llx\n", l2_bank_mask);
		return (l2_bank_mask);
	}
}

/*
 * This routine checks the KT/RF L2 cache index hashing (IDX) status
 * and determines the hashed address for the provided address.  The
 * hashing algorithm used by KT/RF is identical to Niagara-II/VF though
 * the registers involved are different.
 *
 * Unlike Niagara-II/VF the IDX mode can be enabled or disabled on the fly,
 * but the EI will not be doing this because it's a heavyweight operation
 * that requires that the caches are entirely cleared beforehand.
 *
 * The index hashing performs the following operations on the address:
 *	bits[17:13]	= PA[32:28] xor PA[17:13]
 *	bits[12:11]	= PA[19:18] xor PA[12:11]
 *	bits[10:9]	= PA[10:9]
 *
 * The hashed address is returned if index hashing is enabled, and the original
 * address is returned if it is disabled since this will index correctly.
 *
 * NOTE: on KT/RF all L2T banks need to be active in order to enable
 *	 hashing (which makes sense).  This routine only checks the
 *	 enable reg to determine the status, it does not check that
 *	 all L2 banks are indeed available.
 */
int
kt_check_l2_idx_mode(uint64_t addr, uint64_t *idx_addr_ptr)
{
	uint64_t	idx_addr;
	int		idx_enabled;
	char		*fname = "kt_check_l2_idx_mode";

	/*
	 * Check if L2 cache has index hashing (IDX) enabled.
	 */
	idx_enabled = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_L2_IDX_HASH_EN_FULL,
	    NULL, NULL, NULL);

	if (idx_enabled & 0x1) {
		/*
		 * L2 cache index hashing (IDX) is enabled, so transform the
		 * addr inpar to it's hashed version.
		 */
		DPRINTF(2, "%s: L2 cache IDX mode is enabled\n", fname);

		idx_addr = ((addr & KT_L2_IDX_TOPBITS) >> 15) ^ addr;
		idx_addr = ((addr & KT_L2_IDX_BOTBITS) >> 7) ^ idx_addr;
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
 * This routine enables or disables the memory (DRAM) or L2 cache scrubber
 * on KT/RF systems using the scrub_info struct.
 *
 * By default this routine only modifies the scrub settings on the DRAM
 * bank that is involved in the test (determined by the paddr).  However if
 * the user has specified that scrubbers should be enabled or disabled the
 * the scrubbers on every DRAM and/or L2 bank will be enabled or disabled.
 */
int
kt_control_scrub(mdata_t *mdatap, uint64_t flags)
{
	scrub_info_t	*scrubp = mdatap->m_scrubp;
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	check_value;
	uint64_t	temp_value;
	uint64_t	temp_fvalue;
	uint64_t	dram_bank_offset;
	uint64_t	cpu_node_id;
	uint64_t	l2_bank_enabled_val;
	uint_t		ret;
	char		*fname = "kt_control_scrub";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	DPRINTF(3, "%s: changing L2 and/or mem scrub settings "
	    "on cpuid=%d, paddr=0x%08x.%08x\n", fname,
	    getprocessorid(), PRTF_64_TO_32(mdatap->m_paddr_c));

	if (flags & MDATA_SCRUB_DRAM) {
		/*
		 * Determine the bank to set scrubber on by finding the
		 * DRAM bank offset to use below.
		 */
		dram_bank_offset = memtest_get_dram_bank_offset(mdatap);

		scrubp->s_memcr_addr = KT_DRAM_CSR_BASE +
		    KT_DRAM_SCHD_CTL_REG + dram_bank_offset +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
		scrubp->s_memfcr_addr = KT_DRAM_CSR_BASE +
		    KT_DRAM_SCRUB_FREQ_REG + dram_bank_offset +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT);

		/*
		 * If not RESTORE then save the current DRAMCR scrub reg vals.
		 * This only needs to be done once even in 2-channel mode since
		 * the register values of both channels always match.
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
		 *
		 * NOTE: KT/RF adds DIMM specific enable bits to the
		 *	 scrub FREQ register.  These are not being
		 *	 modified by this routine.
		 */
		if (flags & MDATA_SCRUB_ENABLE) {
			temp_value = scrubp->s_memcr_value |
			    KT_DRAM_SCRUB_ENABLE_BIT;
		} else if (flags & MDATA_SCRUB_DISABLE) {
			temp_value = scrubp->s_memcr_value &
			    ~(KT_DRAM_SCRUB_ENABLE_BIT);
		} else {	/* restore previously saved value */
			temp_value = scrubp->s_memcr_value;
		}

		temp_fvalue = scrubp->s_memfcr_value;

		/*
		 * If this is a STORM test or if the user specified that the
		 * DRAM scrubbers should be set; the scrubbers on both MCUs
		 * (channels) must be disabled/restored.  Otherwise the correct
		 * register to use is determined from an offset derived from
		 * the physical corruption address (above).
		 *
		 * In either case only channels that are enabled can be
		 * modified, so this must be checked.
		 */
		if (ERR_MISC_ISSTORM(iocp->ioc_command) ||
		    F_MEMSCRUB_EN(iocp) || F_MEMSCRUB_DIS(iocp)) {

			l2_bank_enabled_val = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, KT_L2_BANK_EN_FULL,
			    NULL, NULL, NULL);
			l2_bank_enabled_val &= KT_L2_BANK_EN_ALLEN;

			/*
			 * If MCU0 is enabled set/modify it's scrubber.
			 */
			if ((l2_bank_enabled_val & 0x55) != 0x0) {
				if ((ret = memtest_hv_util(
				    "hv_paddr_store64",
				    (void *)hv_paddr_store64,
				    scrubp->s_memcr_addr,
				    temp_value,
				    NULL, NULL)) == -1) {
					DPRINTF(0, "%s: unable to set "
					    "DRAM CR!\n", fname);
					return (ret);
				}

				if ((ret = memtest_hv_util(
				    "hv_paddr_store64",
				    (void *)hv_paddr_store64,
				    scrubp->s_memfcr_addr,
				    temp_fvalue,
				    NULL, NULL)) == -1) {
					DPRINTF(0, "%s: unable to set "
					    "DRAM FCR!\n", fname);
					return (ret);
				}
			}

			/*
			 * If MCU1 is enabled set/modify it's scrubber.
			 */
			if ((l2_bank_enabled_val & 0xaa) != 0x0) {
				if ((ret = memtest_hv_util(
				    "hv_paddr_store64",
				    (void *)hv_paddr_store64,
				    scrubp->s_memcr_addr +
				    KT_DRAM_BRANCH_OFFSET, temp_value,
				    NULL, NULL)) == -1) {
					DPRINTF(0, "%s: unable to set "
					    "DRAM CR!\n", fname);
					return (ret);
				}

				if ((ret = memtest_hv_util(
				    "hv_paddr_store64",
				    (void *)hv_paddr_store64,
				    scrubp->s_memfcr_addr +
				    KT_DRAM_BRANCH_OFFSET, temp_fvalue,
				    NULL, NULL)) == -1) {
					DPRINTF(0, "%s: unable to set "
					    "DRAM FCR!\n", fname);
					return (ret);
				}
			}

			/*
			 * This routine does not bother to check the values
			 * if both MCUs scubbers were changed.
			 */
			return (0);
		}

		/*
		 * Otherwise disable only the specific channel (using bank
		 * offset already determined above).
		 */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, scrubp->s_memcr_addr,
		    temp_value, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: unable to set DRAM CR!\n", fname);
			return (ret);
		}

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, scrubp->s_memfcr_addr,
		    temp_fvalue, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: unable to set DRAM FCR!\n", fname);
			return (ret);
		}

		/*
		 * Check the values to ensure they were set correctly.
		 */
		if ((check_value = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, scrubp->s_memcr_addr,
		    NULL, NULL, NULL)) != temp_value) {
			DPRINTF(0, "%s: DRAM CR not set properly, value "
			    "is 0x%llx\n", fname, check_value);
			return (-1);
		} else {
			DPRINTF(3, "%s: DRAM CR set to 0x%llx\n",
			    fname, check_value);
		}

		if ((check_value = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, scrubp->s_memfcr_addr,
		    NULL, NULL, NULL)) != temp_fvalue) {
			DPRINTF(0, "%s: DRAM FCR not set properly, value "
			    "is 0x%llx\n", fname, check_value);
			return (-1);
		} else {
			DPRINTF(3, "%s: DRAM FCR set to 0x%llx\n",
			    fname, check_value);
		}
	}

	return (0);
}

/*
 * This routine allows the KT/RF error status registers (ESRs) to
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
kt_debug_clear_esrs(mdata_t *mdatap)
{
	uint64_t	offset;
	uint64_t	read_val;
	uint64_t	l2_bank_enabled_val;
	uint64_t	cpu_node_id;
	int		i, ret;
	char		*fname = "kt_debug_clear_esrs";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	/*
	 * Clear the L2$ ESRs.
	 */
	for (i = 0, offset = 0; i < KT_NUM_L2_BANKS; i++,
	    offset += KT_L2_BANK_OFFSET) {
		/* The first status reg is RW1C so must be read first */
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_L2_ERR_STS_REG + offset),
		    NULL, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_L2_ERR_STS_REG + offset),
		    read_val, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to KT_L2_ERR_STS_REG"
			    " FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* The second status reg is RW so can be simply cleared */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_L2_ERR_STS_REG2 + offset),
		    0ULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to KT_L2_ERR_STS_REG"
			    " FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	/*
	 * Clear the (enabled) DRAM ESRs.
	 */
	l2_bank_enabled_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_L2_BANK_EN_FULL,
	    NULL, NULL, NULL);
	l2_bank_enabled_val &= KT_L2_BANK_EN_ALLEN;

	/*
	 * IF MCU0 is enabled clear its registers.
	 */
	if ((l2_bank_enabled_val & 0x55) != 0x0) {
		offset = (cpu_node_id << KT_CPU_NODE_ID_SHIFT);

		/* KT_DRAM_ERROR_STATUS_REG is RW1C */
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_STATUS_REG + offset),
		    NULL, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_STATUS_REG + offset), read_val,
		    NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "N2_DRAM_ERROR_STATUS_REG FAILED, "
			    " ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* KT_DRAM_ERROR_ADDR_REG is RW */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_ADDR_REG + offset), 0ULL, NULL,
		    NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "KT_DRAM_ERROR_ADDR_REG FAILED, "
			    "ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* KT_DRAM_ERROR_SYND_REG is RW */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_SYND_REG + offset), 0ULL, NULL,
		    NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "KT_DRAM_ERROR_SYND_REG FAILED, "
			    "ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* KT_DRAM_ERROR_LOC_REG is RW */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_LOC_REG + offset), 0ULL, NULL,
		    NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "KT_DRAM_ERROR_LOC_REG FAILED, "
			    "ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* KT_DRAM_ERROR_RETRY_STS_REG is RW */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_RETRY_STS_REG + offset), 0ULL, NULL,
		    NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "KT_DRAM_ERROR_RETRY_STS_REG FAILED, "
			    "ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* KT_DRAM_FBD_ERROR_STATUS_REG is RW1C */
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_FBD_ERROR_STATUS_REG + offset),
		    NULL, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_FBD_ERROR_STATUS_REG + offset),
		    read_val, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "KT_DRAM_FBD_ERROR_STATUS_REG FAILED, "
			    "ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	/*
	 * If MCU1 is enabled clear its registers.
	 */
	if ((l2_bank_enabled_val & 0xaa) != 0x0) {
		offset = (cpu_node_id << KT_CPU_NODE_ID_SHIFT) +
		    KT_DRAM_BRANCH_OFFSET;

		/* KT_DRAM_ERROR_STATUS_REG is RW1C */
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_STATUS_REG + offset),
		    NULL, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_STATUS_REG + offset), read_val,
		    NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "N2_DRAM_ERROR_STATUS_REG FAILED, "
			    " ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* KT_DRAM_ERROR_ADDR_REG is RW */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_ADDR_REG + offset), 0ULL, NULL,
		    NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "KT_DRAM_ERROR_ADDR_REG FAILED, "
			    "ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* KT_DRAM_ERROR_SYND_REG is RW */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_SYND_REG + offset), 0ULL, NULL,
		    NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "KT_DRAM_ERROR_SYND_REG FAILED, "
			    "ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* KT_DRAM_ERROR_LOC_REG is RW */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_LOC_REG + offset), 0ULL, NULL,
		    NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "KT_DRAM_ERROR_LOC_REG FAILED, "
			    "ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* KT_DRAM_ERROR_RETRY_STS_REG is RW */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_RETRY_STS_REG + offset), 0ULL, NULL,
		    NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "KT_DRAM_ERROR_RETRY_STS_REG FAILED, "
			    "ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* KT_DRAM_FBD_ERROR_STATUS_REG is RW1C */
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_FBD_ERROR_STATUS_REG + offset),
		    NULL, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_FBD_ERROR_STATUS_REG + offset),
		    read_val, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to "
			    "KT_DRAM_FBD_ERROR_STATUS_REG FAILED, "
			    "ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	/*
	 * Skip the internal ESRs since they do not seem to have
	 * issues with bits remaining set.
	 *
	 * The NCU (SOC) ESR is cleared, note this reg is RW1C.
	 */
	read_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_NCU_ERR_STS_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT), NULL, NULL, NULL);

	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, KT_NCU_ERR_STS_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT), read_val,
	    NULL, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to KT_NCU_ERR_STS_REG"
		    " FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * Clear the LFU status regs, they are also RW1C.
	 */
	for (i = 0, offset = (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
	    i < KT_LFU_NUM_UNITS; i++, offset += KT_LFU_REG_STEP) {
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, KT_LFU_ERR_STS_REG + offset,
		    NULL, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, KT_LFU_ERR_STS_REG + offset,
		    read_val, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to KT_LFU_ERR_STS_REG"
			    " %d FAILED, ret=0x%x\n", fname, i, ret);
			return (ret);
		}
	}

	return (0);
}

/*
 * This routine allows the KT/RF error status registers (ESRs) to
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
kt_debug_print_esrs(mdata_t *mdatap)
{
	uint64_t	offset;
	uint64_t	read_val1, read_val2;
	uint64_t	l2_bank_enabled_val;
	uint64_t	cpu_node_id;
	int		i;
	char		*fname = "kt_debug_print_esrs";

	cmn_err(CE_CONT, "%s: the error registers for cpuid = %d are:",
	    fname, mdatap->m_cip->c_cpuid);

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	read_val1 = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_COU_CEILING_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT), NULL, NULL, NULL);

	cmn_err(CE_CONT, "SYS mode reg = 0x%08x.%08x, and "
	    "COU ceiling reg = 0x%08x.%08x\n",
	    PRTF_64_TO_32(mdatap->m_cip->c_sys_mode),
	    PRTF_64_TO_32(read_val1));

	/*
	 * Print the L2 bank enabled register value.
	 * Can look up enabled banks in PRM table 23-36.
	 */
	read_val1 = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_L2_BANK_EN_FULL,
	    NULL, NULL, NULL);

	read_val2 = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_L2_IDX_HASH_EN_FULL,
	    NULL, NULL, NULL);

	cmn_err(CE_CONT, "L2 bank enabled (LSB byte) = 0x%08x.%08x, and "
	    "hash enabled = 0x%lx\n", PRTF_64_TO_32(read_val1), read_val2);

	/*
	 * Print the L2$ ESRs.
	 */
	for (i = 0, offset = 0; i < KT_NUM_L2_BANKS; i++,
	    offset += KT_L2_BANK_OFFSET) {
		read_val1 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_L2_ERR_STS_REG + offset),
		    NULL, NULL, NULL);

		read_val2 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_L2_ERR_STS_REG2 + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "L2_ESR %x = 0x%08x.%08x, "
		    "L2_ESR2 %x = 0x%08x.%08x\n", i,
		    PRTF_64_TO_32(read_val1), i, PRTF_64_TO_32(read_val2));

		/*
		 * Also print the L2 error enable registers.
		 */
		read_val1 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_L2_ERR_REC_REG + offset),
		    NULL, NULL, NULL);

		read_val2 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_L2_ERR_SIG_REG + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "L2_ERER %x = 0x%08x.%08x, "
		    "L2_ESER %x = 0x%08x.%08x\n", i,
		    PRTF_64_TO_32(read_val1), i, PRTF_64_TO_32(read_val2));
	}

	/*
	 * Print the (enabled) DRAM ESRs.
	 */
	l2_bank_enabled_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_L2_BANK_EN_FULL,
	    NULL, NULL, NULL);
	l2_bank_enabled_val &= KT_L2_BANK_EN_ALLEN;

	/*
	 * IF MCU0 is enabled read its registers.
	 */
	if ((l2_bank_enabled_val & 0x55) != 0x0) {
		offset = (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
		i = 0;

		read_val1 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_STATUS_REG + offset),
		    NULL, NULL, NULL);

		read_val2 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_FBD_ERROR_STATUS_REG + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "DRAM_ESR %d = 0x%08x.%08x "
		    "DRAM_FBD %d = 0x%08x.%08x\n",
		    i, PRTF_64_TO_32(read_val1),
		    i, PRTF_64_TO_32(read_val2));

		/*
		 * Also print the MCU syndrome and location regs.
		 */
		read_val1 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_SYND_REG + offset),
		    NULL, NULL, NULL);

		read_val2 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_LOC_REG + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "DRAM_SYND %d = 0x%08x.%08x "
		    "DRAM_LOC %d = 0x%08x.%08x\n",
		    i, PRTF_64_TO_32(read_val1),
		    i, PRTF_64_TO_32(read_val2));

		/*
		 * Also print the MCU error enable registers.
		 */
		read_val1 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERR_REC_REG + offset),
		    NULL, NULL, NULL);

		read_val2 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERR_SIG_REG + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "DRAM_ERER %d = 0x%08x.%08x "
		    "DRAM_ESER %d = 0x%08x.%08x\n",
		    i, PRTF_64_TO_32(read_val1),
		    i, PRTF_64_TO_32(read_val2));
	}

	/*
	 * IF MCU1 is enabled read its registers.
	 */
	if ((l2_bank_enabled_val & 0xaa) != 0x0) {
		offset = (cpu_node_id << KT_CPU_NODE_ID_SHIFT) +
		    KT_DRAM_BRANCH_OFFSET;
		i = 1;

		read_val1 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_STATUS_REG + offset),
		    NULL, NULL, NULL);

		read_val2 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_FBD_ERROR_STATUS_REG + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "DRAM_ESR %d = 0x%08x.%08x "
		    "DRAM_FBD %d = 0x%08x.%08x\n",
		    i, PRTF_64_TO_32(read_val1),
		    i, PRTF_64_TO_32(read_val2));

		/*
		 * Also print the MCU syndrome and location regs.
		 */
		read_val1 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_SYND_REG + offset),
		    NULL, NULL, NULL);

		read_val2 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERROR_LOC_REG + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "DRAM_SYND %d = 0x%08x.%08x "
		    "DRAM_LOC %d = 0x%08x.%08x\n",
		    i, PRTF_64_TO_32(read_val1),
		    i, PRTF_64_TO_32(read_val2));

		/*
		 * Also print the MCU error enable registers.
		 */
		read_val1 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERR_REC_REG + offset),
		    NULL, NULL, NULL);

		read_val2 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERR_SIG_REG + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "DRAM_ERER %d = 0x%08x.%08x "
		    "DRAM_ESER %d = 0x%08x.%08x\n",
		    i, PRTF_64_TO_32(read_val1),
		    i, PRTF_64_TO_32(read_val2));
	}

	/*
	 * Print the strand and core error status regs (CERER, and SETER),
	 * these are accessed the same way as on N2.
	 */
	read_val1 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, N2_CERER_REG, NULL, NULL);

	read_val2 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, N2_SETER_REG, NULL, NULL);

	cmn_err(CE_CONT, "CERER = 0x%08x.%08x, SETER = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1), PRTF_64_TO_32(read_val2));

	/*
	 * Print the internal error status regs,
	 * these are accessed the same way as on N2.
	 */
	read_val1 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, 0x0, NULL, NULL);

	read_val2 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, 0x8, NULL, NULL);

	cmn_err(CE_CONT, "DESR = 0x%08x.%08x, DFESR = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1), PRTF_64_TO_32(read_val2));

	read_val1 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_IMMU, 0x18, NULL, NULL);

	cmn_err(CE_CONT, "ISFSR = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1));

	read_val1 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_DMMU, 0x18, NULL, NULL);

	read_val2 = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_DMMU, 0x20, NULL, NULL);

	cmn_err(CE_CONT, "DSFSR = 0x%08x.%08x, DSFAR = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1), PRTF_64_TO_32(read_val2));

	/*
	 * Print the NCU (SOC) ESR, ERER, ESER, and Fatal Reset reg.
	 */
	read_val1 = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    KT_NCU_ERR_STS_REG + (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
	    NULL, NULL, NULL);

	read_val2 = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    KT_NCU_LOG_ENB_REG + (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
	    NULL, NULL, NULL);

	cmn_err(CE_CONT, "NCU/SOC ESR = 0x%08x.%08x, "
	    "NCU/SOC ERER = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1), PRTF_64_TO_32(read_val2));

	read_val1 = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    KT_NCU_SIG_ENB_REG + (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
	    NULL, NULL, NULL);

	read_val2 = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    KT_RESET_FAT_ENB_REG + (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
	    NULL, NULL, NULL);

	cmn_err(CE_CONT, "NCU/SOC ESER = 0x%08x.%08x, "
	    "RST FATAL = 0x%08x.%08x\n",
	    PRTF_64_TO_32(read_val1), PRTF_64_TO_32(read_val2));

	/*
	 * Print the LFU status regs.
	 */
	for (i = 0, offset = (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
	    i < KT_LFU_NUM_UNITS; i++, offset += KT_LFU_REG_STEP) {
		read_val1 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, KT_LFU_ERR_STS_REG + offset,
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "LFU %d ESR = 0x%08x.%08x\n",
		    i, PRTF_64_TO_32(read_val1));
	}

	/*
	 * Determine the physical address base (range) which the hypervisor
	 * is using for it's displacement flush area (print it out).
	 */
	if ((read_val1 = memtest_hv_util("kt_l2_get_flushbase",
	    (void *)kt_l2_get_flushbase, NULL, NULL, NULL, NULL)) == -1) {
		cmn_err(CE_CONT, "trap to kt_l2_get_flushbase "
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
 * Because KT/RF is a CMT and to minimize system impact, by default the
 * injector will only the set the error checking on the strand that is expected
 * to see the error (this will be determined by higher level framework code).
 *
 * KT/RF has the following error enable registers:
 *	MCU rec	- 2 per processor (one for each MCU)
 *	MCU sig	- 2 per processor (one for each MCU)
 *	L2$ rec	- 16 per processor (one for each bank)
 *	L2$ sig	- 16 per processor (one for each bank)
 *	CERER	- SPARC 16 per processor (one per physical core)
 *	SETER	- SPARC 8 per core (one per strand)
 *	NCU rec	- 1 per processor (enables NCU error logging)
 *	NCU sig - 1 per processor (enables NCU error interrupts)
 *
 *	LFU rec - 3 per processor (enables Link error logging on each link)
 *	LFU sig - 3 per processor (enables Link error interrupts on each link)
 *	Reset Fat - 1 per processor (enables Fatal errors)
 *
 * NOTE: the SPARC error enable registers seem to be only available to the
 *	 strand that owns it.  This means each strand involved in the test
 *	 must enable it's own registers separately via the xcall code.
 *	 These are accessed the same way as on N2.
 *
 * NOTE: this routine is NOT setting or checking the PIU error enable
 *	 registers since these are related only to the IO errors.
 *
 * NOTE: the SSI and SOC error enable bits are in the NCU registers.
 */
int
kt_enable_errors(mdata_t *mdatap)
{
	uint64_t	exp_mcurec_set[KT_NUM_DRAM_BRANCHES];
	uint64_t	exp_mcusig_set[KT_NUM_DRAM_BRANCHES];
	uint64_t	exp_l2rec_set[KT_NUM_L2_BANKS];
	uint64_t	exp_l2sig_set[KT_NUM_L2_BANKS];
	uint64_t	exp_cerer_set, exp_seter_set;
	uint64_t	exp_ncurec_set, exp_ncusig_set;
	uint64_t	exp_lfurec_set, exp_lfusig_set, exp_fat_set;

	uint64_t	exp_mcurec_clr[KT_NUM_DRAM_BRANCHES];
	uint64_t	exp_mcusig_clr[KT_NUM_DRAM_BRANCHES];
	uint64_t	exp_l2rec_clr[KT_NUM_L2_BANKS];
	uint64_t	exp_l2sig_clr[KT_NUM_L2_BANKS];
	uint64_t	exp_cerer_clr, exp_seter_clr;
	uint64_t	exp_ncurec_clr, exp_ncusig_clr;
	uint64_t	exp_lfurec_clr, exp_lfusig_clr, exp_fat_clr;

	uint64_t	obs_mcurec_val[KT_NUM_DRAM_BRANCHES];
	uint64_t	obs_mcusig_val[KT_NUM_DRAM_BRANCHES];
	uint64_t	obs_l2rec_val[KT_NUM_L2_BANKS];
	uint64_t	obs_l2sig_val[KT_NUM_L2_BANKS];
	uint64_t	obs_cerer_val, obs_seter_val;
	uint64_t	obs_ncurec_val, obs_ncusig_val;
	uint64_t	obs_lfurec_val, obs_lfusig_val, obs_fat_val;

	uint64_t	set_mcurec_val[KT_NUM_DRAM_BRANCHES];
	uint64_t	set_mcusig_val[KT_NUM_DRAM_BRANCHES];
	uint64_t	set_l2rec_val[KT_NUM_L2_BANKS];
	uint64_t	set_l2sig_val[KT_NUM_L2_BANKS];
	uint64_t	set_cerer_val, set_seter_val;
	uint64_t	set_ncurec_val, set_ncusig_val;
	uint64_t	set_lfurec_val, set_lfusig_val, set_fat_val;

	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	l2_bank_enabled_val;
	uint64_t	cpu_node_id;
	uint64_t	offset;
	int		i;
	char		*fname = "kt_enable_errors";

	cpu_node_id = KT_SYS_MODE_GET_NODEID(mdatap->m_cip->c_sys_mode);

	/*
	 * Define default (expected) register settings.
	 */
	for (i = 0; i < KT_NUM_DRAM_BRANCHES; i++) {
		exp_mcurec_set[i] = KT_DRAM_ERR_REC_ERREN;
		exp_mcurec_clr[i] = 0;

		exp_mcusig_set[i] = KT_DRAM_ERR_SIG_ERREN;
		exp_mcusig_clr[i] = 0;
	}

	for (i = 0; i < KT_NUM_L2_BANKS; i++) {
		exp_l2rec_set[i] = KT_L2_ERR_REC_ERREN;
		exp_l2rec_clr[i] = 0;

		exp_l2sig_set[i] = CEEN | NCEEN;
		exp_l2sig_clr[i] = 0;
	}

	exp_cerer_set	= KT_CERER_ERREN;
	exp_seter_set	= KT_SETER_ERREN;
	exp_ncurec_set	= KT_NCU_LOG_REG_ERREN;
	exp_ncusig_set	= CEEN | NCEEN;

	exp_cerer_clr	= 0;
	exp_seter_clr	= 0;
	exp_ncurec_clr	= 0;
	exp_ncusig_clr	= 0;

	exp_lfurec_set	= 0x2;	/* CRC enable */
	exp_lfusig_set	= CEEN | NCEEN;
	exp_fat_set	= KT_RESET_FAT_ENB_ERREN;

	exp_lfurec_clr	= exp_lfusig_clr = 0;
	exp_fat_clr	= 0;

	DPRINTF(2, "%s: exp_mcurec_set=0x%llx, exp_mcusig_set=0x%llx, "
	    "exp_l2rec_set=0x%llx, exp_cerer_set=0x%llx, "
	    "exp_seter_set=0x%llx, exp_ncurec_set=0x%llx, exp_fat_set=0x%llx"
	    "\n", fname, exp_mcurec_set[0], exp_mcusig_set[0],
	    exp_l2rec_set[0], exp_cerer_set, exp_seter_set,
	    exp_ncurec_set, exp_fat_set);

	/*
	 * Get the current value of each of the MCU registers.
	 * But only read/write the registers of enabled MCUs.
	 */
	l2_bank_enabled_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_L2_BANK_EN_FULL,
	    NULL, NULL, NULL);
	l2_bank_enabled_val &= KT_L2_BANK_EN_ALLEN;

	/*
	 * IF MCU0 is enabled read its registers.
	 */
	if ((l2_bank_enabled_val & 0x55) != 0x0) {
		offset = (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
		i = 0;

		obs_mcurec_val[i] = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERR_REC_REG + offset),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);

		obs_mcusig_val[i] = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERR_SIG_REG + offset),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);
	}

	/*
	 * If MCU1 is enabled read its registers.
	 */
	if ((l2_bank_enabled_val & 0xaa) != 0x0) {
		offset = (cpu_node_id << KT_CPU_NODE_ID_SHIFT) +
		    KT_DRAM_BRANCH_OFFSET;
		i = 1;

		obs_mcurec_val[i] = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERR_REC_REG + offset),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);

		obs_mcusig_val[i] = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
		    KT_DRAM_ERR_SIG_REG + offset),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);
	}

	/*
	 * Get the current value of each of the L2$ registers.
	 */
	for (i = 0, offset = 0; i < KT_NUM_L2_BANKS; i++,
	    offset += KT_L2_BANK_OFFSET) {
		obs_l2rec_val[i] = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_L2_ERR_REC_REG + offset),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);

		obs_l2sig_val[i] = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (KT_L2_ERR_SIG_REG + offset),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);
	}

	/*
	 * Get the contents of the other enable registers.
	 */
	obs_cerer_val = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, N2_CERER_REG,
	    (uint64_t)kt_debug_buf_pa, NULL);

	obs_seter_val = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, N2_SETER_REG,
	    (uint64_t)kt_debug_buf_pa, NULL);

	/*
	 * NOTE: would be a nice enhancement to quiece IO (is possible
	 *	 on KT) before touching any of the SOC/NCU regs.
	 */
	obs_ncurec_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_NCU_LOG_ENB_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
	    (uint64_t)kt_debug_buf_pa, NULL, NULL);

	obs_ncusig_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_NCU_SIG_ENB_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
	    (uint64_t)kt_debug_buf_pa, NULL, NULL);

	/*
	 * NOTE: ideally should look at the registers for each LFU
	 *	 instead of just the first one.  There are three
	 *	 LFUs each with it's own set of registers (step 2048).
	 */
	obs_lfurec_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_LFU_LOG_ENB_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
	    (uint64_t)kt_debug_buf_pa, NULL, NULL);

	obs_lfusig_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_LFU_SIG_ENB_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
	    (uint64_t)kt_debug_buf_pa, NULL, NULL);

	obs_fat_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_RESET_FAT_ENB_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
	    (uint64_t)kt_debug_buf_pa, NULL, NULL);

	DPRINTF(2, "%s: obs_mcurec_val=0x%llx, obs_mcusig_val=0x%llx, "
	    "obs_l2rec_val=0x%llx, obs_l2sig_val=0x%llx, "
	    "obs_cerer_val=0x%llx, obs_seter_val=0x0x%llx, "
	    "obs_ncurec_val=0x%llx, obs_lfurec_val=0x%llx "
	    "obs_lfusig_val=0x%llx, obs_fat_val=0x%llx\n", fname,
	    obs_mcurec_val[0], obs_mcusig_val[0],
	    obs_l2rec_val[0], obs_l2sig_val[0], obs_cerer_val, obs_seter_val,
	    obs_ncurec_val, obs_lfurec_val, obs_lfusig_val, obs_fat_val);

	/*
	 * Determine the register values either specified via command line
	 * options or using a combination of the existing values plus the
	 * bits required to be set minus the bits required to be clear.
	 *
	 * NOTE: KT/RF has no fields that need to be cleared.
	 */
	for (i = 0; i < KT_NUM_DRAM_BRANCHES; i++) {
		set_mcurec_val[i] = (obs_mcurec_val[i] | exp_mcurec_set[i])
		    & ~exp_mcurec_clr[i];

		set_mcusig_val[i] = (obs_mcusig_val[i] | exp_mcusig_set[i])
		    & ~exp_mcusig_clr[i];
	}

	for (i = 0; i < KT_NUM_L2_BANKS; i++) {
		set_l2rec_val[i] = (obs_l2rec_val[i] | exp_l2rec_set[i])
		    & ~exp_l2rec_clr[i];

		set_l2sig_val[i] = (obs_l2sig_val[i] | exp_l2sig_set[i])
		    & ~exp_l2sig_clr[i];
	}

	set_cerer_val	= (obs_cerer_val | exp_cerer_set) & ~exp_cerer_clr;
	set_seter_val	= (obs_seter_val | exp_seter_set) & ~exp_seter_clr;

	set_ncurec_val	= (obs_ncurec_val | exp_ncurec_set) & ~exp_ncurec_clr;
	set_ncusig_val	= (obs_ncusig_val | exp_ncusig_set) & ~exp_ncusig_clr;

	set_lfurec_val	= (obs_lfurec_val | exp_lfurec_set) & ~exp_lfurec_clr;
	set_lfusig_val	= (obs_lfusig_val | exp_lfusig_set) & ~exp_lfusig_clr;
	set_fat_val	= (obs_fat_val | exp_fat_set) & ~exp_fat_clr;

	DPRINTF(2, "%s: set_mcurec_val=0x%llx, set_mcusig_val=0x%llx, "
	    "set_l2rec_val=0x%llx, set_l2sig_val=0x%llx, "
	    "set_cerer_val=0x%llx, set_seter_val=0x0x%llx, "
	    "set_ncurec_val=0x%llx, set_lfurec_val=0x%llx "
	    "set_lfusig_val=0x%llx, set_fat_val=0x%llx\n", fname,
	    set_mcurec_val[0], set_mcusig_val[0],
	    set_l2rec_val[0], set_l2sig_val[0], set_cerer_val, set_seter_val,
	    set_ncurec_val, set_lfurec_val, set_lfusig_val, set_fat_val);

	/*
	 * Set and verify the MCU register settings if required.
	 */

	/*
	 * IF MCU0 is enabled write its registers.
	 */
	if ((l2_bank_enabled_val & 0x55) != 0x0) {
		offset = (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
		i = 0;

		if (obs_mcurec_val[i] != set_mcurec_val[i]) {
			if (F_VERBOSE(iocp)) {
				cmn_err(CE_NOTE, "setting MCU ERER reg %d to "
				    "new value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)\n", i,
				    PRTF_64_TO_32(obs_mcurec_val[i]),
				    PRTF_64_TO_32(set_mcurec_val[i]));
			}

			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
			    KT_DRAM_ERR_REC_REG + offset), set_mcurec_val[i],
			    (uint64_t)kt_debug_buf_pa, NULL);
			/*
			 * Verify that the value was set correctly.
			 */
			obs_mcurec_val[i] = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
			    KT_DRAM_ERR_REC_REG + offset),
			    (uint64_t)kt_debug_buf_pa, NULL, NULL);
			if (obs_mcurec_val[i] != set_mcurec_val[i]) {
				cmn_err(CE_WARN, "couldn't set MCU err record"
				    "ing reg at offset 0x%lx to desired value "
				    "(obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)!\n", offset,
				    PRTF_64_TO_32(obs_mcurec_val[i]),
				    PRTF_64_TO_32(set_mcurec_val[i]));
				return (-1);
			}
		}

		if (obs_mcusig_val[i] != set_mcusig_val[i]) {
			if (F_VERBOSE(iocp)) {
				cmn_err(CE_NOTE, "setting MCU ESER reg %d to "
				    "new value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)\n", i,
				    PRTF_64_TO_32(obs_mcusig_val[i]),
				    PRTF_64_TO_32(set_mcusig_val[i]));
			}

			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64,
			    (KT_DRAM_ERR_SIG_REG + offset), set_mcusig_val[i],
			    (uint64_t)kt_debug_buf_pa, NULL);
			/*
			 * Verify that the value was set correctly.
			 */
			obs_mcusig_val[i] = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64,
			    (KT_DRAM_ERR_SIG_REG + offset),
			    (uint64_t)kt_debug_buf_pa, NULL, NULL);
			if (obs_mcusig_val[i] != set_mcusig_val[i]) {
				cmn_err(CE_WARN, "couldn't set MCU signal reg "
				    "at offset 0x%lx to desired value "
				    "(obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)!\n", offset,
				    PRTF_64_TO_32(obs_mcusig_val[i]),
				    PRTF_64_TO_32(set_mcusig_val[i]));
				return (-1);
			}
		}
	}

	/*
	 * If MCU1 is enabled write its registers.
	 */
	if ((l2_bank_enabled_val & 0xaa) != 0x0) {
		offset = (cpu_node_id << KT_CPU_NODE_ID_SHIFT) +
		    KT_DRAM_BRANCH_OFFSET;
		i = 1;

		if (obs_mcurec_val[i] != set_mcurec_val[i]) {
			if (F_VERBOSE(iocp)) {
				cmn_err(CE_NOTE, "setting MCU ERER reg %d to "
				    "new value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)\n", i,
				    PRTF_64_TO_32(obs_mcurec_val[i]),
				    PRTF_64_TO_32(set_mcurec_val[i]));
			}

			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, (KT_DRAM_CSR_BASE +
			    KT_DRAM_ERR_REC_REG + offset), set_mcurec_val[i],
			    (uint64_t)kt_debug_buf_pa, NULL);
			/*
			 * Verify that the value was set correctly.
			 */
			obs_mcurec_val[i] = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, (KT_DRAM_CSR_BASE +
			    KT_DRAM_ERR_REC_REG + offset),
			    (uint64_t)kt_debug_buf_pa, NULL, NULL);
			if (obs_mcurec_val[i] != set_mcurec_val[i]) {
				cmn_err(CE_WARN, "couldn't set MCU err record"
				    "ing reg at offset 0x%lx to desired value "
				    "(obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)!\n", offset,
				    PRTF_64_TO_32(obs_mcurec_val[i]),
				    PRTF_64_TO_32(set_mcurec_val[i]));
				return (-1);
			}
		}

		if (obs_mcusig_val[i] != set_mcusig_val[i]) {
			if (F_VERBOSE(iocp)) {
				cmn_err(CE_NOTE, "setting MCU ESER reg %d to "
				    "new value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)\n", i,
				    PRTF_64_TO_32(obs_mcusig_val[i]),
				    PRTF_64_TO_32(set_mcusig_val[i]));
			}

			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64,
			    (KT_DRAM_ERR_SIG_REG + offset), set_mcusig_val[i],
			    (uint64_t)kt_debug_buf_pa, NULL);
			/*
			 * Verify that the value was set correctly.
			 */
			obs_mcusig_val[i] = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64,
			    (KT_DRAM_ERR_SIG_REG + offset),
			    (uint64_t)kt_debug_buf_pa, NULL, NULL);
			if (obs_mcusig_val[i] != set_mcusig_val[i]) {
				cmn_err(CE_WARN, "couldn't set MCU signal reg "
				    "at offset 0x%lx to desired value "
				    "(obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)!\n", offset,
				    PRTF_64_TO_32(obs_mcusig_val[i]),
				    PRTF_64_TO_32(set_mcusig_val[i]));
				return (-1);
			}
		}
	}

	/*
	 * Set and verify the L2 register settings if required.
	 */
	for (i = 0, offset = 0; i < KT_NUM_L2_BANKS; i++,
	    offset += KT_L2_BANK_OFFSET) {
		if (obs_l2rec_val[i] != set_l2rec_val[i]) {
			if (F_VERBOSE(iocp)) {
				cmn_err(CE_NOTE, "setting L2 ERER reg %d to "
				    "new value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)\n", i,
				    PRTF_64_TO_32(obs_l2rec_val[i]),
				    PRTF_64_TO_32(set_l2rec_val[i]));
			}

			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64,
			    (KT_L2_ERR_REC_REG + offset), set_l2rec_val[i],
			    (uint64_t)kt_debug_buf_pa, NULL);
			/*
			 * Verify that the value was set correctly.
			 */
			obs_l2rec_val[i] = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64,
			    (KT_L2_ERR_REC_REG + offset),
			    (uint64_t)kt_debug_buf_pa, NULL, NULL);
			if (obs_l2rec_val[i] != set_l2rec_val[i]) {
				cmn_err(CE_WARN, "couldn't set L2 err recording"
				    " reg at offset 0x%lx to desired value "
				    "(obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)!\n", offset,
				    PRTF_64_TO_32(obs_l2rec_val[i]),
				    PRTF_64_TO_32(set_l2rec_val[i]));
				return (-1);
			}
		}

		if (obs_l2sig_val[i] != set_l2sig_val[i]) {
			if (F_VERBOSE(iocp)) {
				cmn_err(CE_NOTE, "setting L2 ESER reg %d to "
				    "new value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)\n", i,
				    PRTF_64_TO_32(obs_l2sig_val[i]),
				    PRTF_64_TO_32(set_l2sig_val[i]));
			}

			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64,
			    (KT_L2_ERR_SIG_REG + offset), set_l2sig_val[i],
			    (uint64_t)kt_debug_buf_pa, NULL);
			/*
			 * Verify that the value was set correctly.
			 */
			obs_l2sig_val[i] = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64,
			    (KT_L2_ERR_SIG_REG + offset),
			    (uint64_t)kt_debug_buf_pa, NULL, NULL);
			if (obs_l2sig_val[i] != set_l2sig_val[i]) {
				cmn_err(CE_WARN, "couldn't set L2 signal reg "
				    "at offset 0x%lx to desired value "
				    "(obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)!\n", offset,
				    PRTF_64_TO_32(obs_l2sig_val[i]),
				    PRTF_64_TO_32(set_l2sig_val[i]));
				return (-1);
			}
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
		    set_cerer_val, (uint64_t)kt_debug_buf_pa);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_cerer_val = memtest_hv_util("hv_asi_load64",
		    (void *)hv_asi_load64, ASI_ERROR_STATUS, N2_CERER_REG,
		    (uint64_t)kt_debug_buf_pa, NULL);
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
		    set_seter_val, (uint64_t)kt_debug_buf_pa);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_seter_val = memtest_hv_util("hv_asi_load64",
		    (void *)hv_asi_load64, ASI_ERROR_STATUS, N2_SETER_REG,
		    (uint64_t)kt_debug_buf_pa, NULL);
		if (obs_seter_val != set_seter_val) {
			cmn_err(CE_WARN, "couldn't set SETER reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_seter_val),
			    PRTF_64_TO_32(set_seter_val));
			return (-1);
		}
	}

	/*
	 * Set and verify the NCU (SSI and SOC) register settings if required.
	 */
	if (obs_ncurec_val != set_ncurec_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting NCU REC register to new "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_ncurec_val),
			    PRTF_64_TO_32(set_ncurec_val));
		}

		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, KT_NCU_LOG_ENB_REG +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
		    set_ncurec_val, (uint64_t)kt_debug_buf_pa, NULL);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_ncurec_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, KT_NCU_LOG_ENB_REG +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);
		if (obs_ncurec_val != set_ncurec_val) {
			cmn_err(CE_WARN, "couldn't set SSI REC reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_ncurec_val),
			    PRTF_64_TO_32(set_ncurec_val));
			return (-1);
		}
	}

	if (obs_ncusig_val != set_ncusig_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting NCU SIG register to new "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_ncusig_val),
			    PRTF_64_TO_32(set_ncusig_val));
		}

		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, KT_NCU_SIG_ENB_REG +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
		    set_ncusig_val, (uint64_t)kt_debug_buf_pa, NULL);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_ncusig_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, KT_NCU_SIG_ENB_REG +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);
		if (obs_ncusig_val != set_ncusig_val) {
			cmn_err(CE_WARN, "couldn't set SSI SIG reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_ncusig_val),
			    PRTF_64_TO_32(set_ncusig_val));
			return (-1);
		}
	}

	/*
	 * Set and verify the LFU register settings if required.
	 * Though only one LFU was read, all of them are set identically.
	 */
	for (i = 0, offset = (cpu_node_id << KT_CPU_NODE_ID_SHIFT);
	    i <= KT_LFU_NUM_UNITS; i++, offset += KT_LFU_REG_STEP) {
		if (obs_lfurec_val != set_lfurec_val) {
			if (F_VERBOSE(iocp)) {
				cmn_err(CE_NOTE, "setting LFU log register to "
				    "new value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)\n",
				    PRTF_64_TO_32(obs_lfurec_val),
				    PRTF_64_TO_32(set_lfurec_val));
			}

			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, KT_LFU_LOG_ENB_REG +
			    offset, set_lfurec_val, (uint64_t)kt_debug_buf_pa,
			    NULL);

			/*
			 * Verify that the value was set correctly.
			 */
			obs_lfurec_val = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, KT_LFU_LOG_ENB_REG +
			    offset, (uint64_t)kt_debug_buf_pa, NULL, NULL);
			if (obs_lfurec_val != set_lfurec_val) {
				cmn_err(CE_WARN, "couldn't set LFU log reg to "
				    "desired value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)!\n",
				    PRTF_64_TO_32(obs_lfurec_val),
				    PRTF_64_TO_32(set_lfurec_val));
				return (-1);
			}
		}

		if (obs_lfusig_val != set_lfusig_val) {
			if (F_VERBOSE(iocp)) {
				cmn_err(CE_NOTE, "setting LFU int register to "
				    "new value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)\n",
				    PRTF_64_TO_32(obs_lfusig_val),
				    PRTF_64_TO_32(set_lfusig_val));
			}

			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, KT_LFU_SIG_ENB_REG +
			    offset, set_lfusig_val, (uint64_t)kt_debug_buf_pa,
			    NULL);

			/*
			 * Verify that the value was set correctly.
			 */
			obs_lfusig_val = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, KT_LFU_SIG_ENB_REG +
			    offset, (uint64_t)kt_debug_buf_pa, NULL, NULL);
			if (obs_lfusig_val != set_lfusig_val) {
				cmn_err(CE_WARN, "couldn't set LFU sig reg to "
				    "desired value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)!\n",
				    PRTF_64_TO_32(obs_lfusig_val),
				    PRTF_64_TO_32(set_lfusig_val));
				return (-1);
			}
		}
	}

	if (obs_fat_val != set_fat_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting reset fatal register to new "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_fat_val),
			    PRTF_64_TO_32(set_fat_val));
		}

		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, KT_RESET_FAT_ENB_REG +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
		    set_fat_val, (uint64_t)kt_debug_buf_pa, NULL);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_fat_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, KT_RESET_FAT_ENB_REG +
		    (cpu_node_id << KT_CPU_NODE_ID_SHIFT),
		    (uint64_t)kt_debug_buf_pa, NULL, NULL);
		if (obs_fat_val != set_fat_val) {
			cmn_err(CE_WARN, "couldn't set fatal reg to desired"
			    " value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_fat_val),
			    PRTF_64_TO_32(set_fat_val));
			return (-1);
		}
	}

	return (0);
}

/*
 * This routine allows the K/T caches to be flushed from a call through
 * the opsvec table.  Flushing the L2 cache also flushes the L1 caches since
 * the K/T caches are inclusive (as were Niagara-I and II).
 *
 * NOTE: this routine is NOT doing a displacement flush, it's using the
 *	 prefetch-ICE instruction as this is the preferred method to use
 *	 for the default flush routine.
 */
/* ARGSUSED */
int
kt_flushall_l2_hvmode(cpu_info_t *cip)
{
	int	ret;

	if ((ret = memtest_hv_util("kt_l2_flushall_ice",
	    (void *)kt_l2_flushall_ice, NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "kt_flushall_L2_hvmode: trap to kt_l2_flushall_ice "
		    "FAILED, ret=0x%x\n", ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine allows the KT/RF caches to be flushed while remaining in
 * kernel mode (for the actual displacements).  This is used for write-back
 * tests that require the flush/displacement to occur in kernel mode.
 *
 * Flushing the L2 cache also flushes the L1 caches since the Niagara family
 * caches are inclusive.
 *
 * Note that the flush address used is the displacement region used by
 * the kernel.  It is a real address (treated as a physical address by
 * sun4u) and so must be accessed via ASI 0x14 (ASI_MEM).
 *
 * WARNING: this routine can cause panics because KT/RF does not
 *	    support locking TTEs in the TLB.  Cache flushing should be
 *	    performed with one of the hyperpriv routines, OR the injector
 *	    option "-Q pause" can be used to avoid the panics.
 *
 * NOTE: mapping the flush region using the hcall hv_mmu_map_perm_addr()
 *	 does not work because it can't do REAL address mappings and there
 *	 is no virtual mapping for the ecache_flushaddr.
 *
 * NOTE: this routine and the asm routine that this routine calls are
 *	 not N2 or KT specific.  Maybe make generic and move to
 *	 memtest_v.c.
 */
/* ARGSUSED */
int
kt_flushall_l2_kmode(cpu_info_t *cip)
{
	caddr_t		kern_disp_addr = (caddr_t)ecache_flushaddr;
	uint64_t	hv_disp_addr;
	int		ret;
	char		*fname = "kt_flushall_l2_kmode";

	DPRINTF(3, "%s: doing L2 flush (DM mode) with "
	    "displacement flush raddr=0x%llx, cachesize=0x%x, "
	    "sublinesize=0x%x\n", fname, kern_disp_addr,
	    cip->c_l2_flushsize, cip->c_l2_sublinesize);

	/*
	 * Determine the physical address base (range) which the hypervisor
	 * is using for it's displacement flush area.
	 */
	if ((hv_disp_addr = memtest_hv_util("kt_l2_get_flushbase",
	    (void *)kt_l2_get_flushbase, NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to kt_l2_get_flushbase "
		    "FAILED, ret=0x%x\n", fname, hv_disp_addr);
		return (hv_disp_addr);
	}

	if ((ret = memtest_hv_util("kt_l2_enable_DM", (void *)kt_l2_enable_DM,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to kt_l2_enable_DM "
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
	kt_l2_flushall_kmode_asm(kern_disp_addr, cip->c_l2_flushsize,
	    cip->c_l2_sublinesize);

	if ((ret = memtest_hv_util("kt_l2_disable_DM", (void *)kt_l2_disable_DM,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to kt_l2_disable_DM "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine allows a single K/T L2-cache entry to be flushed from
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
kt_flush_l2_entry_hvmode(cpu_info_t *cip, caddr_t paddr)
{
	uint64_t	idx_paddr;
	int		ret;
	char		*fname = "kt_flush_l2_entry_hvmode";

	DPRINTF(3, "%s: doing L2 entry displacement flush, "
	    "HV mode\n", fname);

	/*
	 * Determine if L2 cache index hashing (IDX) is enabled and
	 * call the appropriate routine.
	 */
	if (kt_check_l2_idx_mode((uint64_t)paddr, &idx_paddr) == 0) {
		if ((ret = memtest_hv_util("kt_l2_flushentry",
		    (void *)kt_l2_flushentry, (uint64_t)paddr,
		    NULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: trap to kt_l2_flushentry "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		if ((ret = memtest_hv_util("kt_l2_flushidx",
		    (void *)kt_l2_flushidx, idx_paddr,
		    NULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: trap to kt_l2_flushidx "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	return (0);
}

/*
 * This routine allows a single K/T L2$ cache entry to be flushed
 * using the prefectch-ICE (Invalidate L2 Cache Entry) instruction.
 * The index to send to the instruction is affected by L2$ index hashing
 * which is determined here.  The flush is performed in HV mode.
 *
 * NOTE: the trap to determine if index hashing (IDX) is enabled uses
 *	 instructions that will affect the caches.  Perhaps the IDX state
 *	 can be determined as part of the pre-test setup so this does not
 *	 need to be done here.
 */
/* ARGSUSED */
int
kt_flush_l2_entry_ice(cpu_info_t *cip, caddr_t paddr)
{
	uint64_t	idx_paddr;
	int		ret;
	char		*fname = "kt_flush_l2_entry_ice";

	DPRINTF(3, "%s: doing L2 entry cache invalidate flush, "
	    "HV mode\n", fname);

	/*
	 * Determine if L2 cache index hashing (IDX) is enabled, note
	 * that both routines use the non hashed address regardless.
	 */
	if (kt_check_l2_idx_mode((uint64_t)paddr, &idx_paddr) == 0) {
		if ((ret = memtest_hv_util("kt_l2_flushentry_ice",
		    (void *)kt_l2_flushentry_ice, (uint64_t)paddr,
		    NULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: trap to kt_l2_flushentry_ice "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
#ifdef	L2_DEBUG_BUFFER
		kt_debug_init();
#endif
		if ((ret = memtest_hv_util("kt_l2_flushidx_ice",
		    (void *)kt_l2_flushidx_ice, (uint64_t)paddr,
		    NULL, NULL, (uint64_t)kt_debug_buf_pa)) == -1) {
			DPRINTF(0, "%s: trap to kt_l2_flushidx_ice "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
#ifdef	L2_DEBUG_BUFFER
		kt_debug_dump();
#endif
	}

	return (0);
}

/*
 * This routine allows a single K/T L2$ cache entry to be flushed from
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
kt_flush_l2_entry_kmode(cpu_info_t *cip, caddr_t orig_raddr)
{
	caddr_t		kern_disp_addr = (caddr_t)ecache_flushaddr;
	uint64_t	hv_disp_addr;
	uint64_t	raddr = (uint64_t)orig_raddr;
	uint64_t	idx_raddr;
	int		ret;
	char		*fname = "kt_flush_l2_entry_kmode";

	DPRINTF(3, "%s: doing L2 entry displacement flush, kernel mode "
	    "with flush raddr=0x%llx\n", fname, kern_disp_addr);

	/*
	 * Determine the physical address base (range) which the hypervisor
	 * is using for its displacement flush area.
	 */
	if ((hv_disp_addr = memtest_hv_util("kt_l2_get_flushbase",
	    (void *)kt_l2_get_flushbase, NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to kt_l2_get_flushbase "
		    "FAILED, ret=0x%x\n", fname, hv_disp_addr);
		return (hv_disp_addr);
	}

	if ((ret = memtest_hv_util("kt_l2_enable_DM", (void *)kt_l2_enable_DM,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to kt_l2_enable_DM "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * Unfortunately in order for the kernel to use a real address
	 * directly the MMU has to be involved (even for ASI 0x14).  So
	 * a dTLB RA->PA entry is installed for the displacement flush area.
	 *
	 * NOTE: the N2 routine is used and should also work for KT.
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
	if (kt_check_l2_idx_mode((uint64_t)raddr, &idx_raddr) == 0) {
		raddr = (raddr & 0x3fffc0) + (uint64_t)kern_disp_addr;
		(void) ldphys(raddr);
	} else {
		idx_raddr = (idx_raddr & 0x3fffc0) + (uint64_t)kern_disp_addr;
		(void) kt_check_l2_idx_mode(idx_raddr, &idx_raddr);
		(void) ldphys(idx_raddr);
	}

	if ((ret = memtest_hv_util("kt_l2_disable_DM", (void *)kt_l2_disable_DM,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to kt_l2_disable_DM "
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
kt_get_cpu_info(cpu_info_t *cip)
{
	uint64_t	cpu_node_id = KT_SYS_MODE_GET_NODEID(cip->c_sys_mode);

	cip->c_dc_size = 8 * KB;
	cip->c_dc_linesize = 16;
	cip->c_dc_assoc = 4;

	cip->c_ic_size = 16 * KB;
	cip->c_ic_linesize = 32;
	cip->c_ic_assoc = 8;

	cip->c_l2_size = 6 * MB;
	cip->c_l2_sublinesize = 64;
	cip->c_l2_linesize = cip->c_l2_sublinesize *
	    (cip->c_l2_size / (1 * MB));
	cip->c_l2_assoc = 24;
	cip->c_l2_flushsize = cip->c_l2_size; /* thanks to on the fly DM mode */
	cip->c_mem_flags = MEMFLAGS_LOCAL;

	/*
	 * Similar to Victoria Falls memory is interleaved between nodes
	 * so the c_mem_start and c_mem_size stuct members cannot be used.
	 *
	 * The sys mode and L2 control registers are used to determine
	 * interleave configuration as well as which node a given CPU
	 * belongs to.  This information is then used to select appropriate
	 * CPUs and memory locations for foreign/remote tests.
	 *
	 * The sys mode register has information on how many nodes there
	 * are and what node this CPU belongs to.
	 *
	 * The COU_CEILING register has interleave configuration information.
	 * KT/RF only allows two interleave modes: 1KB and 8GB.  Also the fine
	 * grained mode (1K) is only available on systems containing 2, 4, or
	 * 8 KT/RF processors.
	 *
	 * Note: PA[43] determines mem/io on KT due to it's 44-bit PA space.
	 */
	cip->c_sys_mode = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_SYS_MODE_REG, NULL, NULL, NULL);

	/*
	 * Actually the COU ceiling register value is placed into the
	 * no longer very aptly named c_l2_ctl field.  Don't be confused!
	 *
	 * NOTE: there are four of these regs (step 0x1000) but this
	 * code is only checking the lowest one.
	 */
	cip->c_l2_ctl = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, KT_COU_CEILING_REG +
	    (cpu_node_id << KT_CPU_NODE_ID_SHIFT), NULL, NULL, NULL);

	return (0);
}

void
kt_init(mdata_t *mdatap)
{
	mdatap->m_sopvp = &kt_vops;
	mdatap->m_copvp = &kt_cops;
	mdatap->m_cmdpp = commands;

	/*
	 * Determine the paddr of the kt_debug_buf to pass into the asm
	 * injection routines which run in hyperpriv mode.  Note that the
	 * first translation produces the raddr.
	 */
	kt_debug_buf_pa = memtest_kva_to_ra((void *)kt_debug_buf_va);
	kt_debug_buf_pa = memtest_ra_to_pa((uint64_t)kt_debug_buf_pa);
}

/*
 * The consumer/producer routines below work together to produce the
 * following errors:
 *	- CE in L2 copyback data
 *	- CE on foreign memory read data
 *	- UE in external writeback data
 *
 * They are also used to verify the following:
 * 	- UE in L2 copyback data is propagated to the requesting node
 *	  where the L2 sees and reports the error.
 *	- UE on foreign memory read is propagated to the requesting
 *	  node where the L2 sees and reports an error.
 *
 * Note that a CE in external writeback data is not possible because
 * the coherency link only propagates UEs.
 *
 * The function kt_producer() is where the producer thread starts and
 * then continues on to inject the appropriate errors (as well as
 * trigger the L2 writeback errors).
 *
 * The function kt_pc_err() creates the separate producer thread and
 * then continues on to start the consumer thread which accesses the
 * corrupted data generated by the producer thread.
 *
 * NOTE: For these tests to work properly each thread must be bound to
 *	 a CPU on a separate physical KT/RF chip.
 */

/*
 * This routine is the main producer/consumer test routine.
 * It starts a separate thread for the producer and then
 * continues as the consumer.
 *
 * NOTE: there should be a way to make these multi-thread handling
 *	 routines common.  Perhaps do it as part of the new EI framework.
 */
static int
kt_pc_err(mdata_t *mdatap)
{
	memtest_t	*mp = mdatap->m_memtestp;
	mdata_t		*producer_mdatap, *consumer_mdatap;
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	sync = 0;
	char		*fname = "kt_pc_err";

	/*
	 * Sanity check.
	 */
	if (iocp->ioc_nthreads < 2) {
		DPRINTF(0, "%s: nthreads=%d should 2 or more\n", fname,
		    iocp->ioc_nthreads);
		return (EIO);
	}

	consumer_mdatap = mp->m_mdatap[0];
	producer_mdatap = mp->m_mdatap[1];
	consumer_mdatap->m_syncp = &sync;
	producer_mdatap->m_syncp = &sync;

	DPRINTF(2, "%s: consumer_mdatap=0x%p, producer_mdatap=0x%p, "
	    "consumer_cpu=%d, producer_cpu=%d, &sync=0x%p\n", fname,
	    consumer_mdatap, producer_mdatap,
	    consumer_mdatap->m_cip->c_cpuid,
	    producer_mdatap->m_cip->c_cpuid, &sync);

	/*
	 * Start the producer.
	 */
	DPRINTF(2, "%s: starting producer\n", fname);
	if (memtest_start_thread(producer_mdatap, kt_producer, fname) != 0) {
		cmn_err(CE_WARN, "memtest_start_thread: failed\n");
		return (EIO);
	}

	/*
	 * Start the consumer (using current thread).
	 */
	DPRINTF(2, "%s: starting consumer\n", fname);
	if (kt_consumer(consumer_mdatap) != 0) {
		cmn_err(CE_WARN, "%s: kt_consumer() failed\n", fname);
		return (EIO);
	}

	return (0);
}

/*
 * This routine performs the L2 cache index hashing (IDX) hash on
 * the provided address without checking if hashing is enabled.  The
 * hashing algorithm used by KT/RF is identical to Niagara-II/VF though
 * the registers involved are different.
 *
 * Unlike Niagara-II/VF the IDX mode can be enabled or disabled on the fly,
 * but the EI will not be doing this because it's a heavyweight operation
 * that requires that the caches are entirely cleared beforehand.
 *
 * The index hashing performs the following operations on the address:
 *	bits[17:13]	= PA[32:28] xor PA[17:13]
 *	bits[12:11]	= PA[19:18] xor PA[12:11]
 *	bits[10:9]	= PA[10:9]
 *
 * NOTE: this routine is useful because it does NOT require a call down
 *	 to HV to check the index hashing enable status and if calls no
 *	 other routines (is leaf routine).
 */
uint64_t
kt_perform_l2_idx_hash(uint64_t addr)
{
	uint64_t	idx_addr;

	/*
	 * Transform the addr inpar to it's hashed version.
	 */
	idx_addr = ((addr & KT_L2_IDX_TOPBITS) >> 15) ^ addr;
	idx_addr = ((addr & KT_L2_IDX_BOTBITS) >> 7) ^ idx_addr;

	return (idx_addr);
}

/*
 * This is the common producer thread routine for the
 * producer/consumer tests.
 */
static void
kt_producer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	int		ret = 0;
	char		*fname = "kt_producer";

	/*
	 * Disable preemption here since multi-threaded tests
	 * are responsible for their own control of preemption.
	 */
	kpreempt_disable();

	/*
	 * Wait for OK to inject the error, but don't wait forever.
	 */
	if (memtest_wait_sync(syncp, 1, SYNC_WAIT_MAX, fname) !=
	    SYNC_STATUS_OK) {
		*syncp = -1;
		kpreempt_enable();
		thread_exit();
	}

	(void) memtest_set_scrubbers(mdatap);

	/*
	 * Inject the error.
	 */
	switch (ERR_CLASS(iocp->ioc_command)) {
	case ERR_CLASS_MEM:
		ret = kt_mem_producer(mdatap);
		break;
	case ERR_CLASS_L2:
	case ERR_CLASS_L2WB:
		ret = kt_l2_producer(mdatap);
		break;
	default:
		DPRINTF(0, "%s: unsupported command=0x%llx\n", fname,
		    IOC_COMMAND(iocp));
		ret = EIO;
	}

	/*
	 * If there were any errors set the
	 * sync variable to the error value.
	 */
	if (ret)
		DPRINTF(0, "%s: internal error ret=0x%lx, setting "
		    "sync variable to -1 to inform consumer\n", fname, ret);
		*syncp = -1;

	(void) memtest_restore_scrubbers(mdatap);

	kpreempt_enable();
	thread_exit();
}

/*
 * This is the common consumer thread routine for the
 * producer/consumer tests.
 */
static int
kt_consumer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	int		ret;
	char		*fname = "kt_consumer";

	DPRINTF(2, "%s: mdatap=0x%p, syncp=0x%p\n", fname, mdatap, syncp);

	kpreempt_disable();

	/*
	 * Release the producer thread and have it inject the error(s).
	 */
	*syncp = 1;

	DPRINTF(3, "%s: waiting for producer to inject error\n", fname);

	/*
	 * Wait for the producer thread to inject the error(s),
	 * but don't wait forever.
	 */
	if (memtest_wait_sync(syncp, 2, SYNC_WAIT_MAX, fname) !=
	    SYNC_STATUS_OK) {
		*syncp = -1;
		return (EIO);
	}

	switch (ERR_CLASS(iocp->ioc_command)) {
	case ERR_CLASS_MEM:
		ret = kt_mem_consumer(mdatap);
		break;
	case ERR_CLASS_L2:
		ret = kt_l2_consumer(mdatap);
		break;
	case ERR_CLASS_L2WB:
		/*
		 * For write-back to foreign memory errors the producer
		 * is the one to invoke the error, so do nothing here.
		 */
		*syncp = 3;

		/*
		 * Give the producer thread a chance to
		 * notice the update to the sync flag and exit.
		 */
		delay(1 * hz);
		ret = 0;
		break;
	default:
		DPRINTF(0, "%s: unsupported command=0x%llx\n", fname,
		    IOC_COMMAND(iocp));
		ret = EIO;
	}

	/*
	 * Make sure we give the producer thread a chance to
	 * notice the update to the sync flag and exit.
	 */
	delay(1 * hz);
	kpreempt_enable();
	return (ret);
}

/*
 * This is the producer thread for the foreign/remote L2 tests.
 * This routine directs the injection of L2 errors that will, with the
 * exception of the writeback error, be invoked by the consumer thread.
 */
static int
kt_l2_producer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	int		ret = 0;
	char		*fname = "kt_l2_producer";

	DPRINTF(2, "%s: injecting the error\n", fname);

	/*
	 * Inject the error by calling chain of memtest routines.
	 */
	if (ERR_PROT_ISND(iocp->ioc_command)) {
		if (ret = OP_INJECT_L2ND(mdatap)) {
			DPRINTF(0, "%s: processor specific l2cache NotData "
			    "injection routine FAILED!\n", fname);
		}
	} else {
		ret = memtest_inject_l2cache(mdatap);
	}

	if (ret) {
		return (ret);
	}

	/*
	 * For write-back to foreign memory tests, the producer thread
	 * is the one to invoke the error.  Note: using VF access routine.
	 */
	if (ERR_CLASS_ISL2WB(iocp->ioc_command)) {
		(void) vf_l2wb_invoke(mdatap);
	}

	/*
	 * Tell the consumer thread that we've injected the error.
	 */
	*syncp = 2;

	DPRINTF(2, "%s: waiting for consumer to invoke the error\n", fname);

	/*
	 * Wait for consumer to invoke the error but don't wait forever.
	 */
	if (memtest_wait_sync(syncp, 3, SYNC_WAIT_MAX, fname) !=
	    SYNC_STATUS_OK) {
		ret = EIO;
	}

	DPRINTF(3, "%s: ret=%d, sync=%d\n", fname, ret, *syncp);
	return (ret);
}

/*
 * This is the consumer thread for foreign/remote L2 tests.  From here
 * the appropriate routine is called to invoked error(s) that have been
 * injected by the producer thread.
 */
static int
kt_l2_consumer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		ret = 0;
	volatile int	*syncp = mdatap->m_syncp;
	char		*fname = "kt_l2_consumer";

	/*
	 * Flush the local L2-cache so that the access to invoke the
	 * error does not hit a copy in the local cache.
	 */
	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Invoke the error and store a value to the sync variable
	 * indicating that we've invoked the error.
	 */
	if (!F_NOERR(iocp)) {
		DPRINTF(3, "%s: invoking error\n", fname);
			switch (ERR_MODE(iocp->ioc_command)) {
			case ERR_MODE_HYPR:
				ret = vf_l2_h_invoke(mdatap);
				break;
			case ERR_MODE_KERN:
				ret = vf_l2_k_invoke(mdatap);
				break;
			default:
				DPRINTF(0, "%s: invalid ERR_MODE=0x%x\n", fname,
				    ERR_MODE(iocp->ioc_command));
				ret = EINVAL;
			}
	}

	/*
	 * Notify the producer thread to exit.
	 */
	*syncp = 3;

	/*
	 * Make sure we give the producer thread a chance to
	 * notice the update to the sync flag and exit.
	 */
	delay(1 * hz);
	return (ret);
}

/*
 * This is the producer thread for the foreign/remote DRAM tests.
 * This routine directs the injection of DRAM errors that will
 * later be invoked by the consumer thread.
 */
static int
kt_mem_producer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	uint64_t	pa_2corrupt = mdatap->m_paddr_c;
	uint64_t	ra_2corrupt = mdatap->m_raddr_c;
	uint64_t	paddr;
	uint64_t	paddr_end;
	uint64_t	raddr;
	int		stride = 0x40;
	int		ret = 0;
	int		count, i;
	caddr_t		kva_2corrupt = mdatap->m_kvaddr_c;
	caddr_t		vaddr;
	char		*fname = "kt_mem_producer";

	/*
	 * Check if this is a "storm" command and set the
	 * error count accordingly.
	 */
	if (ERR_PROT_ISCE(iocp->ioc_command) &&
	    ERR_MISC_ISSTORM(iocp->ioc_command)) {
		if (F_MISC1(iocp))
			count = iocp->ioc_misc1;
		else
			count = 64;

		if (count > (iocp->ioc_bufsize / stride))
			count = iocp->ioc_bufsize / stride;

		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "%s: injecting up to %d "
			    "CE errors: paddr=0x%08x.%08x, stride=0x%x\n",
			    fname, count, PRTF_64_TO_32(pa_2corrupt), stride);
		}
	} else {
		count = 1;
	}

	/*
	 * Inject the error(s).
	 */
	DPRINTF(2, "%s: injecting the error(s)\n", fname);

	paddr_end = P2ALIGN(pa_2corrupt, PAGESIZE) + PAGESIZE;

	for (i = 0, vaddr = kva_2corrupt, raddr = ra_2corrupt,
	    paddr = pa_2corrupt; (i < count && paddr < paddr_end);
	    vaddr += stride, raddr += stride, paddr += stride) {

		if (ERR_MISC_ISSTORM(iocp->ioc_command) &&
		    !kt_check_is_local_mem(mdatap, paddr)) {
			continue;
		}

		i++;

		mdatap->m_kvaddr_c = vaddr;
		mdatap->m_raddr_c = raddr;
		mdatap->m_paddr_c = paddr;
		DPRINTF(2, "%s: injecting error %d at vaddr=0x%p, "
		    "paddr=0x%llx\n", fname, i, vaddr, paddr);

		if (ERR_PROT_ISND(iocp->ioc_command)) {
			if ((ret = OP_INJECT_MEMND(mdatap)) != 0)
				return (ret);
		} else {
			if ((ret = memtest_inject_memory(mdatap)) != 0)
				return (ret);
		}
	}

	/*
	 * Tell the consumer that we've injected the error(s).
	 */
	*syncp = 2;

	DPRINTF(3, "%s: waiting for consumer to invoke the error\n", fname);

	/*
	 * Wait for consumer to invoke the error but don't wait forever.
	 */
	if (memtest_wait_sync(syncp, 3, SYNC_WAIT_MAX, fname) !=
	    SYNC_STATUS_OK)
		ret = EIO;

	DPRINTF(3, "%s: ret=%d, sync=%d\n", fname, ret, *syncp);

	return (ret);
}

/*
 * This is the consumer thread for remote DRAM tests.  From here the
 * appropriate routine is called to invoked error(s) that have been
 * injected by the producer thread.
 */
static int
kt_mem_consumer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	uint64_t	pa_2access = mdatap->m_paddr_a;
	uint64_t	paddr;
	uint64_t	paddr_end;
	int		stride = 0x40;
	int		count, i;
	uint_t		myid = getprocessorid();
	int		err_acc = ERR_ACC(iocp->ioc_command);
	caddr_t		kva_2access = mdatap->m_kvaddr_a;
	caddr_t		vaddr;
	int		ret = 0;
	char		*fname = "kt_mem_consumer";

	/*
	 * Check if this is a "storm" command and set the
	 * error count accordingly.
	 */
	if (ERR_PROT_ISCE(iocp->ioc_command) &&
	    ERR_MISC_ISSTORM(iocp->ioc_command)) {
		if (F_MISC1(iocp))
			count = iocp->ioc_misc1;
		else
			count = 64;
		if (count > (iocp->ioc_bufsize / stride))
			count = iocp->ioc_bufsize / stride;

		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "%s: accessing up to %d "
			    "CE errors: paddr=0x%08x.%08x, stride=0x%x\n",
			    fname, count, PRTF_64_TO_32(pa_2access), stride);
		}
	} else {
		count = 1;
	}

	paddr_end = P2ALIGN(pa_2access, PAGESIZE) + PAGESIZE;

	/*
	 * If STORE was specified change the MA access type.
	 */
	if (F_STORE(iocp) && (ERR_ACC(iocp->ioc_command) == ERR_ACC_MAL)) {
		DPRINTF(3, "%s: setting MA access type to STORE\n", fname);
		err_acc = ERR_ACC_MAS;
	}

	for (i = 0, vaddr = kva_2access, paddr = pa_2access;
	    (i < count && paddr < paddr_end);
	    vaddr += stride, paddr += stride) {

		if (ERR_MISC_ISSTORM(iocp->ioc_command) &&
		    kt_check_is_local_mem(mdatap, paddr)) {
			continue;
		}

		i++;

		/*
		 * If we do not want to invoke the error(s) then continue.
		 */
		if (F_NOERR(iocp)) {
			DPRINTF(2, "%s: not invoking error %d at vaddr=0x%p, "
			    "paddr=0x%llx\n", fname, i, vaddr, paddr);
			continue;
		}

		DPRINTF(2, "%s: invoking error %d at "
		    "vaddr=0x%p, paddr=0x%llx\n", fname, i, vaddr, paddr);

		switch (ERR_MODE(iocp->ioc_command)) {
		case ERR_MODE_HYPR:
			switch (err_acc) {
			case ERR_ACC_LOAD:
				(void) memtest_hv_util("hv_paddr_load64",
				    (void *)hv_paddr_load64, mdatap->m_paddr_a,
				    NULL, NULL, NULL);
				break;
			case ERR_ACC_STORE:
				(void) memtest_hv_util("hv_paddr_store16",
				    (void *)hv_paddr_store16, mdatap->m_paddr_a,
				    0xff, NULL, NULL);
				break;
			case ERR_ACC_MAL:	/* using polled mode */
				ret = OP_ACCESS_MA(mdatap, MA_OP_LOAD,
				    (uint_t)0);
				break;
			case ERR_ACC_MAS:	/* using polled mode */
				ret = OP_ACCESS_MA(mdatap, MA_OP_STORE,
				    (uint_t)0);
				break;
			case ERR_ACC_CWQ:	/* using polled mode */
				ret = OP_ACCESS_CWQ(mdatap, CWQ_OP_COPY,
				    (uint_t)0);
				break;
			default:
				DPRINTF(0, "%s: unsupported access type %d\n",
				    fname, err_acc);
				ret = ENOTSUP;
			}

			break;
		case ERR_MODE_KERN:

			switch (err_acc) {
			case ERR_ACC_LOAD:
			case ERR_ACC_FETCH:
				if (ERR_MISC_ISTL1(iocp->ioc_command))
					xt_one(myid,
					    (xcfunc_t *)mdatap->m_asmld_tl1,
					    (uint64_t)vaddr, (uint64_t)0);
				else
					(mdatap->m_asmld)(vaddr);
				break;
			case ERR_ACC_BLOAD:
				if (ERR_MISC_ISTL1(iocp->ioc_command))
					xt_one(myid,
					    (xcfunc_t *)mdatap->m_blkld_tl1,
					    (uint64_t)vaddr, (uint64_t)0);
				else
					mdatap->m_blkld(vaddr);
				break;
			case ERR_ACC_STORE:
				if (ERR_MISC_ISTL1(iocp->ioc_command))
					xt_one(myid,
					    (xcfunc_t *)mdatap->m_asmst_tl1,
					    (uint64_t)vaddr, (uint64_t)0xff);
				else {
					DPRINTF(0, "%s: storing to invoke "
					    "error\n", fname);
					*vaddr = (uchar_t)0xff;
				}
				membar_sync();
				break;
			default:
				DPRINTF(0, "%s: unsupported access type %d\n",
				    fname, err_acc);
				return (ENOTSUP);
			}

			break;
		default:
			DPRINTF(0, "%s: invalid ERR_MODE=0x%x\n", fname,
			    ERR_MODE(iocp->ioc_command));
			ret = EINVAL;
		}
	}

	/*
	 * Notify the producer thread to exit.
	 */
	*syncp = 3;

	return (ret);
}

/*
 * Because KT/RF has the option to use a fine-grained interleave mode
 * not all the instruction command routines can be copied into a
 * memory buffer that will not cross the interleave boundary.
 *
 * Fine grained interleave is 1024B (note that VF was 512B), coarse-grain
 * is 8GB (VF was 1GB).  Since both can be used simultaneously, this routine
 * will not even check the mode and will assume that fine grained mode may
 * be enabled.
 *
 * A subset of the complete asm routines are copied into the buffer.
 * Since each routine has a max size of 256B, only four can be copied.
 */
int
kt_pre_test_copy_asm(mdata_t *mdatap)
{
	memtest_t	*mp = mdatap->m_memtestp;
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip;
	caddr_t		tmpvaddr;
	uint_t		cpu_mode;
	int		len;
	char		*fname = "kt_pre_test_copy_asm";

	/*
	 * For multi-threaded tests (except for L2 writeback tests),
	 * copy data to memory that is local to the node the producer
	 * thread will run on.  m_mdatap[1] represents this node.
	 *
	 * For remote L2 writeback tests, the memory used is remote to
	 * the producer and instead local to the consumer.
	 * m_mdatap[0] represents the node that the consumer thread
	 * will run on.  It is also where single-threaded tests will
	 * run.
	 *
	 * NOTE: the commands are defined with "VFR" set to designate a
	 *	 foreign/remote test.  Should make this cpu-agnostic.
	 */
	if (ERR_VF_ISFR(iocp->ioc_command) &&
	    !ERR_CLASS_ISL2WB(iocp->ioc_command)) {
		cip = mp->m_mdatap[1]->m_cip;
	} else {
		cip = mp->m_mdatap[0]->m_cip;
	}

	DPRINTF(2, "%s: copy data local to CPU %d\n", fname, cip->c_cpuid);

	/*
	 * The common memtest routines set the size limit for each
	 * asm routine as 256 bytes.  This is respected here.
	 */
	len = 256;

	/*
	 * Check the "mode" setting as well since for 8-node
	 * systems the entire 8K (default EI buffer size)
	 * must be used for the copy (1K for each node).
	 *
	 * For 8-node this routine copies the asm routines to whatever
	 * 1K slice within the 8K buffer matches the current node.
	 * This will blow away the data half of the buffer but that
	 * area is unsed for instruction access commands.
	 *
	 * Note that for degraded modes KT/RF still partitions the
	 * memory in the same way (leaving memory holes).  So this
	 * method will still work in those cases.
	 */
	cpu_mode = KT_SYS_MODE_GET_MODE(cip->c_sys_mode);

	if (cpu_mode == KT_SYS_MODE_8MODE) {
		tmpvaddr = mdatap->m_instbuf = mdatap->m_databuf;
	} else {
		tmpvaddr = mdatap->m_instbuf;
	}

	/*
	 * The current size of m_instbuf is 4096 bytes (the upper
	 * half of an 8k page).  On a 4-way KT/RF system
	 * this means that for each node there is only one 1024-byte
	 * area of memory in the buffer that is local to that
	 * node.  In this situation there is not enough memory to
	 * copy all of the asm routines so only a subset are copied
	 * based on what a particular command will use.
	 *
	 * For TL1 tests, all three of the TL1 versions of the
	 * routines are copied as well as the PC relative routine:
	 *		memtest_asmld_tl1
	 *		memtest_asmst_tl1
	 *		memtest_blkld_tl1
	 *		memtest_pcrel
	 *
	 * For everything else, the three regular non-TL1 versions
	 * of the routines are copied as well as the PC relative routine:
	 *		memtest_asmld
	 *		memtest_asmldst
	 *		memtest_blkld
	 *		memtest_pcrel
	 */

	/*
	 * Clear values from the previous test as an aid in debugging.
	 */
	mdatap->m_asmld = 0;
	mdatap->m_asmldst = 0;
	mdatap->m_blkld = 0;
	mdatap->m_asmld_tl1 = 0;
	mdatap->m_asmst_tl1 = 0;
	mdatap->m_blkld_tl1 = 0;
	mdatap->m_pcrel = 0;

	if (ERR_MISC_ISTL1(iocp->ioc_command)) {
		bcopy((caddr_t)memtest_asmld_tl1, tmpvaddr, len);
		mdatap->m_asmld_tl1 = (asmld_tl1_t *)(tmpvaddr);

		tmpvaddr += len;
		bcopy((caddr_t)memtest_asmst_tl1, tmpvaddr, len);
		mdatap->m_asmst_tl1 = (asmst_tl1_t *)(tmpvaddr);

		tmpvaddr += len;
		bcopy((caddr_t)memtest_blkld_tl1, tmpvaddr, len);
		mdatap->m_blkld_tl1 = (blkld_tl1_t *)(tmpvaddr);

		tmpvaddr += len;
		bcopy((caddr_t)memtest_pcrel, tmpvaddr, len);
		mdatap->m_pcrel = (pcrel_t *)(tmpvaddr);

		return (0);
	}

	/*
	 * Otherwise copy the "normal" set of routines.
	 */
	bcopy((caddr_t)memtest_asmld, tmpvaddr, len);
	mdatap->m_asmld = (asmld_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_asmldst, tmpvaddr, len);
	mdatap->m_asmldst = (asmldst_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_blkld, tmpvaddr, len);
	mdatap->m_blkld = (blkld_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_pcrel, tmpvaddr, len);
	mdatap->m_pcrel = (pcrel_t *)(tmpvaddr);

	return (0);
}

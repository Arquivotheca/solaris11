/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains Victoria Falls (UltraSPARC-T2+) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_v.h>
#include <sys/memtestio_ni.h>
#include <sys/memtestio_n2.h>
#include <sys/memtestio_vf.h>
#include <sys/memtest_v.h>
#include <sys/memtest_ni.h>
#include <sys/memtest_n2.h>
#include <sys/memtest_vf.h>
#include <sys/memtest_kt.h>
#include <sys/memtest_v_asm.h>
#include <sys/memtest_n2_asm.h>
#include <sys/memtest_vf_asm.h>

/*
 * Static routines located in this file.
 */
static void	vf_copy_asm_1GB(mdata_t *);
static void	vf_copy_asm_512B(mdata_t *, cpu_info_t *);
static int	vf_get_cpu_info(cpu_info_t *);
static int	vf_get_mem_node_id(cpu_info_t *, uint64_t);
static int	vf_get_num_ways(cpu_info_t *);
static int	vf_k_pc_err(mdata_t *);
static void	vf_producer(mdata_t *);
static int	vf_consumer(mdata_t *);
static int	vf_l2_consumer(mdata_t *);
static int	vf_l2_producer(mdata_t *);
static int	vf_mem_consumer(mdata_t *);
static int	vf_mem_producer(mdata_t *);
static int	vf_mem_h_invoke(mdata_t *);
static int	vf_mem_k_invoke(mdata_t *);

/*
 * Victoria Falls operations vector tables.
 */
static opsvec_v_t vfalls_vops = {
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

static opsvec_c_t vfalls_cops = {
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
	vf_enable_errors,	/* enable AFT errors */
	n2_control_scrub,	/* enable/disable L2 or memory scrubbers */
	vf_get_cpu_info,	/* put cpu info into struct */
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
 * These Victoria Falls error commands are grouped according to the
 * definitions in the memtestio_ni.h, memtestio_n2.h, and
 * memtestio_vf.h header files.
 *
 * This is a complete list of the commands available on Victoria Falls,
 * so no reference needs to be made to the matching Niagara-I and Niagara-II
 * command tables.
 */
cmd_t vfalls_cmds[] = {
	/* Memory (DRAM) uncorrectable errors. */
	NI_HD_DAU,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_HD_DAUMA,	memtest_h_mem_err,	"memtest_h_mem_err",
	N2_HD_DAUCWQ,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_HI_DAU,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_KD_DAU,	memtest_k_mem_err,	"memtest_k_mem_err",
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
	NI_HD_DACMA,	memtest_h_mem_err,	"memtest_h_mem_err",
	N2_HD_DACCWQ,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_HI_DAC,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_KD_DAC,	memtest_k_mem_err,	"memtest_k_mem_err",
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
	NI_LDAUCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	NI_HD_LDAUMA,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HD_LDAUCWQ,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_HI_LDAU,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_KD_LDAU,	memtest_k_l2_err,	"memtest_k_l2_err",
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
	NI_LDACCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	NI_HD_LDACMA,	memtest_h_l2_err,	"memtest_h_l2_err",
	N2_HD_LDACCWQ,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_HI_LDAC,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_KD_LDAC,	memtest_k_l2_err,	"memtest_k_l2_err",
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

	/* Remote memory (DRAM) uncorrectable errors */
	VF_HD_FDAU,		vf_k_pc_err,		"vf_k_pc_err",
	VF_HD_FDAUMA,		vf_k_pc_err,		"vf_k_pc_err",
	VF_HD_FDAUCWQ,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_FDAU,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_FDAUTL1,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_FDAUPR,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KI_FDAU,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KI_FDAUTL1,		vf_k_pc_err,		"vf_k_pc_err",
	VF_UD_FDAU,		memtest_u_mem_err,	"memtest_u_mem_err",
	VF_UI_FDAU,		memtest_u_mem_err,	"memtest_u_mem_err",
	VF_IO_FDRU,		memtest_u_mem_err,	"memtest_u_mem_err",

	/* Remote memory (DRAM) correctable errors */
	VF_HD_FDAC,		vf_k_pc_err,		"vf_k_pc_err",
	VF_HD_FDACMA,		vf_k_pc_err,		"vf_k_pc_err",
	VF_HD_FDACCWQ,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_FDAC,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_FDACTL1,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_FDACPR,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_FDACSTORM,	vf_k_pc_err,		"vf_k_pc_err",
	VF_KI_FDAC,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KI_FDACTL1,		vf_k_pc_err,		"vf_k_pc_err",
	VF_UD_FDAC,		memtest_u_mem_err,	"memtest_u_mem_err",
	VF_UI_FDAC,		memtest_u_mem_err,	"memtest_u_mem_err",
	VF_IO_FDRC,		memtest_u_mem_err,	"memtest_u_mem_err",

	/* Remote L2 cache data uncorrectable errors. */
	VF_HD_L2CBU,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_L2CBU,		vf_k_pc_err,		"vf_k_pc_err",
	VF_HD_L2CBUMA,		vf_k_pc_err,		"vf_k_pc_err",
	VF_HD_L2CBUCWQ,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_L2CBUTL1,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_L2CBUPR,		vf_k_pc_err,		"vf_k_pc_err",
	VF_HD_L2CBUPRI,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KI_L2CBU,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KI_L2CBUTL1,		vf_k_pc_err,		"vf_k_pc_err",
	VF_UD_L2CBU,		memtest_u_l2_err,	"memtest_u_l2_err",
	VF_UI_L2CBU,		memtest_u_l2_err,	"memtest_u_l2_err",
/* XXX	VF_IO_LDRU,		memtest_u_l2_err,	"memtest_u_l2_err", */

	/* Remote L2 cache data and tag correctable errors. */
	VF_HD_L2CBC,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_L2CBC,		vf_k_pc_err,		"vf_k_pc_err",
	VF_HD_L2CBCMA,		vf_k_pc_err,		"vf_k_pc_err",
	VF_HD_L2CBCCWQ,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_L2CBCTL1,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KD_L2CBCPR,		vf_k_pc_err,		"vf_k_pc_err",
	VF_HD_L2CBCPRI,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KI_L2CBC,		vf_k_pc_err,		"vf_k_pc_err",
	VF_KI_L2CBCTL1,		vf_k_pc_err,		"vf_k_pc_err",
	VF_UD_L2CBC,		memtest_u_l2_err,	"memtest_u_l2_err",
	VF_UI_L2CBC,		memtest_u_l2_err,	"memtest_u_l2_err",
/* XXX	VF_IO_LDRC,		memtest_u_l2_err,	"memtest_u_l2_err", */

	/* Remote L2 cache write-back uncorrectable errors. */
	VF_HD_LWBU,	vf_k_pc_err,	"vf_k_pc_err",
	VF_HI_LWBU,	vf_k_pc_err,	"vf_k_pc_err",
	VF_KD_LWBU,	vf_k_pc_err,	"vf_k_pc_err",
	VF_KI_LWBU,	vf_k_pc_err,	"vf_k_pc_err",
	VF_UD_LWBU,	memtest_u_l2_err,	"memtest_u_l2_err",
	VF_UI_LWBU,	memtest_u_l2_err,	"memtest_u_l2_err",

	VF_HD_MCUFBRF,	vf_inject_fbr_failover, "vf_inject_fbr_failover",

	/* Coherency Link Timeout error */
	VF_CLTO,	vf_inject_clc_err,	"vf_inject_clc_err",

	/* LFU errors */
	VF_LFU_RTF,	vf_inject_lfu_rtf_err,	"vf_inject_lfu_rtf_err",
	VF_LFU_TTO,	vf_inject_lfu_to_err,	"vf_inject_lfu_to_err",
	VF_LFU_CTO,	vf_inject_lfu_to_err,	"vf_inject_lfu_to_err",
	VF_LFU_MLF,	vf_inject_lfu_lf_err,	"vf_inject_lfu_lf_err",
	VF_LFU_SLF,	vf_inject_lfu_lf_err,	"vf_inject_lfu_lf_err",

	/* NCX errors */
	VF_IO_NCXFDRTO,	vf_inject_ncx_err,	"vf_inject_ncx_err",
	VF_IO_NCXFSRTO,	vf_inject_ncx_err,	"vf_inject_ncx_err",
	VF_IO_NCXFRE,	vf_inject_ncx_err,	"vf_inject_ncx_err",
	VF_IO_NCXFSE,	vf_inject_ncx_err,	"vf_inject_ncx_err",
	VF_IO_NCXFDE,	vf_inject_ncx_err,	"vf_inject_ncx_err",

	/* SSI (bootROM interface) errors. */
	NI_HD_SSITO,	ni_k_ssi_err,		"ni_k_ssi_err",
	NI_HD_SSITOS,	ni_k_ssi_err,		"ni_k_ssi_err",
	NI_PRINT_SSI,	ni_k_ssi_err,		"ni_k_ssi_err",

	/* DEBUG test case(s) to get processor specific information from HV. */
	N2_TEST,	n2_inject_test_case, 	"n2_inject_test_case",
	VF_PRINT_ESRS,	vf_debug_print_esrs, 	"vf_debug_print_esrs",
	VF_CLEAR_ESRS, 	vf_debug_clear_esrs,	"vf_debug_clear_esrs",
	VF_SET_STEER,	vf_debug_set_errorsteer,
	    "vf_debug_set_errorsteer",

	NULL,		NULL,			NULL,
};

static cmd_t *commands[] = {
	vfalls_cmds,
	sun4v_generic_cmds,
	NULL
};


/*
 * *****************************************************************
 * The following block of routines are the Victoria Falls test routines.
 * *****************************************************************
 */

/*
 * Generate a coherency-link protocol error.  These are always fatal.
 * Currently the only error that can be triggered is a timeout error.
 *
 * A timeout is generated by writing a zero'd timeout value to the L2
 * Error Enable register for a given bank.  The default bank is bank 0
 * but a different bank number can be specified with the misc1 option.
 */
int
vf_inject_clc_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset;
	uint64_t	reg;
	int		l2bank;
	int		ret;
	char		*fname = "vf_inject_clc_err";

	if (F_MISC1(iocp)) {
		l2bank = iocp->ioc_misc1;
		if (l2bank < 0 || l2bank >= N2_NUM_L2_BANKS) {
			DPRINTF(0, "%s: misc1 argument out of bounds\n",
			    fname);
			return (EIO);
		}
	} else {
		l2bank = 0;
	}

	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (0);
	}

	offset = l2bank * N2_L2_BANK_OFFSET;

	reg = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    (N2_L2_ERR_ENB_REG + offset), NULL, NULL, NULL);

	reg &= ~(VF_L2_ERR_ENB_TO_MASK);

	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, (N2_L2_ERR_ENB_REG + offset),
	    reg, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to N2_L2_ERR_ENB_REG "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	return (0);
}

/*
 * Inject FBR errors due to failover by writing the FBDIMM TS3 config
 * register.
 */
int
vf_inject_fbr_failover(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		dram_branch;
	uint64_t	lane_select = IOC_XORPAT(iocp);
	char		*fname = "vf_inject_fbr_failover";

	/*
	 * The DRAM branch to use for injection can be selected
	 * with the misc1 option.  Default is 1.
	 */
	dram_branch = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 0x1);
	if (dram_branch < 0 || dram_branch >= VF_NUM_DRAM_BRANCHES) {
		DPRINTF(0, "%s: misc1 argument out of bounds\n", fname);
		return (EIO);
	}

	if (F_NOERR(mdatap->m_iocp)) {
		DPRINTF(0, "%s: not invoking error\n", fname);
		return (0);
	}

	(void) memtest_hv_util("hv_paddr_store64", (void *)hv_paddr_store64,
	    N2_DRAM_CSR_BASE + N2_DRAM_TS3_FAILOVER_CONFIG_REG +
	    (dram_branch * N2_DRAM_BRANCH_OFFSET), lane_select, NULL, NULL);

	return (0);
}

/*
 * Inject a LFU retrain fail error or increment replay count.
 * If infinite injection is specified a fatal retrain fail error
 * will be triggered, otherwise the replay count in the LFU
 * error status register will be incremented.  Note that
 * incrementing the replay count does not generate an error trap.
 */
int
vf_inject_lfu_rtf_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	reg1addr, reg2addr, reg3addr;
	uint64_t	reg1val, reg2val, reg3val;
	int		lfu_unit;
	int		ret;
	char		*fname = "vf_inject_lfu_rtf_err";

	/*
	 * The LFU unit to use for injection can be selected
	 * with the misc1 option.  Default is 0.
	 */
	lfu_unit = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 0x0);
	if (lfu_unit < 0 || lfu_unit >= VF_LFU_NUM_UNITS) {
		DPRINTF(0, "%s: misc1 argument out of bounds\n",
		    fname);
		return (EIO);
	}

	reg1addr = VF_LFU_ERR_INJ1_REG + (VF_LFU_STEP * lfu_unit);
	reg2addr = VF_LFU_ERR_INJ2_REG + (VF_LFU_STEP * lfu_unit);
	reg3addr = VF_LFU_ERR_INJ3_REG + (VF_LFU_STEP * lfu_unit);

	reg1val = VF_LFU_INJECTION_ENABLE;
	if (!F_INF_INJECT(iocp))
		reg1val |= VF_LFU_SSHOT_ENABLE;

	reg2val = 0;
	reg3val = VF_LFU_LANE_MASK << 24;

	if (F_NOERR(mdatap->m_iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (0);
	}

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
	 * If infinite injection is specified, execution should not make it
	 * this far since the expected outcome is a fatal reset.
	 */
	if (F_INF_INJECT(iocp)) {
		DPRINTF(0, "%s: unexpected continued system operation\n",
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
 */
int
vf_inject_lfu_to_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	to_regaddr, regaddr;
	uint64_t	to_regval, regval;
	int		lfu_unit;
	uint32_t	lane_select = IOC_XORPAT(iocp);
	int		ret;
	char		*fname = "vf_inject_lfu_to_err";

	/*
	 * The LFU unit to use for injection can be selected
	 * with the misc1 option.  Default is 0.
	 */
	lfu_unit = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 0x0);
	if (lfu_unit < 0 || lfu_unit >= VF_LFU_NUM_UNITS) {
		DPRINTF(0, "%s: misc1 argument out of bounds\n",
		    fname);
		return (EIO);
	}

	if (LFU_SUBCLASS_ISTTO(iocp->ioc_command)) {
		to_regaddr = VF_LFU_TRAN_STATE_TO_REG +
		    (VF_LFU_STEP * lfu_unit);
		to_regval = 0x10;	/* Minimum possible value */
	} else {
		to_regaddr = VF_LFU_CFG_STATE_TO_REG +
		    (VF_LFU_STEP * lfu_unit);
		to_regval = 0;
	}

	if (F_NOERR(mdatap->m_iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (0);
	}

	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, to_regaddr, to_regval, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to 0x%llx FAILED, "
		    "ret=0x%x\n", fname, to_regaddr, ret);
		return (ret);
	}

	regaddr = VF_LFU_SERDES_INVPAIR_REG + (VF_LFU_STEP * lfu_unit);
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
 * Inject either a LFU single-lane or multi-lane failure.
 * Lane failures are generated by writing a mask of the lanes selected
 * to fail to the LFU SERDES Transmitter and Receiver Differential
 * Pair Inversion Register (see the VF PRM 30.3.13).
 */
int
vf_inject_lfu_lf_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	regaddr;
	uint64_t	regval;
	int		lfu_unit;
	uint32_t	lane_select = IOC_XORPAT(iocp);
	int		ret = 0;
	char		*fname = "vf_inject_lfu_lf_err";

	/*
	 * The LFU unit to use for injection can be selected
	 * with the misc1 option.  Default is 0.
	 */
	lfu_unit = (F_MISC1(iocp) ? (iocp->ioc_misc1) : 0x0);
	if (lfu_unit < 0 || lfu_unit >= VF_LFU_NUM_UNITS) {
		DPRINTF(0, "%s: misc1 argument out of bounds\n",
		    fname);
		return (EIO);
	}

	/*
	 * The misc2 option can be used to specify the lane to
	 * fail for single-lane failure injections.
	 */
	if (F_MISC2(iocp)) {
		if (LFU_SUBCLASS_ISSLF(iocp->ioc_command)) {
			lane_select = 1 << iocp->ioc_misc2;
			lane_select |= lane_select << 14;
		}
	}

	if (F_NOERR(mdatap->m_iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (0);
	}

	regaddr = VF_LFU_SERDES_INVPAIR_REG + (VF_LFU_STEP * lfu_unit);
	regval = lane_select;
	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, regaddr, regval, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to 0x%llx FAILED, "
		    "ret=0x%x\n", fname, regaddr, ret);
		return (ret);
	}

	return (0);
}

/*
 * Inject an NCX protocol error.  These are always fatal.
 * Currently the only error that can be triggered is a timeout error.
 *
 * A timeout is generated by writing a zero'd timeout value to the NCX
 * Timeout Width Register.
 */
int
vf_inject_ncx_err(mdata_t *mdatap)
{
	int		ret;
	char		*fname = "vf_inject_ncx_err";

	if (F_NOERR(mdatap->m_iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (0);
	}

	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, VF_NCX_TWR_REG, 0ULL, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to VF_NCX_TWR_REG"
		    " FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	return (0);
}

/*
 * ************************************************************************
 * The following block of routines are the Victoria Falls support routines.
 * ************************************************************************
 */

/*
 * The consumer/producer routines below work together to produce the
 * following Victoria Falls errors:
 *	- CE in L2 copyback data (CBCE)
 *	- CE on foreign memory read data (DCE)
 *	- UE in external writeback data (WBUE)
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
 * The function vf_producer() is where the producer thread starts and
 * then continues on to inject the appropriate errors (as well as
 * trigger the L2 writeback errors).
 *
 * The function vf_k_pc_err() creates the separate producer thread and
 * then continues on to start the consumer thread which accesses the
 * corrupted data generated by the producer thread.
 *
 * NOTE: For these tests to work properly each thread must be bound to
 *	 a CPU on a separate VF chip.
 */

/*
 * This routine is used to invoke injected memory errors in hyperpriv
 * mode.
 */
int
vf_l2_h_invoke(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		err_acc = ERR_ACC(iocp->ioc_command);
	int		ret = 0;
	char		*fname = "vf_l2_h_invoke";

	/*
	 * If STORE was specified change the MA access type.
	 */
	if (F_STORE(iocp) && (ERR_ACC(iocp->ioc_command) == ERR_ACC_MAL)) {
		DPRINTF(3, "%s: setting MA access type to STORE\n", fname);
		err_acc = ERR_ACC_MAS;
	}

	switch (err_acc) {
	case ERR_ACC_LOAD:
		(void) memtest_hv_util("hv_paddr_load32",
		    (void *)hv_paddr_load32, mdatap->m_paddr_a, NULL,
		    NULL, NULL);
		break;
	case ERR_ACC_STORE:
		(void) memtest_hv_util("hv_paddr_store8",
		    (void *)hv_paddr_store8, mdatap->m_paddr_a,
		    (uchar_t)0xff, NULL, NULL);
		break;
	case ERR_ACC_MAL:	/* using polled mode */
		ret = OP_ACCESS_MA(mdatap, MA_OP_LOAD, (uint_t)0);
		break;
	case ERR_ACC_MAS:	/* using polled mode */
		ret = OP_ACCESS_MA(mdatap, MA_OP_STORE, (uint_t)0);
		break;
	case ERR_ACC_CWQ:	/* using polled mode */
		ret = OP_ACCESS_CWQ(mdatap, CWQ_OP_COPY, (uint_t)0);
		break;
	case ERR_ACC_PRICE:
		if (CPU_ISKT(mdatap->m_cip)) {
			ret = kt_flush_l2_entry_ice(mdatap->m_cip,
			    (caddr_t)mdatap->m_paddr_a);
		} else {
			ret = n2_flush_l2_entry_ice(mdatap->m_cip,
			    (caddr_t)mdatap->m_paddr_a);
		}
		break;
	default:
		DPRINTF(0, "%s: unsupported access type %d\n", fname, err_acc);
		ret = ENOTSUP;
		break;
	}

	return (ret);
}

/*
 * This routine is used to invoke injected L2 errors in kernel
 * mode.
 */
int
vf_l2_k_invoke(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;
	uint_t	err_acc = ERR_ACC(iocp->ioc_command);
	uint_t	myid = getprocessorid();
	int	ret = 0;
	char	*fname = "vf_l2_k_invoke";

	switch (err_acc) {
	case ERR_ACC_LOAD:
	case ERR_ACC_FETCH:
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)mdatap->m_asmld_tl1,
			    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
		} else {
			mdatap->m_asmld(mdatap->m_kvaddr_a);
		}
		break;
	case ERR_ACC_STORE:
		/*
		 * This store should get merged with the corrupted
		 * data injected above and cause a store merge error.
		 */
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)mdatap->m_asmst_tl1,
			    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0xff);
		} else {
			*mdatap->m_kvaddr_a = (uchar_t)0xff;
		}
		membar_sync();
		break;
	case ERR_ACC_PFETCH:
		memtest_prefetch_access(iocp, mdatap->m_kvaddr_a);
		DELAY(100);
		break;
	case ERR_ACC_BLOAD:
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)mdatap->m_blkld_tl1,
			    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
		} else {
			mdatap->m_blkld(mdatap->m_kvaddr_a);
		}
		break;
	default:
		DPRINTF(0, "%s: unsupported access type %d\n", fname, err_acc);
		ret = ENOTSUP;
	}

	return (ret);
}

/*
 * This is the consumer thread for foreign/remote L2 tests.  From here
 * the appropriate routine is called to invoked error(s) that have been
 * injected by the producer thread.
 */
static int
vf_l2_consumer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		ret = 0;
	volatile int	*syncp = mdatap->m_syncp;
	char		*fname = "vf_l2_consumer";

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
 * This routine is used to invoke L2 write back errors.  For VF,
 * it is called by the producer thread in order to trigger a
 * writeback to foreign memory error (i.e. WBUE).
 */
int
vf_l2wb_invoke(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;
	int	ret = 0;
	char	*fname = "vf_l2wb_invoke";

	if (!F_NOERR(iocp)) {
		DPRINTF(3, "%s: invoking error\n", fname);

		switch (ERR_MODE(iocp->ioc_command)) {
		case ERR_MODE_HYPR:
			OP_FLUSHALL_L2_HVMODE(mdatap);
			break;
		case ERR_MODE_KERN:
			OP_FLUSHALL_CACHES(mdatap);
			break;
		default:
			DPRINTF(0, "%s: invalid ERR_MODE=0x%x\n",
			    fname, ERR_MODE(iocp->ioc_command));
			ret = EINVAL;
		}
	}

	return (ret);
}

/*
 * This is the producer thread for the foreign/remote L2 tests.
 * This routine directs the injection of L2 errors that will, with the
 * exception of the writeback error, be invoked by the consumer thread.
 */
static int
vf_l2_producer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	int		ret = 0;
	char		*fname = "vf_l2_producer";

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
	 * is the one to invoke the error.
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
 * This routine is used to invoke injected memory errors in hyperpriv
 * mode.
 */
static int
vf_mem_h_invoke(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	pa_2access = mdatap->m_paddr_a;
	uint64_t	paddr;
	uint64_t	paddr_end;
	int		stride = 0x40;
	int		count, i;
	int		err_acc = ERR_ACC(iocp->ioc_command);
	caddr_t		kva_2access = mdatap->m_kvaddr_a;
	caddr_t		vaddr;
	int		ret = 0;
	char		*fname = "vf_mem_h_invoke";

	/*
	 * Check if this is a "storm" command and set the
	 * error count accordingly.
	 *
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

	/*
	 * If STORE was specified change the MA access type.
	 */
	if (F_STORE(iocp) && (ERR_ACC(iocp->ioc_command) == ERR_ACC_MAL)) {
		DPRINTF(3, "%s: setting MA access type to STORE\n", fname);
		err_acc = ERR_ACC_MAS;
	}

	paddr_end = P2ALIGN(pa_2access, PAGESIZE) + PAGESIZE;

	for (i = 0, vaddr = kva_2access, paddr = pa_2access;
	    (i < count && paddr < paddr_end);
	    vaddr += stride, paddr += stride) {

		if (ERR_MISC_ISSTORM(iocp->ioc_command) &&
		    vf_is_local_mem(mdatap, paddr)) {
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
			ret = OP_ACCESS_MA(mdatap, MA_OP_LOAD, (uint_t)0);
			break;
		case ERR_ACC_MAS:	/* using polled mode */
			ret = OP_ACCESS_MA(mdatap, MA_OP_STORE, (uint_t)0);
			break;
		case ERR_ACC_CWQ:	/* using polled mode */
			ret = OP_ACCESS_CWQ(mdatap, CWQ_OP_COPY, (uint_t)0);
			break;
		default:
			DPRINTF(0, "%s: unsupported access type %d\n",
			    fname, err_acc);
			ret = ENOTSUP;
		}
	}

	return (ret);
}

/*
 * This routine is used to invoke injected memory errors in kernel
 * mode.
 */
static int
vf_mem_k_invoke(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	pa_2access = mdatap->m_paddr_a;
	uint64_t	paddr;
	uint64_t	paddr_end;
	uint_t		myid = getprocessorid();
	int		stride = 0x40;
	int		count, i;
	int		err_acc = ERR_ACC(iocp->ioc_command);
	caddr_t		kva_2access = mdatap->m_kvaddr_a;
	caddr_t		vaddr;
	char		*fname = "vf_mem_k_invoke";

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

	for (i = 0, vaddr = kva_2access, paddr = pa_2access;
	    (i < count && paddr < paddr_end);
	    vaddr += stride, paddr += stride) {

		if (ERR_MISC_ISSTORM(iocp->ioc_command) &&
		    vf_is_local_mem(mdatap, paddr)) {
			continue;
		}

		i++;

		/*
		 * If we do not want to invoke the error(s) then continue.
		 */
		if (F_NOERR(iocp)) {
			DPRINTF(2, "%s: not invoking error %d "
			    "at vaddr=0x%p, paddr=0x%llx\n", fname,
			    i, vaddr, paddr);
			continue;
		}

		DPRINTF(2, "%s: invoking error %d at "
		    "vaddr=0x%p, paddr=0x%llx\n", fname, i, vaddr, paddr);

		switch (err_acc) {
		case ERR_ACC_LOAD:
		case ERR_ACC_FETCH:
			if (ERR_MISC_ISTL1(iocp->ioc_command))
				xt_one(myid, (xcfunc_t *)mdatap->m_asmld_tl1,
				    (uint64_t)vaddr, (uint64_t)0);
			else
				(mdatap->m_asmld)(vaddr);
			break;
		case ERR_ACC_PFETCH:
			memtest_prefetch_access(iocp, vaddr);
			DELAY(100);
			break;
		case ERR_ACC_BLOAD:
			if (ERR_MISC_ISTL1(iocp->ioc_command))
				xt_one(myid, (xcfunc_t *)mdatap->m_blkld_tl1,
				    (uint64_t)vaddr, (uint64_t)0);
			else
				mdatap->m_blkld(vaddr);
			break;
		case ERR_ACC_STORE:
			if (ERR_MISC_ISTL1(iocp->ioc_command))
				xt_one(myid, (xcfunc_t *)mdatap->m_asmst_tl1,
				    (uint64_t)vaddr, (uint64_t)0xff);
			else {
				DPRINTF(0, "%s: storing to invoke error\n",
				    fname);
				*vaddr = (uchar_t)0xff;
			}
			membar_sync();
			break;
		default:
			DPRINTF(0, "%s: unsupported access type %d\n",
			    fname, err_acc);
			return (ENOTSUP);
		}
	}

	return (0);
}

/*
 * This is the consumer thread for remote DRAM tests.  From here the
 * appropriate routine is called to invoked error(s) that have been
 * injected by the producer thread.
 */
static int
vf_mem_consumer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	int		ret = 0;
	char		*fname = "vf_mem_consumer";

	switch (ERR_MODE(iocp->ioc_command)) {
	case ERR_MODE_HYPR:
		ret = vf_mem_h_invoke(mdatap);
		break;
	case ERR_MODE_KERN:
		ret = vf_mem_k_invoke(mdatap);
		break;
	default:
		DPRINTF(0, "%s: invalid ERR_MODE=0x%x\n", fname,
		    ERR_MODE(iocp->ioc_command));
		ret = EINVAL;
	}

	/*
	 * Notify the producer thread to exit.
	 */
	*syncp = 3;

	return (ret);
}

/*
 * This is the producer thread for the foreign/remote DRAM tests.
 * This routine directs the injection of DRAM errors that will
 * later be invoked by the consumer thread.
 */
static int
vf_mem_producer(mdata_t *mdatap)
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
	char		*fname = "vf_mem_producer";

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

	DPRINTF(2, "%s: injecting the error(s)\n", fname);

	/*
	 * Inject the error(s).
	 */

	paddr_end = P2ALIGN(pa_2corrupt, PAGESIZE) + PAGESIZE;

	for (i = 0, vaddr = kva_2corrupt, raddr = ra_2corrupt,
	    paddr = pa_2corrupt; (i < count && paddr < paddr_end);
	    vaddr += stride, raddr += stride, paddr += stride) {

		if (ERR_MISC_ISSTORM(iocp->ioc_command) &&
		    !vf_is_local_mem(mdatap, paddr)) {
			continue;
		}

		i++;

		mdatap->m_kvaddr_c = vaddr;
		mdatap->m_raddr_c = raddr;
		mdatap->m_paddr_c = paddr;
		DPRINTF(2, "%s: injecting error %d at vaddr=0x%p, "
		    "paddr=0x%llx\n", fname, i, vaddr, paddr);
		if ((ret = memtest_inject_memory(mdatap)) != 0)
			return (ret);
	}

	/*
	 * Tell the consumer that we've injected the error.
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
 * This is the common producer thread routine for the
 * producer/consumer tests.
 */
static void
vf_producer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	int		ret;
	char		*fname = "vf_producer";

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
		ret = vf_mem_producer(mdatap);
		break;
	case ERR_CLASS_L2:
	case ERR_CLASS_L2WB:
		ret = vf_l2_producer(mdatap);
		break;
	default:
		DPRINTF(3, "%s: unsupported command=0x%llx\n", fname,
		    IOC_COMMAND(iocp));
		ret = EIO;
	}

	/*
	 * If there were any errors set the
	 * sync variable to the error value.
	 */
	if (ret)
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
vf_consumer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	int		ret;
	char		*fname = "vf_consumer";

	DPRINTF(3, "%s: mdatap=0x%p, syncp=0x%p\n", fname, mdatap, syncp);

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
		ret = vf_mem_consumer(mdatap);
		break;
	case ERR_CLASS_L2:
		ret = vf_l2_consumer(mdatap);
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
		DPRINTF(3, "%s: unsupported command=0x%llx\n", fname,
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
 * This routine is the main producer/consumer test routine.
 * It starts a separate thread for the producer and then
 * continues as the consumer.
 */
static int
vf_k_pc_err(mdata_t *mdatap)
{
	memtest_t	*mp = mdatap->m_memtestp;
	mdata_t		*producer_mdatap, *consumer_mdatap;
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	sync;
	char		*fname = "vf_k_pc_err";

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
	if (memtest_start_thread(producer_mdatap, vf_producer, fname) != 0) {
		cmn_err(CE_WARN, "memtest_start_thread: failed\n");
		return (EIO);
	}

	/*
	 * Start the consumer.
	 */
	DPRINTF(2, "%s: starting consumer\n", fname);
	if (vf_consumer(consumer_mdatap) != 0) {
		cmn_err(CE_WARN, "%s: vf_consumer() failed\n", fname);
		return (EIO);
	}

	return (0);
}

/*
 * Copy all asm routines into the buffer when the interleave is 1GB.
 */
static void
vf_copy_asm_1GB(mdata_t *mdatap)
{
	caddr_t		tmpvaddr;
	int		len = 256;

	tmpvaddr = mdatap->m_instbuf;
	bcopy((caddr_t)memtest_asmld, tmpvaddr, len);
	mdatap->m_asmld = (asmld_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_asmld_tl1, tmpvaddr, len);
	mdatap->m_asmld_tl1 = (asmld_tl1_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_asmldst, tmpvaddr, len);
	mdatap->m_asmldst = (asmldst_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_asmst_tl1, tmpvaddr, len);
	mdatap->m_asmst_tl1 = (asmst_tl1_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_blkld, tmpvaddr, len);
	mdatap->m_blkld = (blkld_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_blkld_tl1, tmpvaddr, len);
	mdatap->m_blkld_tl1 = (blkld_tl1_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_pcrel, tmpvaddr, len);
	mdatap->m_pcrel = (pcrel_t *)(tmpvaddr);
}

/*
 * Copy a subset of the asm routines into the buffer when the
 * interleave is 512B.
 */
static void
vf_copy_asm_512B(mdata_t *mdatap, cpu_info_t *cip)
{
	ioc_t		*iocp = mdatap->m_iocp;
	caddr_t		tmpvaddr;
	int		cpu_node_id;
	int		len, way;
	char		*fname = "vf_copy_asm_512B";

	len = 256;

	if ((way = vf_get_num_ways(cip)) == 1) {
		DPRINTF(2, "%s: single way (node) system detected, "
		    "asm routines will not be moved\n", fname);
		return;
	}

	cpu_node_id = VF_SYS_MODE_GET_NODEID(cip->c_sys_mode);

	tmpvaddr = mdatap->m_instbuf + (512 * cpu_node_id);

	/*
	 * The current size of m_instbuf is 4096 bytes (the upper
	 * half of an 8k page).  On a 4-way Victoria Falls system
	 * this means that for each node there are two 512-byte
	 * areas of memory in the buffer that are local to that
	 * node.  In this situation there is not enough memory to
	 * copy all of the asm routines so only a subset are copied
	 * based on what a test needs.
	 *
	 * For PC relative tests, only memtest_pcrel is copied.
	 *
	 * For TL1 tests, all three of the TL1 versions of the
	 * routines are copied:
	 *		memtest_asmld_tl1
	 *		memtest_asmst_tl1
	 *		memtest_blkld_tl1
	 *
	 * For everything else, the three regular non-TL1 versions
	 * of the routines are copied:
	 *		memtest_asmld
	 *		memtest_asmldst
	 *		memtest_blkld
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

	if (ERR_MISC_ISPCR(iocp->ioc_command)) {
		bcopy((caddr_t)memtest_pcrel, tmpvaddr, len);
		mdatap->m_pcrel = (pcrel_t *)(tmpvaddr);
		return;
	}

	if (ERR_MISC_ISTL1(iocp->ioc_command)) {
		bcopy((caddr_t)memtest_asmld_tl1, tmpvaddr, len);
		mdatap->m_asmld_tl1 = (asmld_tl1_t *)(tmpvaddr);

		tmpvaddr += len;
		bcopy((caddr_t)memtest_asmst_tl1, tmpvaddr, len);
		mdatap->m_asmst_tl1 = (asmst_tl1_t *)(tmpvaddr);

		/*
		 * Copy memtest_blkd_tl1 into the next local 512 bytes
		 */
		tmpvaddr = mdatap->m_instbuf + (512 * way) +
		    (512 * cpu_node_id);
		bcopy((caddr_t)memtest_blkld_tl1, tmpvaddr, len);
		mdatap->m_blkld_tl1 = (blkld_tl1_t *)(tmpvaddr);
		return;
	}

	bcopy((caddr_t)memtest_asmld, tmpvaddr, len);
	mdatap->m_asmld = (asmld_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_asmldst, tmpvaddr, len);
	mdatap->m_asmldst = (asmldst_t *)(tmpvaddr);

	/*
	 * Copy memtest_blkld into the next local 512 bytes.
	 */
	tmpvaddr = mdatap->m_instbuf + (512 * way) + (512 * cpu_node_id);
	bcopy((caddr_t)memtest_blkld, tmpvaddr, len);
	mdatap->m_blkld = (blkld_t *)(tmpvaddr);
}

/*
 * Early versions firmware/hardware did not leave the L2 and DRAM ESRs clear
 * after booting.  This function clears those ESRs.
 */
/*ARGSUSED*/
int
vf_debug_clear_esrs(mdata_t *mdatap)
{
	uint64_t	offset;
	uint64_t	read_val;
	uint64_t	l2_bank_enabled_val;
	uint64_t	bank_compare;
	int		i;
	int		ret;
	char		*fname = "vf_debug_clear_esrs";

	/*
	 * Clear the L2$ ESRs
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

		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (VF_L2_ND_ERR_REG + offset),
		    NULL, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (VF_L2_ND_ERR_REG + offset),
		    read_val, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to VF_L2_ND_ERR_REG"
			    " FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (VF_L2_ND_ERR_REG + offset),
		    0ULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to VF_L2_ND_ERR_REG"
			    " FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (VF_L2_ERR_SYND_REG + offset),
		    NULL, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (VF_L2_ERR_SYND_REG + offset),
		    read_val, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to VF_L2_ERR_STS_REG"
			    " FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (VF_L2_ERR_SYND_REG + offset),
		    0ULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to VF_L2_ERR_STS_REG"
			    " FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	/*
	 * Clear the (enabled) DRAM ESRs.
	 */
	l2_bank_enabled_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_L2_BANK_EN_STATUS_FULL,
	    NULL, NULL, NULL);
	l2_bank_enabled_val &= N2_L2_BANK_EN_STATUS_ALLEN;

	for (i = 0, offset = 0; i < VF_NUM_DRAM_BRANCHES; i++,
	    offset += N2_DRAM_BRANCH_OFFSET) {
		bank_compare = offset >> 12;
		if (((l2_bank_enabled_val >> bank_compare) & 0x3) != 0) {
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
	 * Clear the COU ESRs.
	 *
	 * NOTE: the COU regs are not available on a single-node VF system.
	 */
	if (VF_SYS_MODE_GET_WAY(mdatap->m_cip->c_sys_mode) !=
	    VF_SYS_MODE_1_WAY) {
		for (i = 0, offset = 0; i < VF_COU_COUNT; i++,
		    offset += VF_COU_STEP) {
			read_val = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, (VF_COU_ERR_STS_REG +
			    offset), NULL, NULL, NULL);

			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, (VF_COU_ERR_STS_REG +
			    offset), read_val, NULL, NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 to "
				    "VF_COU_ERR_STS_REG FAILED, "
				    " ret=0x%x\n", fname, ret);
				return (ret);
			}
		}
	}

	return (0);
}

int
vf_debug_print_esrs(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	uint64_t	offset;
	uint64_t	read_val, read_val2, read_oth;
	uint64_t	read_val3, read_val4;
	uint64_t	l2_bank_enabled_val;
	uint64_t	bank_compare;
	int		cpu_node_id;
	int		i;

	cpu_node_id = VF_SYS_MODE_GET_NODEID(cip->c_sys_mode);

	cmn_err(CE_CONT, " ");
	cmn_err(CE_CONT, "Error register values for node %d (with sys_mode "
	    "reg value = 0x%lx):", cpu_node_id, cip->c_sys_mode);

	/*
	 * Print the L2$ ESRs
	 */
	for (i = 0, offset = 0; i < N2_NUM_L2_BANKS; i++,
	    offset += N2_L2_BANK_OFFSET) {
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (N2_L2_ERR_STS_REG + offset),
		    NULL, NULL, NULL);

		read_val2 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (VF_L2_ERR_SYND_REG + offset),
		    NULL, NULL, NULL);

		read_oth = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (N2_L2_ERR_ENB_REG + offset),
		    NULL, NULL, NULL);

		read_val3 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (VF_L2_ND_ERR_REG + offset),
		    NULL, NULL, NULL);

		read_val4 = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (VF_L2_CTL_REG + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "L2 bank %x registers:", i);

		cmn_err(CE_CONT,
		    "L2_ESR = 0x%08x.%08x, L2_ESYR = 0x%08x.%08x",
		    PRTF_64_TO_32(read_val),
		    PRTF_64_TO_32(read_val2));

		cmn_err(CE_CONT,
		    "L2_ND  = 0x%08x.%08x, L2_EER  = 0x%08x.%08x",
		    PRTF_64_TO_32(read_val3),
		    PRTF_64_TO_32(read_oth));

		cmn_err(CE_CONT,
		    "L2_CTL = 0x%08x.%08x",
		    PRTF_64_TO_32(read_val4));
	}

	/*
	 * Print the (enabled) DRAM ESRs.
	 */
	l2_bank_enabled_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_L2_BANK_EN_STATUS_FULL,
	    NULL, NULL, NULL);
	l2_bank_enabled_val &= N2_L2_BANK_EN_STATUS_ALLEN;

	for (i = 0, offset = 0; i < VF_NUM_DRAM_BRANCHES; i++,
	    offset += N2_DRAM_BRANCH_OFFSET) {
		bank_compare = offset >> 12;
		if (((l2_bank_enabled_val >> bank_compare) & 0x3) != 0) {
			read_val = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, (N2_DRAM_CSR_BASE +
			    N2_DRAM_ERROR_STATUS_REG + offset),
			    NULL, NULL, NULL);

			read_val2 = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, (N2_DRAM_CSR_BASE +
			    N2_DRAM_ERROR_RETRY_REG + offset),
			    NULL, NULL, NULL);

			cmn_err(CE_CONT, "DRAM branch %x registers:", i);

			cmn_err(CE_CONT, "DRAM_ESR = 0x%08x.%08x, "
			    "DRAM_RETRY = 0x%08x.%08x",
			    PRTF_64_TO_32(read_val),
			    PRTF_64_TO_32(read_val2));
		}
	}

	/*
	 * Print the COU ESRs.
	 *
	 * NOTE: the COU regs are not available on a single-node VF system.
	 */
	if (VF_SYS_MODE_GET_WAY(mdatap->m_cip->c_sys_mode) !=
	    VF_SYS_MODE_1_WAY) {
		for (i = 0, offset = 0; i < VF_COU_COUNT; i++,
		    offset += VF_COU_STEP) {
			read_val = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, (VF_COU_ERR_STS_REG +
			    offset), NULL, NULL, NULL);

			read_val2 = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, (VF_COU_ERR_ENB_REG +
			    offset), NULL, NULL, NULL);

			cmn_err(CE_CONT, "COU %x registers:", i);

			cmn_err(CE_CONT, "COU_ESR = 0x%08x.%08x, "
			    "COU_EER = 0x%08x.%08x",
			    PRTF_64_TO_32(read_val),
			    PRTF_64_TO_32(read_val2));
		}
	}

	/*
	 * Print the LFU ESRs.
	 */
	for (i = 0, offset = 0; i < VF_LFU_COUNT; i++,
	    offset += VF_LFU_STEP) {
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (VF_LFU_ERR_STS_REG + offset),
		    NULL, NULL, NULL);

		cmn_err(CE_CONT, "LFU %x registers:", i);

		cmn_err(CE_CONT, "LFU_ESR = 0x%08x.%08x",
		    PRTF_64_TO_32(read_val));
	}

	/*
	 * Print the internal error regs (DESR, CERER, and SETER).
	 */

	cmn_err(CE_CONT, "Internal error registers:");

	read_val = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, 0x0, NULL, NULL);

	cmn_err(CE_CONT, "DESR = 0x%08x.%08x", PRTF_64_TO_32(read_val));

	read_val = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, 0x10, NULL, NULL);

	read_oth = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, 0x18, NULL, NULL);

	cmn_err(CE_CONT, "CERER = 0x%08x.%08x, SETER = 0x%08x.%08x",
	    PRTF_64_TO_32(read_val), PRTF_64_TO_32(read_oth));

	/*
	 * Print the SOC ESRs.
	 */
	cmn_err(CE_CONT, "SOC error registers:");

	read_val = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    N2_SOC_ERR_STS_REG, NULL, NULL, NULL);

	read_oth = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    N2_SOC_ERR_PND_REG, NULL, NULL, NULL);

	cmn_err(CE_CONT, "SOC ESR = 0x%08x.%08x, SOC PESR = 0x%08x.%08x",
	    PRTF_64_TO_32(read_val), PRTF_64_TO_32(read_oth));

	read_val = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    N2_SOC_INT_ENB_REG, NULL, NULL, NULL);

	read_oth = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    N2_SOC_FTL_ENB_REG, NULL, NULL, NULL);

	cmn_err(CE_CONT, "SOC INT = 0x%08x.%08x, SOC FATAL = 0x%08x.%08x",
	    PRTF_64_TO_32(read_val), PRTF_64_TO_32(read_oth));

	return (0);
}

int
vf_debug_set_errorsteer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	core;
	uint64_t	offset;
	uint64_t	reg_val;
	uint64_t	new_val;
	uint64_t	mask = ~(uint64_t)(7 << 18);
	int		i;
	int		ret;
	char		*fname = "vf_debug_set_errorsteer";

	if (F_MISC1(iocp)) {
		core = iocp->ioc_misc1;
		if (core < 0 || core > 7) {
			DPRINTF(0, "%s: misc1 argument out of bounds\n",
			    fname);
			return (EIO);
		}
	} else {
		DPRINTF(0, "%s: No new core specified.\n", fname);
		return (EIO);
	}

	for (i = 0, offset = 0; i < N2_NUM_L2_BANKS; i++,
	    offset += N2_L2_BANK_OFFSET) {
		reg_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (VF_L2_CTL_REG + offset),
		    NULL, NULL, NULL);

		DPRINTF(0, "Orig L2_CTL %x = 0x%08x.%08x\n", i,
		    PRTF_64_TO_32(reg_val));

		new_val = (reg_val & mask) | (core << 18);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (VF_L2_CTL_REG + offset),
		    new_val, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 to VF_L2_CTL_REG"
			    " FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		DPRINTF(0, "New L2_CTL %x =  0x%08x.%08x\n", i,
		    PRTF_64_TO_32(new_val));
	}

	reg_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, N2_SOC_ERR_STEER_REG,
	    NULL, NULL, NULL);

	DPRINTF(0, "Orig SOC steer = 0x%08x.%08x\n", PRTF_64_TO_32(reg_val));

	new_val = core << 3;

	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, N2_SOC_ERR_STEER_REG,
	    new_val, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 to N2_SOC_ERR_STEER_REG"
		    " FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	DPRINTF(0, "New SOC steer =  0x%08x.%08x\n", PRTF_64_TO_32(new_val));

	return (0);
}

/*
 * This routine enables error traps/reporting and cache ecc/parity checking.
 *
 * Because Victoria Falls is a CMT and to minimize system impact, by default the
 * injector will only the set the error checking on the strand that is expected
 * to see the error (this will be determined by higher level framework code).
 *
 * Victoria Falls  has the following error enable registers:
 *	CERER	- SPARC 8 per processor (one per physical core)
 *	SETER	- SPARC 8 per core (one per strand)
 *	L2$	- 8 per processor (one for each bank)
 *	COU	- 4 per processor (one for each coherency link cluster)
 *	Fatal	- 1 per processor (enables reset for fatal L2$/NCX errs)
 *	SOC Log	- 1 per processor (enables SOC error logging)
 *	SOC Int	- 1 per processor (enables SOC error interrupts)
 *	SOC Fat	- 1 per processor (enables SOC Fatal errors)
 *	SSI	- 1 per processor (same as Niagara-II)
 *
 * NOTE: the SPARC error enable registers seem to be only available to the
 *	 strand that owns it.  This means each strand involved in the test
 *	 must enable its own registers separately via the xcall code.
 *
 * NOTE: this routine is NOT setting or checking the PIU error enable
 *	 registers since these are related to the IO errors.
 */
int
vf_enable_errors(mdata_t *mdatap)
{
	uint64_t	exp_l2_set[N2_NUM_L2_BANKS], exp_l2fat_set;
	uint64_t	exp_cerer_set, exp_seter_set, exp_ssi_set;
	uint64_t	exp_soclog_set, exp_socint_set, exp_socfat_set;
	uint64_t	exp_cou_set;

	uint64_t	exp_l2_clr[N2_NUM_L2_BANKS], exp_l2fat_clr;
	uint64_t	exp_cerer_clr, exp_seter_clr, exp_ssi_clr;
	uint64_t	exp_soclog_clr, exp_socint_clr, exp_socfat_clr;
	uint64_t	exp_cou_clr;

	uint64_t	obs_l2_val[N2_NUM_L2_BANKS], obs_l2fat_val;
	uint64_t	obs_cerer_val, obs_seter_val, obs_ssi_val;
	uint64_t	obs_soclog_val, obs_socint_val, obs_socfat_val;
	uint64_t	obs_cou_val;

	uint64_t	set_l2_val[N2_NUM_L2_BANKS], set_l2fat_val;
	uint64_t	set_cerer_val, set_seter_val, set_ssi_val;
	uint64_t	set_soclog_val, set_socint_val, set_socfat_val;
	uint64_t	set_cou_val;

	uint64_t	keep_cou_val;

	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset;
	int		i;
	char		*fname = "vf_enable_errors";

	/*
	 * Define default (expected) register settings.
	 */
	for (i = 0; i < N2_NUM_L2_BANKS; i++) {
		exp_l2_set[i] = CEEN | NCEEN;
		exp_l2_clr[i] = 0;
	}
	exp_l2fat_set	= N2_L2_FTL_RST_ERREN;
	exp_l2fat_clr		= 0;

	exp_cerer_set	= N2_CERER_ERREN;
	exp_seter_set	= N2_SETER_ERREN;
	exp_ssi_set	= N2_SSI_ERR_CFG_ERREN;
	exp_cerer_clr	= 0;
	exp_seter_clr	= 0;
	exp_ssi_clr	= 0;

	exp_socfat_set	= N2_SOC_FAT_ERREN;
	exp_soclog_set	= exp_socint_set = N2_SOC_ERREN;
	exp_soclog_clr	= exp_socint_clr = exp_socfat_clr = 0;

	exp_cou_set 	= VF_COU_ERREN;
	exp_cou_clr	= 0;

	DPRINTF(2, "%s: exp_l2_set=0x%llx, exp_cerer_set=0x%llx, "
	    "exp_seter_set=0x%llx, exp_ssi_set=0x%llx, exp_soc_set=0x%llx\n",
	    fname, exp_l2_set[0], exp_cerer_set, exp_seter_set, exp_ssi_set,
	    exp_soclog_set);

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

	/*
	 * The COU regs are not available on a single-node VF system.
	 */
	if (VF_SYS_MODE_GET_WAY(mdatap->m_cip->c_sys_mode) !=
	    VF_SYS_MODE_1_WAY) {
		obs_cou_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, VF_COU_ERR_ENB_REG,
		    (uint64_t)n2_debug_buf_pa, NULL, NULL);
	}

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
		set_l2_val[i] = (obs_l2_val[i] | exp_l2_set[i]) &
		    ~exp_l2_clr[i];
	}
	set_l2fat_val   = (obs_l2fat_val | exp_l2fat_set) & ~exp_l2fat_clr;

	set_cerer_val   = (obs_cerer_val | exp_cerer_set) & ~exp_cerer_clr;
	set_seter_val   = (obs_seter_val | exp_seter_set) & ~exp_seter_clr;
	set_ssi_val	= (obs_ssi_val | exp_ssi_set) & ~exp_ssi_clr;

	set_soclog_val  = (obs_soclog_val | exp_soclog_set) & ~exp_soclog_clr;
	set_socint_val  = (obs_socint_val | exp_socint_set) & ~exp_socint_clr;
	set_socfat_val  = (obs_socfat_val | exp_socfat_set) & ~exp_socfat_clr;

	/*
	 * The COU regs are not available on a single-node VF system.
	 */
	if (VF_SYS_MODE_GET_WAY(mdatap->m_cip->c_sys_mode) !=
	    VF_SYS_MODE_1_WAY) {
		keep_cou_val	= (obs_cou_val & VF_COU_ERREN_MASK);
		set_cou_val	= (keep_cou_val | exp_cou_set) & ~exp_cou_clr;
	}

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
				    " 0x%lx to desired value (obs=0x%08x.%08x, "
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

	/*
	 * The COU regs are not available on a single-node VF system.
	 */
	if (VF_SYS_MODE_GET_WAY(mdatap->m_cip->c_sys_mode) !=
	    VF_SYS_MODE_1_WAY) {
		if (obs_cou_val != set_cou_val) {
			if (F_VERBOSE(iocp)) {
				cmn_err(CE_NOTE, "setting COU error enable "
				    "register to new falue (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)\n",
				    PRTF_64_TO_32(obs_cou_val),
				    PRTF_64_TO_32(set_cou_val));
			}

			(void) memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, VF_COU_ERR_ENB_REG,
			    set_cou_val, (uint64_t)n2_debug_buf_pa, NULL);

			/*
			 * Verify that the value was set correctly.
			 */
			obs_cou_val = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, VF_COU_ERR_ENB_REG,
			    (uint64_t)n2_debug_buf_pa, NULL, NULL);
			if (obs_cou_val != set_cou_val) {
				cmn_err(CE_WARN, "couldn't set COU err reg "
				    "to desired value (obs=0x%08x.%08x, "
				    "exp=0x%08x.%08x)!\n",
				    PRTF_64_TO_32(obs_cou_val),
				    PRTF_64_TO_32(set_cou_val));
			}
		}
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
static int
vf_get_cpu_info(cpu_info_t *cip)
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
	cip->c_mem_flags = MEMFLAGS_LOCAL;

	/*
	 * Memory is interleaved between nodes on Victoria Falls-based
	 * systems so c_mem_start and c_mem_size cannot be used.
	 *
	 * For Victoria Falls, the sys mode and L2 ctl registers are used
	 * to determine interleave configuration as well as which node a
	 * given CPU belongs to.  This information is then used to select
	 * appropriate CPUs and memory locations for foreign/remote tests
	 *
	 * The sys mode register has information on how many nodes there
	 * are and what node this CPU belongs to.
	 *
	 * The L2 CTL register has information on the interleave configuration.
	 */
	cip->c_sys_mode = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, VF_SYS_MODE_REG, NULL, NULL, NULL);

	cip->c_l2_ctl = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, VF_L2_CTL_REG, NULL, NULL, NULL);

	return (0);
}

/*
 * Get the node id of a physical address.  Unlike its counterpart in userspace
 * code, the address argument is a true physical address instead of a real
 * address.
 */
static int
vf_get_mem_node_id(cpu_info_t *cip, uint64_t paddr)
{
	int		mem_node_id;
	char		*fname = "vf_get_mem_node_id";

	/*
	 * Get the node that paddr belongs to.
	 */
	switch (VF_SYS_MODE_GET_WAY(cip->c_sys_mode)) {
	case VF_SYS_MODE_1_WAY:
		mem_node_id = VF_SYS_MODE_GET_NODEID(cip->c_sys_mode);
		break;

	case VF_SYS_MODE_2_WAY:
		if (VF_IS_512B_INTERLEAVE(cip->c_l2_ctl, paddr))
			mem_node_id = VF_2WY_512B_ADDR_NODEID(paddr);
		else
			mem_node_id = VF_2WY_1GB_ADDR_NODEID(paddr);
		break;

	case VF_SYS_MODE_3_WAY:
		/*
		 * There is no 512B interleave on a 3-way system and the
		 * 1GB interleave is equivalent to that of a 4-way
		 * system.
		 */
		mem_node_id = VF_4WY_1GB_ADDR_NODEID(paddr);
		break;

	case VF_SYS_MODE_4_WAY:
		if (VF_IS_512B_INTERLEAVE(cip->c_l2_ctl, paddr))
			mem_node_id = VF_4WY_512B_ADDR_NODEID(paddr);
		else
			mem_node_id = VF_4WY_1GB_ADDR_NODEID(paddr);
		break;

	default:
		DPRINTF(0, "%s: internal error", fname);
		return (-1);
	}

	return (mem_node_id);
}

static int
vf_get_num_ways(cpu_info_t *cip)
{
	char	*fname = "vf_get_num_ways";

	switch (VF_SYS_MODE_GET_WAY(cip->c_sys_mode)) {
	case VF_SYS_MODE_1_WAY:
		return (1);
	case VF_SYS_MODE_2_WAY:
		return (2);
	case VF_SYS_MODE_3_WAY:	 /* interleave is equal to 4-way 1GB */
	case VF_SYS_MODE_4_WAY:
		return (4);
	default:
		DPRINTF(0, "%s: internal error\n", fname);
		return (-1);
	}
}

void
vf_init(mdata_t *mdatap)
{
	mdatap->m_sopvp = &vfalls_vops;
	mdatap->m_copvp = &vfalls_cops;
	mdatap->m_cmdpp = commands;

	/*
	 * The op for checking esrs is not implemented for this
	 * platform.  If it is implemented in the future, then the
	 * following line should be removed.
	 */
	memtest_flags &= ~MFLAGS_DEFAULT;

	/*
	 * Determine the paddr of the n2_debug_buf to pass into the asm
	 * injection routines which run in hyperpriv mode.  Note that the
	 * first translation produces the raddr.
	 */
	n2_debug_buf_pa = memtest_kva_to_ra((void *)n2_debug_buf_va);
	n2_debug_buf_pa = memtest_ra_to_pa((uint64_t)n2_debug_buf_pa);
}

/*
 * Test whether paddr addresses memory local to the CPU specified in
 * the mdata_t.  paddr is expected to be a true physical address.
 */
int
vf_is_local_mem(mdata_t *mdatap, uint64_t paddr)
{
	cpu_info_t	*cip = mdatap->m_cip;
	int		cpu_node_id;
	int		mem_node_id;
	char		*fname = "vf_is_local_mem";

	cpu_node_id = VF_SYS_MODE_GET_NODEID(cip->c_sys_mode);

	if ((mem_node_id = vf_get_mem_node_id(cip, paddr)) < 0) {
		return (-1);
	}

	DPRINTF(2, "%s: cpu_node_id = 0x%x, mem_node_id = 0x%x, "
	    "paddr = 0x%llx", fname, cpu_node_id, mem_node_id, paddr);

	if (cpu_node_id == mem_node_id)
		return (1);
	else
		return (0);
}

int
vf_pre_test_copy_asm(mdata_t *mdatap)
{
	memtest_t	*mp = mdatap->m_memtestp;
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip;
	uint64_t	raddr, paddr;
	int		cpu_node_id;
	int		mem_node_id;
	char		*fname = "vf_pre_test_copy_asm";

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
	 */
	if (ERR_VF_ISFR(iocp->ioc_command) &&
	    !ERR_CLASS_ISL2WB(iocp->ioc_command)) {
		cip = mp->m_mdatap[1]->m_cip;
	} else {
		cip = mp->m_mdatap[0]->m_cip;
	}

	DPRINTF(2, "%s: copy data local to CPU %d\n", fname, cip->c_cpuid);

	/*
	 * Convert the address of m_instbuf into a true physical address.
	 */
	if ((raddr = memtest_kva_to_ra((void *)mdatap->m_instbuf)) == -1) {
		/*
		 * memtest_kva_to_ra() will have generated an error
		 * message.
		 */
		return (-1);
	}

	if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
		DPRINTF(0, "%s: ra to pa translation FAILED for "
		    "raddr = 0x%lx\n", fname, raddr);
		return (-1);
	}

	/*
	 * Copy the asm routines based on the interleave of the
	 * buffer.
	 */
	if (VF_IS_512B_INTERLEAVE(cip->c_l2_ctl, paddr) &&
	    (vf_get_num_ways(cip) != 1)) {
		vf_copy_asm_512B(mdatap, cip);
	} else {
		if (ERR_CLASS_ISMEM(iocp->ioc_command) ||
		    ERR_CLASS_ISMCU(iocp->ioc_command)) {
			/*
			 * Check if the allocated memory is local to the
			 * CPU that we need it to be local to.
			 */
			cpu_node_id = VF_SYS_MODE_GET_NODEID(cip->c_sys_mode);
			mem_node_id = vf_get_mem_node_id(cip, paddr);
			if (mem_node_id < 0) {
				return (-1);
			}
			if (cpu_node_id != mem_node_id) {
				DPRINTF(0, "%s: instbuf 0x%llx (nodeid %d) "
				    "not local to CPU %d (nodeid %d)\n",
				    fname, mdatap->m_instbuf, mem_node_id,
				    cip->c_cpuid, cpu_node_id);
				cmn_err(CE_NOTE, "Local memory is required "
				    "for memory tests");
				return (-1);
			}
		}
		vf_copy_asm_1GB(mdatap);
	}

	return (0);
}

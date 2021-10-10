/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains Niagara (UltraSPARC-T1) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_v.h>
#include <sys/memtestio_ni.h>
#include <sys/memtest_v.h>
#include <sys/memtest_ni.h>
#include <sys/memtest_v_asm.h>
#include <sys/memtest_ni_asm.h>

/*
 * Static routines located in this file.
 */
static void		ni_debug_init();
static void		ni_debug_dump();

/*
 * Debug buffer passed to some assembly routines.  Must be the PA for
 * routines which run in hyperprivileged mode.
 */
#define		DEBUG_BUF_SIZE	32
uint64_t	ni_debug_buf_va[DEBUG_BUF_SIZE];
uint64_t	ni_debug_buf_pa;

/*
 * Enable/disable the storing of DEBUG values into the debug mem buffer
 * for different test classes.
 */
/* #define	MEMDEBUG_BUFFER		1 */
/* #define	L1_DEBUG_BUFFER		1 */
/* #define	L2_DEBUG_BUFFER		1 */

/*
 * Niagara operations vector tables.
 */
static opsvec_v_t niagara_vops = {
	/* sun4v injection ops vectors */
	ni_inject_hvdcache,	/* corrupt d$ data or tag in hv mode */
	ni_inject_hvicache,	/* corrupt i$ data or tag in hv mode */
	ni_inject_l2dir,	/* corrupt l2$ directory at raddr */
	notsup,			/* no ND */
	ni_inject_l2vad,	/* corrupt l2$ VA(U)D bits at raddr or offset */
	ni_inject_ma_memory,	/* corrupt MA memory parity */
	notsup,			/* no install memory NotData */
	ni_inject_memory,	/* corrupt local memory */

	/* sun4v support ops vectors */
	notsup,			/* no CWQ access */
	ni_access_ma_memory,	/* access MA memory */
	notsup,			/* no index hashing */
	ni_flushall_l2_hvmode,	/* flush all l2$ (inclusive) in hv mode */
	ni_flush_l2_entry_hvmode, /* flush single l2$ entry in hv mode */
};

static opsvec_c_t niagara_cops = {
	/* common injection ops vectors */
	ni_inject_dcache,	/* corrupt d$ data or tag at raddr */
	ni_inject_dphys,	/* corrupt d$ data or tag at offset */
	ni_inject_freg_file,	/* corrupt FP register file */
	ni_inject_icache,	/* corrupt i$ data or tag at raddr */
	notsup,			/* no corrupt internal */
	ni_inject_iphys,	/* corrupt i$ data or tag at offset */
	ni_inject_ireg_file,	/* corrupt integer register file */
	ni_inject_l2cache,	/* corrupt l2$ data or tag at raddr */
	ni_inject_l2phys,	/* corrupt l2$ data or tag at offset */
	notsup,			/* no corrupt l3$ data or tag at raddr */
	notsup,			/* no corrupt l3$ data or tag at offset */
	ni_inject_tlb,		/* I-D TLB parity errors */

	/* common support ops vectors */
	ni_access_freg_file,	/* access FP register file */
	ni_access_ireg_file,	/* access integer register file */
	notimp,			/* check ESRs */
	ni_enable_errors,	/* enable AFT errors */
	ni_control_scrub,	/* enable/disable L2 or memory scrubbers */
	ni_get_cpu_info,	/* put cpu info into struct */
	ni_flushall_caches,	/* flush all caches in hv mode */
	ni_clearall_dcache,	/* clear (not just flush) all d$ in hv mode */
	ni_clearall_icache,	/* clear (not just flush) all i$ in hv mode */
	ni_flushall_l2_kmode,	/* flush all l2$ (inclusive) in kern mode */
	notsup,			/* no flush all l3$ */
	notsup,			/* no flush single d$ entry */
	notsup,			/* no flush single i$ entry */
	notsup,			/* no flush single l2$ entry */
	notsup,			/* no flush single l3$ entry */
};

/*
 * These Niagara error commands are grouped according to the definitions
 * in the memtestio_ni.h header file.
 */
cmd_t niagara_cmds[] = {
	/* Memory (DRAM) uncorrectable errors. */
	NI_HD_DAU,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_HI_DAU,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_KD_DAU,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_HD_DAUMA,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_KD_DAUTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KD_DAUPR,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KI_DAU,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KI_DAUTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_UD_DAU,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_UI_DAU,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_KD_DSU,	ni_inject_memory_scrub,	"ni_inject_memory_scrub",
	NI_KD_DBU,	ni_inject_memory_range,	"ni_inject_memory_range",
	NI_IO_DRU,	memtest_u_mem_err,	"memtest_u_mem_err",

	/* Memory (DRAM) correctable errors. */
	NI_HD_DAC,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_HI_DAC,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_KD_DAC,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_HD_DACMA,	memtest_h_mem_err,	"memtest_h_mem_err",
	NI_KD_DACTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KD_DACPR,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KD_DACSTORM,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KI_DAC,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_KI_DACTL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	NI_UD_DAC,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_UI_DAC,	memtest_u_mem_err,	"memtest_u_mem_err",
	NI_KD_DSC,	ni_inject_memory_scrub,	"ni_inject_memory_scrub",
	NI_IO_DRC,	memtest_u_mem_err,	"memtest_u_mem_err",

	/* L2 cache data and tag uncorrectable errors. */
	NI_HD_LDAU,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_HI_LDAU,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_KD_LDAU,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_LDAUCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	NI_HD_LDAUMA,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_KD_LDAUTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KD_LDAUPR,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_OBP_LDAU,	memtest_obp_err,	"memtest_obp_err",
	NI_KI_LDAU,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KI_LDAUTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_UD_LDAU,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_UI_LDAU,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_KD_LDSU,	ni_inject_l2_scrub,	"ni_inject_l2_scrub",
	NI_IO_LDRU,	memtest_u_l2_err,	"memtest_u_l2_err",

	/* L2 cache data and tag correctable errors. */
	NI_HD_LDAC,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_HI_LDAC,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_KD_LDAC,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_LDACCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	NI_HD_LDACMA,	memtest_h_l2_err,	"memtest_h_l2_err",
	NI_KD_LDACTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KD_LDACPR,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_OBP_LDAC,	memtest_obp_err,	"memtest_obp_err",
	NI_KI_LDAC,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_KI_LDACTL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	NI_UD_LDAC,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_UI_LDAC,	memtest_u_l2_err,	"memtest_u_l2_err",
	NI_KD_LDSC,	ni_inject_l2_scrub,	"ni_inject_l2_scrub",
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
	NI_L2SCRUBPHYS,	ni_inject_l2_scrub,	"ni_inject_l2_scrub",
	NI_K_L2VIRT,	memtest_k_l2virt,	"memtest_k_l2virt",
	NI_U_L2VIRT,	memtest_k_l2virt,	"memtest_k_l2virt",

	/* L2 cache write back errors. */
	NI_HD_LDWU,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_HI_LDWU,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_KD_LDWU,	memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	NI_KI_LDWU,	memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	NI_UD_LDWU,	memtest_u_l2_err,	"memtest_k_l2_err",
	NI_UI_LDWU,	memtest_u_l2_err,	"memtest_k_l2_err",

	NI_HD_LDWC,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_HI_LDWC,	memtest_h_l2wb_err,	"memtest_h_l2wb_err",
	NI_KD_LDWC,	memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	NI_KI_LDWC,	memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	NI_UD_LDWC,	memtest_u_l2_err,	"memtest_k_l2_err",
	NI_UI_LDWC,	memtest_u_l2_err,	"memtest_k_l2_err",

	/* L2 cache VA(U)D errors. */
	NI_KD_LVU_VD,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	NI_KI_LVU_VD,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	NI_UD_LVU_VD,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	NI_UI_LVU_VD,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

	NI_KD_LVU_UA,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	NI_KI_LVU_UA,	memtest_k_l2vad_err,	"memtest_k_l2vad_err",
	NI_UD_LVU_UA,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",
	NI_UI_LVU_UA,	memtest_u_l2vad_err,	"memtest_u_l2vad_err",

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

	NI_DPHYS,	memtest_dphys,		"memtest_dphys",
	NI_DTPHYS,	memtest_dphys,		"memtest_dphys",

	/* L1 instruction cache data and tag correctable errors. */
	NI_HI_IDC,	memtest_h_ic_err, 	"memtest_h_ic_err",
	NI_KI_IDC,	memtest_k_ic_err, 	"memtest_k_ic_err",
	NI_KI_IDCTL1,	memtest_k_ic_err, 	"memtest_k_ic_err",
	NI_HI_ITC,	memtest_h_ic_err, 	"memtest_h_ic_err",
	NI_KI_ITC,	memtest_k_ic_err, 	"memtest_k_ic_err",
	NI_KI_ITCTL1,	memtest_k_ic_err, 	"memtest_k_ic_err",

	NI_IPHYS,	memtest_iphys,		"memtest_iphys",
	NI_ITPHYS,	memtest_iphys,		"memtest_iphys",

	/* Instruction and data TLB data and tag (CAM) errors. */
	NI_KD_DMDU,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	NI_HD_DMTU,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	NI_UD_DMDU,	memtest_u_tlb_err,	"memtest_u_tlb_err",
	NI_UD_DMTU,	memtest_u_tlb_err,	"memtest_u_tlb_err",
	NI_KD_DMSU,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	NI_HD_DMDUASI,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	NI_UD_DMDUASI,	memtest_u_tlb_err,	"memtest_u_tlb_err",

	NI_DMDURAND,	ni_inject_tlb_random,	"ni_inject_tlb_random",
	NI_DMTURAND,	ni_inject_tlb_random,	"ni_inject_tlb_random",

	NI_KI_IMDU,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	NI_HI_IMTU,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	NI_UI_IMDU,	memtest_u_tlb_err,	"memtest_u_tlb_err",
	NI_UI_IMTU,	memtest_u_tlb_err,	"memtest_u_tlb_err",
	NI_HI_IMDUASI,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	NI_UI_IMDUASI,	memtest_u_tlb_err,	"memtest_u_tlb_err",

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

	/* Modular Arithmetic Unit (SPARC Internal) errors. */
	NI_HD_MAUL,	memtest_h_ma_err,	"memtest_h_ma_err",
	NI_HD_MAUS,	memtest_h_ma_err,	"memtest_h_ma_err",
	NI_HD_MAUO,	memtest_h_ma_err,	"memtest_h_ma_err",

	/* JBus (system bus) errors. */
	NI_KD_BE,	memtest_k_bus_err,	"memtest_k_bus_err",
	NI_KD_BEPEEK,	memtest_k_bus_err,	"memtest_k_bus_err",

	NI_HD_APAR,	ni_k_bus_err,		"ni_k_bus_err",
	NI_HI_APAR,	ni_k_bus_err,		"ni_k_bus_err",
	NI_HD_CPAR,	ni_k_bus_err,		"ni_k_bus_err",
	NI_HI_CPAR,	ni_k_bus_err,		"ni_k_bus_err",

	NI_HD_DPAR,	ni_k_bus_err,		"ni_k_bus_err",
	NI_HI_DPAR,	ni_k_bus_err,		"ni_k_bus_err",
	NI_HD_DPARS,	ni_k_bus_err,		"ni_k_bus_err",
	NI_HD_DPARO,	ni_k_bus_err,		"ni_k_bus_err",

	NI_HD_L2TO,	ni_k_bus_err,		"ni_k_bus_err",
	NI_HD_ARBTO,	ni_k_bus_err,		"ni_k_bus_err",
	NI_HD_RTO,	ni_k_bus_err,		"ni_k_bus_err",
	NI_HD_INTRTO,	ni_k_bus_err,		"ni_k_bus_err",

	NI_HD_UMS,	ni_k_bus_err,		"ni_k_bus_err",
	NI_HD_NEMS,	ni_k_bus_err,		"ni_k_bus_err",
	NI_HD_NEMR,	ni_k_bus_err,		"ni_k_bus_err",

	NI_CLR_JBI_LOG,	ni_k_bus_err,		"ni_k_bus_err",
	NI_PRINT_JBI,	ni_k_bus_err,		"ni_k_bus_err",
	NI_TEST_JBI,	ni_k_bus_err,		"ni_k_bus_err",

	/* SSI (bootROM interface) errors. */
	NI_HD_SSITO,	ni_k_ssi_err,		"ni_k_ssi_err",
	NI_HD_SSITOS,	ni_k_ssi_err,		"ni_k_ssi_err",
	NI_PRINT_SSI,	ni_k_ssi_err,		"ni_k_ssi_err",

	/* DEBUG test cases to get processor specific information from HV. */
	NI_TEST,	ni_inject_test_case,	"ni_inject_test_case",
	NI_PRINT_ESRS,	ni_debug_print_esrs,	"ni_debug_print_esrs",
	NI_PRINT_UE,	ni_util_print,		"ni_util_print",
	NI_PRINT_CE,	ni_util_print,		"ni_util_print",

	NULL,		NULL,			NULL,
};

static cmd_t *commands[] = {
	niagara_cmds,
	sun4v_generic_cmds,
	NULL
};

static void
ni_debug_init()
{
	int	i;

	for (i = 0; i < DEBUG_BUF_SIZE; i++)
		ni_debug_buf_va[i] = 0xeccdeb46eccdeb46;
}

static void
ni_debug_dump()
{
	int	i;

	for (i = 0; i < DEBUG_BUF_SIZE; i++) {
		DPRINTF(0, "ni_debug_dump: ni_debug_buf[0x%2x]=0x%llx\n",
		    i*8, ni_debug_buf_va[i]);
	}
}

/*
 * **************************************************************
 * The following block of routines are the Niagara test routines.
 * **************************************************************
 */

/*
 * This routine inserts an error into the data cache parity bits protecting
 * the data or the tags at a location specified by the physical address mdata
 * struct member.
 *
 * Valid xorpat values are:
 * 	Data:     [63:0]
 * 	Data par: [20:13]
 * 	Tag:      [29:1]
 * 	Tag par:  [13]
 */
int
ni_inject_dcache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	int		ret;
	char		*fname = "ni_inject_dcache";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	/*
	 * Ensure that the L1 caches are in 'normal' (not DM) replacement mode.
	 */
	if (!F_NOERR(iocp)) {
		if ((memtest_hv_util("ni_l1_disable_DM",
		    (void *)ni_l1_disable_DM, NULL, NULL,
		    NULL, NULL)) != PASS) {
			return (EIO);
		}
		DPRINTF(1, "%s: L1$ DM mode disabled\n", fname);
	}

	/*
	 * If the IOCTL specified the tag, corrupt the tag parity.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

#ifdef	L1_DEBUG_BUFFER
	ni_debug_init();
#endif	/* L1_DEBUG_BUFFER */

		ret = memtest_hv_inject_error("ni_inj_dcache_tag",
		    (void *)ni_inj_dcache_tag, (uint64_t)paddr,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)ni_debug_buf_pa);
#ifdef	L1_DEBUG_BUFFER
	ni_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_dcache_tag "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Otherwise corrupt the dcache data parity.
		 */

#ifdef	L1_DEBUG_BUFFER
	ni_debug_init();
#endif	/* L1_DEBUG_BUFFER */

		ret = memtest_hv_inject_error("ni_inj_dcache_data",
		    (void *)ni_inj_dcache_data, (uint64_t)paddr,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)ni_debug_buf_pa);
#ifdef	L1_DEBUG_BUFFER
	ni_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_dcache_data "
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
 * This routine is similar to the above ni_inject_dcache() routine.
 */
int
ni_inject_dphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	int		ret;
	char		*fname = "ni_inject_dphys";

	DPRINTF(3, "%s: iocp=0x%p, offset=0x%lx\n", fname, iocp, offset);

	/*
	 * If the IOCTL specified the tag, corrupt the tag parity.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("ni_inj_dphys_tag",
		    (void *)ni_inj_dphys_tag, offset,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)ni_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_dphys_tag "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Otherwise corrupt the dcache data parity.
		 */
		ret = memtest_hv_inject_error("ni_inj_dphys_data",
		    (void *)ni_inj_dphys_data, offset,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)ni_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_dphys_data "
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
 * This routine is similar to the above ni_inject_ireg_file() routine.
 *
 * NOTE: Niagara only implements one set of floating point registers so
 *	 unlike the integer register file tests there are no issues in
 *	 regard to register windows.
 */
int
ni_inject_freg_file(mdata_t *mdatap, uint64_t offset)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	enable;
	uint64_t	eccmask;
	int		ret;
	char		*fname = "ni_inject_freg_file";

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

	ret = memtest_hv_inject_error("ni_inj_freg_file",
	    (void *)ni_inj_freg_file, paddr, enable, eccmask,
	    (offset * REG_ERR_STRIDE));

	DPRINTF(3, "%s: ret = 0x%x\n", fname, ret);
	return (0);
}

/*
 * This routine inserts an error into the parity bits protecting the
 * L1 data cache data or L1 the data cache tags at a location determined
 * by the physical address mdata struct member.
 *
 * This routine is similar to the ni_inject_dcache() routine except that
 * the corrupted paddr will be accessed while in hyperpriv mode so there is
 * no opportunity to use the NO_ERR flag.
 */
int
ni_inject_hvdcache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint_t		access_type = 0;
	int		ret;
	char		*fname = "ni_inject_hvdcache";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	if ((ERR_ACC(iocp->ioc_command) == ERR_ACC_STORE)) {
		access_type = ERR_ACC_STORE;
	}

	/*
	 * If the IOCTL specified the tag, corrupt the tag parity.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("ni_inj_dcache_hvtag",
		    (void *)ni_inj_dcache_hvtag, paddr,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)access_type);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_dcache_hvtag "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Otherwise corrupt the dcache data parity.
		 */
		ret = memtest_hv_inject_error("ni_inj_dcache_hvdata",
		    (void *)ni_inj_dcache_hvdata, paddr,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)access_type);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_dcache_hvdata "
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
 * NOTE: the corrupted hyperpriv routine will be run while in hyperpriv mode
 *	 so there is no opportunity to use the NO_ERR flag.
 */
int
ni_inject_hvicache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	caddr_t		kvaddr;
	uint64_t	raddr, paddr;
	int		ret;
	char		*fname = "ni_inject_hvicache";

	/*
	 * Find the raddr and paddr of asm routine to corrupt from it's kvaddr.
	 */
	kvaddr = (caddr_t)ni_ic_hvaccess;
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

		ret = memtest_hv_inject_error("ni_inj_icache_hvtag",
		    (void *)ni_inj_icache_hvtag, paddr, IOC_XORPAT(iocp),
		    (uint64_t)ni_debug_buf_pa, NULL);

		if (ret == 0xded) {
			DPRINTF(0, "%s: ni_inj_icache_hvtag FAILED to find "
			    "matching tag in icache,\n\t\tlikely it has "
			    "been evicted, ret=0x%x\n", fname, ret);
			return (-1);
		}
	} else {
		/*
		 * Otherwise corrupt the icache instr parity.
		 */
		ret = memtest_hv_inject_error("ni_inj_icache_hvinstr",
		    (void *)ni_inj_icache_hvinstr, paddr, IOC_XORPAT(iocp),
		    (uint64_t)ni_debug_buf_pa, NULL);

		if (ret == 0xded) {
			DPRINTF(0, "%s: ni_inj_icache_hvinstr FAILED to find "
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
 * 	Tag:       [27:0]
 * 	Tag par:   [32]
 */
int
ni_inject_icache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	vaddr, raddr, paddr;
	void		(*func_vaddr)(caddr_t);
	uint_t		myid;
	int		ret;
	char		*fname = "ni_inject_icache";

	DPRINTF(3, "%s: iocp=0x%p\n", fname, iocp);

	myid = getprocessorid();

	/*
	 * Ensure that the L1 caches are in 'normal' (not DM) replacement mode.
	 */
	if (!F_NOERR(iocp)) {
		if ((memtest_hv_util("ni_l1_disable_DM",
		    (void *)ni_l1_disable_DM, NULL, NULL,
		    NULL, NULL)) != PASS) {
			return (EIO);
		}
		DPRINTF(1, "%s: L1$ DM mode disabled\n", fname);
	}

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
	ni_debug_init();
#endif	/* L1_DEBUG_BUFFER */

	/*
	 * If the IOCTL specified the tag, corrupt the tag parity.
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

		ret = memtest_hv_inject_error("ni_inj_icache_tag",
		    (void *)ni_inj_icache_tag, paddr, IOC_XORPAT(iocp),
		    (uint64_t)ni_debug_buf_pa, NULL);

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
			DPRINTF(0, "%s: ni_inj_icache_tag FAILED to find "
			    "matching tag in icache,\n\t\tlikely it has "
			    "been evicted, ret=0x%x\n", fname, ret);
#ifdef	L1_DEBUG_BUFFER
	ni_debug_dump();
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

		ret = memtest_hv_inject_error("ni_inj_icache_instr",
		    (void *)ni_inj_icache_instr, paddr, IOC_XORPAT(iocp),
		    (uint64_t)ni_debug_buf_pa, NULL);

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
			DPRINTF(0, "%s: ni_inj_icache_instr FAILED to find "
			    "matching tag in icache,\n\t\tlikely it has "
			    "been evicted, ret=0x%x\n", fname, ret);
#ifdef	L1_DEBUG_BUFFER
	ni_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

			return (-1);
		}
	}

#ifdef	L1_DEBUG_BUFFER
	ni_debug_dump();
#endif	/* L1_DEBUG_BUFFER */

	return (0);
}

/*
 * This routine inserts an error into the parity bits protecting the
 * instruction cache data or the instruction cache tags at a location
 * specified by the byte offset in the ioc_addr member of the ioc struct.
 *
 * This routine is similar to the above ni_inject_icache() routine except
 * that it uses a cache offset to choose the line to corrupt.
 *
 * Valid byte offset values are in the range: 0x0 - 0x3ff8 (16KB cache)
 * and note that the HV routine will align the offset for the asi accesses.
 */
int
ni_inject_iphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	int		ret;
	char		*fname = "ni_inject_iphys";

	DPRINTF(3, "%s: iocp=0x%p, offset=0x%llx\n", fname, iocp, offset);

	/*
	 * If the IOCTL specified the tag, corrupt the tag parity.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("ni_inj_iphys_tag",
		    (void *)ni_inj_iphys_tag, offset, IOC_XORPAT(iocp),
		    (uint64_t)ni_debug_buf_pa, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_iphys_tag FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Otherwise corrupt the icache instr parity.
		 */
		ret = memtest_hv_inject_error("ni_inj_iphys_instr",
		    (void *)ni_inj_iphys_instr, offset, IOC_XORPAT(iocp),
		    (uint64_t)ni_debug_buf_pa, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_iphys_instr FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}
	}
	return (0);
}

/*
 * The following three functions,
 *
 *      ni_k_inject_ireg_store
 *      ni_k_inject_ireg_op
 *      ni_k_inject_ireg_load
 *
 * are functions for injecting and triggering integer register file errors.
 * Each function triggers the error in a different manner.
 *
 * These routines are intentionally kept sparse to avoid operations on registers
 * in the current register window which could end up overwriting an injected
 * error with good data before the error can be invoked.
 * The injection and invocation of the error is done in a single routine to
 * ensure that the same register window is acted upon.  To accomplish this,
 * both functions called are leaf functions that use the caller's register
 * window.
 *
 * NOTE: ni_k_inject_ireg_load will not actually cause an error
 *
 * NOTE: These three routines must have a return value (even though it's
 *	 not used) or the compiler will change things in such a way that
 *	 no error will be seen.
 *
 * NOTE: memtest_run_hpriv is called directly from these routines to avoid
 *	 changing the register window with an intervening function call.
 */
uint64_t
ni_k_inject_ireg_store(uint64_t func_paddr, uint64_t kvaddr, uint64_t a1,
    uint64_t a2, uint64_t a3, uint64_t a4)
{
	/* Inject the error */
	(void) memtest_run_hpriv((uint64_t)func_paddr, a1, a2, a3, a4);

	/* Access the error */
	(void) ni_k_ireg_store(kvaddr);

	return (0);
}

uint64_t
ni_k_inject_ireg_op(uint64_t func_paddr, uint64_t a1, uint64_t a2,
    uint64_t a3, uint64_t a4)
{
	/* Inject the error */
	(void) memtest_run_hpriv((uint64_t)func_paddr, a1, a2, a3, a4);

	/* Access the error */
	(void) ni_k_ireg_op();

	return (0);
}

uint64_t
ni_k_inject_ireg_load(uint64_t func_paddr, uint64_t kvaddr, uint64_t a1,
    uint64_t a2, uint64_t a3, uint64_t a4)
{
	/* Inject the error */
	(void) memtest_run_hpriv((uint64_t)func_paddr, a1, a2, a3, a4);

	/* Access the error */
	(void) ni_k_ireg_load(kvaddr);

	return (0);
}

/*
 * This routine does some preliminary setup before calling one of the above
 * three routines.
 */
uint64_t
ni_k_inject_ireg(mdata_t *mdatap, char *func_name, void *vaddr, uint64_t a1,
    uint64_t a2, uint64_t a3, uint64_t a4)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	raddr;
	uint64_t	paddr;
	char		*fname = "ni_k_inject_ireg";

	DPRINTF(3, "%s: routine %s at vaddr = 0x%p, arg1 = 0x%lx,"
	    " arg2 = 0x%lx, arg3 = 0x%lx, arg4 = 0x%lx\n",
	    fname, func_name, vaddr, a1, a2, a3, a4);

	/*
	 * Find the raddr from the vaddr.
	 */
	if ((raddr = memtest_kva_to_ra((void *)vaddr)) == -1) {
		return (raddr);
	}

	/*
	 * Translate the raddr of routine to paddr via hypervisor.
	 */
	if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
		DPRINTF(0, "%s: ra to pa translation failed for "
		    "raddr=0x%lx\n", fname, raddr);
		return (paddr);
	}

	if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
		(void) ni_k_inject_ireg_store(paddr,
		    (uint64_t)mdatap->m_kvaddr_a, a1, a2, a3, a4);
	} else if (ERR_ACC_ISOP(iocp->ioc_command)) {
		(void) ni_k_inject_ireg_op(paddr, a1, a2, a3, a4);
	} else {
		(void) ni_k_inject_ireg_load(paddr,
		    (uint64_t)mdatap->m_kvaddr_a, a1, a2, a3, a4);
	}

	return (0);
}

/*
 * This routine inserts an ecc error into an integer register using a register
 * chosen by an offset.  Since the error injection register used is per core
 * (not per strand) no other strands should be active during this test.  This
 * is handled by calling pause_cpus() in the pre_test routine before this
 * function.
 *
 * Valid xorpat/eccmask values are bits[7:0].
 */
int
ni_inject_ireg_file(mdata_t *mdatap, uint64_t offset)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	enable;
	uint64_t	eccmask;
	int		ret;
	char		*fname = "ni_inject_ireg_file";

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

		ret = memtest_hv_inject_error("ni_inj_ireg_hvfile",
		    (void *)ni_inj_ireg_hvfile, paddr, enable, eccmask,
		    (offset * REG_ERR_STRIDE));
	} else {
		ret = ni_k_inject_ireg(mdatap, "ni_inj_ireg_file",
		    (void *)ni_inj_ireg_file, paddr, enable, eccmask,
		    (offset * IREG_STRIDE_SIZE));
	}

	DPRINTF(3, "%s: ret = 0x%x\n", fname, ret);
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
 * must be zero aligned.  The hypervisor routine determines the half word to
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
 * Therefore the asm routine write to/from DMMODE this flushes the buffers.
 *
 * NOTE: only correctable tag errors (LTC) are defined on NIAGARA, even
 *	 multi-bit tag errors will produce an LTC error.
 */
int
ni_inject_l2cache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		myid = getprocessorid();
	uint64_t	raddr = mdatap->m_raddr_c;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	l2cr_addr;
	uint64_t	l2cr_value;
	uint64_t	l2cr_saved;
	int		ret;
	char		*fname = "ni_inject_l2cache";

	DPRINTF(3, "%s: iocp=0x%p, raddr=0x%llx, paddr=0x%llx\n",
	    fname, iocp, raddr, paddr);

	/*
	 * Determine addr and read contents of the L2 control register.
	 */
	l2cr_addr = L2_CTL_REG + (paddr & 0xc0);
	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	l2cr_saved = l2cr_value;

	/*
	 * Set the L2$ into DM mode prior to the call to HV so the
	 * data/instructions can be installed into L2$ by this routine
	 * and the line changed to modified if req'd.
	 */
	l2cr_value |= L2CR_DMMODE;
	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *)hv_paddr_store64, l2cr_addr, l2cr_value, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 for DM "
		    "mode FAILED, ret=0x%x\n", fname, ret);
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
			    "ni_inj_l2cache_instr_tag",
			    (void *)ni_inj_l2cache_instr_tag,
			    paddr, IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), NULL);

			if (ret == -1) {
				DPRINTF(0, "%s: ni_inj_l2cache_instr_tag "
				    "FAILED, ret=0x%x\n", fname, ret);
				return (ret);
			}
		} else {
			/*
			 * Otherwise corrupt the l2cache instr or the instr ECC.
			 */
			ret = memtest_hv_inject_error(
			    "ni_inj_l2cache_instr_data",
			    (void *)ni_inj_l2cache_instr_data,
			    paddr, IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), NULL);

			if (ret == -1) {
				DPRINTF(0, "%s: ni_inj_l2cache_instr_data "
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

			ret = memtest_hv_inject_error("ni_inj_l2cache_tag",
			    (void *)ni_inj_l2cache_tag, paddr,
			    (uint64_t)IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), NULL);

			if (ret == -1) {
				DPRINTF(0, "%s: ni_inj_l2cache_tag FAILED, "
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
			ret = memtest_hv_inject_error("ni_inj_l2cache_data",
			    (void *)ni_inj_l2cache_data, paddr,
			    IOC_XORPAT(iocp),
			    (uint64_t)ni_debug_buf_pa, NULL);
#else	/* L2_DEBUG_BUFFER */

			ret = memtest_hv_inject_error("ni_inj_l2cache_data",
			    (void *)ni_inj_l2cache_data, paddr,
			    IOC_XORPAT(iocp),
			    (uint64_t)F_CHKBIT(iocp), NULL);

#endif	/* L2_DEBUG_BUFFER */

			if (ret == -1) {
				DPRINTF(0, "%s: ni_inj_l2cache_data FAILED, "
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
ni_inject_l2dir(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	enable;
	uint_t		data_flag = 1;
	int		ret;
	char		*fname = "ni_inject_l2dir";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

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
		 * Corrupt the l2cache directory parity using offset.
		 */
		ret = memtest_hv_inject_error("ni_inj_l2dir_phys",
		    (void *)ni_inj_l2dir_phys, offset, enable,
		    (uint64_t)ni_debug_buf_pa, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_l2dir_phys FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Corrupt the l2cache directory parity at physical address.
		 */
		ret = memtest_hv_inject_error("ni_inj_l2dir",
		    (void *)ni_inj_l2dir, paddr, enable,
		    (uint64_t)data_flag, (uint64_t)ni_debug_buf_pa);

		/*
		 * For instruction errors, bring instr(s) into the dir
		 * with corrupt parity.  Data was brought in by asm routine.
		 */
		mdatap->m_asmld(mdatap->m_kvaddr_a);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_l2dir FAILED, ret=0x%x\n",
			    fname, ret);
			return (ret);
		}
	}
	return (0);
}

/*
 * This routine inserts an error into the L2 cache data, the L2 cache tags,
 * or the ECC protecting one of the data or the tags in a location specified
 * by the byte offset in the ioc_addr member of the mdata struct.
 *
 * This routine is similar to the above ni_inject_l2cache() routine.
 */
int
ni_inject_l2phys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	int		ret;
	char		*fname = "ni_inject_l2phys";

	DPRINTF(3, "%s: iocp=0x%p, offset=0x%llx\n", fname, iocp,
	    offset);

	/*
	 * If the IOCTL specified the tag, corrupt the tag or the tag ECC.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		ret = memtest_hv_inject_error("ni_inj_l2phys_tag",
		    (void *)ni_inj_l2phys_tag, offset,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)ni_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_l2phys_tag "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Otherwise corrupt the l2cache data or the data ECC.
		 */
		ret = memtest_hv_inject_error("ni_inj_l2phys_data",
		    (void *)ni_inj_l2phys_data, offset,
		    IOC_XORPAT(iocp), (uint64_t)F_CHKBIT(iocp),
		    (uint64_t)ni_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_l2phys_data "
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
 * NOTE: this routine is called directly from the Niagara command list.
 */
int
ni_inject_l2_scrub(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset;
	uint64_t	l2cr_addr;
	uint64_t	l2cr_value;
	uint64_t	scrub_interval;
	uint_t		ret;
	char		*fname = "ni_inject_l2_scrub";

	if (ERR_MISC_ISPHYS(iocp->ioc_command)) {
		offset = iocp->ioc_addr;
	} else {
		offset = mdatap->m_paddr_c;
	}

	DPRINTF(3, "%s: address/offset=0x%llx\n", fname, offset);

	/*
	 * Determine the bank to set the scrubber running on (paddr<7:6>).
	 */
	l2cr_addr = L2_CTL_REG + (offset & 0xc0);
	l2cr_value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    l2cr_addr, NULL, NULL, NULL);
	DPRINTF(3, "%s: before setting L2CR at 0x%llx = 0x%llx\n", fname,
	    l2cr_addr, l2cr_value);

	/*
	 * Set the scrub interval to a low (default) value and enable
	 * the l2 cache scrubber (scrub interval is bits 14:3) after injection.
	 * MISC2 is used by this test as a custom scrub interval value.
	 */
	if (F_MISC2(iocp)) {
		scrub_interval = (iocp->ioc_misc2) & NI_L2_SCRUB_INTERVAL_MASK;
	} else {
		scrub_interval = NI_L2_SCRUB_INTERVAL_DFLT;
	}

	l2cr_value &= ~((uint64_t)NI_L2_SCRUB_INTERVAL_MASK);
	l2cr_value |= (L2_SCRUBENABLE | scrub_interval);
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
			DPRINTF(0, "%s: ni_inject_l2phys FAILED, ret = 0x%x\n",
			    fname, ret);
			return (ret);
		}
	} else {
		if ((ret = OP_INJECT_L2CACHE(mdatap)) == -1) {
			DPRINTF(0, "%s: ni_inject_l2cache FAILED, ret = 0x%x\n",
			    fname, ret);
			return (ret);
		}
	}

	DPRINTF(3, "%s: ni_inject_l2cache/phys ret = 0x%x\n", fname, ret);

	/*
	 * Wait a finite amount of time for the scrubber to find the error.
	 */
	DELAY(iocp->ioc_delay * MICROSEC);

	return (0);
}

/*
 * This routine inserts an error into the L2 cache VAUD or the VAUD parity
 * bits at a location determined by the physical address member of the mdata
 * structure.
 *
 * The PHYS version of the test expects a byte offset in the iocp->ioc_addr
 * struct member instead of an address.
 *
 * Because the VAD bits of all 12 ways are checked for each L2 cache access
 * these errors can be detected even without the explicit access.
 *
 * Valid xorpat values are combinations of:
 * 	Valid:   [23:12]
 * 	Vparity  [25]
 * 	Dirty:   [11:0]
 * 	Dparity: [24]
 * 	---
 * 	Used:    [23:12] (will not produce an error)
 * 	Alloc:   [11:0]
 * 	Aparity: [24]
 */
int
ni_inject_l2vad(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	VDflag = 0;
	int		ret;
	char		*fname = "ni_inject_l2vad";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

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
		 * Corrupt the l2cache VAD or the VAD parity.
		 */
		ret = memtest_hv_inject_error("ni_inj_l2vad_phys",
		    (void *)ni_inj_l2vad_phys, offset, IOC_XORPAT(iocp),
		    VDflag, (uint64_t)ni_debug_buf_pa);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_l2vad_phys "
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
		 * Corrupt the l2cache VAD or the VAD parity.
		 *
		 * Bring the instructions into the cache for the corruption.
		 */
		mdatap->m_asmld(mdatap->m_kvaddr_a);

		ret = memtest_hv_inject_error("ni_inj_l2vad_instr",
		    (void *)ni_inj_l2vad_instr, paddr,
		    IOC_XORPAT(iocp), VDflag, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_l2vad_instr "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else {
		/*
		 * Corrupt the l2cache VAD or the VAD parity.
		 */
		ret = memtest_hv_inject_error("ni_inj_l2vad",
		    (void *)ni_inj_l2vad, paddr,
		    IOC_XORPAT(iocp), VDflag, NULL);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_l2cache_vad "
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}
	return (0);
}

/*
 * This routine inserts a parity error into Modular Arithmetic memory (MA)
 * using the specified real address as the data/operands to be loaded into MA.
 *
 * NOTE: loads to MA do not generate an error.
 *
 * NOTE: the ni_inj_ma_memory() routine currently uses a value of 3 8-byte
 *	 words as the operand/result length with a spacing in memory of 4
 *	 8-byte words between them.
 */

int
ni_inject_ma_memory(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	operation;
	caddr_t		buffer;
	int		ret;
	char		*fname = "ni_inject_ma_memory";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx\n", fname, iocp, paddr);

	/*
	 * Determine the type of operation to use for error injection, note
	 * that a STORE will not inject an error according to PRM definition.
	 *
	 * XXX	find a way to allow the different types of operations
	 *	(such as mult) to be used for the injection. The access
	 *	field in the command is for the access NOT the injection.
	 */
	operation = NI_MA_OP_LOAD;

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
	ret = memtest_hv_inject_error("ni_inj_ma_memory",
	    (void *)ni_inj_ma_parity, paddr, operation, NULL, NULL);
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
 * Unlike previous UltraSPARC processors the Niagara memory test does not
 * need to calculate the ECC since a DRAM error injection register is provided.
 *
 * The DRAM uses QEC/OED ECC.  This means that any number of errors within
 * a single nibble are correctable, and any errors (up to 8) within two nibbles
 * are detectable as uncorrectable.
 *
 * Valid xorpat values are:
 * 	ECC mask: [15:0]
 */

int
ni_inject_memory(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	eccmask;
	uint64_t	offset;
	uint64_t	disabled_reg, disabled_val;
	int		ret;
	char		*fname = "ni_inject_memory";

	DPRINTF(3, "%s: iocp=0x%p, paddr=0x%llx xor=0x%x\n",
	    fname, iocp, paddr, IOC_XORPAT(iocp));

	/*
	 * Get the mask to use for the ECC bit corruption.
	 */
	eccmask = IOC_XORPAT(iocp);
	eccmask |= (NI_DRAM_INJECTION_ENABLE | NI_DRAM_SSHOT_ENABLE);

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode.
	 */
	if (F_INF_INJECT(iocp))
		eccmask &= ~((uint64_t)NI_DRAM_SSHOT_ENABLE);

	/*
	 * Check to see if the channel that would normally handle this
	 * paddr is in the disabled state.  If it is, the DRAM injection
	 * register to use must be changed in order to use the correct one.
	 *
	 * Normally the set of DRAM registers is dependent on paddr[7:6]
	 * but in one of the 2-channel modes the mapping relies only on bit 6.
	 * Addresses for disabled channels are handled as follows:
	 *
	 * paddr[7:6]	normal (4w ch)	disabled (2w ch)
	 * =============================================
	 *	0x00	0		-> 2
	 *	0x40	1		-> 3
	 *	0x80	2		-> 0
	 *	0xc0	3		-> 1
	 */
	offset = (paddr & 0xc0) << 6;
	disabled_reg = DRAM_CSR_BASE + DRAM_CHANNEL_DISABLED_REG + offset;
	disabled_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, disabled_reg, NULL, NULL, NULL);
	if (disabled_val & 0x1) {
		offset ^= (0x80 << 6);
		DPRINTF(2, "%s: channel %d disabled, using regs for channel "
		    "%d\n", fname, (paddr & 0xc0) >> 6, (offset >> 12));
	}

	/*
	 * Flush the L2$ to ensure data lookup does not hit prior to setting
	 * cache into DM mode.
	 *
	 * NOTE: this may flush existing errors from L2 producing write-back
	 *	 error(s).
	 */
	if (!F_FLUSH_DIS(iocp)) {
		(void) OP_FLUSHALL_L2_HVMODE(mdatap);
	}

#ifdef	MEMDEBUG_BUFFER
	ni_debug_init();
#endif	/* MEMDEBUG_BUFFER */

	ret = memtest_hv_inject_error("ni_inj_memory", (void *)ni_inj_memory,
	    paddr, eccmask, offset, (uint64_t)ni_debug_buf_pa);
#ifdef	MEMDEBUG_BUFFER
	ni_debug_dump();
#endif	/* MEMDEBUG_BUFFER */

	if (ret == -1) {
		DPRINTF(0, "%s: ni_inj_memory FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	DPRINTF(3, "%s: ret = 0x%x\n", fname, ret);
	return (0);
}

/*
 * This routine attempts to access an address outside of installed main
 * memory (DRAM) using the specified raddr in order to produce a memory
 * out-of-range error.
 *
 * The error will be indicated in the reply to the memory request
 * and will produce both a DBU DRAM error and a DAU L2$ error since
 * the L2$ line will be poisoned.
 *
 * The ideal way to call this routine is to know that start of a region
 * of unused addressable memory, use this and add an offset provided
 * by the mtst corruption offset parameter.
 *
 * NOTE: this routine is called directly from the Niagara command list
 *	 because it deals specifically with the Niagara architecture and
 *	 there is no value in creating a sun4v opsvec routine for it.
 *
 * XXX	Perhaps OBP can be asked for a mem address/region to use (Enxs had
 *	holes in the phys mem space).  I believe some code was added to
 *	the Panther EI for just this purpose.
 */
int
ni_inject_memory_range(mdata_t *mdatap)
{
	uint64_t	raddr = mdatap->m_raddr_a;
	uint64_t	paddr = mdatap->m_paddr_a;
	uint64_t	value;
	char		*fname = "ni_inject_memory_range";

	DPRINTF(3, "%s: raddr=0x%llx\n", fname, raddr);

	/*
	 * For now we will just set a high paddr value to test the error.
	 * [39:37] are always set to zero for CPU accesses to memory so
	 * we set [36:35].
	 */
	paddr = 0x1800000000 + paddr;

	value = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    paddr, (uint64_t)ni_debug_buf_pa, NULL, NULL);

	DPRINTF(3, "%s: ret = 0x%x\n", fname, value);
	return (0);
}

/*
 * This routine inserts an error into main memory (DRAM) at the specified
 * physical address to be discovered by the memory scrubber.
 *
 * NOTE: this routine is called directly from the Niagara command list.
 */
int
ni_inject_memory_scrub(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	freq_csr;
	uint64_t	scrub_csr;
	uint64_t	scrub_interval;
	uint64_t	offset;
	uint64_t	disabled_reg, disabled_val;
	uint_t		ret;
	char		*fname = "ni_inject_memory_scrub";

	DPRINTF(3, "%s: paddr=0x%llx\n", fname, paddr);

	/*
	 * Bank selection is based on bits[7:6] of the paddr, and DRAM CSR's
	 * are at intervals of 0x1000.
	 *
	 * Note that if the normal (4-way mode) channel is disabled,
	 * then the scrubber on the channel used must be set.
	 * See the comments in the ni_inject_memory() routine for
	 * more information on 2-channel mode settings.
	 */
	offset = ((paddr & 0xc0) << 6) + DRAM_CSR_BASE;
	disabled_reg = DRAM_CHANNEL_DISABLED_REG + offset;
	disabled_val = memtest_hv_util("hv_paddr_load64", (void *)
	    hv_paddr_load64, disabled_reg, NULL, NULL, NULL);

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
	 * Inject the DRAM error using standard Niagara function.
	 */
	if ((ret = ni_inject_memory(mdatap)) == -1) {
		DPRINTF(0, "%s: ni_inject_memory FAILED, ret = 0x%x\n",
		    fname, ret);
		return (ret);
	}

	DPRINTF(3, "%s: ni_inject_memory ret = 0x%x\n", fname, ret);

	/*
	 * Enable the correct banks DRAM scrubber(s) after the error injection,
	 * first the freqency then the enable.
	 */
	freq_csr = DRAM_SCRUB_FREQ_REG + offset;
	scrub_csr = DRAM_SCRUB_ENABLE_REG + offset;

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
	 * If in 2-channel mode also set the active channels regs,
	 * note that disabled channels must have register contents
	 * that match the corresponding active channel.
	 */
	if (disabled_val & 0x1) {
		freq_csr ^= (0x80 << 6);
		scrub_csr ^= (0x80 << 6);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, freq_csr, scrub_interval,
		    NULL, NULL)) == -1) {
			return (ret);
		}

		ret = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, freq_csr, NULL, NULL, NULL);
		DPRINTF(3, "%s: DRAM freq_csr = 0x%lx\n", fname, ret);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, scrub_csr, 1, NULL,
		    NULL)) == -1) {
			return (ret);
		}

		ret = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, scrub_csr, NULL, NULL, NULL);
		DPRINTF(3, "%s: DRAM scrub_csr = 0x%lx\n", fname, ret);
	}

	/*
	 * Wait a finite amount of time for the scrubber to find the error.
	 */
	DELAY(iocp->ioc_delay * MICROSEC);

	return (0);
}

/*
 * This routine inserts an error into the data TLB data or tag for either
 * privileged (kernel) or non-privileged (user) mode.
 *
 * The TLBs are protected by parity and the injection mode simply flips the
 * parity bit for the next (or all subsequent) entry load(s) for either the
 * tag or the data.
 *
 * NOTE: the TLB CAM (tag) errors can only be triggered via an ASI load access
 *	 to provide support for a possible SW scrubber.
 *
 * NOTE: the USER TLB tests which are triggered via a normal (userland) access
 *	 are defined as not implemented b/c they were found to be unreliable.
 */
int
ni_inject_tlb(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	vaddr;
	uint64_t	enable;
	uint64_t	entry;
	struct hat 	*sfmmup;
	uint32_t	ctxnum;
	uint32_t	uval32;
	tte_t		tte;
	int		i, count = 1;
	uint64_t	ret;
	char		*fname = "ni_inject_tlb";

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
		} else {
			enable |= REG_IMD_ENABLE;
		}
	} else {
		if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {
			enable |= REG_DMT_ENABLE;
		} else {
			enable |= REG_DMD_ENABLE;
		}
	}

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode.
	 */
	if (F_INF_INJECT(iocp))
		enable &= ~((uint64_t)REG_SSHOT_ENABLE);

	/*
	 * If the IOCTL has a misc1 argument, inject multiple dtlb errors.
	 * And if the command is STORE type then inject multiple errors in
	 * order to produce a DMSU (overwrite) error.
	 */
	if (F_MISC1(iocp)) {
		count = iocp->ioc_misc1;
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
	 * Use kernel tte building function to allow for a differnt
	 * method of injecting the TTE error.
	 *
	 * NOTE: when the debug level is set appropriately to print out
	 *	 the tte often there is no actual error due to print
	 *	 latency issues and a non quieced system interferring
	 *	 with the limited TLBs.
	 */

	/*
	 * If the IOCTL specified kernel corruption, use kvaddr translations.
	 * Note that the injection routines for iTLB and dTLB are separate.
	 */
	if (ERR_CLASS_ISITLB(iocp->ioc_command)) {

		/*
		 * Align the vaddr to a pagesize boundary (instr=64k).
		 */
		vaddr = (vaddr & (uint64_t)(~(MMU_PAGEOFFSET << 3)));

		if (memtest_get_tte(sfmmup, (caddr_t)vaddr, &tte) != 0) {
			DPRINTF(0, "%s: Could not get tte for vaddr=0x%llx\n",
			    fname, vaddr);
			return (-1);
		}

		DPRINTF(3, "%s: tte=0x%llx, paddr=0x%llx, vaddr=0x%llx\n",
		    fname, tte.ll, paddr, vaddr);

		ret = memtest_hv_inject_error("ni_inj_itlb",
		    (void *)ni_inj_itlb, paddr, vaddr, enable,
		    (uint64_t)ctxnum);

		if (ret == -1) {
			DPRINTF(0, "%s: ni_inj_itlb FAILED, ret=0x%x\n",
			    fname, ret);
			return ((int)ret);
		} else {
			DPRINTF(3, "%s: ni_inj_itlb returned value 0x%llx\n",
			    fname, ret);
		}
	} else {	/* dTLB test */
		/*
		 * Align the vaddr to a pagesize boundary (data=8k).
		 */
		vaddr = (vaddr & (uint64_t)(~MMU_PAGEOFFSET));

		if (memtest_get_tte(sfmmup, (caddr_t)vaddr, &tte) != 0) {
			DPRINTF(0, "%s: Could not get tte for vaddr=0x%llx\n",
			    fname, vaddr);
			return (-1);
		}

		DPRINTF(3, "%s: tte=0x%llx, paddr=0x%llx, vaddr=0x%llx\n",
		    fname, tte.ll, paddr, vaddr);

		/*
		 * The following is an alternate injection type, where a
		 * pre-built sun4v tte is inserted into the TLB.
		 *
		 *	if (tte.ll != NULL) {
		 *		ret = memtest_hv_inject_error("ni_inj_dtlb_v",
		 *				(void *)ni_inj_dtlb_v, paddr,
		 *				vaddr, enable,  tte.ll);
		 *	} else {
		 *		DPRINTF(0, "%s: memtest_get_tte() returned "
		 *				"a NULL tte\n");
		 *	}
		 */
		if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
			ret = memtest_hv_inject_error("ni_inj_dtlb_store",
			    (void *)ni_inj_dtlb_store, paddr, vaddr, enable,
			    (uint64_t)ctxnum);

				if (ret == -1) {
					DPRINTF(0, "%s: ni_inj_dtlb_store "
					    "FAILED, ret=0x%x\n", fname, ret);
					return ((int)ret);
				}
		} else {
			for (i = 0; i < count; i++) {
				ret = memtest_hv_inject_error("ni_inj_dtlb",
				    (void *)ni_inj_dtlb, paddr, vaddr, enable,
				    (uint64_t)ctxnum);

				if (ret == -1) {
					DPRINTF(0, "%s: ni_inj_dtlb FAILED, "
					    "ret=0x%x\n", fname, ret);
					return ((int)ret);
				} else {
					DPRINTF(3, "%s: ni_inj_dtlb returned "
					    "value 0x%llx\n", fname, ret);
				}
			}
		}
	}

	/*
	 * Invoke the error here if it's a tag error or if ASI access was
	 * specified, data errors will be invoked by the calling routine.
	 *
	 * TLB tag errors are only detected via ASI accesses.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {
		if (F_NOERR(iocp)) {
			DPRINTF(2, "%s: not invoking error\n", fname);
			return (0);
		}

		/*
		 * Access the tags like a scrubber using ASI_DTLB_TAG_READ_REG
		 * or ASI_ITLB_TAG_READ_REG for instruction access.
		 *
		 * A read value of -1 is considered to be a read access failure.
		 *
		 * VA:
		 * +-------+------------+-------+
		 * | Rsvd1 |  TLB Entry | Rsvd0 |
		 * +-------+------------+-------+
		 *   63:9	8:3	   2:0
		 *
		 * Returns:
		 * +-----+------+--------+------+-----------+---------+
		 * | PID | Real | Parity | Size | VA[55:13] | Context |
		 * +-----+------+--------+------+-----------+---------+
		 *  63:61   60	    59	   58:56	55:13	12:0
		 */
		if (ERR_CLASS_ISITLB(iocp->ioc_command)) {

			DPRINTF(3, "%s: invoking iTLB error via ASI_ITLB_TAG "
			    "(0x56) access\n", fname);

			for (entry = 0; entry < NI_ITLB_SIZE; entry += 8) {
				if ((ret = memtest_hv_util("hv_asi_load64",
				    (void *)hv_asi_load64, ASI_ITLB_TAG,
				    entry, NULL, NULL)) == -1) {
					DPRINTF(0, "%s: itag access FAILED, "
					    " ret=0x%x\n", fname, ret);
					return ((int)ret);
				}
			}
		} else {
			DPRINTF(3, "%s: invoking dTLB error via "
			    "ASI_DTLB_TAG (0x5e) access\n", fname);

			for (entry = 0; entry < NI_DTLB_SIZE; entry += 8) {
				if ((ret = memtest_hv_util("hv_asi_load64",
				    (void *)hv_asi_load64, ASI_DTLB_TAG,
				    entry, NULL, NULL)) == -1) {
					DPRINTF(0, "%s: dtag access FAILED, "
					    "ret=0x%x\n", fname, ret);
					return ((int)ret);
				}
			}
		}
	} else if (ERR_ACC_ISASI(iocp->ioc_command)) {
		if (F_NOERR(iocp)) {
			DPRINTF(2, "%s: not invoking error\n", fname);
			return (0);
		}

		/*
		 * D/I-TLB data errors can also be triggered by an ASI access.
		 */
		if (ERR_CLASS_ISITLB(iocp->ioc_command)) {

			DPRINTF(3, "%s: invoking iTLB error via "
			    "ASI_ITLB_DATA_ACCESS (0x55) access\n", fname);

			for (entry = 0; entry < NI_ITLB_SIZE; entry += 8) {
				if ((ret = memtest_hv_util("hv_asi_load64",
				    (void *)hv_asi_load64, ASI_ITLB_DATA_ACC,
				    entry, NULL, NULL)) == -1) {
					DPRINTF(0, "%s: data access FAILED, "
					    "ret=0x%x\n", fname, ret);
					return ((int)ret);
				}
			}
		} else {
			DPRINTF(3, "%s: invoking dTLB error via "
			    "ASI_DTLB_DATA_ACCESS (0x5d) access\n", fname);

			for (entry = 0; entry < NI_DTLB_SIZE; entry += 8) {
				if ((ret = memtest_hv_util("hv_asi_load64",
				    (void *)hv_asi_load64, ASI_DTLB_DATA_ACC,
				    entry, NULL, NULL)) == -1) {
					DPRINTF(0, "%s: data access FAILED, "
					    "ret=0x%x\n", fname, ret);
					return ((int)ret);
				}
			}
		}
	}

	return (0);
}

/*
 * This routine inserts an error into the data or instruction TLB data or
 * tag at an unknown (random) location.  This routine does not specify the
 * location for the injection in order to best simulate a runtime error.
 *
 * The TLBs are protected by parity and the injection mode simply flips the
 * parity bit for the next (or all subsequent) entry load(s) for either the
 * tag or the data.
 *
 * NOTE: Niagara-I TLB CAM (tag) errors can only be triggered via an ASI load
 *	 access to provide support for a possible SW scrubber.
 */
int
ni_inject_tlb_random(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	enable;
	char		*fname = "ni_inject_tlb_random";

	DPRINTF(3, "%s: iocp=0x%p\n", fname, iocp);

	/*
	 * Set appropriate fields to use in error injection register.
	 */
	enable = (REG_INJECTION_ENABLE | REG_SSHOT_ENABLE);

	/*
	 * If the IOCTL has the infinite injection flag set, use multi-shot
	 * injection mode.
	 */
	if (F_INF_INJECT(iocp))
		enable &= ~((uint64_t)REG_SSHOT_ENABLE);

	/*
	 * Inject error into either the iTLB or the dTLB.
	 */
	if (ERR_CLASS_ISITLB(iocp->ioc_command)) {
		if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {
			enable |= REG_IMT_ENABLE;
		} else {
			enable |= REG_IMD_ENABLE;
		}
	} else {
		if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {
			enable |= REG_DMT_ENABLE;
		} else {
			enable |= REG_DMD_ENABLE;
		}
	}

	(void) memtest_hv_inject_error("ni_inj_tlb_rand",
	    (void *)ni_inj_tlb_rand, enable, NULL, NULL, NULL);

	return (0);
}

/*
 * This routine generates specific JBus errors on Niagara.
 *
 * NOTE: this routine is called directly from the Niagara command list
 *	 because it deals specifically with the Niagara architecture and
 *	 there is no value in creating a sun4v opsvec routine for it.
 *
 * The Niagara physical address space is as follows:
 *	[63:40]	- unused
 *	[39]	- when set denotes IO space
 *	[38:37]	- always set to zero on CPU requests to memory
 *
 *	[39:32] - therefor determine the address space to access, for example:
 *		= 0x00-0x7f - DRAM
 *		= 0x80	    - JBus space 1
 *		= 0xc0-0xfe - JBus space 2
 *
 * The Niagara NC physical address space can also be viewed as:
 *	00.0000.0000 - 0f.ffff.ffff	- NC JBus range that aliases to memory
 *	10.0000.0000 - 1f.ffff.ffff	- NC JBus range that aliases to memory
 *	80.0000.0000 - 80.00ff.ffff	- JBI internal CSRs (no JBI trans.)
 *	80.1000.0000 - 80.ffff.ffff	- DMA loopback range (Tom NC range)
 *	81.0000.0000 - 80.0fff.ffff	- Niagara internal CSRs (no NC range)
 *
 *	80.0e00.0000 - 80.0e7f.ffff	- JBus AID = 0x1c
 *	c0.0000.0000 - cf.ffff.ffff	- "
 *	80.0e80.0000 - 80.0eff.ffff	- JBus AID = 0x1d
 *	d0.0000.0000 - df.ffff.ffff	- "
 *	80.0f00.0000 - 80.0f7f.ffff	- JBus AID = 0x1e
 *	e0.0000.0000 - ef.ffff.ffff	- "
 *	80.0f80.0000 - 80.0fff.ffff	- JBus AID = 0x1f
 *	f0.0000.0000 - fe.ffff.ffff	- "
 *
 * The JBus physical address space is 43-bit so addresses are aliased before
 * being sent out on the JBus (the JBI_MEMSIZE reg operates post-aliasing).
 *
 * NOTE: the ASI_REAL* ASIs never bypass the TLB so they require an RA->PA
 *	 translation to be available when used from either SV or HV code.
 *
 * NOTE: the ASI_REAL* ASIs do not modify VA[39] and so rely on the data
 *	 to determine if it is to be an actual MEM or an IO access.
 */
int
ni_k_bus_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	jbi_reg_val, jbi_sig_val, sparc_een_val;
	uint64_t	ioaddr, temp_val;
	uint_t		rxtx;
	uint_t		cyclecount = 0;
	int		i, ret = 0;
	char		*fname = "ni_k_bus_err";

	system_info_t	*sip = mdatap->m_sip;
	uint_t		myid = getprocessorid();
	uint_t		otherid;
	cpu_t		*cp;
	uint64_t	offset;

	/*
	 * If the IOCTL has a misc1 argument, inject error on transmit.
	 */
	if (F_MISC1(iocp)) {
		rxtx = JERR_INJ_OUTPUT;
	} else {
		rxtx = JERR_INJ_INPUT;
	}

	/*
	 * If the IOCTL has a misc2 argument, use an injection cycle delay.
	 */
	if (F_MISC2(iocp)) {
		cyclecount = (iocp->ioc_misc2) & 0xffffff;
	}

	/*
	 * If NOERR set disable the signals/traps for the JBI errs, so logs
	 * must be checked manually for the recorded error(s).
	 */
	if (F_NOERR(iocp)) {
		jbi_sig_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (uint64_t)JBI_SIG_ENB,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (uint64_t)JBI_SIG_ENB, 0,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_SIG_ENB "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/*
		 * Also clear the SPARC EEN register to disable NCU errors.
		 */
		sparc_een_val = memtest_hv_util("hv_asi_load64",
		    (void *)hv_asi_load64, ASI_ERROR_EN, 0,
		    (uint64_t)ni_debug_buf_pa, NULL);

		if ((ret = memtest_hv_util("hv_asi_store64",
		    (void *)hv_asi_store64, ASI_ERROR_EN, 0, 0,
		    (uint64_t)ni_debug_buf_pa)) == -1) {
			DPRINTF(0, "%s: hv_asi_store64 for ASI_ERROR_EN "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/*
		 * Also clear the L2 EEN registers to block the DAU and LDWU
		 * errors that propagate from some of the STORE errors
		 * (DPARS and NEMS) as part of the error handling.
		 */
		for (i = 0, offset = 0; i < NO_L2_BANKS; i++,
		    offset += NI_L2_BANK_OFFSET) {
			ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64,
			    (L2_EEN_BASE + offset), 0x0,
			    (uint64_t)ni_debug_buf_pa, NULL);
		}
		cmn_err(CE_NOTE, "%s: memtest driver cleared L2 EEN "
		    "registers because the -n flag used.  They "
		    "will be set again the next time the injector "
		    "is run without the -N flag.", fname);
	}

	/*
	 * Note that all of these tests are bound to CPUs
	 * such that the data buffer is in local memory.
	 */
	switch (iocp->ioc_command) {
	case NI_HD_APAR:
	case NI_HI_APAR:
		/*
		 * Address parity error(s).
		 *
		 * The JBus Error Injection register is used to inject parity
		 * errors on incoming (rx) or outgoing (tx) transactions.
		 *
		 * MISC1 is used to determine the error direction (rx/tx).
		 *
		 * MISC2 is used to set a count of valid cycles to wait before
		 * injection (default = 0).
		 */

		/*
		 * Set an IO address within the DMA loopback range.
		 *
		 * NOTE: using an ioaddr = NI_IO_BIT | paddr; produced a
		 *	 READ_TO and no APAR error (using load access).
		 */
		ioaddr = NI_DMA_LB_BASE + paddr;

		/* Arm the error inject register */
		jbi_reg_val = rxtx | JERR_INJ_ATYPE | IOC_XORPAT(iocp)
		    | cyclecount;
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_ERR_INJECT, jbi_reg_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_ERR_INJECT "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/* Send an IO transaction out via HV access */
		if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
			ret = memtest_hv_util("hv_paddr_store32",
			    (void *)hv_paddr_store32, ioaddr, 0,
			    (uint64_t)ni_debug_buf_pa, NULL);
		} else { /* LOAD */
			temp_val = memtest_hv_util("hv_paddr_load16",
			    (void *)hv_paddr_load16, ioaddr,
			    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		}

		/* RESULTS */
		/*
		 * Using 'mtst -n' the correct bit is set in the OVF reg.
		 * There is no mecahanism yet to use the STORE.
		 */
		ret = 0;
		break;
	case NI_HD_DPAR:
	case NI_HI_DPAR:
	case NI_HD_DPARS:
		/*
		 * Data parity error(s).
		 *
		 * The method used is similar to the above address parity case.
		 */

		/*
		 * Set an IO address within the DMA loopback range
		 *
		 * NOTE: using an ioaddr = NI_IO_BIT | paddr; produced a
		 *	 READ_TO and no DPAR error (using load access).
		 */
		ioaddr = NI_DMA_LB_BASE + paddr;

		/* Read the data at the chosen location (for store) */
		temp_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, ioaddr,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		/* Arm the error inject register */
		jbi_reg_val = rxtx | IOC_XORPAT(iocp) | cyclecount;
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_ERR_INJECT, jbi_reg_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_ERR_INJECT "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/* Send an IO transaction out via HV access */
		if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
			ret = memtest_hv_util("hv_paddr_store32",
			    (void *)hv_paddr_store32, ioaddr, temp_val,
			    (uint64_t)ni_debug_buf_pa, NULL);
		} else { /* LOAD */
			temp_val = memtest_hv_util("hv_paddr_load16",
			    (void *)hv_paddr_load16, ioaddr,
			    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		}

		/* RESULTS */
		/*
		 * Using 'mtst -n' both the LOAD and STORE tests set the
		 * correct bit in the OVF register (b/c an error was already
		 * logged in the LOG reg).
		 *
		 * Even without any transactions we can get a DPAR RD error,
		 * I guess the JBus is doing some work w/o us.
		 *
		 * Also errors can propagate causing L2 LDWU errors, but only
		 * when using the STORE version of the test.
		 */
		ret = 0;
		break;
	case NI_HD_L2TO:
		/*
		 * L2 interface flow control timeout error.
		 *
		 * All JBus transactions go through L2, the L2 Interface
		 * Timeout register is set to a low value to generate
		 * this error on an otherwise normal JBus transaction.
		 */
		jbi_reg_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, JBI_L2_TIMEOUT,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		/* Set timeout to very low value */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_L2_TIMEOUT, 0x1,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_L2_TIMEOUT "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/* Send an IO transaction out via HV access */
		ioaddr = NI_IO_BIT | paddr;
		temp_val = memtest_hv_util("hv_paddr_load16",
		    (void *)hv_paddr_load16, ioaddr,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		/* Restore the JBI_L2_TIMEOUT register */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_L2_TIMEOUT, jbi_reg_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_L2_TIMEOUT "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/* RESULTS */
		/*
		 * Using 'mtst -n' the correct error is logged in the LOG or
		 * OVF regs depending on the contents of the LOG reg (priority).
		 *
		 * With no flags system resets since FATAL (good).
		 *
		 * The TO tests can run into a race condition where the data
		 * can be sent over the bus before the new TO value takes
		 * effect.  So delays may need to be used to make the tests
		 * more reliable.
		 */
		ret = 0;
		break;
	case NI_HD_ARBTO:
		/*
		 * Arbitration timeout error.
		 *
		 * Detected when Niagara has a trans that it wants to issue
		 * but it can't be issued before the time in the Arb Timeout
		 * register expires.
		 *
		 * To generate this error the Arb Timeout register is set to
		 * a low value then otherwise normal JBus trans's issued.
		 */
		jbi_reg_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, JBI_ARB_TIMEOUT,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		/* Set timeout to very low value */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_ARB_TIMEOUT, 0x1,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_ARB_TIMEOUT "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/* Send some transactions out via HV access */
		ioaddr = NI_IO_BIT | paddr;
		temp_val = memtest_hv_util("hv_paddr_load32",
		    (void *)hv_paddr_load32, ioaddr,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		ret = memtest_hv_util("hv_paddr_store32",
		    (void *)hv_paddr_store32, ioaddr, temp_val,
		    (uint64_t)ni_debug_buf_pa, NULL);
		DELAY(1 * MICROSEC);
		temp_val = memtest_hv_util("hv_paddr_load32",
		    (void *)hv_paddr_load32, ioaddr,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		ret = memtest_hv_util("hv_paddr_store32",
		    (void *)hv_paddr_store32, ioaddr, temp_val,
		    (uint64_t)ni_debug_buf_pa, NULL);

		/* Restore the JBI_ARB_TIMEOUT register */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_ARB_TIMEOUT, jbi_reg_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_ARB_TIMEOUT "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/* RESULTS */
		/*
		 * Using 'mtst -N -n ' on a clean boot, get nothing reported
		 * b/c it the system does not set the ARB log bits by default.
		 *
		 * Using just 'mtst -n' the error is logged in the LOG register
		 * (because it is a high priority error type).
		 *
		 * With no flags system resets since FATAL.
		 */
		ret = 0;
		break;
	case NI_HD_RTO:
		/*
		 * Transaction timeout error (read).
		 *
		 * Detected when a NCRD transaction is outstanding while the
		 * transaction timer wraps twice.
		 *
		 * Method 1:
		 * Generate a read to the address range 0x80_01xx_xxxx -
		 * 0x80_0dxx_xxxx. This will be an NCRD on JBUS to the 8MB
		 * NC Space of JBUS Agents 0x2-0x1b, which will be ignored and
		 * eventually generate a READ_TO w/o a related data return
		 * causing a subsequent UNEXP_DR.  This is a platform specific
		 * solution however and may not work for future Niagara systems.
		 *
		 * Method 2:
		 * To generate the rollover value of the transaction timer is
		 * set to a low value in the Transaction Timeout Value Register
		 * then an otherwise normal JBus trans issued.  This will also
		 * cause an UNEXP_DR when the data is returned (late) after
		 * the TO.  ALSO when errors are enabled the code which restores
		 * the TO is not always run which results in infinite RTO errs.
		 */
		if (!F_MISC2(iocp)) {	/* use Method 1 if MISC2 not set */
			/*
			 * Send the NCRD transaction to nonexistant NC space,
			 * but ensure the value is within the targets 8MB
			 * local NC range.
			 */
			ioaddr = 0x800c000000 | (paddr & 0xffff8);
			temp_val = memtest_hv_util("hv_paddr_load32",
			    (void *)hv_paddr_load32, ioaddr,
			    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		} else { 		/* otherwise use Method 2 */
			jbi_reg_val = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, JBI_TRANS_TIMEOUT,
			    (uint64_t)ni_debug_buf_pa, NULL, NULL);

			/* Set timeout to very low value */
			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, JBI_TRANS_TIMEOUT, 2,
			    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 for "
				    "JBI_TRANS_TIMEOUT FAILED, ret=0x%x\n",
				    fname, ret);
			}

			/* Send the NCRD transaction out via a normal HV load */
			ioaddr = NI_IO_BIT | paddr;
			temp_val = memtest_hv_util("hv_paddr_load32",
			    (void *)hv_paddr_load32, ioaddr,
			    (uint64_t)ni_debug_buf_pa, NULL, NULL);

			/* Restore the JBI_TRANS_TIMEOUT register */
			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, JBI_TRANS_TIMEOUT,
			    jbi_reg_val, (uint64_t)ni_debug_buf_pa, NULL))
			    == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 for "
				    "JBI_TRANS_TIMEOUT FAILED, ret=0x%x\n",
				    fname, ret);
			}
		}

		/* RESULTS */
		/*
		 * When 'mtst -n -N' used for the test:
		 * Method 1: The LOG/OVF reg has READ_TO set.
		 * Method 2: The OVF reg has READ_TO and/or UNEXP_DR set.
		 *
		 * This is b/c when the READ_TO times out the data is still
		 * returned in the normal timeframe and is considered UNEXP_DR.
		 *
		 * Non-res err(s) are queued to Solaris when errors are enabled
		 * which looks good.  But the TO is never restored when the
		 * errors start piling up so a reset is required.
		 */
		ret = 0;
		break;
	case NI_HD_INTRTO:
		/*
		 * Interrupt ACK timeout error.
		 *
		 * Detected when an INTR has been sent, but not INTACKed, while
		 * INTACK Timer wraps twice.  Note that the timer gets reset
		 * only on the first INTR, and not on later retries of the
		 * same INTR.
		 *
		 * Method 1:
		 * The TO value of the timer in the INTACK Timeout Value
		 * Register is set to a very low value and an INTR issued.
		 * The TO value is then restored.
		 *
		 * Method 2:
		 * Set J_INT_BUSY.BUSY = 1, (bit 5) then issue an interrupt.
		 * This will eventually cause a timeout with the next interrupt
		 * issued, regardless of the setting of JBI_INTR_TIMEOUT.
		 * J_INT_BUSY must be cleared again before the end of the test.
		 *
		 * NOTE: if the Interrupt Management Registers for Device ID 1
		 *	 are not setup interrupt errors can be masked even
		 *	 with LOG/SIG enabled. By default INT_CTL will mask
		 *	 error interrupts. It is the responsibility of software
		 *	 to clear INT_CTL after every JBUS error interrupt.
		 *	 All other fields of J_INT_BUSY are RO.
		 *
		 * NOTE: multiple errors can be merged into a single
		 *	 asynchronous interrupt if they get logged around the
		 *	 same time.
		 *
		 * Can send an interrupt over JBus via the following:
		 *	1) use INT_VEC_DIS (0x98.0000.0000 + 0x0800) to send
		 *	   a reset or other (in ASI_SWVR_INTR_RECEIVE 0x72)
		 *	   interrupt across JBus.
		 *	2) perform an xcall by writing the Interrupt Vector
		 *	   Dispatch Register (ASI: 0x73).
		 *	3) perform an xcall via kernel routines.
		 */
		if (F_MISC2(iocp)) {	/* use Method 2 */
			/*
			 * Choose a target virtual core which is not ourselves.
			 */
			for (i = 0; i < sip->s_maxcpuid; i++) {
				mutex_enter(&cpu_lock);
				cp = cpu_get(i);
				if ((cp != NULL) &&
				    (cp->cpu_id != myid)) {
					otherid = cp->cpu_id;
					mutex_exit(&cpu_lock);
					break;
				}
				mutex_exit(&cpu_lock);
			}

			/*
			 * Set the J_INT_BUSY bit of the cpu which was chosen
			 * above to force a TO (bit 5).
			 */
			temp_val = J_INT_BUSY + (otherid * 8);
			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, temp_val, 0x20,
			    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 for J_INT_BUSY"
				    " FAILED, ret=0x%x\n", fname, ret);
			}

			/*
			 * Send an interrupt to the same core which we set the
			 * BUSY bit on (cannot be the vcore we are running on).
			 */
			xc_one(otherid, (xcfunc_t *)ni_jbus_intr_xcfunc,
			    (uint64_t)mdatap, (uint64_t)&ret);

			/*
			 * Clear the J_INT_BUSY bit (bit 5).
			 */
			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, temp_val, 0x0,
			    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 for J_INT_BUSY"
				    "FAILED, ret=0x%x\n", fname, ret);
			}
		} else {	/* otherwise use Method 1 */
			jbi_reg_val = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, JBI_INTR_TIMEOUT,
			    (uint64_t)ni_debug_buf_pa, NULL, NULL);

			/* Set timeout to very low value */
			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, JBI_INTR_TIMEOUT, 0x1,
			    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 for "
				    "JBI_INTR_TIMEOUT FAILED, ret=0x%x\n",
				    fname, ret);
			}
			/*
			 * Wait a little for register settings to be updated.
			 */
			DELAY(500000);

			/*
			 * Send interrupt(s).
			 */
			if (ERR_MISC_ISSTORM(iocp->ioc_command)) {
				(void) memtest_xc_cpus(mdatap,
				    ni_jbus_intr_xcfunc,
				    "jbus_test_intr_stub");
			} else {
				for (i = 0; i < sip->s_maxcpuid; i++) {
					mutex_enter(&cpu_lock);
					cp = cpu_get(i);
					if ((cp != NULL) &&
					    (cp->cpu_id != myid)) {
						xc_one(cp->cpu_id,
						    (xcfunc_t *)
						    ni_jbus_intr_xcfunc,
						    (uint64_t)
						    mdatap, (uint64_t)&ret);
						mutex_exit(&cpu_lock);
						break;
					}
					mutex_exit(&cpu_lock);
				}
			}

			/*
			 * Restore JBI_INTR_TIMEOUT reg.
			 */
			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, JBI_INTR_TIMEOUT,
			    jbi_reg_val, (uint64_t)ni_debug_buf_pa,
			    NULL)) == -1) {
				DPRINTF(0, "%s: hv_paddr_store64 for "
				    "JBI_INTR_TIMEOUT FAILED, ret=0x%x\n",
				    fname, ret);
			}
		}

		/* RESULTS */
		/*
		 * Method 1:
		 * No console output when using the memtest_xc_cpus call or not.
		 * The JBI_LOG/OVF does have correct bit set.  The disabling
		 * of interrupts in the error handler and unfinished SW
		 * may be to blame.  CBG was informed.
		 *
		 * This test suffers the same problem as the other TO tests,
		 * the TO reg may be taking a long time to update.  Introduced
		 * a delay which now produces multiple errors since the low
		 * TO value is held for a long time.  ALSO could try reading
		 * the TO reg again to ensure it updates, but this will take
		 * so long that a delay is just as useful.  Otherwise
		 * re-implement as an HV asm routine.
		 *
		 * Method 2:
		 * No error logged at all.
		 */
		ret = 0;
		break;
	case NI_HD_UMS:
		/*
		 * Unmapped target store error.  Detected on:
		 *	1) non-cacheable write to a non-existent JBus module.
		 *
		 * JBI uses the PORT_PRES field [50:44] in the JBI_CONFIG1
		 * register to determine which modules are mapped.
		 *
		 * PORT_PRES[0] set, transactions to module 0x0 are allowed.
		 * PORT_PRES[1] set, transactions to module 0x1 are allowed.
		 * PORT_PRES[4] set, transactions to modules 0x1C and 0x1D
		 *			are allowed.
		 * PORT_PRES[5] set, transactions to modules 0x1E and 0x1F
		 *			are allowed.
		 */
		if ((jbi_reg_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, JBI_CONFIG1,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_load64 for JBI_CONFIG1 "
			    "FAILED, ret=0x%x\n", fname, jbi_reg_val);
		}

		DPRINTF(2, "%s: JBI_CONFIG1 val = 0x%lx at address 0x%lx\n",
		    fname, jbi_reg_val, JBI_CONFIG1);

		temp_val = jbi_reg_val  & JBI_ERR_CFG_PPRES;
		DPRINTF(3, "%s: PORT_PRES field = 0x%x\n", fname, temp_val);

		/* clear the PORT_PRES field in JBI_CONFIG1 reg */
		temp_val &= ~JBI_ERR_CFG_PPRES;
		ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_CONFIG1,
		    temp_val, (uint64_t)ni_debug_buf_pa, NULL);

		/* Perform an IO store access to JBus AID 0x1d via HV */
		ioaddr = 0xd000001000;
		if ((ret = memtest_hv_util("hv_paddr_store16",
		    (void *)hv_paddr_store16, ioaddr, 0,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store16 at 0x%lx "
			    "FAILED, ret=0x%x\n", fname, ioaddr, ret);
		}

		/* Restore the PORT_PRES field in JBI_CONFIG1 reg */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_CONFIG1, jbi_reg_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_CONFIG1 "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/* RESULTS */
		/*
		 * Using 'mtst -N -n' the correct bit gets set in the OVF reg.
		 * In fact running with no flags also sets the correct register
		 * in a LOG reg.  But there is never any console output or
		 * a trap.
		 */
		ret = 0;
		break;
	case NI_HD_NEMS:
		/*
		 * Non-existent memory error (write).
		 *
		 * Detected on:
		 *	1) receive of a write transaction that is out
		 *	   of the range of installed memory, or to any of
		 *	   Niagara's (non-aliased) NC (Non-Coherent) spaces.
		 *	2) a coherent write directed to Niagara's aliased
		 *	   NC space (Niagara has an 8MB NC range).
		 *	3) a non-coherent write directed to Niagara's coherent
		 *	   memory range.
		 *
		 * NOTE: the JBI spec has this to say:
		 *	 "it is possible for Niagara to be the source of this
		 *	  error, if an outgoing NCWR transaction has an address
		 *	  in the DMA-loopback range that is greater than (after
		 *	  re-aliasing) allowed by the MEMSIZE register."
		 */

		/* Method using DMA-loopback range */
		jbi_reg_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, JBI_MEMSIZE,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		/* Set MEMSIZE register to minimum value (1G) */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_MEMSIZE,
		    (uint64_t)NI_MEMSIZE_MIN, (uint64_t)ni_debug_buf_pa,
		    NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_MEMSIZE "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/* Build paddr in DMA-loopback range larger than MEMSIZE */
		ioaddr = NI_DMA_LB_BASE + NI_MEMSIZE_MIN;

		DPRINTF(2, "%s: sending NCWR to paddr 0x%lx", fname, ioaddr);

		/* Send the NCWR transaction out using a normal HV store */
		if ((ret = memtest_hv_util("hv_paddr_store32",
		    (void *)hv_paddr_store32, ioaddr, 0,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store32 at 0x%lx "
			    "FAILED, ret=0x%x\n", fname, ioaddr, ret);
		}

		/* Restore the MEMSIZE register */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_MEMSIZE, jbi_reg_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_MEMSIZE "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/* RESULTS */
		/*
		 * Must use 'mtst -n' or we get a DAU error which then wedges
		 * the system.  Hopefully just a SW handler issue.  The JBI
		 * regs must of course be read out after the test run.
		 *
		 * Do not use the paddr in the offset otherwise no error gets
		 * logged at all.
		 *
		 * New development - NO console output is (error trap) is
		 * generated, but the correct error bit is set in LOG even
		 * when no -n flag is used (yes the SIG was enabled).
		 *
		 * Correct bit gets set in LOG/OVF reg.
		 */
		ret = 0;
		break;
	case NI_HD_NEMR:
		/*
		 * Non-existent memory error (read).
		 *
		 * Detected on:
		 *	1) receive of a read transaction that is out of
		 *	   the range of installed memory, or to any of
		 *	   Niagara's (non-aliased) NC spaces.
		 *	2) a coherent read is directed to Niagara's aliased
		 *	   NC space.
		 *	3) a non-coherent read directed to Niagara's coherent
		 *	   memory range.
		 *
		 * NOTE: the JBI spec has this to say:
		 *	 "it is possible for Niagara to be the source of this
		 *	  error, if an outgoing NCRD transaction has an address
		 *	  in the DMA-loopback range that is greater than (after
		 *	  re-aliasing) allowed by the MEMSIZE register."
		 */

		/* Method using DMA-loopback range */
		jbi_reg_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (uint64_t)JBI_MEMSIZE,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		/* Set MEMSIZE register to minimum value (1G) */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, (uint64_t)JBI_MEMSIZE,
		    (uint64_t)NI_MEMSIZE_MIN, (uint64_t)ni_debug_buf_pa,
		    NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for "
			    "JBI_MEMSIZE FAILED, ret=0x%x\n", fname, ret);
		}

		/* Build paddr in DMA-loopback range larger than MEMSIZE */
		ioaddr = NI_DMA_LB_BASE + NI_MEMSIZE_MIN;

		DPRINTF(2, "%s: sending NCRD to paddr 0x%lx", fname, ioaddr);

		/* Send the NCRD transaction out using a normal HV load */
		temp_val = memtest_hv_util("hv_paddr_load32",
		    (void *)hv_paddr_load32, ioaddr,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		/* Restore the MEMSIZE register */
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_MEMSIZE, jbi_reg_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_MEMSIZE "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/* RESULTS */
		/*
		 * Using 'mtst -n' the correct bit gets set in LOG/OVF reg.
		 *
		 * With errs enabled error output goes to console (if DEBUG
		 * enabled as well), shows the correct bit set in OVF (all the
		 * JBI regs are output) and a UE err is placed on NR queue.
		 * Then a watchdog panic of course (since DEBUG enabled).
		 */
		ret = 0;
		break;
	case NI_CLR_JBI_LOG:
		jbi_reg_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, JBI_ERR_LOG,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_ERR_LOG, jbi_reg_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_ERR_LOG "
			    "clear FAILED, ret=0x%x\n", fname, ret);
		}

		jbi_reg_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, JBI_ERR_OVF,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_ERR_OVF, jbi_reg_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_ERR_OVF "
			    "clear FAILED, ret=0x%x\n", fname, ret);
		}
		ret = 0;
		break;
	case NI_PRINT_JBI:
		if ((ret = memtest_hv_util("ni_print_jbi_regs",
		    (void *)ni_print_jbi_regs, (uint64_t)ni_debug_buf_pa,
		    NULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: trap to ni_print_jbi_regs "
			    "FAILED, ret=0x%x\n", fname, ret);
		}

		/*
		 * The hyperpriv routine placed the values into the debug
		 * buffer (and also printed them to the HV_UART).  Now we
		 * print them to the standard console from the buffer.
		 */
		DPRINTF(0, "%s: current state of select JBI regs:\n", fname);
		DPRINTF(0, "\n\tJBI_ERR_CONFIG\t0x%08x.%08x\n"
		    "\tJBI_ERR_LOG\t0x%08x.%08x\n"
		    "\tJBI_ERR_OVF\t0x%08x.%08x\n"
		    "\tJBI_ERR_ENB\t0x%08x.%08x\n"
		    "\tJBI_SIG_ENB\t0x%08x.%08x\n"
		    "\tJBI_LOG_ADDR\t0x%08x.%08x\n",
		    PRTF_64_TO_32(ni_debug_buf_va[0]),
		    PRTF_64_TO_32(ni_debug_buf_va[1]),
		    PRTF_64_TO_32(ni_debug_buf_va[2]),
		    PRTF_64_TO_32(ni_debug_buf_va[3]),
		    PRTF_64_TO_32(ni_debug_buf_va[4]),
		    PRTF_64_TO_32(ni_debug_buf_va[5]));
		DPRINTF(0, "\n\tJBI_LOG_DATA0\t0x%08x.%08x\n"
		    "\tJBI_LOG_DATA1\t0x%08x.%08x\n"
		    "\tJBI_LOG_CTRL\t0x%08x.%08x\n"
		    "\tJBI_LOG_PAR\t0x%08x.%08x\n"
		    "\tJBI_LOG_NACK\t0x%08x.%08x\n"
		    "\tJBI_LOG_ARB\t0x%08x.%08x\n"
		    "\tJBI_MEMSIZE\t0x%08x.%08x\n\n",
		    PRTF_64_TO_32(ni_debug_buf_va[6]),
		    PRTF_64_TO_32(ni_debug_buf_va[7]),
		    PRTF_64_TO_32(ni_debug_buf_va[8]),
		    PRTF_64_TO_32(ni_debug_buf_va[9]),
		    PRTF_64_TO_32(ni_debug_buf_va[10]),
		    PRTF_64_TO_32(ni_debug_buf_va[11]),
		    PRTF_64_TO_32(ni_debug_buf_va[12]));
		ret = 0;
		break;
	default:
		DPRINTF(0, "%s: unsupported command 0x%llx\n",
		    fname, iocp->ioc_command);
		ret = ENOTSUP;
		break;
	}

	/*
	 * Restore the JBI_SIG_ENB and ERROR_EN registers.
	 */
	if (F_NOERR(iocp)) {
		if ((temp_val = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_SIG_ENB, jbi_sig_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for JBI_SIG_ENB "
			    "FAILED, ret=0x%x\n", fname, temp_val);
		}

		if ((temp_val = memtest_hv_util("hv_asi_store64",
		    (void *)hv_asi_store64, ASI_ERROR_EN, 0, sparc_een_val,
		    (uint64_t)ni_debug_buf_pa)) == -1) {
			DPRINTF(0, "%s: hv_asi_store64 for ASI_ERROR_EN "
			    "FAILED, ret=0x%x\n", fname, temp_val);
		}
	}

	return (ret);
}

/*
 * This routine generates SSI (BootROM interface) errors on Niagara.
 *
 * Currently the only supported error type is the interface timeout error.
 *
 * Niagara can address SSI devices by using the following range:
 *	ff.0000.0000 - ff.efff.ffff	- SSI internal registers
 *	ff.f000.0000 - ff.ffff.ffff	- SSI addressable locations (BootROM)
 */
int
ni_k_ssi_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	ssi_sav_val, ssi_reg_val;
	uint64_t	ioaddr, temp_reg, temp_val;
	int		ret = 0;
	char		*fname = "ni_k_ssi_err";

	/*
	 * Truncate the offset to ensure a valid address is used.
	 */
	offset &= 0xfff8;

	/*
	 * Save the contents of the SSI config register.
	 * (a value of -1 is considered to be a failure).
	 */
	if ((ssi_sav_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, SSI_TIMEOUT,
	    (uint64_t)ni_debug_buf_pa, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_load64 for SSI_TIMEOUT "
		    "FAILED, ret=0x%x\n", fname, ssi_sav_val);
		return (ssi_sav_val);
	}

	/*
	 * If NOERR clear the enable bit the for the SSI errs, so logs
	 * must be checked manually for the recorded error(s).
	 */
	if (F_NOERR(iocp)) {

		temp_val = ssi_sav_val & ~(NI_SSI_ERR_CFG_ERREN);

		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, SSI_TIMEOUT, temp_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for "
			    "SSI_TIMEOUT FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	}

	switch (iocp->ioc_command) {
	case NI_HD_SSITO:
	case NI_HD_SSITOS:
		/*
		 * To generate an error the value of the SSI timer is
		 * set to a low value in the SSI Timeout Register
		 * then an otherwise normal transaction issued.
		 */
		ssi_reg_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, SSI_TIMEOUT,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		/* Choose an address within the SSI range */
		ioaddr = NI_SSI_BASE + NI_BOOT_BASE + offset;

		/* Read and save the data at that location for STORE */
		temp_val = memtest_hv_util("hv_paddr_load32",
		    (void *)hv_paddr_load32, ioaddr,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);

		/* Set SSI timeout to very low value */
		temp_reg = (ssi_reg_val & ~NI_SSI_TOMASK) + 1;
		if ((ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, SSI_TIMEOUT, temp_reg,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for "
			    "SSI_TIMEOUT FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		/* Send an SSI IO transaction out via HV access */
		DELAY(1 * MICROSEC);

		if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
			temp_val = memtest_hv_util("hv_paddr_store32",
			    (void *)hv_paddr_store32, ioaddr, temp_val,
			    (uint64_t)ni_debug_buf_pa, NULL);
		} else { /* LOAD */
			temp_val = memtest_hv_util("hv_paddr_load16",
			    (void *)hv_paddr_load16, ioaddr,
			    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		}

		/* Restore the SSI_TIMEOUT timeout separate from enable */
		if ((ret = memtest_hv_util("hv_paddr_store64", (void *)
		    hv_paddr_store64, SSI_TIMEOUT, ssi_reg_val,
		    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
			DPRINTF(0, "%s: hv_paddr_store64 for "
			    "SSI_TIMEOUT FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

		/*
		 * RESULTS
		 *
		 * LOAD test - hard hang of system, was still pingable
		 * but everything else was toast.
		 *
		 * DEBUG - removing just the setting of TO to low val
		 * allowed the test to run many times, so the accesses
		 * do work.  Even with -n flag get a hang when TO is
		 * set low.  So there is some sort of problem.
		 *
		 * Even more, just setting the TO to the low val and
		 * not even performing an access produces hang...
		 *
		 * NOTE: the PRM states that the enable bit controls both
		 *	 the logging and the signaling of the errors.
		 *	 So that's not terribly useful.
		 */
		ret = 0;
		break;
	case NI_PRINT_SSI:
		temp_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, SSI_TIMEOUT,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		DPRINTF(0, "SSI_TIMEOUT = 0x%llx\n", temp_val);

		temp_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, SSI_LOG,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		DPRINTF(0, "SSI_LOG     = 0x%llx\n", temp_val);
		ret = 0;
		break;
	default:
		DPRINTF(0, "%s: unsupported command 0x%llx\n",
		    fname, iocp->ioc_command);
		ret = ENOTSUP;
		break;
	}

	/* Restore the SSI_TIMEOUT register (enable bit) */
	if ((ret = memtest_hv_util("hv_paddr_store64",
	    (void *) hv_paddr_store64, SSI_TIMEOUT, ssi_sav_val,
	    (uint64_t)ni_debug_buf_pa, NULL)) == -1) {
		DPRINTF(0, "%s: hv_paddr_store64 for "
		    "SSI_TIMEOUT FAILED, ret=0x%x\n", fname, ret);
	}

	return (ret);
}

/*
 * *****************************************************************
 * The following block of routines are the Niagara support routines.
 * *****************************************************************
 */

/*
 * This routine accesses specific floating-point registers to invoke a
 * previously injected register file parity error.
 *
 * Access routines are available for hyperpriv and kernel mode.
 *
 * NOTE: Register errors are not detected when the corrupt register is
 *	 the target of the operation since data integrity is maintained.
 */
int
ni_access_freg_file(mdata_t *mdatap, uint64_t offset)
{
	ioc_t		*iocp = mdatap->m_iocp;
	char		*fname = "ni_access_freg_file";

	if (ERR_MODE_ISKERN(iocp->ioc_command)) {
		if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
			(void) ni_k_freg_store((uint64_t)mdatap->m_kvaddr_a,
			    offset);
		} else if (ERR_ACC_ISOP(iocp->ioc_command)) {
			(void) ni_k_freg_op(offset);
		} else {
			(void) ni_k_freg_load((uint64_t)mdatap->m_kvaddr_a,
			    offset);
		}
	} else if (ERR_MODE_ISHYPR(iocp->ioc_command)) {
		if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
			(void) memtest_hv_util("ni_h_freg_store",
			    (void *)ni_h_freg_store, mdatap->m_paddr_a,
			    offset, NULL, NULL);
		} else if (ERR_ACC_ISOP(iocp->ioc_command)) {
			(void) memtest_hv_util("ni_h_freg_op",
			    (void *)ni_h_freg_op, offset, NULL, NULL, NULL);
		} else {
			(void) memtest_hv_util("ni_h_freg_load",
			    (void *)ni_h_freg_load, mdatap->m_paddr_a,
			    offset, NULL, NULL);
		}
	} else {
		DPRINTF(0, "%s: unsupported mode 0x%x\n", fname,
		    ERR_MODE(iocp->ioc_command));
		return (ENOTSUP);
	}

	return (0);
}

/*
 * This routine accesses specific integer registers to invoke a previously
 * injected register file parity error.
 *
 * Access routines are available for hyperpriv and kernel mode.
 *
 * NOTE: Register errors are not detected when the corrupt register is
 *	 the target of the operation since data integrity is maintained.
 */
int
ni_access_ireg_file(mdata_t *mdatap, uint64_t offset)
{
	ioc_t		*iocp = mdatap->m_iocp;
	char		*fname = "ni_access_ireg_file";

	if (ERR_MODE_ISKERN(iocp->ioc_command)) {
		if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
			(void) ni_k_ireg_store((uint64_t)mdatap->m_kvaddr_a);
		} else if (ERR_ACC_ISOP(iocp->ioc_command)) {
			(void) ni_k_ireg_op();
		} else {
			(void) ni_k_ireg_load((uint64_t)mdatap->m_kvaddr_a);
		}
	} else if (ERR_MODE_ISHYPR(iocp->ioc_command)) {
		if (ERR_ACC_ISSTORE(iocp->ioc_command)) {
			(void) memtest_hv_util("ni_h_ireg_store",
			    (void *)ni_h_ireg_store, mdatap->m_paddr_a,
			    offset, NULL, NULL);
		} else if (ERR_ACC_ISOP(iocp->ioc_command)) {
			(void) memtest_hv_util("ni_h_ireg_op",
			    (void *)ni_h_ireg_op, offset, NULL, NULL, NULL);
		} else {
			(void) memtest_hv_util("ni_h_ireg_load",
			    (void *)ni_h_ireg_load, mdatap->m_paddr_a,
			    offset, NULL, NULL);
		}
	} else {
		DPRINTF(0, "%s: unsupported mode 0x%x\n", fname,
		    ERR_MODE(iocp->ioc_command));
		return (ENOTSUP);
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
 */
int
ni_access_ma_memory(mdata_t *mdatap, uint_t acc_type, uint_t intr_flag)
{
	int		ret;
	char		*fname = "ni_access_ma_memory";

	/*
	 * Shift the access type for the N1 specific register.
	 */
	acc_type = acc_type << NI_MA_OP_SHIFT;

	ret = memtest_hv_util("ni_acc_ma_memory", (void *)ni_acc_ma_memory,
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
 * This routine allows the Niagara data cache to be flushed from a call through
 * the opsvec table.  Note that the routine called (in hypervisor mode) clears
 * the entire d-cache, and does not just invalidate the tags.
 */
/* ARGSUSED */
int
ni_clearall_dcache(cpu_info_t *cip)
{
	int	ret;

	if ((ret = memtest_hv_util("ni_dcache_clear", (void *)ni_dcache_clear,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "ni_clearall_dcache: trap to ni_dcache_clear"
		    "FAILED, ret=0x%x\n", ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine allows the Niagara instruction cache to be flushed from a call
 * through the opsvec table.  Note that the routine called (in hypervisor mode)
 * clears the entire i-cache, and does not just invalidate the tags.
 */
/* ARGSUSED */
int
ni_clearall_icache(cpu_info_t *cip)
{
	int	ret;

	if ((ret = memtest_hv_util("ni_icache_clear", (void *)ni_icache_clear,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "ni_clearall_icache: trap to ni_icache_clear"
		    "FAILED, ret=0x%x\n", ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine enables error traps/reporting and cache ecc/parity checking.
 *
 * Because Niagara is a CMT and to minimize system impact, by default the
 * injector will only the set the error checking on the strand that is expected
 * to see the error (this will be determined by higher level framework code).
 *
 * Niagara has the following error enable registers:
 *	SPARC	- 4 per core (one per strand)
 *	L2$	- 4 per processor (one for each DRAM bank)
 *	JBI	- 1 per processor (both LOG_ENB and SIG_ENB)
 *	SSI	- 1 per processor
 *
 * NOTE: the SPARC error enable registers seem to be only available to the
 *	 strand that owns it.  This means each strand involved in the test
 *	 must enable it's own registers separately via the xcall code.
 */
int
ni_enable_errors(mdata_t *mdatap)
{
	uint64_t	exp_l2_set[NO_L2_BANKS];
	uint64_t	exp_sparc_set, exp_jbi_log_set, exp_ssi_set;
	uint64_t	exp_l2_clr[NO_L2_BANKS];
	uint64_t	exp_sparc_clr, exp_jbi_log_clr, exp_ssi_clr;
	uint64_t	obs_l2_val[NO_L2_BANKS];
	uint64_t	obs_sparc_val, obs_jbi_log_val, obs_ssi_val;
	uint64_t	set_l2_val[NO_L2_BANKS];
	uint64_t	set_sparc_val, set_jbi_log_val, set_ssi_val;
	uint64_t	obs_jbi_sig_val, set_jbi_sig_val;
	uint64_t	obs_jbi_cfg_val, set_jbi_cfg_val;

	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset;
	int		i;
	char		*fname = "ni_enable_errors";

	/*
	 * Define default register settings.
	 */
	for (i = 0; i < NO_L2_BANKS; i++) {
		exp_l2_set[i] = CEEN | NCEEN;
		exp_l2_clr[i] = 0;
	}
	exp_sparc_set	= CEEN | NCEEN;
	exp_jbi_log_set	= JBI_ERR_MASK;
	exp_ssi_set	= NI_SSI_ERR_CFG_ERREN;

	exp_sparc_clr	= 0;
	exp_jbi_log_clr	= 0;
	exp_ssi_clr	= 0;

	DPRINTF(2, "%s: exp_l2_set=0x%llx, exp_sparc_set=0x%llx, "
	    "exp_jbi_log_set=0x%llx, exp_ssi_set=0x%llx\n", fname,
	    exp_l2_set[0], exp_sparc_set, exp_jbi_log_set, exp_ssi_set);

	/*
	 * Get the current value of each of the registers.
	 */
	for (i = 0, offset = 0; i < NO_L2_BANKS; i++,
	    offset += NI_L2_BANK_OFFSET) {
		obs_l2_val[i] = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (L2_EEN_BASE + offset),
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);
	}

	obs_sparc_val = memtest_hv_util("hv_asi_load64", (void *)hv_asi_load64,
	    ASI_ERROR_EN, (uint64_t)ni_debug_buf_pa, NULL, NULL);

	obs_jbi_log_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, JBI_LOG_ENB,
	    (uint64_t)ni_debug_buf_pa, NULL, NULL);

	obs_jbi_sig_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, JBI_SIG_ENB,
	    (uint64_t)ni_debug_buf_pa, NULL, NULL);

	obs_jbi_cfg_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, JBI_ERR_CONFIG,
	    (uint64_t)ni_debug_buf_pa, NULL, NULL);

	obs_ssi_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, SSI_TIMEOUT,
	    (uint64_t)ni_debug_buf_pa, NULL, NULL);

	DPRINTF(2, "%s: obs_l2_val=0x%llx, obs_sparc_val=0x%llx, "
	    "obs_jbi_log_val=0x%llx, obs_jbi_sig_val=0x0x%llx, "
	    "obs_jbi_cfg_val=0x%llx, obs_ssi_val=0x%llx\n", fname,
	    obs_l2_val[0], obs_sparc_val, obs_jbi_log_val, obs_jbi_sig_val,
	    obs_jbi_cfg_val, obs_ssi_val);

	/*
	 * Determine the register values either specified via command line
	 * options or using a combination of the existing values plus the
	 * bits required to be set minus the bits required to be clear.
	 *
	 * NOTE: Niagara has no fields that need to be cleared.
	 *
	 * Note that the JBus errors also require that a master enable bit
	 * in the JBI_ERROR_CONFIG register is set.
	 */
	for (i = 0, offset = 0; i < NO_L2_BANKS; i++,
	    offset += NI_L2_BANK_OFFSET) {
		set_l2_val[i] = (obs_l2_val[i] | exp_l2_set[i])
		    & ~exp_l2_clr[i];
	}
	set_sparc_val	= (obs_sparc_val | exp_sparc_set) & ~exp_sparc_clr;
	set_jbi_log_val	= (obs_jbi_log_val | exp_jbi_log_set) &
	    ~exp_jbi_log_clr;
	set_jbi_sig_val	= (obs_jbi_sig_val | JBI_ERR_MASK);
	set_jbi_cfg_val	= (obs_jbi_cfg_val | JBI_ERR_CFG_ERREN);
	set_ssi_val	= (obs_ssi_val | exp_ssi_set) & ~exp_ssi_clr;

	DPRINTF(2, "%s: set_l2_val=0x%llx, set_sparc_val=0x%llx, "
	    "set_jbi_log_val=0x%llx, set_jbi_sig_val=0x0x%llx, "
	    "set_jbi_cfg_val=0x%llx, set_ssi_val=0x%llx\n", fname,
	    set_l2_val[0], set_sparc_val, set_jbi_log_val, set_jbi_sig_val,
	    set_jbi_cfg_val, set_ssi_val);

	/*
	 * Set and verify the four L2 register settings if required.
	 */
	for (i = 0, offset = 0; i < NO_L2_BANKS; i++,
	    offset += NI_L2_BANK_OFFSET) {
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
			    (L2_EEN_BASE + offset), set_l2_val[i],
			    (uint64_t)ni_debug_buf_pa, NULL);
			/*
			 * Verify that the value was set correctly.
			 */
			obs_l2_val[i] = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64,
			    (L2_EEN_BASE + offset),
			    (uint64_t)ni_debug_buf_pa, NULL, NULL);
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
	 * Set and verify the SPARC register settings if required.
	 */
	if (obs_sparc_val != set_sparc_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting SPARC register to new value "
			    "(obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_sparc_val),
			    PRTF_64_TO_32(set_sparc_val));
		}

		(void) memtest_hv_util("hv_asi_store64", (void *)hv_asi_store64,
		    ASI_ERROR_EN, 0x0, set_sparc_val,
		    (uint64_t)ni_debug_buf_pa);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_sparc_val = memtest_hv_util("hv_asi_load64",
		    (void *)hv_asi_load64, ASI_ERROR_EN,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		if (obs_sparc_val != set_sparc_val) {
			cmn_err(CE_WARN, "couldn't set SPARC reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_sparc_val),
			    PRTF_64_TO_32(set_sparc_val));
			return (-1);
		}
	}

	/*
	 * Set and verify the JBI_LOG_ENB register settings if required.
	 */
	if (obs_jbi_log_val != set_jbi_log_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting JBI_LOG_ENB reg to new value "
			    "(obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_jbi_log_val),
			    PRTF_64_TO_32(set_jbi_log_val));
		}

		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_LOG_ENB, set_jbi_log_val,
		    (uint64_t)ni_debug_buf_pa, NULL);

		/*
		 * Verify that the values were set correctly.
		 */
		obs_jbi_log_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, JBI_LOG_ENB,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		if (obs_jbi_log_val != set_jbi_log_val) {
			cmn_err(CE_WARN, "couldn't set JBI_LOG_ENB to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_jbi_log_val),
			    PRTF_64_TO_32(set_jbi_log_val));
			return (-1);
		}
	}

	/*
	 * Set and verify the JBI_SIG_ENB register settings if required.
	 */
	if (obs_jbi_sig_val != set_jbi_sig_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting JBI_SIG_ENB reg to new value "
			    "(obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_jbi_sig_val),
			    PRTF_64_TO_32(set_jbi_sig_val));
		}

		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_SIG_ENB,
		    set_jbi_sig_val, (uint64_t)ni_debug_buf_pa, NULL);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_jbi_sig_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, JBI_SIG_ENB,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		if (obs_jbi_sig_val != set_jbi_sig_val) {
			cmn_err(CE_WARN, "couldn't set JBI SIG reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_jbi_sig_val),
			    PRTF_64_TO_32(set_jbi_sig_val));
			return (-1);
		}
	}

	/*
	 * Set and verify the JBI CONFIG register settings if required.
	 */
	if (obs_jbi_cfg_val != set_jbi_cfg_val) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting JBI CONFIG reg to new value "
			    "(obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
			    PRTF_64_TO_32(obs_jbi_cfg_val),
			    PRTF_64_TO_32(set_jbi_cfg_val));
		}

		(void) memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, JBI_ERR_CONFIG,
		    set_jbi_cfg_val, (uint64_t)ni_debug_buf_pa, NULL);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_jbi_cfg_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, JBI_ERR_CONFIG,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		if (obs_jbi_cfg_val != set_jbi_cfg_val) {
			cmn_err(CE_WARN, "couldn't set JBI CFG reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_jbi_cfg_val),
			    PRTF_64_TO_32(set_jbi_cfg_val));
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
		    (void *)hv_paddr_store64, SSI_TIMEOUT,
		    set_ssi_val, (uint64_t)ni_debug_buf_pa, NULL);

		/*
		 * Verify that the value was set correctly.
		 */
		obs_ssi_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, SSI_TIMEOUT,
		    (uint64_t)ni_debug_buf_pa, NULL, NULL);
		if (obs_ssi_val != set_ssi_val) {
			cmn_err(CE_WARN, "couldn't set SSI reg to desired "
			    "value (obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
			    PRTF_64_TO_32(obs_ssi_val),
			    PRTF_64_TO_32(set_ssi_val));
			return (-1);
		}
	}
	return (0);
}

/*
 * This routine enables or disables the memory (DRAM) or L2 cache scrubber
 * on Niagara systems using the scrub_info struct.
 */
int
ni_control_scrub(mdata_t *mdatap, uint64_t flags)
{
	scrub_info_t	*scrubp = mdatap->m_scrubp;
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	check_value;
	uint64_t	temp_value;
	uint64_t	temp_fvalue;
	uint64_t	offset;
	uint64_t	disabled_reg, disabled_val;
	uint_t		ret;
	char		*fname = "ni_control_scrub";

	DPRINTF(3, "%s: changing L2 and/or mem scrub settings "
	    "on cpuid=%d, paddr=0x%08x.%08x\n", fname,
	    getprocessorid(), PRTF_64_TO_32(mdatap->m_paddr_c));

	if (flags & MDATA_SCRUB_L2) {
		/*
		 * Determine the bank to set scrubber on (paddr<7:6>).
		 */
		scrubp->s_l2cr_addr = L2_CTL_REG + (scrubp->s_l2_offset & 0xc0);

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
			temp_value = scrubp->s_l2cr_value | L2_SCRUBENABLE;
		} else if (flags & MDATA_SCRUB_DISABLE) {
			temp_value = scrubp->s_l2cr_value & ~(L2_SCRUBENABLE);
		} else {	/* restore previously saved value */
			temp_value = scrubp->s_l2cr_value;
		}

		/*
		 * Store the appropriate value to L2CR.
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

	if (flags & MDATA_SCRUB_DRAM) {
		/*
		 * Determine the bank to set scrubber on (paddr<7:6>).
		 *
		 * Note that if the normal (4-way mode) channel is disabled,
		 * then the scrubber on the channel used must be set.
		 * See the comments in the ni_inject_memory() routine for
		 * more information on 2-channel mode settings.
		 */
		offset = (scrubp->s_mem_offset & 0xc0) << 6;
		disabled_reg = DRAM_CSR_BASE + DRAM_CHANNEL_DISABLED_REG +
		    offset;
		disabled_val = memtest_hv_util("hv_paddr_load64", (void *)
		    hv_paddr_load64, disabled_reg, NULL, NULL, NULL);

		/*
		 * If this is a STORM test the scrubbers on all four channels
		 * must be disabled.  Otherwise the correct register to use
		 * is determined from an offset derived from the physical
		 * corruption address.
		 */
		if (ERR_MISC_ISSTORM(iocp->ioc_command)) {
			offset = 0;
		}

		scrubp->s_memcr_addr = DRAM_CSR_BASE + DRAM_SCRUB_ENABLE_REG +
		    offset;
		scrubp->s_memfcr_addr = DRAM_CSR_BASE + DRAM_SCRUB_FREQ_REG +
		    offset;

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
		 * Store the appropriate values to the DRAM CR(s).
		 * Set all four registers if this is a STORM test.
		 */
		if (ERR_MISC_ISSTORM(iocp->ioc_command)) {
			for (offset = 0; offset < (0xe0 << 6);
			    offset += (0x40 << 6)) {
				if ((ret = memtest_hv_util("hv_paddr_store64",
				    (void *)hv_paddr_store64,
				    scrubp->s_memcr_addr + offset,
				    temp_value, NULL, NULL)) == -1) {
					DPRINTF(0, "%s: unable to set "
					    "DRAMCR!\n", fname);
					return (ret);
				}

				if ((ret = memtest_hv_util("hv_paddr_store64",
				    (void *)hv_paddr_store64,
				    scrubp->s_memfcr_addr + offset,
				    temp_fvalue, NULL, NULL)) == -1) {
					DPRINTF(0, "%s: unable to set "
					    "DRAMFCR!\n", fname);
					return (ret);
				}
			}

			/*
			 * Do not check the values for the STORM tests.
			 */
			return (0);
		}

		/*
		 * For non-storm tests disable only specific channels.
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
		 * If in 2-channel mode also set the active channels regs,
		 * note that disabled channels must have register contents
		 * that match the corresponding active channel.
		 */
		if (disabled_val & 0x1) {
			scrubp->s_memcr_addr ^= (0x80 << 6);
			scrubp->s_memfcr_addr ^= (0x80 << 6);

			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, scrubp->s_memcr_addr,
			    temp_value, NULL, NULL)) == -1) {
				DPRINTF(0, "%s: unable to set DRAMCR!\n",
				    fname);
				return (ret);
			}

			if ((ret = memtest_hv_util("hv_paddr_store64",
			    (void *)hv_paddr_store64, scrubp->s_memfcr_addr,
			    temp_fvalue, NULL, NULL)) == -1) {
				DPRINTF(0, "%s: unable to set DRAMFCR!\n",
				    fname);
				return (ret);
			}
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

		/*
		 * Also check the associated channel when in 2-channel mode.
		 */
		if (disabled_val & 0x1) {
			scrubp->s_memcr_addr ^= (0x80 << 6);
			scrubp->s_memfcr_addr ^= (0x80 << 6);

			if ((check_value = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, scrubp->s_memcr_addr,
			    NULL, NULL, NULL)) != temp_value) {
				DPRINTF(0, "%s: DRAMCR not set properly, value"
				    " is 0x%llx\n", fname, check_value);
				return (-1);
			} else {
				DPRINTF(3, "%s: DRAMCR set to 0x%llx\n",
				    fname, check_value);
			}

			if ((check_value = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, scrubp->s_memfcr_addr,
			    NULL, NULL, NULL)) != temp_fvalue) {
				DPRINTF(0, "%s: DRAMFCR not set properly, value"
				    " is 0x%llx\n", fname, check_value);
				return (-1);
			} else {
				DPRINTF(3, "%s: DRAMFCR set to 0x%llx\n",
				    fname, check_value);
			}
		}
	}

	return (0);
}

/*
 * This routine allows the Niagara caches to be flushed from a call through
 * the opsvec table.  Flushing the L2 cache also flushes the L1 caches since
 * the Niagara caches are inclusive.
 */
/* ARGSUSED */
int
ni_flushall_caches(cpu_info_t *cip)
{
	int	ret;

	if ((ret = memtest_hv_util("ni_l2_flushall", (void *)ni_l2_flushall,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "ni_flushall_caches: trap to ni_l2_flushall "
		    "FAILED, ret=0x%x\n", ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine allows the Niagara caches to be flushed while remaining in
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
 * Other options for the flush base address to use are:
 *	L2FLUSH_BASEADDR paddr that the HV mode code uses.
 *	mdatap->m_displace which is allocated by the mtst user program and
 *	must be mapped into kernel space.
 */
/* ARGSUSED */
int
ni_flushall_l2_kmode(cpu_info_t *cip)
{
	caddr_t		disp_addr = (caddr_t)ecache_flushaddr;
	int		ret;
	char		*fname = "ni_flushall_l2_kmode";

	DPRINTF(3, "%s: doing L2 flush (DM mode) with "
	    "displacement flush raddr=0x%llx, cachesize=0x%x, "
	    "sublinesize=0x%x\n", fname, disp_addr,
	    cip->c_l2_flushsize, cip->c_l2_sublinesize);

	if ((ret = memtest_hv_util("ni_l2_enable_DM", (void *)ni_l2_enable_DM,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to ni_l2_enable_DM "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * Unfortunately in order for the kernel to use a real address
	 * directly the MMU has to be involved (even for ASI 0x14).  So
	 * a dTLB RA->PA entry is installed for the displacement flush area.
	 */
	if ((ret = memtest_hv_util("ni_install_tte", (void *)ni_install_tte,
	    (uint64_t)disp_addr, (uint64_t)L2FLUSH_BASEADDR, NULL,
	    NULL)) == -1) {
		DPRINTF(0, "%s: trap to ni_install_tte "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	ni_l2_flushall_kmode_asm(disp_addr, cip->c_l2_flushsize,
	    cip->c_l2_sublinesize);

	if ((ret = memtest_hv_util("ni_l2_disable_DM", (void *)ni_l2_disable_DM,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to ni_l2_disable_DM "
		    "FAILED, ret=0x%x\n", fname, ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine allows a single Niagara L2$ cache entry to be flushed from a
 * call through the opsvec table.  The flush is performed in HV mode.
 */
/* ARGSUSED */
int
ni_flush_l2_entry_hvmode(cpu_info_t *cip, caddr_t paddr)
{
	int	ret;

	if ((ret = memtest_hv_util("ni_l2_flushentry", (void *)ni_l2_flushentry,
	    (uint64_t)paddr, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "ni_flush_l2_entry_hvmode: trap to ni_l2_flushentry "
		    "FAILED, ret=0x%x\n", ret);
		return (ret);
	}

	return (0);
}

/*
 * Initialize the cpu_info struct with the processor specific data.
 *
 * The initialization is done locally instead of using global kernel
 * variables and structures to maintain as much compatibility with the
 * standalone environment as possible and to reduce the dependence on
 * specific kernel versions.
 */
int
ni_get_cpu_info(cpu_info_t *cip)
{
	cip->c_dc_size = 8 * KB;
	cip->c_dc_linesize = 16;
	cip->c_dc_assoc = 4;

	cip->c_ic_size = 16 * KB;
	cip->c_ic_linesize = 32;
	cip->c_ic_assoc = 4;

	cip->c_l2_size = 3 * MB;
	cip->c_l2_sublinesize = 64;
	cip->c_l2_linesize = cip->c_l2_sublinesize *
	    (cip->c_l2_size / (1 * MB));
	cip->c_l2_assoc = 12;
	cip->c_l2_flushsize = cip->c_l2_size; /* thanks to on the fly DM mode */
	cip->c_mem_flags = 0;

	/*
	 * XXX	since the cpuid is per virtual core on sun4v, this may no
	 *	longer be the way the physical memory is separated (is using
	 *	raddrs). However the code works reliably as-is below.
	 *
	 * Note that bit 39 determines mem/io, and bits 38:37 are set to 0
	 * for cpu requests to memory.  So bit 36 should still be correct.
	 */
	cip->c_mem_start = (uint64_t)cip->c_cpuid << 36;
	cip->c_mem_size = 1ULL << 36;

	return (0);
}

void
ni_init(mdata_t *mdatap)
{
	mdatap->m_sopvp = &niagara_vops;
	mdatap->m_copvp = &niagara_cops;
	mdatap->m_cmdpp = commands;

	/*
	 * Determine the paddr of the ni_debug_buf to pass into the asm
	 * injection routines which run in hyperpriv mode.  Note that the
	 * first translation produces the raddr.
	 */
	ni_debug_buf_pa = memtest_kva_to_ra((void *)ni_debug_buf_va);
	ni_debug_buf_pa = memtest_ra_to_pa((uint64_t)ni_debug_buf_pa);
}

/*
 * This routine traps to the Niagara hypervisor test case to ensure argument
 * passing works correctly in the Niagara injector framework using the
 * hyperprivileged API.
 */
/*ARGSUSED*/
int
ni_inject_test_case(mdata_t *mdatap)
{
	int	ret;
	char	*fname = "ni_inject_test_case";

	/*
	 * Trap to hypervisor.
	 */
	if ((ret = memtest_hv_inject_error("ni_test_case",
	    (void *)ni_test_case, 0xa, 0xb, 0xc, 0xd)) == -1) {
		DPRINTF(0, "%s: trap to ni_test_case in hypervisor mode "
		    "FAILED, ret=0x%x\n", fname, ret);
		DPRINTF(0, "\n\t%s: The trap to hypervisor mode FAILED,"
		    "\n\tthe most likely cause is that"
		    "\n\tthe installed hypervisor does not have "
		    "\n\tthe trap that is required by the error "
		    "\n\tinjector. Please contact the FW group to "
		    "\n\tobtain an appropriate hypervisor for "
		    "\n\tyour test system.", fname);
		return (ret);
	}

	if (ret == 0xa55) {
		DPRINTF(0, "%s PASSED: ret = 0x%x\n", fname, ret);
	} else {
		DPRINTF(0, "\n\t%s FAILED!\n\tThe trap to hypervisor mode "
		    "FAILED,\n\tthe most likely cause is that"
		    "\n\tthe installed hypervisor does not have "
		    "\n\tthe trap that is required by the error "
		    "\n\tinjector. Please contact the FW group to "
		    "\n\tobtain an appropriate hypervisor for "
		    "\n\tyour test system.", fname);
	}

	return (0);
}

/*
 * This is the cross-call routine called by ni_k_bus_err.
 */
void
ni_jbus_intr_xcfunc(uint64_t arg1, uint64_t arg2)
{
	mdata_t	*mdatap = (mdata_t *)arg1;
	int	*temp = (int *)arg2;

	/*
	 * Perfrom a simple operation.
	 */
	DPRINTF(4, "ni_jbus_intr_xcfunc: running, arg1=0x%lx, arg2=0x%lx\n",
	    mdatap, temp);
}

/*
 * This routine traps to the Niagara hypervisor and has the HV print out
 * the contents of the current UE or CE error report on the HV console.
 *
 * NOTE: for information to be printed via the hypervisors print routines
 *	 (macros) the hypervisor must have been compiled with DEBUG enabled.
 *	 Also the HV structs used by this routine can change without notice.
 */
int
ni_util_print(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;
	int	ret;
	char	*fname = "ni_util_print";

	/*
	 * Trap to hypervisor to access specified print routine.
	 */
	if (ERR_PROT_ISUE(iocp->ioc_command)) {
		if ((ret = memtest_hv_util("ni_print_ue_errs",
		    (void *)ni_print_ue_errs, NULL, NULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: trap to ni_print_ue_errs"
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}
	} else if (ERR_PROT_ISCE(iocp->ioc_command)) {
		if ((ret = memtest_hv_util("ni_print_ce_errs",
		    (void *)ni_print_ce_errs, NULL, NULL, NULL, NULL)) == -1) {
			DPRINTF(0, "%s: trap to ni_print_ce_errs"
			    "FAILED, ret=0x%x\n", fname, ret);
			return (ret);
		}

	} else {
		DPRINTF(0, "%s: unrecognized HV console print util, exiting\n",
		    fname, ret);
		return (ret);
	}

	DPRINTF(0, "%s: ret = 0x%x\n", fname, ret);
	return (0);
}

/*
 * This routine traps to the Niagara hypervisor a number of times to read the
 * contents of the ESR and EER registers, which are then printed to the system
 * console (not the HV console).
 */
/* ARGSUSED */
int
ni_debug_print_esrs(mdata_t *mdatap)
{
	uint64_t	offset;
	uint64_t	read_val, read_oth;
	int		i;
	char		*fname = "ni_debug_print_esrs";

	/*
	 * Print the L2$ ESRs first.
	 */
	for (i = 0, offset = 0; i < NO_L2_BANKS; i++,
	    offset += NI_L2_BANK_OFFSET) {
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (L2_ESR_BASE + offset),
		    NULL, NULL, NULL);

		read_oth = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (L2_EAR_BASE + offset),
		    NULL, NULL, NULL);

		DPRINTF(0, "%s: L2_ESR %x = 0x%08x.%08x, "
		    "L2_EAR %x = 0x%08x.%08x\n", fname, i,
		    PRTF_64_TO_32(read_val), i, PRTF_64_TO_32(read_oth));
	}

	/*
	 * Print the DRAM ESRs next.
	 */
	for (i = 0, offset = 0; i < NO_DRAM_BANKS; i++,
	    offset += DRAM_BANK_STEP) {
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (DRAM_ESR_BASE + offset),
		    NULL, NULL, NULL);

		DPRINTF(0, "%s: DRAM_ESR %d = 0x%08x.%08x\n",
		    fname, i, PRTF_64_TO_32(read_val));
	}

	/*
	 * Print the internal error regs next.
	 */
	read_val = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_LSUCR, 0x0, NULL, NULL);
	DPRINTF(0, "%s: LSUCR = 0x%08x.%08x\n", fname, PRTF_64_TO_32(read_val));

	read_val = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_STATUS, 0x0, NULL, NULL);

	read_oth = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_ADDR, 0x0, NULL, NULL);

	DPRINTF(0, "%s: SPARC = 0x%08x.%08x, SPARC_EAR = 0x%08x.%08x\n", fname,
	    PRTF_64_TO_32(read_val), PRTF_64_TO_32(read_oth));

	/*
	 * Print the JBI ESR.
	 */
	read_val = memtest_hv_util("hv_paddr_load64", (void *)hv_paddr_load64,
	    JBI_ERR_CONFIG, NULL, NULL, NULL);

	DPRINTF(0, "%s: JBI ESR = 0x%08x.%08x\n", fname,
	    PRTF_64_TO_32(read_val));

	/*
	 * Print the EERs last.
	 */
	read_val = memtest_hv_util("hv_asi_load64",
	    (void *)hv_asi_load64, ASI_ERROR_EN, 0x0, NULL, NULL);
	DPRINTF(0, "%s: SPARC EEN = 0x%08x.%08x\n", fname,
	    PRTF_64_TO_32(read_val));

	for (i = 0, offset = 0; i < NO_L2_BANKS; i++,
	    offset += NI_L2_BANK_OFFSET) {
		read_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, (L2_EEN_BASE + offset),
		    NULL, NULL, NULL);

		DPRINTF(0, "%s: L2_EER %x = 0x%08x.%08x\n", fname, i,
		    PRTF_64_TO_32(read_val));
	}

	return (0);
}

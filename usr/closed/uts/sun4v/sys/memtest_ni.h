/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_NI_H
#define	_MEMTEST_NI_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Niagara (UltraSPARC-T1) specific header file for memtest loadable driver.
 */

/*
 * Test routines located in memtest_ni.c.
 */
extern	int	ni_inject_dcache(mdata_t *);
extern	int	ni_inject_dphys(mdata_t *);
extern	int	ni_inject_freg_file(mdata_t *, uint64_t);
extern	int	ni_inject_hvdcache(mdata_t *);
extern	int	ni_inject_hvicache(mdata_t *);
extern	int	ni_inject_icache(mdata_t *);
extern	int	ni_inject_iphys(mdata_t *);
extern	int	ni_inject_ireg_file(mdata_t *, uint64_t);
extern	int	ni_inject_l2cache(mdata_t *);
extern	int	ni_inject_l2dir(mdata_t *);
extern	int	ni_inject_l2phys(mdata_t *);
extern	int	ni_inject_l2_scrub(mdata_t *);
extern	int	ni_inject_l2vad(mdata_t *);
extern	int	ni_inject_ma_memory(mdata_t *);
extern	int	ni_inject_memory(mdata_t *);
extern	int	ni_inject_memory_range(mdata_t *);
extern	int	ni_inject_memory_scrub(mdata_t *);
extern	int	ni_inject_tlb(mdata_t *);
extern	int	ni_inject_tlb_random(mdata_t *);

extern uint64_t	ni_k_inject_ireg(mdata_t *, char *, void *, uint64_t,
    uint64_t, uint64_t, uint64_t);

extern	int	ni_k_bus_err(mdata_t *);
extern	int	ni_k_ssi_err(mdata_t *);

/*
 * Support routines located in memtest_ni.c.
 */
extern	int	ni_access_freg_file(mdata_t *, uint64_t);
extern	int	ni_access_ireg_file(mdata_t *, uint64_t);
extern	int	ni_access_ma_memory(mdata_t *, uint_t, uint_t);
extern	int	ni_clearall_dcache(cpu_info_t *);
extern	int	ni_clearall_icache(cpu_info_t *);
extern	int	ni_enable_errors(mdata_t *);
extern	int	ni_control_scrub(mdata_t *, uint64_t);
extern	int	ni_flushall_caches(cpu_info_t *);
extern	int	ni_flushall_l2_kmode(cpu_info_t *);
#define	ni_flushall_l2_hvmode ni_flushall_caches
extern	int	ni_flush_l2_entry_hvmode(cpu_info_t *, caddr_t);
extern	int	ni_get_cpu_info(cpu_info_t *);
extern	void	ni_init(mdata_t *);
extern	int	ni_inject_test_case(mdata_t *);
extern	void	ni_jbus_intr_xcfunc(uint64_t, uint64_t);
extern	int	ni_util_print(mdata_t *);
extern	int	ni_debug_print_esrs(mdata_t *);

/*
 * Test routines located in memtest_ni_asm.s to be run in
 * hyperpriv mode through the hcall API.
 */
extern	int	ni_inj_dcache_data(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	ni_inj_dcache_hvdata(uint64_t, uint64_t, uint_t, uint_t);
extern	int	ni_inj_dcache_hvtag(uint64_t, uint64_t, uint_t, uint_t);
extern	int	ni_inj_dcache_tag(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	ni_inj_dphys_data(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	ni_inj_dphys_tag(uint64_t, uint64_t, uint_t, uint64_t *);

extern	int	ni_inj_dtlb(uint64_t, uint64_t, uint64_t, uint32_t);
extern	int	ni_inj_dtlb_store(uint64_t, uint64_t, uint64_t, uint32_t);
extern	int	ni_inj_dtlb_v(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	ni_inj_freg_file(uint64_t, uint64_t, uint64_t, uint64_t);

extern	int	ni_inj_icache_instr(uint64_t, uint64_t, uint64_t *);
extern	int	ni_inj_icache_hvinstr(uint64_t, uint64_t, uint64_t *);
extern	int	ni_inj_icache_hvtag(uint64_t, uint64_t, uint64_t *);
extern	int	ni_inj_icache_tag(uint64_t, uint64_t, uint64_t *);
extern	int	ni_inj_iphys_instr(uint64_t, uint64_t, uint64_t *);
extern	int	ni_inj_iphys_tag(uint64_t, uint64_t, uint64_t *);

extern	int	ni_inj_ireg_file(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	ni_inj_ireg_hvfile(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	ni_inj_itlb(uint64_t, uint64_t, uint64_t, uint32_t);

extern	int 	ni_inj_l2cache_instr_data(uint64_t, uint64_t, uint_t);
extern	int	ni_inj_l2cache_instr_tag(uint64_t, uint64_t, uint_t);
extern	int	ni_inj_l2cache_data(uint64_t, uint64_t, uint_t);
extern	int	ni_inj_l2cache_tag(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	ni_inj_l2dir(uint64_t, uint_t, uint_t, uint64_t *);
extern	int	ni_inj_l2dir_phys(uint64_t, uint_t, uint64_t *);
extern	int	ni_inj_l2phys_data(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	ni_inj_l2phys_tag(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	ni_inj_l2vad(uint64_t, uint_t, uint_t);
extern	int	ni_inj_l2vad_instr(uint64_t, uint_t, uint_t);
extern	int	ni_inj_l2vad_phys(uint64_t, uint_t, uint_t, uint64_t *);

extern	int	ni_inj_ma_parity(uint64_t, uint_t);
extern	int	ni_inj_memory(uint64_t, uint_t, uint64_t *);
extern	int	ni_inj_tlb_rand(uint64_t);

/*
 * Support routines located in memtest_ni_asm.s to be run in
 * hyperpriv mode through hcall API (except for ni_k_*reg_* routines
 * which are run in kernel mode).
 */
extern	int		ni_acc_ma_memory(uint64_t, uint_t, uint_t);
extern	void		ni_dcache_clear(void);
extern	void		ni_icache_clear(void);
extern	void		ni_ic_hvaccess(void);
extern	uint64_t	ni_get_guest_partid(void);

extern	int		ni_h_freg_load(uint64_t, uint64_t);
extern	int		ni_h_freg_op(uint64_t);
extern	int		ni_h_freg_store(uint64_t, uint64_t);
extern	int		ni_h_ireg_load(uint64_t, uint64_t);
extern	int		ni_h_ireg_op(uint64_t);
extern	int		ni_h_ireg_store(uint64_t, uint64_t);

extern	int		ni_k_freg_load(uint64_t, uint64_t);
extern	int		ni_k_freg_op(uint64_t);
extern	int		ni_k_freg_store(uint64_t, uint64_t);
extern	int		ni_k_ireg_load(uint64_t);
extern	int		ni_k_ireg_op(void);
extern	int		ni_k_ireg_store(uint64_t);

extern	void		ni_l1_disable_DM(void);
extern	void		ni_l2_disable_DM(void);
extern	void		ni_l2_enable_DM(void);
extern	void		ni_l2_flushall(caddr_t);
extern	void		ni_l2_flushall_kmode_asm(caddr_t, uint64_t, uint64_t);
extern	void		ni_l2_flushentry(caddr_t);
extern	void		ni_install_tte(uint64_t, uint64_t);
extern	void		ni_print_ce_errs(void);
extern	void		ni_print_ue_errs(void);
extern	void		ni_print_icache(void);
extern	void		ni_print_itag(void);
extern	void		ni_print_jbi_regs(uint64_t *);
extern	void		ni_print_mem(uint64_t, uint64_t);
extern	int		ni_test_case(uint64_t, uint64_t, uint64_t, uint64_t);

/*
 * CSR and other register access definitions.
 */
#define	NI_L2_BANK_OFFSET	0x40
#define	NI_DTLB_SIZE		(0x40 << 3)
#define	NI_ITLB_SIZE		(0x40 << 3)

#define	NI_L2_DIR_INJ_ENABLE	0x1
#define	NI_L2_SSHOT_ENABLE	0x2

#define	NI_DRAM_SSHOT_ENABLE		(1LL << 30)
#define	NI_DRAM_INJECTION_ENABLE	(1LL << 31)

/* Custom MA definitions */
#define	NI_MA_OP_SHIFT		6
#define	NI_MA_OP_LOAD		(0x0 << NI_MA_OP_SHIFT) /* from mem/L2 to MA */
#define	NI_MA_OP_STORE		(0x1 << NI_MA_OP_SHIFT) /* from MA to mem/L2 */
#define	NI_MA_OP_MULT		(0x2 << NI_MA_OP_SHIFT)
#define	NI_MA_OP_REDUCT		(0x3 << NI_MA_OP_SHIFT)
#define	NI_MA_OP_EXP		(0x4 << NI_MA_OP_SHIFT)

/* Custom HW scrub definitions */
#define	NI_L2_SCRUB_INTERVAL_MASK	0x7ff8
#define	NI_L2_SCRUB_INTERVAL_DFLT	0x1f0
#define	NI_DRAM_SCRUB_INTERVAL_MASK	0xfff
#define	NI_DRAM_SCRUB_INTERVAL_DFLT	0xff

/* Custom JBus and SSI (IO) definitions */
#define	JBI_ERR_MASK		0x1f03ff37	/* only RSVD bits unset */
#define	JBI_ERR_CFG_ERREN	(1ULL << 3)
#define	JBI_ERR_CFG_PPRES	(0x7fULL << 44)
#define	JERR_INJ_ATYPE		(1ULL << 28)
#define	JERR_INJ_INPUT		(1ULL << 29)
#define	JERR_INJ_OUTPUT		(1ULL << 30)

#define	IOBINT_BASE		0x9f00000000
#define	J_INT_BUSY		(IOBINT_BASE | 0x900)
#define	J_INT_ABUSY		(IOBINT_BASE | 0xb00)

#define	NI_IO_BIT		(1ULL << 39)
#define	NI_DMA_LB_BASE		(NI_IO_BIT | 0x10000000)
#define	NI_MEMSIZE_MIN		(1ULL << 30)

#define	NI_SSI_BASE		0xfff0000000	/* start of SSI address space */
#define	NI_BOOT_BASE		0x0000800000	/* start of BootROM */
#define	NI_SSI_TOMASK		0x0000ffffff	/* timeout control bits */
#define	NI_SSI_ERR_CFG_ERREN	(1ULL << 24)

/*
 * Niagara commands structure.
 */
extern  cmd_t   niagara_cmds[];

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_NI_H */

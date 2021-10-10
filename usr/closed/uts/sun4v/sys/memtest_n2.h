/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_N2_H
#define	_MEMTEST_N2_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Niagara-II (UltraSPARC-T2) specific header file for memtest loadable driver.
 */

/*
 * Test routines located in memtest_n2.c.
 */
extern	int	n2_inject_cwq(mdata_t *);
extern	int	n2_inject_dcache(mdata_t *);
extern	int	n2_inject_dphys(mdata_t *);
extern	int	n2_inject_freg_file(mdata_t *, uint64_t);
extern	int	n2_inject_hvdcache(mdata_t *);
extern	int	n2_inject_hvicache(mdata_t *);
extern	int	n2_inject_icache(mdata_t *);
extern	int	n2_inject_int_array(mdata_t *);
extern	int	n2_inject_iphys(mdata_t *);
extern	int	n2_inject_ireg_file(mdata_t *, uint64_t);

extern	int	n2_inject_l2cache(mdata_t *);
extern	int	n2_inject_l2dir(mdata_t *);
extern	int	n2_inject_l2nd(mdata_t *);
extern	int	n2_inject_l2phys(mdata_t *);
extern	int	n2_inject_l2_scrub(mdata_t *);
extern	int	n2_inject_l2vad(mdata_t *);
extern	int	n2_inject_mamem(mdata_t *);
extern	int	n2_inject_memory(mdata_t *);
extern	int	n2_inject_memory_scrub(mdata_t *);
extern	int	n2_inject_sb(mdata_t *);
extern	int	n2_inject_soc_int(mdata_t *);
extern	int	n2_inject_soc_mcu(mdata_t *);
extern	int	n2_inject_tlb(mdata_t *);
extern	int	n2_inject_tlb_l2_miss(mdata_t *);
extern	int	n2_inject_tlb_mem_miss(mdata_t *);

/*
 * Support routines located in memtest_n2.c.
 */
extern	void		n2_init(mdata_t *);
extern	void		n2_debug_init();
extern	void		n2_debug_dump();
extern	int		n2_inject_test_case(mdata_t *);
extern	int		n2_access_cwq(mdata_t *, uint_t, uint_t);
extern	int		n2_access_mamem(mdata_t *, uint_t, uint_t);
extern	uint64_t	n2_check_l2_bank_mode(void);
extern	int		n2_check_l2_idx_mode(uint64_t, uint64_t *);
extern	int		n2_clearall_dcache(cpu_info_t *);
extern	int		n2_clearall_icache(cpu_info_t *);
extern	int		n2_debug_clear_esrs(mdata_t *);
extern	int		n2_debug_print_esrs(mdata_t *);
extern	int		n2_enable_errors(mdata_t *);
extern	int		n2_control_scrub(mdata_t *, uint64_t);
extern	int		n2_flushall_l2_hvmode(cpu_info_t *);
extern	int		n2_flushall_l2_kmode(cpu_info_t *);
extern	int		n2_flush_l2_entry_hvmode(cpu_info_t *, caddr_t);
extern	int		n2_flush_l2_entry_ice(cpu_info_t *, caddr_t);
extern	int		n2_flush_l2_entry_kmode(cpu_info_t *, caddr_t);
extern	int		n2_get_cpu_info(cpu_info_t *);

/*
 * Support #define's and variables located in memtest_n2.c.
 */

#define	n2_flushall_caches n2_flushall_l2_hvmode

#define	DEBUG_BUF_SIZE  32
extern	uint64_t	n2_debug_buf_va[DEBUG_BUF_SIZE];
extern	uint64_t	n2_debug_buf_pa;

/*
 * Enable/disable the storing of DEBUG values into the debug mem buffer
 * for different test classes.
 */
/* #define	MEM_DEBUG_BUFFER	1 */
/* #define	L1_DEBUG_BUFFER		1 */
/* #define	L2_DEBUG_BUFFER		1 */

/*
 * Test routines located in memtest_n2_asm.s to be run in
 * hyperpriv mode through the hcall API.
 */
extern	int	n2_inj_dcache_data(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	n2_inj_dcache_hvdata(uint64_t, uint64_t, uint_t, uint_t);
extern	int	n2_inj_dcache_hvmult(uint64_t, uint64_t, uint_t);
extern	int	n2_inj_dcache_hvtag(uint64_t, uint64_t, uint_t, uint_t);
extern	int	n2_inj_dcache_mult(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_dcache_tag(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	n2_inj_dphys_data(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	n2_inj_dphys_mult(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_dphys_tag(uint64_t, uint64_t, uint_t, uint64_t *);

extern	int	n2_inj_dtlb(uint64_t, uint64_t, uint64_t, uint32_t);
extern	int	n2_inj_dtlb_mult(uint64_t, uint64_t, uint64_t *, uint64_t);
extern	int	n2_inj_dtlb_v(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	n2_inj_freg_file(uint64_t, uint64_t, uint64_t, uint64_t);

extern	int	n2_inj_icache_instr(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_icache_hvinstr(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_icache_hvmult(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_icache_hvtag(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_icache_mult(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_icache_tag(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_iphys_instr(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_iphys_mult(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_iphys_tag(uint64_t, uint64_t, uint64_t *);

extern	int	n2_inj_ireg_file(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	n2_inj_ireg_hvfile_global(uint64_t, uint64_t, uint64_t,
			uint64_t);
extern	int	n2_inj_ireg_hvfile_in(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	n2_inj_ireg_hvfile_local(uint64_t, uint64_t, uint64_t,
			uint64_t);
extern	int	n2_inj_ireg_hvfile_out(uint64_t, uint64_t, uint64_t, uint64_t);

extern	int	n2_inj_itlb(uint64_t, uint64_t, uint64_t, uint32_t);
extern	int	n2_inj_itlb_mult(uint64_t, uint64_t, uint64_t *, uint64_t);
extern	int	n2_inj_itlb_v(uint64_t, uint64_t, uint64_t, uint64_t);

extern	int 	n2_inj_l2cache_instr_data(uint64_t, uint64_t, uint_t, uint64_t);
extern	int	n2_inj_l2cache_instr_tag(uint64_t, uint64_t, uint_t, uint64_t);
extern	int	n2_inj_l2cache_data(uint64_t, uint64_t, uint_t, uint64_t);
extern	int	n2_inj_l2cache_data_quick(uint64_t, uint64_t, uint_t, uint64_t);
extern	int	n2_inj_l2cache_tag(uint64_t, uint64_t, uint_t, uint64_t);
extern	int	n2_inj_l2dir(uint64_t, uint_t, uint_t, uint64_t);
extern	int	n2_inj_l2dir_phys(uint64_t, uint_t, uint64_t *);
extern	int	n2_inj_l2nd(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_l2nd_quick(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_l2nd_instr(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_l2nd_phys(uint64_t, uint64_t, uint64_t *);
extern	int	n2_inj_l2phys_data(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	n2_inj_l2phys_tag(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	n2_inj_l2vad(uint64_t, uint_t, uint_t, uint64_t);
extern	int	n2_inj_l2vad_instr(uint64_t, uint_t, uint_t, uint64_t);
extern	int	n2_inj_l2vad_phys(uint64_t, uint_t, uint_t, uint64_t *);

extern	int	n2_inj_mamem(uint64_t, uint_t, uint64_t);
extern	int	n2_inj_memory(uint64_t, uint_t, uint_t, uint64_t);
extern	int	n2_inj_memory_quick(uint64_t, uint_t, uint_t, uint64_t);
extern	int	n2_inj_memory_debug(uint64_t, uint_t, uint_t, uint64_t *);

extern	int	n2_inj_mra(uint64_t, uint64_t, uint64_t);
extern	int	n2_inj_sb(uint64_t, uint64_t, uint64_t);
extern	int	n2_inj_sb_asi(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	n2_inj_sb_io(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	n2_inj_sb_load(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	n2_inj_sb_pcx(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	n2_inj_sca(uint64_t, uint64_t, uint64_t);

extern	int	n2_inj_soc_mcu(uint64_t, uint64_t, uint64_t);

extern	int	n2_inj_tca(uint64_t, uint64_t, uint64_t);
extern	int	n2_inj_tlb_mmu(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	n2_inj_tsa(uint64_t, uint64_t, uint64_t);

/*
 * Support routines located in memtest_n2_asm.s to be run in
 * hyperpriv mode through hcall API.
 */
extern	int		n2_acc_cwq(uint64_t, uint64_t, uint_t, uint_t);
extern	int		n2_acc_mamem(uint64_t, uint_t, uint_t);
extern	void		n2_clear_tlb_entry(uint64_t, uint64_t);
extern	void		n2_dcache_clear(void);
extern	void		n2_icache_clear(void);

extern	void		n2_ic_hvaccess(void);

extern	void		n2_l1_disable_DM(void);
extern	void		n2_l2_disable_DM(void);
extern	void		n2_l2_enable_DM(void);
extern	void		n2_l2_flushall(caddr_t);
extern	void		n2_l2_flushall_ice(void);
extern	void		n2_l2_flushall_kmode_asm(caddr_t, uint64_t, uint64_t);
extern	int		n2_flushall_l2_kmode(cpu_info_t *);
extern	void		n2_l2_flushentry(caddr_t);
extern	void		n2_l2_flushentry_ice(caddr_t);
extern	void		n2_l2_flushidx(caddr_t);
extern	void		n2_l2_flushidx_ice(caddr_t);
extern	uint64_t	n2_l2_get_flushbase(void);
extern	void		n2_install_tlb_entry(uint64_t, uint64_t);

/*
 * CSR and other register access definitions.
 */

/* Custom MA definitions (different than Niagara-I) */
#define	N2_MA_OP_SHIFT	8
#define	N2_MA_OP_LOAD	(0x0 << N2_MA_OP_SHIFT) /* from memory/L2 to MA */
#define	N2_MA_OP_STORE	(0x1 << N2_MA_OP_SHIFT) /* from MA to memory/L2 */
#define	N2_MA_OP_MULT	(0x2 << N2_MA_OP_SHIFT)
#define	N2_MA_OP_REDUCT	(0x3 << N2_MA_OP_SHIFT)
#define	N2_MA_OP_EXP	(0x4 << N2_MA_OP_SHIFT)

/* Custom Control Word Queue definitions */
#define	CWQ_OP_INVALID		13
#define	CWQ_OP_SSL		16
#define	CWQ_OP_COPY		32
#define	CWQ_OP_CIPHER		64
#define	CWQ_OP_AUTH		65

/*
 * Niagara-II commands structure.
 */
extern  cmd_t   niagara2_cmds[];

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_N2_H */

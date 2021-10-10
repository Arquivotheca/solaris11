/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MEMTEST_KT_H
#define	_MEMTEST_KT_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Rainbow Falls (UltraSPARC-T3 aka KT) specific header file for
 * memtest loadable driver.
 */

/*
 * Test routines located in memtest_kt.c.
 */
extern	int	kt_inject_freg_file(mdata_t *, uint64_t);
extern	int	kt_inject_fbd_failover(mdata_t *);
extern	int	kt_inject_hvicache(mdata_t *);
extern	int	kt_inject_icache(mdata_t *);

extern	int	kt_inject_l2buf(mdata_t *);
extern	int	kt_inject_l2cache(mdata_t *);
extern	int	kt_inject_l2nd(mdata_t *);
extern	int	kt_inject_l2phys(mdata_t *);
extern	int	kt_inject_l2vads(mdata_t *);
extern	int	kt_inject_lfu_lf_err(mdata_t *);
extern	int	kt_inject_lfu_rtf_err(mdata_t *);
extern	int	kt_inject_lfu_to_err(mdata_t *);
extern	int	kt_inject_mcu_fbd(mdata_t *);
extern	int	kt_inject_memory(mdata_t *);
extern	int	kt_inject_memory_int(mdata_t *);
extern	int	kt_inject_memory_scrub(mdata_t *);
extern	int	kt_inject_ncu_err(mdata_t *);
extern	int	kt_inject_soc_int(mdata_t *);
extern	int	kt_inject_tlb_l2_miss(mdata_t *);
extern	int	kt_inject_tlb_mem_miss(mdata_t *);
extern	int	kt_k_ssi_err(mdata_t *);

/*
 * Support routines located in memtest_kt.c.
 */
extern	void		kt_init(mdata_t *);
extern	void		kt_debug_init();
extern	void		kt_debug_dump();
extern	uint64_t	kt_check_l2_bank_mode(void);
extern	int		kt_check_l2_idx_mode(uint64_t, uint64_t *);
extern	int		kt_control_scrub(mdata_t *, uint64_t);
extern	int		kt_debug_clear_esrs(mdata_t *);
extern	int		kt_debug_print_esrs(mdata_t *);
extern	int		kt_enable_errors(mdata_t *);
extern	int		kt_flushall_l2_hvmode(cpu_info_t *);
extern	int		kt_flushall_l2_kmode(cpu_info_t *);
extern	int		kt_flush_l2_entry_hvmode(cpu_info_t *, caddr_t);
extern	int		kt_flush_l2_entry_ice(cpu_info_t *, caddr_t);
extern	int		kt_flush_l2_entry_kmode(cpu_info_t *, caddr_t);
extern	int		kt_get_cpu_info(cpu_info_t *);
extern	uint64_t	kt_perform_l2_idx_hash(uint64_t);
extern	int		kt_pre_test_copy_asm(mdata_t *);

/*
 * Support #define's and variables located in memtest_kt.c.
 */

#define	kt_flushall_caches kt_flushall_l2_hvmode

#define	DEBUG_BUF_SIZE  32
extern	uint64_t	kt_debug_buf_va[DEBUG_BUF_SIZE];
extern	uint64_t	kt_debug_buf_pa;

/*
 * Enable/disable the storing of DEBUG values into the debug mem buffer
 * for different test classes.
 */
/* #define	MEM_DEBUG_BUFFER	1 */
/* #define	L1_DEBUG_BUFFER		1 */
/* #define	L2_DEBUG_BUFFER		1 */

/*
 * Test routines located in memtest_kt_asm.s to be run in
 * hyperpriv mode through the hcall API.
 */
extern	int	kt_inj_freg_file(uint64_t, uint64_t, uint64_t, uint64_t);

extern	int	kt_inj_icache_instr(uint64_t, uint64_t, uint64_t *);
extern	int	kt_inj_icache_hvinstr(uint64_t, uint64_t, uint64_t *);
extern	int	kt_inj_icache_hvmult(uint64_t, uint64_t, uint64_t *);
extern	int	kt_inj_icache_hvtag(uint64_t, uint64_t, uint64_t *);
extern	int	kt_inj_icache_mult(uint64_t, uint64_t, uint64_t *);
extern	int	kt_inj_icache_tag(uint64_t, uint64_t, uint64_t *);

extern	int	kt_inj_ireg_file(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	kt_inj_ireg_hvfile_global(uint64_t, uint64_t, uint64_t,
			uint64_t);
extern	int	kt_inj_ireg_hvfile_in(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	kt_inj_ireg_hvfile_local(uint64_t, uint64_t, uint64_t,
			uint64_t);
extern	int	kt_inj_ireg_hvfile_out(uint64_t, uint64_t, uint64_t, uint64_t);

extern	int	kt_inj_l2buf(uint64_t, uint_t, uint_t, uint64_t);
extern	int	kt_inj_l2buf_instr(uint64_t, uint_t, uint_t, uint64_t);
extern	int	kt_inj_l2buf_phys(uint64_t, uint_t, uint64_t *, uint64_t);
extern	int 	kt_inj_l2cache_instr_data(uint64_t, uint64_t, uint_t, uint64_t);
extern	int	kt_inj_l2cache_instr_tag(uint64_t, uint64_t, uint_t, uint64_t);
extern	int	kt_inj_l2cache_data(uint64_t, uint64_t, uint_t, uint64_t);
extern	int	kt_inj_l2cache_data_quick(uint64_t, uint64_t, uint_t, uint64_t);
extern	int	kt_inj_l2cache_tag(uint64_t, uint64_t, uint_t, uint64_t);
extern	int	kt_inj_l2nd(uint64_t, uint64_t, uint64_t, uint64_t *);
extern	int	kt_inj_l2nd_quick(uint64_t, uint64_t, uint64_t, uint64_t *);
extern	int	kt_inj_l2nd_instr(uint64_t, uint64_t, uint64_t, uint64_t *);
extern	int	kt_inj_l2nd_phys(uint64_t, uint64_t, uint64_t *);
extern	int	kt_inj_l2phys_data(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	kt_inj_l2phys_tag(uint64_t, uint64_t, uint_t, uint64_t *);
extern	int	kt_inj_l2vads(uint64_t, uint_t, uint_t, uint64_t);
extern	int	kt_inj_l2vads_instr(uint64_t, uint_t, uint_t, uint64_t);
extern	int	kt_inj_l2vads_phys(uint64_t, uint_t, uint_t, uint64_t *);
extern	int	kt_inj_l2wbuf(uint64_t, uint_t, uint_t, uint64_t);

extern	int	kt_inj_memory(uint64_t, uint64_t, uint64_t, uint64_t);
extern	int	kt_inj_memory_debug(uint64_t, uint64_t, uint64_t, uint64_t *);
extern	int	kt_inj_memory_debug_ice(uint64_t, uint64_t, uint64_t,
			uint64_t *);
extern	int	kt_inj_memory_debug_disp(uint64_t, uint64_t, uint64_t,
			uint64_t *);
extern	int	kt_inj_memory_int(uint64_t, uint64_t, uint64_t, uint64_t *);
extern	int	kt_inj_memory_quick(uint64_t, uint64_t, uint64_t, uint64_t);

extern	int	kt_inj_sb_io(uint64_t, uint64_t, uint64_t, uint64_t);

/*
 * Support routines located in memtest_kt_asm.s to be run in
 * hyperpriv mode through hcall API.
 */
extern	void		kt_icache_disable_DM(void);
extern	void		kt_icache_enable_DM(void);

extern	void		kt_ic_hvaccess(void);

extern	void		kt_k_freg_load(uint64_t, uint64_t);
extern	void		kt_k_freg_op(uint64_t);
extern	void		kt_k_freg_store(uint64_t, uint64_t);

extern	void		kt_l2_disable_DM(void);
extern	void		kt_l2_enable_DM(void);
extern	void		kt_l2_flushall(caddr_t);
extern	void		kt_l2_flushall_ice(void);
extern	void		kt_l2_flushall_kmode_asm(caddr_t, uint64_t, uint64_t);
extern	void		kt_l2_flushentry(caddr_t);
extern	void		kt_l2_flushentry_ice(caddr_t);
extern	void		kt_l2_flushidx(caddr_t);
extern	void		kt_l2_flushidx_ice(caddr_t);
extern	uint64_t	kt_l2_get_flushbase(void);

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
 * KT/RF commands structure.
 */
extern  cmd_t   kt_cmds[];

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_KT_H */

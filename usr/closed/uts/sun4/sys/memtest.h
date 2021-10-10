/*
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MEMTEST_H
#define	_MEMTEST_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Common header file for memtest loadable driver.
 */

#include <sys/async.h>
#include <sys/cmn_err.h>
#include <sys/conf.h>
#include <sys/cpuvar.h>
#include <sys/cpu_module.h>
#include <sys/cyclic.h>
#include <sys/ddi.h>
#include <sys/errno.h>
#include <sys/inttypes.h>
#include <sys/kmem.h>
#include <sys/kobj.h>
#include <sys/machsystm.h>
#include <sys/membar.h>
#include <sys/modctl.h>
#include <sys/note.h>
#include <sys/open.h>
#include <sys/proc.h>
#include <sys/promif.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/vmem.h>
#include <sys/vmsystm.h>
#include <sys/x_call.h>
#include <vm/as.h>
#include <vm/hat_sfmmu.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>

/*
 * Boolean definitions
 */
#define	FALSE		0
#define	TRUE		1

#define	KB	1024
#define	MB	(KB * 1024)
#define	GB	(MB * 1024)

#define	MDATA_MSGBUF_SIZE	40

/*
 * Macro for putting 64-bit value onto stack as two 32-bit ints
 */
#define	PRTF_64_TO_32(x)	(uint32_t)((x)>>32), (uint32_t)(x)

/*
 * Macros for calling the common operation vector (opsvec) routines.
 */
#define	OP_ACCESS_FREG(mdatap, offset) \
		((mdatap)->m_copvp->op_access_freg)((mdatap), (offset))

#define	OP_ACCESS_IREG(mdatap, offset) \
		((mdatap)->m_copvp->op_access_ireg)((mdatap), (offset))

#define	OP_CHECK_ESRS(mdatap, string) \
		((mdatap)->m_copvp->op_check_esrs)((mdatap), (string))

#define	OP_ENABLE_ERRORS(mdatap) \
		((mdatap)->m_copvp->op_enable_errors)((mdatap))

#define	OP_CONTROL_SCRUB(mdatap, flags) \
		((mdatap)->m_copvp->op_control_scrub)((mdatap), (flags))

#define	OP_FLUSHALL_CACHES(mdatap) \
		((mdatap)->m_copvp->op_flushall_caches)((mdatap)->m_cip)

#define	OP_FLUSHALL_DC(mdatap) \
		((mdatap)->m_copvp->op_flushall_dc)((mdatap)->m_cip)

#define	OP_FLUSHALL_IC(mdatap) \
		((mdatap)->m_copvp->op_flushall_ic)((mdatap)->m_cip)

#define	OP_FLUSHALL_L2(mdatap) \
		((mdatap)->m_copvp->op_flushall_l2)((mdatap)->m_cip)

#define	OP_FLUSH_DC_ENTRY(mdatap, addr) \
		((mdatap)->m_copvp->op_flush_dc_entry)(((mdatap)->m_cip), \
							(addr))

#define	OP_FLUSH_IC_ENTRY(mdatap, addr) \
		((mdatap)->m_copvp->op_flush_ic_entry)(((mdatap)->m_cip), \
							(addr))

#define	OP_FLUSH_L2_ENTRY(mdatap, addr) \
		((mdatap)->m_copvp->op_flush_l2_entry)(((mdatap)->m_cip), \
							(addr))

#define	OP_GET_CPU_INFO(mdatap) \
		((mdatap)->m_copvp->op_get_cpu_info)((mdatap)->m_cip)

#define	OP_INJECT_DCACHE(mdatap) \
		((mdatap)->m_copvp->op_inject_dcache)((mdatap))

#define	OP_INJECT_DPHYS(mdatap) \
		((mdatap)->m_copvp->op_inject_dphys)((mdatap))

#define	OP_INJECT_FREG(mdatap, offset) \
		((mdatap)->m_copvp->op_inject_freg)((mdatap), (offset))

#define	OP_INJECT_ICACHE(mdatap) \
		((mdatap)->m_copvp->op_inject_icache)((mdatap))

#define	OP_INJECT_INTERNAL(mdatap) \
		((mdatap)->m_copvp->op_inject_internal)((mdatap))

#define	OP_INJECT_IPHYS(mdatap) \
		((mdatap)->m_copvp->op_inject_iphys)((mdatap))

#define	OP_INJECT_IREG(mdatap, offset) \
		((mdatap)->m_copvp->op_inject_ireg)((mdatap), (offset))

#define	OP_INJECT_L2CACHE(mdatap) \
		((mdatap)->m_copvp->op_inject_l2cache)((mdatap))

#define	OP_INJECT_L2PHYS(mdatap) \
		((mdatap)->m_copvp->op_inject_l2phys)((mdatap))

#define	OP_INJECT_TLB(mdatap) \
		((mdatap)->m_copvp->op_inject_tlb)((mdatap))

/*
 * Assembly routine type definitions, to be used in the structure
 * definitions below.
 */
typedef void asmld_t(caddr_t);
typedef void asmld_tl1_t(caddr_t);
typedef void asmldst_t(caddr_t, caddr_t, int);
typedef void asmst_tl1_t(caddr_t, uchar_t);
typedef void blkld_t(caddr_t);
typedef void blkld_tl1_t(caddr_t);
typedef	void pcrel_t(void);

/*
 * This is the main data structure used to pass information within the driver.
 * There is one mdata structure per thread. Some of the fields will be the
 * same while other info is specific to each thread.
 */
typedef struct mdata {
	ioc_t		*m_iocp;	/* user ioctl buffer */
	system_info_t	*m_sip;		/* system info structure */
	cpu_info_t	*m_cip;		/* cpu info structure */
	struct common_opsvec *m_copvp;	/* common operations vector table */
#ifndef	sun4v
	struct sun4u_opsvec *m_sopvp;	/* sun4u operations vector table */
#else	/* !sun4v */
	struct sun4v_opsvec *m_sopvp;	/* sun4v operations vector table */
#endif	/* sun4v */
	struct cmd	**m_cmdpp;	/* commands list array */
	struct cmd	*m_cmdp;	/* command being executed */
	struct memtest	*m_memtestp;	/* per unit driver data structure */
	struct scrub_info *m_scrubp;	/* system HW scrub state structure */
	asmld_t		*m_asmld;	/* routine to load data */
	asmldst_t	*m_asmldst;	/* routine to ld/st data */
	asmld_tl1_t	*m_asmld_tl1;	/* routine to load data at TL 1 */
	asmst_tl1_t	*m_asmst_tl1;	/* routine to store data at TL 1 */
	blkld_t		*m_blkld;	/* routine to block load data */
	blkld_tl1_t	*m_blkld_tl1;	/* routine to block load at TL1 */
	pcrel_t		*m_pcrel;	/* routine for %pc rel instr */
	char		*m_msgp;	/* used to pass misc msgs strings */
	caddr_t		m_databuf;	/* kbuf used for data corruption */
	caddr_t		m_instbuf;	/* kbuf used for inst corruption */
	caddr_t		m_uvaddr_a;	/* user virtual addr to access */
	caddr_t		m_uvaddr_c;	/* user virtual addr to corrupt */
	caddr_t		m_kvaddr_a;	/* kernel virtual addr to access */
	caddr_t		m_kvaddr_c;	/* kernel virtual addr to corrupt */
	uint64_t	m_raddr_a;	/* real address to access */
	uint64_t	m_raddr_c;	/* real address to corrupt */
	uint64_t	m_paddr_a;	/* physical address to corrupt */
	uint64_t	m_paddr_c;	/* physical address to access */
	int		m_threadno;	/* number of this thread */
	volatile int	*m_syncp;	/* thread's synchronization variable */
} mdata_t;

/*
 * Each CPU may require unique routines to support common operations.
 * This is the common operations vector (opsvec) table used to call processor
 * specific routines with a minimum of overhead.
 *
 * Note that the sun4u and sun4v opsvec tables are also defined below so that
 * they can be attached to the mdata struct which is used in the common code.
 */
typedef	struct common_opsvec {
	/* common injection ops vectors */
	int	(*op_inject_dcache)(mdata_t *);
	int	(*op_inject_dphys)(mdata_t *);
	int	(*op_inject_freg)(mdata_t *, uint64_t);
	int	(*op_inject_icache)(mdata_t *);
	int	(*op_inject_internal)(mdata_t *);
	int	(*op_inject_iphys)(mdata_t *);
	int	(*op_inject_ireg)(mdata_t *, uint64_t);
	int	(*op_inject_l2cache)(mdata_t *);
	int	(*op_inject_l2phys)(mdata_t *);
	int	(*op_inject_l3cache)(mdata_t *);
	int	(*op_inject_l3phys)(mdata_t *);
	int	(*op_inject_tlb)(mdata_t *);

	/* common support ops vectors */
	int	(*op_access_freg)(mdata_t *, uint64_t);
	int	(*op_access_ireg)(mdata_t *, uint64_t);
	int	(*op_check_esrs)(mdata_t *, char *);
	int	(*op_enable_errors)(mdata_t *);
	int	(*op_control_scrub)(mdata_t *, uint64_t);
	int	(*op_get_cpu_info)(cpu_info_t *);
	int	(*op_flushall_caches)(cpu_info_t *);
	int	(*op_flushall_dc)(cpu_info_t *);
	int	(*op_flushall_ic)(cpu_info_t *);
	int	(*op_flushall_l2)(cpu_info_t *);
	int	(*op_flushall_l3)(cpu_info_t *);
	int	(*op_flush_dc_entry)(cpu_info_t *, caddr_t);
	int	(*op_flush_ic_entry)(cpu_info_t *, caddr_t);
	int	(*op_flush_l2_entry)(cpu_info_t *, caddr_t);
	int	(*op_flush_l3_entry)(cpu_info_t *, caddr_t);
} opsvec_c_t;

/*
 * Each sun4u CPU may require unique routines to support common operations.
 * This is the sun4u operations vector (opsvec) table used to call processor
 * specific routines with a minimum of overhead.
 *
 * Routines whose names begin with "op_write_" are typically C routines,
 * while those beginning with "op_wr_" are typically assembly routines.
 */
typedef	struct sun4u_opsvec {
	/* sun4u injection ops vectors */
	int	(*op_inject_dtphys)(mdata_t *);
	int	(*op_inject_itphys)(mdata_t *);
	int	(*op_inject_l2tphys)(mdata_t *);
	int	(*op_inject_memory)(mdata_t *, uint64_t, caddr_t, uint_t);
	int	(*op_inject_mtag)(mdata_t *, uint64_t, caddr_t);
	int	(*op_inject_pc)(mdata_t *);	/* P-Cache Parity error */

	/* sun4u support ops vectors */
	int	(*op_gen_ecc)(uint64_t);
} opsvec_u_t;

/*
 * Each sun4v CPU may require unique routines to support common operations.
 * This is the operations vector (opsvec) table used to call the processor
 * specific routines with a minimum of overhead.
 *
 * Routines whose names begin with "op_inject_" are typically C routines,
 * while those beginning with "op_inj_" are typically assembly routines.
 */
typedef	struct sun4v_opsvec {
	/* sun4v injection ops vectors */
	int	(*op_inject_hvdcache)(mdata_t *);
	int	(*op_inject_hvicache)(mdata_t *);
	int	(*op_inject_l2dir)(mdata_t *);
	int	(*op_inject_l2nd)(mdata_t *);
	int	(*op_inject_l2vad)(mdata_t *);
	int	(*op_inject_ma_memory)(mdata_t *);
	int	(*op_inject_memnd)(mdata_t *);
	int	(*op_inject_memory)(mdata_t *);

	/* sun4v support ops vectors */
	int	(*op_access_cwq)(mdata_t *, uint_t, uint_t);
	int	(*op_access_ma)(mdata_t *, uint_t, uint_t);
	int	(*op_check_l2_idx_mode)(uint64_t, uint64_t *);
	int	(*op_flushall_l2_hvmode)(cpu_info_t *);
	int	(*op_flush_l2_entry_hvmode)(cpu_info_t *, caddr_t);
} opsvec_v_t;

/*
 * Each CPU type supports a different set of commands.  A cpu specific
 * commands array is used to map the user command request to the
 * appropriate test routine.
 */
typedef struct cmd {
	uint64_t	c_command;		/* encoded command */
	int		(*c_func)(mdata_t *);	/* function to call */
	char		*c_fname;		/* function name */
} cmd_t;

/*
 * The scrub struct is used to support the memory/L2 scrub enable/disable
 * opsvec routine.  It keeps track of the system HW scrub registers.
 */
typedef	struct scrub_info {
	uint64_t	s_l2_offset;	/* offset into the L2 cache */
	uint64_t	s_l2cr_addr;	/* L2 scrubber CR address */
	uint64_t	s_l2cr_value;	/* L2 scrubber register value */
	uint64_t	s_mem_offset;	/* offset into DRAM */
	uint64_t	s_memcr_addr;	/* mem scrubber CR address */
	uint64_t	s_memcr_value;	/* mem scrubber register value */
	uint64_t	s_memfcr_addr;	/* mem scrubber freq CR address */
	uint64_t	s_memfcr_value;	/* mem scrubber freq register value */
} scrub_info_t;

#define	MDATA_SCRUB_DISABLE	0x01
#define	MDATA_SCRUB_ENABLE	0x02
#define	MDATA_SCRUB_RESTORE	0x04

#define	MDATA_SCRUB_L2		0x10
#define	MDATA_SCRUB_DRAM	0x20

/*
 * A linked list of these structures is used to keep track of
 * user pages that have been locked and need to be unlocked.
 */
typedef struct uplock {
	struct as	*p_asp;		/* address space of locked pages */
	caddr_t		p_uvaddr;	/* virtual address of locked pages */
	proc_t		*p_procp;	/* process pointer of locked pages */
	int		p_size;		/* size of locked area */
	page_t		**p_pplist;	/* list of pages that are locked */
	struct uplock	*p_next;	/* next struct in linked list */
	struct uplock	*p_prev;	/* prev struct in linked list */
} uplock_t;

/*
 * A linked list of these structures is used to keep track of
 * kernel pages that have been locked and need to be unlocked.
 */
typedef struct kplock {
	uint64_t	k_paddr;	/* physical address of locked pages */
	int		k_npages;	/* number of pages locked */
	struct kplock	*k_next;	/* next struct in linked list */
	struct kplock	*k_prev;	/* prev struct in linked list */
} kplock_t;

/*
 * A linked list of these structures is used to keep track of
 * kernel mappings that have been set up and need to be released.
 *
 * Currently nothing larger than a PAGESIZE page is mapped.
 */
typedef struct kmap {
	caddr_t		k_kvaddr;	/* virtual address of locked pages */
	int		k_size;		/* size of locked area */
	int		k_lk_upgrade;	/* page lock was upgraded? */
	page_t		*k_lockpp;	/* page ptr for locked kernel page */
	struct kmap	*k_next;	/* next struct in linked list */
	struct kmap	*k_prev;	/* prev struct in linked list */
} kmap_t;

/*
 * This is the per unit driver data structure.
 */
typedef struct memtest {
	dev_info_t	*m_dip;
	kmutex_t	m_mutex;
	kcondvar_t	m_cv;
	uchar_t		m_open;
	uplock_t	*m_uplockp;
	kplock_t	*m_kplockp;
	kmap_t		*m_kmapp;
	system_info_t	*m_sip;
	cpu_info_t	*m_cip;
	ioc_t		*m_iocp;
	mdata_t		*m_mdatap[MAX_NTHREADS];
} memtest_t;

/*
 * The following are the common definitions for the memtest_flags global var
 * (declared in the common file memtest.c) which controls certain internal
 * operations of the error injector.
 */
#define	MFLAGS_CHECK_ESRS_PRE_TEST1		0x01
#define	MFLAGS_CHECK_ESRS_PRE_TEST2		0x02
#define	MFLAGS_CHECK_ESRS_MEMORY_ERROR		0x04
#define	MFLAGS_CHECK_ESRS_L2CACHE_ERROR		0x08
#define	MFLAGS_DISABLE_ESR_CLEAR		0x10
#define	MFLAGS_COMMON_MAX			MFLAGS_DISABLE_ESR_CLEAR

#define	MFLAGS_DEFAULT			(MFLAGS_CHECK_ESRS_PRE_TEST1 | \
						MFLAGS_CHECK_ESRS_PRE_TEST2)

/*
 * These global flags control certain internal operations of the injector.
 */
extern	uint_t	memtest_flags;
extern	uint_t	memtest_debug;

/*
 * Common routines located in the kernel.
 * (Not found in any kernel header files.)
 */
extern	void	start_cpus();
extern	void	cpu_flush_ecache(void);
extern	void	flush_instr_mem(caddr_t, size_t);
extern	uint_t	getpstate(void);
extern	void	setpstate(uint_t);

/*
 * Kernel variables (not found in any header files).
 */
extern	struct	cpu *cpus;

/*
 * Test routines located in memtest.c
 */
extern	int		memtest_copyin_l2_err(mdata_t *);
extern	int		memtest_cphys(mdata_t *, int (*func)(mdata_t *),
					char *);
extern	int		memtest_internal_err(mdata_t *);

extern	int		memtest_dphys(mdata_t *);
extern	int		memtest_iphys(mdata_t *);
extern	int		memtest_l2phys(mdata_t *);

extern	int		memtest_k_bus_err(mdata_t *);
extern	int		memtest_k_cp_err(mdata_t *);
extern	int		memtest_k_dc_err(mdata_t *);
extern	int		memtest_k_ic_err(mdata_t *);
extern	int		memtest_k_l2_err(mdata_t *);
extern	int		memtest_k_l2virt(mdata_t *);
extern	int		memtest_k_l2wb_err(mdata_t *);
extern	int		memtest_k_mem_err(mdata_t *);
extern	int		memtest_k_mvirt(mdata_t *);
extern	int		memtest_k_tlb_err(mdata_t *);
extern	int		memtest_mphys(mdata_t *);
extern	int		memtest_obp_err(mdata_t *);
extern	int		memtest_u_cmn_err(mdata_t *, int (*func)(mdata_t *),
					char *);
extern	int		memtest_u_l2_err(mdata_t *);
extern	int		memtest_u_mem_err(mdata_t *);
extern	int		memtest_u_mvirt(mdata_t *);

/* Second level test routines. */
extern	int		memtest_inject_dcache(mdata_t *);
extern	int		memtest_inject_dphys(mdata_t *);
extern	int		memtest_inject_icache(mdata_t *);
extern	int		memtest_inject_iphys(mdata_t *);
extern	int		memtest_inject_l2cache(mdata_t *);
extern	int		memtest_inject_l2phys(mdata_t *);

/*
 * Support routines located in memtest.c.
 */
extern	int		memtest_bind_thread(mdata_t *);
extern	int		memtest_check_cpu_status(cpu_t *);
extern	int		memtest_check_esrs(mdata_t *, char *);
extern	void		memtest_check_esrs_xcfunc(uint64_t arg1, uint64_t arg2);
extern	int		memtest_do_cmd(mdata_t *);
extern	void		memtest_dprintf(int, char *, ...);
#define	DPRINTF		memtest_dprintf
extern	void		memtest_dump_mdata(mdata_t *, caddr_t);
extern	int		memtest_enable_errors(mdata_t *);
extern	int		memtest_free_kernel_memory(mdata_t *, uint64_t);
extern	int		memtest_get_a_offset(ioc_t *);
extern	int		memtest_get_c_offset(ioc_t *);
extern	int		memtest_get_tte(struct hat *, caddr_t, tte_t *);
extern	int		memtest_init(mdata_t *);
extern	int		memtest_init_threads(mdata_t *);
extern	void		memtest_init_thread_xcfunc(uint64_t, uint64_t);
extern	int		memtest_k_mpeekpoke(mdata_t *);
extern	int		memtest_k_vpeekpoke(mdata_t *);
extern	uint64_t	memtest_kva_to_pa(void *);
extern	int		memtest_lock_user_pages(memtest_t *, caddr_t,
					int, proc_t *);
extern	caddr_t		memtest_map_p2kvaddr(mdata_t *, uint64_t, int,
					uint64_t, uint_t);
extern	caddr_t		memtest_map_u2kvaddr(mdata_t *, caddr_t, uint64_t,
					uint_t, uint_t);
extern	uint64_t	memtest_mem_request(memtest_t *, uint64_t *, uint64_t *,
					int, int);
extern	int		memtest_offline_cpus(mdata_t *);
extern	int		memtest_online_cpu(mdata_t *, struct cpu *);
extern	int		memtest_online_cpus(mdata_t *);
extern	int		memtest_popc64(uint64_t);
extern	int		memtest_post_test(mdata_t *);
extern	int		memtest_pre_init_threads(mdata_t *);
extern	int		memtest_pre_test(mdata_t *);
extern	void		memtest_prefetch_access(ioc_t *, caddr_t);
extern	int		memtest_start_thread(mdata_t *, void (*)(), char *);
extern	int		memtest_unlock_kernel_pages(kplock_t *);
extern	int		memtest_unlock_user_pages(uplock_t *);
extern	uint64_t	memtest_uva_to_pa(caddr_t, proc_t *);
extern	int		memtest_wait_sync(volatile int *, int, int, char *);
extern	int		notimp(/* typeless */);
extern	int		notsup(/* typeless */);
extern	int		memtest_xc_cpus(mdata_t *, void (*func)(uint64_t,
				uint64_t), char *);

/*
 * The following routines have separate versions one for sun4u in the
 * memtest_u.c file and one for sun4v in the memtest_v.c file.
 *
 * Each architecture sun4u/sun4v will pick up it's own version of the
 * routine because of how the makefiles are setup.
 *
 * These protos are placed here so that the common code can call these
 * routines without differentiating between sun4u and sun4v.
 */

/* Test routines */
extern	int		memtest_inject_memory(mdata_t *);

/* Support routines */
extern	int		memtest_arch_mreq(mem_req_t *);
extern	int		memtest_cmp_quiesce(mdata_t *);
extern	int		memtest_cmp_unquiesce(mdata_t *);
extern	int		memtest_check_command(uint64_t);
extern	int		memtest_cpu_init(mdata_t *);
extern	int		memtest_get_cpu_info(mdata_t *);
extern	uint64_t	memtest_get_cpu_ver(void);
extern	int		memtest_hv_diag_svc_check(void);
extern	int		memtest_idx_to_paddr(memtest_t *, uint64_t *,
					uint64_t *, uint64_t, uint_t, int);
extern	int		memtest_is_local_mem(mdata_t *, uint64_t);
extern	int		memtest_pre_test_kernel(mdata_t *);
extern	int		memtest_pre_test_nomode(mdata_t *);
extern	int		memtest_pre_test_user(mdata_t *);
extern	int		memtest_post_test_kernel(mdata_t *);
extern	int		memtest_set_scrubbers(mdata_t *);
extern	int		memtest_restore_scrubbers(mdata_t *);

/*
 * Routines located in memtest_asm.s.
 */
extern	void		memtest_asmld(caddr_t);
extern	void		memtest_asmld_quick(caddr_t);
extern	void		memtest_asmldx_quick(caddr_t);
extern	void		memtest_asmldst(caddr_t, caddr_t, int);
extern	void		memtest_asmld_tl1(caddr_t);
extern	void		memtest_asmst_tl1(caddr_t, uchar_t);
extern	void		memtest_blkld(caddr_t);
extern	void		memtest_blkld_tl1(caddr_t);
extern	void		memtest_disable_intrs(void);
extern	void		memtest_enable_intrs(void);
extern	uint32_t	memtest_get_uval(uint64_t);
extern	void		memtest_pcrel(void);
extern	void		memtest_prefetch_rd_access(caddr_t);
extern	void		memtest_prefetch_wr_access(caddr_t);

extern	uint8_t		peek_asi8(int, uint64_t);
extern	uint16_t	peek_asi16(int, uint64_t);
extern	uint32_t	peek_asi32(int, uint64_t);
extern	uint64_t	peek_asi64(int, uint64_t);
extern	void		poke_asi8(int, uint64_t, uint8_t);
extern	void		poke_asi16(int, uint64_t, uint16_t);
extern	void		poke_asi32(int, uint64_t, uint32_t);
extern	void		poke_asi64(int, uint64_t, uint64_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_H */

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
 * Copyright (c) 1993, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_MACHSYSTM_H
#define	_SYS_MACHSYSTM_H

/*
 * Numerous platform-dependent interfaces that don't seem to belong
 * in any other header file.
 *
 * This file should not be included by code that purports to be
 * platform-independent.
 */

#ifndef _ASM
#include <sys/types.h>
#include <sys/scb.h>
#include <sys/varargs.h>
#include <sys/machparam.h>
#include <sys/thread.h>
#include <vm/seg_enum.h>
#include <sys/processor.h>
#include <sys/sunddi.h>
#include <sys/memlist.h>
#include <sys/async.h>
#include <sys/errorq.h>
#endif /* _ASM */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL

#ifndef _ASM
/*
 * The following enum types determine how interrupts are distributed
 * on a sun4u system.
 */
enum intr_policies {
	/*
	 * Target interrupt at the CPU running the add_intrspec
	 * thread. Also used to target all interrupts at the panicking
	 * CPU.
	 */
	INTR_CURRENT_CPU = 0,

	/*
	 * Target all interrupts at the boot cpu
	 */
	INTR_BOOT_CPU,

	/*
	 * Flat distribution of all interrupts
	 */
	INTR_FLAT_DIST,

	/*
	 * Weighted distribution of all interrupts
	 */
	INTR_WEIGHTED_DIST
};


/*
 * Structure that defines the interrupt distribution list. It contains
 * enough info about the interrupt so that it can callback the parent
 * nexus driver and retarget the interrupt to a different CPU.
 */
struct intr_dist {
	struct intr_dist *next;	/* link to next in list */
	void (*func)(void *);	/* Callback function */
	void *arg;		/* Nexus parent callback arg 1 */
};

/*
 * Miscellaneous cpu_state changes
 */
extern void power_down(const char *);
extern void do_shutdown(void);

/*
 * Number of seconds until power is shut off
 */
extern int thermal_powerdown_delay;


/*
 * prom-related
 */
extern int obpdebug;
extern int forthdebug_supported;
extern uint_t tba_taken_over;
extern void forthdebug_init(void);
extern void init_vx_handler(void);
extern void kern_preprom(void);
extern void kern_postprom(void);

/*
 * externally (debugger or prom) initiated panic
 */
extern struct regs sync_reg_buf;
extern uint64_t sync_tt;
extern void sync_handler(void);

/*
 * Trap-related
 */
struct regs;
extern void trap(struct regs *rp, caddr_t addr, uint32_t type,
    uint32_t mmu_fsr);
extern void *get_tba(void);
extern void *set_tba(void *);
extern caddr_t set_trap_table(void);
extern struct scb trap_table;

struct panic_trap_info {
	struct regs *trap_regs;
	uint_t	trap_type;
	caddr_t trap_addr;
	uint_t	trap_mmu_fsr;
};

/*
 * misc. primitives
 */
extern void debug_flush_windows(void);
extern void flush_windows(void);
extern int getprocessorid(void);
extern void reestablish_curthread(void);

extern void stphys(uint64_t physaddr, int value);
extern int ldphys(uint64_t physaddr);
extern void stdphys(uint64_t physaddr, uint64_t value);
extern uint64_t lddphys(uint64_t physaddr);

extern void stphysio(u_longlong_t physaddr, uint_t value);
extern uint_t ldphysio(u_longlong_t physaddr);
extern void sthphysio(u_longlong_t physaddr, ushort_t value);
extern ushort_t ldhphysio(u_longlong_t physaddr);
extern void stbphysio(u_longlong_t physaddr, uchar_t value);
extern uchar_t ldbphysio(u_longlong_t physaddr);
extern void stdphysio(u_longlong_t physaddr, u_longlong_t value);
extern u_longlong_t lddphysio(u_longlong_t physaddr);

extern int pf_is_dmacapable(pfn_t);

extern int dip_to_cpu_id(dev_info_t *dip, processorid_t *cpu_id);

extern void set_cmp_error_steering(void);

/*
 * SPARCv9 %ver register and field definitions
 */

#define	ULTRA_VER_MANUF(x)	((x) >> 48)
#define	ULTRA_VER_IMPL(x)	(((x) >> 32) & 0xFFFF)
#define	ULTRA_VER_MASK(x)	(((x) >> 24) & 0xFF)

extern uint64_t ultra_getver(void);

/*
 * bootup-time
 */
extern int ncpunode;
extern int niobus;

extern void segnf_init(void);
extern void kern_setup1(void);
extern void startup(void);
extern void post_startup(void);
extern void install_va_to_tte(void);
extern void setwstate(uint_t);
extern void create_va_to_tte(void);
extern int memscrub_init(void);

extern void kcpc_hw_init(void);
extern void kcpc_hw_startup_cpu(ushort_t);
extern int kcpc_hw_load_pcbe(void);

/*
 * Interrupts
 */
struct cpu;
extern struct cpu cpu0;
extern struct scb *set_tbr(struct scb *);

extern uint_t disable_vec_intr(void);
extern void enable_vec_intr(uint_t);
extern void setintrenable(int);

extern void intr_dist_add(void (*f)(void *), void *);
extern void intr_dist_rem(void (*f)(void *), void *);
extern void intr_dist_add_weighted(void (*f)(void *, int32_t, int32_t), void *);
extern void intr_dist_rem_weighted(void (*f)(void *, int32_t, int32_t), void *);

extern uint32_t intr_dist_cpuid(void);

void intr_dist_cpuid_add_device_weight(uint32_t cpuid, dev_info_t *dip,
		int32_t weight);
void intr_dist_cpuid_rem_device_weight(uint32_t cpuid, dev_info_t *dip);

extern void intr_redist_all_cpus(void);
extern void intr_redist_all_cpus_shutdown(void);

extern void send_dirint(int, int);
extern void setsoftint(uint64_t);
extern void setsoftint_tl1(uint64_t, uint64_t);
extern void siron(void);
extern void sir_on(int);
extern uint64_t getidsr(void);
extern void intr_enqueue_req(uint_t pil, uint64_t inum);
extern void intr_dequeue_req(uint_t pil, uint64_t inum);
extern void wr_clr_softint(uint_t);

/*
 * Time- and %tick-related
 */
extern hrtime_t rdtick(void);
extern void tick_write_delta(uint64_t);
extern void tickcmpr_set(uint64_t);
extern void tickcmpr_reset(void);
extern void tickcmpr_disable(void);
extern int tickcmpr_disabled(void);
extern uint64_t cbe_level14_inum;

/*
 * Contiguos Memory
 */
extern void *contig_mem_alloc(size_t);
extern void *contig_mem_alloc_align(size_t, size_t);
extern void contig_mem_free(void *, size_t);

/*
 * Caches
 */
extern int vac;
extern int cache;
extern int use_mp;
extern uint_t vac_mask;
extern uint64_t ecache_flushaddr;
extern int dcache_size;		/* Maximum dcache size */
extern int dcache_linesize;	/* Minimum dcache linesize */
extern int icache_size;		/* Maximum icache size */
extern int icache_linesize;	/* Minimum icache linesize */
extern int ecache_alignsize;	/* Maximum ecache linesize for struct align */
extern int ecache_size;		/* Maximum ecache size */
extern int ecache_associativity;	/* ecache associativity */
extern int ecache_setsize;	/* Maximum ecache setsize possible */
extern int cpu_setsize;		/* Maximum ecache setsize of configured cpus */

/*
 * VM
 */
extern int do_pg_coloring;
extern int use_page_coloring;
extern uint_t vac_colors_mask;

extern int ndata_alloc_page_freelists(struct memlist *, int);
extern int ndata_alloc_dmv(struct memlist *);
extern int ndata_alloc_tsbs(struct memlist *, pgcnt_t);
extern int ndata_alloc_hat(struct memlist *);
extern int ndata_alloc_kpm(struct memlist *, pgcnt_t);
extern int ndata_alloc_page_mutexs(struct memlist *ndata);

extern size_t calc_pp_sz(pgcnt_t);
extern size_t calc_kpmpp_sz(pgcnt_t);
extern size_t calc_hmehash_sz(pgcnt_t);
extern size_t calc_pagehash_sz(pgcnt_t);
extern size_t calc_free_pagelist_sz(void);

extern caddr_t alloc_hmehash(caddr_t);
extern caddr_t alloc_page_freelists(caddr_t);

extern size_t page_ctrs_sz(void);
extern caddr_t page_ctrs_alloc(caddr_t);
extern void page_freelist_coalesce_all(int);
extern void ppmapinit(void);
extern void hwblkpagecopy(const void *, void *);
extern void hw_pa_bcopy32(uint64_t, uint64_t);

extern int pp_slots;
extern int pp_consistent_coloring;

/*
 * ppcopy/hwblkpagecopy interaction.  See ppage.c.
 */
#define	PPAGE_STORE_VCOLORING	0x1 /* use vcolors to maintain consistency */
#define	PPAGE_LOAD_VCOLORING	0x2 /* use vcolors to maintain consistency */
#define	PPAGE_STORES_POLLUTE	0x4 /* stores pollute VAC */
#define	PPAGE_LOADS_POLLUTE	0x8 /* loads pollute VAC */

/*
 * VIS-accelerated copy/zero
 */
extern int use_hw_bcopy;
extern uint_t hw_copy_limit_1;
extern uint_t hw_copy_limit_2;
extern uint_t hw_copy_limit_4;
extern uint_t hw_copy_limit_8;
extern int use_hw_bzero;

#ifdef CHEETAH
#define	VIS_COPY_THRESHOLD 256
#else
#define	VIS_COPY_THRESHOLD 900
#endif

/*
 * MP
 */
extern void idle_other_cpus(void);
extern void resume_other_cpus(void);
extern void stop_other_cpus(void);
extern void idle_stop_xcall(void);
extern void plat_idle_enter(processorid_t);
extern void plat_idle_exit(processorid_t);
extern void mp_cpu_quiesce(struct cpu *);

/*
 * Error handling
 */
extern void set_error_enable(uint64_t neer);
extern void set_error_enable_tl1(uint64_t neer, uint64_t action);
extern uint64_t get_error_enable(void);
extern void get_asyncflt(uint64_t *afsr);
extern void set_asyncflt(uint64_t afsr);
extern void get_asyncaddr(uint64_t *afar);
extern void scrubphys(uint64_t paddr, int ecache_size);
extern void clearphys(uint64_t paddr, int ecache_size, int ecache_linesize);
extern void flushecacheline(uint64_t paddr, int ecache_size);
extern int ce_scrub_xdiag_recirc(struct async_flt *, errorq_t *,
    errorq_elem_t *, size_t);
extern char *flt_to_error_type(struct async_flt *);

/*
 * Panic at TL > 0
 */
extern uint64_t cpu_pa[];
extern void ptl1_init_cpu(struct cpu *);

/*
 * Constants which define the "hole" in the 64-bit sfmmu address space.
 * These are set to specific values by the CPU module code.
 */
extern caddr_t	hole_start, hole_end;

/* kpm mapping window */
extern size_t	kpm_size;
extern uchar_t	kpm_size_shift;
extern caddr_t	kpm_vbase;

#define	INVALID_VADDR(a)	(((a) >= hole_start && (a) < hole_end))

extern void adjust_hw_copy_limits(int);

#endif /* _ASM */

/*
 * Actions for set_error_enable_tl1
 */
#define	EER_SET_ABSOLUTE	0x0
#define	EER_SET_SETBITS		0x1
#define	EER_SET_CLRBITS		0x2

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_MACHSYSTM_H */

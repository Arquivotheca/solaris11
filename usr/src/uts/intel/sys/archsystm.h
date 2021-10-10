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

#ifndef _SYS_ARCHSYSTM_H
#define	_SYS_ARCHSYSTM_H

/*
 * A selection of ISA-dependent interfaces
 */

#include <vm/seg_enum.h>
#include <vm/page.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL

extern greg_t getfp(void);
extern int getpil(void);

extern ulong_t getcr0(void);
extern void setcr0(ulong_t);
extern ulong_t getcr8(void);
extern void setcr8(ulong_t);
extern ulong_t getcr2(void);
extern void clflush_insn(caddr_t addr);
extern void mfence_insn(void);

extern void patch_xsave(void);
extern void patch_xsaveopt(void);

extern void cli(void);
extern void sti(void);

extern void tenmicrosec(void);

extern void restore_int_flag(ulong_t);
extern void intr_restore(ulong_t);
extern ulong_t clear_int_flag(void);
extern ulong_t intr_clear(void);
extern ulong_t getflags(void);
extern int interrupts_enabled(void);

extern void int3(void);
extern void int18(void);
extern void int20(void);
extern void int_cmci(void);

extern void sys_syscall();
extern void sys_syscall32();
extern void sys_lcall32();
extern void sys_syscall_int();
extern void brand_sys_syscall();
extern void brand_sys_syscall32();
extern void brand_sys_syscall_int();
extern int update_sregs();
extern void reset_sregs();
extern void sys_sysenter();
extern void _sys_sysenter_post_swapgs();
extern void brand_sys_sysenter();
extern void _brand_sys_sysenter_post_swapgs();

extern void dosyscall(void);

extern void bind_hwcap(void);

extern uint16_t inw(int port);
extern uint32_t inl(int port);
extern void outw(int port, uint16_t value);
extern void outl(int port, uint32_t value);

extern void pc_reset(void) __NORETURN;
extern void efi_reset(void) __NORETURN;
extern void reset(void) __NORETURN;
extern int goany(void);

extern void setgregs(klwp_t *, gregset_t);
extern void getgregs(klwp_t *, gregset_t);
extern void setfpregs(klwp_t *, fpregset_t *);
extern void getfpregs(klwp_t *, fpregset_t *);

#if defined(_SYSCALL32_IMPL)
extern void getgregs32(klwp_t *, gregset32_t);
extern void setfpregs32(klwp_t *, fpregset32_t *);
extern void getfpregs32(klwp_t *, fpregset32_t *);
#endif

struct ucontext;
extern	void	xregs_clrptr(klwp_t *, struct ucontext *);
extern	int	xregs_hasptr(klwp_t *, struct ucontext *);
extern	caddr_t	xregs_getptr(klwp_t *, struct ucontext *);
extern	void	xregs_setptr(klwp_t *, struct ucontext *, caddr_t);

#ifdef _SYSCALL32_IMPL
struct	ucontext32;
extern	void	xregs_clrptr32(klwp_t *, struct ucontext32 *);
extern	int	xregs_hasptr32(klwp_t *, struct ucontext32 *);
extern	caddr32_t xregs_getptr32(klwp_t *, struct ucontext32 *);
extern	void	xregs_setptr32(klwp_t *, struct ucontext32 *, caddr32_t);
#endif /* _SYSCALL32_IMPL */

extern	void	xregs_getfpregs(klwp_t *, caddr_t);
extern	void	xregs_get(klwp_t *, caddr_t);
extern	void	xregs_setfpregs(klwp_t *, caddr_t);
extern	void	xregs_set(klwp_t *, caddr_t);
extern	int	xregs_getsize(struct proc *);
extern	int	xregs_isvalid(caddr_t);

struct fpu_ctx;

extern void fp_free(struct fpu_ctx *, int);
extern void fp_save(struct fpu_ctx *);
extern void fp_restore(struct fpu_ctx *);

extern int fpu_pentium_fdivbug;

extern void sep_save(void *);
extern void sep_restore(void *);

extern void brand_interpositioning_enable(void);
extern void brand_interpositioning_disable(void);

struct regs;

extern int instr_size(struct regs *, caddr_t *, enum seg_rw);

extern int enable_cbcp; /* patchable in /etc/system */

extern uint_t cpu_hwcap_flags;
extern uint_t cpu_freq;
extern uint64_t cpu_freq_hz;

extern int use_sse_pagecopy;
extern int use_sse_pagezero;
extern int use_sse_copy;

extern caddr_t i86devmap(pfn_t, pgcnt_t, uint_t);
extern page_t *page_numtopp_alloc(pfn_t pfnum);

extern void hwblkclr(void *, size_t);
extern void hwblkpagecopy(const void *, void *);
extern void page_copy_no_xmm(void *dst, void *src);
extern void block_zero_no_xmm(void *dst, int len);
#define	BLOCKZEROALIGN (4 * sizeof (void *))

extern void (*kcpc_hw_enable_cpc_intr)(void);

extern void init_desctbls(void);

extern user_desc_t *cpu_get_gdt(void);

extern void switch_sp_and_call(void *, void (*)(uint_t, uint_t), uint_t,
	uint_t);
extern hrtime_t (*gethrtimef)(void);
extern hrtime_t (*gethrtimeunscaledf)(void);
extern void (*scalehrtimef)(hrtime_t *);
extern uint64_t (*unscalehrtimef)(hrtime_t);
extern void (*gethrestimef)(timestruc_t *);

extern void av_dispatch_softvect(uint_t);
extern void av_dispatch_autovect(uint_t);
extern uint_t atomic_btr32(uint32_t *, uint_t);
extern uint_t bsrw_insn(uint16_t);
extern int sys_rtt_common(struct regs *);
extern void fakesoftint(ulong_t);

extern void *plat_traceback(void *);

#if defined(__xpv)
extern void xen_init_callbacks(void);
extern void xen_set_callback(void (*)(void), uint_t, uint_t);
extern void xen_printf(const char *, ...);
#define	cpr_dprintf xen_printf
#else
extern void setup_mca(void);
extern void pat_sync(void);
extern void patch_tsc_read(int);
#if !defined(__xpv)
extern void patch_memops(uint_t);
#endif	/* !defined(__xpv) */
extern void setup_xfem(void);
#define	cpr_dprintf prom_printf
#endif

#endif /* _KERNEL */

#if defined(_KERNEL) || defined(_BOOT)
extern uint8_t inb(int port);
extern void outb(int port, uint8_t value);
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_ARCHSYSTM_H */

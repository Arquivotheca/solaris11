
/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 1992 Terrence R. Lambert.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)machdep.c	7.4 (Berkeley) 6/3/91
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/tss.h>
#include <sys/segments.h>
#include <sys/trap.h>
#include <sys/cpuvar.h>
#include <sys/bootconf.h>
#include <sys/x86_archext.h>
#include <sys/controlregs.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/kobj.h>
#include <sys/cmn_err.h>
#include <sys/reboot.h>
#include <sys/kdi.h>
#include <sys/mach_mmu.h>
#include <sys/systm.h>
#include <sys/hrt.h>

#ifdef __xpv
#include <sys/hypervisor.h>
#include <vm/as.h>
#endif

#include <sys/promif.h>
#include <sys/bootinfo.h>
#include <vm/kboot_mmu.h>
#include <vm/hat_pte.h>

/*
 * cpu0 and default tables and structures.
 */
user_desc_t	*gdt0;
#if !defined(__xpv)
desctbr_t	gdt0_default_r;
#endif

gate_desc_t	*idt0; 		/* interrupt descriptor table */

struct tss	*ktss0;			/* kernel task state structure */

user_desc_t	zero_udesc;		/* base zero user desc native procs */
user_desc_t	null_udesc;		/* null user descriptor */
system_desc_t	null_sdesc;		/* null system descriptor */

user_desc_t	zero_u32desc;		/* 32-bit compatibility procs */

user_desc_t	ucs_on;
user_desc_t	ucs_off;
user_desc_t	ucs32_on;
user_desc_t	ucs32_off;

#pragma	align	16(dblfault_stack0)
char		dblfault_stack0[DEFAULTSTKSZ];

extern void	fast_null(void);
extern hrtime_t	get_hrtime(void);
extern hrtime_t	gethrvtime(void);
extern hrtime_t	get_hrestime(void);
extern uint64_t	getlgrp(void);
extern void	*gethrt(void);

void (*(fasttable[]))(void) = {
	fast_null,			/* T_FNULL routine */
	fast_null,			/* T_FGETFP routine (initially null) */
	fast_null,			/* T_FSETFP routine (initially null) */
	(void (*)())get_hrtime,		/* T_GETHRTIME */
	(void (*)())gethrvtime,		/* T_GETHRVTIME */
	(void (*)())get_hrestime,	/* T_GETHRESTIME */
	(void (*)())getlgrp,		/* T_GETLGRP */
	(void (*)())gethrt		/* T_GETHRT */
};

/*
 * Structure containing pre-computed descriptors to allow us to temporarily
 * interpose on a standard handler.
 */
struct interposing_handler {
	int ih_inum;
	gate_desc_t ih_interp_desc;
	gate_desc_t ih_default_desc;
};

/*
 * The brand infrastructure interposes on two handlers, and we use one as a
 * NULL signpost.
 */
static struct interposing_handler brand_tbl[2];

/*
 * software prototypes for default local descriptor table
 */

/*
 * Routines for loading segment descriptors in format the hardware
 * can understand.
 */

/*
 * In long mode we have the new L or long mode attribute bit
 * for code segments. Only the conforming bit in type is used along
 * with descriptor priority and present bits. Default operand size must
 * be zero when in long mode. In 32-bit compatibility mode all fields
 * are treated as in legacy mode. For data segments while in long mode
 * only the present bit is loaded.
 */
void
set_usegd(user_desc_t *dp, uint_t lmode, void *base, size_t size,
    uint_t type, uint_t dpl, uint_t gran, uint_t defopsz)
{
	ASSERT(lmode == SDP_SHORT || lmode == SDP_LONG);

	/*
	 * 64-bit long mode.
	 */
	if (lmode == SDP_LONG)
		dp->usd_def32 = 0;		/* 32-bit operands only */
	else
		/*
		 * 32-bit compatibility mode.
		 */
		dp->usd_def32 = defopsz;	/* 0 = 16, 1 = 32-bit ops */

	dp->usd_long = lmode;	/* 64-bit mode */
	dp->usd_type = type;
	dp->usd_dpl = dpl;
	dp->usd_p = 1;
	dp->usd_gran = gran;		/* 0 = bytes, 1 = pages */

	dp->usd_lobase = (uintptr_t)base;
	dp->usd_midbase = (uintptr_t)base >> 16;
	dp->usd_hibase = (uintptr_t)base >> (16 + 8);
	dp->usd_lolimit = size;
	dp->usd_hilimit = (uintptr_t)size >> 16;
}

/*
 * Install system segment descriptor for LDT and TSS segments.
 */

void
set_syssegd(system_desc_t *dp, void *base, size_t size, uint_t type,
    uint_t dpl)
{
	dp->ssd_lolimit = size;
	dp->ssd_hilimit = (uintptr_t)size >> 16;

	dp->ssd_lobase = (uintptr_t)base;
	dp->ssd_midbase = (uintptr_t)base >> 16;
	dp->ssd_hibase = (uintptr_t)base >> (16 + 8);
	dp->ssd_hi64base = (uintptr_t)base >> (16 + 8 + 8);

	dp->ssd_type = type;
	dp->ssd_zero1 = 0;	/* must be zero */
	dp->ssd_zero2 = 0;
	dp->ssd_dpl = dpl;
	dp->ssd_p = 1;
	dp->ssd_gran = 0;	/* force byte units */
}

void *
get_ssd_base(system_desc_t *dp)
{
	uintptr_t	base;

	base = (uintptr_t)dp->ssd_lobase |
	    (uintptr_t)dp->ssd_midbase << 16 |
	    (uintptr_t)dp->ssd_hibase << (16 + 8) |
	    (uintptr_t)dp->ssd_hi64base << (16 + 8 + 8);
	return ((void *)base);
}

/*
 * Install gate segment descriptor for interrupt, trap, call and task gates.
 */

/*ARGSUSED*/
void
set_gatesegd(gate_desc_t *dp, void (*func)(void), selector_t sel,
    uint_t type, uint_t dpl, uint_t vector)
{
	dp->sgd_looffset = (uintptr_t)func;
	dp->sgd_hioffset = (uintptr_t)func >> 16;
	dp->sgd_hi64offset = (uintptr_t)func >> (16 + 16);

	dp->sgd_selector =  (uint16_t)sel;

	/*
	 * For 64 bit native we use the IST stack mechanism
	 * for double faults. All other traps use the CPL = 0
	 * (tss_rsp0) stack.
	 */
#if !defined(__xpv)
	if (vector == T_DBLFLT)
		dp->sgd_ist = 1;
	else
#endif
		dp->sgd_ist = 0;

	dp->sgd_type = type;
	dp->sgd_dpl = dpl;
	dp->sgd_p = 1;
}

/*
 * Updates a single user descriptor in the the GDT of the current cpu.
 * Caller is responsible for preventing cpu migration.
 */

void
gdt_update_usegd(uint_t sidx, user_desc_t *udp)
{
#if defined(__xpv)

	uint64_t dpa = CPU->cpu_m.mcpu_gdtpa + sizeof (*udp) * sidx;

	if (HYPERVISOR_update_descriptor(pa_to_ma(dpa), *(uint64_t *)udp))
		panic("gdt_update_usegd: HYPERVISOR_update_descriptor");

#else	/* __xpv */

	CPU->cpu_gdt[sidx] = *udp;

#endif	/* __xpv */
}

/*
 * Writes single descriptor pointed to by udp into a processes
 * LDT entry pointed to by ldp.
 */
int
ldt_update_segd(user_desc_t *ldp, user_desc_t *udp)
{
#if defined(__xpv)

	uint64_t dpa;

	dpa = mmu_ptob(hat_getpfnum(kas.a_hat, (caddr_t)ldp)) |
	    ((uintptr_t)ldp & PAGEOFFSET);

	/*
	 * The hypervisor is a little more restrictive about what it
	 * supports in the LDT.
	 */
	if (HYPERVISOR_update_descriptor(pa_to_ma(dpa), *(uint64_t *)udp) != 0)
		return (EINVAL);

#else	/* __xpv */

	*ldp = *udp;

#endif	/* __xpv */
	return (0);
}

#if defined(__xpv)

/*
 * Converts hw format gate descriptor into pseudo-IDT format for the hypervisor.
 * Returns true if a valid entry was written.
 */
int
xen_idt_to_trap_info(uint_t vec, gate_desc_t *sgd, void *ti_arg)
{
	trap_info_t *ti = ti_arg;	/* XXPV	Aargh - segments.h comment */

	/*
	 * skip holes in the IDT
	 */
	if (GATESEG_GETOFFSET(sgd) == 0)
		return (0);

	ASSERT(sgd->sgd_type == SDT_SYSIGT);
	ti->vector = vec;
	TI_SET_DPL(ti, sgd->sgd_dpl);

	/*
	 * Is this an interrupt gate?
	 */
	if (sgd->sgd_type == SDT_SYSIGT) {
		/* LINTED */
		TI_SET_IF(ti, 1);
	}
	ti->cs = sgd->sgd_selector;
	ti->cs |= SEL_KPL;	/* force into ring 3. see KCS_SEL  */
	ti->address = GATESEG_GETOFFSET(sgd);
	return (1);
}

/*
 * Convert a single hw format gate descriptor and write it into our virtual IDT.
 */
void
xen_idt_write(gate_desc_t *sgd, uint_t vec)
{
	trap_info_t trapinfo[2];

	bzero(trapinfo, sizeof (trapinfo));
	if (xen_idt_to_trap_info(vec, sgd, &trapinfo[0]) == 0)
		return;
	if (xen_set_trap_table(trapinfo) != 0)
		panic("xen_idt_write: xen_set_trap_table() failed");
}

#endif	/* __xpv */

/*
 * Build kernel GDT.
 */

static void
init_gdt_common(user_desc_t *gdt)
{
	int i;

	/*
	 * 64-bit kernel code segment.
	 */
	set_usegd(&gdt[GDT_KCODE], SDP_LONG, NULL, 0, SDT_MEMERA, SEL_KPL,
	    SDP_PAGES, SDP_OP32);

	/*
	 * 64-bit kernel data segment. The limit attribute is ignored in 64-bit
	 * mode, but we set it here to 0xFFFF so that we can use the SYSRET
	 * instruction to return from system calls back to 32-bit applications.
	 * SYSRET doesn't update the base, limit, or attributes of %ss or %ds
	 * descriptors. We therefore must ensure that the kernel uses something,
	 * though it will be ignored by hardware, that is compatible with 32-bit
	 * apps. For the same reason we must set the default op size of this
	 * descriptor to 32-bit operands.
	 */
	set_usegd(&gdt[GDT_KDATA], SDP_LONG, NULL, -1, SDT_MEMRWA,
	    SEL_KPL, SDP_PAGES, SDP_OP32);
	gdt[GDT_KDATA].usd_def32 = 1;

	/*
	 * 64-bit user code segment.
	 */
	set_usegd(&gdt[GDT_UCODE], SDP_LONG, NULL, 0, SDT_MEMERA, SEL_UPL,
	    SDP_PAGES, SDP_OP32);

	/*
	 * 32-bit user code segment.
	 */
	set_usegd(&gdt[GDT_U32CODE], SDP_SHORT, NULL, -1, SDT_MEMERA,
	    SEL_UPL, SDP_PAGES, SDP_OP32);

	/*
	 * See gdt_ucode32() and gdt_ucode_native().
	 */
	ucs_on = ucs_off = gdt[GDT_UCODE];
	ucs_off.usd_p = 0;	/* forces #np fault */

	ucs32_on = ucs32_off = gdt[GDT_U32CODE];
	ucs32_off.usd_p = 0;	/* forces #np fault */

	/*
	 * 32 and 64 bit data segments can actually share the same descriptor.
	 * In long mode only the present bit is checked but all other fields
	 * are loaded. But in compatibility mode all fields are interpreted
	 * as in legacy mode so they must be set correctly for a 32-bit data
	 * segment.
	 */
	set_usegd(&gdt[GDT_UDATA], SDP_SHORT, NULL, -1, SDT_MEMRWA, SEL_UPL,
	    SDP_PAGES, SDP_OP32);

#if !defined(__xpv)

	/*
	 * The 64-bit kernel has no default LDT. By default, the LDT descriptor
	 * in the GDT is 0.
	 */

	/*
	 * Kernel TSS
	 */
	set_syssegd((system_desc_t *)&gdt[GDT_KTSS], ktss0,
	    sizeof (*ktss0) - 1, SDT_SYSTSS, SEL_KPL);

#endif	/* !__xpv */

	/*
	 * Initialize fs and gs descriptors for 32 bit processes.
	 * Only attributes and limits are initialized, the effective
	 * base address is programmed via fsbase/gsbase.
	 */
	set_usegd(&gdt[GDT_LWPFS], SDP_SHORT, NULL, -1, SDT_MEMRWA,
	    SEL_UPL, SDP_PAGES, SDP_OP32);
	set_usegd(&gdt[GDT_LWPGS], SDP_SHORT, NULL, -1, SDT_MEMRWA,
	    SEL_UPL, SDP_PAGES, SDP_OP32);

	/*
	 * Initialize the descriptors set aside for brand usage.
	 * Only attributes and limits are initialized.
	 */
	for (i = GDT_BRANDMIN; i <= GDT_BRANDMAX; i++)
		set_usegd(&gdt0[i], SDP_SHORT, NULL, -1, SDT_MEMRWA,
		    SEL_UPL, SDP_PAGES, SDP_OP32);

	/*
	 * Initialize convenient zero base user descriptors for clearing
	 * lwp private %fs and %gs descriptors in GDT. See setregs() for
	 * an example.
	 */
	set_usegd(&zero_udesc, SDP_LONG, 0, 0, SDT_MEMRWA, SEL_UPL,
	    SDP_BYTES, SDP_OP32);
	set_usegd(&zero_u32desc, SDP_SHORT, 0, -1, SDT_MEMRWA, SEL_UPL,
	    SDP_PAGES, SDP_OP32);
}

#if defined(__xpv)

static user_desc_t *
init_gdt(void)
{
	uint64_t gdtpa;
	ulong_t ma[1];		/* XXPV should be a memory_t */
	ulong_t addr;

#if !defined(__lint)
	/*
	 * Our gdt is never larger than a single page.
	 */
	ASSERT((sizeof (*gdt0) * NGDT) <= PAGESIZE);
#endif
	gdt0 = (user_desc_t *)BOP_ALLOC(bootops, (caddr_t)GDT_VA,
	    PAGESIZE, PAGESIZE);
	bzero(gdt0, PAGESIZE);

	init_gdt_common(gdt0);

	/*
	 * XXX Since we never invoke kmdb until after the kernel takes
	 * over the descriptor tables why not have it use the kernel's
	 * selectors?
	 */
	if (boothowto & RB_DEBUG) {
		set_usegd(&gdt0[GDT_B32DATA], SDP_LONG, NULL, -1, SDT_MEMRWA,
		    SEL_KPL, SDP_PAGES, SDP_OP32);
		set_usegd(&gdt0[GDT_B64CODE], SDP_LONG, NULL, -1, SDT_MEMERA,
		    SEL_KPL, SDP_PAGES, SDP_OP32);
	}

	/*
	 * Clear write permission for page containing the gdt and install it.
	 */
	gdtpa = pfn_to_pa(va_to_pfn(gdt0));
	ma[0] = (ulong_t)(pa_to_ma(gdtpa) >> PAGESHIFT);
	kbm_read_only((uintptr_t)gdt0, gdtpa);
	xen_set_gdt(ma, NGDT);

	/*
	 * Reload the segment registers to use the new GDT.
	 * On 64-bit, fixup KCS_SEL to be in ring 3.
	 * See KCS_SEL in segments.h.
	 */
	load_segment_registers((KCS_SEL | SEL_KPL), KFS_SEL, KGS_SEL, KDS_SEL);

	/*
	 *  setup %gs for kernel
	 */
	xen_set_segment_base(SEGBASE_GS_KERNEL, (ulong_t)&cpus[0]);

	/*
	 * XX64 We should never dereference off "other gsbase" or
	 * "fsbase".  So, we should arrange to point FSBASE and
	 * KGSBASE somewhere truly awful e.g. point it at the last
	 * valid address below the hole so that any attempts to index
	 * off them cause an exception.
	 *
	 * For now, point it at 8G -- at least it should be unmapped
	 * until some 64-bit processes run.
	 */
	addr = 0x200000000ul;
	xen_set_segment_base(SEGBASE_FS, addr);
	xen_set_segment_base(SEGBASE_GS_USER, addr);
	xen_set_segment_base(SEGBASE_GS_USER_SEL, 0);

	return (gdt0);
}

#else	/* __xpv */

static user_desc_t *
init_gdt(void)
{
	desctbr_t	r_bgdt, r_gdt;
	user_desc_t	*bgdt;

#if !defined(__lint)
	/*
	 * Our gdt is never larger than a single page.
	 */
	ASSERT((sizeof (*gdt0) * NGDT) <= PAGESIZE);
#endif
	gdt0 = (user_desc_t *)BOP_ALLOC(bootops, (caddr_t)GDT_VA,
	    PAGESIZE, PAGESIZE);
	bzero(gdt0, PAGESIZE);

	init_gdt_common(gdt0);

	/*
	 * Copy in from boot's gdt to our gdt.
	 * Entry 0 is the null descriptor by definition.
	 */
	rd_gdtr(&r_bgdt);
	bgdt = (user_desc_t *)r_bgdt.dtr_base;
	if (bgdt == NULL)
		panic("null boot gdt");

	gdt0[GDT_B32DATA] = bgdt[GDT_B32DATA];
	gdt0[GDT_B32CODE] = bgdt[GDT_B32CODE];
	gdt0[GDT_B16CODE] = bgdt[GDT_B16CODE];
	gdt0[GDT_B16DATA] = bgdt[GDT_B16DATA];
	gdt0[GDT_B64CODE] = bgdt[GDT_B64CODE];

	/*
	 * Install our new GDT
	 */
	r_gdt.dtr_limit = (sizeof (*gdt0) * NGDT) - 1;
	r_gdt.dtr_base = (uintptr_t)gdt0;
	wr_gdtr(&r_gdt);

	/*
	 * Reload the segment registers to use the new GDT
	 */
	load_segment_registers(KCS_SEL, KFS_SEL, KGS_SEL, KDS_SEL);

	/*
	 *  setup %gs for kernel
	 */
	wrmsr(MSR_AMD_GSBASE, (uint64_t)&cpus[0]);

	/*
	 * XX64 We should never dereference off "other gsbase" or
	 * "fsbase".  So, we should arrange to point FSBASE and
	 * KGSBASE somewhere truly awful e.g. point it at the last
	 * valid address below the hole so that any attempts to index
	 * off them cause an exception.
	 *
	 * For now, point it at 8G -- at least it should be unmapped
	 * until some 64-bit processes run.
	 */
	wrmsr(MSR_AMD_FSBASE, 0x200000000ul);
	wrmsr(MSR_AMD_KGSBASE, 0x200000000ul);
	return (gdt0);
}

#endif	/* __xpv */

/*
 * Build kernel IDT.
 *
 * Note that for amd64 we pretty much require every gate to be an interrupt
 * gate which blocks interrupts atomically on entry; that's because of our
 * dependency on using 'swapgs' every time we come into the kernel to find
 * the cpu structure. If we get interrupted just before doing that, %cs could
 * be in kernel mode (so that the trap prolog doesn't do a swapgs), but
 * %gsbase is really still pointing at something in userland. Bad things will
 * ensue. We also use interrupt gates for i386 as well even though this is not
 * required for some traps.
 *
 * Perhaps they should have invented a trap gate that does an atomic swapgs?
 */
static void
init_idt_common(gate_desc_t *idt)
{
	set_gatesegd(&idt[T_ZERODIV], &div0trap, KCS_SEL, SDT_SYSIGT, TRP_KPL,
	    0);
	set_gatesegd(&idt[T_SGLSTP], &dbgtrap, KCS_SEL, SDT_SYSIGT, TRP_KPL,
	    0);
	set_gatesegd(&idt[T_NMIFLT], &nmiint, KCS_SEL, SDT_SYSIGT, TRP_KPL,
	    0);
	set_gatesegd(&idt[T_BPTFLT], &brktrap, KCS_SEL, SDT_SYSIGT, TRP_UPL,
	    0);
	set_gatesegd(&idt[T_OVFLW], &ovflotrap, KCS_SEL, SDT_SYSIGT, TRP_UPL,
	    0);
	set_gatesegd(&idt[T_BOUNDFLT], &boundstrap, KCS_SEL, SDT_SYSIGT,
	    TRP_KPL, 0);
	set_gatesegd(&idt[T_ILLINST], &invoptrap, KCS_SEL, SDT_SYSIGT, TRP_KPL,
	    0);
	set_gatesegd(&idt[T_NOEXTFLT], &ndptrap,  KCS_SEL, SDT_SYSIGT, TRP_KPL,
	    0);

	/*
	 * double fault handler.
	 *
	 * Note that on the hypervisor a guest does not receive #df faults.
	 * Instead a failsafe event is injected into the guest if its selectors
	 * and/or stack is in a broken state. See xen_failsafe_callback.
	 */
#if !defined(__xpv)
	set_gatesegd(&idt[T_DBLFLT], &syserrtrap, KCS_SEL, SDT_SYSIGT, TRP_KPL,
	    T_DBLFLT);
#endif	/* !__xpv */

	/*
	 * T_EXTOVRFLT coprocessor-segment-overrun not supported.
	 */

	set_gatesegd(&idt[T_TSSFLT], &invtsstrap, KCS_SEL, SDT_SYSIGT, TRP_KPL,
	    0);
	set_gatesegd(&idt[T_SEGFLT], &segnptrap, KCS_SEL, SDT_SYSIGT, TRP_KPL,
	    0);
	set_gatesegd(&idt[T_STKFLT], &stktrap, KCS_SEL, SDT_SYSIGT, TRP_KPL, 0);
	set_gatesegd(&idt[T_GPFLT], &gptrap, KCS_SEL, SDT_SYSIGT, TRP_KPL, 0);
	set_gatesegd(&idt[T_PGFLT], &pftrap, KCS_SEL, SDT_SYSIGT, TRP_KPL, 0);
	set_gatesegd(&idt[T_EXTERRFLT], &ndperr, KCS_SEL, SDT_SYSIGT, TRP_KPL,
	    0);
	set_gatesegd(&idt[T_ALIGNMENT], &achktrap, KCS_SEL, SDT_SYSIGT,
	    TRP_KPL, 0);
	set_gatesegd(&idt[T_MCE], &mcetrap, KCS_SEL, SDT_SYSIGT, TRP_KPL, 0);
	set_gatesegd(&idt[T_SIMDFPE], &xmtrap, KCS_SEL, SDT_SYSIGT, TRP_KPL, 0);

	/*
	 * install fast trap handler at 210.
	 */
	set_gatesegd(&idt[T_FASTTRAP], &fasttrap, KCS_SEL, SDT_SYSIGT, TRP_UPL,
	    0);

	/*
	 * System call handler.
	 */
	set_gatesegd(&idt[T_SYSCALLINT], &sys_syscall_int, KCS_SEL, SDT_SYSIGT,
	    TRP_UPL, 0);

	/*
	 * Install the DTrace interrupt handler for the pid provider.
	 */
	set_gatesegd(&idt[T_DTRACE_RET], &dtrace_ret, KCS_SEL,
	    SDT_SYSIGT, TRP_UPL, 0);

	/*
	 * Prepare interposing descriptor for the syscall handler
	 * and cache copy of the default descriptor.
	 */
	brand_tbl[0].ih_inum = T_SYSCALLINT;
	brand_tbl[0].ih_default_desc = idt0[T_SYSCALLINT];

	set_gatesegd(&(brand_tbl[0].ih_interp_desc), &brand_sys_syscall_int,
	    KCS_SEL, SDT_SYSIGT, TRP_UPL, 0);

	brand_tbl[1].ih_inum = 0;
}

#if defined(__xpv)

static void
init_idt(gate_desc_t *idt)
{
	init_idt_common(idt);
}

#else	/* __xpv */

static void
init_idt(gate_desc_t *idt)
{
	char	ivctname[80];
	void	(*ivctptr)(void);
	int	i;

	/*
	 * Initialize entire table with 'reserved' trap and then overwrite
	 * specific entries. T_EXTOVRFLT (9) is unsupported and reserved
	 * since it can only be generated on a 386 processor. 15 is also
	 * unsupported and reserved.
	 */
	for (i = 0; i < NIDT; i++)
		set_gatesegd(&idt[i], &resvtrap, KCS_SEL, SDT_SYSIGT, TRP_KPL,
		    0);

	/*
	 * 20-31 reserved
	 */
	for (i = 20; i < 32; i++)
		set_gatesegd(&idt[i], &invaltrap, KCS_SEL, SDT_SYSIGT, TRP_KPL,
		    0);

	/*
	 * interrupts 32 - 255
	 */
	for (i = 32; i < 256; i++) {
		(void) snprintf(ivctname, sizeof (ivctname), "ivct%d", i);
		ivctptr = (void (*)(void))kobj_getsymvalue(ivctname, 0);
		if (ivctptr == NULL)
			panic("kobj_getsymvalue(%s) failed", ivctname);

		set_gatesegd(&idt[i], ivctptr, KCS_SEL, SDT_SYSIGT, TRP_KPL, 0);
	}

	/*
	 * Now install the common ones. Note that it will overlay some
	 * entries installed above like T_SYSCALLINT, T_FASTTRAP etc.
	 */
	init_idt_common(idt);
}

#endif	/* __xpv */

/*
 * The kernel does not deal with LDTs unless a user explicitly creates
 * one. Under normal circumstances, the LDTR contains 0. Any process attempting
 * to reference the LDT will therefore cause a #gp. System calls made via the
 * obsolete lcall mechanism are emulated by the #gp fault handler.
 */
static void
init_ldt(void)
{
#if defined(__xpv)
	xen_set_ldt(NULL, 0);
#else
	wr_ldtr(0);
#endif
}

#if !defined(__xpv)

static void
init_tss(void)
{
	/*
	 * tss_rsp0 is dynamically filled in by resume() on each context switch.
	 * All exceptions but #DF will run on the thread stack.
	 * Set up the double fault stack here.
	 */
	ktss0->tss_ist1 =
	    (uint64_t)&dblfault_stack0[sizeof (dblfault_stack0)];

	/*
	 * Set I/O bit map offset equal to size of TSS segment limit
	 * for no I/O permission map. This will force all user I/O
	 * instructions to generate #gp fault.
	 */
	ktss0->tss_bitmapbase = sizeof (*ktss0);

	/*
	 * Point %tr to descriptor for ktss0 in gdt.
	 */
	wr_tsr(KTSS_SEL);
}

#endif	/* !__xpv */

#if defined(__xpv)

void
init_desctbls(void)
{
	uint_t vec;
	user_desc_t *gdt;

	/*
	 * Setup and install our GDT.
	 */
	gdt = init_gdt();

	/*
	 * Store static pa of gdt to speed up pa_to_ma() translations
	 * on lwp context switches.
	 */
	ASSERT(IS_P2ALIGNED((uintptr_t)gdt, PAGESIZE));
	CPU->cpu_gdt = gdt;
	CPU->cpu_m.mcpu_gdtpa = pfn_to_pa(va_to_pfn(gdt));

	/*
	 * Setup and install our IDT.
	 */
#if !defined(__lint)
	ASSERT(NIDT * sizeof (*idt0) <= PAGESIZE);
#endif
	idt0 = (gate_desc_t *)BOP_ALLOC(bootops, (caddr_t)IDT_VA,
	    PAGESIZE, PAGESIZE);
	bzero(idt0, PAGESIZE);
	init_idt(idt0);
	for (vec = 0; vec < NIDT; vec++)
		xen_idt_write(&idt0[vec], vec);

	CPU->cpu_idt = idt0;

	/*
	 * set default kernel stack
	 */
	xen_stack_switch(KDS_SEL,
	    (ulong_t)&dblfault_stack0[sizeof (dblfault_stack0)]);

	xen_init_callbacks();

	init_ldt();
}

#else	/* __xpv */

void
init_desctbls(void)
{
	user_desc_t *gdt;
	desctbr_t idtr;

	/*
	 * Allocate IDT and TSS structures on unique pages for better
	 * performance in virtual machines.
	 */
#if !defined(__lint)
	ASSERT(NIDT * sizeof (*idt0) <= PAGESIZE);
#endif
	idt0 = (gate_desc_t *)BOP_ALLOC(bootops, (caddr_t)IDT_VA,
	    PAGESIZE, PAGESIZE);
	bzero(idt0, PAGESIZE);
#if !defined(__lint)
	ASSERT(sizeof (*ktss0) <= PAGESIZE);
#endif
	ktss0 = (struct tss *)BOP_ALLOC(bootops, (caddr_t)KTSS_VA,
	    PAGESIZE, PAGESIZE);
	bzero(ktss0, PAGESIZE);

	/*
	 * Setup and install our GDT.
	 */
	gdt = init_gdt();
	ASSERT(IS_P2ALIGNED((uintptr_t)gdt, PAGESIZE));
	CPU->cpu_gdt = gdt;

	/*
	 * Setup and install our IDT.
	 */
	init_idt(idt0);

	idtr.dtr_base = (uintptr_t)idt0;
	idtr.dtr_limit = (NIDT * sizeof (*idt0)) - 1;
	wr_idtr(&idtr);
	CPU->cpu_idt = idt0;

	init_tss();
	CPU->cpu_tss = ktss0;
	init_ldt();
}

#endif	/* __xpv */

/*
 * In the early kernel, we need to set up a simple GDT to run on.
 *
 * XXPV	Can dboot use this too?  See dboot_gdt.s
 */
void
init_boot_gdt(user_desc_t *bgdt)
{
	set_usegd(&bgdt[GDT_B32DATA], SDP_LONG, NULL, -1, SDT_MEMRWA, SEL_KPL,
	    SDP_PAGES, SDP_OP32);
	set_usegd(&bgdt[GDT_B64CODE], SDP_LONG, NULL, -1, SDT_MEMERA, SEL_KPL,
	    SDP_PAGES, SDP_OP32);
}

/*
 * Enable interpositioning on the system call path by rewriting the
 * sys{call|enter} MSRs and the syscall-related entries in the IDT to use
 * the branded entry points.
 */
void
brand_interpositioning_enable(void)
{
	gate_desc_t	*idt = CPU->cpu_idt;
	int 		i;

	ASSERT(curthread->t_preempt != 0 || getpil() >= DISP_LEVEL);

	for (i = 0; brand_tbl[i].ih_inum; i++) {
		idt[brand_tbl[i].ih_inum] = brand_tbl[i].ih_interp_desc;
#if defined(__xpv)
		xen_idt_write(&idt[brand_tbl[i].ih_inum],
		    brand_tbl[i].ih_inum);
#endif
	}

#if defined(__xpv)

	/*
	 * Currently the hypervisor only supports 64-bit syscalls via
	 * syscall instruction. The 32-bit syscalls are handled by
	 * interrupt gate above.
	 */
	xen_set_callback(brand_sys_syscall, CALLBACKTYPE_syscall,
	    CALLBACKF_mask_events);

#else

	if (is_x86_feature(x86_featureset, X86FSET_ASYSC)) {
		wrmsr(MSR_AMD_LSTAR, (uintptr_t)brand_sys_syscall);
		wrmsr(MSR_AMD_CSTAR, (uintptr_t)brand_sys_syscall32);
	}

#endif

	if (is_x86_feature(x86_featureset, X86FSET_SEP))
		wrmsr(MSR_INTC_SEP_EIP, (uintptr_t)brand_sys_sysenter);
}

/*
 * Disable interpositioning on the system call path by rewriting the
 * sys{call|enter} MSRs and the syscall-related entries in the IDT to use
 * the standard entry points, which bypass the interpositioning hooks.
 */
void
brand_interpositioning_disable(void)
{
	gate_desc_t	*idt = CPU->cpu_idt;
	int i;

	ASSERT(curthread->t_preempt != 0 || getpil() >= DISP_LEVEL);

	for (i = 0; brand_tbl[i].ih_inum; i++) {
		idt[brand_tbl[i].ih_inum] = brand_tbl[i].ih_default_desc;
#if defined(__xpv)
		xen_idt_write(&idt[brand_tbl[i].ih_inum],
		    brand_tbl[i].ih_inum);
#endif
	}

#if defined(__xpv)

	/*
	 * See comment above in brand_interpositioning_enable.
	 */
	xen_set_callback(sys_syscall, CALLBACKTYPE_syscall,
	    CALLBACKF_mask_events);

#else

	if (is_x86_feature(x86_featureset, X86FSET_ASYSC)) {
		wrmsr(MSR_AMD_LSTAR, (uintptr_t)sys_syscall);
		wrmsr(MSR_AMD_CSTAR, (uintptr_t)sys_syscall32);
	}

#endif

	if (is_x86_feature(x86_featureset, X86FSET_SEP))
		wrmsr(MSR_INTC_SEP_EIP, (uintptr_t)sys_sysenter);
}

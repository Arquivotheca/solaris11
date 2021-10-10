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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Management of KMDB's IDT, which is installed upon KMDB activation.
 *
 * Debugger activation has two flavors, which cover the cases where KMDB is
 * loaded at boot, and when it is loaded after boot.  In brief, in both cases,
 * the KDI needs to interpose upon several handlers in the IDT.  When
 * mod-loaded KMDB is deactivated, we undo the IDT interposition, restoring the
 * handlers to what they were before we started.
 *
 * We also take over the entirety of IDT (except the double-fault handler) on
 * the active CPU when we're in kmdb so we can handle things like page faults
 * sensibly.
 *
 * Boot-loaded KMDB
 *
 * When we're first activated, we're running on boot's IDT.  We need to be able
 * to function in this world, so we'll install our handlers into boot's IDT.
 * This is a little complicated: we're using the fake cpu_t set up by
 * boot_kdi_tmpinit(), so we can't access cpu_idt directly.  Instead,
 * kdi_idt_write() notices that cpu_idt is NULL, and works around this problem.
 *
 * Later, when we're about to switch to the kernel's IDT, it'll call us via
 * kdi_idt_sync(), allowing us to add our handlers to the new IDT.  While
 * boot-loaded KMDB can't be unloaded, we still need to save the descriptors we
 * replace so we can pass traps back to the kernel as necessary.
 *
 * The last phase of boot-loaded KMDB activation occurs at non-boot CPU
 * startup.  We will be called on each non-boot CPU, thus allowing us to set up
 * any watchpoints that may have been configured on the boot CPU and interpose
 * on the given CPU's IDT.  We don't save the interposed descriptors in this
 * case -- see kdi_cpu_init() for details.
 *
 * Mod-loaded KMDB
 *
 * This style of activation is much simpler, as the CPUs are already running,
 * and are using their own copy of the kernel's IDT.  We simply interpose upon
 * each CPU's IDT.  We save the handlers we replace, both for deactivation and
 * for passing traps back to the kernel.  Note that for the hypervisors'
 * benefit, we need to xcall to the other CPUs to do this, since we need to
 * actively set the trap entries in its virtual IDT from that vcpu's context
 * rather than just modifying the IDT table from the CPU running kdi_activate().
 */

#include <sys/types.h>
#include <sys/segments.h>
#include <sys/trap.h>
#include <sys/cpuvar.h>
#include <sys/reboot.h>
#include <sys/sunddi.h>
#include <sys/archsystm.h>
#include <sys/kdi_impl.h>
#include <sys/x_call.h>
#include <ia32/sys/psw.h>

#define	KDI_GATE_NVECS	3

#define	KDI_IDT_NOSAVE	0
#define	KDI_IDT_SAVE	1

#define	KDI_IDT_DTYPE_KERNEL	0
#define	KDI_IDT_DTYPE_BOOT	1

kdi_cpusave_t *kdi_cpusave;
int kdi_ncpusave;

static kdi_main_t kdi_kmdb_main;

kdi_drreg_t kdi_drreg;

uint_t		kdi_msr_wrexit_msr;
uint64_t	*kdi_msr_wrexit_valp;

uintptr_t	kdi_kernel_handler;

int		kdi_trap_switch;

#define	KDI_MEMRANGES_MAX	2

kdi_memrange_t	kdi_memranges[KDI_MEMRANGES_MAX];
int		kdi_nmemranges;

typedef void idt_hdlr_f(void);

extern idt_hdlr_f kdi_trap0, kdi_trap1, kdi_int2, kdi_trap3, kdi_trap4;
extern idt_hdlr_f kdi_trap5, kdi_trap6, kdi_trap7, kdi_trap9;
extern idt_hdlr_f kdi_traperr10, kdi_traperr11, kdi_traperr12;
extern idt_hdlr_f kdi_traperr13, kdi_traperr14, kdi_trap16, kdi_trap17;
extern idt_hdlr_f kdi_trap18, kdi_trap19, kdi_trap20, kdi_ivct32;
extern idt_hdlr_f kdi_invaltrap;
extern size_t kdi_ivct_size;
extern char kdi_slave_entry_patch;

typedef struct kdi_gate_spec {
	uint_t kgs_vec;
	uint_t kgs_dpl;
} kdi_gate_spec_t;

/*
 * Beware: kdi_pass_to_kernel() has unpleasant knowledge of this list.
 */
static const kdi_gate_spec_t kdi_gate_specs[KDI_GATE_NVECS] = {
	{ T_SGLSTP, TRP_KPL },
	{ T_BPTFLT, TRP_UPL },
	{ T_DBGENTR, TRP_KPL }
};

static gate_desc_t kdi_kgates[KDI_GATE_NVECS];

gate_desc_t kdi_idt[NIDT];

struct idt_description {
	uint_t id_low;
	uint_t id_high;
	idt_hdlr_f *id_basehdlr;
	size_t *id_incrp;
} idt_description[] = {
	{ T_ZERODIV, 0,		kdi_trap0, NULL },
	{ T_SGLSTP, 0,		kdi_trap1, NULL },
	{ T_NMIFLT, 0,		kdi_int2, NULL },
	{ T_BPTFLT, 0,		kdi_trap3, NULL },
	{ T_OVFLW, 0,		kdi_trap4, NULL },
	{ T_BOUNDFLT, 0,	kdi_trap5, NULL },
	{ T_ILLINST, 0,		kdi_trap6, NULL },
	{ T_NOEXTFLT, 0,	kdi_trap7, NULL },
#if !defined(__xpv)
	{ T_DBLFLT, 0,		syserrtrap, NULL },
#endif
	{ T_EXTOVRFLT, 0,	kdi_trap9, NULL },
	{ T_TSSFLT, 0,		kdi_traperr10, NULL },
	{ T_SEGFLT, 0,		kdi_traperr11, NULL },
	{ T_STKFLT, 0,		kdi_traperr12, NULL },
	{ T_GPFLT, 0,		kdi_traperr13, NULL },
	{ T_PGFLT, 0,		kdi_traperr14, NULL },
	{ 15, 0,		kdi_invaltrap, NULL },
	{ T_EXTERRFLT, 0, 	kdi_trap16, NULL },
	{ T_ALIGNMENT, 0, 	kdi_trap17, NULL },
	{ T_MCE, 0,		kdi_trap18, NULL },
	{ T_SIMDFPE, 0,		kdi_trap19, NULL },
	{ T_DBGENTR, 0,		kdi_trap20, NULL },
	{ 21, 31,		kdi_invaltrap, NULL },
	{ 32, 255,		kdi_ivct32, &kdi_ivct_size },
	{ 0, 0, NULL },
};

void
kdi_idt_init(selector_t sel)
{
	struct idt_description *id;
	int i;

	for (id = idt_description; id->id_basehdlr != NULL; id++) {
		uint_t high = id->id_high != 0 ? id->id_high : id->id_low;
		size_t incr = id->id_incrp != NULL ? *id->id_incrp : 0;

		for (i = id->id_low; i <= high; i++) {
			caddr_t hdlr = (caddr_t)id->id_basehdlr +
			    incr * (i - id->id_low);
			set_gatesegd(&kdi_idt[i], (void (*)())hdlr, sel,
			    SDT_SYSIGT, TRP_KPL, i);
		}
	}
}

/*
 * Patch caller-provided code into the debugger's IDT handlers.  This code is
 * used to save MSRs that must be saved before the first branch.  All handlers
 * are essentially the same, and end with a branch to kdi_cmnint.  To save the
 * MSR, we need to patch in before the branch.  The handlers have the following
 * structure: KDI_MSR_PATCHOFF bytes of code, KDI_MSR_PATCHSZ bytes of
 * patchable space, followed by more code.
 */
void
kdi_idt_patch(caddr_t code, size_t sz)
{
	int i;

	ASSERT(sz <= KDI_MSR_PATCHSZ);

	for (i = 0; i < sizeof (kdi_idt) / sizeof (struct gate_desc); i++) {
		gate_desc_t *gd;
		uchar_t *patch;

		if (i == T_DBLFLT)
			continue;	/* uses kernel's handler */

		gd = &kdi_idt[i];
		patch = (uchar_t *)GATESEG_GETOFFSET(gd) + KDI_MSR_PATCHOFF;

		/*
		 * We can't ASSERT that there's a nop here, because this may be
		 * a debugger restart.  In that case, we're copying the new
		 * patch point over the old one.
		 */
		/* FIXME: dtrace fbt ... */
		bcopy(code, patch, sz);

		/* Fill the rest with nops to be sure */
		while (sz < KDI_MSR_PATCHSZ)
			patch[sz++] = 0x90; /* nop */
	}
}

static void
kdi_idt_gates_install(selector_t sel, int saveold)
{
	gate_desc_t gates[KDI_GATE_NVECS];
	int i;

	bzero(gates, sizeof (*gates));

	for (i = 0; i < KDI_GATE_NVECS; i++) {
		const kdi_gate_spec_t *gs = &kdi_gate_specs[i];
		uintptr_t func = GATESEG_GETOFFSET(&kdi_idt[gs->kgs_vec]);
		set_gatesegd(&gates[i], (void (*)())func, sel, SDT_SYSIGT,
		    gs->kgs_dpl, gs->kgs_vec);
	}

	for (i = 0; i < KDI_GATE_NVECS; i++) {
		uint_t vec = kdi_gate_specs[i].kgs_vec;

		if (saveold)
			kdi_kgates[i] = CPU->cpu_m.mcpu_idt[vec];

		kdi_idt_write(&gates[i], vec);
	}
}

static void
kdi_idt_gates_restore(void)
{
	int i;

	for (i = 0; i < KDI_GATE_NVECS; i++)
		kdi_idt_write(&kdi_kgates[i], kdi_gate_specs[i].kgs_vec);
}

/*
 * Called when we switch to the kernel's IDT.  We need to interpose on the
 * kernel's IDT entries and stop using KMDBCODE_SEL.
 */
void
kdi_idt_sync(void)
{
	kdi_idt_init(KCS_SEL);
	kdi_idt_gates_install(KCS_SEL, KDI_IDT_SAVE);
}

/*
 * On some processors, we'll need to clear a certain MSR before proceeding into
 * the debugger.  Complicating matters, this MSR must be cleared before we take
 * any branches.  We have patch points in every trap handler, which will cover
 * all entry paths for master CPUs.  We also have a patch point in the slave
 * entry code.
 */
static void
kdi_msr_add_clrentry(uint_t msr)
{
	uchar_t code[] = {
		0x51, 0x50, 0x52,		/* pushq %rcx, %rax, %rdx */
		0xb9, 0x00, 0x00, 0x00, 0x00,	/* movl $MSRNUM, %ecx */
		0x31, 0xc0,			/* clr %eax */
		0x31, 0xd2,			/* clr %edx */
		0x0f, 0x30,			/* wrmsr */
		0x5a, 0x58, 0x59		/* popq %rdx, %rax, %rcx */
	};
	uchar_t *patch = &code[4];

	bcopy(&msr, patch, sizeof (uint32_t));

	kdi_idt_patch((caddr_t)code, sizeof (code));

	bcopy(code, &kdi_slave_entry_patch, sizeof (code));
}

static void
kdi_msr_add_wrexit(uint_t msr, uint64_t *valp)
{
	kdi_msr_wrexit_msr = msr;
	kdi_msr_wrexit_valp = valp;
}

void
kdi_set_debug_msrs(kdi_msr_t *msrs)
{
	int nmsrs, i;

	ASSERT(kdi_cpusave[0].krs_msr == NULL);

	/* Look in CPU0's MSRs for any special MSRs. */
	for (nmsrs = 0; msrs[nmsrs].msr_num != 0; nmsrs++) {
		switch (msrs[nmsrs].msr_type) {
		case KDI_MSR_CLEARENTRY:
			kdi_msr_add_clrentry(msrs[nmsrs].msr_num);
			break;

		case KDI_MSR_WRITEDELAY:
			kdi_msr_add_wrexit(msrs[nmsrs].msr_num,
			    msrs[nmsrs].kdi_msr_valp);
			break;
		}
	}

	nmsrs++;

	for (i = 0; i < kdi_ncpusave; i++)
		kdi_cpusave[i].krs_msr = &msrs[nmsrs * i];
}

void
kdi_update_drreg(kdi_drreg_t *drreg)
{
	kdi_drreg = *drreg;
}

void
kdi_memrange_add(caddr_t base, size_t len)
{
	kdi_memrange_t *mr = &kdi_memranges[kdi_nmemranges];

	ASSERT(kdi_nmemranges != KDI_MEMRANGES_MAX);

	mr->mr_base = base;
	mr->mr_lim = base + len - 1;
	kdi_nmemranges++;
}

void
kdi_idt_switch(kdi_cpusave_t *cpusave)
{
	if (cpusave == NULL)
		kdi_idtr_set(kdi_idt, sizeof (kdi_idt) - 1);
	else
		kdi_idtr_set(cpusave->krs_idt, (sizeof (*idt0) * NIDT) - 1);
}

/*
 * Activation for CPUs other than the boot CPU, called from that CPU's
 * mp_startup().  We saved the kernel's descriptors when we initialized the
 * boot CPU, so we don't want to do it again.  Saving the handlers from this
 * CPU's IDT would actually be dangerous with the CPU initialization method in
 * use at the time of this writing.  With that method, the startup code creates
 * the IDTs for slave CPUs by copying the one used by the boot CPU, which has
 * already been interposed upon by KMDB.  Were we to interpose again, we'd
 * replace the kernel's descriptors with our own in the save area.  By not
 * saving, but still overwriting, we'll work in the current world, and in any
 * future world where the IDT is generated from scratch.
 */
void
kdi_cpu_init(void)
{
	kdi_idt_gates_install(KCS_SEL, KDI_IDT_NOSAVE);
	/* Load the debug registers and MSRs */
	kdi_cpu_debug_init(&kdi_cpusave[CPU->cpu_id]);
}

/*
 * Activation for all CPUs for mod-loaded kmdb, i.e. a kmdb that wasn't
 * loaded at boot.
 */
static int
kdi_cpu_activate(void)
{
	kdi_idt_gates_install(KCS_SEL, KDI_IDT_SAVE);
	return (0);
}

void
kdi_activate(kdi_main_t main, kdi_cpusave_t *cpusave, uint_t ncpusave)
{
	int i;
	cpuset_t cpuset;

	CPUSET_ALL(cpuset);

	kdi_cpusave = cpusave;
	kdi_ncpusave = ncpusave;

	kdi_kmdb_main = main;

	for (i = 0; i < kdi_ncpusave; i++) {
		kdi_cpusave[i].krs_cpu_id = i;

		kdi_cpusave[i].krs_curcrumb =
		    &kdi_cpusave[i].krs_crumbs[KDI_NCRUMBS - 1];
		kdi_cpusave[i].krs_curcrumbidx = KDI_NCRUMBS - 1;
	}

	if (boothowto & RB_KMDB)
		kdi_idt_init(KMDBCODE_SEL);
	else
		kdi_idt_init(KCS_SEL);

	/* The initial selector set.  Updated by the debugger-entry code */
	kdi_memranges[0].mr_base = kdi_segdebugbase;
	kdi_memranges[0].mr_lim = kdi_segdebugbase + kdi_segdebugsize - 1;
	kdi_nmemranges = 1;

	kdi_drreg.dr_ctl = KDIREG_DRCTL_RESERVED;
	kdi_drreg.dr_stat = KDIREG_DRSTAT_RESERVED;

	kdi_msr_wrexit_msr = 0;
	kdi_msr_wrexit_valp = NULL;

	if (boothowto & RB_KMDB) {
		kdi_idt_gates_install(KMDBCODE_SEL, KDI_IDT_NOSAVE);
	} else {
		xc_call(0, 0, 0, CPUSET2BV(cpuset),
		    (xc_func_t)kdi_cpu_activate);
	}
}

static int
kdi_cpu_deactivate(void)
{
	kdi_idt_gates_restore();
	return (0);
}

void
kdi_deactivate(void)
{
	cpuset_t cpuset;
	CPUSET_ALL(cpuset);

	xc_call(0, 0, 0, CPUSET2BV(cpuset), (xc_func_t)kdi_cpu_deactivate);
	kdi_nmemranges = 0;
}

/*
 * We receive all breakpoints and single step traps.  Some of them,
 * including those from userland and those induced by DTrace providers,
 * are intended for the kernel, and must be processed there.  We adopt
 * this ours-until-proven-otherwise position due to the painful
 * consequences of sending the kernel an unexpected breakpoint or
 * single step.  Unless someone can prove to us that the kernel is
 * prepared to handle the trap, we'll assume there's a problem and will
 * give the user a chance to debug it.
 */
int
kdi_trap_pass(kdi_cpusave_t *cpusave)
{
	greg_t tt = cpusave->krs_gregs[KDIREG_TRAPNO];
	greg_t pc = cpusave->krs_gregs[KDIREG_PC];
	greg_t cs = cpusave->krs_gregs[KDIREG_CS];

	if (USERMODE(cs))
		return (1);

	if (tt != T_BPTFLT && tt != T_SGLSTP)
		return (0);

	if (tt == T_BPTFLT && kdi_dtrace_get_state() ==
	    KDI_DTSTATE_DTRACE_ACTIVE)
		return (1);

	/*
	 * See the comments in the kernel's T_SGLSTP handler for why we need to
	 * do this.
	 */
	if (tt == T_SGLSTP &&
	    (pc == (greg_t)sys_sysenter || pc == (greg_t)brand_sys_sysenter))
		return (1);

	return (0);
}

/*
 * State has been saved, and all CPUs are on the CPU-specific stacks.  All
 * CPUs enter here, and head off into the debugger proper.
 */
void
kdi_debugger_entry(kdi_cpusave_t *cpusave)
{
	extern int force_screen_output;

	force_screen_output = 1;
	/*
	 * BPTFLT gives us control with %eip set to the instruction *after*
	 * the int 3.  Back it off, so we're looking at the instruction that
	 * triggered the fault.
	 */
	if (cpusave->krs_gregs[KDIREG_TRAPNO] == T_BPTFLT)
		cpusave->krs_gregs[KDIREG_PC]--;

	kdi_kmdb_main(cpusave);
	force_screen_output = 0;
}

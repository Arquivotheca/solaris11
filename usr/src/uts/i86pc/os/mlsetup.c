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
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/disp.h>
#include <sys/promif.h>
#include <sys/clock.h>
#include <sys/cpuvar.h>
#include <sys/stack.h>
#include <vm/as.h>
#include <vm/hat.h>
#include <sys/reboot.h>
#include <sys/avintr.h>
#include <sys/vtrace.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/cpupart.h>
#include <sys/pset.h>
#include <sys/copyops.h>
#include <sys/pg.h>
#include <sys/disp.h>
#include <sys/debug.h>
#include <sys/sunddi.h>
#include <sys/x86_archext.h>
#include <sys/privregs.h>
#include <sys/machsystm.h>
#include <sys/ontrap.h>
#include <sys/bootconf.h>
#include <sys/bootinfo.h>
#include <sys/boot_console.h>
#include <sys/kdi_machimpl.h>
#include <sys/archsystm.h>
#include <sys/promif.h>
#include <sys/pci_cfgspace.h>
#ifdef __xpv
#include <sys/hypervisor.h>
#else
#include <sys/xpv_support.h>
#endif

/*
 * some globals for patching the result of cpuid
 * to solve problems w/ creative cpu vendors
 */

extern uint32_t cpuid_feature_ecx_include;
extern uint32_t cpuid_feature_ecx_exclude;
extern uint32_t cpuid_feature_edx_include;
extern uint32_t cpuid_feature_edx_exclude;

/*
 * Used to detect if the boot console needs us to do some work on its behalf.
 */
extern int	restore_vga_text;
extern int	restore_vesa_mode;

/*
 * Dummy spl priority masks
 */
static unsigned char dummy_cpu_pri[MAXIPL + 1] = {
	0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
	0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf
};

/*
 * Set VGA console mode.
 */
static void
set_console_mode(uint8_t val)
{
#if !defined(__xpv)
	if (_sys_fw_type != FWT_BIOS)
		return;
#endif
	struct bop_regs rp = {0};

	rp.eax.byte.ah = 0x0;
	rp.eax.byte.al = val;
	rp.ebx.word.bx = 0x0;

	BOP_DOINT(bootops, 0x10, &rp);
}

/*
 * Set the specific VESA mode to 'mode' or'ed with VBE_KEEP_VIDEO_MEMORY, so
 * that we don't lose an image left on the screen during fastreboot.
 * Uses the VESA BIOS call 0x4f02 - Set VBE Mode.
 */
#define	VBE_KEEP_VIDEO_MEMORY	(0x8000)
#define	VBE_LINEAR_FB_INIT	(0x4000)
static int
set_vesa_mode(uint16_t mode)
{
	struct bop_regs	rp = {0};
	uint16_t	m = 0;

#if !defined(__xpv)
	/*
	 * This is just a last-ditch effort to prevent INT calls on
	 * a non-BIOS system.  This function should never be actually called
	 * on such systems, so an ASSERT is here to catch that on DEBUG kernels.
	 */
	ASSERT(_sys_fw_type == FWT_BIOS);
	if (_sys_fw_type != FWT_BIOS)
		return (-1);
#endif
	/* Clear flag bits 9 to 15 (keep only the mode information). */
	m = mode & 0x01ff;
	/* Use a linear frame buffer. */
	m |= VBE_LINEAR_FB_INIT;
	/* Do not clear display memory. */
	m |= VBE_KEEP_VIDEO_MEMORY;
	/* Prepare BIOS registers. */
	rp.eax.word.ax = 0x4f02;
	rp.ebx.word.bx = m;
	/* int 0x10 */
	BOP_DOINT(bootops, 0x10, &rp);
	/* Check return value. */
	if (rp.eax.byte.al != 0x4f && rp.eax.byte.ah != 00)
		return (-1);

	/* Success. */
	return (0);
}

/*
 * If the boot console asked us, either reset the saved VESA mode or
 * the VGA TEXT mode. If setting the VESA mode fails, fallback to VGA TEXT mode.
 */
static void
fix_console_mode()
{
	extern int	console;
	extern uint16_t	saved_vbe_mode;

	if (restore_vesa_mode) {
		if (set_vesa_mode(saved_vbe_mode) == 0) {
			return;
		} else {
			/* Fallback to VGA TEXT mode. */
			console = CONS_SCREEN_VGATEXT;
			/*
			 * Debug: track that we failed attempting to use a
			 * VESA BIOS routine.
			 */
			restore_vga_text = 2;
		}
	}

	/*
	 * Restore VGA TEXT mode. The contents on the screen are not preserved.
	 * We could actually preserve them or'ing the mode to 0x80, but what
	 * we really care about here is a -usable- console.
	 * Everything that the boot process left on the screen so far is erased:
	 * we are before kmdb invocation so this is only the case if we booted
	 * with dboot/early stage debugging turned on. (ex. prom_debug=1).
	 *
	 * XXX - Note that there is nothing to call on UEFI systems, so this
	 * ends up as a NOP, leaving the user blind.  The only thing we can do
	 * is to manually manipulate the VGA registers to get back to text mode,
	 * but that is not an easy thing to do, due to VGA spec ambiguities.
	 *
	 */
	if (restore_vga_text)
		set_console_mode(0x3);
}

/*
 * Setup routine called right before main(). Interposing this function
 * before main() allows us to call it in a machine-independent fashion.
 */
void
mlsetup(struct regs *rp)
{
	u_longlong_t prop_value;
	extern struct classfuncs sys_classfuncs;
	extern disp_t cpu0_disp;
	extern char t0stack[];
	extern void boot_fb_clear_screen();
	extern uint64_t plat_dr_options;

	ASSERT_STACK_ALIGNED();

	/*
	 * initialize cpu_self
	 */
	cpu[0]->cpu_self = cpu[0];

#if defined(__xpv)
	/*
	 * Point at the hypervisor's virtual cpu structure
	 */
	cpu[0]->cpu_m.mcpu_vcpu_info = &HYPERVISOR_shared_info->vcpu_info[0];
#endif

	/*
	 * Set up dummy cpu_pri_data values till psm spl code is
	 * installed.  This allows splx() to work on amd64.
	 */

	cpu[0]->cpu_pri_data = dummy_cpu_pri;

	/*
	 * check if we've got special bits to clear or set
	 * when checking cpu features
	 */

	if (bootprop_getval("cpuid_feature_ecx_include", &prop_value) != 0)
		cpuid_feature_ecx_include = 0;
	else
		cpuid_feature_ecx_include = (uint32_t)prop_value;

	if (bootprop_getval("cpuid_feature_ecx_exclude", &prop_value) != 0)
		cpuid_feature_ecx_exclude = 0;
	else
		cpuid_feature_ecx_exclude = (uint32_t)prop_value;

	if (bootprop_getval("cpuid_feature_edx_include", &prop_value) != 0)
		cpuid_feature_edx_include = 0;
	else
		cpuid_feature_edx_include = (uint32_t)prop_value;

	if (bootprop_getval("cpuid_feature_edx_exclude", &prop_value) != 0)
		cpuid_feature_edx_exclude = 0;
	else
		cpuid_feature_edx_exclude = (uint32_t)prop_value;

	/*
	 * Initialize idt0, gdt0, ldt0_default, ktss0 and dftss.
	 */
	init_desctbls();

	/*
	 * lgrp_init() and possibly cpuid_pass1() need PCI config
	 * space access
	 */
#if !defined(__xpv)
	pci_cfgspace_init();
#endif

	/*
	 * The first lightweight pass (pass0) through the cpuid data
	 * was done in locore before mlsetup was called.  Do the next
	 * pass in C code.
	 *
	 * The x86_featureset is initialized here based on the capabilities
	 * of the boot CPU.  Note that if we choose to support CPUs that have
	 * different feature sets (at which point we would almost certainly
	 * want to set the feature bits to correspond to the feature
	 * minimum) this value may be altered.
	 */
	cpuid_pass1(cpu[0], x86_featureset);

#if !defined(__xpv)

	if (get_hwenv() == HW_XEN_HVM)
		xen_hvm_init();

	/*
	 * Patch the tsc_read routine with appropriate set of instructions,
	 * depending on the processor family and architecure, to read the
	 * time-stamp counter while ensuring no out-of-order execution.
	 * Patch it while the kernel text is still writable.
	 *
	 * Note: tsc_read is not patched for intel processors whose family
	 * is >6 and for amd whose family >f (in case they don't support rdtscp
	 * instruction, unlikely). By default tsc_read will use cpuid for
	 * serialization in such cases. The following code needs to be
	 * revisited if intel processors of family >= f retains the
	 * instruction serialization nature of mfence instruction.
	 * Note: tsc_read is not patched for x86 processors which do
	 * not support "mfence". By default tsc_read will use cpuid for
	 * serialization in such cases.
	 *
	 * The Xen hypervisor does not correctly report whether rdtscp is
	 * supported or not, so we must assume that it is not.
	 */
	if (get_hwenv() != HW_XEN_HVM &&
	    is_x86_feature(x86_featureset, X86FSET_TSCP))
		patch_tsc_read(X86_HAVE_TSCP);
	else if (cpuid_getvendor(CPU) == X86_VENDOR_AMD &&
	    cpuid_getfamily(CPU) <= 0xf &&
	    is_x86_feature(x86_featureset, X86FSET_SSE2))
		patch_tsc_read(X86_TSC_MFENCE);
	else if (cpuid_getvendor(CPU) == X86_VENDOR_Intel &&
	    cpuid_getfamily(CPU) <= 6 &&
	    is_x86_feature(x86_featureset, X86FSET_SSE2))
		patch_tsc_read(X86_TSC_LFENCE);

#endif	/* !__xpv */

#if !defined(__xpv)
	patch_memops(cpuid_getvendor(CPU));
#endif	/* !__xpv */

#if !defined(__xpv)
	/* XXPV	what, if anything, should be dorked with here under xen? */

	/*
	 * While we're thinking about the TSC, let's set up %cr4 so that
	 * userland can issue rdtsc, and initialize the TSC_AUX value
	 * (the cpuid) for the rdtscp instruction on appropriately
	 * capable hardware.
	 */
	if (is_x86_feature(x86_featureset, X86FSET_TSC))
		setcr4(getcr4() & ~CR4_TSD);

	if (is_x86_feature(x86_featureset, X86FSET_TSCP))
		(void) wrmsr(MSR_AMD_TSCAUX, 0);

	if (is_x86_feature(x86_featureset, X86FSET_DE))
		setcr4(getcr4() | CR4_DE);
#endif /* __xpv */

	/*
	 * initialize t0
	 */
	t0.t_stk = (caddr_t)rp - MINFRAME;
	t0.t_stkbase = t0stack;
	t0.t_pri = maxclsyspri - 3;
	t0.t_schedflag = TS_LOAD | TS_DONT_SWAP;
	t0.t_procp = &p0;
	t0.t_plockp = &p0lock.pl_lock;
	t0.t_lwp = &lwp0;
	t0.t_forw = &t0;
	t0.t_back = &t0;
	t0.t_next = &t0;
	t0.t_prev = &t0;
	t0.t_cpu = cpu[0];
	t0.t_disp_queue = &cpu0_disp;
	t0.t_bind_cpu = PBIND_NONE;
	t0.t_bind_pset = PS_NONE;
	t0.t_bindflag = (uchar_t)default_binding_mode;
	t0.t_cpupart = &cp_default;
	t0.t_clfuncs = &sys_classfuncs.thread;
	t0.t_copyops = NULL;

	/*
	 * The startup thread (and future page swapper) isn't transient.
	 */
	t0.t_wkld_flags |= T_WKLD_PERMANENT;
	t0.t_transience = 0;

	THREAD_ONPROC(&t0, CPU);

	lwp0.lwp_thread = &t0;
	lwp0.lwp_regs = (void *)rp;
	lwp0.lwp_procp = &p0;
	t0.t_tid = p0.p_lwpcnt = p0.p_lwprcnt = p0.p_lwpid = 1;

	p0.p_exec = NULL;
	p0.p_stat = SRUN;
	p0.p_flag = SSYS;
	p0.p_tlist = &t0;
	p0.p_stksize = 2*PAGESIZE;
	p0.p_stkpageszc = 0;
	p0.p_as = &kas;
	p0.p_lockp = &p0lock;
	p0.p_brkpageszc = 0;
	p0.p_t1_lgrpid = LGRP_NONE;
	p0.p_tr_lgrpid = LGRP_NONE;
	sigorset(&p0.p_ignore, &ignoredefault);

	CPU->cpu_thread = &t0;
	bzero(&cpu0_disp, sizeof (disp_t));
	CPU->cpu_disp = &cpu0_disp;
	CPU->cpu_disp->disp_cpu = CPU;
	CPU->cpu_dispthread = &t0;
	CPU->cpu_idle_thread = &t0;
	CPU->cpu_flags = CPU_READY | CPU_RUNNING | CPU_EXISTS | CPU_ENABLE;
	CPU->cpu_dispatch_pri = t0.t_pri;
	CPU->cpu_disp_flags |= CPU_DISP_REGULAR;
	CPU->cpu_initial_util = 1;

	CPU->cpu_id = 0;

	CPU->cpu_pri = 12;		/* initial PIL for the boot CPU */

	/*
	 * The kernel doesn't use LDTs unless a process explicitly requests one.
	 */
	p0.p_ldt_desc = null_sdesc;

	/*
	 * Initialize thread/cpu microstate accounting
	 */
	init_mstate(&t0, LMS_SYSTEM);
	init_cpu_mstate(CPU, CMS_SYSTEM);

	/*
	 * Initialize lists of available and active CPUs.
	 */
	cpu_list_init(CPU);

	pg_cpu_bootstrap(CPU);

	/*
	 * Now that we have taken over the GDT, IDT and have initialized
	 * active CPU list it's time to inform kmdb if present.
	 */
	if (boothowto & RB_DEBUG)
		kdi_idt_sync();

	/*
	 * If we need to restore the console to a sane state, do it now
	 * before kmdb is invoked.
	 */
	if (restore_vga_text || restore_vesa_mode)
		fix_console_mode();

	/*
	 * Clearly, console=graphics and -kd are not meant to go together.
	 * This is safe here because 'text and 'graphics' in VESA mode
	 * perform the same video card initialization.
	 */
	if (boothowto & RB_DEBUGENTER && console == CONS_SCREEN_GRAPHICS) {
		console = CONS_SCREEN_FB;
		boot_fb_clear_screen();
	}

	/*
	 * If requested (boot -d) drop into kmdb.
	 *
	 * This must be done after cpu_list_init() on the 64-bit kernel
	 * since taking a trap requires that we re-compute gsbase based
	 * on the cpu list.
	 */
	if (boothowto & RB_DEBUGENTER)
		kmdb_enter();

	cpu_vm_data_init(CPU);

	rp->r_fp = 0;	/* terminate kernel stack traces! */

	prom_init("kernel", (void *)NULL);

	/* User-set option overrides firmware value. */
	if (bootprop_getval(PLAT_DR_OPTIONS_NAME, &prop_value) == 0) {
		plat_dr_options = (uint64_t)prop_value;
	}
#if defined(__xpv)
	/* No support of DR operations on xpv */
	plat_dr_options = 0;
#else	/* __xpv */
	/* Flag PLAT_DR_FEATURE_ENABLED should only be set by DR driver. */
	plat_dr_options &= ~PLAT_DR_FEATURE_ENABLED;
#endif	/* __xpv */

	/*
	 * Get value of "plat_dr_physmax" boot option.
	 * It overrides values calculated from MSCT or SRAT table.
	 */
	if (bootprop_getval(PLAT_DR_PHYSMAX_NAME, &prop_value) == 0) {
		plat_dr_physmax = ((uint64_t)prop_value) >> PAGESHIFT;
	}

	/* Get value of boot_ncpus. */
	if (bootprop_getval(BOOT_NCPUS_NAME, &prop_value) != 0) {
		boot_ncpus = NCPU;
	} else {
		boot_ncpus = (int)prop_value;
		if (boot_ncpus <= 0 || boot_ncpus > NCPU)
			boot_ncpus = NCPU;
	}

	/*
	 * Set max_ncpus and boot_max_ncpus to boot_ncpus if platform doesn't
	 * support CPU DR operations.
	 */
	if (plat_dr_support_cpu() == 0) {
		max_ncpus = boot_max_ncpus = boot_ncpus;
	} else {
		if (bootprop_getval(PLAT_MAX_NCPUS_NAME, &prop_value) != 0) {
			max_ncpus = NCPU;
		} else {
			max_ncpus = (int)prop_value;
			if (max_ncpus <= 0 || max_ncpus > NCPU) {
				max_ncpus = NCPU;
			}
			if (boot_ncpus > max_ncpus) {
				boot_ncpus = max_ncpus;
			}
		}

		if (bootprop_getval(BOOT_MAX_NCPUS_NAME, &prop_value) != 0) {
			boot_max_ncpus = boot_ncpus;
		} else {
			boot_max_ncpus = (int)prop_value;
			if (boot_max_ncpus <= 0 || boot_max_ncpus > NCPU) {
				boot_max_ncpus = boot_ncpus;
			} else if (boot_max_ncpus > max_ncpus) {
				boot_max_ncpus = max_ncpus;
			}
		}
	}

	/*
	 * Initialize the lgrp framework
	 */
	lgrp_init(LGRP_INIT_STAGE1);

	if (boothowto & RB_HALT) {
		prom_printf("unix: kernel halted by -h flag\n");
		prom_enter_mon();
	}

	ASSERT_STACK_ALIGNED();

#if !defined(__xpv)
	/*
	 * Fill out cpu_ucode_info.  Update microcode if necessary.
	 */
	ucode_check(CPU);
#endif

	if (workaround_errata(CPU) != 0)
		panic("critical workaround(s) missing for boot cpu");
}


void
mach_modpath(char *path, const char *filename)
{
	/*
	 * Construct the directory path from the filename.
	 */

	int len;
	char *p;
	const char isastr[] = "/amd64";
	size_t isalen = strlen(isastr);

	if ((p = strrchr(filename, '/')) == NULL)
		return;

	while (p > filename && *(p - 1) == '/')
		p--;	/* remove trailing '/' characters */
	if (p == filename)
		p++;	/* so "/" -is- the modpath in this case */

	/*
	 * Remove optional isa-dependent directory name - the module
	 * subsystem will put this back again (!)
	 */
	len = p - filename;
	if (len > isalen &&
	    strncmp(&filename[len - isalen], isastr, isalen) == 0)
		p -= isalen;

	/*
	 * "/platform/mumblefrotz" + " " + MOD_DEFPATH
	 */
	len += (p - filename) + 1 + strlen(MOD_DEFPATH) + 1;
	(void) strncpy(path, filename, p - filename);
}

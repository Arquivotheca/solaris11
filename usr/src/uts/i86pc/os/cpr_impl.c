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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * x64 platform specific implementation code
 */

#define	SUNDDI_IMPL

#include <sys/types.h>
#include <sys/promif.h>
#include <sys/prom_isa.h>
#include <sys/prom_plat.h>
#include <sys/cpuvar.h>
#include <sys/pte.h>
#include <vm/hat.h>
#include <vm/page.h>
#include <vm/as.h>
#include <sys/cpr.h>
#include <sys/cpr_priv.h>
#include <sys/kmem.h>
#include <sys/clock.h>
#include <sys/kmem.h>
#include <sys/panic.h>
#include <vm/seg_kmem.h>
#include <sys/cpu_module.h>
#include <sys/callb.h>
#include <sys/machsystm.h>
#include <sys/vmsystm.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/stack.h>
#include <sys/fs/ufs_fs.h>
#include <sys/memlist.h>
#include <sys/bootconf.h>
#include <sys/thread.h>
#include <sys/x_call.h>
#include <sys/smp_impldefs.h>
#include <vm/vm_dep.h>
#include <sys/psm.h>
#include <sys/epm.h>
#include <sys/cpr_wakecode.h>
#include <sys/x86_archext.h>
#include <sys/reboot.h>
#include <sys/acpi/acpi.h>
#include <sys/acpica.h>
#include <sys/fp.h>

#define	AFMT	"%lx"

extern int	flushes_require_xcalls;
extern cpuset_t	cpu_ready_set;

extern void	*wc_long_mode_64(void);
extern int	tsc_gethrtime_enable;
extern void	i_cpr_start_cpu(void);
extern void	cpr_statef_close(void);


ushort_t	cpr_mach_type = CPR_MACHTYPE_X86;
void		(*cpr_start_cpu_func)(void) = i_cpr_start_cpu;

static wc_cpu_t	*wc_other_cpus = NULL;
static cpuset_t procset;
static  csu_md_t m_info;

static void
init_real_mode_platter(int cpun, uint32_t offset, uint_t cr4, wc_desctbr_t gdt);

static int i_cpr_platform_alloc(psm_state_request_t *req);
static void i_cpr_platform_free(psm_state_request_t *req);
static int i_cpr_save_apic(psm_state_request_t *req);
static int i_cpr_restore_apic(psm_state_request_t *req);
static int wait_for_set(cpuset_t *set, int who);
extern int cpr_nbitmaps;

static	void i_cpr_save_stack(kthread_t *t, wc_cpu_t *wc_cpu);
void i_cpr_restore_stack(kthread_t *t, greg_t *save_stack);

#ifdef STACK_GROWTH_DOWN
#define	CPR_GET_STACK_START(t) ((t)->t_stkbase)
#define	CPR_GET_STACK_END(t) ((t)->t_stk)
#else
#define	CPR_GET_STACK_START(t) ((t)->t_stk)
#define	CPR_GET_STACK_END(t) ((t)->t_stkbase)
#endif	/* STACK_GROWTH_DOWN */

void
i_cpr_machdep_setup(void)
{
	/* Nothing for x86 */
}


/*
 * Stop all interrupt activities in the system
 */
void
i_cpr_stop_intr(void)
{
	(void) spl7();
}

/*
 * Set machine up to take interrupts
 */
void
i_cpr_enable_intr(void)
{
	(void) spl0();
}

/*
 * Save miscellaneous information which needs to be written to the
 * state file.
 * On Sparc, this function just verifies that the resume code fits
 * into the available page, and panic if not.  I would expect that
 * there is a better way to handle that, but this function should
 * be just for saving machine-specifc info in the statefile or
 * default file.
 */
void
i_cpr_save_machdep_info(void)
{
	/* Nothing for x86 yet. */
}


processorid_t
i_cpr_bootcpuid(void)
{
	return (0);
}

/*
 * cpu0 should contain bootcpu info
 */
cpu_t *
i_cpr_bootcpu(void)
{
	ASSERT(MUTEX_HELD(&cpu_lock));

	return (cpu_get(i_cpr_bootcpuid()));
}

/*
 *	Save context for the specified CPU
 */
void *
i_cpr_save_context(void *arg)
{
	long	index = (long)arg;
	psm_state_request_t *papic_state;
	int resuming;
	int	ret;
	wc_cpu_t	*wc_cpu = wc_other_cpus + index;

	PMD(PMD_SX, ("i_cpr_save_context() index = %ld\n", index))

	ASSERT(index < NCPU);

	papic_state = &(wc_cpu)->wc_apic_state;

	ret = i_cpr_platform_alloc(papic_state);
	ASSERT(ret == 0);

	ret = i_cpr_save_apic(papic_state);
	ASSERT(ret == 0);

	i_cpr_save_stack(curthread, wc_cpu);

	/*
	 * wc_save_context returns twice, once when susending and
	 * once when resuming,  wc_save_context() returns 0 when
	 * suspending and non-zero upon resume
	 */
	resuming = (wc_save_context(wc_cpu) == 0);

	/*
	 * do NOT call any functions after this point, because doing so
	 * will modify the stack that we are running on
	 */

	if (resuming) {

		ret = i_cpr_restore_apic(papic_state);
		ASSERT(ret == 0);

		i_cpr_platform_free(papic_state);

		/*
		 * Enable interrupts on this cpu.
		 * Do not bind interrupts to this CPU's local APIC until
		 * the CPU is ready to receive interrupts.
		 */
		ASSERT(CPU->cpu_id != i_cpr_bootcpuid());
		mutex_enter(&cpu_lock);
		cpu_enable_intr(CPU);
		mutex_exit(&cpu_lock);

		/*
		 * Setting the bit in cpu_ready_set must be the last operation
		 * in processor initialization; the boot CPU will continue to
		 * boot once it sees this bit set for all active CPUs.
		 */
		CPUSET_ATOMIC_ADD(cpu_ready_set, CPU->cpu_id);

		PMD(PMD_SX,
		    ("i_cpr_save_context() resuming cpu %d in cpu_ready_set\n",
		    CPU->cpu_id))
	} else {
		/*
		 * Disable interrupts on this CPU so that PSM knows not to bind
		 * interrupts here on resume until the CPU has executed
		 * cpu_enable_intr() (above) in the resume path.
		 * We explicitly do not grab cpu_lock here because at this point
		 * in the suspend process, the boot cpu owns cpu_lock and all
		 * other cpus are also executing in the pause thread (only
		 * modifying their respective CPU structure).
		 */
		(void) cpu_disable_intr(CPU);
	}

	PMD(PMD_SX, ("i_cpr_save_context: wc_save_context returns %d\n",
	    resuming))

	return (NULL);
}

static ushort_t *warm_reset_vector = NULL;

static ushort_t *
map_warm_reset_vector()
{
	/*LINTED*/
	if (!(warm_reset_vector = (ushort_t *)psm_map_phys(WARM_RESET_VECTOR,
	    sizeof (ushort_t *), PROT_READ|PROT_WRITE)))
		return (NULL);

	/*
	 * setup secondary cpu bios boot up vector
	 */
	*warm_reset_vector = (ushort_t)((caddr_t)
	    /*LINTED*/
	    ((struct rm_platter *)rm_platter_va)->rm_code - rm_platter_va
	    + ((ulong_t)rm_platter_va & 0xf));
	warm_reset_vector++;
	*warm_reset_vector = (ushort_t)(rm_platter_pa >> 4);

	--warm_reset_vector;
	return (warm_reset_vector);
}

void
i_cpr_pre_resume_cpus()
{
	/*
	 * this is a cut down version of start_other_cpus()
	 * just do the initialization to wake the other cpus
	 */
	unsigned who;
	int boot_cpuid = i_cpr_bootcpuid();
	uint32_t		code_length = 0;
	caddr_t			wakevirt = rm_platter_va;
	/*LINTED*/
	wakecode_t		*wp = (wakecode_t *)wakevirt;
	char *str = "i_cpr_pre_resume_cpus";
	extern int get_tsc_ready();
	int err;

	/*LINTED*/
	rm_platter_t *real_mode_platter = (rm_platter_t *)rm_platter_va;

	/*
	 * If startup wasn't able to find a page under 1M, we cannot
	 * proceed.
	 */
	if (rm_platter_va == 0) {
		cmn_err(CE_WARN, "Cannot suspend the system because no "
		    "memory below 1M could be found for processor startup");
		return;
	}

	/*
	 * Copy the real mode code at "real_mode_start" to the
	 * page at rm_platter_va.
	 */
	warm_reset_vector = map_warm_reset_vector();
	if (warm_reset_vector == NULL) {
		PMD(PMD_SX, ("i_cpr_pre_resume_cpus() returning #2\n"))
		return;
	}

	flushes_require_xcalls = 1;

	/*
	 * We lock our affinity to the master CPU to ensure that all slave CPUs
	 * do their TSC syncs with the same CPU.
	 */

	affinity_set(CPU_CURRENT);

	/*
	 * Mark the boot cpu as being ready and in the procset, since we are
	 * running on that cpu.
	 */
	CPUSET_ONLY(cpu_ready_set, boot_cpuid);
	CPUSET_ONLY(procset, boot_cpuid);

	for (who = 0; who < max_ncpus; who++) {

		wc_cpu_t	*cpup = wc_other_cpus + who;
		wc_desctbr_t	gdt;

		if (who == boot_cpuid)
			continue;

		if (!CPU_IN_SET(mp_cpus, who))
			continue;

		PMD(PMD_SX, ("%s() waking up cpu %d\n", str, who))

		bcopy(cpup, &(wp->wc_cpu), sizeof (wc_cpu_t));

		gdt.base = cpup->wc_gdt_base;
		gdt.limit = cpup->wc_gdt_limit;

		code_length = (uint32_t)wc_long_mode_64 - (uint32_t)wc_rm_start;

		init_real_mode_platter(who, code_length, cpup->wc_cr4, gdt);

		mutex_enter(&cpu_lock);
		err = mach_cpuid_start(who, rm_platter_va);
		mutex_exit(&cpu_lock);
		if (err != 0) {
			cmn_err(CE_WARN, "cpu%d: failed to start during "
			    "suspend/resume error %d", who, err);
			continue;
		}

		PMD(PMD_SX, ("%s() #1 waiting for cpu %d in procset\n",
		    str, who))

		if (!wait_for_set(&procset, who))
			continue;

		PMD(PMD_SX, ("%s() %d cpu started\n", str, who))

		PMD(PMD_SX, ("%s() tsc_ready = %d\n", str, get_tsc_ready()))

		if (tsc_gethrtime_enable) {
			PMD(PMD_SX, ("%s() calling tsc_sync_master\n", str))
			tsc_sync_master(who);
		}

		PMD(PMD_SX, ("%s() waiting for %d in cpu_ready_set\n", str,
		    who))
		/*
		 * Wait for cpu to declare that it is ready, we want the
		 * cpus to start serially instead of in parallel, so that
		 * they do not contend with each other in wc_rm_start()
		 */
		if (!wait_for_set(&cpu_ready_set, who))
			continue;

		/*
		 * do not need to re-initialize dtrace using dtrace_cpu_init
		 * function
		 */
		PMD(PMD_SX, ("%s() cpu %d now ready\n", str, who))
	}

	affinity_clear();

	PMD(PMD_SX, ("%s() all cpus now ready\n", str))

}

static void
unmap_warm_reset_vector(ushort_t *warm_reset_vector)
{
	psm_unmap_phys((caddr_t)warm_reset_vector, sizeof (ushort_t *));
}

/*
 * We need to setup a 1:1 (virtual to physical) mapping for the
 * page containing the wakeup code.
 */
static struct as *save_as;	/* when switching to kas */

static void
unmap_wakeaddr_1to1(uint64_t wakephys)
{
	uintptr_t	wp = (uintptr_t)wakephys;
	hat_setup(save_as->a_hat, 0);	/* switch back from kernel hat */
	hat_unload(kas.a_hat, (caddr_t)wp, PAGESIZE, HAT_UNLOAD);
}

void
i_cpr_post_resume_cpus()
{
	uint64_t	wakephys = rm_platter_pa;

	if (warm_reset_vector != NULL)
		unmap_warm_reset_vector(warm_reset_vector);

	hat_unload(kas.a_hat, (caddr_t)(uintptr_t)rm_platter_pa, MMU_PAGESIZE,
	    HAT_UNLOAD);

	/*
	 * cmi_post_mpstartup() is only required upon boot not upon
	 * resume from RAM
	 */

	PT(PT_UNDO1to1);
	/* Tear down 1:1 mapping for wakeup code */
	unmap_wakeaddr_1to1(wakephys);
}

/* ARGSUSED */
void
i_cpr_handle_xc(int flag)
{
}

/*
 * These functions are for reusable statefile support.  For the
 * time being, we don't support this for x64.
 */
int
i_cpr_reusable_supported(void)
{
	return (0);
}

int
i_cpr_reuseinit(void)
{
	return (EROFS);
}

int
i_cpr_reusefini(void)
{
	return (EROFS);
}

int
i_cpr_check_cprinfo(void)
{
	return (ENOENT);
}

/*
 * Map in the physical address that has the wake code.
 */
static void
map_wakeaddr_1to1(uint64_t wakephys)
{
	uintptr_t	wp = (uintptr_t)wakephys;
	hat_devload(kas.a_hat, (caddr_t)wp, PAGESIZE, btop(wakephys),
	    (PROT_READ|PROT_WRITE|PROT_EXEC|HAT_STORECACHING_OK),
	    HAT_LOAD_NOCONSIST);
	save_as = curthread->t_procp->p_as;
	hat_setup(kas.a_hat, 0);	/* switch to kernel-only hat */
}


void
prt_other_cpus()
{
	int	who;

	if (ncpus == 1) {
		PMD(PMD_SX, ("prt_other_cpus() other cpu table empty for "
		    "uniprocessor machine\n"))
		return;
	}

	for (who = 0; who < max_ncpus; who++) {

		wc_cpu_t	*cpup = wc_other_cpus + who;

		if (!CPU_IN_SET(mp_cpus, who))
			continue;

		PMD(PMD_SX, ("prt_other_cpus() who = %d, gdt=%p:%x, "
		    "idt=%p:%x, ldt=%lx, tr=%lx, kgsbase="
		    AFMT ", sp=%lx\n", who,
		    (void *)cpup->wc_gdt_base, cpup->wc_gdt_limit,
		    (void *)cpup->wc_idt_base, cpup->wc_idt_limit,
		    (long)cpup->wc_ldt, (long)cpup->wc_tr,
		    (long)cpup->wc_kgsbase, (long)cpup->wc_rsp))
	}
}

/*
 * Power down the system by calling proper suspend method.
 * It is the responsibility of this function to save the wake vector.
 */
int
i_cpr_power_down(void)
{
	caddr_t		wakevirt = rm_platter_va;
	uint64_t	wakephys = rm_platter_pa;
	ulong_t		saved_intr;
	uint32_t	code_length = 0;
	wc_desctbr_t	gdt;
	/*LINTED*/
	wakecode_t	*wp = (wakecode_t *)wakevirt;
	/*LINTED*/
	rm_platter_t	*wcpp = (rm_platter_t *)wakevirt;
	wc_cpu_t	*cpup = &(wp->wc_cpu);
	dev_info_t	*ppm;
	int		ret = 0;
	power_req_t	power_req;
	char *str =	"i_cpr_power_down";

	/*LINTED*/
	rm_platter_t *real_mode_platter = (rm_platter_t *)rm_platter_va;

	extern void	kernel_wc_code();
	extern void	tsc_resume(void);

	ASSERT(CPU->cpu_id == 0);

	/*
	 * I would like to remove the PPM code from this, as there
	 * is little value in the "mis"-direction (plus it isn't used on
	 * Sparc). But that was a disaster the first time, so do that
	 * after things are clean.
	 */
	if ((ppm = PPM(ddi_root_node())) == NULL) {
		PMD(PMD_SX, ("%s: root node not claimed\n", str))
		return (ENOTTY);
	}

	PMD(PMD_SX, ("Entering %s()\n", str))

	PT(PT_IC);
	saved_intr = intr_clear();

	PT(PT_1to1);
	/* Setup 1:1 mapping for wakeup code */
	map_wakeaddr_1to1(wakephys);

	PMD(PMD_SX, ("ncpus=%d\n", ncpus))

	PMD(PMD_SX, ("wc_rm_end - wc_rm_start=%lx WC_CODESIZE=%x\n",
	    ((size_t)((uint_t)wc_rm_end - (uint_t)wc_rm_start)), WC_CODESIZE))

	PMD(PMD_SX, ("wakevirt=%p, wakephys=%x\n",
	    (void *)wakevirt, (uint_t)wakephys))

	ASSERT(((size_t)((uint_t)wc_rm_end - (uint_t)wc_rm_start)) <
	    WC_CODESIZE);

	bzero(wakevirt, PAGESIZE);

	/* Copy code to rm_platter */
	bcopy((caddr_t)wc_rm_start, wakevirt,
	    (size_t)((uint_t)wc_rm_end - (uint_t)wc_rm_start));

#ifdef DEBUG
	/* PMD insanity */
	prt_other_cpus();
#endif

	PMD(PMD_SX, ("real_mode_platter->rm_cr4=%lx, getcr4()=%lx\n",
	    (ulong_t)real_mode_platter->rm_cr4, (ulong_t)getcr4()))
	PMD(PMD_SX, ("real_mode_platter->rm_pdbr=%lx, getcr3()=%lx\n",
	    (ulong_t)real_mode_platter->rm_pdbr, getcr3()))

	real_mode_platter->rm_cr4 = getcr4();
	real_mode_platter->rm_pdbr = getcr3();

	rmp_gdt_init(real_mode_platter);

	/*
	 * The identity mapped address identified in rmp_gdt_init()
	 * is incorrect for suspend/resume, so we need to recalculate
	 * it here.
	 */
	real_mode_platter->rm_longmode64_addr = rm_platter_pa +
	    ((uint32_t)wc_long_mode_64 - (uint32_t)wc_rm_start);

	PMD(PMD_SX, ("real_mode_platter->rm_cr4=%lx, getcr4()=%lx\n",
	    (ulong_t)real_mode_platter->rm_cr4, getcr4()))

	PMD(PMD_SX, ("real_mode_platter->rm_pdbr=%lx, getcr3()=%lx\n",
	    (ulong_t)real_mode_platter->rm_pdbr, getcr3()))

	PMD(PMD_SX, ("real_mode_platter->rm_longmode64_addr=%lx\n",
	    (ulong_t)real_mode_platter->rm_longmode64_addr))

	PT(PT_SC);
	/*
	 * We save the context *and* the return pointer for suspend.
	 * For the most part, the resume code takes over from the
	 * wc_save_context() call and returns 0, allowing the resumed
	 * kernel to exectue the branch.  wc_save_context on suspend,
	 * though, will return 1 allowing the continuation of actually
	 * suspending.
	 */
	if (wc_save_context(cpup)) {

		ret = i_cpr_platform_alloc(&(wc_other_cpus->wc_apic_state));
		if (ret != 0)
			return (ret);

		ret = i_cpr_save_apic(&(wc_other_cpus->wc_apic_state));
		PMD(PMD_SX, ("%s: i_cpr_save_apic() returned %d\n", str, ret))
		if (ret != 0)
			return (ret);

		PMD(PMD_SX, ("wakephys=%x, kernel_wc_code=%p\n",
		    (uint_t)wakephys, (void *)&kernel_wc_code))
		PMD(PMD_SX, ("virtaddr=%lx, retaddr=%lx\n",
		    (long)cpup->wc_virtaddr, (long)cpup->wc_retaddr))
		PMD(PMD_SX, ("ebx=%x, edi=%x, esi=%x, ebp=%x, esp=%x\n",
		    cpup->wc_ebx, cpup->wc_edi, cpup->wc_esi, cpup->wc_ebp,
		    cpup->wc_esp))
		PMD(PMD_SX, ("cr0=%lx, cr3=%lx, cr4=%lx\n",
		    (long)cpup->wc_cr0, (long)cpup->wc_cr3,
		    (long)cpup->wc_cr4))
		PMD(PMD_SX, ("cs=%x, ds=%x, es=%x, ss=%x, fs=%lx, gs=%lx, "
		    "flgs=%lx\n", cpup->wc_cs, cpup->wc_ds, cpup->wc_es,
		    cpup->wc_ss, (long)cpup->wc_fs, (long)cpup->wc_gs,
		    (long)cpup->wc_eflags))

		PMD(PMD_SX, ("gdt=%p:%x, idt=%p:%x, ldt=%lx, tr=%lx, "
		    "kgbase=%lx\n", (void *)cpup->wc_gdt_base,
		    cpup->wc_gdt_limit, (void *)cpup->wc_idt_base,
		    cpup->wc_idt_limit, (long)cpup->wc_ldt,
		    (long)cpup->wc_tr, (long)cpup->wc_kgsbase))

		gdt.base = cpup->wc_gdt_base;
		gdt.limit = cpup->wc_gdt_limit;

		code_length = (uint32_t)wc_long_mode_64 -
		    (uint32_t)wc_rm_start;

		init_real_mode_platter(0, code_length, cpup->wc_cr4, gdt);

		PMD(PMD_SX, ("real_mode_platter->rm_cr4=%lx, getcr4()=%lx\n",
		    (ulong_t)wcpp->rm_cr4, getcr4()))

		PMD(PMD_SX, ("real_mode_platter->rm_pdbr=%lx, getcr3()=%lx\n",
		    (ulong_t)wcpp->rm_pdbr, getcr3()))

		PMD(PMD_SX, ("real_mode_platter->rm_longmode64_addr=%lx\n",
		    (ulong_t)wcpp->rm_longmode64_addr))

		PMD(PMD_SX,
		    ("real_mode_platter->rm_temp_gdt[TEMPGDT_KCODE64]=%lx\n",
		    (ulong_t)wcpp->rm_temp_gdt[TEMPGDT_KCODE64]))

		PMD(PMD_SX, ("gdt=%p:%x, idt=%p:%x, ldt=%lx, tr=%lx, "
		    "kgsbase=%lx\n", (void *)wcpp->rm_gdt_base,
		    wcpp->rm_gdt_lim, (void *)wcpp->rm_idt_base,
		    wcpp->rm_idt_lim, (long)cpup->wc_ldt, (long)cpup->wc_tr,
		    (long)cpup->wc_kgsbase))

		/*
		 * We are finally done saving (and displaying) the system
		 * state enough for resume.  If we are expected to save
		 * the system state to NV storage, do so now.
		 * Note that *any* cpr_dump() failure terminates suspend,
		 * as it is not (currently) recoverable in this module.
		 */
		if (pm_cpr_save_state != 0) {
			cpr_set_substate(C_ST_DUMP);
			ret = cpr_dump(C_VP);
			/* Close the statefile, as we are done with it */
			cpr_statef_close();
			/*
			 * if an error occured or reusable mode,
			 * free/reset locally allocated items, and
			 * return to the caller (cpr_main()), as it
			 * knows what to do next.
			 */
			if (ret != 0 || cpr_reusable_mode) {
				unmap_wakeaddr_1to1(wakephys);
				intr_restore(saved_intr);
				return (ret);
			}
		}

		power_req.request_type = PMR_PPM_ENTER_SX;
		power_req.req.ppm_power_enter_sx_req.sx_state = pm_cpr_state;
		power_req.req.ppm_power_enter_sx_req.test_point =
		    cpr_test_point;
		power_req.req.ppm_power_enter_sx_req.wakephys = wakephys;

		PMD(PMD_SX, ("%s: pm_ctlops PMR_PPM_ENTER_SX\n", str))
		PT(PT_PPMCTLOP);
		(void) pm_ctlops(ppm, ddi_root_node(), DDI_CTLOPS_POWER,
		    &power_req, &ret);
		PMD(PMD_SX, ("%s: returns %d\n", str, ret))

		/*
		 * If it works, we get control back to the else branch below
		 * If we get control back here, it didn't work.
		 */

		unmap_wakeaddr_1to1(wakephys);
		intr_restore(saved_intr);

		return (ret);
	} else {
		cpr_suspend_succeeded = 1;

		/*
		 * Sync the tsc so the rest of resume has the correct time.
		 */
		tsc_resume();
		power_req.request_type = PMR_PPM_EXIT_SX;
		power_req.req.ppm_power_enter_sx_req.sx_state = pm_cpr_state;

		PMD(PMD_SX, ("%s: pm_ctlops PMR_PPM_EXIT_SX\n", str))
		PT(PT_PPMCTLOP);
		(void) pm_ctlops(ppm, ddi_root_node(), DDI_CTLOPS_POWER,
		    &power_req, &ret);
		PMD(PMD_SX, ("%s: returns %d\n", str, ret))

		ret = i_cpr_restore_apic(&(wc_other_cpus->wc_apic_state));
		/*
		 * the restore should never fail, if the saved suceeded
		 */
		ASSERT(ret == 0);

		i_cpr_platform_free(&(wc_other_cpus->wc_apic_state));

		/*
		 * Enable interrupts on boot cpu.
		 */
		ASSERT(CPU->cpu_id == i_cpr_bootcpuid());
		mutex_enter(&cpu_lock);
		cpu_enable_intr(CPU);
		mutex_exit(&cpu_lock);

		PT(PT_INTRRESTORE);
		intr_restore(saved_intr);
		PT(PT_CPU);

		return (ret);
	}
}

/*
 * Stop all other cpu's before halting or rebooting. We pause the cpu's
 * instead of sending a cross call.
 * Stolen from sun4/os/mp_states.c
 */

static int cpu_are_paused;	/* sic */

void
i_cpr_stop_other_cpus(void)
{
	mutex_enter(&cpu_lock);
	if (cpu_are_paused) {
		mutex_exit(&cpu_lock);
		return;
	}
	pause_cpus(NULL);
	cpu_are_paused = 1;

	mutex_exit(&cpu_lock);
}

int
i_cpr_is_supported(int sleeptype)
{
	extern int cpr_supported_override;
	extern int cpr_platform_enable;
	extern int pm_suspend_enabled;

	/*
	 * Check the system capability array.  If it doesn't support
	 * this state, fail.
	 */
	if ((pm_syscap[sleeptype] == NULL) && !pm_cpr_state_override)
		return (0);

	/*
	 * The next statement tests if a specific platform has turned off
	 * cpr support.
	 */
	if (cpr_supported_override)
		return (0);

	/*
	 * If a platform has specifically turned on cpr support ...
	 */
	if (cpr_platform_enable) {
		pm_cpr_state = SYSTEM_POWER_OVERRIDE;
		return (1);
	}

	return (pm_suspend_enabled);
}

void
i_cpr_free_memory_resources(void)
{
	if (CPR->c_mapping_area) {
		vmem_free(heap_arena, CPR->c_mapping_area,
		    mmu_ptob(CPR_MAXCONTIG));
		CPR->c_mapping_area = NULL;
	}
}

/*
 * Needed only for suspend so far
 */
static int
i_cpr_platform_alloc(psm_state_request_t *req)
{
#ifdef DEBUG
	char	*str = "i_cpr_platform_alloc";
#endif

	PMD(PMD_SX, ("cpu = %d, %s(%p) \n", CPU->cpu_id, str, (void *)req))

	if (psm_state == NULL) {
		PMD(PMD_SX, ("%s() : psm_state == NULL\n", str))
		return (0);
	}

	req->psr_cmd = PSM_STATE_ALLOC;
	return ((*psm_state)(req));
}

/*
 * Needed only for suspend so far
 */
static void
i_cpr_platform_free(psm_state_request_t *req)
{
#ifdef DEBUG
	char	*str = "i_cpr_platform_free";
#endif

	PMD(PMD_SX, ("cpu = %d, %s(%p) \n", CPU->cpu_id, str, (void *)req))

	if (psm_state == NULL) {
		PMD(PMD_SX, ("%s() : psm_state == NULL\n", str))
		return;
	}

	req->psr_cmd = PSM_STATE_FREE;
	(void) (*psm_state)(req);
}

static int
i_cpr_save_apic(psm_state_request_t *req)
{
#ifdef DEBUG
	char	*str = "i_cpr_save_apic";
#endif

	if (psm_state == NULL) {
		PMD(PMD_SX, ("%s() : psm_state == NULL\n", str))
		return (0);
	}

	req->psr_cmd = PSM_STATE_SAVE;
	return ((*psm_state)(req));
}

static int
i_cpr_restore_apic(psm_state_request_t *req)
{
#ifdef DEBUG
	char	*str = "i_cpr_restore_apic";
#endif

	if (psm_state == NULL) {
		PMD(PMD_SX, ("%s() : psm_state == NULL\n", str))
		return (0);
	}

	req->psr_cmd = PSM_STATE_RESTORE;
	return ((*psm_state)(req));
}


static void
init_real_mode_platter(int cpun, uint32_t offset, uint_t cr4, wc_desctbr_t gdt)
{
	/*LINTED*/
	rm_platter_t *real_mode_platter = (rm_platter_t *)rm_platter_va;

	/*
	 * Fill up the real mode platter to make it easy for real mode code to
	 * kick it off. This area should really be one passed by boot to kernel
	 * and guaranteed to be below 1MB and aligned to 16 bytes. Should also
	 * have identical physical and virtual address in paged mode.
	 */

	real_mode_platter->rm_pdbr = getcr3();
	real_mode_platter->rm_cpu = cpun;
	real_mode_platter->rm_cr4 = cr4;

	real_mode_platter->rm_gdt_base = gdt.base;
	real_mode_platter->rm_gdt_lim = gdt.limit;

	if (getcr3() > 0xffffffffUL)
		panic("Cannot initialize CPUs; kernel's 64-bit page tables\n"
		    "located above 4G in physical memory (@ 0x%llx).",
		    (unsigned long long)getcr3());

	/*
	 * Setup pseudo-descriptors for temporary GDT and IDT for use ONLY
	 * by code in real_mode_start():
	 *
	 * GDT[0]:  NULL selector
	 * GDT[1]:  64-bit CS: Long = 1, Present = 1, bits 12, 11 = 1
	 *
	 * Clear the IDT as interrupts will be off and a limit of 0 will cause
	 * the CPU to triple fault and reset on an NMI, seemingly as reasonable
	 * a course of action as any other, though it may cause the entire
	 * platform to reset in some cases...
	 */
	real_mode_platter->rm_temp_gdt[0] = 0ULL;
	real_mode_platter->rm_temp_gdt[TEMPGDT_KCODE64] = 0x20980000000000ULL;

	real_mode_platter->rm_temp_gdt_lim = (ushort_t)
	    (sizeof (real_mode_platter->rm_temp_gdt) - 1);
	real_mode_platter->rm_temp_gdt_base = rm_platter_pa +
	    (uint32_t)(&((rm_platter_t *)0)->rm_temp_gdt);

	real_mode_platter->rm_temp_idt_lim = 0;
	real_mode_platter->rm_temp_idt_base = 0;

	/*
	 * Since the CPU needs to jump to protected mode using an identity
	 * mapped address, we need to calculate it here.
	 */
	real_mode_platter->rm_longmode64_addr = rm_platter_pa + offset;

	/* return; */
}

/*
 * This function is called (via cpr_start_cpu_func()) in wc_rm_start for
 * each CPU that is passed the rm_platter at start.  However, some things
 * are not intended for the boot CPU, so it is important that items
 * that are common to *all* CPU's are done at the start of the function.
 */
void
i_cpr_start_cpu(void)
{

	struct cpu *cp = CPU;

	char *str = "i_cpr_start_cpu";
	extern void init_cpu_syscall(struct cpu *cp);

	PMD(PMD_SX, ("%s() called\n", str))

	PMD(PMD_SX, ("%s() #0 cp->cpu_base_spl %d\n", str,
	    cp->cpu_base_spl))

	/*
	 * If we use XSAVE, we need to restore XFEATURE_ENABLE_MASK register.
	 * The CR4 register was restored from the saved stack, so all we
	 * need here is to call setup_xfem().
	 */
	if (fp_save_mech == FP_XSAVE) {
		setup_xfem();
	}

	/*
	 * Initialization common to all CPU's should be done before this
	 * point, as the boot cpu will return back to the resume startup
	 * code.
	 */
	mutex_enter(&cpu_lock);
	if (cp == i_cpr_bootcpu()) {
		mutex_exit(&cpu_lock);
		PMD(PMD_SX,
		    ("%s() called on bootcpu nothing more to do!\n", str))
		return;
	}
	mutex_exit(&cpu_lock);

	/*
	 * We need to Sync PAT with cpu0's PAT. We have to do
	 * this with interrupts disabled.
	 */
	if (is_x86_feature(x86_featureset, X86FSET_PAT))
		pat_sync();

	/*
	 * Initialize this CPU's syscall handlers
	 */
	init_cpu_syscall(cp);

	PMD(PMD_SX, ("%s() #1 cp->cpu_base_spl %d\n", str, cp->cpu_base_spl))

	/*
	 * Do not need to call cpuid_pass2(), cpuid_pass3(), cpuid_pass4() or
	 * init_cpu_info(), since the work that they do is only needed to
	 * be done once at boot time
	 */


	mutex_enter(&cpu_lock);
	CPUSET_ADD(procset, cp->cpu_id);
	mutex_exit(&cpu_lock);

	PMD(PMD_SX, ("%s() #2 cp->cpu_base_spl %d\n", str,
	    cp->cpu_base_spl))

	if (tsc_gethrtime_enable) {
		PMD(PMD_SX, ("%s() calling tsc_sync_slave\n", str))
		tsc_sync_slave();
	}

	PMD(PMD_SX, ("%s() cp->cpu_id %d, cp->cpu_intr_actv %d\n", str,
	    cp->cpu_id, cp->cpu_intr_actv))
	PMD(PMD_SX, ("%s() #3 cp->cpu_base_spl %d\n", str,
	    cp->cpu_base_spl))

	(void) spl0();		/* enable interrupts */

	PMD(PMD_SX, ("%s() #4 cp->cpu_base_spl %d\n", str,
	    cp->cpu_base_spl))

	/*
	 * Set up the CPU module for this CPU.  This can't be done before
	 * this CPU is made CPU_READY, because we may (in heterogeneous systems)
	 * need to go load another CPU module.  The act of attempting to load
	 * a module may trigger a cross-call, which will ASSERT unless this
	 * cpu is CPU_READY.
	 */

	/*
	 * cmi already been init'd (during boot), so do not need to do it again
	 */
#ifdef PM_REINITMCAONRESUME
	if (is_x86_feature(x86_featureset, X86FSET_MCA))
		cmi_mca_init();
#endif

	PMD(PMD_SX, ("%s() returning\n", str))

	/* return; */
}

/*
 * I see a problem here w.r.t. cpu hotplugging, or more precisely, what
 * will happen if a cpu is removed while the OS is frozen?  If one is
 * added, we could deal with it on resume, but we might have a problem
 * trying to deal with one that has dissapeared.
 */
void
i_cpr_alloc_cpus(void)
{
	PMD(PMD_SX, ("i_cpr_alloc_cpus() CPU->cpu_id %d\n", CPU->cpu_id))
	/*
	 * we allocate this only when we actually need it to save on
	 * kernel memory
	 */

	if (wc_other_cpus == NULL) {
		wc_other_cpus = kmem_zalloc(max_ncpus * sizeof (wc_cpu_t),
		    KM_SLEEP);
	}

}

void
i_cpr_free_cpus(void)
{
	int index;
	wc_cpu_t *wc_cpu;

	if (wc_other_cpus != NULL) {
		for (index = 0; index < max_ncpus; index++) {
			wc_cpu = wc_other_cpus + index;
			if (wc_cpu->wc_saved_stack != NULL) {
				kmem_free(wc_cpu->wc_saved_stack,
				    wc_cpu->wc_saved_stack_size);
			}
		}

		kmem_free((void *) wc_other_cpus,
		    max_ncpus * sizeof (wc_cpu_t));
		wc_other_cpus = NULL;
	}
}

/*
 * wrapper for acpica_ddi_save_resources()
 */
void
i_cpr_save_configuration(dev_info_t *dip)
{
	acpica_ddi_save_resources(dip);
}

/*
 * wrapper for acpica_ddi_restore_resources()
 */
void
i_cpr_restore_configuration(dev_info_t *dip)
{
	acpica_ddi_restore_resources(dip);
}

static int
wait_for_set(cpuset_t *set, int who)
{
	int delays;
	char *str = "wait_for_set";

	for (delays = 0; !CPU_IN_SET(*set, who); delays++) {
		if (delays == 500) {
			/*
			 * After five seconds, things are probably
			 * looking a bit bleak - explain the hang.
			 */
			cmn_err(CE_NOTE, "cpu%d: started, "
			    "but not running in the kernel yet", who);
			PMD(PMD_SX, ("%s() %d cpu started "
			    "but not running in the kernel yet\n",
			    str, who))
		} else if (delays > 2000) {
			/*
			 * We waited at least 20 seconds, bail ..
			 */
			cmn_err(CE_WARN, "cpu%d: timed out", who);
			PMD(PMD_SX, ("%s() %d cpu timed out\n",
			    str, who))
			return (0);
		}

		/*
		 * wait at least 10ms, then check again..
		 */
		drv_usecwait(10000);
	}

	return (1);
}

static	void
i_cpr_save_stack(kthread_t *t, wc_cpu_t *wc_cpu)
{
	size_t	stack_size;	/* size of stack */
	caddr_t	start = CPR_GET_STACK_START(t);	/* stack start */
	caddr_t	end = CPR_GET_STACK_END(t);	/* stack end  */

	stack_size = (size_t)end - (size_t)start;

	if (wc_cpu->wc_saved_stack_size < stack_size) {
		if (wc_cpu->wc_saved_stack != NULL) {
			kmem_free(wc_cpu->wc_saved_stack,
			    wc_cpu->wc_saved_stack_size);
		}
		wc_cpu->wc_saved_stack = kmem_zalloc(stack_size, KM_SLEEP);
		wc_cpu->wc_saved_stack_size = stack_size;
	}

	bcopy(start, wc_cpu->wc_saved_stack, stack_size);
}

void
i_cpr_restore_stack(kthread_t *t, greg_t *save_stack)
{
	size_t	stack_size;	/* size of stack */
	caddr_t	start = CPR_GET_STACK_START(t);	/* stack start */
	caddr_t	end = CPR_GET_STACK_END(t);	/* stack end  */

	stack_size = (size_t)end - (size_t)start;

	bcopy(save_stack, start, stack_size);
}

/*
 * 'mdep' is currently not used in this function, but it would be
 * good to also verify that when needed, it is legitimate.
 */
/*ARGSUSED*/
int
i_cpr_checkargs(int fcn, void *mdep)
{
	extern struct vfs *rootvfs;

	/* Unsupported or invalid configurations */
	if (!i_cpr_is_supported(pm_cpr_state))
		return (ENOTSUP);

	if ((pm_cpr_save_state != 0 && !cpr_is_zfs(rootvfs)) ||
	    ((pm_cpr_state == SYSTEM_POWER_S4) && !(pm_cpr_save_state)))
		return (ENOTSUP);

	switch (fcn) {
	case AD_SUSPEND:
		/*
		 * This should be the common entry.  We already determined
		 * above that suspend is supported, so return 0.
		 */
		return (0);

	case AD_SUSPEND_TO_RAM:
	case AD_LOOPBACK_SUSPEND_TO_RAM_PASS:
	case AD_LOOPBACK_SUSPEND_TO_RAM_FAIL:
	case AD_DEVICE_SUSPEND_TO_RAM:
	case AD_CPR_TESTZ:
	case AD_CPR_TESTNOZ:
		/*
		 * Functions that imply SYSTEM_POWER_S3, so make the
		 * change to the global variable.
		 */
		if (i_cpr_is_supported(SYSTEM_POWER_S3))
			pm_cpr_state = SYSTEM_POWER_S3;
		else
			return (ENOTSUP);
		break;

	case AD_CHECK:
		/*
		 * For x86 platforms, we want this case to fail if
		 * S4 is not supported for this platform.
		 */
		if (!i_cpr_is_supported(SYSTEM_POWER_S4))
			return (ENOTSUP);
		break;

	case AD_CHECK_SUSPEND_TO_RAM:
		if (!i_cpr_is_supported(SYSTEM_POWER_S3))
			return (ENOTSUP);
		break;

	case AD_CPR_REUSEINIT:
	case AD_CPR_REUSABLE:
	case AD_CPR_REUSEFINI:
		/*
		 * Reusable Statefile, though it may be supported, we
		 * shouldn't actually suspend.
		 */
		pm_cpr_state = SYSTEM_POWER_S0;
		break;

	case AD_CPR_NOCOMPRESS:
	case AD_CPR_TESTHALT:
		/*
		 * Functions that imply SYSTEM_POWER_S4, so make the
		 * change to the global variable.
		 */
		if (i_cpr_is_supported(SYSTEM_POWER_S4))
			pm_cpr_state = SYSTEM_POWER_S4;
		else
			return (ENOTSUP);
		break;

	case AD_CPR_FORCE:	/* Obsolete */
	case AD_FORCE_SUSPEND_TO_RAM:	/* Doesn't work */
	default:
		return (ENOTSUP);
	}

	return (0);
}

/*
 * save/restore prom pages or free related allocs
 */
/*ARGSUSED*/
int
i_cpr_prom_pages(int action)
{
	return (0);
}

/*
 * Return the virtual address of the mapping area
 */
caddr_t
i_cpr_map_setup(void)
{
	caddr_t	vaddr;

	/*
	 * Allocate a virtual memory range.
	 * This would be 8 hments or 64k bytes.  Starting VA
	 * must be 64k (8-page) aligned.
	 */
	vaddr = vmem_xalloc(heap_arena,
	    mmu_ptob(CPR_MAXCONTIG), mmu_ptob(CPR_MAXCONTIG),
	    0, 0, NULL, NULL, VM_NOSLEEP);
	return (vaddr);
}

/*
 * Allocate bitmaps according to the phys_install list.
 */
static int
i_cpr_bitmap_setup(void)
{
	struct memlist *pmem;
	cbd_t *dp, *tail;
	void *space;
	size_t size;

	/*
	 * The number of bitmap descriptors will be the count of
	 * phys_install ranges plus 1 for a trailing NULL struct.
	 */
	cpr_nbitmaps = 1;
	for (pmem = phys_install; pmem; pmem = pmem->ml_next)
		cpr_nbitmaps++;

	if (cpr_nbitmaps > (CPR_MAX_BMDESC - 1)) {
		cpr_err(CE_WARN, "too many physical memory ranges %d, max %d",
		    cpr_nbitmaps, CPR_MAX_BMDESC - 1);
		return (EFBIG);
	}

	/* Alloc an array of bitmap descriptors. */
	dp = kmem_zalloc(cpr_nbitmaps * sizeof (*dp), KM_NOSLEEP);
	if (dp == NULL) {
		cpr_nbitmaps = 0;
		return (ENOMEM);
	}
	tail = dp + cpr_nbitmaps;

	CPR->c_bmda = dp;
	for (pmem = phys_install; pmem; pmem = pmem->ml_next) {
		size = BITMAP_BYTES(pmem->ml_size);
		space = kmem_zalloc(size * 2, KM_NOSLEEP);
		if (space == NULL)
			return (ENOMEM);
		ASSERT(dp < tail);
		dp->cbd_magic = CPR_BITMAP_MAGIC;
		dp->cbd_spfn = mmu_btop(pmem->ml_address);
		dp->cbd_epfn = mmu_btop(pmem->ml_address + pmem->ml_size) - 1;
		dp->cbd_size = size;
		dp->cbd_reg_bitmap = (cpr_ptr)space;
		dp->cbd_vlt_bitmap = (cpr_ptr)((caddr_t)space + size);
		dp++;
	}

	/* set magic for the last descriptor */
	ASSERT(dp == (tail - 1));
	dp->cbd_magic = CPR_BITMAP_MAGIC;

	return (0);
}

void
i_cpr_bitmap_cleanup(void)
{
	cbd_t *dp;

	if (CPR->c_bmda == NULL)
		return;
	for (dp = CPR->c_bmda; dp->cbd_size; dp++)
		kmem_free((void *)dp->cbd_reg_bitmap, dp->cbd_size * 2);
	kmem_free(CPR->c_bmda, cpr_nbitmaps * sizeof (*CPR->c_bmda));
	CPR->c_bmda = NULL;
	cpr_nbitmaps = 0;
}

int
i_cpr_alloc_bitmaps(void)
{
	int err;

	memlist_read_lock();
	err = i_cpr_bitmap_setup();
	memlist_read_unlock();
	if (err)
		i_cpr_bitmap_cleanup();
	return (err);
}

/*
 * The purpose of this function is to rewrite the first data block
 * of the statefile after the dump is written with header information
 * that contains the actual filesize, rather than the estimate.  The
 * the easiest way is to remember the contents of the first block,
 * and at the end, update it and rewrite it.
 * Unfortunately, the prototype based on the Sparc version was
 * having issues.  This should be fixed before x86 saving of state
 * work is complete.
 */
/*ARGSUSED*/
int
i_cpr_blockzero(char *base, char **bufpp, int *blkno, vnode_t *vp)
{
	return (0);
}

/*ARGSUSED*/
int
i_cpr_check_pgs_dumped(uint_t pgs_expected, uint_t regular_pgs_dumped)
{
	return (0);
}

/*ARGSUSED*/
int
i_cpr_compress_and_save(int chunks, pfn_t spfn, pgcnt_t pages)
{
	return (0);
}

pgcnt_t
i_cpr_count_sensitive_kpages(int mapflag, bitfunc_t bitfunc)
{
	pgcnt_t kdata_cnt = 0, segkmem_cnt = 0;
	extern caddr_t e_moddata, s_data;
	extern struct seg kvalloc;
	size_t size;

	/*
	 * Kernel data nucleus pages
	 */
	/*LINTED*/
	size = e_moddata - s_data;
	kdata_cnt += cpr_count_pages(s_data, size,
	    mapflag, bitfunc, DBG_SHOWRANGE);

	/*
	 * kvseg and kvalloc pages
	 */
	segkmem_cnt += cpr_scan_kvseg(mapflag, bitfunc, &kvseg);
	segkmem_cnt += cpr_count_pages(kvalloc.s_base, kvalloc.s_size,
	    mapflag, bitfunc, DBG_SHOWRANGE);

	CPR_DEBUG(CPR_DEBUG7, "\ni_cpr_count_sensitive_kpages:\n"
	    "\tkdata_cnt %ld + segkmem_cnt %ld = %ld pages\n",
	    kdata_cnt, segkmem_cnt, kdata_cnt + segkmem_cnt);

	return (kdata_cnt + segkmem_cnt);
}

/*ARGSUSED*/
pgcnt_t
i_cpr_count_special_kpages(int mapflag, bitfunc_t bitfunc)
{
	return (0);
}

/*ARGSUSED*/
pgcnt_t
i_cpr_count_storage_pages(int mapflag, bitfunc_t bitfunc)
{
	return (0);
}

/*ARGSUSED*/
int
i_cpr_dump_sensitive_kpages(vnode_t *vp)
{
	return (0);
}

/*
 * Initialize any special structs for this platform.
 */
/*ARGSUSED*/
int
i_cpr_dump_setup(vnode_t *vp)
{
	/*
	 * Zero the m_info data.
	 */
	bzero(&m_info, sizeof (m_info));
	return (0);
}

/*
 * Map a pfn into a cpr transfer buffer.  Should be equally matched
 * with a i_cpr_mapout.
 */
void
i_cpr_mapin(caddr_t vaddr, uint_t pages, pfn_t pfn)
{
	extern pfn_t curthreadpfn;
	extern int curthreadremapped;

	curthreadremapped = (pfn <= curthreadpfn && curthreadpfn < pfn + pages);

	/*
	 * I suspect that we could be more optimized that incrementing
	 * through pages based on MMU_PAGESIZE, but this does work.
	 */
	for (; pages--; pfn++, vaddr += MMU_PAGESIZE) {
		hat_devload(kas.a_hat, vaddr, MMU_PAGESIZE,
		    pfn, PROT_READ, HAT_LOAD_NOCONSIST);
	}
}

/*
 * Release a pfn mapping.
 */
void
i_cpr_mapout(caddr_t vaddr, uint_t pages)
{
	extern int curthreadremapped;

	if (curthreadremapped && vaddr <= (caddr_t)curthread &&
	    (caddr_t)curthread < vaddr + pages * MMU_PAGESIZE)
		curthreadremapped = 0;

	for (; pages--; vaddr += MMU_PAGESIZE) {
		hat_unload(kas.a_hat, vaddr, MMU_PAGESIZE, HAT_UNLOAD);
	}
}

/*
 * Go throught the kernel nucleus and segkmem segments to count pages
 * in use.
 * Currently not used on x86, but should be once save/restore is
 * somewhat known to work.
 */
int
i_cpr_save_sensitive_kpages(void)
{
	return (0);
}

/*
 * Write machine dependent information to the statefile.
 * This will be the machine discription followed by the machine info.
 */
/*ARGSUSED*/
int
i_cpr_write_machdep(vnode_t *vp)
{
	cmd_t cmach;
	label_t *ltp;
	uintptr_t tinfo;	/* "generic" use pointer */
	int rc;
	extern int cpr_test_mode;

	/* The machine discriptor basically describes the machine info */
	cmach.md_magic = (uint_t)CPR_MACHDEP_MAGIC;
	cmach.md_size = sizeof (m_info);

	if (rc = cpr_write(vp, (caddr_t)&cmach, sizeof (cmach))) {
		cpr_err(CE_WARN, "Failed to write descriptor.");
		return (rc);
	}

	/*
	 * m_info is cleared by i_cpr_dump_setup(), so fill in
	 * relevant data here.  Note that much if this is necessary
	 * for the x86 version of cprboot.  Mostly, having the entire
	 * header is needed for block zero accounting, but this is where
	 * much of the information for resume can be found.
	 */
	ltp = &ttolwp(curthread)->lwp_qsav;
	m_info.qsav_pc = (cpr_ext)ltp->val[0];
	m_info.qsav_sp = (cpr_ext)ltp->val[1];
	tinfo = (uintptr_t)curthread;
	m_info.thrp = (cpr_ptr)tinfo;
	m_info.test_mode = cpr_test_mode;


	if (rc = cpr_write(vp, (caddr_t)&m_info, sizeof (m_info))) {
		cpr_err(CE_WARN, "Failed to write machdep info.");
		return (rc);
	}

	return (0);
}

int
i_cpr_get_config_path(char *path) {
	char	pool[MAXPATHLEN], *sep;
	size_t	size;

	/* Caller sends us a pre-allocated buffer */
	ASSERT(path != NULL);

	if (strcmp(rootfs.bo_fstype, "ufs") == 0) {
		/* UFS, just use CPR_CONFIG */
		(void) strncpy(path, CPR_CONFIG, sizeof (CPR_CONFIG));
		return (0);
	} else if (strcmp(rootfs.bo_fstype, "zfs") == 0) {
		/* ZFS, prepend the pool to CPR_CONFIG */
		if ((sep = strstr(rootfs.bo_name, "/ROOT")) == NULL) {
			/*
			 * Don't have a properly described pool,
			 * not much we can do here, so return an
			 * error.
			 */
			return (EINVAL);
		}
		bzero(pool, MAXPATHLEN);
		/*
		 * since the zfs name is like 'rpool/ROOT', we wish to get
		 * everything between the beginning of the name, up to
		 * but not including where 'sep' points to, and prepend
		 * it to CPR_CONFIG.  This string *should not* contain a
		 * leading '/', so ASSERT for it, and hope that the
		 * format is otherwise good on non-debug kernels.
		 */
		/*LINTED*/
		size = (size_t)(sep - rootfs.bo_name);
		(void) strncpy(pool, rootfs.bo_name, size);
		ASSERT(pool[0] != '/');

		(void) sprintf(path, "/%s%s", pool, CPR_CONFIG);
		return (0);

	}

	/* If we get here, nothing matches, return ENOTSUP. */
	return (ENOTSUP);
}

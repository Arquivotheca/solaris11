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

#define	PSMI_1_7

#include <sys/mutex.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/clock.h>
#include <sys/machlock.h>
#include <sys/smp_impldefs.h>
#include <sys/uadmin.h>
#include <sys/promif.h>
#include <sys/psm.h>
#include <sys/psm_common.h>
#include <sys/atomic.h>
#include <sys/apic.h>
#include <sys/archsystm.h>
#include <sys/mach_intr.h>
#include <sys/hypervisor.h>
#include <sys/evtchn_impl.h>
#include <sys/modctl.h>
#include <sys/trap.h>
#include <sys/panic.h>
#include <sys/sysmacros.h>
#include <sys/pci_intr_lib.h>
#include <vm/hat_i86.h>

#include <xen/public/vcpu.h>
#include <xen/public/physdev.h>


/*
 * Global Data
 */

int xen_psm_verbose = 0;

int xen_psm_intr_policy = INTR_ROUND_ROBIN_WITH_AFFINITY;
/* use to make sure only one cpu handles the nmi */
static lock_t xen_psm_nmi_lock;
int xen_psm_kmdb_on_nmi = 0;		/* 0 - no, 1 - yes enter kmdb */
int xen_psm_panic_on_nmi = 0;
int xen_psm_num_nmis = 0;

cpuset_t xen_psm_cpus_online;	/* online cpus */
int xen_psm_ncpus = 1;		/* cpu count */
int xen_psm_next_bind_cpu;	/* next cpu to bind an interrupt to */


static int xen_clock_irq = INVALID_IRQ;

/* flag definitions for xen_psm_verbose */
#define	XEN_PSM_VERBOSE_IRQ_FLAG		0x00000001
#define	XEN_PSM_VERBOSE_POWEROFF_FLAG		0x00000002
#define	XEN_PSM_VERBOSE_POWEROFF_PAUSE_FLAG	0x00000004

#define	XEN_PSM_VERBOSE_IRQ(fmt) \
	if (xen_psm_verbose & XEN_PSM_VERBOSE_IRQ_FLAG) \
		cmn_err fmt;

#define	XEN_PSM_VERBOSE_POWEROFF(fmt) \
	if (xen_psm_verbose & XEN_PSM_VERBOSE_POWEROFF_FLAG) \
		prom_printf fmt;

/*
 * Dummy apic array to point common routines at that want to do some apic
 * manipulation.  Xen doesn't allow guest apic access so we point at these
 * memory locations to fake out those who want to do apic fiddling.
 */
uint32_t xen_psm_dummy_apic[APIC_IRR_REG + 1];

static struct psm_info xen_psm_info;
static void xen_psm_setspl(int);

/*
 * Local support routines
 */

/*
 * Select vcpu to bind xen virtual device interrupt to.
 */
/*ARGSUSED*/
int
xen_psm_bind_intr(int irq)
{
	int bind_cpu;

	bind_cpu = IRQ_UNBOUND;
	if (xen_psm_intr_policy == INTR_LOWEST_PRIORITY)
		return (bind_cpu);
	if (xen_psm_intr_policy == INTR_ROUND_ROBIN_WITH_AFFINITY) {
		do {
			bind_cpu = xen_psm_next_bind_cpu++;
			if (xen_psm_next_bind_cpu >= xen_psm_ncpus)
				xen_psm_next_bind_cpu = 0;
		} while (!CPU_IN_SET(xen_psm_cpus_online, bind_cpu));
	} else {
		bind_cpu = 0;
	}
done:
	return (bind_cpu);
}

/*
 * Autoconfiguration Routines
 */

static int
xen_psm_probe(void)
{
	return (PSM_SUCCESS);
}

static void
xen_psm_softinit(void)
{
	/* LINTED logical expression always true: op "||" */
	ASSERT((1 << EVTCHN_SHIFT) == NBBY * sizeof (ulong_t));
	CPUSET_ATOMIC_ADD(xen_psm_cpus_online, 0);
}

#define	XEN_NSEC_PER_TICK	10 /* XXX - assume we have a 100 Mhz clock */

/*ARGSUSED*/
static int
xen_psm_clkinit(int hertz)
{
	extern enum tod_fault_type tod_fault(enum tod_fault_type, int);
	extern int dosynctodr;

	/*
	 * domU cannot set the TOD hardware, fault the TOD clock now to
	 * indicate that and turn off attempts to sync TOD hardware
	 * with the hires timer.
	 */
	mutex_enter(&tod_lock);
	(void) tod_fault(TOD_RDONLY, 0);
	dosynctodr = 0;
	mutex_exit(&tod_lock);
	/*
	 * The hypervisor provides a timer based on the local APIC timer.
	 * The interface supports requests of nanosecond resolution.
	 * A common frequency of the apic clock is 100 Mhz which
	 * gives a resolution of 10 nsec per tick.  What we would really like
	 * is a way to get the ns per tick value from xen.
	 * XXPV - This is an assumption that needs checking and may change
	 */
	return (XEN_NSEC_PER_TICK);
}

static void
xen_psm_hrtimeinit(void)
{
	extern int gethrtime_hires;
	gethrtime_hires = 1;
}

/* xen_psm NMI handler */
/*ARGSUSED*/
static void
xen_psm_nmi_intr(caddr_t arg, struct regs *rp)
{
	xen_psm_num_nmis++;

	if (!lock_try(&xen_psm_nmi_lock))
		return;

	if (xen_psm_kmdb_on_nmi && psm_debugger()) {
		debug_enter("NMI received: entering kmdb\n");
	} else if (xen_psm_panic_on_nmi) {
		/* Keep panic from entering kmdb. */
		nopanicdebug = 1;
		panic("NMI received\n");
	} else {
		/*
		 * prom_printf is the best shot we have of something which is
		 * problem free from high level/NMI type of interrupts
		 */
		prom_printf("NMI received\n");
	}

	lock_clear(&xen_psm_nmi_lock);
}

static void
xen_psm_picinit()
{

	/* add nmi handler - least priority nmi handler */
	LOCK_INIT_CLEAR(&xen_psm_nmi_lock);

	if (!psm_add_nmintr(0, (avfunc) xen_psm_nmi_intr,
	    "xVM_psm NMI handler", (caddr_t)NULL))
		cmn_err(CE_WARN, "xVM_psm: Unable to add nmi handler");
}


/*
 * generates an interprocessor interrupt to another CPU
 */
static void
xen_psm_send_ipi(int cpun, int ipl)
{
	ulong_t flag = intr_clear();

	ec_send_ipi(ipl, cpun);
	intr_restore(flag);
}

/*ARGSUSED*/
static int
xen_psm_addspl(int irqno, int ipl, int min_ipl, int max_ipl)
{
	int cpu, ret;
	cpuset_t cpus;

	/*
	 * We are called at splhi() so we can't call anything that might end
	 * up trying to context switch.
	 */
	/*
	 * Set priority/affinity/enable for non PIRQs
	 */
	ret = ec_set_irq_priority(irqno, ipl);
	ASSERT(ret == 0);
	if ((cpu = xen_psm_bind_intr(irqno)) == IRQ_UNBOUND) {
		CPUSET_ZERO(cpus);
		CPUSET_OR(cpus, xen_psm_cpus_online);
	} else {
		CPUSET_ONLY(cpus, cpu & ~IRQ_USER_BOUND);
	}
	ec_set_irq_affinity(irqno, cpus);
	ec_enable_irq(irqno);
	return (ret);
}

/*
 * Acquire ownership of this irq on this cpu
 */
void
xen_psm_acquire_irq(int irq)
{
	ulong_t flags;
	int cpuid;

	/*
	 * If the irq is currently being serviced by another cpu
	 * we busy-wait for the other cpu to finish.  Take any
	 * pending interrupts before retrying.
	 */
	do {
		flags = intr_clear();
		cpuid = ec_block_irq(irq);
		intr_restore(flags);
	} while (cpuid != CPU->cpu_id);
}

/*ARGSUSED*/
static int
xen_psm_delspl(int irqno, int ipl, int min_ipl, int max_ipl)
{
	int err = PSM_SUCCESS;

	xen_psm_acquire_irq(irqno);
	ec_unbind_irq(irqno);
	return (err);
}

static processorid_t
xen_psm_get_next_processorid(processorid_t id)
{
	if (id == -1)
		return (0);

	for (id++; id < NCPU; id++) {
		switch (-HYPERVISOR_vcpu_op(VCPUOP_is_up, id, NULL)) {
		case 0:		/* yeah, that one's there */
			return (id);
		default:
		case X_EINVAL:	/* out of range */
			return (-1);
		case X_ENOENT:	/* not present in the domain */
			/*
			 * It's not clear that we -need- to keep looking
			 * at this point, if, e.g., we can guarantee
			 * the hypervisor always keeps a contiguous range
			 * of vcpus around this is equivalent to "out of range".
			 *
			 * But it would be sad to miss a vcpu we're
			 * supposed to be using ..
			 */
			break;
		}
	}

	return (-1);
}

/*
 * XXPV - undo the start cpu op change; return to ignoring this value
 *	- also tweak error handling in main startup loop
 */
/*ARGSUSED*/
static int
xen_psm_cpu_start(processorid_t id, caddr_t arg)
{
	int ret;

	ASSERT(id > 0);
	CPUSET_ATOMIC_ADD(xen_psm_cpus_online, id);
	ec_bind_cpu_ipis(id);
	(void) ec_bind_virq_to_irq(VIRQ_TIMER, id);
	if ((ret = xen_vcpu_up(id)) == 0)
		xen_psm_ncpus++;
	else
		ret = EINVAL;
	return (ret);
}

/*
 * Allocate an irq for inter cpu signaling
 */
/*ARGSUSED*/
static int
xen_psm_get_ipivect(int ipl, int type)
{
	return (ec_bind_ipi_to_irq(ipl, 0));
}

/*ARGSUSED*/
static int
xen_psm_get_clockirq(int ipl)
{
	if (xen_clock_irq != INVALID_IRQ)
		return (xen_clock_irq);

	xen_clock_irq = ec_bind_virq_to_irq(VIRQ_TIMER, 0);
	return (xen_clock_irq);
}

/*ARGSUSED*/
static void
xen_psm_shutdown(int cmd, int fcn)
{
	XEN_PSM_VERBOSE_POWEROFF(("xen_psm_shutdown(%d,%d);\n", cmd, fcn));

	switch (cmd) {
	case A_SHUTDOWN:
		switch (fcn) {
		case AD_BOOT:
		case AD_IBOOT:
			(void) HYPERVISOR_shutdown(SHUTDOWN_reboot);
			break;
		case AD_POWEROFF:
			/* FALLTHRU */
		case AD_HALT:
		default:
			(void) HYPERVISOR_shutdown(SHUTDOWN_poweroff);
			break;
		}
		break;
	case A_REBOOT:
		(void) HYPERVISOR_shutdown(SHUTDOWN_reboot);
		break;
	default:
		return;
	}
}


static int
xen_psm_translate_irq(dev_info_t *dip, int irqno)
{
	if (dip == NULL) {
		XEN_PSM_VERBOSE_IRQ((CE_CONT, "!xen_psm: irqno = %d"
		    " dip = NULL\n", irqno));
		return (irqno);
	}
	return (irqno);
}

/*
 * xen_psm_intr_enter() acks the event that triggered the interrupt and
 * returns the new priority level,
 */
/*ARGSUSED*/
static int
xen_psm_intr_enter(int ipl, int *vector)
{
	int newipl;
	uint_t intno;
	cpu_t *cpu = CPU;

	intno = (*vector);

	ASSERT(intno < NR_IRQS);
	ASSERT(cpu->cpu_m.mcpu_vcpu_info->evtchn_upcall_mask != 0);

	ec_clear_irq(intno);

	newipl = autovect[intno].avh_hi_pri;
	if (newipl == 0) {
		/*
		 * (newipl == 0) means we have no service routines for this
		 * vector.  We will treat this as a spurious interrupt.
		 * We have cleared the pending bit already, clear the event
		 * mask and return a spurious interrupt.  This case can happen
		 * when an interrupt delivery is racing with the removal of
		 * of the service routine for that interrupt.
		 */
		ec_unmask_irq(intno);
		return (-1);	/* flag spurious interrupt */
	} else if (newipl <= cpu->cpu_pri) {
		/*
		 * (newipl <= cpu->cpu_pri) means that we must be trying to
		 * service a vector that was shared with a higher priority
		 * isr.  The higher priority handler has been removed and
		 * we need to service this int.  We can't return a lower
		 * priority than current cpu priority.  Just synthesize a
		 * priority to return that should be acceptable.
		 * It should never happen that we synthesize a priority that
		 * moves us from low-priority to high-priority that would make
		 * a us incorrectly run on the high priority stack.
		 */
		newipl = cpu->cpu_pri + 1;	/* synthetic priority */
		ASSERT(newipl != LOCK_LEVEL + 1);
	}
	cpu->cpu_pri = newipl;
	return (newipl);
}


/*
 * xen_psm_intr_exit() restores the old interrupt
 * priority level after processing an interrupt.
 * It is called with interrupts disabled, and does not enable interrupts.
 */
/* ARGSUSED */
static void
xen_psm_intr_exit(int ipl, int vector)
{
	ec_try_unmask_irq(vector);
	xen_psm_setspl(ipl);
}

intr_exit_fn_t
psm_intr_exit_fn(void)
{
	return (xen_psm_intr_exit);
}

/*
 * Check if new ipl level allows delivery of previously unserviced events
 */
static void
xen_psm_setspl(int ipl)
{
	struct cpu *cpu = CPU;
	volatile vcpu_info_t *vci = cpu->cpu_m.mcpu_vcpu_info;
	uint16_t pending;

	ASSERT(vci->evtchn_upcall_mask != 0);

	/*
	 * If new ipl level will enable any pending interrupts, setup so the
	 * upcoming sti will cause us to get an upcall.
	 */
	pending = cpu->cpu_m.mcpu_intr_pending & ~((1 << (ipl + 1)) - 1);
	if (pending) {
		int i;
		ulong_t pending_sels = 0;
		volatile ulong_t *selp;
		struct xen_evt_data *cpe = cpu->cpu_m.mcpu_evt_pend;

		for (i = bsrw_insn(pending); i > ipl; i--)
			pending_sels |= cpe->pending_sel[i];
		ASSERT(pending_sels);
		selp = (volatile ulong_t *)&vci->evtchn_pending_sel;
		atomic_or_ulong(selp, pending_sels);
		vci->evtchn_upcall_pending = 1;
	}
}

/*
 * This function provides external interface to the nexus for all
 * functionality related to the new DDI interrupt framework.
 *
 * Input:
 * dip     - pointer to the dev_info structure of the requested device
 * hdlp    - pointer to the internal interrupt handle structure for the
 *	     requested interrupt
 * intr_op - opcode for this call
 * result  - pointer to the integer that will hold the result to be
 *	     passed back if return value is PSM_SUCCESS
 *
 * Output:
 * return value is either PSM_SUCCESS or PSM_FAILURE
 */
int
xen_intr_ops(dev_info_t *dip, ddi_intr_handle_impl_t *hdlp,
    psm_intr_op_t intr_op, int *result)
{
	int		cap;
	int		err;
	int		new_priority;
	struct intrspec *ispec;

	DDI_INTR_IMPLDBG((CE_CONT, "xen_intr_ops: dip: %p hdlp: %p "
	    "intr_op: %x\n", (void *)dip, (void *)hdlp, intr_op));

	switch (intr_op) {
	case PSM_INTR_OP_CHECK_MSI:
		/*
		 * Till PCI passthru is supported, domU has no MSI/MSIX
		 */
		*result = hdlp->ih_type & ~(DDI_INTR_TYPE_MSI |
		    DDI_INTR_TYPE_MSIX);
		break;
	case PSM_INTR_OP_ALLOC_VECTORS:
		return (PSM_FAILURE);
	case PSM_INTR_OP_FREE_VECTORS:
		return (PSM_FAILURE);
	case PSM_INTR_OP_NAVAIL_VECTORS:
		/*
		 * XXPV - maybe we should make this be:
		 * min(APIC_VECTOR_PER_IPL, count of all avail vectors);
		 */
		*result = 1;
		break;
	case PSM_INTR_OP_XLATE_VECTOR:
		ispec = ((ihdl_plat_t *)hdlp->ih_private)->ip_ispecp;
		*result = ispec->intrspec_vec;
		break;
	case PSM_INTR_OP_GET_PENDING:
		*result = ec_pending_irq(hdlp->ih_vector);
		break;
	case PSM_INTR_OP_CLEAR_MASK:
		if (hdlp->ih_type != DDI_INTR_TYPE_FIXED)
			return (PSM_FAILURE);
		ec_enable_irq(hdlp->ih_vector);
		break;
	case PSM_INTR_OP_SET_MASK:
		if (hdlp->ih_type != DDI_INTR_TYPE_FIXED)
			return (PSM_FAILURE);
		ec_disable_irq(hdlp->ih_vector);
		break;
	case PSM_INTR_OP_GET_CAP:
		cap = DDI_INTR_FLAG_PENDING | DDI_INTR_FLAG_EDGE;
		if (hdlp->ih_type == DDI_INTR_TYPE_FIXED)
			cap |= DDI_INTR_FLAG_MASKABLE;
		*result = cap;
		break;
	case PSM_INTR_OP_GET_SHARED:
		return (PSM_FAILURE);
	case PSM_INTR_OP_SET_PRI:
		new_priority = *(int *)result;
		err = ec_set_irq_priority(hdlp->ih_vector, new_priority);
		if (err != 0)
			return (PSM_FAILURE);
		break;
	case PSM_INTR_OP_GET_INTR:
		return (PSM_FAILURE);
	case PSM_INTR_OP_SET_CAP:
		/* FALLTHRU */
	default:
		return (PSM_FAILURE);
	}
	return (PSM_SUCCESS);
}

static void
xen_psm_rebind_irq(int irq)
{
	cpuset_t ncpu;
	processorid_t newcpu;

	newcpu = xen_psm_bind_intr(irq);
	if (newcpu == IRQ_UNBOUND) {
		CPUSET_ZERO(ncpu);
		CPUSET_OR(ncpu, xen_psm_cpus_online);
	} else {
		CPUSET_ONLY(ncpu, newcpu & ~IRQ_USER_BOUND);
	}
	ec_set_irq_affinity(irq, ncpu);
}

/*
 * Disable all device interrupts for the given cpu.
 * High priority interrupts are not disabled and will still be serviced.
 */
static int
xen_psm_disable_intr(processorid_t cpun)
{
	int irq;

	/*
	 * Can't offline VCPU 0 on this hypervisor.  There's no reason
	 * anyone would want to given that the CPUs are virtual. Also note
	 * that the hypervisor requires suspend/resume to be on VCPU 0.
	 */
	if (cpun == 0)
		return (PSM_FAILURE);

	CPUSET_ATOMIC_DEL(xen_psm_cpus_online, cpun);
	for (irq = 0; irq < NR_IRQS; irq++) {
		if (!ec_irq_needs_rebind(irq, cpun))
			continue;
		xen_psm_rebind_irq(irq);
	}
	return (PSM_SUCCESS);
}

static void
xen_psm_enable_intr(processorid_t cpun)
{
	int irq;

	if (cpun == 0)
		return;

	CPUSET_ATOMIC_ADD(xen_psm_cpus_online, cpun);

	/*
	 * Rebalance device interrupts among online processors
	 */
	for (irq = 0; irq < NR_IRQS; irq++) {
		if (!ec_irq_rebindable(irq))
			continue;
		xen_psm_rebind_irq(irq);
	}

}

static int
xen_psm_post_cpu_start()
{
	return (PSM_SUCCESS);
}

/*
 * This function will reprogram the timer.
 *
 * When in oneshot mode the argument is the absolute time in future at which to
 * generate the interrupt.
 *
 * When in periodic mode, the argument is the interval at which the
 * interrupts should be generated. There is no need to support the periodic
 * mode timer change at this time.
 *
 * Note that we must be careful to convert from hrtime to Xen system time (see
 * xpv_timestamp.c).
 */
static void
xen_psm_timer_reprogram(hrtime_t timer_req)
{
	hrtime_t now, timer_new, time_delta, xen_time;
	ulong_t flags;

	flags = intr_clear();
	/*
	 * We should be called from high PIL context (CBE_HIGH_PIL),
	 * so kpreempt is disabled.
	 */

	now = xpv_gethrtime();
	xen_time = xpv_getsystime();
	if (timer_req <= now) {
		/*
		 * requested to generate an interrupt in the past
		 * generate an interrupt as soon as possible
		 */
		time_delta = XEN_NSEC_PER_TICK;
	} else
		time_delta = timer_req - now;

	timer_new = xen_time + time_delta;
	if (HYPERVISOR_set_timer_op(timer_new) != 0)
		panic("can't set hypervisor timer?");
	intr_restore(flags);
}

/*
 * This function will enable timer interrupts.
 */
static void
xen_psm_timer_enable(void)
{
	ec_unmask_irq(xen_clock_irq);
}

/*
 * This function will disable timer interrupts on the current cpu.
 */
static void
xen_psm_timer_disable(void)
{
	(void) ec_block_irq(xen_clock_irq);
	/*
	 * If the clock irq is pending on this cpu then we need to
	 * clear the pending interrupt.
	 */
	ec_unpend_irq(xen_clock_irq);
}


/*
 * The hypervisor doesn't permit access to local apics directly
 */
/* ARGSUSED */
uint32_t *
mapin_apic(uint32_t addr, size_t len, int flags)
{
	/*
	 * Return a pointer to a memory area to fake out the
	 * probe code that wants to read apic registers.
	 * The dummy values will end up being ignored by xen
	 * later on when they are used anyway.
	 */
	xen_psm_dummy_apic[APIC_VERS_REG] = APIC_INTEGRATED_VERS;
	return (xen_psm_dummy_apic);
}

/* ARGSUSED */
uint32_t *
mapin_ioapic(uint32_t addr, size_t len, int flags)
{
	/*
	 * Return non-null here to fake out configure code that calls this.
	 * The i86xpv platform will not reference through the returned value..
	 */
	return ((uint32_t *)0x1);
}

/* ARGSUSED */
void
mapout_apic(caddr_t addr, size_t len)
{
}

/* ARGSUSED */
void
mapout_ioapic(caddr_t addr, size_t len)
{
}


/*
 * This function was added as part of x2APIC support in pcplusmp to resolve
 * undefined symbol in xpv_psm.
 */
void
x2apic_update_psm()
{
}

/*
 * This function was added as part of x2APIC support in pcplusmp to resolve
 * undefined symbol in xpv_psm.
 */
void
apic_ret()
{
}



/*
 * The rest of the file is just generic psm module boilerplate
 */

static struct psm_ops xen_psm_ops = {
	xen_psm_probe,				/* psm_probe		*/

	xen_psm_softinit,			/* psm_init		*/
	xen_psm_picinit,			/* psm_picinit		*/
	xen_psm_intr_enter,			/* psm_intr_enter	*/
	xen_psm_intr_exit,			/* psm_intr_exit	*/
	xen_psm_setspl,				/* psm_setspl		*/
	xen_psm_addspl,				/* psm_addspl		*/
	xen_psm_delspl,				/* psm_delspl		*/
	xen_psm_disable_intr,			/* psm_disable_intr	*/
	xen_psm_enable_intr,			/* psm_enable_intr	*/
	(int (*)(int))NULL,			/* psm_softlvl_to_irq	*/
	(void (*)(int))NULL,			/* psm_set_softintr	*/
	(void (*)(processorid_t))NULL,		/* psm_set_idlecpu	*/
	(void (*)(processorid_t))NULL,		/* psm_unset_idlecpu	*/

	xen_psm_clkinit,			/* psm_clkinit		*/
	xen_psm_get_clockirq,			/* psm_get_clockirq	*/
	xen_psm_hrtimeinit,			/* psm_hrtimeinit	*/
	xpv_gethrtime,				/* psm_gethrtime	*/

	xen_psm_get_next_processorid,		/* psm_get_next_processorid */
	xen_psm_cpu_start,			/* psm_cpu_start	*/
	xen_psm_post_cpu_start,			/* psm_post_cpu_start	*/
	xen_psm_shutdown,			/* psm_shutdown		*/
	xen_psm_get_ipivect,			/* psm_get_ipivect	*/
	xen_psm_send_ipi,			/* psm_send_ipi		*/

	xen_psm_translate_irq,			/* psm_translate_irq	*/

	(void (*)(int, char *))NULL,		/* psm_notify_error	*/
	(void (*)(int msg))NULL,		/* psm_notify_func	*/
	xen_psm_timer_reprogram,		/* psm_timer_reprogram	*/
	xen_psm_timer_enable,			/* psm_timer_enable	*/
	xen_psm_timer_disable,			/* psm_timer_disable	*/
	(void (*)(void *arg))NULL,		/* psm_post_cyclic_setup */
	(void (*)(int, int))NULL,		/* psm_preshutdown	*/
	xen_intr_ops,			/* Advanced DDI Interrupt framework */
	(int (*)(psm_state_request_t *))NULL,	/* psm_state		*/
	(int (*)(psm_cpu_request_t *))NULL	/* psm_cpu_ops		*/
};

static struct psm_info xen_psm_info = {
	PSM_INFO_VER01_5,	/* version				*/
	PSM_OWN_SYS_DEFAULT,	/* ownership				*/
	&xen_psm_ops,		/* operation				*/
	"xVM_psm",		/* machine name				*/
	"platform module"	/* machine descriptions			*/
};

static void *xen_psm_hdlp;

int
_init(void)
{
	return (psm_mod_init(&xen_psm_hdlp, &xen_psm_info));
}

int
_fini(void)
{
	return (psm_mod_fini(&xen_psm_hdlp, &xen_psm_info));
}

int
_info(struct modinfo *modinfop)
{
	return (psm_mod_info(&xen_psm_hdlp, &xen_psm_info, modinfop));
}

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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/machparam.h>
#include <sys/machsystm.h>
#include <sys/cpu.h>
#include <sys/elf_SPARC.h>
#include <vm/hat_sfmmu.h>
#include <vm/page.h>
#include <vm/vm_dep.h>
#include <sys/cpuvar.h>
#include <sys/async.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/dditypes.h>
#include <sys/sunddi.h>
#include <sys/cpu_module.h>
#include <sys/prom_debug.h>
#include <sys/vmsystm.h>
#include <sys/prom_plat.h>
#include <sys/sysmacros.h>
#include <sys/intreg.h>
#include <sys/machtrap.h>
#include <sys/ontrap.h>
#include <sys/ivintr.h>
#include <sys/atomic.h>
#include <sys/panic.h>
#include <sys/dtrace.h>
#include <vm/seg_spt.h>
#include <sys/fault.h>
#include <sys/trapstat.h>
#include <sys/mutex_impl.h>
#include <sys/platsvc.h>

char cpu_module_name[] = "sun4v-cpu";

#define	S4V_GENCPU_MAJOR	1
#define	S4V_GENCPU_MINOR	0

#define	MB(n)   ((n) * 1024 * 1024)

void
cpu_setup(void)
{
	extern int cpc_has_overflow_intr;
	extern size_t contig_mem_prealloc_base_size;

	/*
	 * The setup common to all CPU modules is done in cpu_setup_common
	 * routine.
	 */
	cpu_setup_common(NULL);

	/*
	 * hwcap information was not made available in the MD prior to
	 * RF-based platforms, so initialize cpu_hwcap_flags here if it
	 * was not set in cpu_setup_common() using MD info.  The hwcap
	 * value used here is the baseline value expected to be support
	 * by all sun4v processors beginning with N2.
	 */
	if (cpu_hwcap_flags == 0) {
		cpu_hwcap_flags |= AV_SPARC_VIS | AV_SPARC_VIS2 |
		    AV_SPARC_ASI_BLK_INIT | AV_SPARC_POPC;
	}

	cache |= (CACHE_PTAG | CACHE_IOCOHERENT);

	/*
	 * If processor supports the subset of full 64-bit virtual
	 * address space, then set VA hole accordingly.
	 *
	 * Some processors cannot prefetch instructions on pages within 4GB
	 * of the VA hole so the hole is calculated to include this "no-go"
	 * zone.
	 */
	if (va_bits < VA_ADDRESS_SPACE_BITS) {
		hole_start = (caddr_t)((1ull << (va_bits - 1)) - (1ull << 32));
		hole_end = (caddr_t)((0ull - (1ull << (va_bits - 1))) +
		    (1ull << 32));
	} else {
		hole_start = hole_end = 0;
	}

	mutex_delay = rdccr_delay;

	/*
	 * Enable CPU performance counter overflow interrupt
	 */
	cpc_has_overflow_intr = 1;

#ifdef SUN4V_CONTIG_MEM_PREALLOC_SIZE_MB
	/*
	 * Use CPU Makefile specific compile-time define (if exists)
	 * to add to the contig preallocation size.
	 */
	contig_mem_prealloc_base_size = MB(SUN4V_CONTIG_MEM_PREALLOC_SIZE_MB);
#endif
}

/*ARGSUSED*/
void
dtrace_flush_sec(uintptr_t addr)
{
	doflush(0);
}

/*ARGSUSED*/
void
cpu_fiximp(struct cpu_node *cpunode)
{
	cmn_err(CE_PANIC, "cpu_fiximp unexpectedly called");
}

void
cpu_map_exec_units(struct cpu *cp)
{
	ASSERT(MUTEX_HELD(&cpu_lock));

	/*
	 * The cpu_ipipe and cpu_fpu fields are initialized based on
	 * the execution unit sharing information from the MD. They
	 * default to the CPU id in the absence of such information.
	 */
	cp->cpu_m.cpu_ipipe = cpunodes[cp->cpu_id].exec_unit_mapping;
	if (cp->cpu_m.cpu_ipipe == NO_EU_MAPPING_FOUND)
		cp->cpu_m.cpu_ipipe = (id_t)(cp->cpu_id);

	cp->cpu_m.cpu_fpu = cpunodes[cp->cpu_id].fpu_mapping;
	if (cp->cpu_m.cpu_fpu == NO_EU_MAPPING_FOUND)
		cp->cpu_m.cpu_fpu = (id_t)(cp->cpu_id);

	/*
	 * A one-to-one mapping between an fpu and a core is assumed.
	 */
	cp->cpu_m.cpu_core = cp->cpu_m.cpu_fpu;

	/*
	 * cpu_mpipe and cpu_chip are considered equivalent to the
	 * mapping of the last-level cache.
	 */
	cp->cpu_m.cpu_mpipe = cpunodes[cp->cpu_id].lastlevel_cache_mapping;
	if (cp->cpu_m.cpu_mpipe == NO_LASTLEVEL_CACHE_MAPPING_FOUND)
		cp->cpu_m.cpu_mpipe = CPU_LASTLEVEL_CACHEID_INVALID;

	cp->cpu_m.cpu_chip = cpunodes[cp->cpu_id].lastlevel_cache_mapping;
	if (cp->cpu_m.cpu_chip == NO_LASTLEVEL_CACHE_MAPPING_FOUND)
		cp->cpu_m.cpu_chip = CPU_CHIPID_INVALID;
}

void
cpu_init_private(struct cpu *cp)
{
	cpu_map_exec_units(cp);
}

/*ARGSUSED*/
void
cpu_uninit_private(struct cpu *cp)
{}

/*
 * N2, VF, and RF CPUs provide HWTW support for TSB lookup and with HWTW
 * enabled no TSB hit information will be available. Therefore the time spent
 * in a TLB miss handler for a TSB hit will always be zero.
 * Under the assumption that future CPUs will continue to use HWTW, this is
 * considered baseline functionality common to all sun4v processors going
 * forward.
 */
int
cpu_trapstat_conf(int cmd)
{
	int status = 0;

	switch (cmd) {
	case CPU_TSTATCONF_INIT:
	case CPU_TSTATCONF_FINI:
	case CPU_TSTATCONF_ENABLE:
	case CPU_TSTATCONF_DISABLE:
		break;
	default:
		status = EINVAL;
		break;
	}
	return (status);
}

void
cpu_trapstat_data(void *buf, uint_t tstat_pgszs)
{
	tstat_pgszdata_t	*tstatp = (tstat_pgszdata_t *)buf;
	int	i;

	for (i = 0; i < tstat_pgszs; i++, tstatp++) {
		tstatp->tpgsz_kernel.tmode_itlb.ttlb_tlb.tmiss_count = 0;
		tstatp->tpgsz_kernel.tmode_itlb.ttlb_tlb.tmiss_time = 0;
		tstatp->tpgsz_user.tmode_itlb.ttlb_tlb.tmiss_count = 0;
		tstatp->tpgsz_user.tmode_itlb.ttlb_tlb.tmiss_time = 0;
		tstatp->tpgsz_kernel.tmode_dtlb.ttlb_tlb.tmiss_count = 0;
		tstatp->tpgsz_kernel.tmode_dtlb.ttlb_tlb.tmiss_time = 0;
		tstatp->tpgsz_user.tmode_dtlb.ttlb_tlb.tmiss_count = 0;
		tstatp->tpgsz_user.tmode_dtlb.ttlb_tlb.tmiss_time = 0;
	}
}

void
cpu_gen_status(uint16_t *major, uint16_t *minor, uint64_t *caps)
{
	extern int enable_1ghz_stick;

	*major = S4V_GENCPU_MAJOR;
	*minor = S4V_GENCPU_MINOR;
	*caps = DOMAIN_SUSPEND_CAP_GEN_SPEED;
	if (enable_1ghz_stick)
		*caps |= DOMAIN_SUSPEND_CAP_GEN_CPU;
}

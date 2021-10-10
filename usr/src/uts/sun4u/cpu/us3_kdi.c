/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * CPU-specific functions needed by the Kernel-Debugger Interface (KDI).  These
 * functions are invoked directly by the kernel debugger (kmdb) while the system
 * has been stopped, and as such must not use any kernel facilities that block
 * or otherwise rely on forward progress by other parts of the kernel.
 *
 * These functions may also be called before unix`_start, and as such cannot
 * use any kernel facilities that must be initialized as part of system start.
 * An example of such a facility is drv_usecwait(), which relies on a parameter
 * that is initialized by the unix module.  As a result, drv_usecwait() may not
 * be used by KDI functions.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/cpu_module.h>
#include <sys/xc_impl.h>
#include <sys/intreg.h>
#include <sys/kdi_impl.h>

/*
 * We keep our own copies, used for cache flushing, because we can be called
 * before cpu_fiximpl().
 */
static int kdi_dcache_size;
static int kdi_dcache_linesize;
static int kdi_icache_size;
static int kdi_icache_linesize;

/*
 * Assembly support for cheetah modules in cheetah_asm.s
 */
extern int idsr_busy(void);
extern void init_mondo_nocheck(xcfunc_t *func, uint64_t arg1, uint64_t arg2);
extern void shipit(int, int);
extern void kdi_flush_idcache(int, int, int, int);
extern int kdi_get_stick(uint64_t *);

static int
kdi_cpu_ready_iter(int (*cb)(int, void *), void *arg)
{
	int rc, i;

	for (rc = 0, i = 0; i < NCPU; i++) {
		if (CPU_IN_SET(cpu_ready_set, i))
			rc += cb(i, arg);
	}

	return (rc);
}

/*
 * Sends a cross-call to a specified processor.  The caller assumes
 * responsibility for repetition of cross-calls, as appropriate (MARSA for
 * debugging).
 */
static int
kdi_xc_one(int cpuid, void (*func)(uintptr_t, uintptr_t), uintptr_t arg1,
    uintptr_t arg2)
{
	uint64_t idsr;
	uint64_t busymask;

	/*
	 * if (idsr_busy())
	 *	return (KDI_XC_RES_ERR);
	 */

	init_mondo_nocheck((xcfunc_t *)func, arg1, arg2);

	shipit(cpuid, 0);

#if defined(JALAPENO) || defined(SERRANO)
	/*
	 * Lower 2 bits of the agent ID determine which BUSY/NACK pair
	 * will be used for dispatching interrupt. For now, assume
	 * there are no more than IDSR_BN_SETS CPUs, hence no aliasing
	 * issues with respect to BUSY/NACK pair usage.
	 */
	busymask  = IDSR_BUSY_BIT(cpuid);
#else /* JALAPENO || SERRANO */
	busymask = IDSR_BUSY;
#endif /* JALAPENO || SERRANO */

	if ((idsr = getidsr()) == 0)
		return (KDI_XC_RES_OK);
	else if (idsr & busymask)
		return (KDI_XC_RES_BUSY);
	else
		return (KDI_XC_RES_NACK);
}

static void
kdi_tickwait(clock_t nticks)
{
	clock_t endtick = gettick() + nticks;

	while (gettick() < endtick);
}

static void
kdi_cpu_init(int dcache_size, int dcache_linesize, int icache_size,
    int icache_linesize)
{
	kdi_dcache_size = dcache_size;
	kdi_dcache_linesize = dcache_linesize;
	kdi_icache_size = icache_size;
	kdi_icache_linesize = icache_linesize;
}

/* used directly by kdi_read/write_phys */
void
kdi_flush_caches(void)
{
	kdi_flush_idcache(kdi_dcache_size, kdi_dcache_linesize,
	    kdi_icache_size, kdi_icache_linesize);
}

void
cpu_kdi_init(kdi_t *kdi)
{
	kdi->kdi_flush_caches = kdi_flush_caches;
	kdi->mkdi_cpu_init = kdi_cpu_init;
	kdi->mkdi_cpu_ready_iter = kdi_cpu_ready_iter;
	kdi->mkdi_xc_one = kdi_xc_one;
	kdi->mkdi_tickwait = kdi_tickwait;
	kdi->mkdi_get_stick = kdi_get_stick;
}

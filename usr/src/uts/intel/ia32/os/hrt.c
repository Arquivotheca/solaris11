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
#include <sys/clock.h>
#include <sys/hrt.h>
#include <sys/dtrace.h>
#include <sys/archsystm.h>
#include <sys/mman.h>
#include <sys/atomic.h>
#include <sys/sysmacros.h>
#include <vm/seg_kp.h>
#include <vm/seg_vn.h>
#include <vm/as.h>
#include <sys/vmsystm.h>
#include <sys/cpu.h>
#include <sys/lockstat.h>
#include <sys/machlock.h>

static void hres_spin_lock(void);
static void hres_spin_unlock(void);
static void *hrt_alloc(void);

static struct anon_map *hrt_amp;
size_t hrt_size = 0;

hrtime_t
gethrtime_waitfree(void)
{
	return (dtrace_gethrtime());
}

hrtime_t
gethrtime(void)
{
	return (gethrtimef());
}

hrtime_t
gethrtime_unscaled(void)
{
	return (gethrtimeunscaledf());
}

void
scalehrtime(hrtime_t *hrt)
{
	scalehrtimef(hrt);
}

uint64_t
unscalehrtime(hrtime_t nsecs)
{
	return (unscalehrtimef(nsecs));
}

void
gethrestime(timespec_t *tp)
{
	gethrestimef(tp);
}

/*
 * Note: hres_tick does not need to raise spl because it is already at
 * CY_LOCK_LEVEL.
 */
static void
hres_spin_lock(void)
{
	uint8_t value;
	volatile uint8_t *hres_lock =
	    (uint8_t *)&hrt->hres_lock + HRES_LOCK_OFFSET;

	value = atomic_swap_8((volatile unsigned char *)hres_lock,
	    (uint8_t)-1);
	while ((value != 0) && (!panicstr)) {
		/*
		 * Reduce atomic ops by spinning until it is possible to
		 * get the lock.
		 */
		while ((*hres_lock & 1) != 0)
			ht_pause();
		value = atomic_swap_8((volatile unsigned char *)hres_lock,
		    (uint8_t)-1);
	}
}

static void
hres_spin_unlock(void)
{
	++hrt->hres_lock;
}

void
hres_tick(void)
{
	extern int hrestime_one_sec;
	extern int max_hres_adj;
	hrtime_t now, interval;
	long long adj;

	/*
	 * We need to call *gethrtimef before picking up CLOCK_LOCK (obviously,
	 * hrt->hres_last_tick can only be modified while holding CLOCK_LOCK).
	 * At worst, performing this now instead of under CLOCK_LOCK may
	 * introduce some jitter in pc_gethrestime().
	 */
	now = gethrtimef();
	hres_spin_lock();

	/*
	 * Compute the interval since last time hres_tick was called,
	 * and adjust hrt->hrtime_base and hrt->hrestime accordingly.
	 * hrt->hrtime_base is an 8 byte value (in nsec), hrt->hrestime is
	 * a timestruc_t (sec, nsec).
	 */
	interval = now - hrt->hres_last_tick;
	hrt->hrtime_base += interval;
	hrt->hrestime.tv_nsec += interval;

	/*
	 * Now that we have CLOCK_LOCK, we can update hrt->hres_last_tick
	 */
	hrt->hres_last_tick = now;

	if (hrt->hrestime_adj == 0) {
		adj = 0;
	} else if (hrt->hrestime_adj > 0) {
		if (hrt->hrestime_adj < max_hres_adj)
			adj = hrt->hrestime_adj;
		else
			adj = max_hres_adj;
	} else {
		if (hrt->hrestime_adj < -max_hres_adj)
			adj = -max_hres_adj;
		else
			adj = hrt->hrestime_adj;
	}

	hrt->timedelta -= adj;
	hrt->hrestime_adj = hrt->timedelta;
	hrt->hrestime.tv_nsec += adj;

	while (hrt->hrestime.tv_nsec >= NANOSEC) {
		hrestime_one_sec++;
		hrt->hrestime.tv_sec++;
		hrt->hrestime.tv_nsec -= NANOSEC;
	}

	/*
	 * release hrt->hres_lock.
	 */
	hres_spin_unlock();
}

/*
 * Initialize system for userland high resolution time.
 * Allocate the user/kernel shared page(s), and copy the boot-time hrt data
 * into it.
 */
void
hrt_init(void)
{
	hrt_t	*hrtp;

	if (uhrt_enable == 0)
		return;

	/*
	 * Allocate new high resolution time structure in anonymous memory,
	 * so it can be mapped into user processes.
	 */
	hrtp = (hrt_t *)hrt_alloc();
	if (hrtp == NULL) {
		uhrt_enable = 0;
		return;
	}

	plat_boot_hrt_switch(hrtp);
}

/*
 * Called during boot to allocate the new page(s) from anonymous memory for
 * the shared High Resolution Time "hrt" data structure.  Also, create a
 * kernel mapping to the page and lock the page in memory.
 */
static void *
hrt_alloc(void)
{
	caddr_t hrt_addr;

	/*
	 * hrt_size must be a non-zero multiple of page size
	 */
	if (hrt_size == 0)
		return (NULL);
	ASSERT(hrt_size == (P2ALIGN(hrt_size, PAGESIZE)));

	/*
	 * Set up anonymous memory struct.  No swap reservation is
	 * needed since the page will be locked into memory.
	 */
	hrt_amp = anonmap_alloc(hrt_size, 0, ANON_SLEEP);

	/*
	 * Allocate the page.
	 */
	hrt_addr = segkp_get_withanonmap(segkp, hrt_size,
	    KPD_NO_ANON | KPD_LOCKED | KPD_ZERO, hrt_amp);
	if (hrt_addr == NULL) {
		hrt_amp->refcnt--;
		anonmap_free(hrt_amp);
		return ((caddr_t)NULL);
	}

	/*
	 * The page is left SE_SHARED locked so that it won't be
	 * paged out or relocated (KPD_LOCKED above).
	 */
	return (hrt_addr);
}

/*
 * This function is called during process exec to map the page(s)
 * containing the kernel's hrt data into the process's address space.
 * Allocate the user virtual address space for the page(s) and set up the
 * mapping to the page(s).
 */
caddr_t
hrt_map(void)
{
	caddr_t		addr = NULL;
	struct as	*as = curproc->p_as;
	struct segvn_crargs	vn_a;
	caddr_t		kaddr = (caddr_t)hrt;
	int		error;

	if (!uhrt_enable || kaddr == NULL || (hrt_size == 0))
		return (NULL);

	ASSERT((size_t)kaddr == P2ALIGN((size_t)kaddr, PAGESIZE));
	ASSERT((hrt_size != 0) && (hrt_size == (P2ALIGN(hrt_size, PAGESIZE))));

	as_rangelock(as);
	ASSERT(PTOU(curproc)->u_shared.hrt == NULL);
	/*
	 * pass address of kernel mapping as offset to avoid VAC conflicts
	 */
	map_addr(&addr, hrt_size, (offset_t)(uintptr_t)kaddr, 1, 0);
	if (addr == NULL) {
		as_rangeunlock(as);
		return (NULL);
	}

	/*
	 * Use segvn to set up the mapping to the page.
	 */
	vn_a.vp = NULL;
	vn_a.offset = 0;
	vn_a.cred = NULL;
	vn_a.type = MAP_SHARED;
	vn_a.prot = vn_a.maxprot = PROT_READ | PROT_USER;
	vn_a.flags = 0;
	vn_a.amp = hrt_amp;
	vn_a.szc = 0;
	vn_a.lgrp_mem_policy_flags = 0;
	error = as_map(as, addr, hrt_size, segvn_create, &vn_a);
	if (error)
		addr = NULL;

	PTOU(curproc)->u_shared.hrt = (void *)addr;

	as_rangeunlock(as);

	return (addr);
}

/*
 * Called from gethrt fasttrap to return user address of mapped hrt data.
 */
hrt_t *
get_hrt(void)
{
	return (PTOU(curproc)->u_shared.hrt);
}

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/param.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <vm/as.h>
#include <sys/memtest.h>

#include "memtest_impl.h"

static memtest_rsrv_t *
memtest_reserve_findslot(void)
{
	int i;

	for (i = 0; i < memtest.mt_rsrv_maxnum; i++) {
		if (memtest.mt_rsrvs[i].mr_id == 0)
			return (&memtest.mt_rsrvs[i]);
	}

	return (NULL);
}

static int
memtest_reserve_one(memtest_rsrv_t *mr, memtest_memreq_t *mreq)
{
	pfn_t pfn;

	/*
	 * We do not yet support this feature.  In the future we could permit
	 * the injector driver to request physical memory local to a given CPU.
	 */
	if (mreq->mreq_cpuid != -1)
		return (ENOTSUP);

	mr->mr_id = ++memtest.mt_rsrv_lastid;
	mr->mr_bufaddr = kmem_alloc(mreq->mreq_size, KM_SLEEP);
	mr->mr_bufsize = mreq->mreq_size;

	pfn = hat_getpfnum(kas.a_hat, mr->mr_bufaddr);
	mr->mr_bufpaddr = (uint64_t)mmu_ptob(pfn);

	mreq->mreq_vaddr = (uintptr_t)mr->mr_bufaddr;
	mreq->mreq_paddr = mr->mr_bufpaddr;

	memtest_dprintf("rsrv: cpu %d, sz %x => id %u va %p pa %p\n",
	    mreq->mreq_cpuid, mr->mr_bufsize, mr->mr_id, mr->mr_bufaddr,
	    mr->mr_bufpaddr);

	return (0);
}

static void
memtest_release_one(memtest_rsrv_t *mr)
{
	ASSERT(mr->mr_id != 0);

	memtest_dprintf("unrsrv: id %u va %p\n", mr->mr_id,
	    mr->mr_bufaddr);

	if (mr->mr_bufaddr != NULL)
		kmem_free(mr->mr_bufaddr, mr->mr_bufsize);

	bzero(mr, sizeof (memtest_rsrv_t));
}

int
memtest_reserve(intptr_t arg, int mode, int *rvalp)
{
	memtest_memreq_t mreq;
	memtest_rsrv_t *mr;
	int rc;

	if (ddi_copyin((void *)arg, &mreq, sizeof (memtest_memreq_t), mode) < 0)
		return (EFAULT);

	if ((mr = memtest_reserve_findslot()) == NULL)
		return (ENOSPC);

	if (mreq.mreq_size > memtest.mt_rsrv_maxsize)
		return (E2BIG);

	if (mreq.mreq_size == 0)
		mreq.mreq_size = sizeof (uintptr_t);

	if ((rc = memtest_reserve_one(mr, &mreq)) != 0)
		return (rc);

	if (ddi_copyout(&mreq, (void *)arg, sizeof (memtest_memreq_t),
	    mode) < 0) {
		memtest_release_one(mr);
		return (EFAULT);
	}

	*rvalp = mr->mr_id;
	return (0);
}

int
memtest_release(intptr_t arg)
{
	int i;

	for (i = 0; i < memtest.mt_rsrv_maxnum; i++) {
		memtest_rsrv_t *mr = &memtest.mt_rsrvs[i];

		if (mr->mr_id == arg) {
			memtest_release_one(mr);
			return (0);
		}
	}

	return (ENOENT);
}

void
memtest_release_all(void)
{
	int i;

	for (i = 0; i < memtest.mt_rsrv_maxnum; i++) {
		memtest_rsrv_t *mr = &memtest.mt_rsrvs[i];
		if (mr->mr_id != 0)
			memtest_release_one(mr);
	}
}

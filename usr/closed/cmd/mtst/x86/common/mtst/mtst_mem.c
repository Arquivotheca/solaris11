/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Memory reservations, used by the commands to obtain addresses corresponding
 * to specific types of memory.
 */

#include <errno.h>
#include <umem.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/mem.h>

#include <mtst_debug.h>
#include <mtst_err.h>
#include <mtst_list.h>
#include <mtst_mem.h>
#include <mtst_memtest.h>
#include <mtst.h>

static void
mtst_mem_rsrv_print(mtst_mem_rsrv_t *mmr)
{
	char type = mmr->mmr_type == MTST_MEM_RSRV_KERNEL ? 'K' : 'U';
	const char *cmdname = mmr->mmr_cmd == NULL ? "(none)" :
	    mmr->mmr_cmd->mcmd_cmdname;
	const char *modname = (mmr->mmr_cmd == NULL ||
	    mmr->mmr_cmd->mcmd_module == NULL) ? "(none)" :
	    mmr->mmr_cmd->mcmd_module->mcpu_name;

	mtst_warn("  %3d: %c %16x %5x %10s %s\n", mmr->mmr_resnum, type,
	    mmr->mmr_res.mreq_vaddr, mmr->mmr_res.mreq_size, cmdname, modname);
}

static uint64_t
mtst_vatopfn(uintptr_t addr)
{
	mem_vtop_t vtop;
	int fd;

	/*
	 * We use the ioctl directly, rather than via libkvm, because libkvm
	 * requires us to share the kernel's data model.  mtst isn't 64-bit
	 * (yet?), and the need to do vtop translations isn't going to be the
	 * deciding factor.
	 */
	if ((fd = open("/dev/kmem", O_RDONLY)) < 0)
		return (-1ULL); /* errno is set for us */

	vtop.m_as = NULL;	/* driver will fill in our as */
	vtop.m_va = (void *)addr;

	if (ioctl(fd, MEM_VTOP, &vtop) < 0) {
		int oserr = errno;
		(void) close(fd);
		return (mtst_set_errno(oserr));
	}

	(void) close(fd);

	return (vtop.m_pfn);
}

/*ARGSUSED*/
static int
mtst_mem_rsrv_user(mtst_mem_rsrv_t *mmr)
{
	static uint_t pagesize = 0;
	uint64_t pfn;
	void *buf;

	if (mmr->mmr_res.mreq_cpuid != -1 ||
	    mmr->mmr_res.mreq_paddr != MEMTEST_MEMREQ_UNSPEC ||
	    mmr->mmr_res.mreq_vaddr != MEMTEST_MEMREQ_UNSPEC)
		return (mtst_set_errno(EINVAL));

	if (pagesize == 0)
		pagesize = sysconf(_SC_PAGESIZE);

	if (mmr->mmr_res.mreq_size == 0)
		mmr->mmr_res.mreq_size = pagesize;

	if ((buf = memalign(pagesize, mmr->mmr_res.mreq_size)) == NULL)
		return (-1); /* errno is set for us */

	mmr->mmr_res.mreq_vaddr = (uintptr_t)buf;

	if (mlock((caddr_t)(uintptr_t)mmr->mmr_res.mreq_vaddr,
	    mmr->mmr_res.mreq_size) < 0) {
		free((void *)(uintptr_t)mmr->mmr_res.mreq_vaddr);
		return (-1); /* errno is set for us */
	}
	*(uint32_t *)buf = 0;

	mtst_dprintf("user rsrv: got locked %x bytes at %x\n",
	    mmr->mmr_res.mreq_size, (uintptr_t)mmr->mmr_res.mreq_vaddr);

	/*
	 * Now that we've got our locked userland page, figure
	 * out where it lives.
	 */
	if ((pfn = mtst_vatopfn(mmr->mmr_res.mreq_vaddr)) == -1ULL) {
		int oserr = errno;
		(void) munlock((caddr_t)(uintptr_t)mmr->mmr_res.mreq_vaddr,
		    mmr->mmr_res.mreq_size);
		free((void *)(uintptr_t)mmr->mmr_res.mreq_vaddr);
		mtst_dprintf("user rsrv: vatopfn failed\n");
		return (mtst_set_errno(oserr));
	}

	mmr->mmr_res.mreq_paddr = (pfn * pagesize) +
	    (mmr->mmr_res.mreq_vaddr & (pagesize - 1));

	mtst_dprintf("user rsrv: pa for user bytes: %llx\n",
	    (u_longlong_t)mmr->mmr_res.mreq_paddr);

	return (0);
}

static void
mtst_mem_rsrv_overflow(mtst_mem_rsrv_t *failed)
{
	mtst_mem_rsrv_t *mmr;

	mtst_warn("kernel memory reservation failed: too many reservations\n");
	mtst_warn("reservations:\n");

	mtst_mem_rsrv_print(failed);
	mtst_warn("\n");

	for (mmr = mtst_list_next(&mtst.mtst_memrsrvs); mmr != NULL;
	    mmr = mtst_list_next(mmr))
		mtst_mem_rsrv_print(mmr);

	mtst_die("aggh!\n"); /* nothing much else to say */
}

static int
mtst_mem_rsrv_kernel(mtst_mem_rsrv_t *mmr)
{
	mtst_dprintf("kernel rsrv: cpu %d, size %x\n",
	    mmr->mmr_res.mreq_cpuid, mmr->mmr_res.mreq_size);

	if ((mmr->mmr_drvid = mtst_memtest_ioctl(MEMTESTIOC_MEMREQ,
	    &mmr->mmr_res)) < 0) {
		if (errno == ENOSPC)
			mtst_mem_rsrv_overflow(mmr);
		return (-1); /* errno is set for us */
	}

	return (mmr->mmr_resnum);
}

int
mtst_mem_rsrv(int type, int *cpuidp, size_t *sizep, uint64_t *vap,
    uint64_t *pap)
{
	mtst_mem_rsrv_t *mmr;
	int rc;

	ASSERT(type == MTST_MEM_RSRV_KERNEL || type == MTST_MEM_RSRV_USER);

	mmr = umem_zalloc(sizeof (mtst_mem_rsrv_t), UMEM_NOFAIL);
	mmr->mmr_cmd = mtst.mtst_curcmd;
	mmr->mmr_resnum = ++mtst.mtst_memrsrvlastid;
	mmr->mmr_type = type;

	mmr->mmr_res.mreq_cpuid = cpuidp == NULL ? -1 : *cpuidp;
	mmr->mmr_res.mreq_size = sizep == NULL ? 0 : *sizep;
	mmr->mmr_res.mreq_paddr = pap == NULL ? MEMTEST_MEMREQ_UNSPEC : *pap;
	mmr->mmr_res.mreq_vaddr = vap == NULL ? MEMTEST_MEMREQ_UNSPEC : *vap;

	if (mmr->mmr_type == MTST_MEM_RSRV_KERNEL)
		rc = mtst_mem_rsrv_kernel(mmr);
	else
		rc = mtst_mem_rsrv_user(mmr);

	if (rc < 0) {
		umem_free(mmr, sizeof (mtst_mem_rsrv_t));
		return (-1); /* errno is set for us */
	}

	mtst_list_append(&mtst.mtst_memrsrvs, mmr);

	if (cpuidp != NULL)
		*cpuidp = mmr->mmr_res.mreq_cpuid;
	if (sizep != NULL)
		*sizep = mmr->mmr_res.mreq_size;
	if (pap != NULL)
		*pap = mmr->mmr_res.mreq_paddr;
	if (vap != NULL)
		*vap = mmr->mmr_res.mreq_vaddr;

	return (rc);
}

static void
mtst_mem_unrsrv_user(mtst_mem_rsrv_t *mmr)
{
	(void) munlock((caddr_t)(uintptr_t)mmr->mmr_res.mreq_vaddr,
	    mmr->mmr_res.mreq_size);
	free((void *)(uintptr_t)mmr->mmr_res.mreq_vaddr);
}

static void
mtst_mem_unrsrv_kernel(mtst_mem_rsrv_t *mmr)
{
	if (mtst_memtest_ioctl(MEMTESTIOC_MEMREL, (void *)mmr->mmr_drvid) < 0)
		mtst_die("failed to unreserve allocation %d", mmr->mmr_resnum);
}

int
mtst_mem_unrsrv(int id)
{
	mtst_mem_rsrv_t *mmr;

	for (mmr = mtst_list_next(&mtst.mtst_memrsrvs); mmr != NULL;
	    mmr = mtst_list_next(mmr)) {
		if (mmr->mmr_resnum == id)
			break;
	}

	if (mmr == NULL)
		return (mtst_set_errno(ENOENT));

	ASSERT(mmr->mmr_type == MTST_MEM_RSRV_USER ||
	    mmr->mmr_type == MTST_MEM_RSRV_KERNEL);

	if (mmr->mmr_type == MTST_MEM_RSRV_USER)
		mtst_mem_unrsrv_user(mmr);
	else if (mmr->mmr_type == MTST_MEM_RSRV_KERNEL)
		mtst_mem_unrsrv_kernel(mmr);

	mtst_list_delete(&mtst.mtst_memrsrvs, mmr);
	umem_free(mmr, sizeof (mtst_mem_rsrv_t));

	return (0);
}

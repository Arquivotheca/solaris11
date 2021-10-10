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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


#include <sys/param.h>
#include <sys/sunddi.h>
#include <sys/bootconf.h>
#include <sys/bootvfs.h>
#include <sys/filep.h>
#include <sys/kobj.h>
#include <sys/varargs.h>
#include <sys/reboot.h>

extern void (*_kobj_printf)(void *, const char *fmt, ...);
extern int get_weakish_int(int *);
extern struct bootops *ops;
extern struct boot_fs_ops bufs_ops, bhsfs_ops;
extern int kmem_ready;

static uint64_t rd_start, rd_end;
struct boot_fs_ops *bfs_ops;
struct boot_fs_ops *bfs_tab[] = {&bufs_ops, &bhsfs_ops, NULL};

static uintptr_t scratch_max = 0;

#define	_kmem_ready	get_weakish_int(&kmem_ready)

/*
 * This one reads the ramdisk. If fi_memp is set, we copy the
 * ramdisk content to the designated buffer. Otherwise, we
 * do a "cached" read (set fi_memp to the actual ramdisk buffer).
 */
int
diskread(fileid_t *filep)
{
	uint_t blocknum;
	caddr_t diskloc;

	/* add in offset of root slice */
	blocknum = filep->fi_blocknum;

	diskloc = (caddr_t)(uintptr_t)rd_start + blocknum * DEV_BSIZE;
	if (diskloc + filep->fi_count > (caddr_t)(uintptr_t)rd_end) {
		_kobj_printf(ops, "diskread: start = 0x%p, size = 0x%x\n",
		    diskloc, filep->fi_count);
		_kobj_printf(ops, "reading beyond end of ramdisk\n");
		return (-1);
	}

	if (filep->fi_memp) {
		bcopy(diskloc, filep->fi_memp, filep->fi_count);
	} else {
		/* "cached" read */
		filep->fi_memp = diskloc;
	}

	return (0);
}

int
kobj_boot_mountroot()
{
	int i;

	if (BOP_GETPROPLEN(ops, "ramdisk_start") != 8 ||
	    BOP_GETPROP(ops, "ramdisk_start", (void *)&rd_start) != 0 ||
	    BOP_GETPROPLEN(ops, "ramdisk_end") != 8 ||
	    BOP_GETPROP(ops, "ramdisk_end", (void *)&rd_end) != 0) {
		_kobj_printf(ops,
		    "failed to get ramdisk from boot\n");
		return (-1);
	}
#ifdef KOBJ_DEBUG
	_kobj_printf(ops,
	    "ramdisk range: 0x%llx-%llx\n", rd_start, rd_end);
#endif

	for (i = 0; bfs_tab[i] != NULL; i++) {
		bfs_ops = bfs_tab[i];
		if (BRD_MOUNTROOT(bfs_ops, "dummy") == 0)
			return (0);
	}
	_kobj_printf(ops, "failed to mount ramdisk from boot\n");
	return (-1);
}

void
kobj_boot_unmountroot()
{
#ifdef	DEBUG
	if (boothowto & RB_VERBOSE)
		_kobj_printf(ops, "boot scratch memory used: 0x%lx\n",
		    scratch_max);
#endif
	(void) BRD_UNMOUNTROOT(bfs_ops);
}

/*
 * Boot time wrappers for memory allocators. Called for both permanent
 * and temporary boot memory allocations. We have to track which allocator
 * (boot or kmem) was used so that we know how to free.
 */
void *
bkmem_alloc(size_t size)
{
	/* allocate from boot scratch memory */
	void *addr;

	if (_kmem_ready)
		return (kobj_alloc(size, 0));

	/*
	 * Remember the highest BOP_ALLOC allocated address and don't free
	 * anything below it.
	 */
	addr = BOP_ALLOC(ops, 0, size, 0);
	if (scratch_max < (uintptr_t)addr + size)
		scratch_max = (uintptr_t)addr + size;
	return (addr);
}

/*ARGSUSED*/
void
bkmem_free(void *p, size_t size)
{
	/*
	 * Free only if it's not boot scratch memory.
	 */
	if ((uintptr_t)p >= scratch_max)
		kobj_free(p, size);
}

/*PRINTFLIKE1*/
void
kobj_printf(char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	_kobj_printf(ops, fmt, adx);
	va_end(adx);
}

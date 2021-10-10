/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MTST_VF_H
#define	_MTST_VF_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <errno.h>
#include <fcntl.h>
#include <kstat.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <thread.h>
#include <unistd.h>
#include <stdarg.h>

#include <sys/ddi.h>
#include <sys/file.h>
#include <sys/int_const.h>
#include <sys/ioccom.h>
#include <sys/ioctl.h>
#include <sys/lock.h>
#include <sys/mman.h>
#include <sys/open.h>
#include <sys/param.h>
#include <sys/processor.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/systeminfo.h>
#include <sys/types.h>
#include <sys/utsname.h>

/*
 * Support routines located in mtst_vf.c
 */
extern	int	vf_adjust_buf_to_local(mdata_t *);
extern	int	vf_mem_is_local(cpu_info_t *, uint64_t);
extern	int	vf_pre_test_copy_asm(mdata_t *);

#define	VF_SYS_MODE_NODEID_MASK		0x30	/* bits[5:4] */
#define	VF_SYS_MODE_NODEID_SHIFT	4

#define	VF_SYS_MODE_GET_NODEID(reg)	(((reg) & VF_SYS_MODE_NODEID_MASK) \
					>> VF_SYS_MODE_NODEID_SHIFT)

#define	VF_SYS_MODE_WAY_MASK		0x380	/* bits[9:7] */
#define	VF_SYS_MODE_WAY_SHIFT		7
#define	VF_SYS_MODE_1_WAY		0x0	/* bits[9:7] == 0 */
#define	VF_SYS_MODE_2_WAY		0x4	/* bit[9] == 1 */
#define	VF_SYS_MODE_3_WAY		0x2	/* bit[8] == 1 */
#define	VF_SYS_MODE_4_WAY		0x1	/* bit[7] == 1 */

#define	VF_SYS_MODE_GET_WAY(reg)	(((reg) & VF_SYS_MODE_WAY_MASK) \
					>> VF_SYS_MODE_WAY_SHIFT)

#define	VF_L2_CTL_CM_MASK		0x1fe000000ULL	 /* bits[32:25] */
#define	VF_L2_CTL_CM_SHIFT		 25

#define	VF_CEILING_MASK(reg)	((reg & VF_L2_CTL_CM_MASK) >> \
				    VF_L2_CTL_CM_SHIFT)

/*
 * Bits from a physical memory address needed for comparison with
 * the ceiling mask in order to determine whether the address falls
 * in the range of memory interleaved on 512B boundaries or 1GB
 * boundaries.
 */
#define	VF_ADDR_INTERLEAVE_MASK		0xff00000000ULL	/* bits[39:32] */
#define	VF_ADDR_INTERLEAVE_SHIFT	32

#define	VF_ADDR_INTERLEAVE_BITS(paddr) \
		(((paddr) & VF_ADDR_INTERLEAVE_MASK) >> \
		VF_ADDR_INTERLEAVE_SHIFT)

#define	VF_IS_512B_INTERLEAVE(reg, paddr)	(VF_ADDR_INTERLEAVE(paddr) >= \
						VF_CEILING_MASK(reg) ? 0 : 1)

/*
 * Bits from a physical memory address needed to determine its node id
 */
#define	VF_2WY_512B_ADDR_NODEID_MASK	0x200	/* bit[9] */
#define	VF_4WY_512B_ADDR_NODEID_MASK	0x600	/* bits[10:9] */
#define	VF_512B_ADDR_NODEID_SHIFT	9

#define	VF_2WY_512B_ADDR_NODEID(paddr) \
	(((paddr) & VF_2WY_512B_ADDR_NODEID_MASK) >> VF_512B_ADDR_NODEID_SHIFT)

#define	VF_4WY_512B_ADDR_NODEID(paddr) \
	(((paddr) & VF_4WY_512B_ADDR_NODEID_MASK) >> VF_512B_ADDR_NODEID_SHIFT)

#define	VF_2WY_1GB_ADDR_NODEID_MASK	0x40000000	 /* bit[30] */
#define	VF_4WY_1GB_ADDR_NODEID_MASK	0xc0000000	 /* bits[31:30] */
#define	VF_1GB_ADDR_NODEID_SHIFT	30

#define	VF_2WY_1GB_ADDR_NODEID(paddr) \
	(((paddr) & VF_2WY_1GB_ADDR_NODEID_MASK) >> VF_1GB_ADDR_NODEID_SHIFT)

#define	VF_4WY_1GB_ADDR_NODEID(paddr) \
	(((paddr) & VF_4WY_1GB_ADDR_NODEID_MASK) >> VF_1GB_ADDR_NODEID_SHIFT)


#define	VF_LFU_MAX_UNITS	4
#define	VF_LFU_MAX_LANE_SELECT	0x3fff

#ifdef	__cplusplus
}
#endif

#endif	/* _MTST_VF_H */

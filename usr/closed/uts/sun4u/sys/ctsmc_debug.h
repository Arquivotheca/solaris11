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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_SMC_DEBUG_H
#define	_SYS_SMC_DEBUG_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/inttypes.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * turn on appropriate bit depending on the feature
 * to debug
 */
#define	SMC_UTILS_DEBUG		0x00000001
#define	SMC_STREAM_DEBUG	0x00000002
#define	SMC_ASYNC_DEBUG		0x00000004
#define	SMC_WDOG_DEBUG		0x00000008
#define	SMC_DEVICE_DEBUG	0x00000010
#define	SMC_XPORT_DEBUG		0x00000020
#define	SMC_SEND_DEBUG		0x00000040
#define	SMC_RECV_DEBUG		0x00000080
#define	SMC_IPMI_DEBUG		0x00000100
#define	SMC_DEVI_DEBUG		0x00000200
#define	SMC_I2C_DEBUG		0x00000400
#define	SMC_IOC_DEBUG		0x00000800
#define	SMC_CMD_DEBUG		0x00001000
#define	SMC_REQMSG_DEBUG	0x00002000
#define	SMC_RSPMSG_DEBUG	0x00004000
#define	SMC_HWERR_DEBUG		0x00008000
#define	SMC_INTR_DEBUG		0x00010000
#define	SMC_POLLMODE_DEBUG	0x00020000
#define	SMC_KSTAT_DEBUG		0x00040000
#define	SMC_MCT_DEBUG		0x00080000
#define	SMC_DEBUG_DEBUG		0x80000000

#define	SMC_NUM_FLAGS	(8 * sizeof (uint32_t))

#ifdef	_KERNEL

#ifdef	DEBUG
void ctsmc_debug_log(uint_t ctlbit, int pri, char *fmt, ...);

#define	SMC_DEBUG0(flag, fmt)	\
	ctsmc_debug_log(flag, 1, fmt)

#define	SMC_DEBUG(flag, fmt, d1)	\
	ctsmc_debug_log(flag, 1, fmt, d1)

#define	SMC_DEBUG2(flag, fmt, d1, d2)	\
	ctsmc_debug_log(flag, 1, fmt, d1, d2)

#define	SMC_DEBUG3(flag, fmt, d1, d2, d3)	\
	ctsmc_debug_log(flag, 1, fmt, d1, d2, d3)

#define	SMC_DEBUG4(flag, fmt, d1, d2, d3, d4)	\
	ctsmc_debug_log(flag, 1, fmt, d1, d2, d3, d4)

#define	SMC_DEBUG5(flag, fmt, d1, d2, d3, d4, d5)	\
	ctsmc_debug_log(flag, 1, fmt, d1, d2, d3, d4, d5)

#define	SMC_DEBUG6(flag, fmt, d1, d2, d3, d4, d5, d6)	\
	ctsmc_debug_log(flag, 1, fmt, d1, d2, d3, d4, d5, d6)

#define	SMC_DEBUG7(flag, fmt, d1, d2, d3, d4, d5, d6, d7)	\
	ctsmc_debug_log(flag, 1, fmt, d1, d2, d3, d4, d5, d6, d7)

#define	SMC_DEBUG8(flag, fmt, d1, d2, d3, d4, d5, d6, d7, d8)	\
	ctsmc_debug_log(flag, 1, fmt, d1, d2, d3, d4, d5, d6, d7, d8)

#define	SMC_DEBUG9(flag, fmt, d1, d2, d3, d4, d5, d6, d7, d8, d9)	\
	ctsmc_debug_log(flag, 1, fmt, d1, d2, d3, d4, d5, d6, d7, \
			d8, d9)

#define	SMC_DEBUG10(flag, fmt, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10)	\
	ctsmc_debug_log(flag, 1, fmt, d1, d2, d3, d4, d5, d6, d7, \
			d8, d9, d10)

#define	SMC_HDEBUG(flag, fmt, d1)	\
	ctsmc_debug_log(flag, 0, fmt, d1)

#define	SMC_LDEBUG(flag, fmt, d1)	\
	ctsmc_debug_log(flag, 2, fmt, d1)
#else

#define	SMC_DEBUG0(flag, fmt)
#define	SMC_DEBUG(flag, fmt, d1)
#define	SMC_DEBUG2(flag, fmt, d1, d2)
#define	SMC_DEBUG3(flag, fmt, d1, d2, d3)
#define	SMC_DEBUG4(flag, fmt, d1, d2, d3, d4)
#define	SMC_DEBUG5(flag, fmt, d1, d2, d3, d4, d5)
#define	SMC_DEBUG6(flag, fmt, d1, d2, d3, d4, d5, d6)
#define	SMC_DEBUG7(flag, fmt, d1, d2, d3, d4, d5, d6, d7)
#define	SMC_DEBUG8(flag, fmt, d1, d2, d3, d4, d5, d6, d7, d8)
#define	SMC_DEBUG9(flag, fmt, d1, d2, d3, d4, d5, d6, d7, d8, d9)
#define	SMC_DEBUG10(flag, fmt, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10)

#define	SMC_HDEBUG(flag, fmt, d1)
#define	SMC_LDEBUG(flag, fmt, d1)

#endif	/* DEBUG */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SMC_DEBUG_H */

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
 * Copyright(c) 2007-2011 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_IGBVF_OSDEP_H
#define	_IGBVF_OSDEP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strlog.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/ddi.h>
#include <sys/dditypes.h>
#include <sys/sunddi.h>
#include <sys/pci.h>
#include <sys/pci_cap.h>
#include <sys/atomic.h>
#include <sys/note.h>
#include "igbvf_debug.h"

#define	usec_delay(x)		drv_usecwait(x)
#define	msec_delay(x)		drv_usecwait(x * 1000)
#define	msec_delay_irq		msec_delay

#ifdef IGBVF_DEBUG
#define	DEBUGOUT(S)		IGBVF_DEBUGLOG_0(NULL, S)
#define	DEBUGOUT1(S, A)		IGBVF_DEBUGLOG_1(NULL, S, A)
#define	DEBUGOUT2(S, A, B)	IGBVF_DEBUGLOG_2(NULL, S, A, B)
#define	DEBUGOUT3(S, A, B, C)	IGBVF_DEBUGLOG_3(NULL, S, A, B, C)
#else
#define	DEBUGOUT(S)
#define	DEBUGOUT1(S, A)
#define	DEBUGOUT2(S, A, B)
#define	DEBUGOUT3(S, A, B, C)
#endif	/* IGBVF_DEBUG */

#define	DEBUGFUNC(f)

#define	OS_DEP(hw)		((struct igbvf_osdep *)((hw)->back))

#define	false			B_FALSE
#define	true			B_TRUE

#define	CMD_MEM_WRT_INVALIDATE	0x0010	/* BIT_4 */
#define	PCI_COMMAND_REGISTER	0x04
#define	PCI_EX_CONF_CAP		0xE0


/*
 * Constants used in setting flow control thresholds
 */
#define	E1000_PBA_MASK		0xffff
#define	E1000_PBA_SHIFT		10
#define	E1000_FC_HIGH_DIFF	0x1638 /* High: 5688 bytes below Rx FIFO size */
#define	E1000_FC_LOW_DIFF	0x1640 /* Low: 5696 bytes below Rx FIFO size */
#define	E1000_FC_PAUSE_TIME	0x0680 /* 858 usec */

/* PHY Extended Status Register */
#define	IEEE_ESR_1000T_HD_CAPS	0x1000	/* 1000T HD capable */
#define	IEEE_ESR_1000T_FD_CAPS	0x2000	/* 1000T FD capable */
#define	IEEE_ESR_1000X_HD_CAPS	0x4000	/* 1000X HD capable */
#define	IEEE_ESR_1000X_FD_CAPS	0x8000	/* 1000X FD capable */

#define	E1000_WRITE_FLUSH(a)	(void) E1000_READ_REG(a, E1000_STATUS)

#ifdef IGBVF_DEBUG

#define	E1000_WRITE_REG(hw, reg, value)	\
	igbvf_write_reg(hw, reg, value)
#define	E1000_READ_REG(hw, reg)	\
	igbvf_read_reg(hw, reg)
#define	E1000_WRITE_REG_ARRAY(hw, reg, offset, value)	\
	igbvf_write_reg_array(hw, reg, offset, value)
#define	E1000_READ_REG_ARRAY(hw, reg, offset)	\
	igbvf_read_reg_array(hw, reg, offset)

#else	/* IGBVF_DEBUG */

#define	E1000_WRITE_REG(hw, reg, value)	\
	ddi_put32((OS_DEP(hw))->reg_handle, \
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg), (value))
#define	E1000_READ_REG(hw, reg)	\
	ddi_get32((OS_DEP(hw))->reg_handle, \
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg))
#define	E1000_WRITE_REG_ARRAY(hw, reg, offset, value)	\
	ddi_put32((OS_DEP(hw))->reg_handle, \
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg + ((offset) << 2)), \
	    (value))
#define	E1000_READ_REG_ARRAY(hw, reg, offset)	\
	ddi_get32((OS_DEP(hw))->reg_handle, \
	    (uint32_t *)((uintptr_t)(hw)->hw_addr + reg + ((offset) << 2)))

#endif	/* IGBVF_DEBUG */

#define	E1000_WRITE_REG_ARRAY_DWORD(a, reg, offset, value)	\
	E1000_WRITE_REG_ARRAY(a, reg, offset, value)
#define	E1000_READ_REG_ARRAY_DWORD(a, reg, offset)		\
	E1000_READ_REG_ARRAY(a, reg, offset)

#define	UNREFERENCED_1PARAMETER(_p)		_NOTE(ARGUNUSED(_p))
#define	UNREFERENCED_2PARAMETER(_p, _q)		_NOTE(ARGUNUSED(_p, _q))
#define	UNREFERENCED_3PARAMETER(_p, _q, _r)	_NOTE(ARGUNUSED(_p, _q, _r))
#define	UNREFERENCED_4PARAMETER(_p, _q, _r, _s)	_NOTE(ARGUNUSED(_p, _q, _r, _s))
#define	UNREFERENCED_5PARAMETER(_p, _q, _r, _s, _t)	\
	_NOTE(ARGUNUSED(_p, _q, _r, _s, _t))

#define	__le16		u16
#define	__le32		u32
#define	__le64		u64

typedef	int8_t		s8;
typedef	int16_t		s16;
typedef	int32_t		s32;
typedef	int64_t		s64;
typedef uint8_t		u8;
typedef	uint16_t 	u16;
typedef	uint32_t	u32;
typedef	uint64_t	u64;
typedef	boolean_t	bool;

struct igbvf_osdep {
	ddi_acc_handle_t reg_handle;
	ddi_acc_handle_t cfg_handle;
	struct igbvf *igbvf;
};


#ifdef __cplusplus
}
#endif

#endif	/* _IGBVF_OSDEP_H */

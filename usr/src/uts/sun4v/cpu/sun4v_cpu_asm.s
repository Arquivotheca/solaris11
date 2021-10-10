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

#if !defined(lint)
#include "assym.h"
#endif

/*
 * Generic processor assembly routines
 */

#include <sys/asm_linkage.h>
#include <sys/machparam.h>
#include <sys/machasi.h>
#include <sys/niagaraasi.h>
#include <vm/hat_sfmmu.h>

#if defined (lint)
/*
 * Invalidate all of the entries within the TSB, by setting the inv bit
 * in the tte_tag field of each tsbe.
 *
 * We take advantage of the fact that the TSBs are page aligned and a
 * multiple of PAGESIZE to use ASI_BLK_INIT_xxx ASI.
 *
 * See TSB_LOCK_ENTRY and the miss handlers for how this works in practice
 * (in short, we set all bits in the upper word of the tag, and we give the
 * invalid bit precedence over other tag bits in both places).
 */
/*ARGSUSED*/
void
cpu_inv_tsb(caddr_t tsb_base, uint_t tsb_bytes)
{}

#else /* lint */

	ENTRY(cpu_inv_tsb)

	/*
	 * The following code assumes that the tsb_base (%o0) is 256 bytes
	 * aligned and the tsb_bytes count is multiple of 256 bytes.
	 */

	wr	%g0, ASI_BLK_INIT_ST_QUAD_LDD_P, %asi
	set	TSBTAG_INVALID, %o2
	sllx	%o2, 32, %o2		! INV bit in upper 32 bits of the tag
1:
	stxa	%o2, [%o0+0x0]%asi
	stxa	%o2, [%o0+0x40]%asi
	stxa	%o2, [%o0+0x80]%asi
	stxa	%o2, [%o0+0xc0]%asi

	stxa	%o2, [%o0+0x10]%asi
	stxa	%o2, [%o0+0x20]%asi
	stxa	%o2, [%o0+0x30]%asi

	stxa	%o2, [%o0+0x50]%asi
	stxa	%o2, [%o0+0x60]%asi
	stxa	%o2, [%o0+0x70]%asi

	stxa	%o2, [%o0+0x90]%asi
	stxa	%o2, [%o0+0xa0]%asi
	stxa	%o2, [%o0+0xb0]%asi

	stxa	%o2, [%o0+0xd0]%asi
	stxa	%o2, [%o0+0xe0]%asi
	stxa	%o2, [%o0+0xf0]%asi

	subcc	%o1, 0x100, %o1
	bgu,pt	%ncc, 1b
	add	%o0, 0x100, %o0

	membar	#Sync
	retl
	nop

	SET_SIZE(cpu_inv_tsb)
#endif /* lint */

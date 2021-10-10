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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Hypervisor calls called by ds_pri driver.
 */

#include <sys/asm_linkage.h>
#include <sys/hypervisor_api.h>

#if defined(lint) || defined(__lint)

/*ARGSUSED*/
uint64_t
hv_mach_pri(uint64_t buffer_ra, uint64_t *buffer_sizep)
{ return (0); }

#else	/* lint || __lint */

	/*
	 * MACH_PRI
	 * arg0 buffer real address
	 * arg1 pointer to uint64_t for size of buffer
	 * ret0 status
	 * ret1 return required size of buffer / returned data size
	 */
	ENTRY(hv_mach_pri)
	mov	%o1, %o4		! save datap
	ldx	[%o1], %o1
	mov	HV_MACH_PRI, %o5
	ta	FAST_TRAP
	retl
	stx	%o1, [%o4]
	SET_SIZE(hv_mach_pri)

#endif	/* lint || __lint */

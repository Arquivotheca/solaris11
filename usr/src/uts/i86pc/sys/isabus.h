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

#ifndef _SYS_ISABUS_H
#define	_SYS_ISABUS_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * definition of isabus reg spec entry:
 */
typedef struct {
	uint32_t phys_hi;	/* bus type */
	uint32_t phys_lo;	/* 32-bit address */
	uint32_t size;		/* 32-bit length */
} isa_regspec_t;

struct isa_parent_private_data {
	int par_nreg;			/* number of regs */
	isa_regspec_t *par_reg;		/* array of regs */
	int par_nintr;			/* number of interrupts */
	struct intrspec *par_intr;	/* array of possible interrupts */
	int par_nrng;			/* number of ranges */
	struct rangespec *par_rng;	/* array of ranges */
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ISABUS_H */

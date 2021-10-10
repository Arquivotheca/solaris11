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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "base_conversion.h"

/* The following should be coded as inline expansion templates.	 */

/*
 * Multiplies two normal or subnormal doubles, returns result and exceptions.
 */
double
__mul_set(double x, double y, int *pe) {
	extern void _putsw(), _getsw();
	int sw;
	double z;

	_putsw(0);
	z = x * y;
	_getsw(&sw);
	if ((sw & 0x3f) == 0) {
		*pe = 0;
	} else {
		/* Result may not be exact. */
		*pe = 1;
	}
	return (z);
}

/*
 * Divides two normal or subnormal doubles x/y, returns result and exceptions.
 */
double
__div_set(double x, double y, int *pe) {
	extern void _putsw(), _getsw();
	int sw;
	double z;

	_putsw(0);
	z = x / y;
	_getsw(&sw);
	if ((sw & 0x3f) == 0) {
		*pe = 0;
	} else {
		*pe = 1;
	}
	return (z);
}

double
__dabs(double *d)
{
	/* should use hardware fabs instruction */
	return ((*d < 0.0) ? -*d : *d);
}

/*
 * Returns IEEE mode/status and
 * sets up standard environment for base conversion.
 */
void
__get_ieee_flags(__ieee_flags_type *b) {
	extern void _getcw(), _getsw(), _putcw();
	int cw;

	_getcw(&cw);
	b->mode = cw;
	_getsw(&b->status);
	/*
	 * set CW to...
	 * RC (bits 10:11)	0 == round to nearest even
	 * PC (bits 8:9)	2 == round to double
	 * EM (bits 0:5)	0x3f == all exception trapping masked off
	 */
	cw = (cw & ~0xf3f) | 0x23f;
	_putcw(cw);
}

/*
 * Restores previous IEEE mode/status
 */
void
__set_ieee_flags(__ieee_flags_type *b) {
	extern void _putcw(), _putsw();

	_putcw(b->mode);
	_putsw(b->status);
}

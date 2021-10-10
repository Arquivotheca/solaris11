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
/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 *      Copyright (c) 1997, by Sun Microsystems, Inc.
 *      All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI" /* SVr4.0 1.2 */

/*LINTLIBRARY*/

#include <sys/types.h>
#include "utility.h"

int
set_field_status(FIELD *f, int status)
{
	f = Field(f);

	if (status)
		Set(f, USR_CHG);
	else
		Clr(f, USR_CHG);

	return (E_OK);
}

int
field_status(FIELD *f)
{
/*
 * field_status may not be accurate on the current field unless
 * called from within the check validation function or the
 * form/field init/term functions.
 * field_status is always accurate on validated fields.
 */
	if (Status(Field(f), USR_CHG))
		return (TRUE);
	else
		return (FALSE);
}

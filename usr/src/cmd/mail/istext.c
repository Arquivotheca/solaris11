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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#pragma ident	"%Z%%M%	%I%	%E% SMI" 

#include "mail.h"

/*
 * istext(line, size) - check for text characters
 */
int
istext(unsigned char *s, int size)
{
	unsigned char *ep;
	int c;
	
	for (ep = s+size; --ep >= s; ) {
		c = *ep;
		if ((!isprint(c)) && (!isspace(c)) &&
		    /* Since backspace is not included in either of the */
		    /* above, must do separately                        */
		    /* Bell character is allowable control char in the text */
		    (c != 010) && (c != 007)) {
			return(FALSE);
		}
	}
	return(TRUE);
}

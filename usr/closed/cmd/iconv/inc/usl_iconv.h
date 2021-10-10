/*
 * Copyright (c) 1997, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"%Z%%M%	%I%	%E% SMI"



#define	KBDBUFSIZE	512
#define	KBDREADBUF	(KBDBUFSIZE / 8)

#include "kbd.h"
#include "symtab.h"

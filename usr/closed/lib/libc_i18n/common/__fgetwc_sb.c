/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T		*/
/*	  All Rights Reserved  					*/
/*								*/

#include "lint.h"
#include "file64.h"
#include "mtlib.h"
#include <stdio.h>
#include <sys/localedef.h>
#include "libc.h"
#include "stdiom.h"

/* ARGSUSED */
wint_t
__fgetwc_sb(_LC_charmap_t *hdl, FILE *iop)
{
	return ((wint_t)((--iop->_cnt < 0) ? __filbuf(iop) : *iop->_ptr++));
}

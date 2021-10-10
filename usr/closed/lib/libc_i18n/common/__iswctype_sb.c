/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */
/*
 * OSF/1 1.2
 */
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: iswctype.c,v $ "
 * "$Revision: 1.1.2.2 $ (OSF) $Date: 1992/03/17 03:50:48 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (LIBCCHR) LIBC Character Classification Funcions
 *
 * FUNCTIONS: __iswctype_sb
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 *  1.2  com/lib/c/chr/iswctype.c, libcchr, bos320, 9130320 7/17/91 15:16:26
 */

#include "lint.h"
#include <sys/localedef.h>

int
__iswctype_sb(_LC_ctype_t *hdl, wint_t wc, wctype_t mask)
{
	/*
	 * If wc is outside of the bounds, or if the mask is -1, then return 0.
	 * As min_wc is always 0, the check for the lower boundary can be
	 * omitted.  WEOF should be considered to be larger than max_wc.
	 */
	if ((uint32_t)wc > hdl->max_wc || mask == -1)
		return (0);

	return (hdl->mask[(uint32_t)wc] & mask);
}

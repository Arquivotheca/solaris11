/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * regerror: map error number to text string
 *
 * Copyright 1985, 1992 by Mortice Kern Systems Inc.  All rights reserved.
 *
 */
/*
 * static char rcsID[] = "$Header: /u/rd/src/libc/regex/rcs/regerror.c "
 * "1.28 1994/11/07 14:40:06 jeffhe Exp $";
 */
/*
 * static char sccsid[] = "@(#)43	1.2.1.1  "
 * "src/bos/usr/ccs/lib/libc/__regerror_std.c, bos, bos410 5/25/92 14:04:19";
 */
/*
 * COMPONENT_NAME: (LIBCPAT) Standard C Library Pattern Functions
 *
 * FUNCTIONS: regerror
 *
 * ORIGINS: 27
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991, 1992
 * All Rights Reserved
 * Licensed Materials - Property of IBM
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#include "lint.h"
#include <nl_types.h>
#include <sys/localedef.h>
#include <regex.h>
#include <string.h>
#include "_libc_gettext.h"

/*
 * Error messages for regerror
 */
#define	_MSG(m)	(m)
#include "regerror_msg.h"

/*
 * Map error number to text message.
 * preg is supplied to allow an error message with perhaps pieces of
 * the offending regular expression embeded in it.
 * preg is permitted to be zero, results still have to be returned.
 * In this implementation, preg is currently unused.
 * The string is returned into errbuf, which is errbuf_size bytes
 * long, and is possibly truncated.  If errbuf_size is zero, the string
 * is not returned.  The length of the error message is returned.
 */

/* ARGSUSED */
size_t
__regerror_std(_LC_collate_t *hdl, int errcode, const regex_t *preg,
    char *errbuf, size_t errbuf_size)
{
	char	*s;
	size_t	len;

#define	_Last_Error_Code	REG_EEOL

	if (errcode < REG_OK || errcode > _Last_Error_Code) {
		s = _libc_gettext("unknown regex error");
	} else {
		s = _libc_gettext(regerrors[errcode]);
	}

	len = strlcpy(errbuf, s, errbuf_size);
	return (len + 1);
}

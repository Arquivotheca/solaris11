/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

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
 * COMPONENT_NAME: (LIBCCHR) LIBC Character Classification Funcions
 *
 * FUNCTIONS: iswalnum
 * FUNCTIONS: iswalpha
 * FUNCTIONS: iswblank
 * FUNCTIONS: iswcntrl
 * FUNCTIONS: iswdigit
 * FUNCTIONS: iswgraph
 * FUNCTIONS: iswlower
 * FUNCTIONS: iswprint
 * FUNCTIONS: iswpunct
 * FUNCTIONS: iswspace
 * FUNCTIONS: iswupper
 * FUNCTIONS: iswxdigit
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/lib/c/chr/iswalnum.c, libcchr, bos320, 9132320 7/23/91 18:18:39
 * 1.2  com/lib/c/chr/iswalpha.c, libcchr, bos320, 9132320 7/23/91 18:18:50
 * 1.2  com/lib/c/chr/iswcntrl.c, libcchr, bos320, 9132320 7/23/91 18:19:03
 * 1.2  com/lib/c/chr/iswdigit.c, libcchr, bos320, 9132320 7/23/91 18:19:15
 * 1.2  com/lib/c/chr/iswgraph.c, libcchr, bos320, 9132320 7/23/91 18:19:26
 * 1.2  com/lib/c/chr/iswlower.c, libcchr, bos320, 9132320 7/23/91 18:19:34
 * 1.2  com/lib/c/chr/iswprint.c, libcchr, bos320, 9132320 7/23/91 18:19:44
 * 1.2  com/lib/c/chr/iswpunct.c, libcchr, bos320, 9132320 7/23/91 18:19:55
 * 1.2  com/lib/c/chr/iswspace.c, libcchr, bos320, 9132320 7/23/91 18:20:06
 * 1.2  com/lib/c/chr/iswupper.c, libcchr, bos320, 9132320 7/23/91 18:20:18
 * 1.2  com/lib/c/chr/iswxdigit.c, libcchr, bos320, 9132320 7/23/91 18:20:28
 */
/*
 *
 * FUNCTION: Determines if the process code, pc, belongs to the specified class
 *
 *
 * PARAMETERS: pc  -- character to be classified
 *
 *
 * RETURN VALUES: 0 -- if pc is not part of the specified class
 *                >0 - If c is part of the specified class
 *
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak _iswalnum = iswalnum
#pragma weak _iswalpha = iswalpha
#pragma weak _iswcntrl = iswcntrl
#pragma weak _iswdigit = iswdigit
#pragma weak _iswgraph = iswgraph
#pragma weak _iswlower = iswlower
#pragma weak _iswprint = iswprint
#pragma weak _iswpunct = iswpunct
#pragma weak _iswspace = iswspace
#pragma weak _iswupper = iswupper
#pragma weak _iswxdigit = iswxdigit

#include "lint.h"
#include <ctype.h>
#include <wchar.h>
#include <sys/localedef.h>


#define	_ISWCTYPE(pc, mask)	\
	return (METHOD(__lc_ctype, iswctype)(__lc_ctype, (pc), (mask)))

int
iswalnum(wint_t pc)
{
	_ISWCTYPE(pc, _ISALNUM);
}

int
iswalpha(wint_t pc)
{
	_ISWCTYPE(pc, _ISALPHA);
}

/* no weak symbol for iswblank */
int
iswblank(wint_t pc)
{
	_ISWCTYPE(pc, _ISBLANK);
}

int
iswcntrl(wint_t pc)
{
	_ISWCTYPE(pc, _ISCNTRL);
}

int
iswdigit(wint_t pc)
{
	_ISWCTYPE(pc, _ISDIGIT);
}

int
iswgraph(wint_t pc)
{
	_ISWCTYPE(pc, _ISGRAPH);
}

int
iswlower(wint_t pc)
{
	_ISWCTYPE(pc, _ISLOWER);
}

int
iswprint(wint_t pc)
{
	_ISWCTYPE(pc, _ISPRINT);
}

int
iswpunct(wint_t pc)
{
	_ISWCTYPE(pc, _ISPUNCT);
}

int
iswspace(wint_t pc)
{
	_ISWCTYPE(pc, _ISSPACE);
}

int
iswupper(wint_t pc)
{
	_ISWCTYPE(pc, _ISUPPER);
}

int
iswxdigit(wint_t pc)
{
	_ISWCTYPE(pc, _ISXDIGIT);
}

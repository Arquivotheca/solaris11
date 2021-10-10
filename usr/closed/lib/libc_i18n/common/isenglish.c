/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * isxxxx(c) returns true if 'c' is classified xxxx
 */

#pragma weak _isenglish = isenglish
#pragma weak _isideogram = isideogram
#pragma weak _isnumber = isnumber
#pragma weak _isphonogram = isphonogram
#pragma weak _isspecial = isspecial

#include "lint.h"
#include <ctype.h>
#include <wchar.h>
#include <sys/localedef.h>
#include <wctype.h>
#include <note.h>

#define	_ISEXTRACTYPE(pc, mask)	\
	{ \
		if ((uint32_t)(pc) > 0x9f) \
			return (METHOD(__lc_ctype, \
			    iswctype)(__lc_ctype, (pc), (mask))); \
		else \
			return (0); \
		NOTE(NOTREACHED)\
	}

int
isenglish(wint_t pc)
{
	_ISEXTRACTYPE(pc, _E3);
}

int
isideogram(wint_t pc)
{
	_ISEXTRACTYPE(pc, _E2);
}

int
isnumber(wint_t pc)
{
	_ISEXTRACTYPE(pc, _E4);
}

int
isphonogram(wint_t pc)
{
	_ISEXTRACTYPE(pc, _E1);
}

int
isspecial(wint_t pc)
{
	_ISEXTRACTYPE(pc, _E5);
}

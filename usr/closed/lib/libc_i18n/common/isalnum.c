/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */


/*	Copyright (c) 1988 AT&T	*/
/*	All Rights Reserved	*/


#include "lint.h"
#include <ctype.h>
#include <sys/localedef.h>
#include <sys/types.h>
#include <note.h>

#ifdef isalnum
#undef isalnum
#endif

#ifdef isalpha
#undef isalpha
#endif

#ifdef isblank
#undef isblank
#endif

#ifdef iscntrl
#undef iscntrl
#endif

#ifdef isdigit
#undef isdigit
#endif

#ifdef isgraph
#undef isgraph
#endif

#ifdef islower
#undef islower
#endif

#ifdef isprint
#undef isprint
#endif

#ifdef ispunct
#undef ispunct
#endif

#ifdef isspace
#undef isspace
#endif

#ifdef isupper
#undef isupper
#endif

#ifdef isxdigit
#undef isxdigit
#endif

#define	_ISCTYPE(_c, _msk)	\
	{ \
		if ((uint32_t)(_c) < 256U) \
			return (__ctype_mask[_c] & (_msk)); \
		else \
			return (0); \
		NOTE(NOTREACHED)\
	}


int
isalnum(int c)
{
	_ISCTYPE(c, _ISALNUM);
}

int
isalpha(int c)
{
	_ISCTYPE(c, _ISALPHA);
}

int
isblank(int c)
{
	_ISCTYPE(c, _ISBLANK);
}

int
iscntrl(int c)
{
	_ISCTYPE(c, _ISCNTRL);
}

int
isdigit(int c)
{
	_ISCTYPE(c, _ISDIGIT);
}

int
isgraph(int c)
{
	_ISCTYPE(c, _ISGRAPH);
}

int
islower(int c)
{
	_ISCTYPE(c, _ISLOWER);
}

int
isprint(int c)
{
	_ISCTYPE(c, _ISPRINT);
}

int
ispunct(int c)
{
	_ISCTYPE(c, _ISPUNCT);
}

int
isspace(int c)
{
	_ISCTYPE(c, _ISSPACE);
}

int
isupper(int c)
{
	_ISCTYPE(c, _ISUPPER);
}

int
isxdigit(int c)
{
	_ISCTYPE(c, _ISXDIGIT);
}

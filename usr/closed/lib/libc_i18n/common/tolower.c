/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */


/*
 * Copyright (c) 1984 AT&T
 * All Rights Reserved
 *
 *
 * _tolower.c	1.4  com/lib/c/gen,3.1,8943 9/7/89 17:34:21
 * _toupper.c	1.4  com/lib/c/gen,3.1,8943 9/7/89 17:34:35
 */

#include "lint.h"
#include <ctype.h>
#include <sys/localedef.h>
#include <sys/types.h>
#include <note.h>

#ifdef	tolower
#undef	tolower
#endif

#ifdef	toupper
#undef	toupper
#endif

#ifdef	_tolower
#undef	_tolower
#endif

#ifdef	_toupper
#undef	_toupper
#endif

#define	_TOTRANS(_c_, _transclass_)	\
	{ \
		if ((uint32_t)(_c_) < 256U) \
			return (__trans_##_transclass_[_c_]); \
		else \
			return (_c_); \
		NOTE(NOTREACHED)\
	}

int
tolower(int c)
{
	_TOTRANS(c, lower);
}

int
toupper(int c)
{
	_TOTRANS(c, upper);
}

int
_tolower(int c)
{
	_TOTRANS(c, lower);
}

int
_toupper(int c)
{
	_TOTRANS(c, upper);
}

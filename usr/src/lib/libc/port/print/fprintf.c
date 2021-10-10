/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * This symbol should not be defined, but there are some
 * miserable old compiler libraries that depend on it.
 */
#pragma weak _fprintf = fprintf

#include "lint.h"
#include <mtlib.h>
#include <thread.h>
#include <synch.h>
#include <stdarg.h>
#include <values.h>
#include <errno.h>
#include "print.h"
#include <sys/types.h>
#include "libc.h"
#include "mse.h"

/*VARARGS2*/
int
fprintf(FILE *iop, const char *format, ...)
{
	ssize_t count;
	rmutex_t *lk;
	va_list ap;

	va_start(ap, format);

	/* Use F*LOCKFILE() macros because fprintf() is not async-safe. */
	FLOCKFILE(lk, iop);

	_SET_ORIENTATION_BYTE(iop);

	if (!(iop->_flag & _IOWRT)) {
		/* if no write flag */
		if (iop->_flag & _IORW) {
			/* if ok, cause read-write */
			_READ2WRT(iop);
		} else {
			/* else error */
			FUNLOCKFILE(lk);
			errno = EBADF;
			return (EOF);
		}
	}
	count = _ndoprnt(format, ap, iop, 0);

	/* return EOF on error or EOF */
	if (FERROR(iop) || count == EOF) {
		FUNLOCKFILE(lk);
		return (EOF);
	}

	FUNLOCKFILE(lk);

	/* error on overflow */
	if ((size_t)count > MAXINT) {
		errno = EOVERFLOW;
		return (EOF);
	}
	return ((int)count);
}

#ifndef _LP64

/*
 * 32-bit shadow function of fprintf().
 * When using the c89 compiler to build 32-bit applications, the size
 * of intmax_t is 32-bits, otherwise the size of intmax_t is 64-bits.
 * The shadow function uses 32-bit size of intmax_t for %j conversion.
 * The #pragma redefine_extname in <stdio.h> selects the proper routine
 * at compile time for the user application.
 * NOTE: this function only exists in the 32-bit library.
 */

int
_fprintf_c89(FILE *iop, const char *format, ...)
{
	ssize_t count;
	va_list ap;

	va_start(ap, format);
	count = _vfprintf_c89(iop, format, ap);
	va_end(ap);
	return ((int)count);
}

#endif  /* _LP64 */

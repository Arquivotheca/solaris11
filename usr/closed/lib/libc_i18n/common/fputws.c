/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * fputws transforms the process code string pointed to by "ptr"
 * into a byte string, and writes the string to the named
 * output "iop".
 *
 * Use an intermediate buffer to transform a string from wchar_t to
 * multibyte char.  In order to not overflow the intermediate buffer,
 * impose a limit on the length of string to output to PC_MAX process
 * codes.  If the input string exceeds PC_MAX process codes, process
 * the string in a series of smaller buffers.
 */

#include "lint.h"
#include "file64.h"
#include "mse_int.h"
#include <stdio.h>
#include <sys/localedef.h>
#include <stdlib.h>
#include <limits.h>
#include <widec.h>
#include <macros.h>
#include <errno.h>
#include "libc.h"
#include "stdiom.h"
#include "mse.h"
#include "libc_i18n.h"

#define	PC_MAX 		256
#define	MBBUFLEN	(PC_MAX * MB_LEN_MAX)

#undef fputws

int
fputws(const wchar_t *ptr, FILE *iop)
{
	int pcsize, ret;
	ssize_t pclen, pccnt;
	int nbytes, i;
	char mbbuf[MBBUFLEN], *mp;

	/* number of process codes in ptr */
	pclen = pccnt = wcslen(ptr);

	while (pclen > 0) {
		pcsize = (int)min(pclen, PC_MAX - 1);
		nbytes = 0;
		for (i = 0, mp = mbbuf; i < pcsize; i++, mp += ret) {
			if ((ret = METHOD(__lc_charmap, wctomb)
			    (__lc_charmap, mp, *ptr++)) == -1)
				return (EOF);
			nbytes += ret;
		}
		*mp = '\0';
		if (fputs(mbbuf, iop) != nbytes)
			return (EOF);
		pclen -= pcsize;
	}
	if (pccnt <= INT_MAX)
		return ((int)pccnt);
	else
		return (EOF);
}

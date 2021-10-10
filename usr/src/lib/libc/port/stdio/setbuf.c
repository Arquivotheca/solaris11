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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "file64.h"
#include "mtlib.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread.h>
#include <synch.h>
#include "stdiom.h"


void
setbuf(FILE *iop, char *abuf)
{
	Uchar *buf = (Uchar *)abuf;
	int fno = GET_FD(iop);  /* file number */
	int size = BUFSIZ - _SMBFSZ;
	Uchar *temp;
	rmutex_t *lk;

	FLOCKFILE(lk, iop);
	if ((iop->_base != 0) && (iop->_flag & _IOMYBUF))
		free((char *)iop->_base - PUSHBACK);
	iop->_flag &= ~(_IOMYBUF | _IONBF | _IOLBF);
	if (buf == 0) {
		iop->_flag |= _IONBF;
#ifndef _STDIO_ALLOCATE
		if (fno < 2) {
			/* use special buffer for std{in,out} */
			buf = (fno == 0) ? _sibuf : _sobuf;
		} else	/* needed for ifndef */
#endif
		if (fno < _NFILE) {
			buf = _smbuf[fno];
			size = _SMBFSZ - PUSHBACK;
		} else
		if ((buf = (Uchar *)malloc(_SMBFSZ * sizeof (Uchar))) != 0) {
			iop->_flag |= _IOMYBUF;
			size = _SMBFSZ - PUSHBACK;
		}
	} else {	/* regular buffered I/O, standard buffer size */
		if (isatty(fno))
			iop->_flag |= _IOLBF;
	}
	if (buf == 0) {
		FUNLOCKFILE(lk);
		return;		/* malloc() failed */
	}
	temp = buf + PUSHBACK;
	iop->_base = temp;
	_setbufend(iop, temp + size);
	iop->_ptr = temp;
	iop->_cnt = 0;
	FUNLOCKFILE(lk);
}

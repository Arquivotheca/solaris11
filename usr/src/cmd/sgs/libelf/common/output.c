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
 * Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 */

#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libelf.h>
#include <errno.h>
#include "decl.h"
#include "msg.h"

/*
 * File output
 *	These functions write output files.
 *	On SVR4 and newer systems use mmap(2).  On older systems (or on
 *	file systems that don't support mmap), use write(2).
 */
char *
_elf_outmap(Elf *elf, size_t sz)
{
	_elf_imgcalloc_func_t	*imgcalloc_func;
	char			*p;

	/*
	 * Note: Some NFS implementations do not provide from enlarging a file
	 * via ftruncate(), thus this may fail with ENOSUP.  In this case the
	 * fall through to the calloc() mechanism will occur.
	 */
	if (((elf->ed_myflags & EDF_WRALLOC) == 0) &&
	    (ftruncate(elf->ed_fd, (off_t)sz) == 0) &&
	    (p = mmap(0, sz, PROT_READ + PROT_WRITE,
	    MAP_SHARED, elf->ed_fd, (off_t)0)) != MAP_FAILED)
		return (p);

	/*
	 * If mmap fails, or allocation was specifically asked for, use any
	 * caller supplied allocation routine, or fall back to calloc().
	 * Note, calloc() is used rather than malloc(), as ld(1) assumes that
	 * the backing storage is zero filled.
	 */
	ELFACCESSDATA(imgcalloc_func, _elf_imgcalloc_func)
	if (imgcalloc_func)
		p = (* imgcalloc_func)(1, sz);
	else
		p = calloc(1, sz);

	if (p == NULL)
		_elf_seterr(EMEM_OUT, errno);
	else
		elf->ed_myflags |= EDF_IMALLOC;

	return (p);
}

size_t
_elf_outsync(Elf *elf, char *p, size_t sz)
{
	int	err = 0;
	Msg	msg;

	if (elf->ed_myflags & EDF_IMALLOC) {
		/*
		 * If this is an allocated memory image, write the image out
		 * and free the memory.
		 */
		if ((lseek(elf->ed_fd, 0L, SEEK_SET) != 0) ||
		    (write(elf->ed_fd, p, sz) != sz)) {
			err = errno;
			msg = EIO_WRITE;	/* MSG_INTL(EIO_WRITE) */
		}

		free(p);
		elf->ed_myflags &= ~EDF_IMALLOC;
	} else {
		/*
		 * If this is a mapped memory image, sync the image out and
		 * unmap the memory.
		 */
		if (msync(p, sz, MS_ASYNC) == -1) {
			err = errno;
			msg = EIO_SYNC;		/* MSG_INTL(EIO_SYNC) */
		}

		(void) munmap(p, sz);
	}

	if (err) {
		_elf_seterr(msg, err);
		return (0);
	}
	return (sz);
}

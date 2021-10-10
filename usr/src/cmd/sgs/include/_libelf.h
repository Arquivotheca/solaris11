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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	__LIBELF_H
#define	__LIBELF_H

/*
 * Version of libelf.h that supplies definitions for APIs that are private
 * to the link-editors and related tools.  Includes the standard libelf.h
 * and gelf.h and then supplements them with the private additions.
 */

#include <libelf.h>
#include <gelf.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef void		_elf_execfill_func_t(void *, off_t, size_t);
typedef void		*_elf_imgcalloc_func_t(size_t, size_t);

extern void		_elf_execfill(_elf_execfill_func_t *);
extern off_t		_elf_getarhdrbase(Elf *);
extern size_t		_elf_getarsymwordsize(Elf *);
extern size_t		_elf_getnextoff(Elf *);
extern Elf64_Off	_elf_getxoff(Elf_Data *);
extern void		_elf_imgcalloc(_elf_imgcalloc_func_t *);
extern ulong_t		_elf_number(char *, char *, int);
extern int		_elf_swap_wrimage(Elf *);
extern uint_t		_elf_sys_encoding(void);

extern GElf_Xword	_gelf_getdyndtflags_1(Elf *);

#ifdef	__cplusplus
}
#endif

#endif	/* __LIBELF_H */

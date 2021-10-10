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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 */

/*
 * i386 specific setup routine  -  relocate ld.so's symbols, setup its
 * environment, map in loadable sections of the executable.
 *
 * Takes base address ld.so was loaded at, address of ld.so's dynamic
 * structure, address of process environment pointers, address of auxiliary
 * vector and * argv[0] (process name).
 * If errors occur, send process signal - otherwise
 * return executable's entry point to the bootstrap routine.
 */

#include	<signal.h>
#include	<stdlib.h>
#include	<sys/auxv.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<link.h>
#include	<dlfcn.h>
#include	"_rtld.h"
#include	"_audit.h"
#include	"msg.h"

/* VARARGS */
unsigned long
_setup(Boot *ebp, Dyn *ld_dyn)
{
	ulong_t		reladdr, relcount, ld_base = 0;
	ulong_t		relent = 0;
	ulong_t		strtab, soname, interp_base = 0;
	char		*_rt_name, **_envp, **_argv;
	int		_syspagsz = 0, fd = -1;
	uint_t		_flags = 0, hwcap_1 = 0;
	Dyn		*dyn_ptr;
	Phdr		*phdr = NULL;
	Rt_map		*lmp;
	auxv_t		*auxv, *_auxv;
	char		*_platform = NULL, *_execname = NULL, *_emulator = NULL;
	int		auxflags = -1;

	/*
	 * Scan the bootstrap structure to pick up the basics.
	 */
	for (; ebp->eb_tag != EB_NULL; ebp++)
		switch (ebp->eb_tag) {
		case EB_LDSO_BASE:
			ld_base = (unsigned long)ebp->eb_un.eb_val;
			break;
		case EB_ARGV:
			_argv = (char **)ebp->eb_un.eb_ptr;
			break;
		case EB_ENVP:
			_envp = (char **)ebp->eb_un.eb_ptr;
			break;
		case EB_AUXV:
			_auxv = (auxv_t *)ebp->eb_un.eb_ptr;
			break;
		case EB_PAGESIZE:
			_syspagsz = (int)ebp->eb_un.eb_val;
			break;
		}

	/*
	 * Search the aux. vector for the information passed by exec.
	 */
	for (auxv = _auxv; auxv->a_type != AT_NULL; auxv++) {
		switch (auxv->a_type) {
		case AT_EXECFD:
			/* this is the old exec that passes a file descriptor */
			fd = (int)auxv->a_un.a_val;
			break;
		case AT_FLAGS:
			/* processor flags (MAU available, etc) */
			_flags = auxv->a_un.a_val;
			break;
		case AT_PAGESZ:
			/* system page size */
			_syspagsz = (int)auxv->a_un.a_val;
			break;
		case AT_PHDR:
			/* address of the segment table */
			phdr = (Phdr *)auxv->a_un.a_ptr;
			break;
		case AT_BASE:
			/* interpreter base address */
			if (ld_base == 0)
				ld_base = auxv->a_un.a_val;
			interp_base = auxv->a_un.a_val;
			break;
		case AT_SUN_PLATFORM:
			/* platform name */
			_platform = auxv->a_un.a_ptr;
			break;
		case AT_SUN_EXECNAME:
			/* full pathname of execed object */
			_execname = auxv->a_un.a_ptr;
			break;
		case AT_SUN_AUXFLAGS:
			/* auxiliary flags */
			auxflags = (int)auxv->a_un.a_val;
			break;
		case AT_SUN_HWCAP:
			/* hardware capabilities */
			hwcap_1 = (uint_t)auxv->a_un.a_val;
			break;
		case AT_SUN_EMULATOR:
			/* name of emulation library, if any */
			_emulator = auxv->a_un.a_ptr;
			break;
		}
	}

	/*
	 * Get needed info from ld.so's dynamic structure.
	 */
	/* LINTED */
	dyn_ptr = (Dyn *)((char *)ld_dyn + ld_base);
	for (ld_dyn = dyn_ptr; ld_dyn->d_tag != DT_NULL; ld_dyn++) {
		switch (ld_dyn->d_tag) {
		case DT_REL:
			reladdr = ld_dyn->d_un.d_ptr + ld_base;
			break;
		case DT_RELCOUNT:
			relcount = ld_dyn->d_un.d_val;
			break;
		case DT_RELENT:
			relent = ld_dyn->d_un.d_val;
			break;
		case DT_STRTAB:
			strtab = ld_dyn->d_un.d_ptr + ld_base;
			break;
		case DT_SONAME:
			soname = ld_dyn->d_un.d_val;
			break;
		}
	}
	_rt_name = (char *)strtab + soname;

	/*
	 * If we don't have a RELENT, just assume the size.
	 */
	if (relent == 0)
		relent = sizeof (Rel);

	/*
	 * As all global symbol references within ld.so.1 are protected
	 * (symbolic), only RELATIVE and JMPSLOT relocations should be left
	 * to process at runtime.  Process all relative relocations now.
	 */
	for (; relcount; relcount--) {
		ulong_t	roffset;

		roffset = ((Rel *)reladdr)->r_offset + ld_base;
		*((ulong_t *)roffset) += ld_base;
		reladdr += relent;
	}

	/*
	 * If an emulation library is being used, use that as the linker's
	 * effective executable name. The real executable is not linked by this
	 * linker.
	 */
	if (_emulator != NULL) {
		_execname = _emulator;
		rtld_flags2 |= RT_FL2_BRANDED;
	}

	/*
	 * Initialize the dyn_plt_ent_size field.  It currently contains the
	 * size of the dyn_plt_template.  It still needs to be aligned and have
	 * space for the 'dyn_data' area added.
	 */
	dyn_plt_ent_size = ROUND(dyn_plt_ent_size, M_WORD_ALIGN) +
	    sizeof (uintptr_t) + sizeof (uintptr_t) + sizeof (ulong_t) +
	    sizeof (ulong_t) + sizeof (Sym);

	/*
	 * Continue with generic startup processing.
	 */
	if ((lmp = setup((char **)_envp, (auxv_t *)_auxv, _flags, _platform,
	    _syspagsz, _rt_name, ld_base, interp_base, fd, phdr,
	    _execname, _argv, NULL, auxflags, hwcap_1)) == NULL) {
		rtldexit(&lml_main, 1);
	}

	return (LM_ENTRY_PT(lmp)());
}

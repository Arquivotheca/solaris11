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
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Global variables
 */
#include	<sys/elf.h>
#include	"msg.h"
#include	"_libld.h"

Ld_heap		*ld_heap;	/* list of allocated blocks for */
				/* 	link-edit dynamic allocations */
APlist		*lib_support;	/* list of support libraries specified */
				/*	(-S option) */
int		demangle_flag;	/* symbol demangling required */

/*
 * Paths and directories for library searches.  These are used to set up
 * linked lists of directories which are maintained in the ofl structure.
 */
char		*Plibpath;	/* User specified -YP or defaults to LIBPATH */
char		*Llibdir;	/* User specified -YL */
char		*Ulibdir;	/* User specified -YU */

/*
 * A default library search path is used if one was not supplied on the command
 * line.  Note: these strings can not use MSG_ORIG() since they are modified as
 * part of the path processing.
 */
char		def64_Plibpath[] = "/lib/64:/usr/lib/64";
char		def32_Plibpath[] = "/lib:/usr/lib";

/*
 * Rejected file error messages (indexed to match SGS_REJ_ values).
 */
const Msg
reject[SGS_REJ_NUM] = {
		MSG_STR_EMPTY,
		MSG_REJ_MACH,		/* MSG_INTL(MSG_REJ_MACH) */
		MSG_REJ_CLASS,		/* MSG_INTL(MSG_REJ_CLASS) */
		MSG_REJ_DATA,		/* MSG_INTL(MSG_REJ_DATA) */
		MSG_REJ_TYPE,		/* MSG_INTL(MSG_REJ_TYPE) */
		MSG_REJ_BADFLAG,	/* MSG_INTL(MSG_REJ_BADFLAG) */
		MSG_REJ_MISFLAG,	/* MSG_INTL(MSG_REJ_MISFLAG) */
		MSG_REJ_VERSION,	/* MSG_INTL(MSG_REJ_VERSION) */
		MSG_REJ_HAL,		/* MSG_INTL(MSG_REJ_HAL) */
		MSG_REJ_US3,		/* MSG_INTL(MSG_REJ_US3) */
		MSG_REJ_STR,		/* MSG_INTL(MSG_REJ_STR) */
		MSG_REJ_UNKFILE,	/* MSG_INTL(MSG_REJ_UNKFILE) */
		MSG_REJ_UNKCAP,		/* MSG_INTL(MSG_REJ_UNKCAP) */
		MSG_REJ_HWCAP_1,	/* MSG_INTL(MSG_REJ_HWCAP_1) */
		MSG_REJ_SFCAP_1,	/* MSG_INTL(MSG_REJ_SFCAP_1) */
		MSG_REJ_MACHCAP,	/* MSG_INTL(MSG_REJ_MACHCAP) */
		MSG_REJ_PLATCAP,	/* MSG_INTL(MSG_REJ_PLATCAP) */
		MSG_REJ_HWCAP_2,	/* MSG_INTL(MSG_REJ_HWCAP_2) */
		MSG_REJ_ARCHIVE,	/* MSG_INTL(MSG_REJ_ARCHIVE) */
		MSG_REJ_STUB		/* MSG_INTL(MSG_REJ_STUB) */
	};
#if SGS_REJ_NUM != (SGS_REJ_STUB + 1)
#error SGS_REJ_NUM has changed
#endif

/*
 * Symbol types that we include in .SUNW_ldynsym sections
 * (indexed by STT_ values).
 */
const int
ldynsym_symtype[] = {
		0,			/* STT_NOTYPE (not counting 1st slot) */
		0,			/* STT_OBJECT */
		1,			/* STT_FUNC */
		0,			/* STT_SECTION */
		1,			/* STT_FILE */
		0,			/* STT_COMMON */
		0,			/* STT_TLS */
		0,			/* 7 */
		0,			/* 8 */
		0,			/* 9 */
		0,			/* 10 */
		0,			/* 11 */
		0,			/* 12 */
		0,			/* STT_SPARC_REGISTER */
		0,			/* 14 */
		0,			/* 15 */
};
#if STT_NUM != (STT_TLS + 1)
#error "STT_NUM has grown. Update ldynsym_symtype[]."
#endif

/*
 * Symbol types that we include in .SUNW_dynsymsort sections
 * (indexed by STT_ values).
 */
const int
dynsymsort_symtype[] = {
		0,			/* STT_NOTYPE */
		1,			/* STT_OBJECT */
		1,			/* STT_FUNC */
		0,			/* STT_SECTION */
		0,			/* STT_FILE */
		1,			/* STT_COMMON */
		0,			/* STT_TLS */
		0,			/* 7 */
		0,			/* 8 */
		0,			/* 9 */
		0,			/* 10 */
		0,			/* 11 */
		0,			/* 12 */
		0,			/* STT_SPARC_REGISTER */
		0,			/* 14 */
		0,			/* 15 */
};
#if STT_NUM != (STT_TLS + 1)
#error "STT_NUM has grown. Update dynsymsort_symtype[]."
#endif

/*
 * Symbol types that represent data for which stub objects require
 * ASSERT directives to be specified.
 */
const int
assert_data_symtype[] = {
		0,			/* STT_NOTYPE */
		1,			/* STT_OBJECT */
		0,			/* STT_FUNC */
		0,			/* STT_SECTION */
		0,			/* STT_FILE */
		1,			/* STT_COMMON */
		1,			/* STT_TLS */
		0,			/* 7 */
		0,			/* 8 */
		0,			/* 9 */
		0,			/* 10 */
		0,			/* 11 */
		0,			/* 12 */
		0,			/* STT_SPARC_REGISTER */
		0,			/* 14 */
		0,			/* 15 */
};
#if STT_NUM != (STT_TLS + 1)
#error "STT_NUM has grown. Update assert_data_symtype[]."
#endif

/*
 * -z strip-class token descriptor.
 */
const Strip_cls strip_cls_items[] = {
	{ MSG_ARG_STRIP_NONALLOC,	/* MSG_ORIG(MSG_ARG_STRIP_NONALLOC) */
	    FLG_SC_NONALLOC },
	{ MSG_ARG_STRIP_ANNOTATE,	/* MSG_ORIG(MSG_ARG_STRIP_ANNOTATE) */
	    FLG_SC_ANNOTATE },
	{ MSG_ARG_STRIP_COMMENT,	/* MSG_ORIG(MSG_ARG_STRIP_COMMENT) */
	    FLG_SC_COMMENT },
	{ MSG_ARG_STRIP_DEBUG,		/* MSG_ORIG(MSG_ARG_STRIP_DEBUG) */
	    FLG_SC_DEBUG },
	{ MSG_ARG_STRIP_EXCLUDE,	/* MSG_ORIG(MSG_ARG_STRIP_EXCLUDE) */
	    FLG_SC_EXCLUDE },
	{ MSG_ARG_STRIP_NOTE,		/* MSG_ORIG(MSG_ARG_STRIP_NOTE) */
	    FLG_SC_NOTE },
	{ MSG_ARG_STRIP_SYMBOL,		/* MSG_ORIG(MSG_ARG_STRIP_SYMBOL) */
	    FLG_SC_SYMBOL },
	{ 0, 0 }
};
#if SC_NUM != (SC_SYMBOL + 1)
#error "SC_NUM has grown. Update strip_cls_items[]."
#endif

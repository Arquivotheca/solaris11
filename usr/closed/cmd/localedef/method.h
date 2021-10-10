/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LOCALEDEF_METHOD_H
#define	_LOCALEDEF_METHOD_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */
/*
 * OSF/1 1.2
 */
/* @(#)$RCSfile: method.h,v $ $Revision: 1.1.2.8 $ */
/* (OSF) $Date: 1992/03/17 22:14:36 $ */
/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.1  com/inc/sys/method.h, libcloc, bos320, 9130320 7/17/91 17:25:11
 */

/* To make available the definition of _LP64 and _ILP32 */
#include <sys/types.h>

#include "localedef.h"

#define	MACH64	ISA64 "/"

#define	DEFAULT_METHOD_DIR	"/usr/lib/"
#define	DEFAULT_METHOD_NAM	"libc.so.1"

#define	DEFAULT_METHOD		DEFAULT_METHOD_DIR DEFAULT_METHOD_NAM
#define	DEFAULT_METHOD64	DEFAULT_METHOD_DIR MACH64 DEFAULT_METHOD_NAM

/*
 * charmap common
 */
#define	CHARMAP_CHARMAP_DESTRUCTOR	0x00
#define	CHARMAP_CHARMAP_INIT		0x01
#define	CHARMAP_EUCPCTOWC		0x02
#define	CHARMAP_MBLEN			0x03
#define	CHARMAP_MBSINIT			0x04
#define	CHARMAP_MBRLEN			0x05
#define	CHARMAP_NL_LANGINFO		0x06
#define	CHARMAP_WCTOEUCPC		0x07
#define	CHARMAP_COMMON_START		CHARMAP_CHARMAP_DESTRUCTOR
#define	CHARMAP_COMMON_END		CHARMAP_WCTOEUCPC

/* Reserved: 0x08 - 0x09 */

/*
 * ctype common
 */
#define	CTYPE_CTYPE_DESTRUCTOR		0x0a
#define	CTYPE_CTYPE_INIT		0x0b
#define	CTYPE_TRWCTYPE			0x0c
#define	CTYPE_WCTRANS			0x0d
#define	CTYPE_WCTYPE			0x0e
#define	CTYPE_COMMON_START		CTYPE_CTYPE_DESTRUCTOR
#define	CTYPE_COMMON_END		CTYPE_WCTYPE

/* Reserved: 0x0f - 0x10 */

/*
 * collate common
 */
#define	COLLATE_COLLATE_DESTRUCTOR	0x11
#define	COLLATE_COLLATE_INIT		0x12
#define	COLLATE_FNMATCH			0x13
#define	COLLATE_REGCOMP			0x14
#define	COLLATE_REGERROR		0x15
#define	COLLATE_REGEXEC			0x16
#define	COLLATE_REGFREE			0x17
#define	COLLATE_STRCOLL			0x18
#define	COLLATE_STRXFRM			0x19
#define	COLLATE_COMMON_START		COLLATE_COLLATE_DESTRUCTOR
#define	COLLATE_COMMON_END		COLLATE_STRXFRM

/* Reserved: 0x1a - 0x1b */

/*
 * locale common
 */
#define	LOCALE_LOCALE_DESTRUCTOR	0x1c
#define	LOCALE_LOCALE_INIT		0x1d
#define	LOCALE_LOCALECONV		0x1e
#define	LOCALE_NL_LANGINFO		0x1f
#define	LOCALE_COMMON_START		LOCALE_LOCALE_DESTRUCTOR
#define	LOCALE_COMMON_END		LOCALE_NL_LANGINFO

/* Reserved: 0x20 - 0x21 */

/*
 * messages common
 */
#define	MESSAGES_MESSAGES_DESTRUCTOR	0x22
#define	MESSAGES_MESSAGES_INIT		0x23
#define	MESSAGES_NL_LANGINFO		0x24
#define	MESSAGES_COMMON_START		MESSAGES_MESSAGES_DESTRUCTOR
#define	MESSAGES_COMMON_END		MESSAGES_NL_LANGINFO

/* Reserved: 0x25 - 0x26 */

/*
 * monetary common
 */
#define	MONETARY_MONETARY_DESTRUCTOR	0x27
#define	MONETARY_MONETARY_INIT		0x28
#define	MONETARY_NL_LANGINFO		0x29
#define	MONETARY_STRFMON		0x2a
#define	MONETARY_COMMON_START		MONETARY_MONETARY_DESTRUCTOR
#define	MONETARY_COMMON_END		MONETARY_NL_LANGINFO

/* Reserved: 0x2b - 0x2c */

/*
 * numeric common
 */
#define	NUMERIC_NUMERIC_DESTRUCTOR	0x2d
#define	NUMERIC_NUMERIC_INIT		0x2e
#define	NUMERIC_NL_LANGINFO		0x2f
#define	NUMERIC_COMMON_START		NUMERIC_NUMERIC_DESTRUCTOR
#define	NUMERIC_COMMON_END		NUMERIC_NL_LANGINFO

/* Reserved: 0x30 - 0x31 */

/*
 * time common
 */
#define	TIME_TIME_DESTRUCTOR		0x32
#define	TIME_TIME_INIT			0x33
#define	TIME_NL_LANGINFO		0x34
#define	TIME_STRFTIME			0x35
#define	TIME_STRPTIME			0x36
#define	TIME_WCSFTIME			0x37
#define	TIME_GETDATE			0x38
#define	TIME_COMMON_START		TIME_TIME_DESTRUCTOR
#define	TIME_COMMON_END			TIME_GETDATE

/* Reserved: 0x39 - 0x3a */

#define	COMMON_API_START		CHARMAP_COMMON_START
#define	COMMON_API_END			TIME_COMMON_END


/* Reserved: 0x3b - 0x3f */


/*
 * User APIs
 */

/*
 * charmap user
 */
#define	CHARMAP_BTOWC			0x40
#define	CHARMAP_FGETWC			0x41
#define	CHARMAP_MBFTOWC			0x42
#define	CHARMAP_MBRTOWC			0x43
#define	CHARMAP_MBSRTOWCS		0x44
#define	CHARMAP_MBSTOWCS		0x45
#define	CHARMAP_MBTOWC			0x46
#define	CHARMAP_WCRTOMB			0x47
#define	CHARMAP_WCSRTOMBS		0x48
#define	CHARMAP_WCSTOMBS		0x49
#define	CHARMAP_WCSWIDTH		0x4a
#define	CHARMAP_WCTOB			0x4b
#define	CHARMAP_WCTOMB			0x4c
#define	CHARMAP_WCWIDTH			0x4d
#define	CHARMAP_USER_START		CHARMAP_BTOWC
#define	CHARMAP_USER_END		CHARMAP_WCWIDTH

/* Reserved: 0x4e - 0x4f */

/*
 * ctype user
 */
#define	CTYPE_ISWCTYPE			0x50
#define	CTYPE_TOWCTRANS			0x51
#define	CTYPE_TOWLOWER			0x52
#define	CTYPE_TOWUPPER			0x53
#define	CTYPE_USER_START		CTYPE_ISWCTYPE
#define	CTYPE_USER_END			CTYPE_TOWUPPER

/* Reserved: 0x54 - 0x55 */

/*
 * collate user
 */
#define	COLLATE_WCSCOLL			0x56
#define	COLLATE_WCSXFRM			0x57
#define	COLLATE_USER_START		COLLATE_WCSCOLL
#define	COLLATE_USER_END		COLLATE_WCSXFRM

/* Reserved: 0x58 - 0x59 */

#define	USER_API_START			CHARMAP_USER_START
#define	USER_API_END			COLLATE_USER_END

/*
 * Reserved: 0x5a - 0x5f
 */

/*
 * Native APIs
 */

/*
 * charmap native
 */
#define	CHARMAP_BTOWC_AT_NATIVE		0x60
#define	CHARMAP_FGETWC_AT_NATIVE	0x61
#define	CHARMAP_MBFTOWC_AT_NATIVE	0x62
#define	CHARMAP_MBRTOWC_AT_NATIVE	0x63
#define	CHARMAP_MBSRTOWCS_AT_NATIVE	0x64
#define	CHARMAP_MBSTOWCS_AT_NATIVE	0x65
#define	CHARMAP_MBTOWC_AT_NATIVE	0x66
#define	CHARMAP_WCRTOMB_AT_NATIVE	0x67
#define	CHARMAP_WCSRTOMBS_AT_NATIVE	0x68
#define	CHARMAP_WCSTOMBS_AT_NATIVE	0x69
#define	CHARMAP_WCSWIDTH_AT_NATIVE	0x6a
#define	CHARMAP_WCTOB_AT_NATIVE		0x6b
#define	CHARMAP_WCTOMB_AT_NATIVE	0x6c
#define	CHARMAP_WCWIDTH_AT_NATIVE	0x6d
#define	CHARMAP_NATIVE_START		CHARMAP_BTOWC_AT_NATIVE
#define	CHARMAP_NATIVE_END		CHARMAP_WCWIDTH_AT_NATIVE

/* Reserved: 0x6e - 0x6f */

/*
 * ctype native
 */
#define	CTYPE_ISWCTYPE_AT_NATIVE	0x70
#define	CTYPE_TOWCTRANS_AT_NATIVE	0x71
#define	CTYPE_TOWLOWER_AT_NATIVE	0x72
#define	CTYPE_TOWUPPER_AT_NATIVE	0x73
#define	CTYPE_NATIVE_START		CTYPE_ISWCTYPE_AT_NATIVE
#define	CTYPE_NATIVE_END		CTYPE_TOWUPPER_AT_NATIVE

/* Reserved: 0x74 - 0x75 */

/*
 * collate native
 */
#define	COLLATE_WCSCOLL_AT_NATIVE	0x76
#define	COLLATE_WCSXFRM_AT_NATIVE	0x77
#define	COLLATE_NATIVE_START		COLLATE_WCSCOLL_AT_NATIVE
#define	COLLATE_NATIVE_END		COLLATE_WCSXFRM_AT_NATIVE

#define	NATIVE_API_START		CHARMAP_NATIVE_START
#define	NATIVE_API_END			COLLATE_NATIVE_END

#define	START_METHOD			COMMON_API_START
#define	LAST_METHOD			NATIVE_API_END

#define	ISUSER(idx)	((idx) >= USER_API_START && (idx) <= USER_API_END)
#define	ISNATIVE(idx)	((idx) >= NATIVE_API_START && (idx) <= NATIVE_API_END)
#define	TOUSER(idx)	\
	(ISNATIVE(idx) ? ((idx) - (NATIVE_API_START - USER_API_START)) : idx)
#define	TONATIVE(idx)	\
	(ISUSER(idx) ? ((idx) + (NATIVE_API_START - USER_API_START)) : idx)

#define	SB_CODESET	0
#define	MB_CODESET	1
#define	USR_CODESET	2

#define	MX_METHOD_CLASS	3

typedef struct {
	char *method_name;			/* CLASS.component notation */
	int (*instance[MX_METHOD_CLASS])(void);	/* Entrypoint Address */
	char *c_symbol[MX_METHOD_CLASS];	/* Entrypoint (function name) */
	char *package[MX_METHOD_CLASS];	/* Package name */
	char *lib_name[MX_METHOD_CLASS];	/* library name */
	char *lib64_name[MX_METHOD_CLASS]; /* 64-bit library name */

	char *meth_proto;	/* Required calling conventions */
} method_t;

typedef struct {
	char	*library;
	char	*library64;
} library_t;

#define	METH_PROTO(m)	(std_methods[m].meth_proto)
#define	METH_NAME(m)    (std_methods[m].c_symbol[method_class])
#define	METH_OFFS(m)    (std_methods[m].instance[method_class])
#define	METH_LIB(m)	(std_methods[m].lib_name[method_class])
#define	METH_LIB64(m)	(std_methods[m].lib64_name[method_class])
#define	METH_PKG(m)	(std_methods[m].package[method_class])

typedef struct {
	int	method_index;	/* method index */
	int	(*instance[MX_METHOD_CLASS])(void);	/* Entrypoint Address */
	char	*c_symbol[MX_METHOD_CLASS]; 	/* Entrypoint (function name) */
} ow_method_t;

#endif	/* _LOCALEDEF_METHOD_H */

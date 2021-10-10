/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
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
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: method.c,v $ $Revision: 1.5.2.9 $"
 *	" (OSF) $Date: 1992/03/25 22:30:14 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 * FUNCTIONS:
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/cmd/nls/method.c, cmdnls, bos320 6/1/91 14:47:04
 */

#include <stdio.h>
#include "locdef.h"


/* The following prototype declarations are for the method_t structure */
/* Actual definitions are different */
extern int	__mbftowc_euc(void);
extern int	__mbftowc_sb(void);
extern int	__fgetwc_sb(void);
extern int	__mblen_sb(void);
extern int	__mblen_gen(void);
extern int	__mbstowcs_sb(void);
extern int	__mbtowc_sb(void);
extern int	__wcstombs_sb(void);
extern int	__wcswidth_sb(void);
extern int	__wctomb_sb(void);
extern int	__wcwidth_sb(void);
extern int	__collate_init(void);
extern int	__wctype_std(void);
extern int	__ctype_init(void);
extern int	__iswctype_std(void);
extern int	__strcoll_sb(void);
extern int	__strcoll_std(void);
extern int	__strxfrm_sb(void);
extern int	__strxfrm_std(void);
extern int	__towlower_std(void);
extern int	__towupper_std(void);
extern int	__wcscoll_std(void);
extern int	__wcsxfrm_std(void);
extern int	__localeconv_std(void);
extern int	__nl_langinfo_std(void);
extern int	__strfmon_std(void);
extern int	__strftime_std(void);
extern int	__strptime_std(void);
extern int	__getdate_std(void);
extern int	__wcsftime_std(void);
extern int	__regcomp_std(void);
extern int	__regerror_std(void);
extern int	__regexec_std(void);
extern int	__regfree_std(void);
extern int	__fnmatch_std(void);
extern int	__fnmatch_sb(void);
extern int	__charmap_init(void);
extern int	__locale_init(void);
extern int	__monetary_init(void);
extern int	__numeric_init(void);
extern int	__messages_init(void);
extern int	__time_init(void);
extern int	__eucpctowc_gen(void);
extern int	__wctoeucpc_gen(void);
extern int	__trwctype_std(void);
extern int	__wctrans_std(void);
extern int	__towctrans_std(void);
extern int	__fgetwc_euc(void);
extern int	__mbftowc_euc(void);
extern int	__mbstowcs_euc(void);
extern int	__mbtowc_euc(void);
extern int	__wcstombs_euc(void);
extern int	__wctomb_euc(void);
extern int	__iswctype_bc(void);
extern int	__towlower_bc(void);
extern int	__towupper_bc(void);
extern int	__fgetwc_dense(void);
extern int	__iswctype_sb(void);
extern int	__mbftowc_dense(void);
extern int	__mbstowcs_dense(void);
extern int	__mbtowc_dense(void);
extern int	__wcstombs_dense(void);
extern int	__wctomb_dense(void);
extern int	__wcswidth_euc(void);
extern int	__wcswidth_dense(void);
extern int	__wcwidth_euc(void);
extern int	__wcwidth_dense(void);
extern int	__towctrans_bc(void);

extern int	__btowc_sb(void);
extern int	__btowc_euc(void);
extern int	__btowc_dense(void);
extern int	__wctob_sb(void);
extern int	__wctob_euc(void);
extern int	__wctob_dense(void);
extern int	__mbsinit_gen(void);
extern int	__mbrlen_sb(void);
extern int	__mbrlen_gen(void);
extern int	__mbrtowc_sb(void);
extern int	__mbrtowc_euc(void);
extern int	__mbrtowc_dense(void);
extern int	__wcrtomb_sb(void);
extern int	__wcrtomb_euc(void);
extern int	__wcrtomb_dense(void);
extern int	__mbsrtowcs_sb(void);
extern int	__mbsrtowcs_euc(void);
extern int	__mbsrtowcs_dense(void);
extern int	__wcsrtombs_sb(void);
extern int	__wcsrtombs_euc(void);
extern int	__wcsrtombs_dense(void);
extern int	__wcswidth_bc(void);
extern int	__wcswidth_std(void);
extern int	__wcwidth_bc(void);
extern int	__wcwidth_std(void);
extern int	__wcscoll_bc(void);
extern int	__wcsxfrm_bc(void);

/*
 * extern __charmap_destructor();
 * extern __ctype_destructor();
 * extern __locale_destructor();
 * extern __monetary_destructor();
 * extern __numeric_destructor();
 * extern __resp_destructor();
 * extern __time_destructor();
 * extern __collate_destructor();
 */


/*
 * Define the normal place to search for methods.  The default would be
 * in the shared libc.
 */
#define	LIBC		DEFAULT_METHOD
#define	LIBC64		DEFAULT_METHOD64
#define	LIBLIST		LIBC, LIBC, NULL
#define	LIB64LIST	LIBC64, LIBC64, NULL

/*
 * Define the standard package names.  Right now libc is it.
 * The NULL is for the extensible method support
 */
#define	PKGLIST		"libc", "libc", NULL


static method_t std_methods_tbl[LAST_METHOD + 1] = {
	/* 0x00 */
	{
		"charmap.destructor",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		"charmap.init",
		__charmap_init, __charmap_init, 0,
		"__charmap_init", "__charmap_init", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"_LC_charmap_t *%s(_LC_locale_t *)"
	},
	{
		"charmap.eucpctowc",
		__eucpctowc_gen, __eucpctowc_gen, 0,
		"__eucpctowc_gen", "__eucpctowc_gen", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wchar_t %s(_LC_charmap_t *, wchar_t)"
	},
	{
		"charmap.mblen",
		__mblen_sb, __mblen_gen, 0,
		"__mblen_sb", "__mblen_gen", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_charmap_t*, const char*, size_t)"
	},
	{
		"charmap.mbsinit",
		__mbsinit_gen, __mbsinit_gen, 0,
		"__mbsinit_gen", "__mbsinit_gen", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_charmap_t *, const mbstate_t *)"
	},
	{
		"charmap.mbrlen",
		__mbrlen_sb, __mbrlen_gen, 0,
		"__mbrlen_sb", "__mbrlen_gen", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, const char *, size_t, mbstate_t *)"
	},
	{
		"charmap.nl_langinfo",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		"charmap.wctoeucpc",
		__wctoeucpc_gen, __wctoeucpc_gen, 0,
		"__wctoeucpc_gen", "__wctoeucpc_gen", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wchar_t %s(_LC_charmap_t *, wchar_t)"
	},
	/* 0x08 - 0x09 */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x0a */
	{
		"ctype.destructor",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		"ctype.init",
		__ctype_init, __ctype_init, 0,
		"__ctype_init", "__ctype_init", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"_LC_ctype_t *%s(_LC_locale_t *)"
	},
	{
		"ctype.trwctype",
		__trwctype_std, __trwctype_std, 0,
		"__trwctype_std", "__trwctype_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wchar_t %s(_LC_ctype_t *, wchar_t, int)"
	},
	{
		"ctype.wctrans",
		__wctrans_std,   __wctrans_std,   0,
		"__wctrans_std", "__wctrans_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wctrans_t %s(_LC_ctype_t *, const char *)"
	},
	{
		"ctype.wctype",
		__wctype_std, __wctype_std, 0,
		"__wctype_std",  "__wctype_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wctype_t %s(_LC_ctype_t *, const char *)"
	},
	/* 0x0f - 0x10 */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x11 */
	{
		"collate.destructor",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		"collate.init",
		__collate_init, __collate_init, 0,
		"__collate_init", "__collate_init", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"_LC_collate_t *%s(_LC_locale_t *)"
	},
	{
		"collate.fnmatch",
		__fnmatch_sb, __fnmatch_std, 0,
		"__fnmatch_sb", "__fnmatch_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_collate_t *, const char *, const char *, "
		"const char *, int)"
	},
	{
		"collate.regcomp",
		__regcomp_std, __regcomp_std, 0,
		"__regcomp_std", "__regcomp_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_collate_t *, regex_t *, const char *, int)"
	},
	{
		"collate.regerror",
		__regerror_std, __regerror_std, 0,
		"__regerror_std", "__regerror_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_collate_t *, int, const regex_t *, char *, "
		"size_t)"
	},
	{
		"collate.regexec",
		__regexec_std, __regexec_std, 0,
		"__regexec_std", "__regexec_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_collate_t *, const regex_t *, const char *, "
		"size_t, regmatch_t *, int)"
	},
	{
		"collate.regfree",
		__regfree_std, __regfree_std, 0,
		"__regfree_std", "__regfree_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"void %s(_LC_collate_t *, regex_t *)"
	},
	{
		"collate.strcoll",
		__strcoll_sb, __strcoll_std, 0,
		"__strcoll_sb", "__strcoll_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_collate_t *, const char *, const char *)"
	},
	{
		"collate.strxfrm",
		__strxfrm_sb, __strxfrm_std, 0,
		"__strxfrm_sb", "__strxfrm_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_collate_t *, char *, const char *, size_t)"
	},
	/* 0x1a - 0x1b */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x1c */
	{
		"locale.destructor",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		"locale.init",
		__locale_init, __locale_init, 0,
		"__locale_init", "__locale_init", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"_LC_locale_t *%s(_LC_locale_t *)"
	},
	{
		"locale.localeconv",
		__localeconv_std, __localeconv_std, 0,
		"__localeconv_std", "__localeconv_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
#ifdef	EXT_DEBUG
		"struct lconv *%s()"
#else
		"struct lconv *%s(_LC_locale_t *)"
#endif
	},
	{
		"locale.nl_langinfo",
		__nl_langinfo_std, __nl_langinfo_std, 0,
		"__nl_langinfo_std", "__nl_langinfo_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
#ifdef	EXT_DEBUG
		"char *%s()"
#else
		"char *%s(_LC_locale_t *, nl_item)"
#endif
	},
	/* 0x20 - 0x21 */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x22 */
	{
		"messages.destructor",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		"messages.init",
		__messages_init, __messages_init, 0,
		"__messages_init", "__messages_init", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"_LC_messages_t *%s(_LC_locale_t *)"
	},
	{
		"messages.nl_langinfo",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x25 - 0x26 */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x27 */
	{
		"monetary.destructor",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		"monetary.init",
		__monetary_init, __monetary_init, 0,
		"__monetary_init", "__monetary_init", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"_LC_monetary_t *%s(_LC_locale_t *)"
	},
	{
		"monetary.nl_langinfo",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		"monetary.strfmon",
		__strfmon_std, __strfmon_std, 0,
		"__strfmon_std", "__strfmon_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"ssize_t %s(_LC_monetary_t *, char *, size_t, "
		"const char *, va_list)"
	},
	/* 0x2b - 0x2c */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x2d */
	{
		"numeric.destructor",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		"numeric.init",
		__numeric_init, __numeric_init, 0,
		"__numeric_init", "__numeric_init", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"_LC_numeric_t *%s(_LC_locale_t *)"
	},
	{
		"numeric.nl_langinfo",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x30 - 0x31 */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x32 */
	{
		"time.destructor",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		"time.init",
		__time_init, __time_init, 0,
		"__time_init", "__time_init", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"_LC_time_t *%s(_LC_locale_t *)"
	},
	{
		"time.nl_langinfo",
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		"time.strftime",
		__strftime_std, __strftime_std, 0,
		"__strftime_std", "__strftime_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_time_t *, char *, size_t, const char *, "
		"const struct tm *)"
	},
	{
		"time.strptime",
		__strptime_std, __strptime_std, 0,
		"__strptime_std", "__strptime_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"char *%s(_LC_time_t *, const char *, const char *, "
		"struct tm *)"
	},
	{
		"time.wcsftime",
		__wcsftime_std, __wcsftime_std, 0,
		"__wcsftime_std", "__wcsftime_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_time_t *, wchar_t *, size_t, const char *, "
		"const struct tm *)"
	},
	{
		"time.getdate",
		__getdate_std, __getdate_std, 0,
		"__getdate_std", "__getdate_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"struct tm *%s(_LC_time_t *, const char *)"
	},
	/* 0x39 - 0x3a */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* Reserved: 0x3b - 0x3f */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x40 */
	{
		"charmap.btowc",
		__btowc_euc, __btowc_euc, 0,
		"__btowc_euc", "__btowc_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wint_t %s(_LC_charmap_t *, int)"
	},
	{
		"charmap.fgetwc",
		__fgetwc_euc, __fgetwc_euc, 0,
		"__fgetwc_euc", "__fgetwc_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wint_t %s(_LC_charmap_t *, FILE *)"
	},
	{
		"charmap.mbftowc",
		__mbftowc_euc, __mbftowc_euc, 0,
		"__mbftowc_euc", "__mbftowc_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
#ifdef	EXT_DEBUG
		"int %s(_LC_charmap_t *, char *, wchar_t *, int (*)(), "
		"int *)"
#else
		"int %s(_LC_charmap_t *, char *, wchar_t *, int (*)(void), "
		"int *)"
#endif
	},
	{
		"charmap.mbrtowc",
		__mbrtowc_euc, __mbrtowc_euc, 0,
		"__mbrtowc_euc", "__mbrtowc_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, wchar_t *, const char *, "
		"size_t, mbstate_t *)"
	},
	{
		"charmap.mbsrtowcs",
		__mbsrtowcs_euc, __mbsrtowcs_euc, 0,
		"__mbsrtowcs_euc", "__mbsrtowcs_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, wchar_t *, const char **, "
		"size_t, mbstate_t *)"
	},
	{
		"charmap.mbstowcs",
		__mbstowcs_euc, __mbstowcs_euc, 0,
		"__mbstowcs_euc", "__mbstowcs_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, wchar_t *, const char *, size_t)"
	},
	{
		"charmap.mbtowc",
		__mbtowc_euc, __mbtowc_euc, 0,
		"__mbtowc_euc", "__mbtowc_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_charmap_t *, wchar_t *, const char *, size_t)"
	},
	{
		"charmap.wcrtomb",
		__wcrtomb_euc, __wcrtomb_euc, 0,
		"__wcrtomb_euc", "__wcrtomb_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, char *, wchar_t, mbstate_t *)"
	},
	{
		"charmap.wcsrtombs",
		__wcsrtombs_euc, __wcsrtombs_euc, 0,
		"__wcsrtombs_euc", "__wcsrtombs_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, char *, const wchar_t **, "
		"size_t, mbstate_t *)"
	},
	{
		"charmap.wcstombs",
		__wcstombs_euc, __wcstombs_euc, 0,
		"__wcstombs_euc", "__wcstombs_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, char *, const wchar_t *, size_t)"
	},
	{
		"charmap.wcswidth",
		__wcswidth_euc, __wcswidth_euc, 0,
		"__wcswidth_euc", "__wcswidth_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_charmap_t *, const wchar_t *, size_t)"
	},
	{
		"charmap.wctob",
		__wctob_euc, __wctob_euc, 0,
		"__wctob_euc", "__wctob_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_charmap_t *, wint_t)"
	},
	{
		"charmap.wctomb",
		__wctomb_euc, __wctomb_euc, 0,
		"__wctomb_euc", "__wctomb_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_charmap_t *, char *, wchar_t)"
	},
	{
		"charmap.wcwidth",
		__wcwidth_euc, __wcwidth_euc, 0,
		"__wcwidth_euc", "__wcwidth_euc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
#ifdef	EXT_DEBUG
		"int %s(_LC_charmap_t *, const wchar_t)"
#else
		"int %s(_LC_charmap_t *, wchar_t)"
#endif
	},
	/* 0x4e - 0x4f */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x50 */
	{
		"ctype.iswctype",
		__iswctype_bc, __iswctype_bc, 0,
		"__iswctype_bc", "__iswctype_bc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_ctype_t *, wchar_t, wctype_t)"
	},
	{
		"ctype.towctrans",
		__towctrans_bc,   __towctrans_bc,   0,
		"__towctrans_bc", "__towctrans_bc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wint_t %s(_LC_ctype_t *, wint_t, wctrans_t)"
	},
	{
		"ctype.towlower",
		__towlower_bc,   __towlower_bc,   0,
		"__towlower_bc", "__towlower_bc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wint_t %s(_LC_ctype_t *, wint_t)"
	},
	{
		"ctype.towupper",
		__towupper_bc, __towupper_bc, 0,
		"__towupper_bc", "__towupper_bc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wint_t %s(_LC_ctype_t *, wint_t)"
	},
	/* 0x54 - 0x55 */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x56 */
	{
		"collate.wcscoll",
		__wcscoll_bc, __wcscoll_bc, 0,
		"__wcscoll_bc", "__wcscoll_bc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_collate_t *, const wchar_t *, const wchar_t *)"
	},
	{
		"collate.wcsxfrm",
		__wcsxfrm_bc, __wcsxfrm_bc, 0,
		"__wcsxfrm_bc", "__wcsxfrm_bc", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_collate_t *, wchar_t *, const wchar_t *, "
		"size_t)"
	},
	/* 0x58 - 0x59 */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* Reserved: 0x5a - 0x5f */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x60 */
	{
		"charmap.btowc_at_native",
		__btowc_sb, __btowc_dense, 0,
		"__btowc_sb", "__btowc_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wint_t %s(_LC_charmap_t *, int)"
	},
	{
		"charmap.fgetwc_at_native",
		__fgetwc_sb,   __fgetwc_dense,   0,
		"__fgetwc_sb", "__fgetwc_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wint_t %s(_LC_charmap_t *, FILE *)"
	},

	{
		"charmap.mbftowc_at_native",
		__mbftowc_sb,   __mbftowc_dense,   0,
		"__mbftowc_sb", "__mbftowc_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
#ifdef	EXT_DEBUG
		"int %s(_LC_charmap_t *, char *, wchar_t *, int (*)(), "
		"int *)"
#else
		"int %s(_LC_charmap_t *, char *, wchar_t *, int (*)(void), "
		"int *)"
#endif
	},
	{
		"charmap.mbrtowc_at_native",
		__mbrtowc_sb, __mbrtowc_dense, 0,
		"__mbrtowc_sb", "__mbrtowc_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, wchar_t *, const char *, "
		"size_t, mbstate_t *)"
	},
	{
		"charmap.mbsrtowcs_at_native",
		__mbsrtowcs_sb, __mbsrtowcs_dense, 0,
		"__mbsrtowcs_sb", "__mbsrtowcs_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, wchar_t *, const char **, "
		"size_t, mbstate_t *)"
	},
	{
		"charmap.mbstowcs_at_native",
		__mbstowcs_sb,   __mbstowcs_dense,   0,
		"__mbstowcs_sb", "__mbstowcs_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, wchar_t *, const char *, size_t)"
	},
	{
		"charmap.mbtowc_at_native",
		__mbtowc_sb,   __mbtowc_dense,   0,
		"__mbtowc_sb", "__mbtowc_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_charmap_t *, wchar_t *, const char *, size_t)"
	},
	{
		"charmap.wcrtomb_at_native",
		__wcrtomb_sb, __wcrtomb_dense, 0,
		"__wcrtomb_sb", "__wcrtomb_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, char *, wchar_t, mbstate_t *)"
	},
	{
		"charmap.wcsrtombs_at_native",
		__wcsrtombs_sb, __wcsrtombs_dense, 0,
		"__wcsrtombs_sb", "__wcsrtombs_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, char *, const wchar_t **, "
		"size_t, mbstate_t *)"
	},
	{
		"charmap.wcstombs_at_native",
		__wcstombs_sb,   __wcstombs_dense,   0,
		"__wcstombs_sb", "__wcstombs_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_charmap_t *, char *, const wchar_t *, size_t)"
	},
	{
		"charmap.wcswidth_at_native",
		__wcswidth_sb,   __wcswidth_dense,   0,
		"__wcswidth_sb", "__wcswidth_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_charmap_t *, const wchar_t *, size_t)"
	},
	{
		"charmap.wctob_at_native",
		__wctob_sb, __wctob_dense, 0,
		"__wctob_sb", "__wctob_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_charmap_t *, wint_t)"
	},
	{
		"charmap.wctomb_at_native",
		__wctomb_sb,   __wctomb_dense,   0,
		"__wctomb_sb", "__wctomb_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_charmap_t *, char *, wchar_t)"
	},
	{
		"charmap.wcwidth_at_native",
		__wcwidth_sb,   __wcwidth_dense,   0,
		"__wcwidth_sb", "__wcwidth_dense", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
#ifdef	EXT_DEBUG
		"int %s(_LC_charmap_t *, const wchar_t)"
#else
		"int %s(_LC_charmap_t *, wchar_t)"
#endif
	},
	/* 0x6e - 0x6f */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x70 */
	{
		"ctype.iswctype_at_native",
		__iswctype_sb,   __iswctype_std,   0,
		"__iswctype_sb", "__iswctype_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_ctype_t *, wchar_t, wctype_t)"
	},
	{
		"ctype.towctrans_at_native",
		__towctrans_std,   __towctrans_std,   0,
		"__towctrans_std", "__towctrans_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wint_t %s(_LC_ctype_t *, wint_t, wctrans_t)"
	},
	{
		"ctype.towlower_at_native",
		__towlower_std,   __towlower_std,   0,
		"__towlower_std", "__towlower_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wint_t %s(_LC_ctype_t *, wint_t)"
	},
	{
		"ctype.towupper_at_native",
		__towupper_std,   __towupper_std,   0,
		"__towupper_std", "__towupper_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"wint_t %s(_LC_ctype_t *, wint_t)"
	},
	/* 0x74 - 0x75 */
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	{
		0,
		0, 0, 0,
		0, 0, 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		0
	},
	/* 0x76 */
	{
		"collate.wcscoll_at_native",
		__wcscoll_std,   __wcscoll_std,   0,
		"__wcscoll_std", "__wcscoll_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"int %s(_LC_collate_t *, const wchar_t *, const wchar_t *)"
	},
	{
		"collate.wcsxfrm_at_native",
		__wcsxfrm_std,   __wcsxfrm_std,   0,
		"__wcsxfrm_std", "__wcsxfrm_std", 0,
		PKGLIST,
		LIBLIST,
		LIB64LIST,
		"size_t %s(_LC_collate_t *, wchar_t *, const wchar_t * , "
		"size_t)"
	}
};

method_t *std_methods = std_methods_tbl;

static ow_method_t	ow_methods_tbl[] = {
	{
		CHARMAP_WCSWIDTH,
		__wcswidth_bc, __wcswidth_bc, __wcswidth_bc,
		"__wcswidth_bc", "__wcswidth_bc", "__wcswidth_bc"
	},
	{
		CHARMAP_WCSWIDTH_AT_NATIVE,
		__wcswidth_std, __wcswidth_std, __wcswidth_std,
		"__wcswidth_std", "__wcswidth_std", "__wcswidth_std"
	},
	{
		CHARMAP_WCWIDTH,
		__wcwidth_bc, __wcwidth_bc, __wcwidth_bc,
		"__wcwidth_bc", "__wcwidth_bc", "__wcwidth_bc"
	},
	{
		CHARMAP_WCWIDTH_AT_NATIVE,
		__wcwidth_std, __wcwidth_std, __wcwidth_std,
		"__wcwidth_std", "__wcwidth_std", "__wcwidth_std"
	},
	{
		LAST_METHOD + 1,
		NULL, NULL, NULL,
		NULL, NULL, NULL
	}
};

ow_method_t	*ow_methods = ow_methods_tbl;

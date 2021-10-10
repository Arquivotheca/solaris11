/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_LIBC_I18N_H
#define	_LIBC_I18N_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/localedef.h>
#include <stdio.h>
#include <wchar.h>

extern int _getcolval(_LC_collate_t *, wchar_t *, wchar_t, const char *, int);
extern int __regcomp_C(_LC_collate_t *, regex_t *, const char *, int);
extern int __fnmatch_C(_LC_collate_t *, const char *, const char *,
	const char *, int);
extern ssize_t  __strfmon_std(_LC_monetary_t *, char *, size_t,
	const char *, va_list);
extern _LC_charmap_t	*__charmap_init(_LC_locale_t *);
extern int	__charmap_destructor(_LC_locale_t *);
extern int	__mbtowc_sb(_LC_charmap_t *, wchar_t *, const char *,
	size_t);
extern size_t	__mbstowcs_sb(_LC_charmap_t *, wchar_t *,
	const char *, size_t);
extern int	__wctomb_sb(_LC_charmap_t *, char *, wchar_t);
extern size_t	__wcstombs_sb(_LC_charmap_t *, char *,
	const wchar_t *, size_t);
extern int	__mblen_sb(_LC_charmap_t *, const char *, size_t);
extern int	__wcswidth_C(_LC_charmap_t *, const wchar_t *,
	size_t);
extern int	__wcswidth_sb(_LC_charmap_t *, const wchar_t *,
	size_t);
extern int	__wcwidth_sb(_LC_charmap_t *, wchar_t);
extern int	__wcwidth_C(_LC_charmap_t *, wchar_t);
extern int	__mbftowc_sb(_LC_charmap_t *, char *, wchar_t *,
	int (*)(void), int *);
extern wint_t	__fgetwc_sb(_LC_charmap_t *, FILE *);
extern wint_t	__btowc_sb(_LC_charmap_t *, int);
extern int	__wctob_sb(_LC_charmap_t *, wint_t);
extern int	__mbsinit_gen(_LC_charmap_t *, const mbstate_t *);
extern size_t	__mbrlen_sb(_LC_charmap_t *, const char *,
	size_t, mbstate_t *);
extern size_t	__mbrtowc_sb(_LC_charmap_t *, wchar_t *,
	const char *, size_t, mbstate_t *);
extern size_t	__wcrtomb_sb(_LC_charmap_t *, char *, wchar_t,
	mbstate_t *);
extern size_t	__mbsrtowcs_sb(_LC_charmap_t *, wchar_t *,
	const char **, size_t, mbstate_t *);
extern size_t	__wcsrtombs_sb(_LC_charmap_t *, char *,
	const wchar_t **, size_t, mbstate_t *);
extern _LC_ctype_t	*__ctype_init(_LC_locale_t *);
extern int	__ctype_destructor(_LC_locale_t *);
extern wint_t	__towlower_std(_LC_ctype_t *, wint_t);
extern wint_t	__towupper_std(_LC_ctype_t *, wint_t);
extern wctype_t	__wctype_std(_LC_ctype_t *, const char *);
extern int	__iswctype_sb(_LC_ctype_t *, wchar_t, wctype_t);
extern int	__iswctype_std(_LC_ctype_t *, wchar_t, wctype_t);
extern wchar_t	__trwctype_std(_LC_ctype_t *, wchar_t, int);
extern wint_t	__towctrans_std(_LC_ctype_t *, wint_t, wctrans_t);
extern wctrans_t __wctrans_std(_LC_ctype_t *, const char *);
extern _LC_collate_t	*__collate_init(_LC_locale_t *);
extern int	__collate_destructor(_LC_locale_t *);
extern int	__strcoll_C(_LC_collate_t *, const char *, const char *);
extern size_t	__strxfrm_C(_LC_collate_t *, char *, const char *, size_t);
extern int	__wcscoll_C(_LC_collate_t *, const wchar_t *,
	const wchar_t *);
extern size_t	__wcsxfrm_C(_LC_collate_t *, wchar_t *, const wchar_t *,
	size_t);
extern int	__fnmatch_C(_LC_collate_t *, const char *, const char *,
	const char *, int);
extern int	__regcomp_C(_LC_collate_t *, regex_t *, const char *,
	int);
extern size_t	__regerror_std(_LC_collate_t *, int, const regex_t *,
	char *, size_t);
extern int	__regexec_C(_LC_collate_t *, const regex_t *, const char *,
	size_t, regmatch_t *, int);
extern void	__regfree_std(_LC_collate_t *, regex_t *);
extern _LC_time_t	*__time_init(_LC_locale_t *);
extern int	__time_destructor(_LC_locale_t *);
extern size_t	__strftime_std(_LC_time_t *, char *, size_t,
	const char *, const struct tm *);
extern char	*__strptime_std(_LC_time_t *, const char *, const char *,
	struct tm *);
extern struct tm	*__getdate_std(_LC_time_t *, const char *);
extern size_t	__wcsftime_std(_LC_time_t *, wchar_t *, size_t,
	const char *, const struct tm *);
extern _LC_monetary_t	*__monetary_init(_LC_locale_t *);
extern int	__monetary_destructor(_LC_locale_t *);
extern ssize_t	__strfmon_std(_LC_monetary_t *, char *, size_t,
	const char *, va_list);
extern _LC_numeric_t	*__numeric_init(_LC_locale_t *);
extern int	__numeric_destructor(_LC_locale_t *);
extern _LC_messages_t	*__messages_init(_LC_locale_t *);
extern int	__messages_destructor(_LC_locale_t *);
extern _LC_locale_t	*__locale_init(_LC_locale_t *);
extern int	__locale_destructor(_LC_locale_t *);
extern char *__nl_langinfo_std(_LC_locale_t *, nl_item);
extern struct lconv *__localeconv_std(_LC_locale_t *);

extern const _LC_time_t	__C_time_object;

/* This macro checks for control character set 1 */
#define	IS_C1(c)	(((c) >= 0x80) && ((c) <= 0x9f))

/*
 * Macro for testing a valid byte value for a multi-byte/single-byte
 * character. A 96-character character set is assumed.
 */
#define	MB_MIN_BYTE	0xa0
#define	MB_MAX_BYTE	0xff
#define	IS_VALID(c)	(((c) >= MB_MIN_BYTE) && ((c) <= MB_MAX_BYTE))
#define	STRIP(c)	((c) - MB_MIN_BYTE)
#define	COMPOSE(c1, c2)	(((c1) << WCHAR_SHIFT) | (c2))

/* This macro checks if hdl is equal to the C time object */
#define	IS_C_TIME(hdl)	((hdl) == &__C_time_object)

#endif /* _LIBC_I18N_H */

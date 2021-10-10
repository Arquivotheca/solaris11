/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#pragma ident	"%Z%%M%	%I%	%E% SMI"

#define	_LCONV_C99
#include <locale.h>
#include <sys/localedef.h>

extern int __mbftowc_euc(_LC_charmap_t *, char *, wchar_t *, int (*)(void),
    int *);
extern wint_t __fgetwc_euc(_LC_charmap_t *, FILE *);
extern wchar_t __eucpctowc_gen(_LC_charmap_t *, wchar_t);
extern wchar_t __wctoeucpc_gen(_LC_charmap_t *, wchar_t);
extern _LC_charmap_t *__charmap_init(_LC_locale_t *);
extern int __mblen_sb(_LC_charmap_t *, const char *, size_t);
extern size_t __mbstowcs_euc(_LC_charmap_t *, wchar_t *, const char *, size_t);
extern int __mbtowc_euc(_LC_charmap_t *, wchar_t *, const char *, size_t);
extern size_t __wcstombs_euc(_LC_charmap_t *, char *, const wchar_t *, size_t);
extern int __wcswidth_euc(_LC_charmap_t *, const wchar_t *, size_t);
extern int __wctomb_euc(_LC_charmap_t *, char *, wchar_t);
extern int __wcwidth_euc(_LC_charmap_t *, wchar_t);
extern int __fnmatch_sb(_LC_collate_t *, const char *, const char *,
    const char *, int);
extern int __regcomp_std(_LC_collate_t *, regex_t *, const char *, int);
extern size_t __regerror_std(_LC_collate_t *, int, const regex_t *, char *,
    size_t);
extern int __regexec_std(_LC_collate_t *, const regex_t *, const char *,
    size_t, regmatch_t *, int);
extern void __regfree_std(_LC_collate_t *, regex_t *);
extern int __strcoll_sb(_LC_collate_t *, const char *, const char *);
extern size_t __strxfrm_sb(_LC_collate_t *, char *, const char *, size_t);
extern int __wcscoll_std(_LC_collate_t *, const wchar_t *, const wchar_t *);
extern size_t __wcsxfrm_std(_LC_collate_t *, wchar_t *, const wchar_t *,
    size_t);
extern _LC_collate_t *__collate_init(_LC_locale_t *);
extern wctype_t __wctype_std(_LC_ctype_t *, const char *);
extern _LC_ctype_t *__ctype_init(_LC_locale_t *);
extern int __iswctype_bc(_LC_ctype_t *, wchar_t, wctype_t);
extern wint_t __towlower_bc(_LC_ctype_t *, wint_t);
extern wint_t __towupper_bc(_LC_ctype_t *, wint_t);
extern _LC_locale_t *__locale_init(_LC_locale_t *);
extern struct lconv *__localeconv_std(_LC_locale_t *);
extern char *__nl_langinfo_std(_LC_locale_t *, nl_item);
extern _LC_monetary_t *__monetary_init(_LC_locale_t *);
extern ssize_t __strfmon_std(_LC_monetary_t *, char *, size_t, const char *,
    va_list);
extern _LC_numeric_t *__numeric_init(_LC_locale_t *);
extern _LC_messages_t *__messages_init(_LC_locale_t *);
extern _LC_time_t *__time_init(_LC_locale_t *);
extern size_t __strftime_std(_LC_time_t *, char *, size_t, const char *,
    const struct tm *);
extern char *__strptime_std(_LC_time_t *, const char *, const char *,
    struct tm *);
extern size_t __wcsftime_std(_LC_time_t *, wchar_t *, size_t, const char *,
    const struct tm *);
extern struct tm *__getdate_std(_LC_time_t *, const char *);
extern wchar_t __trwctype_std(_LC_ctype_t *, wchar_t, int);
extern wctrans_t __wctrans_std(_LC_ctype_t *, const char *);
extern wint_t __towctrans_bc(_LC_ctype_t *, wint_t, wctrans_t);
extern wint_t __fgetwc_sb(_LC_charmap_t *, FILE *);
extern int __iswctype_sb(_LC_ctype_t *, wchar_t, wctype_t);
extern int __mbftowc_sb(_LC_charmap_t *, char *, wchar_t *, int (*)(void),
    int *);
extern size_t __mbstowcs_sb(_LC_charmap_t *, wchar_t *, const char *, size_t);
extern int __mbtowc_sb(_LC_charmap_t *, wchar_t *, const char *, size_t);
extern wint_t __towlower_std(_LC_ctype_t *, wint_t);
extern wint_t __towupper_std(_LC_ctype_t *, wint_t);
extern int __wcscoll_std(_LC_collate_t *, const wchar_t *, const wchar_t *);
extern size_t __wcstombs_sb(_LC_charmap_t *, char *, const wchar_t *, size_t);
extern size_t __wcsxfrm_std(_LC_collate_t *, wchar_t *, const wchar_t *,
    size_t);
extern int __wctomb_sb(_LC_charmap_t *, char *, wchar_t);
extern int __wcswidth_sb(_LC_charmap_t *, const wchar_t *, size_t);
extern int __wcwidth_sb(_LC_charmap_t *, wchar_t);
extern wint_t __towctrans_std(_LC_ctype_t *, wint_t, wctrans_t);
extern wint_t __btowc_euc(_LC_charmap_t *, int);
extern int __wctob_euc(_LC_charmap_t *, wint_t);
extern int __mbsinit_gen(_LC_charmap_t *, const mbstate_t *);
extern size_t __mbrlen_sb(_LC_charmap_t *, const char *, size_t, mbstate_t *);
extern size_t __mbrtowc_euc(_LC_charmap_t *, wchar_t *, const char *, size_t,
    mbstate_t *);
extern size_t __wcrtomb_euc(_LC_charmap_t *, char *, wchar_t, mbstate_t *);
extern size_t __mbsrtowcs_euc(_LC_charmap_t *, wchar_t *, const char **,
    size_t, mbstate_t *);
extern size_t __wcsrtombs_euc(_LC_charmap_t *, char *, const wchar_t **,
    size_t, mbstate_t *);
extern wint_t __btowc_sb(_LC_charmap_t *, int);
extern int __wctob_sb(_LC_charmap_t *, wint_t);
extern size_t __mbrtowc_sb(_LC_charmap_t *, wchar_t *, const char *, size_t,
    mbstate_t *);
extern size_t __wcrtomb_sb(_LC_charmap_t *, char *, wchar_t, mbstate_t *);
extern size_t __mbsrtowcs_sb(_LC_charmap_t *, wchar_t *, const char **,
    size_t, mbstate_t *);
extern size_t __wcsrtombs_sb(_LC_charmap_t *, char *, const wchar_t **, size_t,
    mbstate_t *);
/* ------------------------------EUC INFO----------------------------- */
static const _LC_euc_info_t cm_eucinfo = {
	(char)1,
	(char)1,
	(char)0,
	(char)0,
	(char)1,
	(char)1,
	(char)0,
	(char)0,
	160,
	256,
	256,
	255,
	-805306240,
	0,
	0,
};

/* ------------------------- CHARMAP OBJECT  ------------------------- */
static const _LC_methods_charmap_t native_methods_charmap = {
	(short)0,
	(short)0,
	(char *(*) ()) 0,
	__mbtowc_sb,
	__mbstowcs_sb,
	__wctomb_sb,
	__wcstombs_sb,
	__mblen_sb,
	__wcswidth_sb,
	__wcwidth_sb,
	__mbftowc_sb,
	__fgetwc_sb,
	__btowc_sb,
	__wctob_sb,
	__mbsinit_gen,
	__mbrlen_sb,
	__mbrtowc_sb,
	__wcrtomb_sb,
	__mbsrtowcs_sb,
	__wcsrtombs_sb,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0
};

static const _LC_methods_charmap_t user_methods_charmap = {
	(short)0,
	(short)0,
	(char *(*) ()) 0,
	__mbtowc_euc,
	__mbstowcs_euc,
	__wctomb_euc,
	__wcstombs_euc,
	__mblen_sb,
	__wcswidth_euc,
	__wcwidth_euc,
	__mbftowc_euc,
	__fgetwc_euc,
	__btowc_euc,
	__wctob_euc,
	__mbsinit_gen,
	__mbrlen_sb,
	__mbrtowc_euc,
	__wcrtomb_euc,
	__mbsrtowcs_euc,
	__wcsrtombs_euc,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0
};

static const _LC_charmap_t lc_cmap = {{
	{ _LC_CHARMAP, _LC_MAGIC, _LC_VERSION_MAJOR, _LC_VERSION_MINOR,
		sizeof (_LC_charmap_t) },
	__charmap_init,
	0,				/* charmap_destructor() */
	(_LC_methods_charmap_t *)&user_methods_charmap,
	(_LC_methods_charmap_t *)&native_methods_charmap,
	__eucpctowc_gen,
	__wctoeucpc_gen,
	(void *)0,
	},	/* End core */
	"ISO8859-1",
	_FC_EUC,
	_PC_EUC,
	1,	/* cm_mb_cur_max */
	1,	/* cm_mb_cur_min */
	0,	/* cm_reserved */
	1,	/* cm_def_width */
	255,	/* cm_base_max */
	0,	/* cm_tbl_ent */
	(_LC_euc_info_t *)&cm_eucinfo,	/* cm_eucinfo */
	NULL	/* cm_tbl */
};

/* ------------------------- CHARACTER CLASSES ------------------------- */
static const _LC_bind_table_t bindtab[] = {
	{ "alnum",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00004004 },
	{ "alpha",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00004000 },
	{ "blank",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00000040 },
	{ "cntrl",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00000020 },
	{ "digit",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00000004 },
	{ "graph",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00002000 },
	{ "lower",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00000002 },
	{ "print",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00008000 },
	{ "punct",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00000010 },
	{ "space",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00000008 },
	{ "tolower",	_LC_TAG_TRANS,	(_LC_bind_value_t)0x00000001 },
	{ "toupper",	_LC_TAG_TRANS,	(_LC_bind_value_t)0x00000002 },
	{ "upper",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00000001 },
	{ "xdigit",	_LC_TAG_CCLASS,	(_LC_bind_value_t)0x00000080 },
};

/* -------------------- toupper -------------------- */
static const wchar_t transformation_toupper[] = {
-1,	/* toupper[EOF] entry */
0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,   /* 0x0000 */
0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f,
0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,   /* 0x0010 */
0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e, 0x001f,
0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,   /* 0x0020 */
0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,   /* 0x0030 */
0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,   /* 0x0040 */
0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,   /* 0x0050 */
0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
0x0060, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,   /* 0x0060 */
0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,   /* 0x0070 */
0x0058, 0x0059, 0x005a, 0x007b, 0x007c, 0x007d, 0x007e, 0x007f,
0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,   /* 0x0080 */
0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,   /* 0x0090 */
0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7,   /* 0x00a0 */
0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7,   /* 0x00b0 */
0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf,
0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7,   /* 0x00c0 */
0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7,   /* 0x00d0 */
0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df,
0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7,   /* 0x00e0 */
0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00f7,   /* 0x00f0 */
0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00ff,
};

/* -------------------- tolower -------------------- */
static const wchar_t transformation_tolower[] = {
-1,	/* tolower[EOF] entry */
0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,   /* 0x0000 */
0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f,
0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,   /* 0x0010 */
0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e, 0x001f,
0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,   /* 0x0020 */
0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,   /* 0x0030 */
0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
0x0040, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,   /* 0x0040 */
0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,   /* 0x0050 */
0x0078, 0x0079, 0x007a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,   /* 0x0060 */
0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,   /* 0x0070 */
0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x007f,
0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,   /* 0x0080 */
0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,   /* 0x0090 */
0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7,   /* 0x00a0 */
0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7,   /* 0x00b0 */
0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf,
0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,   /* 0x00c0 */
0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x00d7,   /* 0x00d0 */
0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00df,
0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,   /* 0x00e0 */
0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x00f7,   /* 0x00f0 */
0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00ff,
};

/* ------------------------- CHAR CLASS MASKS ------------------------- */
static const unsigned int masks[] = {
0,	/* masks[EOF] entry */
0x00000020, 0x00000020, 0x00000020, 0x00000020,
0x00000020, 0x00000020, 0x00000020, 0x00000020,
0x00000020, 0x00000068, 0x00000028, 0x00000028,
0x00000028, 0x00000028, 0x00000020, 0x00000020,
0x00000020, 0x00000020, 0x00000020, 0x00000020,
0x00000020, 0x00000020, 0x00000020, 0x00000020,
0x00000020, 0x00000020, 0x00000020, 0x00000020,
0x00000020, 0x00000020, 0x00000020, 0x00000020,
0x00008048, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a084, 0x0000a084, 0x0000a084, 0x0000a084,
0x0000a084, 0x0000a084, 0x0000a084, 0x0000a084,
0x0000a084, 0x0000a084, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000e081, 0x0000e081, 0x0000e081,
0x0000e081, 0x0000e081, 0x0000e081, 0x0000e001,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000e001,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000e001,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000e001,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000e001,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000e082, 0x0000e082, 0x0000e082,
0x0000e082, 0x0000e082, 0x0000e082, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x00000020,
0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00008048, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000a010, 0x0000a010, 0x0000a010, 0x0000a010,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000e001,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000e001,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000e001,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000e001,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000e001,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000a010,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000e001,
0x0000e001, 0x0000e001, 0x0000e001, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000a010,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000e002,
0x0000e002, 0x0000e002, 0x0000e002, 0x0000e002,
};

/* -------------------- CTYPE COMPATIBILITY TABLE  -------------------- */

#define	SZ_CTYPE	(257 + 257)
#define	SZ_CODESET	7
#define	SZ_TOTAL	(SZ_CTYPE + SZ_CODESET)
/*
 * sizeof_compat_is_table = 255
 * sizeof_compat_upper_table = 255
 * sizeof_compat_lower_table = 255
 */
static const unsigned char ctype_compat_table[SZ_TOTAL] = { 0,
	_C,	_C,	_C,	_C,	_C,	_C,	_C,	_C,
	_C,	_S|_C,	_S|_C,	_S|_C,	_S|_C,	_S|_C,	_C,	_C,
	_C,	_C,	_C,	_C,	_C,	_C,	_C,	_C,
	_C,	_C,	_C,	_C,	_C,	_C,	_C,	_C,
	_S|_B,	_P,	_P,	_P,	_P,	_P,	_P,	_P,
	_P,	_P,	_P,	_P,	_P,	_P,	_P,	_P,
	_N|_X,	_N|_X,	_N|_X,	_N|_X,	_N|_X,	_N|_X,	_N|_X,	_N|_X,
	_N|_X,	_N|_X,	_P,	_P,	_P,	_P,	_P,	_P,
	_P,	_U|_X,	_U|_X,	_U|_X,	_U|_X,	_U|_X,	_U|_X,	_U,
	_U,	_U,	_U,	_U,	_U,	_U,	_U,	_U,
	_U,	_U,	_U,	_U,	_U,	_U,	_U,	_U,
	_U,	_U,	_U,	_P,	_P,	_P,	_P,	_P,
	_P,	_L|_X,	_L|_X,	_L|_X,	_L|_X,	_L|_X,	_L|_X,	_L,
	_L,	_L,	_L,	_L,	_L,	_L,	_L,	_L,
	_L,	_L,	_L,	_L,	_L,	_L,	_L,	_L,
	_L,	_L,	_L,	_P,	_P,	_P,	_P,	_C,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	_S|_B,	_P,	_P,	_P,	_P,	_P,	_P,	_P,
	_P,	_P,	_P,	_P,	_P,	_P,	_P,	_P,
	_P,	_P,	_P,	_P,	_P,	_P,	_P,	_P,
	_P,	_P,	_P,	_P,	_P,	_P,	_P,	_P,
	_U,	_U,	_U,	_U,	_U,	_U,	_U,	_U,
	_U,	_U,	_U,	_U,	_U,	_U,	_U,	_U,
	_U,	_U,	_U,	_U,	_U,	_U,	_U,	_P,
	_U,	_U,	_U,	_U,	_U,	_U,	_U,	_L,
	_L,	_L,	_L,	_L,	_L,	_L,	_L,	_L,
	_L,	_L,	_L,	_L,	_L,	_L,	_L,	_L,
	_L,	_L,	_L,	_L,	_L,	_L,	_L,	_P,
	_L,	_L,	_L,	_L,	_L,	_L,	_L,	_L,

	0,
	0x00,	0x01,	0x02,	0x03,	0x04,	0x05,	0x06,	0x07,
	0x08,	0x09,	0x0a,	0x0b,	0x0c,	0x0d,	0x0e,	0x0f,
	0x10,	0x11,	0x12,	0x13,	0x14,	0x15,	0x16,	0x17,
	0x18,	0x19,	0x1a,	0x1b,	0x1c,	0x1d,	0x1e,	0x1f,
	0x20,	0x21,	0x22,	0x23,	0x24,	0x25,	0x26,	0x27,
	0x28,	0x29,	0x2a,	0x2b,	0x2c,	0x2d,	0x2e,	0x2f,
	0x30,	0x31,	0x32,	0x33,	0x34,	0x35,	0x36,	0x37,
	0x38,	0x39,	0x3a,	0x3b,	0x3c,	0x3d,	0x3e,	0x3f,
	0x40,	0x61,	0x62,	0x63,	0x64,	0x65,	0x66,	0x67,
	0x68,	0x69,	0x6a,	0x6b,	0x6c,	0x6d,	0x6e,	0x6f,
	0x70,	0x71,	0x72,	0x73,	0x74,	0x75,	0x76,	0x77,
	0x78,	0x79,	0x7a,	0x5b,	0x5c,	0x5d,	0x5e,	0x5f,
	0x60,	0x41,	0x42,	0x43,	0x44,	0x45,	0x46,	0x47,
	0x48,	0x49,	0x4a,	0x4b,	0x4c,	0x4d,	0x4e,	0x4f,
	0x50,	0x51,	0x52,	0x53,	0x54,	0x55,	0x56,	0x57,
	0x58,	0x59,	0x5a,	0x7b,	0x7c,	0x7d,	0x7e,	0x7f,
	0x80,	0x81,	0x82,	0x83,	0x84,	0x85,	0x86,	0x87,
	0x88,	0x89,	0x8a,	0x8b,	0x8c,	0x8d,	0x8e,	0x8f,
	0x90,	0x91,	0x92,	0x93,	0x94,	0x95,	0x96,	0x97,
	0x98,	0x99,	0x9a,	0x9b,	0x9c,	0x9d,	0x9e,	0x9f,
	0xa0,	0xa1,	0xa2,	0xa3,	0xa4,	0xa5,	0xa6,	0xa7,
	0xa8,	0xa9,	0xaa,	0xab,	0xac,	0xad,	0xae,	0xaf,
	0xb0,	0xb1,	0xb2,	0xb3,	0xb4,	0xb5,	0xb6,	0xb7,
	0xb8,	0xb9,	0xba,	0xbb,	0xbc,	0xbd,	0xbe,	0xbf,
	0xe0,	0xe1,	0xe2,	0xe3,	0xe4,	0xe5,	0xe6,	0xe7,
	0xe8,	0xe9,	0xea,	0xeb,	0xec,	0xed,	0xee,	0xef,
	0xf0,	0xf1,	0xf2,	0xf3,	0xf4,	0xf5,	0xf6,	0xd7,
	0xf8,	0xf9,	0xfa,	0xfb,	0xfc,	0xfd,	0xfe,	0xdf,
	0xc0,	0xc1,	0xc2,	0xc3,	0xc4,	0xc5,	0xc6,	0xc7,
	0xc8,	0xc9,	0xca,	0xcb,	0xcc,	0xcd,	0xce,	0xcf,
	0xd0,	0xd1,	0xd2,	0xd3,	0xd4,	0xd5,	0xd6,	0xf7,
	0xd8,	0xd9,	0xda,	0xdb,	0xdc,	0xdd,	0xde,	0xff,

	/* multiple byte character width information */

	1,	0,	0,	1,	0,	0,	1
};

/* ---------------------   TRANSFORMATION TABLES   -------------------- */
static const _LC_transnm_t transname[] = {
	{ NULL,	0,	0,	0 },
	{ "toupper",	1,	0x00000000,	0x000000ff },
	{ "tolower",	2,	0x00000000,	0x000000ff },
};

static const _LC_transtabs_t transtabs[] = {
	{ NULL, 0, 0, NULL },
	{
		&transformation_toupper[1],
		0x00000000, 	0x000000ff,
		NULL
	},
	{
		&transformation_tolower[1],
		0x00000000, 	0x000000ff,
		NULL
	},
};

/* -------------------------   CTYPE OBJECT   ------------------------- */
static const _LC_methods_ctype_t native_methods_ctype = {
	(short)0,
	(short)0,
	__wctype_std,
	__iswctype_sb,
	__towupper_std,
	__towlower_std,
	__trwctype_std,
	__wctrans_std,
	__towctrans_std,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0
};

static const _LC_methods_ctype_t user_methods_ctype = {
	(short)0,
	(short)0,
	__wctype_std,
	__iswctype_bc,
	__towupper_bc,
	__towlower_bc,
	__trwctype_std,
	__wctrans_std,
	__towctrans_bc,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0
};

static const _LC_ctype_t lc_ctype = {{
	{ _LC_CTYPE, _LC_MAGIC, _LC_VERSION_MAJOR, _LC_VERSION_MINOR,
		sizeof (_LC_locale_t) },
	__ctype_init,
	0,				/* ctype_destructor() */
	(_LC_methods_ctype_t *)&user_methods_ctype,
	(_LC_methods_ctype_t *)&native_methods_ctype,
	(void *)0,
	},	/* End core */
	(_LC_charmap_t *)&lc_cmap,
	0,
	255,
	255,
	255,
	&transformation_toupper[1],
	&transformation_tolower[1],
	&masks[1],
	(void *)0,
	(void *)0,
	0,
	14,
	(_LC_bind_table_t *)bindtab,
	/*  transformations */
	2,
	(_LC_transnm_t *)transname,
	transtabs,
	SZ_TOTAL,
	ctype_compat_table,
	{
	(void *)0,	/* reserved for future use */
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	}
};

/* --------------------------- LOCALE DEFS -------------------------- */
typedef struct {
	_LC_core_locale_t	core;

	struct lconv	*nl_lconv;

	_LC_charmap_t	*lc_charmap;
	_LC_collate_t	*lc_collate;
	_LC_ctype_t	*lc_ctype;
	_LC_monetary_t	*lc_monetary;
	_LC_numeric_t	*lc_numeric;
	_LC_messages_t	*lc_messages;
	_LC_time_t	*lc_time;

	int	no_of_items;
	const char	*nl_info[_NL_NUM_ITEMS];
} _lc_locale_t;

/* -------------------------- LOCALE OBJECT -------------------------- */
static const _LC_methods_locale_t native_methods_locale = {
	(short)0,
	(short)0,
	__nl_langinfo_std,
	__localeconv_std,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0
};

static const _lc_locale_t lc_loc = {{
	{ _LC_LOCALE, _LC_MAGIC, _LC_VERSION_MAJOR, _LC_VERSION_MINOR,
		sizeof (_LC_locale_t) },
	__locale_init,
	0,				/* locale_destructor() */
	(_LC_methods_locale_t *)&native_methods_locale,
	(_LC_methods_locale_t *)&native_methods_locale,
	(void *)0,
	},	/* End core */
	(void *)0,
	(_LC_charmap_t *)&lc_cmap,
	(void *)0,
	(_LC_ctype_t *)&lc_ctype,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	59,
	{"",			/* not used */
	/* DAY_1 - DAY_7 */
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	/* ABDAY_1 - ABDAY_7 */
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	/* MON_1 - MON_12 */
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	/* ABMON_1 - ABMON_12 */
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	(void *)0,
	/* RADIXCHAR */
	(void *)0,
	/* THOUSEP */
	(void *)0,
	/* YESSTR */
	(void *)0,
	/* NOSTR */
	(void *)0,
	/* CRNCYSTR */
	(void *)0,
	/* D_T_FMT */
	(void *)0,
	/* D_FMT */
	(void *)0,
	/* T_FMT */
	(void *)0,
	/* AM_STR/PM_STR */
	(void *)0,
	(void *)0,
	/* CODESET */
	"ISO8859-1",
	/* T_FMT_AMPM */
	(void *)0,
	/* ERA */
	(void *)0,
	/* ERA_D_FMT */
	(void *)0,
	/* ERA_D_T_FMT */
	(void *)0,
	/* ERA_T_FMT */
	(void *)0,
	/* ALTDIGITS */
	(void *)0,
	/* YESEXPR */
	(void *)0,
	/* NOEXPR */
	(void *)0,
	/* _DATE_FMT */
	(void *)0,
	},
};

_LC_locale_t *
instantiate(void)
{
	return ((_LC_locale_t *)&lc_loc);
}

/*
 * Copyright (c) 1996, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * COMPONENT_NAME: (LIBCLOC) Locale
 *
 * ORIGINS: 27
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1991, 1992
 * All Rights Reserved
 * Licensed Materials - Property of IBM
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

/*
 * New members added in the struct lconv by IEEE Std 1003.1-2001
 * are always activated in the locale object.
 * See <iso/locale_iso.h>.
 */
#ifndef _LCONV_C99
#define	_LCONV_C99
#endif

#include "lint.h"
#include "file64.h"
#include <sys/types.h>
#include <limits.h>
#include <sys/localedef.h>
#include <locale.h>
#include <langinfo.h>
#include "_locale.h"
#include <wctype.h>
#include <string.h>
#include "mse.h"
#include "libc_i18n.h"

/* ------------------------------EUC INFO----------------------------- */
static const _LC_euc_info_t	__C_cm_eucinfo = {
	(char)1,		/* eucw0 */
	(char)0,		/* eucw1 */
	(char)0,		/* eucw2 */
	(char)0,		/* eucw3 */
	(char)1,		/* scrw0 */
	(char)0,		/* scrw1 */
	(char)0,		/* scrw2 */
	(char)0,		/* scrw3 */
	0,				/* CS1 dense code base */
	0,				/* CS2 dense code base */
	0,				/* CS3 dense code base */
	255,			/* dense code last value */
	0,				/* CS1 adjustment value */
	0,				/* CS2 adjustment value */
	0,				/* CS3 adjustment value */
};

/* ------------------------- CHARMAP OBJECT  ------------------------- */
static const _LC_methods_charmap_t	__C_charmap_methods_object = {
	(short)0,
	(short)0,
	(char *(*) (_LC_charmap_t *, nl_item)) 0,
	__mbtowc_sb,
	__mbstowcs_sb,
	__wctomb_sb,
	__wcstombs_sb,
	__mblen_sb,
	__wcswidth_C,
	__wcwidth_C,
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

const _LC_charmap_t	__C_charmap_object = {
	{
		{
			_LC_CHARMAP,
			_LC_MAGIC,
			_LC_VERSION_MAJOR,
			_LC_VERSION_MINOR,
			sizeof (_LC_charmap_t)
		},
		__charmap_init,		/* init */
		__charmap_destructor,	/* destructor */
		(_LC_methods_charmap_t *)
			&__C_charmap_methods_object,	/* user_api */
		(_LC_methods_charmap_t *)
			&__C_charmap_methods_object,	/* native_api */
		/* __eucpctowc */
		(wchar_t (*)(_LC_charmap_t *, wchar_t))0,
		/* __wctoeucpc */
		(wchar_t (*)(_LC_charmap_t *, wchar_t))0,
		(void *) 0,			/* data */
	},						/* End core */
	"646",				/* codeset name */
	_FC_OTHER,			/* file code */
	_PC_DENSE,			/* process code */
	1,				/* max encoding length */
	1,				/* min encoding length */
	0,				/* reserved */
	1,				/* default width */
	0,				/* base max */
	0,				/* # of extended table entries */
	(_LC_euc_info_t *)&__C_cm_eucinfo,	/* euc info table */
	NULL,				/* width table */
};

/* ------------------------- CHARACTER CLASSES ------------------------- */
static const _LC_bind_table_t	_C_bindtab[] = {
	{ "alnum", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISALNUM },
	{ "alpha", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISALPHA },
	{ "blank", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISBLANK },
	{ "cntrl", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISCNTRL },
	{ "digit", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISDIGIT },
	{ "graph", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISGRAPH },
	{ "lower", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISLOWER },
	{ "print", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISPRINT },
	{ "punct", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISPUNCT },
	{ "space", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISSPACE },
	{ "tolower",	_LC_TAG_TRANS,	(_LC_bind_value_t)0x00000001 },
	{ "toupper",	_LC_TAG_TRANS,	(_LC_bind_value_t)0x00000002 },
	{ "upper", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISUPPER },
	{ "xdigit", _LC_TAG_CCLASS, (_LC_bind_value_t)_ISXDIGIT },
};

/* ------------------------- UPPER XLATE ------------------------- */
static const wchar_t _C_upper[] = {
	-1,			/* toupper[EOF] entry */
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
	0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f,
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
	0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e, 0x001f,
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
	0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
	0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
	0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
	0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
	0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
	0x0060, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
	0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
	0x0058, 0x0059, 0x005a, 0x007b, 0x007c, 0x007d, 0x007e, 0x007f,
	0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
	0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
	0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
	0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
	0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7,
	0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
	0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7,
	0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf,
	0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7,
	0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
	0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7,
	0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df,
	0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,
	0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
	0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x00f7,
	0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00ff
};

/* ------------------------- LOWER XLATE ------------------------- */
static const wchar_t _C_lower[] = {
	-1,			/* tolower[EOF] entry */
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
	0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f,
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
	0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e, 0x001f,
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
	0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
	0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
	0x0040, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
	0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
	0x0078, 0x0079, 0x007a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
	0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
	0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
	0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x007f,
	0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
	0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
	0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
	0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
	0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7,
	0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
	0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7,
	0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf,
	0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7,
	0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
	0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7,
	0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df,
	0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,
	0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
	0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x00f7,
	0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00ff
};

/* ------------------------- CHAR CLASS MASKS ------------------------- */
#define	_IC _ISCNTRL
#define	_IB (_ISBLANK|_ISPRINT|_ISSPACE)
#define	_IT (_ISBLANK|_ISSPACE)
#define	_IV _ISSPACE
#define	_IP (_ISPUNCT|_ISPRINT|_ISGRAPH)
#define	_IX _ISXDIGIT
#define	_IU (_ISUPPER|_ISALPHA|_ISPRINT|_ISGRAPH)
#define	_IL (_ISLOWER|_ISALPHA|_ISPRINT|_ISGRAPH)
#define	_IG (_ISGRAPH|_ISPRINT)
#define	_IN (_ISDIGIT|_ISPRINT|_ISGRAPH)

static const unsigned int	_C_masks[] = {
0,		/* masks[EOF] entry */
_IC,		_IC,		_IC,		_IC,
_IC,		_IC,		_IC,		_IC,
_IC,		_IT|_IC,	_IV|_IC,	_IV|_IC,
_IV|_IC,	_IV|_IC,	_IC,		_IC,
_IC,		_IC,		_IC,		_IC,
_IC,		_IC,		_IC,		_IC,
_IC,		_IC,		_IC,		_IC,
_IC,		_IC,		_IC,		_IC,
_IB,		_IP,		_IP,		_IP,
_IP,		_IP,		_IP,		_IP,
_IP,		_IP,		_IP,		_IP,
_IP,		_IP,		_IP,		_IP,
_IN|_IX,	_IN|_IX,	_IN|_IX,	_IN|_IX,
_IN|_IX,	_IN|_IX,	_IN|_IX,	_IN|_IX,
_IN|_IX,	_IN|_IX,	_IP,		_IP,
_IP,		_IP,		_IP,		_IP,
_IP,		_IU|_IX,	_IU|_IX,	_IU|_IX,
_IU|_IX,	_IU|_IX,	_IU|_IX,	_IU,
_IU,		_IU,		_IU,		_IU,
_IU,		_IU,		_IU,		_IU,
_IU,		_IU,		_IU,		_IU,
_IU,		_IU,		_IU,		_IU,
_IU,		_IU,		_IU,		_IP,
_IP,		_IP,		_IP,		_IP,
_IP,		_IL|_IX,	_IL|_IX,	_IL|_IX,
_IL|_IX,	_IL|_IX,	_IL|_IX,	_IL,
_IL,		_IL,		_IL,		_IL,
_IL,		_IL,		_IL,		_IL,
_IL,		_IL,		_IL,		_IL,
_IL,		_IL,		_IL,		_IL,
_IL,		_IL,		_IL,		_IP,
_IP,		_IP,		_IP,		_IC,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0,
0,		0,		0,		0
};


/* -------------------- CTYPE COMPATIBILITY TABLE  -------------------- */

static const unsigned char	_C_ctype[SZ_TOTAL] = {
	0, /* EOF */
	_C,		_C,		_C,		_C,
	_C,		_C,		_C,		_C,
	_C,		_S|_C,	_S|_C,	_S|_C,
	_S|_C,	_S|_C,	_C,		_C,
	_C,		_C,		_C,		_C,
	_C,		_C,		_C,		_C,
	_C,		_C,		_C,		_C,
	_C,		_C,		_C,		_C,
	_S|_B,	_P,		_P,		_P,
	_P,		_P,		_P,		_P,
	_P,		_P,		_P,		_P,
	_P,		_P,		_P,		_P,
	_N|_X,	_N|_X,	_N|_X,	_N|_X,
	_N|_X,	_N|_X,	_N|_X,	_N|_X,
	_N|_X,	_N|_X,	_P,		_P,
	_P,		_P,		_P,		_P,
	_P,		_U|_X,	_U|_X,	_U|_X,
	_U|_X,	_U|_X,	_U|_X,	_U,
	_U,		_U,		_U,		_U,
	_U,		_U,		_U,		_U,
	_U,		_U,		_U,		_U,
	_U,		_U,		_U,		_U,
	_U,		_U,		_U,		_P,
	_P,		_P,		_P,		_P,
	_P,		_L|_X,	_L|_X,	_L|_X,
	_L|_X,	_L|_X,	_L|_X,	_L,
	_L,		_L,		_L,		_L,
	_L,		_L,		_L,		_L,
	_L,		_L,		_L,		_L,
	_L,		_L,		_L,		_L,
	_L,		_L,		_L,		_P,
	_P,		_P,		_P,		_C,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,

/* tolower()  and toupper() conversion table */ 0,
	0,		1,		2,		3,
	4,		5,		6,		7,
	8,		9,		10,		11,
	12,		13,		14,		15,
	16,		17,		18,		19,
	20,		21,		22,		23,
	24,		25,		26,		27,
	28,		29,		30,		31,
	32,		33,		34,		35,
	36,		37,		38,		39,
	40,		41,		42,		43,
	44,		45,		46,		47,
	48,		49,		50,		51,
	52,		53,		54,		55,
	56,		57,		58,		59,
	60,		61,		62,		63,
	64,		97,		98,		99,
	100,	101,	102,	103,
	104,	105,	106,	107,
	108,	109,	110,	111,
	112,	113,	114,	115,
	116,	117,	118,	119,
	120,	121,	122,	91,
	92,		93,		94,		95,
	96,		65,		66,		67,
	68,		69,		70,		71,
	72,		73,		74,		75,
	76,		77,		78,		79,
	80,		81,		82,		83,
	84,		85,		86,		87,
	88,		89,		90,		123,
	124,	125,	126,	127,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
	0,	0,	0,	0,
/* CSWIDTH information */
	1,	0,	0,	1,	0,	0,	1
};

/* ---------------------   TRANSFORMATION TABLES   -------------------- */
static const _LC_transnm_t	_C_transname[] = {
	{ NULL,		0,	0,	0},
	{ "toupper",	1,	0,	255},
	{ "tolower",	2,	0,	255},
};

static const _LC_transtabs_t	_C_transtabs[] = {
	{ NULL, 0, 0, NULL},
	{ &_C_upper[1], 0, 255, NULL},
	{ &_C_lower[1], 0, 255, NULL}
};

/* -------------------------   CTYPE OBJECT   ------------------------- */
static const _LC_methods_ctype_t	__C_ctype_methods_object = {
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

const _LC_ctype_t	__C_ctype_object = {
	{
		{
			_LC_CTYPE,
			_LC_MAGIC,
			_LC_VERSION_MAJOR,
			_LC_VERSION_MINOR,
			sizeof (_LC_ctype_t)
		},
		__ctype_init,			/* init */
		__ctype_destructor,		/* destructor */
		(_LC_methods_ctype_t *)
			&__C_ctype_methods_object, /* user_api */
		(_LC_methods_ctype_t *)
			&__C_ctype_methods_object, /* native_api */
		(void *)0,					/* data */
	},				/* End core */
	(_LC_charmap_t *)
		&__C_charmap_object,	/* pointer to charmap object */
	0,			/* min process code */
	255,		/* max process code */
	122,		/* last char with upper-case equiv */
	90,			/* last char with lower-case equiv */
	&_C_upper[1],	/* upper translation table */
	&_C_lower[1],	/* lower translation table */
	&_C_masks[1],	/* array of masks for CPs <= 25 */
	(const unsigned int *)0,	/* array of masks for CPs > 255 */
	(const unsigned char *)0,	/* index into qmask for CPs > 255 */
	0,			/* last code-point with unique qidx value */
	14,			/* number of lcbind entries */
	(_LC_bind_table_t *)_C_bindtab,	/* pointer to lcbind table */
	2,			/* number of transtab array elements */
	(_LC_transnm_t *)
		_C_transname,	/* pointer to trans name table */
	_C_transtabs,	/* pointer to transtabs array */
	SZ_TOTAL,		/* size of _ctype[] */
	(unsigned char *)_C_ctype,		/* pointer to _ctype[] */
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

/* ------------------------- COLLATE OBJECT  ------------------------- */
static const _LC_methods_collate_t __C_collate_methods_object = {
	(short)0,
	(short)0,
	__strcoll_C,
	__strxfrm_C,
	__wcscoll_C,
	__wcsxfrm_C,
	__fnmatch_C,
	__regcomp_C,
	__regerror_std,
	__regexec_C,
	__regfree_std,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0
};

static const wchar_t	sort[] = {
	_COLL_FORWARD_MASK | 0 | 0 | 0 | 0,
};

const _LC_collate_t	__C_collate_object = {
	{
		{
			_LC_COLLATE,
			_LC_MAGIC,
			_LC_VERSION_MAJOR,
			_LC_VERSION_MINOR,
			sizeof (_LC_collate_t)
		},
		__collate_init,			/* init */
		__collate_destructor,	/* destructor */
		(_LC_methods_collate_t *)
			&__C_collate_methods_object, /* user_api */
		(_LC_methods_collate_t *)
			&__C_collate_methods_object, /* native_api */
		(void *)0,					/* data */
	},							/* End core */
	(_LC_charmap_t *)
		&__C_charmap_object,	/* pointer to charmap object */
	0,			/* number of supported collation order */
	0,			/* relative order */
	1,			/* ext */
	{ 0 },			/* padding */
	(_LC_weight_t)sort,	/* co_sort */
	0,			/* min process code */
	255,		/* max process code */
	255,		/* max process code with unique info */
	0x1010101,	/* min coll weight */
	0x1010182,	/* max coll weight */
	(const _LC_weight_t *)
		NULL,	/* weight table */
	(const _LC_collel_t **)
		NULL,		/* collating elements table */
	0,			/* number of sub strs */
	(void *)0,	/* substitution strs */
	NULL		/* extinfo */
};

/* ------------------------- MONETARY OBJECT  ------------------------- */
static const _LC_methods_monetary_t	__C_monetary_methods_object = {
	(short)0,
	(short)0,
	(char *(*) (_LC_monetary_t *, nl_item)) 0,
	__strfmon_std,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0
};

const _LC_monetary_t	__C_monetary_object = {
	{
		{
			_LC_MONETARY,
			_LC_MAGIC,
			_LC_VERSION_MAJOR,
			_LC_VERSION_MINOR,
			sizeof (_LC_monetary_t)
		},
		__monetary_init,		/* init */
		__monetary_destructor,	/* destructor */
		(_LC_methods_monetary_t *)
			&__C_monetary_methods_object, /* user_api */
		(_LC_methods_monetary_t *)
			&__C_monetary_methods_object, /* native_api */
		(void *)0,					/* data */
	},							/* End core */
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX
};

/* ------------------------- NUMERIC OBJECT  ------------------------- */
static const _LC_methods_numeric_t	__C_numeric_methods_object = {
	(short)0,
	(short)0,
	(char *(*) (_LC_numeric_t *, nl_item)) 0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0
};

const _LC_numeric_t __C_numeric_object = {
	{
		{
			_LC_NUMERIC,
			_LC_MAGIC,
			_LC_VERSION_MAJOR,
			_LC_VERSION_MINOR,
			sizeof (_LC_numeric_t)
		},
		__numeric_init,			/* init */
		__numeric_destructor,	/* destructor */
		(_LC_methods_numeric_t *)
			&__C_numeric_methods_object, /* user_api */
		(_LC_methods_numeric_t *)
			&__C_numeric_methods_object, /* native_api */
		(void *)0,					/* data */
	},							/* End core */
	".",
	"",
	"",
};

/* -------------------------   TIME OBJECT   ------------------------- */
static const _LC_methods_time_t	__C_time_methods_object = {
	(short)0,
	(short)0,
	(char *(*) (_LC_time_t *, nl_item)) 0,
	__strftime_std,
	__strptime_std,
	__getdate_std,
	__wcsftime_std,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0
};

static const char	*era_strings[] = {
	(char *)0
};

const _LC_time_t __C_time_object = {
	{
		{
			_LC_TIME,
			_LC_MAGIC,
			_LC_VERSION_MAJOR,
			_LC_VERSION_MINOR,
			sizeof (_LC_time_t)
		},
		__time_init,			/* init */
		__time_destructor,		/* destructor */
		(_LC_methods_time_t *)
			&__C_time_methods_object, /* user_api */
		(_LC_methods_time_t *)
			&__C_time_methods_object, /* native_api */
		(void *)0,					/* data */
	},							/* End core */
	"%m/%d/%y",					/* d_fmt */
	"%H:%M:%S",					/* t_fmt */
	"%a %b %d %H:%M:%S %Y",		/* d_t_fmt */
	"%I:%M:%S %p",				/* t_fmt_ampm */
	{							/* abday[7] */
		"Sun",
		"Mon",
		"Tue",
		"Wed",
		"Thu",
		"Fri",
		"Sat",
	},
	{							/* day[7] */
		"Sunday",
		"Monday",
		"Tuesday",
		"Wednesday",
		"Thursday",
		"Friday",
		"Saturday",
	},
	{							/* abmon[12] */
		"Jan",
		"Feb",
		"Mar",
		"Apr",
		"May",
		"Jun",
		"Jul",
		"Aug",
		"Sep",
		"Oct",
		"Nov",
		"Dec",
	},
	{							/* mon[12] */
		"January",
		"February",
		"March",
		"April",
		"May",
		"June",
		"July",
		"August",
		"September",
		"October",
		"November",
		"December",
	},
	{							/* am_pm[2] */
		"AM",
		"PM",
	},
	era_strings,		/* era */
	"%x",						/* era_d_fmt */
	"",							/* alt_digits */
	"%c",				/* era_d_t_fmt */
	"%X",						/* era_t_fmt */
	"%a %b %e %T %Z %Y",		/* date_fmt */
};

/* ------------------------- MESSAGE OBJECT  ------------------------- */
static const _LC_methods_messages_t	__C_messages_methods_object = {
	(short)0,
	(short)0,
	(char *(*) (_LC_messages_t *, nl_item)) 0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0,
	(_LC_methods_func_t)0
};

const _LC_messages_t __C_messages_object = {
	{
		{
			_LC_MESSAGES,
			_LC_MAGIC,
			_LC_VERSION_MAJOR,
			_LC_VERSION_MINOR,
			sizeof (_LC_messages_t)
		},
		__messages_init,		/* init */
		__messages_destructor,	/* destructor */
		(_LC_methods_messages_t *)
			&__C_messages_methods_object, /* user_api */
		(_LC_methods_messages_t *)
			&__C_messages_methods_object, /* native_api */
		(void *)0,					/* data */
	},							/* End core */
	"^[yY]",
	"^[nN]",
	"yes",
	"no",
};

/* -------------------------- LCONV STRUCT --------------------------- */
static const struct lconv	__C_lconv = {
	".",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX
};

/* -------------------------- LOCALE OBJECT -------------------------- */
/*
 * The following pseudo structure definition is to move data for
 * nl_info[] to the read-only section.  The nl_info[] entry defined in
 * actual _LC_locale_t structure has (char *) type because the entry
 * will directly become a return value from nl_langinfo() that returns
 * (char *).  Since the nl_info[] data can be read-only, the following
 * _lc_locale_t structure has (const char *) type for nl_info[].
 */
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

	/* to be used for specifying the size of nl_info */
	int	no_of_items;
	const char	*nl_info[_NL_NUM_ITEMS];
} _lc_locale_t;

static const _LC_methods_locale_t	__C_locale_methods_object = {
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

const _lc_locale_t __C_locale_object = {
	{
		{
			_LC_LOCALE,
			_LC_MAGIC,
			_LC_VERSION_MAJOR,
			_LC_VERSION_MINOR,
			sizeof (_LC_locale_t)
		},
		__locale_init,			/* init */
		__locale_destructor,	/* destructor */
		(_LC_methods_locale_t *)
			&__C_locale_methods_object, /* user_api */
		(_LC_methods_locale_t *)
			&__C_locale_methods_object, /* native_api */
		(void *)0,				/* data */
	},							/* End core */
	(struct lconv *)
		&__C_lconv,			/* nl_lconv */
	(_LC_charmap_t *)
		&__C_charmap_object,		/* pointer to charmap */
	(_LC_collate_t *)
		&__C_collate_object,		/* pointer to collate */
	(_LC_ctype_t *)
		&__C_ctype_object,			/* pointer to ctype */
	(_LC_monetary_t *)
		&__C_monetary_object,		/* pointer to monetary */
	(_LC_numeric_t *)
		&__C_numeric_object,		/* pointer to numeric */
	(_LC_messages_t *)
		&__C_messages_object,		/* pointer to messages */
	(_LC_time_t *)
		&__C_time_object,			/* pointer to time */
	_NL_NUM_ITEMS,				/* So far, we use this macro */
	{
		"",						/* not used */
		"Sunday",				/* DAY_1 */
		"Monday",				/* DAY_2 */
		"Tuesday",				/* DAY_3 */
		"Wednesday",			/* DAY_4 */
		"Thursday",				/* DAY_5 */
		"Friday",				/* DAY_6 */
		"Saturday",				/* DAY_7 */
		"Sun",					/* ABDAY_1 */
		"Mon",					/* ABDAY_2 */
		"Tue",					/* ABDAY_3 */
		"Wed",					/* ABDAY_4 */
		"Thu",					/* ABDAY_5 */
		"Fri",					/* ABDAY_6 */
		"Sat",					/* ABDAY_7 */
		"January",				/* MON_1 */
		"February",				/* MON_2 */
		"March",				/* MON_3 */
		"April",				/* MON_4 */
		"May",					/* MON_5 */
		"June",					/* MON_6 */
		"July",					/* MON_7 */
		"August",				/* MON_8 */
		"September",			/* MON_9 */
		"October",				/* MON_10 */
		"November",				/* MON_11 */
		"December",				/* MON_12 */
		"Jan",					/* ABMON_1 */
		"Feb",					/* ABMON_2 */
		"Mar",					/* ABMON_3 */
		"Apr",					/* ABMON_4 */
		"May",					/* ABMON_5 */
		"Jun",					/* ABMON_6 */
		"Jul",					/* ABMON_7 */
		"Aug",					/* ABMON_8 */
		"Sep",					/* ABMON_9 */
		"Oct",					/* ABMON_10 */
		"Nov",					/* ABMON_11 */
		"Dec",					/* ABMON_12 */
		".",					/* RADIXCHAR */
		"",						/* THOUSEP */
		"yes",					/* YESSTR */
		"no",					/* NOSTR */
		"",						/* CRNCYSTR */
		"%a %b %d %H:%M:%S %Y",	/* D_T_FMT */
		"%m/%d/%y",				/* D_FMT */
		"%H:%M:%S",				/* T_FMT */
		"AM",					/* AM_STR */
		"PM",					/* PM_STR */
		"646",					/* CODESET */
		"%I:%M:%S %p",			/* T_FMT_AMPM */
		"",						/* ERA */
		"%x",					/* ERA_D_FMT */
		"%c",					/* ERA_D_T_FMT */
		"%X",					/* ERA_T_FMT */
		"",						/* ALT_DIGITS */
		"^[yY]",				/* YESEXPR */
		"^[nN]",				/* NOEXPR */
		"%a %b %e %T %Z %Y",	/* _DATE_FMT */
	},
};

/* -------------------- GLOBAL VARIABLES -------------------- */
_LC_locale_t	*__C_locale = (_LC_locale_t *)&__C_locale_object;
_LC_charmap_t	*__lc_charmap = (_LC_charmap_t *)&__C_charmap_object;
_LC_ctype_t	*__lc_ctype = (_LC_ctype_t *)&__C_ctype_object;
_LC_collate_t	*__lc_collate = (_LC_collate_t *)&__C_collate_object;
_LC_numeric_t	*__lc_numeric = (_LC_numeric_t *)&__C_numeric_object;
_LC_monetary_t	*__lc_monetary = (_LC_monetary_t *)&__C_monetary_object;
_LC_time_t	*__lc_time = (_LC_time_t *)&__C_time_object;
_LC_messages_t	*__lc_messages = (_LC_messages_t *)&__C_messages_object;
_LC_locale_t	*__lc_locale = (_LC_locale_t *)&__C_locale_object;

unsigned int	*__ctype_mask = (unsigned int *)_C_masks + 1;
int	*__trans_upper = (int *)_C_upper + 1;
int	*__trans_lower = (int *)_C_lower + 1;
static int *__ascii_upper = (int *)_C_upper + 1;
static int *__ascii_lower = (int *)_C_lower + 1;
int	charset_is_ascii = 1;
/* -------------------- GLOBAL VARIABLES -------------------- */

_LC_collate_t *
__collate_init(_LC_locale_t *loc)
{
	/* If C locale, always succeeds. */
	if (loc->lc_collate == &__C_collate_object) {
		return (NULL);
	}

	/* This is a temporary fix. */
	if (loc->lc_collate->co_coltbl) {
		return (NULL);
	} else {
		return ((_LC_collate_t *)-1);
	}
}

_LC_ctype_t	*
__ctype_init(_LC_locale_t *loc)
{
	_LC_ctype_t   *ctype;

	ctype = loc->lc_ctype;
	if (ctype->ctypedata) {
		/* assumes that ctype->ctypesize is smaller than or */
		/* equal to SZ_TOTAL.  No checking. */
		(void) memcpy(_ctype, ctype->ctypedata, ctype->ctypesize);
	} else {
		return ((_LC_ctype_t *)-1);
	}
	__ctype_mask = (unsigned int *)ctype->mask;
	__trans_upper = (int *)ctype->upper;
	__trans_lower = (int *)ctype->lower;
	charset_is_ascii =	/* optimization flag for str[n]casecmp() */
	    ((__trans_upper == __ascii_upper &&
	    __trans_lower == __ascii_lower) ||
	    (memcmp(__trans_upper, __ascii_upper, 256 * sizeof (int)) == 0 &&
	    memcmp(__trans_lower, __ascii_lower, 256 * sizeof (int)) == 0));

	return (NULL);
}

_LC_monetary_t *
__monetary_init(_LC_locale_t *loc)
{
	_LC_monetary_t	*mon;
	char	**nl_info;
	struct lconv	*nl_lconv;

	nl_info = loc->nl_info;
	nl_lconv = loc->nl_lconv;
	mon = loc->lc_monetary;

	/* set nl_langinfo() information */
	nl_info[CRNCYSTR] = (char *)mon->currency_symbol;

	/*
	 * setup localeconv() structure
	 *
	 * Copying the LC_MONETARY info in mon to nl_lconv.
	 * Order of the members in mon should be the same
	 * as that in nl_lconv.
	 */
	(void) memcpy(&nl_lconv->int_curr_symbol,
	    &mon->int_curr_symbol,
	    sizeof (struct lconv) - offsetof(struct lconv, int_curr_symbol));

	return (NULL);
}


_LC_charmap_t *
__charmap_init(_LC_locale_t *loc)
{
	loc->nl_info[CODESET] = (char *)loc->lc_charmap->cm_csname;

	/* clear internal mbstate_t buffers */
	_clear_internal_mbstate();

	return (NULL);
}


_LC_messages_t *
__messages_init(_LC_locale_t *loc)
{
	_LC_messages_t	*messages = loc->lc_messages;

	loc->nl_info[YESEXPR] = (char *)messages->yesexpr;
	loc->nl_info[NOEXPR] = (char *)messages->noexpr;
	loc->nl_info[YESSTR] = (char *)messages->yesstr;
	loc->nl_info[NOSTR] = (char *)messages->nostr;
	return (NULL);
}


_LC_numeric_t *
__numeric_init(_LC_locale_t *loc)
{
	_LC_numeric_t	*num;
	char	**nl_info;
	struct lconv	*nl_lconv;

	nl_info = loc->nl_info;
	nl_lconv = loc->nl_lconv;
	num = loc->lc_numeric;

	nl_info[RADIXCHAR] = (char *)num->decimal_point;
	nl_info[THOUSEP] = (char *)num->thousands_sep;

	_numeric[0] = *num->decimal_point;
	_numeric[1] = *num->thousands_sep;

	/* setup localeconv() lconv structure */
	nl_lconv->decimal_point	= (char *)num->decimal_point;
	nl_lconv->thousands_sep	= (char *)num->thousands_sep;
	nl_lconv->grouping	= (char *)num->grouping;
	return (NULL);
}

_LC_time_t *
__time_init(_LC_locale_t *loc)
{
	_LC_time_t	*time;
	char	**nl_info;

	nl_info = loc->nl_info;
	time    = loc->lc_time;

	/* set nl_langinfo() information */
	nl_info[D_FMT]	= (char *)time->d_fmt;
	nl_info[T_FMT]	= (char *)time->t_fmt;
	nl_info[D_T_FMT]	= (char *)time->d_t_fmt;
	nl_info[AM_STR]	= (char *)time->am_pm[0];
	nl_info[PM_STR]	= (char *)time->am_pm[1];
	nl_info[T_FMT_AMPM]	= (char *)time->t_fmt_ampm;
	nl_info[ERA]	= (char *)(*time->era);
	nl_info[ERA_D_FMT]	= (char *)time->era_d_fmt;
	nl_info[ERA_T_FMT]	= (char *)time->era_t_fmt;
	nl_info[ERA_D_T_FMT]	= (char *)time->era_d_t_fmt;
	nl_info[ALT_DIGITS]	= (char *)time->alt_digits;
	nl_info[_DATE_FMT]	= (char *)time->date_fmt;

	/* copy abbreviate day name pointers ABDAY_x */
	(void) memcpy(&(nl_info[ABDAY_1]), &(time->abday[0]),
	    7 * sizeof (char *));

	/* copy day name pointers DAY_x */
	(void) memcpy(&(nl_info[DAY_1]), &(time->day[0]),
	    7 * sizeof (char *));

	/* copy abbreviated month name pointers ABMON_x */
	(void) memcpy(&(nl_info[ABMON_1]), &(time->abmon[0]),
	    12 * sizeof (char *));

	/* copy month name pointers MON_x */
	(void) memcpy(&(nl_info[MON_1]), &(time->mon[0]),
	    12 * sizeof (char *));

	return (NULL);
}

_LC_locale_t *
__locale_init(_LC_locale_t *loc)
{
	if (loc->lc_charmap) {
		if ((*(loc->lc_charmap->core.init))(loc) ==
		    (_LC_charmap_t *)-1) {
			return ((_LC_locale_t *)-1);
		}
	} else {
		return ((_LC_locale_t *)-1);
	}

	if (loc->lc_collate) {
		if ((*(loc->lc_collate->core.init))(loc) ==
		    (_LC_collate_t *)-1) {
			return ((_LC_locale_t *)-1);
		}
	} else {
		return ((_LC_locale_t *)-1);
	}

	if (loc->lc_ctype) {
		if ((*(loc->lc_ctype->core.init))(loc) ==
		    (_LC_ctype_t *)-1) {
			return ((_LC_locale_t *)-1);
		}
	} else {
		return ((_LC_locale_t *)-1);
	}

	if (loc->lc_monetary) {
		if ((*(loc->lc_monetary->core.init))(loc) ==
		    (_LC_monetary_t *)-1) {
			return ((_LC_locale_t *)-1);
		}
	} else {
		return ((_LC_locale_t *)-1);
	}

	if (loc->lc_numeric) {
		if ((*(loc->lc_numeric->core.init))(loc) ==
		    (_LC_numeric_t *)-1) {
			return ((_LC_locale_t *)-1);
		}
	} else {
		return ((_LC_locale_t *)-1);
	}

	if (loc->lc_messages) {
		if ((*(loc->lc_messages->core.init))(loc) ==
		    (_LC_messages_t *)-1) {
			return ((_LC_locale_t *)-1);
		}
	} else {
		return ((_LC_locale_t *)-1);
	}

	if (loc->lc_time) {
		if ((*(loc->lc_time->core.init))(loc) ==
		    (_LC_time_t *)-1) {
			return ((_LC_locale_t *)-1);
		}
	} else {
		return ((_LC_locale_t *)-1);
	}

	return (NULL);
}

/* The following function is reserved for the future use. */
/* ARGSUSED */
int
__charmap_destructor(_LC_locale_t *hdl)
{
	return (0);
}

/* The following function is reserved for the future use. */
/* ARGSUSED */
int
__ctype_destructor(_LC_locale_t *hdl)
{
	return (0);
}

/* The following function is reserved for the future use. */
/* ARGSUSED */
int
__collate_destructor(_LC_locale_t *hdl)
{
	return (0);
}

/* The following function is reserved for the future use. */
/* ARGSUSED */
int
__time_destructor(_LC_locale_t *hdl)
{
	return (0);
}

/* The following function is reserved for the future use. */
/* ARGSUSED */
int
__monetary_destructor(_LC_locale_t *hdl)
{
	return (0);
}

/* The following function is reserved for the future use. */
/* ARGSUSED */
int
__numeric_destructor(_LC_locale_t *hdl)
{
	return (0);
}

/* The following function is reserved for the future use. */
/* ARGSUSED */
int
__messages_destructor(_LC_locale_t *hdl)
{
	return (0);
}

int
__locale_destructor(_LC_locale_t *hdl)
{
	if ((*(hdl->lc_charmap->core.destructor))(hdl) == -1) {
		return (-1);
	}
	if ((*(hdl->lc_ctype->core.destructor))(hdl) == -1) {
		return (-1);
	}
	if ((*(hdl->lc_collate->core.destructor))(hdl) == -1) {
		return (-1);
	}
	if ((*(hdl->lc_time->core.destructor))(hdl) == -1) {
		return (-1);
	}
	if ((*(hdl->lc_monetary->core.destructor))(hdl) == -1) {
		return (-1);
	}
	if ((*(hdl->lc_numeric->core.destructor))(hdl) == -1) {
		return (-1);
	}
	if ((*(hdl->lc_messages->core.destructor))(hdl) == -1) {
		return (-1);
	}

	return (0);
}

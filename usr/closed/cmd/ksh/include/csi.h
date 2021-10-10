#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 * Copyright (c) 1996, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 *	UNIX shell
 *	S. R. Bourne
 *	rewritten by David Korn
 *
 */

/*
 * CSI assumptions
 * In making ksh CSI (CodeSet Independet), these assumptions are made.
 *
 * CSI assumption1(ascii)
 *	1st byte of multibyte cannot be identical to ASCII character.
 *	(e.g.)
 *		Multibyte character like <0x41><0xc0> not allowed
 *
 * CSI assumption2(head)
 *	Appending one or more bytes to tail of multibyte character
 *	cannot make longer multibyte character.
 *	(e.g.)
 *		If multibyte character <0x82><0xa0> is valid,
 *		multibyte character like <0x82><0xa0><0xee> not allowed.
 *
 * CSI assumption3(nl)
 *	Any byte of multibyte character cannot be identical to
 *	newline ('\n').
 *	(e.g.)
 *		Multibyte character like <0xc1><0x0a> not allowed.
 *
 * CSI assumption4(EOF)
 *	Any byte of multibyte character cannot be identical to EOF.
 *	(e.g.)
 *		Multibyte character like <0xc1><0xff> not allowed.
 *
 * CSI assumption5(slash)
 *	Any byte of multibyte character cannot be identical to slash ('/').
 *	(e.g.)
 *		Multibyte character like <0xc1><0x2f> not allowed.
 *
 * CSI assumption6(wascii)
 *	This is assumption on wchar_t encoding, not on multibyte.
 *	Value of each ASCII (isascii() returns nonzero for it) character
 *	is same value as it's wchar_t value.
 *	(e.g.)
 *		'a' == (char)0x61 and L'a' == (wchar_t)0x61
 *
 * CSI assumption7(invalid)
 *	There is a range in wchar_t value not used in any locale.
 *	ksh uses or reserves ranges below for internal use:
 *		0xfffffe00 - 0xfffffeff (for invalid character)
 *		0xfffffd00 - 0xfffffdff (for internal special value)
 *	(e.g.)
 *		Value 0xfffffea0 cannot be valid wide character
 *		in any locale.
 */

#ifndef ___KSH_CSI_H
#define ___KSH_CSI_H
#include	<wchar.h>
#include	<wctype.h>
#include	"shtype.h"

#define	ASTRIP	0177	/* strip mask for ascii */
#define	SSTRIP	0377	/* strip mask for single-byte */

/* CSI assumption7(invalid) made here. See comments earlier in this file. */
#define	ShWInvalidMask		0xfffff000
#define	ShWRawByteMask		(ShWInvalidMask | 0xe00)
#define	ShWSpecialMask		(ShWInvalidMask | 0xd00)

#define	MARKER		(ShWSpecialMask | 0x1)
#define	UEOF		(ShWSpecialMask | 0x2)
#define	UERASE		(ShWSpecialMask | 0x3)
#define	UKILL		(ShWSpecialMask | 0x4)

#define	IsWInvalid(w)	(((w) & ShWInvalidMask) == ShWInvalidMask)
#define	IsWRawByte(w)	(((w) & ShWRawByteMask) == ShWRawByteMask)
#define	IsWSpecial(w)	(((w) & ShWSpecialMask) == ShWSpecialMask)

#define	ShByteToWRawByte(b)	(ShWRawByteMask | ((b) & SSTRIP))
#define	ShWRawByteToByte(w)	((w) & SSTRIP)

/* CSI assumption6(wascii) made here. See comments earlier in this file. */
#define	ShByteToWChar(b)	((wchar_t)(b) & SSTRIP)

/*
 * This type is for argument of some routines which accpets string
 * as an argument.
 * If value of this enum is AllowEscNull, given string may contain
 * sequence '\\' + '\0', in which '\0' doesn't terminate the string.
 * Otherwise (i.e. value is NotAllowEscNull), '\0' must terminate
 * string.
 */
enum	escnull {
	NotAllowEscNull,
	AllowEscNull
};


#define	mbstowcs_alloc(mbs)	_mbstowcs_alloc((mbs), (NotAllowEscNull))
#define	mbstowcs_alloc_esc(mbs)	_mbstowcs_alloc((mbs), (AllowEscNull))
#define	sh_wcstombs(mbs,wcs,n)	_sh_wcstombs((mbs),(wcs),(n), (NotAllowEscNull))
#define	wcstombs_esc(mbs,wcs,n)	_sh_wcstombs((mbs),(wcs),(n), (AllowEscNull))


/*
 * funciton prototypes
 */

extern int	sh_mbtowc(wchar_t *, const char *, int);
extern int	sh_mbstowcs(wchar_t *, const char *, int);
extern wchar_t*	_mbstowcs_alloc(const char *, enum escnull);
extern int	sh_wctomb(char *, wchar_t);
extern int	_sh_wcstombs(char *, const wchar_t *, size_t, enum escnull);
extern char*	wcstombs_alloc(const wchar_t *);
extern int	sh_wcwidth(wchar_t);
extern int	sh_wcswidth(const wchar_t *);
extern int	wcbytes(wchar_t);
extern int	wcsbytes(const wchar_t *);
extern size_t	wcslen_esc(const wchar_t *);
extern wchar_t	mb_nextc(const char **);
extern wchar_t	mb_peekc(const char *);
extern int	mbschars(const char *);
extern int	mbscolumns(const char *);
extern char*	mbschr(const char *, int);
extern int	sh_iswascii(wchar_t);
extern int	sh_iswalnum(wchar_t);
extern int	sh_iswalpha(wchar_t);
extern int	sh_iswblank(wchar_t);
extern int	sh_iswdigit(wchar_t);
extern int	sh_iswlower(wchar_t);
extern int	sh_iswprint(wchar_t);
extern int	sh_iswspace(wchar_t);
extern int	sh_iswupper(wchar_t);
extern int	iswexp(wchar_t);
extern int	iswmeta(wchar_t);
extern int	iswqmeta(wchar_t);
extern int	waddescape(wchar_t);
extern int	wastchar(wchar_t);
extern int	wdefchar(wchar_t);
extern int	wdipchar(wchar_t);
extern int	wdolchar(wchar_t);
extern int	weolchar(wchar_t);
extern int	wescchar(wchar_t);
extern int	wexpchar(wchar_t);
extern int	wiochar(wchar_t);
extern int	wpatchar(wchar_t);
extern int	wqotchar(wchar_t);
extern int	wsetchar(wchar_t);
extern wchar_t	sh_towupper(wchar_t);
extern wchar_t	sh_towlower(wchar_t);
extern void	xfree(void *);

#endif /* !___KSH_CSI_H */

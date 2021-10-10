/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 * UNIX shell
 *
 * S. R. Bourne
 * Rewritten by David Korn
 * AT&T Bell Laboratories
 *
 */

#include	"csi.h"
#include	"defs.h"
#include	"edit.h"
#include	"history.h"
#include	"sym.h"
#include	<wchar.h>
#include	<wctype.h>

/*
 * sh_mbtowc()
 *	Wrapper for XPG4 mbtowc().
 *	If mbtowc() failed by invalid character, store the value
 *	after OR'd with pattern mask of RawByte.
 */
#ifdef	CSI_ASCIIACCEL
/* CSI assumption1(ascii) made here. See csi.h. */
#endif	/* CSI_ASCIIACCEL */
int
sh_mbtowc(wchar_t *wp, const char *mb, int n)
{
	int	l;
	wchar_t	w;

#ifdef	CSI_ASCIIACCEL
	if ((n > 0) && isascii((int)mb[0])) {
		*wp = ShByteToWChar(mb[0]);
		return (mb[0] ? 1 : 0);
	} else
#endif	/* CSI_ASCIIACCEL */
	if ((l = mbtowc(&w, mb, n)) >= 0) {
		*wp = w;
		return (l);
	} else {
		*wp = ShByteToWRawByte(mb[0]);
		return (1);
	}
}

/*
 * sh_mbstowcs()
 *	Wrapper for XPG4 mbstowcs().
 *	If mbstowcs() failed by invalid character,
 *	try to do conversion one charcter by one with mbtowc().
 *	If mbtowc() failed by invalid character,
 *	policy for sh_mbtowc() is applied.
 */
#ifdef	CSI_ASCIIACCEL
/* CSI assumption1(ascii) made here. See csi.h. */
#endif	/* CSI_ASCIIACCEL */
int
sh_mbstowcs(wchar_t *wcs, const char *mbs, int n)
{
	int	l;
	int	i;
	size_t	ul;
	const char	*p = mbs;
	const char	*lp = &mbs[n];
	wchar_t	*wbuf;
	wchar_t	mwbuf;

	if (wbuf = (wchar_t *)malloc(sizeof (wchar_t) * (n + 1))) {
		ul = mbstowcs(wbuf, mbs, n);
		if (ul != 0 && ul != (size_t)-1) {
			for (i = 0; i < ul; i++) {
				wcs[i] = wbuf[i];
			}
			xfree(wbuf);
			wcs[i] = L'\0';
			return ((int)ul);
		} else {
			xfree(wbuf);
		}
	}

	p = mbs;
	i = 0;
	while ((p < lp) && *p) {
#ifdef	CSI_ASCIIACCEL
		if (isascii((int)*p)) {
			wcs[i] = ShByteToWChar(*p);
			i++;
			p++;
		} else
#endif	/* CSI_ASCIIACCEL */
		if ((l = mbtowc(&mwbuf, p, lp - p)) < 0) {
			wcs[i] = ShByteToWRawByte(*p);
			i++;
			p++;
		} else {
			wcs[i] = mwbuf;
			i++;
			p += l;
		}
	}
	if (i < n)
		wcs[i] = L'\0';
	return (i);
}

/*
 * mbstowcs_esc()
 *	Wrapper for XPG4 mbstowcs().
 *	This function allows string contains sequence '\\'+'\0' in it.
 *	Other policies are same as those of sh_mbstowcs().
 */
#ifdef	CSI_ASCIIACCEL
/* CSI assumption1(ascii) made here. See csi.h. */
#endif	/* CSI_ASCIIACCEL */
static int
mbstowcs_esc(wchar_t *wcs, const char *mbs, size_t n)
{
	int	l;
	int	i;
	size_t	ul;
	const char	*p = mbs;
	const char	*lp = &mbs[n];
	wchar_t	*wbuf;
	wchar_t	mwbuf;

	if (wbuf = (wchar_t *)malloc(sizeof (wchar_t) * (n + 1))) {
		ul = mbstowcs(wbuf, mbs, n);
		if (ul != 0 && ul != (size_t)-1) {
			for (i = 0; i < ul; i++)
				wcs[i] = wbuf[i];
			xfree(wbuf);
			mbs += strlen(mbs);
			wcs[i] = L'\0';
			mbs++;
			if (wcs[i-1] == L'\\') {
				l = mbstowcs_esc(&wcs[i+1], mbs, n-(i+1));
				return (i + 1 + l);
			} else
				return ((int)ul);
		} else {
			xfree(wbuf);
		}
	}

	p = mbs;
	i = 0;
	while ((p < lp) && *p) {
		if (*p == '\0' && wcs[i-1] == L'\\') {
			wcs[i] = L'\0';
			i++;
			p++;
			continue;
		}
#ifdef	CSI_ASCIIACCEL
		if (isascii((int)*p)) {
			wcs[i] = ShByteToWChar(*p);
			i++;
			p++;
		} else
#endif	/* CSI_ASCIIACCEL */
		if ((l = mbtowc(&mwbuf, p, lp - p)) < 0) {
			wcs[i] = ShByteToWRawByte(*p);
			i++;
			p++;
		} else {
			wcs[i] = mwbuf;
			i++;
			p += l;
		}
	}
	if (i < n)
		wcs[i] = L'\0';
	return (i);
}

/*
 * _mbstowcs_alloc()
 *	Wrapper for XPG4 mbstowcs().
 *	Allocates area to store converted wchar_t string in.
 *	This function is called via either of these two marcos.
 *		- mbstowcs_alloc()
 *		- mbstowcs_alloc_esc()
 *	When called via mbstowcs_alloc_esc(), sequence '\\'+'\0' is
 *	allowed in multibyte stiring. mbstowcs_alloc() doesn't allow
 *	it. They are switched by 2nd parameter "enum escnull en".
 */
/* CSI assumption1(ascii) made here. See csi.h. */
wchar_t *
_mbstowcs_alloc(const char *mbs, enum escnull en)
{
	wchar_t	*wcs;
	const char	*mbp = mbs;
	int	l;
	size_t	size;

	if (en == AllowEscNull)
		while (*mbp || ((mbp > mbs) && (*(mbp - 1) == '\\'))) mbp++;
	else
		while (*mbp) mbp++;
	size = (size_t)(mbp - mbs);

	if ((wcs = (wchar_t *)malloc(sizeof (wchar_t) * (size + 1))) == NULL) {
		return (NULL);
	}

	if (en == AllowEscNull)
		l = mbstowcs_esc(wcs, mbs, size);
	else
		l = sh_mbstowcs(wcs, mbs, size);
	wcs[l] = L'\0';

	return (wcs);
}

/*
 * sh_wctomb()
 *	Wrapper for XPG4 wctomb().
 *	If given wchar_t contains Invalid (RawByte) value,
 *	store the value after stripped pattern mask.
 *	Otherwise, calls wctomb().
 *	Code for wctomb() failure exists but it should not occur.
 */
int
sh_wctomb(char *b, wchar_t w)
{
	int	l;

	if (IsWInvalid(w)) {
		b[0] = ShWRawByteToByte(w);
		return (1);
	} else {
		if (
#ifdef	CSI_ASCIIACCEL
			iswascii(w) ||
#endif	/* CSI_ASCIIACCEL */
			((l = wctomb(b, w)) < 0)) {
			b[0] = w & SSTRIP;
			return (1);
		} else {
			return (l);
		}
	}
}

/*
 * _sh_wcstombs()
 *	Wrapper for XPG4 wcstombs().
 *	Invalid character handling policy is same as that for sh_wctomb().
 *	This function is called:
 *		- via macro sh_wcstombs()
 *			doesn't allow sequence L'\\'+L'\0'
 *		- via macro wcstombs_esc()
 *			allows sequence L'\\'+L'\0'
 *		- in function wcstombs_alloc()
 *			doesn't allow sequence L'\\'+L'\0'
 */
int
_sh_wcstombs(char *mbs, const wchar_t *wcs, size_t n, enum escnull en)
{
	char	buf[MB_LEN_MAX + 1];
	char	*mbp = mbs;
	char	*mbmax = mbs + n;
	int	l;
	int	i;
	wchar_t	lastw;

	for (i = 0; mbp < mbmax; i++) {
		if (!IsWInvalid(wcs[i])) {
			if (wcs[i] == L'\0')
				if ((en == AllowEscNull) && (i > 0) &&
					lastw == '\\') {
					lastw = wcs[i];
					*mbp++ = '\0';
				} else
					break;
			else if (
#ifdef	CSI_ASCIIACCEL
				iswascii(wcs[i]) ||
#endif	/* CSI_ASCIIACCEL */
				(l = wctomb(buf, wcs[i])) < 0) {
				lastw = wcs[i];
				*mbp++ = wcs[i] & SSTRIP;
			} else {
				if (mbp + l <= mbmax) {
					lastw = wcs[i];
					strncpy(mbp, buf, l);
					mbp += l;
				} else
					break;
			}
		} else {
			lastw = wcs[i];
			*mbp++ = ShWRawByteToByte(wcs[i]);
		}
	}

	if ((mbp - mbs) < n)
		*mbp = '\0';
	return ((size_t)(mbp - mbs));
}

/*
 * wcstombs_alloc()
 *	Wrapper for XPG4 wcstombs().
 *	Invalid character handling policy is same as that for sh_wctomb().
 *	This function calles _sh_wcstombs() in it.
 *	Doesn't allow sequence L'\\'+L'\0' in given wchar_t string.
 */
char *
wcstombs_alloc(const wchar_t *wcs)
{
	char	*mbs;
	const wchar_t	*wp = wcs;
	size_t	size, l;

	while (*wp) wp++;
	size = (size_t)(wp - wcs);

	if ((mbs = (char *)malloc(sizeof (char) * MB_CUR_MAX * (size + 1))) ==
		NULL) {
		return (NULL);
	}

	l = _sh_wcstombs(mbs, wcs, size * MB_CUR_MAX, NotAllowEscNull);
	mbs[l] = '\0';

	return (mbs);
}

/*
 * sh_wcwidth()
 *	Wrapper for XPG4 wcwidth().
 *	If invalid character is given, return 4 which is for
 *	octal representation "\ooo".
 *	Otherwise, call wcwidth().
 *	If returned width is negative (which means non-printable),
 *	return number of columns needed to print the character visibly.
 *	For control characters <= DEL, use "^x" representation.
 *	For other characters, use octal "\ooo[\ooo]..." representation.
 */
int
sh_wcwidth(wchar_t w)
{
	int	wt = 0;
	int	wd;

	if (IsWInvalid(w)) {
		wt += 4;
	} else {
		if ((wd = wcwidth(w)) < 0) {
			int	c;
			if (iswascii(w)) {
				c = (int)(w);
				if (c < ' ' || c == 0x7f)
					wt += 2;
				else
					wt += 4;
			} else {
				char	buf[MB_LEN_MAX + 1];
				int	l;
				l = wctomb(buf, w);
				wt +=  l*4;
			}
		} else
			wt += wd;
	}

	return (wt);
}

/*
 * sh_wcswidth()
 *	Wrapper for XPG4 wcswidth().
 *	Policy for sh_wcwidth() is applied to each wide character.
 */
int
sh_wcswidth(const wchar_t *wcs)
{
	const wchar_t	*wp = wcs;
	int	wt = 0;

	while (*wp)
		wt += sh_wcwidth(*wp++);

	return (wt);
}

/*
 * wcbytes()
 *	Return number of bytes needed to represent given wide character
 *	as multibyte.
 */
int
wcbytes(wchar_t w)
{
	char	mbbuf[MB_LEN_MAX + 1];

	if (!IsWInvalid(w))
		return (wctomb(mbbuf, w));
	else if (w == MARKER)
		return (0);
	else
		return (1);
}

/*
 * wcsbytes()
 *	String version of wcbytes()
 */
int
wcsbytes(const wchar_t *wcs)
{
	int	bt = 0;

	while (*wcs)
		bt += wcbytes(*wcs++);

	return (bt);
}

/*
 * wcslen_esc()
 *	Wrapper for XPG4 wcslen().
 *	Allow sequence L'\\'+L'\0' in given wchar_t string.
 */

size_t
wcslen_esc(const wchar_t *wcs)
{
	size_t	pl;

	if ((pl = wcslen(wcs)) > 0 && wcs[pl-1] == L'\\') {
		pl += wcslen_esc(&wcs[pl+1]);
	}

	return (pl);
}

/*
 * mb_nextc()
 *	Convert a multibyte chacter (even if invalid) with sh_mbtowc()
 *	and moves pointer to point next character.
 */
wchar_t
mb_nextc(const char **spp)
{
	wchar_t	w;
	int	l;

	l = sh_mbtowc(&w, *spp, MB_CUR_MAX);
	*spp += (l > 0) ? l : 1;
	return (w);
}

/*
 * mb_peekc()
 *	Same as mb_nextc() except this one doesn't move pointer.
 */
wchar_t
mb_peekc(const char *sp)
{
	wchar_t	w;

	(void) sh_mbtowc(&w, sp, MB_CUR_MAX);
	return (w);
}

/*
 * mbschars()
 *	Returns number of characters in mutlibyte string.
 */
int
mbschars(const char *mbs)
{
	int	nc = 0;

	while (mb_nextc(&mbs) != L'\0')
		nc++;

	return (nc);
}

/*
 * mbscolumns()
 *	Returns number of columns needed to print give multibyte string.
 *	Column counting policy is same as that of sh_wcwidth().
 */
int
mbscolumns(const char *mbs)
{
	int	nc = 0;
	wchar_t	w;

	while ((w = mb_nextc(&mbs)) != L'\0')
		nc += sh_wcwidth(w);

	return (nc);
}

/*
 * mbschr()
 *	Wrapper for XPG4 strchr()
 *	Recognizes character boundary in the given string.
 *	Character to search must be ASCII.
 */
char *
mbschr(const char *mbs, int c)
{
	const char	*p = mbs;
	wchar_t	w;

	if (c == 0) {
		while (*mbs)
			mbs++;
		return ((char *)mbs);
	}

	while ((w = mb_nextc(&mbs)) != L'\0') {
		if (iswascii(w) && (int)w == c)
			return ((char *)p);
		else
			p = mbs;
	}

	return (NULL);
}

/*
 * sh_iswascii()
 *	Wrapper for XPG4 iswascii()
 *	For ksh's internal special characters, return 1.
 *	For invalid characters, return 0.
 *	For other characters, call iswascii().
 */
int
sh_iswascii(wchar_t w)
{
	if (IsWSpecial(w))
		return (1);
	else if (!IsWRawByte(w))
		return (iswascii(w));
	else
		return (0);
}

/*
 * sh_iswalnum()
 *	Wrapper for XPG4 iswalnum()
 *	Returns nonzero for only ascii alnum characters.
 *	Doesn't use locale's alnum class but use ksh's internal.
 *	This is used to check characters for identifier.
 */
int
sh_iswalnum(wchar_t w)
{
	if (!IsWInvalid(w))
		return (iswascii(w) && sh_isalnum((int)w));
	else
		return (0);
}

/*
 * sh_iswalpha()
 *	Wrapper for XPG4 iswapha()
 *	Returns nonzero for only ascii alpha characters.
 *	Doesn't use locale's alpha class but use ksh's internal.
 *	This is used to check characters for top of identifier.
 */
int
sh_iswalpha(wchar_t w)
{
	if (!IsWInvalid(w))
		return (iswascii(w) && sh_isalpha((int)w));
	else
		return (0);
}

/*
 * sh_iswblank()
 *	Wrapper for XPG6 iswblank.
 */
int
sh_iswblank(wchar_t w)
{
	if (!IsWInvalid(w))
		return (w == L'\t' || iswctype((wint_t)w, _ISBLANK));
	else
		return (0);
}

/*
 * sh_iswdigit()
 *	Wrapper for XPG4 iswdigit().
 *	Returns nonzero for only ascii digit characters.
 *	Doesn't use locale's digit class but use ksh's internal.
 */
int
sh_iswdigit(wchar_t w)
{
	if (!IsWInvalid(w))
		return (iswascii(w) && sh_isdigit((int)w));
	else
		return (0);
}

/*
 * sh_iswlower()
 *	Wrapper for XPG4 iswlower().
 */
int
sh_iswlower(wchar_t w)
{
	if (!IsWInvalid(w))
		return (iswlower(w));
	else
		return (0);
}

/*
 * sh_iswprint()
 *	Wrapper for XPG4 iswlower().
 */
int
sh_iswprint(wchar_t w)
{
	if (!IsWInvalid(w))
		return (iswprint(w));
	else
		return (0);
}

/*
 * sh_iswspace()
 *	Wrapper for XPG4 iswspace().
 */
int
sh_iswspace(wchar_t w)
{
	if (!IsWInvalid(w))
		return (iswascii(w) && iswspace(w));
	else
		return (0);
}

/*
 * sh_iswupper()
 *	Wrapper for XPG4 iswupper().
 */
int
sh_iswupper(wchar_t w)
{
	if (!IsWInvalid(w))
		return (iswupper(w));
	else
		return (0);
}

/*
 * wchar_t version of ksh specific character classifications
 * 	if given wchar_t is ascii, pass it to macro defined in "shtype.h"
 *	otherwise, return 0.
 */

int
iswexp(wchar_t w)
{
	return (sh_iswascii(w) && isexp(w));
}

int
iswmeta(wchar_t w)
{
	return ((sh_iswascii(w) && ismeta(w)) || sh_iswblank(w));
}

int
iswqmeta(wchar_t w)
{
	return (sh_iswascii(w) && isqmeta((int)w));
}

int
waddescape(wchar_t w)
{
	return (sh_iswascii(w) && addescape((int)w));
}

int
wastchar(wchar_t w)
{
	return (sh_iswascii(w) && astchar((int)w));
}

int
wdefchar(wchar_t w)
{
	return (sh_iswascii(w) && defchar((int)w));
}

int
wdipchar(wchar_t w)
{
	return (sh_iswascii(w) && dipchar((int)w));
}

int
wdolchar(wchar_t w)
{
	return (sh_iswascii(w) && dolchar((int)w));
}

int
weolchar(wchar_t w)
{
	return (sh_iswascii(w) && eolchar((int)w));

}

int
wescchar(wchar_t w)
{
	return (sh_iswascii(w) && escchar((int)w));
}

int
wexpchar(wchar_t w)
{
	return (sh_iswascii(w) && expchar((int)w));
}

int
wiochar(wchar_t w)
{
	return (sh_iswascii(w) && iochar((int)w));
}

int
wpatchar(wchar_t w)
{
	return (sh_iswascii(w) && patchar((int)w));
}

int
wqotchar(wchar_t w)
{
	return (sh_iswascii(w) && qotchar((int)w));
}

int
wsetchar(wchar_t w)
{
	return (sh_iswascii(w) && setchar((int)w));
}

/*
 * sh_towupper()
 *	Wrapper for XPG4 towupper().
 */
wchar_t
sh_towupper(wchar_t w)
{
	if (!IsWInvalid(w))
		w = towupper(w);
	return (w);
}

/*
 * sh_towlower()
 *	Wrapper for XPG4 towupper().
 */
wchar_t
sh_towlower(wchar_t w)
{
	if (!IsWInvalid(w))
		w = towlower(w);
	return (w);
}

/*
 * xfree()
 *	free aread allocated by sh_mbstowcs_alloc/wcstombs_alloc()
 */

void
xfree(void *p)
{
	if (p) (void)free(p);
}

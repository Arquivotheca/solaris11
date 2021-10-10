/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */

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
 *
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: __fnmatch_sb.c,v $ $Revision: 1.3.4.3"
 *	" $ (OSF) $Date: 1992/11/05 21:58:45 $";
 * static char sccsid[] = "@(#)69  1.6.1.3  "
 *	"src/bos/usr/ccs/lib/libc/__fnmatch_std.c, bos, bos410 "
 *	"1/12/93 11:09:07";
 * #endif
 */
/*
 * COMPONENT_NAME: (LIBCPAT) Standard C Library Pattern Functions
 *
 * FUNCTIONS: __fnmatch_sb
 * FUNCTIONS: __fnmatch_std
 *
 * ORIGINS: 27
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991, 1992
 * All Rights Reserved
 * Licensed Materials - Property of IBM
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */


#include "lint.h"
#include <sys/types.h>
#include <stdlib.h>
#include <sys/localedef.h>
#include <fnmatch.h>
#include <string.h>
#include "patlocal.h"

#if defined(_C_COLL)
#define	FUNC_NAME	__fnmatch_C
#define	LEN_S	1		/* increment for string */
#define	LEN_B	1		/* increment for bracket string */
#elif defined(_SB_COLL)
#define	FUNC_NAME	__fnmatch_sb
#define	LEN_S	1		/* increment for string */
#define	LEN_B	1		/* increment for bracket string */
#else  /* STANDARD METHOD */
#define	FUNC_NAME	__fnmatch_std
#define	LEN_S	mblen_s		/* increment for string */
#define	LEN_B	mblen_b		/* increment for bracket string */
#endif

static int bracket(_LC_collate_t *, const char *, const char **, wchar_t,
    wchar_t, int);

static int
fnmatch_common(_LC_collate_t *phdl, const char *ppat, const char *string,
    const char *pstr, int flags, int recursion)
{
	int	stat;		/* recursive rfnmatch() return status	*/
	wchar_t	ucoll_s;	/* string unique coll value		*/
	wchar_t	wc_p;		/* wide character for pattern 		*/
	wchar_t	wc_s;		/* next string character		*/
	char	*pse;		/* ptr to next string character		*/
#if !defined(_C_COLL)
	int	mblen_p;	/* # bytes in next pattern character	*/
	int	mblen_s;	/* # bytes in next string character	*/
	_LC_charmap_t	*cmapp = phdl->cmapp; /* pointer to the charmap obj */
#if !defined(_SB_COLL)
	int	mbcurmax = cmapp->cm_mb_cur_max; /* mb_cur_max */
#endif
#endif

/* Limit the amount of recursion to avoid DoS */
	if (recursion-- == 0)
		return (FNM_ERROR);

/*
 * Loop through pattern, matching string characters with pattern
 * Return success when end-of-pattern and end-of-string reached simultaneously
 * Return no match if pattern/string mismatch
 */
	while (*ppat != '\0') {
		switch (*ppat) {
/*
 * <backslash> quotes the next character if FNM_NOESCAPE flag is NOT set
 * if FNM_NOESCAPE is set,  treat <backslash> as itself
 * Return no match if pattern ends with quoting <backslash>
 */
		case '\\':
			if ((flags & FNM_NOESCAPE) == 0) {
				if (*++ppat == '\0')
					return (FNM_NOMATCH);
			}
/*
 * Ordinary character in pattern matches itself in string
 * Need to compare process codes for multibyte languages
 * Continue if pattern character matches string character
 * Return no match if pattern character does not match string character
 */
		default:
		ordinary:
#if defined(_C_COLL) || defined(_SB_COLL)
			if (*ppat == *pstr || ((flags & FNM_IGNORECASE) != 0 &&
			    tolower((uchar_t)*ppat) ==
			    tolower((uchar_t)*pstr))) {
				ppat++;
				pstr++;
				break;
			}
#else  /* STANDARD METHOD */
			/*
			 * Try to convert into a wide character.
			 * If unsucessful, just compare the character as
			 * a byte and set mblen_s to -1 to indicate that
			 * we don't need to worry about case-insensitive
			 * comparison in case FNM_IGNORECASE is set.
			 */
			mblen_p = MBTOWC_NATIVE(&wc_p, ppat, mbcurmax);
			if (mblen_p < 0) {
				mblen_p = 1;
				mblen_s = -1;
			} else {
				mblen_s = 0;
			}

			if (strncmp(ppat, pstr, mblen_p) == 0) {
				ppat += mblen_p;
				pstr += mblen_p;
				break;
			} else if (mblen_s != -1 &&
			    (flags & FNM_IGNORECASE) != 0) {
				mblen_s = MBTOWC_NATIVE(&wc_s, pstr, mbcurmax);

				/*
				 * Once again, if the pstr points to
				 * an invalid, a null byte, or itself is
				 * a null pointer, they cannot match.
				 */
				if (mblen_s > 0 && TOWLOWER_NATIVE(wc_p) ==
				    TOWLOWER_NATIVE(wc_s)) {
					ppat += mblen_p;
					pstr += mblen_s;
					break;
				}
			}
#endif
			return (FNM_NOMATCH);

/*
 * <asterisk> matches zero or more string characters
 * Cannot match <slash> if FNM_PATHNAME is set
 * Cannot match leading <period> if FNM_PERIOD is set
 * Consecutive <asterisk> are redundant
 *
 * Return success if remaining pattern matches remaining string
 * or FNM_LEADING_DIR is set therefor there is nothing more to match
 * Otherwise advance to the next string character and try again
 * Return no match if string exhausted and more pattern remains
 */
		case '*':
			while (*++ppat == '*')
				;
			if (*ppat == '\0') {
				if ((flags & FNM_LEADING_DIR) != 0)
					return (0);
				if ((flags & FNM_PATHNAME) != 0 &&
				    strchr(pstr, '/') != NULL)
					return (FNM_NOMATCH);
				if (*pstr == '.' && (flags & FNM_PERIOD) != 0)
					if (pstr == string ||
					    (pstr[-1] == '/' &&
					    (flags & FNM_PATHNAME) != 0))
						return (FNM_NOMATCH);
				return (0);
			}
			while (*pstr != '\0') {
				stat = fnmatch_common(phdl, ppat, string, pstr,
				    flags, recursion);
				if (stat != FNM_NOMATCH)
					return (stat);
				if (*pstr == '/') {
					if ((flags & FNM_PATHNAME) != 0)
						return (FNM_NOMATCH);
				} else if (*pstr == '.' &&
				    (flags & FNM_PERIOD) != 0) {
					if (pstr == string ||
					    (pstr[-1] == '/' &&
					    (flags & FNM_PATHNAME) != 0))
						return (FNM_NOMATCH);
				}
#if !defined(_C_COLL) && !defined(_SB_COLL)
				mblen_s = MBLEN_NATIVE(pstr, mbcurmax);
				if (mblen_s <= 0)
					mblen_s = 1;
#endif
				pstr += LEN_S;
			}
			return (FNM_NOMATCH);
/*
 * <question-mark> matches any single character
 * Cannot match <slash> if FNM_PATHNAME is set
 * Cannot match leading <period> if FNM_PERIOD is set
 *
 * Return no match if string is exhausted
 * Otherwise continue with next pattern and string character
 */
		case '?':
			if (*pstr == '/') {
				if ((flags & FNM_PATHNAME) != 0)
					return (FNM_NOMATCH);
			} else if (*pstr == '.' && (flags & FNM_PERIOD) != 0) {
				if (pstr == string || (pstr[-1] == '/' &&
				    (flags & FNM_PATHNAME) != 0))
					return (FNM_NOMATCH);
			}
			if (*pstr != '\0') {
#if !defined(_C_COLL) && !defined(_SB_COLL)
				mblen_s = MBLEN_NATIVE(pstr, mbcurmax);
				if (mblen_s <= 0)
					mblen_s = 1;
#endif
				pstr += LEN_S;
				ppat++;
				break;
			} else {
				return (FNM_NOMATCH);
			}
/*
 * <left-bracket> begins a [bracket expression] which matches single collating
 * element
 * [bracket expression] cannot match <slash> if FNM_PATHNAME is set
 * [bracket expression] cannot match leading <period> if FNM_PERIOD is set
 */
		case '[':
			if (*pstr == '/') {
				if ((flags & FNM_PATHNAME) != 0)
					return (FNM_NOMATCH);
			} else if (*pstr == '.' && (flags & FNM_PERIOD) != 0) {
				if (pstr == string || (pstr[-1] == '/' &&
				    (flags & FNM_PATHNAME) != 0))
					return (FNM_NOMATCH);
			}
#if defined(_C_COLL)
			if (*pstr == '\0')
				return (FNM_NOMATCH);
			wc_s = (wchar_t)*(uchar_t *)pstr;
			ucoll_s = wc_s;
			pse = (char *)pstr + 1;
#else  /* SB or STANDARD METHOD */
/*
 * Determine unique collating value of next collating element
 */
#if defined(_SB_COLL)
			wc_s = (wchar_t)*(uchar_t *)pstr;
#else  /* STANDARD METHOD */
			mblen_s = MBTOWC_NATIVE(&wc_s, pstr, mbcurmax);
			if (mblen_s <= 0) {
				wc_s = (wchar_t)*(uchar_t *)pstr;
				mblen_s = 1;
			}
#endif /* _SB_COLL */
			ucoll_s = _mbucoll(phdl, (char *)pstr, (char **)&pse);
			if ((ucoll_s < MIN_UCOLL) || (ucoll_s > MAX_UCOLL))
				return (FNM_NOMATCH);
#endif /* _C_COLL */
/*
 * Compare unique collating value to [bracket expression]
 *   > 0  no match
 *   = 0  match found
 *   < 0  error, treat [] as individual characters
 */
			stat = bracket(phdl, ppat + 1, &ppat, wc_s, ucoll_s,
			    flags);
			if (stat == 0)
				pstr = pse;
			else if (stat > 0)
				return (FNM_NOMATCH);
			else
				goto ordinary;
			break;
		}
	}
/*
 * <NUL> following end-of-pattern
 * Return success if string is also at <NUL> or string is at '/' and
 * FNM_LEADING_DIR is set
 * Return no match if string is not at <NUL> or string is not at '/' with
 * FNM_LEADING_DIR flag
 */
	if (*pstr == '\0' || ((flags & FNM_LEADING_DIR) != 0 && *pstr == '/'))
		return (0);

	return (FNM_NOMATCH);
}

#define	FNMATCH_RECURSION	64

int
FUNC_NAME(_LC_collate_t *phdl, const char *ppat, const char *string,
    const char *pstr, int flags)
{
	return (fnmatch_common(phdl, ppat, string, pstr, flags,
	    FNMATCH_RECURSION));
}

#if defined(_C_COLL)
/*
 * fnm_casemap() - If possible, return the case converted wide character.
 *		  Otherwise, return 0 to indicate there was no case conversion.
 */
static wchar_t
fnm_casemap(uchar_t c)
{
	if (isupper(c))
		return ((wchar_t)tolower(c));
	if (islower(c))
		return ((wchar_t)toupper(c));
	return (0);
}
#else /* _SB_COLL or STANDARD METHOD */
/*
 * fnm_casemap() - If possible, do a case conversion, collect the primary
 *		collation weight (if requested) and the unique collation
 *		weight of the converted, and return 1. Otherwise, return 0 to
 *		indicate that the case conversion was not done or return -1 to
 *		indicate the unique collation weight was not available.
 */
static int
fnm_casemap(_LC_collate_t *phdl, wchar_t *wc, wchar_t *u)
{
	wchar_t w = *wc;

	if (ISWCTYPE_NATIVE(w, _ISUPPER))
		w = TOWLOWER_NATIVE(w);
	else if (ISWCTYPE_NATIVE(w, _ISLOWER))
		w = TOWUPPER_NATIVE(w);
	else
		return (0);

	if (u)
		*u = __wcprimcollwgt(w);

	w = __wcuniqcollwgt(w);
	if (w < MIN_UCOLL || w > MAX_UCOLL)
		return (-1);

	*wc = w;

	return (1);
}
#endif


/*
 * bracket()    - Determine if [bracket] matches filename character
 *
 *	pdhl	 - ptr to __lc_collate structure
 *	ppat	 - ptr to position of '[' in pattern
 *	wc_s	 - process code of next filename character
 *	ucoll_s	 - unique collating weight of next filename character
 *	flags	 - fnmatch() flags, see <fnmatch.h>
 */
#if defined(_C_COLL)
/* ARGSUSED */
#endif
static int
bracket(_LC_collate_t *phdl, const char *sppat, const char **pend, wchar_t wc_s,
    wchar_t ucoll_s, int flags)
{
	int	neg;		/* nonmatching [] expression	*/
	int	dash;		/* <hyphen> found for a-z range expr	*/
	int	found;		/* match found flag			*/
	uchar_t	*pb;		/* ptr to [bracket] pattern		*/
	uchar_t	*pi;		/* ptr to international [] expression	*/
	uchar_t	*ppat = (uchar_t *)sppat;
	char	type;		/* international [] type =:.		*/
	char	class[CLASS_SIZE+1]; /* character class with <NUL>	*/
	char	*pclass;	/* class[] ptr				*/
	wchar_t	min_ucoll;	/* minimum unique collating value	*/
	wchar_t	min_ucoll2;	/* second one for FNM_IGNORECASE support */
	wchar_t	max_ucoll;	/* maximum unique collating value	*/
	wchar_t	max_ucoll2;	/* second one for FNM_IGNORECASE support */
	wchar_t	prev_min_ucoll;	/* low end of range expr		*/
	wchar_t	prev_min_ucoll2; /* low end of second range expr	*/
	wctype_t wct;		/* temporary wide character type	*/
	int	ret;		/* to save and check on return values	*/
#if defined(_C_COLL)
	wchar_t	tmpwc;		/* temporary wide characer		*/
#else
	uchar_t	*piend;		/* ptr to character after intl [] expr	*/
	wchar_t	uwgt;		/* unique collation weight 		*/
	wchar_t	pcoll;		/* primary collation weight		*/
	wint_t	i;		/* process code loop index		*/
	wchar_t	wc_b;		/* bracket process code			*/
	_LC_charmap_t	*cmapp = phdl->cmapp; /* pointer to the charmap obj */
	int	mblen_b;	/* next bracket character length	*/
	int	mbcurmax = cmapp->cm_mb_cur_max; /* mb_cur_max */
#endif
/*
 * Leading <exclamation-mark> designates nonmatching [bracket expression]
 */
	pb = ppat;
	neg = 0;
	if (*pb == '!') {
		pb++;
		neg++;
	}
/*
 * Loop through each [] collating element comparing unique collating values
 */
	dash = 0;
	found = 0;
	prev_min_ucoll = prev_min_ucoll2 = 0;
	min_ucoll = min_ucoll2 = 0;
	max_ucoll = max_ucoll2 = 0;

	while (*pb != '\0') {
/*
 * Final <right-bracket> so return status based upon whether match was found
 * Return character after final ] if match is found
 * Ordinary character if first character of [barcket expression]
 */
		if (*pb == ']') {
			if ((neg == 0 && pb > ppat) ||
			    (neg != 0 && pb > ppat + 1)) {
				if ((found ^ neg) == 0)
					return (FNM_NOMATCH);
				*pend = (const char *)++pb;
				return (0);
			}
		}
/*
 * Return error if embedded <slash> found and FNM_PATHNAME is set
 */
		else if (*pb == '/') {
			if ((flags & FNM_PATHNAME) != 0)
				return (-1);
		}
/*
 * Decode next [] element
 */
		if (dash == 0) {
			prev_min_ucoll = min_ucoll;
			prev_min_ucoll2 = min_ucoll2;
		}

		switch (*pb) {
/*
 * ordinary character - compare unique collating weight of collating symbol
 */
		default:
ordinary:
#if defined(_C_COLL)
			if ((min_ucoll = (wchar_t)*pb) == wc_s)
				found = 1;
			max_ucoll = min_ucoll;

			/*
			 * If case folding is asked, try to convert to
			 * the other case, compare to see if it matches, and
			 * save the pattern character for later use.
			 */
			if (!found && (flags & FNM_IGNORECASE) != 0 &&
			    (tmpwc = fnm_casemap(*pb)) != 0) {
				max_ucoll2 = min_ucoll2 = tmpwc;

				if (min_ucoll2 == wc_s)
					found = 1;
			}
			pb++;
			break;
#else  /* SB or STANDARD METHOD */
			pi = pb;
			min_ucoll = _mbucoll(phdl, (char *)pb, (char **)&pb);

			if (min_ucoll < MIN_UCOLL || min_ucoll > MAX_UCOLL)
				return (-1);
			if (min_ucoll == ucoll_s)
				found = 1;
			max_ucoll = min_ucoll;

			/*
			 * If case folding is requested, check if
			 * we are dealing with a multi-character collating
			 * element or not.
			 *
			 * If not, we will need to convert the current
			 * pattern character to a wide character, do case-
			 * insensitive comparison and also save the wide
			 * character for later use with range expression.
			 */
			if (!found && (flags & FNM_IGNORECASE) != 0) {
				mblen_b = MBTOWC_NATIVE(&wc_b,
				    (const char *)pi, mbcurmax);

				/* Is it a simple collating element? */
				if ((pb - pi) == mblen_b &&
				    (ret = fnm_casemap(phdl, &wc_b, 0)) != 0) {
					/*
					 * If failed to get the unique
					 * collation weight, return -1.
					 */
					if (ret == -1)
						return (-1);

					max_ucoll2 = min_ucoll2 = wc_b;

					if (min_ucoll2 == ucoll_s)
						found = 1;
				}
			}

			break;
#endif
/*
 * <hyphen> deliniates a range expression unless it is first character of []
 * or it immediately follows another <hyphen> and is therefore an end point
 */
		case '-':
			if (dash == 0 && !((neg == 0 && pb == ppat) ||
			    (neg != 0 && pb == ppat + 1) || (pb[1] == ']'))) {
				dash++;
				pb++;
				continue;
			}
			goto ordinary;
/*
 * <left-bracket> initiates one of the following internationalization
 *   character expressions
 *   [: :] character class
 *   [= =] equivalence character class
 *   [. .] collation symbol
 *
 * it is treated as itself if not followed by appropriate special character
 * it is treated as itself if any error is encountered
 */
		case '[':
			pi = pb + 2;
			if ((type = pb[1]) == ':') {
				pclass = class;
				for (;;) {
					if (*pi == '\0')
						return (-1);
					if (*pi == ':' && pi[1] == ']')
						break;
					if (pclass >= &class[CLASS_SIZE])
						return (-1);
					*pclass++ = *pi++;
				}
				if (pclass == class)
					return (-1);
				*pclass = '\0';

				if (ISWCTYPE_NATIVE(wc_s, WCTYPE_NATIVE(class)))
					found = 1;
				else if ((flags & FNM_IGNORECASE) != 0) {
					/*
					 * If the charclass is either "upper"
					 * or "lower", we also need to check on
					 * the charclass of the other case.
					 */
					if (strcmp(class, "upper") == 0)
						wct = _ISLOWER;
					else if (strcmp(class, "lower") == 0)
						wct = _ISUPPER;
					else
						wct = 0;

					if (wct && ISWCTYPE_NATIVE(wc_s, wct))
						found = 1;
				}

				min_ucoll = min_ucoll2 = 0;
				pb = pi + 2;
				break;
			}
/*
 * equivalence character class
 *   get process code of character, error if invalid or <NUL>
 *   treat as collation symbol if not entire contents of [= =]
 *   locate address of collation weight table
 *   get unique collation weight, error if none
 *   set found flag if unique collation weight matches that of string
 *   collating element
 *   if no match, compare unique collation weight of all equivalent characters
 */
			else if (type == '=') {
#if defined(_C_COLL)
				if (*pi == '\0' || pi[1] != '=' || pi[2] != ']')
					return (-1);
				if ((min_ucoll = (wchar_t)*pi) == wc_s)
					found = 1;
				max_ucoll = min_ucoll;

				/*
				 * If case folding is asked, try to convert to
				 * the other case and compare. Also save the
				 * pattern character for later use.
				 */
				if (!found && (flags & FNM_IGNORECASE) != 0 &&
				    (tmpwc = fnm_casemap(*pi)) != 0) {
					max_ucoll2 = min_ucoll2 = tmpwc;

					if (min_ucoll2 == wc_s)
						found = 1;
				}
				pb = pi + 3;
				break;
#else  /* SB or STANDARD METHOD */
#if defined(_SB_COLL)
				wc_b = (wchar_t)*pi;
				if (wc_b == 0)
					return (-1);
#else  /* STANDARD METHOD */
				mblen_b = MBTOWC_NATIVE(&wc_b, (const char *)pi,
				    mbcurmax);
				if (mblen_b <= 0)
					return (-1);
#endif
				if (pi[LEN_B] != '=')
					goto coll_sym;
				if (pi[LEN_B + 1] != ']')
					return (-1);
				min_ucoll = __wcuniqcollwgt(wc_b);
				if ((min_ucoll < MIN_UCOLL) ||
				    (min_ucoll > MAX_UCOLL))
					return (-1);
				max_ucoll = min_ucoll;
				pcoll = __wcprimcollwgt(wc_b);
				for (i = MIN_PC; i <= MAX_PC; i++) {
					if (__wcprimcollwgt(i) == pcoll) {
						uwgt = __wcuniqcollwgt(i);
						if (uwgt == ucoll_s)
							found = 1;
						if (uwgt < min_ucoll)
							min_ucoll = uwgt;
						if (uwgt > max_ucoll)
							max_ucoll = uwgt;
					}
				}

				/*
				 * If case folding is asked, check if we can
				 * convert case into the other case, collect
				 * the unique weight and the primary weight
				 * (i.e., the weight for the equivalent class),
				 * scan though characters of the equivalent
				 * class to find the match and also to reset
				 * min and max possible unique collation
				 * weights for later use.
				 */
				if (!found && (flags & FNM_IGNORECASE) != 0 &&
				    (ret = fnm_casemap(phdl, &wc_b, &pcoll))
				    != 0) {
					if (ret == -1)
						return (-1);

					max_ucoll2 = min_ucoll2 = wc_b;

					for (i = MIN_PC; i <= MAX_PC; i++) {
						if (__wcprimcollwgt(i) ==
						    pcoll) {
							uwgt =
							    __wcuniqcollwgt(i);
							if (uwgt == ucoll_s)
								found = 1;
							if (uwgt < min_ucoll2)
								min_ucoll2 =
								    uwgt;
							if (uwgt > max_ucoll2)
								max_ucoll2 =
								    uwgt;
						}
					}
				}
				pb = pi + LEN_B + 2;
				break;
#endif /* defined(_C_COLL) */
			}
/*
 * collation symbol
 *   locate address of collation weight table, error if none
 *   verify collation symbol is entire contents of [. .] expression, error if
 *   not get unique collation weight, error if none
 *   set found flag if collation weight matches that of string collating element
 */
			else if (type == '.') {
#if defined(_C_COLL)
				if (*pi == '\0' || pi[1] != '.' || pi[2] != ']')
					return (-1);
				if ((min_ucoll = (wchar_t)*pi) == wc_s)
					found = 1;
				max_ucoll = min_ucoll;

				/*
				 * If case folding is requested, try to
				 * convert to the other case, compare to see
				 * if it matches to, and save the pattern
				 * character for later use.
				 */
				if (!found && (flags & FNM_IGNORECASE) != 0 &&
				    (tmpwc = fnm_casemap(*pi)) != 0) {
					max_ucoll2 = min_ucoll2 = tmpwc;

					if (min_ucoll2 == wc_s)
						found = 1;
				}
				pb = pi + 3;
				break;
#else  /* SB or STANDARD METHOD */
coll_sym:
				min_ucoll = _mbucoll(phdl, (char *)pi,
				    (char **)&piend);
				if ((min_ucoll < MIN_UCOLL) ||
				    (min_ucoll > MAX_UCOLL) ||
				    (*piend != type) || (piend[1] != ']'))
					return (-1);
				if (min_ucoll == ucoll_s)
					found = 1;
				max_ucoll = min_ucoll;

				/*
				 * Check if this is a multi-character collating
				 * symbol or not. If not, do case-insensitive
				 * comparison and save the unique collation
				 * weight for later use.
				 */
				if (!found && (flags & FNM_IGNORECASE) != 0) {
					mblen_b = MBTOWC_NATIVE(&wc_b,
					    (const char *)pi, mbcurmax);

					if ((piend - pi) == mblen_b &&
					    (ret = fnm_casemap(phdl, &wc_b, 0))
					    != 0) {
						if (ret == -1)
							return (-1);

						max_ucoll2 = min_ucoll2 = wc_b;

						if (min_ucoll2 == ucoll_s)
							found = 1;
					}
				}
				pb = piend + 2;
				break;
#endif
			} else {
				goto ordinary;
			}
		} /* end of switch */
/*
 * Check for the completion of a range expression and determine
 * whether string collating element falls between end points
 */
		if (dash != 0) {
			dash = 0;
			if (prev_min_ucoll == 0 || prev_min_ucoll > max_ucoll)
				return (-1);
			if (ucoll_s >= prev_min_ucoll && ucoll_s <= max_ucoll)
				found = 1;
			/*
			 * We check on the ucoll_s only if we have meaningful
			 * min and max ucoll values for the range of the other
			 * case.
			 */
			else if ((flags & FNM_IGNORECASE) != 0 &&
			    prev_min_ucoll2 != 0 &&
			    prev_min_ucoll2 <= max_ucoll2 &&
			    ucoll_s >= prev_min_ucoll2 && ucoll_s <= max_ucoll2)
				found = 1;

			min_ucoll = min_ucoll2 = 0;
		}
	} /* end of while */
/*
 * Return < 0 since <NUL> was found
 */
	return (-1);
}

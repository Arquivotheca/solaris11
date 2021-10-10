/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * COMPONENT_NAME: (LIBCPAT) Standard C Library Pattern Functions
 *
 * FUNCTIONS: __regcomp_C
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
#include <sys/localedef.h>
#include <ctype.h>
#include <limits.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include "reglocal.h"
#include <locale.h>
#include "patlocal.h"

typedef uchar_t	uchar;

#define	UDIFF(p1, p2)    (((uchar_t *)(p1)) - ((uchar_t *)(p2)))


/*
 *	__reg_bits[] is also defined in __regcomp_std.c, __regexec_C.c,
 *	and __regexec_std.c.  When updating the following definition,
 *	make sure to update others
 */

static const int	__reg_bits[] = { /* bitmask for [] bitmap */
	0x00000001, 0x00000002, 0x00000004, 0x00000008,
	0x00000010, 0x00000020, 0x00000040, 0x00000080};

/*
 * RE compilation definitions
 */

/* error if expression follows ERE $ */
#define	EOL_CHECK	{ \
	if (ere != 0) { \
		if (sxp[idx].eol != 0) { \
			preg->re_erroff = sxp[idx].eol - 1; \
			ALLFREE; \
			return (REG_EEOL); \
		} \
	} \
}

/* expand pattern if buffer too small */
#define	OVERFLOW(x)	{ \
	if (pe - pp < x) { \
		enlarge(x, &pp_start, &pe, &pp, &plastce); \
		if (pp_start == NULL) { \
			preg->re_erroff = (uchar_t *)ppat - \
				(uchar_t *)pattern - 1; \
			ALLFREE; \
			return (REG_ESPACE); \
	    } \
	} \
}

#define	ALLFREE	\
	free(pp_start); free(rext->re_lsub); free(rext->re_esub); \
	free(rext); free(sxp); free(subidx)

#define	PATTERN_EXP	128	/* compiled pattern expansion (bytes) */

#define	MAX_A_PATTERN_LEN	16

/* set character bit in C [] bitmap */
#define	SETBITC(pp, c) \
	{*(pp + (c >> 3)) |= __reg_bits[c & 7]; }


/*
 * Internal function prototypes
 */

/* convert [bracket] to bitmap */
static int	bracket(uchar_t *, uchar_t **, uchar_t *, int);

/*
 * enlarge compiled pattern buffer
 * enlarge() is also referred by __regcomp_std.c
 */
void	enlarge(size_t, uchar_t **, uchar_t **, uchar_t **, uchar_t **);

/*
 * remove interval, uncouple last STRING char
 * repeat_check() is also referred by __regcomp_std.c
 */
void	repeat_check(uchar_t **, uchar_t **);

/*
 * __regcomp_C()- Compile RE pattern
 *		  - valid for C locale
 *
 *		  - phdl	ptr to __lc_collate table
 *		  - preg	ptr to structure for compiled pattern
 *		  - pattern	ptr to RE pattern
 *		  - cflags	regcomp() flags
 */
/*ARGSUSED*/
int
__regcomp_C(_LC_collate_t *phdl, regex_t *preg, const char *pattern,
	int cflags)
{
	uchar_t	c;		/* pattern character */
	uchar_t	c2;		/* opposite case pattern character */
	int	do_all;		/* set if {m,} is used. */
	int	ere;		/* extended RE flag */
	int	first;		/* logical beginning of pattern */
	int	first_BOL;	/* set when the first ^ is found */
	int	i;		/* loop index */
	int	icase;		/* ignore case flag */
	int	idx;		/* current subexpression index */
	int	isfirst;	/* first expression flag */
	int	maxri;		/* mamimum repetition interval */
	int	minri;		/* minimum repetition interval */
	int	nsub;		/* highest subexpression index */
	uchar_t	*pe;		/* ptr to end of compiled pattern space */
	uchar_t	*pfrom;		/* expand pattern ptr */
	uchar_t	*plastce;	/* ptr to last compiled expression */
	uchar_t	*pmap;		/* ptr to character map table */
	uchar_t	*pp;		/* ptr to next compiled RE pattern slot */
	uchar_t	*pp_start;	/* ptr to start of compiled RE pattern */
	uchar_t	*ppat;		/* ptr to next RE pattern byte */
	uchar_t	*pri;		/* ptr to repetition interval */
	uchar_t	*pto;		/* expand pattern ptr */
	int	stat;		/* bracket() return status */
	struct _regex_ext_t	*rext;
	subexp_t	*sxp;
	ushort_t	*subidx;
	ushort_t	*psubidx;	/* ptr to current subidx entry */
	size_t	subexp_alloc = _SUBEXP_ALLOC;

/*
 * Allocate initial RE compiled pattern buffer
 * OVERFLOW(X) will expand buffer as required
 */
	pp = malloc(PATTERN_EXP);
	if (!pp) {
		preg->re_erroff = 0;
		return (REG_ESPACE);
	}
	pp_start = pp;
	pe = pp + PATTERN_EXP - 1;
/*
 * Other initialization
 */
	(void) memset(preg, 0, sizeof (regex_t));
	preg->re_sc = calloc(1, sizeof (struct _regex_ext_t));
	if (!preg->re_sc) {
		free(pp);
		return (REG_ESPACE);
	}
	/*
	 * Initialize memory for sub expressions
	 */
	rext = preg->re_sc;
	rext->re_lsub = malloc(sizeof (void *) * subexp_alloc);
	if (!rext->re_lsub) {
		free(pp);
		free(rext);
		return (REG_ESPACE);
	}
	(void) memset(rext->re_lsub, 0, sizeof (void *) * subexp_alloc);
	rext->re_esub = malloc(sizeof (void *) * subexp_alloc);
	if (!rext->re_esub) {
		free(pp);
		free(rext->re_lsub);
		free(rext);
		return (REG_ESPACE);
	}
	(void) memset(rext->re_esub, 0, sizeof (void *) * subexp_alloc);
	sxp = malloc(sizeof (subexp_t) * subexp_alloc);
	if (!sxp) {
		free(pp);
		free(rext->re_lsub);
		free(rext->re_esub);
		free(rext);
		return (REG_ESPACE);
	}
	subidx = malloc(sizeof (ushort_t) * subexp_alloc);
	if (!subidx) {
		free(pp); free(rext->re_lsub); free(rext->re_esub);
		free(rext); free(sxp);
		return (REG_ESPACE);
	}

	preg->re_cflags = cflags;
	icase = cflags & REG_ICASE;
	ere = cflags & REG_EXTENDED;
	nsub = 0;
	plastce = NULL;
	preg->re_sc->re_lsub[0] = 0;
	psubidx = subidx;
	*psubidx = 0;
	sxp[0].altloc = 0;
	idx = 0;
	first = 0;
	first_BOL = 0;
	isfirst = 0;
	sxp[0].eol = 0;
	sxp[0].sol = 0;
	pmap = preg->re_sc->re_map;
/*
 * BIG LOOP to process all characters in RE pattern
 * stop on NUL
 * return on any error
 * set character map for all characters which satisfy the pattern
 * expand pattern space now if large element won't fit
 */
	ppat = (uchar_t *)pattern;
	while ((c = *ppat++) != '\0') {
		OVERFLOW(MAX_A_PATTERN_LEN);
		switch (c) {
			uchar_t	*palt;	/* expand pattern ptr */
			size_t altdiff;
/*
 * match a single character
 *   error if preceded by ERE $
 *   if case sensitive pattern
 *     if single byte character
 *       if no previous pattern, add CC_CHAR code to pattern
 *       if previous pattern is CC_CHAR, convert to CC_STRING
 *       if previous pattern is CC_STRING, add to end of string
 *       otherwise add CC_CHAR code to pattern
 *
 *   if ignore case pattern
 *     if single byte character
 *       determine opposite case of pattern character
 *       if no opposite case, treat as case sensitive
 *       if no previous pattern, add CC_I_CHAR code to pattern
 *       if previous pattern is CC_I_CHAR, convert to CC_I_STRING
 *       if previous pattern is CC_I_STRING, add to end of string
 *       otherwise add CC_I_CHAR code to pattern
 */

		default:
		cc_char:
			EOL_CHECK
			if (icase == 0) {
				if (plastce == NULL) {
					plastce = pp;
					*pp++ = CC_CHAR;
					*pp++ = c;
					if (isfirst++ == 0)
						pmap[c] = 1;
				} else if (*plastce == CC_CHAR) {
					*plastce = CC_STRING;
					*pp++ = plastce[1];
					plastce[1] = 2;
					*pp++ = c;
				} else if (*plastce == CC_STRING &&
				    plastce[1] < 255) {
					plastce[1]++;
					*pp++ = c;
				} else {
					plastce = pp;
					*pp++ = CC_CHAR;
					*pp++ = c;
					if (isfirst++ == 0)
						pmap[c] = 1;
				}
			} else {
				c2 = toupper(c);
				if (c2 == c)
					c2 = tolower(c);
				if (plastce == NULL) {
					plastce = pp;
					*pp++ = CC_I_CHAR;
					*pp++ = c;
					*pp++ = c2;
					if (isfirst++ == 0) {
						pmap[c] = 1;
						pmap[c2] = 1;
					}
				} else if (*plastce == CC_I_CHAR) {
					*plastce = CC_I_STRING;
					*pp++ = plastce[2];
					plastce[2] = plastce[1];
					plastce[1] = 2;
					*pp++ = c;
					*pp++ = c2;
				} else if (*plastce == CC_I_STRING &&
				    plastce[1] < 255) {
					plastce[1]++;
					*pp++ = c;
					*pp++ = c2;
				} else {
					plastce = pp;
					*pp++ = CC_I_CHAR;
					*pp++ = c;
					*pp++ = c2;
					if (isfirst++ == 0) {
						pmap[c] = 1;
						pmap[c2] = 1;
					}
				}
			}
			first++;
			continue;
/*
 * If we can use the smaller CC_BITMAP, use it:
 *   bracket expression
 *     always use 256-bit bitmap - indexed by file code
 *     decode pattern into list of characters which satisfy the [] expression
 *     error if invalid [] expression
 *     add CC_BITMAP to pattern
 *     set character map for each bit set in bitmap
 * otherwise
 *   ILS bracket expression
 *     bitmap size is based upon min/max unique collating value
 *     zero fill bitmap - yes its big for kanji
 *     error if invalid bracket expression
 */
		case '[':
			EOL_CHECK
			OVERFLOW(BITMAP_LEN+1);
			plastce = pp;
			*pp++ = CC_BITMAP;
			(void) memset((char *)pp, 0, BITMAP_LEN);
			stat = bracket(ppat, &pto, pp, cflags);
			if (stat != 0) {
				preg->re_erroff = UDIFF(pto, pattern) - 1;
				ALLFREE;
				return (stat);
			}
			ppat = pto;
			pto = pp;
			pp += BITMAP_LEN;
			if (isfirst++ == 0) {
				pfrom = pmap;
				do {
	if (*pto != 0) {
		for (i = 0; i < 8; i++) {
			if ((*pto & __reg_bits[i]) != 0) {
				pfrom[i] = 1;
			}
		}
	}
	pfrom += 8;
				} while (++pto < pp);
			}
			first++;
			continue;
/*
 * zero or more matches of previous expression
 *   error if no valid previous expression for ERE
 *   ordinary character if no valid previous expression for BRE
 *   specify CR_STAR for previous expression repeat factor
 */
		case '*':
			if (plastce == NULL) {
				if (ere == 0) {
					goto cc_char;
				} else {
					preg->re_erroff =
					    UDIFF(ppat, pattern) - 1;
					ALLFREE;
					return (REG_BADRPT);
				}
			}
			repeat_check(&plastce, &pp);
			isfirst = 0;
			*plastce = (*plastce & ~CR_MASK) | CR_STAR;
			continue;
/*
 * match any character except NUL
 *   error if preceded by ERE $
 *   add CC_DOT code to pattern if REG_NEWLINE is not set & single byte locale
 *   add CC_DOTREG code to pattern if REG_NEWLINE is set & single byte locale
 *   set all map bits
 */
		case '.':
			EOL_CHECK
			plastce = pp;
			if ((cflags & REG_NEWLINE) != 0)
				*pp++ = CC_DOTREG;
			else
				*pp++ = CC_DOT;
			if (isfirst++ == 0)
				(void) memset(pmap, (int)1, (size_t)256);
			first++;
			continue;
/*
 * match beginning of line
 *	error if preceded by ERE $
 *	ordinary character if not
 *         first thing in BRE
 *   ordinary character if inside of the subexpression in BRE
 *   add CC_BOL to pattern
 *   set all map bits
 */
		case '^':
			EOL_CHECK
			if ((first || nsub) && ere == 0)
				goto cc_char;

			if (first_BOL && ere == 0 && *(pp-1) == CC_BOL) {
				first++;
				goto cc_char;
			}

			if (isfirst++ == 0) {
				plastce = NULL;
				(void) memset(pmap, (int)1, (size_t)256);
			}
			first_BOL++;
			*pp++ = CC_BOL;
			continue;
/*
 * match end of line
 *   error if preceded by ERE $
 *   ordinary character if inside of the subexpression in BRE
 *   ordinary character if not last thing in BRE
 *   save $ offset in pattern for later testing and error reporting
 *   add CC_EOL to pattern
 */
		case '$':
/* EOL_CHECK */
			if ((ere == 0) && (*ppat != '\0') &&
			    ((*ppat != '\\') || nsub))
				goto cc_char;
			sxp[idx].eol = UDIFF(ppat, pattern);
			plastce = NULL;
			*pp++ = CC_EOL;
			if (isfirst++ == 0) {
				if ((cflags & REG_NEWLINE) != 0)
					pmap['\n'] = 1;
				pmap[0] = 1;
			}
			continue;
/*
 * backslash
 *   error if followed by NUL
 *   protects next ERE character except < and >
 *   introduces special BRE characters
 *     processing is based upon next character
 *       (     start subexpression
 *       )     end subexpression
 *       {     repetition interval
 *       1-9   backreference
 *       other ordinary character
 */
		case '\\':
			c = *ppat++;
			if (c == 0) {
				preg->re_erroff = UDIFF(ppat, pattern) - 1;
				ALLFREE;
				return (REG_EESCAPE);
			}
			/*
			 * if in ERE mode and if the following char
			 * is not '<' or '>', the backslash just
			 * protects the following char.
			 */
			if ((ere != 0) && (c != '<') && (c != '>'))
				goto cc_char;
			switch (c) {
/*
 * start subexpression
 *   error if too many subexpressions
 *   save start information concerning this subexpression
 *   add CC_SUBEXP to pattern
 *     subexpression data follows up to ending CC_SUBEXP_E
 */
			case '(':
			lparen:
				if (nsub + 1 == subexp_alloc) {
					void	*t;
					ptrdiff_t	tidx;
					/*
					 * SUBEXP overflow
					 * realloc memories
					 */
					if (subexp_alloc == _SUBEXP_MAX) {
						preg->re_erroff =
						    UDIFF(ppat, pattern) - 1;
						ALLFREE;
						return (REG_BADPAT);
					}
					subexp_alloc += _SUBEXP_ALLOC;
					if (subexp_alloc >= _SUBEXP_MAX) {
						subexp_alloc = _SUBEXP_MAX;
					}
					t = realloc(rext->re_lsub,
					    sizeof (void *) * subexp_alloc);
					if (!t) {
						ALLFREE;
						return (REG_ESPACE);
					} else {
						rext->re_lsub = t;
					}
					t = realloc(rext->re_esub,
					    sizeof (void *) * subexp_alloc);
					if (!t) {
						ALLFREE;
						return (REG_ESPACE);
					} else {
						rext->re_esub = t;
					}
					t = realloc(sxp, sizeof (subexp_t) *
					    subexp_alloc);
					if (!t) {
						ALLFREE;
						return (REG_ESPACE);
					} else {
						sxp = (subexp_t *)t;
					}
					tidx = psubidx - subidx;
					t = realloc(subidx, sizeof (ushort_t) *
					    subexp_alloc);
					if (!t) {
						ALLFREE;
						return (REG_ESPACE);
					} else {
						subidx = (ushort_t *)t;
					}
					psubidx = subidx + tidx;
				}
				nsub++;
				*++psubidx = (ushort_t)nsub;
				sxp[nsub].eol = 0;
				sxp[nsub].altloc = 0;
				if (first == 0)
					sxp[nsub].sol = 0;
				else
					sxp[nsub].sol = 1;
				idx = nsub;
				plastce = NULL;
				*pp++ = CC_SUBEXP;
				*pp++ = (uchar_t)((nsub >> 8) & 0xff);
				*pp++ = (uchar_t)(nsub & 0xff);
				preg->re_sc->re_lsub[nsub] =
				    (void *)(pp - pp_start);
				preg->re_sc->re_esub[nsub] = NULL;
				continue;
/*
 * end subexpression
 *   error if no matching start subexpression
 *   save end information concerning this subexpression
 *   add CC_SUBEXP_E to pattern
 */
			case ')':
			rparen:
				if (--psubidx < subidx) {
					if ((*ppat == '\0' || nsub == 0) &&
					    ere) {
						psubidx = subidx;
						goto cc_char;
					}
					preg->re_erroff =
					    UDIFF(ppat, pattern) - 1;
					ALLFREE;
					return (REG_EPAREN);
				}
				preg->re_sc->re_esub[idx] =
				    (void *)(pp - pp_start);
				plastce = pp;
				*pp++ = CC_SUBEXP_E;
				*pp++ = (uchar_t)((idx >> 8) & 0xff);
				*pp++ = (uchar_t)(idx & 0xff);
				idx = *psubidx;
				first++;
				continue;
/*
 * repetition interval match of previous expression
 *   treat characters as themselves if no previous expression
 *   \{m\}   matches exactly m occurances
 *   \{m,\}  matches at least m occurances
 *   \{m,n\} matches m through n occurances
 *   error if invalid sequence or previous expression already has * or {}
 *   insert two bytes for min/max after pattern code
 *   specify CR_INTERVAL for previous expression repeat factor
 */
			case '{':
				do_all = 0;
				if (plastce == NULL) {
					c = '\\';
					ppat--;
					goto cc_char;
				}
				pri = ppat;
				minri = 0;

				while ((c2 = *pri++) >= '0' && c2 <= '9')
					minri = minri * 10 + c2 - '0';
				/* first, lets check if we didn't */
				/* convert anything */
				if ((pri == ppat+1) || (c2 == '\0')) {
					preg->re_erroff = UDIFF(ppat, pattern);
					ALLFREE;
					return ((c2 == '\0') ? REG_EBRACE :
					    REG_BADBR);
				}
				if (c2 == '\\' && *pri == '}') {
					pri++;
					maxri = minri;
				} else if (c2 != ',') {
					preg->re_erroff =
					    UDIFF(pri, pattern) - 1;
					ALLFREE;
					return (REG_BADBR);
				} else if (*pri == '\\' && pri[1] == '}') {
					pri += 2;
					do_all = 1;
					maxri = minri;
				} else {
					maxri = 0;
					while ((c2 = *pri++) >= '0' &&
					    c2 <= '9')
						maxri = maxri * 10 + c2 - '0';
					if (c2 != '\\' || *pri != '}') {
						preg->re_erroff =
						    UDIFF(pri, pattern) - 1;
						ALLFREE;
						return ((c2 == '\0') ?
						    REG_EBRACE :
						    REG_BADBR);
					}
					pri++;
				}
				if (minri > maxri || maxri > RE_DUP_MAX ||
				    *pri == '*' ||
				    (*plastce & CR_MASK) != 0) {
					preg->re_erroff = UDIFF(ppat, pattern);
					ALLFREE;
					return (REG_BADBR);
				}
				maxri -= minri;
				ppat = pri;
				repeat_check(&plastce, &pp);
				/*
				 * To insert minri and maxri after
				 * the last CE, perform the following
				 * move
				 */
				(void) memmove(plastce + 3, plastce + 1,
				    pp - (plastce + 1));
				pp += 2;

				if (do_all)
					*plastce = (*plastce & ~CR_MASK) |
					    CR_INTERVAL_ALL;
				else
					*plastce = (*plastce & ~CR_MASK) |
					    CR_INTERVAL;
				plastce[1] = (uchar_t)minri;
				plastce[2] = (uchar_t)maxri;
				if (minri == 0)
					isfirst = 0;
				continue;
/*
 * subexpression backreference
 *   error if subexpression not completed yet
 *   add CC_BACKREF to pattern if case sensitive
 *   add CC_I_BACKREF to pattern if ignore case
 */
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				c -= '0';
				if (c > _REG_BACKREF_MAX ||
				    preg->re_sc->re_esub[c] == NULL) {
					preg->re_erroff =
					    UDIFF(ppat, pattern) - 1;
					ALLFREE;
					return (REG_ESUBREG);
				}
				plastce = pp;
				if (icase == 0)
					*pp++ = CC_BACKREF;
				else
					*pp++ = CC_I_BACKREF;
				*pp++ = c;
				first++;
				continue;

			case '<':
			case '>':
				plastce = pp;
				*pp++ = CC_WORDB;
				*pp++ = c;
				continue;
/*
 * not a special character
 *   treat as ordinary character
 */
			default:
				goto cc_char;
			}
/*
 * start subexpression for ERE
 *   do same as \( for BRE
 *   treat as ordinary character for BRE
 */
		case '(':
			EOL_CHECK
			if (ere != 0)
				goto lparen;
			goto cc_char;
/*
 * end subexpression for ERE
 *   do same as \) for BRE
 *   treat as ordinary character for BRE
 */
		case ')':
			/* originally, the following was if (ere != 0) */
			if ((ere != 0) && (psubidx > subidx))
				goto rparen;
			goto cc_char;
/*
 * zero or one match of previous expression
 *   ordinary character for BRE
 *   error if no valid previous expression
 *   ignore if previous expression already has *
 *   specify CR_QUESTION for previous expression repeat factor
 */
		case '?':
			if (ere == 0)
				goto cc_char;
			if (plastce == NULL) {
				preg->re_erroff =
				    UDIFF(ppat, pattern) - 1;
				ALLFREE;
				return (REG_BADRPT);
			}
			if ((uchar)((*plastce & CR_MASK)) > CR_QUESTION)
				continue;
			repeat_check(&plastce, &pp);
			*plastce = (*plastce & ~CR_MASK) | CR_QUESTION;
			isfirst = 0;
			continue;
/*
 * one or more matches of previous expression
 *   ordinary character for BRE
 *   error if no valid previous expression
 *   ignore if previous expression already has * or ?
 *   specify CR_PLUS for previous expression repeat factor
 */
		case '+':
			if (ere == 0)
				goto cc_char;
			if (plastce == NULL) {
				preg->re_erroff =
				    UDIFF(ppat, pattern) - 1;
				ALLFREE;
				return (REG_BADRPT);
			}
			if ((uchar)((*plastce & CR_MASK)) > CR_PLUS)
				continue;
			repeat_check(&plastce, &pp);
			*plastce = (*plastce & ~CR_MASK) | CR_PLUS;
			continue;
/*
 * repetition interval match of previous expression
 *   ordinary character for BRE
 *   {m}   matches exactly m occurances
 *   {m,}  matches at least m occurances
 *   {m,n} matches m through n occurances
 *   treat characters as themselves if invalid sequence
 *   ignore if previous expression already has * or ? or +
 *   error if valid {} does not have previous expression
 *   insert two bytes for min/max after pattern code
 *   specify CR_INTERVAL for previous expression repeat factor
 */
		case '{':
			do_all = 0;
			if (ere == 0)
				goto cc_char;
			pri = ppat;
			minri = 0;
			while ((c2 = *pri++) >= '0' && c2 <= '9')
				minri = minri * 10 + c2 - '0';
			if (pri == ppat+1) {
/*
 *			goto cc_char;
 * XPG4 says that '{' is undefined for ERE's if it is not part of a valid
 * repetition interval, so we're going back to treating it as a normal char
 * but this may change in a later release of XPG.  If XPG changes its mind
 * and decides it should return an error, this is what should be done
 * (instead of "goto cc_char;") :
 */
				preg->re_erroff =
				    UDIFF(ppat, pattern);
				ALLFREE;
				return (REG_BADPAT);
			}
			if (c2 == '}') {
				maxri = minri;
			} else if (c2 != ',') {
				preg->re_erroff =
				    UDIFF(pri, pattern) - 1;
				ALLFREE;
				return (REG_BADBR);
			} else if (*pri == '}') {
				do_all = 1;
				maxri = minri;
				pri++;
			} else {
				maxri = 0;
				while ((c2 = *pri++) >= '0' && c2 <= '9')
					maxri = maxri * 10 + c2 - '0';
				if (c2 != '}') {
					preg->re_erroff =
					    UDIFF(pri, pattern) - 1;
					ALLFREE;
					return (REG_BADBR);
				}
			}
			if (minri > maxri || maxri > RE_DUP_MAX) {
				preg->re_erroff =
				    UDIFF(ppat, pattern);
				ALLFREE;
				return (REG_BADBR);
			}
			maxri -= minri;
			if (plastce == NULL) {
				preg->re_erroff =
				    UDIFF(ppat, pattern) - 1;
				ALLFREE;
				return (REG_BADBR);
			}
			ppat = pri;
			if ((uchar)((*plastce & CR_MASK)) > CR_INTERVAL_ALL)
				continue;
			repeat_check(&plastce, &pp);
			/*
			 * To insert minri and maxri after
			 * the last CE, perform the following
			 * move
			 */
			(void) memmove(plastce + 3, plastce + 1,
			    pp - (plastce + 1));
			pp += 2;

			if (do_all)
				*plastce = (*plastce & ~CR_MASK) |
				    CR_INTERVAL_ALL;
			else
				*plastce = (*plastce & ~CR_MASK) | CR_INTERVAL;
			plastce[1] = (uchar_t)minri;
			plastce[2] = (uchar_t)maxri;
			if (minri == 0)
				isfirst = 0;
			continue;
/*
 * begin alternate expression
 *   treat <vertical-line> as normal character if
 *     1) BRE
 *     2) not followed by another expression
 *     3) beginning of pattern
 *     4) no previous expression
 *   insert leading CC_ALTERNATE if this is first alternative at this level
 *      compensate affected begin/end subexpression offsets
 *   compute delta offset from last CC_ALTERNATE to this one
 *   add CC_ALTERNATE_E to pattern, terminating previous alternative
 *   add CC_ALTERNATE to pattern, starting next alternative
 *   indicate now at end-of-line position
 *   indicate now at beginning-of-line if not blocked by previous expression
 */
		case '|':
			if (ere == 0 || *ppat == ')' || *ppat == '\0' ||
			    ppat == (uchar_t *)pattern+1 ||
			    (plastce == NULL && ppat[-2] != '^' &&
			    ppat[-2] != '$'))
				goto cc_char;
			palt = pp_start + (size_t)preg->re_sc->re_lsub[idx];
			if (sxp[idx].altloc == 0) {
				/* BEGIN CSTYLED */
				/*
				 * palt
				 *  |
				 *  v
				 * CRE0, CRE1, ....., CREn, XXX
				 *                           ^
				 *                           |
				 *                           pp
				 *
				 * At this moment, palt points to CRE0 and
				 * pp points to XXX.
				 * 5 (_ALT_LEN) bytes of CC_ALT, off0, off1,
				 * off2, and off3 are inserted into the current
				 * palt, and 3 (_ALT_E_LEN) bytes of CC_ALT_E,
				 * idx0, and idx1 are appended after CREn.
				 * Then, the terminator (CC_ALT, 0, 0, 0, 0)
				 * is appended after idx1.
				 * So, pp will be incremented by
				 * _ALT_LEN + _ALT_E_LEN + _ALT_LEN, and will
				 * point to the next byte of the terminator.
				 *
				 * altdiff becomes the number of bytes
				 * from CRE0 to CREn.
				 *
				 * The result will be as follows:
				 *
				 * palt
				 *  |
				 *  V
				 * CC_ALT, off0, off1, off2, off3,
				 * CRE0, CRE1, ...., CREn,
				 * CC_ALT_E, idx0, idx1,
				 * CC_ALT, 0, 0, 0, 0, XXX
				 *                      ^
				 *                      |
				 *                      pp
				 */
				/* END CSTYLED */
				altdiff = pp - palt;
				(void) memmove(palt + _ALT_LEN, palt, altdiff);

				pp += _ALT_LEN;
				*palt = CC_ALTERNATE;
				palt[1] = 0;
				palt[2] = 0;
				palt[3] = 0;
				palt[4] = 0;
				if (psubidx == subidx) {
					for (i = 1; i <= nsub; i++) {
		preg->re_sc->re_lsub[i] = (void *)((size_t)
		    (preg->re_sc->re_lsub[i]) + _ALT_LEN);
		preg->re_sc->re_esub[i] = (void *)((size_t)
		    (preg->re_sc->re_esub[i]) + _ALT_LEN);
					}
				} else {
					for (i = *psubidx; i <= nsub; i++)
						if (preg->re_sc->re_esub[i] !=
						    NULL) {
		preg->re_sc->re_lsub[i] = (void *)((size_t)
		    (preg->re_sc->re_lsub[i]) + _ALT_LEN);
		preg->re_sc->re_esub[i] = (void *)((size_t)
		    (preg->re_sc->re_esub[i]) + _ALT_LEN);
						}
				}
			} else {
				/* BEGIN CSTYLED */
				/*
				 * palt
				 *  |
				 *  V
				 * CC_ALT, off0, off1, off2, off3,
				 * CRE0, CRE1, ...., CREn, XXX
				 *                          ^
				 *                          |
				 *                         pp
				 *
				 * At this moment, palt points to CC_ALT and
				 * pp points to XXX.
				 * So, 3 (_ALT_E_LEN) bytes of CC_ALT_E, idx0,
				 * and idx1 are appended to the current pp.
				 * Then, the terminator (CC_ALT, 0, 0, 0, 0)
				 * is appended after idx1.
				 * So, pp will be incremented by
				 * _ALT_E_LEN + _ALT_LEN, and will point to
				 * the next byte of the terminator.
				 *
				 * altdiff will be the number of bytes
				 * from CRE0 to CREn.
				 *
				 * The result will be as follows:
				 *
				 * palt
				 *  |
				 *  V
				 * CC_ALT, off0, off1, off2, off3,
				 * CRE0, CRE1, ...., CREn,
				 * CC_ALT_E, idx0, idx1,
				 * CC_ALT, 0, 0, 0, 0, XXX
				 *                      ^
				 *                      |
				 *                      pp
				 */
				/* END CSTYLED */
				palt = sxp[idx].altloc + pp_start;
				altdiff = pp - (palt + _ALT_LEN);
			}
#ifdef	_LP64
			if (altdiff > _ALTEXP_MAX) {
				preg->re_erroff = UDIFF(ppat, pattern) - 1;
				ALLFREE;
				return (REG_BADPAT);
			}
#endif
#ifdef	_RDEBUG
			(void) fprintf(stderr, "comp:altdiff: %lx\n", altdiff);
#endif
			palt[1] = (uchar_t)((altdiff >> 24) & 0xff);
			palt[2] = (uchar_t)((altdiff >> 16) & 0xff);
			palt[3] = (uchar_t)((altdiff >> 8) & 0xff);
			palt[4] = (uchar_t)(altdiff & 0xff);
			*pp++ = CC_ALTERNATE_E;
			*pp++ = (uchar_t)((idx >> 8) & 0xff);
			*pp++ = (uchar_t)(idx & 0xff);
			sxp[idx].altloc = pp - pp_start;
			*pp++ = CC_ALTERNATE;
			*pp++ = 0;
			*pp++ = 0;
			*pp++ = 0;
			*pp++ = 0;
			plastce = NULL;
			sxp[idx].eol = 0;
			if (sxp[idx].sol == 0) {
				first = 0;
				isfirst = 0;
			}
			continue;
		} /* end of switch */
	} /* end of while */
/*
 * Return error if missing ending subexpression
 */
	if (psubidx != subidx) {
		preg->re_erroff = UDIFF(ppat, pattern) - 1;
		ALLFREE;
		return (REG_EPAREN);
	}
/*
 * Set all map bits to prevent regexec() failure if
 * "first" expression not defined yet
 *   1) empty pattern
 *   2) last expression has *, ?, or {0,}
 */
	if (isfirst == 0)
		(void) memset(pmap, (int)1, (size_t)256);
/*
 * No problems so add trailing end-of-pattern compile code
 * There is always suppose to be room for this
 */
	*pp++ = CC_EOP;
/*
 * Convert beginning/ending subexpression offsets to addresses
 * Change first subexpression expression to start of subexpression
 */
	preg->re_sc->re_lsub[0] = pp_start;
	preg->re_sc->re_esub[0] = pp - 1;
	for (i = 1; i <= nsub; i++) {
		preg->re_sc->re_lsub[i] = pp_start +
		    (size_t)preg->re_sc->re_lsub[i] - _SUBEXP_E_LEN;
		preg->re_sc->re_esub[i] = pp_start +
		    (size_t)preg->re_sc->re_esub[i];
	}
/*
 * Define remaining RE structure and return status
 */
	preg->re_comp = (void *)pp_start;
	preg->re_len = pp - pp_start;
	preg->re_sc->re_ac_nsub = nsub;
	if ((cflags & REG_NOSUB) == 0)
		preg->re_nsub = nsub;
	free(sxp);
	free(subidx);
	return (0);
}


/*
 * bracket	- convert [] expression into compiled RE pattern
 *
 *		- ppat		ptr to pattern
 *		- pnext		ptr to pattern address following []
 *		- pp		ptr to compiled RE pattern
 *		- cflags	__regcomp() flags
 */
static int
bracket(uchar_t *ppat, uchar_t **pnext, uchar_t *pp, int cflags)
{
	int	c;		/* file code of pattern character */
	int	c2;		/* file code of character opposite case */
	char	class[CLASS_SIZE+1]; /* [: :] text with terminating NUL */
	int	dash;		/* in the middle of a range expression */
	int	i;		/* loop index */
	int	icase;		/* ignore case flag */
	int	neg;		/* nonmatching bitmap */
	uchar_t	*pb;		/* ptr to [] expression */
	char	*pclass;	/* ptr to class */
	uchar_t	*pend;		/* ptr to end point in range expression */
	int	prev;		/* previous character for range expr */
	uchar_t	*pxor;		/* nonmatching xor bitmap ptr */
	wctype_t wh;		/* character class handle for wctype */

/*
 * Check for nonmatching expression which has a leading <circumflex>
 */
	icase = cflags & REG_ICASE;
	pb = ppat;
	neg = 0;
	if (*pb == '^') {
		pb++;
		neg++;
	}
/*
 * Check for leading <hyphen> or <right-bracket> which is not the [] terminator
 */
	dash = 0;
	prev = 0;
	if (*pb == '-') {
		prev = *pb++;
		SETBITC(pp, prev);
	} else if (*pb == ']') {
		prev = *pb++;
		SETBITC(pp, prev);
	}
/*
 * BIG LOOP to process all characters in [] expression
 * stop on ]
 * return on any error
 * next character can begin any of the following:
 *   a) any single character (default)
 *   b) equivalence character [= =] (only mathces specified character)
 *   c) collating symbol [. .] (assumes only one single byte character)
 *   d) character class [: :]
 */
	while ((c = *pb++) != '\0') {
		switch (c) {
/*
 * single character
 *   set bitmap bit associated with character's file code
 *   if ignore case, also set bit of opposite case character
 */
		default:
		one_char:
			SETBITC(pp, c);
			if (icase != 0) {
				if ((c2 = toupper(c)) == c)
					c2 = tolower(c);
				SETBITC(pp, c2);
			}
			break;
/*
 * [] terminator
 *   set bit for <minus> if expression ends with -]
 *   negate bitmap if nonmatching [] expression and clear
 *     newline bit if REG_NEWLINE is set
 *   clear NUL bit to disallow match of NUL in string
 *   return ptr to next character after ]
 */
		case ']':
			if (dash != 0)
				SETBITC(pp, dash);
			if (neg != 0) {
				for (pxor = pp + BITMAP_LEN - 1;
				    pxor >= pp; pxor--)
					*pxor = ~*pxor;
				*pp &= 0xfe;
				if ((cflags & REG_NEWLINE) != 0)
					pp[1] &= 0xfb;
			}
			*pnext = pb;
			return (0);
/*
 * [: :] character class
 *   move class name into NUL terminated buffer
 *   error if too short or too long
 *   determine class handle, error in undefined
 *   set bitmap bit of all characters with this class characteristic
 *   if ignore case, also set bits of opposite case characters
 */
		case '[':
			if ((c = *pb++) == ':') {
				pclass = class;
				for (;;) {
					if (*pb == '\0') {
						*pnext = pb - 1;
						return (REG_EBRACK);
					}
					if (*pb == ':' && pb[1] == ']')
						break;
					if (pclass >= &class[CLASS_SIZE-1]) {
						*pnext = pb;
						return (REG_ECTYPE);
					}
					*pclass++ = *pb++;
				}
				if (pclass == class) {
					*pnext = pb;
					return (REG_ECTYPE);
				}
				*pclass = '\0';
				if ((wh = WCTYPE_NATIVE(class)) == 0) {
					*pnext = pb;
					return (REG_ECTYPE);
				}
				pb += 2;
				for (i = 1; i <= 255; i++) {
					if (ISWCTYPE_NATIVE(i, wh) != 0) {
						SETBITC(pp, i);
						if (icase != 0) {
							if ((c2 = toupper(i))
							    == i)
								c2 = tolower(i);
							SETBITC(pp, c2);
						}
					}
				}
				c = 0;
				break;
/*
 * [= =] equivalence class or [. .]collating element
 *   error if not a single character followed by terminating character pair
 *   set bitmap bit of character
 *   if ignore case, also set bit of opposite case character
 * set bit
 */
			} else if (c == '=' || c == '.') {
				if (*pb == '\0' || pb[1] != c || pb[2] != ']') {
					int prech = 0;
					while (*pb != '\0') {
						if (*pb == ']' && prech == c) {
							*pnext = pb;
							return (REG_ECOLLATE);
						}
						prech = *pb++;
					}
					*pnext = pb;
					return (REG_EBRACK);
				}
				c = *pb;
				pb += 3;
				SETBITC(pp, c);
				if (icase != 0) {
					if ((c2 = toupper(c)) == c)
						c2 = tolower(c);
					SETBITC(pp, c2);
				}
				break;
			} else {
				pb--;
				c = '[';
				goto one_char;
			}
/*
 * <hyphen> deliniates a range expression unless it is an end point
 */
		case '-':
			if (dash == 0) {
				dash = c;
				pend = pb;
				continue;
			} else
				goto one_char;
		} /* end of switch */
/*
 * Process range expression
 *   prev is file code of previous character (start point)
 *   c is file code of character following <hyphen> (end point)
 *   error if start point is greater than end point
 *   set all bits between prev and c
 *   if ignore case, also set bits of opposite case characters
 */
		if (dash != 0) {
			dash = 0;
			if (prev > c || prev == 0) {
				*pnext = pend;
				return (REG_ERANGE);
			}
			for (i = prev + 1; i < c; i++) {
				SETBITC(pp, i);
				if (icase != 0) {
					if ((c2 = toupper(i)) == i)
						c2 = tolower(i);
					SETBITC(pp, c2);
				}
			}
			prev = 0;
		} else
			prev = c;
	} /* end of while */
/*
 * fatal error if <right-bracket> not found
 */
	*pnext = pb - 1;
	return (REG_EBRACK);
}

/*
 *	enlarge	-	enlarge compiled pattern buffer
 *
 *		- x		# of new bytes needed in pattern buf
 *		- pp_start	ptr to starting address of pattern buf
 *		- pe		ptr to ending address of pattern buf
 *		- plastce	ptr to last compiled pattern code
 */
void
enlarge(size_t x, uchar_t **pp_start, uchar_t **pe, uchar_t **pp,
	uchar_t **plastce)
{
	size_t  old_len;	/* previous length (bytes) */
	size_t  new_len;	/* new length (bytes) */
	uchar_t *old_start;	/* previous pp_start */
	uchar_t *new_start;	/* new pp_start */

	old_start = *pp_start;
	old_len = *pe - old_start + 1;
	new_len = old_len + PATTERN_EXP;
	while (new_len < old_len + x)
		new_len += PATTERN_EXP;
	new_start = malloc(new_len);
	*pp_start = new_start;
	if (new_start != NULL) {
		(void) memcpy(new_start, old_start, old_len);
		*pe = new_start + new_len - 1;
		*pp = (*pp - old_start) + new_start;
		if (*plastce != NULL)
			*plastce = (*plastce - old_start) + new_start;
		free(old_start);
	}
}

/*
 * repeat_check -  remove interval, uncouple last STRING char
 *		- plastce	ptr to last compiled pattern code
 *		- pp		ptr to compiled RE
 */
void
repeat_check(uchar_t **plastce, uchar_t **pp)
{
	if (((**plastce & CR_MASK) == CR_INTERVAL) ||
	    ((**plastce & CR_MASK) == CR_INTERVAL_ALL)) {
		/* BEGIN CSTYLED */
		/*
		 * CPC MIN MAX EXP0 EXP1 EXP2 ... EXPN XXX
		 *  ^                                   ^
		 *  |                                   |
		 * *plastce                            *pp
		 *
		 * Remove MIN and MAX.
		 * So, Moving EXP0..EXPN to *plastce + 1.
		 */
		/* END CSTYLED */
		size_t	mlen = *pp - (*plastce + 3);
		(void) memmove(*plastce + 1, *plastce + 3, mlen);
		*pp -= 2;
	} else if (**plastce == CC_STRING) {
		/* BEGIN CSTYLED */
		/*
		 * CC_STR idx   S0 S1 ... SN-1 SN     XXX
		 *   ^                                 ^
		 *   |                                 |
		 *  *plastce                          *pp
		 *
		 * converting the above to:
		 *
		 * CC_STR idx-1 S0 S1 ... SN-1 CC_CHR SN  XXX
		 *                              ^          ^
		 *                              |          |
		 *                             *plastce   *pp
		 */
		/* END CSTYLED */
		uchar_t	*tp = *pp - 1;	/* points to STRN */

		(*(*plastce + 1))--;	/* decrement idx by 1 */
		**pp = *tp;	/* copy STRN to *pp */
		*tp = CC_CHAR;	/* place CC_CHAR */
		*plastce = tp;	/* move *plastce to CC_CHR */
		(*pp)++;	/* move *pp to the next to STRN */
	} else if (**plastce == CC_I_STRING) {
		/* BEGIN CSTYLED */
		/*
		 * CC_I_STR idx   S0u S0l ... SN-1u SN-1l SNu       SNl XXX
		 *   ^                                                   ^
		 *   |                                                   |
		 * *plastce                                             *pp
		 *
		 * converting the above to:
		 *
		 * CC_I_STR idx-1 S0u S0l ... SN-1u SN-1l CC_I_CHAR SNu SNl XXX
		 *                                           ^               ^
		 *                                           |               |
		 *                                        *plastce         *pp
		 */
		/* END CSTYLED */
		uchar_t	*tp1 = *pp - 1;
		uchar_t	*tp2 = *pp - 2;

		(*(*plastce + 1))--;	/* decrement idx by 1 */

		**pp = *tp1;		/* copy SNl to *pp */
		*tp1 = *tp2;		/* copy SNu to *pp - 1 */
		*tp2 = CC_I_CHAR;	/* place CC_I_CHAR to *pp - 2 */
		*plastce = tp2;		/* move *plastce to CC_I_CHAR */
		(*pp)++;		/* move *pp to the next to SNl */
	}
}

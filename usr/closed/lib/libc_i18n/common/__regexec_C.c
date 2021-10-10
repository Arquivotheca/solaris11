/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * COMPONENT_NAME: (LIBCPAT) Standard C Library Pattern Functions
 *
 * FUNCTIONS: __regexec_C
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
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "reglocal.h"
#include <regex.h>
#include "patlocal.h"
#include <alloca.h>
#ifdef	_EXEC_DEBUG
#include <assert.h>
#endif
#include "libc.h"

/*
 *	__reg_bits[] is also defined in __regcomp_C.c, __regcomp_std.c,
 *	and __regexec_std.c.  When updating the following definition,
 *	make sure to update others
 */

static const int	__reg_bits[] = { /* bitmask for [] bitmap */
	0x00000001, 0x00000002, 0x00000004, 0x00000008,
	0x00000010, 0x00000020, 0x00000040, 0x00000080};

/*
 * Internal function prototypes
 */
int	_regexec_strincmp(uchar_t *, uchar_t *, size_t);

/* match entire pattern against string */
static int	match_re_C(uchar_t *, uchar_t *, regex_t *,
    EXEC1 *, EXEC2 *);

/*
 * __regexec_C() - Determine if RE pattern matches string
 *		   - valid for only C locale
 *
 *		   - hdl	ptr to __lc_collate table
 *		   - preg	ptr to structure with compiled pattern
 *		   - string	string to be matched
 *		   - nmatch	# of matches to report in pmatch
 *		   - pmatch	reported subexpression offsets
 *		   - eflags	regexec() flags
 */
int
__regexec_C(_LC_collate_t *hdl, const regex_t *preg, const char *string,
    size_t nmatch, regmatch_t pmatch[], int eflags)
{
	EXEC1	e1;	/* initial data passing structure1 */
	EXEC2	e2;	/* initial data passing structure2 */
	int	i;	/* loop index */
	uchar_t	*pmap;	/* ptr to character map table */
	uchar_t	*pstr;	/* ptr to next string byte */
	int	stat;	/* match_re() return status */
	size_t	nsub;

/*
 * Return error if RE pattern is undefined
 */
	if (preg->re_comp == NULL)
		return (REG_BADPAT);

/*
 * optimisation:
 *	if the pattern doesn't start with "^",
 *	trim off prefix of pstr that has no hope of matching.  If we
 *	exhaust pstr via this method, we may already be a wiener!
 */
	pmap = (uchar_t *)preg->re_sc->re_map;
	pstr = (uchar_t *)string;

	if (*(uchar_t *)preg->re_comp != CC_BOL) {
		while (*pstr && pmap[*pstr] == 0)
			++pstr;

		if (!*pstr && !pmap[0])
			return (REG_NOMATCH);
	}

/*
 * Initialize data recursion buffer
 */
	e1.flags = preg->re_cflags | eflags;
	e1.wander = 1;
	e1.nest = 0;
	e1.string = (uchar_t *)string;
	e1.pcoltbl = hdl;

	e2.start_match = pstr;
	e2.pend_string = NULL;

	nsub = preg->re_sc->re_ac_nsub;
	if (nsub != 0) {
		if (nsub <= _SMALL_NSUB) {
			/* use alloca */
			e2.tmatch = alloca(sizeof (ea_t) * (nsub + 1));
			e2.heap = 0;
		} else {
			e2.tmatch = malloc(sizeof (ea_t) * (nsub + 1));
			if (e2.tmatch == NULL) {
				return (REG_ESPACE);
			}
			e2.heap = 1;
		}
		(void) memset(e2.tmatch, 0, sizeof (ea_t) * (nsub + 1));
	} else {
		e2.heap = 0;
	}
/*
 * Attempt to match entire compiled RE pattern starting at current
 *     position in string
 */
	stat = match_re_C((uchar_t *)preg->re_comp, pstr, (regex_t *)preg,
	    &e1, &e2);
#ifdef	_EXEC_DEBUG
	assert(e1.nest == 0);
#endif

/*
 * Return offsets of entire pattern match
 * Return subexpression offsets, zero-length changed to -1
 */
	if (stat == 0) {
		pstr = e2.start_match;
		if (nmatch != 0 && (preg->re_cflags & REG_NOSUB) == 0) {
			/*
			 * rm_sp and rm_sp are non-standard interface
			 * (came from MSK regex implementation)
			 */
			pmatch[0].rm_sp = (char *)pstr;
			pmatch[0].rm_ep = (char *)e2.pend_string;
			pmatch[0].rm_so = pstr - (uchar_t *)string;
			pmatch[0].rm_eo = e2.pend_string - (uchar_t *)string;
			for (i = 1; i < nmatch && i <= preg->re_nsub; i++) {
				if (e2.tmatch[i].sa != NULL) {
					pmatch[i].rm_sp = (char *)
					    e2.tmatch[i].sa;
					pmatch[i].rm_ep = (char *)
					    e2.tmatch[i].ea;
					pmatch[i].rm_so = e2.tmatch[i].sa -
					    (uchar_t *)string;
					pmatch[i].rm_eo = e2.tmatch[i].ea -
					    (uchar_t *)string;
				} else {
					pmatch[i].rm_sp = NULL;
					pmatch[i].rm_ep = NULL;
					pmatch[i].rm_so = (regoff_t)-1;
					pmatch[i].rm_eo = (regoff_t)-1;
				}
			}
			for (; i < nmatch; i++) {
				/* for the case nmatch > nsub */
				pmatch[i].rm_sp = NULL;
				pmatch[i].rm_ep = NULL;
				pmatch[i].rm_so = (regoff_t)-1;
				pmatch[i].rm_eo = (regoff_t)-1;
			}
		}
	} else if (stat == REG_EBOL) {
		EXECFREE(e2);
		return (REG_NOMATCH);
	}
	EXECFREE(e2);
	return (stat);
}


/*
 * match_re()	- Match entire RE pattern to string
 *
 *		- ppat		ptr to pattern
 *		- pstr		ptr to string
 *		- preg		ptr to caller's regex_t structure
 *		- pe1		ptr to recursion data structure1
 *		- pe2		ptr to recursion data structure2
 */
static int
match_re_C(uchar_t *ppat, uchar_t *pstr, regex_t *preg, EXEC1 *pe1, EXEC2 *pe2)
{
	uchar_t	*best_alt;	/* best alternative pend_string */
	size_t	count;		/* # bytes to backtrack each time */
	int	cp;		/* pattern character */
	int	cp2;		/* opposite case pattern character */
	int	cs;		/* string character */
	int	idx;		/* subexpression index */
	int	max;		/* maximum repetition count - min */
	int	min;		/* minimum repetition count */
	uchar_t	*pback;		/* ptr to subexpression backreference */
	uchar_t	*pea;		/* ptr to subexpression end address */
	uchar_t	*psa;		/* ptr to subexpression start address */
	uchar_t	*pstop;		/* ptr to backtracking string point */
	uchar_t	*sav_pat;	/* saved pattern */
	uchar_t	*sav_str;	/* saved string */
	uchar_t	*pmap;		/* ptr to character map table */
	int	stat;		/* match_re() recursive status */
	int	wander;		/* copy of EXEC.wander */
	EXEC2	r2;		/* another copy of *pe2 for recursion */
	EXEC2	rbest2 = {NULL, NULL, NULL, 0};
	size_t	nsub;

	wander = pe1->wander;
	pmap = preg->re_sc->re_map;
	sav_pat = ppat;
	sav_str = pstr;
	pe1->wander = 0;
	nsub = preg->re_sc->re_ac_nsub;

	ALLOC_MEM;

	/* LINTED */
	if (0) {
no_match:
		/*
		 * NOTE: the only way to come here is via a goto.
		 */
		if (wander) {
			/*
			 * we come here if we fail to match, and ok to wander
			 * down the string looking for a match.
			 *	- restore the pattern to the start
			 *	- restore string to one past where we left off
			 *	  and trim unmatchables
			 */
			if (*sav_str == '\0') {
				EXECALLFREE;
				return (REG_NOMATCH);
			}

			ppat = sav_pat;		/* restore pattern */
			pstr = sav_str + 1;

			while (*pstr && pmap[*pstr] == 0)
				++pstr;

		/*
		 * If at end of string, and it isn't possible for '$'
		 * to start an expression, then no match.  It is possible
		 * for '$' to start an expression as in "x*$", since
		 * "x*$" is equivalent to "$" when 0 x's precede the end
		 * of line, so we have to check one more time to see if
		 * the pattern matches the empty string (i.e. empty string
		 * is equivalent to end of line).  Note that this way,
		 * "yx*$" won't match "", but "x*$" will.
		 */
			if (*pstr == 0 && !pmap[0]) {
				EXECALLFREE;
				return (REG_NOMATCH);
			}

			pe2->start_match = sav_str = pstr;
		} else {
			EXECALLFREE;
			return (REG_NOMATCH);
		}
	}

/*
 * Perform each compiled RE pattern code until end-of-pattern or non-match
 * Break to bottom of loop to match remaining pattern/string when extra
 *   expressions have been matched
 */
	for (;;) {
		count = 1;
		switch (*ppat++) {
/*
 * a single character, no repetition
 *   continue if pattern character matches next string character
 *   otherwise return no match
 */

		case CC_CHAR:
			if (*ppat != *pstr)
				goto no_match;
			ppat++;
			pstr++;
			continue;
/*
 * any single character, no repetition
 *   continue if next string character is anything but <nul>
 *   otherwise return no match
 */

		case CC_DOT:
			if (*pstr++ != '\0')
				continue;
			EXECALLFREE;
			return (REG_NOMATCH);
/*
 * end-of-pattern
 *   update forward progress of matched location in string
 *   return success
 */

		case CC_EOP:
			pe2->pend_string = pstr;
			EXECALLFREE;
			return (0);
/*
 * bracket expression, no repetition
 *   continue if next string character has bit set in bitmap
 *   otherwise return no match
 */

		case CC_BITMAP:
			cs = *pstr++;
			if ((*(ppat + (cs >> 3)) & __reg_bits[cs & 7]) != 0) {
				ppat += BITMAP_LEN;
				continue;
			}
			goto no_match;
/*
 * character string, no repetition
 * single multibyte character, no repetition
 *   continue if next n pattern characters matches next n string characters
 *   otherwise return no match
 */

		case CC_STRING:
			count = *ppat++;
			do {
				if (*ppat++ != *pstr++)
					goto no_match;
			} while (--count > 0);
			continue;
/*
 * end subexpression, no repetition
 *   save subexpression ending address
 *   continue in all cases
 */

		case CC_SUBEXP_E:
			idx = (*ppat << 8) + *(ppat + 1);
			ppat += 2;
			pe2->tmatch[idx].ea = pstr;
			continue;
/*
 * subexpression backreference, no repetition
 *   continue if next n string characters matches what was previously
 *     matched by the referenced subexpression
 *   otherwise return no match
 */
		case CC_BACKREF:
			idx = *ppat++;
			pback = pe2->tmatch[idx].sa;
			pea = pe2->tmatch[idx].ea;
			while (pback < pea) {
				if (*pback++ != *pstr++)
					goto no_match;
			}
			continue;
/*
 * begin subexpression
 *   generate new copy of recursion data
 *   preserve subexpression starting address
 *   match remaining pattern against remaining string
 *   if remaining pattern match succeeds, update recursion data with
 *   new copy and return success
 *   if remaining pattern match fails and zero length subexpression is ok,
 *   continue with pattern immediately following CC_SUBEXP_E
 *   otherwise return fatal error
 */

		case CC_SUBEXP:
			/* index is 2bytes value */
			idx = (*ppat << 8) + *(ppat + 1);
			ppat += 2;
			ECOPY2(&r2, pe2, idx);
			r2.tmatch[idx].sa = pstr;
			pe1->nest++;
			stat = match_re_C(ppat, pstr, preg, pe1, &r2);
			pe1->nest--;
			if (stat == 0) {
				ECOPY(pe2, &r2);
				EXECALLFREE;
				return (0);
			}
			if (((cp2 = (*(uchar_t *)preg->re_sc->re_esub[idx] &
			    CR_MASK)) == CR_QUESTION || cp2 == CR_STAR) ||
			    ((cp2 == CR_INTERVAL || cp2 == CR_INTERVAL_ALL) &&
			    *(((uchar_t *)preg->re_sc->re_esub[idx])+1) == 0)) {
				ppat = preg->re_sc->re_esub[idx];
				if ((*ppat != (CC_SUBEXP_E | CR_INTERVAL)) &&
				    (*ppat != (CC_SUBEXP_E | CR_INTERVAL_ALL)))
					ppat += _SUBEXP_E_LEN;
				else
					ppat += _SUBEXP_E_INT_LEN;
				continue;
			}
			goto no_match;
/*
 * beginning-of-line anchor
 *   REG_NEWLINE allows ^ to match null string following a newline
 *   REG_NOTBOL means first character is not beginning of line
 *
 *   REG_NOTBOL   REG_NEWLINE   at BOL   ACTION
 *   ----------   -----------   ------   -------------------------
 *        N            N           Y     continue
 *        N            N           N     return REG_EBOL
 *        N            Y           Y     continue
 *        N            Y           N     continue if \n, else return REG_NOMATCH
 *        Y            N           Y     return REG_EBOL
 *        Y            N           N     return REG_EBOL
 *        Y            Y           Y     continue if \n, else return REG_NOMATCH
 *        Y            Y           N     continue if \n, else return REG_NOMATCH
 */

		case CC_BOL:
			if ((pe1->flags & REG_NOTBOL) == 0) {
				if (pstr == pe1->string) {
					continue;
				} else if ((pe1->flags & REG_NEWLINE) == 0) {
					EXECALLFREE;
					return (REG_EBOL);
				}
			} else if ((pe1->flags & REG_NEWLINE) == 0) {
				EXECALLFREE;
				return (REG_EBOL);
			}
			if (pstr > pe1->string && *(pstr-1) == '\n')
				continue;
			goto no_match;
/*
 * end-of-line anchor
 *   REG_NEWLINE allows $ to match null string preceding a newline
 *   REG_NOTEOL means last character is not end of line
 *
 *   REG_NOTEOL   REG_NEWLINE   at EOL   ACTION
 *   ----------   -----------   ------   --------------------------
 *        N            N           Y     continue
 *        N            N           N     return REG_NOMATCH
 *        N            Y           Y     continue
 *        N            Y           N     continue if \n, else return REG_NOMATCH
 *        Y            N           Y     return REG_NOMATCH
 *        Y            N           N     return REG_NOMATCH
 *        Y            Y           Y     continue if \n, else return REG_NOMATCH
 *        Y            Y           N     continue if \n, else return REG_NOMATCH
 */

		case CC_EOL:
			if ((pe1->flags & REG_NOTEOL) == 0) {
				if (*pstr == '\0')
					continue;
				else if ((pe1->flags & REG_NEWLINE) == 0)
					goto no_match;
			} else if ((pe1->flags & REG_NEWLINE) == 0) {
				goto no_match;
			}
			if (*pstr == '\n')
				continue;
			goto no_match;

/*
 * CC_WORDB has been added to support word boundaries (ie, \< and \>
 * for ex). *ppat has '<' or '>' to distinguish beginning or end of a word.
 */
		case CC_WORDB:
		{
			uchar_t c;

			if (*ppat++ == (uchar_t)'<') {
				if (pstr == pe1->string) {
					if ((pe1->flags & REG_NOTBOL) == 0)
						continue;
					else
						goto no_match;
				}
				c = *(pstr - 1);
				if (!IS_WORDCHAR(c) && IS_WORDCHAR(*pstr))
					continue;
			} else {
				if (pstr == pe1->string)
					goto no_match;
				c = *(pstr - 1);
				if (!IS_WORDCHAR(*pstr) && IS_WORDCHAR(c))
					continue;
			}
		}
		goto no_match;
/*
 * start alternative
 *   try each alternate
 *   select best alternative or the one which gets to EOP first
 */
		case CC_ALTERNATE:
			for (best_alt = NULL; ; ) {
				/* altlen is 4bytes value */
				size_t	altlen;
				altlen = (*ppat << 24) + (*(ppat+1) << 16) +
				    (*(ppat+2) << 8) + *(ppat+3);
				ppat += 4;
				ECOPY(&r2, pe2);
				pe1->nest++;
				stat = match_re_C(ppat, pstr, preg, pe1, &r2);
				pe1->nest--;
				if (stat == 0 && best_alt < r2.pend_string) {
					if (*r2.pend_string == '\0') {
						ECOPY(pe2, &r2);
						EXECALLFREE;
						return (0);
					}
					if (altlen == 0) {
						/*
						 * This is the last
						 * alternate pattern.
						 */
						ECOPY(pe2, &r2);
						EXECALLFREE;
						return (0);
					}
					/*
					 * Will try other alternate patterns.
					 * Save the current info.
					 */
					if (nsub != 0) {
	if (rbest2.tmatch == NULL) {
		/*
		 * rbest2 not allocated yet.
		 */
		if (r2.heap == 0) {
			/*
			 * pe1->nest should never be larger than
			 * the threshold here.
			 */
			rbest2.heap = 0;
			rbest2.tmatch = alloca(sizeof (ea_t) * (nsub + 1));
		} else {
			rbest2.heap = 1;
			rbest2.tmatch = malloc(sizeof (ea_t) * (nsub + 1));
			if (rbest2.tmatch == NULL) {
				EXECFREE(r2);
				return (REG_ESPACE);
			}
		}
	} else {
		rbest2.heap = 0;
	}
					}
					best_alt = r2.pend_string;
					ECOPY(&rbest2, &r2);
				}
				if (altlen == 0) {
					/*
					 * This is the last alternate pattern.
					 */
					if (best_alt != NULL) {
						/*
						 * Matched pattern has been
						 * found.
						 */
						ECOPY(pe2, &rbest2);
						EXECALLFREE;
						return (0);
					}
					goto no_match;
				}
				/* BEGIN CSTYLED */
				/*
				 * Since altlen is not 0, the following pattern
				 * should also be an alternate pattern.
				 * Then, skipping the next CC_ALTERNATE and
				 * jumping to the 1st byte of the offset field.
				 *
				 * As follows, the current ppat points to
				 * CRE0.  altlen is the number of bytes
				 * from CRE0 to CREn.  So, the new ppat
				 * will be the current ppat + altlen +
				 * 3 (_ALT_E_LEN) for CC_ALT_E, idx0, idx1 +
				 * 1 for CC_ALT.
				 *
				 * current ppat
				 *  |
				 *  V
				 * CRE0, CRE1, ... CREn, CC_ALT_E, idx0, idx1,
				 * CC_ALT, off0, off1, off2, off3, CREx
				 *          ^
				 *          |
				 *          new ppat
				 *
				 */
				/* END CSTYLED */
				ppat += altlen + _ALT_E_LEN + 1;
			}
			/* NOTREACHED */
/*
 * any single character except <newline>, no repetition
 *   continue if next string character is anything but <nul>
 *     or <newline> and REG_NEWLINE is set
 *   otherwise return no match
 */

		case CC_DOTREG:
			if (*pstr == '\0' || (*pstr++ == '\n' &&
			    (pe1->flags & REG_NEWLINE) != 0))
				goto no_match;
			continue;
/*
 * end alternative
 *   skip over any other alternative patterns and continue matching
 *     pattern to string
 */

		case CC_ALTERNATE_E:
			idx = (*ppat << 8) + *(ppat + 1);
			ppat = preg->re_sc->re_esub[idx];
			continue;
/*
 * invalid compiled RE code
 *   return fatal error
 */

		default:
			EXECALLFREE;
			return (REG_BADPAT);
/*
 * ignore case single character, no repetition
 *   continue if next string character matches pattern character or
 *     opposite case of pattern character
 *   otherwise return no match
 */

		case CC_I_CHAR:
			if (*ppat++ == *pstr) {
				ppat++;
				pstr++;
				continue;
			}
			if (*ppat++ == *pstr++)
				continue;
			goto no_match;
/*
 * ignore case character string, no repetition
 * ignore case single multibyte character, no repetition
 *   continue if next n string characters match next n pattern characters or
 *     opposite case of next n pattern characters
 *   otherwise return no match
 */

		case CC_I_STRING:
			count = *ppat++;
			do {
				if (*ppat++ == *pstr) {
					ppat++;
					pstr++;
				} else if (*ppat++ != *pstr++) {
					goto no_match;
				}
			} while (--count > 0);
			continue;
/*
 * ignore case subexpression backreference, no repetition
 *   continue if next n string characters matches what was previously
 *     matched by the referenced subexpression
 *   otherwise return no match
 */

		case CC_I_BACKREF:
			idx = *ppat++;
			pback = pe2->tmatch[idx].sa;
			pea = pe2->tmatch[idx].ea;
			count = pea - pback;
			if (_regexec_strincmp(pback, pstr, count) != 0)
				goto no_match;
			pstr += count;
			continue;
/*
 * ignore case subexpression backreference, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_I_BACKREF processing
 */

		case CC_I_BACKREF | CR_INTERVAL:
			min = *ppat++;
			max = *ppat++;
			goto cc_ibackref;
/*
 * ignore case subexpression backreference, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_I_BACKREF processing
 */

		case CC_I_BACKREF | CR_INTERVAL_ALL:
			min = *ppat++;
			ppat++;
			max = INT_MAX - 1;
			goto cc_ibackref;
/*
 * ignore case subexpression backreference, zero or more occurances "*"
 *   define min/max and jump to common CC_I_BACKREF processing
 */

		case CC_I_BACKREF | CR_STAR:
			min = 0;
			max = INT_MAX - 1;
			goto cc_ibackref;
/*
 * ignore case subexpression backreference - variable number of matches
 *   continue if subexpression match was zero-length
 *   match minimum number of required times
 *     return no match if cannot meet minimum count
 *   save new string position for backtracking
 *   match maximum number of required times, where max is
 *     number of remaining matches
 *   break-out to match remaining pattern/string
 */

cc_ibackref:
			idx = *ppat++;
			psa = pe2->tmatch[idx].sa;
			pea = pe2->tmatch[idx].ea;
			count = pea - psa;
			if (count == 0)
				continue;
			while (min-- > 0) {
				if (_regexec_strincmp(psa, pstr, count) != 0)
					goto no_match;
				pstr += count;
			}
			pstop = pstr;
			while (max-- > 0) {
				if (_regexec_strincmp(psa, pstr, count) != 0)
					break;
				pstr += count;
			}
			break;
/*
 * ignore case single character, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_I_CHAR processing
 */

		case CC_I_CHAR | CR_INTERVAL:
			min = *ppat++;
			max = *ppat++;
			goto cc_ichar;
/*
 * ignore case single character, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_I_CHAR processing
 */

		case CC_I_CHAR | CR_INTERVAL_ALL:
			min = *ppat++;
			ppat++;
			max = INT_MAX - 1;
			goto cc_ichar;
/*
 * ignore case single character, one or more occurances "+"
 *   define min/max and jump to common CC_I_CHAR processing
 */

		case CC_I_CHAR | CR_PLUS:
			min = 1;
			max = INT_MAX - 1;
			goto cc_ichar;
/*
 * ignore case single character, zero or one occurances "?"
 *   define min/max and jump to common CC_I_CHAR processing
 */

		case CC_I_CHAR | CR_QUESTION:
			min = 0;
			max = 1;
			goto cc_ichar;
/*
 * ignore case single character, zero or more occurances "*"
 *   define min/max and jump to common CC_I_CHAR processing
 */

		case CC_I_CHAR | CR_STAR:
			min = 0;
			max = INT_MAX - 1;
			goto cc_ichar;
/*
 * ignore case single character - variable number of matches
 *   match minimum number of required times
 *     return no match if cannot meet minimum count
 *   save new string position for backtracking
 *   match maximum number of required times, where max is
 *     number of remaining matches
 *   break-out to match remaining pattern/string
 */

cc_ichar:
			cp = *ppat++;
			cp2 = *ppat++;
			while (min-- > 0) {
				if (cp != *pstr && cp2 != *pstr)
					goto no_match;
				pstr++;
			}
			pstop = pstr;
			while (max-- > 0 && (cp == *pstr || cp2 == *pstr)) {
				pstr++;
			}
			break;
/*
 * any single character except <newline> , min/max occurances "{m,n}"
 *   define min/max and jump to common CC_DOTREG processing
 */

		case CC_DOTREG | CR_INTERVAL:
			min = *ppat++;
			max = *ppat++;
			goto cc_dotreg;
/*
 * any single character except <newline> , min/max occurances "{m,n}"
 *   define min/max and jump to common CC_DOTREG processing
 */

		case CC_DOTREG | CR_INTERVAL_ALL:
			min = *ppat++;
			ppat++;
			max = INT_MAX - 1;
			goto cc_dotreg;
/*
 * any single character except <newline>, one or more occurances "+"
 *   define min/max and jump to common CC_DOTREG processing
 */

		case CC_DOTREG | CR_PLUS:
			min = 1;
			max = INT_MAX - 1;
			goto cc_dotreg;
/*
 * any single character except <newline>, zero or one occurances "?"
 *   define min/max and jump to common CC_DOTREG processing
 */

		case CC_DOTREG | CR_QUESTION:
			min = 0;
			max = 1;
			goto cc_dotreg;
/*
 * any single character except <newline>, zero or more occurances "*"
 *   define min/max and jump to common CC_DOTREG processing
 */

		case CC_DOTREG | CR_STAR:
			min = 0;
			max = INT_MAX - 1;
			goto cc_dotreg;
/*
 * any single character except <newline> - variable number of matches
 *   match minimum number of required times
 *     return no match if cannot meet minimum count
 *   save new string position for backtracking
 *   match maximum number of required times, where max is
 *     number of remaining matches
 *   break-out to match remaining pattern/string
 */

cc_dotreg:
			while (min-- > 0) {
				if (*pstr == '\0' || (*pstr++ == '\n' &&
				    (pe1->flags & REG_NEWLINE) != 0)) {
					EXECALLFREE;
					return (REG_NOMATCH);
				}
			}
			pstop = pstr;
			while (max-- > 0) {
				if (*pstr == '\0') {
					break;
				} else if (*pstr++ == '\n' &&
				    (pe1->flags & REG_NEWLINE) != 0) {
					pstr--;
					break;
				}
			}
			break;
/*
 * end subexpression, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_SUBEXP_E processing
 */

		case CC_SUBEXP_E | CR_INTERVAL:
			min = *ppat++;
			max = *ppat++;
			goto cc_subexpe;
/*
 * end subexpression, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_SUBEXP_E processing
 */

		case CC_SUBEXP_E | CR_INTERVAL_ALL:
			min = *ppat++;
			ppat++;
			max = INT_MAX - min - 1;
			goto cc_subexpe;
/*
 * end subexpression, one or more occurances "+"
 *   define min/max and jump to common CC_SUBEXP_E processing
 */

		case CC_SUBEXP_E | CR_PLUS:
			min = 1;
			max = INT_MAX - 1;
			goto cc_subexpe;
/*
 * end subexpression, zero or one occurances "?"
 *   define min/max and jump to common CC_SUBEXP_E processing
 */

		case CC_SUBEXP_E | CR_QUESTION:
			min = 0;
			max = 1;
			goto cc_subexpe;
/*
 * end subexpression, zero or more occurances "*"
 *   define min/max and jump to common CC_SUBEXP_E processing
 */

		case CC_SUBEXP_E | CR_STAR:
			min = 0;
			max = INT_MAX - 1;
			goto cc_subexpe;
/*
 * end subexpression - variable number of matches
 *   save subexpression ending address
 *   if zero-length match, continue with remaining pattern if
 *     at or below minimum # of required matches
 *     otherwise return an error so that the last previous string
 *     matching locations can be used
 *   increment # of subexpression matches
 *   if the maximum # of required matches have not been found,
 *     reexecute the subexpression
 *     if it succeeds or fails without reaching the minimum # of matches
 *       return with the appropriate status
 *   if maximum number of matches found or the last match_re() failed and
 *     the minimum # of matches have been found, continue matching the
 *     remaining pattern against the remaining string
 */

cc_subexpe:
			/* index is 2 bytes value */
			idx = (*ppat << 8) + *(ppat + 1);
			ppat += 2;
			pe2->tmatch[idx].ea = pstr;
			if (pe2->tmatch[idx].ea == pe2->tmatch[idx].sa) {
				if (pe2->tmatch[idx].submatch < min)
					continue;
				else
					goto no_match;
			}
			pe2->tmatch[idx].submatch++;
			if (pe2->tmatch[idx].submatch < min + max) {
				ECOPY(&r2, pe2);
				pe1->nest++;
				stat = match_re_C(
				    (uchar_t *)preg->re_sc->re_lsub[idx],
				    pstr, preg, pe1, &r2);
				pe1->nest--;
				if (stat != REG_NOMATCH ||
				    pe2->tmatch[idx].submatch < min) {
					if (stat == 0) {
						ECOPY(pe2, &r2);
					}
					EXECALLFREE;
					return (stat);
				}
			}
			continue;
/*
 * subexpression backreference, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_BACKREF processing
 */

		case CC_BACKREF | CR_INTERVAL:
			min = *ppat++;
			max = *ppat++;
			goto cc_backref;
/*
 * subexpression backreference, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_BACKREF processing
 */

		case CC_BACKREF | CR_INTERVAL_ALL:
			min = *ppat++;
			ppat++;
			max = INT_MAX -1;
			goto cc_backref;
/*
 * subexpression backreference, zero or more occurances "*"
 *   define min/max and jump to common CC_BACKREF processing
 */

		case CC_BACKREF | CR_STAR:
			min = 0;
			max = INT_MAX - 1;
			goto cc_backref;
/*
 * subexpression backreference - variable number of matches
 *   continue if subexpression match was zero-length
 *   match minimum number of required times
 *     return no match if cannot meet minimum count
 *   save new string position for backtracking
 *   match maximum number of required times, where max is
 *     number of remaining matches
 *   break-out to match remaining pattern/string
 */

cc_backref:
			idx = *ppat++;
			psa = pe2->tmatch[idx].sa;
			pea = pe2->tmatch[idx].ea;
			count = pea - psa;
			if (count == 0)
				continue;
			while (min-- > 0) {
				pback = psa;
				while (pback < pea) {
					if (*pback++ != *pstr++)
						goto no_match;
				}
			}
			pstop = pstr;
			while (max-- > 0) {
				if (strncmp((const char *)psa,
				    (const char *)pstr, count) != 0)
					break;
				pstr += count;
			}
			break;
/*
 * bracket expression, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_BITMAP processing
 */

		case CC_BITMAP | CR_INTERVAL:
			min = *ppat++;
			max = *ppat++;
			goto cc_bitmap;
/*
 * bracket expression, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_BITMAP processing
 */

		case CC_BITMAP | CR_INTERVAL_ALL:
			min = *ppat++;
			ppat++;
			max = INT_MAX - 1;
			goto cc_bitmap;
/*
 * bracket expression, one or more occurances "+"
 *   define min/max and jump to common CC_BITMAP processing
 */

		case CC_BITMAP | CR_PLUS:
			min = 1;
			max = INT_MAX - 1;
			goto cc_bitmap;
/*
 * bracket expression, zero or one occurances "?"
 *   define min/max and jump to common CC_BITMAP processing
 */

		case CC_BITMAP | CR_QUESTION:
			min = 0;
			max = 1;
			goto cc_bitmap;
/*
 * bracket expression, zero or more occurances "*"
 *   define min/max and jump to common CC_BITMAP processing
 */

		case CC_BITMAP | CR_STAR:
			min = 0;
			max = INT_MAX - 1;
			goto cc_bitmap;
/*
 * bracket expression - variable number of matches
 *   match minimum number of required times
 *     return no match if cannot meet minimum count
 *   save new string position for backtracking
 *   match maximum number of required times, where max is
 *     number of remaining matches
 *   break-out to match remaining pattern/string
 */

cc_bitmap:
			while (min-- > 0) {
				cs = *pstr++;
				if ((*(ppat + (cs >> 3)) &
				    __reg_bits[cs & 7]) == 0)
					goto no_match;
			}
			pstop = pstr;
			while (max-- > 0) {
				cs = *pstr;
				if ((*(ppat + (cs >> 3)) &
				    __reg_bits[cs & 7]) != 0)
					pstr++;
				else
					break;
			}
			ppat += BITMAP_LEN;
			break;
/*
 * any single character, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_DOT processing
 */

		case CC_DOT | CR_INTERVAL:
			min = *ppat++;
			max = *ppat++;
			goto cc_dot;
/*
 * any single character, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_DOT processing
 */

		case CC_DOT | CR_INTERVAL_ALL:
			min = *ppat++;
			ppat++;
			max = INT_MAX - 1;
			goto cc_dot;
/*
 * any single character, one or more occurances "+"
 *   define min/max and jump to common CC_DOT processing
 */

		case CC_DOT | CR_PLUS:
			min = 1;
			max = INT_MAX - 1;
			goto cc_dot;
/*
 * any single character, zero or one occurances "?"
 *   define min/max and jump to common CC_DOT processing
 */

		case CC_DOT | CR_QUESTION:
			min = 0;
			max = 1;
			goto cc_dot;
/*
 * any single character, zero or more occurances "*"
 *   define min/max and jump to common CC_DOT processing
 */

		case CC_DOT | CR_STAR:
			min = 0;
			max = INT_MAX - 1;
			goto cc_dot;
/*
 * any single character - variable number of matches
 *   match minimum number of required times
 *     return no match if cannot meet minimum count
 *   save new string position for backtracking
 *   match maximum number of required times, where max is
 *     number of remaining matches
 *   break-out to match remaining pattern/string
 */

cc_dot:
			while (min-- > 0) {
				if (*pstr++ == '\0') {
					EXECALLFREE;
					return (REG_NOMATCH);
				}
			}
			pstop = pstr;
			while (max-- > 0) {
				if (*pstr++ == '\0') {
					pstr--;
					break;
				}
			}
			break;
/*
 * single character, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_CHAR processing
 */

		case CC_CHAR | CR_INTERVAL:
			min = *ppat++;
			max = *ppat++;
			goto cc_char;
/*
 * single character, min/max occurances "{m,n}"
 *   define min/max and jump to common CC_CHAR processing
 */

		case CC_CHAR | CR_INTERVAL_ALL:
			min = *ppat++;
			ppat++;
			max = INT_MAX - 1;
			goto cc_char;
/*
 * single character, one or more occurances "+"
 *   define min/max and jump to common CC_CHAR processing
 */

		case CC_CHAR | CR_PLUS:
			min = 1;
			max = INT_MAX - 1;
			goto cc_char;
/*
 * single character, zero or one occurances "?"
 *   define min/max and jump to common CC_CHAR processing
 */

		case CC_CHAR | CR_QUESTION:
			min = 0;
			max = 1;
			goto cc_char;
/*
 * single character, zero or more occurances "*"
 *   define min/max and jump to common CC_CHAR processing
 */

		case CC_CHAR | CR_STAR:
			min = 0;
			max = INT_MAX - 1;
			goto cc_char;
/*
 * single character - variable number of matches
 *   match minimum number of required times
 *     return no match if cannot meet minimum count
 *   save new string position for backtracking
 *   match maximum number of required times, where max is
 *     number of remaining matches
 *   break-out to match remaining pattern/string
 */

cc_char:
			cp = *ppat++;
			while (min-- > 0) {
				if (cp != *pstr++)
					goto no_match;
			}
			pstop = pstr;
			while (max-- > 0 && cp == *pstr) {
				pstr++;
			}
			break;
		} /* switch */
		break;
	} /* for (;;) loop */
/*
 * surplus matched expressions end up here
 * generate new copy of recursion data
 * match remaining pattern against remaining string
 * if remaining pattern match fails, forfeit one extra matched
 *   character and try again until no spare matches are left
 * return success and new recursion data if entire remaining pattern matches
 * otherwise return no match
 */
	for (;;) {
		ECOPY(&r2, pe2);
		pe1->nest++;
		stat = match_re_C(ppat, pstr, preg, pe1, &r2);
		pe1->nest--;
		if (stat != REG_NOMATCH) {
			if (stat == 0) {
				ECOPY(pe2, &r2);
			}
			EXECALLFREE;
			return (stat);
		}
		if (pstr <= pstop)
			break;
		pstr -= count;
	}
	goto no_match;
}

int
_regexec_strincmp(uchar_t *us1, uchar_t *us2, size_t n)
{
	while (n != 0 && (*us1 == *us2 || tolower(*us1) == tolower(*us2))) {
		if (*us1 == '\0')
			return (0);
		n--;
		us1++;
		us2++;
	}
	return (n);
}

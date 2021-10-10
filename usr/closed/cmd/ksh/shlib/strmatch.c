
/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */


/*
 * D. G. Korn
 * G. S. Fowler
 * AT&T Bell Laboratories
 *
 * match shell file patterns -- derived from Bourne and Korn shell gmatch()
 *
 *	sh pattern	egrep RE	description
 *	----------	--------	-----------
 *	*		.*		0 or more chars
 *	?		.		any single char
 *	[.]		[.]		char class
 *	[!.]		[^.]		negated char class
 *	[[:.:]]		[[:.:]]		ctype class
 *	[[=.=]]		[[=.=]]		equivalence class
 *	*(.)		(.)*		0 or more of
 *	+(.)		(.)+		1 or more of
 *	?(.)		(.)?		0 or 1 of
 *	(.)		(.)		1 of
 *	@(.)		(.)		1 of
 *	a|b		a|b		a or b
 *	a&b				a and b
 *	!(.)				none of
 *
 * \ used to escape metacharacters
 *
 *	*, ?, (, |, &, ), [, \ must be \'d outside of [...]
 *	only ] must be \'d inside [...]
 *
 * BUG: unbalanced ) terminates top level pattern
 */

#include <ctype.h>
#include <fnmatch.h>
#include "csi.h"

#ifndef isequiv
#define	isequiv(a, s)	((a) == (s))
#endif

#ifndef isgraph
#define	isgraph(c)	isprint(c)
#endif
#ifndef isxdigit
#define	isxdigit(c)	 ((c) >= '0' && (c) <= '9' || (c) >= 'a' && \
	(c) <= 'f' || (c) >= 'A' && (c) <= 'F')
#endif

#ifdef	getchar
#undef	getchar
#endif	/* getchar */
#define	getchar(x)	(*x++)

#define	getsource(s, e)	(((s) >= (e)) ? 0 : getchar(s))


static	wchar_t	*endmatch;
static	int	minmatch;

static	int	grpmatch();
static	int	onematch();
static	wchar_t	*gobble();

static	wchar_t	*match_bracket(wchar_t *, wchar_t *);

/*
 * strmatch compares the string s with the shell pattern p
 * returns 1 for match 0 otherwise
 */

int
strmatch(s, p)
register char	*s;
char	*p;
{
	int	rv;
	wchar_t	*wcs;
	wchar_t	*wcp;

	if (!(wcs = mbstowcs_alloc(s))) {
		return (0);
	}
	if (!(wcp = mbstowcs_alloc(p))) {
		xfree((void *)wcs);
		return (0);
	}

	minmatch = 0;
	rv = grpmatch(wcs, wcp, wcs + wcslen(wcs), (wchar_t *)0);

	xfree((void *)wcs);
	xfree((void *)wcp);

	return (rv);
}

int
wstrmatch(s, p)
register wchar_t	*s;
wchar_t	*p;
{
	int	rv;
	minmatch = 0;
	rv = grpmatch(s, p, s + wcslen(s), (wchar_t *)0);

	return (rv);
}

/*
 * leading substring match
 * first char after end of substring returned
 * 0 returned if no match
 * m: (0-min, 1-max) match
 */

wchar_t *
submatch(s, p, m)
register wchar_t	*s;
wchar_t	*p;
int		m;
{
	endmatch = 0;
	minmatch = !m;
	(void) grpmatch(s, p, s + wcslen(s), (wchar_t *)0);
	return (endmatch);
}

/*
 * match any pattern in a group
 * | and & subgroups are parsed here
 */

static	int
grpmatch(s, p, e, g)
wchar_t	*s;
register wchar_t	*p;
wchar_t	*e;
wchar_t	*g;
{
	register wchar_t *a;

	do {
		a = p;
		do {
			if (!onematch(s, a, e, g))
				break;
		} while (a = gobble(a, '&'));
		if (!a)
			return (1);
	} while (p = gobble(p, '|'));
	return (0);
}

/*
 * match_bracket()
 *	given exchar string s which points character follows left-bracket
 *	returns pointer to character (may be null) follows closing bracket.
 *	never reads after s reaached e (end of string).
 *	if s reached e or got a null before closing bracket, returns NULL.
 */
static	wchar_t *
match_bracket(wchar_t *s, wchar_t *e)
{
	int	leftmost = 1;
	int	bang = 0;
	int	incolsym = 0;
	wchar_t	c;

	for (;;) {
		if (s >= e || (c = getchar(s)) == L'\0')
			return (NULL);

		if (!leftmost && !incolsym && c == ']')
			return (s);

		/*
		 * if ']' comes when leftmost is not zero,
		 * it must be handled as a literal character.
		 */
		if (leftmost) {
			if (!bang && c == '!') {
				bang = 1;
			} else {
				leftmost = 0;
			}
		}

		if (c == '\\')
			if (s >= e || (c = getchar(s)) == L'\0')
				return (NULL);
			else
				continue;

		if (!incolsym && c == '[') {
			if (s >= e || (c = getchar(s)) == L'\0')
				return (NULL);
			if (c == '.' || c == ':' || c == '=') {
				incolsym = (int)(c);
				continue;
				/* NOTREACHED */
			}
			/* '[' as a literal charcter */
			if (!leftmost && c == ']')
				return (s);
		}

		if (incolsym && iswascii(c) && (int)(c) == incolsym) {
			if (s >= e || (c = getchar(s)) == L'\0')
				return (NULL);
			if (c == ']')
				incolsym = 0;
			continue;
			/* NOTREACHED */
		}

	}
}

/*
 * match a single pattern
 * e is the end (0) of the substring in s
 * g marks the start of a repeated subgroup pattern
 */

static	int
onematch(s, p, e, g)
wchar_t	*s;
register wchar_t	*p;
wchar_t	*e;
wchar_t	*g;
{
	register wchar_t 	pc;
	register wchar_t 	sc;
	register int	n;
	wchar_t	*olds;
	wchar_t	*oldp;

	do {
		olds = s;
		sc = getsource(s, e);
		if (IsWInvalid(pc = getchar(p))) {
			if (pc != sc)
				return (0);
			else
				continue;
		}
		switch (pc) {
		case '(':
		case '*':
		case '?':
		case '+':
		case '@':
		case '!':
			if (pc == '(' || *p == '(') {
				wchar_t	*subp;
				int	r;

				s = olds;
				oldp = p - 1;
				subp = p + (pc != '(');
				if (!(p = gobble(subp, 0)))
					return (0);
				if (pc == '*' || pc == '?' ||
				    pc == '+' && oldp == g)
				{
					if (onematch(s, p, e, (wchar_t *)0))
						return (1);
					if (!sc || !getsource(s, e))
						return (0);
				}
				if (pc == '*' || pc == '+') p = oldp;
				r = (pc != '!');
				do
				{
					if (grpmatch(olds, subp, s,
						(wchar_t *)0) == r &&
						onematch(s, p, e, oldp))
							return (1);
				} while (s < e && getchar(s));
				return (0);
			} else if (pc == '*') {
				/*
				 * several stars are the same as one
				 */

				while (*p == '*')
					if (*(p + 1) == '(') break;
					else p++;
				oldp = p;
				switch (pc = getchar(p)) {
				case '@':
				case '!':
				case '+':
					n = (*p == '(');
					break;
				case '(':
				case '[':
				case '?':
				case '*':
					n = 1;
					break;
				case 0:
					endmatch = minmatch ? olds : e;
					/*FALLTHROUGH*/
				case '|':
				case '&':
				case ')':
					return (1);

				case '\\':
					if (!(pc = getchar(p)))
						return (0);
					/*FALLTHROUGH*/
				default:
					n = 0;
					break;
				}
				p = oldp;
				for (;;) {
					if ((n || pc == sc) &&
						onematch(olds, p, e,
						    (wchar_t *)0))
							return (1);
					if (!sc)
						return (0);
					olds = s;
					sc = getsource(s, e);
				}
			} else if (pc != '?' && pc != sc)
				return (0);
			break;
		case 0:
			endmatch = olds;
			if (minmatch)
				return (1);
			/*FALLTHROUGH*/
		case '|':
		case '&':
		case ')':
			return (!(sc));
		case '[': {
			wchar_t	*pat;

			if ((pat = match_bracket(p, p + wcslen(p))) == NULL) {
				/*
				 * not a bracket expression,
				 * then '[' is an ordinary character.
				 */
				goto ordinary;
			/* NOTREACHED */
			} else {
				char *mbp;
				char *mbs;
				wchar_t	save = *pat;
				wchar_t	wstr[2];
				int	nomatch;

				*pat = L'\0';
				if ((mbp = wcstombs_alloc(p - 1))
				    == NULL)
					return (0);
				*pat = save;
				wstr[0] = sc;
				wstr[1] = L'\0';
				if ((mbs = wcstombs_alloc(wstr))
				    == NULL) {
					xfree((void *)mbp);
					return (0);
				}

				/*
				 * Use fnmatch() only for resolving each RE
				 * bracket expression. The reason for not
				 * passing the whole pattern to fnmatch() is
				 * because ksh's pattern has some syntax not
				 * supported by fnmatch() (i.e. such syntax
				 * is not defined in XPG4 sh spec) such as
				 * "@(|)".
				 * If XPG4 sh and ksh are speparated by ifdef,
				 * XPG4 sh side will be much simpler.
				 */

				nomatch = fnmatch(mbp, mbs, FNM_NOESCAPE);

				xfree((void *)mbp);
				xfree((void *)mbs);
				if (nomatch)
					return (0);
				else {
					p = pat;
					break;
				}
			}
		}
		case '\\':
			if (!(pc = getchar(p)))
				return (0);
			/*FALLTHROUGH*/
		default:
ordinary:
			if (pc != sc)
				return (0);
			break;
		}
	} while (sc);
	return (0);
}

/*
 * gobble chars up to <sub> or ) keeping track of (...) and [...]
 * sub must be one of { '|', '&', 0 }
 * 0 returned if s runs out
 */

static	wchar_t *
gobble(s, sub)
register wchar_t	*s;
register int	sub;
{
	register int	p = 0;
	register wchar_t	*b = NULL;
	wchar_t	c = L'\0';

	for (;;) {
		switch (getchar(s)) {
		case '\\':
			if (getchar(s))
				break;
			/*FALLTHROUGH*/
		case 0:
			return (0);
		case '[':
			if (!b) b = s;
			break;
		case ']':
			if (b && b != (s - 1)) b = 0;
			break;
		case '(':
			if (!b) p++;
			break;
		case ')':
			if (!b && p-- <= 0)
				return (sub ? 0 : s);
			break;
		case '&':
			if (!b && !p && sub == '&')
				return (s);
			break;
		case '|':
			if (!b && !p) {
				if (sub == '|')
					return (s);
				else if (sub == '&')
					return (0);
			}
			break;
		}
	}
}

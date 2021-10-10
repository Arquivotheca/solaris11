/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *	Parts of this product may be derived from
 *	International Business Machines Corp. tr.c and
 *	Berkeley 4.3 BSD systems licensed from International
 *	Business Machines Corp. and the University of California.
 */

/*
 * (C) COPYRIGHT International Business Machines Corp. 1985, 1993
 * All Rights Reserved
 */

/*
 * (c) Copyright 1990, 1991, 1992 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 *
 * OSF/1 1.1
 */

#include <alloca.h>
#include <sys/param.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include <limits.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <libintl.h>
#include <sys/localedef.h>

#define	WCSXFRM_NATIVE(pws1, pws2, n)	\
	METHOD_NATIVE(__lc_collate, wcsxfrm)(__lc_collate, (pws1), (pws2), (n))
#define	ISWCTYPE_NATIVE(pwc, class)	\
	METHOD_NATIVE(__lc_ctype, iswctype)(__lc_ctype, (pwc), (class))
#define	TOWLOWER_NATIVE(pwc)	\
	METHOD_NATIVE(__lc_ctype, towlower)(__lc_ctype, (pwc))
#define	TOWUPPER_NATIVE(pwc)	\
	METHOD_NATIVE(__lc_ctype, towupper)(__lc_ctype, (pwc))
#define	WCSTOMBS_NATIVE(s, pwcs, n)	\
	METHOD_NATIVE(__lc_charmap, wcstombs)(__lc_charmap, (s), (pwcs), (n))
#define	MBTOWC_NATIVE(pwc, s, n)	\
	METHOD_NATIVE(__lc_charmap, mbtowc)(__lc_charmap, (pwc), (s), (n))
#define	WCTOMB_NATIVE(s, pwc)	\
	METHOD_NATIVE(__lc_charmap, wctomb)(__lc_charmap, (s), (pwc))
#define	MBSTOWCS_NATIVE(pwcs, s, n)	\
	METHOD_NATIVE(__lc_charmap, mbstowcs)(__lc_charmap, (pwcs), (s), (n))
#define	FGETWC_NATIVE(stream)	\
	METHOD_NATIVE(__lc_charmap, fgetwc)(__lc_charmap, (stream))

#define	OCT_LOW		0x01
#define	OCT_HIGH	0x02
#define	OCT_MASK	(OCT_LOW | OCT_HIGH)

#define	COLL_LENGTH	(COLL_WEIGHTS_MAX + 1)

#define	EOS		0xffffffff
#define	MAX_N		0xfffffff

/* Flag argument to next() */
#define	INSTRING1	1
#define	INSTRING2	2
#define	SIZEONLY	3
static	int	dflag = 0;
static	int	sflag = 0;
static	int	cflag = 0;
#ifdef XPG6
static	int	Cflag = 0;
#endif

/*
 * Basic type for holding wide character lists;
 */
typedef struct {
	wchar_t		wc[2];
	wchar_t		translation;
	unsigned char	squeeze : 1;
	unsigned char	delete : 1;
} char_info;

typedef struct {
	unsigned long	count;
	char_info	*list;
} charset;

/*
 * Contains the complete list of valid wide character codes in collated order.
 */
static charset	character_set = {0, NULL};

/*
 * Used to cache ranges, classes, and equivalence classes so we don't have
 * to recompute them each time we reference them.
 */
typedef struct {
	long	count;
	long	avail;
	long	offset;
	wchar_t	*chars;
} clist;

/*
 * struct entry and struct range are used for collation/ranges (ie, "a-z")
 */
struct entry {
	wchar_t	wc;
	wchar_t	ucoll;
};

typedef struct range {
	struct entry	*wd;
	size_t	alloc;
	size_t	used;
} range_t;

typedef struct {
	char	key[128];
	clist	clist;
} cache_node;

static struct cache {
	long		count;
	cache_node	*nodes;
} cache = { 0, NULL };

static	struct string {
	wchar_t	*p;	/* command line source String pointer.		*/
	char	*class;
	clist	*nextclass;
			/* nextclass = alternate source String pointer,	*/
			/* into string of members of a class.		*/
	int nchars;	/* Characters in string so far.			*/
	int totchars;	/* Characters in string.			*/
	int belowlower;	/* Characters below first [:lower:]		*/
	int belowupper;	/* Characters below first [:upper:]		*/
	int octal;
} string1, string2;

static clist	vector = { 0, 0, 0, NULL };
static clist	tvector = { 0, 0, 0, NULL };

static  char    *myName;

static unsigned long	arg_count;
static unsigned long	is_C_locale;
static unsigned long	is_C_collate;
static unsigned long	realloc_size;
static int		mbcurmax;

static wchar_t	wc_min;	/* Minimum wide character value in the LC_COLLATE */
static wchar_t	wc_max;	/* Maximum wide character value in the LC_COLLATE */

static int	rel_ord;
static const wchar_t	*rel_ord_tbl;

static wint_t
__fputwc_native(wint_t wc, FILE *iop)
{
	char	mbs[MB_LEN_MAX];
	unsigned char	*p;
	int	n;

	if (wc == WEOF)
		return (WEOF);
	n = WCTOMB_NATIVE(mbs, (wchar_t)wc);
	if (n <= 0)
		return (WEOF);
	p = (unsigned char *)mbs;
	while (n--) {
		if (putc(*p++, iop) == EOF) {
			return (WEOF);
		}
	}
	return (wc);
}

/*
 * NAME:    tr
 * FUNCTION:    copies standard input to standard output with substitution,
 *      deletion, or suppression of consecutive repetitions of
 *      selected characters.
 */

static void *
allocate(void *prev, size_t size)
{
	void	*ret;

	if ((ret = realloc(prev, size)) == NULL) {
		perror(gettext("tr: Memory reallocation failure"));
		exit(1);
	}
	return (ret);
}

/*
 * NAME:	Usage
 * FUNCTION:	Issue Usage message to standard error and immediately terminate
 *               the tr command with return value 1.
 * ENTRY:
 * EXIT:
 */
static void
Usage(void)
{
#ifdef XPG6
	(void) fprintf(stderr,
	    gettext(
	    "Usage: tr [-c | -C] [-s] string1 string2\n"
	    "       tr -s [-c | -C] string1\n"
	    "       tr -d [-c | -C] string1\n"
	    "       tr -ds [-c | -C] string1 string2\n"));
#else
#ifdef XPG4
	(void) fprintf(stderr,
	    gettext(
	    "Usage: tr [-c] [-s] string1 string2\n"
	    "       tr -s [-c] string1\n"
	    "       tr -d [-c] string1\n"
	    "       tr -ds [-c] string1 string2\n"));
#else
	(void) fprintf(stderr, gettext(
	    "Usage: tr [ -cds ] [ String1 [ String2 ] ]\n"));
#endif
#endif
	exit(1);
}

static int
sccompare(const void *a, const void *b)
{
	unsigned long	wa, wb;

	wa = *(unsigned long *)a;
	wb = *(unsigned long *)b;
	return ((int)(wa - wb));
}

#if defined(XPG4) || defined(XPG6)
static int
wccompare(const void *a, const void *b)
{
	unsigned long	wc[2];
	wchar_t	ws[2];
	wchar_t	wp[2][COLL_LENGTH];
	size_t	ret;
	int	i, result;

	wc[0] = *(unsigned long *)a;
	wc[1] = *(unsigned long *)b;
	if (wc[0] == wc[1])
		return (0);
	if (wc[0] == 0 || wc[1] == 0) {
		return ((int)(wc[0] - wc[1]));
	}
	ws[1] = L'\0';
	for (i = 0; i < 2; i++) {
		ws[0] = (wchar_t)wc[i];
		ret = WCSXFRM_NATIVE(wp[i], ws, COLL_LENGTH);
		if (ret == (size_t)-1) {
			return ((int)(wc[0] - wc[1]));
		}
	}

	result = wcscmp(wp[0], wp[1]);
	if (result == 0) {
		return ((int)(wc[0] - wc[1]));
	} else {
		return (result);
	}
}
#endif

#define	character_set_index(wch)	\
	(((wch) >= wc_min && (wch) <= wc_max) ?	\
	    &character_set.list[wch] : NULL)

static cache_node *
cache_lookup(char *key)
{
	if (cache.count == 0)
		return (NULL);

	return ((cache_node *)bsearch(key, cache.nodes, cache.count,
	    sizeof (cache_node), (int (*)(const void *, const void *))strcmp));
}

static cache_node *
new_cache_node(char *key)
{
	long		count = cache.count + 1;
	cache_node	*node;

	cache.nodes = allocate(cache.nodes, sizeof (cache_node) * count);
	node = &cache.nodes[cache.count];
	cache.count = count;

	(void) strcpy(node->key, key);
	node->clist.avail = 0;
	node->clist.count = 0;
	node->clist.chars = NULL;

	/* sort the cache for bsearch */
	qsort(cache.nodes, count, sizeof (cache_node),
	    (int (*)(const void *, const void *))strcmp);

	return (cache_lookup(key));
}

static void
make_range(range_t *rp, wchar_t wc, wchar_t ucoll)
{
#define	NEW_ALLOC	128
	if (rp->used == rp->alloc) {
		rp->wd = allocate(rp->wd,
		    sizeof (struct entry) * (rp->alloc + NEW_ALLOC));
		rp->alloc += NEW_ALLOC;
	}
	rp->wd[rp->used].wc = wc;
	rp->wd[rp->used].ucoll = ucoll;
	rp->used++;
}

static int
range_comp(const void *a, const void *b)
{
	struct entry	*pa = (struct entry *)a;
	struct entry	*pb = (struct entry *)b;

	return (pa->ucoll - pb->ucoll);
}

static void
sort_range(range_t *rp)
{
	struct entry	*p = rp->wd;

	qsort(p, rp->used, sizeof (struct entry), range_comp);
#ifdef DEBUG_RET
	{
		int	i;
		for (i = 0; i < rp->used; i++) {
			(void) printf("i: %d, wc: %08x, ucoll: %08x\n",
			    i, rp->wd[i].wc, rp->wd[i].ucoll);
		}
	}
#endif
}

static void
add_to_clist(clist *list, wchar_t wch)
{
	/* expand our list if we need more room */
	if (list->avail == 0) {
		list->chars = allocate(list->chars,
		    (list->count + realloc_size) * sizeof (wchar_t));
		list->avail = realloc_size;
	}
	list->chars[list->count++] = wch;
	list->avail--;
}

static void
load_character_set(void)
{
	char	*ctype;
	char	*coll;
	long	i;
	wchar_t	array_max;
	int	C_ctype, C_coll, is_C;

	ctype = setlocale(LC_CTYPE, NULL);
	coll = setlocale(LC_COLLATE, NULL);
	mbcurmax = MB_CUR_MAX;		/* bin/tr range order */

	if (strcmp(ctype, "C") == 0 || strcmp(ctype, "POSIX") == 0) {
		C_ctype = 1;
	} else {
		C_ctype = 0;
	}
	if (strcmp(coll, "C") == 0 || strcmp(coll, "POSIX") == 0) {
		C_coll = 1;
	} else {
		C_coll = 0;
	}

#ifdef	XPG6
	is_C = cflag || (C_ctype && C_coll);
#else
	is_C = C_ctype && C_coll;
#endif
	if (is_C) {
		is_C_locale = 1;
		is_C_collate = 1;
		wc_min = 0;
		wc_max = 255;
		array_max = 255;
	} else {
		is_C_locale = 0;
		wc_min = __lc_collate->co_wc_min;
		wc_max = __lc_collate->co_wc_max;

		if (mbcurmax == 1) {
			array_max = 255;
		} else {
			array_max = wc_max;
		}
		/* may be a mixed locale */
		if (C_coll) {
			/* no relative table exists */
			is_C_collate = 1;
		} else {
			is_C_collate = 0;
			rel_ord = __lc_collate->co_nord +
			    __lc_collate->co_r_order;
			rel_ord_tbl = __lc_collate->co_coltbl[rel_ord];
		}
	}
#ifdef	DEBUG_RET
	printf("wc_min: %d\n", __lc_collate->co_wc_min);
	printf("wc_max: %d\n", __lc_collate->co_wc_max);
	printf("co_nord: %d\n", __lc_collate->co_nord);
	printf("co_r_order: %d\n", __lc_collate->co_r_order);
#endif
	character_set.count = array_max - wc_min + 1;
	character_set.list = allocate(NULL,
	    sizeof (char_info) * character_set.count);
	for (i = (long)wc_min; i <= (long)array_max; i++) {
		character_set.list[i].translation =
		    character_set.list[i].wc[0] = (wchar_t)i;
		character_set.list[i].wc[1] = L'\0';
		character_set.list[i].squeeze =
		    character_set.list[i].delete = 0;
	}
}

static clist *
character_class(const char *class, int flag)
{
	wctype_t	type;
	cache_node	*node;
	char		*expression;

	/* get class type code */
	if ((type = wctype(class)) == 0)
		return (NULL);

	expression = alloca(strlen(class) + 6);
	/* LINTED: E_SEC_SPRINTF_UNBOUNDED_COPY */
	(void) sprintf(expression,
	    "[:%s:]%d", class, (flag == INSTRING2) ? 1 : 0);

	if ((node = cache_lookup(expression)) == NULL) {
		unsigned long	index;
		long		count = character_set.count;
		char_info	*nextchar = character_set.list;

		/* we have not encountered this list before ... */
		node = new_cache_node(expression);

		/*
		 * lookup all characters in class.
		 * We use this method rather than regcomp / regexec to
		 * improve performance.
		 */
		if (flag == INSTRING2) {
			/*
			 * upper / lower are only allowed in string 2
			 * for case conversion purposes.  The cases are
			 * found by scanning for the opposite case and
			 * converting because there can be an unequal number
			 * of upper and lower case characters in a character
			 * set.  This method equalizes the number, converting
			 * only the characters that have conversions defined.
			 */
			if (strcmp(class, "lower") == 0) {
				for (index = 0; index < count; index++) {
					if (ISWCTYPE_NATIVE(nextchar->wc[0],
					    _ISUPPER)) {
						add_to_clist(&(node->clist),
						    TOWLOWER_NATIVE(
						    nextchar->wc[0]));
					}
					nextchar++;
				}
			} else if (strcmp(class, "upper") == 0) {
				for (index = 0; index < count; index++) {
					if (ISWCTYPE_NATIVE(nextchar->wc[0],
					    _ISLOWER)) {
						add_to_clist(&(node->clist),
						    TOWUPPER_NATIVE(
						    nextchar->wc[0]));
					}
					nextchar++;
				}
			}
		} else {
			if (is_C_locale && strcmp(class, "blank") == 0) {
				/*
				 * C locale isblank is broken so
				 * we do it manually
				 */
				add_to_clist(&(node->clist), L' ');
				add_to_clist(&(node->clist), L'\t');
			} else {
				/*
				 * All other classes are handled here
				 */
				for (index = 0; index < count; index++) {
					if (ISWCTYPE_NATIVE(nextchar->wc[0],
					    type)) {
						add_to_clist(&(node->clist),
						    nextchar->wc[0]);
					}
					nextchar++;
				}
			}
		}
	}
	node->clist.offset = 0;
	return (&(node->clist));
}


static clist *
character_equiv_class(const wchar_t equiv)
{
	const char	*fmt = "[[=%C=]]";
	unsigned long	index;
	cache_node	*node;
	char	*expression;
	wchar_t	wcs[2];
	wchar_t	wcs_out[COLL_LENGTH];
	wchar_t	eqwc[COLL_LENGTH];
	size_t	ret;

	expression = alloca(mbcurmax + 7);
	(void) sprintf(expression, fmt, equiv);

	if ((node = cache_lookup(expression)) == NULL) {
		/* we have not encountered this list before ... */
		node = new_cache_node(expression);

		/* The first WC returned from wcsxfrm () is primary weight. */
		wcs[0] = equiv;
		wcs[1] = L'\0';
		if (WCSXFRM_NATIVE(eqwc, wcs, COLL_LENGTH) == (size_t)-1) {
			/* return empty node since errno should be EINVAL */
			(void) fprintf(stderr, gettext(
	"tr: Regular expression \"%s\" contains invalid collating element\n"),
			    expression);
			goto return_character_equiv_class;
		}

		for (index = 0; index < character_set.count; index++) {
			wcs[0] = character_set.list[index].wc[0];
			ret = WCSXFRM_NATIVE(wcs_out, wcs, COLL_LENGTH);
			if (ret != (size_t)-1 && wcscmp(wcs_out, eqwc) == 0) {
				add_to_clist(&(node->clist),
				    character_set.list[index].wc[0]);
			}
		}
	}

return_character_equiv_class:
	node->clist.offset = 0;
	return (&(node->clist));
}

static clist *
character_range_std(wchar_t low, wchar_t high)
{
	cache_node	*node;
	char	expression[18];
	wchar_t	min_coll, max_coll, ucoll;
	wchar_t	ci;
	range_t	range;
	int	i;

	(void) snprintf(expression, sizeof (expression), "%lx-%lx", low, high);

	if ((node = cache_lookup(expression)) == NULL) {
		/* we have not encountered this list before ... */
		node = new_cache_node(expression);

		/*
		 * lookup all characters in range.
		 */

		min_coll = rel_ord_tbl[low];
		max_coll = rel_ord_tbl[high];

		if (min_coll > max_coll) {
			(void) fprintf(stderr, gettext(
			    "tr: Range endpoints out of order.\n"));
			exit(1);
		}
		range.wd = NULL;
		range.used = 0;
		range.alloc = 0;
		for (ci = wc_min; ci <= wc_max; ci++) {
			ucoll = rel_ord_tbl[ci];
			if (ucoll >= min_coll && ucoll <= max_coll) {
				make_range(&range, ci, ucoll);
			}
		}

		sort_range(&range);

		for (i = 0; i < range.used; i++) {
			add_to_clist(&(node->clist), range.wd[i].wc);
		}
	}
	node->clist.offset = 0;
	return (&(node->clist));
}

static clist *
character_range_bin(wchar_t low, wchar_t high)
{
	cache_node	*node;
	char	expression[18];
	wchar_t	ci;

	/* Differentiate binary range from collation range */
	(void) snprintf(expression, sizeof (expression), "B%lx-%lx", low, high);

	if ((node = cache_lookup(expression)) == NULL) {
		/* we have not encountered this list before ... */
		node = new_cache_node(expression);

		/*
		 * lookup all characters in range.
		 */

		if (low > high) {
			(void) fprintf(stderr, gettext(
			    "tr: Range endpoints out of order.\n"));
			exit(1);
		}
		for (ci = low; ci <= high; ci++) {
			add_to_clist(&(node->clist), ci);
		}
	}
	node->clist.offset = 0;
	return (&(node->clist));
}

static clist *
character_range(wchar_t low, wchar_t high, int flag)
{
#if	defined(XPG4) || defined(XPG6)
	/*
	 * Range for xpg4/bin/tr and xpg6/bin/tr:
	 * If C collation use binary range.  If one or more endpoints are
	 * octal representation, use binary range.  Otherwise use
	 * relative table.
	 */
	if (is_C_collate || flag & OCT_MASK) {
		return (character_range_bin(low, high));
	} else {
		return (character_range_std(low, high));
	}

#else	/* !defined(XPG4) && !defined(XPG6) */
	/*
	 * Range for bin/tr:
	 * If C collation, or both endpoints are single byte, or if one or
	 * more endpoints are octal representation, use binary range.
	 * Otherwise, at least one endpoint is multibyte, so use relative
	 * table.
	 */
	if (is_C_collate || mbcurmax == 1 || flag & OCT_MASK) {
		return (character_range_bin(low, high));
	} else {
		char	dummy[MB_LEN_MAX * 2];
		if (WCTOMB_NATIVE(dummy, low) > 1 ||
		    WCTOMB_NATIVE(dummy, high) > 1) {
			/* mb low/high, use relative */
			return (character_range_std(low, high));
		} else {
			/* sb low and high, use binary */
			return (character_range_bin(low, high));
		}
	}
#endif	/* defined(XPG4) || defined(XPG6) */
}

static clist *
character_repeat(wchar_t wch, long count)
{
	cache_node	*node;
	char		expression[18];

	(void) snprintf(expression, sizeof (expression), "%lxR%lx", wch, count);

	if ((node = cache_lookup(expression)) == NULL) {
		/* we have not encountered this repeat before ... */
		node = new_cache_node(expression);

		if (count == MAX_N) {
#if defined(XPG4) || defined(XPG6)
			/* xpg4/tr - extend pattend to end of string */
			count = string1.totchars - string2.totchars + 1;
#else
			/* bin/tr - repeat character means "huge" */
			count = string1.totchars;
#endif
		}

		if (count > 0) {
			while (count--) {
				add_to_clist(&(node->clist), wch);
			}
		}
	}
	node->clist.offset = 0;
	return (&(node->clist));
}

/*
 * Convert up to MB_CUR_MAX octal sequences.  Then call mbtowc() to get a
 * a widechar.  Backtrack if needed, to reposition s->p to the beginning of
 * the unconsumed octal sequence(s), by keeping track of beginning of each
 * octal sequence in wptr[].
 */
int
get_octal(struct string *s, wchar_t *wval)
{
	int i, n, nbytes;
	int mret = -1;
	wchar_t c;

#define	MBMAX	10	/* set max number of bytes in mbchar to 10 */
	wchar_t *wptr[MBMAX];	/* array pointers into s->p */
	char nchr[MBMAX];	/* array of converted bytes */

	for (nbytes = 0; nbytes < mbcurmax; nbytes++) {
		wptr[nbytes] = s->p;
		nchr[nbytes] = '\0';
		c = *s->p++;
		if (c ==  L'\\') {
			switch (*s->p) {
			case L'0': case L'1': case L'2': case L'3':
			case L'4': case L'5': case L'6': case L'7':
				/* octal sequence */
				i = n = 0;
				while (i < 3 && *s->p >= L'0' &&
				    *s->p <= L'7') {
					n = n * 8 + (*s->p++ - L'0');
					i++;
				}
				nchr[nbytes] = n;
				break;
			default:
				s->p--;
				break;
			}
		} else {
			s->p--;
			break;
		}
	}

	if (nbytes > 0) {
		mret = MBTOWC_NATIVE(wval, nchr, nbytes);
	}

	if (mret > 0) {
		if (mret < nbytes) {
			s->p = wptr[mret];
		}
		return (0);
	} else if (mret == 0) {
		/*
		 * nchr must be the null byte
		 * wval will be set to L'\0' from above call to mbtowc()
		 */
		if (nbytes > 1) {
			s->p = wptr[1];
		}
		return (0);
	} else {
		/* mbtowc() failure */
		s->p = wptr[0];
		return (-1);
	}
}

/*
 * NAME: nextc
 *
 * FUNCTION: get the next character from string s with escapes resolved
 * EXIT:	1. IF (next character from s can be delivered as a single byte)
 *		   THEN return value = (int)cast of (next character or EOS)
 *		   ELSE error message is written to standard error
 *			and command terminates.
 */
static wchar_t
nextc(struct string *s, int f)
{
	wchar_t	c;

	c = *s->p++;
	s->octal &= ~f;

	if (c == L'\0') {
		--s->p;
		return (EOS);
	} else if (c == L'\\') {
		/* Resolve escaped '\', '[' or null */
		switch (*s->p) {

		case L'0': case L'1': case L'2': case L'3':
		case L'4': case L'5': case L'6': case L'7':
			s->p--;
			if (get_octal(s, &c) == -1) {
				return (EOS);
			}
			s->octal |= f;
			break;

		default:
			c = *s->p++;
			break;
		}
	}
	return (c);
}

static wchar_t
get_next_in_list(struct string *s)
{
	wchar_t nextchar = s->nextclass->chars[s->nextclass->offset++];

	if (s->nextclass->count <= s->nextclass->offset) {
		/* next round will continue on */
		s->nextclass = NULL;
	}
	s->nchars++;
	return (nextchar);
}

/*
 * NAME: next
 *
 * FUNCTION:	Get the next character represented in string s
 * ENTRY:	1. Flag = INSTRING1	- s points to string1
 *			  INSTRING2	- s points to string2
 *			  SIZEONLY	- compute string size only
 * EXIT:	1. IF (next character from s can be delivered as a single byte)
 *		   THEN return value = (int)cast of (next character or EOS)
 *		   ELSE error message is written to standard error
 *			and command terminates.
 */
static wchar_t
next(struct string *s, int flag)
{
	char		*class;
	int		c, n, bytes;
	int		base;
	wchar_t		basechar; /* Next member of char class to return */
	wchar_t		*dp;	  /* Points to ending : in :] of a class name */
	int		state;
	wchar_t		save1, save2;
	wchar_t		char1, opchar;
	wchar_t		ret;
	wchar_t		*savep;

	/*
	 * If we are generating class members, ranges or repititions
	 * get the next one.
	 */
	if (s->nextclass != NULL) {
		return (get_next_in_list(s));
	}

	char1 = *s->p;
	save1 = nextc(s, OCT_LOW);

#if defined(XPG4) || defined(XPG6)
	if ((char1 == '[') || (*s->p == '-'))
#else
	if (char1 == '[')
#endif
	{
		/*
		 * Check for character class, equivalence class, range,
		 * or repetition in ASCIIPATH. Implementation uses a state
		 * machine to parse the POSIX syntax. Convention used is
		 * that syntax characters specified by POSIX must appear
		 * as explicit characters while user-specified characters
		 * (range endpoints, repetition character, and equivalence
		 *  class character) may use escape sequences.
		 */
/*
 * STATE  STRING SEEN       *p          ACTION
 *
 *   1    [=	            '*'         STATE=4		'[<char1>*' <char1>='='
 *			    '<char1>="  STATE=8		'[=<char1>='
 *			    other       STATE=9
 *
 *   2	  [:	            '*'		STARE=5		'[<char1>*' <char1>=':'
 *		            <class>:]	ACCEPT		'[:<class>:]'
 *			    other	STATE=9
 *
 *   3	  [<char1>          '*'		STATE=5		'[<char1>*'
 *			    other	STATE=9
 *
 *   4    [=*               '='		STATE=8		'[=<char1>=' <char1>='*'
 *                          '<digit>'	STATE=7		'[<char1>*<digit>'
 *							<char1>='='
 *                          ']'		ACCEPT		'[<char1>*]' <char1>='='
 *			    other	STATE=9
 *
 *   5	  [<char1>*         ']'		ACCEPT		'[<char1>*]'
 *			    '<digit>'	STATE=7         '[<char1>*<digit>]'
 *			    other	STATE=9
 *
 *  (for XPG4 and XPG6 tr)
 *   6    <char1>-          '<char2>'	ACCEPT		'<char1>-<char2>'
 *			    other	STATE=9
 *
 *  (for bin tr)
 *   6    [<char1>-         '<char2>]'	ACCEPT		'[<char1>-<char2>]'
 *			    other	STATE=9
 *
 *   7    [<char1>*<digit>  '<digit>'	STATE=7a	'[<char1>*<digit>'
 *                          ']'		ACCEPT		'[<char1>*<digit>]'
 *			    other	STATE=9
 *
 *   7a   [<char1>*<digits> '<digit>'	STATE=7a	'[<char1>*<digits>'
 *                          ']'		ACCEPT		'[<char1>*<digits>]'
 *
 *   8    [=<char1>=        ']'		ACCEPT		'[=<char1>=]'
 *			    other	STATE=9
 *
 *   9    '[<other>' 'c-'		ACCEPT		'[' or 'c'
 *					(set to process second char next)
 *
 */
		n = MAX_N;	/* For short path to STATE_7b */
		savep = s->p;

#if defined(XPG4) || defined(XPG6)
		/* usr/xpg4/bin/tr and usr/xpg6/bin/tr */
		if (char1 == L'[') {
			opchar = *((s->p)++);
			if (opchar == L'=') {
				state = 1;
			} else if (opchar == L':') {
				state = 2;
			} else {	/* Allow escape conversion of char1. */
				s->p--;
				char1 = (uint_t)nextc(s, OCT_LOW);
				state = 3;
			}
		} else {
			s->p++;
			char1 = save1;
			state = 6;
		}

#else	/* !defined(XPG4) && !defined(XPG6) */
		/* usr/bin/tr */
		opchar = *((s->p)++);
		if (opchar == L'=') {
			state = 1;
		} else if (opchar == L':') {
			state = 2;
		} else {
			s->p--;
			char1 = (uint_t)nextc(s, OCT_LOW);
			opchar = *s->p++;
			if (opchar == L'-')
				state = 6;
			else {
				/* Allow escape conversion of char1. */
				s->p--;
				state = 3;
			}
		}
#endif	/* defined(XPG4) || defined(XPG6) */

		while (state != 0) {
		switch (state) {
		case 1:
			opchar = *((s->p)++);
			if (opchar == '*') {
				char1 = '=';
				state = 4;
			} else { /* Allow escape conversion of char1. */
				s->p--;
				char1 = (uint_t)nextc(s, OCT_LOW);
				if (*((s->p)++) == '=')
					state = 8;
				else
					state = 9;
			}
			break;
		case 2:
			opchar = *((s->p)++);
			if (opchar == '*') {
				state = 5;
			} else {
				/*
				 * Check for valid well-known character
				 * class name
				 */
				s->p--;
				if ((dp = wcschr(s->p, L':')) == NULL) {
					state = 9;
					break;
				}
				if (*(dp+1) != ']') {
					state = 9;
					break;
				}
				*dp = '\0';

				/* get class char list */
				bytes = (wcslen(s->p) + 1) * MB_LEN_MAX;
				class = alloca(bytes);
				(void) WCSTOMBS_NATIVE(class, s->p, bytes);

				*dp = L':';
				s->p = dp + 2;

				/*
				 * Check invalid use of char class in String2:
				 */
				if ((flag == INSTRING2) &&
				    ((strcmp(class, "lower") == 0 &&
				    (s->nchars != string1.belowupper)) ||
				    (strcmp(class, "upper") == 0 &&
				    (s->nchars != string1.belowlower)))) {
					(void) fprintf(stderr, gettext("%s: "
					    "String2 contains invalid "
					    "character class '%s'.\n"),
					    myName, class);
					exit(1);
				}

				if (strcmp(class, "upper") == 0) {
					s->class = "upper";
					s->belowupper = s->nchars;
				} else if (strcmp(class, "lower") == 0) {
					s->class = "lower";
					s->belowlower = s->nchars;
				}
				s->nextclass = character_class(class, flag);

				/* handle bogus classes */
				if (s->nextclass == NULL) {
					state = 11;
					break;
				}

				/* handle empty classes */
				if (s->nextclass->count == 0) {
					s->nextclass = NULL;
					return (next(s, flag));
				}
				return (get_next_in_list(s));
			}
			break;
		case 3:
			opchar = *((s->p)++);
			if (opchar == '*')
				state = 5;
			else
				state = 9;
			break;
		case 4:
			opchar = *((s->p)++);
			if (opchar == '=') {
				char1 = '*';
				state = 8;
			} else if (iswdigit(opchar))
				state = 7;
			else if (opchar == ']') {
				n = MAX_N; /* Unspecified length */
				state = 10; /* 7b */
			} else
				state = 9;
			break;
		case 5:
			opchar = *((s->p)++);
			if (opchar == ']') {
				n = MAX_N; /* Unspecified length */
				state = 10; /* 7b */
			} else if (iswdigit(opchar))
				state = 7;
			else
				state = 9;
			break;

		case 6:
#if	!defined(XPG4) && !defined(XPG6)
			if ((save2 = (uint_t)nextc(s, OCT_HIGH)) != EOS &&
			    (uint_t)nextc(s, 0) == (uint_t)']')
#else
			if ((save2 = (uint_t)nextc(s, OCT_HIGH)) != EOS)
#endif
			{
				s->nextclass = character_range(char1, save2,
				    s->octal);
				return (get_next_in_list(s));
			} else {
				state = 9;
			}
			break;

		case 7:
			base = (opchar == '0') ? 8 : 10;  /* which base */
			basechar = (opchar == '0') ? '7' : '9';
			n = opchar - (uint_t)'0';
			while (((c = (uint_t)*s->p) >= '0') &&
			    (c <= (int)basechar)) {
				n = base*n + c - (uint_t)'0';
				s->p++;
			}
			if (*s->p++ != ']') {
				state = 9;
				break;
			}
			if (n == 0)
				n = MAX_N; /* Unspecified length */
			/*FALLTHROUGH*/
		case 10:
			/*
			 * 7b, must follow case 7 without break;
			 * ACCEPT action for repetitions from states 4, 5, and
			 * 7.  POSIX 1003.2/D11 Rule: No repetitions in String1
			 */
			if (flag == INSTRING1) {
				(void) fprintf(stderr, gettext(
				    "%s: Character repetition in String1\n"),
				    myName);
				Usage();
			}

			if (flag == SIZEONLY) {
				s->nchars++;
				return (char1);
			}
			s->nextclass = character_repeat(char1, n);

			/* handle empty classes */
			if (s->nextclass->count == 0) {
				s->nextclass = NULL;
				return (next(s, flag));
			}
			return (get_next_in_list(s));
		case 8:
			if (*s->p++ == ']') {
			/* POSIX 1003.2/D11 Rule: No equiv classes in String2 */
				if (flag == INSTRING2 && (!dflag || !sflag)) {
					(void) fprintf(stderr,
					    gettext("%s: Equivalence class "
					    "in String2\n"), myName);
					Usage();
				}
				/* get class char list */
				s->nextclass = character_equiv_class(char1);

				/* handle empty classes */
				if (s->nextclass->count == 0) {
					s->nextclass = NULL;
					return (next(s, flag));
				}
				return (get_next_in_list(s));
			} else state = 9;
			break;
		case 9:
			s->p = savep;
			ret = save1;
			state = 0;
			break;

		default: /* ERROR state */
			(void) fprintf(stderr, gettext(
			    "%s: Bad string between [ and ].\n"), myName);
			exit(1);
			break;

		}	/* switch */
		}	/* while */
	} else {
		ret = save1;
	}
	s->nchars++;
	return (ret);
}


/*
 * remove_escapes - takes \seq and replace with the actual character value.
 * 		\seq can be a 1 to 3 digit octal quantity or {abfnrtv\}
 *
 *		This prevents problems when trying to extract multibyte
 * 		characters (entered in octal) from the translation strings
 *
 * Note:	the translation can be done in place, as the result is
 * 		guaranteed to be no larger than the source.
 */
#ifdef	XPG6
static void
remove_escapes(char *s)
#else
static void
remove_escapes(wchar_t *s)
#endif
{
	int	i, n;
#ifdef	XPG6
	char	*d = s;
	char	*ssav;
	int	ii, len;
#else
	wchar_t	*d = s;
	wchar_t	*ssav;
#endif

	while (*s) {
		if (*s == '\\') {
			ssav = s;
			switch (*++s) {
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
				/* octal sequence */
				i = n = 0;
				while (i < 3 && *s >= '0' && *s <= '7') {
					n = n * 8 + (*s++ - '0');
					i++;
				}
				if (n == 0) { /* \000 */
					*d++ = '\\';
					*d++ = '0';
				} else if (n == '\\') { /* \134 */
					*d++ = '\\';
					*d++ = '\\';
				} else if (n == '[') { /* \133 */
					*d++ = '\\';
					*d++ = '[';
				} else if (n == ':') { /* \072 */
					*d++ = '\\';
					*d++ = ':';
				} else if (n == '=') { /* \075 */
					*d++ = '\\';
					*d++ = '=';
				} else {
					/* Map octal sequences later (range) */
					do {
						*d++ = *ssav++;
					} while (i-- > 0);
				}
				break;
			case 'a':
				*d++ = '\a';
				s++;
				break;
			case 'b':
				*d++ = '\b';
				s++;
				break;
			case 'f':
				*d++ = '\f';
				s++;
				break;
			case 'n':
				*d++ = '\n';
				s++;
				break;
			case 'r':
				*d++ = '\r';
				s++;
				break;
			case 't':
				*d++ = '\t';
				s++;
				break;
			case 'v':
				*d++ = '\v';
				s++;
				break;
			case '\\':
				*d++ = '\\'; /* leave '\' escaped */
				*d++ = '\\';
				s++;
				break;
			case '[':
				*d++ = '\\'; /* leave '[' escaped */
				*d++ = '[';
				s++;
				break;
			default:
#ifdef	XPG6
				len = mblen((const char *)s, mbcurmax);
				if (len == -1)
					len = 1;
				for (ii = 0; ii < len; ii++) {
					*d++ = *s++;
				}
#else
				*d++ = *s++;
#endif
				break;
			}
		} else {
#ifdef	XPG6
			len = mblen((const char *)s, mbcurmax);
			if (len == -1)
				len = 1;
			for (ii = 0; ii < len; ii++) {
				*d++ = *s++;
			}
#else
			*d++ = *s++;
#endif
		}
	}
	*d = '\0';
}

static void
trans_string(struct string *s, char *instring)
{
	size_t	len;
	wchar_t	*p;

#ifdef	XPG6
	/* calls remove_escapes first in XPG6 */
	remove_escapes(instring);
#endif

	len = strlen(instring) + 1;
	p = allocate(NULL, len * sizeof (wchar_t));
#ifdef	XPG6
	if (!cflag) {
#endif
		/*
		 * -c is not specified in XPG6 or default in XPG4
		 * Handle the string as a sequence of characters
		 */
		if (MBSTOWCS_NATIVE(p, instring, len) == (size_t)-1) {
			(void) fprintf(stderr, gettext("tr: specified string "
			    "contains invalid character sequence\n"));
			exit(1);
		}
#ifndef	XPG6
		/*
		 * calls remove_escapes here in XPG4 for backward
		 * compatibility
		 */
		remove_escapes(p);
#endif
#ifdef	XPG6
	} else {
		/*
		 * -c is specified
		 * Handle the string as a sequence of bytes
		 */
		unsigned char	*ts = (unsigned char *)instring;
		wchar_t	*tp = p;
		while (*ts) {
			*tp++ = (wchar_t)*ts++;
		}
		*tp = L'\0';
	}
#endif
	s->p = p;
}

int
main(int argc, char **argv)
{
	FILE		*fi = stdin, *fo = stdout;
	int		save = (int)EOS;
	char_info	*idx;
	wint_t		wc;
	wchar_t		*temp, c, d, oc;

	realloc_size = PAGESIZE /* sizeof (wchar_t) */;

	/* Get locale variables from environment */
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	/* save program name */
	myName = argv[0];

	/* Parse command line */
#ifdef XPG6
	while ((oc = getopt(argc, argv, "cCds")) != -1)
#else
	while ((oc = getopt(argc, argv, "cds")) != -1)
#endif
	{
		switch (oc) {
#ifdef XPG6
		case 'C':
			if (cflag)
				Usage();
			Cflag++;
			break;
#endif
		case 'c':
#ifdef XPG6
			if (Cflag)
				Usage();
#endif
			cflag++;
			break;
		case 'd':
			dflag++;
			break;
		case 's':
			sflag++;
			break;

		default:
		/* Option syntax or bad option. */
			Usage();
		}
	}

	arg_count = argc - optind;

#if defined(XPG4) || defined(XPG6)

	/* validate options and operands */
	if (dflag) {
		if (sflag) {
			if (arg_count != 2)
				Usage();
		} else {
			if (arg_count != 1)
				Usage();
		}
	} else {
		if (sflag) {
			if (arg_count != 1 && arg_count != 2)
				Usage();
		} else {
			if (arg_count != 2) {
				Usage();
			}
		}
	}

#else	/* !defined(XPG4) && !defined(XPG6) */

	if (arg_count > 2) {
		Usage();
	}
#endif	/* defined(XPG4) || defined(XPG6) */

	load_character_set();

	/* Get translation strings */
#if !defined(XPG4) && !defined(XPG6)
	if (arg_count >= 1) {
		trans_string(&string1, argv[optind]);
	} else {
		string1.p = L"";
	}
#else
	trans_string(&string1, argv[optind]);
#endif
	if (arg_count == 2) {
		trans_string(&string2, argv[optind + 1]);
	} else {
		string2.p = L"";
	}

	string1.nextclass = NULL;
	string1.nchars = 0;
	string1.belowlower = -1;
	string1.belowupper = -1;

	/* expand out string1 - create the vector struct for string1 */
	temp = string1.p;
	while ((c = next(&string1, INSTRING1)) != EOS) {
		add_to_clist(&vector, c);
	}
	string1.p = temp;

#ifdef XPG6
	if (Cflag || cflag)
#else
	if (cflag)
#endif
	{
		/* complement the character set found in string 1 */
		int		i, j;

		qsort(vector.chars, vector.count, sizeof (wchar_t), sccompare);

		/*
		 * Now loop through character set adding any characters
		 * not found in string one to our temp list. The algorithm is
		 * not simpliest, but it is fast.
		 */

		idx = character_set.list;
		for (j = 0, i = 0; i < character_set.count; i++, idx++) {
			wchar_t	cur = idx->wc[0];
			if (j >= vector.count || vector.chars[j] != cur) {
				add_to_clist(&tvector, cur);
				continue;
			}

			do {
				j++;
				if (j >= vector.count)
					break;
			} while (vector.chars[j] == vector.chars[j-1]);
		}

		/* free up old vector's space */
		free(vector.chars);

		/* and point to new vector */
		vector = tvector;

#if defined(XPG6)
	if (Cflag)
#endif
#if defined(XPG4) || defined(XPG6)
		qsort(vector.chars, vector.count, sizeof (wchar_t),
		    wccompare);
#endif
	}
	string1.totchars = vector.count;

	string2.nextclass = NULL;
	string2.nchars = 0;
	string2.belowlower = -1;
	string2.belowupper = -1;

	temp = string2.p;
	while ((c = next(&string2, SIZEONLY)) != EOS)
		;
	string2.p = temp;
	string2.totchars = string2.nchars - 1;

	if (dflag) {
		vector.offset = 0;
		while (vector.offset < vector.count) {
			c = vector.chars[vector.offset++];
			if ((idx = character_set_index(c)) == NULL) {
				/* octal codepoint outside range */
				(void) fprintf(stderr,
				gettext("tr: codepoint outside range\n"));
				exit(1);
			}
			idx->delete = 1;
		}
		if (sflag && arg_count == 2) {
			temp = string2.p;
			while ((c = next(&string2, SIZEONLY)) != EOS) {
				if ((idx = character_set_index(c)) == NULL) {
					(void) fprintf(stderr, gettext("tr: "
					    "codepoint outside range\n"));
					exit(1);
				}
				idx->squeeze = 1;
			}
			string2.p = temp;
		}
	} else if (sflag && arg_count == 1) {
		vector.offset = 0;
		while (vector.offset < vector.count) {
			c = vector.chars[vector.offset++];
			if ((idx = character_set_index(c)) == NULL) {
				(void) fprintf(stderr,
				gettext("tr: codepoint outside range\n"));
				exit(1);
			}
			idx->squeeze = 1;
		}
	} else {
		/* Create mapping/translation table */
		vector.offset = 0;
		string2.nextclass = NULL;
		string2.nchars = 0;
		while (vector.offset < vector.count) {
			c = vector.chars[vector.offset++];
			if ((d = next(&string2, INSTRING2)) == EOS)
				break;
			if ((idx = character_set_index(c)) == NULL) {
				(void) fprintf(stderr,
				gettext("tr: codepoint outside range\n"));
				exit(1);
			}
			idx->translation = d;
			if (sflag) {
				idx = character_set_index(d);
				idx->squeeze = 1;
			}
		}
	}

	/*
	 * optimization for C locale, single-byte locales, and the case
	 * -c is specified in XPG6 mode
	 */
	if (is_C_locale || mbcurmax == 1) {
		int	ch;

		/* Read and process standard input using single bytes */
		for (;;) {
			if ((ch = getc(fi)) == EOF) {
				if (ferror(fi)) {
					perror(gettext("tr: Input file error"));
					exit(1);
				}
				break;
			}

#if !defined(XPG4) && !defined(XPG6)
			/* usr/bin/tr should delete NULL from input */
			if (ch == '\0')
				continue;
#endif
			idx = &character_set.list[ch];
			if (!idx->delete) {
				ch = (int)idx->translation;
				if (!sflag) {
					(void) putc(ch, fo);
				} else if (save != ch) {
					idx = &character_set.list[ch];
					if (idx->squeeze)
						save = ch;
					else
						save = -1;
					(void) putc(ch, fo);
				}
			}
		}
		exit(0);
	}

	/*
	 * Not in C locale.  Collation sequence is unknown and is not
	 * based on binary character value...
	 */
	for (;;) {
		/* Get next input multi-byte character: */
		if ((wc = FGETWC_NATIVE(fi)) == EOF) {
			if (ferror(fi)) {
				perror(gettext("tr: Input file error"));
				exit(1);
			}
			break;
		}

#if !defined(XPG4) && !defined(XPG6)
		/* usr/bin/tr should delete NULL from input */
		if (wc == L'\0')
			continue;
#endif
		idx = character_set_index(wc);
		if (idx == NULL) {
			/*
			 * Anything that gets here is an invalid character,
			 * if -d and -c are set then the character is deleted.
			 * Otherwise we just pass it untranslated.
			 * Reason: -cd implies all characters not represented
			 *	   in first argument.  The characters can't
			 *	   be represented since they are illegal...
			 */
			if (!(dflag && cflag)) {
				(void) __fputwc_native((wint_t)wc, fo);
			}
		} else if (!idx->delete) {
			wc = idx->translation;
			if (!sflag) {
				(void) __fputwc_native((wint_t)wc, fo);
			} else if (save != wc) {
				idx = character_set_index(wc);
				if (idx != NULL && idx->squeeze)
					save = wc;
				else
					save = -1;

				(void) __fputwc_native((wint_t)wc, fo);
			}
		}
	}
	return (0);
}

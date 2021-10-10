/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "iconv_int.h"

map_t	curr_map;
char	*yyfilenm;

static from_tbl_t	**from_tbl;
static symtab_t	from_symbol_tbl, to_symbol_tbl;
static symtab_t	*from_symtbl = &from_symbol_tbl;
static symtab_t	*to_symtbl = &to_symbol_tbl;

static void
tblinit(void)
{
	from_tbl = MALLOC(from_tbl_t *, 256);
	from_symtbl->size = HASH_TBL_SIZE_CHARMAP;
	from_symtbl->symbols = MALLOC(symbol_t *, HASH_TBL_SIZE_CHARMAP);

	to_symtbl->size = HASH_TBL_SIZE_CHARMAP;
	to_symtbl->symbols = MALLOC(symbol_t *, HASH_TBL_SIZE_CHARMAP);
}

static void
initparse(void)
{
	initlex();
	inityacc();
}

static int
get_eachbyte(uint64_t val, unsigned char **p)
{
	unsigned char	*eb;
	uint64_t	mask;
	int	i, nb;

	mask = 0xffULL << ((maxbytes - 1) * 8);
	nb = 1;
	for (i = maxbytes; mask != 0; mask >>= 8, i--) {
		if (val & mask) {
			nb = i;
			break;
		}
	}

	eb = MALLOC(unsigned char, nb);

	for (i = 0; i < nb; i++) {
		eb[nb - i - 1] = (unsigned char)(val & 0xff);
		val >>= 8;
	}

	*p = eb;
	return (nb);
}

static void
from_add_id(char *id, uint64_t val)
{
	int	nb, idx, i;
	from_tbl_t	**tp, *p;
	unsigned char	*eb;

	/*
	 * nb:			the number of bytes making val
	 * eb[0] ... eb[nb-1]:	each byte making val
	 */
	nb = get_eachbyte(val, &eb);

	idx = 0;
	tp = from_tbl;

	while (nb > 0) {
		/* setting non-terminal entries */
		if ((p = tp[eb[idx]]) == NULL) {
			p = MALLOC(from_tbl_t, 1);
			tp[eb[idx]] = p; /* drop in the entry */
			/*
			 * At this point, p->status is S_NONE.
			 * Eventually it becomes either
			 * S_TERMINAL or S_NONTERMINAL.
			 * It will never be left as S_NONE.
			 */
		}
		nb--;
		idx++;
		if (nb > 0) {
			switch (p->status) {
			case S_TERMINAL:
				/*
				 * This entry has been already marked as
				 * a terminal.
				 */
				error(gettext("Invalid sequence found in "
				    "the character symbol '%s' in frommap\n"),
				    id);
				/* NOTREACHED */
			case S_NONE:
				/*
				 * New non-terminal entry
				 * preload the table
				 */
				p->status = S_NONTERMINAL;
				p->data.tp = MALLOC(from_tbl_t *, 256);
				break;
			}
		}
		tp = p->data.tp;
	}
	free(eb);
	/* setting terminal entry */
	switch (p->status) {
	case S_NONTERMINAL:
		/*
		 * This entry has been already marked as
		 * a non-terminal.
		 */
		error(gettext("Invalid sequence found in the character "
		    "symbol '%s' in frommap\n"), id);
		/* NOTREACHED */
	case S_NONE:
		/* New terminal entry */
		p->status = S_TERMINAL;
		p->data.id = MALLOC(idlist_t, 1);
		p->data.id->no = 1;
		p->data.id->ids = MALLOC(char *, 1);
		p->data.id->ids[0] = id;
		break;
	case S_TERMINAL:
		for (i = 0; i < p->data.id->no; i++) {
			if (strcmp(p->data.id->ids[i], id) == 0) {
				/* duplicate entry; just ignoring */
				free(eb);
				free(id);
				return;
			}
		}
		p->data.id->no++;
		p->data.id->ids = REALLOC(char *, p->data.id->ids,
		    p->data.id->no);
		p->data.id->ids[i] = id;
		break;
	}
}

static int
extract_digit_list(char *s, int *value)
{
	char	*endstr;
	int	i;

	for (i = 0; s[i] != '\0' && !isdigit(s[i]); i++)
		;
	*value = (int)strtol(&s[i], &endstr, 10);
	if (*endstr != '>') {
		return (0);
	}
	return ((int)(endstr - &s[i]));
}

static char *
build_symbol_fmt(char *id1, char *id2, int *startp, int *endp)
{
	char	*fmt, *s;
	size_t	fmtlen;
	int	i, n_dig0, n_dig1;

	n_dig0 = extract_digit_list(id1, startp);
	n_dig1 = extract_digit_list(id2, endp);

	if (n_dig0 != n_dig1 || n_dig0 == 0) {
		return (NULL);
	}

	if (*startp > *endp) {
		return (NULL);
	}

	/* 3: '%' '0' num 'd' '>', 10: num (INT_MAX) */
	fmtlen = strlen(id1) - n_dig0 + 4 + 10 + 1;
	fmt = MALLOC(char, fmtlen);
	for (i = 0, s = id1; !isdigit(s[i]); i++) {
		fmt[i] = s[i];
	}
	fmt[i++] = '%';
	(void) snprintf(&fmt[i], fmtlen - i, "0%dd>", n_dig0);
	return (fmt);
}


/*
 * Return:
 *	0:	Succeeded
 *	1:	Failed - multiple correspondings found
 *	2:	Failed - no corresponding found
 */
static int
find_tomap(uint64_t *valp, idlist_t *idlist)
{
	int	i, already_found;
	uint64_t	p_val = 0;
	symbol_t	*sym;

	already_found = 0;
	for (i = 0; i < idlist->no; i++) {
		sym = loc_symbol(idlist->ids[i], to_symtbl);
		if (!sym) {
			continue;
		}
		if (already_found) {
			if (sym->val != p_val) {
				/* multiple conflicting entries */
				return (1);
			}
		} else {
			already_found = 1;
			p_val = sym->val;
		}
	}
	if (!already_found) {
		return (2);
	}
	*valp = p_val;
	idlist->val = p_val;
	idlist->cached = 1;

	return (0);
}

void
set_mbcurmax(int num)
{
#ifdef	DEBUG
	(void) printf("Setting mb_cur_max = %d\n", num);
	(void) fflush(stdout);
#endif
	if (num > MAX_BYTES) {
		error(gettext("Specified %d for MB_CUR_MAX is too large.\n"),
		    num);
	} else {
		maxbytes = num;
	}
}

void
add_symbol_def(char *id, uint64_t val)
{
	symbol_t	*sym;
	symtab_t	*symtbl;

#ifdef	DEBUG
	(void) printf("Defining: \"%s\" = %llx\n", id, val);
	(void) fflush(stdout);
#endif
	symtbl = (curr_map == FROMMAP) ? from_symtbl : to_symtbl;

	sym = loc_symbol(id, symtbl);
	if (!sym) {
		sym = create_symbol(id, val);
		add_symbol(sym, symtbl);
	} else {
		if (sym->val != val) {
			error(
			    gettext("The character symbol '%s' has been "
				"already specified.\n"), id);
		}
		return;
	}

	if (curr_map == FROMMAP) {
		from_add_id(id, val);
	}
}

void
add_symbol_range_def(char *id1, char *id2, uint64_t val)
{
	char	*id, *fmt;
	size_t	idlen;
	int	i, start, end;
	uint64_t	rval;
	symbol_t	*sym;
	symtab_t	*symtbl;

#ifdef	DEBUG
	(void) printf("Defining: \"%s\" ... \"%s\" = %llx\n",
	    id1, id2, val);
	(void) fflush(stdout);
#endif

	symtbl = (curr_map == FROMMAP) ? from_symtbl : to_symtbl;

	sym = loc_symbol(id1, symtbl);
	if (sym) {
		if (sym->val != val) {
			error(
			    gettext("The character symbol '%s' "
				"has been already "
				"specified.\n"), id1);
		}
	}
	fmt = build_symbol_fmt(id1, id2, &start, &end);
	if (!fmt) {
		error(
		    gettext("The symbol range containing %s and %s "
			"is incorrectly formatted.\n"), id1, id2);
	}
	sym = loc_symbol(id2, symtbl);
	if (sym) {
		if (sym->val != val + (end - start)) {
			error(
			    gettext("The character symbol '%s' "
				"has been already "
				"specified.\n"), id2);
		}
	}

	idlen = strlen(id1) + 1;
	rval = val;
	for (i = start; i <= end; i++) {
		char	*tmp_name;

		if (i == start) {
			id = id1;
		} else if (i == end) {
			id = id2;
		} else {
			int	r;

			tmp_name = MALLOC(char, idlen);
			r = snprintf(tmp_name, idlen,
			    fmt, i);
			if (r >= idlen) {
				exit(4);
			}
			id = tmp_name;
			sym = loc_symbol(tmp_name, symtbl);
			if (sym) {
				if (sym->val != rval) {
					error(
					    gettext("The character symbol '%s' "
						"has been already "
						"specified.\n"), tmp_name);
				}
				/*
				 * duplicate entry has been found
				 */
				rval++;
				free(tmp_name);
				continue;
			}
		}
		sym = create_symbol(id, rval);
		add_symbol(sym, symtbl);

		if (curr_map == FROMMAP) {
			from_add_id(id, rval);
		}

		rval++;
	}
	free(fmt);
}

int
use_charmap_init(struct conv_info *cip)
{
	const char	*frommap, *tomap;

	frommap = cip->from;
	tomap = cip->to;

	infp = fopen(frommap, "r");
	if (infp == NULL) {
		(void) fprintf(stderr,
		    gettext("Cannot access the charmap file %s.\n"),
		    frommap);
		return (1);
	}
	initparse();
	tblinit();
	curr_map = FROMMAP;

	yyfilenm = (char *)frommap;

	(void) yyparse();
	(void) fclose(infp);

	infp = fopen(tomap, "r");
	if (infp == NULL) {
		(void) fprintf(stderr,
		    gettext("Cannot access the charmap file %s.\n"),
		    tomap);
		return (1);
	}
	initparse();
	curr_map = TOMAP;

	yyfilenm = (char *)tomap;

	(void) yyparse();
	(void) fclose(infp);

	return (0);
}

/*
 * err = 0:	Not a valid member of frommap found
 * err = 1:	More than one corresponding characters found in tomap
 * err = 2:	No corresponding character found in tomap
 */
static int
handle_invalid(int err, int flags, unsigned char *s, int f, int t)
{
	int	no_c, no_s;

	no_c = !(flags & F_NO_INVALID_OUTPUT);
	no_s = !(flags & F_SUPPRESS_ERR_MSG);

	if (no_s) {
		/* don't suppress error messages */
		switch (err) {
		case 0:
			(void) fprintf(stderr,
			    gettext("Invalid member of frommap found "
				"in input.\n"));
			break;
		case 1:
			(void) fprintf(stderr,
			    gettext("More than one corresponding characters "
				"found in tomap.\n"));
			break;
		case 2:
			(void) fprintf(stderr,
			    gettext("No corresponding character "
				"found in tomap.\n"));
			break;
		}
		if (no_c) {
			return (1);
		} else {
			int	i;
			(void) fprintf(stderr, gettext("Omitting: "));
			for (i = f; i <= t; i++) {
				(void) fprintf(stderr, "0x%0x ", s[i]);
			}
			(void) fputc('\n', stderr);
			return (0);
		}
	} else {
		/* suppress error messages */
		if (no_c) {
			return (1);
		} else {
			return (0);
		}
	}
}

int
use_charmap(struct conv_info *cip)
{
	FILE	*fp = cip->fp;
	unsigned char	s[sizeof (uint64_t)];
	int	c, idx, nb, err, ret;
	unsigned char	*eb;
	from_tbl_t	*p, **tp;
	uint64_t	val;
	unsigned char	pushback[sizeof (uint64_t)], pushback_cnt;

	err = 0;
	idx = 0;
	pushback_cnt = 0;
	for (; ; ) {
		if (idx == 0)
			tp = from_tbl;
		if (pushback_cnt) {
			/* if remaining bytes in pushback buffer */
			c = pushback[--pushback_cnt];
		} else {
			if ((c = fgetc(fp)) == EOF) {
				if (idx != 0) {
					(void) handle_invalid(0, cip->flags,
					    s, 0, 0);
					err = 1;
				}
				break;
			}
		}
		s[idx++] = (unsigned char)c;
		if ((p = tp[c]) == NULL) {
			/* invalid char */
			err = 1;
			if (handle_invalid(0, cip->flags, s, 0, 0)) {
				/* stop conversion */
				break;
			}
			/* omitting the first byte */
			while (--idx > 0) {
				pushback[pushback_cnt++] = s[idx];
			}
			continue;
		}
		if (p->status == S_NONTERMINAL) {
			tp = p->data.tp;
			continue;
		}

		/* got the sequence in the frommap */
		if (p->data.id->cached) {
			/* converted value is cached */
			val = p->data.id->val;
		} else if ((ret = find_tomap(&val, p->data.id)) != 0) {
			if (handle_invalid(ret, cip->flags, s, 0, idx-1)) {
				/* stop conversion */
				err = 1;
				break;
			}
			err = 1;
			/*
			 * valid character but no valid corresponding
			 * character in the destination.
			 * discard s[0] ... s[idx-1]
			 */
			idx = 0;
			continue;
		}
		nb = get_eachbyte(val, &eb);
		(void) fwrite(eb, 1, nb, stdout);
		free(eb);
		idx = 0;
	}

	if (ferror(fp)) {
		(void) fprintf(stderr,
		    gettext("Error in reading the input.\n"));
		exit(2);
	}
	return (err);
}


/*ARGSUSED*/
int
use_charmap_fini(struct conv_info *cip)
{
	return (0);
}


#ifdef	DEBUG
static void
showlist(from_tbl_t *p, char *cbuf)
{
	int	i;
	char	tbuf[1024];
	from_tbl_t	*q;

	if (p->min == 0)
		return;

	if (p->min > 1) {
		q = p->data.tp;
		for (i = 0; i < 256; i++) {
			(void) snprintf(tbuf, sizeof (tbuf), "%s0x%02x ",
			    cbuf, i);
			showlist(&q[i], tbuf);
		}
		return;
	}
	(void) printf("%s", cbuf);
	for (i = 0; i < p->data.id->no; i++) {
		printf("\"%s\" ", p->data.id->ids[i]);
	}
	(void) printf("\n");
}

void
set_frommap(void)
{
	int	i;

	int	idx;
	char	cbuf[1024];

	(void) printf("frommap set\n");
	(void) fflush(stdout);

	idx = 0;
	for (i = 0; i < 256; i++) {
		(void) snprintf(cbuf, sizeof (cbuf), "0x%02x ", i);
		showlist(&from_tbl[i], cbuf);
	}
}

void
set_tomap(void)
{
	int	i;
	symbol_t	*p;
	symtab_t	*symtbl;
	int	noent, total;

	(void) printf("tomap set\n");
	(void) fflush(stdout);

	noent = 0;
	total = 0;
	symtbl = to_symtbl;
	for (i = 0; i < symtbl->size; i++) {
		int	c;
		p = symtbl->symbols[i];
		if (!p) {
			noent++;
			continue;
		}
		c = 0;
		while (p) {
			c++;
			(void) printf("\"%s\": %llx, ", p->id, p->val);
			p = p->next;
		}
		printf("\n");
		total += c;
	}
	(void) fprintf(stderr, "number of empty slots: %d\n", noent);
	(void) fprintf(stderr, "average number: %f\n",
	    (float)total/(float)(symtbl->size - noent));
}
#endif

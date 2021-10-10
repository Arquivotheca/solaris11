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
 */
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: sem_coll.c,v $ $Revision: 1.4.4.2 $"
 *	" (OSF) $Date: 1992/10/27 01:54:10 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 * FUNCTIONS:
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.13  com/cmd/nls/sem_coll.c, cmdnls, bos320, 9137320a 9/6/91 11:41:08
 */
#include <stdlib.h>
#include "locdef.h"

struct ell_list {
	item_t	**itm;
	int	no;
};

struct coll_range {
	symbol_t	*start, *end;
	struct ell_list	*elist;
	int	order;
};
static struct coll_range	rangeinfo;

typedef struct collrec {
	symbol_t	*tgt_symp;
	wchar_t		tgt_wc;
	int		seqno;
	char		wcvalid;	/* 1 if wc is valid */
	char		order;		/* order being set */
	char		mark;		/* temporary use */
	char		pad;
	wchar_t		weight;		/* weight we are setting */
	_LC_weight_t	*weightp;	/* place we are putting weight */
	/*
	 * refcnt is the number of symbols which are being referred by
	 * this weight entry. refp points those symbols.
	 * If weight was PENDING, refcnt is always 1, and refp points
	 * a symbol who's weight is still pending (forward reference).
	 */
	int		refcnt;
	symbol_t 	**refp;
	wchar_t		*wstr;
	size_t		wslen;
	struct collrec	*l_next;
	struct collrec	*l_hash_sym;
} collrec_t;

#define	CRHASHSZ	4093
static collrec_t	*collrec_hash_sym[CRHASHSZ];
static collrec_t	*collrec_head, *collrec_tail;

struct collel_list {
	wchar_t		wc;
	int		nsymbol;
	symbol_t	**symbol;
	struct collel_list *collel_next;
};
static struct collel_list *all_collel;

static _LC_weight_t	*t_weights;

static wchar_t nxt_coll_val = MIN_WEIGHT;
static wchar_t nxt_r_order = MIN_WEIGHT;
static wchar_t undefined_weight;

static symbol_t	*p_col_id = NULL;
symbol_t	*ellipsis_sym;

static wchar_t	coll_wgt_min = 0;	/* minimum collating weight */
static wchar_t	coll_wgt_max = 0;	/* maximum collating weight */
static wchar_t	coll_r_ord_min = 0;	/* minimum relative order */
static wchar_t	coll_r_ord_max = 0;	/* maximum relative order */

#define	OLD_DUP_HANDLING	0x01
#define	OLD_UNDEF_HANDLING	0x02
#define	OLD_FORREF_HANDLING	0x04
static int	old_handling;

#define	WARN_THRESHOLD	40
static int	nwarnd, nsuppressed;

static void	sem_set_dflt_collwgt_range(void);
static void	sem_set_collwgt_range(void);
static void	sem_collel_list(item_t **, int, _LC_weight_t *,
    symbol_t *, wchar_t, uint64_t, int);
static void	set_coll_wgt(_LC_weight_t *, wchar_t, int);
static void	set_coll_wgt_rec(_LC_weight_t *, wchar_t, int,
    symbol_t *, wchar_t, struct ell_list *);
static wchar_t	nxt_coll_wgt(void);
static void	free_elist(struct ell_list *, int);
static collrec_t *find_collrec(symbol_t *, int);
static collrec_t *find_collrec_by_wc(wchar_t, int);
static void	adjust_weight(void);
static void	handle_undefined(_LC_weight_t *, int);
static void	handle_undefined_collel(_LC_weight_t *, wchar_t, int);
static void	build_extinfo(void);

#define	NUM_ORDER	(collate.co_nord + 1) 	/* number of orders; 1-based */
#define	NUM_ORDER_R	(NUM_ORDER + 1)		/* number of orders + r_ord */
#define	R_ORD_IDX	(collate.co_nord + 1)
					/* index to the relative weight table */
#define	set_r_order(wgt, w)	(*(wgt))[R_ORD_IDX] = (w)
#define	NEW_WEIGHT()		alloc_new_weight(NUM_ORDER_R)
#define	NEW_WEIGHT_MAX()	alloc_new_weight(COLL_WEIGHTS_MAX)

/*
 * Regular expression special characters.
 */
#define	REGX_SPECIALS  "[*+.?({|^$"

#define	_WCHAR_MAX	MAX_PC

#ifdef	_LDEB
static void
dump_cr(collrec_t *cr)
{
	int	i;
	(void) printf("-----------------------------\n");
	(void) printf("cr: %p\n", cr);

	(void) printf("tgt_symp: %s\n",
	    (cr->tgt_symp && cr->tgt_symp->sym_id) ? cr->tgt_symp->sym_id :
	    "NULL");
	(void) printf("tgt_wc: %#x\n", cr->tgt_wc);
	(void) printf("seqno: %d\n", cr->seqno);
	(void) printf("wcvalid: %d\n", cr->wcvalid);
	(void) printf("order: %d\n", cr->order);
	(void) printf("mark: %d\n", cr->mark);

	switch (cr->weight) {
	case UNDEFINED:
		(void) printf("weight: UNDEFINED\n");
		break;
	case IGNORE:
		(void) printf("weight: IGNORE\n");
		break;
	case SUB_STRING:
		(void) printf("weight: SUB_STRING\n");
		break;
	case PENDING:
		(void) printf("weight: PENDING\n");
		break;
	default:
		(void) printf("weight: %x\n", cr->weight);
		break;
	}
	(void) printf("weightp: %p\n", cr->weightp);

	(void) printf("refcnt: %d\n", cr->refcnt);
	for (i = 0; i < cr->refcnt; i++) {
		(void) printf("refp[%d]: %s\n", i,
		    cr->refp[i]->sym_id ? cr->refp[i]->sym_id : "NULL");
	}
	if (cr->wstr == NULL) {
		(void) printf("wstr: NULL\n");
	} else {
		for (i = 0; cr->wstr[i] != L'\0'; i++) {
			(void) printf("cr->wstr[%d]: %x\n", i, cr->wstr[i]);
		}
	}
	(void) printf("l_next: %p\n", cr->l_next);
	(void) printf("l_hash_sym: %p\n", cr->l_hash_sym);
}

static void
dump_link(void)
{
	collrec_t	*cr;

	(void) printf("====================================================\n");
	for (cr = collrec_head; cr != NULL; cr = cr->l_next) {
		dump_cr(cr);
	}
	(void) printf("====================================================\n");
}
#endif

static void
coll_diag_error(const char *msg, ...)
{
	va_list ap;

	nwarnd++;
	if (verbose <= 1 && nwarnd > WARN_THRESHOLD) {
		nsuppressed++;
		return;
	}

	va_start(ap, msg);
	diag_verror(msg, ap);
	va_end(ap);
}

static void
coll_diag_error2(const char *msg, ...)
{
	va_list ap;

	if (nwarnd > WARN_THRESHOLD)
		return;
	va_start(ap, msg);
	(void) vfprintf(stderr, msg, ap);
	va_end(ap);
}

static wchar_t *
alloc_new_weight(size_t num)
{
	wchar_t	*tgt;

	tgt = MALLOC(wchar_t, num);
	(void) wmemset(tgt, UNDEFINED, num);

	return (tgt);
}

static void
free_elist(struct ell_list *p, int order)
{
	int	i, n;
	if (!p)
		return;

	for (i = 0; i <= order; i++) {
		if (p[i].itm) {
			for (n = 0; n < p[i].no; n++)
				destroy_item(p[i].itm[n]);
			free(p[i].itm);
		}
	}
	free(p);
}

void
check_range(void)
{
	if (rangeinfo.start) {
		rangeinfo.end = NULL;
		sem_set_collwgt_range();
		rangeinfo.start = NULL;
		rangeinfo.end = NULL;
		free_elist(rangeinfo.elist, rangeinfo.order);
	}
}

static wchar_t
get_nxt_wgt(wchar_t val)
{
	if (val > INT_MAX) {
		error(2, gettext(ERR_NO_MORE_WEIGHTS));
	}

	/* No null bytes permitted */
	if ((val & 0x000000ff) == 0)
		val |= 0x01;
	if ((val & 0x0000ff00) == 0)
		val |= 0x0100;
	if ((val & 0x00ff0000) == 0)
		val |= 0x010000;

	return (val);
}

/*
 *  FUNCTION: nxt_coll_wgt
 *
 *  DESCRIPTION:
 *  Collation weights cannot have a zero in either the first or second
 *  byte (assuming two byte wchar_t).
 */
static wchar_t
nxt_coll_wgt(void)
{
	wchar_t collval;

	collval = nxt_coll_val;
	nxt_coll_val = get_nxt_wgt(collval + 1);

	if (nxt_coll_val >= _WCHAR_MAX)
		INTERNAL_ERROR;

	coll_wgt_max = collval;
	return (collval);
}

static wchar_t
nxt_r_ord(void)
{
	wchar_t	rval;

	rval = nxt_r_order;
	nxt_r_order = get_nxt_wgt(rval + 1);

	coll_r_ord_max = rval;
	return (rval);
}

/*
 *  FUNCTION: set_coll_wgt
 *
 *  DESCRIPTION:
 *  Sets the collation weight for order 'ord' in weight structure 'weights'
 *  to 'weight'.  If 'ord' is -1, all weights in 'weights' are set to
 *  'weight'.
 */
static void
set_coll_wgt(_LC_weight_t *weights, wchar_t weight, int ord)
{
	/* check if weights array has been allocated yet */
	if (*weights == NULL)
		*weights = NEW_WEIGHT();

	if (ord == -1) {
		(void) wmemset(*weights, weight, NUM_ORDER);
	} else {
		if (ord < 0 || ord >= NUM_ORDER)
			INTERNAL_ERROR;

		(*weights)[ord] = weight;
	}
}

/*
 *  FUNCTION: get_coll_wgt
 *
 *  DESCRIPTION:
 *  Gets the collation weight for order 'ord' in weight structure 'weights'.
 */
static wchar_t
get_coll_wgt(_LC_weight_t *weights, int ord)
{
	/* check if weights array has been allocated yet */
	if (*weights == NULL)
		INTERNAL_ERROR;

	/* get_coll_wgt() won't be used to obtain the relative weight order */
	if (ord >= 0 && ord < NUM_ORDER)
		return ((*weights)[ord]);
	else
		error(4, gettext(ERR_TOO_MANY_ORDERS));
	/*NOTREACHED*/
	return (0);
}

/*
 *  FUNCTION: check_if_assigned
 *
 *  DESCRIPTION:
 *  Check to see if weights are already specifed for the symbol.
 */
static int
check_if_assigned(_LC_weight_t *wgt, symbol_t *sym)
{
	if (get_coll_wgt(wgt, 0) != UNDEFINED &&
	    find_collrec(sym, -1) != NULL) {
#ifdef OLD_DUP_HANDLING
		if (old_handling & OLD_DUP_HANDLING) {
			coll_diag_error(gettext(ERR_DUP_COLL_SPEC_IGN),
			    sym->sym_id);
			return (1);
		}
#endif
		coll_diag_error(gettext(ERR_DUP_COLL_SPEC), sym->sym_id);
	}
	return (0);
}

/*
 *  FUNCTION: check_if_assigned_range
 *
 *  DESCRIPTION:
 *  Check to see if weights are already specifed for the range specification.
 */
static int
check_if_assigned_range(_LC_weight_t *wgt,
    wchar_t wc, uint64_t fc, char *start_sym, char *end_sym)
{
	char	mbs[24];

	if (get_coll_wgt(wgt, 0) != UNDEFINED &&
	    find_collrec_by_wc(wc, -1) != NULL) {
		(void) snprintf(mbs, sizeof (mbs), "0x%llx", fc);
#ifdef OLD_DUP_HANDLING
		if (old_handling & OLD_DUP_HANDLING) {
			coll_diag_error(gettext(ERR_DUP_COLL_RNG_SPEC_IGN),
			    mbs, start_sym, end_sym);
			return (1);
		}
#endif
		coll_diag_error(gettext(ERR_DUP_COLL_RNG_SPEC),
		    mbs, start_sym, end_sym);
	}
	return (0);
}

/*
 *  FUNCTION: sem_init_colltbl
 *
 *  DESCRIPTION:
 *  Initialize the collation table.  This amounts to setting all collation
 *  values to IGNORE, assigning the default collation order (which is 1),
 *  allocating memory to contain the table.
 */
void
sem_init_colltbl(void)
{
	/* initialize collation attributes to defaults */
	collate.co_nord   = 0;	/* potentially modified by 'order' */
	collate.co_wc_min = 0;	/* always 0 */
	collate.co_wc_max = max_wchar_enc;	/* always max_wchar_enc */

	/* allocate and zero fill memory to contain collation table */
	collate.co_coltbl = MALLOC(_LC_weight_t, max_wchar_enc+1);
	collate.co_cetbl = MALLOC(_LC_collel_t *, max_wchar_enc+1);
	/* set default min and max collation weights */
	collate.co_col_min = collate.co_col_max = 0;

	/* initialize substitution strings */
	collate.co_nsubs = 0;
	collate.co_subs  = NULL;

	rangeinfo.start = NULL;
	rangeinfo.end = NULL;

	ellipsis_sym = MALLOC(symbol_t, 1);
	ellipsis_sym->sym_type = ST_ELLIPSIS;

#ifdef OLD_DUP_HANDLING
	if (getenv("SUNW_LOCALEDEF_OLD_DUP_HANDLING") != NULL)
		old_handling |= OLD_DUP_HANDLING;
#endif
#ifdef OLD_UNDEF_HANDLING
	if (getenv("SUNW_LOCALEDEF_OLD_UNDEF_HANDLING") != NULL)
		old_handling |= OLD_UNDEF_HANDLING;
#endif
#ifdef OLD_FORREF_HANDLING
	if (getenv("SUNW_LOCALEDEF_OLD_FORREF_HANDLING") != NULL)
		old_handling |= OLD_FORREF_HANDLING;
#endif
}

/*
 *  FUNCTION: sem_push_collel();
 *  DESCRIPTION:
 *  Copies a symbol from the symbol stack to the semantic stack.
 */
void
sem_push_collel(void)
{
	symbol_t *s;
	item_t   *i;

	s = sym_pop();
	i = create_item(SK_SYM, s);
	(void) sem_push(i);
}


/*
 *  FUNCTION: loc_collel
 *
 *  DESCRIPTION:
 *  Locates a collation element in an array of collation elements.  This
 *  function returns the first collation element which matches 'sym'.
 */
static _LC_collel_t *
loc_collel(char *sym, wchar_t pc)
{
	_LC_collel_t *ce;

	for (ce = collate.co_cetbl[pc]; ce->ce_sym != NULL; ce++) {
		if (strcmp(sym, ce->ce_sym) == 0)
			return (ce);
	}

	INTERNAL_ERROR;
	/* NOTREACHED */
}


/*
 *  FUNCTION: sem_coll_sym_ref
 *
 *  DESCRIPTION:
 *  checks that the symbol referenced has a collation weights structure
 *  attached.  If one is not yet present, one is allocated as necessary
 *  for the symbol type.
 */
int
sem_coll_sym_ref(void)
{
	_LC_collel_t *ce;
	symbol_t	*s;
	int	ret = COLL_OK;

	/* Pop symbol specified off of symbol stack */
	s = sym_pop();

	/*
	 * Check that this element has a weights array with
	 * the correct number of orders.  If the element does not, create
	 * one and initialize the contents to UNDEFINED.
	 */
	switch (s->sym_type) {
	case ST_UNDEF_SYM:
		ret = COLL_ERROR;
		break;

	case ST_CHR_SYM:
		if (s->data.chr->wgt == NULL)
			s->data.chr->wgt =
			    &(collate.co_coltbl[s->data.chr->wc_enc]);
		break;

	case ST_COLL_ELL:
		ce = loc_collel(s->data.collel->sym, s->data.collel->pc);
		if (ce->ce_wgt == NULL) {
			ce->ce_wgt = NEW_WEIGHT();
		}
		break;

	case ST_COLL_SYM:
		if (*(s->data.collsym) == NULL) {
			*(s->data.collsym) = NEW_WEIGHT();
		}
		break;
	default:
		INTERNAL_ERROR;
	}

	(void) sym_push(s);
	return (ret);
}


/*
 *  FUNCTION: sem_coll_literal_ref
 *
 *  DESCRIPTION:
 *  A character literal is specified as a collation element.  Take this
 *  element and create a dummy symbol for it.  The dummy symbol is pushed
 *  onto the symbol stack, but is not added to the symbol table.
 */
void
sem_coll_literal_ref(void)
{
#define	SYMID_LEN	24 /* > "0x + 16 digits + null" */
	symbol_t *dummy;
	item_t   *it;
	wchar_t  pc;
	int	rc;
	uint64_t	fc;


	/* Pop the file code to use as character off the semantic stack. */
	it = sem_pop();
	fc = it->value.uint64_no;

	/* Create a dummy symbol with this byte list as its encoding. */
	dummy = MALLOC(symbol_t, 1);
	dummy->sym_type = ST_CHR_SYM;
	dummy->data.chr = MALLOC(chr_sym_t, 1);

	/* save file code for character */
	dummy->data.chr->fc_enc = fc;

	/* use hex translation of file code for symbol id (for errors) */
	dummy->sym_id = MALLOC(char, SYMID_LEN);
	(void) snprintf(dummy->sym_id, SYMID_LEN, "0x%llx", fc);

	/* save length of character */
	dummy->data.chr->len =
	    mbs_from_fc((char *)dummy->data.chr->str_enc, fc);

	/* check if characters this long are valid */
	if (dummy->data.chr->len > mb_cur_max)
		error(4, gettext(ERR_CHAR_TOO_LONG), dummy->sym_id);

	/* define process code for character literal */
	rc = INT_METHOD(
	    (int (*)(_LC_charmap_t *, wchar_t *, const char *, size_t))
	    METH_OFFS(CHARMAP_MBTOWC_AT_NATIVE))(&charmap, &pc,
	    (const char *)dummy->data.chr->str_enc, MB_LEN_MAX);
	if (rc < 0)
		error(4, gettext(ERR_UNSUP_ENC), dummy->sym_id);

	dummy->data.chr->wc_enc = pc;

	/* clear out wgt and subs_str pointers */
	dummy->data.chr->wgt = NULL;
	dummy->data.chr->subs_str = NULL;

	/* mark character as defined */
	define_wchar(pc);

	destroy_item(it);
	(void) sym_push(dummy);
}


/*
 *  FUNCTION: sem_def_substr
 *
 *  DESCRIPTION:
 *  Defines a substitution string.
 */
static void
sem_def_substr(void)
{
	item_t	*it0, *it1;
	char	*src, *tgt;
	_LC_subs_t *subs;
	int	i, j;
	int	flag;

	it1 = sem_pop();		/* target string */
	it0 = sem_pop();		/* source string */

	if (it1->type != SK_STR || it0->type != SK_STR)
		INTERNAL_ERROR;

	/* Translate and allocate space for source string */
	src = copy_string(it0->value.str);

	/* Translate and allocate space for target string */
	tgt = copy_string(it1->value.str);

	/*
	 * Substitute to same string doesn't really make sense.
	 */
	if (strcmp(src, tgt) == 0) {
		free(src);
		free(tgt);
		destroy_item(it0);
		destroy_item(it1);
		return;
	}

	/* allocate space for new substitution string */
	subs = MALLOC(_LC_subs_t, collate.co_nsubs + 1);

	/* Initialize substitution flag */
	flag = 0;

	/*
	 * check source and destination strings for regular expression
	 * special characters. If special characters are found then enable
	 * regular expressions in substitution string.
	 */
	if (strpbrk(src, REGX_SPECIALS) != NULL)
		flag |= _SUBS_REGEXP;
	else if (strpbrk(tgt, REGX_SPECIALS) != NULL)
		flag |= _SUBS_REGEXP;

	/* Add source and target strings to newly allocated substitute list */
	for (i = 0, j = 0;
	    i < (int)((unsigned int)collate.co_nsubs); i++, j++) {
		int	c;

		c = strcmp(src, collate.co_subs[i].ss_src);
		if (c < 0 && i == j) {
			subs[j].ss_src = src;
			subs[j].ss_tgt = tgt;
			set_coll_wgt(&(subs[j].ss_act), flag, -1);
			j++;
		}
		subs[j].ss_src = collate.co_subs[i].ss_src;
		subs[j].ss_tgt = collate.co_subs[i].ss_tgt;
		subs[j].ss_act = collate.co_subs[i].ss_act;
	}
	if (i == j) {
		/* either subs was empty or new substring is greater than */
		/* any other to date */
		subs[j].ss_src = src;
		subs[j].ss_tgt = tgt;
		set_coll_wgt(&(subs[j].ss_act), flag, -1);
	}

	/* increment substitute string count */
	collate.co_nsubs++;

	/* free space occupied by old list */
	free(collate.co_subs);

	/* attach new list to coll table */
	collate.co_subs = subs;

	destroy_item(it0);
	destroy_item(it1);
}

/*
 *  FUNCTION: sem_collel_list
 *
 *  DESCRIPTION:
 *  Process the set of symbols now in the ibuf for the character
 *  for this particular order.
 *
 */
static void
sem_collel_list(item_t **ibuf, int int_no, _LC_weight_t *w, symbol_t *tgt,
    wchar_t wc, uint64_t fc, int order)
{
	item_t	*si;
	symbol_t	*sw;
	_LC_collel_t	*ce;
	_LC_weight_t	*this_w;
	item_t	*xi;
	int	n, nostr;
	char	*subs;
	char	*srcs;
	char	*srcs_temp;
	wchar_t	weight, next_weight;

	if (int_no == 1) {
		/* character gets collation value of symbol */
		sym_type_t	type;
		si = ibuf[0];
		if (si == NULL || si->type != SK_SYM)
			INTERNAL_ERROR;

		sw = si->value.sym;
		type = sw->sym_type;
		switch (type) {
		case ST_ELLIPSIS:
			if (tgt != NULL) {
#ifdef OLD_UNDEF_HANDLING
				if (old_handling & OLD_UNDEF_HANDLING) {
					diag_error(
					    gettext(ERR_BAD_COLL_ELLIPSIS));
					return;
				}
#endif
				/*
				 * Ellipsis is specified as weight.
				 * Ellipsis is only allowed for either
				 * ellipsis itself or UNDEFINED.
				 */
				if (strcmp(tgt->sym_id, "UNDEFINED") != 0) {
					coll_diag_error(
					    gettext(ERR_BAD_COLL_ELLIPSIS));
					return;
				}
				/* Target is UNDEFINED */
				undefined_weight = nxt_coll_wgt();
				set_coll_wgt(tgt->data.chr->wgt,
				    undefined_weight, -1);
				set_coll_wgt(w, undefined_weight, order);
				return;
			}
			weight = get_coll_wgt(&(collate.co_coltbl[wc]), order);
			this_w = &(collate.co_coltbl[wc]);
			break;

		case ST_CHR_SYM:
			weight = get_coll_wgt(sw->data.chr->wgt, order);
			this_w = sw->data.chr->wgt;
			break;

		case ST_COLL_ELL:
			ce = loc_collel(sw->data.collel->sym,
			    sw->data.collel->pc);
			weight = get_coll_wgt(&(ce->ce_wgt), order);
			this_w = &(ce->ce_wgt);
			break;

		case ST_COLL_SYM:
			weight = get_coll_wgt(sw->data.collsym, order);
			this_w = tgt ? tgt->data.collsym : NULL;
			break;
		}

		if (weight == UNDEFINED || weight == SUB_STRING) {
			if (tgt) {
				if (sw == tgt) {
					next_weight = nxt_coll_wgt();
					set_coll_wgt(this_w, next_weight, -1);
					set_coll_wgt(w, next_weight, order);
				} else {
					set_coll_wgt(w, PENDING, order);
				}
			} else {
				if (type == ST_ELLIPSIS) {
					next_weight = nxt_coll_wgt();
					set_coll_wgt(this_w, next_weight, -1);
					set_coll_wgt(w, next_weight, order);
				} else if (type == ST_CHR_SYM &&
				    sw->data.chr->wc_enc == wc) {
					next_weight = nxt_coll_wgt();
					set_coll_wgt(this_w, next_weight, -1);
					set_coll_wgt(w, next_weight, order);
				} else {
					set_coll_wgt(w, PENDING, order);
				}
			}
		} else {
			set_coll_wgt(w, weight, order);
		}
		return;
	}

	set_coll_wgt(w, (wchar_t)SUB_STRING, order);

	/*
	 * collation substitution, i.e. <eszet>   <s><s>;<eszet>
	 */
	subs = MALLOC(char, (int_no * MB_LEN_MAX) + 1);

	/*
	 * create a string from each of all collation elelemnts
	 */
	nostr = 0;
	for (n = 0; n < int_no; n++) {
		if (ibuf[n]->type != SK_SYM) {
			INTERNAL_ERROR;
		}
		sw = ibuf[n]->value.sym;
		if (sw->sym_type == ST_CHR_SYM) {
			(void) strncat(subs,
			    (char *)sw->data.chr->str_enc, MB_LEN_MAX);
		} else {
			nostr++;
		}
	}


	if (*subs == '\0' || nostr != 0) {
		/*
		 * There are some non-character symbols in weight.
		 * They cannot be handle by legacy substitute method.
		 */
		free(subs);
		return;
	}

	/* Set up substring masks. */
	collate.co_sort[order] |= _COLL_SUBS_MASK;

	/*
	 * Get source string from target symbol.
	 *
	 * tgt->data.chr->str_enc must be run through copy_string so that
	 * it is in the same format as collate substring (which is also
	 * run through copy_string
	 *
	 */
	if (tgt) {
		if (tgt->sym_type == ST_COLL_ELL) {
			srcs_temp = STRDUP(tgt->data.collel->str);
			if (!subs) {
				subs = STRDUP(tgt->data.collel->str);
			}
		} else {
			srcs_temp = STRDUP(
			    (const char *)tgt->data.chr->str_enc);
			if (!subs) {
				subs = STRDUP(
				    (const char *)tgt->data.chr->str_enc);
			}
		}
	} else {
		srcs_temp = MALLOC(char, MB_LEN_MAX + 1);
		(void) mbs_from_fc(srcs_temp, fc);
		if (!subs) {
			subs = STRDUP(srcs_temp);
		}
	}

	srcs = copy_string(srcs_temp);

	/*
	 * look for the src string in the set of collation substitution
	 * strings alread defined.  If it is present, then just enable
	 * it for this order.
	 */
	for (n = 0; n < (int)((unsigned int)collate.co_nsubs); n++) {
		_LC_weight_t	*wx;

		wx = &(collate.co_subs[n].ss_act);

		if (strcmp(srcs, collate.co_subs[n].ss_src) == 0) {
			set_coll_wgt(wx, get_coll_wgt(wx, order) & _SUBS_ACTIVE,
			    order);
			free(srcs);
			free(srcs_temp);
			free(subs);

			return;
		}

	}

	/*
	 * If this substitution has never been used before, then we
	 * need to create a new one.  Push source and substitution
	 * strings on semantic stack and then call semantic action to
	 * process substitution strings.  Reset active flag for all
	 * except current order.
	 */
	xi = create_item(SK_STR, srcs_temp);
	(void) sem_push(xi);

	xi = create_item(SK_STR, subs);
	(void) sem_push(xi);

	sem_def_substr();

	/*
	 * locate source string in substitution string array.  After
	 * you locate it, fix the substitution flags to indicate which
	 * order the thing is valid for.
	 */
	for (n = 0; n < (int)((unsigned int)collate.co_nsubs); n++) {
		if (strcmp(collate.co_subs[n].ss_src, srcs) == 0) {
			/*
			 * If weights array has not been already allocated,
			 * set_coll_wgt() will allocate it, and each element
			 * will be initialized to UNDEFINED.  As a result,
			 * substitution for all order will be turned off.
			 * Then, set action for current order to TRUE.
			 */
			set_coll_wgt(&(collate.co_subs[n].ss_act),
			    _SUBS_ACTIVE, order);

			break;
		}
	}
	free(srcs);
	free(srcs_temp);
	free(subs);
}

/*
 *  FUNCTION: sem_set_collwgt
 *
 *  DESCRIPTION:
 *  Assigns the collation weights in the argument 'weights' to the character
 *  popped off the symbol stack.
 */
void
sem_set_collwgt(int order)
{
	symbol_t	*sym;
	item_t	*itm;
	int	i, j, n;
	_LC_weight_t *wgt;
	_LC_collel_t *ce;
	struct ell_list	*elist;

	sym = sym_pop();
	if (sym->sym_type != ST_CHR_SYM &&
	    sym->sym_type != ST_COLL_SYM &&
	    sym->sym_type != ST_COLL_ELL &&
	    sym->sym_type != ST_ELLIPSIS)
		INTERNAL_ERROR;

	elist = MALLOC(struct ell_list, order + 1);
	for (i = order; i >= 0; i--) {
		itm = sem_pop();
		if (itm == NULL || itm->type != SK_INT)
			INTERNAL_ERROR;
		n = itm->value.int_no;
		destroy_item(itm);
		elist[i].no = n;
		elist[i].itm = MALLOC(item_t *, n);

		for (j = n - 1; j >= 0; j--) {
			itm = sem_pop();
			if (itm == NULL || itm->type != SK_SYM)
				INTERNAL_ERROR;
			elist[i].itm[j] = itm;
		}
	}

	if (sym->sym_type == ST_ELLIPSIS) {
		/* range */
		if (rangeinfo.start) {
			/* already ... found */
			coll_diag_error(gettext(ERR_CONT_COL_RANGE));
			free_elist(rangeinfo.elist, rangeinfo.order);
			rangeinfo.start = NULL;
			p_col_id = sym;
			return;
		}
		if (p_col_id && p_col_id->sym_type != ST_CHR_SYM) {
			coll_diag_error(gettext(ERR_INVAL_COLL_RANGE3),
			    p_col_id->sym_id);
			free_elist(elist, order);
			p_col_id = sym;
			return;
		}
		rangeinfo.start = p_col_id;
		rangeinfo.elist = elist;
		rangeinfo.order = order;
		return;
	}

	if (rangeinfo.start) {
		if (sym->sym_type != ST_CHR_SYM) {
			coll_diag_error(gettext(ERR_INVAL_COLL_RANGE),
			    rangeinfo.start->sym_id, sym->sym_id);
			free_elist(rangeinfo.elist, rangeinfo.order);
			rangeinfo.start = NULL;
			p_col_id = sym;
			return;
		}
		rangeinfo.end = sym;
		sem_set_collwgt_range();
		free_elist(rangeinfo.elist, rangeinfo.order);
		rangeinfo.start = NULL;
		rangeinfo.end = NULL;
		rangeinfo.elist = NULL;
	}

	p_col_id = sym;

	switch (sym->sym_type) {
	case ST_CHR_SYM:
		if (sym->data.chr->wgt == NULL)
			sym->data.chr->wgt =
			    &(collate.co_coltbl[sym->data.chr->wc_enc]);
		wgt = sym->data.chr->wgt;
		break;

	case ST_COLL_ELL:
		ce = loc_collel(sym->data.collel->sym, sym->data.collel->pc);
		wgt = &(ce->ce_wgt);
		break;

	case ST_COLL_SYM:
		wgt = sym->data.collsym;
		break;

	default:
		INTERNAL_ERROR;
	}

	if (
#ifdef OLD_DUP_HANDLING
	    (old_handling & OLD_DUP_HANDLING) == 0 &&
#endif
	    check_if_assigned(wgt, sym))
		return;

	switch (sym->sym_type) {
	case ST_CHR_SYM:
	case ST_COLL_ELL:
		/*
		 * Set relative order for this character.
		 * A relative order isn't needed for collating-symbols.
		 */
		set_r_order(wgt, nxt_r_ord());
		break;
	}

	for (i = 0; i <= order; i++) {
		/* save what was assigned and restore it later */
		sem_collel_list(elist[i].itm, elist[i].no,
		    t_weights, sym, 0, 0, i);
	}

	for (i = 0; i < NUM_ORDER; i++) {
		set_coll_wgt_rec(wgt, (*t_weights)[i], i, sym, 0, &elist[i]);
		(*t_weights)[i] = UNDEFINED;
	}

	free_elist(elist, order);
}

/*
 *  FUNCTION: sem_set_dflt_collwgt
 *
 *  DESCRIPTION:
 *  Assign collation weight to character - set weight in symbol table
 *  entry and in coltbl weight array.
 *
 *  The collation weight assigned is the next one available, i.e. the
 *  default collation weight.
 */
void
sem_set_dflt_collwgt(void)
{
	symbol_t	*sym;
	_LC_weight_t *wgt;
	_LC_collel_t *ce;

	sym = sym_pop();
	if (sym->sym_type != ST_CHR_SYM &&
	    sym->sym_type != ST_COLL_SYM &&
	    sym->sym_type != ST_COLL_ELL &&
	    sym->sym_type != ST_ELLIPSIS)
		INTERNAL_ERROR;

	if (sym->sym_type == ST_ELLIPSIS) {
		/* range */
		if (rangeinfo.start) {
			/* already ... found */
			coll_diag_error(gettext(ERR_CONT_COL_RANGE));
			free_elist(rangeinfo.elist, rangeinfo.order);
			rangeinfo.start = NULL;
			p_col_id = sym;
			return;
		}
		if (p_col_id && p_col_id->sym_type != ST_CHR_SYM) {
			coll_diag_error(gettext(ERR_INVAL_COLL_RANGE3),
			    p_col_id->sym_id);
			p_col_id = sym;
			return;
		}
		rangeinfo.start = p_col_id;
		rangeinfo.order = 0;
		return;
	}

	if (rangeinfo.start) {
		if (sym->sym_type != ST_CHR_SYM) {
			coll_diag_error(gettext(ERR_INVAL_COLL_RANGE),
			    rangeinfo.start->sym_id, sym->sym_id);
			free_elist(rangeinfo.elist, rangeinfo.order);
			rangeinfo.start = NULL;
			p_col_id = sym;
			return;
		}
		rangeinfo.end = sym;
		sem_set_dflt_collwgt_range();
		free_elist(rangeinfo.elist, rangeinfo.order);
		rangeinfo.start = NULL;
		rangeinfo.end = NULL;
	}

	p_col_id = sym;

	switch (sym->sym_type) {
	case ST_CHR_SYM:
		if (sym->data.chr->wgt == NULL)
			sym->data.chr->wgt =
			    &(collate.co_coltbl[sym->data.chr->wc_enc]);
		wgt = sym->data.chr->wgt;
		break;

	case ST_COLL_ELL:
		ce = loc_collel(sym->data.collel->sym, sym->data.collel->pc);
		wgt = &(ce->ce_wgt);
		break;

	case ST_COLL_SYM:
		wgt = sym->data.collsym;
		break;

	default:
		INTERNAL_ERROR;
	}

	if (check_if_assigned(wgt, sym))
		return;

	set_coll_wgt_rec(wgt, nxt_coll_wgt(), -1, sym, 0, NULL);

	switch (sym->sym_type) {
	case ST_CHR_SYM:
	case ST_COLL_ELL:
		set_r_order(wgt, nxt_r_ord());
		break;
	}
}

/*
 *  FUNCTION: sem_set_dflt_collwgt_range
 *
 *  DESCRIPTION:
 *  Assign collation weights to a range of characters.  The functions
 *  expects to find two character symbols on the semantic stack.
 *
 *  The collation weight assigned is the next one available, i.e. the
 *  default collation weight.
 */
static void
sem_set_dflt_collwgt_range(void)
{
	symbol_t *s1, *s0;
	char	*start_sym, *end_sym;
	uint64_t	start, end, i;
	wchar_t	weight;
	int	wc;

	/*
	 * Issue warning message that using KW_ELLIPSIS results in the use of
	 * codeset encoding assumptions by localedef.
	 *
	 * - required by POSIX.
	 *
	 * diag_error(ERR_CODESET_DEP);
	 */

	/* pop symbols of symbol stack */
	s1 = rangeinfo.end;
	s0 = rangeinfo.start;

	/* sanity check */
	if (s0 == NULL && s1 == NULL)
		INTERNAL_ERROR;

	/* get starting and ending points in file code */
	if (s0) {
		start = s0->data.chr->fc_enc;
		start_sym = s0->sym_id;
	} else {
		/* NULL is assumed */
		start = 0;
		start_sym = "<NULL>";
	}
	if (s1) {
		end = s1->data.chr->fc_enc;
		end_sym = s1->sym_id;
	} else {
		end = max_fc_enc;
		end_sym = "<\?\?\?>";
	}

	/* invalid symbols in range ? */
	if (start >= end)
		error(4, gettext(ERR_INVAL_COLL_RANGE), start_sym, end_sym);

	for (i = start + 1; i < end; i++) {

		if ((wc = wc_from_fc(i)) >= 0) {

			/*
			 * check if this character is in the charmap
			 * if not then issue a warning
			 */

			if (wchar_defined(wc) == 0) {
				coll_diag_error(gettext(ERR_MISSING_CHAR), i);
			}

			/* check if already defined elsewhere in map */
			if (check_if_assigned_range(&(collate.co_coltbl[wc]),
			    wc, i, start_sym, end_sym)) {
				return;
			}
			/* get next available collation weight */
			weight = nxt_coll_wgt();

			/*
			 * collation weights for symbols assigned weights in
			 * a range are not accessible from the symbol , i.e.
			 *
			 * s->data.chr->wgt[x] = weight;
			 *
			 * cannot be assigned here since we don't have the
			 * symbol which refers to the file code.
			 */

			/*
			 * put weight in coll table at spot for wchar
			 * encoding
			 */
			set_coll_wgt_rec(&(collate.co_coltbl[wc]),
			    weight, -1, NULL, wc, NULL);
			/* set relative order for this character */
			set_r_order(&(collate.co_coltbl[wc]), nxt_r_ord());
		}
	}
}


static void
sem_set_collwgt_range(void)
{
	symbol_t	*s1, *s0;
	uint64_t	start, end, i;
	int	ord;
	wchar_t	wc;
	char	*start_sym, *end_sym;

	s1 = rangeinfo.end;
	s0 = rangeinfo.start;

	/* sanity check */
	if (s0 == NULL && s1 == NULL)
		INTERNAL_ERROR;

	/* get starting and ending points in file code */
	if (s0) {
		start = s0->data.chr->fc_enc;
		start_sym = s0->sym_id;
	} else {
		start = 0;
		start_sym = "<NULL>";
	}
	if (s1) {
		end = s1->data.chr->fc_enc;
		end_sym = s1->sym_id;
	} else {
		end = max_fc_enc;
		end_sym = "<\?\?\?>";
	}

	/* invalid symbols in range ? */
	if (start >= end)
		error(4, gettext(ERR_INVAL_COLL_RANGE), start_sym, end_sym);

	if (rangeinfo.elist == NULL) {
		/*
		 * no weight list specified for the ellipsis entry
		 * So, make dummy ellipsis
		 */
		struct ell_list	*elist;

		elist = MALLOC(struct ell_list, NUM_ORDER);
		for (ord = 0; ord < NUM_ORDER; ord++) {
			item_t	*i;

			i = create_item(SK_SYM, ellipsis_sym);
			elist[ord].no = 1;
			elist[ord].itm = MALLOC(item_t *, 1);
			elist[ord].itm[0] = i;
		}
		rangeinfo.order = NUM_ORDER - 1; /* 0-based */
		rangeinfo.elist = elist;
	}

	for (i = start + 1; i < end; i++) {
		if ((wc = wc_from_fc(i)) < 0)
			continue;

		/*
		 * check if this character is in the charmap
		 * if not then issue a warning
		 */

		if (wchar_defined(wc) == 0) {
			coll_diag_error(gettext(ERR_MISSING_CHAR), i);
		}

		/* check if already defined elsewhere in map */
		if (check_if_assigned_range(&(collate.co_coltbl[wc]),
		    wc, i, start_sym, end_sym)) {
			return;
		}
		ellipsis_sym->data.ellipsis_w = UNDEFINED;
		for (ord = 0; ord <= rangeinfo.order; ord++) {
			sem_collel_list(rangeinfo.elist[ord].itm,
			    rangeinfo.elist[ord].no, &(collate.co_coltbl[wc]),
			    NULL, wc, i, ord);
			/* save what was assigned and restore it later */
			(*t_weights)[ord] = collate.co_coltbl[wc][ord];
		}
		for (ord = 0; ord < NUM_ORDER; ord++) {
			set_coll_wgt_rec(&(collate.co_coltbl[wc]),
			    (*t_weights)[ord], ord, NULL, wc,
			    &rangeinfo.elist[ord]);
			(*t_weights)[ord] = UNDEFINED;
		}
		/* set relative order for this character */
		set_r_order(&(collate.co_coltbl[wc]), nxt_r_ord());
	}
}


/*
 *  FUNCTION: sem_sort_spec
 *
 *  DESCRIPTION:
 *  This function decrements the global order by one to compensate for the
 *  extra increment done by the grammar, and then copies the sort modifier
 *  list to each of the substrings defined thus far.
 */
void
sem_sort_spec(void)
{
	static _LC_weight_t	tt_weights;
	symbol_t *s;
	item_t   *it;
	int	i;
	wchar_t    *buf;

	/*
	 * The number of collation orders is one-based (at this point)
	 * We change it to zero based, which is what the runtime wants
	 */
	collate.co_nord--;

	/*
	 * Get sort values from top of stack and assign to collate.co_sort
	 */
	collate.co_sort = MALLOC(wchar_t, NUM_ORDER);
	for (i = NUM_ORDER - 1; i >= 0; i--) {
		it = sem_pop();
		collate.co_sort[i] = it->value.int_no;
		destroy_item(it);
	}

	buf = alloc_new_weight(NUM_ORDER_R * (max_wchar_enc+1));
	for (i = 0; i <= max_wchar_enc; i++) {
		collate.co_coltbl[i] = buf;
		buf += NUM_ORDER_R;
	}

	tt_weights = NEW_WEIGHT();
	t_weights = &tt_weights;

	/*
	 * Turn off the _SUBS_ACTIVE flag for substitution strings in orders
	 * where this capability is disabled.
	 * This is now done in setup_substr called from the grammar.
	 */
	/*
	 * IGNORE gets a special collation value .  The xfrm and coll
	 * logic must recognize zero and skip a character possesing this
	 * collation value.
	 */
	s = create_symbol("IGNORE");
	s->sym_type = ST_CHR_SYM;
	s->data.chr = MALLOC(chr_sym_t, 1);
	s->data.chr->wc_enc = _WCHAR_MAX;
	s->data.chr->width = 0;
	s->data.chr->len = 0;
	s->data.chr->wgt = MALLOC(_LC_weight_t, 1);
	set_coll_wgt(s->data.chr->wgt, IGNORE, -1);
	set_r_order(s->data.chr->wgt, IGNORE);
	(void) add_symbol(s);

	/* minimum collating weight and minimum relative order are same */
	coll_wgt_min = coll_r_ord_min = nxt_coll_wgt();

	/*
	 * UNDEFINED has _WCHAR_MAX to differentiate explicit UNDEFINED
	 * weight from uninitialized weight.
	 */
	s = create_symbol("UNDEFINED");
	s->sym_type = ST_CHR_SYM;
	s->data.chr = MALLOC(chr_sym_t, 1);
	s->data.chr->wc_enc = _WCHAR_MAX;
	s->data.chr->width = 0;
	s->data.chr->len = 0;
	s->data.chr->wgt = MALLOC(_LC_weight_t, 1);
	set_coll_wgt(s->data.chr->wgt, (wchar_t)_WCHAR_MAX, -1);
	set_r_order(s->data.chr->wgt, (wchar_t)_WCHAR_MAX);
	undefined_weight = (wchar_t)_WCHAR_MAX;
	(void) add_symbol(s);
}

/*
 *  FUNCTION: sem_def_collel
 *
 *  DESCRIPTION:
 *  Defines a collation ellement. Creates a symbol for the collation element
 *  in the symbol table, creates a collation element data structure for
 *  the element and populates the element from the string on the semantic
 *  stack.
 */
void
sem_def_collel(void)
{
	symbol_t	*sym_name;	/* symbol to be defined */
	item_t	*it;		/* string which is the collation symbol */
	wchar_t	pc;		/* process code for collation symbol    */
	wchar_t *wstr, *ws;
	_LC_collel_t *coll_sym;	/* collation symbol pointer */
	char	*sym;		/* translated collation symbol */
	int	n_syms;	/* no. of coll syms beginning with char */
	int	rc;
	int	i, j, skip;
	char	*rstr, *temp_sym;
	struct collel_list *cel;

	sym_name = sym_pop();	/* get coll symbol name off symbol stack */
	it = sem_pop();		/* get coll symbol string off of stack */

	if (it->type != SK_STR)
		INTERNAL_ERROR;

	/* Create symbol in symbol table for coll symbol name */
	if (sym_name->sym_type == ST_CHR_SYM) {
		free(sym_name->data.chr);
	}
	sym_name->sym_type = ST_COLL_ELL;
	sym_name->data.collel = MALLOC(coll_ell_t, 1);
	(void) add_symbol(sym_name);

	rstr = copy(it->value.str); /* Expand without making printable */

	/* Translate collation symbol to file code */
	sym = copy_string(it->value.str);

	/*
	 * Determine process code for collation symbol.
	 */
	rc = INT_METHOD(
	    (int (*)(_LC_charmap_t *, wchar_t *, const char *, size_t))
	    METH_OFFS(CHARMAP_MBTOWC_AT_NATIVE))(&charmap, &pc,
	    (const char *)rstr, MB_LEN_MAX);
	if (rc < 0) {
		coll_diag_error(gettext(ERR_ILL_CHAR), it->value.str);
		return;
	}
	skip = 0;
	for (i = 0; i < rc; i++) {
		if ((unsigned char)rstr[i] < 128)
			skip++;
		else
			skip += 6;	/* Leave space for \\xnn\0 */
	}

	i = strlen(rstr);
	wstr = ws = MALLOC(wchar_t, i + 1);

	*ws++ = pc;
	i = rc;
	while (rstr[i] != '\0') {
		rc = INT_METHOD(
		    (int (*)(_LC_charmap_t *, wchar_t *, const char *, size_t))
		    METH_OFFS(CHARMAP_MBTOWC_AT_NATIVE))(&charmap, ws,
		    (const char *)&rstr[i], MB_LEN_MAX);
		if (rc < 0) {
			coll_diag_error(gettext(ERR_ILL_CHAR), it->value.str);
			free(ws);
			return;
		}
		ws++;
		i += rc;
	}
	*ws = L'\0';

	/* save process code and matching source str in symbol */
	/* do not put the first character in the src str */
	sym_name->data.collel->pc = pc;
	sym_name->data.collel->str = STRDUP(sym);
	sym_name->data.collel->rstr = rstr;
	sym_name->data.collel->sym = sym_name->data.collel->str + skip;
	sym_name->data.collel->wstr = wstr;
	temp_sym = STRDUP(sym + skip);
	free(sym);
	sym = temp_sym;

	if (collate.co_cetbl[pc] != NULL) {
		/*
		 * At least one collation symbol exists already --
		 * Count number of collation symbols with the process code
		 */
		for (i = 0; collate.co_cetbl[pc][i].ce_sym != NULL; i++)
			;

		/*
		 * Allocate memory for
		 * current number + new symbol + terminating null symbol
		 */
		coll_sym = MALLOC(_LC_collel_t, i + 2);
		n_syms = i;
	} else {
		/*
		 * This is the first collation symbol, allocate for
		 *
		 * new symbol + terminating null symbol
		 */
		coll_sym = MALLOC(_LC_collel_t, 2);
		n_syms = 0;
	}

	/* create a linked list of collel symbols */
	for (cel = all_collel; cel != NULL; cel = cel->collel_next) {
		if (cel->wc == pc)
			break;
	}
	if (cel == NULL) {
		cel = MALLOC(struct collel_list, 1);
		cel->collel_next = all_collel;
		all_collel = cel;
		cel->wc = pc;
	}
	cel->nsymbol = n_syms + 1;
	cel->symbol = REALLOC(symbol_t *, cel->symbol, cel->nsymbol);

	/* Add terminating NULL symbol */
	coll_sym[n_syms + 1].ce_sym = NULL;

	/* Add collation symbols to list in sorted order */
	if (n_syms != 0) {
		for (i = n_syms - 1; i >= 0; i--) {
			j = strcmp(sym, collate.co_cetbl[pc][i].ce_sym);
			if (j > 0)
				break;
			coll_sym[i + 1].ce_sym = collate.co_cetbl[pc][i].ce_sym;
			cel->symbol[i + 1] = cel->symbol[i];
		}
		i++;
		for (j = 0; j < i; j++) {
			coll_sym[j].ce_sym = collate.co_cetbl[pc][j].ce_sym;
			cel->symbol[j] = cel->symbol[j];
		}
	} else {
		i = 0;
	}
	coll_sym[i].ce_sym = sym;
	cel->symbol[i] = sym_name;

	/* free space occupied by old list */
	if (n_syms > 0)
		free(collate.co_cetbl[pc]);

	/* attach new list to coll table */
	collate.co_cetbl[pc] = coll_sym;

	destroy_item(it);
}

/*
 *  FUNCTION: sem_spec_collsym
 *
 *  DESCRIPTION:
 *  Defines a placeholder collation symbol name.  These symbols are typically
 *  used to assign collation values to a set of characters.
 */
void
sem_spec_collsym(void)
{
	symbol_t *sym, *t;

	sym = sym_pop();	/* get coll symbol name off symbol stack */

	t = loc_symbol(sym->sym_id);
	if (t != NULL) {
		coll_diag_error(gettext(ERR_DUP_COLL_SYM), sym->sym_id);
	} else {
		/* Create symbol in symbol table for coll symbol name */
		sym_free_chr(sym);
		sym->sym_type = ST_COLL_SYM;
		sym->data.collsym = MALLOC(_LC_weight_t, 1);
		/*
		 * At this point, the max number of order has not been
		 * determined yet.  Allocating the possible max number.
		 */
		*(sym->data.collsym) = NEW_WEIGHT_MAX();

		(void) add_symbol(sym);
	}
}


/*
 *  FUNCTION: sem_collate
 *
 *  DESCRIPTION:
 *  Post processing for collation table which consists of the location
 *  and assignment of specific value for UNDEFINED and IGNORE etc.
 */
void
sem_collate(void)
{
	_LC_weight_t *undefined;
	_LC_collel_t *ce;
	symbol_t	*s;
	int	need_r_wgt = 0;
	int	i, j, k;
	int	warn = FALSE;		/* Local flag to hide extra errors */

	if (!lc_has_collating_elements) {
		/* This locale does not have any collating elements */
		free(collate.co_cetbl);
		collate.co_cetbl = (_LC_collel_t **)NULL;
	}

	s = loc_symbol("UNDEFINED");
	if (s == NULL)
		INTERNAL_ERROR;
	undefined = s->data.chr->wgt;
	if (get_coll_wgt(undefined, 0) == _WCHAR_MAX)
		warn = TRUE;

	handle_undefined(undefined, !warn);

#ifdef	_LDEB
	dump_link();
#endif
	adjust_weight();
#ifdef	_LDEB
	dump_link();
#endif
	/*
	 * Turn IGNORE in to runtime value (0). Also check to see if we can
	 * use the last order as "collating order".
	 */
	for (i = 0; i <= max_wchar_enc; i++) {
		if (!wchar_defined(i))
			continue;

		for (j = 0; j < NUM_ORDER; j++) {
			if (collate.co_coltbl[i][j] == IGNORE)
				collate.co_coltbl[i][j] = 0;
		}
		if (lc_has_collating_elements && collate.co_cetbl[i] != NULL) {
			for (j = 0, ce = &(collate.co_cetbl[i][j]);
			    ce->ce_sym != NULL;
			    ce = &(collate.co_cetbl[i][++j])) {
				for (k = 0; k < NUM_ORDER; k++) {
					if (ce->ce_wgt[k] == IGNORE)
						ce->ce_wgt[k] = 0;
				}
			}
		}
		if (need_r_wgt == 0) {
			/*
			 * Check if the weight of the last order is unique
			 */
			if (collate.co_coltbl[i][NUM_ORDER - 1] <=
			    coll_wgt_min ||
			    collate.co_coltbl[i][NUM_ORDER - 1] >=
			    coll_wgt_max) {
				/*
				 * Weight of the last order is not
				 * unique.  Check if weights of
				 * other orders are unique or not.
				 * If all of them are not unique,
				 * ignore this entry.
				 */
				int	k;
#ifdef	_COLL_DEBUG
	(void) fprintf(stderr, "=================================\n");
	(void) fprintf(stderr, "non-unique weight entry found:\n");
	(void) fprintf(stderr, "%s\n", char_info(i));
	(void) fprintf(stderr, "weight value: 0x%08x\n",
	    (int)collate.co_coltbl[i][NUM_ORDER - 1]);
#endif

				for (k = 0; k < NUM_ORDER - 1; k++) {
					if (collate.co_coltbl[i][k] >
					    coll_wgt_min &&
					    collate.co_coltbl[i][k] <
					    coll_wgt_max) {
						/* unique found */
						break;
					}
				}
				if (k < NUM_ORDER - 1) {
					/*
					 * this collation needs
					 * a dedicated relative
					 * weight table
					 */

#ifdef	_COLL_DEBUG
	(void) fprintf(stderr, "this entry is not unique!!\n");
#endif
					need_r_wgt = 1;
				}
			}
		}

		if (collate.co_coltbl[i][R_ORD_IDX] == UNDEFINED) {
#ifdef	_COLL_DEBUG
	(void) fprintf(stderr, "undefined relative order found:\n");
	(void) fprintf(stderr, "%s\n", char_info(i));
#endif
			collate.co_coltbl[i][R_ORD_IDX] =
			    (*undefined)[R_ORD_IDX];
		}

	}
	if (need_r_wgt) {
		/* this locale requires a dedicated relative order table */
		collate.co_col_min = coll_r_ord_min;
		collate.co_col_max = coll_r_ord_max;
		collate.co_r_order = 1;
	} else {
		/* the weight of the last order is unique */
		collate.co_col_min = coll_wgt_min;
		collate.co_col_max = coll_wgt_max;
		collate.co_r_order = 0;
	}

	if ((coll_wgt_max >> 8) == 0x010101)
		collate.co_sort[0] |= _COLL_WGT_WIDTH1;
	else if ((coll_wgt_max >> 16) == 0x0101)
		collate.co_sort[0] |= _COLL_WGT_WIDTH2;
	else if ((coll_wgt_max >> 24) == 0x01)
		collate.co_sort[0] |= _COLL_WGT_WIDTH3;
	else
		collate.co_sort[0] |= _COLL_WGT_WIDTH4;

	build_extinfo();

#ifdef HASHSTAT
	for (i = 0; i < CRHASHSZ; i++) {
		collrec_t *cr;

		j = 0;
		for (cr = collrec_hash_sym[i];
		    cr != NULL; cr = cr->l_hash_sym) {
			j++;
		}
		printf("[%4d]: %d\n", i, j);
	}
#endif
	if (nsuppressed != 0) {
		(void) fprintf(stderr, gettext(ERR_WARN_SUPPRESSED),
		    nsuppressed);
	}
}

/*
 *  FUNCTION: setup_substr
 *
 *  DESCRIPTION:
 *  Set-up the collation weights for the substitute strings defined in
 *  the collation section using the keyword "substitute". This is executed
 *  after the order keyword (at which time we now know how many orders there
 *  are and if any have subs turned off).
 *
 */
void
setup_substr(void)
{
	int n, i;
	int flag_subs;
	int flag_nosubs;

	flag_nosubs = 0;
	flag_subs = _SUBS_ACTIVE;

	for (n = 0; n < (int)((unsigned int)collate.co_nsubs); n++) {
		collate.co_subs[n].ss_act = NEW_WEIGHT();

		for (i = 0; i < NUM_ORDER; i++) {
			if (collate.co_sort[i] & _COLL_NOSUBS_MASK)
				collate.co_subs[n].ss_act[i] = flag_nosubs;
			else
				collate.co_subs[n].ss_act[i] = flag_subs;
		}
	}
}

/*
 *  FUNCTION: crhash
 *
 *  DESCRIPTION:
 *  Create hashkey by given wide char code or address of symbols.
 */
static int
crhash(ulong_t addr)
{
	ulong_t hcode = addr;

	hcode += ~(hcode << 9);
	hcode ^=  (hcode >> 14);
	hcode +=  (hcode << 4);
	hcode ^=  (hcode >> 10);

	return ((int)(hcode % CRHASHSZ));
}

/*
 *  FUNCTION: set_coll_wgt_rec
 *
 *  DESCRIPTION:
 *  Wrapper function of set_coll_wgt(). This function will take extra
 *  arguments which helps record weight, order, wchar code and target
 *  symbols etc.
 */
static void
set_coll_wgt_rec(_LC_weight_t *weights, wchar_t weight, int ord,
	symbol_t *sym, wchar_t wc, struct ell_list *elist)
{
	collrec_t *cr;
	int key, i;
	static int seqno;

	cr = MALLOC(collrec_t, 1);
	if (sym == NULL) {
		cr->tgt_wc = wc;
		cr->wcvalid = 1;
	} else {
		cr->tgt_symp = sym;
		if (sym->sym_type == ST_CHR_SYM) {
			cr->tgt_wc = sym->data.chr->wc_enc;
			cr->wcvalid = 1;
		}
	}
	cr->seqno = seqno++;
	cr->order = ord;
	cr->weightp = weights;
	cr->weight = weight;

	if (elist != NULL) {
		cr->refcnt = elist->no;
		cr->refp = MALLOC(symbol_t *, elist->no);
		for (i = 0; i < elist->no; i++)
			cr->refp[i] = elist->itm[i]->value.sym;
	}

	if (collrec_tail != NULL)
		collrec_tail->l_next = cr;
	else
		collrec_head = cr;
	collrec_tail = cr;

	if (cr->wcvalid)
		key = crhash((ulong_t)cr->tgt_wc);
	else
		key = crhash((ulong_t)cr->tgt_symp);
	cr->l_hash_sym = collrec_hash_sym[key];
	collrec_hash_sym[key] = cr;

	set_coll_wgt(weights, weight, ord);
}

/*
 *  FUNCTION: find_collrec
 *
 *  DESCRIPTION:
 *  Look up collrec_t which stores weights for given symbol and order.
 */
static collrec_t *
find_collrec(symbol_t *sym, int order)
{
	collrec_t *cr;
	int key;

	if (sym->sym_type == ST_CHR_SYM)
		return (find_collrec_by_wc(sym->data.chr->wc_enc, order));

	key = crhash((ulong_t)sym);
	for (cr = collrec_hash_sym[key]; cr != NULL; cr = cr->l_hash_sym) {
		if (cr->tgt_symp != sym)
			continue;
		if (order == -1 || cr->order == -1 || cr->order == order) {
			return (cr);
		}
	}
	return (NULL);
}

/*
 *  FUNCTION: find_collrec_by_wc
 *
 *  DESCRIPTION:
 *  Look up collrec_t which stores weights for given wchar code and order.
 */
static collrec_t *
find_collrec_by_wc(wchar_t wc, int order)
{
	collrec_t *cr;
	int key;

	key = crhash((ulong_t)wc);
	for (cr = collrec_hash_sym[key]; cr != NULL; cr = cr->l_hash_sym) {
		if (!cr->wcvalid || cr->tgt_wc != wc)
			continue;
		if (order == -1 || cr->order == -1 || cr->order == order)
			return (cr);
	}
	return (NULL);
}

static int
sortwgt(const void *a1, const void *a2)
{
	collrec_t *p1 = *(collrec_t **)a1;
	collrec_t *p2 = *(collrec_t **)a2;

	if ((ulong_t)p1->weight > (ulong_t)p2->weight)
		return (1);
	else if ((ulong_t)p1->weight < (ulong_t)p2->weight)
		return (-1);
	return (p1->seqno - p2->seqno);
}

/*
 *  FUNCTION: adjust_forward_ref
 *
 *  DESCRIPTION:
 *  Resolve forward references by looking up symbols recursively.
 */
static int
adjust_forward_ref(collrec_t *cr)
{
	collrec_t *ncr;
	int ret = 0, ord;

	if (cr->mark)
		error(4, ERR_CYCLIC_SYM_REF, cr->tgt_symp->sym_id);
	cr->mark = 1;

#ifdef OLD_FORREF_HANDLING
	if (old_handling & OLD_FORREF_HANDLING) {
		ord = 0;
	} else
#endif
	{
		ord = cr->order;
	}

	if ((ncr = find_collrec(cr->refp[0], ord)) == NULL)
		error(4, ERR_FORWARD_REF, cr->refp[0]->sym_id);

	if (ncr->weight == PENDING) {
		if ((ret = adjust_forward_ref(ncr)) != 0)
			goto out;
	}
	/* referrence is substitute string. can't resolve it */
	if (ncr->weight == SUB_STRING) {
		ret = -1;
		goto out;
	}

	set_coll_wgt(cr->weightp, ncr->weight, cr->order);
	cr->weight =  ncr->weight;
out:
	cr->mark = 0;
	return (ret);
}

#ifdef ADJ_DEBUG
static void
adj_debug(collrec_t *cr, wchar_t wt)
{
	int cnt;
	char buf[1024], *p;

	(void) printf("%-2d %-8d", cr->order, wt);
	if (cr->weight == SUB_STRING) {
		p = buf;
		(void) printf("  ");
		for (cnt = 0; cnt < cr->refcnt; cnt++) {
			p += sprintf(p, "%s", cr->refp[cnt]->sym_id);
		}
		(void) printf("%s", buf);
	}
	if (cr->tgt_symp == NULL)
		(void) printf("  %s\n", char_info(cr->tgt_wc));
	else
		(void) printf("  %s\n", cr->tgt_symp->sym_id);
}
#endif

/*
 *  FUNCTION: adjust_weight
 *
 *  DESCRIPTION:
 * There are two primary purposes of this function.
 * 1) Resolve forward references.
 * 2) Reassing weights for undefined characters.
 *
 * Undefined characters are put in the collation order at the point where
 * UNDEFINED is specifed. If ellipsis was given in the weight identifier,
 * we need to assign weights as if they appear in ascending order.
 */
static void
adjust_weight(void)
{
	_LC_weight_t *wp;
	collrec_t *cr, *ncr, **crp;
	wchar_t	wt, owt;
	int i, cnt, idx, nent, next;

	if (collrec_head == NULL)
		return;
	/*
	 * First pass: fix forward ref which we should fix before resolving
	 * weights for substitute string.
	 */
	for (cr = collrec_head; cr != NULL; cr = cr->l_next) {
		if (cr->weight != PENDING || cr->weightp == NULL)
			continue;
		(void) adjust_forward_ref(cr);
	}

	/*
	 * Second pass: sort entries by weight. We ignore fixed special
	 * weights. They will never be updated.
	 * We do this because we need to go through entries by weight's
	 * ascending order
	 */
	cnt = 0;
	for (cr = collrec_head; cr != NULL; cr = cr->l_next) {
		if (cr->weightp == NULL ||
		    cr->weight == UNDEFINED ||
		    cr->weight == PENDING ||
		    cr->weight == SUB_STRING ||
		    cr->weight == IGNORE ||
		    cr->weight == (wchar_t)_WCHAR_MAX) {
			continue;
		}
		cr->mark = 1;
		cnt++;
	}
	nent = cnt;
	crp = MALLOC(collrec_t *, nent);
	idx = 0;
	for (cr = collrec_head; cr != NULL; cr = cr->l_next) {
		if (!cr->mark)
			continue;
		crp[idx++] = cr;
		cr->mark = 0;
	}
	qsort(crp, nent, sizeof (collrec_t *), sortwgt);

	/*
	 * Third pass: adjust weight. We go through the entries by weight's
	 * ascending order and reassign weight from small to large, so that
	 * relative order of weights between each symbols won't change.
	 */
	nxt_coll_val = MIN_WEIGHT;
	(void) nxt_coll_wgt();	/* skip coll_wgt_min */
	wt = nxt_coll_wgt();
	for (idx = 0; idx < nent; idx = next) {
		next = idx + 1;
		cr = crp[idx];
		/*
		 * Assign weights for UNDEFINED chars.
		 */
		if (cr->weight == undefined_weight) {
#ifdef OLD_UNDEF_HANDLING
			if (old_handling & OLD_UNDEF_HANDLING) {
				for (i = idx; i < nent; i++) {
					ncr = crp[i];
					if (ncr->weight != undefined_weight)
						break;
#ifdef ADJ_DEBUG
					adj_debug(ncr, wt);
#endif
					ncr->weight = wt;
				}
			} else
#endif
			{
				wp = NULL;
				for (i = idx; i < nent; i++) {
					ncr = crp[i];
					if (ncr->weight != undefined_weight)
						break;
					if (i != idx && wp != ncr->weightp)
						wt = nxt_coll_wgt();
#ifdef ADJ_DEBUG
					adj_debug(ncr, wt);
#endif
					ncr->weight = wt;
					wp = ncr->weightp;
				}
			}
			next = i;
			wt = nxt_coll_wgt();
			continue;
		}
		/*
		 * Update entries which have same weight. Also count how
		 * many substitute strings starts with the same weight.
		 */
		owt = cr->weight;
		for (i = idx; i < nent; i++) {
			ncr = crp[i];
			if (owt != ncr->weight)
				break;
#ifdef ADJ_DEBUG
			adj_debug(ncr, wt);
#endif
			ncr->weight = wt;
		}
		next = i;
		wt = nxt_coll_wgt();
	}

	free(crp);

	/*
	 * 4th pass: update weight
	 */
	for (cr = collrec_head; cr != NULL; cr = cr->l_next) {
		if (cr->weight == IGNORE || cr->weight == _WCHAR_MAX)
			cr->weight = 0;
		set_coll_wgt(cr->weightp, cr->weight, cr->order);
	}

	/*
	 * Create weight string for SUB_STRING weights.
	 */
	for (cr = collrec_head; cr != NULL; cr = cr->l_next) {
		if (cr->weight != SUB_STRING)
			continue;
		i = 0;
		/* count number of collating weights */
		for (cnt = 0; cnt < cr->refcnt; cnt++) {
			ncr = find_collrec(cr->refp[cnt], cr->order);
			if (ncr->wslen == 0)
				i++;
			else
				i += ncr->wslen;
		}
		cr->wstr = MALLOC(wchar_t, i);
		cr->wslen = i;
		i = 0;
		for (cnt = 0; cnt < cr->refcnt; cnt++) {
			ncr = find_collrec(cr->refp[cnt], cr->order);
			if (ncr->wslen == 0) {
				cr->wstr[i++] = ncr->weight;
			} else {
				(void) wmemcpy(&cr->wstr[i], ncr->wstr,
				    ncr->wslen);
				i += ncr->wslen;
			}
		}
	}
}

/*
 *  FUNCTION: handle_undefined
 *
 *  DESCRIPTION:
 * look through the weights table per characters/collating element,
 * and assign "undefined" weight for those undefined weights. Those
 * weights which are marked "undefined" will be adjusted later by
 * adjust_weight().
 */
static void
handle_undefined(_LC_weight_t *undefined, int undef_defined)
{
	wchar_t i, j;
	int warn;
	collrec_t *cr;
	symbol_t *sym;
#ifdef OLD_UNDEF_HANDLING
	wchar_t rhigh;
#endif

	for (j = 0; j < NUM_ORDER; j++)
		(*t_weights)[j] = (*undefined)[j];

#ifdef OLD_UNDEF_HANDLING
	if (old_handling & OLD_UNDEF_HANDLING) {
		undefined_weight = nxt_coll_wgt();
		for (i = 0; i < NUM_ORDER; i++) {
			if ((*t_weights)[i] == _WCHAR_MAX)
				(*t_weights)[i] = undefined_weight;
		}
		rhigh = nxt_r_ord();
	} else
#endif
	if (!undef_defined) {
		/*
		 * Undefined chars belong to the same equivalent class
		 * per spec.
		 */
		(*t_weights)[0] = nxt_coll_wgt();
		/*
		 * But spec doesn't mention about subsequent classes.
		 */
		for (j = 1; j < NUM_ORDER; j++)
			(*t_weights)[j] = IGNORE;
		/*
		 * But, it's still preferred that the last order has unique
		 * weights. By assigning the undefined_weight, weights will
		 * get reassigned properly by adjust_weight().
		 * Here, undefined_weight must be the largest weight, because
		 * undefined chars should follow after the defined chars in
		 * the collating order per spec. Therefore, there must be no
		 * larger weights assigned past this point, until calling into
		 * adjust_weight().
		 */
		if (NUM_ORDER > 1) {
			undefined_weight = nxt_coll_wgt();
			(*t_weights)[NUM_ORDER - 1] = undefined_weight;
		}
	}

	for (i = 0; i <= max_wchar_enc; i++) {
		if (!wchar_defined(i))
			continue;
		warn = !undef_defined;
		/*
		 * spec says that we shall warn. We'll limit it to
		 * what was not specified explicitly.
		 */
		sym = NULL;
		if ((cr = find_collrec_by_wc(i, -1)) == NULL) {
			if (warn) {
				coll_diag_error(gettext(ERR_NO_UNDEFINED));
				coll_diag_error2("%s\n", char_info(i));
				warn = 0;
			}
			/* this symbol has never shown up. */
#ifdef OLD_UNDEF_HANDLING
			if (old_handling & OLD_UNDEF_HANDLING) {
				set_r_order(&(collate.co_coltbl[i]), rhigh);
			} else
#endif
			{
				set_r_order(&(collate.co_coltbl[i]),
				    nxt_r_ord());
			}
		} else {
			sym = cr->tgt_symp;
		}

		for (j = 0; j < NUM_ORDER; j++) {
			/* implicit/explicit doesn't matter */
			if (collate.co_coltbl[i][j] != UNDEFINED &&
			    collate.co_coltbl[i][j] != _WCHAR_MAX) {
				/* weight has been set ? */
				continue;
			}
			if (collate.co_coltbl[i][j] == UNDEFINED && warn) {
				/*
				 * symbol showed up, but missing weight
				 * identifiers.
				 */
				coll_diag_error(gettext(ERR_NO_UNDEFINED));
				coll_diag_error2("%s (order %d)\n",
				    char_info(i), j);
			}
			set_coll_wgt_rec(&(collate.co_coltbl[i]),
			    (*t_weights)[j], j, sym, i, NULL);
		}

		if (lc_has_collating_elements &&
		    collate.co_cetbl[i] != NULL) {
			handle_undefined_collel(t_weights,
			    i, undef_defined);
		}
	}
}

/*
 *  FUNCTION: handle_undefined_collel
 *
 *  DESCRIPTION:
 *  Resolve undefined weights for the collating elements.
 */
static void
handle_undefined_collel(_LC_weight_t *weights, wchar_t wc, int undef_defined)
{
	_LC_collel_t *ce;
	int i, j, warn;
	struct collel_list *cel;

	for (cel = all_collel; cel != NULL; cel = cel->collel_next)
		if (cel->wc == wc)
			break;

	for (i = 0, ce = &(collate.co_cetbl[wc][i]);
	    ce->ce_sym != NULL; ce = &(collate.co_cetbl[wc][++i])) {
		warn = !undef_defined;

		if (ce->ce_wgt == NULL) {
			/*
			 * This collating element has been defined, but
			 * has not been specified in the collating order.
			 */
			ce->ce_wgt = NEW_WEIGHT();
			coll_diag_error(gettext(ERR_COLLEL_NO_WEIGHTS),
			    cel->symbol[i]->sym_id);
			warn = 0;
		}
		for (j = 0; j < NUM_ORDER; j++) {
			if (ce->ce_wgt[j] != UNDEFINED &&
			    ce->ce_wgt[j] != _WCHAR_MAX) {
				continue;
			}
			if (ce->ce_wgt[j] == UNDEFINED && warn) {
				coll_diag_error(gettext(ERR_COLLEL_NO_WEIGHT),
				    cel->symbol[i]->sym_id, j);
			}
			set_coll_wgt_rec(&(ce->ce_wgt),
			    (*weights)[j], j, NULL, wc, NULL);
		}
	}
}

static int
subcomp(const void *a1, const void *a2)
{
	int	r;
	_LC_exsubs_t *s1 = (_LC_exsubs_t *)a1;
	_LC_exsubs_t *s2 = (_LC_exsubs_t *)a2;

	r = s1->ess_order - s2->ess_order;
	if (r != 0)
		return (r);
	return (strcmp(s1->ess_src.sp, s2->ess_src.sp));
}

static int
wsubcomp(const void *a1, const void *a2)
{
	int	r;
	_LC_exsubs_t *s1 = (_LC_exsubs_t *)a1;
	_LC_exsubs_t *s2 = (_LC_exsubs_t *)a2;

	r = s1->ess_order - s2->ess_order;
	if (r != 0)
		return (r);
	return (wcscmp(s1->ess_src.wp, s2->ess_src.wp));
}

/*
 *  FUNCTION: set_subs
 *
 *  DESCRIPTION:
 *  Load each one of _LC_exsubs_t entries.
 */
static int
set_subs(_LC_exsubs_t *sp, _LC_exsubs_t *wsp, symbol_t *sym, char *map)
{
	int	i, nent;
	char	*str;
	wchar_t	*wstr, *nw;
	collrec_t *cr;
	_LC_collel_t *ce;

	nent = 0;
	str = sym->data.collel->rstr;
	wstr = sym->data.collel->wstr;

	for (i = 0; i < NUM_ORDER_R; i++) {
		cr = NULL;
		/*
		 * Remember we don't have collrec entries for R_ORD_IDX.
		 */
		if (i != R_ORD_IDX && (cr = find_collrec(sym, i)) == NULL)
			continue;

		collate.co_sort[i] |= _COLL_SUBS_MASK;

		if (cr == NULL || cr->wslen == 0) {
			/*
			 * This could be either single weight or R_ORD_IDX
			 * weight.
			 */
			nw = MALLOC(wchar_t, 2);
			nw[0] = 1;
			if (cr != NULL) {
				nw[1] = cr->weight;
			} else {
				/* get weight for R_ORD */
				ce = loc_collel(sym->data.collel->sym,
				    sym->data.collel->pc);
				nw[1] = ce->ce_wgt[i];
			}
		} else {
			nw = MALLOC(wchar_t, cr->wslen + 1);
			nw[0] = cr->wslen;
			(void) wmemcpy(&nw[1], cr->wstr, cr->wslen);
		}

		/*
		 * 0x01 for substitue mapping.
		 */
		map[(unsigned char)*str] |= 0x1;
		sp->ess_order = i;
		sp->ess_srclen = strlen(str);
		sp->ess_src.sp = str;
		sp->ess_wgt.wgtstr = nw;
		sp++;

		map[*wstr] |= 0x1;
		wsp->ess_order = i;
		wsp->ess_srclen = wcslen(wstr);
		wsp->ess_src.wp = wstr;
		wsp->ess_wgt.wgtstr = nw;
		wsp++;

		nent++;
	}
	return (nent);
}

/*
 *  FUNCTION: collel_subs
 *
 *  DESCRIPTION:
 * Generate substitute table which involves many to many/one mappings.
 * Many to one mappings (ie collating-elements) are used to be handled
 * via _LC_collel_t, however they are combined into same tables since
 * a) cetbl is a waste of memory(huge non-readonly memory), and b) most
 * locale just have a few entries, and  c) we should have a wide char
 * version of table.
 */
static _LC_exsubs_t *
collel_subs(char *map, int *ordidx, int *ordsz, int *nentp)
{
	int	i, j, s, nent;
	symbol_t *sym;
	struct collel_list *cel;
	_LC_exsubs_t *sph, *wsph, *rsp;

	/* count how many collating elements we have */
	nent = 0;
	for (cel = all_collel; cel != NULL; cel = cel->collel_next)
		nent += cel->nsymbol;
	if (nent == 0)
		return (NULL);

	nent *= NUM_ORDER_R;

	/*
	 * loads table with pair of string and weigts.
	 */
	sph = MALLOC(_LC_exsubs_t, nent + 1);
	wsph = MALLOC(_LC_exsubs_t, nent + 1);
	nent = 0;
	for (cel = all_collel; cel != NULL; cel = cel->collel_next) {
		for (i = 0; i < cel->nsymbol; i++) {
			sym = cel->symbol[i];
			nent += set_subs(sph + nent, wsph + nent, sym, map);
		}
	}
	if (nent == 0) {
		free(sph);
		free(wsph);
		return (NULL);
	}
	/*
	 * Sort it primary by "order", and then string ascending order.
	 */
	qsort(sph, nent, sizeof (_LC_exsubs_t), subcomp);
	qsort(wsph, nent, sizeof (_LC_exsubs_t), wsubcomp);

	/*
	 * Find index and size of entries for each orders.
	 */
	for (i = j = 0; j < NUM_ORDER_R; j++) {
		s = i;
		while (i < nent) {
			if (sph[i].ess_order != j)
				break;
			i++;
		}
		ordidx[j] = s;
		ordsz[j] = i - s;
	}

	/*
	 * Copy into flat memory.
	 */
	rsp = MALLOC(_LC_exsubs_t, nent * 2);
	(void) memcpy(rsp, sph, nent * sizeof (_LC_exsubs_t));
	(void) memcpy(rsp + nent, wsph, nent * sizeof (_LC_exsubs_t));

	free(sph);
	free(wsph);

	*nentp = nent;
	return (rsp);
}

struct wgtinfo {
	wchar_t	*ws;
	size_t	len;
	int	idx;
	struct wgtinfo *next;
};

#define	WGTHASHSZ	1021

static int
wgthash(wchar_t *ws, size_t len)
{
	ulong_t hcode = 0;

	while (len--)
		hcode += *ws++;

	hcode += ~(hcode << 9);
	hcode ^=  (hcode >> 14);
	hcode +=  (hcode << 4);
	hcode ^=  (hcode >> 10);

	return ((int)(hcode % WGTHASHSZ));
}

/*
 *  FUNCTION: wgtlookup
 *
 *  DESCRIPTION:
 *  Register weight string in the given hash table.
 */
static int
wgtlookup(struct wgtinfo **htbl, size_t *nidxp, wchar_t *ws, size_t len)
{
	int	key;
	struct wgtinfo *wp;

	key = wgthash(ws, len);
	for (wp = htbl[key]; wp != NULL; wp = wp->next) {
		if (wp->len != len)
			continue;
		if (wmemcmp(wp->ws, ws, len) == 0)
			break;
	}
	if (wp != NULL)
		return (wp->idx);

	wp = MALLOC(struct wgtinfo, 1);
	wp->ws = ws;
	wp->len = len;
	wp->idx = *nidxp;
	wp->next = htbl[key];
	htbl[key] = wp;
	*nidxp += (len + 1);
	return (wp->idx);
}

/*
 *  FUNCTION: build_wgtstr
 *
 *  DESCRIPTION:
 *  Extract registerd weight string into flat memory.
 */
static wchar_t *
build_wgtstr(struct wgtinfo **htbl, size_t size)
{
	int	i;
	wchar_t *wstr;
	struct wgtinfo *wip, *wnext;

	/*
	 * Now hash table is ready.  We extract weight string into the
	 * location of flat memory where it is calculated when weight
	 * strings were registered into hash.
	 */
	wstr = MALLOC(wchar_t, size);

	for (i = 0; i < WGTHASHSZ; i++) {
		for (wip = htbl[i]; wip != NULL; wip = wnext) {
			wnext = wip->next;
			/* the first entry is size */
			wstr[wip->idx] = wip->len;
			/* weights follow */
			(void) wmemcpy(&wstr[wip->idx + 1], wip->ws, wip->len);
			free(wip);
		}
	}
	return (wstr);
}

/*
 *  FUNCTION: build_extinfo
 *
 *  DESCRIPTION:
 *  build extended callation info (_LC_collextinfo_t)
 */
static void
build_extinfo(void)
{
	int	i, j, k, idx, nsubs, tsize;
	int	max_order = collate.co_nord + collate.co_r_order;
	char	*map;
	int	*ordidx, *ordsz;
	wchar_t	*ws;
	size_t	l, nwgtidx;
	collrec_t *cr;
	_LC_collextinfo_t *ext;
	_LC_exsubs_t *subs, *esubs;
	struct wgtinfo **whash;

	/*
	 * table starts from offset 1, because the offset 0 cannot be
	 * used since it represents IGNORE in weights table.
	 */
	nwgtidx = 1;

	ext = MALLOC(_LC_collextinfo_t, 1);

	map = MALLOC(char, (max_wchar_enc + 1) + 1);
	whash = MALLOC(struct wgtinfo *, WGTHASHSZ);
	/*
	 * Go through all the characters and generate a weight string table.
	 * Each weight string consists of one first wchar_t which holds
	 * number of weights and following weights. Weights are first
	 * registered into hash table so that no duplicate entries exist. At
	 * same time, weights are updated to point where corresponding weigts
	 * located.
	 */
	for (i = 0; i <= max_wchar_enc; i++) {
		k = 0;
		for (j = 0; j < NUM_ORDER; j++) {
			if ((cr = find_collrec_by_wc(i, j)) == NULL ||
			    cr->wstr == NULL) {
				continue;
			}
			if (collate.co_coltbl[i][j] != SUB_STRING)
				INTERNAL_ERROR;
			idx = wgtlookup(whash, &nwgtidx, cr->wstr, cr->wslen);
			collate.co_coltbl[i][j] = 0 - idx;
			k++;
		}
		if (k != 0) {
			map[i + 1] |= 0x10;
			map[0] |= 0x10;
		}
	}

	/*
	 * create subs table for many-to-many/one mappings
	 * (collating-element)
	 */
	ordidx  = MALLOC(int, max_order + 1);
	ordsz  = MALLOC(int, max_order + 1);
	if ((subs = collel_subs(&map[1], ordidx, ordsz, &tsize)) != NULL) {
		/*
		 * We have some substring/substitute entries.
		 */
		ext->ext_hsuboff = (unsigned int *)ordidx;
		ext->ext_hsubsz = (unsigned int *)ordsz;
		/*
		 * Calculate the table size which we need to deliver.
		 * We do this because, subs includes weights for r_order.
		 * If we don't need r_order, table size should be smaller.
		 */
		nsubs = ordidx[max_order] + ordsz[max_order];
		ext->ext_nsubs = nsubs;
		ext->ext_hsubs = subs;
		ext->ext_hwsubs = subs + tsize;

		/*
		 * First, we register weight string into hash, and store
		 * table index instead of pointer.
		 */
		/* single byte entries */
		subs = ext->ext_hsubs;
		esubs = subs + nsubs;
		while (subs != esubs) {
			ws = subs->ess_wgt.wgtstr;
			l = *ws++;
			idx = wgtlookup(whash, &nwgtidx, ws, l);
			subs->ess_wgt.wgtidx = idx;
			subs++;
		}
		/* wide char entries */
		subs = ext->ext_hwsubs;
		esubs = subs + nsubs;
		while (subs != esubs) {
			ws = subs->ess_wgt.wgtstr;
			l = *ws++;
			idx = wgtlookup(whash, &nwgtidx, ws, l);
			subs->ess_wgt.wgtidx = idx;
			subs++;
		}

		map[0] |= 0x01;
	}

	/*
	 * Generate weight string table, if someone has registered
	 * weights.
	 */
	if (nwgtidx > 1) {
		ws = build_wgtstr(whash, nwgtidx);
		ext->ext_wgtstrsz = nwgtidx;
		ext->ext_wgtstr = ws;
		ext->ext_submap = map;
	} else {
		free(ordidx);
		free(ordsz);
		free(map);
	}

	ext->ext_col_max = coll_wgt_max;

	collate.co_extinfo = ext;
	collate.co_ext = 1;

	free(whash);
}

/*
 *  FUNCTION: char_info
 *
 *  DESCRIPTION:
 *  Generate character information for diagnostic purpose.
 */
char *
char_info(wchar_t wc)
{
	int	rc, k, len;
	char	undef_char[MB_LEN_MAX];
	static char obuf[512];
	char	*sym, *p;
	collrec_t *cr;

	rc = INT_METHOD((int (*)(_LC_charmap_t *, char *, wchar_t))
	    METH_OFFS(CHARMAP_WCTOMB_AT_NATIVE))(&charmap, undef_char, wc);
	if (rc < 0) {
		(void) snprintf(obuf, sizeof (obuf),
		    "(wide-char value: 0x%04x)", wc);
	} else if (rc == 0) {
		(void) snprintf(obuf, sizeof (obuf), "NULL character");
	} else {
		len = snprintf(obuf, sizeof (obuf), "0x%04x (", wc);
		for (k = 0; k < rc; k++) {
			len += snprintf(obuf + len, sizeof (obuf) - len,
			    "\\x%02x", (unsigned char)undef_char[k]);
		}
		cr = find_collrec_by_wc(wc, -1);
		if (cr != NULL && cr->tgt_symp != NULL) {
			sym = cr->tgt_symp->sym_id;
		} else if (wchar_defined(wc)) {
			sym = "<...>";
		} else {
			sym = "INVALID";
		}
		len += snprintf(obuf + len, sizeof (obuf) - len, " %s)", sym);
	}
	/*
	 * we'll output this message in the comment of C source. escape chars
	 * from being a end of comments.
	 */
	p = obuf;
	while ((p = strstr(p, "*/")) != NULL) {
		if (len >= (sizeof (obuf) - 2))
			break;
		p++;
		k = obuf + len - p;
		(void) memmove(p + 1, p, k + 1);
		*p = '\\';
		len++;
	}
	return (obuf);
}

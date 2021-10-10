/*
 * Copyright 1996-2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#pragma ident	"%Z%%M%	%I%	%E% SMI"

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
 * static char rcsid[] = "@(#)$RCSfile: sem_ctype.c,v $ $Revision: 1.4.2.3 $"
 *	" (OSF) $Date: 1992/02/18 20:26:02 $";
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
 * 1.9  com/cmd/nls/sem_ctype.c, , bos320, 9135320l 8/22/91 13:17:26
 */
#include <stdlib.h>
#include "locdef.h"

#define	MAX_CHAR_CLASS_TRANS	64

/*
 *  FUNCTION: add_char_ct_name
 *
 *  DESCRIPTION:
 *  Adds a classification name or transformation name and associated mask
 *  to ctype.bindtab
 */
struct lcbind_table *Lcbind_Table = (struct lcbind_table *)NULL;

/* ARGSUSED */
void
add_char_ct_name(_LC_ctype_t *ctype, struct lcbind_table *lcbind_table,
    char *s, _LC_bind_tag_t bindtag, char *orig_value, unsigned int mask,
    int defined)
{
	int i, j;


	if (ctype->nbinds > MAX_CHAR_CLASS_TRANS) {
		error(2, gettext(ERR_TOO_MANY_LCBIND), MAX_CHAR_CLASS_TRANS);
	}

	/*
	 * check if lcbind_table NULL and allocate space for
	 * MAX_CHAR_CLASS_TRANS entries.
	 */
	if (Lcbind_Table == NULL) {
		Lcbind_Table = MALLOC(struct lcbind_table,
		    MAX_CHAR_CLASS_TRANS);
		ctype->nbinds = 0;
	}

	/* search for entry name in names array */
	for (j = -1, i = 0; (i < ctype->nbinds) &&
	    ((j = strcmp(s, Lcbind_Table[i].lcbind.bindname)) > 0); i++)
		;

	/* insert new entry name unless already present */
	if (j != 0) {
		for (j = ctype->nbinds; j > i; j--)
			Lcbind_Table[j] = Lcbind_Table[j-1];

		ctype->nbinds++;

		Lcbind_Table[i].lcbind.bindname = MALLOC(char, strlen(s) + 1);
		(void) strcpy(Lcbind_Table[i].lcbind.bindname, s);
		Lcbind_Table[i].lcbind.bindtag = bindtag;
		/*
		 * tmp = MALLOC(int, 1);
		 * *tmp = mask;
		 * Lcbind_Table[i].lcbind.bindvalue = tmp;
		 */
		Lcbind_Table[i].nvalue = mask;
		if (orig_value != NULL)
			Lcbind_Table[i].orig_value = STRDUP(orig_value);
		else
			Lcbind_Table[i].orig_value = NULL;
		Lcbind_Table[i].defined = defined;
	}
}


/*
 * FUNCTION: add_charclass
 */
/* ARGSUSED1 */
void
add_charclass(_LC_ctype_t *ctype, struct lcbind_table *lcbind_table,
    _LC_bind_tag_t btag, int n)
{
	unsigned int a_bit = 0x01;
	int	i;
	int	j;
	int k;
	int l;
	int found_bindentry;
	int found_empty_bit;
	item_t *charclass;


	for (i = 0; i < n; i++) {
		charclass = sem_pop();

		/* search for bind entry - from LCBIND entries */
		found_bindentry = 0;
		for (j = 0; j < ctype->nbinds; j++) {
			if ((strcmp(Lcbind_Table[j].lcbind.bindname,
			    charclass->value.str) == 0) &&
			    (Lcbind_Table[j].lcbind.bindtag == btag)) {
				Lcbind_Table[j].defined = 1;
				found_bindentry = 1;
				break;
			}
		}

		/*
		 * Didn't find a bind entry so search for a free bit
		 */
		if (!found_bindentry) {
			for (k = 0; k < 32; k++) {
				found_empty_bit = 1;
				for (l = 0; l < ctype->nbinds; l++) {
					struct lcbind_table	*p;
					p = &Lcbind_Table[l];
					if ((a_bit == p->nvalue) &&
					    (btag == p->lcbind.bindtag)) {
						found_empty_bit = 0;
						break;
					}
				}
				if (found_empty_bit)
					break;
				a_bit <<= 1;
			}

			if (!found_empty_bit) {
				error(2, gettext(ERR_TOO_MANY_CLASS));
			}

			/* got a free bit!  make an entry */
			add_char_ct_name(ctype, Lcbind_Table,
			    charclass->value.str,
			    btag, NULL, a_bit, 1);
		}
		destroy_item(charclass);
	}
}


/*
 *  FUNCTION: add_ctype
 *
 *  DESCRIPTION:
 *  Creates a new character classification from the list of characters on
 *  the semantic stack.  The productions using this sem-action implement
 *  statements of the form:
 *
 *	print	<A>;...;<Z>;<a>;<b>;<c>;...;<z>
 *
 *  The function checks if the class is already defined and if so uses the
 *  mask in the table, otherwise a new mask is allocated and used.  This
 *  allows seeding of the table with the POSIX classnames and preassignment
 *  of masks to those classes.
 */
/* ARGSUSED1 */
void
add_ctype(_LC_ctype_t *ctype, struct lcbind_table *lcbind_table,
    char *ctype_symbol_name)
{
	size_t memsize;
	item_t   *it;
	int	i;
	uint64_t	j;
	unsigned int	mask;
	wchar_t	wc;

	mask = 0;
	/*
	 * check if mask array has been defined yet, and
	 * if not allocate memory
	 */
	if (ctype->mask == NULL) {
		if ((max_wchar_enc + 1) > 256)
			memsize = max_wchar_enc + 1;
		else
			memsize = 256;
		ctype->mask = MALLOC(unsigned int, memsize);

		/* make sure <space> is blank and printable */
		ctype->mask[Space_character_code] |= _ISBLANK | _ISPRINT;

	}


	/* find class name entry */
	for (i = 0; i < ctype->nbinds; i++) {
		struct lcbind_table	*p = &Lcbind_Table[i];
		if ((strcmp(p->lcbind.bindname, ctype_symbol_name) == 0) &&
		    (p->lcbind.bindtag == _LC_TAG_CCLASS) &&
		    (p->defined == 1)) {
			mask = p->nvalue;
			break;
		}
	}

	if (mask == 0)
		diag_error(gettext(ERR_MISSING_CHARCLASS), ctype_symbol_name);

	/* handle derived properties */
	switch (mask) {
	case _ISUPPER:
	case _ISLOWER:
		mask |= _ISPRINT | _ISALPHA | _ISGRAPH;
	break;

	case _ISDIGIT:
		mask |= _ISPRINT | _ISGRAPH | _ISXDIGIT;
	break;

	case _ISPUNCT:
	case _ISXDIGIT:
		mask |= _ISGRAPH | _ISPRINT;
	break;

	case _ISBLANK:
		mask |= _ISSPACE;
	break;

	case _ISALPHA:		/* _E7 in Solaris */
		mask |= _ISPRINT | _ISGRAPH;
		break;

	case _ISGRAPH:		/* _E6 in Solaris */
		mask |= _ISPRINT;
		break;

	case _E1:
	case _E2:
	case _E3:
	case _E4:
	case _E5:
		/*
		 * if generating compatible-EUC locale,
		 * a character in _E1, _E2, _E3, _E4, or _E5
		 * should be also classfied into
		 * "print" and "graph" classes
		 * for the backward compatibility.
		 */
		if (charmap.cm_fc_type == _FC_EUC &&
		    charmap.cm_pc_type == _PC_EUC) {
			mask |= _ISPRINT | _ISGRAPH;
		}
		break;

	default:
		break;
	};


	/* for each range on stack - add mask to class mask for character */
	while ((it = sem_pop()) != NULL) {

		if (it->type == SK_UNDEF) {
			destroy_item(it);
			continue;
		}

		for (j = it->value.range->min; j <= it->value.range->max; j++) {

			wc = wc_from_fc(j);
			/*
			 * only set masks for characters which are
			 * actually defined
			 */
			if (wc >= 0 && wchar_defined(wc))
				ctype->mask[wc] |= mask;

		}

		destroy_item(it);
	}
}


/*
 *  FUNCTION: push_char_sym
 *
 *  DESCRIPTION:
 *  Create character range from character symbol.  Routine expects
 *  character symbol on semantic stack.  This symbol is used to create a
 *  simple character range which is then pushed back on semantic stack.
 *  This production
 *
 *  Treatment of single characters as ranges allows a uniform handling of
 *  characters and character ranges in class definition statements.
 */
void
push_char_sym(void)
{
	item_t	*it0, *it1;

	it0 = sem_pop();

	if (it0->type == SK_UNDEF) {
		diag_error2(gettext(ERR_INVAL_CTYPE), it0->value.str);
		(void) sem_push(it0);
		return;
	}

	if (it0->type == SK_CHR) {
		it1 = create_item(SK_RNG,
		    it0->value.chr->fc_enc,
		    it0->value.chr->fc_enc);
	} else if (it0->type == SK_UINT64) {
		it1 = create_item(SK_RNG,
		    it0->value.uint64_no,
		    it0->value.uint64_no);
	} else {
		error(4, gettext(ERR_ILL_RANGE_SPEC));
	}

	destroy_item(it0);

	(void) sem_push(it1);
}


/*
 *  FUNCTION: push_char_range
 *
 *  DESCRIPTION:
 *  Modifies end point of range with character on top of stack. This rule is
 *  used by productions implementing expressions of the form:
 *
 *	<A>;...;<Z>
 */
void
push_char_range(void)
{
	item_t	*it0, *it1, *it2;

	it1 = sem_pop();		/* from character at end of range   */
	it0 = sem_pop();		/* from character at start of range */
	if (it1->type == SK_UNDEF) {
		if (it0->type == SK_UNDEF) {
			diag_error2(gettext(ERR_INVAL_CTYPE_RANGE1),
			    it0->value.str, it1->value.str);
		} else {
			diag_error2(gettext(ERR_INVAL_CTYPE_RANGE2),
			    it1->value.str);
		}

		destroy_item(it1);
		(void) sem_push(it0);
		return;
	} else if (it0->type == SK_UNDEF) {
		diag_error2(gettext(ERR_INVAL_CTYPE_RANGE3), it0->value.str);
		switch (it1->type) {
		case SK_CHR:
			it2 = create_item(SK_RNG,
			    it1->value.chr->fc_enc,
			    it1->value.chr->fc_enc);
			break;
		case SK_UINT64:
			it2 = create_item(SK_RNG,
			    it1->value.uint64_no,
			    it1->value.uint64_no);
			break;
		default:
			error(4, gettext(ERR_ILL_RANGE_SPEC));
			break;
		}
		destroy_item(it1);
		destroy_item(it0);
		(void) sem_push(it2);
		return;
	}

	if (it1->type == SK_CHR && it0->type == SK_RNG) {
		/* make sure min is less than max */
		if (it1->value.chr->fc_enc > it0->value.range->max)
			it0->value.range->max = it1->value.chr->fc_enc;
		else
			it0->value.range->min = it1->value.chr->fc_enc;
	} else if (it1->type == SK_UINT64 && it0->type == SK_RNG) {
		if (it1->value.uint64_no > it0->value.range->max)
			it0->value.range->max = it1->value.uint64_no;
		else
			it0->value.range->min = it1->value.uint64_no;
	} else
		INTERNAL_ERROR;

	destroy_item(it1);

	(void) sem_push(it0);
}


#define	LCBIND_SYMBOL_TABLE	100
struct lcbind_symbol_table lcbind_symbol_table[LCBIND_SYMBOL_TABLE];
int length_lcbind_symbol_table = 0;

void
sem_set_lcbind_symbolic_value(void)
{
	item_t	*symbol_name;
	item_t	*hexadecimal_number;
	unsigned int hex_num;
	int	i;


	hexadecimal_number = sem_pop();
	hex_num = hexadecimal_number->value.int_no;
	symbol_name = sem_pop();

	for (i = 0; i < length_lcbind_symbol_table; i++) {
		if (strcmp(symbol_name->value.str,
		    lcbind_symbol_table[i].symbol_name) == 0) {
			error(4, gettext(ERR_LCBIND_DUPLICATE_NAME));
		}
		if (hex_num == lcbind_symbol_table[i].value) {
			error(4, gettext(ERR_LCBIND_DUPLICATE_VALUE), hex_num);
		}
	}

	/*
	 * didn't find a duplicate so put in an entry
	 */
	if (length_lcbind_symbol_table == LCBIND_SYMBOL_TABLE) {
		error(2, gettext(ERR_TOO_MANY_LCBIND_SYMBOL));
	}
	lcbind_symbol_table[length_lcbind_symbol_table].symbol_name =
	    STRDUP(symbol_name->value.str);
	lcbind_symbol_table[length_lcbind_symbol_table].value = hex_num;
	length_lcbind_symbol_table++;

	destroy_item(symbol_name);
	destroy_item(hexadecimal_number);
}

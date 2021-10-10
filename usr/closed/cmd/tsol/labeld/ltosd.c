/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file may contain confidential information of the Defense
 * Intelligence Agency and MITRE Corporation and should not be
 * distributed in source form without approval from Sun Legal.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>

#include <tsol/label.h>

#include "impl.h"

#undef	NO_CLASSIFICATION
#undef	ALL_ENTRIES
#undef	ACCESS_RELATED
#undef	SHORT_WORDS
#undef	LONG_WORDS

#include "gfi/std_labels.h"

#define	PROMOTE(l, lo) LCLASS_SET((l), (lo)->l_classification); \
	(l)->_comps = *(Compartments_t *)(lo)->l_compartments;
#define	DEMOTE(l, hi) LCLASS_SET((l), (hi)->l_classification); \
	(l)->_comps = *(Compartments_t *)(hi)->l_compartments;

/* 0x + Classification + Compartments + end of string */
#define	HEX_SIZE 2+(sizeof (CLASSIFICATION)*2)+(sizeof (COMPARTMENTS)*2)+1

static char digits[] = "0123456789abcdef";

#define	HEX(h, i, l, s) h[i++] = '0'; h[i++] = 'x'; for (; i < s; /* */) {\
	h[i++] = digits[(unsigned int)(*l >> 4)];\
	h[i++] = digits[(unsigned int)(*l++&0xF)]; }\
	h[i] = '\0'

static void
hexconv(_mac_label_impl_t *l, char *h)
{
	int	i = 0;
	unsigned char	*hl = (unsigned char *)&((l)->_lclass);

	i = 0;
	HEX(h, i, hl, HEX_SIZE - 1);
}

#define	__VIEW (DEF_NAMES | SHORT_NAMES | LONG_NAMES)
static int
convert_flags(uint_t f)
{
	switch (f & __VIEW) {
	case SHORT_NAMES:
		return (SHORT_WORDS);
	case LONG_NAMES:
	default:
		return (LONG_WORDS);
	}
/* NOTREACHED */
}

#define	lscall call->cargs.ls_arg
#define	lsret ret->rvals.ls_ret
/*
 *	convert label to SL or Clearance strings.
 *
 *	Entry	label = Label to translate.
 *		flags = Word lengths to use.
 *
 *	Exit	None.
 *
 *	Returns	err = 1, If successful.
 */

void
ltos(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_mac_label_impl_t *l = (_mac_label_impl_t *)&lscall.label;
	uint_t	f = lscall.flags;
	char	*s = lsret.buf;
	int	word_length = convert_flags(f);
	struct l_tables	*l_tables;	/* which parse table */
	int	check_validity = TRUE;
	struct	l_sensitivity_label *l_lo;
					/* common for both SL and CLR */
	struct	l_sensitivity_label *l_hi = l_hi_sensitivity_label;

	*len = RET_SIZE(char, 0);
	if (debug > 1) {
		char	hex[HEX_SIZE];

		hexconv(l, hex);

		(void) printf("labeld op=ltos:\n");
		(void) printf("\tlabel[flags=%x]=\"%s\" word_len =%x,\n",
		    f, hex, word_length);
	}
	if (_MTYPE(l, SUN_MAC_ID)) {
		l_tables = l_sensitivity_label_tables;
		l_lo = l_lo_sensitivity_label;
	} else if (_MTYPE(l, SUN_UCLR_ID)) {
		l_tables = l_clearance_tables;
		l_lo = l_lo_clearance;
		if (_MEQUAL(l, (_mac_label_impl_t *)BCLTOSL(&admin_low))) {
			/*
			 * per JPLM the minimum clearance is not a valid
			 * clearance,
			 * but no need to validate that the classification
			 * passed to l_convert is valid since this label is
			 * admin_low and the classification will be promoted
			 * to a valid classification if VIEW_EXTERNAL is
			 * selected.
			 */
			check_validity = FALSE;
		}
	} else {
		if (debug > 1)
			(void) printf("\tinvalid label type\n");
		ret->err = -1;
		return;
	}
	/* Special Case Admin High and Admin Low */
	if (_MEQUAL(l, (_mac_label_impl_t *)BCLTOSL(&admin_low))) {
		if (f & VIEW_EXTERNAL) {
			if (debug > 1)
				(void) printf("\tpromoting admin_low label\n");
			PROMOTE(l, l_lo);
		} else {
			(void) strcpy(s, ADMIN_LOW);
			*len = RET_SIZE(char, sizeof (ADMIN_LOW));
			return;
		}
	} if (_MEQUAL(l, (_mac_label_impl_t *)BCLTOSL(&admin_high))) {
		if (f & VIEW_EXTERNAL) {
			if (debug > 1)
				(void) printf("\tdemoting admin_high label\n");
			DEMOTE(l, l_hi);
		} else {
			(void) strcpy(s, ADMIN_HIGH);
			*len = RET_SIZE(char, sizeof (ADMIN_HIGH));
			return;
		}
	}
	if (check_bounds(uc, (_blevel_impl_t *)l) != TRUE) {
		ret->err = -1;
		return;
	}

	(void) mutex_lock(&gfi_lock);

	if (l_convert(s, (CLASSIFICATION)LCLASS(l),
	    l_long_classification, (COMPARTMENTS *)&(l->_comps), NULL,
	    l_tables, NO_PARSE_TABLE, word_length, ALL_ENTRIES,
	    check_validity, NO_INFORMATION_LABEL) == FALSE) {
		(void) mutex_unlock(&gfi_lock);
		if (debug > 1)
			(void) printf("\tl_convert failed\n");
		ret->err = -1;
		return;
	}
	(void) mutex_unlock(&gfi_lock);
	*len = RET_SIZE(char, strlen(s));
	if (debug > 1)
		(void) printf("\tltos len[%d]=\"%s\"\n", *len, s);
} /* ltos */
#undef	lscall
#undef	lsret

#define	prcall call->cargs.pr_arg
#define	prret ret->rvals.pr_ret
/*
 *	convert label to various DIA banner page field strings.
 *
 *	Entry	label = Label to translate into banner page fields.
 *
 *	Exit	None.
 *
 *	Returns	err = 1, If successful.
 */

void
prtos(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_bslabel_impl_t *l = (_bslabel_impl_t *)&prcall.label;

	CLASSIFICATION	protect_as_class;
	char	*string;
	int	word_length = convert_flags(prcall.flags);
	size_t  slen;   /* caveats and channels return length */

	*len = RET_SIZE(char, 0);
	if (debug > 1) {
		char	hex[HEX_SIZE];

		hexconv((_mac_label_impl_t *)l, hex);

		(void) printf("labeld op=banner:\n");
		(void) printf("\tlabel[flags=%x]=\"%s\" word_len =%x,\n",
		    prcall.flags, hex, word_length);
	}
	if (!(_MTYPE(l, SUN_MAC_ID))) {
		if (debug > 1)
			(void) printf("\tbad Label\n");
		ret->err = -1;
		return;
	}

	/* Special Case Admin High and Admin Low */

	/*
	 * Map Sensitivity Label to:
	 * 	Admin Low ==> l_lo_sensitivity_label
	 * or	Admin High ==>  l_hi_sensitivity_label
	 */

	if (_MEQUAL(l, (_mac_label_impl_t *)BCLTOSL(&admin_low))) {
		if (debug > 1)
			(void) printf("\tpromoting admin_low label\n");
		PROMOTE(l, l_lo_sensitivity_label);
	} else if (_MEQUAL(l, (_mac_label_impl_t *)BCLTOSL(&admin_high))) {
		if (debug > 1)
			(void) printf("\tdemoting admin_high label\n");
		DEMOTE(l, l_hi_sensitivity_label);
	}
	if (!check_bounds(uc, l)) {
		ret->err = -1;
		return;
	}

	/* Guard against illegal classifications */
	if (LCLASS(l) < 0 ||
	    LCLASS(l) > l_hi_sensitivity_label->l_classification ||
	    l_long_classification[LCLASS(l)] == NULL) {
		if (debug > 1)
			(void) printf("\tbad label class\n");
		ret->err = -1;
		return;
	}

	protect_as_class = L_MAX(*l_classification_protect_as, LCLASS(l));

	if (call->op == PR_TOP) {
		/* header */

		string = &prret.buf[0];
		(void) strcpy(string, l_long_classification[protect_as_class]);
		*len += strlen(string);
		if (debug > 1)
			(void) printf("\t banner header "
			    "len[%d]=\"%s\"\n", *len, string);
		return;
	}

	if (call->op == PR_LABEL) {
		/* protect_as label */

		string = &prret.buf[0];
		/* protect_as classification */
		(void) strcpy(string, l_long_classification[protect_as_class]);

		/* protect_as compartments */
		(void) mutex_lock(&gfi_lock);
		if (l_convert(&string[strlen(string)],
		    (CLASSIFICATION)LCLASS(l), NO_CLASSIFICATION,
		    (COMPARTMENTS *)&(l->_comps), NULL,
		    l_sensitivity_label_tables, NO_PARSE_TABLE, word_length,
		    ALL_ENTRIES, FALSE, NO_INFORMATION_LABEL) == FALSE) {
			(void) mutex_unlock(&gfi_lock);
			if (debug > 1)
				(void) printf("\tl_convert protect_as "
				    "compartments failed\n");
			ret->err = -1;
			return;
		}

		(void) mutex_unlock(&gfi_lock);
		*len += strlen(string);
		if (debug > 1)
			(void) printf("\t banner protect as label "
			    "len[%d]=\"%s\"\n", *len, string);
		return;

	}
	(void) mutex_unlock(&gfi_lock);

	if (call->op == PR_CAVEATS) {
		/* caveats */
		string = &prret.buf[0];

		(void) mutex_lock(&gfi_lock);
		if (l_convert(string,
		    (CLASSIFICATION)LCLASS(l), NO_CLASSIFICATION,
		    (COMPARTMENTS *)&(l->_comps), NULL,
		    l_printer_banner_tables, NO_PARSE_TABLE, word_length,
		    ALL_ENTRIES, FALSE, NO_INFORMATION_LABEL) == FALSE) {
			(void) mutex_unlock(&gfi_lock);
			if (debug > 1)
				(void) printf("\tl_convert caveats failed\n");
			ret->err = -1;
			return;
		}

		(void) mutex_unlock(&gfi_lock);
		/* compensate for unwanted leading blank on empty strings */
		slen = strlen(string) + 1;
		string[slen] = '\0';
		*len += slen;
		if (debug > 1)
			(void) printf("\t banner printer caveats "
			    "len[%d]:%d=\"%s\"\n", *len, slen, string);
		return;
	}

	if (call->op == PR_CHANNELS) {
		/* printer channels */

		string = &prret.buf[0];
		(void) mutex_lock(&gfi_lock);
		if (l_convert(string,
		    (CLASSIFICATION)LCLASS(l), NO_CLASSIFICATION,
		    (COMPARTMENTS *)&(l->_comps), NULL,
		    l_channel_tables, NO_PARSE_TABLE, word_length,
		    ALL_ENTRIES, FALSE, NO_INFORMATION_LABEL) == FALSE) {
			(void) mutex_unlock(&gfi_lock);
			if (debug > 1)
				(void) printf("\tl_convert channels failed\n");
			ret->err = -1;
			return;
		}
		(void) mutex_unlock(&gfi_lock);
		/* compensate for unwanted leading blank on empty strings */
		slen = strlen(string) + 1;
		string[slen] = '\0';
		*len += slen;
		if (debug > 1)
			(void) printf("\t banner printer channels "
			    "len[%d]:%d=\"%s\"\n", *len, slen, string);
		return;
	}
}  /* prtos */
#undef	prcall
#undef	prret

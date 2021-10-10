/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file may contain confidential information of the Defense
 * Intelligence Agency and MITRE Corporation and should not be
 * distributed in source form without approval from Sun Legal.
 */

#include <errno.h>
#include <priv.h>
#include <stdio.h>
#include <zone.h>

#include <sys/tsol/priv.h>

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

#define	slcall call->cargs.sl_arg
#define	slret ret->rvals.sl_ret
/*
 *	parse string to SL or Clearance labels.
 *
 *	Entry	label = Label to update.
 *		flags = L_DEFAULT,
 *			L_MODIFY_EXISTING (for DIA synonyoms with L_DEFAULT),
 *			L_NEW_LABEL,
 *			L_NO_CORRECTION,
 *			L_CHECK_AR	  (call l_in_accreditation_range)
 *		string = String to parse.
 *			leading spaces and [, ] stripped off
 *			admin_low, admin_high parsed.
 *
 *	Exit	None.
 *
 *	Returns	err = 1, If successful.
 *		label = Updated.
 */

void
stol(labeld_call_t *call, labeld_ret_t *ret, size_t *len,
    const ucred_t *uc)
{
	_mac_label_impl_t	*l = (_mac_label_impl_t *)&slcall.label;
	uint_t		f = slcall.flags;
	char		*s = slcall.string;	/* scan pointer */
	_mac_label_impl_t	*uclabel;
	_mac_label_impl_t	*upper_bound = NULL;
	struct l_tables		*l_tables;	/* which parse table */
	struct	l_sensitivity_label *l_lo;
					/* common struct for both */
	CLASSIFICATION	class;
	int		l_ret;
	uint_t		lf = (f & ~L_CHECK_AR);	/* because L_DEFAULT == 0 */

	*len = RET_SIZE(sl_ret_t, 0);

	if (debug > 1) {
		char	hex[HEX_SIZE];

		hexconv(l, hex);

		(void) printf("labeld op=stol:\n");
		(void) printf("\tlabel[flags=%x]=\"%s\" string =%s,\n",
		    f, hex, s);
	}

	uclabel = (_mac_label_impl_t *)ucred_getlabel(uc);
	if (m_label_dup(&upper_bound, uclabel) != 0) {
		if (debug > 1) {
			int err = errno;

			(void) printf("\tm_label_dup(%s)\n", strerror(err));
		}
		ret->err = L_BAD_CLASSIFICATION;
		return;
	}

	/* check label to update */
	if (((lf == L_MODIFY_EXISTING) || (lf == L_DEFAULT)) &&
	    !check_bounds(uc, l)) {
		if (debug > 1)
			(void) printf("\tout of bounds\n");
		ret->err = L_BAD_CLASSIFICATION;
		return;
	}
	/* check types and guard against illegal classification */
	if (_MTYPE(l, SUN_MAC_ID)) {
		l_tables = l_sensitivity_label_tables;
		l_lo = l_lo_sensitivity_label;
		if (_MEQUAL(l, (_mac_label_impl_t *)&m_low)) {
			if (debug > 1)
				(void) printf("\tpromoting low mac label\n");
			PROMOTE(l, l_lo_sensitivity_label);
		} else if (_MEQUAL(l, (_mac_label_impl_t *)&m_high)) {
			if (debug > 1)
				(void) printf("\tdemoting high mac label\n");
			DEMOTE(l, l_hi_sensitivity_label);
		}
	} else if (_MTYPE(l, SUN_UCLR_ID)) {
		f = lf;	/* Clearances don't have accreditation ranges */
		l_tables = l_clearance_tables;
		l_lo = l_lo_clearance;
		if (_MEQUAL(l, (_mac_label_impl_t *)&clear_low)) {
			if (debug > 1)
				(void) printf("\tpromoting low clearance\n");
			PROMOTE(l, l_lo_clearance);
		} else if (_MEQUAL(l, (_mac_label_impl_t *)&clear_high)) {
			DEMOTE(l, l_hi_sensitivity_label);
			if (debug > 1)
				(void) printf("\tdemoting high clearance\n");
		}
	} else {
		if (debug > 1)
			(void) printf("\tinvalid label type\n");
		ret->err = L_BAD_CLASSIFICATION;
		return;
	}

	/*
	 * Protect against bad upper label boundary.
	 * if privileged or upper_bound admin high set to highest label.
	 * if admin_low, string in error, no label but admin_low possible.
	 */
	if ((ucred_getzoneid(uc) == GLOBAL_ZONEID) ||
	    (priv_ismember(ucred_getprivset(uc, PRIV_EFFECTIVE),
	    PRIV_SYS_TRANS_LABEL)) ||
	    (_MEQUAL(upper_bound, (_mac_label_impl_t *)&m_high))) {
		if (debug > 1)
			(void) printf("privileged or admin high process "
			    "setting upper bound to max label\n");
		DEMOTE(upper_bound, l_hi_sensitivity_label);
	} else if (_MEQUAL(upper_bound, (_mac_label_impl_t *)&m_low)) {
		if (debug > 1)
			(void) printf("admin low unprivileged process can "
			    "only translate admin low label, error "
			    "returned.\n");
		ret->err = 0;
		return;
	}

	/*
	 * Do something with flags here:
	 *	L_NEW_LABEL = new label will not have a valid class.
	 *		class = NO_LABEL;
	 *	L_DEFAULT = replace current label with correction if possible.
	 *	L_MODIFY_EXISTING = modify the existing label from the string.
	 *		don't modify class;
	 *	L_NO_CORRECTION = replace current label without correction.
	 *		class = FULL_PARSE, implies NO_LABEL;
	 *	L_CHECK_AR = check if final label is in accreditation range.
	 *		no action needed here;
	 */
	class = LCLASS(l);
	if (f & L_NO_CORRECTION) {
		class = FULL_PARSE;
	} else if (f & L_NEW_LABEL) {
		class = NO_LABEL;
	}
	if (debug > 1) {
		char	hex[HEX_SIZE];

		hexconv(l, hex);
		(void) printf("\told label class = %d, body = %s\n", class,
		    hex);
		(void) printf("\tlower bound class = %d, upper class = %d\n",
		    l_lo->l_classification,
		    (CLASSIFICATION)LCLASS(upper_bound));
	}
	(void) mutex_lock(&gfi_lock);
	/*
	 *	L_GOOD_LABEL (-1) == OK;
	 *	L_BAD_CLASSIFICATION (-2) == bad input classification: class,
	 *	L_BAD_LABEL (-3) == either string or input label bad
	 *	other == offset into string 0, if entire string.
	 *
	 *	additionally the return from a failed accreditation check
	 *	is M_OUTSIDE_AR (-4)
	 *	-3 translates to M_BAD_STRING, -2 translates to M_BAD_LABEL
	 */
	if ((l_ret = l_parse(s, &class, (COMPARTMENTS *)&l->_comps, NULL,
	    l_tables, l_lo->l_classification, l_lo->l_compartments,
	    (CLASSIFICATION)LCLASS(upper_bound),
	    (COMPARTMENTS *)&upper_bound->_comps)) != L_GOOD_LABEL) {
		/* parsing error */
		(void) mutex_unlock(&gfi_lock);

		if (debug > 1)
			(void) printf("\tparse error = %d\n", l_ret);
		ret->err = l_ret;
		return;
	}
	/* check that resultant MAC label is in accreditation range */
	if ((f & L_CHECK_AR) &&
	    !l_in_accreditation_range(class, (COMPARTMENTS *)&l->_comps)) {
		/* not in accreditation range */
		(void) mutex_unlock(&gfi_lock);

		if (debug > 1) {
			(void) printf("\tnot in accreditation range\n");
		}
		ret->err = M_OUTSIDE_AR;
		return;
	}
	(void) mutex_unlock(&gfi_lock);
	m_label_free(upper_bound);
	LCLASS_SET(l, class);
	slret.label = *(_mac_label_impl_t *)l;
	ret->err = l_ret;
	if (debug > 1) {
		char	hex[HEX_SIZE];

		hexconv(l, hex);
		(void) printf("\tnew label = %s\n", hex);
	}
} /* stol */
#undef	slcall
#undef	slret

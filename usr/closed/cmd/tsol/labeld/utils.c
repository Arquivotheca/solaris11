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

/*
 *	Labeld utility functions.
 */

#include <priv.h>
#include <tsol/label.h>
#include <zone.h>

#undef	NO_CLASSIFICATION
#undef	ALL_ENTRIES
#undef	SHORT_WORDS
#undef	LONG_WORDS
#undef	ACCESS_RELATED

#include "gfi/std_labels.h"

#include "impl.h"
#include <tsol/label.h>
#include <sys/tsol/priv.h>

/* ARGSUSED */
ushort_t
get_gfi_flags(uint_t flags, const ucred_t *uc)
{
	/* paf_label_xlate is 15 bits, bit 0 is ACCESS_RELATED flag */
/*
 *	TODO: Translation flags are not currently defined
 *	uint_t pf = ucred_getplags(uc, XLATE_BITS) << 1;
 */
	uint_t pf = 0;
	uint_t f = flags & ACCESS_RELATED;

	if (f == 0 && pf == 0) {
		f = default_flags;
	}
	return (f | pf | forced_flags);
}

int
check_bounds(const ucred_t *uc, _blevel_impl_t *level)
{
	int	privileged = (ucred_getzoneid(uc) == GLOBAL_ZONEID);
	const priv_set_t	*pset;

	if (BLDOMINATES(ucred_getlabel(uc), level)) {
		if (debug > 1)
			(void) printf("\tcheck_bounds within bounds\n");
		return (TRUE);
	}

	/*
	 * Check privileges of caller for label translation
	 */
	if (!privileged) {
		pset = ucred_getprivset(uc, PRIV_EFFECTIVE);
		privileged = priv_ismember(pset, PRIV_SYS_TRANS_LABEL);
	}

	if (!privileged) {
		if (debug > 1)
			(void) printf("\tcheck_bounds not privileged\n");
		return (FALSE);
	}
	if (debug > 1)
		(void) printf("\tcheck_bounds privileged\n");
	return (TRUE);
}

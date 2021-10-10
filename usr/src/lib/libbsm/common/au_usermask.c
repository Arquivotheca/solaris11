/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <errno.h>
#include <nss.h>
#include <secdb.h>
#include <stdlib.h>
#include <string.h>
#include <user_attr.h>
#include <zone.h>

#include <bsm/libbsm.h>

#include <adt_xlate.h>		/* adt_write_syslog */

/* ARGSUSED */
static int
audit_flags(const char *name, kva_t *kva, void *ctxt, void *pres)
{
	char *val;

	if ((val = kva_match(kva, USERATTR_AUDIT_FLAGS_KW)) != NULL) {
		if ((*(char **)ctxt = strdup(val)) == NULL) {
			adt_write_syslog("au_user_mask strdup failed", errno);
		}
		return (1);
	}
	return (0);
}

/*
 * Build user's audit preselection mask.
 *
 * per-user audit flags are optional and may be missing.
 * If global zone auditing is set, a local zone cannot reduce the default
 * flags.
 *
 * success flags = (system default success flags + per-user always success) -
 *			per-user never success flags
 * failure flags = (system default failure flags + per-user always failure) -
 *			per-user never failure flags
 */

int
au_user_mask(char *user, au_mask_t *mask)
{
	au_mask_t	global_mask;
	char		*user_flags = NULL;

	if (mask == NULL) {
		return (-1);
	}

	/*
	 * Get the audit default flags.  This will be the system wide
	 * Global zone flags, or, if the perzone policy is set, the
	 * zone's default flags.  If this fails, return an error code
	 * now and don't bother trying to get the user specific flags.
	 */

	if (auditon(A_GETAMASK, (caddr_t)&global_mask,
	    sizeof (global_mask)) == -1) {
		return (-1);
	}

	/*
	 * Get per-user audit flags.
	 */
	(void) _enum_attrs(user, audit_flags, &user_flags, NULL);
	if (user_flags == NULL) {
		mask->as_success = global_mask.as_success;
		mask->as_failure = global_mask.as_failure;
	} else {
		au_user_ent_t  per_user;
		uint32_t	policy;
		char		*last = NULL;

		(void) getauditflagsbin(_strtok_escape(user_flags,
		    KV_AUDIT_DELIMIT, &last), &(per_user.au_always));
		(void) getauditflagsbin(_strtok_escape(NULL,
		    KV_AUDIT_DELIMIT, &last), &(per_user.au_never));
		/* merge default and per-user */
		mask->as_success = global_mask.as_success |
		    per_user.au_always.as_success;
		mask->as_failure = global_mask.as_failure |
		    per_user.au_always.as_failure;
		mask->as_success &= ~(per_user.au_never.as_success);
		mask->as_failure &= ~(per_user.au_never.as_failure);
		free(user_flags);

		/*
		 * Enforce the policy that the local zone audit_flags cannot
		 * reduce the global zone defaults unless the perzone audit
		 * policy is set.
		 */
		if (auditon(A_GETPOLICY, (caddr_t)&policy,
		    sizeof (policy)) != 0) {
			return (-1);
		}
		if (((policy & AUDIT_PERZONE) == 0) &&
		    (getzoneid() != GLOBAL_ZONEID)) {
			mask->as_success |= global_mask.as_success;
			mask->as_failure |= global_mask.as_failure;
		}
	}

	return (0);
}

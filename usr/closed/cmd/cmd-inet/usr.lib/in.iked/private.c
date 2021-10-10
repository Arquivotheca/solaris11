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
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <net/pfkeyv2.h>
#include <stdio.h>

#include <ipsec_util.h>

#include "defs.h"

/*
 * File for private key access.
 *
 * In a world with black-box devices that sign data you feed it (e.g.
 * smartcards), this file may have less relevance.
 *
 * TODO:  Be smarter than just trusting the phase1_t - just in case.
 */

/* ARGSUSED */
void
ssh_policy_find_private_key(SshIkePMPhaseI pm_info,
    SshPolicyKeyType key_type, const unsigned char *hash_alg_in,
    const unsigned char *hash_in, size_t hash_len_in,
    SshPolicyFindPrivateKeyCB callback_in, void *callback_context_in)
{
	phase1_t *p1 = (phase1_t *)pm_info->policy_manager_data;
	SshPrivateKey private_key_ret = NULL;
	struct ike_rule *rp;

	PRTDBG(D_KEY, ("Finding private key..."));

	rp = p1->p1_rule;
	if (rp == NULL)
		goto bail;

	if (p1->p1_localcert == NULL) {
		p1->p1_localcert = certlib_find_local_ident(&rp->local_id);
		/*
		 * If I misconfigured the rules so I don't have a matching
		 * cert for the rule, bail!
		 */
		if (p1->p1_localcert == NULL)
			goto bail;
	}

	/*
	 * Use copies of public/private keys for the pm_info and
	 * returns, assuming that libike will free() them.
	 * (libumem should prove or disprove any such assumptions.)
	 */
	(void) ssh_private_key_copy(p1->p1_localcert->keys->key,
	    &private_key_ret);

bail:
	callback_in(private_key_ret, callback_context_in);
}

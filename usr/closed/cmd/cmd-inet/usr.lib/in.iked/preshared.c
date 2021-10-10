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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <netinet/in.h>
#include <net/pfkeyv2.h>
#include <time.h>
#include <stdio.h>
#include <libintl.h>

#include <ipsec_util.h>
#include "defs.h"
#include "readps.h"

/*
 * Preshared setup starts here, but finishes in readps.c.
 */

/*
 * Read in a list of pre-shared keys.  Called from main() when the daemon first
 * starts.
 */
void
preshared_init(void)
{
	const char *filename = PRESHARED_KEY_FILE;
	char *errorstr;

	PRTDBG(D_OP, ("Loading preshared keys..."));
	errorstr = preshared_load(filename, -1, B_TRUE);

	if (errorstr != NULL) {
		if (strncmp(errorstr, "DUP", 3) == 0)
			errorstr = gettext("Duplicate entries ignored\n");
		/*
		 * Error handling... Print to terminal, if exists.
		 * Debug logging already taken care of by other functions.
		 * Don't exit because we want to load other policy.
		 */
		PRTDBG(D_OP, ("Error reading %s: %s", filename, errorstr));
		if (!ignore_errors)
			EXIT_BADCONFIG2("Fatal errors in %s", filename);
	}
}

/*
 * Called when the daemon catches SIGHUP.
 * Currently equivalent to preshared_init().
 */
void
preshared_reload(void)
{
	preshared_init();
}


/*
 * The guts of ssh_policy_find_pre_shared_key(), so we can use it
 * in other places, too.
 */
preshared_entry_t *
lookup_pre_shared_key(SshIkePMPhaseI pm_info)
{
	struct sockaddr_storage local, remote;
	preshared_entry_t	*result = NULL;

	if (pm_info == NULL)
		return (NULL);

	if (!string_to_sockaddr(pm_info->local_ip, &local) ||
	    !string_to_sockaddr(pm_info->remote_ip, &remote)) {
		return (NULL);
	} else {
		assert(local.ss_family == remote.ss_family);
		switch (local.ss_family) {
		case AF_INET:
			result = lookup_ps_by_in_addr(
			    &(((struct sockaddr_in *)&local)->sin_addr),
			    &(((struct sockaddr_in *)&remote)->sin_addr));
			break;
		case AF_INET6:
			result = lookup_ps_by_in6_addr(
			    &(((struct sockaddr_in6 *)&local)->sin6_addr),
			    &(((struct sockaddr_in6 *)&remote)->sin6_addr));
			break;
		default:
			result = NULL;
			/* FALLTHRU */
		}
	}

	return (result);
}

/*
 * Using the identity fields in the pm_info structure, find the appropriate
 * pre-shared key.  Called from the IKE library.
 */
void
ssh_policy_find_pre_shared_key(SshIkePMPhaseI pm_info,
    SshPolicyFindPreSharedKeyCB callback_in, void *callback_context_in)
{
	uint8_t *pre_shared_key;
	size_t key_len = 0;
	preshared_entry_t *result;

	PRTDBG(D_POL, ("Finding preshared key..."));

	result = lookup_pre_shared_key(pm_info);

	if (result == NULL) {
		PRTDBG(D_POL, ("  No pre-shared key found!"));
		pre_shared_key = NULL;
	} else {
		key_len = result->pe_keybuf_bytes;
		pre_shared_key = ssh_malloc(key_len);
		if (pre_shared_key == NULL)
			key_len = 0;
		else
			(void) memcpy(pre_shared_key, result->pe_keybuf,
			    key_len);
	}

	callback_in(pre_shared_key, key_len, callback_context_in);
}

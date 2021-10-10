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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file implements the token list operation for this tool.
 * It loads the PKCS#11 modules, gets the list of slots with
 * tokens in them, displays the list, and cleans up.
 */

#include <stdio.h>
#include <string.h>
#include <cryptoutil.h>
#include <security/cryptoki.h>
#include "common.h"

static char *
flagString(CK_FLAGS tokenFlags)
{
	static char flagStr[5];
	(void) memset(flagStr, 0, sizeof (flagStr));

	if (tokenFlags & CKF_LOGIN_REQUIRED)
		(void) strlcat(flagStr, "L", sizeof (flagStr));
	if (tokenFlags & CKF_TOKEN_INITIALIZED)
		(void) strlcat(flagStr, "I", sizeof (flagStr));
	if (tokenFlags & CKF_USER_PIN_TO_BE_CHANGED)
		(void) strlcat(flagStr, "X", sizeof (flagStr));
	if (tokenFlags & CKF_SO_PIN_TO_BE_CHANGED)
		(void) strlcat(flagStr, "S", sizeof (flagStr));

	return (flagStr);
}

/*
 * Lists all slots with tokens in them.
 */
int
pk_tokens(int argc, char *argv[])
{
	CK_SLOT_ID_PTR	slots = NULL;
	CK_ULONG	slot_count = 0;
	CK_TOKEN_INFO	token_info;
	const char	*hdrfmt = NULL;
	const char	*fldfmt = NULL;
	CK_RV		rv = CKR_OK;
	int		i;


	/* Get rid of subcommand word "tokens". */
	argc--;
	argv++;

	/* No additional args allowed. */
	if (argc != 0)
		return (PK_ERR_USAGE);
	/* Done parsing command line options. */

	/* Get the list of slots with tokens in them. */
	if ((rv = get_token_slots(&slots, &slot_count)) != CKR_OK) {
		cryptoerror(LOG_STDERR,
		    gettext("Unable to get token slot list (%s)."),
		    pkcs11_strerror(rv));
		return (PK_ERR_PK11);
	}

	/* Make sure we have something to display. */
	if (slot_count == 0) {
		cryptoerror(LOG_STDERR, gettext("No slots with tokens found."));
		return (0);
	}

	/* Display the list. */
	hdrfmt = "%-7.7s  %-32.32s  %-32.32s  %-10.10s\n";
	fldfmt = "%-d        %-32.32s  %-32.32s  %-10.10s\n";
	(void) fprintf(stdout, "Flags: L=Login required"
	    "  I=Initialized  X=User PIN expired  S=SO PIN expired\n");
	(void) fprintf(stdout, hdrfmt, gettext("Slot ID"), gettext("Slot Name"),
	    gettext("Token Name"), gettext("Flags"));
	(void) fprintf(stdout, hdrfmt, "-------", "---------",
	    "----------", "-----");
	for (i = 0; i < slot_count; i++) {
		CK_SLOT_INFO slot_info;
		if ((rv = C_GetTokenInfo(slots[i], &token_info)) != CKR_OK) {
			cryptoerror(LOG_STDERR,
			    gettext("Unable to get slot %d token info (%s)."),
			    i, pkcs11_strerror(rv));
			continue;
		}
		if ((rv = C_GetSlotInfo(slots[i], &slot_info)) != CKR_OK) {
			cryptoerror(LOG_STDERR,
			    gettext("Unable to get slot %d slot info (%s)."),
			    i, pkcs11_strerror(rv));
			continue;
		}

		(void) fprintf(stdout, fldfmt,
		    slots[i],  /* slot id */
		    slot_info.slotDescription,
		    token_info.label,
		    flagString(token_info.flags));
	}

	/* Clean up. */
	free(slots);
	(void) C_Finalize(NULL);
	return (0);
}

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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file implements the inittoken operation for this tool.
 * The basic flow of the process is to load the PKCS#11 module,
 * find the token to be initialize , login using the SO pin,
 * and call C_InitToken.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <cryptoutil.h>
#include <security/cryptoki.h>
#include "common.h"

int
pk_inittoken(int argc, char *argv[])
/* ARGSUSED */
{
	int		opt;
	int		rv;
	extern int	optind_av;
	extern char	*optarg_av;
	char		*newlabel = NULL;
	char		*currlabel = NULL;
	int		slotID = -1;
	CK_UTF8CHAR_PTR	sopin;
	CK_ULONG	sopinlen;
	KMF_HANDLE_T	handle;

	/* Parse command line options.  Do NOT i18n/l10n. */
	while ((opt = getopt_av(argc, argv,
		"s:(slotid)"
		"n:(newlabel)"
		"l:(currlabel)")) != EOF) {
		switch (opt) {
			case 'l':	/* token specifier */
				if (currlabel)
					return (PK_ERR_USAGE);
				currlabel = optarg_av;
				break;
			case 'n': /* token specifier */
				if (newlabel)
					return (PK_ERR_USAGE);
				newlabel = optarg_av;
				break;
			case 's':
				slotID = atoi(optarg_av);
				break;
			default:
				return (PK_ERR_USAGE);
				/* NOTREACHED */
				break;
		}
	}

	/* No additional args allowed. */
	argc -= optind_av;
	argv += optind_av;
	if (argc != 0)
		return (PK_ERR_USAGE);

	if (slotID == -1 && (currlabel == NULL || !strlen(currlabel))) {
		cryptoerror(LOG_STDERR,
		    gettext("The current token is not identified by label "
			    "or slot number."));
		return (PK_ERR_SYSTEM);
	}
	if ((rv = get_pin(gettext("Enter SO PIN:"), NULL, &sopin, &sopinlen))
	    != CKR_OK) {
		cryptoerror(LOG_STDERR,
		    gettext("Unable to get SO PIN for token"));
		return (PK_ERR_SYSTEM);
	}

	if (slotID != -1) {
		CK_RV ckrv = C_Initialize(NULL);
		if (ckrv != CKR_OK)
			rv = (ckrv == CKR_PIN_INCORRECT ? KMF_ERR_AUTH_FAILED :
			    KMF_ERR_INTERNAL);

		ckrv =  C_InitToken(slotID, sopin, sopinlen,
		    (CK_UTF8CHAR_PTR)(newlabel ? newlabel : currlabel));

		if (ckrv != CKR_OK)
			rv = (ckrv == CKR_PIN_INCORRECT ? KMF_ERR_AUTH_FAILED :
			    KMF_ERR_INTERNAL);
	} else {
		if ((rv = kmf_initialize(&handle, NULL, NULL)) != KMF_OK)
			return (rv);
		rv = kmf_pk11_init_token(handle, currlabel, newlabel,
		    sopin, sopinlen);

		(void) kmf_finalize(handle);
	}

	free(sopin);

	if (rv == KMF_ERR_AUTH_FAILED) {
		cryptoerror(LOG_STDERR,
		    gettext("Incorrect passphrase."));
		return (PK_ERR_SYSTEM);
	} else if (rv != CKR_OK) {
		cryptoerror(LOG_STDERR,
		    gettext("Unable to initialize token."));
		return (PK_ERR_SYSTEM);
	} else {
		(void) fprintf(stdout, gettext("Token %s initialized.\n"),
		    (newlabel ? newlabel : currlabel));
	}
	return (0);
}

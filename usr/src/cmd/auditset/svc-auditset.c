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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * svc-auditset - auditset transient service (AUDITSET_FMRI) startup method;
 * sets non-/attributable mask in the kernel context.
 */

#include <audit_scf.h>
#include <audit_scf_to_kernel.h>
#include <bsm/adt.h>
#include <bsm/libbsm.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>

#if !defined(SMF_EXIT_ERR_OTHER)
#define	SMF_EXIT_ERR_OTHER	1
#endif

int
main(void)
{
	char		*auditset_fmri;
	char		*err_str = NULL;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	/* allow execution only inside the SMF facility */
	if ((auditset_fmri = getenv("SMF_FMRI")) == NULL ||
	    strcmp(auditset_fmri, AUDITSET_FMRI) != 0) {
		(void) printf(gettext("svc-auditset can be executed only "
		    "inside the SMF facility.\n"));
		return (SMF_EXIT_ERR_NOSMF);
	}

	/* check the c2audit module state */
	if (adt_audit_state(AUC_DISABLED)) {
#ifdef	DEBUG
		if (errno == ENOTSUP) {
			(void) printf("c2audit module is excluded from "
			    "the system(4); kernel won't be updated.\n");
		} else {
			(void) printf("%s\n", strerror(errno));
		}
#endif
		return (SMF_EXIT_OK);
	}

	/* non-global zone versus perzone policy */
	if (getzoneid() != GLOBAL_ZONEID) {
		uint32_t	curp;

		if (auditon(A_GETPOLICY, (caddr_t)&curp, 0) == -1) {
			(void) printf(gettext("svc-auditset could not get "
			    "the audit policy settings from kernel.\n"));
			return (SMF_EXIT_ERR_OTHER);
		}
		if ((curp & AUDIT_PERZONE) != AUDIT_PERZONE) {
			return (SMF_EXIT_OK);
		}
	}

	/* update default user preselection mask */
	if (!scf_to_kernel_flags(&err_str)) {
		if (err_str != NULL) {
			(void) printf("%s\n", err_str);
			free(err_str);
		}
		return (SMF_EXIT_ERR_OTHER);
	}

	/* update non-attributable audit mask */
	if (!scf_to_kernel_naflags(&err_str)) {
		if (err_str != NULL) {
			(void) printf("%s\n", err_str);
			free(err_str);
		}
		return (SMF_EXIT_ERR_OTHER);
	}

	/* update the audit policy for possible per-zone audit */
	if (!scf_to_kernel_policy(&err_str)) {
		if (err_str != NULL) {
			(void) printf("%s\n", err_str);
			free(err_str);
		}
		return (SMF_EXIT_ERR_OTHER);
	}

	/* set the kernel instance host-id for local audit tid lookup */
	(void) __do_sethost("auditset");

	return (SMF_EXIT_OK);
}

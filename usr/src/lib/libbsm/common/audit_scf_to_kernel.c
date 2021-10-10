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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * helper functions for updating active audit settings with the configured
 * values
 */
#include <audit_scf.h>
#include <audit_scf_to_kernel.h>

/*
 * scf_to_kernel_flags() - update the default user preselection mask
 */
boolean_t
scf_to_kernel_flags(char **err_str)
{
	char		*cmask;
	au_mask_t	bmask;

	if (!do_getflags_scf(&cmask) || cmask == NULL) {
		(void) asprintf(err_str, "Could not get configured default "
		    "user preselection mask.");
		return (B_FALSE);
	}

	(void) getauditflagsbin(cmask, &bmask);
	free(cmask);

	if (auditon(A_SETAMASK, (caddr_t)&bmask, sizeof (bmask)) == -1) {
		(void) asprintf(err_str, "Could not update kernel context with "
		    "the default user preselection mask.");
		return (B_FALSE);
	}

	DPRINT((dbfp, "default user preselection mask updated\n"));
	return (B_TRUE);
}

/*
 * scf_to_kernel_naflags() - update the active non-attributable audit mask
 */
boolean_t
scf_to_kernel_naflags(char **err_str)
{
	char		*cmask;
	au_mask_t	bmask;

	if (!do_getnaflags_scf(&cmask) || cmask == NULL) {
		(void) asprintf(err_str, "Could not get configured "
		    "non-attributable audit mask.");
		return (B_FALSE);
	}

	(void) getauditflagsbin(cmask, &bmask);
	free(cmask);

	if (auditon(A_SETKMASK, (caddr_t)&bmask, sizeof (bmask)) == -1) {
		(void) asprintf(err_str, "Could not update kernel context with "
		    "the non-attributable audit mask.");
		return (B_FALSE);
	}

	DPRINT((dbfp, "active non-attributable audit mask updated\n"));
	return (B_TRUE);
}

/*
 * scf_to_kernel_policy() - update the audit service policies
 */
boolean_t
scf_to_kernel_policy(char **err_str)
{
	uint32_t	policy;

	if (!do_getpolicy_scf(&policy)) {
		(void) asprintf(err_str, "Unable to get audit policy "
		    "configuration from the SMF repository.");
		return (B_FALSE);
	}

	if (auditon(A_SETPOLICY, (caddr_t)&policy, 0) != 0) {
		(void) asprintf(err_str, "Could not update active policy "
		    "settings.");
		return (B_FALSE);
	}

	DPRINT((dbfp, "active policy settings updated\n"));
	return (B_TRUE);
}

/*
 * scf_to_kernel_qctrl() - update/reset the kernel queue control parameters
 */
boolean_t
scf_to_kernel_qctrl(char **err_str)
{
	struct au_qctrl	cfg_qctrl;

	if (!do_getqctrl_scf(&cfg_qctrl)) {
		(void) asprintf(err_str, "Unable to gather audit queue control "
		    "parameters from the SMF repository.");
		return (B_FALSE);
	}

	DPRINT((dbfp, "Received qctrl controls from SMF repository:\n"));
	DPRINT((dbfp, "\thiwater: %u\n", cfg_qctrl.aq_hiwater));
	DPRINT((dbfp, "\tlowater: %u\n", cfg_qctrl.aq_lowater));
	DPRINT((dbfp, "\tbufsz: %u\n", cfg_qctrl.aq_bufsz));
	DPRINT((dbfp, "\tdelay: %ld\n", cfg_qctrl.aq_delay));

	/* overwrite the default (zeros) from the qctrl configuration */
	if (cfg_qctrl.aq_hiwater == 0) {
		cfg_qctrl.aq_hiwater = AQ_HIWATER;
		DPRINT((dbfp, "hiwater changed to active value: %u\n",
		    cfg_qctrl.aq_hiwater));
	}
	if (cfg_qctrl.aq_lowater == 0) {
		cfg_qctrl.aq_lowater = AQ_LOWATER;
		DPRINT((dbfp, "lowater changed to active value: %u\n",
		    cfg_qctrl.aq_lowater));
	}
	if (cfg_qctrl.aq_bufsz == 0) {
		cfg_qctrl.aq_bufsz = AQ_BUFSZ;
		DPRINT((dbfp, "bufsz changed to active value: %u\n",
		    cfg_qctrl.aq_bufsz));
	}
	if (cfg_qctrl.aq_delay == 0) {
		cfg_qctrl.aq_delay = AQ_DELAY;
		DPRINT((dbfp, "delay changed to active value: %ld\n",
		    cfg_qctrl.aq_delay));
	}

	if (auditon(A_SETQCTRL, (caddr_t)&cfg_qctrl, 0) != 0) {
		(void) asprintf(err_str,
		    "Could not configure audit queue controls.");
		return (B_FALSE);
	}

	DPRINT((dbfp, "active qctrl parameters set\n"));
	return (B_TRUE);
}

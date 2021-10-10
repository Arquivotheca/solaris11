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

#include <tsol/label.h>
#include <sys/tsol/tndb.h>
#include <sys/tsol/label_macro.h>

#include <ike/sshincludes.h>
#include <ike/isakmp.h>
#include <ike/sshmp.h>
#include <ike/isakmp_doi.h>
#include <ike/cmi.h>
#include <ipsec_util.h>

#include "defs.h"

boolean_t ike_labeled = B_FALSE;
boolean_t hide_outer_label = B_FALSE;

int	system_doi = 0;

boolean_t
is_ike_labeled(void)
{
	if (!is_system_labeled())
		return (B_FALSE);

	return (ike_labeled);
}

void
set_ike_label_aware(void)
{
	ike_labeled = B_TRUE;
}

bslabel_t *
string_to_label(char *s)
{
	int err;

	bslabel_t *sl = NULL;

	err = str_to_label(s, &sl, MAC_LABEL, L_DEFAULT, NULL);

	if (err != 0)
		return (NULL);	/* XXX check callers */

	return (sl);
}

static void
ucred_setlabel(ucred_t *uc, bslabel_t *label)
{
	bslabel_t *ucl = ucred_getlabel(uc);

	*ucl = *label;
}

void
init_rule_label(struct ike_rule *global, struct ike_rule *rule)
{
	if (!is_system_labeled())
		return;

	if (!global->label_set)
		set_outer_label(global, B_FALSE, NULL);

	rule->p1_override_label = global->p1_override_label;
	rule->p1_implicit_label = global->p1_implicit_label;

	rule->outer_bslabel = global->outer_bslabel;
}

static void update_rule_label(struct ike_rule *rule)
{
	int len;
	ucred_t *outer_ucred;
	sadb_sens_t *sens;
	boolean_t hide;

	bslabel_t *label = rule->outer_bslabel;

	outer_ucred = ucred_get(getpid());

	if (rule->p1_override_label) {
		hide = rule->p1_implicit_label;
		rule->p1_override_label = B_TRUE;

		len = ipsec_convert_sl_to_sens(system_doi, label, NULL);
		sens = ssh_malloc(len);
		if (sens == NULL)
			return;		/* XXX error handling */

		(void) memset(sens, 0, len);

		(void) ipsec_convert_sl_to_sens(system_doi, label, sens);

		sens->sadb_sens_exttype = SADB_X_EXT_OUTER_SENS;
		if (hide)
			sens->sadb_x_sens_flags |= SADB_X_SENS_IMPLICIT;

		rule->outer_label = sens;

		ucred_setlabel(outer_ucred, label);
	}
	rule->outer_ucred = outer_ucred;
}

void
rulebase_update_label(struct ike_rulebase *rbp)
{
	int ix;

	for (ix = 0; ix < rbp->num_rules; ++ix)
		update_rule_label(rbp->rules[ix]);
}


void
set_outer_label(struct ike_rule *rule, boolean_t hide, bslabel_t *label)
{
	rule->label_set = B_TRUE;

	if (!is_system_labeled())
		return;

	hide_outer_label = hide; /* XXX XXX */

	rule->outer_bslabel = label;

	if (label == NULL) {
		rule->p1_override_label = B_FALSE;
		rule->p1_implicit_label = B_FALSE;
	} else {
		rule->p1_override_label = B_TRUE;
		rule->p1_implicit_label = hide;
	}
}

boolean_t
label_already(struct ike_rule *rule)
{
	if (rule->p1_multi_label)
		return (B_TRUE);

	if (rule->p1_single_label)
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * Set return values to "safe" values for unknown peers.
 * min=admin_high, max=admin_low -> no valid label intersects range.
 */
static void
label_range_fail(int *label_doi, bslabel_t *min, bslabel_t *max,
    boolean_t *label_aware)
{
	*label_doi = -1;
	bslhigh(min);
	bsllow(max);
	*label_aware = B_FALSE;
}

void
fix_p1_label_range(phase1_t *p1)
{
	char *min_slp;
	char *max_slp;
	boolean_t label_aware;
	struct ike_rule *rule;

	/*
	 * Based on remote_ip, find address-based clearance
	 */
	get_host_label_range(&p1->p1_remote,
	    &p1->label_doi, &p1->min_sl, &p1->max_sl, &label_aware);
	PRTDBG((D_POL | D_P1), ("  LABEL DOI: %d (0x%x)", p1->label_doi,
	    p1->label_doi));
	ipsec_convert_bslabel_to_string(&p1->min_sl, &min_slp);
	ipsec_convert_bslabel_to_string(&p1->max_sl, &max_slp);
	PRTDBG((D_POL | D_P1), ("  LABEL MIN: %s", min_slp));
	PRTDBG((D_POL | D_P1), ("  LABEL MAX: %s", max_slp));
	ssh_free(min_slp);
	ssh_free(max_slp);

	rule = p1->p1_rule;

	if (rule->p1_single_label)
		p1->label_aware = B_FALSE;
	else if (rule->p1_multi_label)
		p1->label_aware = B_TRUE;
	else
		p1->label_aware = label_aware;

	p1->outer_ucred = rule->outer_ucred;

	if (!label_aware) {
		ucred_t *outer_ucred = ucred_get(getpid());
		ucred_setlabel(outer_ucred, &(p1->max_sl));
		p1->outer_ucred = outer_ucred;
	}
	if (rule->p1_override_label) {
		p1->outer_label = rule->outer_label;
	}

	/*
	 * We may further narrow this later.
	 */
}

#include <libtsnet.h>
#include <string.h>

/*
 * This function is a candidate for moving to libtsnet.
 */
void
get_host_label_range(struct sockaddr_storage *ss,
    int *label_doi, bslabel_t *min, bslabel_t *max, boolean_t *label_aware)
{
	tsol_rhent_t rhent;
	tsol_tpent_t tp;

	label_range_fail(label_doi, min, max, label_aware);

	(void) memset(&rhent, 0, sizeof (rhent));

	rhent.rh_address.ta_family = ss->ss_family;
	switch (ss->ss_family) {
	case AF_INET:
		rhent.rh_address.ip_addr_v4 = *(struct sockaddr_in *)ss;
		break;
	case AF_INET6:
		rhent.rh_address.ip_addr_v6 = *(struct sockaddr_in6 *)ss;
		break;
	default:
		return;
	}

	if (tnrh(TNDB_GET, &rhent) != 0)
		return;

	if (rhent.rh_template[0] == '\0')
		return;

	(void) strlcpy(tp.name, rhent.rh_template, sizeof (tp.name));

	if (tnrhtp(TNDB_GET, &tp) != 0)
		return;

	*label_doi = tp.tp_doi;
	*label_aware = B_FALSE;

	switch (tp.host_type) {
	case SUN_CIPSO:
		*min = tp.tp_sl_range_cipso.lower_bound;
		*max = tp.tp_sl_range_cipso.upper_bound;
		*label_aware = B_TRUE;
		break;

	case UNLABELED:
		*min = tp.tp_def_label;
		*max = tp.tp_def_label;
		break;

	default:
		return;
	}
}

/*
 * XXX range check sense byte len
 *
 * Assumes DOI has already been checked.
 */
boolean_t
sit_to_bslabel(SshIkeIpsecSituationPacket sit, bslabel_t *sl)
{
	int sens_bits = sit->secrecy_category_bitmap_length;
	int sens_bytes = roundup_bits_to_64(sens_bits);

	bsllow(sl);
	LCLASS_SET((_bslabel_impl_t *)sl, sit->secrecy_level_data[0]);
	(void) memcpy(&((_bslabel_impl_t *)sl)->compartments,
	    sit->secrecy_category_bitmap_data, sens_bytes);

	return (B_TRUE);
}

void
prtdbg_label(char *msg, bslabel_t *label)
{
	char *str1;
	char *str2;

	if ((debug & D_LABEL) == 0)
		return;

	ipsec_convert_bslabel_to_hex(label, &str1);
	ipsec_convert_bslabel_to_string(label, &str2);
	PRTDBG(D_LABEL, ("%s %s (%s)", msg, str1, str2));
	ssh_free(str1);
	ssh_free(str2);
}

void
init_system_label(sadb_sens_t *sens)
{
	if (system_doi != 0)
		return;

	system_doi = sens->sadb_sens_dpd;
}

void
label_update(void)
{
	if (!is_system_labeled())
		return;

	rulebase_update_label(&rules);
}

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

#include <sys/types.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <libnvpair.h>

#include <scsi/libses.h>
#include <scsi/libses_plugin.h>
#include <scsi/plugins/ses/framework/ses2.h>
#include <scsi/plugins/ses/framework/ses2_impl.h>
#include <scsi/plugins/ses/vendor/sun.h>
#include <scsi/plugins/ses/vendor/sun_impl.h>

static int
elem_parse_sunw_fanmodule(const ses2_elem_status_impl_t *esip, nvlist_t *nvl)
{
	sun_fanmodule_status_impl_t *fip = (sun_fanmodule_status_impl_t *)esip;
	int nverr;

	SES_NV_ADD(boolean_value, nverr, nvl, SES_PROP_IDENT, fip->sfsi_ident);
	SES_NV_ADD(boolean_value, nverr, nvl, SES_PROP_FAIL, fip->sfsi_fail);

	return (0);
}

static const struct status_parser {
	ses2_element_type_t type;
	int (*func)(const ses2_elem_status_impl_t *, nvlist_t *);
} status_parsers[] = {
	{ SES_ET_SUNW_FANMODULE, elem_parse_sunw_fanmodule },
	{ (ses2_element_type_t)-1, NULL }
};

static int
elem_parse_sd(ses_plugin_t *spp, ses_node_t *np)
{
	ses2_elem_status_impl_t *esip;
	const struct status_parser *sp;
	nvlist_t *nvl = ses_node_props(np);
	size_t len;
	int nverr;
	uint64_t type;

	if ((esip = ses_plugin_page_lookup(spp,
	    ses_node_snapshot(np), SES2_DIAGPAGE_ENCLOSURE_CTL_STATUS,
	    np, &len)) == NULL)
		return (0);

	VERIFY(nvlist_lookup_uint64(nvl, SES_PROP_ELEMENT_TYPE,
	    &type) == 0);

	SES_NV_ADD(uint64, nverr, nvl, SES_PROP_STATUS_CODE,
	    esip->sesi_common.sesi_status_code);
	SES_NV_ADD(boolean_value, nverr, nvl, SES_PROP_SWAP,
	    esip->sesi_common.sesi_swap);
	SES_NV_ADD(boolean_value, nverr, nvl, SES_PROP_DISABLED,
	    esip->sesi_common.sesi_disabled);
	SES_NV_ADD(boolean_value, nverr, nvl, SES_PROP_PRDFAIL,
	    esip->sesi_common.sesi_prdfail);

	for (sp = &status_parsers[0]; sp->type != (ses2_element_type_t)-1; sp++)
		if (sp->type == type && sp->func != NULL)
			return (sp->func(esip, nvl));

	return (0);
}

static int
elem_parse_descr(ses_plugin_t *sp, ses_node_t *np)
{
	char *desc;
	size_t len;
	nvlist_t *props = ses_node_props(np);
	int nverr;

	if ((desc = ses_plugin_page_lookup(sp, ses_node_snapshot(np),
	    SES2_DIAGPAGE_ELEMENT_DESC, np, &len)) == NULL)
		return (0);

	SES_NV_ADD(fixed_string, nverr, props, SES_PROP_DESCRIPTION,
	    desc, len);

	return (0);
}

int
sun_fill_element_node(ses_plugin_t *sp, ses_node_t *np)
{
	ses_snap_t *snap = ses_node_snapshot(np);
	nvlist_t *props = ses_node_props(np);
	sun_fru_descr_impl_t *sfdip;
	size_t len;
	int err;

	if ((err = elem_parse_sd(sp, np)) != 0)
		return (err);

	if ((err = elem_parse_descr(sp, np)) != 0)
		return (err);

	if ((sfdip = ses_plugin_page_lookup(sp, snap,
	    SUN_DIAGPAGE_FRUID, np, &len)) != NULL) {
		if ((err = sun_fruid_parse_common(sfdip, props)) != 0)
			return (err);
	}

	return (0);
}

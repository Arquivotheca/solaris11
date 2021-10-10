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

#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <alloca.h>
#include <libnvpair.h>

#include <scsi/libses.h>
#include <scsi/libses_plugin.h>
#include <scsi/plugins/ses/framework/libses.h>
#include <scsi/plugins/ses/vendor/sun.h>
#include <scsi/plugins/ses/vendor/sun_impl.h>
#include <scsi/plugins/ses/framework/ses2.h>
#include <scsi/plugins/ses/framework/ses2_impl.h>

#include "../../../../../../lib/libfru/libnvfru/nvfru.h"

int
sun_fruid_parse_common(sun_fru_descr_impl_t *sfdip, nvlist_t *nvl)
{
	int nverr;

	SES_NV_ADD(boolean_value, nverr, nvl,
	    LIBSES_PROP_FRU, sfdip-> sfdi_fru);
	SES_NV_ADD(uint64, nverr, nvl,
	    LIBSES_PROP_PHYS_PARENT, sfdip->sfdi_parent_element_index);

	return (0);
}

/*
 * Given an nvlist of properties and an array of property handlers, invoke the
 * appropriate handler for all supported properties.
 */
int
sun_setprop(ses_plugin_t *sp, ses_node_t *np,
    const ses2_ctl_prop_t *ctlprops, nvlist_t *props)
{
	const ses2_ctl_prop_t *cpp;
	nvpair_t *nvp, *next;

	for (nvp = nvlist_next_nvpair(props, NULL); nvp != NULL;
	    nvp = next) {
		next = nvlist_next_nvpair(props, nvp);
		for (cpp = ctlprops; cpp->scp_name != NULL; cpp++)
			if (strcmp(cpp->scp_name, nvpair_name(nvp)) == 0)
				break;
		if (cpp->scp_name == NULL)
			continue;

		if (cpp->scp_setprop(sp, np, cpp->scp_num, nvp) != 0)
			return (-1);

		(void) nvlist_remove(props, nvpair_name(nvp),
		    nvpair_type(nvp));
	}

	return (0);
}

int
sun_ctl_common_setprop(ses_plugin_t *sp, ses_node_t *np, ses2_diag_page_t page,
    nvpair_t *nvp)
{
	ses2_cmn_elem_ctl_impl_t *eip;
	const char *name;
	boolean_t v;

	ASSERT(page == SES2_DIAGPAGE_ENCLOSURE_CTL_STATUS);

	if ((eip = ses_plugin_ctlpage_lookup(sp, ses_node_snapshot(np),
	    page, 0, np, B_FALSE)) == NULL)
		return (-1);

	name = nvpair_name(nvp);
	(void) nvpair_value_boolean_value(nvp, &v);

	if (strcmp(name, SES_PROP_SWAP) == 0)
		eip->seci_rst_swap = !v;
	else if (strcmp(name, SES_PROP_DISABLED) == 0)
		eip->seci_disable = v;
	else if (strcmp(name, SES_PROP_PRDFAIL) == 0)
		eip->seci_prdfail = v;
	else
		ses_panic("Bad property %s", name);

	return (0);
}

static int
sun_node_parse(ses_plugin_t *sp, ses_node_t *np)
{
	switch (ses_node_type(np)) {
	case SES_NODE_ENCLOSURE:
		return (sun_fill_enclosure_node(sp, np));

	case SES_NODE_AGGREGATE:
	case SES_NODE_ELEMENT:
		return (sun_fill_element_node(sp, np));

	default:
		return (0);
	}
}

static int
sun_node_ctl(ses_plugin_t *sp, ses_node_t *np, const char *op,
    nvlist_t *nvl)
{
	switch (ses_node_type(np)) {
	case SES_NODE_ENCLOSURE:
		break;

	case SES_NODE_AGGREGATE:
	case SES_NODE_ELEMENT:
		return (sun_element_ctl(sp, np, op, nvl));
	}

	return (0);
}

/*ARGSUSED*/
static int
sun_node_type_known(ses_plugin_t *sp, ses_node_t *np)
{

	nvlist_t *props = ses_node_props(np);
	uint64_t type;

	VERIFY(nvlist_lookup_uint64(props, SES_PROP_ELEMENT_TYPE,
	    &type) == 0);

	if ((type != SES_ET_SUNW_FANMODULE) &&
	    (type != SES_ET_SUNW_POWERBOARD) &&
	    (type != SES_ET_SUNW_POWERMODULE) &&
	    (type != SES_ET_SUNW_FANBOARD)) {
		return (-1);
	}

	return (0);
}

int
_ses_init(ses_plugin_t *sp)
{
	ses_plugin_config_t config = {
		.spc_pages = sun_pages,
		.spc_node_parse = sun_node_parse,
		.spc_node_ctl = sun_node_ctl,
		.spc_node_type_known = sun_node_type_known
	};

	return (ses_plugin_register(sp, LIBSES_PLUGIN_VERSION,
	    &config) != 0);
}

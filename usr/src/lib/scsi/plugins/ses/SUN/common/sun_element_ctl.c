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
#include <scsi/plugins/ses/framework/ses2.h>
#include <scsi/plugins/ses/framework/ses2_impl.h>
#include <scsi/plugins/ses/vendor/sun.h>
#include <scsi/plugins/ses/vendor/sun_impl.h>

static int
elem_setprop_sunw_fanmodule(ses_plugin_t *sp, ses_node_t *np,
    ses2_diag_page_t page, nvpair_t *nvp)
{
	sun_fanmodule_ctl_impl_t *fip;
	const char *name;
	boolean_t v1;
	uint64_t v64;

	if ((fip = ses_plugin_ctlpage_lookup(sp, ses_node_snapshot(np),
	    page, 0, np, B_FALSE)) == NULL)
		return (-1);

	name = nvpair_name(nvp);

	if (strcmp(name, SES_COOLING_PROP_SPEED_CODE) == 0) {
		(void) nvpair_value_uint64(nvp, &v64);
		fip->sfci_requested_speed_code = v64;
		return (0);
	}

	(void) nvpair_value_boolean_value(nvp, &v1);

	if (strcmp(name, SES_PROP_IDENT) == 0) {
		fip->sfci_rqst_ident = v1;
	} else if (strcmp(name, SES_PROP_FAIL) == 0) {
		fip->sfci_rqst_fail = v1;
	} else {
		ses_panic("Bad property %s", name);
	}

	return (0);
}

static const ses2_ctl_prop_t sunw_fanmodule_props[] = {
{
	.scp_name = SES_PROP_IDENT,
	.scp_type = DATA_TYPE_BOOLEAN_VALUE,
	.scp_num = SES2_DIAGPAGE_ENCLOSURE_CTL_STATUS,
	.scp_setprop = elem_setprop_sunw_fanmodule
},
{
	.scp_name = SES_COOLING_PROP_SPEED_CODE,
	.scp_type = DATA_TYPE_UINT64,
	.scp_num = SES2_DIAGPAGE_ENCLOSURE_CTL_STATUS,
	.scp_setprop = elem_setprop_sunw_fanmodule
},
{
	.scp_name = SES_PROP_FAIL,
	.scp_type = DATA_TYPE_BOOLEAN_VALUE,
	.scp_num = SES2_DIAGPAGE_ENCLOSURE_CTL_STATUS,
	.scp_setprop = elem_setprop_sunw_fanmodule
},
{
	NULL
}
};

/*ARGSUSED*/
static int
elem_setdef_sunw_fanmodule(ses_node_t *np, ses2_diag_page_t page, void *data)
{
	sun_fanmodule_ctl_impl_t *fip = data;
	nvlist_t *props = ses_node_props(np);

	SES_NV_CTLBOOL(props, SES_PROP_IDENT, fip->sfci_rqst_ident);
	SES_NV_CTL64(props, SES_COOLING_PROP_SPEED_CODE,
	    fip->sfci_requested_speed_code);
	SES_NV_CTLBOOL(props, SES_PROP_FAIL, fip->sfci_rqst_fail);

	return (0);
}

#define	CTL_DESC(_e, _n)	\
	{	\
		.scd_et = _e,	\
		.scd_props = _n##_props,	\
		.scd_setdef = elem_setdef_##_n	\
	}

static const ses2_ctl_desc_t ctl_descs[] = {
	CTL_DESC(SES_ET_SUNW_FANMODULE, sunw_fanmodule),
	{ .scd_et = -1 }
};

int
sun_element_ctl(ses_plugin_t *sp, ses_node_t *np, const char *op,
    nvlist_t *nvl)
{
	const ses2_ctl_desc_t *dp;
	nvlist_t *props = ses_node_props(np);
	uint64_t type;

	if (strcmp(op, SES_CTL_OP_SETPROP) != 0)
		return (0);

	VERIFY(nvlist_lookup_uint64(props, SES_PROP_ELEMENT_TYPE,
	    &type) == 0);

	for (dp = &ctl_descs[0]; dp->scd_et != -1; dp++)
		if (dp->scd_et == type)
			break;

	if (dp->scd_et == -1)
		return (0);

	return (sun_setprop(sp, np, dp->scd_props, nvl));
}

int
sun_element_setdef(ses_node_t *np, ses2_diag_page_t page, void *data)
{
	const ses2_ctl_desc_t *dp;
	nvlist_t *props = ses_node_props(np);
	uint64_t type;

	VERIFY(nvlist_lookup_uint64(props, SES_PROP_ELEMENT_TYPE, &type) == 0);

	for (dp = &ctl_descs[0]; dp->scd_et != -1; dp++)
		if (dp->scd_et == type)
			break;

	if (dp->scd_et == -1)
		return (0);

	if (dp->scd_setdef(np, page, data) != 0)
		return (-1);

	return (0);
}

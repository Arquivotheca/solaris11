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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dld.h>
#include <sys/dld_ioc.h>
#include <fcntl.h>
#include <unistd.h>
#include <libdevinfo.h>
#include <libdladm_impl.h>
#include <libdlflow.h>
#include <libdllink.h>
#include <libdlflow_impl.h>
#include <libintl.h>
#include <zone.h>

#include <dlfcn.h>
#include <link.h>

static dladm_status_t	i_dladm_set_flowprop_db(dladm_handle_t, const char *,
			    const char *, char **, uint_t, const char *);
static dladm_status_t	i_dladm_get_flowprop_db(dladm_handle_t, const char *,
			    const char *, char **, uint_t *);

static fpd_getf_t	do_get_maxbw;
static fpd_setf_t	do_set_maxbw;
static fpd_checkf_t	do_check_maxbw;

static fprop_desc_t	prop_table[] = {
	{ "maxbw",	{ "", NULL }, NULL, 0, B_FALSE,
	    do_set_maxbw, NULL,
	    do_get_maxbw, do_check_maxbw}
};

#define	DLADM_MAX_FLOWPROPS	(sizeof (prop_table) / sizeof (fprop_desc_t))

static prop_table_t	prop_tbl = {
	prop_table,
	DLADM_MAX_FLOWPROPS
};

static resource_prop_t rsrc_prop_table[] = {
	{"maxbw",	extract_maxbw},
};
#define	DLADM_MAX_RSRC_PROP (sizeof (rsrc_prop_table) / \
	sizeof (resource_prop_t))

static dladm_status_t	flow_proplist_check(dladm_arg_list_t *);

zoneid_t
dladm_extra_names(const char *name, char *flowname, char *zonename)
{
	char	*token, tmp[MAXFLOWNAMELEN];

	(void) strlcpy(tmp, name, MAXFLOWNAMELEN);
	token = strchr(tmp, '/');
	if (token != NULL) {
		*token = '\0';
		(void) strlcpy(zonename, tmp, ZONENAME_MAX);
		token++;
		(void) strlcpy(flowname, token, MAXFLOWNAMELEN);
		return (getzoneidbyname(zonename));
	} else {
		(void) strlcpy(flowname, name, MAXFLOWNAMELEN);
		return (getzoneid());
	}
}

dladm_status_t
dladm_set_flowprop(dladm_handle_t handle, const char *flow,
    const char *prop_name, char **prop_val, uint_t val_cnt, uint_t flags,
    char **errprop, const char *root)
{
	dladm_status_t	status;
	char		fname[MAXFLOWNAMELEN], zonename[ZONENAME_MAX];
	zoneid_t	zoneid = getzoneid(), inputzoneid;
	boolean_t	onloan = B_FALSE;

	if (flow == NULL || (prop_val == NULL && val_cnt > 0) ||
	    (prop_val != NULL && val_cnt == 0) || flags == 0)
		return (DLADM_STATUS_BADARG);

	inputzoneid = dladm_extra_names(flow, fname, zonename);

	if (zoneid != inputzoneid)
		return (DLADM_STATUS_FLOW_WRONG_ZONE);

	(void) dladm_read_flowconf(handle, fname, &onloan);
	if (onloan == B_TRUE)
		return (DLADM_STATUS_FLOW_WRONG_ZONE);

	if ((flags & DLADM_OPT_ACTIVE) != 0) {
		status = i_dladm_set_prop_temp(handle, fname, prop_name,
		    prop_val, val_cnt, flags, errprop, &prop_tbl);
		if (status == DLADM_STATUS_TEMPONLY &&
		    (flags & DLADM_OPT_PERSIST) != 0)
			return (DLADM_STATUS_TEMPONLY);
		if (status != DLADM_STATUS_OK)
			return (status);
	}

	if ((flags & DLADM_OPT_PERSIST) != 0) {
		if (i_dladm_is_prop_temponly(prop_name, errprop, &prop_tbl))
			return (DLADM_STATUS_TEMPONLY);
		status = i_dladm_set_flowprop_db(handle, fname, prop_name,
		    prop_val, val_cnt, root);
	}

	return (status);
}

dladm_status_t
dladm_walk_flowprop(int (*func)(void *, const char *), const char *flow,
    void *arg)
{
	int	i;

	if (flow == NULL || func == NULL)
		return (DLADM_STATUS_BADARG);

	/* Then show data-flow properties if there are any */
	for (i = 0; i < DLADM_MAX_FLOWPROPS; i++) {
		if (func(arg, prop_table[i].pd_name) != DLADM_WALK_CONTINUE)
			break;
	}
	return (DLADM_STATUS_OK);
}

dladm_status_t
dladm_get_flowprop(dladm_handle_t handle, const char *flow, uint32_t type,
    const char *prop_name, char **prop_val, uint_t *val_cntp)
{
	dladm_status_t status;

	if (flow == NULL || prop_name == NULL || prop_val == NULL ||
	    val_cntp == NULL || *val_cntp == 0)
		return (DLADM_STATUS_BADARG);

	if (type == DLADM_PROP_VAL_PERSISTENT) {
		if (i_dladm_is_prop_temponly(prop_name, NULL, &prop_tbl))
			return (DLADM_STATUS_TEMPONLY);
		return (i_dladm_get_flowprop_db(handle, flow, prop_name,
		    prop_val, val_cntp));
	}

	status = i_dladm_get_prop_temp(handle, flow, type, prop_name,
	    prop_val, val_cntp, &prop_tbl);
	if (status != DLADM_STATUS_NOTFOUND)
		return (status);

	return (DLADM_STATUS_BADARG);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_set_flowprop_db(dladm_handle_t handle, const char *flow,
    const char *prop_name, char **prop_val, uint_t val_cnt, const char *root)
{
	dladm_status_t	status;
	uint64_t	a;

	status = dladm_read_flowconf(handle, flow, NULL);
	if (status != DLADM_STATUS_OK)
		return (status);

	if (prop_name == NULL) {
		int i;

		for (i = 0; i < DLADM_MAX_FLOWPROPS; i++) {
			if (strcmp(prop_table[i].pd_name, FMAXBW) == 0)
				a = MRP_MAXBW_RESETVAL;

			status = dladm_set_flowconf_field(handle, flow,
			    prop_table[i].pd_name, DLADM_TYPE_UINT64, &a);
			if (status != 0)
				return (status);
		}
		return (dladm_write_flowconf(handle, flow, root));
	} else if (strcmp(prop_name, FMAXBW) == 0) {
		if (prop_val != NULL)
			(void) dladm_str2bw(*prop_val, &a);
		else
			a = MRP_MAXBW_RESETVAL;
	} else if (strcmp(prop_name, FPRIORITY) == 0) {
		if (prop_val != NULL) {
			mac_priority_level_t	prio;

			(void) dladm_str2pri(*prop_val, &prio);
			a = prio;
		} else {
			a = MPL_RESET;
		}
	} else {
		return (DLADM_STATUS_BADARG);
	}

	status = dladm_set_flowconf_field(handle, flow, prop_name,
	    DLADM_TYPE_UINT64, &a);
	return (status == DLADM_STATUS_OK ?
	    dladm_write_flowconf(handle, flow, root) : status);
}

static dladm_status_t
i_dladm_get_flowprop_db(dladm_handle_t handle, const char *flow,
    const char *prop_name, char **prop_val, uint_t *val_cntp)
{
	dladm_status_t	status;
	uint64_t	attrval;
	char		zonename[ZONENAME_MAX], flowname[MAXFLOWNAMELEN];
	zoneid_t	zoneid;

	zoneid = dladm_extra_names(flow, flowname, zonename);

	status = dladm_read_flowconf(handle, flow, NULL);
	if (status != DLADM_STATUS_OK)
		return (status);

	if (prop_name == NULL) {
		int	i;

		for (i = 0; i < DLADM_MAX_FLOWPROPS; i++) {
			status = dladm_get_flowconf_field(handle, flowname,
			    prop_table[i].pd_defval.vd_name, zoneid,
			    &attrval, sizeof (uint64_t));
			if (status != 0)
				break;
		}
		return (status);
	}

	status = dladm_get_flowconf_field(handle, flowname, prop_name, zoneid,
	    &attrval, sizeof (uint64_t));
	if (status != DLADM_STATUS_OK)
		return (status);

	if (strcmp(prop_name, FMAXBW) == 0)
		(void) dladm_bw2str(attrval, *prop_val);
	else if (strcmp(prop_name, FPRIORITY) == 0)
		(void) dladm_pri2str(attrval, *prop_val);
	*val_cntp = 1;
	return (status);
}

#define	MIN_INFO_SIZE (4 * 1024)

dladm_status_t
dladm_flow_info(dladm_handle_t handle, const char *flow,
    dladm_flow_attr_t *attr)
{
	dld_ioc_walkflow_t	*ioc;
	int			bufsize;
	dld_flowinfo_t		*flowinfo;
	char			fname[MAXFLOWNAMELEN], zonename[ZONENAME_MAX];
	zoneid_t		zoneid = getzoneid(), inputzoneid;

	if ((flow == NULL) || (attr == NULL))
		return (DLADM_STATUS_BADARG);

	inputzoneid = dladm_extra_names(flow, fname, zonename);
	if (zoneid != GLOBAL_ZONEID && zoneid != inputzoneid &&
	    strcmp(zonename, "global") != 0)
		return (DLADM_STATUS_FLOW_WRONG_ZONE);

	bufsize = MIN_INFO_SIZE;
	if ((ioc = calloc(1, bufsize)) == NULL)
		return (dladm_errno2status(errno));

	(void) strlcpy(ioc->wf_name, flow, sizeof (ioc->wf_name));
	ioc->wf_len = bufsize - sizeof (*ioc);
	while (ioctl(dladm_dld_fd(handle), DLDIOC_WALKFLOW, ioc) < 0) {
		if (errno == ENOSPC) {
			bufsize *= 2;
			ioc = realloc(ioc, bufsize);
			if (ioc != NULL) {
				(void) strlcpy(ioc->wf_name, flow,
				    MAXFLOWNAMELEN);
				ioc->wf_len = bufsize - sizeof (*ioc);
				continue;
			}
		}
		free(ioc);
		return (dladm_errno2status(errno));
	}

	bzero(attr, sizeof (*attr));

	flowinfo = (dld_flowinfo_t *)(void *)(ioc + 1);

	attr->fa_linkid = flowinfo->fi_linkid;
	bcopy(flowinfo->fi_flowname, &attr->fa_flowname,
	    sizeof (attr->fa_flowname));
	bcopy(&flowinfo->fi_flow_desc, &attr->fa_flow_desc,
	    sizeof (attr->fa_flow_desc));
	bcopy(&flowinfo->fi_resource_props, &attr->fa_resource_props,
	    sizeof (attr->fa_resource_props));
	free(ioc);
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
do_get_maxbw(dladm_handle_t handle, const char *flow, char **prop_val,
    uint_t *val_cnt)
{
	mac_resource_props_t	*mrp;
	char 			buf[DLADM_STRSIZE];
	dladm_flow_attr_t	fa;
	dladm_status_t		status;

	status = dladm_flow_info(handle, flow, &fa);
	if (status != DLADM_STATUS_OK)
		return (status);
	mrp = &(fa.fa_resource_props);

	*val_cnt = 1;
	if (mrp->mrp_mask & MRP_MAXBW) {
		(void) snprintf(prop_val[0], DLADM_STRSIZE, "%s",
		    dladm_bw2str(mrp->mrp_maxbw, buf));
	} else {
		return (DLADM_STATUS_NOTSUP);
	}
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
do_set_maxbw(dladm_handle_t handle, const char *flow, val_desc_t *vdp,
    uint_t val_cnt)
{
	dld_ioc_modifyflow_t	attr;
	mac_resource_props_t	mrp;
	void			*val;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	bzero(&mrp, sizeof (mrp));
	if (vdp != NULL && (val = (void *)vdp->vd_val) != NULL) {
		bcopy(val, &mrp.mrp_maxbw, sizeof (int64_t));
		free(val);
	} else {
		mrp.mrp_maxbw = MRP_MAXBW_RESETVAL;
	}
	mrp.mrp_mask = MRP_MAXBW;

	bzero(&attr, sizeof (attr));
	(void) strlcpy(attr.mf_name, flow, sizeof (attr.mf_name));
	bcopy(&mrp, &attr.mf_resource_props, sizeof (mac_resource_props_t));
	attr.mf_zoneid = getzoneid();

	if (ioctl(dladm_dld_fd(handle), DLDIOC_MODIFYFLOW, &attr) < 0)
		return (dladm_errno2status(errno));

	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
do_check_maxbw(fprop_desc_t *pdp, char **prop_val, uint_t val_cnt,
    val_desc_t **vdpp)
{
	uint64_t	*maxbw;
	val_desc_t	*vdp = NULL;
	dladm_status_t	status = DLADM_STATUS_OK;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	maxbw = malloc(sizeof (uint64_t));
	if (maxbw == NULL)
		return (DLADM_STATUS_NOMEM);

	status = dladm_str2bw(*prop_val, maxbw);
	if (status != DLADM_STATUS_OK) {
		free(maxbw);
		return (status);
	}

	if ((*maxbw < MRP_MAXBW_MINVAL) && (*maxbw != 0)) {
		free(maxbw);
		return (DLADM_STATUS_MINMAXBW);
	}

	vdp = malloc(sizeof (val_desc_t));
	if (vdp == NULL) {
		free(maxbw);
		return (DLADM_STATUS_NOMEM);
	}

	vdp->vd_val = (uintptr_t)maxbw;
	*vdpp = vdp;
	return (DLADM_STATUS_OK);
}

static dladm_status_t
flow_proplist_check(dladm_arg_list_t *proplist)
{
	int		i, j;
	boolean_t	matched;

	for (i = 0; i < proplist->al_count; i++) {
		matched = B_FALSE;
		for (j = 0; j < DLADM_MAX_FLOWPROPS; j++) {
			if (strcmp(proplist->al_info[i].ai_name,
			    prop_table[j].pd_name) == 0)
				matched = B_TRUE;
			}
		if (!matched)
			return (DLADM_STATUS_BADPROP);
	}
	return (DLADM_STATUS_OK);

}

dladm_status_t
dladm_parse_flow_props(char *str, dladm_arg_list_t **listp, boolean_t novalues)
{
	dladm_status_t	status;

	status = dladm_parse_args(str, listp, novalues);
	if (status != DLADM_STATUS_OK)
		return (status);

	if (*listp != NULL && (status = flow_proplist_check(*listp)
	    != DLADM_STATUS_OK)) {
		dladm_free_props(*listp);
		return (status);
	}

	return (DLADM_STATUS_OK);
}

/*
 * Retrieve the named property from a proplist, check the value and
 * convert to a kernel structure.
 */
static dladm_status_t
i_dladm_flow_proplist_extract_one(dladm_arg_list_t *proplist,
    const char *name, void *arg)
{
	dladm_status_t		status;
	dladm_arg_info_t	*aip = NULL;
	int			i, j;

	/* Find named property in proplist */
	for (i = 0; i < proplist->al_count; i++) {
		aip = &proplist->al_info[i];
		if (strcasecmp(aip->ai_name, name) == 0)
			break;
	}

	/* Property not in list */
	if (i == proplist->al_count)
		return (DLADM_STATUS_OK);

	for (i = 0; i < DLADM_MAX_FLOWPROPS; i++) {
		fprop_desc_t	*pdp = &prop_table[i];
		val_desc_t	*vdp;

		vdp = malloc(sizeof (val_desc_t) * aip->ai_count);
		if (vdp == NULL)
			return (DLADM_STATUS_NOMEM);

		if (strcasecmp(aip->ai_name, pdp->pd_name) != 0)
			continue;

		if (aip->ai_val == NULL)
			return (DLADM_STATUS_BADARG);

		/* Check property value */
		if (pdp->pd_check != NULL) {
			status = pdp->pd_check(pdp, aip->ai_val,
			    aip->ai_count, &vdp);
		} else {
			status = DLADM_STATUS_BADARG;
		}

		if (status != DLADM_STATUS_OK)
			return (status);

		for (j = 0; j < DLADM_MAX_RSRC_PROP; j++) {
			resource_prop_t	*rpp = &rsrc_prop_table[j];

			if (strcasecmp(aip->ai_name, rpp->rp_name) != 0)
				continue;

			/* Extract kernel structure */
			if (rpp->rp_extract != NULL) {
				status = rpp->rp_extract(vdp,
				    aip->ai_count, arg);
			} else {
				status = DLADM_STATUS_BADARG;
			}
			break;
		}

		if (status != DLADM_STATUS_OK)
			return (status);

		break;
	}
	return (status);
}

/*
 * Extract properties from a proplist and convert to mac_resource_props_t.
 */
dladm_status_t
dladm_flow_proplist_extract(dladm_arg_list_t *proplist,
    mac_resource_props_t *mrp)
{
	dladm_status_t	status = DLADM_STATUS_OK;

	status = i_dladm_flow_proplist_extract_one(proplist, "maxbw", mrp);
	if (status != DLADM_STATUS_OK)
		return (status);
	return (status);
}

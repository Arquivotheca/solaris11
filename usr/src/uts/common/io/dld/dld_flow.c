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

/*
 * Flows ioctls implementation.
 */

#include <sys/cred.h>
#include <sys/dld.h>
#include <sys/mac_provider.h>
#include <sys/mac_client.h>
#include <sys/mac_client_priv.h>

/*
 * Implements flow add, remove, modify ioctls.
 */
int
dld_add_flow(datalink_id_t linkid, char *flow_name, flow_desc_t *flow_desc,
    mac_resource_props_t *mrp)
{
	return (mac_link_flow_add(linkid, flow_name, flow_desc, mrp));
}

int
dld_remove_flow(char *flow_name, zoneid_t zoneid)
{
	return (mac_link_flow_remove(flow_name, zoneid));
}

int
dld_modify_flow(char *flow_name, mac_resource_props_t *mrp, zoneid_t zoneid)
{
	return (mac_link_flow_modify(flow_name, mrp, zoneid));
}

int
dld_create_kstat(char *flow_name, zoneid_t zoneid)
{
	return (mac_link_flow_createkstat(flow_name, zoneid));
}

/*
 * Callback function and structure used by dld_walk_flow().
 */
typedef struct flowinfo_state_s {
	int			fi_bufsize;
	int			fi_nflows;
	uchar_t			*fi_fl;
} flowinfo_state_t;

static int
dld_walk_flow_cb(mac_flowinfo_t *finfo, void *arg)
{
	flowinfo_state_t		*statep = arg;
	dld_flowinfo_t			*fi;

	if (statep->fi_bufsize < sizeof (dld_flowinfo_t))
		return (ENOSPC);

	fi = kmem_zalloc(sizeof (*fi), KM_SLEEP);
	(void) strlcpy(fi->fi_flowname, finfo->fi_flow_name,
	    sizeof (fi->fi_flowname));
	fi->fi_linkid = finfo->fi_link_id;
	fi->fi_flow_desc = finfo->fi_flow_desc;
	fi->fi_resource_props = finfo->fi_resource_props;

	if (copyout(fi, statep->fi_fl, sizeof (*fi)) != 0) {
		kmem_free(fi, sizeof (*fi));
		return (EFAULT);
	}
	kmem_free(fi, sizeof (*fi));
	statep->fi_nflows++;
	statep->fi_bufsize -= sizeof (dld_flowinfo_t);
	statep->fi_fl += sizeof (dld_flowinfo_t);
	return (0);
}

/*
 * Implements flow walk ioctl.
 * Retrieves a specific flow or a list of flows from the specified link.
 * ENOSPC is returned a bigger buffer is needed.
 */
int
dld_walk_flow(dld_ioc_walkflow_t *wf, intptr_t uaddr, cred_t *credp)
{
	flowinfo_state_t	state;
	mac_flowinfo_t		*finfo;
	int			err = 0;
	zoneid_t		zoneid = crgetzoneid(credp);

	finfo = kmem_zalloc(sizeof (*finfo), KM_SLEEP);
	state.fi_bufsize = wf->wf_len;
	state.fi_fl = (uchar_t *)uaddr + sizeof (*wf);
	state.fi_nflows = 0;

	if (wf->wf_name[0] == '\0') {
		err = mac_link_flow_walk(wf->wf_linkid, dld_walk_flow_cb,
		    &state);
	} else {
		err = mac_link_flow_info(wf->wf_name, finfo, zoneid);
		if (err != 0) {
			kmem_free(finfo, sizeof (*finfo));
			return (err);
		}
		err = dld_walk_flow_cb(finfo, &state);
	}
	kmem_free(finfo, sizeof (*finfo));
	wf->wf_nflows = state.fi_nflows;
	return (err);
}

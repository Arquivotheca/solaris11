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

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ethernet.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/dld_ioc.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stropts.h>
#include <stdlib.h>
#include <errno.h>
#include <strings.h>
#include <libintl.h>
#include <netdb.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <inet/ip.h>
#include <inet/ip6.h>
#include <libdlflow.h>
#include <libdlflow_impl.h>
#include <libdladm_impl.h>
#include <libdllink.h>
#include <zone.h>

/* minimum buffer size for DLDIOCWALKFLOW */
#define	MIN_INFO_SIZE	(4 * 1024)

#define	DLADM_FLOW_DB		"/etc/dladm/datalink.conf"
#define	DLADM_FLOW_DB_TMP	"/etc/dladm/datalink.conf.new"
#define	DLADM_FLOW_DB_LOCK	"/tmp/datalink.conf.lock"

#define	DLADM_FLOW_DB_PERMS	S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
#define	DLADM_FLOW_DB_OWNER	UID_DLADM
#define	DLADM_FLOW_DB_GROUP	GID_NETADM

#define	BLANK_LINE(s)	((s[0] == '\0') || (s[0] == '#') || (s[0] == '\n'))
#define	MAXLINELEN	1024
#define	MAXPATHLEN	1024

#define	FPRINTF_ERR(fcall) if ((fcall) < 0) return (-1);

/* Remove a flow in the DB */
static int
i_dladm_flow_remove_db(dladm_handle_t handle, const char *name,
    const char *root)
{
	if (dladm_remove_flowconf(handle, name, root) != DLADM_STATUS_OK)
		return (-1);

	return (0);
}

static char *
dladm_ipaddr_to_str(flow_desc_t *flow_desc, boolean_t localaddr)
{
	char abuf[INET6_ADDRSTRLEN], *ap;
	static char ret_str[INET6_ADDRSTRLEN + 4];
	/* add 4 byte to store '/' and the net mask */
	struct in_addr ipaddr;
	int prefix_len, prefix_max;

	if (flow_desc->fd_ipversion != 6) {
		if (localaddr)
			ipaddr.s_addr =
			    flow_desc->fd_local_addr._S6_un._S6_u32[3];
		else
			ipaddr.s_addr =
			    flow_desc->fd_remote_addr._S6_un._S6_u32[3];

		ap = inet_ntoa(ipaddr);
		prefix_max = IP_ABITS;
	} else {
		if (localaddr)
			(void) inet_ntop(AF_INET6, &flow_desc->fd_local_addr,
			    abuf, INET6_ADDRSTRLEN);
		else
			(void) inet_ntop(AF_INET6, &flow_desc->fd_remote_addr,
			    abuf, INET6_ADDRSTRLEN);

		ap = abuf;
		prefix_max = IPV6_ABITS;
	}

	if (localaddr)
		(void) dladm_mask2prefixlen(&flow_desc->fd_local_netmask,
		    prefix_max, &prefix_len);
	else
		(void) dladm_mask2prefixlen(&flow_desc->fd_remote_netmask,
		    prefix_max, &prefix_len);
	(void) snprintf(ret_str, sizeof (ret_str), "%s/%d", ap,
	    prefix_len);
	return (ret_str);
}

/* Add flow to the DB */
static dladm_status_t
i_dladm_flow_create_db(dladm_handle_t handle, const char *name,
    dld_flowinfo_t *attrp, const char *root)
{
	dladm_status_t status;
	char linkover[MAXLINKNAMELEN];

	if ((status = dladm_create_flowconf(handle, name, attrp->fi_linkid))
	    != DLADM_STATUS_OK)
		return (status);

	/* set link name over which the flow resides */
	if (attrp->fi_linkid != DATALINK_INVALID_LINKID) {
		status = dladm_datalink_id2info(handle, attrp->fi_linkid, NULL,
		    NULL, NULL, linkover, sizeof (linkover));
		if (status != DLADM_STATUS_OK)
			goto done;
		status = dladm_set_flowconf_field(handle, name, FLINKOVER,
		    DLADM_TYPE_STR, linkover);
		if (status != DLADM_STATUS_OK)
			goto done;
	}

	/* flow policy */
	if (attrp->fi_resource_props.mrp_mask & MRP_MAXBW) {
		uint64_t maxbw = attrp->fi_resource_props.mrp_maxbw;
		status = dladm_set_flowconf_field(handle, name, FMAXBW,
		    DLADM_TYPE_UINT64, &maxbw);
		if (status != DLADM_STATUS_OK)
			goto done;
	}

	if (attrp->fi_resource_props.mrp_mask & MRP_PRIORITY) {
		uint64_t prio = attrp->fi_resource_props.mrp_priority;
		status = dladm_set_flowconf_field(handle, name, FPRIORITY,
		    DLADM_TYPE_UINT64, &prio);
		if (status != DLADM_STATUS_OK)
			goto done;
	}

	/* flow descriptor */
	if (attrp->fi_flow_desc.fd_mask & FLOW_IP_DSFIELD) {
		status = dladm_set_flowconf_field(handle, name, FDSFIELD,
		    DLADM_TYPE_UINT64, &attrp->fi_flow_desc.fd_dsfield);
		if (status != DLADM_STATUS_OK)
			goto done;

		status = dladm_set_flowconf_field(handle, name, FDSFIELD_MASK,
		    DLADM_TYPE_UINT64, &attrp->fi_flow_desc.fd_dsfield_mask);
		if (status != DLADM_STATUS_OK)
			goto done;
	}

	if (attrp->fi_flow_desc.fd_mask & FLOW_IP_LOCAL) {
		char *addr_str;

		addr_str = dladm_ipaddr_to_str(&attrp->fi_flow_desc, B_TRUE);
		status = dladm_set_flowconf_field(handle, name, FLOCAL_IP_ADDR,
		    DLADM_TYPE_STR, addr_str);
		if (status != DLADM_STATUS_OK)
			goto done;
	}

	if (attrp->fi_flow_desc.fd_mask & FLOW_IP_REMOTE) {
		/* add 4 byte to store '/' and the net mask */
		char *addr_str;

		addr_str = dladm_ipaddr_to_str(&attrp->fi_flow_desc, B_FALSE);
		status = dladm_set_flowconf_field(handle, name, FREMOTE_IP_ADDR,
		    DLADM_TYPE_STR, addr_str);
		if (status != DLADM_STATUS_OK)
			goto done;
	}

	if (attrp->fi_flow_desc.fd_mask & FLOW_IP_PROTOCOL) {
		uint64_t prot = attrp->fi_flow_desc.fd_protocol;

		status = dladm_set_flowconf_field(handle, name, FTRANSPORT,
		    DLADM_TYPE_UINT64, &prot);
		if (status != DLADM_STATUS_OK)
			goto done;
	}

	if (attrp->fi_flow_desc.fd_mask & FLOW_ULP_PORT_LOCAL) {
		uint64_t lport = attrp->fi_flow_desc.fd_local_port;

		status = dladm_set_flowconf_field(handle, name, FLOCAL_PORT,
		    DLADM_TYPE_UINT64, &lport);
		if (status != DLADM_STATUS_OK)
			goto done;

	}

	if (attrp->fi_flow_desc.fd_mask & FLOW_ULP_PORT_REMOTE) {
		uint64_t rport = attrp->fi_flow_desc.fd_remote_port;

		status = dladm_set_flowconf_field(handle, name, FREMOTE_PORT,
		    DLADM_TYPE_UINT64, &rport);
		if (status != DLADM_STATUS_OK)
			goto done;
	}

	/*
	 * Commit the flow configuration.
	 */
	status = dladm_write_flowconf(handle, name, root);
done:
	return (status);
}

/* Add flow to kernel */
static dladm_status_t
i_dladm_flow_add(dladm_handle_t handle, char *flowname, datalink_id_t linkid,
    flow_desc_t *flowdesc, mac_resource_props_t *mrp)
{
	dld_ioc_addflow_t	attr;

	/* create flow */
	bzero(&attr, sizeof (attr));
	bcopy(flowdesc, &attr.af_flow_desc, sizeof (flow_desc_t));
	if (mrp != NULL) {
		bcopy(mrp, &attr.af_resource_props,
		    sizeof (mac_resource_props_t));
	}

	(void) strlcpy(attr.af_name, flowname, sizeof (attr.af_name));
	attr.af_linkid = linkid;

	if (ioctl(dladm_dld_fd(handle), DLDIOC_ADDFLOW, &attr) < 0)
		return (dladm_errno2status(errno));

	return (DLADM_STATUS_OK);
}

/* Remove flow from kernel */
static dladm_status_t
i_dladm_flow_remove(dladm_handle_t handle, char *flowname, zoneid_t zoneid)
{
	dld_ioc_removeflow_t	attr;
	dladm_status_t		status = DLADM_STATUS_OK;

	(void) strlcpy(attr.rf_name, flowname, sizeof (attr.rf_name));
	attr.rf_zoneid = zoneid;

	if (ioctl(dladm_dld_fd(handle), DLDIOC_REMOVEFLOW, &attr) < 0)
		status = dladm_errno2status(errno);

	return (status);
}

dladm_status_t
dladm_flow_create_ngz_kstat(dladm_handle_t handle, char *flowname,
    zoneid_t zoneid)
{
	dld_ioc_createkstat_t	attr;
	dladm_status_t		status = DLADM_STATUS_OK;

	(void) strlcpy(attr.ck_flow, flowname, sizeof (attr.ck_flow));
	attr.ck_zoneid = zoneid;

	if (ioctl(dladm_dld_fd(handle), DLDIOC_CREATEKSTAT, &attr) < 0)
		status = dladm_errno2status(errno);
	return (status);
}

/* ARGSUSED */
dladm_status_t
dladm_flow_add(dladm_handle_t handle, datalink_id_t linkid,
    dladm_arg_list_t *attrlist, dladm_arg_list_t *proplist, char *flowname,
    boolean_t tempop, const char *root)
{
	dld_flowinfo_t		db_attr;
	flow_desc_t		flowdesc;
	mac_resource_props_t	mrp;
	dladm_status_t		status;
	zoneid_t		zoneid = getzoneid();

	/* Extract flow attributes from attrlist */
	bzero(&flowdesc, sizeof (flow_desc_t));
	if (attrlist != NULL && (status = dladm_flow_attrlist_extract(attrlist,
	    &flowdesc)) != DLADM_STATUS_OK)
		return (status);

	/* Extract resource_ctl and cpu_list from proplist */
	bzero(&mrp, sizeof (mac_resource_props_t));
	if (proplist != NULL && (status = dladm_flow_proplist_extract(proplist,
	    &mrp)) != DLADM_STATUS_OK)
		return (status);

	flowdesc.fd_zoneid = zoneid;
	/* Add flow in kernel */
	status = i_dladm_flow_add(handle, flowname, linkid, &flowdesc, &mrp);
	if (status != DLADM_STATUS_OK || tempop)
		return (status);

	/* Add flow to DB */
	bzero(&db_attr, sizeof (db_attr));
	bcopy(&flowdesc, &db_attr.fi_flow_desc, sizeof (flow_desc_t));
	bcopy(&mrp, &db_attr.fi_resource_props, sizeof (mac_resource_props_t));
	(void) strlcpy(db_attr.fi_flowname, flowname,
	    sizeof (db_attr.fi_flowname));
	db_attr.fi_linkid = linkid;

	if ((status = i_dladm_flow_create_db(handle, flowname, &db_attr, root))
	    != DLADM_STATUS_OK) {
		/* if write to DB failed, remove flow from kernel */
		(void) i_dladm_flow_remove(handle, flowname, zoneid);
		return (status);
	}

done:
	return (status);
}

/*
 * Remove a flow.
 */
/* ARGSUSED */
dladm_status_t
dladm_flow_remove(dladm_handle_t handle, char *flowname, boolean_t tempop,
    const char *root)
{
	dladm_status_t	status = DLADM_STATUS_OK;
	dladm_status_t	s = DLADM_STATUS_OK;
	char		fname[MAXFLOWNAMELEN], zonename[ZONENAME_MAX];
	zoneid_t	zoneid = getzoneid(), inputzoneid;
	boolean_t	onloan = B_FALSE;

	/* Check if this zone owns this flow or not */
	inputzoneid = dladm_extra_names(flowname, fname, zonename);
	if (zoneid != inputzoneid)
		return (DLADM_STATUS_FLOW_WRONG_ZONE);

	/* Before remove, check if the flow is onloan to another zone */
	status = dladm_read_flowconf(handle, fname, &onloan);
	if (status == DLADM_STATUS_OK && onloan)
		return (DLADM_STATUS_FLOW_WRONG_ZONE);


	/* Remove flow from kernel */
	status = i_dladm_flow_remove(handle, fname, zoneid);
	if (tempop)
		return (status);

	/* Remove flow from DB */
	if (!tempop) {
		/* flow DB */
		if (i_dladm_flow_remove_db(handle, fname, root) < 0)
			s = dladm_errno2status(errno);
	}

done:
	if (!tempop) {
		if (s == DLADM_STATUS_OK) {
			if (status == DLADM_STATUS_NOTFOUND)
				status = s;
		} else {
			if (s != DLADM_STATUS_NOTFOUND)
				status = s;
		}
	}
	return (status);
}

/*
 * Get an existing flow in the DB.
 */

typedef struct get_db_state {
	int		(*gs_fn)(dladm_handle_t, dladm_flow_attr_t *, void *);
	void		*gs_arg;
	datalink_id_t	gs_linkid;
} get_db_state_t;

static int
dladm_get_zonelist(zoneid_t **zoneids, int *zonenum)
{
	zoneid_t	*zids = NULL;
	uint_t		nzids, nzids_saved;

	if (zone_list(NULL, &nzids) != 0)
		return (errno);
again:
	nzids_saved = nzids;
	if ((zids = malloc(nzids * sizeof (zoneid_t))) == NULL)
		return (errno);
	if (zone_list(zids, &nzids) != 0) {
		free(zids);
		return (errno);
	}

	if (nzids > nzids_saved) {
		free(zids);
		goto again;
	}

	*zoneids = zids;
	*zonenum = nzids;
	return (0);
}

/*
 * Walk through the flows defined on the system and for each flow
 * invoke <fn>(<arg>, <flow>);
 * Currently used for show-flow.
 */
/* ARGSUSED */
dladm_status_t
dladm_walk_flow(int (*fn)(dladm_handle_t, dladm_flow_attr_t *, void *),
    dladm_handle_t handle, datalink_id_t linkid, void *arg, boolean_t persist)
{
	dld_flowinfo_t		*flow;
	int			i, bufsize;
	dld_ioc_walkflow_t	*ioc = NULL;
	dladm_flow_attr_t 	attr;
	dladm_status_t		status = DLADM_STATUS_OK;
	zoneid_t		cur_zoneid = getzoneid();
	char			zonename[ZONENAME_MAX];
	ssize_t			s;
	boolean_t		onloan = B_FALSE;

	if (fn == NULL)
		return (DLADM_STATUS_BADARG);

	if (persist) {
		dladm_status_t	status;
		dld_flowinfo_t	flowinfo;
		int		ret_err = 0, numzone = 0, zindex = 0;
		char		flowname[MAXFLOWNAMELEN];
		zoneid_t	*zids = NULL, next_zid = cur_zoneid;

		bzero(flowname, MAXFLOWNAMELEN);
		if (cur_zoneid == GLOBAL_ZONEID)
			if (dladm_get_zonelist(&zids, &numzone) != 0)
				return (DLADM_STATUS_ZONE_ERR);

		while (ret_err != ENOENT) {
			s = getzonenamebyid(next_zid, zonename, ZONENAME_MAX);
			/*
			 * Global zone needs to walks through all zones, if the
			 * zone that has been walked is not activate, go to
			 * the next one.
			 */
			if (s < 0 && cur_zoneid == GLOBAL_ZONEID) {
				next_zid = zids[++zindex];
				if (zindex > numzone - 1)
					break;
				continue;
			}

			bzero(&flowinfo, sizeof (flowinfo));
			status = dladm_get_flownext(handle, flowname, next_zid,
			    &flowinfo, &ret_err);
			if (status != DLADM_STATUS_OK &&
			    cur_zoneid != GLOBAL_ZONEID)
					return (status);
			if (status == DLADM_STATUS_OK) {
				(void) strlcpy(flowname, flowinfo.fi_flowname,
				    MAXFLOWNAMELEN);
				if (flowinfo.fi_linkid != linkid &&
				    flowinfo.fi_linkid !=
				    DATALINK_INVALID_LINKID)
					continue;
			} else if (status == DLADM_STATUS_NOTFOUND &&
			    cur_zoneid == GLOBAL_ZONEID) {
				next_zid = zids[++zindex];
				if (zindex > numzone - 1)
					break;
				bzero(flowname, sizeof (flowname));
				continue;
			}

			bzero(&attr, sizeof (attr));
			if (next_zid != cur_zoneid) {
				(void) snprintf(attr.fa_flowname,
				    sizeof (attr.fa_flowname), "%s/%s",
				    zonename, flowinfo.fi_flowname);
			} else {
				/* ngz, onloan flow shows as global/flowname */
				status = dladm_read_flowconf(handle, flowname,
				    &onloan);
				if (status != DLADM_STATUS_OK)
					continue;
				if (onloan && cur_zoneid != GLOBAL_ZONEID)
					(void) snprintf(attr.fa_flowname,
					    sizeof (attr.fa_flowname), "%s/%s",
					    "global", flowinfo.fi_flowname);
				else
					bcopy(flowinfo.fi_flowname,
					    &attr.fa_flowname,
					    sizeof (attr.fa_flowname));
			}

			attr.fa_linkid = flowinfo.fi_linkid;
			bcopy(&flowinfo.fi_flow_desc, &attr.fa_flow_desc,
			    sizeof (attr.fa_flow_desc));

			bcopy(&flowinfo.fi_resource_props,
			    &attr.fa_resource_props,
			    sizeof (attr.fa_resource_props));
			if (fn(handle, &attr, arg) == DLADM_WALK_TERMINATE)
				break;
		}
	} else {
		bufsize = MIN_INFO_SIZE;
		if ((ioc = calloc(1, bufsize)) == NULL) {
			status = dladm_errno2status(errno);
			return (status);
		}

		ioc->wf_linkid = linkid;
		ioc->wf_len = bufsize - sizeof (*ioc);

		while (ioctl(dladm_dld_fd(handle), DLDIOC_WALKFLOW, ioc) < 0) {
			if (errno == ENOSPC) {
				bufsize *= 2;
				ioc = realloc(ioc, bufsize);
				if (ioc != NULL) {
					ioc->wf_linkid = linkid;
					ioc->wf_len = bufsize - sizeof (*ioc);
					continue;
				}
			}
			goto bail;
		}

		flow = (dld_flowinfo_t *)(void *)(ioc + 1);
		for (i = 0; i < ioc->wf_nflows; i++, flow++) {
			zoneid_t	zid;

			zid = flow->fi_flow_desc.fd_zoneid;
			bzero(&attr, sizeof (attr));

			(void) dladm_read_flowconf(handle, flow->fi_flowname,
			    &onloan);
			if (zid != cur_zoneid) {
				char	zonename[ZONENAME_MAX];
				ssize_t	s = 0;

				if (zid == GLOBAL_ZONEID)
					(void) strlcpy(zonename, "global",
					    ZONENAME_MAX);
				else
					s = getzonenamebyid(zid, zonename,
					    ZONENAME_MAX);
				if (!onloan && s < 0)
					continue;
				(void) snprintf(attr.fa_flowname,
				    sizeof (attr.fa_flowname), "%s/%s",
				    zonename, flow->fi_flowname);
			} else {
				bcopy(&flow->fi_flowname, &attr.fa_flowname,
				    sizeof (attr.fa_flowname));
			}
			attr.fa_linkid = flow->fi_linkid;
			bcopy(&flow->fi_flow_desc, &attr.fa_flow_desc,
			    sizeof (attr.fa_flow_desc));
			bcopy(&flow->fi_resource_props, &attr.fa_resource_props,
			    sizeof (attr.fa_resource_props));
			if (fn(handle, &attr, arg) == DLADM_WALK_TERMINATE)
				break;
		}
	}

bail:
	free(ioc);
	return (status);
}

dladm_status_t
dladm_flow_init(dladm_handle_t handle)
{
	dladm_status_t		status = DLADM_STATUS_OK;
	dld_flowinfo_t		flowinfo;
	int			ret_err = 0;
	char			flowname[MAXFLOWNAMELEN];
	zoneid_t		zoneid = getzoneid();

	bzero(flowname, sizeof (flowname));
	bzero(&flowinfo, sizeof (flowinfo));
	while (ret_err == 0) {
		bzero(&flowinfo, sizeof (flowinfo));
		status = dladm_get_flownext(handle, flowname, zoneid, &flowinfo,
		    &ret_err);
		if (status != DLADM_STATUS_OK) {
			return (status == DLADM_STATUS_NOTFOUND ?
			    DLADM_STATUS_OK : status);
		}
		if (flowinfo.fi_flow_desc.fd_zoneid != zoneid)
			return (DLADM_STATUS_OK);

		status = i_dladm_flow_add(handle, flowinfo.fi_flowname,
		    flowinfo.fi_linkid, &flowinfo.fi_flow_desc,
		    &flowinfo.fi_resource_props);
		if (status != DLADM_STATUS_OK)
			return (status);
		(void) strlcpy(flowname, flowinfo.fi_flowname, MAXFLOWNAMELEN);
	}
	return (status);
}

dladm_status_t
dladm_prefixlen2mask(int prefixlen, int maxlen, uchar_t *mask)
{
	if (prefixlen < 0 || prefixlen > maxlen)
		return (DLADM_STATUS_BADARG);

	while (prefixlen > 0) {
		if (prefixlen >= 8) {
			*mask++ = 0xFF;
			prefixlen -= 8;
			continue;
		}
		*mask |= 1 << (8 - prefixlen);
		prefixlen--;
	}
	return (DLADM_STATUS_OK);
}

dladm_status_t
dladm_mask2prefixlen(in6_addr_t *mask, int plen, int *prefixlen)
{
	int		bits;
	int		i, end;

	switch (plen) {
	case IP_ABITS:
		end = 3;
		break;
	case IPV6_ABITS:
		end = 0;
		break;
	default:
		return (DLADM_STATUS_BADARG);
	}

	for (i = 3; i >= end; i--) {
		if (mask->_S6_un._S6_u32[i] == 0) {
			plen -= 32;
			continue;
		}
		bits = ffs(ntohl(mask->_S6_un._S6_u32[i])) - 1;
		if (bits == 0)
			break;
		plen -= bits;
	}
	*prefixlen = plen;
	return (DLADM_STATUS_OK);
}

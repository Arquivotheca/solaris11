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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/mach_descrip.h>
#include <sys/ethernet.h>
#include <sys/vlan.h>
#include <sys/mac.h>
#include <sys/mac_client.h>
#include <sys/iovcfg.h>
#include <sys/iovcfg_net.h>
#include <sys/iovcfg_4v.h>

/* Exported functions */
void iovcfg_reconfig_reg_pf_net(iov_pf_t *pfp);
int iovcfg_read_props_net(iov_vf_t *vfp, void *arg2, void *arg3);
int iovcfg_mdeg_cb_net(void *cb_argp,  mdeg_result_t *resp);
#ifdef IOVCFG_UNCONFIG_SUPPORTED
void iovcfg_reconfig_unreg_pf_net(iov_pf_t *pfp);
#endif

/* Internal functions */
static int iovcfg_vf_update_net(iov_vf_t *vfp, md_t *mdp, mde_cookie_t node);

static char pri_macaddr_propname[] = "primary-mac-addr";
static char pvid_propname[] = "port-vlan-id";
static char vid_propname[] = "vlan-id";
static char bandwidth_propname[] = "bandwidth";
static char alt_macaddr_propname[] = "alt-mac-addrs";
static char mtu_propname[] = "mtu";

/*
 * Register mdeg callbacks for all VFs under the given PF.
 */
void
iovcfg_reconfig_reg_pf_net(iov_pf_t *pfp)
{
	int		rv;
	iov_vf_t	*vfp;

	if (pfp == NULL)
		return;
	for (vfp = pfp->ipf_vfp; vfp != NULL; vfp = vfp->ivf_nextp) {
		rv = iovcfg_mdeg_register(vfp, iovcfg_mdeg_cb_net);
		if (rv != 0) {
			DBGNET("Failed to register MDEB cb for VF(%s)"
			    " err = 0x%x\n", vfp->ivf_pathname, rv);
		}
	}
}

/*
 * Read class specific properties of the VF from its class props MD node.
 */
int
iovcfg_read_props_net(iov_vf_t *vfp, void *arg2, void *arg3)
{
	int			i;
	int			j;
	int			rv;
	int			size;
	uint_t			nvids;
	uint64_t		*data;
	uint8_t			*addrp;
	int			addrsz;
	uint64_t		val;
	struct ether_addr	ea;
	iov_vlan_t		*vidp;
	md_t			*mdp = (md_t *)arg2;
	mde_cookie_t		node = (mde_cookie_t)arg3;
	iov_vf_net_t		*vnp;

	if ((vfp == NULL) || (arg2 == NULL))
		return (-1);
	vnp = (iov_vf_net_t *)vfp->ivf_cl_data;
	/* Read primary macaddr */
	rv = md_get_prop_val(mdp, node, pri_macaddr_propname, &val);
	if (rv != 0) {
		return (rv);
	}
	for (i = ETHERADDRL - 1; i >= 0; i--) {
		ea.ether_addr_octet[i] = val & 0xFF;
		val >>= 8;
	}
	ether_copy(&ea, &vnp->ivf_macaddr);

	/* Read alternate macaddrs */
	if (md_get_prop_data(mdp, node, alt_macaddr_propname, &addrp, &addrsz))
		addrsz = 0;
	else
		addrsz /= (sizeof (uint64_t));
	vnp->ivf_num_alt_macaddr = addrsz;
	if (addrsz > 0) {
		vnp->ivf_alt_macaddr = kmem_zalloc(sizeof (ea) * addrsz,
		    KM_SLEEP);
		for (i = 0; i < addrsz; i++) {
			val = *(((uint64_t *)addrp) + i);
			for (j = ETHERADDRL - 1; j >= 0; j--) {
				ea.ether_addr_octet[j]  = val & 0xFF;
				val >>= 8;
			}
			ether_copy(&ea, &vnp->ivf_alt_macaddr[i]);
		}
		iovcfg_alloc_def_alt_table(vfp);
	} else {
		vnp->ivf_alt_macaddr = NULL;
		vnp->ivf_pvid_altp = NULL;
	}

	/* Read PVID */
	if (md_get_prop_val(mdp, node, pvid_propname, &val)) {
		vnp->ivf_pvid = VLAN_ID_NONE;
	} else {
		vnp->ivf_pvid = val & 0xFFF;
		iovcfg_alloc_pvid_alt_table(vfp);
	}

	/* Read VLAN IDs */
	if (md_get_prop_data(mdp, node, vid_propname, (uint8_t **)&data,
	    &size) != 0)
		size = 0;
	else
		size /= sizeof (uint64_t);
	nvids = size;
	if (nvids != 0) {
		vnp->ivf_vids =  kmem_zalloc(sizeof (iov_vlan_t) * nvids,
		    KM_SLEEP);
		for (i = 0; i < nvids; i++) {
			vidp = &vnp->ivf_vids[i];
			vidp->ivl_vid = data[i] & 0xFFFF;
		}
		vnp->ivf_nvids = nvids;
		iovcfg_alloc_vid_alt_table(vfp);
	}

	/* Read bandwidth limit */
	if (md_get_prop_val(mdp, node, bandwidth_propname, &val))
		vnp->ivf_bandwidth = 0;
	else
		vnp->ivf_bandwidth = val;

	/* Read MTU */
	if (md_get_prop_val(mdp, node, mtu_propname, &val) == 0)
		vnp->ivf_mtu = (uint32_t)val;
	else
		vnp->ivf_mtu = IOVCFG_MTU_UNSPECIFIED;
	return (0);
}

/*
 * MDEG callback handler. This function is invoked by the MDEG framework when
 * there are changes in the class specific subnode of a VF iov-device node.
 */
int
iovcfg_mdeg_cb_net(void *cb_argp,  mdeg_result_t *resp)
{
	md_t		*mdp;
	mde_cookie_t	node;
	iov_vf_t	*vfp;

	if ((resp == NULL) || (cb_argp == NULL)) {
		return (MDEG_FAILURE);
	}
	vfp = (iov_vf_t *)cb_argp;
	DBGNET("VF(%s), iov-device-class-props node: removed(%d), added(%d),"
	    " updated(%d)\n", vfp->ivf_pathname, resp->removed.nelem,
	    resp->added.nelem, resp->match_curr.nelem);
	/*
	 * We get an initial callback that the class-props node is 'added' when
	 * we register with MDEG. However, we would have read all the props
	 * before registering with MDEG (see iovcfg_config_cl_net()). But there
	 * is a remote possibility that during this small window some props of
	 * this node might have changed. To take this into account, we handle
	 * this initial added-callback as if an update occured and invoke the
	 * same function (below) which handles updates to the class specific
	 * node. Currently, we don't support dynamic VF removal; so we don't
	 * expect callbacks for node removal. The only callbacks that we expect
	 * are when there are updates (match).
	 */
	if (resp->added.nelem != 0) {
		mdp = resp->added.mdp;
		node = resp->added.mdep[0];
	} else if (resp->match_curr.nelem != 0) {
		mdp = resp->match_curr.mdp;
		node = resp->match_curr.mdep[0];
	} else {
		/* removal invalid */
		return (MDEG_FAILURE);
	}
	(void) iovcfg_vf_update_net(vfp, mdp, node);
	return (MDEG_SUCCESS);
}

/*
 * Process MD update notification for the given VF. In the case of network
 * class, the parameters of interest are - vlan ids and bandwidth assigned to
 * the VF. We determine if any of these parameters have changed and dispatch
 * a task to reconfigure the VF.
 */
static int
iovcfg_vf_update_net(iov_vf_t *vfp, md_t *mdp, mde_cookie_t node)
{
	uint16_t		pvid;
	int			i;
	int			j;
	int			rv;
	int			size;
	int			nvids;
	int			addrsz;
	uint_t			num_alt_macaddr;
	uint32_t		mtu;
	uint8_t			*addrp;
	uint64_t		*data;
	iov_vlan_t		*vidp;
	iov_vlan_t		*vf_vids = NULL;
	iov_net_tsk_arg_t 	*argp;
	struct ether_addr	*alt_macaddr = NULL;
	struct ether_addr	macaddr;
	struct ether_addr	ea;
	uint64_t		val;
	iov_upd_t		updated;
	iov_vf_net_t		*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	updated = IOV_UPD_NONE;

	/*
	 * We get an initial callback as soon as the callback handler is
	 * registered with mdeg. This could happen in the window where the
	 * initial config task kicked off by class config function has not
	 * started running yet. In that case, we simply ignore this update
	 * callback.
	 */
	if (vnp->ivf_mch == NULL || vnp->ivf_bound == B_FALSE) {
		return (0);
	}

	/*
	 * Now start reading all the props that could change. Compare with the
	 * existing ones to determine if there is a change.
	 */

	/* Read primary macaddr */
	rv = md_get_prop_val(mdp, node, pri_macaddr_propname, &val);
	if (rv != 0) {
		return (rv);
	}
	for (i = ETHERADDRL - 1; i >= 0; i--) {
		ea.ether_addr_octet[i] = val & 0xFF;
		val >>= 8;
	}
	/* Primary macaddr change? */
	if (ether_cmp(&ea, &vnp->ivf_macaddr)) {
		ether_copy(&ea, &macaddr);
		updated |= IOV_UPD_MACADDR;
	}

	/* Read alternate macaddrs */
	if (md_get_prop_data(mdp, node, alt_macaddr_propname, &addrp, &addrsz))
		addrsz = 0;
	else
		addrsz /= (sizeof (uint64_t));
	num_alt_macaddr = addrsz;
	if (addrsz > 0) {
		alt_macaddr = kmem_zalloc(sizeof (ea) * addrsz, KM_SLEEP);
		for (i = 0; i < addrsz; i++) {
			val = *(((uint64_t *)addrp) + i);
			for (j = ETHERADDRL - 1; j >= 0; j--) {
				ea.ether_addr_octet[j]  = val & 0xFF;
				val >>= 8;
			}
			ether_copy(&ea, &alt_macaddr[i]);
		}
	}
	/* Alternate macaddr change? */
	if (num_alt_macaddr != vnp->ivf_num_alt_macaddr ||
	    ((num_alt_macaddr != 0) && (vnp->ivf_num_alt_macaddr != 0) &&
	    !iovcfg_cmp_macaddr(alt_macaddr, vnp->ivf_alt_macaddr,
	    num_alt_macaddr))) {
		updated |= IOV_UPD_ALT_MACADDR;
	}

	/* Read PVID */
	if (md_get_prop_val(mdp, node, pvid_propname, &val))
		pvid = VLAN_ID_NONE;
	else
		pvid = val & 0xFFF;

	/* Read VLAN IDs */
	if (md_get_prop_data(mdp, node, vid_propname, (uint8_t **)&data, &size))
		size = 0;
	else
		size /= sizeof (uint64_t);
	nvids = size;
	if (nvids != 0) {
		vf_vids =  kmem_zalloc(sizeof (iov_vlan_t) * nvids, KM_SLEEP);
		for (i = 0; i < nvids; i++) {
			vidp = &vf_vids[i];
			vidp->ivl_vid = data[i] & 0xFFFF;
		}
	}

	if (pvid != vnp->ivf_pvid) {
		/* pvid changed? */
		updated |= IOV_UPD_PVID;
	}
	/* Determine if there are any VLAN updates */
	if ((nvids != vnp->ivf_nvids) ||		/* # of vids changed? */
	    ((nvids != 0) && (vnp->ivf_nvids != 0) &&	 /* vids changed? */
	    !iovcfg_cmp_vids(vf_vids, vnp->ivf_vids, nvids))) {
		updated |= IOV_UPD_VIDS;
	}

	/* Read MTU */
	if (md_get_prop_val(mdp, node, mtu_propname, &val))
		mtu = IOVCFG_MTU_UNSPECIFIED;
	else
		mtu = (uint32_t)val;
	/* MTU update ? */
	if (mtu != vnp->ivf_mtu) {
		updated |= IOV_UPD_MTU;
	}

	/* No updates? */
	if (updated == IOV_UPD_NONE) {
		DBGNET("No changes for VF(%s)\n", vfp->ivf_pathname);
		if (alt_macaddr != NULL) {
			kmem_free(alt_macaddr, sizeof (ea) * addrsz);
		}
		if (vf_vids != NULL) {
			kmem_free(vf_vids, sizeof (iov_vlan_t) * nvids);
		}
		return (0);
	}

	argp = kmem_zalloc(sizeof (*argp), KM_SLEEP);
	if (updated & IOV_UPD_MACADDR) {
		DBGNET("Mac Address changed for VF(%s)\n", vfp->ivf_pathname);
		ether_copy(&macaddr, &argp->arg_macaddr);
	}
	if (updated & IOV_UPD_ALT_MACADDR) {
		DBGNET("Alternate Mac Address changed for VF(%s)\n",
		    vfp->ivf_pathname);
		argp->arg_num_alt_macaddr = num_alt_macaddr;
		argp->arg_alt_macaddr = alt_macaddr;
	} else {
		if (alt_macaddr != NULL) {
			kmem_free(alt_macaddr, sizeof (ea) * addrsz);
		}
	}
	if (updated & IOV_UPD_PVID) {
		DBGNET("PVID changed for VF(%s)\n", vfp->ivf_pathname);
		argp->arg_pvid = pvid;
	}
	if (updated & IOV_UPD_VIDS) {
		DBGNET("VLAN IDs changed for VF(%s)\n", vfp->ivf_pathname);
		argp->arg_nvids = nvids;
		argp->arg_vids = vf_vids;
	} else {
		if (vf_vids != NULL) {
			kmem_free(vf_vids, sizeof (iov_vlan_t) * nvids);
		}
	}
	if (updated & IOV_UPD_MTU) {
		DBGNET("MTU changed for VF(%s)\n", vfp->ivf_pathname);
		argp->arg_mtu = mtu;
	}
	argp->arg_vfp = vfp;
	argp->arg_upd = updated;

	rv = iovcfg_dispatch_reconfig_task(vfp, argp);
	if (rv != 0) {
		if (argp->arg_alt_macaddr != NULL) {
			kmem_free(argp->arg_alt_macaddr,
			    sizeof (ea) * argp->arg_num_alt_macaddr);
		}
		if (argp->arg_vids != NULL) {
			kmem_free(argp->arg_vids,
			    sizeof (iov_vlan_t) * argp->arg_nvids);
		}
		kmem_free(argp, sizeof (*argp));
	}
	return (rv);
}

#ifdef IOVCFG_UNCONFIG_SUPPORTED

/*
 * Unregister mdeg callbacks for all VFs under the given PF.
 */
void
iovcfg_reconfig_unreg_pf_net(iov_pf_t *pfp)
{
	iov_vf_t	*vfp;

	if (pfp == NULL)
		return;
	for (vfp = pfp->ipf_vfp; vfp != NULL; vfp = vfp->ivf_nextp) {
		iovcfg_mdeg_unreg(vfp);
	}
}

#endif

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
#include <sys/instance.h>			/* in_node_t */
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/ethernet.h>
#include <sys/vlan.h>
#include <sys/mac.h>
#include <sys/mac_client.h>
#include <sys/iovcfg.h>
#include <sys/iovcfg_net.h>

/* Internal functions */
static void iovcfg_pf_config_mac(void *arg);
#ifdef IOVCFG_UNCONFIG_SUPPORTED
static void iovcfg_pf_unconfig_mac(void *arg);
#endif
static int iovcfg_vf_config_mac(iov_pf_t *pfp, iov_vf_t *vfp);
static int iovcfg_add_primary_macaddr(iov_vf_t *vfp);
static int iovcfg_add_alt_macaddr(iov_vf_t *vfp);
static int iovcfg_add_alt_table(iov_vf_t *vfp, iov_alt_muh_t *alt_tbl,
    uint16_t vid, uint16_t mac_flags);
static void iovcfg_remove_alt_table(iov_vf_t *vfp, iov_alt_muh_t *alt_tbl);
static void iovcfg_vf_unconfig_mac(iov_vf_t *vfp, boolean_t mcl_close);
static void iovcfg_remove_alt_macaddr(iov_vf_t *vfp);
static void iovcfg_remove_primary_macaddr(iov_vf_t *vfp);
static void iovcfg_vf_reconfig_task(void *arg);
static void iovcfg_set_vfs_state(iov_pf_t *pfp, int state);
static int iovcfg_vf_set_mtu(iov_vf_t *vfp, boolean_t restore);
static int iovcfg_path_to_macname(char *pf_path, char *macname);
static void iovcfg_free_vid_alt_table(iov_vf_t *vfp);
static void iovcfg_free_pvid_alt_table(iov_vf_t *vfp);
static void iovcfg_free_def_alt_table(iov_vf_t *vfp);
static int iovcfg_add_pvid(iov_vf_t *vfp);
static void iovcfg_rem_pvid(iov_vf_t *vfp);
static int iovcfg_add_vids(iov_vf_t *vfp);
static void iovcfg_rem_vids(iov_vf_t *vfp);

extern iov_pf_t *iovcfg_pf_listp;

/* delay in ticks for mac_open() retries */
int	iovcfg_pf_macopen_delay = 10;

/* max # of mac_open() retries */
int	iovcfg_pf_macopen_retries = 10;

/*
 * Network class configuration notes:
 *
 * The configuration and reconfiguration of PFs (and its underlying VFs) are
 * done through taskq mechanism. For each PF that belongs to the network class,
 * a taskq is created to process config/reconfig. The taskq is created with a
 * single thread to serialize processing of operations on the PF.
 *
 * Configuration:
 *
 * Configuration is done when we get a notification from the PCIE framework
 * for a given PF, that its VFs have been configured and the class specific
 * configuration can safely proceed. We dispatch a task to configure the PF.
 * When this initialization task for the PF runs, it iterates over the list of
 * VFs under the PF and configures each VF with class specific data. For each
 * VF, a mac client instance is opened to its PF, and the properties of the VF
 * are programmed in the PF thru mac client interfaces. Note that the mac
 * client in this case provides only a control path and the mac layer does not
 * setup datapath for such clients (see mac_client_vf_bind()).
 *
 * Reconfiguration:
 * For each PF (and its underlying VFs), any platform specific callbacks can
 * be registered for reconfiguration updates. Whenever there is a change to the
 * network class props of the VF, the platform specific callback thread would
 * invoke the reconfiguration function with the updated data. We don't want to
 * process the VF reconfiguration in the context of such platform specific cb
 * thread, to avoid delays in its return. Therefore, we dispatch a task from
 * the callback function, to handle reconfig (iovcfg_vf_reconfig_task()).
 * As a result, there could be multiple updates to the same VF, while some of
 * its previous tasks are pending on the taskq. Only the last reconfig task
 * updates vf state.
 *
 * Unconfiguration:
 * The configuration of PFs (and their VFs) is torn down only at iovcfg module
 * unload time (_fini()). This shouldn't occur as px driver depends on iovcfg
 * module.
 */

/*
 * Create network class specific PF data.
 */
void
iovcfg_alloc_pf_net(iov_pf_t *pfp)
{
	iov_pf_net_t	*pnp;

	ASSERT(pfp != NULL);
	pnp = kmem_zalloc(sizeof (*pnp), KM_SLEEP);
	pfp->ipf_cl_data = (void *)pnp;
}

/*
 * Destroy network class specific PF data.
 */
void
iovcfg_free_pf_net(iov_pf_t *pfp)
{
	iov_pf_net_t	*pnp = (iov_pf_net_t *)pfp->ipf_cl_data;

	ASSERT(pfp != NULL);
	if (pnp != NULL) {
		kmem_free(pnp, sizeof (*pnp));
		pfp->ipf_cl_data = NULL;
	}
}

/*
 * Create network class specific VF data.
 */
void
iovcfg_alloc_vf_net(iov_vf_t *vfp)
{
	iov_vf_net_t	*vnp;

	ASSERT(vfp != NULL);
	vnp = kmem_zalloc(sizeof (*vnp), KM_SLEEP);
	vfp->ivf_cl_data = (void *)vnp;
}

/*
 * Destroy network class specific VF data.
 */
void
iovcfg_free_vf_net(iov_vf_t *vfp)
{
	iov_vf_net_t	*vnp;

	ASSERT(vfp != NULL);
	vnp = (iov_vf_net_t *)vfp->ivf_cl_data;
	if (vnp == NULL) {
		return;
	}
	ASSERT(vfp->ivf_state == IOVCFG_VF_UNCONFIGURED);

	iovcfg_free_pvid_alt_table(vfp);
	iovcfg_free_vid_alt_table(vfp);
	if (vnp->ivf_num_alt_macaddr > 0) {
		kmem_free(vnp->ivf_alt_macaddr,
		    sizeof (struct ether_addr) * vnp->ivf_num_alt_macaddr);
	}
	if (vnp->ivf_nvids > 0) {
		kmem_free(vnp->ivf_vids, sizeof (iov_vlan_t) * vnp->ivf_nvids);
	}
	kmem_free(vnp, sizeof (*vnp));
	vfp->ivf_cl_data = NULL;
}

/*
 * Perform class specific IOV configuration for network devices (PFs). This
 * function simply kicks off a task for the given PF to be configured.
 * Returns:
 * 	Success: DDI_EPENDING: Task to configure PF is dispatched.
 * 	Failure: DDI_FAILURE
 */
int
iovcfg_config_pf_net(iov_pf_t *pfp)
{
	char		qname[TASKQ_NAMELEN];
	iov_pf_net_t	*pnp;

	ASSERT(pfp != NULL);
	pnp = (iov_pf_net_t *)pfp->ipf_cl_data;
	/* Process only network class PFs */
	ASSERT(pfp->ipf_cl_ops->iop_class == IOV_CLASS_NET);

	/* Convert PF pathname to mac name */
	if (iovcfg_path_to_macname(pfp->ipf_pathname, pnp->ipf_macname)) {
		cmn_err(CE_WARN, "!Unable to get mac name: PF %s\n",
		    pfp->ipf_pathname);
		return (DDI_FAILURE);
	}

	(void) snprintf(qname, TASKQ_NAMELEN, "PF-%s", pnp->ipf_macname);
	pfp->ipf_taskq = ddi_taskq_create(NULL, qname, 1, TASKQ_DEFAULTPRI, 0);
	if (pfp->ipf_taskq == NULL) {
		cmn_err(CE_WARN, "!Unable to create task queue: %s\n", qname);
		return (DDI_FAILURE);
	}

	/* Mark the state of VFs as initializing */
	iovcfg_set_vfs_state(pfp, IOVCFG_VF_CONFIGURING);

	/* Dispatch a task to initialize the PF and its VFs */
	if (ddi_taskq_dispatch(pfp->ipf_taskq, iovcfg_pf_config_mac,
	    pfp, DDI_NOSLEEP)) {
		iovcfg_set_vfs_state(pfp, IOVCFG_VF_UNCONFIGURED);
		cmn_err(CE_WARN,
		    "!Failed to dispatch config task: %s\n", qname);
		return (DDI_FAILURE);
	}

	/*
	 * DDI_EPENDING is returned to tell the framework that class specific
	 * config task has been kicked off and is pending. When the config task
	 * runs and finishes configuration, it will notify the framework using
	 * pciv_class_config_completed() callback.
	 */
	return (DDI_EPENDING);
}

/*
 * Compare VLAN ids, array size expected to be same
 */
boolean_t
iovcfg_cmp_vids(iov_vlan_t *vids1, iov_vlan_t *vids2, int nvids)
{
	int		i;
	int		j;
	uint16_t	vid;

	ASSERT(vids1 != 0);
	ASSERT(vids2 != 0);
	for (i = 0; i < nvids; i++) {
		vid = vids1[i].ivl_vid;
		for (j = 0; j < nvids; j++) {
			if (vid == vids2[j].ivl_vid) {
				break;
			}
		}
		if (j == nvids) {
			return (B_FALSE);
		}
	}
	return (B_TRUE);
}

/*
 * Compare ether addr arrays; size expected to be same
 */
boolean_t
iovcfg_cmp_macaddr(struct ether_addr *ea1, struct ether_addr *ea2, int cnt)
{
	int		i;
	int		j;

	for (i = 0; i < cnt; i++) {
		for (j = 0; j < cnt; j++) {
			if (ether_cmp(&ea1[i], &ea2[j]) == 0) {
				break;
			}
		}
		if (j == cnt) {
			return (B_FALSE);
		}
	}
	return (B_TRUE);
}

int
iovcfg_dispatch_reconfig_task(iov_vf_t *vfp, iov_net_tsk_arg_t *argp)
{
	int		rv;
	iov_pf_t	*pfp;

	ASSERT(vfp != NULL);
	ASSERT(argp != NULL);
	pfp = vfp->ivf_pfp;
	mutex_enter(&vfp->ivf_lock);
	vfp->ivf_task_cnt++;
	if ((vfp->ivf_state & IOVCFG_VF_RECONFIGURING) == 0) {
		vfp->ivf_state |= IOVCFG_VF_RECONFIGURING;
	}
	mutex_exit(&vfp->ivf_lock);

	rv = ddi_taskq_dispatch(pfp->ipf_taskq, iovcfg_vf_reconfig_task,
	    argp, DDI_NOSLEEP);
	if (rv != DDI_SUCCESS) {
		DBGNET("Can't dispatch reconfig task: VF(%s)\n",
		    vfp->ivf_pathname);
		mutex_enter(&vfp->ivf_lock);
		ASSERT(vfp->ivf_task_cnt > 0);
		vfp->ivf_task_cnt--;
		/*
		 * Clear RECONFIGURING state, if more
		 * reconfig tasks are not pending.
		 */
		if (vfp->ivf_task_cnt == 0) {
			vfp->ivf_state &= ~(IOVCFG_VF_RECONFIGURING);
		}
		mutex_exit(&vfp->ivf_lock);
	}

	return (rv);
}

/*
 * Perform class specific IOV configuration for the given PF. Open the PF
 * device using its mac name first. Then obtain a mac client handle for each
 * of its VFs and program the VF props such as macaddr, vlan ids etc.
 */
static void
iovcfg_pf_config_mac(void *arg)
{
	int		rv;
	iov_vf_t	*vfp;
	int		retries = 0;
	iov_pf_t	*pfp = (iov_pf_t *)arg;
	iov_pf_net_t	*pnp = (iov_pf_net_t *)pfp->ipf_cl_data;

	do {
		rv = mac_open(pnp->ipf_macname, &pnp->ipf_mh);
		/*
		 * We retry mac_open() in these error cases :
		 *   ENOENT:	device is not available yet
		 *   EBUSY:	mac is exclusively held
		 */
		if (rv == 0) {
			break;
		}
		delay(iovcfg_pf_macopen_delay);
	} while ((rv == ENOENT || rv == EBUSY) &&
	    retries++ <= iovcfg_pf_macopen_retries);

	if (rv != 0) {
		cmn_err(CE_WARN,
		    "!PF(%s): mac open(%s) failed(%d)\n",
		    pfp->ipf_pathname, pnp->ipf_macname, rv);
		goto exit;
	} else {
		DBGNET("mac_open(%s) done,"
		    " retries(%d)\n", pnp->ipf_macname, retries);
	}

	for (vfp = pfp->ipf_vfp; vfp != NULL; vfp = vfp->ivf_nextp) {
		rv = iovcfg_vf_config_mac(pfp, vfp);
		if (rv != 0) {
			cmn_err(CE_WARN,
			    "!PF(%s), VF-ID(0x%x): Failed to configure mac\n",
			    pfp->ipf_pathname, vfp->ivf_id);
		}
		mutex_enter(&vfp->ivf_lock);
		ASSERT((vfp->ivf_state & IOVCFG_VF_CONFIGURING) != 0);
		vfp->ivf_state &= ~(IOVCFG_VF_CONFIGURING);
		/*
		 * Update the state, if reconfig
		 * has not started in the meantime.
		 */
		if ((vfp->ivf_state & IOVCFG_VF_RECONFIGURING) == 0) {
			if (rv == 0) {
				vfp->ivf_state |= IOVCFG_VF_CONFIGURED;
			} else {
				vfp->ivf_state |= IOVCFG_VF_UNCONFIGURED;
			}
		}
		mutex_exit(&vfp->ivf_lock);
	}
exit:
	pciv_class_config_completed(pfp->ipf_pathname);
}

/*
 * Configure the VF. Note this is not the SRIOV spec VF configuration; that is
 * already done by this time as part of the PF driver attach(9E). What we do
 * here is to provide the L2 policies to the PF on behalf of the VF, so that
 * the PF driver can validate and restrict the traffic to/from the VF (which
 * might have been assigned to a different domain). We open an instance of mac
 * client to the PF driver. We provide the VF-ID to the mac layer so it can
 * associate the client with the specific VF in the underlying PF driver. We
 * then program the unicast address, vlan ids and bandwidth limits for the VF.
 */
static int
iovcfg_vf_config_mac(iov_pf_t *pfp, iov_vf_t *vfp)
{
	int		rv;
	char		cli_name[MAXNAMELEN];
	iov_pf_net_t	*pnp = (iov_pf_net_t *)pfp->ipf_cl_data;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;
	uint16_t	flags = 0;

	vnp->ivf_mch = NULL;
	(void) snprintf(cli_name, MAXNAMELEN, "%s-VF-%d",
	    mac_name(pnp->ipf_mh), vfp->ivf_id);
	rv = mac_client_open(pnp->ipf_mh, &vnp->ivf_mch, cli_name, flags);
	if (rv != 0) {
		cmn_err(CE_WARN,
		    "!PF(%s), VF-ID(0x%x): Failed to open mac client(%d)\n",
		    pfp->ipf_pathname, vfp->ivf_id, rv);
		goto fail;
	}

	rv = mac_client_vf_bind(vnp->ivf_mch, vfp->ivf_id);
	if (rv != 0) {
		cmn_err(CE_WARN,
		    "!PF(%s), VF-ID(0x%x): Failed to bind mac client(%d)\n",
		    pfp->ipf_pathname, vfp->ivf_id, rv);
		goto fail;
	}
	vnp->ivf_bound = B_TRUE;

	rv = iovcfg_add_primary_macaddr(vfp);
	if (rv != 0) {
		cmn_err(CE_WARN,
		    "!PF(%s), VF-ID(0x%x): Failed to program primary"
		    " macaddr(%d)\n", pfp->ipf_pathname, vfp->ivf_id, rv);
		goto fail;
	}

	rv = iovcfg_add_alt_macaddr(vfp);
	if (rv != 0) {
		/*
		 * Failure to add alternate mac addrs will not result in VF
		 * configuration failure. This could happen if the client has
		 * not reserved any mac address slots (via some devprops that a
		 * driver might support) and is requesting more than the avail
		 * # slots. We print a warning and allow the VF to finish
		 * configuration and be available, although in a less desirable
		 * state.
		 */
		cmn_err(CE_WARN,
		    "!PF(%s), VF-ID(0x%x): Failed to program alternate"
		    " macaddr(%d)\n", pfp->ipf_pathname, vfp->ivf_id, rv);
	}

	rv = iovcfg_vf_set_mtu(vfp, B_FALSE);
	if (rv != 0) {
		/*
		 * Failure to set the mtu will not result in config failure.
		 */
		cmn_err(CE_WARN,
		    "!PF(%s), VF-ID(0x%x): Failed to program mtu(%d)\n",
		    pfp->ipf_pathname, vfp->ivf_id, rv);
	}

	return (0);
fail:
	iovcfg_vf_unconfig_mac(vfp, B_FALSE);
	return (1);
}

/*
 * Add the primary mac address in the VLANs specified for the VF.
 */
static int
iovcfg_add_primary_macaddr(iov_vf_t *vfp)
{
	int		rv;
	int		i;
	mac_diag_t	diag;
	uint8_t		*macaddr;
	iov_vlan_t	*vidp;
	uint16_t	vid;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	macaddr = (uint8_t *)vnp->ivf_macaddr.ether_addr_octet;
	vid = VLAN_ID_NONE;
	rv = mac_unicast_add(vnp->ivf_mch, macaddr,
	    IOVCFG_MAC_FLAGS_DEFAULT | MAC_UNICAST_VF_PRIMARY,
	    &vnp->ivf_pri_muh, vid, &diag);
	if (rv != 0) {
		/* cleanup stale hdl left by mac in some cases like EINVAL */
		vnp->ivf_pri_muh = NULL;
		return (rv);
	}

	if (vnp->ivf_pvid != VLAN_ID_NONE) {
		vid = vnp->ivf_pvid;
		rv = mac_unicast_add(vnp->ivf_mch, macaddr,
		    IOVCFG_MAC_FLAGS_PVID, &vnp->ivf_pri_pvid_muh, vid, &diag);
		if (rv != 0) {
			vnp->ivf_pri_pvid_muh = NULL;
			return (rv);
		}
	}

	for (i = 0; i < vnp->ivf_nvids; i++) {
		vidp = &vnp->ivf_vids[i];

		if (vidp->ivl_added == B_TRUE) {
			continue;
		}
		rv = mac_unicast_add(vnp->ivf_mch, macaddr,
		    IOVCFG_MAC_FLAGS_VID,
		    &vidp->ivl_muh, vidp->ivl_vid, &diag);
		if (rv != 0) {
			vidp->ivl_muh = NULL;
			return (rv);
		}
		vidp->ivl_added = B_TRUE;
	}

	return (0);
}

/*
 * Add the alternate mac addresses in the VLANs specified for the VF.
 */
static int
iovcfg_add_alt_macaddr(iov_vf_t *vfp)
{
	int		rv;
	int		i;
	iov_vlan_t	*vidp;
	uint16_t	vid;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if (vnp->ivf_num_alt_macaddr <= 0) {
		return (0);
	}

	vid = VLAN_ID_NONE;
	rv = iovcfg_add_alt_table(vfp, vnp->ivf_def_altp, vid,
	    IOVCFG_MAC_FLAGS_DEFAULT);
	if (rv != 0) {
		return (rv);
	}

	if (vnp->ivf_pvid != VLAN_ID_NONE) {
		vid = vnp->ivf_pvid;
		rv = iovcfg_add_alt_table(vfp, vnp->ivf_pvid_altp, vid,
		    IOVCFG_MAC_FLAGS_PVID);
		if (rv != 0) {
			return (rv);
		}
	}

	for (i = 0; i < vnp->ivf_nvids; i++) {
		vidp = &vnp->ivf_vids[i];
		rv = iovcfg_add_alt_table(vfp, vidp->ivl_altp, vidp->ivl_vid,
		    IOVCFG_MAC_FLAGS_VID);
		if (rv != 0) {
			return (rv);
		}
	}

	return (0);
}

static int
iovcfg_add_alt_table(iov_vf_t *vfp, iov_alt_muh_t *alt_tbl, uint16_t vid,
    uint16_t mac_flags)
{
	int		rv;
	int		i;
	uint_t		num_alt_macaddr;
	mac_diag_t	diag;
	uint8_t		*macaddr;
	iov_alt_muh_t	*altp;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if ((num_alt_macaddr = vnp->ivf_num_alt_macaddr) == 0) {
		return (0);
	}

	for (i = 0; i < num_alt_macaddr; i++) {
		macaddr = (uint8_t *)vnp->ivf_alt_macaddr[i].ether_addr_octet;
		altp = &alt_tbl[i];
		rv = mac_unicast_add(vnp->ivf_mch, macaddr, mac_flags,
		    &altp->alt_muh, vid, &diag);
		if (rv != 0) {
			altp->alt_muh = NULL;
			return (rv);
		}
		altp->alt_added = B_TRUE;
		altp->alt_index = i;
	}

	return (0);
}

/*
 * This routine tears down the L2 policies(macaddr, vlan ids, bw) for the VF,
 * using the mac client interfaces to the PF driver. We then close the mac
 * client instance if the caller wants to fully tear down the configuration.
 */
static void
iovcfg_vf_unconfig_mac(iov_vf_t *vfp, boolean_t mcl_close)
{
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if (vnp->ivf_bound == B_FALSE) {
		/* Not even bound; not much to cleanup */
		if (mcl_close == B_FALSE) {
			return;
		}
		if (vnp->ivf_mch != NULL) {
			mac_client_close(vnp->ivf_mch, 0);
			vnp->ivf_mch = NULL;
			return;
		}
	}

	iovcfg_remove_alt_macaddr(vfp);
	iovcfg_remove_primary_macaddr(vfp);
	if (vnp->ivf_mtu_orig != IOVCFG_MTU_UNSPECIFIED) {
		/*
		 * We have programmed some mtu; restore original mtu for the VF.
		 */
		if (vnp->ivf_mtu != vnp->ivf_mtu_orig) {
			(void) iovcfg_vf_set_mtu(vfp, B_TRUE);
		}
	}

	/*
	 * We close the mac client only if we are completely tearing down.
	 * Otherwise, we keep the client open to allow any reconfig updates to
	 * come in and update new parameters for the VF.
	 */
	if (mcl_close == B_TRUE) {
		if (vnp->ivf_bound == B_TRUE) {
			vnp->ivf_bound = B_FALSE;
		}
		if (vnp->ivf_mch != NULL) {
			mac_client_close(vnp->ivf_mch, 0);
			vnp->ivf_mch = NULL;
		}
	}
}

static void
iovcfg_remove_alt_macaddr(iov_vf_t *vfp)
{
	int		i;
	iov_vlan_t	*vidp;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if (vnp->ivf_num_alt_macaddr <= 0) {
		return;
	}

	for (i = 0; i < vnp->ivf_nvids; i++) {
		vidp = &vnp->ivf_vids[i];
		if (vidp->ivl_altp == NULL) {
			continue;
		}
		iovcfg_remove_alt_table(vfp, vidp->ivl_altp);
	}

	if (vnp->ivf_pvid_altp != NULL) {
		iovcfg_remove_alt_table(vfp, vnp->ivf_pvid_altp);
	}
	if (vnp->ivf_def_altp != NULL) {
		iovcfg_remove_alt_table(vfp, vnp->ivf_def_altp);
	}
}

static void
iovcfg_remove_alt_table(iov_vf_t *vfp, iov_alt_muh_t *alt_tbl)
{
	int		i;
	uint_t		num_alt_macaddr;
	iov_alt_muh_t	*altp;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if ((num_alt_macaddr = vnp->ivf_num_alt_macaddr) == 0) {
		return;
	}

	for (i = 0; i < num_alt_macaddr; i++) {
		altp = &alt_tbl[i];

		if (altp->alt_added == B_FALSE) {
			continue;
		}
		(void) mac_unicast_remove(vnp->ivf_mch, altp->alt_muh);
		altp->alt_added = B_FALSE;
		altp->alt_muh = NULL;
	}
}

static void
iovcfg_remove_primary_macaddr(iov_vf_t *vfp)
{
	int		i;
	iov_vlan_t	*vidp;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	for (i = 0; i < vnp->ivf_nvids; i++) {
		vidp = &vnp->ivf_vids[i];

		if (vidp->ivl_added == B_FALSE) {
			continue;
		}
		(void) mac_unicast_remove(vnp->ivf_mch, vidp->ivl_muh);
		vidp->ivl_added = B_FALSE;
		vidp->ivl_muh = NULL;
	}
	if (vnp->ivf_pri_pvid_muh != NULL) {
		(void) mac_unicast_remove(vnp->ivf_mch, vnp->ivf_pri_pvid_muh);
		vnp->ivf_pri_pvid_muh = NULL;
	}
	if (vnp->ivf_pri_muh != NULL) {
		(void) mac_unicast_remove(vnp->ivf_mch, vnp->ivf_pri_muh);
		vnp->ivf_pri_muh = NULL;
	}
}

/*
 * Taskq routine to reconfigure the VF.
 */
static void
iovcfg_vf_reconfig_task(void *arg)
{
	iov_vf_t		*vfp;
	iov_pf_t		*pfp;
	iov_upd_t		upd;
	iov_vf_net_t		*vnp;
	iov_net_tsk_arg_t	*argp = (iov_net_tsk_arg_t *)arg;
	int			rv = 0;
	int			err = 0;

	if (argp == NULL) {
		return;
	}
	if ((vfp = argp->arg_vfp) == NULL) {
		kmem_free(argp, sizeof (*argp));
		return;
	}
	if ((vnp = (iov_vf_net_t *)vfp->ivf_cl_data) == NULL) {
		kmem_free(argp, sizeof (*argp));
		return;
	}

	if (vnp->ivf_mch == NULL || vnp->ivf_bound == B_FALSE) {
		kmem_free(argp, sizeof (*argp));
		return;
	}

	pfp = vfp->ivf_pfp;
	upd = argp->arg_upd;

	/* Mac address update */
	if (upd & (IOV_UPD_MACADDR | IOV_UPD_ALT_MACADDR)) {
		/*
		 * We remove and re-add all mac addresses for either primary
		 * and/or alt-macaddr changes. This is because in the case of
		 * primary macaddr change, we need to retain it as the first
		 * mac address added in the mac layer. Removing and re-adding
		 * only the primary macaddr would change its order if there
		 * are existing alt macaddrs.
		 */

		/* Remove old pri-mac from h/w */
		iovcfg_remove_primary_macaddr(vfp);

		/* Remove old alt-macs from h/w */
		iovcfg_remove_alt_macaddr(vfp);

		if (upd & IOV_UPD_MACADDR) {
			/* Save new pri-mac */
			ether_copy(&argp->arg_macaddr, &vnp->ivf_macaddr);
		}

		if (upd & IOV_UPD_ALT_MACADDR) {
			/* Free tables that are based on old alt-macs */
			iovcfg_free_def_alt_table(vfp);
			iovcfg_free_pvid_alt_table(vfp);
			iovcfg_free_vid_alt_table(vfp);
			if (vnp->ivf_num_alt_macaddr > 0) {
				kmem_free(vnp->ivf_alt_macaddr,
				    sizeof (struct ether_addr) *
				    vnp->ivf_num_alt_macaddr);
				vnp->ivf_alt_macaddr = NULL;
			}

			/* Save new alt-macs */
			vnp->ivf_num_alt_macaddr = argp->arg_num_alt_macaddr;
			vnp->ivf_alt_macaddr = argp->arg_alt_macaddr;

			/* Create tables based on new alt-macs */
			if (vnp->ivf_num_alt_macaddr > 0) {
				iovcfg_alloc_def_alt_table(vfp);
				iovcfg_alloc_pvid_alt_table(vfp);
				iovcfg_alloc_vid_alt_table(vfp);
			}
		}

		/* Add new pri-mac to h/w */
		rv = iovcfg_add_primary_macaddr(vfp);
		if (rv != 0) {
			/*
			 * Failure to add primary macaddr will result in
			 * reconfig failure. Set err that gets checked at exit.
			 */
			err = rv;
			cmn_err(CE_WARN,
			    "!PF(%s), VF-ID(0x%x): Failed to reconfig primary"
			    " macaddr(%d)\n",
			    pfp->ipf_pathname, vfp->ivf_id, rv);
			goto done;
		}

		/* Add new alt-macs to h/w */
		rv = iovcfg_add_alt_macaddr(vfp);
		if (rv != 0) {
			/* only warning for altmac failure; no reconfig fail */
			cmn_err(CE_WARN,
			    "!PF(%s), VF-ID(0x%x): Failed to reconfig alternate"
			    " macaddr(%d)\n",
			    pfp->ipf_pathname, vfp->ivf_id, rv);
		}
	}

	if (upd & IOV_UPD_PVID) {
		/* Remove old pvid from h/w */
		iovcfg_rem_pvid(vfp);

		/* Free tables that are based on old pvid */
		iovcfg_free_pvid_alt_table(vfp);

		/* Save new pvid */
		vnp->ivf_pvid = argp->arg_pvid;

		/* Create tables based on new pvid */
		iovcfg_alloc_pvid_alt_table(vfp);

		/* Add new pvid to h/w */
		rv = iovcfg_add_pvid(vfp);
		if (rv != 0) {
			/* only warning for pvid failure; no reconfig fail */
			cmn_err(CE_WARN,
			    "!PF(%s), VF-ID(0x%x): Failed to reconfig"
			    " pvid(%d)\n",
			    pfp->ipf_pathname, vfp->ivf_id, rv);
		}
	}

	if (upd & IOV_UPD_VIDS) {
		/* Remove old vids from h/w */
		iovcfg_rem_vids(vfp);

		/* Free tables that are based on old vids */
		iovcfg_free_vid_alt_table(vfp);
		if (vnp->ivf_nvids > 0) {
			kmem_free(vnp->ivf_vids, sizeof (iov_vlan_t) *
			    vnp->ivf_nvids);
			vnp->ivf_vids = NULL;
			vnp->ivf_nvids = 0;
		}

		/* Save new vids */
		vnp->ivf_nvids = argp->arg_nvids;
		vnp->ivf_vids = argp->arg_vids;

		/* Create tables based on new vids */
		iovcfg_alloc_vid_alt_table(vfp);

		/* Add new vids to h/w */
		rv = iovcfg_add_vids(vfp);
		if (rv != 0) {
			/* only warning for vid failure; no reconfig fail */
			cmn_err(CE_WARN,
			    "!PF(%s), VF-ID(0x%x): Failed to reconfig"
			    " vids(%d)\n",
			    pfp->ipf_pathname, vfp->ivf_id, rv);
		}
	}

	if (upd & IOV_UPD_MTU) {
		/* Save new value */
		vnp->ivf_mtu = argp->arg_mtu;

		/* Reconfig mtu */
		rv = iovcfg_vf_set_mtu(vfp, B_FALSE);
		if (rv != 0) {
			/* only warning for mtu failure; no reconfig fail */
			cmn_err(CE_WARN,
			    "!PF(%s), VF-ID(0x%x): Failed to reconfig"
			    " mtu(%d)\n",
			    pfp->ipf_pathname, vfp->ivf_id, rv);
		}
	}

done:
	/* Set VF state */
	mutex_enter(&vfp->ivf_lock);
	ASSERT(vfp->ivf_task_cnt > 0);
	ASSERT((vfp->ivf_state & IOVCFG_VF_RECONFIGURING) != 0);
	vfp->ivf_task_cnt--;
	/*
	 * Clear RECONFIGURING and set the new state,
	 * if more reconfig tasks are not pending.
	 */
	if (vfp->ivf_task_cnt == 0) {
		vfp->ivf_state &= ~(IOVCFG_VF_RECONFIGURING);
		if (err == 0) {
			vfp->ivf_state &= ~(IOVCFG_VF_UNCONFIGURED);
			vfp->ivf_state |= IOVCFG_VF_CONFIGURED;
		} else {
			vfp->ivf_state |= IOVCFG_VF_UNCONFIGURED;
		}
	}
	mutex_exit(&vfp->ivf_lock);

	kmem_free(argp, sizeof (*argp));
}

/*
 * Set the state of all VFs under the given PF.
 */
static void
iovcfg_set_vfs_state(iov_pf_t *pfp, int state)
{
	iov_vf_t	*vfp;

	for (vfp = pfp->ipf_vfp; vfp != NULL; vfp = vfp->ivf_nextp) {
		mutex_enter(&vfp->ivf_lock);
		vfp->ivf_state = state;
		mutex_exit(&vfp->ivf_lock);
	}
}

/*
 * Configure MTU for the VF. The arg 'restore' tells whether the caller wants
 * to program a new MTU or wants to restore the original MTU for the VF.
 */
static int
iovcfg_vf_set_mtu(iov_vf_t *vfp, boolean_t restore)
{
	int		rv;
	uint32_t	old_mtu;
	uint32_t	mtu;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if (restore == B_TRUE) {
		/* restore orig mtu */
		mtu = vnp->ivf_mtu_orig;
	} else {
		/* set a new mtu */
		mtu = vnp->ivf_mtu;
	}

	if (mtu == IOVCFG_MTU_UNSPECIFIED) {
		/* nothing really to set */
		return (0);
	}
	rv = mac_client_set_mtu(vnp->ivf_mch, mtu, &old_mtu);
	if (rv == 0) {
		if (restore == B_TRUE) {
			/*
			 * We restored the original mtu; clear the value which
			 * tells us that we have not programmed any mtu for
			 * the VF.
			 */
			vnp->ivf_mtu_orig = IOVCFG_MTU_UNSPECIFIED;
		} else {
			if (vnp->ivf_mtu_orig == IOVCFG_MTU_UNSPECIFIED) {
				/*
				 * Save the original mtu the
				 * first time we set a new mtu.
				 */
				vnp->ivf_mtu_orig = old_mtu;
			}
		}
	}

	return (rv);
}

/*
 * Convert the given PF pathname (/devices/...) to its mac name (drvN).
 */
static int
iovcfg_path_to_macname(char *pf_path, char *macname)
{
	in_node_t	*np;
	int		instance = 0;
	char		*driver_name = "";

	if (pf_path == NULL || macname == NULL) {
		return (EINVAL);
	}

	e_ddi_enter_instance();
	np = e_ddi_path_to_instance(pf_path);
	e_ddi_exit_instance();
	if (np && np->in_drivers) {
		driver_name = np->in_drivers->ind_driver_name;
		instance = np->in_drivers->ind_instance;
		(void) snprintf(macname, MAXNAMELEN, "%s%d", driver_name,
		    instance);
		return (0);
	}

	return (ENXIO);
}

void
iovcfg_alloc_vid_alt_table(iov_vf_t *vfp)
{
	int		i;
	iov_vlan_t	*vidp;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if (vnp->ivf_nvids == 0 || vnp->ivf_num_alt_macaddr == 0) {
		return;
	}
	ASSERT(vnp->ivf_vids != NULL);
	for (i = 0; i < vnp->ivf_nvids; i++) {
		vidp = &vnp->ivf_vids[i];
		vidp->ivl_altp = kmem_zalloc(sizeof (iov_alt_muh_t) *
		    vnp->ivf_num_alt_macaddr, KM_SLEEP);
	}
}

static void
iovcfg_free_vid_alt_table(iov_vf_t *vfp)
{
	int		i;
	iov_vlan_t	*vidp;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if (vnp->ivf_nvids == 0) {
		return;
	}
	ASSERT(vnp->ivf_vids != NULL);
	for (i = 0; i < vnp->ivf_nvids; i++) {
		vidp = &vnp->ivf_vids[i];
		if (vidp->ivl_altp != NULL) {
			kmem_free(vidp->ivl_altp, sizeof (iov_alt_muh_t) *
			    vnp->ivf_num_alt_macaddr);
			vidp->ivl_altp = NULL;
		}
	}
}

void
iovcfg_alloc_pvid_alt_table(iov_vf_t *vfp)
{
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if (vnp->ivf_num_alt_macaddr == 0) {
		return;
	}
	vnp->ivf_pvid_altp = kmem_zalloc(sizeof (iov_alt_muh_t) *
	    vnp->ivf_num_alt_macaddr, KM_SLEEP);
}

static void
iovcfg_free_pvid_alt_table(iov_vf_t *vfp)
{
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if (vnp->ivf_pvid_altp == NULL) {
		return;
	}
	kmem_free(vnp->ivf_pvid_altp, sizeof (iov_alt_muh_t) *
	    vnp->ivf_num_alt_macaddr);
	vnp->ivf_pvid_altp = NULL;
}

void
iovcfg_alloc_def_alt_table(iov_vf_t *vfp)
{
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if (vnp->ivf_num_alt_macaddr == 0) {
		return;
	}
	vnp->ivf_def_altp = kmem_zalloc(sizeof (iov_alt_muh_t) *
	    vnp->ivf_num_alt_macaddr, KM_SLEEP);
}

static void
iovcfg_free_def_alt_table(iov_vf_t *vfp)
{
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if (vnp->ivf_def_altp == NULL) {
		return;
	}
	kmem_free(vnp->ivf_def_altp, sizeof (iov_alt_muh_t) *
	    vnp->ivf_num_alt_macaddr);
	vnp->ivf_def_altp = NULL;
}

static int
iovcfg_add_pvid(iov_vf_t *vfp)
{
	int		rv;
	uint16_t	vid;
	mac_diag_t	diag;
	uint8_t		*macaddr;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if ((vid = vnp->ivf_pvid) == VLAN_ID_NONE) {
		return (0);
	}
	macaddr = (uint8_t *)vnp->ivf_macaddr.ether_addr_octet;
	rv = mac_unicast_add(vnp->ivf_mch, macaddr,
	    IOVCFG_MAC_FLAGS_PVID,
	    &vnp->ivf_pri_pvid_muh, vid, &diag);
	if (rv != 0) {
		vnp->ivf_pri_pvid_muh = NULL;
		return (rv);
	}
	if (iovcfg_add_alt_table(vfp, vnp->ivf_pvid_altp, vid,
	    IOVCFG_MAC_FLAGS_PVID) == 0)
		return (0);

	/* Failure */
	iovcfg_rem_pvid(vfp);
	return (1);
}

static void
iovcfg_rem_pvid(iov_vf_t *vfp)
{
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	if (vnp->ivf_pvid == VLAN_ID_NONE) {
		return;
	}
	if (vnp->ivf_pri_pvid_muh != NULL) {
		(void) mac_unicast_remove(vnp->ivf_mch, vnp->ivf_pri_pvid_muh);
		vnp->ivf_pri_pvid_muh = NULL;
	}
	if (vnp->ivf_pvid_altp != NULL) {
		iovcfg_remove_alt_table(vfp, vnp->ivf_pvid_altp);
	}
}

static int
iovcfg_add_vids(iov_vf_t *vfp)
{
	int		i;
	mac_diag_t	diag;
	uint8_t		*macaddr;
	iov_vlan_t	*vidp;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	macaddr = (uint8_t *)vnp->ivf_macaddr.ether_addr_octet;
	for (i = 0; i < vnp->ivf_nvids; i++) {
		vidp = &vnp->ivf_vids[i];

		if (vidp->ivl_added == B_TRUE) {
			continue;
		}
		if (mac_unicast_add(vnp->ivf_mch, macaddr,
		    IOVCFG_MAC_FLAGS_VID,
		    &vidp->ivl_muh, vidp->ivl_vid, &diag)) {
			vidp->ivl_muh = NULL;
			goto fail;
		}
		vidp->ivl_added = B_TRUE;
		if (iovcfg_add_alt_table(vfp, vidp->ivl_altp, vidp->ivl_vid,
		    IOVCFG_MAC_FLAGS_VID))
			goto fail;
	}
	return (0);

fail:
	iovcfg_rem_vids(vfp);
	return (1);
}

static void
iovcfg_rem_vids(iov_vf_t *vfp)
{
	int		i;
	iov_vlan_t	*vidp;
	iov_vf_net_t	*vnp = (iov_vf_net_t *)vfp->ivf_cl_data;

	for (i = 0; i < vnp->ivf_nvids; i++) {
		vidp = &vnp->ivf_vids[i];

		if (vidp->ivl_added == B_TRUE) {
			(void) mac_unicast_remove(vnp->ivf_mch, vidp->ivl_muh);
			vidp->ivl_added = B_FALSE;
			vidp->ivl_muh = NULL;
		}
		if (vidp->ivl_altp != NULL) {
			iovcfg_remove_alt_table(vfp, vidp->ivl_altp);
		}
	}
}

#ifdef IOVCFG_UNCONFIG_SUPPORTED

/*
 * Tear down class specific configuration of network devices (PFs). This
 * function simply kicks off a task for the given PF to be unconfigured.
 */
void
iovcfg_unconfig_pf_net(iov_pf_t *pfp)
{
	int		rv;
	iov_pf_net_t	*pnp;

	ASSERT(pfp != NULL);
	pnp = (iov_pf_net_t *)pfp->ipf_cl_data;
	if (pfp->ipf_cl_ops->iop_class != IOV_CLASS_NET) {
		return;
	}
	if (pnp->ipf_mh == NULL || pfp->ipf_taskq == NULL) {
		return;
	}


	/*
	 * Now kick off the task to uncofigure all VFs.
	 */
	if (ddi_taskq_dispatch(pfp->ipf_taskq, iovcfg_pf_unconfig_mac,
	    pfp, DDI_NOSLEEP)) {
		DBGNET("Can't dispatch unconfig mac task: PF(%s)\n",
		    pfp->ipf_pathname);
	}
	/*
	 * Destroy taskq; also waits for above unconfig to complete.
	 */
	ddi_taskq_destroy(pfp->ipf_taskq);
	pfp->ipf_taskq = NULL;
}

/*
 * Taskq routine to tear down network class specific config of the given PF.
 */
static void
iovcfg_pf_unconfig_mac(void *arg)
{
	iov_vf_t	*vfp;
	iov_vf_net_t	*vnp;
	iov_pf_t	*pfp;
	iov_pf_net_t	*pnp;

	ASSERT(arg != NULL);
	pfp = (iov_pf_t *)arg;
	pnp = (iov_pf_net_t *)pfp->ipf_cl_data;
	for (vfp = pfp->ipf_vfp; vfp != NULL; vfp = vfp->ivf_nextp) {
		vnp = (iov_vf_net_t *)vfp->ivf_cl_data;
		if (vnp->ivf_mch == NULL) {
			continue;
		}
		/*
		 * Unconfigure VF. Note that plat reconfig callbacks (and the
		 * resulting reconfig tasks) are expected to be stopped by this
		 * time. This task must be the last task to be operating on the
		 * VF.
		 */
		mutex_enter(&vfp->ivf_lock);
		ASSERT((vfp->ivf_state &
		    (IOVCFG_VF_CONFIGURING|IOVCFG_VF_RECONFIGURING)) == 0);
		ASSERT((vfp->ivf_state & IOVCFG_VF_RECONFIGURING) == 0);
		ASSERT(vfp->ivf_task_cnt == 0);
		mutex_exit(&vfp->ivf_lock);

		iovcfg_vf_unconfig_mac(vfp, B_TRUE);
		vfp->ivf_state = IOVCFG_VF_UNCONFIGURED;
	}

	if (pnp->ipf_mh != NULL) {
		mac_close(pnp->ipf_mh);
		pnp->ipf_mh = NULL;
	}
}

#endif

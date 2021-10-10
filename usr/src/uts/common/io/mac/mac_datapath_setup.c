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

#include <sys/vlan.h>
#include <sys/mac_impl.h>
#include <sys/mac_client_impl.h>
#include <sys/mac_client_priv.h>
#include <sys/mac_flow_impl.h>
#include <sys/mac_cpu_impl.h>
#include <sys/mac_stat.h>

/*
 * Set up the RX group.
 */
void
mac_rx_group_setup(mac_client_impl_t *mcip)
{
	flow_entry_t	*flent = mcip->mci_flent;
	mac_group_t	*group = flent->fe_rx_ring_group;

	mac_rx_client_quiesce((mac_client_handle_t)mcip);
	flent->fe_cb_fn = mac_rx_deliver;
	flent->fe_cb_arg1 = mcip;
	flent->fe_cb_arg2 = NULL;

	if (group != NULL) {
		if (group->mrg_state == MAC_GROUP_STATE_RESERVED &&
		    i_mac_flow_vid(flent) == VLAN_ID_NONE) {
			group->mrg_flent = flent;
		} else {
			group->mrg_flent = NULL;
		}
	}
	ASSERT((mcip->mci_state_flags & MCIS_NO_UNICAST_ADDR) == 0);

	if (mac_rx_fanout_enable &&
	    (mcip->mci_state_flags & MCIS_EXCLUSIVE) == 0) {
		mac_rx_fanout_init(mcip);
	}
	mac_rx_client_restart((mac_client_handle_t)mcip);
}

void
mac_rx_group_teardown(mac_client_impl_t *mcip)
{
	flow_entry_t	*flent = mcip->mci_flent;
	mac_group_t	*group = flent->fe_rx_ring_group;

	mac_rx_client_quiesce((mac_client_handle_t)mcip);
	if (mcip->mci_rx_fanout != NULL)
		mac_rx_fanout_fini(mcip);

	if (group != NULL && group->mrg_flent == flent)
		group->mrg_flent = NULL;

	mac_rx_client_restart((mac_client_handle_t)mcip);
}

/*
 * Set up the TX group.
 */

/* ARGSUSED */
void
mac_tx_group_setup(mac_client_impl_t *mcip)
{
}

/* ARGSUSED */
void
mac_tx_group_teardown(mac_client_impl_t *mcip)
{
}


/*
 * This is the group state machine.
 *
 * The state of an Rx group is given by
 * the following table. The default group and its rings are started in
 * mac_start itself and the default group stays in SHARED state until
 * mac_stop at which time the group and rings are stopped and and it
 * reverts to the Registered state.
 *
 * Typically this function is called on a group after adding or removing a
 * client from it, to find out what should be the new state of the group.
 * If the new state is RESERVED, then the client that owns this group
 * exclusively is also returned. Note that adding or removing a client from
 * a group could also impact the default group and the caller needs to
 * evaluate the effect on the default group.
 *
 * Group type		# of clients	mi_nactiveclients	Group State
 *			in the group
 *
 * Non-default		0		N.A.			REGISTERED
 * Non-default		1		N.A.			RESERVED
 *
 * Default		0		N.A.			SHARED
 * Default		1		1			RESERVED
 * Default		1		> 1			SHARED
 * Default		> 1		N.A.			SHARED
 *
 * For a TX group, the following is the state table.
 *
 * Group type		# of clients	Group State
 *			in the group
 *
 * Non-default		0		REGISTERED
 * Non-default		1		RESERVED
 *
 * Default		0		REGISTERED
 * Default		1		RESERVED
 * Default		> 1		SHARED
 */
mac_group_state_t
mac_group_next_state(mac_group_t *grp, mac_client_impl_t **group_only_mcip,
    mac_group_t *defgrp, boolean_t rx_group)
{
	mac_impl_t		*mip = (mac_impl_t *)grp->mrg_mh;

	*group_only_mcip = NULL;

	/* Non-default group */

	if (grp != defgrp) {
		if (MAC_GROUP_NO_CLIENT(grp))
			return (MAC_GROUP_STATE_REGISTERED);

		*group_only_mcip = MAC_GROUP_ONLY_CLIENT(grp);
		if (*group_only_mcip != NULL)
			return (MAC_GROUP_STATE_RESERVED);

		return (MAC_GROUP_STATE_SHARED);
	}

	/* Default group */

	if (MAC_GROUP_NO_CLIENT(grp)) {
		if (rx_group)
			return (MAC_GROUP_STATE_SHARED);
		else
			return (MAC_GROUP_STATE_REGISTERED);
	}
	*group_only_mcip = MAC_GROUP_ONLY_CLIENT(grp);
	if (*group_only_mcip == NULL)
		return (MAC_GROUP_STATE_SHARED);

	if (rx_group && mip->mi_nactiveclients != 1)
		return (MAC_GROUP_STATE_SHARED);

	ASSERT(*group_only_mcip != NULL);
	return (MAC_GROUP_STATE_RESERVED);
}

int
mac_datapath_setup(mac_client_impl_t *mcip, flow_entry_t *flent, uint16_t flags)
{
	mac_impl_t		*mip = mcip->mci_mip;
	mac_group_t		*rgroup = NULL;
	mac_group_t		*tgroup = NULL;
	mac_group_t		*default_rgroup;
	mac_group_t		*default_tgroup;
	int			err;
	uint8_t 		*mac_addr;
	uint16_t		vid;
	mac_group_state_t	next_state;
	mac_client_impl_t	*group_only_mcip;
	mac_resource_props_t	*mrp = MCIP_RESOURCE_PROPS(mcip);
	boolean_t		rxhw;
	boolean_t		txhw;
	boolean_t		no_unicast;
	boolean_t		isprimary = flent->fe_type & FLOW_PRIMARY_MAC;
	mac_client_impl_t	*reloc_pmcip = NULL;
	boolean_t		usehw;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	/*
	 * Block everything while rings/groups are being moved around.
	 */
	i_mac_setup_enter(mip);

	no_unicast = mcip->mci_state_flags & MCIS_NO_UNICAST_ADDR;
	mac_addr = flent->fe_flow_desc.fd_dst_mac;
	vid = flent->fe_flow_desc.fd_vid;

	/* Default RX group */
	default_rgroup = MAC_DEFAULT_RX_GROUP(mip);

	/* Default TX group */
	default_tgroup = MAC_DEFAULT_TX_GROUP(mip);

	if (no_unicast)
		goto grp_found;

	rxhw = (mrp->mrp_mask & MRP_RX_RINGS) &&
	    ((mrp->mrp_nrxrings > 0) ||
	    (mrp->mrp_mask & MRP_RXRINGS_UNSPEC));
	txhw = (mrp->mrp_mask & MRP_TX_RINGS) &&
	    ((mrp->mrp_ntxrings > 0) ||
	    (mrp->mrp_mask & MRP_TXRINGS_UNSPEC));

	/*
	 * By default we have given the primary all the rings
	 * i.e. the default group. Let's see if the primary
	 * needs to be relocated so that the addition of this
	 * client doesn't impact the primary's performance,
	 * i.e. if the primary is in the default group and
	 * we add this client, the primary will lose polling.
	 * We do this only for NICs supporting dynamic ring
	 * grouping and only when this is the first client
	 * after the primary (i.e. nactiveclients is 2)
	 */
	if (!isprimary && mip->mi_nactiveclients == 2 &&
	    (group_only_mcip = mac_primary_client_handle(mip)) !=
	    NULL && mip->mi_rx_group_type == MAC_GROUP_TYPE_DYNAMIC) {
		reloc_pmcip = mac_check_primary_relocation(
		    group_only_mcip, rxhw);
	}
	/*
	 * Check to see if we can get an exclusive group for
	 * this mac address or if there already exists a
	 * group that has this mac address (case of VLANs).
	 * If no groups are available, use the default group.
	 */
	rgroup = mac_reserve_rx_group(mcip, mac_addr, B_FALSE);
	if (rgroup == NULL && rxhw) {
		err = ENOSPC;
		goto setup_failed;
	} else if (rgroup == NULL) {
		rgroup = default_rgroup;
	}
	/*
	 * Check to see if we can get an exclusive group for
	 * this mac client. If no groups are available, use
	 * the default group.
	 */
	tgroup = mac_reserve_tx_group(mcip, B_FALSE);
	if (tgroup == NULL && txhw) {
		if (rgroup != NULL && rgroup != default_rgroup)
			mac_release_rx_group(mcip, rgroup);
		err = ENOSPC;
		goto setup_failed;
	} else if (tgroup == NULL) {
		tgroup = default_tgroup;
	}

	/*
	 * Some NICs don't support any Rx rings, so there may not
	 * even be a default group.
	 */
grp_found:
	if (rgroup != NULL) {
		if ((rgroup != default_rgroup) &&
		    MAC_GROUP_NO_CLIENT(rgroup) &&
		    (rxhw || mcip->mci_share != NULL)) {
			MAC_RX_GRP_RESERVED(mip);
			if (mip->mi_rx_group_type ==
			    MAC_GROUP_TYPE_DYNAMIC) {
				MAC_RX_RING_RESERVED(mip,
				    rgroup->mrg_cur_count);
			}
		}
		flent->fe_rx_ring_group = rgroup;
		/*
		 * Add the client to the group. This could cause
		 * either this group to move to the shared state or
		 * cause the default group to move to the shared state.
		 * The actions on this group are done here, while the
		 * actions on the default group are postponed to
		 * the end of this function.
		 */
		mac_group_add_client(rgroup, mcip);
		next_state = mac_group_next_state(rgroup,
		    &group_only_mcip, default_rgroup, B_TRUE);
		mac_set_group_state(rgroup, next_state);
	}

	if (tgroup != NULL) {
		if (tgroup != default_tgroup &&
		    MAC_GROUP_NO_CLIENT(tgroup) &&
		    (txhw || mcip->mci_share != NULL)) {
			MAC_TX_GRP_RESERVED(mip);
			if (mip->mi_tx_group_type ==
			    MAC_GROUP_TYPE_DYNAMIC) {
				MAC_TX_RING_RESERVED(mip,
				    tgroup->mrg_cur_count);
			}
		}
		flent->fe_tx_ring_group = tgroup;
		mac_group_add_client(tgroup, mcip);
		next_state = mac_group_next_state(tgroup,
		    &group_only_mcip, default_tgroup, B_FALSE);
		tgroup->mrg_state = next_state;
	}
	/* We are setting up minimal datapath only */
	if (no_unicast)
		goto done;

	mac_tx_group_setup(mcip);
	mac_rx_group_setup(mcip);
	mac_cpu_pool_setup(mcip);

	/* Program the S/W Classifer */
	if ((err = mac_flow_add(mip->mi_flow_tab, flent)) != 0)
		goto setup_failed;

	/* Program the H/W Classifier */
	usehw = ((mcip->mci_state_flags & MCIS_UNICAST_HW) != 0);
	if ((err = mac_add_macaddr(mip, rgroup, mac_addr,
	    usehw, B_FALSE)) != 0)
		goto setup_failed;
	if ((vid != VLAN_ID_NONE) && usehw) {
		err = mac_add_macvlan(mip, rgroup, vid, flags);
		if (err != 0)
			goto setup_failed;
	}
	mcip->mci_unicast = mac_find_macaddr(mip, mac_addr);
	ASSERT(mcip->mci_unicast != NULL);
	/* Initialize the v6 local addr used by link protection */
	mac_protect_update_v6_local_addr(mcip);

done:
	/*
	 * All broadcast and multicast traffic is received only on the default
	 * group. If we have setup the datapath for a non-default group above
	 * then move the default group to shared state to allow distribution of
	 * incoming broadcast traffic to the other groups.
	 */
	if (rgroup != NULL) {
		if (rgroup != default_rgroup) {
			if (default_rgroup->mrg_state ==
			    MAC_GROUP_STATE_RESERVED) {
				group_only_mcip = MAC_GROUP_ONLY_CLIENT(
				    default_rgroup);
				ASSERT(group_only_mcip != NULL &&
				    mip->mi_nactiveclients > 1);

				mac_set_group_state(default_rgroup,
				    MAC_GROUP_STATE_SHARED);
			}
			ASSERT(default_rgroup->mrg_state ==
			    MAC_GROUP_STATE_SHARED);
		}
	}
	mac_set_rings_effective(mcip);
	i_mac_setup_exit(mip);
	return (0);

setup_failed:
	/* Switch the primary back to default group */
	if (reloc_pmcip != NULL) {
		(void) mac_rx_switch_group(reloc_pmcip,
		    reloc_pmcip->mci_flent->fe_rx_ring_group, default_rgroup);
	}
	i_mac_setup_exit(mip);
	mac_datapath_teardown(mcip);
	return (err);
}

void
mac_datapath_teardown(mac_client_impl_t *mcip)
{
	mac_impl_t		*mip = mcip->mci_mip;
	mac_client_impl_t	*grp_only_mcip;
	mac_group_t		*default_group, *group = NULL;
	flow_entry_t		*group_only_flent, *flent = mcip->mci_flent;
	boolean_t		check_default_group = B_FALSE;
	mac_group_state_t	next_state;
	mac_resource_props_t	*mrp = MCIP_RESOURCE_PROPS(mcip);
	int			err;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	/*
	 * Block everything while rings/groups are being moved around.
	 */
	i_mac_setup_enter(mip);

	/*
	 * Stop the packets coming from the H/W
	 */
	if (mcip->mci_unicast != NULL) {
		err = mac_remove_macaddr(mip, mcip->mci_unicast->ma_addr);
		if (err != 0) {
			cmn_err(CE_WARN, "%s: failed to remove a MAC"
			    " address because of error 0x%x",
			    mip->mi_name, err);
		}
		mcip->mci_unicast = NULL;
	}

	/* Stop the packets coming from the S/W classifier */
	mac_flow_remove(mip->mi_flow_tab, flent, B_FALSE);
	mac_flow_wait(flent, FLOW_DRIVER_UPCALL);

	mac_cpu_teardown(mcip);
	mac_rx_group_teardown(mcip);
	mac_tx_group_teardown(mcip);

	ASSERT((mcip->mci_flent == flent) && (flent->fe_next == NULL));

	/*
	 * Release our hold on the group as well. We need
	 * to check if the shared group has only one client
	 * left who can use it exclusively. Also, if we
	 * were the last client, release the group.
	 */
	group = flent->fe_rx_ring_group;
	default_group = MAC_DEFAULT_RX_GROUP(mip);
	if (group != NULL) {
		mac_group_remove_client(group, mcip);
		next_state = mac_group_next_state(group,
		    &grp_only_mcip, default_group, B_TRUE);
		if (next_state == MAC_GROUP_STATE_RESERVED) {
			/*
			 * Only one client left on this RX group.
			 */
			ASSERT(grp_only_mcip != NULL);
			mac_set_group_state(group,
			    MAC_GROUP_STATE_RESERVED);
			group_only_flent = grp_only_mcip->mci_flent;

			/*
			 * The only remaining client has exclusive
			 * access on the group. Allow it to
			 * dynamically poll the H/W rings etc.
			 */
			mac_rx_group_setup(grp_only_mcip);
			mac_cpu_setup(grp_only_mcip);
			mac_set_rings_effective(grp_only_mcip);
		} else if (next_state == MAC_GROUP_STATE_REGISTERED) {
			/*
			 * This is a non-default group being freed up.
			 * We need to reevaluate the default group
			 * to see if the primary client can get
			 * exclusive access to the default group.
			 */
			ASSERT(group != MAC_DEFAULT_RX_GROUP(mip));
			if (mrp->mrp_mask & MRP_RX_RINGS) {
				MAC_RX_GRP_RELEASED(mip);
				if (mip->mi_rx_group_type ==
				    MAC_GROUP_TYPE_DYNAMIC) {
					MAC_RX_RING_RELEASED(mip,
					    group->mrg_cur_count);
				}
			}
			mac_release_rx_group(mcip, group);
			mac_set_group_state(group,
			    MAC_GROUP_STATE_REGISTERED);
			check_default_group = B_TRUE;
		} else {
			ASSERT(next_state == MAC_GROUP_STATE_SHARED);
			mac_set_group_state(group,
			    MAC_GROUP_STATE_SHARED);
		}
		flent->fe_rx_ring_group = NULL;
	}
	/*
	 * Remove the client from the TX group. Additionally, if
	 * this a non-default group, then we also need to release
	 * the group.
	 */
	group = flent->fe_tx_ring_group;
	default_group = MAC_DEFAULT_TX_GROUP(mip);
	if (group != NULL) {
		mac_group_remove_client(group, mcip);
		next_state = mac_group_next_state(group,
		    &grp_only_mcip, default_group, B_FALSE);
		if (next_state == MAC_GROUP_STATE_REGISTERED) {
			if (group != default_group) {
				if (mrp->mrp_mask & MRP_TX_RINGS) {
					MAC_TX_GRP_RELEASED(mip);
					if (mip->mi_tx_group_type ==
					    MAC_GROUP_TYPE_DYNAMIC) {
						MAC_TX_RING_RELEASED(
						    mip, group->
						    mrg_cur_count);
					}
				}
				mac_release_tx_group(mcip, group);
				/*
				 * If the default group is reserved,
				 * then we need to set the effective
				 * rings as we would have given
				 * back some rings when the group
				 * was released
				 */
				if (mip->mi_tx_group_type ==
				    MAC_GROUP_TYPE_DYNAMIC &&
				    default_group->mrg_state ==
				    MAC_GROUP_STATE_RESERVED) {
					grp_only_mcip =
					    MAC_GROUP_ONLY_CLIENT
					    (default_group);
					mac_set_rings_effective(
					    grp_only_mcip);
				}
			}
		} else if (next_state == MAC_GROUP_STATE_RESERVED) {
			mac_set_rings_effective(grp_only_mcip);
		}
		flent->fe_tx_ring_group = NULL;
		group->mrg_state = next_state;
	}

	/*
	 * The mac client using the default group gets exclusive access to the
	 * default group if and only if it is the sole client on the entire
	 * mip.
	 */
	if (check_default_group) {
		default_group = MAC_DEFAULT_RX_GROUP(mip);
		ASSERT(default_group->mrg_state == MAC_GROUP_STATE_SHARED);
		next_state = mac_group_next_state(default_group,
		    &grp_only_mcip, default_group, B_TRUE);
		if (next_state == MAC_GROUP_STATE_RESERVED) {
			ASSERT(grp_only_mcip != NULL &&
			    mip->mi_nactiveclients == 1);
			mac_set_group_state(default_group,
			    MAC_GROUP_STATE_RESERVED);
			mac_rx_group_setup(grp_only_mcip);
			mac_cpu_setup(grp_only_mcip);
			mac_set_rings_effective(grp_only_mcip);
		}
	}

	/*
	 * If the primary is the only one left and the MAC supports
	 * dynamic grouping, we need to see if the primary needs to
	 * be moved to the default group so that it can use all the
	 * H/W rings.
	 */
	if (!(flent->fe_type & FLOW_PRIMARY_MAC) &&
	    (mip->mi_nactiveclients == 1) &&
	    (mip->mi_rx_group_type == MAC_GROUP_TYPE_DYNAMIC)) {
		default_group = MAC_DEFAULT_RX_GROUP(mip);
		grp_only_mcip = mac_primary_client_handle(mip);
		if (grp_only_mcip == NULL)
			goto done;

		group_only_flent = grp_only_mcip->mci_flent;
		mrp = MCIP_RESOURCE_PROPS(grp_only_mcip);
		/*
		 * If the primary has an explicit property set, leave it
		 * alone.
		 */
		if (mrp->mrp_mask & MRP_RX_RINGS)
			goto done;
		/*
		 * Switch the primary to the default group.
		 */
		(void) mac_rx_switch_group(grp_only_mcip,
		    group_only_flent->fe_rx_ring_group, default_group);
	}
done:
	i_mac_setup_exit(mip);
	mac_tx_client_quiesce((mac_client_handle_t)mcip);
}

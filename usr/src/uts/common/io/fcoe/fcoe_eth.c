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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/inttypes.h>
#include <sys/strsun.h>
#include <sys/mac_client.h>
#include <sys/vlan.h>
#include <sys/ddi.h>

/*
 * FCoE header files
 */
#include <sys/fcoe/fcoeio.h>
#include <sys/fcoe/fcoe_common.h>

/*
 * Driver's own header files
 */
#include <fcoe.h>
#include <fcoe_eth.h>
#include <fcoe_fc.h>

/*
 * FIP descriptors length field value corresponding to the order in
 * fip_descrpt_type_val_t:
 * entry 0 does not exist
 */
static uint8_t  fip_descrpt_len_val[FIP_DESC_TYPE_VAL_MAX] =
	{0, 1, 2, 2, 3, 4, 1, 36, 38, 11, 33, 5, 2, 3, 1, 40};
static uint8_t all_enode_macs[ETHERADDRL] =
	{0x01, 0x10, 0x18, 0x01, 0x00, 0x01};
static uint8_t all_fcf_macs[ETHERADDRL] =
	{0x01, 0x10, 0x18, 0x01, 0x00, 0x02};

static void fcoe_rx(void *arg, mac_resource_handle_t mrh,
    mblk_t *mp, boolean_t loopback);
static boolean_t fcoe_update_fcf_list(fcoe_mac_t *mac,
    fcoe_fip_descriptor_t **descrpt,
    int avalogin, int solicited);
void fcoe_initiate_descriptor(fcoe_mac_t *mac,
    fcoe_fip_descriptor_t *descrpt, fip_descrpt_type_val_t descrpt_type,
    fip_frm_type_t frm_type);

/*
 * Global variable definitions
 */

/*
 * Internal tunable, used to enable p2p mode
 */
volatile uint32_t	fcoe_enable_p2pmode = 0;

int
fcoe_open_mac(fcoe_mac_t *mac, int force_promisc, fcoeio_stat_t *err_detail)
{
	int		ret;
	char		cli_name[MAXNAMELEN];
	mac_diag_t	diag;
	uint16_t	fm_open_flag = 0;
	*err_detail = 0;

	/*
	 * Open MAC interface
	 */
	ret = mac_open_by_linkid(mac->fm_linkid, &mac->fm_handle);
	if (ret != 0) {
		FCOE_LOG("fcoe", "mac_open_by_linkname %d failed %x",
		    mac->fm_linkid, ret);
		return (FCOE_FAILURE);
	}

	(void) sprintf(cli_name, "%s-%d", "fcoe", mac->fm_linkid);

	ret = mac_client_open(mac->fm_handle,
	    &mac->fm_cli_handle, cli_name, fm_open_flag);
	if (ret != 0) {
		(void) fcoe_close_mac(mac);
		return (FCOE_FAILURE);
	}
	/*
	 * Cache the pointer of the immutable MAC inforamtion and
	 * the current and primary MAC address
	 */
	mac_unicast_primary_get(mac->fm_handle, mac->fm_primary_addr);
	bcopy(mac->fm_primary_addr, mac->fm_current_addr,
	    ETHERADDRL);

	if (mac_unicast_add(mac->fm_cli_handle, NULL, MAC_UNICAST_PRIMARY,
	    &mac->fm_unicst_handle, 0, &diag)) {
		(void) fcoe_close_mac(mac);
		return (FCOE_FAILURE);
	}

	if (fcoe_enable_p2pmode && !force_promisc) {
		force_promisc = 1;
	}

	if (force_promisc) {
		mac->fm_force_promisc = B_TRUE;
	}

	/* Get mtu */
	mac_sdu_get(mac->fm_handle, NULL, &mac->fm_eport.eport_mtu);
	if (mac->fm_eport.eport_mtu < FCOE_MIN_MTU_SIZE) {
		if (!fcoe_enable_p2pmode || mac->fm_eport.eport_mtu < 1500) {
			/*
			 * Fail open if fail to get mtu, or we are not
			 * using p2p, or we are using p2p, but
			 * the mtu is too small
			 */
			(void) fcoe_close_mac(mac);
			*err_detail = FCOEIOE_NEED_JUMBO_FRAME;
			return (FCOE_FAILURE);
		}
	}

	/* Get FCoE offload capabilities */

	(void) mac_capab_fcoe_get(mac->fm_cli_handle,
	    &mac->fm_eport.offload_capab.capab_fcoe_flags,
	    &mac->fm_eport.offload_capab.fcoe_max_lso_size,
	    &mac->fm_eport.offload_capab.fcoe_max_lro_size,
	    &mac->fm_eport.offload_capab.fcoe_min_lro_xchgid,
	    &mac->fm_eport.offload_capab.fcoe_max_lro_xchgid);

	mac->fm_eport.eport_link_speed =
	    mac_client_stat_get(mac->fm_cli_handle, MAC_STAT_IFSPEED);

	mac->fm_running = B_TRUE;

	mac->fm_fip_mode = B_TRUE;
	mac->fm_fip_stage = FIP_STAGE_PRE_FIP;

	return (FCOE_SUCCESS);
}

int
fcoe_close_mac(fcoe_mac_t *mac)
{
	int ret;

	if (mac->fm_handle == NULL) {
		return (FCOE_SUCCESS);
	}

	if (mac->fm_promisc_handle != NULL) {
		mac_promisc_remove(mac->fm_promisc_handle);
		mac->fm_promisc_handle = NULL;
	} else {
		mac_rx_clear(mac->fm_cli_handle);
	}

	if (mac->fm_notify_handle != NULL) {
		ret = mac_notify_remove(mac->fm_notify_handle, B_TRUE);
		ASSERT(ret == 0);
		mac->fm_notify_handle = NULL;
	}

	if (mac->fm_unicst_handle != NULL) {
		(void) mac_unicast_remove(mac->fm_cli_handle,
		    mac->fm_unicst_handle);
		mac->fm_unicst_handle = NULL;
	}

	mutex_enter(&mac->fm_mutex);
	if (mac->fm_running == B_TRUE) {
		mac->fm_running = B_FALSE;
	}
	while (mac->fm_event_cnt != 0) {
		mutex_exit(&mac->fm_mutex);
		delay(drv_usectohz(10000));
		mutex_enter(&mac->fm_mutex);
	}
	mutex_exit(&mac->fm_mutex);

	mac_client_close(mac->fm_cli_handle, 0);
	mac->fm_cli_handle = NULL;

	FCOE_LOG("FCOE", "mac is to be closed");

	(void) mac_close(mac->fm_handle);
	mac->fm_handle = NULL;

	/* clean up fcoe fip fcf list */
	while (!list_is_empty(&mac->fm_fip_fcf)) {
		fcoe_fip_fcf_t  *fcf;

		fcf = list_head(&mac->fm_fip_fcf);
		if (fcf != NULL) {
			list_remove(&(mac->fm_fip_fcf), fcf);
		} else {
			break;
		}
		kmem_free(fcf, sizeof (fcoe_fip_fcf_t));
	}

	return (FCOE_SUCCESS);
}

int
fcoe_enable_callback(fcoe_mac_t *mac)
{
	int ret;

	/*
	 * Set message callback
	 */
	if (mac->fm_force_promisc) {
		ret = mac_promisc_add(mac->fm_cli_handle,
		    MAC_CLIENT_PROMISC_FILTERED, fcoe_rx, mac,
		    &mac->fm_promisc_handle,
		    MAC_PROMISC_FLAGS_NO_TX_LOOP);
		if (ret != 0) {
			FCOE_LOG("foce", "mac_promisc_add on %d failed %x",
			    mac->fm_linkid, ret);
			return (FCOE_FAILURE);
		}
	} else {
		mac_rx_set(mac->fm_cli_handle, fcoe_rx, mac);
		ret = mac_multicast_add(mac->fm_cli_handle, all_enode_macs);
		if (ret != 0) {
			FCOE_LOG("foce", "mac_multicast_add on %d failed %x",
			    mac->fm_linkid, ret);
			return (FCOE_FAILURE);
		}
	}

	/* Get the link state, if it's up, we will need to notify client */
	mac->fm_link_state =
	    mac_stat_get(mac->fm_handle, MAC_STAT_LINK_UP)?
	    FCOE_MAC_LINK_STATE_UP:FCOE_MAC_LINK_STATE_DOWN;

	mac->fm_eport.eport_link_speed =
	    mac_client_stat_get(mac->fm_cli_handle, MAC_STAT_IFSPEED);

	/*
	 * Add a notify function so that we get updates from MAC
	 */
	mac->fm_notify_handle = mac_notify_add(mac->fm_handle,
	    fcoe_mac_notify, (void *)mac);
	return (FCOE_SUCCESS);
}

int
fcoe_disable_callback(fcoe_mac_t *mac)
{
	int ret;
	if (mac->fm_promisc_handle) {
		mac_promisc_remove(mac->fm_promisc_handle);
		mac->fm_promisc_handle = NULL;
	} else {
		mac_multicast_remove(mac->fm_cli_handle, all_enode_macs);
		mac_rx_clear(mac->fm_cli_handle);
	}

	if (mac->fm_notify_handle) {
		ret = mac_notify_remove(mac->fm_notify_handle, B_TRUE);
		ASSERT(ret == 0);
		mac->fm_notify_handle = NULL;
	}

	ret = fcoe_mac_set_address(&mac->fm_eport,
	    mac->fm_primary_addr, B_FALSE);
	FCOE_SET_DEFAULT_FPORT_ADDR(mac->fm_eport.eport_efh_dst);
	return (ret);
}

/* ARGSUSED */
static void
fcoe_rx(void *arg, mac_resource_handle_t mrh, mblk_t *mp, boolean_t loopback)
{
	fcoe_mac_t	*mac = (fcoe_mac_t *)arg;
	mblk_t		*next, *next_cont, *mp_payload;
	fcoe_frame_t	*frm;
	uint32_t	raw_frame_size, frame_size;
	uint16_t	frm_type;
	int	payload_size = 0;

	while (mp != NULL) {
		next = mp->b_next;
		mp->b_next = NULL;
		frm_type = ntohs(*(uint16_t *)((uintptr_t)mp->b_rptr + 12));

		if (frm_type == ETHERTYPE_VLAN) {
			mp = mac_strip_vlan_tag(mp);
			frm_type = ntohs(*(uint16_t *)((uintptr_t)mp->b_rptr +
			    VLAN_ID_SIZE));
		}

		if ((frm_type != ETHERTYPE_FCOE) &&
		    (frm_type != ETHERTYPE_FIP)) {
			/*
			 * This mp will not be processed in FCoE, we free it
			 */
			freeb(mp);
			mp = next;
			continue;
		}

		raw_frame_size = MBLKL(mp);
		if (frm_type == ETHERTYPE_FCOE) {
			frame_size = raw_frame_size - PADDING_SIZE;
			if (FCOE_MAC_LRO_ENABLED(mac) && (mp->b_cont != NULL)) {
				/* Receive LRO */
				mp_payload = mp->b_cont;
				payload_size = 0;
				while (mp_payload != NULL) {
					next_cont = mp_payload->b_cont;
					payload_size += MBLKL(mp_payload);
					mp_payload = next_cont;
				}

				frm = fcoe_allocate_frame_lro(&mac->fm_eport,
				    mp, payload_size);
				if (frm != NULL) {
					frm->frm_clock = CURRENT_CLOCK;
					fcoe_post_frame(frm);
				}
			} else {
				frm = fcoe_allocate_frame(&mac->fm_eport,
				    frame_size, mp);
				if (frm != NULL) {
					frm->frm_clock = CURRENT_CLOCK;
					fcoe_post_frame(frm);
				}
			}
		} else {
			/* FIP */
			fcoe_fip_frm_t *fip_frm =
			    fcoe_allocate_fip(mac, raw_frame_size, mp);
			if (fip_frm != NULL) {
				if (fip_frm->frm_op_header->fip_subcode
				    == FIP_SUBCODE_REQ) {
					/* ENode does not accept requests */
					fcoe_free_fip(fip_frm, 1);
					return;
				}
				fcoe_post_fip_event(fip_frm->frm_mac,
				    FCOE_EVENT_MAC_LIAFFECT_FIPFRM_DONE,
				    (uint64_t)(uintptr_t)fip_frm);
			}
		}
		mp = next;
	}
}

void
fcoe_mac_notify(void *arg, mac_notify_type_t type)
{
	fcoe_mac_t *mac = (fcoe_mac_t *)arg;

	/*
	 * We assume that the calls to this notification callback are serialized
	 * by MAC layer
	 */
	switch (type) {
	case MAC_NOTE_LINK:
		/*
		 * This notification is sent every time the MAC driver
		 * updates the link state.
		 */
		if (mac_stat_get(mac->fm_handle, MAC_STAT_LINK_UP) != 0) {
			if (mac->fm_link_state == FCOE_MAC_LINK_STATE_UP) {
				break;
			}
			/* Get speed */
			mac->fm_eport.eport_link_speed =
			    mac_client_stat_get(mac->fm_cli_handle,
			    MAC_STAT_IFSPEED);
			(void) fcoe_mac_set_address(&mac->fm_eport,
			    mac->fm_primary_addr, B_FALSE);
			FCOE_SET_DEFAULT_FPORT_ADDR(
			    mac->fm_eport.eport_efh_dst);
			mac->fm_fip_mode = B_TRUE;
			mac->fm_fip_stage = FIP_STAGE_PRE_FIP;

			fcoe_post_fip_event(mac, FCOE_EVENT_MAC_LINK_UP, NULL);
			FCOE_LOG(NULL,
			    "fcoe_mac_notify: link/%d mac/%p LINK up",
			    mac->fm_linkid, arg);
		} else {
			mac->fm_fip_stage = FIP_STAGE_PRE_FIP;

			if (mac->fm_link_state ==
			    FCOE_MAC_LINK_STATE_DOWN) {
				break;
			}
			if (mac->fm_fip_mode == B_TRUE) {
				fcoe_post_fip_event(mac,
				    FCOE_EVENT_MAC_LINK_DOWN, NULL);
			} else {
				mac->fm_link_state = FCOE_MAC_LINK_STATE_DOWN;
				mac->fm_fip_mode = B_TRUE;
				fcoe_mac_notify_link_down(mac);
			}
			FCOE_LOG(NULL,
			    "fcoe_mac_notify: link/%d arg/%p LINK down",
			    mac->fm_linkid, arg);
		}
		break;

	case MAC_NOTE_TX:
		/*
		 * MAC is not so busy now, then wake up fcoe_tx_frame to try
		 */
		mutex_enter(&mac->fm_mutex);
		cv_broadcast(&mac->fm_tx_cv);
		mutex_exit(&mac->fm_mutex);

		FCOE_LOG("fcoe_mac_notify", "wake up");
		break;

	default:
		break;
	}
}

int
fcoe_mac_set_address(fcoe_port_t *eport, uint8_t *addr, boolean_t fc_assigned)
{
	fcoe_mac_t	*mac = EPORT2MAC(eport);
	int		ret;

	if (bcmp(addr, mac->fm_current_addr, 6) == 0) {
		return (FCOE_SUCCESS);
	}

	mutex_enter(&mac->fm_mutex);
	if (mac->fm_promisc_handle == NULL) {
		ret = mac_unicast_primary_set(mac->fm_handle, addr);
		if (ret != 0) {
			mutex_exit(&mac->fm_mutex);
			FCOE_LOG("fcoe", "mac_unicast_primary_set on %d "
			    "failed %x", mac->fm_linkid, ret);
			return (FCOE_FAILURE);
		}
	}
	if (fc_assigned) {
		bcopy(addr, mac->fm_current_addr, ETHERADDRL);
	} else {
		bcopy(mac->fm_primary_addr,
		    mac->fm_current_addr, ETHERADDRL);
	}
	mutex_exit(&mac->fm_mutex);
	return (FCOE_SUCCESS);
}

/*
 * allocate mblk (and raw frame buffer) for solicited frames only
 */
fcoe_fip_frm_t *
fcoe_allocate_fip(fcoe_mac_t *mac, uint32_t fc_frame_size, void *xmp)
{
	fcoe_fip_frm_t	*frm;
	mblk_t		*mp = (mblk_t *)xmp;

	if (mp == NULL) {
		mp = fcoe_get_fip_mblk(mac, fc_frame_size);
		if (mp == NULL) {
			return (NULL);
		}
	}

	/*
	 * Do fcoe_fip_frm_t initialization
	 */
	frm = (fcoe_fip_frm_t *)kmem_alloc(sizeof (fcoe_fip_frm_t), KM_SLEEP);
	frm->frm_mac = mac;
	frm->frm_retry = 0;
	frm->frm_netb = mp;

	frm->frm_eth = (void *)mp->b_rptr;
	frm->frm_ffh = (fcoe_fip_eth_ver_t *)((uint8_t *)frm->frm_eth +
	    sizeof (struct ether_header));
	frm->frm_op_header = (fcoe_fip_op_header_t *)((uint8_t *)frm->frm_ffh +
	    sizeof (fcoe_fip_eth_ver_t));
	frm->frm_op_desc_list_header = (fcoe_fip_descriptor_t *)
	    ((uint8_t *)frm->frm_op_header + sizeof (fcoe_fip_op_header_t));
	frm->frm_pad = NULL; /* not assigned yet */

	return (frm);
}

void
fcoe_free_fip(fcoe_fip_frm_t *frm, uint8_t freenetb)
{
	if ((frm->frm_netb != NULL) && freenetb == 1) {
		freemsg((mblk_t *)frm->frm_netb);
		frm->frm_netb = NULL;
	}
	kmem_free(frm, sizeof (fcoe_fip_frm_t));
}

/*
 * Create FIP frames for ENode
 */

fcoe_fip_frm_t *
fcoe_initiate_fip_req(fcoe_mac_t *mac, uint32_t fip_frame_type)
{
	uint16_t	descript_len = 0;
	uint16_t	frame_len = 0;
	fcoe_fip_frm_t *frm;
	fcoe_fip_descriptor_t	*descrpt = NULL;
	fip_descrpt_type_val_t	descrpt_type;
	uint8_t		i;
	fip_frm_type_t	frm_type;
	uint16_t	fip_op_code = (fip_frame_type & 0xffff0000) >> 16;
	uint8_t		fip_op_subcode = (fip_frame_type & 0x0000ff00) >> 8;
	/*
	 * Descriptor list for Discovery solicitation, FLOGI req, NPIV FDISC
	 * req, LOGO req, Keep Alive req, corresponding to FIP_FRAME
	 * The array is defined here as const and with 4 as the size limit
	 * when spec get updated, this code should be taken care of
	 */
	fip_descrpt_type_val_t	descrpt_type_list[][4] =
	    {{FIP_DESC_TYPE_VAL_MACADDR, FIP_DESC_TYPE_VAL_NAMEIDENTIFIER,
	    FIP_DESC_TYPE_VAL_MAXRCVSIZE, 0},
	    {FIP_DESC_TYPE_VAL_FLOGI, FIP_DESC_TYPE_VAL_MACADDR, 0, 0},
	    {FIP_DESC_TYPE_VAL_NPIVFDISC, FIP_DESC_TYPE_VAL_MACADDR, 0, 0},
	    {FIP_DESC_TYPE_VAL_LOGO, FIP_DESC_TYPE_VAL_MACADDR, 0, 0},
	    {FIP_DESC_TYPE_VAL_MACADDR, 0, 0, 0}};

	ASSERT(fip_op_subcode == FIP_SUBCODE_REQ);

	frm_type = fip_frame_type & 0x000000ff;
	for (i = 0, descrpt_type = descrpt_type_list[frm_type][0];
	    descrpt_type != 0;
	    descrpt_type = descrpt_type_list[frm_type][++i]) {
		descript_len += fip_descrpt_len_val[descrpt_type];
	}
	frame_len = PADDING_FIP_HEADER_SIZE + descript_len * 4;

	frm = fcoe_allocate_fip(mac, frame_len, NULL);

	if (frm == NULL) {
		return (NULL);
	}

	frm->frm_type = fip_frame_type;
	/* Fill in ethernet header */
	if (frm_type == FIP_FRAME_KEEP_ALIVE) {
		bcopy(mac->fm_primary_addr,
		    &frm->frm_eth->ether_shost, ETHERADDRL);
	} else {
		bcopy(mac->fm_current_addr,
		    &frm->frm_eth->ether_shost, ETHERADDRL);
	}

	if (frm_type == FIP_FRAME_DISC) {
		mutex_enter(&mac->fm_mutex);
		if (mac->fm_current_fcf == NULL ||
		    (mac->fm_current_fcf != NULL &&
		    mac->fm_state == FCOE_MAC_STATE_OFFLINE)) {
			bcopy(all_fcf_macs,
			    &frm->frm_eth->ether_dhost, ETHERADDRL);
		} else {
			bcopy(mac->fm_current_fcf->fcf_addr,
			    &frm->frm_eth->ether_dhost, ETHERADDRL);
		}
		mutex_exit(&mac->fm_mutex);
	} else {
		bcopy(mac->fm_eport.eport_efh_dst, &frm->frm_eth->ether_dhost,
		    ETHERADDRL);
	}
	frm->frm_eth->ether_type = htons(ETHERTYPE_FIP);

	/* Fill in FIP version */
	frm->frm_ffh->ffe_ver = FCOE_FIP_FRM_VER<<4 & 0xf0;

	/* Fill in Encapsulated FIP operation header */
	FCOE_V2B_2(fip_op_code, frm->frm_op_header->fip_opcode);
	frm->frm_op_header->fip_subcode = fip_op_subcode;
	FCOE_V2B_2(descript_len, frm->frm_op_header->fip_descrpt_len);

	/* Set flags for Discovery Solicitation, FLOGI, NPIV FDISC */
	FCOE_V2B_2(0x8000, &frm->frm_op_header->fip_flags);

	/* Fill in Encapsulated FIP operation descriptors */
	descrpt = frm->frm_op_desc_list_header;
	for (i = 0, descrpt_type = descrpt_type_list[frm_type][0];
	    descrpt_type != 0;
	    descrpt_type = descrpt_type_list[frm_type][++i]) {
		fcoe_initiate_descriptor(mac, descrpt, descrpt_type, frm_type);
		descrpt = (fcoe_fip_descriptor_t *)((uint8_t *)descrpt +
		    fip_descrpt_len_val[descrpt_type] * 4);
	}
	/* Fill in paddings if needed */

	return (frm);
}

void
fcoe_initiate_descriptor(fcoe_mac_t *mac, fcoe_fip_descriptor_t *descrpt,
    fip_descrpt_type_val_t descrpt_type, fip_frm_type_t frm_type)
{
	fcoe_fc_frame_header_t	*fc_frm_header;
	uint8_t			*fc_payload;

	/* Fill in descriptor type and length */
	ASSERT(descrpt_type < FIP_DESC_TYPE_VAL_MAX);
	descrpt->fip_descrpt_type = (uint8_t)descrpt_type;
	descrpt->fip_descrpt_len = fip_descrpt_len_val[descrpt_type];

	/* Fill in descriptor value per type */
	switch (descrpt_type) {
		case FIP_DESC_TYPE_VAL_MACADDR:
			if (frm_type == FIP_FRAME_FLOGI_LS) {
				if (mac->mac_fpma) {
					/* FPMA */
					bzero(descrpt->fip_descrpt_value,
					    ETHERADDRL);
				} else {
					/* SPMA */
					bcopy(mac->fm_primary_addr,
					    descrpt->fip_descrpt_value,
					    ETHERADDRL);
				}
			} else if (frm_type <= FIP_FRAME_NPIVFDISC_LS ||
			    frm_type == FIP_FRAME_KEEP_ALIVE) {
				/*
				 * for discovery solicitation, NPIVFDISC
				 * and VN_PORT Keep Alive
				 */
				bcopy(mac->fm_primary_addr,
				    descrpt->fip_descrpt_value, ETHERADDRL);
			} else {
				/* for LOGO */
				bcopy(mac->fm_current_addr,
				    descrpt->fip_descrpt_value, ETHERADDRL);
			}
			break;
		case FIP_DESC_TYPE_VAL_MAXRCVSIZE:
			FCOE_V2B_2(FCOE_MAX_FRAME_SIZE,
			    descrpt->fip_descrpt_value);
			break;
		case FIP_DESC_TYPE_VAL_FLOGI:
			bzero(descrpt->fip_descrpt_value,
			    descrpt->fip_descrpt_len * 4 - 2);
			fc_frm_header =
			    (fcoe_fc_frame_header_t *)(uint8_t *)
			    (descrpt->fip_descrpt_value+2);
			fc_frm_header->hdr_r_ctl[0] = 0x22;
			FCOE_V2B_3(0xFFFFFE, fc_frm_header->hdr_d_id);
			fc_frm_header->hdr_type[0] = 0x01; /* ELS */
			FCOE_V2B_3(0x290000, fc_frm_header->hdr_f_ctl);
			FCOE_V2B_2(0x0001, fc_frm_header->hdr_oxid);
			FCOE_V2B_2(0xffff, fc_frm_header->hdr_rxid);
			/* set payload */
			fc_payload = (uint8_t *)fc_frm_header +
			    sizeof (fcoe_fc_frame_header_t);
			fc_payload[0] = 0x04; /* ELS_OP_FLOGI; */
			FCOE_V2B_2(0x2008, fc_payload + 4);
			FCOE_V2B_2(0x0003, fc_payload + 6);
			FCOE_V2B_2(0x8800, fc_payload + 8);
			FCOE_V2B_2(0x0800, fc_payload + 10);
			FCOE_V2B_2(0x00ff, fc_payload + 12);
			FCOE_V2B_2(0x0003, fc_payload + 14);
			FCOE_V2B_2(0x0000, fc_payload + 16);
			FCOE_V2B_2(0x07d0, fc_payload + 18);
			/* Port wwn */
			bcopy(&mac->fm_eport.eport_portwwn, fc_payload + 20, 8);
			bcopy(&mac->fm_eport.eport_nodewwn, fc_payload + 28, 8);
			FCOE_V2B_2(0x8800, fc_payload + 68);
			FCOE_V2B_2(0x0800, fc_payload + 74);
			FCOE_V2B_2(0x00ff, fc_payload + 76);
			break;
		case FIP_DESC_TYPE_VAL_LOGO:
			bzero(descrpt->fip_descrpt_value,
			    descrpt->fip_descrpt_len * 4 - 2);
			fc_frm_header =
			    (fcoe_fc_frame_header_t *)(uint8_t *)
			    (descrpt->fip_descrpt_value+2);
			fc_frm_header->hdr_r_ctl[0] = 0x22;
			FCOE_V2B_3(0xFFFFFE, fc_frm_header->hdr_d_id);
			fc_frm_header->hdr_type[0] = 0x01; /* ELS */
			FCOE_V2B_3(0x290000, fc_frm_header->hdr_f_ctl);
			FCOE_V2B_2(0x01, fc_frm_header->hdr_oxid);
			FCOE_V2B_2(0xffff, fc_frm_header->hdr_rxid);
			/* set payload */
			fc_payload = (uint8_t *)fc_frm_header +
			    sizeof (fcoe_fc_frame_header_t);
			fc_payload[0] = 0x05; /* ELS_OP_LOGO; */
			/* Port information */
			bcopy(mac->fm_eport.eport_portid, fc_payload + 5, 3);
			bcopy(mac->fm_eport.eport_portwwn, fc_payload + 8, 8);
			break;
		case FIP_DESC_TYPE_VAL_NAMEIDENTIFIER:
			/* Name Identifier, i.e. ENode Node Name */
			bzero(descrpt->fip_descrpt_value, 2);
			bcopy(mac->fm_eport.eport_nodewwn,
			    descrpt->fip_descrpt_value+2, 8);
			break;
		case FIP_DESC_TYPE_VAL_VXPORTINDENTIFIER:
			/* For VN_PORT Keep Alive Only */
			/* MAC Address */
			bcopy(mac->fm_current_addr,
			    descrpt->fip_descrpt_value, ETHERADDRL);
			/* Reserved */
			bzero(descrpt->fip_descrpt_value+6, 1);
			/* Address Identifier, i.e. VN_PORT FC_ID */
			bcopy(mac->fm_current_addr+3,
			    descrpt->fip_descrpt_value+7, 3);
			/* Port name */
			bzero(descrpt->fip_descrpt_value+10, 8);
			break;
		default:
			FCOE_LOG("FCOE", "Descriptor(type=%x) initilization"
			    "not supported", descrpt_type);
			break;
	}
}

void
fcoe_process_fip_resp(fcoe_fip_frm_t *fip)
{
	fcoe_fc_frame_header_t	*fc_frm_header;
	uint8_t			*fc_payload;
	fcoe_fip_descriptor_t	*descrpt_tmp;
	uint16_t	descrpt_list_len, len = 0;
	uint8_t		i = 0, descrpt_num = 0, free_fip = 0;
	fcoe_fip_frm_t		*frm;
	uint16_t	op_code;
	uint16_t	 alloc_size;
	fcoe_fip_descriptor_t	**descrpt;

	ASSERT(fip->frm_op_header->fip_subcode == FIP_SUBCODE_RSP);

	descrpt_list_len = FCOE_B2V_2(fip->frm_op_header->fip_descrpt_len);
	ASSERT(descrpt_list_len >= 1);	/* At least one descriptor */

	if (fip->frm_mac->fm_link_state == FCOE_MAC_LINK_STATE_DOWN) {
		fcoe_free_fip(fip, 1);
		return;
	}

	descrpt_tmp = fip->frm_op_desc_list_header;
	while (len < descrpt_list_len) {
		/*
		 * Discard the descriptor type value larger
		 * than FIP_DESC_TYPE_VAL_MAX
		 */
		if (descrpt_tmp->fip_descrpt_type < FIP_DESC_TYPE_VAL_MAX) {
			descrpt_num ++;
		}
		len += descrpt_tmp->fip_descrpt_len;
		descrpt_tmp = (fcoe_fip_descriptor_t *)
		    ((uint8_t *)descrpt_tmp +
		    descrpt_tmp->fip_descrpt_len * 4);
	}

	alloc_size = descrpt_num * sizeof (fcoe_fip_descriptor_t *);
	descrpt = (fcoe_fip_descriptor_t **)kmem_alloc(alloc_size, KM_SLEEP);

	descrpt_tmp = fip->frm_op_desc_list_header;
	descrpt_num = 0;
	len = 0;
	while (len < descrpt_list_len) {
		if (descrpt_tmp->fip_descrpt_type < FIP_DESC_TYPE_VAL_MAX) {
			descrpt[descrpt_num] = descrpt_tmp;
			descrpt_num ++;
		}
		len += descrpt_tmp->fip_descrpt_len;
		descrpt_tmp = (fcoe_fip_descriptor_t *)
		    ((uint8_t *)descrpt_tmp +
		    descrpt_tmp->fip_descrpt_len * 4);
	}

	ASSERT(descrpt_num >= 1);

	op_code = FCOE_B2V_2(fip->frm_op_header->fip_opcode);

	switch (op_code) {
	case FIP_OPCODE_DISC:
		/* discovery advertisement (DA) */
		if (descrpt_num != 5) {
			/* Not a complete DA */
			break;
		}

		if ((FIP_DESC_TYPE(descrpt[0]) !=
		    FIP_DESC_TYPE_VAL_PRIORITY) ||
		    (FIP_DESC_TYPE(descrpt[1]) !=
		    FIP_DESC_TYPE_VAL_MACADDR) ||
		    (FIP_DESC_TYPE(descrpt[2]) !=
		    FIP_DESC_TYPE_VAL_NAMEIDENTIFIER) ||
		    (FIP_DESC_TYPE(descrpt[3]) !=
		    FIP_DESC_TYPE_VAL_FABRICNAME) ||
		    (FIP_DESC_TYPE(descrpt[4]) !=
		    FIP_DESC_TYPE_VAL_FKAADVPERIOD)) {
			/* Not a legal DA */
			break;
		}

		if (FIP_F_FLAG(fip) != 1) {
			/* Not from an FCF */
			break;
		}

		if ((FIP_SPMA_FLAG(fip) + FIP_FPMA_FLAG(fip)) != 1) {
			/* Not a valid FP and SP pair set by FCF */
			break;
		}

		if (FIP_S_FLAG(fip) == 1) {
			mutex_enter(&fip->frm_mac->fm_mutex);
			if (fip->frm_mac->fm_current_fcf != NULL &&
			    bcmp(&fip->frm_eth->ether_dhost,
			    fip->frm_mac->fm_primary_addr,
			    ETHERADDRL) != 0) {
				/*
				 * This solicited DA is not for me
				 */
				mutex_exit(&fip->frm_mac->fm_mutex);
				break;
			}
			mutex_exit(&fip->frm_mac->fm_mutex);
		}

		fip->frm_mac->mac_fpma = FIP_FPMA_FLAG(fip);
		if (fcoe_update_fcf_list(fip->frm_mac, descrpt,
		    FIP_A_FLAG(fip), FIP_S_FLAG(fip)) == B_TRUE) {
			if (fip->frm_mac->fm_link_state ==
			    FCOE_MAC_LINK_STATE_WAIT_FCF) {
				fcoe_post_fip_event(fip->frm_mac,
				    FCOE_EVENT_SOFT_LINK_UP, NULL);
				break;
			}
			if (fip->frm_mac->fm_fip_stage ==
			    FIP_STAGE_DO_DISC_SOLICIT) {
				/* ready for FLOGI */
				bcopy(&fip->frm_eth->ether_shost,
				    fip->frm_mac->fm_eport.eport_efh_dst,
				    ETHERADDRL);
				fip->frm_mac->fm_fip_stage ++;
				free_fip = 1;
				fcoe_post_fip_event(fip->frm_mac,
				    FCOE_EVENT_MAC_SEND_FLOGI, NULL);
			}
		}
		break;
	case FIP_OPCODE_LINK:
		/* FLOGI, NPIV FDISC, LOGO */
		if (FIP_DESC_TYPE(descrpt[0]) == FIP_DESC_TYPE_VAL_FLOGI) {
			/* FLOGI */
			uint8_t event_type;

			descrpt_tmp = descrpt[0];
			fc_frm_header = (fcoe_fc_frame_header_t *)
			    (uint8_t *)(descrpt_tmp->fip_descrpt_value + 2);
			fc_payload = (uint8_t *)fc_frm_header +
			    sizeof (fcoe_fc_frame_header_t);
			if (fip->frm_mac->fm_fip_stage !=
			    FIP_STAGE_DO_FLOGI) {
				break;
			}
			free_fip = 1;
			if (fc_payload[0] == 0x02) {
				/* Accept */
				uint8_t *mac_addr;
				fcoe_mac_t *mac = fip->frm_mac;
				ASSERT(descrpt[1]);
				if (FIP_DESC_TYPE(descrpt[1]) !=
				    FIP_DESC_TYPE_VAL_MACADDR) {
					break;
				}
				descrpt_tmp = descrpt[1];
				mac_addr =
				    descrpt_tmp->fip_descrpt_value;
				if (FIP_FPMA_FLAG(fip) == 1) {
				(void) fcoe_mac_set_address(
				    &fip->frm_mac->fm_eport, mac_addr, 1);
				}
				bcopy(fc_frm_header->hdr_d_id,
				    mac->fm_eport.eport_portid, 3);
				bcopy(&fip->frm_eth->ether_shost,
				    mac->fm_eport.eport_efh_dst,
				    ETHERADDRL);
				if ((fc_payload[8]&0x10) == 0) {
					atomic_or_32(&mac->fm_eport.eport_flags,
					    EPORT_FLAG_IS_DIRECT_P2P);
				}
				fip->frm_mac->fm_fip_stage = FIP_STAGE_MAX;
				event_type = FCOE_EVENT_MAC_LINK_INIT_DONE;
			} else {
				event_type = FCOE_EVENT_MAC_LINK_DOWN;
			}
			fcoe_post_fip_event(fip->frm_mac, event_type, NULL);
		} else if (FIP_DESC_TYPE(descrpt[0]) ==
		    FIP_DESC_TYPE_VAL_NPIVFDISC) {
			FCOE_LOG("FCOE", "NPIV FDISC is not supported yet");
		} else if (FIP_DESC_TYPE(descrpt[0]) ==
		    FIP_DESC_TYPE_VAL_LOGO) {
			descrpt_tmp = descrpt[0];
			fc_frm_header = (fcoe_fc_frame_header_t *)
			    descrpt_tmp->fip_descrpt_value;
			fc_payload = (uint8_t *)fc_frm_header +
			    sizeof (fcoe_fc_frame_header_t);
			if (fc_payload[0] == 0x02) {
				FCOE_LOG("FCOE", "LOGO is accepted");
				fcoe_post_fip_event(fip->frm_mac,
				    FCOE_EVENT_MAC_LINK_DOWN, NULL);
			} else {
				FCOE_LOG("FCOE", "LOGO is rejected");
			}
		}
		break;
	case FIP_OPCODE_KA:
		if (FIP_DESC_TYPE(descrpt[0]) != FIP_DESC_TYPE_VAL_MACADDR) {
			break;
		}
		descrpt_tmp = descrpt[0];
		if (memcmp(fip->frm_mac->fm_eport.eport_efh_dst,
		    descrpt_tmp->fip_descrpt_value,
		    ETHERADDRL)) {
			break;
		}

		/* descrpt[1] is Name_identifier */
		i = 2;
		while (i < descrpt_num) {
			descrpt_tmp = descrpt[i];
			if (FIP_DESC_TYPE(descrpt_tmp) !=
			    FIP_DESC_TYPE_VAL_VXPORTINDENTIFIER) {
				break;
			}
			if (memcmp(fip->frm_mac->fm_current_addr,
			    descrpt_tmp->fip_descrpt_value,
			    ETHERADDRL) == 0) {
				FCOE_LOG("FCOE",
				    "Clear Virtural Link received");
				fcoe_post_fip_event(fip->frm_mac,
				    FCOE_EVENT_SOFT_LINK_DOWN, NULL);
				break;
			}
			i++;
		}
		break;
	}
	if (free_fip == 1) {
		fcoe_soft_state_t	*ss = fip->frm_mac->fm_ss;
		for (frm = list_head(&ss->ss_pfip_list); frm;
		    frm = list_next(&ss->ss_pfip_list, frm)) {
			if (frm->frm_mac == fip->frm_mac) {
				list_remove(&ss->ss_pfip_list, frm);
				frm->frm_mac->fm_frm_cnt--;
				fcoe_free_fip(frm, 0);
				break;
			}
		}
	}

	fcoe_free_fip(fip, 1);
	kmem_free(descrpt, alloc_size);
}

/*
 * Return B_TRUE when finding an avaliable FCF
 */

static boolean_t
fcoe_update_fcf_list(fcoe_mac_t *mac, fcoe_fip_descriptor_t **descrpt,
    int avalogin, int solicited)
{
	fcoe_fip_fcf_t	*fcf;
	uint32_t	helptoshow;

	mutex_enter(&mac->fm_mutex);
	for (fcf = list_head(&mac->fm_fip_fcf); fcf;
	    fcf = list_next(&mac->fm_fip_fcf, fcf)) {
		if ((memcmp(fcf->fcf_addr, (*(descrpt+1))->fip_descrpt_value,
		    ETHERADDRL) == 0) &&
		    (memcmp(fcf->fcf_NameID,
		    (*(descrpt+2))->fip_descrpt_value+2, 8) == 0) &&
		    (memcmp(fcf->fcf_FCMAP,
		    (*(descrpt+3))->fip_descrpt_value+3, 3) == 0) &&
		    (memcmp(fcf->fcf_FName,
		    (*(descrpt+3))->fip_descrpt_value+6, 8) == 0)) {
			fcf->fcf_AvaforLogin =
			    (avalogin != 0 ? B_TRUE : B_FALSE);
			fcf->fcf_pri = *((*descrpt)->fip_descrpt_value+1);

			if (mac->fm_current_fcf == fcf) {
				bcopy((*(descrpt+4))->fip_descrpt_value+2,
				    &helptoshow, 4);
				mac->fm_current_fcf->fcf_timeout =
				    ddi_get_lbolt() +
				    drv_usectohz(2500*ntohl(helptoshow));
				/* period updated, recalculate ka_time */
				if (fcf->fcf_adv_period != ntohl(helptoshow)) {
					fcf->fcf_ka_time = 0;
					fcf->fcf_adv_period = ntohl(helptoshow);
					cv_signal(&mac->fm_ss->ss_mlist_cv);
				}
				if (mac->fm_current_fcf->fcf_MaxSizeVerif
				    == B_TRUE && mac->fm_fip_stage !=
				    FIP_STAGE_DO_DISC_SOLICIT) {
					mutex_exit(&mac->fm_mutex);
					return (B_FALSE);
				}
				if (solicited != 0) {
					if (avalogin != 0) {
						mac->fm_current_fcf->
						    fcf_MaxSizeVerif = B_TRUE;
						mutex_exit(&mac->fm_mutex);
						return (B_TRUE);
					}
					/*
					 * not avaliable for login
					 */
					fcoe_post_fip_event(mac,
					    FCOE_EVENT_SOFT_LINK_DOWN, NULL);
					mutex_exit(&mac->fm_mutex);
					return (B_FALSE);
				}
				mutex_exit(&mac->fm_mutex);
				return (B_FALSE);
			}
		}
	}
	fcf = kmem_zalloc(sizeof (fcoe_fip_fcf_t), KM_SLEEP);
	(void) memcpy(fcf->fcf_addr,
	    (*(descrpt+1))->fip_descrpt_value, ETHERADDRL);
	(void) memcpy(fcf->fcf_NameID, (*(descrpt+2))->fip_descrpt_value+2, 8);
	(void) memcpy(fcf->fcf_FCMAP, (*(descrpt+3))->fip_descrpt_value+3, 3);
	(void) memcpy(fcf->fcf_FName, (*(descrpt+3))->fip_descrpt_value+6, 8);
	fcf->fcf_MaxSizeVerif = B_FALSE;
	fcf->fcf_AvaforLogin = (avalogin != 0 ? B_TRUE : B_FALSE);
	fcf->fcf_pri = *((*descrpt)->fip_descrpt_value+1);
	list_insert_tail(&mac->fm_fip_fcf, fcf);

	if (avalogin != 0 && solicited == 0 &&
	    (mac->fm_current_fcf == NULL) &&
	    ((mac->fm_link_state == FCOE_MAC_LINK_STATE_WAIT_FCF) ||
	    (mac->fm_link_state == FCOE_MAC_LINK_STATE_DOWN))) {
		mac->fm_current_fcf = fcf;
		mutex_exit(&mac->fm_mutex);
		return (B_TRUE);
	}

	if (avalogin != 0 && solicited != 0 &&
	    (mac->fm_link_state == FCOE_MAC_LINK_STATE_UP)) {
		if (mac->fm_current_fcf == NULL) {
			/*
			 * The is the first solicited DA replying to
			 * my broadcasted DS
			 */
			bcopy((*(descrpt+4))->fip_descrpt_value+2,
			    &helptoshow, 4);
			fcf->fcf_adv_period = ntohl(helptoshow);
			fcf->fcf_timeout =
			    ddi_get_lbolt() +
			    drv_usectohz(2500*ntohl(helptoshow));
			fcf->fcf_MaxSizeVerif = B_TRUE;
			mac->fm_current_fcf = fcf;
			mutex_exit(&mac->fm_mutex);
			return (B_TRUE);
		}
	}

	mutex_exit(&mac->fm_mutex);
	return (B_FALSE);
}



/*
 * When there is one valid fcf, fm_current_fcf gets set and B_TRUE is returned
 */
boolean_t
fcoe_select_fcf(fcoe_mac_t *mac)
{
	fcoe_fip_fcf_t	*fcf;
	fcoe_fip_fcf_t	*selected;

	mutex_enter(&mac->fm_mutex);

	if (list_is_empty(&mac->fm_fip_fcf)) {
		mutex_exit(&mac->fm_mutex);
		return (B_FALSE);
	}

	for (selected = fcf = list_head(&mac->fm_fip_fcf); fcf;
	    fcf = list_next(&mac->fm_fip_fcf, fcf)) {
		if (fcf->fcf_AvaforLogin == B_TRUE &&
		    fcf->fcf_pri < selected->fcf_pri) {
			selected = fcf;
		}
	}

	if (selected->fcf_AvaforLogin != B_TRUE) {
		mutex_exit(&mac->fm_mutex);
		return (B_FALSE);
	} else {
		/* We are going to send DS to selected */
		selected->fcf_MaxSizeVerif = B_FALSE;
		mac->fm_current_fcf = selected;
		mutex_exit(&mac->fm_mutex);
		return (B_TRUE);
	}
}

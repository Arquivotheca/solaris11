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

/*
 * This file defines interfaces between fcoe and its clients (FCoEI/FCoET)
 */

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/byteorder.h>
#include <sys/atomic.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/crc32.h>
#include <sys/fcntl.h>
#include <sys/unistd.h>
#include <sys/mac_client.h>
#include <sys/pattr.h>
#include <sys/strsun.h>
#include <sys/strsubr.h>

/*
 * FCoE header files
 */
#include <sys/fcoe/fcoeio.h>
#include <sys/fcoe/fcoe_common.h>

/*
 * Driver's own header files
 */
#include <fcoe.h>
#include <fcoe_fc.h>
#include <fcoe_eth.h>

static void fcoe_fill_frame_headers(fcoe_frame_t *frm);
static void fcoe_fill_frame_tailers(fcoe_frame_t *frm);
static void fcoe_deregister_client(fcoe_port_t *eport);
static int fcoe_ctl(fcoe_port_t *eport, int cmd, void *arg);
static void fcoe_tx_frame(fcoe_frame_t *frm);
static void *fcoe_alloc_netb(fcoe_port_t *eport,
    uint32_t fc_frame_size, uint8_t **ppfc);

/*
 * Only this function will be called explicitly by clients
 * Register the specified client port (fcoei/fcoet)
 */
fcoe_port_t *
fcoe_register_client(fcoe_client_t *client)
{
	fcoe_mac_t	*mac;
	fcoe_port_t	*eport;

	if (client->ect_fcoe_ver != fcoe_ver_now) {
		cmn_err(CE_WARN, "FCoE modules version mismatch, "
		    "fail registering client.");
		return (NULL);
	}

	/*
	 * We will not come here, when someone is changing ss_mac_list,
	 * so it's safe to go through ss_mac_list.
	 */
	mutex_enter(&fcoe_global_ss->ss_mlist_mutex);
	for (mac = list_head(&fcoe_global_ss->ss_mac_list); mac;
	    mac = list_next(&fcoe_global_ss->ss_mac_list, mac)) {
		if (client->ect_channelid == mac->fm_linkid) {
			break;
		}
	}
	mutex_exit(&fcoe_global_ss->ss_mlist_mutex);
	if (mac == NULL) {
		FCOE_LOG(0, "can't find the MAC you want to bind");
		return (NULL);
	}

	if (mac->fm_flags & FCOE_MAC_FLAG_BOUND) {
		FCOE_LOG(0, "the MAC you want to bind is bound already");
		return (NULL);
	}

	atomic_or_32(&mac->fm_flags, FCOE_MAC_FLAG_BOUND);
	bcopy(client, &mac->fm_client, sizeof (fcoe_client_t));

	/*
	 * fcoe_port_t initialization
	 */
	eport = &mac->fm_eport;
	eport->eport_flags = client->ect_eport_flags | EPORT_FLAG_MAC_IN_USE;
	eport->eport_fcoe_private = mac;
	eport->eport_client_private = client->ect_client_port_struct;
	eport->eport_max_fc_frame_size = 2136;
	eport->eport_tx_frame = fcoe_tx_frame;
	eport->eport_alloc_frame = fcoe_allocate_frame;
	eport->eport_release_frame = fcoe_release_frame;
	eport->eport_alloc_netb = fcoe_alloc_netb;
	eport->eport_free_netb = fcoe_free_netb;
	eport->eport_deregister_client = fcoe_deregister_client;
	eport->eport_ctl = fcoe_ctl;
	eport->eport_set_mac_address = fcoe_mac_set_address;
	eport->eport_fcoe_setup_lro = fcoe_mac_setup_lro;
	eport->eport_fcoe_cancel_lro = fcoe_mac_cancel_lro;

	return (eport);
}

/*
 * The following routines will be called through vectors in fcoe_port_t
 */

/*
 * Deregister fcoet/fcoei modules, client should make sure the port is in
 * offline status already
 */
static void
fcoe_deregister_client(fcoe_port_t *eport)
{
	fcoe_mac_t	*mac = EPORT2MAC(eport);

	/*
	 * Wait for all the related frame to be freed, this should be fast
	 * because before deregister fcoei/fcoet will make sure its port
	 * is already in offline status so no frame will be received or sent
	 * any more
	 */
	while (mac->fm_frm_cnt > 0) {
		delay(10);
	}

	atomic_and_32(&EPORT2MAC(eport)->fm_flags, ~FCOE_MAC_FLAG_BOUND);
	atomic_and_32(&mac->fm_eport.eport_flags, ~EPORT_FLAG_MAC_IN_USE);
	if (!(EPORT2MAC(eport)->fm_flags & FCOE_MAC_FLAG_USER_DEL)) {
		(void) fcoe_close_mac(mac);
		fcoe_destroy_mac(mac);
	}
}

/* ARGSUSED */
static int
fcoe_ctl(fcoe_port_t *eport, int cmd, void *arg)
{
	fcoe_mac_t	*mac = EPORT2MAC(eport);

	switch (cmd) {
		case FCOE_CMD_PORT_ONLINE:
			/*
			 * client ask us to online, so it's safe to post event
			 * and data up
			 */
			if (fcoe_enable_callback(mac) == FCOE_FAILURE) {
				return (FCOE_FAILURE);
			}
			FCOE_LOG("FCOE", "fcoe_ctl: FCOE_CMD_PORT_ONLINE");
			mac->fm_state = FCOE_MAC_STATE_ONLINE;
			if (mac->fm_fip_mode == B_FALSE &&
			    mac->fm_link_state ==
			    FCOE_MAC_LINK_STATE_UP) {
				(void) ddi_taskq_dispatch(
				    fcoe_global_ss->ss_watchdog_taskq,
				    fcoe_mac_notify_link_up, mac, DDI_SLEEP);
				break;
			}
			/* Fake one mac link notification */
			mac->fm_link_state = FCOE_MAC_LINK_STATE_DOWN;
			fcoe_mac_notify(mac, MAC_NOTE_LINK);
			break;
		case FCOE_CMD_PORT_OFFLINE:
			if (fcoe_disable_callback(mac) == FCOE_FAILURE) {
				return (FCOE_FAILURE);
			}
			FCOE_LOG("FCOE", "fcoe_ctl: FCOE_CMD_PORT_OFFLINE");
			mac->fm_state = FCOE_MAC_STATE_OFFLINE;
			// in case there are threads waiting
			mutex_enter(&mac->fm_mutex);
			cv_broadcast(&mac->fm_tx_cv);
			mutex_exit(&mac->fm_mutex);
			break;
		default:
			FCOE_LOG("fcoe", "fcoe_ctl, unsupported cmd %x", cmd);
			break;
	}

	return (FCOE_SUCCESS);
}

static void
fcoe_fake_els_resp(void *arg)
{
	fcoe_frame_t *frm_req = (fcoe_frame_t *)arg;
	fcoe_client_t *clt = &FRM2MAC(frm_req)->fm_client;
	fcoe_frame_t	*frm;
	fcoe_mac_t	*fmac = EPORT2MAC(frm_req->frm_eport);

	if (fmac->fm_eport.eport_portid[0] == 0) {
		FCOE_LOG("FCOE", "%s: FIP FLOGI not done yet,fmac %p",
		    __func__, fmac);
		fcoe_free_netb(frm_req->frm_netb);
		fcoe_release_frame(frm_req);
		return;
	}

	if (frm_req->frm_payload[0] == 0x04) {
		/* FLOGI */
		if (FRM2MAC(frm_req)->fm_fip_stage >= FIP_STAGE_DO_KA) {
			frm = fcoe_allocate_frame(frm_req->frm_eport,
			    FLOGI_ACC_PAYLOAD_SIZE + FCFH_SIZE, 0);

			bcopy(frm_req->frm_eport->eport_efh_dst,
			    FRM2FMI(frm)->fmi_efh->
			    ether_shost.ether_addr_octet,
			    ETHERADDRL);
			frm->frm_payload[0] = 0x02;

			/* Actually we should store what we saved here */
			frm->frm_payload[4] = 0x20;
			frm->frm_payload[5] = 0x20;
			frm->frm_payload[7] = 0x0A;
			frm->frm_payload[8] = 0x10;
			frm->frm_payload[10] = 0x05;
			frm->frm_payload[11] = 0xAC;
			FCOE_LOG("FCOE", "faked FLOGI ACC to fcoei/fcoet");
		} else {
			frm = fcoe_allocate_frame(frm_req->frm_eport,
			    8 + FCFH_SIZE, 0);
			frm->frm_payload[0] = 0x01;
			frm->frm_payload[5] = 0x09; /* Unable to perform now */
			frm->frm_payload[6] = 0x00; /* No additional explain */
			FCOE_LOG("FCOE", "faked FLOGI RJT to fcoei/fcoet");
		}
	} else if (frm_req->frm_payload[0] == 0x05) {
		/* LOGO */
		frm = fcoe_allocate_frame(frm_req->frm_eport, 4 + FCFH_SIZE, 0);
		frm->frm_payload[0] = 0x02;
		FCOE_LOG("FCOE", "faked LOGO ACC");
	} else {
		FCOE_LOG("FCOE", "unknown frame type");
		fcoe_free_netb(frm_req->frm_netb);
		fcoe_release_frame(frm_req);
		return;
	}
	FFM_R_CTL(0x23, frm);
	FFM_TYPE(0x01, frm);
	FFM_F_CTL(0x980000, frm);
	FFM_OXID(FRM_OXID(frm_req), frm);
	FFM_RXID(0xffff, frm);
	FFM_S_ID(0xfffffe, frm);
	FFM_D_ID(FCOE_B2V_3(fmac->fm_eport.eport_portid), frm);

	FCOE_LOG("FCOE", "faked one, frm=%p,frm_payload[8]=%x",
	    frm, frm->frm_payload[8]);
	fcoe_free_netb(frm_req->frm_netb);
	mutex_enter(&FRM2MAC(frm_req)->fm_ss->ss_watch_mutex);
	list_insert_tail(&FRM2MAC(frm_req)->fm_ss->ss_pfrm_list,
	    FRM2FMI(frm_req));
	FRM2MAC(frm_req)->fm_frm_cnt ++;
	if (FRM2MAC(frm_req)->fm_ss->ss_flags & SS_FLAG_DOG_WAITING) {
		cv_signal(&FRM2MAC(frm_req)->fm_ss->ss_watch_cv);
	}
	mutex_exit(&FRM2MAC(frm_req)->fm_ss->ss_watch_mutex);

	clt->ect_rx_frame(frm);
}

/*
 * Transmit the specified frame to the link
 */
static void
fcoe_tx_frame(fcoe_frame_t *frm)
{
	mblk_t		*ret_mblk = NULL;
	fcoe_mac_t	*mac = FRM2MAC(frm);
	fcoe_i_frame_t	*fmi = FRM2FMI(frm);
	mac_tx_cookie_t	ret_cookie;
	uint32_t	fcp_data_payload_size;
	uint16_t 	mss;
	uint8_t		sof, eof;
	uint16_t	ox_id = FRM_OXID(frm);

	/* filter out FLOGI, LOGO */
	if (mac->fm_fip_mode == B_TRUE &&
	    (!(mac->fm_eport.eport_flags & EPORT_FLAG_IS_DIRECT_P2P) ||
	    mac->fm_fip_stage != FIP_STAGE_MAX) &&
	    frm->frm_hdr->hdr_r_ctl[0] == 0x22 &&
	    frm->frm_hdr->hdr_type[0] == 0x1 &&
	    (frm->frm_payload[0] == 0x04 || frm->frm_payload[0] == 0x05)) {
		/* Since FLOGI is sent by fcoe now, so just return resp here */
		(void) ddi_taskq_dispatch(fcoe_global_ss->ss_watchdog_taskq,
		    fcoe_fake_els_resp, frm, DDI_SLEEP);
		return;
	}

	fcoe_fill_frame_headers(frm);
	if (FCOE_MAC_TXCRC_ENABLED(mac) || FCOE_MAC_LSO_ENABLED(mac)) {
		/* offload */
		/*
		 * NIC driver will handle CRC, bzero fcoe_frame_tailer_t
		 */
		bzero(fmi->fmi_fft, sizeof (fcoe_frame_tailer_t));
		/*
		 * Construct offload params structure
		 */
		if (mac->fm_eport.eport_mtu >= FCOE_MIN_MTU_SIZE) {
			fcp_data_payload_size =
			    FCOE_DEFAULT_FCP_DATA_PAYLOAD_SIZE;
		} else {
			fcp_data_payload_size = FCOE_MIN_FCP_DATA_PAYLOAD_SIZE;
		}
		if (FCOE_MAC_LSO_ENABLED(mac) &&
		    (frm->frm_payload_size > fcp_data_payload_size)) {
			if (mac->fm_eport.eport_mtu >= FCOE_MIN_MTU_SIZE) {
				mss = FCOE_DEFAULT_FCP_DATA_PAYLOAD_SIZE;
			} else {
				mss = FCOE_MIN_FCP_DATA_PAYLOAD_SIZE;
			}
		} else {
			mss = 0;
		}
		if (FRM_F_CTL(frm) & 0x080000) {
			eof = FCOE_TYPE_EOF_T;
		} else {
			eof = FCOE_TYPE_EOF_N;
		}
		if (FRM_SEQ_CNT(frm) == 0) {
			sof = FCOE_TYPE_SOF_I;
		} else {
			sof = FCOE_TYPE_SOF_N;
		}
		fcoe_mac_set(frm, mss, FCFH_SIZE, sof, eof);
	} else {
		fcoe_fill_frame_tailers(frm);
	}

tx_frame:
	ret_cookie = mac_tx(mac->fm_cli_handle,
	    FRM2MBLK(frm), (uintptr_t)(ox_id),
	    MAC_TX_NO_ENQUEUE, &ret_mblk);
	if (ret_cookie != NULL) {
		mutex_enter(&mac->fm_mutex);
		(void) cv_timedwait(&mac->fm_tx_cv, &mac->fm_mutex,
		    ddi_get_lbolt() + drv_usectohz(100000));
		mutex_exit(&mac->fm_mutex);

		if (mac->fm_state == FCOE_MAC_STATE_OFFLINE) {
			/*
			 * we are doing offline, so just tell the upper that
			 * this is finished, the cmd will be aborted soon.
			 */
			fcoe_free_netb(ret_mblk);
		} else {
			goto tx_frame;
		}
	}

	/*
	 * MAC driver will release the mblk of the frame
	 * We need only release the frame itself
	 */
	mutex_enter(&FRM2MAC(frm)->fm_ss->ss_watch_mutex);
	list_insert_tail(&FRM2MAC(frm)->fm_ss->ss_pfrm_list,
	    FRM2FMI(frm));
	mac->fm_frm_cnt++;
	if (FRM2MAC(frm)->fm_ss->ss_flags & SS_FLAG_DOG_WAITING) {
		cv_signal(&FRM2MAC(frm)->fm_ss->ss_watch_cv);
	}
	mutex_exit(&FRM2MAC(frm)->fm_ss->ss_watch_mutex);
}

/*
 * Consider cache allocation in the future
 */
void
fcoe_release_frame(fcoe_frame_t *frame)
{
	kmem_free(frame, frame->frm_alloc_size);
}

static void *
fcoe_alloc_netb(fcoe_port_t *eport, uint32_t fc_frame_size, uint8_t **ppfc)
{
	mblk_t *mp;

	mp = fcoe_get_mblk(eport->eport_fcoe_private,
	    fc_frame_size + PADDING_SIZE);
	if (mp != NULL) {
		*ppfc = mp->b_rptr + PADDING_HEADER_SIZE;
	}

	return (mp);
}

void
fcoe_free_netb(void *netb)
{
	freemsg((mblk_t *)netb);
}

fcoe_frame_t *
fcoe_allocate_frame(fcoe_port_t *eport, uint32_t fc_frame_size, void *xmp)
{
	fcoe_frame_t	*frm;
	fcoe_i_frame_t	*fmi;
	mblk_t		*mp = xmp;
	uint32_t	 alloc_size;
	uint32_t	 raw_frame_size;

	if (mp == NULL) {
		/*
		 * We are allocating solicited frame now
		 */
		raw_frame_size = PADDING_SIZE + fc_frame_size;
		mp = fcoe_get_mblk(EPORT2MAC(eport), raw_frame_size);
		if (mp == NULL) {
			return (NULL);
		}
	}

	alloc_size = sizeof (fcoe_frame_t) + sizeof (fcoe_i_frame_t) +
	    EPORT2MAC(eport)->fm_client.ect_private_frame_struct_size;

	/*
	 * fcoe_frame_t initialization
	 */
	frm = (fcoe_frame_t *)kmem_alloc(alloc_size, KM_SLEEP);
	frm->frm_alloc_size = alloc_size;
	frm->frm_fc_frame_size = fc_frame_size;
	frm->frm_payload_size = fc_frame_size -
	    sizeof (fcoe_fc_frame_header_t);
	frm->frm_fcoe_private = sizeof (fcoe_frame_t) + (uint8_t *)frm;
	frm->frm_client_private = sizeof (fcoe_i_frame_t) +
	    (uint8_t *)frm->frm_fcoe_private;
	frm->frm_flags = 0;
	frm->frm_eport = eport;
	frm->frm_netb = mp;

	/*
	 * fcoe_i_frame_t initialization
	 */
	fmi = FRM2FMI(frm);
	fmi->fmi_frame = frm;
	fmi->fmi_mac = EPORT2MAC(eport);
	fmi->fmi_efh = (void *)mp->b_rptr;
	fmi->fmi_ffh = (fcoe_frame_header_t *)
	    (sizeof (struct ether_header) + (uint8_t *)fmi->fmi_efh);
	fmi->fmi_fc_frame = sizeof (fcoe_frame_header_t) +
	    (uint8_t *)fmi->fmi_ffh;
	fmi->fmi_fft = (fcoe_frame_tailer_t *)
	    (fc_frame_size + (uint8_t *)fmi->fmi_fc_frame);
	/*
	 * Continue to initialize fcoe_frame_t
	 */
	frm->frm_hdr = (fcoe_fc_frame_header_t *)fmi->fmi_fc_frame;
	frm->frm_ofh1 = NULL;
	frm->frm_ofh2 = NULL;
	frm->frm_fc_frame = (uint8_t *)frm->frm_hdr;
	frm->frm_payload = sizeof (fcoe_fc_frame_header_t) +
	    (uint8_t *)frm->frm_fc_frame;
	return (frm);
}

fcoe_frame_t *
fcoe_allocate_frame_lro(fcoe_port_t *eport, void *xmp, int payload_size)
{
	fcoe_frame_t	*frm;
	fcoe_i_frame_t	*fmi;
	mblk_t		*hdr_mp = xmp;
	uint32_t	 alloc_size;

	ASSERT(xmp != NULL);

	alloc_size = sizeof (fcoe_frame_t) + sizeof (fcoe_i_frame_t) +
	    EPORT2MAC(eport)->fm_client.ect_private_frame_struct_size;

	/*
	 * For the offload LRO, mblk is like:
	 * first mblk(xmp) is only PADDING information.
	 * Pure Payload is from second mblk(xmp->b_cont)
	 * Every 4k per mblk.
	 */

	/*
	 * fcoe_frame_t initialization
	 */
	frm = (fcoe_frame_t *)kmem_alloc(alloc_size, KM_SLEEP);
	frm->frm_alloc_size = alloc_size;
	frm->frm_fc_frame_size = sizeof (fcoe_fc_frame_header_t) + payload_size;
	frm->frm_payload_size = payload_size;
	frm->frm_fcoe_private = sizeof (fcoe_frame_t) + (uint8_t *)frm;
	frm->frm_client_private = sizeof (fcoe_i_frame_t) +
	    (uint8_t *)frm->frm_fcoe_private;
	frm->frm_flags = 0;
	frm->frm_eport = eport;
	frm->frm_netb = hdr_mp;

	/*
	 * fcoe_i_frame_t initialization
	 */
	fmi = FRM2FMI(frm);
	fmi->fmi_frame = frm;
	fmi->fmi_mac = EPORT2MAC(eport);
	fmi->fmi_efh = (void *)hdr_mp->b_rptr;
	fmi->fmi_ffh = (fcoe_frame_header_t *)
	    (sizeof (struct ether_header) + (uint8_t *)fmi->fmi_efh);
	fmi->fmi_fc_frame = sizeof (fcoe_frame_header_t) +
	    (uint8_t *)fmi->fmi_ffh;
	fmi->fmi_fft = (fcoe_frame_tailer_t *)
	    (sizeof (fcoe_fc_frame_header_t) + (uint8_t *)fmi->fmi_fc_frame);
	/*
	 * Continue to initialize fcoe_frame_t
	 */
	frm->frm_hdr = (fcoe_fc_frame_header_t *)fmi->fmi_fc_frame;
	frm->frm_ofh1 = NULL;
	frm->frm_ofh2 = NULL;
	frm->frm_fc_frame = NULL;
	/*
	 * For offload LRO, frm_payload is the pointer to
	 * the first payload mp(xmp->b_cont);
	 */
	frm->frm_payload = hdr_mp->b_cont->b_rptr;
	return (frm);
}

/*
 * Sub routines called by interface functions
 */

/*
 * According to spec, fill EthernetII frame header, FCoE frame header
 * VLAN (not included for now)
 */
static void
fcoe_fill_frame_headers(fcoe_frame_t *frm)
{
	fcoe_i_frame_t *fmi = FRM2FMI(frm);

	/*
	 * Initialize ethernet frame header
	 */
	bcopy(FRM2MAC(frm)->fm_current_addr, &fmi->fmi_efh->ether_shost,
	    ETHERADDRL);
	bcopy(frm->frm_eport->eport_efh_dst,
	    &fmi->fmi_efh->ether_dhost, ETHERADDRL);
	fmi->fmi_efh->ether_type = htons(ETHERTYPE_FCOE);

	/*
	 * Initialize FCoE frame header
	 */
	bzero(fmi->fmi_ffh, sizeof (fcoe_frame_header_t));
	FCOE_ENCAPS_VER(fmi->fmi_ffh, FCOE_VER);
	/* set to SOFi3 for the first frame of a sequence */
	if (FRM_SEQ_CNT(frm) == 0) {
		FCOE_V2B_1(FCOE_TYPE_SOF_I, fmi->fmi_ffh->ffh_sof);
	} else {
		FCOE_V2B_1(FCOE_TYPE_SOF_N, fmi->fmi_ffh->ffh_sof);
	}
}

/*
 * According to spec, fill FCOE frame tailer including CRC
 * VLAN (not included for now)
 */
static void
fcoe_fill_frame_tailers(fcoe_frame_t *frm)
{
	uint32_t crc;

	/*
	 * Initialize FCoE frame tailer
	 * CRC is not big endian, can't use macro V2B
	 */
	CRC32(crc, frm->frm_fc_frame, frm->frm_fc_frame_size,
	    (uint32_t)~0, crc32_table);
	FRM2FMI(frm)->fmi_fft->fft_crc[0] = 0xFF & (~crc);
	FRM2FMI(frm)->fmi_fft->fft_crc[1] = 0xFF & (~crc >> 8);
	FRM2FMI(frm)->fmi_fft->fft_crc[2] = 0xFF & (~crc >> 16);
	FRM2FMI(frm)->fmi_fft->fft_crc[3] = 0xFF & (~crc >> 24);
	if (FRM_F_CTL(frm) & 0x080000) {
		FCOE_V2B_1(FCOE_TYPE_EOF_T, FRM2FMI(frm)->fmi_fft->fft_eof);
	} else {
		FCOE_V2B_1(FCOE_TYPE_EOF_N, FRM2FMI(frm)->fmi_fft->fft_eof);
	}

	FRM2FMI(frm)->fmi_fft->fft_resvd[0] = 0;
	FRM2FMI(frm)->fmi_fft->fft_resvd[1] = 0;
	FRM2FMI(frm)->fmi_fft->fft_resvd[2] = 0;
}

void
fcoe_mac_notify_link_up(void *arg)
{
	fcoe_mac_t *mac = (fcoe_mac_t *)arg;

	ASSERT(mac->fm_flags & FCOE_MAC_FLAG_BOUND);

	mac->fm_client.ect_port_event(&mac->fm_eport,
	    FCOE_NOTIFY_EPORT_LINK_UP);
}
void
fcoe_mac_notify_link_down(void *arg)
{
	fcoe_mac_t *mac = (fcoe_mac_t *)arg;

	if (mac->fm_flags & FCOE_MAC_FLAG_BOUND) {
		mac->fm_client.ect_port_event(&mac->fm_eport,
		    FCOE_NOTIFY_EPORT_LINK_DOWN);
	}
}

int
fcoe_mac_setup_lro(fcoe_port_t *eport, uint16_t flags,
    uint16_t xid, uint32_t buf_size)
{
	fcoe_mac_t	*mac = EPORT2MAC(eport);
	mac_fcoe_lro_params_t	params;
	int ret = -1;

	bzero(&params, sizeof (mac_fcoe_lro_params_t));
	params.flags = flags;
	params.xchg_id = xid;
	params.buf_size = buf_size;

	ret = mac_client_fcoe_setup_lro(mac->fm_cli_handle,
	    &params);
	return (ret);
}

void
fcoe_mac_cancel_lro(fcoe_port_t *eport, uint16_t flags,
    uint16_t xid)
{
	fcoe_mac_t	*mac = EPORT2MAC(eport);
	mac_fcoe_lro_params_t	params;

	bzero(&params, sizeof (mac_fcoe_lro_params_t));
	params.flags = flags;
	params.xchg_id = xid;

	mac_client_fcoe_cancel_lro(mac->fm_cli_handle,
	    &params);
}

void
fcoe_mac_set(fcoe_frame_t *frame, uint16_t mss,
    uint16_t fc_headlen, uint8_t sof, uint8_t eof)
{
	mac_fcoe_tx_params_t	params;
	mblk_t	*mp = FRM2MBLK(frame);

	bzero(&params, sizeof (mac_fcoe_tx_params_t));

	params.flags = MAC_FCOE_ENABLE_TXCRC;

	if (mss != 0) {
		/* LSO */
		params.flags |= MAC_FCOE_ENABLE_LSO;
		if (FRM_F_CTL(frame) & 0x000008) {
			params.flags |= MAC_FCOE_REL_OFFSET;
		}
		params.mss = mss;
	}

	params.fc_hdr_len = fc_headlen;
	params.sof = sof;
	params.eof = eof;

	mac_fcoe_set(mp, &params);
	DB_CKSUMFLAGS(mp) |= HW_FCOE;
}

int
fcoe_create_port(dev_info_t *parent, fcoe_mac_t *mac, int is_target)
{
	int		 rval	  = 0;
	dev_info_t	*child	  = NULL;
	char *devname = is_target ? FCOET_DRIVER_NAME : FCOEI_DRIVER_NAME;

	ndi_devi_alloc_sleep(parent, devname, DEVI_PSEUDO_NODEID, &child);
	if (child == NULL) {
		FCOE_LOG("fcoe", "fail to create new devinfo");
		return (NDI_FAILURE);
	}

	if (ddi_prop_update_int(DDI_DEV_T_NONE, child,
	    "mac_id", mac->fm_linkid) != DDI_PROP_SUCCESS) {
		FCOE_LOG("fcoe",
		    "fcoe%d: prop_update port mac id failed for mac %d",
		    ddi_get_instance(parent), mac->fm_linkid);
		(void) ndi_devi_free(child);
		return (NDI_FAILURE);
	}

	rval = ndi_devi_online(child, NDI_ONLINE_ATTACH);
	if (rval != NDI_SUCCESS) {
		FCOE_LOG("fcoe", "fcoe%d: online_driver failed for mac %d",
		    ddi_get_instance(parent), mac->fm_linkid);
		return (NDI_FAILURE);
	}
	mac->fm_client_dev = child;

	return (rval);
}

int
fcoe_delete_port(dev_info_t *parent, fcoeio_t *fcoeio, datalink_id_t linkid,
    uint64_t *is_target)
{
	int		 rval = 0;
	fcoe_mac_t	*mac;

	mac = fcoe_lookup_mac_by_id(linkid);
	if (mac == NULL) {
		fcoeio->fcoeio_status = FCOEIOE_MAC_NOT_FOUND;
		return (EINVAL);
	}

	*is_target = EPORT_CLT_TYPE(&mac->fm_eport);
	if ((mac->fm_flags & FCOE_MAC_FLAG_ENABLED) != FCOE_MAC_FLAG_ENABLED) {
		fcoeio->fcoeio_status = FCOEIOE_ALREADY;
		return (EALREADY);
	}

	if (!(mac->fm_flags & FCOE_MAC_FLAG_BOUND)) {
		/*
		 * It means that deferred detach has finished
		 * of last delete operation
		 */
		goto skip_devi_offline;
	}

	atomic_and_32(&mac->fm_eport.eport_flags, ~EPORT_FLAG_MAC_IN_USE);
	mac->fm_flags |= FCOE_MAC_FLAG_USER_DEL;
	rval = ndi_devi_offline(mac->fm_client_dev, NDI_DEVI_REMOVE);
	if (rval != NDI_SUCCESS) {
		FCOE_LOG("fcoe", "fcoe%d: offline_driver %s failed",
		    ddi_get_instance(parent),
		    ddi_get_name(mac->fm_client_dev));
		atomic_or_32(&mac->fm_eport.eport_flags,
		    EPORT_FLAG_MAC_IN_USE);

		fcoeio->fcoeio_status = FCOEIOE_OFFLINE_FAILURE;
		return (EBUSY);
	}

skip_devi_offline:
	(void) fcoe_close_mac(mac);
	fcoe_destroy_mac(mac);
	return (0);
}

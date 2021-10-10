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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include "sxge.h"
#include "sxge_mbx.h"
#include "sxge_mbx_proto.h"

/*
 * These definitions are not in S10, hence defining here for all of IB
 * code to use. Remove these when these become available in S10.
 */
#ifndef	htonll
#ifdef	_BIG_ENDIAN
#define	htonll(x)   (x)
#define	ntohll(x)   (x)
#else
#define	htonll(x)   ((((uint64_t)htonl(x)) << 32) + htonl((uint64_t)(x) >> 32))
#define	ntohll(x)   ((((uint64_t)ntohl(x)) << 32) + ntohl((uint64_t)(x) >> 32))
#endif
#endif

int sxge_mbx_max_retry = 80000;

/*
 * static int
 * sxge_mbx_reset() - reset the NIU MB hardware
 *
 * Returns:
 *      0, on success.
 */
#ifdef SXGE_DEBUG
static int
sxge_mbx_reset(sxge_t *sxge)
{
	boolean_t	notdone = B_TRUE;
	niu_mb_status_t	cs;
	uint64_t	addr;

	if (sxge == NULL)
		return (ENXIO);

	/*
	 * Reset the mbox.
	 */
	addr = NIU_MB_STAT;
	cs.value = 0x0ULL;
	cs.bits.func_rst = 1;
	SXGE_PUT64(sxge->pio_hdl, addr, cs.value);

	/*
	 * Wait for reset to complete.
	 */
	while (notdone) {
		cs.value = SXGE_GET64(sxge->pio_hdl, addr);

		SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "mbox:cs.value 0x%llx\n", cs.value));

		if (cs.bits.func_rst_done == 1) {
			cs.value = 0x0ULL;
			cs.bits.func_rst_done = 1;
			addr = NIU_MB_STAT;
			SXGE_PUT64(sxge->pio_hdl, addr, cs.value);
			notdone = B_FALSE;
		}
	}

	return (0);
}

/*
 * static int
 * sxge_mbx_int_mask() -- Mask on/off the interrupt bits for the mailbox.
 *
 * Return(s):
 *      0, on success.
 */

static int
sxge_mbx_int_mask(sxge_t *sxge, boolean_t on)
{
	niu_mb_int_msk_t	mask;
	uint64_t		addr;

	mask.value = 0x0ULL;
	if (on) {
		mask.bits.omb_err_ecc_msk = 1;
		mask.bits.imb_err_ecc_msk = 1;
		mask.bits.omb_ovl_msk = 1;
		mask.bits.imb_full_msk = 1;
		mask.bits.omb_acked_msk = 1;
		mask.bits.omb_failed_msk = 1;
	}

	addr = NIU_MB_MSK;
	SXGE_PUT64(sxge->pio_hdl, addr, mask.value);

	return (0);
}

/*
 * int
 * sxge_mbx_init() - Initialze the mbox hardware by
 *      resetting it, arming it's interrupts.
 *
 * XXX -- mbox.lock and mbox.cv are initialized somewhere else with
 *      the other driver locks and condition variables.
 *
 * Returns:
 *      0, if successful.
 *      EINVAL, on invalid input.
 *      ENXIO, on failure.
 */

int
sxge_mbx_init(sxge_t *sxge)
{
	int	err;

	/*
	 * Validate the input.
	 */
	if (sxge == NULL)
		return (EINVAL);

	/*
	 * Reset the NIU mailbox.
	 */
	err = sxge_mbx_reset(sxge);
	if (err != 0)
		return (err);

	/*
	 * Enable mbox interrupt evnets.
	 */
	err = sxge_mbx_int_mask(sxge, B_FALSE);
	if (err != 0)
		return (err);

	/*
	 * Set the state to inited.
	 */
	sxge->mbox.ready = B_TRUE;

	SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "mbox: initialization complete\n"));

	return (0);
}
#endif

/*
 * int
 * sxge_mbx_post() - Post a new message to the mailbox.
 *
 * Return(s):
 *	0, if message posted successfully and was acked.
 *	EINVAL, invalid length for MBOX request.
 *	ENXIO, if requested timed out.
 *	EIO, mbox requested was negatively acknowledged.
 */

int
sxge_mbx_post(sxge_t *sxge, uint64_t *msgp, int len)
{
	int			i, retries = 0;
	niu_mb_status_t		cs;
	uint64_t		addr;
	/* uint8_t			option = 0, rid = 0; */
	/* set but not used */
	boolean_t		notdone = B_TRUE;

	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "==> sxge_mbx_post\n"));
	/*
	 * Validate the inputs.
	 */
	if ((sxge == NULL) || (msgp == NULL) || (len == 0) ||
	    (len > NIU_MB_MAX_LEN)) {
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
		    "<== sxge_mbx_post: invalid\n"));
		return (EINVAL);
	}

	MUTEX_ENTER(&sxge->mbox_lock);

	/*
	 * Has the mbox been initialzied.
	 */
	if ((sxge->mbox.ready != B_TRUE) || sxge->mbox.posted) {
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
		    "<== sxge_mbx_post: mbox not ready\n"));
		MUTEX_EXIT(&sxge->mbox_lock);
		return (ENXIO);
	}

	/*
	 * Check for room in the mailbox.
	 */
	addr = NIU_MB_STAT;
	cs.value = 0;
	cs.value = SXGE_GET64(sxge->pio_hdl, addr);

	/*
	 * Is the mailbox full? If so, then return an error.
	 */
	if (cs.bits.omb_full) {
		/*
		 * XXX - Should driver wait for the FULL condition to be
		 * cleared? Or, return an error?
		 */
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
		    "<== sxge_mbx_post: mailbox is full, can't post\n"));
		MUTEX_EXIT(&sxge->mbox_lock);
		return (ENXIO);
	}

	/*
	 * Post the message.  0th entry is the last 64-bit word to
	 * be posted.
	 */
	for (i = 1; i < len; i++) {
		addr = NIU_OMB_ENTRY(i);
		SXGE_PUT64(sxge->pio_hdl, addr, msgp[i]);
	}
	addr = NIU_OMB_ENTRY(0);
	SXGE_PUT64(sxge->pio_hdl, addr, msgp[0]);

	/*
	 * Set state.
	 */
	sxge->mbox.acked = B_FALSE;
	sxge->mbox.posted = B_TRUE;
	sxge->mbox.imb_full = B_FALSE;

	MUTEX_EXIT(&sxge->mbox_lock);
	/*
	 * POLL for ack/nack.
	 * XXX - this can be done in interrupt.
	 */
	do {
		delay(2);

#ifdef ENABLE_MBOX_INTERRUPT
		if (sxge->load_state & A_INTR_ADD_DONE) {
			MUTEX_ENTER(&sxge->mbox_lock);
			if (sxge->mbox.acked == B_TRUE)
				notdone = B_FALSE;
			MUTEX_EXIT(&sxge->mbox_lock);
		}
		else
#endif
		{
			addr = NIU_MB_STAT;
			cs.value = 0;
			cs.value = SXGE_GET64(sxge->pio_hdl, addr);

			SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
			    "cs.value is 0x%llx\n", cs.value));

			if ((cs.bits.omb_acked == 1) ||
			    (cs.bits.omb_failed == 1)) {
				addr = NIU_MB_STAT;
				SXGE_PUT64(sxge->pio_hdl, addr,
				    (cs.value & 0x0006ULL));
				sxge->mbox.acked = B_TRUE;
				notdone = B_FALSE;
			}
		}

		retries++;
		if (notdone && (retries > sxge_mbx_max_retry)) {
			notdone = B_FALSE;
		}
	} while (notdone);

	MUTEX_ENTER(&sxge->mbox_lock);
	if (sxge->mbox.acked == B_FALSE) {
			addr = NIU_MB_STAT;
			cs.value = 0;
			cs.value = SXGE_GET64(sxge->pio_hdl, addr);

			SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
			    "poll NIU_MB_STAT: 0x%llx\n", cs.value));

#ifdef ENABLE_MBOX_INTERRUPT
		{
			sxge_ldg_t	*ldgp;
			uint64_t	ldsv, ldgimgn;
			SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
			    "maxldgs %d, maxldvs %d\n",
			    sxge->ldgvp->maxldgs,
			    sxge->ldgvp->maxldvs));

			ldgp = sxge->ldgvp->ldgp;
			for (i = 0; i < sxge->ldgvp->ldg_intrs; i++, ldgp++) {
				ldsv = SXGE_GET64(sxge->pio_hdl,
				    INTR_LDSV(ldgp->vni_num, ldgp->nf,
				    ldgp->ldg));
				ldgimgn = SXGE_GET64(sxge->pio_hdl,
				    INTR_LDGIMGN(ldgp->ldg));
				SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
				    "vni %x, nf %x, ldg %x, ldsv 0x%llx,
				    ldgimgn 0x%llx\n",
				    ldgp->vni_num,
				    ldgp->nf,
				    ldgp->ldg,
				    ldsv,
				    ldgimgn));
			}
		}
#endif

			if ((cs.bits.omb_acked == 1) ||
			    (cs.bits.omb_failed == 1)) {
				addr = NIU_MB_STAT;
				SXGE_PUT64(sxge->pio_hdl, addr,
				    (cs.value & 0x0006ULL));
				/* sxge->mbox.acked = B_TRUE; */
				sxge->mbox.posted = B_FALSE;
				MUTEX_EXIT(&sxge->mbox_lock);
				return (0);
			}
	}

	/*
	 * Did we get a NACK or ACK? If not acked
	 */
	if (!sxge->mbox.acked && sxge->mbox.posted) {
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
		    "<== sxge_mbx_post failed\n"));
		sxge->mbox.posted = B_FALSE;
		MUTEX_EXIT(&sxge->mbox_lock);
		return (EIO);
	}

	sxge->mbox.posted = B_FALSE;
	sxge->mbox.acked = B_FALSE;
	MUTEX_EXIT(&sxge->mbox_lock);

	/*
	 * At this point, we posted a message and it was
	 * acked.
	 */
	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "<== sxge_mbx_post success, %d\n", retries));
	return (0);
}


int
sxge_mbx_wait_for_response(sxge_t *sxge)
{
	niu_mb_status_t		cs;
	uint64_t		addr;
	/* uint8_t			option = 0, rid = 0; */
	/* set but not used */
	boolean_t		notdone = B_TRUE;
	int			retries = 0;

	while (notdone) {
		delay(2);

#ifdef ENABLE_MBOX_INTERRUPT
		if (sxge->load_state & A_INTR_ADD_DONE) {
			MUTEX_ENTER(&sxge->mbox_lock);
			if (sxge->mbox.imb_full == B_TRUE)
				notdone = B_FALSE;
			MUTEX_EXIT(&sxge->mbox_lock);
		}
		else
#endif
		{
			addr = NIU_MB_STAT;
			cs.value = SXGE_GET64(sxge->pio_hdl, addr);

			SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
			    "cs.value is 0x%llx, %d\n", cs.value, retries));

			if (cs.bits.imb_full) {
				SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
				    "sxge_mbx_wait_for_respone: received\n"));
				addr = NIU_MB_STAT;
				SXGE_PUT64(sxge->pio_hdl, addr, 0x0008ULL);
				sxge->mbox.imb_full = B_TRUE;
				notdone = B_FALSE;
			}
		}

		retries++;
		if (notdone && (retries > sxge_mbx_max_retry)) {
			MUTEX_ENTER(&sxge->mbox_lock);
			addr = NIU_MB_STAT;
			cs.value = SXGE_GET64(sxge->pio_hdl, addr);

			SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
			    "imb NIU_MB_STAT: 0x%llx, %d\n",
			    cs.value, retries));

			if (cs.bits.imb_full) {
				SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
				    "sxge_mbx_wait_for_respone: received\n"));
				addr = NIU_MB_STAT;
				SXGE_PUT64(sxge->pio_hdl, addr, 0x0008ULL);
				/* sxge->mbox.imb_full = B_TRUE; */
				/* notdone = B_FALSE; */
			}
			MUTEX_EXIT(&sxge->mbox_lock);
			SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
			    "sxge_mbx_wait_for_respone: no reply\n"));
			return (0);
		}
	}

	return (0);
}

static int
sxge_mbx_check_validity(sxge_t *sxge, niu_mb_tag_t *hdrp)
{
	/*
	 * Check the length of the incoming message.
	 */
	if (hdrp->mb_len > NIU_MB_MAX_LEN) {
		return (EINVAL);
	}

	/*
	 * In this a response message or request message.
	 */
	switch (hdrp->mb_request) {
	case NIU_MB_REQUEST:
	case NIU_MB_RESPONSE:
		break;

	default:
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_check validty: not req or resp\n"));
		return (EINVAL);
	}

	/*
	 * Check the inbound message type and it's
	 * parameters.
	 */
	switch (hdrp->mb_type) {
	case NIU_MB_GET_L2_ADDRESS_CAPABILITIES:
	case NIU_MB_GET_TCAM_CAPABILITIES:
	case NIU_MB_LINK_SPEED:
		/*
		 * Need to add checks.
		 */
		break;

	case NIU_MB_L2_ADDRESS_ADD:
	case NIU_MB_L2_ADDRESS_REMOVE:
	case NIU_MB_L2_MULTICAST_ADD:
	case NIU_MB_L2_MULTICAST_REMOVE:
		if (hdrp->mb_len != NIU_MB_L2_ADDRESS_REQUEST_SZ)
			return (EINVAL);

		if (hdrp->mb_request == NIU_MB_REQUEST) {
			/*
			 * EPS is sending a request instead of
			 * of a response.
			 */
			return (EINVAL);
		}
		break;

	case NIU_MB_VLAN_ADD:
	case NIU_MB_VLAN_REMOVE:
		if (hdrp->mb_len != NIU_MB_VLAN_REQUEST_SZ)
			return (EINVAL);

		if (hdrp->mb_request == NIU_MB_REQUEST) {
			/*
			 * EPS is sending a request instead of
			 * of a response.
			 */
			return (EINVAL);
		}
		break;

	case NIU_MB_PIO_WRITE:
		if (hdrp->mb_request == NIU_MB_REQUEST) {
			/*
			 * EPS is sending a request instead of
			 * of a response.
			 */
			return (EINVAL);
		}
		break;

	case NIU_MB_PIO_READ:
		if (hdrp->mb_request == NIU_MB_REQUEST) {
			/*
			 * EPS is sending a request instead of
			 * of a response.
			 */
			return (EINVAL);
		}

		if (hdrp->mb_len != NIU_MB_PIO_SZ) {
			return (EINVAL);
		}
		break;

	default:
		/*
		 * Not a known message.
		 */
		return (0);
	}

	/*
	 * Good message.
	 */
	return (0);
}

static int
sxge_mbx_process(sxge_t *sxge, niu_mb_msg_t *msgp)
{
	niu_mb_tag_t	*tagp;
	niu_imb_ack_t	ack;
	/* uint8_t		option = 0, rid = 0; */	/* set but not used */
	uint64_t	addr;
	int		err, i;

	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "==> sxge_mbx_process()\n"));
	bzero(msgp, sizeof (niu_mb_msg_t));

	/*
	 * Get entry 0, it contains the length of the message.
	 */
	addr = NIU_IMB_ENTRY(0);
	msgp->msg_data[0] = SXGE_GET64(sxge->pio_hdl, addr);

	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "msgp->data[0] = 0x%llx\n",
	    (unsigned long long)msgp->msg_data[0]));

	tagp = (niu_mb_tag_t *)&msgp->msg_data[0];

	/*
	 * Check the validity of the inbound message.
	 */
	err = sxge_mbx_check_validity(sxge, tagp);
	/* sxge->mbox.imb_full = B_FALSE; */

	if (err != 0) {
		/*
		 * NACK the message, it was not valid.
		 */
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_ERR_PRI),
		    "IMB NACK\n"));
		ack.value = 0;
		ack.bits.imb_nack = 1;
		addr = NIU_IMB_ACK;
		SXGE_PUT64(sxge->pio_hdl, addr, ack.value);
		return (err);
	}

	/*
	 * Store away the length of the message.
	 */
	msgp->length = (uint64_t)tagp->mb_len;
	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "tagp: Length of received message is %d\n", tagp->mb_len));

	/*
	 * Read in the rest of the message.
	 */
	for (i = 1; i < tagp->mb_len; i++) {
		addr = NIU_IMB_ENTRY(i);
		msgp->msg_data[i] = SXGE_GET64(sxge->pio_hdl, addr);
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "msgp->msg_data[%d] = 0x%llx\n", i,
		    (unsigned long long)msgp->msg_data[i]));

	}

	/*
	 * Message is valid and received, so ACK it.
	 */
	ack.value = 0;
	ack.bits.imb_ack = 1;
	addr = NIU_IMB_ACK;
	SXGE_PUT64(sxge->pio_hdl, addr, ack.value);

	/*
	 * Now process the message.
	 */
	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "<== sxge_mbx_process\n"));
	return (0);
}

/*
 * Use the protocol.
 */
int
sxge_mbx_link_speed_req(sxge_t *sxge)
{
	l2_address_capability_t	l2add;
	niu_mb_msg_t		msg;
	int			err;

	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "==> sxge_mbx_link_speed_req\n"));

	bzero((void *)&l2add, sizeof (l2_address_req_t));

	l2add.hdr.mb_request = NIU_MB_REQUEST;
	l2add.hdr.mb_type = NIU_MB_LINK_SPEED;
	l2add.hdr.mb_len = NIU_MB_L2_ADDRESS_CAP_SZ;
	l2add.hdr.mb_seq = 0xaabb;

	err = sxge_mbx_post(sxge, (uint64_t *)&l2add,
	    NIU_MB_L2_ADDRESS_CAP_SZ);
	if (err != 0) {
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_post failed\n"));
		return (EIO);
	}

	err = sxge_mbx_wait_for_response(sxge);
	if (err != 0) {
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_wait_for_response failed\n"));
		return (EIO);
	}

	err = sxge_mbx_process(sxge, &msg);
	if (err != 0) {
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_get_response failed\n"));
		return (EIO);
	}

	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "sxge_mbx_link_speed_req, 0x%x, 0x%x, speed %d\n",
	    msg.msg_data[4], msg.msg_data[5], msg.msg_data[3]));

	if ((msg.msg_data[NIU_MB_PCS_MODE_INDEX] & NIU_MB_PCS_MODE) ==
	    NIU_MB_PCS_MODE)
		sxge->link_speed = 40000;
	else
		sxge->link_speed = 10000;

	return (0);
}

int
sxge_mbx_l2_req(sxge_t *sxge, struct ether_addr *addr, uint16_t type, int slot)
{
	l2_address_req_t	l2add;
	niu_mb_msg_t		msg;
	int			err;

	bzero((void *)&l2add, sizeof (l2_address_req_t));

	l2add.hdr.mb_request = NIU_MB_REQUEST;
	l2add.hdr.mb_type = type;
	l2add.hdr.mb_len = NIU_MB_L2_ADDRESS_REQUEST_SZ;
	l2add.hdr.mb_seq = 0xaabb;
	bcopy((uint8_t *)addr, (uint8_t *)&l2add.address, ETHERADDRL);
	l2add.address = htonll(l2add.address) >> L2_ADDRESS_RESERVE_BITS;

	/*
	 * slot# is the mac-address slot per blade, each blade has 4 slots
	 * slot-0 is for factory mac-id, 3 other slots are programmble
	 * by the blade
	 */

	l2add.slot[0] = (uint8_t)slot;
	l2add.mask = 0;
	/*
	 * Example: If we set l2add.mask to 0xFF then it means a range of
	 * MAC addresses from xx xx xx xx xx 00 through xx xx xx xx xx FF
	 */
	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "sxge_mbx_l2_req, 0x%llx, %X\n", l2add.address, type));

	err = sxge_mbx_post(sxge, (uint64_t *)&l2add,
	    NIU_MB_L2_ADDRESS_REQUEST_SZ);
	if (err != 0) {
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_post failed\n"));
		return (EIO);
	}

	err = sxge_mbx_wait_for_response(sxge);
	if (err != 0) {
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_wait_for_response failed\n"));
		return (EIO);
	}

	err = sxge_mbx_process(sxge, &msg);
	if (err != 0) {
		SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_get_response failed\n"));
		return (EIO);
	}

	return (0);
}

int
sxge_mbx_vlan_req(sxge_t *sxge, uint16_t vid, uint16_t type)
{
	vlan_req_t		req;
	niu_mb_msg_t		msg;
	int			err;

	bzero((void *)&req, sizeof (vlan_req_t));

	req.hdr.mb_request = NIU_MB_REQUEST;
	req.hdr.mb_type = type;
	req.hdr.mb_len = NIU_MB_VLAN_REQUEST_SZ;
	req.hdr.mb_seq = 0xaabb;
	req.vlan_id = (uint64_t)vid;

	err = sxge_mbx_post(sxge, (uint64_t *)&req, NIU_MB_VLAN_REQUEST_SZ);
	if (err != 0) {
		SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_post failed\n"));
		return (EIO);
	}

	err = sxge_mbx_wait_for_response(sxge);
	if (err != 0) {
		SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_wait_for_response failed\n"));
		return (EIO);
	}

	err = sxge_mbx_process(sxge, &msg);
	if (err != 0) {
		SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_get_response failed\n"));
		return (EIO);
	}

	return (0);
}

int
sxge_mbx_pio_read(sxge_t *sxge, uint64_t addr, uint64_t *data)
{
	niu_mb_msg_t	msg;
	niu_mb_pio_t	read;
	int		err;

	bzero((void *)&read, sizeof (niu_mb_pio_t));

	read.hdr.mb_request = NIU_MB_REQUEST;
	read.hdr.mb_type = NIU_MB_PIO_READ;
	read.hdr.mb_len = NIU_MB_PIO_SZ;
	read.hdr.mb_seq = 0xaabb;
	read.offset = addr;
	read.data = 0;

	err = sxge_mbx_post(sxge, (uint64_t *)&read, NIU_MB_PIO_SZ);
	if (err != 0) {
		SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_post failed\n"));
		return (EIO);
	}

	err = sxge_mbx_wait_for_response(sxge);
	if (err != 0) {
		SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_wait_for_response failed\n"));
		return (EIO);
	}

	err = sxge_mbx_process(sxge, &msg);
	if (err != 0) {
		SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_get_response failed\n"));
		return (EIO);
	}

	/*
	 * Get the word returned from the EPS.
	 */
	*data = msg.msg_data[2];
	SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "sxge_mbx_pio_read: addr:%llx, data:%llx\n", addr, *data));

	return (0);
}

int
sxge_mbx_pio_write(sxge_t *sxge, uint64_t addr, uint64_t data)
{
	niu_mb_msg_t	msg;
	niu_mb_pio_t	write;
	int		err;

	bzero((void *)&write, sizeof (niu_mb_pio_t));

	write.hdr.mb_request = NIU_MB_REQUEST;
	write.hdr.mb_type = NIU_MB_PIO_WRITE;
	write.hdr.mb_len = NIU_MB_PIO_SZ;
	write.hdr.mb_seq = 0xaabb;
	write.offset = addr;
	write.data = data;

	err = sxge_mbx_post(sxge, (uint64_t *)&write, NIU_MB_PIO_SZ);
	if (err != 0) {
		SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_post failed\n"));
		return (EIO);
	}

	err = sxge_mbx_wait_for_response(sxge);
	if (err != 0) {
		SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_wait_for_response failed\n"));
		return (EIO);
	}

	err = sxge_mbx_process(sxge, &msg);
	if (err != 0) {
		SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
		    "sxge_mbx_get_response failed\n"));
		return (EIO);
	}

	SXGE_DBG((NULL, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "sxge_mbx_pio_read: addr:%llx, data:%llx\n", addr, data));

	return (0);
}

/* Add a multicast address entry into the HW hash table */
int
sxge_add_mcast_addr(sxge_t *sxge, struct ether_addr *addrp)
{
	int status;

	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "sxge_add_mcast_addr, %02x:%02x:%02x:%02x:%02x:%02x\n",
	    addrp->ether_addr_octet[0],
	    addrp->ether_addr_octet[1],
	    addrp->ether_addr_octet[2],
	    addrp->ether_addr_octet[3],
	    addrp->ether_addr_octet[4],
	    addrp->ether_addr_octet[5]));

	status = sxge_mbx_l2_req(sxge, addrp, NIU_MB_L2_MULTICAST_ADD, 0);
	return (status);
}

/* Remove a multicast address entry from the HW hash table */
int
sxge_del_mcast_addr(sxge_t *sxge, struct ether_addr *addrp)
{
	int status;

	SXGE_DBG((sxge, (SXGE_MBOX_BLK | SXGE_INFO_PRI),
	    "sxge_del_mcast_addr, %02x:%02x:%02x:%02x:%02x:%02x\n",
	    addrp->ether_addr_octet[0],
	    addrp->ether_addr_octet[1],
	    addrp->ether_addr_octet[2],
	    addrp->ether_addr_octet[3],
	    addrp->ether_addr_octet[4],
	    addrp->ether_addr_octet[5]));

	status = sxge_mbx_l2_req(sxge, addrp, NIU_MB_L2_MULTICAST_REMOVE, 0);
	return (status);
}

int
sxge_ucast_find(sxge_t *sxge, struct ether_addr *mac_addr)
{
	int slot;

	for (slot = 0; slot < XMAC_MAX_ADDR_ENTRY; slot++) {
		if (bcmp(sxge->mac_pool[slot].addr, mac_addr, ETHERADDRL) == 0)
			return (slot);
	}

	return (-1);
}


/* Add a unicast address entry into the HW hash table */
int
sxge_add_ucast_addr(sxge_t *sxge, struct ether_addr *addrp)
{
	int status = SXGE_FAILURE;
	int slot;
	uint64_t empty_addr = 0;

	slot = sxge_ucast_find(sxge, addrp);
	if (slot >= 0)
		return (SXGE_SUCCESS);

	slot = sxge_ucast_find(sxge, (struct ether_addr *)&empty_addr);
	if (slot < 0)
		return (ENOSPC);

	status = sxge_mbx_l2_req(sxge, addrp, NIU_MB_L2_ADDRESS_ADD, slot);
	if (status == SXGE_SUCCESS)
		bcopy(addrp, sxge->mac_pool[slot].addr, ETHERADDRL);

	return (status);
}

/* Remove a unicast address entry from the HW hash table */
/* ARGSUSED */
int
sxge_del_ucast_addr(sxge_t *sxge, struct ether_addr *addrp)
{
	int status = SXGE_FAILURE;
	int slot;
	uint64_t empty_addr = 0;

	slot = sxge_ucast_find(sxge, addrp);
	if (slot < 0)
		return (status);

	status = sxge_mbx_l2_req(sxge, addrp, NIU_MB_L2_ADDRESS_REMOVE, 0);
	if (status == SXGE_SUCCESS)
		bcopy(&empty_addr, sxge->mac_pool[slot].addr, ETHERADDRL);

	return (status);
}

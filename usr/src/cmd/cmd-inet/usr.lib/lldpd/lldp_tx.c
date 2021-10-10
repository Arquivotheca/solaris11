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

/*
 * This file contains routines that implements LLDP Transmit state machine and
 * Transmit timer state machine.
 */
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "lldp_impl.h"
#include "lldp_provider.h"

uint32_t lldp_msgFastTx = 1;		/* Ticks */
uint32_t lldp_msgTxInterval = 30;	/* seconds */
uint32_t lldp_reinitDelay = 2;		/* seconds */
uint32_t lldp_msgTxHold = 4;
uint32_t lldp_txFastInit = 4;
uint32_t lldp_txCreditMax = 5;
/* Is part of the MIB but does not have section in the IEEE document */
uint32_t lldp_txNotifyInterval = 0;

static int	lldp_send_txFrame(lldp_agent_t *, boolean_t);

/*
 * something has changed locally for this agent, transmit out the new
 * information.
 */
void
lldp_something_changed_local(lldp_agent_t *lap)
{
	/* Recreate the Tx PDU */
	(void) lldp_create_txFrame(lap, B_FALSE);

	lldp_mutex_lock(&lap->la_mutex);
	lap->la_localChanges = B_TRUE;
	lldp_mutex_unlock(&lap->la_mutex);
	/* Signal the timer thread */
	(void) pthread_cond_broadcast(&lap->la_cond_var);
}

static void
lldp_txAddCredit(lldp_agent_t *lap)
{
	lap->la_txCredit++;
	if (lap->la_txCredit > lldp_txCreditMax)
		lap->la_txCredit = lldp_txCreditMax;
}

/* Construct a LLDPDU and send it to the peer */
static int
lldp_mibConstrInfo(lldp_agent_t *lap, uint8_t *lldpdu, size_t maxlen,
    size_t *msglen, boolean_t shutdown)
{
	lldp_write2pdu_t *wpdu;
	lldp_tlv_info_t	*infop;
	int		err;
	uint16_t	ttl;

	*msglen = 0;
	maxlen -= sizeof (uint16_t);	/* account for 2 bytes of END TLV */

	/* add first 3 mandatory TLV's */
	infop = lldp_get_tlvinfo(LLDP_TLVTYPE_CHASSIS_ID, 0, 0);
	assert(infop != NULL);
	if ((err = infop->lti_writef(lap, lldpdu + *msglen, maxlen - *msglen,
	    msglen)) != 0) {
		goto ret;
	}
	infop = lldp_get_tlvinfo(LLDP_TLVTYPE_PORT_ID, 0, 0);
	assert(infop != NULL);
	if ((err = infop->lti_writef(lap, lldpdu + *msglen, maxlen - *msglen,
	    msglen)) != 0) {
		goto ret;
	}
	ttl = shutdown ? 0 : lap->la_txTTL;
	infop = lldp_get_tlvinfo(LLDP_TLVTYPE_TTL, 0, 0);
	assert(infop != NULL);
	if ((err = infop->lti_writef(&ttl, lldpdu + *msglen, maxlen - *msglen,
	    msglen)) != 0) {
		goto ret;
	}
	if (shutdown)
		goto ret;

	/*
	 * Add any optional TLVs that were selected by the administrator or
	 * by a consumer by walking through the list of registered write2pdu
	 * functions and call them.
	 *
	 * Since these are optional TLVs, we do not care if the construction
	 * of the TLV failed. The administrator will find out that the TLVs
	 * are not getting transmitted from `show-agentinfo' subcommand
	 */
	for (wpdu = list_head(&lap->la_write2pdu); wpdu != NULL;
	    wpdu = list_next(&lap->la_write2pdu, wpdu)) {
		(void) wpdu->ltp_writef(wpdu->ltp_cbarg, lldpdu + *msglen,
		    maxlen - *msglen, msglen);
	}

ret:
	if (err != 0 && err != ENOBUFS) {
		return (err);
	} else if (err == ENOBUFS) {
		LLDP_STATS_INCR(lap->la_stats, ls_stats_FramesOutTotal);
		err = 0;
	}
	infop = lldp_get_tlvinfo(LLDP_TLVTYPE_END, 0, 0);
	assert(infop != NULL);
	(void) infop->lti_writef(NULL, lldpdu + *msglen, maxlen - *msglen,
	    msglen);
	return (0);
}

static void
lldp_tx_add_eheader(lldp_agent_t *agent)
{
	struct ether_header	ehdr;
	uint8_t			lldp_mcast[ETHERADDRL] = LLDP_GROUP_ADDRESS;

	bcopy(lldp_mcast, &ehdr.ether_dhost, ETHERADDRL);
	bcopy(agent->la_physaddr, &ehdr.ether_shost, agent->la_physaddrlen);
	ehdr.ether_type = htons(ETHERTYPE_LLDP);

	bcopy(&ehdr, agent->la_tx_pdu,  sizeof (struct ether_header));
}

int
lldp_create_txFrame(lldp_agent_t *lap, boolean_t shutdown)
{
	int	err = 0;
	size_t	elen = sizeof (struct ether_header);
	size_t	lldpdulen;

	lldp_rw_lock(&lap->la_txmib_rwlock, LLDP_RWLOCK_READER);
	lap->la_tx_pdulen = 0;
	bzero(lap->la_tx_pdu, LLDP_MAX_PDULEN);

	/* Add ethernet header */
	lldp_tx_add_eheader(lap);

	/*
	 * construct the lldpdu from the local mib based on if it is a
	 * shutdown frame or not
	 */
	err = lldp_mibConstrInfo(lap, lap->la_tx_pdu + elen,
	    LLDP_MAX_PDULEN - elen, &lldpdulen, shutdown);
	if (err == 0)
		lap->la_tx_pdulen = elen + lldpdulen;
	lldp_rw_unlock(&lap->la_txmib_rwlock);
	return (err);
}

static int
lldp_send_txFrame(lldp_agent_t *lap, boolean_t shutdown)
{
	int	err = 0;

	/*
	 * Maybe an early attempt to create the PDU failed, try again.
	 */
	if (lap->la_tx_pdulen == 0) {
		if ((err = lldp_create_txFrame(lap, shutdown)) != 0) {
			syslog(LOG_ERR, "lldp_send_txFrame: failed\n");
			return (err);
		}
	}
	if (send(lap->la_tx_sockfd, (void *)lap->la_tx_pdu,
	    lap->la_tx_pdulen, 0) == 1) {
		err = errno;
		syslog(LOG_ERR, "lldp_send_txFrame: failed\n");
	} else {
		LLDP_PKT_SEND(lap->la_linkname, lap->la_tx_pdulen);
		LLDP_STATS_INCR(lap->la_stats, ls_stats_FramesOutTotal);
	}
	return (err);
}

/* Initialize TX parameters */
static void
lldpd_txInitializeLLDP(lldp_agent_t *lap)
{
	/* Initialize LLDP TX variables */
	lap->la_txShutdownWhile =  lldp_reinitDelay;
	lap->la_txTTL = MIN(65535, (lldp_msgTxInterval * lldp_msgTxHold) + 1);

	(void) lldp_create_txFrame(lap, B_FALSE);
}

/* The Tx state machine */
void *
lldpd_tx_state_machine(void *args)
{
	lldp_agent_t	*lap = (lldp_agent_t *)args;
	uint16_t	newtxTTL;

restart_tsm:
	switch (lap->la_tx_state) {
	case LLDP_TX_INITIALIZE:
		lldpd_txInitializeLLDP(lap);
		lldp_mutex_lock(&lap->la_mutex);
		while ((!lap->la_portEnabled &&
		    lap->la_adminStatus != LLDP_MODE_DISABLE) ||
		    lap->la_adminStatus == LLDP_MODE_RXONLY ||
		    lap->la_adminStatus == LLDP_MODE_UNKNOWN) {
			(void) pthread_cond_wait(&lap->la_cond_var,
			    &lap->la_mutex);
		}
		if (lap->la_adminStatus == LLDP_MODE_DISABLE) {
			lldp_write2pdu_t	*wp;
			lldp_tlv_info_t		*tip;

			lldp_mutex_unlock(&lap->la_mutex);
			/*
			 * we need to call the TLV fini function for all
			 * the TLVs being advertised.
			 */
			lldp_rw_lock(&lap->la_txmib_rwlock, LLDP_RWLOCK_WRITER);
			while ((wp = list_head(&lap->la_write2pdu)) != NULL) {
				list_remove(&lap->la_write2pdu, wp);
				tip = wp->ltp_infop;
				if (tip->lti_finif != NULL)
					tip->lti_finif(lap);
				free(wp);
			}
			list_destroy(&lap->la_write2pdu);
			lldp_rw_unlock(&lap->la_txmib_rwlock);
			lldp_agent_refcnt_decr(lap);
			return (NULL);
		}
		lldp_mutex_unlock(&lap->la_mutex);
		LLDP_TX_STATE_CHANGE(lap->la_linkname,
		    lldp_state2str(lap->la_tx_state));
		lap->la_tx_state = LLDP_TX_IDLE;

	/* FALLTHRU */
	case LLDP_TX_IDLE:
		lldp_mutex_lock(&lap->la_mutex);
		while ((!lap->la_txNow || lap->la_txCredit == 0) &&
		    (lap->la_adminStatus == LLDP_MODE_TXONLY ||
		    lap->la_adminStatus == LLDP_MODE_RXTX) &&
		    lap->la_portEnabled) {
			(void) pthread_cond_wait(&lap->la_cond_var,
			    &lap->la_mutex);
		}
		newtxTTL = MIN(65535,
		    (lldp_msgTxInterval * lldp_msgTxHold) + 1);
		if (newtxTTL != lap->la_txTTL) {
			lap->la_txTTL = newtxTTL;
			/*
			 * Recreate the PDU with the right TTL.
			 */
			lap->la_tx_pdulen = 0;
		}
		if (!lap->la_portEnabled) {
			/*
			 * port got disabled there is nothing we
			 * can send.
			 */
			lldp_mutex_unlock(&lap->la_mutex);
			LLDP_TX_STATE_CHANGE(lap->la_linkname,
			    lldp_state2str(lap->la_tx_state));
			lap->la_tx_state = LLDP_TX_INITIALIZE;
		} else if (lap->la_txNow && lap->la_txCredit > 0) {
			if (lldp_send_txFrame(lap, B_FALSE) == 0)
				lap->la_txCredit--;
			lap->la_txNow = B_FALSE;
			lldp_mutex_unlock(&lap->la_mutex);
		} else {
			/* admin status changed. Send a Shutdown frame. */
			if (lldp_create_txFrame(lap, B_TRUE) == 0 &&
			    lldp_send_txFrame(lap, B_TRUE)) {
				lap->la_txShutdownWhile = lldp_reinitDelay;
				LLDP_TX_STATE_CHANGE(lap->la_linkname,
				    lldp_state2str(lap->la_tx_state));
				lap->la_tx_state = LLDP_PORT_SHUTDOWN;
				while (lap->la_txShutdownWhile != 0) {
					(void) pthread_cond_wait(
					    &lap->la_cond_var, &lap->la_mutex);
				}
			}
			LLDP_TX_STATE_CHANGE(lap->la_linkname,
			    lldp_state2str(lap->la_tx_state));
			lap->la_tx_state = LLDP_TX_INITIALIZE;
			lldp_mutex_unlock(&lap->la_mutex);
		}
		goto restart_tsm;
	}
	return (NULL);
}

/* The Tx timer state machine */
void *
lldpd_txtimer_state_machine(void *args)
{
	lldp_agent_t	*lap = (lldp_agent_t *)args;

restart_timer_sm:
	switch (lap->la_tx_timer_state) {
	case LLDP_TX_TIMER_INITIALIZE:
		lap->la_txTick = B_FALSE;
		lap->la_txNow = B_FALSE;
		lap->la_localChanges = B_FALSE;
		lap->la_txTTR = 0;
		lap->la_txFast = lldp_txFastInit;
		lap->la_txShutdownWhile = 0;
		lap->la_newNeighbor = B_FALSE;
		lap->la_txCredit = lldp_txCreditMax;

		lldp_mutex_lock(&lap->la_mutex);
		while ((!lap->la_portEnabled &&
		    lap->la_adminStatus != LLDP_MODE_DISABLE) ||
		    lap->la_adminStatus == LLDP_MODE_RXONLY ||
		    lap->la_adminStatus == LLDP_MODE_UNKNOWN) {
			(void) pthread_cond_wait(&lap->la_cond_var,
			    &lap->la_mutex);
		}
		if (lap->la_adminStatus == LLDP_MODE_DISABLE) {
			lldp_mutex_unlock(&lap->la_mutex);
			lldp_agent_refcnt_decr(lap);
			return (NULL);
		}
		lldp_mutex_unlock(&lap->la_mutex);
		lap->la_tx_timer_state = LLDP_TX_TIMER_IDLE;

	/* FALLTHRU */
	case LLDP_TX_TIMER_IDLE:
		lldp_mutex_lock(&lap->la_mutex);
		while (!lap->la_localChanges && lap->la_txTTR > 0 &&
		    !lap->la_newNeighbor && !lap->la_txTick &&
		    (lap->la_adminStatus == LLDP_MODE_TXONLY ||
		    lap->la_adminStatus == LLDP_MODE_RXTX) &&
		    lap->la_portEnabled) {
			(void) pthread_cond_wait(&lap->la_cond_var,
			    &lap->la_mutex);
		}
		if (lap->la_adminStatus == LLDP_MODE_DISABLE) {
			lldp_mutex_unlock(&lap->la_mutex);
			lldp_agent_refcnt_decr(lap);
			return (NULL);
		} else if (!lap->la_portEnabled ||
		    lap->la_adminStatus == LLDP_MODE_RXONLY) {
			lldp_mutex_unlock(&lap->la_mutex);
			lap->la_tx_timer_state = LLDP_TX_TIMER_INITIALIZE;
		} else if (lap->la_newNeighbor || lap->la_localChanges ||
		    lap->la_txTTR == 0) {
			if (lap->la_localChanges || lap->la_newNeighbor) {
				if (lap->la_txFast == 0)
					lap->la_txFast = lldp_txFastInit;
				if (lap->la_localChanges)
					lap->la_localChanges = B_FALSE;
				if (lap->la_newNeighbor)
					lap->la_newNeighbor = B_FALSE;
			}
			if (lap->la_txFast > 0)
				lap->la_txFast--;
			lap->la_txNow = B_TRUE;
			if (lap->la_txFast > 0)
				lap->la_txTTR = lldp_msgFastTx;
			else
				lap->la_txTTR = lldp_msgTxInterval;
			lldp_mutex_unlock(&lap->la_mutex);
			/* Signal TX state machine */
			(void) pthread_cond_broadcast(&lap->la_cond_var);
		} else if (lap->la_txTick) {
			lap->la_txTick = B_FALSE;
			lldp_txAddCredit(lap);
			lldp_mutex_unlock(&lap->la_mutex);
		}
		goto restart_timer_sm;
	}
	return (NULL);
}

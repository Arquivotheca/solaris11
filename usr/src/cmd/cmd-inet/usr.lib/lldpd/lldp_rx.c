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
 * This file contains routines that implements LLDP Receive state machine.
 */
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netpacket/packet.h>
#include <sys/ethernet.h>
#include "lldp_impl.h"
#include "lldpsnmp_impl.h"
#include "lldp_provider.h"

extern uint64_t		starttime;
static uint16_t		tooManyNeighborsTimer = 0;
lldp_remtable_stats_t	remtable_stats = {0};
/* lock to modify remote table stats as it as one 64 bit quantity */
pthread_mutex_t		remtable_stats_lock = PTHREAD_MUTEX_INITIALIZER;

extern int	lldp_cmp_rmib_objects(nvlist_t *, nvlist_t *, nvlist_t *,
		    nvlist_t *, nvlist_t *);

#define	LLDP_REMTABLE_STATS_INCR(counter) {				\
	lldp_mutex_lock(&remtable_stats_lock);				\
	if (snmp_enabled) {						\
		remtable_stats.lrs_stats_RemTablesLastChangeTime =	\
		    netsnmp_get_agent_uptime();				\
	}								\
	remtable_stats.counter++;					\
	lldp_mutex_unlock(&remtable_stats_lock);			\
}

/*
 * This fucntion is called whenever a timer expires for a remote MSAP. What
 * this means is that the information for the remote is invalid and needs to
 * be purged.
 */
static void
lldp_rxinfottl_timer_cbfunc(void *args)
{
	lldp_agent_t	*lap = args;

	lldp_mutex_lock(&lap->la_rx_mutex);
	lap->la_rxInfoAge = B_TRUE;
	(void) pthread_cond_signal(&lap->la_rx_cv);
	lldp_mutex_unlock(&lap->la_rx_mutex);
	lldp_agent_refcnt_decr(lap);
}

static void
lldp_toomanyneighbors_timer_cbfun(void *args)
{
	lldp_agent_t	*lap = args;

	lap->la_tooManyNeighbors = B_FALSE;
	lldp_agent_refcnt_decr(lap);
}

void
lldp_nvlist2msap(nvlist_t *tlv_nvl, char *msap, size_t msaplen)
{
	char		pidstr[LLDP_MAX_PORTIDSTRLEN];
	char		cidstr[LLDP_MAX_CHASSISIDSTRLEN];
	lldp_portid_t	pid;
	lldp_chassisid_t cid;

	/* retrieve chassisID as a string */
	(void) lldp_nvlist2chassisid(tlv_nvl, &cid);
	(void) lldp_chassisID2str(&cid, cidstr, sizeof (cidstr));

	/* retrieve portID as a string */
	(void) lldp_nvlist2portid(tlv_nvl, &pid);
	(void) lldp_portID2str(&pid, pidstr, sizeof (pidstr));

	/*
	 * we already verified the chassisID and portID, so they better
	 * not be NULL.
	 */
	(void) snprintf(msap, msaplen, "%s_%s", cidstr, pidstr);
}

/*
 * Stops the current rxInfoTTL timer for the given MSAP and adds
 * a new rxInfoTTL timer with new value from rxTTL
 */
static void
lldp_modify_timer(lldp_agent_t *lap, const char *msap, uint16_t rxTTL)
{
	nvlist_t	*mib_nvl;
	uint_t		timerid;
	uint64_t	rxinfo_age;
	struct timeval	time, curtime;
	int		err = 0;

	lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_WRITER);

	if (nvlist_lookup_nvlist(lap->la_remote_mib, msap, &mib_nvl) != 0 ||
	    nvlist_lookup_uint32(mib_nvl, LLDP_NVP_RXINFO_TIMER_ID,
	    &timerid) != 0) {
		syslog(LOG_WARNING, "could not untimeout the timer for MSAP %s",
		    msap);
		goto ret;
	}
	if (lldp_untimeout(timerid))
		lldp_agent_refcnt_decr(lap);

	(void) gettimeofday(&curtime, NULL);
	rxinfo_age = curtime.tv_sec + rxTTL;
	if (nvlist_add_uint64(mib_nvl, LLDP_NVP_RXINFO_AGE_ABSTIME,
	    rxinfo_age) != 0) {
		syslog(LOG_WARNING, "could not add rxInfoTTL timer for MSAP %s",
		    msap);
		goto ret;
	}

	time.tv_sec = rxTTL;
	time.tv_usec = 0;
	lldp_agent_refcnt_incr(lap);
	timerid = lldp_timeout(lap, lldp_rxinfottl_timer_cbfunc, &time);
	if (timerid == 0) {
		lldp_agent_refcnt_decr(lap);
		err = -1;
	} else if (nvlist_add_uint32(mib_nvl, LLDP_NVP_RXINFO_TIMER_ID,
	    timerid) != 0) {
		if (lldp_untimeout(timerid))
			lldp_agent_refcnt_decr(lap);
		err = -1;
	}
	if (err != 0) {
		syslog(LOG_WARNING, "could not add rxInfoTTL timer for MSAP %s",
		    msap);
	}
ret:
	lldp_rw_unlock(&lap->la_rxmib_rwlock);
}

/*
 * This is called whenever information has aged (i.e., rxInfoAge is B_TRUE).
 */
static void
lldp_remote_info_aged(lldp_agent_t *lap)
{
	struct timeval	curtime;
	nvlist_t	*envl, *aged_nvl, *mib_nvl;
	nvpair_t	*nvp;
	uint64_t	rxinfo_age;
	char 		*name;

	LLDP_STATS_INCR(lap->la_stats, ls_stats_AgeoutsTotal);
	lap->la_remoteChanges = B_TRUE;

	lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_WRITER);
	(void) gettimeofday(&curtime, NULL);
	if (nvlist_alloc(&aged_nvl, NV_UNIQUE_NAME, 0) != 0)
		aged_nvl = NULL;
	for (nvp = nvlist_next_nvpair(lap->la_remote_mib, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(lap->la_remote_mib, nvp)) {
		if (nvpair_value_nvlist(nvp, &mib_nvl) != 0 ||
		    nvlist_lookup_uint64(mib_nvl, LLDP_NVP_RXINFO_AGE_ABSTIME,
		    &rxinfo_age) != 0) {
			continue;
		}
		name = nvpair_name(nvp);
		if (rxinfo_age <= curtime.tv_sec) {
			(void) nvlist_add_nvlist(aged_nvl, name, mib_nvl);
			(void) nvlist_remove(lap->la_remote_mib,
			    name, DATA_TYPE_NVLIST);
			LLDP_REMTABLE_STATS_INCR(lrs_stats_RemTablesAgeouts);
		}
	}
	lldp_rw_unlock(&lap->la_rxmib_rwlock);

	/*
	 * walk through the list of peers for which information has aged
	 * and send out remote changed event.
	 */
	for (nvp = nvlist_next_nvpair(aged_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(aged_nvl, nvp)) {
		if (nvpair_value_nvlist(nvp, &mib_nvl) != 0)
			continue;
		if (nvlist_alloc(&envl, NV_UNIQUE_NAME, 0) != 0)
			continue;
		if (lldp_add_peer_identity(envl, mib_nvl) != 0 ||
		    nvlist_add_uint32(envl, LLDP_CHANGE_TYPE,
		    LLDP_REMOTE_INFOAGE) != 0) {
			nvlist_free(envl);
			continue;
		}
		lldp_something_changed_remote(lap, envl);
		nvlist_free(envl);
	}
	nvlist_free(aged_nvl);
}

/*
 * This is called whenever we receive an LLDPDU with TTL value of zero. This
 * indicates that the remote is shuting down and we have to delete all the
 * information associated with the remote.
 */
static void
lldp_remote_shutdown(lldp_agent_t *lap, const char *msap)
{
	uint32_t	timerid;
	nvlist_t	*mib_nvl, *envl;
	boolean_t	send_event = B_FALSE;

	lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_WRITER);
	if (nvlist_lookup_nvlist(lap->la_remote_mib, msap, &mib_nvl) != 0 ||
	    nvlist_lookup_uint32(mib_nvl, LLDP_NVP_RXINFO_TIMER_ID,
	    &timerid) != 0) {
		lldp_rw_unlock(&lap->la_rxmib_rwlock);
		syslog(LOG_ERR, "Recieved Shutdown LLDPDU. Could "
		    "not cancel the rxInfoTTL timer for MSAP %s", msap);
		return;
	}
	if (lldp_untimeout(timerid))
		lldp_agent_refcnt_decr(lap);

	/* Capture information needed to send a remote change event */
	if (nvlist_alloc(&envl, NV_UNIQUE_NAME, 0) == 0) {
		if (lldp_add_peer_identity(envl, mib_nvl) == 0 &&
		    nvlist_add_uint32(envl, LLDP_CHANGE_TYPE,
		    LLDP_REMOTE_SHUTDOWN) == 0) {
			send_event = B_TRUE;
		} else {
			nvlist_free(envl);
		}
	}
	(void) nvlist_remove(lap->la_remote_mib, msap, DATA_TYPE_NVLIST);
	LLDP_REMTABLE_STATS_INCR(lrs_stats_RemTablesDeletes);
	lldp_rw_unlock(&lap->la_rxmib_rwlock);

	if (send_event) {
		lldp_something_changed_remote(lap, envl);
		nvlist_free(envl);
	}
}

/*
 * Updates the LLDP agent`s remote MIB with new remote information for the
 * given `msap'
 */
static void
lldp_mibUpdateObjects(lldp_agent_t *lap, nvlist_t *tlv_nvl, const char *msap)
{
	nvlist_t	*mib_nvl;
	uint32_t	old_timerid, new_timerid;
	uint64_t	rxinfo_age;
	uint16_t	rxTTL;
	struct timeval	time, curtime;
	uint32_t	remIfindex;
	int		err = 0;

	lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_WRITER);
	if (!lap->la_newNeighbor) {
		(void) nvlist_lookup_nvlist(lap->la_remote_mib, msap,
		    &mib_nvl);
		(void) nvlist_lookup_uint32(mib_nvl, LLDP_NVP_RXINFO_TIMER_ID,
		    &old_timerid);
		if (lldp_untimeout(old_timerid))
			lldp_agent_refcnt_decr(lap);
		(void) nvlist_lookup_uint32(mib_nvl, LLDP_NVP_REMIFINDEX,
		    &remIfindex);
		(void) nvlist_remove(lap->la_remote_mib, msap,
		    DATA_TYPE_NVLIST);
	} else {
		remIfindex = (lap->la_remote_index)++;
		LLDP_REMTABLE_STATS_INCR(lrs_stats_RemTablesInserts);
	}

	/* update the remote mib with new information */
	(void) nvlist_lookup_uint16(tlv_nvl, LLDP_NVP_TTL, &rxTTL);
	(void) gettimeofday(&curtime, NULL);
	rxinfo_age = curtime.tv_sec + rxTTL;
	(void) nvlist_add_uint64(tlv_nvl, LLDP_NVP_RXINFO_AGE_ABSTIME,
	    rxinfo_age);
	/* add the remote index */
	(void) nvlist_add_uint32(tlv_nvl, LLDP_NVP_REMIFINDEX, remIfindex);
	/* add the sysUptime */
	(void) nvlist_add_uint64(tlv_nvl, LLDP_NVP_REMSYSDATA_UPDTIME,
	    netsnmp_get_agent_uptime());
	(void) nvlist_add_nvlist(lap->la_remote_mib, msap, tlv_nvl);

	/* enable the rxInfoTTL timer for this MSAP */
	time.tv_sec = rxTTL;
	time.tv_usec = 0;
	lldp_agent_refcnt_incr(lap);
	new_timerid = lldp_timeout(lap, lldp_rxinfottl_timer_cbfunc, &time);
	if (new_timerid == 0) {
		lldp_agent_refcnt_decr(lap);
		err = -1;
	} else {
		/* add the timerid into the nvlist */
		(void) nvlist_lookup_nvlist(lap->la_remote_mib, msap,
		    &mib_nvl);
		if (nvlist_add_uint32(mib_nvl, LLDP_NVP_RXINFO_TIMER_ID,
		    new_timerid) != 0) {
			if (lldp_untimeout(new_timerid))
				lldp_agent_refcnt_decr(lap);
			err = -1;
		}
	}
	if (err != 0) {
		syslog(LOG_ERR, "could not add rxInfoTTL timer for MSAP %s",
		    msap);
	}

	lldp_rw_unlock(&lap->la_rxmib_rwlock);
}

/*
 * Parses the given LLDPDU and verifies the validitiy of the constituent TLV's.
 * Parsing and validation is done by calling the respective TLV parse funciton.
 * If the PDU is invalid, appropriate stat counters will be incremented. If a
 * parse function returns.
 *
 *	EPROTO - its a protocol error. We discard the entire LLDPDU and return
 *		to the RX state machine.
 *	ENOMEM - we trigger too many neighbors condition.
 *	Other - we discard the TLV and proceed with the next TLV in the LLDPDU.
 *
 * At the end of parsing of TLVs in LLDPDU, we get an nvlist with TLV data in
 * it. We check to see if this LLDPDU is from a new neighbor or from an existing
 * neighbor. If it is from an existing neighbor we check to see if the LLDPDU
 * recevied differs from the one we already have. Any differences will be
 * captured in `change_nvl'.
 */
static int
lldp_rxProcessFrame(lldp_agent_t *lap, uint8_t *pdu, size_t pdulen,
    nvlist_t **tlv_nvl, nvlist_t **change_nvl)
{
	lldp_tlv_info_t	*tlvinfop;
	nvlist_t	*rmib_nvl = NULL;
	nvlist_t	*deleted_tlvnvl = NULL, *added_tlvnvl = NULL;
	nvlist_t	*modified_tlvnvl = NULL;
	uint8_t		*end, ntlv = 1;
	char		msap[LLDP_MAX_MSAPSTRLEN];
	int		err = 0;
	struct timeval	time;
	uint16_t	rxTTL = 0;

	LLDP_STATS_INCR(lap->la_stats, ls_stats_FramesInTotal);
	LLDP_PKT_RECV(lap->la_linkname, pdulen);

	*tlv_nvl = NULL;
	if (nvlist_alloc(tlv_nvl, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_alloc(change_nvl, NV_UNIQUE_NAME, 0) != 0 ||
	    lldp_create_nested_nvl(*change_nvl, LLDP_ADDED_TLVS, NULL,
	    NULL, &added_tlvnvl) != 0 ||
	    lldp_create_nested_nvl(*change_nvl, LLDP_DELETED_TLVS, NULL,
	    NULL, &deleted_tlvnvl) != 0 ||
	    lldp_create_nested_nvl(*change_nvl, LLDP_MODIFIED_TLVS, NULL,
	    NULL, &modified_tlvnvl) != 0) {
		syslog(LOG_ERR, "unable to allocate nvlist(s) to capture the "
		    "incoming frame, added/deleted/modified TLVs");
		goto toomanyneighbors;
	}

	end = pdu + pdulen;
	while (pdu < end) {
		lldp_tlv_t	tlv;
		uint8_t		tlvtype, stype = 0, *tlvvalue;
		uint16_t	tlvlen;
		uint32_t	oui = 0;

		/* Check if we at least have the Type/Len in the PDU */
		if ((pdu + LLDP_TLVHDR_SZ) > end) {
			syslog(LOG_ERR, "Malformed PDU: Type/Length missing");
			goto discard_pdu;
		}
		tlvtype = tlv.lt_type = LLDP_TLV_TYPE(pdu);
		tlvlen = tlv.lt_len = LLDP_TLV_LEN(pdu);
		/* Check if we at least len in the PDU */
		if ((pdu + LLDP_TLVHDR_SZ + tlvlen) > end) {
			syslog(LOG_ERR, "Malformed PDU: Incorrect length");
			goto discard_pdu;
		}
		tlvvalue = tlv.lt_value = pdu + 2;
		if (tlvtype == 127) {
			oui = LLDP_ORGTLV_OUI(tlvvalue);
			stype = LLDP_ORGTLV_STYPE(tlvvalue);
		}

		/* fire dtrace probe */
		LLDP_TLV_RECV(tlvtype, tlvlen, oui, stype);

		/*
		 * the first 3 tlv's should be chassis_id, port_id and ttl
		 * and in that order only.
		 */
		if ((ntlv == 1 && tlvtype != LLDP_TLVTYPE_CHASSIS_ID) ||
		    (ntlv == 2 && tlvtype != LLDP_TLVTYPE_PORT_ID) ||
		    (ntlv == 3 && tlvtype != LLDP_TLVTYPE_TTL)) {
			syslog(LOG_ERR, "first 3 tlvs were incorrect");
			goto discard_pdu;
		}

		if ((tlvinfop = lldp_get_tlvinfo(tlvtype, oui,
		    stype)) == NULL) {
			goto discard_pdu;
		} else {
			/*
			 * Check to see if we recognize this TLV. If we don't
			 * then increment the appropriate counters.
			 */
			if (tlvinfop->lti_type == LLDP_TLVTYPE_RESERVED ||
			    (tlvinfop->lti_type == LLDP_ORGSPECIFIC_TLVTYPE &&
			    tlvinfop->lti_oui == 0 &&
			    tlvinfop->lti_stype == 0)) {
				LLDP_STATS_INCR(lap->la_stats,
				    ls_stats_TLVSUnrecognizedTotal);
				LLDP_STATS_INCR(lap->la_stats,
				    ls_stats_FramesInErrorsTotal);
			}
		}

		if ((err = tlvinfop->lti_parsef(lap, &tlv, *tlv_nvl)) != 0) {
			syslog(LOG_ERR, "parsing of tlv %d failed", tlvtype);
			switch (err) {
			case ENOMEM:
				goto toomanyneighbors;
			case EPROTO:
				goto discard_pdu;
			default:
				/*
				 * we discard the TLV, increment the stats
				 * and make progress.
				 */
				LLDP_TLV_RECV_DISCARD(tlvtype, oui, stype);
				LLDP_STATS_INCR(lap->la_stats,
				    ls_stats_TLVSDiscardedTotal);
				LLDP_STATS_INCR(lap->la_stats,
				    ls_stats_FramesInErrorsTotal);
				break;
			}
		}

		if (tlvtype == LLDP_TLVTYPE_END)
			break;

		if (ntlv == 3) {
			assert(tlvtype == LLDP_TLVTYPE_TTL);
			/*
			 * Extract the ttl value and if it is 0, a shutdown
			 * frame has been received and we terminate further
			 * LLDPDU validation and return to the state machine.
			 */
			lap->la_rxTTL = ntohs(*(uint16_t *)(void *)tlvvalue);
			if (lap->la_rxTTL == 0)
				return (0);
		}

		/* increment the number of tlv's processed */
		++ntlv;
		pdu += tlvlen + 2;
	}
	/* Let us identify the neighbor who is sending us these messages. */
	lldp_nvlist2msap(*tlv_nvl, msap, sizeof (msap));
	(void) nvlist_lookup_nvlist(lap->la_remote_mib, msap, &rmib_nvl);

	/* check if the neighbor is new */
	if (rmib_nvl == NULL) {
		/* new neighbor */

		/*
		 * `la_newNeighbor' is reset by the transmit timer
		 * state machine.
		 */
		lap->la_newNeighbor = B_TRUE;
		lap->la_rxChanges = B_TRUE;
		lap->la_tooManyNeighbors = B_FALSE;
		if ((err = nvlist_merge(added_tlvnvl, *tlv_nvl, 0)) != 0) {
			goto toomanyneighbors;
		}
	} else {
		int	change;
		nvlist_t *dup_nvl = NULL;

		if (nvlist_dup(*tlv_nvl, &dup_nvl, 0) != 0 ||
		    (change = lldp_cmp_rmib_objects(rmib_nvl, dup_nvl,
		    added_tlvnvl, deleted_tlvnvl, modified_tlvnvl)) < 0) {
			nvlist_free(dup_nvl);
			goto toomanyneighbors;
		}
		nvlist_free(dup_nvl);
		lap->la_rxChanges = (change > 0);
	}
	if (lap->la_rxChanges) {
		if (lldp_add_peer_identity(*change_nvl, *tlv_nvl) != 0 ||
		    nvlist_add_uint32(*change_nvl, LLDP_CHANGE_TYPE,
		    LLDP_REMOTE_CHANGED) != 0) {
			goto toomanyneighbors;
		}
	}
	return (0);

toomanyneighbors:
	LLDP_PKT_RECV_TOOMANY_NEIGHBORS(lap->la_linkname);
	LLDP_STATS_INCR(lap->la_stats, ls_stats_FramesDiscardedTotal);
	lap->la_tooManyNeighbors = B_TRUE;
	if (*tlv_nvl != NULL)
		(void) nvlist_lookup_uint16(*tlv_nvl, LLDP_NVP_TTL, &rxTTL);
	time.tv_sec = tooManyNeighborsTimer = MAX(tooManyNeighborsTimer, rxTTL);
	time.tv_usec = 0;
	lldp_agent_refcnt_incr(lap);
	if (lldp_timeout(lap, lldp_toomanyneighbors_timer_cbfun, &time) == 0)
		lldp_agent_refcnt_decr(lap);
	err = 0;
	goto fail;

discard_pdu:
	LLDP_PKT_RECV_DISCARD(lap->la_linkname);
	LLDP_STATS_INCR(lap->la_stats, ls_stats_FramesDiscardedTotal);
	LLDP_STATS_INCR(lap->la_stats, ls_stats_FramesInErrorsTotal);
	lap->la_badFrame = B_TRUE;

fail:
	nvlist_free(*change_nvl);
	*change_nvl = NULL;
	nvlist_free(*tlv_nvl);
	*tlv_nvl = NULL;
	return (err);
}

/*
 * Initialize Rx side
 */
static int
lldpd_rxInitializeLLDP(lldp_agent_t *lap)
{
	int	err;

	lap->la_tooManyNeighbors = B_FALSE;

	/* Delete all the information in remote MIB datastore */
	lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_WRITER);
	nvlist_free(lap->la_remote_mib);
	lap->la_remote_mib = NULL;
	err = nvlist_alloc(&lap->la_remote_mib, NV_UNIQUE_NAME, 0);
	lldp_rw_unlock(&lap->la_rxmib_rwlock);
	return (err);
}

static void *
lldpd_get_lldpdus_cleanup(void *arg)
{
	lldp_agent_t	*lap = arg;

	lldp_mutex_unlock(&lap->la_nextpkt_mutex);
	lldp_agent_refcnt_decr(lap);
	return (NULL);
}

/*
 * Thread that reads the LLDP socket for an agent. If we read a packet
 * we set rxrcvFrame to true and signal the threads waiting for this
 * event.
 */
static void *
lldpd_get_lldpdus(void *arg) {
	lldp_agent_t	*lap = arg;
	uint8_t		buf[LLDP_MAX_PDULEN];
	ssize_t		pdulen = LLDP_MAX_PDULEN;
	struct ether_header *eth;

	pthread_cleanup_push(lldpd_get_lldpdus_cleanup, arg);
	for (;;) {
		lldp_mutex_lock(&lap->la_nextpkt_mutex);
		/*
		 * do not read the packet until the current packet is
		 * processed and the rx state machine sets rxrcvFrame
		 * to B_FALSE
		 */
		while (lap->la_rxrcvFrame)
			(void) pthread_cond_wait(&lap->la_nextpkt_cv,
			    &lap->la_nextpkt_mutex);
		/*
		 * We are holding a lock and making a blocking call. It's OK,
		 * as the rx_state_machine cannot proceed until we have
		 * read something. If we unlock the mutex before calling read,
		 * and say, we received a pthread_cancel() request then read
		 * will be interrupted and the lldpd_get_lldpdus_cleanup()
		 * will try to unlock the mutex, again, which is bad.
		 */
		pdulen = read(lap->la_rx_sockfd, buf, LLDP_MAX_PDULEN);
		lldp_mutex_unlock(&lap->la_nextpkt_mutex);
		if (pdulen == -1)
			continue;
		eth = (struct ether_header *)(void *)buf;
		/* ignore packets originating from this lldp agent */
		if (bcmp(eth->ether_shost.ether_addr_octet,
		    lap->la_physaddr, lap->la_physaddrlen) == 0)
			continue;

		/*
		 * Copy over the remote's MAC address, this is used by
		 * symmetric state machine of DCBX. When there are multiple
		 * peers, we overwrite this value with the latest peers MAC
		 * Address. This is fine, as we need the DCBX data from the
		 * latest peer till we detect the multi-peer condition.
		 */
		bcopy(eth->ether_shost.ether_addr_octet, lap->la_rmac,
		    ETHERADDRL);
		lap->la_rmaclen = ETHERADDRL;

		/* we received LLDPDU */
		lap->la_pdu = buf + sizeof (struct ether_header);
		lap->la_pdulen = pdulen - sizeof (struct ether_header);

		lldp_mutex_lock(&lap->la_rx_mutex);
		lap->la_rxrcvFrame = B_TRUE;
		(void) pthread_cond_broadcast(&lap->la_rx_cv);
		lldp_mutex_unlock(&lap->la_rx_mutex);
	}

	/* NOTREACHED */
	pthread_cleanup_pop(0);
	return (NULL);
}

/*
 * On disabling lldp agent, we need to remove all the timers for
 * all the neighbors.
 */
static void
lldp_remove_timers(lldp_agent_t *lap)
{
	nvlist_t	*mib_nvl, *nvl;
	nvpair_t	*nvp;
	uint_t		timerid;

	/*
	 * No need to hold locks here as we were called to perform cleanup
	 * from the exiting rx_state_machine. Therefore there will be no
	 * updates to remote mib.
	 */
	nvl = lap->la_remote_mib;
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		if (nvpair_value_nvlist(nvp, &mib_nvl) != 0)
			continue;
		if (nvlist_lookup_uint32(mib_nvl, LLDP_NVP_RXINFO_TIMER_ID,
		    &timerid) != 0)
			continue;
		if (lldp_untimeout(timerid))
			lldp_agent_refcnt_decr(lap);
	}
	lldp_agent_refcnt_decr(lap);
}

/* The Rx state machine */
void *
lldpd_rx_state_machine(void *args)
{
	lldp_agent_t	*lap = args;
	nvlist_t	*tlv_nvl = NULL, *change_nvl = NULL;
	char		msap[LLDP_MAX_MSAPSTRLEN];

restart_rsm:
	switch (lap->la_rx_state) {
	case LLDP_PORT_DISABLED:
		/* Wait for la_rxInfoAge to be true or port to be enabled */
		lldp_mutex_lock(&lap->la_rx_mutex);
		while (!lap->la_rxInfoAge && !lap->la_portEnabled &&
		    lap->la_adminStatus != LLDP_MODE_DISABLE) {
			(void) pthread_cond_wait(&lap->la_rx_cv,
			    &lap->la_rx_mutex);
		}
		if (lap->la_adminStatus == LLDP_MODE_DISABLE) {
			lldp_mutex_unlock(&lap->la_rx_mutex);
			lldp_remove_timers(lap);
			return (NULL);
		}
		if (lap->la_rxInfoAge) {
			lldp_remote_info_aged(lap);
			lap->la_rxInfoAge = B_FALSE;
			lldp_mutex_unlock(&lap->la_rx_mutex);
			goto restart_rsm;
		}
		LLDP_RX_STATE_CHANGE(lap->la_linkname,
		    lldp_state2str(lap->la_rx_state));
		lap->la_rx_state = LLDP_RX_INITIALIZE;
		lldp_mutex_unlock(&lap->la_rx_mutex);

	/* FALLTHRU */
	case LLDP_RX_INITIALIZE:
		if (lldpd_rxInitializeLLDP(lap) != 0) {
			syslog(LOG_ERR, "Error initializing RX state machine "
			    "for %s", lap->la_linkname);
			lldp_agent_refcnt_decr(lap);
			return (NULL);
		}
		lap->la_rxrcvFrame = B_FALSE;
		lldp_mutex_lock(&lap->la_rx_mutex);
		while (lap->la_adminStatus == LLDP_MODE_TXONLY ||
		    lap->la_adminStatus == LLDP_MODE_UNKNOWN) {
			(void) pthread_cond_wait(&lap->la_rx_cv,
			    &lap->la_rx_mutex);
		}
		if (lap->la_adminStatus == LLDP_MODE_DISABLE) {
			lldp_mutex_unlock(&lap->la_rx_mutex);
			lldp_remove_timers(lap);
			return (NULL);
		}
		lldp_mutex_unlock(&lap->la_rx_mutex);
		LLDP_RX_STATE_CHANGE(lap->la_linkname,
		    lldp_state2str(lap->la_rx_state));
		lap->la_rx_state = LLDP_RX_WAIT_FOR_FRAME;
		lap->la_badFrame = B_FALSE;
		lap->la_rxInfoAge = B_FALSE;
		lap->la_rxrcvFrame = B_FALSE;
		lldp_agent_refcnt_incr(lap);
		(void) pthread_create(&lap->la_rx_thr, NULL,
		    lldpd_get_lldpdus, (void *)lap);
		(void) pthread_detach(lap->la_rx_thr);

	/* FALLTHRU */
	case LLDP_RX_WAIT_FOR_FRAME:
		for (;;) {
			lldp_mutex_lock(&lap->la_rx_mutex);
			while (!lap->la_rxInfoAge && !lap->la_rxrcvFrame &&
			    (lap->la_adminStatus == LLDP_MODE_RXTX ||
			    lap->la_adminStatus == LLDP_MODE_RXONLY) &&
			    lap->la_portEnabled) {
				(void) pthread_cond_wait(&lap->la_rx_cv,
				    &lap->la_rx_mutex);
			}
			if (!lap->la_portEnabled) {
				LLDP_RX_STATE_CHANGE(lap->la_linkname,
				    lldp_state2str(lap->la_rx_state));
				lap->la_rx_state = LLDP_PORT_DISABLED;
				(void) pthread_cancel(lap->la_rx_thr);
				lldp_mutex_unlock(&lap->la_rx_mutex);
				goto restart_rsm;
			}
			if (lap->la_adminStatus != LLDP_MODE_RXTX &&
			    lap->la_adminStatus != LLDP_MODE_RXONLY) {
				LLDP_RX_STATE_CHANGE(lap->la_linkname,
				    lldp_state2str(lap->la_rx_state));
				lap->la_rx_state = LLDP_RX_INITIALIZE;
				(void) pthread_cancel(lap->la_rx_thr);
				lldp_mutex_unlock(&lap->la_rx_mutex);
				if (lap->la_adminStatus == LLDP_MODE_DISABLE) {
					lldp_remove_timers(lap);
					return (NULL);
				}
				goto restart_rsm;
			}
			if (lap->la_rxInfoAge) {
				lldp_remote_info_aged(lap);
				lap->la_rxInfoAge = B_FALSE;
				lldp_mutex_unlock(&lap->la_rx_mutex);
				continue;
			}
			if (lap->la_rxrcvFrame) {
				LLDP_RX_STATE_CHANGE(lap->la_linkname,
				    lldp_state2str(lap->la_rx_state));
				lap->la_rx_state = LLDP_RX_FRAME;
				break;
			}
		}
		lldp_mutex_unlock(&lap->la_rx_mutex);

	/* FALLTHRU */
	case LLDP_RX_FRAME:
		assert(lap->la_rxrcvFrame == B_TRUE);
		lap->la_rxChanges = B_FALSE;
		lap->la_badFrame = B_FALSE;
		if (lldp_rxProcessFrame(lap, lap->la_pdu, lap->la_pdulen,
		    &tlv_nvl, &change_nvl) == 0 && !lap->la_badFrame &&
		    !lap->la_tooManyNeighbors) {
			lldp_nvlist2msap(tlv_nvl, msap, sizeof (msap));
			lap->la_remoteChanges = (lap->la_rxChanges &&
			    lap->la_rxTTL != 0);
			if (lap->la_rxTTL == 0)
				lldp_remote_shutdown(lap, msap);
			else if (lap->la_rxChanges)
				lldp_mibUpdateObjects(lap, tlv_nvl, msap);
			else
				lldp_modify_timer(lap, msap, lap->la_rxTTL);
			/* now inform the world, if there were any changes */
			if (lap->la_rxChanges)
				lldp_something_changed_remote(lap, change_nvl);
			/* Signal the Transmit Timer state machine */
			if (lap->la_newNeighbor) {
				(void) pthread_cond_broadcast(
				    &lap->la_cond_var);
			}
		}
		nvlist_free(tlv_nvl);
		nvlist_free(change_nvl);
		tlv_nvl = change_nvl = NULL;
		LLDP_RX_STATE_CHANGE(lap->la_linkname,
		    lldp_state2str(lap->la_rx_state));
		lap->la_rx_state = LLDP_RX_WAIT_FOR_FRAME;
		/* signal the lldp_get_lldpdus to get next lldp packet */
		lldp_mutex_lock(&lap->la_nextpkt_mutex);
		lap->la_rxrcvFrame = B_FALSE;
		(void) pthread_cond_signal(&lap->la_nextpkt_cv);
		lldp_mutex_unlock(&lap->la_nextpkt_mutex);
		goto restart_rsm;
	}
	return (NULL);
}

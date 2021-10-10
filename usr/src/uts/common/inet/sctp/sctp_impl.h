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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_INET_SCTP_SCTP_IMPL_H
#define	_INET_SCTP_SCTP_IMPL_H

#include <sys/inttypes.h>
#include <sys/taskq.h>
#include <sys/list.h>
#include <sys/strsun.h>
#include <sys/zone.h>
#include <sys/cpuvar.h>
#include <sys/clock_impl.h>

#include <netinet/ip6.h>
#include <inet/optcom.h>
#include <inet/tunables.h>
#include <netinet/sctp.h>
#include <inet/sctp_itf.h>
#include <inet/tcpcong.h>
#include <inet/sctp/sctp_stack.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* Streams device identifying info and version */
#define	SCTP_DEV_IDINFO	"SCTP Streams device 1.0"

#define	SSN_GT(a, b)	((int16_t)((a)-(b)) > 0)
#define	SSN_GE(a, b)	((int16_t)((a)-(b)) >= 0)

/* Default buffer size and flow control wake up threshold. */
#define	SCTP_XMIT_LOWATER	8192
#define	SCTP_XMIT_HIWATER	102400
#define	SCTP_RECV_LOWATER	8192
#define	SCTP_RECV_HIWATER	102400

/* SCTP Timer control structure */
typedef struct sctpt_s {
	pfv_t	sctpt_pfv;	/* The routine we are to call */
	struct sctp_s *sctpt_sctp;	/* The parameter we are to pass in */
	struct sctp_faddr_s *sctpt_faddr;
} sctpt_t;

/*
 * Maximum number of duplicate TSNs we can report. This is currently
 * static, and governs the size of the mblk used to hold the duplicate
 * reports. The use of duplcate TSN reports is currently experimental,
 * so for now a static limit should suffice.
 */
#define	SCTP_DUP_MBLK_SZ	64

#define	SCTP_IS_ADDR_UNSPEC(isv4, addr)		\
	((isv4) ? IN6_IS_ADDR_V4MAPPED_ANY(&(addr)) :	\
	IN6_IS_ADDR_UNSPECIFIED(&(addr)))

/*
 * SCTP properties/tunables
 */
#define	sctps_max_init_retr		sctps_propinfo_tbl[0].prop_cur_uval
#define	sctps_max_init_retr_high	sctps_propinfo_tbl[0].prop_max_uval
#define	sctps_max_init_retr_low		sctps_propinfo_tbl[0].prop_min_uval
#define	sctps_pa_max_retr		sctps_propinfo_tbl[1].prop_cur_uval
#define	sctps_pa_max_retr_high		sctps_propinfo_tbl[1].prop_max_uval
#define	sctps_pa_max_retr_low		sctps_propinfo_tbl[1].prop_min_uval
#define	sctps_pp_max_retr		sctps_propinfo_tbl[2].prop_cur_uval
#define	sctps_pp_max_retr_high		sctps_propinfo_tbl[2].prop_max_uval
#define	sctps_pp_max_retr_low		sctps_propinfo_tbl[2].prop_min_uval
#define	sctps_cwnd_max_			sctps_propinfo_tbl[3].prop_cur_uval
#define	sctps_smallest_nonpriv_port	sctps_propinfo_tbl[4].prop_cur_uval
#define	sctps_ipv4_ttl			sctps_propinfo_tbl[5].prop_cur_uval
#define	sctps_heartbeat_interval	sctps_propinfo_tbl[6].prop_cur_uval
#define	sctps_heartbeat_interval_high	sctps_propinfo_tbl[6].prop_max_uval
#define	sctps_heartbeat_interval_low	sctps_propinfo_tbl[6].prop_min_uval
#define	sctps_initial_mtu		sctps_propinfo_tbl[7].prop_cur_uval
#define	sctps_mtu_probe_interval	sctps_propinfo_tbl[8].prop_cur_uval
#define	sctps_new_secret_interval	sctps_propinfo_tbl[9].prop_cur_uval
#define	sctps_deferred_ack_interval	sctps_propinfo_tbl[10].prop_cur_uval
#define	sctps_deferred_ack_interval_high \
	sctps_propinfo_tbl[10].prop_max_uval
#define	sctps_deferred_ack_interval_low	sctps_propinfo_tbl[10].prop_min_uval
#define	sctps_snd_lowat_fraction	sctps_propinfo_tbl[11].prop_cur_uval
#define	sctps_ignore_path_mtu		sctps_propinfo_tbl[12].prop_cur_bval
#define	sctps_initial_ssthresh		sctps_propinfo_tbl[13].prop_cur_uval
#define	sctps_smallest_anon_port	sctps_propinfo_tbl[14].prop_cur_uval
#define	sctps_largest_anon_port		sctps_propinfo_tbl[15].prop_cur_uval
#define	sctps_xmit_hiwat		sctps_propinfo_tbl[16].prop_cur_uval
#define	sctps_xmit_lowat		sctps_propinfo_tbl[17].prop_cur_uval
#define	sctps_recv_hiwat		sctps_propinfo_tbl[18].prop_cur_uval
#define	sctps_max_buf			sctps_propinfo_tbl[19].prop_cur_uval
#define	sctps_rtt_updates		sctps_propinfo_tbl[20].prop_cur_uval
#define	sctps_ipv6_hoplimit		sctps_propinfo_tbl[21].prop_cur_uval
#define	sctps_rto_ming			sctps_propinfo_tbl[22].prop_cur_uval
#define	sctps_rto_ming_high		sctps_propinfo_tbl[22].prop_max_uval
#define	sctps_rto_ming_low		sctps_propinfo_tbl[22].prop_min_uval
#define	sctps_rto_maxg			sctps_propinfo_tbl[23].prop_cur_uval
#define	sctps_rto_maxg_high		sctps_propinfo_tbl[23].prop_max_uval
#define	sctps_rto_maxg_low		sctps_propinfo_tbl[23].prop_min_uval
#define	sctps_rto_initialg		sctps_propinfo_tbl[24].prop_cur_uval
#define	sctps_rto_initialg_high		sctps_propinfo_tbl[24].prop_max_uval
#define	sctps_rto_initialg_low		sctps_propinfo_tbl[24].prop_min_uval
#define	sctps_cookie_life		sctps_propinfo_tbl[25].prop_cur_uval
#define	sctps_cookie_life_high		sctps_propinfo_tbl[25].prop_max_uval
#define	sctps_cookie_life_low		sctps_propinfo_tbl[25].prop_min_uval
#define	sctps_max_in_streams		sctps_propinfo_tbl[26].prop_cur_uval
#define	sctps_max_in_streams_high	sctps_propinfo_tbl[26].prop_max_uval
#define	sctps_max_in_streams_low	sctps_propinfo_tbl[26].prop_min_uval
#define	sctps_initial_out_streams	sctps_propinfo_tbl[27].prop_cur_uval
#define	sctps_initial_out_streams_high	sctps_propinfo_tbl[27].prop_max_uval
#define	sctps_initial_out_streams_low	sctps_propinfo_tbl[27].prop_min_uval
#define	sctps_shutack_wait_bound	sctps_propinfo_tbl[28].prop_cur_uval
#define	sctps_maxburst			sctps_propinfo_tbl[29].prop_cur_uval
#define	sctps_addip_enabled		sctps_propinfo_tbl[30].prop_cur_bval
#define	sctps_recv_hiwat_minmss		sctps_propinfo_tbl[31].prop_cur_uval
#define	sctps_slow_start_initial	sctps_propinfo_tbl[32].prop_cur_uval
#define	sctps_slow_start_after_idle	sctps_propinfo_tbl[33].prop_cur_uval
#define	sctps_prsctp_enabled		sctps_propinfo_tbl[34].prop_cur_bval
#define	sctps_fast_rxt_thresh		sctps_propinfo_tbl[35].prop_cur_uval
#define	sctps_deferred_acks_max		sctps_propinfo_tbl[36].prop_cur_uval
#define	sctps_wroff_xtra		sctps_propinfo_tbl[37].prop_cur_uval
#define	sctps_naglim_def		sctps_propinfo_tbl[38].prop_cur_uval
#define	sctps_faddr_policy		sctps_propinfo_tbl[39].prop_cur_uval
#define	sctps_faddr_sticky_prim_cnt	sctps_propinfo_tbl[40].prop_cur_uval
#define	sctps_faddr_pref_prim_cnt	sctps_propinfo_tbl[41].prop_cur_uval
#define	sctps_cong_default		sctps_propinfo_tbl[42].prop_cur_sval

/*
 * Retransmission timer start and stop macro for a given faddr.
 */
#define	SCTP_FADDR_TIMER_RESTART(sctp, fp, intvl, now)			\
{									\
	DTRACE_PROBE4(sctp_faddr_timer_restart, sctp_t *, (sctp),	\
	    sctp_faddr_t *, (fp), int, (intvl), int64_t, (now));	\
	if ((fp)->sf_restart_time < (now) || !(fp)->sf_timer_running) {	\
		sctp_timer((sctp), (fp)->sf_timer_mp, (intvl));		\
		(fp)->sf_timer_running = 1;				\
		(fp)->sf_restart_time = (now);				\
	}								\
}

#define	SCTP_FADDR_TIMER_STOP(fp)					\
	DTRACE_PROBE1(sctp_faddr_timer_stop, sctp_faddr_t *, (fp));	\
	ASSERT((fp)->sf_timer_mp != NULL);				\
	if ((fp)->sf_timer_running) {					\
		sctp_timer_stop((fp)->sf_timer_mp);			\
		(fp)->sf_timer_running = 0;				\
	}

/* For per endpoint association statistics */
#define	SCTP_MAX_RTO(sctp, fp) {			\
	/*						\
	 * Record the maximum observed RTO,		\
	 * sctp_maxrto is zeroed elsewhere		\
	 * at the end of each stats request.		\
	 */						\
	(sctp)->sctp_maxrto =				\
	    MAX((sctp)->sctp_maxrto, (fp)->sf_rto);	\
	DTRACE_PROBE2(sctp__maxrto, sctp_t *,		\
	    sctp, struct sctp_faddr_s, fp);		\
}

#define	SCTP_CALC_RXT(sctp, fp, max)	\
{					\
	if (((fp)->sf_rto <<= 1) > (max))	\
		(fp)->sf_rto = (max);	\
	SCTP_MAX_RTO(sctp, fp);		\
}


#define	SCTP_MAX_COMBINED_HEADER_LENGTH	(60 + 12) /* Maxed out ip + sctp */
#define	SCTP_MAX_IP_OPTIONS_LENGTH	(60 - IP_SIMPLE_HDR_LENGTH)
#define	SCTP_MAX_HDR_LENGTH		60

#define	SCTP_SECRET_LEN	16

#define	SCTP_REFHOLD(sctp) {				\
	mutex_enter(&(sctp)->sctp_reflock);		\
	(sctp)->sctp_refcnt++;				\
	DTRACE_PROBE1(sctp_refhold, sctp_t, sctp);	\
	ASSERT((sctp)->sctp_refcnt != 0);		\
	mutex_exit(&(sctp)->sctp_reflock);		\
}

#define	SCTP_REFRELE(sctp) {					\
	mutex_enter(&(sctp)->sctp_reflock);			\
	ASSERT((sctp)->sctp_refcnt != 0);			\
	if (--(sctp)->sctp_refcnt == 0) {			\
		DTRACE_PROBE1(sctp_refrele, sctp_t, sctp);	\
		mutex_exit(&(sctp)->sctp_reflock);		\
		CONN_DEC_REF((sctp)->sctp_connp);		\
	} else {						\
		DTRACE_PROBE1(sctp_refrele, sctp_t, sctp);	\
		mutex_exit(&(sctp)->sctp_reflock);		\
	}							\
}

#define	SCTP_PRINTADDR(a)					\
	ntohl((a).s6_addr32[0]), ntohl((a).s6_addr32[1]),	\
	ntohl((a).s6_addr32[2]), ntohl((a).s6_addr32[3])

#define	CONN2SCTP(conn)	((sctp_t *)(&((conn_t *)conn)[1]))

/*
 * Outbound data, flags and macros for per-message, per-chunk info
 */
typedef struct {
	int64_t		smh_ttl;		/* Time to Live */
	int64_t		smh_tob;		/* Time of Birth */
	uint32_t	smh_context;		/* Context set by app */
	uint16_t	smh_sid;		/* Stream number */
	uint16_t	smh_ssn;		/* Stream sequence number */
	uint32_t	smh_ppid;		/* Payload ID set by app */
	uint32_t	smh_msglen;		/* Msg length */
	mblk_t		*smh_xmit_head;		/* First message mblk_t */
	mblk_t		*smh_xmit_tail;		/* Last message mblk_t */
	mblk_t		*smh_xmit_last_sent;	/* Last msg sent mblk_t */
	uint16_t	smh_flags;		/* Send flags set by app */
} sctp_msg_hdr_t;

#define	SCTP_CHUNK_FLAG_SENT		0x01
#define	SCTP_CHUNK_FLAG_REXMIT		0x02
#define	SCTP_CHUNK_FLAG_ACKED		0x04
#define	SCTP_MSG_SSN_SET		0x08
#define	SCTP_MSG_FLAG_ABANDONED		0x10
#define	SCTP_CHUNK_FLAG_ABANDONED	0x20
#define	SCTP_MSG_COMPLETE		0x40

#define	SCTP_MSG_IS_SSN_SET(mp)		((mp)->b_flag & SCTP_MSG_SSN_SET)
#define	SCTP_SET_MSG_SSN(mp)		((mp)->b_flag |= SCTP_MSG_SSN_SET)
#define	SCTP_MSG_IS_COMPLETE(mp)	((mp)->b_flag & SCTP_MSG_COMPLETE)
#define	SCTP_SET_MSG_COMPLETE(mp)	((mp)->b_flag |= SCTP_MSG_COMPLETE)

#define	SCTP_CHUNK_CLEAR_FLAGS(mp) ((mp)->b_flag = 0)
/*
 * If we are transmitting the chunk for the first time we assign the TSN and
 * SSN here. The reason we assign the SSN here (as opposed to doing it in
 * sctp_chunkify()) is that the chunk may expire, if PRSCTP is enabled, before
 * we get a chance to send it out. If we assign the SSN in sctp_chunkify()
 * and this happens, then we need to send a Forward TSN to the peer, which
 * will be expecting this SSN, assuming ordered. If we assign it here we
 * can just take out the chunk from the transmit list without having to
 * send a Forward TSN chunk. While assigning the SSN we use (meta)->b_cont
 * to determine if it needs a new SSN (i.e. the next SSN for the stream),
 * since (meta)->b_cont signifies the first chunk of a message (if the message
 * is unordered, then the SSN is 0).
 *
 */
#define	SCTP_CHUNK_SENT(sctp, mp, sdc, fp, data_len, meta) {		\
	if (!SCTP_CHUNK_ISSENT(mp)) {					\
		sctp_msg_hdr_t	*mhdr = (sctp_msg_hdr_t *)(meta)->b_rptr; \
		ASSERT(!SCTP_CHUNK_ABANDONED(mp));			\
		(mp)->b_flag = SCTP_CHUNK_FLAG_SENT;			\
		(sdc)->sdh_tsn = htonl((sctp)->sctp_ltsn++);		\
		if ((mhdr)->smh_flags & MSG_UNORDERED) {		\
			(sdc)->sdh_ssn = 0;				\
			SCTP_DATA_SET_UBIT(sdc);			\
			BUMP_LOCAL((sctp)->sctp_oudchunks);		\
		} else {						\
			BUMP_LOCAL((sctp)->sctp_odchunks);		\
			if (!SCTP_MSG_IS_SSN_SET(meta)) {		\
				mhdr->smh_ssn = htons(			\
				    (sctp)->sctp_ostrcntrs[mhdr->smh_sid]++); \
				SCTP_SET_MSG_SSN(meta);			\
			}						\
			(sdc)->sdh_ssn = mhdr->smh_ssn;			\
		}							\
		DTRACE_PROBE4(sctp__chunk__sent1, sctp_t *, sctp,	\
		    mblk_t *, mp, mblk_t *, meta, int, data_len);	\
		(sctp)->sctp_unacked += (data_len);			\
		ASSERT((sctp)->sctp_unsent >= (data_len));		\
		(sctp)->sctp_unsent -= (data_len);			\
		ASSERT((sctp)->sctp_frwnd >= (data_len));		\
		(sctp)->sctp_frwnd -= (data_len);			\
	} else {							\
		if (SCTP_CHUNK_ISACKED(mp)) {				\
			(sctp)->sctp_unacked += (data_len);		\
		} else {						\
			ASSERT(SCTP_CHUNK_DEST(mp)->sf_suna >= (data_len)); \
			SCTP_CHUNK_DEST(mp)->sf_suna -= (data_len);	\
		}							\
		DTRACE_PROBE4(sctp__chunk__sent2, sctp_t *, sctp,	\
		    mblk_t *, mp, mblk_t *, meta, int, data_len);	\
		(mp)->b_flag &= ~(SCTP_CHUNK_FLAG_REXMIT |		\
			SCTP_CHUNK_FLAG_ACKED);				\
		SCTP_CHUNK_SET_SACKCNT(mp, 0);				\
		BUMP_LOCAL(sctp->sctp_rxtchunks);			\
		BUMP_LOCAL((sctp)->sctp_T3expire);			\
		BUMP_LOCAL((fp)->sf_T3expire);				\
	}								\
	SCTP_SET_CHUNK_DEST(mp, fp);					\
	(fp)->sf_suna += (data_len);					\
}

#define	SCTP_CHUNK_ISSENT(mp)	((mp)->b_flag & SCTP_CHUNK_FLAG_SENT)
#define	SCTP_CHUNK_CANSEND(mp)	\
	(!(SCTP_CHUNK_ABANDONED(mp)) &&	\
	(((mp)->b_flag & (SCTP_CHUNK_FLAG_REXMIT|SCTP_CHUNK_FLAG_SENT)) != \
	SCTP_CHUNK_FLAG_SENT))

#define	SCTP_CHUNK_DEST(mp)		((sctp_faddr_t *)(mp)->b_queue)
#define	SCTP_SET_CHUNK_DEST(mp, fp)	((mp)->b_queue = (queue_t *)fp)

#define	SCTP_CHUNK_REXMIT(sctp, mp) {					\
	DTRACE_PROBE2(sctp__chunk__rexmit, sctp_t *, sctp, mblk_t *,	\
	    mp);							\
	(mp)->b_flag |= SCTP_CHUNK_FLAG_REXMIT; 			\
}
#define	SCTP_CHUNK_CLEAR_REXMIT(mp) ((mp)->b_flag &= ~SCTP_CHUNK_FLAG_REXMIT)
#define	SCTP_CHUNK_WANT_REXMIT(mp) ((mp)->b_flag & SCTP_CHUNK_FLAG_REXMIT)

#define	SCTP_CHUNK_ACKED(mp) \
	((mp)->b_flag = (SCTP_CHUNK_FLAG_SENT|SCTP_CHUNK_FLAG_ACKED))
#define	SCTP_CHUNK_ISACKED(mp)	((mp)->b_flag & SCTP_CHUNK_FLAG_ACKED)
#define	SCTP_CHUNK_CLEAR_ACKED(sctp, mp) {				\
	DTRACE_PROBE2(sctp__chunk__clracked, sctp_t *, sctp, mblk_t *,	\
	    mp);							\
	(mp)->b_flag &= ~SCTP_CHUNK_FLAG_ACKED;				\
}

#define	SCTP_CHUNK_SACKCNT(mp)	((intptr_t)((mp)->b_prev))
#define	SCTP_CHUNK_SET_SACKCNT(mp, val) ((mp)->b_prev = \
					(mblk_t *)(uintptr_t)(val))


/* For PR-SCTP */
#define	SCTP_ABANDON_CHUNK(mp)	((mp)->b_flag |= SCTP_CHUNK_FLAG_ABANDONED)
#define	SCTP_CHUNK_ABANDONED(mp) \
	((mp)->b_flag & SCTP_CHUNK_FLAG_ABANDONED)

#define	SCTP_MSG_SET_ABANDONED(mp)	\
	((mp)->b_flag |= SCTP_MSG_FLAG_ABANDONED)
#define	SCTP_MSG_CLEAR_ABANDONED(mp)((mp)->b_flag &= ~SCTP_MSG_FLAG_ABANDONED)
#define	SCTP_IS_MSG_ABANDONED(mp)	((mp)->b_flag & SCTP_MSG_FLAG_ABANDONED)

/*
 * Check if a message has expired.  A message is expired if
 *	1. It has a non-zero time to live value.
 *	2. The difference between current time and sent time is bigger than
 *	   the time to live value.
 *	3a. The message has not been sent; or
 *	3b. It is sent using PRSCTP and it has not been SACK'ed before
 *	    its lifetime expires.
 */
#define	SCTP_MSG_TO_BE_ABANDONED(mhdr, sctp, now)			\
	(((mhdr)->smh_ttl > 0) &&					\
	(((now) - (mhdr)->smh_tob) > (mhdr)->smh_ttl) &&		\
	(!SCTP_CHUNK_ISSENT((mhdr)->smh_xmit_head) ||			\
	((sctp)->sctp_prsctp_aware && ((mhdr)->smh_flags & MSG_PR_SCTP))))

#define	SCTP_MSG_HEAD(meta) ((sctp_msg_hdr_t *)(meta)->b_rptr)->smh_xmit_head

/* SCTP association hash function. */
#define	SCTP_CONN_HASH(sctps, ports)			\
	((((ports) ^ ((ports) >> 16)) * 31) & 		\
	    ((sctps)->sctps_conn_hash_size - 1))

/*
 * Linked list struct to store SCTP listener association limit configuration
 * per IP stack.  The list is stored at sctps_listener_conf in sctp_stack_t.
 *
 * sl_port: the listener port of this limit configuration
 * sl_ratio: the maximum amount of memory consumed by all concurrent SCTP
 *           connections created by a listener does not exceed 1/tl_ratio
 *           of the total system memory.  Note that this is only an
 *           approximation.
 * sl_link: linked list struct
 */
typedef struct sctp_listener_s {
	in_port_t	sl_port;
	uint32_t	sl_ratio;
	list_node_t	sl_link;
} sctp_listener_t;

/*
 * If there is a limit set on the number of association allowed per each
 * listener, the following struct is used to store that counter.  It keeps
 * the number of SCTP association created by a listener.  Note that this needs
 * to be separated from the listener since the listener can go away before
 * all the associations are gone.
 *
 * When the struct is allocated, slc_cnt is set to 1.  When a new association
 * is created by the listener, slc_cnt is incremented by 1.  When an
 * association created by the listener goes away, slc_count is decremented by
 * 1.  When the listener itself goes away, slc_cnt is decremented  by one.
 * The last association (or the listener) which decrements slc_cnt to zero
 * frees the struct.
 *
 * slc_max is the maximum number of concurrent associations created from a
 * listener.  It is calculated when the sctp_listen_cnt_t is allocated.
 *
 * slc_report_time stores the time when cmn_err() is called to report that the
 * max has been exceeeded.  Report is done at most once every
 * SCTP_SLC_REPORT_INTERVAL mins for a listener.
 *
 * slc_drop stores the number of connection attempt dropped because the
 * limit has reached.
 */
typedef struct sctp_listen_cnt_s {
	uint64_t	slc_max;
	uint64_t	slc_cnt;
	int64_t		slc_report_time;
	uint64_t	slc_drop;
} sctp_listen_cnt_t;

#define	SCTP_SLC_REPORT_INTERVAL	(30 * MINUTES)

#define	SCTP_DECR_LISTEN_CNT(sctp)					\
{									\
	ASSERT((sctp)->sctp_listen_cnt->slc_cnt > 0);			\
	if (atomic_add_64_nv(&(sctp)->sctp_listen_cnt->slc_cnt, -1) == 0) \
		kmem_free((sctp)->sctp_listen_cnt, sizeof (sctp_listen_cnt_t));\
	(sctp)->sctp_listen_cnt = NULL;					\
}

/* Increment and decrement the number of associations in sctp_stack_t. */
#define	SCTPS_ASSOC_INC(sctps)						\
	atomic_inc_64(							\
	    (uint64_t *)&(sctps)->sctps_sc[CPU->cpu_seqid]->sctp_sc_assoc_cnt)

#define	SCTPS_ASSOC_DEC(sctps)						\
	atomic_dec_64(							\
	    (uint64_t *)&(sctps)->sctps_sc[CPU->cpu_seqid]->sctp_sc_assoc_cnt)

#define	SCTP_ASSOC_EST(sctps, sctp)					\
{									\
	(sctp)->sctp_state = SCTPS_ESTABLISHED;				\
	(sctp)->sctp_assoc_start_time = (uint32_t)LBOLT_FASTPATH64;	\
	SCTPS_ASSOC_INC(sctps);						\
}

/*
 * Bind hash array size and hash function.  The size must be a power
 * of 2 and lport must be in host byte order.
 */
#define	SCTP_BIND_FANOUT_SIZE	2048
#define	SCTP_BIND_HASH(lport)	(((lport) * 31) & (SCTP_BIND_FANOUT_SIZE - 1))

/* options that SCTP negotiates during association establishment */
#define	SCTP_PRSCTP_OPTION	0x01
#define	SCTP_AL_OPTION		0x02

/*
 * Listener hash array size and hash function.  The size must be a power
 * of 2 and lport must be in host byte order.
 */
#define	SCTP_LISTEN_FANOUT_SIZE	512
#define	SCTP_LISTEN_HASH(lport) (((lport) * 31) & (SCTP_LISTEN_FANOUT_SIZE - 1))

typedef struct sctp_tf_s {
	struct sctp_s	*tf_sctp;
	kmutex_t	tf_lock;
} sctp_tf_t;

/* Round up the value to the nearest mss. */
#define	MSS_ROUNDUP(value, mss)		((((value) - 1) / (mss) + 1) * (mss))

extern sin_t	sctp_sin_null;	/* Zero address for quick clears */
extern sin6_t	sctp_sin6_null;	/* Zero address for quick clears */

#define	SCTP_IS_DETACHED(sctp)		((sctp)->sctp_detached)

/* Data structure used to track received TSNs */
typedef struct sctp_set_s {
	struct sctp_set_s *next;
	struct sctp_set_s *prev;
	uint32_t begin;
	uint32_t end;
} sctp_set_t;

/* Data structure used to track TSNs for PR-SCTP */
typedef struct sctp_ftsn_set_s {
	struct sctp_ftsn_set_s *next;
	ftsn_entry_t	ftsn_entries;
} sctp_ftsn_set_t;

/* Data structure used to track incoming SCTP streams */
typedef struct sctp_instr_s {
	uint16_t	istr_sid;
	mblk_t		*istr_msgs;
	int		istr_nmsgs;
	uint16_t	nextseq;
	struct sctp_s	*sctp;
	mblk_t		*istr_reass;
} sctp_instr_t;

/* Reassembly data structure (per-stream) */
typedef struct sctp_reass_s {
	uint16_t	sr_ssn;
	uint16_t	sr_needed;
	uint16_t	sr_got;
	uint16_t	sr_msglen;	/* len of consecutive fragments */
					/* from the begining (B-bit) */
	mblk_t		*sr_tail;
	boolean_t	sr_hasBchunk;	/* If the fragment list begins with */
					/* a B-bit set chunk */
	uint32_t	sr_nexttsn;	/* TSN of the next fragment we */
					/* are expecting */
	uint32_t	sr_oldesttsn;	/* Oldest known TSN of this fragment */
	boolean_t	sr_partial_delivered;
} sctp_reass_t;

/* debugging */
#undef	dprint
#ifdef DEBUG
extern int sctpdebug;
#define	dprint(level, args)	{ if (sctpdebug > (level)) printf args; }
#else
#define	dprint(level, args) {}
#endif


/* Peer address tracking */

/*
 * States for peer addresses
 *
 * SCTP_FADDRS_UNCONFIRMED: we have not communicated with this peer address
 *     before, mark it as unconfirmed so that we will not send data to it.
 *     All addresses initially are in unconfirmed state and required
 *     validation.  SCTP sends a heartbeat to each of them and when it gets
 *     back a heartbeat ACK, the address will be marked as alive.  This
 *     validation fixes a security issue with multihoming.  If an attacker
 *     establishes an association with us and tells us that it has addresses
 *     belonging to another host A, this will prevent A from communicating
 *     with us.  This is fixed by peer address validation.  In the above case,
 *     A will respond with an abort.
 *
 * SCTP_FADDRS_ALIVE: this peer address is alive and we can communicate with
 *     it with no problem.
 *
 * SCTP_FADDRS_DOWN: we have exceeded the retransmission limit to this
 *     peer address.  Once an address is marked down, we will only send
 *     a heartbeat to it every hb_interval in case it becomes alive now.
 *
 * SCTP_FADDRS_UNREACH: there is no suitable source address to send to
 *     this peer address.  For example, the peer address is v6 but we only
 *     have v4 addresses.  It is marked unreachable until there is an
 *     address configuration change.  At that time, mark these addresses
 *     as unconfirmed and try again to see if those unreachable addresses
 *     are OK as we may have more source addresses.
 */
typedef enum {
	SCTP_FADDRS_UNREACH,
	SCTP_FADDRS_DOWN,
	SCTP_FADDRS_ALIVE,
	SCTP_FADDRS_UNCONFIRMED
} sctp_faddr_state_t;

typedef struct sctp_faddr_s {
	struct sctp_faddr_s *sf_next;
	sctp_faddr_state_t	sf_state;

	in6_addr_t	sf_faddr;
	in6_addr_t	sf_saddr;

	int64_t		sf_hb_expiry;	/* time to retransmit heartbeat */
	uint32_t	sf_hb_interval;	/* the heartbeat interval */

	int		sf_rto;		/* RTO in tick */
	clock_t		sf_srtt;	/* Smoothed RTT in ms */
	clock_t		sf_rttvar;	/* RTT variance in ms */
	uint32_t	sf_rtt_updates;
	int		sf_strikes;
	int		sf_max_retr;
	uint32_t	sf_pmss;
	uint32_t	sf_cwnd;
	uint32_t	sf_ssthresh;
	uint32_t	sf_suna;	/* sent - unack'ed */
	int32_t		sf_acked;
	int64_t		sf_lastactive;
	mblk_t		*sf_timer_mp;	/* retransmission timer control */
	int64_t		sf_restart_time; /* when timer is restarted */
	uint32_t
			sf_hb_pending : 1,
			sf_timer_running : 1,
			sf_df : 1,
			sf_pmtu_discovered : 1,

			sf_rc_timer_running : 1,
			sf_isv4 : 1,
			sf_hb_enabled : 1,
			sf_connectx : 1;

	mblk_t		*sf_rc_timer_mp; /* reliable control chunk timer */
	ip_xmit_attr_t	*sf_ixa;	/* Transmit attributes */
	uint32_t	sf_T3expire;	/* # of times T3 timer expired */

	uint64_t	sf_hb_secret;	/* per addr "secret" in heartbeat */
	uint32_t	sf_rxt_unacked;	/* # unack'ed retransmitted bytes */
	uint32_t	sf_naglim;	/* Nagle Algorithm threshold */
	uint32_t	sf_last_sent_len; /* Last sent packet length */
	int32_t		sf_chk_cnt;	/* faddr policy check count */
	uint32_t	sf_v6_flowlabel; /* IPv6 header flow label */
	uint8_t		sf_v4_tos;	/* IPv4 header TOS */

	/* pluggable congestion control */
	tcpcong_handle_t sf_cong_hdl;	/* current module handle */
	tcpcong_ops_t	*sf_cong_ops;	/* current algorithm ops */
	tcpcong_args_t	sf_cong_args;	/* args passed to the alg */
	/* preallocated algorithm state buffer */
	uint64_t	sf_cong_state_buf[TCPCONG_MAX_STATE_SIZE];
	int32_t		sf_cwnd_cnt;	/* cwnd cnt in congestion avoidance */
	uint16_t	sf_dupack_cnt;	/* # of consequtive duplicate acks */
} sctp_faddr_t;

/* Flags to indicate supported address type in the PARM_SUP_ADDRS. */
#define	PARM_SUPP_V6	0x1
#define	PARM_SUPP_V4	0x2

/*
 * Set heartbeat interval plus jitter.  The jitter is supposed to be random,
 * up to +/- 50% of the RTO.  We use gethrtime() here for  performance reason
 * as the jitter does not really need to be "very" random.
 */
#define	SET_HB_INTVL(fp)					\
	((fp)->sf_hb_interval + (fp)->sf_rto + ((fp)->sf_rto >> 1) -	\
	(uint_t)gethrtime() % (fp)->sf_rto)

#define	SCTP_IPIF_HASH	16

typedef	struct	sctp_ipif_hash_s {
	list_t		sctp_ipif_list;
	int		ipif_count;
	krwlock_t	ipif_hash_lock;
} sctp_ipif_hash_t;


/*
 * Initialize cwnd according to RFC 3390.  def_max_init_cwnd is
 * either sctp_slow_start_initial or sctp_slow_start_after idle
 * depending on the caller.
 */
#define	SET_CWND(fp, mss, def_max_init_cwnd)				\
{									\
	(fp)->sf_cwnd = MIN(def_max_init_cwnd * (mss),			\
	    MIN(4 * (mss), MAX(2 * (mss), 4380 / (mss) * (mss))));	\
}

/*
 * Policy, controlled by the stack parameter sctps_faddr_policy, on how to use
 * peer addresses.
 *
 * SCTP_FADDR_POLICY_ROTATE: (default policy) Whenever a retransmission timer
 *   fires, SCTP will try to find an alternate address to do the retransmission
 *   and set sctp_current to this address.  This means that future data
 *   transmission will also be sent to this address.  In this policy,
 *   sctp_primary does not have special meaning except that it is used to send
 *   data initially.  If the socket end point uses SCTP_PRIMARY_ADDR to set
 *   the primary address, SCTP will also immediately switch back to use it
 *   if it is not sctp_current.
 *
 * SCTP_FADDR_POLICY_STICKY_PRIMARY: SCTP will always send to the primary
 *   address until it is not alive.  It means that retransmission will also
 *   be sent to the primary address until after sctp_pp_max_retr times.  After
 *   that, SCTP will mark the primary as dead and SCTP will switch to use an
 *   alternate address.  But when SCTP detects that the primary becomes alive
 *   again (getting consecutive sctp_faddr_sticky_prim_cnt HEARTBEAT-ACK
 *   replies from the primary address), it will switch back to use the primary
 *   immediately.
 *
 * SCTP_FADDR_POLICY_PREF_PRIMARY: When a timeout happens, SCTP will try to
 *   find an alternate address to do the retransmission.  And future data will
 *   also be sent to that alternate address.  After SCTP detects that the
 *   primary address is "well enough" again (getting consecutive
 *   sctp_faddr_pref_prim_cnt HEARBTBEAT-ACK replies from the primary address)
 *   SCTP will switch back to use the primary.
 *
 * An association's policy is set when the sctp_t is created.  Changes
 * of the sctps_faddr_policy after that does not affect the association's
 * policy.
 *
 * Note: When a new policy is added, add it at the end of enum and update the
 *       range of private parameter "_faddr_policy" in sctp_tunables.c.
 */
typedef enum {
	SCTP_FADDR_POLICY_ROTATE = 0,
	SCTP_FADDR_POLICY_STICKY_PRIMARY,
	SCTP_FADDR_POLICY_PREF_PRIMARY
} sctp_faddr_policy_t;

/* Congestion control ops */
#define	SCTP_CONG_STATE_SIZE(fp)				\
	(fp)->sf_cong_ops->co_state_size(TCPCONG_PROTO_SCTP)
#define	SCTP_CONG_STATE_INIT(fp)				\
	(fp)->sf_cong_ops->co_state_init(&(fp)->sf_cong_args)
#define	SCTP_CONG_STATE_FINI(fp)				\
	(fp)->sf_cong_ops->co_state_fini(&(fp)->sf_cong_args)
#define	SCTP_CONG_ACK(fp)					\
	(fp)->sf_cong_ops->co_ack(&(fp)->sf_cong_args);
#define	SCTP_CONG_LOSS(fp)					\
	(fp)->sf_cong_ops->co_loss(&(fp)->sf_cong_args);
#define	SCTP_CONG_RTO(fp)					\
	(fp)->sf_cong_ops->co_rto(&(fp)->sf_cong_args);
#define	SCTP_CONG_CONGESTION(fp)				\
	(fp)->sf_cong_ops->co_congestion(&(fp)->sf_cong_args);
#define	SCTP_CONG_AFTER_IDLE(fp)				\
	(fp)->sf_cong_ops->co_after_idle(&(fp)->sf_cong_args)

/* Struct to hold additional peer address for setting up an association. */
typedef struct {
	in6_addr_t	sconnf_addr;
	uint_t		sconnf_scope_id;
} sctp_conn_faddr_t;

typedef struct {
	int		sconnf_addr_cnt;
	int		sconnf_cur_addr;
	/* Array of sctp_conn_faddr_t follows here. */
} sctp_conn_faddrs_t;

struct sctp_s;

/*
 * Control structure for each open SCTP stream,
 * defined only within the kernel or for a kmem user.
 * NOTE: sctp_reinit_values MUST have a line for each field in this structure!
 */
#if (defined(_KERNEL) || defined(_KMEMUSER))

typedef struct sctp_s {

	/*
	 * The following is shared with (and duplicated) in IP, so if you
	 * make changes, make sure you also change things in ip_sctp.c.
	 */
	struct sctp_s	*sctp_conn_hash_next;
	struct sctp_s	*sctp_conn_hash_prev;

	struct sctp_s	*sctp_listen_hash_next;
	struct sctp_s	*sctp_listen_hash_prev;

	sctp_tf_t	*sctp_listen_tfp;	/* Ptr to tf */
	sctp_tf_t	*sctp_conn_tfp;		/* Ptr to tf */

	/* Global list of sctp */
	list_node_t	sctp_list;

	sctp_faddr_t		*sctp_faddrs;
	sctp_conn_faddrs_t	*sctp_conn_faddrs;
	int			sctp_nfaddrs;
	sctp_ipif_hash_t	sctp_saddrs[SCTP_IPIF_HASH];
	int			sctp_nsaddrs;

	kmutex_t	sctp_lock;
	kcondvar_t	sctp_cv;
	boolean_t	sctp_running;

#define	sctp_ulpd	sctp_connp->conn_upper_handle
#define	sctp_upcalls	sctp_connp->conn_upcalls

#define	sctp_ulp_newconn	sctp_upcalls->su_newconn
#define	sctp_ulp_connected	sctp_upcalls->su_connected
#define	sctp_ulp_disconnected	sctp_upcalls->su_disconnected
#define	sctp_ulp_opctl		sctp_upcalls->su_opctl
#define	sctp_ulp_recv		sctp_upcalls->su_recv
#define	sctp_ulp_txq_full	sctp_upcalls->su_txq_full
#define	sctp_ulp_prop		sctp_upcalls->su_set_proto_props

	sctp_state_t	sctp_state;

	conn_t		*sctp_connp;		/* conn_t stuff */
	sctp_stack_t	*sctp_sctps;

	/* Peer address tracking */
	sctp_faddr_t	*sctp_lastfaddr;	/* last faddr in list */
	sctp_faddr_t	*sctp_primary;		/* primary faddr */
	sctp_faddr_t	*sctp_current;		/* current faddr */
	sctp_faddr_t	*sctp_lastdata;		/* last data seen from this */
	sctp_faddr_policy_t	sctp_faddr_policy; /* faddr use policy */
	int		sctp_faddr_chk_cnt;	/* faddr policy check count */

	/* Outbound data tracking */
	mblk_t		*sctp_xmit_head;
	mblk_t		*sctp_xmit_tail;
	mblk_t		*sctp_xmit_last_sent;
	mblk_t		*sctp_xmit_unacked;

	uint32_t	sctp_unacked;		/* # of unacked bytes */
	int32_t		sctp_unsent;		/* # of unsent bytes in hand */

	uint32_t	sctp_ltsn;		/* Local instance TSN */
	uint32_t	sctp_lastack_rxd;	/* Last rx'd cumtsn */
	uint32_t	sctp_recovery_tsn;	/* Exit from fast recovery */
	uint32_t	sctp_adv_pap;		/* Adv. Peer Ack Point */
	uint32_t	sctp_max_burst;		/* Max burst of pkts */

	uint16_t	sctp_num_ostr;
	uint16_t	*sctp_ostrcntrs;

	mblk_t		*sctp_pad_mp;		/* pad unaligned data chunks */

	/* sendmsg() default parameters */
	uint16_t	sctp_def_stream;	/* default stream id */
	uint16_t	sctp_def_flags;		/* default xmit flags */
	uint32_t	sctp_def_ppid;		/* default payload id */
	uint32_t	sctp_def_context;	/* default context */
	uint32_t	sctp_def_timetolive;	/* default msg TTL in tick */

	/* Inbound data tracking */
	sctp_set_t	*sctp_sack_info;	/* Sack tracking */
	mblk_t		*sctp_ack_mp;		/* Delayed ACK timer block */
	sctp_instr_t	*sctp_instr;		/* Instream trackers */
	mblk_t		*sctp_uo_frags;		/* Un-ordered msg. fragments */
	uint32_t	sctp_ftsn;		/* Peer's TSN */
	uint32_t	sctp_lastacked;		/* last cumtsn SACKd */
	uint16_t	sctp_num_istr;		/* No. of instreams */
	int32_t		sctp_istr_nmsgs;	/* No. of chunks in instreams */
	mblk_t		*sctp_rx_ready;		/* Msg queued because of PD */
	mblk_t		*sctp_rx_ready_tail;	/* Rx ready list tail */

	int32_t		sctp_sack_gaps;		/* No. of received gaps */
	int32_t		sctp_sack_toggle;	/* Pkts rcvd without SACK'ed */
	uint32_t	sctp_sack_max;		/* Max rcvd pkts before SACK */
	uint32_t	sctp_sack_interval;	/* Delayed SACK intrvl (tick) */

	/* RTT calculation */
	uint32_t	sctp_rtt_tsn;
	int64_t		sctp_out_time;

	/* Stats can be reset by snmp users kstat, netstat and snmp agents */
	uint64_t	sctp_opkts;		/* sent pkts */
	uint64_t	sctp_obchunks;		/* sent control chunks */
	uint64_t	sctp_odchunks;		/* sent ordered data chunks */
	uint64_t	sctp_oudchunks;		/* sent unord data chunks */
	uint64_t	sctp_rxtchunks;		/* retransmitted chunks */
	uint64_t	sctp_ipkts;		/* recv pkts */
	uint64_t	sctp_ibchunks;		/* recv control chunks */
	uint64_t	sctp_idchunks;		/* recv ordered data chunks */
	uint64_t	sctp_iudchunks;		/* recv unord data chunks */
	uint64_t	sctp_fragdmsgs;
	uint64_t	sctp_reassmsgs;
	uint32_t	sctp_T1expire;		/* # of times T1timer expired */
	uint32_t	sctp_T2expire;		/* # of times T2timer expired */
	uint32_t	sctp_T3expire;		/* # of times T3timer expired */
	uint32_t	sctp_assoc_start_time;	/* time when assoc was est. */

	uint32_t	sctp_frwnd;		/* Peer RWND */
	uint32_t	sctp_cwnd_max;

	/* Inbound flow control */
	int32_t		sctp_rwnd;		/* Current receive window */
	int32_t		sctp_arwnd;		/* Last advertised window */
	int32_t		sctp_rxqueued;		/* No. of bytes in RX q's */
	int32_t		sctp_ulp_rxqueued;	/* Data in ULP */

	/* Pre-initialized composite headers */
	uchar_t		*sctp_iphc;	/* v4 sctp/ip hdr template buffer */
	uchar_t		*sctp_iphc6;	/* v6 sctp/ip hdr template buffer */

	int32_t		sctp_iphc_len;	/* actual allocated v4 buffer size */
	int32_t		sctp_iphc6_len;	/* actual allocated v6 buffer size */

	int32_t		sctp_hdr_len;	/* len of combined SCTP/IP v4 hdr */
	int32_t		sctp_hdr6_len;	/* len of combined SCTP/IP v6 hdr */

	ipha_t		*sctp_ipha;	/* IPv4 header in the buffer */
	ip6_t		*sctp_ip6h;	/* IPv6 header in the buffer */

	int32_t		sctp_ip_hdr_len; /* Byte len of our current v4 hdr */
	int32_t		sctp_ip_hdr6_len; /* Byte len of our current v6 hdr */

	sctp_hdr_t	*sctp_sctph;	/* sctp header in combined v4 hdr */
	sctp_hdr_t	*sctp_sctph6;	/* sctp header in combined v6 hdr */

	uint32_t	sctp_lvtag;	/* local SCTP instance verf tag */
	uint32_t	sctp_fvtag;	/* Peer's SCTP verf tag */

	/* Path MTU Discovery */
	int64_t		sctp_last_mtu_probe;
	clock_t		sctp_mtu_probe_intvl;
	uint32_t	sctp_mss;	/* min of all faddrs' max send size */

	/* structs sctp_bits, sctp_events are for clearing all bits at once */
	struct {
		uint32_t

		sctp_understands_asconf : 1, /* Peer handles ASCONF chunks */
		sctp_cchunk_pend : 1,	/* Control chunk in flight. */
		sctp_lingering : 1,	/* Lingering in close */
		sctp_loopback: 1,	/* src and dst are the same machine */

		sctp_force_sack : 1,
		sctp_ack_timer_running: 1,	/* Delayed ACK timer running */
		sctp_hwcksum : 1,	/* The NIC is capable of hwcksum */
		sctp_understands_addip : 1,

		sctp_bound_to_all : 1,
		sctp_cansleep : 1,	/* itf routines can sleep */
		sctp_detached : 1,	/* If we're detached from a stream */
		sctp_send_adaptation : 1,	/* send adaptation layer ind */

		sctp_recv_adaptation : 1,	/* recv adaptation layer ind */
		sctp_ndelay : 1,	/* turn off Nagle */
		sctp_condemned : 1,	/* this sctp is about to disappear */
		sctp_chk_fast_rexmit : 1, /* check for fast rexmit message */

		sctp_prsctp_aware : 1,	/* is peer PR-SCTP aware? */
		sctp_linklocal : 1,	/* is linklocal assoc. */
		sctp_rexmitting : 1,	/* SCTP is retransmitting */
		sctp_zero_win_probe : 1,	/* doing zero win probe */

		sctp_txq_full : 1,	/* the tx queue is full */
		sctp_ulp_discon_done : 1,	/* ulp_disconnecting done */
		sctp_flowctrld : 1,	/* upper layer flow controlled */
		sctp_recvrcvinfo : 1,	/* want sctp_rcvinfo data  */

		sctp_recvnxtinfo : 1,	/* want sctp_nxtinfo data */
		sctp_explicit_eor : 1,	/* SCTP_EXPLICIT_EOR option set */
		sctp_eor_on : 1,	/* SCTP_EOR needed to end a msg */
		sctp_frag_interleave : 1,	/* allow frag interleave */

		sctp_pd_on : 1,		/* partial delivery going on */
		sctp_dummy : 3;
	} sctp_bits;
	struct {
		uint32_t

		sctp_recvsndrcvinfo : 1,
		sctp_recvassocevnt : 1,
		sctp_recvpathevnt : 1,
		sctp_recvsendfailevnt : 1,

		sctp_recvpeererr : 1,
		sctp_recvshutdownevnt : 1,
		sctp_recvpdevnt : 1,
		sctp_recvalevnt : 1,

		sctp_recvsender_dry : 1,
		sctp_recvstopped : 1,
		sctp_recvsendfail_event : 1,
		sctp_dummy : 21;
	} sctp_events;
#define	sctp_priv_stream sctp_bits.sctp_priv_stream
#define	sctp_understands_asconf sctp_bits.sctp_understands_asconf
#define	sctp_cchunk_pend sctp_bits.sctp_cchunk_pend
#define	sctp_lingering sctp_bits.sctp_lingering
#define	sctp_loopback sctp_bits.sctp_loopback
#define	sctp_force_sack sctp_bits.sctp_force_sack
#define	sctp_ack_timer_running sctp_bits.sctp_ack_timer_running
#define	sctp_hwcksum sctp_bits.sctp_hwcksum
#define	sctp_understands_addip sctp_bits.sctp_understands_addip
#define	sctp_bound_to_all sctp_bits.sctp_bound_to_all
#define	sctp_cansleep sctp_bits.sctp_cansleep
#define	sctp_detached sctp_bits.sctp_detached
#define	sctp_send_adaptation sctp_bits.sctp_send_adaptation
#define	sctp_recv_adaptation sctp_bits.sctp_recv_adaptation
#define	sctp_ndelay sctp_bits.sctp_ndelay
#define	sctp_condemned sctp_bits.sctp_condemned
#define	sctp_chk_fast_rexmit sctp_bits.sctp_chk_fast_rexmit
#define	sctp_prsctp_aware sctp_bits.sctp_prsctp_aware
#define	sctp_linklocal sctp_bits.sctp_linklocal
#define	sctp_rexmitting sctp_bits.sctp_rexmitting
#define	sctp_zero_win_probe sctp_bits.sctp_zero_win_probe
#define	sctp_txq_full sctp_bits.sctp_txq_full
#define	sctp_ulp_discon_done sctp_bits.sctp_ulp_discon_done
#define	sctp_flowctrld sctp_bits.sctp_flowctrld
#define	sctp_recvrcvinfo sctp_bits.sctp_recvrcvinfo
#define	sctp_recvnxtinfo sctp_bits.sctp_recvnxtinfo
#define	sctp_explicit_eor sctp_bits.sctp_explicit_eor
#define	sctp_eor_on sctp_bits.sctp_eor_on
#define	sctp_frag_interleave sctp_bits.sctp_frag_interleave
#define	sctp_pd_on sctp_bits.sctp_pd_on

#define	sctp_recvsndrcvinfo sctp_events.sctp_recvsndrcvinfo
#define	sctp_recvassocevnt sctp_events.sctp_recvassocevnt
#define	sctp_recvpathevnt sctp_events.sctp_recvpathevnt
#define	sctp_recvsendfailevnt sctp_events.sctp_recvsendfailevnt
#define	sctp_recvpeererr sctp_events.sctp_recvpeererr
#define	sctp_recvshutdownevnt sctp_events.sctp_recvshutdownevnt
#define	sctp_recvpdevnt sctp_events.sctp_recvpdevnt
#define	sctp_recvalevnt sctp_events.sctp_recvalevnt
#define	sctp_recvsender_dry sctp_events.sctp_recvsender_dry
#define	sctp_recvstopped sctp_events.sctp_recvstopped
#define	sctp_recvsendfail_event sctp_events.sctp_recvsendfail_event

	/* Retransmit info */
	mblk_t		*sctp_cookie_mp; /* cookie chunk, if rxt needed */
	int32_t		sctp_strikes;	/* Total number of assoc strikes */
	int32_t		sctp_max_init_rxt;
	int32_t		sctp_pa_max_rxt; /* Max per-assoc retransmit cnt */
	int32_t		sctp_pp_max_rxt; /* Max per-path retransmit cnt */
	uint32_t	sctp_rto_max;
	uint32_t	sctp_rto_max_init;
	uint32_t	sctp_rto_min;
	uint32_t	sctp_rto_initial;

	int64_t		sctp_last_secret_update;
	uint8_t		sctp_secret[SCTP_SECRET_LEN]; /* for cookie auth */
	uint8_t		sctp_old_secret[SCTP_SECRET_LEN];
	uint32_t	sctp_cookie_lifetime;	/* cookie lifetime in tick */

	/* Bind hash tables */
	kmutex_t	*sctp_bind_lockp;	/* Ptr to tf_lock */
	struct sctp_s	*sctp_bind_hash;
	struct sctp_s **sctp_ptpbhn;

	/* Shutdown / cleanup */
	sctp_faddr_t	*sctp_shutdown_faddr;	/* rotate faddr during shutd */
	int32_t		sctp_client_errno;	/* How the client screwed up */
	kmutex_t	sctp_reflock;	/* Protects sctp_refcnt & timer mp */
	ushort_t	sctp_refcnt;	/* No. of pending upstream msg */
	mblk_t		*sctp_timer_mp;	/* List of fired timers. */

	mblk_t		*sctp_heartbeat_mp; /* Timer block for heartbeats */
	uint32_t	sctp_hb_interval; /* Default hb_interval */

	int32_t		sctp_autoclose;	/* Auto disconnect in ticks */
	int64_t		sctp_active;	/* Last time data/sack on this conn */

	/* TX and RX adaptation code in host byte order. */
	uint32_t	sctp_tx_adaptation_code;
	uint32_t	sctp_rx_adaptation_code;

	/* Reliable control chunks */
	mblk_t		*sctp_cxmit_list; /* Xmit list for control chunks */
	uint32_t	sctp_lcsn;	/* Our serial number */
	uint32_t	sctp_fcsn;	/* Peer serial number */

	/* Per association receive queue */
	kmutex_t	sctp_recvq_lock;
	mblk_t		*sctp_recvq;
	mblk_t		*sctp_recvq_tail;
	taskq_t		*sctp_recvq_tq;

	/* IPv6 ancillary data */
	uint_t		sctp_recvifindex;	/* last rcvd IPV6_RCVPKTINFO */
	uint_t		sctp_recvhops;		/*  " IPV6_RECVHOPLIMIT */
	uint_t		sctp_recvtclass;	/*  " IPV6_RECVTCLASS */
	ip6_hbh_t	*sctp_hopopts;		/*  " IPV6_RECVHOPOPTS */
	ip6_dest_t	*sctp_dstopts;		/*  " IPV6_RECVDSTOPTS */
	ip6_dest_t	*sctp_rthdrdstopts;	/*  " IPV6_RECVRTHDRDSTOPTS */
	ip6_rthdr_t	*sctp_rthdr;		/*  " IPV6_RECVRTHDR */
	uint_t		sctp_hopoptslen;
	uint_t		sctp_dstoptslen;
	uint_t		sctp_rthdrdstoptslen;
	uint_t		sctp_rthdrlen;

	/* Stats */
	uint64_t	sctp_msgcount;
	uint64_t	sctp_prsctpdrop;

	uint_t		sctp_v4label_len;	/* length of cached v4 label */
	uint_t		sctp_v6label_len;	/* length of cached v6 label */
	uint32_t	sctp_rxt_nxttsn;	/* Next TSN to be rexmitted */
	uint32_t	sctp_rxt_maxtsn;	/* Max TSN sent at time out */

	int		sctp_pd_point;		/* Partial delivery point */
	mblk_t		*sctp_err_chunks;	/* Error chunks */
	uint32_t	sctp_err_len;		/* Total error chunks length */

	/* additional source data for per endpoint association statistics */
	uint64_t	sctp_outseqtsns;	/* TSN rx > expected TSN */
	uint64_t	sctp_osacks;		/* total sacks sent */
	uint64_t	sctp_isacks;		/* total sacks received */
	uint64_t	sctp_idupchunks;	/* rx dups, ord or unord */
	uint64_t	sctp_gapcnt;		/* total gap acks rx */
	/*
	 * Add the current data from the counters which are reset by snmp
	 * to these cumulative counters to use in per endpoint statistics.
	 */
	uint64_t	sctp_cum_obchunks;	/* sent control chunks */
	uint64_t	sctp_cum_odchunks;	/* sent ordered data chunks */
	uint64_t	sctp_cum_oudchunks;	/* sent unord data chunks */
	uint64_t	sctp_cum_rxtchunks;	/* retransmitted chunks */
	uint64_t	sctp_cum_ibchunks;	/* recv control chunks */
	uint64_t	sctp_cum_idchunks;	/* recv ordered data chunks */
	uint64_t	sctp_cum_iudchunks;	/* recv unord data chunks */

	/*
	 * When non-zero, this is the maximum observed RTO since assoc stats
	 * were last requested. When zero, no RTO update has occurred since
	 * the previous user request for stats on this endpoint.
	 */
	int	sctp_maxrto;
	/*
	 * The stored value of sctp_maxrto passed to user during the previous
	 * user request for stats on this endpoint.
	 */
	int	sctp_prev_maxrto;

	/* For association counting. */
	sctp_listen_cnt_t	*sctp_listen_cnt;

	/* Congestion control algorithm name, used as default for faddrs */
	char			sctp_cong_alg_name[MODMAXNAMELEN];
} sctp_t;

#define	SCTP_TXQ_LEN(sctp)	((sctp)->sctp_unsent + (sctp)->sctp_unacked)
#define	SCTP_TXQ_UPDATE(sctp)					\
	if ((sctp)->sctp_txq_full && SCTP_TXQ_LEN(sctp) <=	\
	    (sctp)->sctp_connp->conn_sndlowat) {		\
		(sctp)->sctp_txq_full = 0;			\
		(sctp)->sctp_ulp_txq_full((sctp)->sctp_ulpd,	\
		    B_FALSE);					\
	}

#endif	/* (defined(_KERNEL) || defined(_KMEMUSER)) */

extern void	sctp_ack_timer(sctp_t *);
extern size_t	sctp_adaptation_code_param(sctp_t *, uchar_t *);
extern void	sctp_adaptation_event(sctp_t *);
extern int	sctp_add_conn_addr(sctp_stack_t *, sctp_t *, conn_t *,
		    in6_addr_t *, sctp_faddr_t **);
extern void	sctp_add_err(sctp_t *, uint16_t, void *, size_t,
		    sctp_faddr_t *);
extern int	sctp_add_faddr(sctp_t *, sctp_t *, in6_addr_t *,
		    sctp_faddr_t **, sctp_tf_t *, boolean_t);
extern boolean_t sctp_add_ftsn_set(sctp_ftsn_set_t **, sctp_faddr_t *, mblk_t *,
		    uint_t *, uint32_t *);
extern void	sctp_add_recvq(sctp_t *, mblk_t *, boolean_t,
		    ip_recv_attr_t *);
extern void	sctp_add_unrec_parm(sctp_parm_hdr_t *, mblk_t **, boolean_t);
extern size_t	sctp_addr_params(sctp_t *, int, uchar_t *, boolean_t);
extern mblk_t	*sctp_add_proto_hdr(sctp_t *, sctp_faddr_t *, mblk_t *, int,
		    int *);
extern void	sctp_addr_req(sctp_t *, mblk_t *);
extern sctp_t	*sctp_addrlist2sctp(mblk_t *, sctp_hdr_t *, sctp_chunk_hdr_t *,
		    zoneid_t, sctp_stack_t *);
extern void	sctp_check_adv_ack_pt(sctp_t *, mblk_t *, mblk_t *);
extern void	sctp_assoc_event(sctp_t *, uint16_t, uint16_t,
		    sctp_chunk_hdr_t *);

extern void	sctp_bind_hash_insert(sctp_tf_t *, sctp_t *, boolean_t);
extern void	sctp_bind_hash_remove(sctp_t *);
extern int	sctp_bind_add(sctp_t *, const void *, uint32_t, cred_t *);
extern int	sctp_bind_del(sctp_t *, const void *, uint32_t);
extern int	sctp_build_hdrs(sctp_t *, int);

extern int	sctp_check_abandoned_msg(sctp_t *, mblk_t *);
extern boolean_t sctp_check_input(sctp_chunk_hdr_t *, ssize_t, boolean_t);
extern void	sctp_clean_death(sctp_t *, int);
extern void	sctp_close_eager(sctp_t *);
extern int	sctp_compare_faddrsets(sctp_faddr_t *, sctp_faddr_t *);
extern void	sctp_congest_reset(sctp_t *);
extern void	sctp_conn_hash_insert(sctp_tf_t *, sctp_t *, boolean_t);
extern void	sctp_conn_hash_remove(sctp_t *);
extern sctp_t	*sctp_conn_match(in6_addr_t **, uint32_t, in6_addr_t *,
		    uint32_t, zoneid_t, iaflags_t, sctp_stack_t *);
extern void	sctp_conn_reclaim(void *);
extern sctp_t	*sctp_conn_request(sctp_t *, mblk_t *, uint_t, uint_t,
		    sctp_init_chunk_t *, ip_recv_attr_t *);
extern uint32_t	sctp_cumack(sctp_t *, uint32_t, mblk_t **);
extern sctp_t	*sctp_create_eager(sctp_t *);

extern void	sctp_dispatch_rput(queue_t *, sctp_t *, sctp_hdr_t *, mblk_t *,
		    uint_t, uint_t, in6_addr_t);
extern char	*sctp_display(sctp_t *, char *);
extern void	sctp_display_all(sctp_stack_t *);

extern void	sctp_error_event(sctp_t *, sctp_chunk_hdr_t *, boolean_t);

extern void	sctp_faddr_alive(sctp_t *, sctp_faddr_t *, int64_t);
extern boolean_t	sctp_faddr_dead(sctp_t *, sctp_faddr_t *);
extern void	sctp_faddr_fini(void);
extern void	sctp_faddr_init(void);
extern void	sctp_fast_rexmit(sctp_t *);
extern void	sctp_fill_sack(sctp_t *, unsigned char *, int);
extern uint32_t sctp_find_listener_conf(sctp_stack_t *, in_port_t);
extern void	sctp_free_faddr_timers(sctp_t *);
extern void	sctp_free_ftsn_set(sctp_ftsn_set_t *);
extern void	sctp_free_msg(mblk_t *);
extern void	sctp_free_reass(sctp_instr_t *);
extern void	sctp_free_set(sctp_set_t *);
extern void	sctp_ftsn_sets_fini(void);
extern void	sctp_ftsn_sets_init(void);

extern int	sctp_get_addrlist(sctp_t *, const void *, uint32_t *,
		    uchar_t **, int *, size_t *);
extern int	sctp_get_addrparams(sctp_t *, sctp_t *, mblk_t *,
		    sctp_chunk_hdr_t *, uint_t *);
extern void	sctp_get_dest(sctp_t *, sctp_faddr_t *);
extern void	sctp_get_faddr_list(sctp_t *, uchar_t *, size_t);
extern mblk_t	*sctp_get_next_chunk(sctp_t *, mblk_t *, mblk_t **,
		    sctp_msg_hdr_t **);
extern void	sctp_get_saddr_list(sctp_t *, uchar_t *, size_t);

extern int	sctp_handle_error(sctp_t *, sctp_hdr_t *, sctp_chunk_hdr_t *,
		    mblk_t *, ip_recv_attr_t *);
extern void	sctp_hash_destroy(sctp_stack_t *);
extern void	sctp_hash_init(sctp_stack_t *);
extern void	sctp_heartbeat_timer(sctp_t *);

extern void	sctp_icmp_error(sctp_t *, mblk_t *);
extern void	sctp_inc_taskq(sctp_stack_t *);
extern void	sctp_info_req(sctp_t *, mblk_t *);
extern mblk_t	*sctp_init_mp(sctp_t *, sctp_faddr_t *);
extern boolean_t sctp_initialize_params(sctp_t *, sctp_init_chunk_t *,
		    sctp_init_chunk_t *);
extern uint32_t	sctp_init2vtag(sctp_chunk_hdr_t *);
extern void	sctp_intf_event(sctp_t *, in6_addr_t, int, int);
extern void	sctp_input_data(sctp_t *, mblk_t *, ip_recv_attr_t *);
extern void	sctp_instream_cleanup(sctp_t *, boolean_t);
extern boolean_t sctp_is_a_faddr_clean(sctp_t *);

extern void	*sctp_kstat_init(netstackid_t);
extern void	sctp_kstat_fini(netstackid_t, kstat_t *);
extern void	*sctp_kstat2_init(netstackid_t);
extern void	sctp_kstat2_fini(netstackid_t, kstat_t *);

extern ssize_t	sctp_link_abort(mblk_t *, uint16_t, char *, size_t, int,
		    boolean_t);
extern void	sctp_listen_hash_insert(sctp_tf_t *, sctp_t *);
extern void	sctp_listen_hash_remove(sctp_t *);
extern void	sctp_listener_conf_cleanup(sctp_stack_t *);
extern sctp_t	*sctp_lookup(sctp_t *, in6_addr_t *, sctp_tf_t *, uint32_t *,
		    int);
extern sctp_faddr_t *sctp_lookup_faddr(sctp_t *, in6_addr_t *);

extern mblk_t	*sctp_make_err(sctp_t *, uint16_t, void *, size_t);
extern mblk_t	*sctp_make_ftsn_chunk(sctp_t *, sctp_faddr_t *,
		    sctp_ftsn_set_t *, uint_t, uint32_t);
extern void	sctp_make_ftsns(sctp_t *, mblk_t *, mblk_t *, mblk_t **,
		    sctp_faddr_t *, uint32_t *);
extern mblk_t	*sctp_make_mp(sctp_t *, sctp_faddr_t *, int);
extern mblk_t	*sctp_make_sack(sctp_t *, sctp_faddr_t *, mblk_t *);
extern void	sctp_maxpsz_set(sctp_t *);
extern void	sctp_min_fp_mss(sctp_t *);
extern void	sctp_move_faddr_timers(queue_t *, sctp_t *);

extern sctp_parm_hdr_t *sctp_next_parm(sctp_parm_hdr_t *, ssize_t *);

extern void	sctp_ootb_shutdown_ack(mblk_t *, uint_t, ip_recv_attr_t *,
		    ip_stack_t *);
extern size_t	sctp_options_param(const sctp_t *, void *, int);
extern size_t	sctp_options_param_len(const sctp_t *, int);
extern void	sctp_output(sctp_t *, uint_t);

extern void	sctp_partial_delivery_event(sctp_t *, uint16_t, uint16_t);
extern void	sctp_partial_delivery_failure(sctp_t *);
extern int	sctp_process_cookie(sctp_t *, sctp_chunk_hdr_t *, mblk_t *,
		    sctp_init_chunk_t **, int *, in6_addr_t *,
		    ip_recv_attr_t *);
extern void	sctp_process_err(sctp_t *);
extern void	sctp_process_heartbeat(sctp_t *, sctp_chunk_hdr_t *);
extern void	sctp_process_timer(sctp_t *);

extern void	sctp_redo_faddr_srcs(sctp_t *);
extern void	sctp_regift_xmitlist(sctp_t *);
extern void	sctp_return_heartbeat(sctp_t *, sctp_chunk_hdr_t *, mblk_t *);
extern void	sctp_rexmit(sctp_t *, sctp_faddr_t *);
extern void	sctp_rexmit_init(sctp_stack_t *, sctp_t *, sctp_faddr_t *);
extern mblk_t	*sctp_rexmit_packet(sctp_t *, mblk_t **, mblk_t **,
		    sctp_faddr_t *, uint_t *);
extern void	sctp_rexmit_timer(sctp_t *, sctp_faddr_t *);
extern sctp_faddr_t *sctp_rotate_faddr(sctp_t *, sctp_faddr_t *);

extern boolean_t sctp_sack(sctp_t *, mblk_t *);
extern boolean_t sctp_secure_restart_check(sctp_t *, mblk_t *,
		    sctp_chunk_hdr_t *, ip_recv_attr_t *);
extern void	sctp_send_abort(sctp_t *, uint32_t, uint16_t, char *, size_t,
		    mblk_t *, int, boolean_t, ip_recv_attr_t *);
extern void	sctp_ootb_send_abort(uint32_t, uint16_t, char *, size_t,
		    const mblk_t *, int, boolean_t, ip_recv_attr_t *,
		    ip_stack_t *);
extern void	sctp_send_cookie_ack(sctp_t *);
extern void	sctp_send_cookie_echo(sctp_t *, sctp_chunk_hdr_t *, mblk_t *,
			ip_recv_attr_t *);
extern void	sctp_send_initack(sctp_t *, sctp_hdr_t *, sctp_chunk_hdr_t *,
		    mblk_t *, ip_recv_attr_t *);
extern void	sctp_send_shutdown(sctp_t *, int);
extern void	sctp_send_heartbeat(sctp_t *, sctp_faddr_t *);
extern void	sctp_sender_dry_event(sctp_t *);
extern void	sctp_sendfail_event(sctp_t *, mblk_t *, int, boolean_t);
extern void	sctp_set_faddr_current(sctp_t *, sctp_faddr_t *);
extern void	sctp_set_if_mtu(sctp_t *);
extern void	sctp_set_iplen(sctp_t *, mblk_t *, ip_xmit_attr_t *);
extern void	sctp_set_saddr(sctp_t *, sctp_faddr_t *);
extern void	sctp_set_prim(sctp_t *, sctp_faddr_t *);
extern void	sctp_set_ulp_prop(sctp_t *);
extern void	sctp_sets_init(void);
extern void	sctp_sets_fini(void);
extern void	sctp_shutdown_event(sctp_t *);
extern void	sctp_stop_faddr_timers(sctp_t *);
extern int	sctp_shutdown_received(sctp_t *, sctp_chunk_hdr_t *, boolean_t,
		    boolean_t, sctp_faddr_t *);
extern void	sctp_shutdown_complete(sctp_t *);
extern void	sctp_ss_rexmit(sctp_t *);
extern void	sctp_stack_cpu_add(sctp_stack_t *, processorid_t);
extern size_t	sctp_supaddr_param_len(sctp_t *);
extern size_t	sctp_supaddr_param(sctp_t *, uchar_t *);

extern void	sctp_timer(sctp_t *, mblk_t *, clock_t);
extern mblk_t	*sctp_timer_alloc(sctp_t *, pfv_t, int);
extern void	sctp_timer_call(sctp_t *sctp, mblk_t *);
extern void	sctp_timer_free(mblk_t *);
extern void	sctp_timer_stop(mblk_t *);
extern int	sctp_tsol_get_label(sctp_t *);

extern void	sctp_unlink_faddr(sctp_t *, sctp_faddr_t *);
extern void	sctp_update_dce(sctp_t *sctp);
extern in_port_t sctp_update_next_port(in_port_t, zone_t *zone, sctp_stack_t *);
extern void	sctp_update_rtt(sctp_t *, sctp_faddr_t *, clock_t);
extern void	sctp_user_abort(sctp_t *, mblk_t *);

extern void	sctp_validate_peer(sctp_t *);

extern int	sctp_xmit_list_clean(sctp_t *, ssize_t);

extern void	sctp_zap_addrs(sctp_t *);
extern void	sctp_zap_faddrs(sctp_t *, int);
extern sctp_chunk_hdr_t	*sctp_first_chunk(uchar_t *, ssize_t);
extern void	sctp_send_shutdown_ack(sctp_t *, sctp_faddr_t *, boolean_t);

extern void	sctp_cong_init(sctp_t *, sctp_t *, sctp_faddr_t *,
		    const char *);
extern void	sctp_cong_destroy(sctp_faddr_t *);

/* Contract private interface between SCTP and Clustering - PSARC/2005/602 */

extern void	(*cl_sctp_listen)(sa_family_t, uchar_t *, uint_t, in_port_t);
extern void	(*cl_sctp_unlisten)(sa_family_t, uchar_t *, uint_t, in_port_t);
extern void 	(*cl_sctp_connect)(sa_family_t, uchar_t *, uint_t, in_port_t,
		    uchar_t *, uint_t, in_port_t, boolean_t, cl_sctp_handle_t);
extern void	(*cl_sctp_disconnect)(sa_family_t, cl_sctp_handle_t);
extern void	(*cl_sctp_assoc_change)(sa_family_t, uchar_t *, size_t, uint_t,
		    uchar_t *, size_t, uint_t, int, cl_sctp_handle_t);
extern void	(*cl_sctp_check_addrs)(sa_family_t, in_port_t, uchar_t **,
		    size_t, uint_t *, boolean_t);

#define	RUN_SCTP(sctp)						\
{								\
	mutex_enter(&(sctp)->sctp_lock);			\
	while ((sctp)->sctp_running)				\
		cv_wait(&(sctp)->sctp_cv, &(sctp)->sctp_lock);	\
	(sctp)->sctp_running = B_TRUE;				\
	mutex_exit(&(sctp)->sctp_lock);				\
}

/* Wake up recvq taskq */
#define	WAKE_SCTP(sctp)				\
{						\
	mutex_enter(&(sctp)->sctp_lock);	\
	if ((sctp)->sctp_timer_mp != NULL)	\
		sctp_process_timer(sctp);	\
	ASSERT((sctp)->sctp_running);		\
	(sctp)->sctp_running = B_FALSE;		\
	cv_broadcast(&(sctp)->sctp_cv);		\
	mutex_exit(&(sctp)->sctp_lock);		\
}

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_SCTP_SCTP_IMPL_H */

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

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/stream.h>
#include <sys/cmn_err.h>
#include <sys/tihdr.h>
#include <sys/kmem.h>
#define	_SUN_TPI_VERSION 2
#include <sys/tihdr.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/sctp.h>

#include <inet/common.h>
#include <inet/ipclassifier.h>
#include <inet/ip.h>

#include "sctp_impl.h"

/* ARGSUSED */
static void
sctp_notify(sctp_t *sctp, mblk_t *emp, size_t len)
{
	struct T_unitdata_ind *tudi;
	mblk_t *mp;
	sctp_faddr_t *fp;
	int32_t rwnd = 0;
	int error;
	conn_t *connp = sctp->sctp_connp;

	if ((mp = allocb(sizeof (*tudi) + sizeof (void *) +
		sizeof (struct sockaddr_in6), BPRI_HI)) == NULL) {
		/* XXX trouble: don't want to drop events. should queue it. */
		freemsg(emp);
		return;
	}
	dprint(3, ("sctp_notify: event %d\n", (*(uint16_t *)emp->b_rptr)));

	mp->b_datap->db_type = M_PROTO;
	mp->b_flag |= MSGMARK;
	mp->b_rptr += sizeof (void *); /* pointer worth of padding */

	tudi = (struct T_unitdata_ind *)mp->b_rptr;
	tudi->PRIM_type = T_UNITDATA_IND;
	tudi->SRC_offset = sizeof (*tudi);
	tudi->OPT_length = 0;
	tudi->OPT_offset = 0;

	fp = sctp->sctp_primary;
	ASSERT(fp);

	/*
	 * Fill in primary remote address.
	 */
	if (IN6_IS_ADDR_V4MAPPED(&fp->sf_faddr)) {
		struct sockaddr_in *sin4;

		tudi->SRC_length = sizeof (*sin4);
		sin4 = (struct sockaddr_in *)(tudi + 1);
		sin4->sin_family = AF_INET;
		sin4->sin_port = connp->conn_fport;
		IN6_V4MAPPED_TO_IPADDR(&fp->sf_faddr, sin4->sin_addr.s_addr);
		mp->b_wptr = (uchar_t *)(sin4 + 1);
	} else {
		struct sockaddr_in6 *sin6;

		tudi->SRC_length = sizeof (*sin6);
		sin6 = (struct sockaddr_in6 *)(tudi + 1);
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = connp->conn_fport;
		sin6->sin6_addr = fp->sf_faddr;
		mp->b_wptr = (uchar_t *)(sin6 + 1);
	}

	mp->b_cont = emp;

	/*
	 * Notifications are queued regardless of socket rx space.  So
	 * we do not decrement sctp_rwnd here as this will confuse the
	 * other side.
	 */
#ifdef DEBUG
	for (emp = mp->b_cont; emp; emp = emp->b_cont) {
		rwnd += emp->b_wptr - emp->b_rptr;
	}
	ASSERT(len == rwnd);
#endif

	/*
	 * Override b_flag for SCTP sockfs internal use
	 */
	mp->b_flag = (short)SCTP_IS_NOTIFICATION;

	rwnd = sctp->sctp_ulp_recv(sctp->sctp_ulpd, mp, msgdsize(mp), 0,
	    &error, NULL);
	if (rwnd > sctp->sctp_rwnd) {
		sctp->sctp_rwnd = rwnd;
	}
}

void
sctp_assoc_event(sctp_t *sctp, uint16_t state, uint16_t error,
    sctp_chunk_hdr_t *ch)
{
	struct sctp_assoc_change *sacp;
	mblk_t *mp;
	uint16_t ch_len;

	if (!sctp->sctp_recvassocevnt) {
		return;
	}

	ch_len = (ch != NULL) ? ntohs(ch->sch_len) : 0;

	if ((mp = allocb(sizeof (*sacp) + ch_len, BPRI_MED)) == NULL) {
		return;
	}

	sacp = (struct sctp_assoc_change *)mp->b_rptr;
	sacp->sac_type = SCTP_ASSOC_CHANGE;
	sacp->sac_flags = sctp->sctp_prsctp_aware ? SCTP_PRSCTP_CAPABLE : 0;
	sacp->sac_length = sizeof (*sacp) + ch_len;
	sacp->sac_state = state;
	sacp->sac_error = error;
	sacp->sac_outbound_streams = sctp->sctp_num_ostr;
	sacp->sac_inbound_streams = sctp->sctp_num_istr;
	sacp->sac_assoc_id = 0;

	if (ch != NULL)
		bcopy(ch, sacp + 1, ch_len);
	mp->b_wptr += sacp->sac_length;
	sctp_notify(sctp, mp, sacp->sac_length);
}

/*
 * Send failure event. Message is expected to have message header still
 * in place, data follows in subsequent mblk's.
 */
static void
sctp_sendfail(sctp_t *sctp, mblk_t *msghdr, uint16_t flags, int error)
{
	struct sctp_send_failed *sfp;
	struct sctp_send_failed_event *sfvp;
	mblk_t *mp;
	sctp_msg_hdr_t *smh;
	uint32_t len;

	smh = (sctp_msg_hdr_t *)msghdr->b_rptr;

	/*
	 * Allocate a mblk for the notification header.
	 */
	if (sctp->sctp_recvsendfail_event) {
		if ((mp = allocb(sizeof (*sfvp), BPRI_MED)) == NULL) {
			/* give up */
			freemsg(msghdr);
			return;
		}

		sfvp = (struct  sctp_send_failed_event *)mp->b_rptr;
		sfvp->ssfe_type = SCTP_SEND_FAILED_EVENT;
		sfvp->ssfe_flags = flags;
		sfvp->ssfe_length = smh->smh_msglen + sizeof (*sfvp);
		len = sfvp->ssfe_length;
		sfvp->ssfe_error = error;
		sfvp->ssfe_assoc_id = 0;

		bzero(&sfvp->ssfe_info, sizeof (sfvp->ssfe_info));
		sfvp->ssfe_info.snd_sid = smh->smh_sid;
		sfvp->ssfe_info.snd_flags = smh->smh_flags;
		sfvp->ssfe_info.snd_ppid = smh->smh_ppid;
		sfvp->ssfe_info.snd_context = smh->smh_context;
		sfvp->ssfe_info.snd_assoc_id = 0;

		mp->b_wptr = (uchar_t *)(sfvp + 1);
	} else if (sctp->sctp_recvsendfailevnt) {
		if ((mp = allocb(sizeof (*sfp), BPRI_MED)) == NULL) {
			/* give up */
			freemsg(msghdr);
			return;
		}

		sfp = (struct sctp_send_failed *)mp->b_rptr;
		sfp->ssf_type = SCTP_SEND_FAILED;
		sfp->ssf_flags = flags;
		sfp->ssf_length = smh->smh_msglen + sizeof (*sfp);
		len = sfp->ssf_length;
		sfp->ssf_error = error;
		sfp->ssf_assoc_id = 0;

		bzero(&sfp->ssf_info, sizeof (sfp->ssf_info));
		sfp->ssf_info.sinfo_stream = smh->smh_sid;
		sfp->ssf_info.sinfo_flags = smh->smh_flags;
		sfp->ssf_info.sinfo_ppid = smh->smh_ppid;
		sfp->ssf_info.sinfo_context = smh->smh_context;
		sfp->ssf_info.sinfo_timetolive = TICK_TO_MSEC(smh->smh_ttl);

		mp->b_wptr = (uchar_t *)(sfp + 1);
	} else {
		ASSERT(!sctp->sctp_recvsendfail_event &&
		    !sctp->sctp_recvsendfailevnt);
		return;
	}

	mp->b_cont = msghdr->b_cont;
	freeb(msghdr);
	sctp_notify(sctp, mp, len);
}

/*
 * Send failure when the message hasn't been fully chunkified.
 */
void
sctp_sendfail_event(sctp_t *sctp, mblk_t *meta, int error, boolean_t chunkified)
{
	mblk_t		*mp, *nmp, *tmp_mp, *tail;

	if (meta == NULL)
		return;

	if (!sctp->sctp_recvsendfailevnt && !sctp->sctp_recvsendfail_event) {
		if (chunkified)
			sctp_free_msg(meta);
		else
			freemsg(meta);
		return;
	}

	if (!chunkified) {
		sctp_sendfail(sctp, meta, SCTP_DATA_UNSENT, error);
		return;
	}

	/* Remove any prepended headers restoring messages as raw mblks. */
	tail = meta;
	for (mp = SCTP_MSG_HEAD(meta); mp != NULL; mp = nmp) {
		nmp = mp->b_next;

		/* We have allocated a chunk header mblk. */
		if (mp->b_cont != NULL) {
			tmp_mp = mp;
			mp = mp->b_cont;
			tmp_mp->b_prev = tmp_mp->b_next = NULL;
			freeb(tmp_mp);
		} else {
			mp->b_rptr += sizeof (sctp_data_hdr_t);
		}
		mp->b_next = NULL;
		tail->b_cont = mp;
		tail = mp;
	}

	sctp_sendfail(sctp, meta, SCTP_DATA_UNSENT, error);
}

void
sctp_regift_xmitlist(sctp_t *sctp)
{
	mblk_t *meta;

	if (!sctp->sctp_recvsendfailevnt && !sctp->sctp_recvsendfail_event) {
		return;
	}

	while ((meta = sctp->sctp_xmit_head) != NULL) {
		sctp->sctp_xmit_head = meta->b_next;
		meta->b_next = NULL;
		ASSERT(meta->b_prev == NULL);
		if (sctp->sctp_xmit_head != NULL)
			sctp->sctp_xmit_head->b_prev = NULL;
		sctp_sendfail_event(sctp, meta, 0, B_TRUE);
	}
	sctp->sctp_xmit_tail = sctp->sctp_xmit_last_sent = NULL;
	sctp->sctp_unacked = sctp->sctp_unsent = 0;
}

void
sctp_intf_event(sctp_t *sctp, in6_addr_t addr, int state, int error)
{
	struct sctp_paddr_change *spc;
	ipaddr_t addr4;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	mblk_t *mp;

	if (!sctp->sctp_recvpathevnt) {
		return;
	}

	if ((mp = allocb(sizeof (*spc), BPRI_MED)) == NULL) {
		return;
	}

	spc = (struct sctp_paddr_change *)mp->b_rptr;
	spc->spc_type = SCTP_PEER_ADDR_CHANGE;
	spc->spc_flags = 0;
	spc->spc_length = sizeof (*spc);
	if (IN6_IS_ADDR_V4MAPPED(&addr)) {
		IN6_V4MAPPED_TO_IPADDR(&addr, addr4);
		sin = (struct sockaddr_in *)&spc->spc_aaddr;
		sin->sin_family = AF_INET;
		sin->sin_port = 0;
		sin->sin_addr.s_addr = addr4;
	} else {
		sin6 = (struct sockaddr_in6 *)&spc->spc_aaddr;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = 0;
		sin6->sin6_addr = addr;
	}
	spc->spc_state = state;
	spc->spc_error = error;
	spc->spc_assoc_id = 0;

	mp->b_wptr = (uchar_t *)(spc + 1);
	sctp_notify(sctp, mp, spc->spc_length);
}

void
sctp_error_event(sctp_t *sctp, sctp_chunk_hdr_t *ch, boolean_t is_asconf)
{
	struct sctp_remote_error *sre;
	mblk_t *mp;
	size_t len;
	sctp_parm_hdr_t *errh = NULL;
	uint16_t dlen = 0;
	uint16_t error = 0;
	void *dtail = NULL;

	if (!sctp->sctp_recvpeererr) {
		return;
	}

	/*
	 * ASCONF PARM error chunks :
	 * PARM_ERROR_IND parm type is always followed by correlation id.
	 * See sctp_asconf_adderr() and sctp_asconf_prepend_errwrap() as to
	 * how these error chunks are build.
	 * error cause wrapper (PARM_ERROR_IND) + correlation id +
	 * error cause + error cause details.
	 *
	 * Regular error chunks :
	 * See sctp_make_err() as to how these error chunks are build.
	 * error chunk header (CHUNK_ERROR) + error cause + error cause details.
	 */
	if (is_asconf) {
		if (ntohs(ch->sch_len) >
		    (sizeof (*ch) + sizeof (uint32_t))) {
			errh = (sctp_parm_hdr_t *)((char *)ch +
			    sizeof (uint32_t) + sizeof (sctp_parm_hdr_t));
		}
	} else {
		if (ntohs(ch->sch_len) > sizeof (*ch)) {
			errh = (sctp_parm_hdr_t *)(ch + 1);
		}
	}

	if (errh != NULL) {
		error = ntohs(errh->sph_type);
		dlen = ntohs(errh->sph_len) - sizeof (*errh);
		if (dlen > 0) {
			dtail = errh + 1;
		}
	}

	len = sizeof (*sre) + dlen;
	if ((mp = allocb(len, BPRI_MED)) == NULL) {
		return;
	}

	sre = (struct sctp_remote_error *)mp->b_rptr;
	sre->sre_type = SCTP_REMOTE_ERROR;
	sre->sre_flags = 0;
	sre->sre_length = len;
	sre->sre_assoc_id = 0;
	sre->sre_error = error;
	if (dtail != NULL) {
		bcopy(dtail, sre + 1, dlen);
	}

	mp->b_wptr = mp->b_rptr + len;
	sctp_notify(sctp, mp, len);
}

void
sctp_shutdown_event(sctp_t *sctp)
{
	struct sctp_shutdown_event *sse;
	mblk_t *mp;

	if (!sctp->sctp_recvshutdownevnt) {
		return;
	}

	if ((mp = allocb(sizeof (*sse), BPRI_MED)) == NULL) {
		return;
	}

	sse = (struct sctp_shutdown_event *)mp->b_rptr;
	sse->sse_type = SCTP_SHUTDOWN_EVENT;
	sse->sse_flags = 0;
	sse->sse_length = sizeof (*sse);
	sse->sse_assoc_id = 0;

	mp->b_wptr = (uchar_t *)(sse + 1);
	sctp_notify(sctp, mp, sse->sse_length);
}

void
sctp_adaptation_event(sctp_t *sctp)
{
	struct sctp_adaptation_event *sai;
	mblk_t *mp;

	if (!sctp->sctp_recvalevnt || !sctp->sctp_recv_adaptation) {
		return;
	}
	if ((mp = allocb(sizeof (*sai), BPRI_MED)) == NULL) {
		return;
	}

	sai = (struct sctp_adaptation_event *)mp->b_rptr;
	sai->sai_type = SCTP_ADAPTATION_INDICATION;
	sai->sai_flags = 0;
	sai->sai_length = sizeof (*sai);
	sai->sai_assoc_id = 0;
	sai->sai_adaptation_ind = sctp->sctp_rx_adaptation_code;

	mp->b_wptr = (uchar_t *)(sai + 1);
	sctp_notify(sctp, mp, sai->sai_length);

	sctp->sctp_recv_adaptation = 0; /* in case there's a restart later */
}

/* Send partial deliver event */
void
sctp_partial_delivery_event(sctp_t *sctp, uint16_t sid, uint16_t ssn)
{
	struct sctp_pdapi_event	*pdapi;
	mblk_t			*mp;

	if (!sctp->sctp_recvpdevnt)
		return;

	if ((mp = allocb(sizeof (*pdapi), BPRI_MED)) == NULL)
		return;

	pdapi = (struct sctp_pdapi_event *)mp->b_rptr;
	pdapi->pdapi_type = SCTP_PARTIAL_DELIVERY_EVENT;
	pdapi->pdapi_flags = 0;
	pdapi->pdapi_length = sizeof (*pdapi);
	pdapi->pdapi_indication = SCTP_PARTIAL_DELIVERY_ABORTED;
	pdapi->pdapi_assoc_id = 0;
	pdapi->pdapi_stream = sid;
	pdapi->pdapi_seq = ssn;
	mp->b_wptr = (uchar_t *)(pdapi + 1);
	sctp_notify(sctp, mp, pdapi->pdapi_length);
}

void
sctp_sender_dry_event(sctp_t *sctp)
{
	struct sctp_sender_dry_event	*sde;
	mblk_t				*mp;

	/*
	 * There is nothing to report if the association is not
	 * established.
	 */
	if (!sctp->sctp_recvsender_dry || sctp->sctp_state < SCTPS_ESTABLISHED)
		return;

	if ((mp = allocb(sizeof (*sde), BPRI_MED)) == NULL)
		return;

	sde = (struct sctp_sender_dry_event *)mp->b_rptr;
	sde->sender_dry_type = SCTP_SENDER_DRY_EVENT;
	sde->sender_dry_flags = 0;
	sde->sender_dry_length = sizeof (*sde);
	sde->sender_dry_assoc_id = 0;
	mp->b_wptr = (uchar_t *)(sde + 1);
	sctp_notify(sctp, mp, sde->sender_dry_length);
}

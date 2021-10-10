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
#define	_SUN_TPI_VERSION 2
#include <sys/tihdr.h>
#include <sys/socket.h>
#include <sys/stropts.h>
#include <sys/strsun.h>
#include <sys/strsubr.h>
#include <sys/socketvar.h>
#include <sys/sdt.h>
#include <inet/common.h>
#include <inet/mi.h>
#include <inet/ip.h>
#include <inet/ip_ire.h>
#include <inet/ip6.h>
#include <inet/sctp_ip.h>
#include <inet/ipclassifier.h>

/*
 * PR-SCTP comments.
 *
 * A message can expire before it gets to the transmit list (i.e. it is still
 * in the unsent list - unchunked), after it gets to the transmit list, but
 * before transmission has actually started, or after transmission has begun.
 * Accordingly, we check for the status of a message in sctp_chunkify() when
 * the message is being transferred from the unsent list to the transmit list;
 * in sctp_get_msg_to_send(), when we get the next chunk from the transmit
 * list and in sctp_rexmit() when we get the next chunk to be (re)transmitted.
 * When we nuke a message in sctp_chunkify(), all we need to do is take it
 * out of the unsent list and update sctp_unsent; when a message is deemed
 * timed-out in sctp_get_msg_to_send() we can just take it out of the transmit
 * list, update sctp_unsent IFF transmission for the message has not yet begun
 * (i.e. !SCTP_CHUNK_ISSENT(meta->b_cont)). However, if transmission for the
 * message has started, then we cannot just take it out of the list, we need
 * to send Forward TSN chunk to the peer so that the peer can clear its
 * fragment list for this message. However, we cannot just send the Forward
 * TSN in sctp_get_msg_to_send() because there might be unacked chunks for
 * messages preceding this abandoned message. So, we send a Forward TSN
 * IFF all messages prior to this abandoned message has been SACKd, if not
 * we defer sending the Forward TSN to sctp_cumack(), which will check for
 * this condition and send the Forward TSN via sctp_check_abandoned_msg(). In
 * sctp_rexmit() when we check for retransmissions, we need to determine if
 * the advanced peer ack point can be moved ahead, and if so, send a Forward
 * TSN to the peer instead of retransmitting the chunk. Note that when
 * we send a Forward TSN for a message, there may be yet unsent chunks for
 * this message; we need to mark all such chunks as abandoned, so that
 * sctp_cumack() can take the message out of the transmit list, additionally
 * sctp_unsent need to be adjusted. Whenever sctp_unsent is updated (i.e.
 * decremented when a message/chunk is deemed abandoned), sockfs needs to
 * be notified so that it can adjust its idea of the queued message.
 */

#include "sctp_impl.h"

static struct kmem_cache	*sctp_kmem_ftsn_set_cache;
static mblk_t			*sctp_chunkify(sctp_t *, int, int, int);

#ifdef	DEBUG
static boolean_t	sctp_verify_chain(mblk_t *, mblk_t *);
#endif

/*
 * Called to allocate a header mblk when sending data to SCTP.
 * Data will follow in b_cont of this mblk.
 */
mblk_t *
sctp_alloc_hdr(const char *name, int nlen, const char *control, int clen,
    int flags)
{
	mblk_t *mp;
	struct T_unitdata_req *tudr;
	size_t size;
	int error;

	size = sizeof (*tudr) + _TPI_ALIGN_TOPT(nlen) + clen;
	size = MAX(size, sizeof (sctp_msg_hdr_t));
	if (flags & SCTP_CAN_BLOCK) {
		mp = allocb_wait(size, BPRI_MED, 0, &error);
	} else {
		mp = allocb(size, BPRI_MED);
	}
	if (mp) {
		tudr = (struct T_unitdata_req *)mp->b_rptr;
		tudr->PRIM_type = T_UNITDATA_REQ;
		tudr->DEST_length = nlen;
		tudr->DEST_offset = sizeof (*tudr);
		tudr->OPT_length = clen;
		tudr->OPT_offset = (t_scalar_t)(sizeof (*tudr) +
		    _TPI_ALIGN_TOPT(nlen));
		if (nlen > 0)
			bcopy(name, tudr + 1, nlen);
		if (clen > 0)
			bcopy(control, (char *)tudr + tudr->OPT_offset, clen);
		mp->b_wptr += (tudr ->OPT_offset + clen);
		mp->b_datap->db_type = M_PROTO;
	}
	return (mp);
}

/*
 * Called to allocate a header mblk when sending data to SCTP.  Data will
 * follow in b_cont of this mblk.  The difference between this function and
 * sctp_alloc_hdr() is that this function can do a copyin from user space.
 */
int
sctp_alloc_hdr_copy(mblk_t **mctl, const void *name, int nlen,
    void **control, int clen, uint_t ctype, boolean_t kernel)
{
	mblk_t *mp;
	struct T_unitdata_req *tudr;
	size_t size;
	int error;

	size = sizeof (*tudr) + _TPI_ALIGN_TOPT(nlen) + clen +
	    sizeof (struct cmsghdr);
	size = MAX(size, sizeof (sctp_msg_hdr_t));
	if ((mp = allocb(size, BPRI_MED)) == NULL)
		return (ENOMEM);

	tudr = (struct T_unitdata_req *)mp->b_rptr;
	tudr->PRIM_type = T_UNITDATA_REQ;
	tudr->DEST_length = nlen;
	tudr->DEST_offset = sizeof (*tudr);
	tudr->OPT_length = sizeof (struct cmsghdr) + clen;
	tudr->OPT_offset = (t_scalar_t)(sizeof (*tudr) + _TPI_ALIGN_TOPT(nlen));
	if (nlen > 0) {
		if (kernel) {
			bcopy(name, tudr + 1, nlen);
		} else {
			if ((error = xcopyin(name, tudr + 1, nlen)) != 0) {
				freeb(mp);
				return (error);
			}
		}
	}
	if (clen > 0) {
		struct cmsghdr *cmsg;

		cmsg = (struct cmsghdr *)((char *)tudr + tudr->OPT_offset);
		if (kernel) {
			bcopy(*control, cmsg + 1, clen);
		} else {
			if ((error = xcopyin(*control, cmsg + 1, clen)) != 0) {
				freeb(mp);
				return (error);
			}
		}
		cmsg->cmsg_len = tudr->OPT_length;
		cmsg->cmsg_level = IPPROTO_SCTP;
		cmsg->cmsg_type = ctype;
		*control = cmsg;
	}
	mp->b_wptr += (tudr->OPT_offset + tudr->OPT_length);
	DB_TYPE(mp) = M_PROTO;
	*mctl = mp;
	return (0);
}

/*
 * Helper function for sctp_sendmsg().  It parses the ancillary data structures
 * and set the given message meta structure accordingly.
 */
static int
sctp_sendmsg_anx(struct cmsghdr *cmsg, char *cend, sctp_msg_hdr_t *hdr)
{
	struct sctp_sndrcvinfo	*sndrcv;
	struct sctp_sndinfo	*sinfo;
	struct sctp_prinfo	*prinfo;
	struct sctp_sendv_spa	*spa;
	boolean_t		sinfo_exist = B_FALSE;
	boolean_t		prinfo_exist = B_FALSE;

	for (;;) {
		if ((char *)(cmsg + 1) > cend ||
		    ((char *)cmsg + cmsg->cmsg_len) > cend) {
			break;
		}
		if (cmsg->cmsg_level == IPPROTO_SCTP) {
			switch (cmsg->cmsg_type) {
			case SCTP_SNDRCV :
				if (cmsg->cmsg_len < sizeof (*cmsg) +
				    sizeof (*sndrcv) || sinfo_exist) {
					return (EINVAL);
				}
				sndrcv = (struct sctp_sndrcvinfo *)(cmsg + 1);
				hdr->smh_sid = sndrcv->sinfo_stream;
				hdr->smh_flags = sndrcv->sinfo_flags;
				hdr->smh_ppid = sndrcv->sinfo_ppid;
				hdr->smh_context = sndrcv->sinfo_context;
				if (sndrcv->sinfo_timetolive != 0) {
					/*
					 * If smh_ttl != 0, smh_tob should have
					 * been set by caller.
					 */
					if (hdr->smh_ttl == 0) {
						hdr->smh_tob =
						    ddi_get_lbolt64();
					}
					hdr->smh_ttl = MAX(1, MSEC_TO_TICK(
					    sndrcv->sinfo_timetolive));
				}
				sinfo_exist = B_TRUE;
				break;
			case SCTP_SNDINFO:
				if (cmsg->cmsg_len < sizeof (*cmsg) +
				    sizeof (*sinfo) || sinfo_exist) {
					return (EINVAL);
				}
				sinfo = (struct sctp_sndinfo *)(cmsg + 1);
				hdr->smh_sid = sinfo->snd_sid;
				hdr->smh_flags = sinfo->snd_flags;
				hdr->smh_ppid = sinfo->snd_ppid;
				hdr->smh_context = sinfo->snd_context;
				sinfo_exist = B_TRUE;
				break;
			case SCTP_PRINFO:
				if (cmsg->cmsg_len < sizeof (*cmsg) +
				    sizeof (*prinfo) || prinfo_exist) {
					return (EINVAL);
				}
				prinfo = (struct sctp_prinfo *)(cmsg + 1);
				/* Only support Timed Partial Reliability. */
				if (prinfo->pr_policy != SCTP_PR_SCTP_TTL)
					break;
				if (prinfo->pr_value != 0) {
					if (hdr->smh_ttl == 0) {
						hdr->smh_tob =
						    ddi_get_lbolt64();
					}
					hdr->smh_ttl = MAX(1, MSEC_TO_TICK(
					    prinfo->pr_value));
				}
				prinfo_exist = B_TRUE;
				break;
			case SCTP_SPAINFO:
				if (cmsg->cmsg_len < sizeof (*cmsg) +
				    sizeof (*spa)) {
					return (EINVAL);
				}
				spa = (struct sctp_sendv_spa *)(cmsg + 1);
				if (spa->sendv_flags &
				    SCTP_SEND_SNDINFO_VALID) {
					if (sinfo_exist)
						return (EINVAL);
					sinfo = &spa->sendv_sndinfo;
					hdr->smh_sid = sinfo->snd_sid;
					hdr->smh_flags = sinfo->snd_flags;
					hdr->smh_ppid = sinfo->snd_ppid;
					hdr->smh_context = sinfo->snd_context;
					sinfo_exist = B_TRUE;
				}
				if (spa->sendv_flags &
				    SCTP_SEND_PRINFO_VALID) {
					if (prinfo_exist)
						return (EINVAL);
					prinfo = &spa->sendv_prinfo;
					if (prinfo->pr_policy !=
					    SCTP_PR_SCTP_TTL) {
						break;
					}
					if (prinfo->pr_value != 0) {
						if (hdr->smh_ttl == 0) {
							hdr->smh_tob =
							    ddi_get_lbolt64();
						}
						hdr->smh_ttl = MAX(1,
						    MSEC_TO_TICK(
						    prinfo->pr_value));
					}
					prinfo_exist = B_TRUE;
				}
				if (spa->sendv_flags &
				    SCTP_SEND_AUTHINFO_VALID) {
					return (EOPNOTSUPP);
				}
				break;
			default:
				return (EOPNOTSUPP);
			}
		}
		if (cmsg->cmsg_len > 0)
			cmsg = CMSG_NEXT(cmsg);
		else
			break;
	}
	return (0);
}

/*
 * Create SCTP data chunks from a given message.  Note that the upper layer is
 * supposed to fragment a message into sctp_mss size mblk_t.  Hence we can make
 * each mblk_t of the given message into an SCTP data chunk.  The
 * smh_xmit_head in the messsage meta structure points to the first of those
 * chunks.  And smh_xmit_tail points to the last mblk_t.  The data chunks
 * are linked using the b_next pointer.
 */
static int
sctp_make_chunks(sctp_t *sctp, mblk_t *meta, mblk_t *new_msg,
    boolean_t first, boolean_t last, uint32_t *extra_len)
{
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	sctp_msg_hdr_t	*msg_hdr;
	sctp_data_hdr_t	*sdc;
	mblk_t		*mp, *next_mp, *save_mp, *last_mp;
	mblk_t		*smh_tail;
	uint32_t	 msg_len;
	size_t		xtralen;

	msg_hdr = (sctp_msg_hdr_t *)meta->b_rptr;
	xtralen = MAX(sctp->sctp_hdr_len, sctp->sctp_hdr6_len) +
	    sizeof (*sdc) + sctps->sctps_wroff_xtra;

	mp = new_msg->b_cont;
	new_msg->b_cont = NULL;
	smh_tail = msg_hdr->smh_xmit_tail;
	msg_len = 0;

	for (; mp != NULL; mp = next_mp) {
		mblk_t *chunk_hdr;
		size_t mp_len;

		mp_len = MBLKL(mp);

		/* Check if we need to allocate a chunk header mblk. */
		if (((intptr_t)mp->b_rptr) & (SCTP_ALIGN - 1) ||
		    DB_REF(mp) > 1 || MBLKHEAD(mp) < sizeof (*sdc)) {
			chunk_hdr = allocb(xtralen, BPRI_MED);
			if (chunk_hdr == NULL)
				goto chunk_cleanup;
			chunk_hdr->b_rptr += xtralen - sizeof (*sdc);
			chunk_hdr->b_wptr = chunk_hdr->b_rptr + sizeof (*sdc);
			chunk_hdr->b_cont = mp;
		} else {
			chunk_hdr = mp;
			chunk_hdr->b_rptr -= sizeof (*sdc);
		}

		next_mp = mp->b_cont;
		mp->b_cont = NULL;

		sdc = (sctp_data_hdr_t *)chunk_hdr->b_rptr;
		sdc->sdh_id = CHUNK_DATA;
		sdc->sdh_flags = 0;
		sdc->sdh_len = htons(sizeof (*sdc) + mp_len);
		sdc->sdh_sid = htons(msg_hdr->smh_sid);
		sdc->sdh_ssn = 0;
		sdc->sdh_tsn = 0;

		/*
		 * We defer assigning the SSN just before sending the chunk,
		 * else if we drop the chunk in sctp_get_msg_to_send(), we
		 * would need to send a Forward TSN to let the peer know. Some
		 * more comments about this in sctp_impl.h for SCTP_CHUNK_SENT.
		 */
		sdc->sdh_payload_id = msg_hdr->smh_ppid;

		if (msg_hdr->smh_xmit_head == NULL) {
			msg_hdr->smh_xmit_head = chunk_hdr;
			msg_hdr->smh_xmit_tail = chunk_hdr;
		} else {
			msg_hdr->smh_xmit_tail->b_next = chunk_hdr;
			msg_hdr->smh_xmit_tail = chunk_hdr;
		}

		msg_len += mp_len;
	}

	/* Empty message... */
	if (msg_len == 0) {
		sctp_sendfail_event(sctp, dupmsg(new_msg),
		    SCTP_ERR_NO_USR_DATA, B_FALSE);
		return (EINVAL);
	}

	/*
	 * Note that we may be adding new data to an existing message if
	 * SCTP_EXPLICIT_EOR opton is in use.
	 */
	msg_hdr->smh_msglen += msg_len;
	*extra_len = msg_len;

	if (first) {
		sdc = (sctp_data_hdr_t *)msg_hdr->smh_xmit_head->b_rptr;
		SCTP_DATA_SET_BBIT(sdc);
	}
	if (last) {
		sdc = (sctp_data_hdr_t *)msg_hdr->smh_xmit_tail->b_rptr;
		SCTP_DATA_SET_EBIT(sdc);
		SCTP_SET_MSG_COMPLETE(meta);
	}

	return (0);

chunk_cleanup:
	/*
	 * Recontruct the original mblk chain.  Save where we stop in the for
	 * loop above.
	 */
	save_mp = mp;
	last_mp = NULL;
	for (mp = smh_tail->b_next; mp != NULL; mp = next_mp) {
		mblk_t *cont_mp;

		next_mp = mp->b_next;

		/* Free the extra header we allocated. */
		if (mp->b_cont) {
			cont_mp = mp->b_cont;
			mp->b_next = NULL;
			freeb(mp);
			mp = cont_mp;
		}
		if (new_msg->b_cont == NULL)
			new_msg->b_cont = mp;
		else
			last_mp->b_cont = mp;
		last_mp = mp;
	}
	/* last_mp is NULL if we have not processed anything. */
	if (last_mp != NULL)
		last_mp->b_cont = save_mp;
	else
		new_msg->b_cont = save_mp;
	smh_tail->b_next = NULL;

	return (ENOBUFS);
}

/*ARGSUSED2*/
int
sctp_sendmsg(sctp_t *sctp, mblk_t *mp, int flags, cred_t *cr)
{
	sctp_faddr_t	*fp = NULL;
	struct T_unitdata_req	*tudr;
	int		error = 0;
	mblk_t		*mproto = mp;
	in6_addr_t	*addr;
	in6_addr_t	tmpaddr;
	sctp_msg_hdr_t	hdr;
	conn_t		*connp = sctp->sctp_connp;
	uint32_t	msg_len;
	boolean_t	add_xmit_list = B_TRUE;

	ASSERT(DB_TYPE(mproto) == M_PROTO);
	ASSERT(cr != NULL);

	mp = mp->b_cont;
	ASSERT(mp == NULL || DB_TYPE(mp) == M_DATA);

	tudr = (struct T_unitdata_req *)mproto->b_rptr;
	ASSERT(tudr->PRIM_type == T_UNITDATA_REQ);

	/* Initialize the SCTP message header. */
	hdr.smh_ttl = sctp->sctp_def_timetolive;
	if (hdr.smh_ttl != 0)
		hdr.smh_tob = ddi_get_lbolt64();
	else
		hdr.smh_tob = 0;
	hdr.smh_context = sctp->sctp_def_context;
	hdr.smh_sid = sctp->sctp_def_stream;
	hdr.smh_ssn = 0;
	hdr.smh_ppid = sctp->sctp_def_ppid;
	hdr.smh_msglen = 0;
	hdr.smh_flags = sctp->sctp_def_flags;
	hdr.smh_xmit_head = NULL;
	hdr.smh_xmit_tail = NULL;
	hdr.smh_xmit_last_sent = NULL;

	/* Get destination address, if specified */
	if (tudr->DEST_length > 0) {
		sin_t *sin;
		sin6_t *sin6;

		sin = (struct sockaddr_in *)
		    (mproto->b_rptr + tudr->DEST_offset);
		switch (sin->sin_family) {
		case AF_INET:
			if (tudr->DEST_length < sizeof (*sin)) {
				return (EINVAL);
			}
			IN6_IPADDR_TO_V4MAPPED(sin->sin_addr.s_addr, &tmpaddr);
			addr = &tmpaddr;
			break;
		case AF_INET6:
			if (tudr->DEST_length < sizeof (*sin6)) {
				return (EINVAL);
			}
			sin6 = (struct sockaddr_in6 *)
			    (mproto->b_rptr + tudr->DEST_offset);
			addr = &sin6->sin6_addr;
			break;
		default:
			return (EAFNOSUPPORT);
		}
		fp = sctp_lookup_faddr(sctp, addr);
		if (fp == NULL) {
			return (EINVAL);
		}
	}

	/* Ancillary Data? */
	if (tudr->OPT_length > 0) {
		struct cmsghdr		*cmsg;
		char			*cend;

		cmsg = (struct cmsghdr *)(mproto->b_rptr + tudr->OPT_offset);
		cend = ((char *)cmsg + tudr->OPT_length);
		ASSERT(cend <= (char *)mproto->b_wptr);

		if ((error = sctp_sendmsg_anx(cmsg, cend, &hdr)) != 0)
			return (error);
	}

	if (hdr.smh_flags & SCTP_ABORT) {
		if (mp != NULL && mp->b_cont) {
			mblk_t *pump = msgpullup(mp, -1);

			if (pump == NULL)
				return (ENOMEM);
			freemsg(mp);
			mp = pump;
			mproto->b_cont = mp;
		}
		RUN_SCTP(sctp);
		sctp_user_abort(sctp, mp);
		freemsg(mproto);
		WAKE_SCTP(sctp);
		return (0);
	}

	/* No data, just return. */
	if (mp == NULL) {
		freeb(mproto);
		return (0);
	}

	RUN_SCTP(sctp);

	/*
	 * Reject any new data requests before the set up of an association.
	 */
	if (sctp->sctp_state < SCTPS_COOKIE_WAIT ||
	    (sctp->sctp_state == SCTPS_COOKIE_WAIT &&
	    connp->conn_passive_connect)) {
		error = ENOTCONN;
		goto unlock_done;
	}
	/* Reject any new data requests if we are shutting down */
	if (sctp->sctp_state > SCTPS_ESTABLISHED ||
	    (sctp->sctp_connp->conn_state_flags & CONN_CLOSING)) {
		error = EPIPE;
		goto unlock_done;
	}

	/*
	 * We accept any stream number before the initial handshake is
	 * finished since we don't really know the correct number of
	 * output stream we can use.  When sending the COOKIE-ECHO chunk
	 * (sctp_send_cookie_echo()), we will go through this list and
	 * remove those invalid messages.
	 */
	if (sctp->sctp_state >= SCTPS_COOKIE_ECHOED &&
	    hdr.smh_sid >= sctp->sctp_num_ostr) {
		/* Send sendfail event */
		sctp_sendfail_event(sctp, dupmsg(mproto), SCTP_ERR_BAD_SID,
		    B_FALSE);
		error = EINVAL;
		goto unlock_done;
	}

	/* Re-use the mproto to store relevant info. */
	ASSERT(MBLKSIZE(mproto) >= sizeof (sctp_msg_hdr_t));

	mproto->b_rptr = mproto->b_datap->db_base;
	mproto->b_wptr = mproto->b_rptr + sizeof (sctp_msg_hdr_t);

	if (sctp->sctp_explicit_eor) {
		boolean_t eor_set;

		if (hdr.smh_flags & SCTP_EOR)
			eor_set = B_TRUE;
		else
			eor_set = B_FALSE;

		/* Check if this is a continuation of the previous message. */
		if (!sctp->sctp_eor_on) {
			/* Beginning of a message. */
			bcopy(&hdr, mproto->b_rptr, sizeof (sctp_msg_hdr_t));
			if ((error = sctp_make_chunks(sctp, mproto,
			    mproto, B_TRUE, eor_set ? B_TRUE : B_FALSE,
			    &msg_len)) != 0) {
				goto unlock_done;
			}
		} else {
			sctp_msg_hdr_t *msg_hdr;

			/*
			 * Continuation of the previous message.  Make sure
			 * that the sid matches.
			 */
			msg_hdr =
			    (sctp_msg_hdr_t *)sctp->sctp_xmit_tail->b_rptr;
			if (msg_hdr->smh_sid != hdr.smh_sid) {
				error = EINVAL;
				goto unlock_done;
			}
			/*
			 * Use the original sctp_msg_hdr_t so that the
			 * message is linked to it.
			 */
			if ((error = sctp_make_chunks(sctp,
			    sctp->sctp_xmit_tail, mproto, B_FALSE,
			    eor_set ? B_TRUE : B_FALSE, &msg_len)) != 0) {
				goto unlock_done;
			}
			add_xmit_list = B_FALSE;
		}

		/* End of message.  Turn off sctp_eor_on. */
		if (eor_set)
			sctp->sctp_eor_on = B_FALSE;
		else
			sctp->sctp_eor_on = B_TRUE;
	} else {
		bcopy(&hdr, mproto->b_rptr, sizeof (sctp_msg_hdr_t));
		if ((error = sctp_make_chunks(sctp, mproto, mproto,
		    B_TRUE, B_TRUE, &msg_len)) != 0) {
			goto unlock_done;
		}
	}

	/* User requested specific destination */
	SCTP_SET_CHUNK_DEST(mproto, fp);

	sctp->sctp_unsent += msg_len;
	BUMP_LOCAL(sctp->sctp_msgcount);

	/* Add it to the xmit list if it is not a continuation of last msg. */
	if (add_xmit_list) {
		if (sctp->sctp_xmit_head == NULL) {
			sctp->sctp_xmit_head = sctp->sctp_xmit_tail = mproto;
			sctp->sctp_xmit_last_sent = NULL;
		} else {
			mproto->b_prev = sctp->sctp_xmit_tail;
			sctp->sctp_xmit_tail->b_next = mproto;
			sctp->sctp_xmit_tail = mproto;
		}
	} else {
		/*
		 * Since we re-use the original sctp_msg_hdr_t, we need
		 * to free the header of the new message.
		 */
		ASSERT(mproto->b_cont == NULL);
		freeb(mproto);
	}

	/*
	 * Notify sockfs if the tx queue is full.
	 */
	if (SCTP_TXQ_LEN(sctp) > connp->conn_sndbuf) {
		sctp->sctp_txq_full = 1;
		sctp->sctp_ulp_txq_full(sctp->sctp_ulpd, B_TRUE);
	}
	if (sctp->sctp_state == SCTPS_ESTABLISHED)
		sctp_output(sctp, sctp->sctp_max_burst);

unlock_done:
	WAKE_SCTP(sctp);
	return (error);
}

void
sctp_free_msg(mblk_t *meta)
{
	mblk_t *mp, *nxt_mp;

	for (mp = SCTP_MSG_HEAD(meta); mp != NULL; mp = nxt_mp) {
		nxt_mp = mp->b_next;
		mp->b_next = mp->b_prev = NULL;
		freemsg(mp);
	}
	meta->b_prev = meta->b_next = NULL;
	freeb(meta);
}

mblk_t *
sctp_add_proto_hdr(sctp_t *sctp, sctp_faddr_t *fp, mblk_t *mp, int sacklen,
    int *error)
{
	int hdrlen;
	uchar_t *hdr;
	int isv4 = fp->sf_isv4;
	sctp_stack_t	*sctps = sctp->sctp_sctps;

	if (error != NULL)
		*error = 0;

	if (isv4) {
		hdrlen = sctp->sctp_hdr_len;
		hdr = sctp->sctp_iphc;
	} else {
		hdrlen = sctp->sctp_hdr6_len;
		hdr = sctp->sctp_iphc6;
	}
	/*
	 * A reject|blackhole could mean that the address is 'down'. Similarly,
	 * it is possible that the address went down, we tried to send an
	 * heartbeat and ended up setting fp->sf_saddr as unspec because we
	 * didn't have any usable source address.  In either case
	 * sctp_get_dest() will try find an IRE, if available, and set
	 * the source address, if needed.  If we still don't have any
	 * usable source address, fp->sf_state will be SCTP_FADDRS_UNREACH and
	 * we return EHOSTUNREACH.
	 */
	ASSERT(fp->sf_ixa->ixa_ire != NULL);
	if ((fp->sf_ixa->ixa_ire->ire_flags & (RTF_REJECT|RTF_BLACKHOLE)) ||
	    SCTP_IS_ADDR_UNSPEC(fp->sf_isv4, fp->sf_saddr)) {
		sctp_get_dest(sctp, fp);
		if (fp->sf_state == SCTP_FADDRS_UNREACH) {
			if (error != NULL)
				*error = EHOSTUNREACH;
			return (NULL);
		}
	}
	/* Copy in IP header. */
	if ((mp->b_rptr - mp->b_datap->db_base) <
	    (sctps->sctps_wroff_xtra + hdrlen + sacklen) || DB_REF(mp) > 2) {
		mblk_t *nmp;

		/*
		 * This can happen if IP headers are adjusted after
		 * data was moved into chunks, or during retransmission,
		 * or things like snoop is running.
		 */
		nmp = allocb(sctps->sctps_wroff_xtra + hdrlen + sacklen,
		    BPRI_MED);
		if (nmp == NULL) {
			if (error !=  NULL)
				*error = ENOMEM;
			return (NULL);
		}
		nmp->b_rptr += sctps->sctps_wroff_xtra;
		nmp->b_wptr = nmp->b_rptr + hdrlen + sacklen;
		nmp->b_cont = mp;
		mp = nmp;
	} else {
		mp->b_rptr -= (hdrlen + sacklen);
	}
	bcopy(hdr, mp->b_rptr, hdrlen);
	if (sacklen) {
		sctp_fill_sack(sctp, mp->b_rptr + hdrlen, sacklen);
	}
	if (fp != sctp->sctp_current) {
		/* change addresses in header */
		if (isv4) {
			ipha_t *iph = (ipha_t *)mp->b_rptr;

			IN6_V4MAPPED_TO_IPADDR(&fp->sf_faddr, iph->ipha_dst);
			if (!IN6_IS_ADDR_V4MAPPED_ANY(&fp->sf_saddr)) {
				IN6_V4MAPPED_TO_IPADDR(&fp->sf_saddr,
				    iph->ipha_src);
			} else if (sctp->sctp_bound_to_all) {
				iph->ipha_src = INADDR_ANY;
			}
		} else {
			ip6_t *ip6h = (ip6_t *)mp->b_rptr;

			ip6h->ip6_dst = fp->sf_faddr;
			if (!IN6_IS_ADDR_UNSPECIFIED(&fp->sf_saddr)) {
				ip6h->ip6_src = fp->sf_saddr;
			} else if (sctp->sctp_bound_to_all) {
				ip6h->ip6_src = ipv6_all_zeros;
			}
		}
	}
	return (mp);
}

/*
 * SCTP requires every chunk to be padded so that the total length
 * is a multiple of SCTP_ALIGN.  This function adds the padding and if
 * necessary, links a mblk with the specified pad length.  It returns the
 * tail of the mblk_t chain in last_mp after linking the given mp to the
 * b_cont of last_mp.
 */
static boolean_t
sctp_add_padding(sctp_t *sctp, mblk_t *mp, int pad, mblk_t **last_mp)
{
	mblk_t *fill;
	int i;

	ASSERT(pad < SCTP_ALIGN);
	ASSERT(sctp->sctp_pad_mp != NULL);
	ASSERT(last_mp != NULL);

	/* No padding is needed, just set *last_mp. */
	if (pad == 0) {
		/*
		 * sctp_make_chunks() makes sure that either this is
		 * a standalone mblk_t; or one mblk_t containing the
		 * data chunk header (allocated by sctp_make_chunks())
		 * and the mblk_t containing the data.
		 */
		ASSERT(mp->b_cont == NULL || mp->b_cont->b_cont == NULL);

		/* Link the given mp to the last_mp chain and set last_mp. */
		if (*last_mp != NULL)
			(*last_mp)->b_cont = mp;
		*last_mp = (mp->b_cont == NULL) ? mp : mp->b_cont;
		return (B_TRUE);
	}

	while (mp->b_cont != NULL)
		mp = mp->b_cont;

	/*
	 * If the reference count is 2, it means that we are the only
	 * one accessing the mblk.  Check if there is any room left
	 * at the end.  If there is, extend b_wptr and fill it with 0.
	 */
	if (DB_REF(mp) == 2 && MBLKTAIL(mp) >= pad) {
		for (i = 0; i < pad; i++)
			mp->b_wptr[i] = 0;
		mp->b_wptr += pad;
		if (*last_mp != NULL)
			(*last_mp)->b_cont = mp;
		*last_mp = mp;
		return (B_TRUE);
	}

	if ((fill = dupb(sctp->sctp_pad_mp)) != NULL) {
		fill->b_wptr += pad;
		mp->b_cont = fill;
		if (*last_mp != NULL)
			(*last_mp)->b_cont = mp;
		*last_mp = fill;
		return (B_TRUE);
	}

	/*
	 * The memory saving path of reusing the sctp_pad_mp
	 * fails may be because it has been dupb() too
	 * many times (DBLK_REFMAX).  Use the memory consuming
	 * path of allocating the pad mblk.
	 */
	if ((fill = allocb(SCTP_ALIGN, BPRI_MED)) != NULL) {
		/* Zero it out.  SCTP_ALIGN is sizeof (int32_t) */
		*(int32_t *)fill->b_rptr = 0;
		fill->b_wptr += pad;
		mp->b_cont = fill;
		if (*last_mp != NULL)
			(*last_mp)->b_cont = mp;
		*last_mp = fill;
		return (B_TRUE);
	}

	return (B_FALSE);
}

static mblk_t *
sctp_find_fast_rexmit_mblks(sctp_t *sctp, int *total, sctp_faddr_t **fp)
{
	mblk_t		*meta;
	mblk_t		*start_mp;
	mblk_t		*end_mp;
	mblk_t		*mp, *nmp;
	sctp_data_hdr_t	*sdh;
	int		msglen;
	int		extra;
	sctp_msg_hdr_t	*msg_hdr;
	sctp_faddr_t	*old_fp = NULL;
	sctp_faddr_t	*chunk_fp;
	sctp_stack_t	*sctps = sctp->sctp_sctps;

	start_mp = NULL;
	end_mp = NULL;
	for (meta = sctp->sctp_xmit_head; meta != NULL; meta = meta->b_next) {
		msg_hdr = (sctp_msg_hdr_t *)meta->b_rptr;
		if (SCTP_IS_MSG_ABANDONED(meta) ||
		    SCTP_MSG_TO_BE_ABANDONED(msg_hdr, sctp, LBOLT_FASTPATH64)) {
			continue;
		}
		for (mp = msg_hdr->smh_xmit_head; mp != NULL; mp = mp->b_next) {
			if (SCTP_CHUNK_WANT_REXMIT(mp)) {
				/*
				 * Use the same peer address to do fast
				 * retransmission.  If the original peer
				 * address is dead, switch to the current
				 * one.  Record the old one so that we
				 * will pick the chunks sent to the old
				 * one for fast retransmission.
				 */
				chunk_fp = SCTP_CHUNK_DEST(mp);
				if (*fp == NULL) {
					*fp = chunk_fp;
					if ((*fp)->sf_state !=
					    SCTP_FADDRS_ALIVE) {
						old_fp = *fp;
						*fp = sctp->sctp_current;
					}
				} else if (old_fp == NULL && *fp != chunk_fp) {
					continue;
				} else if (old_fp != NULL &&
				    old_fp != chunk_fp) {
					continue;
				}

				sdh = (sctp_data_hdr_t *)mp->b_rptr;
				msglen = ntohs(sdh->sdh_len);
				extra = SCTP_PAD_LEN(msglen);

				/*
				 * We still return at least the first message
				 * even if that message cannot fit in as
				 * PMTU may have changed.
				 */
				if (*total + msglen + extra >
				    (*fp)->sf_pmss && start_mp != NULL) {
					return (start_mp);
				}
				if ((nmp = dupmsg(mp)) == NULL)
					return (start_mp);
				if (!sctp_add_padding(sctp, nmp, extra,
				    &end_mp)) {
					return (start_mp);
				}

				SCTPS_BUMP_MIB(sctps, sctpOutFastRetrans);
				BUMP_LOCAL(sctp->sctp_rxtchunks);
				SCTP_CHUNK_CLEAR_REXMIT(mp);

				if (start_mp == NULL)
					start_mp = nmp;
				*total += msglen + extra;
				DTRACE_PROBE3(sctp_find_fast_rexmit_mblks,
				    sctp_t *, sctp, sctp_faddr_t *, fp,
				    uint32_t, ntohl(sdh->sdh_tsn));
			}
		}
	}
	/* Clear the flag as there is no more message to be fast rexmitted. */
	sctp->sctp_chk_fast_rexmit = B_FALSE;
	return (start_mp);
}

/* A debug function just to make sure that a mblk chain is not broken */
#ifdef	DEBUG
static boolean_t
sctp_verify_chain(mblk_t *head, mblk_t *tail)
{
	mblk_t	*mp = head;

	if (head == NULL || tail == NULL)
		return (B_TRUE);
	while (mp != NULL) {
		if (mp == tail)
			return (B_TRUE);
		mp = mp->b_next;
	}
	return (B_FALSE);
}
#endif

/*
 * Given a message, return the next new chunk to send.  If the given message is
 * NULL, start with the head of xmit list.  Messages that are abandoned are
 * skipped.  A message can be abandoned if it has a non-zero timetolive and
 * transmission has not yet started or if it is a partially reliable
 * message and its time is up (assuming we are PR-SCTP aware).
 */
mblk_t *
sctp_get_next_chunk(sctp_t *sctp, mblk_t *cur_meta, mblk_t **nxt_meta,
    sctp_msg_hdr_t **nxt_mhdr)
{
	sctp_msg_hdr_t	*msg_hdr;
	mblk_t		*tmp_meta;
	mblk_t		*chunk;
	int64_t		now;

	/* If no message is given, start from the beginning. */
	if (cur_meta == NULL) {
		cur_meta = sctp->sctp_xmit_head;
		if (cur_meta == NULL)
			return (NULL);
	}

	msg_hdr = (sctp_msg_hdr_t *)cur_meta->b_rptr;
	now = LBOLT_FASTPATH64;

	/*
	 * smh_xmit_last_sent != NULL means that part of this message
	 * has been sent.
	 */
	if (msg_hdr->smh_xmit_last_sent != NULL) {
		if (SCTP_MSG_TO_BE_ABANDONED(msg_hdr, sctp, now)) {
			if (sctp_check_abandoned_msg(sctp, cur_meta) != 0) {
				DTRACE_PROBE2(sctp_get_next_chunk, sctp_t *,
				    sctp, mblk_t *, cur_meta);
#ifdef  DEBUG
				ASSERT(sctp_verify_chain(sctp->sctp_xmit_head,
				    sctp->sctp_xmit_tail));
#endif
				return (NULL);
			}
		} else {
			/*
			 * If there is a next chunk in the current message,
			 * return it.
			 */
			if ((chunk = msg_hdr->smh_xmit_last_sent->b_next) !=
			    NULL) {
				*nxt_meta = cur_meta;
				*nxt_mhdr = msg_hdr;
				return (chunk);
			}
		}

		/* No next chunk in the current message, go to next one. */
		cur_meta = cur_meta->b_next;

		/* No next message, just return. */
		if (cur_meta == NULL)
			return (NULL);
		msg_hdr = (sctp_msg_hdr_t *)cur_meta->b_rptr;
	}

	/*
	 * This message, cur_meta, has never been sent.  Check if it is to be
	 * abandoned.  If it is, remove it from the xmit list, send up a
	 * notification and continue with the next one.
	 */
	while (SCTP_MSG_TO_BE_ABANDONED(msg_hdr, sctp, now)) {
		/*
		 * b_prev == NULL means that it is the first msge in
		 * the xmit_list.
		 */
		if (cur_meta->b_prev == NULL)
			sctp->sctp_xmit_head = cur_meta->b_next;
		else
			cur_meta->b_prev->b_next = cur_meta->b_next;
		if (cur_meta->b_next != NULL)
			cur_meta->b_next->b_prev = cur_meta->b_prev;
		tmp_meta = cur_meta;
		cur_meta = cur_meta->b_next;
		tmp_meta->b_prev = tmp_meta->b_next = NULL;
		sctp->sctp_unsent -= msg_hdr->smh_msglen;

		/*
		 * If the current message is not complete because
		 * SCTP_EOR option is set, turn off sctp_eor_on
		 * so that the next send is treated as another msg.
		 */
		if (!SCTP_MSG_IS_COMPLETE(tmp_meta)) {
			ASSERT(sctp->sctp_eor_on);
			sctp->sctp_eor_on = B_FALSE;
		}
		if (!SCTP_IS_DETACHED(sctp))
			SCTP_TXQ_UPDATE(sctp);
		sctp_sendfail_event(sctp, tmp_meta, 0, B_TRUE);
		if (cur_meta == NULL)
			return (NULL);
		msg_hdr = (sctp_msg_hdr_t *)cur_meta->b_rptr;
	}

	*nxt_meta = cur_meta;
	*nxt_mhdr = msg_hdr;
	ASSERT(msg_hdr->smh_xmit_head != NULL);
	return (msg_hdr->smh_xmit_head);
}

/*
 * This function handles the case when SCTP needs to send out a packet
 * larger than the MSS.  It returns an appropriate ixa to be used to
 * send the packet.  And if it is an IPv4 packet, the given packet's
 * IPv4 header DF bit is also cleared.
 */
static ip_xmit_attr_t *
sctp_big_pkt_ixa(mblk_t *mp, sctp_faddr_t *fp)
{
	ip_xmit_attr_t *ixa;

	/*
	 * Path MTU is different from what we thought it would
	 * be when we created chunks, or IP headers have grown.
	 * Need to clear the DF bit.  We also need to set
	 * the ixa flags appropriately, otherwise IP will
	 * drop this packet.
	 */
	ixa = ip_xmit_attr_duplicate(fp->sf_ixa);
	if (ixa == NULL) {
		freemsg(mp);
		return (NULL);
	}
	ixa->ixa_flags &= ~(IXAF_DONTFRAG | IXAF_PMTU_IPV4_DF |
	    IXAF_VERIFY_PMTU);

	if (fp->sf_isv4) {
		ipha_t *iph = (ipha_t *)mp->b_rptr;

		iph->ipha_fragment_offset_and_flags = 0;
	}
	return (ixa);
}

void
sctp_fast_rexmit(sctp_t *sctp)
{
	mblk_t		*mp, *head;
	int		pktlen = 0;
	sctp_faddr_t	*fp = NULL;
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	ip_xmit_attr_t	*ixa;

	ASSERT(sctp->sctp_xmit_head != NULL);
	mp = sctp_find_fast_rexmit_mblks(sctp, &pktlen, &fp);
	if (mp == NULL) {
		SCTP_KSTAT(sctps, sctp_fr_not_found);
		return;
	}
	if ((head = sctp_add_proto_hdr(sctp, fp, mp, 0, NULL)) == NULL) {
		freemsg(mp);
		SCTP_KSTAT(sctps, sctp_fr_add_hdr);
		return;
	}
	if (pktlen > fp->sf_pmss) {
		DTRACE_PROBE2(sctp_fast_rexmit_df, sctp_t *, sctp,
		    sctp_faddr_t *, fp);
		ixa = sctp_big_pkt_ixa(head, fp);
		if (ixa == NULL) {
			SCTP_KSTAT(sctps, sctp_fr_add_hdr);
			return;
		}
	} else {
		ixa = fp->sf_ixa;
	}

	sctp_set_iplen(sctp, head, fp->sf_ixa);
	(void) conn_ip_output(head, fp->sf_ixa);
	BUMP_LOCAL(sctp->sctp_opkts);
	sctp->sctp_active = fp->sf_lastactive = ddi_get_lbolt64();
	if (ixa != fp->sf_ixa)
		ixa_refrele(ixa);
}

/*
 * This function handles new data transmission.
 */
void
sctp_output(sctp_t *sctp, uint_t num_pkt)
{
	mblk_t			*mp = NULL;
	mblk_t			*nmp;
	mblk_t			*head, *tail;
	mblk_t			*meta;
	uint16_t 		data_len;
	uint32_t		pkt_data_len;
	uint32_t 		cansend;
	int32_t			seglen;
	int32_t			sacklen;
	int			extra;
	int64_t			now = LBOLT_FASTPATH64;
	sctp_faddr_t		*fp;
	sctp_faddr_t		*lfp;
	sctp_data_hdr_t		*sdc;
	int			error;
	boolean_t		notsent = B_TRUE;
	sctp_stack_t		*sctps = sctp->sctp_sctps;
	sctp_msg_hdr_t		*msg_hdr;
	ip_xmit_attr_t		*ixa;
	uint32_t		tsn;
	uint32_t		mss;

	lfp = NULL;
	head = NULL;
	meta = sctp->sctp_xmit_last_sent;
	cansend = sctp->sctp_frwnd;
	if (sctp->sctp_unsent < cansend)
		cansend = sctp->sctp_unsent;

	/* Check if there is something to be sent.  Chunks may be abandoned. */
	mp = sctp_get_next_chunk(sctp, meta, &meta, &msg_hdr);
	if (mp == NULL) {
		DTRACE_PROBE2(sctp_output_null, sctp_t *, sctp, uint32_t,
		    cansend);
		return;
	}

	/* Check if we can bundle a SACK chunk. */
	if (sctp->sctp_ftsn == sctp->sctp_lastacked + 1) {
		sacklen = 0;
	} else {
		sacklen = sizeof (sctp_chunk_hdr_t) +
		    sizeof (sctp_sack_chunk_t) +
		    (sizeof (sctp_sack_frag_t) * sctp->sctp_sack_gaps);
		/*
		 * SACK chunk needs to be sent to where the data chunk came
		 * from.
		 */
		lfp = sctp->sctp_lastdata;
		ASSERT(lfp != NULL);
		if (lfp->sf_state != SCTP_FADDRS_ALIVE)
			lfp = NULL;
	}

	/*
	 * Check if the message is supposed to be sent to a specific peer
	 * address.  If not, send to sctp_current or lfp.  After fp is set,
	 * this will be the dest for all chunks sent during this call to
	 * sctp_output().
	 */
	fp = SCTP_CHUNK_DEST(meta);
	if (fp == NULL || fp->sf_state != SCTP_FADDRS_ALIVE) {
		if (lfp != NULL)
			fp = lfp;
		else
			fp = sctp->sctp_current;
	} else {
		/*
		 * The chunk destination is set and it is not the same as
		 * where we shoould send the SACK chunk.  So we cannot bundle
		 * the SACK chunk.
		 */
		if (fp != lfp)
			sacklen = 0;
	}
	mss = fp->sf_pmss;

	/*
	 * Nagle Algorithm:
	 *
	 * If there is unack'ed data in the network and the data to be
	 * sent is less than sf_naglim (Nagle Algorithm threshold), delay
	 * sending the unsent messages.  But if SCTP_NODELAY is set or the
	 * last packet sent is larger than sf_naglim, don't delay.
	 *
	 * Start persist timer if unable to send or when trying to send
	 * into a zero window.  This timer ensures the blocked send attempt
	 * is retried.
	 */
	if ((cansend < fp->sf_naglim && sctp->sctp_unacked != 0 &&
	    fp->sf_last_sent_len < fp->sf_naglim && !sctp->sctp_ndelay) ||
	    (cansend == 0 && sctp->sctp_unacked == 0 &&
	    sctp->sctp_unsent != 0)) {
		DTRACE_PROBE3(sctp_output_nagle, sctp_t *, sctp, uint32_t,
		    cansend, uint32_t, fp->sf_last_sent_len);
		goto unsent_data;
	}

	/*
	 * If we haven't sent data to this destination for
	 * a while, do slow start again.
	 */
	if (now - fp->sf_lastactive > fp->sf_rto) {
		SET_CWND(fp, mss, sctps->sctps_slow_start_after_idle);
		SCTP_CONG_AFTER_IDLE(fp);
		DTRACE_PROBE3(sctp_cwnd_update, sctp_faddr_t *, fp, uint32_t,
		    fp->sf_cwnd, int, TCPCONG_D_CWND_AFTER_IDLE);
	}

	/* Does this fp's cwnd allows us to send more? */
	if (fp->sf_cwnd <= fp->sf_suna) {
		DTRACE_PROBE4(sctp_output_cwnd, sctp_t *, sctp,
		    sctp_faddr_t *, fp, uint32_t, fp->sf_cwnd, uint32_t,
		    fp->sf_suna);
		goto unsent_data;
	}
	cansend = MIN(cansend, fp->sf_cwnd - fp->sf_suna);

	/*
	 * If length of SACK chunk + data chunk is bigger than 1 packet,
	 * don't bundle the SACK chunk.
	 */
	if (sacklen > 0) {
		sdc = (sctp_data_hdr_t *)mp->b_rptr;
		if (sacklen + ntohs(sdc->sdh_len) +
		    SCTP_PAD_LEN(ntohs(sdc->sdh_len)) > mss) {
			sacklen = 0;
		}
	}

	/*
	 * We have a data chunk to send and potentially a SACK chunk
	 * to bundle with it (if sacklen > 0).  Begin packetizing that
	 * data now.  Inside this per-packet loop there is another
	 * loop potentially bundling other chunks if pkt space and
	 * receiver window permit.
	 */
	seglen = sacklen;
	for (; mp != NULL && cansend > 0 && num_pkt-- != 0;
	    mp = sctp_get_next_chunk(sctp, meta, &meta, &msg_hdr)) {
		ASSERT(!SCTP_CHUNK_ISSENT(mp));

		sdc = (sctp_data_hdr_t *)mp->b_rptr;
		seglen += ntohs(sdc->sdh_len);
		data_len = ntohs(sdc->sdh_len) - sizeof (*sdc);
		pkt_data_len = data_len;

		if (cansend < data_len) {
			DTRACE_PROBE3(sctp_output_cansend, sctp_t *, sctp,
			    sctp_faddr_t *, fp, uint32_t, cansend);
			head = NULL;
			goto unsent_data;
		}
		if ((nmp = dupmsg(mp)) == NULL)
			goto unsent_data;
		SCTP_CHUNK_CLEAR_FLAGS(nmp);
		head = sctp_add_proto_hdr(sctp, fp, nmp, sacklen, &error);
		if (head == NULL) {
			/*
			 * If none of the source addresses are
			 * available (i.e error == EHOSTUNREACH),
			 * pretend we have sent the data. We will
			 * eventually time out trying to retramsmit
			 * the data if the interface never comes up.
			 * If we have already sent some stuff (i.e.,
			 * notsent is B_FALSE) then we are fine, else
			 * just mark this packet as sent.
			 */
			if (notsent && error == EHOSTUNREACH) {
				SCTP_CHUNK_SENT(sctp, mp, sdc, fp, data_len,
				    meta);
				msg_hdr->smh_xmit_last_sent = mp;
				sctp->sctp_xmit_last_sent = meta;
			}
			freemsg(nmp);
			SCTP_KSTAT(sctps, sctp_output_failed);
			goto unsent_data;
		}
		sacklen = 0;

		SCTP_CHUNK_SENT(sctp, mp, sdc, fp, data_len, meta);
		msg_hdr->smh_xmit_last_sent = mp;
		sctp->sctp_xmit_last_sent = meta;

		extra = SCTP_PAD_LEN(data_len);
		tail = NULL;
		if (!sctp_add_padding(sctp, nmp, extra, &tail))
			goto unsent_data;
		seglen += extra;

		DTRACE_PROBE5(sctp_output, sctp_t *, sctp, sctp_faddr_t *, fp,
		    uint32_t, ntohl(sdc->sdh_tsn), uint16_t,
		    ntohs(sdc->sdh_ssn), int32_t, data_len);

		/*
		 * Bundle chunks.  We link up (using b_cont) the chunks
		 * together to send downstream in a single packet.  We keep
		 * the pointer tail pointing to the last mblk_t of the chain.
		 *
		 * Data chunks are fragmented in sctp_make_chunks() when
		 * a message is sent down from upper layer via sctp_sendmsg().
		 * Chunks will not be fragmented again to fit in the
		 * remaining space of a packet.
		 */
		while (seglen < mss) {
			int32_t		new_len;

			mp = sctp_get_next_chunk(sctp, meta, &meta, &msg_hdr);
			if (mp == NULL)
				break;

			/*
			 * If the next chunk is set to be sent to a different
			 * peer address, don't bundle it.
			 */
			if (SCTP_CHUNK_DEST(meta) != NULL &&
			    fp != SCTP_CHUNK_DEST(meta)) {
				break;
			}
			ASSERT(!SCTP_CHUNK_ISSENT(mp));

			sdc = (sctp_data_hdr_t *)mp->b_rptr;
			new_len = ntohs(sdc->sdh_len);
			data_len = new_len - sizeof (*sdc);
			if (data_len + pkt_data_len > cansend)
				break;
			extra = SCTP_PAD_LEN(data_len);
			new_len += seglen + extra;

			if (new_len > mss)
				break;

			if ((nmp = dupmsg(mp)) == NULL)
				break;
			if (!sctp_add_padding(sctp, nmp, extra, &tail)) {
				freemsg(nmp);
				break;
			}
			seglen = new_len;
			pkt_data_len += data_len;
			SCTP_CHUNK_CLEAR_FLAGS(nmp);
			SCTP_CHUNK_SENT(sctp, mp, sdc, fp, data_len, meta);
			sctp->sctp_xmit_last_sent = meta;
			msg_hdr->smh_xmit_last_sent = mp;

			DTRACE_PROBE5(sctp_output, sctp_t *, sctp,
			    sctp_faddr_t *, fp, uint32_t, ntohl(sdc->sdh_tsn),
			    uint16_t, ntohs(sdc->sdh_ssn), int32_t, data_len);
		}

		if (seglen > mss) {
			DTRACE_PROBE2(sctp_output_df, sctp_t *, sctp,
			    sctp_faddr_t *, fp);
			ixa = sctp_big_pkt_ixa(head, fp);
			if (ixa == NULL) {
				SCTP_KSTAT(sctps, sctp_output_failed);
				head = NULL;
				goto unsent_data;
			}
		} else {
			ixa = fp->sf_ixa;
		}

		/* Remember this TSN in case we need it for RTT measurement. */
		tsn = ntohl(sdc->sdh_tsn);

		/* Send the segment. */
		ASSERT(cansend >= pkt_data_len);
		cansend -= pkt_data_len;
		sctp_set_iplen(sctp, head, ixa);
		fp->sf_last_sent_len = seglen;
		(void) conn_ip_output(head, ixa);

		BUMP_LOCAL(sctp->sctp_opkts);
		notsent = B_FALSE;
		if (ixa != fp->sf_ixa)
			ixa_refrele(ixa);
		DTRACE_PROBE3(sctp_output_out, sctp_t *, sctp, sctp_faddr_t *,
		    fp, int32_t, seglen);
		seglen = 0;
	}
	/*
	 * Use this chunk (last chunk in the last packet sent) to measure RTT?
	 */
	if (sctp->sctp_out_time == 0) {
		sctp->sctp_out_time = now;
		sctp->sctp_rtt_tsn = tsn;
	}
	/* Arm rto timer (if not set) */
	if (!fp->sf_timer_running)
		SCTP_FADDR_TIMER_RESTART(sctp, fp, fp->sf_rto, now);
	fp->sf_lastactive = now;
	sctp->sctp_active = now;
	return;

unsent_data:
	/* Arm persist timer (if rto timer not set) */
	if (!fp->sf_timer_running)
		SCTP_FADDR_TIMER_RESTART(sctp, fp, fp->sf_rto, now);
	if (head != NULL)
		freemsg(head);
}

/*
 * The following two functions initialize and destroy the cache
 * associated with the sets used for PR-SCTP.
 */
void
sctp_ftsn_sets_init(void)
{
	sctp_kmem_ftsn_set_cache = kmem_cache_create("sctp_ftsn_set_cache",
	    sizeof (sctp_ftsn_set_t), 0, NULL, NULL, NULL, NULL,
	    NULL, 0);
}

void
sctp_ftsn_sets_fini(void)
{
	kmem_cache_destroy(sctp_kmem_ftsn_set_cache);
}


/* Free PR-SCTP sets */
void
sctp_free_ftsn_set(sctp_ftsn_set_t *s)
{
	sctp_ftsn_set_t *p;

	while (s != NULL) {
		p = s->next;
		s->next = NULL;
		kmem_cache_free(sctp_kmem_ftsn_set_cache, s);
		s = p;
	}
}

/*
 * Given a message meta block, meta, this routine creates or modifies
 * the set that will be used to generate a Forward TSN chunk. If the
 * entry for stream id, sid, for this message already exists, the
 * sequence number, ssn, is updated if it is greater than the existing
 * one. If an entry for this sid does not exist, one is created if
 * the size does not exceed fp->sf_pmss. We return false in case
 * or an error.
 */
boolean_t
sctp_add_ftsn_set(sctp_ftsn_set_t **s, sctp_faddr_t *fp, mblk_t *meta,
    uint_t *nsets, uint32_t *slen)
{
	sctp_ftsn_set_t		*p;
	sctp_msg_hdr_t		*msg_hdr = (sctp_msg_hdr_t *)meta->b_rptr;
	uint16_t		sid = htons(msg_hdr->smh_sid);
	/* msg_hdr->smh_ssn is already in NBO */
	uint16_t		ssn = msg_hdr->smh_ssn;

	ASSERT(s != NULL && nsets != NULL);
	ASSERT((*nsets == 0 && *s == NULL) || (*nsets > 0 && *s != NULL));

	if (*s == NULL) {
		ASSERT((*slen + sizeof (uint32_t)) <= fp->sf_pmss);
		*s = kmem_cache_alloc(sctp_kmem_ftsn_set_cache, KM_NOSLEEP);
		if (*s == NULL)
			return (B_FALSE);
		(*s)->ftsn_entries.ftsn_sid = sid;
		(*s)->ftsn_entries.ftsn_ssn = ssn;
		(*s)->next = NULL;
		*nsets = 1;
		*slen += sizeof (uint32_t);
		return (B_TRUE);
	}
	for (p = *s; p->next != NULL; p = p->next) {
		if (p->ftsn_entries.ftsn_sid == sid) {
			if (SSN_GT(ssn, p->ftsn_entries.ftsn_ssn))
				p->ftsn_entries.ftsn_ssn = ssn;
			return (B_TRUE);
		}
	}
	/* the last one */
	if (p->ftsn_entries.ftsn_sid == sid) {
		if (SSN_GT(ssn, p->ftsn_entries.ftsn_ssn))
			p->ftsn_entries.ftsn_ssn = ssn;
	} else {
		if ((*slen + sizeof (uint32_t)) > fp->sf_pmss)
			return (B_FALSE);
		p->next = kmem_cache_alloc(sctp_kmem_ftsn_set_cache,
		    KM_NOSLEEP);
		if (p->next == NULL)
			return (B_FALSE);
		p = p->next;
		p->ftsn_entries.ftsn_sid = sid;
		p->ftsn_entries.ftsn_ssn = ssn;
		p->next = NULL;
		(*nsets)++;
		*slen += sizeof (uint32_t);
	}
	return (B_TRUE);
}

/*
 * Given a set of stream id - sequence number pairs, this routing creates
 * a Forward TSN chunk. The cumulative TSN (advanced peer ack point)
 * for the chunk is obtained from sctp->sctp_adv_pap. The caller
 * will add the IP/SCTP header.
 */
mblk_t *
sctp_make_ftsn_chunk(sctp_t *sctp, sctp_faddr_t *fp, sctp_ftsn_set_t *sets,
    uint_t nsets, uint32_t seglen)
{
	mblk_t			*ftsn_mp;
	sctp_chunk_hdr_t	*ch_hdr;
	uint32_t		*advtsn;
	uint16_t		schlen;
	size_t			xtralen;
	ftsn_entry_t		*ftsn_entry;
	sctp_stack_t	*sctps = sctp->sctp_sctps;

	seglen += sizeof (sctp_chunk_hdr_t);
	if (fp->sf_isv4)
		xtralen = sctp->sctp_hdr_len + sctps->sctps_wroff_xtra;
	else
		xtralen = sctp->sctp_hdr6_len + sctps->sctps_wroff_xtra;
	ftsn_mp = allocb(xtralen + seglen, BPRI_MED);
	if (ftsn_mp == NULL)
		return (NULL);
	ftsn_mp->b_rptr += xtralen;
	ftsn_mp->b_wptr = ftsn_mp->b_rptr + seglen;

	ch_hdr = (sctp_chunk_hdr_t *)ftsn_mp->b_rptr;
	ch_hdr->sch_id = CHUNK_FORWARD_TSN;
	ch_hdr->sch_flags = 0;
	/*
	 * The cast here should not be an issue since seglen is
	 * the length of the Forward TSN chunk.
	 */
	schlen = (uint16_t)seglen;
	U16_TO_ABE16(schlen, &(ch_hdr->sch_len));

	advtsn = (uint32_t *)(ch_hdr + 1);
	U32_TO_ABE32(sctp->sctp_adv_pap, advtsn);
	ftsn_entry = (ftsn_entry_t *)(advtsn + 1);
	while (nsets > 0) {
		ASSERT((uchar_t *)&ftsn_entry[1] <= ftsn_mp->b_wptr);
		ftsn_entry->ftsn_sid = sets->ftsn_entries.ftsn_sid;
		ftsn_entry->ftsn_ssn = sets->ftsn_entries.ftsn_ssn;
		ftsn_entry++;
		sets = sets->next;
		nsets--;
	}
	return (ftsn_mp);
}

/*
 * Given a starting message, the routine steps through all the
 * messages whose TSN is less than sctp->sctp_adv_pap and creates
 * ftsn sets. The ftsn sets is then used to create an Forward TSN
 * chunk. All the messages, that have chunks that are included in the
 * ftsn sets, are flagged abandonded. If a message is partially sent
 * and is deemed abandoned, all remaining unsent chunks are marked
 * abandoned and are deducted from sctp_unsent.
 */
void
sctp_make_ftsns(sctp_t *sctp, mblk_t *meta, mblk_t *mp, mblk_t **nmp,
    sctp_faddr_t *fp, uint32_t *seglen)
{
	mblk_t		*mp1 = mp;
	mblk_t		*mp_head = mp;
	mblk_t		*meta_head = meta;
	mblk_t		*head;
	sctp_ftsn_set_t	*sets = NULL;
	uint_t		nsets = 0;
	uint16_t	clen;
	sctp_data_hdr_t	*sdc;
	uint32_t	sacklen;
	uint32_t	adv_pap = sctp->sctp_adv_pap;
	uint32_t	unsent = 0;
	boolean_t	ubit;
	sctp_stack_t	*sctps = sctp->sctp_sctps;

	*seglen = sizeof (uint32_t);

	sdc  = (sctp_data_hdr_t *)mp1->b_rptr;
	while (meta != NULL &&
	    SEQ_GEQ(sctp->sctp_adv_pap, ntohl(sdc->sdh_tsn))) {
		/*
		 * Skip adding FTSN sets for un-ordered messages as they do
		 * not have SSNs.
		 */
		ubit = SCTP_DATA_GET_UBIT(sdc);
		if (!ubit &&
		    !sctp_add_ftsn_set(&sets, fp, meta, &nsets, seglen)) {
			meta = NULL;
			sctp->sctp_adv_pap = adv_pap;
			goto ftsn_done;
		}
		while (mp1 != NULL && SCTP_CHUNK_ISSENT(mp1)) {
			sdc = (sctp_data_hdr_t *)mp1->b_rptr;
			adv_pap = ntohl(sdc->sdh_tsn);
			mp1 = mp1->b_next;
		}
		meta = meta->b_next;
		if (meta != NULL) {
			mp1 = SCTP_MSG_HEAD(meta);
			if (!SCTP_CHUNK_ISSENT(mp1))
				break;
			sdc  = (sctp_data_hdr_t *)mp1->b_rptr;
		}
	}
ftsn_done:
	/*
	 * Can't compare with sets == NULL, since we don't add any
	 * sets for un-ordered messages.
	 */
	if (meta == meta_head)
		return;
	*nmp = sctp_make_ftsn_chunk(sctp, fp, sets, nsets, *seglen);
	sctp_free_ftsn_set(sets);
	if (*nmp == NULL)
		return;
	if (sctp->sctp_ftsn == sctp->sctp_lastacked + 1) {
		sacklen = 0;
	} else {
		sacklen = sizeof (sctp_chunk_hdr_t) +
		    sizeof (sctp_sack_chunk_t) +
		    (sizeof (sctp_sack_frag_t) * sctp->sctp_sack_gaps);
		if (*seglen + sacklen > sctp->sctp_lastdata->sf_pmss) {
			/* piggybacked SACK doesn't fit */
			sacklen = 0;
		} else {
			fp = sctp->sctp_lastdata;
		}
	}
	head = sctp_add_proto_hdr(sctp, fp, *nmp, sacklen, NULL);
	if (head == NULL) {
		freemsg(*nmp);
		*nmp = NULL;
		SCTP_KSTAT(sctps, sctp_send_ftsn_failed);
		return;
	}
	*seglen += sacklen;
	*nmp = head;

	/*
	 * XXXNeed to optimise this, the reason it is done here is so
	 * that we don't have to undo in case of failure.
	 */
	mp1 = mp_head;
	sdc  = (sctp_data_hdr_t *)mp1->b_rptr;
	while (meta_head != NULL &&
	    SEQ_GEQ(sctp->sctp_adv_pap, ntohl(sdc->sdh_tsn))) {
		if (!SCTP_IS_MSG_ABANDONED(meta_head))
			SCTP_MSG_SET_ABANDONED(meta_head);
		while (mp1 != NULL && SCTP_CHUNK_ISSENT(mp1)) {
			sdc = (sctp_data_hdr_t *)mp1->b_rptr;
			if (!SCTP_CHUNK_ISACKED(mp1)) {
				clen = ntohs(sdc->sdh_len) - sizeof (*sdc);
				SCTP_CHUNK_SENT(sctp, mp1, sdc, fp, clen,
				    meta_head);
			}
			mp1 = mp1->b_next;
		}
		while (mp1 != NULL) {
			sdc = (sctp_data_hdr_t *)mp1->b_rptr;
			if (!SCTP_CHUNK_ABANDONED(mp1)) {
				ASSERT(!SCTP_CHUNK_ISSENT(mp1));
				unsent += ntohs(sdc->sdh_len) - sizeof (*sdc);
				SCTP_ABANDON_CHUNK(mp1);
			}
			mp1 = mp1->b_next;
		}
		meta_head = meta_head->b_next;
		if (meta_head != NULL) {
			mp1 = SCTP_MSG_HEAD(meta_head);
			if (!SCTP_CHUNK_ISSENT(mp1))
				break;
			sdc  = (sctp_data_hdr_t *)mp1->b_rptr;
		}
	}
	if (unsent > 0) {
		ASSERT(sctp->sctp_unsent >= unsent);
		sctp->sctp_unsent -= unsent;
		/*
		 * Update ULP the amount of queued data, which is
		 * sent-unack'ed + unsent.
		 */
		if (!SCTP_IS_DETACHED(sctp))
			SCTP_TXQ_UPDATE(sctp);
	}
}

/*
 * This function steps through messages starting at meta and checks if
 * the message is abandoned. It stops when it hits an unsent chunk or
 * a message that has all its chunk acked. This is the only place
 * where the sctp_adv_pap is moved forward to indicated abandoned
 * messages.
 */
void
sctp_check_adv_ack_pt(sctp_t *sctp, mblk_t *meta, mblk_t *mp)
{
	uint32_t	tsn = sctp->sctp_adv_pap;
	sctp_data_hdr_t	*sdc;
	sctp_msg_hdr_t	*msg_hdr;
	int64_t		now = ddi_get_lbolt64();

	ASSERT(mp != NULL);
	sdc = (sctp_data_hdr_t *)mp->b_rptr;
	ASSERT(SEQ_GT(ntohl(sdc->sdh_tsn), sctp->sctp_lastack_rxd));
	msg_hdr = (sctp_msg_hdr_t *)meta->b_rptr;
	if (!SCTP_IS_MSG_ABANDONED(meta) &&
	    !SCTP_MSG_TO_BE_ABANDONED(msg_hdr, sctp, now)) {
		return;
	}
	while (meta != NULL) {
		while (mp != NULL && SCTP_CHUNK_ISSENT(mp)) {
			sdc = (sctp_data_hdr_t *)mp->b_rptr;
			tsn = ntohl(sdc->sdh_tsn);
			mp = mp->b_next;
		}
		if (mp != NULL)
			break;
		/*
		 * We continue checking for successive messages only if there
		 * is a chunk marked for retransmission. Else, we might
		 * end up sending FTSN prematurely for chunks that have been
		 * sent, but not yet acked.
		 */
		if ((meta = meta->b_next) != NULL) {
			msg_hdr = (sctp_msg_hdr_t *)meta->b_rptr;
			if (!SCTP_IS_MSG_ABANDONED(meta) &&
			    !SCTP_MSG_TO_BE_ABANDONED(msg_hdr, sctp, now)) {
				break;
			}
			for (mp = msg_hdr->smh_xmit_head; mp != NULL;
			    mp = mp->b_next) {
				if (!SCTP_CHUNK_ISSENT(mp)) {
					sctp->sctp_adv_pap = tsn;
					return;
				}
				if (SCTP_CHUNK_WANT_REXMIT(mp))
					break;
			}
			if (mp == NULL)
				break;
		}
	}
	sctp->sctp_adv_pap = tsn;
}

/*
 * Retransmit first segment which hasn't been acked with cumtsn or send
 * a Forward TSN chunk, if appropriate.
 */
void
sctp_rexmit(sctp_t *sctp, sctp_faddr_t *oldfp)
{
	mblk_t		*mp;
	mblk_t		*nmp = NULL;
	mblk_t		*head, *tail;
	mblk_t		*meta = sctp->sctp_xmit_head;
	uint32_t	seglen = 0;
	uint32_t	sacklen;
	uint16_t	chunklen;
	int		extra;
	sctp_data_hdr_t	*sdc;
	sctp_faddr_t	*fp;
	uint32_t	adv_pap = sctp->sctp_adv_pap;
	boolean_t	do_ftsn = B_FALSE;
	boolean_t	ftsn_check = B_TRUE;
	uint32_t	first_ua_tsn;
	sctp_msg_hdr_t	*mhdr;
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	int64_t		now = ddi_get_lbolt64();
	ip_xmit_attr_t	*ixa;

	while (meta != NULL) {
		mhdr = (sctp_msg_hdr_t *)meta->b_rptr;
		for (mp = mhdr->smh_xmit_head; mp != NULL; mp = mp->b_next) {
			uint32_t	tsn;

			if (!SCTP_CHUNK_ISSENT(mp))
				goto window_probe;

			/*
			 * We break in the following cases -
			 *
			 *	if the advanced peer ack point includes the next
			 *	chunk to be retransmitted - possibly the Forward
			 * 	TSN was lost.
			 *
			 *	if we are PRSCTP aware and the next chunk to be
			 *	retransmitted is now abandoned
			 *
			 *	if the next chunk to be retransmitted is for
			 *	the dest on which the timer went off. (this
			 *	message is not abandoned).
			 *
			 * We check for Forward TSN only for the first
			 * eligible chunk to be retransmitted. The reason
			 * being if the first eligible chunk is skipped (say
			 * it was sent to a destination other than oldfp)
			 * then we cannot advance the cum TSN via Forward
			 * TSN chunk.
			 *
			 * Also, ftsn_check is B_TRUE only for the first
			 * eligible chunk, it  will be B_FALSE for all
			 * subsequent candidate messages for retransmission.
			 */
			sdc = (sctp_data_hdr_t *)mp->b_rptr;
			tsn = ntohl(sdc->sdh_tsn);
			if (SEQ_GT(tsn, sctp->sctp_lastack_rxd)) {
				if (sctp->sctp_prsctp_aware && ftsn_check) {
					if (SEQ_GEQ(sctp->sctp_adv_pap, tsn)) {
						ASSERT(sctp->sctp_prsctp_aware);
						do_ftsn = B_TRUE;
						goto out;
					} else {
						sctp_check_adv_ack_pt(sctp,
						    meta, mp);
						if (SEQ_GT(sctp->sctp_adv_pap,
						    adv_pap)) {
							do_ftsn = B_TRUE;
							goto out;
						}
					}
					ftsn_check = B_FALSE;
				}
				if (SCTP_CHUNK_DEST(mp) == oldfp)
					goto out;
			}
		}
		meta = meta->b_next;
		if (meta != NULL && sctp->sctp_prsctp_aware) {
			mhdr = (sctp_msg_hdr_t *)meta->b_rptr;

			while (meta != NULL && (SCTP_IS_MSG_ABANDONED(meta) ||
			    SCTP_MSG_TO_BE_ABANDONED(mhdr, sctp, now))) {
				meta = meta->b_next;
			}
		}
	}

window_probe:
	/*
	 * Retransmit fired for a destination which didn't have
	 * any unacked data pending.
	 */
	if (sctp->sctp_unacked == 0 && sctp->sctp_unsent != 0) {
		uint32_t old_frwnd;

		/* next TSN to send */
		sctp->sctp_rxt_nxttsn = sctp->sctp_ltsn;

		/*
		 * Set sctp_frwnd to sctp_unsent so that sctp_output() will
		 * send everything.  But since sctp_output() is called with
		 * num_pkt == 1, at most 1 packet will be sent.  This will
		 * start things rolling again.
		 */
		old_frwnd = sctp->sctp_frwnd;
		sctp->sctp_frwnd = sctp->sctp_unsent;
		sctp->sctp_zero_win_probe = B_TRUE;
		sctp_output(sctp, 1);
		sctp->sctp_frwnd = old_frwnd;

		/* Last sent TSN */
		sctp->sctp_rxt_maxtsn = sctp->sctp_ltsn - 1;
		ASSERT(sctp->sctp_rxt_maxtsn >= sctp->sctp_rxt_nxttsn);
		SCTPS_BUMP_MIB(sctps, sctpOutWinProbe);
	}
	return;
out:
	/*
	 * After a time out, assume that everything has left the network.  So
	 * we can clear rxt_unacked for the original peer address.
	 */
	oldfp->sf_rxt_unacked = 0;

	/*
	 * If we were probing for zero window, don't adjust retransmission
	 * variables, but the timer is still backed off.
	 */
	if (sctp->sctp_zero_win_probe) {
		mblk_t	*pkt;
		uint_t	pkt_len;

		/*
		 * The strikes will be cleared by sctp_faddr_alive() when the
		 * other side sends us an ack.
		 */
		oldfp->sf_strikes++;
		sctp->sctp_strikes++;

		/*
		 * If the old peer address is not alive, find an alternate
		 * one to send the probe.
		 */
		if (oldfp->sf_state != SCTP_FADDRS_ALIVE)
			fp = sctp_rotate_faddr(sctp, oldfp);
		else
			fp = oldfp;

		/*
		 * Get the Zero Win Probe for retransmission, sctp_rxt_nxttsn
		 * and sctp_rxt_maxtsn will specify the ZWP packet.
		 */
		pkt = sctp_rexmit_packet(sctp, &meta, &mp, fp, &pkt_len);
		if (pkt != NULL) {
			if (pkt_len > fp->sf_pmss) {
				ixa = sctp_big_pkt_ixa(mp, fp);
				if (ixa == NULL) {
					SCTP_KSTAT(sctps,
					    sctp_ss_rexmit_failed);
					goto after_zwin_sent;
				}
			} else {
				ixa = fp->sf_ixa;
			}
			sctp_set_iplen(sctp, pkt, ixa);
			(void) conn_ip_output(pkt, ixa);
			BUMP_LOCAL(sctp->sctp_opkts);
			if (ixa != fp->sf_ixa)
				ixa_refrele(ixa);
		} else {
			SCTP_KSTAT(sctps, sctp_ss_rexmit_failed);
		}
after_zwin_sent:
		SCTP_CALC_RXT(sctp, oldfp, sctp->sctp_rto_max);
		if (oldfp != fp && oldfp->sf_suna != 0) {
			SCTP_FADDR_TIMER_RESTART(sctp, oldfp, oldfp->sf_rto,
			    now);
		}
		SCTP_FADDR_TIMER_RESTART(sctp, fp, fp->sf_rto, now);
		SCTPS_BUMP_MIB(sctps, sctpOutWinProbe);
		return;
	}

	/*
	 * Enter slow start for this destination
	 */
	SCTP_CONG_RTO(oldfp);
	DTRACE_PROBE3(sctp_cwnd_update, sctp_faddr_t *, oldfp,
	    uint32_t, oldfp->sf_cwnd, int, TCPCONG_D_CWND);

	/*
	 * sctp_rotate_faddr() uses sf_strikes to pick the peer address
	 * with the lowest number of retransmission.
	 */
	oldfp->sf_strikes++;
	sctp->sctp_strikes++;

	/* Clear the update counter as the RTT calculation may be off. */
	oldfp->sf_rtt_updates = 0;

	/*
	 * Pick a peer address to do the retransmission and future
	 * data transmission according to the peer address policy in
	 * sctp_rotate_faddr().
	 */
	fp = sctp_rotate_faddr(sctp, oldfp);
	ASSERT(fp != NULL);
	if (fp != oldfp) {
		fp->sf_rtt_updates = 0;
		sctp_set_faddr_current(sctp, fp);
	}

	first_ua_tsn = ntohl(sdc->sdh_tsn);
	if (do_ftsn) {
		sctp_make_ftsns(sctp, meta, mp, &nmp, fp, &seglen);
		if (nmp == NULL) {
			sctp->sctp_adv_pap = adv_pap;
			goto restart_timer;
		}
		head = nmp;
		/*
		 * Move to the next unabandoned chunk. XXXCheck if meta will
		 * always be marked abandoned.
		 */
		while (meta != NULL && SCTP_IS_MSG_ABANDONED(meta))
			meta = meta->b_next;
		if (meta != NULL) {
			mhdr = (sctp_msg_hdr_t *)meta->b_rptr;
			mp = mhdr->smh_xmit_head;
		} else {
			mp = NULL;
		}
		goto try_bundle;
	}

	seglen = ntohs(sdc->sdh_len);
	chunklen = seglen - sizeof (*sdc);
	extra = SCTP_PAD_LEN(seglen);

	/* Find out if we need to piggyback SACK. */
	if (sctp->sctp_ftsn == sctp->sctp_lastacked + 1) {
		sacklen = 0;
	} else {
		sacklen = sizeof (sctp_chunk_hdr_t) +
		    sizeof (sctp_sack_chunk_t) +
		    (sizeof (sctp_sack_frag_t) * sctp->sctp_sack_gaps);
		if (seglen + sacklen > sctp->sctp_lastdata->sf_pmss) {
			/* piggybacked SACK doesn't fit */
			sacklen = 0;
		} else {
			/*
			 * OK, we have room to send SACK back.  But we
			 * should send it back to the last fp where we
			 * receive data from, unless sctp_lastdata equals
			 * oldfp, then we should probably not send it
			 * back to that fp.  Also we should check that
			 * the fp is alive.
			 */
			if (sctp->sctp_lastdata != oldfp &&
			    sctp->sctp_lastdata->sf_state ==
			    SCTP_FADDRS_ALIVE) {
				fp = sctp->sctp_lastdata;
			}
		}
	}

	/*
	 * Cancel RTT measurement if the retransmitted TSN is before the
	 * TSN used for timimg.
	 */
	if (sctp->sctp_out_time != 0 &&
	    SEQ_GEQ(sctp->sctp_rtt_tsn, sdc->sdh_tsn)) {
		sctp->sctp_out_time = 0;
	}

	nmp = dupmsg(mp);
	if (nmp == NULL)
		goto restart_timer;
	tail = NULL;
	if (!sctp_add_padding(sctp, nmp, extra, &tail)) {
		freemsg(nmp);
		goto restart_timer;
	}
	seglen += extra;
	SCTP_CHUNK_CLEAR_FLAGS(nmp);
	head = sctp_add_proto_hdr(sctp, fp, nmp, sacklen, NULL);
	if (head == NULL) {
		freemsg(nmp);
		SCTP_KSTAT(sctps, sctp_rexmit_failed);
		goto restart_timer;
	}
	seglen += sacklen;

	SCTP_CHUNK_SENT(sctp, mp, sdc, fp, chunklen, meta);

	DTRACE_PROBE5(sctp_rexmit_1, sctp_t *, sctp, sctp_faddr_t *, oldfp,
	    sctp_faddr_t *, fp, uint32_t, ntohl(sdc->sdh_tsn),
	    uint16_t, ntohs(sdc->sdh_ssn));

	/* Check if there are more chunks which can be bundled. */
	mp = mp->b_next;

try_bundle:
	/*
	 * We can at least and at most send 1 packet at timeout (congestion
	 * control).  We may bundle new chunks in this packet if the peer's
	 * receive window allows.
	 */
	while (seglen < fp->sf_pmss) {
		int32_t new_len;

		/*
		 * Go through the list to find more chunks to be bundled.  A
		 * chunk can be bundled if
		 *
		 * - the chunk is sent to the same destination and unack'ed.
		 * OR
		 * - the chunk is unsent, i.e. new data, and sctp_frwnd allows
		 *   it to be sent.
		 */
		for (; mp != NULL; mp = mp->b_next) {
			if (SCTP_CHUNK_ABANDONED(mp))
				continue;
			sdc = (sctp_data_hdr_t *)mp->b_rptr;
			new_len = ntohs(sdc->sdh_len);
			chunklen = new_len - sizeof (*sdc);
			if (SCTP_CHUNK_ISSENT(mp)) {
				if (SCTP_CHUNK_DEST(mp) != oldfp ||
				    SCTP_CHUNK_ISACKED(mp)) {
					continue;
				}
			} else if (sctp->sctp_frwnd < chunklen) {
				/*
				 * This chunk has never been sent, check peer's
				 * rwnd and see if we can send it.  If not,
				 * exit the bundle loop.
				 */
				goto done_bundle;
			}
			break;
		}

		/* Go to the next message. */
		if (mp == NULL) {
			for (meta = meta->b_next; meta != NULL;
			    meta = meta->b_next) {
				mhdr = (sctp_msg_hdr_t *)meta->b_rptr;

				if (SCTP_IS_MSG_ABANDONED(meta) ||
				    SCTP_MSG_TO_BE_ABANDONED(mhdr, sctp, now)) {
					continue;
				}
				mp = mhdr->smh_xmit_head;
				/*
				 * If the next chunk has never been sent, check
				 * if the chunk should be sent to a specific
				 * peer.  If it should and the peer is not fp,
				 * exit the bundle loop.
				 */
				if (!SCTP_CHUNK_ISSENT(mp) &&
				    SCTP_CHUNK_DEST(meta) != NULL &&
				    SCTP_CHUNK_DEST(meta) != fp) {
					goto done_bundle;
				}
				goto try_bundle;
			}
			/* No more unsent message. */
			break;
		}

		extra = SCTP_PAD_LEN(new_len);
		if ((new_len = seglen + new_len + extra) > fp->sf_pmss)
			break;
		if ((nmp = dupmsg(mp)) == NULL)
			break;
		if (!sctp_add_padding(sctp, nmp, extra, &tail)) {
			freemsg(nmp);
			break;
		}

		if (!SCTP_CHUNK_ISSENT(mp)) {
			mhdr->smh_xmit_last_sent = mp;
			sctp->sctp_xmit_last_sent = meta;
		}
		SCTP_CHUNK_CLEAR_FLAGS(nmp);
		SCTP_CHUNK_SENT(sctp, mp, sdc, fp, chunklen, meta);

		DTRACE_PROBE5(sctp_rexmit_2, sctp_t *, sctp, sctp_faddr_t *,
		    oldfp, sctp_faddr_t *, fp, uint32_t, ntohl(sdc->sdh_tsn),
		    uint16_t, ntohs(sdc->sdh_ssn));

		seglen = new_len;
		mp = mp->b_next;
	}

done_bundle:
	if (seglen > fp->sf_pmss) {
		/*
		 * Path MTU is different from path we thought it would
		 * be when we created chunks, or IP headers have grown.
		 * Need to clear the DF bit.
		 *
		 * If ixa is NULL, we keep going and just treat this case
		 * as the retransmitted packet is dropped.
		 */
		ixa = sctp_big_pkt_ixa(head, fp);
	} else {
		ixa = fp->sf_ixa;
	}

	fp->sf_rxt_unacked += seglen;

	sctp->sctp_rexmitting = B_TRUE;
	sctp->sctp_rxt_nxttsn = first_ua_tsn;
	sctp->sctp_rxt_maxtsn = sctp->sctp_ltsn - 1;
	if (ixa != NULL) {
		sctp_set_iplen(sctp, head, ixa);
		(void) conn_ip_output(head, ixa);
		if (ixa != fp->sf_ixa)
			ixa_refrele(ixa);
	}
	BUMP_LOCAL(sctp->sctp_opkts);

	/*
	 * Restart the oldfp timer with exponential backoff and
	 * the new fp timer for the retransmitted chunks.
	 */
restart_timer:
	SCTP_CALC_RXT(sctp, oldfp, sctp->sctp_rto_max);
	/*
	 * If there is still some data in the oldfp, restart the
	 * retransmission timer.  If there is no data, the heartbeat will
	 * continue to run so it will do its job in checking the reachability
	 * of the oldfp.
	 */
	if (oldfp != fp && oldfp->sf_suna != 0)
		SCTP_FADDR_TIMER_RESTART(sctp, oldfp, oldfp->sf_rto, now);

	/*
	 * Should we restart the timer of the new fp?  If there is
	 * outstanding data to the new fp, the timer should be
	 * running already.  So restarting it means that the timer
	 * will fire later for those outstanding data.  But if
	 * we don't restart it, the timer will fire too early for the
	 * just retransmitted chunks to the new fp.  The reason is that we
	 * don't keep a timestamp on when a chunk is retransmitted.
	 * So when the timer fires, it will just search for the
	 * chunk with the earliest TSN sent to new fp.  This probably
	 * is the chunk we just retransmitted.  So for now, let's
	 * be conservative and restart the timer of the new fp.
	 */
	SCTP_FADDR_TIMER_RESTART(sctp, fp, fp->sf_rto, now);

	sctp->sctp_active = fp->sf_lastactive = now;
}

/*
 * This function is called by sctp_ss_rexmit() to create a packet
 * to be retransmitted to the given fp.  The given meta and mp
 * parameters are respectively the sctp_msg_hdr_t and the mblk of the
 * first chunk to be retransmitted.  This is also called when we want
 * to retransmit a zero window probe from sctp_rexmit() or when we
 * want to retransmit the zero window probe after the window has
 * opened from sctp_got_sack().
 */
mblk_t *
sctp_rexmit_packet(sctp_t *sctp, mblk_t **meta, mblk_t **mp, sctp_faddr_t *fp,
    uint_t *packet_len)
{
	uint32_t	seglen = 0;
	uint16_t	chunklen;
	int		extra;
	mblk_t		*nmp;
	mblk_t		*head, *tail;
	sctp_data_hdr_t	*sdc;
	sctp_msg_hdr_t	*mhdr;
	int64_t		now = ddi_get_lbolt64();

	ASSERT(SCTP_CHUNK_ISSENT(*mp));
	sdc = (sctp_data_hdr_t *)(*mp)->b_rptr;
	seglen = ntohs(sdc->sdh_len);
	chunklen = seglen - sizeof (*sdc);
	extra = SCTP_PAD_LEN(seglen);

	nmp = dupmsg(*mp);
	if (nmp == NULL)
		return (NULL);
	tail = NULL;
	if (!sctp_add_padding(sctp, nmp, extra, &tail)) {
		freemsg(nmp);
		return (NULL);
	}
	seglen += extra;

	SCTP_CHUNK_CLEAR_FLAGS(nmp);
	head = sctp_add_proto_hdr(sctp, fp, nmp, 0, NULL);
	if (head == NULL) {
		freemsg(nmp);
		return (NULL);
	}
	SCTP_CHUNK_SENT(sctp, *mp, sdc, fp, chunklen, *meta);

	/*
	 * Don't update the TSN if we are doing a Zero Win Probe.
	 */
	if (!sctp->sctp_zero_win_probe)
		sctp->sctp_rxt_nxttsn = ntohl(sdc->sdh_tsn);
	*mp = (*mp)->b_next;

try_bundle:
	while (seglen < fp->sf_pmss) {
		int32_t new_len;

		/*
		 * Go through the list to find more chunks to be bundled.
		 * We should only retransmit sent by unack'ed chunks.  Since
		 * they were sent before, the peer's receive window should
		 * be able to receive them.
		 */
		while (*mp != NULL) {
			/* Check if the chunk can be bundled. */
			if (SCTP_CHUNK_ISSENT(*mp) && !SCTP_CHUNK_ISACKED(*mp))
				break;
			*mp = (*mp)->b_next;
		}
		/* Go to the next message. */
		if (*mp == NULL) {
			if (*meta == sctp->sctp_xmit_last_sent)
				break;
			for (*meta = (*meta)->b_next; *meta != NULL &&
			    *meta != sctp->sctp_xmit_last_sent;
			    *meta = (*meta)->b_next) {
				mhdr = (sctp_msg_hdr_t *)(*meta)->b_rptr;

				if (SCTP_IS_MSG_ABANDONED(*meta) ||
				    SCTP_MSG_TO_BE_ABANDONED(mhdr, sctp, now)) {
					continue;
				}

				*mp = mhdr->smh_xmit_head;
				goto try_bundle;
			}
			/* No more chunk to be bundled. */
			break;
		}

		ASSERT(SCTP_CHUNK_ISSENT(*mp));
		sdc = (sctp_data_hdr_t *)(*mp)->b_rptr;
		/* Don't bundle chunks beyond sctp_rxt_maxtsn. */
		if (SEQ_GT(ntohl(sdc->sdh_tsn), sctp->sctp_rxt_maxtsn))
			break;
		new_len = ntohs(sdc->sdh_len);
		chunklen = new_len - sizeof (*sdc);

		extra = SCTP_PAD_LEN(new_len);
		if ((new_len = seglen + new_len + extra) > fp->sf_pmss)
			break;
		if ((nmp = dupmsg(*mp)) == NULL)
			break;
		if (!sctp_add_padding(sctp, nmp, extra, &tail)) {
			freemsg(nmp);
			break;
		}

		SCTP_CHUNK_CLEAR_FLAGS(nmp);
		SCTP_CHUNK_SENT(sctp, *mp, sdc, fp, chunklen, *meta);
		/*
		 * Don't update the TSN if we are doing a Zero Win Probe.
		 */
		if (!sctp->sctp_zero_win_probe)
			sctp->sctp_rxt_nxttsn = ntohl(sdc->sdh_tsn);

		seglen = new_len;
		*mp = (*mp)->b_next;
	}
	*packet_len = seglen;
	fp->sf_rxt_unacked += seglen;
	return (head);
}

/*
 * sctp_ss_rexmit() is called when we get a SACK after a timeout which
 * advances the cum_tsn but the cum_tsn is still less than what we have sent
 * (sctp_rxt_maxtsn) at the time of the timeout.  This SACK is a "partial"
 * SACK.  We retransmit unacked chunks without having to wait for another
 * timeout.  The rationale is that the SACK should not be "partial" if all the
 * lost chunks have been retransmitted.  Since the SACK is "partial,"
 * the chunks between the cum_tsn and the sctp_rxt_maxtsn should still
 * be missing.  It is better for us to retransmit them now instead
 * of waiting for a timeout.
 */
void
sctp_ss_rexmit(sctp_t *sctp)
{
	mblk_t		*meta;
	mblk_t		*mp;
	mblk_t		*pkt;
	sctp_faddr_t	*fp;
	uint_t		pkt_len;
	uint32_t	tot_wnd;
	sctp_data_hdr_t	*sdc;
	int		burst;
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	int64_t		now;
	sctp_msg_hdr_t	*mhdr;
	ip_xmit_attr_t	*ixa;

	ASSERT(!sctp->sctp_zero_win_probe);

	/*
	 * If the last cum ack is smaller than what we have just
	 * retransmitted, simply return.
	 */
	if (SEQ_GEQ(sctp->sctp_lastack_rxd, sctp->sctp_rxt_nxttsn))
		sctp->sctp_rxt_nxttsn = sctp->sctp_lastack_rxd + 1;
	else
		return;
	ASSERT(SEQ_LEQ(sctp->sctp_rxt_nxttsn, sctp->sctp_rxt_maxtsn));

	/*
	 * After a timer fires, sctp_current should be set to the new
	 * fp where the retransmitted chunks are sent.
	 */
	fp = sctp->sctp_current;

	/*
	 * Since we are retransmitting, we only need to use cwnd to determine
	 * how much we can send as we were allowed (by peer's receive window)
	 * to send those retransmitted chunks previously when they are first
	 * sent.  If we record how much we have retransmitted but
	 * unacknowledged using rxt_unacked, then the amount we can now send
	 * is equal to cwnd minus rxt_unacked.
	 *
	 * The field rxt_unacked is incremented when we retransmit a packet
	 * and decremented when we got a SACK acknowledging something.  And
	 * it is reset when the retransmission timer fires as we assume that
	 * all packets have left the network after a timeout.  If this
	 * assumption is not true, it means that after a timeout, we can
	 * get a SACK acknowledging more than rxt_unacked (its value only
	 * contains what is retransmitted when the timer fires).  So
	 * rxt_unacked will become very big (it is an unsiged int so going
	 * negative means that the value is huge).  This is the reason we
	 * always send at least 1 MSS bytes.
	 *
	 * The reason why we do not have an accurate count is that we
	 * only know how many packets are outstanding (using the TSN numbers).
	 * But we do not know how many bytes those packets contain.  To
	 * have an accurate count, we need to walk through the send list.
	 * As it is not really important to have an accurate count during
	 * retransmission, we skip this walk to save some time.  This should
	 * not make the retransmission too aggressive to cause congestion.
	 */
	if (fp->sf_cwnd <= fp->sf_rxt_unacked)
		tot_wnd = fp->sf_pmss;
	else
		tot_wnd = fp->sf_cwnd - fp->sf_rxt_unacked;

	now = LBOLT_FASTPATH64;

	/* Find the first unack'ed chunk */
	for (meta = sctp->sctp_xmit_head; meta != NULL; meta = meta->b_next) {
		mhdr = (sctp_msg_hdr_t *)meta->b_rptr;

		if (SCTP_IS_MSG_ABANDONED(meta) ||
		    SCTP_MSG_TO_BE_ABANDONED(mhdr, sctp, now)) {
			continue;
		}

		for (mp = mhdr->smh_xmit_head; mp != NULL; mp = mp->b_next) {
			/* Again, this may not be possible */
			if (!SCTP_CHUNK_ISSENT(mp))
				return;
			sdc = (sctp_data_hdr_t *)mp->b_rptr;
			if (ntohl(sdc->sdh_tsn) == sctp->sctp_rxt_nxttsn)
				goto found_msg;
		}
	}

	/* Everything is abandoned... */
	return;

found_msg:
	if (!fp->sf_timer_running)
		SCTP_FADDR_TIMER_RESTART(sctp, fp, fp->sf_rto, now);
	pkt = sctp_rexmit_packet(sctp, &meta, &mp, fp, &pkt_len);
	if (pkt == NULL) {
		SCTP_KSTAT(sctps, sctp_ss_rexmit_failed);
		return;
	}
	if (pkt_len > fp->sf_pmss) {
		ixa = sctp_big_pkt_ixa(pkt, fp);
		if (ixa == NULL) {
			SCTP_KSTAT(sctps, sctp_ss_rexmit_failed);
			return;
		}
	} else {
		ixa = fp->sf_ixa;
	}
	sctp_set_iplen(sctp, pkt, ixa);
	(void) conn_ip_output(pkt, ixa);
	BUMP_LOCAL(sctp->sctp_opkts);
	if (ixa != fp->sf_ixa)
		ixa_refrele(ixa);

	/* Check and see if there is more chunk to be retransmitted. */
	if (tot_wnd <= pkt_len || tot_wnd - pkt_len < fp->sf_pmss ||
	    meta == NULL)
		return;
	if (mp == NULL)
		meta = meta->b_next;
	if (meta == NULL)
		return;

	/* Retransmit another packet if the window allows. */
	for (tot_wnd -= pkt_len, burst = sctp->sctp_max_burst - 1;
	    meta != NULL && burst > 0; meta = meta->b_next, burst--) {
		if (mp == NULL) {
			mhdr = (sctp_msg_hdr_t *)meta->b_rptr;
			mp = mhdr->smh_xmit_head;
		}
		for (; mp != NULL; mp = mp->b_next) {
			/* Again, this may not be possible */
			if (!SCTP_CHUNK_ISSENT(mp))
				return;
			if (!SCTP_CHUNK_ISACKED(mp))
				goto found_msg;
		}
	}
}

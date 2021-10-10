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
#include <sys/strsubr.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/kmem.h>
#include <sys/socket.h>
#include <sys/random.h>
#include <sys/tsol/tndb.h>
#include <sys/tsol/tnet.h>

#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/sctp.h>

#include <inet/common.h>
#include <inet/ip.h>
#include <inet/ip6.h>
#include <inet/ip_ire.h>
#include <inet/ip_if.h>
#include <inet/ip_ndp.h>
#include <inet/mib2.h>
#include <inet/nd.h>
#include <inet/optcom.h>
#include <inet/sctp_ip.h>
#include <inet/ipclassifier.h>

#include "sctp_impl.h"
#include "sctp_addr.h"
#include "sctp_asconf.h"

static struct kmem_cache *sctp_kmem_faddr_cache;

static void	sctp_init_faddr(sctp_t *, sctp_t *, sctp_faddr_t *,
		    in6_addr_t *, mblk_t *);

/* Set the source address.  Refer to comments in sctp_get_dest(). */
void
sctp_set_saddr(sctp_t *sctp, sctp_faddr_t *fp)
{
	boolean_t v6 = !fp->sf_isv4;
	boolean_t addr_set;

	fp->sf_saddr = sctp_get_valid_addr(sctp, v6, &addr_set);
	/*
	 * If there is no source address avaialble, mark this peer address
	 * as unreachable for now.  When the heartbeat timer fires, it will
	 * call sctp_get_dest() to re-check if there is any source address
	 * available.
	 */
	if (!addr_set)
		fp->sf_state = SCTP_FADDRS_UNREACH;
}

/*
 * Find the minimum of all peer addresses' path MSS.
 */
void
sctp_min_fp_mss(sctp_t *sctp)
{
	sctp_faddr_t *fp;
	uint32_t mss;

	mss = IP_MAXPACKET;
	for (fp = sctp->sctp_faddrs; fp != NULL; fp = fp->sf_next) {
		if (fp->sf_pmss < mss)
			mss = fp->sf_pmss;
	}
	sctp->sctp_mss = mss;
}

/*
 * Call this function to get information about a peer addr fp.
 *
 * Uses ip_attr_connect to avoid explicit use of ire and source address
 * selection.
 */
void
sctp_get_dest(sctp_t *sctp, sctp_faddr_t *fp)
{
	in6_addr_t	laddr;
	in6_addr_t	nexthop;
	sctp_saddr_ipif_t *sp;
	int		hdrlen;
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	conn_t		*connp = sctp->sctp_connp;
	iulp_t		uinfo;
	uint_t		pmtu;
	int		error;
	uint32_t	flags = IPDF_VERIFY_DST | IPDF_IPSEC |
	    IPDF_SELECT_SRC | IPDF_UNIQUE_DCE;

	/*
	 * Tell sctp_make_mp it needs to call us again should we not
	 * complete and set the saddr.
	 */
	fp->sf_saddr = ipv6_all_zeros;

	/*
	 * If this addr is not reachable, mark it as unconfirmed for now, the
	 * state will be changed back to unreachable later in this function
	 * if it is still the case.
	 */
	if (fp->sf_state == SCTP_FADDRS_UNREACH) {
		fp->sf_state = SCTP_FADDRS_UNCONFIRMED;
	}

	/*
	 * Socket is connected - enable PMTU discovery.
	 */
	if (!sctps->sctps_ignore_path_mtu)
		fp->sf_ixa->ixa_flags |= IXAF_PMTU_DISCOVERY;

	ip_attr_nexthop(&connp->conn_xmit_ipp, fp->sf_ixa, &fp->sf_faddr,
	    &nexthop);

	laddr = fp->sf_saddr;
	error = ip_attr_connect(connp, fp->sf_ixa, &laddr, &fp->sf_faddr,
	    &nexthop, connp->conn_fport, &laddr, &uinfo, flags);

	if (error != 0) {
		dprint(3, ("sctp_get_dest: no ire for %x:%x:%x:%x\n",
		    SCTP_PRINTADDR(fp->sf_faddr)));
		/*
		 * It is tempting to just leave the src addr
		 * unspecified and let IP figure it out, but we
		 * *cannot* do this, since IP may choose a src addr
		 * that is not part of this association... unless
		 * this sctp has bound to all addrs.  So if the dest
		 * lookup fails, try to find one in our src addr
		 * list, unless the sctp has bound to all addrs, in
		 * which case we change the src addr to unspec.
		 *
		 * Note that if this is a v6 endpoint but it does
		 * not have any v4 address at this point (e.g. may
		 * have been  deleted), sctp_get_valid_addr() will
		 * return mapped INADDR_ANY.  In this case, this
		 * address should be marked not reachable so that
		 * it won't be used to send data.
		 */
		sctp_set_saddr(sctp, fp);
		if (fp->sf_state == SCTP_FADDRS_UNREACH)
			return;
		goto check_current;
	}
	ASSERT(fp->sf_ixa->ixa_ire != NULL);
	ASSERT(!(fp->sf_ixa->ixa_ire->ire_flags & (RTF_REJECT|RTF_BLACKHOLE)));

	if (!sctp->sctp_loopback)
		sctp->sctp_loopback = uinfo.iulp_loopback;

	/* Make sure the laddr is part of this association */
	if ((sp = sctp_saddr_lookup(sctp, &laddr, 0)) != NULL &&
	    !sp->saddr_ipif_dontsrc) {
		if (sp->saddr_ipif_unconfirmed == 1)
			sp->saddr_ipif_unconfirmed = 0;
		/* We did IPsec policy lookup for laddr already */
		fp->sf_saddr = laddr;
	} else {
		dprint(2, ("sctp_get_dest: src addr is not part of assoc "
		    "%x:%x:%x:%x\n", SCTP_PRINTADDR(laddr)));

		/*
		 * Set the src to the first saddr and hope for the best.
		 * Note that this case should very seldomly
		 * happen.  One scenario this can happen is an app
		 * explicitly bind() to an address.  But that address is
		 * not the preferred source address to send to the peer.
		 */
		sctp_set_saddr(sctp, fp);
		if (fp->sf_state == SCTP_FADDRS_UNREACH) {
			return;
		}
	}
	/*
	 * Pull out RTO information for this faddr and use it if we don't
	 * have any yet.
	 */
	if (fp->sf_srtt == -1 && uinfo.iulp_rtt != 0) {
		/* The cached value is in ms. */
		fp->sf_srtt = uinfo.iulp_rtt;
		fp->sf_rttvar = uinfo.iulp_rtt_sd;
		fp->sf_rto = 3 * MSEC_TO_TICK(fp->sf_srtt);

		/* Bound the RTO by configured min and max values */
		if (fp->sf_rto < sctp->sctp_rto_min) {
			fp->sf_rto = sctp->sctp_rto_min;
		}
		if (fp->sf_rto > sctp->sctp_rto_max) {
			fp->sf_rto = sctp->sctp_rto_max;
		}
		SCTP_MAX_RTO(sctp, fp);
	}
	pmtu = uinfo.iulp_mtu;

	/*
	 * Record the MTU for this faddr. If the MTU for this faddr has
	 * changed, check if the assc MTU will also change.
	 */
	if (fp->sf_isv4) {
		hdrlen = sctp->sctp_hdr_len;
	} else {
		hdrlen = sctp->sctp_hdr6_len;
	}
	if ((fp->sf_pmss + hdrlen) != pmtu) {
		uint32_t old_mss;

		old_mss = fp->sf_pmss;
		/* Make sure that sf_pmss is a multiple of SCTP_ALIGN. */
		fp->sf_pmss = (pmtu - hdrlen) & ~(SCTP_ALIGN - 1);
		if (fp->sf_cwnd < (fp->sf_pmss * 2)) {
			SET_CWND(fp, fp->sf_pmss,
			    sctps->sctps_slow_start_initial);
			DTRACE_PROBE3(sctp_cwnd_update,
			    sctp_faddr_t *, fp, uint32_t, fp->sf_cwnd,
			    int, TCPCONG_D_CWND_AFTER_IDLE);
		}

		/*
		 * If the path MSS becomes bigger, sctp_mss may also increase.
		 * Call sctp_min_fp_mss() to set that.  If it becomes smaller,
		 * compare with the current sctp_mss to see if it needs to be
		 * reduced.
		 */
		if (fp->sf_pmss != old_mss) {
			if (fp->sf_pmss > old_mss)
				sctp_min_fp_mss(sctp);
			else if (fp->sf_pmss < sctp->sctp_mss)
				sctp->sctp_mss = fp->sf_pmss;

			/* Update ULP about the changed MSS */
			if (!SCTP_IS_DETACHED(sctp) &&
			    sctp->sctp_current != NULL) {
				sctp_set_ulp_prop(sctp);
			}
		}
	}
	fp->sf_naglim = MIN(fp->sf_pmss - sizeof (sctp_data_hdr_t),
	    sctps->sctps_naglim_def);

check_current:
	if (fp == sctp->sctp_current)
		sctp_set_faddr_current(sctp, fp);
}

void
sctp_update_dce(sctp_t *sctp)
{
	sctp_faddr_t	*fp;
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	iulp_t		uinfo;
	ip_stack_t	*ipst = sctps->sctps_netstack->netstack_ip;
	uint_t		ifindex;

	for (fp = sctp->sctp_faddrs; fp != NULL; fp = fp->sf_next) {
		bzero(&uinfo, sizeof (uinfo));
		/*
		 * Only record the PMTU for this faddr if we actually have
		 * done discovery. This prevents initialized default from
		 * clobbering any real info that IP may have.
		 */
		if (fp->sf_pmtu_discovered) {
			if (fp->sf_isv4) {
				uinfo.iulp_mtu = fp->sf_pmss +
				    sctp->sctp_hdr_len;
			} else {
				uinfo.iulp_mtu = fp->sf_pmss +
				    sctp->sctp_hdr6_len;
			}
		}
		if (sctps->sctps_rtt_updates != 0 &&
		    fp->sf_rtt_updates >= sctps->sctps_rtt_updates) {
			/*
			 * dce_update_uinfo() merges these values with the
			 * old values.
			 */
			uinfo.iulp_rtt = fp->sf_srtt;
			uinfo.iulp_rtt_sd = fp->sf_rttvar;
			fp->sf_rtt_updates = 0;
		}
		ifindex = 0;
		if (IN6_IS_ADDR_LINKSCOPE(&fp->sf_faddr)) {
			/*
			 * If we are going to create a DCE we'd better have
			 * an ifindex
			 */
			if (fp->sf_ixa->ixa_nce != NULL) {
				ifindex = fp->sf_ixa->ixa_nce->nce_common->
				    ncec_ill->ill_phyint->phyint_ifindex;
			} else {
				continue;
			}
		}

		(void) dce_update_uinfo(&fp->sf_faddr, ifindex, &uinfo, ipst);
	}
}

/*
 * The sender must later set the total length in the IP header.
 */
mblk_t *
sctp_make_mp(sctp_t *sctp, sctp_faddr_t *fp, int trailer)
{
	mblk_t *mp;
	size_t ipsctplen;
	int isv4;
	sctp_stack_t *sctps = sctp->sctp_sctps;
	boolean_t src_changed = B_FALSE;

	ASSERT(fp != NULL);
	isv4 = fp->sf_isv4;

	if (SCTP_IS_ADDR_UNSPEC(isv4, fp->sf_saddr) ||
	    (fp->sf_ixa->ixa_ire->ire_flags & (RTF_REJECT|RTF_BLACKHOLE))) {
		/* Need to pick a source */
		sctp_get_dest(sctp, fp);
		/*
		 * Although we still may not get an IRE, the source address
		 * may be changed in sctp_get_ire().  Set src_changed to
		 * true so that the source address is copied again.
		 */
		src_changed = B_TRUE;
	}

	/* There is no suitable source address to use, return. */
	if (fp->sf_state == SCTP_FADDRS_UNREACH)
		return (NULL);

	ASSERT(fp->sf_ixa->ixa_ire != NULL);
	ASSERT(!SCTP_IS_ADDR_UNSPEC(isv4, fp->sf_saddr));

	if (isv4) {
		ipsctplen = sctp->sctp_hdr_len;
	} else {
		ipsctplen = sctp->sctp_hdr6_len;
	}

	mp = allocb(ipsctplen + sctps->sctps_wroff_xtra + trailer, BPRI_MED);
	if (mp == NULL) {
		ip1dbg(("sctp_make_mp: error making mp..\n"));
		return (NULL);
	}
	mp->b_rptr += sctps->sctps_wroff_xtra;
	mp->b_wptr = mp->b_rptr + ipsctplen;

	ASSERT(OK_32PTR(mp->b_wptr));

	if (isv4) {
		ipha_t *iph = (ipha_t *)mp->b_rptr;

		bcopy(sctp->sctp_iphc, mp->b_rptr, ipsctplen);
		if (fp != sctp->sctp_current || src_changed) {
			/* Fix the source and destination addresses. */
			IN6_V4MAPPED_TO_IPADDR(&fp->sf_faddr, iph->ipha_dst);
			IN6_V4MAPPED_TO_IPADDR(&fp->sf_saddr, iph->ipha_src);
		}
		/* set or clear the don't fragment bit */
		if (fp->sf_df) {
			iph->ipha_fragment_offset_and_flags = htons(IPH_DF);
		} else {
			iph->ipha_fragment_offset_and_flags = 0;
		}
	} else {
		bcopy(sctp->sctp_iphc6, mp->b_rptr, ipsctplen);
		if (fp != sctp->sctp_current || src_changed) {
			/* Fix the source and destination addresses. */
			((ip6_t *)(mp->b_rptr))->ip6_dst = fp->sf_faddr;
			((ip6_t *)(mp->b_rptr))->ip6_src = fp->sf_saddr;
		}
	}
	ASSERT(sctp->sctp_connp != NULL);
	return (mp);
}

/*
 * Notify upper layers about preferred write offset, write size.
 */
void
sctp_set_ulp_prop(sctp_t *sctp)
{
	int hdrlen;
	struct sock_proto_props sopp;
	sctp_stack_t *sctps = sctp->sctp_sctps;

	ASSERT(sctp->sctp_ulpd);

	if (sctp->sctp_current->sf_isv4) {
		hdrlen = sctp->sctp_hdr_len;
	} else {
		hdrlen = sctp->sctp_hdr6_len;
	}

	/*
	 * Make sure that we have enough head room to hold the IP/SCTP header
	 * and one chunk header, plus the default write offset.
	 */
	sctp->sctp_connp->conn_wroff = sctps->sctps_wroff_xtra + hdrlen +
	    sizeof (sctp_data_hdr_t);

	bzero(&sopp, sizeof (sopp));
	sopp.sopp_flags = SOCKOPT_MAXBLK|SOCKOPT_WROFF;
	sopp.sopp_wroff = sctp->sctp_connp->conn_wroff;
	sopp.sopp_maxblk = sctp->sctp_mss - sizeof (sctp_data_hdr_t);
	sctp->sctp_ulp_prop(sctp->sctp_ulpd, &sopp);
}

/*
 * Set the lengths in the packet and the transmit attributes.
 */
void
sctp_set_iplen(sctp_t *sctp, mblk_t *mp, ip_xmit_attr_t *ixa)
{
	uint16_t	sum = 0;
	ipha_t		*iph;
	ip6_t		*ip6h;
	mblk_t		*pmp = mp;
	boolean_t	isv4;

	isv4 = (IPH_HDR_VERSION(mp->b_rptr) == IPV4_VERSION);
	for (; pmp; pmp = pmp->b_cont)
		sum += pmp->b_wptr - pmp->b_rptr;

	ixa->ixa_pktlen = sum;
	if (isv4) {
		iph = (ipha_t *)mp->b_rptr;
		iph->ipha_length = htons(sum);
		ixa->ixa_ip_hdr_length = sctp->sctp_ip_hdr_len;
	} else {
		ip6h = (ip6_t *)mp->b_rptr;
		ip6h->ip6_plen = htons(sum - IPV6_HDR_LEN);
		ixa->ixa_ip_hdr_length = sctp->sctp_ip_hdr6_len;
	}
}

int
sctp_compare_faddrsets(sctp_faddr_t *a1, sctp_faddr_t *a2)
{
	int na1 = 0;
	int overlap = 0;
	int equal = 1;
	int onematch;
	sctp_faddr_t *fp1, *fp2;

	for (fp1 = a1; fp1; fp1 = fp1->sf_next) {
		onematch = 0;
		for (fp2 = a2; fp2; fp2 = fp2->sf_next) {
			if (IN6_ARE_ADDR_EQUAL(&fp1->sf_faddr,
			    &fp2->sf_faddr)) {
				overlap++;
				onematch = 1;
				break;
			}
			if (!onematch) {
				equal = 0;
			}
		}
		na1++;
	}

	if (equal) {
		return (SCTP_ADDR_EQUAL);
	}
	if (overlap == na1) {
		return (SCTP_ADDR_SUBSET);
	}
	if (overlap) {
		return (SCTP_ADDR_OVERLAP);
	}
	return (SCTP_ADDR_DISJOINT);
}

/*
 * Remove peer addresses added by connectx() but are not included in the
 * peer address parameter list.
 */
static void
sctp_remove_ctx_addrs(sctp_t *sctp)
{
	sctp_conn_faddrs_t	*cfaddrs;
	sctp_faddr_t		*cur_fp, *prev_fp;

	cfaddrs = sctp->sctp_conn_faddrs;
	kmem_free(cfaddrs, sizeof (sctp_conn_faddrs_t) +
	    (cfaddrs->sconnf_addr_cnt * sizeof (sctp_conn_faddr_t)));
	sctp->sctp_conn_faddrs = NULL;

	prev_fp = NULL;
	cur_fp = sctp->sctp_faddrs;
	do {
		sctp_faddr_t *tmp_fp;

		if (cur_fp->sf_connectx) {
			mutex_enter(&sctp->sctp_conn_tfp->tf_lock);
			if (prev_fp != NULL)
				prev_fp->sf_next = cur_fp->sf_next;
			else
				sctp->sctp_faddrs = cur_fp->sf_next;
			mutex_exit(&sctp->sctp_conn_tfp->tf_lock);
			sctp_timer_free(cur_fp->sf_timer_mp);
			ixa_refrele(cur_fp->sf_ixa);

			tmp_fp = cur_fp;
			cur_fp = cur_fp->sf_next;
			sctp_cong_destroy(tmp_fp);
			kmem_cache_free(sctp_kmem_faddr_cache, tmp_fp);
			sctp->sctp_nfaddrs--;
		} else {
			prev_fp = cur_fp;
			cur_fp = cur_fp->sf_next;
		}
	} while (cur_fp != NULL);
}

/*
 * Add a new peer address to an sctp_t.  Returns 0 on success, ENOMEM on
 * memory allocation failure, EHOSTUNREACH.  Since this function needs to
 * hold the conn fanout lock, all memory allocation must not sleep.  The
 * new peer address is always added at the end of the sctp_faddrs list.
 * If parameter new_faddr is not NULL, the new peer address is returned.
 *
 * if the connection credentials fail remote host accreditation or
 * if the new destination does not support the previously established
 * connection security label.
 */
int
sctp_add_faddr(sctp_t *sctp, sctp_t *psctp, in6_addr_t *addr,
    sctp_faddr_t **new_faddr, sctp_tf_t *tfp, boolean_t caller_hold_lock)
{
	sctp_faddr_t	*faddr;
	mblk_t		*timer_mp;
	int		err = 0;
	conn_t		*connp = sctp->sctp_connp;
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	sctp_t		*lsctp;

	/*
	 * Make sure that the new peer address is not part of another
	 * association.  Need to hold the conn fanout lock when doing the
	 * lookup.
	 */
	if (tfp == NULL) {
		tfp = &sctps->sctps_conn_fanout[SCTP_CONN_HASH(sctps,
		    connp->conn_ports)];
	}
	if (!caller_hold_lock)
		mutex_enter(&tfp->tf_lock);

	if ((lsctp = sctp_lookup(sctp, addr, tfp, &connp->conn_ports,
	    SCTPS_COOKIE_WAIT)) != NULL) {
		/* found a duplicate connection */
		if (!caller_hold_lock)
			mutex_exit(&tfp->tf_lock);
		SCTP_REFRELE(lsctp);
		return (EADDRINUSE);
	}

	if (is_system_labeled()) {
		ip_xmit_attr_t	*ixa = connp->conn_ixa;
		ts_label_t	*effective_tsl = NULL;

		ASSERT(ixa->ixa_tsl != NULL);

		/*
		 * Verify the destination is allowed to receive packets
		 * at the security label of the connection we are initiating.
		 *
		 * tsol_check_dest() will create a new effective label for
		 * this connection with a modified label or label flags only
		 * if there are changes from the original label.
		 *
		 * Accept whatever label we get if this is the first
		 * destination address for this connection. The security
		 * label and label flags must match any previuous settings
		 * for all subsequent destination addresses.
		 */
		if (IN6_IS_ADDR_V4MAPPED(addr)) {
			uint32_t dst;
			IN6_V4MAPPED_TO_IPADDR(addr, dst);
			err = tsol_check_dest(ixa->ixa_tsl,
			    &dst, IPV4_VERSION, connp->conn_mac_mode,
			    connp->conn_zone_is_global, &effective_tsl);
		} else {
			err = tsol_check_dest(ixa->ixa_tsl,
			    addr, IPV6_VERSION, connp->conn_mac_mode,
			    connp->conn_zone_is_global, &effective_tsl);
		}
		if (err != 0)
			goto add_faddr_out;

		if (sctp->sctp_faddrs == NULL && effective_tsl != NULL) {
			ip_xmit_attr_replace_tsl(ixa, effective_tsl);
		} else if (effective_tsl != NULL) {
			label_rele(effective_tsl);
			err = EHOSTUNREACH;
			goto add_faddr_out;
		}
	}

	if ((faddr = kmem_cache_alloc(sctp_kmem_faddr_cache, KM_NOSLEEP)) ==
	    NULL) {
		err = ENOMEM;
		goto add_faddr_out;
	}
	bzero(faddr, sizeof (*faddr));
	timer_mp = sctp_timer_alloc((sctp), sctp_rexmit_timer, KM_NOSLEEP);
	if (timer_mp == NULL) {
		kmem_cache_free(sctp_kmem_faddr_cache, faddr);
		err = ENOMEM;
		goto add_faddr_out;
	}
	((sctpt_t *)(timer_mp->b_rptr))->sctpt_faddr = faddr;

	/* Start with any options set on the conn */
	faddr->sf_ixa = conn_get_ixa_exclusive(connp);
	if (faddr->sf_ixa == NULL) {
		freemsg(timer_mp);
		kmem_cache_free(sctp_kmem_faddr_cache, faddr);
		err = ENOMEM;
		goto add_faddr_out;
	}
	faddr->sf_ixa->ixa_notify_cookie = connp->conn_sctp;

	/*
	 * Insert the new faddr to the list before calling sctp_init_faddr().
	 * sctp_init_faddr() calls sctp_get_dest().  And sctp_get_dest()
	 * may call sctp_min_fp_mss() which requires that the list of faddrs
	 * is correct.
	 */
	if (sctp->sctp_faddrs == NULL) {
		ASSERT(sctp->sctp_lastfaddr == NULL);
		/* only element on list; first and last are same */
		sctp->sctp_faddrs = sctp->sctp_lastfaddr = faddr;
	} else {
		sctp->sctp_lastfaddr->sf_next = faddr;
		sctp->sctp_lastfaddr = faddr;
	}
	sctp->sctp_nfaddrs++;

	sctp_init_faddr(sctp, psctp, faddr, addr, timer_mp);
	ASSERT(faddr->sf_ixa->ixa_cred != NULL);

	/* ip_attr_connect didn't allow broadcats/multicast dest */
	ASSERT(faddr->sf_next == NULL);

	if (new_faddr != NULL)
		*new_faddr = faddr;

add_faddr_out:
	if (!caller_hold_lock)
		mutex_exit(&tfp->tf_lock);
	return (err);
}

sctp_faddr_t *
sctp_lookup_faddr(sctp_t *sctp, in6_addr_t *addr)
{
	sctp_faddr_t *fp;

	for (fp = sctp->sctp_faddrs; fp != NULL; fp = fp->sf_next) {
		if (IN6_ARE_ADDR_EQUAL(&fp->sf_faddr, addr))
			break;
	}

	return (fp);
}

/*
 * To change the currently used peer address to the specified one.
 */
void
sctp_set_faddr_current(sctp_t *sctp, sctp_faddr_t *fp)
{
	conn_t *connp = sctp->sctp_connp;
	ip_pkt_t *ipp = &connp->conn_xmit_ipp;

	/* Now setup the composite header. */
	if (fp->sf_isv4) {
		ipha_t *ipha;

		ipha = sctp->sctp_ipha;

		IN6_V4MAPPED_TO_IPADDR(&fp->sf_faddr, ipha->ipha_dst);
		IN6_V4MAPPED_TO_IPADDR(&fp->sf_saddr, ipha->ipha_src);

		/* Update don't fragment bit */
		if (fp->sf_df) {
			ipha->ipha_fragment_offset_and_flags = htons(IPH_DF);
		} else {
			ipha->ipha_fragment_offset_and_flags = 0;
		}

		/*
		 * TOS may be different for each peer address.  Set it to
		 * the default first and then see if it needs to be changed.
		 */
		ipha->ipha_type_of_service = ipp->ipp_type_of_service;
		if (fp->sf_v4_tos != 0)
			ipha->ipha_type_of_service = fp->sf_v4_tos;
	} else {
		ip6_t *ip6h;

		ip6h = sctp->sctp_ip6h;

		ip6h->ip6_dst = fp->sf_faddr;
		ip6h->ip6_src = fp->sf_saddr;

		/*
		 * Flow label may be different for each peer address.  Set
		 * it to the default first and then see if it needst to be
		 * changed.
		 */
		ip6h->ip6_vcf =
		    (IPV6_DEFAULT_VERS_AND_FLOW & IPV6_VERS_AND_FLOW_MASK) |
		    (connp->conn_flowinfo & ~IPV6_VERS_AND_FLOW_MASK);
		if (ipp->ipp_fields & IPPF_TCLASS) {
			/* Overrides the class part of flowinfo */
			ip6h->ip6_vcf = IPV6_TCLASS_FLOW(ip6h->ip6_vcf,
			    ipp->ipp_tclass);
		}
		if (fp->sf_v6_flowlabel != 0) {
			ip6h->ip6_vcf = IPV6_FLOWINFO_FLOW(ip6h->ip6_vcf,
			    fp->sf_v6_flowlabel);
		}

	}

	sctp->sctp_current = fp;
}

void
sctp_redo_faddr_srcs(sctp_t *sctp)
{
	sctp_faddr_t *fp;

	for (fp = sctp->sctp_faddrs; fp != NULL; fp = fp->sf_next) {
		sctp_get_dest(sctp, fp);
	}
}

/*
 * A peer address is alive, this function sets the relevant states for
 * event.  And if an address is not alive before, call sctp_get_dest()
 * to find out the source address information for it.  Because of this,
 * this address may be marked unreachable because there is no source
 * to reach it (asymmetric route).
 */
void
sctp_faddr_alive(sctp_t *sctp, sctp_faddr_t *fp, int64_t now)
{
	/*
	 * If we are under memory pressure, we abort association waiting
	 * in zero window probing state for too long.  We do this by not
	 * resetting sctp_strikes.  So if sctp_zero_win_probe continues
	 * while under memory pressure, this association will eventually
	 * time out.
	 */
	if (!sctp->sctp_zero_win_probe || !sctp->sctp_sctps->sctps_reclaim) {
		sctp->sctp_strikes = 0;
	}
	fp->sf_strikes = 0;
	fp->sf_lastactive = now;
	fp->sf_hb_expiry = now + SET_HB_INTVL(fp);
	fp->sf_hb_pending = B_FALSE;
	if (fp->sf_state != SCTP_FADDRS_ALIVE) {
		DTRACE_PROBE2(sctp_faddr_alive, sctp_t *, sctp, sctp_faddr_t *,
		    fp);
		fp->sf_state = SCTP_FADDRS_ALIVE;
		sctp_intf_event(sctp, fp->sf_faddr, SCTP_ADDR_AVAILABLE, 0);
		/*
		 * The peer address is alive again, call sctp_get_dest()
		 * to find the source address used (may be changed).
		 *
		 * Note that if we didn't find a source in sctp_get_dest(),
		 * fp will be marked SCTP_FADDRS_UNREACH.
		 */
		sctp_get_dest(sctp, fp);
	}
}

/*
 * Return B_TRUE if there is still an active peer address with zero strikes;
 * otherwise rturn B_FALSE.
 */
boolean_t
sctp_is_a_faddr_clean(sctp_t *sctp)
{
	sctp_faddr_t *fp;

	for (fp = sctp->sctp_faddrs; fp; fp = fp->sf_next) {
		if (fp->sf_state == SCTP_FADDRS_ALIVE && fp->sf_strikes == 0) {
			return (B_TRUE);
		}
	}

	return (B_FALSE);
}

/*
 * Mark a peer address as SCTP_FADDRS_DOWN.  If the downed faddr was the
 * current faddr, a new current faddr will be chosen.
 *
 * Returns B_FALSE if there is at least one other active faddr, B_TRUE
 * if there is none.  If there is none left, sct_faddr_dead() will call
 * sctp_clean_death() to kill the association.
 */
boolean_t
sctp_faddr_dead(sctp_t *sctp, sctp_faddr_t *fp)
{
	sctp_faddr_t *ofp;
	sctp_stack_t *sctps = sctp->sctp_sctps;

	if (fp->sf_state == SCTP_FADDRS_ALIVE)
		sctp_intf_event(sctp, fp->sf_faddr, SCTP_ADDR_UNREACHABLE, 0);
	fp->sf_state = SCTP_FADDRS_DOWN;

	/*
	 * Reset RTO and RTT estimate.  We still send HEARTBEAT probe to
	 * down peer address.  If we don't reset RTO, the interval to send
	 * HEARTBEAT will be significantly skewed by the already backed off
	 * RTO.
	 */
	fp->sf_rto = MIN(sctp->sctp_rto_initial, sctp->sctp_rto_max_init);
	SCTP_MAX_RTO(sctp, fp);
	fp->sf_rtt_updates = 0;
	fp->sf_srtt = -1;

	if (fp == sctp->sctp_current) {
		/* Current faddr down; need to switch it */
		sctp->sctp_current = NULL;
	}
	/*
	 * If the dead address is the primary, set the sf_chk_cnt so that
	 * we will switch back according to the peer address policy (set
	 * at sctp_init_values().
	 */
	if (fp == sctp->sctp_primary)
		fp->sf_chk_cnt = sctp->sctp_faddr_chk_cnt;

	/*
	 * Check if there is still an alive faddr.  If there is none, kill the
	 * assoc.
	 */
	ofp = fp;
	for (fp = fp->sf_next; fp != NULL; fp = fp->sf_next) {
		if (fp->sf_state == SCTP_FADDRS_ALIVE) {
			break;
		}
	}

	if (fp == NULL) {
		/* Continue from beginning of list */
		for (fp = sctp->sctp_faddrs; fp != ofp; fp = fp->sf_next) {
			if (fp->sf_state == SCTP_FADDRS_ALIVE) {
				break;
			}
		}
	}

	/*
	 * Found a new fp.  If the current faddr is dead, use the new fp
	 * as the current one.
	 */
	if (fp != ofp) {
		if (sctp->sctp_current == NULL) {
			DTRACE_PROBE3(sctp_faddr_dead, sctp_t *, sctp,
			    sctp_faddr_t *, ofp, sctp_faddr_t, fp);
			/*
			 * Note that we don't need to reset the source addr
			 * of the new fp.
			 */
			sctp_set_faddr_current(sctp, fp);
		}
		return (B_FALSE);
	}


	/* All faddrs are down; kill the association */
	dprint(1, ("sctp_faddr_dead: all faddrs down, killing assoc\n"));
	SCTPS_BUMP_MIB(sctps, sctpAborted);
	sctp_assoc_event(sctp, sctp->sctp_state < SCTPS_ESTABLISHED ?
	    SCTP_CANT_STR_ASSOC : SCTP_COMM_LOST, 0, NULL);
	sctp_clean_death(sctp, sctp->sctp_client_errno ?
	    sctp->sctp_client_errno : ETIMEDOUT);

	return (B_TRUE);
}

/*
 * Based on the sctp_faddr_policy, pick and return an alternate address when
 * the retransmission timer fires.  It always returns a faddr.
 */
sctp_faddr_t *
sctp_rotate_faddr(sctp_t *sctp, sctp_faddr_t *ofp)
{
	sctp_faddr_t *nfp = NULL;
	sctp_faddr_t *saved_fp = NULL;
	int min_strikes;

	ASSERT(ofp != NULL);

	/* Nothing to do since there is only 1 peer address. */
	if (sctp->sctp_nfaddrs < 2)
		return (ofp);

	/*
	 * If the old peer address is the primary, need to follow the peer
	 * address policy on how to choose.
	 */
	if (ofp == sctp->sctp_primary) {
		switch (sctp->sctp_faddr_policy) {
		case SCTP_FADDR_POLICY_STICKY_PRIMARY:
			/*
			 * Always use the peer primary unless it is not alive.
			 */
			if (ofp->sf_state == SCTP_FADDRS_ALIVE) {
				return (ofp);
			} else {
				/*
				 * Original primary address is dead.  If there
				 * are consecutive sf_chk_cnt HEARTBEAT-ACK
				 * subsequently received on the primary
				 * address, switch back to use it.  Default
				 * value is 1 so that we switch back to use
				 * the primary when it is alive again.
				 */
				ofp->sf_chk_cnt = sctp->sctp_faddr_chk_cnt;
			}
			break;
		case SCTP_FADDR_POLICY_PREF_PRIMARY:
			/*
			 * Peer primary address is preferred.  Use an
			 * alternate address to tranfer for now.  If there
			 * are consecutive sf_chk_cnt HEARTBEAT-ACK
			 * subsequently received on the primary address,
			 * switch back to use it.
			 */
			ofp->sf_chk_cnt = sctp->sctp_faddr_chk_cnt;
			break;
		case SCTP_FADDR_POLICY_ROTATE:
		default:
			/* Always use an alternate address after a timeout. */
			break;
		}
	}

	/*
	 * Find the next live peer address with zero strikes. In case
	 * there is none, find the one with the lowest number of strikes.
	 */
	min_strikes = ofp->sf_strikes;
	nfp = ofp->sf_next;
	while (nfp != ofp) {
		/* If reached end of list, continue scan from the head */
		if (nfp == NULL) {
			nfp = sctp->sctp_faddrs;
			continue;
		}
		if (nfp->sf_state == SCTP_FADDRS_ALIVE) {
			if (nfp->sf_strikes == 0)
				break;
			if (nfp->sf_strikes < min_strikes) {
				min_strikes = nfp->sf_strikes;
				saved_fp = nfp;
			}
		}
		nfp = nfp->sf_next;
	}
	/* If reached the old address, there is no zero strike path */
	if (nfp == ofp)
		nfp = NULL;

	/*
	 * If there is a peer address with zero strikes  we use that, if not
	 * return a peer address with fewer strikes than the one last used,
	 * if neither exist we may as well stay with the old one.
	 */
	if (nfp != NULL)
		return (nfp);
	if (saved_fp != NULL)
		return (saved_fp);
	return (ofp);
}

void
sctp_unlink_faddr(sctp_t *sctp, sctp_faddr_t *fp)
{
	sctp_faddr_t *fpp;

	if (!sctp->sctp_faddrs) {
		return;
	}

	if (fp->sf_timer_mp != NULL) {
		sctp_timer_free(fp->sf_timer_mp);
		fp->sf_timer_mp = NULL;
		fp->sf_timer_running = 0;
	}
	if (fp->sf_rc_timer_mp != NULL) {
		sctp_timer_free(fp->sf_rc_timer_mp);
		fp->sf_rc_timer_mp = NULL;
		fp->sf_rc_timer_running = 0;
	}
	if (fp->sf_ixa != NULL) {
		ixa_refrele(fp->sf_ixa);
		fp->sf_ixa = NULL;
	}

	sctp_cong_destroy(fp);

	if (fp == sctp->sctp_faddrs) {
		goto gotit;
	}

	for (fpp = sctp->sctp_faddrs; fpp->sf_next != fp; fpp = fpp->sf_next)
		;

gotit:
	ASSERT(sctp->sctp_conn_tfp != NULL);
	mutex_enter(&sctp->sctp_conn_tfp->tf_lock);
	if (fp == sctp->sctp_faddrs) {
		sctp->sctp_faddrs = fp->sf_next;
	} else {
		fpp->sf_next = fp->sf_next;
	}
	mutex_exit(&sctp->sctp_conn_tfp->tf_lock);
	kmem_cache_free(sctp_kmem_faddr_cache, fp);
	sctp->sctp_nfaddrs--;
}

void
sctp_zap_faddrs(sctp_t *sctp, int caller_holds_lock)
{
	sctp_faddr_t *fp, *fpn;

	if (sctp->sctp_conn_faddrs != NULL) {
		kmem_free(sctp->sctp_conn_faddrs, sizeof (sctp_conn_faddrs_t) +
		    (sctp->sctp_conn_faddrs->sconnf_addr_cnt *
		    sizeof (sctp_conn_faddr_t)));
		sctp->sctp_conn_faddrs = NULL;
	}

	if (sctp->sctp_faddrs == NULL) {
		ASSERT(sctp->sctp_lastfaddr == NULL);
		return;
	}

	ASSERT(sctp->sctp_lastfaddr != NULL);
	sctp->sctp_lastfaddr = NULL;
	sctp->sctp_current = NULL;
	sctp->sctp_primary = NULL;

	sctp_free_faddr_timers(sctp);

	if (sctp->sctp_conn_tfp != NULL && !caller_holds_lock) {
		/* in conn fanout; need to hold lock */
		mutex_enter(&sctp->sctp_conn_tfp->tf_lock);
	}

	for (fp = sctp->sctp_faddrs; fp; fp = fpn) {
		fpn = fp->sf_next;
		if (fp->sf_ixa != NULL) {
			ixa_refrele(fp->sf_ixa);
			fp->sf_ixa = NULL;
		}
		sctp_cong_destroy(fp);
		kmem_cache_free(sctp_kmem_faddr_cache, fp);
		sctp->sctp_nfaddrs--;
	}

	sctp->sctp_faddrs = NULL;
	ASSERT(sctp->sctp_nfaddrs == 0);
	if (sctp->sctp_conn_tfp != NULL && !caller_holds_lock) {
		mutex_exit(&sctp->sctp_conn_tfp->tf_lock);
	}

}

void
sctp_zap_addrs(sctp_t *sctp)
{
	sctp_zap_faddrs(sctp, 0);
	sctp_free_saddrs(sctp);
}

/*
 * Build two SCTP header templates; one for IPv4 and one for IPv6.
 * Store them in sctp_iphc and sctp_iphc6 respectively (and related fields).
 * There are no IP addresses in the templates, but the port numbers and
 * verifier are field in from the conn_t and sctp_t.
 *
 * Returns failure if can't allocate memory, or if there is a problem
 * with a routing header/option.
 *
 * We allocate space for the minimum sctp header (sctp_hdr_t).
 *
 * We massage an routing option/header. There is no checksum implication
 * for a routing header for sctp.
 *
 * Caller needs to update conn_wroff if desired.
 *
 * TSol notes: This assumes that a SCTP association has a single peer label
 * since we only track a single pair of ipp_label_v4/v6 and not a separate one
 * for each faddr.
 */
int
sctp_build_hdrs(sctp_t *sctp, int sleep)
{
	conn_t		*connp = sctp->sctp_connp;
	ip_pkt_t	*ipp = &connp->conn_xmit_ipp;
	uint_t		ip_hdr_length;
	uchar_t		*hdrs;
	uint_t		hdrs_len;
	uint_t		ulp_hdr_length = sizeof (sctp_hdr_t);
	ipha_t		*ipha;
	ip6_t		*ip6h;
	sctp_hdr_t	*sctph;
	in6_addr_t	v6src, v6dst;
	ipaddr_t	v4src, v4dst;
	boolean_t	hdr_len_chg = B_FALSE;
	boolean_t	hdr_len_chg_v6 = B_FALSE;
	int		old_hdr_len = 0;
	int		old_hdr6_len = 0;

	v4src = connp->conn_saddr_v4;
	v4dst = connp->conn_faddr_v4;
	v6src = connp->conn_saddr_v6;
	v6dst = connp->conn_faddr_v6;

	/* First do IPv4 header */
	ip_hdr_length = ip_total_hdrs_len_v4(ipp);

	/* In case of TX label and IP options it can be too much */
	if (ip_hdr_length > IP_MAX_HDR_LENGTH) {
		/* Preserves existing TX errno for this */
		return (EHOSTUNREACH);
	}
	hdrs_len = ip_hdr_length + ulp_hdr_length;
	ASSERT(hdrs_len != 0);

	if (hdrs_len != sctp->sctp_iphc_len) {
		/* Allocate new before we free any old */
		hdrs = kmem_alloc(hdrs_len, sleep);
		if (hdrs == NULL)
			return (ENOMEM);

		if (sctp->sctp_iphc != NULL)
			kmem_free(sctp->sctp_iphc, sctp->sctp_iphc_len);
		sctp->sctp_iphc = hdrs;
		sctp->sctp_iphc_len = hdrs_len;
		hdr_len_chg = B_TRUE;
		old_hdr_len = sctp->sctp_hdr_len;
	} else {
		hdrs = sctp->sctp_iphc;
	}
	sctp->sctp_hdr_len = sctp->sctp_iphc_len;
	sctp->sctp_ip_hdr_len = ip_hdr_length;

	sctph = (sctp_hdr_t *)(hdrs + ip_hdr_length);
	sctp->sctp_sctph = sctph;
	sctph->sh_sport = connp->conn_lport;
	sctph->sh_dport = connp->conn_fport;
	sctph->sh_verf = sctp->sctp_fvtag;
	sctph->sh_chksum = 0;

	ipha = (ipha_t *)hdrs;
	sctp->sctp_ipha = ipha;

	ipha->ipha_src = v4src;
	ipha->ipha_dst = v4dst;
	ip_build_hdrs_v4(hdrs, ip_hdr_length, ipp, connp->conn_proto);
	ipha->ipha_length = htons(hdrs_len);
	ipha->ipha_fragment_offset_and_flags = 0;

	if (ipp->ipp_fields & IPPF_IPV4_OPTIONS)
		(void) ip_massage_options(ipha, connp->conn_netstack,
		    (ipp->ipp_flags & IPP_SR_REVERSED) == 0);

	/* Now IPv6 */
	ip_hdr_length = ip_total_hdrs_len_v6(ipp);
	hdrs_len = ip_hdr_length + ulp_hdr_length;
	ASSERT(hdrs_len != 0);

	if (hdrs_len != sctp->sctp_iphc6_len) {
		/* Allocate new before we free any old */
		hdrs = kmem_alloc(hdrs_len, sleep);
		if (hdrs == NULL)
			return (ENOMEM);

		if (sctp->sctp_iphc6 != NULL)
			kmem_free(sctp->sctp_iphc6, sctp->sctp_iphc6_len);
		sctp->sctp_iphc6 = hdrs;
		sctp->sctp_iphc6_len = hdrs_len;
		hdr_len_chg_v6 = B_TRUE;
		old_hdr6_len = sctp->sctp_hdr6_len;
	} else {
		hdrs = sctp->sctp_iphc6;
	}
	sctp->sctp_hdr6_len = sctp->sctp_iphc6_len;
	sctp->sctp_ip_hdr6_len = ip_hdr_length;

	sctph = (sctp_hdr_t *)(hdrs + ip_hdr_length);
	sctp->sctp_sctph6 = sctph;
	sctph->sh_sport = connp->conn_lport;
	sctph->sh_dport = connp->conn_fport;
	sctph->sh_verf = sctp->sctp_fvtag;
	sctph->sh_chksum = 0;

	ip6h = (ip6_t *)hdrs;
	sctp->sctp_ip6h = ip6h;

	ip6h->ip6_src = v6src;
	ip6h->ip6_dst = v6dst;
	ip_build_hdrs_v6(hdrs, ip_hdr_length, ipp, connp->conn_proto,
	    connp->conn_flowinfo);
	ip6h->ip6_plen = htons(hdrs_len - IPV6_HDR_LEN);

	if (ipp->ipp_fields & IPPF_RTHDR) {
		uint8_t		*end;
		ip6_rthdr_t	*rth;

		end = (uint8_t *)ip6h + ip_hdr_length;
		rth = ip_find_rthdr_v6(ip6h, end);
		if (rth != NULL) {
			(void) ip_massage_options_v6(ip6h, rth,
			    connp->conn_netstack);
		}

		/*
		 * Verify that the first hop isn't a mapped address.
		 * Routers along the path need to do this verification
		 * for subsequent hops.
		 */
		if (IN6_IS_ADDR_V4MAPPED(&ip6h->ip6_dst))
			return (EADDRNOTAVAIL);
	}

	/*
	 * If the IP header length is changed, we need to re-adjust the MSS
	 * of every peer address (sctp_faddr_t).
	 */
	if (hdr_len_chg || hdr_len_chg_v6) {
		sctp_faddr_t *fp;
		uint_t mss;

		/*
		 * If the old length is smaller than the new length, old_*_len
		 * will be negative.  And we can just add it to sf_pmss.  We
		 * also need to adjust the sctp_mss, which is the minimum of
		 * all fp_pmss.
		 */
		old_hdr_len -= sctp->sctp_hdr_len;
		old_hdr6_len -= sctp->sctp_hdr6_len;
		mss = IP_MAXPACKET;
		for (fp = sctp->sctp_faddrs; fp != NULL; fp = fp->sf_next) {
			if (fp->sf_isv4 && hdr_len_chg) {
				/*
				 * Make sure sf_pmss is a multiple of
				 * SCTP_ALIGN.
				 */
				fp->sf_pmss = (fp->sf_pmss + old_hdr_len) &
				    ~(SCTP_ALIGN - 1);
			} else if (!fp->sf_isv4 && hdr_len_chg_v6) {
				fp->sf_pmss = (fp->sf_pmss + old_hdr6_len) &
				    ~(SCTP_ALIGN - 1);
			}
			if (fp->sf_pmss < mss)
				mss = fp->sf_pmss;
		}
		sctp->sctp_mss = mss;
	}

	return (0);
}

/*
 * Tsol note: We have already verified the addresses using tsol_check_dest()
 * in sctp_add_faddr(), thus no need to redo that here.
 * We do setup ipp_label_v4 and ipp_label_v6 based on which addresses
 * we have.  But we assume that the security label data sent over an SCTP
 * association is fixed for the lifetime of the association, regardless
 * of the different peer addresses.  So when we got one label (or two labels
 * for IPv4 and IPv6), we are done.
 */
int
sctp_tsol_get_label(sctp_t *sctp)
{
	sctp_faddr_t *fp;
	boolean_t gotv4 = B_FALSE;
	boolean_t gotv6 = B_FALSE;
	conn_t *connp = sctp->sctp_connp;

	ASSERT(sctp->sctp_faddrs != NULL);
	ASSERT(sctp->sctp_nsaddrs > 0);
	ASSERT(is_system_labeled());

	for (fp = sctp->sctp_faddrs; fp != NULL; fp = fp->sf_next) {
		if (!gotv4 && IN6_IS_ADDR_V4MAPPED(&fp->sf_faddr)) {
			if (conn_update_label(connp, fp->sf_ixa, &fp->sf_faddr,
			    &connp->conn_xmit_ipp) == 0) {
				gotv4 = B_TRUE;
				/*
				 * We are done if it is an IPv4 only endpoint,
				 * or if we also got the IPv6 label.
				 */
				if (connp->conn_family == AF_INET || gotv6) {
					break;
				}
			}
		} else if (!gotv6 && !IN6_IS_ADDR_V4MAPPED(&fp->sf_faddr)) {
			if (conn_update_label(connp, fp->sf_ixa, &fp->sf_faddr,
			    &connp->conn_xmit_ipp) == 0) {
				gotv6 = B_TRUE;
				if (gotv4)
					break;
			}
		}
	}

done:
	if (!gotv4 && !gotv6)
		return (EACCES);

	return (0);
}

/*
 * got_errchunk is set B_TRUE only if called from validate_init_params(), when
 * an ERROR chunk is already prepended the size of which needs updating for
 * additional unrecognized parameters. Other callers either prepend the ERROR
 * chunk with the correct size after calling this function, or they are calling
 * to add an invalid parameter to an INIT_ACK chunk, in that case no ERROR chunk
 * exists, the CAUSE blocks go into the INIT_ACK directly.
 *
 * *errmp will be non-NULL both when adding an additional CAUSE block to an
 * existing prepended COOKIE ERROR chunk (processing params of an INIT_ACK),
 * and when adding unrecognized parameters after the first, to an INIT_ACK
 * (processing params of an INIT chunk).
 */
void
sctp_add_unrec_parm(sctp_parm_hdr_t *uph, mblk_t **errmp,
    boolean_t got_errchunk)
{
	mblk_t *mp;
	sctp_parm_hdr_t *ph;
	size_t len;
	int pad;
	sctp_chunk_hdr_t *ecp;

	len = sizeof (*ph) + ntohs(uph->sph_len);
	if ((pad = SCTP_PAD_LEN(len)) != 0)
		len += pad;

	mp = allocb(len, BPRI_MED);
	if (mp == NULL) {
		return;
	}

	ph = (sctp_parm_hdr_t *)(mp->b_rptr);
	ph->sph_type = htons(PARM_UNRECOGNIZED);
	ph->sph_len = htons(len - pad);

	/* copy in the unrecognized parameter */
	bcopy(uph, ph + 1, ntohs(uph->sph_len));

	if (pad != 0)
		bzero((mp->b_rptr + len - pad), pad);

	mp->b_wptr = mp->b_rptr + len;
	if (*errmp != NULL) {
		/*
		 * Update total length if an ERROR chunk, then link
		 * this CAUSE block to the possible chain of CAUSE
		 * blocks attached to the ERROR chunk or INIT_ACK
		 * being created.
		 */
		if (got_errchunk) {
			/* ERROR chunk already prepended */
			ecp = (sctp_chunk_hdr_t *)((*errmp)->b_rptr);
			ecp->sch_len = htons(ntohs(ecp->sch_len) + len);
		}
		linkb(*errmp, mp);
	} else {
		*errmp = mp;
	}
}

/*
 * Given a valid (has at least the sctp_parm_hdr_t which this function
 * accesses) current parameter and the size (remaining) of the buffer
 * including the current parameter, returns a pointer to the next valid
 * parameter in the buffer with bounds checking.  The remaining buffer size
 * after subtracting the current parameter's size and padding is also returned.
 */
sctp_parm_hdr_t *
sctp_next_parm(sctp_parm_hdr_t *current, ssize_t *remaining)
{
	int pad;
	uint16_t len;

	ASSERT(*remaining >= (ssize_t)sizeof (sctp_parm_hdr_t));

	len = ntohs(current->sph_len);
	*remaining -= len;

	ASSERT(len >= sizeof (sctp_parm_hdr_t));

	if ((pad = SCTP_PAD_LEN(len)) != 0)
		*remaining -= pad;
	if (*remaining >= (ssize_t)sizeof (sctp_parm_hdr_t)) {
		/*LINTED pointer cast may result in improper alignment*/
		current = (sctp_parm_hdr_t *)((char *)current + len + pad);
		return (current);
	}

	return (NULL);
}

/*
 * Sets the address parameters given in the INIT chunk into sctp's
 * faddrs; if psctp is non-NULL, copies psctp's saddrs. If there are
 * no address parameters in the INIT chunk, a single faddr is created
 * from the ip hdr at the beginning of pkt.
 * If there already are existing addresses hanging from sctp, merge
 * them in, if the old info contains addresses which are not present
 * in this new info, get rid of them, and clean the pointers if there's
 * messages which have this as their target address.
 *
 * We also re-adjust the source address list here since the list may
 * contain more than what is actually part of the association. If
 * we get here from sctp_send_cookie_echo(), we are on the active
 * side and psctp will be NULL and ich will be the INIT-ACK chunk.
 * If we get here from sctp_accept_comm(), ich will be the INIT chunk
 * and psctp will the listening endpoint.
 *
 * INIT processing: When processing the INIT we inherit the src address
 * list from the listener. For a loopback or linklocal association, we
 * delete the list and just take the address from the IP header (since
 * that's how we created the INIT-ACK). Additionally, for loopback we
 * ignore the address params in the INIT. For determining which address
 * types were sent in the INIT-ACK we follow the same logic as in
 * creating the INIT-ACK. We delete addresses of the type that are not
 * supported by the peer.
 *
 * INIT-ACK processing: When processing the INIT-ACK since we had not
 * included addr params for loopback or linklocal addresses when creating
 * the INIT, we just use the address from the IP header. Further, for
 * loopback we ignore the addr param list. We mark addresses of the
 * type not supported by the peer as unconfirmed.
 *
 * In case of INIT processing we look for supported address types in the
 * supported address param, if present. In both cases the address type in
 * the IP header is supported as well as types for addresses in the param
 * list, if any.
 *
 * Once we have the supported address types sctp_check_saddr() runs through
 * the source address list and deletes or marks as unconfirmed address of
 * types not supported by the peer.
 *
 * Returns 0 on success, sys errno on failure
 */
int
sctp_get_addrparams(sctp_t *sctp, sctp_t *psctp, mblk_t *pkt,
    sctp_chunk_hdr_t *ich, uint_t *sctp_options)
{
	sctp_init_chunk_t	*init;
	ipha_t			*iph;
	ip6_t			*ip6h;
	in6_addr_t		hdrsaddr[1];
	in6_addr_t		hdrdaddr[1];
	sctp_parm_hdr_t		*ph;
	ssize_t			remaining;
	int			isv4;
	int			err;
	sctp_faddr_t		*fp;
	int			supp_af = 0;
	boolean_t		check_saddr = B_TRUE;
	in6_addr_t		curaddr;
	sctp_stack_t		*sctps = sctp->sctp_sctps;
	conn_t			*connp = sctp->sctp_connp;

	if (sctp_options != NULL)
		*sctp_options = 0;

	/* extract the address from the IP header */
	isv4 = (IPH_HDR_VERSION(pkt->b_rptr) == IPV4_VERSION);
	if (isv4) {
		iph = (ipha_t *)pkt->b_rptr;
		IN6_IPADDR_TO_V4MAPPED(iph->ipha_src, hdrsaddr);
		IN6_IPADDR_TO_V4MAPPED(iph->ipha_dst, hdrdaddr);
		supp_af |= PARM_SUPP_V4;
	} else {
		ip6h = (ip6_t *)pkt->b_rptr;
		hdrsaddr[0] = ip6h->ip6_src;
		hdrdaddr[0] = ip6h->ip6_dst;
		supp_af |= PARM_SUPP_V6;
	}

	/*
	 * Unfortunately, we can't delay this because adding an faddr
	 * looks for the presence of the source address (from the ire
	 * for the faddr) in the source address list. We could have
	 * delayed this if, say, this was a loopback/linklocal connection.
	 * Now, we just end up nuking this list and taking the addr from
	 * the IP header for loopback/linklocal.
	 */
	if (psctp != NULL && psctp->sctp_nsaddrs > 0) {
		ASSERT(sctp->sctp_nsaddrs == 0);

		err = sctp_dup_saddrs(psctp, sctp, KM_NOSLEEP);
		if (err != 0)
			return (err);
	}
	/*
	 * We will add the faddr before parsing the address list as this
	 * might be a loopback connection and we would not have to
	 * go through the list.
	 *
	 * Make sure the header's addr is in the list
	 */
	fp = sctp_lookup_faddr(sctp, hdrsaddr);
	if (fp == NULL) {
		/* Not included; add it now */
		err = sctp_add_faddr(sctp, psctp, hdrsaddr, &fp,
		    sctp->sctp_conn_tfp, B_FALSE);
		if (err != 0)
			return (err);
	} else {
		/* In case it was added by connectx(). */
		fp->sf_connectx = B_FALSE;
	}

	if (cl_sctp_assoc_change != NULL && psctp == NULL)
		curaddr = sctp->sctp_current->sf_faddr;
	/*
	 * Make the header addr the primary.
	 * sctp_add_faddr() above calls sctp_init_faddr() which calls
	 * sctp_get_dest(). sctp_get_dest() would update fp with ire and
	 * src address, and update header and mss for fp == sctp_current.
	 * If however the INIT ACK originates from a different address than
	 * that to which the INIT was send, it is necessary to use the source
	 * address from the INIT ACK as the peer address to communicate with.
	 * sctp_set_faddr_current() is called to update header template,
	 * mss and sctp_current, also inform ULP about this change.
	 */
	sctp->sctp_primary = fp;
	if (fp != sctp->sctp_current)
		sctp_set_faddr_current(sctp, fp);

	/* For loopback connections & linklocal get address from the header */
	if (sctp->sctp_loopback || sctp->sctp_linklocal) {
		if (sctp->sctp_nsaddrs != 0)
			sctp_free_saddrs(sctp);
		if ((err = sctp_saddr_add_addr(sctp, hdrdaddr, 0)) != 0)
			return (err);
		/* For loopback ignore address list */
		if (sctp->sctp_loopback)
			return (0);
		check_saddr = B_FALSE;
	}

	/* Walk the params in the INIT [ACK], pulling out addr params */
	remaining = ntohs(ich->sch_len) - sizeof (*ich) -
	    sizeof (sctp_init_chunk_t);
	if (remaining < sizeof (*ph)) {
		if (check_saddr) {
			sctp_check_saddr(sctp, supp_af, psctp == NULL ?
			    B_FALSE : B_TRUE, hdrdaddr);
		}
		ASSERT(sctp_saddr_lookup(sctp, hdrdaddr, 0) != NULL);
		return (0);
	}

	init = (sctp_init_chunk_t *)(ich + 1);
	ph = (sctp_parm_hdr_t *)(init + 1);

	/* params will have already been byteordered when validating */
	while (ph != NULL) {
		if (ph->sph_type == htons(PARM_SUPP_ADDRS)) {
			int		plen;
			uint16_t	*p;
			uint16_t	addrtype;

			plen = ntohs(ph->sph_len);
			p = (uint16_t *)(ph + 1);
			while (plen > 0) {
				addrtype = ntohs(*p);
				switch (addrtype) {
					case PARM_ADDR6:
						supp_af |= PARM_SUPP_V6;
						break;
					case PARM_ADDR4:
						supp_af |= PARM_SUPP_V4;
						break;
					default:
						break;
				}
				p++;
				plen -= sizeof (*p);
			}
		} else if (ph->sph_type == htons(PARM_ADDR4)) {
			if (remaining >= PARM_ADDR4_LEN) {
				in6_addr_t addr;
				ipaddr_t ta;

				supp_af |= PARM_SUPP_V4;
				/*
				 * Screen out broad/multicasts & loopback.
				 * If the endpoint only accepts v6 address,
				 * go to the next one.
				 *
				 * Subnet broadcast check is done in
				 * sctp_add_faddr().  If the address is
				 * a broadcast address, it won't be added.
				 */
				bcopy(ph + 1, &ta, sizeof (ta));
				if (ta == 0 ||
				    ta == INADDR_BROADCAST ||
				    ta == htonl(INADDR_LOOPBACK) ||
				    CLASSD(ta) || connp->conn_ipv6_v6only) {
					goto next;
				}
				IN6_INADDR_TO_V4MAPPED((struct in_addr *)
				    (ph + 1), &addr);

				/* Check for duplicate. */
				if ((fp = sctp_lookup_faddr(sctp, &addr)) !=
				    NULL) {
					/* Possibly added by connectx(). */
					fp->sf_connectx = B_FALSE;
					fp->sf_df = B_TRUE;
					goto next;
				}

				/* OK, add it to the faddr set */
				err = sctp_add_faddr(sctp, psctp, &addr, NULL,
				    sctp->sctp_conn_tfp, B_FALSE);
				/* Something is wrong...  Try the next one. */
				if (err != 0)
					goto next;
			}
		} else if (ph->sph_type == htons(PARM_ADDR6) &&
		    connp->conn_family == AF_INET6) {
			/* An v4 socket should not take v6 addresses. */
			if (remaining >= PARM_ADDR6_LEN) {
				in6_addr_t *addr6;

				supp_af |= PARM_SUPP_V6;
				addr6 = (in6_addr_t *)(ph + 1);
				/*
				 * Screen out link locals, mcast, loopback
				 * and bogus v6 address.
				 */
				if (IN6_IS_ADDR_LINKLOCAL(addr6) ||
				    IN6_IS_ADDR_MULTICAST(addr6) ||
				    IN6_IS_ADDR_LOOPBACK(addr6) ||
				    IN6_IS_ADDR_V4MAPPED(addr6)) {
					goto next;
				}
				/* Check for duplicate. */
				if ((fp = sctp_lookup_faddr(sctp, addr6)) !=
				    NULL) {
					fp->sf_connectx = B_FALSE;
					fp->sf_df = B_TRUE;
					goto next;
				}

				err = sctp_add_faddr(sctp, psctp,
				    (in6_addr_t *)(ph + 1), NULL,
				    sctp->sctp_conn_tfp, B_FALSE);
				/* Something is wrong...  Try the next one. */
				if (err != 0)
					goto next;
			}
		} else if (ph->sph_type == htons(PARM_FORWARD_TSN)) {
			if (sctp_options != NULL)
				*sctp_options |= SCTP_PRSCTP_OPTION;
		} /* else; skip */

next:
		ph = sctp_next_parm(ph, &remaining);
	}
	if (check_saddr) {
		sctp_check_saddr(sctp, supp_af, psctp == NULL ? B_FALSE :
		    B_TRUE, hdrdaddr);
	}
	ASSERT(sctp_saddr_lookup(sctp, hdrdaddr, 0) != NULL);

	/*
	 * Remove addresses added by connectx() but do not show up in the
	 * address parameter list.
	 */
	if (sctp->sctp_conn_faddrs != NULL)
		sctp_remove_ctx_addrs(sctp);

	/*
	 * We have the right address list now, update clustering's
	 * knowledge because when we sent the INIT we had just added
	 * the address the INIT was sent to.
	 */
	if (psctp == NULL && cl_sctp_assoc_change != NULL) {
		uchar_t	*alist;
		size_t	asize;
		uchar_t	*dlist;
		size_t	dsize;

		asize = sizeof (in6_addr_t) * sctp->sctp_nfaddrs;
		alist = kmem_alloc(asize, KM_NOSLEEP);
		if (alist == NULL) {
			SCTP_KSTAT(sctps, sctp_cl_assoc_change);
			return (ENOMEM);
		}
		/*
		 * Just include the address the INIT was sent to in the
		 * delete list and send the entire faddr list. We could
		 * do it differently (i.e include all the addresses in the
		 * add list even if it contains the original address OR
		 * remove the original address from the add list etc.), but
		 * this seems reasonable enough.
		 */
		dsize = sizeof (in6_addr_t);
		dlist = kmem_alloc(dsize, KM_NOSLEEP);
		if (dlist == NULL) {
			kmem_free(alist, asize);
			SCTP_KSTAT(sctps, sctp_cl_assoc_change);
			return (ENOMEM);
		}
		bcopy(&curaddr, dlist, sizeof (curaddr));
		sctp_get_faddr_list(sctp, alist, asize);
		(*cl_sctp_assoc_change)(connp->conn_family, alist, asize,
		    sctp->sctp_nfaddrs, dlist, dsize, 1, SCTP_CL_PADDR,
		    (cl_sctp_handle_t)sctp);
		/* alist and dlist will be freed by the clustering module */
	}
	return (0);
}

/*
 * Given an INIT chunk, check to see if all the addresses present belong
 * to the peer address list of the sctp_t.  Returns B_FALSE if the check
 * fails (new address added) and the restart should be refused,  B_TRUE
 * if the check succeeds.
 */
boolean_t
sctp_secure_restart_check(sctp_t *sctp, mblk_t *pkt, sctp_chunk_hdr_t *ich,
    ip_recv_attr_t *ira)
{
	sctp_parm_hdr_t *ph;
	ssize_t remaining;
	ipha_t *iph;
	ip6_t *ip6h;
	in6_addr_t addr, *addrp;
	sctp_init_chunk_t *init;
	boolean_t added = B_FALSE;

	/* Extract the address from the IP header */
	if (IPH_HDR_VERSION(pkt->b_rptr) == IPV4_VERSION) {
		iph = (ipha_t *)pkt->b_rptr;
		IN6_IPADDR_TO_V4MAPPED(iph->ipha_src, &addr);
		addrp = &addr;
	} else {
		ip6h = (ip6_t *)pkt->b_rptr;
		addrp = &ip6h->ip6_src;
	}
	/* Is it a new address? */
	if (sctp_lookup_faddr(sctp, addrp) == NULL)
		goto send_abort;

	/* Walk the params in the INIT [ACK], pulling out addr params */
	remaining = ntohs(ich->sch_len) - sizeof (*ich) -
	    sizeof (sctp_init_chunk_t);
	if (remaining < sizeof (*ph)) {
		/* no parameters; restart OK */
		return (B_TRUE);
	}
	init = (sctp_init_chunk_t *)(ich + 1);
	ph = (sctp_parm_hdr_t *)(init + 1);

	while (ph != NULL) {
		if (ph->sph_type == htons(PARM_ADDR4)) {
			if (remaining >= PARM_ADDR4_LEN) {
				IN6_INADDR_TO_V4MAPPED((struct in_addr *)
				    (ph + 1), &addr);
				addrp = &addr;
			}
		} else if (ph->sph_type == htons(PARM_ADDR6)) {
			if (remaining >= PARM_ADDR6_LEN)
				addrp = (in6_addr_t *)(ph + 1);
		}
		if (sctp_lookup_faddr(sctp, addrp) == NULL) {
			added = B_TRUE;
			break;
		}
		ph = sctp_next_parm(ph, &remaining);
	}
	if (!added)
		return (B_TRUE);

send_abort:
	/*
	 * RFC 4960 says "An ABORT SHOULD be sent in response that MAY include
	 * the error 'Restart of an association with new addresses'."  To
	 * save effort, we just send back an ABORT.  If this is an attack
	 * instead of a peer user app error, we should spend the least amount
	 * of time processing it.
	 */
	sctp_send_abort(sctp, sctp_init2vtag(ich), SCTP_ERR_UNKNOWN,
	    NULL, 0, pkt, 0, B_TRUE, ira);

	return (B_FALSE);
}

/*
 * Reset any state related to transmitted chunks.
 */
void
sctp_congest_reset(sctp_t *sctp)
{
	sctp_faddr_t	*fp;
	sctp_stack_t	*sctps = sctp->sctp_sctps;
	mblk_t		*mp;

	for (fp = sctp->sctp_faddrs; fp != NULL; fp = fp->sf_next) {
		fp->sf_ssthresh = sctps->sctps_initial_mtu;
		SET_CWND(fp, fp->sf_pmss, sctps->sctps_slow_start_initial);
		fp->sf_suna = 0;
		fp->sf_cwnd_cnt = 0;
		DTRACE_PROBE3(sctp_cwnd_ssthresh_update, sctp_faddr_t *, fp,
		    uint32_t, fp->sf_cwnd, uint32_t, fp->sf_ssthresh);
	}
	/*
	 * Clean up the transmit list as well since we have reset accounting
	 * on all the fps. Send event upstream, if required.
	 */
	while ((mp = sctp->sctp_xmit_head) != NULL) {
		sctp->sctp_xmit_head = mp->b_next;
		mp->b_next = NULL;
		ASSERT(mp->b_prev == NULL);
		if (sctp->sctp_xmit_head != NULL)
			sctp->sctp_xmit_head->b_prev = NULL;
		sctp_sendfail_event(sctp, mp, 0, B_TRUE);
	}
	sctp->sctp_xmit_head = NULL;
	sctp->sctp_xmit_tail = NULL;
	sctp->sctp_xmit_unacked = NULL;

	sctp->sctp_unacked = 0;
	/*
	 * Any control message as well. We will clean-up this list as well.
	 * This contains any pending ASCONF request that we have queued/sent.
	 * If we do get an ACK we will just drop it. However, given that
	 * we are restarting chances are we aren't going to get any.
	 */
	if (sctp->sctp_cxmit_list != NULL)
		sctp_asconf_free_cxmit(sctp, NULL);
	sctp->sctp_cxmit_list = NULL;
	sctp->sctp_cchunk_pend = 0;

	sctp->sctp_rexmitting = B_FALSE;
	sctp->sctp_rxt_nxttsn = 0;
	sctp->sctp_rxt_maxtsn = 0;

	sctp->sctp_zero_win_probe = B_FALSE;

	/* If ULP is flow controlled, cancel that. */
	if (!SCTP_IS_DETACHED(sctp))
		SCTP_TXQ_UPDATE(sctp);
}

/*
 * Initialize a given sctp_faddr_t structure using the IP address and
 * timer mblk.  The caller is assumed to have called bzero() on the
 * sctp_faddr_t.
 */
static void
sctp_init_faddr(sctp_t *sctp, sctp_t *psctp, sctp_faddr_t *fp,
    in6_addr_t *addr, mblk_t *timer_mp)
{
	sctp_stack_t	*sctps = sctp->sctp_sctps;

	ASSERT(fp->sf_ixa != NULL);

	bcopy(addr, &fp->sf_faddr, sizeof (*addr));
	if (IN6_IS_ADDR_V4MAPPED(addr)) {
		fp->sf_isv4 = 1;
		/* Make sure that sf_pmss is a multiple of SCTP_ALIGN. */
		fp->sf_pmss =
		    (sctps->sctps_initial_mtu - sctp->sctp_hdr_len) &
		    ~(SCTP_ALIGN - 1);
		fp->sf_ixa->ixa_flags |= IXAF_IS_IPV4;
	} else {
		fp->sf_isv4 = 0;
		fp->sf_pmss =
		    (sctps->sctps_initial_mtu - sctp->sctp_hdr6_len) &
		    ~(SCTP_ALIGN - 1);
		fp->sf_ixa->ixa_flags &= ~IXAF_IS_IPV4;
	}
	if (sctp->sctp_mss > fp->sf_pmss)
		sctp->sctp_mss = fp->sf_pmss;

	/*
	 * The following fields should be 0 so no need to be set again.
	 *
	 * sf_rtt_updates
	 * sf_strikes
	 * sf_suna
	 * sf_acked
	 * sf_pmtu_discovered
	 * sf_next (NULL)
	 * sf_T3expire
	 * sf_rxt_unacked
	 */
	ASSERT(fp->sf_rtt_updates == 0);
	ASSERT(fp->sf_strikes == 0);
	ASSERT(fp->sf_suna == 0);
	ASSERT(fp->sf_acked == 0);
	ASSERT(fp->sf_pmtu_discovered == 0);
	ASSERT(fp->sf_next == NULL);
	ASSERT(fp->sf_T3expire == 0);
	ASSERT(fp->sf_rxt_unacked == 0);

	/*
	 * (Modified) Nagle Algorithm threshold is the size of data which
	 * can be fit in a packet.  We need to account for the chunk headers.
	 */
	fp->sf_naglim = MIN(fp->sf_pmss - sizeof (sctp_data_hdr_t),
	    sctps->sctps_naglim_def);
	fp->sf_cwnd = sctps->sctps_slow_start_initial * fp->sf_pmss;
	fp->sf_cwnd_cnt = 0;
	fp->sf_rto = MIN(sctp->sctp_rto_initial, sctp->sctp_rto_max_init);
	SCTP_MAX_RTO(sctp, fp);
	fp->sf_srtt = -1;
	fp->sf_max_retr = sctp->sctp_pp_max_rxt;

	/* Address is not confirmed initially until a HB-ACK is received. */
	fp->sf_state = SCTP_FADDRS_UNCONFIRMED;
	fp->sf_hb_interval = sctp->sctp_hb_interval;
	fp->sf_ssthresh = sctps->sctps_initial_ssthresh;
	fp->sf_lastactive = fp->sf_hb_expiry = ddi_get_lbolt64();
	fp->sf_timer_mp = timer_mp;
	fp->sf_hb_pending = B_FALSE;
	fp->sf_hb_enabled = B_TRUE;
	fp->sf_df = 1;
	(void) random_get_pseudo_bytes((uint8_t *)&fp->sf_hb_secret,
	    sizeof (fp->sf_hb_secret));
	DTRACE_PROBE3(sctp_cwnd_ssthresh_update, sctp_faddr_t *, fp,
	    uint32_t, fp->sf_cwnd, uint32_t, fp->sf_ssthresh);

	sctp_get_dest(sctp, fp);

	sctp_cong_init(sctp, psctp, fp, NULL);
}

void
sctp_faddr_init(void)
{
	sctp_kmem_faddr_cache = kmem_cache_create("sctp_faddr_cache",
	    sizeof (sctp_faddr_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
}

void
sctp_faddr_fini(void)
{
	kmem_cache_destroy(sctp_kmem_faddr_cache);
}

/*
 * Initialize congestion control for the faddr.
 * Allocate resources and get algorithm handle if necessary.
 */
void
sctp_cong_init(sctp_t *sctp, sctp_t *psctp, sctp_faddr_t *fp, const char *alg)
{
	sctp_stack_t		*sctps = sctp->sctp_sctps;
	tcpcong_args_t		*args = &fp->sf_cong_args;
	tcpcong_list_ent_t	*tce;
	tcpcong_handle_t	hdl;
	tcpcong_ops_t		*ops;
	size_t			state_size;

	/* only one of them can be set to something, not both */
	ASSERT(psctp == NULL || alg == NULL);

	/* if alg not specified, inherit from parent or pick stack's default */
	if (alg == NULL) {
		if (psctp != NULL)
			alg = psctp->sctp_cong_alg_name;
		else
			alg = sctps->sctps_cong_default;
	}
	if (fp->sf_cong_ops != NULL &&
	    strcmp(alg, fp->sf_cong_ops->co_name) == 0)
		return;

	/* load requested alg, use builtin as last resort */
	if ((hdl = tcpcong_lookup(alg, &ops)) == NULL) {
		if (strcmp(alg, TCPCONG_ALG_BUILTIN) != 0)
			hdl = tcpcong_lookup(TCPCONG_ALG_BUILTIN, &ops);
	}
	ASSERT(hdl != NULL && ops != NULL);

	/* release previously allocated alg if necessary */
	if (fp->sf_cong_hdl != NULL)
		sctp_cong_destroy(fp);
	fp->sf_cong_hdl = hdl;
	fp->sf_cong_ops = ops;

	/* allocate algorithm's private state */
	state_size = SCTP_CONG_STATE_SIZE(fp);
	if (state_size <= sizeof (fp->sf_cong_state_buf)) {
		fp->sf_cong_args.ca_state = fp->sf_cong_state_buf;
	} else {
		fp->sf_cong_args.ca_state = kmem_zalloc(state_size, KM_SLEEP);
	}

	/* get the private property table pointer */
	mutex_enter(&sctps->sctps_cong_lock);
	for (tce = list_head(&sctps->sctps_cong_enabled); tce != NULL; ) {
		if (strcmp(fp->sf_cong_ops->co_name, tce->tce_name) == 0) {
			args->ca_propinfo = tce->tce_propinfo;
			break;
		}
		tce = list_next(&sctps->sctps_cong_enabled, tce);
	}
	mutex_exit(&sctps->sctps_cong_lock);

	args->ca_flags = TCPCONG_PROTO_SCTP;
	args->ca_ssthresh = &fp->sf_ssthresh;
	args->ca_cwnd = &fp->sf_cwnd;
	args->ca_cwnd_cnt = &fp->sf_cwnd_cnt;
	args->ca_cwnd_max = &sctp->sctp_cwnd_max;
	args->ca_mss = &fp->sf_pmss;
	args->ca_bytes_acked = &fp->sf_acked;
	args->ca_flight_size = &fp->sf_suna;
	args->ca_dupack_cnt = &fp->sf_dupack_cnt;
	args->ca_srtt = &fp->sf_srtt;
	args->ca_rttdev = &fp->sf_rttvar;

	SCTP_CONG_STATE_INIT(fp);
}

/*
 * Release congestion control resources.
 */
void
sctp_cong_destroy(sctp_faddr_t *fp)
{
	if (fp->sf_cong_hdl == NULL)
		return;

	SCTP_CONG_STATE_FINI(fp);
	if (fp->sf_cong_args.ca_state != fp->sf_cong_state_buf) {
		kmem_free(fp->sf_cong_args.ca_state,
		    SCTP_CONG_STATE_SIZE(fp));
	}
	tcpcong_unref(fp->sf_cong_hdl);
	fp->sf_cong_hdl = NULL;
	fp->sf_cong_ops = NULL;
}

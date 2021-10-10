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

/*
 * MAC Services Module - misc utilities
 */

#include <sys/types.h>
#include <sys/mac.h>
#include <sys/mac_impl.h>
#include <sys/mac_client_priv.h>
#include <sys/mac_client_impl.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/vlan.h>
#include <sys/pattr.h>
#include <sys/pci_tools.h>
#include <inet/ip.h>
#include <inet/ip_impl.h>
#include <inet/ip6.h>
#include <netinet/tcp.h>
#include <inet/tcp.h>
#include <sys/vtrace.h>
#include <sys/dlpi.h>
#include <sys/sunndi.h>
#include <inet/ipsec_impl.h>
#include <inet/sadb.h>
#include <inet/ipsecesp.h>
#include <inet/ipsecah.h>

/*
 * Copy an mblk, preserving its hardware checksum flags.
 */
static mblk_t *
mac_copymsg_cksum(mblk_t *mp)
{
	mblk_t *mp1;
	uint32_t start, stuff, end, value, flags;

	mp1 = copymsg(mp);
	if (mp1 == NULL)
		return (NULL);

	hcksum_retrieve(mp, NULL, NULL, &start, &stuff, &end, &value, &flags);
	(void) hcksum_assoc(mp1, NULL, NULL, start, stuff, end, value,
	    flags, KM_NOSLEEP);

	return (mp1);
}

/*
 * Copy an mblk chain, presenting the hardware checksum flags of the
 * individual mblks.
 */
mblk_t *
mac_copymsgchain_cksum(mblk_t *mp)
{
	mblk_t *nmp = NULL;
	mblk_t **nmpp = &nmp;

	for (; mp != NULL; mp = mp->b_next) {
		if ((*nmpp = mac_copymsg_cksum(mp)) == NULL) {
			freemsgchain(nmp);
			return (NULL);
		}

		nmpp = &((*nmpp)->b_next);
	}

	return (nmp);
}


/*
 * Process the specified mblk chain for proper handling of hardware
 * checksum offload. This routine is invoked for loopback traffic
 * between MAC clients.
 * The function handles a NULL mblk chain passed as argument.
 */
mblk_t *
mac_fix_cksum(mblk_t *mp_chain, boolean_t hash, uint16_t hash_val)
{
	mblk_t *mp, *prev = NULL, *new_chain = mp_chain, *mp1, *hashmp;
	uint32_t flags, start, stuff, end, value;

	for (mp = mp_chain; mp != NULL; prev = mp, mp = mp->b_next) {
		uint16_t len;
		uint32_t offset;
		struct ether_header *ehp;
		uint16_t sap;

		hcksum_retrieve(mp, NULL, NULL, &start, &stuff, &end, &value,
		    &flags);
		if (flags == 0)
			continue;

		/*
		 * Since the processing of checksum offload for loopback
		 * traffic requires modification of the packet contents,
		 * ensure sure that we are always modifying our own copy.
		 */
		if (DB_REF(mp) > 1) {
			mp1 = copymsg(mp);
			if (mp1 == NULL)
				continue;
			mp1->b_next = mp->b_next;
			mp->b_next = NULL;
			freemsg(mp);
			if (prev != NULL)
				prev->b_next = mp1;
			else
				new_chain = mp1;
			mp = mp1;
		}

		/*
		 * Ethernet, and optionally VLAN header.
		 */
		/* LINTED: improper alignment cast */
		ehp = (struct ether_header *)mp->b_rptr;
		if (ntohs(ehp->ether_type) == VLAN_TPID) {
			struct ether_vlan_header *evhp;

			ASSERT(MBLKL(mp) >= sizeof (struct ether_vlan_header));
			/* LINTED: improper alignment cast */
			evhp = (struct ether_vlan_header *)mp->b_rptr;
			sap = ntohs(evhp->ether_type);
			offset = sizeof (struct ether_vlan_header);
		} else {
			sap = ntohs(ehp->ether_type);
			offset = sizeof (struct ether_header);
		}

		/*
		 * This mp will have the hash value stashed, we keep
		 * track of this as mp might proceed ahead.
		 */
		hashmp = mp;

		if (MBLKL(mp) <= offset) {
			offset -= MBLKL(mp);
			if (mp->b_cont == NULL) {
				/* corrupted packet, skip it */
				if (prev != NULL)
					prev->b_next = mp->b_next;
				else
					new_chain = mp->b_next;
				mp1 = mp->b_next;
				mp->b_next = NULL;
				freemsg(mp);
				mp = mp1;
				continue;
			}
			mp = mp->b_cont;
		}

		if (flags & (HCK_FULLCKSUM | HCK_IPV4_HDRCKSUM)) {
			ipha_t *ipha = NULL;

			/*
			 * In order to compute the full and header
			 * checksums, we need to find and parse
			 * the IP and/or ULP headers.
			 */

			sap = (sap < ETHERTYPE_802_MIN) ? 0 : sap;

			/*
			 * IP header.
			 */
			if (sap != ETHERTYPE_IP)
				continue;

			ASSERT(MBLKL(mp) >= offset + sizeof (ipha_t));
			/* LINTED: improper alignment cast */
			ipha = (ipha_t *)(mp->b_rptr + offset);

			if (flags & HCK_FULLCKSUM) {
				ipaddr_t src, dst;
				uint32_t cksum;
				uint16_t *up;
				uint8_t proto;

				/*
				 * Pointer to checksum field in ULP header.
				 */
				proto = ipha->ipha_protocol;
				ASSERT(ipha->ipha_version_and_hdr_length ==
				    IP_SIMPLE_HDR_VERSION);

				switch (proto) {
				case IPPROTO_TCP:
					/* LINTED: improper alignment cast */
					up = IPH_TCPH_CHECKSUMP(ipha,
					    IP_SIMPLE_HDR_LENGTH);
					break;

				case IPPROTO_UDP:
					/* LINTED: improper alignment cast */
					up = IPH_UDPH_CHECKSUMP(ipha,
					    IP_SIMPLE_HDR_LENGTH);
					break;

				default:
					cmn_err(CE_WARN, "mac_fix_cksum: "
					    "unexpected protocol: %d", proto);
					continue;
				}

				/*
				 * Pseudo-header checksum.
				 */
				src = ipha->ipha_src;
				dst = ipha->ipha_dst;
				len = ntohs(ipha->ipha_length) -
				    IP_SIMPLE_HDR_LENGTH;

				cksum = (dst >> 16) + (dst & 0xFFFF) +
				    (src >> 16) + (src & 0xFFFF);
				cksum += htons(len);

				/*
				 * The checksum value stored in the packet needs
				 * to be correct. Compute it here.
				 */
				*up = 0;
				cksum += (((proto) == IPPROTO_UDP) ?
				    IP_UDP_CSUM_COMP : IP_TCP_CSUM_COMP);
				cksum = IP_CSUM(mp, IP_SIMPLE_HDR_LENGTH +
				    offset, cksum);
				*(up) = (uint16_t)(cksum ? cksum : ~cksum);

				/*
				 * Flag the packet so that it appears
				 * that the checksum has already been
				 * verified by the hardware.
				 */
				flags &= ~HCK_FULLCKSUM;
				flags |= HCK_FULLCKSUM_OK;
				value = 0;
			}

			if (flags & HCK_IPV4_HDRCKSUM) {
				ASSERT(ipha != NULL);
				ipha->ipha_hdr_checksum =
				    (uint16_t)ip_csum_hdr(ipha);
				flags &= ~HCK_IPV4_HDRCKSUM;
				flags |= HCK_IPV4_HDRCKSUM_OK;

			}
		}

		if (flags & HCK_PARTIALCKSUM) {
			uint16_t *up, partial, cksum;
			uchar_t *ipp; /* ptr to beginning of IP header */

			if (mp->b_cont != NULL) {
				mblk_t *mp1;

				mp1 = msgpullup(mp, offset + end);
				if (mp1 == NULL)
					continue;

				mp1->b_next = mp->b_next;
				mp->b_next = NULL;
				freemsg(mp);
				if (prev != NULL)
					prev->b_next = mp1;
				else
					new_chain = mp1;
				mp = mp1;
				hashmp = mp;
			}

			ipp = mp->b_rptr + offset;
			/* LINTED: cast may result in improper alignment */
			up = (uint16_t *)((uchar_t *)ipp + stuff);
			partial = *up;
			*up = 0;

			cksum = IP_BCSUM_PARTIAL(mp->b_rptr + offset + start,
			    end - start, partial);
			cksum = ~cksum;
			*up = cksum ? cksum : ~cksum;

			/*
			 * Since we already computed the whole checksum,
			 * indicate to the stack that it has already
			 * been verified by the hardware.
			 */
			flags &= ~HCK_PARTIALCKSUM;
			flags |= HCK_FULLCKSUM_OK;
			value = 0;
		}

		(void) hcksum_assoc(mp, NULL, NULL, start, stuff, end,
		    value, flags, KM_NOSLEEP);
		/* Stash the hash value, if required. */
		if (hash) {
			DB_CKSUMFLAGS(hashmp) |= HW_HASH;
			DB_HASH(hashmp) = hash_val;
		}
	}
#ifdef	_DEBUG
	/*
	 * Verify that the packets have the hash saved, if
	 * provided.
	 */
	if (hash) {
		for (mp = new_chain; mp != NULL; mp = mp->b_next) {
			if ((DB_CKSUMFLAGS(mp) & HW_HASH) == 0 ||
			    DB_HASH(mp) != hash_val) {
				cmn_err(CE_PANIC,
				"==> mac_fix_cksum: mp %p %p, "
				" hash %u", mp, new_chain, hash_val);
			}
		}
	}
#endif
	return (new_chain);
}

/*
 * Add VLAN tag to the specified mblk.
 */
mblk_t *
mac_add_vlan_tag(mblk_t *mp, uint_t pri, uint16_t vid)
{
	mblk_t *hmp;
	struct ether_vlan_header *evhp;
	struct ether_header *ehp;
	uint32_t start, stuff, end, value, flags, mss;
	ushort_t tci;

	ehp = (struct ether_header *)mp->b_rptr;
	if (ntohs(ehp->ether_type) == ETHERTYPE_VLAN) {
		/*
		 * This packet is already a VLAN packet, we don't want to put
		 * a new header to the mblk because:
		 *	- The mblk if from a VLAN VNIC.
		 *	- We already put a VLAN tagged mblk for this mblk, but
		 *	  MAC_TX() failed to send it out.
		 */
		evhp = (struct ether_vlan_header *)mp->b_rptr;
		tci = ntohs(evhp->ether_tci);
		if (VLAN_TCI(pri, 0, vid) != tci) {
			evhp->ether_tci = htons(VLAN_TCI(pri, 0, vid));
		}
		return (mp);
	}

	/*
	 * Allocate an mblk for the new tagged ethernet header,
	 * and copy the MAC addresses and ethertype from the
	 * original header.
	 */

	hmp = allocb(sizeof (struct ether_vlan_header), BPRI_MED);
	if (hmp == NULL) {
		freemsg(mp);
		return (NULL);
	}

	evhp = (struct ether_vlan_header *)hmp->b_rptr;

	bcopy(ehp, evhp, (ETHERADDRL * 2));
	evhp->ether_type = ehp->ether_type;
	evhp->ether_tpid = htons(ETHERTYPE_VLAN);

	hmp->b_wptr += sizeof (struct ether_vlan_header);
	mp->b_rptr += sizeof (struct ether_header);

	hcksum_retrieve(mp, NULL, NULL, &start, &stuff, &end, &value, &flags);
	(void) hcksum_assoc(hmp, NULL, NULL, start, stuff, end, value, flags,
	    KM_NOSLEEP);

	/*
	 * Retrieve LSO info from the packet.
	 */
	mac_lso_get(mp, &mss, &flags);
	if ((flags & HW_LSO) != 0)
		lso_info_set(hmp, mss, flags);

	/*
	 * Free the original message if it's now empty. Link the
	 * rest of messages to the header message.
	 */
	if (MBLKL(mp) == 0) {
		hmp->b_cont = mp->b_cont;
		freeb(mp);
	} else {
		hmp->b_cont = mp;
	}
	ASSERT(MBLKL(hmp) >= sizeof (struct ether_vlan_header));

	/*
	 * Initialize the new TCI (Tag Control Information).
	 */
	evhp->ether_tci = htons(VLAN_TCI(pri, 0, vid));

	return (hmp);
}

/*
 * Adds a VLAN tag with the specified VID and priority to each mblk of
 * the specified chain.
 */
mblk_t *
mac_add_vlan_tag_chain(mblk_t *mp_chain, uint_t pri, uint16_t vid)
{
	mblk_t *next_mp, **prev, *mp;

	mp = mp_chain;
	prev = &mp_chain;

	while (mp != NULL) {
		next_mp = mp->b_next;
		mp->b_next = NULL;
		if ((mp = mac_add_vlan_tag(mp, pri, vid)) == NULL) {
			freemsgchain(next_mp);
			break;
		}
		*prev = mp;
		prev = &mp->b_next;
		mp = mp->b_next = next_mp;
	}

	return (mp_chain);
}

/*
 * Strip VLAN tag
 */
mblk_t *
mac_strip_vlan_tag(mblk_t *mp)
{
	mblk_t *newmp;
	struct ether_vlan_header *evhp;

	evhp = (struct ether_vlan_header *)mp->b_rptr;
	if (ntohs(evhp->ether_tpid) == ETHERTYPE_VLAN) {
		ASSERT(MBLKL(mp) >= sizeof (struct ether_vlan_header));

		if (DB_REF(mp) > 1) {
			newmp = copymsg(mp);
			if (newmp == NULL)
				return (NULL);
			freemsg(mp);
			mp = newmp;
		}

		evhp = (struct ether_vlan_header *)mp->b_rptr;

		ovbcopy(mp->b_rptr, mp->b_rptr + VLAN_TAGSZ, 2 * ETHERADDRL);
		mp->b_rptr += VLAN_TAGSZ;
	}
	return (mp);
}

/*
 * Strip VLAN tag from each mblk of the chain.
 */
mblk_t *
mac_strip_vlan_tag_chain(mblk_t *mp_chain)
{
	mblk_t *mp, *next_mp, **prev;

	mp = mp_chain;
	prev = &mp_chain;

	while (mp != NULL) {
		next_mp = mp->b_next;
		mp->b_next = NULL;
		if ((mp = mac_strip_vlan_tag(mp)) == NULL) {
			freemsgchain(next_mp);
			break;
		}
		*prev = mp;
		prev = &mp->b_next;
		mp = mp->b_next = next_mp;
	}

	return (mp_chain);
}

/*
 * Default callback function. Used when the datapath is not yet initialized.
 */
/* ARGSUSED */
void
mac_pkt_drop(void *arg, void *arg2, mblk_t *mp, uint_t flags)
{
	mblk_t	*mp1 = mp;

	while (mp1 != NULL) {
		mp1->b_prev = NULL;
		mp1->b_queue = NULL;
		mp1 = mp1->b_next;
	}
	freemsgchain(mp);
}

/* ARGSUSED */
void
mac_pkt_drop_old(void *arg, mac_resource_handle_t resource, mblk_t *mp,
    boolean_t loopback)
{
	mac_pkt_drop(arg, resource, mp, 0);
}

/*
 * Determines the IPv6 header length accounting for all the optional IPv6
 * headers (hop-by-hop, destination, routing and fragment). The header length
 * and next header value (a transport header) is captured.
 *
 * Returns B_FALSE if all the IP headers are not in the same mblk otherwise
 * returns B_TRUE.
 */
boolean_t
mac_ip_hdr_length_v6(ip6_t *ip6h, uint8_t *endptr, uint16_t *hdr_length,
    uint8_t *next_hdr, ip6_frag_t **fragp)
{
	uint16_t length;
	uint_t	ehdrlen;
	uint8_t *whereptr;
	uint8_t *nexthdrp;
	ip6_dest_t *desthdr;
	ip6_rthdr_t *rthdr;
	ip6_frag_t *fraghdr;

	if (((uchar_t *)ip6h + IPV6_HDR_LEN) > endptr)
		return (B_FALSE);
	ASSERT(IPH_HDR_VERSION(ip6h) == IPV6_VERSION);
	length = IPV6_HDR_LEN;
	whereptr = ((uint8_t *)&ip6h[1]); /* point to next hdr */

	if (fragp != NULL)
		*fragp = NULL;

	nexthdrp = &ip6h->ip6_nxt;
	while (whereptr < endptr) {
		/* Is there enough left for len + nexthdr? */
		if (whereptr + MIN_EHDR_LEN > endptr)
			break;

		switch (*nexthdrp) {
		case IPPROTO_HOPOPTS:
		case IPPROTO_DSTOPTS:
			/* Assumes the headers are identical for hbh and dst */
			desthdr = (ip6_dest_t *)whereptr;
			ehdrlen = 8 * (desthdr->ip6d_len + 1);
			if ((uchar_t *)desthdr +  ehdrlen > endptr)
				return (B_FALSE);
			nexthdrp = &desthdr->ip6d_nxt;
			break;
		case IPPROTO_ROUTING:
			rthdr = (ip6_rthdr_t *)whereptr;
			ehdrlen =  8 * (rthdr->ip6r_len + 1);
			if ((uchar_t *)rthdr +  ehdrlen > endptr)
				return (B_FALSE);
			nexthdrp = &rthdr->ip6r_nxt;
			break;
		case IPPROTO_FRAGMENT:
			fraghdr = (ip6_frag_t *)whereptr;
			ehdrlen = sizeof (ip6_frag_t);
			if ((uchar_t *)&fraghdr[1] > endptr)
				return (B_FALSE);
			nexthdrp = &fraghdr->ip6f_nxt;
			if (fragp != NULL)
				*fragp = fraghdr;
			break;
		case IPPROTO_NONE:
			/* No next header means we're finished */
		default:
			*hdr_length = length;
			*next_hdr = *nexthdrp;
			return (B_TRUE);
		}
		length += ehdrlen;
		whereptr += ehdrlen;
		*hdr_length = length;
		*next_hdr = *nexthdrp;
	}
	switch (*nexthdrp) {
	case IPPROTO_HOPOPTS:
	case IPPROTO_DSTOPTS:
	case IPPROTO_ROUTING:
	case IPPROTO_FRAGMENT:
		/*
		 * If any know extension headers are still to be processed,
		 * the packet's malformed (or at least all the IP header(s) are
		 * not in the same mblk - and that should never happen.
		 */
		return (B_FALSE);

	default:
		/*
		 * If we get here, we know that all of the IP headers were in
		 * the same mblk, even if the ULP header is in the next mblk.
		 */
		*hdr_length = length;
		*next_hdr = *nexthdrp;
		return (B_TRUE);
	}
}

static int mac_pkt_get_info_inline(mac_handle_t, mblk_t *, mac_pkt_info_t *);
#pragma inline(mac_pkt_get_info_inline)

static int
mac_pkt_get_info_inline(mac_handle_t mh, mblk_t *mp, mac_pkt_info_t *info)
{
	struct ether_header	*ehp;
	mac_header_info_t	mhi;
	size_t			hdrlen;
	uint32_t		sap;
	uint16_t		vid = 0;
	mac_impl_t		*mip = (mac_impl_t *)mh;
	boolean_t		is_unicast;

	if (mh == NULL || mip->mi_info.mi_nativemedia == DL_ETHER) {
		ehp = (struct ether_header *)mp->b_rptr;
		sap = ntohs(ehp->ether_type);
		if (sap == ETHERTYPE_VLAN) {
			struct ether_vlan_header *evhp;
			mblk_t *newmp = NULL;

			hdrlen = sizeof (struct ether_vlan_header);
			if (MBLKL(mp) < hdrlen) {
				/* the vlan tag is the payload, pull up first */
				newmp = msgpullup(mp, -1);
				if ((newmp == NULL) ||
				    (MBLKL(newmp) < hdrlen)) {
					freemsg(newmp);
					return (EINVAL);
				}
				evhp = (struct ether_vlan_header *)
				    newmp->b_rptr;
			} else {
				evhp = (struct ether_vlan_header *)
				    mp->b_rptr;
			}

			sap = ntohs(evhp->ether_type);
			vid = VLAN_ID(ntohs(evhp->ether_tci));
			freemsg(newmp);
		} else {
			hdrlen = sizeof (struct ether_header);
		}
		is_unicast = ((((uint8_t *)&ehp->ether_dhost)[0] & 0x01) == 0);
	} else {
		if (mac_header_info(mh, mp, &mhi) != 0)
			return (EINVAL);

		sap = mhi.mhi_bindsap;
		hdrlen = mhi.mhi_hdrsize;
		is_unicast = (mhi.mhi_dsttype == MAC_ADDRTYPE_UNICAST);
	}
	info->pi_hdrlen = hdrlen;
	info->pi_sap = sap;
	info->pi_vid = vid;
	info->pi_unicast = is_unicast;
	return (0);
}

int
mac_pkt_get_info(mac_handle_t mh, mblk_t *mp, mac_pkt_info_t *info)
{
	return (mac_pkt_get_info_inline(mh, mp, info));
}

#define	PKT_HASH_2BYTES(x)	((x)[0] ^ (x)[1])
#define	PKT_HASH_4BYTES(x)	((x)[0] ^ (x)[1] ^ (x)[2] ^ (x)[3])
#define	PKT_HASH_MAC(x)		((x)[0] ^ (x)[1] ^ (x)[2] ^ (x)[3] ^ \
				(x)[4] ^ (x)[5])

#define	ROT_HASH(h, b)		(((h) << 5) ^ ((h) >> 27) ^ b)
#define	ROT_HASH_4BYTES(h, x)	{ \
	(h) = ROT_HASH((h), (x)[0]); \
	(h) = ROT_HASH((h), (x)[1]); \
	(h) = ROT_HASH((h), (x)[2]); \
	(h) = ROT_HASH((h), (x)[3]); \
};

uint32_t
mac_pkt_hash(mac_handle_t mh, mblk_t *mp, uint8_t policy,
    mac_pkt_info_t *pkt_info)
{
	mac_pkt_info_t		*infop, info;
	uint32_t		hash = 0;
	uint16_t		sap;
	uint_t			skip_len;
	uint8_t			proto;
	boolean_t		ip_fragmented;
	mac_impl_t		*mip = (mac_impl_t *)mh;

	ASSERT(policy != 0);
	if (!IS_P2ALIGNED(mp->b_rptr, sizeof (uint16_t)))
		return (0);

	/* compute L2 hash */
	if ((policy & MAC_PKT_HASH_L2) != 0) {
		uchar_t			*mac_src, *mac_dst;
		struct ether_header	*ehp;

		if (mip == NULL || mip->mi_info.mi_nativemedia == DL_ETHER) {
			ehp = (struct ether_header *)mp->b_rptr;
			mac_src = ehp->ether_shost.ether_addr_octet;
			mac_dst = ehp->ether_dhost.ether_addr_octet;

			hash = PKT_HASH_MAC(mac_src) ^ PKT_HASH_MAC(mac_dst);
		}
		policy &= ~MAC_PKT_HASH_L2;
		if (policy == 0)
			goto done;
	}

	/* skip L2 header */
	infop = pkt_info != NULL ? pkt_info : &info;
	if (mac_pkt_get_info_inline(mh, mp, infop) != 0)
		return (0);

	skip_len = infop->pi_hdrlen;
	sap = infop->pi_sap;

	/* if ethernet header is in its own mblk, skip it */
	if (MBLKL(mp) <= skip_len) {
		skip_len -= MBLKL(mp);
		mp = mp->b_cont;
		if (mp == NULL)
			goto done;
	}

	sap = (sap < ETHERTYPE_802_MIN) ? 0 : sap;

	/* compute IP src/dst addresses hash and skip IPv{4,6} header */

	switch (sap) {
	case ETHERTYPE_IP: {
		ipha_t *iphp;

		/*
		 * If the header is not aligned or the header doesn't fit
		 * in the mblk, bail now. Note that this may cause packets
		 * reordering.
		 */
		iphp = (ipha_t *)(mp->b_rptr + skip_len);
		if (((unsigned char *)iphp + sizeof (ipha_t) > mp->b_wptr) ||
		    !OK_32PTR((char *)iphp))
			goto done;

		proto = iphp->ipha_protocol;
		skip_len += IPH_HDR_LENGTH(iphp);

		/* Check if the packet is fragmented. */
		ip_fragmented = ntohs(iphp->ipha_fragment_offset_and_flags) &
		    IPH_OFFSET;

		/*
		 * For fragmented packets, use addresses in addition to
		 * the frag_id to generate the hash inorder to get
		 * better distribution.
		 */
		if (ip_fragmented || (policy & MAC_PKT_HASH_L3) != 0) {
			uint8_t *ip_src = (uint8_t *)&(iphp->ipha_src);
			uint8_t *ip_dst = (uint8_t *)&(iphp->ipha_dst);

			hash ^= (PKT_HASH_4BYTES(ip_src) ^
			    PKT_HASH_4BYTES(ip_dst));
			policy &= ~MAC_PKT_HASH_L3;
		}

		if (ip_fragmented) {
			uint8_t *identp = (uint8_t *)&iphp->ipha_ident;
			hash ^= PKT_HASH_2BYTES(identp);
			goto done;
		}
		break;
	}
	case ETHERTYPE_IPV6: {
		ip6_t *ip6hp;
		ip6_frag_t *frag = NULL;
		uint16_t hdr_length;

		/*
		 * If the header is not aligned or the header doesn't fit
		 * in the mblk, bail now. Note that this may cause packets
		 * reordering.
		 */

		ip6hp = (ip6_t *)(mp->b_rptr + skip_len);
		if (((unsigned char *)ip6hp + IPV6_HDR_LEN > mp->b_wptr) ||
		    !OK_32PTR((char *)ip6hp))
			goto done;

		if (!mac_ip_hdr_length_v6(ip6hp, mp->b_wptr, &hdr_length,
		    &proto, &frag))
			goto done;
		skip_len += hdr_length;

		/*
		 * For fragmented packets, use addresses in addition to
		 * the frag_id to generate the hash inorder to get
		 * better distribution.
		 */
		if (frag != NULL || (policy & MAC_PKT_HASH_L3) != 0) {
			uint8_t *ip_src = &(ip6hp->ip6_src.s6_addr8[12]);
			uint8_t *ip_dst = &(ip6hp->ip6_dst.s6_addr8[12]);

			hash ^= (PKT_HASH_4BYTES(ip_src) ^
			    PKT_HASH_4BYTES(ip_dst));
			policy &= ~MAC_PKT_HASH_L3;
		}

		if (frag != NULL) {
			uint8_t *identp = (uint8_t *)&frag->ip6f_ident;
			hash ^= PKT_HASH_4BYTES(identp);
			goto done;
		}
		break;
	}
	default:
		goto done;
	}

	if (policy == 0)
		goto done;

	/* if ip header is in its own mblk, skip it */
	if (MBLKL(mp) <= skip_len) {
		skip_len -= MBLKL(mp);
		mp = mp->b_cont;
		if (mp == NULL)
			goto done;
	}

	/* parse ULP header */
again:
	switch (proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_ESP:
	case IPPROTO_SCTP:
		/*
		 * These Internet Protocols are intentionally designed
		 * for hashing from the git-go.  Port numbers are in the first
		 * word for transports, SPI is first for ESP.
		 */
		if (mp->b_rptr + skip_len + 4 > mp->b_wptr)
			goto done;

		ROT_HASH_4BYTES(hash, (mp->b_rptr + skip_len));
		break;

	case IPPROTO_AH: {
		ah_t *ah = (ah_t *)(mp->b_rptr + skip_len);
		uint_t ah_length = AH_TOTAL_LEN(ah);

		if ((unsigned char *)ah + sizeof (ah_t) > mp->b_wptr)
			goto done;

		proto = ah->ah_nexthdr;
		skip_len += ah_length;

		/* if AH header is in its own mblk, skip it */
		if (MBLKL(mp) <= skip_len) {
			skip_len -= MBLKL(mp);
			mp = mp->b_cont;
			if (mp == NULL)
				goto done;
		}

		goto again;
	}
	}

done:
	return (hash);
}

/*
 * The input message may include LSO information, if so, go to soft LSO
 * logic to eliminate the oversized LSO packet.
 * Free the original LSO message.
 * Does not handle a chain of packets.
 */
mblk_t *
mac_do_softlso(mblk_t *mp, uint32_t mss, size_t *pktsize, uint_t *cnt)
{
	uint32_t	hckflags;
	int		pktlen, hdrlen, segnum, i;
	struct ether_vlan_header *evh;
	int		aehlen, ehlen, iphlen, tcphlen;
	struct ip	*oiph, *niph;
	struct tcphdr 	*otcph, *ntcph;
	int		available, len, left;
	uint16_t	ip_id;
	uint32_t	tcp_seq;
	mblk_t		*datamp;
	uchar_t		*rptr;
	mblk_t		*nmp, *cmp, *mp_chain;
	boolean_t 	do_cleanup = B_FALSE;
	t_uscalar_t 	start_offset = 0, stuff_offset = 0, value = 0, end = 0;
	uint16_t	l4_len;
	ipaddr_t	src, dst;
	uint32_t	cksum, sum, l4cksum;
	uint_t		plen;

	*pktsize = 0;
	*cnt = 0;

	/*
	 * check the length of LSO packet payload and calculate the number of
	 * segments to be generated.
	 */
	pktlen = msgsize(mp);

	/*
	 * Ethernet header should be present.
	 */
	if (MBLKL(mp) < (sizeof (struct ether_header))) {
		DTRACE_PROBE2(softlso__drop1, size_t, pktlen, mblk_t *, mp);
		freemsg(mp);
		return (NULL);
	}
	evh = (struct ether_vlan_header *)mp->b_rptr;

	/* VLAN? */
	if (evh->ether_tpid == htons(ETHERTYPE_VLAN))
		ehlen = sizeof (struct ether_vlan_header);
	else
		ehlen = sizeof (struct ether_header);

	/* Minimum IP header should be present */
	if (MBLKL(mp) < (ehlen + IP_SIMPLE_HDR_LENGTH)) {
		DTRACE_PROBE2(softlso__drop2, size_t, pktlen, mblk_t *, mp);
		freemsg(mp);
		return (NULL);
	}
	oiph = (struct ip *)(mp->b_rptr + ehlen);
	if (!OK_32PTR(oiph)) {
		DTRACE_PROBE2(softlso__drop3, size_t, pktlen, mblk_t *, mp);
		freemsg(mp);
		return (NULL);
	}
	iphlen = oiph->ip_hl * 4;

	l4_len = pktlen - ehlen - iphlen;

	/*
	 * If it is not IPv4 or TCP or if there is IP length mismatch or
	 * if we don't have minimum TCP header, drop.
	 */
	if ((oiph->ip_v != IPV4_VERSION) || (oiph->ip_p != IPPROTO_TCP) ||
	    (ntohs(oiph->ip_len) != pktlen - ehlen) ||
	    (MBLKL(mp) < (ehlen + iphlen + TCP_MIN_HEADER_LENGTH))) {
		DTRACE_PROBE2(softlso__drop4, size_t, pktlen, mblk_t *, mp);
		freemsg(mp);
		return (NULL);
	}

	otcph = (struct tcphdr *)(mp->b_rptr + ehlen + iphlen);
	tcphlen = otcph->th_off * 4;

	/* TCP flags can not include URG, RST, or SYN */
	VERIFY((otcph->th_flags & (TH_SYN | TH_RST | TH_URG)) == 0);

	hdrlen = ehlen + iphlen + tcphlen;

	VERIFY(MBLKL(mp) >= hdrlen);

	if (MBLKL(mp) > hdrlen) {
		datamp = mp;
		rptr = mp->b_rptr + hdrlen;
	} else { /* = */
		datamp = mp->b_cont;
		rptr = datamp->b_rptr;
	}

	hckflags = 0;
	hcksum_retrieve(mp, NULL, NULL,
	    &start_offset, &stuff_offset, &value, &end, &hckflags);

	dst = oiph->ip_dst.s_addr;
	src = oiph->ip_src.s_addr;

	cksum = (dst >> 16) + (dst & 0xFFFF) + (src >> 16) + (src & 0xFFFF);
	l4cksum = cksum + IP_TCP_CSUM_COMP;

	sum = l4_len + l4cksum;
	sum = (sum & 0xFFFF) + (sum >> 16);

	/*
	 * Start to process.
	 */
	available = pktlen - hdrlen;
	segnum = (available - 1) / mss + 1;

	*cnt = segnum;
	VERIFY(segnum >= 2);

	/*
	 * Try to pre-allocate all header messages
	 */
	mp_chain = NULL;
	/* 8-byte alignment */
	aehlen = ehlen & ~0x7;
	aehlen += 0x8;
	for (i = 0; i < segnum; i++) {
		if ((nmp = allocb(aehlen + hdrlen, 0)) == NULL) {
			freemsgchain(mp_chain);
			freemsg(mp);
			return (NULL);
		}
		nmp->b_rptr += (aehlen - ehlen);
		nmp->b_next = mp_chain;
		mp_chain = nmp;
	}

	/*
	 * Associate payload with new packets
	 */
	cmp = mp_chain;
	left = available;
	while (cmp != NULL) {
		nmp = dupb(datamp);
		if (nmp == NULL) {
			do_cleanup = B_TRUE;
			goto cleanup_msgs;
		}

		cmp->b_cont = nmp;
		nmp->b_rptr = rptr;
		len = (left < mss) ? left : mss;
		left -= len;

		len -= MBLKL(nmp);
		while (len > 0) {
			mblk_t *mmp = NULL;

			if (datamp->b_cont != NULL) {
				datamp = datamp->b_cont;
				rptr = datamp->b_rptr;
				mmp = dupb(datamp);
				if (mmp == NULL) {
					do_cleanup = B_TRUE;
					goto cleanup_msgs;
				}
			} else {
				cmn_err(CE_PANIC,
				    "==> mac_do_softlso: "
				    "Pointers must have been corrupted!\n"
				    "datamp: $%p, nmp: $%p, rptr: $%p",
				    (void *)datamp,
				    (void *)nmp,
				    (void *)rptr);
			}
			nmp->b_cont = mmp;
			nmp = mmp;
			len -= MBLKL(nmp);
		}
		if (len < 0) {
			nmp->b_wptr += len;
			rptr = nmp->b_wptr;

		} else if (len == 0) {
			if (datamp->b_cont != NULL) {
				datamp = datamp->b_cont;
				rptr = datamp->b_rptr;
			} else {
				VERIFY(cmp->b_next == NULL);
				VERIFY(left == 0);
				break; /* Done! */
			}
		}
		cmp = cmp->b_next;
	}

	/*
	 * From now, start to fill up all headers for the first message
	 * Hardware checksum flags need to be updated separately for FULLCKSUM
	 * and PARTIALCKSUM cases. For full checksum, copy the original flags
	 * into every new packet is enough. But for HCK_PARTIALCKSUM, all
	 * required fields need to be updated properly.
	 */
	nmp = mp_chain;
	bcopy(mp->b_rptr, nmp->b_rptr, hdrlen);
	nmp->b_wptr = nmp->b_rptr + hdrlen;
	niph = (struct ip *)(nmp->b_rptr + ehlen);
	plen = mss + iphlen + tcphlen;
	niph->ip_len = htons(plen);
	ip_id = ntohs(niph->ip_id);
	ntcph = (struct tcphdr *)(nmp->b_rptr + ehlen + iphlen);
	tcp_seq = ntohl(ntcph->th_seq);

	ntcph->th_flags &= ~(TH_FIN | TH_PUSH | TH_RST);

	DB_CKSUMFLAGS(nmp) = (uint16_t)hckflags;
	DB_CKSUMSTART(nmp) = start_offset;
	DB_CKSUMSTUFF(nmp) = stuff_offset;
	DB_CKSUMEND(nmp) = plen;
	pktsize += msgdsize(nmp);

	/* calculate IP checksum and TCP pseudo header checksum */
	niph->ip_sum = 0;
	if ((hckflags & HCK_PARTIALCKSUM) != 0) {
		l4_len = mss + tcphlen;
		sum = htons(l4_len) + l4cksum;
		sum = (sum & 0xFFFF) + (sum >> 16);
		ntcph->th_sum = (sum & 0xffff);
	}
	if ((hckflags & HCK_IPV4_HDRCKSUM) == 0)
		niph->ip_sum = ip_csum_hdr((ipha_t *)niph);
	cmp = nmp;
	while ((nmp = nmp->b_next)->b_next != NULL) {
		bcopy(cmp->b_rptr, nmp->b_rptr, hdrlen);
		nmp->b_wptr = nmp->b_rptr + hdrlen;
		niph = (struct ip *)(nmp->b_rptr + ehlen);
		niph->ip_id = htons(++ip_id);
		plen = mss + iphlen + tcphlen;
		niph->ip_len = htons(plen);
		ntcph = (struct tcphdr *)(nmp->b_rptr + ehlen + iphlen);
		tcp_seq += mss;

		ntcph->th_flags &= ~(TH_FIN | TH_PUSH | TH_RST | TH_URG);
		ntcph->th_seq = htonl(tcp_seq);

		DB_CKSUMFLAGS(nmp) = (uint16_t)hckflags;
		DB_CKSUMSTART(nmp) = start_offset;
		DB_CKSUMSTUFF(nmp) = stuff_offset;
		DB_CKSUMEND(nmp) = plen;
		pktsize += msgdsize(nmp);

		/* calculate IP checksum and TCP pseudo header checksum */
		niph->ip_sum = 0;
		if ((hckflags & HCK_PARTIALCKSUM) != 0)
			ntcph->th_sum = (sum & 0xffff);
		if ((hckflags & HCK_IPV4_HDRCKSUM) == 0)
			niph->ip_sum = ip_csum_hdr((ipha_t *)niph);
	}

	/* Last segment */
	/*
	 * Set FIN and/or PSH flags if present only in the last packet.
	 * The ip_len could be different from prior packets.
	 */
	bcopy(cmp->b_rptr, nmp->b_rptr, hdrlen);
	nmp->b_wptr = nmp->b_rptr + hdrlen;
	niph = (struct ip *)(nmp->b_rptr + ehlen);
	niph->ip_id = htons(++ip_id);
	plen = msgsize(nmp->b_cont) + iphlen + tcphlen;
	niph->ip_len = htons(plen);
	ntcph = (struct tcphdr *)(nmp->b_rptr + ehlen + iphlen);
	tcp_seq += mss;
	ntcph->th_seq = htonl(tcp_seq);
	ntcph->th_flags = (otcph->th_flags & ~TH_URG);

	DB_CKSUMFLAGS(nmp) = (uint16_t)hckflags;
	DB_CKSUMSTART(nmp) = start_offset;
	DB_CKSUMSTUFF(nmp) = stuff_offset;
	DB_CKSUMEND(nmp) = plen;
	pktsize += msgdsize(nmp);

	/* calculate IP checksum and TCP pseudo header checksum */
	niph->ip_sum = 0;
	if ((hckflags & HCK_PARTIALCKSUM) != 0) {
		l4_len = ntohs(niph->ip_len) - iphlen;
		sum = htons(l4_len) + l4cksum;
		sum = (sum & 0xFFFF) + (sum >> 16);
		ntcph->th_sum = (sum & 0xffff);
	}
	if ((hckflags & HCK_IPV4_HDRCKSUM) == 0)
		niph->ip_sum = ip_csum_hdr((ipha_t *)niph);

cleanup_msgs:
	if (do_cleanup)
		freemsgchain(mp_chain);

	/*
	 * We're done here, so just free the original message and return the
	 * new message chain, that could be NULL if failed, back to the caller.
	 */
	freemsg(mp);

	return (mp_chain);
}

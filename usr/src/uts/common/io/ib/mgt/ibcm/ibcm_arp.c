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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/ib/mgt/ibcm/ibcm_arp.h>

extern char cmlog[];

extern int ibcm_resolver_pr_lookup(ibcm_arp_streams_t *ib_s,
    ibt_ip_addr_t *dst_addr, ibt_ip_addr_t *src_addr, zoneid_t myzoneid);
extern void ibcm_arp_delete_prwqn(ibcm_arp_prwqn_t *wqnp);

_NOTE(SCHEME_PROTECTS_DATA("Unshared data", ibt_ip_addr_s))
_NOTE(SCHEME_PROTECTS_DATA("Unshared data", ibcm_arp_prwqn_t))
_NOTE(SCHEME_PROTECTS_DATA("Unshared data", sockaddr_in))
_NOTE(SCHEME_PROTECTS_DATA("Unshared data", sockaddr_in6))

int ibcm_printip = 0;

/*
 * Function:
 *	ibcm_ip_print
 * Input:
 *	label		Arbitrary qualifying string
 *	ipa		Pointer to IP Address to print
 */
void
ibcm_ip_print(char *label, ibt_ip_addr_t *ipaddr)
{
	char    buf[INET6_ADDRSTRLEN];

	if (ipaddr->family == AF_INET) {
		IBTF_DPRINTF_L2(cmlog, "%s: %s", label,
		    inet_ntop(AF_INET, &ipaddr->un.ip4addr, buf, sizeof (buf)));
	} else if (ipaddr->family == AF_INET6) {
		IBTF_DPRINTF_L2(cmlog, "%s: %s", label, inet_ntop(AF_INET6,
		    &ipaddr->un.ip6addr, buf, sizeof (buf)));
	} else {
		IBTF_DPRINTF_L2(cmlog, "%s: IP ADDR NOT SPECIFIED ", label);
	}
}


ibt_status_t
ibcm_arp_get_ibaddr(zoneid_t myzoneid, ibt_ip_addr_t srcaddr,
    ibt_ip_addr_t destaddr, ibcm_ibaddr_t *ibaddrp)
{
	ibcm_arp_streams_t	*ib_s;
	ibcm_arp_prwqn_t	*wqnp;
	int			ret = 0;
	int			len;

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_get_ibaddr(%d, %p, %p, %p)",
	    myzoneid, srcaddr, destaddr, ibaddrp);

	ib_s = (ibcm_arp_streams_t *)kmem_zalloc(sizeof (ibcm_arp_streams_t),
	    KM_SLEEP);

	mutex_init(&ib_s->lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&ib_s->cv, NULL, CV_DRIVER, NULL);

	mutex_enter(&ib_s->lock);
	ib_s->done = B_FALSE;
	mutex_exit(&ib_s->lock);

	ret = ibcm_resolver_pr_lookup(ib_s, &destaddr, &srcaddr, myzoneid);

	IBTF_DPRINTF_L3(cmlog, "ibcm_arp_get_ibaddr: ibcm_resolver_pr_lookup "
	    "returned: %d", ret);
	if (ret == 0) {
		mutex_enter(&ib_s->lock);
		while (ib_s->done != B_TRUE)
			cv_wait(&ib_s->cv, &ib_s->lock);
		mutex_exit(&ib_s->lock);
	}

	mutex_enter(&ib_s->lock);
	wqnp = ib_s->wqnp;
	if (ib_s->status == 0) {
		ibaddrp->sgid = wqnp->sgid;
		ibaddrp->dgid = wqnp->dgid;
		ibaddrp->src_mismatch = 0;
		(void) strncpy(ibaddrp->ifname, wqnp->ifname,
		    sizeof (wqnp->ifname));

		/*
		 * If the user supplied a address, then verify we got
		 * for the same address.
		 */
		if (wqnp->usrc_addr.family) {
			len = (wqnp->usrc_addr.family == AF_INET) ?
			    IP_ADDR_LEN : sizeof (in6_addr_t);
			if (bcmp(&wqnp->usrc_addr.un,
			    &wqnp->src_addr.un, len)) {
				IBTF_DPRINTF_L3(cmlog, "ibcm_arp_get_ibaddr: "
				    "srcaddr mismatch");

				/* Clean-up old data, and reset the done flag */
				ibcm_arp_delete_prwqn(wqnp);
				ib_s->done = B_FALSE;
				mutex_exit(&ib_s->lock);

				ret = ibcm_resolver_pr_lookup(ib_s, &srcaddr,
				    &srcaddr, myzoneid);
				if (ret == 0) {
					mutex_enter(&ib_s->lock);
					while (ib_s->done != B_TRUE)
						cv_wait(&ib_s->cv, &ib_s->lock);
					mutex_exit(&ib_s->lock);
				}
				mutex_enter(&ib_s->lock);
				wqnp = ib_s->wqnp;
				if (ib_s->status == 0) {
					ibaddrp->sgid = wqnp->dgid;
					ibaddrp->src_mismatch = 1;

					bcopy(&wqnp->src_addr, &ibaddrp->src_ip,
					    sizeof (ibt_ip_addr_t));

					IBTF_DPRINTF_L4(cmlog,
					    "ibcm_arp_get_ibaddr: %s, "
					    "SGID: %llX:%llX DGID: %llX:%llX",
					    wqnp->ifname,
					    ibaddrp->sgid.gid_prefix,
					    ibaddrp->sgid.gid_guid,
					    ibaddrp->dgid.gid_prefix,
					    ibaddrp->dgid.gid_guid);

					ibcm_arp_delete_prwqn(wqnp);
				} else if (ret == 0) {
					if (wqnp)
						kmem_free(wqnp,
						    sizeof (ibcm_arp_prwqn_t));
				}
				goto arp_ibaddr_done;
			}
		}

		bcopy(&wqnp->src_addr, &ibaddrp->src_ip,
		    sizeof (ibt_ip_addr_t));

		IBTF_DPRINTF_L4(cmlog, "ibcm_arp_get_ibaddr: %s \n"
		    "SGID: %llX:%llX DGID: %llX:%llX", ibaddrp->ifname,
		    ibaddrp->sgid.gid_prefix, ibaddrp->sgid.gid_guid,
		    ibaddrp->dgid.gid_prefix, ibaddrp->dgid.gid_guid);

		ibcm_arp_delete_prwqn(wqnp);
	} else if (ret == 0) {
		/*
		 * We come here only when lookup has returned empty (failed)
		 * via callback routine.
		 * i.e. ib_s->status is non-zero, while ret is zero.
		 */
		if (wqnp)
			kmem_free(wqnp, sizeof (ibcm_arp_prwqn_t));
	}
arp_ibaddr_done:
	ret = ib_s->status;
	mutex_exit(&ib_s->lock);

arp_ibaddr_error:

	mutex_destroy(&ib_s->lock);
	cv_destroy(&ib_s->cv);
	kmem_free(ib_s, sizeof (ibcm_arp_streams_t));

	if (ret)
		return (IBT_FAILURE);
	else
		return (IBT_SUCCESS);
}

ibt_status_t
ibcm_arp_get_ibds(ibt_srcip_attr_t *sattr, ibt_srcip_info_t **src_info_p,
    uint_t *entries_p)
{
	ibt_part_attr_t	*attr;
	int		nparts;
	uint8_t		i, alloc_cnt, ibd_cnt;
	char		ifname[MAXLINKNAMELEN];
	zoneid_t	zoneid;
	netstack_t	*netstack;
	ip_stack_t	*ipst;
	uint64_t	ipif_flags = 0;
	ill_t		*ill = NULL;
	sa_family_t	family;
	in6_addr_t	setsrc6, setdst6;
	ibt_srcip_info_t	*si_p;
	ibt_status_t	retval;

	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_get_ibds(%p, %p, %p)",
	    sattr, src_info_p, entries_p);
	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_get_ibds: I/P: sgid %llX, "
	    "pkey %lx, zoneid %d, family %d", sattr->sip_gid.gid_guid,
	    sattr->sip_pkey, sattr->sip_zoneid, sattr->sip_family);

	if (sattr->sip_family == AF_INET6)
		family = AF_INET6;
	else
		family = AF_INET;

	if ((ibt_get_all_part_attr(&attr, &nparts) != IBT_SUCCESS) ||
	    (nparts == 0)) {
		IBTF_DPRINTF_L2(cmlog, "ibcm_arp_get_ibds: Failed to "
		    "IB Part List - %d", nparts);
		return (IBT_SRC_IP_NOT_FOUND);
	}
	IBTF_DPRINTF_L4(cmlog, "ibcm_arp_get_ibds: Found %d IB Part List",
	    nparts);

	alloc_cnt = 0;
	for (i = 0; i < nparts; i++) {
		IBTF_DPRINTF_L4(cmlog, "ibcm_arp_get_ibds: [%d] :: "
		    "pa_portguid %llX pa_pkey %lX", i, attr[i].pa_port_guid,
		    attr[i].pa_pkey);
		if (attr[i].pa_port_guid == sattr->sip_gid.gid_guid) {

			/* Is specific pkey requested */
			if ((sattr->sip_pkey) &&
			    (attr[i].pa_pkey != sattr->sip_pkey))
				continue;

			if (sattr->sip_zoneid != ALL_ZONES) {
				/* We need check for specific zone */
				if (dls_devnet_get_active_linkname(
				    attr[i].pa_plinkid, &zoneid, ifname,
				    MAXLINKNAMELEN) != 0) {
					IBTF_DPRINTF_L3(cmlog,
					    "ibcm_arp_get_ibds: [%d] :: "
					    "dls_devnet_get_active_linkname "
					    "%d failed", i, attr[i].pa_plinkid);
					continue;
				}
				IBTF_DPRINTF_L4(cmlog, "ibcm_arp_get_ibds: "
				    "[%d] :: pa_portguid %llX pkey %X ifname"
				    " %s zoneid %d", i, attr[i].pa_port_guid,
				    attr[i].pa_pkey, ifname, zoneid);

				if (sattr->sip_zoneid != zoneid) {
					IBTF_DPRINTF_L3(cmlog,
					    "ibcm_arp_get_ibds: [%d] :: zoneid"
					    "do not match %d != %d", i,
					    sattr->sip_zoneid, zoneid);
					continue;
				} else {
					IBTF_DPRINTF_L3(cmlog,
					    "ibcm_arp_get_ibds: [%d] :: "
					    "zoneid match %d == %d", i,
					    sattr->sip_zoneid, zoneid);
				}
			}
			alloc_cnt++;
		}
	}

	IBTF_DPRINTF_L3(cmlog, "ibcm_arp_get_ibds: Matching IB Partition"
	    " %d found", alloc_cnt);
	if (alloc_cnt == 0) {
		(void) ibt_free_part_attr(attr, nparts);
		return (IBT_SRC_IP_NOT_FOUND);
	}

	/*
	 * Allocate memory for return buffer, to be freed by
	 * ibt_free_srcip_info().
	 */
	si_p = kmem_zalloc((alloc_cnt * sizeof (ibt_srcip_info_t)), KM_SLEEP);

	ibd_cnt = 0;
	for (i = 0; i < nparts; i++) {
		IBTF_DPRINTF_L4(cmlog, "ibcm_arp_get_ibds: [%d] :: "
		    "pa_portguid %llX pa_pkey %lX", i, attr[i].pa_port_guid,
		    attr[i].pa_pkey);
		if (attr[i].pa_port_guid == sattr->sip_gid.gid_guid) {

			/* Is specific pkey requested */
			if ((sattr->sip_pkey) &&
			    (attr[i].pa_pkey != sattr->sip_pkey))
				continue;

			if (dls_devnet_get_active_linkname(attr[i].pa_plinkid,
			    &zoneid, ifname, MAXLINKNAMELEN) != 0) {
				IBTF_DPRINTF_L3(cmlog, "ibcm_arp_get_ibds: "
				    "[%d] :: dls_devnet_get_active_linkname "
				    "%d failed", i, attr[i].pa_plinkid);
				continue;
			}
			IBTF_DPRINTF_L4(cmlog, "ibcm_arp_get_ibds: [%d] :: "
			    "pa_portguid %llX pkey %x ifname %s zoneid %d", i,
			    attr[i].pa_port_guid, attr[i].pa_pkey, ifname,
			    zoneid);

			/* We need check for specific zone */
			if ((sattr->sip_zoneid != ALL_ZONES) &&
			    (sattr->sip_zoneid != zoneid)) {
				IBTF_DPRINTF_L3(cmlog, "ibcm_arp_get_ibds: "
				    "[%d] :: zoneid do not match %d != %d", i,
				    sattr->sip_zoneid, zoneid);
				continue;
			}

			_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*si_p))
			si_p[ibd_cnt].ip_zoneid = zoneid;

			netstack = netstack_find_by_zoneid(zoneid);
			ipst = netstack->netstack_ip;
			/*
			 * For exclusive stacks we set the zoneid to zero
			 */
			if (netstack->netstack_stackid != GLOBAL_NETSTACKID)
				zoneid = GLOBAL_ZONEID;

			IBTF_DPRINTF_L3(cmlog, "ibcm_arp_get_ibds: [%d] :: "
			    "As per netstack: zoneid is %d", i, zoneid);

			if ((ill = ill_lookup_on_name(ifname, B_FALSE,
			    family != AF_INET, NULL, ipst)) == NULL) {
				IBTF_DPRINTF_L2(cmlog, "ibcm_arp_get_ibds: "
				    "[%d] :: failed to find ill %s", i, ifname);
				continue;
			}

			/*
			 * Pick a source address so that the receiver can
			 * determine which zone this connection comes from
			 */
			setsrc6 = ipv6_all_zeros;
			setdst6 = ipv6_loopback;
			if ((family == AF_INET &&
			    ip_select_source_v4(ill, INADDR_ANY,
			    INADDR_LOOPBACK, INADDR_ANY, zoneid, ipst,
			    &si_p[ibd_cnt].ip_addr.un.ip4addr, NULL,
			    &ipif_flags) != 0) ||
			    (family == AF_INET6 &&
			    ip_select_source_v6(ill, &setsrc6, &setdst6,
			    zoneid, ipst, B_TRUE, IPV6_PREFER_SRC_DEFAULT,
			    &si_p[ibd_cnt].ip_addr.un.ip6addr, NULL,
			    &ipif_flags) != 0)) {

				IBTF_DPRINTF_L2(cmlog, "ibcm_arp_get_ibds: "
				    "[%d] :: failed to find a valid IP "
				    "address on %s", i, ifname);
				ill_refrele(ill);
				continue;
			}
			ill_refrele(ill);

			si_p[ibd_cnt].ip_addr.family = family;
			if (ipif_flags & IPIF_DUPLICATE)
				si_p[ibd_cnt].ip_flag = IBT_IPADDR_DUPLICATE;
			_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(*si_p))

			ibd_cnt++;

			IBTF_DPRINTF_L4(cmlog, "ibcm_arp_get_ibds: [%d]"
			    ":: Partition Attributes: \n\t p-linkid %lX"
			    " d-linkid %lX, pkey 0x%lX hca_guid 0x%llX "
			    "\n\t attr-port_guid %llX", i,
			    attr[i].pa_plinkid, attr[i].pa_dlinkid,
			    attr[i].pa_pkey, attr[i].pa_hca_guid,
			    attr[i].pa_port_guid);
			IBCM_PRINT_IP("ibcm_arp_get_ibds",
			    &si_p[ibd_cnt].ip_addr);
		}
	}

	(void) ibt_free_part_attr(attr, nparts);

	IBTF_DPRINTF_L3(cmlog, "ibcm_arp_get_ibds: Found %d ibd instances",
	    ibd_cnt);

	if (ibd_cnt == 0) {
		kmem_free(si_p, alloc_cnt * sizeof (ibt_srcip_info_t));
		return (IBT_SRC_IP_NOT_FOUND);
	} else if (ibd_cnt != alloc_cnt) {
		ibt_srcip_info_t	*tmp_si_p;

		/*
		 * We allocated earlier memory based on "max_paths",
		 * but we got lesser path-records, so re-adjust that
		 * buffer so that caller can free the correct memory.
		 */
		tmp_si_p = kmem_zalloc(sizeof (ibt_srcip_info_t) * ibd_cnt,
		    KM_SLEEP);

		bcopy(si_p, tmp_si_p, ibd_cnt * sizeof (ibt_srcip_info_t));

		kmem_free(si_p, alloc_cnt * sizeof (ibt_srcip_info_t));

		si_p = tmp_si_p;
		retval = IBT_SUCCESS;
	} else
		retval = IBT_SUCCESS;

	*src_info_p = si_p;
	*entries_p = ibd_cnt;

	return (retval);
}

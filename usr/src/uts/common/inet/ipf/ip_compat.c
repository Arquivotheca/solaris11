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

#if defined(_KERNEL)
#include <sys/systm.h>
#else
#include <string.h>
#endif

#include <sys/socket.h>
#include <net/if.h>
#include <net/af.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include "netinet/ip_compat.h"
#ifdef	USE_INET6
#include <netinet/icmp6.h>
#endif
#include <netinet/tcpip.h>
#include "netinet/ip_fil.h"
#include "netinet/ip_nat.h"
#include "netinet/ip_frag.h"
#include "netinet/ip_state.h"
#include "netinet/ip_proxy.h"
#include "netinet/ip_auth.h"
#include "netinet/ipf_stack.h"
#ifdef IPFILTER_SCAN
#include "netinet/ip_scan.h"
#endif
#ifdef IPFILTER_SYNC
#include "netinet/ip_sync.h"
#endif
#include "netinet/ip_pool.h"
#include "netinet/ip_htable.h"
#ifdef IPFILTER_COMPILED
#include "netinet/ip_rules.h"
#endif
#if defined(IPFILTER_BPF) && defined(_KERNEL)
#include <net/bpf.h>
#endif
#include "netinet/ipl.h"
/* END OF INCLUDES */

#ifdef IPFILTER_COMPAT

#define	IPFILTER_VERSION_4010900	4010900
#define	IPFILTER_VERSION_4010901	4010901

struct nat_4010900 {
	ipfmutex_t	nat_lock;
	struct	nat	*nat_next;
	struct	nat	**nat_pnext;
	struct	nat	*nat_hnext[2];
	struct	nat	**nat_phnext[2];
	struct	hostmap	*nat_hm;
	void		*nat_data;
	struct	nat	**nat_me;
	struct	ipstate	*nat_state;
	struct	ap_session	*nat_aps;		/* proxy session */
	frentry_t	*nat_fr;	/* filter rule ptr if appropriate */
	struct	ipnat	*nat_ptr;	/* pointer back to the rule */
	void		*nat_ifps[2];
	void		*nat_sync;
	ipftqent_t	nat_tqe;
	u_32_t		nat_flags;
	u_32_t		nat_sumd[2];	/* ip checksum delta for data segment */
	u_32_t		nat_ipsumd;	/* ip checksum delta for ip header */
	u_32_t		nat_mssclamp;	/* if != zero clamp MSS to this */
	i6addr_t	nat_inip6;
	i6addr_t	nat_outip6;
	i6addr_t	nat_oip6;		/* other ip */
	U_QUAD_T	nat_pkts[2];
	U_QUAD_T	nat_bytes[2];
	union	{
		udpinfo_t	nat_unu;
		tcpinfo_t	nat_unt;
		icmpinfo_t	nat_uni;
		greinfo_t	nat_ugre;
	} nat_un;
	u_short		nat_oport;		/* other port */
	u_short		nat_use;
	u_char		nat_p;			/* protocol for NAT */
	int		nat_dir;
	int		nat_ref;		/* reference count */
	int		nat_hv[2];
	char		nat_ifnames[2][LIFNAMSIZ];
	int		nat_rev;		/* 0 = forward, 1 = reverse */
	int		nat_redir;
};

struct  nat_save_4010900    {
	void	*ipn_next;
	struct	nat_4010900	ipn_nat;
	struct	ipnat		ipn_ipnat;
	struct	frentry		ipn_fr;
	int			ipn_dsize;
	char			ipn_data[4];
};

struct natlookup_4010900 {
	struct	in_addr	nlc_inip;
	struct	in_addr	nlc_outip;
	struct	in_addr	nlc_realip;
	int		nlc_flags;
	u_short		nlc_inport;
	u_short		nlc_outport;
	u_short		nlc_realport;
};

/*
 * 4.1.9_02 added ns_tick (current)
 * 4.1.9_00 base
 */
struct	natstat_4010900	{
	u_long	ns_mapped[2];
	u_long	ns_rules;
	u_long	ns_added;
	u_long	ns_expire;
	u_long	ns_inuse;
	u_long	ns_logged;
	u_long	ns_logfail;
	u_long	ns_memfail;
	u_long	ns_badnat;
	u_long	ns_addtrpnt;
	nat_t	**ns_table[2];
	hostmap_t **ns_maptable;
	ipnat_t	*ns_list;
	void	*ns_apslist;
	u_int	ns_wilds;
	u_int	ns_nattab_sz;
	u_int	ns_nattab_max;
	u_int	ns_rultab_sz;
	u_int	ns_rdrtab_sz;
	u_int	ns_trpntab_sz;
	u_int	ns_hostmap_sz;
	nat_t	*ns_instances;
	nattrpnt_t *ns_trpntlist;
	hostmap_t *ns_maplist;
	u_long	*ns_bucketlen[2];
	u_int	ns_orphans;
	u_long	ns_uncreate[2][2];
};

/*
 * 4.1.9_02 added iss_tcptab (current)
 * 4.1.9_00 base
 */
struct	ips_stat_4010900 {
	u_long	iss_hits;
	u_long	iss_miss;
	u_long	iss_max;
	u_long	iss_maxref;
	u_long	iss_tcp;
	u_long	iss_udp;
	u_long	iss_icmp;
	u_long	iss_nomem;
	u_long	iss_expire;
	u_long	iss_fin;
	u_long	iss_active;
	u_long	iss_logged;
	u_long	iss_logfail;
	u_long	iss_inuse;
	u_long	iss_wild;
	u_long	iss_killed;
	u_long	iss_ticks;
	u_long	iss_bucketfull;
	int	iss_statesize;
	int	iss_statemax;
	ipstate_t **iss_table;
	ipstate_t *iss_list;
	u_long	*iss_bucketlen;
	u_int	iss_orphans;
};

/* ------------------------------------------------------------------------ */
/* Function:    fr_incomptrans                                              */
/* Returns:     int     - 0 = success, else failure                         */
/* Parameters:  obj(I) - pointer to ioctl data                              */
/*              ptr(I)  - pointer to store real data in                     */
/*                                                                          */
/* Translate the copied in ipfobj_t to new for backward compatibility at    */
/* the ABI for user land.                                                   */
/* ------------------------------------------------------------------------ */
int fr_incomptrans(obj, ptr)
ipfobj_t *obj;
void *ptr;
{
	int error;
	natlookup_t *nlp;
	nat_save_t *nsp;
	struct nat_save_4010900 nsc;
	struct natlookup_4010900 nlc;

	switch (obj->ipfo_type)
	{
	case IPFOBJ_NATLOOKUP :
                if (obj->ipfo_rev == IPFILTER_VERSION_4010901) {
			if (obj->ipfo_size != sizeof (struct natlookup))
				return EINVAL;
                        error = COPYIN((caddr_t)obj->ipfo_ptr, (caddr_t)ptr,
                                obj->ipfo_size);
			break;
		}
		if ((obj->ipfo_rev != IPFILTER_VERSION_4010900) ||
		    (obj->ipfo_size != sizeof (nlc)))
			return EINVAL;
		error = COPYIN((caddr_t)obj->ipfo_ptr, (caddr_t)&nlc,
				obj->ipfo_size);
		if (!error) {
			nlp = (natlookup_t *)ptr;
			bzero((char *)nlp, sizeof (*nlp));
			nlp->nl_inip = nlc.nlc_inip;
			nlp->nl_outip = nlc.nlc_outip;
			nlp->nl_inport = nlc.nlc_inport;
			nlp->nl_outport = nlc.nlc_outport;
			nlp->nl_flags = nlc.nlc_flags;
			nlp->nl_v = 4;
		}
		break;
	case IPFOBJ_NATSAVE :
                if (obj->ipfo_rev == IPFILTER_VERSION_4010901) {
                        if (obj->ipfo_size != sizeof (struct nat_save))
                                return EINVAL;
                        error = COPYIN((caddr_t)obj->ipfo_ptr, (caddr_t)ptr,
				obj->ipfo_size);
			break;
		}
		if ((obj->ipfo_rev != IPFILTER_VERSION_4010900) ||
		    (obj->ipfo_size != sizeof (nsc)))
			return EINVAL;
		error = COPYIN((caddr_t)obj->ipfo_ptr, (caddr_t)&nsc,
				obj->ipfo_size);
		if (!error) {
			nsp = (nat_save_t *)ptr;
			bzero((char *)nsp, sizeof (*nsp));
			nsp->ipn_next = nsc.ipn_next;
			nsp->ipn_dsize = nsc.ipn_dsize;
			nsp->ipn_nat.nat_inip = nsc.ipn_nat.nat_inip;
			nsp->ipn_nat.nat_outip = nsc.ipn_nat.nat_outip;
			nsp->ipn_nat.nat_oip = nsc.ipn_nat.nat_oip;
			nsp->ipn_nat.nat_inport = nsc.ipn_nat.nat_inport;
			nsp->ipn_nat.nat_outport = nsc.ipn_nat.nat_outport;
			nsp->ipn_nat.nat_oport = nsc.ipn_nat.nat_oport;
			nsp->ipn_nat.nat_flags = nsc.ipn_nat.nat_flags;
			nsp->ipn_nat.nat_dir = nsc.ipn_nat.nat_dir;
			nsp->ipn_nat.nat_p = nsc.ipn_nat.nat_p;
			nsp->ipn_nat.nat_v = 4;
		}
		break;
	default :
		return EINVAL;
	}
	return error;
}

/* ------------------------------------------------------------------------ */
/* Function:    fr_outcomptrans                                             */
/* Returns:     int     - 0 = success, else failure                         */
/* Parameters:  obj(I) - pointer to ioctl data                              */
/*              ptr(I)  - pointer to store real data in                     */
/*                                                                          */
/* Translate the copied out ipfobj_t to new definition for backward         */
/* compatibility at the ABI for user land.                                  */
/* ------------------------------------------------------------------------ */
int fr_outcomptrans(obj, ptr)
ipfobj_t *obj;
void *ptr;
{
	int error;
	natlookup_t *nlp;
	struct natlookup_4010900 nlc;

	switch (obj->ipfo_type)
	{
	case IPFOBJ_NATLOOKUP :
		if (obj->ipfo_rev == IPFILTER_VERSION_4010901) {
                        if (obj->ipfo_size != sizeof (struct natlookup))
                                return EINVAL;
			error = COPYOUT((caddr_t)ptr, (caddr_t)obj->ipfo_ptr,
                                obj->ipfo_size);
			break;
		}
		if ((obj->ipfo_rev != IPFILTER_VERSION_4010900) ||
		    (obj->ipfo_size != sizeof (nlc)))
			return EINVAL;
		bzero((char *)&nlc, sizeof (nlc));
		nlp = (natlookup_t *)ptr;
		nlc.nlc_inip = nlp->nl_inip;
		nlc.nlc_outip = nlp->nl_outip;
		nlc.nlc_realip = nlp->nl_realip;
		nlc.nlc_inport = nlp->nl_inport;
		nlc.nlc_outport = nlp->nl_outport;
		nlc.nlc_realport = nlp->nl_realport;
		nlc.nlc_flags = nlp->nl_flags;
		error = COPYOUT((caddr_t)&nlc, (caddr_t)obj->ipfo_ptr,
				obj->ipfo_size);
		break;
	case IPFOBJ_NATSTAT :
		if ((obj->ipfo_rev != IPFILTER_VERSION_4010900) ||
		    (obj->ipfo_rev != IPFILTER_VERSION_4010901) ||
		    (obj->ipfo_size != sizeof (struct natstat_4010900)))
			return EINVAL;
		error = COPYOUT(ptr, obj->ipfo_ptr, obj->ipfo_size);
		break;
	case IPFOBJ_STATESTAT :
		if ((obj->ipfo_rev != IPFILTER_VERSION_4010900) ||
		    (obj->ipfo_rev != IPFILTER_VERSION_4010901) ||
		    (obj->ipfo_size != sizeof (struct ips_stat_4010900)))
			return EINVAL;
		error = COPYOUT(ptr, obj->ipfo_ptr, obj->ipfo_size);
		break;
	default :
		return EINVAL;
	}
	return error;
}

#endif /* IPFILTER_COMPAT */

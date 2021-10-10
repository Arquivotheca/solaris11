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
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Transitive Probing module
 *
 * This streams module is used to send and receive transitive probes
 * between interfaces in an IPMP group.  The IPMP daemon, in.mpathd, pushes
 * the xprobe module on top of each underlying interface in the IPMP group.
 * To send a probe, in.mpathd writes out a single xprobe_data_t packet of
 * type XP_REQUEST addressed to the target interface of the IPMP group.
 * The request packet is intercepted by the xprobe module on the target
 * interface which then changes the xp_type from XP_REQUEST to XP_RESPONSE,
 * returns the response back to the sending interface. The returned response
 * is passed up to in.mpathd where the information in the xprobe_data_t
 * is used to confirm bidirectional reachability from the sender to the target
 * interface, compute round-trip time, and collect other statistics about the
 * path.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/stropts.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/cred.h>
#include <sys/dlpi.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/modctl.h>
#include <inet/common.h>
#include <sys/ethernet.h>
#include <sys/ksynch.h>
#include <sys/sunddi.h>
#include <sys/ddi.h>
#include <inet/ip.h>
#include <inet/xprobe.h>
#include <sys/strsun.h>


#define	DL_PRIM(mp)	(((union DL_primitives *)(mp)->b_rptr)->dl_primitive)

#define	D_SM_NAME	"xprobe"
#define	D_SM_COMMENT	"Transitive probing module"
#define	D_SM_FLAGS	D_MP

/*
 * xprobe_t - 1 per queue
 */
struct xprobe_s {
	struct xprobe_s	*xp_next;	/* Next instance */
	queue_t		*xp_queue;	/* Associated read queue */
	uint32_t	xp_flags;
#define	XP_INFO_ACK_DONE	1
	size_t		xp_phys_addr_len;
	t_scalar_t	xp_sap_len;
};

/*
 * The functions provided by the xprobe module
 */
static int xpopen(queue_t *, dev_t *, int, int, cred_t *);
static int xpclose(queue_t *, int, cred_t *);
static int xprput(queue_t *, mblk_t *);
static int xpwput(queue_t *, mblk_t *);
static int xpwputmod(queue_t *, mblk_t *);
static mblk_t *xp_dlur_gen(uchar_t *, uint_t, t_scalar_t);

/*
 * STREAMS linkage information.
 */
static struct module_info xp_minfo = {
	0xaaaa,		/* mi_idnum */
	D_SM_NAME,	/* mi_idname */
	0,		/* mi_minpsz */
	16384,		/* mi_maxpsz */
	8*16384,	/* mi_hiwat */
	8192		/* mi_lowat */
};

static struct qinit xp_rinit = {
	xprput, NULL, xpopen, xpclose, NULL, &xp_minfo, NULL
};

static struct qinit xp_winit = {
	xpwput, NULL, NULL, NULL, NULL, &xp_minfo, NULL
};

static struct streamtab xp_info = {
	&xp_rinit, &xp_winit, NULL, NULL
};

static struct fmodsw fsw = {
	D_SM_NAME,
	&xp_info,
	D_SM_FLAGS
};

/*
 * Module linkage information for the kernel.
 */
static struct modlstrmod modlstrmod = {
	&mod_strmodops, D_SM_COMMENT, &fsw
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modlstrmod,
	NULL
};

int
_init()
{
	return (mod_install(&modlinkage));
}

int
_fini()
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * xpopen is called when the module is pushed on the stream. It is
 * also called on opens of the device to which the stream is
 * attached. Per queue instance, read and write side have distinct
 * xprobe instances.
 */
/* ARGSUSED */
static int
xpopen(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *crp)
{
	struct	xprobe_s *xp;

	if (q->q_ptr != NULL)		/* not first open */
		return (0);

	if (sflag != MODOPEN)
		return (EINVAL);

	xp = kmem_zalloc(sizeof (struct xprobe_s), KM_SLEEP);
	xp->xp_queue = q;
	q->q_ptr = (caddr_t)xp;
	OTHERQ(q)->q_ptr = q->q_ptr;
	qprocson(q);
	return (0);
}

/* ARGSUSED */
static int
xpclose(queue_t *q, int flag, cred_t *crp)
{
	struct	xprobe_s *xp;

	if (WR(q)->q_next != NULL) {
		/* Only clean up if we are a module instance */
		xp = (struct xprobe_s *)q->q_ptr;
		kmem_free(xp, sizeof (struct xprobe_s));
	}
	qprocsoff(q);
	return (0);
}

/*
 * given an mblk chain with first mblk == DL_UNITDATA_IND, rest of the mblks
 * containing data, if the data contains the XP_REQUEST type, we send a
 * response back to the the sender's link layer address. Unrecognized types
 * are returned back to send up the queue.
 */
static mblk_t *
xp_send_response(queue_t *q, mblk_t *omp)
{
	struct xprobe_s *xp = (struct xprobe_s *)q->q_ptr;
	xprobe_data_t *xp_data;
	dl_unitdata_ind_t *ind;
	mblk_t *mp, *dlur_mp;
	uint8_t *saddr = NULL, *cp;
	uint_t extra_offset;

	/*
	 * Just pass up all messages until fully initialized.
	 */
	if ((xp->xp_flags & XP_INFO_ACK_DONE) == 0)
		return (omp);

	mp = omp;
	ind = (dl_unitdata_ind_t *)mp->b_rptr;
	if (xp->xp_sap_len < 0)
		extra_offset = 0;
	else
		extra_offset = xp->xp_sap_len;
	if (ind->dl_src_addr_length > 0)
		saddr = (uint8_t *)ind + ind->dl_src_addr_offset + extra_offset;

	mp = msgpullup(omp->b_cont, -1);
	if (mp == NULL) {
		freemsg(omp);
		return (NULL);
	}

	xp_data = (xprobe_data_t *)mp->b_rptr;
	if (xp_data->xp_proto != ORCL_XPROBE_PROTO ||
	    xp_data->xp_type != XP_REQUEST) {
		freemsg(mp);
		return (omp);
	}
	xp_data->xp_type = XP_RESPONSE;
	if (saddr == NULL) {
		/*
		 * some links, e.g., DL_IB, don't have access to source
		 * information, so they don't include the source address in
		 * the dl_unitdata_ind_t. For these, we have to resort to the
		 * information available in the packet.
		 */
		cp = (uint8_t *)&xp_data[1];
		saddr = cp + *cp + 2;
	}
	dlur_mp = xp_dlur_gen(saddr, xp->xp_phys_addr_len, xp->xp_sap_len);
	freemsg(omp);
	if (dlur_mp == NULL) {
		freemsg(mp);
		return (NULL);
	} else {
		dlur_mp->b_cont = mp;
		putnext(WR(q), dlur_mp);
	}
	return (NULL);
}

static int
xpwput(queue_t *q, mblk_t *mp)
{
	struct	iocblk	*iocp;

	if (q->q_next != NULL)
		return (xpwputmod(q, mp));

	/* Now we know we are a driver instance */
	switch (mp->b_datap->db_type) {
	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW)
			ASSERT(q->q_first == NULL);
		if (*mp->b_rptr & FLUSHR) {
			ASSERT(RD(q)->q_first == NULL);
			*mp->b_rptr &= ~FLUSHW;
			qreply(q, mp);
		}
		freemsg(mp);
		break;

	case M_IOCTL:
		iocp = (struct iocblk *)mp->b_rptr;
		mp->b_datap->db_type = M_IOCNAK;
		if (mp->b_cont != NULL) {
			freemsg(mp->b_cont);
			mp->b_cont = NULL;
		}
		iocp->ioc_count = 0;
		iocp->ioc_error = EINVAL;
		qreply(q, mp);
		break;

	default:
		/*
		 * Free anything else
		 */
		freemsg(mp);
		break;
	}
	return (0);
}

static void
xp_process_info_ack(struct xprobe_s *xp, dl_info_ack_t *dlia)
{
	xp->xp_phys_addr_len = dlia->dl_brdcst_addr_length;
	xp->xp_sap_len = dlia->dl_sap_length;
	xp->xp_flags |= XP_INFO_ACK_DONE;
}

/*
 * xprput is called when messages are passed to the module for transmission
 * upstream or downstream
 */
static int
xprput(queue_t *q, mblk_t *mp)
{
	struct	xprobe_s *xp = (struct xprobe_s *)q->q_ptr;
	t_uscalar_t prim;

	switch (mp->b_datap->db_type) {
	case M_PCPROTO:
	case M_PROTO: {
		union DL_primitives	*dlp =
		    (union DL_primitives *)mp->b_rptr;

		if ((mp->b_wptr - mp->b_rptr) < sizeof (dlp->dl_primitive)) {
			putnext(q, mp);
			return (0);
		}
		prim = dlp->dl_primitive;

		switch (prim) {
		case DL_INFO_ACK:
			xp_process_info_ack(xp, (dl_info_ack_t *)mp->b_rptr);
			putnext(q, mp);
			return (0);
		case DL_UNITDATA_IND:
			/*
			 * Requests are handled within xp_send_response itself.
			 * All the rest (including responses) are pased up the
			 * queue.
			 */
			mp = xp_send_response(q, mp);
			if (mp != NULL)
				putnext(q, mp);
			return (0);
		default:
			putnext(q, mp);
			return (0);
		}
	}
	/* FALLTHRU */
	case M_DATA:
		putnext(q, mp);
		break;

	case M_FLUSH:
		if (*mp->b_rptr & FLUSHR)
			ASSERT(q->q_first == NULL);
		/* FALLTHRU */
	default:
		putnext(q, mp);
		break;
	}
	return (0);
}

static int
xpwputmod(queue_t *q, mblk_t *mp)
{
	struct	xprobe_s *xp = (struct xprobe_s *)q->q_ptr;
	mblk_t *dlur_mp;
	uint8_t *cp, *daddr;
	uint8_t hlen;

	switch (mp->b_datap->db_type) {
	case M_PROTO:
		putnext(q, mp);
		break;
	case M_DATA:
		if ((xp->xp_flags & XP_INFO_ACK_DONE) == 0) {
			freemsg(mp);
			return (ENXIO);
		}
		cp = (uint8_t *)mp->b_rptr;
		cp += sizeof (xprobe_data_t);
		hlen = *(uint8_t *)cp;
		if (hlen != xp->xp_phys_addr_len) {
			printf("hlen %x != phys_addr_len %x\n",
			    hlen, (uint_t)xp->xp_phys_addr_len);
		}
		daddr = ++cp;
		dlur_mp = xp_dlur_gen(daddr, xp->xp_phys_addr_len,
		    xp->xp_sap_len);
		if (dlur_mp == NULL) {
			freemsg(mp);
			return (ENOMEM);
		} else {
			dlur_mp->b_cont = mp;
			putnext(q, dlur_mp);
		}
		break;

	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW)
			ASSERT(q->q_first == NULL);
		/* FALLTHRU */
	default:
		putnext(q, mp);
		break;
	}
	return (0);
}

static mblk_t *
xp_dlur_gen(uchar_t *addr, uint_t addr_len, t_scalar_t sap_len)
{
	dl_unitdata_req_t *dlur;
	mblk_t  *mp;
	t_scalar_t abs_sap_len;	/* absolute value */
	uint16_t sap_addr = ETHERTYPE_ORCL_1;
	size_t len;

	abs_sap_len = ABS(sap_len);
	len = sizeof (*dlur) + addr_len + abs_sap_len;
	mp = allocb(len, BPRI_MED);
	if (mp == NULL)
		return (NULL);
	mp->b_datap->db_type = M_PROTO;
	mp->b_wptr = mp->b_rptr + len;
	bzero(mp->b_rptr, len);
	dlur = (dl_unitdata_req_t *)mp->b_rptr;
	dlur->dl_primitive = DL_UNITDATA_REQ;
	/* HACK: accomodate incompatible DLPI drivers */
	if (addr_len == 8)
		addr_len = 6;
	dlur->dl_dest_addr_length = addr_len + abs_sap_len;
	dlur->dl_dest_addr_offset = sizeof (*dlur);
	dlur->dl_priority.dl_min = 0;
	dlur->dl_priority.dl_max = 0;
	if (sap_len <= 0) {
		uchar_t *dst = (uchar_t *)&dlur[1];

		if (addr == NULL)
			bzero(dst, addr_len);
		else
			bcopy(addr, dst, addr_len);
		if (sap_len < 0) {
			bcopy(&sap_addr, (char *)dst + addr_len,
			    sizeof (sap_addr));
		}
	} else {
		uchar_t *dst = (uchar_t *)&dlur[1];

		bcopy(&sap_addr, dst, sizeof (sap_addr));
		if (addr == NULL)
			bzero(dst + sap_len, addr_len);
		else
			bcopy(addr, dst + sap_len, addr_len);
	}
	return (mp);
}

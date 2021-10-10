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
 * The IEEE 802.1 bridging specification defines the GARP VLAN Registration
 * Protocol (GVRP) to allow the automatic configuration and discovery of VLANs
 * across ethernet switches. The protocol uses GARP messages to propagate
 * VLAN participation information.
 *
 * The Solaris GVRP client project propagates information about the hosts's
 * VLANs to the network. It does not accept external join or leave requests.
 * The goal is to allow the network to discover the VLANs of all local
 * VNICs/mac clients, by registering all local VIDs with the network.
 *
 * Each mac instance (mac_impl) has a list of VIDs associated with it. A join
 * needs to be sent, across the associated physical interface, for each of
 * these VIDs. The GVRP client will keep a list of VIDs for each mac instance.
 * When mac clients attach/detach from a mac instance, they will update a
 * reference count in the VID list for the VID in which they participate. A
 * new entry will be created for a VID entry that doesn't exit. When the
 * reference count on a VID entry goes to zero, the entry is removed from the
 * list.
 */

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/mac_impl.h>
#include <sys/mac_client_impl.h>
#include <sys/mac_client_priv.h>
#include <sys/ethernet.h>
#include <sys/vlan.h>
#include <sys/llc1.h>
#include <sys/net802dot1.h>

#include <sys/thread.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/sdt.h>

#include <sys/mac_gvrp_impl.h>

extern pri_t minclsyspri;

#define	GVRP_MIN_FRAME_SIZE	60
#define	GVRP_TIMEOUT_MIN	100 /* (ms) */
#define	GVRP_TIMEOUT_MAX	100000 /* (ms) */

#define	GARP_GROUP_ATTR    1
#define	GARP_SERVICE_REQ   2

#define	GARP_GVRP_ID_BYTE0 0x0
#define	GARP_GVRP_ID_BYTE1 0x1

typedef uint8_t garp_endmark;

struct garp_msg_header
{
	uint8_t attr_type;
};

struct garp_header
{
	uint8_t  id[2];
};

#define	GVRP_FRAME_HEADER_SIZE  (sizeof (struct ether_header)	+\
				sizeof (struct llchdr)	+\
				sizeof (struct garp_header)	+\
				sizeof (struct garp_msg_header)	+\
				sizeof (garp_endmark))

struct mac_gvrp_vid_list {
	struct mac_gvrp_vid_list *gvl_next;
	uint16_t vid;
	uint_t ref_count;
};

struct mac_gvrp_active_link {
	mac_handle_t mh;
	kthread_t *thread;
	boolean_t thread_shutdown;
	kmutex_t register_lock;
	kcondvar_t register_cv;
};

static krwlock_t mac_gvrp_rw_lock;
static int mac_gvrp_nlinks;

static uint_t
mac_gvrp_list_len(mac_gvrp_vid_list_t *list)
{
	uint_t len = 0;
	for (len = 0; list != NULL; len++) {
		list = list->gvl_next;
	}
	return (len);
}

/*
 * Generate header data, common to all GVRP frames
 */
static mblk_t *
mac_gvrp_setup_frame(uint_t size, struct ether_addr *saddr)
{
	struct ether_header *eh;
	struct llchdr *llch;
	struct garp_header *garph;
	struct garp_msg_header *garpmh;
	struct ether_addr daddr = GVRP_DEST_ADDR;

	ASSERT(saddr != NULL);
	mblk_t *frame = NULL;

	frame = allocb(size, BPRI_HI);
	if (frame == NULL)
		return (NULL);

	bzero(frame->b_wptr, size);
	frame->b_wptr += size;

	/* set headers */
	eh = (struct ether_header *)frame->b_rptr;
	bcopy(&daddr, &(eh->ether_dhost), ETHERADDRL);
	bcopy(saddr, &(eh->ether_shost), ETHERADDRL);
	eh->ether_type = htons((ushort_t)(size - GVRP_FRAME_HEADER_SIZE));

	llch = (struct llchdr *)((char *)eh+sizeof (struct ether_header));
	llch->llc_dsap = LLC_BPDU_SAP;
	llch->llc_ssap = LLC_BPDU_SAP;
	llch->llc_ctl = LLCS_BRDCSTXMT;

	garph = (struct garp_header *)((char *)llch +
	    sizeof (struct llchdr));
	garph->id[0] = GARP_GVRP_ID_BYTE0;
	garph->id[1] = GARP_GVRP_ID_BYTE1;

	garpmh = (struct garp_msg_header *)((char *)garph +
	    sizeof (struct garp_header));
	garpmh->attr_type = GARP_GROUP_ATTR;

	return (frame);
}

/*
 * Generate the join/leave request frames for a given list of VLAN IDs.
 *
 * All GVRP packets are sent to address 01:80:C2:00:00:21 per
 * 802.1D section 12.4
 */
static mblk_t *
mac_gvrp_generate_pdu_vidlist(mac_gvrp_vid_list_t *vid_list,
    struct ether_addr *saddr, uint_t linkmtu, uint8_t req_type)
{
	ASSERT(vid_list != NULL);
	int i;
	mblk_t *cur_mp = NULL;
	mblk_t *mp = NULL;
	mac_gvrp_vid_list_t *cur_list;
	uint_t rem_list_size, payload_space;
	uint_t vidlist_len = mac_gvrp_list_len(vid_list);
	uint_t n_reqd_frame = 0;

	if (linkmtu <= GVRP_MIN_FRAME_SIZE)
		return (NULL);

	n_reqd_frame = 1 + ((vidlist_len) * sizeof (gvrp_attr_t))
	    / (linkmtu - GVRP_FRAME_HEADER_SIZE);

	/* Allocate mblks and populate */
	cur_list = vid_list;
	rem_list_size = vidlist_len;

	for (i = 1; i <= n_reqd_frame; i++) {
		mblk_t *new_mp;
		uint_t blk_size;
		char *payload;

		if (i == n_reqd_frame) { /* last frame */
			blk_size = GVRP_FRAME_HEADER_SIZE +
			    (rem_list_size * sizeof (gvrp_attr_t));
		} else {
			blk_size = linkmtu;
		}

		blk_size = max(blk_size, GVRP_MIN_FRAME_SIZE);

		new_mp = mac_gvrp_setup_frame(blk_size, saddr);
		if (new_mp == NULL) {
			if (mp != NULL)
				freemsgchain(mp);
			return (NULL);
		}

		/* populate GVRP requests */
		payload_space = blk_size - GVRP_FRAME_HEADER_SIZE;
		/* Start after the header, but not after the endmark */
		payload = (char *)new_mp->b_rptr + GVRP_FRAME_HEADER_SIZE
		    - sizeof (uint8_t);
		while ((payload_space >= sizeof (gvrp_attr_t)) &&
		    (cur_list != NULL)) {
			gvrp_attr_t attr;

			attr.len   = sizeof (gvrp_attr_t);
			attr.event = req_type;
			attr.value = ntohs(cur_list->vid);
			bcopy(&attr, payload, sizeof (gvrp_attr_t));
			cur_list = cur_list->gvl_next;
			rem_list_size--;
			payload_space -= sizeof (gvrp_attr_t);
			payload += sizeof (gvrp_attr_t);
		}

		*payload = 0x0; /* end marker */
		if (i == 1) { /* first frame */
			mp = new_mp;
		} else {
			cur_mp->b_next = new_mp;
		}
		cur_mp = new_mp;
	}
	return (mp);
}

/*
 * Generate PDU(s). If a single VID is given, a single request is
 * generated. Otherwise requests will be created for mip->vidlist.
 */
static mblk_t *
mac_gvrp_generate_req(mac_impl_t *mip, uint8_t req_type, uint16_t vid)
{
	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	struct ether_addr saddr;
	mblk_t *mp;
	uint_t linkmtu;
	mac_gvrp_vid_list_t tmp_vid_list, *vid_list;

	if (vid == VLAN_ID_NONE) {
		vid_list = mip->mi_vid_list;
	} else {
		tmp_vid_list.vid = vid;
		tmp_vid_list.gvl_next = NULL;
		vid_list = &tmp_vid_list;
	}

	mac_unicast_primary_get((mac_handle_t)mip, (uint8_t *)&saddr);
	linkmtu = mip->mi_sdu_max;
	mp = mac_gvrp_generate_pdu_vidlist(vid_list, &saddr,
	    linkmtu, req_type);

	return (mp);
}

static void
mac_gvrp_mh_process(mac_handle_t mh)
{
	mac_impl_t		*mip  = (mac_impl_t *)mh;
	mblk_t			*mp   = NULL;

	i_mac_perim_enter(mip);
	ASSERT((mip->mi_state_flags & MIS_IS_VNIC) == 0);

	/*
	 * In the event that the vlan-announce type has
	 * changed while processing this mip, we bail
	 * out.
	 */
	if (mip->mi_vlan_announce != MAC_VLAN_ANNOUNCE_GVRP) {
		i_mac_perim_exit(mip);
		return;
	}

	DTRACE_PROBE2(interface_process,  (char *),
	    ddi_driver_name(mip->mi_dip), (uint_t),
	    mac_gvrp_list_len(mip->mi_vid_list));

	if (mip->mi_vid_list != NULL)
		mp = mac_gvrp_generate_req(mip, GARP_JOIN_IN, 0);

	i_mac_perim_exit(mip);

	if (mp != NULL) {
		ASSERT(mip->mi_gvrp_mch != NULL);
		(void) mac_tx(mip->mi_gvrp_mch, mp, 0, MAC_DROP_ON_NO_DESC,
		    NULL);
	}
}


/*
 * This is the main loop for the GVRP registration worker thread.
 */
static void
mac_gvrp_client_handler(void *arg) {
	mac_gvrp_active_link_t *link = arg;

	ASSERT(link != NULL);
	ASSERT(link->mh != NULL);

	link->thread_shutdown = B_FALSE;
	do {
		clock_t tm, wt;
		mac_impl_t *mip = (mac_impl_t *)link->mh;

		mac_gvrp_mh_process(link->mh);

		wt = drv_usectohz((mip->mi_gvrp_timeout) * 1000);
		tm = ddi_get_lbolt() + wt;

		mutex_enter(&link->register_lock);
		(void) cv_timedwait(&link->register_cv, &link->register_lock,
		    tm);
		mutex_exit(&link->register_lock);

	} while (!link->thread_shutdown);
}

static void
mac_gvrp_signal_worker(mac_gvrp_active_link_t *link)
{
	ASSERT(link != NULL);

	/* Wakeup the registration thread */
	mutex_enter(&link->register_lock);
	cv_signal(&link->register_cv);
	mutex_exit(&link->register_lock);
}

/*
 * Create the global GVRP client structure and registration thread.
 * Called only once, during mac startup
 */
void
mac_vlan_announce_init()
{
	mac_gvrp_nlinks = 0;
	rw_init(&mac_gvrp_rw_lock, NULL, RW_DRIVER, NULL);
}

void
mac_vlan_announce_fini()
{
	rw_enter(&mac_gvrp_rw_lock, RW_WRITER);
	ASSERT(mac_gvrp_nlinks == 0);
	rw_exit(&mac_gvrp_rw_lock);
	rw_destroy(&mac_gvrp_rw_lock);
}

/*
 * Register a VLAN ID use for calling mac client.
 * Must be called from inside mip's perim.
 */
int
mac_vlan_announce_register(mac_handle_t *mh, uint16_t vid)
{
	mac_impl_t *mip = (mac_impl_t *)mh;
	mac_gvrp_vid_list_t *list;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	/*
	 * For the first registration, we need to create a client to
	 * send requests for us.
	 */
	if (mip->mi_vid_list == NULL) {
		char cli_name[MAXNAMELEN];
		struct ether_addr dummy_addr;
		int rc;

		bzero(&dummy_addr, sizeof (struct ether_addr));
		(void) snprintf(cli_name, MAXNAMELEN, "%sgvrp", mip->mi_name);
		rc = mac_client_open((mac_handle_t)mip, &mip->mi_gvrp_mch,
		    cli_name, MAC_OPEN_FLAGS_NO_UNICAST_ADDR);

		if (rc != 0) {
			return (rc);
		}

		DTRACE_PROBE1(gvrp_client_started, (mac_impl_t *), mip);
	}

	/* search for VID entry */
	list = mip->mi_vid_list;
	while (list != NULL) {
		if (list->vid == vid)
			break; /* found VID entry */
		else
			list = list->gvl_next;
	}
	if (list == NULL) { /* no entry was found in list */
		/* create entry */
		list = kmem_alloc(sizeof (mac_gvrp_vid_list_t), KM_SLEEP);
		list->vid = vid;
		list->ref_count = 1;
		list->gvl_next = mip->mi_vid_list;
		mip->mi_vid_list = list;

		/* send first registration request */
		if (mip->mi_vlan_announce == MAC_VLAN_ANNOUNCE_GVRP)
			mac_gvrp_signal_worker(mip->gvrp_link);

	} else {
		list->ref_count++;
	}
	return (0);
}

/*
 * Unregister a VLAN ID use for calling mac client.
 * Must be called from inside mip's perim.
 */
int
mac_vlan_announce_deregister(mac_handle_t *mh, uint16_t vid)
{
	mac_impl_t *mip = (mac_impl_t *)mh;
	mac_gvrp_vid_list_t	*cur, *last;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));

	/* search for VID entry */
	cur = last = mip->mi_vid_list;
	while (cur != NULL) {
		if (cur->vid == vid) {
			break; /* found VID entry */
		} else {
			last = cur;
			cur = cur->gvl_next;
		}
	}

	/* we should not be able to deregister unknown VIDs */
	ASSERT(cur != NULL);

	cur->ref_count--;

	if (cur->ref_count == 0) {
		/*
		 * We have removed the last reference to the VID. Now we should
		 * remove the entry from the vid_list and send a leave request
		 * to the network. We want to do something like:
		 *
		 * gvrp_mac_tx_req(mip, GARP_LEAVE_IN, vid)
		 *
		 * However, we can't send because we have the perimeter held
		 * the worker doesn't provide a mechanism for sending LEAVE.
		 * For now we rely on the timers on the switch to deregister
		 * the VID.
		 */

		if (mip->mi_vid_list == cur)
			mip->mi_vid_list = cur->gvl_next;
		else
			last->gvl_next = cur->gvl_next;

		kmem_free(cur, sizeof (mac_gvrp_vid_list_t));
	}

	if (mip->mi_vid_list == NULL && mip->mi_gvrp_mch != NULL) {
		DTRACE_PROBE1(gvrp_client_shutdown, (mac_impl_t *), mip);
		mac_client_close(mip->mi_gvrp_mch, 0);
		mip->mi_gvrp_mch = NULL;
	}

	return (0);
}


void
mac_gvrp_enable(mac_handle_t mh)
{
	mac_gvrp_active_link_t *link;
	mac_impl_t *mip = (mac_impl_t *)mh;

	link = kmem_alloc(sizeof (mac_gvrp_active_link_t), KM_SLEEP);
	link->mh = mh;
	mip->gvrp_link = link;

	mutex_init(&link->register_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&link->register_cv, NULL, CV_DRIVER, NULL);

	link->thread = thread_create(NULL, 0, mac_gvrp_client_handler, link, 0,
	    &p0, TS_RUN, minclsyspri);

	rw_enter(&mac_gvrp_rw_lock, RW_WRITER);
	mac_gvrp_nlinks++;
	rw_exit(&mac_gvrp_rw_lock);
}

void
mac_gvrp_disable(mac_handle_t mh)
{
	mac_gvrp_active_link_t *link;
	mac_impl_t *mip = (mac_impl_t *)mh;

	if (mip->mi_vlan_announce != MAC_VLAN_ANNOUNCE_GVRP)
		return;

	link = mip->gvrp_link;
	link->thread_shutdown = B_TRUE;
	mac_gvrp_signal_worker(mip->gvrp_link);
	thread_join(link->thread->t_did);
	kmem_free(link, sizeof (mac_gvrp_active_link_t));
	mip->gvrp_link = NULL;

	rw_enter(&mac_gvrp_rw_lock, RW_WRITER);
	mac_gvrp_nlinks--;
	rw_exit(&mac_gvrp_rw_lock);
}

int
mac_gvrp_set_timeout(mac_handle_t mh, uint32_t to)
{
	mac_impl_t *mip = (mac_impl_t *)mh;

	mip->mi_gvrp_timeout = to;
	if (mip->mi_vlan_announce == MAC_VLAN_ANNOUNCE_GVRP)
		mac_gvrp_signal_worker(mip->gvrp_link);

	return (0);
}

uint32_t
mac_gvrp_get_timeout_min()
{
	static uint32_t min = GVRP_TIMEOUT_MIN;
	return (min);
}

uint32_t
mac_gvrp_get_timeout_max()
{
	static uint32_t max = GVRP_TIMEOUT_MAX;
	return (max);
}

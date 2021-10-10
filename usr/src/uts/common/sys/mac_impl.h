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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */
#ifndef	_SYS_MAC_IMPL_H
#define	_SYS_MAC_IMPL_H

#include <sys/cpupart.h>
#include <sys/modhash.h>
#include <sys/mac_client.h>
#include <sys/mac_provider.h>
#include <sys/mac_stat.h>
#include <sys/mac_gvrp_impl.h>
#include <sys/note.h>
#include <sys/avl.h>
#include <net/if.h>
#include <sys/mac_flow_impl.h>
#include <sys/numaio.h>
#include <netinet/ip6.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * These externs are here because of the difficulty in including
 * platform dependent machsystm.h files in platform independent code.
 */
#if defined(__sparc)
extern void *contig_mem_alloc(size_t);
extern void *contig_mem_alloc_align(size_t, size_t);
extern void contig_mem_free(void *, size_t);
#endif

/*
 * This is the first minor number available for MAC provider private
 * use.  This makes it possible to deliver a driver that is both a MAC
 * provider and a regular character/block device.  See PSARC 2009/380
 * for more detail about the construction of such devices.  The value
 * chosen leaves half of the 32-bit minor numbers (which are really
 * only 18 bits wide) available for driver private use.  Drivers can
 * easily identify their private number by the presence of this value
 * in the bits that make up the minor number, since its just the
 * highest bit available for such minor numbers.
 */
#define	MAC_PRIVATE_MINOR		((MAXMIN32 + 1) / 2)

/*
 * The maximum minor number that corresponds to a real instance.  This
 * limits the number of physical ports that a mac provider can offer.
 * Note that this macro must be synchronized with DLS_MAX_MINOR in
 * <sys/dls.h>
 */
#define	MAC_MAX_MINOR			1000

typedef struct mac_margin_req_s	mac_margin_req_t;

struct mac_margin_req_s {
	mac_margin_req_t	*mmr_nextp;
	uint_t			mmr_ref;
	uint32_t		mmr_margin;
};

/*
 * Generic mac callback list manipulation structures and macros. The mac_cb_t
 * represents a general callback list element embedded in a particular
 * data structure such as a mac_notify_cb_t or a mac_promisc_impl_t.
 * The mac_cb_info_t represents general information about list walkers.
 * Please see the comments above mac_callback_add for more information.
 */
/* mcb_flags */
#define	MCB_CONDEMNED		0x1		/* Logically deleted */
#define	MCB_NOTIFY_CB_T		0x2
#define	MCB_TX_NOTIFY_CB_T	0x4

typedef struct mac_cb_s {
	struct mac_cb_s		*mcb_nextp;	/* Linked list of callbacks */
	void			*mcb_objp;	/* Ptr to enclosing object  */
	size_t			mcb_objsize;	/* Sizeof the enclosing obj */
	uint_t			mcb_flags;
} mac_cb_t;

typedef struct mac_cb_info_s {
	kmutex_t	*mcbi_lockp;
	kcondvar_t	mcbi_cv;
	uint_t		mcbi_del_cnt;		/* Deleted callback cnt */
	uint_t		mcbi_walker_cnt;	/* List walker count */
} mac_cb_info_t;

typedef struct mac_notify_cb_s {
	mac_cb_t	mncb_link;		/* Linked list of callbacks */
	mac_notify_t	mncb_fn;		/* callback function */
	void		*mncb_arg;		/* callback argument */
	struct mac_impl_s *mncb_mip;
} mac_notify_cb_t;

/* Tx notify callback */
typedef struct mac_tx_notify_cb_s {
	mac_cb_t		mtnf_link;	/* Linked list of callbacks */
	mac_tx_notify_t		mtnf_fn;	/* The callback function */
	void			*mtnf_arg;	/* Callback function argument */
} mac_tx_notify_cb_t;

/*
 * mac_callback_add(listinfo, listhead, listelement)
 * mac_callback_remove(listinfo, listhead, listelement)
 */
typedef boolean_t (*mcb_func_t)(mac_cb_info_t *, mac_cb_t **, mac_cb_t *);

#define	MAC_CALLBACK_WALKER_INC(mcbi) {				\
	mutex_enter((mcbi)->mcbi_lockp);			\
	(mcbi)->mcbi_walker_cnt++;				\
	mutex_exit((mcbi)->mcbi_lockp);				\
}

#define	MAC_CALLBACK_WALKER_INC_HELD(mcbi)	(mcbi)->mcbi_walker_cnt++;

#define	MAC_CALLBACK_WALKER_DCR(mcbi, headp) {			\
	mac_cb_t	*rmlist;				\
								\
	mutex_enter((mcbi)->mcbi_lockp);			\
	if (--(mcbi)->mcbi_walker_cnt == 0 && (mcbi)->mcbi_del_cnt != 0) { \
		rmlist = mac_callback_walker_cleanup((mcbi), headp);	\
		mac_callback_free(rmlist);			\
		cv_broadcast(&(mcbi)->mcbi_cv);			\
	}							\
	mutex_exit((mcbi)->mcbi_lockp);				\
}

#define	MAC_PROMISC_WALKER_INC(mip)				\
	MAC_CALLBACK_WALKER_INC(&(mip)->mi_promisc_cb_info)

#define	MAC_PROMISC_WALKER_DCR(mip) {				\
	mac_cb_info_t	*mcbi;					\
								\
	mcbi = &(mip)->mi_promisc_cb_info;			\
	mutex_enter(mcbi->mcbi_lockp);				\
	if (--mcbi->mcbi_walker_cnt == 0 && mcbi->mcbi_del_cnt != 0) { \
		i_mac_promisc_walker_cleanup(mip);		\
		cv_broadcast(&mcbi->mcbi_cv);			\
	}							\
	mutex_exit(mcbi->mcbi_lockp);				\
}

typedef struct mactype_s {
	const char	*mt_ident;
	uint32_t	mt_ref;
	uint_t		mt_type;
	uint_t		mt_nativetype;
	size_t		mt_addr_length;
	uint8_t		*mt_brdcst_addr;
	mactype_ops_t	mt_ops;
	mac_stat_info_t	*mt_stats;	/* array of mac_stat_info_t elements */
	size_t		mt_statcount;	/* number of elements in mt_stats */
	mac_ndd_mapping_t *mt_mapping;
	size_t		mt_mappingcount;
} mactype_t;

/*
 * Multiple rings implementation.
 */
typedef	enum {
	MAC_GROUP_STATE_UNINIT	= 0,	/* initial state of data structure */
	MAC_GROUP_STATE_REGISTERED,	/* hooked with h/w group */
	MAC_GROUP_STATE_RESERVED,	/* group is reserved and opened */
	MAC_GROUP_STATE_SHARED		/* default group shared among */
					/* multiple mac clients */
} mac_group_state_t;

#define	MAC_GROUP_TX	1
#define	MAC_GROUP_RX	2

typedef	struct mac_ring_s mac_ring_t;
typedef	struct mac_group_s mac_group_t;
typedef struct mac_packet_pool_s mac_packet_pool_t;
typedef struct mac_block_pool_s mac_block_pool_t;

/*
 * Ring data structure for ring control and management.
 */
typedef enum {
	MR_NONE = 0,		/* No state for the ring. */
	MR_FREE,		/* Available for assignment to flows */
	MR_NEWLY_ADDED,		/* Just assigned to another group */
	MR_INUSE		/* Assigned */
} mac_ring_state_t;

/* mr_flag values */
#define	MR_BUSY		0x01
#define	MR_QUIESCE	0x02

/* mr_worker_state values for tx rings */
#define	MR_TX_READY	0x01	/* Ring init done and ready for Tx */
#define	MR_TX_BUSY	0x02	/* Provides mutual exclusion on Tx */
#define	MR_TX_AWAKEN	0x04	/* Driver reclaimed Tx desc, wakeup worker */
#define	MR_TX_NODESC	0x08	/* Driver out of Tx descriptors */
				/* MR_TX_NODESC flag owned by Tx worker */
#define	MR_TX_WAITING	0x10	/* Mac client is blocked on transmit */

/* mr_worker_state values for rx rings */
#define	MR_RX_READY	0x01
#define	MR_RX_POLLING	0x02

/* mbm_mem_type in mac_block_t is one of the following */
typedef	enum {
	MAC_DDI_MEM = 0,	/* Memory allocated using ddi_dma_mem_alloc */
	MAC_CONTIG_MEM,		/* Memory allocated using contig_mem_alloc */
	MAC_PREMAP_MEM,		/* Memory allocated using allocb_premapped */
	MAC_CONTIG_PREMAP_MEM	/* Memory allocated using premap_mem_alloc */
} dma_mem_type_t;

typedef struct mac_impl_s mac_impl_t;
typedef struct mac_descriptor_s mac_descriptor_t;

struct mac_ring_s {
	mac_ring_type_t		mr_type;	/* ring type, must be first */
	int32_t			mr_index;	/* index in the original list */
	int			mr_gindex;	/* index within a group */
	mac_ring_t		*mr_next;	/* next ring in a group */
	mac_ring_t		*mr_list_next;	/* next ring in global chain */
	mac_group_handle_t	mr_gh;		/* reference to group */

	mac_ring_handle_t	mr_prh;		/* associated pseudo ring hdl */
	mac_resource_handle_t	mr_mrh;		/* driver's (pseudo) ring hdl */
	mac_ring_handle_t	mr_hwrh;	/* associated hw ring hdl */
	uint_t			mr_refcnt;	/* Ring references */
	uint64_t		mr_gen_num;	/* generation number */
	mac_impl_t		*mr_mip;	/* pointer to primary's mip */

	kstat_t			*mr_ksp;
	kstat_t			*mr_gz_ksp;
	kstat_t			*mr_hwlane_ksp;
	kstat_t			*mr_hwlane_gz_ksp;

	kmutex_t		mr_lock;
	mac_ring_state_t	mr_state;	/* mr_lock */
	uint_t			mr_flag;	/* mr_lock */
	kcondvar_t		mr_cv;		/* mr_lock */
	kcondvar_t		mr_ref_cv;	/* mr_lock */
	uint_t			mr_worker_state;
	kthread_t		*mr_worker;

	mblk_t			*mr_tx_queue;
	mblk_t			**mr_tx_tailp;
	uint_t			mr_tx_cnt;

	mac_packet_pool_t	*mr_packet_pool;
	mac_descriptor_t	*mr_packet_descriptor;
	void			*mr_contig_mem;
	size_t			mr_contig_mem_length;
	boolean_t		mr_contig_mem_in_use;

	uint_t			mr_poll_gen;
	uint_t			mr_poll_gen_saved;
	uint_t			mr_poll_estimate;
	boolean_t		mr_poll_pending;

	/* NUMA IO objects for mac_ring_t */
	numaio_object_t		*mr_worker_obj;
	numaio_object_t		*mr_intr_obj;

	mac_tx_stats_t		mr_tx_stat;
	mac_rx_stats_t		mr_rx_stat;

	mac_ring_info_t		mr_info;	/* driver supplied info */
};

#define	mr_driver		mr_info.mri_driver
#define	mr_start		mr_info.mri_start
#define	mr_stop			mr_info.mri_stop
#define	mr_post			mr_info.mri_post
#define	mr_stat			mr_info.mri_stat

/*
 * Reference hold and release on mac_ring_t 'mr'
 */
#define	MR_REFHOLD_LOCKED(mr)		{		\
	ASSERT(MUTEX_HELD(&mr->mr_lock));		\
	(mr)->mr_refcnt++;				\
	(mr)->mr_flag |= MR_BUSY;			\
}

#define	MR_REFRELE(mr)		{			\
	mutex_enter(&(mr)->mr_lock);			\
	ASSERT((mr)->mr_refcnt != 0);			\
	(mr)->mr_refcnt--;				\
	(mr)->mr_flag &= ~MR_BUSY;			\
	if ((mr)->mr_refcnt == 0 && ((mr)->mr_flag &	\
	    (MR_QUIESCE)))				\
		cv_signal(&(mr)->mr_ref_cv);		\
	mutex_exit(&(mr)->mr_lock);			\
}

/*
 * Per mac client flow information associated with a RX group.
 * The entire structure is SL protected.
 */
typedef struct mac_grp_client {
	struct mac_grp_client		*mgc_next;
	struct mac_client_impl_s	*mgc_client;
} mac_grp_client_t;

#define	MAC_GROUP_NO_CLIENT(g)	((g)->mrg_clients == NULL)

#define	MAC_GROUP_ONLY_CLIENT(g)			\
	((((g)->mrg_clients != NULL) &&			\
	((g)->mrg_clients->mgc_next == NULL)) ?		\
	(g)->mrg_clients->mgc_client : NULL)

/*
 * Define Hardware Group capabilities.
 */
typedef enum {
	MRG_CAPAB_VLAN_FILTER = 0x001,	/* Able to do vlan filtering */
	MRG_CAPAB_SRIOV = 0x002,	/* An SRIOV capable group */
	MRG_CAPAB_MTU = 0x004,		/* Able to set MTU for the group */
	MRG_CAPAB_VLAN_TAG = 0x008	/* Able to do tagging. */
} mrg_capab_t;

#define	MRG_SRIOV(group) \
	((group->mrg_capab & MRG_CAPAB_SRIOV) == MRG_CAPAB_SRIOV)
#define	MRG_VLAN_FILTER(group) \
	((group->mrg_capab & MRG_CAPAB_VLAN_FILTER) == MRG_CAPAB_VLAN_FILTER)

typedef enum {
	MRG_NONE = 0,
	MRG_DEFAULT = 0x001,		/* Group is the default group. */
	MRG_VLAN_ALL = 0x002,		/* Group tags/strips all vlans */
	MRG_VLAN_TRANSPARENT = 0x004, 	/* Group transparent vlan */
	MRG_POOL = 0x008,		/* Group is a pool group. */
	MRG_QUIESCE = 0x010		/* Group is being quiesced */
} mrg_flags_t;

#define	MRG_DEFAULT(group) \
	((group->mrg_flags & MRG_DEFAULT) == MRG_DEFAULT)

/*
 * We maintain a per-DCB priority list of rings. In the non-DCB mode
 * there will only be one list containing all the rings in that group.
 */
typedef struct mac_rings_cache_s {
	uint_t		mrc_count;
	mac_ring_t	**mrc_rings;
} mac_rings_cache_t;

/*
 * Common ring group data structure for ring control and management.
 * The entire structure is SL protected
 */
struct mac_group_s {
	int			mrg_index;	/* index in the list */
	mac_ring_type_t		mrg_type;	/* ring type */
	mac_group_state_t	mrg_state;	/* state of the group */
	mac_group_t		*mrg_next;	/* next ring in the chain */
	mac_handle_t		mrg_mh;		/* reference to MAC */
	mac_ring_t		*mrg_rings;	/* grouped rings */
	mac_rings_cache_t	*mrg_rings_cache;
	uint_t			mrg_cur_count;	/* actual size of group */
	uint_t			mrg_cache_count; /* cache count */
	struct mac_vlan_s	*mrg_vlans;	/* group vlans */
	uint32_t		mrg_nvlans;	/* number of vlans in use */
	mac_client_handle_t	mrg_mch;	/* mac client handle */
	mrg_capab_t		mrg_capab;	/* Group Capabilities */
	mrg_flags_t		mrg_flags;	/* Group flags */
	uint32_t		mrg_vf_index;	/* VF index for the grp */
	mac_grp_client_t	*mrg_clients;	/* clients list */
	flow_entry_t		*mrg_flent;	/* flow entry */
	mac_group_info_t	mrg_info;	/* driver supplied info */
	uint32_t		mrg_mtu;	/* Group mtu */
};

#define	mrg_driver		mrg_info.mgi_driver
#define	mrg_start		mrg_info.mgi_start
#define	mrg_stop		mrg_info.mgi_stop

#define	MAC_RING_TX(mhp, rh, mp, rest) {				\
	mac_ring_handle_t mrh = rh;					\
	mac_impl_t *mimpl = (mac_impl_t *)mhp;				\
	/*								\
	 * Send packets through a selected tx ring, or through the 	\
	 * default handler if there is no selected ring.		\
	 */								\
	if (mrh == NULL)						\
		mrh = mimpl->mi_default_tx_ring;			\
	if (mrh == NULL) {						\
		rest = mimpl->mi_tx(mimpl->mi_driver, mp);		\
	} else {							\
		rest = mac_hwring_tx(mrh, mp);				\
	}								\
}

/*
 * This is the final stop before reaching the underlying driver
 * or aggregation, so this is where the bridging hook is implemented.
 * Packets that are bridged will return through mac_tx_bridge(), with
 * rh nulled out if the bridge chooses to send output on a different
 * link due to forwarding.
 */
#define	MAC_TX(mip, rh, mp, src_mcip) {					\
	mac_ring_handle_t	rhandle = (rh);				\
	/*								\
	 * If there is a bound Hybrid I/O share, send packets through 	\
	 * the default tx ring. (When there's a bound Hybrid I/O share,	\
	 * the tx rings of this client are mapped in the guest domain 	\
	 * and not accessible from here.)				\
	 */								\
	_NOTE(CONSTANTCONDITION)					\
	if ((src_mcip)->mci_state_flags & MCIS_SHARE_BOUND)		\
		rhandle = (mip)->mi_default_tx_ring;			\
	if (mip->mi_promisc_list != NULL)				\
		mac_promisc_dispatch(mip, mp, src_mcip);		\
	/*								\
	 * Grab the proper transmit pointer and handle. Special 	\
	 * optimization: we can test mi_bridge_link itself atomically,	\
	 * and if that indicates no bridge send packets through tx ring.\
	 */								\
	if (mip->mi_bridge_link == NULL) {				\
		MAC_RING_TX(mip, rhandle, mp, mp);			\
	} else {							\
		(void) mac_tx_bridge(mip, rhandle, mp,			\
		    MAC_DROP_ON_NO_DESC, NULL);				\
		mp = NULL;						\
	}								\
}

/* mci_tx_flag */
#define	MCI_TX_QUIESCE	0x1

typedef struct mac_factory_addr_s {
	boolean_t		mfa_in_use;
	uint8_t			mfa_addr[MAXMACADDRLEN];
	struct mac_client_impl_s	*mfa_client;
} mac_factory_addr_t;

typedef struct mac_mcast_addrs_s {
	struct mac_mcast_addrs_s	*mma_next;
	uint8_t				mma_addr[MAXMACADDRLEN];
	int				mma_ref;
} mac_mcast_addrs_t;

typedef enum {
	MAC_ADDRESS_TYPE_UNICAST_CLASSIFIED = 1,	/* hardware steering */
	MAC_ADDRESS_TYPE_UNICAST_PROMISC		/* promiscuous mode */
} mac_address_type_t;

typedef struct mac_address_s {
	mac_address_type_t	ma_type;		/* address type */
	int			ma_nusers;		/* number of users */
							/* of that address */
	struct mac_address_s	*ma_next;		/* next address */
	uint8_t			ma_addr[MAXMACADDRLEN];	/* address value */
	size_t			ma_len;			/* address length */
	mac_group_t		*ma_group;		/* asscociated group */
	mac_impl_t		*ma_mip;		/* MAC handle */
} mac_address_t;

typedef void	(*mac_rx_cb_t)(mac_handle_t, mac_ring_handle_t,
		    mblk_t *, uint_t);
extern void	mac_rx_common(mac_handle_t, mac_ring_handle_t,
		    mblk_t *, uint_t);
extern void	mac_rx_bridge(mac_handle_t, mac_ring_handle_t,
		    mblk_t *, uint_t);

typedef struct mac_vlan_s {
	struct mac_vlan_s	*mv_next;		/* next address */
	int			mv_nusers;		/* number of users */
							/* of that vlan */
	uint16_t		mv_vid;			/* vlan id */
	mac_group_t		*mv_group;		/* asscociated group */
	mac_impl_t		*mv_mip;		/* MAC handle */
	boolean_t		mv_transparent;		/* Transparent */
} mac_vlan_t;

#define	MAC_RX_INTR		0x0001
#define	MAC_RX_POLL		0x0002
#define	MAC_RX_HW		0x0004
#define	MAC_RX_SW		0x0008
#define	MAC_RX_LOOPBACK		0x0010
#define	MAC_RX_MULTICAST	0x0020
#define	MAC_RX_BROADCAST	0x0040

extern krwlock_t i_mac_impl_lock;
extern mod_hash_t *i_mac_impl_hash;
extern kmem_cache_t *i_mac_impl_cachep;
extern uint_t i_mac_impl_count;

/*
 * The virtualization level conveys the extent of the NIC hardware assistance
 * for traffic steering employed for virtualization:
 *
 * MI_VIRT_NONE:	No assist for v12n.
 *
 * MI_VIRT_LEVEL1:	Multiple Rx rings with MAC address level
 *			classification between groups of rings.
 *			Requires the support of the MAC_CAPAB_RINGS
 *			capability.
 *
 * MI_VIRT_HIO:		Hybrid I/O capable MAC. Require the support
 *			of the MAC_CAPAB_SHARES capability.
 */
#define	MI_VIRT_NONE	0x000
#define	MI_VIRT_LEVEL1	0x001
#define	MI_VIRT_HIO	0x002

/*
 * Each registered MAC is associated with a mac_impl_t structure. The
 * structure represents the undelying hardware, in terms of definition,
 * resources (transmit, receive rings etc.), callback functions etc. It
 * also holds the table of MAC clients that are configured on the device.
 * The table is used for classifying incoming packets in software.
 *
 * The protection scheme uses 2 elements, a coarse serialization mechanism
 * called perimeter and a finer traditional lock based scheme. More details
 * can be found in the big block comment in mac.c.
 *
 * The protection scheme for each member of the mac_impl_t is described below.
 *
 * Write Once Only (WO): Typically these don't change for the lifetime of the
 * data structure. For example something in mac_impl_t that stays the same
 * from mac_register to mac_unregister, or something in a mac_client_impl_t
 * that stays the same from mac_client_open to mac_client_close.
 *
 * Serializer (SL): Protected by the Serializer. All SLOP operations on a
 * mac endpoint go through the serializer. MTOPs don't care about reading
 * these fields atomically.
 *
 * Lock: Traditional mutex/rw lock. Modify operations still go through the
 * mac serializer, the lock helps synchronize readers with writers.
 */
struct mac_impl_s {
	krwlock_t		mi_rw_lock;
	char			mi_name[LIFNAMSIZ];	/* WO */
	char			mi_linkname[LIFNAMSIZ];	/* SL */
	datalink_id_t		mi_linkid;		/* WO */
	uint32_t		mi_state_flags;
	void			*mi_driver;		/* Driver private, WO */
	mac_info_t		mi_info;		/* WO */
	mactype_t		*mi_type;		/* WO */
	void			*mi_pdata;		/* mi_pdata_lock */
	size_t			mi_pdata_size;		/* mi_pdata_lock */
	mac_callbacks_t		*mi_callbacks;		/* WO */
	mac_rx_cb_t		mi_rx;			/* WO */
	dev_info_t		*mi_dip;		/* WO */
	uint32_t		mi_ref;			/* i_mac_impl_lock */
	uint_t			mi_active;		/* SL */
	uint32_t		mi_mem_ref_cnt;
	link_state_t		mi_linkstate;		/* none */
	link_state_t		mi_lowlinkstate;	/* none */
	link_state_t		mi_lastlowlinkstate;	/* none */
	uint_t			mi_devpromisc;		/* SL */
	uint8_t			mi_addr[MAXMACADDRLEN];	/* mi_rw_lock */
	uint8_t			mi_defaddr[MAXMACADDRLEN]; /* mi_rw_lock */
	uint8_t			mi_dstaddr[MAXMACADDRLEN]; /* mi_rw_lock */
	boolean_t		mi_dstaddr_set;

	/*
	 * The mac perimeter. All client initiated create/modify operations
	 * on a mac end point go through this.
	 */
	kmutex_t		mi_perim_lock;
	kthread_t		*mi_perim_owner;	/* mi_perim_lock */
	uint_t			mi_perim_ocnt;		/* mi_perim_lock */
	kcondvar_t		mi_perim_cv;		/* mi_perim_lock */

	/* mac notification callbacks */
	kmutex_t		mi_notify_lock;
	mac_cb_info_t		mi_notify_cb_info;	/* mi_notify_lock */
	mac_cb_t		*mi_notify_cb_list;	/* mi_notify_lock */
	kthread_t		*mi_notify_thread;	/* mi_notify_lock */
	uint_t			mi_notify_bits;		/* mi_notify_lock */

	kmutex_t		mi_pdata_lock;		/* mi_pdata_lock  */

	uint32_t		mi_v12n_level;		/* Virt'ion readiness */

	/*
	 * Buffer Management
	 */
	mac_capab_bm_t		mi_bm_tx_cap;
	boolean_t		mi_bm_tx_enabled;
	mac_capab_bm_t		mi_bm_rx_cap;
	boolean_t		mi_bm_rx_enabled;
	uint32_t		mi_bm_tx_block_cnt;
	uint32_t		mi_bm_rx_block_cnt;
	kmutex_t		mi_bm_list_lock;
	list_t			mi_bm_rx_block_list;
	list_t			mi_bm_tx_block_list;
	pm_handle_t		mi_pmh_tx_drv;	/* premapped info handle */
	pm_handle_t		mi_pmh_tx;	/* premapped info handle */

	/*
	 * RX groups, ring capability
	 * Fields of this block are SL protected.
	 */
	mac_group_type_t	mi_rx_group_type;	/* grouping type */
	uint_t			mi_rx_group_count;
	mac_group_t		*mi_rx_groups;
	mac_group_t		*mi_rx_defaultgrp;
	mac_group_t		*mi_rx_vf_groups;
	mac_group_t		*mi_rx_groups_alloc;
	uint_t			mi_rx_groups_alloc_count;
	mac_group_t		*mi_rx_donor_grp;
	mac_group_t		*mi_default_rx_group;
	boolean_t		mi_rx_vlan_transparent;
	uint_t			mi_rxrings_rsvd;
	uint_t			mi_rxrings_avail;
	uint_t			mi_rxhwclnt_avail;
	uint_t			mi_rxhwclnt_used;

	mac_capab_rings_t	mi_rx_rings_cap;

	/*
	 * TX groups and ring capability, SL Protected.
	 */
	mac_group_type_t	mi_tx_group_type;	/* grouping type */
	uint_t			mi_tx_group_count;
	uint_t			mi_tx_group_free;
	mac_group_t		*mi_tx_groups;
	mac_group_t		*mi_tx_defaultgrp;
	mac_group_t		*mi_tx_vf_groups;
	mac_group_t		*mi_tx_groups_alloc;
	uint_t			mi_tx_groups_alloc_count;
	mac_capab_rings_t	mi_tx_rings_cap;
	uint_t			mi_txrings_rsvd;
	uint_t			mi_txrings_avail;
	uint_t			mi_txhwclnt_avail;
	uint_t			mi_txhwclnt_used;

	mac_group_t		*mi_default_tx_group;
	mac_ring_handle_t	mi_default_tx_ring;
	mac_ring_t		mi_fake_tx_ring;

	/*
	 * FCoE offload capability
	 */
	mac_capab_fcoe_t	mi_fcoe_cap;

	/*
	 * MAC address list. SL protected.
	 */
	mac_address_t		*mi_addresses;

	/*
	 * This MAC's table of sub-flows
	 */
	flow_tab_t		*mi_flow_tab;		/* WO */

	kstat_t			*mi_ksp;		/* WO */
	uint_t			mi_kstat_count;		/* WO */
	uint_t			mi_nactiveclients;	/* SL */

	/* for broadcast and multicast support */
	struct mac_mcast_addrs_s *mi_mcast_addrs;	/* mi_rw_lock */
	struct mac_bcast_grp_s *mi_bcast_grp;		/* mi_rw_lock */
	uint_t			mi_bcast_ngrps;		/* mi_rw_lock */

	/* list of MAC clients which opened this MAC */
	struct mac_client_impl_s *mi_clients_list;	/* mi_rw_lock */
	uint_t			mi_nclients;		/* mi_rw_lock */
	struct mac_client_impl_s *mi_single_active_client; /* mi_rw_lock */

	uint32_t		mi_margin;		/* mi_rw_lock */
	uint_t			mi_sdu_min;		/* mi_rw_lock */
	uint_t			mi_sdu_max;		/* mi_rw_lock */
	uint_t			mi_sdu_multicast;	/* mi_rw_lock */

	/*
	 * Cache of factory MAC addresses provided by the driver. If
	 * the driver doesn't provide multiple factory MAC addresses,
	 * the mi_factory_addr is set to NULL, and mi_factory_addr_num
	 * is set to zero.
	 */
	mac_factory_addr_t	*mi_factory_addr;	/* mi_rw_lock */
	uint_t			mi_factory_addr_num;	/* mi_rw_lock */

	/* for promiscuous mode support */
	kmutex_t		mi_promisc_lock;
	mac_cb_t		*mi_promisc_list;	/* mi_promisc_lock */
	mac_cb_info_t		mi_promisc_cb_info;	/* mi_promisc_lock */

	/* cache of rings over this mac_impl */
	kmutex_t		mi_ring_lock;
	mac_ring_t		*mi_ring_freelist;	/* mi_ring_lock */
	mac_ring_t		*mi_ring_list;		/* mi_ring_lock */

	/*
	 * These are used for caching the properties, if any, for the
	 * primary MAC client. If the MAC client is not yet in place
	 * when the properties are set then we cache them here to be
	 * applied to the MAC client when it is created.
	 */
	mac_resource_props_t	mi_resource_props;	/* SL */
	uint16_t		mi_pvid;		/* SL */

	minor_t			mi_minor;		/* WO */
	uint32_t		mi_oref;		/* SL */
	mac_capab_legacy_t	mi_capab_legacy;	/* WO */
	dev_t			mi_phy_dev;		/* WO */

	/*
	 * List of margin value requests added by mac clients. This list is
	 * sorted: the first one has the greatest value.
	 */
	mac_margin_req_t	*mi_mmrp;
	char			**mi_priv_prop;
	uint_t			mi_priv_prop_count;

	/*
	 * Hybrid I/O related definitions.
	 */
	mac_capab_share_t	mi_share_capab;

	/*
	 * Bridging hooks and limit values.  Uses mutex and reference counts
	 * (bridging only) for data path.  Limits need no synchronization.
	 */
	mac_handle_t		mi_bridge_link;
	kmutex_t		mi_bridge_lock;
	uint32_t		mi_llimit;
	uint32_t		mi_ldecay;

	/*
	 * GVRP structures
	 */
	mac_vlan_announce_t	mi_vlan_announce;	/* mi_rw_lock */
	uint32_t		mi_gvrp_timeout;	/* mi_rw_lock */
	mac_gvrp_active_link_t	*gvrp_link;
	mac_client_handle_t	mi_gvrp_mch;		/* mi_rw_lock */
	mac_unicast_handle_t	mi_gvrp_muh;		/* mi_rw_lock */
	mac_gvrp_vid_list_t 	*mi_vid_list;		/* mi_rw_lock */

	/*
	 * zoneid of this mac impl, default is GLOBAL_ZONE, set to the ngz's id
	 * when the link is dedicated to a ngz.
	 */
	zoneid_t		mi_zoneid;

	/* DCB related information */
	uint32_t		mi_dcb_flags;
	uint8_t			mi_ntcs;
	boolean_t		mi_vtag;

/* This should be the last block in this structure */
#ifdef DEBUG
#define	MAC_PERIM_STACK_DEPTH	15
	int			mi_perim_stack_depth;
	pc_t			mi_perim_stack[MAC_PERIM_STACK_DEPTH];
#endif
};

/* Maximum number of DCB Traffic Classes supported */
#define	MAX_DCB_NTCS		8

/*
 * MAC block pool:
 *    used to maintain blocks of memory
 *    allocated for packet buffers out of a kmem cache.
 */
struct mac_block_pool_s {
	mac_handle_t		mbc_handle;
	mac_buffer_mgmt_type_t	mbc_type;
	kmem_cache_t		*mbc_cachep;
	kmutex_t		mbc_lock;
	uint64_t		mbc_ref_cnt;
};

/*
 * MAC block memory
 *
 * This structure contains the data associated with an allocated
 * buffer that has been IO mapped by the mac layer on the behalf
 * of the driver using the buffer.
 */
typedef struct mac_block_s {
	mac_handle_t		mbm_handle;		/* MAC handle */
	mac_ring_handle_t	mbm_ring_handle;	/* Ring associated */
	size_t			mbm_length;		/* Length of buffer */

	caddr_t			mbm_kaddrp;		/* Kernel Virt Addr */
	uint64_t		mbm_ioaddrp;		/* IO Address */
	ddi_dma_cookie_t	mbm_dma_cookie;		/* DDI DMA Cookie */
	ddi_dma_handle_t	mbm_dma_handle;		/* DDI DMA Handle */
	ddi_acc_handle_t	mbm_acc_handle;		/* DDI Access Handle */
	uint32_t		mbm_ncookies;		/* # of DMA Cookies */
	dma_mem_type_t		mbm_mem_type;
	boolean_t		mbm_unbound;
	uint64_t		mbm_flags;		/* block mbm flags */
	size_t			mbm_cur_offset;
	uint32_t		mbm_pkt_cnt;
	mac_capab_bm_t		*mbm_bm;
	list_node_t		mbm_list;
} mac_block_t;

/*
 * MAC descriptor block
 *
 * This structure describes the descriptor memory used by NIC drivers.
 */
struct mac_descriptor_s {
	mac_handle_t		md_handle;
	mac_ring_handle_t	md_ring_handle;
	mac_block_t		*md_mblockp;
	uint32_t		md_descriptors;	/* Number of descriptors */
	uint32_t		md_min;		/* Minimum descriptor */
	uint32_t		md_max;		/* Maximum descriptor */
	size_t			md_size;	/* Size of a descriptor */

	uint8_t			md_pad[16];
};

/*
 * MAC packet
 *
 * This structure contains the data associated with an allocated mblk
 * that has been DMA mapped by the MAC layer on the behalf of the driver.
 * The mac_packet_t itself does not reference any mblk or dblk directly.
 * mblks reference the mac_packet_t via the db_packet pointer.
 * The mac_packet_t is associated 1-1 with the block of memory that
 * is DMA mapped for the life of the mac_packet_t which is typically from
 * mac_start to mac_stop. The mblk/dblk pairs that are associated with
 * a mac_packet_t are transient. They are allocated initially using
 * desballoc_reusable() and STREAMS may choose to either recycle the
 * same dblk_t or allocate a new one, on the last freeb(). The new mblk
 * is passed as a parameter to the freeb() callback.
 */
typedef struct mac_packet_s {
	mac_block_t		*mp_dm_p;	/* mac_block_t */
	mac_packet_pool_t	*mp_pool;	/* Packet Pool */
	boolean_t		mp_unbound;	/* Bound or unbound */
	boolean_t		mp_reserved;

	ddi_dma_cookie_t	mp_dma_cookie;	/* DDI DMA Cookie */
	ddi_dma_handle_t	mp_dma_handle;	/* DDI DMA Handle */
	size_t			mp_length;	/* Length of packet */

	caddr_t			mp_kaddr;	/* Kernel Virt Addr */
	uint64_t		mp_ioaddr;	/* IO Address */
	frtn_t			mp_free_rtn;	/* freemsg() callback */

	uint32_t		mp_index;
	uint8_t			mp_pad[28];	/* for 64 bytes alignment */
} mac_packet_t;

/*
 * MAC packet pool list.
 */
typedef struct mac_packet_list_s {
	kmutex_t	mpl_lock;	/* Lock for mac pool list. */
	mblk_t		*mpl_head;	/* Head */
	mblk_t		*mpl_tail;	/* Tail */
	uint32_t	mpl_cnt;	/* Count of packets in the list */
	uint8_t		mpl_pads[36];	/* for 64 bytes alignment */
} mac_packet_list_t;

#define	MAC_PACKET_RECYCLE_THRESHOLD	16
/*
 * SPARC MAC_PACKET_RECYCLE_LISTS value is higher than i386, since SPARC has
 * more number of CPUs.
 */
#if defined(__sparc)
#define	MAC_PACKET_RECYCLE_LISTS	16
#else
#define	MAC_PACKET_RECYCLE_LISTS	8
#endif

#define	MAC_PACKET_LIST(id) \
	((uint32_t)id % MAC_PACKET_RECYCLE_LISTS)

/*
 * MAC layer packet pool.
 */
struct mac_packet_pool_s {
	mac_handle_t		mpp_mh;
	mac_ring_handle_t	mpp_mrh;
	kmutex_t		mpp_lock;

	boolean_t		mpp_offline;
	boolean_t		mpp_pad;

	/*
	 * mpp_nic_pool -- packets to be loaned to the underlying
	 *	network interface card.
	 * mpp_recycle_pools -- packets returned to the MAC layer
	 *	that will accummulate to a threshold and the be
	 *	returned to the mpp_nic_pool. This is to reduce lock
	 *	contention when upper layers are returning packets.
	 */
	mac_packet_list_t	mpp_nic_pool;
	mac_packet_list_t	mpp_recycle_pools[MAC_PACKET_RECYCLE_LISTS];

	/*
	 * mpp_list -- list of all packets associated with the pool loaned
	 *	out or otherwise.  Used for unmapping buffers when the
	 *	interface is unplumbed.
	 */
	mac_packet_t		**mpp_list;

	uint32_t		mpp_min_buffers;
	uint32_t		mpp_max_buffers;
	uint32_t		mpp_desired_buffers;
	uint32_t		mpp_pad1;

	/*
	 * mpp_buffer_size -- buffer size to be used for creating packets.
	 * mpp_offset -- Offset to start the packet at.   This is used
	 *	to align packets buffers IP headers properly.
	 */
	size_t			mpp_buffer_size;
	off_t			mpp_offset;

	/*
	 * mpp_npackets -- Number of packets in this packet pool.
	 */
	uint32_t		mpp_npackets;

	/*
	 * mpp_mem_ref_cnt -- reference count to see if the pool can
	 *	be destroyed.
	 */
	uint32_t		mpp_mem_ref_cnt;
	uint8_t			mpp_pads[48];	/* for 64 bytes alignment */
};

extern boolean_t mac_bm_single_block_per_ring;

/*
 * The default TX group is the last one in the list.
 */
#define	MAC_DEFAULT_TX_GROUP(mip) 	(mip)->mi_tx_defaultgrp

/*
 * The default RX group is the first one in the list
 */
#define	MAC_DEFAULT_RX_GROUP(mip)	(mip)->mi_rx_defaultgrp

/* Reserved RX rings */
#define	MAC_RX_RING_RESERVED(m, cnt)	{	\
	ASSERT((m)->mi_rxrings_avail >= (cnt));	\
	(m)->mi_rxrings_rsvd += (cnt);		\
	(m)->mi_rxrings_avail -= (cnt);		\
}

/* Released RX rings */
#define	MAC_RX_RING_RELEASED(m, cnt)	{	\
	ASSERT((m)->mi_rxrings_rsvd >= (cnt));	\
	(m)->mi_rxrings_rsvd -= (cnt);		\
	(m)->mi_rxrings_avail += (cnt);		\
}

/* Reserved a RX group */
#define	MAC_RX_GRP_RESERVED(m)	{		\
	ASSERT((m)->mi_rxhwclnt_avail > 0);	\
	(m)->mi_rxhwclnt_avail--;		\
	(m)->mi_rxhwclnt_used++;		\
}

/* Released a RX group */
#define	MAC_RX_GRP_RELEASED(m)	{		\
	ASSERT((m)->mi_rxhwclnt_used > 0);	\
	(m)->mi_rxhwclnt_avail++;		\
	(m)->mi_rxhwclnt_used--;		\
}

/* Reserved TX rings */
#define	MAC_TX_RING_RESERVED(m, cnt)	{	\
	ASSERT((m)->mi_txrings_avail >= (cnt));	\
	(m)->mi_txrings_rsvd += (cnt);		\
	(m)->mi_txrings_avail -= (cnt);		\
}
/* Released TX rings */
#define	MAC_TX_RING_RELEASED(m, cnt)	{	\
	ASSERT((m)->mi_txrings_rsvd >= (cnt));	\
	(m)->mi_txrings_rsvd -= (cnt);		\
	(m)->mi_txrings_avail += (cnt);		\
}

/* Reserved a TX group */
#define	MAC_TX_GRP_RESERVED(m)	{		\
	ASSERT((m)->mi_txhwclnt_avail > 0);	\
	(m)->mi_txhwclnt_avail--;		\
	(m)->mi_txhwclnt_used++;		\
}

/* Released a TX group */
#define	MAC_TX_GRP_RELEASED(m)	{		\
	ASSERT((m)->mi_txhwclnt_used > 0);	\
	(m)->mi_txhwclnt_avail++;		\
	(m)->mi_txhwclnt_used--;		\
}

/* for mi_state_flags */
#define	MIS_DISABLED		0x0001
#define	MIS_IS_VNIC		0x0002
#define	MIS_IS_AGGR		0x0004
#define	MIS_NOTIFY_DONE		0x0008
#define	MIS_EXCLUSIVE		0x0010
#define	MIS_EXCLUSIVE_HELD	0x0020
#define	MIS_LEGACY		0x0040
#define	MIS_NO_ACTIVE		0x0080
#define	MIS_MULTICAST_ONLY	0x0100
#define	MIS_RX_BLOCK		0x0200
#define	MIS_TX_BLOCK		0x0400

#define	mi_getstat	mi_callbacks->mc_getstat
#define	mi_start	mi_callbacks->mc_start
#define	mi_stop		mi_callbacks->mc_stop
#define	mi_open		mi_callbacks->mc_open
#define	mi_close	mi_callbacks->mc_close
#define	mi_setpromisc	mi_callbacks->mc_setpromisc
#define	mi_multicst	mi_callbacks->mc_multicst
#define	mi_unicst	mi_callbacks->mc_unicst
#define	mi_tx		mi_callbacks->mc_tx
#define	mi_ioctl	mi_callbacks->mc_ioctl
#define	mi_getcapab	mi_callbacks->mc_getcapab

typedef struct mac_notify_task_arg {
	mac_impl_t		*mnt_mip;
	mac_notify_type_t	mnt_type;
	mac_ring_t		*mnt_ring;
} mac_notify_task_arg_t;

/*
 * XXX All MAC_DBG_PRTs must be replaced with call to dtrace probes. For now
 * it may be easier to have these printfs for easier debugging
 */
#ifdef DEBUG
extern int mac_dbg;
#define	MAC_DBG_PRT(a)	if (mac_dbg > 0) {(void) printf a; }
#else
#define	MAC_DBG_PRT(a)
#endif

/*
 * The mac_perim_handle_t is an opaque type that encodes the 'mip' pointer
 * and whether internally a mac_open was done when acquiring the perimeter.
 */
#define	MAC_ENCODE_MPH(mph, mh, need_close)		\
	(mph) = (mac_perim_handle_t)((uintptr_t)(mh) | need_close)

#define	MAC_DECODE_MPH(mph, mip, need_close) {		\
	mip = (mac_impl_t *)(((uintptr_t)mph) & ~0x1);	\
	(need_close) = ((uintptr_t)mph & 0x1);		\
}

/*
 * Type of property information that can be returned by a driver.
 * Valid flags of the pr_flags of the mac_prop_info_t data structure.
 */
#define	MAC_PROP_INFO_DEFAULT	0x0001
#define	MAC_PROP_INFO_RANGE	0x0002
#define	MAC_PROP_INFO_PERM	0x0004

/*
 * Property information. pr_flags is a combination of one of the
 * MAC_PROP_INFO_* flags, it is reset by the framework before invoking
 * the driver's prefix_propinfo() entry point.
 *
 * Drivers should use MAC_PROP_INFO_SET_*() macros to provide
 * information about a property.
 */
typedef struct mac_prop_info_state_s {
	uint8_t			pr_flags;
	uint8_t			pr_perm;
	uint8_t			pr_errno;
	void			*pr_default;
	size_t			pr_default_size;
	mac_propval_range_t	*pr_range;
	uint_t			pr_range_cur_count;
} mac_prop_info_state_t;

#define	MAC_PROTECT_ENABLED(mcip, type) \
	(((mcip)->mci_flent-> \
	fe_resource_props.mrp_mask & MRP_PROTECT) != 0 && \
	((mcip)->mci_flent-> \
	fe_resource_props.mrp_protect.mp_types & (type)) != 0)

typedef struct mac_client_impl_s mac_client_impl_t;

extern void	mac_init(void);
extern int	mac_fini(void);

extern void	mac_ndd_ioctl(mac_impl_t *, queue_t *, mblk_t *);
extern boolean_t mac_ip_hdr_length_v6(ip6_t *, uint8_t *, uint16_t *,
    uint8_t *, ip6_frag_t **);

extern mblk_t *mac_copymsgchain_cksum(mblk_t *);
extern mblk_t *mac_fix_cksum(mblk_t *, boolean_t, uint16_t);
extern void mac_packet_print(mac_handle_t, mblk_t *);
extern void mac_rx_deliver(void *, void *, mblk_t *, uint_t);
extern void mac_rx_deliver_fanout(void *, void *, mblk_t *, uint_t);
extern void mac_tx_notify(mac_impl_t *);

extern	boolean_t mac_callback_find(mac_cb_info_t *, mac_cb_t **, mac_cb_t *);
extern	void	mac_callback_add(mac_cb_info_t *, mac_cb_t **, mac_cb_t *);
extern	boolean_t mac_callback_remove(mac_cb_info_t *, mac_cb_t **, mac_cb_t *);
extern	void	mac_callback_remove_wait(mac_cb_info_t *);
extern	void	mac_callback_free(mac_cb_t *);
extern	mac_cb_t *mac_callback_walker_cleanup(mac_cb_info_t *, mac_cb_t **);

/* in mac_bcast.c */
extern void mac_bcast_init(void);
extern void mac_bcast_fini(void);
extern mac_impl_t *mac_bcast_grp_mip(void *);
extern int mac_bcast_add(mac_client_impl_t *, const uint8_t *, uint16_t,
    mac_addrtype_t);
extern void mac_bcast_delete(mac_client_impl_t *, const uint8_t *, uint16_t);
extern void mac_bcast_send(void *, void *, mblk_t *, uint_t);
extern void mac_bcast_grp_free(void *);
extern void mac_bcast_refresh(mac_impl_t *, mac_multicst_t, void *,
    boolean_t);
extern void mac_client_bcast_refresh(mac_client_impl_t *, mac_multicst_t,
    void *, boolean_t);

/*
 * Grouping functions are used internally by MAC layer.
 */
extern int mac_group_addmac(mac_group_t *, const uint8_t *, boolean_t);
extern int mac_group_remmac(mac_group_t *, const uint8_t *);
extern int mac_group_add_vlan(mac_group_t *, uint16_t, uint32_t);
extern int mac_group_rem_vlan(mac_group_t *, uint16_t);
extern int mac_group_set_mtu(mac_group_t *, uint32_t, uint32_t *);
extern int mac_rx_group_add_flow(mac_client_impl_t *, flow_entry_t *,
    mac_group_t *);
extern mblk_t *mac_hwring_tx(mac_ring_handle_t, mblk_t *);
extern boolean_t mac_tx_bridge(mac_impl_t *, mac_ring_handle_t, mblk_t *,
    uint16_t, mblk_t **);
extern mac_group_t *mac_reserve_rx_group(mac_client_impl_t *, uint8_t *,
    boolean_t);
extern void mac_release_rx_group(mac_client_impl_t *, mac_group_t *);
extern int mac_rx_switch_group(mac_client_impl_t *, mac_group_t *,
    mac_group_t *);
extern mac_ring_t *mac_reserve_tx_ring(mac_impl_t *, mac_ring_t *);
extern mac_group_t *mac_reserve_tx_group(mac_client_impl_t *, boolean_t);
extern void mac_release_tx_group(mac_client_impl_t *, mac_group_t *);
extern void mac_tx_switch_group(mac_client_impl_t *, mac_group_t *,
    mac_group_t *);
extern mac_client_impl_t *mac_get_grp_primary(mac_group_t *);


/*
 * MAC address functions are used internally by MAC layer.
 */
extern mac_address_t *mac_find_macaddr(mac_impl_t *, uint8_t *);
extern boolean_t mac_check_macaddr_shared(mac_address_t *);
extern int mac_update_macaddr(mac_address_t *, uint8_t *);
extern void mac_freshen_macaddr(mac_address_t *, uint8_t *);
extern void mac_retrieve_macaddr(mac_address_t *, uint8_t *);
extern void mac_init_macaddr(mac_impl_t *);
extern void mac_fini_macaddr(mac_impl_t *);

/*
 * Flow construction/destruction routines.
 * Not meant to be used by mac clients.
 */
extern int mac_link_flow_init(mac_client_handle_t, flow_entry_t *);
extern void mac_link_flow_clean(mac_client_handle_t, flow_entry_t *);

/*
 * The following functions are used internally by the MAC layer to
 * add/remove/update flows associated with a mac_impl_t. They should
 * never be used directly by MAC clients.
 */
extern int mac_datapath_setup(mac_client_impl_t *, flow_entry_t *, uint16_t);
extern void mac_datapath_teardown(mac_client_impl_t *);
extern void mac_rx_group_setup(mac_client_impl_t *);
extern void mac_tx_group_setup(mac_client_impl_t *);
extern void mac_rx_group_teardown(mac_client_impl_t *);
extern void mac_tx_group_teardown(mac_client_impl_t *);

extern int mac_rx_classify_flow_quiesce(flow_entry_t *, void *);
extern int mac_rx_classify_flow_restart(flow_entry_t *, void *);
extern void mac_client_quiesce(mac_client_impl_t *);
extern void mac_client_restart(mac_client_impl_t *);

extern void mac_flow_update_priority(mac_client_impl_t *);

extern void mac_flow_rem_subflow(flow_entry_t *);
extern void mac_rename_flow(flow_entry_t *, const char *);
extern void mac_flow_set_name(flow_entry_t *, const char *);

extern mblk_t *mac_add_vlan_tag(mblk_t *, uint_t, uint16_t);
extern mblk_t *mac_add_vlan_tag_chain(mblk_t *, uint_t, uint16_t);
extern mblk_t *mac_strip_vlan_tag_chain(mblk_t *);
extern mblk_t *mac_strip_vlan_tag(mblk_t *);
extern void mac_pkt_drop(void *, void *, mblk_t *, uint_t);
extern void mac_pkt_drop_old(void *, mac_resource_handle_t, mblk_t *,
    boolean_t);

extern void i_mac_share_alloc(mac_client_impl_t *);
extern void i_mac_share_free(mac_client_impl_t *);
extern void i_mac_perim_enter(mac_impl_t *);
extern void i_mac_perim_exit(mac_impl_t *);
extern void i_mac_setup_enter(mac_impl_t *);
extern void i_mac_setup_exit(mac_impl_t *);
extern int i_mac_perim_enter_nowait(mac_impl_t *);
extern int mac_hold(const char *, mac_impl_t **);
extern void mac_rele(mac_impl_t *);
extern int i_mac_disable(mac_impl_t *);
extern void i_mac_notify(mac_impl_t *, mac_notify_type_t);
extern void i_mac_notify_exit(mac_impl_t *);
extern void mac_tx_invoke_callbacks(mac_client_impl_t *, mac_tx_cookie_t);
extern int i_mac_promisc_set(mac_impl_t *, boolean_t);
extern void i_mac_promisc_walker_cleanup(mac_impl_t *);
extern mactype_t *mactype_getplugin(const char *);
extern void mac_addr_factory_init(mac_impl_t *);
extern void mac_addr_factory_fini(mac_impl_t *);
extern void mac_register_priv_prop(mac_impl_t *, char **);
extern void mac_unregister_priv_prop(mac_impl_t *);
extern int mac_init_rings(mac_impl_t *, mac_capab_rings_t *);
extern void mac_free_rings(mac_impl_t *, mac_ring_type_t);
extern void mac_init_fake_tx_ring(mac_impl_t *);
extern void mac_fini_fake_tx_ring(mac_impl_t *);
extern void mac_compare_ddi_handle(mac_group_t *, mac_ring_t *);

extern void mac_buffermgmt_init();
extern void mac_buffermgmt_fini();
extern boolean_t i_mac_capab_bm_get(mac_impl_t *mip,
    mac_buffer_mgmt_type_t type);
extern int mac_bm_create_pools(mac_impl_t *);
extern void mac_bm_offline_pools(mac_impl_t *);
extern void mac_bm_destroy_pools(mac_impl_t *);
extern void mac_bm_offline_pools_check(mac_impl_t *);
extern void mac_bm_reset_values(mac_impl_t *);

extern int mac_start_group(mac_group_t *);
extern void mac_stop_group(mac_group_t *);
extern int mac_start_group_and_rings(mac_group_t *);
extern void mac_stop_group_and_rings(mac_group_t *);
extern int mac_start_ring(mac_ring_t *);
extern void mac_stop_ring(mac_ring_t *);
extern boolean_t mac_sriov_ready(mac_impl_t *);
extern mac_group_t *mac_search_group(mac_impl_t *, uint16_t, uint32_t);
extern int mac_bind_group(mac_group_t *grps, mac_client_handle_t);
extern int mac_add_macaddr(mac_impl_t *, mac_group_t *, uint8_t *,
    boolean_t, boolean_t);
extern int mac_remove_macaddr(mac_impl_t *, uint8_t *);
extern int mac_add_macvlan(mac_impl_t *, mac_group_t *, uint16_t, uint16_t);
extern int mac_remove_macvlan(mac_impl_t *, mac_group_t *, uint16_t);

extern void mac_set_group_state(mac_group_t *, mac_group_state_t);
extern void mac_group_add_client(mac_group_t *, mac_client_impl_t *);
extern void mac_group_remove_client(mac_group_t *, mac_client_impl_t *);
extern mac_group_t *mac_group_find(mac_impl_t *mip, mac_ring_type_t type,
    int grp_index);

extern int i_mac_group_add_ring(mac_group_t *, mac_ring_t *, int);
extern void i_mac_group_rem_ring(mac_group_t *, mac_ring_t *, boolean_t);
extern int mac_group_ring_modify(mac_client_impl_t *, mac_group_t *,
    mac_group_t *);

extern mac_group_state_t mac_group_next_state(mac_group_t *,
    mac_client_impl_t **, mac_group_t *, boolean_t);

extern mblk_t *mac_protect_check(mac_client_handle_t, mblk_t *);
extern int mac_protect_set(mac_client_handle_t, mac_resource_props_t *);
extern boolean_t mac_protect_enabled(mac_client_handle_t, uint32_t);
extern int mac_protect_validate(mac_resource_props_t *);
extern void mac_protect_update(mac_resource_props_t *, mac_resource_props_t *);
extern void mac_protect_update_v6_local_addr(mac_client_impl_t *);
extern void mac_protect_intercept_dhcp(mac_client_impl_t *, mblk_t *);
extern void mac_protect_flush_dhcp(mac_client_impl_t *);
extern void mac_protect_cancel_timer(mac_client_impl_t *);
extern void mac_protect_init(mac_client_impl_t *);
extern void mac_protect_fini(mac_client_impl_t *);

extern int mac_set_resources(mac_handle_t, mac_resource_props_t *);
extern void mac_get_resources(mac_handle_t, mac_resource_props_t *);
extern void mac_get_effective_resources(mac_handle_t, mac_resource_props_t *);

extern cpupart_t *mac_pset_find(mac_resource_props_t *, boolean_t *);
extern void mac_set_pool_effective(boolean_t, cpupart_t *,
    mac_resource_props_t *, mac_resource_props_t *);
extern void mac_set_rings_effective(mac_client_impl_t *);
extern mac_client_impl_t *mac_check_primary_relocation(mac_client_impl_t *,
    boolean_t);

extern boolean_t mac_tx_ring(mac_ring_handle_t, mblk_t *, uint16_t, mblk_t **);
extern void mac_tx_ring_worker(mac_ring_t *);
extern void mac_poll_worker(mac_ring_t *);
extern void mac_rx_fanout_init(mac_client_impl_t *);
extern void mac_rx_fanout_fini(mac_client_impl_t *);
extern void mac_rx_fanout_wait(mac_client_impl_t *);
extern uint32_t mac_default_rx_fanout_cnt(mac_client_impl_t *);
extern void mac_rx_fanout_recompute(mac_impl_t *);
extern boolean_t mac_rx_fanout_enable;
extern uint_t mac_rx_fanout_inline_max;
extern uint_t mac_ib_rx_fanout_inline_max;

extern int i_mac_descriptor_create(mac_handle_t,
    mac_ring_handle_t, uint32_t, uint32_t, uint32_t, size_t);
extern void i_mac_descriptor_destroy(mac_descriptor_t *);
extern int i_mac_packet_pool_create(mac_handle_t,
    mac_ring_handle_t, uint32_t, uint32_t, uint32_t, size_t, off_t);

extern void mac_tx_update_obytes_pkts(mac_client_impl_t *);

/* Global callbacks into the bridging module (when loaded) */
extern mac_bridge_tx_t mac_bridge_tx_cb;
extern mac_bridge_rx_t mac_bridge_rx_cb;
extern mac_bridge_ref_t mac_bridge_ref_cb;
extern mac_bridge_ls_t mac_bridge_ls_cb;

extern int mac_get_linkid2name(datalink_id_t, char *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MAC_IMPL_H */

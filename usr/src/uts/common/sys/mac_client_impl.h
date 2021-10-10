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

#ifndef	_SYS_MAC_CLIENT_IMPL_H
#define	_SYS_MAC_CLIENT_IMPL_H

#include <sys/modhash.h>
#include <sys/mac_client.h>
#include <sys/mac_provider.h>
#include <sys/mac.h>
#include <sys/mac_impl.h>
#include <sys/mac_cpu.h>
#include <sys/mac_stat.h>
#include <net/if.h>
#include <sys/mac_flow_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	MAC_BWCTL_LINK	0x00000001
#define	MAC_BWCTL_FLOW	0x00000002
#define	MAC_PROTECT	0x00000004

extern kmem_cache_t	*mac_client_impl_cache;
extern kmem_cache_t	*mac_unicast_impl_cache;
extern kmem_cache_t	*mac_promisc_impl_cache;

/*
 * Need a list to chain all VIDs assigned to a client. Normally, one
 * MAC client only has one VID. But vsw might need multiple VIDs.
 */
typedef struct mac_unicast_impl_s {			/* Protected by */
	struct mac_unicast_impl_s	*mui_next;	/* SL */
	mac_address_t			*mui_map;	/* SL */
	uint16_t			mui_vid;	/* SL */
} mac_unicast_impl_t;

#define	MAC_CLIENT_FLAGS_PRIMARY		0X0001
#define	MAC_CLIENT_FLAGS_VNIC_PRIMARY		0x0002
#define	MAC_CLIENT_FLAGS_MULTI_PRIMARY		0x0004
#define	MAC_CLIENT_FLAGS_PASSIVE_PRIMARY	0x0008

/*
 * One of these is instantiated per MAC client promiscuous callback.
 *
 * Each element of this structure belongs to two linked list. One
 * for the mac_client_impl_t (mci_promisc_list) which created allocated
 * the callback, the other for the mac_impl_t (mi_promisc_list) corresponding
 * to the MAC client.
 * The former allows us to do bookkeeping, the latter allows us
 * to more efficiently dispatch packets to the promiscuous callbacks.
 */
typedef struct mac_promisc_impl_s {			/* Protected by */
	mac_cb_t			mpi_mci_link;	/* mi_promisc_lock */
	mac_cb_t			mpi_mi_link;	/* mi_promisc_lock */
	mac_client_promisc_type_t	mpi_type;	/* WO */
	mac_rx_t			mpi_fn;		/* WO */
	void				*mpi_arg;	/* WO */
	struct mac_client_impl_s	*mpi_mcip;	/* WO */
	boolean_t			mpi_no_tx_loop;	/* WO */
	boolean_t			mpi_no_phys;	/* WO */
	boolean_t			mpi_strip_vlan_tag;	/* WO */
	boolean_t			mpi_no_copy;	/* WO */
} mac_promisc_impl_t;

typedef union mac_tx_percpu_s {
	struct {
		kmutex_t	_pcpu_tx_lock;
		uint_t		_pcpu_tx_refcnt;
		uint64_t	_pcpu_tx_obytes;
		uint64_t	_pcpu_tx_opackets;
	} pcpu_lr;
	uchar_t		pcpu_pad[64];
} mac_tx_percpu_t;

#define	pcpu_tx_lock		pcpu_lr._pcpu_tx_lock
#define	pcpu_tx_refcnt		pcpu_lr._pcpu_tx_refcnt
#define	pcpu_tx_obytes		pcpu_lr._pcpu_tx_obytes
#define	pcpu_tx_opackets	pcpu_lr._pcpu_tx_opackets

typedef	struct mac_queue_s {
	mblk_t			*mq_head;
	mblk_t			**mq_tailp;
	uint_t			mq_cnt;
} mac_queue_t;

typedef struct mac_rx_fanout_s {
	kmutex_t		rf_lock;
	kcondvar_t		rf_cv;
	kcondvar_t		rf_wait_cv;
	uint_t			rf_state;
	mac_client_impl_t	*rf_mcip;
	mac_queue_t		rf_ipq;
	mac_queue_t		rf_fullq;
	uint_t			rf_total_cnt;
	mac_ip_sqinfo_t		rf_sqinfo;
	uint_t			rf_xmit_hint;
	kthread_t		*rf_worker;
	mac_ring_t		*rf_ring;
	numaio_object_t		*rf_worker_obj;
	numaio_object_t		*rf_squeue_obj;
	uint_t			rf_ring_gen;
} mac_rx_fanout_t;

/* rf_state */
#define	RF_READY		0x01
#define	RF_BUSY			0x02
#define	RF_QUIESCE		0x04

typedef struct mac_tunables_s {
	uint_t	mt_rx_fanout_inline_max;
} mac_tunables_t;

/*
 * One of these is instantiated for each MAC client.
 */
struct mac_client_impl_s {				/* Protected by */
	struct mac_client_impl_s *mci_client_next;	/* mi_rw_lock */
	char			mci_name[MAXNAMELEN];	/* mi_rw_lock */

	/*
	 * This flow entry will contain all the internal constructs
	 * for this MAC client. The MAC client may have more than one
	 * flow corresponding to each upper client sharing this
	 * mac_client_impl_t.
	 */
	flow_entry_t		*mci_flent;		/* mi_rw_lock */
	struct mac_impl_s	*mci_mip;		/* WO */
	/*
	 * If this is a client that has a pass thru MAC (e.g. a VNIC),
	 * then we also keep the handle for the client's upper MAC.
	 */
	struct mac_impl_s	*mci_upper_mip;		/* WO */

	uint32_t		mci_state_flags;	/* WO */
	mac_rx_t		mci_rx_fn;		/* Rx Quiescence */
	void			*mci_rx_arg;		/* Rx Quiescence */
	mac_direct_rx_t		mci_direct_rx_fn;	/* SL */
	void			*mci_direct_rx_arg;	/* SL */
	mac_rx_t		mci_rx_p_fn;		/* Rx Quiescence */
	void			*mci_rx_p_arg;		/* Rx Quiescence */
	void			*mci_p_unicast_list;

	mac_cb_t		*mci_promisc_list;	/* mi_promisc_lock */

	mac_address_t		*mci_unicast;
	uint32_t		mci_flags;		/* SL */
	krwlock_t		mci_rw_lock;
	mac_unicast_impl_t	*mci_unicast_list;	/* mci_rw_lock */
	/*
	 * The mac_client_impl_t may be shared by multiple clients, i.e
	 * multiple VLANs sharing the same MAC client. In this case the
	 * address/vid tuples differ and are each associated with their
	 * own flow entry, but the rest underlying components are common.
	 */
	flow_entry_t		*mci_flent_list;	/* mci_rw_lock */
	uint_t			mci_nflents;		/* mci_rw_lock */
	uint_t			mci_nvids;		/* mci_rw_lock */

	/* Resource Management Functions */
	mac_resource_add_t	mci_resource_add;	/* SL */
	mac_resource_remove_t	mci_resource_remove;	/* SL */
	void			*mci_resource_arg;	/* SL */


	/* Tx notify callback */
	kmutex_t		mci_tx_cb_lock;
	mac_cb_info_t		mci_tx_notify_cb_info;	/* cb list info */
	mac_cb_t		*mci_tx_notify_cb_list;	/* The cb list */
	uintptr_t		mci_tx_notify_id;

	/* per MAC client stats */			/* None */

	mac_client_stats_t	mci_stat;
	kstat_t			*mci_ksp;

	uint32_t		mci_feature;

	flow_tab_t		*mci_subflow_tab;	/* Rx quiescence */

	/*
	 * Priority range for this MAC client. This the range
	 * corresponding to the priority configured (nr_flow_priority).
	 */
	pri_t			mci_min_pri;
	pri_t			mci_max_pri;

	/*
	 * Hybrid I/O related definitions.
	 */
	mac_share_handle_t	mci_share;

	/*
	 * If this client is tied to a VF record it's index.
	 */
	uint32_t		mci_vf_index;		/* mi_rw_lock */

	/*
	 * MAC groups that are associated with this mac client.
	 */
	struct mac_group_s	*mci_tx_groups;		/* mi_rw_lock */
	struct mac_group_s	*mci_rx_groups;		/* mi_rw_lock */

	/*
	 * for multicast support
	 */
	struct mac_mcast_addrs_s *mci_mcast_addrs;	/* mi_rw_lock */

	/*
	 * Mac protection related fields
	 */
	kmutex_t		mci_protect_lock;
	uint32_t		mci_protect_flags;	/* SL */
	in6_addr_t		mci_v6_local_addr;	/* SL */
	avl_tree_t		mci_v4_pending_txn;	/* mci_protect_lock */
	avl_tree_t		mci_v4_completed_txn;	/* mci_protect_lock */
	avl_tree_t		mci_v4_dyn_ip;		/* mci_protect_lock */
	avl_tree_t		mci_v6_pending_txn;	/* mci_protect_lock */
	avl_tree_t		mci_v6_cid;		/* mci_protect_lock */
	avl_tree_t		mci_v6_dyn_ip;		/* mci_protect_lock */
	timeout_id_t		mci_txn_cleanup_tid;	/* mci_protect_lock */

	mac_select_ring_fn_t	mci_select_ring;
	void			*mci_select_ring_arg;

	mac_rx_fanout_t		*mci_rx_fanout;
	uint_t			mci_rx_fanout_cnt;
	uint_t			mci_rx_fanout_cnt_per_ring;

	kmutex_t		mci_bwctl_lock;
	kcondvar_t		mci_bwctl_cv;
	flow_entry_t		*mci_bwctl_list;
	uint_t			mci_bwctl_state;
	kthread_t		*mci_bwctl_worker;

	/* NUMA IO */
	mac_numa_group_t	*mci_numa_grp;
	int			mci_numa_grp_cnt; /* grp count > 1 for aggr */
	numaio_constraint_t	*mci_numa_constraint;

	/*
	 * Protected by mci_tx_pcpu[0].pcpu_tx_lock
	 */
	uint_t			mci_tx_flag;
	kcondvar_t		mci_tx_cv;

	/* 802.1p usr priority */
	uint8_t			mci_usrpri;
	mac_tunables_t		mci_tunables;
	mac_tx_percpu_t		*mci_tx_pcpu;		/* SL */
};

extern	int	mac_tx_percpu_cnt;

/* Defensive coding, non-null mcip_flent could be an assert */

#define	MCIP_DATAPATH_SETUP(mcip)		\
	((mcip)->mci_flent == NULL ? B_FALSE :	\
	!((mcip)->mci_flent->fe_flags & FE_MC_NO_DATAPATH))

#define	MCIP_RESOURCE_PROPS(mcip)		\
	((mcip)->mci_flent == NULL ? NULL :	\
	&(mcip)->mci_flent->fe_resource_props)

#define	MCIP_EFFECTIVE_PROPS(mcip)		\
	(mcip->mci_flent == NULL ? NULL : 	\
	&(mcip)->mci_flent->fe_effective_props)

#define	MCIP_RESOURCE_PROPS_MASK(mcip)		\
	((mcip)->mci_flent == NULL ? 0 :	\
	(mcip)->mci_flent->fe_resource_props.mrp_mask)

#define	MCIP_RESOURCE_PROPS_MAXBW(mcip)		\
	((mcip)->mci_flent == NULL ? 0 :	\
	(mcip)->mci_flent->fe_resource_props.mrp_maxbw)

#define	MCIP_RESOURCE_PROPS_PRIORITY(mcip)		\
	((mcip)->mci_flent == NULL ? 0 :	\
	(mcip)->mci_flent->fe_resource_props.mrp_priority)

#define	MCIP_RESOURCE_PROPS_CPUS(mcip)		\
	((mcip)->mci_flent == NULL ? 0 :	\
	&(mcip)->mci_flent->fe_resource_props.mrp_cpus)

#define	MCIP_RESOURCE_PROPS_NCPUS(mcip)		\
	((mcip)->mci_flent == NULL ? 0 :	\
	(mcip)->mci_flent->fe_resource_props.mrp_ncpus)

#define	MCIP_RESOURCE_PROPS_CPU(mcip)		\
	((mcip)->mci_flent == NULL ? 0 :	\
	(mcip)->mci_flent->fe_resource_props.mrp_ncpu)

#define	MCIP_RESOURCE_PROPS_USRPRI(mcip)		\
	((mcip)->mci_flent == NULL ? 0 :	\
	(mcip)->mci_flent->fe_resource_props.mrp_usrpri)

/*
 * We validate the VLAN id of the packet w.r.t the client's vid,
 * if required (i.e. !MCIS_DISABLE_TX_VID_CHECK). DLS clients
 * will have MCIS_DISABLE_TX_VID_CHECK set.
 * (In the case of aggr when we get back packets, due to
 * the underlying driver being flow controlled, we won't
 * drop the packet even if it is VLAN tagged as we
 * don't set MCIS_DISABLE_TX_VID_CHECK for an aggr.)
 */
#define	MAC_VID_CHECK_NEEDED(mcip)					\
	(((mcip)->mci_state_flags & MCIS_DISABLE_TX_VID_CHECK) == 0 &&	\
	(mcip)->mci_mip->mi_info.mi_nativemedia == DL_ETHER)

#define	MAC_VID_CHECK(mcip, mp, err) {					\
	if (ntohs(((struct ether_header *)(mp)->b_rptr)->ether_type) ==	\
	    ETHERTYPE_VLAN) {						\
		/*							\
		 * err is set to EINVAL (so the caller can take the	\
		 * appropriate action. e.g. freemsg()) for two cases:	\
		 * -client is not responsible for filling in the vid.	\
		 * -client is responsible for filling in the vid, but	\
		 *  the vid doesn't match the vid of the MAC client.	\
		 */							\
		(err) = EINVAL;						\
		if (((mcip)->mci_state_flags & MCIS_TAG_DISABLE) != 0) {\
			struct ether_vlan_header	*evhp;		\
			uint16_t			vlanid;		\
									\
			evhp = (struct ether_vlan_header *)(mp)->b_rptr;\
			vlanid = VLAN_ID(ntohs(evhp->ether_tci));	\
			if (mac_client_check_flow_vid((mcip), vlanid))	\
				(err) = 0;				\
		}							\
	}								\
}

#define	MAC_TAG_NEEDED(mcip)						\
	(((mcip)->mci_state_flags & MCIS_TAG_DISABLE) == 0 &&		\
	((mcip)->mci_nvids == 1 || (mcip)->mci_mip->mi_dcb_flags != 0))	\

/* MCI state flags */
#define	MCIS_IS_VNIC			0x00000001
#define	MCIS_EXCLUSIVE			0x00000002
#define	MCIS_TAG_DISABLE		0x00000004
#define	MCIS_STRIP_DISABLE		0x00000008
#define	MCIS_IS_AGGR_PORT		0x00000010
#define	MCIS_CLIENT_POLL_CAPABLE	0x00000020
#define	MCIS_DESC_LOGGED		0x00000040
#define	MCIS_SHARE_BOUND		0x00000080
#define	MCIS_DISABLE_TX_VID_CHECK	0x00000100
#define	MCIS_USE_DATALINK_NAME		0x00000200
#define	MCIS_UNICAST_HW			0x00000400
#define	MCIS_IS_AGGR			0x00000800
#define	MCIS_RX_BYPASS_DISABLE		0x00001000
#define	MCIS_RX_BYPASS_OK		0x00002000
#define	MCIS_NO_UNICAST_ADDR		0x00004000
#define	MCIS_IS_VF			0x00008000
#define	MCIS_TX_BLOCK			0x00010000

#define	MCI_EXCLUSIVE(mcip) \
	((mcip->mci_state_flags & MCIS_EXCLUSIVE) == MCIS_EXCLUSIVE)
#define	MCI_IS_VF(mcip) \
	((mcip->mci_state_flags & MCIS_IS_VF) == MCIS_IS_VF)

/* Mac protection flags */
#define	MPT_FLAG_V6_LOCAL_ADDR_SET	0x0001

/* in mac_client.c */
extern void mac_promisc_client_dispatch(mac_client_impl_t *, mblk_t *);
extern void mac_client_init(void);
extern void mac_client_fini(void);
extern void mac_promisc_dispatch(mac_impl_t *, mblk_t *,
    mac_client_impl_t *);

extern void mac_rx_resource_add(mac_client_impl_t *, mac_rx_fanout_t *);
extern void mac_rx_resource_remove(mac_client_impl_t *, mac_rx_fanout_t *);

extern int mac_validate_props(mac_impl_t *, mac_resource_props_t *);

extern mac_client_impl_t *mac_vnic_lower(mac_impl_t *);
extern mac_client_impl_t *mac_primary_client_handle(mac_impl_t *);
extern uint16_t i_mac_flow_vid(flow_entry_t *);
extern boolean_t i_mac_capab_get(mac_handle_t, mac_capab_t, void *);

extern void mac_unicast_update_clients(mac_impl_t *, mac_address_t *);
extern void mac_update_resources(mac_resource_props_t *,
    mac_resource_props_t *, boolean_t);

boolean_t mac_client_check_flow_vid(mac_client_impl_t *, uint16_t);

extern boolean_t mac_is_primary_client(mac_client_impl_t *);

extern int mac_client_set_rings_prop(mac_client_impl_t *,
    mac_resource_props_t *, mac_resource_props_t *);
extern void mac_set_prim_vlan_rings(mac_impl_t *, mac_resource_props_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MAC_CLIENT_IMPL_H */

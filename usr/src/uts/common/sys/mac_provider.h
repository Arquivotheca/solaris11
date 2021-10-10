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
#ifndef	_SYS_MAC_PROVIDER_H
#define	_SYS_MAC_PROVIDER_H

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/mkdev.h>
#include <sys/mac.h>
#include <sys/mac_flow.h>

/*
 * MAC Provider Interface
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * MAC version identifiers. Drivers compiled against the stable V1 version
 * of the API should register with MAC_VERSION_V1. ON drivers should use
 * MAC_VERSION. This is used by mac_alloc() mac_register() to
 * verify that incompatible drivers don't register.
 */
#define	MAC_VERSION_V1	0x1
#define	MAC_VERSION	MAC_VERSION_V1

/*
 * Possible values for ETHER_STAT_XCVR_INUSE statistic.
 */

#define	XCVR_UNDEFINED		0
#define	XCVR_NONE		1
#define	XCVR_10			2
#define	XCVR_100T4		3
#define	XCVR_100X		4
#define	XCVR_100T2		5
#define	XCVR_1000X		6
#define	XCVR_1000T		7

#ifdef	_KERNEL

/*
 * Definitions for MAC Drivers Capabilities
 */
/*
 * MAC layer capabilities.  These capabilities are handled by the drivers'
 * mc_capab_get() callbacks.  Some capabilities require the driver to fill
 * in a given data structure, and others are simply boolean capabilities.
 * Note that capability values must be powers of 2 so that consumers and
 * providers of this interface can keep track of which capabilities they
 * care about by keeping a bitfield of these things around somewhere.
 */
typedef enum {
	/*
	 * Public Capabilities (MAC_VERSION_V1)
	 */
	MAC_CAPAB_HCKSUM	= 0x00000001, /* data is a uint32_t */
	MAC_CAPAB_LSO		= 0x00000008, /* data is mac_capab_lso_t */
	MAC_CAPAB_RINGS		= 0x00000010, /* data is mac_capab_rings_t */

	/*
	 * Reserved capabilities, do not use
	 */
	MAC_CAPAB_RESERVED1	= 0x00000002,
	MAC_CAPAB_RESERVED2	= 0x00000004,
	MAC_CAPAB_RESERVED3	= 0x00001000, /* Priv IOV caapabiltiy in S10 */


	/*
	 * Private driver capabilities
	 */
	MAC_CAPAB_SHARES	= 0x00000020, /* data is mac_capab_share_t */
	MAC_CAPAB_MULTIFACTADDR = 0x00000040, /* mac_data_multifactaddr_t */
	MAC_CAPAB_BUFFER_MGMT	= 0x00000080, /* mac_capab_bm_t */
	MAC_CAPAB_FCOE		= 0x00000100, /* data is mac_capab_fcoe_t */
	MAC_CAPAB_LINKID	= 0x00000200, /* mac_capab_linkid_t */

	/*
	 * Private driver capabilities for use by the GLDv3 framework only
	 */
	MAC_CAPAB_VNIC		= 0x00010000, /* data is mac_capab_vnic_t */
	MAC_CAPAB_ANCHOR_VNIC	= 0x00020000, /* boolean only, no data */
	MAC_CAPAB_AGGR		= 0x00040000, /* data is mac_capab_aggr_t */
	MAC_CAPAB_NO_NATIVEVLAN	= 0x00080000, /* boolean only, no data */
	MAC_CAPAB_NO_ZCOPY	= 0x00100000, /* boolean only, no data */
	MAC_CAPAB_LEGACY	= 0x00200000, /* data is mac_capab_legacy_t */
	MAC_CAPAB_VRRP		= 0x00400000, /* data is mac_capab_vrrp_t */
	MAC_CAPAB_DCB		= 0x00800000 /* supports DCB */
} mac_capab_t;

/*
 * Buffer Management Capability.
 */
typedef enum _mac_bm_type {
	MAC_BUFFER_MGMT_TX = 1,
	MAC_BUFFER_MGMT_RX
} mac_buffer_mgmt_type_t;

typedef struct mac_capab_bm_s {
	mac_buffer_mgmt_type_t	mbm_type;		/* BM type: Rx vs Tx */
	ddi_dma_attr_t		*mbm_packet_attrp;	/* DDI DMA Attr's */
	ddi_device_acc_attr_t	*mbm_packet_accattrp;	/* DDI Access Attr's */
	ddi_dma_attr_t		*mbm_descriptor_attrp;
	ddi_device_acc_attr_t	*mbm_descriptor_accattrp;
	uint32_t		mbm_min_descriptors;	/* MIN # of descrip */
	uint32_t		mbm_max_descriptors;	/* MAX # of descrip */
	uint32_t		mbm_desired_descriptors;
	uint32_t		mbm_desired_buffers;
	size_t			mbm_descriptor_size;	/* Size of descriptor */
	size_t			mbm_buffer_size;	/* Packet Buffer Size */
	size_t			mbm_contig_mem_size;	/* Eg. NIU buffer */
	size_t			mbm_block_size;
	off_t			mbm_offset;
	uint64_t		mbm_flags;
} mac_capab_bm_t;

/*
 * MAC Buffer Management flags.
 */
#define	MAC_BM_PACKET_CONTIGUOUS	0x0001	/* Need Contiguous Memory */
#define	MAC_BM_DESCRIPTOR_CONTIGUOUS	0x0002	/* Need Contiguous Memory */
#define	MAC_BM_GROUP_SHARED		0x0100	/* Share Buffer: Group */
#define	MAC_BM_SHARED			0x0200	/* Share Buffers: Interface */
#define	MAC_BM_DRIVER_MANAGES_BUFFERS	0x1000	/* Driver Manages */
#define	MAC_BM_PREMAP_OK		0x2000	/* Driver understands premap */

/*
 * FCoE Offload Capability
 */
typedef struct mac_capab_fcoe_s {
	uint16_t	mac_capab_fcoe_flags;
	uint32_t	mac_fcoe_max_lso_size;
	uint32_t	mac_fcoe_max_lro_size;
	uint16_t	mac_fcoe_min_lro_xchgid;
	uint16_t	mac_fcoe_max_lro_xchgid;
	int		(*mac_fcoe_setup_lro)(void *, mac_fcoe_lro_params_t *);
	void		(*mac_fcoe_cancel_lro)(void *, mac_fcoe_lro_params_t *);
} mac_capab_fcoe_t;

/*
 * LSO capability
 */
typedef struct lso_basic_tcp_ipv4_s {
	t_uscalar_t	lso_max;		/* maximum payload */
} lso_basic_tcp_ipv4_t;

/*
 * Currently supported flags for LSO.
 */
#define	LSO_TX_BASIC_TCP_IPV4	0x01		/* TCP LSO capability */

/*
 * Future LSO capabilities can be added at the end of the mac_capab_lso_t.
 * When such capability is added to the GLDv3 framework, the size of the
 * mac_capab_lso_t it allocates and passes to the drivers increases. Older
 * drivers wil access only the (upper) sections of that structure, that is the
 * sections carrying the capabilities they understand. This ensures the
 * interface can be safely extended in a binary compatible way.
 */
typedef	struct mac_capab_lso_s {
	t_uscalar_t		lso_flags;
	lso_basic_tcp_ipv4_t	lso_basic_tcp_ipv4;
	/* Add future lso capabilities here */
} mac_capab_lso_t;

/*
 * Multiple Factory MAC Addresses Capability
 */
typedef struct mac_capab_multifactaddr_s {
	/*
	 * Number of factory addresses
	 */
	uint_t		mcm_naddr;

	/*
	 * Callbacks to query all the factory addresses.
	 */
	void		(*mcm_getaddr)(void *, uint_t, uint8_t *);
} mac_capab_multifactaddr_t;

/*
 * Info and callbacks of legacy devices.
 */
typedef struct mac_capab_legacy_s {
	/*
	 * Notifications that the legacy device does not support.
	 */
	uint32_t	ml_unsup_note;
	/*
	 * dev_t of the legacy device; can be held to force attach.
	 */
	dev_t		ml_dev;
	boolean_t	(*ml_active_set)(void *);
	void		(*ml_active_clear)(void *);
	int		(*ml_fastpath_disable)(void *);
	void		(*ml_fastpath_enable)(void *);
} mac_capab_legacy_t;

/*
 * Capability pseudo drivers like vnic, aggr and ibp partition
 * to provide the linkid. If capable, the linkid provided will
 * be used to derive mi_linkname.
 */
typedef struct mac_capab_linkid_s {
	datalink_id_t	mpl_linkid;
} mac_capab_linkid_t;

typedef struct __mac_prop_info_handle *mac_prop_info_handle_t;

/*
 * MAC driver entry point types.
 */
typedef int		(*mac_getstat_t)(void *, uint_t, uint64_t *);
typedef	int		(*mac_start_t)(void *);
typedef void		(*mac_stop_t)(void *);
typedef int		(*mac_setpromisc_t)(void *, boolean_t);
typedef int		(*mac_multicst_t)(void *, boolean_t, const uint8_t *);
typedef int		(*mac_unicst_t)(void *, const uint8_t *);
typedef void		(*mac_ioctl_t)(void *, queue_t *, mblk_t *);
typedef void		(*mac_resources_t)(void *);
typedef mblk_t		*(*mac_tx_t)(void *, mblk_t *);
typedef	boolean_t	(*mac_getcapab_t)(void *, mac_capab_t, void *);
typedef	int		(*mac_open_t)(void *);
typedef void		(*mac_close_t)(void *);
typedef	int		(*mac_set_prop_t)(void *, const char *, mac_prop_id_t,
			    uint_t, const void *);
typedef	int		(*mac_get_prop_t)(void *, const char *, mac_prop_id_t,
			    uint_t, void *);
typedef void		(*mac_prop_info_t)(void *, const char *, mac_prop_id_t,
			    mac_prop_info_handle_t);

/*
 * Driver callbacks. The following capabilities are optional, and if
 * implemented by the driver, must have a corresponding MC_ flag set
 * in the mc_callbacks field.
 *
 * Any future additions to this list must also be accompanied by an
 * associated mc_callbacks flag so that the framework can grow without
 * affecting the binary compatibility of the interface.
 */
typedef struct mac_callbacks_s {
	uint_t		mc_callbacks;	/* Denotes which callbacks are set */
	mac_getstat_t	mc_getstat;	/* Get the value of a statistic */
	mac_start_t	mc_start;	/* Start the device */
	mac_stop_t	mc_stop;	/* Stop the device */
	mac_setpromisc_t mc_setpromisc;	/* Enable or disable promiscuous mode */
	mac_multicst_t	mc_multicst;	/* Enable or disable a multicast addr */
	mac_unicst_t	mc_unicst;	/* Set the unicast MAC address */
	mac_tx_t	mc_tx;		/* Transmit a packet */
	void		*mc_reserved;	/* Reserved, do not use */
	mac_ioctl_t	mc_ioctl;	/* Process an unknown ioctl */
	mac_getcapab_t	mc_getcapab;	/* Get capability information */
	mac_open_t	mc_open;	/* Open the device */
	mac_close_t	mc_close;	/* Close the device */
	mac_set_prop_t	mc_setprop;
	mac_get_prop_t	mc_getprop;
	mac_prop_info_t	mc_propinfo;
} mac_callbacks_t;

/*
 * Flags for mc_callbacks.  Requiring drivers to set the flags associated
 * with optional callbacks initialized in the structure allows the mac
 * module to add optional callbacks in the future without requiring drivers
 * to recompile.
 */
#define	MC_RESERVED	0x0001
#define	MC_IOCTL	0x0002
#define	MC_GETCAPAB	0x0004
#define	MC_OPEN		0x0008
#define	MC_CLOSE	0x0010
#define	MC_SETPROP	0x0020
#define	MC_GETPROP	0x0040
#define	MC_PROPINFO	0x0080
#define	MC_PROPERTIES	(MC_SETPROP | MC_GETPROP | MC_PROPINFO)

/*
 * Virtualization Capabilities
 */
/*
 * The ordering of entries below is important. MAC_HW_CLASSIFIER
 * is the cutoff below which are entries which don't depend on
 * H/W. MAC_HW_CLASSIFIER and entries after that are cases where
 * H/W has been updated through add/modify/delete APIs.
 */
typedef enum {
	MAC_NO_CLASSIFIER = 0,
	MAC_SW_CLASSIFIER,
	MAC_HW_CLASSIFIER
} mac_classify_type_t;

typedef	void	(*mac_rx_func_t)(void *, mac_resource_handle_t, mblk_t *,
    boolean_t);

/*
 * DCB flags indicate if the NIC H/W supports DCB and if so the
 * features that are supported.
 */
#define	MAC_DCB_NONE		0x0
#define	MAC_DCB_PFC		0x1


typedef enum {
	MAC_RING_TYPE_RX = 1,	/* Receive ring */
	MAC_RING_TYPE_TX	/* Transmit ring */
} mac_ring_type_t;

/*
 * Grouping type of a ring group
 *
 * MAC_GROUP_TYPE_STATIC: The ring group can not be re-grouped.
 * MAC_GROUP_TYPE_DYNAMIC: The ring group support dynamic re-grouping
 */
typedef enum {
	MAC_GROUP_TYPE_STATIC = 0x001,	/* Static ring group */
	MAC_GROUP_TYPE_DYNAMIC = 0x002	/* Dynamic ring group */
} mac_group_type_t;

/*
 * Flags for the MAC RINGS capability.
 *
 * MAC_RINGS_FLAG_VLAN_TRANSPARENT -- the NIC hardware will explicitly
 *	add the VLAN to outbound packets and strip this VLAN from
 *	inbound packets.
 */
typedef enum {
	MAC_RINGS_FLAGS_NONE = 0x000,		/* No flags */
	MAC_RINGS_VLAN_TRANSPARENT = 0x001	/* Transparent VLANs possible */
} mac_rings_flags_t;

typedef	struct mac_ring_info_s mac_ring_info_t;
typedef	struct mac_group_info_s mac_group_info_t;

typedef void	(*mac_get_ring_t)(void *, mac_ring_type_t, const int, const int,
    mac_ring_info_t *, mac_ring_handle_t);
typedef void	(*mac_get_group_t)(void *, mac_ring_type_t, const int,
    mac_group_info_t *, mac_group_handle_t);

typedef void	(*mac_group_add_ring_t)(mac_group_driver_t,
    mac_ring_driver_t, mac_ring_type_t);
typedef void	(*mac_group_rem_ring_t)(mac_group_driver_t,
    mac_ring_driver_t, mac_ring_type_t);

typedef int	(*mac_group_get_ring_tc_t)(mac_ring_driver_t, mac_ring_type_t);

/*
 * Multiple Rings Capability
 */
typedef enum mac_rings_version_s {
	MAC_RINGS_VERSION_1 = 0x100
} mac_rings_version_t;

typedef struct	mac_capab_rings_s {
	mac_rings_version_t	mr_version;	/* Version */
	mac_ring_type_t		mr_type;	/* Ring type: Rx vs Tx */
	mac_group_type_t	mr_group_type;	/* Dynamic vs static grouping */
	mac_rings_flags_t	mr_flags;	/* Rings flags */
	uint_t			mr_rnum;	/* Number of rings */
	uint_t			mr_gnum;	/* Number of ring groups */
	mac_get_ring_t		mr_rget;	/* Get ring from driver */
	mac_get_group_t		mr_gget;	/* Get ring group from driver */
	mac_group_add_ring_t	mr_gaddring;	/* Add ring into a group */
	mac_group_rem_ring_t	mr_gremring;	/* Remove ring from a group */
	mac_group_get_ring_tc_t	mr_ggetringtc;	/* Get traffic class info */
} mac_capab_rings_t;

/*
 * Common ring functions and driver interfaces
 */
typedef	int	(*mac_ring_start_t)(mac_ring_driver_t, uint64_t);
typedef	void	(*mac_ring_stop_t)(mac_ring_driver_t);

typedef	mblk_t	*(*mac_ring_send_t)(void *, mblk_t *);
typedef	mblk_t	*(*mac_ring_poll_t)(void *, int, int);

typedef int	(*mac_ring_stat_t)(mac_ring_driver_t, uint_t, uint64_t *);

struct mac_ring_info_s {
	mac_ring_driver_t	mri_driver;
	union {
		struct _mri_tx {
			mac_ring_send_t	send;
		} tx;
		struct _mri_rx {
			mac_ring_poll_t	poll;
		} rx;
	} mrfunion;
	mac_intr_t		mri_intr;
	mac_ring_start_t	mri_start;
	mac_ring_stop_t		mri_stop;

	mac_ring_stat_t		mri_stat;

	/*
	 * mri_flags will have some bits set to indicate some special
	 * property/feature of a ring like serialization needed for a
	 * Tx ring or packets should always need enqueuing on Rx side,
	 * etc.
	 */
	uint_t			mri_flags;
};

#define	mri_tx			mrfunion.tx.send
#define	mri_poll		mrfunion.rx.poll
#define	mri_intr_enable		mri_intr.mi_enable
#define	mri_intr_disable	mri_intr.mi_disable
#define	mri_intr_handle		mri_intr.mi_handle
#define	mri_intr_ddi_handle	mri_intr.mi_ddi_handle

/*
 * MAC sriov info structure.
 */
typedef struct	mac_sriov_info_s {
	uint32_t	msi_vf_index;	/* VF index for this group */
} mac_sriov_info_t;

/*
 * #defines for mri_flags. The flags are temporary flags that are provided
 * only to workaround issues in specific drivers, and they will be
 * removed in the future.
 */
#define	MAC_RING_TX_SERIALIZE		0x1
#define	MAC_RING_RX_ENQUEUE		0x2

/*
 * The mgi_flags are flags used by the driver to inidicate some of the
 * capabilities of the group.  These flags are set at the time the group
 * get driver entry point is called. When using MAC_GROUP_POOL, this on
 * the only flag set for the group.  The meaning of this group is a set
 * idle (inactive) rings.
 */
typedef enum {
	/* No flags */
	MAC_GROUP_NONE = 0x000,

	/* Default Group for the NIC */
	MAC_GROUP_DEFAULT = 0x001,

	/* Group is a Pool of non-active rings */
	MAC_GROUP_POOL = 0x002,

	/*
	 * HW tag/strips for VLANs.  Hardware implements tagging/stripping
	 * for all vlans.  Or, it implmenents tagging/stripping for a single
	 * VLAN.
	 */
	MAC_GROUP_VLAN_TRANSPARENT_ALL = 0x004,
	MAC_GROUP_VLAN_TRANSPARENT_ONE = 0x008
} mac_group_flags_t;

/*
 * Flags for mac_group_addvlan/remvlan driver entry points.
 */
#define	MAC_GROUP_VLAN_TRANSPARENT_ENABLE	0x001	/* H/W tag/strip */

/*
 * Flags for the mac_group_addmac/remmac entry points.
 *
 * MAC_GROUP_MACADDR_PRIMARY is used only by a PF driver to indicate
 * the address being added to the group is the primary address for
 * the corresponding VF instance.  This is only used when the VF is
 * assigned more than one mac address.
 */
#define	MAC_GROUP_MACADDR_PRIMARY	0x001	/* Primary Address for Group */
						/* Only used for SRIOV groups */

typedef	int	(*mac_group_start_t)(mac_group_driver_t);
typedef	void	(*mac_group_stop_t)(mac_group_driver_t);
typedef	int	(*mac_group_addmac_t)(void *, const uint8_t *, uint64_t);
typedef	int	(*mac_group_remmac_t)(void *, const uint8_t *);
typedef int	(*mac_group_addvlan_t)(void *, uint16_t, uint32_t);
typedef int	(*mac_group_remvlan_t)(void *, uint16_t);
typedef int	(*mac_group_setmtu_t)(void *, uint32_t);
typedef int	(*mac_group_getsriov_info_t)(void *, mac_sriov_info_t *);

struct mac_group_info_s {
	mac_group_driver_t	mgi_driver;	/* Driver reference */
	mac_group_start_t	mgi_start;	/* Start the group */
	mac_group_stop_t	mgi_stop;	/* Stop the group */
	uint_t			mgi_count;	/* Count of rings */
	mac_group_flags_t	mgi_flags;	/* flags for group. */

	/*
	 * Group Entry points.  Most of these entry points are
	 * generally for RX groups.  However, the actually NIC
	 * hardware may dictate some entry points be populated
	 * for TX groups.
	 */

	/*
	 * Add/remove MAC address for this group.
	 */
	mac_group_addmac_t	mgi_addmac;
	mac_group_remmac_t	mgi_remmac;

	/*
	 * Add/remove VLAN filters for this group.
	 */
	mac_group_addvlan_t	mgi_addvlan;
	mac_group_remvlan_t	mgi_remvlan;

	/*
	 * Set group MTU.
	 */
	mac_group_setmtu_t	mgi_setmtu;

	/*
	 * SR-IOV entry point.
	 */
	mac_group_getsriov_info_t	mgi_getsriov_info;
};

/*
 * Share management functions.
 */
typedef uint64_t mac_share_handle_t;

/*
 * Allocate and free a share. Returns ENOSPC if all shares have been
 * previously allocated.
 */
typedef int (*mac_alloc_share_t)(void *, mac_share_handle_t *);
typedef void (*mac_free_share_t)(mac_share_handle_t);

/*
 * Bind and unbind a share. Binding a share allows a domain
 * to have direct access to the groups and rings associated with
 * that share.
 */
typedef int (*mac_bind_share_t)(mac_share_handle_t, uint64_t, uint64_t *);
typedef void (*mac_unbind_share_t)(mac_share_handle_t);

/*
 * Return information on about a share.
 */
typedef void (*mac_share_query_t)(mac_share_handle_t, mac_ring_type_t,
    mac_ring_handle_t *, uint_t *);

/*
 * Basic idea, bind previously created ring groups to shares
 * for them to be exported (or shared) by another domain.
 * These interfaces bind/unbind the ring group to a share.
 * The groups and their rings will be shared with the guest
 * as soon as the share is bound.
 */
typedef int (*mac_share_add_group_t)(mac_share_handle_t,
    mac_group_driver_t);
typedef int (*mac_share_rem_group_t)(mac_share_handle_t,
    mac_group_driver_t);

typedef struct  mac_capab_share_s {
	uint_t			ms_snum;	/* Number of shares (vr's) */
	void			*ms_handle;	/* Handle to driver. */
	mac_alloc_share_t	ms_salloc;	/* Get a share from driver. */
	mac_free_share_t	ms_sfree;	/* Return a share to driver. */
	mac_share_add_group_t	ms_sadd;	/* Add a group to the share. */
	mac_share_rem_group_t	ms_sremove;	/* Remove group from share. */
	mac_share_query_t	ms_squery;	/* Query share constraints */
	mac_bind_share_t	ms_sbind;	/* Bind a share */
	mac_unbind_share_t	ms_sunbind;	/* Unbind a share */
} mac_capab_share_t;

typedef struct mac_capab_vrrp_s {
	/* IPv6 or IPv4? */
	int		mcv_af;
} mac_capab_vrrp_t;

struct dbsizes_s;

typedef struct mac_capab_dcb_s {
	uint32_t	mcd_flags;	/* Supported DCB features */
	uint8_t		mcd_pfc;	/* PFC map */
	uint_t		mcd_ntc;	/* no. of Traffic Classes */
} mac_capab_dcb_t;

#define	MAC_DCB_NONE		0x0
#define	MAC_DCB_PFC		0x1

#define	MAC_MAX_TRAFFIC_CLASS	8
#define	MAC_MAX_BWG		8

#define	MAC_DCB_TX		0
#define	MAC_DCB_RX		1

/*
 * m_flags for mac_register entry point
 */
typedef enum {
	MAC_FLAGS_NONE = 0x000,			/* No Flags. */
	MAC_FLAGS_PROMISCUOUS_MULTICAST = 0x001	/* Only Multicast Promiscuous */
} mac_register_flags_t;

/*
 * MAC registration interface
 */
typedef struct mac_register_s {
	uint_t			m_version;	/* set by mac_alloc() */
	const char		*m_type_ident;
	void			*m_driver;	/* Driver private data */
	dev_info_t		*m_dip;
	uint_t			m_instance;
	uint8_t			*m_src_addr;
	uint8_t			*m_dst_addr;
	mac_callbacks_t		*m_callbacks;
	uint_t			m_min_sdu;
	uint_t			m_max_sdu;
	void			*m_pdata;
	size_t			m_pdata_size;
	char			**m_priv_props;
	uint32_t		m_margin;
	mac_register_flags_t	m_flags;	/* m_flags */
	uint_t			m_multicast_sdu;
} mac_register_t;

/*
 * Driver interface functions.
 */
extern mac_protect_t		*mac_protect_get(mac_handle_t);
extern void			mac_sdu_get(mac_handle_t, uint_t *, uint_t *);
extern void			mac_sdu_get2(mac_handle_t, uint_t *, uint_t *,
				    uint_t *);
extern int			mac_maxsdu_update(mac_handle_t, uint_t);
extern int			mac_maxsdu_update2(mac_handle_t, uint_t,
				    uint_t);

extern mac_register_t		*mac_alloc(uint_t);
extern void			mac_free(mac_register_t *);
extern int			mac_register(mac_register_t *, mac_handle_t *);
extern int			mac_register_zone(mac_register_t *,
				    mac_handle_t *, zoneid_t);
extern int			mac_disable_nowait(mac_handle_t);
extern int			mac_disable(mac_handle_t);
extern int  			mac_unregister(mac_handle_t);
extern void 			mac_rx(mac_handle_t, mac_resource_handle_t,
				    mblk_t *);
extern void 			mac_rx_ring(mac_handle_t, mac_ring_handle_t,
				    mblk_t *, uint64_t);
extern void 			mac_link_update(mac_handle_t, link_state_t);
extern void 			mac_link_redo(mac_handle_t, link_state_t);
extern void 			mac_unicst_update(mac_handle_t,
				    const uint8_t *);
extern void			mac_dst_update(mac_handle_t, const uint8_t *);
extern void			mac_tx_update(mac_handle_t);
extern void			mac_tx_ring_update(mac_handle_t,
				    mac_ring_handle_t);
extern void			mac_capab_update(mac_handle_t);
extern int			mac_pdata_update(mac_handle_t, void *,
				    size_t);
extern void			mac_multicast_refresh(mac_handle_t,
				    mac_multicst_t, void *, boolean_t);
extern void			mac_unicst_refresh(mac_handle_t, mac_unicst_t,
				    void *);
extern void			mac_promisc_refresh(mac_handle_t,
				    mac_setpromisc_t, void *);
extern boolean_t		mac_margin_update(mac_handle_t, uint32_t);
extern void			mac_margin_get(mac_handle_t, uint32_t *);
extern int			mac_margin_remove(mac_handle_t, uint32_t);
extern int			mac_margin_add(mac_handle_t, uint32_t *,
				    boolean_t);
extern void			mac_init_ops(struct dev_ops *, const char *);
extern void			mac_fini_ops(struct dev_ops *);
extern int			mac_devt_to_instance(dev_t);
extern minor_t			mac_private_minor(void);
extern void			mac_ring_intr_set(mac_ring_handle_t,
				    ddi_intr_handle_t);
extern void			mac_ring_intr_modify(mac_ring_handle_t,
				    ddi_intr_handle_t, boolean_t);


extern mactype_register_t	*mactype_alloc(uint_t);
extern void			mactype_free(mactype_register_t *);
extern int			mactype_register(mactype_register_t *);
extern int			mactype_unregister(const char *);

extern boolean_t		mac_unicst_verify(mac_handle_t,
				    const uint8_t *, uint_t);

extern int			mac_group_add_ring(mac_group_handle_t, int);
extern void			mac_group_rem_ring(mac_group_handle_t,
				    mac_ring_handle_t);
extern mac_ring_handle_t	mac_find_ring(mac_group_handle_t, int);

extern void			mac_prop_info_set_default_uint8(
				    mac_prop_info_handle_t, uint8_t);
extern void			mac_prop_info_set_default_str(
				    mac_prop_info_handle_t, const char *);
extern void			mac_prop_info_set_default_uint64(
				    mac_prop_info_handle_t, uint64_t);
extern void			mac_prop_info_set_default_uint32(
				    mac_prop_info_handle_t, uint32_t);
extern void			mac_prop_info_set_default_link_flowctrl(
				    mac_prop_info_handle_t, link_flowctrl_t);
extern void			mac_prop_info_set_range_uint32(
				    mac_prop_info_handle_t,
				    uint32_t, uint32_t);
extern void			mac_prop_info_set_perm(mac_prop_info_handle_t,
				    uint8_t);

extern void			mac_hcksum_get(mblk_t *, uint32_t *,
				    uint32_t *, uint32_t *, uint32_t *,
				    uint32_t *);
extern void			mac_hcksum_set(mblk_t *, uint32_t, uint32_t,
				    uint32_t, uint32_t, uint32_t);

extern void			mac_lso_get(mblk_t *, uint32_t *, uint32_t *);

extern void			mac_fcoe_get(mblk_t *, mac_fcoe_tx_params_t *,
				    uint32_t *);

/*
 * MAC MBLK Interfaces for Buffer Management.
 */
extern mblk_t			*mac_mblk_get(mac_handle_t mh,
				    mac_ring_handle_t mrh, mblk_t **tail,
				    int *);

extern void			mac_mblk_info_get(mac_handle_t mh,
				    mblk_t *mp, ddi_dma_handle_t *handlep,
				    uint64_t *ioaddrp, size_t *sizep);

extern boolean_t		mac_mblk_unbound(mac_handle_t mh,
				    mac_ring_handle_t,  mblk_t *mp);

/*
 * MAC descriptors Interface.
 */
extern mac_descriptor_handle_t	mac_descriptors_get(mac_handle_t,
				    mac_ring_handle_t, uint32_t *);

extern size_t			mac_descriptors_length_get(
				    mac_descriptor_handle_t);

extern caddr_t			mac_descriptors_address_get(
				    mac_descriptor_handle_t);

extern uint64_t			mac_descriptors_ioaddress_get(
				    mac_descriptor_handle_t);

extern ddi_dma_handle_t		mac_descriptors_dma_handle_get(
				    mac_descriptor_handle_t);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_MAC_PROVIDER_H */

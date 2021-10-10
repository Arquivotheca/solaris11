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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_IB_EOIB_EIB_IMPL_H
#define	_SYS_IB_EOIB_EIB_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/ddi.h>
#include <sys/mac.h>
#include <sys/mac_provider.h>
#include <sys/mac_ether.h>
#include <sys/sunddi.h>
#include <sys/varargs.h>
#include <sys/vlan.h>
#include <inet/ip.h>				/* ipha_t */
#include <sys/ib/ibtl/ibti.h>
#include <sys/ib/ibtl/ibvti.h>
#include <sys/ib/ib_pkt_hdrs.h>

#include <sys/ib/clients/eoib/fip.h>
#include <sys/ib/clients/eoib/eib.h>

/*
 * Driver specific constants
 */
#define	EIB_E_SUCCESS			0
#define	EIB_E_FAILURE			-1
#define	EIB_MAX_LINE			128
#define	EIB_MAX_SGL			59
#define	EIB_MAX_NDS			0x7FFF
#define	EIB_MAX_POST_MULTIPLE		4
#define	EIB_MAX_PAYLOAD_HDR_SZ		160
#define	EIB_MAX_RX_MP_LEN		16
#define	EIB_TX_COPY_THRESH		4096	/* greater than mtu */
#define	EIB_MAX_VNICS			64	/* do not change this */
#define	EIB_LOGIN_TIMEOUT_USEC		500000	/* set to half a second */
#define	EIB_RWR_CHUNK_SZ		8
#define	EIB_IPHDR_ALIGN_ROOM		32
#define	EIB_IP_HDR_ALIGN		2
#define	EIB_MAX_RX_PKTS_ONINTR		0x800
#define	EIB_MAX_LOGIN_ATTEMPTS		3
#define	EIB_MAX_VHUB_TBL_ATTEMPTS	3
#define	EIB_MAX_KA_ATTEMPTS		3
#define	EIB_MAX_ATTEMPTS		10
#define	EIB_DELAY_HALF_SECOND		500000
#define	EIB_GRH_SZ			(sizeof (ib_grh_t))

/*
 * Debug messages
 */
#define	EIB_MSGS_CRIT		0x01
#define	EIB_MSGS_ERR		0x02
#define	EIB_MSGS_WARN		0x04
#define	EIB_MSGS_DEBUG		0x08
#define	EIB_MSGS_ARGS		0x10
#define	EIB_MSGS_PKT		0x20
#define	EIB_MSGS_VERBOSE	0x40
#define	EIB_MSGS_DEFAULT	(EIB_MSGS_CRIT | EIB_MSGS_ERR | EIB_MSGS_WARN)

#define	EIB_LOGSZ_DEFAULT	0x20000

#define	EIB_DPRINTF_CRIT	eib_dprintf_crit
#define	EIB_DPRINTF_ERR		eib_dprintf_err
#define	EIB_DPRINTF_WARN	eib_dprintf_warn
#ifdef EIB_DEBUG
#define	EIB_DPRINTF_DEBUG	eib_dprintf_debug
#define	EIB_DPRINTF_ARGS	eib_dprintf_args
#define	EIB_DPRINTF_PKT		eib_dprintf_pkt
#define	EIB_DPRINTF_VERBOSE	eib_dprintf_verbose
#else
#define	EIB_DPRINTF_DEBUG	0 &&
#define	EIB_DPRINTF_ARGS	0 &&
#define	EIB_DPRINTF_PKT		0 &&
#define	EIB_DPRINTF_VERBOSE	0 &&
#endif

/*
 * Definitions borrowed from ip implementation specific header
 */
#define	EIB_TCP_CKSUM_OFFSET	16
#define	EIB_UDP_CKSUM_OFFSET	6
#define	EIB_TCPH_CHECKSUMP(ipha, hlen)  \
	((uint16_t *)(((uchar_t *)(ipha)) + ((hlen) + EIB_TCP_CKSUM_OFFSET)))
#define	EIB_UDPH_CHECKSUMP(ipha, hlen)  \
	((uint16_t *)(((uchar_t *)(ipha)) + ((hlen) + EIB_UDP_CKSUM_OFFSET)))

/*
 *  EoIB threads to provide various services
 */
#define	EIB_EVENTS_HDLR		"eib_events_handler"
#define	EIB_VNIC_MANAGER	"eib_vnic_manager"
#define	EIB_KEEPALIVES_HDLR	"eib_keepalives_handler"
#define	EIB_TXWQES_MONITOR	"eib_txwqe_monitor"
#define	EIB_LSOBUFS_MONITOR	"eib_lsobufs_monitor"

/*
 * Macro for finding the least significant bit set in a 64-bit unsigned int
 */
#define	EIB_FIND_LSB_SET(val64)	eib_setbit_mod67[((-(val64) & (val64)) % 67)]

/*
 * LSO buffers
 *
 * Under normal circumstances we should never need to use any buffer
 * that's larger than MTU.  Unfortunately, IB HCA has limitations
 * on the length of SGL that are much smaller than those for regular
 * ethernet NICs.  Since the network layer doesn't care to limit the
 * number of mblk fragments in any send mp chain, we end up having to
 * use these larger buffers occasionally.
 */
#define	EIB_LSO_MAXLEN			65536
#define	EIB_LSO_BUFSZ			8192
#define	EIB_LSO_NUM_BUFS		1024
#define	EIB_LSO_FREE_BUFS_THRESH	(EIB_LSO_NUM_BUFS >> 5)

typedef struct eib_lsobuf_s {
	struct eib_lsobuf_s *lb_next;
	uint8_t		*lb_buf;
	int		lb_isfree;
} eib_lsobuf_t;

typedef struct eib_lsobkt_s {
	kmutex_t	bk_lock;
	kcondvar_t	bk_cv;
	uint_t		bk_status;
	uint8_t		*bk_mem;
	eib_lsobuf_t	*bk_bufl;
	eib_lsobuf_t	*bk_free_head;
	ibt_mr_hdl_t	bk_mr_hdl;
	ibt_lkey_t	bk_lkey;
	uint_t		bk_nelem;
	uint_t		bk_nfree;
} eib_lsobkt_t;

#define	EIB_LBUF_SHORT		0x1
#define	EIB_LBUF_MONITOR_DIE	0x2

/*
 * The admin qp is used only for logins and logouts. The control qp is
 * per vhub, and is used for vhub table, vhub update and vnic alive
 * messages. All of which require precious little in terms of resource.
 * So we set the per qp send limit to around 30 for admin/ctl channels
 * and the combined SRQ size for admin/ctl channels to around 512.
 * For data qps, the send limit is around 4000, and we attempt to set
 * the SRQ size to the minimum of hca_max_chan_sz and hca_max_cq_sz - 1.
 */
#define	EIB_ADMIN_MAX_SWQE		30
#define	EIB_ADMIN_CQ_SIZE		(2 * EIB_ADMIN_MAX_SWQE + 1)
#define	EIB_CTL_MAX_SWQE		30
#define	EIB_CTL_CQ_SIZE			(2 * EIB_CTL_MAX_SWQE + 1)
#define	EIB_DATA_MAX_SWQE		4000
#define	EIB_DFL_DATA_SRQ_WR_SZ		4000
#define	EIB_MIN_DATA_SRQ_WR_SZ		512
#define	EIB_DFL_CTLADM_SRQ_WR_SZ	96
#define	EIB_MIN_CTLADM_SRQ_WR_SZ	64

/*
 * vNIC data channel CQ moderation parameters
 */
#define	EIB_TX_COMP_COUNT		10
#define	EIB_TX_COMP_USEC		300
#define	EIB_RX_COMP_COUNT		4
#define	EIB_RX_COMP_USEC		10

/*
 * qe_info masks (blk:ndx:type:flags)
 */
#define	EIB_WQEBLK_SHIFT		24
#define	EIB_WQEBLK_MASK			0xFF
#define	EIB_WQENDX_SHIFT		16
#define	EIB_WQENDX_MASK			0xFF
#define	EIB_WQETYP_SHIFT		8
#define	EIB_WQETYP_MASK			0xFF
#define	EIB_WQEFLGS_SHIFT		0
#define	EIB_WQEFLGS_MASK		0xFF

/*
 * Macros to get the bit fields from qe_info
 */
#define	EIB_WQE_BLK(info)	(((info) >> EIB_WQEBLK_SHIFT) & EIB_WQEBLK_MASK)
#define	EIB_WQE_NDX(info)	(((info) >> EIB_WQENDX_SHIFT) & EIB_WQENDX_MASK)
#define	EIB_WQE_TYPE(info)	(((info) >> EIB_WQETYP_SHIFT) & EIB_WQETYP_MASK)
#define	EIB_WQE_FLAGS(info)	((info) & EIB_WQEFLGS_MASK)

/*
 * Values for type and flags in qe_info
 */
#define	EIB_WQE_TX			0x1
#define	EIB_WQE_RX			0x2

/*
 * Flag for rx wqes
 */
#define	EIB_WQE_FLG_RET_TO_POOL		0x1
#define	EIB_WQE_FLG_WITH_NW		0x2

/*
 * Flags for tx wqes/buffers
 */
#define	EIB_WQE_FLG_BUFTYPE_LSO		0x4
#define	EIB_WQE_FLG_BUFTYPE_MAPPED	0x8

/*
 * Send/Recv workq entries
 */
typedef struct eib_wqe_s {
	struct eib_wqe_pool_s	*qe_pool;
	uint8_t			*qe_cpbuf;
	uint8_t			*qe_payload_hdr;
	uint_t			qe_bufsz;
	uint_t			qe_info;
	ibt_ud_dest_hdl_t	qe_dest;
	frtn_t			qe_frp;

	mblk_t			*qe_mp;
	ibt_mi_hdl_t		qe_iov_hdl;
	ibt_all_wr_t		qe_wr;
	ibt_wr_ds_t		qe_sgl;
	ibt_wr_ds_t		qe_big_sgl[EIB_MAX_SGL];
	ibt_adds_vect_t		qe_av;
	struct eib_wqe_s	*qe_nxt_post;
	uint_t			qe_hash;
} eib_wqe_t;

/*
 * The wqe in-use/free status in EoIB is managed via a 2-level bitmap
 * logic.
 *
 * Each set of 64 wqes (a "wqe block") is managed by a single 64-bit
 * integer bitmap.  The free status of a set of 64 such wqe blocks (a
 * "wqe pool") is managed by one 64-bit integer bitmap (if any wqe in
 * the wqe block is free, the bit in the map is 1, otherwise it is 0).
 *
 * The maximum pool size is 4096 wqes, but this can easily be extended
 * to support more wqes using additional pools of wqes.
 *
 * Note that an entire pool of wqes is allocated via a single allocation,
 * the wqe addresses in a pool are all contiguous.  The tx/rx copy buffers
 * for a wqe pool are also allocated via a single allocation.
 */
#define	EIB_BLKS_PER_POOL	64
#define	EIB_WQES_PER_BLK	64	/* do not change this */
#define	EIB_WQES_PER_POOL	(EIB_BLKS_PER_POOL * EIB_WQES_PER_BLK)

#define	EIB_WQE_SZ		(sizeof (eib_wqe_t))
#define	EIB_WQEBLK_SZ		(EIB_WQES_PER_BLK * EIB_WQE_SZ)

typedef struct eib_wqe_pool_s {
	struct eib_wqe_pool_s	*wp_next;
	struct eib_s		*wp_ss;
	ib_vaddr_t		wp_vaddr;
	ib_memlen_t		wp_memsz;
	ibt_mr_hdl_t		wp_mr;
	ibt_lkey_t		wp_lkey;
	uint_t			wp_nfree_lwm;
	uint_t			wp_num_with_nw;
	int			wp_type;

	kmutex_t		wp_lock;
	kcondvar_t		wp_cv;
	uint_t			wp_status;
	uint_t			wp_nfree;
	uint64_t		wp_free_blks;
	uint64_t		wp_free_wqes[EIB_BLKS_PER_POOL];
	struct eib_wqe_s	*wp_wqe;
} eib_wqe_pool_t;

/*
 * Values for wp_type
 */
#define	EIB_WP_TYPE_TX		0x1
#define	EIB_WP_TYPE_RX		0x2

/*
 * Values for wp_status (bit fields)
 */
#define	EIB_TXWQE_SHORT		0x1	/* only for tx wqe pool */
#define	EIB_TXWQE_MONITOR_DIE	0x2	/* only for tx wqe pool */

/*
 * The low-water-mark is an indication of when low-priority swqe grabs
 * should start to get refused (but swqe grabs for sending keepalives
 * will still be allowed).  The high-water-mark is an indication of when
 * normal behavior should resume.
 */
#define	EIB_NFREE_SWQES_LWM	(EIB_WQES_PER_POOL / 64)	/* 1/64 */
#define	EIB_NFREE_SWQES_HWM	(EIB_WQES_PER_POOL / 32)	/* 1/32 */

/*
 * For keepalive post-send operations and all post-srq operations, we
 * specify EIB_WPRI_HI.  All regular post-send operations use EIB_WPRI_LO.
 */
#define	EIB_WPRI_HI		1
#define	EIB_WPRI_LO		2

/*
 * The "max with nw" is used to determine when we should start using
 * allocb() to copy and send received mblks in the rx path.  This is to
 * avoid all the receive wqes being stuck with the network layer with
 * nothing on the SRQ (which is really bad).  The threshold should take
 * into account the receive wqes that could remain in the data SRQ
 * rxq's.
 */
#define	EIB_MIN_POSTED		(EIB_WQES_PER_POOL / 8)
#define	EIB_MAX_IN_RXQS		(EIB_NUM_DATA_RXQ * EIB_RWR_CHUNK_SZ)
#define	EIB_NUM_WITH_NW_LIMIT	\
	(EIB_WQES_PER_POOL - EIB_MAX_IN_RXQS - EIB_MIN_POSTED)

/*
 * Multicast GID Layout: the multicast gid is specified in big-endian
 * representation, as a collection of different-sized fields in the
 * EoIB specification.  On Solaris, the multicast gid is represented
 * as a collection of two 8-byte fields (in ib_gid_t).
 */
typedef struct eib_mgid_spec_s {
	uint8_t			sp_mgid_prefix[FIP_MGID_PREFIX_LEN];
	uint8_t			sp_type;
	uint8_t			sp_dmac[ETHERADDRL];
	uint8_t			sp_rss_hash;
	uint8_t			sp_vhub_id[FIP_VHUBID_LEN];
} eib_mgid_spec_t;

/*
 * Values for sp_type in mgid as per EoIB specification
 */
#define	EIB_MGID_VHUB_DATA	0x0
#define	EIB_MGID_VHUB_UPDATE	0x2
#define	EIB_MGID_VHUB_TABLE	0x3

typedef union eib_mgid_s {
	eib_mgid_spec_t		gd_spec;
	ib_gid_t		gd_sol;
} eib_mgid_t;

/*
 * Gateway properties handed over to us by the EoIB nexus
 */
typedef struct eib_gw_props_s {
	kmutex_t		pp_gw_lock;

	ib_guid_t		pp_gw_system_guid;
	ib_guid_t		pp_gw_guid;
	ib_sn_prefix_t		pp_gw_sn_prefix;

	uint_t			pp_gw_adv_period;
	uint_t			pp_gw_ka_period;
	uint_t			pp_vnic_ka_period;

	ib_qpn_t		pp_gw_ctrl_qpn;
	ib_lid_t		pp_gw_lid;
	uint16_t		pp_gw_portid;

	uint16_t		pp_gw_num_net_vnics;
	uint8_t			pp_gw_flag_available;
	uint8_t			pp_gw_is_host_adm_vnics;
	uint8_t			pp_gw_sl;
	uint8_t			pp_gw_n_rss_qpn;

	uint8_t			*pp_gw_system_name;
	uint8_t			*pp_gw_port_name;
	uint8_t			*pp_gw_vendor_id;

	clock_t			pp_gw_ka_ticks;		/* 2.5 x gw_ka_period */
	clock_t			pp_vnic_ka_ticks;	/* vnic_ka_period */
} eib_gw_props_t;

/*
 * Port-specific properties
 */
typedef struct eib_props_s {
	uint64_t		ep_ifspeed;
	ib_guid_t		ep_hca_guid;
	uint8_t			ep_port_num;
	ib_gid_t		ep_sgid;
	ib_lid_t		ep_blid;
	uint16_t		ep_mtu;
	ibt_srate_t		ep_srate;
} eib_props_t;

/*
 * Capabilities derived from HCA attributes
 */
typedef struct eib_caps_s {
	uint_t			cp_lso_maxlen;
	uint32_t		cp_cksum_flags;
	int			cp_resv_lkey_capab;
	ibt_lkey_t		cp_resv_lkey;

	uint_t			cp_max_swqe;
	uint_t			cp_max_rwqe;
	uint_t			cp_max_sgl;
	uint_t			cp_hiwm_sgl;
} eib_caps_t;

/*
 * Transmit queues per vnic data channel.  Simple XOR hash on
 * {src_ip, dst_ip, src_port, dst_port} picks the queue.
 */
#define	EIB_NUM_TX_QUEUES	11
#define	EIB_PKT_HASH_4B(x)	((x)[0] ^ (x)[1] ^ (x)[2] ^ (x)[3])

typedef struct eib_chan_txq_s {
	kmutex_t		tx_lock;
	kcondvar_t		tx_cv;
	boolean_t		tx_busy;
	struct eib_wqe_s	*tx;
	struct eib_wqe_s	*tx_tail;
} eib_chan_txq_t;

/*
 * Admin/control/data channel information
 */
typedef struct eib_chan_s {
	ibt_channel_hdl_t	ch_chan;
	ib_qpn_t		ch_qpn;

	ibt_wc_t		*ch_wc;
	ibt_cq_hdl_t		ch_cq_hdl;
	uint_t			ch_cq_sz;

	ibt_wc_t		*ch_rcv_wc;
	ibt_cq_hdl_t		ch_rcv_cq_hdl;
	uint_t			ch_rcv_cq_sz;

	boolean_t		ch_tear_down;

	kmutex_t		ch_pkey_lock;
	ib_pkey_t		ch_pkey;
	uint16_t		ch_pkey_ix;

	kmutex_t		ch_cep_lock;
	kcondvar_t		ch_cep_cv;
	ibt_cep_state_t		ch_cep_state;

	eib_chan_txq_t		ch_txq[EIB_NUM_TX_QUEUES];

	kmutex_t		ch_cqstate_lock;
	kcondvar_t		ch_cqstate_cv;
	boolean_t		ch_cqstate_wait;
	boolean_t		ch_cqstate_empty;

	kmutex_t		ch_rcqstate_lock;
	kcondvar_t		ch_rcqstate_cv;
	boolean_t		ch_rcqstate_wait;
	boolean_t		ch_rcqstate_empty;

	kmutex_t		ch_emptychan_lock;
	kcondvar_t		ch_emptychan_cv;
	boolean_t		ch_emptychan;
} eib_chan_t;

/*
 * States for vNIC state machine during login
 */
#define	EIB_LOGIN_INIT		0
#define	EIB_LOGIN_ACK_WAIT	1
#define	EIB_LOGIN_ACK_RCVD	2
#define	EIB_LOGIN_NACK_RCVD	3
#define	EIB_LOGIN_TBL_WAIT	4
#define	EIB_LOGIN_TBL_INPROG	5
#define	EIB_LOGIN_TBL_DONE	6
#define	EIB_LOGIN_TBL_FAILED	7
#define	EIB_LOGIN_DONE		8
#define	EIB_LOGIN_TIMED_OUT	9
#define	EIB_LOGOUT_DONE		10

typedef struct eib_login_data_s {
	ib_guid_t		ld_gw_guid;
	ib_lid_t		ld_gw_lid;
	uint_t			ld_syndrome;
	uint16_t		ld_gw_port_id;
	ib_qpn_t		ld_gw_data_qpn;
	ib_qpn_t		ld_gw_ctl_qpn;
	uint16_t		ld_vnic_id;	/* includes set msbit */
	uint16_t		ld_vhub_mtu;
	uint16_t		ld_vhub_pkey;
	uint16_t		ld_assigned_vlan;
	uint8_t			ld_gw_sl;
	uint8_t			ld_n_rss_mcgid;
	uint8_t			ld_n_mac_mcgid;
	uint8_t			ld_vnic_name[FIP_VNIC_NAME_LEN];
	uint8_t			ld_assigned_mac[ETHERADDRL];
	uint8_t			ld_gw_mgid_prefix[FIP_MGID_PREFIX_LEN];
	uint8_t			ld_vlan_in_packets;
	uint32_t		ld_vhub_id;
} eib_login_data_t;

#define	EIB_UNICAST_MAC(mac)		(((mac)[0] & 0x01) == 0)

/*
 * Map to translate between DMAC and {qpn, lid, sl}
 */
typedef struct eib_vhub_map_s {
	struct eib_vhub_map_s	*mp_next;
	uint32_t		mp_tusn;
	ib_qpn_t		mp_qpn;
	ib_lid_t		mp_lid;
	uint8_t			mp_mac[ETHERADDRL];
	uint8_t			mp_sl;
	uint8_t			mp_v_rss_type;
} eib_vhub_map_t;

/*
 * Per-vNIC vHUB Table
 */
#define	EIB_TB_NBUCKETS		13
typedef struct eib_vhub_table_s {
	krwlock_t		tb_lock;
	struct eib_vhub_map_s	*tb_gateway;
	struct eib_vhub_map_s	*tb_unicast_miss;
	struct eib_vhub_map_s	*tb_vhub_multicast;
	struct eib_vhub_map_s	*tb_vnic_entry[EIB_TB_NBUCKETS];
	struct eib_vhub_map_s	*tb_mcast_entry[EIB_TB_NBUCKETS];

	uint32_t		tb_tusn;
	uint8_t			tb_eport_state;

	uint16_t		tb_entries_seen;
	uint16_t		tb_entries_in_table;
	uint32_t		tb_checksum;
} eib_vhub_table_t;

typedef struct eib_vhub_update_s {
	kmutex_t		up_lock;
	eib_vhub_map_t		*up_vnic_entry;
	uint32_t		up_tusn;
	uint8_t			up_eport_state;
} eib_vhub_update_t;

typedef struct eib_ether_hdr_s {
	int			eh_tagless;
	uint16_t		eh_ether_type;
	uint16_t		eh_vlan;
	uint8_t			eh_dmac[ETHERADDRL];
	uint8_t			eh_smac[ETHERADDRL];
} eib_ether_hdr_t;

typedef struct eib_nw_hdrinfo_s {
	uint_t			nh_eth_hdr_len;
	uint_t			nh_ip_hdr_len;
	uint_t			nh_tcp_hdr_len;
	ipha_t			*nh_ipha;
	uint_t			nh_hash;
} eib_nw_hdrinfo_t;

/*
 * List of multicast groups the vnic joined.  Only one join is
 * actually performed and all the {macaddr, dgid} tuples
 * multiplexing on this channel are held in a list.
 */
typedef struct eib_mcg_member_s {
	struct eib_mcg_member_s	*mm_next;
	ib_gid_t		mm_dgid;
	uint8_t			mm_mac[ETHERADDRL];
} eib_mcg_member_t;

#define	EIB_FLG_LEAVE_ALL_MCGS		0
#define	EIB_FLG_LEAVE_ONE_MCG		1

#define	EIB_MAX_MCGS_PER_VNIC		48
typedef struct eib_mcg_s {
	ib_gid_t		mg_rgid;
	ib_gid_t		mg_mgid;
	uint8_t			mg_join_state;
	ibt_mcg_info_t		*mg_mcginfo;
	struct eib_mcg_member_s	*mg_elem;
	uint_t			mg_nelem;
} eib_mcg_t;

/*
 * vNIC Information
 */
typedef struct eib_vnic_s {
	struct eib_s		*vn_ss;
	eib_chan_t		*vn_ctl_chan;
	eib_chan_t		*vn_data_chan;
	int			vn_instance;
	uint16_t		vn_vlan;
	uint16_t		vn_id;
	uint8_t			vn_macaddr[ETHERADDRL];
	struct eib_login_data_s	vn_login_data;

	kmutex_t		vn_lock;
	kcondvar_t		vn_cv;
	uint_t			vn_state;
	struct eib_vhub_table_s	*vn_vhub_table;
	struct eib_vhub_update_s *vn_vhub_update;

	krwlock_t		vn_vhub_mcgs_lock;
	struct eib_mcg_s	*vn_vhub_tbl_mcg;
	struct eib_mcg_s	*vn_vhub_upd_mcg;
	struct eib_mcg_s	*vn_vhub_data_mcgs;

	ddi_softint_handle_t    vn_ctl_si_hdl;
	ddi_softint_handle_t    vn_data_tx_si_hdl;
	ddi_softint_handle_t    vn_data_rx_si_hdl;
} eib_vnic_t;


/*
 * Base NIC's mac state flags. The lock protects the starting/stopping
 * bits.  Access to the rest of the mac state is protected by these
 * two bits.
 */
#define	EIB_NIC_STARTING	0x01
#define	EIB_NIC_STOPPING	0x02
#define	EIB_NIC_STARTED		0x80
#define	EIB_NIC_RESTARTING	(EIB_NIC_STARTING | EIB_NIC_STOPPING)

typedef struct eib_node_state_s {
	kmutex_t		ns_lock;
	kcondvar_t		ns_cv;
	uint_t			ns_nic_state;
	link_state_t		ns_link_state;
} eib_node_state_t;

#define	EIB_STARTED(x)	((x)->ei_node_state->ns_nic_state & EIB_NIC_STARTED)

/*
 * MIB-II statistics to report to the mac layer
 */
typedef struct eib_stats_s {
	uint64_t		st_obytes;	/* bytes sent out */
	uint64_t		st_opkts;	/* pkts sent out */
	uint64_t		st_brdcstxmit;	/* broadcast pkts transmitted */
	uint64_t		st_multixmit;	/* multicast pkts transmitted */
	uint64_t		st_oerrors;	/* transmit errors */
	uint64_t		st_noxmitbuf;	/* transmit pkts discarded */

	uint64_t		st_rbytes;	/* bytes received */
	uint64_t		st_ipkts;	/* pkts received */
	uint64_t		st_brdcstrcv;	/* broadcast pkts received */
	uint64_t		st_multircv;	/* multicast pkts received */
	uint64_t		st_ierrors;	/* receive errors */
	uint64_t		st_norcvbuf;	/* receive pkts discarded */
} eib_stats_t;

#define	EIB_UPDT_COUNTER_32(addr, val)	(atomic_add_32((addr), (val)))
#define	EIB_INCR_COUNTER_32(addr)	(atomic_inc_32((addr)))
#define	EIB_INCR_COUNTER_32_NV(addr)	(atomic_inc_32_nv((addr)))
#define	EIB_DECR_COUNTER_32(addr)	(atomic_dec_32((addr)))

#define	EIB_UPDT_COUNTER(addr, val)	(atomic_add_64((addr), (val)))
#define	EIB_INCR_COUNTER(addr)		(atomic_inc_64((addr)))
#define	EIB_INCR_COUNTER_NV(addr)	(atomic_inc_64_nv((addr)))
#define	EIB_DECR_COUNTER(addr)		(atomic_dec_64((addr)))

/*
 * vNIC creation and deletion are serialized by a non-zero value
 * to the ei_vnic_state member (i.e. only one vnic may be created
 * or deleted at a time). The code makes sure to access/update
 * the ei_active_vnics member only after a successful setting of
 * ei_vnic_state.
 */
#define	EIB_VN_BEING_CREATED	0x01
#define	EIB_VN_BEING_DELETED	0x02
#define	EIB_VN_BEING_MODIFIED	(EIB_VN_BEING_CREATED | EIB_VN_BEING_DELETED)

/*
 * All possible EoIB event work items that need to be handled
 */
#define	EIB_EV_NONE		0
#define	EIB_EV_PORT_DOWN	1
#define	EIB_EV_PORT_UP		2
#define	EIB_EV_PKEY_CHANGE	3
#define	EIB_EV_SGID_CHANGE	4
#define	EIB_EV_CLNT_REREG	5
#define	EIB_EV_GW_EPORT_DOWN	6
#define	EIB_EV_GW_DOWN		7
#define	EIB_EV_GW_UP		8
#define	EIB_EV_GW_INFO_UPDATE	9
#define	EIB_EV_MCG_DELETED	10
#define	EIB_EV_MCG_CREATED	11
#define	EIB_EV_SHUTDOWN		12

typedef struct eib_event_s {
	struct eib_event_s	*ev_next;
	uint_t			ev_code;
	void			*ev_arg;
} eib_event_t;

/*
 * Work element for vnic creation/deletion
 */
typedef struct eib_vnic_req_s {
	struct eib_vnic_req_s	*vr_next;
	uint_t			vr_req;
	uint8_t			vr_mac[ETHERADDRL];
	uint16_t		vr_vlan;
} eib_vnic_req_t;

/*
 * Values for vr_req
 */
#define	EIB_CR_REQ_NEW_VNIC	1
#define	EIB_CR_REQ_DEL_VNIC	2
#define	EIB_CR_REQ_FLUSH	3
#define	EIB_CR_REQ_DIE		4

/*
 * Work element for vnics kept alive by the keepalive manager thread
 * and bitfield values for ei_ka_event.
 */
typedef struct eib_ka_vnics_s {
	struct eib_ka_vnics_s	*ka_next;
	struct eib_vnic_s	*ka_vnic;
} eib_ka_vnics_t;

#define	EIB_KA_MANAGER_DIE	0x1
#define	EIB_KA_VNICS_TIMED_OUT	0x2

/*
 * Receive wqes waiting to get posted into SRQ.
 */
typedef struct eib_rxq_s {
	kmutex_t		rx_q_lock;
	uint_t			rx_q_count;
	struct eib_wqe_s	*rx_q[EIB_RWR_CHUNK_SZ];
} eib_rxq_t;

/*
 * These values go into sr_num_rxq and should be powers of 2
 */
#define	EIB_MAX_RX_QUEUES	8
#define	EIB_NUM_CTLADM_RXQ	2
#define	EIB_NUM_DATA_RXQ	EIB_MAX_RX_QUEUES

/*
 * SRQ for data and control qps
 */
typedef struct eib_srq_s {
	ibt_srq_hdl_t		sr_srq_hdl;
	uint_t			sr_srq_wr_sz;
	uint_t			sr_srq_sgl_sz;
	uint_t			sr_srq_min_wr_sz;

	uint_t			sr_ip_hdr_align;
	boolean_t		sr_alloc_mp;

	uint_t			sr_num_rxq;
	struct eib_rxq_s	sr_rxq[EIB_MAX_RX_QUEUES];
} eib_srq_t;

/*
 * For rwqe hashing we take (sr_num_rxq - 1) bits off of the rwqe address
 * skipping the least-significant zero bits in every wqe address.  Ideally,
 * this would mean we find the least significant bit set in sizeof
 * (eib_wqe_t) and shift the rwqe address that many bits.  But we can avoid
 * the effort (load) in doing this if we hardcode the value 7 here. If the
 * size of eib_wqe_t changes, review this value.
 */
#define	EIB_RWQE_HASH_SHIFT	7
#define	EIB_RWQE_HASH(s, x)	\
	((((uintptr_t)(x)) >> EIB_RWQE_HASH_SHIFT) & ((s)->sr_num_rxq - 1))

/*
 * RX Ring
 */
#define	EIB_NUM_RX_RINGS	1
typedef struct eib_rx_ring_s {
	struct eib_s		*rr_ss;

	kmutex_t		rr_lock;
	mac_ring_handle_t	rr_hdl;
	uint64_t		rr_gen_num;
	mblk_t			*rr_mp;
	mblk_t			*rr_mp_tail;
	uint_t			rr_polling;
} eib_rx_ring_t;

/*
 * EoIB per-instance state
 */
typedef struct eib_s {
	ibt_clnt_hdl_t		ei_ibt_hdl;
	ibt_hca_hdl_t		ei_hca_hdl;
	ibt_pd_hdl_t		ei_pd_hdl;
	mac_handle_t		ei_mac_hdl;
	mac_group_handle_t	ei_mac_rxgrp_hdl;
	mac_resource_handle_t   ei_mac_rsrc_hdl;

	ddi_softint_handle_t    ei_admin_si_hdl;
	ddi_callback_id_t	ei_login_ack_cb;
	ddi_callback_id_t	ei_gw_alive_cb;
	ddi_callback_id_t	ei_gw_info_cb;

	ibt_hca_attr_t		*ei_hca_attrs;
	dev_info_t		*ei_dip;
	uint_t			ei_instance;

	struct eib_gw_props_s	*ei_gw_props;
	struct eib_props_s	*ei_props;
	struct eib_caps_s	*ei_caps;
	struct eib_stats_s	*ei_stats;

	struct eib_node_state_s	*ei_node_state;
	struct eib_chan_s	*ei_admin_chan;

	struct eib_wqe_pool_s	*ei_tx;
	struct eib_wqe_pool_s	*ei_rx;
	struct eib_lsobkt_s	*ei_lso;

	struct eib_rx_ring_s	*ei_rx_ring[EIB_NUM_RX_RINGS];

	struct eib_srq_s	*ei_data_srq;
	struct eib_srq_s	*ei_ctladm_srq;

	kmutex_t		ei_vnic_state_lock;
	kcondvar_t		ei_vnic_state_cv;
	uint_t			ei_vnic_state;

	krwlock_t		ei_vnic_lock;
	uint64_t		ei_active_vnics;
	uint64_t		ei_zombie_vnics;
	uint64_t		ei_rejoin_vnics;
	struct eib_vnic_s	*ei_vnic[EIB_MAX_VNICS];
	struct eib_vnic_s	*ei_vnic_pending;
	struct eib_vnic_s	*ei_vnic_torndown;

	kmutex_t		ei_ev_lock;
	kcondvar_t		ei_ev_cv;
	struct eib_event_s	*ei_event;

	kmutex_t		ei_vnic_req_lock;
	kcondvar_t		ei_vnic_req_cv;
	struct eib_vnic_req_s	*ei_vnic_req;
	struct eib_vnic_req_s	*ei_failed_vnic_req;
	struct eib_vnic_req_s	*ei_pending_vnic_req;

	kmutex_t		ei_ka_lock;
	kcondvar_t		ei_ka_cv;
	uint_t			ei_ka_event;
	struct eib_ka_vnics_s	*ei_ka_vnics;
	int64_t			ei_ka_gw_heartbeat;
	boolean_t		ei_ka_gw_unreachable;
	uint8_t			ei_ka_gw_eport_state;

	kt_did_t		ei_txwqe_monitor;
	kt_did_t		ei_lsobufs_monitor;
	kt_did_t		ei_vnic_manager;
	kt_did_t		ei_events_handler;
	kt_did_t		ei_keepalives_manager;
} eib_t;

/*
 * Private read-only datalink properties
 */
#define	EIB_DLPROP_GW_EPORT_STATE	"_eib_eport_state"
#define	EIB_DLPROP_HCA_GUID		"_eib_hca_guid"
#define	EIB_DLPROP_PORT_GUID		"_eib_port_guid"

/*
 * FUNCTION PROTOTYPES FOR CROSS-FILE LINKAGE
 */

/*
 * FIP protocol related
 */
extern int eib_fip_login(eib_t *, eib_vnic_t *, int *);
extern int eib_fip_heartbeat(eib_t *, eib_vnic_t *, int *);
extern int eib_fip_vhub_table(eib_t *, eib_vnic_t *, int *);
extern int eib_fip_logout(eib_t *, eib_vnic_t *, int *);
extern int eib_fip_parse_login_ack(eib_t *, uint8_t *, eib_login_data_t *);
extern int eib_fip_parse_ctl_pkt(uint8_t *, eib_vnic_t *);

/*
 * Service threads and other handlers
 */
extern void eib_events_handler(eib_t *);
extern void eib_svc_enqueue_event(eib_t *, eib_event_t *);
extern void eib_vnic_manager(eib_t *);
extern void eib_monitor_tx_wqes(eib_t *);
extern void eib_monitor_lso_bufs(eib_t *);
extern void eib_manage_keepalives(eib_t *);
extern void eib_stop_events_handler(eib_t *);
extern void eib_stop_vnic_manager(eib_t *);
extern void eib_stop_monitor_tx_wqes(eib_t *);
extern int eib_stop_monitor_lso_bufs(eib_t *);
extern void eib_stop_manage_keepalives(eib_t *);
extern void eib_flush_vnic_reqs(eib_t *);
extern void eib_gw_info_cb(dev_info_t *, ddi_eventcookie_t, void *, void *);
extern void eib_gw_alive_cb(dev_info_t *, ddi_eventcookie_t, void *, void *);
extern void eib_login_ack_cb(dev_info_t *, ddi_eventcookie_t, void *, void *);

/*
 * Admin QP related
 */
extern int eib_adm_setup_qp(eib_t *, int *);
extern uint_t eib_adm_comp_handler(caddr_t, caddr_t);
extern void eib_rb_adm_setup_qp(eib_t *);

/*
 * Control QP related
 */
extern int eib_ctl_create_qp(eib_t *, eib_vnic_t *, int *);
extern uint_t eib_ctl_comp_handler(caddr_t, caddr_t);
extern void eib_rb_ctl_create_qp(eib_t *, eib_vnic_t *);

/*
 * Data QP related
 */
extern int eib_data_create_qp(eib_t *, eib_vnic_t *, int *);
extern uint_t eib_data_rx_comp_handler(caddr_t, caddr_t);
extern uint_t eib_data_tx_comp_handler(caddr_t, caddr_t);
extern void eib_data_rx_recycle(caddr_t);
extern void eib_data_post_tx(eib_vnic_t *, eib_wqe_t *);
extern void eib_data_parse_ether_hdr(mblk_t *, eib_ether_hdr_t *);
extern int eib_data_lookup_vnic(eib_t *, uint8_t *, uint16_t, eib_vnic_t **,
    boolean_t *);
extern int eib_data_prepare_frame(eib_vnic_t *, eib_wqe_t *, mblk_t *,
    eib_ether_hdr_t *);
extern void eib_rb_data_create_qp(eib_t *, eib_vnic_t *);

/*
 * Resource related
 */
extern int eib_rsrc_setup_bufs(eib_t *, int *);
extern int eib_rsrc_grab_swqes(eib_t *, eib_wqe_t **, uint_t, uint_t *, int);
extern int eib_rsrc_grab_rwqes(eib_t *, eib_wqe_t **, uint_t, uint_t *, int);
extern int eib_rsrc_grab_lsobufs(eib_t *, uint_t, ibt_wr_ds_t *, uint32_t *);
extern eib_wqe_t *eib_rsrc_grab_swqe(eib_t *, int);
extern eib_wqe_t *eib_rsrc_grab_rwqe(eib_t *, int);
extern void eib_rsrc_return_swqe(eib_t *, eib_wqe_t *);
extern void eib_rsrc_return_rwqe(eib_t *, eib_wqe_t *);
extern void eib_rsrc_return_lsobufs(eib_t *, ibt_wr_ds_t *, uint32_t);
extern eib_wqe_t *eib_rsrc_nxt_rwqe_to_free(eib_t *);
extern void eib_rsrc_txwqes_needed(eib_t *);
extern void eib_rsrc_lsobufs_needed(eib_t *);
extern void eib_rb_rsrc_setup_bufs(eib_t *);

/*
 * IBT related
 */
extern int eib_ibt_hca_init(eib_t *);
extern void eib_ibt_link_mod(eib_t *);
extern int eib_ibt_modify_chan_pkey(eib_t *, eib_chan_t *, ib_pkey_t);
extern void eib_ibt_async_handler(void *, ibt_hca_hdl_t, ibt_async_code_t,
    ibt_async_event_t *);
extern void eib_ibt_record_capab(eib_t *, ibt_hca_attr_t *, eib_caps_t *);
extern int eib_ibt_setup_srqs(eib_t *, int *);
extern void eib_rb_ibt_hca_init(eib_t *, uint_t);
extern void eib_rb_ibt_setup_srqs(eib_t *);

/*
 * Chan related
 */
extern eib_chan_t *eib_chan_init(void);
extern void eib_chan_fini(eib_chan_t *);
extern int eib_chan_fill_srq(eib_t *, eib_srq_t *);
extern void eib_chan_post_rwqe(eib_t *, eib_srq_t *, eib_wqe_t *);
extern int eib_chan_post_rwrs(eib_t *, eib_srq_t *, eib_wqe_t **,
    uint_t, uint_t *);

/*
 * Mac layer related
 */
extern void eib_mac_set_nic_state(eib_t *, uint_t);
extern void eib_mac_clr_nic_state(eib_t *, uint_t);
extern void eib_mac_upd_nic_state(eib_t *, uint_t, uint_t);
extern uint_t eib_mac_get_nic_state(eib_t *);
extern void eib_mac_link_state(eib_t *, link_state_t, boolean_t);
extern void eib_mac_link_down(eib_t *, boolean_t);
extern void eib_mac_link_up(eib_t *, boolean_t);
extern int eib_mac_start(eib_t *);
extern void eib_mac_stop(eib_t *);
extern int eib_mac_multicast(eib_t *, boolean_t, uint8_t *);
extern int eib_mac_promisc(eib_t *, boolean_t);
extern int eib_mac_tx(eib_t *, mblk_t *);
extern void eib_mac_ring_fill(void *, mac_ring_type_t, const int,
    const int, mac_ring_info_t *, mac_ring_handle_t);
extern void eib_mac_group_fill(void *, mac_ring_type_t, const int,
    mac_group_info_t *, mac_group_handle_t);
extern int eib_mac_hca_portstate(eib_t *, ib_lid_t *, int *);
extern int eib_mac_cleanup_hca_resources(eib_t *);

/*
 * VNIC related
 */
extern int eib_vnic_create(eib_t *, uint8_t *, uint16_t, eib_vnic_t **, int *);
extern void eib_vnic_delete(eib_t *, eib_vnic_t *);
extern int eib_vnic_wait_for_login_ack(eib_t *, eib_vnic_t *, int *);
extern void eib_vnic_login_ack(eib_t *, eib_login_data_t *);
extern int eib_vnic_wait_for_table(eib_t *, eib_vnic_t *, int *);
extern void eib_vnic_vhub_table_done(eib_vnic_t *, uint_t);
extern int eib_vnic_join_multicast(eib_t *, eib_vnic_t *, uint8_t *, int *);
extern void eib_vnic_leave_multicast(eib_t *, eib_vnic_t *, uint8_t *);
extern int eib_vnic_setup_dest(eib_vnic_t *, eib_wqe_t *, uint8_t *, uint16_t);
extern void eib_vnic_init_tables(eib_t *, eib_vnic_t *);
extern void eib_vnic_fini_tables(eib_t *, eib_vnic_t *, boolean_t);
extern void eib_vnic_need_new(eib_t *, uint8_t *, uint16_t);
extern void eib_vnic_do_delete(eib_t *, uint8_t *, uint16_t);
extern void eib_vnic_enqueue_req(eib_t *, eib_vnic_req_t *);
extern void eib_vnic_resurrect_zombies(eib_t *, uint8_t *);
extern void eib_vnic_restart(eib_t *, int, uint8_t *);
extern void eib_vnic_rejoin_mcgs(eib_t *);
extern void eib_rb_vnic_create(eib_t *, eib_vnic_t *, uint_t);

/*
 * Logging and other stuff
 */
extern void eib_debug_init(void);
extern void eib_debug_fini(void);
extern void eib_dprintf_crit(int, const char *fmt, ...);
extern void eib_dprintf_err(int, const char *fmt, ...);
extern void eib_dprintf_warn(int, const char *fmt, ...);
#ifdef EIB_DEBUG
extern void eib_dprintf_debug(int, const char *fmt, ...);
extern void eib_dprintf_args(int, const char *fmt, ...);
extern void eib_dprintf_pkt(int, uint8_t *, uint_t);
extern void eib_dprintf_verbose(int, const char *fmt, ...);
#endif
extern int eib_get_props(eib_t *);
extern void eib_update_props(eib_t *, eib_gw_info_t *);
extern void eib_rb_get_props(eib_t *);

/*
 * EoIB specific global variables
 */
extern ib_gid_t eib_reserved_gid;
extern uint8_t eib_zero_mac[];
extern uint8_t eib_broadcast_mac[];
extern int eib_setbit_mod67[];
extern char *eib_pvt_props[];

/*
 * HW/FW workarounds
 */
extern int eib_wa_no_desc_list_len;
extern int eib_wa_no_cksum_offload;
extern int eib_wa_no_lso;
extern int eib_wa_no_mcast_entries;
extern int eib_wa_no_good_vp_flag;
extern int eib_wa_no_good_vhub_cksum;

/*
 * Miscellaneous externs
 */
extern void freemsgchain(mblk_t *);
extern pri_t minclsyspri;

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IB_EOIB_EIB_IMPL_H */

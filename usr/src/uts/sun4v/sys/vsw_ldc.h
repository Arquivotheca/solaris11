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

/*
 * This header file contains the basic data structures which the
 * virtual switch (vsw) uses to communicate with vnet clients.
 *
 * The virtual switch reads the machine description (MD) to
 * determine how many port_t structures to create (each port_t
 * can support communications to a single network device). The
 * port_t's are maintained in a linked list.
 *
 * Each port in turn contains a number of logical domain channels
 * (ldc's) which are inter domain communications channels which
 * are used for passing small messages between the domains. There
 * may be any number of channels associated with each port, though
 * currently most devices only have a single channel. The current
 * implementation provides support for only one channel per port.
 *
 * The ldc is a bi-directional channel, which is divided up into
 * two directional 'lanes', one outbound from the switch to the
 * virtual network device, the other inbound to the switch.
 * Depending on the type of device each lane may have separate
 * communication parameters (such as mtu etc).
 *
 * For those network clients which use descriptor rings the
 * rings are associated with the appropriate lane. I.e. rings
 * which the switch exports are associated with the outbound lanes
 * while those which the network clients are exporting to the switch
 * are associated with the inbound lane.
 *
 * In diagram form the data structures look as follows:
 *
 * vsw instance
 *     |
 *     +----->port_t----->port_t----->port_t----->
 *		|
 *		+--->ldc_t
 *		       |
 *		       +--->lane_t (inbound)
 *		       |       |
 *		       |       +--->dring
 *		       |
 *		       +--->lane_t (outbound)
 *			       |
 *			       +--->dring
 *
 */

#ifndef	_VSW_LDC_H
#define	_VSW_LDC_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * LDC pkt tranfer MTU - largest msg size used
 */
#define	VSW_LDC_MTU		64

#define	VSW_DEF_MSG_WORDS	\
	(VNET_DRING_REG_EXT_MSG_SIZE_MAX / sizeof (uint64_t))

/*
 * Default message type.
 */
typedef struct def_msg {
	uint64_t	data[VSW_DEF_MSG_WORDS];
} def_msg_t;

/*
 * Currently only support one major/minor pair.
 */
#define	VSW_NUM_VER	1

typedef struct ver_sup {
	uint16_t	ver_major;	/* major version number */
	uint16_t	ver_minor;	/* minor version number */
} ver_sup_t;

/*
 * Lane states.
 */
#define	VSW_LANE_INACTIV	0x0	/* No params set for lane */

#define	VSW_VER_INFO_SENT	0x1	/* Version # sent to peer */
#define	VSW_VER_INFO_RECV	0x2	/* Version # recv from peer */
#define	VSW_VER_ACK_RECV	0x4
#define	VSW_VER_ACK_SENT	0x8
#define	VSW_VER_NACK_RECV	0x10
#define	VSW_VER_NACK_SENT	0x20

#define	VSW_ATTR_INFO_SENT	0x40	/* Attributes sent to peer */
#define	VSW_ATTR_INFO_RECV	0x80	/* Peer attributes received */
#define	VSW_ATTR_ACK_SENT	0x100
#define	VSW_ATTR_ACK_RECV	0x200
#define	VSW_ATTR_NACK_SENT	0x400
#define	VSW_ATTR_NACK_RECV	0x800

#define	VSW_DRING_INFO_SENT	0x1000	/* Dring info sent to peer */
#define	VSW_DRING_INFO_RECV	0x2000	/* Dring info received */
#define	VSW_DRING_ACK_SENT	0x4000
#define	VSW_DRING_ACK_RECV	0x8000
#define	VSW_DRING_NACK_SENT	0x10000
#define	VSW_DRING_NACK_RECV	0x20000

#define	VSW_RDX_INFO_SENT	0x40000	/* RDX sent to peer */
#define	VSW_RDX_INFO_RECV	0x80000	/* RDX received from peer */
#define	VSW_RDX_ACK_SENT	0x100000
#define	VSW_RDX_ACK_RECV	0x200000
#define	VSW_RDX_NACK_SENT	0x400000
#define	VSW_RDX_NACK_RECV	0x800000

#define	VSW_MCST_INFO_SENT	0x1000000
#define	VSW_MCST_INFO_RECV	0x2000000
#define	VSW_MCST_ACK_SENT	0x4000000
#define	VSW_MCST_ACK_RECV	0x8000000
#define	VSW_MCST_NACK_SENT	0x10000000
#define	VSW_MCST_NACK_RECV	0x20000000

#define	VSW_LANE_ACTIVE		0x40000000	/* Lane open to xmit data */

/* Handshake milestones */
#define	VSW_MILESTONE0		0x1	/* ver info exchanged */
#define	VSW_MILESTONE1		0x2	/* attribute exchanged */
#define	VSW_MILESTONE2		0x4	/* dring info exchanged */
#define	VSW_MILESTONE3		0x8	/* rdx exchanged */
#define	VSW_MILESTONE4		0x10	/* handshake complete */

/*
 * Lane direction (relative to ourselves).
 */
#define	INBOUND			0x1
#define	OUTBOUND		0x2

/* Peer session id received */
#define	VSW_PEER_SESSION	0x1

/*
 * Maximum number of consecutive reads of data from channel
 */
#define	VSW_MAX_CHAN_READ	50

/*
 * Currently only support one ldc per port.
 */
#define	VSW_PORT_MAX_LDCS	1	/* max # of ldcs per port */

/*
 * Used for port add/deletion.
 */
#define	VSW_PORT_UPDATED	0x1

#define	LDC_TX_SUCCESS		0	/* ldc transmit success */
#define	LDC_TX_FAILURE		1	/* ldc transmit failure */
#define	LDC_TX_NORESOURCES	2	/* out of descriptors */

/*
 * Descriptor ring info
 *
 * Each descriptor element has a pre-allocated data buffer
 * associated with it, into which data being transmitted is
 * copied. By pre-allocating we speed up the copying process.
 * The buffer is re-used once the peer has indicated that it is
 * finished with the descriptor.
 */
#define	VSW_RING_EL_DATA_SZ	2048	/* Size of data section (bytes) */
#define	VSW_PRIV_SIZE	sizeof (vnet_private_desc_t)

#define	VSW_MAX_COOKIES		((ETHERMTU >> MMU_PAGESHIFT) + 2)

/*
 * Size of the mblk in each mblk pool.
 */
#define	VSW_MBLK_SZ_128		128
#define	VSW_MBLK_SZ_256		256
#define	VSW_MBLK_SZ_2048	2048

/*
 * Number of mblks in each mblk pool.
 */
#define	VSW_NUM_MBLKS	1024

/*
 * Number of rcv buffers in RxDringData mode
 */
#define	VSW_RXDRING_NRBUFS	(vsw_num_descriptors * vsw_nrbufs_factor)

/* increment recv index */
#define	INCR_DESC_INDEX(dp, i)	\
		((i) = (((i) + 1) & ((dp)->num_descriptors - 1)))

/* decrement recv index */
#define	DECR_DESC_INDEX(dp, i)	\
		((i) = (((i) - 1) & ((dp)->num_descriptors - 1)))

#define	INCR_TXI	INCR_DESC_INDEX
#define	DECR_TXI	DECR_DESC_INDEX
#define	INCR_RXI	INCR_DESC_INDEX
#define	DECR_RXI	DECR_DESC_INDEX

/* bounds check rx index */
#define	CHECK_DESC_INDEX(dp, i)	\
		(((i) >= 0) && ((i) < (dp)->num_descriptors))

#define	CHECK_RXI	CHECK_DESC_INDEX
#define	CHECK_TXI	CHECK_DESC_INDEX

/*
 * Private descriptor
 */
typedef struct vsw_private_desc {
	/*
	 * Below lock must be held when accessing the state of
	 * a descriptor on either the private or public sections
	 * of the ring.
	 */
	kmutex_t		dstate_lock;
	uint64_t		dstate;
	vnet_public_desc_t	*descp;
	ldc_mem_handle_t	memhandle;
	void			*datap;
	uint64_t		datalen;
	uint64_t		ncookies;
	ldc_mem_cookie_t	memcookie[VSW_MAX_COOKIES];
	int			bound;
} vsw_private_desc_t;

/*
 * Descriptor ring structure
 */
typedef struct dring_info {
	kmutex_t		dlock;		/* sync access */
	uint32_t		num_descriptors; /* # of descriptors */
	uint32_t		descriptor_size; /* size of descriptor */
	uint32_t		options;	/* dring options (mode) */
	ldc_dring_handle_t	dring_handle;	/* dring LDC handle */
	uint32_t		dring_ncookies;	/* # of dring cookies */
	ldc_mem_cookie_t	dring_cookie[1]; /* LDC cookie of dring */
	ldc_mem_handle_t	data_handle;	/* data area  LDC handle */
	uint32_t		data_ncookies;	/* # of data area cookies */
	ldc_mem_cookie_t	*data_cookie;	/* data area LDC cookies */
	uint64_t		ident;		/* identifier sent to peer */
	uint64_t		end_idx;	/* last idx processed */
	int64_t			last_ack_recv;	/* last ack received */
	kmutex_t		txlock;		/* protect tx desc alloc */
	uint32_t		next_txi;	/* next tx descriptor index */
	uint32_t		next_rxi;	/* next expected recv index */
	kmutex_t		restart_lock;	/* protect restart_reqd */
	boolean_t		restart_reqd;	/* send restart msg */
	uint32_t		restart_peer_txi; /* index to restart peer */
	void			*pub_addr;	/* base of public section */
	void			*priv_addr;	/* base of private section */
	void			*data_addr;	/* base of data section */
	size_t			data_sz;	/* size of data section */
	size_t			desc_data_sz;	/* size of descr data blk */
	uint8_t			dring_mtype;	/* dring mem map type */
	uint32_t		num_bufs;	/* # of buffers */
	vio_mblk_pool_t		*rx_vmp;	/* rx mblk pool */
	vio_mblk_t		**rxdp_to_vmp;	/* descr to buf map tbl */
} dring_info_t;

/*
 * Each ldc connection is comprised of two lanes, incoming
 * from a peer, and outgoing to that peer. Each lane shares
 * common ldc parameters and also has private lane-specific
 * parameters.
 */
typedef struct lane {
	uint64_t	lstate;		/* Lane state */
	uint16_t	ver_major;	/* Version major number */
	uint16_t	ver_minor;	/* Version minor number */
	uint64_t	seq_num;	/* Sequence number */
	uint64_t	mtu;		/* ETHERMTU */
	uint64_t	addr;		/* Unique physical address */
	uint8_t		addr_type;	/* Only MAC address at moment */
	uint8_t		xfer_mode;	/* Dring or Pkt based */
	uint8_t		ack_freq;	/* Only non zero for Pkt based xfer */
	uint32_t	physlink_update;	/* physlink updates */
	uint8_t		dring_mode;	/* Descriptor ring mode */
	dring_info_t	*dringp;	/* List of drings for this lane */
} lane_t;

/* channel drain states */
#define	VSW_LDC_INIT		0x1	/* Initial non-drain state */
#define	VSW_LDC_DRAINING	0x2	/* Channel draining */

/*
 * vnet-protocol-version dependent function prototypes.
 */
typedef int	(*vsw_ldctx_t) (void *, mblk_t *, mblk_t *, uint32_t);
typedef void	(*vsw_ldcrx_pktdata_t) (void *, void *, uint32_t);
typedef void	(*vsw_ldcrx_dringdata_t) (void *, void *);

/* ldc information associated with a vsw-port */
typedef struct vsw_ldc {
	struct vsw_ldc		*ldc_next;	/* next ldc in the list */
	struct vsw_port		*ldc_port;	/* associated port */
	struct vsw		*ldc_vswp;	/* associated vsw */
	kmutex_t		ldc_cblock;	/* sync callback processing */
	kmutex_t		ldc_txlock;	/* sync transmits */
	kmutex_t		ldc_rxlock;	/* sync rx */
	uint64_t		ldc_id;		/* channel number */
	ldc_handle_t		ldc_handle;	/* channel handle */
	kmutex_t		drain_cv_lock;
	kcondvar_t		drain_cv;	/* channel draining */
	int			drain_state;
	uint32_t		hphase;		/* handshake phase */
	int			hcnt;		/* # handshake attempts */
	kmutex_t		status_lock;
	ldc_status_t		ldc_status;	/* channel status */
	uint8_t			reset_active;	/* reset flag */
	uint64_t		local_session;	/* Our session id */
	uint64_t		peer_session;	/* Our peers session id */
	uint8_t			session_status;	/* Session recv'd, sent */
	uint32_t		hss_id;		/* Handshake session id */
	uint64_t		next_ident;	/* Next dring ident # to use */
	lane_t			lane_in;	/* Inbound lane */
	lane_t			lane_out;	/* Outbound lane */
	uint8_t			dev_class;	/* Peer device class */
	boolean_t		pls_negotiated;	/* phys link state update ? */
	vio_multi_pool_t	vmp;		/* Receive mblk pools */
	uint32_t		max_rxpool_size; /* max size of rxpool in use */
	uint64_t		*ldcmsg;	/* msg buffer for ldc_read() */
	uint64_t		msglen;		/* size of ldcmsg */
	uint32_t		dringdata_msgid; /* msgid in RxDringData mode */

	/* tx thread fields */
	kthread_t		*tx_thread;	/* tx thread */
	uint32_t		tx_thr_flags;	/* tx thread flags */
	kmutex_t		tx_thr_lock;	/* lock for tx thread */
	kcondvar_t		tx_thr_cv;	/* cond.var for tx thread */
	mblk_t			*tx_mhead;	/* tx mblks head */
	mblk_t			*tx_mtail;	/* tx mblks tail */
	uint32_t		tx_cnt;		/* # of pkts queued for tx */

	/* message thread fields */
	kthread_t		*msg_thread;	/* message thread */
	uint32_t		msg_thr_flags;	/* message thread flags */
	kmutex_t		msg_thr_lock;	/* lock for message thread */
	kcondvar_t		msg_thr_cv;	/* cond.var for msg thread */

	/* receive thread fields */
	kthread_t		*rcv_thread;	/* receive thread */
	uint32_t		rcv_thr_flags;	/* receive thread flags */
	kmutex_t		rcv_thr_lock;	/* lock for receive thread */
	kcondvar_t		rcv_thr_cv;	/* cond.var for recv thread */

	vsw_ldctx_t		tx;		/* transmit function */
	vsw_ldcrx_pktdata_t	rx_pktdata;	/* process raw data msg */
	vsw_ldcrx_dringdata_t	rx_dringdata;	/* process dring data msg */

	/* channel statistics */
	vgen_stats_t		ldc_stats;	/* channel statistics */
	kstat_t			*ksp;		/* channel kstats */
} vsw_ldc_t;

/* worker thread flags */
#define	VSW_WTHR_DATARCVD 	0x01	/* data received */
#define	VSW_WTHR_STOP 		0x02	/* stop worker thread request */

/* multicast addresses port is interested in */
typedef struct mcst_addr {
	struct mcst_addr	*nextp;
	struct ether_addr	mca;	/* multicast address */
	uint64_t		addr;	/* mcast addr converted to hash key */
	boolean_t		mac_added; /* added into physical device */
} mcst_addr_t;

/* Port detach states */
#define	VSW_PORT_INIT		0x1	/* Initial non-detach state */
#define	VSW_PORT_DETACHING	0x2	/* In process of being detached */
#define	VSW_PORT_DETACHABLE	0x4	/* Safe to detach */

/* port information associated with a vsw */
typedef struct vsw_port {
	int			p_instance;	/* port instance */
	struct vsw_port		*p_next;	/* next port in the list */
	struct vsw		*p_vswp;	/* associated vsw */
	int			num_ldcs;	/* # of ldcs in the port */
	uint64_t		*ldc_ids;	/* ldc ids */
	vsw_ldc_t		*ldcp;		/* ldc for this port */

	kmutex_t		tx_lock;	/* transmit lock */
	int			(*transmit)(vsw_ldc_t *, mblk_t *);

	int			state;		/* port state */
	kmutex_t		state_lock;
	kcondvar_t		state_cv;

	krwlock_t		maccl_rwlock;	/* protect fields below */
	mac_client_handle_t	p_mch;		/* mac client handle */
	mac_unicast_handle_t	p_muh;		/* mac unicast handle */

	kmutex_t		mca_lock;	/* multicast lock */
	mcst_addr_t		*mcap;		/* list of multicast addrs */

	boolean_t		addr_set;	/* Addr set where */

	/*
	 * mac address of the port & connected device
	 */
	struct ether_addr	p_macaddr;
	uint16_t		pvid;	/* port vlan id (untagged) */
	struct vsw_vlanid	*vids;	/* vlan ids (tagged) */
	uint16_t		nvids;	/* # of vids */
	mod_hash_t		*vlan_hashp;	/* vlan hash table */
	uint32_t		vlan_nchains;	/* # of vlan hash chains */

	/* HybridIO related info */
	uint32_t		p_hio_enabled;	/* Hybrid mode enabled? */
	uint32_t		p_hio_capable;	/* Port capable of HIO */

	/* bandwidth limit */
	uint64_t		p_bandwidth;	/* bandwidth limit */
} vsw_port_t;

/* list of ports per vsw */
typedef struct vsw_port_list {
	vsw_port_t	*head;		/* head of the list */
	krwlock_t	lockrw;		/* sync access(rw) to the list */
	int		num_ports;	/* number of ports in the list */
} vsw_port_list_t;

/*
 * Taskq control message
 */
typedef struct vsw_ctrl_task {
	vsw_ldc_t	*ldcp;
	def_msg_t	pktp;
	uint32_t	hss_id;
} vsw_ctrl_task_t;

/*
 * State of connection to peer. Some of these states
 * can be mapped to LDC events as follows:
 *
 * VSW_CONN_RESET -> LDC_RESET_EVT
 * VSW_CONN_UP    -> LDC_UP_EVT
 */
#define	VSW_CONN_UP		0x1	/* Connection come up */
#define	VSW_CONN_RESET		0x2	/* Connection reset */
#define	VSW_CONN_RESTART	0x4	/* Restarting handshake on connection */

typedef struct vsw_conn_evt {
	uint16_t	evt;		/* Connection event */
	vsw_ldc_t	*ldcp;
} vsw_conn_evt_t;

/*
 * Ethernet broadcast address definition.
 */
static	struct	ether_addr	etherbroadcastaddr = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

#define	IS_BROADCAST(ehp) \
	(bcmp(&ehp->ether_dhost, &etherbroadcastaddr, ETHERADDRL) == 0)
#define	IS_MULTICAST(ehp) \
	((ehp->ether_dhost.ether_addr_octet[0] & 01) == 1)

#define	READ_ENTER(x)	rw_enter(x, RW_READER)
#define	WRITE_ENTER(x)	rw_enter(x, RW_WRITER)
#define	RW_EXIT(x)	rw_exit(x)

#define	VSW_PORT_REFHOLD(portp)	atomic_inc_32(&((portp)->ref_cnt))
#define	VSW_PORT_REFRELE(portp)	atomic_dec_32(&((portp)->ref_cnt))

#ifdef	__cplusplus
}
#endif

#endif	/* _VSW_LDC_H */

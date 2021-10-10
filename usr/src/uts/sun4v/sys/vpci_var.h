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

#ifndef	_VPCI_VAR_H
#define	_VPCI_VAR_H

#include <sys/modhash.h>
#include <sys/vpci_vio.h>

/* Golbal varibables */

extern int vpci_msglevel;
extern uint64_t vpci_matchinst;

/*
 * Bit-field values to indicate if vpci driver attach initialization progress
 */
#define	VPCI_SOFT_STATE	0x0001
#define	VPCI_LOCKS	0x0002
#define	VPCI_MDEG	0x0004

#define	VPCI_NCHAINS		32
#define	VPCI_LDC_MTU		256
#define	VPCI_TASKQ_NAMELEN	256
#define	VPCI_SET_MDEG_PROP_INST(specp, val) (specp)[1].ps_val = (val);

/*
 * Definitions of MD nodes/properties.
 */
#define	VPCI_MD_CHAN_NAME		"channel-endpoint"
#define	VPCI_MD_VDEV_NAME		"virtual-device"
#define	VPCI_MD_PORT_NAME		"virtual-device-port"
#define	VPCI_MD_PCIV_VDEV_NAME		"pciv-communication"
#define	VPCI_MD_PCIV_PORT_NAME		"pciv-comm-port"
#define	VPCI_MD_CFG_HDL			"cfg-handle"
#define	VPCI_MD_ID			"id"
#define	VPCI_MD_IOV_DEV_NAME		"iov-device"
#define	VPCI_MD_IOV_DEV_PATH		"path"
#define	VPCI_MD_IOV_DEV_NUMVFS		"numvfs"
#define	VPCI_MD_IOV_DEV_VFID		"vf-id"

/*
 * The MD parsing action.
 */
typedef enum vpci_md_action {
	VPCI_MD_ADD = 0x1,		/* Add new MD nodes */
	VPCI_MD_CHANGE_CHK = 0x2	/* Check updated MD nodes */
} vpci_md_action_t;

/*
 * Debugging macros
 */
#ifdef DEBUG

#define	DMSG(_vpci, err_level, format, ...)				\
	do {								\
		if (vpci_msglevel > err_level &&			\
		(vpci_matchinst & (1ull << (_vpci)->instance)))		\
			cmn_err(CE_CONT, "?[%d,t@%p] %s: "format,	\
			(_vpci)->instance, (void *)curthread,		\
			__func__, __VA_ARGS__);				\
		_NOTE(CONSTANTCONDITION)				\
	} while (0);

#define	DMSGX(err_level, format, ...)					\
	do {								\
		if (vpci_msglevel > err_level)				\
			cmn_err(CE_CONT, "?%s: "format, __func__, __VA_ARGS__);\
		_NOTE(CONSTANTCONDITION)				\
	} while (0);

#define	VPCI_DUMP_DRING_MSG(dmsgp)					\
		DMSGX(0, "sq:%lu start:%d end:%d ident:%lu\n",		\
			dmsgp->seq_num, dmsgp->start_idx,		\
			dmsgp->end_idx, dmsgp->dring_ident);
#else	/* !DEBUG */

#define	DMSG(err_level, ...)
#define	DMSGX(err_level, format, ...)
#define	VPCI_DUMP_DRING_MSG

#endif

/*
 * Soft state of vpci driver instance
 */
typedef struct vpci {
	dev_info_t		*dip;
	int			instance;
	int			init_progress;
	mdeg_node_spec_t	*ispecp;	/* mdeg node specification */
	mdeg_handle_t		mdeg;		/* handle for MDEG operations */

	mod_hash_t		*vport_tbl;	/* table of vport served */
} vpci_t;

/*
 * Tx request definitions
 *
 * When vpci driver gets a PCIV packet from common code layer, it will populate
 * a tx request by using LDC memory bind interfaces. All of information in tx
 * request is platform specific data, which is only can be understood and used
 * in vritual proxy driver layer. Part of tx request information could be
 * filled on tx descriptors, and receive side gets them from rx descriptors.
 * Then all these information will be used for rx req and rx PCIV packet
 * population work.
 */
typedef struct tx_req {
	struct tx_req		*next;
	caddr_t			align_addr;
	ldc_mem_handle_t	ldc_mhdl;
	ldc_mem_cookie_t	ldc_cookie[VPCI_MAX_COOKIES];
	uint32_t		ncookie;
	pciv_pkt_t		*pkt;
} vpci_tx_req_t;

#define	PKT2TXREQ(pkt)	((vpci_tx_req_t *)((pkt)->priv_data))

/*
 * Rx request definitions
 *
 * A rx request contains all of platform specific information which is used
 * to populated a PCIV packet in local Domain. The priv_data field of PCIV
 * packet points its corresponding rx request, but this information is never
 * exposed to common code layer. When a PCIV packet io callback is called, the
 * rx request existing in the packet will be used to ACK the rx descriptors.
 */
typedef struct rx_req {
	int			idx;
	int			next;
	uint64_t		id;
	ldc_mem_handle_t	ldc_mhdl;
	ldc_mem_cookie_t	ldc_cookie[VPCI_MAX_COOKIES];
	uint32_t		ncookie;
	size_t			nbytes;
	vio_dring_msg_t		*dring_msg;
	pciv_pkt_t		*pkt;
} vpci_rx_req_t;

#define	PKT2RXREQ(pkt)	((vpci_rx_req_t *)((pkt)->priv_data))

/*
 * Dring and dring entry operations
 */

#define	VPCI_GET_DRING_ENTRY_PTR(ring)	\
	    ((vpci_dring_entry_t *)(uintptr_t)((ring)->dring->mem_info.vaddr + \
	    ((ring)->ident/2 * (ring)->nentry * (ring)->dring->entrysize)))

#define	VPCI_MARK_DRING_ENTRY_FREE(dring, idx)	\
	do {						\
		vpci_dring_entry_t *dep = NULL;		\
		ASSERT(dring != NULL);			\
		ASSERT(idx < dring->nentry);		\
		ASSERT(dring->mem_info.vaddr != NULL);	\
		dep = (vpci_dring_entry_t *)(uintptr_t)	\
			(dring->mem_info.vaddr +	\
			(idx * dring->entrysize));	\
		ASSERT(dep != NULL);			\
		dep->hdr.dstate = VIO_DESC_FREE;	\
		_NOTE(CONSTANTCONDITION)		\
	} while (0);

#define	VPCI_RING_SIZE(ring)	ring->nentry

#define	VPCI_GET_RING_LOCAL_IDX(ring, idx)	\
	((idx) & (VPCI_RING_SIZE(ring) - 1))

#define	VPCI_GET_RING_GLOBAL_IDX(ring, idx)	\
	((ring)->ident/2 * (ring)->nentry + VPCI_GET_RING_LOCAL_IDX(ring, idx))

#define	VPCI_GET_RING_ENTRY(ring, idx)	\
	((ring)->dep + VPCI_GET_RING_LOCAL_IDX(ring, idx))

#define	VPCI_TX_RING_AVAIL_SLOTS(tx_ring)	\
	(VPCI_RING_SIZE(tx_ring) - ((tx_ring)->req_prod - (tx_ring)->rsp_cons))

#define	VPCI_RX_RING_HAS_UNCONSUM_REQ(rx_ring)	\
	(((rx_ring)->req_cons != (rx_ring)->req_peer_prod) ? B_TRUE : B_FALSE)

#define	VPCI_TX_RING_HAS_UNCONSUM_RSP(tx_ring)	\
	(((tx_ring)->rsp_cons != (tx_ring)->rsp_peer_prod) ? B_TRUE : B_FALSE)

#define	VPCI_TX_RING_HAS_INCOMP_REQ(tx_ring)	\
	(((tx_ring)->req_prod != (tx_ring)->rsp_peer_prod) ? B_TRUE : B_FALSE)

#define	VPCI_DRING_IS_INIT(ring)	\
	((ring->dring->hdl != NULL) ? B_TRUE : B_FALSE)

#define	VPCI_DUMP_DRING_ENTRY(vpci, entry)		\
	DMSG(vpci, 0, "[%d]dst:%x id:%"PRIu64"\n",	\
	    vpci->instance,				\
	    entry->hdr.dstate,				\
	    entry->payload.id);

/* Initialise the Session ID and Sequence Num in the DRing msg */
#define	VPCI_INIT_DRING_DATA_MSG_IDS(dmsg, vport)	\
	do {						\
		ASSERT(vport != NULL);			\
		dmsg.tag.vio_sid = vport->sid;		\
		dmsg.seq_num = vport->local_seq;	\
		_NOTE(CONSTANTCONDITION)		\
	} while (0);

/*
 * Even number indicates peer side is the TX ring and local side should
 * get a RX dring msg.
 */
#define	VPCI_IS_RX_DRING_MSG(dmsg)	\
	((dmsg->dring_ident % 2 == 0) ? B_TRUE : B_FALSE)
/*
 * Dring information for dring register
 */
typedef struct vpci_dring {
	uint64_t		ident;		/* identifier of dring */
	uint32_t		entrysize;	/* size of rx desc */
	uint32_t		nentry;		/* number of rx desc */
	ldc_dring_handle_t	hdl;		/* dring handle */
	ldc_mem_cookie_t	*cookie;	/* dring cookies */
	uint32_t		ncookie;	/* num cookies */
	ldc_mem_info_t		mem_info;	/* dring information */
} vpci_dring_t;

/*
 * Software rx and tx rings definitions
 */
typedef struct vpci_rx_ring {
	kmutex_t		rx_lock;	/* rx lock */
	uint64_t		ident;		/* identifier of rx_ring */
	uint32_t		nentry;		/* number of rx desc */
	uint32_t		req_cons;	/* idx for req consume */
	uint32_t		rsp_prod;	/* idx for rsp prod */
	uint32_t		req_peer_prod;	/* idx for peer side req prod */
	vpci_dring_entry_t	*dep;		/* Dring entry pointer */
	vpci_rx_req_t		*req;		/* rx request */
	int			latest_req;	/* idx of next to use req */
	int			free_req;	/* idx of free req */
	pciv_pkt_t		*pkt_chain;	/* pciv packet chain */
	struct vpci_dring	*dring;		/* VIO dring */
	struct vpci_port	*vport;		/* pointer to vport */
} vpci_rx_ring_t;

typedef struct vpci_tx_ring {
	kmutex_t		tx_lock;	/* tx lock */
	uint64_t		ident;		/* identifier of tx_ring */
	uint32_t		nentry;		/* number of tx desc */
	uint32_t		req_prod;	/* req prod idx */
	uint32_t		rsp_cons;	/* rsp consume idx */
	uint32_t		rsp_peer_prod;	/* peer side rsp prod idx */
	uint32_t		req_send;	/* req send idx */
	vpci_dring_entry_t	*dep;		/* Dring entry pointer */
	vpci_tx_req_t		*req_head;	/* head of pending tx reqs */
	vpci_tx_req_t		*req_tail;	/* tail of pending tx reqs */
	clock_t			last_access;	/* timestamp */
	struct vpci_dring	*dring;		/* VIO dring */
	struct vpci_port	*vport;		/* pointer to vport */
} vpci_tx_ring_t;

#define	VPCI_HSHAKE_MAX_RETRIES	3

/*
 * Per vritual pci LDC channel states
 */
#define	VPCI_LDC_INIT	0x0001
#define	VPCI_LDC_CB	0x0002
#define	VPCI_LDC_OPEN	0x0004
#define	VPCI_LDC	(VPCI_LDC_INIT | VPCI_LDC_CB | VPCI_LDC_OPEN)

/*
 * The states that the read thread can be in.
 */
typedef enum vpci_read_state {
	VPCI_READ_IDLE = 0x1,		/* idling - conn is not up */
	VPCI_READ_WAITING,		/* waiting for data */
	VPCI_READ_PENDING,		/* pending data avail for read */
	VPCI_READ_RESET			/* channel was reset - stop reads */
} vpci_read_state_t;

/*
 * Definition of the various states the vpci state machine can be
 * in during the handshake between negotiation client and server.
 */
typedef enum vpci_hshake_state {
	VPCI_NEGOTIATE_INIT = 0x0,
	VPCI_NEGOTIATE_VER,
	VPCI_NEGOTIATE_ATTR,
	VPCI_NEGOTIATE_DRING,
	VPCI_NEGOTIATE_RDX,
	VPCI_NEGOTIATE_FINI
} vpci_hshake_state_t;

/*
 * Virtual pci port definitions
 */
typedef struct vpci_port {
	uint64_t		id;		/* port id */
	dom_id_t		domain_id;	/* Domain id used by pcie */
	pciv_handle_t		pciv_handle;	/* pciv handle */
	uint8_t			dev_class;
	ddi_taskq_t		*reset_taskq;	/* taskq for resetting LDC */
	kthread_t		*msg_thr;	/* msg processing thread */

	/* Used for hand shake process */
	krwlock_t		hshake_lock;	/* protects hshake states */
	boolean_t		reset_req;	/* reset request */
	boolean_t		reset_ldc;	/* need reset LDC */
	kmutex_t		io_lock;	/* pending io lock */
	kcondvar_t		io_cv;		/* signal for pending cv */
	int			hshake_cnt;
	vio_ver_t		hshake_ver;
	timeout_id_t		hshake_tid;	/* handshake timer id */
	vpci_hshake_state_t	hshake_state;	/* current handshake state */
	boolean_t		hshake_disable;	/* disable handshake */

	uint64_t		ldc_id;		/* LDC id */
	int			ldc_init_flag;	/* LDC init progress */
	ldc_handle_t		ldc_handle;	/* LDC handle */
	ldc_status_t		ldc_state;	/* LDC state */

	kmutex_t		read_lock;	/* reading lock */
	kcondvar_t		read_cv;	/* signal when ldc conn is up */
	vpci_read_state_t	read_state;	/* read state */
	uint64_t		sid;		/* Session ID */
	uint64_t		local_seq;	/* Latest tx sequence num */
	uint64_t		local_seq_ack;  /* TX seq num ACK/NACK'ed */
	uint64_t		peer_seq;	/* Rx seq num ACK/NACK'ed */


	vpci_dring_t		tx_dring;	/* VIO tx dring */
	vpci_tx_ring_t		*tx_rings;	/* Tx rings */
	vpci_dring_t		rx_dring;	/* VIO rx dring */
	vpci_rx_ring_t		*rx_rings;	/* Rx rings */
	int			num_rx_rings;	/* number of rx rings */
	int			num_tx_rings;	/* number of tx rings */
	timeout_id_t		tx_tid;		/* tx watchdog timer */
	uint64_t		tx_reset_cnt;	/* tx reset counter */
	uint64_t		max_xfer_sz;	/* Max transfer sz */

	vpci_t			*vpci;		/* Pointer to soft state */
} vpci_port_t;

/*
 * Prototypes in vpci_ldc.c
 */

/* Pointer of LDC interrupt callback */
typedef uint_t (*vpci_ldc_cb_t)(uint64_t event, caddr_t arg);
int vpci_do_ldc_init(vpci_port_t *vport, vpci_ldc_cb_t vpci_ldc_cb);
int vpci_get_ldc_id(md_t *md, mde_cookie_t vpci_node, uint64_t *ldc_id);
dom_id_t vpci_get_domain_id(uint64_t vport_id);
int vpci_parse_dev_md(md_t *md, mde_cookie_t vpci_node, uint64_t vport_id,
    vpci_md_action_t action);
int vpci_do_ldc_up(vpci_port_t *vport);
int vpci_do_ldc_down(vpci_port_t *vport);
int vpci_ldc_cb_disable(vpci_port_t *vport);
int vpci_ldc_cb_enable(vpci_port_t *vport);
int vpci_get_ldc_status(vpci_port_t *vport, ldc_status_t *ldc_state);
void vpci_terminate_ldc(vpci_port_t *vport);
int vpci_send(vpci_port_t *vport, caddr_t pkt, size_t msglen);
void vpci_request_reset(vpci_port_t *vport, boolean_t reset_ldc);
void vpci_reset_session(vpci_port_t *vport);
int vpci_hshake_check(vpci_port_t *vport);
boolean_t vpci_next_hshake_state(vpci_port_t *vport);
int vpci_hshake(vpci_port_t *vport);
void vpci_ack_msg(vpci_port_t *vport, vio_msg_t *msg, boolean_t ack);
void vpci_recv_msgs(vpci_port_t *vport);

/*
 * Prototypes in vpci_rings.c
 */

int vpci_alloc_rings(vpci_port_t *vport);
void vpci_free_rings(vpci_port_t *vport);
int vpci_init_tx_rings(vpci_port_t *vport);
int vpci_init_rx_rings(vpci_port_t *vport);
void vpci_fini_tx_rings(vpci_port_t *vport);
void vpci_fini_rx_rings(vpci_port_t *vport);
vpci_tx_ring_t *vpci_tx_ring_get(vpci_port_t *vport, pciv_pkt_t *pkt);
#ifndef DEBUG
#pragma inline(vpci_tx_ring_get)
#endif
void vpci_ring_tx(vpci_tx_ring_t *tx_ring, pciv_pkt_t *pkt);
vpci_tx_ring_t *vpci_tx_ring_get_by_ident(vpci_port_t *vport,
    uint8_t ident);
vpci_rx_ring_t *vpci_rx_ring_get_by_ident(vpci_port_t *vport,
    uint8_t ident);
vpci_dring_entry_t *vpci_rx_ring_get_req_entry(vpci_rx_ring_t *rx_ring);
vpci_dring_entry_t *vpci_rx_ring_get_rsp_entry(vpci_rx_ring_t *rx_ring);
vpci_dring_entry_t *vpci_tx_ring_get_rsp_entry(vpci_tx_ring_t *tx_ring);
vpci_dring_entry_t *vpci_tx_ring_get_req_entry(vpci_tx_ring_t *tx_ring);
int vpci_rx_ring_acquire(vpci_rx_ring_t *rx_ring, on_trap_data_t *otd,
    uint32_t start_idx, uint32_t end_idx);
int vpci_rx_ring_release(vpci_rx_ring_t *rx_ring, uint32_t start_idx,
    uint32_t end_idx);
uint32_t vpci_tx_ring_avail_slots(vpci_tx_ring_t *tx_ring);

/* Prototypes in vpci_rx.c */

int vpci_dring_msg_rx(vpci_rx_ring_t *rx_ring, vio_dring_msg_t *dring_msg);

/* Prototypes in vpci_tx.c */

void vpci_drain_fini_tx_rings(vpci_port_t *vport);
int vpci_dring_msg_tx(vpci_tx_ring_t *tx_ring, vio_dring_msg_t *dring_msg);

void vpci_tx_req_queue_clean(vpci_port_t *vport);
void vpci_tx_watchdog_timer(vpci_port_t *vport);

#endif	/* _VPCI_VAR_H */

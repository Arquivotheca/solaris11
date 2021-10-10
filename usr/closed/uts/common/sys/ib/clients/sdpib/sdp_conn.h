/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * LEGAL NOTICE
 *
 * This file contains source code that implements the Sockets Direct
 * Protocol (SDP) as defined by the InfiniBand Architecture Specification,
 * Volume 1, Annex A4, Version 1.1.  Due to restrictions in the SDP license,
 * source code contained in this file may not be distributed outside of
 * Sun Microsystems without further legal review to ensure compliance with
 * the license terms.
 *
 * Sun employees and contactors are cautioned not to extract source code
 * from this file and use it for other purposes.  The SDP implementation
 * code in this and other files must be kept separate from all other source
 * code.
 *
 * As required by the license, the following notice is added to the source
 * code:
 *
 * This source code may incorporate intellectual property owned by
 * Microsoft Corporation.  Our provision of this source code does not
 * include any licenses or any other rights to you under any Microsoft
 * intellectual property.  If you would like a license from Microsoft
 * (e.g., to rebrand, redistribute), you need to contact Microsoft
 * directly.
 */

/*
 * Sun elects to include this software in this distribution under the
 * OpenIB.org BSD license
 *
 *
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _SYS_IB_CLIENTS_SDP_CONN_H
#define	_SYS_IB_CLIENTS_SDP_CONN_H


#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/socket_proto.h>

/*
 * SDP connection specific definitions.
 */

#define	BIND_HDL_ARR_MAX_ENTRIES	48
#define	SDP_CONN_SEND_POST_NAME "tx_sdp_post"	/* name for pool */
#define	SDP_CONN_RECV_POST_NAME "rx_sdp_post"	/* name for pool */
#define	SDP_CONN_SEND_DATA_NAME "tx_sdp_data"	/* name for pool */
#define	SDP_CONN_SEND_CTRL_NAME "tx_sdp_ctrl"	/* name for pool */
#define	SDP_CONN_RDMA_READ_NAME "rx_sdp_rdma"	/* name for pool */
#define	SDP_CONN_RDMA_SEND_NAME "tx_sdp_rdma"	/* name for pool */
#define	SDP_SOCK_RECV_DATA_NAME "rx_lnx_data"
#define	SDP_INET_PEEK_DATA_NAME "rx_lnx_peek"

#define	SDP_CONN_F_SRC_CANCEL_L 0x01 /* source cancel was issued */
#define	SDP_CONN_F_SRC_CANCEL_R 0x02 /* source cancel was received */
#define	SDP_CONN_F_SRC_CANCEL_C 0x04 /* source data cancelled */
#define	SDP_CONN_F_SNK_CANCEL   0x08 /* sink cancel issued */
#define	SDP_CONN_F_DIS_PEND	0x10 /* disconnect pending */
#define	SDP_CONN_F_OOB_SEND	0x20 /* OOB notification pending. */
#define	SDP_CONN_F_DEAD		0xFF /* connection has been deleted */

/*
 * Size of completion array to be filled by a single poll call.
 */
#define	SDP_MAX_CQ_WC	16

#ifdef _KERNEL

/*
 * SDP flow control flags
 */
#define	SDP_WRFULL 0x0001

#define	SDP_RECV_OOB_PEND 0x1
#define	SDP_RECV_OOB_PRESENT 0x2
#define	SDP_RECV_OOB_ATMARK 0x4
#define	SDP_RECV_OOB_CONSUMED 0x8
/*
 * SDP connection structure.
 */
struct sdp_conn_struct_t {
	/*
	 * Note: If the order of following fields is changed please change
	 * sdp_ext_conn_t as well.
	 */
	uint16_t state;		/* connection state */
	uint8_t	 sdp_family;	/* IPV4 or IPV6 specified by the application */
	in6_addr_t saddr;
	in6_addr_t faddr;
#define	conn_src	V4_PART_OF_V6(saddr)
#define	conn_rem	V4_PART_OF_V6(faddr)
#define	conn_srcv6	saddr
#define	conn_remv6	faddr
	uint16_t src_port;		/* sdp port on the stream interface */
	uint16_t dst_port;		/* sdp port of the remote SDP client */

	zoneid_t sdp_zoneid;
	netstack_t *sdp_stack;
#define	sdp_sdps	sdp_stack->netstack_sdp

	int32_t sdp_hashent;	 /* connection ID/hash entry */
	kmutex_t sdp_reflock;
	int32_t sdp_ib_refcnt; /* connection reference count. */
	int32_t sdp_inet_refcnt; /* connection ref count held by socket */

	boolean_t sdp_priv_stream;	/* if stream was opened by */
	uint16_t sdp_bound_state;
	uint8_t sdp_ipversion;	/* IPV4 or IPV6 on the wire */
	uint8_t sdp_family_used;	/* family used for opening the socket */

	dev_t dip;	/* device pointer */
	minor_t instance_num;	/* minor device we created by opening */
				/* the stream */

	/*
	 * kmem_cache is inherited from HCA
	 */
	kmem_cache_t *sdp_conn_buff_cache;
	ibt_channel_hdl_t channel_hdl;	/* channel hdl that we'll be using */
					/* for conn */

	/*
	 * |<----- Avail Space -------->|
	 * |<------------------------sdpc_tx_max_queue_size---------------->|
	 * |============================|<-----sdpc_tx_bytes_queued-------->|
	 * 				|<--unposted-->|<---posted on HW--->|
	 */
	uint32_t sdpc_tx_max_queue_size; /* maximum bytes that can be */
					/* queued after which the */
					/* application is flow controlled */
					/* Count includes bytes Txed */
					/* not yet acked. Plus bytes */
					/* queued for transmission */

	uint32_t sdpc_tx_bytes_queued;  /* bytes currently queued in the */
					/* send queue (see previous comment) */

	uint32_t sdpc_tx_bytes_unposted; /* bytes in the send queue that have */
					/* not yet been posted for Tx */

	int32_t oob_offset;	/* bytes till OOB byte is sent. */
				/* Relative to send_pipe */
	int16_t send_usig;	/* number of unsignaled sends in the */
				/* pipe. */
	int16_t send_cons;	/* number of consecutive unsignaled */
				/* sends. */

	sdp_generic_table_t send_queue;	/* queue of send objects. */
	sdp_pool_t send_post;	/* posted sends */

	timeout_id_t		sdp_conn_tid;
	uint16_t		sdp_saved_state;
	int32_t			sdp_saved_sendq_size;
	uchar_t			sdp_saved_s_wq_size;

	uint32_t send_seq;	/* sequence number of last message */
				/* sent */
	uint32_t recv_seq;	/* sequence number of last message */
				/* received */
	uint32_t advt_seq;	/* sequence number of last message */
				/* acknowledged */

	sdp_pool_t recv_pool;	/* pool of received buffers */
	sdp_pool_t recv_post;	/* posted receives */

	int32_t sdp_recv_byte_strm; /* buffered bytes in the local recv */
				    /* queue */
	char sdp_recv_oob_msg;		/* Recv side: oob byte itself */
	int32_t sdp_recv_oob_offset; /* Recv side: relative offset of OOB */
	int32_t sdp_recv_oob_state; /* Recv side: oob state, PEND or PRESENT */

	int32_t  sdpc_max_rwin;		/* max recv window size (bytes) */
	uint32_t sdpc_max_rbuffs;	/* max posted/used receive buffers */
	uint32_t sdpc_local_buff_size;	/* local recv buffer size */
	uint32_t sdpc_remote_buff_size;	/* remote recv buffer size */
	uint32_t sdpc_send_buff_size;	/* buffer size used for sending */

	uchar_t flags;		/* single bit flags. */
	uchar_t shutdown;	/* shutdown flag */
	uchar_t recv_mode;	/* current flow control mode */
	uchar_t send_mode;	/* current flow control mode */

	sock_upper_handle_t	sdp_ulpd;	/* SDP upper layer desc. */
	struct sdp_upcalls_s    sdp_upcalls;	/* upcalls for sdp_ulpd */
#define	sdp_ulp_newconn		sdp_upcalls.su_newconn
#define	sdp_ulp_connected	sdp_upcalls.su_connected
#define	sdp_ulp_disconnected	sdp_upcalls.su_disconnected
#define	sdp_ulp_connfailed	sdp_upcalls.su_connfailed
#define	sdp_ulp_recv		sdp_upcalls.su_recv
#define	sdp_ulp_xmitted		sdp_upcalls.su_xmitted
#define	sdp_ulp_urgdata		sdp_upcalls.su_urgdata
#define	sdp_ulp_ordrel		sdp_upcalls.su_ordrel
#define	sdp_ulp_set_proto_props	sdp_upcalls.su_set_proto_props

	uchar_t l_max_adv;	/* local maximum zcopy advertisements */
	uchar_t r_max_adv;	/* remote maximum zcopy */
				/* advertisements */
	uchar_t s_cur_adv;	/* current source advertisements */
				/* (slow start) */
	uchar_t s_par_adv;	/* current source advertisements */
				/* (slow start) */

	uint16_t r_recv_bf;	/* number of recv buffers remote */
				/* currently has */
	uint16_t l_recv_bf;	/* number of recv buffers local */
				/* currently has (posted in recv_post) */
	uint16_t l_advt_bf;	/* number of recv buffers local has */
				/* advertised */

	uchar_t s_wq_size;	/* current number of posted sends. */

	uchar_t s_wq_cur;	/* buffered transmission limit */
				/* current */
	uchar_t s_wq_par;	/* buffered transmission limit */
				/* increment */

	uchar_t src_recv;	/* outstanding remote source */
				/* advertisements */
	uchar_t snk_recv;	/* outstanding remote sink */
				/* advertisements */
	uchar_t sink_actv;
	/*
	 * work request ID's -- used to double-check queue consistency
	 */
	uint32_t send_wrid;
	uint32_t recv_wrid;

	uint32_t rcq_size;
	uint32_t scq_size;

	/*
	 * stale snk_avail detection
	 */
	uint32_t nond_recv;	/* non discarded buffers received. */
	uint32_t nond_send;	/* non discarded buffers sent */

	int32_t error;	/* error value on connection. */

	/*
	 * listen backlog
	 */
	uint32_t backlog_cnt;	/* depth of the listen backlog queue */
	uint32_t backlog_max;	/* max length of the listen backlog */
				/* queue */
	/*
	 * memory specific data
	 */
	sdp_generic_table_t send_ctrl;	/* control messages waiting */
						/* to be transmitted, which */
						/* do not depend on data */
						/* ordering */
	/*
	 * advertisement management
	 */
	sdp_advt_table_t src_pend;	/* pending remote source */
						/* advertisements */
	sdp_advt_table_t src_actv;	/* active remote source */
						/* advertisements */
	sdp_advt_table_t snk_pend;	/* pending remote sink */
						/* advertisements */

	sdp_generic_table_t r_src;	/* active user read source */
						/* iocbs */
	sdp_generic_table_t w_snk;	/* active user write sink */
						/* iocbs */

	/*
	 * IB specific data
	 */
	sdp_lookup_id_t plid;
	ib_gid_t d_gid;
	ib_gid_t d_alt_gid;
	ib_gid_t s_gid;
	ib_gid_t s_alt_gid;
	ib_qpn_t d_qpn;
	ib_qpn_t s_qpn;

	sdp_dev_hca_t	*hcap;
	ibt_hca_hdl_t hca_hdl;		/* hca that we'll be using for conn */

	ibt_pd_hdl_t pd_hdl;		/* protection domain for this conn */
	ibt_cq_hdl_t	scq_hdl;	/* completion queue */
	ibt_wc_t	ib_swc[SDP_MAX_CQ_WC];
	ibt_cq_hdl_t	rcq_hdl;	/* completion queue */
	ibt_wc_t	ib_rwc[SDP_MAX_CQ_WC];
	uint16_t	ib_inline_max;
	uint8_t		ib_use_reserved_lkey;

	uint8_t		hw_port;	/* hca port */

	/*
	 * Pointer to global state
	 */
	sdp_state_t *sdp_global_state;

	/*
	 * SDP connection lock
	 */
	kmutex_t conn_lock;
	/*
	 * Table management
	 */
	sdp_conn_t *lstn_next;		/* next connection in the chain */
	sdp_conn_t **lstn_p_next;	/* previous next connection in the */
					/* chain */

	sdp_conn_t *bind_next;	/* next connection in the chain */
	sdp_conn_t **bind_p_next;	/* previous next connection in the */
					/* chain */
	sdp_conn_t *delete_next;	/* next connnection to be deleted */

	/*
	 * Listen/accept managment
	 */
	sdp_conn_t *parent;	/* listening socket queuing. */
	sdp_conn_t *accept_next;	/* sockets waiting for acceptance. */
	sdp_conn_t *accept_prev;	/* sockets waiting for acceptance. */

	uchar_t sdp_msg_version; /* SDP message version for this connection */

	/*
	 * SDP specific socket options
	 */
	uchar_t nodelay;	/* socket nodelay is set */

	uint64_t send_bytes;	/* socket bytes sent */
	uint64_t recv_bytes;	/* socket bytes received */
	uint64_t write_bytes;	/* AIO bytes sent */
	uint64_t read_bytes;	/* AIO bytes received */

	uint32_t send_mid[SDP_MSG_EVENT_TABLE_SIZE];	/* send event stats */
	uint32_t recv_mid[SDP_MSG_EVENT_TABLE_SIZE];	/* recv event stats */

	cred_t	*sdp_credp;
	int	closeflags;
	kcondvar_t	closecv;

	kcondvar_t ss_rxdata_cv;
	kcondvar_t ss_txdata_cv;
	uint32_t   xmitflags;
	struct sdp_inet_ops inet_ops;

	/* Needed for APM Operations */
	boolean_t sdp_active_open;
	boolean_t sdp_apm_port_up;
	boolean_t sdp_apm_path_migrated;

#ifdef DEBUG
	pc_t	tcmp_stk[15];
#endif

};  /* sdp_conn_struct_t */

/*
 * Event handling function, demultiplexed base on message ID
 */
typedef int32_t(*sdp_event_cb_func_t)(sdp_conn_t *conn, sdp_buff_t *buff);

#define	SDP_OS_CONN_GET_ERR(conn)	(conn)->error

#define	SDP_WRID_GT(x, y) ((int32_t)((x) - (y)) > 0)
#define	SDP_WRID_LT(x, y) ((int32_t)((x) - (y)) < 0)
#define	SDP_WRID_GTE(x, y) ((int32_t)((x) - (y)) >= 0)
#define	SDP_WRID_LTE(x, y) ((int32_t)((x) - (y)) <= 0)

#define	SDP_SEQ_GT(x, y) ((int32_t)((x) - (y)) > 0)
#define	SDP_SEQ_LT(x, y) ((int32_t)((x) - (y)) < 0)
#define	SDP_SEQ_GTE(x, y) ((int32_t)((x) - (y)) >= 0)
#define	SDP_SEQ_LTE(x, y) ((int32_t)((x) - (y)) <= 0)

#define	SDP_CONN_STAT_SEND_INC(conn, size)  ((conn)->send_bytes += (size))
#define	SDP_CONN_STAT_RECV_INC(conn, size)  ((conn)->recv_bytes += (size))
#define	SDP_CONN_STAT_READ_INC(conn, size)  ((conn)->read_bytes += (size))
#define	SDP_CONN_STAT_WRITE_INC(conn, size) ((conn)->write_bytes += (size))
#define	SDP_CONN_STAT_SEND_MID_INC(conn, mid) \
	((conn)->send_mid[(mid)]++)
#define	SDP_CONN_STAT_RECV_MID_INC(conn, mid) \
	((conn)->recv_mid[(mid)]++)

#define	SDP_CONN_EVENT_RECV SDP_GENERIC_TYPE_BUFF
#define	SDP_CONN_EVENT_SEND SDP_GENERIC_TYPE_IOCB

extern void sdp_mutex_enter(sdp_conn_t *conn);
#define	SDP_CONN_LOCK(conn)	sdp_mutex_enter(conn)
#define	SDP_CONN_UNLOCK(conn) mutex_exit(&conn->conn_lock)

#define	SDP_CONN_TABLE_VERIFY(conn) { \
	ASSERT(conn->sdp_global_state->conn_array[conn->sdp_hashent] == conn); \
	SDP_CONN_LOCK(conn); \
}

extern void sdp_conn_destruct(sdp_conn_t *conn);
extern void sdp_conn_destruct_isr(sdp_conn_t *conn);

typedef enum {
	sdp_unbound = 0,
	sdp_bound_v4,
	sdp_bound_v6
} sdp_bound_state;

typedef enum {
	sdp_binding_none = 0,
	sdp_binding_v4,		/* can be INADDR_ANY */
	sdp_binding_v6		/* can be INADDR_ANY */
} sdp_binding_addr;

typedef enum {
	SDP_APM_PORT_UP = 1,
	SDP_APM_PORT_DOWN = 2,
	SDP_APM_PATH_MIGRATED = 3,
} sdp_apm_event;

extern int32_t sdp_conn_error(sdp_conn_t *conn);

#ifdef	TRACE_REFCNT
extern void SDP_CONN_HOLD(sdp_conn_t *conn);
extern void SDP_CONN_PUT(sdp_conn_t *conn);
extern void SDP_CONN_PUT_ISR(sdp_conn_t *conn);
extern void SDP_CONN_SOCK_HOLD(sdp_conn_t *conn);
extern void SDP_CONN_SOCK_PUT(sdp_conn_t *conn);

#else

#define	SDP_CONN_HOLD(conn) {					\
	mutex_enter(&conn->sdp_reflock);			\
	conn->sdp_ib_refcnt++;					\
	ASSERT(conn->sdp_ib_refcnt >= 1);			\
	mutex_exit(&conn->sdp_reflock);				\
}
#define	SDP_CONN_PUT(conn) {					\
	mutex_enter(&conn->sdp_reflock);			\
	ASSERT(conn->sdp_ib_refcnt >= 1);			\
	if (--conn->sdp_ib_refcnt == 0) {			\
		sdp_conn_deallocate_ib(conn);			\
		if (conn->sdp_inet_refcnt == 0)			\
			sdp_conn_deallocate(conn);		\
		else						\
			mutex_exit(&conn->sdp_reflock);		\
	} else							\
		mutex_exit(&conn->sdp_reflock);			\
}
#define	SDP_CONN_PUT_ISR(conn)	sdp_conn_destruct_isr(conn);

#define	SDP_CONN_SOCK_HOLD(conn) {				\
	mutex_enter(&conn->sdp_reflock);			\
	conn->sdp_inet_refcnt++;				\
	ASSERT(conn->sdp_inet_refcnt >= 1);			\
	mutex_exit(&conn->sdp_reflock);				\
}
#define	SDP_CONN_SOCK_PUT(conn)	{				\
	mutex_enter(&conn->sdp_reflock);			\
	ASSERT(conn->sdp_inet_refcnt >= 1);			\
	if (--conn->sdp_inet_refcnt == 0 && conn->sdp_ib_refcnt == 0) \
		sdp_conn_deallocate(conn);			\
	else							\
		mutex_exit(&conn->sdp_reflock);			\
}
#endif	/* TRACE_REFCNT */

#endif /* _KERNEL */
#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_CONN_H */

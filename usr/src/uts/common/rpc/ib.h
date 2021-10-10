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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2007, The Ohio State University. All rights reserved.
 *
 * Portions of this source code is developed by the team members of
 * The Ohio State University's Network-Based Computing Laboratory (NBCL),
 * headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * Acknowledgements to contributions from developors:
 *   Ranjit Noronha: noronha@cse.ohio-state.edu
 *   Lei Chai      : chail@cse.ohio-state.edu
 *   Weikuan Yu    : yuw@cse.ohio-state.edu
 *
 */


#ifndef _IB_H
#define	_IB_H

/*
 * ib.h, rpcib plugin interface.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <rpc/rpc.h>
#include <rpc/rpc_rdma.h>
#include <sys/ib/ibtl/ibti.h>
#include <sys/avl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	RIBBUF_TO_REGINFO(buf, ri)					\
	ri = (rib_buf_reginfo_t *)(buf -			\
	    (offsetof(rib_buf_md_t, rbm_buffer)))

#define	REGINFO_TO_RIBBUF(ri, buf)					\
	buf = ((caddr_t)ri + offsetof(rib_buf_md_t, rbm_buffer))

#define	NEXT_BUF(buf, bp)				\
	buf = ((buf) + bp->rsize + sizeof (rib_buf_reginfo_t))	\

#define	MAX_BUFS	1024	/* max no. of buffers per pool */

#define	DEF_CQ_SIZE	4096 - 1	/* default CQ size */
				/*
				 *
				 * Tavor returns the next higher power of 2
				 * CQ entries than the requested size.
				 * For instance, if you request (2^12 - 1)
				 * CQ entries, Tavor returns 2^12 entries.
				 * 4K CQ entries suffice.  Hence, 4096 - 1.
				 */
#define	DEF_SQ_SIZE	128	/* default SendQ size */
#define	DEF_RQ_SIZE	256	/* default RecvQ size */
#define	DSEG_MAX	2
#define	RQ_DSEG_MAX	1	/* default RQ data seg */
#define	IBSRM_HB	0x8000	/* high order bit of pkey */

/* max no. of refresh attempts on IBT_CM_CONN_STALE error */
#define	REFRESH_ATTEMPTS	3

typedef struct rib_hca_s rib_hca_t;
typedef struct rib_qp_s rib_qp_t;
typedef struct rib_cq_s rib_cq_t;

/*
 * Notification for RDMA_DONE is based on xid
 */
struct rdma_done_list {
	uint32_t	xid;		/* XID waiting for RDMA_DONE */
	kcondvar_t	rdma_done_cv;	/* cv for RDMA_DONE */
	struct rdma_done_list	*next;
	struct rdma_done_list	*prev;
};

/*
 * State of the plugin.
 * ACCEPT = accepting new connections and requests
 * NO_ACCEPT = not accepting new connection and requests
 */
#define	ACCEPT		1
#define	NO_ACCEPT	2

/*
 * Send Wait states
 */
#define	SEND_WAIT	-1

/*
 * Reply states
 */
#define	REPLY_WAIT	-1

typedef void * rib_pvoid;
typedef rib_pvoid RIB_SYNCMEM_HANDLE;

/*
 * IB buffer pool management structure
 */

typedef struct buf_info {
	caddr_t		bi_bufp;
	int		bi_bufsize;
	int		bi_index;
	struct buf_info	*bi_next;
} buf_info_t;

/*
 * Buffer pool info
 */
typedef struct {
	kmutex_t	buflock;	/* lock for this structure */
	buf_info_t	*buf_info;	/* pool address */
	uint32_t	bufhandle;	/* rkey for this pool */
	ulong_t		bufsize;	/* size of pool */
	int		rsize;		/* size of each element */
	int		numelems;	/* no. of elements allocated */
	int		buffree;	/* no. of free elements */
	void		**buflist;	/* free elements in pool */
} bufpool_t;


typedef struct rib_buf_reginfo
{
	ibt_mr_hdl_t	ri_mrhdl;
	ibt_mr_desc_t	ri_mrdesc;
} rib_buf_reginfo_t;

typedef struct rib_buf_md
{
	rib_buf_reginfo_t	rbm_info;
	caddr_t			rbm_buffer;
} rib_buf_md_t;

/*
 * ATS relsted defines and structures.
 */
#define	ATS_AR_DATA_LEN	16


/*
 * Service types supported by RPCIB
 * For now only NFS is supported.
 */
#define	NFS		1
#define	NLM		2

/*
 * Tracks consumer state (client or server).
 */
typedef enum {
	RIB_SERVER,
	RIB_CLIENT
} rib_mode_t;

/*
 * CQ structure
 */
struct rib_cq_s {
	rib_hca_t		*rib_hca;
	ibt_cq_hdl_t		rib_cq_hdl;
};

/*
 * Each registered service's data structure.
 */
typedef struct rib_service_s rib_service_t;
struct rib_service_s {
	uint32_t		srv_type;	/* i.e, NFS, NLM, v4CBD */
	ibt_srv_hdl_t		srv_hdl;	/* from ibt_register call */
	ib_svc_id_t		srv_id;
	rib_service_t		*next;
};

/*
 * RPCIB plugin state
 */
typedef struct rpcib_state {
	ibt_clnt_hdl_t		ibt_clnt_hdl;
	uint32_t		hca_count;
	uint32_t		nhca_inited;
	rib_hca_t		*hcas_list;
	krwlock_t		hcas_list_lock;	/* protects hcas_list */
	int			refcount;
	kmutex_t		open_hca_lock;
	queue_t			*q;		/* up queue for a serv_type */
	void			*private;
	rib_service_t		*service_list;
	krwlock_t		service_list_lock;
	kmutex_t		listen_lock;
} rpcib_state_t;

/*
 * Connection lists
 */
typedef struct {
	krwlock_t	conn_lock;	/* list lock */
	CONN		*conn_hd;	/* list head */
} rib_conn_list_t;

enum hca_state {
	HCA_DETACHED,		/* hca in detached state */
	HCA_INITED,		/* hca in up and running state */
};

typedef struct rib_hca_service_s rib_hca_service_t;
struct rib_hca_service_s {
	ib_svc_id_t	srv_id;
	ib_gid_t	gid;
	ibt_sbind_hdl_t	sbind_hdl;
	rib_hca_service_t *next;
};

/*
 * RPCIB per HCA structure
 */
struct rib_hca_s {
	ibt_clnt_hdl_t		ibt_clnt_hdl;

	/*
	 * per HCA.
	 */
	ibt_hca_hdl_t		hca_hdl;	/* HCA handle */
	ibt_hca_attr_t		hca_attrs;	/* HCA attributes */
	ibt_pd_hdl_t		pd_hdl;
	rib_hca_service_t	*bound_services;
	krwlock_t		bound_services_lock;
	ib_guid_t		hca_guid;
	uint32_t		hca_nports;
	ibt_hca_portinfo_t	*hca_ports;
	size_t			hca_pinfosz;
	enum hca_state		state;		/* state of HCA */
	krwlock_t		state_lock;	/* protects state field */
	bool_t			inuse;		/* indicates HCA usage */
	kmutex_t		inuse_lock;	/* protects inuse field */

	rib_conn_list_t		cl_conn_list;	/* client conn list */
	rib_conn_list_t		srv_conn_list;	/* server conn list */

	rib_cq_t		*clnt_scq;
	rib_cq_t		*clnt_rcq;
	rib_cq_t		*svc_scq;
	rib_cq_t		*svc_rcq;

	/*
	 * The cb_cv is used in two contexts:
	 *  - to wait on all hca references to drop before
	 *    destroying an HCA.  cb_inuse lock is used in this case.
	 *  - wait on all connections to be closed while
	 *    stopping the services.  cb_lock is used in this case.
	 */
	kmutex_t		cb_lock;
	kcondvar_t		cb_cv;
	int			cb_wait;	/* someone is waiting */
						/* for a callback */
	int			expand_pool_ip;	/* Buffer Pool expanding */
	bufpool_t		*recv_pool;	/* recv buf pool */
	bufpool_t		*send_pool;	/* send buf pool */

	void			*iblock;	/* interrupt cookie */

	kmem_cache_t	*server_side_cache;	/* long reply pool */
	avl_tree_t	avl_tree;
	kmutex_t	avl_lock;
	krwlock_t	avl_rw_lock;
	volatile bool_t avl_init;
	kmutex_t	cache_allocation_lock;
	ddi_taskq_t	*cleanup_helper;
	ddi_taskq_t	*buffer_taskq;
	ib_svc_id_t	srv_id;
	ibt_srv_hdl_t 	srv_hdl;
	uint_t		reg_state;

	volatile uint64_t	cache_allocation;
	uint64_t	cache_hits;
	uint64_t	cache_misses;
	uint64_t	cache_cold_misses;
	uint64_t	cache_hot_misses;
	uint64_t	cache_misses_above_the_limit;

	struct rib_hca_s *next;
};


/*
 * Structure on wait state of a post send
 */
struct send_wid {
	uint32_t 	xid;
	int		cv_sig;
	kmutex_t	sendwait_lock;
	kcondvar_t	wait_cv;
	uint_t		status;
	rib_qp_t	*qp;
	int		nsbufs;			/* # of send buffers posted */
	uint64_t	sbufaddr[DSEG_MAX];	/* posted send buffers */
	caddr_t		c;
	caddr_t		c1;
	int		l1;
	caddr_t		c2;
	int		l2;
	int		wl, rl;
};

/*
 * Structure on reply descriptor for recv queue.
 * Different from the above posting of a descriptor.
 */
struct reply {
	uint32_t 	xid;
	uint_t		status;
	uint64_t	vaddr_cq;	/* buf addr from CQ */
	uint_t		bytes_xfer;
	kcondvar_t	wait_cv;
	struct reply	*next;
	struct reply 	*prev;
};

struct svc_recv {
	rib_qp_t	*qp;
	uint64_t	vaddr;
	uint_t		bytes_xfer;
};

struct recv_wid {
	uint32_t 	xid;
	rib_qp_t	*qp;
	uint64_t	addr;	/* posted buf addr */
};

/*
 * Per QP data structure
 */
struct rib_qp_s {
	rib_hca_t		*hca;
	rib_mode_t		mode;	/* RIB_SERVER or RIB_CLIENT */
	CONN			rdmaconn;
	ibt_channel_hdl_t	qp_hdl;
	uint_t			port_num;
	ib_qpn_t		qpn;
	int			chan_flags;
	clock_t			timeout;
	ibt_rc_chan_query_attr_t	qp_q_attrs;
	rib_cq_t		*send_cq;	/* send CQ */
	rib_cq_t		*recv_cq;	/* recv CQ */

	/*
	 * Number of pre-posted rbufs
	 */
	uint_t			n_posted_rbufs;
	kcondvar_t 		posted_rbufs_cv;
	kmutex_t		posted_rbufs_lock;

	/*
	 * Number of SENDs pending completion
	 */

	uint_t			n_send_rbufs;
	kcondvar_t 		send_rbufs_cv;
	kmutex_t		send_rbufs_lock;

	/*
	 * RPC reply
	 */
	uint_t			rep_list_size;
	struct reply		*replylist;
	kmutex_t		replylist_lock;

	/*
	 * server only, RDMA_DONE
	 */
	struct rdma_done_list	*rdlist;
	kmutex_t		rdlist_lock;

	kmutex_t		cb_lock;
	kcondvar_t 		cb_conn_cv;

	caddr_t			q;	/* upstream queue */
	struct send_wid		wd;
};

#define	ctoqp(conn)	((rib_qp_t *)((conn)->c_private))
#define	qptoc(rqp)	((CONN *)&((rqp)->rdmaconn))

/*
 * Timeout for various calls
 */
#define	CONN_WAIT_TIME	40
#define	SEND_WAIT_TIME	40	/* time for send completion */

#define	REPLY_WAIT_TIME	40	/* time to get reply from remote QP */

#ifdef __cplusplus
}
#endif

#endif	/* !_IB_H */

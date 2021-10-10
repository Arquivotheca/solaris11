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

#ifndef	_SYS_IB_CLIENTS_SDP_BUFF_H
#define	_SYS_IB_CLIENTS_SDP_BUFF_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/ib/clients/sdpib/sdp_queue.h>
#include <sys/ib/clients/sdpib/sdp_dev.h>

struct sdp_buff_s;
/*
 * Structures
 */
typedef struct sdp_pool_s {
	struct sdp_buff_s *head;	/* double linked list of buffers */
	uint32_t sdp_pool_size;		/* current num of buffers */
	kmutex_t sdp_pool_mutex;
} sdp_pool_t;

typedef struct sdp_buff_s {
	struct sdp_buff_s *next;
	struct sdp_buff_s *prev;
	uint32_t type;	/* element type. (for generic queue) */
	sdp_pool_t *buff_pool;	/* pool currently holding this buffer. */
	sdp_gen_destruct_fn_t *release;	/* release the object */
	/*
	 * Primary generic data pointers
	 */
	void *sdp_buff_head;	/* first byte of data buffer */
	void *sdp_buff_data;	/* first byte of valid data in buffer */
	void *sdp_buff_tail;	/* last byte of valid data in buffer */
	void *sdp_buff_end;	/* last byte of data buffer */
	uint32_t sdp_buff_size;	/* buffer size */
	kmem_cache_t *sdp_buff_cache; /* kmem_cache the buff belongs to */

	/*
	 * Protocol specific data
	 */
	uint32_t flags;	/* buffer flags				*/
	uint32_t u_id;	/* unique buffer ID, used for tracking */

	sdp_msg_bsdh_t *bsdh_hdr;	/* SDP header (BSDH) */
	uint32_t sdp_buff_data_size;	/* size of just data in the buffer */
	ibt_wrid_t sdp_buff_ib_wrid;	/* IB work request ID */
	uint32_t sdp_buff_lkey;		/* scatter/gather list (key) */

	/* needed when reserved lkey cannot be used */
	ibt_mr_hdl_t sdp_buff_mr_hdl;	/* memory hdl for deregistration */
	ibt_wr_ds_t sdp_sgl[SDP_QP_LIMIT_SG_BUFF]; /* #sges usually needed */

	/* needed when reserved lkey can be used */
	ibt_mi_hdl_t sdp_mi_hdl;	/* memory hdl for deregistration */
	ibt_recv_wr_t sdp_rwr;		/* fully ready receive work request */

	ibt_hca_hdl_t sdp_buff_hca_hdl;	/* hca hdl for deregistration */
	frtn_t		frtn;
	uintptr_t	frtn_arg[2];

} sdp_buff_t;

/*
 * Buffer flag defintions
 */
#define	SDP_BUFF_F_UNSIG	0x0001	/* unsignalled buffer */
#define	SDP_BUFF_F_SE		0x0002	/* buffer is an IB solicited event */
#define	SDP_BUFF_F_OOB_PEND	0x0004	/* urgent byte in flight (OOB) */
#define	SDP_BUFF_F_OOB_PRES	0x0008	/* urgent byte in buffer (OOB) */
#define	SDP_BUFF_F_QUEUEDED	0x0010	/* buffer is queued for transmission */

#define	SDP_BUFF_F_GET_SE(buff) ((buff)->flags & SDP_BUFF_F_SE)
#define	SDP_BUFF_F_SET_SE(buff) ((buff)->flags |= SDP_BUFF_F_SE)
#define	SDP_BUFF_F_CLR_SE(buff) ((buff)->flags &= (~SDP_BUFF_F_SE))
#define	SDP_BUFF_F_GET_UNSIG(buff) ((buff)->flags & SDP_BUFF_F_UNSIG)
#define	SDP_BUFF_F_SET_UNSIG(buff) ((buff)->flags |= SDP_BUFF_F_UNSIG)
#define	SDP_BUFF_F_CLR_UNSIG(buff) ((buff)->flags &= (~SDP_BUFF_F_UNSIG))

/*
 * Function prototypes used in certain functions.
 */
typedef	int32_t	(*sdp_buff_test_func_t) (sdp_buff_t *buff, void *arg);
typedef	int32_t	(*sdp_buff_trav_func_t) (sdp_buff_t *buff, void *arg);

/*
 * Pool size
 */
#define	buff_pool_size(pool) ((pool)->sdp_pool_size)

#define	buff_pool_put_tail(pool, buff)	sdp_buff_pool_put(pool, buff, B_FALSE)
#define	buff_pool_put_head(pool, buff)	sdp_buff_pool_put(pool, buff, B_TRUE)
#define	buff_pool_put(pool, buff)	sdp_buff_pool_put(pool, buff, B_TRUE)
#define	buff_pool_get_tail(pool)	sdp_buff_pool_get(pool, B_FALSE)
#define	buff_pool_get_head(pool)	sdp_buff_pool_get(pool, B_TRUE)
#define	buff_pool_get(pool)		sdp_buff_pool_get(pool, B_TRUE)

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_BUFF_H */

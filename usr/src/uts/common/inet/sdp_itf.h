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

#ifndef	_INET_SDP_ITF_H
#define	_INET_SDP_ITF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/socket_proto.h>
#include <sys/ib/ib_types.h>

/*
 * Kernel SDP programming interface.  Note that this interface
 * is private to Sun and can be changed without notice.
 */

#ifdef _KERNEL

/*
 * The version number of the SDP kernel interface.  Use it with
 * sdp_itf_ver() to verify if the kernel supports the correct
 * version of the interface.
 *
 * NOTE: do not assume backward compatibility of the interface.
 * If the return value of sdp_itf_ver() is different from what
 * is expected, do not call any of the routines.
 */
#define	SDP_ITF_VER	1

/*
 * This struct holds all the upcalls the SDP kernel module will
 * invoke for different events.  When calling sdp_create() to create
 * a SDP handle, the caller must provide this information.
 */
typedef struct sdp_upcalls_s {
	void *	(*su_newconn)(sock_upper_handle_t, void *);
	void	(*su_connected)(sock_upper_handle_t);
	void	(*su_disconnected)(sock_upper_handle_t, int);
	void	(*su_connfailed)(sock_upper_handle_t, int);
	int	(*su_recv)(sock_upper_handle_t, mblk_t *, int);
	void	(*su_xmitted)(sock_upper_handle_t, int);
	void	(*su_urgdata)(sock_upper_handle_t);
	void	(*su_ordrel)(sock_upper_handle_t);
	void	(*su_set_proto_props)(sock_upper_handle_t,
		    struct sock_proto_props *);
} sdp_upcalls_t;


struct sdp_conn_struct_t;
struct sonode;

/*
 * part of sdp_conn_t exported to the external world
 * These fields have to be in exactly the same order as the
 * corresponding fields in sdp_conn_t
 */
typedef struct sdp_ext_conn_s {
	uint16_t sdp_ext_state;
	uint8_t	 sdp_ext_family;
	in6_addr_t sdp_ext_saddr;
	in6_addr_t sdp_ext_faddr;
	uint16_t sdp_ext_src_port;
	uint16_t sdp_ext_dst_port;
	zoneid_t sdp_ext_zoneid;
	netstack_t *sdp_ext_netstack;
} sdp_ext_conn_t;

/*
 * The list of routines the SDP kernel module provides.
 */
extern int sdp_bind(struct sdp_conn_struct_t *conn, struct sockaddr *addr,
    socklen_t addrlen);
extern void sdp_close(struct sdp_conn_struct_t *conn);
extern int sdp_connect(struct sdp_conn_struct_t *conn,
    const struct sockaddr *dst, socklen_t addrlen);
extern struct sdp_conn_struct_t *sdp_create(sock_upper_handle_t,
    struct sdp_conn_struct_t *, int, int, const sdp_upcalls_t *,
    cred_t *, int *);
extern int sdp_disconnect(struct sdp_conn_struct_t *conn, int flags);
extern int sdp_shutdown(struct sdp_conn_struct_t *conn, int flag);
extern int sdp_polldata(struct sdp_conn_struct_t *conn, int flag);
extern int sdp_get_opt(struct sdp_conn_struct_t *conn, int level, int opt,
    void *opts, socklen_t *optlen);
extern int sdp_getpeername(struct sdp_conn_struct_t *conn,
    struct sockaddr *addr, socklen_t *addrlen);
extern int sdp_getsockname(struct sdp_conn_struct_t *conn,
    struct sockaddr *addr, socklen_t *addrlen);
extern int sdp_itf_ver(int);
extern int sdp_listen(struct sdp_conn_struct_t *conn, int backlog);
extern int sdp_send(struct sdp_conn_struct_t *conn, struct msghdr *msg,
    size_t size, int flags, struct uio *uiop);
extern int sdp_recv(struct sdp_conn_struct_t *conn, struct msghdr *msg,
    int flags, struct uio *uiop);
extern int sdp_set_opt(struct sdp_conn_struct_t *conn, int level, int opt,
    const void *opts, socklen_t optlen);
extern int sdp_ioctl(struct sdp_conn_struct_t *conn, int cmd, int32_t *value,
    struct cred *cr);
extern void sdp_set_sorcvbuf(struct sdp_conn_struct_t *conn);

/* Flags for sdp_create() */
#define	SDP_CAN_BLOCK			0x01

#define	SDP_READ 0x01
#define	SDP_XMIT 0x02
#define	SDP_OOB  0x03

#endif /* _KERNEL */

#define	SDPIB_STR_NAME	"sdpib"
#define	SDP_IOC		('S' << 24 | 'D' << 16 | 'P' << 8)
#define	SDP_IOC_HCA	(SDP_IOC | 0x01)

typedef struct sdp_ioc_hca_s {
	ib_guid_t	sih_guid;
	boolean_t	sih_query;
	boolean_t	sih_force;
	boolean_t	sih_offline;
} sdp_ioc_hca_t;

/*
 * SDP Conn States:
 *
 * First two bytes are the primary state values. third byte is a bit field
 * used for different mask operations, defined below. fourth byte are the mib
 * values for the different states.
 */

#define	SDP_CONN_ST_LISTEN	0x0100 /* listening */

#define	SDP_CONN_ST_ESTABLISHED 0x1171 /* connected */

#define	SDP_CONN_ST_REQ_PATH    0x2100 /* active open, path record lookup */
#define	SDP_CONN_ST_REQ_SENT    0x2200 /* active open, Hello msg sent */
#define	SDP_CONN_ST_REQ_RECV    0x2340 /* passive open, Hello msg recv'd */
#define	SDP_CONN_ST_REP_RECV    0x2440 /* active open, Hello ack recv'd */
#define	SDP_CONN_ST_RTU_SENT	0x2580  /* active open, hello ack, acked */

#define	SDP_CONN_ST_DIS_RECV_1  0x4171 /* recv disconnect, passive close */
#define	SDP_CONN_ST_DIS_SEND_1  0x4271 /* send disconnect, active close */
#define	SDP_CONN_ST_DIS_SENT_1  0x4361 /* disconnect sent, active close */
#define	SDP_CONN_ST_DIS_RECV_R  0x4471 /* disconnect recv, active close */
#define	SDP_CONN_ST_DIS_SEND_2  0x4571 /* send disconnect, passive close */
#define	SDP_CONN_ST_TIME_WAIT_1 0x4701 /* IB/gateway disconnect */
#define	SDP_CONN_ST_TIME_WAIT_2 0x4801 /* waiting for idle close */

#define	SDP_CONN_ST_CLOSED	0x8E01 /* not connected */
#define	SDP_CONN_ST_ERROR	0x8D01 /* not connected */
#define	SDP_CONN_ST_INVALID	0x8F01 /* not connected */

/*
 * SDP states.
 */
typedef enum {
	SDP_MODE_BUFF = 0x00,
	SDP_MODE_COMB = 0x01,
	SDP_MODE_PIPE = 0x02,
	SDP_MODE_ERROR = 0x03
} sdp_mode_t;

/*
 * states masks for SDP
 */
#define	SDP_ST_MASK_CONNECT   0x2000	/* connection establishment states */
#define	SDP_ST_MASK_CLOSED    0x8000	/* all connection closed states. */
#define	SDP_ST_MASK_EVENTS    0x0001	/* event processing is allowed. */
#define	SDP_ST_MASK_SEND_OK   0x0010	/* posting data for send */
#define	SDP_ST_MASK_CTRL_OK   0x0020	/* posting control for send */
#define	SDP_ST_MASK_RCV_POST  0x0040	/* posting IB recv's is allowed. */


/*
 * Shutdown
 */
#define	SDP_SHUTDOWN_RECV   0x01	/* recv connection half close */
#define	SDP_SHUTDOWN_SEND   0x02	/* send connection half close */
#define	SDP_SHUTDOWN_MASK   0x03
#define	SDP_SHUTDOWN_NONE   0x00


#ifdef DEBUG
#define	SDP_CONN_ST_SET(conn, val) {			\
	(conn)->state = (val);				\
	(void) getpcstack((conn)->tcmp_stk, 15);	\
}
#else
#define	SDP_CONN_ST_SET(conn, val)	(conn)->state = (val)
#endif
#define	SDP_CONN_ST_INIT(conn)		(conn)->state = SDP_CONN_ST_INVALID

#define	SDP_RECV_OOB_PEND 0x1
#define	SDP_RECV_OOB_PRESENT 0x2
#define	SDP_RECV_OOB_ATMARK 0x4
#define	SDP_RECV_OOB_CONSUMED 0x8

/*
 * To maintain alignment we want the length to be exactly sizeof uint_t
 */
#define	SCI_STATE_LEN (sizeof (uint_t)/sizeof (char))

typedef struct sdp_connect_info_s {
	uint_t		sci_size;	/* size of this struct */
	uint_t		sci_family;
	uint_t		sci_lport;
	uint_t		sci_fport;
	uint_t		sci_laddr[4];
	uint_t		sci_faddr[4];
	char		sci_state[SCI_STATE_LEN];
	uint_t		sci_lbuff_size;
	uint_t		sci_rbuff_size;
	uint_t		sci_recv_bytes_pending;
	uint_t		sci_lbuff_posted;
	uint_t		sci_lbuff_advt;
	uint_t		sci_rbuff_advt;
	uint_t		sci_tx_bytes_queued;
	uint_t		sci_tx_bytes_unposted;
	zoneid_t	sci_zoneid;
}sdp_connect_info_t;
#define	SDP_NODELAY 0x01

#ifdef __cplusplus
}
#endif

#endif /* _INET_SDP_ITF_H */

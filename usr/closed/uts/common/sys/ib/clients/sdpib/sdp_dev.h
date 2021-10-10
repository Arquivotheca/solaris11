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

#ifndef _SYS_IB_CLIENTS_SDP_DEV_H
#define	_SYS_IB_CLIENTS_SDP_DEV_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/ib/clients/sdpib/sdp_kstat.h>

/*
 * SDP root device (state) struct and other definitions, including sdp port
 * and sdp hca.
 *
 */
#define	SDP_DEV_NAME  "sdp"		/* character device driver name */
#define	SDP_MSG_HDR_REQ_SIZE 0x10	/* required header size (BSDH) */
#define	SDP_MSG_HDR_OPT_SIZE 0x14	/* optional header size (SNKAH) */
#define	SDP_MSG_HDR_SIZE	SDP_MSG_HDR_REQ_SIZE

/*
 * Set of performance parameters. Some are interdependent. If you
 * change any of these, run full regression test suite.
 */
#define	SDP_DEV_SEND_CQ_SIZE	0x003f	/* Initial value of conn->scq_size */
#define	SDP_DEV_RECV_CQ_SIZE	0x003f	/* Initial value of conn->rcq_size */
#define	SDP_DEV_SEND_POST_MAX  0x003f

#define	SDP_DEV_SEND_BACKLOG   0x0002  /* tx backlog before PIPELINED mode */

#define	SDP_DEV_INET_THRSH	1024    /* send high water mark */
#define	SDP_PORT_RANGE_LOW	10000
#define	SDP_PORT_RANGE_HIGH	20000

#define	SDP_QP_LIMIT_SG_BUFF	3	/* scatter/gather entries in buff */
#define	SDP_QP_LIMIT_SG_MAX	18	/* max scatter/gather entries */
/*
 * SDP_LKEY_MAX_BUFF_SIZE should map into (SDP_QP_LIMIT_SG_MAX - 2) entries
 * This means that on systems with larger physical page size larger buffer sizes
 * can be used with lkey's.
 */
#define	SDP_LKEY_MAX_BUFF_SIZE	((SDP_QP_LIMIT_SG_MAX - 1) << PAGESHIFT)

/*
 * Maximum number of src/sink advertisements we can handle at a given time.
 */
#define	SDP_MSG_MAX_ADVS	0xff

/*
 * Service ID is 64 bits, but a socket port is only the low 16 bits. A
 * mask is defined for the rest of the 48 bits, and is reserved in the
 * IBTA.
 */
#define	SDP_MSG_SERVICE_ID_RANGE (0x0000000000010000ull)
#define	SDP_MSG_SERVICE_ID_VALUE (0x000000000001ffffull)
#define	SDP_MSG_SERVICE_ID_MASK  (0xffffffffffff0000ull)

#define	SDP_MSG_SID_TO_PORT(sid)  ((uint16_t)((sid) & 0xffff))
#define	SDP_MSG_PORT_TO_SID(port) \
		((uint64_t)(SDP_MSG_SERVICE_ID_RANGE | ((port) & 0xffff)))

/*
 * Invalid socket identifier, top entry in table.
 */
#define	SDP_DEV_SK_LIST_SIZE 16384	/* array of active sockets */
#define	SDP_DEV_SK_INVALID   (-1)

/*
 * The protocol requires a src_avail message to contain at least one
 * byte of the data stream, when the connection is in combined mode.
 * Here's the amount of data to send.
 */
#define	SDP_SRC_AVAIL_GRATUITOUS 0x01
#define	SDP_DEV_SEND_POST_SLOW   0x01
#define	SDP_SRC_AVAIL_THRESHOLD  0x40

#define	SDP_DEV_SEND_POST_COUNT  0x0a


/*
 * Maximum consecutive unsignalled send events.
 * (watch out for deactivated nodelay!)
 */
#define	SDP_CONN_UNSIG_SEND_MAX 0x0f

/*
 * When to post new recv buffers and send buffer advertisements
 */
#define	SDP_CONN_RECV_POST_ACK_LIM 0x08 /* rate for posting ack windows. */

typedef struct sdp_dev_port_s {
	uint8_t index;	/* port ID -- begins with 1 */
	ib_gid_t sgid;	/* source port GID */
	ib_lid_t base_lid;
	ibt_port_state_t state;
	struct sdp_dev_port_s *next;	/* next port in the list */
	ibt_sbind_hdl_t bind_hdl;
} sdp_dev_port_t;

typedef struct sdp_dev_hca_s {
	ibt_hca_hdl_t hca_hdl;
	uint8_t hca_nports;
	uint8_t hca_use_reserved_lkey;
	uint16_t hca_inline_max;
	uint32_t hca_nds;		/* sgl size for reserved_lkey */
	ibt_pd_hdl_t pd_hdl;		/* protection domain for this HCA */
	ib_guid_t guid;			/* 64-bit value */
	sdp_dev_port_t *port_list;	/* ports on this HCA */
	struct sdp_dev_hca_s *next;		/* next HCA in the list */
	kmem_cache_t *sdp_hca_buff_cache;
	uint32_t hca_num_conns;
	uint32_t hca_sdp_buff_size;	/* sizeof a buffer in the cache */
	ibt_sched_hdl_t	sdp_hca_cq_sched_hdl;
	/*
	 * the refcnt is bumped when the HCA is looked up for DR operation,
	 * used to prevent the HCA from being detached during DR
	 */
	uint32_t sdp_hca_refcnt;
	boolean_t sdp_hca_offlined;
} sdp_dev_hca_t;

typedef enum sdp_dev_state {
	SDP_DETACHED,
	SDP_ATTACHED,
	SDP_DETACHING,
	SDP_ATTACHING,
} sdp_dev_state_t;

#define	SDP_NUM_EPRIV_PORTS	64

/* Named Dispatch Parameter Management Structure */
typedef struct sdpparam_s {
	uint32_t	sdp_param_min;
	uint32_t	sdp_param_max;
	uint32_t	sdp_param_val;
	char		*sdp_param_name;
} sdpparam_t;

/*
 * Per-netstack SDP information
 */
typedef struct sdp_stack {
	netstack_t *sdps_netstack;
	kmutex_t sdps_bind_lock;
	sdp_conn_t *sdps_bind_list;
	kmutex_t sdps_listen_lock;
	sdp_conn_t *sdps_listen_list;
	int32_t sdps_port_rover;

	/* per-stack-instance ndd related fields */
	kmutex_t sdps_param_lock;
	caddr_t sdps_nd;
	sdpparam_t *sdps_param_arr;

	/*
	 * Extra privileged ports, in host byte order.
	 * Protected by sdp_epriv_port_lock.
	 */
	kmutex_t sdps_epriv_port_lock;
	uint16_t sdps_epriv_ports[SDP_NUM_EPRIV_PORTS];
	int sdps_num_epriv_ports;

	sdp_named_kstat_t sdps_named_ks;
	kstat_t *sdps_kstat;
} sdp_stack_t;

/*
 * SDP root device structure.
 *
 * There is one instance of the root device, also known as "sdp global state",
 * instantiated as a static in the sdp_inet.c module.
 */

typedef struct sdp_dev_root_s {

	dev_info_t *dip;
	major_t    major;
	sdp_dev_state_t sdp_state;
	kmutex_t sdp_state_lock;
	int sdp_buff_size;
	uint32_t src_addr;

	ibt_clnt_hdl_t sdp_ibt_hdl;

	/*
	 * Devices: list of installed HCA's and some associated parameters.
	 */
	sdp_dev_hca_t *hca_list;
	uint32_t hca_count;
	/*
	 * Driver mutex protects hca_list
	 */
	kmutex_t hcas_mutex;
	kcondvar_t hcas_cv;
	/*
	 * Connections: the table is a simple linked list, since it does not
	 * need to require fast lookup capabilities.
	 */
	uint32_t conn_array_size;	/* socket array size */
	uint32_t conn_array_ordr;	/* order size of region. */
	uint32_t conn_array_rover;	/* order size of region. */
	uint32_t conn_array_num_entries; /* number of socket table entries. */
	sdp_conn_t **conn_array;	/* array of conn structs. */
	/*
	 * Connection management
	 */
	sdp_conn_t *delete_list;	/* connections marked for deletion */

	/*
	 * List locks
	 */

	kmutex_t delete_lock;
	kmutex_t sock_lock;	/* protects connection list and delete list */
	boolean_t exit_flag;
	kt_did_t  delete_thr_id;

	kcondvar_t delete_cv;

	kmem_cache_t *conn_cache;

	/*
	 * Path resolution fields
	 */
	kmutex_t lookup_lock;
	uint64_t lookup_id;	/* a counter for lookup id */
	sdp_prcn_t *prc_head;	/* pr cache list head */
	sdp_prwqn_t *wq_head;	/* wait queue node list */
	struct kmem_cache *wq_kmcache;	/* kmem cache for  wait queue */
	struct kmem_cache *pr_kmcache;	/* kmem cache pr cache */
	timeout_id_t prc_timeout_id;	/* cache reaping timer id */
	ibt_srv_hdl_t sdp_ibt_srv_hdl;	/* from ibt_register_service() */
	boolean_t sdp_apm_enabled;
} sdp_dev_root_t;

typedef sdp_dev_root_t sdp_state_t;

extern sdpparam_t	sdp_param_arr[];

#define	sdp_xmit_hiwat		sdps_param_arr[0].sdp_param_val
#define	sdp_recv_hiwat		sdps_param_arr[1].sdp_param_val
#define	sdp_recv_hiwat_max	sdps_param_arr[1].sdp_param_max
#define	sdp_recv_hiwat_min	sdps_param_arr[1].sdp_param_min
#define	sdp_sth_rcv_lowat	sdps_param_arr[2].sdp_param_val

#define	SDP_XMIT_LOWATER	4096
#define	SDP_XMIT_HIWATER	262144	/* 64*4096 */
#define	SDP_RECV_LOWATER	SDP_MIN_BUFF_COUNT * SDP_MSG_BUFF_SIZE
#define	SDP_RECV_HIWATER	262144	/* 64*4096 */

/* Retry count before port failure is reported */
extern int sdp_path_retry_cnt;

/* Retry count for Receive Not Ready (no posted buffers) */
extern int sdp_path_rnr_retry_cnt;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_DEV_H */

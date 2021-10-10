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
 * Copyright (c) 2002-2004, Network Appliance, Inc. All rights reserved.
 */

/*
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_DAT_H_
#define	_DAT_H_

/*
 *
 * HEADER: dat.h
 *
 * PURPOSE: defines the common DAT API for uDAPL and kDAPL.
 *
 * Description: Header file for "uDAPL: User Direct Access Programming
 *		Library, Version: 1.2"
 *
 * Mapping rules:
 *      All global symbols are prepended with "DAT_" or "dat_"
 *      All DAT objects have an 'api' tag which, such as 'ep' or 'lmr'
 *      The method table is in the provider definition structure.
 *
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <dat/dat_error.h>

/* Generic DAT types */

typedef char *DAT_NAME_PTR;   /* Format for ia_name and attributes */
#define	DAT_NAME_MAX_LENGTH	256

/*
 * Used for provider, vendor, transport, hardware specific attributes
 * definitions.
 */

typedef struct dat_named_attr {
	const char	*name;   /* Name of attribute  */
	const char	*value;  /* Value of attribute */
} DAT_NAMED_ATTR;

typedef enum dat_boolean {
	DAT_FALSE = 0,
	DAT_TRUE = 1
} DAT_BOOLEAN;

typedef union dat_context {
	DAT_PVOID	as_ptr;
	DAT_UINT64	as_64;
	DAT_UVERYLONG	as_index;
} DAT_CONTEXT;

typedef DAT_CONTEXT	DAT_DTO_COOKIE;
typedef DAT_CONTEXT	DAT_RMR_COOKIE;

typedef enum dat_completion_flags {
	/* Completes with notification					*/
	DAT_COMPLETION_DEFAULT_FLAG			= 0x00,

	/* Completions suppressed if successful				*/
	DAT_COMPLETION_SUPPRESS_FLAG			= 0x01,

	/* Sender controlled notification for recv completion		*/
	DAT_COMPLETION_SOLICITED_WAIT_FLAG		= 0x02,

	/* Completions with unsignaled notifications			*/
	DAT_COMPLETION_UNSIGNALLED_FLAG			= 0x04,

	/* Do not start processing until all previous RDMA reads complete. */
	DAT_COMPLETION_BARRIER_FENCE_FLAG		= 0x08,

	/*
	 * Only valid for uDAPL as EP attribute for Recv Completion flags.
	 * Waiter unblocking is controlled by Threshold value of dat_evd_wait.
	 * UNSIGNALLED for RECV not allowed when EP has this attribute
	 */
	DAT_COMPLETION_EVD_THRESHOLD_FLAG		= 0x10

} DAT_COMPLETION_FLAGS;


typedef	DAT_UINT32	DAT_TIMEOUT;		/* microseconds */

/* timeout = infinity */
#define	DAT_TIMEOUT_INFINITE	((DAT_TIMEOUT) ~0)

/* dat handles */
typedef DAT_PVOID	DAT_HANDLE;
typedef DAT_HANDLE	DAT_CR_HANDLE;
typedef DAT_HANDLE	DAT_EP_HANDLE;
typedef DAT_HANDLE	DAT_EVD_HANDLE;
typedef DAT_HANDLE	DAT_IA_HANDLE;
typedef DAT_HANDLE	DAT_LMR_HANDLE;
typedef DAT_HANDLE	DAT_PSP_HANDLE;
typedef DAT_HANDLE	DAT_PZ_HANDLE;
typedef DAT_HANDLE	DAT_RMR_HANDLE;
typedef DAT_HANDLE	DAT_RSP_HANDLE;
typedef DAT_HANDLE	DAT_SRQ_HANDLE;


/* dat NULL handles */
#define	DAT_HANDLE_NULL	((DAT_HANDLE)NULL)

typedef DAT_SOCK_ADDR   *DAT_IA_ADDRESS_PTR;

typedef DAT_UINT64	DAT_CONN_QUAL;
typedef DAT_UINT64	DAT_PORT_QUAL;

/* QOS definitions */
typedef enum dat_qos {
	DAT_QOS_BEST_EFFORT		= 0x00,
	DAT_QOS_HIGH_THROUGHPUT		= 0x01,
	DAT_QOS_LOW_LATENCY		= 0x02,
	/* not low latency, nor high throughput   */
	DAT_QOS_ECONOMY			= 0x04,
	/* both low latency and high throughput   */
	DAT_QOS_PREMIUM			= 0x08
} DAT_QOS;

/*
 * FLAGS
 */

typedef enum dat_connect_flags {
	DAT_CONNECT_DEFAULT_FLAG	= 0x00,
	DAT_CONNECT_MULTIPATH_FLAG	= 0x01
} DAT_CONNECT_FLAGS;

typedef enum dat_close_flags {
	DAT_CLOSE_ABRUPT_FLAG		= 0x00,
	DAT_CLOSE_GRACEFUL_FLAG		= 0x01
} DAT_CLOSE_FLAGS;

#define	DAT_CLOSE_DEFAULT	DAT_CLOSE_ABRUPT_FLAG

typedef enum dat_evd_flags {
	DAT_EVD_SOFTWARE_FLAG		= 0x001,
	DAT_EVD_CR_FLAG			= 0x010,
	DAT_EVD_DTO_FLAG		= 0x020,
	DAT_EVD_CONNECTION_FLAG		= 0x040,
	DAT_EVD_RMR_BIND_FLAG		= 0x080,
	DAT_EVD_ASYNC_FLAG		= 0x100,
	/* DAT events only, no software events */
	DAT_EVD_DEFAULT_FLAG		= 0x1F0
} DAT_EVD_FLAGS;

typedef enum dat_psp_flags {
	DAT_PSP_CONSUMER_FLAG	= 0x00, /*  Consumer creates an endpoint */
	DAT_PSP_PROVIDER_FLAG	= 0x01  /*  Provider creates an endpoint */
} DAT_PSP_FLAGS;

/*
 * Memory Buffers
 *
 * Both LMR and RMR triplets specify 64-bit addresses in the local host's byte
 * order, even when that exceeds the size of a DAT_PVOID for the host
 * architecture.
 */

/*
 * Both LMR and RMR Triplets specify 64-bit addresses in the local host
 * order, even when that exceeds the size of a void pointer for the host
 * architecture. The DAT_VADDR type that represents addresses is in the
 * native byte-order of the local host. Helper macros that allow Consumers
 * to convert DAT_VADDR into various orders that might be useful for
 * inclusion of RMR Triplets into a payload of a message follow.
 *
 * DAT defines the following macros to convert the fields on an RMR Triplet
 * to defined byte orders to allow their export by the Consumer over wire
 * protocols. DAT does not define how the two peers decide which byte should be
 * used.
 *
 * DAT_LMRC_TO_LSB(lmrc) returns the supplied LMR Context in ls-byte
 * order.
 * DAT_LMRC_TO_MSB(lmrc) returns the supplied LMR Context in ms-byte
 * order.
 * DAT_RMRC_TO_LSB(rmrc) returns the supplied RMR Context in ls-byte
 * order.
 * DAT_RMRC_TO_MSB(rmrc) returns the supplied RMR Context in ms-byte
 * order.
 * DAT_VADDR_TO_LSB(vaddr) returns the supplied Virtual Address in ls-byte
 * order.
 * DAT_VADDR_TO_MSB(vaddr) returns the supplied Virtual Address in
 * ms-byte order.
 * DAT_VLEN_TO_LSB(vlen) returns the supplied length in ls-byte order.
 * DAT_VLEN_TO_MSB(vlen) returns the supplied length in ms-byte order.
 *
 * Consumers are free to use 64-bit or 32-bit arithmetic for local or remote
 * memory address and length manipulation in their preferred byte-order. Only
 * the LMR and RMR Triplets passed to a Provider as part of a Posted DTO are
 * required to be in 64-bit address and local host order formats. Providers
 * shall convert RMR_Triplets to a Transport-required wire format.
 *
 * For the best performance, Consumers should align each buffer segment to
 * the boundary specified by the dat_optimal_alignment.
 */
typedef	DAT_UINT32	DAT_LMR_CONTEXT;
typedef DAT_UINT32	DAT_RMR_CONTEXT;

typedef DAT_UINT64	DAT_VLEN;
typedef DAT_UINT64	DAT_VADDR;

typedef struct dat_provider_attr	DAT_PROVIDER_ATTR;
typedef struct dat_evd_param		DAT_EVD_PARAM;
typedef struct dat_lmr_param		DAT_LMR_PARAM;

/*
 * It is legal for Consumer to specify zero for segment_length
 * of the dat_lmr_triplet. When 0 is specified for the
 * segment_length then the other two elements of the
 * dat_lmr_triplet are irrelevant and can be invalid.
 */

typedef struct dat_lmr_triplet {
	DAT_LMR_CONTEXT		lmr_context;
	DAT_UINT32		pad;
	DAT_VADDR		virtual_address;
	DAT_VLEN		segment_length;
} DAT_LMR_TRIPLET;

typedef struct dat_rmr_triplet {
	DAT_RMR_CONTEXT		rmr_context;
	DAT_UINT32		pad;
	DAT_VADDR		target_address;
	DAT_VLEN		segment_length;
} DAT_RMR_TRIPLET;

/*
 * The extra padding ensures that context, address, and length parameters are
 * aligned on 64-bit boundaries.
 */

/* Memory privileges */

typedef enum dat_mem_priv_flags {
	DAT_MEM_PRIV_NONE_FLAG		= 0x00,
	DAT_MEM_PRIV_LOCAL_READ_FLAG	= 0x01,
	DAT_MEM_PRIV_REMOTE_READ_FLAG	= 0x02,
	DAT_MEM_PRIV_LOCAL_WRITE_FLAG	= 0x10,
	DAT_MEM_PRIV_REMOTE_WRITE_FLAG	= 0x20,
	DAT_MEM_PRIV_ALL_FLAG		= 0x33,
	DAT_MEM_PRIV_RO_DISABLE_FLAG	= 0x100
} DAT_MEM_PRIV_FLAGS;

/*
 * For backwards compatibility with DAT-1.0 memory privileges values
 * are supported
 */
#define	DAT_MEM_PRIV_READ_FLAG  (DAT_MEM_PRIV_LOCAL_READ_FLAG |		\
					DAT_MEM_PRIV_REMOTE_READ_FLAG)
#define	DAT_MEM_PRIV_WRITE_FLAG (DAT_MEM_PRIV_LOCAL_WRITE_FLAG |	\
					DAT_MEM_PRIV_REMOTE_WRITE_FLAG)

/* LMR Arguments Mask */

typedef enum dat_lmr_param_mask {
	DAT_LMR_FIELD_IA_HANDLE			= 0x001,
	DAT_LMR_FIELD_MEM_TYPE			= 0x002,
	DAT_LMR_FIELD_REGION_DESC		= 0x004,
	DAT_LMR_FIELD_LENGTH			= 0x008,
	DAT_LMR_FIELD_PZ_HANDLE			= 0x010,
	DAT_LMR_FIELD_MEM_PRIV			= 0x020,
	DAT_LMR_FIELD_LMR_CONTEXT		= 0x040,
	DAT_LMR_FIELD_RMR_CONTEXT		= 0x080,
	DAT_LMR_FIELD_REGISTERED_SIZE		= 0x100,
	DAT_LMR_FIELD_REGISTERED_ADDRESS	= 0x200,

	DAT_LMR_FIELD_ALL			= 0x3FF
} DAT_LMR_PARAM_MASK;

/* RMR Argumments & RMR Arguments Mask */

typedef struct dat_rmr_param {
	DAT_IA_HANDLE		ia_handle;
	DAT_PZ_HANDLE		pz_handle;
	DAT_LMR_TRIPLET		lmr_triplet;
	DAT_MEM_PRIV_FLAGS	mem_priv;
	DAT_RMR_CONTEXT		rmr_context;
} DAT_RMR_PARAM;

typedef enum dat_rmr_param_mask {
	DAT_RMR_FIELD_IA_HANDLE		= 0x01,
	DAT_RMR_FIELD_PZ_HANDLE		= 0x02,
	DAT_RMR_FIELD_LMR_TRIPLET	= 0x04,
	DAT_RMR_FIELD_MEM_PRIV		= 0x08,
	DAT_RMR_FIELD_RMR_CONTEXT	= 0x10,

	DAT_RMR_FIELD_ALL		= 0x1F
} DAT_RMR_PARAM_MASK;

/* Provider attributes */

typedef enum dat_iov_ownership {
	/* Not modification by provider, consumer can use anytime.	*/
	DAT_IOV_CONSUMER		= 0x0,

	/* Provider does not modify returned IOV DTO on completion.	*/
	DAT_IOV_PROVIDER_NOMOD		= 0x1,

	/* Provider may modify IOV DTO on completion, can't trust it.	*/
	DAT_IOV_PROVIDER_MOD		= 0x2

} DAT_IOV_OWNERSHIP;

typedef enum dat_ep_creator_for_psp {
	DAT_PSP_CREATES_EP_NEVER,	/* provider never creates endpoint    */
	DAT_PSP_CREATES_EP_IFASKED,	/* provider creates endpoint if asked */
	DAT_PSP_CREATES_EP_ALWAYS	/* provider always creates endpoint   */
} DAT_EP_CREATOR_FOR_PSP;

/* General Interface Adapter attributes. These apply to both udat and kdat. */

/* To support backwards compatibility for DAPL-1.0 */
#define	max_rdma_read_per_ep 		max_rdma_read_per_ep_in
#define	DAT_IA_FIELD_IA_MAX_DTO_PER_OP 	DAT_IA_FIELD_IA_ MAX_DTO_PER_EP_IN

/* to support backwards compatibility for DAPL-1.0 & DAPL-1.1 */
#define	max_mtu_size max_message_size

typedef struct dat_ia_attr
{
	char				adapter_name[DAT_NAME_MAX_LENGTH];
	char				vendor_name[DAT_NAME_MAX_LENGTH];
	DAT_UINT32			hardware_version_major;
	DAT_UINT32			hardware_version_minor;
	DAT_UINT32			firmware_version_major;
	DAT_UINT32			firmware_version_minor;
	DAT_IA_ADDRESS_PTR		ia_address_ptr;
	DAT_COUNT			max_eps;
	DAT_COUNT			max_dto_per_ep;
	DAT_COUNT			max_rdma_read_per_ep_in;
	DAT_COUNT			max_rdma_read_per_ep_out;
	DAT_COUNT			max_evds;
	DAT_COUNT			max_evd_qlen;
	DAT_COUNT			max_iov_segments_per_dto;
	DAT_COUNT			max_lmrs;
	DAT_VLEN			max_lmr_block_size;
	DAT_VADDR			max_lmr_virtual_address;
	DAT_COUNT			max_pzs;
	DAT_VLEN			max_message_size;
	DAT_VLEN			max_rdma_size;
	DAT_COUNT			max_rmrs;
	DAT_VADDR			max_rmr_target_address;
	DAT_COUNT			max_srqs;
	DAT_COUNT			max_ep_per_srq;
	DAT_COUNT			max_recv_per_srq;
	DAT_COUNT			max_iov_segments_per_rdma_read;
	DAT_COUNT			max_iov_segments_per_rdma_write;
	DAT_COUNT			max_rdma_read_in;
	DAT_COUNT			max_rdma_read_out;
	DAT_BOOLEAN			max_rdma_read_per_ep_in_guaranteed;
	DAT_BOOLEAN			max_rdma_read_per_ep_out_guaranteed;
	DAT_COUNT			num_transport_attr;
	DAT_NAMED_ATTR			*transport_attr;
	DAT_COUNT			num_vendor_attr;
	DAT_NAMED_ATTR			*vendor_attr;
} DAT_IA_ATTR;

/* To support backwards compatibility for DAPL-1.0 & DAPL-1.1 */
#define	DAT_IA_FIELD_IA_MAX_MTU_SIZE	DAT_IA_FIELD_IA_MAX_MESSAGE_SIZE

typedef DAT_UINT64 DAT_IA_ATTR_MASK;

#define	DAT_IA_FIELD_IA_ADAPTER_NAME			UINT64_C(0x000000001)
#define	DAT_IA_FIELD_IA_VENDOR_NAME			UINT64_C(0x000000002)
#define	DAT_IA_FIELD_IA_HARDWARE_MAJOR_VERSION		UINT64_C(0x000000004)
#define	DAT_IA_FIELD_IA_HARDWARE_MINOR_VERSION		UINT64_C(0x000000008)
#define	DAT_IA_FIELD_IA_FIRMWARE_MAJOR_VERSION		UINT64_C(0x000000010)
#define	DAT_IA_FIELD_IA_FIRMWARE_MINOR_VERSION		UINT64_C(0x000000020)
#define	DAT_IA_FIELD_IA_ADDRESS_PTR			UINT64_C(0x000000040)
#define	DAT_IA_FIELD_IA_MAX_EPS				UINT64_C(0x000000080)
#define	DAT_IA_FIELD_IA_MAX_DTO_PER_EP			UINT64_C(0x000000100)
#define	DAT_IA_FIELD_IA_MAX_RDMA_READ_PER_EP_IN		UINT64_C(0x000000200)
#define	DAT_IA_FIELD_IA_MAX_RDMA_READ_PER_EP_OUT	UINT64_C(0x000000400)
#define	DAT_IA_FIELD_IA_MAX_EVDS			UINT64_C(0x000000800)
#define	DAT_IA_FIELD_IA_MAX_EVD_QLEN			UINT64_C(0x000001000)
#define	DAT_IA_FIELD_IA_MAX_IOV_SEGMENTS_PER_DTO	UINT64_C(0x000002000)
#define	DAT_IA_FIELD_IA_MAX_LMRS			UINT64_C(0x000004000)
#define	DAT_IA_FIELD_IA_MAX_LMR_BLOCK_SIZE		UINT64_C(0x000008000)
#define	DAT_IA_FIELD_IA_MAX_LMR_VIRTUAL_ADDRESS		UINT64_C(0x000010000)
#define	DAT_IA_FIELD_IA_MAX_PZS				UINT64_C(0x000020000)
#define	DAT_IA_FIELD_IA_MAX_MESSAGE_SIZE		UINT64_C(0x000040000)
#define	DAT_IA_FIELD_IA_MAX_RDMA_SIZE			UINT64_C(0x000080000)
#define	DAT_IA_FIELD_IA_MAX_RMRS			UINT64_C(0x000100000)
#define	DAT_IA_FIELD_IA_MAX_RMR_TARGET_ADDRESS		UINT64_C(0x000200000)
#define	DAT_IA_FIELD_IA_MAX_SRQS			UINT64_C(0x000400000)
#define	DAT_IA_FIELD_IA_MAX_EP_PER_SRQ			UINT64_C(0x000800000)
#define	DAT_IA_FIELD_IA_MAX_RECV_PER_SRQ		UINT64_C(0x001000000)
#define	DAT_IA_FIELD_IA_MAX_IOV_SEGMENTS_PER_RDMA_READ	UINT64_C(0x002000000)
#define	DAT_IA_FIELD_IA_MAX_IOV_SEGMENTS_PER_RDMA_WRITE	UINT64_C(0x004000000)
#define	DAT_IA_FIELD_IA_MAX_RDMA_READ_IN		UINT64_C(0x008000000)
#define	DAT_IA_FIELD_IA_MAX_RDMA_READ_OUT		UINT64_C(0x010000000)
#define	DAT_IA_FIELD_IA_MAX_RDMA_READ_PER_EP_IN_GUARANTEED \
							UINT64_C(0x020000000)
#define	DAT_IA_FIELD_IA_MAX_RDMA_READ_PER_EP_OUT_GUARANTEED \
							UINT64_C(0x040000000)
#define	DAT_IA_FIELD_IA_NUM_TRANSPORT_ATTR		UINT64_C(0x080000000)
#define	DAT_IA_FIELD_IA_TRANSPORT_ATTR			UINT64_C(0x100000000)
#define	DAT_IA_FIELD_IA_NUM_VENDOR_ATTR			UINT64_C(0x200000000)
#define	DAT_IA_FIELD_IA_VENDOR_ATTR			UINT64_C(0x400000000)

/* To support backwards compatibility for DAPL-1.0 & DAPL-1.1 */
#define	DAT_IA_ALL 					DAT_IA_FIELD_ALL
#define	DAT_IA_FIELD_ALL				UINT64_C(0x7FFFFFFFF)
#define	DAT_IA_FIELD_NONE				UINT64_C(0x0)

/* Endpoint attributes */

typedef enum dat_service_type {
	DAT_SERVICE_TYPE_RC		/* reliable connections */
} DAT_SERVICE_TYPE;

typedef struct dat_ep_attr {
	DAT_SERVICE_TYPE		service_type;
	DAT_VLEN			max_message_size;
	DAT_VLEN			max_rdma_size;
	DAT_QOS				qos;
	DAT_COMPLETION_FLAGS		recv_completion_flags;
	DAT_COMPLETION_FLAGS		request_completion_flags;
	DAT_COUNT			max_recv_dtos;
	DAT_COUNT			max_request_dtos;
	DAT_COUNT			max_recv_iov;
	DAT_COUNT			max_request_iov;
	DAT_COUNT			max_rdma_read_in;
	DAT_COUNT			max_rdma_read_out;
	DAT_COUNT			srq_soft_hw;
	DAT_COUNT			max_rdma_read_iov;
	DAT_COUNT			max_rdma_write_iov;
	DAT_COUNT			ep_transport_specific_count;
	DAT_NAMED_ATTR *		ep_transport_specific;
	DAT_COUNT			ep_provider_specific_count;
	DAT_NAMED_ATTR *		ep_provider_specific;
} DAT_EP_ATTR;

/* Endpoint Parameters */

/* For backwards compatibility */
#define	DAT_EP_STATE_ERROR	DAT_EP_STATE_DISCONNECTED

typedef enum dat_ep_state {
	DAT_EP_STATE_UNCONNECTED,	/* quiescent state */
	DAT_EP_STATE_UNCONFIGURED_UNCONNECTED,
	DAT_EP_STATE_RESERVED,
	DAT_EP_STATE_UNCONFIGURED_RESERVED,
	DAT_EP_STATE_PASSIVE_CONNECTION_PENDING,
	DAT_EP_STATE_UNCONFIGURED_PASSIVE,
	DAT_EP_STATE_ACTIVE_CONNECTION_PENDING,
	DAT_EP_STATE_TENTATIVE_CONNECTION_PENDING,
	DAT_EP_STATE_UNCONFIGURED_TENTATIVE,
	DAT_EP_STATE_CONNECTED,
	DAT_EP_STATE_DISCONNECT_PENDING,
	DAT_EP_STATE_DISCONNECTED,
	DAT_EP_STATE_COMPLETION_PENDING
} DAT_EP_STATE;

typedef struct dat_ep_param {
	DAT_IA_HANDLE			ia_handle;
	DAT_EP_STATE			ep_state;
	DAT_IA_ADDRESS_PTR		local_ia_address_ptr;
	DAT_PORT_QUAL			local_port_qual;
	DAT_IA_ADDRESS_PTR		remote_ia_address_ptr;
	DAT_PORT_QUAL			remote_port_qual;
	DAT_PZ_HANDLE			pz_handle;
	DAT_EVD_HANDLE			recv_evd_handle;
	DAT_EVD_HANDLE			request_evd_handle;
	DAT_EVD_HANDLE			connect_evd_handle;
	DAT_SRQ_HANDLE			srq_handle;
	DAT_EP_ATTR			ep_attr;
} DAT_EP_PARAM;

typedef DAT_UINT64 DAT_EP_PARAM_MASK;
#define	DAT_EP_FIELD_IA_HANDLE				UINT64_C(0x00000001)
#define	DAT_EP_FIELD_EP_STATE				UINT64_C(0x00000002)
#define	DAT_EP_FIELD_LOCAL_IA_ADDRESS_PTR		UINT64_C(0x00000004)
#define	DAT_EP_FIELD_LOCAL_PORT_QUAL			UINT64_C(0x00000008)
#define	DAT_EP_FIELD_REMOTE_IA_ADDRESS_PTR		UINT64_C(0x00000010)
#define	DAT_EP_FIELD_REMOTE_PORT_QUAL			UINT64_C(0x00000020)
#define	DAT_EP_FIELD_PZ_HANDLE				UINT64_C(0x00000040)
#define	DAT_EP_FIELD_RECV_EVD_HANDLE			UINT64_C(0x00000080)
#define	DAT_EP_FIELD_REQUEST_EVD_HANDLE			UINT64_C(0x00000100)
#define	DAT_EP_FIELD_CONNECT_EVD_HANDLE			UINT64_C(0x00000200)
#define	DAT_EP_FIELD_SRQ_HANDLE				UINT64_C(0x00000400)

	/* Remainder of values from EP_ATTR, 0x00001000 and up */

#define	DAT_EP_FIELD_EP_ATTR_SERVICE_TYPE		UINT64_C(0x00001000)
#define	DAT_EP_FIELD_EP_ATTR_MAX_MESSAGE_SIZE		UINT64_C(0x00002000)
#define	DAT_EP_FIELD_EP_ATTR_MAX_RDMA_SIZE		UINT64_C(0x00004000)
#define	DAT_EP_FIELD_EP_ATTR_QOS			UINT64_C(0x00008000)

#define	DAT_EP_FIELD_EP_ATTR_RECV_COMPLETION_FLAGS	UINT64_C(0x00010000)
#define	DAT_EP_FIELD_EP_ATTR_REQUEST_COMPLETION_FLAGS	UINT64_C(0x00020000)
#define	DAT_EP_FIELD_EP_ATTR_MAX_RECV_DTOS		UINT64_C(0x00040000)
#define	DAT_EP_FIELD_EP_ATTR_MAX_REQUEST_DTOS		UINT64_C(0x00080000)

#define	DAT_EP_FIELD_EP_ATTR_MAX_RECV_IOV		UINT64_C(0x00100000)
#define	DAT_EP_FIELD_EP_ATTR_MAX_REQUEST_IOV		UINT64_C(0x00200000)

#define	DAT_EP_FIELD_EP_ATTR_MAX_RDMA_READ_IN		UINT64_C(0x00400000)
#define	DAT_EP_FIELD_EP_ATTR_MAX_RDMA_READ_OUT		UINT64_C(0x00800000)

#define	DAT_EP_FIELD_EP_ATTR_SRQ_SOFT_HW		UINT64_C(0x01000000)

#define	DAT_EP_FIELD_EP_ATTR_MAX_RDMA_READ_IOV		UINT64_C(0x02000000)
#define	DAT_EP_FIELD_EP_ATTR_MAX_RDMA_WRITE_IOV		UINT64_C(0x04000000)

#define	DAT_EP_FIELD_EP_ATTR_NUM_TRANSPORT_ATTR		UINT64_C(0x08000000)
#define	DAT_EP_FIELD_EP_ATTR_TRANSPORT_SPECIFIC_ATTR	UINT64_C(0x10000000)

#define	DAT_EP_FIELD_EP_ATTR_NUM_PROVIDER_ATTR		UINT64_C(0x20000000)
#define	DAT_EP_FIELD_EP_ATTR_PROVIDER_SPECIFIC_ATTR	UINT64_C(0x40000000)

#define	DAT_EP_FIELD_EP_ATTR_ALL			UINT64_C(0x7FFFF000)
#define	DAT_EP_FIELD_ALL				UINT64_C(0x7FFFF7FF)

#define	DAT_WATERMARK_INFINITE ((DAT_COUNT)~0)

#define	DAT_HW_DEFAULT DAT_WATERMARK_INFINITE

#define	DAT_SRQ_LW_DEFAULT 0x0

typedef enum dat_srq_state {
	DAT_SRQ_STATE_OPERATIONAL,
	DAT_SRQ_STATE_ERROR
} DAT_SRQ_STATE;

#define	DAT_VALUE_UNKNOWN	(((DAT_COUNT) ~0)-1)

typedef struct dat_srq_attr {
	DAT_COUNT	max_recv_dtos;
	DAT_COUNT	max_recv_iov;
	DAT_COUNT	low_watermark;
} DAT_SRQ_ATTR;

typedef struct dat_srq_param {
	DAT_IA_HANDLE		ia_handle;
	DAT_SRQ_STATE		srq_state;
	DAT_PZ_HANDLE		pz_handle;
	DAT_COUNT		max_recv_dtos;
	DAT_COUNT		max_recv_iov;
	DAT_COUNT		low_watermark;
	DAT_COUNT 		available_dto_count;
	DAT_COUNT		outstanding_dto_count;
} DAT_SRQ_PARAM;

typedef enum dat_srq_param_mask {
	DAT_SRQ_FIELD_IA_HANDLE				= 0x001,
	DAT_SRQ_FIELD_SRQ_STATE				= 0x002,
	DAT_SRQ_FIELD_PZ_HANDLE				= 0x004,
	DAT_SRQ_FIELD_MAX_RECV_DTO			= 0x008,
	DAT_SRQ_FIELD_MAX_RECV_IOV			= 0x010,
	DAT_SRQ_FIELD_LOW_WATERMARK			= 0x020,
	DAT_SRQ_FIELD_AVAILABLE_DTO_COUNT		= 0x040,
	DAT_SRQ_FIELD_OUTSTANDING_DTO_COUNT		= 0x080,

	DAT_SRQ_FIELD_ALL				= 0x0FF
} DAT_SRQ_PARAM_MASK;

/* PZ Parameters */

typedef struct dat_pz_param {
	DAT_IA_HANDLE		ia_handle;
} DAT_PZ_PARAM;

typedef enum dat_pz_param_mask {
    DAT_PZ_FIELD_IA_HANDLE	= 0x01,

    DAT_PZ_FIELD_ALL		= 0x01
} DAT_PZ_PARAM_MASK;

/* PSP Parameters */

typedef struct dat_psp_param {
	DAT_IA_HANDLE		ia_handle;
	DAT_CONN_QUAL		conn_qual;
	DAT_EVD_HANDLE		evd_handle;
	DAT_PSP_FLAGS		psp_flags;
} DAT_PSP_PARAM;

typedef enum dat_psp_param_mask {
    DAT_PSP_FIELD_IA_HANDLE	= 0x01,
    DAT_PSP_FIELD_CONN_QUAL	= 0x02,
    DAT_PSP_FIELD_EVD_HANDLE	= 0x04,
    DAT_PSP_FIELD_PSP_FLAGS	= 0x08,

    DAT_PSP_FIELD_ALL		= 0x0F
} DAT_PSP_PARAM_MASK;

/* RSP Parameters */

typedef struct dat_rsp_param {
	DAT_IA_HANDLE		ia_handle;
	DAT_CONN_QUAL		conn_qual;
	DAT_EVD_HANDLE		evd_handle;
	DAT_EP_HANDLE		ep_handle;
} DAT_RSP_PARAM;

typedef enum dat_rsp_param_mask {
	DAT_RSP_FIELD_IA_HANDLE		= 0x01,
	DAT_RSP_FIELD_CONN_QUAL		= 0x02,
	DAT_RSP_FIELD_EVD_HANDLE	= 0x04,
	DAT_RSP_FIELD_EP_HANDLE		= 0x08,

	DAT_RSP_FIELD_ALL		= 0x0F
} DAT_RSP_PARAM_MASK;

/*
 * Connection Request Parameters.
 *
 * The Connection Request does not provide Remote Endpoint attributes.
 * If a local Consumer needs this information, the remote Consumer should
 * encode it into Private Data.
 */

typedef struct dat_cr_param {
	/* Remote IA whose Endpoint requested the connection.		*/
	DAT_IA_ADDRESS_PTR	remote_ia_address_ptr;

	/* Port qualifier of the remote Endpoint of the requested connection. */
	DAT_PORT_QUAL		remote_port_qual;

	/* Size of the Private Data.					*/
	DAT_COUNT		private_data_size;

	/*
	 * Pointer to the Private Data passed by remote side in the Connection
	 * Request.
	 */
	DAT_PVOID		private_data;

	/*
	 * The local Endpoint provided by the Service Point for the requested
	 * connection. It is the only Endpoint that can accept a Connection
	 * Request on this Service Point. The value DAT_HANDLE_NULL represents
	 * that there is no associated local Endpoint for the requested
	 * connection.
	 */
	DAT_EP_HANDLE			local_ep_handle;

} DAT_CR_PARAM;

typedef enum dat_cr_param_mask {
	DAT_CR_FIELD_REMOTE_IA_ADDRESS_PTR	= 0x01,
	DAT_CR_FIELD_REMOTE_PORT_QUAL		= 0x02,
	DAT_CR_FIELD_PRIVATE_DATA_SIZE		= 0x04,
	DAT_CR_FIELD_PRIVATE_DATA		= 0x08,
	DAT_CR_FIELD_LOCAL_EP_HANDLE		= 0x10,

	DAT_CR_FIELD_ALL			= 0x1F
} DAT_CR_PARAM_MASK;

/*
 * *********************** Events *****************************************
 */

/* Completion status flags */

/* dto completion status */

/* For backwards compatibility */
#define	DAT_DTO_LENGTH_ERROR    DAT_DTO_ERR_LOCAL_LENGTH
#define	DAT_DTO_FAILURE		DAT_DTO_ERR_FLUSHED

typedef enum dat_dto_completion_status {
	DAT_DTO_SUCCESS			= 0,
	DAT_DTO_ERR_FLUSHED		= 1,
	DAT_DTO_ERR_LOCAL_LENGTH	= 2,
	DAT_DTO_ERR_LOCAL_EP		= 3,
	DAT_DTO_ERR_LOCAL_PROTECTION	= 4,
	DAT_DTO_ERR_BAD_RESPONSE	= 5,
	DAT_DTO_ERR_REMOTE_ACCESS	= 6,
	DAT_DTO_ERR_REMOTE_RESPONDER	= 7,
	DAT_DTO_ERR_TRANSPORT		= 8,
	DAT_DTO_ERR_RECEIVER_NOT_READY	= 9,
	DAT_DTO_ERR_PARTIAL_PACKET	= 10,
	DAT_RMR_OPERATION_FAILED	= 11
} DAT_DTO_COMPLETION_STATUS;

/* RMR completion status */

/* For backwards compatibility */
#define	DAT_RMR_BIND_SUCCESS    DAT_DTO_SUCCESS
#define	DAT_RMR_BIND_FAILURE    DAT_DTO_ERR_FLUSHED

#define	DAT_RMR_BIND_COMPLETION_STATUS DAT_DTO_COMPLETION_STATUS

/* Completion group structs (six total) */

/* DTO completion event data */
/* transferred_length is not defined if status is not DAT_SUCCESS */
typedef struct dat_dto_completion_event_data {
	DAT_EP_HANDLE				ep_handle;
	DAT_DTO_COOKIE				user_cookie;
	DAT_DTO_COMPLETION_STATUS		status;
	DAT_VLEN				transfered_length;
} DAT_DTO_COMPLETION_EVENT_DATA;

/* RMR bind completion event data */
typedef struct dat_rmr_bind_completion_event_data {
	DAT_RMR_HANDLE				rmr_handle;
	DAT_RMR_COOKIE				user_cookie;
	DAT_RMR_BIND_COMPLETION_STATUS		status;
} DAT_RMR_BIND_COMPLETION_EVENT_DATA;

typedef union dat_sp_handle {
	DAT_RSP_HANDLE		rsp_handle;
	DAT_PSP_HANDLE		psp_handle;
} DAT_SP_HANDLE;

/* Connection Request Arrival event data */
typedef struct dat_cr_arrival_event_data {
	/*
	 * Handle to the Service Point that received the Connection Request
	 * from the remote side. If the Service Point was Reserved, sp is
	 * DAT_HANDLE_NULL because the reserved Service Point is automatically
	 * destroyed upon generating this event. Can be PSP or RSP.
	 */
	DAT_SP_HANDLE			sp_handle;

	/* Address of the IA on which the Connection Request arrived. */
	DAT_IA_ADDRESS_PTR		local_ia_address_ptr;

	/*
	 * Connection Qualifier of the IA on which the Service Point received a
	 * Connection Request.
	 */
	DAT_CONN_QUAL			conn_qual;

	/*
	 * The Connection Request instance created by a Provider for the
	 * arrived Connection Request. Consumers can find out private_data
	 * passed by a remote Consumer from cr_handle. It is up to a Consumer
	 * to dat_cr_accept or dat_cr_reject of the Connection Request.
	 */
	DAT_CR_HANDLE			cr_handle;

} DAT_CR_ARRIVAL_EVENT_DATA;


/* Connection event data */
typedef struct dat_connection_event_data {
	DAT_EP_HANDLE		ep_handle;
	DAT_COUNT		private_data_size;
	DAT_PVOID		private_data;
} DAT_CONNECTION_EVENT_DATA;

/* Async Error event data */
/*
 * For unaffiliated asynchronous event dat_handle is ia_handle. For Endpoint
 * affiliated asynchronous event dat_handle is ep_handle. For EVD affiliated
 * asynchronous event dat_handle is evd_handle. For SRQ affiliated asynchronous
 * event dat_handle is srq_handle. For Memory affiliated asynchronous event
 * dat_handle is either lmr_handle, rmr_handle or pz_handle.
 */
typedef struct dat_asynch_error_event_data {
	DAT_HANDLE		dat_handle;	/* either IA, EP, EVD, SRQ */
						/* LMR, RMR, or PZ handle  */
	DAT_COUNT		reason;		/* object specific	   */
} DAT_ASYNCH_ERROR_EVENT_DATA;

/* The reason is object type specific and its values are defined below */
typedef enum ia_async_error_reason {
	DAT_IA_CATASTROPHIC_ERROR,
	DAT_IA_OTHER_ERROR
} DAT_IA_ASYNC_ERROR_REASON;

typedef enum ep_async_error_reason {
	DAT_EP_TRANSFER_TO_ERROR,
	DAT_EP_OTHER_ERROR,
	DAT_SRQ_SOFT_HIGH_WATERMARK_EVENT
} DAT_EP_ASYNC_ERROR_REASON;

typedef enum ep_evd_error_reason {
	DAT_EVD_OVERFLOW_ERROR,
	DAT_EVD_OTHER_ERROR
} DAT_EVD_ASYNC_ERROR_REASON;

typedef enum ep_srq_error_reason {
	DAT_SRQ_TRANSFER_TO_ERROR,
	DAT_SRQ_OTHER_ERROR,
	DAT_SRQ_LOW_WATERMARK_EVENT
} DAT_SRQ_ASYNC_ERROR_REASON;

typedef enum lmr_async_error_reason {
	DAT_LMR_OTHER_ERROR
} DAT_LMR_ASYNC_ERROR_REASON;

typedef enum rmr_async_error_reason {
	DAT_RMR_OTHER_ERROR
} DAT_RMR_ASYNC_ERROR_REASON;

typedef enum pz_async_error_reason {
	DAT_PZ_OTHER_ERROR
} DAT_PZ_ASYNC_ERROR_REASON;


/* Software event data */
typedef struct dat_software_event_data {
	DAT_PVOID	pointer;
} DAT_SOFTWARE_EVENT_DATA;

typedef enum dat_event_number {
	DAT_DTO_COMPLETION_EVENT				= 0x00001,

	DAT_RMR_BIND_COMPLETION_EVENT				= 0x01001,

	DAT_CONNECTION_REQUEST_EVENT				= 0x02001,

	DAT_CONNECTION_EVENT_ESTABLISHED			= 0x04001,
	DAT_CONNECTION_EVENT_PEER_REJECTED			= 0x04002,
	DAT_CONNECTION_EVENT_NON_PEER_REJECTED			= 0x04003,
	DAT_CONNECTION_EVENT_ACCEPT_COMPLETION_ERROR		= 0x04004,
	DAT_CONNECTION_EVENT_DISCONNECTED			= 0x04005,
	DAT_CONNECTION_EVENT_BROKEN				= 0x04006,
	DAT_CONNECTION_EVENT_TIMED_OUT				= 0x04007,
	DAT_CONNECTION_EVENT_UNREACHABLE			= 0x04008,

	DAT_ASYNC_ERROR_EVD_OVERFLOW				= 0x08001,
	DAT_ASYNC_ERROR_IA_CATASTROPHIC				= 0x08002,
	DAT_ASYNC_ERROR_EP_BROKEN				= 0x08003,
	DAT_ASYNC_ERROR_TIMED_OUT				= 0x08004,
	DAT_ASYNC_ERROR_PROVIDER_INTERNAL_ERROR			= 0x08005,

	DAT_SOFTWARE_EVENT					= 0x10001
} DAT_EVENT_NUMBER;

/* Union for event Data */

typedef union dat_event_data {
	DAT_DTO_COMPLETION_EVENT_DATA		dto_completion_event_data;
	DAT_RMR_BIND_COMPLETION_EVENT_DATA	rmr_completion_event_data;
	DAT_CR_ARRIVAL_EVENT_DATA		cr_arrival_event_data;
	DAT_CONNECTION_EVENT_DATA		connect_event_data;
	DAT_ASYNCH_ERROR_EVENT_DATA		asynch_error_event_data;
	DAT_SOFTWARE_EVENT_DATA			software_event_data;
} DAT_EVENT_DATA;

/* Event struct that holds all event information */

typedef struct dat_event {
	DAT_EVENT_NUMBER	event_number;
	DAT_EVD_HANDLE		evd_handle;
	DAT_EVENT_DATA		event_data;
} DAT_EVENT;

/* Provider/registration info */

typedef struct dat_provider_info {
	char			ia_name[DAT_NAME_MAX_LENGTH];
	DAT_UINT32		dapl_version_major;
	DAT_UINT32		dapl_version_minor;
	DAT_BOOLEAN		is_thread_safe;
} DAT_PROVIDER_INFO;

/*
 * FUNCTION PROTOTYPES
 */
/*
 * IA functions
 *
 * Note that there are actual 'dat_ia_open' and 'dat_ia_close'
 * functions, it is not just a re-directing #define. That is
 * because the functions may have to ensure that the provider
 * library is loaded before it can call it, and may choose to
 * unload the library after the last close.
 */

extern DAT_RETURN dat_ia_openv(
	IN	const DAT_NAME_PTR,	/* provider		*/
	IN	DAT_COUNT,		/* asynch_evd_min_qlen  */
	INOUT	DAT_EVD_HANDLE *,	/* asynch_evd_handle    */
	OUT	DAT_IA_HANDLE *,	/* ia_handle		*/
	IN	DAT_UINT32,		/* dat major version number */
	IN	DAT_UINT32,		/* dat minor version number */
	IN	DAT_BOOLEAN);		/* dat thread safety */

#define	dat_ia_open(name, qlen, async_evd, ia) \
	dat_ia_openv((name), (qlen), (async_evd), (ia), \
		DAT_VERSION_MAJOR, DAT_VERSION_MINOR, \
		DAT_THREADSAFE)

extern DAT_RETURN dat_ia_query(
	IN	DAT_IA_HANDLE,		/* ia handle		*/
	OUT	DAT_EVD_HANDLE *,	/* async_evd_handle	*/
	IN	DAT_IA_ATTR_MASK,	/* ia_attr_mask		*/
	OUT	DAT_IA_ATTR *,		/* ia_attr		*/
	IN	DAT_PROVIDER_ATTR_MASK,	/* provider_attr_mask	*/
	OUT	DAT_PROVIDER_ATTR *);	/* provider_attr	*/

extern DAT_RETURN dat_ia_close(
	IN	DAT_IA_HANDLE,		/* ia_handle		*/
	IN	DAT_CLOSE_FLAGS);	/* close_flags		*/

/* helper functions */

extern DAT_RETURN dat_set_consumer_context(
	IN	DAT_HANDLE,		/* dat handle		*/
	IN	DAT_CONTEXT);		/* context		*/

extern DAT_RETURN dat_get_consumer_context(
	IN	DAT_HANDLE,		/* dat handle		*/
	OUT	DAT_CONTEXT *);		/* context		*/

extern DAT_RETURN dat_get_handle_type(
	IN	DAT_HANDLE,		/* dat_handle		*/
	OUT	DAT_HANDLE_TYPE *);	/* handle_type		*/

/* CR Functions */

extern DAT_RETURN dat_cr_query(
	IN	DAT_CR_HANDLE,		/* cr_handle		*/
	IN	DAT_CR_PARAM_MASK,	/* cr_param_mask	*/
	OUT	DAT_CR_PARAM *);	/* cr_param		*/

extern DAT_RETURN dat_cr_accept(
	IN	DAT_CR_HANDLE,		/* cr_handle		*/
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_COUNT,		/* private_data_size	*/
	IN	const DAT_PVOID);	/* private_data		*/

extern DAT_RETURN dat_cr_reject(
	IN	DAT_CR_HANDLE);

/*
 * For DAT-1.1 and above, this function is defined for both uDAPL and kDAPL.
 * For DAT-1.0 it was only defined for uDAPL.
 */
extern DAT_RETURN dat_cr_handoff(
	IN	DAT_CR_HANDLE,		/* cr_handle		*/
	IN	DAT_CONN_QUAL);		/* handoff		*/

/* EVD Functions */

extern DAT_RETURN dat_evd_resize(
	IN	DAT_EVD_HANDLE,		/* evd_handle		*/
	IN	DAT_COUNT);		/* evd_min_qlen		*/

extern DAT_RETURN dat_evd_post_se(
	IN	DAT_EVD_HANDLE,		/* evd_handle		*/
	IN	const DAT_EVENT *);	/* event		*/

extern DAT_RETURN dat_evd_dequeue(
	IN	DAT_EVD_HANDLE,		/* evd_handle		*/
	OUT	DAT_EVENT *);		/* event		*/

extern DAT_RETURN dat_evd_query(
	IN	DAT_EVD_HANDLE,		/* evd_handle		*/
	IN	DAT_EVD_PARAM_MASK,	/* evd_param_mask	*/
	OUT	DAT_EVD_PARAM *);	/* evd_param		*/

extern DAT_RETURN dat_evd_free(
	IN	DAT_EVD_HANDLE);	/* evd_handle		*/

/* EP functions */

extern DAT_RETURN dat_ep_create(
	IN	DAT_IA_HANDLE,		/* ia_handle		*/
	IN	DAT_PZ_HANDLE,		/* pz_handle		*/
	IN	DAT_EVD_HANDLE,		/* recv_completion_evd_handle */
	IN	DAT_EVD_HANDLE,		/* request_completion_evd_handle */
	IN	DAT_EVD_HANDLE,		/* connect_evd_handle   */
	IN	const DAT_EP_ATTR *,	/* ep_attributes	*/
	OUT	DAT_EP_HANDLE *);	/* ep_handle		*/

extern DAT_RETURN dat_ep_query(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_EP_PARAM_MASK,	/* ep_param_mask	*/
	OUT	DAT_EP_PARAM *);	/* ep_param		*/

extern DAT_RETURN dat_ep_modify(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_EP_PARAM_MASK,	/* ep_param_mask	*/
	IN	const DAT_EP_PARAM *);	/* ep_param		*/

extern DAT_RETURN dat_ep_connect(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_IA_ADDRESS_PTR,	/* remote_ia_address	*/
	IN	DAT_CONN_QUAL,		/* remote_conn_qual	*/
	IN	DAT_TIMEOUT,		/* timeout		*/
	IN	DAT_COUNT,		/* private_data_size	*/
	IN	const DAT_PVOID,	/* private_data		*/
	IN	DAT_QOS,		/* quality_of_service	*/
	IN	DAT_CONNECT_FLAGS);	/* connect_flags	*/

extern DAT_RETURN dat_ep_dup_connect(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_EP_HANDLE,		/* ep_dup_handle	*/
	IN	DAT_TIMEOUT,		/* timeout		*/
	IN	DAT_COUNT,		/* private_data_size	*/
	IN	const DAT_PVOID,	/* private_data		*/
	IN	DAT_QOS);		/* quality_of_service	*/

extern DAT_RETURN dat_ep_disconnect(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_CLOSE_FLAGS);	/* close_flags		*/

extern DAT_RETURN dat_ep_post_send(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_COUNT,		/* num_segments		*/
	IN	DAT_LMR_TRIPLET *,	/* local_iov		*/
	IN	DAT_DTO_COOKIE,		/* user_cookie		*/
	IN	DAT_COMPLETION_FLAGS);	/* completion_flags	*/

extern DAT_RETURN dat_ep_post_recv(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_COUNT,		/* num_segments		*/
	IN	DAT_LMR_TRIPLET *,	/* local_iov		*/
	IN	DAT_DTO_COOKIE,		/* user_cookie		*/
	IN	DAT_COMPLETION_FLAGS);	/* completion_flags	*/

extern DAT_RETURN dat_ep_post_rdma_read(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_COUNT,		/* num_segments		*/
	IN	DAT_LMR_TRIPLET *,	/* local_iov		*/
	IN	DAT_DTO_COOKIE,		/* user_cookie		*/
	IN	const DAT_RMR_TRIPLET *,	/* remote_iov		*/
	IN	DAT_COMPLETION_FLAGS);	/* completion_flags	*/

extern DAT_RETURN dat_ep_post_rdma_write(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_COUNT,		/* num_segments		*/
	IN	DAT_LMR_TRIPLET *,	/* local_iov		*/
	IN	DAT_DTO_COOKIE,		/* user_cookie		*/
	IN	const DAT_RMR_TRIPLET *,	/* remote_iov		*/
	IN	DAT_COMPLETION_FLAGS);	/* completion_flags	*/

extern DAT_RETURN dat_ep_get_status(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	OUT	DAT_EP_STATE *,		/* ep_state		*/
	OUT	DAT_BOOLEAN *,		/* recv_idle		*/
	OUT	DAT_BOOLEAN *);		/* request_idle		*/

extern DAT_RETURN dat_ep_free(
	IN	DAT_EP_HANDLE);		/* ep_handle		*/

extern DAT_RETURN dat_ep_reset(
	IN	DAT_EP_HANDLE);		/* ep_handle		*/

/* LMR functions */

extern DAT_RETURN dat_lmr_query(
	IN	DAT_LMR_HANDLE,		/* lmr_handle		*/
	IN	DAT_LMR_PARAM_MASK,	/* lmr_param_mask	*/
	OUT	DAT_LMR_PARAM *);	/* lmr_param		*/

extern DAT_RETURN dat_lmr_free(
	IN	DAT_LMR_HANDLE);	/* lmr_handle		*/

/* Non-coherent memory functions */

extern DAT_RETURN dat_lmr_sync_rdma_read(
	IN	DAT_IA_HANDLE,		/* ia_handle		*/
	IN 	const DAT_LMR_TRIPLET *, /* local_segments	*/
	IN	DAT_VLEN);		/* num_segments		*/

extern DAT_RETURN dat_lmr_sync_rdma_write(
	IN	DAT_IA_HANDLE,		/* ia_handle		*/
	IN 	const DAT_LMR_TRIPLET *, /* local_segments	*/
	IN	DAT_VLEN);		/* num_segments		*/

/* RMR Functions */

extern DAT_RETURN dat_rmr_create(
	IN	DAT_PZ_HANDLE,		/* pz_handle		*/
	OUT	DAT_RMR_HANDLE *);	/* rmr_handle		*/

extern DAT_RETURN dat_rmr_query(
	IN	DAT_RMR_HANDLE,		/* rmr_handle		*/
	IN	DAT_RMR_PARAM_MASK,	/* rmr_param_mask	*/
	OUT	DAT_RMR_PARAM *);	/* rmr_param		*/

extern DAT_RETURN dat_rmr_bind(
	IN	DAT_RMR_HANDLE,		/* rmr_handle		*/
	IN	const DAT_LMR_TRIPLET *,	/* lmr_triplet		*/
	IN	DAT_MEM_PRIV_FLAGS,	/* mem_priv		*/
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_RMR_COOKIE,		/* user_cookie		*/
	IN	DAT_COMPLETION_FLAGS,	/* completion_flags	*/
	OUT	DAT_RMR_CONTEXT *);	/* context		*/

extern DAT_RETURN dat_rmr_free(
	IN	DAT_RMR_HANDLE);	/* rmr_handle		*/

/* PSP Functions */

extern DAT_RETURN dat_psp_create(
	IN	DAT_IA_HANDLE,		/* ia_handle		*/
	IN	DAT_CONN_QUAL,		/* conn_qual		*/
	IN	DAT_EVD_HANDLE,		/* evd_handle		*/
	IN	DAT_PSP_FLAGS,		/* psp_flags		*/
	OUT	DAT_PSP_HANDLE *);	/* psp_handle		*/

extern DAT_RETURN dat_psp_create_any(
	IN	DAT_IA_HANDLE,		/* ia_handle		*/
	OUT	DAT_CONN_QUAL *,	/* conn_qual		*/
	IN	DAT_EVD_HANDLE,		/* evd_handle		*/
	IN	DAT_PSP_FLAGS,		/* psp_flags		*/
	OUT	DAT_PSP_HANDLE *);	/* psp_handle		*/

extern DAT_RETURN dat_psp_query(
	IN	DAT_PSP_HANDLE,		/* psp_handle		*/
	IN	DAT_PSP_PARAM_MASK,	/* psp_param_mask	*/
	OUT	DAT_PSP_PARAM *);	/* psp_param		*/

extern DAT_RETURN dat_psp_free(
	IN	DAT_PSP_HANDLE);	/* psp_handle		*/

/* RSP Functions */

extern DAT_RETURN dat_rsp_create(
	IN	DAT_IA_HANDLE,		/* ia_handle		*/
	IN	DAT_CONN_QUAL,		/* conn_qual		*/
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_EVD_HANDLE,		/* evd_handle		*/
	OUT	DAT_RSP_HANDLE *);	/* rsp_handle		*/

extern DAT_RETURN dat_rsp_query(
	IN	DAT_RSP_HANDLE,		/* rsp_handle		*/
	IN	DAT_RSP_PARAM_MASK,	/* rsp_param_mask	*/
	OUT	DAT_RSP_PARAM *);	/* rsp_param		*/

extern DAT_RETURN dat_rsp_free(
	IN	DAT_RSP_HANDLE);	/* rsp_handle		*/

/* PZ Functions */

extern DAT_RETURN dat_pz_create(
	IN	DAT_IA_HANDLE,		/* ia_handle		*/
	OUT	DAT_PZ_HANDLE *);	/* pz_handle		*/

extern DAT_RETURN dat_pz_query(
	IN	DAT_PZ_HANDLE,		/* pz_handle		*/
	IN	DAT_PZ_PARAM_MASK,	/* pz_param_mask	*/
	OUT	DAT_PZ_PARAM *);	/* pz_param		*/

extern DAT_RETURN dat_pz_free(
	IN	DAT_PZ_HANDLE);		/* pz_handle		*/

/*
 * SRQ functions
 */
extern DAT_RETURN dat_ep_create_with_srq(
	IN	DAT_IA_HANDLE,		/* ia_handle		*/
	IN	DAT_PZ_HANDLE,		/* pz_handle		*/
	IN	DAT_EVD_HANDLE,		/* recv_evd_handle	*/
	IN	DAT_EVD_HANDLE,		/* request_evd_handle	*/
	IN	DAT_EVD_HANDLE,		/* connect_evd_handle	*/
	IN	DAT_SRQ_HANDLE,		/* srq_handle 		*/
	IN	const DAT_EP_ATTR *,	/* ep_attributes	*/
	OUT	DAT_EP_HANDLE *);	/* ep_handle		*/

extern DAT_RETURN dat_ep_recv_query(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	OUT	DAT_COUNT *,		/* nbufs_allocated	*/
	OUT	DAT_COUNT *);		/* bufs_alloc_span	*/

extern DAT_RETURN dat_ep_set_watermark(
	IN	DAT_EP_HANDLE,		/* ep_handle		*/
	IN	DAT_COUNT,		/* soft_high_watermark	*/
	IN	DAT_COUNT);		/* hard_high_watermark	*/

extern DAT_RETURN dat_srq_create(
	IN	DAT_IA_HANDLE,		/* ia_handle		*/
	IN	DAT_PZ_HANDLE,		/* pz_handle		*/
	IN	DAT_SRQ_ATTR *,		/* srq_attr		*/
	OUT	DAT_SRQ_HANDLE *);	/* srq_handle		*/

extern DAT_RETURN dat_srq_free(
	IN	DAT_SRQ_HANDLE);	/* srq_handle		*/

extern DAT_RETURN dat_srq_post_recv(
	IN	DAT_SRQ_HANDLE,		/* srq_handle		*/
	IN	DAT_COUNT,		/* num_segments		*/
	IN	DAT_LMR_TRIPLET *,	/* local_iov		*/
	IN	DAT_DTO_COOKIE);	/* user_cookie		*/

extern DAT_RETURN dat_srq_query(
	IN	DAT_SRQ_HANDLE,		/* srq_handle		*/
	IN	DAT_SRQ_PARAM_MASK,	/* srq_param_mask	*/
	OUT	DAT_SRQ_PARAM *);	/* srq_param		*/

extern DAT_RETURN dat_srq_resize(
	IN	DAT_SRQ_HANDLE,		/* srq_handle		*/
	IN	DAT_COUNT);		/* srq_max_recv_dto	*/

extern DAT_RETURN dat_srq_set_lw(
	IN	DAT_SRQ_HANDLE,		/* srq_handle		*/
	IN	DAT_COUNT);		/* low_watermark	*/

/*
 * DAT registry functions.
 *
 * Note the dat_ia_open and dat_ia_close functions are linked to
 * registration code which "redirects" to the appropriate provider.
 */
extern DAT_RETURN dat_registry_list_providers(
	IN	DAT_COUNT,		/* max_to_return */
	OUT	DAT_COUNT *,		/* entries_returned */
	/* dat_provider_list */
	OUT	DAT_PROVIDER_INFO *(dat_provider_list[]));
/*
 * DAT error functions.
 */
extern DAT_RETURN dat_strerror(
	IN	DAT_RETURN,		/* dat function return */
	OUT	const char **,		/* major message string */
	OUT	const char **);		/* minor message string */


#ifdef __cplusplus
}
#endif

#endif /* _DAT_H_ */

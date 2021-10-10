/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
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

#ifndef _SYS_IB_CLIENTS_SDP_MSGS_H
#define	_SYS_IB_CLIENTS_SDP_MSGS_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	__BIG_ENDIAN	_BIG_ENDIAN

/*
 * Message identifier opcodes for BSDH -- Base Sockets Direct Header
 */

/*
 * Name                               Value       Extended Header  Payload
 */
#define	SDP_MSG_MID_HELLO		0x00	/* sdp_msg_hh_t*   <none>   */
#define	SDP_MSG_MID_HELLO_ACK		0x01	/* sdp_msg_hah_t*  <none>   */
#define	SDP_MSG_MID_DISCONNECT		0x02	/* <none>	<none>   */
#define	SDP_MSG_MID_ABORT_CONN		0x03	/* <none>	<none>   */
#define	SDP_MSG_MID_SEND_SM		0x04	/* <none>	<none>   */
#define	SDP_MSG_MID_RDMA_WR_COMP	0x05	/* sdp_msg_rwch_t* <none>   */
#define	SDP_MSG_MID_RDMA_RD_COMP	0x06	/* sdp_msg_rrch_t* <none>   */
#define	SDP_MSG_MID_MODE_CHANGE		0x07	/* sdp_msg_mch_t*  <none>   */
#define	SDP_MSG_MID_SRC_CANCEL		0x08	/* <none>	<none>   */
#define	SDP_MSG_MID_SNK_CANCEL		0x09	/* <none>	<none>   */
#define	SDP_MSG_MID_SNK_CANCEL_ACK	0x0a	/* <none>	<none>   */
#define	SDP_MSG_MID_CH_RECV_BUF		0x0b	/* sdp_msg_crbh_t* <none>   */
#define	SDP_MSG_MID_CH_RECV_BUF_ACK	0x0c	/* sdp_msg_crbah_t* <none>  */
#define	SDP_MSG_MID_SUSPEND		0x0d	/* sdp_msg_sch_t*  <none>   */
#define	SDP_MSG_MID_SUSPEND_ACK		0x0e	/* <none>	<none>   */
#define	SDP_MSG_MID_SNK_AVAIL		0xfd	/* sdp_msg_snkah_t*<optional> */
#define	SDP_MSG_MID_SRC_AVAIL		0xfe	/* sdp_msg_srcah_t*<optional> */
#define	SDP_MSG_MID_DATA		0xff	/* <none>	<optional> */

/*
 * Shift number for BSDH flags.
 */
#define	SDP_MSG_FLAG_NON_FLAG (0x0)	/* no flag present */
#define	SDP_MSG_FLAG_OOB_PRES  0	/* out-of-band data present */
#define	SDP_MSG_FLAG_OOB_PEND  1	/* out-of-band data pending */
#define	SDP_MSG_FLAG_REQ_PIPE  2	/* request change to pipelined  */

/*
 * Base sockets direct header (header for all SDP messages)
 */
#pragma pack(2)
typedef struct sdp_msg_bsdh_s {
	uchar_t mid;	/* message identifier opcode (SDP_MSG_MID_*) */
	uchar_t flags;	/* flags as defined by SDP_MSG_FLAG_* */
	uint16_t recv_bufs; /* number of posted private receive buffers */
	uint32_t size;	/* length of message, including header(s) and data */
	uint32_t seq_num;	/* message sequence number */
	uint32_t seq_ack;	/* last received message sequence number */
} sdp_msg_bsdh_t;

#pragma pack()

/*
 * Hello header constants (two 8-bit constants, no conversion needed)
 */
#define	SDP_MSG_IPV4   0x40	/* (1: ipversion), (0: reserved) */
#define	SDP_MSG_IPV6   0x60	/* (1: ipversion), (0: reserved) */
#define	SDP_MSG_VERSION 0x20	/* (1: major ), (0: minor ) */
#define	SDP_MSG_VERSION_OLD 0x11 /* (1: major ), (0: minor ) */

/*
 * Hello header (BSDH + HH are contained in private data of the CM REQ MAD
 */
#pragma pack(2)
typedef struct sdp_msg_hh_s {
	/*
	 * 0-3: minor version (current spec; 0x1)
	 * 4-7: major version (current spec; 0x1)
	 */
	uchar_t version;
	/*
	 * 0-3: reserved
	 * 4-7: ip version (0x4 = ipv4, 0x6 = ipv6)
	 */
	uchar_t ip_ver;

	uchar_t rsvd_1;		/* reserved */
	uchar_t max_adv;	/* max outstanding zcopy advertisements (>0) */
	uint32_t r_rcv_size;	/* requested size of each remote receive buf */
	uint32_t l_rcv_size;	/* initial size of each local receive buffer */
	uint16_t port;		/* local port */
	uint16_t rsvd_2;	/* reserved */

	union {	/* source IP address. */
		struct {
			uint32_t addr3;	/* ipv6 96-127 */
			uint32_t addr2;	/* ipv6 64-95  */
			uint32_t addr1;	/* ipv6 32-63  */
			uint32_t addr0;	/* ipv6  0-31  */
		} ipv6;	/* 128bit ipv6 address */
		struct {
			uint32_t none2;	/* unused 96-127 */
			uint32_t none1;	/* unused 64-95  */
			uint32_t none0;	/* unused 32-63  */
			uint32_t addr;	/* ipv4    0-31  */
		} ipv4;	/* 32bit ipv4 address */
	} src;

	union {	/* destination IP address. */
		struct {
			uint32_t addr3;	/* ipv6 96-127 */
			uint32_t addr2;	/* ipv6 64-95  */
			uint32_t addr1;	/* ipv6 32-63  */
			uint32_t addr0;	/* ipv6  0-31  */
		} ipv6;	/* 128bit ipv6 address */
		struct {
			uint32_t none2;	/* unused 96-127 */
			uint32_t none1;	/* unused 64-95  */
			uint32_t none0;	/* unused 32-63  */
			uint32_t addr;	/* ipv4    0-31  */
		} ipv4;	/* 32bit ipv4 address */
	} dst;

	uchar_t rsvd_3[28];	/* reserved for future use, and zero'd */
} sdp_msg_hh_t;

#pragma pack()

/*
 * Hello acknowledgement header (BSDH + HAH are contained in private data
 * of the CM REP MAD).
 */
#pragma pack(1)
typedef struct sdp_msg_hah_s {
	/*
	 * 0-3: minor version (current spec; 0x1)
	 * 4-7: major version (current spec; 0x1)
	 */
	uchar_t version;

	uint16_t rsvd_1;	/* reserved */
	uchar_t max_adv;	/* max outstanding zcopy advertisements (>0) */
	uint32_t l_rcv_size;	/* initial size of each local receive buffer */
	uchar_t rsvd_2[172];	/* reserved for future use, and zero'd (big) */
} sdp_msg_hah_t;

#pragma pack()

/*
 * Source available header. Source notifies sink that there are buffers
 * which can be moved, using RDMA read, by the sink. The message is flowing
 * in the same direction as the data it is advertising.
 */
#pragma pack(2)
typedef struct sdp_msg_srcah_s {
	uint32_t size;	/* size, in bytes, of buffer to be RDMA'd */
	uint32_t r_key;	/* r_key needed for sink to perform RMDA read */
	uint64_t addr;	/* virtual address of buffer */
} sdp_msg_srcah_t;

#pragma pack()

/*
 * Sink available header. Sink notifies source that there are buffers
 * into which the source, using RMDA write, can move data. the message
 * is flowing in the opposite direction as the data will be moving into
 * the buffer.
 */
#pragma pack(2)
typedef struct sdp_msg_snkah_s {
	uint32_t size;	/* size, in bytes, of buffer to be RDMA'd */
	uint32_t r_key;	/* r_key needed for sink to perform RMDA read */
	uint64_t addr;	/* virtual address of buffer */
	uint32_t non_disc; /* SDP messages, containing data, not discarded */
} sdp_msg_snkah_t;

#pragma pack()

/*
 * RDMA write completion header. Notifies the data sink, which sent a
 * sink_available message that the RDMA write for the oldest outstanding
 * SNKAH message has completed.
 */
#pragma pack(2)
typedef struct sdp_msg_rwch_s {
	uint32_t size;	/* size of data RDMA'd */
} sdp_msg_rwch_t;

#pragma pack()

/*
 * RDMA read completion header. Notifies the data source, which sent a
 * source available message, that the RDMA read. Sink must RDMA the
 * entire contents of the advertised buffer, minus the data sent as
 * immediate data in the SRCAH.
 */
#pragma pack(2)
typedef struct sdp_msg_rrch_s {
	uint32_t size;	/* size of data actually RDMA'd */
} sdp_msg_rrch_t;

#pragma pack()

/*
 * Mode change header constants. (Low 4 bits are reserved, next 3 bits
 * are cast to integer and determine mode, highest bit determines send
 * or recv half of the receiving peers connection.)
 */
#define	SDP_MSG_MCH_BUFF_RECV 0x0
#define	SDP_MSG_MCH_COMB_RECV 0x1
#define	SDP_MSG_MCH_PIPE_RECV 0x2
#define	SDP_MSG_MCH_BUFF_SEND 0x8
#define	SDP_MSG_MCH_COMB_SEND 0x9
#define	SDP_MSG_MCH_PIPE_SEND 0xA

/*
 * Mode change header. notification of a flowcontrol mode transition.
 * the receiver is required to change mode upon notification.
 */
#pragma pack(2)
typedef struct sdp_msg_mch_s {
	/*
	 * 0-3: reserved
	 * 4-6: flow control modes
	 * 7: send/recv flow control
	 */
	uchar_t flags;
	uchar_t reserved[3];	/* reserved for future use */
} sdp_msg_mch_t;

#pragma pack()

/*
 * Change receive buffer size header. Request for the peer to change the
 * size of its private receive buffers.
 */
#pragma pack(2)
typedef struct sdp_msg_crbh_s {
	uint32_t size;	/* desired receive buffer size */
} sdp_msg_crbh_t;

#pragma pack()

/*
 * Change receive buffer size acknowledgement header. Response to the
 * peers request for a receive buffer size change, containing the
 * actual size size of the receive buffer.
 */
#pragma pack(2)
typedef struct sdp_msg_crbah_t {
	uint32_t size;	/* actual receive buffer size */
} sdp_msg_crbah_t;

#pragma pack()

/*
 * Suspend communications header. Request for the peer to suspend
 * communication in preperation for a socket duplication. The message
 * contains the new service_id of the connection.
 */
#pragma pack(2)
typedef struct sdp_msg_sch_t {
	uint64_t service_id;	/* new service ID */
} sdp_msg_sch_t;

#pragma pack()

/*
 * Header flags accessor functions.
 */
#define	SDP_MSG_HDR_GET_FLAG(bsdh, flag) \
		(((bsdh)->flags & (0x1u << (flag))) >> (flag))
#define	SDP_MSG_HDR_SET_FLAG(bsdh, flag) \
		((bsdh)->flags |= (0x1u << (flag)))
#define	SDP_MSG_HDR_CLR_FLAG(bsdh, flag) \
		((bsdh)->flags &= ~(0x1u << (flag)))

#define	SDP_MSG_HDR_GET_OOB_PRES(bsdh) \
		SDP_MSG_HDR_GET_FLAG(bsdh, SDP_MSG_FLAG_OOB_PRES)
#define	SDP_MSG_HDR_SET_OOB_PRES(bsdh) \
		SDP_MSG_HDR_SET_FLAG(bsdh, SDP_MSG_FLAG_OOB_PRES)
#define	SDP_MSG_HDR_CLR_OOB_PRES(bsdh) \
		SDP_MSG_HDR_CLR_FLAG(bsdh, SDP_MSG_FLAG_OOB_PRES)
#define	SDP_MSG_HDR_GET_OOB_PEND(bsdh) \
		SDP_MSG_HDR_GET_FLAG(bsdh, SDP_MSG_FLAG_OOB_PEND)
#define	SDP_MSG_HDR_SET_OOB_PEND(bsdh) \
		SDP_MSG_HDR_SET_FLAG(bsdh, SDP_MSG_FLAG_OOB_PEND)
#define	SDP_MSG_HDR_CLR_OOB_PEND(bsdh) \
		SDP_MSG_HDR_CLR_FLAG(bsdh, SDP_MSG_FLAG_OOB_PEND)
#define	SDP_MSG_HDR_GET_REQ_PIPE(bsdh) \
		SDP_MSG_HDR_GET_FLAG(bsdh, SDP_MSG_FLAG_REQ_PIPE)
#define	SDP_MSG_HDR_SET_REQ_PIPE(bsdh) \
		SDP_MSG_HDR_SET_FLAG(bsdh, SDP_MSG_FLAG_REQ_PIPE)
#define	SDP_MSG_HDR_CLR_REQ_PIPE(bsdh) \
		SDP_MSG_HDR_CLR_FLAG(bsdh, SDP_MSG_FLAG_REQ_PIPE)

#define	SDP_MSG_MCH_GET_MODE(mch) (((mch)->flags & 0xf0) >> 4)
#define	SDP_MSG_MCH_SET_MODE(mch, value) \
		((mch)->flags = (((mch)->flags & 0x0f) | (value << 4)))


/*
 * endian conversions
 */


#ifdef _LITTLE_ENDIAN

#if defined(lint) || defined(__lint)
int32_t sdp_msg_swap_bsdh(sdp_msg_bsdh_t *header);
int32_t sdp_msg_swap_hh(sdp_msg_hh_t *header);
int32_t sdp_msg_swap_hah(sdp_msg_hah_t *header);
int32_t sdp_msg_swap_srcah(sdp_msg_srcah_t *header);
int32_t sdp_msg_swap_snkah(sdp_msg_snkah_t *header);
int32_t sdp_msg_swap_rwch(sdp_msg_rwch_t *header);
int32_t sdp_msg_swap_rrch(sdp_msg_rrch_t *header);
int32_t sdp_msg_swap_mch(sdp_msg_mch_t *header);
int32_t sdp_msg_swap_crbh(sdp_msg_crbh_t *header);
int32_t sdp_msg_swap_crbah(sdp_msg_crbah_t *header);
int32_t sdp_msg_swap_sch(sdp_msg_sch_t *header);

#else

/*
 * SDP header endian byte swapping
 */
static int32_t
sdp_msg_swap_bsdh(sdp_msg_bsdh_t *header)
{
	SDP_CHECK_NULL(header, EINVAL);

	header->recv_bufs	= ntohs(header->recv_bufs);
	header->size		= ntohl(header->size);
	header->seq_num		= ntohl(header->seq_num);
	header->seq_ack		= ntohl(header->seq_ack);

	return (0);
}   /* sdp_msg_swap_bsdh */
#pragma inline(sdp_msg_swap_bsdh)


/*
 * SDP header endian byte swapping
 */
static int32_t
sdp_msg_swap_hh(sdp_msg_hh_t *header)
{
	SDP_CHECK_NULL(header, EINVAL);

	header->r_rcv_size	= ntohl(header->r_rcv_size);
	header->l_rcv_size	= ntohl(header->l_rcv_size);
	header->port		= ntohs(header->port);
	header->src.ipv6.addr0	= ntohl(header->src.ipv6.addr0);
	header->src.ipv6.addr1	= ntohl(header->src.ipv6.addr1);
	header->src.ipv6.addr2	= ntohl(header->src.ipv6.addr2);
	header->src.ipv6.addr3	= ntohl(header->src.ipv6.addr3);
	header->dst.ipv6.addr0	= ntohl(header->dst.ipv6.addr0);
	header->dst.ipv6.addr1	= ntohl(header->dst.ipv6.addr1);
	header->dst.ipv6.addr2	= ntohl(header->dst.ipv6.addr2);
	header->dst.ipv6.addr3	= ntohl(header->dst.ipv6.addr3);

	return (0);
}   /* sdp_msg_swap_hh */

#pragma inline(sdp_msg_swap_hh)

static int32_t
sdp_msg_swap_hah(sdp_msg_hah_t *header)
{
	SDP_CHECK_NULL(header, EINVAL);

	header->l_rcv_size = ntohl(header->l_rcv_size);

	return (0);
}   /* sdp_msg_swap_hah */

#pragma inline(sdp_msg_swap_hah)

static int32_t
sdp_msg_swap_srcah(sdp_msg_srcah_t *header)
{
	SDP_CHECK_NULL(header, EINVAL);

	header->size	= ntohl(header->size);
	header->r_key	= ntohl(header->r_key);
	header->addr	= BSWAP_64(header->addr);
	return (0);
}   /* sdp_msg_swap_srcah */

#pragma inline(sdp_msg_swap_srcah)

static int32_t
sdp_msg_swap_snkah(sdp_msg_snkah_t *header)
{
	SDP_CHECK_NULL(header, EINVAL);

	header->size		= ntohl(header->size);
	header->r_key		= ntohl(header->r_key);
	header->addr		= BSWAP_64(header->addr);
	header->non_disc	= ntohl(header->non_disc);

	return (0);
}   /* sdp_msg_swap_snkah */

#pragma inline(sdp_msg_swap_snkah)

static int32_t
sdp_msg_swap_rwch(sdp_msg_rwch_t *header)
{
	SDP_CHECK_NULL(header, EINVAL);

	header->size = ntohl(header->size);

	return (0);
}   /* sdp_msg_swap_rwch */

#pragma inline(sdp_msg_swap_rwch)

static int32_t
sdp_msg_swap_rrch(sdp_msg_rrch_t *header)
{
	SDP_CHECK_NULL(header, EINVAL);

	header->size = ntohl(header->size);

	return (0);
}   /* sdp_msg_swap_rrch */

#pragma inline(sdp_msg_swap_rrch)

static int32_t
sdp_msg_swap_mch(sdp_msg_mch_t *header)
{
	return (0);
}   /* sdp_msg_swap_mch */

#pragma inline(sdp_msg_swap_mch)

static int32_t
sdp_msg_swap_crbh(sdp_msg_crbh_t *header)
{
	SDP_CHECK_NULL(header, EINVAL);

	header->size = ntohl(header->size);

	return (0);
}   /* sdp_msg_swap_crbh */

#pragma inline(sdp_msg_swap_crbh)

static int32_t
sdp_msg_swap_crbah(sdp_msg_crbah_t *header)
{
	SDP_CHECK_NULL(header, EINVAL);

	header->size = ntohl(header->size);

	return (0);
}   /* sdp_msg_swap_crbah */

#pragma inline(sdp_msg_swap_crbah)

static int32_t
sdp_msg_swap_sch(sdp_msg_sch_t *header)
{
	SDP_CHECK_NULL(header, EINVAL);

	header->service_id = BSWAP_64(header->service_id);

	return (0);
}   /* sdp_msg_swap_sch */

#pragma inline(sdp_msg_swap_sch)

#endif	/* defined(lint) || defined(__lint) */

#define	sdp_msg_host_to_wire_bsdh  sdp_msg_swap_bsdh
#define	sdp_msg_wire_to_host_bsdh  sdp_msg_swap_bsdh
#define	sdp_msg_host_to_wire_hh    sdp_msg_swap_hh
#define	sdp_msg_wire_to_host_hh    sdp_msg_swap_hh
#define	sdp_msg_host_to_wire_hah   sdp_msg_swap_hah
#define	sdp_msg_wire_to_host_hah   sdp_msg_swap_hah
#define	sdp_msg_host_to_wire_srcah sdp_msg_swap_srcah
#define	sdp_msg_wire_to_host_srcah sdp_msg_swap_srcah
#define	sdp_msg_host_to_wire_snkah sdp_msg_swap_snkah
#define	sdp_msg_wire_to_host_snkah sdp_msg_swap_snkah
#define	sdp_msg_host_to_wire_rwch  sdp_msg_swap_rwch
#define	sdp_msg_wire_to_host_rwch  sdp_msg_swap_rwch
#define	sdp_msg_host_to_wire_rrch  sdp_msg_swap_rrch
#define	sdp_msg_wire_to_host_rrch  sdp_msg_swap_rrch
#define	sdp_msg_host_to_wire_mch   sdp_msg_swap_mch
#define	sdp_msg_wire_to_host_mch   sdp_msg_swap_mch
#define	sdp_msg_host_to_wire_crbh  sdp_msg_swap_crbh
#define	sdp_msg_wire_to_host_crbh  sdp_msg_swap_crbh
#define	sdp_msg_host_to_wire_crbah sdp_msg_swap_crbah
#define	sdp_msg_wire_to_host_crbah sdp_msg_swap_crbah
#define	sdp_msg_host_to_wire_sch   sdp_msg_swap_sch
#define	sdp_msg_wire_to_host_sch   sdp_msg_swap_sch

#else /* big endian */

#if !defined(__BIG_ENDIAN)
#warning "assuming big endian architecture, but it's not defined!!"
#endif

#define	sdp_msg_host_to_wire_srcah(x) (0)
#define	sdp_msg_wire_to_host_srcah(x) (0)

#define	sdp_msg_host_to_wire_bsdh(x)  (0)
#define	sdp_msg_wire_to_host_bsdh(x)  (0)
#define	sdp_msg_host_to_wire_hh(x)    (0)
#define	sdp_msg_wire_to_host_hh(x)    (0)
#define	sdp_msg_host_to_wire_hah(x)   (0)
#define	sdp_msg_wire_to_host_hah(x)   (0)
#define	sdp_msg_host_to_wire_snkah(x) (0)
#define	sdp_msg_wire_to_host_snkah(x) (0)
#define	sdp_msg_host_to_wire_rwch(x)  (0)
#define	sdp_msg_wire_to_host_rwch(x)  (0)
#define	sdp_msg_host_to_wire_rrch(x)  (0)
#define	sdp_msg_wire_to_host_rrch(x)  (0)
#define	sdp_msg_host_to_wire_mch(x)   (0)
#define	sdp_msg_wire_to_host_mch(x)   (0)
#define	sdp_msg_host_to_wire_crbh(x)  (0)
#define	sdp_msg_wire_to_host_crbh(x)  (0)
#define	sdp_msg_host_to_wire_crbah(x) (0)
#define	sdp_msg_wire_to_host_crbah(x) (0)
#define	sdp_msg_host_to_wire_sch(x)   (0)
#define	sdp_msg_wire_to_host_sch(x)   (0)

#endif


/*
 * Miscellaneous message related definitions.
 */

/*
 * Connection messages
 */
#pragma pack(2)
typedef struct sdp_msg_hello_s {
	sdp_msg_bsdh_t bsdh;	/* base sockets direct header */
	sdp_msg_hh_t hh;	/* hello message header */
} sdp_msg_hello_t;

#pragma pack()

#pragma pack(2)
typedef struct sdp_msg_hello_ack_s {
	sdp_msg_bsdh_t bsdh;	/* base sockets direct header */
	sdp_msg_hah_t hah;	/* hello ack message header */
} sdp_msg_hello_ack_t;

#pragma pack()

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_MSGS_H */

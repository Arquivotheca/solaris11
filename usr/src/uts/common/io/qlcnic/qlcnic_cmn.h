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
 * Copyright 2010 QLogic Corporation. All rights reserved.
 */

#ifndef _QLCNIC_CMN_H_
#define	_QLCNIC_CMN_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef sun
#include "qlcnic_config.h"
#include "qlcnic_compiler_defs.h"
#endif

#define	IP_ALIGNMENT_BYTES		2  /* make ip aligned on 16byteaddr */
#define	P3_MAX_MTU			(9600)
#define	QLCNIC_ETHERMTU			1500
#define	QLCNIC_MAX_ETHERHDR		32 /* This contains some padding */
#define	QLCNIC_MAX_ETHHDR		14

#define	QLCNIC_RX_NORMAL_BUF_MAX_LEN	(QLCNIC_MAX_ETHERHDR + QLCNIC_ETHERMTU)
#define	QLCNIC_P3_RX_JUMBO_BUF_MAX_LEN	(QLCNIC_MAX_ETHERHDR + P3_MAX_MTU)

#define	MAX_RX_LRO_BUFFER_LENGTH	((8*1024) - 512)
#define	RX_LRO_DMA_MAP_LEN		(MAX_RX_LRO_BUFFER_LENGTH -\
					    IP_ALIGNMENT_BYTES)

/* Opcodes to be used with the commands */
#define	TX_ETHER_PKT	0x01
/* The following opcodes are for IP checksum    */
#define	TX_TCP_PKT		0x02
#define	TX_UDP_PKT		0x03
#define	TX_IP_PKT		0x04
#define	TX_TCP_LSO		0x05
#define	TX_TCP_LSO6		0x06
#define	TX_IPSEC		0x07
#define	TX_IPSEC_CMD		0x0a
#define	TX_TCPV6_PKT		0x0b
#define	TX_UDPV6_PKT		0x0c

#define	QLCNIC_MAC_NOOP		0
#define	QLCNIC_MAC_ADD		1
#define	QLCNIC_MAC_DEL		2

/* The following opcodes are for internal consumption. */
#define	QLCNIC_CONTROL_OP		0x10
#define	PEGNET_REQUEST		0x11
#define	QLCNIC_HOST_REQUEST		0x13
#define	QLCNIC_REQUEST		0x14
#define	QLCNIC_LRO_REQUEST	0x15

#define	QLCNIC_MAC_EVENT		0x1

#define	QLCNIC_IP_UP		2
#define	QLCNIC_IP_DOWN		3

/*
 * Data bit definitions.
 */
#define	BIT_0	0x1
#define	BIT_1	0x2
#define	BIT_2	0x4
#define	BIT_3	0x8
#define	BIT_4	0x10
#define	BIT_5	0x20
#define	BIT_6	0x40
#define	BIT_7	0x80
#define	BIT_8	0x100
#define	BIT_9	0x200
#define	BIT_10	0x400
#define	BIT_11	0x800
#define	BIT_12	0x1000
#define	BIT_13	0x2000
#define	BIT_14	0x4000
#define	BIT_15	0x8000

enum {
	QLCNIC_H2C_OPCODE_START = 0,
	QLCNIC_H2C_OPCODE_CONFIG_RSS,
	QLCNIC_H2C_OPCODE_CONFIG_RSS_TBL,
	QLCNIC_H2C_OPCODE_CONFIG_INTR_COALESCE,
	QLCNIC_H2C_OPCODE_CONFIG_LED,
	QLCNIC_H2C_OPCODE_CONFIG_PROMISCUOUS,
	QLCNIC_H2C_OPCODE_CONFIG_L2_MAC,
	QLCNIC_H2C_OPCODE_LRO_REQUEST,
	QLCNIC_H2C_OPCODE_GET_SNMP_STATS,
	QLCNIC_H2C_OPCODE_PROXY_START_REQUEST,
	QLCNIC_H2C_OPCODE_PROXY_STOP_REQUEST,
	QLCNIC_H2C_OPCODE_PROXY_SET_MTU,
	QLCNIC_H2C_OPCODE_PROXY_SET_VPORT_MISS_MODE,
	QLCNIC_H2P_OPCODE_GET_FINGER_PRINT_REQUEST,
	QLCNIC_H2P_OPCODE_INSTALL_LICENSE_REQUEST,
	QLCNIC_H2P_OPCODE_GET_LICENSE_CAPABILITY_REQUEST,
	QLCNIC_H2C_OPCODE_GET_NET_STATS,
	QLCNIC_H2C_OPCODE_PROXY_UPDATE_P2V,
	QLCNIC_H2C_OPCODE_CONFIG_IPADDR,
	QLCNIC_H2C_OPCODE_CONFIG_LOOPBACK,
	QLCNIC_H2C_OPCODE_PROXY_STOP_DONE,
	QLCNIC_H2C_OPCODE_GET_LINKEVENT,
	QLCNIC_C2C_OPCODE,
	QLCNIC_H2C_OPCODE_CONFIG_BRIDGING,
	QLCNIC_H2C_OPCODE_CONFIG_HW_LRO,
	QLCNIC_H2C_OPCODE_LAST
};

#define	VPORT_MISS_MODE_DROP			0 /* drop all unmatched */
#define	VPORT_MISS_MODE_ACCEPT_ALL		1 /* accept all packets */
#define	VPORT_MISS_MODE_ACCEPT_MULTI	2 /* accept unmatched multicast */

#ifdef QLCNIC_RSS
#define	RSS_CNTRL_CMD		0x20
#endif

#define	DESC_CHAIN		0xFF /* descriptor command continuation */

#define	MAX_BUFFERS_PER_CMD		16
#define	MAX_BUFFERS_PER_DESC	4

#define	DUMMY_BUF_UNINIT	0x55555555
#define	DUMMY_BUF_INIT		0

/*
 * Following are the states of the Phantom. Phantom will set them and
 * Host will read to check if the fields are correct.
 */
#define	PHAN_INITIALIZE_START		0xff00
#define	PHAN_INITIALIZE_FAILED		0xffff
#define	PHAN_INITIALIZE_COMPLETE	0xff01

/* Host writes the following to notify that it has done the init-handshake */
#define	PHAN_INITIALIZE_ACK			0xf00f

/* Following defines will be used in the status descriptor */
#define	TX_ETHER_PKT_COMPLETE  0xB  /* same for both commands */

#define	NUM_RCV_DESC_RINGS		2 /* No of Rcv Descriptor contexts */

/* descriptor types */
#define	RCV_DESC_NORMAL			0x01
#define	RCV_DESC_JUMBO			0x02
#define	RCV_DESC_LRO			0x04
#define	RCV_DESC_NORMAL_CTXID	0
#define	RCV_DESC_JUMBO_CTXID	1
#define	RCV_DESC_LRO_CTXID		2

#define	RCV_DESC_TYPE(ID) \
	((ID == RCV_DESC_JUMBO_CTXID) ? RCV_DESC_JUMBO :  \
	    ((ID == RCV_DESC_LRO_CTXID) ? RCV_DESC_LRO : (RCV_DESC_NORMAL)))

#define	RCV_DESC_TYPE_NAME(ID) \
	((ID	==	RCV_DESC_JUMBO_CTXID)	?	"Jumbo"	:	\
	(ID == RCV_DESC_LRO_CTXID)    ? "LRO"    :  \
	(ID == RCV_DESC_NORMAL_CTXID) ? "Normal" : "Unknown")

#define	MAX_CMD_DESCRIPTORS		16384
#define	DEFAULT_CMD_DESCRIPTORS		2048
#define	DEFAULT_CMD_DESCRIPTORS_DMA_HDLS	8192
#define	MAX_CMD_DESCRIPTORS_DMA_HDLS	(64 * 1024)

#define	MAX_RCV_DESCRIPTORS		32768
#define	DEFAULT_RCV_DESCRIPTORS		8192
#define	DEFAULT_JUMBO_RCV_DESCRIPTORS	1024
#define	MAX_LRO_RCV_DESCRIPTORS		16

#define	QLCNIC_MAX_SUPPORTED_RDS_SIZE	(32 * 1024)
#define	QLCNIC_MAX_SUPPORTED_JUMBO_RDS_SIZE	(4 * 1024)

#define	DEFAULT_STATUS_RING_SIZE	2048
#define	MAX_STATUS_RING_SIZE		4096
#define	PHAN_PEG_RCV_INITIALIZED		0xff01
#define	PHAN_PEG_RCV_START_INITIALIZE	0xff00

#define	get_next_index(index, length)  ((((index)  + 1) == length)?0:(index) +1)

#define	get_index_range(index, length, count)	\
	((((index) + (count)) >= length)? \
		(((index)  + (count))-(length)):((index) + (count)))

#define	QLCNIC_FLOW_TICKS_PER_SEC    2048
#define	QLCNIC_FLOW_TO_TV_SHIFT_SEC  11
#define	QLCNIC_FLOW_TO_TV_SHIFT_USEC 9
#define	QLCNIC_FLOW_TICK_USEC   (1000000ULL/QLCNIC_FLOW_TICKS_PER_SEC)
#define	QLCNIC_GLOBAL_TICKS_PER_SEC  (4*QLCNIC_FLOW_TICKS_PER_SEC)
#define	QLCNIC_GLOBAL_TICK_USEC (1000000ULL/QLCNIC_GLOBAL_TICKS_PER_SEC)


/*
 * Following data structures describe the descriptors that will be used.
 * Added fileds of tcpHdrSize and ipHdrSize, The driver needs to do it only when
 * we are doing LSO (above the 1500 size packet) only.
 * This is an overhead but we need it. Let me know if you have questions.
 */

/*
 * the size of reference handle been changed to 16 bits to pass the MSS fields
 * for the LSO packet
 */

#define	FLAGS_MCAST				0x01
#define	FLAGS_LSO_ENABLED			0x02
#define	FLAGS_IPSEC_SA_ADD			0x04
#define	FLAGS_IPSEC_SA_DELETE			0x08
#define	FLAGS_VLAN_TAGGED			0x10

#if QLCNIC_CONF_PROCESSOR == QLCNIC_CONF_X86

#ifndef U64
typedef unsigned long long U64;
typedef uint32_t U32;
typedef uint16_t U16;
typedef uint8_t  U8;
#endif

#endif

#define	NUM_SUPPORTED_RINGSETS	4
#define	MAX_RING_CTX			4
#define	QLCNIC_CTX_SIGNATURE		0xdee0
#define	QLCNIC_CTX_RESET			0xbad0
#define	QLCNIC_CTX_D3_RESET		0xacc0

/* define opcode for ctx_msg */
#define	RX_PRODUCER				0
#define	RX_PRODUCER_JUMBO		1
#define	RX_PRODUCER_LRO			2
#define	TX_PRODUCER				3
#define	UPDATE_STATUS_CONSUMER	4
#define	RESET_CTX				5

#define	NUM_DB_CODE				6

#define	QLCNIC_RCV_PRODUCER(ringid)	(ringid)
#define	QLCNIC_CMD_PRODUCER			TX_PRODUCER
#define	QLCNIC_RCV_STATUS_CONSUMER		UPDATE_STATUS_CONSUMER

typedef struct __msg
{
    uint32_t  PegId:2,   /* 0x2 for tx and 01 for rx */
			    privId:1, /* must be 1 */
			    Count:15, /* for doorbell */
			    CtxId:10, /* Ctx_id */
			    Opcode:4; /* opcode */
}ctx_msg, CTX_MSG, *PCTX_MSG;

typedef struct __int_msg
{
    uint32_t  Count:18, /* INT */
			    ConsumerIdx:10,
			    CtxId:4; /* Ctx_id */

}int_msg, INT_MSG, *PINT_MSG;

/* For use in CRB_MPORT_MODE */
#define	MPORT_SINGLE_FUNCTION_MODE	0x1111
#define	MPORT_MULTI_FUNCTION_MODE	0x2222

typedef struct _RcvContext
{
	uint32_t		RcvRingAddrLo;
	uint32_t		RcvRingAddrHi;
	uint32_t		RcvRingSize;
	uint32_t		Rsrv;
}RcvContext;

typedef struct PREALIGN(64) _RingContext
{

	/* one command ring */
	uint64_t		CMD_CONSUMER_OFFSET;
	uint32_t		CmdRingAddrLo;
	uint32_t		CmdRingAddrHi;
	uint32_t		CmdRingSize;
	uint32_t		Rsrv;

	/* three receive rings */
	RcvContext		RcvContext[3];

	/* one status ring */
	uint32_t		StsRingAddrLo;
	uint32_t		StsRingAddrHi;
	uint32_t		StsRingSize;

	uint32_t		CtxId;

	uint64_t		D3_STATE_REGISTER;
	uint32_t		DummyDmaAddrLo;
	uint32_t		DummyDmaAddrHi;

}POSTALIGN(64) RingContext, RING_CTX, *PRING_CTX;

#ifdef QLCNIC_RSS
/*
 * RSS_SreInfo{} has the information for SRE to calculate the hash value
 * Will be passed by the host=> as part of comd descriptor...
 */

#if QLCNIC_CONF_PROCESSOR == QLCNIC_CONF_X86
typedef struct _RSS_SreInfo {
	U32		HashKeySize;
	U32		HashInformation;
	char	key[40];
}RSS_SreInfo;
#endif

/*
 * The following Descriptor is used to send RSS commands to the
 * PEG.... to be do the SRE registers..
 */
typedef struct PREALIGN(64) _rssCmdDesc
{

	/*
	 * To keep the opcode at the same location as
	 * the cmdDescType0, we will have to breakup the key into
	 * 2 areas.... Dont like it but for now will do... FSL
	 */

#if QLCNIC_CONF_PROCESSOR == QLCNIC_CONF_X86
	U8		Key0[16];

	U64		HashMethod:32,
			HashKeySize:8,
			Unused:	16,
			opcode:8;

	U8		Key1[24];
	U64		Unused1;
	U64		Unused2;
#else

	qlcnic_msgword_t		Key0[2];
	qlcnic_halfmsgword_t	HashMethod;
	qlcnic_halfmsgword_t
						HashKeySize:8,
						Unused:16,
						opcode:8;

	qlcnic_msgword_t    Key1[3];
	qlcnic_msgword_t    Unused1;
	qlcnic_msgword_t    Unused2;

#endif

} POSTALIGN(64) rssCmdDesc_t;


#endif /* QLCNIC_RSS */


#define	qlcnic_set_tx_vlan_tci(cmd_desc, v)	\
	(cmd_desc)->vlan_TCI = HOST_TO_LE_16(v);

#define	qlcnic_set_cmd_desc_port(cmd_desc, var)	\
	((cmd_desc)->port_ctxid |= ((var) & 0x0F))
#define	qlcnic_set_cmd_desc_ctxid(cmd_desc, var)	\
	((cmd_desc)->port_ctxid |= ((var) << 4 & 0xF0))

#define	qlcnic_set_tx_port(_desc, _port) \
	(_desc)->port_ctxid = ((_port) & 0xf) | (((_port) << 4) & 0xf0)

#define	qlcnic_set_tx_flags_opcode(_desc, _flags, _opcode) \
	(_desc)->flags_opcode = \
	HOST_TO_LE_16(((_flags) & 0x7f) | (((_opcode) & 0x3f) << 7))

#define	qlcnic_set_tx_frags_len(_desc, _frags, _len) \
	(_desc)->nfrags_length = \
	HOST_TO_LE_32(((_frags) & 0xff) | (((_len) & 0xffffff) << 8))

typedef struct PREALIGN(64) cmdDescType0
{
	uint8_t tcp_hdr_offset;	/* For LSO only */
	uint8_t ip_hdr_offset;	/* For LSO only */
	uint16_t flags_opcode;	/* 12:7 opcode, 6:0 flags, rest unsed */
	uint32_t nfrags_length;	/* 31:8 total len, 7:0 frag count */

	uint64_t addr_buffer2;

	uint16_t reference_handle;
	uint16_t mss;
	uint8_t port_ctxid;		/* 7:4 ctxid 3:0 port */
	uint8_t total_hdr_length;	/* LSO only : MAC+IP+TCP Hdr size */
	uint16_t conn_id;		/* IPSec offoad only */

	uint64_t addr_buffer3;
	uint64_t addr_buffer1;

	uint16_t buffer_length[4];

	uint64_t addr_buffer4;

	uint32_t reserved2;
	uint16_t reserved;
	uint16_t vlan_TCI;

} POSTALIGN(64) cmdDescType0_t;

#define	RSS_HASHTYPE_IP_TCP	0x3

typedef struct PREALIGN(64) cmdDesc
{
	uint64_t qhdr;
	uint64_t req_hdr;
	uint64_t qwords[6];

} POSTALIGN(64) cmdDesc_t;

/* Note: sizeof(rcvDesc) should always be a mutliple of 2 */
typedef struct rcvDesc
{
	uint32_t	referenceHandle:16,
			flags:16;
	/* allocated buffer length (usually 2K) */
	uint32_t	bufferLength:32;
	uint64_t	AddrBuffer;
} rcvDesc_t;

/* opcode field in status_desc */
#define	QLCNIC_SYN_OFFLOAD		0x03
#define	QLCNIC_RXPKT_DESC		0x04
#define	QLCNIC_OLD_RXPKT_DESC		0x3f
#define	QLCNIC_RESPONSE_DESC	0x05
#define	QLCNIC_LRO_DESC		0x12

/* for status field in statusDesc_t */
#define	STATUS_NEED_CKSUM		(1)
#define	STATUS_CKSUM_OK			(2)
#define	STATUS_CKSUM_NOT_OK		(3)

/* owner bits of statusDesc_t */
#define	STATUS_OWNER_HOST		(0x1ULL << 56)
#define	STATUS_OWNER_PHANTOM		(0x2ULL << 56)

/*
 * Status descriptor:
 * 0-3 port, 4-7 status, 8-11 type, 12-27 total_length
 * 28-43 reference_handle, 44-47 protocol, 48-52 pkt_offset
 * 53-55 desc_cnt, 56-57 owner, 58-63 opcode
 */
#define	qlcnic_get_sts_port(sts_data)	\
	((sts_data) & 0x0F)
#define	qlcnic_get_sts_status(sts_data)	\
	(((sts_data) >> 4) & 0x0F)
#define	qlcnic_get_sts_type(sts_data)	\
	(((sts_data) >> 8) & 0x0F)
#define	qlcnic_get_sts_totallength(sts_data)	\
	(((sts_data) >> 12) & 0xFFFF)
#define	qlcnic_get_sts_refhandle(sts_data)	\
	(((sts_data) >> 28) & 0xFFFF)
#define	qlcnic_get_sts_prot(sts_data)	\
	(((sts_data) >> 44) & 0x0F)
#define	qlcnic_get_sts_pkt_offset(sts_data)	\
	(((sts_data) >> 48) & 0x1F)
#define	qlcnic_get_sts_desc_cnt(sts_data)	\
	(((sts_data) >> 53) & 0x7)
#define	qlcnic_get_sts_opcode(sts_data)	\
	(((sts_data) >> 58) & 0x03F)


typedef struct PREALIGN(16) statusDesc {
	uint64_t	status_desc_data[2];
} POSTALIGN(16) statusDesc_t;


#ifdef	QLCNIC_IPSECOFFLOAD

#define	MAX_IPSEC_SAS		1024
#define	RECEIVE_IPSEC_SA_BASE	0x8000

/*
 * IPSEC related structures and defines
 */

/* Values for DIrFlag in the ipsec_sa_t structure below: */
#define	QLCNIC_IPSEC_SA_DIR_INBOUND	1
#define	QLCNIC_IPSEC_SA_DIR_OUTBOUND	2

/* Values for Operation Field below: */
#define	QLCNIC_IPSEC_SA_AUTHENTICATE	1
#define	QLCNIC_IPSEC_SA_ENDECRYPT		2

/* Confidential Algorithm Types: */
#define	QLCNIC_IPSEC_CONF_NONE		0 /* NULL encryption? */
#define	QLCNIC_IPSEC_CONF_DES		1
#define	QLCNIC_IPSEC_CONF_RESERVED		2
#define	QLCNIC_IPSEC_CONF_3DES		3

/* Integrity algorithm (AH) types: */
#define	QLCNIC_IPSEC_INTEG_NONE	0
#define	QLCNIC_IPSEC_INTEG_MD5	1
#define	QLCNIC_IPSEC_INTEG_SHA1	2

#define	QLCNIC_PROTOCOL_OFFSET		0x9 /* from ip header begin, in bytes */
#define	QLCNIC_PKT_TYPE_AH			0x33
#define	QLCNIC_PKT_TYPE_ESP		0x32


/* 96 bits of output for MD5/SHA1 algorithms */
#define	QLCNIC_AHOUTPUT_LENGTH		12
/*
 * 8 bytes (64 bits) of ICV value for each block of DES_CBC
 * at the begin of ESP payload
 */
#define	QLCNIC_DES_ICV_LENGTH		8

#if QLCNIC_CONF_PROCESSOR == QLCNIC_CONF_X86

typedef struct PREALIGN(512) s_ipsec_sa {
	U32	SrcAddr;
	U32	SrcMask;
	U32	DestAddr;
	U32	DestMask;
	U32	Protocol:8,
		DirFlag:4,
		IntegCtxInit:2,
		ConfCtxInit:2,
		No_of_keys:8,
		Operation:8;
	U32	IntegAlg:8,
		IntegKeyLen:8,
		ConfAlg:8,
		ConfAlgKeyLen:8;
	U32	SAIndex;
	U32	SPI_Id;
	U64	Key1[124];
} POSTALIGN(512) qlcnic_ipsec_sa_t;

#else

typedef struct PREALIGN(512) s_ipsec_sa {
	qlcnic_halfmsgword_t	SrcAddr;
	qlcnic_halfmsgword_t	SrcMask;
	qlcnic_halfmsgword_t	DestAddr;
	qlcnic_halfmsgword_t	DestMask;
	qlcnic_halfmsgword_t	Protocol:8,
				DirFlag:4,
				IntegCtxInit:2,
				ConfCtxInit:2,
				No_of_keys:8,
				Operation:8;
	qlcnic_halfmsgword_t	IntegAlg:8,
				IntegKeyLen:8,
				ConfAlg:8,
				ConfAlgKeyLen:8;
	qlcnic_halfmsgword_t	SAIndex:32;
	qlcnic_halfmsgword_t	SPI_Id:32;
	/* to round up to 1K of structure */
	qlcnic_msgword_t		Key1[124];
} POSTALIGN(512) qlcnic_ipsec_sa_t;

#endif /* NOT-X86 */

/* Other common header formats that may be needed */

typedef struct _qlcnic_ip_header_s {
	U32	HdrVer:8,
		diffser:8,
		TotalLength:16;
	U32	ipId:16,
		flagfrag:16;
	U32	TTL:8,
		Protocol:8,
		Chksum:16;
	U32	srcaddr;
	U32	destaddr;
} qlcnic_ip_header_t;

typedef struct _qlcnic_ah_header_s {
	U32	NextProto:8,
		length:8,
		reserved:16;
	U32	SPI;
	U32	seqno;
	U16	ICV;
	U16	ICV1;
	U16	ICV2;
	U16	ICV3;
	U16	ICV4;
	U16	ICV5;
} qlcnic_ah_header_t;

typedef struct _qlcnic_esp_hdr_s {
	U32 SPI;
	U32 seqno;
} qlcnic_esp_hdr_t;

#endif /* QLCNIC_IPSECOFFLOAD */

/*
 * Defines for various loop counts. These determine the behaviour of the
 * system. The classic tradeoff between latency and throughput.
 */

/*
 * MAX_DMA_LOOPCOUNT : After how many interations do we start the dma for
 * the status descriptors.
 */
#define	MAX_DMA_LOOPCOUNT	(32)

/*
 * MAX_TX_DMA_LOOP_COUNT : After how many interations do we start the dma for
 * the command descriptors.
 */
#define	MAX_TX_DMA_LOOP_COUNT	1000

/*
 * MAX_RCV_BUFS : Max number Rx packets that can be buffered before DMA/INT
 */
#define	MAX_RCV_BUFS	(4096)

/*
 * XXX;shouldnt be exposed in nic_cmn.h
 * DMA_MAX_RCV_BUFS : Max number Rx packets that can be buffered before DMA
 */
#define	DMA_MAX_RCV_BUFS	(4096)

/*
 * XXX;shouldnt be exposed in nic_cmn.h
 * MAX_DMA_ENTRIES : Max number Rx dma entries can be in dma list
 */
#define	MAX_DMA_ENTRIES		(4096)


/*
 * MAX_INTR_LOOPCOUNT : After how many iterations do we interrupt the
 * host ?
 */
#define	MAX_INTR_LOOPCOUNT	(1024)

/*
 * XMIT_LOOP_THRESHOLD : How many times do we spin before we process the
 * transmit buffers.
 */
#define	XMIT_LOOP_THRESHOLD	0x20

/*
 * XMIT_DESC_THRESHOLD : How many descriptors pending before we process
 * the descriptors.
 */
#define	XMIT_DESC_THRESHOLD	0x4

/*
 * TX_DMA_THRESHOLD : When do we start the dma of the command descriptors.
 * We need these number of command descriptors, or we need to exceed the
 * loop count.   P1 only.
 */
#define	TX_DMA_THRESHOLD	16

#if defined(QLCNIC_IP_FILTER)
/*
 * Commands. Must match the definitions in nic/Linux/include/qlcnic_ioctl.h
 */
enum {
	QLCNIC_IP_FILTER_CLEAR = 1,
	QLCNIC_IP_FILTER_ADD,
	QLCNIC_IP_FILTER_DEL,
	QLCNIC_IP_FILTER_SHOW
};

#define	MAX_FILTER_ENTRIES	16

typedef struct {
	uint32_t count;
	uint32_t ip_addr[15];
} qlcnic_ip_filter_t;
#endif /* QLCNIC_IP_FILTER */

enum {
	QLCNIC_RCV_PEG_0 = 0,
	QLCNIC_RCV_PEG_1
};

#ifdef __cplusplus
}
#endif

#endif /* !_QLCNIC_CMN_H_ */

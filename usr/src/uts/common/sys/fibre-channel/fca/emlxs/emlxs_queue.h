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
 * Copyright 2010 Emulex.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _EMLXS_QUEUE_H
#define	_EMLXS_QUEUE_H

#ifdef	__cplusplus
extern "C" {
#endif


/* Queue entry defines */

/* EQ entries */
typedef struct EQE
{
#ifdef EMLXS_BIG_ENDIAN
	uint32_t	CQId: 16;
	uint32_t	MinorCode: 12;
	uint32_t	MajorCode: 3;
	uint32_t	Valid: 1;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	Valid: 1;
	uint32_t	MajorCode: 3;
	uint32_t	MinorCode: 12;
	uint32_t	CQId: 16;
#endif

} EQE_t;

typedef union
{
	uint32_t	word;
	EQE_t		entry;

} EQE_u;

#define	EQE_VALID	0x00000001  /* Mask for EQE valid */
#define	EQE_CQID	0xFFFF0000  /* Mask for EQE CQID */

/* CQ entries */
typedef struct CQE_CmplWQ
{
#ifdef EMLXS_BIG_ENDIAN
	uint16_t	RequestTag;	/* Word 0 */
	uint8_t		Status;
	uint8_t		hw_status;

	uint32_t	CmdSpecific;	/* Word 1 */
	uint32_t	Parameter;	/* Word 2 */

	uint32_t	Valid: 1;	/* Word 3 */
	uint32_t	Rsvd1: 2;
	uint32_t	XB: 1;
	uint32_t	PV: 1;
	uint32_t	Priority: 3;
	uint32_t	Code: 8;
	uint32_t	Rsvd2: 16;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint8_t		hw_status;
	uint8_t		Status;
	uint16_t	RequestTag;	/* Word 0 */

	uint32_t	CmdSpecific;	/* Word 1 */
	uint32_t	Parameter;	/* Word 2 */

	uint32_t	Rsvd2: 16;
	uint32_t	Code: 8;
	uint32_t	Priority: 3;
	uint32_t	PV: 1;
	uint32_t	XB: 1;
	uint32_t	Rsvd1: 2;
	uint32_t	Valid: 1;	/* Word 3 */
#endif
} CQE_CmplWQ_t;

typedef struct CQE_RelWQ
{
#ifdef EMLXS_BIG_ENDIAN
	uint32_t	Reserved1;	/* Word 0 */
	uint32_t	Reserved2;	/* Word 1 */

	uint16_t	WQid;		/* Word 2 */
	uint16_t	WQindex;

	uint32_t	Valid: 1;	/* Word 3 */
	uint32_t	Rsvd1: 7;
	uint32_t	Code: 8;
	uint32_t	Rsvd2: 16;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	Reserved1;	/* Word 0 */
	uint32_t	Reserved2;	/* Word 1 */

	uint16_t	WQindex;
	uint16_t	WQid;		/* Word 2 */

	uint32_t	Rsvd2: 16;
	uint32_t	Code: 8;
	uint32_t	Rsvd1: 7;
	uint32_t	Valid: 1;	/* Word 3 */
#endif
} CQE_RelWQ_t;

typedef struct CQE_UnsolRcv
{
#ifdef EMLXS_BIG_ENDIAN
	uint16_t	RQindex;	/* Word 0 */
	uint8_t		Status;
	uint8_t		Rsvd1;

	uint32_t	Rsvd2;		/* Word 1 */

	uint32_t	data_size: 16;	/* Word 2 */
	uint32_t	RQid: 10;
	uint32_t	FCFId: 6;

	uint32_t	Valid: 1;	/* Word 3 */
	uint32_t	port: 1;
	uint32_t	hdr_size: 6;
	uint32_t	Code: 8;
	uint32_t	eof: 8;
	uint32_t	sof: 8;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint8_t		Rsvd1;
	uint8_t		Status;
	uint16_t	RQindex;	/* Word 0 */

	uint32_t	Rsvd2;		/* Word 1 */

	uint32_t	FCFId: 6;
	uint32_t	RQid: 10;
	uint32_t	data_size: 16;	/* Word 2 */

	uint32_t	sof: 8;
	uint32_t	eof: 8;
	uint32_t	Code: 8;
	uint32_t	hdr_size: 6;
	uint32_t	port: 1;
	uint32_t	Valid: 1;	/* Word 3 */
#endif
} CQE_UnsolRcv_t;

/* Status defines */
#define	RQ_STATUS_SUCCESS		0x10
#define	RQ_STATUS_BUFLEN_EXCEEDED	0x11
#define	RQ_STATUS_NEED_BUFFER		0x12
#define	RQ_STATUS_FRAME_DISCARDED	0x13


typedef struct CQE_XRI_Abort
{
#ifdef EMLXS_BIG_ENDIAN
	uint16_t	Rsvd1;		/* Word 0 */
	uint8_t		Status;
	uint8_t		Rsvd2;

	uint32_t	rjtStatus;	/* Word 1 */

	uint16_t	RemoteXID;	/* Word 2 */
	uint16_t	XRI;

	uint32_t	Valid: 1;	/* Word 3 */
	uint32_t	IA: 1;
	uint32_t	BR: 1;
	uint32_t	EO: 1;
	uint32_t	Rsvd3: 4;
	uint32_t	Code: 8;
	uint32_t	Rsvd4: 16;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint8_t		Rsvd2;
	uint8_t		Status;
	uint16_t	Rsvd1;		/* Word 0 */

	uint32_t	rjtStatus;	/* Word 1 */

	uint16_t	XRI;
	uint16_t	RemoteXID;	/* Word 2 */

	uint32_t	Rsvd4: 16;
	uint32_t	Code: 8;
	uint32_t	Rsvd3: 4;
	uint32_t	EO: 1;
	uint32_t	BR: 1;
	uint32_t	IA: 1;
	uint32_t	Valid: 1;	/* Word 3 */
#endif
} CQE_XRI_Abort_t;



#define	CQE_VALID    0x80000000  /* Mask for CQE valid */

/* Defines for CQE Codes */
#define	CQE_TYPE_WQ_COMPLETION	1
#define	CQE_TYPE_RELEASE_WQE	2
#define	CQE_TYPE_UNSOL_RCV	4
#define	CQE_TYPE_XRI_ABORTED	5


typedef struct CQE_ASYNC_FCOE
{
#ifdef EMLXS_BIG_ENDIAN
	uint32_t	ref_index;	/* Word 0 */

	uint16_t	evt_type;	/* Word 1 */
	uint16_t	fcf_count;

	uint32_t	event_tag;	/* Word 2 */
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	ref_index;	/* Word 0 */

	uint16_t	fcf_count;
	uint16_t	evt_type;	/* Word 1 */

	uint32_t	event_tag;	/* Word 2 */
#endif
} CQE_ASYNC_FCOE_t;

typedef struct CQE_ASYNC_LINK_STATE
{
#ifdef EMLXS_BIG_ENDIAN
	uint8_t		port_speed;	/* Word 0 */
	uint8_t		port_duplex;
	uint8_t		link_status;
	uint8_t		phys_port;

	uint16_t	qos_link_speed;	/* Word 1 */
	uint8_t		Rsvd1;
	uint8_t		port_fault;

	uint32_t	event_tag;	/* Word 2 */
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint8_t		phys_port;
	uint8_t		link_status;
	uint8_t		port_duplex;
	uint8_t		port_speed;	/* Word 0 */

	uint8_t		port_fault;	/* Word 1 */
	uint8_t		Rsvd1;
	uint16_t	qos_link_speed;

	uint32_t	event_tag;	/* Word 2 */
#endif
} CQE_ASYNC_LINK_STATE_t;

typedef struct CQE_ASYNC_GRP_5_QOS
{
#ifdef EMLXS_BIG_ENDIAN
	uint8_t		Rsvd2;
	uint8_t		Rsvd1;
	uint8_t		Rsvd0;
	uint8_t		phys_port;	/* Word 0 */

	uint16_t	qos_link_speed;
	uint8_t		Rsvd4;
	uint8_t		Rsvd3;		/* Word 1 */

	uint32_t	event_tag;	/* Word 2 */
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint8_t		phys_port;
	uint8_t		Rsvd0;
	uint8_t		Rsvd1;
	uint8_t		Rsvd2;		/* Word 0 */

	uint8_t		Rsvd3;
	uint8_t		Rsvd4;
	uint16_t	qos_link_speed;	/* Word 1 */

	uint32_t	event_tag;	/* Word 2 */
#endif
} CQE_ASYNC_GRP_5_QOS_t;

typedef struct CQE_ASYNC
{
	/* Words 0-2 */
	union
	{
		CQE_ASYNC_LINK_STATE_t	link;
		CQE_ASYNC_FCOE_t	fcoe;
		CQE_ASYNC_GRP_5_QOS_t	qos;
	} un;

#ifdef EMLXS_BIG_ENDIAN
	uint32_t	valid: 1;
	uint32_t	async_evt: 1;
	uint32_t	Rsvd2: 6;
	uint32_t	event_type: 8;
	uint32_t	event_code: 8;
	uint32_t	Rsvd3: 8;	/* Word 3 */
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	Rsvd3: 8;
	uint32_t	event_code: 8;
	uint32_t	event_type: 8;
	uint32_t	Rsvd2: 6;
	uint32_t	async_evt: 1;
	uint32_t	valid: 1;	/* Word 3 */
#endif
} CQE_ASYNC_t;

/* port_speed defines */
#define	PHY_1GHZ_LINK			3
#define	PHY_10GHZ_LINK			4

/* event_code defines */
#define	ASYNC_EVENT_CODE_LINK_STATE	1
#define	ASYNC_EVENT_CODE_FCOE_FIP	2
#define	ASYNC_EVENT_CODE_DCBX		3
#define	ASYNC_EVENT_CODE_GRP_5		5

/* LINK_STATE - link_status defines */
#define	ASYNC_EVENT_PHYS_LINK_DOWN	0
#define	ASYNC_EVENT_PHYS_LINK_UP	1
#define	ASYNC_EVENT_LOGICAL_LINK_DOWN	2
#define	ASYNC_EVENT_LOGICAL_LINK_UP	3

/* FCOE_FIP - evt_type defines */
#define	ASYNC_EVENT_NEW_FCF_DISC	1
#define	ASYNC_EVENT_FCF_TABLE_FULL	2
#define	ASYNC_EVENT_FCF_DEAD		3
#define	ASYNC_EVENT_VIRT_LINK_CLEAR	4
#define	ASYNC_EVENT_FCF_MODIFIED	5

/* GRP_5 - evt_type defines */
#define	ASYNC_EVENT_QOS_SPEED		1

typedef struct CQE_MBOX
{
#ifdef EMLXS_BIG_ENDIAN
	uint16_t	extend_status;	/* Word 0 */
	uint16_t	cmpl_status;

	uint32_t	tag_low;	/* Word 1 */
	uint32_t	tag_high;	/* Word 2 */

	uint32_t	valid: 1;	/* Word 3 */
	uint32_t	async_evt: 1;
	uint32_t	hpi: 1;
	uint32_t	completed: 1;
	uint32_t	consumed: 1;
	uint32_t	Rsvd1: 27;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint16_t	cmpl_status;
	uint16_t	extend_status;	/* Word 0 */

	uint32_t	tag_low;	/* Word 1 */
	uint32_t	tag_high;	/* Word 2 */

	uint32_t	Rsvd1: 27;
	uint32_t	consumed: 1;
	uint32_t	completed: 1;
	uint32_t	hpi: 1;
	uint32_t	async_evt: 1;
	uint32_t	valid: 1;	/* Word 3 */
#endif
} CQE_MBOX_t;

typedef union
{
	uint32_t	word[4];

	/* Group 1 types */
	CQE_ASYNC_t	cqAsyncEntry;
	CQE_ASYNC_FCOE_t cqAsyncFCOEEntry;
	CQE_MBOX_t	cqMboxEntry;

	/* Group 2 types */
	CQE_CmplWQ_t	cqCmplEntry;
	CQE_RelWQ_t	cqRelEntry;
	CQE_UnsolRcv_t	cqUnsolRcvEntry;
	CQE_XRI_Abort_t	cqXRIEntry;
} CQE_u;

/* RQ entries */
typedef struct RQE
{
	uint32_t	AddrHi;
	uint32_t	AddrLo;

} RQE_t;


/* Definitions for WQEs */
typedef struct
{
	ULP_BDE64	Payload;
	uint32_t	PayloadLength;

#ifdef EMLXS_BIG_ENDIAN
	uint32_t	Rsvd1: 6;
	uint32_t	VF: 1;
	uint32_t	SP: 1;
	uint32_t	LocalId: 24;

	uint32_t	Rsvd2:  8;
	uint32_t	RemoteId: 24;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	LocalId: 24;
	uint32_t	SP: 1;
	uint32_t	VF: 1;
	uint32_t	Rsvd1: 6;

	uint32_t	RemoteId: 24;
	uint32_t	Rsvd2:  8;
#endif

} ELS_REQ_WQE;

typedef struct
{
	ULP_BDE64	Payload;
	uint32_t	Rsvd1[2];

#ifdef EMLXS_BIG_ENDIAN
	uint32_t	Rsvd2: 8;
	uint32_t	RemoteId: 24;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	RemoteId: 24;
	uint32_t	Rsvd2: 8;
#endif

} ELS_RSP_WQE;

typedef struct
{
	ULP_BDE64	Payload;
	uint32_t	PayloadLength;

	uint32_t	Parameter;

#ifdef EMLXS_BIG_ENDIAN
	uint32_t	Rctl: 8;
	uint32_t	Type: 8;
	uint32_t	DFctl: 8;
	uint32_t	Rsvd1: 4;
	uint32_t	la: 1;
	uint32_t	Rsvd2: 3;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	Rsvd2: 3;
	uint32_t	la: 1;
	uint32_t	Rsvd1: 4;
	uint32_t	DFctl: 8;
	uint32_t	Type: 8;
	uint32_t	Rctl: 8;
#endif

} GEN_REQ_WQE;

typedef struct
{
	ULP_BDE64	Payload;
	uint32_t	PayloadLength;

	uint32_t	Parameter;

#ifdef EMLXS_BIG_ENDIAN
	uint32_t	Rctl: 8;
	uint32_t	Type: 8;
	uint32_t	DFctl: 8;
	uint32_t	ls: 1;
	uint32_t	Rsvd1: 3;
	uint32_t	la: 1;
	uint32_t	si: 1;
	uint32_t	Rsvd2: 2;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	Rsvd2: 2;
	uint32_t	si: 1;
	uint32_t	la: 1;
	uint32_t	Rsvd1: 3;
	uint32_t	ls: 1;
	uint32_t	DFctl: 8;
	uint32_t	Type: 8;
	uint32_t	Rctl: 8;
#endif

} XMIT_SEQ_WQE;

typedef struct
{
	ULP_BDE64	Payload;
	uint32_t	PayloadLength;

	uint32_t	TotalTransferCount;
	uint32_t	Rsvd1;

} FCP_WQE;


typedef struct
{
	uint32_t	Rsvd1[3];

#ifdef EMLXS_BIG_ENDIAN
	uint32_t	Rsvd2: 16;
	uint32_t	Criteria: 8;
	uint32_t	Rsvd3: 7;
	uint32_t	IA: 1;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	IA: 1;
	uint32_t	Rsvd3: 7;
	uint32_t	Criteria: 8;
	uint32_t	Rsvd2: 16;
#endif

	uint32_t	Rsvd4[2];

} ABORT_WQE;

#define	ABORT_XRI_TAG	1	/* Abort tag is a XRITag */
#define	ABORT_ABT_TAG	2	/* Abort tag is a AbortTag */
#define	ABORT_REQ_TAG	3	/* Abort tag is a RequestTag */

typedef struct
{
#ifdef EMLXS_BIG_ENDIAN
	uint8_t		Payload0;
	uint8_t		Payload1;
	uint8_t		Payload2;
	uint8_t		Payload3;

	uint32_t	OXId: 16;
	uint32_t	RXId: 16;

	uint32_t	SeqCntLow: 16;
	uint32_t	SeqCntHigh: 16;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint8_t		Payload3;
	uint8_t		Payload2;
	uint8_t		Payload1;
	uint8_t		Payload0;

	uint32_t	RXId: 16;
	uint32_t	OXId: 16;

	uint32_t	SeqCntHigh: 16;
	uint32_t	SeqCntLow: 16;
#endif
	uint32_t	Rsvd1[2];
#ifdef EMLXS_BIG_ENDIAN
	uint32_t	XO: 1;
	uint32_t	AR: 1;
	uint32_t	PT: 1;
	uint32_t	Rsvd2: 5;
	uint32_t	RemoteId: 24;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	RemoteId: 24;
	uint32_t	Rsvd2: 5;
	uint32_t	PT: 1;
	uint32_t	AR: 1;
	uint32_t	XO: 1;
#endif

} BLS_WQE;


typedef struct
{
	uint32_t	Rsvd1[5];

#ifdef EMLXS_BIG_ENDIAN
	uint32_t	XO: 1;
	uint32_t	Rsvd2: 7;
	uint32_t	RemoteId: 24;
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	RemoteId: 24;
	uint32_t	Rsvd2: 7;
	uint32_t	XO: 1;
#endif

} CREATE_XRI_WQE;

typedef struct emlxs_wqe
{
	/* Words 0-5 */
	union
	{
		uint32_t	word[6];	/* Words 0-5: cmd specific */
		ELS_REQ_WQE	ElsCmd;		/* ELS command overlay */
		GEN_REQ_WQE	GenReq;		/* CT command overlay */
		FCP_WQE		FcpCmd;		/* FCP command overlay */
		ELS_RSP_WQE	ElsRsp;		/* ELS response overlay */
		ABORT_WQE	Abort;		/* Abort overlay */
		BLS_WQE		BlsRsp;		/* BLS overlay */
		CREATE_XRI_WQE	CreateXri;	/* Create XRI */
		XMIT_SEQ_WQE	XmitSeq;	/* Xmit Sequence */
	} un;

#ifdef EMLXS_BIG_ENDIAN
	/* Word 6 */
	uint16_t	ContextTag;	/* Context Tag */
	uint16_t	XRITag;		/* XRItag */
	/* Word 7 */
	uint32_t	Timer: 8;	/* TOV */
	uint32_t	Rsvd2: 1;
	uint32_t	ERP: 1;		/* ERP */
	uint32_t	PU: 2;		/* PU */
	uint32_t	Rsvd1: 1;
	uint32_t	Class: 3;	/* COS */
	uint32_t	Command: 8;	/* Command Code */
	uint32_t	Status: 4;	/* Final Status */
	uint32_t	ContextType: 2;	/* Context Type */
	uint32_t	Rsvd0: 2;
	/* Word 8 */
	uint32_t	AbortTag;	/* Abort Tag */
	/* Word 9 */
	uint16_t	OXId;		/* OXId on xmitted rsp */
	uint16_t	RequestTag;	/* Request Tag */
	/* Word 10 */
	uint32_t	CCP: 8;		/* CCP */
	uint32_t	CCPE: 1;	/* CCPEnabled */
	uint32_t	Rsvd6: 1;
	uint32_t	XC: 1;		/* Exchange Create */
	uint32_t	Rsvd5: 1;
	uint32_t	PV: 1;		/* PRIValid */
	uint32_t	PRI: 3;		/* PRI */
	uint32_t	Rsvd4: 16;
	/* Word 11 */
	uint32_t	Rsvd9: 6;
	uint32_t	CQId: 10;	/* CompletionQueueID */
	uint32_t	Rsvd8: 8;
	uint32_t	WQEC: 1;	/* Request WQE consumed CQE */
	uint32_t	Rsvd7: 1;
	uint32_t	ELSId: 2;
	uint32_t	CmdType: 4;	/* Command Type */
#endif
#ifdef EMLXS_LITTLE_ENDIAN
	/* Word 6 */
	uint16_t	XRITag;		/* XRItag */
	uint16_t	ContextTag;	/* Context Tag */
	/* Word 7 */
	uint32_t	Rsvd0: 2;
	uint32_t	ContextType: 2;	/* Context Type */
	uint32_t	Status: 4;	/* Final Status */
	uint32_t	Command: 8;	/* Command Code */
	uint32_t	Class: 3;	/* COS */
	uint32_t	Rsvd1: 1;
	uint32_t	PU: 2;		/* PU */
	uint32_t	ERP: 1;		/* ERP */
	uint32_t	Rsvd2: 1;
	uint32_t	Timer: 8;	/* TOV */
	/* Word 8 */
	uint32_t	AbortTag;	/* Abort Tag */
	/* Word 9 */
	uint16_t	RequestTag;	/* Request Tag */
	uint16_t	OXId;		/* OXId on xmitted rsp */
	/* Word 10 */
	uint32_t	Rsvd4: 16;
	uint32_t	PRI: 3;		/* PRI */
	uint32_t	PV: 1;		/* PRIValid */
	uint32_t	Rsvd5: 1;
	uint32_t	XC: 1;		/* Exchange Create */
	uint32_t	Rsvd6: 1;
	uint32_t	CCPE: 1;	/* CCPEnabled */
	uint32_t	CCP: 8;		/* CCP */
	/* Word 11 */
	uint32_t	CmdType: 4;	/* Command Type */
	uint32_t	ELSId: 2;
	uint32_t	Rsvd7: 1;
	uint32_t	WQEC: 1;	/* Request WQE consumed CQE */
	uint32_t	Rsvd8: 8;
	uint32_t	CQId: 10;	/* CompletionQueueID */
	uint32_t	Rsvd9: 6;
#endif

	/* Words 12-15 */
	uint32_t	CmdSpecific[4];	/* Word12-15: commandspecific */
} emlxs_wqe_t;

/* Defines for ContextType */
#define	WQE_RPI_CONTEXT		0
#define	WQE_VPI_CONTEXT		1
#define	WQE_VFI_CONTEXT		2
#define	WQE_FCFI_CONTEXT	3

/* Defines for CmdType */
#define	WQE_TYPE_FCP_DATA_IN	0x00
#define	WQE_TYPE_FCP_DATA_OUT	0x01
#define	WQE_TYPE_ELS		0x0C
#define	WQE_TYPE_GEN		0x08
#define	WQE_TYPE_ABORT		0x08
#define	WQE_TYPE_MASK_FIP	0x01

/* Defines for ELSId */
#define	WQE_ELSID_FLOGI		0x03
#define	WQE_ELSID_FDISC		0x02
#define	WQE_ELSID_LOGO		0x01
#define	WQE_ELSID_CMD		0x0

/* RQB */
#define	RQB_HEADER_SIZE		32
#define	RQB_DATA_SIZE		2048
#define	RQB_COUNT		256

#define	EMLXS_NUM_WQ_PAGES	4

#define	EQ_DEPTH		1024
#define	CQ_DEPTH		256
#define	WQ_DEPTH		(64 * EMLXS_NUM_WQ_PAGES)
#define	MQ_DEPTH		16
#define	RQ_DEPTH		512 /* Multiple of RQB_COUNT */
#define	RQ_DEPTH_EXPONENT	9


/* Principal doorbell register layouts */
typedef struct emlxs_rqdb
{
#ifdef EMLXS_BIG_ENDIAN
	uint32_t	Rsvd2:2;
	uint32_t	NumPosted:14;	/* Number of entries posted */
	uint32_t	Rsvd1:6;
	uint32_t	Qid:10;		/* RQ id for posted RQE */
#endif /* EMLXS_BIG_ENDIAN */

#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	Qid:10;		/* RQ id for posted RQE */
	uint32_t	Rsvd1:6;
	uint32_t	NumPosted:14;	/* Number of entries posted */
	uint32_t	Rsvd2:2;
#endif /* EMLXS_LITTLE_ENDIAN */

} emlxs_rqdb_t;


typedef union emlxs_rqdbu
{
	uint32_t	word;
	emlxs_rqdb_t	db;

} emlxs_rqdbu_t;


typedef struct emlxs_wqdb
{
#ifdef EMLXS_BIG_ENDIAN
	uint32_t	NumPosted:8;	/* Number of entries posted */
	uint32_t	Index:8;	/* Queue index for posted command */
	uint32_t	Rsvd1:6;
	uint32_t	Qid:10;		/* WQ id for posted WQE */
#endif /* EMLXS_BIG_ENDIAN */

#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	Qid:10;		/* WQ id for posted WQE */
	uint32_t	Rsvd1:6;
	uint32_t	Index:8;	/* Queue index for posted command */
	uint32_t	NumPosted:8;	/* Number of entries posted */
#endif /* EMLXS_LITTLE_ENDIAN */

} emlxs_wqdb_t;


typedef union emlxs_wqdbu
{
	uint32_t	word;
	emlxs_wqdb_t	db;

} emlxs_wqdbu_t;


typedef struct emlxs_cqdb
{
#ifdef EMLXS_BIG_ENDIAN
	uint32_t	NumPosted:2;	/* Number of entries posted */
	uint32_t	Rearm:1;	/* Rearm CQ */
	uint32_t	NumPopped:13;	/* Number of CQ entries processed */
	uint32_t	Rsvd1:5;
	uint32_t	Event:1;	/* 1 if processed entry is EQE */
				/* 0 if processed entry is CQE */
	uint32_t	Qid:10;		/* CQ id for posted CQE */
#endif /* EMLXS_BIG_ENDIAN */

#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	Qid:10;		/* CQ id for posted CQE */
	uint32_t	Event:1;	/* 1 if processed entry is EQE */
				/* 0 if processed entry is CQE */
	uint32_t	Rsvd1:5;
	uint32_t	NumPopped:13;	/* Number of CQ entries processed */
	uint32_t	Rearm:1;	/* Rearm CQ */
	uint32_t	NumPosted:2;	/* Number of entries posted */
#endif /* EMLXS_LITTLE_ENDIAN */

} emlxs_cqdb_t;


typedef union emlxs_cqdbu
{
	uint32_t	word;
	emlxs_cqdb_t	db;

} emlxs_cqdbu_t;

typedef struct emlxs_eqdb
{
#ifdef EMLXS_BIG_ENDIAN
	uint32_t	Rsvd2:2;
	uint32_t	Rearm:1;	/* Rearm EQ */
	uint32_t	NumPopped:13;	/* Number of CQ entries processed */
	uint32_t	Rsvd1:5;
	uint32_t	Event:1;	/* True iff processed entry is EQE */
	uint32_t	Clear:1;	/* clears EQ interrupt when set */
	uint32_t	Qid:9;		/* EQ id for posted EQE */
#endif /* EMLXS_BIG_ENDIAN */

#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	Qid:9;		/* EQ id for posted EQE */
	uint32_t	Clear:1;	/* clears EQ interrupt when set */
	uint32_t	Event:1;	/* True iff processed entry is EQE */
	uint32_t	Rsvd1:5;
	uint32_t	NumPopped:13;	/* Number of CQ entries processed */
	uint32_t	Rearm:1;	/* Rearm EQ */
	uint32_t	Rsvd2:2;
#endif /* EMLXS_LITTLE_ENDIAN */

} emlxs_eqdb_t;


typedef union emlxs_eqdbu
{
	uint32_t	word;
	emlxs_eqdb_t	db;

} emlxs_eqdbu_t;


typedef struct emlxs_mqdb
{
#ifdef EMLXS_BIG_ENDIAN
	uint32_t	Rsvd2:2;
	uint32_t	NumPosted:14;	/* Number of entries posted */
	uint32_t	Rsvd1:5;
	uint32_t	Qid:11;		/* MQ id for posted MQE */
#endif /* EMLXS_BIG_ENDIAN */

#ifdef EMLXS_LITTLE_ENDIAN
	uint32_t	Qid:11;		/* MQ id for posted MQE */
	uint32_t	Rsvd1:5;
	uint32_t	NumPosted:14;	/* Number of entries posted */
	uint32_t	Rsvd2:2;
#endif /* EMLXS_LITTLE_ENDIAN */

} emlxs_mqdb_t;


typedef union emlxs_mqdbu
{
	uint32_t	word;
	emlxs_mqdb_t	db;

} emlxs_mqdbu_t;


#ifdef	__cplusplus
}
#endif

#endif	/* _EMLXS_QUEUE_H */

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

#ifndef _NXHAL_NIC_INTERFACE_H_
#define	_NXHAL_NIC_INTERFACE_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 *        Simple Types
 */

typedef	U32	qlcnic_reg_addr_t;

/*
 *        Root crb-based firmware commands
 */

/*
 * CRB Root Command
 * A single set of crbs is used across all physical/virtual
 * functions for capability queries, initialization, and
 * context creation/destruction.
 *
 * There are 4 CRBS:
 *					Command/Response CRB
 *					Argument1 CRB
 *					Argument2 CRB
 *					Argument3 CRB
 *					Signature CRB
 *
 * The cmd/rsp crb is always intiated by the host via
 * a command code and always responded by the card with
 * a response code. The cmd and rsp codes are disjoint.
 * The sequence of use is always CMD, RSP, CLEAR CMD.
 *
 * The arguments are for passing in command specific
 * and response specific parameters/data.
 *
 * The signature is composed of a magic value, the
 * pci function id, and a command sequence id:
 *		[7:0]  = pci function
 *		[15:8]  = version
 *		[31:16] = magic of 0xcafe
 *
 *	The pci function allows the card to take correct
 *	action for the given particular commands.
 *	The firmware will attempt to detect
 *	an errant driver that has died while holding
 *	the root crb hardware lock. Such an error condition
 *	shows up as the cmd/rsp crb stuck in a non-clear state.
 *
 * Interface Sequence:
 *	 Host always makes requests and firmware always responds.
 *	 Note that data field is always set prior to command field.
 *
 *		[READ]             CMD/RSP CRB      ARGUMENT FIELD
 *		Host grab lock
 *		Host  ->           CMD              optional parameter
 *		FW   <-  (Good)    RSP-OK           DATA
 *		FW   <-  (Fail)    RSP-FAIL         optional failure code
 *		Host ->            CLEAR
 *		Host release lock
 *
 * [WRITE]            CMD/RSP CRB      ARGUMENT FIELD
 * Host grab lock
 * Host  ->           CMD              DATA
 * FW   <-  (Good)    RSP-OK           optional write status
 * FW   <-  (Write)   RSP-FAIL         optional failure code
 * Host ->            CLEAR
 * Host release lock
 */


/*
 *        CMD/RSP
 */

#define	QLCNIC_CDRP_SIGNATURE_TO_PCIFN(sign)    ((sign) & 0xff)
#define	QLCNIC_CDRP_SIGNATURE_TO_VERSION(sign)  (((sign)>>8) & 0xff)
#define	QLCNIC_CDRP_SIGNATURE_TO_MAGIC(sign)    (((sign)>>16) & 0xffff)
#define	QLCNIC_CDRP_SIGNATURE_VALID(sign)       \
	(QLCNIC_CDRP_SIGNATURE_TO_MAGIC(sign) == 0xcafe && \
	    QLCNIC_CDRP_SIGNATURE_TO_PCIFN(sign) < 8)
#define	QLCNIC_CDRP_SIGNATURE_MAKE(pcifn, version) \
	(((pcifn) & 0xff) |		      \
	    (((version) & 0xff) << 8) |	      \
	    ((u32)0xcafe << 16))

#define	QLCNIC_CDRP_CLEAR			0x00000000
#define	QLCNIC_CDRP_CMD_BIT			0x80000000

/*
 * All responses must have the QLCNIC_CDRP_CMD_BIT cleared
 * in the crb QLCNIC_CDRP_CRB_OFFSET.
 */

#define	QLCNIC_CDRP_FORM_RSP(rsp)	(rsp)
#define	QLCNIC_CDRP_IS_RSP(rsp)		(((rsp) & QLCNIC_CDRP_CMD_BIT) == 0)

#define	QLCNIC_CDRP_RSP_OK			0x00000001
#define	QLCNIC_CDRP_RSP_FAIL			0x00000002
#define	QLCNIC_CDRP_RSP_TIMEOUT			0x00000003

/*
 * All commands must have the QLCNIC_CDRP_CMD_BIT set in
 * the crb QLCNIC_CDRP_CRB_OFFSET.
 * The macros below do not have it explicitly set to
 * allow their use in lookup tables
 */
#define	QLCNIC_CDRP_FORM_CMD(cmd)	(QLCNIC_CDRP_CMD_BIT | (cmd))
#define	QLCNIC_CDRP_IS_CMD(cmd)		(((cmd) & QLCNIC_CDRP_CMD_BIT) != 0)

/* [CMD] Capability Vector [RSP] Capability Vector */
#define	QLCNIC_CDRP_CMD_SUBMIT_CAPABILITIES		0x00000001

/* [CMD] - [RSP] Query Value */
#define	QLCNIC_CDRP_CMD_READ_MAX_RDS_PER_CTX    0x00000002

/* [CMD] - [RSP] Query Value */
#define	QLCNIC_CDRP_CMD_READ_MAX_SDS_PER_CTX    0x00000003

/* [CMD] - [RSP] Query Value */
#define	QLCNIC_CDRP_CMD_READ_MAX_RULES_PER_CTX  0x00000004

/* [CMD] - [RSP] Query Value */
#define	QLCNIC_CDRP_CMD_READ_MAX_RX_CTX			0x00000005

/* [CMD] - [RSP] Query Value */
#define	QLCNIC_CDRP_CMD_READ_MAX_TX_CTX			0x00000006

/* [CMD] Rx Config DMA Addr [RSP] rcode */
#define	QLCNIC_CDRP_CMD_CREATE_RX_CTX			0x00000007

/* [CMD] Rx Context Handle, Reset Kind [RSP] rcode */
#define	QLCNIC_CDRP_CMD_DESTROY_RX_CTX			0x00000008

/* [CMD] Tx Config DMA Addr [RSP] rcode */
#define	QLCNIC_CDRP_CMD_CREATE_TX_CTX			0x00000009

/* [CMD] Tx Context Handle, Reset Kind [RSP] rcode */
#define	QLCNIC_CDRP_CMD_DESTROY_TX_CTX			0x0000000a

/* [CMD] Stat setup dma addr - [RSP] Handle, rcode */
#define	QLCNIC_CDRP_CMD_SETUP_STATISTICS		0x0000000e

/* [CMD] Handle - [RSP] rcode */
#define	QLCNIC_CDRP_CMD_GET_STATISTICS			0x0000000f

/* [CMD] Handle - [RSP] rcode */
#define	QLCNIC_CDRP_CMD_DELETE_STATISTICS		0x00000010

/* [CMD] - [RSP] rcode */
#define	QLCNIC_CDRP_CMD_GEN_INT				0x00000011

/* [CMD] MTU - [RSP] rcode */
#define	QLCNIC_CDRP_CMD_SET_MTU				0x00000012

#define	QLCNIC_CDRP_CMD_MAX				0x00000013

/*
 *        Capabilities
 */

#define	QLCNIC_CAP_BIT(class, bit)			(1 << bit)

/* Class 0 (i.e. ARGS 1) */
#define	QLCNIC_CAP0_LEGACY_CONTEXT			QLCNIC_CAP_BIT(0, 0)
#define	QLCNIC_CAP0_MULTI_CONTEXT			QLCNIC_CAP_BIT(0, 1)
#define	QLCNIC_CAP0_LEGACY_MN				QLCNIC_CAP_BIT(0, 2)
#define	QLCNIC_CAP0_LEGACY_MS				QLCNIC_CAP_BIT(0, 3)
#define	QLCNIC_CAP0_CUT_THROUGH				QLCNIC_CAP_BIT(0, 4)
#define	QLCNIC_CAP0_LRO					QLCNIC_CAP_BIT(0, 5)
#define	QLCNIC_CAP0_LSO					QLCNIC_CAP_BIT(0, 6)
#define	QLCNIC_CAP0_JUMBO_CONTIGUOUS			QLCNIC_CAP_BIT(0, 7)
#define	QLCNIC_CAP0_LRO_CONTIGUOUS			QLCNIC_CAP_BIT(0, 8)

/* Class 1 (i.e. ARGS 2) */
#define	QLCNIC_CAP1_NIC					QLCNIC_CAP_BIT(1, 0)
#define	QLCNIC_CAP1_PXE					QLCNIC_CAP_BIT(1, 1)
#define	QLCNIC_CAP1_CHIMNEY				QLCNIC_CAP_BIT(1, 2)
#define	QLCNIC_CAP1_LSA					QLCNIC_CAP_BIT(1, 3)
#define	QLCNIC_CAP1_RDMA				QLCNIC_CAP_BIT(1, 4)
#define	QLCNIC_CAP1_ISCSI				QLCNIC_CAP_BIT(1, 5)
#define	QLCNIC_CAP1_FCOE				QLCNIC_CAP_BIT(1, 6)

/* Class 2 (i.e. ARGS 3) */

/*
 *        Rules
 */

typedef	U32	qlcnic_rx_rule_type_t;

#define	QLCNIC_RX_RULETYPE_DEFAULT				0
#define	QLCNIC_RX_RULETYPE_MAC					1
#define	QLCNIC_RX_RULETYPE_MAC_VLAN				2
#define	QLCNIC_RX_RULETYPE_MAC_RSS				3
#define	QLCNIC_RX_RULETYPE_MAC_VLAN_RSS			4
#define	QLCNIC_RX_RULETYPE_MAX					5

typedef	U32	qlcnic_rx_rule_cmd_t;

#define	QLCNIC_RX_RULECMD_ADD					0
#define	QLCNIC_RX_RULECMD_REMOVE				1
#define	QLCNIC_RX_RULECMD_MAX					2

typedef struct qlcnic_rx_rule_arg_s {
	union {
		struct {
			char mac[6];
		} m;
		struct {
			char mac[6];
			char vlan;
		} mv;
		struct {
			char mac[6];
		} mr;
		struct {
			char mac[6];
			char vlan;
		} mvr;
	} s1;
	/* will be union of all the different args for rules */
	U64 data;
} qlcnic_rx_rule_arg_t;

typedef struct qlcnic_rx_rule_s {
	U32 id;
	U32 active;
	qlcnic_rx_rule_arg_t arg;
	qlcnic_rx_rule_type_t type;
} qlcnic_rx_rule_t;

/* MSG - REQUIRES TX CONTEXT */

/*
 * The rules can be added/deleted from both the
 *  host and card sides so rq/rsp are similar.
 */
typedef struct qlcnic_hostmsg_rx_rule_s {
	qlcnic_rx_rule_cmd_t cmd;
	qlcnic_rx_rule_t rule;
} qlcnic_hostmsg_rx_rule_t;

typedef struct qlcnic_cardmsg_rx_rule_s {
	qlcnic_rcode_t rcode;
	qlcnic_rx_rule_cmd_t cmd;
	qlcnic_rx_rule_t rule;
} qlcnic_cardmsg_rx_rule_t;


/*
 *        Common to Rx/Tx contexts
 */

/*
 * Context states
 */

typedef U32 qlcnic_host_ctx_state_t;

#define	QLCNIC_HOST_CTX_STATE_FREED		0 /* Invalid state */
#define	QLCNIC_HOST_CTX_STATE_ALLOCATED	1 /* Not committed */
/* The following states imply FW is aware of context */
#define	QLCNIC_HOST_CTX_STATE_ACTIVE	2
#define	QLCNIC_HOST_CTX_STATE_DISABLED	3
#define	QLCNIC_HOST_CTX_STATE_QUIESCED	4
#define	QLCNIC_HOST_CTX_STATE_MAX		5

/*
 * Interrupt mask crb use must be set identically on the Tx
 * and Rx context configs across a pci function
 */

/* Rx and Tx have unique interrupt/crb */
#define	QLCNIC_HOST_INT_CRB_MODE_UNIQUE		0
/* Rx and Tx share a common interrupt/crb */
#define	QLCNIC_HOST_INT_CRB_MODE_SHARED		1	/* <= LEGACY */
/* Rx does not use a crb */
#define	QLCNIC_HOST_INT_CRB_MODE_NORX		2
/* Tx does not use a crb */
#define	QLCNIC_HOST_INT_CRB_MODE_NOTX		3
/* Neither Rx nor Tx use a crb */
#define	QLCNIC_HOST_INT_CRB_MODE_NORXTX		4

/*
 * Destroy Rx/Tx
 */

#define	QLCNIC_DESTROY_CTX_RESET			0
#define	QLCNIC_DESTROY_CTX_D3_RESET			1
#define	QLCNIC_DESTROY_CTX_MAX				2


/*
 *        Tx
 */

/*
 * Components of the host-request for Tx context creation.
 * CRB - DOES NOT REQUIRE Rx/TX CONTEXT
 */

typedef struct qlcnic_hostrq_cds_ring_s {
	U64 host_phys_addr;	/* Ring base addr */
	U32 ring_size;		/* Ring entries */
	U32 rsvd;		/* Padding */
} qlcnic_hostrq_cds_ring_t;

typedef struct qlcnic_hostrq_tx_ctx_s {
	U64 host_rsp_dma_addr;	/* Response dma'd here */
	U64 cmd_cons_dma_addr;	/*  */
	U64 dummy_dma_addr;	/*  */
	U32 capabilities[4];	/* Flag bit vector */
	U32 host_int_crb_mode;	/* Interrupt crb usage */
	U32 rsvd1;		/* Padding */
	U16 rsvd2;		/* Padding */
	U16 interrupt_ctl;
	U16 msi_index;
	U16 rsvd3;		/* Padding */
	qlcnic_hostrq_cds_ring_t cds_ring;	/* Desc of cds ring */
	U8  reserved[128];	/* future expansion */
} qlcnic_hostrq_tx_ctx_t;

typedef struct qlcnic_cardrsp_cds_ring_s {
	U32 host_producer_crb;	/* Crb to use */
	U32 interrupt_crb;	/* Crb to use */
} qlcnic_cardrsp_cds_ring_t;

typedef struct qlcnic_cardrsp_tx_ctx_s {
	U32 host_ctx_state;	/* Starting state */
	U16 context_id;		/* Handle for context */
	U8  phys_port;		/* Physical id of port */
	U8  virt_port;		/* Virtual/Logical id of port */
	qlcnic_cardrsp_cds_ring_t cds_ring;	/* Card cds settings */
	U8  reserved[128];	/* future expansion */
} qlcnic_cardrsp_tx_ctx_t;

#define	SIZEOF_HOSTRQ_TX(HOSTRQ_TX) 			\
		(sizeof (HOSTRQ_TX))

#define	SIZEOF_CARDRSP_TX(CARDRSP_TX) 			\
		(sizeof (CARDRSP_TX))

/*
 *        Rx
 */

/*
 * RDS ring mapping to producer crbs
 */

/* Each ring has a unique crb */
#define	QLCNIC_HOST_RDS_CRB_MODE_UNIQUE    0	/* <= LEGACY */

/*
 * All configured RDS Rings share common crb:
 *		1 Ring  - same as unique
 *		2 Rings - 16, 16
 *		3 Rings - 10, 10, 10
 */
#define	QLCNIC_HOST_RDS_CRB_MODE_SHARED    1

/*
 * Bit usage is specified per-ring using the
 * ring's size. Sum of bit lengths must be <= 32.
 * Packing is [Ring N] ... [Ring 1][Ring 0]
 */
#define	QLCNIC_HOST_RDS_CRB_MODE_CUSTOM		2
#define	QLCNIC_HOST_RDS_CRB_MODE_MAX		3


/*
 * RDS Ting Types
 */

#define	QLCNIC_RDS_RING_TYPE_NORMAL		0
#define	QLCNIC_RDS_RING_TYPE_JUMBO		1
#define	QLCNIC_RDS_RING_TYPE_LRO		2
#define	QLCNIC_RDS_RING_TYPE_MAX		3

/*
 * Components of the host-request for Rx context creation.
 * CRB - DOES NOT REQUIRE Rx/TX CONTEXT
 */

typedef struct qlcnic_hostrq_sds_ring_s {
	U64 host_phys_addr;	/* Ring base addr */
	U32 ring_size;		/* Ring entries */
	U16 msi_index;
	U16 rsvd;		/* Padding */
} qlcnic_hostrq_sds_ring_t;

typedef struct qlcnic_hostrq_rds_ring_s {
	U64 host_phys_addr;	/* Ring base addr */
	U64 buff_size;		/* Packet buffer size */
	U32 ring_size;		/* Ring entries */
	U32 ring_kind;		/* Class of ring */
} qlcnic_hostrq_rds_ring_t;

typedef struct qlcnic_hostrq_rx_ctx_s {
	U64 host_rsp_dma_addr;	/* Response dma'd here */
	U32 capabilities[4];	/* Flag bit vector */
	U32 host_int_crb_mode;	/* Interrupt crb usage */
	U32 host_rds_crb_mode;	/* RDS crb usage */
	/* These ring offsets are relative to end of structure */
	U32 rds_ring_offset;	/* Offset to RDS config */
	U32 sds_ring_offset;	/* Offset to SDS config */
	U16 num_rds_rings;	/* Count of RDS rings */
	U16 num_sds_rings;	/* Count of SDS rings */
	U16 rsvd1;		/* Padding */
	U16 rsvd2;		/* Padding */
	U8  reserved[128]; 	/* reserve space for future expansion */
	/*
	 * MUST BE 64-bit aligned.
	 * The following is packed:
	 * - N hostrq_rds_rings
	 * - N hostrq_sds_rings
	 */
} qlcnic_hostrq_rx_ctx_t;

typedef struct qlcnic_cardrsp_rds_ring_s {
	U32 host_producer_crb;	/* Crb to use */
	U32 rsvd1;		/* Padding */
} qlcnic_cardrsp_rds_ring_t;

typedef struct qlcnic_cardrsp_sds_ring_s {
	U32 host_consumer_crb;	/* Crb to use */
	U32 interrupt_crb;	/* Crb to use */
} qlcnic_cardrsp_sds_ring_t;

typedef struct qlcnic_cardrsp_rx_ctx_s {
	/* These ring offsets are relative to end of structure */
	U32 rds_ring_offset;	/* Offset to RDS config */
	U32 sds_ring_offset;	/* Offset to SDS config */
	U32 host_ctx_state;	/* Starting State */
	U32 num_fn_per_port;	/* How many PCI fn share the port */
	U16 num_rds_rings;	/* Count of RDS rings */
	U16 num_sds_rings;	/* Count of SDS rings */
	U16 context_id;		/* Handle for context */
	U8  phys_port;		/* Physical id of port */
	U8  virt_port;		/* Virtual/Logical id of port */
	U8  reserved[128];	/* save space for future expansion */
	/*
	 * MUST BE 64-bit aligned.
	 * The following is packed:
	 * - N cardrsp_rds_rings
	 * - N cardrs_sds_rings
	 */
} qlcnic_cardrsp_rx_ctx_t;

#define	SIZEOF_HOSTRQ_RX(HOSTRQ_RX, rds_rings, sds_rings)	\
	(sizeof (HOSTRQ_RX) + 					\
	(rds_rings)*(sizeof (qlcnic_hostrq_rds_ring_t)) +		\
	    (sds_rings)*(sizeof (qlcnic_hostrq_sds_ring_t)))

#define	SIZEOF_CARDRSP_RX(CARDRSP_RX, rds_rings, sds_rings) 	\
	(sizeof (CARDRSP_RX) + 					\
	(rds_rings)*(sizeof (qlcnic_cardrsp_rds_ring_t)) + 		\
	    (sds_rings)*(sizeof (qlcnic_cardrsp_sds_ring_t)))


/*
 *        Statistics
 */

/*
 * The model of statistics update to use
 */

#define	QLCNIC_STATISTICS_MODE_INVALID		0

/*
 * Permanent setup; Updates are only sent on explicit request
 * QLCNIC_CDRP_CMD_GET_STATISTICS)
 */
#define	QLCNIC_STATISTICS_MODE_PULL			1

/*
 * Permanent setup; Updates are sent automatically and on
 * explicit request (QLCNIC_CDRP_CMD_GET_STATISTICS)
 */
#define	QLCNIC_STATISTICS_MODE_PUSH			2

/* One time stat update. */
#define	QLCNIC_STATISTICS_MODE_SINGLE_SHOT	3

#define	QLCNIC_STATISTICS_MODE_MAX			4

/*
 * What set of stats
 */
#define	QLCNIC_STATISTICS_TYPE_INVALID		0
#define	QLCNIC_STATISTICS_TYPE_NIC_RX_CORE	1
#define	QLCNIC_STATISTICS_TYPE_NIC_TX_CORE	2
#define	QLCNIC_STATISTICS_TYPE_NIC_RX_ALL	3
#define	QLCNIC_STATISTICS_TYPE_NIC_TX_ALL	4
#define	QLCNIC_STATISTICS_TYPE_MAX			5


/*
 * Request to setup statistics gathering.
 * CRB - DOES NOT REQUIRE Rx/TX CONTEXT
 */

typedef struct qlcnic_hostrq_stat_setup_s {
	U64 host_stat_buffer;	/* Where to dma stats */
	U32 host_stat_size;	/* Size of stat buffer */
	U16 context_id;		/* Which context */
	U16 stat_type;		/* What class of stats */
	U16 stat_mode;		/* When to update */
	U16 stat_interval;	/* Frequency of update */
} qlcnic_hostrq_stat_setup_t;

#ifdef __cplusplus
}
#endif

#endif /* _NXHAL_NIC_INTERFACE_H_ */

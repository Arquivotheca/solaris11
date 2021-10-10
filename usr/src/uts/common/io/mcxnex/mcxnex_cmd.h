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
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_MCXNEX_CMD_H
#define	_MCXNEX_CMD_H

/*
 * mcxnex_cmd.h
 *    Contains all of the prototypes, #defines, and structures necessary
 *    for the Firmware Command routines.
 *    Specifically it contains the command types, command statuses and flags,
 *    structures used for managing mailboxes and outstanding commands,
 *    and prototypes for most of the functions consumed by other parts of
 *    the driver.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#include <sys/ib/mgt/sm_attr.h>

/*
 * Defines used mcxnex_write_hcr() to determine the duration and number of
 * times (at maximum) to poll while waiting for a Mcxnex firmware command to
 * release the HCR resource (i.e. waiting for the command to complete)
 */
#define	MCXNEX_CMD_POLL_DELAY		1
#define	MCXNEX_CMD_POLL_MAX		3000000

/*
 * The following defines specify the default number of mailboxes (log 2) of
 * each type and their size and alignment restrictions.  By default the number
 * of both "In" and "Out" mailboxes is set to 1024 (with each mailbox being
 * 4KB in size), but both the number and sizes of each are controllable
 * through the "mcxnex_log_num_inmbox", "mcxnex_log_num_outmbox",
 * "mcxnex_log_inmbox_size" and "mcxnex_log_outmbox_size" configuration
 * variables. Also, we have a define that is used to allocate interrupt
 * mailboxes (1 in, 1 out).
 */
#define	MCXNEX_NUM_MAILBOXES_SHIFT		0xA
#define	MCXNEX_NUM_INTR_MAILBOXES_SHIFT	0
#define	MCXNEX_MBOX_SIZE_SHIFT		0xC
#define	MCXNEX_MBOX_SIZE			(1 << MCXNEX_MBOX_SIZE_SHIFT)
#define	MCXNEX_MBOX_ALIGN			MCXNEX_MBOX_SIZE

/*
 * These are the defines for the command type (opcodes).  They are
 * specified by the PRM
 */

/* Init Commands */
#define	QUERY_DEV_LIM			0x3
#define	QUERY_DEV_CAP			0x3
#define	QUERY_FW			0x4
#define	QUERY_ADAPTER			0x6
#define	INIT_HCA			0x7
#define	CLOSE_HCA			0x8
#define	INIT_IB				0x9
#define	INIT_PORT			0x9
#define	CLOSE_IB			0xA
#define	CLOSE_PORT			0xA
#define	QUERY_HCA			0xB
#define	SET_IB				0xC
#define	SET_PORT			0xC
/* added late in tavor for SRQ support */
#define	MOD_STAT_CFG			0x34
/* added late in Mcxnex (PRM 0.35) */
#define	QUERY_PORT			0x43


/* TPT Commands */
#define	SW2HW_MPT			0xD
#define	QUERY_MPT			0xE
#define	HW2SW_MPT			0xF
#define	READ_MTT			0x10
#define	WRITE_MTT			0x11
#define	SYNC_TPT			0x2F
#define	MODIFY_MPT			0x39

/* EQ Commands */
#define	MAP_EQ				0x12
#define	SW2HW_EQ			0x13
#define	HW2SW_EQ			0x14
#define	QUERY_EQ			0x15

/* CQ Commands */
#define	SW2HW_CQ			0x16
#define	HW2SW_CQ			0x17
#define	QUERY_CQ			0x18
#define	MODIFY_CQ			0x2C

/* Modify CQ Command - opcode modifiers */
#define	RESIZE_CQ			0x0
#define	MODIFY_MODERATION_CQ		0x1
#define	MODIFY_EQN			0x2

/* QP Commands */
#define	RST2INIT_QP			0x19
#define	INIT2INIT_QP			0x2D
#define	INIT2RTR_QP			0x1A
#define	RTR2RTS_QP			0x1B
#define	RTS2RTS_QP			0x1C
#define	SQERR2RTS_QP			0x1D
#define	TOERR_QP			0x1E
#define	RTS2SQD_QP			0x1F
#define	SQD2SQD_QP			0x38
#define	SQD2RTS_QP			0x20
#define	TORST_QP			0x21
#define	QUERY_QP			0x22
#define	SUSPEND_QP			0x32
#define	UNSUSPEND_QP			0x33

/* SPECIAL QPs Commands */
#define	CONF_SPECIAL_QP			0x23
#define	MAD_IFC				0x24

/* added late in tavor for SRQ support */
/* SRQ Commands */
#define	SW2HW_SRQ			0x35
#define	HW2SW_SRQ			0x36
#define	QUERY_SRQ			0x37
/* new in mcxnex, replaces part of modifyMPT */
#define	RESIZE_SRQ			0X44
/* new in mcxnex, set limit water mark */
#define	ARM_RQ				0X40

/* Multicast Group Commands */
#define	READ_MGM			0x25
#define	READ_MCG			0x25
#define	WRITE_MGM			0x26
#define	WRITE_MCG			0x26
#define	MGID_HASH			0x27

/* Debug/Diagnostic Commands */
#define	QUERY_DEBUG_MSG			0x2A
#define	SET_DEBUG_MSG			0x2B
#define	DIAG_RPRT			0x30
#define	CMD_NOP				0x31

/* Ethernet Commands */
#define	EN_SET_VLAN_FLTR		0x47
#define	EN_SET_MCAST_FLTR		0x48
#define	EN_DUMP_ETH_STATS		0x49


enum {
	EN_MCAST_CONFIG = 0,
	EN_MCAST_DISABLE = 1,
	EN_MCAST_ENABLE = 2,
};

#define	EN_BCAST		0xffffffffffffULL

/* Port Sensing */
#define	SENSE_PORT			0x4d


#define	MCXNEX_PORT_TYPE_IB	   1
#define	MCXNEX_PORT_TYPE_ETH	   2
#define	MCXNEX_PORT_TYPE_AUTO	   3
#define	MCXNEX_PORT_TYPE_INVALID   0xff



/* ICM and related commands - w/out LAM commands from Arbel */
#define	RUN_FW				0xFF6
#define	UNMAP_ICM			0xFF9
#define	MAP_ICM				0xFFA
#define	UNMAP_ICM_AUX			0xFFB
#define	MAP_ICM_AUX			0xFFC
#define	SET_ICM_SIZE			0xFFD
#define	UNMAP_FA			0xFFE
#define	MAP_FA				0xFFF

/*
 * Commands mentioned but not defined in PRM v35
 *	REL_ICM_AUX
 *	INIT_VM
 * 	HEART_BEAT_RQ
 */

/*
 * These are the defines for the command completion statuses.  They are
 * also specified (in part) by the PRM.  However,
 * the MCXNEX_CMD_INSUFF_RSRC, MCXNEX_CMD_TIMEOUT and MCXNEX_CMD_INVALID_STATUS
 * codes were added for this driver specifically to indicate the conditions
 * when insufficient resources are available for a command, when a command has
 * timed out (failure in the Mcxnex firmware) or when some other invalid result
 * was received.
 */
#define	MCXNEX_CMD_TIMEOUT_TOGGLE	0xFFFC 	/* -4 */
#define	MCXNEX_CMD_INSUFF_RSRC		0xFFFD	/* -3 */
#define	MCXNEX_CMD_TIMEOUT_GOBIT	0xFFFE 	/* -2 */
#define	MCXNEX_CMD_INVALID_STATUS	0xFFFF  /* -1 */
#define	MCXNEX_CMD_SUCCESS		0x00
#define	MCXNEX_CMD_INTERNAL_ERR		0x01
#define	MCXNEX_CMD_BAD_OP		0x02
#define	MCXNEX_CMD_BAD_PARAM		0x03
#define	MCXNEX_CMD_BAD_SYS_STATE		0x04
#define	MCXNEX_CMD_BAD_RESOURCE		0x05
#define	MCXNEX_CMD_RESOURCE_BUSY		0x06
#define	MCXNEX_CMD_EXCEED_LIM		0x08
#define	MCXNEX_CMD_BAD_RES_STATE		0x09
#define	MCXNEX_CMD_BAD_INDEX		0x0A
#define	MCXNEX_CMD_BAD_NVMEM		0x0B
#define	MCXNEX_CMD_ICM_ERROR		0x0C
#define	MCXNEX_CMD_BAD_QP_STATE		0x10
#define	MCXNEX_CMD_BAD_SEG_PARAM		0x20
#define	MCXNEX_CMD_REG_BOUND		0x21
#define	MCXNEX_CMD_BAD_PKT		0x30
#define	MCXNEX_CMD_BAD_SIZE		0x40

/*
 * These defines are used in the "special QP" allocation to indicate the type
 * of special QP (SMI, GSI, or one of the raw types).  These values are
 * specified by the Mcxnex PRM
 */
#define	MCXNEX_CMD_QP_SMI		0
#define	MCXNEX_CMD_QP_GSI		1
#define	MCXNEX_CMD_QP_RAW_IPV6		2
#define	MCXNEX_CMD_QP_RAW_ETH		3

#define	MCXNEX_CMD_SPEC_QP_OPMOD(smi, gsi) \
	((smi & 0x01) | ((gsi & 0x01) << 1))

/*
 * For certain Mcxnex QP state transition commands some optional flags are
 * allowed.  These "opmask" flags are defined by the Mcxnex PRM
 * as a bitmask.
 */

#define	MCXNEX_CMD_OP_ALT_PATH		(1 << 0)
#define	MCXNEX_CMD_OP_RRE		(1 << 1)
#define	MCXNEX_CMD_OP_RAE		(1 << 2)
#define	MCXNEX_CMD_OP_RWE		(1 << 3)
#define	MCXNEX_CMD_OP_PKEYINDX		(1 << 4) /* primary path */
#define	MCXNEX_CMD_OP_QKEY		(1 << 5)
#define	MCXNEX_CMD_OP_MINRNRNAK		(1 << 6)
#define	MCXNEX_CMD_OP_PRIM_PATH		(1 << 7)
#define	MCXNEX_CMD_OP_SRA_SET		(1 << 8)
#define	MCXNEX_CMD_OP_RRA_SET		(1 << 9)
#define	MCXNEX_CMD_OP_PM_STATE		(1 << 10) /* migration */
/* MCXNEX_CMD_OP_PRIM_PORT is obsolete, instead use MCXNEX_CMD_OP_SCHEDQUEUE */
#define	MCXNEX_CMD_OP_PRIM_PORT		(1 << 11)
#define	MCXNEX_CMD_OP_RETRYCNT		(1 << 12) /* Global */
#define	MCXNEX_CMD_OP_ALT_RNRRETRY	(1 << 13)
#define	MCXNEX_CMD_OP_ACKTIMEOUT	(1 << 14) /* primary path */
#define	MCXNEX_CMD_OP_PRIM_RNRRETRY	(1 << 15) /* reserved in MCXNEX */
#define	MCXNEX_CMD_OP_SCHEDQUEUE	(1 << 16)
#define	MCXNEX_CMD_OP_RSSCONTEXT	(1 << 17)
#define	MCXNEX_CMD_OP_SRQN		(1 << 18) /* for rss balancing */
#define	MCXNEX_CMD_OP_CQN_RCV		(1 << 19) /* for rss balancing */
/* Bits 20 - 31 RESERVED - per PRM 0.35c */



/*
 * The Mcxnex RTS2SQD command can take the following flag as part of its
 * input modifier to request the Send Queue Drained event
 */
#define	MCXNEX_CMD_REQ_SQD_EVENT		0x80000000

/*
 * The Mcxnex TORST command can take the following flag (as part of a bitmask)
 * in its opcode modifier to request that the transition to reset should
 * not go through the Error state (and, hence, should not generate "flushed-
 * in-error" completions
 */
#define	MCXNEX_CMD_DIRECT_TO_RESET	(1 << 1)

/*
 * Some Mcxnex commands write an OUT mailbox entry, depending on the value of
 * the 'opmod' parameter.  These defines provide the correct opmod value to
 * write depending on whether to write an entry or not.
 */
#define	MCXNEX_CMD_DO_OUTMBOX		(0)
#define	MCXNEX_CMD_NO_OUTMBOX		(1 << 0)


/*
 * The Mcxnex MAP_EQ command can take the following flags (and use the
 * MCXNEX_CMD_UNMAP_EQ_MASK input modifier) to indicate whether the given
 * event queue should mapped to or unmapped from the given event type.
 */


#define	MCXNEX_CMD_MAP_EQ_EVT_MAP	0
#define	MCXNEX_CMD_MAP_EQ_EVT_UNMAP	1
#define	MCXNEX_CMD_UNMAP_EQ_MASK	0x80000000

/*
 * The following defines are used by the MAD_IFC command and the helper
 * routines that get PortInfo, NodeInfo, GUIDInfo, and PKeyTable entries.
 *
 * The first indicates whether of not MKey checking should be enforced.
 * This is passed in the opcode modifier field for MAD_IFC commands.
 *
 * The next set are used to define certain hardcoded management datagram (MAD)
 * sizes, offsets, and header formats for each of the helper operations.
 */
#define	MCXNEX_CMD_MKEY_CHECK		0
#define	MCXNEX_CMD_MKEY_DONTCHECK	1
#define	MCXNEX_CMD_BKEY_DONTCHECK	2

#define	MCXNEX_CMD_MAD_IFC_SIZE		0x100
#define	MCXNEX_CMD_MADDATA_OFFSET	0x40
#define	MCXNEX_CMD_MADHDR0		0x01010101
#define	MCXNEX_CMD_MADHDR1		0x00000000
#define	MCXNEX_CMD_MADHDR2		0x00000000
#define	MCXNEX_CMD_MADHDR3		0x00000000

#define	MCXNEX_CMD_PORTINFO		0x00150000
#define	MCXNEX_CMD_NODEINFO		0x00110000
#define	MCXNEX_CMD_NODEDESC		0x00100000
#define	MCXNEX_CMD_GUIDINFO		0x00140000
#define	MCXNEX_CMD_PKEYTBLE		0x00160000

#define	MCXNEX_CMD_PERF_GET		0x01040101
#define	MCXNEX_CMD_PERF_SET		0x01040102
#define	MCXNEX_CMD_CLASSPORTINFO	0x00010000
#define	MCXNEX_CMD_PERFCNTRS		0x00120000
#define	MCXNEX_CMD_EXTPERFCNTRS		0x001D0000
#define	MCXNEX_CMD_PERFATTR		0x00000000

#define	MCXNEX_IS_EXT_WIDTH_SUPPORTED		0x0000020000000000
#define	MCXNEX_IS_EXT_WIDTH_SUPPORTED_NOIETF	0x0000040000000000


/*
 * The next few defines are used to indicate the size of the "reserved" area
 * in the WRITE_MTT command, and the respective sizes of the SET_PORT and
 * MGID_HASH commands
 */
#define	MCXNEX_CMD_WRITEMTT_RSVD_SZ	0x10
#define	MCXNEX_CMD_SETPORT_SZ		0x8
#define	MCXNEX_CMD_MGIDHASH_SZ		0x10

/*
 * This last define is used by mcxnex_cmn_ownership_cmd_post() to keep track
 * of the direction (from hardware ownership to software, or vice versa) of
 * the requested operation
 */
#define	MCXNEX_CMD_RSRC_HW2SW		0
#define	MCXNEX_CMD_RSRC_SW2HW		1

/*
 * The following macros are used for handling any endianness related issues
 * that might arise from the Mcxnex driver's internal use of MADs.
 *
 *    MCXNEX_GETPORTINFO_SWAP	- All the necessary swapping to handle the
 *				    response to a GetPortInfo MAD
 *    MCXNEX_GETNODEINFO_SWAP	- All the necessary swapping to handle the
 *				    response to a GetNodeInfo MAD
 *    MCXNEX_GETGUIDINFO_SWAP	- All the necessary swapping to handle the
 *				    response to a GetGUIDInfo MAD
 *    MCXNEX_GETPKEYTABLE_SWAP	- All the necessary swapping to handle the
 *				    response to a GetPKeyTable MAD
 */


#ifdef	_LITTLE_ENDIAN
#define	MCXNEX_GETPORTINFO_SWAP(portinfo)				\
{									\
	(portinfo)->M_Key = ddi_swap64((portinfo)->M_Key);		\
	(portinfo)->GidPrefix = ddi_swap64((portinfo)->GidPrefix);	\
	(portinfo)->LID = ddi_swap16((portinfo)->LID);			\
	(portinfo)->MasterSMLID = ddi_swap16((portinfo)->MasterSMLID);	\
	(portinfo)->CapabilityMask =					\
	    ddi_swap32((portinfo)->CapabilityMask);			\
	(portinfo)->DiagCode = ddi_swap16((portinfo)->DiagCode);	\
	(portinfo)->M_KeyLeasePeriod =					\
	    ddi_swap16((portinfo)->M_KeyLeasePeriod);			\
	(portinfo)->M_KeyViolations =					\
	    ddi_swap16((portinfo)->M_KeyViolations);			\
	(portinfo)->P_KeyViolations =					\
	    ddi_swap16((portinfo)->P_KeyViolations);			\
	(portinfo)->Q_KeyViolations =					\
	    ddi_swap16((portinfo)->Q_KeyViolations);			\
}
#else
#define	MCXNEX_GETPORTINFO_SWAP(portinfo)
#endif

#ifdef	_LITTLE_ENDIAN
#define	MCXNEX_GETNODEINFO_SWAP(nodeinfo)				\
{									\
	uint32_t	tmp;						\
									\
	tmp = ddi_swap32(((uint32_t *)nodeinfo)[9]);			\
	(nodeinfo)->VendorID	 = tmp & 0xFFFFFF;			\
	(nodeinfo)->LocalPortNum = tmp >> 24;				\
	(nodeinfo)->Revision	 =					\
	    ddi_swap32(((uint32_t *)nodeinfo)[8]);			\
	tmp = ddi_swap32(((uint32_t *)nodeinfo)[7]);			\
	(nodeinfo)->PartitionCap = tmp >> 16;				\
	(nodeinfo)->DeviceID	 = tmp & 0xFFFF;			\
	(nodeinfo)->PortGUID = ddi_swap64((((uint64_t)			\
	    (((uint32_t *)nodeinfo)[6]) << 32) |			\
	    ((uint32_t *)nodeinfo)[5]));				\
	(nodeinfo)->NodeGUID = ddi_swap64((((uint64_t)			\
	    (((uint32_t *)nodeinfo)[4]) << 32) |			\
	    ((uint32_t *)nodeinfo)[3]));				\
	(nodeinfo)->SystemImageGUID = ddi_swap64((((uint64_t)		\
	    (((uint32_t *)nodeinfo)[2]) << 32) |			\
	    ((uint32_t *)nodeinfo)[1]));				\
}
#else
#define	MCXNEX_GETNODEINFO_SWAP(nodeinfo)				\
{									\
	uint32_t	tmp;						\
									\
	tmp = ((uint32_t *)nodeinfo)[9];				\
	(nodeinfo)->VendorID	 = tmp & 0xFFFFFF;			\
	(nodeinfo)->LocalPortNum = tmp >> 24;				\
	(nodeinfo)->Revision	 = ((uint32_t *)nodeinfo)[8];		\
	tmp = ((uint32_t *)nodeinfo)[7];				\
	(nodeinfo)->PartitionCap = tmp >> 16;				\
	(nodeinfo)->DeviceID	 = tmp & 0xFFFF;			\
	(nodeinfo)->PortGUID = (((uint64_t)				\
	    (((uint32_t *)nodeinfo)[5]) << 32) |			\
	    ((uint32_t *)nodeinfo)[6]);					\
	(nodeinfo)->NodeGUID = (((uint64_t)				\
	    (((uint32_t *)nodeinfo)[3]) << 32) |			\
	    ((uint32_t *)nodeinfo)[4]);					\
	(nodeinfo)->SystemImageGUID = (((uint64_t)			\
	    (((uint32_t *)nodeinfo)[1]) << 32) |			\
	    ((uint32_t *)nodeinfo)[2]);					\
}
#endif

#ifdef	_LITTLE_ENDIAN
#define	MCXNEX_GETGUIDINFO_SWAP(guidinfo)				\
{									\
	int	i;							\
									\
	for (i = 0; i < 8; i++) {					\
		(guidinfo)->GUIDBlocks[i] =				\
		    ddi_swap64((guidinfo)->GUIDBlocks[i]);		\
	}								\
}
#else
#define	MCXNEX_GETGUIDINFO_SWAP(guidinfo)
#endif

#ifdef	_LITTLE_ENDIAN
#define	MCXNEX_GETPKEYTABLE_SWAP(pkeytable)				\
{									\
	int	i;							\
									\
	for (i = 0; i < 32; i++) {					\
		(pkeytable)->P_KeyTableBlocks[i] =			\
		    ddi_swap16((pkeytable)->P_KeyTableBlocks[i]);	\
	}								\
}
#else
#define	MCXNEX_GETPKEYTABLE_SWAP(pkeytable)
#endif

/*
 * The Mcxnex MODIFY_MPT command can take the following opcode modifier
 * options to specify whether to modify for ResizeSRQ() or to swap the
 * full MPT entry.
 */
#define	MCXNEX_CMD_MODIFY_MPT_RESIZESRQ	3
#define	MCXNEX_CMD_MODIFY_MPT_SWAPFULL	5

/*
 * Mcxnex MOD_STAT_CFG Opcode Modifier
 */
#define	MCXNEX_MOD_STAT_CFG_PTR			0x0
#define	MCXNEX_MOD_STAT_CFG_INLINE		0x1
#define	MCXNEX_MOD_STAT_CFG_QUERY_PTR		0x2
#define	MCXNEX_MOD_STAT_CFG_QUERY_INLINE	0x3
#define	MCXNEX_MOD_STAT_CFG_DEFAULTS		0xF

#define	MCXNEX_MOD_STAT_CFG_SETUP_PORT		0x1

#define	MCXNEX_MOD_STAT_CFG_PORT_EN_OFFSET	8
#define	MCXNEX_MOD_STAT_CFG_PORT_EN_MASK	(1 << 20)


/*
 * The mcxnex_mbox_t structure is used internally by the driver to track
 * all the information necessary to manage mailboxes for the command
 * interface.  Specifically, by containing a pointer to the buffer, the
 * PCI mapped address, the access handle, and a back pointer to the
 * mcxnex_rsrc_t structure used to track this resource, it provides enough
 * information allocate, use, and free any type of mailbox.
 *
 * The mb_indx, mb_next, and mb_prev fields are used only by the mailbox
 * alloc/free routines (see mcxnex_impl_mbox_alloc/free() for more details)
 * and are not read or modified by any mailbox consumers.  They are used
 * to implement a fast allocation mechanism.
 */
typedef struct mcxnex_mbox_s {
	void			*mb_addr;
	uint64_t		mb_mapaddr;
	ddi_acc_handle_t	mb_acchdl;
	mcxnex_rsrc_t		*mb_rsrcptr;
	uint_t			mb_indx;
	uint_t			mb_next;
	uint_t			mb_prev;
} mcxnex_mbox_t;

/*
 * The mcxnex_mboxlist_t structure is used to track all the information
 * relevant to the pools of Mcxnex mailboxes.  Specifically, it has a pointer
 * to an array of mcxnex_mbox_t structures, a lock and cv used for blocking
 * on alloc when mailboxes are not available, and a head, tail, and entries
 * free counter to keep track of which (if any) mailboxes are currently free.
 * This is used (along with the mb_indx, mb_next, and mb_prev fields in the
 * mcxnex_mbox_t) to implement the fast allocation mechanism.
 */
typedef struct mcxnex_mboxlist_s {
	kmutex_t		mbl_lock;
	kcondvar_t		mbl_cv;
	mcxnex_mbox_t		*mbl_mbox;
	uint_t			mbl_list_sz;
	uint_t			mbl_num_alloc;
	uint_t			mbl_head_indx;
	uint_t			mbl_tail_indx;
	uint_t			mbl_entries_free;
	uint_t			mbl_waiters;
	uint_t			mbl_pollers;
	uint_t			mbl_signal;
} mcxnex_mboxlist_t;
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_mboxlist_t::mbl_lock,
	mcxnex_mboxlist_t::mbl_mbox
	mcxnex_mboxlist_t::mbl_list_sz
	mcxnex_mboxlist_t::mbl_num_alloc
	mcxnex_mboxlist_t::mbl_cv
	mcxnex_mboxlist_t::mbl_head_indx
	mcxnex_mboxlist_t::mbl_tail_indx
	mcxnex_mboxlist_t::mbl_entries_free
	mcxnex_mboxlist_t::mbl_waiters
	mcxnex_mboxlist_t::mbl_pollers
	mcxnex_mboxlist_t::mbl_signal
	mcxnex_mbox_t::mb_next
	mcxnex_mbox_t::mb_prev))

/*
 * The mcxnex_mbox_info_t structure is used by mailbox allocators to
 * specify the type of mailbox(es) being requested.  On a call to
 * mcxnex_mbox_alloc() the mbi_alloc_flags may be set to
 * MCXNEX_ALLOC_INMBOX, MCXNEX_ALLOC_OUTMBOX, or both.  If it is able to
 * allocate the request type(s) of mailboxes, mcxnex_mbox_alloc() will
 * fill in the "mbi_in" and/or "mbi_out" pointers to point to valid
 * mcxnex_mbox_t structures from the appropriate mcxnex_mboxlist_t
 * (see above).  This same structure is also passed to
 * mcxnex_mbox_free().  It is the responsibility of the caller to
 * mcxnex_mbox_alloc() to return this exact structure (unmodified) to
 * mcxnex_mbox_free().
 *
 * Note: If both "In" and "Out" mailboxes are requested, it is assured that
 * no deadlock can result (from holding one mailbox while attempting to get
 * the other).  This is assured by the fact that the "In" mailbox will always
 * be allocated first before attempting to allocate the "Out"
 */
typedef struct mcxnex_mbox_info_s {
	uint_t			mbi_alloc_flags;
	uint_t			mbi_sleep_context;
	mcxnex_mbox_t		*mbi_in;
	mcxnex_mbox_t		*mbi_out;
} mcxnex_mbox_info_t;
#define	MCXNEX_ALLOC_INMBOX	(1 << 0)
#define	MCXNEX_ALLOC_OUTMBOX	(1 << 1)


/*
 * The mcxnex_cmd_t structure is used internally by the Mcxnex driver to track
 * all the information necessary to manage outstanding firmware commands on
 * the Mcxnex command interface.
 *
 * Each mcxnex_cmd_t structure contains a cv and lock which are used by the
 * posting thread to block for completion (with cmd_status being overloaded
 * to indicate the condition variable).  The cmd_outparam field is used to
 * return additional status from those Mcxnex commands that specifically
 * require it.
 *
 * The cmd_indx, cmd_next, and cmd_prev fields are used by the outstanding
 * command alloc/free routines (see mcxnex_outstanding_cmd_alloc/free() for
 * more details).  They are used (in much the same way as the mb_indx,
 * mb_next, and mb_prev fields in mcxnex_mbox_t above) to implement a fast
 * allocation mechanism.
 */
typedef struct mcxnex_cmd_s {
	kmutex_t		cmd_comp_lock;
	kcondvar_t		cmd_comp_cv;
	uint64_t		cmd_outparm;
	uint_t			cmd_status;
	uint_t			cmd_indx;
	uint_t			cmd_next;
	uint_t			cmd_prev;
} mcxnex_cmd_t;
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_cmd_t::cmd_comp_lock,
    mcxnex_cmd_t::cmd_comp_cv
    mcxnex_cmd_t::cmd_status))

/*
 * The mcxnex_cmdlist_t structure is used in almost exactly the same way as
 * the mcxnex_mboxlist_t above, but instead to track all the information
 * relevant to the pool of outstanding Mcxnex commands.  Specifically, it has
 * a pointer to an array of mcxnex_cmd_t structures, a lock and cv used for
 * blocking on alloc when outstanding command slots are not available, and a
 * head, tail, and entries free counter to keep track of which (if any)
 * command slots are currently free.  This is used (along with the cmd_indx,
 * cmd_next, and cmd_prev fields in the mcxnex_cmd_t) to implement the fast
 * allocation mechanism.
 */
typedef struct mcxnex_cmdlist_s {
	kmutex_t		cml_lock;
	kcondvar_t		cml_cv;
	mcxnex_cmd_t		*cml_cmd;
	uint_t			cml_list_sz;
	uint_t			cml_num_alloc;
	uint_t			cml_head_indx;
	uint_t			cml_tail_indx;
	uint_t			cml_entries_free;
	uint_t			cml_waiters;
} mcxnex_cmdlist_t;
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_cmdlist_t::cml_lock,
    mcxnex_cmdlist_t::cml_cv
    mcxnex_cmdlist_t::cml_cmd
    mcxnex_cmdlist_t::cml_list_sz
    mcxnex_cmdlist_t::cml_num_alloc
    mcxnex_cmdlist_t::cml_head_indx
    mcxnex_cmdlist_t::cml_tail_indx
    mcxnex_cmdlist_t::cml_entries_free
    mcxnex_cmdlist_t::cml_waiters
    mcxnex_cmd_t::cmd_next
    mcxnex_cmd_t::cmd_prev))
_NOTE(LOCK_ORDER(mcxnex_cmdlist_t::cml_lock
    mcxnex_cmd_t::cmd_comp_lock))

/*
 * The mcxnex_cmd_post_t structure is used by all the Mcxnex Firmware Command
 * routines to post to Mcxnex firmware.  The fields almost exactly mimic
 * the fields in the Mcxnex HCR registers.  The notable exception is the
 * addition of the "cp_flags" field (which can be set to MCXNEX_CMD_SPIN or
 * MCXNEX_CMD_NOSPIN).  This flag really controls the value of the "e" bit
 * in the HCR (i.e. the bit to indicate whether command should complete
 * "in place" - in the HCR - or whether they should have their completions
 * written to the command completion event queue.  MCXNEX_CMD_SPIN means
 * to allow commands to complete "in place" and to poll the "go" bit in
 * the HCR to determine completion.
 */
typedef struct mcxnex_cmd_post_s {
	uint64_t		cp_inparm;
	uint64_t		cp_outparm;
	uint32_t		cp_inmod;
	uint16_t		cp_opcode;
	uint16_t		cp_opmod;
	uint32_t		cp_flags;
} mcxnex_cmd_post_t;


/*
 * The following are the Mcxnex Firmware Command routines that accessible
 * externally (i.e. throughout the rest of the Mcxnex driver software).
 * These include the all the alloc/free routines, some initialization
 * and cleanup routines, and the various specific Mcxnex firmware commands.
 */
int mcxnex_cmd_post(mcxnex_state_t *, mcxnex_cmd_post_t *);
int mcxnex_mbox_alloc(mcxnex_state_t *, mcxnex_mbox_info_t *, uint_t mbox_wait);
void mcxnex_mbox_free(mcxnex_state_t *, mcxnex_mbox_info_t *);
int mcxnex_cmd_complete_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe);
int mcxnex_inmbox_list_init(mcxnex_state_t *state);
int mcxnex_intr_inmbox_list_init(mcxnex_state_t *state);
int mcxnex_outmbox_list_init(mcxnex_state_t *state);
int mcxnex_intr_outmbox_list_init(mcxnex_state_t *state);
void mcxnex_inmbox_list_fini(mcxnex_state_t *state);
void mcxnex_intr_inmbox_list_fini(mcxnex_state_t *state);
void mcxnex_outmbox_list_fini(mcxnex_state_t *state);
void mcxnex_intr_outmbox_list_fini(mcxnex_state_t *state);
int mcxnex_outstanding_cmdlist_init(mcxnex_state_t *state);
void mcxnex_outstanding_cmdlist_fini(mcxnex_state_t *state);

/* Added for MemFree */
int mcxnex_map_cmd_post(mcxnex_state_t *state, mcxnex_dma_info_t *dinfo,
    uint16_t opcode, ddi_dma_cookie_t cookie, uint_t ccount);
int mcxnex_map_fa_cmd_post(mcxnex_state_t *state);
int mcxnex_run_fw_cmd_post(mcxnex_state_t *state);
int mcxnex_set_icm_size_cmd_post(mcxnex_state_t *state);
int mcxnex_map_icm_aux_cmd_post(mcxnex_state_t *state);
int mcxnex_map_icm_cmd_post(mcxnex_state_t *state);
int mcxnex_disable_lam_cmd_post(mcxnex_state_t *state);
int mcxnex_unmap_icm_cmd_post(mcxnex_state_t *state,
    mcxnex_dma_info_t *dma_info);
int mcxnex_unmap_icm_aux_cmd_post(mcxnex_state_t *state);
int mcxnex_unmap_fa_cmd_post(mcxnex_state_t *state);

/*
 * INIT_HCA and CLOSE_HCA - used for initialization and teardown of Mcxnex
 * device configuration
 */
int mcxnex_init_hca_cmd_post(mcxnex_state_t *state,
    mcxnex_hw_initqueryhca_t *inithca, uint_t sleepflag);
int mcxnex_close_hca_cmd_post(mcxnex_state_t *state, uint_t sleepflag);

/*
 * INIT_PORT, CLOSE_PORT, and SET_PORT - used for bring Mcxnex ports up and
 * down, and to set properties of each port (e.g. PortInfo capability mask)
 * NOTE:  New names for the commands in Mcxnex (previously init_ close_ and
 * set_ib
 */
int mcxnex_set_port_cmd_post(mcxnex_state_t *state,
    mcxnex_hw_set_port_t *initport, uint_t port, uint_t sleepflag);
int mcxnex_init_port_cmd_post(mcxnex_state_t *state, uint_t port,
    uint_t sleepflag);
int mcxnex_close_port_cmd_post(mcxnex_state_t *, uint_t, uint_t);
int mcxnex_stat_cfg_is_port_enabled(mcxnex_state_t *, int);


/*
 * This common function is used to post the following Mcxnex QP state
 * transition firmware commands:
 * RTS2SQD, TOERR, TORST, RST2INIT, INIT2INIT, INIT2RTR, RTR2RTS, RTS2RTS,
 * SQD2SQD, SQD2RTS, and SQERR2RTS.
 */
int mcxnex_cmn_qp_cmd_post(mcxnex_state_t *state, uint_t opcode,
    mcxnex_hw_qpc_t *qp, uint_t qpindx, uint32_t opmask, uint_t sleepflag);

/*
 * This common function is used to post the following Mcxnex query firmware
 * commands:
 * QUERY_DEV_LIM/CAP, QUERY_FW, QUERY_ADAPTER, QUERY_HCA, QUERY_MPT,
 * QUERY_EQ, QUERY_CQ, and QUERY_QP.
 */
int mcxnex_cmn_query_cmd_post(mcxnex_state_t *, uint_t opcode,
    uint_t opmod, uint_t queryindx, void *query, uint_t size, uint_t sleepflag);

/*
 * This common function is used to post the following Mcxnex resource ownership
 * firmware commands:
 * HW2SW_MPT, HW2SW_EQ, HW2SW_CQ, SW2HW_MPT, SW2HW_EQ, and SW2HW_CQ
 */
int mcxnex_cmn_ownership_cmd_post(mcxnex_state_t *state, uint_t opcode,
    void *hwrsrc, uint_t size, uint_t hwrsrcindx, uint_t sleepflag);

/*
 * MAD_IFC and helper functions - used for posting IB MADs to Mcxnex firmware.
 * The helper functions are for the MADs most frequently used by the Mcxnex
 * driver (internally).
 */
int mcxnex_mad_ifc_cmd_post(mcxnex_state_t *state, uint_t port,
    uint_t sleepflag, uint32_t *mad, uint32_t *resp);
int mcxnex_getportinfo_cmd_post(mcxnex_state_t *state, uint_t port,
    uint_t sleepflag, sm_portinfo_t *portinfo);
int mcxnex_getnodeinfo_cmd_post(mcxnex_state_t *state, uint_t sleepflag,
    sm_nodeinfo_t *nodeinfo);
int mcxnex_getnodedesc_cmd_post(mcxnex_state_t *state, uint_t sleepflag,
    sm_nodedesc_t *nodedesc);
int mcxnex_getguidinfo_cmd_post(mcxnex_state_t *state, uint_t port,
    uint_t guidblock, uint_t sleepflag, sm_guidinfo_t *guidinfo);
int mcxnex_getpkeytable_cmd_post(mcxnex_state_t *state, uint_t port,
    uint_t pkeyblock, uint_t sleepflag, sm_pkey_table_t *pkeytable);
int mcxnex_is_ext_port_counters_supported(mcxnex_state_t *state, uint_t port,
    uint_t sleepflag, int *ext_width_supported);
int mcxnex_getextperfcntr_cmd_post(mcxnex_state_t *state, uint_t port,
    uint_t sleepflag, mcxnex_hw_sm_extperfcntr_t *perfinfo);
int mcxnex_getperfcntr_cmd_post(mcxnex_state_t *state, uint_t port,
    uint_t sleepflag, mcxnex_hw_sm_perfcntr_t *perfinfo, int reset);
/*
 * WRITE_MTT - used for write MTT entries to the Mcxnex MTT table
 */
int mcxnex_write_mtt_cmd_post(mcxnex_state_t *state, mcxnex_rsrc_t *mtt,
    uint64_t start_addr, uint_t nummtt, uint_t sleepflag);

/*
 * SYNC_TPT - used to sync Mcxnex TPT caches
 */
int mcxnex_sync_tpt_cmd_post(mcxnex_state_t *state, uint_t sleepflag);

/*
 * MAP_EQ - used for map classes of events to Mcxnex event queues (EQ)
 */
int mcxnex_map_eq_cmd_post(mcxnex_state_t *state, uint_t map,
    uint_t eqcindx, uint64_t eqmapmask, uint_t sleepflag);

/*
 * RESIZE_CQ - used for resize completion queue (CQ)
 *	opmod 0 is resize cq.  opmod 1 is modify interrupt moderation.
 */
int mcxnex_resize_cq_cmd_post(mcxnex_state_t *state, mcxnex_hw_cqc_t *cqc,
    uint_t cqcindx, uint32_t *prod_indx, uint_t sleepflag);
int mcxnex_modify_cq_cmd_post(mcxnex_state_t *state, mcxnex_hw_cqc_t *cqc,
    uint_t cqcindx, uint_t opmod, uint_t sleepflag);

/*
 * CONF_SPECIAL_QP - used to configure a pair of queue pairs for use as
 * special QP.  Necessary to enable full QP0 and/or QP1 operation.
 */
int mcxnex_conf_special_qp_cmd_post(mcxnex_state_t *state, uint_t qpindx,
    uint_t qptype, uint_t sleepflag, uint_t opmod);

/*
 * MGID_HASH, READ_MGM, and WRITE_MGM - used for manipulation of the
 * hardware resource tables for multicast groups.
 *	NOTE: for intial implementation these functions retain their original
 *		names, though the proper mcxnex terminology is READ_MCG and
 *		WRITE_MCG - MGID_HASH retains its original name
 */
int mcxnex_mgid_hash_cmd_post(mcxnex_state_t *state, uint64_t mgid_h,
    uint64_t mgid_l, uint64_t *mgid_hash, uint_t sleepflag);
int mcxnex_read_mgm_cmd_post(mcxnex_state_t *state, mcxnex_hw_mcg_t *mcg,
    uint_t mcgindx, uint_t sleepflag);
int mcxnex_write_mgm_cmd_post(mcxnex_state_t *state, mcxnex_hw_mcg_t *mcg,
    uint_t mcgindx, uint_t sleepflag);

/*
 * MOD_STAT_CFG - used to configure (override) settings set in NVRAM before
 * a call to QUERY_DEV_LIM.  This is primarily used for SRQ settings in
 * the firmware.
 */
int mcxnex_mod_stat_cfg_cmd_post(mcxnex_state_t *state);

/*
 * MODIFY_MPT - used to change MPT attributes of a memory region.  This
 * was (Tavor/Arbel) primarily used for Resizing SRQs -- now may be used
 * to modify MPT paramters
 */
int mcxnex_modify_mpt_cmd_post(mcxnex_state_t *state, mcxnex_hw_dmpt_t *mpt,
    uint_t mptindx, uint_t flags, uint_t sleepflag);

/*
 * RESIZE_SRQ is new in mcxnex, replacing opcodes in modify_mpt.  It is used
 * to resize the SRQ, by passing the new information in the same format as
 * the original srqc, which the HCA will update appropriately
 */

int mcxnex_resize_srq_cmd_post(mcxnex_state_t *state, mcxnex_hw_srqc_t *srq,
    uint_t srqnum, uint_t sleepflag);

/*
 * CMD_NOP - used to test the interrupt/Event Queue mechanism.
 */
int mcxnex_nop_post(mcxnex_state_t *state, uint_t interval, uint_t sleep);

#endif	/* _MCXNEX_CMD_H */

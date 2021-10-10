/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_SCSI_ADAPTERS_IFPVAR_H
#define	_SYS_SCSI_ADAPTERS_IFPVAR_H

#include <sys/note.h>

#ifdef	__cplusplus
extern "C" {
#endif




/*
 * Convenient short hand defines
 */
#define	TRUE			 1
#define	FALSE			 0
#define	UNDEFINED		-1

#define	IFP_MAX_LUNS		16	/* max number of luns per target */

/*
 * This is the maximum number of luns that we will scan
 * for on devices that do not support the report_luns command.
 */
#define	IFP_MAX_LUNS_SCAN	1

#define	CNUM(ifp)		(ddi_get_instance(ifp->ifp_tran.tran_dev))

#define	IFP_RETRY_DELAY		5
#define	IFP_RETRIES		5	/* retry for initialize control block */
#define	IFP_FATAL_MBOX_RETRIES	1	/* mbox retries on a fatal event */
#define	IFP_INITIAL_SOFT_SPACE	5	/* Used for the softstate_init func */

#define	MSW(x)			(ushort_t)((((uint_t)x) >> 16) & 0xFFFF)
#define	LSW(x)			(ushort_t)((uint_t)(x) & 0xFFFF)

#define	MSB(x)			(uchar_t)(((ushort_t)(x) >> 8) & 0xFF)
#define	LSB(x)			(uchar_t)((ushort_t)(x) & 0xFF)

#define	TGT(sp)			(CMD2PKT(sp)->pkt_address.a_target)
#define	LUN(sp)			(CMD2PKT(sp)->pkt_address.a_lun)

/*
 * Interrupt actions returned by ifp_i_response_error()
 */
#define	ACTION_CONTINUE		1	/* Continue */
#define	ACTION_RETURN		2	/* Exit */
#define	ACTION_IGNORE		3	/* Ignore */

/*
 * Reset actions for ifp_i_reset_interface()
 */
#define	IFP_RESET_BUS_IF_BUSY	0x01	/* reset scsi bus if it is busy */
#define	IFP_FORCE_RESET_BUS	0x02	/* reset scsi bus on error reco */
#define	IFP_DOWNLOAD_FW_ON_ERR	0x04	/* Download the firmware after reset */
#define	IFP_FIRMWARE_ERROR	0x08    /* Sys_Err Async Event happened */
#define	IFP_DONT_WAIT_FOR_FW_READY	0x10    /* poll for fw to go ready */

/*
 * firmware download options for ifp_i_download_fw()
 */
#define	IFP_DOWNLOAD_FW_OFF		0
#define	IFP_DOWNLOAD_FW_IF_NEWER	1
#define	IFP_DOWNLOAD_FW_ALWAYS		2

/*
 * timeout values
 */
#define	IFP_LOOPDOWN_TIME	drv_usectohz((clock_t)15000000)	/* in ticks */
#define	IFP_GRACE		10	/* Timeout margin (sec.) */
#define	IFP_TIMEOUT_DELAY(secs, delay)	(secs * (1000000 / delay))

/*
 * delay time for polling loops
 */
#define	IFP_NOINTR_POLL_DELAY_TIME		1000	/* usecs */

/*
 * busy wait delay time after chip reset
 */
#define	IFP_CHIP_RESET_BUSY_WAIT_TIME		100	/* usecs */

/*
 * timeout for IFP coming out of reset
 */
#define	IFP_RESET_WAIT				50000	/* usecs */
#define	IFP_SOFT_RESET_TIME			1	/* second */
#define	IFP_SOFT_RESET_WAIT_RETRIES		50	/* times to poll */

/*
 * Default secs for watchdog thread to be invoked. Should be greater
 * than ssd_io_time(pkt_time)
 */
#define	IFP_DEFLT_WATCHDOG_SECS			60

/*
 * Debugging macros
 */
#ifdef IFPDEBUG
#define	IFP_DEBUG(level, args) \
	if (ifpdebug >= (level)) ifp_i_log args
#else	/* IFPDEBUG */
#define	IFP_DEBUG(level, args)
#define	ifpdebug	(0)
#define	INFORMATIVE	(0)
#define	DEBUGGING	(0)
#define	DEBUGGING_ALL	(0)
#endif /* IFPDEBUG */

/*
 * Size definitions for request and response queues.
 */
#define	IFP_MAX_REQUESTS	512
#define	IFP_MAX_RESPONSES	256
#define	IFP_QUEUE_SIZE		\
	(IFP_MAX_REQUESTS  * sizeof (struct ifp_request) + \
	    IFP_MAX_RESPONSES * sizeof (struct ifp_response))

#define	IFP_INIT_WAIT_TIMEOUT	60000000

/*
 * Size definition for FC-AL Position Map
 */
#define	IFP_FCAL_MAP_SIZE	128

#define	IFP_MAX_TARGETS		126

/*
 * definitions to get minors for controllers/HBA, Use this
 * get controller instance mapping on /dev/cfg as link to /devices...
 */
#define	INST_SHIFT4MINOR	6
#define	IFP_BASE_MINOR		(1 << (INST_SHIFT4MINOR - 1))
#define	IFP_DEVCTL_MINOR	(IFP_BASE_MINOR + 1)
#define	IFP_FC_MINOR		(IFP_BASE_MINOR + 2)
#define	IFP_INST2DEVCTL_MINOR(x)(((x) << INST_SHIFT4MINOR) | IFP_DEVCTL_MINOR)
#define	IFP_INST2FC_MINOR(x)	(((x) << INST_SHIFT4MINOR) | IFP_FC_MINOR)
#define	IFP_MINOR2INST(x)	((x) >> INST_SHIFT4MINOR)

#define	IFP_NUM_HASH_QUEUES	32
#define	IFP_HASH(x)		((x[0]+x[1]+x[2]+x[3]+x[4]+x[5]+x[6]+x[7]) &\
			(IFP_NUM_HASH_QUEUES - 1))

/*
 * Frame lengths
 */
#define	IFP_FRAME_LENGTH_512	512
#define	IFP_FRAME_LENGTH_1024	1024
#define	IFP_FRAME_LENGTH_2048	2048

/*
 * ISP2100 Initialization Control block
 */
struct ifp_icb {
	uchar_t			icb_reserved0;
	uchar_t			icb_version;
	ushort_t		icb_fw_options;
	ushort_t		icb_max_frame_length;
	ushort_t		icb_max_resource_allocation;
	ushort_t		icb_execution_throttle;
	uchar_t			icb_retry_delay;
	uchar_t			icb_retry_count;
	ushort_t		icb_port_name[4];
	ushort_t		icb_hard_address;
	uchar_t			icb_reserved1;
	uchar_t			icb_target_dev_type;
	ushort_t		icb_node_name[4];
	ushort_t		icb_request_out;
	ushort_t		icb_response_in;
	ushort_t		icb_request_q_length;
	ushort_t		icb_response_q_length;
	ushort_t		icb_request_q_addr[4];
	ushort_t		icb_response_q_addr[4];
};

#define	ICB_VERSION_01			1

#define	ICB_VERSION			ICB_VERSION_01

/*
 * Defines for firmware options
 */
#define	ICB_HARD_ADDRESS_ENABLE		0x0001
#define	ICB_FAIRNESS			0x0002
#define	ICB_FULL_DUPLEX			0x0004
#define	ICB_FAST_STATUS			0x0008
#define	ICB_TARGET_MODE_ENABLE		0x0010
#define	ICB_INITIATOR_MODE_DISABLE	0x0020
#define	ICB_ENABLE_ADISC		0x0040
#define	ICB_ENABLE_TARGET_DEV_TYPE	0x0080
#define	ICB_ENABLE_PDB_UPDATE_EVENT	0x0100
#define	ICB_DISABLE_INITIAL_LIP		0x0200
#define	ICB_DESCENDING_LOOP_ID		0x0400
#define	ICB_PREVIOUSLY_ASSIGNED_ALPA	0x0800
#define	ICB_FREEZE_ON_Q_FULL		0x1000
#define	ICB_ENABLE_FULL_LOGIN_ON_LIP	0x2000
#define	ICB_USE_NODE_NAME		0x4000

/*
 * Port database structure
 * Note: This is returned in little endian format by the FW
 */
struct ifp_portdb {
	uchar_t			pdb_options;
	uchar_t			pdb_control;
	uchar_t			pdb_master_state;
	uchar_t			pdb_slave_state;
	uchar_t			pdb_hard_address[4];
	uchar_t			pdb_port_id[4];
	uchar_t			pdb_node_name[8]; /* comes in little endian */
	uchar_t			pdb_port_name[8]; /* comes in little endian */
	uchar_t			pdb_reserved0[0x2c];
	ushort_t		pdb_prli_payload_length;
	uchar_t			pdb_reserved1[4];
	uchar_t			pdb_prli_acc_word3_bits07;
	uchar_t			pdb_reserved2[0x31]; /* make it 128 bytes */
};
typedef struct ifp_portdb ifp_portdb_t;

/*
 * Defines for bits in pdb_options & PRLI words
 */
#define	PDB_HARD_ADDRESS_FROM_ADISC			0x0002

#define	PDB_PRLI_TARGET_FUNCTION			0x10
#define	PDB_PRLI_INITIATOR_FUNCTION			0x20

#define	IFP_PORTDB_NODE_NOTSTABLE_YET(portdb)	\
	(((portdb).pdb_master_state != 0x6 && \
		(portdb).pdb_master_state != 0x7) || \
		((portdb).pdb_master_state == 0x7 && \
		(portdb).pdb_prli_payload_length == 0))

#define	IFP_PORTDB_TARGET_MODE(portdb)	\
	((portdb).pdb_prli_acc_word3_bits07 & PDB_PRLI_TARGET_FUNCTION)

#define	IFP_PORTDB_INITIATOR_MODE(portdb)	\
	((portdb).pdb_prli_acc_word3_bits07 & PDB_PRLI_INITIATOR_FUNCTION)

enum alpa_status {IFP_PROC_ALPA_SUCCESS,
	IFP_PROC_ALPA_NO_TARGET_MODE, IFP_PROC_ALPA_NO_PORTDB,
	IFP_PROC_ALPA_LIP_OCCURED, IFP_PROC_ALPA_NO_HARD_ADDR,
	IFP_PROC_ALPA_DUPLICATE_WWNS, IFP_PROC_ALPA_DUPLICATE_ADDRS,
	IFP_PROC_ALPA_ALLOC_FAILED, IFP_PROC_ALPA_GET_DEVICE_TYPE,
	IFP_PROC_ALPA_MISBEHAVING_NODE, IFP_PROC_ALPA_GOT_SOFT_ADDR,
	IFP_PROC_ALPA_FW_OUT_OF_RESOURCES, IFP_PROC_ALPA_TRY_LATER,
	IFP_PROC_ALPA_PRLI_TIMED_OUT, IFP_PROC_ALPA_REPORT_LUNS_TIMED_OUT,
	IFP_PROC_ALPA_INQUIRY_TIMED_OUT};

char *alpa_status_msg[] = {
	"Successful",
	"Node does not support target mode functions",
	"Unable to get port database",
	"Operation aborted due to a LIP occurring",
	"Target does not have a hard address",
	"Duplicate WWNs",
	"Duplicate switch settings",
	"Unable to allocate memory for target structure",
	"Successful -- need to fill device type",
	"Node claims to be neither a target nor an initiator",
	"Target did not get its hard address",
	"ISP fw temporarily out of resources",
	"ISP fw not done with PRLI for this node",
	"ISP fw PLOGI or PRLI timed out for this node",
	"REPORT LUNS timed out for this node",
	"INQUIRY timed out for this node"
};

/*
 * ISP request packet as defined by the Firmware Interface
 */
struct ifp_dataseg {
	int	d_base;
	int	d_count;
};


struct cq_header {
#if defined(_BIG_ENDIAN)
	uchar_t		cq_entry_count;
	uchar_t		cq_entry_type;
	uchar_t		cq_flags;
	uchar_t		cq_seqno;
#else
	uchar_t		cq_entry_type;
	uchar_t		cq_entry_count;
	uchar_t		cq_seqno;
	uchar_t		cq_flags;
#endif
};

/* Type 11 request */
struct ifp_request {
	struct cq_header	req_header;
	uint_t			req_token;

#if defined(_BIG_ENDIAN)
	uchar_t			req_target;
	uchar_t			req_lun_trn;
#else
	uchar_t			req_lun_trn;
	uchar_t			req_target;
#endif

	ushort_t		req_cdblen;
#define	req_modifier		req_cdblen	/* marker packet */
#define	req_ext_lun		req_cdblen	/* extended lun */
	ushort_t		req_flags;
	ushort_t		req_reserved;
	ushort_t		req_time;
	ushort_t		req_seg_count;

	uint_t			req_cdb[4];
	uint_t			req_byte_count;
	struct ifp_dataseg	req_dataseg[3];
};

typedef struct ifp_request	ifp_request_t;

#if !defined(offsetof)
#define		offsetof(s, m)	(size_t)(&(((s *)0)->m))
#endif

#define	IFP_REQ_TOKEN_OFF	offsetof(ifp_request_t, req_token)
#define	IFP_REQ_TARG_OFF	offsetof(ifp_request_t, req_target)
#define	IFP_REQ_CDB_OFF		offsetof(ifp_request_t, req_cdb)
#define	IFP_REQ_BYTE_CNT_OFF	offsetof(ifp_request_t, req_byte_count)

#define	IFP_UPDATE_QUEUE_SPACE(ifp) \
	ifp->ifp_request_out = IFP_GET_REQUEST_OUT(ifp); \
	if (ifp->ifp_request_in == ifp->ifp_request_out) { \
		ifp->ifp_queue_space = IFP_MAX_REQUESTS - 1; \
	} else if (ifp->ifp_request_in > ifp->ifp_request_out) { \
		ifp->ifp_queue_space = ((IFP_MAX_REQUESTS - 1) - \
		    (ifp->ifp_request_in - ifp->ifp_request_out)); \
	} else { \
		ifp->ifp_queue_space = ifp->ifp_request_out - \
		    ifp->ifp_request_in - 1; \
	}

/*
 * Header flags definitions
 */
#define	CQ_FLAG_FULL		0x01
#define	CQ_FLAG_BADTYPE		0x02
#define	CQ_FLAG_BADPACKET	0x04
#define	CQ_FLAG_BADHEADER	0x08
#define	CQ_FLAG_BADORDER	0x10
#define	CQ_FLAG_ERR_MASK	\
	(CQ_FLAG_FULL|CQ_FLAG_BADTYPE|CQ_FLAG_BADPACKET|CQ_FLAG_BADHEADER| \
	CQ_FLAG_BADORDER)

/*
 * Header entry_type definitions
 */
#define	CQ_TYPE_REQUEST		0x11
#define	CQ_TYPE_DATASEG		0x02
#define	CQ_TYPE_RESPONSE	0x03
#define	CQ_TYPE_MARKER		0x04

#define	CQ_TYPE_RESPONSE_CONT	0x10
#define	CQ_TYPE_REQUEST_64	0x19

/*
 * Copy cdb into request using 32-bit word transfers to save time.
 */
#define	IFP_LOAD_REQUEST_CDB(req, sp, cdbsize) { \
	uint_t *cdbp, *sp_cdbp; \
	cdbp = (uint_t *)(req)->req_cdb; \
	sp_cdbp = (uint_t *)CMD2PKT(sp)->pkt_cdbp; \
	*cdbp = *sp_cdbp, *(cdbp+1) = *(sp_cdbp+1), \
	*(cdbp+2) = *(sp_cdbp+2); \
}

/*
 * marker packet (req_modifier) values
 */
#define	SYNCHRONIZE_NEXUS	0
#define	SYNCHRONIZE_TARGET	1
#define	SYNCHRONIZE_ALL		2

/*
 * request flag values
 */
#define	IFP_REQ_FLAG_HEAD_TAG		0x0002
#define	IFP_REQ_FLAG_ORDERED_TAG	0x0004
#define	IFP_REQ_FLAG_SIMPLE_TAG		0x0008
#define	IFP_REQ_FLAG_DATA_READ		0x0020
#define	IFP_REQ_FLAG_DATA_WRITE		0x0040
#define	IFP_REQ_FLAG_STOP_QUEUE		0x2000
#define	IFP_REQ_FLAG_HI_PRI		0x8000

/*
 * translate scsi_pkt tag flags into IFP request packet flags
 * It would be illegal if two flags are set; the driver does not
 * check for this. If none set, make it simple tag.
 */
#define	IFP_SET_PKT_FLAGS(scsa_flags, ifp_flags) {		\
	(ifp_flags) = ((scsa_flags) >> 11) & 0xe; /* tags */	\
	(ifp_flags) ? (ifp_flags) :				\
		((ifp_flags) |= IFP_REQ_FLAG_SIMPLE_TAG);	\
}

/*
 * XXX: Note, this request queue macro *ASSUMES* that queue full cannot
 *	occur.
 */
#define	IFP_GET_NEXT_REQUEST_IN(ifp, ptr) { \
	(ptr) = (ifp)->ifp_request_ptr; \
	if ((ifp)->ifp_request_in == (IFP_MAX_REQUESTS - 1)) {	 \
		(ifp)->ifp_request_in = 0; \
		(ifp)->ifp_request_ptr = (ifp)->ifp_request_base; \
	} else { \
		(ifp)->ifp_request_in++; \
		(ifp)->ifp_request_ptr++; \
	} \
}

/*
 * slots queue for ifp timeout handling
 * Must be a multiple of 8
 */
#define	IFP_DISK_QUEUE_DEPTH	1
#define	IFP_MAX_SLOTS		((IFP_FCAL_MAP_SIZE * IFP_DISK_QUEUE_DEPTH) + \
				IFP_MAX_REQUESTS)

/*
 * IFP response packet as defined by the Firmware Interface
 */
struct ifp_response {
	struct cq_header	resp_header;
	uint_t			resp_token;

	ushort_t		resp_scb;	/* SCSI status */
	ushort_t		resp_reason;	/* Completion status */
	ushort_t		resp_state;
	ushort_t		resp_status_flags;
	ushort_t		resp_resp_info_len;
	ushort_t		resp_sense_data_len;
	uint_t			resp_resid;
	uchar_t			resp_fcp_resp_info[8];
	uchar_t			resp_request_sense[32];
};

typedef struct ifp_response	ifp_response_t;

#define	IFP_RESP_TOKEN_OFF	offsetof(ifp_response_t, resp_token)
#define	IFP_RESP_STATUS_OFF	offsetof(ifp_response_t, resp_scb)
#define	IFP_RESP_RESID_OFF	offsetof(ifp_response_t, resp_resid)
#define	IFP_RESP_FCP_RESP_OFF	offsetof(ifp_response_t, resp_fcp_resp_info)
#define	IFP_RESP_RQS_OFF	offsetof(ifp_response_t, resp_request_sense)

/*
 * Bit definitions for byte 2 of FCP_STATUS fields of FCP_RSP IU. These
 * are the bits that can be set in the upper byte of resp_scb.
 */
#define	IFP_RESID_UNDER			0x0800
#define	IFP_RESID_OVER			0x0400
#define	IFP_SENSE_LEN_VALID		0x0200
#define	IFP_RESP_INFO_LEN_VALID		0x0100

/*
 * Defines for resp_reason -- this is the command completion status
 */
#define	IFP_CMD_CMPLT			0x00	/* no transport errors */
#define	IFP_CMD_INCOMPLETE		0x01	/* abnormal transport state */
#define	IFP_CMD_DMA_DERR		0x02	/* DMA direction error */
#define	IFP_CMD_TRAN_ERR		0x03	/* unspecified xport error */
#define	IFP_CMD_RESET			0x04	/* reset aborted transport */
#define	IFP_CMD_ABORTED			0x05	/* aborted on request */
#define	IFP_CMD_TIMEOUT			0x06	/* command timed out */
#define	IFP_CMD_DATA_OVR		0x07	/* data overrun-discard extra */
#define	IFP_CMD_ABORT_REJECTED		0x0e	/* target rejected abort msg */
#define	IFP_CMD_RESET_REJECTED		0x12	/* target rejected reset msg */
#define	IFP_CMD_DATA_UNDER		0x15	/* data underrun */
#define	IFP_CMD_QUEUE_FULL		0x1c	/* queue full SCSI status */
#define	IFP_CMD_PORT_UNAVAIL		0x28	/* port unavailable */
#define	IFP_CMD_PORT_LOGGED_OUT		0x29	/* port loged out */
#define	IFP_CMD_PORT_CONFIG_CHANGED	0x2a	/* port name changed */

/*
 * Defines for resp_state -- this is the state flags field
 */
#define	IFP_STATE_SENT_CMD	0x0400	/* Command successfully sent */
#define	IFP_STATE_XFERRED_DATA	0x0800	/* Data transfer took place */
#define	IFP_STATE_GOT_STATUS	0x1000	/* SCSI status received */
#define	IFP_STATE_XFER_COMPLT	0x4000	/* Xferred all data specified in IOCB */

/*
 * Defines for resp_status_flags -- this is the status flags field
 */
#define	IFP_STAT_BUS_RESET	0x0008	/* Bus reset */
#define	IFP_STAT_DEV_RESET	0x0010	/* Target reset */
#define	IFP_STAT_ABORTED	0x0020	/* Command was aborted */
#define	IFP_STAT_TIMEOUT	0x0040	/* Command experienced a timeout */

#define	IFP_GET_NEXT_RESPONSE_OUT(ifp, ptr) { \
	(ptr) = (ifp)->ifp_response_ptr; \
	if ((ifp)->ifp_response_out == (IFP_MAX_RESPONSES - 1)) {  \
		(ifp)->ifp_response_out = 0; \
		(ifp)->ifp_response_ptr = (ifp)->ifp_response_base; \
	} else { \
		(ifp)->ifp_response_out++; \
		(ifp)->ifp_response_ptr++; \
	} \
}

#define	IFP_RESPONSE_STATUS_CONTINUATION(header) \
	((header).cq_entry_type == CQ_TYPE_RESPONSE_CONT)

#define	IFP_IS_RESPONSE_INVALID(header) \
	((header).cq_entry_type != CQ_TYPE_RESPONSE && \
	(header).cq_entry_type != CQ_TYPE_REQUEST && \
	(header).cq_entry_type != CQ_TYPE_RESPONSE_CONT)

#define	IFP_GET_PKT_STATE(state, ret)	{ \
	(ret) = (STATE_GOT_BUS); \
	((state) & IFP_STATE_GOT_STATUS) ? (ret) |= STATE_GOT_STATUS : (0); \
	((state) & IFP_STATE_SENT_CMD) ? (ret) |= STATE_SENT_CMD : (0); \
	((state) & IFP_STATE_XFERRED_DATA) ? (ret) |= STATE_XFERRED_DATA : (0);\
}

#define	IFP_GET_PKT_STATS(stats, ret)	{ \
	(ret) = 0; \
	((stats) & IFP_STAT_BUS_RESET)? (ret) |= STAT_BUS_RESET : (0); \
	((stats) & IFP_STAT_DEV_RESET)? (ret) |= STAT_DEV_RESET : (0); \
	((stats) & IFP_STAT_ABORTED)? (ret) |= STAT_ABORTED : (0); \
	((stats) & IFP_STAT_TIMEOUT) ? (ret) |= STAT_TIMEOUT : (0); \
}

#define	IFP_SET_REASON(sp, reason) { \
	if ((sp) && CMD2PKT(sp)->pkt_reason == CMD_CMPLT) \
		CMD2PKT(sp)->pkt_reason = (reason); \
}

/*
 * mutex and semaphore short hands
 */
#define	IFP_HP_DAEMON_CV(ifp)	(&ifp->ifp_hp_daemon_cv)
#define	IFP_HP_DAEMON_MUTEX(ifp) (&ifp->ifp_hp_daemon_mutex)

#define	IFP_MBOX_MUTEX(ifp)	(&ifp->ifp_mbox.mbox_mutex)
#define	IFP_REQ_MUTEX(ifp)	(&ifp->ifp_request_mutex)
#define	IFP_RESP_MUTEX(ifp)	(&ifp->ifp_response_mutex)
#define	IFP_WAITQ_MUTEX(ifp)	(&ifp->ifp_waitq_mutex)


#define	IFP_MUTEX_ENTER(ifp)	mutex_enter(IFP_RESP_MUTEX(ifp)),	\
				mutex_enter(IFP_REQ_MUTEX(ifp))
#define	IFP_MUTEX_EXIT(ifp)	mutex_exit(IFP_RESP_MUTEX(ifp)),	\
				mutex_exit(IFP_REQ_MUTEX(ifp))
/*
 * HBA interface macros
 */
#define	SDEV2TRAN(sd)		((sd)->sd_address.a_hba_tran)
#define	SDEV2ADDR(sd)		(&((sd)->sd_address))
#define	PKT2TRAN(pkt)		((pkt)->pkt_address.a_hba_tran)
#define	ADDR2TRAN(ap)		((ap)->a_hba_tran)


#define	TRAN2IFP(tran)		((struct ifp *)(tran)->tran_hba_private)
#define	SDEV2IFP(sd)		(TRAN2IFP(SDEV2TRAN(sd)))
#define	PKT2IFP(pkt)		(TRAN2IFP(PKT2TRAN(pkt)))
#define	ADDR2IFP(ap)		(TRAN2IFP(ADDR2TRAN(ap)))

#define	CMD2ADDR(cmd)		(&CMD2PKT(cmd)->pkt_address)
#define	CMD2TRAN(cmd)		(CMD2PKT(cmd)->pkt_address.a_hba_tran)
#define	CMD2IFP(cmd)		(TRAN2IFP(CMD2TRAN(cmd)))

#define	ADDR2LUN(ap)		((ifp_lun_t *)((ap)->a_hba_tran->\
					tran_tgt_private))
#define	CMD2LUN(cmd)		(ADDR2LUN(CMD2ADDR((cmd))))
#define	PKT2LUN(pkt)		(ADDR2LUN(&(pkt)->pkt_address))

/*
 * deadline slot structure for timeout handling
 */
struct ifp_slot {
	struct ifp_cmd *slot_cmd;
	clock_t		slot_deadline;
};

struct ifp_hp_elem {
	int			what;
	struct ifp_hp_elem	*next;
	dev_info_t		*dip;
	struct ifp_target	*target;
	struct ifp		*ifp;
};
#define	IFP_ONLINE	0
#define	IFP_OFFLINE	1

/*
 * Per LUN info
 */
struct ifp_lun {
	struct ifp_target	*ifpl_target;	/* back ptr to target struct */
	struct ifp_lun		*ifpl_next;	/* next on lun list */
	ushort_t		ifpl_lun_num;	/* number of this LUN */
	uchar_t			ifpl_device_type; /* dev type at LUN */
	uint_t			ifpl_state;	/* target state */
	dev_info_t		*ifpl_dip;	/* target/lun's devinfo */
	struct scsi_inquiry	*ifpl_inq;	/* target/lun's inquiry data */
};
typedef struct ifp_lun	ifp_lun_t;
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp_lun::ifpl_inq))

#define	IFPL_LUN_USED			0x0001	/* lun_instance is not free */
#define	IFPL_LUN_INIT_DONE		0x0002	/* lun_init was done */

/*
 * Per target info
 */
struct ifp_target {
	struct scsi_hba_tran	*ifpt_tran;	/* hba target is on */
	struct ifp_target	*ifpt_next;	/* next on target list */
	kmutex_t		ifpt_mutex;	/* lock while manipulating */
	uint_t			ifpt_lip_cnt;	/* lip count target has seen */
	uint_t			ifpt_state;	/* target state */
	uchar_t			ifpt_node_wwn[FC_WWN_SIZE];
	uchar_t			ifpt_port_wwn[FC_WWN_SIZE];
	ifp_lun_t		ifpt_lun;	/* LUN 0 structure */
	uchar_t			ifpt_al_pa;	/* hard or soft assigned */
	uchar_t			ifpt_hard_address; /* hard address */
	uchar_t			ifpt_loop_id;	/* hard assigned */
};
typedef struct ifp_target	ifp_target_t;

/*
 * target state defines
 */
#define	IFPT_TARGET_OFFLINE		0x0001	/* target is offline */
#define	IFPT_TARGET_INIT_DONE		0x0002	/* tgt_init was done */
#define	IFPT_TARGET_MARK		0x0004	/* marked for offline */
#define	IFPT_TARGET_NEEDS_MARKER	0x0008	/* send marker to FW */
#define	IFPT_TARGET_NEEDS_LIP		0x0010	/* force a LIP next time */
#define	IFPT_TARGET_BUSY		0x0020	/* target busy w/ autconfig */

/*
 * ifp softstate structure
 */
struct ifp {
	/*
	 * Transport structure for this instance of the hba
	 */
	scsi_hba_tran_t		*ifp_tran;

	/*
	 * dev_info_t reference can be found in the transport structure
	 */
	dev_info_t		*ifp_dip;

	/*
	 * Interrupt block cookie
	 */
	ddi_iblock_cookie_t	ifp_iblock;

	/*
	 * linked list of all ifp's for ifp_intr_loop() and debugging
	 */
	struct ifp		*ifp_next;

	/*
	 * Firmware revision number
	 */
	ushort_t		ifp_major_rev;
	ushort_t		ifp_minor_rev;
	uint16_t		ifp_subminor_rev;

	/*
	 * scsi_reset_delay per ifp
	 */
	uint_t			ifp_scsi_reset_delay;

	/*
	 * suspended flag for power management
	 */
	uchar_t			ifp_suspended;

	/*
	 * IFP Hardware register pointers.
	 */
	struct ifp_biu_regs	*ifp_biu_reg;

	/*
	 * Initialization Control Block
	 */
	struct ifp_icb		ifp_icb;

	/*
	 * mbox values are stored here before and after the mbox cmd
	 * (protected by semaphore inside ifp_mbox)
	 */
	struct ifp_mbox		ifp_mbox;

	/*
	 * to indicate if a thread other than ifp_intr took care of ints
	 */
	uchar_t			ifp_polled_intr;

	/*
	 * shutdown flag if things get really confused
	 */
	uchar_t			ifp_shutdown;

	/*
	 * Tell the world that we are handling a fw crash
	 */
	uchar_t			ifp_handling_fatal_aen;

	/*
	 * for watchdog timing
	 */
	uchar_t			ifp_alive;

	/*
	 * tells if the queues are inited yet
	 */
	int			ifp_que_inited;

	/*
	 * finish loop init if LIP not seen by this time
	 */
	clock_t			ifp_loopdown_timeout;

	/*
	 * request and response queue dvma space
	 */
	caddr_t			ifp_cmdarea;
	ddi_dma_cookie_t	ifp_dmacookie;
	ddi_dma_handle_t	ifp_dmahandle;
	ddi_acc_handle_t	ifp_dma_acc_handle;
	uint_t			ifp_request_dvma;
	uint_t			ifp_response_dvma;
	/*
	 * data access handles
	 */
	ddi_acc_handle_t	ifp_pci_config_acc_handle;
	ddi_acc_handle_t	ifp_biu_acc_handle;

	/*
	 * handle for getting FC-AL map
	 */
	ddi_dma_handle_t	ifp_fcal_maphandle;

	/*
	 * for reading port database.
	 */
	ddi_dma_handle_t	ifp_fcal_porthandle;

	/*
	 * And cookies for the same
	 */
	ddi_dma_cookie_t	ifp_map_dmacookie;
	ddi_dma_cookie_t	ifp_portdb_dmacookie;

	/*
	 * waitQ (used for storing cmds in case request mutex is held)
	 */
	kmutex_t		ifp_waitq_mutex;
	struct	ifp_cmd		*ifp_waitf;
	struct	ifp_cmd		*ifp_waitb;
	timeout_id_t		ifp_waitq_timeout;

	int			ifp_burst_size;

	/*
	 * time since reset/lip initialization for bus_config
	 */
	int64_t			ifp_reset_time;

	/*
	 * for keeping track of targets
	 */
	ifp_target_t		*ifp_targets[IFP_MAX_TARGETS];

	/*
	 * for quick lookup up based on WWNs
	 */
	ifp_target_t		*ifp_wwn_lists[IFP_NUM_HASH_QUEUES];

	/*
	 * current lip count
	 */
	uint_t			ifp_lip_cnt;

	/*
	 * Most recent loop map
	 */
	uchar_t			ifp_loop_map[IFP_FCAL_MAP_SIZE];

	/*
	 * Most recent alpa states
	 */
	uchar_t			ifp_alpa_states[IFP_FCAL_MAP_SIZE];

	/*
	 * Portdb structure
	 */
	ifp_portdb_t		ifp_portdb;

	/*
	 * AL-PA for the HBA
	 */
	uchar_t			ifp_my_alpa;

	/*
	 * Node name and port name for the HBA
	 */
	la_wwn_t		ifp_my_wwn;
	la_wwn_t		ifp_my_port_wwn;

	/*
	 * IFP input request and output response queue pointers
	 * and mutexes protecting request and response queue.
	 */
	uint_t			ifp_queue_space;
	kmutex_t		ifp_request_mutex;
	kmutex_t		ifp_response_mutex;
	ushort_t		ifp_request_in;
	ushort_t		ifp_request_out;
	ushort_t		ifp_response_in;
	ushort_t		ifp_response_out;

	struct ifp_request	*ifp_request_ptr,
				*ifp_request_base;
	struct ifp_response	*ifp_response_ptr,
				*ifp_response_base;

	kstat_t			*ifp_ksp;

#ifdef IFP_PERF
	/*
	 * performance counters
	 */
	ulong_t			ifp_request_count,
				ifp_mail_requests;
	ulong_t			ifp_intr_count,
				ifp_perf_ticks;
	ulong_t			ifp_rpio_count,
				ifp_wpio_count;
#endif
	/*
	 * State
	 */
	/* this is for implementing leaf interfaces */
	uint_t			ifp_softstate;

	/* this is for maintaining board state */
	uint_t			ifp_state;

	/* this is for maintaining lip state */
	uint_t			ifp_lip_state;

	/*
	 * These are for handling cmd. timeouts.
	 *
	 * Because the IFP request queue is a round-robin, entries
	 * in progress can be overwritten. In order to provide crash
	 * recovery, we have to keep a list of requests in progress
	 * here.
	 */
	ushort_t		ifp_free_slot;
	ushort_t		ifp_last_slot_watched;

	/*
	 * list of reset notification requests
	 */
	struct scsi_reset_notify_entry	*ifp_reset_notify_listf;
	struct kmem_cache		*ifp_kmem_cache;

	struct	ifp_slot	ifp_slots[IFP_MAX_SLOTS];

	/*
	 * FC-AL Position Map
	 */
	uchar_t			ifp_fcal_map[IFP_FCAL_MAP_SIZE];

	int			ifp_hp_thread_go_away;
	kcondvar_t		ifp_hp_daemon_cv;
	kmutex_t		ifp_hp_daemon_mutex;
	struct ifp_hp_elem	*ifp_hp_elem_head;
	struct ifp_hp_elem	*ifp_hp_elem_tail;
	ndi_event_hdl_t		ifp_ndi_event_hdl;
	ndi_event_set_t		ifp_ndi_events;

	/*
	 * to tell that diags are underway
	 */
	uint_t			ifp_running_diags;

	/*
	 * retry count for scan portdb
	 */
	uint_t			ifp_scandb_retry;

	/*
	 * kstat info
	 */
	struct ifp_stats	ifp_stats;

	/* This is the chip ID; 2100 or 2200 */
	uint16_t		ifp_chip_id;
	uint16_t		ifp_chip_rev;
	uint16_t		ifp_chip_reg_cnt;

#ifdef STE_TARGET_MODE
	void			*ifp_tm_private;
	void			(*ifp_tm_hba_event)();
	ddi_dma_attr_t		*ifp_tm_dma_attr;
	ushort_t		ifp_serial_num;
	uint16_t		ifp_tm_hard_loop_id;
#endif /* STE_TARGET_MODE */
};
typedef struct ifp		ifp_t;

/*
 * Defines for ifp_alpa_states
 */
#define	IFPA_ALPA_INIT			0
#define	IFPA_ALPA_NEEDS_RETRY		1
#define	IFPA_ALPA_ONLINE		2
#define	IFPA_ALPA_OFFLINE		3

/*
 * Defines for ifp_softstate
 */
#define	IFP_OPEN			0x0000001	/* opened */
#define	IFP_DRAINING			0x0000002	/* drain in progress */
#ifdef STE_TARGET_MODE
#define	IFP_IFPTM_ATTACHED		0x0010000	/* ifptm attached */
#define	IFP_IFPTM_DETACHED		0x0020000	/* ifptm detached */
#endif /* STE_TARGET_MODE */

/*
 * Defines for ifp_state (actually, loop state)
 */
#define	IFP_STATE_OFFLINE		1	/* initial loop state */
#define	IFP_STATE_FORCED_FINISH_INIT	2	/* loop remained down for a  */
						/* long time */
#define	IFP_STATE_ONLINE		3	/* loop is up and running */

/*
 * Defines for ifp_lip_state
 */
#define	IFPL_EXPECTING_LIP		0x0001
#define	IFPL_GETTING_INFO		0x0002
#define	IFPL_HANDLING_LIP		0x0004
#define	IFPL_NEED_LIP_FORCED		0x0008
#define	IFPL_GET_HBA_ALPA		0x0020
#define	IFPL_RETRY_SCAN_PORTDB		0x0040

/*
 * Defines for event tags
 */
#define	IFP_EVENT_TAG_INSERT		0
#define	IFP_EVENT_TAG_REMOVE		1
/*
 * Warlock directives
 */
_NOTE(LOCK_ORDER(ifp::ifp_response_mutex ifp::ifp_request_mutex
	ifp_target::ifpt_mutex))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp::ifp_tran))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp::ifp_major_rev))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp::ifp_minor_rev))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp::ifp_subminor_rev))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp::ifp_my_alpa))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp::ifp_cmdarea ifp::ifp_dmahandle))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp::ifp_dmacookie))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp_target))

_NOTE(SCHEME_PROTECTS_DATA("Failure Mode", ifp::ifp_shutdown))
_NOTE(SCHEME_PROTECTS_DATA("Save Sharing", ifp::ifp_alive))
_NOTE(SCHEME_PROTECTS_DATA("Mutex", ifp::ifp_mbox))
_NOTE(SCHEME_PROTECTS_DATA("Mutexes Held", ifp::ifp_wwn_lists
	ifp::ifp_stats))
_NOTE(SCHEME_PROTECTS_DATA("Mutexes Held", ifp_hp_elem))

_NOTE(SCHEME_PROTECTS_DATA("Watch Thread", ifp::ifp_last_slot_watched))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_request_mutex, ifp::ifp_free_slot))

_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp::ifp_request_base ifp::ifp_response_base))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp::ifp_request_dvma ifp::ifp_response_dvma))

_NOTE(SCHEME_PROTECTS_DATA("HW Registers", ifp::ifp_biu_reg))

_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_request_mutex, ifp::ifp_slots))

_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_waitq_mutex, ifp::ifp_waitf ifp::ifp_waitb))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_request_mutex, ifp::ifp_queue_space))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_request_mutex, ifp::ifp_request_in))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_request_mutex, ifp::ifp_request_out))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_request_mutex, ifp::ifp_request_ptr))

_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp::ifp_response_in))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp::ifp_response_out))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp::ifp_response_ptr))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp::ifp_state))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp::ifp_my_alpa
	ifp::ifp_lip_cnt))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_hp_daemon_mutex, ifp::ifp_hp_elem_head
	ifp::ifp_hp_elem_tail))

_NOTE(SCHEME_PROTECTS_DATA("Only init and free change", ifp::ifp_targets))
_NOTE(MUTEX_PROTECTS_DATA(ifp_target::ifpt_mutex, ifp_target::ifpt_lun))
_NOTE(MUTEX_PROTECTS_DATA(ifp_target::ifpt_mutex, ifp_lun::{ifpl_dip
	ifpl_lun_num ifpl_next ifpl_device_type ifpl_state ifpl_target}))
_NOTE(MUTEX_PROTECTS_DATA(ifp_target::ifpt_mutex,
	ifp_target::ifpt_lun.{ifpl_dip ifpl_lun_num ifpl_next
	ifpl_device_type ifpl_state ifpl_target}))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp_lun::{ifpl_dip ifpl_lun_num
	ifpl_next ifpl_device_type ifpl_state ifpl_target}))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp_target::ifpt_lun.{ifpl_dip
	ifpl_lun_num ifpl_next ifpl_device_type ifpl_state ifpl_target}))

_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp::ifp_state ifp::ifp_lip_state
	ifp::ifp_targets))
_NOTE(SCHEME_PROTECTS_DATA("Mutex ordering", ifp_target))

_NOTE(SCHEME_PROTECTS_DATA("Target Mode Code", ifp::ifp_serial_num))
_NOTE(SCHEME_PROTECTS_DATA("Target Mode Code", ifp::ifp_tm_dma_attr))
_NOTE(SCHEME_PROTECTS_DATA("Target Mode Code", ifp::ifp_tm_hard_loop_id))

_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp_diag_cmd::ifp_cmds_done))

_NOTE(SCHEME_PROTECTS_DATA("SCSI API value", scsi_device::sd_address))
_NOTE(SCHEME_PROTECTS_DATA("SCSI API value", scsi_device::sd_inq))
_NOTE(SCHEME_PROTECTS_DATA("SCSI API value", scsi_device::sd_dev))
_NOTE(SCHEME_PROTECTS_DATA("SCSI API value", scsi_device::sd_reserved))

/*
 * Convenient short-hand macros for reading/writing IFP registers
 */
#define	IFP_WRITE_BIU_REG(ifp, regp, value)				\
	ddi_put16((ifp)->ifp_biu_acc_handle, (regp), (value))

#define	IFP_READ_BIU_REG(ifp, regp)					\
	ddi_get16((ifp)->ifp_biu_acc_handle, (regp))

#define	IFP_WRITE_MBOX_REG IFP_WRITE_BIU_REG

#define	IFP_READ_MBOX_REG IFP_READ_BIU_REG

#define	IFP_WRITE_RISC_REG IFP_WRITE_BIU_REG

#define	IFP_READ_RISC_REG IFP_READ_BIU_REG

/*
 * Convenient short-hand macros for setting/clearing register bits
 */
#define	IFP_SET_BIU_REG_BITS(ifp, regp, value)				\
	IFP_WRITE_BIU_REG((ifp), (regp),				\
		(IFP_READ_BIU_REG((ifp), (regp)) | (value)))

#define	IFP_CLR_BIU_REG_BITS(ifp, regp, value)				\
	IFP_WRITE_BIU_REG((ifp), (regp),				\
		(IFP_READ_BIU_REG((ifp), (regp)) & ~(value)))

/*
 * Hardware  access definitions for IFP chip
 *
 */
#ifdef IFP_PERF
#define	IFP_WRITE_RISC_HCCR(ifp, value)					\
	IFP_WRITE_BIU_REG((ifp),					\
		&((ifp)->ifp_biu_reg->ifp_risc_reg.ifp_hccr), (value));	\
	(ifp)->ifp_wpio_count++

#define	IFP_READ_RISC_HCCR(ifp)						\
	((ifp)->ifp_rpio_count++,					\
	IFP_READ_BIU_REG((ifp),						\
		&(ifp)->ifp_biu_reg->ifp_risc_reg.ifp_hccr))

#define	IFP_REG_GET_RISC_INT(ifp)					\
	((ifp)->ifp_rpio_count++,					\
	(IFP_READ_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_bus_isr) &	\
		IFP_BUS_ISR_RISC_INT))

#define	IFP_CLEAR_SEMAPHORE_LOCK(ifp)					\
	IFP_CLR_BIU_REG_BITS((ifp), &(ifp)->ifp_biu_reg->ifp_bus_sema,	\
		IFP_BUS_SEMA_LOCK);					\
	(ifp)->ifp_wpio_count++, (ifp)->ifp_rpio_count++

#define	IFP_SET_REQUEST_IN(ifp)						\
	IFP_WRITE_BIU_REG((ifp), &((ifp)->ifp_biu_reg->ifp_mailbox4,	\
		(ifp)->ifp_request_in);					\
	(ifp)->ifp_wpio_count++, (ifp)->ifp_request_count++

#define	IFP_SET_RESPONSE_OUT(ifp)					\
	IFP_WRITE_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_mailbox5,	\
		ifp->ifp_response_out);					\
	(ifp)->ifp_wpio_count++

#define	IFP_GET_REQUEST_OUT(ifp)					\
	((ifp)->ifp_rpio_count++,					\
	IFP_READ_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_mailbox4))

#define	IFP_GET_RESPONSE_IN(ifp)					\
	((ifp)->ifp_rpio_count++,					\
	IFP_READ_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_mailbox5))

#define	IFP_INT_PENDING(ifp)						\
	((ifp)->ifp_rpio_count++,					\
	(IFP_READ_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_bus_isr) &	\
		IFP_BUS_ISR_RISC_INT))

#define	IFP_CHECK_SEMAPHORE_LOCK(ifp)					\
	((ifp)->ifp_rpio_count++,					\
	(IFP_READ_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_bus_sema) &	\
		IFP_BUS_SEMA_LOCK))

#else	/* IFP_PERF */
#define	IFP_WRITE_RISC_HCCR(ifp, value)					\
	IFP_WRITE_BIU_REG((ifp),					\
		&((ifp)->ifp_biu_reg->ifp_risc_reg.ifp_hccr), (value));

#define	IFP_READ_RISC_HCCR(ifp)						\
	IFP_READ_BIU_REG((ifp),						\
		&(ifp)->ifp_biu_reg->ifp_risc_reg.ifp_hccr)

#define	IFP_REG_GET_RISC_INT(ifp)					\
	(IFP_READ_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_bus_isr) &	\
		IFP_BUS_ISR_RISC_INT)

#define	IFP_CLEAR_SEMAPHORE_LOCK(ifp)					\
	IFP_CLR_BIU_REG_BITS((ifp), &(ifp)->ifp_biu_reg->ifp_bus_sema,	\
		IFP_BUS_SEMA_LOCK)

#define	IFP_SET_REQUEST_IN(ifp)						\
	IFP_WRITE_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_mailbox4,	\
		(ifp)->ifp_request_in)

#define	IFP_SET_RESPONSE_OUT(ifp)					\
	IFP_WRITE_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_mailbox5,	\
		ifp->ifp_response_out)

#define	IFP_GET_REQUEST_OUT(ifp)					\
	IFP_READ_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_mailbox4)

#define	IFP_GET_RESPONSE_IN(ifp)					\
	IFP_READ_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_mailbox5)

#define	IFP_INT_PENDING(ifp)						\
	(IFP_READ_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_bus_isr) &	\
		IFP_BUS_ISR_RISC_INT)

#define	IFP_CHECK_SEMAPHORE_LOCK(ifp)					\
	(IFP_READ_BIU_REG((ifp), &(ifp)->ifp_biu_reg->ifp_bus_sema) &	\
		IFP_BUS_SEMA_LOCK)

#endif /* IFP_PERF */

#define	IFP_REG_CLEAR_HOST_INT(ifp)					\
	IFP_WRITE_RISC_HCCR((ifp), IFP_HCCR_CMD_CLEAR_HOST_INT);	\

#define	IFP_REG_SET_HOST_INT(ifp)					\
	IFP_WRITE_RISC_HCCR((ifp), IFP_HCCR_CMD_SET_HOST_INT);		\

#define	IFP_REG_GET_HOST_INT(ifp)					\
	(IFP_READ_RISC_HCCR((ifp)) & IFP_HCCR_HOST_INT)

#define	IFP_CLEAR_RISC_INT(ifp)						\
	IFP_WRITE_RISC_HCCR((ifp), IFP_HCCR_CMD_CLEAR_RISC_INT);

/*
 * short hand-macros to copy entries in/out of queues (IOPB area).
 */
#define	IFP_COPY_OUT_DMA_8(hndl, source, dest, count)			\
	ddi_rep_put8((hndl), (uchar_t *)(source),			\
		(uchar_t *)(dest), (count), DDI_DEV_AUTOINCR)
#define	IFP_COPY_OUT_DMA_16(hndl, source, dest, count)			\
	ddi_rep_put16((hndl), (ushort_t *)(source),			\
		(ushort_t *)(dest), (count), DDI_DEV_AUTOINCR)
#define	IFP_COPY_OUT_DMA_32(hndl, source, dest, count)			\
	ddi_rep_put32((hndl), (uint_t *)(source),			\
		(uint_t *)(dest), (count), DDI_DEV_AUTOINCR)

#define	IFP_COPY_IN_DMA_8(hndl, source, dest, count)			\
	ddi_rep_get8((hndl), (uchar_t *)(dest),				\
		(uchar_t *)(source), (count), DDI_DEV_AUTOINCR)
#define	IFP_COPY_IN_DMA_16(hndl, source, dest, count)			\
	ddi_rep_get16((hndl), (ushort_t *)(dest),			\
		(ushort_t *)(source), (count), DDI_DEV_AUTOINCR)
#define	IFP_COPY_IN_DMA_32(hndl, source, dest, count)			\
	ddi_rep_get32((hndl), (uint_t *)(dest),				\
		(uint_t *)(source), (count), DDI_DEV_AUTOINCR)

#define	IFP_COPY_OUT_REQ(hndl, source, dest)				\
	{								\
		uchar_t *s, *d;						\
		s = (uchar_t *)(source);				\
		d = (uchar_t *)(dest);					\
		IFP_COPY_OUT_DMA_16((hndl), s, d, 2);			\
		IFP_COPY_OUT_DMA_32((hndl), (s + IFP_REQ_TOKEN_OFF),	\
			(d + IFP_REQ_TOKEN_OFF), 1);			\
		IFP_COPY_OUT_DMA_16((hndl), (s + IFP_REQ_TARG_OFF),	\
			(d + IFP_REQ_TARG_OFF), 6);			\
		IFP_COPY_OUT_DMA_8((hndl), (s + IFP_REQ_CDB_OFF),	\
			(d + IFP_REQ_CDB_OFF), 12);			\
		IFP_COPY_OUT_DMA_32((hndl), (s + IFP_REQ_BYTE_CNT_OFF), \
			(d + IFP_REQ_BYTE_CNT_OFF), 3);			\
	}

#define	IFP_COPY_IN_RESP_HDR(hndl, source, dest)			\
	IFP_COPY_IN_DMA_16((hndl), (uchar_t *)(source),			\
		(uchar_t *)(dest), 2);

#define	IFP_COPY_IN_TOKEN(hndl, source, dest)				\
	IFP_COPY_IN_DMA_32((hndl), ((uchar_t *)(source) +		\
		IFP_RESP_TOKEN_OFF), (dest), 1);

#define	IFP_COPY_IN_RESP(hndl, source, dest)				\
	{								\
		uchar_t *s, *d;						\
		s = (uchar_t *)(source);				\
		d = (uchar_t *)(dest);					\
		IFP_COPY_IN_DMA_32((hndl), (s + IFP_RESP_TOKEN_OFF),	\
			(d + IFP_RESP_TOKEN_OFF), 1);			\
		IFP_COPY_IN_DMA_16((hndl), (s + IFP_RESP_STATUS_OFF),	\
			(d + IFP_RESP_STATUS_OFF), 6);			\
		IFP_COPY_IN_DMA_32((hndl), (s + IFP_RESP_RESID_OFF),	\
			(d + IFP_RESP_RESID_OFF), 1);			\
		if (((struct ifp_response *)(dest))->resp_scb != 0) {	\
			IFP_COPY_IN_DMA_8((hndl),			\
				(s + IFP_RESP_FCP_RESP_OFF),		\
				(d + IFP_RESP_FCP_RESP_OFF), 8);	\
			IFP_COPY_IN_DMA_8((hndl),			\
				(s + IFP_RESP_RQS_OFF),			\
				(d + IFP_RESP_RQS_OFF), 32);		\
		}							\
	}

/*
 * This describes the returned data from the SCSI REPORT LUNS command
 */
typedef struct rpt_luns_single {
	uchar_t	rptl_bus_id;
	uchar_t	rptl_lun1;
	uchar_t	rptl_lun2_msb;
	uchar_t	rptl_lun2_lsb;
	uchar_t	rptl_lun3_msb;
	uchar_t	rptl_lun3_lsb;
	uchar_t	rptl_lun4_msb;
	uchar_t	rptl_lun4_lsb;
} rpt_luns_single_t;

typedef struct rpt_luns_data {
	uchar_t	rpt_len_msb;
	uchar_t	rpt_len_2sb;
	uchar_t	rpt_len_1sb;
	uchar_t	rpt_len_lsb;
	uchar_t	rpt_resvd[4];
	rpt_luns_single_t rpt_luns[IFP_MAX_LUNS];
} rpt_luns_data_t;


/*
 * This defined a trace facility that can be used for
 * debugging.
 */

#if defined(IFPTRACE)

#ifndef NIFPTRACE
#define	NIFPTRACE 1024
#endif

struct ifptrace {
	int count;
	int function;		/* address of function */
	int trace_action;	/* descriptive 4 characters */
	int object;		/* object operated on */
};

extern struct ifptrace ifptrace_buffer[];
extern struct ifptrace *ifptrace_ptr;
extern int ifptrace_count;

#define	AT_ACT(__a, __b, __c, __d) \
	(int) \
	    (((int)(__d) << 24) | \
	    ((int)(__c) << 16) | \
	    ((int)(__b) << 8) | \
	    (int)(__a))

#define	ATRACEINIT() {				\
	if (ifptrace_ptr == NULL)		\
		ifptrace_ptr = ifptrace_buffer; \
	}

#define	LOCK_TRACE()	mutex_enter(&ifp_atrace_mutex)
#define	UNLOCK_TRACE()	mutex_exit(&ifp_atrace_mutex)

#define	TR_REVERSE /* I think that it is easier to read the */
/*  if it is recorded in reverse order.   That way hitting */
/* return in adb walks the trace from newest to oldest. */

#ifdef TR_REVERSE
#define	ATRACE(func, act, obj) {		\
	int *_p;				\
	LOCK_TRACE();				\
	_p = &ifptrace_ptr->count;		\
	if ((struct ifptrace *)(void *)_p == &ifptrace_buffer[0])\
		_p = (int *)&ifptrace_buffer[NIFPTRACE]; \
	_p--;					\
	*_p = (int)(uintptr_t)(obj);		\
	_p--;					\
	*_p = (int)(act);			\
	_p--;					\
	*_p = (int)(uintptr_t)(func);		\
	_p--;					\
	*_p = ++ifptrace_count;			\
	ifptrace_ptr = (struct ifptrace *)_p;	\
	UNLOCK_TRACE();				\
	}
#else
#define	ATRACE(func, act, obj) {		\
	int *_p;				\
	LOCK_TRACE();				\
	_p = &ifptrace_ptr->count;		\
	*_p++ = ++ifptrace_count;		\
	*_p++ = (int)(uintptr_t)(func);		\
	*_p++ = (int)(act);			\
	*_p++ = (int)(uintptr_t)(obj);		\
	if ((struct ifptrace *)(void *)_p >= &ifptrace_buffer[NIFPTRACE])\
		ifptrace_ptr = ifptrace_buffer; \
	else					\
		ifptrace_ptr = (struct ifptrace *)(void *)_p; \
	UNLOCK_TRACE();			\
	}
#endif
#else	/* !IFPTRACE */

/* If no tracing, define no-ops */
#define	ATRACEINIT()
#define	ATRACE(a, b, c)

#endif	/* !IFPTRACE */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_IFPVAR_H */

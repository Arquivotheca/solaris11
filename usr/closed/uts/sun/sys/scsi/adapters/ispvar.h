/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_SCSI_ADAPTERS_ISPVAR_H
#define	_SYS_SCSI_ADAPTERS_ISPVAR_H


#include <sys/note.h>
#include <sys/kstat.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	ISPDEBUG_IOCTL
/*
 * for allowing ioctls for debugging
 */
#define	ISP_RELOAD_FIRMWARE		(('i' << 8) | 0x01)
#define	ISP_PRINT_DEVQ_STATS		(('i' << 8) | 0x02)
#define	ISP_RESET_TARGET		(('i' << 8) | 0x03)
#define	ISP_PRINT_SLOTS			(('i' << 8) | 0x05)
#define	ISP_DROP_COMMAND		(('i' << 8) | 0x06)
#endif	/* ISPDEBUG_IOCTL */

/*
 * Convenient short hand defines
 */
#define	TRUE			 1
#define	FALSE			 0
#define	UNDEFINED		-1

#define	CNUM(isp)		(ddi_get_instance(isp->isp_tran.tran_dev))

#define	ISP_RETRY_DELAY		5
#define	ISP_RETRIES		0	/* retry of selections */
#define	ISP_INITIAL_SOFT_SPACE	5	/* Used for the softstate_init func */

#define	MSW(x)			(uint16_t)(((uint32_t)(x) >> 16) & 0xFFFF)
#define	LSW(x)			(uint16_t)((uint32_t)(x) & 0xFFFF)

#define	MSB(x)			(char)(((uint16_t)(x) >> 8) & 0xFF)
#define	LSB(x)			(char)((uint16_t)(x) & 0xFF)

#define	TGT(sp)			(CMD2PKT(sp)->pkt_address.a_target)
#define	LUN(sp)			(CMD2PKT(sp)->pkt_address.a_lun)

#define	TGT_N_LUN(t, l)		((((t) & 0xff) << 8) | ((l) & 0xff))

#define	ISP_SBUS		0
#define	ISP_PCI			1

#define	ISP_DEVICEID_1040	0x1020

/*
 * Use for Qfull Capability
 */
#define	ISP_GET_QFULL_CAP	1
#define	ISP_SET_QFULL_CAP	0

/*
 * Defines for response reasons returned by ISP firmware
 */
#define	DATA_UNDER_RUN	21
#define	TAG_REJECT	28

/*
 * Interrupt actions returned by isp_i_flag_event()
 */
#define	ACTION_CONTINUE		0	/* Continue */
#define	ACTION_RETURN		1	/* Exit */
#define	ACTION_IGNORE		2	/* Ignore */

/*
 * Reset actions for isp_i_reset_interface()
 */
#define	ISP_FORCE_RESET_BUS	0x01	/* reset scsi bus on error reco */
#define	ISP_DOWNLOAD_FW_ON_ERR	0x02	/* Download the firmware after reset */
#define	ISP_FIRMWARE_ERROR	0x04	/* Sys_Err Async Event happened */
#define	ISP_SKIP_STOP_QUEUES	0x08	/* don't reset bus *or* stop queues */
#define	ISP_FROM_INTR		0x10	/* error from interrupt context */

/*
 * extracting period and offset from isp_synch
 */
#define	PERIOD_MASK(val)	((val) & 0xff)
#define	OFFSET_MASK(val)	(((val) >> 8) & 0xff)

/*
 * timeout values
 */
#define	ISP_GRACE		10	/* Timeout margin (sec.) */
#define	ISP_TIMEOUT_DELAY(secs, delay)	(secs * (1000000 / delay))

/*
 * delay time for polling loops
 */
#define	ISP_NOINTR_POLL_DELAY_TIME		1000	/* usecs */

/*
 * busy wait delay time after chip reset
 */
#define	ISP_CHIP_RESET_BUSY_WAIT_TIME		100	/* usecs */

/*
 * timeout for ISP coming out of reset
 */
#define	ISP_RESET_WAIT				1000	/* ms */
#define	ISP_SOFT_RESET_TIME			1	/* second */

/*
 * Default secs for watchdog thread to be invoked. Should be greater
 * than sd/st_io_time(pkt_time).
 */
#define	ISP_DEFLT_WATCHDOG_SECS			60

/*
 * isp_softstate flags for introducing hot plug
 */
#define	ISP_SS_DRAINING		0x02
#define	ISP_SS_QUIESCED		0x04
#define	ISP_SS_DRAIN_ERROR	0x08
#define	ISP_SS_HOTPLUG_MASK	0x0f	/* mask for above flags */
#define	ISP_HOTPLUG_IN_PROG(i)	((i)->isp_softstate & ISP_SS_HOTPLUG_MASK)

/*
 * Time to drain the bus while quiesceing.
 */
#define	ISP_BUS_DRAIN_TIME	3		/* seconds */
/*
 * Debugging macros
 */
#ifdef ISPDEBUG
/*
 *	debug level 1 => bootup, initialization routines
 *	debug level 2 => verbose bootup or debug error printout messages
 *			error means "exceptions conditions" that rarely occur
 *	debug level 3 => isp routine call entry
 *	debug level 4 => verbose debug printout
 *	debug levels are inclusive of lower level's messages
 */
#define	ISP_DEBUG	if (ispdebug) isp_i_log
#define	ISP_DEBUG2	if (ispdebug > 1) isp_i_log
#define	ISP_DEBUG3	if (ispdebug > 2) isp_i_log
#define	ISP_DEBUG4	if (ispdebug > 3) isp_i_log
#else	/* ISPDEBUG */
#define	ispdebug	(0)
#define	INFORMATIVE	(0)
#define	DEBUGGING	(0)
#define	DEBUGGING_ALL	(0)

#define	ISP_DEBUG	if (0) isp_i_log
#define	ISP_DEBUG2	if (0) isp_i_log
#define	ISP_DEBUG3	if (0) isp_i_log
#define	ISP_DEBUG4	if (0) isp_i_log
#endif /* ISPDEBUG */

/*
 * Size definitions for request and response queues.
 *
 * ISP_QUEUE_SIZE is the size of the area set aside for the request *and*
 * response queues
 *
 * ISP_QUEUE_SIZE must be large enough to store the firmware image,
 * since the queue space is used for this when reloading the firmware
 *
 * ISP_MAX_RESPONSES has a max of 256 on PCI and 1024 on SBus.
 * ISP_MAX_REQUESTS can go as high as 1024 on SBUS and PCI, but testing
 * has shown that these larger sizes didn't improve performance, so
 * 256 continues to be used.
 */
#define	ISP_MAX_REQUESTS	190		/* request queue size */
#define	ISP_MAX_RESPONSES	190		/* response queue size */

/*
 * the max the the request and response queues can be
 */
#define	ISP_MAX_QUEUE_SIZE	256

/*
 * size of memory set aside for both request and response queues
 */
#define	ISP_QUEUE_SIZE(req_size, resp_size) \
	((size_t)(((req_size) * sizeof (struct isp_request)) + \
	    ((resp_size) * sizeof (struct isp_response))))

/*
 * Kernel interrupt statistics support
 */
#define	ISP_KSTAT_INTR_PTR(ss)	KSTAT_INTR_PTR((ss)->isp_kstat)

/*
 * ISP request packet as defined by the Firmware Interface
 */
struct isp_dataseg {
	int32_t	d_base;
	int32_t	d_count;
};

/*
 * this structure is defined by the firmware and is embedded at the start
 * of the request and response structures
 */
struct cq_header {
#if defined(_BIG_ENDIAN)
	uint8_t	cq_entry_count;
	uint8_t	cq_entry_type;
	uint8_t	cq_flags;
	uint8_t	cq_seqno;
#else
	uint8_t	cq_entry_type;
	uint8_t	cq_entry_count;
	uint8_t	cq_seqno;
	uint8_t	cq_flags;
#endif
};

/* create a struct to handle raw PCI, little endian header */
struct cq_header_pci {
	uint8_t	cq_entry_type;
	uint8_t	cq_entry_count;
	uint8_t	cq_seqno;
	uint8_t	cq_flags;
};

union isp_token {
	int32_t token;
	struct {
		uint16_t slot;
		uint16_t seq;
	} slot_n_seq;
};

#define	ISP_CHECK_TOKEN(tok) ((uint8_t)( \
	((tok).token >> 24) ^ ((tok).token >> 16) ^ \
	((tok).token >> 8) ^ (tok).token))

/*
 * data structure we pass to firmware
 */
struct isp_request {
	struct cq_header	req_header;
	uint32_t		req_token;

	struct {
#if defined(_BIG_ENDIAN)
		uint8_t		req_target;
		uint8_t		req_lun_trn;
#else
		uint8_t		req_lun_trn;
		uint8_t		req_target;
#endif
	} req_scsi_id;

	uint16_t		req_cdblen;
#define	req_modifier		req_cdblen	/* marker packet */
	uint16_t		req_flags;
	uint16_t		req_reserved;
	uint16_t		req_time;
	uint16_t		req_seg_count;

	uint32_t		req_cdb[3];
	struct isp_dataseg	req_dataseg[4];
};

#define	ISP_REQ_TOKEN_OFF	offsetof(struct isp_request, req_token)
#define	ISP_REQ_LUN_OFF		offsetof(struct isp_request, req_scsi_id)
#define	ISP_REQ_CDB_OFF		offsetof(struct isp_request, req_cdb)
#define	ISP_REQ_DATA_OFF	offsetof(struct isp_request, req_dataseg)

/*
 * Header flags definitions
 */
#define	CQ_FLAG_CONTINUATION	0x01
#define	CQ_FLAG_FULL		0x02
#define	CQ_FLAG_BADHEADER	0x04
#define	CQ_FLAG_BADPACKET	0x08
#define	CQ_FLAG_ERR_MASK	\
	(CQ_FLAG_FULL | CQ_FLAG_BADHEADER | CQ_FLAG_BADPACKET)

/*
 * Header entry_type definitions
 */
#define	CQ_TYPE_REQUEST		1
#define	CQ_TYPE_DATASEG		2
#define	CQ_TYPE_RESPONSE	3
#define	CQ_TYPE_MARKER		4
#define	CQ_TYPE_CMDONLY		5

/*
 * Copy cdb into request using int32_t word transfers to save time.
 */
#define	ISP_CDBMAX	12
#define	ISP_LOAD_REQUEST_CDB(req, sp, cdbsize) { \
	register int32_t *cdbp, *sp_cdbp; \
	(req)->req_cdblen = (int16_t)(cdbsize); \
	cdbp = (int32_t *)(req)->req_cdb; \
	sp_cdbp = (int32_t *)CMD2PKT(sp)->pkt_cdbp; \
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
#define	ISP_REQ_FLAG_NODISCON		0x0001
#define	ISP_REQ_FLAG_HEAD_TAG		0x0002
#define	ISP_REQ_FLAG_ORDERED_TAG	0x0004
#define	ISP_REQ_FLAG_SIMPLE_TAG		0x0008
#define	ISP_REQ_FLAG_USE_TRN		0x0010
#define	ISP_REQ_FLAG_DATA_READ		0x0020
#define	ISP_REQ_FLAG_DATA_WRITE		0x0040
#define	ISP_REQ_FLAG_DISARQ		0x0100
#define	ISP_REQ_FLAG_FORCE_ASYNCH	0x0200
#define	ISP_REQ_FLAG_FORCE_SYNCH	0x0400
#define	ISP_REQ_FLAG_WIDE		0x0800
#define	ISP_REQ_FLAG_DISABLE_PARITY	0x1000
#define	ISP_REQ_FLAG_STOP_QUEUE		0x2000
#define	ISP_REQ_FLAG_PRIORITY_CMD	0x8000


/*
 * translate scsi_pkt flags into ISP request packet flags
 * It would be illegal if two flags are set; the driver does not
 * check for this. Setting NODISCON and a tag flag is harmless.
 */
#define	ISP_SET_PKT_FLAGS(scsa_flags, isp_flags) {		\
	isp_flags = (scsa_flags >> 11) & 0xe; /* tags */ \
	isp_flags |= (scsa_flags >> 1) & 0x1; /* no disconnect */ \
}

/*
 * isp_request size minus header.
 */
#define	ISP_PAYLOAD		\
	(sizeof (struct isp_request) - sizeof (struct cq_header))

/*
 * throttle values for ISP request queue
 */
#define	SHUTDOWN_THROTTLE	-1	/* do not submit any requests */
#define	CLEAR_THROTTLE		(ISP_MAX_REQUESTS -1)

/*
 * XXX: Note, this request queue macro *ASSUMES* that queue full cannot
 *	occur.
 */
#define	ISP_GET_NEXT_REQUEST_IN(isp, ptr) { \
	(ptr) = (isp)->isp_request_ptr; \
	if ((isp)->isp_request_in == (isp_max_requests - 1)) {	 \
		(isp)->isp_request_in = 0; \
		(isp)->isp_request_ptr = (isp)->isp_request_base; \
	} else { \
		(isp)->isp_request_in++; \
		(isp)->isp_request_ptr++; \
	} \
}

/*
 * slots queue for isp timeout handling
 * Must be a multiple of 8
 */
#define	ISP_DISK_QUEUE_DEPTH	100
#define	ISP_MAX_SLOTS		((NTARGETS_WIDE * ISP_DISK_QUEUE_DEPTH) + \
				ISP_MAX_REQUESTS)

/*
 * ISP response packet as defined by the Firmware Interface
 */
struct isp_response {
	struct cq_header	resp_header;
	uint32_t		resp_token;

	uint16_t		resp_scb;
	uint16_t		resp_reason;
	uint16_t		resp_state;
	uint16_t		resp_status_flags;
	uint16_t		resp_time;
	uint16_t		resp_rqs_count;

	uint32_t		resp_resid;
	uint32_t		resp_reserved[2];
	uint32_t		resp_request_sense[8];
};

#define	ISP_RESP_TOKEN_OFF	offsetof(struct isp_response, resp_token)
#define	ISP_RESP_STATUS_OFF	offsetof(struct isp_response, resp_scb)
#define	ISP_RESP_RESID_OFF	offsetof(struct isp_response, resp_resid)
#define	ISP_RESP_RQS_OFF	offsetof(struct isp_response, \
							resp_request_sense)
/*
 * Decrement the "sequence" number and free the slot.  The sequence number
 * decreases to reduce the time it takes to wrap for testing purposes so
 * that the values where the potential for sign extension are seen sooner
 * in the test.
 */
#define	ISP_FREE_SLOT(isp, slot) { \
	ASSERT((isp)->isp_slots[(slot)].isp_cmd != NULL); \
	(isp)->isp_slots[(slot)].seq--; \
	(isp)->isp_slots[(slot)].isp_cmd = NULL; \
	isp_i_delete_from_slot_list(isp, &isp->busy_slots, slot); \
	isp_i_add_to_free_slots(isp, slot); \
}

#define	ISP_GET_NEXT_RESPONSE_OUT(isp, ptr) { \
	(ptr) = (isp)->isp_response_ptr; \
	if ((isp)->isp_response_out == (isp_max_responses - 1)) { \
		(isp)->isp_response_out = 0; \
		(isp)->isp_response_ptr = (isp)->isp_response_base; \
	} else { \
		(isp)->isp_response_out++; \
		(isp)->isp_response_ptr++; \
	} \
}

#define	ISP_IS_RESPONSE_INVALID(resp) \
	((resp)->resp_header.cq_entry_type != CQ_TYPE_RESPONSE)


#define	ISP_GET_PKT_STATE(state)	((uint32_t)(state >> 8))
#define	ISP_GET_PKT_STATS(stats)	((uint32_t)(stats))

#define	ISP_STAT_NEGOTIATE	0x0080
#define	ISP_STAT_SYNC		0x0002

#define	ISP_SET_REASON(sp, reason) { \
	if ((sp) && CMD2PKT(sp)->pkt_reason == CMD_CMPLT) \
		CMD2PKT(sp)->pkt_reason = (reason); \
}

/*
 * mutex and semaphore short hands
 */
#define	ISP_MBOX_SEMA(isp)	(&isp->isp_mbox.mbox_sema)

#define	ISP_REQ_MUTEX(isp)	(&isp->isp_request_mutex)
#define	ISP_RESP_MUTEX(isp)	(&isp->isp_response_mutex)
#define	ISP_WAITQ_MUTEX(isp)	(&isp->isp_waitq_mutex)
#define	ISP_HOTPLUG_MUTEX(isp)	(&isp->isp_hotplug_mutex)
#define	ISP_INTR_MUTEX(isp)	(&(isp)->isp_intr_mutex)
#define	ISP_INTR_CV(isp)	(&(isp)->isp_intr_cv)

#define	ISP_MUTEX_ENTER(isp)	mutex_enter(ISP_RESP_MUTEX(isp)),	\
				mutex_enter(ISP_REQ_MUTEX(isp))
#define	ISP_MUTEX_EXIT(isp)	mutex_exit(ISP_RESP_MUTEX(isp)),	\
				mutex_exit(ISP_REQ_MUTEX(isp))

#define	ISP_CV(isp)		(&(isp)->isp_cv)

#ifdef __lock_lint
#define	ISP_INC32_ERRSTATS(isp_ptr, xx)
#define	ISP_SET64_ERRSTATS(isp_ptr, xx, val)
#else
#define	ISP_INC32_ERRSTATS(isp_ptr, xx)					\
	if ((isp_ptr != NULL) && (isp_ptr->isp_err_kstats != NULL)) {	\
		struct isp_errstats *iep;				\
		iep = (struct isp_errstats *)isp->isp_err_kstats->ks_data;\
		iep->xx.value.ui32++;					\
	}

#define	ISP_SET64_ERRSTATS(isp_ptr, xx, val)				\
	if ((isp_ptr != NULL) && (isp_ptr->isp_err_kstats != NULL)) {	\
		struct isp_errstats *iep;				\
		iep = (struct isp_errstats *)isp->isp_err_kstats->ks_data;\
		iep->xx.value.ull = val;				\
	}
#endif

/*
 * HBA interface macros
 */
#define	SDEV2TRAN(sd)		((sd)->sd_address.a_hba_tran)
#define	SDEV2ADDR(sd)		(&((sd)->sd_address))
#define	PKT2TRAN(pkt)		((pkt)->pkt_address.a_hba_tran)
#define	ADDR2TRAN(ap)		((ap)->a_hba_tran)

#define	TRAN2ISP(tran)		((struct isp *)(tran)->tran_hba_private)
#define	SDEV2ISP(sd)		(TRAN2ISP(SDEV2TRAN(sd)))
#define	PKT2ISP(pkt)		(TRAN2ISP(PKT2TRAN(pkt)))
#define	ADDR2ISP(ap)		(TRAN2ISP(ADDR2TRAN(ap)))

#define	CMD2ADDR(cmd)		(&CMD2PKT(cmd)->pkt_address)
#define	CMD2TRAN(cmd)		(CMD2PKT(cmd)->pkt_address.a_hba_tran)
#define	CMD2ISP(cmd)		(TRAN2ISP(CMD2TRAN(cmd)))


/*
 * max number of LUNs per target
 */
#define	ISP_NLUNS_PER_TARGET	32

/*
 * the number of LUNs we can address on any bus
 */
#define	ISP_MI_SIZE		(ISP_NLUNS_PER_TARGET * NTARGETS_WIDE)

/*
 * linked list for keeping track of marker(s) that need to be sent to ISP
 * chip when they can't be set because there's no room in the
 * reqeust queue
 */
struct isp_marker_info {
	short			isp_marker_mode;
	uchar_t			isp_marker_tgt;
	uchar_t			isp_marker_lun;
};

struct isp_errstats {
	kstat_named_t	isperr_waitq_cmds;
	kstat_named_t	isperr_total_cmds;	/* total cmds started */
	kstat_named_t	isperr_completed_cmds;
	kstat_named_t	isperr_mailbox_cmds;
	kstat_named_t	isperr_polled_cmds;
	kstat_named_t	isperr_marker_cmds;
	kstat_named_t	isperr_mailbox_started;

	kstat_named_t	isperr_cmd_incomplete;
	kstat_named_t	isperr_lbolt_incomplete;
	kstat_named_t	isperr_cmd_dma_err;
	kstat_named_t	isperr_lbolt_dma_err;
	kstat_named_t	isperr_cmd_tran_err;
	kstat_named_t	isperr_lbolt_tran_err;
	kstat_named_t	isperr_cmd_reset;
	kstat_named_t	isperr_lbolt_reset;
	kstat_named_t	isperr_cmd_abort;
	kstat_named_t	isperr_lbolt_abort;
	kstat_named_t	isperr_cmd_terminated;
	kstat_named_t	isperr_lbolt_terminated;
	kstat_named_t	isperr_cmd_timeout;
	kstat_named_t	isperr_lbolt_timeout;
	kstat_named_t	isperr_cmd_data_error;
	kstat_named_t	isperr_lbolt_data_error;
	kstat_named_t	isperr_cmd_overrun;
	kstat_named_t	isperr_lbolt_overrun;
	kstat_named_t	isperr_cmd_misc_error; 	/* any other error from isp */
	kstat_named_t	isperr_lbolt_misc_error;
	kstat_named_t	isperr_response_error;

	kstat_named_t	isperr_ars_pkts;
	kstat_named_t	isperr_set_capability;
	kstat_named_t	isperr_get_capability;
	kstat_named_t	isperr_fatal_errors;
	kstat_named_t	isperr_reset_chip;
	kstat_named_t	isperr_firmware_loads;
	kstat_named_t	isperr_suspends;
	kstat_named_t	isperr_resumes;
};

struct isp_slot {
	struct	isp_cmd		*isp_cmd;
	hrtime_t		timeout; /* absolute time to time out */
	uint16_t		seq;
	uint8_t			xor;
	uint16_t		next;
	uint16_t		prev;
};

struct isp_slot_list {
	uint16_t		head;
	uint16_t		tail;
};

/*
 * isp softstate structure
 */
struct isp {

	/*
	 * Transport structure for this instance of the hba
	 */
	scsi_hba_tran_t		*isp_tran;

	/*
	 * dev_info_t reference can be found in the transport structure
	 */
	dev_info_t		*isp_dip;

	/*
	 * Bus the card is connected to. 0 - Sbus, 1 - PCI
	 */
	uchar_t			isp_bus;

	/*
	 * Clock frequency of the chip
	 */
	int			isp_clock_frequency;

	/*
	 * Interrupt block cookie
	 */
	ddi_iblock_cookie_t	isp_iblock;

	/*
	 * linked list of all isp's for isp_intr_loop() and debugging
	 */
	struct isp		*isp_next;

	/*
	 * Firmware revision number and customer/product number
	 */
	ushort_t		isp_maj_min_rev;
	ushort_t		isp_subminor_rev;
	ushort_t		isp_cust_prod;

	/*
	 * scsi options, scsi_tag_age_limit per isp
	 */
	uint16_t		isp_target_scsi_options_defined;
	int			isp_scsi_options;
	int			isp_target_scsi_options[NTARGETS_WIDE];
	int			isp_scsi_tag_age_limit;
	int			isp_arq_pending[NTARGETS_WIDE];

	/*
	 * scsi_reset_delay per isp
	 */
	clock_t			isp_scsi_reset_delay;

	/*
	 * current host ID
	 */
	uchar_t			isp_initiator_id;

	/*
	 * suspended flag for power management
	 */
	uchar_t			isp_suspended;

	/*
	 * Host adapter capabilities and offset/period values per target
	 */
	ushort_t		isp_cap[NTARGETS_WIDE];
	ushort_t		isp_synch[NTARGETS_WIDE];

	/*
	 * ISP Hardware register pointers.
	 */
	struct isp_biu_regs	*isp_biu_reg;
	struct isp_mbox_regs	*isp_mbox_reg;
	struct isp_sxp_regs	*isp_sxp_reg;
	struct isp_risc_regs	*isp_risc_reg;

	uint_t			isp_reg_number;

	/*
	 * mbox values are stored here before and after the mbox cmd
	 * (protected by semaphore inside isp_mbox)
	 */
	struct isp_mbox		isp_mbox;

	/*
	 * flag for updating properties in isp_i_watch()
	 * to avoid updating in interrupt context
	 */
	ushort_t		isp_prop_update;

	/*
	 * request and response queue dvma space
	 */
	caddr_t			isp_cmdarea;
	ddi_dma_cookie_t	isp_dmacookie;
	ddi_dma_handle_t	isp_dmahandle;
	ddi_acc_handle_t	isp_dma_acc_handle;
	uint32_t		isp_request_dvma,
				isp_response_dvma;
	/*
	 * data access handles
	 */
	ddi_acc_handle_t	isp_pci_config_acc_handle;
	ddi_acc_handle_t	isp_biu_acc_handle;
	ddi_acc_handle_t	isp_mbox_acc_handle;
	ddi_acc_handle_t	isp_sxp_acc_handle;
	ddi_acc_handle_t	isp_risc_acc_handle;

	/*
	 * ISP input request and output response queue pointers
	 * and mutexes protecting request and response queue
	 */
	uint_t			isp_queue_space;
	kmutex_t		isp_request_mutex;
	kmutex_t		isp_response_mutex;
	uint16_t		isp_request_in,
				isp_request_out;
	uint16_t		isp_response_in,
				isp_response_out;

	struct isp_request	*isp_request_ptr,
				*isp_request_base;
	struct isp_response	*isp_response_ptr,
				*isp_response_base;
	/*
	 * waitQ (used for storing cmds in case request mutex is held)
	 */
	kmutex_t		isp_waitq_mutex;
	struct	isp_cmd		*isp_waitf;
	struct	isp_cmd		*isp_waitb;
	timeout_id_t		isp_waitq_timeout;

	int			isp_burst_size;
	ushort_t		isp_conf1_fifo;


#ifdef ISP_PERF
	/*
	 * performance counters
	 */
	uint_t			isp_request_count,
				isp_mail_requests;
	uint_t			isp_intr_count,
				isp_perf_ticks;
	uint_t			isp_rpio_count,
				isp_wpio_count;
#endif

	ushort_t		isp_alive;

	/*
	 * list of reset notification requests
	 */
	struct scsi_reset_notify_entry	*isp_reset_notify_listf;
	struct kmem_cache		*isp_kmem_cache;

	ushort_t		isp_backoff;
	uint_t			isp_softstate; /* flags for hotplug */
	int			isp_hotplug_waiting;
	kcondvar_t		isp_cv; /* cv for bus quiesce/unquiesce */
	kmutex_t		isp_hotplug_mutex; /* Mutex for hotplug */
	struct kstat		*isp_kstat;
	kstat_t			*isp_err_kstats;

	/*
	 * isp_slots are used for three things.
	 * 	1) To provide a mapping from the token back to the
	 *	   isp_cmd.
	 * 	2) To allow reset recovery if the chip is reset.
	 * 	3) To allow commands to be timed out.
	 * They are always kept on one of two lists, the free list
	 * and the busy list. The busy list is kept sorted so that
	 * the first candidate for time out is on the tail of the
	 * list.
	 */
	struct	isp_slot	isp_slots[ISP_MAX_SLOTS];
	struct	isp_slot_list busy_slots;
	struct	isp_slot_list free_slots;

	/*
	 * for keeping track of the max LUNs per target on this bus
	 */
	uchar_t			isp_max_lun[NTARGETS_WIDE];

	/*
	 * a circular queue for when we want to send a marker, but there is
	 * no room in the request queue -- big enough for all targets/LUNs
	 * on an adapter (so unlikely it will ever run out of room)
	 */
	ushort_t		isp_marker_in;	/* circ. queue in index */
	ushort_t		isp_marker_out;	/* circ. queue out index */
	ushort_t		isp_marker_free; /* count of entries free */
	struct isp_marker_info	isp_markers[ISP_MI_SIZE];

	/*
	 * for keeping track of when we are in an interrupts (for
	 * reset handling)
	 */
	kmutex_t		isp_intr_mutex;
	kcondvar_t		isp_intr_cv;
	ushort_t		isp_in_intr;

	/*
	 * for throttling back I/O requests
	 */
	ushort_t		isp_in_reset;

	/*
	 * for keeping track of whether or not we think
	 * the OBP is present (since we do extra bus resets
	 * if we have been hot-plugged)
	 */
	ushort_t		isp_no_obp;

	/*
	 * variable for insuring that only one thread is checking the isp
	 * semaphore at a time
	 */
	ushort_t		isp_checking_semlock;

	/*
	 * to keep track of when interrupts are enabled but we
	 * are not yet fully attached and ready to handle I/O (and
	 * markers)
	 */
	ushort_t		isp_attached;

	ushort_t		isp_instance;

	ushort_t		isp_polled_completion;
#ifdef	ISPDEBUG_IOCTL
	uint16_t		drop_command;
#endif
};


_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_tran))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_dip))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_instance))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_maj_min_rev))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_subminor_rev))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_cust_prod))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_initiator_id))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_cmdarea isp::isp_dmahandle))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_dmacookie))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_attached))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_instance))

_NOTE(SCHEME_PROTECTS_DATA("Semaphore", isp::isp_mbox))
_NOTE(SCHEME_PROTECTS_DATA("save sharing", isp::isp_alive))
_NOTE(SCHEME_PROTECTS_DATA("save sharing", isp::isp_prop_update))
_NOTE(SCHEME_PROTECTS_DATA("save sharing", isp::isp_target_scsi_options))
_NOTE(SCHEME_PROTECTS_DATA("save sharing", isp::isp_backoff))

_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::free_slots.head))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::free_slots.tail))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::busy_slots.head))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::busy_slots.tail))

_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_request_base
				isp::isp_response_base))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_request_dvma
				isp::isp_response_dvma))
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_request_dvma
				isp::isp_response_dvma))

_NOTE(SCHEME_PROTECTS_DATA("HW Registers", isp::isp_biu_reg))
_NOTE(SCHEME_PROTECTS_DATA("HW Registers", isp::isp_mbox_reg))
_NOTE(SCHEME_PROTECTS_DATA("HW Registers", isp::isp_sxp_reg))
_NOTE(SCHEME_PROTECTS_DATA("HW Registers", isp::isp_risc_reg))

_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex,
	isp::isp_cap isp::isp_synch))
_NOTE(SCHEME_PROTECTS_DATA("ISR only accesses after fully prepared",
	isp::isp_slots))

_NOTE(MUTEX_PROTECTS_DATA(isp::isp_waitq_mutex,
	isp::isp_waitf isp::isp_waitb))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::isp_queue_space))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::isp_request_in))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::isp_request_out))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::isp_request_ptr))

_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp::isp_response_in))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp::isp_response_out))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp::isp_response_ptr))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp::isp_kstat))

_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp::isp_max_lun))

_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::isp_markers))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::isp_marker_in))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::isp_marker_out))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::isp_marker_free))

#ifdef	ISPDEBUG_IOCTL
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_request_mutex, isp::drop_command))
#endif

_NOTE(MUTEX_PROTECTS_DATA(isp::isp_intr_mutex, isp::isp_in_intr))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_intr_mutex, isp::isp_intr_cv))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_intr_mutex, isp::isp_polled_completion))

_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp::isp_checking_semlock))

_NOTE(MUTEX_PROTECTS_DATA(isp::isp_waitq_mutex, isp::isp_in_reset))

/* since we only set/read this in attach no lock is needed */
_NOTE(DATA_READABLE_WITHOUT_LOCK(isp::isp_no_obp))

/*
 * Convenient short-hand macros for reading/writing ISP registers
 */
#define	ISP_WRITE_BIU_REG(isp, regp, value)				\
	ddi_put16((isp)->isp_biu_acc_handle, (regp), (value))

#define	ISP_READ_BIU_REG(isp, regp)					\
	ddi_get16((isp)->isp_biu_acc_handle, (regp))

#define	ISP_WRITE_MBOX_REG(isp, regp, value)				\
	ddi_put16((isp)->isp_mbox_acc_handle, (regp), (value))

#define	ISP_READ_MBOX_REG(isp, regp)					\
	ddi_get16((isp)->isp_mbox_acc_handle, (regp))

#define	ISP_WRITE_SXP_REG(isp, regp, value)				\
	ddi_put16((isp)->isp_sxp_acc_handle, (regp), (value))

#define	ISP_READ_SXP_REG(isp, regp)					\
	ddi_get16((isp)->isp_sxp_acc_handle, (regp))

#define	ISP_WRITE_RISC_REG(isp, regp, value)				\
	ddi_put16((isp)->isp_risc_acc_handle, (regp), (value))

#define	ISP_READ_RISC_REG(isp, regp)					\
	ddi_get16((isp)->isp_risc_acc_handle, (regp))

/*
 * Convenient short-hand macros for setting/clearing register bits
 */
#define	ISP_SET_BIU_REG_BITS(isp, regp, value)				\
	ISP_WRITE_BIU_REG((isp), (regp),				\
		(ISP_READ_BIU_REG((isp), (regp)) | (value)))

#define	ISP_CLR_BIU_REG_BITS(isp, regp, value)				\
	ISP_WRITE_BIU_REG((isp), (regp),				\
		(ISP_READ_BIU_REG((isp), (regp)) & ~(value)))

/*
 * Hardware access definitions for ISP chip
 *
 */
#ifdef ISP_PERF
#define	ISP_WRITE_RISC_HCCR(isp, value)					\
	ISP_WRITE_RISC_REG((isp), &(isp)->isp_risc_reg->isp_hccr,	\
		(value));						\
	(isp)->isp_wpio_count++

#define	ISP_READ_RISC_HCCR(isp)						\
	((isp)->isp_rpio_count++,					\
	ISP_READ_RISC_REG((isp), &(isp)->isp_risc_reg->isp_hccr))

#define	ISP_REG_GET_RISC_INT(isp)					\
	((isp)->isp_rpio_count++,					\
	(ISP_READ_BIU_REG((isp), &(isp)->isp_biu_reg->isp_bus_isr) &	\
		ISP_BUS_ISR_RISC_INT))

#define	ISP_CLEAR_SEMAPHORE_LOCK(isp)					\
	ISP_CLR_BIU_REG_BITS((isp), &(isp)->isp_biu_reg->isp_bus_sema,\
		ISP_BUS_SEMA_LOCK);					\
	(isp)->isp_wpio_count++, (isp)->isp_rpio_count++

#define	ISP_SET_REQUEST_IN(isp)						\
	ISP_WRITE_MBOX_REG((isp), &(isp)->isp_mbox_reg->isp_mailbox4,	\
		(isp)->isp_request_in);					\
	(isp)->isp_wpio_count++, (isp)->isp_request_count++

#define	ISP_SET_RESPONSE_OUT(isp)					\
	ISP_WRITE_MBOX_REG((isp), &(isp)->isp_mbox_reg->isp_mailbox5,	\
		isp->isp_response_out);					\
	(isp)->isp_wpio_count++

#define	ISP_GET_REQUEST_OUT(isp)					\
	((isp)->isp_rpio_count++,					\
	ISP_READ_MBOX_REG((isp), &(isp)->isp_mbox_reg->isp_mailbox4))

#define	ISP_GET_RESPONSE_IN(isp)					\
	((isp)->isp_rpio_count++,					\
	ISP_READ_MBOX_REG((isp), &(isp)->isp_mbox_reg->isp_mailbox5))

#define	ISP_INT_PENDING(isp)						\
	((isp)->isp_rpio_count++,					\
	(ISP_READ_BIU_REG((isp), &(isp)->isp_biu_reg->isp_bus_isr) &	\
		ISP_BUS_ISR_RISC_INT))

#define	ISP_CHECK_SEMAPHORE_LOCK(isp)					\
	((isp)->isp_rpio_count++,					\
	(ISP_READ_BIU_REG((isp), &(isp)->isp_biu_reg->isp_bus_sema) &	\
		ISP_BUS_SEMA_LOCK))

#else	/* ISP_PERF */
#define	ISP_WRITE_RISC_HCCR(isp, value)					\
	ISP_WRITE_RISC_REG((isp), &(isp)->isp_risc_reg->isp_hccr,	\
		(value));

#define	ISP_READ_RISC_HCCR(isp)						\
	ISP_READ_RISC_REG((isp), &(isp)->isp_risc_reg->isp_hccr)

#define	ISP_REG_GET_RISC_INT(isp)					\
	(ISP_READ_BIU_REG((isp), &(isp)->isp_biu_reg->isp_bus_isr) &	\
		ISP_BUS_ISR_RISC_INT)

#define	ISP_CLEAR_SEMAPHORE_LOCK(isp)					\
	ISP_CLR_BIU_REG_BITS((isp), &(isp)->isp_biu_reg->isp_bus_sema,	\
		ISP_BUS_SEMA_LOCK)

#define	ISP_SET_REQUEST_IN(isp)						\
	ISP_WRITE_MBOX_REG((isp), &(isp)->isp_mbox_reg->isp_mailbox4,	\
		(isp)->isp_request_in)

#define	ISP_SET_RESPONSE_OUT(isp)					\
	ISP_WRITE_MBOX_REG((isp), &(isp)->isp_mbox_reg->isp_mailbox5,	\
		isp->isp_response_out)

#define	ISP_GET_REQUEST_OUT(isp)					\
	ISP_READ_MBOX_REG((isp), &(isp)->isp_mbox_reg->isp_mailbox4)

#define	ISP_GET_RESPONSE_IN(isp)					\
	ISP_READ_MBOX_REG((isp), &(isp)->isp_mbox_reg->isp_mailbox5)

#define	ISP_INT_PENDING(isp)						\
	(ISP_READ_BIU_REG((isp), &(isp)->isp_biu_reg->isp_bus_isr) &	\
		ISP_BUS_ISR_RISC_INT)

#define	ISP_CHECK_SEMAPHORE_LOCK(isp)					\
	(ISP_READ_BIU_REG((isp), &(isp)->isp_biu_reg->isp_bus_sema) &	\
		ISP_BUS_SEMA_LOCK)

#endif /* ISP_PERF */

#define	ISP_REG_SET_HOST_INT(isp)					\
	ISP_WRITE_RISC_HCCR((isp), ISP_HCCR_CMD_SET_HOST_INT);		\

#define	ISP_REG_GET_HOST_INT(isp)					\
	(ISP_READ_RISC_HCCR((isp)) & ISP_HCCR_HOST_INT)

#define	ISP_CLEAR_RISC_INT(isp)						\
	ISP_WRITE_RISC_HCCR((isp), ISP_HCCR_CMD_CLEAR_RISC_INT);

/*
 * short hand-macros to copy entries in/out of queues (IOPB area).
 */
#define	ISP_COPY_OUT_DMA_8(isp, source, dest, count)			\
	ddi_rep_put8((isp)->isp_dma_acc_handle, (uint8_t *)(source),	\
		(uint8_t *)(dest), (count), DDI_DEV_AUTOINCR)
#define	ISP_COPY_OUT_DMA_16(isp, source, dest, count)			\
	ddi_rep_put16((isp)->isp_dma_acc_handle, (uint16_t *)(source), 	\
		(uint16_t *)(dest), (count), DDI_DEV_AUTOINCR)
#define	ISP_COPY_OUT_DMA_32(isp, source, dest, count)			\
	ddi_rep_put32((isp)->isp_dma_acc_handle, (uint32_t *)(source),	\
		(uint32_t *)(dest), (count), DDI_DEV_AUTOINCR)

#define	ISP_COPY_IN_DMA_8(isp, source, dest, count)			\
	ddi_rep_get8((isp)->isp_dma_acc_handle, (uint8_t *)(dest),	\
		(uint8_t *)(source), (count), DDI_DEV_AUTOINCR)
#define	ISP_COPY_IN_DMA_16(isp, source, dest, count)			\
	ddi_rep_get16((isp)->isp_dma_acc_handle, (uint16_t *)(dest),	\
		(uint16_t *)(source), (count), DDI_DEV_AUTOINCR)
#define	ISP_COPY_IN_DMA_32(isp, source, dest, count)			\
	ddi_rep_get32((isp)->isp_dma_acc_handle, (uint32_t *)(dest),	\
		(uint32_t *)(source), (count), DDI_DEV_AUTOINCR)

#define	ISP_COPY_OUT_REQ(isp, source, dest)				\
	{								\
		register intptr_t s, d;					\
		s = (intptr_t)(source);					\
		d = (intptr_t)(dest);					\
		ISP_COPY_OUT_DMA_16((isp), s, d, 2);			\
		ISP_COPY_OUT_DMA_32((isp), (s + ISP_REQ_TOKEN_OFF),	\
			(d + ISP_REQ_TOKEN_OFF), 1);			\
		ISP_COPY_OUT_DMA_16((isp), (s + ISP_REQ_LUN_OFF),	\
			(d + ISP_REQ_LUN_OFF), 6);			\
		ISP_COPY_OUT_DMA_8((isp), (s + ISP_REQ_CDB_OFF),	\
			(d + ISP_REQ_CDB_OFF), 12);			\
		ISP_COPY_OUT_DMA_32((isp), (s + ISP_REQ_DATA_OFF),	\
			(d + ISP_REQ_DATA_OFF), 2);			\
	}

#define	ISP_COPY_IN_TOKEN(isp, source, dest)				\
	ISP_COPY_IN_DMA_32((isp), ((uchar_t *)(source) +		\
		ISP_RESP_TOKEN_OFF), (dest), 1);

#define	ISP_COPY_IN_RESP(isp, source, dest)				\
	{								\
		register intptr_t s, d;					\
		s = (intptr_t)(source);					\
		d = (intptr_t)(dest);					\
		ISP_COPY_IN_DMA_16((isp), s, d, 2);			\
		ISP_COPY_IN_DMA_32((isp), (s + ISP_RESP_TOKEN_OFF),	\
			(d + ISP_RESP_TOKEN_OFF), 1);			\
		ISP_COPY_IN_DMA_16((isp), (s + ISP_RESP_STATUS_OFF),	\
			(d + ISP_RESP_STATUS_OFF), 6);			\
		ISP_COPY_IN_DMA_32((isp), (s + ISP_RESP_RESID_OFF),	\
			(d + ISP_RESP_RESID_OFF), 1);			\
		if (((struct isp_response *)(dest))->resp_scb != 0) {	\
			ISP_COPY_IN_DMA_8((isp),			\
				(s + ISP_RESP_RQS_OFF),			\
				(d + ISP_RESP_RQS_OFF), 32);		\
		}							\
	}

/*
 * limit for number of times we will try to debounce a chip register --
 * after this many tries, if we still do not get the same value two times
 * in a row, it's okay to print a warning an just return the most recent
 * value
 */
#define	ISP_REG_DEBOUNCE_LIMIT		10

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_ISPVAR_H */

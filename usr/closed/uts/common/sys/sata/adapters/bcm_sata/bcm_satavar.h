/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file may contain confidential information of Broadcom
 * Semiconductor, and should not be distributed in source form
 * without approval from Sun Legal.
 */

#ifndef _BCM_SATAVAR_H
#define	_BCM_SATAVAR_H

#ifdef	__cplusplus
extern "C" {
#endif

/* Type for argument of event handler */
typedef struct bcm_event_arg {
	void		*bcmea_ctlp;
	void		*bcmea_portp;
	uint32_t	bcmea_event;
} bcm_event_arg_t;

/* Warlock annotation */
_NOTE(DATA_READABLE_WITHOUT_LOCK(bcm_event_arg_t::bcmea_ctlp))
_NOTE(DATA_READABLE_WITHOUT_LOCK(bcm_event_arg_t::bcmea_portp))
_NOTE(DATA_READABLE_WITHOUT_LOCK(bcm_event_arg_t::bcmea_event))

/*
 * flags for bcmp_flags
 *
 * BCM_PORT_FLAG_MOPPING: this flag will be set when the HBA is stopped,
 * and all the outstanding commands need to be aborted and sent to upper
 * layers.
 *
 * BCM_PORT_FLAG_POLLING: this flag will be set when the interrupt is
 * disabled, and the command is executed in POLLING mode.
 *
 * BCM_PORT_FLAG_RQSENSE: this flag will be set when a REQUEST SENSE which
 * is used to retrieve sense data is being executed.
 *
 * BCM_PORT_FLAG_STARTED: this flag will be set when the port is started.
 */
#define	BCM_PORT_FLAG_MOPPING	0x01
#define	BCM_PORT_FLAG_POLLING	0x02
#define	BCM_PORT_FLAG_RQSENSE	0x04
#define	BCM_PORT_FLAG_STARTED	0x08

/*
 * NON QDMA cmd flags
 */
#define	BCM_NONQDMA_CMD_COMPLETE	0x1
#define	BCM_NONQDMA_CMD_BUSY		0x2
#define	BCM_NONQDMA_CMD_RQSENSE		0x4

typedef struct bcm_nonqdma_cmd {
	caddr_t		bcm_v_addr;  /* I/O buffer address */
	size_t		bcm_byte_count; /* # bytes left to read/write */
	sata_pkt_t	*bcm_spkt;
	uint32_t	bcm_cmd_flags;
	uint8_t		bcm_rqsense_buff[SATA_ATAPI_RQSENSE_LEN];
} bcm_nonqdma_cmd_t;

typedef struct bcm_port {

	/* The port number */
	uint8_t			bcmp_num;
	/* Type of the device attached to the port */
	uint8_t			bcmp_device_type;
	/* State of the port */
	uint32_t		bcmp_state;
	/* port flags */
	uint32_t		bcmp_flags;

	/* Pointers and handles to command ring structure */
	bcm_cmd_descriptor_t	*bcmp_cmd_queue;
	ddi_dma_handle_t	bcmp_cmd_queue_dma_handle;
	ddi_acc_handle_t	bcmp_cmd_queue_acc_handle;
	ddi_dma_cookie_t	bcmp_cmd_queue_dma_cookie;

	/* pointers and handles for dma when QDMA is used */
	uchar_t *bcmp_prdts_qdma_dma_vaddr[BCM_CTL_QUEUE_DEPTH];
	ddi_dma_handle_t bcmp_prdts_qdma_dma_hdl[BCM_CTL_QUEUE_DEPTH];
	ddi_acc_handle_t bcmp_prdts_qdma_acc_hdl[BCM_CTL_QUEUE_DEPTH];
	ddi_dma_cookie_t bcmp_prdts_qdma_dma_cookie[BCM_CTL_QUEUE_DEPTH];

	/* pointers and handles for CDB blocks for ATAPI commands */
	uchar_t *bcmp_qdma_atapi_cdb_vaddr[BCM_CTL_QUEUE_DEPTH];
	ddi_dma_handle_t bcmp_qdma_cdb_dma_hdl[BCM_CTL_QUEUE_DEPTH];
	ddi_acc_handle_t bcmp_qdma_cdb_acc_hdl[BCM_CTL_QUEUE_DEPTH];
	ddi_dma_cookie_t bcmp_qdma_cdb_dma_cookie[BCM_CTL_QUEUE_DEPTH];

	/* cache value for port QPI */
	uint32_t 		bcmp_qpi;

	/* interrupt index */
	uint8_t			bcmp_intr_idx;

	/* QDMA engine flags */
	int bcmp_qdma_engine_flags;

	/* ATAPI command running flag to serialize ATAPI commands */
	int bcmp_qdma_pkt_cmd_running;

	/* Condition variable used for sync mode commands */
	kcondvar_t		bcmp_cv;

	/* The whole mutex for the port structure */
	kmutex_t		bcmp_mutex;

	/* Keep all the pending sata packets when QDMA is enabled */
	sata_pkt_t		*bcmp_slot_pkts[BCM_CTL_QUEUE_DEPTH];

	/* command slot for non qdma command */
	bcm_nonqdma_cmd_t  *bcmp_nonqdma_cmd;

	/* Keep the error retrieval sata packet */
	sata_pkt_t		*bcmp_err_retri_pkt;

	/*
	 * SATA HBA driver is supposed to remember and maintain device
	 * reset state. While the reset is in progress, it doesn't accept
	 * any more commands until receiving the command with
	 * SATA_CLEAR_DEV_RESET_STATE flag and SATA_IGNORE_DEV_RESET_STATE.
	 */
	int			bcmp_reset_in_progress;

	/* This is for error recovery handler */
	bcm_event_arg_t		*bcmp_event_args;
	/* This is to calculate how many mops are in progress */
	int			bcmp_mop_in_progress;
} bcm_port_t;

/* Warlock annotation */
_NOTE(READ_ONLY_DATA(bcm_port_t::bcmp_cmd_queue_dma_handle))
_NOTE(READ_ONLY_DATA(bcm_port_t::bcmp_prdts_qdma_dma_hdl))
_NOTE(MUTEX_PROTECTS_DATA(bcm_port_t::bcmp_mutex,
                                    bcm_port_t::bcmp_device_type))
_NOTE(MUTEX_PROTECTS_DATA(bcm_port_t::bcmp_mutex,
                                    bcm_port_t::bcmp_state))
_NOTE(MUTEX_PROTECTS_DATA(bcm_port_t::bcmp_mutex,
                                    bcm_port_t::bcmp_flags))
_NOTE(MUTEX_PROTECTS_DATA(bcm_port_t::bcmp_mutex,
                                    bcm_port_t::bcmp_slot_pkts))
_NOTE(MUTEX_PROTECTS_DATA(bcm_port_t::bcmp_mutex,
                                    bcm_port_t::bcmp_qpi))
_NOTE(MUTEX_PROTECTS_DATA(bcm_port_t::bcmp_mutex,
                                    bcm_port_t::bcmp_intr_idx))
_NOTE(MUTEX_PROTECTS_DATA(bcm_port_t::bcmp_mutex,
                                    bcm_port_t::bcmp_qdma_engine_flags))
_NOTE(MUTEX_PROTECTS_DATA(bcm_port_t::bcmp_mutex,
                                    bcm_port_t::bcmp_nonqdma_cmd))
_NOTE(MUTEX_PROTECTS_DATA(bcm_port_t::bcmp_mutex,
                                    bcm_port_t::bcmp_reset_in_progress))
_NOTE(MUTEX_PROTECTS_DATA(bcm_port_t::bcmp_mutex,
                                    bcm_port_t::bcmp_mop_in_progress))

typedef struct bcm_ctl {

	dev_info_t		*bcmc_dip;

	/* Number of controller ports */
	int			bcmc_num_ports;

	/* Port structures for the controller */
	bcm_port_t		*bcmc_ports[BCM_NUM_CPORTS];

	/* Pci configuration space handle */
	ddi_acc_handle_t	bcmc_pci_conf_handle;

	/* Mapping into bar 5 -  handle and base address(MMIO) */
	ddi_acc_handle_t	bcmc_bar_handle;
	uintptr_t		bcmc_bar_addr;

	/* Pointer used for sata hba framework registration */
	struct sata_hba_tran	*bcmc_sata_hba_tran;

	/* DMA attributes for the data buffer */
	ddi_dma_attr_t		bcmc_buffer_dma_attr;
	/* DMA attributes for the command queue */
	ddi_dma_attr_t		bcmc_cmd_queue_dma_attr;
	/* DMA attributes for prd tables when qdma is used */
	ddi_dma_attr_t		bcmc_prdt_qdma_dma_attr;
	/* DMA attributes for cdb block for ATAPI command */
	ddi_dma_attr_t		bcmc_atapi_cdb_dma_attr;

	/* Used for watchdog handler */
	timeout_id_t		bcmc_timeout_id;

	/* Per controller mutex */
	kmutex_t		bcmc_mutex;

	/* Components for interrupt */
	ddi_intr_handle_t	*bcmc_intr_htable;	/* For array of intrs */
	int			bcmc_intr_type;	/* What type of interrupt */
	int			bcmc_intr_cnt;	/* # of intrs returned */
	size_t			bcmc_intr_size;	/* Size of intr array */
	uint_t			bcmc_intr_pri;	/* Intr priority */
	int			bcmc_intr_cap;	/* Intr capabilities */

	int			bcmc_flags;	/* State flags of ctrl */
	off_t			bcmc_pmcsr_offset;
	/* int			bcmc_ctl_power_level; */

	int			bcmc_cap;	/* capability flags */

	/* Taskq for handling event */
	ddi_taskq_t		*bcmc_event_taskq;
} bcm_ctl_t;

/* Warlock annotation */
_NOTE(READ_ONLY_DATA(bcm_ctl_t::bcmc_ports))

_NOTE(MUTEX_PROTECTS_DATA(bcm_ctl_t::bcmc_mutex,
                                        bcm_ctl_t::bcmc_flags))
_NOTE(MUTEX_PROTECTS_DATA(bcm_ctl_t::bcmc_mutex,
                                        bcm_ctl_t::bcmc_timeout_id))

#define	BCM_SUCCESS	0  /* Successful return */
#define	BCM_TIMEOUT	1  /* Timed out */
#define	BCM_FAILURE	-1 /* Unsuccessful return */

/* Flags for bcmc_flags */
#define	BCM_ATTACH		0x1
#define	BCM_DETACH		0x2
#define	BCM_SUSPEND		0x4

/* Values for bcmc_cap */
#define	BCM_CAP_PM		0x1
#define	BCM_CAP_MSI		0x2
#define	BCM_CAP_PCIX		0x4
#define	BCM_CAP_SATA		0x8
#define	BCM_CAP_VS		0x10
#define	BCM_CAP_MSIX		0x20

/* 10 microseconds */
#define	BCM_WAIT_REG_CHECK	10

/* Flags controlling the restart port behavior */
#define	BCM_PORT_RESET		0x0001	/* Reset the port */
#define	BCM_PORT_INIT		0x0002	/* Initialize port */
#define	BCM_RESET_NO_EVENTS_UP	0x0004  /* Don't send reset events up */

/* Command types */
#define	BCM_NON_QDMA_CMD	0x1
#define	BCM_QDMA_CMD		0x2
#define	BCM_QDMA_CMD_ATAPI	0x4
#define	BCM_ERR_RETRI_CMD	0x8
#define	BCM_NCQ_QDMA_CMD	0x10

/* State values for bcm_attach */
#define	BCM_ATTACH_STATE_NONE			(0x1 << 0)
#define	BCM_ATTACH_STATE_STATEP_ALLOC		(0x1 << 1)
#define	BCM_ATTACH_STATE_REG_MAP		(0x1 << 2)
#define	BCM_ATTACH_STATE_PCICFG_SETUP		(0x1 << 3)
#define	BCM_ATTACH_STATE_INTR_ADDED		(0x1 << 4)
#define	BCM_ATTACH_STATE_MUTEX_INIT		(0x1 << 5)
#define	BCM_ATTACH_STATE_PORT_ALLOC		(0x1 << 6)
#define	BCM_ATTACH_STATE_ERR_RECV_TASKQ		(0x1 << 7)
#define	BCM_ATTACH_STATE_HW_INIT		(0x1 << 8)
#define	BCM_ATTACH_STATE_TIMEOUT_ENABLED	(0x1 << 9)

#define	BCM_BYTES_PER_SEC 512

/*
 * transform seconds to microseconds
 */
#define	BCM_SEC2USEC(x) x * MICROSEC

/* watchdog timeout set to 5 seconds */
#define	BCM_WATCHDOG_TIMEOUT	5

/* Interval used for delay */
#define	BCM_10MS_TICKS	(drv_usectohz(10000))	/* ticks in 10 millisec */
#define	BCM_1MS_TICKS	(drv_usectohz(1000))	/* ticks in 1 millisec */
#define	BCM_2MS_TICKS	(drv_usectohz(2000))	/* ticks in 2 millisec */
#define	BCM_100US_TICKS	(drv_usectohz(100))	/* ticks in 100  */
#define	BCM_100_USECS	(100)			/* usecs in 0.1 millisec */
#define	BCM_1MS_USECS	(1000)			/* usecs in 1 millisec */
#define	BCM_2MS_USECS	(2000)			/* usecs in 2 millisec */

#define	BCM_DELAY_NSEC(wait_ns)		\
{					\
	hrtime_t start, end;		\
	start = end =  gethrtime();	\
	while ((end - start) < wait_ns)	\
		end = gethrtime();	\
}

/*
 * The constants for the loops
 */
#define	BCM_NUMBER_FOR_LOOPS	100

/*
 * The following values are the numbers of times to retry polled requests.
 */
#define	BCM_POLLRATE_PORT_SSTATUS	10
#define	BCM_POLLRATE_GET_SPKT		100
#define	BCM_POLLRATE_PORT_TFD_ERROR	1000
#define	BCM_POLLRATE_PORT_RESET		5


#if DEBUG

#define	BCM_DEBUG		1

#define	BCMDBG_INIT		0x0001
#define	BCMDBG_ENTRY		0x0002
#define	BCMDBG_DUMP_PRB		0x0004
#define	BCMDBG_EVENT		0x0008
#define	BCMDBG_POLL_LOOP	0x0010
#define	BCMDBG_PKTCOMP		0x0020
#define	BCMDBG_TIMEOUT		0x0040
#define	BCMDBG_INFO		0x0080
#define	BCMDBG_VERBOSE		0x0100
#define	BCMDBG_INTR		0x0200
#define	BCMDBG_ERRS		0x0400
#define	BCMDBG_COOKIES		0x0800
#define	BCMDBG_POWER		0x1000
#define	BCMDBG_COMMAND		0x2000
#define	BCMDBG_SENSEDATA	0x4000
#define	BCMDBG_NCQ		0x8000
#define	BCMDBG_PM		0x10000

extern int bcm_debug_flag;

#define	BCMDBG0(flag, bcm_ctlp, format)					\
	if (bcm_debug_flags & (flag)) {					\
		bcm_log(bcm_ctlp, CE_WARN, format);			\
	}

#define	BCMDBG1(flag, bcm_ctlp, format, arg1)				\
	if (bcm_debug_flags & (flag)) {					\
		bcm_log(bcm_ctlp, CE_WARN, format, arg1);		\
	}

#define	BCMDBG2(flag, bcm_ctlp, format, arg1, arg2)			\
	if (bcm_debug_flags & (flag)) {					\
		bcm_log(bcm_ctlp, CE_WARN, format, arg1, arg2);		\
	}

#define	BCMDBG3(flag, bcm_ctlp, format, arg1, arg2, arg3)		\
	if (bcm_debug_flags & (flag)) {					\
		bcm_log(bcm_ctlp, CE_WARN, format, arg1, arg2, arg3); 	\
	}

#define	BCMDBG4(flag, bcm_ctlp, format, arg1, arg2, arg3, arg4)		\
	if (bcm_debug_flags & (flag)) {					\
		bcm_log(bcm_ctlp, CE_WARN, format, arg1, arg2, arg3, arg4); \
	}

#define	BCMDBG5(flag, bcm_ctlp, format, arg1, arg2, arg3, arg4, arg5)	\
	if (bcm_debug_flags & (flag)) {					\
		bcm_log(bcm_ctlp, CE_WARN, format, arg1, arg2,		\
		    arg3, arg4, arg5); 					\
	}
#else

#define	BCMDBG0(flag, dip, frmt)
#define	BCMDBG1(flag, dip, frmt, arg1)
#define	BCMDBG2(flag, dip, frmt, arg1, arg2)
#define	BCMDBG3(flag, dip, frmt, arg1, arg2, arg3)
#define	BCMDBG4(flag, dip, frmt, arg1, arg2, arg3, arg4)
#define	BCMDBG5(flag, dip, frmt, arg1, arg2, arg3, arg4, arg5)

#endif /* DEBUG */

#ifdef	__cplusplus
}
#endif

#endif /* _BCM_SATAVAR_H */

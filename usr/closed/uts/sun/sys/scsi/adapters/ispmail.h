/*
 * Copyright (c) 1993, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_SCSI_ADAPTERS_ISPMAIL_H
#define	_SYS_SCSI_ADAPTERS_ISPMAIL_H

#include <sys/note.h>

/*
 * isp mailbox definitions
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Mailbox Register 0 status bit definitions.
 */
#define	ISP_MBOX_EVENT_MASK			0xF000
#define	ISP_MBOX_EVENT_AEN			0x8000
#define	ISP_MBOX_EVENT_CMD			0x4000

#define	ISP_MBOX_STATUS_MASK			0x00FF
#define	ISP_MBOX_STATUS_OK			0x00
#define	ISP_MBOX_STATUS_INVALID_CMD		0x01
#define	ISP_MBOX_STATUS_INVALID_PARAMS		0x02
#define	ISP_MBOX_STATUS_BOOT_ERR		0x03
#define	ISP_MBOX_STATUS_FIRMWARE_ERR		0x04

/*
 * macros for getting various mailbox return registers
 *
 * Note: there should perhaps be a separate macro for setting a
 * mailbox register (for clarity sake) -- perhaps at a later time
 */
#define	ISP_MBOX_RETURN_REG(c, n)		\
			((c)->mbox_in[(n)])
#define	ISP_MBOX_RETURN_STATUS(c)		\
			(ISP_MBOX_RETURN_REG((c), 0) & ISP_MBOX_STATUS_MASK)

/*
 * these values are returned in mbox return registers when an invalid cmd
 * occurs after a power on reset of the chip -- at that time the ASCII
 * string "ISP   " is returned in the first three return registers
 */
#define	ISP_MBOX_INVALID_REG1			0x4953	/* ascii "IS" */
#define	ISP_MBOX_INVALID_REG2			0x5020	/* ascii "P " */
#define	ISP_MBOX_INVALID_REG3			0x2020	/* ascii "  " */

#define	ISP_MBOX_ASYNC_RESET			0x01
#define	ISP_MBOX_ASYNC_ERR			0x02
#define	ISP_MBOX_ASYNC_REQ_DMA_ERR		0x03
#define	ISP_MBOX_ASYNC_RESP_DMA_ERR		0x04
#define	ISP_MBOX_ASYNC_WAKEUP			0x05
#define	ISP_MBOX_ASYNC_INT_RESET		0x06
#define	ISP_MBOX_ASYNC_INT_DEV_RESET		0x09
#define	ISP_MBOX_ASYNC_INT_ABORT		0x08
#define	ISP_MBOX_ASYNC_EXTMSG_ERROR		0x0A
#define	ISP_MBOX_ASYNC_BUS_HANG			0x0C
#define	ISP_MBOX_ASYNC_OVR_RESET		0x0D
#define	ISP_MBOX_ASYNC_FASTPOST			0x20

#define	ISP_MBOX_BUSY				0x04

#define	ISP_GET_MBOX_STATUS(mailbox)		\
	(mailbox & ISP_MBOX_STATUS_MASK)
#define	ISP_GET_MBOX_EVENT(mailbox)		\
	(mailbox & ISP_MBOX_STATUS_MASK)

/* asynch event related defines */
#define	ISP_AEN_FAILURE		0
#define	ISP_AEN_SUCCESS		0x1
#define	ISP_AEN_MBOX_COMPLETED	0x2
#define	ISP_AEN_ASYNC_ERR	0x4
#define	ISP_AEN_DMA_ERR		0x8
#define	ISP_AEN_RESET_ERR	0x10
#define	ISP_AEN_DEV_RESET_ERR	0x20
#define	ISP_AEN_DEFAULT_ERR	0x40
#define	ISP_AEN_UNKNOWN		0x8000

#define	ISP_AEN_ERROR_MASK	(ISP_AEN_ASYNC_ERR | ISP_AEN_DMA_ERR |\
			ISP_AEN_MBOX_COMPLETED | ISP_AEN_UNKNOWN)
#define	ISP_AEN_NEED_RECOVERY	(ISP_AEN_ASYNC_ERR | ISP_AEN_DMA_ERR |\
			ISP_AEN_RESET_ERR)

#define	ISP_MBOX_CMD_NOP				0x00
#define	ISP_MBOX_CMD_ABOUT_PROM				0x08
#define	ISP_MBOX_CMD_RELOAD_CODE			0x0A
#define	ISP_MBOX_CMD_DNLD_CODE_SEG			0x0B
#define	ISP_MBOX_CMD_VFY_CODE_CHKSUM			0x0C
#define	ISP_MBOX_CMD_RUN_FW				0x0D
#define	ISP_MBOX_CMD_CHECKSUM_FIRMWARE			0x0E
#define	ISP_MBOX_CMD_STOP_FW				0x14
#define	ISP_MBOX_CMD_LOAD_RAM				0x01
#define	ISP_MBOX_CMD_START_FW				0x02
#define	ISP_MBOX_CMD_DUMP_RAM				0x03
#define	ISP_MBOX_CMD_LOAD_WORD				0x04
#define	ISP_MBOX_CMD_DUMP_WORD				0x05
#define	ISP_MBOX_CMD_WRAP_MAILBOXES			0x06
#define	ISP_MBOX_CMD_CHECKSUM				0x07
#define	ISP_MBOX_CMD_INIT_REQUEST_QUEUE			0x10
#define	ISP_MBOX_CMD_INIT_RESPONSE_QUEUE		0x11
#define	ISP_MBOX_CMD_SCSI_CMD				0x12
#define	ISP_MBOX_CMD_WAKE_UP				0x13
#define	ISP_MBOX_CMD_ABORT_IOCB				0x15
#define	ISP_MBOX_CMD_ABORT_DEVICE			0x16
#define	ISP_MBOX_CMD_ABORT_TARGET			0x17
#define	ISP_MBOX_CMD_BUS_RESET				0x18
#define	ISP_MBOX_CMD_STOP_QUEUE				0x19
#define	ISP_MBOX_CMD_START_QUEUE			0x1A
#define	ISP_MBOX_CMD_STEP_QUEUE				0x1B
#define	ISP_MBOX_CMD_ABORT_QUEUE			0x1C
#define	ISP_MBOX_CMD_GET_DEVICE_QUEUE_STATE		0x1D
#define	ISP_MBOX_CMD_GET_ISP_STAT			0x1E
#define	ISP_MBOX_CMD_GET_FIRMWARE_STATUS		0x1F
#define	ISP_MBOX_CMD_GET_SXP_CONFIG			0x2F
#define	ISP_MBOX_CMD_SET_SXP_CONFIG			0x3F
#define	ISP_MBOX_CMD_GET_SCSI_ID			0x20
#define	ISP_MBOX_CMD_SET_SCSI_ID			0x30
#define	ISP_MBOX_CMD_GET_SEL_TIMEOUT			0x21
#define	ISP_MBOX_CMD_SET_SEL_TIMEOUT			0x31
#define	ISP_MBOX_CMD_GET_RETRY_ATTEMPTS			0x22
#define	ISP_MBOX_CMD_SET_RETRY_ATTEMPTS			0x32
#define	ISP_MBOX_CMD_GET_AGE_LIMIT			0x23
#define	ISP_MBOX_CMD_SET_AGE_LIMIT			0x33
#define	ISP_MBOX_CMD_GET_CLOCK_RATE			0x24
#define	ISP_MBOX_CMD_SET_CLOCK_RATE			0x34
#define	ISP_MBOX_CMD_GET_PULL_UPS			0x25
#define	ISP_MBOX_CMD_SET_PULL_UPS			0x35
#define	ISP_MBOX_CMD_GET_DATA_TRANS_TIME		0x26
#define	ISP_MBOX_CMD_SET_DATA_TRANS_TIME		0x36
#define	ISP_MBOX_CMD_GET_BUS_INTERFACE			0x27
#define	ISP_MBOX_CMD_SET_BUS_INTERFACE			0x37
#define	ISP_MBOX_CMD_GET_TARGET_CAP			0x28
#define	ISP_MBOX_CMD_SET_DELAY_BDR			0x3B
#define	ISP_MBOX_CMD_SET_TARGET_CAP			0x38
#define	ISP_MBOX_CMD_GET_DEVICE_QUEUE_PARAMS		0x29
#define	ISP_MBOX_CMD_SET_DEVICE_QUEUE_PARAMS		0x39
#define	ISP_MBOX_CMD_GET_QFULL_RETRIES			0x4E
#define	ISP_MBOX_CMD_SET_QFULL_RETRIES			0x5E
#define	ISP_MBOX_CMD_GET_QFULL_RETRY_INTERVAL		0x4F
#define	ISP_MBOX_CMD_SET_QFULL_RETRY_INTERVAL		0x5F
#define	ISP_PCI_MBOX_CMD_RET_BIOS_BLK_ADDR		0x40	/* pci only */
#define	ISP_PCI_MBOX_CMD_WRITE_4_RAM_WORDS		0x41	/* pci only */
#define	ISP_PCI_MBOX_CMD_EXEC_BIOS_IOCB			0x42	/* pci only */
#define	ISP_MBOX_CMD_SET_DATA_OVR_RECOV_MODE		0x5A
#define	ISP_MBOX_CMD_GET_DATA_OVR_RECOV_MODE		0x5B
#define	ISP_MBOX_CMD_SET_HOST_DATA			0x5C
#define	ISP_MBOX_CMD_GET_HOST_DATA			0x5D

#define	ISP_CAP_DISCONNECT		0x8000	/* disconnected cmds */
#define	ISP_CAP_PARITY			0x4000	/* enable parity */
#define	ISP_CAP_WIDE			0x2000	/* wide mode */
#define	ISP_CAP_SYNC			0x1000	/* synchronous mode */
#define	ISP_CAP_TAG			0x0800	/* tagged queueing */
#define	ISP_CAP_AUTOSENSE		0x0400	/* ARS (auto request sense) */
#define	ISP_CAP_ERRSTOP			0x0200
#define	ISP_CAP_RENEGO_ERROR		0x0100
#define	ISP_CAP_FORCE_NARROW		0x0080
#define	ISP_CAP_FORCE_ASYNC		0x0040
#define	ISP_CAP_RENEGOT_WIDE_SYNC	0x0001	/* renegotiate wide/sync */

/*
 * ISP supports sync period in steps of 4 ns. For FAST20
 * we are supposed to round down to next whole integer.
 */
#define	ISP_20M_SYNC_PERIOD	0x000C
#define	ISP_20M_SYNC_OFFSET	0x0008
#define	ISP_20M_SYNC_PARAMS	((ISP_20M_SYNC_OFFSET << 8) | \
				ISP_20M_SYNC_PERIOD)
#define	ISP_10M_SYNC_PERIOD	0x0019
#define	ISP_10M_SYNC_OFFSET	0x000C
#define	ISP_10M_SYNC_PARAMS	((ISP_10M_SYNC_OFFSET << 8) | \
				ISP_10M_SYNC_PERIOD)
#define	ISP_8M_SYNC_PERIOD	0x0025
#define	ISP_8M_SYNC_OFFSET	0x000C
#define	ISP_8M_SYNC_PARAMS	((ISP_8M_SYNC_OFFSET << 8) | \
				ISP_8M_SYNC_PERIOD)
#define	ISP_5M_SYNC_PERIOD	0x0032
#define	ISP_5M_SYNC_OFFSET	0x000C
#define	ISP_5M_SYNC_PARAMS	((ISP_5M_SYNC_OFFSET << 8) | \
				ISP_5M_SYNC_PERIOD)
#define	ISP_4M_SYNC_PERIOD	0x0041
#define	ISP_4M_SYNC_OFFSET	0x000C
#define	ISP_4M_SYNC_PARAMS	((ISP_4M_SYNC_OFFSET << 8) | \
				ISP_4M_SYNC_PERIOD)

/* mailbox related structures and defines */
#define	ISP_MAX_MBOX_REGS		8
#define	ISP_MBOX_CMD_TIMEOUT		10
#define	ISP_MBOX_CMD_RETRY_CNT		1


#define	ISP_MBOX_CMD_FLAGS_COMPLETE	0x01
#define	ISP_MBOX_CMD_FLAGS_Q_NOT_INIT	0x02

/* mailbox command struct */
struct isp_mbox_cmd {
	uchar_t		n_mbox_out;	/* no of mbox out regs wrt driver */
	uchar_t		n_mbox_in;	/* no of mbox in  regs wrt driver */
	uint16_t	mbox_out[ISP_MAX_MBOX_REGS]; /* outgoing registers  */
	uint16_t	mbox_in[ISP_MAX_MBOX_REGS]; /* incoming registers  */
};

_NOTE(SCHEME_PROTECTS_DATA("Semaphore", isp_mbox_cmd::n_mbox_out))
_NOTE(SCHEME_PROTECTS_DATA("Semaphore", isp_mbox_cmd::n_mbox_in))
_NOTE(SCHEME_PROTECTS_DATA("Semaphore", isp_mbox_cmd::mbox_out))
_NOTE(SCHEME_PROTECTS_DATA("Semaphore", isp_mbox_cmd::mbox_in))


/* isp mailbox struct */
struct isp_mbox {
	ksema_t			mbox_sema;   /* sema to sequentialize access */
	uchar_t			mbox_flags;  /* mbox register flags */
	struct isp_mbox_cmd 	mbox_cmd;    /* mbox command */
};

#define	ISP_MBOX_CMD_BUSY_WAIT_TIME		1    /* sec */
#define	ISP_MBOX_CMD_BUSY_POLL_DELAY_TIME	100  /* usecs */


/* the number of regular mailbox registers used for parameters */
#define	ISP_MAX_MBOX_PARAMS		3

/*
 * the following structure defines each mailbox command, used in list
 * referenced below
 */
struct isp_mbox_cmd_info {
	short		mbox_cmd_num_params_out;	/* to chip */
	short		mbox_cmd_num_params_in;		/* from chip */
};

/*
 * declare the list of mailbox commands and its size for external use
 */
extern struct isp_mbox_cmd_info	isp_mbox_cmd_list[];
extern const int		isp_mbox_cmd_list_size;


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_ISPMAIL_H */

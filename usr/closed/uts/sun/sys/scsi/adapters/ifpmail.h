/*
 * Copyright (c) 1997, 1999, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_SCSI_ADAPTERS_IFPMAIL_H
#define	_SYS_SCSI_ADAPTERS_IFPMAIL_H

#include <sys/note.h>

/*
 * ifp mailbox definitions
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Mailbox Register 0 status bit definitions.
 */
#define	IFP_MBOX_EVENT_MASK			0xF000
#define	IFP_MBOX_EVENT_ASYNCH			0x8000
#define	IFP_MBOX_EVENT_CMD			0x4000

#define	IFP_MBOX_STATUS_LOOP_DOWN		0x4005

#define	IFP_MBOX_STATUS_MASK			0x00FF
#define	IFP_MBOX_STATUS_OK			0x00
#define	IFP_MBOX_STATUS_INVALID_CMD		0x01
#define	IFP_MBOX_STATUS_HOST_INTERFACE_ERR	0x02
#define	IFP_MBOX_STATUS_BOOT_ERR		0x03
#define	IFP_MBOX_STATUS_FIRMWARE_ERR		0x04
#define	IFP_MBOX_STATUS_COMMAND_ERR		0x05
#define	IFP_MBOX_STATUS_COMMAND_PARAM_ERR	0x06

#define	IFP_MBOX_ASYNC_RESET			0x01
#define	IFP_MBOX_ASYNC_ERR			0x02
#define	IFP_MBOX_ASYNC_REQ_DMA_ERR		0x03
#define	IFP_MBOX_ASYNC_RESP_DMA_ERR		0x04
#define	IFP_MBOX_ASYNC_LIP_OCCURED		0x10
#define	IFP_MBOX_ASYNC_LOOP_UP			0x11
#define	IFP_MBOX_ASYNC_LOOP_DOWN		0x12
#define	IFP_MBOX_ASYNC_LIP_RESET		0x13
#define	IFP_MBOX_ASYNC_PDB_UPDATED		0x14
#define	IFP_MBOX_ASYNC_FAST_IO_POST		0x20
#define	IFP_MBOX_ASYNC_CTIO_COMPLETE		0x21

#define	IFP_MBOX_BUSY				0x04

#define	IFP_GET_MBOX_EVENT(mailbox)		\
	(mailbox & IFP_MBOX_STATUS_MASK)

/*
 * At times, an mbox status of loop down is acceptable.
 * Also, a fatal error status is acceptable if the driver is already
 * in the process of handling a fatal error. This would happen, for
 * instance, if the target driver decides to do a reset (which uses
 * the mbox) when the driver is already in the process of handling
 * a fw crash as part of starting a previous mbox cmd.
 */
#define	MBOX_ACCEPTABLE_STATUS(ifp, status)				\
	((((status) == IFP_MBOX_STATUS_LOOP_DOWN) ||			\
	((ifp)->ifp_handling_fatal_aen && ((status) == 0x8002 ||	\
	(status) == 0x8003 || (status == 0x8004)))) ? 1 : 0)

/* option to broadcast LIP_RESET to all nodes on the loop */
#define	IFP_LIP_RESET_ALL_ID			0xFF

/* asynch event related defines */
#define	IFP_AEN_NOT_AN_AEN				-1
#define	IFP_AEN_SUCCESS					0
#define	IFP_AEN_RESET					1 /* 3rd party reset */
#define	IFP_AEN_SYS_ERR					2 /* FW crash */
#define	IFP_AEN_DMA_ERR					3 /* req/resp DMA err */
#define	IFP_AEN_FAST_POST 				4 /* fast posting    */
#define	IFP_AEN_LIP 					5 /* LIP occured evt */
#define	IFP_AEN_LOOP_DOWN 				6 /* loss of sync    */
#define	IFP_AEN_LOOP_UP 				7 /* loop back up    */

#define	IFP_MBOX_CMD_NOP				0x00
#define	IFP_MBOX_CMD_LOAD_RAM				0x01
#define	IFP_MBOX_CMD_START_FW				0x02
#define	IFP_MBOX_CMD_DUMP_RAM				0x03
#define	IFP_MBOX_CMD_LOAD_WORD				0x04
#define	IFP_MBOX_CMD_DUMP_WORD				0x05
#define	IFP_MBOX_CMD_WRAP_MAILBOXES			0x06
#define	IFP_MBOX_CMD_CHECKSUM				0x07
#define	IFP_MBOX_CMD_ABOUT_PROM				0x08
#define	IFP_MBOX_CMD_LOAD_RISC_RAM			0x09
#define	IFP_MBOX_CMD_DUMP_RISC_RAM			0x0A
#define	IFP_MBOX_CMD_CHECKSUM_FIRMWARE			0x0E
#define	IFP_MBOX_CMD_INIT_REQUEST_QUEUE			0x10
#define	IFP_MBOX_CMD_INIT_RESPONSE_QUEUE		0x11
#define	IFP_MBOX_CMD_SCSI_CMD				0x12
#define	IFP_MBOX_CMD_WAKE_UP				0x13
#define	IFP_MBOX_CMD_STOP_FW				0x14
#define	IFP_MBOX_CMD_ABORT_IOCB				0x15
#define	IFP_MBOX_CMD_ABORT_DEVICE			0x16
#define	IFP_MBOX_CMD_ABORT_TARGET			0x17
#define	IFP_MBOX_CMD_BUS_RESET				0x18
#define	IFP_MBOX_CMD_STOP_QUEUE				0x19
#define	IFP_MBOX_CMD_START_QUEUE			0x1A
#define	IFP_MBOX_CMD_STEP_QUEUE				0x1B
#define	IFP_MBOX_CMD_ABORT_QUEUE			0x1C
#define	IFP_MBOX_CMD_GET_DEVICE_QUEUE_STATE		0x1D
#define	IFP_MBOX_CMD_GET_IFP_STAT			0x1E
#define	IFP_MBOX_CMD_GET_FIRMWARE_STATUS		0x1F
#define	IFP_MBOX_CMD_GET_ID				0x20
#define	IFP_MBOX_CMD_GET_RETRY_ATTEMPTS			0x22
#define	IFP_MBOX_CMD_GET_TARGET_CAP			0x28
#define	IFP_MBOX_CMD_GET_DEVICE_QUEUE_PARAMS		0x29
#define	IFP_MBOX_CMD_SET_RETRY_ATTEMPTS			0x32
#define	IFP_MBOX_CMD_SET_TARGET_CAP			0x38
#define	IFP_MBOX_CMD_SET_DEVICE_QUEUE_PARAMS		0x39
#define	IFP_MBOX_CMD_LOOPBACK				0x45
#define	IFP_MBOX_CMD_SELF_TEST				0x46
#define	IFP_MBOX_CMD_ENH_GET_PORT_DB			0x47
#define	IFP_MBOX_CMD_SET_LOAD_RISC_RAM			0x50
#define	IFP_MBOX_CMD_SET_DUMP_RISC_RAM			0x51
#define	IFP_MBOX_CMD_SCSI_CMD64				0x54
#define	IFP_MBOX_CMD_INIT_FIRMWARE			0x60
#define	IFP_MBOX_CMD_GET_INIT_CONTROL_BLOCK		0x61
#define	IFP_MBOX_CMD_INIT_LIP				0x62
#define	IFP_MBOX_CMD_GET_FC_AL_POSITION_MAP		0x63
#define	IFP_MBOX_CMD_GET_PORT_DB			0x64
#define	IFP_MBOX_CMD_CLEAR_ACA				0x65
#define	IFP_MBOX_CMD_TARGET_RESET			0x66
#define	IFP_MBOX_CMD_CLEAR_TASK_SET			0x67
#define	IFP_MBOX_CMD_ABORT_TASK_SET			0x68
#define	IFP_MBOX_CMD_GET_FW_STATE			0x69
#define	IFP_MBOX_CMD_GET_PORT_NAME			0x6A
#define	IFP_MBOX_CMD_GET_LINK_STATUS			0x6B
#define	IFP_MBOX_CMD_ISSUE_LIP_RESET			0x6C
#define	IFP_MBOX_CMD_ISSUE_LIP_WITH_PLOGI		0x72

/*
 * Firmware state defintions. Returned in mailbox 1 on Get FW State
 * command.
 */
#define	IFP_FW_CONFIG_WAIT	0x0000
#define	IFP_FW_WAIT_AL_PA	0x0001
#define	IFP_FW_WAIT_LOGIN	0x0002
#define	IFP_FW_READY		0x0003
#define	IFP_FW_LOSS_OF_SYNC	0x0004
#define	IFP_FW_ERROR		0x0005
#define	IFP_FW_REINIT		0x0006
#define	IFP_FW_NON_PART		0x0007

/*
 * Defines for mbox registers needed for a given command
 */
#define	ARG0			0
#define	ARG1			1
#define	ARG2			2
#define	ARG3			3
#define	ARG4			4
#define	ARG5			5
#define	ARG6			6
#define	ARG7			7
#define	ARG8			8

#define	REG0			0
#define	REG1			1
#define	REG2			2
#define	REG3			3
#define	REG4			4
#define	REG5			5
#define	REG6			6
#define	REG7			7
#define	REG8			8
#define	REG9			9
#define	REG10			10
#define	REG11			11
#define	REG12			12
#define	REG13			13
#define	REG14			14
#define	REG15			15
#define	REG16			16
#define	REG17			17
#define	REG18			18
#define	REG19			19

/* mailbox related structures and defines */
#define	IFP_MAX_MBOX_REGS21		8
#define	IFP_MAX_MBOX_REGS22		20
#define	IFP_MBOX_CMD_TIMEOUT		10
#define	IFP_MBOX_CMD_RETRY_CNT		1

#define	IFP_MBOX_CMD_FLAGS_COMPLETE	0x01
#define	IFP_MBOX_CMD_FLAGS_Q_NOT_INIT	0x02

/* mailbox command struct */
struct ifp_mbox_cmd {
	uint_t		timeout;	/* timeout for cmd */
	uchar_t		retry_cnt;	/* retry count */
	uchar_t		n_mbox_out;	/* no of mbox out regs wrt driver */
	uchar_t		n_mbox_in;	/* no of mbox in  regs wrt driver */
	ushort_t	mbox_out [IFP_MAX_MBOX_REGS22]; /* outgoing regs  */
	ushort_t	mbox_in  [IFP_MAX_MBOX_REGS22]; /* incoming regs  */
};

_NOTE(SCHEME_PROTECTS_DATA("Mutex", ifp_mbox_cmd::retry_cnt))
_NOTE(SCHEME_PROTECTS_DATA("Mutex", ifp_mbox_cmd::timeout))
_NOTE(SCHEME_PROTECTS_DATA("Mutex", ifp_mbox_cmd::n_mbox_out))
_NOTE(SCHEME_PROTECTS_DATA("Mutex", ifp_mbox_cmd::n_mbox_in))
_NOTE(SCHEME_PROTECTS_DATA("Mutex", ifp_mbox_cmd::mbox_out))
_NOTE(SCHEME_PROTECTS_DATA("Mutex", ifp_mbox_cmd::mbox_in))

/* ifp mailbox struct */
struct ifp_mbox {
	kmutex_t		mbox_mutex;  /* mutex to sequentialize access */
	struct ifp_mbox_cmd 	mbox_cmd;    /* mbox command */
	uchar_t			mbox_flags;  /* mbox register flags */
};

#define	IFP_MBOX_CMD_BUSY_WAIT_TIME		1    /* sec */
#define	IFP_MBOX_CMD_BUSY_POLL_DELAY_TIME	100  /* usecs */

#define	TRS	IFP_MBOX_CMD_TARGET_RESET
#define	CTS	IFP_MBOX_CMD_CLEAR_TASK_SET
#define	ABTS	IFP_MBOX_CMD_ABORT_TASK_SET
#define	CACA	IFP_MBOX_CMD_CLEAR_ACA

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_IFPMAIL_H */

/*
 * Copyright (c) 2000 by Sun Microsystems, Inc.
 * All rights reserved.
 *
 * isp_cmds - A list of ISP 10X0 Mailbox commands and arguments
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"


#include <sys/types.h>
#include <sys/ksynch.h>

#include <sys/scsi/adapters/ispmail.h>


/*
 * this array represents all commands known to the ISP firmware
 *
 * entries are indexed by their opcode (see ispmail.h>
 *
 * entries for invalid commands have -1 for their param count as a marker
 *
 * empty entries are listed as {-1, -1} and
 * entries not yet verified have {-2, -2}
 */
struct isp_mbox_cmd_info	isp_mbox_cmd_list[] = {
	{ 0, 0 },	/* ISP_MBOX_CMD_NOP 0x00 */
	{ 4, 0 },	/* ISP_MBOX_CMD_LOAD_RAM 0x01 */
	{ 1, 0 },	/* ISP_MBOX_CMD_START_FW 0x02 */
	{ 4, 0 },	/* ISP_MBOX_CMD_DUMP_RAM 0x03 */
	{ 2, 1 },	/* ISP_MBOX_CMD_LOAD_WORD 0x04 */
	{ 2, 1 },	/* ISP_MBOX_CMD_DUMP_WORD 0x05 */
	{ 5, 5 },	/* ISP_MBOX_CMD_WRAP_MAILBOXES 0x06 */
	{ 1, 0 },	/* ISP_MBOX_CMD_CHECKSUM 0x07 */
	{ 0, 3 },	/* ISP_MBOX_CMD_ABOUT_PROM 0x08 */
	{ -1, -1},	/* 0x09 */
	{ 0, 0},	/* ISP_MBOX_CMD_RELOAD_CODE 0x0A */
	{ 0, 0},	/* ISP_MBOX_CMD_DNLD_CODE_SEG 0x0B */
	{ 0, 0},	/* ISP_MBOX_CMD_VFY_CODE_CHKSUM 0x0C */
	{ 0, 0},	/* ISP_MBOX_CMD_RUN_FW 0x0D */
	{ 0, 2 },	/* ISP_MBOX_CMD_CHECKSUM_FIRMWARE 0x0E */
	{ -1, -1},	/* 0x0F */
	{ 5, 5 },	/* ISP_MBOX_CMD_INIT_REQUEST_QUEUE 0x10 */
	{ 5, 5 },	/* ISP_MBOX_CMD_INIT_RESPONSE_QUEUE 0x11 */
	{ 3, 3 },	/* ISP_MBOX_CMD_SCSI_CMD 0x12 */
	{ 1, 1 },	/* ISP_MBOX_CMD_WAKE_UP 0x13 */
	{ 0, 0 },	/* ISP_MBOX_CMD_STOP_FW 0x14 */
	{ 3, 3 },	/* ISP_MBOX_CMD_ABORT_IOCB 0x15 */
	{ 1, 1 },	/* ISP_MBOX_CMD_ABORT_DEVICE 0x16 */
	{ 2, 2 },	/* ISP_MBOX_CMD_ABORT_TARGET 0x17 */
	{ 1, 1 },	/* ISP_MBOX_CMD_BUS_RESET 0x18 */
	{ 1, 2 },	/* ISP_MBOX_CMD_STOP_QUEUE 0x19 */
	{ 1, 2 },	/* ISP_MBOX_CMD_START_QUEUE 0x1A */
	{ 1, 2 },	/* ISP_MBOX_CMD_STEP_QUEUE 0x1B */
	{ 1, 2 },	/* ISP_MBOX_CMD_ABORT_QUEUE 0x1C */
	{ 1, 3 },	/* ISP_MBOX_CMD_GET_DEVICE_QUEUE_STATE 0x1D */
	{ 0, 3 },	/* ISP_MBOX_CMD_GET_ISP_STAT 0x1E */
	{ 1, 3 },	/* ISP_MBOX_CMD_GET_FIRMWARE_STATUS 0x1F */
	{ 0, 1 },	/* ISP_MBOX_CMD_GET_SCSI_ID 0x20 */
	{ 0, 1 },	/* ISP_MBOX_CMD_GET_SEL_TIMEOUT 0x21 */
	{ 0, 2 },	/* ISP_MBOX_CMD_GET_RETRY_ATTEMPTS 0x22 */
	{ 0, 1 },	/* ISP_MBOX_CMD_GET_AGE_LIMIT 0x23 */
	{ 0, 1 },	/* ISP_MBOX_CMD_GET_CLOCK_RATE 0x24 */
	{ 0, 1 },	/* ISP_MBOX_CMD_GET_PULL_UPS 0x25 */
	{ 0, 1 },	/* ISP_MBOX_CMD_GET_DATA_TRANS_TIME 0x26 */
	{ 0, 2 },	/* ISP_MBOX_CMD_GET_BUS_INTERFACE 0x27 */
	{ 1, 3 },	/* ISP_MBOX_CMD_GET_TARGET_CAP 0x28 */
	{ 1, 3 },	/* ISP_MBOX_CMD_GET_DEVICE_QUEUE_PARAMS 0x29 */
	{ -1, -1},	/* 0x2A */
	{ -1, -1},	/* 0x2B */
	{ -1, -1},	/* 0x2C */
	{ -1, -1},	/* 0x2D */
	{ -1, -1},	/* 0x2E */
	{ 1, 3 },	/* ISP_MBOX_CMD_GET_SXP_CONFIG 0x2F */
	{ 1, 1 },	/* ISP_MBOX_CMD_SET_SCSI_ID 0x30 */
	{ 1, 1 },	/* ISP_MBOX_CMD_SET_SEL_TIMEOUT 0x31 */
	{ 2, 2 },	/* ISP_MBOX_CMD_SET_RETRY_ATTEMPTS 0x32 */
	{ 1, 0 },	/* ISP_MBOX_CMD_SET_AGE_LIMIT 0x33 */
	{ 1, 2 },	/* ISP_MBOX_CMD_SET_CLOCK_RATE 0x34 */
	{ 1, 1 },	/* ISP_MBOX_CMD_SET_PULL_UPS 0x35 */
	{ 1, 1 },	/* ISP_MBOX_CMD_SET_DATA_TRANS_TIME 0x36 */
	{ 2, 2 },	/* ISP_MBOX_CMD_SET_BUS_INTERFACE 0x37 */
	{ 3, 3 },	/* ISP_MBOX_CMD_SET_TARGET_CAP 0x38 */
	{ 3, 3 },	/* ISP_MBOX_CMD_SET_DEVICE_QUEUE_PARAMS 0x39 */
	{ -1, -1},	/* 0x3A */
	{ 1, 1 },	/* ISP_MBOX_CMD_SET_DELAY_BDR 0x3B */
	{ -1, -1},	/* 0x3C */
	{ -1, -1},	/* 0x3D */
	{ -1, -1},	/* 0x3E */
	{ 3, 3 },	/* ISP_MBOX_CMD_SET_SXP_CONFIG 0x3F */
	{ -2, -2 },	/* ISP_PCI_MBOX_CMD_RET_BIOS_BLK_ADDR 0x40 */
	{ -2, -2 },	/* ISP_PCI_MBOX_CMD_WRITE_4_RAM_WORDS 0x41 */
	{ -2, -2 },	/* ISP_PCI_MBOX_CMD_EXEC_BIOS_IOCB 0x42 */
	{ -1, -1},	/* 0x43 */
	{ -1, -1},	/* 0x44 */
	{ -1, -1},	/* 0x45 */
	{ -1, -1},	/* 0x46 */
	{ -1, -1},	/* 0x47 */
	{ -1, -1},	/* 0x48 */
	{ -1, -1},	/* 0x49 */
	{ -1, -1 },	/* 0x4A */
	{ -1, -1},	/* 0x4B */
	{ -1, -1},	/* 0x4C */
	{ -1, -1},	/* 0x4D */
	{ 2, 2 },	/* ISP_MBOX_CMD_GET_QFULL_RETRIES 0x4E */
	{ 2, 2 },	/* ISP_MBOX_CMD_GET_QFULL_RETRY_INTERVAL 0x4F */
	{ -1, -1},	/* 0x50 */
	{ -1, -1},	/* 0x51 */
	{ -1, -1},	/* 0x52 */
	{ -1, -1},	/* 0x53 */
	{ -1, -1},	/* 0x54 */
	{ -1, -1},	/* 0x55 */
	{ -1, -1},	/* 0x56 */
	{ -1, -1},	/* 0x57 */
	{ -1, -1},	/* 0x58 */
	{ -1, -1},	/* 0x59 */
	{ 1, 1 },	/* ISP_MBOX_CMD_SET_DATA_OVR_RECOV_MODE 0x5A */
	{ 0, 1},	/* ISP_MBOX_CMD_GET_DATA_OVR_RECOV_MODE 0x5B */
	{ 3, 3},	/* ISP_MBOX_CMD_SET_HOST_DATA 0x5C */
	{ 0, 3},	/* ISP_MBOX_CMD_GET_HOST_DATA 0x5D */
	{ 2, 2 },	/* ISP_MBOX_CMD_SET_QFULL_RETRIES 0x5E */
	{ 2, 2 }	/* ISP_MBOX_CMD_SET_QFULL_RETRY_INTERVAL 0x5F */
};

const int	isp_mbox_cmd_list_size =
		    (sizeof (isp_mbox_cmd_list) /
			sizeof (isp_mbox_cmd_list[0]));

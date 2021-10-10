/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * This file may contain confidential information of LSI Logic
 * and should not be distributed in source form without approval
 * from Sun Legal.
 */

/*
 * Implementation of "scsi_vhci_f_asym_lsi" asymmetric failover_ops.
 */

#include <sys/conf.h>
#include <sys/file.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/scsi_vhci.h>

/* Supported device table entries.  */
char *lsi_dev_table[] = {
/*	"                  111111" */
/*	"012345670123456789012345" */
/*	"|-VID--||-----PID------|" */

	"LSI     INF-01-00       ",
	"ENGENIO INF-01-00       ",	/* 6998; E6000 - 6091 */
	"SUN     ArrayStorage6000",	/* Pikes Peak */
	"SUN     CSM100_R_FC     ",	/* Unibrick FC 2882 */
	"SUN     CSM100_R_SA     ",	/* Unibrick SATA 2882 */
	"SUN     CSM100_E_SA_I   ",	/* Unibrick SATA 2882 */
	"SUN     CSM100_E_SA_D   ",	/* Unibrick SATA 2882 */
	"SUN     CSM200_E        ",	/* Sun - ESM Card - 4600 */
	"SUN     CSM200_R        ",	/* Sun - 3994 */
	"SUN     LCSM100_F       ",	/* Hickory for FC */
	"SUN     LCSM100_S       ",	/* Hickory for SAS */
	"SUN     LCSM100_I       ",	/* Hickory for iSCSI */
	"SUN     LCSM100_E       ",	/* Hickory for expansion */
	"SUN     STK6580_6780    ",	/* XBB2 */
	"SUN     SUN_6180        ",	/* Matterhorn */
	"STK     OPENstorage D178",	/* StorageTek D178 */
	"STK     OPENstorage D280",	/* StorageTek D280 */
	"STK     OPENstorage D220",	/* StorageTek D220 */
	"STK     OPENstorage D240",	/* StorageTek D240 */
	"STK     BladeCtlr BC84  ",	/* BladeStore 4884 */
	"STK     BladeCtlr BC82  ",	/* BladeStore 2882 */
	"STK     BladeCtlr BC88  ",	/* BladeStore 5884 */
	"STK     BladeCtlr B220  ",	/* BStr SATA 4884 */
	"STK     BladeCtlr B280  ",	/* BStr SATA 5884 */
	"STK     BladeCtlr B210  ",	/* BStr SATA 2882 */
	"STK     FLEXLINE 320    ",	/* STK - 3992 */
	"STK     FLEXLINE 340    ",	/* STK - 3994 */
	"STK     FLEXLINE 380    ",	/* STK - 6091 */
	"IBM     1742            ",	/* FAStT700 - 4884 */
	"IBM     1742-900        ",	/* FAStT900 - 5884 */
	"IBM     1722-600        ",	/* FAStT600 - 2882 */
	"IBM     1724-100  FAStT ",	/* DS4100 */
	"IBM     1726-2xx  FAStT ",	/* IBM X-series Cary SAS */
	"IBM     1726-22x        ",	/* DS3200 */
	"IBM     1726-4xx  FAStT ",	/* IBM X-series Cary FC */
	"IBM     1726-42x        ",	/* DS3400 */
	"IBM     1726-3xx  FAStT ",	/* IBM X-series Cary iSCSI */
	"IBM     1815      FAStT ",	/* IBM FAStT - 6091 */
	"IBM     1814      FAStT ",	/* DS4700 */
	"IBM     1814-200  FAStT ",	/* DS4200 */
	"IBM     1818      FAStT ",	/* DS5000 */
	"IBM     3542            ",	/* FAStT200 */
	"IBM     3552            ",	/* FAStT500 */
	"SGI     TP9300          ",	/* TP9300 - 2882 */
	"SGI     TP9300 1G-2C    ",	/* TP9300 - 2882 */
	"SGI     TP9400          ",	/* TP9400 - 4884 */
	"SGI     TP9500          ",	/* TP9500 - 5884 FC */
	"SGI     TP9500S         ",	/* TP9500 - 5884 SATA */
	"SGI     TP9700          ",	/* SGI - 6091 */
	"SGI     IS500           ",	/* SGI - 3994 */
	"SGI     IS400           ",	/* SGI - 1932 */
	"SGI     IS300           ",	/* SGI - 1331, 1332 */
	"SGI     IS600           ",	/* InfiniteStorage */
	"Fujitsu SX300           ",	/* Primergy Storage */
	"DELL    MD3000          ",	/* DELL SAS Storage */
	"DELL    MD3000i         ",	/* DELL iSCSI Storage */
	NULL
};

/* Failover module plumbing. */
SCSI_FAILOVER_OP("f_asym_lsi", lsi);

#define	LSI_FO_CMD_RETRY_DELAY	1000000 /* 1 seconds */
#define	LSI_FO_RETRY_DELAY	2000000 /* 2 seconds */
#define	LSI_TIMEOUT		144
/*
 * max time for failover to complete is 3 minutes.  Compute
 * number of retries accordingly, to ensure we wait for at least
 * 3 minutes
 */
#define	LSI_FO_MAX_RETRIES	(3*60*1000000)/LSI_FO_RETRY_DELAY

/*
 * max number of retries for lsi 2882 controller failover
 * to complete where the ping command is failing due to
 * transport errors or commands being rejected by
 * LSI device.
 * LSI2882_FO_MAX_RETRIES takes into account the case
 * where CMD_CMPLTs but LSI2882 takes time to complete the failover.
 */
#define	LSI_FO_MAX_CMD_RETRIES		3
#define	LSI_MAX_CMD_RETRIES		3
#define	LSI_MAX_MODE_CMD_RETRIES	60
#define	LSI_DEVICE_READY_RETRIES	3

#define	LSI_INQ_PAGE_C8			0xc8
#define	LSI_INQ_PAGE_C9			0xc9
#define	LSI_MODE_PAGE_2C		0x2c
#define	LSI_RDAC_CLAIM_OWNERSHIP	0x81
#define	LSI_C9_INQ_BUF_SIZE		0xff
#define	LSI_C8_INQ_BUF_SIZE		LSI_C9_INQ_BUF_SIZE
#define	LSI_SCSI_CMD10_BUF_SIZE		512
#define	LSI_RDAC_DUAL_ACTIVE_MODE	0x02
#define	LSI_OTHER_CONTROLLER_OWNED	0x02

/*
 * Private Function return values
 */
#define	LSI_DEVICE_COMMAND_SUCCESS	0
#define	LSI_DEVICE_COMMAND_FAILED	1
#define	LSI_DEVICE_COMMAND_RETRY	2

#define	LSI_SCSI_CMD_SUCCESS		0
#define	LSI_SCSI_CMD_FAILED		1
#define	LSI_SCSI_CMD_CHECK_SENSE	2

/*
 * LSI proprietary Sense Key Definitions
 */
#define	LSI_INVALID_LU_OWNER_SKEY	0x05
#define	LSI_INVALID_LU_OWNER_ASCQ	0x01
#define	LSI_INVALID_LU_OWNER_ASC	0x94

#define	LSI_INVALID_SECONDLU_OWNER_ASCQ	0x02

#define	LSI_CMD_LOCK_VIOLATION_SKEY	0x05
#define	LSI_CMD_LOCK_VIOLATION_ASCQ	0x36
#define	LSI_CMD_LOCK_VIOLATION_ASC	0x91

#define	LSI_IN_QUIESCENCE_SKEY		0x06
#define	LSI_IN_QUIESCENCE_ASCQ		0x02
#define	LSI_IN_QUIESCENCE_ASC		0x8b

#define	LSI_LUN_GETTING_READY_SKEY	0x02
#define	LSI_LUN_GETTING_READY_ASC	0x04
#define	LSI_LUN_GETTING_READY_ASCQ	0x01

#define	LSI_RECOVERABLE_ERROR_SKEY	0x0b
#define	LSI_RECOVERABLE_ERROR_ASC	0x44
#define	LSI_RECOVERABLE_ERROR_ASCQ	0x00

#define	LSI_BUS_DEV_RESET_SKEY		0x06
#define	LSI_BUS_DEV_RESET_ASC		0x29
#define	LSI_BUS_DEV_RESET_ASCQ		0x00

#define	LSI_DEV_INTRNL_RESET_SKEY	0x06
#define	LSI_DEV_INTRNL_RESET_ASC	0x29
#define	LSI_DEV_INTRNL_RESET_ASCQ	0x04

#define	LSI_LUN_NOT_READY_SKEY		0x02
#define	LSI_LUN_NOT_READY_ASC		0x04
#define	LSI_LUN_NOT_READY_ACSQ		0x07

#define	LSI_LUN_NOT_RDY_FORMAT_SKEY	0x02
#define	LSI_LUN_NOT_RDY_FORMAT_ASC	0x04
#define	LSI_LUN_NOT_RDY_FORMAT_ASCQ	0x04

#define	LSI_QUIESCENCE_ARCHIVED_SKEY	0x02
#define	LSI_QUIESCENCE_ARCHIVED_ASC	0x04
#define	LSI_QUIESCENCE_ARCHIVED_ASCQ	0xa1

#define	LSI_CNTRLR_REPLACED_SKEY	0x06
#define	LSI_CNTRLR_REPLACED_ASC		0x95
#define	LSI_CNTRLR_REPLACED_ASCQ	0x02

#define	LSI_DRIVE_BUSY_SKEY		0x04
#define	LSI_DRIVE_BUSY_ASC		0xd1
#define	LSI_DRIVE_BUSY_ASCQ		0x0a

#define	LSI_LUN_DATA_CHANGED_SKEY	0x06
#define	LSI_LUN_DATA_CHANGED_ASC	0x3f
#define	LSI_LUN_DATA_CHANGED_ASCQ	0x0e

#define	LSI_EXT_DRV_INSERTION_SKEY	0x01
#define	LSI_EXT_DRV_INSERTION_ASC	0x95
#define	LSI_EXT_DRV_INSERTION_ASCQ	0x01

#define	LSI_WRITE_CACHE_ACT_SKEY	0x06
#define	LSI_WRITE_CACHE_ACT_ASC		0x0c
#define	LSI_WRITE_CACHE_ACT_ASCQ	0x00

#define	LSI_MICROCODE_CHANGED_SKEY	0x06
#define	LSI_MICROCODE_CHANGED_ASC	0x3f
#define	LSI_MICROCODE_CHANGED_ASCQ	0x01

#define	LSI_INQUIRY_DATA_CHANGED_SKEY	0x06
#define	LSI_INQUIRY_DATA_CHANGED_ASC	0x3f
#define	LSI_INQUIRY_DATA_CHANGED_ASCQ	0X03

/*
 * private function return values
 */
#define	LSI_SENSE_CMD_LOCK		1 /* CMD Lock Violation */
#define	LSI_SENSE_QUIESCENCE		2 /* Array in quiescence */
#define	LSI_SENSE_UNKNOWN		3 /* Unknown sense resp for mode cmd */

static int lsi_get_fo_mode(struct scsi_device *sd,
		int *mode, int *ownership, int *xlf_capable);
static int lsi_analyze_mode_cmd_sense_info(uint8_t *sns, char *c_str);
static int lsi_do_scsi_cmd(struct scsi_pkt *pkt);
static int lsi_send_scsi_cmd(struct scsi_pkt *pkt, char *c_str);

/*
 * Framework Routine to probe the target device.
 * Returns 1 if lsi device was detected
 * Returns 0 if probe failed.
 */
/* ARGSUSED */
static int
lsi_device_probe(struct scsi_device *sd, struct scsi_inquiry *stdinq,
void **ctpriv)
{
	char	**dt;
	int	xlf = 0, mode = 0, ownership = 0;

	VHCI_DEBUG(6, (CE_NOTE, NULL, "lsi_device_probe str: %s\n",
	    stdinq->inq_vid));

	for (dt = lsi_dev_table; *dt; dt++) {
		if (strncmp(stdinq->inq_vid, *dt, strlen(*dt)))
			continue;

		/* match */
		if (lsi_get_fo_mode(sd, &mode, &ownership, &xlf)) {
			VHCI_DEBUG(4, (CE_WARN, NULL, "device_probe: "
			    "Fetching FO mode failed"));
			return (SFO_DEVICE_PROBE_PHCI);
		}

		/*
		 * Currently we do not support implicit
		 * mode failover(AVT) under scsi_vhci.
		 */
		if (mode == SCSI_EXPLICIT_FAILOVER) {
			VHCI_DEBUG(4, (CE_NOTE, NULL, "device_probe: "
			    "explicit mode\n"));
			return (SFO_DEVICE_PROBE_VHCI);
		} else {
			VHCI_DEBUG(4, (CE_WARN, NULL, "device_probe:"
			    "implicit mode, not supported"));
			return (SFO_DEVICE_PROBE_PHCI);
		}
	}
	return (SFO_DEVICE_PROBE_PHCI);
}

/*
 * Framework Routine to probe the target device.
 */
/* ARGSUSED */
static void
lsi_device_unprobe(struct scsi_device *sd, void *ctpriv)
{
	/*
	 * For future use
	 */
}

/*
 * Local routine to get inquiry page Cx(c8 or c9) from the LSI device.
 */
static int
lsi_get_inquiry_page_cx(struct scsi_device *sd, unsigned char page,
			unsigned char *buf, int size)
{
	int		retval = 0;
	struct buf	*bp;
	struct scsi_pkt	*pkt;
	struct scsi_address	*ap;
	char		*str;
	int		retry_cnt = 0;

	if ((buf == NULL) || (size == 0) ||
	    ((page != LSI_INQ_PAGE_C8) && (page != LSI_INQ_PAGE_C9))) {
		return (1);
	}
	bp = getrbuf(KM_NOSLEEP);
	if (bp == NULL) {
		return (1);
	}
	bp->b_un.b_addr = (char *)buf;
	bp->b_flags = B_READ;
	bp->b_bcount = size;
	bp->b_resid = 0;

	ap = &sd->sd_address;
	pkt = scsi_init_pkt(ap, NULL, bp, CDB_GROUP0,
	    sizeof (struct scsi_arq_status), 0, 0, NULL, NULL);
	if (pkt == NULL) {
		VHCI_DEBUG(4, (CE_WARN, NULL, "lsi_get_inquiry_page_cx: "
		    "Failed to initialize packet"));
		freerbuf(bp);
		return (1);
	}

	/*
	 * Send the inquiry command for page cx to the target and
	 * peel thru the returned data to figure out the ownership
	 * and other required details.
	 */

	pkt->pkt_cdbp[0] = SCMD_INQUIRY;
	pkt->pkt_cdbp[1] = 0x1;
	pkt->pkt_cdbp[2] = page;
	pkt->pkt_cdbp[4] = (unsigned char)size;
	pkt->pkt_time = 90;
	if (page == LSI_INQ_PAGE_C9) {
		str = "lsi_get_inquiry_page_c9";
	} else {
		str = "lsi_get_inquiry_page_c8";
	}

retry_cx_cmd:
	retval = lsi_send_scsi_cmd(pkt, str);
	if (retval == LSI_DEVICE_COMMAND_SUCCESS) {
		if ((buf[1] & 0xff) != (unsigned int)page) {
			VHCI_DEBUG(4, (CE_WARN, NULL,
			    "lsi_get_inquiry_page_cx: "
			    "Returned page is NOT %x !..got %x",
			    page, (buf[1] & 0xff)));
			retval = 1;
		} else {
			retval = 0;
		}
	} else {
		/*
		 * Failed to send command
		 */

		if (retval == LSI_DEVICE_COMMAND_RETRY) {
			retry_cnt++;
			if (retry_cnt <
			    LSI_MAX_MODE_CMD_RETRIES) {
				VHCI_DEBUG(4, (CE_NOTE, NULL,
				    "%s: Retrying cmd again. "
				    " Retry Count: %d\n", str,
				    retry_cnt));
				goto retry_cx_cmd;
			} else {
				VHCI_DEBUG(1, (CE_NOTE, NULL,
				    "%s: Retry attempts failed."
				    " Giving up\n", str));
			}
		}
		retval = 1;
	}
	scsi_destroy_pkt(pkt);
	freerbuf(bp);
	return (retval);
}

/*
 * Local Routine to fetch failover mode from the target.
 * return 1 for failure
 * return 0 for success
 */
/* ARGSUSED */
static int
lsi_get_fo_mode(struct scsi_device *sd, int *mode,
int *ownership, int *xlf_capable)
{
	unsigned char		inq_c9_buf[LSI_C9_INQ_BUF_SIZE];


	*mode = *ownership = *xlf_capable = 0;

	if (lsi_get_inquiry_page_cx(sd, LSI_INQ_PAGE_C9, inq_c9_buf,
	    LSI_C9_INQ_BUF_SIZE)) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_get_fo_mode: sd(%p):Unable to "
		    "get inquiry Page c9", (void*)sd));
		return (1);
	}

	/*
	 * Now lets look at the AVTE(byte8 bit 7) to figure out if
	 * the target is operating with in Auto volume Transfer(AVT) mode
	 * or not. NOTE: byte offset indicated is counting from 0.
	 */
	if (inq_c9_buf[8] & 0x80) {
		*mode =  SCSI_IMPLICIT_FAILOVER;
	} else {
		*mode =  SCSI_EXPLICIT_FAILOVER;
	}

	/*
	 * Now lets check if the this path is primary or Secondary
	 * The CVP bit for this is bit 0 or byte8 - counting from
	 * 0.
	 * CVP bit value 0 means 'the volume is NOT owned by the
	 *		controller receiving the inquiry command.
	 * CVP bit value 1 means that the volume is owned by the
	 *		receiving the inquiry command at the time
	 *		the command was processed.
	 */

	*ownership = inq_c9_buf[8] & 0x01;

	/*
	 * No support for xlf capability in LSI
	 * framework uses this field, hence setting it to 0.
	 */
	*xlf_capable = 0;
	return (0);
}

/*
 * Local Routine to get Mode sense data from the target
 * Returns	LSI_DEVICE_COMMAND_SUCCESS or
 *		LSI_DEVICE_COMMAND_FAILED or
 *		LSI_DEVICE_COMMAND_RETRY
 */
/* ARGSUSED */
static int
lsi_fetch_mode_sense_data(struct scsi_device *sd, unsigned char *buf,
			int size, unsigned char page)
{

	struct scsi_address	*ap;
	struct buf		*bp;
	struct scsi_pkt		*pkt;
	int			retval;
	unsigned char		lval;

	if (buf == NULL || size == 0) {
		return (LSI_DEVICE_COMMAND_FAILED);
	}

	ap = &sd->sd_address;
	bp = getrbuf(KM_NOSLEEP);
	if (bp == NULL) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_fetch_mode_sense_data:"
		    "Failed to allocate buffer"));
		return (LSI_DEVICE_COMMAND_FAILED);
	}
	bzero(buf, size);
	bp->b_un.b_addr = (char *)buf;
	bp->b_flags = B_READ;
	bp->b_bcount = size;
	bp->b_resid = 0;
	pkt = scsi_init_pkt(ap, NULL, bp, CDB_GROUP1,
	    sizeof (struct scsi_arq_status), 0, 0, NULL, NULL);
	if (pkt == NULL) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_fetch_mode_sense_data:"
		    "Failed to initialize packet"));
		freerbuf(bp);
		return (LSI_DEVICE_COMMAND_FAILED);
	}

	pkt->pkt_cdbp[0] = SCMD_MODE_SENSE_G1;
	pkt->pkt_cdbp[1] = 0;
	pkt->pkt_cdbp[2] = page;
	pkt->pkt_cdbp[3] = 0x01;
	pkt->pkt_cdbp[4] = 0;
	pkt->pkt_cdbp[5] = 0;
	pkt->pkt_cdbp[6] = 0;
	/*
	 * MSB len
	 */
	lval = (unsigned char)((size & 0xff00) >>8);

	pkt->pkt_cdbp[7] = lval;

	/*
	 * LSB len
	 */
	lval = (unsigned char)(size & 0xff);

	pkt->pkt_cdbp[8] = lval;

	pkt->pkt_time = LSI_TIMEOUT;

	retval = lsi_send_scsi_cmd(pkt, "lsi_fetch_mode_sense_data");
	scsi_destroy_pkt(pkt);
	freerbuf(bp);
	return (retval);
}

/*
 * Local Routine to send Mode select data to the target
 * Returns	LSI_DEVICE_COMMAND_SUCCESS or
 *		LSI_DEVICE_COMMAND_FAILED or
 *		LSI_DEVICE_COMMAND_RETRY
 */
/* ARGSUSED */
static int
lsi_send_mode_select_data(struct scsi_device *sd, unsigned char *buf,
			int size)
{

	struct scsi_address	*ap;
	struct buf		*bp;
	struct scsi_pkt		*pkt;
	int			retval;
	unsigned char		lval;


	if (buf == NULL || size == 0) {
		return (LSI_DEVICE_COMMAND_FAILED);
	}

	ap = &sd->sd_address;
	bp = getrbuf(KM_NOSLEEP);
	if (bp == NULL) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_send_mode_select_data:"
		    "Failed to allocate buffer"));
		return (LSI_DEVICE_COMMAND_FAILED);
	}
	bp->b_un.b_addr = (char *)buf;
	bp->b_flags = B_WRITE;
	bp->b_bcount = size;
	bp->b_resid = 0;
	pkt = scsi_init_pkt(ap, NULL, bp, CDB_GROUP1,
	    sizeof (struct scsi_arq_status), 0, 0, NULL, NULL);
	if (pkt == NULL) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_send_mode_select_data:"
		    "Failed to initialize packet"));
		freerbuf(bp);
		return (LSI_DEVICE_COMMAND_FAILED);
	}

	pkt->pkt_cdbp[0] = SCMD_MODE_SELECT_G1;
	pkt->pkt_cdbp[1] = 0;
	pkt->pkt_cdbp[2] = 0;
	pkt->pkt_cdbp[3] = 0;
	pkt->pkt_cdbp[4] = 0;
	pkt->pkt_cdbp[5] = 0;
	pkt->pkt_cdbp[6] = 0;

	/*
	 * MSB len
	 */
	lval = (unsigned char)((size & 0xff00) >>8);

	pkt->pkt_cdbp[7] = lval;

	/*
	 * LSB len
	 */

	lval = (unsigned char)(size & 0xff);

	pkt->pkt_cdbp[8] = lval;

	pkt->pkt_time = LSI_TIMEOUT;
	retval = lsi_send_scsi_cmd(pkt, "lsi_send_mode_select_data");
	scsi_destroy_pkt(pkt);
	freerbuf(bp);
	return (retval);
}



/*
 * Local Routine to send Scsi Command target
 * The routine in scsi_vhci.c - vhci_do_scsi_cmd() is not
 * being used is because, if the array is reporting busy or
 * quiescence using the device proprietary code values, we
 * do not need to retry the command 3 times immediately. Instead,
 * in this routine we wait for a second and then retry.
 * This way we are not overwhelming the array with multiple
 * retransmissions even though its reporting busy.
 *
 * Returns	LSI_DEVICE_COMMAND_SUCCESS or
 *		LSI_DEVICE_COMMAND_FAILED or
 *		LSI_DEVICE_COMMAND_RETRY
 */
/* ARGSUSED */
static int
lsi_send_scsi_cmd(struct scsi_pkt *pkt, char *c_str)
{
	int retval;
	uint8_t	*sns;

	retval = lsi_do_scsi_cmd(pkt);
	switch (retval) {

		case LSI_SCSI_CMD_SUCCESS:
			retval =
			    LSI_DEVICE_COMMAND_SUCCESS;
			break;

		case LSI_SCSI_CMD_CHECK_SENSE:

			sns = (uint8_t *)
			    &(((struct scsi_arq_status *)(uintptr_t)
			    (pkt->pkt_scbp))->sts_sensedata);

			retval = lsi_analyze_mode_cmd_sense_info(sns, c_str);

			if ((retval == LSI_SENSE_CMD_LOCK) ||
			    (retval == LSI_SENSE_QUIESCENCE)) {
				/*
				 * suggest retry the whole
				 * operation and not just this
				 * particular command
				 */
				retval =
				    LSI_DEVICE_COMMAND_RETRY;
				break;
			}
			/* Fall thru */

		default:
			/* Failed to send */
			retval = LSI_DEVICE_COMMAND_FAILED;
			break;
	}
	return (retval);
}

/*
 * Local Routine to handle sense code info that typically
 * gets returned on a Mode sense/mode select command
 * after failover
 * Returns:
 *	LSI_SENSE_CMD_LOCK: if in COM lock violation
 *	LSI_SENSE_QUIESCENCE: If array is in quiescence
 *	LSI_SENSE_UNKNOWN: If none of the above
 */
/* ARGSUSED */
static int
lsi_analyze_mode_cmd_sense_info(uint8_t *sns, char *c_str)
{
	uint8_t skey, asc, ascq;

	skey = scsi_sense_key(sns);
	asc = scsi_sense_asc(sns);
	ascq = scsi_sense_ascq(sns);

	if ((skey == LSI_CMD_LOCK_VIOLATION_SKEY) &&
	    (asc == LSI_CMD_LOCK_VIOLATION_ASC) &&
	    (ascq == LSI_CMD_LOCK_VIOLATION_ASCQ)) {
		/*
		 * maybe some other path has already initiated
		 * the failover operation
		 */
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "%s: Cmd in process - via another path? "
		    "retry MODE command. Got Sense Code"
		    "Key %x; ASC %x ; ASCQ %x\n", c_str, skey,
		    asc, ascq));
		return (LSI_SENSE_CMD_LOCK);
	}

	if (((skey == LSI_IN_QUIESCENCE_SKEY) &&
	    (asc == LSI_IN_QUIESCENCE_ASC) &&
	    (ascq == LSI_IN_QUIESCENCE_ASCQ)) ||

	    ((skey == LSI_LUN_GETTING_READY_SKEY) &&
	    (asc == LSI_LUN_GETTING_READY_ASC) &&
	    (ascq == LSI_LUN_GETTING_READY_ASCQ)) ||

	    ((skey == LSI_BUS_DEV_RESET_SKEY) &&
	    (asc == LSI_BUS_DEV_RESET_ASC) &&
	    (ascq == LSI_BUS_DEV_RESET_ASCQ)) ||

	    ((skey == LSI_DEV_INTRNL_RESET_SKEY) &&
	    (asc == LSI_DEV_INTRNL_RESET_ASC) &&
	    (ascq == LSI_DEV_INTRNL_RESET_ASCQ)) ||

	    ((skey == LSI_LUN_NOT_READY_SKEY) &&
	    (asc == LSI_LUN_NOT_READY_ASC) &&
	    (ascq == LSI_LUN_NOT_READY_ACSQ)) ||

	    ((skey == LSI_LUN_NOT_RDY_FORMAT_SKEY) &&
	    (asc == LSI_LUN_NOT_RDY_FORMAT_ASC) &&
	    (ascq == LSI_LUN_NOT_RDY_FORMAT_ASCQ)) ||

	    ((skey == LSI_QUIESCENCE_ARCHIVED_SKEY) &&
	    (asc == LSI_QUIESCENCE_ARCHIVED_ASC) &&
	    (ascq == LSI_QUIESCENCE_ARCHIVED_ASCQ)) ||

	    ((skey == LSI_CNTRLR_REPLACED_SKEY) &&
	    (asc == LSI_CNTRLR_REPLACED_ASC) &&
	    (ascq == LSI_CNTRLR_REPLACED_ASCQ)) ||

	    ((skey == LSI_DRIVE_BUSY_SKEY) &&
	    (asc == LSI_DRIVE_BUSY_ASC) &&
	    (ascq == LSI_DRIVE_BUSY_ASCQ)) ||

	    ((skey == LSI_LUN_DATA_CHANGED_SKEY) &&
	    (asc == LSI_LUN_DATA_CHANGED_ASC) &&
	    (ascq == LSI_LUN_DATA_CHANGED_ASCQ)) ||

	    ((skey == LSI_EXT_DRV_INSERTION_SKEY) &&
	    (asc == LSI_EXT_DRV_INSERTION_ASC) &&
	    (ascq == LSI_EXT_DRV_INSERTION_ASCQ)) ||

	    ((skey == LSI_WRITE_CACHE_ACT_SKEY) &&
	    (asc == LSI_WRITE_CACHE_ACT_ASC) &&
	    (ascq == LSI_WRITE_CACHE_ACT_ASCQ)) ||

	    ((skey == LSI_RECOVERABLE_ERROR_SKEY) &&
	    (asc == LSI_RECOVERABLE_ERROR_ASC) &&
	    (ascq == LSI_RECOVERABLE_ERROR_ASCQ)) ||

	    ((skey ==  LSI_MICROCODE_CHANGED_SKEY) &&
	    (asc == LSI_MICROCODE_CHANGED_ASC) &&
	    (ascq == LSI_MICROCODE_CHANGED_ASCQ)) ||

	    ((skey == LSI_INQUIRY_DATA_CHANGED_SKEY) &&
	    (asc == LSI_INQUIRY_DATA_CHANGED_ASC) &&
	    (ascq == LSI_INQUIRY_DATA_CHANGED_ASCQ))) {
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "%s: In Quiescence-retry MODE command. Got Sense Code"
		    "Key %x; ASC %x ; ASCQ %x\n", c_str, skey, asc, ascq));
		return (LSI_SENSE_QUIESCENCE);
	}


	VHCI_DEBUG(1, (CE_WARN, NULL,
	    "%s: Unhandled/recognized Sense Code. "
	    "Key %x; ASC %x ; ASCQ %x", c_str, skey, asc, ascq));
	return (LSI_SENSE_UNKNOWN);
}
/*
 * Local Routine to verify if the target is ready - for instance
 * after failover
 */
/* ARGSUSED */
static int
lsi_is_target_ready(struct scsi_device *sd)
{

	struct scsi_address	*ap;
	struct buf		*bp;
	struct scsi_pkt		*pkt;
	int			retval;
	int			rc;
	uint8_t			*sns, skey, asc, ascq;

	ap = &sd->sd_address;
	bp = getrbuf(KM_NOSLEEP);
	if (bp == NULL) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_is_target_ready:"
		    "Failed to allocate buffer"));
		return (1);
	}
	bp->b_flags = B_READ;
	bp->b_bcount = 0;
	bp->b_resid = 0;
	pkt = scsi_init_pkt(ap, NULL, bp, CDB_GROUP0,
	    sizeof (struct scsi_arq_status), 0, 0, NULL, NULL);
	if (pkt == NULL) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_is_target_ready:"
		    "Failed to initialize packet"));
		freerbuf(bp);
		return (1);
	}

	pkt->pkt_cdbp[0] = SCMD_TEST_UNIT_READY;
	pkt->pkt_cdbp[1] = 0;
	pkt->pkt_cdbp[2] = 0;
	pkt->pkt_cdbp[3] = 0;
	pkt->pkt_cdbp[4] = 0;
	pkt->pkt_time = LSI_TIMEOUT;

	retval = vhci_do_scsi_cmd(pkt);
	if (retval == 0) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_is_target_ready:"
		    "Failed to send UNIT Test Command"));
	} else {
		rc = (*pkt->pkt_scbp) & STATUS_MASK;
		retval = 0;
		switch (rc) {

			case STATUS_QFULL:
				VHCI_DEBUG(4, (CE_NOTE, NULL,
				    "lsi_is_target_ready: Status QFULL\n"));
				break;

			case STATUS_BUSY:
				VHCI_DEBUG(4, (CE_NOTE, NULL,
				    "lsi_is_target_ready: Status BUSY\n"));
				break;

			case STATUS_CHECK:
				if ((pkt->pkt_state & STATE_ARQ_DONE)) {
					sns = (uint8_t *)
					    &(((struct scsi_arq_status *)
					    (uintptr_t)(pkt->pkt_scbp))->
					    sts_sensedata);
					skey = scsi_sense_key(sns);
					asc = scsi_sense_asc(sns);
					ascq = scsi_sense_ascq(sns);
					VHCI_DEBUG(4, (CE_WARN, NULL,
					    "lsi_is_target_ready:"
					    "Chk Condition Sense key %x"
					    "ASC: %x ASCQ:%x",
					    skey, asc, ascq));
				}
				break;

			case STATUS_GOOD:
				retval = 1;
				break;

			default:
				VHCI_DEBUG(4, (CE_NOTE, NULL,
				    "lsi_is_target_ready: Target Not ready\n"));
				break;
		}
	}
	scsi_destroy_pkt(pkt);
	freerbuf(bp);
	return (retval);
}

/*
 * Local Routine to activate the target via explicit mode(non-AVT mode)
 */
/* ARGSUSED */
static int
lsi_activate_explicit(struct scsi_device *sd, int xlf_capable)
{

	unsigned char		s_data[LSI_SCSI_CMD10_BUF_SIZE], *ptr;
	unsigned char		c8_buf[LSI_C8_INQ_BUF_SIZE];
	int			i, get_lun_num;
	int			lun;
	dev_info_t		*cdip;
	mdi_pathinfo_t		*pip = NULL;
	int			j, k, sense_retry_cnt = 0;
	int			select_retry_cnt = 0;

do_retry:
	k = lsi_fetch_mode_sense_data(sd, s_data,
	    LSI_SCSI_CMD10_BUF_SIZE, LSI_MODE_PAGE_2C);

	switch (k) {
		case LSI_DEVICE_COMMAND_SUCCESS:
			break;

		case LSI_DEVICE_COMMAND_RETRY:
			sense_retry_cnt++;
			if (sense_retry_cnt < LSI_MAX_MODE_CMD_RETRIES) {
				VHCI_DEBUG(4, (CE_NOTE, NULL,
				    "lsi_activate_explicit:"
				    "Retrying Sense Data request!!! cnt:%d\n",
				    sense_retry_cnt));
				/*
				 * sleep for 1 second and try again
				 */
				drv_usecwait(LSI_FO_CMD_RETRY_DELAY);
				goto do_retry;
			}
			VHCI_DEBUG(4, (CE_NOTE, NULL,
			    "lsi_activate_explicit:"
			    "EXCEEDS Max Retry Cnt %d\n", sense_retry_cnt));
			/* FALL THRU */

		default:
		VHCI_DEBUG(4, (CE_NOTE, NULL, "lsi_activate_explicit:"
		    "Unable to fetch Mode sense data for page 2c!!\n"));
		return (1);
	}

	/*
	 * The s_data[0] points to the parameterlist field following
	 * the CDB:
	 *		+-------------------------+ <=== s_data[0]
	 *		| Mode Parameter Header   |
	 *		+-------------------------+
	 *		| Block Descriptor	  |
	 *		+-------------------------+
	 *		| Page info/0x2c page info|
	 *		+-------------------------+
	 * The s_data buffer will have the received sense data.
	 * skip over the Mode Parameter header(8bytes) and block
	 * descriptor information and get the offset to the page
	 * 2c data. s_data[6] and s_data[7] has the block descriptor
	 * length info and the +8 refers to the Mode parameter
	 * header_10 size.
	 */
	i = ((s_data[6] << 8) | s_data[7]) + 8;

	/*
	 * 'i' Now has the offset to the first byte to the received
	 * 0x2c byte. Lets ensure that value of 'i' obtained is
	 * within expected bounds.
	 */
	if (i >= LSI_SCSI_CMD10_BUF_SIZE) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_activate_explicit:"
		    "Offset for Block Descr field(%d) in mode "
		    "sense data exceeds the provided buffer!!", i));
		return (1);
	}

	/*
	 * lets make ptr refer to the first byte of the received
	 * mode sense data
	 */

	ptr = &s_data[i];

	/*
	 * First lets check if the returned page is indeed
	 * 0x2c before we proceed further.
	 */

	if ((ptr[0] & 0x3f) != 0x2c) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_activate_explicit:"
		    "Expected page 2c but GOT page %x !!!",
		    (ptr[0] & 0x3f)));
		return (1);
	}

	/*
	 * Let us further validate that we did indeed receive the
	 * extended version of 0x2c page.
	 */
	if (ptr[1] != 0x01) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_activate_explicit:"
		    "Expected page 2c subpage 1 but GOT subpage %x !!!",
		    ptr[1]));
		return (1);
	}

	/*
	 * The RDAC mode options field is at offset 36 and 37
	 * decimal. At offset 36 is the MSB byte of the RDAC
	 * option and mean the following in the returned
	 * Mode Sense data :
	 * 0x00 ==> No Alternate Controller Present
	 * 0x01 ==> Alternate Controller Present
	 * 0x02 to 0xff ==>Reserved.
	 *
	 * Now lets validate the LSB part of the RDAC mode
	 * at byte offset 37(decimal-counting from 0) in the received
	 * mode sense data.
	 * The LSB byte of the RDAC mode has following info -
	 * 0x00 ==> Reserved.
	 * 0x01 ==> The controller is operating in a single controller
	 *		environment.
	 * 0x02 ==> The controller is operating in dual active
	 *	    mode - meaning that both the controllers are
	 *	    active but the LUNs are distributed between the
	 *	    two controllers and for any given lun configuration
	 *	    ONLY 1 controller will be primary while the
	 *	    other will be secondary.
	 * 0x03 to 0x0ff ==> reserved.
	 *
	 * We do not need to modify or validate these bytes as they
	 * reserved fields and are don't cares for mode select command.
	 *
	 */

	/*
	 * This info is obtained from the lsb byte of the
	 * alternate RDAC mode byte(byte 39 decimal-counting from 0) of
	 * the mode sense value
	 * 0x00 ==> Reserved
	 * 0x01 ==> controller is operating in single controller environment
	 * 0x02 ==> Controller is operating in dual active mode
	 * 0x03 ==> Reserved
	 * 0x04 ==> The Alternate controller is being held in reset(failed)
	 *	    by the active controller.
	 * 0x05 to 0xff ==> Reserved.
	 * No need to validate these details.
	 */

	/*
	 * set the page code to be 0x2c and make sure the PS bit is
	 * set to 0 and SPF bits is set to 1. The ORing of 0x40
	 * sets the SPF bit below.
	 */
	ptr[0] = (LSI_MODE_PAGE_2C | 0x40);
	/*
	 * Now lets force the failover to occur by setting the RDAC
	 * lsb byte to a value of LSI_RDAC_DUAL_ACTIVE_MODE.
	 */
	ptr[37] = (unsigned char)LSI_RDAC_DUAL_ACTIVE_MODE;

	/*
	 * Quiescence setting - Set it to not wait for IOs
	 * to complete.
	 * Byte 40 is the quiescence timeout value. This field
	 * works in conjunction with the RDAC options byte - byte
	 * 41. In the RDAC options byte, bit 1(zero based) field
	 * indicates if the controller should be sensitive to the
	 * currently active IOs while initiating lun ownership
	 * of the controller.
	 * If the bit 1 is set, the controller does NOT wait for
	 * IOs to complete or the other hand, if the bit 1 is not
	 * then it puts the array in 'disbaled forced quiescence'
	 * mode. That is, it waits for the IOs to end for changing
	 * the lun ownership.
	 */
	ptr[40] = 0x05; /* set 5 secs quiescence timeout */
	ptr[41] = 0x02; /* Enable Forced Quiescence */

	/*
	 * Let us get the lun number that this failover is referring to
	 * using the inquire page c8
	 */
	lun = 0;
	get_lun_num = 1;
	cdip = sd->sd_dev;
	j = mdi_select_path(cdip, NULL, (MDI_SELECT_ONLINE_PATH |
	    MDI_SELECT_STANDBY_PATH), NULL, &pip);

	if ((j == MDI_SUCCESS) && (pip != NULL)) {
		/*
		 * Let us get the LUN information
		 */
		j = mdi_prop_lookup_int(pip, "lun", &lun);
		if (j == DDI_PROP_SUCCESS) {
			get_lun_num = 0;
		}
		mdi_rele_path(pip);
	}

	/*
	 * If for some reason we failed to get internal lun number,
	 * we can get it from the target. This typically should
	 * not happen.
	 */
	if (get_lun_num) {
		bzero(c8_buf, LSI_C8_INQ_BUF_SIZE);
		if (lsi_get_inquiry_page_cx(sd, LSI_INQ_PAGE_C8,
		    c8_buf, LSI_C8_INQ_BUF_SIZE)) {
			/*
			 * Failed to fetch c8 page
			 */
			return (1);
		}

		/*
		 * in the received page c8 data, the lun number
		 * array is located from byte offset 167 to 174
		 * (decimal, counting from 0).
		 * The actual lun number is in byte 1 of the
		 * 8 byte array which is at the offset 174(decimal).
		 */
		lun = c8_buf[174];
	}
	/*
	 * let us ONLY failover the lun for which the
	 * failover was initiated. The LSI Controller LUN table
	 * information starts at the offset of 42(0 based) and goes
	 * upto 297(inclusive) - which provides support fpr 255 luns
	 * in SCSI CMD mode 10 format.
	 */
	j = lun + 42;
	/*
	 * last lun information is available at offset 297
	 */
	if (j < 298) {
		/*
		 * Now for the lun# of interest, checkout if
		 * the other controller is said to be the owner.
		 * if so, set the value to cause the RDAC ownership
		 * to change.
		 */
		if (ptr[j] == LSI_OTHER_CONTROLLER_OWNED) {
			ptr[j] = (unsigned char) LSI_RDAC_CLAIM_OWNERSHIP;
		} else {
			/*
			 * The controller currently owns the lun...
			 * no message to send...just say failover
			 * completed.
			 */
			VHCI_DEBUG(4, (CE_NOTE, NULL,
			    "lsi_activate_explicit: "
			    "lun(%d) Already owned by the"
			    "controller\n", lun));
			return (0);
		}
	} else {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "lsi_activate_explicit: "
		    "lun#(%d) exceeds the supported 255 luns!!"
		    "in scsi command mode10", lun));
		return (1);
	}

	/*
	 * Let us extract the length information of the returned
	 * by the sense data in the mode Parameter header field -
	 * available in s_data[0] & s_data[1].
	 * Need to account for the mode data length field(2 bytes)
	 * as this is NOT accounted in the reported length. We
	 * are adding 2 bytes because we are using Mode Sense(10).
	 */

	i = ((s_data[0] << 8) | (s_data[1] & 0xff));

	i += 2; /* account for mode length field */

	/*
	 * Now that we have already extracted the length
	 * value in 'i', let us set the length fields
	 * 0 as for mode select, s_data[0] & s_data[1]
	 * fields are reserved... hence initializing
	 * them to 0.
	 */
	s_data[0] = 0;
	s_data[1] = 0;
	k = lsi_send_mode_select_data(sd, s_data, i);
	switch (k) {

		case LSI_DEVICE_COMMAND_SUCCESS:
			break;

		case LSI_DEVICE_COMMAND_RETRY:
			select_retry_cnt++;
			if (select_retry_cnt < LSI_MAX_MODE_CMD_RETRIES) {
				VHCI_DEBUG(4, (CE_WARN, NULL,
				    "lsi_activate_explicit: Requesting retry"
				    "of MODE SELECT command. retry count %d",
				    select_retry_cnt));
				/*
				 * sleep for 1 second and try again
				 */
				drv_usecwait(LSI_FO_CMD_RETRY_DELAY);
				goto do_retry;
			}
			VHCI_DEBUG(4, (CE_WARN, NULL,
			    "lsi_activate_explicit: EXCEEDS Max select cmd "
			    "retry count %d", select_retry_cnt));
			/* FALL THRU */

		default:
			VHCI_DEBUG(4, (CE_WARN, NULL,
			    "lsi_activate_explicit: Failure to send "
			    "MODE SELECT command %p", (void *)sd));
			return (1);
	}

	for (i = 0; i < LSI_DEVICE_READY_RETRIES; i++) {
		if (lsi_is_target_ready(sd)) {
			break;
		} else {
			/*
			 * sleep for a second and try again
			 */
			drv_usecwait(LSI_FO_CMD_RETRY_DELAY);
		}
	}

	if (i == LSI_DEVICE_READY_RETRIES) {
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "lsi_activate_explicit: "
		    "Target still not ready after failover\n"));
		return (1);
	}
	return (0);
}

/*
 * Framework Routine to activate a path
 */
/* ARGSUSED */
static int
lsi_path_activate(struct scsi_device *sd, char *pathclass,
void *ctpriv)
{
	struct buf		*bp;
	struct scsi_pkt		*pkt;
	struct scsi_address	*ap;
	int			err, retry_cnt, retry_cmd_cnt;
	int			mode, ownership, retval, xlf;
	uint8_t			*sns, skey;

	ap = &sd->sd_address;

	mode = ownership = 0;

	if (lsi_get_fo_mode(sd, &mode, &ownership, &xlf)) {
		VHCI_DEBUG(4, (CE_NOTE, NULL, "!unable to fetch fo "
		    "mode: sd(%p)\n", (void *) sd));
		return (1);
	}
	if (ownership == 1) {
		VHCI_DEBUG(4, (CE_NOTE, NULL, "!path already active for %p\n",
		    (void *)sd));
		return (0);
	}

	if (mode != SCSI_IMPLICIT_FAILOVER) {
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "!mode is EXPLICIT for %p xlf %x\n",
		    (void *)sd, xlf));
		retval = lsi_activate_explicit(sd, xlf);
		if (retval != 0) {
			VHCI_DEBUG(1, (CE_WARN, NULL,
			    "lsi_path_activation: Failed (%p)",
			    (void *)sd));
			return (1);
		}
	} else {
		VHCI_DEBUG(4, (CE_NOTE, NULL, "LSI mode is IMPLICIT for %p\n",
		    (void *)sd));
	}
	bp = scsi_alloc_consistent_buf(ap, (struct buf *)NULL, DEV_BSIZE,
	    B_READ, NULL, NULL);
	if (!bp) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "!(sd:%p)lsi_path_activate failed to alloc buffer",
		    (void *)sd));
		return (1);
	}

	pkt = scsi_init_pkt(ap, NULL, bp, CDB_GROUP1,
	    sizeof (struct scsi_arq_status), 0, PKT_CONSISTENT, NULL, NULL);
	if (!pkt) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "!(sd:%p)lsi_path_activate failed to initialize packet",
		    (void *)sd));
		scsi_free_consistent_buf(bp);
		return (1);
	}

	(void) scsi_setup_cdb((union scsi_cdb *)(uintptr_t)pkt->pkt_cdbp,
	    SCMD_READ, 1, 1, 0);
	pkt->pkt_time = 3*30;
	pkt->pkt_flags |= FLAG_NOINTR;

	retry_cnt = 0;
	retry_cmd_cnt = 0;
	retval = 1;

retry:
	err = scsi_transport(pkt);
	if (err != TRAN_ACCEPT) {

		/*
		 * Retry TRAN_BUSY till LSI_FO_MAX_RETRIES is exhausted.
		 * All other errors are fatal and should not be retried.
		 */

		if ((err == TRAN_BUSY) &&
		    (retry_cnt++ < LSI_FO_MAX_RETRIES)) {
			drv_usecwait(LSI_FO_RETRY_DELAY);
			goto retry;
		}

		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "!(sd:%p)LSI:"
		    " lsi_path_activate: Transport"
		    " Busy...giving up",
		    (void *)sd));
		goto failure_exit;
	}

	switch (pkt->pkt_reason) {

		case CMD_TIMEOUT:
			VHCI_DEBUG(4, (CE_WARN, NULL,
			    "!(sd:%p)LSI:"
			    " lsi_path_activate: cmd"
			    "Cmd timeout...giving up",
			    (void *)sd));
			goto failure_exit;

		case CMD_CMPLT:

			/*
			 * Re-initialize retry_cmd_cnt. Allow transport and
			 * cmd errors to go through a full retry count when
			 * these are encountered.  This way TRAN/CMD errors
			 * retry count is not exhausted due to CMD_CMPLTs
			 * delay for a LSI fo to finish. This allows the system
			 * to brave a hick-up on the link at any given time,
			 * while waiting for the fo to complete.
			 */

			if (pkt->pkt_state & STATE_ARQ_DONE) {
				sns = (uint8_t *)
				    &(((struct scsi_arq_status *)(uintptr_t)
				    (pkt->pkt_scbp))->sts_sensedata);
				skey = scsi_sense_key(sns);
				if (skey == KEY_UNIT_ATTENTION) {
					/*
					 * swallow unit attention
					 */
					drv_usecwait(LSI_FO_RETRY_DELAY);
					goto retry;
				} else if (skey == KEY_NOT_READY) {
					if (retry_cnt++ >=
					    LSI_FO_MAX_RETRIES) {
						goto failure_exit;
					}
					VHCI_DEBUG(6, (CE_NOTE, NULL,
					    "!(sd:%p)lun "
					    "becoming active...\n",
					    (void *)sd));
					drv_usecwait(LSI_FO_RETRY_DELAY);
					goto retry;
				}
				goto failure_exit;
			}

			switch (SCBP_C(pkt)) {

				case STATUS_GOOD:
					break;

				case STATUS_CHECK:
					VHCI_DEBUG(4, (CE_WARN, NULL,
					    "!(sd:%p)LSI:"
					    " cont allegiance during lsi "
					    "activation", (void *)sd));
					goto failure_exit;

				case STATUS_QFULL:
					if (retry_cmd_cnt <
					    LSI_FO_MAX_CMD_RETRIES) {
						drv_usecwait(5000);
						goto retry;
					}
					VHCI_DEBUG(4, (CE_WARN, NULL,
					    "!(sd:%p)LSI: pkt Status "
					    "Qfull. Giving up on path"
					    " Activation", (void *)sd));
					goto failure_exit;

				case STATUS_BUSY:
					retry_cmd_cnt++;
					if (retry_cmd_cnt <
					    LSI_FO_MAX_CMD_RETRIES) {
						drv_usecwait(5000);
						goto retry;
					}
					VHCI_DEBUG(4, (CE_WARN, NULL,
					    "!(sd:%p)LSI: pkt Status "
					    "BUSY. Giving up on path"
					    " Activation", (void *)sd));
					goto failure_exit;

				default:
					VHCI_DEBUG(4, (CE_WARN, NULL,
					    "!(sd:%p) Bad status "
					    "returned during lsi2882 "
					    "activation (pkt %p, status %x)",
					    (void *)sd, (void *)pkt,
					    SCBP_C(pkt)));
					goto failure_exit;

			}
			break;

		case CMD_INCOMPLETE:
		case CMD_RESET:
		case CMD_ABORTED:
		case CMD_TRAN_ERR:

			/*
			 * Increased the number of retries when these error
			 * cases are encountered.  Also added a 1 sec wait
			 * before retrying.
			 */

			if (retry_cmd_cnt++ < LSI_FO_MAX_CMD_RETRIES) {
				drv_usecwait(LSI_FO_CMD_RETRY_DELAY);
				VHCI_DEBUG(4, (CE_WARN, NULL,
				    "!Retrying LSI path activation due to "
				    "pkt reason:%x, retry cnt:%d",
				    pkt->pkt_reason, retry_cmd_cnt));
				goto retry;
			}

			/* FALLTHROUGH */

		default:
			goto failure_exit;
	}
	retval = 0;
	VHCI_DEBUG(4, (CE_NOTE, NULL, "!LSI path activation success\n"));
failure_exit:
	scsi_destroy_pkt(pkt);
	scsi_free_consistent_buf(bp);
	return (retval);
}
/*
 * Framework Routine to de-activate a path
 * exists for framework completeness...not required).
 */
/* ARGSUSED */
static int lsi_path_deactivate(struct scsi_device *sd, char *pathclass,
void *ctpriv)
{
	return (0);
}

/*
 * Framework Routine to get path operation info
 */
/* ARGSUSED */
static int
lsi_path_get_opinfo(struct scsi_device *sd, struct scsi_path_opinfo
*opinfo, void *ctpriv)
{
	int			mode, ownership, xlf;
	unsigned char		inq_c9_buf[0xff];

	if (lsi_get_inquiry_page_cx(sd, LSI_INQ_PAGE_C9, inq_c9_buf, 0xff)) {
		VHCI_DEBUG(1, (CE_WARN, NULL,
		    "lsi_path_get_opinfo: "
		    "Unable to get inquiry Page c9"));
		return (1);
	}

	/*
	 * Lower 4 bits of C9 page data contains primary and secondary
	 * path information. The following are the values for the
	 * lsb 4 bits of the byte 9(0 based indexing):
	 * 0000  ===> reserved
	 * 0001 ===> Primary path
	 * 0010 ===> Secondary path
	 * 0011 to 1111 ====> reserved.
	 */

	switch ((inq_c9_buf[9] & 0x0f)) {

		case 1:
			/*
			 * Primary path
			 */
			(void) strcpy(opinfo->opinfo_path_attr, "primary");
			break;

		case 2:
			/*
			 * Secondary path
			 */
			(void) strcpy(opinfo->opinfo_path_attr, "secondary");
			break;

		default:
			VHCI_DEBUG(4, (CE_WARN, NULL,
			    "lsi_path_get_opinfo:Invalid/unknown mode "
			    "settings(%x)", (inq_c9_buf[9] & 0x0f)));
			return (1);
	}

	if (lsi_get_fo_mode(sd, &mode, &ownership, &xlf)) {
		VHCI_DEBUG(4, (CE_WARN, NULL, "!lsi_path_get_opinfo:"
		    "Fetching FO mode failed"));
		return (1);
	}

	if (ownership == 1) {
		opinfo->opinfo_path_state = SCSI_PATH_ACTIVE;
	} else {
		opinfo->opinfo_path_state = SCSI_PATH_INACTIVE;
	}
	opinfo->opinfo_xlf_capable = xlf;

	opinfo->opinfo_rev = OPINFO_REV;
	opinfo->opinfo_pswtch_best = 30;
	opinfo->opinfo_pswtch_worst = 3*30;
	opinfo->opinfo_mode = (uint16_t)mode;
	opinfo->opinfo_preferred = 1;

	return (0);
}

/*
 * Framework Routine to ping path
 */
/* ARGSUSED */
static int lsi_path_ping(struct scsi_device *sd, void *ctpriv)
{
	/*
	 * For future use
	 */
	return (1);
}

/*
 * Typical target device specific sense code parsing and mapping
 * routine.
 */
/* ARGSUSED */
static int
lsi_analyze_sense(struct scsi_device *sd, uint8_t *sense,
void *ctpriv)
{
	uint8_t skey, asc, ascq;

	skey = scsi_sense_key(sense);
	asc = scsi_sense_asc(sense);
	ascq = scsi_sense_ascq(sense);

	/*
	 * Check if a an externally initiated failover(from another host
	 * for example) has been initiated. If so, the controller
	 * ownership of the lun would have changed. If so, return
	 * SCSI_SENSE_INACTIVE to signal the calling routine to
	 * perform the appropriate action associated with the
	 * failover. The values for this are sense key(5),
	 * ASC (94), and ASCQ(01) or ASCQ(02).
	 */
	if ((skey == LSI_INVALID_LU_OWNER_SKEY) &&
	    (asc == LSI_INVALID_LU_OWNER_ASC) &&
	    ((ascq == LSI_INVALID_LU_OWNER_ASCQ) ||
	    (ascq == LSI_INVALID_SECONDLU_OWNER_ASCQ))) {
		return (SCSI_SENSE_INACTIVE);
	}

	if ((skey == LSI_LUN_GETTING_READY_SKEY) &&
	    (asc == LSI_LUN_GETTING_READY_ASC) &&
	    (ascq == LSI_LUN_GETTING_READY_ASCQ)) {
		return (SCSI_SENSE_NOT_READY);
	}

	/*
	 * The skey(6), ASC(8b) and ASCQ(02) gets returned
	 * under various situations when the controller
	 * is busy, or failing over or is held in quiescence
	 * condition etc. Since we are unable to exactly
	 * determine the cause, we do not check for this
	 * sense key values and resort to returning
	 * SCSI_SENSE_UNKNOWN.
	 */

	/*
	 * At this point sense data may be for power-on-reset UNIT ATTN
	 * or hardware errors, vendor unqiue sense data etc.  For all
	 * these cases return SCSI_SENSE_UNKNOWN.
	 */
	VHCI_DEBUG(4, (CE_NOTE, NULL, "!LSI analyze sense UNKNOWN:"
	    " sense key:%x, ASC: %x, ASCQ:%x\n", skey, asc, ascq));
	return (SCSI_SENSE_UNKNOWN);
}

/*
 * Local utility routine for fetching the next path class
 *
 */
/* ARGSUSED */
static int
lsi_pathclass_next(char *cur, char **nxt, void *ctpriv)
{

	if (cur == NULL) {
		*nxt = PCLASS_PRIMARY;
		return (0);
	} else if (strcmp(cur, PCLASS_PRIMARY) == 0) {
		*nxt = PCLASS_SECONDARY;
		return (0);
	} else if (strcmp(cur, PCLASS_SECONDARY) == 0) {
		VHCI_DEBUG(6, (CE_NOTE, NULL,
		    "lsi_pathclass_next: returning ENOENT\n"));
		return (ENOENT);
	} else {
		VHCI_DEBUG(6, (CE_NOTE, NULL,
		    "lsi_pathclass_next: returning EINVAL\n"));
		return (EINVAL);
	}
}
/*
 * Local utility routine for sending a scsi command to the
 * array and perform retries ONLY when necessary. Currently
 * used to send ONLY mode select and mode sense commands
 *
 */
/* ARGSUSED */
static int
lsi_do_scsi_cmd(struct scsi_pkt *pkt)
{
	int	err = 0;
	int	retry_cnt = 0;

retry:
	err = scsi_poll(pkt);
	if (err) {
		if ((pkt->pkt_reason == CMD_CMPLT) &&
		    (SCBP_C(pkt) == STATUS_CHECK) &&
		    (pkt->pkt_state & STATE_ARQ_DONE)) {
			return (LSI_SCSI_CMD_CHECK_SENSE);
		}
		return (LSI_SCSI_CMD_FAILED);
	}

	switch (pkt->pkt_reason) {
		case CMD_TIMEOUT:
			VHCI_DEBUG(1, (CE_WARN, NULL, "!pkt timed "
			    "out (pkt %p)", (void *)pkt));
			return (LSI_SCSI_CMD_FAILED);

		case CMD_CMPLT:
			switch (SCBP_C(pkt)) {
				case STATUS_GOOD:
					break;

				case STATUS_CHECK:
					if (pkt->pkt_state & STATE_ARQ_DONE) {
						return (
						    LSI_SCSI_CMD_CHECK_SENSE);
					}
					/* Fall thru */

				default:
					VHCI_DEBUG(1, (CE_WARN, NULL,
					    "!Bad status returned "
					    "(pkt %p, status %x)",
					    (void *)pkt, SCBP_C(pkt)));
					return (LSI_SCSI_CMD_FAILED);
			}
			break;

		case CMD_INCOMPLETE:
		case CMD_RESET:
		case CMD_ABORTED:
		case CMD_TRAN_ERR:
			if (retry_cnt++ < LSI_MAX_CMD_RETRIES) {
				VHCI_DEBUG(1, (CE_WARN, NULL,
				    "!lsi_do_scsi_cmd(2): retry packet cmd:"
				    " %x/%x/%x pkt:%p %s",
				    pkt->pkt_cdbp[0], pkt->pkt_cdbp[1],
				    pkt->pkt_cdbp[2], (void *)pkt,
				    scsi_rname(pkt->pkt_reason)));
				goto retry;
			}
			/* FALLTHROUGH */
		default:
			VHCI_DEBUG(1, (CE_WARN, NULL, "!pkt did not "
			    "complete successfully (pkt %p,"
			    "reason %x)", (void *)pkt, pkt->pkt_reason));
			return (LSI_SCSI_CMD_FAILED);
	}
	return (LSI_SCSI_CMD_SUCCESS);
}

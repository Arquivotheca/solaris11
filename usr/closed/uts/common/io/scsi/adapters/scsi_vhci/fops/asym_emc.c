/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file contains confidential information of EMC Corporation
 * and should not be distributed in source form without approval
 * from Sun Legal.
 */

/*
 * Implementation of "scsi_vhci_f_asym_emc" asymmetric failover_ops.
 */

#include <sys/conf.h>
#include <sys/file.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/scsi/scsi.h>
#include <sys/scsi/generic/sense.h>
#include <sys/scsi/adapters/scsi_vhci.h>

/* Supported device table entries.  */
char *emc_dev_table[] = {
/*	"                  111111" */
/*	"012345670123456789012345" */
/*	"|-VID--||-----PID------|" */

	"DGC     DISK",
	"DGC     RAID",
	NULL
};

/* module plumbing */
SCSI_FAILOVER_OP("f_asym_emc", emc);

#define	EMC_FO_CMD_RETRY_DELAY	1000000 /* 1 second  */
#define	EMC_FO_RETRY_DELAY	2000000 /* 2 seconds */
/*
 * Max time for failover to complete is unspecified, use the same
 * 3 minute value as other devices. Compute number of retries accordingly,
 * to ensure we wait for at least 3 minutes
 */
#define	EMC_FO_MAX_RETRIES	(3*60*1000000)/EMC_FO_RETRY_DELAY

/*
 * max number of retries for Clariion controller failover
 * to complete where the ping command is failing due to
 * transport errors or commands being rejected.
 */
#define	EMC_FO_MAX_CMD_RETRIES		3
#define	EMC_MAX_CMD_RETRIES		3
#define	EMC_MAX_MODE_CMD_RETRIES	60
#define	EMC_DEVICE_READY_RETRIES	3

#define	EMC_INQ_PAGE_0			0x00
#define	EMC_INQ_PAGE_C0			0xc0
#define	EMC_MODE_PAGE_TRESPASS		0x22
#define	EMC_C0_INQ_BUF_SIZE		0xff
#define	EMC_MAX_INQ_BUF_SIZE		0xff

#define	EMC_VPDC0_NO_ATF_MASK		0x80
#define	EMC_INTFOPT_FOMODE_MASK		0x0f
#define	EMC_INTFOPT_LUNZ_MASK		0x04

#define	EMC_PATH_INACTIVE		0
#define	EMC_PATH_ACTIVE			1

/*
 * Private Function return values
 */
#define	EMC_DEVICE_COMMAND_SUCCESS	0
#define	EMC_DEVICE_COMMAND_FAILED	1
#define	EMC_DEVICE_COMMAND_RETRY	2

#define	EMC_SCSI_CMD_SUCCESS		0
#define	EMC_SCSI_CMD_FAILED		1
#define	EMC_SCSI_CMD_CHECK_SENSE	2

/*
 * ASC, ASCQ values used by CLARiiON
 */
#define	EMC_LUN_NOT_READY_ASC		0x04
#define	EMC_LUN_NOT_READY_CNR_ASCQ	0x00 /* Cause not reportable */
#define	EMC_LUN_NOT_READY_MIR_ASCQ	0x03 /* Manual intervention required */
#define	EMC_LUN_NOT_SUPP_ASC		0x25
#define	EMC_LUN_NOT_SUPP_CA_ASCQ	0x01 /* Copy active */

static unsigned char   emc_short_trespass_cmd[] = {
	0x00, 0x00, 0x00, 0x00,			/* Mode Parameter header */
	0x22, 0x02, 0x01, 0xff			/* Mode page data */
};

static unsigned char   emc_long_trespass_cmd[] = {
	0x00, 0x00, 0x00, 0x00,			/* Mode Parameter header */
	0x22, 0x09, 0x01, 0xff, 0xff,		/* Mode page data */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00	/* Mode page data */
};


static int emc_get_fo_mode(struct scsi_device *sd,
		int *mode, int *state, int *xlf_capable, int *preferred);
static int emc_analyze_mode_cmd_sense_info(uint8_t *sns, char *c_str);
static int emc_do_scsi_cmd(struct scsi_pkt *pkt);
static int emc_send_scsi_cmd(struct scsi_pkt *pkt, char *c_str);
static int emc_validate_device_config(struct scsi_device *sd);
static int emc_try_active_path(struct scsi_device *sd);

/*
 * Framework Routine to probe the target device.
 * Returns 1, SFO_DEVICE_PROBE_VHCI if device was detected
 * Returns 0, SFO_DEVICE_PROBE_PHCI if probe failed.
 */
/* ARGSUSED */
static int
emc_device_probe(struct scsi_device *sd, struct scsi_inquiry *stdinq,
    void **ctpriv)
{
	char	**dt;
	int	xlf = 0, mode = 0, state = 0, preferred = 0;
	int	retval = SFO_DEVICE_PROBE_PHCI;

	VHCI_DEBUG(6, (CE_NOTE, NULL, "emc_device_probe: vidpid %s\n",
	    stdinq->inq_vid));

	for (dt = emc_dev_table; *dt; dt++) {
		if (strncmp(stdinq->inq_vid, *dt, strlen(*dt)))
			continue;

		/* match */
		if (emc_validate_device_config(sd) != 0) {
			VHCI_DEBUG(4, (CE_WARN, NULL,
			    "emc_device_probe:"
			    "Unsupported device config"));
			retval = SFO_DEVICE_PROBE_PHCI;
		} else if (emc_get_fo_mode(sd, &mode, &state, &xlf,
		    &preferred)) {
			VHCI_DEBUG(4, (CE_WARN, NULL,
			    "emc_device_probe:"
			    "Fetching FO mode failed"));
			retval = SFO_DEVICE_PROBE_PHCI;
		} else if (mode != SCSI_EXPLICIT_FAILOVER) {
			retval = SFO_DEVICE_PROBE_PHCI;
		} else {
			retval = SFO_DEVICE_PROBE_VHCI;
		}
		break;
	}
	return (retval);
}

/*
 * Framework Routine to unprobe the target device.
 */
/* ARGSUSED */
static void
emc_device_unprobe(struct scsi_device *sd, void *ctpriv)
{
	/*
	 * For future use
	 */
}

/*
 * Local routine to get inquiry VPD page from the device.
 *
 * return 1 for failure
 * return 0 for success
 */
static int
emc_get_inquiry_vpd_page(struct scsi_device *sd, unsigned char page,
    unsigned char *buf, int size)
{
	int		retval = 0;
	struct buf	*bp;
	struct scsi_pkt	*pkt;
	struct scsi_address	*ap;
	int		retry_cnt = 0;
	char		str[32];

	if ((buf == NULL) || (size == 0)) {
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
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "emc_get_inquiry_vpd_page:"
		    "Failed to initialize packet"));
		freerbuf(bp);
		return (1);
	}

	/*
	 * Send the inquiry command for page xx to the target.
	 * Data is returned in the buf pointed to by buf.
	 */

	pkt->pkt_cdbp[0] = SCMD_INQUIRY;
	pkt->pkt_cdbp[1] = 0x1;
	pkt->pkt_cdbp[2] = page;
	pkt->pkt_cdbp[4] = (unsigned char)size;
	pkt->pkt_time = 90;
	(void) snprintf(str, sizeof (str), "emc_get_inquiry_page_%x", page);

	do {
		retval = emc_send_scsi_cmd(pkt, str);
	} while ((retval == EMC_DEVICE_COMMAND_RETRY) &&
	    (++retry_cnt < EMC_MAX_MODE_CMD_RETRIES));

	if (retval == EMC_DEVICE_COMMAND_SUCCESS) {
		if ((buf[1] & 0xff) != (unsigned int)page) {
			VHCI_DEBUG(4, (CE_WARN, NULL,
			    "emc_get_inquiry_vpd_page: "
			    "Returned page is NOT %x !..got %x",
			    page, (buf[1] & 0xff)));
			retval = 1;
		} else {
			retval = 0;
		}
	} else {
		/* Failed to send command */
		if (retval == EMC_DEVICE_COMMAND_RETRY) {
			VHCI_DEBUG(1, (CE_NOTE, NULL,
			    "%s: Retry attempts failed."
			    " Giving up\n", str));
		}
		retval = 1;
	}
	scsi_destroy_pkt(pkt);
	freerbuf(bp);
	return (retval);
}

/*
 * Get Inquiry VPD page 0,
 *   verify page 0xc0 is listed
 * Get Inquiry VPD page 0xc0
 *   verify page revision is 0
 *   verify effective initiator type is 3
 *   verify failovermode setting
 *   verify arraycommpath setting
 *
 * return 1 for failure
 * return 0 for success
 */
static int
emc_validate_device_config(struct scsi_device *sd)
{
	unsigned char	inq_vpd_buf[EMC_MAX_INQ_BUF_SIZE];
	unsigned char	*sup_page_p;
	int		page_length;
	int		i;

	if (emc_get_inquiry_vpd_page(sd, EMC_INQ_PAGE_0, inq_vpd_buf,
	    sizeof (inq_vpd_buf))) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "emc_validate_device_config: sd(%p):Unable to "
		    "get inquiry Page 00", (void*)sd));
		return (1);
	}
	page_length = inq_vpd_buf[3];
	sup_page_p = &inq_vpd_buf[4];
	for (i = 0; i < page_length; i++, sup_page_p++) {
		if (*sup_page_p == EMC_INQ_PAGE_C0) {
			break;
		}
	}
	if (i == page_length) {
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "emc_device_probe:"
		    "Page %x not in supported page list\n", EMC_INQ_PAGE_C0));
		return (1);
	}
	if (emc_get_inquiry_vpd_page(sd, EMC_INQ_PAGE_C0, inq_vpd_buf,
	    sizeof (inq_vpd_buf))) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "emc_validate_device_config: sd(%p):Unable to "
		    "get inquiry Page c0", (void*)sd));
		return (1);
	}
	if (inq_vpd_buf[9] != 0x00) {
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "emc_device_probe:"
		    "Page C0 page revision is: %x, should be 0\n",
		    inq_vpd_buf[9]));
		return (1);
	}
	if (inq_vpd_buf[27] != 0x03) {
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "emc_device_probe:"
		    "Effective initiator type is: %x, should be 3\n",
		    inq_vpd_buf[27]));
		return (1);
	}
	if ((inq_vpd_buf[28] & EMC_INTFOPT_FOMODE_MASK) != 0x04) {
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "emc_device_probe:"
		    "Failover mode is: %x, should be 4\n",
		    (inq_vpd_buf[28] & EMC_INTFOPT_FOMODE_MASK)));
		return (1);
	}
	if ((inq_vpd_buf[30] & EMC_INTFOPT_LUNZ_MASK) != 0x04) {
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "emc_device_probe:"
		    "Arraycommpath is: %x, should be 4\n",
		    (inq_vpd_buf[30] & EMC_INTFOPT_LUNZ_MASK)));
		return (1);
	}
	return (0);
}
/*
 * Local Routine to fetch failover mode from the target.
 * return 1 for failure
 * return 0 for success
 */
/* ARGSUSED */
static int
emc_get_fo_mode(struct scsi_device *sd, int *mode,
    int *state, int *xlf_capable, int *preferred)
{
	unsigned char inq_c0_buf[EMC_C0_INQ_BUF_SIZE];

	if (emc_get_inquiry_vpd_page(sd, EMC_INQ_PAGE_C0, inq_c0_buf,
	    sizeof (inq_c0_buf))) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "emc_get_fo_mode: sd(%p):Unable to "
		    "get inquiry Page c0", (void*)sd));
		return (1);
	}
	/*
	 * Read VPD page 0xc0. check LUN operation and LUN state fields.
	 *   state: if LUN operation == 1 SP is rebooting.
	 *   result: this path is inactive.
	 *
	 *   state: if LUN state == 0 LUN is not bound
	 *   result: this path is inactive.
	 *
	 *   state: if LUN state == 1 LUN is bound but not owned by this SP
	 *   result: this path is inactive.
	 *
	 *   state: if LUN state == 2 LUN is bound and owned by this SP
	 *   result: this path is active.
	 *
	 *   state: if the Inquiry cmd failed.
	 *   result: this path is inactive.
	 */
	if (inq_c0_buf[48] == 1) {
		*state = EMC_PATH_INACTIVE;
	} else if (inq_c0_buf[4] == 2) {
		*state = EMC_PATH_ACTIVE;
	} else {
		*state = EMC_PATH_INACTIVE;
	}
	/*
	 * Check if this path is preferred.
	 * The default owner field tells which SP is the owner,
	 * and the SP ID field indicates which SP this command
	 * was received through. If they are equal then this
	 * is the owner/preferred path.
	 */

	*preferred = (inq_c0_buf[8] == inq_c0_buf[5])? 1: 0;

	*mode =  SCSI_EXPLICIT_FAILOVER;
	/*
	 * No support for xlf capability in clariion.
	 * Framework uses this field, hence setting it to 0.
	 */
	*xlf_capable = 0;
	return (0);
}

/*
 * Local Routine to send Mode select data to the target
 * Returns	EMC_DEVICE_COMMAND_SUCCESS or
 *		EMC_DEVICE_COMMAND_FAILED or
 *		EMC_DEVICE_COMMAND_RETRY
 */
/* ARGSUSED */
static int
emc_send_mode_select_data(struct scsi_device *sd, unsigned char *buf,
    int size, int cmdlen)
{
	struct scsi_address	*ap;
	struct buf		*bp;
	struct scsi_pkt		*pkt;
	int			retval;

	if (buf == NULL || size == 0) {
		return (EMC_DEVICE_COMMAND_FAILED);
	}

	ap = &sd->sd_address;
	bp = getrbuf(KM_NOSLEEP);
	if (bp == NULL) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "emc_send_mode_select_data:"
		    "Failed to allocate buffer"));
		return (EMC_DEVICE_COMMAND_FAILED);
	}
	bp->b_un.b_addr = (char *)buf;
	bp->b_flags = B_WRITE;
	bp->b_bcount = size;
	bp->b_resid = 0;
	pkt = scsi_init_pkt(ap, NULL, bp, cmdlen,
	    sizeof (struct scsi_arq_status), 0, 0, NULL, NULL);
	if (pkt == NULL) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "emc_send_mode_select_data:"
		    "Failed to initialize packet"));
		freerbuf(bp);
		return (EMC_DEVICE_COMMAND_FAILED);
	}

	if (cmdlen == CDB_GROUP1) {
		pkt->pkt_cdbp[0] = SCMD_MODE_SELECT_G1;
		pkt->pkt_cdbp[4] = 0;
		pkt->pkt_cdbp[6] = 0;
		/* Parameter list len */
		pkt->pkt_cdbp[7] = (uchar_t)((size & 0xff00) >>8);
		pkt->pkt_cdbp[8] = (uchar_t)(size & 0xff);
	} else {
		pkt->pkt_cdbp[0] = SCMD_MODE_SELECT;
		ASSERT(size <= 0xff);
		pkt->pkt_cdbp[4] = (uchar_t)(size & 0xff);
	}
	pkt->pkt_cdbp[1] = 0;
	pkt->pkt_cdbp[2] = 0;
	pkt->pkt_cdbp[3] = 0;
	pkt->pkt_cdbp[5] = 0;

	pkt->pkt_time = 60;
	retval = emc_send_scsi_cmd(pkt, "emc_send_mode_select_data");
	scsi_destroy_pkt(pkt);
	freerbuf(bp);
	return (retval);
}



/*
 * Local Routine to send Scsi Command target
 * The routine in scsi_vhci.c - vhci_do_scsi_cmd() is not
 * being used is because of its processing of status
 * and internal retries. This is not desirable in situations
 * where immediate retries are known to be ineffective.
 * This routine instead returns appropriate status and lets
 * the caller retry.
 *
 * Returns	EMC_DEVICE_COMMAND_SUCCESS or
 *		EMC_DEVICE_COMMAND_FAILED or
 *		EMC_DEVICE_COMMAND_RETRY
 */
/* ARGSUSED */
static int
emc_send_scsi_cmd(struct scsi_pkt *pkt, char *c_str)
{
	int retval;
	uint8_t	*sns;

	retval = emc_do_scsi_cmd(pkt);
	switch (retval) {

		case EMC_SCSI_CMD_SUCCESS:
			retval = EMC_DEVICE_COMMAND_SUCCESS;
			break;

		case EMC_SCSI_CMD_CHECK_SENSE:

			sns = (uint8_t *)
			    &(((struct scsi_arq_status *)(uintptr_t)
			    (pkt->pkt_scbp))->sts_sensedata);

			retval = emc_analyze_mode_cmd_sense_info(sns, c_str);

			if (retval == EMC_DEVICE_COMMAND_RETRY) {
				break;
			}
			/* Fall thru */

		default:
			/* Failed to send */
			retval = EMC_DEVICE_COMMAND_FAILED;
			break;
	}
	return (retval);
}

/*
 * Local Routine to handle sense code info that
 * gets returned on an internally generated Inquiry
 * or Mode select command.
 * Returns:
 *	EMC_DEVICE_COMMAND_RETRY
 *	EMC_DEVICE_COMMAND_FAILED
 */
/* ARGSUSED */
static int
emc_analyze_mode_cmd_sense_info(uint8_t *sns, char *c_str)
{
	uint8_t skey, asc, ascq;

	skey = scsi_sense_key(sns);
	asc = scsi_sense_asc(sns);
	ascq = scsi_sense_ascq(sns);

	if ((skey == KEY_ILLEGAL_REQUEST) &&
	    (asc == EMC_LUN_NOT_READY_ASC) &&
	    (ascq == EMC_LUN_NOT_READY_CNR_ASCQ)) {
		/*
		 * trespass sent to owning path during copy,
		 * retry.
		 */
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "%s: Cmd in process - via another path? "
		    "retry MODE command. Got Sense Code"
		    "Key %x; ASC %x ; ASCQ %x\n", c_str, skey,
		    asc, ascq));
		return (EMC_DEVICE_COMMAND_RETRY);
	}

	VHCI_DEBUG(1, (CE_NOTE, NULL,
	    "%s: Unhandled/recognized Sense Code. "
	    "Key %x; ASC %x ; ASCQ %x\n", c_str, skey,
	    asc, ascq));
	return (EMC_DEVICE_COMMAND_FAILED);
}

/*
 * Local Routine to try sending a command down a newly activated path
 * return 1 for failure
 * return 0 for success
 */
/* ARGSUSED */
static int
emc_try_active_path(struct scsi_device *sd)
{
	struct buf		*bp;
	struct scsi_pkt		*pkt;
	struct scsi_address	*ap;
	int			err, retry_cnt, retry_cmd_cnt;
	int			retval;
	int			retry;

	VHCI_DEBUG(6, (CE_NOTE, NULL, "!emc_try_active_path: entry\n"));

	ap = &sd->sd_address;
	bp = scsi_alloc_consistent_buf(ap, (struct buf *)NULL, DEV_BSIZE,
	    B_READ, NULL, NULL);
	if (!bp) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "!(sd:%p)emc_try_active_path failed to alloc buffer",
		    (void *)sd));
		return (1);
	}

	pkt = scsi_init_pkt(ap, NULL, bp, CDB_GROUP1,
	    sizeof (struct scsi_arq_status), 0, PKT_CONSISTENT, NULL, NULL);
	if (!pkt) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "!(sd:%p)emc_try_active_path failed to initialize packet",
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

	do {
		retry = 0;
		for (; retry_cnt < EMC_FO_MAX_RETRIES; retry_cnt++) {
			VHCI_DEBUG(7, (CE_NOTE, NULL,
			    "!emc_try_active_path: sending cmd\n"));
			err = scsi_transport(pkt);
			if (err == TRAN_BUSY) {
				drv_usecwait(EMC_FO_RETRY_DELAY);
			} else {
				break;
			}
		}
		if (err != TRAN_ACCEPT) {
			VHCI_DEBUG(4, (CE_WARN, NULL,
			    "!(sd:%p)EMC:"
			    " emc_try_active_path: Transport"
			    " Busy...giving up",
			    (void *)sd));
			break;
		}

		switch (pkt->pkt_reason) {

		case CMD_TIMEOUT:
			VHCI_DEBUG(4, (CE_WARN, NULL,
			    "!(sd:%p)EMC:"
			    " emc_try_active_path: cmd"
			    "Cmd timeout...giving up",
			    (void *)sd));
			break;

		case CMD_CMPLT:

			/*
			 * Re-initialize retry_cmd_cnt. Allow transport
			 * and cmd errors to go through a full retry
			 * count when these are encountered. This way
			 * TRAN/CMD errors retry count is not exhausted
			 * due to CMD_CMPLTs delay for a fo to
			 * finish. This allows the system to brave a
			 * hick-up on the link at any given time,
			 * while waiting for the fo to complete.
			 */

			if (pkt->pkt_state & STATE_ARQ_DONE) {
				uint8_t *sns;

				sns = (uint8_t *)
				    &(((struct scsi_arq_status *)(uintptr_t)
				    (pkt->pkt_scbp))->sts_sensedata);
				if ((emc_analyze_sense(sd, sns, NULL) !=
				    SCSI_SENSE_INACTIVE) &&
				    (retry_cnt++ < EMC_FO_MAX_RETRIES)) {
					VHCI_DEBUG(6, (CE_NOTE, NULL,
					    "!(sd:%p)lun "
					    "becoming active...\n",
					    (void *)sd));
					drv_usecwait(EMC_FO_RETRY_DELAY);
					retry = 1;
				}
				break;
			}

			switch (SCBP_C(pkt)) {

			case STATUS_GOOD:
				VHCI_DEBUG(4, (CE_NOTE, NULL,
				    "!EMC path activation success\n"));
				retval = 0;
				break;

			case STATUS_CHECK:
				VHCI_DEBUG(4, (CE_WARN, NULL,
				    "!(sd:%p)EMC:"
				    " cont allegiance during "
				    "activation", (void *)sd));
				break;

			case STATUS_QFULL:
				if (retry_cmd_cnt < EMC_FO_MAX_CMD_RETRIES) {
					drv_usecwait(5000);
					retry = 1;
				} else {
					VHCI_DEBUG(4, (CE_WARN, NULL,
					    "!(sd:%p)EMC: pkt Status "
					    "Qfull. Giving up on path"
					    " Activation", (void *)sd));
				}
				break;

			case STATUS_BUSY:
				retry_cmd_cnt++;
				if (retry_cmd_cnt < EMC_FO_MAX_CMD_RETRIES) {
					drv_usecwait(5000);
					retry = 1;
				} else {
					VHCI_DEBUG(4, (CE_WARN, NULL,
					    "!(sd:%p)EMC: pkt Status "
					    "BUSY. Giving up on path"
					    " Activation", (void *)sd));
				}
				break;

			default:
				VHCI_DEBUG(4, (CE_WARN, NULL,
				    "!(sd:%p)EMC: Bad status "
				    "returned during "
				    "activation (pkt %p, status %x)",
				    (void *)sd, (void *)pkt,
				    SCBP_C(pkt)));
				break;

			}
			break;

		case CMD_INCOMPLETE:
		case CMD_RESET:
		case CMD_ABORTED:
		case CMD_TRAN_ERR:

			/*
			 * Increased the number of retries when these
			 * error cases are encountered.  Also added a
			 * 1 sec wait before retrying.
			 */

			if (retry_cmd_cnt++ < EMC_FO_MAX_CMD_RETRIES) {
				drv_usecwait(EMC_FO_CMD_RETRY_DELAY);
				VHCI_DEBUG(4, (CE_WARN, NULL,
				    "!Retrying EMC path activation due to"
				    " pkt reason:%x, retry cnt:%d",
				    pkt->pkt_reason, retry_cmd_cnt));
				retry = 1;
			}
			break;

		default:
			break;
		}
	} while (retry != 0);

	scsi_destroy_pkt(pkt);
	scsi_free_consistent_buf(bp);

	VHCI_DEBUG(7, (CE_NOTE, NULL,
	    "!emc_try_active_path: on exit "
	    "retry_cnt: %d, retry_cmd_cnt: %d, err: %d\n",
	    retry_cnt, retry_cmd_cnt, err));

	VHCI_DEBUG(6, (CE_NOTE, NULL,
	    "!emc_try_active_path: exit with %d\n", retval));

	return (retval);
}
/*
 * Local Routine to explicitly activate the target
 * return 1 for failure
 * return 0 for success
 */
/* ARGSUSED */
static int
emc_activate_explicit(struct scsi_device *sd, int xlf_capable)
{

	unsigned char		*ptr;
	int			cmd_size;
	int			k;
	int			select_retry_cnt = 0;
	unsigned char		inq_c0_buf[EMC_C0_INQ_BUF_SIZE];

	if (emc_get_inquiry_vpd_page(sd, EMC_INQ_PAGE_C0, inq_c0_buf,
	    sizeof (inq_c0_buf))) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "emc_activate_explicit: sd(%p):Unable to "
		    "get inquiry Page c0", (void*)sd));
		return (1);
	}
	if ((inq_c0_buf[6] & EMC_VPDC0_NO_ATF_MASK) == 0) {
		VHCI_DEBUG(8, (CE_NOTE, NULL,
		    "emc_activate_explicit:"
		    "Using short trespass_cmd\n"));
		ptr = emc_short_trespass_cmd;
		cmd_size = sizeof (emc_short_trespass_cmd);
	} else {
		VHCI_DEBUG(8, (CE_NOTE, NULL,
		    "emc_activate_explicit:"
		    "Using long trespass_cmd\n"));
		ptr = emc_long_trespass_cmd;
		cmd_size = sizeof (emc_long_trespass_cmd);
	}

	do {
		k = emc_send_mode_select_data(sd, ptr, cmd_size, CDB_GROUP0);
		if (k == EMC_DEVICE_COMMAND_RETRY) {
			if (++select_retry_cnt < EMC_MAX_MODE_CMD_RETRIES) {
				drv_usecwait(EMC_FO_CMD_RETRY_DELAY);
			} else {
				VHCI_DEBUG(4, (CE_WARN, NULL,
				    "emc_activate_explicit: "
				    "EXCEEDS Max select cmd "
				    "retry count %d", select_retry_cnt));
				k = EMC_DEVICE_COMMAND_FAILED;
			}
		}
	} while (k == EMC_DEVICE_COMMAND_RETRY);

	if (k != EMC_DEVICE_COMMAND_SUCCESS) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "emc_activate_explicit: Failure to send "
		    "MODE SELECT Trespass command %p", (void *)sd));
		return (1);
	}
	return (0);
}

/*
 * Framework Routine to activate a path
 * return 1 for failure
 * return 0 for success
 */
/* ARGSUSED */
static int
emc_path_activate(struct scsi_device *sd, char *pathclass, void *ctpriv)
{
	int	mode, state, xlf, preferred;
	int	retval;

	if (emc_get_fo_mode(sd, &mode, &state, &xlf, &preferred)) {
		VHCI_DEBUG(4, (CE_WARN, NULL, "!unable to fetch fo "
		    "mode: sd(%p)", (void *) sd));
		return (1);
	}
	if (state == EMC_PATH_ACTIVE) {
		VHCI_DEBUG(4, (CE_NOTE, NULL, "!path already active for %p\n",
		    (void *)sd));
		return (0);
	}
	if (mode != SCSI_EXPLICIT_FAILOVER) {
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "!mode is not EXPLICIT for %p xlf %x\n",
		    (void *)sd, xlf));
		return (1);
	}
	retval = emc_activate_explicit(sd, xlf);
	if (retval == 0) {
		retval = emc_try_active_path(sd);
	}
	if (retval != 0) {
		VHCI_DEBUG(1, (CE_WARN, NULL,
		    "emc_path_activation: Failed (%p)",
		    (void *)sd));
	} else {
		VHCI_DEBUG(4, (CE_NOTE, NULL,
		    "!EMC path activation success\n"));
	}
	return (retval);
}
/*
 * Framework Routine to de-activate a path
 * exists for framework completeness...not required).
 */
/* ARGSUSED */
static int
emc_path_deactivate(struct scsi_device *sd, char *pathclass, void *ctpriv)
{
	return (0);
}

/*
 * Framework Routine to get path operation info
 */
/* ARGSUSED */
static int
emc_path_get_opinfo(struct scsi_device *sd, struct scsi_path_opinfo *opinfo,
    void *ctpriv)
{
	int	mode, state, xlf, preferred;

	if (emc_get_fo_mode(sd, &mode, &state, &xlf, &preferred)) {
		VHCI_DEBUG(4, (CE_WARN, NULL, "!emc_path_get_opinfo:"
		    "Fetching FO mode failed"));
		return (1);
	}

	if (preferred) {
		(void) strcpy(opinfo->opinfo_path_attr, PCLASS_PRIMARY);
	} else {
		(void) strcpy(opinfo->opinfo_path_attr, PCLASS_SECONDARY);
	}

	if (state == EMC_PATH_ACTIVE) {
		opinfo->opinfo_path_state = SCSI_PATH_ACTIVE;
	} else {
		opinfo->opinfo_path_state = SCSI_PATH_INACTIVE;
	}
	VHCI_DEBUG(4, (CE_NOTE, NULL, "emc_path_get_opinfo: "
	    "class: %s state: %s\n",
	    opinfo->opinfo_path_attr,
	    opinfo->opinfo_path_state == SCSI_PATH_ACTIVE ?
	    "ACTIVE" : "INACTIVE"));

	opinfo->opinfo_xlf_capable = xlf;

	opinfo->opinfo_rev = OPINFO_REV;
	opinfo->opinfo_pswtch_best = 30;
	opinfo->opinfo_pswtch_worst = 3*30;
	opinfo->opinfo_mode = (uint16_t)mode;
	opinfo->opinfo_preferred = (uint16_t)preferred;

	return (0);
}

/*
 * Framework Routine to ping path
 * return 1 for success
 * return 0 for failure
 */
/* ARGSUSED */
static int
emc_path_ping(struct scsi_device *sd, void *ctpriv)
{
	unsigned char	inq_c0_buf[EMC_C0_INQ_BUF_SIZE];

	VHCI_DEBUG(6, (CE_NOTE, NULL, "!emc_path_ping: entry\n"));

	if (emc_get_inquiry_vpd_page(sd, EMC_INQ_PAGE_C0, inq_c0_buf,
	    sizeof (inq_c0_buf))) {
		VHCI_DEBUG(4, (CE_WARN, NULL,
		    "emc_path_ping: sd(%p):Unable to "
		    "get inquiry Page c0", (void*)sd));
		return (0);
	}

	VHCI_DEBUG(6, (CE_NOTE, NULL,
	    "!emc_path_ping: exit with success\n"));

	return (1);
}

/*
 * Typical target device specific sense code parsing and mapping
 * routine.
 */
/* ARGSUSED */
static int
emc_analyze_sense(struct scsi_device *sd, uint8_t *sense,
    void *ctpriv)
{
	uint8_t skey, asc, ascq;

	skey = scsi_sense_key(sense);
	asc = scsi_sense_asc(sense);
	ascq = scsi_sense_ascq(sense);

	/*
	 * Check for cmd sent to non-owning path or array s/w
	 * being upgraded.
	 * This path is no longer active.
	 */
	if ((skey == KEY_NOT_READY) &&
	    (asc == EMC_LUN_NOT_READY_ASC) &&
	    (ascq == EMC_LUN_NOT_READY_MIR_ASCQ)) {
		return (SCSI_SENSE_INACTIVE);
	}

	/*
	 * Check for I/O command sent to owning path during a copy operation
	 * The path is still active, retry the command.
	 */
	if ((skey == KEY_ILLEGAL_REQUEST) &&
	    (asc == EMC_LUN_NOT_SUPP_ASC) &&
	    (ascq == EMC_LUN_NOT_SUPP_CA_ASCQ)) {
		return (SCSI_SENSE_UNKNOWN);
	}
	/*
	 * Trespass sent to owning path during a copy operation or
	 * The path is still active, retry the command.
	 * Trespass sent to a MirrorView copy LUN, retry.
	 */
	if ((skey == KEY_ILLEGAL_REQUEST) &&
	    (asc == EMC_LUN_NOT_READY_ASC) &&
	    (ascq == EMC_LUN_NOT_READY_CNR_ASCQ)) {
		return (SCSI_SENSE_UNKNOWN);
	}

	/*
	 * At this point sense data may be for power-on-reset UNIT ATTN
	 * or hardware errors, vendor unique sense data etc.  For all
	 * these cases return SCSI_SENSE_UNKNOWN.
	 */
	VHCI_DEBUG(4, (CE_NOTE, NULL, "!EMC analyze sense UNKNOWN:"
	    " sense key:%x, ASC: %x, ASCQ:%x\n", skey, asc, ascq));
	return (SCSI_SENSE_UNKNOWN);
}

/*
 * Local utility routine for fetching the next path class
 *
 */
/* ARGSUSED */
static int
emc_pathclass_next(char *cur, char **nxt, void *ctpriv)
{

	if (cur == NULL) {
		*nxt = PCLASS_PRIMARY;
		return (0);
	} else if (strcmp(cur, PCLASS_PRIMARY) == 0) {
		*nxt = PCLASS_SECONDARY;
		return (0);
	} else if (strcmp(cur, PCLASS_SECONDARY) == 0) {
		VHCI_DEBUG(6, (CE_NOTE, NULL,
		    "emc_pathclass_next: returning ENOENT\n"));
		return (ENOENT);
	} else {
		VHCI_DEBUG(6, (CE_NOTE, NULL,
		    "emc_pathclass_next: returning EINVAL\n"));
		return (EINVAL);
	}
}
/*
 * Local utility routine for sending a scsi command to the
 * array and perform retries ONLY when necessary. Currently
 * used to send ONLY mode select and inquiry commands
 *
 */
/* ARGSUSED */
static int
emc_do_scsi_cmd(struct scsi_pkt *pkt)
{
	int	err = 0;
	int	retry_cnt = 0;

retry:
	err = scsi_poll(pkt);
	if (err) {
		if ((pkt->pkt_reason == CMD_CMPLT) &&
		    (SCBP_C(pkt) == STATUS_CHECK) &&
		    (pkt->pkt_state & STATE_ARQ_DONE)) {
			return (EMC_SCSI_CMD_CHECK_SENSE);
		}
		return (EMC_SCSI_CMD_FAILED);
	}

	switch (pkt->pkt_reason) {
		case CMD_TIMEOUT:
			VHCI_DEBUG(1, (CE_WARN, NULL, "!pkt timed "
			    "out (pkt %p)", (void *)pkt));
			return (EMC_SCSI_CMD_FAILED);

		case CMD_CMPLT:
			switch (SCBP_C(pkt)) {
				case STATUS_GOOD:
					break;

				case STATUS_CHECK:
					if (pkt->pkt_state & STATE_ARQ_DONE) {
						return (
						    EMC_SCSI_CMD_CHECK_SENSE);
					}
					/* Fall thru */

				default:
					VHCI_DEBUG(1, (CE_WARN, NULL,
					    "!Bad status returned "
					    "(pkt %p, status %x)",
					    (void *)pkt, SCBP_C(pkt)));
					return (EMC_SCSI_CMD_FAILED);
			}
			break;

		case CMD_INCOMPLETE:
		case CMD_RESET:
		case CMD_ABORTED:
		case CMD_TRAN_ERR:
			if (retry_cnt++ < EMC_MAX_CMD_RETRIES) {
				VHCI_DEBUG(1, (CE_WARN, NULL,
				    "!emc_do_scsi_cmd(2): retry packet cmd:"
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
			return (EMC_SCSI_CMD_FAILED);
	}
	return (EMC_SCSI_CMD_SUCCESS);
}

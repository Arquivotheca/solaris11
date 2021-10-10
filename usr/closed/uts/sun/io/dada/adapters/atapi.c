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
 * Copyright (c) 1996, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 */

#include <sys/types.h>
#include <sys/scsi/scsi.h>
#include <sys/dada/adapters/ata_common.h>
#include <sys/dada/adapters/atapi.h>


/*
 * External Functions.
 */
extern void
make_prd(gcmd_t *gcmdp, ddi_dma_cookie_t *cookie, int single_seg, int num_segs);
extern void
write_prd(struct ata_controller *ata_ctlp, struct ata_pkt *ata_pktp);
extern int prd_init(struct ata_controller *ata_ctlp, int chno);
extern void change_endian(unsigned char *string, int length);
extern void ata_ghd_complete_wraper(struct ata_controller *ata_ctlp,
			struct ata_pkt *ata_pktp, int chno);
extern int ata_dmaget(dev_info_t   *dip, struct ata_pkt  *ata_pktp,
	struct buf *bp, int dma_flags, int (*callback)(), caddr_t arg);
extern void ata_dmafree(struct ata_pkt  *ata_pktp);

/*
 * External Data.
 */
extern ddi_dma_attr_t ata_dma_attrs;

/*
 * SCSA entry points
 */
static int atapi_tran_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
	scsi_hba_tran_t *hba_tran, struct scsi_device *sd);
static int atapi_tran_tgt_probe(struct scsi_device *sd, int (*callback)());
static void atapi_tran_tgt_free(dev_info_t *hba_dip, dev_info_t *tgt_dip,
	scsi_hba_tran_t *hba_tran, struct scsi_device *sd);
static int atapi_tran_abort(struct scsi_address *ap, struct scsi_pkt *spktp);
static int atapi_tran_reset(struct scsi_address *ap, int level);
static int atapi_tran_getcap(struct scsi_address *ap, char *capstr, int whom);
static int atapi_tran_setcap(struct scsi_address *ap, char *capstr,
	int value, int whom);
static struct scsi_pkt *atapi_tran_init_pkt(struct scsi_address *ap,
	struct scsi_pkt *spktp, struct buf *bp, int cmdlen, int statuslen,
	int tgtlen, int flags, int (*callback)(caddr_t), caddr_t arg);
static void atapi_tran_destroy_pkt(struct scsi_address *ap,
		struct scsi_pkt *spktp);
static void atapi_tran_dmafree(struct scsi_address *ap, struct scsi_pkt *spktp);
static void atapi_tran_sync_pkt(struct scsi_address *ap,
		struct scsi_pkt *spktp);
static int atapi_tran_start(struct scsi_address *ap, struct scsi_pkt *spktp);

/*
 * packet callbacks
 */
static int atapi_start(struct ata_controller *ata_ctlp,
			struct ata_pkt *ata_pktp);
static void atapi_complete(struct ata_pkt *ata_pktp, int do_callback);
static int atapi_intr(struct ata_controller *ata_ctlp,
			struct ata_pkt *ata_pktp);

/*
 * local functions
 */
static int atapi_send_cdb(struct ata_controller *ata_ctlp, int chno);

/*
 * Local static data
 */
static ushort_t ata_bit_bucket[ATAPI_SECTOR_SIZE >> 1];

/*
 * If set in /etc/system it will allow us to determine if the device
 * is violating ATA time standards.
 */

static int atapi_id_timewarn = 0;
static int32_t atapi_device_reset_waittime = 0;
static int32_t atapi_dev_rst_waittime_default = 3000000;
extern int ata_debug_attach;

/*
 * initialize atapi sub-system
 */
int
atapi_init(struct ata_controller *ata_ctlp)
{
	dev_info_t *dip = ata_ctlp->ac_dip;
	scsi_hba_tran_t *tran;
	int value = 1;

	ADBG_TRACE(("atapi_init entered\n"));

	(void) ddi_prop_update_int(DDI_DEV_T_NONE, dip, "atapi", value);

	/*
	 * allocate transport structure
	 */

	tran = scsi_hba_tran_alloc(dip, SCSI_HBA_CANSLEEP);

	if (tran == NULL) {
		ADBG_WARN(("atapi_init: scsi_hba_tran_alloc failed\n"));
		atapi_destroy(ata_ctlp);
		return (FAILURE);
	}

	ata_ctlp->ac_atapi_tran = tran;
	ata_ctlp->ac_flags |= AC_SCSI_HBA_TRAN_ALLOC;

	/*
	 * initialize transport structure
	 */
	tran->tran_hba_private = ata_ctlp;
	tran->tran_tgt_private = NULL;

	tran->tran_tgt_init = atapi_tran_tgt_init;
	tran->tran_tgt_probe = atapi_tran_tgt_probe;
	tran->tran_tgt_free = atapi_tran_tgt_free;
	tran->tran_start = atapi_tran_start;
	tran->tran_reset = atapi_tran_reset;
	tran->tran_abort = atapi_tran_abort;
	tran->tran_getcap = atapi_tran_getcap;
	tran->tran_setcap = atapi_tran_setcap;
	tran->tran_init_pkt = atapi_tran_init_pkt;
	tran->tran_destroy_pkt = atapi_tran_destroy_pkt;
	tran->tran_dmafree = atapi_tran_dmafree;
	tran->tran_sync_pkt = atapi_tran_sync_pkt;
	tran->tran_add_eventcall = NULL;
	tran->tran_bus_reset = NULL;
	tran->tran_get_bus_addr = NULL;
	tran->tran_get_eventcookie = NULL;
	tran->tran_get_name = NULL;
	tran->tran_post_event = NULL;
	tran->tran_quiesce = NULL;
	tran->tran_remove_eventcall = NULL;
	tran->tran_unquiesce = NULL;

	if (scsi_hba_attach_setup(ata_ctlp->ac_dip, &ata_dma_attrs, tran,
	    SCSI_HBA_TRAN_CLONE) != DDI_SUCCESS) {
		ADBG_WARN(("atapi_init: scsi_hba_attach failed\n"));
		atapi_destroy(ata_ctlp);
		return (FAILURE);
	}

	ata_ctlp->ac_flags |= AC_SCSI_HBA_ATTACH;

	/*
	 * Look for atapi-device-reset-waittime property
	 * See /kernel/drv/uata.conf file for more details.
	 */

	atapi_device_reset_waittime = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "atapi-device-reset-waittime",
	    atapi_dev_rst_waittime_default);
	/*
	 * Check for the Min and Max values and clip it
	 * accordingly
	 */

	if (atapi_device_reset_waittime < 20)
		atapi_device_reset_waittime = 20;
	else if (atapi_device_reset_waittime > atapi_dev_rst_waittime_default)
		atapi_device_reset_waittime = atapi_dev_rst_waittime_default;

	/*
	 * Save the value into softstate structure
	 */
	ata_ctlp->ac_atapi_dev_rst_waittime = atapi_device_reset_waittime;

	return (SUCCESS);
}


/*
 * destroy the atapi sub-system
 */
void
atapi_destroy(struct ata_controller *ata_ctlp)
{
	ADBG_TRACE(("atapi_destroy entered\n"));

	if (ata_ctlp->ac_flags & AC_SCSI_HBA_ATTACH) {
		(void) scsi_hba_detach(ata_ctlp->ac_dip);
	}

	if (ata_ctlp->ac_flags & AC_SCSI_HBA_TRAN_ALLOC) {
		scsi_hba_tran_free(ata_ctlp->ac_atapi_tran);
	}
}

/*
 * initialize an atapi drive
 */
int
atapi_init_drive(struct ata_drive *ata_drvp)
{
	struct ata_controller *ata_ctlp = ata_drvp->ad_ctlp;
	int chno = ata_drvp->ad_channel;

	ADBG_TRACE(("atapi_init_drive entered\n"));

	/*
	 * Determine ATAPI CDB size
	 */

	switch (ata_drvp->ad_id.dcd_config & ATAPI_ID_CFG_PKT_SZ) {

		case ATAPI_ID_CFG_PKT_12B:
			ata_drvp->ad_cdb_len = 12;
			break;
		case ATAPI_ID_CFG_PKT_16B:
			ata_drvp->ad_cdb_len = 16;
			break;
		default:
			ADBG_WARN(("atapi_init_drive: bad pkt size support\n"));
			return (FAILURE);
	}

	/*
	 * determine if drive gives
	 * an intr when it wants the CDB
	 */

	if ((ata_drvp->ad_id.dcd_config & ATAPI_ID_CFG_DRQ_TYPE) !=
	    ATAPI_ID_CFG_DRQ_INTR) {
		ata_drvp->ad_flags |= AD_NO_CDB_INTR;
	}


	if (!ata_ctlp->ata_prd_acc_handle[chno])
		return (prd_init(ata_ctlp, chno));

	return (SUCCESS);
}

/*
 * destroy an atapi drive
 */
/* ARGSUSED */
void
atapi_destroy_drive(struct ata_drive *ata_drvp)
{
	ADBG_TRACE(("atapi_destroy_drive entered\n"));

}

/*
 * ATAPI Identify Device command
 */
int
atapi_id(ddi_acc_handle_t handle, uint8_t *ioaddr, ushort_t *buf)
{
	int i;

	ADBG_TRACE(("atapi_id entered\n"));

	if (ata_wait(handle, ioaddr + AT_STATUS, 0, ATS_BSY, IGN_ERR,
	    100, 2) == FAILURE) {
		/*
		 * Will not issue the command in case the BSY is high
		 */
		return (FAILURE);
	}

	ddi_put8(handle, ioaddr + AT_CMD, ATC_PI_ID_DEV);
	/*
	 * As per the atapi specs, we need to wait for 200ms after ATAPI IDENT
	 * command has been issued to the drive. Originally the wait time in
	 * the code was 100ms. There is a bug with the LGE Goldstar 32X cdrom
	 * drives as a result of which they do not respond within 200ms and
	 * time taken to respond can be in tune of seconds too. As a workaround
	 * to that am providing longer wait period in code so the drive gets
	 * recognized.
	 */
	if ((i = ata_wait(handle, ioaddr + AT_STATUS, ATS_DRQ, ATS_BSY,
	    IGN_ERR, 10, 2000000)) == FAILURE) {
		return (FAILURE);
	}

	if (atapi_id_timewarn && (i >= 20000)) {
		cmn_err(CE_WARN, "Atapi specification violation:\
		    \n Response time to ATAPI IDENTIFY command was \
		    greater than 200ms\n");
	}

	ddi_rep_get16(handle, buf, (uint16_t *)(ioaddr + AT_DATA),
	    NBPSCTR >> 1, 0);
	change_endian((uchar_t *)buf, NBPSCTR);

	/*
	 * wait for the drive to recognize I've read all the data.  some
	 * drives have been observed to take as much as 3msec to finish
	 * sending the data; allow 5 msec just in case.
	 */

	if (ata_wait(handle, ioaddr + AT_STATUS, ATS_DRDY,
	    ATS_BSY | ATS_DRQ, IGN_ERR, 10, 500) == FAILURE) {
		ADBG_WARN(("atapi_id: no DRDY\n"));
		return (FAILURE);
	}

	/*
	 * check for error
	 */

	if (ddi_get8(handle, ioaddr + AT_STATUS) & ATS_ERR) {
		ADBG_WARN(("atapi_id: ERROR status\n"));
		return (FAILURE);
	}
	return (SUCCESS);
}

/*
 * Look for atapi signature
 */
int
atapi_signature(ddi_acc_handle_t handle, uint8_t *ioaddr)
{
	ADBG_TRACE(("atapi_signature entered\n"));

	if ((ddi_get8(handle, ioaddr + AT_HCYL) == ATAPI_SIG_HI) &&
	    (ddi_get8(handle, ioaddr + AT_LCYL) == ATAPI_SIG_LO)) {
		return (SUCCESS);
	}

	return (FAILURE);
}

/*
 * reset an atapi device
 */
int
atapi_reset_drive(struct ata_drive *ata_drvp)
{
	struct ata_controller *ata_ctlp = ata_drvp->ad_ctlp;
	struct ata_pkt *ata_pktp;
	int chno = ata_drvp->ad_channel;
	int target = ata_drvp->ad_targ;
	int i;

	/*
	 * XXX - what should we do when controller is busy
	 * with I/O for another device?  For now, do nothing.
	 */

	ata_pktp = ata_ctlp->ac_active[chno];

	if ((ata_pktp != NULL) && (APKT2DRV(ata_pktp) != ata_drvp)) {
		return (FAILURE);
	}

	/*
	 * Might want to reset the IDE channel on the chip internally before
	 * device reset to the atapi device. Call the chip specific routine
	 * to do so.
	 * This has been added as a workaround for a bug in Southbridge
	 * controller. The machine (sunblade-100) used to hard hang if
	 * the dma is incomplete & we just reset the drive. So for SB need
	 * to reset the chip also.
	 */

	if (ata_pktp != NULL) {
		ata_ctlp->reset_chip(ata_ctlp, chno);
	}

	/*
	 * Stop the DMA, wait for bit 1 to clear and then issue atapi
	 * soft reset
	 */
	ddi_put8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno),
	    (ddi_get8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno))
	    & 0xfe));
	i = ata_wait(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno),
	    0, 0x1, IGN_ERR, 10,
	    (ata_ctlp->ac_atapi_dev_rst_waittime) / 10);
	if (i == FAILURE) {
		cmn_err(CE_WARN, "ATAPI drv, target: %d, "
		    "dma stop failed but continuing\n", target);
	}

	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_drvhd[chno], ata_drvp->ad_drive_bits);
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_cmd[chno], ATC_PI_SRESET);

	/*
	 *  wait for 2 microsecs before checking the reset status
	 */

	drv_usecwait(2);

	/*
	 * Wait for busy bit to get cleared. Min wait time is 20 micro seconds
	 * and Max wait time is 3 seconds
	 */

	i = ata_wait(ata_ctlp->ata_datap[chno], ata_ctlp->ac_status[chno],
	    0, ATS_BSY, IGN_ERR, 10,
	    (ata_ctlp->ac_atapi_dev_rst_waittime) / 10);

	if (i == FAILURE)
		cmn_err(CE_WARN, "ATAPI drv, target: %d, "
		    "reset failed but continuing\n", target);
	else if (ata_debug_attach) {
		cmn_err(CE_WARN, "ATAPI drv, target: %d, "
		    "reset took %d micro secs\n", target, (i * 10));
	}
	/*
	 * Clean up any active packet on this drive
	 */

	if (ata_pktp != NULL) {
		ata_ctlp->ac_active[chno] = NULL;
		ata_pktp->ap_flags |= AP_DEV_RESET;
		ata_ghd_complete_wraper(ata_ctlp, ata_pktp, chno);
	}


	return (SUCCESS);
}


/*
 * SCSA tran_tgt_init entry point
 */
/* ARGSUSED */
static int
atapi_tran_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
	scsi_hba_tran_t *hba_tran, struct scsi_device *sd)
{
	gtgt_t  *gtgtp;	/* GHD's per-target-instance structure */
	struct ata_controller *ata_ctlp;
	struct ata_drive *ata_drvp;
	struct scsi_address *ap;
	int rc = DDI_SUCCESS, chno;

	ADBG_TRACE(("atapi_tran_tgt_init entered\n"));

	/*
	 * Qualification of targ, lun, and ATAPI device presence
	 *  have already been taken care of by ata_bus_ctl
	 */

	/*
	 * store pointer to drive
	 * struct in cloned tran struct
	 */

	ata_ctlp = TRAN2CTL(hba_tran);
	ap = &sd->sd_address;

	chno = SADR2CHNO(ap);

	ata_drvp = CTL2DRV(ata_ctlp, ap->a_target, ap->a_lun);

	if (ata_drvp->ad_ref == 0) {
		gtgtp = ghd_target_init(hba_dip, tgt_dip,
		    &ata_ctlp->ac_ccc[chno], 0, ata_ctlp,
		    (uint32_t)ap->a_target, (uint32_t)ap->a_lun);

		hba_tran->tran_tgt_private = gtgtp;
		ata_drvp->ad_gtgtp = gtgtp;
		GTGTP2TARGET(gtgtp) = ata_drvp;
	}
	ata_drvp->ad_ref++;
	return (rc);
}

/*
 * SCSA tran_tgt_probe entry point
 */
static int
atapi_tran_tgt_probe(struct scsi_device *sd, int (*callback)())
{
	ADBG_TRACE(("atapi_tran_tgt_probe entered\n"));

	return (scsi_hba_probe(sd, callback));
}

/*
 * SCSA tran_tgt_free entry point
 */
/* ARGSUSED */
static void
atapi_tran_tgt_free(dev_info_t *hba_dip, dev_info_t *tgt_dip,
	scsi_hba_tran_t *hba_tran, struct scsi_device *sd)
{
	int chno;
	struct scsi_address *ap = &sd->sd_address;
	struct ata_controller *ata_ctlp = ADDR2CTLP(ap);
	struct ata_drive *ata_drvp =
	    CTL2DRV(ata_ctlp, ap->a_target, ap->a_lun);

	ADBG_TRACE(("atapi_tran_tgt_free entered\n"));

	ata_drvp->ad_ref--;
	if (ata_drvp->ad_ref == 0) {
		chno = SADR2CHNO(&sd->sd_address);

		ghd_target_free(hba_dip, tgt_dip,
		    &TRAN2ATAP(hba_tran)->ac_ccc[chno], ata_drvp->ad_gtgtp);
		hba_tran->tran_tgt_private = NULL;
	}
}

/*
 * SCSA tran_abort entry point
 */
/* ARGSUSED */
static int
atapi_tran_abort(struct scsi_address *ap, struct scsi_pkt *spktp)
{
	int chno;
	struct ata_controller *ata_ctlp = ADDR2CTLP(ap);
	struct ata_drive *ata_drvp =
	    CTL2DRV(ata_ctlp, ap->a_target, ap->a_lun);

	ADBG_TRACE(("atapi_tran_abort entered\n"));

	chno = SADR2CHNO(ap);

	if (spktp) {
		return (ghd_tran_abort(&ADDR2CTL(ap)->ac_ccc[chno],
		    PKTP2GCMDP(spktp), ata_drvp->ad_gtgtp, NULL));
	}

	return (ghd_tran_abort_lun(&ADDR2CTL(ap)->ac_ccc[chno],
	    ata_drvp->ad_gtgtp, NULL));
}

/*
 * SCSA tran_reset entry point
 */
/* ARGSUSED */
static int
atapi_tran_reset(struct scsi_address *ap, int level)
{
	int chno;
	struct ata_controller *ata_ctlp = ADDR2CTLP(ap);
	struct ata_drive *ata_drvp =
	    CTL2DRV(ata_ctlp, ap->a_target, ap->a_lun);

	ADBG_TRACE(("atapi_tran_reset entered\n"));

	chno = SADR2CHNO(ap);

	if (level == RESET_TARGET) {
		return (ghd_tran_reset_target(&ADDR2CTL(ap)->ac_ccc[chno],
		    ata_drvp->ad_gtgtp, NULL));
	}
	if (level == RESET_ALL) {
		return (ghd_tran_reset_bus(&ADDR2CTL(ap)->ac_ccc[chno],
		    ata_drvp->ad_gtgtp, NULL));
	}
	return (FALSE);

}

/*
 * SCSA tran_setcap entry point
 */
/* ARGSUSED */
static int
atapi_tran_setcap(struct scsi_address *ap, char *capstr, int value, int whom)
{
	ADBG_TRACE(("atapi_tran_setcap entered\n"));

	/* we have no settable capabilities */
	return (0);
}

/*
 * SCSA tran_getcap entry point
 */
/* ARGSUSED0 */
static int
atapi_tran_getcap(struct scsi_address *ap, char *capstr, int whom)
{
	int rval = -1;

	ADBG_TRACE(("atapi_tran_getcap entered\n"));

	if (capstr == NULL || whom == 0) {
		return (-1);
	}
	switch (scsi_hba_lookup_capstr(capstr)) {
		case SCSI_CAP_INITIATOR_ID:
			rval = 7;
			break;

		case SCSI_CAP_DMA_MAX:
			/* XXX - what should the real limit be?? */
			rval = 1 << 24;	/* limit to 16 megabytes */
			break;

		case SCSI_CAP_GEOMETRY:
			/* Default geometry */
			rval = ATAPI_HEADS << 16 | ATAPI_SECTORS_PER_TRK;
			break;
	}

	return (rval);
}


/*
 * SCSA tran_init_pkt entry point
 */
/* ARGSUSED6 */
static struct scsi_pkt *
atapi_tran_init_pkt(struct scsi_address *ap, struct scsi_pkt *spktp,
	struct buf *bp, int cmdlen, int statuslen, int tgtlen,
	int flags, int (*callback)(caddr_t), caddr_t arg)
{
	struct ata_controller *ata_ctlp = ADDR2CTL(ap);
	struct ata_pkt *ata_pktp;
	struct ata_drive *ata_drvp;
	gcmd_t *gcmdp;
	int bytes;

	ADBG_TRACE(("atapi_tran_init_pkt entered\n"));

	if (spktp == NULL) {
		spktp = scsi_hba_pkt_alloc(ata_ctlp->ac_dip, ap,
		    cmdlen, statuslen, tgtlen, sizeof (gcmd_t),
		    callback, arg);
		if (spktp == NULL) {
			return (NULL);
		}

		ata_pktp = (struct ata_pkt *)
		    kmem_zalloc(sizeof (struct ata_pkt),
		    (callback == SLEEP_FUNC)? KM_SLEEP: KM_NOSLEEP);

		if (ata_pktp == NULL) {
			scsi_hba_pkt_free(ap, spktp);
			return (NULL);
		}

		gcmdp = APKT2GCMD(ata_pktp);

		ata_drvp = CTL2DRV(ata_ctlp, ap->a_target, ap->a_lun);

		GHD_GCMD_INIT(gcmdp, (void *)ata_pktp, ata_drvp->ad_gtgtp);

		spktp->pkt_ha_private = (void *)gcmdp;
		gcmdp->cmd_pktp = (void *)spktp;

		/*
		 * save length of the SCSI CDB, and calculate CDB padding
		 * note that for convenience, padding is expressed in shorts.
		 */

		ata_pktp->ap_cdb_len = (uchar_t)cmdlen;
		ata_pktp->ap_cdb_pad =
		    ((unsigned)(ata_drvp->ad_cdb_len - cmdlen)) >> 1;

		/*
		 * set up callback functions
		 */

		ata_pktp->ap_start = atapi_start;
		ata_pktp->ap_intr = atapi_intr;
		ata_pktp->ap_complete = atapi_complete;

		/*
		 * set-up for start
		 */

		ata_pktp->ap_flags = AP_ATAPI;
		ata_pktp->ap_cmd = ATC_PI_PKT;
		ata_pktp->ap_hd = ata_drvp->ad_drive_bits;
		ata_pktp->ap_chno = ata_drvp->ad_channel;
		ata_pktp->ap_targ = ata_drvp->ad_targ;
		ata_pktp->ap_bytes_per_block = ata_drvp->ad_bytes_per_block;

		if ((bp) && (bp->b_bcount)) {
			if (ata_drvp->ad_cur_disk_mode == DMA_MODE) {
				int	dma_flags;
				/*
				 * Set up dma info if there's any data and
				 * if the device supports DMA.
				 */
				if (bp->b_flags & B_READ) {
					dma_flags = DDI_DMA_READ;
					ata_pktp->ap_flags |= AP_READ | AP_DMA;
				} else {
					dma_flags = DDI_DMA_WRITE;
					ata_pktp->ap_flags |= AP_WRITE | AP_DMA;
				}
				/*
				 * check dma option flags
				 */
				if (flags & PKT_CONSISTENT) {
					dma_flags |= DDI_DMA_CONSISTENT;
				}
				if (flags & PKT_DMA_PARTIAL) {
					dma_flags |= DDI_DMA_PARTIAL;
				}
				/*
				 * map the buffer
				 */
				if (ata_dmaget(ata_ctlp->ac_dip,
				    ata_pktp, bp, dma_flags,
				    callback, arg) == DDI_FAILURE) {
					return (NULL);
				}
			}
			spktp->pkt_resid = 0;
			APKT2GCMD(ata_pktp)->cmd_resid = bp->b_bcount;
			ata_pktp->ap_count_bytes = bp->b_bcount;
			ata_pktp->ap_v_addr = bp->b_un.b_addr;
			ata_pktp->ap_orig_addr = bp->b_un.b_addr;
		} else {
			spktp->pkt_resid =
			    APKT2GCMD(ata_pktp)->cmd_resid = 0;
			ata_pktp->ap_count_bytes = 0;
		}

		bytes = min(ata_pktp->ap_gcmd.cmd_resid,
		    ATAPI_MAX_BYTES_PER_DRQ);
		ata_pktp->ap_hicyl = (uchar_t)(bytes >> 8);
		ata_pktp->ap_lwcyl = (uchar_t)bytes;

		/*
		 * fill these with zeros
		 * for ATA/ATAPI-4 compatibility
		 */
		ata_pktp->ap_sec = 0;
		ata_pktp->ap_count = 0;
	} else {
		printf("atapi_tran_init_pkt called with pre allocated pkt\n");
	}

	return (spktp);
}

/*
 * SCSA tran_destroy_pkt entry point
 */
static void
atapi_tran_destroy_pkt(struct scsi_address *ap, struct scsi_pkt *spktp)
{
	struct ata_pkt *ata_pktp = SPKT2APKT(spktp);

	ADBG_TRACE(("atapi_tran_destroy_pkt entered\n"));

	if (ata_pktp->ap_flags & AP_DMA) {
		/*
		 * release the old DMA resources
		 */
		ata_dmafree(ata_pktp);
	}

	/*
	 * bp_mapout happens automagically from biodone
	 */

	if (SPKT2APKT(spktp)) {
		kmem_free((caddr_t)SPKT2APKT(spktp), sizeof (struct ata_pkt));
	}

	scsi_hba_pkt_free(ap, spktp);
}

/*ARGSUSED*/
static void
atapi_tran_dmafree(struct scsi_address *ap, struct scsi_pkt *spktp)
{
	ADBG_TRACE(("atapi_tran_dmafree entered\n"));
}

/* SCSA tran_sync_pkt entry point */

/*ARGSUSED*/
static void
atapi_tran_sync_pkt(struct scsi_address *ap, struct scsi_pkt *spktp)
{
	ADBG_TRACE(("atapi_tran_sync_pkt entered\n"));
}

/*
 * SCSA tran_start entry point
 */
/* ARGSUSED */
static int
atapi_tran_start(struct scsi_address *ap, struct scsi_pkt *spktp)
{
	struct ata_pkt *ata_pktp = SPKT2APKT(spktp);
	struct ata_drive *ata_drvp = APKT2DRV(ata_pktp);
	struct ata_controller *ata_ctlp = ata_drvp->ad_ctlp;
	int rc, polled = FALSE, chno, intr_status = 0;

	ADBG_TRACE(("atapi_tran_start entered\n"));

	if (ata_drvp->ad_invalid == 1) {
		return (TRAN_FATAL_ERROR);
	}

	/*
	 * Atapi driver can not handle odd count requests
	 */
	if (ata_pktp->ap_count_bytes % 2) {
		return (TRAN_BADPKT);
	}
	chno = SADR2CHNO(ap);

	/*
	 * basic initialization
	 */

	/*
	 * Clear the error bits by retaining only the following bits.
	 * This is required especially when the same
	 * pkt is retried by the target driver in the case of failures.
	 */

	ata_pktp->ap_flags &= (AP_READ | AP_WRITE | AP_DMA |
	    AP_POLL | AP_ATAPI_OVERLAP);

	ata_pktp->ap_flags |= AP_ATAPI;
	spktp->pkt_state = 0;
	spktp->pkt_statistics = 0;
	spktp->pkt_reason = 0;

	/*
	 * check for polling pkt
	 */

	if (spktp->pkt_flags & FLAG_NOINTR) {
		ata_pktp->ap_flags |= AP_POLL;
		mutex_enter(&ata_ctlp->ac_hba_mutex);
		ata_ctlp->ac_polled_count++;
		mutex_exit(&ata_ctlp->ac_hba_mutex);
		polled = TRUE;
	} else {
		ata_pktp->ap_flags &= ~AP_POLL;
	}


	/*
	 * driver cannot accept tagged commands
	 */

	if (spktp->pkt_flags & (FLAG_HTAG|FLAG_OTAG|FLAG_STAG)) {
		spktp->pkt_reason = CMD_TRAN_ERR;
		return (TRAN_BADPKT);
	}

	/*
	 * call common transport routine
	 */

	rc = ghd_transport(&ata_ctlp->ac_ccc[chno], APKT2GCMD(ata_pktp),
	    APKT2GCMD(ata_pktp)->cmd_gtgtp, spktp->pkt_time, polled,
	    (void *) &intr_status);

	/*
	 * see if pkt was not accepted
	 */

	if (rc == TRAN_BUSY) {
		return (TRAN_BUSY);
	}

	/*
	 * for polled pkt, set up return status
	 */

	if (polled) {
		atapi_complete(ata_pktp, FALSE);
	}

	return (rc);
}

/*
 * packet start callback routine
 */
static int
atapi_start(struct ata_controller *ata_ctlp, struct ata_pkt *ata_pktp)
{
	struct ata_drive *ata_drvp = APKT2DRV(ata_pktp);
	int chno = ata_drvp->ad_channel;

	ADBG_TRACE(("atapi_start entered\n"));
	ADBG_TRANSPORT(("atapi_start:\tpkt = 0x%p, pkt flags = 0x%x\n",
	    (void *)ata_pktp, ata_pktp->ap_flags));
	ADBG_TRANSPORT(("atapi_start:\tcnt = 0x%lx, addr = 0x%p\n",
	    ata_pktp->ap_gcmd.cmd_resid, (void *)ata_pktp->ap_v_addr));

	/*
	 * Reinitialize the gcmd.resid as sd allocs pkt at beginning and
	 * re-uses them for request sense where as the resid is decremented
	 * depending on what is read as data from the devices. So a new
	 * field is being introduced to take the count so that it can be
	 * restored here in this place to reset the gcmd.resid properly.
	 * All other feilds like ap_status, ap_error and ap_flags also need
	 * to be reset to take care of reusability of pkts.
	 */
	if (ata_drvp->ad_invalid == 1) {
		ata_pktp->ap_flags &= ~(AP_TIMEOUT|AP_ABORT|
		    AP_BUS_RESET|AP_ERROR);
		ata_pktp->ap_flags |= AP_FATAL;
		return (TRAN_FATAL_ERROR);
	}
	ata_pktp->ap_gcmd.cmd_resid = ata_pktp->ap_count_bytes;
	ata_pktp->ap_error = 0;
	ata_pktp->ap_status = 0;
	ata_pktp->ap_flags &= ~AP_ERROR;
	/*
	 * Need to set the ap_v_addr flag again to the original address.
	 * as received in init_pkt, since there can be a call to transport
	 * from higher layer re-using the original initialized packet.
	 */
	ata_pktp->ap_v_addr = ata_pktp->ap_orig_addr;

	/*
	 * check for busy before starting command.  This
	 * is important for laptops that do suspend/resume
	 */

	if (ata_wait(ata_ctlp->ata_datap1[chno], ata_ctlp->ac_altstatus[chno],
	    0, ATS_BSY, IGN_ERR, 10, 500000) == FAILURE) {
		ADBG_WARN(("atapi_start: BSY too long!\n"));
		ata_pktp->ap_flags |= AP_ERROR;
		ata_pktp->ap_status =
		    ddi_get8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_status[chno]);
		ata_pktp->ap_error =
		    ddi_get8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_error[chno]);
		return (TRAN_FATAL_ERROR);
	}
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_drvhd[chno],
	    ata_pktp->ap_hd);
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_lcyl[chno],
	    ata_pktp->ap_lwcyl);
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_hcyl[chno],
	    ata_pktp->ap_hicyl);
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_sect[chno],
	    ata_pktp->ap_sec);
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_count[chno],
	    ata_pktp->ap_count);

	/*
	 * enable or disable interrupts for command
	 */

	if (ata_pktp->ap_flags & AP_POLL) {
		ata_ctlp->disable_intr(ata_ctlp, chno);
	} else {
		ddi_put8(ata_ctlp->ata_datap1[chno],
		    ata_ctlp->ac_devctl[chno], ATDC_D3);
	}

	if ((ata_drvp->ad_cur_disk_mode == DMA_MODE) &&
	    (ata_pktp->ap_flags & AP_DMA)) {
		ddi_put8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_feature[chno], 1);
		write_prd(ata_ctlp, ata_pktp);
		if (ata_pktp->ap_flags & AP_READ) {
			ddi_put8(ata_ctlp->ata_cs_handle,
			    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno), 8);
		} else {
			ddi_put8(ata_ctlp->ata_cs_handle,
			    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno), 0);
		}
	} else {
		ddi_put8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_feature[chno], 0);
	}
	/*
	 * This next one sets the controller in motion
	 */

	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_cmd[chno],
	    ata_pktp->ap_cmd);

	/*
	 * If  we don't receive an interrupt requesting the scsi CDB,
	 * we must poll for DRQ, and then send out the CDB.
	 */

	if (ata_drvp->ad_flags & AD_NO_CDB_INTR) {

		if (ata_wait(ata_ctlp->ata_datap1[chno],
		    ata_ctlp->ac_altstatus[chno], ATS_DRQ, ATS_BSY, IGN_ERR,
		    10, 400000) == FAILURE) {
			ata_pktp->ap_flags |= AP_TRAN_ERROR;
			ata_pktp->ap_status = ddi_get8
			    (ata_ctlp->ata_datap[chno],
			    ata_ctlp->ac_status[chno]);
			ata_pktp->ap_error =
			    ddi_get8(ata_ctlp->ata_datap[chno],
			    ata_ctlp->ac_error[chno]);
			ADBG_WARN(("atapi_start: no DRQ\n"));
			return (TRAN_FATAL_ERROR);
		}

		if (atapi_send_cdb(ata_ctlp, chno) != SUCCESS) {
			return (TRAN_FATAL_ERROR);
		}
		if ((ata_drvp->ad_cur_disk_mode == DMA_MODE) &&
		    (ata_pktp->ap_flags & AP_DMA)) {
			ddi_put8(ata_ctlp->ata_cs_handle,
			    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno),
			    (ddi_get8(ata_ctlp->ata_cs_handle,
			    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno))
			    | 0x01));
		}

	}

	return (TRAN_ACCEPT);
}

/*
 * packet complete callback
 */

static void
atapi_complete(struct ata_pkt *ata_pktp, int do_callback)
{
	struct scsi_pkt *spktp = APKT2SPKT(ata_pktp);
	struct scsi_status *scsi_stat = (struct scsi_status *)spktp->pkt_scbp;

	ADBG_TRACE(("atapi_complete entered\n"));
	ADBG_TRANSPORT(("atapi_complete: pkt = 0x%p\n", (void *)ata_pktp));

	/*
	 * update resid
	 */

	spktp->pkt_resid = ata_pktp->ap_gcmd.cmd_resid;

	/*
	 * check for fatal errors
	 */

	if (ata_pktp->ap_flags & AP_FATAL) {
		spktp->pkt_reason = CMD_TRAN_ERR;
	} else if (ata_pktp->ap_flags & AP_TRAN_ERROR) {
		spktp->pkt_reason = CMD_TRAN_ERR;
	} else if (ata_pktp->ap_flags & AP_BUS_RESET) {
		spktp->pkt_reason = CMD_RESET;
		spktp->pkt_statistics |= STAT_BUS_RESET;
	} else if (ata_pktp->ap_flags & AP_DEV_RESET) {
		spktp->pkt_reason = CMD_RESET;
		spktp->pkt_statistics |= STAT_DEV_RESET;
	} else if (ata_pktp->ap_flags & AP_ABORT) {
		spktp->pkt_reason = CMD_ABORTED;
		spktp->pkt_statistics |= STAT_ABORTED;
	} else if (ata_pktp->ap_flags & AP_TIMEOUT) {
		spktp->pkt_reason = CMD_TIMEOUT;
		spktp->pkt_statistics |= STAT_TIMEOUT;
	} else {
		spktp->pkt_reason = CMD_CMPLT;
	}

	/*
	 * non-fatal errors
	 */

	if (ata_pktp->ap_flags & AP_ERROR) {
		scsi_stat->sts_chk = 1;
	} else {
		scsi_stat->sts_chk = 0;
	}

	ADBG_TRANSPORT(("atapi_complete: reason = 0x%x stats = 0x%x "
	    "sts_chk = %d\n", spktp->pkt_reason,
	    spktp->pkt_statistics, scsi_stat->sts_chk));

	if (do_callback && (spktp->pkt_comp)) {
		(*spktp->pkt_comp)(spktp);
	}
}


/*
 * packet "process interrupt" callback
 *
 * returns STATUS_PKT_DONE when a packet has completed.
 * returns STATUS_PARTIAL when an event occurred but no packet completed
 * returns STATUS_NOINTR when there was no interrupt status
 */
static int
atapi_intr(struct ata_controller *ata_ctlp, struct ata_pkt *ata_pktp)
{
	struct ata_drive *ata_drvp = APKT2DRV(ata_pktp);
	struct scsi_pkt	*spktp;
	int	drive_bytes, data_bytes;
	uchar_t	status, intr;
	int chno = ata_drvp->ad_channel;


	ADBG_TRACE(("atapi_intr entered\n"));
	ADBG_TRANSPORT(("atapi_intr: pkt = 0x%p\n", (void *)ata_pktp));

	/*
	 * this clears the interrupt
	 */

	status = ddi_get8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_status[chno]);

	ddi_put8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2),
	    (ddi_get8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2)) | 6));
	ata_ctlp->clear_interrupt(ata_ctlp, chno);

	/*
	 * if busy, can't be our interrupt
	 */

	if (status & ATS_BSY) {
		return (STATUS_NOINTR);
	}

	spktp = APKT2SPKT(ata_pktp);

	intr = ddi_get8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_count[chno]);

	/*
	 * The atapi interrupt reason register (ata count)
	 * RELEASE/CoD/IO bits and status register SERVICE/DRQ bits
	 * define the state of the atapi packet command.
	 *
	 * If RELEASE is 1, then the device has released the
	 * bus before completing the command in progress (overlap)
	 *
	 * If SERVICE is 1, then the device has completed an
	 * overlap command and needs to be issued the SERVICE command
	 *
	 * Otherwise, the interrupt reason can be interpreted
	 * from other bits as follows:
	 *
	 *  IO  DRQ  CoD
	 *  --  ---  ---
	 *   0    1   1  Ready for atapi (scsi) pkt
	 *   1    1   1  Future use
	 *   1    1   0  Data from device.
	 *   0    1   0  Data to device
	 *   1    0   1  Status ready
	 *
	 * There is a separate interrupt for each of phases.
	 */

	if (status & ATS_DRQ) {

		if ((intr & (ATI_COD | ATI_IO)) == ATI_COD) {

			/*
			 * send out atapi pkt
			 */

			if (atapi_send_cdb(ata_ctlp, chno) == FAILURE) {
				goto errout;
			} else {
				if ((ata_drvp->ad_cur_disk_mode == DMA_MODE) &&
				    (ata_pktp->ap_flags & AP_DMA)) {
					ddi_put8(ata_ctlp->ata_cs_handle,
					    (uchar_t *)(ata_ctlp->ata_cs_addr
					    + 8*chno),
					    (ddi_get8(ata_ctlp->ata_cs_handle,
					    (uchar_t *)(ata_ctlp->ata_cs_addr
					    + 8*chno)) | 0x01));
				}
				return (STATUS_PARTIAL);
			}
		}

		drive_bytes = (int)(ddi_get8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_hcyl[chno]) << 8) +
		    ddi_get8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_lcyl[chno]);

		ASSERT(!(drive_bytes & 1)); /* even bytes */

		if ((intr & (ATI_COD | ATI_IO)) == ATI_IO) {

			/*
			 * Data from device
			 */

			data_bytes = min(ata_pktp->ap_gcmd.cmd_resid,
			    drive_bytes);

			if (data_bytes) {
				ADBG_TRANSPORT(("atapi_intr: read "
				    "0x%x bytes\n", data_bytes));
				ddi_rep_get16(ata_ctlp->ata_datap[chno],
				    (ushort_t *)
				    ata_pktp->ap_v_addr, (uint16_t *)
				    ata_ctlp->ac_data[chno],  (data_bytes
				    >> 1), 0);
				ata_pktp->ap_gcmd.cmd_resid -= data_bytes;
				ata_pktp->ap_v_addr += data_bytes;
				drive_bytes -= data_bytes;
			}

			if (drive_bytes) {
				ADBG_TRANSPORT(("atapi_intr: dump "
				    "0x%x bytes\n", drive_bytes));
				ddi_rep_get16(ata_ctlp->ata_datap[chno],
				    ata_bit_bucket, (uint16_t *)
				    ata_ctlp->ac_data[chno],
				    (drive_bytes >> 1), 0);
			}
			if (ata_wait(ata_ctlp->ata_datap1[chno],
			    ata_ctlp->ac_altstatus[chno], 0, ATS_BSY, IGN_ERR,
			    10, 500000) == FAILURE) {
				/* EMPTY */
				ADBG_WARN(("atapi_id: no DRDY\n"));
			}
			spktp->pkt_state |= STATE_XFERRED_DATA;
			return (STATUS_PARTIAL);
		}

		if ((intr & (ATI_COD | ATI_IO)) == 0) {

			/*
			 * Data to device
			 */

			ddi_rep_put16(ata_ctlp->ata_datap[chno],
			    (ushort_t *)ata_pktp->ap_v_addr,
			    (uint16_t *)ata_ctlp->ac_data[chno],
			    (drive_bytes >> 1), 0);
			ADBG_TRANSPORT(("atapi_intr: wrote 0x%x bytes\n",
			    drive_bytes));
			ata_pktp->ap_v_addr += drive_bytes;
			ata_pktp->ap_gcmd.cmd_resid -= drive_bytes;

			spktp->pkt_state |= STATE_XFERRED_DATA;
			return (STATUS_PARTIAL);
		}

		if (!((intr & (ATI_COD | ATI_IO)) == (ATI_COD | ATI_IO))) {

			/*
			 * Unsupported intr combination
			 */

			return (STATUS_PARTIAL);
		}
	} else {
		if ((ata_drvp->ad_cur_disk_mode == DMA_MODE) &&
		    (ata_pktp->ap_flags & AP_DMA)) {
			if ((intr & (ATI_COD | ATI_IO)) == (ATI_IO | ATI_COD)) {
				ata_pktp->ap_gcmd.cmd_resid = 0;
				spktp->pkt_state |= STATE_XFERRED_DATA;
				ddi_put8(ata_ctlp->ata_cs_handle,
				    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno),
				    (ddi_get8(ata_ctlp->ata_cs_handle,
				    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno))
				    & 0xfe));
			}
		}
	}

	/* If we get here, a command has completed! */

	/*
	 * re-enable interrupts after a polling packet
	 */
	if (ata_pktp->ap_flags & AP_POLL) {
		ata_ctlp->enable_intr(ata_ctlp, chno);
		ddi_put8(ata_ctlp->ata_datap1[chno], ata_ctlp->ac_devctl[chno],
		    ATDC_D3);
	}

	/*
	 * check status of completed command
	 */
	if (status & ATS_ERR) {
		ata_pktp->ap_flags |= AP_ERROR;
		if ((ata_drvp->ad_cur_disk_mode == DMA_MODE) &&
		    (ata_pktp->ap_flags & AP_DMA)) {
			/*
			 * Stop the DMA engine in case of error.
			 * Note that the above check does not cover the
			 * possibility to DMA having not started at all.
			 * but that should be ok since we stop the DMA engine
			 * without checking for DMA start at other places in
			 * the code ex - ata_reset_bus.
			 */
			ddi_put8(ata_ctlp->ata_cs_handle,
			    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno),
			    (ddi_get8(ata_ctlp->ata_cs_handle,
			    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno))
			    & 0xfe));
		}
		ata_pktp->ap_status = ddi_get8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_status[chno]);
		ata_pktp->ap_error = ddi_get8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_error[chno]);
	}

	spktp->pkt_state |= STATE_GOT_STATUS;

	return (STATUS_PKT_DONE);

errout:
	ata_pktp->ap_flags |= AP_TRAN_ERROR;

	/*
	 * re-enable interrupts after a polling packet
	 */
	if (ata_pktp->ap_flags & AP_POLL) {
		ddi_put8(ata_ctlp->ata_datap1[chno],
		    ata_ctlp->ac_devctl[chno], ATDC_D3);
		ata_ctlp->enable_intr(ata_ctlp, chno);
	}


	return (STATUS_PKT_DONE);
}

/*
 * Send a SCSI CDB to ATAPI device
 */
static int
atapi_send_cdb(struct ata_controller *ata_ctlp, int chno)
{
	struct ata_pkt *ata_pktp = ata_ctlp->ac_active[chno];
	struct scsi_pkt *spktp = APKT2SPKT(ata_pktp);
	int padding;
	char my_cdb[10];

	ADBG_TRACE(("atapi_send_cdb entered\n"));

	if (spktp->pkt_cdbp[0] == 0x03) {
		spktp->pkt_cdbp[4] = 0x12;
	}

	if (spktp->pkt_cdbp[0] == 0x08 && ata_pktp->ap_cdb_len == 6) {
		ata_pktp->ap_cdb_pad -= 2;
		my_cdb[0] = 0x28;
		my_cdb[1] = (unsigned char)(spktp->pkt_cdbp[1] & 0xf0) >> 4;
		my_cdb[2] = 0;
		my_cdb[3] = spktp->pkt_cdbp[1] & 0x0f;
		my_cdb[4] = spktp->pkt_cdbp[2];
		my_cdb[5] = spktp->pkt_cdbp[3];
		my_cdb[6] = 0;
		my_cdb[7] = 0;
		my_cdb[8] = spktp->pkt_cdbp[4];
		if (my_cdb[8] == 0)
			my_cdb[8] = 1;
		my_cdb[9] = 0;

		ddi_rep_put16(ata_ctlp->ata_datap[chno],
		    (ushort_t *)(&my_cdb[0]),
		    (uint16_t *)ata_ctlp->ac_data[chno],  5, 0);
	} else {
		ddi_rep_put16(ata_ctlp->ata_datap[chno],
		    (ushort_t *)spktp->pkt_cdbp,
		    (uint16_t *)ata_ctlp->ac_data[chno],
		    ata_pktp->ap_cdb_len >> 1, 0);
	}

	/*
	 * pad to ad_cdb_len bytes
	 */
	padding = ata_pktp->ap_cdb_pad;

	while (padding) {
		ddi_put16(ata_ctlp->ata_datap[chno], (uint16_t *)
		    ata_ctlp->ac_data[chno], 0);
		padding--;
	}

#ifdef ATA_DEBUG
	{
		/* LINTED */
		unsigned char *cp = (unsigned char *)spktp->pkt_cdbp;

		ADBG_TRANSPORT(("\tatapi scsi cmd (%d bytes):\n ",
		    ata_pktp->ap_cdb_len));
		ADBG_TRANSPORT(("\t\t 0x%x 0x%x 0x%x 0x%x\n",
		    cp[0], cp[1], cp[2], cp[3]));
		ADBG_TRANSPORT(("\t\t 0x%x 0x%x 0x%x 0x%x\n",
		    cp[4], cp[5], cp[6], cp[7]));
		ADBG_TRANSPORT(("\t\t 0x%x 0x%x 0x%x 0x%x\n",
		    cp[8], cp[9], cp[10], cp[11]));
	}
#endif

	spktp->pkt_state = (STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD);

	return (SUCCESS);
}

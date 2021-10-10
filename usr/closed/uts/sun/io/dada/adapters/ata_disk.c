/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>

#include <sys/dada/dada.h>
#include <sys/dada/adapters/ata_common.h>
#include <sys/dada/adapters/ata_disk.h>

/*
 * DADA entry points
 */
static	int ata_disk_tran_tgt_init(dev_info_t *hba_dip,
		dev_info_t *tgt_dip, dcd_hba_tran_t *hba_tran,
		struct dcd_device *sd);
static	int ata_disk_tran_tgt_probe(struct dcd_device *sd, int (*callback)());

static	int ata_disk_tran_abort(struct dcd_address *ap, struct dcd_pkt *pktp);
static	int ata_disk_tran_reset(struct dcd_address *ap, int level);
static	struct dcd_pkt *ata_disk_tran_init_pkt(struct dcd_address *ap,
		register struct dcd_pkt *pkt, struct buf *bp,
		int cmdlen, int statuslen, int tgtlen,
		int flags, int (*callback)(), caddr_t arg);
static	int ata_disk_tran_start(struct dcd_address *ap, struct dcd_pkt *pktp);
static	void ata_disk_tran_destroy_pkt(struct dcd_address *ap,
		struct dcd_pkt *pkt);

/*
 * packet callbacks
 */
static int ata_disk_start(struct ata_controller *ata_ctlp,
			struct ata_pkt *ata_pktp);
static void ata_disk_complete(struct ata_pkt *ata_pktp, int do_callback);
static int ata_disk_intr(struct ata_controller *ata_ctlp,
			struct ata_pkt *ata_pktp);

/*
 * local functions
 */
static int ata_disk_xfer_data(struct ata_controller *ata_ctlp, int chno);
int ata_disk_set_rw_multiple(struct ata_drive *ata_drvp);

/*
 * External Functions.
 */
extern void
make_prd(gcmd_t *gcmdp, ddi_dma_cookie_t *cookie, int single_seg, int num_segs);
extern void
write_prd(struct ata_controller *ata_ctlp, struct ata_pkt *ata_pktp);
extern int prd_init(struct ata_controller *ata_ctlp, int chno);
extern void change_endian(unsigned char *string, int length);
extern int ata_dmaget(dev_info_t   *dip, struct ata_pkt  *ata_pktp,
	struct buf *bp, int dma_flags, int (*callback)(), caddr_t arg);
extern void ata_dmafree(struct ata_pkt  *ata_pktp);


extern ddi_dma_attr_t ata_dma_attrs;
#define	APKT2DPKT(pkt)	((struct dcd_pkt *)(GCMD2CPKT(APKT2GCMD(pkt))))
#define	APKT2CMD(pkt)	((opaque_t)((APKT2DPKT(pkt))->pkt_cdbp))
#define	ADDR2CTL(ap)	(TRAN2CTL(ADDR2TRAN(ap)))
#define	TRAN2CTL(tran)	((struct ata_controller *)((tran)->tran_hba_private))


/*
 * initialize the ata_disk sub-system
 */
/*ARGSUSED*/
int
ata_disk_init(struct ata_controller *ata_ctlp)
{
	dev_info_t	*dip = ata_ctlp->ac_dip;
	dcd_hba_tran_t *tran;

	ADBG_TRACE(("ata_disk_init entered\n"));

	/* allocate transport structure */

	tran = dcd_hba_tran_alloc(dip, DCD_HBA_CANSLEEP);

	if (tran == NULL) {
		ADBG_WARN(("ata_disk_init: dcd_hba_tran_alloc failed\n"));
		return (FAILURE);
	}

	ata_ctlp->ac_ata_tran = (dcd_hba_tran_t *)tran;

	/*
	 * initialize transport structure
	 */

	tran->tran_hba_private = ata_ctlp;
	tran->tran_tgt_private = NULL;
	tran->tran_tgt_init = ata_disk_tran_tgt_init;
	tran->tran_tgt_probe = ata_disk_tran_tgt_probe;
	tran->tran_tgt_free = NULL;
	tran->tran_start = ata_disk_tran_start;
	tran->tran_reset = ata_disk_tran_reset;
	tran->tran_abort = ata_disk_tran_abort;
	tran->tran_init_pkt = ata_disk_tran_init_pkt;
	tran->tran_destroy_pkt = ata_disk_tran_destroy_pkt;
	tran->tran_dmafree = NULL;
	tran->tran_sync_pkt = NULL;

	if (dcd_hba_attach(ata_ctlp->ac_dip,
	    &ata_dma_attrs, tran, 0) != DDI_SUCCESS) {
		ADBG_WARN(("ata_disk_init: dcd_hba_attach failed\n"));
		return (FAILURE);
	}

	return (SUCCESS);
}



/*ARGSUSED*/
int
ata_disk_init_reset(struct ata_controller *ata_ctlp)
{
	uchar_t  mode;
	int	targ;
	struct ata_drive *ata_drvp;

	for (targ = 0; targ < ATA_MAXTARG; targ++) {
		ata_drvp = CTL2DRV(ata_ctlp, targ, 0);
		if (ata_drvp == NULL) {
			continue;
		}
		if (ata_drvp->ad_flags & AD_ATAPI) {
			continue;
		}
		if (ata_drvp->ad_cur_disk_mode == DMA_MODE) {
			mode = ata_drvp->ad_dmamode;
		} else {
			mode = ata_drvp->ad_piomode;
			if (mode > DCD_PIO_MODE2)
				mode |= ENABLE_PIO_FEATURE;
		}
		ata_ctlp->program_timing_reg(ata_drvp);
		if (ata_set_feature(ata_drvp, ATA_FEATURE_SET_MODE, mode) !=
		    SUCCESS) {
			return (FAILURE);
		}
	}

	return (SUCCESS);
}

/* ARGSUSED0 */
static int
ata_disk_tran_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    dcd_hba_tran_t *hba_tran, struct dcd_device *sd)
{
	return ((sd->dcd_address->da_target < 4) ? DDI_SUCCESS : DDI_FAILURE);
}

static int
ata_disk_tran_tgt_probe(struct dcd_device *sd, int (*callback)())
{
	ADBG_TRACE(("dcd_tran_tgt_probe entered\n"));

	return (dcd_hba_probe(sd, callback));
}


/*
 * destroy the ata_disk sub-system
 */
/*ARGSUSED*/
void
ata_disk_destroy(struct ata_controller *ata_ctlp)
{
	ADBG_TRACE(("ata_disk_destroy entered\n"));
	/* nothing to do */
}

/*
 * initialize an ata_disk drive
 */
int
ata_disk_init_drive(struct ata_drive *ata_drvp)
{
	struct ata_controller *ata_ctlp = ata_drvp->ad_ctlp;
	struct 	dcd_device *devp;
	int 	len, val;
	char 	buf[80];
	int chno = ata_drvp->ad_channel;

	ADBG_TRACE(("ata_disk_init_drive entered\n"));

	/*
	 * ATA disks don't support LUNs
	 */
	if (ata_drvp->ad_lun != 0) {
		return (FAILURE);
	}

	/*
	 * set up drive structure
	 */
	ata_drvp->ad_invalid = 0;

	if (ata_drvp->ad_id.dcd_cap & ATAC_LBA_SUPPORT) {
		ata_drvp->ad_drive_bits |= ATDH_LBA;
	}

	/*
	 * set up the dcd_device
	 */
	devp = &ata_drvp->ad_device;
	devp->dcd_ident = &ata_drvp->ad_inquiry;
	devp->dcd_dev = NULL;
	devp->dcd_address = &(ata_drvp->ad_address);
	devp->dcd_address->da_target = (ushort_t)ata_drvp->ad_targ;
	devp->dcd_address->da_lun = (uchar_t)ata_drvp->ad_lun;
	mutex_init(&devp->dcd_mutex, NULL, MUTEX_DRIVER, NULL);
	ata_drvp->ad_flags |= AD_MUTEX_INIT;

	/*
	 * get highest block factor supported by the drive.
	 * value may be 0 if read/write multiple is not supported
	 */
	ata_drvp->ad_block_factor = ata_drvp->ad_id.dcd_mult1 & 0xff;

	if (ata_drvp->ad_block_factor == 0) {
		ata_drvp->ad_block_factor = 1;
	}

	/*
	 * If a block factor property exists, use the smaller of the
	 * property value and the highest value the drive can support.
	 */

	len = sizeof (int);
	(void) sprintf(buf, "drive%d_block_factor", ata_drvp->ad_targ);
	if (ATA_INTPROP(ata_ctlp->ac_dip, buf, &val, &len) ==
	    DDI_PROP_SUCCESS) {
		ata_drvp->ad_block_factor =
		    (short)min(val, ata_drvp->ad_block_factor);
	}


	if (ata_drvp->ad_block_factor > 1) {
		/*
		 * Program the block factor into the drive. If this
		 * fails, then go back to using a block size of 1.
		 */
		if ((ata_disk_set_rw_multiple(ata_drvp) == FAILURE)) {
			ata_drvp->ad_block_factor = 1;
		}
	}

	if (ata_drvp->ad_block_factor > 1) {
		ata_drvp->ad_rd_cmd = ATC_RDMULT;
		ata_drvp->ad_wr_cmd = ATC_WRMULT;
	} else {
		ata_drvp->ad_rd_cmd = ATC_RDSEC;
		ata_drvp->ad_wr_cmd = ATC_WRSEC;
	}

	ata_drvp->ad_bytes_per_block = ata_drvp->ad_block_factor << SCTRSHFT;

	ADBG_INIT(("set block factor for drive %d to %d\n",
	    ata_drvp->ad_targ, ata_drvp->ad_block_factor));

	if (!ata_ctlp->ata_prd_acc_handle[chno])
		return (prd_init(ata_ctlp, chno));
	else
		return (SUCCESS);
}

/*
 * destroy an ata disk drive
 */
void
ata_disk_destroy_drive(struct ata_drive *ata_drvp)
{
	struct 	dcd_device *devp = &ata_drvp->ad_device;

	ADBG_TRACE(("ata_disk_destroy_drive entered\n"));

	if (ata_drvp->ad_flags & AD_MUTEX_INIT) {
		mutex_destroy(&devp->dcd_mutex);
	}
}

/*
 * ATA Identify Device command
 */
int
ata_disk_id(ddi_acc_handle_t handle, uint8_t *ioaddr,  ushort_t *buf)
{
	ADBG_TRACE(("ata_disk_id entered\n"));
	if (ata_wait(handle, ioaddr + AT_STATUS, 0, ATS_BSY, IGN_ERR,
	    100, 2) == FAILURE) {
		/*
		 * Do not issue the command if BSY is high
		 */
		return (FAILURE);
	}
	ddi_put8(handle, (ioaddr + AT_CMD), ATC_READPARMS);

	/*
	 * According to the ATA specification, some drives may have
	 * to read the media to complete this command.  We need to
	 * make sure we give them enough time to respond.
	 */

	if (ata_wait(handle, ioaddr + AT_STATUS, ATS_DRQ, ATS_BSY, IGN_ERR,
	    100, 10000) == FAILURE) {
		return (FAILURE);
	}

	ddi_rep_get16(handle, buf, (uint16_t *)(ioaddr + AT_DATA),
	    NBPSCTR >> 1, 0);

	change_endian((uchar_t *)buf, NBPSCTR);

	/*
	 * wait for the drive to recognize I've read all the data.  some
	 * drives have been observed to take as much as 3msec to finish
	 * sending the data; allow 5 msec just in case.
	 */
	if (ata_wait(handle, ioaddr + AT_STATUS, ATS_DRDY, ATS_BSY | ATS_DRQ,
	    IGN_ERR, 30, 500) == FAILURE) {
		ADBG_WARN(("ata_disk_id: no DRDY\n"));
		return (FAILURE);
	}

	if (ddi_get8(handle, (ioaddr + AT_STATUS)) & ATS_ERR) {
		ADBG_WARN(("ata_disk_id: ERROR status\n"));
		return (FAILURE);
	}

	return (SUCCESS);
}

/*
 * DADA compliant bus_ctl entry point
 */
/*ARGSUSED*/
int
ata_disk_bus_ctl(dev_info_t *d, dev_info_t *r, ddi_ctl_enum_t o,
	void *a, void *v, struct ata_drive *ata_drvp)
{
	ADBG_TRACE(("ata_disk_bus_ctl entered\n"));

	switch (o) {

	case DDI_CTLOPS_REPORTDEV:
	{
		int	len = sizeof (int);
		int	targ;

		cmn_err(CE_CONT, "?%s%d at %s%d",
		    ddi_driver_name(r), ddi_get_instance(r),
		    ddi_driver_name(d), ddi_get_instance(d));
		if (ATA_INTPROP(r, "target", &targ, &len) != DDI_SUCCESS) {
			targ = 0;
		}
		cmn_err(CE_CONT, "? target %d lun %d\n", targ, 0);
		return (DDI_SUCCESS);
	}
	case DDI_CTLOPS_INITCHILD:
	{
		dev_info_t *cdip = (dev_info_t *)a;
		struct 	dcd_device *devp;
		struct	dcd_address *ap;
		char name[MAXNAMELEN];
		struct ata_controller *ata_ctlp;
		gtgt_t *gtgtp;
		int chno;

		/*
		 * set up pointers to child dip
		 */
		ata_ctlp = ata_drvp->ad_ctlp;
		devp = &(ata_drvp->ad_device);
		/*
		 * Don't trample on devices already initialized
		 */
		if (devp->dcd_dev != NULL)
			return (DDI_FAILURE);

		devp->dcd_dev = cdip;
		devp->dcd_ident = NULL;
		ap = devp->dcd_address;
		ap->da_target = ata_drvp->ad_targ;
		ap->da_lun = ata_drvp->ad_lun;
		ap->a_hba_tran = ata_ctlp->ac_ata_tran;
		chno = DADR2CHNO(ap);

		gtgtp = ghd_target_init(d, cdip, &ata_ctlp->ac_ccc[chno],
		    0, ata_ctlp, (uint32_t)ata_drvp->ad_targ,
		    (uint32_t)ata_drvp->ad_lun);

		ata_drvp->ad_gtgtp = gtgtp;

		/*
		 * gt_tgt_private points to ata_drive
		 */
		GTGTP2TARGET(gtgtp) = ata_drvp;

		/*
		 * create device name
		 */
		(void) sprintf(name, "%d,%d", ata_drvp->ad_targ,
		    ata_drvp->ad_lun);
		ddi_set_name_addr(cdip, name);
		ddi_set_driver_private(cdip, devp);

		return (DDI_SUCCESS);
	}

	case DDI_CTLOPS_UNINITCHILD:
	{
		dev_info_t *cdip = (dev_info_t *)a;

		ddi_set_driver_private(cdip, NULL);
		ddi_set_name_addr(cdip, NULL);
		return (DDI_SUCCESS);
	}

	default:
		return (DDI_FAILURE);
	}
}


/*
 * DADA abort entry point - not currently used by dadk
 */
/* ARGSUSED */
static int
ata_disk_tran_abort(struct dcd_address *ap, struct dcd_pkt *pktp)
{
	ADBG_TRACE(("ata_disk_tran_abort entered\n"));

	/* XXX - Note that this interface is currently not used by dadk */

	/*
	 *  GHD abort functions take a pointer to a dcd_address
	 *  and so they're unusable here.  The ata driver used to
	 *  return DDI_SUCCESS here without doing anything.  Its
	 *  seems that DDI_FAILURE is more appropriate.
	 */

	return (DDI_FAILURE);
}

/*
 * DADA reset entry point
 */
/* ARGSUSED */
static int
ata_disk_tran_reset(struct dcd_address *ap, int level)
{
	int rc, chno;
	struct ata_controller *ata_ctlp = ADDR2CTLP(ap);
	struct ata_drive *ata_drvp =
	    CTL2DRV(ata_ctlp, ap->da_target, ap->da_lun);

	ADBG_TRACE(("ata_disk_tran_reset entered\n"));

	chno = DADR2CHNO(ap);

	if (level == RESET_TARGET) {
		rc = ghd_tran_reset_target(&ata_ctlp->ac_ccc[chno],
		    ata_drvp->ad_gtgtp, NULL);
	} else if (level == RESET_ALL) {
		rc = ghd_tran_reset_bus(&ata_ctlp->ac_ccc[chno],
		    ata_drvp->ad_gtgtp, NULL);
	}

	if (rc == TRUE) {
		return (1);
	}
	return (0);
}


/* ARGSUSED */
static struct dcd_pkt *
ata_disk_tran_init_pkt(struct dcd_address *ap, register struct dcd_pkt *pkt,
	struct buf *bp, int cmdlen, int statuslen, int tgtlen,
	int flags, int (*callback)(), caddr_t arg)
{
	struct ata_pkt *ata_pktp;
	struct ata_controller *ata_ctlp = ADDR2CTLP(ap);
	struct ata_drive *ata_drvp;
	gcmd_t *gcmdp;


	ADBG_TRACE(("ata_disk_tran_init_pkt entered\n"));
	if (pkt == NULL) {
		pkt = dcd_hba_pkt_alloc(ap, cmdlen, statuslen,
		    tgtlen, (uint32_t)sizeof (gcmd_t), callback, arg);
		if (pkt == NULL) {
			return (NULL);
		}

		ata_pktp = (struct ata_pkt *)
		    kmem_zalloc(sizeof (struct ata_pkt),
		    (callback == SLEEP_FUNC)? KM_SLEEP: KM_NOSLEEP);
		if (ata_pktp == NULL) {
			dcd_hba_pkt_free(ap, pkt);
			return (NULL);
		}

		gcmdp = APKT2GCMD(ata_pktp);
		ata_drvp = CTL2DRV(ata_ctlp, ap->da_target, ap->da_lun);

		GHD_GCMD_INIT(gcmdp, (void *)ata_pktp, ata_drvp->ad_gtgtp);

		pkt->pkt_ha_private = (void *)gcmdp;
		gcmdp->cmd_pktp = (void *)pkt;

		/*
		 * callback functions
		 */
		ata_pktp->ap_start = ata_disk_start;
		ata_pktp->ap_intr = ata_disk_intr;
		ata_pktp->ap_complete = ata_disk_complete;

		ata_pktp->ap_bytes_per_block = ata_drvp->ad_bytes_per_block;
		ata_pktp->ap_chno = ata_drvp->ad_channel;
		ata_pktp->ap_targ = ata_drvp->ad_targ;

		if ((ata_drvp->ad_dmamode != 0x7f) && bp && bp->b_bcount) {
			int	dma_flags;
			/*
			 * Set up dma info if there's any data and
			 * if the device supports DMA.
			 */

			/*
			 * check direction for data transfer
			 */
			if (bp->b_flags & B_READ) {
				dma_flags = DDI_DMA_READ;
			} else {
				dma_flags = DDI_DMA_WRITE;
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
			    ata_pktp, bp, dma_flags, callback,
			    arg) == DDI_FAILURE) {
				return (NULL);
			}
			pkt->pkt_resid =
			    ata_pktp->ap_gcmd.cmd_resid = bp->b_bcount;
		}
		if (bp && bp->b_bcount != 0) {

			pkt->pkt_resid =
			    ata_pktp->ap_gcmd.cmd_resid = bp->b_bcount;
			ata_pktp->ap_count_bytes = bp->b_bcount;
			if (ata_drvp->ad_piomode != 0x7f) {
				bp_mapin(bp);
			}
			ata_pktp->ap_v_addr = bp->b_un.b_addr;
		} else {
			ata_pktp->ap_count_bytes = 0;
			pkt->pkt_resid =
			    ata_pktp->ap_gcmd.cmd_resid = 0;
		}
	} else {
		printf("wht");
		/* Should we have to allocate again a  ata_pkt ??? */
	}
	return (pkt);
}


static int
ata_disk_tran_start(struct dcd_address *ap, struct dcd_pkt *pkt)
{
	struct ata_controller *ata_ctlp;
	struct ata_drive *ata_drvp;
	struct ata_pkt *ata_pktp = PKT2APKT(pkt);
	int rc, polled = FALSE;
	int	intr_status = 0, chno;

	ata_ctlp = ADDR2CTLP(ap);
	ata_drvp = CTL2DRV(ata_ctlp, ap->da_target, ap->da_lun);
	chno = DADR2CHNO(ap);

	ADBG_TRACE(("ata_disk_transport entered\n"));

	if (ata_drvp->ad_invalid == 1) {
		return (TRAN_FATAL_ERROR);
	}

	/*
	 * Ata driver can not handle odd count requests
	 */
	if (ata_pktp->ap_count_bytes % 2) {
		return (TRAN_BADPKT);
	}

	/*
	 * check for polling pkt
	 */
	if (pkt->pkt_flags & FLAG_NOINTR) {
		ata_pktp->ap_flags |= AP_POLL;
		polled = TRUE;
		mutex_enter(&ata_ctlp->ac_hba_mutex);
		ata_ctlp->ac_polled_count++;
		mutex_exit(&ata_ctlp->ac_hba_mutex);
	}

	/*
	 * call ghd transport routine
	 */
	rc = ghd_transport(&ata_ctlp->ac_ccc[chno], APKT2GCMD(ata_pktp),
	    ata_drvp->ad_gtgtp, pkt->pkt_time, polled, &intr_status);

	/*
	 * see if pkt was not accepted
	 */
	if (rc == TRAN_BUSY) {
		return (TRAN_BUSY);
	}

	if (rc == TRAN_ACCEPT) {
		return (TRAN_ACCEPT);
	}

	return (TRAN_FATAL_ERROR);
}

/*
 * packet start callback routine
 */
static int
ata_disk_start(struct ata_controller *ata_ctlp, struct ata_pkt *ata_pktp)
{
	uchar_t ap_count, ap_sec, ap_lwcyl, ap_hicyl, ap_hd;
	struct dcd_pkt *dcd_pkt =
	    (struct dcd_pkt *)APKT2DPKT(ata_pktp);
	struct dcd_address *ap = &(dcd_pkt->pkt_address);
	struct dcd_cmd *dcd_cmd = (struct dcd_cmd *)(dcd_pkt->pkt_cdbp);
	struct ata_drive *ata_drvp =
	    CTL2DRV(ata_ctlp, ap->da_target, ap->da_lun);
	int chno = ata_drvp->ad_channel;

	ADBG_TRACE(("ata_disk_start entered\n"));

	ADBG_TRANSPORT(("ata_disk_start:\tpkt = 0x%p, pkt flags = 0x%x\n",
	    (void *)ata_pktp, ata_pktp->ap_flags));

	if (ata_drvp->ad_invalid == 1) {
		ata_pktp->ap_flags &= ~(AP_TIMEOUT|AP_ABORT|
		    AP_BUS_RESET|AP_ERROR);
		ata_pktp->ap_flags |= AP_FATAL;
		return (TRAN_FATAL_ERROR);
	}

	ata_pktp->ap_status = 0;
	ata_pktp->ap_error = 0;
	ata_pktp->ap_cmd = dcd_cmd->cmd;
	ata_pktp->ap_flags &= ~(AP_TIMEOUT|AP_ABORT|AP_BUS_RESET|AP_ERROR);
	/*
	 * For non DMA/STANDBY/IDLE commands
	 */
	if ((dcd_cmd->cmd != ATA_READ_DMA) &&
	    (dcd_cmd->cmd != ATA_WRITE_DMA) &&
	    (dcd_cmd->cmd != IDENTIFY_DMA) &&
	    (dcd_cmd->cmd != ATC_STANDBY) &&
	    (dcd_cmd->cmd != ATC_IDLE)) {
		if (ata_drvp->ad_cur_disk_mode == DMA_MODE) {
			/*
			 * Set to the mode  4 for PIO mode
			 * command while the normal read write go through
			 * DMA.
			 *
			 * This assumes all ATA disk will support PIO Mode if
			 * it is already in
			 * DMA mode supported.
			 */
			ata_drvp->ad_piomode = 4;
			ata_ctlp->program_timing_reg(ata_drvp);
			ata_drvp->ad_piomode = 0x7f;
			(void) ata_set_feature(ata_drvp,
			    ATA_FEATURE_SET_MODE, 0xc);
			ata_drvp->ad_cur_disk_mode = PIO_MODE;
		}
	} else {
		if (ata_drvp->ad_cur_disk_mode == PIO_MODE) {
			ata_ctlp->program_timing_reg(ata_drvp);
			(void) ata_set_feature(ata_drvp,
			    ATA_FEATURE_SET_MODE, ata_drvp->ad_dmamode);
			ata_drvp->ad_cur_disk_mode = DMA_MODE;
		}
	}

	/*
	 * check for busy before starting command.  This
	 * is important for laptops that do suspend/resume
	 */
	if (ata_wait(ata_ctlp->ata_datap1[chno], ata_ctlp->ac_altstatus[chno],
	    0, ATS_BSY, IGN_ERR, 80, 700000) == FAILURE) {
		ADBG_WARN(("ata_disk_start: BSY too long!\n"));
		ata_pktp->ap_flags |= AP_ERROR;
		ata_pktp->ap_status = ddi_get8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_status[chno]);
		ata_pktp->ap_error = ddi_get8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_error[chno]);
		printf(" BUSY set : Hence returning error status %x, "
		    "error %x\n",
		    ata_pktp->ap_status, ata_pktp->ap_error);
		return (TRAN_FATAL_ERROR);
	}

	ap_count = (uchar_t)(dcd_cmd->size >> 9);
	ap_sec = dcd_cmd->sector_num.lba_num & 0xff;
	ap_lwcyl = (dcd_cmd->sector_num.lba_num >> 8) & 0xff;
	ap_hicyl = (dcd_cmd->sector_num.lba_num >> 16) & 0xff;
	ap_hd = (dcd_cmd->sector_num.lba_num >> 24) & 0xf;

	if (ap->da_target & 1) {
		ap_hd |= (ATDH_DRIVE1 | ATDH_LBA);
	} else {
		ap_hd |= (ATDH_DRIVE0 | ATDH_LBA);
	}

	ADBG_TRANSPORT(("\tcommand=0x%x, drvhd=0x%x, sect=0x%x\n",
	    dcd_cmd->cmd, ap_hd, ap_sec));
	ADBG_TRANSPORT(("\tcount=0x%x, lwcyl=0x%x, hicyl=0x%x address=0x%p\n",
	    ap_count, ap_lwcyl, ap_hicyl, (void *)ata_pktp->ap_v_addr));


	if (dcd_cmd->direction & DATA_READ) {
		ata_pktp->ap_flags |= AP_READ;
	} else {
		ata_pktp->ap_flags |= AP_WRITE;
	}
	if (dcd_cmd->cmd == 0x20) {
		ata_pktp->ap_flags &= ~AP_WRITE;
		ata_pktp->ap_flags |= AP_READ;
	}


	/*
	 * Adjust Block factor for Set Multiple command.
	 */
	if (dcd_cmd->cmd == ATA_SET_MULTIPLE) {
		if (ap_count > 1) {
			ata_drvp->ad_block_factor = ap_count;
			ata_drvp->ad_rd_cmd = ATC_RDMULT;
			ata_drvp->ad_wr_cmd = ATC_WRMULT;
		} else {
			ata_drvp->ad_block_factor = 1;
			ata_drvp->ad_rd_cmd = ATC_RDSEC;
			ata_drvp->ad_wr_cmd = ATC_WRSEC;
		}

		ata_drvp->ad_bytes_per_block =
		    ata_drvp->ad_block_factor << SCTRSHFT;
	}

	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_drvhd[chno], ap_hd);

	/*
	 * Enable the interrupts - Just in case
	 */
	ddi_put8(ata_ctlp->ata_datap1[chno],
	    ata_ctlp->ac_devctl[chno], ATDC_D3);
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_sect[chno], ap_sec);
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_count[chno], ap_count);
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_lcyl[chno], ap_lwcyl);
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_hcyl[chno], ap_hicyl);
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_feature[chno],
	    dcd_cmd->features);

	if ((ata_drvp->ad_dmamode != 0x7f) && (dcd_cmd->cmd == ATA_READ_DMA ||
	    dcd_cmd->cmd == ATA_WRITE_DMA ||
	    dcd_cmd->cmd == IDENTIFY_DMA)) {
		ata_drvp->ad_dmamode |= DCD_DMA_MODE;
		write_prd(ata_ctlp, ata_pktp);
		if (ata_pktp->ap_flags & AP_READ) {
			ddi_put8(ata_ctlp->ata_cs_handle,
			    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno), 8);
		} else {
			ddi_put8(ata_ctlp->ata_cs_handle,
			    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno), 0);
		}

		if (ata_pktp->ap_flags & AP_POLL) {
			ata_ctlp->disable_intr(ata_ctlp, chno);
		} else {
			ddi_put8(ata_ctlp->ata_datap1[chno],
			    ata_ctlp->ac_devctl[chno], ATDC_D3);
		}

		ddi_put8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_cmd[chno], dcd_cmd->cmd);
		ddi_put8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno),
		    (ddi_get8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno)) | 0x01));

		return (TRAN_ACCEPT);
	}



	/*
	 * enable or disable interrupts for command
	 */
	if (ata_pktp->ap_flags & AP_POLL) {
		ata_ctlp->disable_intr(ata_ctlp, chno);
	} else {
		ddi_put8(ata_ctlp->ata_datap1[chno],
		    ata_ctlp->ac_devctl[chno], ATDC_D3);
	}

	/*
	 * This next one sets the controller in motion
	 */
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_cmd[chno], dcd_cmd->cmd);

	/*
	 * If no data to write, we're done for now
	 */
	if (!(ata_pktp->ap_flags & AP_WRITE)) {
		return (TRAN_ACCEPT);
	}

	/*
	 * For a write, we send some data
	 */
	if (ata_disk_xfer_data(ata_ctlp, chno) == FAILURE) {
		ata_pktp->ap_flags |= AP_ERROR;
		ata_pktp->ap_status =
		    ddi_get8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_status[chno]);
		ata_pktp->ap_error =
		    ddi_get8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_error[chno]);
		return (TRAN_FATAL_ERROR);
	}

	return (TRAN_ACCEPT);
}

/*
 * packet complete callback routine
 */
static void
ata_disk_complete(struct ata_pkt *ata_pktp, int do_callback)
{
	struct dcd_pkt	*pktp;
	uchar_t	*pkt_scbp;

	ADBG_TRACE(("ata_disk_complete entered\n"));
	ADBG_TRANSPORT(("ata_disk_complete: pkt = 0x%p\n",
	    (void *)ata_pktp));

	pktp = APKT2DPKT(ata_pktp);
	pkt_scbp = pktp->pkt_scbp;
	*pkt_scbp++ = ata_pktp->ap_status;
	*pkt_scbp = ata_pktp->ap_error;

	/*
	 * update resid
	 */
	pktp->pkt_resid = ata_pktp->ap_gcmd.cmd_resid;

	if (ata_pktp->ap_flags & AP_FATAL) {
		pktp->pkt_reason = CMD_FATAL;
	} else if (ata_pktp->ap_flags & AP_ERROR) {
		pktp->pkt_reason = CMD_INCOMPLETE;
	} else if (ata_pktp->ap_flags &
	    (AP_ABORT|AP_TIMEOUT|AP_BUS_RESET)) {

		if (ata_pktp->ap_flags & AP_TIMEOUT) {
			pktp->pkt_reason = CMD_TIMEOUT; /* To be checked up */
		} else if (ata_pktp->ap_flags & AP_BUS_RESET) {
			pktp->pkt_reason = CMD_RESET;
		} else {
			pktp->pkt_reason = CMD_ABORTED;
		}
	} else {
		pktp->pkt_reason = CMD_CMPLT;
	}

	/*
	 * callback
	 */
	if (do_callback) {
		if (pktp->pkt_comp) {
			(*pktp->pkt_comp)(pktp);
		}
	}
}


/*
 * packet "process interrupt" callback
 * returns STATUS_PKT_DONE when a packet has completed.
 * returns STATUS_PARTIAL when an event occurred but no packet completed
 * returns STATUS_NOINTR when there was no interrupt status
 */
static int
ata_disk_intr(struct ata_controller *ata_ctlp, struct ata_pkt *ata_pktp)
{
	struct dcd_pkt *dcd_pkt =
	    (struct dcd_pkt *)APKT2DPKT(ata_pktp);
	struct dcd_address *ap = &(dcd_pkt->pkt_address);
	struct ata_drive *ata_drvp =
	    CTL2DRV(ata_ctlp, ap->da_target, ap->da_lun);
	int chno = ata_drvp->ad_channel;
	uchar_t	status;


	ADBG_TRACE(("ata_disk_intr entered\n"));
	ADBG_TRANSPORT(("ata_disk_intr: pkt = 0x%p\n", (void *)ata_pktp));

	/*
	 * this clears the interrupt
	 */
	status = ddi_get8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_status[chno]);

	/*
	 * clear interrupt
	 */
	if (ddi_get8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2)) & 2) {
		ddi_put8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno), 6);
	}
	ddi_put8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2),
	    (ddi_get8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2))  | 6));

	/*
	 * Clear the interrupt
	 * if pending  by reading config reg
	 */
	ata_ctlp->clear_interrupt(ata_ctlp, chno);

	/*
	 * if busy, can't be our interrupt
	 */
	if (status & ATS_BSY) {
		return (STATUS_NOINTR);
	}

	/*
	 * check for errors
	 */
	if (status & ATS_ERR) {
		ata_pktp->ap_flags |= AP_ERROR;
		goto out;
	}

	/*
	 * check for final interrupt on write command
	 */
	if (!ata_pktp->ap_gcmd.cmd_resid) {
		goto out;
	}

	if (ata_drvp->ad_dmamode & DCD_DMA_MODE) {
		ata_drvp->ad_dmamode &= 0x7f;
		ata_pktp->ap_gcmd.cmd_resid = 0;
		dcd_pkt->pkt_state |= STATE_XFERRED_DATA;
		ddi_put8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno),
		    (ddi_get8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno)) & 0xfe));
		goto out;
	}

	/*
	 * perform data transfer
	 */
	if (ata_pktp->ap_flags & (AP_WRITE|AP_READ)) {
		if (ata_disk_xfer_data(ata_ctlp, chno) == FAILURE) {
			ata_pktp->ap_flags |= AP_ERROR;
			goto out;
		}
	}

	if ((ata_pktp->ap_gcmd.cmd_resid) || (ata_pktp->ap_flags & AP_WRITE)) {
		return (STATUS_PARTIAL);
	}

out:
	if (ata_pktp->ap_flags & AP_ERROR) {
		if (ata_drvp->ad_dmamode & DCD_DMA_MODE) {
			/*
			 * Stop the DMA engine.
			 */
			ata_drvp->ad_dmamode &= 0x7f;
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

	/*
	 * re-enable intrs after polling packet
	 */
	if (ata_pktp->ap_flags & AP_POLL) {
		ddi_put8(ata_ctlp->ata_datap1[chno],
		    ata_ctlp->ac_devctl[chno], ATDC_D3);
		ata_ctlp->enable_intr(ata_ctlp, chno);
	}

	return (STATUS_PKT_DONE);
}

/*
 * transfer data to/from the drive
 */
static int
ata_disk_xfer_data(struct ata_controller *ata_ctlp, int chno)
{
	struct ata_pkt	*ata_pktp = ata_ctlp->ac_active[chno];
	struct dcd_pkt *pktp = APKT2DPKT(ata_pktp);
	struct dcd_cmd *dcd_cmd = (struct dcd_cmd *)(pktp->pkt_cdbp);
	int count;

	ADBG_TRACE(("ata_disk_xfer_data entered\n"));

	count = min(ata_pktp->ap_gcmd.cmd_resid, ata_pktp->ap_bytes_per_block);
	if (count == 0) {
		return (SUCCESS);
	}


	/*
	 * Before transferring data DRQ needs to be set and BSY needs to be
	 * 0. Wait time = 30S.
	 */
	if (ata_wait(ata_ctlp->ata_datap1[chno], ata_ctlp->ac_altstatus[chno],
	    ATS_DRQ, ATS_BSY, IGN_ERR, 1000, 30000) == FAILURE) {
		ADBG_WARN(("ata_disk_xfer_data: no DRQ\n"));
		cmn_err(CE_NOTE, " resid = %x\n",
		    (int)ata_pktp->ap_gcmd.cmd_resid);
		return (FAILURE);
	}

	ADBG_TRANSPORT(("ata_disk_xfer_data: %s 0x%x bytes, addr = 0x%p\n",
	    ((ata_pktp->ap_flags & AP_READ) ? "READ":"WRITE"),
	    count, (void *)ata_pktp->ap_v_addr));

	/*
	 * read or write count bytes
	 */
	if (ata_pktp->ap_flags & AP_READ) {
		ddi_rep_get16(ata_ctlp->ata_datap[chno], (ushort_t *)
		    ata_pktp->ap_v_addr, (uint16_t *)
		    ata_ctlp->ac_data[chno], (count >> 1), 0);
	} else {
		ddi_rep_put16(ata_ctlp->ata_datap[chno], (ushort_t *)
		    ata_pktp->ap_v_addr, (uint16_t *)
		    ata_ctlp->ac_data[chno], (count >> 1), 0);
	}

	/*
	 * update counts...
	 */
	if (dcd_cmd->cmd == 0xec) {
		change_endian((uchar_t *)ata_pktp->ap_v_addr, count);
	}
	ata_pktp->ap_v_addr += count;
	ata_pktp->ap_gcmd.cmd_resid -= count;

	pktp->pkt_state |= STATE_XFERRED_DATA;

	return (SUCCESS);
}


#define	LOOP_COUNT	10000

int
ata_disk_set_rw_multiple(struct ata_drive *ata_drvp)
{
	struct ata_controller	*ata_ctlp = ata_drvp->ad_ctlp;
	int i;
	uchar_t laststat;
	int chno = ata_drvp->ad_channel;

	/*
	 * set drive number
	 */
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_drvhd[chno], ata_drvp->ad_drive_bits);
	/*
	 * disable interrupts
	 */
	ddi_put8(ata_ctlp->ata_datap1[chno],
	    ata_ctlp->ac_devctl[chno], ATDC_NIEN | ATDC_D3);

	/*
	 * send the command
	 */
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_count[chno], ata_drvp->ad_block_factor);
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_cmd[chno], ATC_SETMULT);

	/*
	 * wait for not busy
	 */
	if (ata_wait(ata_ctlp->ata_datap1[chno], ata_ctlp->ac_altstatus[chno],
	    0, ATS_BSY, IGN_ERR, 30, 400000) == FAILURE) {
		ADBG_WARN(("at_disk_set_rw_multiple: BSY too long!\n"));
		return (FAILURE);
	}

	/*
	 * Wait for DRDY or error
	 */
	for (i = 0; i < LOOP_COUNT; i++) {
		if (((laststat = ddi_get8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_status[chno])) & (ATS_DRDY | ATS_ERR))
		    != 0) {
			break;
		}
		drv_usecwait(10);
	}

	/*
	 * check for timeout
	 */
	if (i == LOOP_COUNT) {
		ADBG_WARN(("at_disk_set_rw_multiple: no DRDY!\n"));
		return (FAILURE);
	}

	/*
	 * check for error
	 */
	if (laststat & ATS_ERR) {
		ADBG_WARN(("at_disk_set_rw_multiple: status has ERR!\n"));
		return (FAILURE);
	}
	/*
	 * set drive number
	 */
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_drvhd[chno], ata_drvp->ad_drive_bits);
	/*
	 * enable interrupts
	 */
	ddi_put8(ata_ctlp->ata_datap1[chno],
	    ata_ctlp->ac_devctl[chno], ATDC_D3);

	return (SUCCESS);
}

static void
ata_disk_tran_destroy_pkt(struct dcd_address *ap, struct dcd_pkt *pkt)
{
	struct ata_pkt *ata_pktp = PKT2APKT(pkt);

	/*
	 * if (pkt->pkt_scbp) {
	 *	kmem_free(pkt->pkt_scbp, pkt->pkt_scblen);
	 * }
	 * if (pkt->pkt_cdbp) {
	 *	kmem_free(pkt->pkt_cdbp, pkt->pkt_cdblen);
	 * }
	 */

	if (ata_pktp->ap_gcmd.cmd_dma_handle) {
		/*
		 * release the old DMA resources
		 */
		ata_dmafree(ata_pktp);
	}
	if (PKT2APKT(pkt)) {
		kmem_free(PKT2APKT(pkt), sizeof (struct ata_pkt));
	}
	dcd_hba_pkt_free(ap, pkt);
}

/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/debug.h>
#include <sys/note.h>
#include <sys/dada/adapters/ata_common.h>
#include <sys/dada/adapters/ata_disk.h>
#include <sys/dada/adapters/atapi.h>


#define	ATA_MODULE_NAME  "ATA controller Driver"
/*
 * Solaris Entry Points.
 */
static int ata_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int ata_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int ata_power(dev_info_t *dip, int component, int level);
static int ata_bus_ctl(dev_info_t *d,
		dev_info_t *r, ddi_ctl_enum_t o, void *a, void *v);
static uint_t ata_intr(caddr_t arg);

/*
 * GHD Entry points
 */
static int ata_get_status(void *hba_handle, void *intr_status, int chno);
static void ata_process_intr(void *hba_handle, void *intr_status, int chno);
static int ata_hba_start(void *handle, gcmd_t *cmdp);
static void ata_hba_complete(void *handle, gcmd_t *cmdp, int do_callback);
static int ata_timeout_func(void *hba_handle,
		gcmd_t  *gcmdp, gtgt_t  *gtgtp, gact_t  action, int calltype);

/*
 * Local Function Prototypes
 */
static struct ata_controller *ata_init_controller(dev_info_t *dip);
static void ata_destroy_controller(dev_info_t *dip);
static struct ata_drive *ata_init_drive(struct ata_controller *ata_ctlp,
		uchar_t targ, uchar_t lun);
static void ata_destroy_drive(struct ata_drive *ata_drvp);
static int ata_drive_type(struct ata_controller *ata_ctlp,
		ushort_t *secbuf, int chno, int tgtno);
static int ata_reset_bus(struct ata_controller *ata_ctlp, int chno);
static int ata_sleep_standby(struct ata_drive *ata_drvp, uchar_t cmd);
void ata_ghd_complete_wraper(struct ata_controller *ata_ctlp,
			struct ata_pkt *ata_pktp, int chno);
void
make_prd(gcmd_t *gcmdp, ddi_dma_cookie_t *cookie, int single_seg, int num_segs);
void write_prd(struct ata_controller *ata_ctlp, struct ata_pkt *ata_pktp);
int prd_init(struct ata_controller *ata_ctlp, int chno);
void change_endian(unsigned char *string, int length);
int ata_drive_initialize(struct ata_controller *ata_ctlp);
int ata_dmaget(dev_info_t   *dip, struct ata_pkt  *ata_pktp, struct buf *bp,
	int dma_flags, int (*callback)(), caddr_t arg);
void ata_dmafree(struct ata_pkt  *ata_pktp);
void ata_raise_power(struct ata_controller *ata_ctlp);
int  ata_lower_power(struct ata_controller *ata_ctlp);

/*
 * External Functions
 */
extern int ata_disk_set_rw_multiple(struct ata_drive *ata_drvp);
extern void cmd_init(struct ata_controller   *ata_ctlp);
extern void sil_init(struct ata_controller   *ata_ctlp);
extern void acersb_init(struct ata_controller   *ata_ctlp);


/*
 * Linked list of all the controllers
 */
struct ata_controller *ata_head = NULL;
kmutex_t ata_global_mutex;

/*
 * Local static data
 */
void *ata_state;

static	tmr_t	ata_timer_conf; /* single timeout list for all instances */
static	clock_t	ata_watchdog_usec = 100000; /* check timeouts every 100 ms */
int	ata_debug_attach = 0;
/*
 * external dependencies
 */
char _depends_on[] = "misc/scsi misc/dada";

/* bus nexus operations */
static struct bus_ops ata_bus_ops, *scsa_bus_ops_p;

static struct dev_ops	ata_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ddi_no_info,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	ata_attach,		/* attach */
	ata_detach,		/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	NULL,			/* bus operations */
	ata_power,		/* power */
	ddi_quiesce_not_supported,	/* quiesce */
};


/* driver loadable module wrapper */
static	struct modldrv modldrv = {
	&mod_driverops,		/* Type of module. This one is a driver */
	ATA_MODULE_NAME,	/* module name */
	&ata_ops,					/* driver ops */
};

static	struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

static ddi_device_acc_attr_t dev_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

static ddi_device_acc_attr_t dev_attr1 = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_BE_ACC,
	DDI_STRICTORDER_ACC
};

ddi_dma_attr_t ata_dma_attrs = {
	DMA_ATTR_V0,	/* attribute layout version */
	0x0ull,		/* address low - should be 0 (longlong) */
	0xffffffffull,	/* address high - 32-bit max range */
	0x00ffffffull,	/* count max - max DMA object size */
	4,		/* allocation alignment requirements */
	0x78,		/* burstsizes - binary encoded values */
	2,		/* minxfer - gran. of DMA engine */
	0x00ffffffull,	/* maxxfer - gran. of DMA engine */
	0xffffffffull,	/* max segment size (DMA boundary) */
	1,		/* scatter/gather list length */
	512,		/* granularity - device transfer size */
	0		/* flags, set to 0 */
};

extern int atapi_work_pio;
/*
 * warlock directives
 */
_NOTE(MUTEX_PROTECTS_DATA(ata_global_mutex, ata_head))
_NOTE(MUTEX_PROTECTS_DATA(ata_global_mutex, ata_controller::ac_next))
_NOTE(MUTEX_PROTECTS_DATA(ata_controller::ac_hba_mutex, \
				ata_controller::ac_actv_chnl))
_NOTE(MUTEX_PROTECTS_DATA(ata_controller::ac_hba_mutex, \
				ata_controller::ac_pending))
_NOTE(MUTEX_PROTECTS_DATA(ata_controller::ac_hba_mutex, \
				ata_controller::ac_polled_finish))

_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", ata_bus_ops))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", scsa_bus_ops_p))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", dcd_address ata_drive))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", ghd_target_instance))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", scsi_hba_tran::tran_tgt_private))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", scsi_status))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", scsi_cdb))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", scsi_device))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", dcd_device::dcd_dev))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", dcd_device::dcd_ident))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", dcd_hba_tran::tran_tgt_private))
_NOTE(SCHEME_PROTECTS_DATA("unique per pkt", ghd_cmd scsi_pkt dcd_pkt ata_pkt))
_NOTE(SCHEME_PROTECTS_DATA("unique per pkt", buf))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", \
				ata_controller::ac_intr_unclaimed))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", \
				ata_controller::ac_active))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", \
				ata_controller::ac_power_level))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", \
				ata_controller::ac_reset_done))

_NOTE(READ_ONLY_DATA(ata_watchdog_usec))

#ifdef ATA_DEBUG
int	ata_debug = 0
		/* | ADBG_FLAG_ERROR */
		/* | ADBG_FLAG_WARN */
		/* | ADBG_FLAG_TRACE */
		/* | ADBG_FLAG_INIT */
		/* | ADBG_FLAG_TRANSPORT */
		;
/*PRINTFLIKE1*/
void
ata_err(char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vcmn_err(CE_CONT, fmt, ap);
	va_end(ap);
}
#endif

int
_init(void)
{
	int err;

	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	if ((err = ddi_soft_state_init(&ata_state,
			sizeof (struct ata_controller), 0)) != 0) {
		return (err);
	}

	if ((err = scsi_hba_init(&modlinkage)) != 0) {
		ddi_soft_state_fini(&ata_state);
		return (err);
	}
	/*
	 * save pointer to SCSA provided bus_ops struct
	 */
	scsa_bus_ops_p = ata_ops.devo_bus_ops;

	/*
	 * make a copy of SCSA bus_ops
	 */
	ata_bus_ops = *(ata_ops.devo_bus_ops);

	/*
	 * Modify our bus_ops to call our routines.  Our implementation
	 * will determine if the device is ATA or ATAPI/SCSA and react
	 * accordingly.
	 */
	ata_bus_ops.bus_ctl = ata_bus_ctl;

	/*
	 * patch our bus_ops into the dev_ops struct
	 */
	ata_ops.devo_bus_ops = &ata_bus_ops;

	if ((err = mod_install(&modlinkage)) != 0) {
		scsi_hba_fini(&modlinkage);
		ddi_soft_state_fini(&ata_state);
	}

	/*
	 * Initialize the per driver timer info.
	 */
	ghd_timer_init(&ata_timer_conf, drv_usectohz(ata_watchdog_usec));

	mutex_init(&ata_global_mutex, NULL, MUTEX_DRIVER, NULL);
	return (err);
}

int
_fini(void)
{
	int err;

	/* CONSTCOND */
	if ((err = mod_remove(&modlinkage)) == 0) {
		mutex_destroy(&ata_global_mutex);
		ghd_timer_fini(&ata_timer_conf);
		scsi_hba_fini(&modlinkage);
		ddi_soft_state_fini(&ata_state);
	}
	return (err);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


/*
 * driver attach entry point
 */
static int
ata_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	struct ata_controller	*ata_ctlp;
	struct ata_drive	*ata_drvp;
	struct ata_pkt	*ata_pktp;
	uchar_t	targ, lun, lastlun;
	int	atapi_count = 0, disk_count = 0;
	int	instance;
	int	chno, pstate;
	char	*pm_comp[6];
	int	p_exist = TRUE;
	int	s_exist = TRUE;

	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	ADBG_TRACE(("ata_attach entered\n"));

	switch (cmd) {
	case DDI_ATTACH:
		/*
		 * initialize controller
		 */
		ata_ctlp = ata_init_controller(dip);

		if (ata_ctlp == NULL) {
			goto errout;
		}

		ata_ctlp->ac_flags |= AC_ATTACH_IN_PROGRESS;

		/*
		 * Invoke power management initialization routine for
		 * the controller
		 */
		ata_ctlp->ac_power_level = ATA_POWER_UNKNOWN;
		pstate = ata_ctlp->power_mgmt_initialize();
		if (pstate != -1) {
			int i = 1;
			/*
			 * Since pstate is not -1 the controller supports
			 * power management states. bit position 0 if set
			 * indicate D0 supported. Similar for other bits too.
			 */
			pm_comp[0] = "NAME=ide-controller";
			if (pstate & 0x08) { /* D3 supported */
				pm_comp[i] = PM_LEVEL_D3_STR;
				i++;
			}
			if (pstate & 0x04) { /* D2 supported */
				pm_comp[i] = PM_LEVEL_D2_STR;
				i++;
			}
			if (pstate & 0x02) { /* D1 supported */
				pm_comp[i] = PM_LEVEL_D1_STR;
				i++;
			}
			if (pstate & 0x01) { /* D0 supported */
				pm_comp[i] = PM_LEVEL_D0_STR;
				i++;
			}
			pm_comp[i] = NULL;
			if (ddi_prop_update_string_array(DDI_DEV_T_NONE,
			    dip, "pm-components", pm_comp, i) ==
			    DDI_PROP_SUCCESS) {
				(void) pm_raise_power(dip, 0, ATA_POWER_D0);
			}
		}

		/*
		 * initialize drives
		 */
		for (targ = 0; targ < ATA_MAXTARG; targ++) {
			ata_drvp = ata_init_drive(ata_ctlp, targ, 0);

			/*
			 * if a master device is not found don't bother
			 * looking for the slave.
			 */
			if (ata_drvp == NULL) {
				if (targ == 0) {
					targ++;
					p_exist = FALSE;
				}
				if (targ == 2) {
					targ++;
					s_exist = FALSE;
				}
				continue;
			}


			if (ATAPIDRV(ata_drvp)) {
				atapi_count++;
				lastlun = ata_drvp->ad_id.dcd_lastlun & 0x03;
				/* Initialize higher LUNs, if there are any */
				for (lun = 1; lun <= lastlun; lun++)
					(void) ata_init_drive(ata_ctlp,
					    targ, lun);
			} else {
				disk_count++;
				lastlun = 0;
				/* If it is ATA disks we don't have LUNs */
			}
		}
		if ((atapi_count == 0) && (disk_count == 0)) {
			if (ata_ctlp) {
				ata_ctlp->ac_flags &= ~AC_ATTACH_IN_PROGRESS;
			}
			return (DDI_SUCCESS);
		}

		/*
		 * Call the chip specific routine for initializing timing regs.
		 */
		ata_ctlp->init_timing_tables(ata_ctlp);

		/*
		 * Now we have an idea of
		 * what devies exist now program the
		 * read ahead count
		 */

		ata_ctlp->program_read_ahead(ata_ctlp);

		/*
		 * initialize atapi/ata_dsk modules if we have at least
		 * one drive of that type.
		 */
		if (atapi_count) {
			if (atapi_init(ata_ctlp) != SUCCESS)
				goto errout;
			ata_ctlp->ac_flags |= AC_ATAPI_INIT;
		}

		if (disk_count) {
			if (ata_disk_init(ata_ctlp) != SUCCESS) {
				goto errout;
			}
			ata_ctlp->ac_flags |= AC_DISK_INIT;
		}

		/*
		 * Obtain the cable speeds for both the channels.
		 */
		ata_ctlp->ac_speed[0] =
		    ata_ctlp->get_speed_capabilities(ata_ctlp, 0);
		ata_ctlp->ac_speed[1] =
		    ata_ctlp->get_speed_capabilities(ata_ctlp, 1);

		/*
		 * get target(n)-dcd-option if it exists
		 */
		ata_ctlp->ac_dcd_options =
		    ddi_prop_get_int(DDI_DEV_T_ANY,
		    dip, 0, "dcd_options", -1);

		/* add interrupt handler to system */
		if (ddi_add_intr(dip, 0, &ata_ctlp->ac_iblock, NULL,
		    ata_intr, (caddr_t)ata_ctlp) != DDI_SUCCESS) {
			return (DDI_FAILURE);
		}

		/*
		 * Reset the primary channel if it has any devices.
		 * This is needed since set features will fail for
		 * some devices on SoutBridge without this reset.
		 * This should be ~5 micro Sec as per ata 5 spec.
		 * But we are waiting for a longer time for slower
		 * devices. This corresponds to moving from HSR0 state
		 * to HSR1 state in Host Soft Reset sequence.
		 *
		 * Interrupts were disabled up to this point to prevent
		 * the generation of spurious interrupts, since all
		 * previous commands polled to check for completion. We
		 * enable interrupts at this point however, because the
		 * implementation of the SET FEATURES command makes use
		 * of interrupts in the case of the CMD chip. Since we
		 * have already added the handler, spurious interrupts
		 * should not be a problem.
		 */

		if (p_exist == TRUE) {
			ddi_put8(ata_ctlp->ata_datap1[0],
			    ata_ctlp->ac_devctl[0],
			    ATDC_D3| ATDC_NIEN|ATDC_SRST);
			drv_usecwait(10000);

			/* Clear soft reset, re-enable interrupts. */
			ddi_put8(ata_ctlp->ata_datap1[0],
			    ata_ctlp->ac_devctl[0], ATDC_D3);
			drv_usecwait(20000);
		}

		/* reset the secondary channel if it has any devices */

		if (s_exist == TRUE) {
			ddi_put8(ata_ctlp->ata_datap1[1],
			    ata_ctlp->ac_devctl[1],
			    ATDC_D3| ATDC_NIEN|ATDC_SRST);
			drv_usecwait(10000);

			/* re-enable interrupts, clear soft reset */
			ddi_put8(ata_ctlp->ata_datap1[1],
			    ata_ctlp->ac_devctl[1], ATDC_D3);
			drv_usecwait(20000);
		}

		/*
		 * Set the transfer mode for all the drives.
		 */
		if (ata_drive_initialize(ata_ctlp) != SUCCESS) {
			if (ata_debug_attach) {
				cmn_err(CE_NOTE, "drive initialization failed");
			}
			goto errout;
		}
		ddi_report_dev(dip);
		ata_ctlp->ac_flags &= ~AC_ATTACH_IN_PROGRESS;
		/*
		 * Add to the ctlr list for debugging
		 */
		mutex_enter(&ata_global_mutex);
		if (ata_head) {
			ata_ctlp->ac_next = ata_head;
			ata_head = ata_ctlp;
		} else {
			ata_head = ata_ctlp;
		}
		mutex_exit(&ata_global_mutex);

		ata_ctlp->ac_cccp[0] = &ata_ctlp->ac_ccc[0];
		ata_ctlp->ac_cccp[1] = &ata_ctlp->ac_ccc[1];
		/*
		 * Clear any pending intterupt
		 */
		(void) ddi_get8(ata_ctlp->ata_datap[0],
		    ata_ctlp->ac_status[0]);
		(void) ddi_get8(ata_ctlp->ata_datap[1],
		    ata_ctlp->ac_status[1]);

		/*
		 * Invoke the controller specific routine for clearing intr.
		 * Value of 2 as the second argument indicates clear int for
		 * both channels
		 */
		ata_ctlp->clear_interrupt(ata_ctlp, 2);

		/*
		 * Clear all the interrupt pending status indicated in DMA
		 * status register.
		 */
		chno = 0;
		ddi_put8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2),
		    (ddi_get8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2))  | 6));

		chno = 1;
		ddi_put8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2),
		    (ddi_get8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2))  | 6));

		/*
		 * Enable the interrupts for both the channels
		 * as OBP disables it.
		 */
		ata_ctlp->enable_intr(ata_ctlp, 2);

		return (DDI_SUCCESS);

	case DDI_RESUME:
		instance = ddi_get_instance(dip);
		ata_ctlp = ddi_get_soft_state(ata_state, instance);

		ASSERT(ata_ctlp != NULL);
		if ((ata_ctlp->ac_vendor_id == ASBVID) &&
		    (ata_ctlp->ac_device_id == ASBDID ||
		    ata_ctlp->ac_device_id == ASBDID1575)) {
			acersb_init(ata_ctlp);
		}

		ata_ctlp->ac_suspended = 0;

		ata_ctlp->ac_reset_done = 0;
		if (ata_ctlp->ac_power_level != ATA_POWER_UNKNOWN)
			(void) pm_raise_power(dip, 0, ATA_POWER_D0);
		/*
		 * Reset the channel and set all the targets in right mode
		 */
		if (!ata_ctlp->ac_reset_done) {
		chno = 0;
		if (ata_ctlp->ac_active[chno] != NULL) {
			ata_pktp = ata_ctlp->ac_active[chno];
			ata_drvp = CTL2DRV(ata_ctlp, ata_pktp->ap_targ, 0);
			(void) ghd_tran_reset_bus(&ata_ctlp->ac_ccc[chno],
			    ata_drvp->ad_gtgtp, NULL);
		} else {
			/*
			 * We cannot choose a arbit drive and call
			 * ghd_tran_reset_bus as there is a possibility that
			 * drive doesn't exist at all.
			 */
			(void) ata_reset_bus(ata_ctlp, chno);
		}
		chno = 1;
		if (ata_ctlp->ac_active[chno] != NULL) {
			ata_pktp = ata_ctlp->ac_active[chno];
			ata_drvp = CTL2DRV(ata_ctlp, ata_pktp->ap_targ, 0);
			(void) ghd_tran_reset_bus(&ata_ctlp->ac_ccc[chno],
			    ata_drvp->ad_gtgtp, NULL);
		} else {
			/*
			 * We cannot choose a arbit drive and call
			 * ghd_tran_reset_bus as there is a possibility that
			 * drive doesn't exist at all.
			 */
			(void) ata_reset_bus(ata_ctlp, chno);
		}
		} else {
			ata_ctlp->ac_reset_done = 0;
		}

		/*
		 * Enable the interrupts for both the channels
		 */
		ata_ctlp->enable_intr(ata_ctlp, 2);

		return (DDI_SUCCESS);

	default:

errout:
		if (ata_ctlp) {
			ata_ctlp->ac_flags &= ~AC_ATTACH_IN_PROGRESS;
		}
		(void) ata_detach(dip, DDI_DETACH);
		return (DDI_FAILURE);
	}
}

/*
 * driver detach entry point
 */
static int
ata_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int instance;
	struct	ata_controller *ata_ctlp, *ata;
	struct	ata_drive *ata_drvp;
	struct  ata_pkt *ata_pktp;
	int	i, j;

	ADBG_TRACE(("ata_detach entered\n"));

	switch (cmd) {
	case DDI_DETACH:
		instance = ddi_get_instance(dip);
		ata_ctlp = ddi_get_soft_state(ata_state, instance);

		if (!ata_ctlp)
			return (DDI_SUCCESS);

		/*
		 * destroy ata module
		 */
		if (ata_ctlp->ac_flags & AC_DISK_INIT)
			ata_disk_destroy(ata_ctlp);

		mutex_enter(&ata_global_mutex);
		if (ata_head == ata_ctlp) {
			ata_head = ata_ctlp->ac_next;
			ata_ctlp->ac_next = NULL;
		} else {
			for (ata = ata_head; ata; ata = ata->ac_next) {
				if (ata->ac_next == ata_ctlp) {
					ata->ac_next = ata_ctlp->ac_next;
					ata_ctlp->ac_next = NULL;
					break;
				}
			}
		}
		mutex_exit(&ata_global_mutex);

		/*
		 * destroy atapi module
		 */
		if (ata_ctlp->ac_flags & AC_ATAPI_INIT)
			atapi_destroy(ata_ctlp);

		/*
		 * destroy drives
		 */
		for (i = 0; i < ATA_MAXTARG; i++) {
			for (j = 0; j < ATA_MAXLUN; j++) {
				ata_drvp = CTL2DRV(ata_ctlp, i, j);
				if (ata_drvp != NULL)
					ata_destroy_drive(ata_drvp);
			}
		}

		/*
		 * destroy controller
		 */
		ata_destroy_controller(dip);

		ddi_prop_remove_all(dip);

		return (DDI_SUCCESS);
	case DDI_SUSPEND:
		/* Get the Controller Pointer */
		instance = ddi_get_instance(dip);
		ata_ctlp = ddi_get_soft_state(ata_state, instance);
		/*
		 * Disable the interrupt from the controller
		 */
		ata_ctlp->disable_intr(ata_ctlp, 2);

		/*
		 * There are no timeouts which are set to be untimedout
		 * The call to ghd_complete should take care of those
		 * untimeout for any command which has not timedout when
		 * a command is active.
		 */
		/* reset the first channel */
		if (ata_ctlp->ac_active[0] != NULL) {
			ata_pktp = ata_ctlp->ac_active[0];
			ata_drvp = CTL2DRV(ata_ctlp, ata_pktp->ap_targ, 0);
			(void) ghd_tran_reset_bus(&ata_ctlp->ac_ccc[0],
			    ata_drvp->ad_gtgtp, NULL);
		}

		if (ata_ctlp->ac_active[1] != NULL) {
			ata_pktp = ata_ctlp->ac_active[1];
			ata_drvp = CTL2DRV(ata_ctlp, ata_pktp->ap_targ, 0);
			(void) ghd_tran_reset_bus(&ata_ctlp->ac_ccc[1],
			    ata_drvp->ad_gtgtp, NULL);
		}

		/*
		 * Initial thought was that we need to drain all the commands
		 * That is not required as there will be only one command
		 * active on a channel at any point in time and if we blow it
		 * out it should be OK to retain the commands in the Queue.
		 * Hence there is no need to drain the command.
		 */


		/* Just Set the flag */
		ata_ctlp->ac_suspended = 1;

		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

/*
 * Nexus driver power entry point.
 */
static int
ata_power(dev_info_t *dip, int component, int level)
{
	int instance, rval;
	struct  ata_controller *ata_ctlp;

	instance = ddi_get_instance(dip);
	ata_ctlp = ddi_get_soft_state(ata_state, instance);
	if (ata_ctlp == NULL)
		return (DDI_FAILURE);

	rval = ata_ctlp->power_entry_point(ata_ctlp, component, level);

	if (rval == FAILURE)
		return (DDI_FAILURE);

	return (DDI_SUCCESS);
}

/*
 * driver quiesce entry point
 */
static int
ata_quiesce(dev_info_t *dip)
{
	return (ata_detach(dip, DDI_DETACH));
}

/*
 * Nexus driver bus_ctl entry point
 */
/*ARGSUSED*/
static int
ata_bus_ctl(dev_info_t *d, dev_info_t *r, ddi_ctl_enum_t o, void *a, void *v)
{
	dev_info_t *tdip;
	int target_type, rc, len, drive_type;
	char buf[80];
	struct ata_drive *ata_drvp;
	int instance;
	struct ata_controller *ata_ctlp;

	instance = ddi_get_instance(d);
	ata_ctlp = ddi_get_soft_state(ata_state, instance);

	ADBG_TRACE(("ata_bus_ctl entered\n"));

	switch (o) {

	case DDI_CTLOPS_IOMIN:
		*((int *)v) = 2;
		return (DDI_SUCCESS);

	case DDI_CTLOPS_DMAPMAPC:
	case DDI_CTLOPS_REPORTINT:
	case DDI_CTLOPS_REGSIZE:
	case DDI_CTLOPS_NREGS:
	case DDI_CTLOPS_SIDDEV:
	case DDI_CTLOPS_SLAVEONLY:
	case DDI_CTLOPS_AFFINITY:
	case DDI_CTLOPS_POKE:
	case DDI_CTLOPS_PEEK:

		/*
		 * These ops shouldn't be called by a target driver
		 */
		ADBG_ERROR(("ata_bus_ctl: %s%d: invalid op (%d) "
		    "from %s%d\n", ddi_driver_name(d),
		    ddi_get_instance(d), o, ddi_driver_name(r),
		    ddi_get_instance(r)));
		return (DDI_FAILURE);


	case DDI_CTLOPS_REPORTDEV:
	case DDI_CTLOPS_INITCHILD:
	case DDI_CTLOPS_UNINITCHILD:

		/* these require special handling below */
		break;

	default:
		return (ddi_ctlops(d, r, o, a, v));
	}

	/*
	 * get targets dip
	 */
	if ((o == DDI_CTLOPS_INITCHILD) ||
	    (o == DDI_CTLOPS_UNINITCHILD)) {

		tdip = (dev_info_t *)a; /* Getting the childs dip */
	} else {
		tdip = r;
	}

	len = 80;
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, tdip, DDI_PROP_DONTPASS,
	    "class_prop", buf, &len) == DDI_PROP_SUCCESS) {
		if (strcmp(buf, "ata") == 0) {
			target_type = ATA_DEV_DISK;
		} else if (strcmp(buf, "atapi") == 0) {
			target_type = ATA_DEV_ATAPI;
		} else {
			ADBG_WARN(("ata_bus_ctl: invalid target class %s\n",
			    buf));
			return (DDI_FAILURE);
		}
	} else {
		return (DDI_FAILURE);
	}


	if (o == DDI_CTLOPS_INITCHILD) {
		int targ, lun;

		if (ata_ctlp == NULL) {
			ADBG_WARN(("ata_bus_ctl: failed to find ctl struct\n"));
			return (DDI_FAILURE);
		}

		/*
		 * get (target,lun) of child device
		 */
		len = sizeof (int);
		if (ATA_INTPROP(tdip, "target", &targ, &len) != DDI_SUCCESS) {
			ADBG_WARN(("ata_bus_ctl: failed to get targ num\n"));
			return (DDI_FAILURE);
		}
		if (ATA_INTPROP(tdip, "lun", &lun, &len) != DDI_SUCCESS) {
			lun = 0;
		}

		if ((targ < 0) || (targ >= ATA_MAXTARG) ||
		    (lun < 0) || (lun >= ATA_MAXLUN)) {
			return (DDI_FAILURE);
		}

		ata_drvp = CTL2DRV(ata_ctlp, targ, lun);

		if (ata_drvp == NULL) {
			return (DDI_FAILURE);	/* no drive */
		}

		/*
		 * get type of device
		 */
		if (ATAPIDRV(ata_drvp)) {
			drive_type = ATA_DEV_ATAPI;
		} else {
			drive_type = ATA_DEV_DISK;
		}

		if (target_type != drive_type) {
			return (DDI_FAILURE);
		}

		/* save pointer to drive struct for ata_disk_bus_ctl */
		ddi_set_driver_private(tdip, ata_drvp);
	}

	if (target_type == ATA_DEV_ATAPI) {
		rc = scsa_bus_ops_p->bus_ctl(d, r, o, a, v);
	} else {
		rc = ata_disk_bus_ctl(d, r, o, a, v, ata_drvp);
	}


	return (rc);
}

/*
 * driver interrupt handler
 */
static uint_t
ata_intr(caddr_t arg)
{
	struct ata_controller *ata_ctlp;
	int one_shot = 1, ret1, ret2;
	unsigned char chno;

	ret1 = ret2 = DDI_INTR_UNCLAIMED;
	ata_ctlp = (struct ata_controller *)arg;


	if (ata_ctlp->ac_flags & AC_ATTACH_IN_PROGRESS) {
		goto clear_intr;
	}
	if (ata_ctlp->ac_simplex == 1) {
		mutex_enter(&ata_ctlp->ac_hba_mutex);
		if (ata_ctlp->ac_actv_chnl != 2) {
			int chno = ata_ctlp->ac_actv_chnl;

			mutex_exit(&ata_ctlp->ac_hba_mutex);
			ret1 = ghd_intr(&ata_ctlp->ac_ccc[chno],
			    (void *)&one_shot, chno);
			if (ret1 == DDI_INTR_CLAIMED) {
				return (ret1);
			}
			mutex_enter(&ata_ctlp->ac_hba_mutex);
		}
		mutex_exit(&ata_ctlp->ac_hba_mutex);
	} else {
		/*
		 * We don't know which channel is the intr for
		 * so check and process both the channels
		 */
		ret1 = ghd_intr(&ata_ctlp->ac_ccc[0], (void *)&one_shot, 0);
		if (ret1 != DDI_INTR_CLAIMED) {
			one_shot = 1;
			ret2 = ghd_intr(&ata_ctlp->ac_ccc[1],
			    (void *)&one_shot, 1);
		}
	}

	if ((ret1 == DDI_INTR_UNCLAIMED) &&
	    (ret2 == DDI_INTR_UNCLAIMED)) {
clear_intr:
		(void) ddi_get8(ata_ctlp->ata_datap[0],
		    ata_ctlp->ac_status[0]);
		(void) ddi_get8(ata_ctlp->ata_datap[1],
		    ata_ctlp->ac_status[1]);
		if ((ata_ctlp->ac_vendor_id == ASBVID) &&
		    (ata_ctlp->ac_device_id == ASBDID ||
		    ata_ctlp->ac_device_id == ASBDID1575)) {
			ata_ctlp->ac_intr_unclaimed++;
			return (DDI_INTR_CLAIMED);
		}

		ata_ctlp->clear_interrupt(ata_ctlp, 2);

		/*
		 * Clear all the interrupt pending status indicated in DMA
		 * status register.
		 */
		chno = 0;
		ddi_put8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2),
		    (ddi_get8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2))  | 6));

		chno = 1;
		ddi_put8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2),
		    (ddi_get8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2))  | 6));
		ata_ctlp->ac_intr_unclaimed++;
	}
	return (DDI_INTR_CLAIMED);
}

/*
 * GHD ccc_get_status callback
 */
/* ARGSUSED */
static int
ata_get_status(void *hba_handle, void *intr_status, int chno)
{
	struct ata_controller *ata_ctlp = NULL;
	struct ata_pkt  *active_pktp = NULL;
	struct ata_drive *ata_drvp = NULL;
	struct scsi_pkt *spktp;
	int val;


	ADBG_TRACE(("ata_get_status entered\n"));
	ata_ctlp = (struct ata_controller *)hba_handle;

	if (ata_ctlp->ac_active[chno]) {
		active_pktp = ata_ctlp->ac_active[chno];
	} else {
		return (FALSE);
	}


	ata_drvp = CTL2DRV(ata_ctlp, active_pktp->ap_targ, 0);

	if (ata_drvp->ad_cur_disk_mode == DMA_MODE) {
		val = ddi_get8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 8 * chno +2)) & 0x4;
		if (!val) {
			if (ata_drvp->ad_flags & AD_ATAPI) {
				spktp = APKT2SPKT(active_pktp);
				if ((!(active_pktp->ap_flags & AP_DMA)) ||
				    ((!(ata_drvp->ad_flags & AD_NO_CDB_INTR)) &&
				    (!(spktp->pkt_state & STATE_SENT_CMD)))) {
					val =
					    ddi_get8(ata_ctlp->ata_datap1[chno],
					    ata_ctlp->ac_altstatus[chno]);
					if (val & ATS_BSY) {
						return (FALSE);
					}
				} else {
					/*
					 * None of the special ATAPI cases
					 */
					return (FALSE);
				}
			} else {
				/*
				 * For CMD controller check the intr. pending
				 * status for non-data transfer commands.
				 * For non-data xfer commands check the bits
				 * at offset 0x50,0x57 for intr status.
				 */
				if (((ata_ctlp->ac_vendor_id == CMDVID) &&
				    (ata_ctlp->ac_device_id != SIL680)) &&
				    ((active_pktp->ap_cmd == ATC_STANDBY) ||
				    (active_pktp->ap_cmd == ATC_IDLE))) {

					if (ata_ctlp->get_intr_status
					    (ata_ctlp, chno) == SUCCESS) {
						return (TRUE);
					}
				}
				return (FALSE);
			}
		}
	} else {
		val = ddi_get8(ata_ctlp->ata_datap1[chno],
		    ata_ctlp->ac_altstatus[chno]);
		if (val & ATS_BSY) {
			return (FALSE);
		}
	}

	return (TRUE);
}

/*
 * GHD ccc_process_intr callback
 */
/* ARGSUSED */
static void
ata_process_intr(void *hba_handle, void *intr_status, int chno)
{
	struct ata_controller *ata_ctlp;
	struct ata_pkt	*active_pktp = NULL;
	int rc;

	ADBG_TRACE(("ata_process_intr entered\n"));

	ata_ctlp = (struct ata_controller *)hba_handle;

	if (ata_ctlp->ac_active[chno]) {
		active_pktp = ata_ctlp->ac_active[chno];
	}

	rc = active_pktp->ap_intr(ata_ctlp, active_pktp);

	if ((rc != STATUS_NOINTR) && (!(* (int *)intr_status)) &&
	    (!(active_pktp->ap_flags & AP_POLL))) {
		/*
		 * We are through the polled route and packet is non-polled.
		 */
		mutex_enter(&ata_ctlp->ac_hba_mutex);
		ata_ctlp->ac_polled_finish++;
		mutex_exit(&ata_ctlp->ac_hba_mutex);
	}

	/*
	 * check if packet completed
	 */
	if (rc == STATUS_PKT_DONE) {
		/*
		 * Be careful to only reset ac_active if it's
		 * the command we just finished.  An overlap
		 * command may have already become active again.
		 */
		if (ata_ctlp->ac_active[chno] == active_pktp) {
			ata_ctlp->ac_active[chno] = NULL;
		}
		ata_ghd_complete_wraper(ata_ctlp, active_pktp, chno);
	}
}


/*
 * GHD ccc_hba_start callback
 */
static int
ata_hba_start(void *hba_handle, gcmd_t *cmdp)
{
	struct ata_controller *ata_ctlp;
	struct ata_pkt *ata_pktp;
	int rc, chno;

	ADBG_TRACE(("ata_hba_start entered\n"));

	ata_ctlp = (struct ata_controller *)hba_handle;
	ata_pktp = GCMD2APKT(cmdp);
	chno = ata_pktp->ap_chno;

	if (ata_ctlp->ac_power_level != ATA_POWER_UNKNOWN) {
		/*
		 * Don't allow the pm framework to power off the controller
		 * until we are sure the command has been completed.
		 */
		(void) pm_busy_component(ata_ctlp->ac_dip, 0);

		/* If we need to power on */
		if (ata_ctlp->ac_power_level != ATA_POWER_D0) {
			if (pm_raise_power(ata_ctlp->ac_dip, 0, ATA_POWER_D0) !=
			    DDI_SUCCESS) {
				(void) pm_idle_component(ata_ctlp->ac_dip, 0);
				return (TRAN_FATAL_ERROR);
			}
		}
	}

	if (ata_ctlp->ac_active[chno] != NULL) {
		if (ata_ctlp->ac_power_level != ATA_POWER_UNKNOWN)
			(void) pm_idle_component(ata_ctlp->ac_dip, 0);
		return (TRAN_BUSY);
	}

	ata_ctlp->ac_active[chno] = ata_pktp;

	if (ata_ctlp->ac_simplex == 1) {
		mutex_enter(&ata_ctlp->ac_hba_mutex);
		ata_ctlp->ac_pending[chno] = 1;
		if (ata_ctlp->ac_actv_chnl != 2) {
			mutex_exit(&ata_ctlp->ac_hba_mutex);
			return (TRAN_ACCEPT);
		}
		ata_ctlp->ac_actv_chnl = chno;
		mutex_exit(&ata_ctlp->ac_hba_mutex);
	}

	rc = ata_pktp->ap_start(ata_ctlp, ata_pktp);

	if (rc != TRAN_ACCEPT) {
		ata_ctlp->ac_active[chno] = NULL;
	}

	return (rc);
}

/*
 * GHD ccc_hba_complete callback
 */
/* ARGSUSED */
static void
ata_hba_complete(void *hba_handle, gcmd_t *cmdp, int do_callback)
{
	struct ata_controller *ata_ctlp;
	struct ata_pkt *ata_pktp;

	ADBG_TRACE(("ata_hba_complete entered\n"));

	ata_ctlp = (struct ata_controller *)hba_handle;
	ata_pktp = GCMD2APKT(cmdp);

	if (ata_pktp->ap_buf_addr != 0) {
		/*
		 * Just to cross confirm. - This condition shall always be true.
		 */
		if (((uintptr_t)ata_pktp->ap_v_addr) & 0x1) {
			/*
			 * Copy the data from our temp. buffer
			 */
			bcopy(ata_pktp->ap_buf_addr, ata_pktp->ap_v_addr,
			    ata_pktp->ap_count_bytes);
		} else {
			cmn_err(CE_WARN,
			"ata_hba_complete:Using temp. buffer for aligned addr");
		}
	}
	ata_pktp->ap_complete(ata_pktp, do_callback);

	if (ata_ctlp->ac_power_level != ATA_POWER_UNKNOWN) {
		/*
		 * We busied the component for each command that came through
		 * ata_hba_start we now must do the corresponding idle.
		 */
		(void) pm_idle_component(ata_ctlp->ac_dip, 0);
	}
}

/*
 * GHD ccc_timeout_func callback
 */
static int
ata_timeout_func(void *hba_handle, gcmd_t  *gcmdp,
	gtgt_t  *gtgtp, gact_t  action, int calltype)
{
	struct ata_controller *ata_ctlp;
	struct ata_drive *ata_drvp;
	struct ata_pkt *ata_pktp;
	int chno;
	char *prop_template = "target%x-dcd-options";
	char prop_str[32];
	uchar_t orig_mode;


	ADBG_TRACE(("ata_timeout_func entered\n"));

	ata_ctlp = (struct ata_controller *)hba_handle;
	ata_drvp = GTGTP2ATADRVP(gtgtp);
	chno = ata_drvp->ad_channel;

	/*
	 * This is a workaround for Seagate disk model ST39120A.
	 * ST39120A doen not generate a interrupt after the last block
	 * of data is transferred in firmware download command (even though
	 * the command has completed correctly).
	 * For this case, complete the command correctly.
	 */
	if (strncmp(ata_drvp->ad_id.dcd_model, "ST39120A", 8) == 0) {
		ata_pktp = ata_ctlp->ac_active[chno];
		if (ata_pktp != NULL) {
			if ((ata_pktp->ap_cmd == ATA_DOWNLOAD_MICROCODE) &&
			    (ata_pktp->ap_gcmd.cmd_resid == 0)) {

				ata_pktp->ap_flags &= ~(AP_TIMEOUT|AP_ABORT|
				    AP_FATAL|AP_BUS_RESET|AP_ERROR);
				ata_ctlp->ac_active[chno] = NULL;
				if (ata_reset_bus(ata_ctlp, chno) == FALSE) {
					ata_pktp->ap_flags |= AP_FATAL;
				}
				ata_ghd_complete_wraper(ata_ctlp,
				    ata_pktp, chno);
				/*
				 * Always return TRUE, even if reset had failed,
				 * otherwise ghd is going to call timeout
				 * routine again. Failure is indicated by
				 * returning FATAL for the packet.
				 */
				return (TRUE);
			}
		}
	}

	if (gcmdp != NULL) {
		ata_pktp = GCMD2APKT(gcmdp);
	} else {
		ata_pktp = NULL;
	}

	if ((ata_pktp != NULL) && (ata_drvp->ad_invalid) &&
	    ((action == GACTION_EARLY_ABORT) ||
	    (action == GACTION_EARLY_TIMEOUT) ||
	    (action == GACTION_RESET_BUS))) {
		ata_pktp->ap_flags |= AP_FATAL;
		ata_ghd_complete_wraper(ata_ctlp, ata_pktp, chno);
		return (FALSE);
	}

	switch (action) {
	case GACTION_EARLY_ABORT:
		/*
		 * abort before request was started
		 */
		if (calltype != GHD_NEWSTATE_TGTREQ)
			cmn_err(CE_WARN,
			    "timeout: early abort chno = %d targ = %d\n",
			    chno, ata_drvp->ad_targ);
		if (ata_pktp != NULL) {
			ata_pktp->ap_flags |= AP_ABORT;
			ata_ghd_complete_wraper(ata_ctlp, ata_pktp, chno);
		}
		return (TRUE);

	case GACTION_EARLY_TIMEOUT:
		/*
		 * timeout before request was started
		 */
		if (ata_pktp != NULL) {
			ata_pktp->ap_flags |= AP_TIMEOUT;
			ata_ghd_complete_wraper(ata_ctlp, ata_pktp, chno);
		}
		return (TRUE);

	case GACTION_RESET_TARGET:
		/* reset a device */
		if (calltype != GHD_NEWSTATE_TGTREQ)
			cmn_err(CE_WARN,
			    "timeout: reset target chno = %d targ = %d\n",
			    chno, ata_drvp->ad_targ);

		/* can't currently reset a single IDE disk drive */
		if (!(ata_drvp->ad_flags & AD_ATAPI)) {
			return (FALSE);
		}

		if (atapi_reset_drive(ata_drvp) == SUCCESS) {
			return (TRUE);
		} else {
			return (FALSE);
		}

	case GACTION_RESET_BUS:
		/*
		 * Step Down the mode of operation
		 * UDMA5 --> UDMA4 --> UDMA2 --> DMA-2.
		 */
		if (calltype != GHD_NEWSTATE_TGTREQ)
			cmn_err(CE_WARN,
			    "timeout: reset bus chno = %d targ = %d\n",
			    chno, ata_drvp->ad_targ);
		if (ata_pktp != NULL) {
			if (ata_drvp->ad_cur_disk_mode == DMA_MODE) {
				orig_mode = ata_drvp->ad_dmamode;
				if (ata_drvp->ad_dmamode &
				    ENABLE_ULTRA_FEATURE) {

					if ((ata_drvp->ad_dmamode &
					    DMA_BITS) > 4) {
						/*
						 * Bump down to multiword DMA-4
						 */
						ata_drvp->ad_dmamode =
						    ENABLE_ULTRA_FEATURE |
						    DCD_MULT_DMA_MODE4;

						ata_drvp->ad_dcd_options =
						    DCD_DMA_MODE |
						    DCD_ULTRA_ATA |
						    0x4;
					} else if ((ata_drvp->ad_dmamode &
					    DMA_BITS) > 2) {
						/*
						 * Bump down to UDMA-2
						 */
						ata_drvp->ad_dmamode =
						    ENABLE_ULTRA_FEATURE |
						    DCD_MULT_DMA_MODE2;

						ata_drvp->ad_dcd_options =
						    DCD_DMA_MODE |
						    DCD_ULTRA_ATA |
						    0x2;
					} else {
						/*
						 * Bump down to multiword DMA-2
						 */
						ata_drvp->ad_dmamode =
						    ENABLE_DMA_FEATURE | 0x2;
						ata_drvp->ad_dcd_options =
						    DCD_DMA_MODE | 0x2;
						/*
						 * Disable run ultra dma
						 */
						ata_drvp->ad_run_ultra = 0;
					}
				}
				if (orig_mode != ata_drvp->ad_dmamode) {
					(void) sprintf(prop_str, prop_template,
					    ata_pktp->ap_targ);
					/* BEGIN CSTYLED */
					(void) ddi_prop_modify(DDI_DEV_T_NONE,
					    ata_ctlp->ac_dip, 0, prop_str,
					    (caddr_t)&(ata_drvp->ad_dcd_options),
					    sizeof (int));
					/* END CSTYLED */
					cmn_err(CE_WARN,
					    "Changing the mode of"
					    " targ:%d to %s DMA"
					    " mode:%d\n", ata_drvp->ad_targ,
					    ata_drvp->ad_run_ultra?"Ultra" :
					    "Multiword", ata_drvp->ad_dmamode &
					    DMA_BITS);
				}
			}
		}

		/* reset bus */
		if (ata_reset_bus(ata_ctlp, chno) == FALSE) {
			if (ata_pktp != NULL) {
				ata_pktp->ap_flags |= AP_FATAL;
				ata_ghd_complete_wraper(ata_ctlp,
				    ata_pktp, chno);
			}
			return (FALSE);
		} else {
			return (TRUE);
		}

	case GACTION_INCOMPLETE:
		if (ata_pktp != NULL) {
			ata_pktp->ap_flags |= AP_TIMEOUT;
			ata_ghd_complete_wraper(ata_ctlp, ata_pktp, chno);
		}
		return (FALSE);

#ifdef DSC_OVERLAP_SUPPORT
	case GACTION_POLL:
		atapi_dsc_poll(ata_drvp);
		return (TRUE);
#endif
	}

	return (FALSE);
}

/*
 * reset the bus
 */
static int
ata_reset_bus(struct ata_controller *ata_ctlp, int chno)
{
	struct ata_pkt *ata_pktp;
	int  targ;
	struct ata_drive *ata_drvp;
	unsigned char status;


	ADBG_TRACE(("ata_reset_bus entered\n"));
	if (chno == 0)
		targ = 0;
	else
		targ = 2;
	/*
	 * If there is no drive 0 on the channel, no point in trying to
	 * issue a reset.
	 */
	ata_drvp = CTL2DRV(ata_ctlp, targ, 0);
	if (ata_drvp == NULL)
		return (FALSE);

	ata_pktp = ata_ctlp->ac_active[chno];

	/*
	 * Stop the DMA bus master operation for that channel
	 */
	ddi_put8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno),
	    (ddi_get8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno))  & 0xFE));
	/*
	 * Clear the dma error bit for channel
	 */
	ddi_put8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2),
	    (ddi_get8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2))  | 6));

	/*
	 * Clear the interrupts
	 */
	ata_ctlp->clear_interrupt(ata_ctlp, chno);

	if (chno == 1) {
		/*
		 * Enable the secondary channel
		 */
		ata_ctlp->enable_channel(ata_ctlp, 1);
	}

	/*
	 * Issue soft reset , wait for some time & enable intterupts
	 */
	ddi_put8(ata_ctlp->ata_datap1[chno],
	    ata_ctlp->ac_devctl[chno], ATDC_D3 | ATDC_NIEN | ATDC_SRST);
	drv_usecwait(15000);
	ddi_put8(ata_ctlp->ata_datap1[chno],
	    ata_ctlp->ac_devctl[chno], ATDC_D3);
	drv_usecwait(20000);
	/*
	 * Wait for the BSY to go low
	 */
	if (ata_wait(ata_ctlp->ata_datap[chno], ata_ctlp->ac_status[chno],
	    0, ATS_BSY, ATS_ERR, 3000, 10000) == FAILURE) {
		if (ata_drvp) {
			ata_drvp->ad_invalid = 1;
			cmn_err(CE_WARN, "Drive not ready in ata_reset_bus");
		}
		targ++;
		ata_drvp = CTL2DRV(ata_ctlp, targ, 0);
		if (ata_drvp)
			ata_drvp->ad_invalid = 1;
		return (FALSE);
	}


	/*
	 * Reset the chip channel.
	 */
	ata_ctlp->reset_chip(ata_ctlp, chno);

	/*
	 * After Resetting the drives connected to that channel should be
	 * set back to the requested mode. Otherwise the DMA willnot happen.
	 * It is done for all targets
	 */
	for (targ = chno * 2; targ < ((chno + 1) * 2); targ++) {
		ata_drvp = CTL2DRV(ata_ctlp, targ, 0);
		if (ata_drvp == NULL) {
			continue;
		}

		/*
		 * check whether any of the DMA has errored by checking the
		 * Bus Master IDE status register appropriate channel
		 */
		status = ddi_get8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr +
		    8 * ata_drvp->ad_channel + 2));
		if (status & 0x02) {
			ddi_put8(ata_ctlp->ata_cs_handle,
			    (uchar_t *)(ata_ctlp->ata_cs_addr +
			    8 * ata_drvp->ad_channel + 2), 0x02);
			status = ddi_get8(ata_ctlp->ata_cs_handle,
			    (uchar_t *)(ata_ctlp->ata_cs_addr +
			    8 * ata_drvp->ad_channel + 2));
		}
		ata_drvp->ad_dmamode &= 0x7f;
		if (((ata_drvp->ad_flags & AD_DISK) &&
		    (ata_ctlp->ac_dcd_options & DCD_DMA_MODE)) ||
		    ((ata_drvp->ad_flags & AD_ATAPI) &&
		    (ata_drvp->ad_cur_disk_mode == DMA_MODE))) {
			ata_ctlp->program_timing_reg(ata_drvp);
			if (ata_set_feature(ata_drvp, ATA_FEATURE_SET_MODE,
			    ata_drvp->ad_dmamode) != SUCCESS) {
				ata_drvp->ad_invalid = 1;
				if ((targ == 0) || (targ == 2)) {
					ata_drvp = CTL2DRV(ata_ctlp, targ+1, 0);
					if (ata_drvp)
						ata_drvp->ad_invalid = 1;
				} else {
					ata_drvp = CTL2DRV(ata_ctlp, targ-1, 0);
					if (ata_drvp)
						ata_drvp->ad_invalid = 1;
				}
				return (FALSE);
			}
			ata_drvp->ad_cur_disk_mode = DMA_MODE;
		} else {
			ata_ctlp->program_timing_reg(ata_drvp);
			if (ata_set_feature(ata_drvp, ATA_FEATURE_SET_MODE,
			    ENABLE_PIO_FEATURE |
			    (ata_drvp->ad_piomode & 0x07)) != SUCCESS) {
				ata_drvp->ad_invalid = 1;
				if ((targ == 0) || (targ == 2)) {
					ata_drvp = CTL2DRV(ata_ctlp, targ+1, 0);
					if (ata_drvp)
						ata_drvp->ad_invalid = 1;
				} else {
					ata_drvp = CTL2DRV(ata_ctlp, targ-1, 0);
					if (ata_drvp)
						ata_drvp->ad_invalid = 1;
				}
				return (FALSE);
			}

			if (ata_drvp->ad_block_factor > 1) {
				/*
				 * Program the block factor into the drive.
				 * If this fails, then go back to using a
				 * block size of 1.
				 */
				if ((ata_disk_set_rw_multiple(ata_drvp)
				    == FAILURE))
					ata_drvp->ad_block_factor = 1;
			}
		}
	}

	/*
	 * Clear the interrupt if pending  by reading config reg
	 */

	ddi_put8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2),
	    (ddi_get8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8*chno +2))  | 6));

	ata_ctlp->clear_interrupt(ata_ctlp, chno);

	if (ata_pktp != NULL) {
		ata_ctlp->ac_active[chno] = NULL;
		ata_pktp->ap_flags |= AP_BUS_RESET;
		ata_ghd_complete_wraper(ata_ctlp, ata_pktp, chno);
	}

	return (TRUE);
}

/*
 * Initialize a controller structure
 */
static struct ata_controller *
ata_init_controller(dev_info_t *dip)
{
	int instance, chno;
	struct ata_controller *ata_ctlp;
	uint8_t *ioaddr1, *ioaddr2;
	int hw_simplex, ata_simplex = 0, not_supported = 0;

	ADBG_TRACE(("ata_init_controller entered\n"));

	instance = ddi_get_instance(dip);

	/*
	 * allocate controller structure
	 */
	if (ddi_soft_state_zalloc(ata_state, instance) != DDI_SUCCESS) {
		ADBG_WARN(("ata_init_controller: soft_state_zalloc "
		    "failed\n"));
		return (NULL);
	}

	ata_ctlp = ddi_get_soft_state(ata_state, instance);

	if (ata_ctlp == NULL) {
		ADBG_WARN(("ata_init_controller: failed to find "
		    "controller struct\n"));
		return (NULL);
	}

	/*
	 * initialize controller
	 */
	ata_ctlp->ac_dip = dip;

	if (ddi_regs_map_setup(dip, 1, &ata_ctlp->ata_devaddr[0],
	    0, 8, &dev_attr1, &ata_ctlp->ata_datap[0]) != DDI_SUCCESS) {
		ddi_soft_state_free(ata_state, instance);
		return (NULL);

	}

	if (ddi_regs_map_setup(dip, 2, &ata_ctlp->ata_devaddr1[0],
	    0, 4, &dev_attr, &ata_ctlp->ata_datap1[0]) != DDI_SUCCESS) {
		ddi_soft_state_free(ata_state, instance);
		return (NULL);

	}



	if (ddi_regs_map_setup(dip, 3, &ata_ctlp->ata_devaddr[1],
	    0, 8, &dev_attr1, &ata_ctlp->ata_datap[1]) != DDI_SUCCESS) {
		ddi_soft_state_free(ata_state, instance);
		return (NULL);

	}

	if (ddi_regs_map_setup(dip, 4, &ata_ctlp->ata_devaddr1[1],
	    0, 4, &dev_attr, &ata_ctlp->ata_datap1[1]) != DDI_SUCCESS) {
		ddi_soft_state_free(ata_state, instance);
		return (NULL);

	}


	if (ddi_regs_map_setup(dip, 0, &ata_ctlp->ata_conf_addr,
	    0, 0, &dev_attr, &ata_ctlp->ata_conf_handle) != DDI_SUCCESS) {
		ddi_soft_state_free(ata_state, instance);
		return (NULL);
	}

	if (ddi_regs_map_setup(dip, 5, &ata_ctlp->ata_cs_addr,
	    0, 16, &dev_attr, &ata_ctlp->ata_cs_handle) != DDI_SUCCESS) {
		ddi_soft_state_free(ata_state, instance);
		return (NULL);
	}

	ata_ctlp->ac_vendor_id = ddi_get16(ata_ctlp->ata_conf_handle,
	    ((ushort_t *)ata_ctlp->ata_conf_addr + VENDORID));
	ata_ctlp->ac_device_id = ddi_get16(ata_ctlp->ata_conf_handle,
	    (ushort_t *)ata_ctlp->ata_conf_addr + DEVICEID);
	ata_ctlp->ac_revision = ddi_get8(ata_ctlp->ata_conf_handle,
	    (uchar_t *)ata_ctlp->ata_conf_addr + REVISION);

	if (ata_debug_attach) {
		cmn_err(CE_NOTE,
		    "uata: Vendor 0x%x, Device 0x%x, Revision 0x%x",
		    ata_ctlp->ac_vendor_id, ata_ctlp->ac_device_id,
		    ata_ctlp->ac_revision);
	}

	/*
	 * Initialize the controller specific routine.
	 */

	if (ata_ctlp->ac_vendor_id == CMDVID) {
		switch (ata_ctlp->ac_device_id) {
			case CMDDID:
			case CMD649:
				cmd_init(ata_ctlp);
				break;
			case SIL680:
				sil_init(ata_ctlp);
				break;
			default:
				not_supported = 1;
		}
	} else if ((ata_ctlp->ac_vendor_id == ASBVID) &&
	    (ata_ctlp->ac_device_id == ASBDID ||
	    ata_ctlp->ac_device_id == ASBDID1575)) {
		acersb_init(ata_ctlp);
	} else
		not_supported = 1;

	if (not_supported) {
		cmn_err(CE_WARN, "ata_controller[%d] - Unsupported Controller",
		    instance);
		cmn_err(CE_CONT, "\tVendor 0x%x, Device 0x%x, Revision 0x%x\n",
		    ata_ctlp->ac_vendor_id, ata_ctlp->ac_device_id,
		    ata_ctlp->ac_revision);
		ddi_soft_state_free(ata_state, instance);
		return (NULL);
	}

	ata_simplex = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, 0, "ata-simplex", 0);
	hw_simplex = ddi_get8(ata_ctlp->ata_cs_handle,
	    (uint8_t *)(ata_ctlp->ata_cs_addr + 2));
	if ((ata_simplex == 1) || (hw_simplex & 0x80)) {
		ata_ctlp->ac_simplex = 1;
		if (!ata_simplex)
			(void) ddi_prop_modify(DDI_DEV_T_NONE, dip, 0,
			    "ata-simplex", (caddr_t)&ata_ctlp->ac_simplex,
			    sizeof (int));
	} else {
		ata_ctlp->ac_simplex = 0;
	}
	ata_ctlp->ac_pending[0] = ata_ctlp->ac_pending[1] = 0;
	ata_ctlp->ac_actv_chnl = 2;


	ioaddr1 = (uint8_t *)(ata_ctlp->ata_devaddr[0]);
	ioaddr2 = (uint8_t *)(ata_ctlp->ata_devaddr1[0]);

	/*
	 * port addresses associated with ioaddr1
	 */
	ata_ctlp->ioaddr1[0]	= ioaddr1;
	ata_ctlp->ac_data[0]	= ioaddr1 + AT_DATA;
	ata_ctlp->ac_error[0]	= ioaddr1 + AT_ERROR;
	ata_ctlp->ac_feature[0]	= ioaddr1 + AT_FEATURE;
	ata_ctlp->ac_count[0]	= ioaddr1 + AT_COUNT;
	ata_ctlp->ac_sect[0]	= ioaddr1 + AT_SECT;
	ata_ctlp->ac_lcyl[0]	= ioaddr1 + AT_LCYL;
	ata_ctlp->ac_hcyl[0]	= ioaddr1 + AT_HCYL;
	ata_ctlp->ac_drvhd[0]	= ioaddr1 + AT_DRVHD;
	ata_ctlp->ac_status[0]	= ioaddr1 + AT_STATUS;
	ata_ctlp->ac_cmd[0]	= ioaddr1 + AT_CMD;

	/*
	 * port addresses associated with ioaddr2
	 */
	ata_ctlp->ioaddr2[0]		= ioaddr2;
	ata_ctlp->ac_altstatus[0] = ioaddr2 + 2;
	ata_ctlp->ac_devctl[0]    = ioaddr2 + 2;
	ata_ctlp->ac_drvaddr[0]   = ioaddr2 + AT_DRVADDR;



	ioaddr1 = (uint8_t *)(ata_ctlp->ata_devaddr[1]);
	ioaddr2 = (uint8_t *)(ata_ctlp->ata_devaddr1[1]);
	ata_ctlp->ioaddr1[1]	= ioaddr1;
	ata_ctlp->ac_data[1]	= ioaddr1 + AT_DATA;
	ata_ctlp->ac_error[1]	= ioaddr1 + AT_ERROR;
	ata_ctlp->ac_feature[1]	= ioaddr1 + AT_FEATURE;
	ata_ctlp->ac_count[1]	= ioaddr1 + AT_COUNT;
	ata_ctlp->ac_sect[1]	= ioaddr1 + AT_SECT;
	ata_ctlp->ac_lcyl[1]	= ioaddr1 + AT_LCYL;
	ata_ctlp->ac_hcyl[1]	= ioaddr1 + AT_HCYL;
	ata_ctlp->ac_drvhd[1]	= ioaddr1 + AT_DRVHD;
	ata_ctlp->ac_status[1]	= ioaddr1 + AT_STATUS;
	ata_ctlp->ac_cmd[1]	= ioaddr1 + AT_CMD;

	/*
	 * port addresses associated with ioaddr2
	 */
	ata_ctlp->ioaddr2[1]	  = ioaddr2;
	ata_ctlp->ac_altstatus[1] = ioaddr2 + 2;
	ata_ctlp->ac_devctl[1]    = ioaddr2 + 2;
	ata_ctlp->ac_drvaddr[1]   = ioaddr2 + AT_DRVADDR;


	/*
	 * Reset devices, but don't enable interrupts, since
	 * an interrupt handler hasn't been added to the
	 * system yet.
	 */
	ddi_put8(ata_ctlp->ata_datap1[0], ata_ctlp->ac_devctl[0],
	    ATDC_D3 | ATDC_NIEN | ATDC_SRST);
	drv_usecwait(30000);
	ddi_put8(ata_ctlp->ata_datap1[0], ata_ctlp->ac_devctl[0],
	    ATDC_D3 | ATDC_NIEN);
	drv_usecwait(30000);

	/*
	 * Enable the secondary channel and issue soft reset
	 * Enabling is required as a workaround to a CMD chip problem
	 */
	ata_ctlp->enable_channel(ata_ctlp, 1);

	ddi_put8(ata_ctlp->ata_datap1[1], ata_ctlp->ac_devctl[1],
	    ATDC_D3 | ATDC_NIEN | ATDC_SRST);
	drv_usecwait(30000);
	ddi_put8(ata_ctlp->ata_datap1[1], ata_ctlp->ac_devctl[1],
	    ATDC_D3 | ATDC_NIEN);
	drv_usecwait(90000);

	if (ddi_get_iblock_cookie(dip, 0, &ata_ctlp->ac_iblock) !=
	    DDI_SUCCESS) {
		cmn_err(CE_WARN, "ddi_get_iblock_cookie failed");
	}


	mutex_init(&ata_ctlp->ac_hba_mutex, NULL, MUTEX_DRIVER,
	    ata_ctlp->ac_iblock);

	for (chno = 0; chno < ATA_CHANNELS; chno ++) {
		/*
		 * initialize ghd
		 */
		GHD_WAITQ_INIT(&ata_ctlp->ac_ccc[chno].ccc_waitq, NULL, 1);

		/*
		 * initialize ghd
		 */
		if (!ghd_register("ata", &(ata_ctlp->ac_ccc[chno]), dip,
		    0, ata_ctlp, NULL, NULL, NULL, ata_hba_start,
		    ata_hba_complete, ata_intr, ata_get_status,
		    ata_process_intr, ata_timeout_func, &ata_timer_conf,
		    ata_ctlp->ac_iblock, chno)) {
			return (NULL);
		}
	}

	ata_ctlp->ac_flags |= AC_GHD_INIT;

	return (ata_ctlp);
}

/*
 * destroy a controller
 */
static void
ata_destroy_controller(dev_info_t *dip)
{
	int instance;
	struct ata_controller *ata_ctlp;

	ADBG_TRACE(("ata_destroy_controller entered\n"));

	instance = ddi_get_instance(dip);
	ata_ctlp = ddi_get_soft_state(ata_state, instance);

	if (ata_ctlp == NULL) {
		return;
	}

	/*
	 * destroy ghd
	 */
	if (ata_ctlp->ac_flags & AC_GHD_INIT) {
		ghd_unregister(&ata_ctlp->ac_ccc[0]);
		ghd_unregister(&ata_ctlp->ac_ccc[1]);
	}

	/*
	 * Free regs mappings
	 */
	ddi_regs_map_free(&ata_ctlp->ata_cs_handle);
	ddi_regs_map_free(&ata_ctlp->ata_conf_handle);
	ddi_regs_map_free(&ata_ctlp->ata_datap1[1]);
	ddi_regs_map_free(&ata_ctlp->ata_datap[1]);
	ddi_regs_map_free(&ata_ctlp->ata_datap1[0]);
	ddi_regs_map_free(&ata_ctlp->ata_datap[0]);

	/*
	 * Free the DMA allocations
	 */
	if (ata_ctlp->ata_prd_acc_handle[0]) {
		(void) ddi_dma_unbind_handle(ata_ctlp->ata_prd_dma_handle[0]);
		ddi_dma_mem_free(&ata_ctlp->ata_prd_acc_handle[0]);
		ddi_dma_free_handle(&ata_ctlp->ata_prd_dma_handle[0]);
	}

	if (ata_ctlp->ata_prd_acc_handle[1]) {
		(void) ddi_dma_unbind_handle(ata_ctlp->ata_prd_dma_handle[1]);
		ddi_dma_mem_free(&ata_ctlp->ata_prd_acc_handle[1]);
		ddi_dma_free_handle(&ata_ctlp->ata_prd_dma_handle[1]);
	}

	mutex_destroy(&ata_ctlp->ac_hba_mutex);

	/*
	 * destroy controller struct
	 */
	ddi_soft_state_free(ata_state, instance);
}

/*
 * initialize a drive
 */
static struct ata_drive *
ata_init_drive(struct ata_controller *ata_ctlp, uchar_t	targ, uchar_t lun)
{
	struct ata_drive *ata_drvp;
	struct dcd_identify *ata_idp;
	int drive_type, chno;

	ADBG_TRACE(("ata_init_drive entered, targ = %d, lun = %d\n",
	    targ, lun));

	if (ata_debug_attach) {
		cmn_err(CE_NOTE, "ata_init_drive: targ = %d, lun = %d",
		    targ, lun);
	}

	/*
	 * check if device already exists
	 */
	ata_drvp = CTL2DRV(ata_ctlp, targ, lun);

	if (ata_drvp != NULL) {
		return (ata_drvp);
	}

	/*
	 * allocate new device structure
	 */
	ata_drvp = (struct ata_drive *)
	    kmem_zalloc((unsigned)sizeof (struct ata_drive), KM_NOSLEEP);

	if (!ata_drvp) {
		return (NULL);
	}

	/*
	 * set up drive struct
	 */
	ata_drvp->ad_ctlp = ata_ctlp;
	ata_drvp->ad_targ = targ;
	ata_drvp->ad_lun = lun;

	if (targ & 1) {
		ata_drvp->ad_drive_bits = ATDH_DRIVE1;
	} else {
		ata_drvp->ad_drive_bits = ATDH_DRIVE0;
	}

	if (targ < 2) {
		chno = ata_drvp->ad_channel = 0;
	} else {
		chno = ata_drvp->ad_channel = 1;
	}

	/*
	 * Program drive/hd and feature register
	 */
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_drvhd[chno], ata_drvp->ad_drive_bits);
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_feature[chno], 0);

	/*
	 * get drive type, side effect is to collect
	 * IDENTIFY DRIVE data
	 */

	ata_idp = &ata_drvp->ad_id;
	drive_type = ata_drive_type(ata_ctlp, (ushort_t *)ata_idp, chno,
	    ata_drvp->ad_drive_bits);

	switch (drive_type) {

		case ATA_DEV_NONE:
			goto errout;
		case ATA_DEV_ATAPI:
			ata_drvp->ad_flags |= AD_ATAPI;
			break;
		case ATA_DEV_DISK:
			ata_drvp->ad_flags |= AD_DISK;
			break;
	}

	if (ATAPIDRV(ata_drvp)) {
		if (atapi_init_drive(ata_drvp) != SUCCESS)
			goto errout;
	} else {
		if (ata_disk_init_drive(ata_drvp) != SUCCESS)
			goto errout;
	}

	/*
	 * store pointer in controller struct
	 */
	ata_ctlp->ac_drvp[targ][lun] = ata_drvp;
	return (ata_drvp);

errout:
	ata_destroy_drive(ata_drvp);
	return (NULL);
}

/*
 * destroy a drive
 */
static void
ata_destroy_drive(struct ata_drive *ata_drvp)
{
	ADBG_TRACE(("ata_destroy_drive entered\n"));

	/*
	 * interface specific clean-ups
	 */
	if (ata_drvp->ad_flags & AD_ATAPI) {
		atapi_destroy_drive(ata_drvp);
	} else if (ata_drvp->ad_flags & AD_DISK) {
		ata_disk_destroy_drive(ata_drvp);
	}

	/*
	 * free drive struct
	 */
	kmem_free(ata_drvp, (unsigned)sizeof (struct ata_drive));

}

/*
 * ata_drive_type()
 *
 * The timeout values and exact sequence of checking is critical
 * especially for atapi device detection, and should not be changed lightly.
 */
static int
ata_drive_type(struct ata_controller *ata_ctlp, ushort_t *secbuf, int chno,
    int tgtno)
{
	uint_t ch, cl, status;
	uint_t sc, sn;
	uint_t drive_type;
	int i;

	ADBG_TRACE(("ata_drive_type entered\n"));

	/*
	 * Issue a soft reset to make sure the device is not busy.
	 * This should be ~5 micro Sec as per ata 5 spec.
	 * But we are waiting for a longer time for slower
	 * devices. This corresponds to moving from HSR0 state
	 * to HSR1 state in Host Soft Reset sequence.
	 */

	ddi_put8(ata_ctlp->ata_datap1[chno],
	    ata_ctlp->ac_devctl[chno], ATDC_D3 | ATDC_NIEN | ATDC_SRST);
	drv_usecwait(10000);

	/*
	 * Clear the soft reset and leave interrupts disabled, since
	 * no handler has been added to the system at this point.
	 */
	ddi_put8(ata_ctlp->ata_datap1[chno], ata_ctlp->ac_devctl[chno],
	    ATDC_D3 | ATDC_NIEN);
	drv_usecwait(20000);

	/*
	 * Wait to for busy to go down. Southbridge will have error after reset
	 * if there are no devices attached to the channel, so only fail if
	 * busy is still high and and error does not occur. We will wait
	 * for up to 30 seconds for the device busy to go down or for an error
	 * to occur.
	 */

	i = ata_wait(ata_ctlp->ata_datap[chno], ata_ctlp->ac_status[chno],
	    0, ATS_BSY, ATS_ERR, 500, 60000);

	if (ata_debug_attach) {

		if (i == FAILURE) {
			cmn_err(CE_WARN,
			    "Drive busy after inital reset chno = %d, %s", chno,
			    (tgtno == ATDH_DRIVE0) ? "master" : "slave");
			return (ATA_DEV_NONE);
		}

		if (i > 1000) {
			cmn_err(CE_NOTE,
			    "chno %d, %s recovered from reset in %d ms",
			    chno, (tgtno == ATDH_DRIVE0) ? "master" : "slave",
			    i/2);
		}
	}

	/* reselect device else we tend to get shadow register information */
	ddi_put8(ata_ctlp->ata_datap[chno], ata_ctlp->ac_drvhd[chno], tgtno);
	drv_usecwait(10000);

	sc = ddi_get8(ata_ctlp->ata_datap[chno], ata_ctlp->ioaddr1[chno] +
	    AT_COUNT);
	sn = ddi_get8(ata_ctlp->ata_datap[chno], ata_ctlp->ioaddr1[chno] +
	    AT_SECT);
	ch = ddi_get8(ata_ctlp->ata_datap[chno], ata_ctlp->ioaddr1[chno] +
	    AT_HCYL);
	cl = ddi_get8(ata_ctlp->ata_datap[chno], ata_ctlp->ioaddr1[chno] +
	    AT_LCYL);
	status = ddi_get8(ata_ctlp->ata_datap[chno], ata_ctlp->ioaddr1[chno] +
	    AT_STATUS);

	/*
	 * Check to see if there is no drive attached.As per ATA/ATAPI spec.
	 * sn and sc should both be 1 if a device is attached, the other
	 * values should determine if it's an ATA or ATAPI device. But we
	 * have seen with some DVD+RW drives that reports sc=3 and sn=1.
	 * In order to detect these kind of devices we are being more
	 * flexible by only looking for either sc=1 or sn=1.Contents of ch
	 * and cl will determine whether the drive is ATA disk or ATAPI drive.
	 */

	if ((sc != 1) && (sn != 1)) {
		if (ata_debug_attach) {
			cmn_err(CE_NOTE,
			    "chno %d, %s no devices detected sc=%d, sn=%d",
			    chno, (tgtno == ATDH_DRIVE0) ? "master" : "slave",
			    sc, sn);
		}
		drive_type = ATA_DEV_NONE;

	/* check for atapi signature, atapi_id uses ATA packet identify */
	} else if ((ch == ATAPI_SIG_HI) && (cl == ATAPI_SIG_LO)) {

		if (atapi_id(ata_ctlp->ata_datap[chno], ata_ctlp->ioaddr1[chno],
		    secbuf) == SUCCESS) {
			drive_type = ATA_DEV_ATAPI;
		} else {
			if (ata_debug_attach) {
				cmn_err(CE_NOTE, "chno %d, %s atapi_id failed",
				    chno, (tgtno == ATDH_DRIVE0) ?
				    "master" : "slave");
			}

			drive_type = ATA_DEV_NONE;
		}

	/* check for disk, ata_disk_id implements ATA identify */
	} else if ((cl == 0x00) && (ch == 0x00) && (status != 0x00)) {

		if (ata_disk_id(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ioaddr1[chno], secbuf) == SUCCESS) {

			drive_type = ATA_DEV_DISK;

		} else {
			if (ata_debug_attach)
				cmn_err(CE_NOTE, "chno %d, %s: disk id failed",
				    chno, (tgtno == ATDH_DRIVE0) ?
				    "master" : "slave");

			drive_type = ATA_DEV_NONE;
		}

	} else {

		drive_type = ATA_DEV_NONE;
	}

	if (ata_debug_attach) {
		switch (drive_type) {
			case ATA_DEV_NONE:
				cmn_err(CE_NOTE, "chno %d, %s:\
				    No device detected", chno, (tgtno ==
				    ATDH_DRIVE0) ? "master" : "slave");

				break;
			case ATA_DEV_DISK:
				cmn_err(CE_NOTE, "chno %d, %s: disk detected",
				    chno, (tgtno == ATDH_DRIVE0) ?
				    "master" : "slave");
				break;
			case ATA_DEV_ATAPI:
				cmn_err(CE_NOTE,
				    "chno %d, %s: atapi device detected", chno,
				    (tgtno == ATDH_DRIVE0) ?
				    "master" : "slave");
				break;
			default:
				cmn_err(CE_WARN,
				    "chno %d %s: Invalid device type!", chno,
				    (tgtno == ATDH_DRIVE0) ?
				    "master" : "slave");
		}
	}
	return (drive_type);

}

/*
 * Wait for a register of a controller to achieve a specific state.
 * To return normally, all the bits in the first sub-mask must be ON,
 * all the bits in the second sub-mask must be OFF.
 * If (usec_delay * iterations) passes without the controller achieving
 * the desired bit configuration, we return a positive iteration count
 * otherwise we return FAILURE.
 */
int
ata_wait(ddi_acc_handle_t handle, uint8_t  *port, ushort_t onbits,
	ushort_t offbits, ushort_t errbits, int usec_delay, int iterations)
{
	uint_t i;
	ushort_t val;

	for (i = 1; i <= iterations; i++) {
		val = ddi_get8(handle, port);
		if (((val & onbits) == onbits) &&
		    ((val & offbits) == 0)) {
			return (i);
		}

		/* if checking for error bail out if an error is found */
		if (errbits && ((val & errbits) == errbits))
			return (FAILURE);

		drv_usecwait(usec_delay);
	}

	return (FAILURE);
}

/*
 * Issue SLEEP or STANDBY command, the command is passed as a parameter
 * to the function. This function should not be called for an ATAPI device
 * Since the standby/sleep commands are not applicable for them.
 */

static int
ata_sleep_standby(struct ata_drive *ata_drvp, uchar_t cmd)
{
	struct ata_controller *ata_ctlp = ata_drvp->ad_ctlp;
	int chno = ata_drvp->ad_channel, retval = SUCCESS;


	if (ata_drvp->ad_flags & AD_ATAPI) {
		return (FAILURE);
	}

	/*
	 * Block interrupts from the channel
	 */
	ata_ctlp->disable_intr(ata_ctlp, chno);
	ata_ctlp->nien_toggle(ata_ctlp, chno, ATDC_NIEN | ATDC_D3);

	/*
	 * set up drive/head register
	 */
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_drvhd[chno], ata_drvp->ad_drive_bits);

	/*
	 * Confirm that the DRDY is set and busy is low.
	 */

	if (ata_wait(ata_ctlp->ata_datap1[chno], ata_ctlp->ac_altstatus[chno],
	    ATS_DRDY, ATS_BSY, IGN_ERR, 3000, 10000) == FAILURE) {
		retval = FAILURE;
		cmn_err(CE_WARN,
		"ata_controller - Drive not ready before command %x", cmd);
		goto end_sleep_standby;
	}

	/*
	 * issue command
	 */
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_cmd[chno], cmd);

	retval = ata_ctlp->get_intr_status(ata_ctlp, chno);
	if (retval == FAILURE) {
		cmn_err(CE_WARN,
		"ata_controller - Command %x failed", cmd);
		goto end_sleep_standby;
	}

	mutex_enter(&ata_ctlp->ac_hba_mutex);
	ata_ctlp->ac_polled_count++;
	mutex_exit(&ata_ctlp->ac_hba_mutex);

	/*
	 * check for error
	 */
	if (ddi_get8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_status[chno]) & ATS_ERR) {
		retval = FAILURE;
		cmn_err(CE_WARN,
		"ata_controller - Command %x returned error", cmd);
		goto end_sleep_standby;
	}

end_sleep_standby:
	ata_ctlp->enable_intr(ata_ctlp, chno);
	ata_ctlp->nien_toggle(ata_ctlp, chno, ATDC_D3);
	return (retval);
}


/*
 * Use SET FEATURES command
 */
int
ata_set_feature(struct ata_drive *ata_drvp, uchar_t feature, uchar_t value)
{
	struct ata_controller *ata_ctlp = ata_drvp->ad_ctlp;
	int chno = ata_drvp->ad_channel, retval = SUCCESS, speed = 0;
	unsigned char secbuf[512];
	struct dcd_identify *ad_id = NULL;


	ADBG_TRACE(("ata_set_feature entered\n"));

	/*
	 * clear the status bit so that we can clearly identify
	 * the interrupt happening
	 */
	ata_ctlp->clear_interrupt(ata_ctlp, chno);

	/*
	 * Block interrupts from the channel
	 */
	ata_ctlp->disable_intr(ata_ctlp, chno);
	ata_ctlp->nien_toggle(ata_ctlp, chno, ATDC_NIEN | ATDC_D3);



	/*
	 * Wait for the BSY to go low.
	 * The other pre-requisite for the command is DRDY should be
	 * set. We do not currently check for that since have seen in
	 * past that couple of models of cdrom's do not have this set.
	 * Not seen a case where set feature failed b'cause DRDY not set.
	 */
	if (ata_wait(ata_ctlp->ata_datap[chno], ata_ctlp->ac_status[chno], 0,
	    ATS_BSY, IGN_ERR, 3000, 10000) == FAILURE) {
		cmn_err(CE_WARN, "Drive not ready before set_features");
		retval = FAILURE;
		goto end_set_feature;
	}


	/*
	 * set up drive/head register.
	 * NOTE: This should not be moved up in the function before BSY since
	 * drive head register should not be touched in case BSY is set.
	 */
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_drvhd[chno], ata_drvp->ad_drive_bits);

	if (ata_drvp->ad_flags & AD_ATAPI) {
		/*
		 * Use the recently read IDENTIFY info. to know the selected
		 * mode else use the IDENTIFY data read during boot.During boot
		 * both these values are same.
		 * This info. is only used if set feature fails.
		 */
		if (atapi_id(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ioaddr1[chno], (ushort_t *)secbuf)
		    == SUCCESS) {
			ad_id = (struct dcd_identify *)secbuf;
		} else {
			ad_id = &(ata_drvp->ad_id);
		}
	}

	/*
	 * set up feature and value
	 */
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_feature[chno], feature);
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_count[chno], value);

	/*
	 * issue SET FEATURE command
	 */
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_cmd[chno], ATC_SET_FEAT);

	/*
	 * Wait for the interrupt pending bit to be set
	 * and then clear the status and then the interrupt.
	 */

	retval = ata_ctlp->get_intr_status(ata_ctlp, chno);
	if (retval == FAILURE) {
		cmn_err(CE_WARN,
		    "ata_controller - Set features failed");
		goto end_set_feature;
	}

	/*
	 * wait for not-busy status
	 */
	if (ata_wait(ata_ctlp->ata_datap[chno], ata_ctlp->ac_status[chno], 0,
	    ATS_BSY, IGN_ERR, 30, 400000) == FAILURE) {
		cmn_err(CE_WARN, "Drive not ready after set_features");
		retval = FAILURE;
		goto end_set_feature;
	}

	mutex_enter(&ata_ctlp->ac_hba_mutex);
	ata_ctlp->ac_polled_count++;
	mutex_exit(&ata_ctlp->ac_hba_mutex);


	/*
	 * check for error
	 */
	if (ddi_get8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_status[chno]) & ATS_ERR) {

		ddi_put8(ata_ctlp->ata_datap[chno],
		    ata_ctlp->ac_drvhd[chno], ata_drvp->ad_drive_bits);
		cmn_err(CE_WARN, "Error set after issuing Set Feature command");
		retval = FAILURE;
		goto end_set_feature;
	}
	ddi_put8(ata_ctlp->ata_datap[chno],
	    ata_ctlp->ac_drvhd[chno], ata_drvp->ad_drive_bits);

	/*
	 * Clear any pending intterupt
	 */
	retval = ata_ctlp->get_intr_status(ata_ctlp, chno);
	if (retval == FAILURE) {
		cmn_err(CE_WARN,
		    "Interrupt not seen after set_features");
		goto end_set_feature;
	}
	ata_ctlp->clear_interrupt(ata_ctlp, chno);
	ddi_put8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8 * chno + 2),
	    (ddi_get8(ata_ctlp->ata_cs_handle,
	    (uchar_t *)(ata_ctlp->ata_cs_addr + 8 * chno + 2))  | 6));

end_set_feature:
	/*
	 * For atapi devices: if setting of features fails because of
	 * any reason, pick the currently set speed from the identify
	 * information and use that as the current speed.
	 * This change is primarily being done - because certain model
	 * of atapi zip drives fail the set feature command (even though
	 * it is a mandatory command).
	 */
	if ((retval == FAILURE) && (ata_drvp->ad_flags & AD_ATAPI)) {
		value = 0xFF;
		if (ad_id != NULL && ad_id->dcd_validinfo & 0x04) {
			/*
			 * Contents of 0x88 are valid, Check if any of the
			 * ultra speeds have been selected.
			 * Check the cable controller capability also.
			 * A value of 3 indicates that ATA100 is possible.
			 * A value 2 for ac_speed indicate ATA66 possible.
			 * A value of 1 indicates ATA33 possible
			 */

			/*
			 * Some ATAPI devices had problem running
			 * in UDMA mode when connected to CMD646
			 * Rev > 5. So disabling UDMA mode for these
			 * devices.
			 */
			speed = ata_ctlp->ac_speed[chno];
			if ((ata_ctlp->ac_device_id == CMDDID) &&
			    (ata_ctlp->ac_revision > 0x5)) {
				speed = 0;
			}
			switch (speed) {
				case 3:
					if (ad_id->dcd_ultra_dma &
					    UDMA_MODE5_SELECTED)
						value = DCD_MULT_DMA_MODE5;
					/* FALLTHROUGH */
				case 2:
					if (ad_id->dcd_ultra_dma &
					    UDMA_MODE4_SELECTED)
						value = DCD_MULT_DMA_MODE4;
					else if (ad_id->dcd_ultra_dma &
					    UDMA_MODE3_SELECTED)
						value = DCD_MULT_DMA_MODE3;
					break;
				case 1:
					if (ad_id->dcd_ultra_dma &
					    UDMA_MODE2_SELECTED)
						value = DCD_MULT_DMA_MODE2;
					else if (ad_id->dcd_ultra_dma &
					    UDMA_MODE1_SELECTED)
						value = DCD_MULT_DMA_MODE1;
					else if (ad_id->dcd_ultra_dma &
					    UDMA_MODE0_SELECTED)
						value = DCD_MULT_DMA_MODE0;
					break;
				default:
					break;
			}
			if (value != 0xFF) {
				/*
				 * Device is currently in Ultra mode.
				 */
				ata_drvp->ad_dcd_options =
				    DCD_DMA_MODE | DCD_ULTRA_ATA | value;
				ata_drvp->ad_run_ultra = 1;
				value |= ENABLE_ULTRA_FEATURE;
				ata_drvp->ad_dmamode = value;
				ata_drvp->ad_cur_disk_mode = DMA_MODE;
				ata_drvp->ad_piomode = 0x7f;
			}
		}
		if (ad_id != NULL && (value == 0xFF) &&
		    (ad_id->dcd_validinfo & 0x02)) {
			/*
			 * Check for DMA mode.
			 */
			if (ad_id->dcd_dworddma &  DMA_MODE2_SELECTED)
				value = DCD_MULT_DMA_MODE2;
			else if (ad_id->dcd_dworddma & DMA_MODE1_SELECTED)
				value = DCD_MULT_DMA_MODE1;
			else if (ad_id->dcd_dworddma & DMA_MODE0_SELECTED)
				value = DCD_MULT_DMA_MODE0;
			if (value != 0xFF) {
				/*
				 * DMA mode has been selected for the device.
				 */
				ata_drvp->ad_dcd_options = DCD_DMA_MODE;
				ata_drvp->ad_dcd_options |= value;
				value |= ENABLE_DMA_FEATURE;
				ata_drvp->ad_dmamode = value;
				ata_drvp->ad_cur_disk_mode = DMA_MODE;
				ata_drvp->ad_run_ultra = 0;
				ata_drvp->ad_piomode = 0x7f;
			}
		}
		if (value == 0xFF) {
			/*
			 * Go with the PIO MODE 0
			 */
			ata_drvp->ad_dmamode = 0x7f;
			ata_drvp->ad_run_ultra = 0;
			ata_drvp->ad_piomode = 0;
			ata_drvp->ad_dcd_options = 0;
			ata_drvp->ad_cur_disk_mode = PIO_MODE;
			ata_drvp->ad_block_factor = 1;
			ata_drvp->ad_bytes_per_block =
			    ata_drvp->ad_block_factor << SCTRSHFT;
		}
		/*
		 * Need to program the controller accordingly.
		 */
		ata_ctlp->program_timing_reg(ata_drvp);
		retval = SUCCESS;
	}


	/*
	 * Do not enable the interrupts if attach in progress
	 * This could generate "level 4 Interrupts not serviced
	 * Message because of the anomalous behavior of NIEN bit.
	 * The enabling of the interrupt will happen after the
	 * attach is complete.
	 */
	if ((ata_ctlp->ac_flags & AC_ATTACH_IN_PROGRESS) == 0) {
		ata_ctlp->enable_intr(ata_ctlp, chno);
		ata_ctlp->nien_toggle(ata_ctlp, chno, ATDC_D3);
	}

	return (retval);
}

void
ata_ghd_complete_wraper(struct ata_controller *ata_ctlp,
			struct ata_pkt *ata_pktp, int chno)
{
	if (ata_ctlp->ac_simplex == 1) {
		int rc, nchno = (chno) ? 0 : 1;

		mutex_enter(&ata_ctlp->ac_hba_mutex);
		ata_ctlp->ac_pending[chno] = 0;
		if (ata_ctlp->ac_pending[nchno]) {
			ata_ctlp->ac_actv_chnl = nchno;
		} else {
			ata_ctlp->ac_actv_chnl = 2;
		}
		if (ata_ctlp->ac_actv_chnl != 2) {
			struct ata_pkt *lata_pktp;

			mutex_exit(&ata_ctlp->ac_hba_mutex);

			lata_pktp = ata_ctlp->ac_active[nchno];

			if (lata_pktp != NULL) {
				rc = lata_pktp->ap_start(ata_ctlp, lata_pktp);

				if (rc != TRAN_ACCEPT) {

					mutex_enter(&ata_ctlp->ac_hba_mutex);
					ata_ctlp->ac_actv_chnl = 2;
					ata_ctlp->ac_pending[nchno] = 0;
					mutex_exit(&ata_ctlp->ac_hba_mutex);

					ata_ctlp->ac_active[nchno] = NULL;

					ghd_async_complete(
					    &ata_ctlp->ac_ccc[nchno],
					    APKT2GCMD(lata_pktp));
				}
			}
		} else {
			mutex_exit(&ata_ctlp->ac_hba_mutex);
		}
	}
	ghd_complete(&ata_ctlp->ac_ccc[chno], APKT2GCMD(ata_pktp));
}

void
make_prd(gcmd_t *gcmdp, ddi_dma_cookie_t *cookie, int single_seg, int num_segs)
{
	struct ata_pkt *ata_pktp = gcmdp->cmd_private;
#ifdef __lint
	int seg = num_segs;

	single_seg = seg;
	seg = single_seg;
#endif
	ata_pktp->ap_addr = cookie->dmac_address;
	ata_pktp->ap_cnt = cookie->dmac_size;
}

void
write_prd(struct ata_controller *ata_ctlp, struct ata_pkt *ata_pktp)
{
	int chno = ata_pktp->ap_chno;
	ddi_acc_handle_t ata_prd_acc_handle;
	ddi_dma_handle_t ata_prd_dma_handle;
	caddr_t  memp;
	int i = 0, andfactor;
	uint32_t addr = ata_pktp->ap_addr;
	size_t  cnt = ata_pktp->ap_cnt;
	struct ata_drive *ata_drvp;

	ata_prd_acc_handle = (void *)ata_ctlp->ata_prd_acc_handle[chno];
	ata_prd_dma_handle = (void *)ata_ctlp->ata_prd_dma_handle[chno];
	memp = (void *)ata_ctlp->ac_memp[chno];
	ata_drvp = CTL2DRV(ata_ctlp, ata_pktp->ap_targ, 0);
	if (ata_drvp->ad_flags & AD_ATAPI) {
		andfactor = 0x00ffffff;
	} else {
		andfactor = 0x0003ffff;
	}

	while (cnt) {
		if (((addr & 0x0000ffff) + (cnt &
		    andfactor)) <= 0x00010000) {
			ddi_put32(ata_prd_acc_handle, (uint32_t *)memp + i++,
			    addr);
			ddi_put32(ata_prd_acc_handle, (uint32_t *)memp + i++,
			    (cnt & 0x0000ffff) | 0x80000000);
			break;
		} else {
			ddi_put32(ata_prd_acc_handle, (uint32_t *)memp +i++,
			    addr);
			ddi_put32(ata_prd_acc_handle, (uint32_t *)memp + i++,
			    (0x10000 - (addr & 0x0000ffff)));
			cnt -= (0x10000 - (addr & 0x0000ffff));
			addr += (0x10000 - (addr & 0x0000ffff));
		}
	}
	ddi_put32(ata_ctlp->ata_cs_handle,
	    (uint32_t *)(ata_ctlp->ata_cs_addr + 4 + 8*chno),
	    (uint32_t)ata_ctlp->ac_saved_dmac_address[chno]);
	(void) ddi_dma_sync(ata_prd_dma_handle, 0, 2048, DDI_DMA_SYNC_FORDEV);
}


/*
 * prd_init()
 */
int
prd_init(struct ata_controller *ata_ctlp, int chno)
{
	size_t	alloc_len;
	uint_t	ncookie;
	ddi_dma_cookie_t cookie;
	ddi_dma_attr_t  prd_dma_attrs;
	ddi_acc_handle_t ata_prd_acc_handle;
	ddi_dma_handle_t ata_prd_dma_handle;
	caddr_t  memp;

	prd_dma_attrs = ata_dma_attrs;
	prd_dma_attrs.dma_attr_sgllen = 1;
	prd_dma_attrs.dma_attr_granular = 32;
	prd_dma_attrs.dma_attr_addr_lo = 0x0ull;
	prd_dma_attrs.dma_attr_addr_hi = 0xffffffffull; /* 32-bit max range */

	if (ddi_dma_alloc_handle(ata_ctlp->ac_dip, &prd_dma_attrs,
	    DDI_DMA_DONTWAIT, NULL, &ata_prd_dma_handle) != DDI_SUCCESS) {
		return (FAILURE);
	}

	/*
	 * The area being allocated for the prd tables is 2048 bytes,
	 * Each entry takes 8 bytes, so the table would be able to hold
	 * 256 entries of 64K each. Thus allowing a DMA size upto 16MB
	 */
	if (ddi_dma_mem_alloc(ata_prd_dma_handle, 2048,
	    &dev_attr, DDI_DMA_STREAMING, DDI_DMA_DONTWAIT,
	    NULL, &memp, &alloc_len, &ata_prd_acc_handle) != DDI_SUCCESS) {
		return (FAILURE);
	}

	if (ddi_dma_addr_bind_handle(ata_prd_dma_handle, NULL, memp,
	    alloc_len, DDI_DMA_READ | DDI_DMA_STREAMING, DDI_DMA_DONTWAIT,
	    NULL, &cookie, &ncookie) != DDI_DMA_MAPPED) {
		return (FAILURE);
	}

	ata_ctlp->ata_prd_acc_handle[chno] = (void *)ata_prd_acc_handle;
	ata_ctlp->ata_prd_dma_handle[chno] = (void *)ata_prd_dma_handle;
	ata_ctlp->ac_memp[chno] = memp;
	ddi_put32(ata_ctlp->ata_cs_handle,
	    (uint32_t *)(ata_ctlp->ata_cs_addr + 4 + 8*chno),
	    cookie.dmac_address);
	/*
	 * This need to be saved as the address written to the prd pointer
	 * register will go away after a cpr cycle and power might have been
	 * removed and hencewe need to use the saved address to program
	 * that register
	 */
	ata_ctlp->ac_saved_dmac_address[chno] = cookie.dmac_address;
	return (SUCCESS);
}

void
change_endian(unsigned char *string, int length)
{
	unsigned char x;
	int i;
	for (i = 0; i < length; i++) {
		x = string[i];
		string[i] = string[i+1];
		string[i+1] = x;
		i++;
	}
}

/*
 * ata_drive_initialize() is the routine called from atapi.c and ata_disk.c
 * identifies the maximum speed for the drive and programs it accordingly.
 */
int
ata_drive_initialize(struct ata_controller *ata_ctlp)
{
	dev_info_t	*dip = ata_ctlp->ac_dip;
	char *prop_template = "target%x-dcd-options";
	char prop_str[32];
	uchar_t mode, mode1;
	int	targ, chno, speed;
	struct ata_drive *ata_drvp;
	struct dcd_identify *ad_id;
	int	value = 0;

	for (targ = 0; targ < ATA_MAXTARG; targ++) {
		if (targ < 2)
			chno = 0;
		else
			chno = 1;

		ata_drvp = CTL2DRV(ata_ctlp, targ, 0);
		if (ata_drvp == NULL) {
			continue;
		}
		ad_id = &(ata_drvp->ad_id);
		(void) sprintf(prop_str, prop_template, targ);
		ata_drvp->ad_dcd_options =
		    ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0, prop_str, -1);
		if (ata_drvp->ad_dcd_options == -1) {
			/* else create it */
			/*  dcd-option if defined will override this. */
			value = (ata_ctlp->ac_dcd_options == -1) ?
			    (int)DEFAULT_DCD_OPTIONS:
			    (int)ata_ctlp->ac_dcd_options;
			(void) ddi_prop_create(DDI_DEV_T_NONE, dip, 0, prop_str,
			    (caddr_t)&value, sizeof (int));
			ata_drvp->ad_dcd_options =
			    (ata_ctlp->ac_dcd_options == -1) ?
			    DEFAULT_DCD_OPTIONS:
			    ata_ctlp->ac_dcd_options;
			} else if (ata_ctlp->ac_dcd_options != -1) {

			/*
			 * dcd-option if defined will override this.
			 */
			ata_drvp->ad_dcd_options = ata_ctlp->ac_dcd_options;
		}

		mode = ata_drvp->ad_dcd_options & 0x07;

		switch (ata_drvp->ad_dcd_options & DCD_CHECK_ULTRA) {
		/*
		 * The higher bits of dcd_options indicate DMA and
		 * Ultra DMA option request.
		 * bit 7 if = 1, indicates DMA mode
		 * bit 5 if = 1, indicates UDMA mode
		 */
			case DCD_CHECK_ULTRA:
				/*
				 * Go for UDMA mode.
				 * Get the cable/controller capabilities
				 * A return of 3 indicates UDMA100 possible
				 * A return of 2 indicates UDM66 possible
				 * A value of 1 indicates UDMA33 possible
				 * A value of 0 indicates only DMA0-2 pos
				 */

				/*
				 * Some ATAPI devices had problem running
				 * in UDMA mode when connected to CMD646
				 * Rev > 5. So disabling UDMA mode for these
				 * devices.
				 */

				speed = ata_ctlp->ac_speed[chno];
				if ((ata_ctlp->ac_device_id == CMDDID) &&
				    (ata_ctlp->ac_revision > 0x5) &&
				    (ATAPIDRV(ata_drvp))) {
					speed = 0;
				}
				/* Ultra DMA mode set */
				if (speed) {
					/*
					 * Check disk capabilities
					 */
					if (ad_id->dcd_validinfo & 0x04) {
						if (ad_id->dcd_ultra_dma &
						    UDMA_MODE5)
							mode1 =
							    DCD_MULT_DMA_MODE5;
						else if (ad_id->dcd_ultra_dma &
						    UDMA_MODE4)
							mode1 =
							    DCD_MULT_DMA_MODE4;
						else if (ad_id->dcd_ultra_dma &
						    UDMA_MODE3)
							mode1 =
							    DCD_MULT_DMA_MODE3;
						else if (ad_id->dcd_ultra_dma &
						    UDMA_MODE2)
							mode1 =
							    DCD_MULT_DMA_MODE2;
						else if (ad_id->dcd_ultra_dma &
						    UDMA_MODE1)
							mode1 =
							    DCD_MULT_DMA_MODE1;
						else if (ad_id->dcd_ultra_dma &
						    UDMA_MODE0)
							mode1 =
							    DCD_MULT_DMA_MODE0;

						/*
						 * If the controller or cable
						 * cannot handle the higher
						 * speed reduce the DMA mode.
						 */

						if (mode1 > 2) {
							/* BEGIN CSTYLED */
							if (speed == 2)
								mode1 =
							DCD_MULT_DMA_MODE4;
							else if (speed == 1)
								mode1 =
							DCD_MULT_DMA_MODE2;
							/* END CSTYLED */
						}

						/*
						 * Use the highest speed
						 * reported from the target,
						 * controller and cable.
						 */

						if (mode > mode1)
							mode = mode1;

						ata_drvp->ad_run_ultra = 1;
						ata_drvp->ad_piomode = 0x7f;
						ata_drvp->ad_dcd_options =
						    DCD_DMA_MODE |
						    DCD_ULTRA_ATA;
						ata_drvp->ad_dcd_options
						    |= mode;
						mode |= ENABLE_ULTRA_FEATURE;
						ata_drvp->ad_dmamode = mode;
						ata_drvp->ad_cur_disk_mode =
						    DMA_MODE;
						break;
					}
				}
				/*
				 * If the disk/cable/controller not capable of
				 * Ultra operations, try DMA.
				 */
				/* FALLTHROUGH */
			case DCD_CHECK_DMA:
				/*
				 * DMA Mode 0-2
				 */
				if (ad_id->dcd_validinfo & 0x02) {
					ata_drvp->ad_piomode = 0x7f;
					ata_drvp->ad_run_ultra = 0;
					if (ad_id->dcd_dworddma &  DMA_MODE2)
						mode1 = DCD_MULT_DMA_MODE2;
					else if (ad_id->dcd_dworddma &
					    DMA_MODE1)
						mode1 = DCD_MULT_DMA_MODE1;
					else
						mode1 = DCD_MULT_DMA_MODE0;
					if (mode > mode1)
						mode = mode1;
					ata_drvp->ad_dcd_options = DCD_DMA_MODE;
					ata_drvp->ad_dcd_options |= mode;
					mode |= ENABLE_DMA_FEATURE;
					ata_drvp->ad_dmamode = mode;
					ata_drvp->ad_cur_disk_mode = DMA_MODE;
					break;
				}
				/*
				 * Fall through in case DMA is not supported
				 */
				/* FALLTHROUGH */
			case DCD_CHECK_PIO:

				ata_drvp->ad_dmamode = 0x7f;
				ata_drvp->ad_run_ultra = 0;
				if (ad_id->dcd_validinfo & 0x02) {
					/*
					 * Check advance pio mode
					 */
					if ((ad_id->dcd_advpiomode
					    & PIO_MODE4_MASK) == 0x2)
						mode1 = DCD_PIO_MODE4;
					else if ((ad_id->dcd_advpiomode &
					    PIO_MODE3_MASK) == 0x1)
						mode1 = DCD_PIO_MODE3;
				} else {
					mode1 = DCD_PIO_MODE2;
				}
				if (mode > mode1)
					mode = mode1;
				ata_drvp->ad_piomode = mode;
				ata_drvp->ad_dcd_options = 0;
				ata_drvp->ad_dcd_options |= mode;
				if (mode > DCD_PIO_MODE2) {
					mode |= ENABLE_PIO_FEATURE;
				}
				ata_drvp->ad_cur_disk_mode = PIO_MODE;
				if (!(ata_drvp->ad_dcd_options & 0x40)) {
					ata_drvp->ad_block_factor = 1;
					ata_drvp->ad_bytes_per_block =
					    ata_drvp->ad_block_factor <<
					    SCTRSHFT;
				}
				break;
			default:
				/* BEGIN CSTYLED */
				cmn_err(CE_WARN,
				"\n Invalid settings for the mode property\n");
				/* END CSTYLED */
		}

		ata_ctlp->program_timing_reg(ata_drvp);
		if (ata_set_feature(ata_drvp, ATA_FEATURE_SET_MODE, mode) !=
		    SUCCESS) {
			return (FAILURE);
		}
		/*
		 * now modify the property with current value
		 */
		(void) ddi_prop_modify(DDI_DEV_T_NONE, dip, 0, prop_str,
		    (caddr_t)&(ata_drvp->ad_dcd_options), sizeof (int));
	}

	return (SUCCESS);
}

/*
 * ata_dmaget routine will perform the DMA resources allocation and
 * mapping tasks for the data buffers.
 * This routine will take care of the misaligned address coming from
 * user land.
 */

int
ata_dmaget(dev_info_t   *dip,
	struct ata_pkt	*ata_pktp,
	struct buf	*bp,
	int		dma_flags,
	int		(*callback)(),
	caddr_t		arg)
{
	gcmd_t *gcmdp = APKT2GCMD(ata_pktp);
	int	rval;
	ddi_dma_cookie_t cmd_cookie;
	uint_t cmd_cookiec;


	/*
	 * Allocate a DMA handle.
	 */
	if (ddi_dma_alloc_handle(dip, &ata_dma_attrs, callback, arg,
	    &gcmdp->cmd_dma_handle) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	/*
	 * Bind handle with the buffer.
	 */

	ata_pktp->ap_count_bytes = gcmdp->cmd_totxfer = bp->b_bcount;
	ata_pktp->ap_buf_addr = NULL;

	if (!bioaligned(bp, 2, BIOALIGNED_BEGIN)) {
		/*
		 * Non word aligned address. Allocate an aligned one.
		 */
		ata_pktp->ap_buf_addr = kmem_zalloc(ata_pktp->ap_count_bytes,
		    KM_NOSLEEP);
		if (ata_pktp->ap_buf_addr == NULL) {
			ddi_dma_free_handle(&gcmdp->cmd_dma_handle);
			return (DDI_FAILURE);
		}

		/*
		 * If this is a write operation, then copy the user buffer
		 * into shadow buffer.
		 */
		if (!(bp->b_flags & B_READ))
			bcopy(bp->b_un.b_addr, ata_pktp->ap_buf_addr,
			    ata_pktp->ap_count_bytes);

		rval = ddi_dma_addr_bind_handle(gcmdp->cmd_dma_handle, 0,
		    ata_pktp->ap_buf_addr, ata_pktp->ap_count_bytes,
		    dma_flags, callback, arg, &cmd_cookie, &cmd_cookiec);
	} else {
		rval = ddi_dma_buf_bind_handle(gcmdp->cmd_dma_handle, bp,
		    dma_flags, callback, arg, &cmd_cookie, &cmd_cookiec);
	}

	if (rval && (rval != DDI_DMA_MAPPED)) {
		switch (rval) {
			case DDI_DMA_NORESOURCES:
				bioerror(bp, 0);
				break;
			case DDI_DMA_BADATTR:
			case DDI_DMA_NOMAPPING:
				bioerror(bp, EFAULT);
				break;
			case DDI_DMA_TOOBIG:
			default:
				bioerror(bp, EINVAL);
				break;
		}
		ddi_dma_free_handle(&gcmdp->cmd_dma_handle);
		if (ata_pktp->ap_buf_addr != NULL) {
			kmem_free(ata_pktp->ap_buf_addr,
			    ata_pktp->ap_count_bytes);
		}
		return (DDI_FAILURE);
	}

	make_prd(gcmdp, &cmd_cookie, 0, 0);

	return (DDI_SUCCESS);
}

void
ata_dmafree(struct ata_pkt  *ata_pktp)
{
	gcmd_t *gcmdp = APKT2GCMD(ata_pktp);

	if (gcmdp->cmd_dma_handle != NULL) {
		(void) ddi_dma_unbind_handle(gcmdp->cmd_dma_handle);
		ddi_dma_free_handle(&gcmdp->cmd_dma_handle);
		gcmdp->cmd_dma_handle = NULL;
		gcmdp->cmd_dmawin = NULL;
		gcmdp->cmd_totxfer = 0;
		if (ata_pktp->ap_buf_addr != NULL) {
			kmem_free(ata_pktp->ap_buf_addr,
			    ata_pktp->ap_count_bytes);
		}
	}
}

/*
 * This routine will issue reset on both the channel and try to take
 * the drives in standby mode. This would be invoked from the chip
 * specific part to do general stuff.
 */

void
ata_raise_power(struct ata_controller *ata_ctlp)
{
	struct	ata_drive *ata_drvp;
	int	targ, retry;

	/*
	 * Do not attempt to issue a reset if there is no device 0 on channel 0
	 */
	ata_drvp = CTL2DRV(ata_ctlp, 0, 0);
	if (ata_drvp != NULL) {
		for (retry = 0; retry < 3; retry++) {
			if (ata_reset_bus(ata_ctlp, 0) != FALSE) break;
		}
		if (retry == 3)
			cmn_err(CE_WARN,
			"ata_controller - Can not reset Primary channel");
	}

	/*
	 * Do not attempt to issue a reset if there is no device 0 on channel 1
	 */
	ata_drvp = CTL2DRV(ata_ctlp, 2, 0);
	if (ata_drvp != NULL) {
		for (retry = 0; retry < 3; retry++) {
			if (ata_reset_bus(ata_ctlp, 1) != FALSE) break;
		}
		if (retry == 3)
			cmn_err(CE_WARN,
			"ata_controller - Can not reset Secondary channel");
	}
	/*
	 * Issue standby command on each disk device on the bus.
	 * This is being done as it is not guaranteed that the
	 * device will come back to standby after soft reset.
	 */
	for (targ = 0; targ < ATA_MAXTARG; targ++) {
		ata_drvp = CTL2DRV(ata_ctlp, targ, 0);
		if (ata_drvp == NULL)
			continue;
		if (ata_drvp->ad_flags & AD_ATAPI)
			continue;
		(void) ata_sleep_standby(ata_drvp, ATC_STANDBY);
		/*
		 * Note that we are not checking the return value
		 * value of standby command - This will result in
		 * a possibility that the device state at target level
		 * is different from the actual state. But this should
		 * be OK since coming back to active is not dependent
		 * on prior state. It will be too harsh to fail device
		 * recovery if standby fails at this stage.
		 */
	}
	ata_ctlp->ac_reset_done = 1;
}

/*
 * This would be invoked from the chip specific part to do general stuff
 * While lowering the power.
 */
int
ata_lower_power(struct ata_controller *ata_ctlp)
{
	struct	ata_drive *ata_drvp;
	int	rval, targ;

	/*
	 * While the HBA is attaching there is a possibiity that pm scan
	 * can send a request to lower the power as soon as this HBA
	 * calls pm_raise_power() in ata_attach() routine.
	 * If we accept the request then the power to the connected
	 * devices will be turned OFF as soon as the HBA goes into
	 * low power. Because of this power off at later statge in
	 * the attach path we fail to detect the connected drives.
	 *
	 * To fix this fail the request to lower HBA power while
	 * ATTACH is still in progress
	 */
	if (ata_ctlp->ac_flags & AC_ATTACH_IN_PROGRESS) {
		return (FAILURE);
	}
	/*
	 * Request is for lowering the power level of the controller.
	 * Issue sleep command on each disk device on the bus.
	 */
	for (targ = 0; targ < ATA_MAXTARG; targ++) {
		ata_drvp = CTL2DRV(ata_ctlp, targ, 0);
		if (ata_drvp == NULL)
			continue;
		if (ata_drvp->ad_flags & AD_ATAPI)
			continue;
		rval = ata_sleep_standby(ata_drvp, ATC_SLEEP);
		if (rval == FAILURE) {
			cmn_err(CE_WARN,
			"ata_controller- Can not take drive %d to sleep", targ);
			(void) ata_reset_bus(ata_ctlp, 0);
			if (targ > 1)
				(void) ata_reset_bus(ata_ctlp, 1);
				return (FAILURE);
			}
	}

	return (SUCCESS);
}

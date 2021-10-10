/*
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* #define ADPU320_TEST_64 */
/* #define ADPU320_DEBUG */
/* #define ADPU320_MAPCHECK */
/* #define ADPU320_DEBUG2 */
/* #define ADPU320_CHIM_UNDERRUN */
/*
 * PH3.0 RC1 09/04/03
 * TODO:
 *
 * - fix the RST's in the code
 * - if attach fails, it should disable IRQs
 * - should fill in targetCommand for PAC (not used currently).
 * - add reason and pEventContext to PROTOCOL_AUTO_CONFIG
 *
 */

/*
 *
 * Copyright (c) 1995-2003 ADAPTEC, Inc.
 * All Rights Reserved.
 *
 *
 */

/*
 * November 97 Modify to support multiple LUN, each LUN must have
 *	     a unique Task Set Handle. (2.1 alpha 1)
 * January  98 Fixed problems for Adac : devices is not recognize
 *	     if it is not ready during initialization.  Fake that
 *	     all target is present
 *	     by returning DDI_SUCCESS for any target ID from 0 to 15
 *	     in adpu320_tran_tgt_init. So, we must check if the Task set
 *	     handle is NULL in adpu320_pktalloc.(2.1 alpha 2)
 * February 98 Fixed the problem of check scanner for Italian banks. Send
 *	     HIM_IOB_UNFREEZE to CHIM if target return status HIM_IOB_BUSY
 *	     (2.1 alpha 3)
 * March 98    Fixed the problem of UltraSparc 10 panic when the volume
 *	     manager start.  Panic happenned when the target driver try a
 *	     SCSI bus reset.  In adpu320_IobSpecial we have to Disable
 *	     Interrupt
 *	     first then call HIMQueueIOB and then poll the status.(2.1 alpha4)
 *
 */

#include <sys/int_types.h>
#include <sys/scsi/scsi.h>
#include <sys/debug.h>
#include <sys/pci.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#ifdef ADPU320_DEBUG
#include <sys/promif.h>
#endif
#include <sys/modctl.h>

#include "chim.h"
#include "adpu320.h"
#include "release.h"

/*
 * Local data
 */
/* CSTYLED */
STATIC kmutex_t			adpu320_global_mutex;
/* CSTYLED */
STATIC unsigned char		adpu320_mem_map = HIM_IOSPACE;
/* CSTYLED */
STATIC void			*adpu320_state = NULL;
/* CSTYLED */
STATIC int			adpu320_watchdog_tick = 5;
/* CSTYLED */
STATIC int			adpu320_tick;
int				adpu320_max_active_cmds = 16;

#if defined(ADPU320_DEBUG) || defined(ADPU320_DEBUG2)
int				adpu320_debug = DENT | DPKT | DDATA | DTEMP |
					DCHN | DSTUS | DINIT | DTEST | DPROBE |
					DVERBOS | DTUR | DLIST | DINTR |
					DIOERR | DIOSTS | DPKTDMP;

kmutex_t			adpu320_print_mutex;
unsigned			adpu320_buffer_output = 1;
unsigned			adpu320_debug_enable = 1;
char				adpu320_print_line[LINESIZE];
char				adpu320_print_buffer[PRINT_BUF_SIZE];
char				*adpu320_print_buf_ptr;
unsigned			adpu320_print_buf_wrap;
adpu320_queue_t			adpu320_cfp_list;
#endif

#ifdef ADPU320_TEST_64
unsigned	adpu320_check_64 = 0;
long long	adpu320_phys_addr;
#endif

/*
 * Forward declarations
 */
static int adpu320_tran_tgt_init(dev_info_t *, dev_info_t *, scsi_hba_tran_t *,
	struct scsi_device *);
static int adpu320_tran_tgt_probe(struct scsi_device *, int (*)());
static void adpu320_tran_tgt_free(dev_info_t *, dev_info_t *, scsi_hba_tran_t *,
	struct scsi_device *);
static int adpu320_tran_setup_pkt(struct scsi_pkt *,
    int (*)(), caddr_t);
static void adpu320_tran_teardown_pkt(struct scsi_pkt *);
static int adpu320_constructor(struct scsi_pkt *, scsi_hba_tran_t *, int);
static void adpu320_destructor(struct scsi_pkt *, scsi_hba_tran_t *);
static int adpu320_tran_abort(struct scsi_address *, struct scsi_pkt *);
static int adpu320_tran_reset(struct scsi_address *, int);
static int adpu320_tran_reset_notify(struct scsi_address *, int,
	void (*callback)(caddr_t), caddr_t);
static void adpu320_do_reset_notify_callbacks(adpu320_config_t *);
static int adpu320_capchk(char *, int, int *);
static int adpu320_tran_getcap(struct scsi_address *, char *, int);
static int adpu320_tran_setcap(struct scsi_address *, char *, int, int);
static int adpu320_pktalloc(struct scsi_pkt *, int (*)(), caddr_t);
static void adpu320_pktfree(struct scsi_address *, struct scsi_pkt *);
static void adpu320_dmaget(struct scsi_pkt *);
static void adpu320_i_watch(caddr_t);
static int adpu320_tran_start(struct scsi_address *, struct scsi_pkt *);
static struct adpu320_scsi_cmd	*adpu320_IobSpecial(adpu320_config_t *,
	HIM_IOB *, int, int, uint_t);
static int adpu320_cfginit(adpu320_soft_state_t *);
static int adpu320_validate(adpu320_config_t *);

static uint_t adpu320_intr(caddr_t);
static void adpu320_pollret(adpu320_config_t *, struct scsi_pkt *);
static int adpu320_chkstatus(struct scsi_pkt *, HIM_IOB *);
static int adpu320_setup_tran(dev_info_t *, adpu320_soft_state_t *);
static adpu320_soft_state_t *adpu320_alloc_adpu320(dev_info_t *);
static int adpu320_cfpinit(dev_info_t *, adpu320_soft_state_t *);
static uint_t adpu320_run_callbacks(caddr_t);
static HIM_IOB * adpu320_allocIOB(adpu320_config_t *,
	struct adpu320_scsi_cmd *);
void adpu320_destroy_him_memory(adpu320_config_t *);
static int adpu320_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int adpu320_attach(dev_info_t *, ddi_attach_cmd_t);
static int adpu320_detach(dev_info_t *, ddi_detach_cmd_t);
static int adpu320_add_intrs(adpu320_soft_state_t *, int);
static void adpu320_rem_intrs(adpu320_soft_state_t *);

/*
 * DMA attribute for data transfer
 */
/* CSTYLED */
STATIC	ddi_dma_attr_t	adpu320_dmalim = {
	DMA_ATTR_V0,					/* Version number */
	(unsigned long long) 0,				/* address low */
#if OSM_BUS_ADDRESS_SIZE == 64
	(unsigned long long) 0xffffffffffffffff,	/* address high */
#else
	(unsigned long long) 0xffffffff,		/* address high */
#endif
	(unsigned long long) 0x00ffffff,		/* counter max */
	(unsigned long long) 4,				/* Byte alignment */
	DMA_DEFAULT_BURSTSIZE,				/* burstsize */
	1,						/* minimum xfer */
	(unsigned long long) 0x00FFFFFE,		/* max xfer size */
	(unsigned long long) 0xFFFFFFFF,		/* max segment size */
	ADPU320_MAX_DMA_SEGS,				/* SG list length */
	512,						/* sector size */
	0						/* flag */
};

/* CSTYLED */
STATIC ddi_dma_attr_t	adpu320_contig_dmalim = {
	DMA_ATTR_V0,					/* Version number */
	(unsigned long long) 0,				/* address low */
#if	OSM_BUS_ADDRESS_SIZE == 64
	(unsigned long long) 0xffffffffffffffff,	/* address high */
#else
	(unsigned long long) 0xffffffff,		/* address high */
#endif
	(unsigned long long) 0x00ffffff,		/* counter max */
	(unsigned long long) 0x4,			/* Byte alignment */
	DMA_DEFAULT_BURSTSIZE,				/* burstsize */
	1,						/* minimum xfer */
	(unsigned long long) 0x00FFFFFE,		/* max xfer size */
	(unsigned long long) 0xFFFFFFFF,		/* max segment size */
	1,						/* SG list length */
	1,						/* sector size */
	0						/* flag */
};

/*
 * Devive access attribute
 */
/* CSTYLED */
STATIC  ddi_device_acc_attr_t adpu320_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC,
};

static struct cb_ops adpu320_cb_ops = {
	scsi_hba_open,		/* open */
	scsi_hba_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	adpu320_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* chpoll */
	nodev,			/* cb_prop_op */
	NULL,			/* streamtab */
	D_MP | D_NEW,		/* cb_flag */
	CB_REV,			/* rev */
	nodev,			/* aread */
	nodev			/* awrite */
};

/* CSTYLED */
STATIC  struct dev_ops	adpu320_ops = {
	DEVO_REV,			/* devo_rev */
	0,				/* refcnt  */
	adpu320_getinfo,		/* info */
	nulldev,			/* identify */
	adpu320_probe,			/* probe */
	adpu320_attach,			/* attach */
	adpu320_detach,			/* detach */
	nodev,				/* reset */
	&adpu320_cb_ops,		/* driver operations */
	NULL,				/* bus operations */
	NULL,				/* power */
	ddi_quiesce_not_needed		/* quiesce */
};

char _depends_on[] = "misc/scsi";

extern struct mod_ops mod_driverops;

/* CSTYLED */
STATIC  struct modldrv modldrv = {
	&mod_driverops,	/* Type of module. This one is a driver */
	"Adaptec Ultra320 HBA " ADPU320_RELEASE,
	&adpu320_ops,	/* driver ops */
};

/* CSTYLED */
STATIC  struct modlinkage modlinkage = {
	MODREV_1, { (void *)&modldrv, NULL }
};


/*
 * Function : _init()
 *
 * OK
 */
int
_init(void)
{
	int	status;

#ifdef ADPU320_DEBUG
#if 0
	debug_enter("adpu320_init\n");
#endif
#endif

	/*
	 * DDI-2.5
	 * 5 means we preallocate 5 of "adpu320_soft_state_t" space.
	 * Each 39320 will require a "adpu320_soft_state_t" space.
	 */
	status = ddi_soft_state_init(&adpu320_state,
	    (sizeof (adpu320_soft_state_t) +
	    sizeof (adpu320_config_t)), 5);

	if (status != 0) {
		return (status);
	}

	/*
	 * Figure out how many ticks are in adpu320_watchdog_tick (which
	 * is in seconds ... 5 seconds at the time of this comment).
	 */
	adpu320_tick = drv_usectohz((clock_t)adpu320_watchdog_tick * 1000000);

	if ((status = scsi_hba_init(&modlinkage)) != 0) {
		/*
		 * DDI-2.5
		 */
		ddi_soft_state_fini(&adpu320_state);

		return (status);
	}

	mutex_init(&adpu320_global_mutex, NULL, MUTEX_DRIVER, (void *)NULL);

#ifdef ADPU320_DEBUG
	mutex_init(&adpu320_print_mutex, NULL, MUTEX_DRIVER, (void *)NULL);
#endif
	if ((status = mod_install(&modlinkage)) != 0) {
		/*
		 * DDI-2.5
		 */
		ddi_soft_state_fini(&adpu320_state);

		scsi_hba_fini(&modlinkage);

		mutex_destroy(&adpu320_global_mutex);

#ifdef ADPU320_DEBUG
		mutex_destroy(&adpu320_print_mutex);
#endif
	}

	return (status);
}

/*
 * Function : _fini()
 *
 * OK
 */
int
_fini(void)
{
	int	status;

	status = mod_remove(&modlinkage);

	if (status != 0) {
		/*
		 * The mod_remove failed.
		 */
		return (status);
	}

	/*
	 * DDI-2.5 - the mod_removed succeeded.
	 */
	ddi_soft_state_fini(&adpu320_state);

	scsi_hba_fini(&modlinkage);

	mutex_destroy(&adpu320_global_mutex);

#ifdef ADPU320_DEBUG
	mutex_destroy(&adpu320_print_mutex);
#endif

	return (status);
}

/*
 * Function : _info()
 *
 * OK
 */
int
_info(
struct modinfo *modinfop
)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Function : adpu320_getinfo()
 *
 *	Autoconfiguration routines
 * OK
 */
/*ARGSUSED*/
/* CSTYLED */
STATIC int
adpu320_getinfo(
dev_info_t	*dip,
ddi_info_cmd_t	infocmd,
void		*arg,
void		**result
)
{
	return (DDI_FAILURE);
}

/*
 * Function : adpu320_probe()
 *
 * PCI HBAs are suppose to be self-identifying, but we cannot
 * tell the difference between Ultra2 and Ultra320 HBAs based
 * on their subsystem vendor and device IDs.  We need to check
 * the vendor id and device id instead.
 *
 * OK
 */
/* CSTYLED */
STATIC int
adpu320_probe(
register dev_info_t *dip
)
{
	ushort_t		vendorid;
	ushort_t		deviceid;

	if (adpu320_get_pci_id(dip, &vendorid, &deviceid) != DDI_SUCCESS) {
		return (DDI_PROBE_FAILURE);
	}

	if (vendorid != ADAPTEC_PCI_ID) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_probe: bad vendor %x\n",
		    vendorid);
#endif
		return (DDI_PROBE_FAILURE);
	}
	if ((deviceid != 0x8010) && (deviceid != 0x8011) &&
	    (deviceid != 0x8000) && (deviceid != 0x8012) &&
	    (deviceid != 0x8014) && (deviceid != 0x8015) &&
	    (deviceid != 0x8016) && (deviceid != 0x8017) &&
	    (deviceid != 0x801d) && (deviceid != 0x801e) &&
	    (deviceid != 0x801f) && (deviceid != 0x800f) &&
	    (deviceid != 0x808f)) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_probe: unsupported deviceid %x\n",
		    deviceid);
#endif
		return (DDI_PROBE_FAILURE);
	}

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_probe: done\n");
#endif

	return (DDI_SUCCESS);
}

int
adpu320_get_pci_id(
dev_info_t	*dip,
ushort_t	*vendorid,
ushort_t	*deviceid
)
{
	ddi_acc_handle_t	cfg_handle;

	if (pci_config_setup(dip, &cfg_handle) != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_get_pci_id: config_setup failed\n");
#endif
		return (DDI_FAILURE);
	}

	*vendorid = pci_config_get16(cfg_handle, PCI_CONF_VENID);

	*deviceid = pci_config_get16(cfg_handle, PCI_CONF_DEVID);

	pci_config_teardown(&cfg_handle);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_probe: vendor ID=%x "
		    "device ID=%x\n", *vendorid, *deviceid);
	}
#endif

	return (DDI_SUCCESS);
}

/*
 * Function : adpu320_attach()
 *
 * OK
 */
/*ARGSUSED*/
/* CSTYLED */
STATIC int
adpu320_attach(
dev_info_t		*dip,
ddi_attach_cmd_t	cmd
)
{
	adpu320_soft_state_t		*softp;
	adpu320_config_t		*cfp;
	int				instance;

	instance = ddi_get_instance(dip);

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_attach%d:\n", instance);
#if 0
	/*
	 * Call Kernel Debugger when the system call our adpu320_attach().
	 * For debug only.
	 */
	if (adpu320_debug & DVERBOS) {
		debug_enter("adpu320_attach");
	}
#endif
#endif

	switch (cmd) {
	case DDI_ATTACH:
		/*
		 * Allocate "adpu320_soft_state_t" + "struct cfg", and
		 * scsi_hba_tran_alloc
		 */
		softp = adpu320_alloc_adpu320(dip);

		if (softp == NULL) {
			return (DDI_FAILURE);
		}

		if (adpu320_cfpinit(dip, softp) != DDI_SUCCESS) {
			/*
			 * scsi_hba_tran_free(softp->a_tran); tran is NOT
			 * setup yet
			 */
			ddi_soft_state_free(adpu320_state, instance);

			return (DDI_FAILURE);
		}

#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			adpu320_printf("adpu320_attach%d: after "
			    "adpu320_cfpinit\n", instance);
		}
#endif

		if (adpu320_setup_tran(dip, softp) != DDI_SUCCESS) {
			/*
			 * All the memory has been free by the
			 * adpu320_setup_tran().
			 */
			return (DDI_FAILURE);
		}

#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			adpu320_printf("adpu320attach%d: after "
			    "adpu320_setup_tran\n", instance);
		}
#endif

		/*
		 * Add a software interrupt to the system.
		 * The interrupt is delivered with ddi_trigger_softintr
		 * below.
		 */
		if (ddi_add_softintr(
		    dip,
		    DDI_SOFTINT_LOW,
		    &softp->cfp->ab_softid,
		    0,
		    0,
		    adpu320_run_callbacks,
		    (caddr_t)softp->cfp) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "adpu320_attach%d: cannot add "
			    "softintr\n", instance);

			scsi_hba_tran_free(softp->a_tran);

			ddi_soft_state_free(adpu320_state, instance);

			return (DDI_FAILURE);
		}

#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			adpu320_printf("adpu320_attach%d: after "
			    "ddi_add_softintr\n", instance);
		}
#endif

		cfp = SOFT2CFP(softp);

		mutex_enter(&cfp->ab_mutex);

		/*
		 * Use the following function to scan all target devices
		 */
		if (adpu320_attach_PAC(softp->cfp) == ADPU320_FAILURE) {
#ifdef ADPU320_DEBUG
			adpu320_printf("adpu320_attach%d: "
			    "adpu320_attach_PAC failed\n", instance);
#endif
		/*
		 * RST - we can't just exit and go away, there
		 * may be a adpu320_run_callbacks pending
		 */
		mutex_exit(&cfp->ab_mutex);

		mutex_enter(&cfp->ab_mutex);

		ddi_remove_softintr(cfp->ab_softid);

		mutex_exit(&cfp->ab_mutex);

		cfp->ha_himFuncPtr->HIMDisableIRQ(cfp->ha_adapterTSH);

		adpu320_rem_intrs(softp);

		mutex_destroy(&cfp->ab_mutex);

		cv_destroy(&cfp->ab_quiesce_cv);

		adpu320_destroy_him_memory(cfp);

		scsi_hba_tran_free(softp->a_tran);

		ddi_soft_state_free(adpu320_state, instance);

		cmn_err(CE_WARN, "adpu320: adpu320 PAC failed !");

		return (DDI_FAILURE);
	}

	cfp->ha_himFuncPtr->HIMEnableIRQ(cfp->ha_adapterTSH);

#ifdef ADPU320_DEBUG
		/* adpu320_add_queue(&adpu320_cfp_list, cfp); */
#endif
		/*
		 * We grabbed this mutex after it was initialized in
		 * adpu320_cfginit.
		 */
		mutex_exit(&cfp->ab_mutex);

		adpu320_set_timeout(cfp);

#ifdef ADPU320_DEBUG
		if (adpu320_debug & DINIT) {
			adpu320_printf("adpu320_attach%d: softp %x blk %x\n",
			    instance, softp, softp->cfp);
		}
#endif

		return (DDI_SUCCESS);

	/*
	 * Restore the hardware state of a device after the system has been
	 * suspended
	 */
	case DDI_RESUME:
		/*
		 * According to Sun, this is not necessary to support for
		 * PCI hotplug
		 */
		return (DDI_FAILURE);

	default:
		return (DDI_FAILURE);
	}
}

/* CSTYLED */
STATIC int
adpu320_detach(
dev_info_t		*dip,
ddi_detach_cmd_t	cmd
)
{
	register adpu320_config_t	*cfp;
	adpu320_soft_state_t		*softp;
	int				instance;
	scsi_hba_tran_t			*tran;

	switch (cmd) {
	/*
	 * The system is attempting to unload the driver.  This can be used
	 * when memory is low or hardware will be removed.  The device will
	 * be closed at this point (the kernel makes sure of this).
	 */
	case DDI_DETACH:

		/*
		 * tran saved by scsi_hba_attach (3rd param) called in attach
		 */
		tran = ddi_get_driver_private(dip);

		if (tran == NULL) {
			return (DDI_SUCCESS);
		}

		softp = TRAN2ADPU320(tran);

		cfp = TRAN2CFP(tran);

		if (cfp == NULL) {
			return (DDI_SUCCESS);
		}

		mutex_enter(&adpu320_global_mutex);

		/*
		 * RST - check to make sure there are no outstanding requests,
		 * we need to cancel these requests if they are outstanding.
		 * we also need to cancel "cfp->adpu320_timeout_id"
		 */
		if (cfp->ab_child) {
#ifdef ADPU320_DEBUG
			if (adpu320_debug & DINIT) {
				adpu320_printf("adpu320_detach%d: blk %x "
				    "failure with %d children",
				    INST(cfp), cfp, cfp->ab_child);
			}
#endif
			return (DDI_FAILURE);
		}

		/*
		 * we also need to cancel "cfp->adpu320_timeout_id"
		 */
		untimeout(cfp->adpu320_timeout_id);

		ddi_remove_softintr(cfp->ab_softid);

		/*
		 * Disable interrupts on the board
		 */
		cfp->ha_himFuncPtr->HIMDisableIRQ(
		    cfp->ha_adapterTSH);

		/*
		 * Remove kernel interrupt handler
		 */
		adpu320_rem_intrs(softp);

		mutex_destroy(&cfp->adpu320_reset_notify_mutex);
		mutex_destroy(&cfp->ab_mutex);

		cv_destroy(&cfp->ab_quiesce_cv);

		mutex_exit(&adpu320_global_mutex);

		adpu320_destroy_him_memory(cfp);

		instance = softp->instance;

#ifdef ADPU320_DEBUG
		/* adpu320_remove_queue(&adpu320_cfp_list, cfp); */
#endif

		if (scsi_hba_detach(dip) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "!adpu320: scsi_hba_detach failed");
		}

		scsi_hba_tran_free(tran);

		ddi_prop_remove_all(dip);

		ddi_soft_state_free(adpu320_state, instance);

#ifdef ADPU320_DEBUG
		if (adpu320_debug & DINIT) {
			adpu320_printf("adpu320_detach%d: success", instance);
		}
#endif

		return (DDI_SUCCESS);

	/*
	 * This command is issued when the entire system is suspended before
	 * power is (possibly) removed or for dynamic reconfiguration.  All
	 * incoming or queued requests must be blocked until the system is
	 * resumed.
	 */
	case DDI_SUSPEND:
		/*
		 * According to Sun, this is not necessary to support for
		 * PCI hotplug
		 */
		return (DDI_FAILURE);

	default:
		return (DDI_FAILURE);
	}
}


/*
 * Function : adpu320_alloc_adpu320()
 *
 * Description :
 * 	Allocate an softp structure, a adpu320_config_t structure
 *  and call scsi_hba_tran_alloc to allocate a scsi_tranport
 *	structure
 * OK
 */
/* CSTYLED */
STATIC
adpu320_soft_state_t *
adpu320_alloc_adpu320(
dev_info_t	*dip
)
{
	adpu320_soft_state_t	*softp;
	int			instance;

	/*
	 * DDI-2.5
	 */
	instance = ddi_get_instance(dip);

	if (ddi_soft_state_zalloc(adpu320_state, instance) != DDI_SUCCESS) {
		return ((adpu320_soft_state_t *)NULL);
	}

	softp = (adpu320_soft_state_t *)
	    ddi_get_soft_state(adpu320_state, instance);

	if (softp == (adpu320_soft_state_t *)NULL) {
		return ((adpu320_soft_state_t *)NULL);
	}

	softp->cfp = (adpu320_config_t *)(softp + 1);
	softp->cfp->ab_dip = dip;

	if ((softp->a_tran = scsi_hba_tran_alloc(dip, 0)) ==
	    (scsi_hba_tran_t *)NULL) {
		ddi_soft_state_free(adpu320_state, instance);
		return ((adpu320_soft_state_t *)NULL);
	}

	/*
	 * this enables graceful failure early in adpu320_attach until fix in
	 * scsi_hba_tran_alloc XXX
	 */
	softp->a_tran->tran_hba_dip = dip;

	softp->instance = instance;

	return (softp);
}

/*
 * Function : adpu320_setup_tran()
 *
 * OK
 */
/* CSTYLED */
STATIC int
adpu320_setup_tran(
dev_info_t		*dip,
adpu320_soft_state_t	*softp
)
{
	adpu320_config_t		*cfp;
	register scsi_hba_tran_t	*hba_tran;
	int				instance;

	cfp = softp->cfp;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_setup_tran%d:\n", INST(cfp));
#endif

	hba_tran = softp->a_tran;

	/*
	 * tgt_private always points to the adpu320 structure
	 */
	hba_tran->tran_tgt_private	= softp;

	/*
	 * hba_private always points to the adpu320_config_t struct
	 */
	hba_tran->tran_hba_private	= softp->cfp;
	hba_tran->tran_tgt_init		= adpu320_tran_tgt_init;
	hba_tran->tran_tgt_probe	= adpu320_tran_tgt_probe;
	hba_tran->tran_tgt_free		= adpu320_tran_tgt_free;
	hba_tran->tran_start 		= adpu320_tran_start;
	hba_tran->tran_abort		= adpu320_tran_abort;
	hba_tran->tran_reset		= adpu320_tran_reset;
	hba_tran->tran_reset_notify	= adpu320_tran_reset_notify;
	hba_tran->tran_getcap		= adpu320_tran_getcap;
	hba_tran->tran_setcap		= adpu320_tran_setcap;
	hba_tran->tran_quiesce		= adpu320_tran_quiesce;
	hba_tran->tran_unquiesce	= adpu320_tran_unquiesce;
	hba_tran->tran_pkt_constructor	= adpu320_constructor;
	hba_tran->tran_pkt_destructor	= adpu320_destructor;
	hba_tran->tran_setup_pkt	= adpu320_tran_setup_pkt;
	hba_tran->tran_teardown_pkt	= adpu320_tran_teardown_pkt;
	hba_tran->tran_hba_len		= sizeof (struct adpu320_scsi_cmd);

	if (scsi_hba_attach_setup(dip, &adpu320_dmalim, hba_tran,
	    SCSI_HBA_TRAN_CLONE) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "adpu320_attach: scsi_hba_attach fail");

#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_setup_tran failed\n");
#endif

		mutex_enter(&adpu320_global_mutex);

		cfp->ha_himFuncPtr->HIMDisableIRQ(
		    cfp->ha_adapterTSH);

		adpu320_rem_intrs(softp);

		mutex_destroy(&cfp->ab_mutex);

		cv_destroy(&cfp->ab_quiesce_cv);

		adpu320_destroy_him_memory(cfp);

		mutex_exit(&adpu320_global_mutex);

		instance = softp->instance;

		ddi_soft_state_free(adpu320_state, instance);

		scsi_hba_tran_free(hba_tran);

		return (DDI_FAILURE);
	}

	ddi_report_dev(dip);

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_setup_tran%d: success\n", INST(cfp));
#endif

	return (DDI_SUCCESS);
}

/*
 * Function : adpu320_cfpinit()
 *
 * OK
 */
/* CSTYLED */
STATIC int
adpu320_cfpinit(
dev_info_t		*dip,
adpu320_soft_state_t	*softp
)
{
	int 			bus;
	int 			device;
	int 			pcifunc;
	adpu320_config_t 	*cfp;
	int 			*regp;
	int 			reglen;
	ushort_t		venID;
	ushort_t		devID;

	cfp = softp->cfp;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_cfpinit%d:\n", INST(cfp));
#endif

	/*
	 * get the hba's address
	 */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&regp, &reglen) != DDI_PROP_SUCCESS) {
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DINIT) {
			adpu320_printf("adpu320_cfpinit%d: reg property not "
			    "found\n", INST(cfp));
		}
#endif
		return (DDI_FAILURE);
	}

	/*
	 * get the values from the first member of the reg triplet
	 */
	device = PCI_REG_DEV_G(*regp);
	bus = PCI_REG_BUS_G(*regp);
	pcifunc = PCI_REG_FUNC_G(*regp);
	kmem_free(regp, reglen);

	if (adpu320_get_pci_id(dip, &venID, &devID) != DDI_SUCCESS)
		return (DDI_FAILURE);

	mutex_enter(&adpu320_global_mutex);

	cfp->Cf_id.id_struct.VendorId = venID;

	cfp->Cf_id.id_struct.AdapterId = devID;

	cfp->ha_BusNumber = (unsigned char) bus;
	cfp->ha_DeviceNumber = (unsigned char) device;
	cfp->ha_FuncNumber = (unsigned char) pcifunc;

	if (adpu320_cfginit(softp) != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DINIT) {
			adpu320_printf("adpu320_cfpinit%d: adpu320_cfginit "
			    "failed\n",  INST(cfp));
		}
#endif
		mutex_exit(&adpu320_global_mutex);

		return (DDI_FAILURE);
	}

	mutex_exit(&adpu320_global_mutex);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DINIT) {
		adpu320_printf("adpu320_cfpinit%d: done\n", INST(cfp));
	}
#endif

	mutex_init(&cfp->adpu320_reset_notify_mutex, NULL, MUTEX_DRIVER,
	    (void *)NULL);

	L2_INIT(&cfp->adpu320_reset_notify_list);

	return (DDI_SUCCESS);
}

adpu320_dma_info_t	*
adpu320_alloc_dmainfo(
adpu320_config_t 	*cfp,
unsigned		memSize
)
{
	adpu320_dma_info_t	*dma_infop;
	void			*pMemory;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_alloc_dmainfo%d: size 0x%x\n",
	    INST(cfp), memSize);
#endif

	dma_infop = (adpu320_dma_info_t *)
	    kmem_zalloc(sizeof (adpu320_dma_info_t), KM_SLEEP);

	pMemory = (void *)kmem_zalloc(memSize, KM_NOSLEEP);

	if (pMemory == NULL) {
		kmem_free(dma_infop, sizeof (adpu320_dma_info_t));

		return (NULL);
	}

	dma_infop->type = ADPU320_NONCONTIG_MEM;

	dma_infop->size = memSize;

	dma_infop->virt_addr = pMemory;

	/*
	 * In the case of non LOCKED memory, the CHIM will not ask for the
	 * physical address;
	 */
	dma_infop->phys_addr = (HIM_BUS_ADDRESS)NULL;

	/*
	 * Put this dma_infop on the chain so that we can free it later.
	 */
	dma_infop->next = cfp->ab_dmainfo;

	cfp->ab_dmainfo = dma_infop;

	return (dma_infop);
}

adpu320_dma_info_t	*
adpu320_alloc_contig_dmainfo(
adpu320_config_t 	*cfp,
unsigned		memSize,
HIM_ULONG 		alignMask
)
{
	caddr_t 		buf2;
	int			retval;
	size_t 			real_length;
	adpu320_dma_info_t	*dma_infop;
	ddi_dma_cookie_t	dmacookie;
	uint_t 			count;
	unsigned		non_aligned_size;
	caddr_t			non_aligned_addr;
	HIM_BUS_ADDRESS		busAlignMask;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_alloc_contig_dmainfo%d: size 0x%x\n",
	    INST(cfp), memSize);
#endif

	busAlignMask = (HIM_BUS_ADDRESS)alignMask;

	dma_infop = (adpu320_dma_info_t *)
	    kmem_zalloc(sizeof (adpu320_dma_info_t), KM_SLEEP);

	/*
	 * Setup up the memory alignment
	 */
	adpu320_contig_dmalim.dma_attr_align = 1;

	non_aligned_size = memSize + alignMask + 1;

	retval = ddi_dma_alloc_handle(
	    cfp->ab_dip,
	    &adpu320_contig_dmalim,
	    DDI_DMA_SLEEP,
	    NULL,
	    &dma_infop->ab_dmahandle);

	if (retval != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_alloc_dmainfo : dma handle alloc "
		    "fail %d\n", retval);
#endif
		cmn_err(CE_WARN, "adpu320_alloc_dmainfo : dma handle alloc "
		    "fail");

		kmem_free(dma_infop, sizeof (adpu320_dma_info_t));

		return (NULL);
	}

	retval = ddi_dma_mem_alloc(
	    dma_infop->ab_dmahandle,
	    non_aligned_size,
	    &adpu320_attr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP,
	    NULL,
	    &non_aligned_addr,
	    &real_length,
	    &dma_infop->ab_handle);

	if (retval == DDI_FAILURE) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_alloc_dmainfo : HIM LOCKED "
		    "memory alloc fail\n");
#endif

#if 0
		cmn_err(CE_WARN,
		    "adpu320_alloc_dmainfo : HIM LOCKED memory alloc fail"
		    " memSize 0x%x", memSize);
#endif

		ddi_dma_free_handle(&dma_infop->ab_dmahandle);

		kmem_free(dma_infop, sizeof (adpu320_dma_info_t));

		return (NULL);
	}

	retval = ddi_dma_addr_bind_handle(
	    dma_infop->ab_dmahandle,
	    NULL,
	    non_aligned_addr,
	    non_aligned_size,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP,
	    NULL,
	    &dmacookie,
	    &count);

	if (retval != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_alloc_dmainfo : HIM LOCKED memory addr "
		    "bind fail %d\n", retval);
#endif
		cmn_err(CE_WARN, "adpu320_alloc_dmainfo : HIM LOCKED memory "
		    "addr bind fail");

		ddi_dma_mem_free(&dma_infop->ab_handle);

		ddi_dma_free_handle(&dma_infop->ab_dmahandle);

		kmem_free(dma_infop, sizeof (adpu320_dma_info_t));

		return (NULL);
	}

	buf2 = (caddr_t)
	    ((uintptr_t)(non_aligned_addr + alignMask) & ~alignMask);

	bzero(buf2, memSize);

	dma_infop->virt_addr = (uchar_t *)buf2;

#if OSM_BUS_ADDRESS_SIZE == 64
	dma_infop->phys_addr = (dmacookie.dmac_laddress
	    + busAlignMask) & ~busAlignMask;
#else
	dma_infop->phys_addr = (dmacookie.dmac_address
	    + busAlignMask) & ~busAlignMask;
#endif

	dma_infop->type = ADPU320_CONTIG_MEM;

	dma_infop->size = memSize;

	/*
	 * Put this dma_infop on the chain so that we can free it later.
	 */
	dma_infop->next = cfp->ab_dmainfo;

	cfp->ab_dmainfo = dma_infop;

	return (dma_infop);
}

HIM_BOOLEAN
adpu320_alloc_him_pointer(
adpu320_config_t 	*cfp,
unsigned		i,
HIM_UINT32 		memSize,
HIM_UINT32 		minBytes,
HIM_UINT32 		granularity,
HIM_UINT8		memCategory,
HIM_ULONG 		alignMask
)
{
	adpu320_dma_info_t	*dma_infop;
	unsigned		size;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_alloc_him_pointer : size 0x%x minBytes 0x%x "
	    "index 0x%x granularity 0x%x memCategory 0x%x alignMask 0x%x\n",
	    memSize, minBytes, i, granularity, memCategory, alignMask);
#endif

	dma_infop = NULL;

	switch (memCategory) {
	case HIM_MC_UNLOCKED:
		/*
		 * Try to get as much memory as possible
		 */
		while (memSize >= minBytes) {
			dma_infop = adpu320_alloc_dmainfo(cfp,
			    memSize);

			if (dma_infop != NULL) {
				break;
			}

			memSize -= granularity;
		}

		if (dma_infop != NULL)
			break;

		/*
		 * If we didn't get the memory in that loop, try again
		 * with the minimum amount of memory possible.
		 */
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			adpu320_printf("productID 0x%x : "
			    "HIMCheckMemoryNeeded on NULL.",
			    cfp->Cf_id.ha_productID);
		}
#endif
		dma_infop = adpu320_alloc_dmainfo(cfp, minBytes);

		if (dma_infop != NULL) {
			break;
		}

		/*
		 * We didn't get the memory at all.  Give up.
		 */
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			adpu320_printf("productID 0x%x : The memory allocation "
			    "NULL.", cfp->Cf_id.ha_productID);
		}

#endif
		return (HIM_FALSE);

	case HIM_MC_LOCKED:
		dma_infop = adpu320_alloc_contig_dmainfo(cfp, memSize,
		    alignMask);

		if (dma_infop != NULL) {
			break;
		}

		for (size = 1; size < memSize; size = size * 2)
			continue;

		for (size = size / 2; size >= minBytes; size = size / 2) {
			dma_infop = adpu320_alloc_contig_dmainfo(cfp,
			    size, alignMask);

			if (dma_infop != NULL) {
				break;
			}
		}

		if (dma_infop != NULL) {
#if 0
			cmn_err(CE_WARN, "adpu320: allocated memsize 0x%x\n",
			    size);
#endif
			break;
		}

		/*
		 * We didn't get the memory at all.  Give up.
		 */
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			adpu320_printf("productID 0x%x : The memory allocation "
			    "NULL.", cfp->Cf_id.ha_productID);
		}

#endif
		return (HIM_FALSE);

	default:
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			adpu320_printf("productID 0x%x : Illegal "
			    "memory category.",
			    cfp->Cf_id.ha_productID);
		}
#endif
		return (HIM_FALSE);
	}

#ifdef ADPU320_DEBUG
#if OSM_BUS_ADDRESS_SIZE == 64
	adpu320_printf("adpu320_alloc_him_pointer : allocated "
	    "vaddr = 0x%x paddr = 0x%llx size = 0x%x\n",
	    dma_infop->virt_addr,
	    dma_infop->phys_addr, dma_infop->size);
#else
	adpu320_printf("adpu320_alloc_him_pointer : allocated "
	    "vaddr = 0x%x paddr = 0x%x size = 0x%x\n", dma_infop->virt_addr,
	    dma_infop->phys_addr, dma_infop->size);
#endif
#endif

	if ((cfp->ha_himFuncPtr->HIMSetMemoryPointer(
	    cfp->ha_adapterTSH, i, memCategory,
	    (void *)dma_infop->virt_addr, dma_infop->size)) != 0) {
		cmn_err(CE_WARN, "Mem Not accepted(PID=0x%x) ",
		    cfp->Cf_id.ha_productID);

		/*
		 * The memory is not acceptable by HIM
		 */
		return (HIM_FALSE);
	}

	return (HIM_TRUE);
}

void
adpu320_destroy_him_memory(
adpu320_config_t 	*cfp
)
{
	adpu320_dma_info_t	*dma_infop;
	adpu320_dma_info_t	*dma_infop_next;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_destroy_him_memory :\n");
#endif

	for (dma_infop = cfp->ab_dmainfo; dma_infop != NULL;
	    dma_infop = dma_infop_next) {
		switch (dma_infop->type) {
		case ADPU320_NONCONTIG_MEM:
			kmem_free(dma_infop->virt_addr,
			    dma_infop->size);
			break;

		case ADPU320_CONTIG_MEM:
			ddi_dma_unbind_handle(dma_infop->ab_dmahandle);
			ddi_dma_mem_free(&dma_infop->ab_handle);
			ddi_dma_free_handle(&dma_infop->ab_dmahandle);

			break;
		}

		dma_infop_next = dma_infop->next;

		kmem_free(dma_infop, sizeof (adpu320_dma_info_t));
	}

	cfp->ab_dmainfo = NULL;
}


HIM_BOOLEAN
adpu320_alloc_him_memory(
adpu320_config_t 		*cfp,
HIM_TASK_SET_HANDLE		adpu320_InitTSH
)
{
	HIM_UINT32 	memSize;
	HIM_UINT32 	minBytes;
	HIM_UINT32 	granularity;
	HIM_ULONG 	alignMask;
	HIM_UINT8	memCategory;
	int		i;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_alloc_him_memory :\n");
#endif

	i = 0;

	while ((memSize = cfp->ha_himFuncPtr->HIMCheckMemoryNeeded(
	    adpu320_InitTSH, cfp->ha_adapterTSH,
	    cfp->Cf_id.ha_productID, i, &memCategory,
	    &minBytes, &granularity, &alignMask)) != 0) {
#ifdef ADPU320_DEBUG
		adpu320_printf("HIMCheckMemoryNeeded memSize = 0x%x\n",
		    memSize, memCategory);
#endif

		if (adpu320_alloc_him_pointer(cfp, i, memSize,
		    minBytes, granularity, memCategory, alignMask)
		    == HIM_FALSE) {
			adpu320_destroy_him_memory(cfp);

			return (HIM_FALSE);
		}

		i++;
	}

	return (HIM_TRUE);
}

#define	HIM_MAX_INDEX	100
#define	HIM_SCSI	0

/*
 * Function : adpu320_cfginit()
 *
 * This is called with the global mutex held
 * OK
 */
/* CSTYLED */
STATIC int
adpu320_cfginit(
adpu320_soft_state_t *softp
)
{
	adpu320_config_t 	*cfp;
	int 			intr_idx;
	int			num_scsi_channels;
	int			x, i, adpu320UserOpt;
	int			mem_release;
	int			conflag;
	int			intr_types;
	HIM_UINT16		index;
	HIM_UINT16		gb_scsi;
	HIM_UINT8		adpu320_NumScsiProductSupp;
	HIM_UINT8		adpu320_bustype;
	int			adpu320_habuscnt;
	HIM_TASK_SET_HANDLE	adpu320_HimInitTSCBp;
	HIM_TASK_SET_HANDLE	adpu320_InitTSH;
	HIM_TASK_SET_HANDLE	adpu320_TscbPtr;
	HIM_UINT32		adpu320_HimTscbSize;
	HIM_UINT32		adpu320_HimMaxInitTscbSize;
	HIM_CONFIGURATION	*adpu320_HimConfig;
	HIM_HOST_ADDRESS	*hostAddress;
	HIM_FUNC_PTRS		*adpu320_ScsiHimFuncPtr;
	HIM_OSM_FUNC_PTRS	*osmRoutines;

#ifdef HIM_INCLUDE_SCSI
	struct adpu320_productInfo adpu320_ScsiProductInfo[HIM_MAX_INDEX];
#endif /* HIM_INCLUDE_SCSI */

	cfp = softp->cfp;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_cfginit%d:\n", INST(cfp));
	}
#endif


/*
 *      Because multiple HIM implementations can be linked into a single driver,
 *      acquiring function pointer to each HIM for initialization and run-time
 *      operation.
 */
#ifdef HIM_INCLUDE_SCSI
	adpu320_ScsiHimFuncPtr = (HIM_FUNC_PTRS *)kmem_zalloc(
	    sizeof (HIM_FUNC_PTRS), KM_SLEEP);

	HIM_ENTRYNAME_SCSI(adpu320_ScsiHimFuncPtr, sizeof (HIM_FUNC_PTRS));
#endif

#ifdef HIM_INCLUDE_SCSI
	/*
	 * Find the vendor IDs of PCI Host Device that the HIM supports
	 */
	adpu320_NumScsiProductSupp = 0;

	/*
	 * FOR loop to search for HIM supported PCI SCSI products
	 */
	for (index = 0; index < HIM_MAX_INDEX; index++) {
		adpu320_ScsiProductInfo[index].id =
		    adpu320_ScsiHimFuncPtr->HIMGetNextHostDeviceType(
		    index, &adpu320_bustype,
		    &adpu320_ScsiProductInfo[index].mask);

		if (adpu320_bustype != HIM_HOST_BUS_PCI) {
			continue;	/* adpu320 only supports the PCI */
		}

		if (adpu320_ScsiProductInfo[index].id == 0) {
			break;	/* No more supported products */
		}
		else
		{
			adpu320_ScsiProductInfo[index].index = index;

			/* This Host Device supported by HIM */
			adpu320_ScsiProductInfo[index].himtype = HIM_SCSI;

			adpu320_NumScsiProductSupp++;
		}
	}

	if (adpu320_NumScsiProductSupp == 0) {
		adpu320_HimMaxInitTscbSize = 0;
	}
	else
	{
		adpu320_HimMaxInitTscbSize = HIM_SCSI_INIT_TSCB_SIZE;
	}

	cfp->ha_himFuncPtr = adpu320_ScsiHimFuncPtr;

#endif /* HIM_INCLUDE_SCSI */

	/*
	 *  Initialization  Task Set Handle  and Task Set Control Block
	 */
	if (adpu320_HimMaxInitTscbSize == 0) {
		return (DDI_FAILURE); /* HIM does not support this product */
	}

	adpu320_HimInitTSCBp = (void HIM_PTR)kmem_zalloc(
	    adpu320_HimMaxInitTscbSize, KM_SLEEP);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_cfginit%d: "
		    "HIMCreateInitializationTSCB\n", INST(cfp));
	}
#endif

	adpu320_InitTSH = cfp->ha_himFuncPtr->HIMCreateInitializationTSCB(
	    adpu320_HimInitTSCBp);

	/*
	 * Determine max IOBS in the queue
	 */
	cfp->ab_ha_profilesize = sizeof (HIM_ADAPTER_PROFILE);

	adpu320_habuscnt++;

	mem_release = 0;

	hostAddress = (HIM_HOST_ADDRESS *)kmem_zalloc(
	    sizeof (HIM_HOST_ADDRESS), KM_SLEEP);

	mem_release |= HA_ADDR_REL;

#ifdef HIM_INCLUDE_SCSI
	/*
	 * If at least 1 PCI SCSI supported product found then we save the
	 * HIM function entry into ha_himFuncPtr for use later on
	 */

	if (adpu320_NumScsiProductSupp) {
		for (gb_scsi = 0; gb_scsi < adpu320_NumScsiProductSupp;
		    gb_scsi++) {
#ifdef ADPU320_DEBUG
			if (adpu320_debug & DVERBOS) {
				adpu320_printf("adpu320_cfginit%d: OS product "
				    "id = %x, HIM product id = %x\n",
				    INST(cfp),
				    cfp->Cf_id.ha_productID,
				    adpu320_ScsiProductInfo[gb_scsi].id);
			}
#endif
			if ((cfp->Cf_id.ha_productID &
			    adpu320_ScsiProductInfo[gb_scsi].mask)
			    == adpu320_ScsiProductInfo[gb_scsi].id) {
				cfp->ha_himFuncPtr = adpu320_ScsiHimFuncPtr;

				goto adpu320_supported;
			}
		}
	}
#endif /* HIM_INCLUDE_SCSI */

adpu320_supported:

	/*
	 * Adapter  GetConfiguration
	 */

	adpu320_HimConfig = (HIM_CONFIGURATION *)
	    kmem_zalloc(sizeof (HIM_CONFIGURATION), KM_SLEEP);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_cfginit%d: Call HIMGetConfiguration\n",
		    INST(cfp));
	}
#endif

	cfp->ha_himFuncPtr->HIMGetConfiguration(adpu320_InitTSH,
	    adpu320_HimConfig, cfp->Cf_id.ha_productID);

	cfp->ha_himConfig = adpu320_HimConfig;

	conflag = 0;

	if (adpu320_HimConfig->memoryMapped != adpu320_mem_map) {
		if ((adpu320_mem_map != HIM_MEMORYSPACE) &&
		    (adpu320_mem_map != HIM_IOSPACE)) {
			/*
			 * default value
			 */
			adpu320_HimConfig->memoryMapped = HIM_IOSPACE;
		}
		else
		{
			adpu320_HimConfig->memoryMapped = adpu320_mem_map;
		}

		conflag = 1;
	}

	if (adpu320_HimConfig->maxTargets > ADPU320_MAX_TARGETS) {
		adpu320_HimConfig->maxTargets = ADPU320_MAX_TARGETS;
		conflag = 1;
	}
	if (adpu320_HimConfig->maxSGDescriptors > ADPU320_MAX_DMA_SEGS) {
		adpu320_HimConfig->maxSGDescriptors = ADPU320_MAX_DMA_SEGS;
		conflag = 1;
	}

	if (conflag) {
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			adpu320_printf("adpu320_cfginit%d: "
			    "HIMSetConfiguration\n", INST(cfp));
		}
#endif
		cfp->ha_himFuncPtr->HIMSetConfiguration(adpu320_InitTSH,
		    adpu320_HimConfig, cfp->Cf_id.ha_productID);

		conflag = 0;
	}

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_cfginit%d: HIMSizeAdapterTSCB\n",
		    INST(cfp));
	}
#endif

	/*
	 *	For each supported adapter found, allocate a Task Set Control
	 *	Block and acquire an Adapter Task Set Handle to allow HIM to
	 *	validate the adapter's existence, but it does not yet
	 *	initialize the hardware.
	 */
	adpu320_HimTscbSize = cfp->ha_himFuncPtr->HIMSizeAdapterTSCB(
	    adpu320_InitTSH, cfp->Cf_id.ha_productID);

	adpu320_TscbPtr = (void *)kmem_zalloc(adpu320_HimTscbSize, KM_SLEEP);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_cfginit%d: HIMCreateAdapterTSCB\n",
		    INST(cfp));
	}
#endif
hostAddress->pciAddress.busNumber = cfp->ha_BusNumber;
hostAddress->pciAddress.deviceNumber = cfp->ha_DeviceNumber;
hostAddress->pciAddress.functionNumber = cfp->ha_FuncNumber;
	cfp->ha_adapterTSH = cfp->ha_himFuncPtr->HIMCreateAdapterTSCB(
	    adpu320_InitTSH, adpu320_TscbPtr, cfp, *hostAddress,
	    cfp->Cf_id.ha_productID);


	/*
	 * Passing OSM Function Pointers to the HIM
	 */
	osmRoutines = (HIM_OSM_FUNC_PTRS *)kmem_alloc(
	    sizeof (HIM_OSM_FUNC_PTRS), KM_SLEEP);

	adpu320_FillOutOSMPointers(osmRoutines);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_cfginit%d: HIMSetupAdapterTSCB\n",
		    INST(cfp));
	}
#endif

	if (cfp->ha_himFuncPtr->HIMSetupAdapterTSCB(
	    cfp->ha_adapterTSH, osmRoutines,
	    (HIM_UINT16)(sizeof (HIM_OSM_FUNC_PTRS))) != HIM_SUCCESS) {
		cmn_err(CE_WARN, "HIM SetupAdapterTSCB failed  ");

		goto adpu320_release;
	}

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_cfginit%d: HIMVerifyAdapter\n",
		    INST(cfp));
	}
#endif

	/*
	 * Verify Adapter Existence
	 */
	if (cfp->ha_himFuncPtr->HIMVerifyAdapter(
	    cfp->ha_adapterTSH) != HIM_SUCCESS) {
		cmn_err(CE_WARN, " This adapter is not verified by HIM");

		goto adpu320_release;
	}

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_cfginit%d: adpu320_alloc_him_memory\n",
		    INST(cfp));
	}
#endif

	/*
	 * Memory management is implemented and controlled by OSM. Call
	 * this routine to determine the amount of memory in each
	 * category reqired by HIM.
	 */
	if (adpu320_alloc_him_memory(cfp, adpu320_InitTSH) == HIM_FALSE) {
		goto adpu320_release;
	}

	/*
	 *	Now the profile will be called for physical information about
	 *	an adapter.
	 */

	/* Please make sure (cfp->ab_ha_profilesize) is not 0 */
	cfp->ha_profile = (HIM_ADAPTER_PROFILE *)kmem_zalloc(
	    cfp->ab_ha_profilesize, KM_SLEEP);

	mem_release |= HA_PROFILE_REL;

	/*
	 * Create a linked list to keep track of the pointer to sense data
	 * buffer
	 */
	cfp->Ptr_List = (element_list_t *)kmem_zalloc(
	    sizeof (element_list_t), KM_SLEEP);

	/* Create a first dummy element */

	cfp->Ptr_List->first_element =
	    (element_t *)kmem_zalloc(sizeof (element_t), KM_SLEEP);

	cfp->Ptr_List->last_element =
	    cfp->Ptr_List->first_element;


	/*
	 * Perform Adapter Initialization
	 */
#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_cfginit%d: HIMProfileAdapter\n",
		    INST(cfp));
	}
#endif
	cfp->ha_himFuncPtr->HIMProfileAdapter(
	    cfp->ha_adapterTSH, cfp->ha_profile);

	/*
	 * mdt workaround for IBM seagate sp40-00 tape and other devices that
	 * can not handles inq > 8 luns
	 */
	adpu320UserOpt = ddi_prop_get_int(DDI_DEV_T_ANY, cfp->ab_dip,
	    DDI_PROP_DONTPASS,
	    "ADPU320_SCSI_NLUN_SUPPORT", -1);

	if ((adpu320UserOpt >= 1) && (adpu320UserOpt <= 64)) {
		for (i = 0; i < 16; i++)
			cfp->ha_profile->himu.TS_SCSI.AP_SCSINumberLuns[i] =
			    adpu320UserOpt;
	} else {
		for (i = 0; i < 16; i++)
			cfp->ha_profile->himu.TS_SCSI.AP_SCSINumberLuns[i] = 8;
	}
	adpu320UserOpt = ddi_prop_get_int(DDI_DEV_T_ANY, cfp->ab_dip, 0,
	    "initiator-id",
	    -1);
	if (adpu320UserOpt == -1) {
		adpu320UserOpt = ddi_prop_get_int(DDI_DEV_T_ANY, cfp->ab_dip, 0,
		    "scsi-initiator-id", -1);
	}

	if (adpu320UserOpt >= 0 && adpu320UserOpt < ADPU320_MAX_TARGETS) {
		cfp->ha_profile->himu.TS_SCSI.AP_SCSIAdapterID = adpu320UserOpt;

	}
	cfp->ha_himFuncPtr->HIMAdjustAdapterProfile(
	    cfp->ha_adapterTSH, cfp->ha_profile);
	cfp->ha_himFuncPtr->HIMProfileAdapter(
	    cfp->ha_adapterTSH, cfp->ha_profile);
/* mdt011603 */

#ifdef ADPU320_DEBUG
	adpu320_dump_profile(cfp->ha_profile);
#endif
	cfp->ha_MaxTargets =
	    cfp->ha_profile->AP_MaxTargets;

	cfp->ha_ScsiId =
	    cfp->ha_profile->himu.TS_SCSI.AP_SCSIAdapterID;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_cfginit%d: HIMInitialize\n", INST(cfp));
	}
#endif

	if (cfp->ha_himFuncPtr->HIMInitialize(cfp->ha_adapterTSH) !=
	    HIM_SUCCESS) {
		cmn_err(CE_WARN, "HIMInitialize failed PID=0x%x  ",
		    cfp->Cf_id.ha_productID);
		/* Adapter hardware failed initialization */
		goto adpu320_release;
	}

	/*
	 * Attach interrupts for each "active" device. The driver
	 * must set the active filed of the idata structure to 1
	 * for each device it has configured.
	 *
	 * Note: Interrupts are attached here, in init(), as opposed
	 * to load(), because this is required for static autoconfig
	 * drivers as well as loadable.
	 */

	/* Get supported interrupt types */
	if (ddi_intr_get_supported_types(cfp->ab_dip, &intr_types)
	    != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!adpu320_cfginit: "
		    "ddi_intr_get_supported_types failed\n");
		adpu320_destroy_him_memory(cfp);
		goto adpu320_release;
	}

	if (intr_types & DDI_INTR_TYPE_MSI) {
		/* Try MSI first, but fall back to FIXED if MSI attach fails */
		if (adpu320_add_intrs(softp, DDI_INTR_TYPE_MSI)
		    == DDI_SUCCESS) {
			cfp->ab_intr_type = DDI_INTR_TYPE_MSI;
			goto adpu320_intrdone;
		}
		cmn_err(CE_WARN, "!MSI registration failed, "
		    "trying FIXED interrupts");
	}

	if (intr_types & DDI_INTR_TYPE_FIXED) {
		if (adpu320_add_intrs(softp, DDI_INTR_TYPE_FIXED)
		    == DDI_SUCCESS) {
			cfp->ab_intr_type = DDI_INTR_TYPE_FIXED;
			goto adpu320_intrdone;
		}
		cmn_err(CE_WARN, "!FIXED interrupt registration failed");
	}

	adpu320_destroy_him_memory(cfp);
	goto adpu320_release;

adpu320_intrdone:
	mutex_init(&cfp->ab_mutex, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(cfp->ab_intr_pri));

	cv_init(&cfp->ab_quiesce_cv, NULL, CV_DRIVER, NULL);

	kmem_free(adpu320_HimInitTSCBp, adpu320_HimMaxInitTscbSize);

	kmem_free(hostAddress, sizeof (HIM_HOST_ADDRESS));
	kmem_free(osmRoutines, sizeof (HIM_OSM_FUNC_PTRS));

	return (DDI_SUCCESS);

adpu320_release:
	kmem_free(hostAddress, sizeof (HIM_HOST_ADDRESS));
	kmem_free(osmRoutines, sizeof (HIM_OSM_FUNC_PTRS));
	if (num_scsi_channels == 1) {
		mem_release |= HA_STRUCT_REL;
	}
	else
	{
		if (x == 0) {
			mem_release |= HA_CH0_REL;
		}
		else
		{
			mem_release |= HA_CH1_REL;
		}
	}

	adpu320_mem_release(cfp, mem_release);

#ifdef ADPU320_DEBUG
	if (adpu320_habuscnt == 0) {
		adpu320_printf("!No HBA's found.\n");
	}
	else
	{
		adpu320_printf("adpu320_cfginit : Failed to initialize "
		    "host adapter\n");
	}

	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_cfginit%d: done\n", INST(cfp));
	}
#endif

	return (DDI_FAILURE);
}

/*
 * Called during attach to generate a PAC. Even though interrupts should
 * be off at this point, we are assuming that an event could have already
 * happened.
 */
int
adpu320_attach_PAC(
adpu320_config_t	*cfp
)
{
	struct adpu320_scsi_cmd	*cmd;
	int			count;
	int			time;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_attach_PAC%d:\n", INST(cfp));
#endif

	for (count = 0; count < ADPU320_PAC_RETRY_COUNT; count++) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_attach_PAC%d: count=0x%x\n",
		    INST(cfp), count);
#endif

		time = cfp->ha_profile->AP_AutoConfigTimeout * 2000;

#ifdef ADPU320_DEBUG
		if (ADPU320_QUEUE_FROZEN | ADPU320_PAC_ACTIVE) {
			adpu320_printf("adpu320_attach_PAC%d: waiting for "
			    "active PAC\n", INST(cfp));
		}
#endif
		/*
		 * Check to see if a PAC is already active.
		 */
		while (cfp->ab_flag &
		    (ADPU320_QUEUE_FROZEN | ADPU320_PAC_ACTIVE)) {
			/*
			 * More than likely, we received an event during one
			 * of our PACs, and we now are blocked from resending
			 * a new PAC until the previous one is finished.
			 */
			if (time <= 0) {
				break;
			}

			/*
			 * Interrupts are disabled, so we have to poll until
			 * this condition clears.
			 */
			adpu320_poll_request(cfp);

			drv_usecwait(1000);

			time--;
		}

		/*
		 * Check to see if the command has timed out.  We should
		 * probably abort this command, but this should never happen.
		 */
		if (time <= 0) {
			cmn_err(CE_WARN, "adpu320_IobSpecial: "
			    "adpu320_attach_PAC timeout ");

			cfp->ab_flag &=
			    ~(ADPU320_QUEUE_FROZEN | ADPU320_PAC_ACTIVE);

			/*
			 * For whatever reason, the PAC that interrupted our
			 * original PAC never finished.  There may be a
			 * hardware problem, but we will keep trying.
			 */
#ifdef ADPU320_DEBUG
			debug_enter("PAC Timeout");
#endif
		}

		cmd = adpu320_IobSpecial(cfp, NULL, HIM_PROTOCOL_AUTO_CONFIG, 0,
		    CFLAG_POLL);

		if (adpu320_wait_special_iob_complete(cmd) != 0) {
			break;
		}

		/*
		 * We do not have to worry about completing any commands
		 * at this point, because there have been none sent.
		 * We do not need to call adpu320_drain_completed_queue.
		 */

#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_attach_PAC%d: - did not finish\n",
		    INST(cfp));
#endif
	}

	if (count == ADPU320_PAC_RETRY_COUNT) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_attach_PAC%d: - could not PAC\n",
		    INST(cfp));
#endif
		return (ADPU320_FAILURE);
	}

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_attach_PAC%d: 0x%x - finished\n", INST(cfp),
	    cfp);
#endif

	return (ADPU320_SUCCESS);
}

/*
 * Function name: adpu320_validate
 * Description:
 *      This routine is for the purpose of execute protocol configuration
 *      (e.g. Fiber Channel loop initialization, SCSI bus reset), validate
 *      existing target devices, and to allocate TSCB and register TSH for
 *      newly-founded target devices.
 *
 * Return Value :       0 successful
 *			1 failed
 */
/* CSTYLED */
STATIC int
adpu320_validate(
adpu320_config_t *cfp
)
{
	HIM_UINT32		index;
	int			ret;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_validate%d:\n", INST(cfp));
#endif

	/*
	 * Disable interrupts for poll
	 */
	cfp->ha_himFuncPtr->HIMDisableIRQ(cfp->ha_adapterTSH);

	for (index = 0; index < MAX_EQ; index++) {
		ret = adpu320_validate_target(index, cfp);

		if (ret == ADPU320_DONE) {
			break;
		}	else if (ret == ADPU320_FAILURE) {
			return (ADPU320_FAILURE);
		}

		/* ADPU320_SUCCESS */
	}

	cfp->ha_himFuncPtr->HIMEnableIRQ(cfp->ha_adapterTSH);

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_validate%d: - finished\n", INST(cfp));
#endif

	return (ADPU320_SUCCESS);
}

void
adpu320_poll_request(
adpu320_config_t		*cfp
)
{
	unsigned char		status;

#ifdef ADPU320_DEBUG
	adpu320_verify_lock_held("adpu320_poll_request", &cfp->ab_mutex);
#endif

	if (cfp->ha_himFuncPtr->HIMPollIRQ(cfp->ha_adapterTSH)) {
		status = cfp->ha_himFuncPtr->HIMFrontEndISR(
		    cfp->ha_adapterTSH);

		switch (status) {
		case HIM_LONG_INTERRUPT_PENDING:
		case HIM_INTERRUPT_PENDING:
#ifdef ADPU320_DEBUG
			adpu320_printf("adpu320_poll_request: status = 0x%x\n",
			    status);
#endif
			cfp->ha_himFuncPtr->HIMBackEndISR(
			    cfp->ha_adapterTSH);
			break;

		case HIM_NOTHING_PENDING:
			break;

		default:
#ifdef ADPU320_DEBUG
			adpu320_printf("adpu320_poll_request: illegal "
			    "int_status=%d \n", status);
#endif
			break;
		}
	}
}

int
adpu320_validate_target(
HIM_UINT32		index,
adpu320_config_t	*cfp
)
{
	int			ret, adpu320UserOpt;
	HIM_UINT8		tValid;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_validate_target%d:\n", INST(cfp));
	}
#endif

	/*
	 * This routine is being called from a timeout.
	 */
	if (cfp->ha_targetTSH[index] == NULL) {
		ret = adpu320_create_target(cfp, index);

		return (ret);
	}

	/*
	 * Validate if the specified target TSH is still valid.
	 */
	tValid = cfp->ha_himFuncPtr->HIMValidateTargetTSH(
	    cfp->ha_targetTSH[index]);

	switch (tValid) {
	case HIM_TARGET_VALID:
		cfp->ha_himFuncPtr->HIMProfileTarget(
		    cfp->ha_targetTSH[index], cfp->ha_targetProfile[index]);

		adpu320UserOpt = ddi_prop_get_int(DDI_DEV_T_ANY,
		    cfp->ab_dip, DDI_PROP_DONTPASS,
		    "ADPU320_SCSI_RD_STRM", -1);

		if (adpu320UserOpt == 1)
			cfp->ha_targetProfile[index]->himu.
			    TS_SCSI.TP_SCSIDefaultProtocolOptionMask
			    |= HIM_SCSI_RD_STRM;
		else
			cfp->ha_targetProfile[index]->himu.
			    TS_SCSI.TP_SCSIDefaultProtocolOptionMask
			    &= ~HIM_SCSI_RD_STRM;

		cfp->ha_himFuncPtr->HIMAdjustTargetProfile(
		    cfp->ha_targetTSH[index],
		    cfp->ha_targetProfile[index]);

		break;

	case HIM_TARGET_CHANGED:
		/*
		 * profile information has changed , but
		 * device is still valid
		 */
		cfp->ha_himFuncPtr->HIMProfileTarget(
		    cfp->ha_targetTSH[index],
		    cfp->ha_targetProfile[index]);

		adpu320UserOpt = ddi_prop_get_int(DDI_DEV_T_ANY,
		    cfp->ab_dip, DDI_PROP_DONTPASS,
		    "ADPU320_SCSI_RD_STRM", -1);

		if (adpu320UserOpt == 1)
			cfp->ha_targetProfile[index]->himu.
			    TS_SCSI.TP_SCSIDefaultProtocolOptionMask
			    |= HIM_SCSI_RD_STRM;
		else
			cfp->ha_targetProfile[index]->himu.
			    TS_SCSI.TP_SCSIDefaultProtocolOptionMask
			    &= ~HIM_SCSI_RD_STRM;

		cfp->ha_himFuncPtr->HIMAdjustTargetProfile(
		    cfp->ha_targetTSH[index],
		    cfp->ha_targetProfile[index]);

		break;

	case HIM_TARGET_INVALID:
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			adpu320_printf("adpu320_validate_target: index %d "
			    "invalid\n", index);
		}
#endif

		break;

	default:
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			adpu320_printf("adpu320_validate_target: index %d "
			    "returned %d\n", index, tValid);
		}
#endif

		break;
	}

	return (ADPU320_SUCCESS);
}

int
adpu320_create_target(
adpu320_config_t	*cfp,
HIM_UINT32		index
)
{
	int			scsiId, adpu320UserOpt;
	int			Lun;
	int			loca;
	HIM_TARGET_PROFILE	*targetProfile;
	HIM_UINT32		adpu320_target_tscb_size;
	HIM_UINT8		target_tscb_needed;
	void			*targetTSCB;
	void			*targetTSH;
	int			sleepflag;

	sleepflag = KM_SLEEP;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_create_target%d:\n", INST(cfp));
#endif

	target_tscb_needed = cfp->ha_himFuncPtr->HIMCheckTargetTSCBNeeded(
	    cfp->ha_adapterTSH, index);

	if (target_tscb_needed == HIM_NO_NEW_DEVICES) {
		return (ADPU320_DONE);
	}

	adpu320_target_tscb_size = cfp->ha_himFuncPtr->HIMSizeTargetTSCB(
	    cfp->ha_adapterTSH);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_create_target%d: target = %x need "
		    "target_tscb_size %x\n", INST(cfp),
		    index, adpu320_target_tscb_size);
	}
#endif

	targetTSCB = (void *)kmem_zalloc(adpu320_target_tscb_size, sleepflag);

	targetTSH = cfp->ha_himFuncPtr->HIMCreateTargetTSCB(
	    cfp->ha_adapterTSH, index, targetTSCB);

	if (targetTSH == NULL) {
		/*
		 * targetTSCB rejected by CHIM
		 */
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_create_target%d: "
		    "HIMCreateTargetTSCB failed\n", INST(cfp));
#endif

		cmn_err(CE_WARN, "Target TSCB Create failed");

		return (ADPU320_FAILURE);
	}

	targetProfile = (HIM_TARGET_PROFILE *)kmem_zalloc(
	    sizeof (HIM_TARGET_PROFILE), KM_SLEEP);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_create_target%d: HIMProfileTarget\n",
		    INST(cfp));
	}
#endif

	if (cfp->ha_himFuncPtr->HIMProfileTarget(targetTSH, targetProfile)
	    == HIM_FAILURE) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_create_target: "
		    "HIMProfileTarget failed\n");
#endif
		return (ADPU320_FAILURE);
	}

	scsiId = (int)targetProfile->himu.TS_SCSI.TP_SCSI_ID;

	Lun = (int)targetProfile->himu.TS_SCSI.TP_SCSILun;

	if ((Lun >= MAX_LUNS) || (scsiId >= MAX_TARGETS)) {
		kmem_free(targetTSCB, adpu320_target_tscb_size);

		kmem_free(targetProfile, sizeof (HIM_TARGET_PROFILE));

		return (ADPU320_FAILURE);
	}

	loca = (scsiId) + (Lun * MAX_TARGETS);

	targetProfile->TP_MaxActiveCommands = adpu320_max_active_cmds;

	cfp->ha_targetTSH[loca] = targetTSH;

	adpu320UserOpt = ddi_prop_get_int(DDI_DEV_T_ANY,
	    cfp->ab_dip, DDI_PROP_DONTPASS,
	    "ADPU320_SCSI_RD_STRM", -1);
	if (adpu320UserOpt == 1)
		targetProfile->himu.TS_SCSI.TP_SCSIDefaultProtocolOptionMask |=
		    HIM_SCSI_RD_STRM;
	else
		targetProfile->himu.TS_SCSI.TP_SCSIDefaultProtocolOptionMask &=
		    ~HIM_SCSI_RD_STRM;

	cfp->ha_targetProfile[loca] = targetProfile;

	cfp->ha_himFuncPtr->HIMAdjustTargetProfile(
	    targetTSH, targetProfile);

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_create_target%d: -- done\n", INST(cfp));
#endif

	return (ADPU320_SUCCESS);
}


void
adpu320_FillOutOSMPointers(
HIM_OSM_FUNC_PTRS *osmRoutines
)
{
	osmRoutines->versionNumber = 1;
	osmRoutines->OSMMapIOHandle = OSMMapIOHandle;
	osmRoutines->OSMReleaseIOHandle = OSMReleaseIOHandle;
	osmRoutines->OSMEvent = OSMEvent;
	osmRoutines->OSMGetBusAddress = OSMGetBusAddress;
	osmRoutines->OSMAdjustBusAddress = OSMAdjustBusAddress;
	osmRoutines->OSMGetNVSize = OSMGetNVSize;
	osmRoutines->OSMPutNVData = OSMPutNVData;
	osmRoutines->OSMGetNVData = OSMGetNVData;
	osmRoutines->OSMReadUExact8 = OSMReadUExact8;
	osmRoutines->OSMReadUExact16 = OSMReadUExact16;
	osmRoutines->OSMReadUExact32 = OSMReadUExact32;
	osmRoutines->OSMReadStringUExact8 = OSMReadStringUExact8;
	osmRoutines->OSMReadStringUExact16 = OSMReadStringUExact16;
	osmRoutines->OSMReadStringUExact32 = OSMReadStringUExact32;
	osmRoutines->OSMWriteUExact8 = OSMWriteUExact8;
	osmRoutines->OSMWriteUExact16 = OSMWriteUExact16;
	osmRoutines->OSMWriteUExact32 = OSMWriteUExact32;
	osmRoutines->OSMWriteStringUExact8 = OSMWriteStringUExact8;
	osmRoutines->OSMWriteStringUExact16 = OSMWriteStringUExact16;
	osmRoutines->OSMWriteStringUExact32 = OSMWriteStringUExact32;
	osmRoutines->OSMSynchronizeRange = OSMSynchronizeRange;
	osmRoutines->OSMWatchdog = OSMWatchdog;
	osmRoutines->OSMSaveInterruptState = OSMSaveInterruptState;
	osmRoutines->OSMSetInterruptState = OSMSetInterruptState;

	osmRoutines->OSMReadPCIConfigurationDword =
	    OSMReadPCIConfigurationDword;

	osmRoutines->OSMReadPCIConfigurationWord =
	    OSMReadPCIConfigurationWord;

	osmRoutines->OSMReadPCIConfigurationByte =
	    OSMReadPCIConfigurationByte;

	osmRoutines->OSMWritePCIConfigurationDword =
	    OSMWritePCIConfigurationDword;

	osmRoutines->OSMWritePCIConfigurationWord =
	    OSMWritePCIConfigurationWord;

	osmRoutines->OSMWritePCIConfigurationByte =
	    OSMWritePCIConfigurationByte;

	osmRoutines->OSMDelay = OSMDelay;
}

/*
 *		      OSM Procedural Interfaces
 */

/*
 * Function name: IOB->postRoutine
 * Description:
 *      This routine posts a completed IOB to the OSM.
 * Return Value :       Zero
 */
void
OSMIobCompleted(
HIM_IOB	*IOBp
)
{
	adpu320_config_t	*cfp;
	sp_t			*scbp;
	struct scsi_pkt		*pkt;
	struct adpu320_scsi_cmd	*cmd;
#ifdef ADPU320_DEBUG
	int			loca;
#endif

	cmd = (struct adpu320_scsi_cmd *)IOBp->osRequestBlock;

	scbp = (sp_t *)cmd->cmd_private;

	cfp = scbp->SP_ConfigPtr;

#ifdef ADPU320_DEBUG
	/*
	 * This is a callback from the HIM, and we lock this mutex whenever
	 * we call into the him.
	 */
	adpu320_verify_lock_held("OSMIobCompleted", &cfp->ab_mutex);
#endif

	cmd->cmd_cflags |= CFLAG_FINISHED;

	if (adpu320_delete_element(cfp->Ptr_List, (uchar_t *)IOBp->errorData)) {
#ifdef ADPU320_DEBUG
		debug_enter("Delete element failed\n");
#endif
	}

#ifdef ADPU320_DEBUG

	loca = (cmd->target) + (cmd->lun * MAX_TARGETS);

	cfp->ha_stats[loca].iobs_sent--;
#endif

	pkt = cmd->cmd_pkt;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		cfp->ab_pkts_out--;

		adpu320_printf("OSMIobCompleted%d: pktp %x scbp %x blk %x "
		    "cmd %x\n", INST(cfp), pkt, scbp, cfp, cmd);
	}
#endif

	if (pkt == (struct scsi_pkt *)0) {
		cmn_err(CE_WARN, "OSMIobCompleted: Cannot retrieve pktp");

		return;
	}

	/*
	 * adpu320_pollret will handle all the details if FLAG_NOINTR is set.
	 */
	if ((pkt->pkt_flags & FLAG_NOINTR) == 0) {
		/*
		 * Queue this to be completed later.
		 */
		adpu320_add_queue(&cfp->ab_scb_completed_queue,
		    (adpu320_entry_t *)scbp);
	}
}

/*
 * Function name: OSMMapIOHandle
 * Description:
 *      This routine requests the OSM to provide handles to I/O ranges
 *      needed to access adapter registers and on-board memory
 * Return Value :
 *      0 - Handle is successfully returned.
 *      1 - Unable to map this I/O range.
 */
HIM_UINT8
OSMMapIOHandle(
void HIM_PTR		osmContext,
HIM_UINT8		rangeIndex,
HIM_UINT32		offset,
HIM_UINT32		length,
HIM_UINT32		pacing,
HIM_UINT16		attributes,
HIM_IO_HANDLE HIM_PTR	handle
)
{
	ddi_device_acc_attr_t 	dev_attr;
	adpu320_config_t    	*cfp;
	adpu320_regs_t		*regs;
	uint_t 			rnumber;

	cfp = (adpu320_config_t *)osmContext;

	if (cfp == NULL) {
		cmn_err(CE_WARN, " OSM Map IO cannot recognize osmContext");

		return (1);
	}

	regs = kmem_zalloc(sizeof (adpu320_config_t), KM_SLEEP);

	dev_attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;

	/*
	 * The first thing that we need to do is set the attribute flags
	 * for mapping.
	 */
	if (attributes & HIM_IO_BIG_ENDIAN) {
		dev_attr.devacc_attr_endian_flags = DDI_STRUCTURE_BE_ACC;
	}	else if (attributes & HIM_IO_LITTLE_ENDIAN) {

		dev_attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	}	else {

		dev_attr.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC;
	}

	if (attributes & HIM_IO_STRICTORDER) {
		dev_attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	}	else if (attributes & HIM_IO_MERGING_OK) {

		dev_attr.devacc_attr_dataorder = DDI_MERGING_OK_ACC;
	}	else if (attributes & HIM_IO_LOAD_CACHING_OK) {

		dev_attr.devacc_attr_dataorder = DDI_LOADCACHING_OK_ACC;
	}	else if (attributes & HIM_IO_STORE_CACHING_OK) {

		dev_attr.devacc_attr_dataorder = DDI_STORECACHING_OK_ACC;
	}	else {

		dev_attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	}

	/*
	 * Enable I/O access to the device
	 * The index into the reg property and thus the adds space to
	 * map is based on the CHIM rangeIndex (PCI BAR number)
	 */

	if (!adpu320_get_reg_prop_index(cfp, rangeIndex, &rnumber)) {
#ifdef ADPU320_DEBUG
		adpu320_printf("OSMMapIOHandle: no memory");
#endif
		return (1);
	}

#ifdef ADPU320_MAPCHECK
	{
		caddr_t			config_space;
		ddi_device_acc_attr_t 	check_dev_attr;
		ddi_acc_handle_t	check_handle;
		int			i;

		check_dev_attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
		check_dev_attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
		check_dev_attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;

		if (ddi_regs_map_setup(cfp->ab_dip, 0, &config_space,
		    0, 0x40, &check_dev_attr, &check_handle) != DDI_SUCCESS) {
			adpu320_printf("OSMMapIOHandle - ddi_regs_map_setup "
			    "failed\n");
		}
		else
		{
			adpu320_printf("Config space ADDRS - ");
			for (i = 0; i < 6; i++) {
				adpu320_printf("0x%x ", ddi_get32(check_handle,
				    (uint32_t *)
				    ((uint32_t)
				    (config_space + 0x10 + (i * 4))
				    & 0xfffffffc)));
			}
			adpu320_printf("\n");
		}
	}
#endif

	if (ddi_regs_map_setup(cfp->ab_dip, rnumber, &regs->baseAddress,
	    (offset_t)offset, (offset_t)length, &dev_attr,
	    &regs->regs_handle) != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("OSMMapIOHandle - ddi_regs_map_setup "
		    "failed\n");
#endif
		return (1);
	}

	handle->cfp = cfp;

	handle->baseAddress = (HIM_ULONG)regs;

	handle->attributes = attributes;

#ifdef ADPU320_DEBUG
	adpu320_printf("OSMMapIOHandle%d: BaseAddress = 0x%x\n",
	    INST(cfp), regs->baseAddress);
#endif

	return (0);
}

/*
 * This function will return the index into the "reg" property
 * that maps the PCI BAR number passed in.
 * Return Value :
 *      1 - success
 *      0 - fail
 *
 */
int
adpu320_get_reg_prop_index(
adpu320_config_t    	*cfp,
HIM_UINT8		bar_num,
uint_t			*rnumber
)
{
	int 		nregs;
	int		i;
	int		reglen;
	HIM_UINT8	pci_reg_num;
	pci_regspec_t	*regp;
	pci_regspec_t	*p;

	if (bar_num > PCI_BASE_NUM) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_get_reg_prop_index%d: - "
		    "Invalid BAR %d\n", INST(cfp), bar_num);
#endif
		return (0);
	}

	pci_reg_num = (bar_num * PCI_BASE_SIZE) + PCI_CONF_BASE0;

	if (ddi_dev_nregs(cfp->ab_dip, &nregs)) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_get_reg_prop_index%d: "
		    "ddi_dev_nregs failed\n",
		    INST(cfp));
#endif
		return (0);
	}

	if (ddi_getlongprop(DDI_DEV_T_ANY, cfp->ab_dip, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&regp, &reglen) != DDI_PROP_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_get_reg_prop_index%d: "
		    "ddi_getlongprop failed\n", INST(cfp));
#endif
		return (0);
	}

	p = regp;

	for (i = 0; i < nregs; i++) {
		if ((HIM_UINT8)
		    (PCI_REG_REG_G(p->pci_phys_hi)) == pci_reg_num) {
			break;
		}

		p++;
	}

	*rnumber = i;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_get_reg_prop_index: Map BAR %d to "
	    "reg index %d\n", bar_num, i);
#endif

	kmem_free(regp, reglen);

	return (1);
}


/*
 * Function name: OSMReleaseIOHandle
 * Description:
 *      This routine allows OSM to release memory and other resources
 *      associated with an obsolete HIM_IO_HANDLER
 * Return Value :
 *      A zero return code indicates Success.
 *      A non-zero return code will be added in the future.
 */
HIM_UINT8
OSMReleaseIOHandle(
void HIM_PTR	osmContext,
HIM_IO_HANDLE	handle
)
{
	adpu320_config_t	*cfp;
	adpu320_regs_t		*regs;

	cfp = (adpu320_config_t *)osmContext;

#ifdef ADPU320_DEBUG
	adpu320_printf("OSMReleaseIOHandle%d:\n", INST(cfp));
#endif

	regs = (adpu320_regs_t *)handle.baseAddress;

	/*
	 * unmap the device from I/O space
	 */
	ddi_regs_map_free(&regs->regs_handle);

	kmem_free(regs, sizeof (adpu320_regs_t));

	return (0);
}

/*
 * Function name: OSMEvent
 * Description:
 * This routine provides asynchronous event notification from the CHIM.
 * Return Value : None
 */
void
OSMEvent(
void		*osmHaContext,
HIM_UINT16	event,
void		*pEventContext,
...
)
{
	adpu320_config_t		*cfp;
	struct adpu320_scsi_cmd		*cmd;

	cfp = (adpu320_config_t *)osmHaContext;

#ifdef ADPU320_DEBUG2
	adpu320_printf("OSMEvent%d:\n", INST(cfp));

	adpu320_verify_lock_held("OSMEvent", &cfp->ab_mutex);
#endif

	switch (event) {
	case HIM_EVENT_OSMFREEZE:
#ifdef ADPU320_DEBUG2
		adpu320_printf("HIM_EVENT_OSMFREEZE!\n");

		if (cfp->ab_flag & ADPU320_QUEUE_FROZEN) {
			debug_enter("already frozen");
		}
#endif
		/*
		 * Freeze the queue's.  At this point we cannot send
		 * any IOB other than the required HIM_PROTOCOL_AUTO_CONFIG
		 * and perhaps HIM_RESET_HARDWARE until a HIM_EVENT_UNFREEZE
		 * event is presented. 6.6 #1
		 */
		cfp->ab_flag |= ADPU320_QUEUE_FROZEN;

		break;


	case HIM_EVENT_IO_CHANNEL_RESET:
		/*
		 * A 3rd party reset the bus.
		 */
#ifdef ADPU320_DEBUG2
		adpu320_printf("HIM_EVENT_IO_CHANNEL_RESET\n");

		if ((cfp->ab_flag & ADPU320_QUEUE_FROZEN) == 0) {
			debug_enter("IO_CHANNEL_RESET with no FREEZE");
		}
#endif
		cmn_err(CE_NOTE, "adpu320: 3rd party SCSI bus reset");

		adpu320_IobSpecial(cfp, NULL, HIM_PROTOCOL_AUTO_CONFIG, 0, 0);

		adpu320_do_reset_notify_callbacks(cfp);

		break;

	case HIM_EVENT_PCIX_SPLIT_ERROR:
	case HIM_EVENT_PCI_ERROR:
		cmd = adpu320_IobSpecial(cfp, NULL, HIM_RESET_HARDWARE, 0,
		    CFLAG_POLL);

		adpu320_wait_special_iob_complete(cmd);
		/* FALLTHROUGH */

	case HIM_EVENT_AUTO_CONFIG_REQUIRED:
#ifdef ADPU320_DEBUG2
		adpu320_printf("HIM_EVENT_AUTO_CONFIG_REQUIRED\n");

		if ((cfp->ab_flag & ADPU320_QUEUE_FROZEN) == 0) {
			debug_enter("AUTO_CONFIG_REQUIRED with no FREEZE");
		}
#endif
		adpu320_IobSpecial(cfp, NULL, HIM_PROTOCOL_AUTO_CONFIG, 0, 0);

		break;

	case HIM_EVENT_HA_FAILED:
		/*
		 * Adapter Hardware Failed
		 */
		cmn_err(CE_WARN, "HIM_EVENT_HA_FAILED");

#ifdef ADPU320_DEBUG2
		if ((cfp->ab_flag & ADPU320_QUEUE_FROZEN) == 0) {
			debug_enter("HA_FAILED with no FREEZE");
		}
#endif

		cmd = adpu320_IobSpecial(cfp, NULL, HIM_RESET_HARDWARE, 0,
		    CFLAG_POLL);

		(void) adpu320_wait_special_iob_complete(cmd);

		/*
		 * We do not have to worry about completing any commands
		 * at this point, because this routine is called at interrupt
		 * time.  We do not have to call adpu320_drain_completed_queue.
		 * When the BackEndISR has completed, the interrupt handler
		 * will drain the completed queue.
		 */
		adpu320_IobSpecial(cfp, NULL, HIM_PROTOCOL_AUTO_CONFIG, 0, 0);

		break;

	case HIM_EVENT_TRANSPORT_MODE_CHANGE:
#ifdef ADPU320_DEBUG2
		adpu320_printf("HIM_EVENT_TRANSPORT_MODE_CHANGE!\n");

		if ((cfp->ab_flag & ADPU320_QUEUE_FROZEN) == 0) {
			debug_enter("TRANSPORT_MODE_CHANGE with no FREEZE");
		}
#endif
		adpu320_IobSpecial(cfp, NULL, HIM_PROTOCOL_AUTO_CONFIG, 0, 0);

		break;

	case HIM_EVENT_OSMUNFREEZE:
#ifdef ADPU320_DEBUG2
		adpu320_printf("HIM_EVENT_OSMUNFREEZE!\n");
#endif
		ddi_trigger_softintr(cfp->ab_softid);

		cfp->ab_flag &= ~ADPU320_QUEUE_FROZEN;

		cv_signal(&cfp->ab_quiesce_cv);

		break;

	case HIM_EVENT_IO_CHANNEL_FAILED:
		/*
		 * Adapter Hardware Failed
		 */
		cmn_err(CE_WARN, "adpu320: HIM_EVENT_IO_CHANNEL_FAILED!\n");

		break;

	default:
		/*
		 * Unknown Asynchronous Event
		 */
		cmn_err(CE_WARN, " Unknown Asynchronous Event 0x%02x",
		    (uint_t)event);

		break;
	}

}

/*
 *		      TOOL KIT Procedure interfaces
 */

/*
 * Function name: OSMGetBusAddress
 * Description:
 * This routine returns a bus physcal address that is translated from
 *      a supplied virtual address.
 * Return Value : bus address
 */
HIM_BUS_ADDRESS
OSMGetBusAddress(
HIM_TASK_SET_HANDLE	osmContext,
HIM_UINT8		category,
void HIM_PTR		virtualAddress
)
{
	adpu320_config_t	*cfp;
	HIM_BUS_ADDRESS		b_addr;
	adpu320_dma_info_t	*dma_infop;

	cfp = (adpu320_config_t *)osmContext;

	b_addr = (HIM_BUS_ADDRESS)adpu320_search_element(cfp->Ptr_List,
	    (uchar_t *)virtualAddress);

	if (b_addr != NULL) {
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			adpu320_printf("OSMGetBusAddress: "
			    "virtualAddress = 0x%x Bus Address 0x%x\n",
			    virtualAddress, b_addr);
		}
#endif
		return (b_addr);
	}

#if 0
	adpu320_printf("OSMGetBusAddress: cfp = 0x%x &cfp->ab_dmainfo = 0x%x "
	    "virtualAddress = 0x%x\n", cfp, &cfp->ab_dmainfo,
	    virtualAddress);
#endif

	for (dma_infop = cfp->ab_dmainfo; dma_infop != NULL;
	    dma_infop = dma_infop->next) {
#if 0
		adpu320_printf("  OSMGetBusAddress: dma_infop = 0x%x "
		    "testing = 0x%x type = 0x%x\n",
		    dma_infop, dma_infop->virt_addr, dma_infop->type);

		switch (dma_infop->type) {
			case ADPU320_CONTIG_MEM:
			case ADPU320_NONCONTIG_MEM:
				break;
			default:
				debug_enter("OSMGetBusAddress: illegal type\n");
				break;
		}
#endif

		if (dma_infop->type != ADPU320_CONTIG_MEM) {
			/*
			 * We only have physical addresses for contiguous
			 * memory.
			 */

			continue;
		}

		if ((virtualAddress >= (void *)dma_infop->virt_addr) &&
		    ((uchar_t *)virtualAddress <
		    ((uchar_t *)dma_infop->virt_addr +
		    dma_infop->size))) {
			break;
		}
	}

	if (dma_infop == NULL) {
#ifdef ADPU320_DEBUG
		adpu320_printf("OSMGetBusAddress: didn't find address\n");

		debug_enter("OSMGetBusAddress: didn't find address\n");
#endif

		return (NULL);
	}

#if 0
	adpu320_printf(" OSMGetBusAddress: found dma_infop 0x%x", dma_infop);
#endif


	b_addr = dma_infop->phys_addr +
	    (HIM_BUS_ADDRESS)((uintptr_t)virtualAddress -
	    (uintptr_t)dma_infop->virt_addr);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
#if OSM_BUS_ADDRESS_SIZE == 64
		adpu320_printf("virtualAddress = 0x%x Bus Address 0x%llx "
		    "phys_addr = 0x%llx\n",
		    virtualAddress, b_addr, dma_infop->phys_addr);
#else
		adpu320_printf("virtualAddress = 0x%x Bus Address 0x%x "
		    "phys_addr = 0x%x\n",
		    virtualAddress, b_addr, dma_infop->phys_addr);
#endif
	}
#endif
	return (b_addr);
}

/*
 * Function name: OSMAdjustBusAddress
 * Description:
 *      This routine adds or subtracts a value to a HIM_BUS_ADDRESS.
 * Return Value : None
 */
void
OSMAdjustBusAddress(
HIM_BUS_ADDRESS HIM_PTR		busAddress,
int				value
)
{
	*busAddress = ((*busAddress) + (value));
}

/*
 * Function name: OSMGetNVSize
 * Description:
 *      This routine will return the amount of non-volatile memory associated
 *      with an adapter TSCB.
 *      The OSM defines this routine if and only if non-volatile memory is
 *      implemented on a motherboard, and the method of access is system-
 *      specific.
 * Return Value :
 *      Amount of nonvolatile memory (NVRAM), in bytes, associated with the
 *      adapter. If no NVRAM exists, zero is returned.
 */
HIM_UINT32
OSMGetNVSize(
HIM_TASK_SET_HANDLE	osmContext
)
{
	return (0);
}

/*
 * Function name: OSMPutNVData
 * Description:
 *      This function will store data in NVRAM. The OSM defines this routine
 *      if and only if non-volatile memory is implemented on a motherboard,
 *      and the method of access is system-specific.
 * Return Value :
 *      0:      Storage succeeded
 *      1:      Write failed
 *      2:      Write to NVRAM not supported. On some systems, NVRAM may be
 *	      manipulated by a configuration utility, then copied to main
 *	      memory. In this case, writes to NVRAM are not supported.
 */
HIM_UINT8
OSMPutNVData(
HIM_TASK_SET_HANDLE osmContext,
HIM_UINT32	destinationOffset,
void HIM_PTR	source,
HIM_UINT32	length
)
{
	return (HIM_WRITE_NOT_SUPPORTED);
}

/*
 * Function name: OSMGetNVData
 * Description:
 *      This function will retrieve data from NVRAM. The OSM defines this
 *      routine if and only if non-volatile memory is implemented on a
 *      motherboard and the method of access is system-specific.
 * Return Value :
 *      0:      retrieval succeeded
 *      1:      retrieval failed, destination buffer contents unknown.
 */
HIM_UINT8
OSMGetNVData(
HIM_TASK_SET_HANDLE	osmContext,
void			*destination,
HIM_UINT32		sourceOffset,
HIM_UINT32		length
)
{
	return (1);
}


/*
 * Function name: OSMReadUEXACT8, 16, 32
 * Description:
 *      This routine reads from an I/O range which the OSM has
 *	proviously mapped for the HIM.
 * Return Value :
 *      Data value at requested base and offset.
 */
HIM_UEXACT8
OSMReadUExact8(
HIM_IO_HANDLE	ioBase,
HIM_UINT32	ioOffset
)
{
	adpu320_regs_t	*regs;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	return (ddi_get8(regs->regs_handle,
	    (uint8_t *)(regs->baseAddress + ioOffset)));

}

HIM_UEXACT16
OSMReadUExact16(
HIM_IO_HANDLE	ioBase,
HIM_UINT32	ioOffset
)
{
	HIM_UEXACT16	word;
	adpu320_regs_t	*regs;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	word = ddi_get16(regs->regs_handle,
	    (uint16_t *)(regs->baseAddress + ioOffset));

	if (OSM_CPU_LITTLE_ENDIAN) {
		return (word);
	}
	else
	{
		return (((word & 0x00ff) << 8) | ((word & 0xff00) >> 8));
	}

}

HIM_UEXACT32
OSMReadUExact32(
HIM_IO_HANDLE	ioBase,
HIM_UINT32	ioOffset
)
{
	HIM_UEXACT32	dword;
	adpu320_regs_t	*regs;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	dword = ddi_get32(regs->regs_handle,
	    (uint32_t  *)(regs->baseAddress + ioOffset));

	if (OSM_CPU_LITTLE_ENDIAN) {
		return (dword);
	}
	else
	{
		return (((dword & 0xff) << 24) | ((dword & 0xff00) << 8) |
		    ((dword & 0xff0000) >> 8) |
		    ((dword & 0xff000000) >> 24));
	}
}


/*
 * Function name: OSMReadStringUEXACT8, 16, 32
 * Description:
 *      This routine reads from an I/O range which the OSM has proviously
 *      mapped for the HIM.
 * Return Value :
 *      Multiple data of the requested size into a HIM-specified buffer.
 */
void
OSMReadStringUExact8(
HIM_IO_HANDLE		ioBase,
HIM_UINT32		ioOffset,
HIM_UEXACT8 HIM_PTR	destBuffer,
HIM_UINT32		count,
HIM_UINT8		stride
)
{
	HIM_UEXACT8	byte;
	HIM_UINT32	i;
	HIM_UINT32	strideIndex;
	adpu320_regs_t	*regs;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	strideIndex = 0;

	for (i = 0; i < count; i++) {
		byte = ddi_get8(regs->regs_handle, (uint8_t *)
		    (regs->baseAddress + ioOffset + strideIndex));

		HIM_PTR destBuffer = byte;
		HIM_PTR destBuffer = byte;
		destBuffer++;
		strideIndex += stride;
	}
}

void
OSMReadStringUExact16(
HIM_IO_HANDLE		ioBase,
HIM_UINT32		ioOffset,
HIM_UEXACT16 HIM_PTR	destBuffer,
HIM_UINT32		count,
HIM_UINT8		stride
)
{
	HIM_UEXACT16	word;
	HIM_UINT32	i;
	HIM_UINT32	strideIndex;
	adpu320_regs_t	*regs;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	strideIndex = 0;

	for (i = 0; i < count; i++) {
		word = ddi_get16(regs->regs_handle, (uint16_t *)
		    (regs->baseAddress + ioOffset + strideIndex));

		if (OSM_CPU_LITTLE_ENDIAN) {
			HIM_PTR destBuffer = word;
		}
		else
		{
			HIM_PTR destBuffer =
			    (((word & 0xff) << 8) | ((word & 0xff00) >> 8));
		}

		destBuffer++;

		strideIndex += stride;
	}
}

void
OSMReadStringUExact32(
HIM_IO_HANDLE		ioBase,
HIM_UINT32		ioOffset,
HIM_UEXACT32 HIM_PTR	destBuffer,
HIM_UINT32		count,
HIM_UINT8		stride
)
{
	HIM_UEXACT32	dword;
	HIM_UINT32	i;
	HIM_UINT32	strideIndex;
	adpu320_regs_t	*regs;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	strideIndex = 0;

	for (i = 0; i < count; i++) {
		dword = ddi_get32(regs->regs_handle, (uint32_t *)
		    (regs->baseAddress + ioOffset + strideIndex));

		if (OSM_CPU_LITTLE_ENDIAN) {
			HIM_PTR destBuffer = dword;
		}
		else
		{
			HIM_PTR destBuffer =
			    (((dword & 0xff) << 24) |
			    ((dword & 0xff00) << 8) |
			    ((dword & 0xff0000) >> 8) |
			    ((dword & 0xff000000) >> 24));
		}

		destBuffer++;

		strideIndex += stride;
	}
}


/*
 * Function name: OSMWriteUEXACT8, 16, 32
 * Description:
 *      This routine writes to an I/O range which the OSM has proviously
 *      mapped for the HIM.
 * Return Value : None
 */
void
OSMWriteUExact8(
HIM_IO_HANDLE	ioBase,
HIM_UINT32	ioOffset,
HIM_UEXACT8	value
)
{
	adpu320_regs_t	*regs;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	ddi_put8(regs->regs_handle,
	    (uint8_t *)(regs->baseAddress + ioOffset), value);
}

void
OSMWriteUExact16(
HIM_IO_HANDLE	ioBase,
HIM_UINT32	ioOffset,
HIM_UEXACT16	value
)
{
	HIM_UINT16	word;
	adpu320_regs_t	*regs;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	if (OSM_CPU_LITTLE_ENDIAN) {
		word = value;
	}
	else
	{
		word = (((value & 0xff) << 8) | ((value & 0xff00) >> 8));
	}

	ddi_put16(regs->regs_handle,
	    (uint16_t *)(regs->baseAddress + ioOffset), word);
}

void
OSMWriteUExact32(
HIM_IO_HANDLE	ioBase,
HIM_UINT32	ioOffset,
HIM_UEXACT32	value
)
{
	HIM_UINT32	dword;
	adpu320_regs_t	*regs;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	if (OSM_CPU_LITTLE_ENDIAN) {
		dword = value;
	}
	else
	{
		dword =
		    (((value & 0xff) << 24) |
		    ((value & 0xff00) << 8) |
		    ((value & 0xff0000) >> 8) |
		    ((value & 0xff000000) >> 24));
	}

	ddi_put32(regs->regs_handle,
	    (uint32_t *)(regs->baseAddress + ioOffset), dword);

}

/*
 * Function name: OSMWriteStringUEXACT8, 16, 32
 * Description:
 *      This routine writes to an I/O range which the OSM has proviously
 *      mapped for the HIM.
 * Return Value :
 *      Multiple data of the requested size into a HIM-specified buffer.
 */
void
OSMWriteStringUExact8(
HIM_IO_HANDLE		ioBase,
HIM_UINT32		ioOffset,
HIM_UEXACT8 HIM_PTR	sourceBuffer,
HIM_UINT32		count,
HIM_UINT8		stride
)
{
	HIM_UEXACT8	byte;
	HIM_UINT32	i;
	HIM_UINT32	strideIndex;
	adpu320_regs_t	*regs;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	strideIndex = 0;

	for (i = 0; i < count; i++) {
		byte = HIM_PTR sourceBuffer;

		ddi_put8(regs->regs_handle, (uint8_t *)
		    (regs->baseAddress + ioOffset + strideIndex), byte);

		sourceBuffer++;

		strideIndex += stride;
	}
}

void
OSMWriteStringUExact16(
HIM_IO_HANDLE		ioBase,
HIM_UINT32		ioOffset,
HIM_UEXACT16 HIM_PTR	sourceBuffer,
HIM_UINT32		count,
HIM_UINT8		stride
)
{
	HIM_UEXACT16	value;
	HIM_UEXACT16	word;
	HIM_UINT32	i;
	HIM_UINT32	strideIndex;
	adpu320_regs_t	*regs;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	strideIndex = 0;

	for (i = 0; i < count; i++) {
		value = HIM_PTR sourceBuffer;

		if (OSM_CPU_LITTLE_ENDIAN) {
			word = value;
		}
		else
		{
			word = (((value & 0xff) << 8) |
			    ((value & 0xff00) >> 8));
		}

		ddi_put16(regs->regs_handle, (uint16_t *)
		    (regs->baseAddress + ioOffset + strideIndex), word);

		sourceBuffer++;

		strideIndex += stride;
	}
}

void
OSMWriteStringUExact32(
HIM_IO_HANDLE		ioBase,
HIM_UINT32		ioOffset,
HIM_UEXACT32 HIM_PTR	sourceBuffer,
HIM_UINT32		count,
HIM_UINT8		stride
)
{
	HIM_UEXACT32	value;
	HIM_UEXACT32	dword;
	HIM_UINT32	i;
	HIM_UINT32	strideIndex;
	adpu320_regs_t	*regs;

	strideIndex = 0;

	regs = (adpu320_regs_t *)ioBase.baseAddress;

	for (i = 0; i < count; i++) {
		value = HIM_PTR sourceBuffer;

		if (OSM_CPU_LITTLE_ENDIAN) {
			dword = value;
		}
		else
		{
			dword = (((value & 0xff) << 24) |
			    ((value & 0xff00) << 8) |
			    ((value & 0xff0000) >> 8) |
			    ((value & 0xff000000) >> 24));
		}

		ddi_put32(regs->regs_handle, (uint32_t *)
		    (regs->baseAddress + ioOffset + strideIndex), dword);

		sourceBuffer++;

		strideIndex += stride;
	}
}

/*
 * Function name: OSMSynchronizeRange
 * Description:
 *      This call ensures that previous I/O range (OSMRead or OSMWrite)
 *      operations to the specified range have completed. On some platforms,
 *      this is merely a read-back of the data. Other platforms have specific
 *      instructions to force I/O accesses to complete.
 * Return Value : None
 */
void
OSMSynchronizeRange(
HIM_IO_HANDLE	ioBase,
HIM_UINT32	ioOffset,
HIM_UINT32	length
)
{

}


/*
 * Function name: OSMWatchdog
 * Description:
 *      This routine registers or de-registers a routine to be serviced with
 *      a watch dog timer.
 * Return Value : none
 */
void
OSMWatchdog(
HIM_TASK_SET_HANDLE	osmContext,
HIM_WATCHDOG_FUNC	watchdogProcedure,
HIM_UINT32		microSeconds
)
{
	;
}

/*
 * Function name: OSMSaveInterruptState
 * Description:
 *      This routine reports the system interrupt-enable state to the HIM
 * Return Value :
 *      0       :       Interrupt are disabled
 *      1       :       Interrupt are enabled
 */
HIM_UINT8
OSMSaveInterruptState()
{
	return (1);
}

/*
 * Function name: OSMSetInterruptState
 * Description:
 *      This routine disables all interrupts on the system.
 * Return Value : None
 */
void
OSMSetInterruptState(
HIM_UINT8	interruptState
)
{
	;
}


/*
 *		      For PCI Local Bus
 */

/*
 * Function name: OSMReadPCIConfiguration Byte, Word, Dword
 * DescriptION:
 *      These routines read from PCI configuration space.
 * Return Value :
 *      Contents of the register read from PCI configuration space.
 */
HIM_UEXACT8
OSMReadPCIConfigurationByte(
HIM_TASK_SET_HANDLE	osmContext,
HIM_UINT8		registerNumber
)
{
	adpu320_config_t	*cfp;
	ddi_acc_handle_t	cfg_handle;
	unsigned char		ret;

	cfp = (adpu320_config_t *)osmContext;

	ASSERT(cfp->ab_dip);

	if (pci_config_setup(cfp->ab_dip, &cfg_handle) != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("OSMRead : pci_config_setup failed\n");
#endif
		pci_config_teardown(&cfg_handle);

		return (0);
	}

	ret = pci_config_get8(cfg_handle, (off_t)registerNumber);

	pci_config_teardown(&cfg_handle);

	return (ret);
}

HIM_UEXACT16
OSMReadPCIConfigurationWord(
HIM_TASK_SET_HANDLE	osmContext,
HIM_UINT8		registerNumber
)
{
	adpu320_config_t	*cfp;
	ddi_acc_handle_t	cfg_handle;
	unsigned short		ret;

	cfp = (adpu320_config_t *)osmContext;

	ASSERT(cfp->ab_dip);

	if (pci_config_setup(cfp->ab_dip, &cfg_handle) != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("OSMRead : pci_config_setup failed\n");
#endif
		pci_config_teardown(&cfg_handle);

		return (0);
	}

	ret = pci_config_get16(cfg_handle, (off_t)registerNumber);

	pci_config_teardown(&cfg_handle);

	return (ret);
}

HIM_UEXACT32
OSMReadPCIConfigurationDword(
HIM_TASK_SET_HANDLE	osmContext,
HIM_UINT8		registerNumber
)
{
	adpu320_config_t	*cfp;
	ddi_acc_handle_t	cfg_handle;
	HIM_UEXACT32		ret;

	cfp = (adpu320_config_t *)osmContext;

	ASSERT(cfp->ab_dip);

	if (pci_config_setup(cfp->ab_dip, &cfg_handle) != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("OSMRead : pci_config_setup failed\n");
#endif
		pci_config_teardown(&cfg_handle);

		return (0);
	}

	ret = pci_config_get32(cfg_handle, (off_t)registerNumber);

	pci_config_teardown(&cfg_handle);

	return (ret);

}

/*
 * Function name: OSMWritePCIConfiguration Byte, Word, Dword
 * Description:
 *      These routines writes to PCI configuration space.
 * Return Value : None
 */
void
OSMWritePCIConfigurationByte(
HIM_TASK_SET_HANDLE	osmContext,
HIM_UINT8		registerNumber,
HIM_UEXACT8		registerValue
)
{
	adpu320_config_t	*cfp;
	ddi_acc_handle_t	cfg_handle;

	cfp = (adpu320_config_t *)osmContext;

	ASSERT(cfp->ab_dip);

	if (pci_config_setup(cfp->ab_dip, &cfg_handle) != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("OSMWrite : pci_config_setup failed\n");
#endif
		pci_config_teardown(&cfg_handle);
	}

	pci_config_put8(cfg_handle, (off_t)registerNumber, registerValue);

	pci_config_teardown(&cfg_handle);
}

void
OSMWritePCIConfigurationWord(
HIM_TASK_SET_HANDLE	osmContext,
HIM_UINT8		registerNumber,
HIM_UEXACT16		registerValue
)
{
	adpu320_config_t	*cfp;
	ddi_acc_handle_t	cfg_handle;

	cfp = (adpu320_config_t *)osmContext;

	ASSERT(cfp->ab_dip);

	if (pci_config_setup(cfp->ab_dip, &cfg_handle) != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("OSMWrite : pci_config_setup failed\n");
#endif
		pci_config_teardown(&cfg_handle);

	}

	pci_config_put16(cfg_handle, (off_t)registerNumber, registerValue);

	pci_config_teardown(&cfg_handle);
}

void
OSMWritePCIConfigurationDword(
HIM_TASK_SET_HANDLE	osmContext,
HIM_UINT8		registerNumber,
HIM_UEXACT32		registerValue
)
{
	adpu320_config_t	*cfp;
	ddi_acc_handle_t	cfg_handle;

	cfp = (adpu320_config_t *)osmContext;

	ASSERT(cfp->ab_dip);

	if (pci_config_setup(cfp->ab_dip, &cfg_handle) != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("OSMWrite : pci_config_setup failed\n");
#endif
		pci_config_teardown(&cfg_handle);

	}

	pci_config_put32(cfg_handle, (off_t)registerNumber, registerValue);

	pci_config_teardown(&cfg_handle);
}

void
OSMDelay(
void HIM_PTR osmAdapterContext,
HIM_UINT32	microSeconds
)
{
	drv_usecwait(microSeconds);
}


void
OSMmemcpy(
void	*to,
void	*from,
int	len
)
{
	bcopy(from, to, len);
}

int
OSMmemcmp(
void	*p1,
void	*p2,
int	len
)
{
	return (bcmp(p1, p2, len));
}

void
OSMmemset(
void	*buf,
int	value,
int	len
)
{
	if (value == 0)
		bzero(buf, len);
}


/*
 * Function : adpu320_pktalloc()
 *
 * OK
 */
/* CSTYLED */
STATIC int
adpu320_pktalloc(
struct scsi_pkt		*pkt,
int			(*callback)(),
caddr_t			arg
)
{
	struct scsi_address		*ap = &pkt->pkt_address;
	struct adpu320_scsi_cmd		*cmd;
	sp_t				*scbp;
	adpu320_config_t		*cfp;
	HIM_IOB HIM_PTR			IOBp;
	uint_t				loca;

	cfp = ADDR2CFP(ap);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("=========================================\n");
		adpu320_printf("adpu320_pktalloc%d:\n", INST(cfp));
	}
#endif

	loca = (ap->a_target) + (ap->a_lun * MAX_TARGETS);

	if (cfp->ha_targetTSH[loca] == NULL) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_pktalloc: Task handle not set <%d,%d>- "
		    "returning NULL\n", ap->a_target, ap->a_lun);
#endif

		return (-1);
	}

	/*
	 * addr struct adpu320_scsi_cmd is in pkt->pkt_ha_private
	 */
	cmd = PKT2CMD(pkt);
	cmd->cmd_pkt = pkt;
	scbp = (sp_t *)cmd->cmd_private;

	/*
	 * cmd_private is used to communicate the completed scb
	 * and to save and free scb memory
	 * in other drivers this area is known as the ccb
	 */

	/*
	 * Allocate a IOB associate with this scsi_cmd
	 */
	IOBp = adpu320_allocIOB(cfp, cmd);

	/*
	 * initialize scb
	 */

	scbp->next = NULL;
	scbp->Sp_cmdp = cmd;
	scbp->SP_ConfigPtr = cfp;
	bzero(&scbp->Sp_sense, sizeof (scbp->Sp_sense));

	/*
	 * initialize arq
	 */
	if ((ADDR2ADPU320UNITP(ap))->au_arq) {
		IOBp->flagsIob.autoSense = 1;
	}

	/*
	 * initialize target and lun
	 */
	cmd->target = ap->a_target;
	cmd->lun    = ap->a_lun;

	/*
	 * This cmd associate with a certain IOB
	 */

	pkt->pkt_cdbp		= (opaque_t)scbp->Sp_CDB;
	pkt->pkt_scbp		= (uchar_t *)&scbp->Sp_sense;

	IOBp->priority = 0;
	IOBp->flagsIob.disableDma = 0;
	IOBp->flagsIob.disableNotification = 0;
	IOBp->flagsIob.freezeOnError = 0;
	IOBp->graphNode = NULL;
	IOBp->sortTag = 0;
	IOBp->taskSetHandle	= cfp->ha_targetTSH[loca];
	IOBp->taskStatus	= 0;
	IOBp->postRoutine	= (HIM_POST_PTR)OSMIobCompleted;
	IOBp->targetCommand	= (void  *) &scbp->Sp_CDB[0];
	IOBp->errorData	= (void *) ((uchar_t *)&scbp->Sp_sense.sts_sensedata);
	IOBp->errorDataLength	= ADPU320_SENSE_LEN;
	IOBp->residual		= 0;
	IOBp->residualError	= 0;
	IOBp->taskAttribute	= HIM_TASK_SIMPLE;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_pktalloc%d: pkt->pkt_resid = 0x%x\n",
		    INST(cfp), pkt->pkt_resid);
	}

	if (adpu320_debug & DPKT) {
		adpu320_printf("adpu320_pktalloc%d: cmd %x scbp %x pktp %x\n",
		    INST(cfp), cmd, scbp, pkt);
	}
#endif
	return (0);
}

/*
 * Function : adpu320_pktfree()
 *
 * Description :
 *	Packet free
 *
 * OK
 */
/* CSTYLED */
STATIC void
adpu320_pktfree(
struct scsi_address	*ap,
struct scsi_pkt		*pkt
)
{
	register struct adpu320_scsi_cmd	*cmd;
	register HIM_IOB			*IOBp;
	register sp_t				*scbp;
	register adpu320_config_t		*cfp;
#ifdef ADPU320_DEBUG
	int					i;
	element_t				*elementp;
#endif

	cmd = PKT2CMD(pkt);

	scbp = (sp_t *)cmd->cmd_private;

	cfp = (adpu320_config_t *)scbp->SP_ConfigPtr;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_pktfree%d: cmd 0x%x scbp 0x%x cfp 0x%x ",
	    INST(cfp), cmd, scbp, cfp);
#endif

	IOBp = cmd->cmd_IOBp;

#ifdef ADPU320_DEBUG
	adpu320_printf("IOBp 0x%x\n", IOBp);
#endif

	ddi_dma_unbind_handle(cmd->rsv_dmahandle);

	ddi_dma_mem_free(&cmd->rsv_handle);

	ddi_dma_free_handle(&cmd->rsv_dmahandle);

#ifdef ADPU320_DEBUG
	for (i = 0; i < sizeof (HIM_IOB); i++) {
		*((char *)IOBp + i) = 0xaa;
	}

	for (elementp = cfp->Ptr_List->first_element;
	    elementp != NULL; elementp = elementp->next_element) {
		if (elementp->pktp == pkt) {
			adpu320_printf("adpu320_pktfree: elementp = 0x%x "
			    ": pktp = 0x%x : virt_addr = 0x%x\n",
			    elementp, pkt,
			    elementp->virt_addr);
			debug_enter("adpu320_pktfree\n");
		}
	}
#endif

	kmem_free(IOBp, sizeof (HIM_IOB) + sizeof (HIM_TS_SCSI));
}

void
adpu320_mem_release(
adpu320_config_t	*cfp,
int			flag
)
{
	if (flag & HA_CONF_REL) {
		kmem_free(cfp->ha_himConfig, sizeof (HIM_CONFIGURATION));

		cfp->ha_himConfig = NULL;

		flag &= ~HA_CONF_REL;
	}

	if (flag & HA_PROFILE_REL) {
		kmem_free(cfp->ha_profile, cfp->ab_ha_profilesize);

		cfp->ha_profile = NULL;

		flag &= ~HA_PROFILE_REL;
	}

}

void
adpu320_dmaget(
	struct scsi_pkt	*pkt
)
{
	struct adpu320_scsi_cmd	*cmd;
	sp_t			*scbp;
	struct adpu320_sg	*dmap;
	int			cnt;
	ddi_dma_cookie_t	*cookiep = pkt->pkt_cookies;

	cmd = PKT2CMD(pkt);
	scbp = (sp_t *)cmd->cmd_private;
	scbp->SP_SegCnt = (unsigned char) pkt->pkt_numcookies;

	for (cnt = 0; cnt < scbp->SP_SegCnt; cnt++) {
		/*
		 * save cookies away
		 */
		dmap = &scbp->Sp_sg_list[cnt];

#if OSM_BUS_ADDRESS_SIZE == 64
		dmap->data_len = APCI_FLIP64(
		    cookiep->dmac_size);

		dmap->data_addr = APCI_FLIP64(
		    cookiep->dmac_laddress);
#else
		dmap->data_len = APCI_FLIP((ulong_t)(
		    cookiep->dmac_size));

		dmap->data_addr = APCI_FLIP((ulong_t)
		    cookiep->dmac_address);
#endif
		cookiep++;
	}
}

static int adpu320_watch_abort_pkt(adpu320_config_t *, element_t *);

/*
 * This timeout will go off once every adpu320_watchdog_tick seconds
 * (5 seconds at the time of this comment).
 */
/* CSTYLED */
STATIC void
adpu320_i_watch(
caddr_t	arg
)
{
	adpu320_config_t	*cfp;
	element_t		*elementp;
	struct adpu320_scsi_cmd	*cmdp;
	element_t		*abort_list = NULL;
	int do_abort = FALSE;

	cfp = (adpu320_config_t *)arg;

#ifdef ADPU320_DEBUG2
	if (adpu320_debug & DTIME) {
		adpu320_printf("adpu320_i_watch%d: begin\n", INST(cfp));
	}
#endif
	mutex_enter(&cfp->ab_mutex);

	for (elementp = cfp->Ptr_List->first_element;
	    elementp != NULL; elementp = elementp->next_element) {
		if (elementp->pktp == NULL) {
			continue;
		}

		cmdp = PKT2CMD(elementp->pktp);

		if (cmdp == NULL) {
			continue;
		}

#ifdef ADPU320_DEBUG
		if (cmdp->cmd_pkt == NULL) {
			adpu320_printf("adpu320_i_watch: cmdp = 0x%x "
			    "cmd_pkt = NULL\n", cmdp);
			debug_enter("adpu320_i_watch1");
		}
#endif

		/*
		 * Some pkts have no timeout values.
		 */
		if (cmdp->cmd_pkt->pkt_time == 0) {
			continue;
		}

		if (elementp->dog_bite < cmdp->watch) {
			elementp->dog_bite++;
		}	else {
			cmn_err(CE_WARN, "Timeout on target %d lun %d. "
			    "Initiating recovery.",
			    cmdp->target, cmdp->lun);

			do_abort = TRUE;
			elementp->next_abort_element = abort_list;
			abort_list = elementp;
		}
	}


#ifdef ADPU320_DEBUG2
	adpu320_dump_scb("adpu320_i_watch: ", cfp,
	    (sp_t *)cmdp->cmd_private);

#if 0
	debug_enter("adpu320_i_watch2");
#endif
#endif

	if (do_abort) {
		cfp->ha_himFuncPtr->HIMDisableIRQ(cfp->ha_adapterTSH);

		while (abort_list != NULL) {
			elementp = abort_list;
			abort_list = elementp->next_abort_element;
			/*
			 * If this resets the SCSI bus all the other packets
			 * will have been blown away.
			 */
			if (adpu320_watch_abort_pkt(cfp, elementp))
				break;
		}

		/*
		 * Call the completion routine for any IOBs that were completed
		 * as a result of aborts/resets above.
		 */
		adpu320_drain_completed_queue(cfp);

		cfp->ha_himFuncPtr->HIMEnableIRQ(cfp->ha_adapterTSH);
	}

	adpu320_set_timeout(cfp);

	mutex_exit(&cfp->ab_mutex);
}

/*
 * adpu320_watch_abort_pkt() attempts to abort a timed out packet.
 * If the packet cannot be aborted or has already been aborted, reset
 * the SCSI bus.
 *
 * Returns TRUE if the SCSI bus was reset, FALSE if not reset.
 */
static
int
adpu320_watch_abort_pkt(adpu320_config_t *cfp, element_t *elementp)
{
	struct adpu320_scsi_cmd *cmdp = PKT2CMD(elementp->pktp);
	struct adpu320_scsi_cmd *new_cmdp;

	if (elementp->flag & ADPU320_IOB_ABORTED) {
		new_cmdp = adpu320_IobSpecial(cfp, (HIM_IOB *)cmdp->cmd_IOBp,
		    HIM_RESET_BUS_OR_TARGET, ADPU320_RESET_BUS, CFLAG_POLL);

		/*
		 * adpu320_drain_completed_queue() will drain any non
		 * special IOBs that are completed while we wait for this IOB.
		 */
		if (adpu320_wait_special_iob_complete(new_cmdp) != 0) {
			return (TRUE);
		} else {
			return (FALSE);
		}
	}	else {
		new_cmdp = adpu320_IobSpecial(cfp, (HIM_IOB *)cmdp->cmd_IOBp,
		    HIM_ABORT_TASK, 0, CFLAG_POLL);

		if (adpu320_wait_special_iob_complete(new_cmdp) == 0) {
			new_cmdp = adpu320_IobSpecial(cfp,
			    (HIM_IOB *)cmdp->cmd_IOBp,
			    HIM_RESET_BUS_OR_TARGET,
			    ADPU320_RESET_BUS, CFLAG_POLL);
			if (adpu320_wait_special_iob_complete(new_cmdp) != 0) {
				return (TRUE);
			} else {
				return (FALSE);
			}
		}	else {
			elementp->flag |= ADPU320_IOB_ABORTED;
			return (FALSE);
		}

		/*
		 * adpu320_drain_completed_queue will drain any non
		 * special IOBs that are completed while we wait for this IOB.
		 */
	}
}

void
adpu320_set_timeout(
adpu320_config_t		*cfp
)
{
	/*
	 * Reset the timeout to go off once every adpu320_tick seconds
	 * (60 seconds at the time of this comment).
	 */
	cfp->adpu320_timeout_id = timeout((void (*)(void *))adpu320_i_watch,
	    (caddr_t)cfp, adpu320_tick);

}

/*
 * Function : adpu320_tran_tgt_init()
 *
 * Description :
 *	It will be called when the node has been created by the
 * 	system. also see the adpu320_tran_free().
 *	This routine will allocate the "struct adpu320_unit" for
 *	each device node.
 *
 *  the traditional x86 scsi hba adpu320_blk structure is the
 *  adpu320_config_t structure in the adpu320 driver
 *
 *  tran_tgt_private points to the adpu320 structure
 *  tran_hba_private points to the adpu320_config_t
 *
 * OK
 */

/*ARGSUSED*/
/* CSTYLED */
STATIC int
adpu320_tran_tgt_init(
dev_info_t		*hba_dip,
dev_info_t		*tgt_dip,
scsi_hba_tran_t		*hba_tran,
struct scsi_device	*sd
)
{
	int 				targ;
	int				lun;
	adpu320_soft_state_t		*hba_adpu320p;
	adpu320_soft_state_t		*unit_adpu320p;
	adpu320_config_t		*cfp;

	targ = sd->sd_address.a_target;

	lun = sd->sd_address.a_lun;

	hba_adpu320p = SDEV2ADPU320(sd);

	cfp = hba_adpu320p->cfp;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DINIT) {
		adpu320_printf("adpu320_tran_tgt_init%d: %s%d: %s%d <%d,%d>\n",
		    INST(cfp),
		    ddi_get_name(hba_dip), ddi_get_instance(hba_dip),
		    ddi_get_name(tgt_dip), ddi_get_instance(tgt_dip),
		    targ, lun);
	}
#endif

	if (targ < 0 || targ > ((int)cfp->ha_MaxTargets) ||
	    lun < 0 ||
	    lun > (cfp->ha_profile->himu.TS_SCSI.AP_SCSINumberLuns[targ] - 1) ||
	    targ == ((int)cfp->ha_ScsiId)) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_tran_tgt_init%d: %s%d: %s%d bad "
		    "address <%d,%d>\n", INST(cfp),
		    ddi_get_name(hba_dip), ddi_get_instance(hba_dip),
		    ddi_get_name(tgt_dip), ddi_get_instance(tgt_dip),
		    targ, lun);
#endif

		return (DDI_FAILURE);
	}

	unit_adpu320p = kmem_zalloc(sizeof (adpu320_soft_state_t) +
	    sizeof (struct adpu320_unit), KM_SLEEP);

	bcopy((caddr_t)hba_adpu320p, (caddr_t)unit_adpu320p,
	    sizeof (*hba_adpu320p));

	unit_adpu320p->a_unitp = (struct adpu320_unit *)(unit_adpu320p+1);

	hba_tran->tran_tgt_private = unit_adpu320p;

	mutex_enter(&adpu320_global_mutex);

	SOFT2CFP(hba_adpu320p)->ab_child++;

	mutex_exit(&adpu320_global_mutex);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DINIT) {
		adpu320_printf("adpu320_tran_tgt_init%d: <%d,%d> adpu320 %x "
		    "unit %x tran %x\n", INST(cfp), targ, lun,
		    unit_adpu320p, unit_adpu320p->a_unitp, hba_tran);
	}
#endif

	return (DDI_SUCCESS);
}


/*
 * Function : adpu320_tran_tgt_probe()
 *
 * OK
 */
/*ARGSUSED*/
/* CSTYLED */
STATIC int
adpu320_tran_tgt_probe(
struct scsi_device	*sd,
int			(*callback)())
{
#ifdef ADPU320_DEBUG
	adpu320_soft_state_t		*softp;
	char				*s;
#endif
	int				rval;
	adpu320_config_t		*cfp;
	uint_t				loca;

	cfp = ADDR2CFP(&sd->sd_address);

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_tran_tgt_probe%d: - <%d,%d>\n", INST(cfp),
	    sd->sd_address.a_target, sd->sd_address.a_lun);
#endif

	loca = (sd->sd_address.a_target) + (sd->sd_address.a_lun * MAX_TARGETS);

	if (cfp->ha_targetTSH[loca] == NULL) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_tran_tgt_probe%d: Task handle not set "
		    "<%d,%d>\n", INST(cfp),
		    sd->sd_address.a_target, sd->sd_address.a_lun);
#endif

		scsi_unprobe(sd);

		return (SCSIPROBE_NORESP);
	}

#ifdef ADPU320_DEBUG
	softp = SDEV2ADPU320(sd);

#endif
	rval = scsi_hba_probe(sd, callback);

#ifdef ADPU320_DEBUG
	switch (rval) {
		case SCSIPROBE_NOMEM:
			s = "scsi_probe_nomem";
			break;

		case SCSIPROBE_EXISTS:
			s = "scsi_probe_exists";
			break;

		case SCSIPROBE_NONCCS:
			s = "scsi_probe_nonccs";
			break;

		case SCSIPROBE_FAILURE:
			s = "scsi_probe_failure";
			break;

		case SCSIPROBE_BUSY:
			s = "scsi_probe_busy";
			break;

		case SCSIPROBE_NORESP:
			s = "scsi_probe_noresp";
			break;

		default:
			s = "???";
			break;
	}

	if (adpu320_debug & DPROBE) {
		adpu320_printf("adpu320_tran_tgt_probe%d: adpu320%d: %s "
		    "target %d lun %d %s\n", INST(cfp),
		    ddi_get_instance(ADPU320_DIP(softp)),
		    ddi_get_name(sd->sd_dev),
		    sd->sd_address.a_target,
		    sd->sd_address.a_lun, s);
	}
#endif

	return (rval);
}

/*
 * Function : adpu320_tran_tgt_free()
 *
 * OK
 */
/*ARGSUSED*/
/* CSTYLED */
STATIC void
adpu320_tran_tgt_free(
dev_info_t		*hba_dip,
dev_info_t		*tgt_dip,
scsi_hba_tran_t		*hba_tran,
struct scsi_device	*sd
)
{
	adpu320_soft_state_t	*softp;
	adpu320_soft_state_t	*adpu320_unitp;
	adpu320_config_t	*cfp;

	adpu320_unitp = hba_tran->tran_tgt_private;

	softp = SDEV2ADPU320(sd);

	cfp = SOFT2CFP(softp);

	/*
	 * decrement count in block (per driver instance) structure
	 */
	mutex_enter(&adpu320_global_mutex);

	cfp->ab_child--;

	mutex_exit(&adpu320_global_mutex);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DINIT) {
		adpu320_printf("adpu320_tran_tgt_free%d: <%d,%d> adpu320_unit "
		    "at %x\n", INST(cfp), sd->sd_address.a_target,
		    sd->sd_address.a_lun, adpu320_unitp);
	}
#endif

	kmem_free((caddr_t)adpu320_unitp, sizeof (adpu320_soft_state_t)
	    + sizeof (struct adpu320_unit));
}

/*
 * Function : adpu320_tran_start()
 *
 * OK
 */
/*ARGSUSED*/
/* CSTYLED */
STATIC int
adpu320_tran_start(
struct scsi_address		*ap,
register struct scsi_pkt	*pktp
)
{
	register adpu320_config_t		*cfp;
	register sp_t				*scbp;
	register HIM_IOB			*IOBp;
	register struct adpu320_scsi_cmd	*cmd;
#ifdef ADPU320_DEBUG
	int					loca;
#endif

	cmd = PKT2CMD(pktp);

	cmd->cmd_pkt = pktp;

	scbp = (sp_t *)cmd->cmd_private;

	adpu320_dmaget(pktp);

	cfp = PKT2CFP(pktp);

	IOBp = cmd->cmd_IOBp;

	/*
	 * Compute the timeout based on the timeout value in pkt->pkt_time.
	 * pkt_time is in seconds, but our timeout routine only goes off
	 * every adpu320_watchdog_tick seconds.  So we will compute the
	 * number of timeout intervals this command should take, and round
	 * up.
	 */
	cmd->watch = (pktp->pkt_time + adpu320_watchdog_tick - 1) /
	    adpu320_watchdog_tick;

	IOBp->targetCommandLength = pktp->pkt_cdblen;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DPKT) {
		adpu320_printf("+++++++++++++++++++++++++++++++++++++++++\n");

		adpu320_printf("adpu320_tran_start%d: pktp %x scbp %x cfp %x "
		    "cmd %x watch 0x%x\n", INST(cfp), pktp, scbp, cfp,
		    PKT2CMD(pktp), cmd->watch);
	}
#endif

	/*
	 * initialize in case of packet reuse
	 */
	pktp->pkt_state = 0;
	pktp->pkt_statistics = 0;
	pktp->pkt_resid = 0;

	/*
	 * Set default, then handle special cases
	 * Do this here because of packet reuse by both target and him
	 */
	if (pktp->pkt_flags & FLAG_NOINTR) {
		cfp->ab_flag |= ADPU320_POLLING;
	}

	/*
	 * disable disconnect when requested or for polling
	 */
#if 0
	if (pktp->pkt_flags & (FLAG_NODISCON | FLAG_NOINTR)) {
		((HIM_TS_SCSI *)IOBp->transportSpecific)->
		    disallowDisconnect = 1;
	}
#endif
#ifdef ADPU320_DEBUG
	if (adpu320_debug & DPKTDMP) {
		adpu320_dump_config(cfp);
	}
#endif

	IOBp->function = HIM_INITIATE_TASK;

	if (cmd->cmd_cflags & CFLAG_DMASEND) {
		IOBp->flagsIob.inboundData = 0;
		IOBp->flagsIob.outboundData = 1;
	}
	else
	{
		IOBp->flagsIob.inboundData = 1;
		IOBp->flagsIob.outboundData = 0;
	}

	IOBp->relatedIob = NULL;
	IOBp->data.busAddress = scbp->SP_SegPtr;
	IOBp->data.bufferSize = sizeof (struct adpu320_sg) * scbp->SP_SegCnt;
	IOBp->data.virtualAddress = scbp->Sp_sg_list;

	/*
	 * Set the most significant bit of the last length field to 1
	 */
	if (scbp->SP_SegCnt) {
		scbp->Sp_sg_list[scbp->SP_SegCnt - 1].data_len |=
		    APCI_FLIP((ulong_t)0x80000000);
	}

	IOBp->flagsIob.autoSense = 1;

	if (scbp->Sp_CDB[0] == 0x00 || scbp->Sp_CDB[0] == 0x1b ||
	    scbp->Sp_CDB[0] == 0x01) {
		IOBp->flagsIob.inboundData = 0;
		IOBp->flagsIob.outboundData = 0;
		IOBp->data.bufferSize = 0;
	}

	IOBp->postRoutine = (HIM_POST_PTR)OSMIobCompleted;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DPKT) {
		adpu320_printf("adpu320_tran_start: id (%x) lun (%x)\n",
		    ap->a_target, ap->a_lun);

		adpu320_dump_scb("adpu320_tran_start", cfp, scbp);
	}
#endif /* ADPU320_DEBUG */

	/*
	 * track the last outstanding scb (scsi_pkt) for adpu320_tran_abort
	 */
	cfp->ab_last_IOBp = IOBp;

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_tran_start: pktp->pkt_flags = 0x%x\n",
	    pktp->pkt_flags);
#endif

	if (pktp->pkt_flags & FLAG_TAGMASK) {
		((HIM_TS_SCSI *)(IOBp->transportSpecific))->
		    forceUntagged = HIM_FALSE;

		switch (pktp->pkt_flags & FLAG_TAGMASK) {
		case FLAG_STAG:
			IOBp->taskAttribute = HIM_TASK_SIMPLE;
			break;

		case FLAG_HTAG:
			IOBp->taskAttribute = HIM_TASK_HEAD_OF_QUEUE;
			break;

		case FLAG_OTAG:
			IOBp->taskAttribute = HIM_TASK_ORDERED;
			break;

		default:
			IOBp->taskAttribute = HIM_TASK_SIMPLE;
			break;
		}
	}
	else
	{
		IOBp->taskAttribute = HIM_TASK_SIMPLE;

		((HIM_TS_SCSI *)(IOBp->transportSpecific))->
		    forceUntagged = HIM_TRUE;
	}

	mutex_enter(&cfp->ab_mutex);

#if 0
	print_iob(IOBp);
#endif

#if 0
	dump_qout("before queue", cfp->ha_adapterTSH);
#endif

	if (pktp->pkt_flags & FLAG_NOINTR) {
		cfp->ha_himFuncPtr->HIMDisableIRQ(cfp->ha_adapterTSH);
	}

	/*
	 * check for frozen queue before sending to the HIM
	 */
	if (cfp->ab_flag & (ADPU320_QUEUE_FROZEN | ADPU320_PAC_ACTIVE)) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_tran_start%d: queueing scbp 0x%x "
		    "IOBp 0x%x\n", INST(cfp), scbp, IOBp);

		loca = (cmd->target) + (cmd->lun * MAX_TARGETS);

		cfp->ha_stats[loca].iobs_queued++;

		if (cfp->ha_stats[loca].iobs_queued >
		    cfp->ha_stats[loca].max_iobs_queued) {
			cfp->ha_stats[loca].max_iobs_queued =
			    cfp->ha_stats[loca].iobs_queued;
		}
#endif

		/*
		 * Queue the request up so that adpu320_run_callbacks()
		 * can complete the finished request.
		 */
		adpu320_add_queue(&cfp->ab_scb_start_queue,
		    (adpu320_entry_t *)scbp);
	}
	else
	{
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_tran_start%d: sending scbp 0x%x IOBp "
		    "0x%x\n", INST(cfp), scbp, IOBp);
#endif
		adpu320_queue_IOB(IOBp);

	}

	if (pktp->pkt_flags & FLAG_NOINTR) {
		adpu320_pollret(cfp, pktp);

		cfp->ab_flag &= ~ADPU320_POLLING;

		cfp->ha_himFuncPtr->HIMEnableIRQ(cfp->ha_adapterTSH);
	}

	mutex_exit(&cfp->ab_mutex);

	return (TRAN_ACCEPT);
}

void
adpu320_queue_IOB(
HIM_IOB			*IOBp
)
{
	adpu320_config_t		*cfp;
	struct adpu320_scsi_cmd		*cmdp;
	element_t			*new_element;
	struct scsi_pkt			*pktp;
	sp_t				*scbp;
#ifdef ADPU320_DEBUG
	int				loca;
#endif

	cmdp = (struct adpu320_scsi_cmd *)IOBp->osRequestBlock;

	pktp = cmdp->cmd_pkt;

	cfp = PKT2CFP(pktp);

#ifdef ADPU320_DEBUG
	loca = (cmdp->target) + (cmdp->lun * MAX_TARGETS);

	cfp->ha_stats[loca].iobs_sent++;

	cfp->ha_stats[loca].total_iobs++;

	if (cfp->ha_stats[loca].iobs_sent > cfp->ha_stats[loca].max_iobs_sent) {
		cfp->ha_stats[loca].max_iobs_sent =
		    cfp->ha_stats[loca].iobs_sent;
	}
#endif

	scbp = (sp_t *)cmdp->cmd_private;

	cmdp->cmd_cflags &= ~CFLAG_FINISHED;

	/*
	 * calculate deadline from pkt_time
	 * Instead of multiplying by 100 (ie. HZ), we multiply by 128 so
	 * we can shift and at the same time have a 28% grace period
	 * we ignore the rare case of pkt_time == 0 and deal with it
	 * in isp_i_watch()
	 */
	new_element = (element_t *)kmem_zalloc(sizeof (element_t), KM_SLEEP);

	new_element->virt_addr = (uchar_t *)(&scbp->Sp_sense.sts_sensedata);
	new_element->phys_addr = (HIM_BUS_ADDRESS) (scbp->Sp_SensePtr);
	new_element->pktp	= pktp;

	adpu320_append_element(cfp->Ptr_List, new_element);

	cfp->ha_himFuncPtr->HIMQueueIOB(IOBp);
}

/*
 * Function : adpu320_tran_abort()
 *
 * Description :
 * 	Abort specific command on target device
 * 	returns 0 on failure, 1 on success
 *
 * OK
 */
static int
adpu320_tran_abort(
struct scsi_address	*ap,
struct scsi_pkt		*pktp
)
{
	HIM_IOB			*IOBp;
	struct adpu320_scsi_cmd	*cmd;
	struct adpu320_scsi_cmd	*new_cmd;
	adpu320_config_t	*cfp;
	int			status;

	cfp = ADDR2CFP(ap);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DPKT) {
		adpu320_printf("adpu320_tran_abort%d:\n", INST(cfp));
	}
#endif

	if (pktp) {
		cmd = PKT2CMD(pktp);
	} else {
		cmd = NULL;
	}

	/*
	 * abort last packet transmistted by this controller
	 */
	if (!cmd) {
		IOBp = cfp->ab_last_IOBp;
	}
	else
	{
		IOBp = cmd->cmd_IOBp;
	}

	mutex_enter(&cfp->ab_mutex);

	cfp->ha_himFuncPtr->HIMDisableIRQ(cfp->ha_adapterTSH);

	/*
	 * Abort Task
	 */
	new_cmd = adpu320_IobSpecial(cfp, IOBp, HIM_ABORT_TASK, 0, CFLAG_POLL);
	if (adpu320_wait_special_iob_complete(new_cmd) == 0) {
		status = 0;
	}
	else
	{
		status = 1;
	}

	/*
	 * Call the completion routine for any IOBs that were completed
	 * as a result of the abort above.
	 */
	adpu320_drain_completed_queue(cfp);

	cfp->ha_himFuncPtr->HIMEnableIRQ(cfp->ha_adapterTSH);

	mutex_exit(&cfp->ab_mutex);

	return (status);
}

/*
 * Function : adpu320_tran_reset()
 *
 * Description :
 * 	returns 0 on failure, 1 on success
 *
 * OK
 */
/* CSTYLED */
STATIC int
adpu320_tran_reset(
struct scsi_address	*ap,
int			level
)
{
	adpu320_config_t	*cfp;
	int			ret;
	int			rs_tar;
	struct adpu320_scsi_cmd	*cmdp = NULL;

	ret = 0;

	cfp = ADDR2CFP(ap);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DPKT) {
		adpu320_printf("adpu320_tran_reset%d:\n", INST(cfp));
	}
#endif

	switch (level) {
	case RESET_ALL:
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			cmn_err(CE_NOTE, "adpu320_tran_reset bus %d\n",
			    INST(cfp));
		}
#endif
		mutex_enter(&cfp->ab_mutex);

		cfp->ha_himFuncPtr->HIMDisableIRQ(cfp->ha_adapterTSH);

		cmdp = adpu320_IobSpecial(cfp, (HIM_IOB *) 0,
		    HIM_RESET_BUS_OR_TARGET, ADPU320_RESET_BUS, CFLAG_POLL);

		if (adpu320_wait_special_iob_complete(cmdp) == 0) {
			cmn_err(CE_WARN, "adpu320 %d: bus reset failed",
			    INST(cfp));
		}

		/*
		 * Call the completion routine for any IOBs that were completed
		 * as a result of the reset above.
		 */
		adpu320_drain_completed_queue(cfp);

		cfp->ha_himFuncPtr->HIMEnableIRQ(cfp->ha_adapterTSH);

		mutex_exit(&cfp->ab_mutex);

		break;

	case RESET_TARGET:
		/*
		 * reset a target using IOB
		 */
		rs_tar = ap->a_target + (ap->a_lun * MAX_TARGETS);

		mutex_enter(&cfp->ab_mutex);

		cfp->ha_himFuncPtr->HIMDisableIRQ(cfp->ha_adapterTSH);

		cmdp = adpu320_IobSpecial(cfp, (HIM_IOB *) 0,
		    HIM_RESET_BUS_OR_TARGET, rs_tar, CFLAG_POLL);

		if (adpu320_wait_special_iob_complete(cmdp) == 0) {
			cmn_err(CE_WARN, "adpu320 %d: target %d reset failed",
			    INST(cfp), (int)ap->a_target);
		}

		/*
		 * Call the completion routine for any IOBs that were completed
		 * as a result of the reset above.
		 */
		adpu320_drain_completed_queue(cfp);

		cfp->ha_himFuncPtr->HIMEnableIRQ(cfp->ha_adapterTSH);

		mutex_exit(&cfp->ab_mutex);

		break;
	default:
		return (0);
	}

	if (cmdp && !(cmdp->cmd_cflags & CFLAG_FAILED))
		ret = 1;
	else
		ret = 0;

	return (ret);
}

/*
 * Function : adpu320_capchk()
 *
 * OK
 */
/* CSTYLED */
STATIC int
adpu320_capchk(
char	*cap,
int	tgtonly,
int	*cidxp
)
{
	register int	cidx;

	if ((tgtonly != 0 && tgtonly != 1) || cap == (char *)0) {
		return (FALSE);
	}

	if ((cidx = scsi_hba_lookup_capstr(cap)) == -1) {
		return (FALSE);
	}

	*cidxp = cidx;

	return (TRUE);
}

/*
 * Function : adpu320_tran_getcap()
 *
 * OK
 */
/* CSTYLED */
STATIC int
adpu320_tran_getcap(
struct scsi_address	*ap,
char			*cap,
int			tgtonly
)
{
	int			ckey;
	int			total_sectors;
	int			h;
	int			s;
	int			rval;
	adpu320_config_t	*cfp;
	struct adpu320_unit	*unitp;
	HIM_TARGET_PROFILE	*targetProfile;
	int			loca;

	cfp = ADDR2CFP(ap);

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_tran_getcap%d: <%s> target = 0x%x, "
	    "lun = 0x%x\n", INST(cfp), cap, ap->a_target, ap->a_lun);
#endif

	if (adpu320_capchk(cap, tgtonly, &ckey) != TRUE) {
		return (UNDEFINED);
	}

	if (tgtonly) {
		loca = (ap->a_target) + (ap->a_lun * MAX_TARGETS);

		targetProfile = cfp->ha_targetProfile[loca];

		if (targetProfile == NULL) {
			return (0);
		}
	}

	rval = 0;

	switch (ckey) {
	case SCSI_CAP_DMA_MAX:
		rval = adpu320_dmalim.dma_attr_maxxfer;
		break;

	case SCSI_CAP_GEOMETRY:
		unitp = ADDR2ADPU320UNITP(ap);

		total_sectors = unitp->au_total_sectors;
		if ((cfp->ha_profile->AP_ExtendedTrans != 0) &&
		    (total_sectors > 0x200000)) {
			h = 255;
			s = 63;
		}
		else
		{
			h = 64;
			s = 32;
		}

#ifdef ADPU320_DEBUG
		adpu320_printf("heads = 0x%x, sectors = 0x%x "
		    "AP_ExtendedTrans = 0x%x total_sectors = 0x%x\n",
		    h, s, cfp->ha_profile->AP_ExtendedTrans, total_sectors);
#endif
		rval = ADPU320_SETGEOM(h, s);

		break;

	case SCSI_CAP_INITIATOR_ID:
		rval = cfp->ha_profile->himu.TS_SCSI.AP_SCSIAdapterID;
		break;

	case SCSI_CAP_ARQ:
		rval = 1;
		break;

	case SCSI_CAP_MSG_OUT:
		rval = 1;
		break;

	case SCSI_CAP_SYNCHRONOUS:
		if (tgtonly) {
			if (targetProfile->himu.TS_SCSI.TP_SCSIMaxSpeed) {
				rval = 1;
			}
		}
		else
		{
			rval = 1;
		}
		break;

	case SCSI_CAP_DISCONNECT:
		if (tgtonly) {
			if (targetProfile->himu.TS_SCSI.
			    TP_SCSIDisconnectAllowed)
			rval = 1;
		}
		else
		{
			rval = 1;
		}
		break;

	case SCSI_CAP_WIDE_XFER:
		if (tgtonly) {
			if (targetProfile->himu.TS_SCSI.TP_SCSIMaxWidth > 8) {
				rval = 1;
			}
		}
		else
		{
			if (cfp->ha_profile->himu.TS_SCSI.AP_SCSIWidth > 8) {
				rval = 1;
			}
		}
		break;

	case SCSI_CAP_TAGGED_QING:
		if (tgtonly) {
			if (targetProfile->TP_TaggedQueuing) {
				rval = 1;
			}
		}
		else
		{
			rval = 1;
		}

		break;

	case SCSI_CAP_UNTAGGED_QING:
		rval = 1;
		break;

	case SCSI_CAP_PARITY:
		rval = 1;
		break;

	case SCSI_CAP_LINKED_CMDS:
		rval = 0;
		break;

	default:
		rval = UNDEFINED;
		break;
	}

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_tran_getcap returns: 0x%x\n", rval);
#endif

	return (rval);
}

/*
 * Function : adpu320_tran_setcap()
 *
 * OK
 */
/* CSTYLED */
STATIC int
adpu320_tran_setcap(
struct scsi_address	*ap,
char			*cap,
int			value,
int			tgtonly
)
{
	int			ckey;
	int			status;
	struct adpu320_unit	*unitp;
	adpu320_config_t	*cfp;
	HIM_TARGET_PROFILE	*targetProfile;
	int			loca;

	status = FALSE;

	cfp = ADDR2CFP(ap);

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_tran_setcap%d: <%s> target = 0x%x, "
	    "lun = 0x%x\n", INST(cfp), cap, ap->a_target, ap->a_lun);
#endif

	if (adpu320_capchk(cap, tgtonly, &ckey) != TRUE) {
		return (UNDEFINED);
	}

	if (tgtonly) {
		loca = (ap->a_target) + (ap->a_lun * MAX_TARGETS);

		targetProfile = cfp->ha_targetProfile[loca];

		if (targetProfile == NULL) {
			return (0);
		}
	}

	unitp = ADDR2ADPU320UNITP(ap);

	switch (ckey) {
	case SCSI_CAP_SECTOR_SIZE:

		status = TRUE;

		break;

	case SCSI_CAP_ARQ:
		if (tgtonly) {
			unitp->au_arq = (uint_t)value;
			status = TRUE;
		}

		break;

	case SCSI_CAP_TOTAL_SECTORS:
		unitp->au_total_sectors = value;

		status = TRUE;

		break;

	case SCSI_CAP_TAGGED_QING:
		if (tgtonly) {
			if (targetProfile->TP_TaggedQueuing) {
				status = TRUE;
			}
		}

		break;

	case SCSI_CAP_GEOMETRY:
	default:
		break;
	}

#ifdef ADPU320_DEBUG
	adpu320_printf("adpu320_tran_setcap returns: 0x%x\n", status);
#endif

	return (status);
}

/*
 * Function : adpu320_tran_setup_pkt()
 *
 * OK
 */
/* CSTYLED */
STATIC int
adpu320_tran_setup_pkt(
struct scsi_pkt		*pkt,
int			(*callback)(caddr_t),
caddr_t			arg
)
{
	struct adpu320_scsi_cmd	*cmd;
	int			err;

	cmd = PKT2CMD(pkt);

	/*
	 * Allocate a pkt
	 */
#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_tran_setup_pkt%d:\n", INST(cfp));
	}
#endif
	err = adpu320_pktalloc(pkt, callback, arg);


	if (err != 0) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_tran_setup_pkt%d:"
		    " adpu320_pktalloc failed\n",
		    INST(ADDR2CFP(ap)));
#endif
		return (-1);
	}

	cmd->cmd_pkt		= pkt;
	cmd->cmd_cflags		= 0;
	cmd->watch		= 0;

	ASSERT(pkt->pkt_resid == 0);
	ASSERT(pkt->pkt_statistics == 0);
	ASSERT(pkt->pkt_reason == 0);

	return (0);
}

int
adpu320_tran_quiesce(
dev_info_t	*dip
)
{
	register adpu320_config_t		*cfp;
	scsi_hba_tran_t				*tran;
	register struct adpu320_scsi_cmd	*quiesce_cmd;
	clock_t					until;

	/*
	 * tran saved by scsi_hba_attach (3rd param) called in attach
	 */
	tran = ddi_get_driver_private(dip);

	if (tran == NULL) {
		return (DDI_FAILURE);
	}

	cfp = TRAN2CFP(tran);

	if (cfp == NULL) {
		return (DDI_FAILURE);
	}

	mutex_enter(&cfp->ab_mutex);

	/*
	 * If we are already in a quiesce, or bus reset, or queue full,
	 * then fail the request.  Someone could pull the drive, generate
	 * a PAC and then run cfgadm to quiesce the bus,  or they could
	 * try to quiesce the bus before a previous unquiesce (PAC) has
	 * finished.  We will just fail for now, and they can try again
	 * later.
	 */
	if (cfp->ab_flag & (ADPU320_QUEUE_FROZEN | ADPU320_PAC_ACTIVE)) {
		/*
		 * We will wait 10 seconds for the condition to clear.  If
		 * it doesn't clear by then, we will fail the quiesce.
		 */
		until = drv_usectohz(USECSPERSECOND) * 10;

		if ((cv_reltimedwait_sig(&cfp->ab_quiesce_cv,
		    &cfp->ab_mutex, until, TR_CLOCK_TICK)) == 0) {
			/*
			 * Someone has interrupted our wait.
			 */
			mutex_exit(&cfp->ab_mutex);

			return (DDI_FAILURE);
		}

		if (cfp->ab_flag &
		    (ADPU320_QUEUE_FROZEN | ADPU320_PAC_ACTIVE)) {
			mutex_exit(&cfp->ab_mutex);

			return (DDI_FAILURE);
		}
	}

	cfp->ha_himFuncPtr->HIMDisableIRQ(cfp->ha_adapterTSH);

	quiesce_cmd = adpu320_IobSpecial(cfp, NULL, HIM_QUIESCE, 0, CFLAG_POLL);

	adpu320_wait_special_iob_complete(quiesce_cmd);

	/*
	 * Call the completion routine for any IOBs that were completed
	 * while we were waiting for QUIESCE
	 */
	adpu320_drain_completed_queue(cfp);

	cfp->ha_himFuncPtr->HIMEnableIRQ(cfp->ha_adapterTSH);

	cfp->ab_flag |= ADPU320_QUEUE_FROZEN;

	mutex_exit(&cfp->ab_mutex);

	return (DDI_SUCCESS);
}

static int
adpu320_tran_unquiesce(
dev_info_t	*dip
)
{
	register adpu320_config_t		*cfp;
	scsi_hba_tran_t				*tran;
	register struct adpu320_scsi_cmd	*PAC_cmd;

	/*
	 * tran saved by scsi_hba_attach (3rd param) called in attach
	 */
	tran = ddi_get_driver_private(dip);

	if (tran == NULL) {
		return (DDI_FAILURE);
	}

	cfp = TRAN2CFP(tran);

	if (cfp == NULL) {
		return (DDI_FAILURE);
	}

	mutex_enter(&cfp->ab_mutex);

	/*
	 * If PAC is already active, then we do not need to send
	 * another one, and we should not unfreeze the queue.
	 * HIM_EVENT_OSMUNFREEZE will trigger the clear of
	 * QUEUE_FROZEN when it is done.
	 */
	if (cfp->ab_flag & ADPU320_PAC_ACTIVE) {
		mutex_exit(&cfp->ab_mutex);

		return (DDI_SUCCESS);
	}

	cfp->ha_himFuncPtr->HIMDisableIRQ(cfp->ha_adapterTSH);

	PAC_cmd = adpu320_IobSpecial(cfp, NULL,
	    HIM_PROTOCOL_AUTO_CONFIG, 0, CFLAG_POLL);

	adpu320_wait_special_iob_complete(PAC_cmd);

	/*
	 * Call the completion routine for any IOBs that were completed
	 * while we were waiting for PAC to finish.
	 */
	adpu320_drain_completed_queue(cfp);

	cfp->ha_himFuncPtr->HIMEnableIRQ(cfp->ha_adapterTSH);

	cfp->ab_flag &= ~ADPU320_QUEUE_FROZEN;

	mutex_exit(&cfp->ab_mutex);

	return (DDI_SUCCESS);
}

/* CSTYLED */
STATIC
struct adpu320_scsi_cmd	*
adpu320_IobSpecial(
adpu320_config_t	*cfp,
HIM_IOB			*old_IOBp,
int			function,
int			reset_target,
uint_t			poll_flag
)
{
	int			target;
	int			lun;
	int			loca;
	HIM_IOB			*IOBp;
	struct adpu320_scsi_cmd	*cmd;

#ifdef ADPU320_DEBUG
	adpu320_verify_lock_held("adpu320_IobSpecial", &cfp->ab_mutex);

	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_IobSpecial%d:, function=%x, "
		    "reset_target=%d\n", INST(cfp), function, reset_target);
	}
#endif

	cmd = kmem_zalloc(sizeof (struct adpu320_scsi_cmd), KM_SLEEP);

	cmd->cmd_cflags |= poll_flag;

	if (old_IOBp) {
		target = ((struct adpu320_scsi_cmd *)
		    old_IOBp->osRequestBlock)->target;

		lun = ((struct adpu320_scsi_cmd *)
		    old_IOBp->osRequestBlock)->lun;
	}

	loca = (target) + (lun * MAX_TARGETS);

	switch (function) {
	case HIM_ABORT_TASK:
	case HIM_TERMINATE_TASK:
		/*
		 * allocate a new IOB
		 */
		IOBp = adpu320_allocIOB(cfp, cmd);
		IOBp->function = function;
		IOBp->relatedIob = old_IOBp;
		break;
	case HIM_ABORT_TASK_SET:
		ASSERT(old_IOBp != NULL);
		/*
		 * allocate a new IOB
		 */
		IOBp = adpu320_allocIOB(cfp, cmd);
		IOBp->function = function;
		IOBp->taskSetHandle = cfp->ha_targetTSH[loca];
		break;
	case HIM_RESET_BUS_OR_TARGET:
		IOBp = adpu320_allocIOB(cfp, cmd);
		IOBp->function = function;
		if (reset_target < 0) {
			IOBp->taskSetHandle = cfp->ha_adapterTSH;
		}
		else
		{
			IOBp->taskSetHandle = cfp->ha_targetTSH[reset_target];
		}
		break;
	case HIM_RESUME:
	case HIM_SUSPEND:
	case HIM_QUIESCE:
	case HIM_RESET_HARDWARE:
		IOBp = adpu320_allocIOB(cfp, cmd);
		IOBp->function = function;
		IOBp->taskSetHandle = cfp->ha_adapterTSH;
		break;
	case HIM_UNFREEZE_QUEUE:
		IOBp = adpu320_allocIOB(cfp, cmd);
		IOBp->function = function;
		IOBp->taskSetHandle = old_IOBp->taskSetHandle;
		break;
	case HIM_PROTOCOL_AUTO_CONFIG:
		cfp->ab_flag |= ADPU320_PAC_ACTIVE;
		IOBp = adpu320_allocIOB(cfp, cmd);
		IOBp->function = function;
		IOBp->taskSetHandle = cfp->ha_adapterTSH;
		IOBp->taskAttribute = HIM_TASK_SIMPLE;
		IOBp->targetCommand = 0; /* Full Scan */
		break;

	default:
		cmn_err(CE_WARN, "The function is: %x\n", function);
#ifdef ADPU320_DEBUG
		debug_enter("adpu320_IobSpecial");
#endif /* ADPU320_DEBUG */
	}

	/*
	 * The upper layers could be asking us to reset a device that no
	 * longer exists.
	 */
	if (IOBp->taskSetHandle == NULL) {
		adpu320_free_cmd(cmd, IOBp);

		return (NULL);
	}

	IOBp->postRoutine = adpu320_IobSpecialCompleted;

	cfp->ha_himFuncPtr->HIMQueueIOB(IOBp);

	return (cmd);
}

static int adpu320_poll_request_max_time = 0;
static int adpu320_poll_request_scale = 2000;

/*
 * Must be holding ab_mutex on entry.
 *
 * 0 is fail - 1 is success
 */
int
adpu320_wait_special_iob_complete(
struct adpu320_scsi_cmd		*cmd
)
{
	adpu320_config_t	*cfp;
	HIM_IOB			*IOBp;
	int			time;
	int			retval;

	if (cmd == NULL) {
		return (0);
	}

	IOBp = cmd->cmd_IOBp;

	cfp = cmd->cmd_cfp;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_wait_special_iob_complete%d:\n",
		    INST(cfp));
	}

	adpu320_verify_lock_held("adpu320_wait_special_iob", &cfp->ab_mutex);
#endif

	time = cfp->ha_profile->AP_AutoConfigTimeout *
	    adpu320_poll_request_scale;
	// time = cfp->ha_profile->AP_AutoConfigTimeout * 10000;

	/*
	 * Poll for the command to complete.  Even if the command fails,
	 * the CFLAG_FINISHED bit will be set.
	 */
	while ((cmd->cmd_cflags & CFLAG_FINISHED) == 0) {
		if (time <= 0) {
			break;
		}

		adpu320_poll_request(cfp);

		drv_usecwait(1000);
		// drv_usecwait(10000);
		time--;
	}

	if (adpu320_poll_request_max_time <
	    ((cfp->ha_profile->AP_AutoConfigTimeout
	    * adpu320_poll_request_scale) - time)) {
		adpu320_poll_request_max_time =
		    (cfp->ha_profile->AP_AutoConfigTimeout
		    * adpu320_poll_request_scale) - time;
	}

	/*
	 * Check to see if the command has timed out.  We should probably
	 * abort this command, but this should never happen.
	 */
	if (time <= 0) {
		cmn_err(CE_WARN, "adpu320_IobSpecial: adpu320_poll_request "
		    "timeout ");

		/*
		 * We are not freeing the command because we didn't abort it,
		 * and it may finish.
		 */
		return (0);
	}

	retval = 1;

	/*
	 * Check to see if the command failed (taskStatus was not HIM_IOB_GOOD)
	 */
	if (cmd->cmd_cflags & CFLAG_FAILED) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_wait_special_iob_complete%d: "
		    " PAC failed\n", INST(cfp));
#endif
		retval = 0;
	}

	adpu320_free_cmd(cmd, IOBp);

	return (retval);
}

void
adpu320_free_cmd(
struct adpu320_scsi_cmd	*cmd,
HIM_IOB			*IOBp
)
{
	ddi_dma_unbind_handle(cmd->rsv_dmahandle);

	/*
	 * Free IOB_reserve
	 */
	ddi_dma_mem_free(&cmd->rsv_handle);

	ddi_dma_free_handle(&cmd->rsv_dmahandle);

	kmem_free(IOBp, sizeof (HIM_IOB) + sizeof (HIM_TS_SCSI));

	kmem_free(cmd, sizeof (struct adpu320_scsi_cmd));
}

HIM_UINT32
adpu320_IobSpecialCompleted(
HIM_IOB		*IOBp
)
{
	struct adpu320_scsi_cmd	*cmd;
	adpu320_config_t	*cfp;

	cmd = (struct adpu320_scsi_cmd *)IOBp->osRequestBlock;

	cfp = cmd->cmd_cfp;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("IobSpecialCompleted%d: status = %x\n",
		    INST(cfp), IOBp->taskStatus);
	}
#endif

	if (IOBp->function == HIM_PROTOCOL_AUTO_CONFIG) {
		/*
		 * The queue was just unfrozen after some sort of OSMEvent
		 * We need to re-validate and un-freeze the queue.  Also
		 * wakeup anyone who was waiting for the freeze/unfreeze
		 * sequence to be over.
		 */
		if (IOBp->taskStatus == HIM_IOB_GOOD) {
			adpu320_validate(cfp);
		}

		cfp->ab_flag &= ~ADPU320_PAC_ACTIVE;

		adpu320_restart_queues(cfp);
	}

	if (cmd->cmd_cflags & CFLAG_POLL) {
		if (IOBp->taskStatus != HIM_IOB_GOOD) {
			cmd->cmd_cflags |= CFLAG_FAILED;
		}

		cmd->cmd_cflags |= CFLAG_FINISHED;
	}
	else
	{
		adpu320_free_cmd(cmd, IOBp);
	}

	return (0);
}


/*
 * Function : adpu320_run_callback()
 *
 * Description :
 * allows us to run stacked up callbacks beyond
 * the polling loop inside adpu320_pollret
 *
 * OK
 */
/* CSTYLED */
STATIC uint_t
adpu320_run_callbacks(
caddr_t	arg
)
{
	register adpu320_config_t	*cfp;
	uint_t				intr_status;

	cfp = (adpu320_config_t *)arg;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DTIME) {
		adpu320_printf("adpu320_run_callbacks%d: 0x%x\n", INST(cfp),
		    cfp);
	}
#endif
	mutex_enter(&cfp->ab_mutex);

	intr_status = DDI_INTR_UNCLAIMED;

	if (adpu320_drain_start_queue(cfp) == DDI_INTR_CLAIMED) {
		intr_status = DDI_INTR_CLAIMED;
	}

	cfp->ab_flag &= ~ADPU320_RESTART;

	mutex_exit(&cfp->ab_mutex);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DTIME) {
		adpu320_printf("adpu320_run_callbacks: done\n");
	}
#endif

	return (intr_status);
}

void
adpu320_drain_completed_queue(
adpu320_config_t	*cfp
)
{
	HIM_IOB			*IOBp;
	register sp_t 		*scbp;
	struct adpu320_scsi_cmd	*cmd;
	HIM_UINT16		int_status;
	struct scsi_pkt		*pkt;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DTIME) {
		adpu320_printf("adpu320_drain_completed_queue%d: none found "
		    "in queue\n", INST(cfp));
	}
#endif

	/* We are already attempting to drain the queue, just leave */
	if (cfp->ab_flag & ADPU320_DRAINING)
		return;

	if (cfp->ab_scb_completed_queue.queue_head == NULL) {
#ifdef ADPU320_DEBUG
		if (adpu320_debug & DTIME) {
			adpu320_printf("adpu320_drain_completed_queue%d: none "
			    "found in queue\n", INST(cfp));
		}
#endif
		return;
	}

	/* We are currently attempting to drain the queue */
	cfp->ab_flag |= ADPU320_DRAINING;

	while (cfp->ab_scb_completed_queue.queue_head != NULL) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_drain_completed_queue%d: pkt=0x%x\n",
		    INST(cfp));
#endif
		scbp = (sp_t *)adpu320_remove_queue(
		    &cfp->ab_scb_completed_queue);

		cmd = scbp->Sp_cmdp;

		pkt = CMD2PKT(scbp->Sp_cmdp);

		IOBp = cmd->cmd_IOBp;

		int_status = IOBp->taskStatus;

		if (adpu320_chkstatus(pkt, IOBp) != TRUE) {
			cmn_err(CE_WARN, "invalid intr int_status=%0x02x)",
			    (uint_t)int_status);

			/* Invalid interrupt status */
		}

		ASSERT(pkt->pkt_comp);

		mutex_exit(&cfp->ab_mutex);

		scsi_hba_pkt_comp(pkt);

		mutex_enter(&cfp->ab_mutex);
	}

	/* Done draining the queue */
	cfp->ab_flag &= ~ADPU320_DRAINING;
}

uint_t
adpu320_drain_start_queue(
adpu320_config_t	*cfp
)
{
	HIM_IOB			*IOBp;
	register sp_t 		*scbp;
	struct adpu320_scsi_cmd	*cmd;
	uint_t			intr_status;
#ifdef ADPU320_DEBUG
	int			loca;
#endif

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DTIME) {
		adpu320_printf("adpu320_drain_start_queue%d:\n", INST(cfp));
	}
#endif

	intr_status = DDI_INTR_UNCLAIMED;

	while (cfp->ab_scb_start_queue.queue_head != NULL) {
		if (cfp->ab_flag &
		    (ADPU320_QUEUE_FROZEN | ADPU320_PAC_ACTIVE)) {
#ifdef ADPU320_DEBUG2
			adpu320_printf("adpu320_drain_start_queue%d: %s - %s\n",
			    INST(cfp),
			    (cfp->ab_flag & ADPU320_QUEUE_FROZEN)?
			    "ADPU320_QUEUE_FROZEN":"",
			    (cfp->ab_flag & ADPU320_PAC_ACTIVE)?
			    "ADPU320_PAC_ACTIVE":"");
#endif
			break;
		}

		scbp = (sp_t *)adpu320_remove_queue(&cfp->ab_scb_start_queue);

		cmd = scbp->Sp_cmdp;

#ifdef ADPU320_DEBUG
		loca = (cmd->target) + (cmd->lun * MAX_TARGETS);

		cfp->ha_stats[loca].iobs_queued--;
#endif

		IOBp = cmd->cmd_IOBp;

#ifdef ADPU320_DEBUG2
		adpu320_printf("adpu320_drain_start_queue%d: sending scbp "
		    "0x%x IOBp 0x%x\n", INST(cfp), scbp, IOBp);
#endif
		adpu320_queue_IOB(IOBp);

		intr_status = DDI_INTR_CLAIMED;
	}

	return (intr_status);
}

void
adpu320_restart_queues(
adpu320_config_t	*cfp
)
{
#ifdef ADPU320_DEBUG2
	adpu320_printf("adpu320_restart_queues%d:\n", INST(cfp));
#endif

	if (cfp->ab_flag & ADPU320_RESTART) {
		return;
	}

	ddi_trigger_softintr(cfp->ab_softid);

	cfp->ab_flag |= ADPU320_RESTART;
}

static int
adpu320_constructor(struct scsi_pkt *pkt, scsi_hba_tran_t *tranp, int kmflag)
{
	struct adpu320_scsi_cmd *cmd = pkt->pkt_ha_private;
	sp_t			*sp;
	size_t			real_length;
	ddi_dma_cookie_t	adpu320_dmacookie;
	uint_t			count;
	adpu320_config_t	*cfp;
	int (*callback) (caddr_t);

	cfp = ADDR2CFP(&pkt->pkt_address);

	if (kmflag == KM_SLEEP)
		callback = DDI_DMA_SLEEP;
	else
		callback = DDI_DMA_DONTWAIT;

	if (ddi_dma_alloc_handle(
	    tranp->tran_hba_dip,
	    &adpu320_contig_dmalim,
	    callback,
	    NULL,
	    &cmd->sp_dmahandle) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "adpu320_constructor: dma handle alloc fail");
		return (-1);
	}

	if (ddi_dma_mem_alloc(
	    cmd->sp_dmahandle,
	    sizeof (sp_t),
	    &adpu320_attr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    callback,
	    NULL,
	    (caddr_t *)&sp,
	    &real_length,
	    &cmd->sp_acchandle) == DDI_FAILURE) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_constructor%d: scb alloc fail\n",
		    INST(cfp));
#endif
		ddi_dma_free_handle(&cmd->sp_dmahandle);

		return (-1);
	}

	if (ddi_dma_addr_bind_handle(cmd->sp_dmahandle,
	    NULL, (caddr_t)sp, sizeof (sp_t),
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT, callback,
	    NULL, &adpu320_dmacookie, &count) != DDI_SUCCESS) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_constructor%d: dma addr bind failed\n",
		    INST(cfp));
#endif
		/*
		 * When we call ddi_dma_mem_free(), it will free up the memory
		 * accociated with the data access handle and the data access
		 * handle itself.
		 */
		ddi_dma_mem_free(&cmd->sp_acchandle);

		ddi_dma_free_handle(&cmd->sp_dmahandle);

		return (-1);
	}
	bzero(sp, sizeof (sp_t));
	cmd->cmd_private = sp;
#if OSM_BUS_ADDRESS_SIZE == 64
	sp->Sp_paddr	= adpu320_dmacookie.dmac_laddress;
#else
	sp->Sp_paddr	= adpu320_dmacookie.dmac_address;
#endif

	/*
	 * auto request sense data physical address
	 */
	sp->Sp_SensePtr = (sp->Sp_paddr +
	    ((caddr_t)(&sp->Sp_sense.sts_sensedata) - (caddr_t)sp));

	/*
	 * physical address of scatter gather list
	 */
	sp->SP_SegPtr = sp->Sp_paddr +
	    ((uintptr_t)sp->Sp_sg_list - (uintptr_t)sp);
	return (0);

}

static void
adpu320_destructor(struct scsi_pkt *pkt, scsi_hba_tran_t *tranp)
{
	struct adpu320_scsi_cmd *cmd = pkt->pkt_ha_private;

	ddi_dma_unbind_handle(cmd->sp_dmahandle);
	ddi_dma_mem_free(&cmd->sp_acchandle);
	ddi_dma_free_handle(&cmd->sp_dmahandle);
	cmd->sp_dmahandle = NULL;
}

/* CSTYLED */
STATIC void
adpu320_tran_teardown_pkt(
struct scsi_pkt	*pkt
)
{
	struct scsi_address		*ap = &pkt->pkt_address;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("destroy_pkt %x\n", pkt);
	}
#endif

	adpu320_pktfree(ap, pkt);
}

#ifdef STATIC_IOBS
/*
 * What we need to do is allocate static IOBs for the specialIOB case.
 * Normally, all the packets/cmds/IOBs are allocated in the context
 * adpu320_tran_init_pkt, but in the case of special IOBs, they can
 * be allocated during interrupt context.
 */
#define	NUM_IOBS	2

int
adpu320_alloc_static_IOBs(
{
	unsigned	i;

	for (i = 0; i < NUM_IOBS; i++)
	{
		adpu320_allocIOB(cfp, NULL);
	}
}
#endif


/*
 * Function : adpu320_allocIOB()
 * Dynamically allocate IOB and init IOB
 * OK
 */
/* CSTYLED */
STATIC HIM_IOB *
adpu320_allocIOB(
register adpu320_config_t	*cfp,
struct adpu320_scsi_cmd		*cmd
)
{
	register HIM_IOB	*IOBp;
	uchar_t			*iob_reserve;
	HIM_UINT32		iob_reserve_size;
	caddr_t			buf2;
	uint_t			count;
	size_t			real_length;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_allocIOB%d:\n", INST(cfp));
	}
#endif


	/* RST - we cannot sleep at interrupt time */
	/*
	 * Allocate memory buffer for IOB
	 */
	IOBp = (HIM_IOB *)kmem_zalloc(sizeof (HIM_IOB) + sizeof (HIM_TS_SCSI),
	    KM_SLEEP);

	IOBp->transportSpecific = (uchar_t *)IOBp + sizeof (HIM_IOB);

	/*
	 * Allocate DMA buffer for iobReserve inside the IOB struct
	 */
	iob_reserve_size = cfp->ha_profile->AP_IOBReserveSize;

	if (ddi_dma_alloc_handle(cfp->ab_dip,
	    &adpu320_contig_dmalim,
	    DDI_DMA_SLEEP,
	    NULL,
	    &cmd->rsv_dmahandle) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "adpu320_allocIOB : dma handle alloc fail");
		return ((HIM_IOB*)0);
	}

	if (ddi_dma_mem_alloc(cmd->rsv_dmahandle,
	    iob_reserve_size,
	    &adpu320_attr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP,
	    NULL,
	    &buf2,
	    &real_length,
	    &cmd->rsv_handle) == DDI_FAILURE) {
		cmn_err(CE_WARN, "adpu320_allocIOB : iobReserve alloc fail");
		ddi_dma_free_handle(&cmd->rsv_dmahandle);
		return ((HIM_IOB*)0);
	}

	if (ddi_dma_addr_bind_handle(cmd->rsv_dmahandle,
	    NULL,
	    (caddr_t)buf2,
	    iob_reserve_size,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP,
	    NULL,
	    &cmd->rsv_dmacookie,
	    &count) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "adpu320_allocIOB : iobReserve dma addr bind "
		    "fail");
		ddi_dma_mem_free(&cmd->rsv_handle);
		ddi_dma_free_handle(&cmd->rsv_dmahandle);
		return ((HIM_IOB*)0);
	}

	bzero((caddr_t)buf2, iob_reserve_size);
	iob_reserve = (uchar_t *)buf2;

	/*
	 * initialize the IOB allocated and pointed by IOBp
	 */

	IOBp->iobReserve.virtualAddress = iob_reserve;
#if OSM_BUS_ADDRESS_SIZE == 64
	IOBp->iobReserve.busAddress	= (cmd->rsv_dmacookie.dmac_laddress);
#else
	IOBp->iobReserve.busAddress	= (cmd->rsv_dmacookie.dmac_address);
#endif
	IOBp->iobReserve.bufferSize	= iob_reserve_size;

	IOBp->priority = 0;
	IOBp->flagsIob.disableDma = 0;
	IOBp->flagsIob.disableNotification = 0;
	IOBp->flagsIob.freezeOnError = 0;
	IOBp->relatedIob = NULL;

	IOBp->osRequestBlock	= (void *) cmd;
	cmd->cmd_IOBp = IOBp;

	/*
	 * Set up the cmd_cfp pointer.  This is important because there
	 * is no "sp_t" associated with this "adpu320_scsi_cmd".
	 * for special IOB's.  The only way that we can get back to the
	 * "adpu320_config_t" from the special IOB is to store it away in
	 * the "cmd".
	 */
	cmd->cmd_cfp = cfp;


#ifdef ADPU320_DEBUG
	if (adpu320_debug & DVERBOS) {
		adpu320_printf("adpu320_allocIOB%d: IOBp = %x\n",
		    INST(cfp), IOBp);
	}
#endif
	return (IOBp);
}

/*
 * Function : adpu320_pollret()
 *
 * Description :
 * 	adpu320_pollret is always called with the mutex
 * 	held PH_IntHandler is used to poll for
 *  completion because it transfers control to
 *	the HIM SCSI state machine
 *
 *  polled packets are single threaded by control
 *  of the mutex and are not allowed to disconnect
 *
 * OK
 */
/* CSTYLED */
STATIC void
adpu320_pollret(
register adpu320_config_t	*cfp,
register struct scsi_pkt	*poll_pktp
)
{
	register sp_t				*scbp;
	register sp_t				*poll_scbp;
	register HIM_IOB			*IOBp;
	register HIM_IOB			*poll_IOBp;
	register struct adpu320_scsi_cmd	*cmd;
	register struct adpu320_scsi_cmd	*abort_cmd;
	register struct adpu320_scsi_cmd	*poll_cmd;
	int					poll_done;
	int					i;
	int					j;
#ifdef ADPU320_DEBUG
	int					loca;
#endif

	poll_done = FALSE;

	poll_cmd = PKT2CMD(poll_pktp);

	poll_scbp = (sp_t *)poll_cmd->cmd_private;

	poll_IOBp = poll_cmd->cmd_IOBp;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DPKT) {
		adpu320_printf("adpu320_pollret%d: IOBp %x scbp %x pktp %x "
		    "cmd_cflags 0%x\n",
		    INST(cfp), poll_IOBp, poll_scbp, poll_pktp,
		    poll_cmd->cmd_cflags);
	}
#endif

#if 0
	dump_qout("before poll", cfp->ha_adapterTSH);
#endif

	for (i = 500; i > 0; ) {
		/*
		 * If possible, we should submit any queued requests
		 * at this point.  We may have gotten a poll request
		 * during a 3rd party bus reset.  If the request was
		 * queued because it couldn't be sent, we will need
		 * to send it as soon as we can.
		 */
		while (cfp->ab_scb_start_queue.queue_head != NULL) {
			if (cfp->ab_flag & (ADPU320_QUEUE_FROZEN |
			    ADPU320_PAC_ACTIVE)) {
				break;
			}

			scbp = (sp_t *)adpu320_remove_queue(
			    &cfp->ab_scb_start_queue);

			cmd = scbp->Sp_cmdp;

#ifdef ADPU320_DEBUG
			loca = (cmd->target) + (cmd->lun * MAX_TARGETS);

			cfp->ha_stats[loca].iobs_queued--;
#endif

			IOBp = cmd->cmd_IOBp;

#ifdef ADPU320_DEBUG
			adpu320_printf("adpu320_pollret%d: sending "
			    "scbp 0x%x IOBp 0x%x\n",
			    INST(cfp), scbp, IOBp);
#endif
			adpu320_queue_IOB(IOBp);
		}

		for (j = 0; j < 1000; j++) {
			drv_usecwait(10);
		}

		adpu320_poll_request(cfp);

#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_pollret%d:[%x] PH_IntHandler "
		    "IOBp %x ", INST(cfp), i, poll_IOBp);
#endif
#if 0
		dump_qout("during poll", cfp->ha_adapterTSH);
#endif

		if (poll_cmd->cmd_cflags & CFLAG_FINISHED) {
#ifdef ADPU320_DEBUG
			adpu320_printf("Finished!\n");
#endif

			poll_done = TRUE;

			break;
		}

		i--;
	}

#if 0
	dump_qout("after poll", cfp->ha_adapterTSH);
#endif

	if (poll_done == TRUE) {
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_pollret%d: poll_done: pktp %x "
		    "scbp %x\n", INST(cfp), poll_pktp, poll_scbp);
#endif

		adpu320_chkstatus(poll_pktp, poll_IOBp);

		/*
		 * We do not have to call scsi_hba_pkt_comp(pkt) if we are
		 * polling. We are done.
		 */
		return;
	}

#ifdef ADPU320_DEBUG
	debug_enter("adpu320_pollret timeout");
#endif

	abort_cmd = adpu320_IobSpecial(cfp, poll_IOBp, HIM_ABORT_TASK,
	    0, CFLAG_POLL);

	if (adpu320_wait_special_iob_complete(abort_cmd) == 0) {
		abort_cmd = adpu320_IobSpecial(cfp,
		    poll_IOBp,
		    HIM_RESET_BUS_OR_TARGET,
		    ADPU320_RESET_BUS, CFLAG_POLL);
		if (adpu320_wait_special_iob_complete(abort_cmd) == 0) {
			cmn_err(CE_WARN, "adpu320 %d: bus reset failed",
			    INST(cfp));
		}
	}

	/*
	 * We do not have to worry about completing any commands
	 * at this point, because this routine is called at interrupt
	 * time.  We do not need to call adpu320_drain_completed_queue.
	 */

	poll_pktp->pkt_reason = CMD_INCOMPLETE;

	poll_IOBp->data.busAddress = 0;

	poll_IOBp->data.bufferSize = 0;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DPKT) {
		adpu320_printf("adpu320_pollret%d: pollret timeout "
		    "pkt %x scb %x", INST(cfp), poll_pktp,
		    poll_scbp);
	}
#endif
}

/*
 * Function : adpu320_chkstatus()
 *
 * OK
 */
/*ARGSUSED*/
/* CSTYLED */
STATIC int
adpu320_chkstatus(
register struct scsi_pkt	*pktp,
register HIM_IOB		*IOBp
)
{
	struct scsi_arq_status	*arqp;
	sp_t			*scbp;
	struct adpu320_scsi_cmd	*cmd;
	adpu320_config_t	*cfp;
	UBYTE			target;

	cmd = PKT2CMD(pktp);

	cfp = PKT2CFP(pktp);

	scbp = (sp_t *)cmd->cmd_private;

	pktp->pkt_state = 0;

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DIOSTS) {
		adpu320_printf("adpu320_chkstatus%d: cmd=%x,t=%x,l=%x,"
		    "resid=%x\n",
		    INST(cfp),
		    (*(pktp->pkt_cdbp)), cmd->target, cmd->lun,
		    pktp->pkt_resid);

		adpu320_printf(" io_status = %x\n", IOBp->taskStatus);
	}
#endif

	/*
	 * Errors may occur, check taskStatus and handle error here
	 */
#ifdef ADPU320_DEBUG
	if (adpu320_debug & DIOERR) {
		adpu320_printf("adpu320_chkstatus%d: IOBp->taskStatus = %x\n",
		    INST(cfp), IOBp->taskStatus);
	}
#endif

	switch (IOBp->taskStatus) {
	/*
	 * ----------------------------------------------------
	 * Normal cases and errors reported by a target (8.9.1)
	 * ----------------------------------------------------
	 */
	case HIM_IOB_GOOD:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_CMPLT;

		pktp->pkt_resid = 0;

		pktp->pkt_state = (STATE_XFERRED_DATA|STATE_GOT_BUS|
		    STATE_GOT_TARGET|STATE_SENT_CMD|STATE_GOT_STATUS);
		break;

	case HIM_IOB_ERRORDATA_VALID:
		pktp->pkt_reason = CMD_CMPLT;

		pktp->pkt_state |= (STATE_GOT_BUS|STATE_GOT_TARGET|
		    STATE_SENT_CMD|STATE_GOT_STATUS|STATE_ARQ_DONE);

		arqp = (struct scsi_arq_status *)pktp->pkt_scbp;

		arqp->sts_status.sts_chk = 1;

		arqp->sts_rqpkt_reason = CMD_CMPLT;

		*(pktp->pkt_scbp) = 2; /* Check condition */

		bzero(&arqp->sts_rqpkt_status, sizeof (struct scsi_status));

#ifdef ADPU320_DEBUG
		if (adpu320_debug & DVERBOS) {
			uchar_t *b;
			int i;

			b = (uchar_t *)(&arqp->sts_sensedata);

			adpu320_printf("\n");

			for (i = 0; i < ADPU320_SENSE_LEN; i++) {
				adpu320_printf(" %x", *(b));
				b++;
			}

			adpu320_printf("\n");
		}
#endif
		arqp->sts_rqpkt_resid = 0;

		arqp->sts_rqpkt_statistics = 0;

		arqp->sts_rqpkt_state |=
		    (STATE_XFERRED_DATA|STATE_GOT_BUS|STATE_GOT_TARGET|
		    STATE_SENT_CMD|STATE_GOT_STATUS);

		break;

	case HIM_IOB_ERRORDATA_REQUIRED:
		*pktp->pkt_scbp = STATUS_CHECK;

		pktp->pkt_reason = CMD_TRAN_ERR;

		pktp->pkt_resid = 0;

		pktp->pkt_state |= (STATE_GOT_BUS|STATE_GOT_TARGET|
		    STATE_SENT_CMD|STATE_XFERRED_DATA|STATE_GOT_STATUS);

		break;

	case HIM_IOB_ERRORDATA_OVERUNDERRUN:
		/*
		 * RST - Look at this more
		 */
		arqp = (struct scsi_arq_status *)pktp->pkt_scbp;

		arqp->sts_status.sts_chk = 1;

		if (IOBp->residualError == 0) {
			arqp->sts_rqpkt_reason = CMD_DATA_OVR;

			arqp->sts_rqpkt_resid = 0;

			arqp->sts_rqpkt_state |= STATE_XFERRED_DATA;
		}
		else
		{
			arqp->sts_rqpkt_reason = CMD_CMPLT;

			arqp->sts_rqpkt_resid = IOBp->residualError;

			arqp->sts_rqpkt_state |= STATE_XFERRED_DATA;
		}

		pktp->pkt_reason = CMD_CMPLT;

		bzero(&arqp->sts_rqpkt_status, sizeof (struct scsi_status));

		pktp->pkt_state = (STATE_XFERRED_DATA|STATE_GOT_BUS|
		    STATE_GOT_TARGET|STATE_SENT_CMD|STATE_GOT_STATUS);

		break;

	case HIM_IOB_ERRORDATA_FAILED:
		pktp->pkt_reason = CMD_CMPLT;

		pktp->pkt_state |= (STATE_GOT_BUS|STATE_GOT_TARGET|
		    STATE_SENT_CMD|STATE_GOT_STATUS|STATE_ARQ_DONE);

		arqp = (struct scsi_arq_status *)pktp->pkt_scbp;

		arqp->sts_status.sts_chk = 1;

		bzero(&arqp->sts_rqpkt_status, sizeof (struct scsi_status));

		arqp->sts_rqpkt_reason = CMD_CMPLT;

		arqp->sts_rqpkt_state = STATE_GOT_BUS|STATE_GOT_TARGET|
		    STATE_SENT_CMD|STATE_GOT_STATUS;

		arqp->sts_rqpkt_resid = IOBp->errorDataLength;

		arqp->sts_rqpkt_statistics = 0;

#ifdef ADPU320_DEBUG
		adpu320_printf("HIM_IOB_ERRORDATA_FAILED\n");
#endif
		break;

	case HIM_IOB_XCA_ERRORDATA_VALID:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_statistics = 0;

		pktp->pkt_reason = CMD_TRAN_ERR;

		pktp->pkt_state |=
		    (STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD);

		break;

	case HIM_IOB_XCA_ERRORDATA_REQUIRED:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_statistics = 0;

		pktp->pkt_reason = CMD_TRAN_ERR;

		pktp->pkt_state |=
		    (STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD);

		break;

	case HIM_IOB_XCA_ERRORDATA_OVERUNDERRUN:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_statistics = 0;

		pktp->pkt_reason = CMD_TRAN_ERR;

		pktp->pkt_state |=
		    (STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD);

		break;

	case HIM_IOB_XCA_ERRORDATA_FAILED:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_statistics = 0;

		pktp->pkt_reason = CMD_TRAN_ERR;

		pktp->pkt_state |=
		    (STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD);

		break;

	/*
	 * ----------------------------------------------------
	 * IOB Errors and support issues (8.9.2)
	 * ----------------------------------------------------
	 */
	case HIM_IOB_INVALID:
		cmn_err(CE_PANIC, "adpu320: invalid IOB\n");

		return (FALSE);

	case HIM_IOB_UNSUPPORTED:
		cmn_err(CE_PANIC, "adpu320: IOB unsupported\n");

		break;

	case HIM_IOB_TSH_INVALID:
		cmn_err(CE_PANIC, "adpu320: invalid TSH\n");

		return (FALSE);

	case HIM_IOB_TSH_NOT_VALIDATED:
		cmn_err(CE_PANIC, "adpu320: TSH not validated\n");

		return (FALSE);

	case HIM_IOB_ADAPTER_NOT_IDLE:
		cmn_err(CE_PANIC, "adpu320: adapter not idle\n");

		return (FALSE);

	case HIM_IOB_NO_XCA_PENDING:
		cmn_err(CE_PANIC, "adpu320: no XCA pending\n");

		return (FALSE);

	case HIM_IOB_QUEUE_SUSPENDED:
		cmn_err(CE_PANIC, "adpu320: queue suspended\n");

		return (FALSE);

	/*
	 * ----------------------------------------------------
	 * Transport errors detected by HIM (8.9.3)
	 * ----------------------------------------------------
	 */
	case HIM_IOB_NO_RESPONSE:
		/*
		 * The command never was sent out, it should return
		 * CMD_INCOMPLETE status instead of CMD_TIMEOUT
		 * The target driver would then know this is a
		 * selection timeout, not command timeout.
		 */
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_TIMEOUT;

		pktp->pkt_statistics |= STAT_TIMEOUT;

		pktp->pkt_state |= STATE_GOT_BUS;

		pktp->pkt_resid = 0;

#ifdef ADPU320_DEBUG
		if (adpu320_debug & DIOSTS) {
			adpu320_printf("adpu320_chkstatus%d: selection timeout",
			    INST(cfp));
		}

#if 0
		debug_enter("selection timeout");
#endif
#endif

		break;

	case HIM_IOB_PROTOCOL_ERROR:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_TRAN_ERR;

		pktp->pkt_state |=
		    (STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD);

		break;

	case HIM_IOB_CONNECTION_FAILED:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_UNX_BUS_FREE;

		pktp->pkt_statistics = 0;

		pktp->pkt_state |= (STATE_GOT_BUS|STATE_GOT_TARGET);

		break;

	case HIM_IOB_PARITY_ERROR:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_TRAN_ERR;

		pktp->pkt_statistics = STAT_PERR;

		pktp->pkt_state |=
		    (STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD);
		break;

	case HIM_IOB_DATA_OVERUNDERRUN:
		*pktp->pkt_scbp = STATUS_GOOD;

		if (IOBp->residual == 0) {
			target = ((struct adpu320_scsi_cmd *)
			    IOBp->osRequestBlock)->target;
			if (cfp->cmd_retry[target] < 10) {
			pktp->pkt_reason = CMD_CMPLT;
			cfp->cmd_retry[target]++;
			pktp->pkt_resid = 1;
			} else {
			pktp->pkt_reason = CMD_DATA_OVR;
			cfp->cmd_retry[target] = 0;
			pktp->pkt_resid = 0;
			cmn_err(CE_WARN, "adpu320: overrun");
			}
		}
		else
		{
			/*  underrun per Frits of scsi-steer	*/
			pktp->pkt_reason = CMD_CMPLT;

#ifdef ADPU320_CHIM_UNDERRUN
			pktp->pkt_resid = (IOBp->residual);
			cmn_err(CE_WARN, "adpu320: underrun");
#else
			pktp->pkt_resid = (IOBp->residual);
#endif

#ifdef ADPU320_DEBUG
			if (adpu320_debug & DSTUS) {
				adpu320_printf("underrun: resid %x\n",
				    pktp->pkt_resid);
			}
#endif
			/*
			 * It's OK for INQUIRY commands to come back short
			 */
			if ((pktp->pkt_cdbp[0] == SCMD_INQUIRY) ||
			    (pktp->pkt_cdbp[0] == SCMD_REQUEST_SENSE)) {
				pktp->pkt_resid = 0;
			}
		}

		pktp->pkt_state = (STATE_XFERRED_DATA|STATE_GOT_BUS|
		    STATE_GOT_TARGET|STATE_SENT_CMD|STATE_GOT_STATUS);

		break;

	/*
	 * ----------------------------------------------------
	 * Target activity unusual conditions (8.9.4)
	 * ----------------------------------------------------
	 */
	case HIM_IOB_BUSY:
		*pktp->pkt_scbp = STATUS_BUSY; /* Target Busy */

		pktp->pkt_reason = CMD_CMPLT;

		pktp->pkt_resid = 0;

		pktp->pkt_state = (STATE_GOT_BUS|STATE_GOT_TARGET|
		    STATE_SENT_CMD|STATE_GOT_STATUS);

		pktp->pkt_statistics = 0;

		adpu320_IobSpecial(cfp, IOBp, HIM_UNFREEZE_QUEUE,
		    0, CFLAG_POLL);

		break;

	case HIM_IOB_TASK_SET_FULL:
		*pktp->pkt_scbp = STATUS_QFULL;

		pktp->pkt_reason = CMD_CMPLT;

		pktp->pkt_resid = 0;

		pktp->pkt_state = (STATE_GOT_BUS|STATE_GOT_TARGET|
		    STATE_SENT_CMD|STATE_GOT_STATUS);

		adpu320_IobSpecial(cfp, IOBp, HIM_UNFREEZE_QUEUE,
		    0, CFLAG_POLL);

		break;

	case HIM_IOB_TARGET_RESERVED:
		*pktp->pkt_scbp = STATUS_RESERVATION_CONFLICT;

		pktp->pkt_reason = CMD_CMPLT;

		pktp->pkt_statistics = 0;

		pktp->pkt_state |= (STATE_GOT_BUS|STATE_GOT_TARGET|
		    STATE_SENT_CMD|STATE_GOT_STATUS);

		break;

	case HIM_IOB_PCI_OR_PCIX_ERROR:
		*pktp->pkt_scbp = STATUS_BUSY; /* Target Busy */

		pktp->pkt_reason = CMD_CMPLT;

		pktp->pkt_resid = 0;

		pktp->pkt_state = (STATE_GOT_BUS|STATE_GOT_TARGET|
		    STATE_SENT_CMD|STATE_GOT_STATUS);

		pktp->pkt_statistics = 0;
		pktp->pkt_reason = CMD_RESET;

		break;


	/*
	 * ----------------------------------------------------
	 * Abort statuses (8.9.5)
	 * ----------------------------------------------------
	 */
	case HIM_IOB_ABORTED_ON_REQUEST:
		*pktp->pkt_scbp = STATUS_TERMINATED;

		pktp->pkt_reason = CMD_ABORTED;

		pktp->pkt_statistics |= STAT_ABORTED;

		pktp->pkt_resid = 0;

		break;

	case HIM_IOB_ABORTED_REQ_BUS_RESET:
	case HIM_IOB_ABORTED_PCIX_SPLIT_ERROR:
	case HIM_IOB_ABORTED_PCI_ERROR:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_RESET;

		pktp->pkt_statistics |= STAT_BUS_RESET;

		pktp->pkt_resid = 0;

		break;

	case HIM_IOB_ABORTED_CHIM_RESET:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_RESET;

		pktp->pkt_statistics |= STAT_BUS_RESET;

		pktp->pkt_resid = 0;

		break;

	case HIM_IOB_ABORTED_3RD_PARTY_RESET:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_RESET;

		pktp->pkt_statistics |= STAT_BUS_RESET;

		pktp->pkt_resid = 0;

		break;

	case HIM_IOB_ABORTED_BY_CHIM:
		*pktp->pkt_scbp = STATUS_TERMINATED;

		pktp->pkt_reason = CMD_ABORTED;

		pktp->pkt_statistics |= STAT_ABORTED;

		pktp->pkt_resid = 0;

		break;

	case HIM_IOB_ABORTED_REQ_TARGET_RESET:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_RESET;

		pktp->pkt_statistics |= STAT_DEV_RESET;

		pktp->pkt_resid = 0;

		break;

	case HIM_IOB_ABORT_FAILED:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_ABORT_FAIL;

		pktp->pkt_statistics = 0;

		pktp->pkt_state |=
		    (STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD);

		break;

	case HIM_IOB_TERMINATED:
		*pktp->pkt_scbp = STATUS_TERMINATED;

		pktp->pkt_reason = CMD_ABORTED;

		pktp->pkt_statistics |= STAT_ABORTED;

		break;

	case HIM_IOB_ABORT_NOT_FOUND:
		cmn_err(CE_PANIC, "adpu320: abort not found\n");

		return (FALSE);

	case HIM_IOB_ABORT_STARTED:
		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_CMPLT;

		pktp->pkt_resid = 0;

		pktp->pkt_state = (STATE_XFERRED_DATA|STATE_GOT_BUS|
		    STATE_GOT_TARGET|STATE_SENT_CMD|STATE_GOT_STATUS);
		break;

	case HIM_IOB_ABORT_ALREADY_DONE:
		cmn_err(CE_PANIC, "adpu320: abort already done\n");

		return (FALSE);

	case HIM_IOB_ABORTED_TRANSPORT_MODE_CHANGE:
		*pktp->pkt_scbp = STATUS_TERMINATED;

		pktp->pkt_reason = CMD_ABORTED;

		pktp->pkt_statistics |= STAT_ABORTED;

		pktp->pkt_resid = 0;

		break;

	/*
	 * ----------------------------------------------------
	 * Misc (8.9.8)
	 * ----------------------------------------------------
	 *
	 * RST: case HIM_IOB_TRANSPORT_SPECIFIC:
	 *
	 * The CHIM spec talks about this TaskStatus,
	 * but it doesn't seem to exist in the CHIM.
	 */
	case HIM_IOB_HOST_ADAPTER_FAILURE:
		cmn_err(CE_WARN, "adpu320: host adapter hardware failure");

		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_TRAN_ERR;

		pktp->pkt_state |= STATE_GOT_BUS;

		break;

	case HIM_IOB_TARGET_RESET_FAILED:
		cmn_err(CE_WARN, "adpu320: target reset failed");

		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_TRAN_ERR;

		pktp->pkt_state |= STATE_GOT_BUS;

		return (FALSE);

	case HIM_IOB_TRANSPORT_SPECIFIC:
		cmn_err(CE_WARN, "adpu320: protocol specific error");

		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_reason = CMD_TRAN_ERR;

		pktp->pkt_state |= STATE_GOT_BUS;

		return (FALSE);

	/*
	 * Target-Mode abort statuses (8.9.6)
	 */
	case HIM_IOB_ABORTED_ABTS_RECVD:
	case HIM_IOB_ABORTED_ABT_RECVD:
	case HIM_IOB_ABORTED_TR_RECVD:
	case HIM_IOB_ABORTED_CTS_RECVD:
	case HIM_IOB_ABORTED_TT_RECVD:

	/*
	 * Target-Mode errors (8.9.7)
	 */
	case HIM_IOB_TARGETCOMMANDBUFFER_OVERRUN:
	case HIM_IOB_OUT_OF_ORDER_TRANSFER_REJECTED:
	case HIM_IOB_INITIATOR_DETECTED_PARITY_ERROR:
	case HIM_IOB_INITIATOR_DETECTED_ERROR:
	case HIM_IOB_INVALID_MESSAGE_REJECT:
	case HIM_IOB_INVALID_MESSAGE_RCVD:
	default:
		cmn_err(CE_WARN, "adpu320_chkstatus: Error IOB status = %u",
		    (uint_t)IOBp->taskStatus);

		*pktp->pkt_scbp = STATUS_GOOD;

		pktp->pkt_statistics = 0;

		pktp->pkt_reason = CMD_TRAN_ERR;

		return (FALSE);
	}

	return (TRUE);
}

/*
 * Function : adpu320_intr()
 *
 * OK
 */
/* CSTYLED */
STATIC uint_t
adpu320_intr(
caddr_t arg
)
{
	adpu320_config_t	*cfp;
	unsigned char		status;

	cfp = SOFT2CFP((adpu320_soft_state_t *)arg);

#ifdef ADPU320_DEBUG
	if (adpu320_debug & DPKTDMP) {
		adpu320_printf("\nadpu320_intr: ");
		adpu320_dump_config(cfp);
	}
#endif

	mutex_enter(&cfp->ab_mutex);

	status = cfp->ha_himFuncPtr->HIMFrontEndISR(
	    cfp->ha_adapterTSH);

	switch (status) {
	case HIM_LONG_INTERRUPT_PENDING:
	case HIM_INTERRUPT_PENDING:
		cfp->ha_himFuncPtr->HIMBackEndISR(
		    cfp->ha_adapterTSH);
		break;

	case HIM_NOTHING_PENDING:
		break;

	default:
#ifdef ADPU320_DEBUG
		adpu320_printf("adpu320_intr: illegal int_status=%d \n",
		    status);
#endif
		break;
	}

	adpu320_drain_completed_queue(cfp);

	mutex_exit(&cfp->ab_mutex);

	if ((status == HIM_INTERRUPT_PENDING) ||
	    (status == HIM_LONG_INTERRUPT_PENDING)) {
		return (DDI_INTR_CLAIMED);
	}

	if (cfp->ab_flag & ADPU320_POLLING) {
		return (DDI_INTR_CLAIMED);
	}

	return (DDI_INTR_UNCLAIMED);
}

#if defined(ADPU320_DEBUG) || defined(ADPU320_DEBUG2)
void
adpu320_dump_config(
adpu320_config_t *cfp
)
{
#if 0
	adpu320_printf("\n id %x dip 0x%x ioaddr %x int_idx %d ",
	    cfp->Cf_id.id_struct.AdapterId & 0xffff,
	    cfp->ab_dip,
	    cfp->ha_BaseAddress,
	    cfp->ab_intr_idx);
#else
	adpu320_printf("\n dip 0x%x int_idx %d ",
	    cfp->ab_dip,
	    cfp->ab_intr_idx);
#endif

	adpu320_printf(" child %d flg %x\n", cfp->ab_child, cfp->ab_flag);

	adpu320_printf("pkts_out %d\n", cfp->ab_pkts_out);

	adpu320_printf("\n");
}

void
adpu320_dump_scb(
char			*callstring,
adpu320_config_t	*cfp,
sp_t			*scbp
)
{
	HIM_IOB			*IOBp;
	struct adpu320_scsi_cmd	*cmd;
	struct scsi_pkt 	*pktp;

	cmd = scbp->Sp_cmdp;

	IOBp = cmd->cmd_IOBp;

	pktp = cmd->cmd_pkt;

	adpu320_printf("%s: Target %d Lun %d addr adpu320_scsi_cmd %x\n",
	    callstring, cmd->target, cmd->lun, scbp->Sp_cmdp);

	adpu320_printf("%s: cmd = 0x%x, IOBp = 0x%x, pktp = 0x%x\n",
	    callstring, cmd, IOBp, pktp);

	if (IOBp->flagsIob.autoSense) {
		adpu320_printf("%s: Auto Request Sense Enabled\n", callstring);
	}
	else
	{
		adpu320_printf("%s: No Auto Request Sense\n", callstring);
	}

#if OSM_BUS_ADDRESS_SIZE == 64
	adpu320_printf("%s: pktp (%x) dat_addr %llx dat_len %llx\n"
	    "resid=%x, Sp_sg_list = 0x%x, SegPtr=%llxx, SegCnt=%x "
	    "SensePtr %llx\n",
	    callstring,
	    pktp,
	    scbp->Sp_sg_list[0].data_addr,
	    scbp->Sp_sg_list[0].data_len,
	    pktp->pkt_resid,
	    scbp->Sp_sg_list,
	    scbp->SP_SegPtr, scbp->SP_SegCnt, scbp->Sp_SensePtr);

#else
	adpu320_printf("%s: pktp (%x) dat_addr %x dat_len %x\n"
	    "resid=%x, Sp_sg_list = 0x%x, SegPtr=%x, SegCnt=%x "
	    "SensePtr %x\n",
	    callstring,
	    pktp,
	    scbp->Sp_sg_list[0].data_addr,
	    scbp->Sp_sg_list[0].data_len,
	    pktp->pkt_resid,
	    scbp->Sp_sg_list,
	    scbp->SP_SegPtr, scbp->SP_SegCnt, scbp->Sp_SensePtr);
#endif
	adpu320_printf("%s: cmd [%x %x %x %x %x %x], cmd_len=%x, ",
	    callstring,
	    (*(pktp->pkt_cdbp+0)), (*(pktp->pkt_cdbp+1)),
	    (*(pktp->pkt_cdbp+2)), (*(pktp->pkt_cdbp+3)),
	    (*(pktp->pkt_cdbp+4)), (*(pktp->pkt_cdbp+5)),
	    IOBp->targetCommandLength);

	adpu320_printf("%s: Status of IOB %x ResCnt %x\n", callstring,
	    IOBp->taskStatus, IOBp->residualError);
}

void
adpu320_dump_profile(
HIM_ADAPTER_PROFILE	*targetProfile
)
{
	adpu320_printf("AP_SCSIForceWide 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIForceWide);
	adpu320_printf("AP_SCSIForceNoWide 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIForceNoWide);
	adpu320_printf("AP_SCSIForceSynch 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIForceSynch);
	adpu320_printf("AP_SCSIForceNoSynch 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIForceNoSynch);
	adpu320_printf("AP_SCSIAdapterID 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIAdapterID);
	adpu320_printf("AP_SCSISpeed 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSISpeed);
	adpu320_printf("AP_SCSIWidth 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIWidth);
	adpu320_printf("AP_SCSIDisableParityErrors 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIDisableParityErrors);
	adpu320_printf("AP_SCSISelectionTimeout 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSISelectionTimeout);
	adpu320_printf("AP_SCSITransceiverMode 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSITransceiverMode);
	adpu320_printf("AP_SCSIDomainValidationMethod 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIDomainValidationMethod);
	adpu320_printf("AP_SCSIPPRSupport 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIPPRSupport);
	adpu320_printf("AP_SCSIExpanderDetection 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIExpanderDetection);
	adpu320_printf("AP_SCSIHostTargetVersion 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIHostTargetVersion);
	adpu320_printf("AP_SCSI2_IdentifyMsgRsv 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSI2_IdentifyMsgRsv);
	adpu320_printf("AP_SCSI2_TargetRejectLuntar 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSI2_TargetRejectLuntar);
	adpu320_printf("AP_SCSIGroup6CDBSize 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIGroup6CDBSize);
	adpu320_printf("AP_SCSIGroup7CDBSize 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSIGroup7CDBSize);
	adpu320_printf("AP_SCSITargetIgnoreWideResidue 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSITargetIgnoreWideResidue);
	adpu320_printf("AP_SCSITargetEnableSCSI1Selection 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSITargetEnableSCSI1Selection);
	adpu320_printf("AP_SCSITargetInitNegotiation 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSITargetInitNegotiation);
	adpu320_printf("AP_SCSITargetMaxSpeed 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSITargetMaxSpeed);
	adpu320_printf("AP_SCSITargetDefaultSpeed 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSITargetDefaultSpeed);
	adpu320_printf("AP_SCSITargetMaxOffset 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSITargetMaxOffset);
	adpu320_printf("AP_SCSITargetDefaultOffset 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSITargetDefaultOffset);
	adpu320_printf("AP_SCSITargetMaxWidth 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSITargetMaxWidth);
	adpu320_printf("AP_SCSITargetDefaultWidth 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSITargetDefaultWidth);
	adpu320_printf("AP_SCSITargetAdapterIDMask 0x%x\n",
	    targetProfile->himu.TS_SCSI.AP_SCSITargetAdapterIDMask);
}

#endif /* ADPU320_DEBUG */


void
adpu320_append_element(
element_list_t	*listp,
element_t	*elementp
)
{
#if defined(ADPU320_DEBUG)
	adpu320_printf("adpu320_append_element: 0x%x virt_addr = 0x%x\n",
	    elementp, elementp->virt_addr);
#endif
	listp->last_element->next_element = elementp;

	listp->last_element = elementp;

}

HIM_BUS_ADDRESS
adpu320_search_element(
element_list_t	*listp,
uchar_t		*key_search
)
{
	HIM_BUS_ADDRESS	p_ptr;
	element_t	*elementp;

	elementp = listp->first_element;

	p_ptr = (HIM_BUS_ADDRESS)NULL;

	while (1) {
		if (key_search == elementp->virt_addr) {
			p_ptr = elementp->phys_addr;

			break;
		}

		if (elementp->next_element == NULL) {
			break;
		}
		else
		{
			elementp = elementp->next_element;
		}
	}

	return (p_ptr);
}

int
adpu320_delete_element(
element_list_t	*listp,
uchar_t		*key_search
)
{

	element_t	*prev_ptr;
	element_t	*elementp;
	int		deleted;

	elementp = listp->first_element->next_element;

	prev_ptr = listp->first_element;

	deleted = 1;

	for (;;) {
		if (key_search == elementp->virt_addr) {
			prev_ptr->next_element = elementp->next_element;

			if (listp->last_element == elementp) {
				listp->last_element = prev_ptr;
			}

#if defined(ADPU320_DEBUG)
			adpu320_printf("adpu320_delete_element: "
			    "elementp = 0x%p virt_addr 0x%p\n", elementp,
			    elementp->virt_addr);
#endif
			kmem_free(elementp, sizeof (element_t));

			deleted = 0;

			break;
		}

		if (elementp->next_element == NULL) {
			break;
		}
		else
		{
			prev_ptr = elementp;

			elementp = elementp->next_element;
		}
	}

	return (deleted);
}

void
adpu320_empty_queue(
adpu320_queue_t	*queuep
)
{
	queuep->queue_head = NULL;

	queuep->queue_tail = NULL;
}

adpu320_entry_t *
adpu320_remove_queue(
adpu320_queue_t	*queuep
)
{
	adpu320_entry_t	*entryp;

	if (queuep->queue_head == NULL) {
		return (NULL);
	}

	entryp = queuep->queue_head;

	if (queuep->queue_head == queuep->queue_tail) {
		queuep->queue_head = NULL;

		queuep->queue_tail = NULL;
	}
	else
	{
		queuep->queue_head = entryp->next;
	}

	return (entryp);
}

void
adpu320_add_queue(
adpu320_queue_t	*queuep,
adpu320_entry_t	*entryp
)
{
	if (queuep->queue_head == NULL) {
		queuep->queue_head = entryp;

		queuep->queue_tail = entryp;
	}
	else
	{
		queuep->queue_tail->next = entryp;

		queuep->queue_tail = entryp;
	}
}

#if defined(ADPU320_DEBUG) || defined(ADPU320_DEBUG2)
void
adpu320_printf(char *format, ...)
{
	va_list		ap;
	char		*s;

	if (adpu320_debug_enable == 0) {
		return;
	}

	adpu320_va_start(ap, format);

	if (adpu320_buffer_output == 0) {
		prom_vprintf(format, ap);

		adpu320_va_end(ap);

		return;
	}

	mutex_enter(&adpu320_print_mutex);

	vsprintf(adpu320_print_line, format, ap);

	adpu320_va_end(ap);

	for (s = &adpu320_print_line[0]; *s != '\0'; s++) {
		if (adpu320_print_buf_ptr == NULL) {
			adpu320_print_buf_ptr = &adpu320_print_buffer[0];
		} else if (adpu320_print_buf_ptr ==
		    &adpu320_print_buffer[PRINT_BUF_SIZE]) {
			adpu320_print_buf_ptr = &adpu320_print_buffer[0];
			adpu320_print_buf_wrap = 1;
		}

		*adpu320_print_buf_ptr = *s;

		adpu320_print_buf_ptr++;
	}

	mutex_exit(&adpu320_print_mutex);
}

void
adpu320_printhex(
unsigned char	*s,
unsigned	len
)
{
	unsigned	i;
	unsigned	count;
	unsigned	num;

	for (count = 0; count < len; count = count + num, s = s + num) {
		num = ((len - count) > 16) ? 16 : (len - count);

		adpu320_printf("0x%08x: ", s);

		for (i = 0; i < num; i++) {
			adpu320_printf("%02x ", *(s+i));
		}

		while (i != num) {
			adpu320_printf("   ");
			i++;
		}

		for (i = 0; i < num; i++) {
			if (ISPRINT(*(s+i))) {
				adpu320_printf("%c", *(s+i));
			}
			else
			{
				adpu320_printf(".");
			}
		}

		adpu320_printf("\n");
	}
}

void
adpu320_dump_buffer()
{
	int	line;

	line = 0;

	if (adpu320_print_buf_wrap) {
		line = adpu320_dump_chars(adpu320_print_buf_ptr,
		    &adpu320_print_buffer[PRINT_BUF_SIZE], 0);

		if (line == -1)
			return;
	}

	adpu320_dump_chars(&adpu320_print_buffer[0], adpu320_print_buf_ptr,
	    line);
}

int
adpu320_dump_chars(
char		*s1,
char		*s2,
int		line
)
{
	char	*s;
	char	c;
	int	num_lines;

	num_lines = NUM_LINES;

	for (s = s1; s != s2; s++) {
		prom_printf("%c", *s);

		if (*s != '\n') {
			continue;
		}

		line++;

		if (line % num_lines) {
			continue;
		}

		c = prom_getchar();

		prom_printf("\r");

		if ((c == ESC) || (c == 'q')) {
			prom_printf("\n");

			return (-1);
		}

		if (c == ' ') {
			num_lines = NUM_LINES;
		}
		else
		{
			num_lines = 1;
		}
	}

	return (line);
}

void
adpu320_verify_lock_held(
char		*s,
kmutex_t	*mutexp
)
{
	if (mutex_owned(mutexp) == 0) {
		debug_enter(s);
	}
}
#endif

/* CSTYLED */
STATIC void
L2_add(L2el_t *headp, L2el_t *elementp, void *private)
{

	ASSERT(headp != NULL && elementp != NULL);
	ASSERT(headp->l2_nextp != NULL);
	ASSERT(headp->l2_prevp != NULL);

	elementp->l2_private = private;

	elementp->l2_nextp = headp;
	elementp->l2_prevp = headp->l2_prevp;
	headp->l2_prevp->l2_nextp = elementp;
	headp->l2_prevp = elementp;
}

/* CSTYLED */
STATIC void
L2_delete(L2el_t *elementp)
{

	ASSERT(elementp != NULL);
	ASSERT(elementp->l2_nextp != NULL);
	ASSERT(elementp->l2_prevp != NULL);
	ASSERT(elementp->l2_nextp->l2_prevp == elementp);
	ASSERT(elementp->l2_prevp->l2_nextp == elementp);

	elementp->l2_prevp->l2_nextp = elementp->l2_nextp;
	elementp->l2_nextp->l2_prevp = elementp->l2_prevp;

	/* link it to itself in case someone does a double delete */
	elementp->l2_nextp = elementp;
	elementp->l2_prevp = elementp;
}


/* CSTYLED */
STATIC void
L2_add_head(L2el_t *headp, L2el_t *elementp, void *private)
{

	ASSERT(headp != NULL && elementp != NULL);
	ASSERT(headp->l2_nextp != NULL);
	ASSERT(headp->l2_prevp != NULL);

	elementp->l2_private = private;

	elementp->l2_prevp = headp;
	elementp->l2_nextp = headp->l2_nextp;
	headp->l2_nextp->l2_prevp = elementp;
	headp->l2_nextp = elementp;
}



/*
 * L2_remove()
 *
 *	Remove the entry from the head of the list (if any).
 *
 */

/* CSTYLED */
STATIC void *
L2_remove_head(L2el_t *headp)
{
	L2el_t *elementp;

	ASSERT(headp != NULL);

	if (L2_EMPTY(headp))
		return (NULL);

	elementp = headp->l2_nextp;

	headp->l2_nextp = elementp->l2_nextp;
	elementp->l2_nextp->l2_prevp = headp;

	/* link it to itself in case someone does a double delete */
	elementp->l2_nextp = elementp;
	elementp->l2_prevp = elementp;

	return (elementp->l2_private);
}

/* CSTYLED */
STATIC void *
L2_next(L2el_t *elementp)
{

	ASSERT(elementp != NULL);

	if (L2_EMPTY(elementp))
		return (NULL);
	return (elementp->l2_nextp->l2_private);
}

/* CSTYLED */
STATIC int
adpu320_tran_reset_notify(struct scsi_address *ap, int flag,
    void (*callback)(caddr_t), caddr_t arg)
{
	adpu320_config_t *cfp;
	adpu320_soft_state_t *softp;
	adpu320_reset_notify_entry_t *rnp;
	int rc = DDI_FAILURE;

	cfp = ap->a_hba_tran->tran_hba_private;
	softp = ap->a_hba_tran->tran_tgt_private;


	switch (flag) {

	case SCSI_RESET_NOTIFY:

		rnp = (adpu320_reset_notify_entry_t *)kmem_zalloc(sizeof (*rnp),
		    KM_SLEEP);

		rnp->adpu320_unitp = softp;
		rnp->callback = callback;
		rnp->arg = arg;

		mutex_enter(&cfp->adpu320_reset_notify_mutex);
		L2_add(&cfp->adpu320_reset_notify_list, &rnp->l2_link,
		    (void *)rnp);
		mutex_exit(&cfp->adpu320_reset_notify_mutex);

		rc = DDI_SUCCESS;

		break;

	case SCSI_RESET_CANCEL:

		mutex_enter(&cfp->adpu320_reset_notify_mutex);
		for (rnp = (adpu320_reset_notify_entry_t *)
		    L2_next(&cfp->adpu320_reset_notify_list);
		    rnp != NULL;
		    rnp = (adpu320_reset_notify_entry_t *)
		    L2_next(&rnp->l2_link)) {
			if (rnp->adpu320_unitp == softp &&
			    rnp->callback == callback &&
			    rnp->arg == arg) {
				L2_delete(&rnp->l2_link);
				kmem_free(rnp, sizeof (*rnp));
				rc = DDI_SUCCESS;
				break;
			}
		}
		mutex_exit(&cfp->adpu320_reset_notify_mutex);
		break;

	default:
		rc = DDI_FAILURE;
		break;
	}

	return (rc);
}

/* CSTYLED */
STATIC void
adpu320_do_reset_notify_callbacks(adpu320_config_t *cfp)
{
	adpu320_reset_notify_entry_t *rnp;
	L2el_t *rnl = &cfp->adpu320_reset_notify_list;

	/* lock the reset notify list while we operate on it */
	mutex_enter(&cfp->adpu320_reset_notify_mutex);

	for (rnp = (adpu320_reset_notify_entry_t *)L2_next(rnl);
	    rnp != NULL;
	    rnp = (adpu320_reset_notify_entry_t *)L2_next(&rnp->l2_link)) {

		(*rnp->callback)(rnp->arg);
	}
	mutex_exit(&cfp->adpu320_reset_notify_mutex);
}

/* CSTYLED */
STATIC int adpu320_ioctl(
dev_t dev,
int cmd,
intptr_t arg,
int mode,
cred_t *credp,
int *rvalp
)
{

	int			rval;
	int			instance;
	adpu320_soft_state_t	*softp;
	adpu320_config_t 	*cfp;
	/*
	 * DDI-2.5
	 */
	instance = MINOR2INST(getminor(dev));

	softp = (adpu320_soft_state_t *)
	    ddi_get_soft_state(adpu320_state, instance);

	if (softp == (adpu320_soft_state_t *)NULL) {
		return (ENXIO);
	}

	cfp = softp->cfp;

	if (cmd ==  DEVCTL_BUS_CONFIGURE) {
		mutex_enter(&cfp->ab_mutex);
		rval = adpu320_attach_PAC(cfp);
		mutex_exit(&cfp->ab_mutex);
		if (rval == ADPU320_FAILURE) {
			cmn_err(CE_NOTE,
			    "adpu320_ioctl: Protocol Auto Configuration "
			    "failed!");
			return (EIO);
		}
	}

	return (scsi_hba_ioctl(dev, cmd, arg, mode, credp, rvalp));
}

/*
 * adpu320_add_intrs:
 *
 * Register FIXED or MSI interrupts.
 */
static int
adpu320_add_intrs(adpu320_soft_state_t *softp, int intr_type)
{
	adpu320_config_t *cfp = softp->cfp;
	dev_info_t	*dip = cfp->ab_dip;
	int		avail, actual, intr_size;
	int		i, j, ret;

	/* Get number of available interrupts */
	ret = ddi_intr_get_navail(dip, intr_type, &avail);
	if ((ret != DDI_SUCCESS) || (avail == 0)) {
		cmn_err(CE_WARN, "!ddi_intr_get_navail() failed,"
		    "ret %d avail %d", ret, avail);
		return (DDI_FAILURE);
	}

	/* Register one MSI for IO */
	if (intr_type == DDI_INTR_TYPE_MSI && avail > 1) {
		avail = 1;
	}

	/* Allocate an array of interrupt handlers */
	intr_size = avail * sizeof (ddi_intr_handle_t);
	cfp->ab_htable = kmem_alloc(intr_size, KM_SLEEP);

	/* call ddi_intr_alloc */
	ret = ddi_intr_alloc(dip, cfp->ab_htable, intr_type, 0,
	    avail, &actual, DDI_INTR_ALLOC_NORMAL);

	if ((ret != DDI_SUCCESS) || (actual == 0)) {
		cmn_err(CE_WARN, "!ddi_intr_alloc() failed, ret %d", ret);
		kmem_free(cfp->ab_htable, intr_size);
		return (DDI_FAILURE);
	}

#ifdef ADPU320_DEBUG
	if (actual < avail) {
		cmn_err(CE_NOTE, "!Requested: %d, Received: %d", avail, actual);
	}
#endif

	cfp->ab_intr_cnt = actual;

	/* Get priority for first msi, assume remaining are all the same */
	if ((ret = ddi_intr_get_pri(cfp->ab_htable[0], &cfp->ab_intr_pri))
	    != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!ddi_intr_get_pri() failed %d", ret);

		/* Free already allocated intr */
		for (i = 0; i < actual; i++) {
			(void) ddi_intr_free(cfp->ab_htable[i]);
		}

		kmem_free(cfp->ab_htable, intr_size);
		return (DDI_FAILURE);
	}

	/* Test for high level mutex */
	if (cfp->ab_intr_pri >= ddi_intr_get_hilevel_pri()) {
		cmn_err(CE_WARN, "!adpu320_add_intrs: Hi level interrupt not "
		    "supported");

		/* Free already allocated intr */
		for (i = 0; i < actual; i++) {
			(void) ddi_intr_free(cfp->ab_htable[i]);
		}

		kmem_free(cfp->ab_htable, intr_size);
		return (DDI_FAILURE);
	}

	/* Call ddi_intr_add_handler */
	for (i = 0; i < actual; i++) {
		if ((ret = ddi_intr_add_handler(cfp->ab_htable[i],
		    (ddi_intr_handler_t *)adpu320_intr, (caddr_t)softp,
		    (caddr_t)NULL)) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "!ddi_intr_add_handler failed %d",
			    ret);

			/* Remove already added handlers */
			for (j = 0; j < i; j++) {
				(void) ddi_intr_remove_handler(
				    cfp->ab_htable[j]);
			}

			/* Free already allocated intr */
			for (i = 0; i < actual; i++) {
				(void) ddi_intr_free(cfp->ab_htable[i]);
			}

			kmem_free(cfp->ab_htable, intr_size);
			return (DDI_FAILURE);
		}
	}

	if ((ret = ddi_intr_get_cap(cfp->ab_htable[0], &cfp->ab_intr_cap))
	    != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!ddi_intr_get_cap() failed %d", ret);

		/* Remove and free already allocated intr */
		for (i = 0; i < actual; i++) {
			(void) ddi_intr_remove_handler(cfp->ab_htable[j]);
			(void) ddi_intr_free(cfp->ab_htable[i]);
		}

		kmem_free(cfp->ab_htable, intr_size);
		return (DDI_FAILURE);
	}

	/* Enable interrupts */
	if (cfp->ab_intr_cap & DDI_INTR_FLAG_BLOCK) {
		/* Call ddi_intr_block_enable() for MSI interrupts */
		(void) ddi_intr_block_enable(cfp->ab_htable, cfp->ab_intr_cnt);
	} else {
		/* Call ddi_intr enable for MSI or FIXED interrupts */
		for (i = 0; i < cfp->ab_intr_cnt; i++) {
			(void) ddi_intr_enable(cfp->ab_htable[i]);
		}
	}

	return (DDI_SUCCESS);
}

/*
 * adpu320_rem_intrs:
 *
 * Unregister FIXED or MSI interrupts
 */
static void
adpu320_rem_intrs(adpu320_soft_state_t *softp)
{
	int	i;
	adpu320_config_t	*cfp = softp->cfp;

	/* Disable all interrupts */
	if (cfp->ab_intr_cap & DDI_INTR_FLAG_BLOCK) {
		/* Call ddi_intr_block_disable */
		(void) ddi_intr_block_disable(cfp->ab_htable, cfp->ab_intr_cnt);
	} else {
		for (i = 0; i < cfp->ab_intr_cnt; i++) {
			(void) ddi_intr_disable(cfp->ab_htable[i]);
		}
	}

	/* Call ddi_intr_remove_handler() */
	for (i = 0; i < cfp->ab_intr_cnt; i++) {
		(void) ddi_intr_remove_handler(cfp->ab_htable[i]);
		(void) ddi_intr_free(cfp->ab_htable[i]);
	}

	kmem_free(cfp->ab_htable, cfp->ab_intr_cnt *
	    sizeof (ddi_intr_handle_t));
}

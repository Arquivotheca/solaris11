/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * isp - Emulex/QLogic Intelligent SCSI Processor driver for
 *	ISP 1000 and 1040A
 */

#if defined(lint) && !defined(DEBUG)
#define	DEBUG	1
#endif

#ifdef DEBUG
#define	ISPDEBUG
#define	ISPDEBUG_IOCTL
static int ispdebug = 0;
static int isp_enable_brk_fatal = 0;
static int isp_watch_disable = 0; /* Disable watchdog for debug */
#include <sys/debug.h>
#endif	/* DEBUG */

static int isp_debug_timeout = 0;
static int isp_debug_state = 0;
static int isp_debug_mbox = 0;
static int isp_debug_ars = 0;
static int isp_debug_reset_sent = 0;

#ifdef	ISPDEBUG
static int isp_debug_renegotiate = 0;		/* renegotiate on next INQ */
#endif

#include <sys/note.h>
#include <sys/modctl.h>
#include <sys/pci.h>
#include <sys/scsi/scsi.h>
#include <sys/time.h>

#include <sys/scsi/adapters/ispmail.h>
#include <sys/scsi/adapters/ispvar.h>
#include <sys/scsi/adapters/ispreg.h>
#include <sys/scsi/adapters/ispcmd.h>

#include <sys/scsi/impl/scsi_reset_notify.h>

#include <sys/varargs.h>

/*
 * NON-ddi compliant include files
 */
#include <sys/kmem.h>
#include <sys/vtrace.h>
#include <sys/kstat.h>

#define	SEC_TO_USEC(A) ((A) * 1000000)
#define	SEC_TO_NSEC(A) ((hrtime_t)(A) * 1000000000LL)

/*
 * for debugging request queue size (is 190 right?)
 */
static int	isp_max_requests = ISP_MAX_REQUESTS;
static int	isp_max_responses = ISP_MAX_RESPONSES;

/*
 * the values of the following variables are used to initialize
 * the cache line size and latency timer registers in the PCI
 * configuration space.  variables are used instead of constants
 * to allow tuning.
 */
static int isp_conf_cache_linesz = 0x10;	/* 64 bytes */
static int isp_conf_latency_timer = 0x40;	/* 64 PCI cycles */

/*
 * Starting risc code address for ISP1040 and ISP1000.
 */
static unsigned short isp_risc_code_addr = 0x1000;

/*
 * patch in case of hw problems
 */
static int isp_burst_sizes_limit = 0xff;

/*
 * Firmware related externs
 */
extern ushort_t isp_sbus_risc_code[];
extern ushort_t isp_1040_risc_code[];
extern ushort_t isp_sbus_risc_code_length;
extern ushort_t isp_1040_risc_code_length;

/*
 * External data
 */
extern uchar_t scsi_cdb_size[];

/*
 * Hotplug support
 * Leaf ops (hotplug controls for target devices)
 */
#ifdef	ISPDEBUG
#ifndef lint
static void isp_i_print_response(struct isp *isp,
	struct isp_response *data_resp);
#endif
#endif
#ifdef	ISPDEBUG_FW
static int	isp_new_fw(dev_t dev, struct uio *uio, cred_t *cred_p);
#endif	/* ISPDEBUG_FW */

#ifdef	ISPDEBUG_IOCTL
static int	isp_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);
static int	isp_debug_ioctl = 0;
#endif	/* ISPDEBUG_IOCTL */

static struct cb_ops isp_cb_ops = {
	scsi_hba_open,
	scsi_hba_close,
	nodev,		/* strategy */
	nodev,		/* print */
	nodev,		/* dump */
	nodev,		/* read */
#ifdef ISPDEBUG_FW
	isp_new_fw,	/* write -- replace F/W image */
#else
	nodev,		/* write */
#endif
#ifdef	ISPDEBUG_IOCTL
	isp_ioctl,	/* ioctl */
#else
	scsi_hba_ioctl,	/* ioctl */
#endif
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* poll */
	ddi_prop_op,	/* prop_op */
	NULL,
	D_NEW | D_MP | D_HOTPLUG,
	CB_REV,		/* rev */
	nodev,		/* int (*cb_aread)() */
	nodev		/* int (*cb_awrite)() */
};

/*
 * dev_ops functions prototypes
 */
static int isp_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int isp_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int isp_dr_detach(dev_info_t *dip);

/*
 * Function prototypes
 *
 * SCSA functions exported by means of the transport table
 */
static int isp_scsi_tgt_probe(struct scsi_device *sd,
    int (*waitfunc)(void));
static int isp_scsi_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd);

static int isp_scsi_start(struct scsi_address *ap, struct scsi_pkt *pkt);
static int isp_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt);
static int isp_scsi_reset(struct scsi_address *ap, int level);
static int isp_scsi_getcap(struct scsi_address *ap, char *cap, int whom);
static int isp_scsi_setcap(struct scsi_address *ap, char *cap, int value,
    int whom);
static struct scsi_pkt *isp_scsi_init_pkt(struct scsi_address *ap,
    struct scsi_pkt *pkt, struct buf *bp, int cmdlen, int statuslen,
    int tgtlen, int flags, int (*callback)(), caddr_t arg);
static void isp_scsi_destroy_pkt(struct scsi_address *ap, struct scsi_pkt *pkt);
static void isp_scsi_dmafree(struct scsi_address *ap, struct scsi_pkt *pkt);
static void isp_scsi_sync_pkt(struct scsi_address *ap, struct scsi_pkt *pkt);
static int isp_scsi_reset_notify(struct scsi_address *ap, int flag,
    void (*callback)(caddr_t), caddr_t arg);
static int isp_scsi_quiesce(dev_info_t *dip);
static int isp_scsi_unquiesce(dev_info_t *dip);

static void isp_i_send_marker(struct isp *isp, short mod, ushort_t tgt,
    ushort_t lun);

static void isp_i_add_marker_to_list(struct isp *isp, short mod, ushort_t tgt,
    ushort_t lun);
static int isp_i_check_response_header(struct isp *isp,
	struct isp_response *resp, uint8_t *xor);

static int isp_i_backoff_host_int(struct isp *isp, clock_t total_time,
	clock_t max_sleep);

static void isp_i_add_to_free_slots(struct isp *isp, uint16_t entry);
static void isp_i_delete_from_slot_list(struct isp *isp,
		struct isp_slot_list *list, uint16_t entry);

/*
 * isp interrupt handlers
 */
static uint_t isp_intr(caddr_t arg);

/*
 * internal functions
 */
static int isp_i_commoncap(struct scsi_address *ap, char *cap, int val,
    int tgtonly, int doset);
static int isp_i_updatecap(struct isp *isp, ushort_t start_tgt,
    ushort_t end_tgt, int force);
static void isp_i_update_props(struct isp *isp, int tgt, ushort_t cap,
    ushort_t synch);
static void isp_i_update_this_prop(struct isp *isp, char *property, int tgt,
    int value, int size, int flag);
static void isp_i_update_sync_prop(struct isp *isp, struct isp_cmd *sp);
static void isp_i_initcap(struct isp *isp, int start_tgt, int end_tgt);

static void isp_i_watch();
static void isp_i_watch_isp(struct isp *isp);
static void isp_i_fatal_error(struct isp *isp, int flags);

static void isp_i_empty_waitQ(struct isp *isp);
static int isp_i_start_cmd(struct isp *isp, struct isp_cmd *sp,
	uint16_t *slotp);
static uint16_t isp_i_find_freeslot(struct isp *isp);
static int isp_i_polled_cmd_start(struct isp *isp, struct isp_cmd *sp);
static void isp_i_call_pkt_comp(struct isp_cmd *head);
static void isp_i_handle_arq(struct isp *isp, struct isp_cmd *sp);

static void isp_i_qflush(struct isp *isp,
    ushort_t start_tgt, ushort_t end_tgt);
static int isp_i_reset_interface(struct isp *isp, int action);
static int isp_i_reset_init_chip(struct isp *isp);
static int isp_i_set_marker(struct isp *isp,
    short mod, short tgt, short lun);

/*PRINTFLIKE3*/
static void isp_i_log(struct isp *isp, int level, char *fmt, ...);
static void isp_i_print_state(int level, struct isp *isp);

static int isp_i_response_error(struct isp *isp, struct isp_response *resp,
	uint16_t slot);

static int isp_i_mbox_cmd_start(struct isp *isp, struct isp_mbox_cmd *cmdp,
    ushort_t op_code, ...);
static void isp_i_mbox_cmd_complete(struct isp *isp);

static int isp_i_download_fw(struct isp *isp,
    ushort_t risc_addrp, ushort_t *fw_addrp, ushort_t fw_len);
static int isp_i_alive(struct isp *isp);
static int isp_i_handle_qfull_cap(struct isp *isp,
	ushort_t start, ushort_t end, int val,
	int flag_get_set, int flag_cmd);

static int isp_i_pkt_alloc_extern(struct isp *isp, struct isp_cmd *sp,
	int cmdlen, int tgtlen, int statuslen, int kf);
static void isp_i_pkt_destroy_extern(struct isp *isp, struct isp_cmd *sp);
static int isp_quiesce_bus(struct isp  *isp);
static int isp_unquiesce_bus(struct isp  *isp);
static int isp_mailbox_all(struct isp *isp, ushort_t op_code);
static int isp_i_outstanding(struct isp *isp);

static void isp_i_check_waitQ(struct isp *isp);
static void isp_create_errstats(struct isp *isp, int instance);
static short isp_i_get_mbox_event(struct isp *isp);
static int isp_i_handle_aen(struct isp *isp, short event);
static void isp_i_complete_aen(struct isp *isp, int async_event, int from_intr);

#ifdef ISPDEBUG
static void isp_i_test(struct isp *isp, struct isp_cmd *sp);
#endif

/* these replace macros (adding debouncing as well) */
static void	isp_i_update_queue_space(struct isp *isp);
static ushort_t	isp_i_get_response_in_db(struct isp *isp);

/*
 * kmem cache constuctor and destructor
 */
static int isp_kmem_cache_constructor(void * buf, void *cdrarg, int kmflags);
static void isp_kmem_cache_destructor(void * buf, void *cdrarg);

/* for updating the max no. of LUNs for a tgt */
static void isp_update_max_luns(struct isp *isp, int tgt);

static void	isp_check_waitq_and_exit_req_mutex(struct isp *isp);

/*
 * mutex for protecting variables shared by all instances of the driver
 */
static kmutex_t isp_global_mutex;

/*
 * mutex for protecting isp_log_buf which is shared among instances.
 */
static kmutex_t isp_log_mutex;

/*
 * readers/writer lock to protect the integrity of the softc structure
 * linked list while being traversed (or updated).
 */
static krwlock_t isp_global_rwlock;

/*
 * Local static data
 */
static void *isp_state = NULL;
static struct isp *isp_head;	/* for linking softc structures */
static struct isp *isp_tail;	/* for linking softc structures */
static int isp_scsi_watchdog_tick; /* isp_i_watch() interval in sec */
static int isp_tick;		/* isp_i_watch() interval in HZ */
static timeout_id_t isp_timeout_id;	/* isp_i_watch() timeout id */
static int timeout_initted = FALSE; /* isp_i_watch() timeout status */
static	char	isp_log_buf[256]; /* buffer used in isp_i_log */
/* selection timeouts are in millisecs */
static int isp_selection_timeout = SCSI_DEFAULT_SELECTION_TIMEOUT;
static int isp_select_timeout_array[] = {
	25, 50, 75, 100, 250, 500, 750, 1000
};
#define	ISP_SELECT_TIMEOUT_ARRAY_SIZE	\
	(sizeof (isp_select_timeout_array)/sizeof (int))

/*
 * default isp dma attr structure describes device
 * and DMA engine specific attributes/constrains necessary
 * to allocate DMA resources for ISP device.
 *
 * we currently don't support PCI 64-bit addressing supported by
 * 1040A card. 64-bit addressing allows 1040A to operate in address
 * spaces greater than 4 gigabytes.
 */
static ddi_dma_attr_t dma_ispattr = {
	DMA_ATTR_V0,				/* dma_attr_version */
	(unsigned long long)0,			/* dma_attr_addr_lo */
	(unsigned long long)0xffffffffULL,	/* dma_attr_addr_hi */
	(unsigned long long)0x00ffffff,		/* dma_attr_count_max */
	(unsigned long long)1,			/* dma_attr_align */
	DEFAULT_BURSTSIZE | BURST32 | BURST64 | BURST128,
						/* dma_attr_burstsizes */
	1,					/* dma_attr_minxfer */
	(unsigned long long)0x00ffffff,		/* dma_attr_maxxfer */
	(unsigned long long)0xffffffffULL,	/* dma_attr_seg */
	1,					/* dma_attr_sgllen */
	512,					/* dma_attr_granular */
	0					/* dma_attr_flags */
};

/*
 * warlock directives
 */
_NOTE(MUTEX_PROTECTS_DATA(isp_global_mutex, timeout_initted))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_hotplug_mutex, isp::isp_softstate))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_hotplug_mutex, isp::isp_cv))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_hotplug_mutex, isp::isp_hotplug_waiting))
_NOTE(MUTEX_PROTECTS_DATA(isp_global_mutex, isp::isp_next))
_NOTE(MUTEX_PROTECTS_DATA(isp_global_mutex, isp_timeout_id))
_NOTE(MUTEX_PROTECTS_DATA(isp_global_mutex, isp_head isp_tail))
_NOTE(MUTEX_PROTECTS_DATA(isp_log_mutex, isp_log_buf))

_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", isp_response))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", isp_request))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", scsi_arq_status))
_NOTE(SCHEME_PROTECTS_DATA("unique per pkt", buf scsi_pkt isp_cmd scsi_cdb))
_NOTE(SCHEME_PROTECTS_DATA("stable data", scsi_device scsi_address))
_NOTE(SCHEME_PROTECTS_DATA("protected by mutexes or no competing threads",
	isp_biu_regs isp_mbox_regs isp_sxp_regs isp_risc_regs))

_NOTE(DATA_READABLE_WITHOUT_LOCK(isp_tick))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ispdebug))
_NOTE(DATA_READABLE_WITHOUT_LOCK(scsi_watchdog_tick))
_NOTE(DATA_READABLE_WITHOUT_LOCK(scsi_reset_delay scsi_hba_tran))

/*
 * autoconfiguration routines.
 */
static struct dev_ops isp_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ddi_no_info,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	isp_attach,		/* attach */
	isp_detach,		/* detach */
	nodev,			/* reset */
	&isp_cb_ops,		/* driver operations */
	NULL,			/* bus operations */
	NULL,			/* no power management */
	ddi_quiesce_not_supported,	/* devo_quiesce */
};

char _depends_on[] = "misc/scsi";

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module. This one is a driver */
	"ISP SCSI HBA Driver", /* Name of the module. */
	&isp_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

int
_init(void)
{
	int ret;

	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);


	ret = ddi_soft_state_init(&isp_state, sizeof (struct isp),
	    ISP_INITIAL_SOFT_SPACE);
	if (ret != 0) {
		return (ret);
	}

	mutex_init(&isp_global_mutex, NULL, MUTEX_DRIVER, NULL);
	rw_init(&isp_global_rwlock, NULL, RW_DRIVER, NULL);
	mutex_init(&isp_log_mutex, NULL, MUTEX_DRIVER, NULL);

	if ((ret = scsi_hba_init(&modlinkage)) != 0) {
		rw_destroy(&isp_global_rwlock);
		mutex_destroy(&isp_global_mutex);
		mutex_destroy(&isp_log_mutex);
		ddi_soft_state_fini(&isp_state);
		return (ret);
	}

	ret = mod_install(&modlinkage);
	if (ret != 0) {
		scsi_hba_fini(&modlinkage);
		rw_destroy(&isp_global_rwlock);
		mutex_destroy(&isp_global_mutex);
		mutex_destroy(&isp_log_mutex);
		ddi_soft_state_fini(&isp_state);
	}

	return (ret);
}

/*
 * nexus drivers are currently not unloaded so this routine is really redundant
 */
int
_fini(void)
{
	int ret;

	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	if ((ret = mod_remove(&modlinkage)) != 0)
		return (ret);


	scsi_hba_fini(&modlinkage);

	rw_destroy(&isp_global_rwlock);
	mutex_destroy(&isp_global_mutex);
	mutex_destroy(&isp_log_mutex);

	ddi_soft_state_fini(&isp_state);

	return (ret);
}

int
_info(struct modinfo *modinfop)
{
	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);
	return (mod_info(&modlinkage, modinfop));
}


static int
isp_scsi_tgt_probe(struct scsi_device *sd, int (*waitfunc)(void))
{
	dev_info_t *dip = ddi_get_parent(sd->sd_dev);
	int rval = SCSIPROBE_FAILURE;
	scsi_hba_tran_t *tran;
	struct isp *isp;
	ushort_t tgt = sd->sd_address.a_target;


	tran = ddi_get_driver_private(dip);
	ASSERT(tran != NULL);
	isp = TRAN2ISP(tran);
	ASSERT(isp != NULL);

	if (tgt >= NTARGETS_WIDE) {
		isp_i_log(isp, CE_WARN, "%d: Invalid target.", tgt);
		return (rval);
	}

	ISP_DEBUG(isp, SCSI_DEBUG, "scsi_tgt_probe: entered tgt %d", tgt);

	/*
	 * force renegotiation since inquiry cmds do not always
	 * cause check conditions
	 */
	ISP_MUTEX_ENTER(isp);
	/* no use continuing if the LUN is larger than we support */
	if (sd->sd_address.a_lun >= isp->isp_max_lun[tgt]) {
		isp_i_log(isp, CE_WARN,
		    "probe request for LUN %d denied: max LUN %d",
		    sd->sd_address.a_lun, isp->isp_max_lun[tgt] - 1);
		ISP_MUTEX_EXIT(isp);
		return (rval);
	}
	(void) isp_i_updatecap(isp, tgt, tgt, FALSE);
	ISP_MUTEX_EXIT(isp);

	rval = scsi_hba_probe(sd, waitfunc);

	/*
	 * the scsi-options precedence is:
	 *	target-scsi-options		highest
	 * 	device-type-scsi-options
	 *	per bus scsi-options
	 *	global scsi-options		lowest
	 */
	ISP_MUTEX_ENTER(isp);
	/* does this target exist ?? */
	if (rval == SCSIPROBE_EXISTS) {
		/* are options defined for this tgt ?? */
		if ((isp->isp_target_scsi_options_defined & (1 << tgt)) ==
		    0) {
			int options;

			if ((options = scsi_get_device_type_scsi_options(dip,
			    sd, -1)) != -1) {
				/* dev-specific options were found */
				isp->isp_target_scsi_options[tgt] = options;
				isp_i_initcap(isp, tgt, tgt);
				(void) isp_i_updatecap(isp, tgt, tgt, FALSE);

				/* log scsi-options for this LUN */
				isp_i_log(isp, CE_WARN,
				    "?target%x-scsi-options = 0x%x", tgt,
				    isp->isp_target_scsi_options[tgt]);
			}

			/* update max LUNs for this tgt */
			isp_update_max_luns(isp, tgt);

		}
	}
	ISP_MUTEX_EXIT(isp);

	ISP_DEBUG(isp, SCSI_DEBUG, "target%x-scsi-options=0x%x",
	    tgt, isp->isp_target_scsi_options[tgt]);

	return (rval);
}


/*
 * update max luns for this target
 *
 * called with isp mutex held
 */
static void
isp_update_max_luns(struct isp *isp, int tgt)
{
	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));

	switch (SCSI_OPTIONS_NLUNS(isp->isp_target_scsi_options[tgt])) {
	case SCSI_OPTIONS_NLUNS_32:
		isp->isp_max_lun[tgt] = SCSI_32LUNS_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_16:
		isp->isp_max_lun[tgt] = SCSI_16LUNS_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_8:
		isp->isp_max_lun[tgt] = NLUNS_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_1:
		isp->isp_max_lun[tgt] = SCSI_1LUN_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_DEFAULT:
		isp->isp_max_lun[tgt] = (uchar_t)ISP_NLUNS_PER_TARGET;
		break;
	default:
		/* do something sane and print a warning */
		isp_i_log(isp, CE_WARN,
		    "unknown scsi-options value for max luns: using %d",
		    isp->isp_max_lun[tgt]);
		break;
	}

}


/*ARGSUSED*/
static int
isp_scsi_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd)
{
	int		res = DDI_FAILURE;
	scsi_hba_tran_t	*tran;
	struct isp	*isp;
	int		tgt;


	ASSERT(hba_dip != NULL);
	tran = ddi_get_driver_private(hba_dip);
	ASSERT(tran != NULL);
	isp = TRAN2ISP(tran);
	ASSERT(isp != NULL);
	ISP_DEBUG(isp, SCSI_DEBUG, "scsi_tgt_init called");

	ASSERT(sd != NULL);
	tgt = sd->sd_address.a_target;

	mutex_enter(ISP_RESP_MUTEX(isp));
	if ((tgt < NTARGETS_WIDE) &&
	    (sd->sd_address.a_lun < isp->isp_max_lun[tgt])) {
		res = DDI_SUCCESS;
	}
	mutex_exit(ISP_RESP_MUTEX(isp));

	return (res);
}


/*
 * Attach isp host adapter.  Allocate data structures and link
 * to isp_head list.  Initialize the isp and we're
 * on the air.
 */
/*ARGSUSED*/
static int
isp_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	char		buf[64];		/* for "isp%d" */
	int		id;
	int		mutex_initted = FALSE;
	int		interrupt_added = FALSE;
	int		bound_handle = FALSE;
	struct isp	*isp;
	int		instance;
	struct isp_regs_off	isp_reg_off;
	scsi_hba_tran_t	*tran = NULL;
	ddi_device_acc_attr_t	dev_attr;
	size_t		rlen;
	uint_t		count;
	struct isp	*s_isp;
	struct isp	*l_isp;
	ddi_dma_attr_t	tmp_dma_attr;
	char		*prop_template = "target%x-scsi-options";
	char		prop_str[32];		/* for getting dev. type */
	int		rval;
	int		i;
	int		dt_len = 0;
	int		mode;
	uint_t		fw_len_bytes;


	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	instance = ddi_get_instance(dip);

	ISP_DEBUG(NULL, SCSI_DEBUG, "isp: instance %d attach called", instance);

	switch (cmd) {
	case DDI_ATTACH:
		dev_attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
		dev_attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
		break;

	case DDI_RESUME:
		if ((tran = ddi_get_driver_private(dip)) == NULL)
			return (DDI_FAILURE);

		isp = TRAN2ISP(tran);
		if (!isp) {
			return (DDI_FAILURE);
		}

		/*
		 * the downloaded firmware on the card will be erased by
		 * the power cycle and a new download is needed.
		 */
		ISP_MUTEX_ENTER(isp);

		if (isp->isp_bus == ISP_SBUS) {
			rval = isp_i_download_fw(isp,
			    isp_risc_code_addr, isp_sbus_risc_code,
			    isp_sbus_risc_code_length);
		} else {
			rval = isp_i_download_fw(isp,
			    isp_risc_code_addr, isp_1040_risc_code,
			    isp_1040_risc_code_length);
		}
		if (rval != 0) {
			ISP_MUTEX_EXIT(isp);
			isp_i_log(isp, CE_WARN,
			    "can't reload firmware: failing attach/resume");
			return (DDI_FAILURE);
		}

		isp->isp_suspended = FALSE;

		/*
		 * if there is no obp then when machine is rebooted w/o
		 * pwr cycle the targets still have old parameters -- so
		 * reset the bus in this case
		 */
		if (isp->isp_no_obp) {
			rval = isp_i_reset_interface(isp, ISP_FORCE_RESET_BUS);
		} else {
			rval = isp_i_reset_interface(isp,
			    ISP_SKIP_STOP_QUEUES);
		}
		if (rval != 0) {
			ISP_MUTEX_EXIT(isp);
			isp_i_log(isp, CE_WARN,
			    "can't reset interfaceduring attach/resume");
			return (DDI_FAILURE);
		}

		mutex_exit(ISP_RESP_MUTEX(isp));
		isp_check_waitq_and_exit_req_mutex(isp);
		mutex_enter(&isp_global_mutex);
		if (isp_timeout_id == 0) {
			isp_timeout_id =
			    timeout(isp_i_watch, NULL, isp_tick);
			timeout_initted = TRUE;
		}
		mutex_exit(&isp_global_mutex);
		ISP_INC32_ERRSTATS(isp, isperr_resumes);

		return (DDI_SUCCESS);

	default:
		isp_i_log(NULL, CE_WARN,
		    "isp%d: Cmd != DDI_ATTACH/DDI_RESUME", instance);
		return (DDI_FAILURE);
	}

	/*
	 * Since we know that some instantiations of this device can
	 * be plugged into slave-only SBus slots, check to see whether
	 * this is one such.
	 */
	if (ddi_slaveonly(dip) == DDI_SUCCESS) {
		isp_i_log(NULL, CE_WARN,
		    "isp%d: Device in slave-only slot, unused",
		    instance);
		return (DDI_FAILURE);
	}

	if (ddi_intr_hilevel(dip, 0)) {
		/*
		 * Interrupt number '0' is a high-level interrupt.
		 * At this point you either add a special interrupt
		 * handler that triggers a soft interrupt at a lower level,
		 * or - more simply and appropriately here - you just
		 * fail the attach.
		 */
		isp_i_log(NULL, CE_WARN,
		    "isp%d: Device is using a hilevel intr, unused",
		    instance);
		return (DDI_FAILURE);
	}

	/*
	 * Allocate isp data structure.
	 */
	if (ddi_soft_state_zalloc(isp_state, instance) != DDI_SUCCESS) {
		isp_i_log(NULL, CE_WARN, "isp%d: Failed to alloc soft state",
		    instance);
		return (DDI_FAILURE);
	}

	isp = (struct isp *)ddi_get_soft_state(isp_state, instance);
	if (isp == NULL) {
		isp_i_log(NULL, CE_WARN, "isp%d: Bad soft state", instance);
		ddi_soft_state_free(isp_state, instance);
		return (DDI_FAILURE);
	}

	/*
	 * initialise the slots lists.
	 */
	isp->free_slots.head = ISP_MAX_SLOTS;
	isp->free_slots.tail = ISP_MAX_SLOTS;

	for (i = 0; i < ISP_MAX_SLOTS; i++) {
		isp_i_add_to_free_slots(isp, i);
	}

	isp->busy_slots.head = ISP_MAX_SLOTS;
	isp->busy_slots.tail = ISP_MAX_SLOTS;
	/*
	 * get device type of parent to figure out which bus we are on
	 */
	dt_len = sizeof (prop_str);
	if (ddi_prop_op(DDI_DEV_T_ANY, ddi_get_parent(dip),
	    PROP_LEN_AND_VAL_BUF, DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP,
	    "device_type", prop_str,
	    &dt_len) != DDI_SUCCESS) {
		/* must be (an older?) SPARC/SBUS system */
		isp->isp_bus = ISP_SBUS;
	} else {
		prop_str[dt_len] = '\0';
		if (strcmp("pci", prop_str) == 0) {
			isp->isp_bus = ISP_PCI;
		} else {
			isp->isp_bus = ISP_SBUS;
		}
	}

	/*
	 * set up as much bus-specific stuff as we can here
	 */
	if (isp->isp_bus == ISP_SBUS) {

		ISP_DEBUG2(isp, SCSI_DEBUG, "isp bus is ISP_SBUS");

		isp->isp_reg_number = ISP_SBUS_REG_NUMBER;
		dev_attr.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC;
		isp_reg_off.isp_biu_regs_off = ISP_BUS_BIU_REGS_OFF;
		isp_reg_off.isp_mbox_regs_off = ISP_SBUS_MBOX_REGS_OFF;
		isp_reg_off.isp_sxp_regs_off = ISP_SBUS_SXP_REGS_OFF;
		isp_reg_off.isp_risc_regs_off = ISP_SBUS_RISC_REGS_OFF;

	} else {
		int	device_id;

		ISP_DEBUG2(isp, SCSI_DEBUG, "isp bus is ISP_PCI");

		device_id = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "device-id", -1);
		/* check if this is an isp1040 chip */
		if (device_id != ISP_DEVICEID_1040) {
			isp_i_log(isp, CE_WARN, "unsupported deviceid %x",
			    device_id);
			ddi_soft_state_free(isp_state, instance);
			return (DDI_FAILURE);
		}

		isp->isp_reg_number = ISP_PCI_REG_NUMBER;
		dev_attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
		isp_reg_off.isp_biu_regs_off = ISP_BUS_BIU_REGS_OFF;
		isp_reg_off.isp_mbox_regs_off = ISP_PCI_MBOX_REGS_OFF;
		isp_reg_off.isp_sxp_regs_off = ISP_PCI_SXP_REGS_OFF;
		isp_reg_off.isp_risc_regs_off = ISP_PCI_RISC_REGS_OFF;
		/*
		 * map in pci config space
		 */
		if (pci_config_setup(dip, &isp->isp_pci_config_acc_handle) !=
		    DDI_SUCCESS) {
			isp_i_log(isp, CE_WARN,
			    "Unable to map pci config registers");
			ddi_soft_state_free(isp_state, instance);
			return (DDI_FAILURE);
		}
	}

	/*
	 * map in device registers
	 */

	if (ddi_regs_map_setup(dip, isp->isp_reg_number,
	    (caddr_t *)&isp->isp_biu_reg, isp_reg_off.isp_biu_regs_off,
	    sizeof (struct isp_biu_regs),
	    &dev_attr, &isp->isp_biu_acc_handle) != DDI_SUCCESS) {
		isp_i_log(isp, CE_WARN, "Unable to map biu registers");
		goto fail;
	}

	if (ddi_regs_map_setup(dip, isp->isp_reg_number,
	    (caddr_t *)&isp->isp_mbox_reg, isp_reg_off.isp_mbox_regs_off,
	    sizeof (struct isp_mbox_regs),
	    &dev_attr, &isp->isp_mbox_acc_handle) != DDI_SUCCESS) {
		isp_i_log(isp, CE_WARN, "Unable to map mbox registers");
		goto fail;
	}

	if (ddi_regs_map_setup(dip, isp->isp_reg_number,
	    (caddr_t *)&isp->isp_sxp_reg, isp_reg_off.isp_sxp_regs_off,
	    sizeof (struct isp_sxp_regs),
	    &dev_attr, &isp->isp_sxp_acc_handle) != DDI_SUCCESS) {
		isp_i_log(isp, CE_WARN, "Unable to map sxp registers");
		goto fail;
	}

	if (ddi_regs_map_setup(dip, isp->isp_reg_number,
	    (caddr_t *)&isp->isp_risc_reg, isp_reg_off.isp_risc_regs_off,
	    sizeof (struct isp_risc_regs),
	    &dev_attr, &isp->isp_risc_acc_handle) != DDI_SUCCESS) {
		isp_i_log(isp, CE_WARN, "Unable to map risc registers");
		goto fail;
	}

	isp->isp_cmdarea = NULL;
	tmp_dma_attr = dma_ispattr;

	if (ddi_dma_alloc_handle(dip, &tmp_dma_attr,
	    DDI_DMA_SLEEP, NULL, &isp->isp_dmahandle) != DDI_SUCCESS) {
		isp_i_log(isp, CE_WARN, "Cannot alloc dma handle");
		goto fail;
	}

	/*
	 * sun4u/sbus needs streaming mode and pci bus can't handle
	 * streaming mode since not all IOPBs are 64-byte (e.g. if they
	 * do not contain ARS data)
	 */
	if (isp->isp_bus == ISP_SBUS) {
		mode = DDI_DMA_STREAMING;
		ISP_DEBUG2(isp, SCSI_DEBUG, "using streaming DMA memory");
	} else {
		mode = DDI_DMA_CONSISTENT;
		ISP_DEBUG2(isp, SCSI_DEBUG, "using consistent DMA memory");
	}

	if (ddi_dma_mem_alloc(isp->isp_dmahandle,
	    ISP_QUEUE_SIZE(isp_max_requests, isp_max_responses),
	    &dev_attr, mode, DDI_DMA_SLEEP,
	    NULL, (caddr_t *)&isp->isp_cmdarea, &rlen,
	    &isp->isp_dma_acc_handle) != DDI_SUCCESS) {
		isp_i_log(isp, CE_WARN, "Cannot alloc cmd area");
		goto fail;
	}
	if (ddi_dma_addr_bind_handle(isp->isp_dmahandle,
	    NULL, isp->isp_cmdarea,
	    ISP_QUEUE_SIZE(isp_max_requests, isp_max_responses),
	    DDI_DMA_RDWR|mode,
	    DDI_DMA_SLEEP, NULL, &isp->isp_dmacookie,
	    &count) != DDI_DMA_MAPPED) {
		isp_i_log(isp, CE_WARN, "Cannot bind cmd area");
		goto fail;
	}
	bound_handle = TRUE;
	bzero(isp->isp_cmdarea, ISP_QUEUE_SIZE(isp_max_requests,
	    isp_max_responses));
	isp->isp_request_dvma = isp->isp_dmacookie.dmac_address;
	isp->isp_request_base = (struct isp_request *)isp->isp_cmdarea;

	/*
	 * verify that request/response queue size is within sane limits,
	 * since it can be hacked on via /etc/system -- puke if:
	 *  1. the request and response queues are different sizes, or
	 *  2. the request (and hence response) queue is too large, or
	 *  3. the request/response queues are too small to fit the RAM
	 *	image in
	 */
	if (isp->isp_bus == ISP_SBUS) {
		fw_len_bytes = isp_sbus_risc_code_length * sizeof (ushort_t);
	} else {
		fw_len_bytes = isp_1040_risc_code_length * sizeof (ushort_t);
	}
	if ((isp_max_requests != isp_max_responses) ||
	    (isp_max_requests > ISP_MAX_QUEUE_SIZE) ||
	    (isp_max_responses > ISP_MAX_QUEUE_SIZE) ||
	    (ISP_QUEUE_SIZE(isp_max_requests, isp_max_responses) <
	    fw_len_bytes)) {
		/*
		 * specified value(s) bogus -- assume defined value is ok
		 */
		isp_i_log(isp, CE_WARN,
		    "isp request/response queue size %d/%d incorrect: "
		    "using %d/%d",
		    isp_max_requests, isp_max_responses,
		    ISP_MAX_REQUESTS, ISP_MAX_RESPONSES);
		isp_max_requests = ISP_MAX_REQUESTS;
		isp_max_responses = ISP_MAX_RESPONSES;
	}

	/* set up DMA */
	isp->isp_response_dvma =
	    isp->isp_request_dvma + (isp_max_requests *
	    sizeof (struct isp_request));
	isp->isp_response_base = (struct isp_response *)
	    ((intptr_t)isp->isp_request_base +
	    (isp_max_requests * sizeof (struct isp_request)));
	isp->isp_request_in = isp->isp_request_out = 0;
	isp->isp_response_in = isp->isp_response_out = 0;

	/*
	 * for reset throttling -- when this is set then requests
	 * will be put on the wait queue -- protected by the
	 * wait queue mutex
	 */
	isp->isp_in_reset = 0;
	isp->isp_polled_completion = 0;

	/* init softstate for hotplugging */
	isp->isp_softstate = 0;

	/*
	 * get cookie so we can initialize the mutexes
	 */
	if (ddi_get_iblock_cookie(dip, (uint_t)0, &isp->isp_iblock)
	    != DDI_SUCCESS) {
		goto fail;
	}

	/*
	 * Allocate a transport structure
	 */
	tran = scsi_hba_tran_alloc(dip, SCSI_HBA_CANSLEEP);

	/* Indicate that we are 'sizeof (scsi_*(9S))' clean. */
	scsi_size_clean(dip);		/* SCSI_SIZE_CLEAN_VERIFY ok */

	isp->isp_tran		= tran;
	isp->isp_dip		= dip;
	isp->isp_instance	= (ushort_t)instance;

	tran->tran_hba_private	= isp;
	tran->tran_tgt_private	= NULL;
	tran->tran_tgt_init	= isp_scsi_tgt_init;
	tran->tran_tgt_probe	= isp_scsi_tgt_probe;
	tran->tran_tgt_free	= NULL;

	tran->tran_start	= isp_scsi_start;
	tran->tran_abort	= isp_scsi_abort;
	tran->tran_reset	= isp_scsi_reset;
	tran->tran_getcap	= isp_scsi_getcap;
	tran->tran_setcap	= isp_scsi_setcap;
	tran->tran_init_pkt	= isp_scsi_init_pkt;
	tran->tran_destroy_pkt	= isp_scsi_destroy_pkt;
	tran->tran_dmafree	= isp_scsi_dmafree;
	tran->tran_sync_pkt	= isp_scsi_sync_pkt;
	tran->tran_reset_notify = isp_scsi_reset_notify;
	tran->tran_get_bus_addr	= NULL;
	tran->tran_get_name	= NULL;
	tran->tran_quiesce	= isp_scsi_quiesce;
	tran->tran_unquiesce	= isp_scsi_unquiesce;
	tran->tran_bus_reset	= NULL;
	tran->tran_add_eventcall	= NULL;
	tran->tran_get_eventcookie	= NULL;
	tran->tran_post_event		= NULL;
	tran->tran_remove_eventcall	= NULL;

	/*
	 * find the clock frequency of chip
	 */
	isp->isp_clock_frequency =
	    ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "clock-frequency", -1);

	/*
	 * if scsi-selection-timeout property exists, use it
	 */
	isp_selection_timeout = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, 0, "scsi-selection-timeout", SCSI_DEFAULT_SELECTION_TIMEOUT);
	/* check user's selection timeout against valid values */
	for (i = 0; i < ISP_SELECT_TIMEOUT_ARRAY_SIZE; i++) {
		if (isp_selection_timeout == isp_select_timeout_array[i]) {
			break;
		}
	}
	if (i >= ISP_SELECT_TIMEOUT_ARRAY_SIZE) {
		isp_i_log(isp, CE_WARN, "invalid selection_timeout %d value,"
		    " using default value", isp_selection_timeout);
		isp_selection_timeout = SCSI_DEFAULT_SELECTION_TIMEOUT;
	} else if (isp_selection_timeout != SCSI_DEFAULT_SELECTION_TIMEOUT) {
		isp_i_log(isp, CE_NOTE, "set selection_timeout set to %d",
		    isp_selection_timeout);
	}

	/*
	 * For PCI-ISP if the clock frequency property does not exist then
	 * assume that the card does not have Fcode. This will also work
	 * for cPCI cards because though the cPCI deamon will create
	 * SUNW,isp node, it will not execute the FCode and hence no
	 * clock-frequency property.
	 *
	 * this also now applies to SBus, since it can now be hotplugged on
	 * E10k platform just like PCI
	 */
	if (isp->isp_clock_frequency == -1) {
		isp->isp_no_obp = TRUE;
	}

	/*
	 * if there's no OBP then assume std. clock frequency
	 */
	if (isp->isp_no_obp) {
		isp->isp_clock_frequency = 60 * 1000000;
	}

	if (isp->isp_clock_frequency <= 0) {
		isp_i_log(isp, CE_WARN,
		    "Can't determine clock frequency of chip");
		goto fail;
	}

	/*
	 * convert from Hz to MHz, making  sure to round to the nearest MHz.
	 */
	isp->isp_clock_frequency = (isp->isp_clock_frequency + 500000)/1000000;
	ISP_DEBUG2(isp, SCSI_DEBUG, "clock frequency=%d MHz",
	    isp->isp_clock_frequency);

	/*
	 * find scsi host id property
	 */
	id = ddi_getprop(DDI_DEV_T_ANY, dip, 0, "scsi-initiator-id", -1);
	if ((id != scsi_host_id) && (id >= 0) && (id < NTARGETS_WIDE)) {
		isp_i_log(isp, CE_NOTE, "initiator SCSI ID now %d", id);
		isp->isp_initiator_id = (uchar_t)id;
	} else {
		isp->isp_initiator_id = (uchar_t)scsi_host_id;
	}

	isp->isp_scsi_tag_age_limit =
	    ddi_getprop(DDI_DEV_T_ANY, dip, 0, "scsi-tag-age-limit",
	    scsi_tag_age_limit);
	ISP_DEBUG2(isp, SCSI_DEBUG, "isp scsi_tage_age_limit=%d, global=%d",
	    isp->isp_scsi_tag_age_limit, scsi_tag_age_limit);
	if (isp->isp_scsi_tag_age_limit != scsi_tag_age_limit) {
		isp_i_log(isp, CE_NOTE, "scsi-tag-age-limit=%d",
		    isp->isp_scsi_tag_age_limit);
	}

	isp->isp_scsi_reset_delay =
	    ddi_getprop(DDI_DEV_T_ANY, dip, 0, "scsi-reset-delay",
	    scsi_reset_delay);
	ISP_DEBUG2(isp, SCSI_DEBUG, "isp scsi_reset_delay=%ld, global=%d",
	    isp->isp_scsi_reset_delay, scsi_reset_delay);
	if (isp->isp_scsi_reset_delay != scsi_reset_delay) {
		isp_i_log(isp, CE_NOTE, "scsi-reset-delay=%ld",
		    isp->isp_scsi_reset_delay);
	}

	/*
	 * find the burstsize and reduce ours if necessary
	 * If no burst size found, select a reasonable default.
	 */
	tmp_dma_attr.dma_attr_burstsizes &=
	    (ddi_dma_burstsizes(isp->isp_dmahandle) &
	    isp_burst_sizes_limit);
	isp->isp_burst_size = tmp_dma_attr.dma_attr_burstsizes;


	ISP_DEBUG2(isp, SCSI_DEBUG, "ispattr burstsize=%x",
	    isp->isp_burst_size);

	if (isp->isp_burst_size == -1) {
		isp->isp_burst_size = DEFAULT_BURSTSIZE | BURST32 | BURST64;
		ISP_DEBUG2(isp, SCSI_DEBUG, "Using default burst sizes, 0x%x",
		    isp->isp_burst_size);
	} else {
		isp->isp_burst_size &= BURSTSIZE_MASK;
		ISP_DEBUG2(isp, SCSI_DEBUG, "burst sizes= 0x%x",
		    isp->isp_burst_size);
	}

	/*
	 * set the threshold for the dma fifo
	 */
	if (isp->isp_burst_size & BURST128) {
		if (isp->isp_bus == ISP_SBUS) {
			isp_i_log(isp, CE_WARN, "Wrong burst size for SBus");
			goto fail;
		}
		isp->isp_conf1_fifo = ISP_PCI_CONF1_FIFO_128;
	} else if (isp->isp_burst_size & BURST64) {
		if (isp->isp_bus == ISP_SBUS) {
			isp->isp_conf1_fifo = ISP_SBUS_CONF1_FIFO_64;
		} else {
			isp->isp_conf1_fifo = ISP_PCI_CONF1_FIFO_64;
		}
	} else if (isp->isp_burst_size & BURST32) {
		if (isp->isp_bus == ISP_SBUS) {
			isp->isp_conf1_fifo = ISP_SBUS_CONF1_FIFO_32;
		} else {
			isp->isp_conf1_fifo = ISP_PCI_CONF1_FIFO_32;
		}
	} else if (isp->isp_burst_size & BURST16) {
		if (isp->isp_bus == ISP_SBUS) {
			isp->isp_conf1_fifo = ISP_SBUS_CONF1_FIFO_16;
		} else {
			isp->isp_conf1_fifo = ISP_PCI_CONF1_FIFO_16;
		}
	} else if (isp->isp_burst_size & BURST8) {
		if (isp->isp_bus == ISP_SBUS) {
			isp->isp_conf1_fifo = ISP_SBUS_CONF1_FIFO_8 |
			    ISP_SBUS_CONF1_BURST8;
		} else {
			isp_i_log(isp, CE_WARN, "Wrong burst size for PCI");
			goto fail;
		}
	}

	if (isp->isp_conf1_fifo) {
		isp->isp_conf1_fifo |= ISP_BUS_CONF1_BURST_ENABLE;
	}

	ISP_DEBUG2(isp, SCSI_DEBUG, "isp_conf1_fifo=0x%x", isp->isp_conf1_fifo);

	/*
	 * Attach this instance of the hba
	 */
	if (scsi_hba_attach_setup(dip, &tmp_dma_attr, tran, 0) !=
	    DDI_SUCCESS) {
		isp_i_log(isp, CE_WARN, "SCSI HBA attach failed");
		goto fail;
	}

	/*
	 * if scsi-options property exists, use it;
	 * otherwise use the global variable
	 */
	isp->isp_scsi_options =
	    ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0, "scsi-options",
	    SCSI_OPTIONS_DR);
	ISP_DEBUG2(isp, SCSI_DEBUG, "isp scsi_options=%x",
	    isp->isp_scsi_options);

	/*
	 * if target<n>-scsi-options property exists, use it;
	 * otherwise use the isp_scsi_options
	 */
	for (i = 0; i < NTARGETS_WIDE; i++) {
		(void) sprintf(prop_str, prop_template, i);
		isp->isp_target_scsi_options[i] = ddi_prop_get_int(
		    DDI_DEV_T_ANY, dip, 0, prop_str, -1);
		if (isp->isp_target_scsi_options[i] != -1) {
			isp_i_log(isp, CE_NOTE,
			    "?target%x-scsi-options = 0x%x",
			    i, isp->isp_target_scsi_options[i]);
			isp->isp_target_scsi_options_defined |= 1 << i;
		} else {
			isp->isp_target_scsi_options[i] =
			    isp->isp_scsi_options;
		}

		ISP_DEBUG2(isp, SCSI_DEBUG, "target%x-scsi-options=%x", i,
		    isp->isp_target_scsi_options[i]);

		/*
		 * set default max luns per target
		 *
		 * Note: this max should really depend on the SCSI type
		 * of the target, i.e. SCSI-2 would only get 8 LUNs max,
		 * SCSI-1 one LUN, etc.  But, historyically, this adapter
		 * driver has always handled 32 as a default
		 */
		isp->isp_max_lun[i] = (ushort_t)ISP_NLUNS_PER_TARGET;
	}

	/*
	 * initialize the "need to send a marker" list to "none needed"
	 */
	isp->isp_marker_in = isp->isp_marker_out = 0;	 /* no markers */
	isp->isp_marker_free = ISP_MI_SIZE - 1;		 /* all empty */

	/*
	 * initialize the mbox sema
	 */
	sema_init(ISP_MBOX_SEMA(isp), 1, NULL, SEMA_DRIVER, isp->isp_iblock);

	/*
	 * initialize the wait queue mutex
	 */
	mutex_init(ISP_WAITQ_MUTEX(isp), NULL, MUTEX_DRIVER, isp->isp_iblock);

	/*
	 * initialize intr mutex/cv
	 */
	mutex_init(ISP_INTR_MUTEX(isp), NULL, MUTEX_DRIVER, isp->isp_iblock);
	cv_init(ISP_INTR_CV(isp), NULL, CV_DRIVER, isp->isp_iblock);
	isp->isp_in_intr = 0;

	isp->isp_checking_semlock = FALSE;

	/*
	 * mutexes to protect the isp request and response queue
	 */
	mutex_init(ISP_REQ_MUTEX(isp), NULL, MUTEX_DRIVER, isp->isp_iblock);
	mutex_init(ISP_RESP_MUTEX(isp), NULL, MUTEX_DRIVER, isp->isp_iblock);

	/*
	 * Initialize the conditional variable for quiescing the bus
	 */
	cv_init(ISP_CV(isp), NULL, CV_DRIVER, NULL);

	/*
	 * Initialize mutex for hotplug support.
	 */
	mutex_init(ISP_HOTPLUG_MUTEX(isp), NULL, MUTEX_DRIVER,
	    isp->isp_iblock);

	mutex_initted = TRUE;

	if (ddi_add_intr(dip, (uint_t)0,
	    (ddi_iblock_cookie_t *)&isp->isp_iblock,
	    (ddi_idevice_cookie_t *)0,
	    isp_intr,
	    (caddr_t)isp) != DDI_SUCCESS) {
		isp_i_log(isp, CE_WARN, "Cannot add intr");
		goto fail;
	}
	interrupt_added = TRUE;

	/*
	 * note: we can have competing threads starting here (but don't tell
	 * warlock, since it can't handle branch paths that are different)
	 */

	/*
	 * kstat_intr support
	 */
	(void) sprintf(buf, "isp%d", instance);
	isp->isp_kstat = kstat_create("isp", instance, buf, "controller",
	    KSTAT_TYPE_INTR, 1, KSTAT_FLAG_PERSISTENT);
	if (isp->isp_kstat != NULL) {
		kstat_install(isp->isp_kstat);
	}
	isp_create_errstats(isp, instance);

	/*
	 * link all isp's for debugging
	 */
	rw_enter(&isp_global_rwlock, RW_WRITER);
	isp->isp_next = NULL;

	if (isp_head != NULL) {
		isp_tail->isp_next = isp;
		isp_tail = isp;
	} else {
		isp_head = isp_tail = isp;
	}
	rw_exit(&isp_global_rwlock);

	/*
	 * set up watchdog per all isp's
	 */
	mutex_enter(&isp_global_mutex);
	if (isp_timeout_id == 0) {
		ASSERT(!timeout_initted);
		isp_scsi_watchdog_tick =
		    ddi_getprop(DDI_DEV_T_ANY, dip, 0, "scsi-watchdog-tick",
		    scsi_watchdog_tick);
		if (isp_scsi_watchdog_tick != scsi_watchdog_tick) {
			isp_i_log(isp, CE_NOTE, "scsi-watchdog-tick=%d",
			    isp_scsi_watchdog_tick);
		}
		/*
		 * The isp_scsi_watchdog_tick should not be less than
		 * the pkt_time otherwise we will induce spurious timeouts.
		 */
		if (isp_scsi_watchdog_tick < ISP_DEFLT_WATCHDOG_SECS) {
			isp_scsi_watchdog_tick = ISP_DEFLT_WATCHDOG_SECS;
		}
		isp_tick =
		    drv_usectohz((clock_t)isp_scsi_watchdog_tick * 1000000);
		ISP_DEBUG2(isp, SCSI_DEBUG,
		    "isp_scsi_watchdog_tick=%d, isp_tick=%d",
		    isp_scsi_watchdog_tick, isp_tick);
		isp_timeout_id = timeout(isp_i_watch, NULL, isp_tick);
		timeout_initted = TRUE;
	}

	mutex_exit(&isp_global_mutex);

	ISP_MUTEX_ENTER(isp);

	/*
	 * create kmem cache for packets
	 */
	(void) sprintf(buf, "isp%d_cache", instance);
	isp->isp_kmem_cache = kmem_cache_create(buf,
	    EXTCMDS_SIZE, 8, isp_kmem_cache_constructor,
	    isp_kmem_cache_destructor, NULL, (void *)isp, NULL, 0);

	/*
	 * Download the ISP firmware that has been linked in
	 * We need the mutexes here to avoid assertion failures in
	 * the mbox cmds
	 */
	if (isp->isp_bus == ISP_SBUS) {
		rval = isp_i_download_fw(isp, isp_risc_code_addr,
		    isp_sbus_risc_code, isp_sbus_risc_code_length);
	} else {
		rval = isp_i_download_fw(isp, isp_risc_code_addr,
		    isp_1040_risc_code, isp_1040_risc_code_length);
	}
	if (rval) {
		ISP_MUTEX_EXIT(isp);
		isp_i_log(isp, CE_WARN, "couldn't load firmware");
		goto fail;
	}

	/*
	 * Initialize the default Target Capabilites and Sync Rates
	 */
	isp_i_initcap(isp, 0, NTARGETS_WIDE - 1);

	/*
	 * reset isp and initialize capabilities
	 * Do NOT reset the bus since that will cause a reset delay
	 * which adds substantially to the boot time -- but if
	 * there is no obp, then when you just reboot
	 * the machine without power cycle, the disks still
	 * have the old parameters (e.g. wide) because no reset is sent
	 * on the bus.
	 */
	if (isp_i_reset_interface(isp, isp->isp_no_obp ? ISP_FORCE_RESET_BUS :
	    ISP_SKIP_STOP_QUEUES) != 0) {
		ISP_MUTEX_EXIT(isp);
		isp_i_log(isp, CE_WARN, "can't reset interface");
		goto fail;
	}
	ISP_MUTEX_EXIT(isp);

	/* report the device we're managing */
	ddi_report_dev(dip);

	/* send info about firmware version to the log file */
	if (isp->isp_bus == ISP_SBUS) {
		isp_i_log(isp, CE_NOTE,
		    "?Firmware Version: v%d.%02d.%d, "
		    "Customer: %d, Product: %d",
		    MSB(isp->isp_maj_min_rev), LSB(isp->isp_maj_min_rev),
		    isp->isp_subminor_rev,
		    MSB(isp->isp_cust_prod), LSB(isp->isp_cust_prod));
	} else {
		isp_i_log(isp, CE_NOTE,
		    "?Firmware Version: v%d.%02d, Customer: %d, Product: %d",
		    isp->isp_maj_min_rev, isp->isp_subminor_rev,
		    MSB(isp->isp_cust_prod), LSB(isp->isp_cust_prod));
	}

	/*
	 * we can now handle I/O requests and/or markers on the
	 * request queue
	 */
	isp->isp_attached = 1;

	/*
	 * in case any marker have been queued before we finished attaching
	 * call update_queue_space routine (which will send any queued markers)
	 */
	mutex_enter(ISP_REQ_MUTEX(isp));
	isp_i_update_queue_space(isp);
	mutex_exit(ISP_REQ_MUTEX(isp));

	return (DDI_SUCCESS);

fail:
	isp_i_log(isp, CE_WARN,
	    "Unable to attach: check for hardware problem");

	/* remove this instance from the isp_head list */
	rw_enter(&isp_global_rwlock, RW_WRITER);
	for (l_isp = s_isp = isp_head; s_isp != NULL;
	    s_isp = s_isp->isp_next) {
		if (s_isp == isp) {
			if (s_isp == isp_head) {
				isp_head = isp->isp_next;
				if (isp_tail == isp) {
					isp_tail = NULL;
				}
			} else {
				if (isp_tail == isp) {
					isp_tail = l_isp;
				}
				l_isp->isp_next = isp->isp_next;
			}
			break;
		}
		l_isp = s_isp;
	}
	rw_exit(&isp_global_rwlock);

	if (isp->isp_kmem_cache != NULL) {
		kmem_cache_destroy(isp->isp_kmem_cache);
	}
	if (isp->isp_cmdarea != NULL) {
		if (bound_handle) {
			(void) ddi_dma_unbind_handle(isp->isp_dmahandle);
		}
		ddi_dma_mem_free(&isp->isp_dma_acc_handle);
	}
	mutex_enter(&isp_global_mutex);
	if (timeout_initted && (isp_head == NULL)) {
		timeout_id_t tid = isp_timeout_id;
		timeout_initted = FALSE;
		isp_timeout_id = 0;
		mutex_exit(&isp_global_mutex);
		(void) untimeout(tid);
	} else {
		mutex_exit(&isp_global_mutex);
	}
	if (interrupt_added) {
		ddi_remove_intr(dip, (uint_t)0, isp->isp_iblock);
	}

	/*
	 * kstat_intr support
	 */
	if (isp->isp_kstat != NULL) {
		kstat_delete(isp->isp_kstat);
	}
	if (isp->isp_err_kstats != NULL) {
		kstat_delete(isp->isp_err_kstats);
		isp->isp_err_kstats = NULL;
	}

	if (mutex_initted) {
		mutex_destroy(ISP_WAITQ_MUTEX(isp));
		mutex_destroy(ISP_REQ_MUTEX(isp));
		mutex_destroy(ISP_RESP_MUTEX(isp));
		mutex_destroy(ISP_HOTPLUG_MUTEX(isp));
		sema_destroy(ISP_MBOX_SEMA(isp));
		cv_destroy(ISP_CV(isp));
		mutex_destroy(ISP_INTR_MUTEX(isp));
		cv_destroy(ISP_INTR_CV(isp));
	}
	if (isp->isp_dmahandle) {
		ddi_dma_free_handle(&isp->isp_dmahandle);
	}

	/* Note: there are no failure paths before mutex_initted is set */
	if (mutex_initted) {
		(void) scsi_hba_detach(dip);
	}
	if (tran != NULL) {
		scsi_hba_tran_free(tran);
	}
	if ((isp->isp_bus == ISP_PCI) &&
	    (isp->isp_pci_config_acc_handle != NULL)) {
		pci_config_teardown(&isp->isp_pci_config_acc_handle);
	}
	if (isp->isp_biu_acc_handle != NULL) {
		ddi_regs_map_free(&isp->isp_biu_acc_handle);
	}
	if (isp->isp_mbox_acc_handle != NULL) {
		ddi_regs_map_free(&isp->isp_mbox_acc_handle);
	}
	if (isp->isp_sxp_acc_handle != NULL) {
		ddi_regs_map_free(&isp->isp_sxp_acc_handle);
	}
	if (isp->isp_risc_acc_handle != NULL) {
		ddi_regs_map_free(&isp->isp_risc_acc_handle);
	}
	ddi_soft_state_free(isp_state, instance);
	return (DDI_FAILURE);
}


/*ARGSUSED*/
static int
isp_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	struct isp		*isp, *nisp;
	scsi_hba_tran_t		*tran;
	uint16_t		slot;

	ASSERT(dip != NULL);
	ISP_DEBUG(NULL, SCSI_DEBUG, "isp: instance %d detach %d called",
	    ddi_get_instance(dip), cmd);

	switch (cmd) {
	case DDI_DETACH:
		return (isp_dr_detach(dip));

	case DDI_SUSPEND:
		if ((tran = ddi_get_driver_private(dip)) == NULL)
			return (DDI_FAILURE);

		isp = TRAN2ISP(tran);
		if (!isp) {
			return (DDI_FAILURE);
		}

		/*
		 * Prevent any new I/O from occuring and then check for any
		 * outstanding I/O.  We could put in a delay, but since all
		 * target drivers should have been suspended before we were
		 * called there should not be any pending commands.
		 */
		ISP_MUTEX_ENTER(isp);
		isp->isp_suspended = TRUE;
		for (slot = 0; slot < ISP_MAX_SLOTS; slot++) {
			if (isp->isp_slots[slot].isp_cmd != NULL) {
				isp->isp_suspended = FALSE;
				ISP_MUTEX_EXIT(isp);
				return (DDI_FAILURE);
			}
		}
		ISP_MUTEX_EXIT(isp);

		mutex_enter(ISP_WAITQ_MUTEX(isp));
		if (isp->isp_waitq_timeout != 0) {
			timeout_id_t tid = isp->isp_waitq_timeout;
			isp->isp_waitq_timeout = 0;
			mutex_exit(ISP_WAITQ_MUTEX(isp));
			(void) untimeout(tid);
		} else {
			mutex_exit(ISP_WAITQ_MUTEX(isp));
		}
		rw_enter(&isp_global_rwlock, RW_WRITER);
		for (nisp = isp_head; nisp; nisp = nisp->isp_next) {
			if (!nisp->isp_suspended) {
				rw_exit(&isp_global_rwlock);
				return (DDI_SUCCESS);
			}
		}
		mutex_enter(&isp_global_mutex);
		rw_exit(&isp_global_rwlock);
		if (isp_timeout_id != 0) {
			timeout_id_t tid = isp_timeout_id;
			isp_timeout_id = 0;
			timeout_initted = FALSE;
			mutex_exit(&isp_global_mutex);
			(void) untimeout(tid);
		} else {
			mutex_exit(&isp_global_mutex);
		}
		ISP_INC32_ERRSTATS(isp, isperr_suspends);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}


static int
isp_dr_detach(dev_info_t *dip)
{
	struct isp		*isp, *nisp, *tisp;
	scsi_hba_tran_t		*tran;
	int			instance = ddi_get_instance(dip);
	int			rval = DDI_SUCCESS;


	if ((tran = ddi_get_driver_private(dip)) == NULL) {
		rval = DDI_FAILURE;
		goto dun;
	}

	if ((isp = TRAN2ISP(tran)) == NULL) {
		rval = DDI_FAILURE;
		goto dun;
	}

	ISP_DEBUG(isp, SCSI_DEBUG, "dr_detach called");

	/*
	 * deallocate reset notify callback list
	 */
	scsi_hba_reset_notify_tear_down(isp->isp_reset_notify_listf);

	/*
	 * Force interrupts OFF and remove handler
	 */
	ISP_WRITE_BIU_REG(isp, &isp->isp_biu_reg->isp_bus_icr,
	    ISP_BUS_ICR_DISABLE_ALL_INTS);
	ddi_remove_intr(dip, (uint_t)0, isp->isp_iblock);

	/*
	 * kstat_intr support
	 */
	if (isp->isp_kstat != NULL) {
		kstat_delete(isp->isp_kstat);
	}
	if (isp->isp_err_kstats != NULL) {
		kstat_delete(isp->isp_err_kstats);
		isp->isp_err_kstats = NULL;
	}

	/*
	 * Remove device instance from the global linked list
	 */
	rw_enter(&isp_global_rwlock, RW_WRITER);
	for (nisp = tisp = isp_head; nisp;
	    tisp = nisp, nisp = nisp->isp_next) {
		if (nisp == isp)
			break;
	}
	ASSERT(nisp);

	if (nisp == isp_head) {
		isp_head = tisp = isp->isp_next;
	} else {
		tisp->isp_next = isp->isp_next;
	}
	if (nisp == isp_tail) {
		isp_tail = tisp;
	}
	rw_exit(&isp_global_rwlock);

	/*
	 * If active, CANCEL watch thread.
	 */
	mutex_enter(&isp_global_mutex);
	if (timeout_initted && (isp_head == NULL)) {
		timeout_id_t tid = isp_timeout_id;
		timeout_initted = FALSE;
		isp_timeout_id = 0;
		mutex_exit(&isp_global_mutex);
		(void) untimeout(tid);
	} else {
		mutex_exit(&isp_global_mutex);
	}

	/*
	 * Release miscellaneous device resources
	 */
	if (isp->isp_kmem_cache) {
		kmem_cache_destroy(isp->isp_kmem_cache);
	}

	if (isp->isp_cmdarea) {
		(void) ddi_dma_unbind_handle(isp->isp_dmahandle);
		ddi_dma_mem_free(&isp->isp_dma_acc_handle);
	}

	if (isp->isp_dmahandle)
		ddi_dma_free_handle(&isp->isp_dmahandle);

	if (isp->isp_bus == ISP_PCI && isp->isp_pci_config_acc_handle) {
		pci_config_teardown(&isp->isp_pci_config_acc_handle);
	}
	if (isp->isp_biu_acc_handle) {
		ddi_regs_map_free(&isp->isp_biu_acc_handle);
	}
	if (isp->isp_mbox_acc_handle) {
		ddi_regs_map_free(&isp->isp_mbox_acc_handle);
	}
	if (isp->isp_sxp_acc_handle) {
		ddi_regs_map_free(&isp->isp_sxp_acc_handle);
	}
	if (isp->isp_risc_acc_handle) {
		ddi_regs_map_free(&isp->isp_risc_acc_handle);
	}

	/*
	 * Remove device MT locks
	 */
	mutex_destroy(ISP_WAITQ_MUTEX(isp));
	mutex_destroy(ISP_REQ_MUTEX(isp));
	mutex_destroy(ISP_RESP_MUTEX(isp));
	mutex_destroy(ISP_HOTPLUG_MUTEX(isp));
	sema_destroy(ISP_MBOX_SEMA(isp));
	cv_destroy(ISP_CV(isp));
	mutex_destroy(ISP_INTR_MUTEX(isp));
	cv_destroy(ISP_INTR_CV(isp));

	/*
	 * Remove properties created during attach()
	 */
	ddi_prop_remove_all(dip);

	/*
	 * Delete the DMA limits, transport vectors and remove the device
	 * links to the scsi_transport layer.
	 * 	-- ddi_set_driver_private(dip, NULL)
	 */
	(void) scsi_hba_detach(dip);

	/*
	 * Free the scsi_transport structure for this device.
	 */
	scsi_hba_tran_free(tran);

	isp->isp_dip = (dev_info_t *)NULL;
	isp->isp_tran = (scsi_hba_tran_t *)NULL;

	ddi_soft_state_free(isp_state, instance);

dun:
	if (rval != DDI_SUCCESS) {
		ISP_DEBUG(NULL, SCSI_DEBUG,
		    "FAILURE: isp_dr_detach returning %d", rval);
	}
	return (rval);
}


/*
 * Hotplug functions for the driver.
 */
static int
isp_scsi_quiesce(dev_info_t *dip)
{
	struct isp *isp;
	scsi_hba_tran_t *tran;

	tran = ddi_get_driver_private(dip);

	if ((tran == NULL) || ((isp = TRAN2ISP(tran)) == NULL)) {
		return (-1);
	}

	return (isp_quiesce_bus(isp));
}


static int
isp_scsi_unquiesce(dev_info_t *dip)
{
	struct isp *isp;
	scsi_hba_tran_t *tran;

	tran = ddi_get_driver_private(dip);

	if ((tran == NULL) || ((isp = TRAN2ISP(tran)) == NULL)) {
		return (-1);
	}

	return (isp_unquiesce_bus(isp));
}


static int
isp_quiesce_bus(struct isp *isp)
{
	int result;
	int outstanding, oldoutstanding = 0;
	clock_t delta;

	mutex_enter(ISP_HOTPLUG_MUTEX(isp));

	/* only start draining if not already being done */
	if ((isp->isp_softstate & ISP_SS_DRAINING) == 0) {
		isp->isp_softstate |= ISP_SS_DRAINING;

		/*
		 * For each LUN send the stop queue mailbox command down.
		 */
		mutex_exit(ISP_HOTPLUG_MUTEX(isp));
		if (isp_mailbox_all(isp, ISP_MBOX_CMD_STOP_QUEUE)) {
			/* the stop-queue failed: start it back up again */
			(void) isp_mailbox_all(isp, ISP_MBOX_CMD_START_QUEUE);
			mutex_enter(ISP_HOTPLUG_MUTEX(isp));
			isp->isp_softstate &= ~ISP_SS_DRAINING;
			isp->isp_softstate |= ISP_SS_DRAIN_ERROR;
			cv_broadcast(ISP_CV(isp));
			mutex_exit(ISP_HOTPLUG_MUTEX(isp));
			return (-1);
		}

		/* Save the outstanding command count */
		if ((oldoutstanding = outstanding =
		    isp_i_outstanding(isp)) == 0) {
			/* That was fast */
			mutex_enter(ISP_HOTPLUG_MUTEX(isp));
			isp->isp_softstate &= ~ISP_SS_DRAINING;
			isp->isp_softstate |= ISP_SS_QUIESCED;
			cv_broadcast(ISP_CV(isp));
			mutex_exit(ISP_HOTPLUG_MUTEX(isp));
			return (0);
		}
		mutex_enter(ISP_HOTPLUG_MUTEX(isp));
	}

	/*
	 * Here's how this works.  Pay attention as it's a bit complicated.
	 *
	 * The first thread (isp->isp_hotplug_waiting == 0) in goes into the
	 * poll loop and runs the show. Any additional threads
	 * (isp->isp_hotplug_waiting !=0) go into a deep sleep.  Every
	 * ISP_BUS_DRAIN_TIME the polling thread wakes up and checks if the
	 * drain has completed.  If it has, it cleans up, clears the
	 * ISP_SS_DRAINING flag, wakes up all the other threads, and returns
	 * success.
	 *
	 * If the poll thread wakes up due to an interrupt, it wakes up the
	 * next thread and returns EINTR.  The next thread takes over the
	 * polling duties.  If there is no next thread, it aborts the operation
	 * and re-starts the ISP chip.
	 */
	if (isp->isp_hotplug_waiting++) {
		/* no need for us to do this, so we'll wait */
		result = cv_wait_sig(ISP_CV(isp), ISP_HOTPLUG_MUTEX(isp));
	}

	/*
	 * so now we are the only thread doing the draining, so we'll wait
	 * for things to drain or we are another thread and the draining
	 * has finished
	 */
	if (isp->isp_softstate & ISP_SS_DRAINING) {
		delta = ISP_BUS_DRAIN_TIME * drv_usectohz(MICROSEC);
		result = cv_reltimedwait_sig(ISP_CV(isp),
		    ISP_HOTPLUG_MUTEX(isp), delta, TR_CLOCK_TICK);
		mutex_exit(ISP_HOTPLUG_MUTEX(isp));
		while ((result == -1) &&
		    ((outstanding = isp_i_outstanding(isp)) != 0)) {
			mutex_enter(ISP_HOTPLUG_MUTEX(isp));
			/* Timeout -- re-poll */
			if (outstanding == oldoutstanding &&
			    oldoutstanding != 0) {
				/*
				 * If nothing at all changed for a whole
				 * ISP_BUS_DRAIN_TIME, something must be
				 * hosed so abort everyone
				 */
				mutex_exit(ISP_HOTPLUG_MUTEX(isp));
				(void) isp_mailbox_all(isp,
				    ISP_MBOX_CMD_START_QUEUE);
				mutex_enter(ISP_HOTPLUG_MUTEX(isp));
				isp->isp_softstate &= ~ISP_SS_DRAINING;
				isp->isp_softstate |= ISP_SS_DRAIN_ERROR;
				cv_broadcast(ISP_CV(isp));
				isp->isp_hotplug_waiting --;
				mutex_exit(ISP_HOTPLUG_MUTEX(isp));
				(void) isp_i_check_waitQ(isp);
				return (EIO);
			}
			oldoutstanding = outstanding;
			result = cv_reltimedwait_sig(ISP_CV(isp),
			    ISP_HOTPLUG_MUTEX(isp), delta, TR_CLOCK_TICK);
			mutex_exit(ISP_HOTPLUG_MUTEX(isp));
		}
		mutex_enter(ISP_HOTPLUG_MUTEX(isp));
		if ((result != 0) && (outstanding == 0)) {
			/* Done.  Wake up the others */
			isp->isp_hotplug_waiting --;
			isp->isp_softstate &= ~ISP_SS_DRAINING;
			isp->isp_softstate |= ISP_SS_QUIESCED;
			cv_broadcast(ISP_CV(isp));
			mutex_exit(ISP_HOTPLUG_MUTEX(isp));
			return (0);
		}
	}

	/* clear flag saying we're handling the draining duties */
	if (--isp->isp_hotplug_waiting == 0) {
		/* Last one out cleans up. */
		if (result == 0) {
			/*
			 * quiesce has been interrupted and no waiters.
			 * Restart the queues after reseting ISP_SS_DRAINING;
			 */
			ISP_DEBUG2(isp, SCSI_DEBUG,
			    "isp_quiesce: abort QUIESCE\n");
			isp->isp_softstate &= ~ISP_SS_DRAINING;
			mutex_exit(ISP_HOTPLUG_MUTEX(isp));
			(void) isp_mailbox_all(isp, ISP_MBOX_CMD_START_QUEUE);
			(void) isp_i_check_waitQ(isp);
			return (EINTR);
		}
		if (isp->isp_softstate & ISP_SS_DRAIN_ERROR) {
			isp->isp_softstate &= ~ISP_SS_DRAIN_ERROR;
			mutex_exit(ISP_HOTPLUG_MUTEX(isp));
			return (EINTR);
		}
	} else {
		/* we are not the last one out */
		if (result == 0) {
			/*
			 * Interrupted but others waiting, wake a replacement
			 */
			cv_signal(ISP_CV(isp));
			mutex_exit(ISP_HOTPLUG_MUTEX(isp));
			return (EINTR);
		}
		if (isp->isp_softstate & ISP_SS_DRAIN_ERROR) {
			mutex_exit(ISP_HOTPLUG_MUTEX(isp));
			return (EIO);
		}
	}

	/*
	 * the bus quiesce has worked
	 */

	mutex_exit(ISP_HOTPLUG_MUTEX(isp));
	ISP_DEBUG3(isp, SCSI_DEBUG, "isp_quiesce: bus has been QUIESCED\n");
	return (0);
}


/*
 * unquiesce the SCSI bus -- return 0 for success, else errno value
 */
static int
isp_unquiesce_bus(struct isp *isp)
{
	int		rval = 0;		/* default return value */

	ASSERT(isp != NULL);

	mutex_enter(ISP_HOTPLUG_MUTEX(isp));
	if (isp->isp_softstate & ISP_SS_DRAINING) {
		mutex_exit(ISP_HOTPLUG_MUTEX(isp));
		/* EBUSY would be more appropriate */
		rval = EIO;
		goto dun;
	}
	mutex_exit(ISP_HOTPLUG_MUTEX(isp));

	(void) isp_mailbox_all(isp, ISP_MBOX_CMD_START_QUEUE);

	mutex_enter(ISP_HOTPLUG_MUTEX(isp));
	isp->isp_softstate &= ~ISP_SS_QUIESCED;
	mutex_exit(ISP_HOTPLUG_MUTEX(isp));

	(void) isp_i_check_waitQ(isp);

	ISP_DEBUG3(isp, SCSI_DEBUG,
	    "isp_unquiesce_bus: bus has been UNQUIESCED\n");
dun:
	if (rval != 0) {
		ISP_DEBUG2(isp, SCSI_DEBUG,
		    "FAILURE: isp_unquiesce_bus returning %d", rval);
	}

	return (rval);
}


/*
 * Get the number of executing commands from the ISP.
 */
static int
isp_i_outstanding(struct isp *isp)
{
	struct isp_mbox_cmd	mbox_cmd;
	int			cmd_count = -1;		/* default value */


	ISP_MUTEX_ENTER(isp);
	/* get command count from firmware */
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_GET_ISP_STAT) ==
	    0) {
		cmd_count = ISP_MBOX_RETURN_REG(&mbox_cmd, 2);
	}
	ISP_MUTEX_EXIT(isp);
	return (cmd_count);
}


/*
 * send start or stop queue cmd to all targets
 */
/*ARGSUSED*/
static int
isp_mailbox_all(struct isp *isp, ushort_t op_code)
{
	ushort_t		i, j;			/* tgt/LUN indexes */
	struct isp_mbox_cmd	mbox_cmd;		/* mbox cmd to use */
	int			res = 0;		/* default result */


	ISP_MUTEX_ENTER(isp);
	for (i = 0; i < NTARGETS_WIDE; i++) {
		ushort_t	luns = isp->isp_max_lun[i];

		for (j = 0; j < luns; j++) {
			if ((res = isp_i_mbox_cmd_start(isp, &mbox_cmd,
			    op_code, TGT_N_LUN(i, j))) != 0) {
				/* an error, so stop here */
				break;
			}
		}
	}
	ISP_MUTEX_EXIT(isp);

	return (res);
}


/*
 * Function name : isp_i_download_fw ()
 *
 * Return Values : 0  on success.
 *		   -1 on error.
 *
 * Description	 : Uses the request and response queue iopb memory for dma.
 *		   Verifies that fw fits in iopb memory.
 *		   Verifies fw checksum.
 *		   Copies firmware to iopb memory.
 *		   Sends mbox cmd to ISP to (down) Load RAM.
 *		   After command is done, resets ISP which starts it
 *			executing from new f/w.
 *
 * Context	 : Can be called ONLY from user context.
 *		 : NOT MT-safe.
 *		 : Driver must be in a quiescent state.
 */
static int
isp_i_download_fw(struct isp *isp,
    ushort_t risc_addr, ushort_t *fw_addrp, ushort_t fw_len)
{
	int rval			= -1;
	int fw_len_bytes		= (int)fw_len *
	    sizeof (unsigned short);
	ushort_t checksum		= 0;
	int found			= 0;
	char *string			= " Firmware  Version ";
	int string_len = strlen(string);
	char *startp;
	char buf[10];
	int length;
	int major_rev, minor_rev;
	struct isp_mbox_cmd mbox_cmd;
	ushort_t i;


	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));

	ISP_DEBUG3(isp, SCSI_DEBUG,
	    "isp_i_download_fw start: risc = 0x%x fw = 0x%p, fw_len =0x%x",
	    risc_addr, (void *)fw_addrp, fw_len);

	/*
	 * Since we use the request and response queue iopb
	 * we check to see if f/w will fit in this memory.
	 * This iopb memory presently is 32k and the f/w is about
	 * 13k but check the headers for definite values.
	 */
	if (fw_len_bytes >
	    ISP_QUEUE_SIZE(isp_max_requests, isp_max_responses)) {
		isp_i_log(isp, CE_WARN,
		    "Firmware (0x%x) should be < 0x%lx bytes",
		    fw_len_bytes, ISP_QUEUE_SIZE(isp_max_requests,
		    isp_max_responses));
		goto fail;
	}

	/*
	 * verify checksum equals zero
	 */
	for (i = 0; i < fw_len; i++) {
		checksum += fw_addrp[i];
	}
	if (checksum != 0) {
		isp_i_log(isp, CE_WARN, "Firmware checksum incorrect");
		goto fail;
	}

	/*
	 * get new firmware version numbers
	 *
	 * XXX: this searches *THE WHOLE FIRMWARE* for the rev string!
	 * and makes mucho assumptions about how the string looks -- this
	 * really isn't very portable
	 */
	startp = (char *)fw_addrp;
	length = fw_len_bytes;
	while (length > string_len) {
		if (strncmp(startp, string, string_len) == 0) {
			found = 1;
			break;
		}
		startp++;
		length--;
	}

	if (!found) {
		/* XXX: is this right?? */
		isp_i_log(isp, CE_WARN, "can't find firmware rev. string");
		goto done;
	}

	startp += strlen(string);
	(void) strncpy(buf, startp, 5);
	buf[2] = buf[5] = '\0';
	startp = buf;
	major_rev = stoi(&startp);
	startp++;
	minor_rev = stoi(&startp);

	ISP_DEBUG4(isp, SCSI_DEBUG, "New f/w: major = %d minor = %d",
	    major_rev, minor_rev);

	/*
	 * reset and initialize isp chip
	 */
	if (isp_i_reset_init_chip(isp) != 0) {
		isp_i_log(isp, CE_WARN, "reset/init ISP chip failed");
		goto fail;
	}

	/*
	 * copy firmware to iopb memory that was allocated for queues.
	 */
	ASSERT(fw_len_bytes <=
	    ISP_QUEUE_SIZE(isp_max_requests, isp_max_responses));
	ASSERT(fw_len_bytes > 0);
	ISP_COPY_OUT_DMA_16(isp, fw_addrp, isp->isp_request_base, fw_len);
	/* sync memory */
	(void) ddi_dma_sync(isp->isp_dmahandle, (off_t)0, (size_t)fw_len_bytes,
	    DDI_DMA_SYNC_FORDEV);

	ISP_DEBUG4(isp, SCSI_DEBUG, "Load Ram");
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_LOAD_RAM,
	    risc_addr, MSW(isp->isp_request_dvma),
	    LSW(isp->isp_request_dvma), fw_len) != 0) {
		isp_i_log(isp, CE_WARN, "Load ram failed");
		if (isp_debug_state) {
			isp_i_print_state(CE_NOTE, isp);
		}
		goto fail;
	}

	/*
	 * reset the ISP chip so it starts with the new firmware
	 */
	ISP_DEBUG4(isp, SCSI_DEBUG, "Resetting Chip");
	ISP_WRITE_RISC_HCCR(isp, ISP_HCCR_CMD_RESET);
	drv_usecwait(ISP_CHIP_RESET_BUSY_WAIT_TIME);
	ISP_WRITE_RISC_HCCR(isp, ISP_HCCR_CMD_RELEASE);

	/*
	 * Start ISP firmware up.
	 */
	ISP_DEBUG4(isp, SCSI_DEBUG, "Starting firmware");
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_START_FW,
	    risc_addr) != 0) {
		isp_i_log(isp, CE_WARN,
		    "start firmware mailbox command failed");
		goto fail;
	}

	/*
	 * set clock rate
	 */
	ISP_DEBUG4(isp, SCSI_DEBUG, "Setting clock rate");
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_SET_CLOCK_RATE,
	    isp->isp_clock_frequency) != 0) {
		isp_i_log(isp, CE_WARN, "can't set clock rate");
		goto fail;
	}

	/*
	 * get ISP Ram firmware version numbers
	 */
	ISP_DEBUG4(isp, SCSI_DEBUG, "Getting RAM info");
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_ABOUT_PROM) !=
	    0) {
		isp_i_log(isp, CE_WARN, "can't get RAM info.");
		goto fail;
	}

	isp->isp_maj_min_rev = ISP_MBOX_RETURN_REG(&mbox_cmd, 1);
	isp->isp_subminor_rev = ISP_MBOX_RETURN_REG(&mbox_cmd, 2);
	isp->isp_cust_prod = ISP_MBOX_RETURN_REG(&mbox_cmd, 3);

	if (isp->isp_bus == ISP_SBUS) {
		ISP_DEBUG3(isp, SCSI_DEBUG,
		    "Downloaded f/w: major=%d minor=%d subminor=%d",
		    MSB(isp->isp_maj_min_rev), LSB(isp->isp_maj_min_rev),
		    isp->isp_subminor_rev);
	} else {
		ISP_DEBUG3(isp, SCSI_DEBUG,
		    "Downloaded f/w: major=%d minor=%d",
		    isp->isp_maj_min_rev, isp->isp_subminor_rev);
	}

done:
	rval = 0;

fail:
	ISP_DEBUG4(isp, SCSI_DEBUG,
	    "isp_i_download_fw: 0x%x 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox0),
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox1),
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox2),
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox3),
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox4),
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox5));

	ISP_DEBUG3(isp, SCSI_DEBUG, "isp_i_download_fw end: rval = %d", rval);

	bzero((caddr_t)isp->isp_request_base,
	    ISP_QUEUE_SIZE(isp_max_requests, isp_max_responses));

	ISP_INC32_ERRSTATS(isp, isperr_firmware_loads);
	return (rval);
}


/*
 * Function name : isp_i_initcap
 *
 * Return Values : NONE
 * Description	 : Initializes the default target capabilites and
 *		   Sync Rates.
 *
 * Context	 : Called from the user thread through attach.
 *
 */
static void
isp_i_initcap(struct isp *isp, int start_tgt, int end_tgt)
{
	int i;
	ushort_t cap, synch;

	for (i = start_tgt; i <= end_tgt; i++) {
		/*
		 * force to narrow and async on startup
		 * then let target drivers reset wide & synch later
		 */
		cap = (ISP_CAP_RENEGO_ERROR | ISP_CAP_FORCE_NARROW);
		synch = 0;
		if (isp->isp_target_scsi_options[i] & SCSI_OPTIONS_DR) {
			cap |= ISP_CAP_DISCONNECT;
		}
		if (isp->isp_target_scsi_options[i] & SCSI_OPTIONS_PARITY) {
			cap |= ISP_CAP_PARITY;
		}
		if (isp->isp_target_scsi_options[i] & SCSI_OPTIONS_SYNC) {
			cap |= ISP_CAP_SYNC;
			if (isp->isp_target_scsi_options[i] &
			    SCSI_OPTIONS_FAST20) {
				synch = ISP_20M_SYNC_PARAMS;
			} else if (isp->isp_target_scsi_options[i] &
			    SCSI_OPTIONS_FAST) {
				synch = ISP_10M_SYNC_PARAMS;
			} else {
				synch = ISP_5M_SYNC_PARAMS;
			}
		} else {
			cap |= ISP_CAP_FORCE_ASYNC;
		}
		isp->isp_cap[i] = cap;
		isp->isp_synch[i] = synch;
	}
	ISP_DEBUG3(isp, SCSI_DEBUG, "init_cap tgts %d-%d: cap=0x%x, sync=0x%x",
	    start_tgt, end_tgt, cap, synch);
}


/*
 * Function name : isp_i_commoncap
 *
 * Return Values : TRUE - capability exists  or could be changed
 *		   FALSE - capability does not exist or could not be changed
 *		   value - current value of capability
 * Description	 : sets a capability for a target or all targets
 *		   or returns the current value of a capability
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
/*
 * SCSA host adapter get/set capability routines.
 * isp_scsi_getcap and isp_scsi_setcap are wrappers for isp_i_commoncap.
 */
static int
isp_i_commoncap(struct scsi_address *ap, char *cap,
    int val, int tgtonly, int doset)
{
	struct isp *isp = ADDR2ISP(ap);
	ushort_t tgt = ap->a_target;
	int cidx;
	int i;
	int rval = FALSE;
	int update_isp = 0;
	ushort_t	start, end;

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());

	if (cap == (char *)0) {
		ISP_DEBUG2(isp, SCSI_DEBUG, "isp_i_commoncap: invalid arg");
		return (rval);
	}

	cidx = scsi_hba_lookup_capstr(cap);
	if (cidx == -1) {
		return (UNDEFINED);
	}

	if (doset) {
		ISP_INC32_ERRSTATS(isp, isperr_set_capability);
	} else {
		ISP_INC32_ERRSTATS(isp, isperr_get_capability);
	}

	ISP_MUTEX_ENTER(isp);

	/*
	 * Process setcap request.
	 */
	if (doset) {
		/*
		 * At present, we can only set binary (0/1) values
		 */
		switch (cidx) {
		case SCSI_CAP_DMA_MAX:
		case SCSI_CAP_MSG_OUT:
		case SCSI_CAP_PARITY:
		case SCSI_CAP_UNTAGGED_QING:
		case SCSI_CAP_LINKED_CMDS:
		case SCSI_CAP_RESET_NOTIFICATION:
			/*
			 * None of these are settable via
			 * the capability interface.
			 */
			break;
		case SCSI_CAP_DISCONNECT:
			if ((isp->isp_target_scsi_options[tgt] &
			    SCSI_OPTIONS_DR) == 0) {
				break;
			} else if (tgtonly) {
				if (val) {
					isp->isp_cap[tgt] |=
					    ISP_CAP_DISCONNECT;
				} else {
					isp->isp_cap[tgt] &=
					    ~ISP_CAP_DISCONNECT;
				}
			} else {
				for (i = 0; i < NTARGETS_WIDE; i++) {
					if (val) {
						isp->isp_cap[i] |=
						    ISP_CAP_DISCONNECT;
					} else {
						isp->isp_cap[i] &=
						    ~ISP_CAP_DISCONNECT;
					}
				}
			}
			rval = TRUE;
			update_isp++;
			break;
		case SCSI_CAP_SYNCHRONOUS:
			if ((isp->isp_target_scsi_options[tgt] &
			    SCSI_OPTIONS_SYNC) == 0) {
				break;
			} else if (tgtonly) {
				if (val) {
					isp->isp_cap[tgt] &=
					    ~ISP_CAP_FORCE_ASYNC;
					isp->isp_cap[tgt] |= ISP_CAP_SYNC;
				} else {
					isp->isp_cap[tgt] &= ~ISP_CAP_SYNC;
					isp->isp_cap[tgt] |=
					    ISP_CAP_FORCE_ASYNC;
				}
			} else {
				for (i = 0; i < NTARGETS_WIDE; i++) {
					if (val) {
						isp->isp_cap[i] &=
						    ~ISP_CAP_FORCE_ASYNC;
						isp->isp_cap[i] |=
						    ISP_CAP_SYNC;
					} else {
						isp->isp_cap[i] &=
						    ~ISP_CAP_SYNC;
						isp->isp_cap[i] |=
						    ISP_CAP_FORCE_ASYNC;
					}
				}
			}
			rval = TRUE;
			update_isp++;
			break;
		case SCSI_CAP_TAGGED_QING:
			if ((isp->isp_target_scsi_options[tgt] &
			    SCSI_OPTIONS_DR) == 0 ||
			    (isp->isp_target_scsi_options[tgt] &
			    SCSI_OPTIONS_TAG) == 0) {
				break;
			} else if (tgtonly) {
				if (val) {
					isp->isp_cap[tgt] |= ISP_CAP_TAG;
				} else {
					isp->isp_cap[tgt] &= ~ISP_CAP_TAG;
				}
			} else {
				for (i = 0; i < NTARGETS_WIDE; i++) {
					if (val) {
						isp->isp_cap[i] |= ISP_CAP_TAG;
					} else {
						isp->isp_cap[i] &= ~ISP_CAP_TAG;
					}
				}
			}
			rval = TRUE;
			update_isp++;
			break;
		case SCSI_CAP_WIDE_XFER:
			if ((isp->isp_target_scsi_options[tgt] &
			    SCSI_OPTIONS_WIDE) == 0) {
				break;
			} else if (tgtonly) {
				if (val) {
					isp->isp_cap[tgt] &=
					    ~ISP_CAP_FORCE_NARROW;
					isp->isp_cap[tgt] |= ISP_CAP_WIDE;
				} else {
					isp->isp_cap[tgt] &= ~ISP_CAP_WIDE;
					isp->isp_cap[tgt] |=
					    ISP_CAP_FORCE_NARROW;
				}
			} else {
				for (i = 0; i < NTARGETS_WIDE; i++) {
					if (val) {
						isp->isp_cap[i] &=
						    ~ISP_CAP_FORCE_NARROW;
						isp->isp_cap[i] |=
						    ISP_CAP_WIDE;
					} else {
						isp->isp_cap[i] &=
						    ~ISP_CAP_WIDE;
						isp->isp_cap[i] |=
						    ISP_CAP_FORCE_NARROW;
					}
				}
			}
			rval = TRUE;
			update_isp++;
			break;
		case SCSI_CAP_INITIATOR_ID:
			if (val < NTARGETS_WIDE) {
				struct isp_mbox_cmd mbox_cmd;

				isp->isp_initiator_id = (ushort_t)val;

				/*
				 * set Initiator SCSI ID
				 */
				if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
				    ISP_MBOX_CMD_SET_SCSI_ID,
				    isp->isp_initiator_id) == 0) {
					rval = TRUE;
				}
			}
			break;
		case SCSI_CAP_ARQ:
			if (tgtonly) {
				if (val) {
					isp->isp_cap[tgt] |= ISP_CAP_AUTOSENSE;
				} else {
					isp->isp_cap[tgt] &= ~ISP_CAP_AUTOSENSE;
				}
			} else {
				for (i = 0; i < NTARGETS_WIDE; i++) {
					if (val) {
						isp->isp_cap[i] |=
						    ISP_CAP_AUTOSENSE;
					} else {
						isp->isp_cap[i] &=
						    ~ISP_CAP_AUTOSENSE;
					}
				}
			}
			rval = TRUE;
			update_isp++;
			break;

		case SCSI_CAP_QFULL_RETRIES:
			if (tgtonly) {
				start = end = tgt;
			} else {
				start = 0;
				end = NTARGETS_WIDE;
			}
			rval = isp_i_handle_qfull_cap(isp, start,
			    end,
			    val, ISP_SET_QFULL_CAP,
			    SCSI_CAP_QFULL_RETRIES);
			break;
		case SCSI_CAP_QFULL_RETRY_INTERVAL:
			if (tgtonly) {
				start = end = tgt;
			} else {
				start = 0;
				end = NTARGETS_WIDE;
			}
			rval = isp_i_handle_qfull_cap(isp, start,
			    end,
			    val, ISP_SET_QFULL_CAP,
			    SCSI_CAP_QFULL_RETRY_INTERVAL);
			break;

		default:
			ISP_DEBUG2(isp, SCSI_DEBUG,
			    "isp_i_setcap: unsupported cap \"%s\" (%d)",
			    cap, cidx);
			rval = UNDEFINED;
			break;
		}

		ISP_DEBUG3(isp, SCSI_DEBUG,
		    "setcap: cap=%s,val=0x%x,tgtonly=0x%x,doset=0x%x,rval=%d",
		    cap, val, tgtonly, doset, rval);

		/*
		 * now update the isp, if necessary
		 */
		if ((rval == TRUE) && update_isp) {
			ushort_t start_tgt, end_tgt;

			if (tgtonly) {
				start_tgt = end_tgt = tgt;
				isp->isp_prop_update |= 1 << tgt;
			} else {
				/* update all targets */
				start_tgt = 0;
				end_tgt = NTARGETS_WIDE;
				isp->isp_prop_update = 0xffff;
			}
			if (isp_i_updatecap(isp, start_tgt, end_tgt,
			    FALSE) != 0) {
				/*
				 * if we can't update the capabilities
				 * in the isp, we are hosed ???
				 */
				isp_i_log(isp, CE_WARN, "can't update "
				    "capabilities: hardware error?");

				mutex_exit(ISP_REQ_MUTEX(isp));
				isp_i_fatal_error(isp, ISP_FIRMWARE_ERROR);
				mutex_enter(ISP_REQ_MUTEX(isp));

				rval = FALSE;
			}
		}

	/*
	 * Process getcap request.
	 */
	} else {
		switch (cidx) {
		case SCSI_CAP_DMA_MAX:
			rval = (int)dma_ispattr.dma_attr_maxxfer;
			break;
		case SCSI_CAP_MSG_OUT:
			rval = TRUE;
			break;
		case SCSI_CAP_DISCONNECT:
			if ((isp->isp_target_scsi_options[tgt] &
			    SCSI_OPTIONS_DR) == 0) {
				break;
			} else if (tgtonly &&
			    (isp->isp_cap[tgt] & ISP_CAP_DISCONNECT) == 0) {
				break;
			}
			rval = TRUE;
			break;
		case SCSI_CAP_SYNCHRONOUS:
			if ((isp->isp_target_scsi_options[tgt] &
			    SCSI_OPTIONS_SYNC) == 0) {
				break;
			} else if (tgtonly &&
			    (isp->isp_cap[tgt] & ISP_CAP_SYNC) == 0) {
				break;
			}
			rval = TRUE;
			break;
		case SCSI_CAP_WIDE_XFER:
			if ((isp->isp_target_scsi_options[tgt] &
			    SCSI_OPTIONS_WIDE) == 0) {
				break;
			} else if (tgtonly &&
			    (isp->isp_cap[tgt] & ISP_CAP_WIDE) == 0) {
				break;
			}
			rval = TRUE;
			break;
		case SCSI_CAP_TAGGED_QING:
			if ((isp->isp_target_scsi_options[tgt] &
			    SCSI_OPTIONS_DR) == 0 ||
			    (isp->isp_target_scsi_options[tgt] &
			    SCSI_OPTIONS_TAG) == 0) {
				break;
			} else if (tgtonly &&
			    (isp->isp_cap[tgt] & ISP_CAP_TAG) == 0) {
				break;
			}
			rval = TRUE;
			break;
		case SCSI_CAP_UNTAGGED_QING:
			rval = TRUE;
			break;
		case SCSI_CAP_PARITY:
			if (isp->isp_target_scsi_options[tgt] &
			    SCSI_OPTIONS_PARITY) {
				rval = TRUE;
			}
			break;
		case SCSI_CAP_INITIATOR_ID:
			rval = isp->isp_initiator_id;
			break;
		case SCSI_CAP_ARQ:
			if (isp->isp_cap[tgt] & ISP_CAP_AUTOSENSE) {
				rval = TRUE;
			}
			break;
		case SCSI_CAP_LINKED_CMDS:
			break;
		case SCSI_CAP_RESET_NOTIFICATION:
			rval = TRUE;
			break;
		case SCSI_CAP_QFULL_RETRIES:
			rval = isp_i_handle_qfull_cap(isp, tgt,
			    tgt,
			    0, ISP_GET_QFULL_CAP,
			    SCSI_CAP_QFULL_RETRIES);
			break;
		case SCSI_CAP_QFULL_RETRY_INTERVAL:
			rval = isp_i_handle_qfull_cap(isp, tgt,
			    tgt,
			    0, ISP_GET_QFULL_CAP,
			    SCSI_CAP_QFULL_RETRY_INTERVAL);
			break;
		case SCSI_CAP_CDB_LEN:
			rval = CDB_GROUP5;
			break;
		default:
			ISP_DEBUG3(isp, SCSI_DEBUG,
			    "isp_scsi_getcap: unsupported cap \"%s\" (%d)",
			    cap, cidx);
			rval = UNDEFINED;
			break;
		}
		ISP_DEBUG3(isp, SCSI_DEBUG,
		    "get cap: cap=%s,val=0x%x,tgtonly=0x%x,doset=0x%x,rval=%d",
		    cap, val, tgtonly, doset, rval);
	}
	ISP_MUTEX_EXIT(isp);

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());

	return (rval);
}

/*
 * Function name : isp_scsi_getcap(), isp_scsi_setcap()
 *
 * Return Values : see isp_i_commoncap()
 * Description	 : wrappers for isp_i_commoncap()
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_scsi_getcap(struct scsi_address *ap, char *cap, int whom)
{
	int e;
	e = isp_i_commoncap(ap, cap, 0, whom, 0);
	return (e);
}

static int
isp_scsi_setcap(struct scsi_address *ap, char *cap, int value, int whom)
{
	int e;
	e = isp_i_commoncap(ap, cap, value, whom, 1);
	return (e);
}

/*
 * Function name : isp_i_updatecap()
 *
 * Return Values : -1	failed.
 *		    0	success
 *
 * Description	 : sync's the isp target parameters with the desired
 *		   isp_caps for the specified target range
 *
 *		   does not display an error message if getting or
 *		   setting cap fails, so calling routine must handle that
 *
 *		   iterates from start_tgt to one less than end_target,
 *		   but start_tgt is *always* done (so going from N to N
 *		   works)
 *
 *		   (in practice, called to update either one target or
 *		    all targets)
 *
 *		   if the force flag is passed in then force wide/sync
 *		   renegotiation
 *
 *		   Note that no error messages are produced, so it is up
 *		   to the caller to print a message (or not)
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_i_updatecap(struct isp *isp, ushort_t start_tgt, ushort_t end_tgt,
    int force)
{
	ushort_t		fw_cap;
	ushort_t		fw_synch;
	struct isp_mbox_cmd	mbox_cmd;
	int			i;
	int			rval = -1;


	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));

	if ((start_tgt >= NTARGETS_WIDE) || (end_tgt > NTARGETS_WIDE)) {
		isp_i_log(isp, CE_WARN, "start_tgt = %d, end_tgt = %d: "
		    "Invalid target.", start_tgt, end_tgt);
		return (rval);
	}

	i = start_tgt;

	do {
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_GET_TARGET_CAP, TGT_N_LUN(i, 0)) != 0) {
			ISP_DEBUG2(isp, SCSI_DEBUG,
			    "mbox cmd failed getting tgt %d caps", i);
			goto fail;
		}

		fw_cap = ISP_MBOX_RETURN_REG(&mbox_cmd, 2);
		fw_synch = ISP_MBOX_RETURN_REG(&mbox_cmd, 3);

		ISP_DEBUG3(isp, SCSI_DEBUG,
		    "updatecap:tgt=%d:fw_cap=0x%x,isp_cap=0x%x,"
		    "fw_synch=0x%x,isp_synch=0x%x",
		    i, fw_cap, isp->isp_cap[i], fw_synch, isp->isp_synch[i]);

		/*
		 * enable or disable ERRSYNC
		 */
		if (isp->isp_cap[i] & (ISP_CAP_WIDE | ISP_CAP_SYNC)) {
			/* need to sync on error if in wide or sync mode */
			isp->isp_cap[i] |= ISP_CAP_RENEGO_ERROR;
		} else {
			isp->isp_cap[i] &= ~ISP_CAP_RENEGO_ERROR;
		}

		/*
		 * if requested to do so tell firmware to renegotiate
		 * wide/sync
		 */
		if (force) {
			isp->isp_cap[i] |= ISP_CAP_RENEGOT_WIDE_SYNC;
		}

		/*
		 * Set isp cap if different from ours.
		 */
		if ((isp->isp_cap[i] != fw_cap) ||
		    (isp->isp_synch[i] != fw_synch)) {

			ISP_DEBUG4(isp, SCSI_DEBUG,
			    "setting tgt=%d, cap=0x%x(fw=0x%x), "
			    "synch=0x%x(fw=0x%x)",
			    i, isp->isp_cap[i], fw_cap, isp->isp_synch[i],
			    fw_synch);

			/* set target/lun capabilities */
			if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
			    ISP_MBOX_CMD_SET_TARGET_CAP, TGT_N_LUN(i, 0),
			    isp->isp_cap[i], isp->isp_synch[i]) != 0) {
				ISP_DEBUG2(isp, SCSI_DEBUG,
				    "mbox cmd failed setting tgt %d caps", i);
				goto fail;
			}
		}

	} while (++i < end_tgt);

	rval = 0;
fail:
	return (rval);
}


/*
 * Function name : isp_i_update_sync_prop()
 *
 * Description	 : called  when isp reports renegotiation
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_i_update_sync_prop(struct isp *isp, struct isp_cmd *sp)
{
	ushort_t cap, synch;
	struct isp_mbox_cmd mbox_cmd;
	int target;


	ASSERT(isp != NULL);
	ASSERT(sp != NULL);

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));

	target = TGT(sp);

	ISP_DEBUG3(isp, SCSI_DEBUG, "tgt %d.%d: Negotiated new rate", target,
	    LUN(sp));

	/*
	 * Get new rate from ISP and save for later
	 * chip resets or scsi bus resets.
	 */
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_GET_TARGET_CAP,
	    TGT_N_LUN(target, 0)) != 0) {
		/* can't get target/lun cap!? */
		return;
	}

	cap = ISP_MBOX_RETURN_REG(&mbox_cmd, 2);
	synch = ISP_MBOX_RETURN_REG(&mbox_cmd, 3);

	if (!(cap & ISP_CAP_WIDE)) {
		if (isp->isp_backoff & (1 << target)) {
			isp_i_log(isp, CE_WARN,
			    "Target %d disabled wide SCSI mode", target);
			isp->isp_backoff &= ~(1 << target);
		}
	}

	ISP_DEBUG4(isp, SCSI_DEBUG,
	"tgt=%d: cap=0x%x, isp_cap=0x%x, synch=0x%x, isp_synch=0x%x",
	    target, cap, isp->isp_cap[target], synch,
	    isp->isp_synch[target]);

	isp->isp_cap[target] = cap;
	isp->isp_synch[target] = synch;
	isp->isp_prop_update |= 1 << target;
}


/*
 * Function name : isp_i_update_props()
 *
 * Description	 : Creates/modifies/removes a target sync mode speed,
 *		   wide, and TQ properties
 *		   If offset is 0 then asynchronous mode is assumed and the
 *		   property is removed, if it existed.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_i_update_props(struct isp *isp, int tgt, ushort_t cap, ushort_t synch)
{
	int		xfer_speed = 0;
	int		offset = OFFSET_MASK(synch);
	uint16_t	period = PERIOD_MASK(synch);


	ASSERT(isp != NULL);
	ASSERT((tgt >= 0) && (tgt < NTARGETS_WIDE));
	ASSERT(!mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(!mutex_owned(ISP_RESP_MUTEX(isp)));

	/*
	 * update synchronous speed integer property
	 */
	if (period != 0) {
		/*
		 * XXX: could this mpy be more efficient, i.e. have a
		 * pre-computed constant or macro to convert to
		 * freq from period
		 *
		 * assume that if period is non-zero then 2 and 4
		 * times period are also non-zero (shoule be ok)
		 */
		if (cap & ISP_CAP_WIDE) {
			/* double xfer speed if wide has been enabled */
			xfer_speed = (1000 * 1000)/(period << 1);
		} else {
			xfer_speed = (1000 * 1000)/(period << 2);
		}
	}
	/* if offset is zero then remove property else set it */
	isp_i_update_this_prop(isp, "target%x-sync-speed", tgt, xfer_speed,
	    sizeof (xfer_speed), offset);

	/*
	 * update tagged queueing and wide boolean properties
	 */
	isp_i_update_this_prop(isp, "target%x-TQ", tgt, 0, 0,
	    cap & ISP_CAP_TAG);
	isp_i_update_this_prop(isp, "target%x-wide", tgt, 0, 0,
	    cap & ISP_CAP_WIDE);
}


/*
 * Creates/updates/removes a boolean/int property
 */
static void
isp_i_update_this_prop(struct isp *isp, char *prop_fmt, int tgt,
    int value, int size, int flag)
{
	int		err;
	dev_info_t	*dip;			/* nexus dip */
	char		property[32];		/* 32 is large enough */



	ASSERT(isp != NULL);
	ASSERT(!mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(!mutex_owned(ISP_RESP_MUTEX(isp)));

	dip = isp->isp_dip;
	ASSERT(dip != NULL);

	/* put tgt in property string */
	(void) sprintf(property, prop_fmt, tgt);

	ISP_DEBUG3(isp, SCSI_DEBUG,
	    "isp_i_update_this_prop: %s=0x%x, size=0x%x, flag=0x%x",
	    property, value, size, flag);

	/* does caller want to remove the property (flag is zero) ?? */
	if (flag == 0) {
		(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, property);
		return;
	}

	/* is property boolean (size is 0) ?? */
	if (size == 0) {
		/*
		 * Use ddi_prop_create() for boolean properties
		 * because the newer interface ddi_prop_update_*()
		 * can't deal with boolean props.
		 */
		err = ddi_prop_create(DDI_DEV_T_NONE, dip, 0, property,
		    NULL, 0);
	} else {
		err = ddi_prop_update_int(DDI_DEV_T_NONE, dip,
		    property, value);
	}
	if (err != DDI_PROP_SUCCESS) {
		ISP_DEBUG2(isp, SCSI_DEBUG,
		    "isp_i_update_this_prop: can't create/update "
		    "property \"%s\"", property);
	}
}


/*
 * Function name : isp_i_handle_qfull_cap()
 *
 * Return Values : FALSE - if setting qfull capability failed
 *		   TRUE	 - if setting qfull capability succeeded
 *		   -1    - if getting qfull capability succeeded
 *		   value - if getting qfull capability succeeded
 *
 * Descrption :		called to get or set the qfull retry interval
 *			or retry count (depending on flags/params)
 *
 * Mutexes :		Must called with response and request mutex held
 */
static int
isp_i_handle_qfull_cap(struct isp *isp, ushort_t start, ushort_t end,
	int val, int flag_get_set, int flag_retry_interval)
{
	struct isp_mbox_cmd	mbox_cmd;
	short			rval = 0;		/* default return */
	ushort_t		cmd;
	ushort_t		value = (ushort_t)val;
	ushort_t		tgt;


	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));

	if (flag_retry_interval == SCSI_CAP_QFULL_RETRIES) {
		if (flag_get_set == ISP_GET_QFULL_CAP) {
			cmd = ISP_MBOX_CMD_GET_QFULL_RETRIES;
		} else {
			cmd = ISP_MBOX_CMD_SET_QFULL_RETRIES;
			rval = TRUE;
		}

	} else {
		if (flag_get_set == ISP_GET_QFULL_CAP) {
			cmd = ISP_MBOX_CMD_GET_QFULL_RETRY_INTERVAL;
		} else {
			cmd = ISP_MBOX_CMD_SET_QFULL_RETRY_INTERVAL;
			rval = TRUE;
		}
	}

	tgt = start;

	do {
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd, cmd,
		    TGT_N_LUN(tgt, 0), value) != 0) {
			if (flag_get_set == ISP_SET_QFULL_CAP) {
				rval = FALSE;
			} else {
				rval = -1;
			}
			break;
		}
		/* get return value requested (if any) */
		if (flag_get_set == ISP_GET_QFULL_CAP) {
			rval = ISP_MBOX_RETURN_REG(&mbox_cmd, 2);
		}

	} while (++tgt < end);

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));

	return ((int)rval);
}



/*
 * (de)allocator for non-std size cdb/pkt_private/status
 */
/*ARGSUSED*/
static int
isp_i_pkt_alloc_extern(struct isp *isp, struct isp_cmd *sp,
	int cmdlen, int tgtlen, int statuslen, int kf)
{
	caddr_t cdbp, scbp, tgt;
	int failure = 0;
	struct scsi_pkt *pkt = CMD2PKT(sp);

	tgt = cdbp = scbp = NULL;
	if (cmdlen > sizeof (sp->cmd_cdb)) {
		if ((cdbp = kmem_zalloc((size_t)cmdlen, kf)) == NULL) {
			failure++;
		} else {
			pkt->pkt_cdbp = (opaque_t)cdbp;
			sp->cmd_flags |= CFLAG_CDBEXTERN;
		}
	}
	if (tgtlen > PKT_PRIV_LEN) {
		if ((tgt = kmem_zalloc(tgtlen, kf)) == NULL) {
			failure++;
		} else {
			sp->cmd_flags |= CFLAG_PRIVEXTERN;
			pkt->pkt_private = tgt;
		}
	}
	if (statuslen > EXTCMDS_STATUS_SIZE) {
		if ((scbp = kmem_zalloc((size_t)statuslen, kf)) == NULL) {
			failure++;
		} else {
			sp->cmd_flags |= CFLAG_SCBEXTERN;
			pkt->pkt_scbp = (opaque_t)scbp;
		}
	}
	if (failure) {
		isp_i_pkt_destroy_extern(isp, sp);
	}
	return (failure);
}


static void
isp_i_pkt_destroy_extern(struct isp *isp, struct isp_cmd *sp)
{
	struct scsi_pkt *pkt = CMD2PKT(sp);

	if (sp->cmd_flags & CFLAG_FREE) {
		panic("isp_scsi_impl_pktfree: freeing free packet");
		_NOTE(NOT_REACHED)
		/* NOTREACHED */
	}
	if (sp->cmd_flags & CFLAG_CDBEXTERN) {
		kmem_free((caddr_t)pkt->pkt_cdbp,
		    (size_t)sp->cmd_cdblen);
	}
	if (sp->cmd_flags & CFLAG_SCBEXTERN) {
		kmem_free((caddr_t)pkt->pkt_scbp,
		    (size_t)sp->cmd_scblen);
	}
	if (sp->cmd_flags & CFLAG_PRIVEXTERN) {
		kmem_free((caddr_t)pkt->pkt_private,
		    (size_t)sp->cmd_privlen);
	}

	sp->cmd_flags = CFLAG_FREE;
	kmem_cache_free(isp->isp_kmem_cache, (void *)SP2ALLOC_PKT(sp));
}


/*
 * Function name : isp_scsi_init_pkt
 *
 * Return Values : pointer to scsi_pkt, or NULL
 * Description	 : Called by kernel on behalf of a target driver
 *		   calling scsi_init_pkt(9F).
 *		   Refer to tran_init_pkt(9E) man page
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static struct scsi_pkt *
isp_scsi_init_pkt(struct scsi_address *ap, struct scsi_pkt *pkt,
	struct buf *bp, int cmdlen, int statuslen, int tgtlen,
	int flags, int (*callback)(), caddr_t arg)
{
	int		kf;
	struct isp_cmd	*sp;
	int		sp_allocated = FALSE;
	struct isp	*isp = ADDR2ISP(ap);
	struct isp_cmd	*new_cmd = NULL;


#ifdef ISP_TEST_ALLOC_EXTERN
	cdblen *= 3; statuslen *= 3; tgtlen *= 3;
#endif

	ISP_DEBUG3(isp, SCSI_DEBUG, "isp_scsi_init_pkt enter pkt=0x%p",
	    (void *)pkt);


	/*
	 * If we've already allocated a pkt once,
	 * this request is for dma allocation only.
	 * since isp usually has TQ targets with ARQ enabled, always
	 * allocate an extended pkt
	 */
	if (pkt == NULL) {
		struct isp_alloc_pkt *alloc_pkt;
		/*
		 * First step of isp_scsi_init_pkt:  pkt allocation
		 */
		kf = (callback == SLEEP_FUNC) ? KM_SLEEP: KM_NOSLEEP;
		alloc_pkt = kmem_cache_alloc(isp->isp_kmem_cache, kf);

		/*
		 * Selective zeroing of the pkt.
		 * Zeroing cmd_pkt, cmd_cdb_un, cmd_pkt_private, and cmd_flags.
		 */
		if (alloc_pkt != NULL) {
			int	*p;

			sp = &alloc_pkt->sp;
			pkt = &alloc_pkt->pkt;
			sp->cmd_pkt		= pkt;
			pkt->pkt_ha_private	= (opaque_t)sp;
			pkt->pkt_scbp		= (opaque_t)&alloc_pkt->stat;
			sp->cmd_flags		= 0;
			sp->cmd_cdblen		= cmdlen;
			sp->cmd_scblen		= statuslen;
			sp->cmd_privlen		= tgtlen;
			pkt->pkt_address	= *ap;
			pkt->pkt_comp		= NULL;
			pkt->pkt_flags		= 0;
			pkt->pkt_time		= 0;
			pkt->pkt_resid		= 0;
			pkt->pkt_statistics	= 0;
			pkt->pkt_reason		= 0;
			pkt->pkt_cdbp		= (opaque_t)&sp->cmd_cdb;
			/* zero cdbp and pkt_private */
			p = (int *)pkt->pkt_cdbp;
			*p++	= 0;
			*p++	= 0;
			*p	= 0;
			pkt->pkt_private = (opaque_t)sp->cmd_pkt_private;
			sp->cmd_pkt_private[0] = NULL;
			sp->cmd_pkt_private[1] = NULL;
			sp_allocated = TRUE;
		}

		/*
		 * cleanup or do more allocations
		 */
		if (!sp_allocated ||
		    (cmdlen > sizeof (sp->cmd_cdb)) ||
		    (tgtlen > PKT_PRIV_LEN) ||
		    (statuslen > EXTCMDS_STATUS_SIZE)) {
			int	failure = 0;

			if (sp_allocated) {
				failure = isp_i_pkt_alloc_extern(isp, sp,
				    cmdlen, tgtlen, statuslen, kf);
			}
			if (!sp_allocated || failure) {
				/*
				 * XXX: shouldn't we release memory,
				 * if allocated, like sp?
				 */
				return (NULL);
			}
		}

		new_cmd = sp;
	} else {
		sp = PKT2CMD(pkt);
		new_cmd = NULL;
		if (sp->cmd_flags & (CFLAG_COMPLETED | CFLAG_FINISHED)) {
			sp->cmd_flags &= ~(CFLAG_COMPLETED | CFLAG_FINISHED);
		}
	}

	/*
	 * Second step of isp_scsi_init_pkt:  dma allocation
	 */
	/*
	 * Here we want to check for CFLAG_DMAVALID because some target
	 * drivers like scdk on x86 can call this routine with
	 * non-zero pkt and without freeing the DMA resources.
	 */
	if ((bp != NULL) && (bp->b_bcount != 0) &&
	    !(sp->cmd_flags & CFLAG_DMAVALID)) {
		int cmd_flags, dma_flags;
		int rval;
		uint_t dmacookie_count;			/* set but not used */

		cmd_flags = sp->cmd_flags;

		/*
		 * Get the host adapter's dev_info pointer
		 */
		if (bp->b_flags & B_READ) {
			cmd_flags &= ~CFLAG_DMASEND;
			dma_flags = DDI_DMA_READ;
		} else {
			cmd_flags |= CFLAG_DMASEND;
			dma_flags = DDI_DMA_WRITE;
		}
		if (flags & PKT_CONSISTENT) {
			cmd_flags |= CFLAG_CMDIOPB;
			dma_flags |= DDI_DMA_CONSISTENT;
		}
		ASSERT(sp->cmd_dmahandle != NULL);
		rval = ddi_dma_buf_bind_handle(sp->cmd_dmahandle, bp,
		    dma_flags, callback, arg, &sp->cmd_dmacookie,
		    &dmacookie_count);

		if (rval) {
			switch (rval) {
			case DDI_DMA_NORESOURCES:
				bioerror(bp, 0);
				break;
			case DDI_DMA_NOMAPPING:
			case DDI_DMA_BADATTR:
				bioerror(bp, EFAULT);
				break;
			case DDI_DMA_TOOBIG:
			default:
				bioerror(bp, EINVAL);
				break;
			}
			sp->cmd_flags = cmd_flags & ~CFLAG_DMAVALID;
			if (new_cmd) {
				isp_scsi_destroy_pkt(ap, pkt);
			}
			ISP_DEBUG2(isp, SCSI_DEBUG,
			    "isp_scsi_init_pkt error rval=%x", rval);
			return (NULL);
		}
		sp->cmd_dmacount = bp->b_bcount;
		sp->cmd_flags = cmd_flags | CFLAG_DMAVALID;

	}
#ifdef ISPDEBUG
	/* Clear this out so we know when a command has completed. */
	bzero(&sp->cmd_isp_response, sizeof (sp->cmd_isp_response));
#endif
	ISP_DEBUG4(isp, SCSI_DEBUG, "isp_scsi_init_pkt return pkt=0x%p",
	    (void *)pkt);
	return (pkt);
}

/*
 * Function name : isp_scsi_destroy_pkt
 *
 * Return Values : none
 * Description	 : Called by kernel on behalf of a target driver
 *		   calling scsi_destroy_pkt(9F).
 *		   Refer to tran_destroy_pkt(9E) man page
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_scsi_destroy_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct isp_cmd *sp = PKT2CMD(pkt);
	struct isp *isp = ADDR2ISP(ap);

	/*
	 * isp_scsi_dmafree inline to make things faster
	 */

	if (sp->cmd_flags & CFLAG_DMAVALID) {
		/*
		 * Free the mapping.
		 */
		(void) ddi_dma_unbind_handle(sp->cmd_dmahandle);
		sp->cmd_flags ^= CFLAG_DMAVALID;
	}

	/*
	 * Free the pkt
	 */

	/*
	 * first test the most common case
	 */
	if ((sp->cmd_flags &
	    (CFLAG_FREE | CFLAG_CDBEXTERN | CFLAG_PRIVEXTERN |
	    CFLAG_SCBEXTERN)) == 0) {
		sp->cmd_flags = CFLAG_FREE;
		kmem_cache_free(isp->isp_kmem_cache, (void *)SP2ALLOC_PKT(sp));
	} else {
		isp_i_pkt_destroy_extern(isp, sp);
	}
}


/*
 * Function name : isp_scsi_dmafree()
 *
 * Return Values : none
 * Description	 : free dvma resources
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
/*ARGSUSED*/
static void
isp_scsi_dmafree(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct isp_cmd *sp = PKT2CMD(pkt);


	if (sp->cmd_flags & CFLAG_DMAVALID) {
		/*
		 * Free the mapping.
		 */
		(void) ddi_dma_unbind_handle(sp->cmd_dmahandle);
		sp->cmd_flags ^= CFLAG_DMAVALID;
	}
}


/*
 * Function name : isp_scsi_sync_pkt()
 *
 * Return Values : none
 * Description	 : sync dma
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
/*ARGSUSED*/
static void
isp_scsi_sync_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	int i;
	struct isp_cmd *sp = PKT2CMD(pkt);


	if (sp->cmd_flags & CFLAG_DMAVALID) {
		i = ddi_dma_sync(sp->cmd_dmahandle, 0, sp->cmd_dmacount,
		    (sp->cmd_flags & CFLAG_DMASEND) ?
		    DDI_DMA_SYNC_FORDEV : DDI_DMA_SYNC_FORKERNEL);
		if (i != DDI_SUCCESS) {
			struct isp	*isp = PKT2ISP(pkt);

			isp_i_log(isp, CE_WARN, "sync pkt failed");
		}
	}
}


/*
 * routine for reset notification setup, to register or cancel.
 */
static int
isp_scsi_reset_notify(struct scsi_address *ap, int flag,
void (*callback)(caddr_t), caddr_t arg)
{
	struct isp	*isp = ADDR2ISP(ap);
	return (scsi_hba_reset_notify_setup(ap, flag, callback, arg,
	    ISP_REQ_MUTEX(isp), &isp->isp_reset_notify_listf));
}


/*
 * the waitQ is used when the request mutex is held. requests will go
 * in the waitQ which will be emptied just before releasing the request
 * mutex; the waitQ reduces the contention on the request mutex significantly
 *
 * Note that the waitq mutex is released *after* the request mutex; this
 * closes a small window where we empty the waitQ but before releasing
 * the request mutex, the waitQ is filled again. isp_scsi_start will
 * attempt to get the request mutex after adding the cmd to the waitQ
 * which ensures that after the waitQ is always emptied.
 */
#define	ISP_CHECK_WAITQ_TIMEOUT(isp, iwait)				\
	if (isp->isp_waitq_timeout == 0) {				\
		isp->isp_waitq_timeout = timeout(			\
		    (void (*)(void*))isp_i_check_waitQ,			\
		    (caddr_t)isp, drv_usectohz(iwait));			\
	}

/*
 * The following hold values for scheduling a timeout
 * handler which attempts to empty the cmd. waitQ.
 * The default timeout is one second.
 * However, if an attempt was made to send a cmd. and
 * it had to be put on the waitQ instead, then use the
 * busy waitQ timeout value, initially 5 ms, to quickly
 * try emptying the waitQ.
 */
static clock_t isp_default_waitq_timeout = 1000000;	/* usecs */
static clock_t isp_busy_waitq_timeout = 5000;		/* usecs */

static void
isp_i_check_waitQ(struct isp *isp)
{
	mutex_enter(ISP_REQ_MUTEX(isp));
	mutex_enter(ISP_WAITQ_MUTEX(isp));
	isp->isp_waitq_timeout = 0;
	isp_i_empty_waitQ(isp);
	mutex_exit(ISP_REQ_MUTEX(isp));
	if (isp->isp_waitf != NULL) {
		ISP_CHECK_WAITQ_TIMEOUT(isp, isp_default_waitq_timeout);
	}
	mutex_exit(ISP_WAITQ_MUTEX(isp));
}

/*
 * Function name : isp_i_empty_waitQ()
 *
 * Return Values : none
 *
 * Description	 : empties the waitQ
 *		   copies the head of the queue and zeroes the waitQ
 *		   calls isp_i_start_cmd() for each packet
 *		   if all cmds have been submitted, check waitQ again
 *		   before exiting
 *		   if a transport error occurs, complete packet here
 *		   if a TRAN_BUSY occurs, then restore waitQ and try again
 *		   later
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_i_empty_waitQ(struct isp *isp)
{
	struct isp_cmd	*sp, *head, *tail;
	int		rval;

	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(!mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)));
	ASSERT(!mutex_owned(ISP_HOTPLUG_MUTEX(isp)));


	/* optimization check */
	if (isp->isp_waitf == NULL) {
		/* nothing to dequeue */
		goto exit;
	}

	/*
	 * if we are in the middle of doing a reset then we
	 * we do not want to take pkts off of the waitQ
	 * just yet
	 */
	if (isp->isp_in_reset) {
		ISP_DEBUG2(isp, SCSI_DEBUG, "can't dequeue 0x%p (in reset)",
		    (void *)isp->isp_waitf);
		goto exit;
	}

	/*
	 * check to see if we are handling hotplugging (any of the hotplug
	 * bits set) and skip emptying the queue for now if we are
	 */
	mutex_enter(ISP_HOTPLUG_MUTEX(isp));
	if (ISP_HOTPLUG_IN_PROG(isp)) {
		ISP_DEBUG2(isp, SCSI_DEBUG, "can't dequeue 0x%p (hotplugging)",
		    (void *)isp->isp_waitf);
		mutex_exit(ISP_HOTPLUG_MUTEX(isp));
		goto exit;
	}
	mutex_exit(ISP_HOTPLUG_MUTEX(isp));

again:
	/*
	 * walk thru the waitQ and attempt to start the cmd
	 */
	while (isp->isp_waitf != NULL) {
		/*
		 * copy queue head, clear wait queue and release WAITQ_MUTEX
		 */
		head = isp->isp_waitf;
		tail = isp->isp_waitb;
		isp->isp_waitf = isp->isp_waitb = NULL;
		mutex_exit(ISP_WAITQ_MUTEX(isp));

		/*
		 * empty the local list
		 */
		while (head != NULL) {
			struct scsi_pkt *pkt;

			sp = head;
			head = sp->cmd_forw;
			sp->cmd_forw = NULL;

			ISP_DEBUG4(isp, SCSI_DEBUG, "starting waitQ sp=0x%p",
			    (void *)sp);

			/* try to start the command */
			if ((rval = isp_i_start_cmd(isp, sp, NULL)) ==
			    TRAN_ACCEPT) {
				continue;	/* success: go to next one */
			}

			/* transport of the cmd failed */

			ISP_DEBUG4(isp, SCSI_DEBUG,
			    "isp_i_empty_waitQ: transport failed (%x)", rval);

			/*
			 * if the isp could not handle more requests,
			 * (rval was TRAN_BUSY) then
			 * put all requests back on the waitQ before
			 * releasing the REQ_MUTEX
			 * if there was another transport error then
			 * do not put this packet back on the queue
			 * but complete it here
			 */
			if (rval == TRAN_BUSY) {
				sp->cmd_forw = head;
				head = sp;
			}

			mutex_enter(ISP_WAITQ_MUTEX(isp));
			if (isp->isp_waitf != NULL) {
				/*
				 * somebody else has added to waitQ while
				 * we were messing around, so add our queue
				 * to what is now on wiatQ
				 */
				tail->cmd_forw = isp->isp_waitf;
				isp->isp_waitf = head;
			} else {
				/*
				 * waitQ is still empty, so just put our
				 * list back
				 */
				isp->isp_waitf = head;
				isp->isp_waitb = tail;
			}

			if (rval == TRAN_BUSY) {
				/*
				 * request queue was full; try again
				 * 1 sec later
				 */
				ISP_CHECK_WAITQ_TIMEOUT(isp,
				    isp_default_waitq_timeout);
				goto exit;
			}

			/*
			 * transport failed, but (rval != TRAN_BUSY)
			 *
			 * set reason and call target completion routine
			 */

			ISP_SET_REASON(sp, CMD_TRAN_ERR);
			ASSERT(sp != NULL);
			pkt = CMD2PKT(sp);
			ASSERT(pkt != NULL);
			if (pkt->pkt_comp != NULL) {
#ifdef ISPDEBUG
				sp->cmd_flags |= CFLAG_FINISHED;
#endif
				/* pkt no longer in transport */
				sp->cmd_flags &= ~CFLAG_IN_TRANSPORT;
				mutex_exit(ISP_WAITQ_MUTEX(isp));
				mutex_exit(ISP_REQ_MUTEX(isp));
				(*pkt->pkt_comp)(pkt);
				mutex_enter(ISP_REQ_MUTEX(isp));
				mutex_enter(ISP_WAITQ_MUTEX(isp));
			}
			/*
			 * this goto saves having to do a mutex exit
			 * just to do an enter before looping around
			 */
			goto again;
		}
		mutex_enter(ISP_WAITQ_MUTEX(isp));
	}

exit:

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)));
}


/*
 * Function name : isp_scsi_start()
 *
 * Return Values : TRAN_BUSY		- request queue is full
 *		   TRAN_ACCEPT		- pkt has been submitted to isp
 *					  (or is held in the waitQ)
 * Description	 : init pkt
 *		   check the waitQ and if empty try to get the request mutex
 *		   if this mutex is held, put request in waitQ and return
 *		   if we can get the mutex, start the request
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 *
 * XXX: We assume that dvma bounds checking is performed by
 *	the target driver!  Also, that sp is *ALWAYS* valid.
 *
 * Note: No support for > 1 data segment.
 */
static int
isp_scsi_start(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct isp_cmd *sp = PKT2CMD(pkt);
	struct isp *isp;
	int rval = TRAN_ACCEPT;
	int cdbsize;
	struct isp_request *req;


	isp = ADDR2ISP(ap);

	ASSERT(isp != NULL);
	ASSERT(!mutex_owned(ISP_REQ_MUTEX(isp)) || ddi_in_panic());
	ASSERT(!mutex_owned(ISP_RESP_MUTEX(isp)) || ddi_in_panic());
	ASSERT(!mutex_owned(ISP_WAITQ_MUTEX(isp)) || ddi_in_panic());

	ISP_DEBUG3(isp, SCSI_DEBUG,
	    "isp_scsi_start 0x%p: suspended=%d, pkt_flags=0x%x",
	    (void *)sp, isp->isp_suspended, pkt->pkt_flags);


	ASSERT(sp != NULL);

	/* if no CDB then puke it back to target driver */
	if ((cdbsize = sp->cmd_cdblen) == 0) {
		isp_i_log(isp, CE_WARN,
		    "can't send SCSI packet with zero CDB length: rejecting");
		return (TRAN_BADPKT);
	}

	if ((cdbsize = sp->cmd_cdblen) > ISP_CDBMAX ||
	    (scsi_cdb_size[CDB_GROUPID(pkt->pkt_cdbp[0])] > ISP_CDBMAX))  {
		isp_i_log(isp, CE_WARN,
		    "SCSI packet with CDB length too long : rejecting");
		return (TRAN_BADPKT);
	}


	ISP_DEBUG4(isp, SCSI_DEBUG, "SCSI starting packet, sp=0x%p",
	    (void *)sp);

	/* pkt had better not already be in transport */
	ASSERT(!(sp->cmd_flags & CFLAG_IN_TRANSPORT));
	sp->cmd_flags = (sp->cmd_flags & ~CFLAG_TRANFLAG) | CFLAG_IN_TRANSPORT;
	pkt->pkt_reason = CMD_CMPLT;

	/*
	 * set up request in cmd_isp_request area so it is ready to
	 * go once we have the request mutex
	 * XXX do we need to zero each time
	 */
	req = &sp->cmd_isp_request;

	req->req_header.cq_entry_type = CQ_TYPE_REQUEST;
	req->req_header.cq_entry_count = 1;
	req->req_header.cq_flags = 0;
	req->req_reserved = 0;

	req->req_scsi_id.req_target = TGT(sp);
	req->req_scsi_id.req_lun_trn = LUN(sp);
	req->req_time = pkt->pkt_time;

	ISP_SET_PKT_FLAGS(pkt->pkt_flags, req->req_flags);

	/*
	 * Setup dma transfers data segments.
	 *
	 * NOTE: Only 1 dataseg supported.
	 */
	if (sp->cmd_flags & CFLAG_DMAVALID) {
		/*
		 * Have to tell isp which direction dma transfer is going.
		 */
		pkt->pkt_resid = (size_t)sp->cmd_dmacount;

		if (sp->cmd_flags & (CFLAG_CMDIOPB | CFLAG_DMASEND)) {
			(void) ddi_dma_sync(sp->cmd_dmahandle, 0,
			    sp->cmd_dmacount,
			    DDI_DMA_SYNC_FORDEV);
		}

		req->req_seg_count = 1;
		req->req_dataseg[0].d_count = sp->cmd_dmacount;
		req->req_dataseg[0].d_base = sp->cmd_dmacookie.dmac_address;
		if (sp->cmd_flags & CFLAG_DMASEND) {
			req->req_flags |= ISP_REQ_FLAG_DATA_WRITE;
		} else {
			req->req_flags |= ISP_REQ_FLAG_DATA_READ;
		}
	} else {
		req->req_seg_count = 0;
		req->req_dataseg[0].d_count = 0;
	}

	ISP_LOAD_REQUEST_CDB(req, sp, cdbsize);

	/*
	 * the normal case is a non-polled cmd, so deal with that first
	 */
	if ((pkt->pkt_flags & FLAG_NOINTR) == 0) {
		/*
		 * isp request mutex can be held for a long time; therefore,
		 * if request mutex is held, we queue the packet in a waitQ
		 * Consequently, we now need to check the waitQ before every
		 * release of the request mutex
		 *
		 * if the waitQ is non-empty, add cmd to waitQ to preserve
		 * some order
		 */
		mutex_enter(ISP_WAITQ_MUTEX(isp));
		mutex_enter(ISP_HOTPLUG_MUTEX(isp));
		if (isp->isp_in_reset ||
		    !isp->isp_attached ||
		    (isp->isp_waitf != NULL) ||
		    ISP_HOTPLUG_IN_PROG(isp) ||
		    (mutex_tryenter(ISP_REQ_MUTEX(isp)) == 0)) {

			ISP_DEBUG4(isp, SCSI_DEBUG,
			    "putting pkt on waitQ sp=0x%p", (void *)sp);

			mutex_exit(ISP_HOTPLUG_MUTEX(isp));
			/*
			 * either we have pkts on the wait queue or
			 * we can't get the request mutex
			 * OR we are in the middle of handling a reset
			 * OR we are handling a hotplug event
			 *
			 * in any case we don't have the request
			 * queue mutex but we do have the wait queue
			 * mutex
			 *
			 * so, for whatever reason, we want to put the
			 * supplied packet on the wait queue instead of
			 * on the request queue
			 */
			if (isp->isp_waitf == NULL) {
				/*
				 * there's nothing on the wait queue
				 * and we can't get the request queue,
				 * so put pkt on wait queue
				 */
				isp->isp_waitb = isp->isp_waitf = sp;
				sp->cmd_forw = NULL;
			} else {
				/*
				 * there is something on the
				 * wait queue so put our pkt at its end
				 */
				struct isp_cmd *dp = isp->isp_waitb;
				dp->cmd_forw = isp->isp_waitb = sp;
				sp->cmd_forw = NULL;
			}

			ISP_INC32_ERRSTATS(isp, isperr_waitq_cmds);
			ISP_CHECK_WAITQ_TIMEOUT(isp, isp_busy_waitq_timeout);
			mutex_exit(ISP_WAITQ_MUTEX(isp));
		} else {
			/*
			 * no entries on wait queue *and* we were
			 * able to get request queue lock
			 */

			/*
			 * no need to hold this, and releasing it
			 * will give others a chance to put
			 * their request some place, since we now
			 * own the request queue
			 */
			mutex_exit(ISP_WAITQ_MUTEX(isp));

			/* likewise */
			mutex_exit(ISP_HOTPLUG_MUTEX(isp));

			rval = isp_i_start_cmd(isp, sp, NULL);
			if (rval == TRAN_BUSY) {
				/*
				 * put request back at the head of the waitQ
				 */
				mutex_enter(ISP_WAITQ_MUTEX(isp));
				sp->cmd_forw = isp->isp_waitf;
				isp->isp_waitf = sp;
				if (isp->isp_waitb == NULL) {
					isp->isp_waitb = sp;
				}
				ISP_INC32_ERRSTATS(isp, isperr_waitq_cmds);
				mutex_exit(ISP_WAITQ_MUTEX(isp));
				rval = TRAN_ACCEPT;
			}
			isp_check_waitq_and_exit_req_mutex(isp);

			/* keep track of fact that instance is alive */
			isp->isp_alive = TRUE;
		}
	} else {
		rval = isp_i_polled_cmd_start(isp, sp);
	}

	ASSERT(!mutex_owned(ISP_REQ_MUTEX(isp)) || ddi_in_panic());
	ASSERT(!mutex_owned(ISP_RESP_MUTEX(isp)) || ddi_in_panic());
	ASSERT(!mutex_owned(ISP_WAITQ_MUTEX(isp)) || ddi_in_panic());
	return (rval);
}

static void
isp_i_add_to_free_slots(struct isp *isp, uint16_t entry)
{
	isp->isp_slots[entry].next = isp->free_slots.head;
	isp->isp_slots[entry].prev = ISP_MAX_SLOTS;

	isp->free_slots.head = entry;

	if (isp->isp_slots[entry].next != ISP_MAX_SLOTS) {
		ASSERT(isp->isp_slots[isp->isp_slots[entry].next].prev ==
		    ISP_MAX_SLOTS);
		isp->isp_slots[isp->isp_slots[entry].next].prev = entry;
	} else {
		ASSERT(isp->free_slots.tail == ISP_MAX_SLOTS);
		isp->free_slots.tail = entry;
	}
}

/*
 * The list of busy commands is kept in time order so that the tail of
 * the list always points to the first candidate entry to time out.
 * Since most commands will come in with the same scsi packet timeout this
 * mostly means we add to the head of the list.
 */
static void
isp_i_add_to_busy_slots(struct isp *isp, uint16_t entry)
{
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(isp->isp_slots[entry].isp_cmd != NULL);

	if (isp->busy_slots.head == ISP_MAX_SLOTS) {
		ASSERT(isp->busy_slots.tail == ISP_MAX_SLOTS);
		isp->busy_slots.head = entry;
		isp->busy_slots.tail = entry;
		isp->isp_slots[entry].prev = ISP_MAX_SLOTS;
		isp->isp_slots[entry].next = ISP_MAX_SLOTS;
	} else {
		uint16_t i;

		for (i = isp->busy_slots.head; i != ISP_MAX_SLOTS;
		    i = isp->isp_slots[i].next) {
			if (isp->isp_slots[entry].timeout >
			    isp->isp_slots[i].timeout) {
				break;
			}
		}

		if (i == ISP_MAX_SLOTS) {
			isp->isp_slots[entry].next = ISP_MAX_SLOTS;
			isp->isp_slots[entry].prev = isp->busy_slots.tail;
			isp->isp_slots[isp->busy_slots.tail].next = entry;
			isp->busy_slots.tail = entry;
		} else {
			isp->isp_slots[entry].next = i;
			isp->isp_slots[entry].prev = isp->isp_slots[i].prev;
			if (isp->isp_slots[i].prev != ISP_MAX_SLOTS) {
				isp->isp_slots[isp->isp_slots[i].prev].next =
				    entry;
			} else {
				ASSERT(isp->busy_slots.head == i);
				isp->busy_slots.head = entry;
			}
			isp->isp_slots[i].prev = entry;
		}
	}
}

static void
isp_i_delete_from_slot_list(struct isp *isp,
		struct isp_slot_list *list, uint16_t entry)
{
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));

	if (isp->isp_slots[entry].next == ISP_MAX_SLOTS) {
		ASSERT(list->tail == entry);

		list->tail = isp->isp_slots[entry].prev;
	} else {
		ASSERT(isp->isp_slots[isp->isp_slots[entry].next].prev ==
		    entry);

		isp->isp_slots[isp->isp_slots[entry].next].prev =
		    isp->isp_slots[entry].prev;
	}

	if (isp->isp_slots[entry].prev == ISP_MAX_SLOTS) {
		ASSERT(list->head == entry);

		list->head = isp->isp_slots[entry].next;
	} else {
		ASSERT(isp->isp_slots[isp->isp_slots[entry].prev].next ==
		    entry);

		isp->isp_slots[isp->isp_slots[entry].prev].next =
		    isp->isp_slots[entry].next;
	}
}

/*
 * Function name : isp_i_start_cmd()
 *
 * Return Values : TRAN_ACCEPT	- request is in the isp request queue
 *		   TRAN_BUSY	- request queue is full
 *
 * Description	 : if there is space in the request queue, copy over request
 *		   enter normal requests in the isp_slots list
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_i_start_cmd(struct isp *isp, struct isp_cmd *sp, uint16_t *slotp)
{
	struct isp_request *req;
	struct scsi_pkt *pkt;
	union isp_token tok;
	ushort_t	tgt;
	uint16_t	lun;
	uint16_t	request_in;	/* for saving a copy of the real one */
	uint16_t	slot;


	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(!mutex_owned(ISP_WAITQ_MUTEX(isp)) || ddi_in_panic());
	ASSERT(sp != NULL);

	pkt = CMD2PKT(sp);
	ASSERT(pkt != NULL);
	tgt = TGT(sp);
	lun = LUN(sp);

	/*
	 *
	 * Slots are used for reset protection, timeout detection
	 * and as part of the token so that tokens are unique.
	 * So we can see if tokens are replayed there is also a sequence number
	 * stored in the isp_slot which makes up the rest of the token.
	 *
	 * We should *ALWAYS* be able to get a free slot; or we're broke!
	 *
	 * Delete from the tail so that we use all the slots.  This
	 * leaves footprints in the slots that can be useful for debugging.
	 */

	if ((slot = isp->free_slots.tail) == ISP_MAX_SLOTS) {
		isp_i_log(isp, CE_WARN, "isp: no free slots!!");
		return (TRAN_BUSY);
	}

	ASSERT(isp->isp_slots[slot].isp_cmd == NULL);

	isp_i_delete_from_slot_list(isp, &isp->free_slots, slot);

	isp->isp_slots[slot].isp_cmd = sp;
	/*
	 * If the caller needs the slot then store it in slotp.
	 * Only needed for POLLED commands.
	 */
	if (slotp != NULL) {
		*slotp = slot;
	}

	/*
	 * The timeout should be handled by the chip and the firmware so
	 * the driver only has to act as a back up in case this fails. Hence
	 * the addition of ISP_GRACE.
	 */
	isp->isp_slots[slot].timeout = gethrtime() +
	    SEC_TO_NSEC(pkt->pkt_time + ISP_GRACE);

	isp_i_add_to_busy_slots(isp, slot);

	ISP_DEBUG4(isp, SCSI_DEBUG, "sp=0x%p put in slot %d",
	    (void *)sp, slot);

	tok.slot_n_seq.seq = isp->isp_slots[slot].seq;
	tok.slot_n_seq.slot = slot;
	req = &sp->cmd_isp_request;
	req->req_token = tok.token;
	req->req_header.cq_seqno = isp->isp_slots[slot].xor =
	    ISP_CHECK_TOKEN(tok);
	ISP_DEBUG3(isp, SCSI_DEBUG,
	    "isp_i_start_cmd: sp=0x%p, req_in=%d, pkt_time=0x%x",
	    (void *)sp, isp->isp_request_in, pkt->pkt_time);


#ifdef	ISPDEBUG_IOCTL
	if (isp->drop_command) {
		/* drop a command to test the driver timeout code. */
		isp->drop_command--;
		return (TRAN_ACCEPT);
	}
#endif
	/*
	 * Check to see how much space is available in the
	 * Request Queue, save this so we do not have to do
	 * a lot of PIOs
	 */
	if (isp->isp_queue_space == 0) {
		isp_i_update_queue_space(isp);

		/*
		 * Check now to see if the queue is still full
		 * Report TRAN_BUSY if we are full
		 */
		if (isp->isp_queue_space == 0) {
			ISP_FREE_SLOT(isp, slot);
			return (TRAN_BUSY);
		}
	}

	/*
	 * this flag is defined in firmware source code although
	 * not documented.
	 */
	/* The ability to disable auto request sense per packet */
	if ((sp->cmd_scblen < sizeof (struct scsi_arq_status)) &&
	    (isp->isp_cap[tgt] & ISP_CAP_AUTOSENSE)) {
		ISP_DEBUG4(isp, SCSI_DEBUG,
		    "isp_i_start_cmd: disabling ARQ=0x%p", (void *)sp);
		sp->cmd_isp_request.req_flags |= ISP_REQ_FLAG_DISARQ;
	}

	/*
	 * see if we have an unresolved check condition for this I_T_L
	 * (eg: ARQ was turned off), set priority bit to put restart the
	 * I/O queues in fw.
	 */
	if ((isp->isp_arq_pending[tgt] != 0) &&
	    (isp->isp_arq_pending[tgt] & (1 << lun))) {
		sp->cmd_isp_request.req_flags |= ISP_REQ_FLAG_PRIORITY_CMD;
		isp->isp_arq_pending[tgt] &= ~(1 << lun);
	}

	/* save a copy of the request_in pointer (for DMA) */
	request_in = isp->isp_request_in;

	/*
	 * Put I/O request in isp request queue to run.
	 * Get the next request in pointer.
	 */
	ISP_GET_NEXT_REQUEST_IN(isp, req);

	/*
	 * Copy 40 of the  64 byte request into the request queue
	 * (only 1 data seg)
	 */
	ISP_COPY_OUT_REQ(isp, &sp->cmd_isp_request, req);

	/*
	 * Use correct offset and size for syncing
	 */
	(void) ddi_dma_sync(isp->isp_dmahandle,
	    (off_t)(request_in * sizeof (struct isp_request)),
	    (size_t)sizeof (struct isp_request),
	    DDI_DMA_SYNC_FORDEV);


#ifdef	ISPDEBUG
	/*
	 * for debugging, set the renegotiate flag on every other inquiry
	 */
	if (isp_debug_renegotiate) {
		if ((pkt->pkt_cdbp != NULL) &&
		    (pkt->pkt_cdbp[0] == SCMD_INQUIRY)) {
			isp_i_log(isp, CE_NOTE,
			    "DEBUG: requesting renegotiation");
			pkt->pkt_flags |= FLAG_RENEGOTIATE_WIDE_SYNC;
			isp_debug_renegotiate = 0;
		}
	}
#endif

#ifndef	__lock_lint
	/* update capabilities if requested by target */
	if (pkt->pkt_flags & FLAG_RENEGOTIATE_WIDE_SYNC) {

		/*
		 * we are in the request queue part of the code, but now we
		 * want to call a routine associated with the response
		 * queue -- so some times we can get here owning the response
		 * mutex, and some times we get here not owning it, hence the
		 * ugly check and code (the horrors of error handling)
		 *
		 * note: warlock doesn't understand this code, hence the
		 * ifndef for lock lint
		 */
		if (mutex_owned(ISP_RESP_MUTEX(isp))) {
			ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
			(void) isp_i_updatecap(isp, tgt, tgt, TRUE);
		} else {
			mutex_enter(ISP_RESP_MUTEX(isp));
			(void) isp_i_updatecap(isp, tgt, tgt, TRUE);
			mutex_exit(ISP_RESP_MUTEX(isp));
		}
		ISP_DEBUG4(isp, SCSI_DEBUG, "DEBUG: renegotiated (tgt %d)",
		    tgt);
	}
#endif	/* __lock_lint */

	/*
	 * Tell isp it's got a new I/O request...
	 */
	ISP_SET_REQUEST_IN(isp);
	isp->isp_queue_space--;
	ISP_INC32_ERRSTATS(isp, isperr_total_cmds);

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(!mutex_owned(ISP_WAITQ_MUTEX(isp)) || ddi_in_panic());
	return (TRAN_ACCEPT);
}

/*
 * Function name : isp_i_check_token()
 *
 * Return Values : Index into isp_slot array of entry
 *		   ISP_MAX_SLOTS on error.
 *
 * Description	 : Validate that the token is not corrupt and
 *		   is not being replayed.
 *
 * Context	 : Called by interrupt thread
 */
static uint16_t
isp_i_check_token(struct isp *isp, union isp_token tok, uint8_t xor)
{
	uint8_t x;

	x = ISP_CHECK_TOKEN(tok);

	if (x != xor || tok.slot_n_seq.slot >= ISP_MAX_SLOTS ||
	    isp->isp_slots[tok.slot_n_seq.slot].seq != tok.slot_n_seq.seq) {
		return (ISP_MAX_SLOTS);
	} else {
		return (tok.slot_n_seq.slot);
	}
}

/*
 * the isp interrupt routine
 */
static uint_t
isp_intr(caddr_t arg)
{
	struct isp_cmd	*sp;
	struct isp_cmd	*head, *tail;
	struct isp	*isp = (struct isp *)arg;
	struct isp_response *resp;
	struct isp_response *cmd_resp;
	int		n;
	off_t		offset;
	uint_t		sync_size;
	uint_t		intr_claimed = DDI_INTR_UNCLAIMED;
	int		aen_event;
	ushort_t	mbox_event;
	ushort_t	response_in;
	uint16_t 	slot;

	ISP_DEBUG3(isp, SCSI_DEBUG, "isp_intr entry");

	ASSERT(isp != NULL);

#ifdef ISP_PERF
	isp->isp_intr_count++;
#endif
	/*
	 * set flag saying we are in interrupt routine -- this is in case
	 * the reset code has reset the chip and assumes that interrupts
	 * have been handled (but they have not)
	 */
	mutex_enter(ISP_INTR_MUTEX(isp));
	isp->isp_in_intr++;
	mutex_exit(ISP_INTR_MUTEX(isp));

	while (ISP_INT_PENDING(isp)) {
again:
		/*
		 * head list collects completed packets for callback later
		 */
		head = tail = NULL;

		/*
		 * Assume no mailbox events (e.g. mailbox cmds, asynch
		 * events, and isp dma errors) as common case.
		 */
		mutex_enter(ISP_RESP_MUTEX(isp));

		/*
		 * kstat_intr support
		 */
		if ((intr_claimed == DDI_INTR_UNCLAIMED) &&
		    (isp->isp_kstat != NULL)) {
			ISP_KSTAT_INTR_PTR(isp)->intrs[KSTAT_INTR_HARD]++;
		}

		intr_claimed = DDI_INTR_CLAIMED;

		/*
		 * we can be interrupted for one of three reasons:
		 * - a mailbox command has completed
		 * - a chip-generated async event has happened (e.g.
		 *	external bus reset), or
		 * - one or more pkts have been placed on the response queue
		 *	by the chip
		 * so check here for/handle the first two cases
		 *
		 * check to see if someone else is handling the isp semaphore
		 *
		 */
		if (!(isp->isp_checking_semlock) &&
		    ISP_CHECK_SEMAPHORE_LOCK(isp)) {

			isp->isp_checking_semlock = TRUE;

			mbox_event = isp_i_get_mbox_event(isp);

			if (mbox_event & ISP_MBOX_EVENT_AEN) {
				/* async event intr */
				mutex_enter(ISP_REQ_MUTEX(isp));
				/* this returns the aen_event */
				aen_event = isp_i_handle_aen(isp, mbox_event);
				mutex_exit(ISP_REQ_MUTEX(isp));
				isp->isp_checking_semlock = FALSE;
				mutex_exit(ISP_RESP_MUTEX(isp));
			} else if (mbox_event & ISP_MBOX_EVENT_CMD) {
				/* regular mbox intr */
				mutex_exit(ISP_RESP_MUTEX(isp));
				isp_i_mbox_cmd_complete(isp);

				mutex_enter(ISP_RESP_MUTEX(isp));
				isp->isp_checking_semlock = FALSE;
				mutex_exit(ISP_RESP_MUTEX(isp));

				aen_event = ISP_AEN_MBOX_COMPLETED;
			} else {
				/* unknown intr event */
				ISP_CLEAR_RISC_INT(isp);
				isp->isp_checking_semlock = FALSE;
				mutex_exit(ISP_RESP_MUTEX(isp));
				isp_i_log(isp, CE_NOTE,
				    "isp_intr: unknown event 0x%x",
				    mbox_event);
				aen_event = ISP_AEN_UNKNOWN;
			}

			/*
			 * at this point, the isp risc intr has been cleared
			 * and the isp semaphore has been cleared
			 */
			ASSERT(!mutex_owned(ISP_RESP_MUTEX(isp)));
			ASSERT(!mutex_owned(ISP_REQ_MUTEX(isp)));

			/*
			 * check if we need to do any recovery for AEN
			 */
			if (aen_event & ISP_AEN_NEED_RECOVERY) {
				mutex_enter(ISP_RESP_MUTEX(isp));
				isp_i_complete_aen(isp, aen_event,
				    ISP_FROM_INTR);
				mutex_exit(ISP_RESP_MUTEX(isp));
			}

			/*
			 * if there was a mailbox cmd, unknown intr or an AEN
			 * error event, then we're finished
			 */
			if (aen_event & ISP_AEN_ERROR_MASK) {
				goto dun;
			}

			/*
			 * if there was a reset then check the response
			 * queue again
			 */
			goto again;
		}

		/*
		 * semaphore lock was zero and we processed the mbox response
		 */

		/*
		 * Workaround for queue-not-initialzed-yet problems
		 */
		if (isp->isp_mbox.mbox_flags & ISP_MBOX_CMD_FLAGS_Q_NOT_INIT) {
			ISP_CLEAR_RISC_INT(isp);
			mutex_exit(ISP_RESP_MUTEX(isp));
			continue;
		}

		/*
		 * Loop through completion response queue and post
		 * completed pkts.  Check response queue again
		 * afterwards in case there are more and process them
		 * to keep interrupt response time low under heavy load.
		 *
		 * To conserve PIO's, we only update the isp response
		 * out index after draining it.
		 */
		isp->isp_response_in =
		    response_in = isp_i_get_response_in_db(isp);
		ISP_CLEAR_RISC_INT(isp);

		/*
		 * Calculate how many requests there are in the queue
		 * If this is < 0, then we are wrapping around
		 * and syncing the packets need two separate syncs
		 */
		n = response_in - isp->isp_response_out;
		offset = (off_t)((isp_max_requests *
		    sizeof (struct isp_request)) +
		    (isp->isp_response_out *
		    sizeof (struct isp_response)));

		if (n == 1) {
			/*
			 * most common case: just one response, no wrap
			 */
			sync_size =
			    ((uint_t)sizeof (struct isp_response));
		} else if (n > 0) {
			/*
			 * 2nd most common case: >1 response, no wrap
			 */
			sync_size =
			    n * ((uint_t)sizeof (struct isp_response));
		} else if (n < 0) {
			/*
			 * last/least common case: 1 or more responses
			 * with wrap around
			 */
			sync_size =
			    (isp_max_requests - isp->isp_response_out) *
			    ((uint_t)sizeof (struct isp_response));

			/*
			 * we wrapped around and need an extra sync
			 */
			/* no use syncing if nothing to sync */
			if (response_in) {
				(void) ddi_dma_sync(isp->isp_dmahandle,
				    (off_t)((isp_max_requests *
				    sizeof (struct isp_request))),
				    (size_t)(response_in *
				    sizeof (struct isp_response)),
				    DDI_DMA_SYNC_FORKERNEL);

				n = isp_max_requests - isp->isp_response_out +
				    response_in;
			} else {
				/* optimize -- we know response_in is 0 */
				n = isp_max_requests - isp->isp_response_out;
			}

		} else {
			ISP_DEBUG2(isp, SCSI_DEBUG,
			    "isp_intr called with no requests to handle!");
			goto update;
		}

		(void) ddi_dma_sync(isp->isp_dmahandle,
		    (off_t)offset, sync_size, DDI_DMA_SYNC_FORKERNEL);
		ISP_DEBUG4(isp, SCSI_DEBUG, "sync: cnt=%d, resp_in=%d, "
		    "resp_out=%d, offset=%ld, size=%d", n, response_in,
		    isp->isp_response_out, offset, sync_size);

		/*
		 * process each IOCB we found
		 */
		while (n-- > 0) {
			union isp_token tok;
			uint8_t xor;

			ISP_GET_NEXT_RESPONSE_OUT(isp, resp);
			ASSERT(resp != NULL);

			/*
			 * check raw isp_response data now,
			 * we used to check later but that was too late if
			 * the isp_response is corrupt
			 */
			if (isp_i_check_response_header(isp, resp, &xor)) {
				/* check_response_header failed, continue */
				continue;
			}

			ISP_COPY_IN_TOKEN(isp, resp, &tok.token);

			if ((slot = isp_i_check_token(isp, tok, xor)) ==
			    ISP_MAX_SLOTS) {
				/*
				 * oh oh -- likely a hardware (or firmware)
				 * error has caused the token to be corrupt,
				 * and the ID token lookup routine has
				 * detected it -- we need to reset the
				 * whole interface, since this was likely
				 * an I/O completion we just lost, and no
				 * telling which one it is
				 */
				if (tok.slot_n_seq.slot >= ISP_MAX_SLOTS) {
					isp_i_log(isp, CE_WARN,
					    "Out of range token in isp_intr "
					    "slot %x seq %x, XOR %x\n",
					    tok.slot_n_seq.slot,
					    tok.slot_n_seq.seq,
					    xor);
				} else {
					isp_i_log(isp, CE_WARN,
					    "Bad token in isp_intr slot %x "
					    "seq %x, current seq %x, XOR %x\n",
					    tok.slot_n_seq.slot,
					    tok.slot_n_seq.seq,
					    isp->isp_slots[
					    tok.slot_n_seq.slot].seq,
					    xor);
				}
				isp_i_fatal_error(isp, ISP_FROM_INTR);
				/*
				 * chip has been reset, so exit while loop
				 */
				break;
			}
			if ((sp = isp->isp_slots[slot].isp_cmd) == NULL) {
				/*
				 * The token is good but the slot is bad.
				 * Given that the sequence number should have
				 * been good the chances of this failure are
				 * slim, but better to be safe.
				 */
				isp_i_log(isp, CE_WARN,
				    "Null command in isp_intr, resetting");
				isp_i_fatal_error(isp, ISP_FROM_INTR);
				/*
				 * chip has been reset, so exit while loop
				 */
				break;
			}

#ifdef ISPDEBUG

			ASSERT((sp->cmd_flags & CFLAG_COMPLETED) == 0);
			ASSERT((sp->cmd_flags & CFLAG_FINISHED) == 0);
			sp->cmd_flags |= CFLAG_FINISHED;
			ISP_DEBUG4(isp, SCSI_DEBUG,
			    "isp_intr 0x%p done, pkt_time=%x", (void *)sp,
			    CMD2PKT(sp)->pkt_time);
#endif	/* ISPDEBUG */

			/*
			 * copy over response packet in sp
			 */
			ISP_COPY_IN_RESP(isp, resp,
			    &sp->cmd_isp_response);

			cmd_resp = &sp->cmd_isp_response;

			/*
			 * check for firmware returning the packet because
			 * the queue has been aborted (and is waiting for a
			 * marker) -- if this happens set both target and
			 * bus reset statistics (since we don't know which
			 * caused it)
			 *
			 * Note: when CMD_RESET returned no other fields of
			 * response are valid according to QLogic
			 */
			if (cmd_resp->resp_reason == CMD_RESET) {
				resp->resp_state = STAT_BUS_RESET << 8;
				ISP_DEBUG4(isp, SCSI_DEBUG, "found CMD_RESET");
			}

			/*
			 * Check response header flags.
			 */
			if (cmd_resp->resp_header.cq_flags &
			    CQ_FLAG_ERR_MASK) {
				ISP_DEBUG2(isp, SCSI_DEBUG,
				    "flag error (0x%x)",
				    cmd_resp->resp_header.cq_flags &
				    CQ_FLAG_ERR_MASK);
				if (isp_i_response_error(isp,
				    cmd_resp, slot) == ACTION_IGNORE) {
					continue;
				}
			}
			mutex_enter(ISP_REQ_MUTEX(isp));
			ISP_FREE_SLOT(isp, slot);
			mutex_exit(ISP_REQ_MUTEX(isp));

			if (head != NULL) {
				tail->cmd_forw = sp;
				tail = sp;
				tail->cmd_forw = NULL;
			} else {
				tail = head = sp;
				sp->cmd_forw = NULL;
			}

		}
update:
		ISP_SET_RESPONSE_OUT(isp);

		mutex_exit(ISP_RESP_MUTEX(isp));

		if (head != NULL) {
			isp_i_call_pkt_comp(head);
		}

		/*
		 * go around and check for another interrupt pending (rather
		 * than waiting until the system has to call us again)
		 */
	}

	/* keep track of the fact that this instance is alive */
	mutex_enter(ISP_REQ_MUTEX(isp));
	if (isp->busy_slots.tail != ISP_MAX_SLOTS &&
	    isp->isp_slots[isp->busy_slots.tail].timeout < gethrtime()) {
		mutex_exit(ISP_REQ_MUTEX(isp));
		/*
		 * Even though only one command has timed out reset the
		 * chip. The chip and the firmware should have timed this
		 * out for us so the chip is not right in some way.
		 */
		isp_i_log(isp, CE_WARN, "ISP: Command timed out, resetting");

		mutex_enter(ISP_RESP_MUTEX(isp));
		isp_i_fatal_error(isp,
		    ISP_FROM_INTR|ISP_FIRMWARE_ERROR|ISP_DOWNLOAD_FW_ON_ERR);
		mutex_exit(ISP_RESP_MUTEX(isp));
	} else {
		mutex_exit(ISP_REQ_MUTEX(isp));
	}
	isp->isp_alive = TRUE;

dun:
	/* signal possible waiting thread that we are done w/interrupt */
	mutex_enter(ISP_INTR_MUTEX(isp));
	if (isp->isp_in_intr != 0) {
		isp->isp_in_intr--;
	}

	if (isp->isp_in_intr == 0) {
		cv_broadcast(ISP_INTR_CV(isp));
	}
	/*
	 * check if DDI_INTR_UNCLAIMED and we had a polled cmd completion.
	 * It is possible that a mailbox cmd completed in isp_i_mbox_cmd_start
	 * and cleared the INTR already, so just return DDI_INTR_CLAIMED
	 * to avoid spurious kernel messages.
	 */
	if ((intr_claimed == DDI_INTR_UNCLAIMED) &&
	    (isp->isp_polled_completion == 1)) {
		intr_claimed = DDI_INTR_CLAIMED;
	}
	isp->isp_polled_completion = 0;

	mutex_exit(ISP_INTR_MUTEX(isp));

	return (intr_claimed);
}

static int
isp_i_check_response_header(struct isp *isp, struct isp_response *resp,
	uint8_t *xor)
{
	struct cq_header	*sbus_header;
	struct cq_header_pci	*pci_header;

	if (isp->isp_bus == ISP_SBUS) {
		sbus_header = (struct cq_header *)&(resp->resp_header);

		*xor = sbus_header->cq_seqno;
		/*
		 * Paranoia:  This should never happen.
		 * Do basic checks of response header early
		 */
		if (sbus_header->cq_entry_type == CQ_TYPE_REQUEST) {
			/*
			 * The firmware had problems w/this
			 * packet and punted.  Forge a reply
			 * in isp_i_response_error() and send
			 * it back to the target driver.
			 */
			sbus_header->cq_entry_type = CQ_TYPE_RESPONSE;
			sbus_header->cq_flags |= CQ_FLAG_FULL;
		} else if (sbus_header->cq_entry_type !=
		    CQ_TYPE_RESPONSE) {
			isp_i_log(isp, CE_WARN,
			    "invalid response:in=%x, out=%x, mbx5=%x",
			    isp->isp_response_in,
			    isp->isp_response_out,
			    ISP_READ_MBOX_REG(isp,
			    &isp->isp_mbox_reg->isp_mailbox5));
			isp_i_log(isp, CE_CONT,
			    "header type %x cnt %x flags %x %x",
			    sbus_header->cq_entry_type,
			    sbus_header->cq_entry_count,
			    sbus_header->cq_flags,
			    sbus_header->cq_seqno);
			return (1);
		}
	} else {
		/* pci card */
		pci_header = (struct cq_header_pci *)&(resp->resp_header);

		*xor = pci_header->cq_seqno;
		/*
		 * Paranoia:  This should never happen.
		 * Do basic checks of response header early
		 */
		if (pci_header->cq_entry_type == CQ_TYPE_REQUEST) {
			/*
			 * The firmware had problems w/this
			 * packet and punted.  Forge a reply
			 * in isp_i_response_error() and send
			 * it back to the target driver.
			 */
			pci_header->cq_entry_type = CQ_TYPE_RESPONSE;
			pci_header->cq_flags |= CQ_FLAG_FULL;
		} else if (pci_header->cq_entry_type != CQ_TYPE_RESPONSE) {
			isp_i_log(isp, CE_WARN,
			    "invalid response:in=%x, out=%x, mbx5=%x",
			    isp->isp_response_in,
			    isp->isp_response_out,
			    ISP_READ_MBOX_REG(isp,
			    &isp->isp_mbox_reg->isp_mailbox5));
			isp_i_log(isp, CE_CONT,
			    "header type %x cnt %x flags %x %x",
			    pci_header->cq_entry_type,
			    pci_header->cq_entry_count,
			    pci_header->cq_flags,
			    pci_header->cq_seqno);
			return (1);
		}
	}
	return (0);
}

/*
 * Function name : isp_i_call_pkt_comp()
 *
 * Return Values : none
 *
 * Description	 :
 *		   callback into target driver
 *		   argument is a  NULL-terminated list of packets
 *		   copy over stuff from response packet
 *
 * Context	 : Can be called by interrupt thread.
 */

#ifdef ISPDEBUG
static int isp_test_reason = 0;
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", isp_test_reason))
#endif

static void
isp_i_call_pkt_comp(struct isp_cmd *head)
{
	struct isp *isp;
	struct isp_cmd *sp;
	struct scsi_pkt *pkt;
	struct isp_response *resp;
	uchar_t status;
	ushort_t 	tgt;
	uint16_t 	lun;


	isp = CMD2ISP(head);

	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)) == 0 || ddi_in_panic());

	while (head != NULL) {
		sp = head;
		tgt = TGT(sp);
		lun = LUN(sp);
		pkt = CMD2PKT(sp);
		head = sp->cmd_forw;

		ASSERT(sp->cmd_flags & CFLAG_FINISHED);

		if (tgt >= NTARGETS_WIDE) {
			/*
			 * We should never get here.
			 * Need further investigation, if we get here.
			 */
			isp_i_log(isp, CE_CONT, "%d: Invalid target."
			    "Potential array overflow.", tgt);
			continue;
		}

		resp = &sp->cmd_isp_response;

		ISP_DEBUG4(isp, SCSI_DEBUG,
		    "completing pkt, sp=0x%p, reason=0x%x",
		    (void *)sp, resp->resp_reason);


		ISP_INC32_ERRSTATS(isp, isperr_completed_cmds);

		status = pkt->pkt_scbp[0] = (uchar_t)resp->resp_scb;
		if (pkt->pkt_reason == CMD_CMPLT) {
			pkt->pkt_reason = (uchar_t)resp->resp_reason;
			if (pkt->pkt_reason > CMD_UNX_BUS_FREE) {
				/*
				 * An underrun is not an error to be
				 * reported inside pkt->pkt_reason as
				 * resid will have this information
				 * for target drivers.
				 */
				if (pkt->pkt_reason == DATA_UNDER_RUN) {
					pkt->pkt_reason = CMD_CMPLT;
				} else if (pkt->pkt_reason == TAG_REJECT) {
					/*
					 * XXX: on SBUS this will never happen
					 * since the firmware doesn't ever
					 * return this -- on queue full it
					 * returns CMD_CMPLT and sense data
					 * of 0x28 (queue full), so isn't
					 * that what we should be checking
					 * for ???
					 */
					pkt->pkt_reason = CMD_TAG_REJECT;
				} else {
					/* catch all */
					pkt->pkt_reason = CMD_TRAN_ERR;
				}
			}
		}

		/*
		 * The packet state is actually the high byte of the
		 * resp_state
		 */
		pkt->pkt_state = resp->resp_state >> 8;
		pkt->pkt_statistics = resp->resp_status_flags;
		pkt->pkt_resid = (size_t)resp->resp_resid;

		if (pkt->pkt_statistics & ISP_STAT_SYNC) {
			isp_i_log(isp, CE_WARN,
			    "Target %d reducing transfer rate", tgt);
			ISP_MUTEX_ENTER(isp);
			isp->isp_backoff |= (1 << tgt);
			ISP_MUTEX_EXIT(isp);
			pkt->pkt_statistics &= ~ISP_STAT_SYNC;
		}
		/*
		 * check for parity errors
		 */
		if (pkt->pkt_statistics & STAT_PERR) {
			isp_i_log(isp, CE_WARN, "Parity Error");
		}
		/*
		 * Check to see if the ISP has negotiated a new sync
		 * rate with the device and store that information
		 * for a latter date
		 */
		if (pkt->pkt_statistics & ISP_STAT_NEGOTIATE) {
			ISP_MUTEX_ENTER(isp);
			isp_i_update_sync_prop(isp, sp);
			pkt->pkt_statistics &= ~ISP_STAT_NEGOTIATE;
			ISP_MUTEX_EXIT(isp);
		}



#ifdef ISPDEBUG
		if ((isp_test_reason != 0) &&
		    (pkt->pkt_reason == CMD_CMPLT)) {
			pkt->pkt_reason = (uchar_t)isp_test_reason;
			if (isp_test_reason == CMD_ABORTED) {
				pkt->pkt_statistics |= STAT_ABORTED;
			}
			if (isp_test_reason == CMD_RESET) {
				pkt->pkt_statistics |=
				    STAT_DEV_RESET | STAT_BUS_RESET;
			}
			isp_test_reason = 0;
		}
		if (pkt->pkt_resid || status ||
		    pkt->pkt_reason) {
			uchar_t *cp;
			char buf[128];
			int i;

			ISP_DEBUG4(isp, SCSI_DEBUG,
	"tgt %d.%d: resid=%lx,reason=%s,status=%x,stats=%x,state=%x",
			    TGT(sp), LUN(sp), pkt->pkt_resid,
			    scsi_rname(pkt->pkt_reason),
			    (unsigned)status,
			    (unsigned)pkt->pkt_statistics,
			    (unsigned)pkt->pkt_state);

			cp = (uchar_t *)pkt->pkt_cdbp;
			buf[0] = '\0';
			for (i = 0; i < (int)sp->cmd_cdblen; i++) {
				(void) sprintf(
				    &buf[strlen(buf)], " 0x%x", *cp++);
				if (strlen(buf) > 124) {
					break;
				}
			}
			ISP_DEBUG4(isp, SCSI_DEBUG,
			"\tcflags=%x, cdb dump: %s", sp->cmd_flags, buf);

			if (pkt->pkt_reason == CMD_RESET) {
				ASSERT(pkt->pkt_statistics &
				    (STAT_BUS_RESET | STAT_DEV_RESET
				    | STAT_ABORTED));
			} else if (pkt->pkt_reason ==
			    CMD_ABORTED) {
				ASSERT(pkt->pkt_statistics &
				    STAT_ABORTED);
			}
		}
		if (pkt->pkt_state & STATE_XFERRED_DATA) {
			if (ispdebug > 1 && pkt->pkt_resid) {
				ISP_DEBUG4(isp, SCSI_DEBUG,
				    "%d.%d finishes with %ld resid",
				    TGT(sp), LUN(sp), pkt->pkt_resid);
			}
		}
#endif	/* ISPDEBUG */

		/* user raw isp resp_reason for error counter */
		switch (resp->resp_reason) {
		case CMD_CMPLT:
			break;
		case CMD_INCOMPLETE:
			ISP_INC32_ERRSTATS(isp, isperr_cmd_incomplete);
			ISP_SET64_ERRSTATS(isp, isperr_lbolt_incomplete,
			    ddi_get_lbolt());
			break;
		case CMD_DMA_DERR:
			ISP_INC32_ERRSTATS(isp, isperr_cmd_dma_err);
			ISP_SET64_ERRSTATS(isp, isperr_lbolt_dma_err,
			    ddi_get_lbolt());
			break;
		case CMD_TRAN_ERR:
			ISP_INC32_ERRSTATS(isp, isperr_cmd_tran_err);
			ISP_SET64_ERRSTATS(isp, isperr_lbolt_tran_err,
			    ddi_get_lbolt());
			break;
		case CMD_RESET:
			ISP_INC32_ERRSTATS(isp, isperr_cmd_reset);
			ISP_SET64_ERRSTATS(isp, isperr_lbolt_reset,
			    ddi_get_lbolt());
			break;
		case CMD_ABORTED:
			ISP_INC32_ERRSTATS(isp, isperr_cmd_abort);
			ISP_SET64_ERRSTATS(isp, isperr_lbolt_abort,
			    ddi_get_lbolt());
			break;
		case CMD_TIMEOUT:
			ISP_INC32_ERRSTATS(isp, isperr_cmd_timeout);
			ISP_SET64_ERRSTATS(isp, isperr_lbolt_timeout,
			    ddi_get_lbolt());
			break;
		case CMD_DATA_OVR:
		case CMD_TERMINATED:
			ISP_INC32_ERRSTATS(isp, isperr_cmd_data_error);
			ISP_SET64_ERRSTATS(isp, isperr_lbolt_data_error,
			    ddi_get_lbolt());
			break;
		case CMD_CMD_OVR:
			ISP_INC32_ERRSTATS(isp, isperr_cmd_overrun);
			ISP_SET64_ERRSTATS(isp, isperr_lbolt_overrun,
			    ddi_get_lbolt());
			break;
		default:
			ISP_INC32_ERRSTATS(isp, isperr_cmd_misc_error);
			ISP_SET64_ERRSTATS(isp, isperr_lbolt_misc_error,
			    ddi_get_lbolt());
			break;
		}


		/*
		 * was there a check condition and auto request sense?
		 * fake some arqstat fields
		 */
		if ((status != 0)) {
			if ((pkt->pkt_state &
			    (STATE_GOT_STATUS | STATE_ARQ_DONE)) ==
			    (STATE_GOT_STATUS | STATE_ARQ_DONE)) {
				isp_i_handle_arq(isp, sp);
			} else {
				uint16_t	arq_cap, disabled_arq;

				ISP_MUTEX_ENTER(isp);
				arq_cap = (isp->isp_cap[tgt] &
				    ISP_CAP_AUTOSENSE);
				disabled_arq = (sp->cmd_isp_request.req_flags &
				    ISP_REQ_FLAG_DISARQ);
				if ((arq_cap == 0) || (disabled_arq != 0)) {
					/*
					 * bad status, but ARQ was turned off.
					 * fw will suspend all IOCBs to lun,
					 * will restart queue on next cmd
					 */
					isp->isp_arq_pending[tgt] |= (1 << lun);
				}
				ISP_MUTEX_EXIT(isp);
			}
		}

		/*
		 * if data was xferred and this was an IOPB, we need
		 * to do a dma sync
		 */
		if ((sp->cmd_flags & CFLAG_CMDIOPB) &&
		    (pkt->pkt_state & STATE_XFERRED_DATA)) {

			/*
			 * only one segment yet
			 */
			(void) ddi_dma_sync(sp->cmd_dmahandle, 0,
			    sp->cmd_dmacount, DDI_DMA_SYNC_FORKERNEL);
		}


		/*
		 * pkt had better be in transport, be finished, and not
		 * be completed
		 */
		ASSERT(sp->cmd_flags & CFLAG_IN_TRANSPORT);
		ASSERT(sp->cmd_flags & CFLAG_FINISHED);
		ASSERT((sp->cmd_flags & CFLAG_COMPLETED) == 0);

		sp->cmd_flags = (sp->cmd_flags & ~CFLAG_IN_TRANSPORT) |
		    CFLAG_COMPLETED;

		/*
		 * Call packet completion routine if FLAG_NOINTR is not set.
		 * If FLAG_NOINTR is set turning on CFLAG_COMPLETED in line
		 * above will cause busy wait loop in
		 * isp_i_polled_cmd_start() to exit.
		 */
		if (!(pkt->pkt_flags & FLAG_NOINTR) &&
		    (pkt->pkt_comp != NULL)) {
			ISP_DEBUG4(isp, SCSI_DEBUG,
			    "completing pkt(tgt), sp=0x%p, reason=0x%x",
			    (void *)sp, pkt->pkt_reason);
			(*pkt->pkt_comp)(pkt);
		}
	}
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)) == 0 || ddi_in_panic());
}


static short
isp_i_get_mbox_event(struct isp *isp)
{
	return ((short)
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox0));
}

/*
 * Function name : isp_i_handle_arq()
 *
 * Description	 : called on an autorequest sense condition, sets up arqstat
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_i_handle_arq(struct isp *isp, struct isp_cmd *sp)
{
	struct isp_response *resp = &sp->cmd_isp_response;
	struct scsi_pkt *pkt = CMD2PKT(sp);
	uint8_t		sense_size;
	char		status;


	ASSERT(isp != NULL);


	ISP_INC32_ERRSTATS(isp, isperr_ars_pkts);

	if (sp->cmd_scblen >= sizeof (struct scsi_arq_status)) {
		struct scsi_arq_status *arqstat;

		ISP_DEBUG3(isp, SCSI_DEBUG,
		    "tgt %d.%d: auto request sense", TGT(sp), LUN(sp));
		arqstat = (struct scsi_arq_status *)(pkt->pkt_scbp);
		status = pkt->pkt_scbp[0];
		bzero((caddr_t)arqstat, sp->cmd_scblen);

		/* normal ARQ, 20 bytes */
		sense_size = (uint8_t)SENSE_LENGTH;
		if (resp->resp_rqs_count < SENSE_LENGTH) {
			arqstat->sts_rqpkt_resid = (size_t)
			    SENSE_LENGTH - resp->resp_rqs_count;
		}
		arqstat->sts_rqpkt_state = (STATE_GOT_BUS |
		    STATE_GOT_TARGET | STATE_SENT_CMD |
		    STATE_XFERRED_DATA | STATE_GOT_STATUS);

		/*
		 * use same statistics as the original cmd
		 */
		arqstat->sts_rqpkt_statistics = pkt->pkt_statistics;
		bcopy(resp->resp_request_sense, &arqstat->sts_sensedata,
		    sense_size);
		/*
		 * restore status which was wiped out by bzero
		 */
		pkt->pkt_scbp[0] = status;

		if (isp_debug_ars) {
			struct scsi_extended_sense	*s =
			    (struct scsi_extended_sense *)
			    resp->resp_request_sense;

			isp_i_log(isp, CE_NOTE,
			    "tgt %d.%d: ARS, stat=0x%x,key=0x%x,add=0x%x/0x%x",
			    TGT(sp), LUN(sp), status,
			    s->es_key, s->es_add_code, s->es_qual_code);
			isp_i_log(isp, CE_CONT,
			    "   cdb 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x ...",
			    pkt->pkt_cdbp[0], pkt->pkt_cdbp[1],
			    pkt->pkt_cdbp[2], pkt->pkt_cdbp[3],
			    pkt->pkt_cdbp[4], pkt->pkt_cdbp[5]);
		}
	} else {
		/*
		 * bad packet; can't copy over ARQ data
		 * XXX need CMD_BAD_PKT
		 */
		ISP_SET_REASON(sp, CMD_TRAN_ERR);
	}
}

/*
 * Function name : isp_i_response_error()
 *
 * Return Values : ACTION_CONTINUE
 *		   ACTION_IGNORE
 *
 * Description	 : handle response packet error conditions
 *
 * Context	 : Called by interrupt thread
 */
static int
isp_i_response_error(struct isp *isp, struct isp_response *resp,
	uint16_t slot)
{
	struct isp_cmd	*sp;
	int		rval;

	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(resp != NULL);


	ISP_INC32_ERRSTATS(isp, isperr_response_error);

	/*
	 * RAS: in real life, this token has been verified as being non-null
	 *	in isp_intr(), which called us, but we check again here
	 *	just to be safe, since we're about to deference the value
	 *	we get
	 */
	sp = isp->isp_slots[slot].isp_cmd;
	ASSERT(sp != NULL);

	/*
	 * we handle "queue full" (which we periodically expect),
	 * and everything else (which we don't)
	 */
	if (resp->resp_header.cq_flags & CQ_FLAG_FULL) {

		ISP_DEBUG2(isp, SCSI_DEBUG,
		    "isp_i_response_error: queue full");

		/*
		 * Need to forge request sense of busy.
		 */
		resp->resp_scb = STATUS_BUSY;
		resp->resp_rqs_count = 1;	/* cnt of request sense data */
		resp->resp_reason = CMD_CMPLT;
		resp->resp_state = 0;

		rval = ACTION_CONTINUE;
	} else {

		/*
		 * For bad request pkts, flag error and try again.
		 * This should *NEVER* happen.
		 */
		ISP_SET_REASON(sp, CMD_TRAN_ERR);
		if (resp->resp_header.cq_flags & CQ_FLAG_BADPACKET) {
			isp_i_log(isp, CE_WARN, "Bad request pkt");
		} else if (resp->resp_header.cq_flags & CQ_FLAG_BADHEADER) {
			isp_i_log(isp, CE_WARN, "Bad request pkt header");
		}
		rval = ACTION_CONTINUE;
	}

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());

	return (rval);
}


/*
 * Function name : isp_i_polled_cmd_start()
 *
 * Return Values : TRAN_ACCEPT	if transaction was accepted
 *		   TRAN_BUSY	if I/O could not be started
 *		   TRAN_ACCEPT	if I/O timed out, pkt fields indicate error
 *
 * Description	 : Starts up I/O in normal fashion by calling isp_i_start_cmd().
 *		   Busy waits for I/O to complete or timeout.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_i_polled_cmd_start(struct isp *isp, struct isp_cmd *sp)
{
	int delay_loops;
	int rval;
	struct scsi_pkt *pkt = CMD2PKT(sp);
	uint16_t slot;

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());


	/*
	 * set timeout to SCSI_POLL_TIMEOUT for non-polling
	 * commands that do not have this field set
	 */
	if (pkt->pkt_time == 0) {
		pkt->pkt_time = SCSI_POLL_TIMEOUT;
	}

	/*
	 * try and start up command
	 */
	mutex_enter(ISP_REQ_MUTEX(isp));
	rval = isp_i_start_cmd(isp, sp, &slot);
	isp_check_waitq_and_exit_req_mutex(isp);
	if (rval != TRAN_ACCEPT) {
		goto done;
	}

	/*
	 * busy wait for command to finish ie. till CFLAG_COMPLETED is set
	 */
	delay_loops = ISP_TIMEOUT_DELAY(
	    (pkt->pkt_time + (2 * ISP_GRACE)),
	    ISP_NOINTR_POLL_DELAY_TIME);

	ISP_DEBUG3(isp, SCSI_DEBUG,
	    "polled_cmd_start delay_loops=%d, delay=%d, pkt_time=%x, cdb[0]=%x",
	    delay_loops, ISP_NOINTR_POLL_DELAY_TIME, pkt->pkt_time,
	    *(CMD2PKT(sp)->pkt_cdbp));

	while ((sp->cmd_flags & CFLAG_COMPLETED) == 0) {
		drv_usecwait(ISP_NOINTR_POLL_DELAY_TIME);


		if (--delay_loops <= 0) {
			struct isp_response *resp;

			/*
			 * Call isp_scsi_abort()  to abort the I/O
			 * and if isp_scsi_abort fails then
			 * blow everything away
			 */
			if ((isp_scsi_reset(&pkt->pkt_address, RESET_TARGET))
			    == FALSE) {
				isp_i_log(isp, CE_WARN, "isp_i_polled_cmd_start"
				    ": isp_scsi_reset failed");
				mutex_enter(ISP_RESP_MUTEX(isp));
				isp_i_fatal_error(isp, 0);
				mutex_exit(ISP_RESP_MUTEX(isp));
			}

			resp = &sp->cmd_isp_response;
			bzero((caddr_t)resp, sizeof (struct isp_response));

			/*
			 * update stats in resp_status_flags
			 * isp_i_call_pkt_comp() copies this
			 * over to pkt_statistics
			 */
			resp->resp_status_flags |=
			    STAT_BUS_RESET | STAT_TIMEOUT;
			resp->resp_reason = CMD_TIMEOUT;
#ifdef ISPDEBUG
			sp->cmd_flags |= CFLAG_FINISHED;
#endif
			mutex_enter(ISP_REQ_MUTEX(isp));
			ISP_FREE_SLOT(isp, slot);
			mutex_exit(ISP_REQ_MUTEX(isp));

			isp_i_call_pkt_comp(sp);
			break;
		}
		/*
		 * This call is required to handle the cases when
		 * isp_intr->isp_i_call_pkt_comp->sdintr->
		 * sd_handle_autosense->sd_decode_sense->sd_handle_ua
		 * sd_handle_mchange->scsi_poll. You will be in the intr.
		 *
		 * this check *must* be present in order for booting
		 * from isp to work, since polled commands are used at
		 * that time, and the interrupt framework may not yet
		 * be enabled
		 */
		if (ISP_INT_PENDING(isp)) {
			(void) isp_intr((caddr_t)isp);
		}

	}
	ISP_DEBUG4(isp, SCSI_DEBUG, "polled cmd done\n");

done:

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());

	return (rval);
}


/*
 * Function name : isp_i_handle_aen
 *
 * Return Values : -1	Fatal error occurred
 *		    0	normal return
 * Description	 :
 * An Event of 8002 is a Sys Err in the ISP.  This would require
 *	Chip reset.
 *
 * An Event of 8001 is a external SCSI Bus Reset
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 *
 * called w/both mutexes held from interrupt context
 */
static int
isp_i_handle_aen(struct isp *isp, short event)
{
	int		rval = ISP_AEN_SUCCESS;
	int		target_lun, target;
	ushort_t	mbox_event;

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));


	switch (mbox_event = ISP_GET_MBOX_EVENT(event)) {
	case ISP_MBOX_ASYNC_ERR:
		/*
		 * Force the current commands to timeout after
		 * resetting the chip.
		 */
		isp_i_log(isp, CE_WARN, "SCSI Cable/Connection problem.");
		isp_i_log(isp, CE_CONT, "Hardware/Firmware error.");
		ISP_DEBUG2(isp, SCSI_DEBUG, "Mbx1/PanicAddr=0x%x, Mbx2=0x%x",
		    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox1),
		    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox2));
		rval = ISP_AEN_ASYNC_ERR;
		break;

	case ISP_MBOX_ASYNC_REQ_DMA_ERR:
	case ISP_MBOX_ASYNC_RESP_DMA_ERR:
		/*
		 *  DMA failed in the ISP chip force a Reset
		 */
		isp_i_log(isp, CE_WARN, "DMA Failure (%x)", event);
		rval = ISP_AEN_DMA_ERR;
		break;

	case ISP_MBOX_ASYNC_RESET:
		isp_i_log(isp, CE_WARN, "Received unexpected SCSI Reset");
		/* FALLTHROUGH */
	case ISP_MBOX_ASYNC_OVR_RESET:
		/* FALLTHROUGH */
	case ISP_MBOX_ASYNC_INT_RESET:
		/*
		 * Set a marker to for a internal SCSI BUS reset done
		 * to recover from a timeout.
		 */
		ISP_DEBUG2(isp, SCSI_DEBUG,
		    "ISP initiated SCSI BUS Reset or external SCSI Reset");
		ISP_MUTEX_EXIT(isp);
		if (isp_i_set_marker(isp, SYNCHRONIZE_ALL, 0, 0)) {
			ISP_DEBUG2(isp, SCSI_DEBUG,
			    "can't set marker, all targets (async): queueing");
		}
		ISP_MUTEX_ENTER(isp);
		rval = ISP_AEN_RESET_ERR;
		break;

	case ISP_MBOX_ASYNC_EXTMSG_ERROR:
	case ISP_MBOX_ASYNC_INT_DEV_RESET:
		/* Get the target an lun value */
		target_lun = ISP_READ_MBOX_REG(isp,
		    &isp->isp_mbox_reg->isp_mailbox1);
		target = (target_lun >> 8) & 0xff;
		ISP_DEBUG2(isp, SCSI_DEBUG,
		    "ISP initiated SCSI Device Reset (reason timeout?)");
		ISP_MUTEX_EXIT(isp);
		/* Post the Marker to synchrnise the target */
		if (isp_i_set_marker(isp, SYNCHRONIZE_TARGET, target, 0)) {
			ISP_DEBUG2(isp, SCSI_DEBUG,
			    "can't set marker, target %d (async): queueing",
			    target);
		}
		ISP_MUTEX_ENTER(isp);
		rval = ISP_AEN_DEV_RESET_ERR;
		break; /* Leave holding mutex */

	default:
		isp_i_log(isp, CE_NOTE,
		    "Unknown AEN 0x%x: mbox regs, %x %x %x %x %x %x", event,
		    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox0),
		    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox1),
		    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox2),
		    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox3),
		    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox4),
		    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox5));
		rval = ISP_AEN_DEFAULT_ERR;
		break;
	}
	ISP_CLEAR_RISC_INT(isp);
	ISP_CLEAR_SEMAPHORE_LOCK(isp);

	if (isp_debug_reset_sent) {
		/* check if the firmware issued a SCSI bus reset or BDR */
		if ((mbox_event == ISP_MBOX_ASYNC_INT_RESET) ||
		    (mbox_event == ISP_MBOX_ASYNC_BUS_HANG) ||
		    (mbox_event == ISP_MBOX_ASYNC_OVR_RESET)) {
			isp_i_log(isp, CE_NOTE, "isp fw sent Bus Reset, %x",
			    event);
		} else if ((mbox_event == ISP_MBOX_ASYNC_EXTMSG_ERROR) ||
		    (mbox_event == ISP_MBOX_ASYNC_INT_DEV_RESET)) {
			target_lun = ISP_READ_MBOX_REG(isp,
			    &isp->isp_mbox_reg->isp_mailbox1);
			target = (target_lun >> 8) & 0xff;
			isp_i_log(isp, CE_NOTE, "isp fw sent Device Reset, %x"
			    "tgt %d, lun %d", event, target,
			    (target_lun & 0xff));
		}
	}


	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	return (rval);
}

static void
isp_i_complete_aen(struct isp *isp, int async_event, int from_intr)
{
	int	intr_flag;

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));

	intr_flag = (from_intr ? ISP_FROM_INTR : 0);
	switch (async_event) {
	case ISP_AEN_ASYNC_ERR:
		isp_i_fatal_error(isp, (ISP_FIRMWARE_ERROR |
		    ISP_DOWNLOAD_FW_ON_ERR | intr_flag));
		break;

	case ISP_AEN_DMA_ERR:
		isp_i_fatal_error(isp, (ISP_FORCE_RESET_BUS | intr_flag));
		break;

	case ISP_AEN_RESET_ERR:
		(void) scsi_hba_reset_notify_callback(ISP_RESP_MUTEX(isp),
		    &isp->isp_reset_notify_listf);
		break;
	}

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
}

/*
 * Function name : isp_i_mbox_cmd_complete ()
 *
 * Return Values : None.
 *
 * Description	 : Sets ISP_MBOX_CMD_FLAGS_COMPLETE flag so busy wait loop
 *		   in isp_i_mbox_cmd_start() exits.
 *
 * Context	 : Can be called by interrupt thread only.
 *
 * Semaphores	: might have both req/resp locks or neither
 */
static void
isp_i_mbox_cmd_complete(struct isp *isp)
{
	uint16_t *mbox_regp;
	int delay_loops;
	uchar_t i;


	ASSERT(isp != NULL);

	mbox_regp = &isp->isp_mbox_reg->isp_mailbox0;

	ISP_DEBUG3(isp, SCSI_DEBUG,
	    "isp_i_mbox_cmd_complete: (cmd = 0x%x)",
	    isp->isp_mbox.mbox_cmd.mbox_out[0]);


	/*
	 * Check for completions that are caused by mailbox events
	 * but that do not set the mailbox status bit ie. 0x4xxx
	 * For now only the busy condition is checked, the others
	 * will automatically time out and error.
	 */
	delay_loops = ISP_TIMEOUT_DELAY(ISP_MBOX_CMD_BUSY_WAIT_TIME,
	    ISP_MBOX_CMD_BUSY_POLL_DELAY_TIME);
	while (ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox0) ==
	    ISP_MBOX_BUSY) {
		drv_usecwait(ISP_MBOX_CMD_BUSY_POLL_DELAY_TIME);
		if (--delay_loops < 0) {
			ISP_MBOX_RETURN_REG(&(isp->isp_mbox.mbox_cmd), 0) =
			    ISP_MBOX_STATUS_FIRMWARE_ERR;
			goto fail;
		}
	}

	/*
	 * save away status registers
	 */
	for (i = 0; i < ISP_MAX_MBOX_REGS; i++, mbox_regp++) {
		ISP_MBOX_RETURN_REG(&(isp->isp_mbox.mbox_cmd), i) =
		    ISP_READ_MBOX_REG(isp, mbox_regp);
	}

fail:
	/*
	 * set flag so that busy wait loop will detect this and return
	 */
	isp->isp_mbox.mbox_flags |= ISP_MBOX_CMD_FLAGS_COMPLETE;

	/*
	 * clear the risc interrupt only if no more interrupts are pending
	 *
	 * We do not need isp_response_mutex because of isp semaphore
	 * lock is held.
	 */
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(isp->isp_response_out))
	if (((isp_i_get_response_in_db(isp) - isp->isp_response_out) == 0) ||
	    (isp->isp_mbox.mbox_flags & ISP_MBOX_CMD_FLAGS_Q_NOT_INIT) ||
	    (ISP_MBOX_RETURN_STATUS(&(isp->isp_mbox.mbox_cmd)) ==
	    ISP_MBOX_STATUS_FIRMWARE_ERR)) {
		/* clear pending interrupt indication */
		ISP_CLEAR_RISC_INT(isp);
	}
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(isp->isp_response_out))

	/*
	 * clear the semaphore lock
	 */
	ISP_CLEAR_SEMAPHORE_LOCK(isp);

	ISP_SET64_ERRSTATS(isp, isperr_mailbox_started, 0);
}

/*
 * Function name : isp_i_backoff_host_int ()
 *
 * Return values : 0 if the host int bit is set 1 if it is not.
 *
 * Description   :
 *
 * 	Busy loop on the host int bit using a quadratic back off.
 *
 * 	Loops around for a maximum of total_time usec checking the
 * 	host int bit each time around. Doubling the timeout each
 * 	time around the loop until the timeout is greater than
 *	max_sleep, then it is capped at max_sleep. The last time where
 *	the remainder of the time is used up.  Then return if the
 *	bit is set or not.
 *
 * Context	 : Can be called from any context
 *
 * Semaphores	 : must own the isp mailbox semaphore.
 */

static int
isp_i_backoff_host_int(struct isp *isp, clock_t total_time,
	clock_t max_sleep)
{
	clock_t slept = 0; /* How long we have slept for */
	clock_t sleep = 2; /* start point */

	while ((slept < total_time) && (0 != sleep)) {
		drv_usecwait(sleep);
		if (!ISP_REG_GET_HOST_INT(isp)) {
			return (1);
		}
		slept += sleep;
		if (sleep != max_sleep) {
			sleep <<= 1;
			sleep = MIN(sleep, max_sleep);
		}
		sleep = MIN(sleep, total_time - slept);
	}
	/* give it just one last chance */
	return (ISP_REG_GET_HOST_INT(isp) ? 0 : 1);
}

/*
 * Function name : isp_i_mbox_cmd_start ()
 *
 * Return Values : 0 for success else return non-zero
 *
 * Description	 : Initializes then sends a mailbox command, waiting
 *		   for it to complete.
 *
 * Context	 : Can be called from any context
 *
 * Semaphores	: must own request and response queues
 */
int
isp_i_mbox_cmd_start(struct isp *isp, struct isp_mbox_cmd *cmdp,
    ushort_t op_code, ...)
{
	struct isp_mbox_cmd_info	*ip;
	ushort_t			*mbox_regp;
	int				res = -1;	/* default result */
	int				args_out;	/* #args to chip */
	va_list				ap;		/* for varargs */
	int				delay_loops;	/* for loop */
	int				i;		/* loop index */
	int				aen_event;
	ushort_t			released_sema = FALSE;
	char				retry_cnt =
	    (char)ISP_MBOX_CMD_RETRY_CNT;
#ifdef ISPDEBUG
	int				laststate[16];
#endif

	ASSERT(isp != NULL);
	ASSERT(cmdp != NULL);

	ASSERT(op_code < isp_mbox_cmd_list_size);

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));


	/* get ptr to our cmd entry */
	ip = &isp_mbox_cmd_list[op_code];

	if (isp_debug_mbox > 1) {
		isp_i_log(isp, CE_NOTE,
		    "mbox_cmd_run: op_code=0x%x, args out=%d, args back=%d",
		    op_code, ip->mbox_cmd_num_params_out,
		    ip->mbox_cmd_num_params_in);
	}

	/* see if this is a valid command */
	if (ip->mbox_cmd_num_params_out < 0) {
		/* oh oh - internal error! what to do? just return failure */
		ISP_DEBUG2(isp, SCSI_DEBUG, "illegal internal command");
		goto just_go;
	}

	/* set # args/returns based on our entry */
	args_out = ip->mbox_cmd_num_params_out;
	cmdp->n_mbox_out = ip->mbox_cmd_num_params_out + 1;
	cmdp->n_mbox_in	= ip->mbox_cmd_num_params_in + 1;

	/* set op code */
	cmdp->mbox_out[0] = op_code;

	/* now get up to max args */
	if (args_out > 0) {
		va_start(ap, op_code);
		for (i = 1; i <= args_out; i++) {
			cmdp->mbox_out[i] = va_arg(ap, int);
		}
	}

	/*
	 * allow only one thread to access mailboxes
	 *
	 * release queue mutexes before the sema_p to avoid a deadlock when
	 * another thread needs to do a mailbox cmd and this thread needs
	 * to reenter the mutex again (see below) while holding the semaphore
	 */
	ISP_MUTEX_EXIT(isp);
	sema_p(ISP_MBOX_SEMA(isp));
	ISP_MUTEX_ENTER(isp);

	/*
	 * save away mailbox command in state structure
	 */
	bcopy(cmdp, &isp->isp_mbox.mbox_cmd, sizeof (struct isp_mbox_cmd));
	ISP_SET64_ERRSTATS(isp, isperr_mailbox_started, ddi_get_lbolt());
	ISP_INC32_ERRSTATS(isp, isperr_mailbox_cmds);

retry:

	/*
	 * Verify that the host interrupt bit is not still set from a
	 * previous mailbox command.
	 *
	 * The host interrupt bit is unset by the card to signal that it
	 * is ready to accept the next mailbox command. Then and only then
	 * can the driver load the new mailbox command then set the bit to
	 * signal to the card that there is a command to be processed.
	 *
	 * On fast machines when the card is under heavy load it is possible
	 * for the bit to still be set here. If the bit is still set when
	 * the driver sets it the card will not see the change, as there is
	 * no change, and the command is lost. Hence the busy wait. The first
	 * check is done here to aviod the function call then any remaining
	 * checks are done in isp_i_backoff_host_int() as by then we are
	 * burning cycles anyway.
	 */
	if (ISP_REG_GET_HOST_INT(isp)) {
		if (isp_i_backoff_host_int(isp,
		    (clock_t)SEC_TO_USEC(ISP_MBOX_CMD_TIMEOUT),
		    (clock_t)ISP_MBOX_CMD_BUSY_POLL_DELAY_TIME) == 0) {
			sema_v(ISP_MBOX_SEMA(isp));
			released_sema = TRUE;
			isp_i_log(isp, CE_WARN,
			    "Interrupt bit still set after %d seconds. "
			    "Card or firmware failure.",
			    ISP_MBOX_CMD_TIMEOUT);
			goto just_go;
		}
	}

	/*
	 * get ptr to first mailbox register
	 *
	 * XXX: this technique assume that the mailbox register fields in
	 * the mailbox reg struct are contiguous -- this might be very
	 * dangerous in the long run (i.e. if struct changes), but it
	 * currently seems to work
	 */
	mbox_regp = (ushort_t *)&isp->isp_mbox_reg->isp_mailbox0;

	/* write outgoing registers */
	for (i = 0; i < cmdp->n_mbox_out; i++, mbox_regp++) {
		ISP_WRITE_MBOX_REG(isp, mbox_regp, cmdp->mbox_out[i]);
	}

#ifdef ISP_PERF
	isp->isp_wpio_count += isp->isp_mbox.mbox_cmd.n_mbox_out;
	isp->isp_mail_requests++;
#endif /* ISP_PERF */

	/*
	 * Turn completed flag off indicating mbox command was issued.
	 * Interrupt handler will set flag when done.
	 */
	isp->isp_mbox.mbox_flags &= ~ISP_MBOX_CMD_FLAGS_COMPLETE;

	/* signal isp firmware that mailbox cmd was loaded */
	ISP_REG_SET_HOST_INT(isp);

	/* busy wait for mailbox cmd to be processed. */
	delay_loops = ISP_TIMEOUT_DELAY(ISP_MBOX_CMD_TIMEOUT,
	    ISP_MBOX_CMD_BUSY_POLL_DELAY_TIME);

	aen_event = 0;
#ifdef ISPDEBUG
	bzero(&laststate[0], sizeof (laststate));
#endif
	/*
	 * release mutexes, we are now protected by the sema and we don't
	 * want to deadlock with isp_intr()
	 */
	ISP_MUTEX_EXIT(isp);

	/*
	 * need to do polling mbox cmd complete because intrs do not
	 * always work during boot and/or dump
	 */
	while (!(isp->isp_mbox.mbox_flags & ISP_MBOX_CMD_FLAGS_COMPLETE)) {
		ushort_t	mbox_event;

		/* sleep a while */
		drv_usecwait(ISP_MBOX_CMD_BUSY_POLL_DELAY_TIME);

		/* if cmd does not complete retry or return error */
		if (--delay_loops <= 0) {
			/* we've used up all of our retry loops */
			if (--retry_cnt <= 0) {
				ISP_DEBUG(isp, SCSI_DEBUG,
				    " too many retries sending mbox cmd");
#ifdef ISPDEBUG
				laststate[1]++;
#endif
				goto done;
			}
			/* go retry again */
			ISP_MUTEX_ENTER(isp);
#ifdef ISPDEBUG
			laststate[2]++;
#endif
			goto retry;
		}

		/*
		 * check to see if the mailbox command has complete
		 * already -- this is an optimization that's *needed*
		 * during booting/dumping (i.e when interrupts are not
		 * enabled)
		 */
#ifdef ISPDEBUG
		laststate[3]++;
#endif
		ISP_MUTEX_ENTER(isp);

		if ((isp->isp_checking_semlock) ||
		    !ISP_CHECK_SEMAPHORE_LOCK(isp)) {
			/*
			 * someone else is checking isp sema or
			 * do we have a mailbox command completion?
			 */
			ISP_MUTEX_EXIT(isp);
#ifdef ISPDEBUG
			laststate[4]++;
#endif
			continue;
		}

		isp->isp_checking_semlock = TRUE;

#ifdef ISPDBEUG
		laststate[5]++;
#endif
		/* get mailbox event */
		mbox_event = isp_i_get_mbox_event(isp);

#ifdef ISPDEBUG
		laststate[6]++;
#endif
#ifdef ISP_PERF
		isp->isp_rpio_count += 1;
#endif
		if (mbox_event & ISP_MBOX_EVENT_AEN) {
			/*
			 * if an async event occurs during
			 * fatal error recovery, we are hosed
			 * with a recursive mutex panic
			 */
			switch (ISP_GET_MBOX_EVENT(mbox_event)) {
			case ISP_MBOX_ASYNC_ERR:
			case ISP_MBOX_ASYNC_REQ_DMA_ERR:
			case ISP_MBOX_ASYNC_RESP_DMA_ERR:
				sema_v(ISP_MBOX_SEMA(isp));
				released_sema = TRUE;
#ifdef ISPDEBUG
				laststate[7]++;
#endif
				break;
			}
			aen_event = isp_i_handle_aen(isp, mbox_event);
			isp->isp_checking_semlock = FALSE;
			if (aen_event & ISP_AEN_ERROR_MASK) {
				/*
				 * Do not release the req/resp mutexes
				 * as the calling function is holding them
				 */
#ifdef ISPDEBUG
				laststate[8]++;
#endif
				goto just_go;
			}
#ifdef ISPDEBUG
			laststate[9]++;
#endif
		} else if (mbox_event & ISP_MBOX_EVENT_CMD) {
			mutex_enter(ISP_INTR_MUTEX(isp));
			isp->isp_polled_completion = 1;
			mutex_exit(ISP_INTR_MUTEX(isp));

			isp_i_mbox_cmd_complete(isp);
			isp->isp_checking_semlock = FALSE;
			aen_event = ISP_AEN_MBOX_COMPLETED;
#ifdef ISPDEBUG
			laststate[10]++;
#endif
		} else {
			isp_i_log(isp, CE_WARN, "isp_mbox_cmd_start: unknown"
			    "mbox event %x", mbox_event);
			isp->isp_checking_semlock = FALSE;
			aen_event = ISP_AEN_UNKNOWN;
		}

#ifdef ISPDEBUG
		laststate[11]++;
#endif
		ISP_MUTEX_EXIT(isp);

		/* do any AEN error recovery needed */
		if (aen_event & ISP_AEN_NEED_RECOVERY) {
			mutex_enter(ISP_RESP_MUTEX(isp));
			mutex_enter(ISP_REQ_MUTEX(isp));
			sema_v(ISP_MBOX_SEMA(isp));
			released_sema = TRUE;
			mutex_exit(ISP_REQ_MUTEX(isp));
			isp_i_complete_aen(isp, aen_event, 0 /* from intr */);
			mutex_exit(ISP_RESP_MUTEX(isp));
		}

#ifdef ISPDEBUG
		laststate[12]++;
#endif
	}

	/* do not have the request/response queue mutexes now */

	/*
	 * copy registers saved by isp_i_mbox_cmd_complete() to cmdp
	 */
	for (i = 0; i < ISP_MAX_MBOX_REGS; i++) {
		/*
		 * save all mailbox in registers in our state stucture
		 * (XXX: could this be done with a bcopy to be faster?)
		 */
		ISP_MBOX_RETURN_REG(cmdp, i) =
		    ISP_MBOX_RETURN_REG(&(isp->isp_mbox.mbox_cmd), i);
	}

#ifdef ISP_PERF
	isp->isp_rpio_count += isp->isp_mbox.mbox_cmd.n_mbox_in;
#endif

	/* did the mbox cmd succeed ?? */
	if (ISP_MBOX_RETURN_STATUS(cmdp) == ISP_MBOX_STATUS_OK) {
		res = 0;			/* mailbox cmd succeeded */
	} else {
		ISP_DEBUG2(isp, SCSI_DEBUG,
		    "new mbox command returned failure status (0x%x)",
		    ISP_MBOX_RETURN_STATUS(cmdp));
		/*
		 * check for a Power On Reset of bus -- while this has only
		 * appeared in practice on SBus systems it can theoretically
		 * happen any time the chip firmware is reset but doesn't
		 * tell us
		 */
		if ((ISP_MBOX_RETURN_STATUS(cmdp) ==
		    ISP_MBOX_STATUS_INVALID_CMD) &&
		    (ISP_MBOX_RETURN_REG(cmdp, 1) == ISP_MBOX_INVALID_REG1) &&
		    (ISP_MBOX_RETURN_REG(cmdp, 2) == ISP_MBOX_INVALID_REG2) &&
		    (ISP_MBOX_RETURN_REG(cmdp, 3) == ISP_MBOX_INVALID_REG3)) {
			/* reset the chip */
			isp_i_log(isp, CE_WARN, "chip reset detected");
			mutex_enter(ISP_RESP_MUTEX(isp));
			mutex_enter(ISP_REQ_MUTEX(isp));
			sema_v(ISP_MBOX_SEMA(isp));
			released_sema = TRUE;
			mutex_exit(ISP_REQ_MUTEX(isp));
			isp_i_fatal_error(isp, ISP_FORCE_RESET_BUS);
			mutex_exit(ISP_RESP_MUTEX(isp));
		}
	}

done:
	if ((((ispdebug > 1) || isp_debug_mbox) && (res != 0)) ||
	    (isp_debug_mbox > 2)) {
		isp_i_log(isp, CE_WARN, "new mbox cmd %s:",
		    ((res != 0) ? "failed" : "succeeded"));
		isp_i_log(isp, CE_WARN,
		    "\tcmd=0x%x; 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		    cmdp->mbox_out[0], cmdp->mbox_out[1],
		    cmdp->mbox_out[2], cmdp->mbox_out[3],
		    cmdp->mbox_out[4], cmdp->mbox_out[5]);
		isp_i_log(isp, CE_WARN,
		    "\tstatus=0x%x; 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		    cmdp->mbox_in[0], cmdp->mbox_in[1],
		    cmdp->mbox_in[2], cmdp->mbox_in[3],
		    cmdp->mbox_in[4], cmdp->mbox_in[5]);
	} else {
		ISP_DEBUG4(isp, SCSI_DEBUG, "mbox cmd succeeded:");
		ISP_DEBUG4(isp, SCSI_DEBUG,
		    "\tcmd=0x%x; 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		    cmdp->mbox_out[0], cmdp->mbox_out[1],
		    cmdp->mbox_out[2], cmdp->mbox_out[3],
		    cmdp->mbox_out[4], cmdp->mbox_out[5]);
		ISP_DEBUG4(isp, SCSI_DEBUG,
		    "\tstatus=0x%x; 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		    cmdp->mbox_in[0], cmdp->mbox_in[1],
		    cmdp->mbox_in[2], cmdp->mbox_in[3],
		    cmdp->mbox_in[4], cmdp->mbox_in[5]);
	}
	/* grab mutex and release sema (in that order) */
	ISP_MUTEX_ENTER(isp);
	if (!released_sema) {
		sema_v(ISP_MBOX_SEMA(isp));
	}

#ifdef ISPDEBUG
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(isp->isp_in_intr))
	if (res != 0) {
		isp_i_log(isp, CE_WARN, "mbox 0x%x failed, intr %d,  %d %d %d "
		    "%d %d %d %d %d %d %d %d %d",
		    cmdp->mbox_out[0], isp->isp_in_intr,
		    laststate[0], laststate[1], laststate[2], laststate[3],
		    laststate[4], laststate[5], laststate[6], laststate[7],
		    laststate[8], laststate[9], laststate[10], laststate[11]);
		isp_i_print_state(CE_WARN, isp);
	}
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(isp->isp_in_intr))
#endif
	ISP_DEBUG4(isp, SCSI_DEBUG,
	    "isp_i_mbox_cmd_start_end (op_code = 0x%x)", op_code);

just_go:

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));

	return (res);
}

/*
 * Function name : isp_i_watch()
 *
 * Return Values : none
 * Description	 :
 * Isp deadman timer routine.
 * A hung isp controller is detected by failure to complete
 * cmds within a timeout interval (including grace period for
 * isp error recovery).	 All target error recovery is handled
 * directly by the isp.
 *
 * If isp hung, restart by resetting the isp and flushing the
 * crash protection queue (isp_slots) via isp_i_qflush.
 *
 * we check only 1/8 of the slots at the time; this is really only a sanity
 * check on isp so we know when it dropped a packet. The isp performs
 * timeout checking and recovery on the target
 * If the isp dropped a packet then this is a fatal error
 *
 * if lbolt wraps around then those packets will never timeout; there
 * is small risk of a hang in this short time frame. It is cheaper though
 * to ignore this problem since this is an extremely unlikely event
 *
 * Context	 : Can be called by timeout thread.
 */
#ifdef ISPDEBUG
static int isp_test_abort;
static int isp_test_abort_all;
static int isp_test_reset;
static int isp_test_reset_all;
static int isp_test_fatal;
static int isp_debug_enter;
static int isp_debug_enter_count;
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp_test_abort))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp_test_abort_all))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp_test_reset))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp_test_reset_all))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp_test_fatal))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp_debug_enter))
_NOTE(MUTEX_PROTECTS_DATA(isp::isp_response_mutex, isp_debug_enter_count))
#endif


_NOTE(READ_ONLY_DATA(isp::isp_next isp_head))

static void
isp_i_watch()
{
	struct isp *isp = isp_head;

	rw_enter(&isp_global_rwlock, RW_READER);
	for (isp = isp_head; isp != NULL; isp = isp->isp_next) {
		isp_i_watch_isp(isp);
	}
	rw_exit(&isp_global_rwlock);

	mutex_enter(&isp_global_mutex);
	/*
	 * If timeout_initted has been cleared then somebody
	 * is trying to untimeout() this thread so no point in
	 * reissuing another timeout.
	 */
	if (timeout_initted) {
		ASSERT(isp_timeout_id);
		isp_timeout_id = timeout((void (*)(void *))isp_i_watch,
		    (caddr_t)isp, isp_tick);
	}
	mutex_exit(&isp_global_mutex);
}


/*
 * called from isp_i_watch(), which is called from watch thread
 */
static void
isp_i_watch_isp(struct isp *isp)
{
	uint16_t slot;
	int ret_code;


	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());


#ifdef ISPDEBUG
	if (isp_watch_disable)
		return;
#endif

#ifdef ISP_PERF
	isp->isp_perf_ticks += isp_scsi_watchdog_tick;

	if (isp->isp_request_count >= 20000) {
		isp_i_log(isp, CE_NOTE,
	"%d reqs/sec (ticks=%d, intr=%d, req=%d, rpio=%d, wpio=%d)",
		    isp->isp_request_count/isp->isp_perf_ticks,
		    isp->isp_perf_ticks,
		    isp->isp_intr_count, isp->isp_request_count,
		    isp->isp_rpio_count, isp->isp_wpio_count);

		isp->isp_request_count = isp->isp_perf_ticks = 0;
		isp->isp_intr_count = 0;
		isp->isp_rpio_count = isp->isp_wpio_count = 0;
	}
#endif /* ISP_PERF */

	if (!(isp->isp_alive) && !(ISP_INT_PENDING(isp))) {
		if ((ret_code = isp_i_alive(isp)) == ETIMEDOUT) {
			isp_i_log(isp, CE_WARN, "ISP: Firmware cmd timeout");
			mutex_enter(ISP_RESP_MUTEX(isp));
			isp_i_fatal_error(isp, ISP_FORCE_RESET_BUS);
			mutex_exit(ISP_RESP_MUTEX(isp));
		} else if (ret_code == ETIME) {
			isp_i_log(isp, CE_WARN, "ISP: Firmware cmd timeout");
			mutex_enter(ISP_RESP_MUTEX(isp));
			isp_i_fatal_error(isp,
			    ISP_FIRMWARE_ERROR|ISP_DOWNLOAD_FW_ON_ERR);
			mutex_exit(ISP_RESP_MUTEX(isp));
		} else if (ret_code == EBADF) {
			isp_i_log(isp, CE_WARN, "ISP: Bad queue counts");
			mutex_enter(ISP_RESP_MUTEX(isp));
			isp_i_fatal_error(isp, ISP_FORCE_RESET_BUS);
			mutex_exit(ISP_RESP_MUTEX(isp));
		}
	}

	isp->isp_alive = FALSE;

#ifdef ISPDEBUG
	mutex_enter(ISP_RESP_MUTEX(isp));
	for (slot = 0; slot < ISP_MAX_SLOTS; slot++) {
		struct isp_cmd *sp = isp->isp_slots[slot].isp_cmd;

		if (sp != NULL) {
			isp_i_test(isp, sp);
			break;
		}
	}
	isp_test_abort = 0;
	if (isp_debug_enter && isp_debug_enter_count) {
		debug_enter("isp_i_watch");
		isp_debug_enter = 0;
	}
	mutex_exit(ISP_RESP_MUTEX(isp));
#endif	/* ISPDEBUG */

	if (isp->isp_prop_update != 0) {
		int		i;
		int		isp_prop_update_save;
		ushort_t	isp_cap[NTARGETS_WIDE];
		ushort_t	isp_synch[NTARGETS_WIDE];

		/*
		 * grab the mutexes and save a copy of isp_prop_update, then
		 * release the mutexes -- this eliminates a possible race
		 * condition that can occur if we hold the mutexes: doing
		 * a prop update and cause the kernel to need to page out
		 * some memory, and if that memory is going through this isp
		 * instance a deadlock can occur
		 */
		ISP_MUTEX_ENTER(isp);
		isp_prop_update_save = isp->isp_prop_update;
		isp->isp_prop_update = 0;
		bcopy(isp->isp_cap, isp_cap,
		    NTARGETS_WIDE * sizeof (ushort_t));
		bcopy(isp->isp_synch, isp_synch,
		    NTARGETS_WIDE * sizeof (ushort_t));
		ISP_MUTEX_EXIT(isp);

		for (i = 0; i < NTARGETS_WIDE; i++) {
			if (isp_prop_update_save & (1 << i)) {
				isp_i_update_props(isp, i, isp_cap[i],
				    isp_synch[i]);
			}
		}
	}


	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
}


/*
 * Function name : isp_i_fatal_error()
 *
 * Return Values :  none
 *
 * Description	 :
 * Isp fatal error recovery:
 * Reset the isp and flush the active queues and attempt restart.
 * This should only happen in case of a firmware bug or hardware
 * death.  Flushing is from backup queue as ISP cannot be trusted.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 *
 * called owning response mutex and not owning request mutex
 */
static void
isp_i_fatal_error(struct isp *isp, int flags)
{
	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(!mutex_owned(ISP_REQ_MUTEX(isp)) || ddi_in_panic());


	isp_i_log(isp, CE_WARN, "Fatal error, resetting interface, flg %x",
	    flags);

	/*
	 * hold off starting new requests by grabbing the request
	 * mutex
	 */
	mutex_enter(ISP_REQ_MUTEX(isp));

	if (isp_debug_state) {
		isp_i_print_state(CE_NOTE, isp);
	}

#ifdef ISPDEBUG
	if (isp_enable_brk_fatal) {
		char buf[128];
		char path[128];
		(void) sprintf(buf,
		"isp_i_fatal_error: You can now look at %s",
		    ddi_pathname(isp->isp_dip, path));
		debug_enter(buf);
	}
#endif

	if (isp_i_reset_interface(isp, flags) != 0) {
		ISP_DEBUG2(isp, SCSI_DEBUG,
		    "fatal_error: reset_interface failed");
	}

	mutex_exit(ISP_REQ_MUTEX(isp));
	(void) scsi_hba_reset_notify_callback(ISP_RESP_MUTEX(isp),
	    &isp->isp_reset_notify_listf);
	mutex_enter(ISP_REQ_MUTEX(isp));
	mutex_exit(ISP_RESP_MUTEX(isp));

	isp_check_waitq_and_exit_req_mutex(isp);

	mutex_enter(ISP_RESP_MUTEX(isp));
	ISP_INC32_ERRSTATS(isp, isperr_fatal_errors);
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
}


/*
 * Function name : isp_i_qflush()
 *
 * Return Values : none
 * Description	 :
 *	Flush isp queues  over range specified
 *	from start_tgt to end_tgt.  Flushing goes from oldest to newest
 *	to preserve some cmd ordering.
 *	This is used for isp crash recovery as normally isp takes
 *	care of target or bus problems.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 *
 * Note: always called for all targets
 */
static void
isp_i_qflush(struct isp *isp, ushort_t start_tgt, ushort_t end_tgt)
{
	struct isp_cmd *sp;
	struct isp_cmd *head, *tail;
	uint16_t slot;
	uint16_t prev_slot;
	int n = 0;
	struct isp_response *resp;

	ASSERT(start_tgt <= end_tgt);
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(!mutex_owned(ISP_WAITQ_MUTEX(isp)));

	ISP_DEBUG3(isp, SCSI_DEBUG,
	    "isp_i_qflush: range= %d-%d", start_tgt, end_tgt);


	/*
	 * Flush specified range of active queue entries
	 * (e.g. target nexus).
	 * we allow for just flushing one target, ie start_tgt == end_tgt
	 */
	head = tail = NULL;

	/*
	 * Flush busy slots, in the order in which they would timeout.
	 */
	for (slot = isp->busy_slots.tail; slot != ISP_MAX_SLOTS;
	    slot = prev_slot) {
		prev_slot = isp->isp_slots[slot].prev;
		sp = isp->isp_slots[slot].isp_cmd;
		ASSERT(sp != NULL);

		if ((TGT(sp) >= start_tgt) &&
		    (TGT(sp) <= end_tgt)) {

			/* free the slot */
			ISP_FREE_SLOT(isp, slot);

			/* get ptr to response part of packet */
			resp = &sp->cmd_isp_response;

			/* clear out response */
			bzero((caddr_t)resp,
			    sizeof (struct isp_response));

			/*
			 * update stats in resp_status_flags
			 * isp_i_call_pkt_comp() copies this
			 * over to pkt_statistics
			 */
			resp->resp_status_flags = STAT_BUS_RESET | STAT_ABORTED;
			resp->resp_reason = CMD_RESET;
#ifdef ISPDEBUG
			sp->cmd_flags |= CFLAG_FINISHED;
#endif
			/*
			 * queue up sp
			 * we don't want to do a callback yet
			 * until we have flushed everything and
			 * can release the mutexes
			 */
			n++;
			if (head != NULL) {
				tail->cmd_forw = sp;
				tail = sp;
				tail->cmd_forw = NULL;
			} else {
				tail = head = sp;
				sp->cmd_forw = NULL;
			}
		}
	}

	/*
	 * XXX we don't worry about the waitQ since we cannot
	 * guarantee order anyway.
	 */
	if (head != NULL) {
		/*
		 * if we would	hold the REQ mutex and	the target driver
		 * decides to do a scsi_reset(), we will get a recursive
		 * mutex failure in isp_i_set_marker
		 */
		ISP_DEBUG4(isp, SCSI_DEBUG, "isp_i_qflush: %d flushed", n);
		ISP_MUTEX_EXIT(isp);
		isp_i_call_pkt_comp(head);
		ISP_MUTEX_ENTER(isp);
	}

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
}


/*
 * Function name : isp_i_set_marker()
 *
 * Return Values : none
 * Description	 :
 * Send marker request to unlock the request queue for a given target/lun
 * nexus.
 *
 * If no marker can be sent (which means no request queue space is available)
 * then keep track of the fact for later and
 * return failure -- isp_i_update_queue_space() will send the marker when
 * it finds more space
 *
 * XXX: Right now this routine expects to be called without the request
 * mutext being held, but then it acquires it.  But in every place its
 * called, the request mutex is alredy held (usually the response mutex
 * is as well).  So this routine should change to expect the request
 * mutex to already be held, and all code calling it should change
 * correspondingly -- this would remove holes where we release the
 * request mutex and call this routine, but then somebody else adds to
 * the request queue before we acquire the mutex again here
 *
 * but part of the problem is that, right now, the last thing this routine
 * does is to move any entries on the waitQ to the request Q and then
 * release the request Q mutex (in a macro), so that would have to change
 * as well, and may have far-reaching consequences
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 *
 * called with neither mutex held
 */
/*ARGSUSED*/
static int
isp_i_set_marker(struct isp *isp, short mod, short tgt, short lun)
{

	ASSERT(isp != NULL);
	ASSERT(!mutex_owned(ISP_REQ_MUTEX(isp)) || ddi_in_panic());
	ASSERT(!mutex_owned(ISP_RESP_MUTEX(isp)) || ddi_in_panic());


	mutex_enter(ISP_REQ_MUTEX(isp));

	/*
	 * Check to see how much space is available in the
	 * Request Queue, save this so we do not have to do
	 * a lot of PIOs
	 */
	if (isp->isp_queue_space == 0) {
		isp_i_update_queue_space(isp);

		/*
		 * Check now to see if the queue is still full
		 */
		if (isp->isp_queue_space == 0) {
			/*
			 * oh oh -- still no space for the marker -- keep
			 * track of that for later
			 */
			isp_i_add_marker_to_list(isp, mod, tgt, lun);

			/*
			 * release request mutex and return failure
			 */
			mutex_exit(ISP_REQ_MUTEX(isp));
			return (-1);
		}
	}

	/*
	 * if not yet attached add to marker queue instead
	 */
	if (!isp->isp_attached) {
		ISP_DEBUG4(isp, SCSI_DEBUG, "saved marker until after attach");
		isp_i_add_marker_to_list(isp, mod, tgt, lun);
		mutex_exit(ISP_REQ_MUTEX(isp));
		return (-1);
	}

	/*
	 * call helper routine to do work of sending marker
	 * (must hold reqQ mutex)
	 */
	isp_i_send_marker(isp, mod, tgt, lun);

	/* move any packets that've been point on the wait Q onto the req Q */
	isp_check_waitq_and_exit_req_mutex(isp);

	ASSERT(!mutex_owned(ISP_REQ_MUTEX(isp)) || ddi_in_panic());
	ASSERT(!mutex_owned(ISP_RESP_MUTEX(isp)) || ddi_in_panic());

	return (0);
}


/*
 * Function name : isp_scsi_abort()
 *
 * Return Values : FALSE	- abort failed
 *		   TRUE		- abort succeeded
 * Description	 :
 * SCSA interface routine to abort pkt(s) in progress.
 * Abort the pkt specified.  If NULL pkt, abort ALL pkts.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct isp		*isp;
	struct isp_mbox_cmd	mbox_cmd;
	ushort_t		rval = FALSE;


	ASSERT(ap != NULL);

	isp = ADDR2ISP(ap);

	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());


	/*
	 * hold off new requests, we need the req mutex anyway for mbox cmds.
	 * the waitQ must be empty since the request mutex was free
	 */
	ISP_MUTEX_ENTER(isp);

	/*
	 * If no space in request queue, return error
	 */
	if (isp->isp_queue_space == 0) {
		isp_i_update_queue_space(isp);

		/*
		 * Check now to see if the queue is still full
		 */
		if (isp->isp_queue_space == 0) {
			ISP_DEBUG2(isp, SCSI_DEBUG,
			    "isp_scsi_abort: No space in Queue for Marker");
			goto fail;
		}
	}

	if (pkt) {
		struct isp_cmd *sp = PKT2CMD(pkt);

		ISP_DEBUG4(isp, SCSI_DEBUG, "aborting pkt 0x%p", (void*)pkt);

		if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_ABORT_IOCB, TGT_N_LUN((ushort_t)ap->a_target,
		    (ushort_t)ap->a_lun), MSW(SP2TOKEN(sp)),
		    LSW(SP2TOKEN(sp))) != 0) {
			goto fail;
		}

		ISP_MUTEX_EXIT(isp);
	} else {
		ISP_DEBUG4(isp, SCSI_DEBUG, "aborting all pkts");

		if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_ABORT_DEVICE,
		    TGT_N_LUN((ushort_t)ap->a_target, (ushort_t)ap->a_lun)) !=
		    0) {
			goto fail;
		}

		ISP_MUTEX_EXIT(isp);
		if (isp_i_set_marker(isp, SYNCHRONIZE_NEXUS,
		    (short)ap->a_target, (short)ap->a_lun)) {
			ISP_DEBUG2(isp, SCSI_DEBUG,
			    "can't set marker, target %d LUN %d: queueing",
			    ap->a_target, ap->a_lun);
			ISP_MUTEX_ENTER(isp);
			goto fail;
		}
	}

	rval = TRUE;

	mutex_enter(ISP_REQ_MUTEX(isp));
	isp_check_waitq_and_exit_req_mutex(isp);

	return (rval);

fail:

	mutex_exit(ISP_RESP_MUTEX(isp));
	isp_check_waitq_and_exit_req_mutex(isp);

	return (rval);
}


/*
 * Function name : isp_scsi_reset()
 *
 * Return Values : FALSE - reset failed
 *		   TRUE	 - reset succeeded
 * Description	 :
 * SCSA interface routine to perform scsi resets on either
 * a specified target or the bus (default).
 * XXX check waitQ as well
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_scsi_reset(struct scsi_address *ap, int level)
{
	struct isp		*isp;
	struct isp_mbox_cmd	mbox_cmd;
	int			rval = FALSE;


	ASSERT(ap != NULL);

	isp = ADDR2ISP(ap);

	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());


	/*
	 * hold off new requests, we need the req mutex anyway for mbox cmds.
	 * the waitQ must be empty since the request mutex was free
	 */
	ISP_MUTEX_ENTER(isp);

	/*
	 * If no space in request queue, return error
	 */
	if (isp->isp_queue_space == 0) {
		isp_i_update_queue_space(isp);

		/*
		 * Check now to see if the queue is still full
		 */
		if (isp->isp_queue_space == 0) {
			ISP_DEBUG2(isp, SCSI_DEBUG,
			    "isp_scsi_abort: No space in Queue for Marker");
			goto fail;
		}
	}

	if (level == RESET_TARGET) {
		ISP_DEBUG4(isp, SCSI_DEBUG, "isp_scsi_reset: reset target %d",
		    ap->a_target);
		if (isp_debug_reset_sent) {
			isp_i_log(isp, CE_NOTE, "isp_scsi_reset: "
			    "Bus Device Reset- target %d lun %d", ap->a_target,
			    ap->a_lun);
		}

		if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_ABORT_TARGET,
		    TGT_N_LUN((ushort_t)ap->a_target, (ushort_t)ap->a_lun),
		    (ushort_t)(isp->isp_scsi_reset_delay/1000)) != 0) {
			goto fail;
		}

		ISP_MUTEX_EXIT(isp);
		if (isp_i_set_marker(isp, SYNCHRONIZE_TARGET,
		    (short)ap->a_target, (short)ap->a_lun)) {
			ISP_DEBUG2(isp, SCSI_DEBUG, "can't set marker, "
			    "target %d (SCSI reset): queueing", ap->a_target);
			ISP_MUTEX_ENTER(isp);
			goto fail;
		}
		ISP_MUTEX_ENTER(isp);
	} else {

		ISP_DEBUG4(isp, SCSI_DEBUG, "isp_scsi_reset: Reset Bus");
		if (isp_debug_reset_sent) {
			isp_i_log(isp, CE_NOTE, "isp_scsi_reset: Reset Bus");
		}

		if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_BUS_RESET,
		    (ushort_t)isp->isp_scsi_reset_delay) != 0) {
			goto fail;
		}

		mutex_exit(ISP_REQ_MUTEX(isp));
		(void) scsi_hba_reset_notify_callback(ISP_RESP_MUTEX(isp),
		    &isp->isp_reset_notify_listf);

		mutex_exit(ISP_RESP_MUTEX(isp));

		if (isp_i_set_marker(isp, SYNCHRONIZE_ALL, 0, 0)) {
			ISP_DEBUG2(isp, SCSI_DEBUG, "can't set marker, "
			    "all targets (SCSI reset): queueing");
			ISP_MUTEX_ENTER(isp);
			goto fail;
		}
		ISP_MUTEX_ENTER(isp);
	}

	rval = TRUE;

fail:
	mutex_exit(ISP_RESP_MUTEX(isp));
	isp_check_waitq_and_exit_req_mutex(isp);

	return (rval);
}


/*
 * Function name : isp_i_reset_interface()
 *
 * Return Values : 0 - success
 *		  -1 - hw failure
 *
 * Description	 :
 * Master reset routine for hardware/software.	Resets softc struct,
 * isp chip, and scsi bus.  The works!
 * This function is called from isp_attach or
 * from isp_i_fatal_error with response and request mutex held
 *
 * NOTE: it is up to the caller to flush the response queue and isp_slots
 *	 list
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 *
 * Called with request and response queue mutexes held
 *
 * actions that can be passed in:
 *	ISP_FIRMWARE_ERROR	- a firmware error occured, so you can no
 *				  longer trust the firmware
 *	ISP_FORCE_RESET_BUS	- reset the bus before doing anything else
 *	ISP_SKIP_STOP_QUEUES	- do not send a "stop queue" for each device
 *				  queue
 *	ISP_FROM_INTR		- being called from interrupt context
 *	ISP_DOWNLOAD_FW_ON_ERR	- reload the firmware
 */
static int
isp_i_reset_interface(struct isp *isp, int action)
{
	int i, j;
	struct isp_mbox_cmd mbox_cmd;
	int rval = -1;

	ASSERT(isp != NULL);


	mutex_enter(ISP_WAITQ_MUTEX(isp));
	if (isp->isp_in_reset) {
		/*
		 * we are already resetting, so just return a fake
		 * "success" value
		 *
		 * NOTE: perhaps we should wait until the other
		 * reset is done before returnning success, since the
		 * the chip hasn't been reset until that happens, but in
		 * practice it hasn't been a problem, perhaps because
		 * dual resets don't happen that often
		 */
		mutex_exit(ISP_WAITQ_MUTEX(isp));
		ISP_DEBUG4(isp, SCSI_DEBUG,
		    "already reseting -- not again!?");
		return (0);	/* fake successful return */
	}
	isp->isp_in_reset++;
	mutex_exit(ISP_WAITQ_MUTEX(isp));

	/*
	 * If a firmware error is seen do not trust the firmware to correctly
	 * handle mailbox commands
	 */
	if (!(action & ISP_FIRMWARE_ERROR)) {
		/*
		 * Reset the SCSI bus to blow away all the commands
		 * under process
		 */
		if (action & ISP_FORCE_RESET_BUS) {
			ISP_DEBUG3(isp, SCSI_DEBUG,
			    "isp_scsi_reset: reset bus");
			if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
			    ISP_MBOX_CMD_BUS_RESET,
			    (ushort_t)isp->isp_scsi_reset_delay) != 0) {
				goto pause_risc;
			}

			mutex_exit(ISP_REQ_MUTEX(isp));
			(void) scsi_hba_reset_notify_callback(
			    ISP_RESP_MUTEX(isp), &isp->isp_reset_notify_listf);
			mutex_enter(ISP_REQ_MUTEX(isp));

			drv_usecwait((clock_t)isp->isp_scsi_reset_delay*1000);

		} else if (!(action & ISP_SKIP_STOP_QUEUES)) {
			/*
			 * if we are not going to reset the bus stop
			 * all the queues
			 */
			for (i = 0; i < NTARGETS_WIDE; i++) {
				for (j = 0; j < isp->isp_max_lun[i]; j++) {
					/*
					 * Stop the queue for individual
					 * target/lun combination
					 */
					if (isp_i_mbox_cmd_start(isp,
					    &mbox_cmd, ISP_MBOX_CMD_STOP_QUEUE,
					    TGT_N_LUN(i, j)) != 0) {
						goto pause_risc;
					}
				}
			}
		}

	}

pause_risc:
	/* Put the Risc in pause mode */
	ISP_WRITE_RISC_HCCR(isp, ISP_HCCR_CMD_PAUSE);

	/*
	 * we do not want to wait for an interrupt to finish if we
	 * were called from that context
	 */
	if (!(action & ISP_FROM_INTR)) {
		/* wait to ensure no final interrupt(s) being processed */
		ISP_MUTEX_EXIT(isp);
		mutex_enter(ISP_INTR_MUTEX(isp));
		while (isp->isp_in_intr != 0) {
			if (cv_wait_sig(ISP_INTR_CV(isp),
			    ISP_INTR_MUTEX(isp)) == 0) {
				/* interrupted */
				break;
			}
		}
		mutex_exit(ISP_INTR_MUTEX(isp));
		ISP_MUTEX_ENTER(isp);
	}

	/* flush pkts in queue back to target driver */
	isp_i_qflush(isp, 0, (ushort_t)NTARGETS_WIDE - 1);

	/*
	 * put register set in sxp mode
	 */
	if (isp->isp_bus == ISP_PCI) {
		ISP_SET_BIU_REG_BITS(isp, &isp->isp_biu_reg->isp_bus_conf1,
		    ISP_PCI_CONF1_SXP);
	}

	/*
	 * reset and initialize isp chip
	 *
	 * resetting the chip will put it in default risc mode
	 */
	if (isp_i_reset_init_chip(isp)) {
		goto fail;
	}

	if (action & ISP_DOWNLOAD_FW_ON_ERR) {
		/*
		 * If user wants firmware to be downloaded
		 */
		if (isp->isp_bus == ISP_SBUS) {
			rval = isp_i_download_fw(isp, isp_risc_code_addr,
			    isp_sbus_risc_code, isp_sbus_risc_code_length);
		} else {
			rval = isp_i_download_fw(isp, isp_risc_code_addr,
			    isp_1040_risc_code, isp_1040_risc_code_length);
		}
	}
	if (rval == -1) {
		/*
		 * Either ISP_DOWNLOAD_FW_ON_ERR was not set or
		 * isp_i_download_fw() failed.
		 *
		 * Start ISP firmware up.
		 */
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_START_FW,
		    isp_risc_code_addr) != 0) {
			goto fail;
		}

		/*
		 * set clock rate
		 */
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_SET_CLOCK_RATE, isp->isp_clock_frequency) !=
		    0) {
			goto fail;
		}
	} else {
		rval = -1;
	}

	/*
	 * set Initiator SCSI ID
	 */
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
	    ISP_MBOX_CMD_SET_SCSI_ID, isp->isp_initiator_id) != 0) {
		goto fail;
	}

	ISP_DEBUG4(isp, SCSI_DEBUG, "Resetting queues");

	/*
	 * Initialize request and response queue indexes.
	 */
	ISP_DEBUG4(isp, SCSI_DEBUG, "setting req(i/o) to 0/0 (was %d/%d)",
	    isp->isp_request_in, isp->isp_request_out);
	isp->isp_request_in = isp->isp_request_out = 0;
	isp->isp_request_ptr = isp->isp_request_base;
	isp->isp_response_in = isp->isp_response_out = 0;
	isp->isp_response_ptr = isp->isp_response_base;

	/*
	 * extra init that should be done
	 */
	isp->isp_queue_space = isp_max_requests - 1;
	isp->isp_marker_in = isp->isp_marker_out = 0;
	isp->isp_marker_free = ISP_MI_SIZE - 1;

	if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
	    ISP_MBOX_CMD_INIT_REQUEST_QUEUE, isp_max_requests,
	    MSW(isp->isp_request_dvma), LSW(isp->isp_request_dvma),
	    isp->isp_request_in, isp->isp_response_out) != 0) {
		goto fail;
	}
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
	    ISP_MBOX_CMD_INIT_RESPONSE_QUEUE, isp_max_responses,
	    MSW(isp->isp_response_dvma), LSW(isp->isp_response_dvma),
	    isp->isp_request_in, isp->isp_response_out) != 0) {
		goto fail;
	}

	isp->isp_mbox.mbox_flags &= ~ISP_MBOX_CMD_FLAGS_Q_NOT_INIT;

	/*
	 * Handle isp capabilities adjustments.
	 */
	ISP_DEBUG4(isp, SCSI_DEBUG, "Initializing isp capabilities");

	/*
	 * Check and adjust "host id" as required.
	 */
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_GET_SCSI_ID) !=
	    0) {
		goto fail;
	}
	if (ISP_MBOX_RETURN_REG(&mbox_cmd, 1) != isp->isp_initiator_id) {
		ISP_DEBUG2(isp, SCSI_DEBUG, "SCSI id will be (was) = %d (%d)",
		    isp->isp_initiator_id, ISP_MBOX_RETURN_REG(&mbox_cmd, 1));
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_SET_SCSI_ID, isp->isp_initiator_id) != 0) {
			goto fail;
		}
	}

	/*
	 * Check and adjust "retries" as required.
	 */
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
	    ISP_MBOX_CMD_GET_RETRY_ATTEMPTS) != 0) {
		goto fail;
	}
	if ((ISP_MBOX_RETURN_REG(&mbox_cmd, 1) != ISP_RETRIES) ||
	    (ISP_MBOX_RETURN_REG(&mbox_cmd, 2) != ISP_RETRY_DELAY)) {
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_SET_RETRY_ATTEMPTS, ISP_RETRIES,
		    ISP_RETRY_DELAY) != 0) {
			goto fail;
		}
		ISP_DEBUG4(isp, SCSI_DEBUG, "retries=%d(%d), delay=%d(%d)",
		    ISP_RETRIES, ISP_MBOX_RETURN_REG(&mbox_cmd, 1),
		    ISP_RETRY_DELAY, ISP_MBOX_RETURN_REG(&mbox_cmd, 2));
	}

	if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_SET_SEL_TIMEOUT,
	    isp_selection_timeout) != 0) {
		goto fail;
	}
	/*
	 * Set and adjust the Data Over run Recovery method. Set to Mode 2
	 */
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
	    ISP_MBOX_CMD_SET_DATA_OVR_RECOV_MODE, 2) != 0) {
		goto fail;
	}

	/*
	 * Check and adjust "tag age limit" as required.
	 */
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_GET_AGE_LIMIT) !=
	    0) {
		goto fail;
	}
	if (ISP_MBOX_RETURN_REG(&mbox_cmd, 1) != isp->isp_scsi_tag_age_limit) {
		ISP_DEBUG4(isp, SCSI_DEBUG, "tag age = %d(%d)",
		    isp->isp_scsi_tag_age_limit,
		    ISP_MBOX_RETURN_REG(&mbox_cmd, 1));
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_SET_AGE_LIMIT,
		    (ushort_t)isp->isp_scsi_tag_age_limit) != 0) {
			goto fail;
		}
	}

#ifdef	ISPDEBUG
	/*
	 * if in debug mode print info about queues and I/Os
	 */
	for (i = 0; i < NTARGETS_WIDE; i++) {
		(void) isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_GET_DEVICE_QUEUE_PARAMS,
		    TGT_N_LUN(i, 0));
		ISP_DEBUG4(isp, SCSI_DEBUG,
		    "Max Queue Depth = 0x%x, Exec Trottle = 0x%x",
		    ISP_MBOX_RETURN_REG(&mbox_cmd, 2),
		    ISP_MBOX_RETURN_REG(&mbox_cmd, 3));
	}

	(void) isp_i_mbox_cmd_start(isp, &mbox_cmd,
	    ISP_MBOX_CMD_GET_FIRMWARE_STATUS);
	ISP_DEBUG4(isp, SCSI_DEBUG, "Max # of IOs = 0x%x",
	    ISP_MBOX_RETURN_REG(&mbox_cmd, 2));
#endif	/* ISPDEBUG */

	/*
	 * Set the delay after BDR during a timeout.
	 */
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_SET_DELAY_BDR,
	    (ushort_t)(isp->isp_scsi_reset_delay/1000)) != 0) {
		isp_i_log(isp, CE_NOTE, "Failed to set BDR delay");
	} else {
		ISP_DEBUG4(isp, SCSI_DEBUG, "Set BDR delay to %d",
		    mbox_cmd.mbox_out[1]);
	}

	/*
	 * Update caps from isp.
	 */
	ISP_DEBUG4(isp, SCSI_DEBUG, "Getting isp capabilities");
	if (isp_i_updatecap(isp, 0, NTARGETS_WIDE, FALSE) != 0) {
		isp_i_log(isp, CE_WARN,
		    "updatecap failed during reset interface: "
		    "continuing anyway");
	}

	/* As the firmware is started afresh - reset backoff flag */
	isp->isp_backoff = 0;

	rval = 0;

fail:
	if (rval != 0) {
		ISP_DEBUG2(isp, SCSI_DEBUG, "reset interface failed");
		isp_i_log(isp, CE_WARN, "interface going offline");
		/*
		 * put register set in risc mode in case the
		 * reset didn't complete
		 */
		if (isp->isp_bus == ISP_PCI) {
			ISP_CLR_BIU_REG_BITS(isp,
			    &isp->isp_biu_reg->isp_bus_conf1,
			    ISP_PCI_CONF1_SXP);
		}
		ISP_CLEAR_RISC_INT(isp);
		ISP_WRITE_BIU_REG(isp, &isp->isp_biu_reg->isp_bus_icr,
		    ISP_BUS_ICR_DISABLE_ALL_INTS);
		ISP_WRITE_RISC_HCCR(isp, ISP_HCCR_CMD_PAUSE);
		ISP_WRITE_RISC_REG(isp, &isp->isp_risc_reg->isp_risc_psr,
		    ISP_RISC_PSR_FORCE_TRUE | ISP_RISC_PSR_LOOP_COUNT_DONE);
		ISP_WRITE_RISC_REG(isp, &isp->isp_risc_reg->isp_risc_pcr,
		    ISP_RISC_PCR_RESTORE_PCR);
		isp_i_qflush(isp, (ushort_t)0, (ushort_t)NTARGETS_WIDE - 1);
	}

	/* keep track of fact we are no longer resetting */
	mutex_enter(ISP_WAITQ_MUTEX(isp));
	isp->isp_in_reset--;
	mutex_exit(ISP_WAITQ_MUTEX(isp));


	ISP_INC32_ERRSTATS(isp, isperr_reset_chip);
	return (rval);
}


/*
 * Function name : isp_i_reset_init_chip()
 *
 * Return Values : 0 - success
 *		  -1 - hw failure
 *
 * Description	 :
 * Reset the ISP chip and perform BIU initializations. Also enable interrupts.
 * It is assumed that EXTBOOT will be strobed low after reset.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called at attach time.
 */
static int
isp_i_reset_init_chip(struct isp *isp)
{
	int delay_loops;
	int rval = -1;
	unsigned short isp_conf_comm;

	ISP_DEBUG3(isp, SCSI_DEBUG, "isp_i_reset_init_chip");


	if (isp->isp_bus == ISP_PCI) {
		/*
		 * we want to respect framework's setting of PCI
		 * configuration space command register and also
		 * want to make sure that all bits interest to us
		 * are properly set in command register.
		 */
		isp_conf_comm = pci_config_get16(
		    isp->isp_pci_config_acc_handle,
		    PCI_CONF_COMM);
		ISP_DEBUG4(isp, SCSI_DEBUG,
		    "PCI conf command register was 0x%x", isp_conf_comm);
		isp_conf_comm |= PCI_COMM_IO | PCI_COMM_MAE | PCI_COMM_ME |
		    PCI_COMM_MEMWR_INVAL | PCI_COMM_PARITY_DETECT |
		    PCI_COMM_SERR_ENABLE;
		ISP_DEBUG4(isp, SCSI_DEBUG,
		    "PCI conf command register is 0x%x",
		    isp_conf_comm);
		pci_config_put16(isp->isp_pci_config_acc_handle,
		    PCI_CONF_COMM, isp_conf_comm);

		/*
		 * set cache line & latency register in pci configuration
		 * space. line register is set in units of 32-bit words.
		 */
		pci_config_put8(isp->isp_pci_config_acc_handle,
		    PCI_CONF_CACHE_LINESZ, (uchar_t)isp_conf_cache_linesz);
		pci_config_put8(isp->isp_pci_config_acc_handle,
		    PCI_CONF_LATENCY_TIMER,
		    (uchar_t)isp_conf_latency_timer);
	}

	/*
	 * reset the isp
	 */
	ISP_WRITE_BIU_REG(isp, &isp->isp_biu_reg->isp_bus_icr,
	    ISP_BUS_ICR_SOFT_RESET);
	/*
	 * we need to wait a bit before touching the chip again,
	 * otherwise problems show up running ISP1040A on
	 * fast sun4u machines.
	 */
	drv_usecwait(ISP_CHIP_RESET_BUSY_WAIT_TIME);
	ISP_WRITE_BIU_REG(isp, &isp->isp_biu_reg->isp_cdma_control,
	    ISP_DMA_CON_RESET_INT | ISP_DMA_CON_CLEAR_CHAN);
	ISP_WRITE_BIU_REG(isp, &isp->isp_biu_reg->isp_dma_control,
	    ISP_DMA_CON_RESET_INT | ISP_DMA_CON_CLEAR_CHAN);

	/*
	 * wait for isp to fire up.
	 */
	delay_loops = ISP_TIMEOUT_DELAY(ISP_SOFT_RESET_TIME,
	    ISP_CHIP_RESET_BUSY_WAIT_TIME);
	while (ISP_READ_BIU_REG(isp, &isp->isp_biu_reg->isp_bus_icr) &
	    ISP_BUS_ICR_SOFT_RESET) {
		drv_usecwait(ISP_CHIP_RESET_BUSY_WAIT_TIME);
		if (--delay_loops < 0) {
			isp_i_log(isp, CE_WARN, "Chip reset timeout");
			goto fail;
		}
	}

	ISP_WRITE_BIU_REG(isp, &isp->isp_biu_reg->isp_bus_conf1, 0);

	/*
	 * reset the risc processor
	 */

	ISP_WRITE_RISC_HCCR(isp, ISP_HCCR_CMD_RESET);
	drv_usecwait(ISP_CHIP_RESET_BUSY_WAIT_TIME);

	/*
	 * initialization biu
	 */
	ISP_SET_BIU_REG_BITS(isp, &isp->isp_biu_reg->isp_bus_conf1,
	    isp->isp_conf1_fifo);
	if (isp->isp_conf1_fifo & ISP_BUS_CONF1_BURST_ENABLE) {
		ISP_SET_BIU_REG_BITS(isp, &isp->isp_biu_reg->isp_cdma_conf,
		    ISP_DMA_CONF_ENABLE_BURST);
		ISP_SET_BIU_REG_BITS(isp, &isp->isp_biu_reg->isp_dma_conf,
		    ISP_DMA_CONF_ENABLE_BURST);
	}
	ISP_WRITE_RISC_REG(isp, &isp->isp_risc_reg->isp_risc_mtr,
	    ISP_RISC_MTR_PAGE0_DEFAULT | ISP_RISC_MTR_PAGE1_DEFAULT);
	ISP_WRITE_RISC_HCCR(isp, ISP_HCCR_CMD_RELEASE);
	isp->isp_mbox.mbox_flags |= ISP_MBOX_CMD_FLAGS_Q_NOT_INIT;

	if (isp->isp_bus == ISP_PCI) {
		/*
		 * make sure that BIOS is disabled
		 */
		ISP_WRITE_RISC_HCCR(isp, ISP_PCI_HCCR_CMD_BIOS);
	}

	/*
	 * enable interrupts
	 */
	ISP_WRITE_BIU_REG(isp, &isp->isp_biu_reg->isp_bus_icr,
	    ISP_BUS_ICR_ENABLE_RISC_INT | ISP_BUS_ICR_ENABLE_ALL_INTS);


	if (isp_debug_state) {
		isp_i_print_state(CE_WARN, isp);
	}

	rval = 0;

fail:
	return (rval);
}


/*
 * Error logging, printing, and debug print routines
 */

/*PRINTFLIKE3*/
static void
isp_i_log(struct isp *isp, int level, char *fmt, ...)
{
	dev_info_t *dip;
	va_list ap;


	ASSERT(mutex_owned((&isp_log_mutex)) == 0 || ddi_in_panic());

	if (isp != NULL) {
		dip = isp->isp_dip;
	} else {
		dip = 0;
	}

	mutex_enter(&isp_log_mutex);
	va_start(ap, fmt);
	(void) vsprintf(isp_log_buf, fmt, ap);
	va_end(ap);

	if (level == CE_WARN) {
		scsi_log(dip, "isp", level, "%s", isp_log_buf);
	} else {
		scsi_log(dip, "isp", level, "%s\n", isp_log_buf);
	}
	mutex_exit(&isp_log_mutex);
}


static void
isp_i_print_state(int level, struct isp *isp)
{
	char buf[128];
	int i;
	char risc_paused = 0;
	struct isp_biu_regs *isp_biu = isp->isp_biu_reg;
	struct isp_risc_regs *isp_risc = isp->isp_risc_reg;


	ASSERT(isp != NULL);

	/* Put isp header in buffer for later messages. */
	isp_i_log(isp, level, "State dump from isp registers and driver:");

	/*
	 * Print isp registers.
	 */
	(void) sprintf(buf,
	    "mailboxes(0-5): 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox0),
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox1),
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox2),
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox3),
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox4),
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox5));
	isp_i_log(isp, CE_CONT, buf);

	if (ISP_READ_RISC_HCCR(isp) ||
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_bus_sema)) {
		(void) sprintf(buf,
		    "hccr= 0x%x, bus_sema= 0x%x", ISP_READ_RISC_HCCR(isp),
		    ISP_READ_BIU_REG(isp, &isp_biu->isp_bus_sema));
		isp_i_log(isp, CE_CONT, buf);
	}
	if ((ISP_READ_RISC_HCCR(isp) & ISP_HCCR_PAUSE) == 0) {
		ISP_WRITE_RISC_HCCR(isp, ISP_HCCR_CMD_PAUSE);
		risc_paused = 1;
	}

	(void) sprintf(buf,
	    "bus: isr= 0x%x, icr= 0x%x, conf0= 0x%x, conf1= 0x%x",
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_bus_isr),
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_bus_icr),
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_bus_conf0),
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_bus_conf1));
	isp_i_log(isp, CE_CONT, buf);

	(void) sprintf(buf,
	    "cdma: count= %d, addr= 0x%x, status= 0x%x, conf= 0x%x",
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_cdma_count),
	    (ISP_READ_BIU_REG(isp, &isp_biu->isp_cdma_addr1) << 16) |
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_cdma_addr0),
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_cdma_status),
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_cdma_conf));
	if ((i = ISP_READ_BIU_REG(isp, &isp_biu->isp_cdma_control)) != 0) {
		(void) sprintf(&buf[strlen(buf)], ", control= 0x%x",
		    (ushort_t)i);
	}
	if ((i = ISP_READ_BIU_REG(isp, &isp_biu->isp_cdma_fifo_status)) != 0) {
		(void) sprintf(&buf[strlen(buf)], ", fifo_status= 0x%x",
		    (ushort_t)i);
	}
	isp_i_log(isp, CE_CONT, buf);

	(void) sprintf(buf,
	    "dma: count= %d, addr= 0x%x, status= 0x%x, conf= 0x%x",
	    (ISP_READ_BIU_REG(isp, &isp_biu->isp_dma_count_hi) << 16) |
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_dma_count_lo),
	    (ISP_READ_BIU_REG(isp, &isp_biu->isp_dma_addr1) << 16) |
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_dma_addr0),
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_dma_status),
	    ISP_READ_BIU_REG(isp, &isp_biu->isp_dma_conf));
	if ((i = ISP_READ_BIU_REG(isp, &isp_biu->isp_dma_control)) != 0) {
		(void) sprintf(&buf[strlen(buf)], ", control= 0x%x",
		    (ushort_t)i);
	}
	if ((i = ISP_READ_BIU_REG(isp, &isp_biu->isp_dma_fifo_status)) != 0) {
		(void) sprintf(&buf[strlen(buf)], ", fifo_status= 0x%x",
		    (ushort_t)i);
	}
	isp_i_log(isp, CE_CONT, buf);

	/*
	 * If the risc isn't already paused, pause it now.
	 */
	if (risc_paused == 0) {
		ISP_WRITE_RISC_HCCR(isp, ISP_HCCR_CMD_PAUSE);
		risc_paused = 1;
	}

	(void) sprintf(buf,
	    "risc: R0-R7= 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x 0x%x",
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_acc),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r1),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r2),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r3),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r4),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r5),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r6),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r7));
	isp_i_log(isp, CE_CONT, buf);

	(void) sprintf(buf,
	    "risc: R8-R15= 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x 0x%x",
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r8),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r9),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r10),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r11),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r12),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r13),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r14),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_r15));
	isp_i_log(isp, CE_CONT, buf);

	(void) sprintf(buf,
	    "risc: PSR= 0x%x, IVR= 0x%x, PCR=0x%x, RAR0=0x%x, RAR1=0x%x",
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_psr),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_ivr),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_pcr),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_rar0),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_rar1));
	isp_i_log(isp, CE_CONT, buf);

	(void) sprintf(buf,
	    "risc: LCR= 0x%x, PC= 0x%x, MTR=0x%x, EMB=0x%x, SP=0x%x",
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_lcr),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_pc),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_mtr),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_emb),
	    ISP_READ_RISC_REG(isp, &isp_risc->isp_risc_sp));
	isp_i_log(isp, CE_CONT, buf);

	/*
	 * If we paused the risc, restart it.
	 */
	if (risc_paused) {
		ISP_WRITE_RISC_HCCR(isp, ISP_HCCR_CMD_RELEASE);
	}

	/*
	 * Print isp queue settings out.
	 */
	isp->isp_request_out =
	    ISP_READ_MBOX_REG(isp, &isp->isp_mbox_reg->isp_mailbox4);
	(void) sprintf(buf,
	    "request(in/out)= %d/%d(%d), response(in/out)= %d/%d",
	    isp->isp_request_in, ISP_GET_REQUEST_OUT(isp), isp->isp_request_out,
	    isp->isp_response_in, isp->isp_response_out);
	isp_i_log(isp, CE_CONT, buf);

	(void) sprintf(buf,
	    "request_ptr(current, base)=  0x%p (0x%p)",
	    (void*) isp->isp_request_ptr, (void*) isp->isp_request_base);
	isp_i_log(isp, CE_CONT, buf);

	(void) sprintf(buf,
	    "response_ptr(current, base)= 0x%p (0x%p)",
	    (void*) isp->isp_response_ptr, (void*) isp->isp_response_base);
	isp_i_log(isp, CE_CONT, buf);

	if (ISP_READ_BIU_REG(isp, &isp->isp_biu_reg->isp_cdma_addr1) ||
	    ISP_READ_BIU_REG(isp, &isp->isp_biu_reg->isp_cdma_addr0)) {
		(void) sprintf(buf,
		    "dvma request_ptr= 0x%x - 0x%x",
		    (int)isp->isp_request_dvma,
		    (int)isp->isp_response_dvma);
		isp_i_log(isp, CE_CONT, buf);

		(void) sprintf(buf,
		    "dvma response_ptr= 0x%x - 0x%x",
		    (int)isp->isp_response_dvma,
		    (int)(isp->isp_request_dvma +
		    ISP_QUEUE_SIZE(isp_max_requests, isp_max_responses)));
		isp_i_log(isp, CE_CONT, buf);
	}


	/*
	 * period and offset entries.
	 * XXX this is not quite right if target options are different
	 */
	if (isp->isp_scsi_options & SCSI_OPTIONS_SYNC) {
		(void) sprintf(buf, "period/offset:");
		for (i = 0; i < NTARGETS; i++) {
			(void) sprintf(&buf[strlen(buf)], " %d/%d",
			    PERIOD_MASK(isp->isp_synch[i]),
			    OFFSET_MASK(isp->isp_synch[i]));
		}
		isp_i_log(isp, CE_CONT, buf);
		(void) sprintf(buf, "period/offset:");
		for (i = NTARGETS; i < NTARGETS_WIDE; i++) {
			(void) sprintf(&buf[strlen(buf)], " %d/%d",
			    PERIOD_MASK(isp->isp_synch[i]),
			    OFFSET_MASK(isp->isp_synch[i]));
		}
		isp_i_log(isp, CE_CONT, buf);
	}
}


/*
 * Function name : isp_i_alive()
 *
 * Return Values : use errno numbers for internal return code, not
 *		   for OS's consumption
 *		   zero  	= succeeded
 *		   ETIMEDOUT	= mbox cmd timedout
 *		   ETIME	= scsi packet time exceeded and the firmware
 *				  has failed to timeout the packet.
 *		   EBADF	= card returned bogus queue stats
 */
static int
isp_i_alive(struct isp *isp)
{
	struct isp_mbox_cmd	mbox_cmd;
	ushort_t 	rval = 0;
	ushort_t	total_io_completion;
	ushort_t	total_queued_io;
	ushort_t	total_exe_io;
#ifdef ISPDEBUG
	uint16_t	slot;
#endif

	ASSERT(isp != NULL);

	/*
	 * While we are draining commands or quiesceing the bus, the function
	 * should not do any checking.
	 */
	mutex_enter(ISP_HOTPLUG_MUTEX(isp));
	if ((isp->isp_softstate & ISP_SS_DRAINING) ||
	    (isp->isp_softstate & ISP_SS_QUIESCED)) {
		mutex_exit(ISP_HOTPLUG_MUTEX(isp));
		return (TRUE);
	}
	mutex_exit(ISP_HOTPLUG_MUTEX(isp));

	ISP_MUTEX_ENTER(isp);

#ifdef ISPDEBUG
	/*
	 * Verify that the busy list is in order with the first to
	 * timeout at the tail of the list.
	 */
	for (slot = isp->busy_slots.head; slot != ISP_MAX_SLOTS &&
	    isp->isp_slots[slot].next != ISP_MAX_SLOTS;
	    slot = isp->isp_slots[slot].next) {
		ASSERT(isp->isp_slots[slot].timeout >
		    isp->isp_slots[isp->isp_slots[slot].next].timeout);
	}
#endif

	/*
	 * First check that the oldest command should not be timed out.
	 */
	if (isp->busy_slots.tail != ISP_MAX_SLOTS &&
	    isp->isp_slots[isp->busy_slots.tail].timeout < gethrtime()) {
		isp_i_log(isp, CE_WARN, "ISP: Command timed out, resetting");
		rval = ETIME;
		goto fail;
	}

	if (isp_i_mbox_cmd_start(isp, &mbox_cmd, ISP_MBOX_CMD_GET_ISP_STAT) !=
	    0) {
		rval = ETIMEDOUT;
		goto fail;
	}

	total_io_completion = ISP_MBOX_RETURN_REG(&mbox_cmd, 1);
	total_queued_io = ISP_MBOX_RETURN_REG(&mbox_cmd, 3);
	total_exe_io = ISP_MBOX_RETURN_REG(&mbox_cmd, 2);


	/*
	 * for bug id# 4218841: we get many errors where total_queue_io is 1,
	 * and total_io_completion and total_exe_io are both 0 -- an error
	 *
	 * in this case, many of the times, that one command is a non-queued
	 * command
	 *
	 * many times the unqueued commands take longer to be processed when
	 * the ISP chip is busy, so we give those commands a little longer
	 * to finish
	 *
	 * (note: if we have queued I/O we should have executing I/O,
	 * regardless of whether any has completed or not)
	 */
	if ((total_io_completion == 0) &&	/* no I/O has completed */
	    (total_queued_io != 0) && (total_exe_io == 0)) {
		/* save knowledge of this for later */
		rval = EBADF;
	}

	if (isp_debug_timeout || (rval != 0)) {
		int	i, j;
		int	warn_value = rval ? CE_WARN : CE_NOTE;

		isp_i_log(isp, warn_value, "Command totals: queued=%d, "
		    "completed=%d, executing=%d",
		    total_queued_io, total_io_completion, total_exe_io);

		isp_i_log(isp, CE_CONT, "Running commands:");
		for (i = 0; i < ISP_MAX_SLOTS; i++) {
			int		tgt, lun;
			struct isp_cmd	*sp;
			struct scsi_pkt	*pkt;


			if ((sp = isp->isp_slots[i].isp_cmd) == NULL) {
				continue;
			}

			pkt = CMD2PKT(sp);
			ASSERT(pkt != NULL);

			/*
			 * save the isp_cmd information because it may
			 * complete and be deallocated during the
			 * mailbox command
			 */

			tgt = TGT(sp);
			lun = LUN(sp);

			if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
			    ISP_MBOX_CMD_GET_DEVICE_QUEUE_STATE,
			    TGT_N_LUN(tgt, lun)) != 0) {
				/*
				 * not surprising to have an error if
				 * the firmware is hosed -- assume all the
				 * rest of the mailbox commands will
				 * also puke and give up -- set an_error,
				 * since we *really* have an error now
				 */
				rval = ETIMEDOUT;
				goto fail;
			}
			isp_i_log(isp, CE_CONT,
			    "isp_cmd 0x%p: target %d "
			    "LUN %d state=%x exe=%d total=%d",
			    (void *)sp, tgt, lun,
			    ISP_MBOX_RETURN_REG(&mbox_cmd, 1),
			    ISP_MBOX_RETURN_REG(&mbox_cmd, 2),
			    ISP_MBOX_RETURN_REG(&mbox_cmd, 3));

		}

		isp_i_log(isp, CE_CONT, "Device queues:");
		for (i = 0; i < NTARGETS_WIDE; i++) {
			for (j = 0; j < isp->isp_max_lun[i]; j++) {
				if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
				    ISP_MBOX_CMD_GET_DEVICE_QUEUE_STATE,
				    TGT_N_LUN(i, j)) != 0) {
					/*
					 * as above, give up here
					 */
					rval = ETIMEDOUT;
					goto fail;
				}
				if ((ISP_MBOX_RETURN_REG(&mbox_cmd, 2) != 0) ||
				    (ISP_MBOX_RETURN_REG(&mbox_cmd, 3) != 0)) {
					isp_i_log(isp, CE_CONT,
					    "target %d LUN %d: "
					    "state=%x exe=%d total=%d",
					    i, j,
					    ISP_MBOX_RETURN_REG(&mbox_cmd, 1),
					    ISP_MBOX_RETURN_REG(&mbox_cmd, 2),
					    ISP_MBOX_RETURN_REG(&mbox_cmd, 3));
				}
			}
		}
	}

fail:
	mutex_exit(ISP_RESP_MUTEX(isp));
	isp_check_waitq_and_exit_req_mutex(isp);
	return (rval);
}


/*
 * kmem cache constructor and destructor.
 * When constructing, we bzero the isp cmd structure
 * When destructing, just free the dma handle
 */
/*ARGSUSED*/
static int
isp_kmem_cache_constructor(void * buf, void *cdrarg, int kmflags)
{
	struct isp_alloc_pkt *ap = buf;
	struct isp_cmd *sp = &ap->sp;
	struct isp *isp = cdrarg;
	ddi_dma_attr_t	tmp_dma_attr = dma_ispattr;

	int  (*callback)(caddr_t) = (kmflags == KM_SLEEP) ? DDI_DMA_SLEEP:
	    DDI_DMA_DONTWAIT;

	bzero((caddr_t)ap, EXTCMDS_SIZE);

	tmp_dma_attr.dma_attr_burstsizes = isp->isp_burst_size;

	if (ddi_dma_alloc_handle(isp->isp_dip, &tmp_dma_attr, callback,
	    NULL, &sp->cmd_dmahandle) != DDI_SUCCESS) {
		return (-1);
	}
	return (0);
}

/* ARGSUSED */
static void
isp_kmem_cache_destructor(void *buf, void *cdrarg)
{
	struct isp_alloc_pkt *ap = buf;
	struct isp_cmd *sp = &ap->sp;
	struct isp	*isp = cdrarg;


	ASSERT(sp != NULL);
	ASSERT(isp != NULL);

	if (sp->cmd_dmahandle) {
		ddi_dma_free_handle(&sp->cmd_dmahandle);
	}
}


#ifdef ISPDEBUG

/*
 * for testing
 *
 * called owning response mutex
 */
static void
isp_i_test(struct isp *isp, struct isp_cmd *sp)
{
	struct scsi_pkt *pkt = (sp != NULL)? CMD2PKT(sp) : NULL;
	struct scsi_address ap;

	/*
	 * Get the address from the packet - fill in address
	 * structure from pkt on to the local scsi_address structure
	 */
	ap.a_hba_tran = pkt->pkt_address.a_hba_tran;
	ap.a_target = pkt->pkt_address.a_target;
	ap.a_lun = pkt->pkt_address.a_lun;
	ap.a_sublun = pkt->pkt_address.a_sublun;

	if (isp_test_abort) {
		mutex_exit(ISP_RESP_MUTEX(isp));
		(void) isp_scsi_abort(&ap, pkt);
		mutex_enter(ISP_RESP_MUTEX(isp));
		isp_debug_enter_count++;
		isp_test_abort = 0;
	}
	if (isp_test_abort_all) {
		mutex_exit(ISP_RESP_MUTEX(isp));
		(void) isp_scsi_abort(&ap, NULL);
		mutex_enter(ISP_RESP_MUTEX(isp));
		isp_debug_enter_count++;
		isp_test_abort_all = 0;
	}
	if (isp_test_reset) {
		mutex_exit(ISP_RESP_MUTEX(isp));
		(void) isp_scsi_reset(&ap, RESET_TARGET);
		mutex_enter(ISP_RESP_MUTEX(isp));
		isp_debug_enter_count++;
		isp_test_reset = 0;
	}
	if (isp_test_reset_all) {
		mutex_exit(ISP_RESP_MUTEX(isp));
		(void) isp_scsi_reset(&ap, RESET_ALL);
		mutex_enter(ISP_RESP_MUTEX(isp));
		isp_debug_enter_count++;
		isp_test_reset_all = 0;
	}
	if (isp_test_fatal) {
		isp_test_fatal = 0;
		isp_i_fatal_error(isp, ISP_FORCE_RESET_BUS);
		isp_debug_enter_count++;
		isp_test_fatal = 0;
	}
}

#endif	/* ISPDEBUG */


#ifdef	ISPDEBUG_FW
/*
 * If a debug driver is loaded then writing to the devctl node will
 * cause the ISP driver to use whatever is written as new firmware.
 * The entire firmware image MUST be written in one operation.
 * Additional resets will blow away the new firmware.
 *
 * Called with neither request nor response mutexes held
 */
/*ARGSUSED*/
static int
isp_new_fw(dev_t dev, struct uio *uio, cred_t *cred_p)
{
	struct isp *isp;
	minor_t instance;
	unsigned short *newfw = NULL;
	size_t fwlen = 0;
	int rval = 0;


	instance = getminor(dev);
	isp = (struct isp *)ddi_get_soft_state(isp_state, instance);

	if (isp == NULL) {
		return (ENXIO);
	}

	/* Must start at beginning */
	if (uio->uio_loffset != 0) {
		return (EINVAL);
	}

	/* Sanity check the size: card has 128KB max */
	if ((fwlen = uio->uio_iov->iov_len) > 128*1024) {
		return (EINVAL);
	}

	if ((newfw = kmem_alloc(fwlen, KM_SLEEP)) == 0) {
		return (ENOMEM);
	}

	isp_i_log(isp, CE_WARN, "loading %x bytes of firmware", fwlen);

	/* Read it in */
	if ((rval = uiomove(newfw, fwlen, UIO_WRITE, uio)) != 0) {
		goto fail;
	}

	/* Grab this mutexes to prevent any commands from queueing */
	ISP_MUTEX_ENTER(isp);

	/* Blow away the F/W */
	(void) isp_i_reset_interface(isp, ISP_FORCE_RESET_BUS);

	/* Download new F/W */
	rval = isp_i_download_fw(isp, isp_risc_code_addr, newfw, fwlen/2);

	/* Re-init the F/W */
	(void) isp_i_reset_interface(isp, SKIP_STOP_QUEUES);

	/* Flush any pending commands (redundant?) */
	isp_i_qflush(isp, (ushort_t)0, (ushort_t)NTARGETS_WIDE - 1);

	mutex_exit(ISP_REQ_MUTEX(isp));
	(void) scsi_hba_reset_notify_callback(ISP_RESP_MUTEX(isp),
	    &isp->isp_reset_notify_listf);
	mutex_enter(ISP_REQ_MUTEX(isp));

	mutex_exit(ISP_RESP_MUTEX(isp));
	isp_check_waitq_and_exit_req_mutex(isp);
fail:
	kmem_free(newfw, fwlen);

	return (rval);
}

#endif	/* ISPDEBUG_FW */



#ifdef	ISPDEBUG_IOCTL

/*
 * used by ioctl routine to print devQ stats for one tgt/LUN
 *
 * return non-zero for error
 */
static int
isp_i_print_devq(struct isp *isp, int tgt, int lun)
{
	struct isp_mbox_cmd	mbox_cmd;


	if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
	    ISP_MBOX_CMD_GET_DEVICE_QUEUE_STATE, TGT_N_LUN(tgt, lun)) != 0) {
		isp_i_log(isp, CE_WARN, "error: can't get devQ stats");
		return (1);
	}

	/* any cmds executing here? */
	if ((ISP_MBOX_RETURN_REG(&mbox_cmd, 1) == 0) &&
	    (ISP_MBOX_RETURN_REG(&mbox_cmd, 2) == 0) &&
	    (ISP_MBOX_RETURN_REG(&mbox_cmd, 3) == 0)) {
		return (0);
	}

	/* print results and return */
	isp_i_log(isp, CE_CONT, "tgt=%d lun=%d state=0x%x, exe=%d, ttl=%d",
	    tgt, lun, ISP_MBOX_RETURN_REG(&mbox_cmd, 1),
	    ISP_MBOX_RETURN_REG(&mbox_cmd, 2),
	    ISP_MBOX_RETURN_REG(&mbox_cmd, 3));
	return (0);

}


/*
 * ioctl routine to allow debugging
 */
static int
isp_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	struct isp_mbox_cmd mbox_cmd;
	int		instance;		/* our minor number */
	struct isp	*isp;			/* our soft state ptr */
	int		rv = 0;			/* return value */


	/* get minor number -- mask off devctl bits */
	instance = (getminor(dev) >> 6) & 0xff;
	if ((isp = (struct isp *)ddi_get_soft_state(isp_state, instance)) ==
	    NULL) {
		isp_i_log(NULL, CE_WARN,
		    "isp: can't get soft state for instance %d", instance);
		goto try_scsa;
	}

	if (isp_debug_ioctl) {
		isp_i_log(isp, CE_NOTE,
		    "isp_ioctl(%d.%d, 0x%x, ...): entering",
		    getmajor(dev), getminor(dev), cmd);
	}

	switch (cmd) {

	case ISP_RELOAD_FIRMWARE:
		mutex_enter(ISP_RESP_MUTEX(isp));
		isp_i_fatal_error(isp,
		    (ISP_FORCE_RESET_BUS| ISP_DOWNLOAD_FW_ON_ERR));
		mutex_exit(ISP_RESP_MUTEX(isp));
		break;

	case ISP_PRINT_DEVQ_STATS: {
		int			tgt, lun;
		int			tgt_mask, lun_mask;


		tgt = ((int)arg >> 8) & 0xff;
		lun = (int)arg & 0xff;

		if ((tgt < 0) || (tgt >= NTARGETS_WIDE)) {
			tgt_mask = 0xffff;	/* all tgts */
		} else {
			tgt_mask = 1 << tgt;
		}
		if ((lun < 0) || (lun >= ISP_NLUNS_PER_TARGET)) {
			lun_mask = 0xffff;	/* all LUNs */
		} else {
			lun_mask = 1 << lun;
		}

		ISP_MUTEX_ENTER(isp);	/* freeze while looking at it */

		/* print genl device state */
		isp_i_print_state(CE_NOTE, isp);

		if (isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_GET_ISP_STAT) == 0) {
			isp_i_log(isp, CE_NOTE,
			    "Get ISP stats: completed %d, queued %d, exe %d\n",
			    ISP_MBOX_RETURN_REG(&mbox_cmd, 1),
			    ISP_MBOX_RETURN_REG(&mbox_cmd, 3),
			    ISP_MBOX_RETURN_REG(&mbox_cmd, 2));
		}

		(void) isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_GET_FIRMWARE_STATUS);
		isp_i_log(isp, CE_NOTE, "# of Completions %d # of IOCBs = %d",
		    ISP_MBOX_RETURN_REG(&mbox_cmd, 1),
		    ISP_MBOX_RETURN_REG(&mbox_cmd, 2));

		/* scan all tgts/luns */
		isp_i_log(isp, CE_NOTE,
		    "devQ stats (tgt=%d mask=0x%x, lun=%d mask=0x%x) ...",
		    tgt, tgt_mask, lun, lun_mask);

		for (tgt = 0; tgt < NTARGETS_WIDE; tgt++) {
			if (!(tgt_mask & (1 << tgt))) {
				continue;	/* skip this tgt */
			}
			for (lun = 0; lun < ISP_NLUNS_PER_TARGET; lun++) {
				if (!(lun_mask & (1 << lun))) {
					continue; /* skip this LUN */
				}
				if ((rv = isp_i_print_devq(isp, tgt, lun)) !=
				    0) {
					/*
					 * an error getting/printing data, so
					 * give up
					 */
					break;
				}
			}
		}

		ISP_MUTEX_EXIT(isp);		/* unfreeze now we're done */

		break;
	}

	case ISP_RESET_TARGET: {
		int			tgt;
		struct isp_mbox_cmd	mbox_cmd;


		tgt = (int)arg;

		if ((tgt < 0) || (tgt >= NTARGETS_WIDE)) {
			isp_i_log(isp, CE_NOTE,
			    "target out of range: %d (range 0-%d)",
			    tgt, NTARGETS_WIDE);
			rv = EINVAL;
			break;
		}

		isp_i_log(isp, CE_NOTE, "resetting target %d", tgt);

		ISP_MUTEX_ENTER(isp);	/* freeze while looking at it */
		if ((rv = isp_i_mbox_cmd_start(isp, &mbox_cmd,
		    ISP_MBOX_CMD_ABORT_TARGET, TGT_N_LUN(tgt, 0),
		    (ushort_t)(isp->isp_scsi_reset_delay/1000))) != 0) {
			isp_i_log(isp, CE_WARN, "can't start mbox cmd");
		}
		ISP_MUTEX_EXIT(isp);

		if (rv != 0) {
			break;
		}

		if ((rv = isp_i_set_marker(isp, SYNCHRONIZE_TARGET, (short)tgt,
		    0)) != 0) {
			ISP_DEBUG2(isp, CE_WARN, "can't set marker, "
			    "target %d (SCSI reset): queueing", tgt);
		}
		break;
	}

	case ISP_PRINT_SLOTS: {
		uint16_t		slot;
		struct isp_cmd		*sp;
		int			cnt = 0;

		mutex_enter(ISP_REQ_MUTEX(isp));

		slot = isp->busy_slots.head;
		isp_i_log(isp, CE_NOTE,
		    "Walking busy slots list, from slot: %d", slot);

		for (; slot != ISP_MAX_SLOTS;
		    slot = isp->isp_slots[slot].next) {
			cnt++;
			sp = isp->isp_slots[slot].isp_cmd;
			isp_i_log(isp, CE_CONT,
			    "slot %4d @ 0x%p: id=0x%x", slot,
			    (void *)sp, SP2TOKEN(sp));
		}
		mutex_exit(ISP_REQ_MUTEX(isp));
		isp_i_log(isp, CE_CONT, "%d active slot(s) found", cnt);
		break;
	}
	case ISP_DROP_COMMAND: {
		mutex_enter(ISP_REQ_MUTEX(isp));
		isp->drop_command++;
		mutex_exit(ISP_REQ_MUTEX(isp));
		break;
	}
	default:
try_scsa:
		rv = scsi_hba_ioctl(dev, cmd, arg, mode, credp, rvalp);
		break;
	}

	return (rv);
}

#endif	/* ISPDEBUG_IOCTL */


/*
 * the QLogic chip controls the "request queue out" pointer (and we control
 * the in pointer) -- the isp chip returns the out pointer in mailbox
 * register 4
 *
 * XXX: note that we do not do any handshaking with the chip here to ensure
 * that the value is stable/valid, so instead we hacked up reading the
 * register two times in a row until it returns the same value twice!
 *
 * enter and leave owning request mutex (don't care about response mutex)
 */
static void
isp_i_update_queue_space(struct isp *isp)
{
	ushort_t	old_outp;
	int		cnt = 0;


	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));

	old_outp = ISP_GET_REQUEST_OUT(isp);

	/* debounce request out value */
	while ((isp->isp_request_out = ISP_GET_REQUEST_OUT(isp)) != old_outp) {
		if (cnt++ >= ISP_REG_DEBOUNCE_LIMIT) {
			isp_i_log(isp, CE_WARN,
			    "request out register bounce excessive: "
			    "noise problem?");
			break;
		}
		ISP_DEBUG4(isp, SCSI_DEBUG,
		    "reqeust out pointer bounced from %d to %d (try %d)",
		    old_outp, isp->isp_request_out, cnt);
		old_outp = isp->isp_request_out;
	}

	ASSERT(isp->isp_request_out < isp_max_requests);

	/* update our count of space based on in and out pointers */
	if (isp->isp_request_in == isp->isp_request_out) {
		/* queue is empty so set space to max */
		isp->isp_queue_space = isp_max_requests - 1;
	} else if (isp->isp_request_in > isp->isp_request_out) {
		/* queue is partly empty/full (w/wraparound) */
		isp->isp_queue_space = (isp_max_requests - 1) -
		    (isp->isp_request_in - isp->isp_request_out);
	} else {
		/* queue is partly empty/full (no wraparound) */
		isp->isp_queue_space = isp->isp_request_out -
		    isp->isp_request_in - 1;
	}



	/*
	 * send any markers needed before any other I/O (if room)
	 */
	while ((isp->isp_marker_free < (ISP_MI_SIZE - 1)) &&
	    (isp->isp_queue_space > 0)) {
		struct isp_marker_info	*m;

		/* get pointer to this entry */
		m = &isp->isp_markers[isp->isp_marker_out++];
		isp->isp_marker_free++;		/* one more free slot now */

		/* double check assumption */
		ASSERT(isp->isp_marker_free < ISP_MI_SIZE);

		/* keep track of queue space, tracking wrap around */
		if (isp->isp_marker_out >= ISP_MI_SIZE) {
			isp->isp_marker_out = 0;	 /* wrap around */
		}

		/* send marker to chip */
		isp_i_send_marker(isp, m->isp_marker_mode, m->isp_marker_tgt,
		    m->isp_marker_lun);
	}

	ISP_DEBUG4(isp, SCSI_DEBUG,
	    "updated queue space: req(i/o)=%d/%d, now space=%d",
	    isp->isp_request_in, isp->isp_request_out,
	    isp->isp_queue_space);
}


/*
 * this routine is the guts of isp_i_set_marker() -- but, if the request
 * queue space is zero, this routine will *not* try to free new space.  This
 * is so that it can be called from the "get more queue space" thread without
 * recursing
 */
static void
isp_i_send_marker(struct isp *isp, short mod, ushort_t tgt, ushort_t lun)
{
	struct isp_request	*req;
	struct isp_request	req_buf;
	uint16_t		request_in;	/* for saving a copy */


	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));


	/*
	 * create a marker entry by filling in a request struct
	 */
	bzero(&req_buf, sizeof (struct isp_request));
	req_buf.req_header.cq_entry_type = CQ_TYPE_MARKER;
	req_buf.req_header.cq_entry_count = 1;
	req_buf.req_header.cq_seqno = 0xff;	/* for debugging purposes */
	req_buf.req_scsi_id.req_target = (uchar_t)tgt;
	req_buf.req_scsi_id.req_lun_trn = (uchar_t)lun;
	req_buf.req_modifier = mod;

	/* save a copy of the current reqeuest_in (for DMA) */
	request_in = isp->isp_request_in;

	/* get a pointer to the next free reqeust struct */
	ISP_GET_NEXT_REQUEST_IN(isp, req);

	/* copy our request into the request buffer */
	ISP_COPY_OUT_REQ(isp, &req_buf, req);

	/* sync request out to firmware */
	(void) ddi_dma_sync(isp->isp_dmahandle,
	    (off_t)(request_in * sizeof (struct isp_request)),
	    (uint_t)(sizeof (struct isp_request)), DDI_DMA_SYNC_FORDEV);

	/* Tell isp it's got a new I/O request... */
	ISP_SET_REQUEST_IN(isp);

	/* keep track of request Q space available */
	isp->isp_queue_space--;
	ISP_INC32_ERRSTATS(isp, isperr_marker_cmds);
}


/*
 * add marker info to the cirular queue for this instance
 *
 * Note: we could optimize this addition by skipping adding an entry
 * if a subsuming entry existed already.  For example, we could skip
 * adding an entry to send a marker for target 0 lun 0 if there's alredy
 * an entry to send a marker for target 0 all luns.  But this won't
 * be done since the need to send multiple targets when the request queue
 * is busy seems very unlikely (and hasn't happened in practice).  So
 * optimization is overkill (and may not work as well)
 *
 * this function can be called from interrupt context, so can not sleep
 *
 * return 0 upon success
 *
 * must be called owning the request mutex and not owning the response mutex
 */
static void
isp_i_add_marker_to_list(struct isp *isp, short mode, ushort_t tgt,
    ushort_t lun)
{
	struct isp_marker_info	*m;


	ASSERT(isp != NULL);
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());

	/*
	 * we allocate enough room so that we never run out, but just
	 * in case ...
	 */
	if (isp->isp_marker_free == 0) {
		/*
		 * perhaps later just set index to zero and try to go on???
		 */
		isp_i_log(isp, CE_PANIC,
		    "fatal error: no room to save markers, already saving %d",
		    ISP_MI_SIZE - 1);
		_NOTE(NOT_REACHED)
		/*NOTREACHED*/
	}

	/* get pointer to our entry */
	m = &isp->isp_markers[isp->isp_marker_in++];
	isp->isp_marker_free--;			/* one less free slot */

	/* check for wrap around */
	if (isp->isp_marker_in >= (ISP_MI_SIZE - 1)) {
		isp->isp_marker_in = 0;
	}

	/* fill in our entry */
	m->isp_marker_mode = mode;
	m->isp_marker_tgt = tgt;
	m->isp_marker_lun = lun;
}


/*
 * get response_in pointer, with debouncing
 *
 * Note that there is no limit on the number of times we'll loop here, but
 * the good news is that since a bounce is a transitory event the theory
 * is that we will soon get the same value two times in a row
 */
static ushort_t
isp_i_get_response_in_db(struct isp *isp)
{
	ushort_t	ri_old;		/* old resp */
	ushort_t	ri_new;		/* new resp */
	int		cnt = 0;


	ASSERT(isp != NULL);

	ri_old = ISP_GET_RESPONSE_IN(isp);

	while ((ri_new = ISP_GET_RESPONSE_IN(isp)) != ri_old) {
		/* should we do a sleep?  probably not */
		if (cnt++ >= ISP_REG_DEBOUNCE_LIMIT) {
			isp_i_log(isp, CE_WARN,
			    "response in register bounce excessive: "
			    "noise problem?");
			break;
		}
		ISP_DEBUG4(isp, SCSI_DEBUG,
		    "response in pointer bounced from %d to %d (try %d)",
		    ri_old, ri_new, cnt);
		ri_old = ri_new;
	}

	ASSERT(ri_new < isp_max_responses);

	/* we got the same value twice in a row */
	return (ri_new);
}


/*
 * this used to be a macro, but that was hard to read and hard to warlock
 */
static void
isp_check_waitq_and_exit_req_mutex(struct isp *isp)
{
	ASSERT(isp != NULL);

	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(ISP_REQ_MUTEX(isp)))

	mutex_enter(ISP_WAITQ_MUTEX(isp));

	isp_i_empty_waitQ(isp);

	mutex_exit(ISP_REQ_MUTEX(isp));

	mutex_exit(ISP_WAITQ_MUTEX(isp));
}

/*
 * isp_create_errstats
 */
static void
isp_create_errstats(struct isp *isp, int instance)
{
	char	kstatname[KSTAT_STRLEN];
	struct  isp_errstats 	*iep;

	ASSERT(isp != NULL);

	(void) snprintf(kstatname, KSTAT_STRLEN, "isperr_%d", instance);
	isp->isp_err_kstats = kstat_create("isperr", instance,
	    kstatname, "isp_error", KSTAT_TYPE_NAMED,
	    sizeof (struct isp_errstats)/sizeof (kstat_named_t),
	    KSTAT_FLAG_PERSISTENT);

	if (isp->isp_err_kstats == NULL) {
		isp_i_log(isp, CE_WARN, "isp_create_errstats failed: \n");
		return;
	}

	iep = (struct isp_errstats *)isp->isp_err_kstats->ks_data;
	kstat_named_init(&iep->isperr_completed_cmds, "completed cmds",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_waitq_cmds, "total waitq cmds",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_total_cmds, "total cmds started",
	    KSTAT_DATA_UINT32);

	kstat_named_init(&iep->isperr_cmd_incomplete, "cmd incomplete count",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_cmd_dma_err, "cmd dma error count",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_cmd_tran_err,
	    "cmd transport error count", KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_cmd_reset, "cmd reset count",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_cmd_abort, "cmd abort count",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_cmd_terminated, "cmd terminated count",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_cmd_timeout, "cmd timeout count",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_cmd_data_error, "cmd data error count",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_cmd_overrun, "cmd overrun count",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_cmd_misc_error, "cmd misc error count",
	    KSTAT_DATA_UINT32);

	kstat_named_init(&iep->isperr_lbolt_incomplete, "cmd incomplete lbolt",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&iep->isperr_lbolt_dma_err, "cmd dma error lbolt",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&iep->isperr_lbolt_tran_err,
	    "cmd transport error lbolt", KSTAT_DATA_UINT64);
	kstat_named_init(&iep->isperr_lbolt_reset, "cmd reset lbolt",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&iep->isperr_lbolt_abort, "cmd abort lbolt",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&iep->isperr_lbolt_terminated, "cmd terminated lbolt",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&iep->isperr_lbolt_timeout, "cmd timeout lbolt",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&iep->isperr_lbolt_data_error, "cmd data error lbolt",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&iep->isperr_lbolt_overrun, "cmd overrun lbolt",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&iep->isperr_lbolt_misc_error, "cmd misc error lbolt",
	    KSTAT_DATA_UINT64);

	kstat_named_init(&iep->isperr_mailbox_cmds, "mailbox cmds",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_polled_cmds, "polled cmds",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_marker_cmds, "marker cmds",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_ars_pkts, "ars pkts",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_get_capability, "scsi_getcap calls",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_set_capability, "scsi_setcap calls",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_response_error, "isp req/response errors",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_mailbox_started, "mailbox started",
	    KSTAT_DATA_UINT64);

	kstat_named_init(&iep->isperr_fatal_errors, "isp fatal errors",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_reset_chip, "isp reset chip",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_firmware_loads, "isp firmware loads",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_suspends, "isp suspends",
	    KSTAT_DATA_UINT32);
	kstat_named_init(&iep->isperr_resumes, "isp resumes",
	    KSTAT_DATA_UINT32);
	isp->isp_err_kstats->ks_private = isp;
	isp->isp_err_kstats->ks_update = nulldev;
	kstat_install(isp->isp_err_kstats);
}

#ifdef ISPDEBUG
#ifndef lint
static void
isp_i_print_response(struct isp *isp, struct isp_response *data_resp)
{
	if (data_resp == NULL)
		return;

	isp_i_log(isp, CE_WARN,
	    "isp_print_resp header type %x cnt %x flags %x %x",
	    (uint8_t)data_resp->resp_header.cq_entry_type,
	    (uint8_t)data_resp->resp_header.cq_entry_count,
	    (uint8_t)data_resp->resp_header.cq_flags,
	    (uint8_t)data_resp->resp_header.cq_seqno);
	isp_i_log(isp, CE_WARN, "    token  %x, scb %x reason %x, state %x",
	    data_resp->resp_token, data_resp->resp_scb, data_resp->resp_reason,
	    data_resp->resp_state);
	isp_i_log(isp, CE_WARN, "    status %x, time %x, rqs %x resid %x",
	    data_resp->resp_status_flags, data_resp->resp_time,
	    data_resp->resp_rqs_count,
	    data_resp->resp_resid);
}
#endif
#endif

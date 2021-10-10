/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * mpt - This is a driver based on LSI Logic's MPT interface.
 *
 * Current HW support:
 * (Parallel SCSI)	1030
 * (IR and IT fw)	1064, 1064E, 1068 and 1068E
 * (IR fw only)		1078
 */

#if defined(__lint) && !defined(DEBUG)
#define	DEBUG 1
#define	MPT_DEBUG
#endif

/*
 * standard header files.
 */
#include <sys/note.h>
#include <sys/scsi/scsi.h>
#include <sys/pci.h>
#include <sys/file.h>
#include <sys/policy.h>
#include <sys/byteorder.h>
#include <sys/sysevent.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/sysevent/dr.h>
#include <sys/sata/sata_defs.h>
#include <sys/time.h>

#pragma pack(1)
#include <sys/mpt/mpi.h>
#include <sys/mpt/mpi_cnfg.h>
#include <sys/mpt/mpi_init.h>
#include <sys/mpt/mpi_ioc.h>
#include <sys/mpt/mpi_raid.h>
#pragma pack()

/*
 * private header files.
 *
 */
#include <sys/scsi/impl/scsi_reset_notify.h>
#include <sys/scsi/adapters/mptvar.h>
#include <sys/scsi/adapters/mptreg.h>
#include <sys/scsi/adapters/mpt_ioctl.h>
#include <sys/raidioctl.h>

#include <sys/fs/dv_node.h>	/* devfs_clean */

/*
 * FMA header files
 */
#include <sys/ddifm.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/fm/io/ddi.h>

/*
 * Values for the SAS DeviceInfo field used in SAS Device Status Change Event
 * data and SAS IO Unit Configuration pages.
 */
#define	MPI_SAS_DEVICE_INFO_SEP			(0x00004000)
#define	MPI_SAS_DEVICE_INFO_ATAPI_DEVICE	(0x00002000)
#define	MPI_SAS_DEVICE_INFO_LSI_DEVICE		(0x00001000)
#define	MPI_SAS_DEVICE_INFO_DIRECT_ATTACH	(0x00000800)
#define	MPI_SAS_DEVICE_INFO_SSP_TARGET		(0x00000400)
#define	MPI_SAS_DEVICE_INFO_STP_TARGET		(0x00000200)
#define	MPI_SAS_DEVICE_INFO_SMP_TARGET		(0x00000100)
#define	MPI_SAS_DEVICE_INFO_SATA_DEVICE		(0x00000080)
#define	MPI_SAS_DEVICE_INFO_SSP_INITIATOR	(0x00000040)
#define	MPI_SAS_DEVICE_INFO_STP_INITIATOR	(0x00000020)
#define	MPI_SAS_DEVICE_INFO_SMP_INITIATOR	(0x00000010)
#define	MPI_SAS_DEVICE_INFO_SATA_HOST		(0x00000008)

#define	MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE	(0x00000007)
#define	MPI_SAS_DEVICE_INFO_NO_DEVICE		(0x00000000)
#define	MPI_SAS_DEVICE_INFO_END_DEVICE		(0x00000001)
#define	MPI_SAS_DEVICE_INFO_EDGE_EXPANDER	(0x00000002)
#define	MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER	(0x00000003)

/*
 * autoconfiguration data and routines.
 */
static int mpt_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int mpt_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);
static int mpt_power(dev_info_t *dip, int component, int level);

static void mpt_setup_cmd_reg(mpt_t *mpt);
static void mpt_disable_bus_master(mpt_t *mpt);
static void mpt_hba_fini(mpt_t *mpt);
static void mpt_cfg_fini(mpt_t *mpt_blkp);
static int mpt_alloc_post_frames(mpt_t *mpt);
static int mpt_alloc_extra_cmd_mem(mpt_t *mpt, mpt_cmd_t *cmd);
static void mpt_free_extra_cmd_mem(mpt_cmd_t *cmd);
static int mpt_ioc_init_reply_queue(mpt_t *mpt);
static int mpt_get_sas_device_page0(mpt_t *mpt, int address, uint16_t *handle,
    uint32_t *info, uint64_t *sas_wwn, int *tgt);
static int mpt_get_scsi_device_params(mpt_t *mpt, int target, uint32_t *params);
static int mpt_get_scsi_port_params(mpt_t *mpt, int bus, uint32_t *params);
static int mpt_write_scsi_device_params(mpt_t *mpt, int target,
    uint32_t parameters, uint32_t configuration);
static int mpt_set_ioc_params(mpt_t *mpt);
static int mpt_set_port_params(mpt_t *mpt, int port, uint32_t timer);
static int mpt_init_chip(mpt_t *mpt, int first_time);
static int mpt_init_pm(mpt_t *mpt);
static int mpt_get_wwid(mpt_t *mpt);
static void mpt_idle_pm(void *arg);

/*
 * SCSA function prototypes with some helper functions for DMA.
 */
static int mpt_scsi_start(struct scsi_address *ap, struct scsi_pkt *pkt);
static int mpt_scsi_reset(struct scsi_address *ap, int level);
static int mpt_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt);
static int mpt_capchk(char *cap, int tgtonly, int *cidxp);
static int mpt_scsi_getcap(struct scsi_address *ap, char *cap, int tgtonly);
static int mpt_scsi_setcap(struct scsi_address *ap, char *cap, int value,
    int tgtonly);

static int mpt_tran_setup_pkt(struct scsi_pkt *pkt, int (*callback)(),
    caddr_t arg);
static void mpt_tran_teardown_pkt(struct scsi_pkt *pkt);
static int mpt_tran_pkt_constructor(struct scsi_pkt *pkt, scsi_hba_tran_t *tran,
    int kmflag);
static void mpt_tran_pkt_destructor(struct scsi_pkt *pkt,
    scsi_hba_tran_t *tran);
static int mpt_pkt_alloc_extern(mpt_t *mpt, mpt_cmd_t *cmd,
    int cmdlen, int tgtlen, int statuslen, int kf);
static void mpt_pkt_destroy_extern(mpt_cmd_t *cmd);

static int mpt_scsi_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd);
static int mpt_scsi_tgt_atapi(struct scsi_device *sd);
static void mpt_scsi_tgt_free(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd);
static int mpt_scsi_reset_notify(struct scsi_address *ap, int flag,
    void (*callback)(caddr_t), caddr_t arg);
static int mpt_scsi_get_name(struct scsi_device *sd, char *ua, int len);
static int mpt_scsi_get_bus_addr(struct scsi_device *sd, char *ra, int len);

#ifndef	__sparc
static int mpt_quiesce(dev_info_t *devi);
#endif	/* __sparc */

static void mpt_update_max_luns(mpt_t *mpt, int tgt);
static int mpt_scsi_quiesce(dev_info_t *dip);
static int mpt_scsi_unquiesce(dev_info_t *dip);
static int mpt_quiesce_bus(mpt_t *mpt);
static int mpt_unquiesce_bus(mpt_t *mpt);
static void mpt_ncmds_checkdrain(void *arg);
static int mpt_alloc_handshake_msg(mpt_t *mpt, size_t alloc_size);
static void mpt_free_handshake_msg(mpt_t *mpt);

static int mpt_smp_start(struct smp_pkt *smp_pkt);

/*
 * This device is created by the SCSI pseudo nexus driver (SCSI vHCI).  It is
 * under this device that the paths to a physical device are created when
 * MPxIO is used.
 */
extern dev_info_t	*scsi_vhci_dip;
/*
 * internal function prototypes.
 */
static int mpt_dr_detach(dev_info_t *dev);

static int mpt_accept_txwq_and_pkt(mpt_t *mpt, mpt_cmd_t *sp);
static int mpt_accept_pkt(mpt_t *mpt, mpt_cmd_t *sp);
static int mpt_prepare_pkt(mpt_t *mpt, mpt_cmd_t *cmd);
static int mpt_do_scsi_reset(struct scsi_address *ap, int level);
static int mpt_do_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt);
static void mpt_handle_qfull(mpt_t *mpt, mpt_cmd_t *cmd);
static void mpt_handle_event(void *args);
static int mpt_handle_event_sync(void *args);
static void mpt_handle_dr(void *args);
static void mpt_restart_cmd(void *);
static void mpt_remove_cmd(mpt_t *mpt, mpt_cmd_t *cmd);
static void mpt_flush_hba(mpt_t *mpt);
static void mpt_flush_target(mpt_t *mpt, ushort_t target, int lun,
    uint8_t tasktype);
static void mpt_set_pkt_reason(mpt_t *mpt, mpt_cmd_t *cmd,
    uchar_t reason, uint_t stat);
static void mpt_process_intr(mpt_t *mpt, uint32_t istat);
static int mpt_wait_intr(mpt_t *mpt, int polltime);
static void mpt_sge_setup(mpt_t *mpt, mpt_cmd_t *cmd, uint32_t *control,
    msg_scsi_io_request_t *frame, uint32_t fma, ddi_acc_handle_t acc_hdl);

static void mpt_watch(void *arg);
static void mpt_watchsubr(mpt_t *mpt);
static void mpt_cmd_timeout(mpt_t *mpt, int target);
static void mpt_sync_wide_backoff(mpt_t *mpt, int target);


static uint_t mpt_intr(caddr_t arg1, caddr_t arg2);
static void mpt_check_scsi_io_error(mpt_t *mpt, msg_scsi_io_reply_t *reply,
    mpt_cmd_t *cmd);
static void mpt_check_task_mgt(mpt_t *mpt, msg_scsi_task_mgmt_reply_t *reply,
    mpt_cmd_t *cmd);
static void mpt_accept_tx_waitq(mpt_t *mpt);
static void mpt_restart_hba(mpt_t *mpt);
static void mpt_restart_waitq(mpt_t *mpt);
static int mpt_start_cmd(mpt_t *mpt, mpt_cmd_t *cmd);
static void mpt_deliver_doneq_thread(mpt_t *mpt);
static void mpt_doneq_add(mpt_t *mpt, mpt_cmd_t *cmd);
static void mpt_doneq_mv(mpt_t *mpt, uint64_t t);
static mpt_cmd_t *mpt_doneq_thread_rm(mpt_t *mpt, uint64_t t);
static void mpt_doneq_empty(mpt_t *mpt);
static void mpt_waitq_add(mpt_t *mpt, mpt_cmd_t *cmd);
static mpt_cmd_t *mpt_waitq_rm(mpt_t *mpt);
static mpt_cmd_t *mpt_tx_waitq_rm(mpt_t *mpt);
static void mpt_waitq_delete(mpt_t *mpt, mpt_cmd_t *cmd);
static void mpt_tx_waitq_delete(mpt_t *mpt, mpt_cmd_t *cmd);

static void mpt_start_watch_reset_delay();
static void mpt_setup_bus_reset_delay(mpt_t *mpt);
static void mpt_watch_reset_delay(void *arg);
static int mpt_watch_reset_delay_subr(mpt_t *mpt);

static void mpt_update_props(mpt_t *mpt, int tgt);
static void mpt_update_this_prop(mpt_t *mpt, char *property, int value);
static int mpt_alloc_active_slots(mpt_t *mpt, int flag);
static void mpt_set_scsi_options(mpt_t *mpt, int target, int options);

static void mpt_dump_cmd(mpt_t *mpt, mpt_cmd_t *cmd);
static int mpt_ioctl(dev_t dev, int cmd, intptr_t data, int mode,
	cred_t *credp, int *rval);

static dev_info_t *mpt_find_child(mpt_t *mpt, uint16_t target, int lun);
static dev_info_t *mpt_find_smp_child(mpt_t *mpt, char *str_wwn);
static mdi_pathinfo_t *mpt_find_path(mpt_t *mpt, uint16_t target, int lun);
static uint64_t mpt_get_sata_device_name(mpt_t *mpt, int target);
static int mpt_wwid_to_tgt(mpt_t *mpt, uint64_t wwid);
static int mpt_phy_to_tgt(mpt_t *mpt, uint8_t phy);
static int mpt_parse_id_lun(char *name, uint64_t *wwid, uint8_t *phy, int *lun);
static int mpt_parse_name(char *name, int *target, int *lun);
static int mpt_parse_smp_name(char *name, uint64_t *wwn);
static int mpt_is_hex(char *num);
static char *mpt_correct_obp_sas_disk_arg(void *arg, size_t *size);
static int mpt_send_inquiryVpd(mpt_t *mpt, int target, int lun, uchar_t page,
    unsigned char *buf, int len, int *rlen);
static int mpt_create_phys_lun(mpt_t *mpt, struct scsi_device *sd,
    char *guid, uint32_t devinfo, uint64_t sas_wwn, dev_info_t **dip);
static int mpt_create_virt_lun(mpt_t *mpt, struct scsi_device *sd,
    char *guid, uint64_t sas_wwn, uint32_t devinfo, dev_info_t **dip,
    mdi_pathinfo_t **pip);
static int mpt_config_luns(mpt_t *mpt, int target, int dev_info,
    uint64_t sas_wwn);

static int mpt_scsi_probe_lun(mpt_t *mpt, uint16_t target, int lun,
    dev_info_t **dip);
static void mpt_scsi_config_all(mpt_t *mpt);
static int mpt_scsi_config_one(mpt_t *mpt, int target, int lun,
    dev_info_t **lun_dip);
static int mpt_scsi_bus_config(dev_info_t *parent, uint_t flags,
    ddi_bus_config_op_t op, void *arg, dev_info_t **childp);
static int mpt_sas_probe_lun(mpt_t *mpt, uint16_t target, int lun,
    uint32_t dev_info, dev_info_t **dip, uint64_t sas_wwn);
static void mpt_sas_config_all(mpt_t *mpt);
static int mpt_sas_config_one(mpt_t *mpt, int target, int lun,
    dev_info_t **lun_dip);
static int mpt_sas_unconfig_one(mpt_t *mpt, int target, int lun, uint_t flags);
static int mpt_sas_create_lun(mpt_t *mpt, struct scsi_device *sd,
    uint64_t sas_wwn, uint32_t devinfo, dev_info_t **dip);
static void mpt_handle_missed_luns(mpt_t *mpt, int target,
    uint16_t *repluns, int lun_cnt);
static void mpt_sas_config_target(mpt_t *mpt, int target, int dev_info,
    uint64_t sas_wwn);
static int mpt_sas_config_raid(mpt_t *mpt, uint16_t target, uint64_t wwn,
    dev_info_t **dip);
static int mpt_sas_config_smp(mpt_t *mpt, uint64_t sas_wwn,
    dev_info_t **smp_dip);
static int mpt_sas_offline_smp(mpt_t *mpt, uint64_t wwn, uint_t flags);
static int mpt_sas_offline_target(mpt_t *mpt, int target);
static int mpt_sas_bus_config(dev_info_t *parent, uint_t flags,
    ddi_bus_config_op_t op, void *arg, dev_info_t **childp);
static void mpt_gen_sysevent(mpt_t *mpt, int target, int hint);
static int mpt_set_init_mode(mpt_t *mpt);
static int mpt_event_query(mpt_t *mpt, mpt_event_query_t *data, int mode,
    int *rval);
static int mpt_event_enable(mpt_t *mpt, mpt_event_enable_t *data, int mode,
    int *rval);
static int mpt_event_report(mpt_t *mpt, mpt_event_report_t *data, int mode,
    int *rval);
static void mpt_record_event(void *args);

static void mpt_doneq_thread(mpt_doneq_thread_arg_t *arg);

static void mpt_start_passthru(mpt_t *mpt, mpt_cmd_t *cmd);

/*
 * MPT MSI tunable:
 *
 * By default MSI is enabled on all supported platforms.
 */
boolean_t mpt_enable_msi = B_TRUE;
boolean_t mpt_physical_bind_failed_page_83 = B_FALSE;

static int mpt_add_intrs(mpt_t *, int);
static void mpt_rem_intrs(mpt_t *);

/*
 * FMA Prototypes
 */
static void mpt_fm_init(mpt_t *mpt);
static void mpt_fm_fini(mpt_t *mpt);
static int mpt_fm_error_cb(dev_info_t *, ddi_fm_error_t *, const void *);
int mpt_check_acc_handle(ddi_acc_handle_t handle);
int mpt_check_dma_handle(ddi_dma_handle_t handle);
void mpt_fm_ereport(mpt_t *mpt, char *detail);

/*
 * Tunable timeout value for Test Unit Ready
 * By default the value is 30 seconds.
 */
int mpt_inq83_retry_timeout = 30;

/*
 * This is used to allocate memory for message frame storage, not for
 * data I/O DMA. All message frames must be stored in the first 4G of
 * physical memory.
 */
ddi_dma_attr_t mpt_dma_attrs = {
	DMA_ATTR_V0,	/* attribute layout version		*/
	0x0ull,		/* address low - should be 0 (longlong)	*/
	0xffffffffull,	/* address high - 32-bit max range	*/
	0x00ffffffull,	/* count max - max DMA object size	*/
	4,		/* allocation alignment requirements	*/
	0x78,		/* burstsizes - binary encoded values	*/
	1,		/* minxfer - gran. of DMA engine	*/
	0x00ffffffull,	/* maxxfer - gran. of DMA engine	*/
	0xffffffffull,	/* max segment size (DMA boundary)	*/
	MPT_MAX_DMA_SEGS, /* scatter/gather list length		*/
	512,		/* granularity - device transfer size	*/
	0		/* flags, set to 0			*/
};

/*
 * This is used for data I/O DMA memory allocation. (full 64-bit DMA
 * physical addresses are supported.)
 */
ddi_dma_attr_t mpt_dma_attrs64 = {
	DMA_ATTR_V0,	/* attribute layout version		*/
	0x0ull,		/* address low - should be 0 (longlong)	*/
	0xffffffffffffffffull,	/* address high - 64-bit max	*/
	0x00ffffffull,	/* count max - max DMA object size	*/
	4,		/* allocation alignment requirements	*/
	0x78,		/* burstsizes - binary encoded values	*/
	1,		/* minxfer - gran. of DMA engine	*/
	0x00ffffffull,	/* maxxfer - gran. of DMA engine	*/
	0xffffffffull,	/* max segment size (DMA boundary)	*/
	MPT_MAX_DMA_SEGS, /* scatter/gather list length		*/
	512,		/* granularity - device transfer size	*/
	DDI_DMA_RELAXED_ORDERING	/* flags, enable relaxed ordering */
};

ddi_device_acc_attr_t dev_attr = {
	DDI_DEVICE_ATTR_V1,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC,
	DDI_DEFAULT_ACC
};

static struct cb_ops mpt_cb_ops = {
	scsi_hba_open,		/* open */
	scsi_hba_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	mpt_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* chpoll */
	ddi_prop_op,		/* cb_prop_op */
	NULL,			/* streamtab */
	D_MP,			/* cb_flag */
	CB_REV,			/* rev */
	nodev,			/* aread */
	nodev			/* awrite */
};

static struct dev_ops mpt_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ddi_no_info,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	mpt_attach,		/* attach */
	mpt_detach,		/* detach */
	nodev,			/* reset */
	&mpt_cb_ops,		/* driver operations */
	NULL,			/* bus operations */
	mpt_power,		/* power management */
#ifdef	__sparc
	ddi_quiesce_not_needed
#else
	mpt_quiesce		/* quiesce */
#endif	/* __sparc */
};


#define	MPT_MOD_STRING "MPT HBA Driver"

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module. This one is a driver */
	MPT_MOD_STRING, /* Name of the module. */
	&mpt_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};
#define	TARGET_PROP	"target"
#define	LUN_PROP	"lun"
#define	SAS_PROP	"sas-mpt"
#define	OFFLINE_DELAY_PROP	"mpt_offline_delay"
#define	QUIESCE_DELAY_PROP	"mpt_quiesce_delay"
#define	CACHE_PROBE_PROP	"mpt_cache_probe"
#define	MDI_GUID	"wwn"
#define	NDI_GUID	"guid"
#define	MPT_DEV_GONE	"mpt_dev_gone"

extern pri_t minclsyspri, maxclsyspri;

/*
 * Local static data
 */
#if defined(MPT_DEBUG)
uint32_t mpt_debug_flags = 0x0;
#endif	/* defined(MPT_DEBUG) */
uint32_t mpt_debug_resets = 0;

static kmutex_t		mpt_global_mutex;
static void		*mpt_state;		/* soft	state ptr */
static krwlock_t	mpt_global_rwlock;

static kmutex_t		mpt_log_mutex;
static char		mpt_log_buf[256];
_NOTE(MUTEX_PROTECTS_DATA(mpt_log_mutex, mpt_log_buf))

static mpt_t *mpt_head, *mpt_tail;
static clock_t mpt_scsi_watchdog_tick;
static clock_t mpt_tick;
static timeout_id_t mpt_reset_watch;
static timeout_id_t mpt_timeout_id;
static int mpt_timeouts_enabled = 0;

/*
 * warlock directives
 */
_NOTE(SCHEME_PROTECTS_DATA("unique per pkt", scsi_pkt \
	mpt_cmd NcrTableIndirect buf scsi_cdb scsi_status))
_NOTE(SCHEME_PROTECTS_DATA("unique per pkt", smp_pkt))
_NOTE(SCHEME_PROTECTS_DATA("stable data", scsi_device scsi_address))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", mpt_tgt_private))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", scsi_hba_tran::tran_tgt_private))

#ifdef MPT_DEBUG
#define	MPT_TEST
static int mpt_no_sync_wide_backoff;
static int mpt_rtest = -1;
static int mpt_rtest_type;
static int mpt_atest = -1;
static int mpt_atest_type;
static int mpt_test_stop;
static int mpt_test_instance;
static int mpt_test_untagged;
static int mpt_enable_untagged;
static int mpt_test_timeouts;
static int mpt_fail_raid = 0;
static int mpt_kill_ioc = 0;

void debug_enter(char *);
static void mpt_test_reset(mpt_t *mpt, int target);
static void mpt_test_abort(mpt_t *mpt, int target);
#endif

/*
 * Notes:
 *	- scsi_hba_init(9F) initializes SCSI HBA modules
 *	- must call scsi_hba_fini(9F) if modload() fails
 */
int
_init(void)
{
	int status;
	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	NDBG0(("_init"));

	status = ddi_soft_state_init(&mpt_state, MPT_SIZE,
	    MPT_INITIAL_SOFT_SPACE);
	if (status != 0) {
		return (status);
	}

	if ((status = scsi_hba_init(&modlinkage)) != 0) {
		ddi_soft_state_fini(&mpt_state);
		return (status);
	}

	mutex_init(&mpt_global_mutex, NULL, MUTEX_DRIVER, NULL);
	rw_init(&mpt_global_rwlock, NULL, RW_DRIVER, NULL);
	mutex_init(&mpt_log_mutex, NULL, MUTEX_DRIVER, NULL);

	if ((status = mod_install(&modlinkage)) != 0) {
		mutex_destroy(&mpt_log_mutex);
		rw_destroy(&mpt_global_rwlock);
		mutex_destroy(&mpt_global_mutex);
		ddi_soft_state_fini(&mpt_state);
		scsi_hba_fini(&modlinkage);
	}

	return (status);
}

/*
 * Notes:
 *	- scsi_hba_fini(9F) uninitializes SCSI HBA modules
 */
int
_fini(void)
{
	int status;
	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	NDBG0(("_fini"));

	if ((status = mod_remove(&modlinkage)) == 0) {
		ddi_soft_state_fini(&mpt_state);
		scsi_hba_fini(&modlinkage);
		mutex_destroy(&mpt_global_mutex);
		rw_destroy(&mpt_global_rwlock);
		mutex_destroy(&mpt_log_mutex);
	}
	return (status);
}

/*
 * The loadable-module _info(9E) entry point
 */
int
_info(struct modinfo *modinfop)
{
	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);
	NDBG0(("mpt _info"));

	return (mod_info(&modlinkage, modinfop));
}

/*
 * Notes:
 *	Set up all device state and allocate data structures,
 *	mutexes, condition variables, etc. for device operation.
 *	Add interrupts needed.
 *	Return DDI_SUCCESS if device is ready, else return DDI_FAILURE.
 */
static int
mpt_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	mpt_t			*mpt = NULL;
	char			*prop_template = "target%d-scsi-options";
	char			prop_str[32];
	char			*prop;
	int			instance, i, j, id;
	int			doneq_thread_num = 0;
	char			intr_added = 0;
	char			map_setup = 0;
	char			config_setup = 0;
	char			hba_attach_setup = 0;
	char			phci_register = 0;
	char			mutex_init_done = 0;
	char			ev_taskq_create = 0;
	char			dr_taskq_create = 0;
	char			doneq_thread_create = 0;
	scsi_hba_tran_t		*hba_tran;
	ushort_t		comm;
	int			intr_types;
	char			initiator_wwnstr[MPT_SAS_WWN_STRLEN];
	uint_t			mem_bar = MEM_SPACE;
	char			*device_type;

	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		if ((hba_tran = ddi_get_driver_private(dip)) == NULL)
			return (DDI_FAILURE);

		mpt = TRAN2MPT(hba_tran);

		if (!mpt) {
			return (DDI_FAILURE);
		}

		/*
		 * Restart watch thread
		 */
		mutex_enter(&mpt_global_mutex);
		if (mpt_timeout_id == 0) {
			mpt_timeout_id = timeout(mpt_watch, NULL, mpt_tick);
			mpt_timeouts_enabled = 1;
		}
		mutex_exit(&mpt_global_mutex);

		/*
		 * Reset hardware and softc to "no outstanding commands"
		 * Note	that a check condition can result on first command
		 * to a	target.
		 */
		mutex_enter(&mpt->m_mutex);

		/*
		 * raise power.
		 */
		if (mpt->m_options & MPT_OPT_PM) {
			int rval;
			mutex_exit(&mpt->m_mutex);
			(void) pm_busy_component(dip, 0);
			rval = pm_raise_power(dip, 0, PM_LEVEL_D0);
			if (rval == DDI_SUCCESS) {
				mutex_enter(&mpt->m_mutex);
			} else {
				/*
				 * The pm_raise_power() call above failed,
				 * and that can only occur if we were unable
				 * to reset the hardware.  This is probably
				 * due to unhealty hardware, and because
				 * important filesystems(such as the root
				 * filesystem) could be on the attached disks,
				 * it would not be a good idea to continue,
				 * as we won't be entirely certain we are
				 * writing correct data.  So we panic() here
				 * to not only prevent possible data corruption,
				 * but to give developers or end users a hope
				 * of identifying and correcting any problems.
				 */
				fm_panic("mpt could not reset hardware "
				    "during resume");
			}
		}

		mpt->m_suspended = 0;

		/*
		 * Reinitialize ioc
		 */
		if (mpt_init_chip(mpt, FALSE)) {
			mutex_exit(&mpt->m_mutex);
			if (mpt->m_options & MPT_OPT_PM) {
				(void) pm_idle_component(dip, 0);
			}
			fm_panic("mpt init chip fail during resume");
		}

		MPT_ENABLE_INTR(mpt);

		/* start requests, if possible */
		mpt_restart_hba(mpt);

		mutex_exit(&mpt->m_mutex);

		/* report idle status to pm framework */
		if (mpt->m_options & MPT_OPT_PM) {
			(void) pm_idle_component(dip, 0);
		}

		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);

	}

	instance = ddi_get_instance(dip);

	/*
	 * Allocate softc information.
	 */
	if (ddi_soft_state_zalloc(mpt_state, instance) != DDI_SUCCESS) {
		mpt_log(NULL, CE_WARN,
		    "mpt%d: cannot allocate soft state", instance);
		cmn_err(CE_WARN, "!ID[SUNWpd.mpt.attach.4002]");
		goto fail;
	}

	mpt = ddi_get_soft_state(mpt_state, instance);

	if (mpt == NULL) {
		mpt_log(NULL, CE_WARN,
		    "mpt%d: cannot get soft state", instance);
		cmn_err(CE_WARN, "!ID[SUNWpd.mpt.attach.4003]");
		goto fail;
	}

	/* Allocate a transport structure */
	hba_tran = mpt->m_tran = scsi_hba_tran_alloc(dip, SCSI_HBA_CANSLEEP);
	ASSERT(mpt->m_tran != NULL);

	/* Indicate that we are 'sizeof (scsi_*(9S))' clean. */
	scsi_size_clean(dip);

	mpt->m_dip = dip;
	mpt->m_instance = instance;

	/* Make a per-instance copy of the structures */
	mpt->m_io_dma_attr = mpt_dma_attrs64;
	mpt->m_msg_dma_attr = mpt_dma_attrs;
	mpt->m_dev_acc_attr = dev_attr;
	mpt->m_reg_acc_attr = dev_attr;

	/*
	 * set host ID
	 */
	mpt->m_mptid = MPT_INVALID_HOSTID;
	id = ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0, "initiator-id",
	    MPT_INVALID_HOSTID);
	if (id == MPT_INVALID_HOSTID) {
		id = ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0,
		    "scsi-initiator-id", MPT_INVALID_HOSTID);
	}

	if (id >= 0 && id < NTARGETS_WIDE) {
		mpt_log(mpt, CE_NOTE, "?initiator SCSI ID now %d\n", id);
		mpt->m_mptid = id;
	}

	/*
	 * Initialize FMA
	 */
	mpt->m_fm_capabilities = ddi_getprop(DDI_DEV_T_ANY, mpt->m_dip,
	    DDI_PROP_CANSLEEP | DDI_PROP_DONTPASS, "fm-capable",
	    DDI_FM_EREPORT_CAPABLE | DDI_FM_ACCCHK_CAPABLE |
	    DDI_FM_DMACHK_CAPABLE | DDI_FM_ERRCB_CAPABLE);

	mpt_fm_init(mpt);

	/*
	 * set mpt_offline_delay
	 */
	mpt->m_offline_delay = ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0,
	    OFFLINE_DELAY_PROP,
	    MPT_DEFAULT_OFFLINE_DELAY);

	/*
	 * FOR DEBUG USE ONLY
	 * mpt_quiesce_delay property is used to determine whether to
	 * quiesce SCSI bus before probing specified target ID or entire
	 * SCSI bus. Below is an explanation of the supported settings
	 * for mpt_quiesce_delay property:
	 *
	 * 1. When mpt_quiesce_delay == -1, mpt_scsi_bus_config() will not
	 *    quiesce SCSI bus before probing.  This is the default behavior.
	 *
	 * 2. When mpt_quiesce_delay == 0, mpt_scsi_bus_config() will wait
	 *    until condition is met (quiesce succeeded) or prematurely
	 *    signaled for other reasons (e.g. kill(2)) before proceeding
	 *    with SCSI bus or target probe.
	 *
	 * 3. When mpt_quiesce_delay > 0, mpt_scsi_bus_config() will wait
	 *    until condition is met (quiesce succeeded), mpt_quiesce_delay
	 *    (seconds) expires, or condition is prematurely signaled for
	 *    other reasons (e.g. kill(2)) before proceeding with SCSI bus
	 *    or target probe - which ever comes first.
	 *
	 * Note that mpt_scsi_bus_config() will proceed whether quiesce
	 * succeeded or not.
	 *
	 * This property can and should be set per mpt instance.  This
	 * property should not be set for mpt instance containing root
	 * OS device.  Below is an example of how to set mpt_quiesce_delay
	 * to 5 seconds for a mpt instance in PCIE1 slot on a SunFire T2000:
	 *
	 * name="mpt" parent="/pci@7c0/pci@0/pci@8/pci@0"
	 *	unit-address="8"
	 *	mpt_quiesce_delay=5;
	 */
	mpt->m_quiesce_delay_timebase = ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0,
	    QUIESCE_DELAY_PROP, -1);

	/*
	 * FOR DEBUG USE ONLY
	 * mpt_cache_probe property is used to prevent probing of SCSI targets
	 * that weren't discovered during initial SCSI bus probe.
	 * mpt_cache_probe can be set per mpt instance.  When enabled, mpt will
	 * not probe new targets until mpt instance is reattached (e.g. reboot).
	 */
	if (ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0,
	    "mpt_cache_probe", 0) == 1) {
		mpt->m_cache_probe |= MPT_CACHE_PROBE_ENABLED;
	}

	if (pci_config_setup(mpt->m_dip,
	    &mpt->m_config_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "cannot map configuration space.");
		goto fail;
	}
	config_setup++;

	if (mpt_alloc_handshake_msg(mpt, sizeof (struct msg_scsi_task_mgmt))
	    == DDI_FAILURE) {
		mpt_log(mpt, CE_WARN, "cannot initialize handshake msg.");
		goto fail;
	}

	/*
	 * This is a workaround for a XMITS ASIC bug which does not
	 * drive the CBE upper bits.
	 */
	if (pci_config_get16(mpt->m_config_handle, PCI_CONF_STAT) &
	    PCI_STAT_PERROR) {
		pci_config_put16(mpt->m_config_handle, PCI_CONF_STAT,
		    PCI_STAT_PERROR);
	}

	/*
	 * Setup configuration space
	 */
	if (mpt_config_space_init(mpt) == FALSE) {
		mpt_log(mpt, CE_WARN, "mpt_config_space_init failed");
		goto fail;
	}

	/*
	 * The devid and revid were fetched in mpt_config_space_init() so we
	 * can use them to determine if we need to impose the DMA restrictions
	 * to satisfy LSI Errata 1064E/1068E B1 (revid 0x02) #17 (don't cross
	 * a 4GB boundary) and LSI Erratum 1064E/1068E B2 (revid 0x04) #15 and
	 * B3 (revid 0x08) #14 (don't span or end on 4GB boundary).
	 *
	 * For SPARC, we have an IOMMU, so we won't have a performance
	 * penalty for restricting DMAs to < 4G. But for x86, we will get
	 * a large performance penalty if you require a copy/bounce buffer
	 * for every DMA which has a page that lives > 4G. So use a private
	 * interface to only bounce if we a buffer which straddles the 4G
	 * boundary for rev 3-8.
	 */
	if ((mpt->m_devid == MPT_1064E) || (mpt->m_devid == MPT_1068E)) {
		if (mpt->m_revid < 2) {
			mpt->m_io_dma_attr.dma_attr_addr_hi = 0xffffffffull;
			mpt_log(mpt, CE_NOTE, "!DMA restricted to lower "
			    "4GB due to errata");
		} else if (mpt->m_revid <= 8) {
#ifdef	__sparc
			mpt->m_io_dma_attr.dma_attr_addr_hi = 0xfffffffeull;
			mpt_log(mpt, CE_NOTE, "!DMA restricted below 4GB "
			    "boundary due to errata");
#else
			mpt->m_io_dma_attr.dma_attr_seg = 0xffffefffull;
			mpt->m_io_dma_attr.dma_attr_flags |=
			    _DDI_DMA_BOUNCE_ON_SEG;
			mpt_log(mpt, CE_NOTE, "!DMA can't cross 4GB "
			    "boundary due to errata");
#endif
		}
	}

	/*
	 * Map the operating register.
	 */
	if (mpt->m_devid == MPT_1078IR) {
		mem_bar = MEM_SPACE_1078IR;
	}
	if (ddi_regs_map_setup(dip, mem_bar, (caddr_t *)&mpt->m_reg,
	    0, 0, &mpt->m_reg_acc_attr, &mpt->m_datap) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "map setup failed");
		cmn_err(CE_WARN, "!ID[SUNWpd.mpt.attach.4005]");
		goto fail;
	}
	map_setup++;

	/*
	 * A taskq is created for dealing with the event handler
	 */
	if ((mpt->m_event_taskq = ddi_taskq_create(dip, "mpt_event_taskq",
	    1, TASKQ_DEFAULTPRI, 0)) == NULL) {
		mpt_log(mpt, CE_NOTE, "ddi_taskq_create failed");
		goto fail;
	}
	ev_taskq_create++;

	/*
	 * A taskq is created for dealing with dr events
	 */
	if ((mpt->m_dr_taskq = ddi_taskq_create(dip,
	    "mpt_dr_taskq",
	    1, TASKQ_DEFAULTPRI, 0)) == NULL) {
		mpt_log(mpt, CE_NOTE, "ddi_taskq_create for discovery failed");
		goto fail;
	}
	dr_taskq_create++;

	mpt->m_doneq_thread_threshold = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    0, "mpt_doneq_thread_threshold_prop", 10);
	mpt->m_doneq_length_threshold = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    0, "mpt_doneq_length_threshold_prop", 8);
	mpt->m_doneq_thread_n = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    0, "mpt_doneq_thread_n_prop", 8);

	if (mpt->m_doneq_thread_n) {
		cv_init(&mpt->m_doneq_thread_cv, NULL, CV_DRIVER, NULL);
		mutex_init(&mpt->m_doneq_mutex, NULL, MUTEX_DRIVER, NULL);

		mutex_enter(&mpt->m_doneq_mutex);
		mpt->m_doneq_thread_id =
		    kmem_zalloc(sizeof (mpt_doneq_thread_list_t)
		    * mpt->m_doneq_thread_n, KM_SLEEP);

		for (j = 0; j < mpt->m_doneq_thread_n; j++) {
			cv_init(&mpt->m_doneq_thread_id[j].cv, NULL,
			    CV_DRIVER, NULL);
			mutex_init(&mpt->m_doneq_thread_id[j].mutex, NULL,
			    MUTEX_DRIVER, NULL);
			mutex_enter(&mpt->m_doneq_thread_id[j].mutex);
			mpt->m_doneq_thread_id[j].flag |=
			    MPT_DONEQ_THREAD_ACTIVE;
			mpt->m_doneq_thread_id[j].arg.mpt = mpt;
			mpt->m_doneq_thread_id[j].arg.t = j;
			mpt->m_doneq_thread_id[j].threadp =
			    thread_create(NULL, 0, mpt_doneq_thread,
			    &mpt->m_doneq_thread_id[j].arg,
			    0, &p0, TS_RUN, minclsyspri);
			mpt->m_doneq_thread_id[j].donetail =
			    &mpt->m_doneq_thread_id[j].doneq;
			mutex_exit(&mpt->m_doneq_thread_id[j].mutex);
		}
		mutex_exit(&mpt->m_doneq_mutex);
		doneq_thread_create++;
	}

	/* Get supported interrupt types */
	if (ddi_intr_get_supported_types(dip, &intr_types) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "ddi_intr_get_supported_types failed\n");
		cmn_err(CE_WARN, "!ID[SUNWpd.mpt.attach.4008]");
		goto fail;
	}

	NDBG6(("ddi_intr_get_supported_types() returned: 0x%x", intr_types));

	if ((intr_types & DDI_INTR_TYPE_MSI) && mpt_enable_msi) {
		/*
		 * Try MSI first, but fall back to FIXED if MSI attach fails
		 */
		if (mpt_add_intrs(mpt, DDI_INTR_TYPE_MSI) == DDI_SUCCESS) {
			NDBG0(("Using MSI interrupt type"));
			mpt->m_intr_type = DDI_INTR_TYPE_MSI;

			goto intr_done;
		}

		NDBG0(("MSI registration failed, trying FIXED interrupts"));
	}

	if (intr_types & DDI_INTR_TYPE_FIXED) {

		if (mpt_add_intrs(mpt, DDI_INTR_TYPE_FIXED) == DDI_SUCCESS) {
			NDBG0(("Using FIXED interrupt type"));
			mpt->m_intr_type = DDI_INTR_TYPE_FIXED;

			goto intr_done;
		}

		NDBG0(("FIXED interrupt registration failed"));
	}

	cmn_err(CE_WARN, "!ID[SUNWpd.mpt.attach.4009]");
	goto fail;

intr_done:
	intr_added++;

	/* Initialize mutex used in interrupt handler */
	mutex_init(&mpt->m_mutex, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(mpt->m_intr_pri));
	mutex_init(&mpt->m_passthru_mutex, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&mpt->m_waitq_mutex, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(mpt->m_intr_pri));
	cv_init(&mpt->m_cv, NULL, CV_DRIVER, NULL);
	cv_init(&mpt->m_passthru_cv, NULL, CV_DRIVER, NULL);
	mutex_init_done++;

	/*
	 * Initialize power management component
	 */
	if (mpt->m_options & MPT_OPT_PM) {
		if (mpt_init_pm(mpt)) {
			mpt_log(mpt, CE_WARN, "mpt pm initialization failed");
			goto fail;
		}

		/*
		 * Enable I/O access
		 * mpt_init_pm() eventually results in a call to
		 * mpt_setup_cmd_reg() via MPT_POWER_ON() which disables
		 * r/w access to command register.  Access to comand
		 * register below requires this bit be set.
		 */
		comm = pci_config_get16(mpt->m_config_handle, PCI_CONF_COMM);
		comm |= PCI_COMM_IO;
		pci_config_put16(mpt->m_config_handle, PCI_CONF_COMM, comm);
	}

	/*
	 * Disable hardware interrupt since we're not ready to
	 * handle it yet.
	 */
	MPT_DISABLE_INTR(mpt);

	/*
	 * Enable interrupts
	 */
	if (mpt->m_intr_cap & DDI_INTR_FLAG_BLOCK) {
		/* Call ddi_intr_block_enable() for MSI interrupts */
		(void) ddi_intr_block_enable(mpt->m_htable, mpt->m_intr_cnt);
	} else {
		/* Call ddi_intr_enable for MSI or FIXED interrupts */
		for (i = 0; i < mpt->m_intr_cnt; i++) {
			(void) ddi_intr_enable(mpt->m_htable[i]);
		}
	}

	/*
	 * Initialize chip
	 */
	mutex_enter(&mpt->m_mutex);
	if (mpt_init_chip(mpt, TRUE)) {
		mutex_exit(&mpt->m_mutex);
		mpt_log(mpt, CE_WARN, "mpt chip initialization failed");
		goto fail;
	}
	mutex_exit(&mpt->m_mutex);

	/*
	 * initialize SCSI HBA transport structure
	 */
	hba_tran->tran_hba_private	= mpt;
	hba_tran->tran_tgt_private	= NULL;

	hba_tran->tran_tgt_init		= mpt_scsi_tgt_init;
	hba_tran->tran_tgt_free		= mpt_scsi_tgt_free;

	hba_tran->tran_start		= mpt_scsi_start;
	hba_tran->tran_reset		= mpt_scsi_reset;
	hba_tran->tran_abort		= mpt_scsi_abort;
	hba_tran->tran_getcap		= mpt_scsi_getcap;
	hba_tran->tran_setcap		= mpt_scsi_setcap;

	hba_tran->tran_setup_pkt	= mpt_tran_setup_pkt;
	hba_tran->tran_teardown_pkt	= mpt_tran_teardown_pkt;
	hba_tran->tran_pkt_constructor	= mpt_tran_pkt_constructor;
	hba_tran->tran_pkt_destructor	= mpt_tran_pkt_destructor;

	hba_tran->tran_reset_notify	= mpt_scsi_reset_notify;
	hba_tran->tran_get_name		= mpt_scsi_get_name;
	hba_tran->tran_get_bus_addr	= mpt_scsi_get_bus_addr;

	hba_tran->tran_quiesce		= mpt_scsi_quiesce;
	hba_tran->tran_unquiesce	= mpt_scsi_unquiesce;
	hba_tran->tran_bus_reset	= NULL;

	hba_tran->tran_add_eventcall	= NULL;
	hba_tran->tran_get_eventcookie	= NULL;
	hba_tran->tran_post_event	= NULL;
	hba_tran->tran_remove_eventcall	= NULL;
	hba_tran->tran_hba_len		= sizeof (mpt_cmd_t);

	if (MPT_IS_SAS(mpt)) {
		hba_tran->tran_bus_config	= mpt_sas_bus_config;
		hba_tran->tran_interconnect_type = INTERCONNECT_SATA;
	} else {
		hba_tran->tran_bus_config	= mpt_scsi_bus_config;
		hba_tran->tran_interconnect_type = INTERCONNECT_PARALLEL;
	}
	if (scsi_hba_attach_setup(dip, &mpt->m_io_dma_attr,
	    hba_tran, SCSI_HBA_TRAN_CLONE) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "hba attach setup failed");
		goto fail;
	}
	hba_attach_setup++;

	if (MPT_IS_SAS(mpt)) {
		mpt->m_smptran = smp_hba_tran_alloc(dip);
		ASSERT(mpt->m_smptran != NULL);
		mpt->m_smptran->smp_tran_hba_private = mpt;
		mpt->m_smptran->smp_tran_start = mpt_smp_start;
		if (smp_hba_attach_setup(dip, mpt->m_smptran) != DDI_SUCCESS) {
			mpt_log(mpt, CE_WARN, "smp attach setup failed");
			goto fail;
		}
	}

	if (mpt_alloc_active_slots(mpt, KM_SLEEP)) {
		goto fail;
	}

	/*
	 * Search IOC for raid devices
	 */
	if (mpt_get_raid_info(mpt) == DDI_FAILURE) {
		mpt_log(mpt, CE_WARN, "unable to obtain raid information");
		goto fail;
	}

	/*
	 * disable tag-qing for all scsi targets
	 * (will be enabled by target driver if required)
	 */
	if (MPT_IS_SCSI(mpt)) {
		mpt->m_notag = ALL_TARGETS;
	}
	if (MPT_IS_SAS(mpt)) {
		/*
		 * register sas hba with mdi (MPxIO/vhci)
		 */
		if (mdi_phci_register(MDI_HCI_CLASS_SCSI,
		    dip, 0) == MDI_SUCCESS) {
			/*
			 * NOTE: Since we called mdi_phci_register() after
			 * scsi_hba_attach_setup(), we are responsible for
			 * calling mdi_devi_set_dma_attr().
			 */
			mdi_devi_set_dma_attr(dip, &hba_tran->tran_dma_attr);

			phci_register++;
			mpt->m_mpxio_enable = TRUE;
		}
		mpt->m_sata_mpxio_enable = TRUE;
		if ((ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, 0,
		    "disable-sata-mpxio", &prop) == DDI_SUCCESS)) {
			if (strcmp(prop, "yes") == 0) {
				mpt->m_sata_mpxio_enable = FALSE;
			}
			ddi_prop_free(prop);
		}
	}

	/*
	 * Get scsi_options if they exist
	 */
	mpt->m_scsi_options = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, 0, "scsi-options", DEFAULT_SCSI_OPTIONS);

	/*
	 * Setup config pages for each target
	 */
	if (MPT_IS_SCSI(mpt)) {
		if (mpt_get_scsi_port_params(mpt, 0,
		    &mpt->m_scsi_params) == -1) {
			mpt_log(mpt, CE_WARN, "get scsi port params failed");
			goto fail;
		}
		mpt->m_nowide = ALL_TARGETS;

		for (i = 0; i < NTARGETS_WIDE; i++) {

			/*
			 * if target<n>-scsi-options property exists, use it;
			 * otherwise use the m_scsi_options
			 */
			(void) sprintf(prop_str, prop_template, i);
			mpt->m_target_scsi_options[i] = ddi_prop_get_int(
			    DDI_DEV_T_ANY, dip, 0, prop_str, -1);

			if (mpt->m_target_scsi_options[i] != -1) {
				mpt_log(mpt, CE_NOTE,
				    "?target%x-scsi-options=0x%x\n",
				    i, mpt->m_target_scsi_options[i]);
				mpt->m_target_scsi_options_defined |= (1 << i);
			} else {
				mpt->m_target_scsi_options[i] =
				    mpt->m_scsi_options;
			}
			if (((mpt->m_target_scsi_options[i] &
			    SCSI_OPTIONS_DR) == 0) &&
			    (mpt->m_target_scsi_options[i] &
			    SCSI_OPTIONS_TAG)) {
				mpt->m_target_scsi_options[i] &=
				    ~SCSI_OPTIONS_TAG;
				mpt_log(mpt, CE_WARN,
				    "Target %d: disabled TQ since disconnects "
				    "are disabled", i);
			}
			/*
			 * by default, we support 32 LUNS per target.
			 */
			mpt->m_max_lun[i] = NLUNS_MPT;

			/*
			 * write the scsi options to the chip
			 */
			mpt_set_scsi_options(mpt, i,
			    mpt->m_target_scsi_options[i]);
		}
	}

	mpt->m_scsi_reset_delay	= ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, 0, "scsi-reset-delay",	SCSI_DEFAULT_RESET_DELAY);
	if (mpt->m_scsi_reset_delay == 0) {
		mpt_log(mpt, CE_NOTE,
		    "scsi_reset_delay of 0 is not recommended,"
		    " resetting to SCSI_DEFAULT_RESET_DELAY\n");
		mpt->m_scsi_reset_delay = SCSI_DEFAULT_RESET_DELAY;
	}

	/*
	 * Initialize the wait and done FIFO queue
	 */
	mpt->m_donetail = &mpt->m_doneq;
	mpt->m_waitqtail = &mpt->m_waitq;
	mpt->m_tx_waitqtail = &mpt->m_tx_waitq;
	mpt->m_tx_draining = 0;

	/*
	 * ioc cmd queue initialize
	 */
	mpt->m_ioc_event_cmdtail = &mpt->m_ioc_event_cmdq;

	/*
	 * For SCSI, issue a bus reset before getting started.	The bus reset
	 * code will also enable interrupts.
	 * For SAS, skip the bus reset and just enable interrupts.
	 */

	if (MPT_IS_SCSI(mpt)) {
		mutex_enter(&mpt->m_mutex);
		(void) mpt_ioc_task_management(mpt,
		    MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS, 0, 0, 0);
		mutex_exit(&mpt->m_mutex);
	} else {
		mpt->m_dev_handle = 0xFFFF;
		bzero(initiator_wwnstr, sizeof (initiator_wwnstr));
		(void) sprintf(initiator_wwnstr, "%016"PRIx64,
		    mpt->un.m_base_wwid);
		if (ddi_prop_update_string(DDI_DEV_T_NONE, dip,
		    SCSI_ADDR_PROP_INITIATOR_PORT, initiator_wwnstr) !=
		    DDI_PROP_SUCCESS) {
			goto fail;
		}
		if (ddi_prop_update_string(DDI_DEV_T_NONE, dip,
		    "initiator-interconnect-type", "SAS") !=
		    DDI_PROP_SUCCESS) {
			goto fail;
		}
		if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "device_type", &device_type) ==
		    DDI_SUCCESS) {
			if (strncmp(device_type, "scsi-sas", 8) == 0) {
				mpt->m_wwid_obp = 1;
			}
			ddi_prop_free(device_type);
		}
		MPT_ENABLE_INTR(mpt);
	}

	/*
	 * enable event notification
	 */
	mutex_enter(&mpt->m_mutex);
	if (mpt_ioc_enable_event_notification(mpt)) {
		mutex_exit(&mpt->m_mutex);
		goto fail;
	}
	mutex_exit(&mpt->m_mutex);

	/* Check all dma handles allocated in attach */
	if ((mpt_check_dma_handle(mpt->m_dma_hdl) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(mpt->m_reply_dma_h) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(mpt->m_hshk_dma_hdl) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_LOST);
		goto fail;
	}

	/* Check all acc handles allocated in attach */
	if ((mpt_check_acc_handle(mpt->m_datap) != DDI_SUCCESS) ||
	    (mpt_check_acc_handle(mpt->m_config_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_LOST);
		goto fail;
	}

	/*
	 * After this point, we are not going to fail the attach.
	 */

	for (i = 0; i < MPT_MAX_TARGETS; i++) {
		mpt_set_throttle(mpt, i, MAX_THROTTLE);
	}

	/*
	 * used for mpt_watch
	 */
	rw_enter(&mpt_global_rwlock, RW_WRITER);
	if (mpt_head == NULL) {
		mpt_head = mpt;
	} else {
		mpt_tail->m_next = mpt;
	}
	mpt_tail = mpt;
	rw_exit(&mpt_global_rwlock);

	mutex_enter(&mpt_global_mutex);
	if (mpt_scsi_watchdog_tick == 0) {
		mpt_scsi_watchdog_tick = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dip, 0, "scsi-watchdog-tick", DEFAULT_WD_TICK);

		mpt_tick = mpt_scsi_watchdog_tick *
		    drv_usectohz((clock_t)1000000);

		mpt_timeout_id = timeout(mpt_watch, NULL, mpt_tick);
		mpt_timeouts_enabled = 1;
	}
	mutex_exit(&mpt_global_mutex);

	/* Print message of HBA present */
	ddi_report_dev(dip);

	/* report idle status to pm framework */
	if (mpt->m_options & MPT_OPT_PM) {
		(void) pm_idle_component(dip, 0);
	}

	return (DDI_SUCCESS);

fail:
	mpt_log(mpt, CE_WARN, "attach failed");
	cmn_err(CE_WARN, "!ID[SUNWpd.mpt.attach.4011]");
	if (mpt) {
		if (phci_register) {
			(void) mdi_phci_unregister(mpt->m_dip, 0);
		}
		if (hba_attach_setup) {
			(void) scsi_hba_detach(dip);
		}
		if (intr_added) {
			mpt_rem_intrs(mpt);
		}

		if (doneq_thread_create) {
			mutex_enter(&mpt->m_doneq_mutex);
			doneq_thread_num = mpt->m_doneq_thread_n;
			for (j = 0; j < mpt->m_doneq_thread_n; j++) {
				mutex_enter(&mpt->m_doneq_thread_id[j].mutex);
				mpt->m_doneq_thread_id[j].flag &=
				    (~MPT_DONEQ_THREAD_ACTIVE);
				cv_signal(&mpt->m_doneq_thread_id[j].cv);
				mutex_exit(&mpt->m_doneq_thread_id[j].mutex);
			}
			while (mpt->m_doneq_thread_n) {
				cv_wait(&mpt->m_doneq_thread_cv,
				    &mpt->m_doneq_mutex);
			}
			for (j = 0; j < doneq_thread_num; j++) {
				cv_destroy(&mpt->m_doneq_thread_id[j].cv);
				mutex_destroy(&mpt->m_doneq_thread_id[j].mutex);
			}
			kmem_free(mpt->m_doneq_thread_id,
			    sizeof (mpt_doneq_thread_list_t)
			    * doneq_thread_num);
			mutex_exit(&mpt->m_doneq_mutex);
			cv_destroy(&mpt->m_doneq_thread_cv);
			mutex_destroy(&mpt->m_doneq_mutex);
		}
		if (ev_taskq_create) {
			ddi_taskq_destroy(mpt->m_event_taskq);
		}
		if (dr_taskq_create) {
			ddi_taskq_destroy(mpt->m_dr_taskq);
		}
		if (mutex_init_done) {
			mutex_destroy(&mpt->m_mutex);
			mutex_destroy(&mpt->m_passthru_mutex);
			mutex_destroy(&mpt->m_waitq_mutex);
			cv_destroy(&mpt->m_cv);
			cv_destroy(&mpt->m_passthru_cv);
		}
		mpt_free_handshake_msg(mpt);
		mpt_hba_fini(mpt);
		if (map_setup) {
			mpt_cfg_fini(mpt);
		}
		if (config_setup) {
			pci_config_teardown(&mpt->m_config_handle);
		}
		if (mpt->m_tran) {
			scsi_hba_tran_free(mpt->m_tran);
		}
		if (mpt->m_smptran) {
			smp_hba_tran_free(mpt->m_smptran);
		}
		mpt_fm_fini(mpt);
		ddi_soft_state_free(mpt_state, instance);
		ddi_prop_remove_all(dip);
	}
	return (DDI_FAILURE);
}

static int
mpt_suspend(dev_info_t *devi)
{
	mpt_t	*mpt, *g;
	scsi_hba_tran_t *tran;

	if ((tran = ddi_get_driver_private(devi)) == NULL)
		return (DDI_SUCCESS);

	mpt = TRAN2MPT(tran);
	if (!mpt) {
		return (DDI_SUCCESS);
	}

	mutex_enter(&mpt->m_mutex);

	if (mpt->m_suspended++) {
		mutex_exit(&mpt->m_mutex);
		return (DDI_SUCCESS);
	}

	/*
	 * Cancel timeout threads for this mpt
	 */
	if (mpt->m_quiesce_timeid) {
		timeout_id_t tid = mpt->m_quiesce_timeid;
		mpt->m_quiesce_timeid = 0;
		mutex_exit(&mpt->m_mutex);
		(void) untimeout(tid);
		mutex_enter(&mpt->m_mutex);
	}

	if (mpt->m_restart_cmd_timeid) {
		timeout_id_t tid = mpt->m_restart_cmd_timeid;
		mpt->m_restart_cmd_timeid = 0;
		mutex_exit(&mpt->m_mutex);
		(void) untimeout(tid);
		mutex_enter(&mpt->m_mutex);
	}

	if (mpt->m_pm_timeid != 0) {
		timeout_id_t tid = mpt->m_pm_timeid;
		mpt->m_pm_timeid = 0;
		mutex_exit(&mpt->m_mutex);
		(void) untimeout(tid);
		/*
		 * Report idle status for last ioctl since
		 * calls to pm_busy_component(9F) are stacked.
		 */
		(void) pm_idle_component(mpt->m_dip, 0);
		mutex_enter(&mpt->m_mutex);
	}

	mutex_exit(&mpt->m_mutex);


	/*
	 * Cancel watch threads if all mpts suspended
	 */
	rw_enter(&mpt_global_rwlock, RW_WRITER);
	for (g = mpt_head; g != NULL; g = g->m_next) {
		if (!g->m_suspended)
			break;
	}
	rw_exit(&mpt_global_rwlock);

	mutex_enter(&mpt_global_mutex);
	if (g == NULL) {
		timeout_id_t tid;

		mpt_timeouts_enabled = 0;
		if (mpt_timeout_id) {
			tid = mpt_timeout_id;
			mpt_timeout_id = 0;
			mutex_exit(&mpt_global_mutex);
			(void) untimeout(tid);
			mutex_enter(&mpt_global_mutex);
		}
		if (mpt_reset_watch) {
			tid = mpt_reset_watch;
			mpt_reset_watch = 0;
			mutex_exit(&mpt_global_mutex);
			(void) untimeout(tid);
			mutex_enter(&mpt_global_mutex);
		}
	}
	mutex_exit(&mpt_global_mutex);

	mutex_enter(&mpt->m_mutex);

	/*
	 * If this mpt is not in full power(PM_LEVEL_D0), just return.
	 */
	if ((mpt->m_options & MPT_OPT_PM) &&
	    (mpt->m_power_level != PM_LEVEL_D0)) {
		mutex_exit(&mpt->m_mutex);
		return (DDI_SUCCESS);
	}

	/* Disable HBA interrupts in hardware */
	MPT_DISABLE_INTR(mpt);

	mutex_exit(&mpt->m_mutex);

	/* drain the taskq */
	ddi_taskq_wait(mpt->m_event_taskq);
	ddi_taskq_wait(mpt->m_dr_taskq);

	return (DDI_SUCCESS);

}

/*
 * quiesce(9E) entry point.
 *
 * This function is called when the system is single-threaded at high
 * PIL with preemption disabled. Therefore, this function must not be
 * blocked.
 *
 * This function returns DDI_SUCCESS on success, or DDI_FAILURE on failure.
 * DDI_FAILURE indicates an error condition and should almost never happen.
 */
#ifndef	__sparc
static int
mpt_quiesce(dev_info_t *devi)
{
	mpt_t	*mpt;
	scsi_hba_tran_t *tran;

	if ((tran = ddi_get_driver_private(devi)) == NULL)
		return (DDI_SUCCESS);

	if ((mpt = TRAN2MPT(tran)) == NULL)
		return (DDI_SUCCESS);

	/* Disable HBA interrupts in hardware */
	MPT_DISABLE_INTR(mpt);

	return (DDI_SUCCESS);
}
#endif	/* __sparc */

/*
 * detach(9E).	Remove all device allocations and system resources;
 * disable device interrupts.
 * Return DDI_SUCCESS if done; DDI_FAILURE if there's a problem.
 */
static int
mpt_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);
	NDBG0(("mpt_detach: dip=0x%p cmd=0x%p", (void *)devi, (void *)cmd));

	switch (cmd) {
	case DDI_DETACH:
		return (mpt_dr_detach(devi));

	case DDI_SUSPEND:
		return (mpt_suspend(devi));

	default:
		return (DDI_FAILURE);
	}
	/* NOTREACHED */
}


static int
mpt_dr_detach(dev_info_t *dip)
{
	mpt_t *mpt, *m;
	scsi_hba_tran_t *tran;
	mpt_slots_t *active;
	int circ = 0;
	int circ1 = 0;
	mdi_pathinfo_t *pip = NULL;
	int j;
	int doneq_thread_num = 0;

	NDBG0(("mpt_dr_detach: dip=0x%p", (void *)dip));

	if ((tran = ddi_get_driver_private(dip)) == NULL)
		return (DDI_FAILURE);

	mpt = TRAN2MPT(tran);
	if (!mpt) {
		return (DDI_FAILURE);
	}
	/*
	 * Still have pathinfo child, should not detach mpt driver
	 */
	if (mpt->m_mpxio_enable) {
		ndi_devi_enter(scsi_vhci_dip, &circ1);
		ndi_devi_enter(mpt->m_dip, &circ);
		while (pip = mdi_get_next_client_path(mpt->m_dip, NULL)) {
			if (mdi_pi_free(pip, 0) == MDI_SUCCESS) {
				continue;
			}
			ndi_devi_exit(mpt->m_dip, circ);
			ndi_devi_exit(scsi_vhci_dip, circ1);
			NDBG12(("mpt%d: detach failed."
			    "mpt%d has outstanding path info",
			    mpt->m_instance, mpt->m_instance));
			return (DDI_FAILURE);
		}
		ndi_devi_exit(mpt->m_dip, circ);
		ndi_devi_exit(scsi_vhci_dip, circ1);
	}

	/* Make sure power level is D0 before accessing registers */
	if (mpt->m_options & MPT_OPT_PM) {
		(void) pm_busy_component(dip, 0);
		if (mpt->m_power_level != PM_LEVEL_D0) {
			if (pm_raise_power(dip, 0, PM_LEVEL_D0) !=
			    DDI_SUCCESS) {
				mpt_log(mpt, CE_WARN,
				    "mpt%d: Raise power request failed.",
				    mpt->m_instance);
				(void) pm_idle_component(dip, 0);
				return (DDI_FAILURE);
			}
		}
	}

	mutex_enter(&mpt->m_mutex);
	MPT_DISABLE_INTR(mpt);
	mutex_exit(&mpt->m_mutex);
	mpt_rem_intrs(mpt);

	ddi_taskq_destroy(mpt->m_event_taskq);
	ddi_taskq_destroy(mpt->m_dr_taskq);

	if (mpt->m_doneq_thread_n) {
		mutex_enter(&mpt->m_doneq_mutex);
		doneq_thread_num = mpt->m_doneq_thread_n;
		for (j = 0; j < mpt->m_doneq_thread_n; j++) {
			mutex_enter(&mpt->m_doneq_thread_id[j].mutex);
			mpt->m_doneq_thread_id[j].flag &=
			    (~MPT_DONEQ_THREAD_ACTIVE);
			cv_signal(&mpt->m_doneq_thread_id[j].cv);
			mutex_exit(&mpt->m_doneq_thread_id[j].mutex);
		}
		while (mpt->m_doneq_thread_n) {
			cv_wait(&mpt->m_doneq_thread_cv,
			    &mpt->m_doneq_mutex);
		}
		for (j = 0;  j < doneq_thread_num; j++) {
			cv_destroy(&mpt->m_doneq_thread_id[j].cv);
			mutex_destroy(&mpt->m_doneq_thread_id[j].mutex);
		}
		kmem_free(mpt->m_doneq_thread_id,
		    sizeof (mpt_doneq_thread_list_t)
		    * doneq_thread_num);
		mutex_exit(&mpt->m_doneq_mutex);
		cv_destroy(&mpt->m_doneq_thread_cv);
		mutex_destroy(&mpt->m_doneq_mutex);
	}

	scsi_hba_reset_notify_tear_down(mpt->m_reset_notify_listf);

	/*
	 * Remove device instance from the global linked list
	 */
	rw_enter(&mpt_global_rwlock, RW_WRITER);
	if (mpt_head == mpt) {
		m = mpt_head = mpt->m_next;
	} else {
		for (m = mpt_head; m != NULL; m = m->m_next) {
			if (m->m_next == mpt) {
				m->m_next = mpt->m_next;
				break;
			}
		}
		if (m == NULL) {
			cmn_err(CE_WARN, "!ID[SUNWpd.mpt.detach.4012]");
			mpt_log(mpt, CE_PANIC, "Not in softc list!");
		}
	}

	if (mpt_tail == mpt) {
		mpt_tail = m;
	}
	rw_exit(&mpt_global_rwlock);

	/*
	 * Cancel timeout threads for this mpt
	 */
	mutex_enter(&mpt->m_mutex);
	if (mpt->m_quiesce_timeid) {
		timeout_id_t tid = mpt->m_quiesce_timeid;
		mpt->m_quiesce_timeid = 0;
		mutex_exit(&mpt->m_mutex);
		(void) untimeout(tid);
		mutex_enter(&mpt->m_mutex);
	}

	if (mpt->m_restart_cmd_timeid) {
		timeout_id_t tid = mpt->m_restart_cmd_timeid;
		mpt->m_restart_cmd_timeid = 0;
		mutex_exit(&mpt->m_mutex);
		(void) untimeout(tid);
		mutex_enter(&mpt->m_mutex);
	}

	if (mpt->m_pm_timeid != 0) {
		timeout_id_t tid = mpt->m_pm_timeid;
		mpt->m_pm_timeid = 0;
		mutex_exit(&mpt->m_mutex);
		(void) untimeout(tid);
		/*
		 * Report idle status for last ioctl since
		 * calls to pm_busy_component(9F) are stacked.
		 */
		(void) pm_idle_component(mpt->m_dip, 0);
		mutex_enter(&mpt->m_mutex);
	}

	mutex_exit(&mpt->m_mutex);

	/*
	 * last mpt? ... if active, CANCEL watch threads.
	 */
	mutex_enter(&mpt_global_mutex);
	if (mpt_head == NULL) {
		timeout_id_t tid;

		mpt_timeouts_enabled = 0;
		if (mpt_timeout_id) {
			tid = mpt_timeout_id;
			mpt_timeout_id = 0;
			mutex_exit(&mpt_global_mutex);
			(void) untimeout(tid);
			mutex_enter(&mpt_global_mutex);
		}
		if (mpt_reset_watch) {
			tid = mpt_reset_watch;
			mpt_reset_watch = 0;
			mutex_exit(&mpt_global_mutex);
			(void) untimeout(tid);
			mutex_enter(&mpt_global_mutex);
		}

		/*
		 * Clear mpt_scsi_watchdog_tick so that the watch thread
		 * gets restarted on DDI_ATTACH
		 */
		mpt_scsi_watchdog_tick = 0;
	}
	mutex_exit(&mpt_global_mutex);

	/*
	 * Delete nt_active.
	 */
	active = mpt->m_active;
	if (active) {
		kmem_free(active, active->m_size);
		mpt->m_active = NULL;
	}

	/* deallocate everything that was allocated in mpt_attach */
	mpt_fm_fini(mpt);
	if (mpt->m_mpxio_enable) {
		(void) mdi_phci_unregister(mpt->m_dip, 0);
	}

	(void) scsi_hba_detach(dip);
	mpt_free_handshake_msg(mpt);
	mpt_hba_fini(mpt);
	mpt_cfg_fini(mpt);

	/* Lower the power informing PM Framework */
	if (mpt->m_options & MPT_OPT_PM) {
		if (pm_lower_power(dip, 0, PM_LEVEL_D3) != DDI_SUCCESS)
			mpt_log(mpt, CE_WARN,
			    "!mpt%d: Lower power request failed "
			    "during detach, ignoring.",
			    mpt->m_instance);
	}

	mutex_destroy(&mpt->m_mutex);
	mutex_destroy(&mpt->m_passthru_mutex);
	mutex_destroy(&mpt->m_waitq_mutex);
	cv_destroy(&mpt->m_cv);
	cv_destroy(&mpt->m_passthru_cv);
	pci_config_teardown(&mpt->m_config_handle);
	if (mpt->m_tran) {
		scsi_hba_tran_free(mpt->m_tran);
	}
	if (mpt->m_smptran) {
		smp_hba_tran_free(mpt->m_smptran);
	}
	mpt->m_tran = NULL;
	ddi_soft_state_free(mpt_state, ddi_get_instance(dip));
	ddi_prop_remove_all(dip);

	return (DDI_SUCCESS);
}

static int
mpt_alloc_handshake_msg(mpt_t *mpt, size_t alloc_size)
{
	ddi_dma_attr_t		task_dma_attrs;
	ddi_dma_cookie_t	tmp_dma_cookie;
	size_t			alloc_len;
	uint_t			ncookie;

	/* allocate Task Management ddi_dma resources */
	task_dma_attrs = mpt->m_msg_dma_attr;
	task_dma_attrs.dma_attr_sgllen = 1;
	task_dma_attrs.dma_attr_granular = (uint32_t)(alloc_size);

	if (ddi_dma_alloc_handle(mpt->m_dip, &task_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &mpt->m_hshk_dma_hdl) != DDI_SUCCESS) {
		mpt->m_hshk_dma_hdl = NULL;
		return (DDI_FAILURE);
	}

	if (ddi_dma_mem_alloc(mpt->m_hshk_dma_hdl, alloc_size,
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &mpt->m_hshk_memp, &alloc_len, &mpt->m_hshk_acc_hdl)
	    != DDI_SUCCESS) {
		ddi_dma_free_handle(&mpt->m_hshk_dma_hdl);
		mpt->m_hshk_dma_hdl = NULL;
		return (DDI_FAILURE);
	}

	if (ddi_dma_addr_bind_handle(mpt->m_hshk_dma_hdl, NULL,
	    mpt->m_hshk_memp, alloc_len, (DDI_DMA_RDWR | DDI_DMA_CONSISTENT),
	    DDI_DMA_SLEEP, NULL, &tmp_dma_cookie, &ncookie)
	    != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&mpt->m_hshk_acc_hdl);
		ddi_dma_free_handle(&mpt->m_hshk_dma_hdl);
		mpt->m_hshk_dma_hdl = NULL;
		return (DDI_FAILURE);
	}
	mpt->m_hshk_dma_size = alloc_size;
	return (DDI_SUCCESS);
}

static void
mpt_free_handshake_msg(mpt_t *mpt)
{
	if (mpt->m_hshk_dma_hdl != NULL) {
		(void) ddi_dma_unbind_handle(mpt->m_hshk_dma_hdl);
		(void) ddi_dma_mem_free(&mpt->m_hshk_acc_hdl);
		ddi_dma_free_handle(&mpt->m_hshk_dma_hdl);
		mpt->m_hshk_dma_hdl = NULL;
		mpt->m_hshk_dma_size = 0;
	}
}

static int
mpt_power(dev_info_t *dip, int component, int level)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(component))
#endif
	mpt_t *mpt;
	int rval = DDI_SUCCESS;
	int polls = 0;

	mpt = ddi_get_soft_state(mpt_state, ddi_get_instance(dip));
	if (mpt == NULL) {
		return (DDI_FAILURE);
	}

	mutex_enter(&mpt->m_mutex);

	/*
	 * If the device is busy, don't lower its power level
	 */
	if (mpt->m_busy && (mpt->m_power_level > level)) {
		mutex_exit(&mpt->m_mutex);
		return (DDI_FAILURE);
	}

	switch (level) {
	case PM_LEVEL_D0:
		NDBG11(("mpt%d: turning power ON.", mpt->m_instance));
		MPT_POWER_ON(mpt);
		/*
		 * Wait up to 30 seconds for IOC to come out of reset.
		 */
		while ((ddi_get32(mpt->m_datap, &mpt->m_reg->m_doorbell) &
		    MPI_IOC_STATE_MASK) == MPI_IOC_STATE_RESET) {
			if (polls++ > 3000) {
				/*
				 * If IOC does not come out of reset,
				 * try to hard reset it.
				 */
				if (mpt_ioc_reset(mpt) == DDI_FAILURE) {
					mpt_log(mpt, CE_WARN,
					    "mpt_power: hard reset failed");
					mpt_fm_ereport(mpt,
					    DDI_FM_DEVICE_NO_RESPONSE);
					ddi_fm_service_impact(mpt->m_dip,
					    DDI_SERVICE_LOST);
					mutex_exit(&mpt->m_mutex);
					return (DDI_FAILURE);
				}
			}
			delay(drv_usectohz(10000));
		}

		if (!DEVI_IS_ATTACHING(dip)) {
			if (mpt_restart_ioc(mpt)) {
				mpt_log(mpt, CE_WARN, "IOC restart failed.");
				mutex_exit(&mpt->m_mutex);
				return (DDI_FAILURE);
			}
		}
		mpt->m_power_level = PM_LEVEL_D0;
		break;
	case PM_LEVEL_D3:
		NDBG11(("mpt%d: turning power OFF.", mpt->m_instance));
		MPT_POWER_OFF(mpt);
		break;
	default:
		mpt_log(mpt, CE_WARN, "mpt%d: unknown power level <%x>.\n",
		    mpt->m_instance, level);
		rval = DDI_FAILURE;
		break;
	}
	mutex_exit(&mpt->m_mutex);
	return (rval);
}

/*
 * Initialize configuration space and figure out which
 * chip and revison of the chip the mpt driver is using.
 */
int
mpt_config_space_init(mpt_t *mpt)
{
	ushort_t caps_ptr, cap, cap_count, comm;

	NDBG0(("mpt_config_space_init"));

	mpt_setup_cmd_reg(mpt);

	/*
	 * Enable I/O access
	 */
	comm = pci_config_get16(mpt->m_config_handle, PCI_CONF_COMM);
	comm |= PCI_COMM_IO;
	pci_config_put16(mpt->m_config_handle, PCI_CONF_COMM, comm);

	/*
	 * Get the chip device id:
	 */
	mpt->m_devid = pci_config_get16(mpt->m_config_handle, PCI_CONF_DEVID);

	/*
	 * Save the revision in the lower nibble, and the chip type
	 * in the high nibble.
	 */
	mpt->m_revid = pci_config_get16(mpt->m_config_handle, PCI_CONF_REVID);

	/*
	 * Save the SubSystem Vendor and Device IDs
	 */
	mpt->m_svid = pci_config_get16(mpt->m_config_handle, PCI_CONF_SUBVENID);
	mpt->m_ssid = pci_config_get16(mpt->m_config_handle, PCI_CONF_SUBSYSID);

	/*
	 * Each chip has different capabilities, disable certain
	 * features depending on which chip is found.
	 */
	switch (mpt->m_devid) {
	case MPT_1030:
		mpt_log(mpt, CE_NOTE, "?Rev. %d LSI, Inc. 1030 found.\n",
		    MPT_REV(mpt));
		mpt->m_port_type[0] = MPI_PORTFACTS_PORTTYPE_SCSI;
		break;
	case MPT_1064:
	case MPT_1064E:
		if (mpt->m_svid == MPT_DELL_SAS_VID) {
			char dellmsg[MPT_DELL_SAS_BUFLEN];

			switch (mpt->m_ssid) {
			case MPT_DELL_SAS5E_PLAIN:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "%s",
				    MPT_DELL_SAS5E_PLAIN_MSG);
				break;
			case MPT_DELL_SAS5IR_ADAPTERRAID:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "%s",
				    MPT_DELL_SAS5IR_ADAPTERRAID_MSG);
				break;
			default:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "unknown (%x,%x) ",
				    mpt->m_svid, mpt->m_ssid);
				break;
			}
			mpt_log(mpt, CE_CONT,
			    "?Dell SAS%s Controller found.",
			    dellmsg);
		} else {
			mpt_log(mpt, CE_NOTE,
			    "?Rev. %d LSI, Inc. 1064%s found.\n",
			    MPT_REV(mpt),
			    (mpt->m_devid == MPT_1064E ? "E" : ""));
		}
		mpt->m_port_type[0] = MPI_PORTFACTS_PORTTYPE_SAS;
		break;
	case MPT_1068:
	case MPT_1068E:
		if (mpt->m_svid == MPT_DELL_SAS_VID) {
			char dellmsg[MPT_DELL_SAS_BUFLEN];

			switch (mpt->m_ssid) {
			case MPT_DELL_SAS6IR_PLAIN:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "%s", MPT_DELL_SAS6IR_PLAIN_MSG);
				break;
			case MPT_DELL_SAS6IR_INTBLADES:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "%s",
				    MPT_DELL_SAS6IR_INTBLADES_MSG);
				break;
			case MPT_DELL_SAS6IR_INTPLAIN:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "%s",
				    MPT_DELL_SAS6IR_INTPLAIN_MSG);
				break;
			case MPT_DELL_SAS6IR_INTWS:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "%s",
				    MPT_DELL_SAS6IR_INTWS_MSG);
				break;
			case MPT_DELL_SAS5E_PLAIN:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "%s",
				    MPT_DELL_SAS5E_PLAIN_MSG);
				break;
			case MPT_DELL_SAS5I_PLAIN:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "%s",
				    MPT_DELL_SAS5I_PLAIN_MSG);
				break;
			case MPT_DELL_SAS5I_INT:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "%s",
				    MPT_DELL_SAS5I_INT_MSG);
				break;
			case MPT_DELL_SAS5IR_INTRAID1:
			case MPT_DELL_SAS5IR_INTRAID2:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "%s",
				    MPT_DELL_SAS5IR_INTRAID_MSG);
				break;
			case MPT_DELL_SAS5IR_ADAPTERRAID:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "%s",
				    MPT_DELL_SAS5IR_ADAPTERRAID_MSG);
				break;
			default:
				(void) snprintf(dellmsg,
				    MPT_DELL_SAS_BUFLEN,
				    "unknown (%x,%x) ",
				    mpt->m_svid, mpt->m_ssid);
				break;
			}
			mpt_log(mpt, CE_CONT,
			    "?Dell SAS%s Controller found.",
			    dellmsg);
		} else {
			mpt_log(mpt, CE_NOTE,
			    "?Rev. %d LSI, Inc. 1068%s found.\n",
			    MPT_REV(mpt),
			    (mpt->m_devid == MPT_1068E ? "E" : ""));
		}
		mpt->m_port_type[0] = MPI_PORTFACTS_PORTTYPE_SAS;
		break;
	case MPT_1078IR:
		mpt_log(mpt, CE_NOTE, "?Rev. %d LSI, Inc. 1078IR found.\n",
		    MPT_REV(mpt));
		mpt->m_port_type[0] = MPI_PORTFACTS_PORTTYPE_SAS;
		break;
	default:
		/*
		 * Free the configuration registers and fail.
		 */
		mpt_log(mpt, CE_WARN, "LSI PCI device (1000,%x) not supported.",
		    mpt->m_devid);
		return (FALSE);
	}

	/*
	 * Set the latency timer to 0x40 as specified by the upa -> pci
	 * bridge chip design team.  This may be done by the sparc pci
	 * bus nexus driver, but the driver should make sure the latency
	 * timer is correct for performance reasons.
	 */
	pci_config_put8(mpt->m_config_handle, PCI_CONF_LATENCY_TIMER,
	    MPT_LATENCY_TIMER);

	/*
	 * Check if capabilities list is supported and if so,
	 * get initial capabilities pointer and clear bits 0,1.
	 */
	if (pci_config_get16(mpt->m_config_handle, PCI_CONF_STAT)
	    & PCI_STAT_CAP) {
		caps_ptr = P2ALIGN(pci_config_get8(mpt->m_config_handle,
		    PCI_CONF_CAP_PTR), 4);
	} else {
		caps_ptr = PCI_CAP_NEXT_PTR_NULL;
	}

	/*
	 * Walk capabilities if supported.
	 */
	for (cap_count = 0; caps_ptr != PCI_CAP_NEXT_PTR_NULL; ) {

		/*
		 * Check that we haven't exceeded the maximum number of
		 * capabilities and that the pointer is in a valid range.
		 */
		if (++cap_count > 48) {
			mpt_log(mpt, CE_WARN,
			    "too many device capabilities.\n");
			return (FALSE);
		}
		if (caps_ptr < 64) {
			mpt_log(mpt, CE_WARN,
			    "capabilities pointer 0x%x out of range.\n",
			    caps_ptr);
			return (FALSE);
		}

		/*
		 * Get next capability and check that it is valid.
		 * For now, we only support power management.
		 */
		cap = pci_config_get8(mpt->m_config_handle, caps_ptr);
		switch (cap) {
			case PCI_CAP_ID_PM:
				mpt_log(mpt, CE_NOTE,
				    "?mpt%d supports power management.\n",
				    mpt->m_instance);
				mpt->m_options |= MPT_OPT_PM;

				/* Save PMCSR offset */
				mpt->m_pmcsr_offset = caps_ptr + PCI_PMCSR;
				break;

			/*
			 * 0x5 is Message signaled interrupts and 0x7
			 * is pci-x capable.  Both are unsupported for now
			 * but supported by the 1030 chip so we don't
			 * need to keep printing out the notice.
			 * 0x10 is PCI-E support (1064E/1068E)
			 * 0x11 is MSIX supported by the 1064/1068
			 */
			case 0x5:
			case 0x7:
			case 0x10:
			case 0x11:
				break;
			default:
				mpt_log(mpt, CE_NOTE,
				    "?mpt%d unrecognized capability "
				    "0x%x.\n", mpt->m_instance, cap);
			break;
		}

		/*
		 * Get next capabilities pointer and clear bits 0,1.
		 */
		caps_ptr = P2ALIGN(pci_config_get8(mpt->m_config_handle,
		    (caps_ptr + PCI_CAP_NEXT_PTR)), 4);
	}

	return (TRUE);
}

static void
mpt_setup_cmd_reg(mpt_t *mpt)
{
	ushort_t cmdreg;

	/*
	 * Set the command register to the needed values.
	 */
	cmdreg = pci_config_get16(mpt->m_config_handle, PCI_CONF_COMM);
	cmdreg |= (PCI_COMM_ME | PCI_COMM_SERR_ENABLE |
	    PCI_COMM_PARITY_DETECT | PCI_COMM_MAE);
	cmdreg &= ~PCI_COMM_IO;
	pci_config_put16(mpt->m_config_handle, PCI_CONF_COMM, cmdreg);
}

static void
mpt_disable_bus_master(mpt_t *mpt)
{
	ushort_t cmdreg;

	/*
	 * Clear the master enable bit in the PCI command register.
	 * This prevents any bus mastering activity like DMA.
	 */
	cmdreg = pci_config_get16(mpt->m_config_handle, PCI_CONF_COMM);
	cmdreg &= ~PCI_COMM_ME;
	pci_config_put16(mpt->m_config_handle, PCI_CONF_COMM, cmdreg);
}

int
mpt_passthru_dma_alloc(mpt_t *mpt, mpt_dma_alloc_state_t *dma_statep)
{
	ddi_dma_attr_t attrs;
	uint_t ncookie;
	size_t alloc_len;

	attrs = mpt->m_msg_dma_attr;
	attrs.dma_attr_sgllen = 1;

	ASSERT(dma_statep != NULL);

	if (ddi_dma_alloc_handle(mpt->m_dip, &attrs,
	    DDI_DMA_SLEEP, NULL, &dma_statep->handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate dma handle.");
		return (DDI_FAILURE);
	}

	if (ddi_dma_mem_alloc(dma_statep->handle, dma_statep->size,
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &dma_statep->memp, &alloc_len, &dma_statep->accessp) !=
	    DDI_SUCCESS) {
		ddi_dma_free_handle(&dma_statep->handle);
		dma_statep->handle = NULL;
		mpt_log(mpt, CE_WARN,
		    "unable to allocate memory for dma xfer.");
		return (DDI_FAILURE);
	}

	if (ddi_dma_addr_bind_handle(dma_statep->handle, NULL, dma_statep->memp,
	    alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &dma_statep->cookie, &ncookie) != DDI_DMA_MAPPED) {
		ddi_dma_mem_free(&dma_statep->accessp);
		dma_statep->accessp = NULL;
		ddi_dma_free_handle(&dma_statep->handle);
		dma_statep->handle = NULL;
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

void
mpt_passthru_dma_free(mpt_dma_alloc_state_t *dma_statep)
{
	ASSERT(dma_statep != NULL);
	if (dma_statep->handle != NULL) {
		(void) ddi_dma_unbind_handle(dma_statep->handle);
		(void) ddi_dma_mem_free(&dma_statep->accessp);
		ddi_dma_free_handle(&dma_statep->handle);
	}
}

int
mpt_do_dma(mpt_t *mpt, uint32_t size, int var, int (*callback)())
{
	ddi_dma_attr_t attrs;
	ddi_dma_handle_t dma_handle;
	caddr_t memp;
	uint_t ncookie;
	ddi_dma_cookie_t cookie;
	ddi_acc_handle_t accessp;
	size_t alloc_len;
	int rval;

	ASSERT(mutex_owned(&mpt->m_mutex));

	attrs = mpt->m_msg_dma_attr;
	attrs.dma_attr_sgllen = 1;
	attrs.dma_attr_granular = size;

	if (ddi_dma_alloc_handle(mpt->m_dip, &attrs,
	    DDI_DMA_SLEEP, NULL, &dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate dma handle.");
		return (DDI_FAILURE);
	}

	if (ddi_dma_mem_alloc(dma_handle, size,
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &memp, &alloc_len, &accessp) != DDI_SUCCESS) {
		ddi_dma_free_handle(&dma_handle);
		mpt_log(mpt, CE_WARN,
		    "unable to allocate ioc_facts structure.");
		return (DDI_FAILURE);
	}

	if (ddi_dma_addr_bind_handle(dma_handle, NULL, memp,
	    alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &cookie, &ncookie) != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&accessp);
		ddi_dma_free_handle(&dma_handle);
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		return (DDI_FAILURE);
	}

	rval = (*callback) (mpt, memp, var, accessp);

	if (mpt_check_dma_handle(dma_handle) != DDI_SUCCESS) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		rval = DDI_FAILURE;
	}

	if (dma_handle != NULL) {
		(void) ddi_dma_unbind_handle(dma_handle);
		(void) ddi_dma_mem_free(&accessp);
		ddi_dma_free_handle(&dma_handle);
	}

	return (rval);

}

int
mpt_send_config_request_msg(mpt_t *mpt, uint8_t action, uint8_t pagetype,
	uint32_t pageaddress, uint8_t pagenumber, uint8_t pageversion,
	uint8_t pagelength, uint32_t SGEflagslength, uint32_t SGEaddress32)
{
	msg_config_t *config;
	int send_numbytes;

	bzero(mpt->m_hshk_memp, sizeof (struct msg_config));
	config = (struct msg_config *)mpt->m_hshk_memp;
	ddi_put8(mpt->m_hshk_acc_hdl, &config->Function, MPI_FUNCTION_CONFIG);
	ddi_put8(mpt->m_hshk_acc_hdl, &config->Action, action);
	ddi_put8(mpt->m_hshk_acc_hdl, &config->Header.PageNumber, pagenumber);
	ddi_put8(mpt->m_hshk_acc_hdl, &config->Header.PageType, pagetype);
	ddi_put32(mpt->m_hshk_acc_hdl, &config->PageAddress, pageaddress);
	ddi_put8(mpt->m_hshk_acc_hdl, &config->Header.PageVersion, pageversion);
	ddi_put8(mpt->m_hshk_acc_hdl, &config->Header.PageLength, pagelength);
	ddi_put32(mpt->m_hshk_acc_hdl,
	    &config->PageBufferSGE.u1.Simple.FlagsLength, SGEflagslength);
	ddi_put32(mpt->m_hshk_acc_hdl,
	    &config->PageBufferSGE.u1.Simple.u1.Address32, SGEaddress32);
	send_numbytes = sizeof (struct msg_config);

	/*
	 * Post message via handshake
	 */
	if (mpt_send_handshake_msg(mpt, (caddr_t)config, send_numbytes,
	    mpt->m_hshk_acc_hdl)) {
		return (-1);
	}
	return (0);
}

int
mpt_send_extended_config_request_msg(mpt_t *mpt, uint8_t action,
	uint8_t extpagetype, uint32_t pageaddress, uint8_t pagenumber,
	uint8_t pageversion, uint16_t extpagelength,
	uint32_t SGEflagslength, uint32_t SGEaddress32)
{
	msg_config_t *config;
	int send_numbytes;

	bzero(mpt->m_hshk_memp, sizeof (struct msg_config));
	config = (struct msg_config *)mpt->m_hshk_memp;
	ddi_put8(mpt->m_hshk_acc_hdl, &config->Function, MPI_FUNCTION_CONFIG);
	ddi_put8(mpt->m_hshk_acc_hdl, &config->Action, action);
	ddi_put8(mpt->m_hshk_acc_hdl, &config->Header.PageNumber, pagenumber);
	ddi_put8(mpt->m_hshk_acc_hdl, &config->Header.PageType,
	    MPI_CONFIG_PAGETYPE_EXTENDED);
	ddi_put8(mpt->m_hshk_acc_hdl, &config->ExtPageType, extpagetype);
	ddi_put32(mpt->m_hshk_acc_hdl, &config->PageAddress, pageaddress);
	ddi_put8(mpt->m_hshk_acc_hdl, &config->Header.PageVersion, pageversion);
	ddi_put16(mpt->m_hshk_acc_hdl, &config->ExtPageLength, extpagelength);
	ddi_put32(mpt->m_hshk_acc_hdl,
	    &config->PageBufferSGE.u1.Simple.FlagsLength, SGEflagslength);
	ddi_put32(mpt->m_hshk_acc_hdl,
	    &config->PageBufferSGE.u1.Simple.u1.Address32, SGEaddress32);
	send_numbytes = sizeof (struct msg_config);

	/*
	 * Post message via handshake
	 */
	if (mpt_send_handshake_msg(mpt, (caddr_t)config, send_numbytes,
	    mpt->m_hshk_acc_hdl)) {
		return (-1);
	}
	return (0);
}

int8_t
mpt_get_sas_device_phynum(mpt_t *mpt, int bus, int diskid)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_sas_device_0_t *sasdevpage;
	int recv_numbytes, address;
	caddr_t recv_memp, page_memp;
	int8_t rval = DDI_FAILURE;
	int recv_dmastate = 0;
	int page_dmastate = 0;

	MPT_DISABLE_INTR(mpt);

	address = (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
	    MPI_SAS_DEVICE_PGAD_FORM_SHIFT)
	    | (bus << MPI_SAS_DEVICE_PGAD_BT_BUS_SHIFT)
	    | (diskid << MPI_SAS_DEVICE_PGAD_BT_TID_SHIFT);

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular = (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "unable to allocate dma handle.");
		goto cleanup;
	} else {
		recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;
	}

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "unable to allocate reply structure.");
		goto cleanup;
	} else {
		recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;
	}

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto cleanup;
	} else {
		recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	}

	bzero(recv_memp, sizeof (*configreply));
	configreply = (struct msg_config_reply *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	if (mpt_send_extended_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
	    address, 0, 0, 0, 0, 0)) {
		mpt_log(mpt, CE_WARN, "send message failed.");
		goto cleanup;
	}

	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		mpt_log(mpt, CE_WARN, "handshake failed.");
		goto cleanup;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_sas_device_phynum header", 1)) {
		goto cleanup;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular =
	    (sizeof (struct config_page_sas_device_0));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "(unable to allocate dma handle.");
		goto cleanup;
	} else {
		page_dmastate |= MPT_DMA_HANDLE_ALLOCD;
	}

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (struct config_page_sas_device_0)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto cleanup;
	} else {
		page_dmastate |= MPT_DMA_MEMORY_ALLOCD;
	}

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto cleanup;
	} else {
		page_dmastate |= MPT_DMA_HANDLE_BOUND;
	}

	bzero(page_memp, sizeof (config_page_sas_device_0_t));
	sasdevpage = (struct config_page_sas_device_0 *)page_memp;

	if (mpt_send_extended_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE, address, 0,
	    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get16(recv_accessp, &configreply->ExtPageLength),
	    sizeof (struct config_page_sas_device_0),
	    page_cookie.dmac_address)) {
		mpt_log(mpt, CE_WARN, "send message failed.");
		goto cleanup;
	}

	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		mpt_log(mpt, CE_WARN, "handshake failed.");
		goto cleanup;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_sas_device_phynum config", 1)) {
		goto cleanup;
	}

	if ((ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORCPU))
	    != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "dma sync failure.");
		goto cleanup;
	}
	rval = ddi_get8(page_accessp, &sasdevpage->PhyNum);

	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		rval = DDI_FAILURE;
	}

cleanup:
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);

	MPT_ENABLE_INTR(mpt);
	return (rval);
}

static int
mpt_get_scsi_device_params(mpt_t *mpt, int target, uint32_t *params)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_scsi_device_0_t *scsidevicepage;
	int recv_numbytes, i, n;
	caddr_t recv_memp, page_memp;
	int rval = (-1);
	mpt_slots_t *slots = mpt->m_active;
	int recv_dmastate = 0;
	int page_dmastate = 0;

	*params = 0;

	MPT_DISABLE_INTR(mpt);

	if (mpt_send_config_request_msg(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_PAGETYPE_SCSI_DEVICE, target, 0, 0, 0, 0, 0)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular =
	    (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config_reply structure.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(recv_memp, sizeof (*configreply));
	configreply = (struct msg_config_reply *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	/*
	 * get config reply message
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_scsi_device_params header", 1)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular =
	    (sizeof (struct config_page_scsi_device_0));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (struct config_page_scsi_device_0)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto done;
	}
	page_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(page_memp, sizeof (config_page_scsi_device_0_t));
	scsidevicepage = (struct config_page_scsi_device_0 *)page_memp;

	/*
	 * Give reply address to IOC to store config page in and send
	 * config request out.
	 */

	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    MPI_CONFIG_PAGETYPE_SCSI_DEVICE, target, 0,
	    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    sizeof (struct config_page_scsi_device_0),
	    page_cookie.dmac_address)) {
		goto done;
	}

	/*
	 * get reply view handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_scsi_device_params config", 1)) {
		goto done;
	}

	(void) ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORCPU);
	*params = ddi_get32(page_accessp,
	    &scsidevicepage->NegotiatedParameters);

	/*
	 * set m_neg_occured to indicate if the negotiation has occured
	 * or not
	 */
	for (n = 0; n < MPT_MAX_RAIDVOLS; n++) {
		if (MPT_RAID_EXISTS(mpt, n)) {
			for (i = 0; i < mpt->m_ntargets; i++) {
				if (TGT_IS_RAID(mpt, n, i)) {
					if (slots->m_raidvol[n].m_diskid[i] ==
					    target) {
						target = i;
						break;
					}
				}
			}
			if (i != mpt->m_ntargets)
				break;
		}
	}
	if ((ddi_get32(page_accessp, &scsidevicepage->Information)) &
	    MPI_SCSIDEVPAGE0_INFO_PARAMS_NEGOTIATED) {
		mpt->m_neg_occured |= (1<<target);
	} else {
		mpt->m_neg_occured &= ~(1<<target);
	}

	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		rval = -1;
	} else {
		rval = 0;
	}
done:
	/*
	 * free up memory
	 */
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);
	MPT_ENABLE_INTR(mpt);
	return (rval);
}

static int
mpt_get_scsi_port_params(mpt_t *mpt, int bus, uint32_t *params)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_scsi_port_0_t *scsiportpage;
	int recv_numbytes;
	caddr_t recv_memp, page_memp;
	int rval = (-1);
	uint32_t flagslength;
	int recv_dmastate = 0;
	int page_dmastate = 0;

	*params = 0;

	if (mpt_send_config_request_msg(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_PAGETYPE_SCSI_PORT, bus, 0, 0, 0, 0, 0)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular =
	    (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config_reply structure.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(recv_memp, sizeof (*configreply));
	configreply = (struct msg_config_reply *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	/*
	 * get config reply message
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_scsi_port_params header", 1)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular =
	    (sizeof (struct config_page_scsi_port_0));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (struct config_page_scsi_port_0)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto done;
	}
	page_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(page_memp, sizeof (config_page_scsi_port_0_t));
	scsiportpage = (struct config_page_scsi_port_0 *)page_memp;
	flagslength = sizeof (struct config_page_scsi_port_0);

	/*
	 * set up scatter gather element flags
	 */
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_IOC_TO_HOST |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

	/*
	 * Give reply address to IOC to store config page in and send
	 * config request out.
	 */

	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    MPI_CONFIG_PAGETYPE_SCSI_PORT, bus, 0,
	    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    flagslength, page_cookie.dmac_address)) {
			goto done;
	}

	/*
	 * get reply view handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_scsi_port_params config", 1)) {
		goto done;
	}

	(void) ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORCPU);
	*params = ddi_get32(page_accessp, &scsiportpage->Capabilities);

	mpt->m_bus_type = (MPI_SCSIPORTPAGE0_PHY_SIGNAL_TYPE_MASK &
	    ddi_get32(page_accessp, &scsiportpage->PhysicalInterface));

	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		rval = -1;
	} else {
		rval = 0;
	}
done:
	/*
	 * free up memory
	 */
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);
	return (rval);
}

static int
mpt_write_scsi_device_params(mpt_t *mpt, int target,
	uint32_t parameters, uint32_t configuration)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_scsi_device_1_t *scsidevicepage;
	int recv_numbytes, i, n;
	int rval = (-1);
	uint32_t flagslength;
	caddr_t recv_memp, page_memp;
	mpt_slots_t *slots = mpt->m_active;
	int recv_dmastate = 0;
	int page_dmastate = 0;

	/*
	 * If the target id is the id of the hba, don't
	 * write the device config page and don't report an error.
	 */
	if (target == mpt->m_mptid) {
		return (0);
	}

	if (mpt_send_config_request_msg(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_PAGETYPE_SCSI_DEVICE, target, 1, 0, 0, 0, 0)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular =
	    (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config_reply structure.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(recv_memp, sizeof (*configreply));
	configreply = (struct msg_config_reply *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	/*
	 * get config reply message
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo,
	    "mpt_write_scsi_device_params header", 1)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular =
	    (sizeof (struct config_page_scsi_device_1));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (struct config_page_scsi_device_1)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto done;
	}
	page_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(page_memp, sizeof (config_page_scsi_device_1_t));
	scsidevicepage = (struct config_page_scsi_device_1 *)page_memp;
	flagslength = sizeof (struct config_page_scsi_device_1);

	/*
	 * set up scatter gather element flags
	 */
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_IOC_TO_HOST |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);


	/*
	 * Give reply address to IOC to store config page in and send
	 * config request out.
	 */

	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    ddi_get8(recv_accessp, &configreply->Header.PageType), target,
	    1, ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    flagslength, page_cookie.dmac_address)) {
			goto done;
	}

	/*
	 * get reply handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo,
	    "mpt_write_scsi_device_params config", 1)) {
		goto done;
	}

	(void) ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORCPU);

	/*
	 * set up scatter gather element flags
	 */
	flagslength &= ~(MPI_SGE_FLAGS_IOC_TO_HOST <<
	    MPI_SGE_FLAGS_SHIFT);
	flagslength |= (MPI_SGE_FLAGS_HOST_TO_IOC <<
	    MPI_SGE_FLAGS_SHIFT);

	/*
	 * write new parameters to config page
	 */
	ddi_put32(page_accessp, &scsidevicepage->RequestedParameters,
	    parameters);

	/*
	 * write new configuration to config page
	 */
	ddi_put32(page_accessp, &scsidevicepage->Configuration, configuration);

	(void) ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORDEV);

	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
	    ddi_get8(recv_accessp, &configreply->Header.PageType), target,
	    1, ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    flagslength, page_cookie.dmac_address)) {
			goto done;
	}

	/*
	 * get reply handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo,
	    "mpt_write_scsi_device_params update", 1)) {
		goto done;
	}

	/*
	 * set m_props_update so the properties will get updated
	 */
	for (n = 0; n < MPT_MAX_RAIDVOLS; n++) {
		if (MPT_RAID_EXISTS(mpt, n)) {
			for (i = 0; i < mpt->m_ntargets; i++) {
				if (TGT_IS_RAID(mpt, n, i)) {
					if (slots->m_raidvol[n].m_diskid[i] ==
					    target) {
						target = i;
						break;
					}
				}
			}
		}
		if (i != mpt->m_ntargets)
			break;
	}

	mpt->m_props_update |= (1<<target);

	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		rval = -1;
	} else {
		rval = 0;
	}

done:
	/*
	 * free up memory
	 */
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);
	return (rval);
}

static int
mpt_set_port_params(mpt_t *mpt, int port, uint32_t timer)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_scsi_port_1_t *scsiportpage;
	int recv_numbytes;
	int rval = (-1);
	uint32_t flagslength, configuration;
	caddr_t recv_memp, page_memp;
	int recv_dmastate = 0;
	int page_dmastate = 0;

	if (mpt_send_config_request_msg(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_PAGETYPE_SCSI_PORT, port, 1, 0, 0, 0, 0)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular =
	    (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config_reply structure.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(recv_memp, sizeof (*configreply));
	configreply = (struct msg_config_reply *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	/*
	 * get config reply message
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_set_port_params header", 1)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular =
	    (sizeof (struct config_page_scsi_port_1));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (struct config_page_scsi_port_1)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto done;
	}
	page_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(page_memp, sizeof (config_page_scsi_port_1_t));
	scsiportpage = (struct config_page_scsi_port_1 *)page_memp;
	flagslength = sizeof (struct config_page_scsi_port_1);

	/*
	 * set up scatter gather element flags
	 */
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_IOC_TO_HOST |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);


	/*
	 * Give reply address to IOC to store config page in and send
	 * config request out.
	 */

	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    ddi_get8(recv_accessp, &configreply->Header.PageType), port,
	    1, ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    flagslength, page_cookie.dmac_address)) {
			goto done;
	}

	/*
	 * get reply handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_set_port_params config", 1)) {
		goto done;
	}

	(void) ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORCPU);

	/*
	 * get initiator id from scsi port configuration page
	 */
	configuration = ddi_get32(page_accessp, &scsiportpage->Configuration);
	/*
	 * set up scatter gather element flags
	 */
	flagslength &= ~(MPI_SGE_FLAGS_IOC_TO_HOST <<
	    MPI_SGE_FLAGS_SHIFT);
	flagslength |= (MPI_SGE_FLAGS_HOST_TO_IOC <<
	    MPI_SGE_FLAGS_SHIFT);

	/*
	 * mpt->m_mptid is initialized during attach progress. If the initiator
	 * id is set in .conf file and the value is different from the current
	 * configuration, change the initiator id of the scsi port configuration
	 * page.
	 */
	if (mpt->m_mptid != MPT_INVALID_HOSTID && mpt->m_mptid !=
	    MPT_GET_HOST_ID(configuration)) {
		configuration = MPT_SET_HOST_ID(mpt->m_mptid);
		ddi_put32(page_accessp, &scsiportpage->Configuration,
		    configuration);
	} else {
		/*
		 * No one set the property "initiator-id"/"scsi-initiator-id"
		 * in .conf file. We assign the current initiator-id in
		 * scsi port configuration page to the mpt->m_mptid.
		 */
		mpt->m_mptid = MPT_GET_HOST_ID(configuration);
	}
	ddi_put32(page_accessp, &scsiportpage->OnBusTimerValue, timer);

	(void) ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORDEV);
	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
	    ddi_get8(recv_accessp, &configreply->Header.PageType), port,
	    1, ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    flagslength, page_cookie.dmac_address)) {
			goto done;
	}

	/*
	 * get reply handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_set_port_params update", 1)) {
		goto done;
	}

	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		rval = -1;
	} else {
		rval = 0;
	}

done:
	/*
	 * free up memory
	 */
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);
	return (rval);
}

static int
mpt_alloc_post_frames(mpt_t *mpt)
{
	ddi_dma_attr_t frame_dma_attrs;
	caddr_t	memp;
	uint_t ncookie;
	ddi_dma_cookie_t cookie;
	size_t alloc_len;
	size_t mem_size;

	/*
	 * If we are on sparc we will allocate frames based on the frame
	 * size returned to us by the IOC.  If we are on x86 we need
	 * allocate more since we can have multiple cookies that will
	 * lead to multiple S/G elements being attached to a request
	 * that might be greated than the max frame size.
	 */
	mem_size =
	    (mpt->m_max_request_depth * (mpt->m_req_frame_size * 4 *
	    MPT_FRAME_SIZE(mpt)));

	/*
	 * create the frame space
	 */
	frame_dma_attrs = mpt->m_msg_dma_attr;
	frame_dma_attrs.dma_attr_align = 0x10;
	frame_dma_attrs.dma_attr_sgllen = 1;

	/*
	 * allocate a per cmd frame.
	 */
	if (ddi_dma_alloc_handle(mpt->m_dip, &frame_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &mpt->m_dma_hdl) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "Unable to allocate dma handle.");
		return (DDI_FAILURE);
	}

	if (ddi_dma_mem_alloc(mpt->m_dma_hdl,
	    mem_size, &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, (caddr_t *)&memp, &alloc_len, &mpt->m_acc_hdl)
	    != DDI_SUCCESS) {
		ddi_dma_free_handle(&mpt->m_dma_hdl);
		mpt->m_dma_hdl = NULL;
		mpt_log(mpt, CE_WARN,
		    "Unable to allocate post frame structure.");
		return (DDI_FAILURE);
	}

	if (ddi_dma_addr_bind_handle(mpt->m_dma_hdl, NULL,
	    memp, alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP, NULL, &cookie, &ncookie) != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&mpt->m_acc_hdl);
		ddi_dma_free_handle(&mpt->m_dma_hdl);
		mpt->m_dma_hdl = NULL;
		mpt_log(mpt, CE_WARN, "Unable to bind DMA resources.");
		return (DDI_FAILURE);
	}

	/*
	 * Store the frame memory address.  This chip uses this
	 * address to dma to and from the driver's frame.  The
	 * second address is the address mpt uses to fill in
	 * in the frame.
	 */
	mpt->m_fma = cookie.dmac_address;
	mpt->m_frame = memp;

	return (DDI_SUCCESS);
}

static int
mpt_alloc_extra_cmd_mem(mpt_t *mpt, mpt_cmd_t *cmd)
{
	ddi_dma_attr_t cmd_dma_attrs;
	caddr_t memp;
	uint_t ncookie;
	ddi_dma_cookie_t cookie;
	size_t alloc_len;
	size_t mem_size;
	int sgemax, frames;
	struct scsi_pkt *pkt = cmd->cmd_pkt;

	ASSERT(cmd->cmd_extra_fma == NULL);
	ASSERT(cmd->cmd_extra_frame == NULL);

	/*
	 * figure out how much memory we will need.  we can
	 * fit sgemax sge's per frame.
	 */
	sgemax = (((mpt->m_req_frame_size << 2) /
	    sizeof (sge_simple64_t)) - 1);
	frames = (pkt->pkt_numcookies / sgemax);
	if ((frames * sgemax) < (pkt->pkt_numcookies -
	    (MPT_MAX_FRAME_SGES64(mpt) - 1))) {
		frames = (frames + 1);
	}
	mem_size = (mpt->m_req_frame_size * 4 * (frames + 1));

	/*
	 * create the frame space
	 */
	cmd_dma_attrs = mpt->m_msg_dma_attr;
	cmd_dma_attrs.dma_attr_align = 0x10;
	cmd_dma_attrs.dma_attr_sgllen = 1;

	/*
	 * allocate a per cmd frame.
	 */
	if (ddi_dma_alloc_handle(mpt->m_dip, &cmd_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &cmd->cmd_extra_dma_hdl) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "Unable to allocate dma handle.");
		return (DDI_FAILURE);
	}

	if (ddi_dma_mem_alloc(cmd->cmd_extra_dma_hdl,
	    mem_size, &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, (caddr_t *)&memp, &alloc_len, &cmd->cmd_extra_acc_hdl)
	    != DDI_SUCCESS) {
		ddi_dma_free_handle(&cmd->cmd_extra_dma_hdl);
		cmd->cmd_extra_dma_hdl = NULL;
		mpt_log(mpt, CE_WARN,
		    "Unable to allocate extra cmd structure.");
		return (DDI_FAILURE);
	}

	if (ddi_dma_addr_bind_handle(cmd->cmd_extra_dma_hdl, NULL,
	    memp, alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP, NULL, &cookie, &ncookie) != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&cmd->cmd_extra_acc_hdl);
		ddi_dma_free_handle(&cmd->cmd_extra_dma_hdl);
		cmd->cmd_extra_dma_hdl = NULL;
		mpt_log(mpt, CE_WARN, "Unable to bind DMA resources.");
		return (DDI_FAILURE);
	}

	/*
	 * Store the frame memory address.  This chip uses this
	 * address to dma to and from the driver's frame.  The
	 * second address is the address mpt uses to fill in
	 * in the frame.
	 */
	cmd->cmd_extra_fma = cookie.dmac_address;
	cmd->cmd_extra_frame = memp;

	return (DDI_SUCCESS);
}

static void
mpt_free_extra_cmd_mem(mpt_cmd_t *cmd)
{
	struct scsi_pkt *pkt = cmd->cmd_pkt;

	if ((pkt->pkt_numcookies > MPT_MAX_DMA_SEGS) &&
	    (cmd->cmd_extra_dma_hdl != NULL)) {
		(void) ddi_dma_unbind_handle(cmd->cmd_extra_dma_hdl);
		(void) ddi_dma_mem_free(&cmd->cmd_extra_acc_hdl);
		ddi_dma_free_handle(&cmd->cmd_extra_dma_hdl);
		cmd->cmd_extra_frame = NULL;
		cmd->cmd_extra_fma = NULL;
		cmd->cmd_extra_dma_hdl = NULL;
		cmd->cmd_extra_acc_hdl = NULL;
	}
}

static int
mpt_ioc_init_reply_queue(mpt_t *mpt)
{
	size_t alloc_len;
	uint_t ncookie;
	ddi_dma_cookie_t cookie;
	ddi_dma_attr_t reply_dma_attrs;

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's per-hba structures.
	 */
	reply_dma_attrs = mpt->m_msg_dma_attr;
	reply_dma_attrs.dma_attr_align = 0x10;
	reply_dma_attrs.dma_attr_sgllen = 1;

	/*
	 * allocate reply frame buffer pool.
	 */
	if (ddi_dma_alloc_handle(mpt->m_dip, &reply_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &mpt->m_reply_dma_h) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "Unable to allocate reply dma handle.");
		cmn_err(CE_WARN, "!ID[SUNWpd.mpt.hba_init.4016]");
		return (DDI_FAILURE);
	}

	if (ddi_dma_mem_alloc(mpt->m_reply_dma_h, (MPT_REPLY_FRAME_SIZE *
	    mpt->m_max_reply_depth), &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP, NULL, &mpt->m_reply_fifo, &alloc_len,
	    &mpt->m_reply_acc_h) != DDI_SUCCESS) {
		ddi_dma_free_handle(&mpt->m_reply_dma_h);
		mpt->m_reply_dma_h = NULL;
		mpt_log(mpt, CE_WARN, "Unable to allocate reply memory.");
		cmn_err(CE_WARN, "!ID[SUNWpd.mpt.hba_init.4017]");
		return (DDI_FAILURE);
	}

	if (ddi_dma_addr_bind_handle(mpt->m_reply_dma_h,
	    NULL, mpt->m_reply_fifo, alloc_len,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &cookie, &ncookie) != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&mpt->m_reply_acc_h);
		ddi_dma_free_handle(&mpt->m_reply_dma_h);
		mpt->m_reply_dma_h = NULL;
		mpt_log(mpt, CE_WARN, "Unable to bind reply DMA resources.");
		cmn_err(CE_WARN, "!ID[SUNWpd.mpt.hba_init.4018]");
		return (DDI_FAILURE);
	}

	bzero(mpt->m_reply_fifo, MPT_REPLY_FRAME_SIZE * mpt->m_max_reply_depth);
	mpt->m_replyh_args = kmem_zalloc(sizeof (m_replyh_arg_t)
	    * mpt->m_max_reply_depth, KM_SLEEP);
	mpt->m_reply_addr = cookie.dmac_address;

	/*
	 * fill reply fifo with addresses
	 */
	while (cookie.dmac_address < (mpt->m_reply_addr +
	    (MPT_REPLY_FRAME_SIZE * mpt->m_max_reply_depth))) {

		ddi_put32(mpt->m_datap, &mpt->m_reg->m_reply_q,
		    cookie.dmac_address);
		cookie.dmac_address += MPT_REPLY_FRAME_SIZE;
	}
	return (DDI_SUCCESS);
}

static void
mpt_cfg_fini(mpt_t *mpt)
{
	NDBG0(("mpt_cfg_fini"));
	ddi_regs_map_free(&mpt->m_datap);
}

static void
mpt_hba_fini(mpt_t *mpt)
{
	NDBG0(("mpt_hba_fini"));

	/*
	 * Disable any bus mastering ability (i.e: DMA) prior to freeing any
	 * allocated DMA resources.
	 */
	if (mpt->m_config_handle != NULL)
		mpt_disable_bus_master(mpt);

	/*
	 * Free up any allocated memory
	 */
	if (mpt->m_dma_hdl != NULL) {
		(void) ddi_dma_unbind_handle(mpt->m_dma_hdl);
		ddi_dma_mem_free(&mpt->m_acc_hdl);
		ddi_dma_free_handle(&mpt->m_dma_hdl);
		mpt->m_dma_hdl = NULL;
	}

	if (mpt->m_reply_dma_h != NULL) {
		(void) ddi_dma_unbind_handle(mpt->m_reply_dma_h);
		ddi_dma_mem_free(&mpt->m_reply_acc_h);
		ddi_dma_free_handle(&mpt->m_reply_dma_h);
		mpt->m_reply_dma_h = NULL;
	}
	if (mpt->m_replyh_args != NULL) {
		kmem_free(mpt->m_replyh_args, sizeof (m_replyh_arg_t)
		    * mpt->m_max_reply_depth);
	}
}

/*
 * mpt_name_child is for composing the name of the node
 * the format of the name is "target,lun".
 */
static int
mpt_name_child(dev_info_t *dip, char *name, int namelen)
{
	int target;
	int lun;

	target = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, TARGET_PROP, -1);
	if (target == -1)
		return (DDI_FAILURE);
	lun = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, LUN_PROP, -1);
	if (lun == -1)
		return (DDI_FAILURE);

	(void) snprintf(name, namelen, "%x,%x", target, lun);
	return (DDI_SUCCESS);
}


/*
 * tran_tgt_init(9E) - target device instance initialization
 */
static int
mpt_scsi_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(hba_tran))
#endif

	/*
	 * At this point, the scsi_device structure already exists
	 * and has been initialized.
	 *
	 * Use this function to allocate target-private data structures,
	 * if needed by this HBA.  Add revised flow-control and queue
	 * properties for child here, if desired and if you can tell they
	 * support tagged queueing by now.
	 */
	mpt_t *mpt;
	int targ = sd->sd_address.a_target;
	int lun = sd->sd_address.a_lun;
	int type = 0;
	mdi_pathinfo_t	*pip = NULL;
	mpt_tgt_private_t *tgt_private = NULL;
	mpt = SDEV2MPT(sd);

	NDBG0(("mpt_scsi_tgt_init: hbadip=0x%p tgtdip=0x%p tgt=%d lun=%d",
	    (void *)hba_dip, (void *)tgt_dip, targ, lun));

	if (ndi_dev_is_persistent_node(tgt_dip) == 0) {
		/*
		 * If no persistent node exist, we don't allow .conf node
		 * to be created.
		 */
		if (mpt_find_child(mpt, targ, lun) != NULL) {
			if ((ndi_merge_node(tgt_dip, mpt_name_child) !=
			    DDI_SUCCESS)) {
				return (DDI_SUCCESS);
			}
		}
		ddi_set_name_addr(tgt_dip, NULL);
		return (DDI_FAILURE);
	}

	type = mdi_get_component_type(tgt_dip);
	if (type == MDI_COMPONENT_CLIENT) {
		if ((pip = (void *)(sd->sd_private)) == NULL) {
			/*
			 * Very bad news if this occurs. Somehow scsi_vhci has
			 * lost the pathinfo node for this target.
			 */
			return (DDI_NOT_WELL_FORMED);
		}
		if (mdi_prop_lookup_int(pip, TARGET_PROP, &targ) !=
		    DDI_PROP_SUCCESS) {
			mpt_log(mpt, CE_WARN, "Get targ property failed\n");
			return (DDI_FAILURE);
		}
		if (mdi_prop_lookup_int(pip, LUN_PROP, &lun) !=
		    DDI_PROP_SUCCESS) {
			mpt_log(mpt, CE_WARN, "Get lun property failed\n");
			return (DDI_FAILURE);
		}
		if (hba_tran->tran_tgt_private != NULL) {
			return (DDI_SUCCESS);
		}

		tgt_private = kmem_zalloc(sizeof (mpt_tgt_private_t), KM_SLEEP);
		tgt_private->t_target = targ;
		tgt_private->t_lun = lun;
		hba_tran->tran_tgt_private = tgt_private;

		return (DDI_SUCCESS);
	}
	mutex_enter(&mpt->m_mutex);

	if (targ < 0 || targ >= mpt->m_ntargets ||
	    lun < 0 || (MPT_IS_SCSI(mpt) && lun >= NLUNS_MPT)) {
		NDBG0(("%s%d: %s%d bad address <%d,%d>",
		    ddi_driver_name(hba_dip), ddi_get_instance(hba_dip),
		    ddi_driver_name(tgt_dip), ddi_get_instance(tgt_dip),
		    targ, lun));
		mutex_exit(&mpt->m_mutex);
		return (DDI_FAILURE);
	}

	/*
	 * Save the tgt_dip for the given target if one doesn't exist
	 * already.  Dip's for non-existance tgt's will be cleared
	 * in tgt_free
	 */
	if ((mpt->m_active->m_target[targ].m_tgt_dip == NULL) &&
	    (strcmp(ddi_driver_name(sd->sd_dev), "sd") == 0)) {
		mpt->m_active->m_target[targ].m_tgt_dip = tgt_dip;
	}

	if (mpt->m_active->m_target[targ].m_deviceinfo &
	    MPI_SAS_DEVICE_INFO_SATA_DEVICE &&
	    !(mpt->m_active->m_target[targ].m_deviceinfo &
	    MPI_SAS_DEVICE_INFO_ATAPI_DEVICE)) {
		uchar_t *inq89 = NULL;
		int inq89_len = 0x238;
		int reallen = 0;
		int rval = 0;
		struct sata_id *sid = NULL;
		char model[SATA_ID_MODEL_LEN + 1];
		char fw[SATA_ID_FW_LEN + 1];
		char *vid, *pid;
		int i;

		mutex_exit(&mpt->m_mutex);
		/*
		 * According SCSI/ATA Translation -2 (SAT-2) revision 01a
		 * chapter 12.4.2 VPD page 89h includes 512 bytes ATA IDENTIFY
		 * DEVICE data or ATA IDENTIFY PACKET DEVICE data.
		 */
		inq89 = kmem_zalloc(inq89_len, KM_SLEEP);
		rval = mpt_send_inquiryVpd(mpt, targ, 0, 0x89,
		    inq89, inq89_len, &reallen);

		if (rval != 0) {
			if (inq89 != NULL) {
				kmem_free(inq89, inq89_len);
			}

			mpt_log(mpt, CE_WARN, "!mpt request inquiry page "
			    "0x89 for SATA target:%x failed!", targ);
			return (DDI_SUCCESS);
		}
		sid = (void *)(&inq89[60]);

		swab(sid->ai_model, model, SATA_ID_MODEL_LEN);
		swab(sid->ai_fw, fw, SATA_ID_FW_LEN);

		model[SATA_ID_MODEL_LEN] = 0;
		fw[SATA_ID_FW_LEN] = 0;

		/*
		 * split model into into vid/pid
		 */
		for (i = 0, pid = model; i < SATA_ID_MODEL_LEN; i++, pid++)
			if ((*pid == ' ') || (*pid == '\t'))
				break;
		if (i < SATA_ID_MODEL_LEN) {
			vid = model;
			/*
			 * terminate vid, establish pid
			 */
			*pid++ = 0;
		} else {
			/*
			 * vid will stay "ATA     ", the rule is same
			 * as sata framework implementation.
			 */
			vid = NULL;
			/*
			 * model is all pid
			 */
			pid = model;
		}

		/*
		 * override SCSA "inquiry-*" properties
		 */
		if (vid)
			(void) scsi_device_prop_update_inqstring(sd,
			    INQUIRY_VENDOR_ID, vid, strlen(vid));
		if (pid)
			(void) scsi_device_prop_update_inqstring(sd,
			    INQUIRY_PRODUCT_ID, pid, strlen(pid));
		(void) scsi_device_prop_update_inqstring(sd,
		    INQUIRY_REVISION_ID, fw, strlen(fw));

		if (inq89 != NULL) {
			kmem_free(inq89, inq89_len);
		}
	} else {
		mutex_exit(&mpt->m_mutex);
	}

	return (DDI_SUCCESS);
}

/*
 * Test for an ATAPI target device (Only on OPL platforms for now)
 * using the MMC-4 Standard INQUIRY Data Format.
 */
static int
mpt_scsi_tgt_atapi(struct scsi_device *sd)
{
	char *sysname = ddi_node_name(ddi_root_node());
	uchar_t *mmc4_inq = (uchar_t *)sd->sd_inq;
	int rval = FALSE;

	if (sysname && (strcmp(sysname, "SUNW,SPARC-Enterprise") == 0) &&
	    (sd->sd_inq->inq_dtype == DTYPE_RODIRECT) && /* MMC3/4 */
	    (mmc4_inq[1] & 0x80) && /* RMB=1 for Removable Media */
	    (mmc4_inq[2] == 0) && /* Version = 0 for ATAPI */
	    (mmc4_inq[3] == 0x32) && /* IDF1=3, RDF=2 for ATAPI */
	    (mmc4_inq[5] == 0) && /* IDF2 = 0 for ATAPI */
	    (mmc4_inq[6] == 0) && /* IDF3 = 0 for ATAPI */
	    (mmc4_inq[7] == 0)) { /* IDF4 = 0 for ATAPI */
		rval = TRUE;
	}

	return (rval);
}

/*
 * update max lun for this target
 */
static void
mpt_update_max_luns(mpt_t *mpt, int tgt)
{
	switch (SCSI_OPTIONS_NLUNS(mpt->m_target_scsi_options[tgt])) {
		case SCSI_OPTIONS_NLUNS_32:
		mpt->m_max_lun[tgt] = SCSI_32LUNS_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_16:
		mpt->m_max_lun[tgt] = SCSI_16LUNS_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_8:
		mpt->m_max_lun[tgt] = NLUNS_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_1:
		mpt->m_max_lun[tgt] = SCSI_1LUN_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_DEFAULT:
		mpt->m_max_lun[tgt] = (uchar_t)NLUNS_PER_TARGET;
		break;
	default:
		mpt_log(mpt, CE_WARN,
		    "unknown scsi-options value for max luns: using %d",
		    mpt->m_max_lun[tgt]);
		break;
	}
}

/*
 * tran_tgt_free(9E) - target device instance deallocation
 */
static void
mpt_scsi_tgt_free(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(hba_dip, tgt_dip, hba_tran))
#endif

	mpt_t *mpt;
	int targ = sd->sd_address.a_target;
	mpt_tgt_private_t *tgt_private = hba_tran->tran_tgt_private;
	mpt = SDEV2MPT(sd);

	if (tgt_private != NULL) {
		kmem_free(tgt_private, sizeof (mpt_tgt_private_t));
		hba_tran->tran_tgt_private = NULL;
	}
	mutex_enter(&mpt->m_mutex);
	mpt->m_active->m_target[targ].m_tgt_dip = NULL;
	mutex_exit(&mpt->m_mutex);
}


/*
 * scsi_pkt handling
 *
 * Visible to the external world via the transport structure.
 */

/*
 * Notes:
 *	- transport the command to the addressed SCSI target/lun device
 *	- normal operation is to schedule the command to be transported,
 *	  and return TRAN_ACCEPT if this is successful.
 *	- if NO_INTR, tran_start must poll device for command completion
 */
static int
mpt_scsi_start(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	mpt_t *mpt = PKT2MPT(pkt);
	mpt_cmd_t *cmd = PKT2CMD(pkt);
	int rval;

	NDBG1(("mpt_scsi_start: target=%d pkt=0x%p",
	    ap->a_target, (void *)pkt));

	/*
	 * Do not transport the command to the HBA's own SCSI ID
	 * as that operation is not supported.
	 */
	if (MPT_IS_SCSI(mpt) && Tgt(cmd) == mpt->m_mptid) {
		mpt_log(NULL, CE_WARN,
		    "?mpt%d: Can not send SCSI command to HBA's own SCSI ID "
		    "%d. SCSI command rejected.\n", mpt->m_instance,
		    mpt->m_mptid);
		return (TRAN_BADPKT);
	}

	/*
	 * prepare the pkt before taking mutex.
	 */
	rval = mpt_prepare_pkt(mpt, cmd);
	if (rval != TRAN_ACCEPT) {
		return (rval);
	}

	/*
	 * Send the command to target/lun, however your HBA requires it.
	 * If busy, return TRAN_BUSY; if there's some other formatting error
	 * in the packet, return TRAN_BADPKT; otherwise, fall through to the
	 * return of TRAN_ACCEPT.
	 *
	 * Remember that access to shared resources, including the mpt_t
	 * data structure and the HBA hardware registers, must be protected
	 * with mutexes, here and everywhere.
	 *
	 * Also remember that at interrupt time, you'll get an argument
	 * to the interrupt handler which is a pointer to your mpt_t
	 * structure; you'll have to remember which commands are outstanding
	 * and which scsi_pkt is the currently-running command so the
	 * interrupt handler can refer to the pkt to set completion
	 * status, call the target driver back through pkt_comp, etc.
	 *
	 * If the instance lock is held by other thread, don't spin to wait
	 * for it. Instead, queue the cmd and next time when the instance lock
	 * is not held, accept all the queued cmd. A extra tx_waitq is
	 * introduced to protect the queue.
	 *
	 * The polled cmd will not be queud and accepted as usual.
	 *
	 * Under the tx_waitq mutex, record whether a thread is draining
	 * the tx_waitq.  An IO requesting thread that finds the instance
	 * mutex contended appends to the tx_waitq and while holding the
	 * tx_wait mutex, if the draining flag is not set, sets it and then
	 * proceeds to spin for the instance mutex. This scheme ensures that
	 * the last cmd in a burst be processed.
	 *
	 * we enable this feature only when the helper threads are enabled,
	 * at which we think the loads are heavy.
	 *
	 * per instance mutex m_waitq_mutex is introduced to protect the
	 * m_tx_waitqtail, m_tx_waitq, m_tx_draining.
	 */

	if (mpt->m_doneq_thread_n) {
		if (mutex_tryenter(&mpt->m_mutex) != 0) {
			rval = mpt_accept_txwq_and_pkt(mpt, cmd);
			mutex_exit(&mpt->m_mutex);
		} else if (cmd->cmd_pkt_flags & FLAG_NOINTR) {
			mutex_enter(&mpt->m_mutex);
			rval = mpt_accept_txwq_and_pkt(mpt, cmd);
			mutex_exit(&mpt->m_mutex);
		} else {
			mutex_enter(&mpt->m_waitq_mutex);
			if (mpt->m_tx_draining) {
				cmd->cmd_flags |= CFLAG_TXQ;
				*mpt->m_tx_waitqtail = cmd;
				mpt->m_tx_waitqtail = &cmd->cmd_linkp;
				mutex_exit(&mpt->m_waitq_mutex);
			} else { /* drain the queue */
				mpt->m_tx_draining = 1;
				mutex_exit(&mpt->m_waitq_mutex);
				mutex_enter(&mpt->m_mutex);
				rval = mpt_accept_txwq_and_pkt(mpt, cmd);
				mutex_exit(&mpt->m_mutex);
			}
		}
	} else {
		mutex_enter(&mpt->m_mutex);
		rval = mpt_accept_pkt(mpt, cmd);
		mutex_exit(&mpt->m_mutex);
	}

	return (rval);
}

/*
 * Accept all the queued cmds(if any) before accept the current one.
 */
static int
mpt_accept_txwq_and_pkt(mpt_t *mpt, mpt_cmd_t *cmd)
{
	int rval;

	/*
	 * The call to mpt_accept_tx_waitq() must always be performed
	 * because that is where mpt->m_tx_draining is cleared.
	 */
	mutex_enter(&mpt->m_waitq_mutex);
	mpt_accept_tx_waitq(mpt);
	mutex_exit(&mpt->m_waitq_mutex);
	rval = mpt_accept_pkt(mpt, cmd);

	return (rval);
}

static int
mpt_accept_pkt(mpt_t *mpt, mpt_cmd_t *cmd)
{
	int rval = TRAN_ACCEPT;
	mpt_slots_t *slots = mpt->m_active;
	int t = Tgt(cmd);

	NDBG1(("mpt_accept_pkt: cmd=0x%p", (void *)cmd));

	ASSERT(mutex_owned(&mpt->m_mutex));

	if ((cmd->cmd_flags & CFLAG_PREPARED) == 0) {
		rval = mpt_prepare_pkt(mpt, cmd);
		if (rval != TRAN_ACCEPT) {
			cmd->cmd_flags &= ~CFLAG_TRANFLAG;
			return (rval);
		}
	}

	/*
	 * If this packet is being reused it might have extra
	 * memory allocated for it.  In that case free it
	 */
	(void) mpt_free_extra_cmd_mem(cmd);

	/*
	 * reset the throttle if we were draining
	 */
	if ((slots->m_target[t].m_t_ncmds == 0) &&
	    (slots->m_target[t].m_t_throttle == DRAIN_THROTTLE)) {
		NDBG23(("reset throttle"));
		ASSERT(slots->m_target[t].m_reset_delay == 0);
		mpt_set_throttle(mpt, t, MAX_THROTTLE);
	}
	/*
	 * The device is in unstable state now, so return TRAN_BUSY
	 * to stall the I/O's which come from scsi_vhci.
	 *
	 * If the cmd has been in the tx_waitq, it makes no sense to return
	 * TRAN_BUSY now. So just add it to the instance waitq. It is the
	 * DR logic that determines whether to flush the waitq or resend
	 * cmds on the waitq. If it is really necessary to flush the
	 * the waitq, then the reason CMD_RESET is set and cmds are returned
	 * to sd, afterwards sd will resend thosed cmds.
	 */
	if ((slots->m_target[t].m_dr_flag &
	    (MPT_DR_PRE_OFFLINE_TIMEOUT_NO_CANCEL |
	    MPT_DR_PRE_OFFLINE_TIMEOUT) ||
	    (slots->m_target[t].m_dr_offline_dups != 0)) &&
	    (cmd->cmd_pkt_flags & FLAG_NOQUEUE)) {
		if (cmd->cmd_flags & CFLAG_TXQ) {
			mpt_waitq_add(mpt, cmd);
			return (rval);
		} else {
			return (TRAN_BUSY);
		}
	}

	/*
	 * If something catastrophic has happened the throttle is
	 * set to CHOKE_THROTTLE which means this target is no
	 * longer accepting commands. Set the packet reason to
	 * reason CMD_INCOMPLETE status STAT_TERMINATED and return
	 * TRAN_FATAL_ERROR.
	 *
	 * If the cmd has been in the tx_waitq, it makes no sense to return
	 * TRAN_FATAL_ERROR now. So add it to the instance doneq.
	 * Then the completion will be called to notice sd.
	 */
	if (slots->m_target[t].m_t_throttle == CHOKE_THROTTLE) {
		mpt_log(mpt, CE_WARN, "rejecting command, throttle choked");
		mpt_set_pkt_reason(mpt, cmd, CMD_TRAN_ERR, STAT_TERMINATED);
		if (cmd->cmd_flags & CFLAG_TXQ) {
			mpt_doneq_add(mpt, cmd);
			return (rval);
		} else {
			return (TRAN_FATAL_ERROR);
		}
	}

	/*
	 * The first case is the normal case.  mpt gets a
	 * command from the target driver and starts it.
	 */
	if ((mpt->m_ncmds < mpt->m_max_request_depth) &&
	    (slots->m_target[t].m_t_throttle > HOLD_THROTTLE) &&
	    (slots->m_target[t].m_t_ncmds < slots->m_target[t].m_t_throttle) &&
	    (slots->m_target[t].m_reset_delay == 0) &&
	    (((slots->m_target[t].m_t_nwait == 0) &&
	    (mpt->m_bus_config_thread == NULL)) ||
	    (mpt->m_bus_config_thread == curthread)) &&
	    ((cmd->cmd_pkt_flags & FLAG_NOINTR) == 0)) {
		if (mpt_save_cmd(mpt, cmd) == TRUE) {
			(void) mpt_start_cmd(mpt, cmd);
		} else {
			mpt_waitq_add(mpt, cmd);
		}
	} else {
		/*
		 * Add this pkt to the work queue
		 */
		mpt_waitq_add(mpt, cmd);

		if (cmd->cmd_pkt_flags & FLAG_NOINTR) {
			(void) mpt_poll(mpt, cmd, MPT_POLL_TIME);

			/*
			 * Only flush the doneq if this is not a proxy
			 * cmd.  For proxy cmds the flushing of the
			 * doneq will be done in those routines.
			 */
			if ((cmd->cmd_flags & CFLAG_CMDPROXY) == 0) {
				mpt_doneq_empty(mpt);
			}
		}
	}
	return (rval);
}

int
mpt_save_cmd(mpt_t *mpt, mpt_cmd_t *cmd)
{
	mpt_slots_t *slots;
	int slot;
	int t = Tgt(cmd);

	ASSERT(mutex_owned(&mpt->m_mutex));
	slots = mpt->m_active;

	ASSERT(slots->m_n_slots == mpt->m_max_request_depth);

	slot = (slots->m_tags)++;
	if (slots->m_tags >= slots->m_n_slots) {
		slots->m_tags = 0;
	}

alloc_tag:
	/* Validate tag, should never fail. */
	if (slots->m_slot[slot] == NULL) {
		/*
		 * Store assigned tag and tag queue type.
		 * Note, in case of multiple choice, default to simple queue.
		 */
		ASSERT(slot < slots->m_n_slots);
		cmd->cmd_slot = slot;
		slots->m_slot[slot] = cmd;
		mpt->m_ncmds++;

		/*
		 * only increment per target ncmds if this is not a
		 * command that has no target associated with it (i.e. a
		 * event acknoledgment)
		 */
		if ((cmd->cmd_flags & CFLAG_CMDIOC) == 0) {
			slots->m_target[t].m_t_ncmds++;
		}
		cmd->cmd_active_timeout = cmd->cmd_pkt->pkt_time;
		/*
		 * If initial timeout is less than or equal to one tick, bump
		 * the timeout by a tick so that command doesn't timeout before
		 * its allotted time.
		 */
		if (cmd->cmd_active_timeout <= mpt_scsi_watchdog_tick) {
			cmd->cmd_active_timeout += mpt_scsi_watchdog_tick;
		}
		return (TRUE);
	} else {
		int i;

		/* If slot in use, scan until a free one is found. */
		for (i = 0; i < slots->m_n_slots; i++) {
			slot = slots->m_tags;
			if (++(slots->m_tags) >= slots->m_n_slots) {
				slots->m_tags = 0;
			}
			if (slots->m_slot[slot] == NULL) {
				NDBG22(("found free slot %d", slot));
				goto alloc_tag;
			}
		}
	}
	return (FALSE);
}

/*
 * prepare the pkt:
 * the pkt may have been resubmitted or just reused so
 * initialize some fields and do some checks.
 */
static int
mpt_prepare_pkt(mpt_t *mpt, mpt_cmd_t *cmd)
{
	struct scsi_pkt *pkt = CMD2PKT(cmd);

	NDBG1(("mpt_prepare_pkt: cmd=0x%p", (void *)cmd));

	/*
	 * Reinitialize some fields that need it; the packet may
	 * have been resubmitted
	 */
	pkt->pkt_reason = CMD_CMPLT;
	pkt->pkt_state	= 0;
	pkt->pkt_statistics = 0;
	pkt->pkt_resid = 0;
	cmd->cmd_age = 0;
	cmd->cmd_pkt_flags = pkt->pkt_flags;

	/*
	 * According to pkt_dma_flags, set cmd
	 * flags and it would be used when setup
	 * sge function
	 */
	if (pkt->pkt_dma_flags & DDI_DMA_READ) {
		cmd->cmd_flags &= ~CFLAG_DMASEND;
	} else {
		cmd->cmd_flags |= CFLAG_DMASEND;
	}
	if (pkt->pkt_dma_flags & DDI_DMA_CONSISTENT) {
		cmd->cmd_flags |= CFLAG_CMDIOPB;
	}
	/*
	 * zero status byte.
	 */
	*(pkt->pkt_scbp) = 0;

	if (cmd->cmd_flags & CFLAG_DMAVALID) {
		pkt->pkt_resid = pkt->pkt_dma_len;

		/*
		 * consistent packets need to be sync'ed first
		 * (only for data going out)
		 */
		if ((cmd->cmd_flags & CFLAG_CMDIOPB) &&
		    (cmd->cmd_flags & CFLAG_DMASEND)) {
			(void) ddi_dma_sync(pkt->pkt_handle, 0, 0,
			    DDI_DMA_SYNC_FORDEV);
		}
	}

#ifdef MPT_DEBUG
#ifndef __lock_lint
	if (mpt_test_untagged > 0) {
		mpt_slots_t *slots = mpt->m_active;
		int t = Tgt(cmd);
		if (TAGGED(mpt, t) &&
		    slots->m_target[t].m_t_ncmds) {
			cmd->cmd_pkt_flags &= ~FLAG_TAGMASK;
			cmd->cmd_pkt_flags &= ~FLAG_NODISCON;
			mpt_log(mpt, CE_NOTE,
			    "starting untagged cmd, target=%d,"
			    " t_ncmds=%d, cmd=0x%p, throttle=%d\n",
			    Tgt(cmd), slots->m_target[t].m_t_ncmds,
			    (void *)cmd, slots->m_target[t].m_t_throttle);
			mpt_test_untagged = 0;
		}
	}

	if ((pkt->pkt_comp == NULL) &&
	    ((pkt->pkt_flags & FLAG_NOINTR) == 0)) {
		NDBG1(("packet with pkt_comp == 0"));
		return (TRAN_BADPKT);
	}
#endif
#endif

	if ((mpt->m_target_scsi_options[Tgt(cmd)] & SCSI_OPTIONS_DR) == 0) {
		cmd->cmd_pkt_flags |= FLAG_NODISCON;
	}

	cmd->cmd_flags =
	    (cmd->cmd_flags & ~(CFLAG_TRANFLAG | CFLAG_CMD_REMOVED)) |
	    CFLAG_PREPARED | CFLAG_IN_TRANSPORT;

	return (TRAN_ACCEPT);
}
/*
 * tran_setup_pkt(9E) - is the entry point into the HBA
 * which is used to initialize HBA specific information
 * in a scsi_pkt structure on behalf of a SCSI target driver.
 */
/* ARGSUSED */
static int
mpt_tran_setup_pkt(struct scsi_pkt *pkt, int (*callback)(),
    caddr_t arg)
{
	mpt_cmd_t *cmd = NULL;
	mpt_t *mpt = NULL;
	ddi_dma_handle_t	save_arq_dma_handle;
	struct buf *save_arq_bp;
	struct scsi_address *ap;
	ddi_dma_cookie_t	save_arqcookie;
	int kf =  (callback == SLEEP_FUNC)? KM_SLEEP: KM_NOSLEEP;
	int reval = 0;
	mpt_tgt_private_t *tgt_private;

	NDBG4(("mpt_tran_setup_pkt is in\n"));
	mpt = ADDR2MPT(&((pkt)->pkt_address));
	if (mpt == NULL) {
		NDBG4(("mpt_tran_setup_pkt, struct mpt is NULL\n"));
		return (-1);
	}
	cmd = PKT2CMD(pkt);
	if (cmd == NULL) {
		NDBG4(("mpt_tran_setup_pkt, struct cmd is NULL\n"));
		return (-1);
	}
	ap = &(pkt->pkt_address);
	if (ap == NULL) {
		NDBG4(("mpt_tran_setup_pkt, struct pkt_address is NULL\n"));
		return (-1);
	}

	tgt_private = (mpt_tgt_private_t *)ap->a_hba_tran->
	    tran_tgt_private;
	if (tgt_private != NULL) {
		ap->a_target = tgt_private->t_target;
		ap->a_lun = tgt_private->t_lun;
	}
	/*
	 * save arqhandle, it allocated when scsi_pkt constructor
	 */
	save_arq_dma_handle = cmd->cmd_arqhandle;
	save_arq_bp = cmd->cmd_arq_buf;
	save_arqcookie = cmd->cmd_arqcookie;
	bzero(cmd, sizeof (mpt_cmd_t));
	cmd->cmd_arqhandle = save_arq_dma_handle;
	cmd->cmd_arqcookie = save_arqcookie;
	cmd->cmd_arq_buf = save_arq_bp;

	cmd->cmd_pkt = pkt;


	cmd->cmd_cdblen = (uchar_t)(pkt->pkt_cdblen);
	cmd->cmd_scblen	= pkt->pkt_scblen;
	cmd->cmd_rqslen	= SENSE_LENGTH;
	/*
	 * check whether it should used extern cdb and scb.
	 */
#define	DEFAULT_CDBLEN	16
#define	DEFAULT_TGTLEN	0
#define	DEFAULT_SCBLEN	(sizeof (struct scsi_arq_status))
	if ((pkt->pkt_cdblen > DEFAULT_CDBLEN) ||
	    (pkt->pkt_tgtlen > DEFAULT_TGTLEN) ||
	    (pkt->pkt_scblen > DEFAULT_SCBLEN)) {
		reval = mpt_pkt_alloc_extern(mpt, cmd,
		    pkt->pkt_cdblen, pkt->pkt_tgtlen,
		    pkt->pkt_scblen, kf);
		if (reval) {
			return (-1);
		}
	}

	if (pkt->pkt_cdblen <= DEFAULT_CDBLEN)
		pkt->pkt_cdbp = (opaque_t)&cmd->cmd_cdb[0];
	if (pkt->pkt_tgtlen <= (uint_t)(DEFAULT_TGTLEN))
		pkt->pkt_private = (opaque_t)cmd->cmd_pkt_private;
	if (pkt->pkt_scblen <= DEFAULT_SCBLEN)
		pkt->pkt_scbp =  (opaque_t)&cmd->cmd_scb;
	/*
	 * set dma available flag, it is used when setup sge
	 */
	cmd->cmd_flags |= CFLAG_DMAVALID;

	NDBG4(("mpt_tran_setup_pkt exit\n"));

	return (0);
}
/*
 * tran_teardown_pkt() is the entry point into the HBA that
 * must free all of the resources that were allocated to the
 * scsi_pkt(9S) structure during tran_setup_pkt().
 */
static void
mpt_tran_teardown_pkt(struct scsi_pkt *pkt)
{
	mpt_cmd_t *cmd = PKT2CMD(pkt);

	if (cmd->cmd_flags & CFLAG_DMAVALID) {
		cmd->cmd_flags &= ~CFLAG_DMAVALID;
	}

	(void) mpt_free_extra_cmd_mem(cmd);
	/*
	 * check pkt is whether extern pkt
	 */
	if ((cmd->cmd_flags &
	    (CFLAG_FREE | CFLAG_CDBEXTERN | CFLAG_PRIVEXTERN |
	    CFLAG_SCBEXTERN)) == 0) {
		cmd->cmd_flags = CFLAG_FREE;
	} else {
		mpt_pkt_destroy_extern(cmd);
	}
}
/*
 * tran_pkt_constructor -  The constructor is called after
 * the following fields in the scsi_pkt structure have been
 * initialized
 */
static int
mpt_tran_pkt_constructor(struct scsi_pkt *pkt, scsi_hba_tran_t *tran,
    int kmflag)
{
	mpt_cmd_t *cmd = pkt->pkt_ha_private;
	struct scsi_address ap;
	mpt_t *mpt;
	uint_t cookiec;
	ddi_dma_attr_t arq_dma_attr;
	int  (*callback)(caddr_t) = (kmflag == KM_SLEEP) ? DDI_DMA_SLEEP:
	    DDI_DMA_DONTWAIT;

	NDBG4(("mpt_kmem_cache_constructor"));
	mpt = TRAN2MPT(tran);
	if (mpt == NULL) {
		NDBG4(("mpt_kmem_cache_constructor, struct mpt is NULL"));
		return (-1);
	}

	ap.a_hba_tran = tran;
	ap.a_target = 0;
	ap.a_lun = 0;

	cmd->cmd_arq_buf = scsi_alloc_consistent_buf(&ap, (struct buf *)NULL,
	    SENSE_LENGTH, B_READ, callback, NULL);
	if (cmd->cmd_arq_buf == NULL) {
		return (-1);
	}
	arq_dma_attr = mpt->m_msg_dma_attr;
	arq_dma_attr.dma_attr_sgllen = 1;
	if ((ddi_dma_alloc_handle(mpt->m_dip, &arq_dma_attr, callback,
	    NULL, &cmd->cmd_arqhandle)) != DDI_SUCCESS) {
		scsi_free_consistent_buf(cmd->cmd_arq_buf);
		cmd->cmd_arqhandle = NULL;
		return (-1);
	}

	if (ddi_dma_buf_bind_handle(cmd->cmd_arqhandle,
	    cmd->cmd_arq_buf, (DDI_DMA_READ | DDI_DMA_CONSISTENT),
	    callback, NULL, &cmd->cmd_arqcookie, &cookiec) != DDI_SUCCESS) {
		ddi_dma_free_handle(&cmd->cmd_arqhandle);
		scsi_free_consistent_buf(cmd->cmd_arq_buf);
		cmd->cmd_arqhandle = NULL;
		cmd->cmd_arq_buf = NULL;
		return (-1);
	}

	return (0);
}
/*
 * tran_pkt_destructor - free resource what allocated by constructor
 */
/* ARGSUSED */
static void
mpt_tran_pkt_destructor(struct scsi_pkt *pkt, scsi_hba_tran_t *tran)
{
	mpt_cmd_t *cmd = pkt->pkt_ha_private;
	NDBG4(("mpt_kmem_cache_destructor"));

	if (cmd->cmd_arqhandle) {
		(void) ddi_dma_unbind_handle(cmd->cmd_arqhandle);
		ddi_dma_free_handle(&cmd->cmd_arqhandle);
		cmd->cmd_arqhandle = NULL;
	}
	if (cmd->cmd_arq_buf) {
		scsi_free_consistent_buf(cmd->cmd_arq_buf);
		cmd->cmd_arq_buf = NULL;
	}
}
/*
 * allocate and deallocate external pkt space (ie. not part of mpt_cmd)
 * for non-standard length cdb, pkt_private, status areas
 * if allocation fails, then deallocate all external space and the pkt
 */
/* ARGSUSED */
static int
mpt_pkt_alloc_extern(mpt_t *mpt, mpt_cmd_t *cmd,
    int cmdlen, int tgtlen, int statuslen, int kf)
{
	caddr_t cdbp, scbp;
	int  (*callback)(caddr_t) = (kf == KM_SLEEP) ?
	    DDI_DMA_SLEEP : DDI_DMA_DONTWAIT;
	struct scsi_address ap;
	size_t senselength;
	ddi_dma_attr_t	ext_arq_dma_attr;
	uint_t cookiec;

	NDBG3(("mpt_pkt_alloc_extern: "
	    "cmd=0x%p cmdlen=%d tgtlen=%d statuslen=%d kf=%x",
	    (void *)cmd, cmdlen, tgtlen, statuslen, kf));

	cdbp = scbp = NULL;
	cmd->cmd_scblen		= statuslen;
	cmd->cmd_privlen	= (uchar_t)tgtlen;

	if (cmdlen > DEFAULT_CDBLEN) {
		cmd->cmd_flags |= CFLAG_CDBEXTERN;
		if ((cdbp = kmem_zalloc((size_t)cmdlen, kf)) == NULL) {
			goto fail;
		}
		cmd->cmd_pkt->pkt_cdbp = (opaque_t)cdbp;
	}
	if (tgtlen > PKT_PRIV_LEN) {
		cmd->cmd_flags |= CFLAG_PRIVEXTERN;
	}
	if (statuslen > DEFAULT_SCBLEN) {
		if ((scbp = kmem_zalloc((size_t)statuslen, kf)) == NULL) {
			goto fail;
		}
		cmd->cmd_flags |= CFLAG_SCBEXTERN;
		cmd->cmd_pkt->pkt_scbp = (opaque_t)scbp;
		/* allocate sense data buf for DMA */

		senselength = statuslen - MPT_GET_ITEM_OFF(
		    struct scsi_arq_status, sts_sensedata);
		cmd->cmd_rqslen = (uchar_t)senselength;

		ap.a_hba_tran = mpt->m_tran;
		ap.a_target = 0;
		ap.a_lun = 0;

		cmd->cmd_ext_arq_buf = scsi_alloc_consistent_buf(&ap,
		    (struct buf *)NULL, senselength, B_READ,
		    callback, NULL);

		if (cmd->cmd_ext_arq_buf == NULL) {
			goto fail;
		}
		/*
		 * allocate a extern arq handle and bind the buf
		 */
		ext_arq_dma_attr = mpt->m_msg_dma_attr;
		ext_arq_dma_attr.dma_attr_sgllen = 1;
		if ((ddi_dma_alloc_handle(mpt->m_dip,
		    &ext_arq_dma_attr, callback,
		    NULL, &cmd->cmd_ext_arqhandle)) != DDI_SUCCESS) {
			goto fail;
		}

		if (ddi_dma_buf_bind_handle(cmd->cmd_ext_arqhandle,
		    cmd->cmd_ext_arq_buf, (DDI_DMA_READ | DDI_DMA_CONSISTENT),
		    callback, NULL, &cmd->cmd_ext_arqcookie,
		    &cookiec)
		    != DDI_SUCCESS) {
			goto fail;
		}
		cmd->cmd_flags |= CFLAG_EXTARQBUFVALID;
	}
	return (0);
fail:
	mpt_pkt_destroy_extern(cmd);
	return (1);
}

/*
 * deallocate external pkt space and deallocate the pkt
 */
static void
mpt_pkt_destroy_extern(mpt_cmd_t *cmd)
{

	NDBG3(("mpt_pkt_destroy_extern: cmd=0x%p", (void *)cmd));

	if (cmd->cmd_flags & CFLAG_FREE) {
		cmn_err(CE_WARN,
		    "!ID[SUNWpd.mpt.pkt_destroy_extern.4022]");
		/* NOTREACHED */
	}
	if (cmd->cmd_flags & CFLAG_CDBEXTERN) {
		kmem_free(cmd->cmd_pkt->pkt_cdbp, (size_t)cmd->cmd_cdblen);
	}
	if (cmd->cmd_flags & CFLAG_SCBEXTERN) {
		kmem_free(cmd->cmd_pkt->pkt_scbp, (size_t)cmd->cmd_scblen);
		if (cmd->cmd_flags & CFLAG_EXTARQBUFVALID) {
			(void) ddi_dma_unbind_handle(cmd->cmd_ext_arqhandle);
		}
		if (cmd->cmd_ext_arqhandle) {
			ddi_dma_free_handle(&cmd->cmd_ext_arqhandle);
			cmd->cmd_ext_arqhandle = NULL;
		}
		if (cmd->cmd_ext_arq_buf)
			scsi_free_consistent_buf(cmd->cmd_ext_arq_buf);
	}
	cmd->cmd_flags = CFLAG_FREE;
}

static void
mpt_sge_setup(mpt_t *mpt, mpt_cmd_t *cmd, uint32_t *control,
	msg_scsi_io_request_t *frame, uint32_t fma, ddi_acc_handle_t acc_hdl)
{
	uint_t cookiec;
	uint32_t flags;
	ddi_dma_cookie_t *dma_cookie;
	sge_simple64_t *sge;
	sge_chain64_t *sgechain;
	uint32_t dma_low_address;
	uint32_t dma_high_address;
	struct scsi_pkt *pkt = cmd->cmd_pkt;

	ASSERT(cmd->cmd_flags & CFLAG_DMAVALID);

	/*
	 * Save the number of entries in the DMA
	 * Scatter/Gather list
	 */
	cookiec = pkt->pkt_numcookies;

	/*
	 * Set read/write bit in control.
	 */
	if (cmd->cmd_flags & CFLAG_DMASEND) {
		*control |= MPI_SCSIIO_CONTROL_WRITE;
	} else {
		*control |= MPI_SCSIIO_CONTROL_READ;
	}

	ddi_put32(acc_hdl, &frame->DataLength, pkt->pkt_dma_len);

	/*
	 * We have 2 cases here.  First where we can fit all the
	 * SG elements into the main frame, and the case
	 * where we can't.
	 * If we have more cookies than we can attach to a frame
	 * we will need to use a chain element to point
	 * a location of memory where the rest of the S/G
	 * elements reside.
	 */
	if (cookiec <= MPT_MAX_FRAME_SGES64(mpt)) {
		dma_cookie = pkt->pkt_cookies;
		sge = (sge_simple64_t *)(&frame->SGL);
		while (cookiec--) {
			dma_low_address = (uint32_t)
			    (dma_cookie->dmac_laddress & 0xffffffffull);
			dma_high_address = (uint32_t)
			    (dma_cookie->dmac_laddress >> 32);
			ddi_put32(acc_hdl,
			    &sge->Address_Low, dma_low_address);
			ddi_put32(acc_hdl,
			    &sge->Address_High, dma_high_address);
			ddi_put32(acc_hdl, &sge->FlagsLength,
			    dma_cookie->dmac_size);
			flags = ddi_get32(acc_hdl, &sge->FlagsLength);
			flags |= ((uint32_t)
			    (MPI_SGE_FLAGS_SIMPLE_ELEMENT |
			    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
			    MPI_SGE_FLAGS_64_BIT_ADDRESSING) <<
			    MPI_SGE_FLAGS_SHIFT);

			/*
			 * If this is the last cookie, we set the flags
			 * to indicate so
			 */
			if (cookiec == 0) {
				flags |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT
				    | MPI_SGE_FLAGS_END_OF_BUFFER
				    | MPI_SGE_FLAGS_END_OF_LIST) <<
				    MPI_SGE_FLAGS_SHIFT);
			}
			if (cmd->cmd_flags & CFLAG_DMASEND) {
				flags |=
				    (MPI_SGE_FLAGS_HOST_TO_IOC <<
				    MPI_SGE_FLAGS_SHIFT);
			} else {
				flags |=
				    (MPI_SGE_FLAGS_IOC_TO_HOST <<
				    MPI_SGE_FLAGS_SHIFT);
			}
			ddi_put32(acc_hdl, &sge->FlagsLength, flags);
			dma_cookie++;
			sge++;
		}
	} else {
		/*
		 * Hereby we start to deal with multiple frames.
		 * The process is as follows:
		 * 1. Determine how many frames are needed for SGL element
		 *    storage; Note that all frames are stored in contiguous
		 *    memory space and in 64-bit DMA mode each element is
		 *    3 double-words (12 bytes) long.
		 * 2. Fill up the main frame. We need to do this separately
		 *    since it contains the SCSI IO request header and needs
		 *    dedicated processing. Note that the last 2 double-words
		 *    of the SCSI IO header is for SGL element storage.
		 * 3. Fill the chain element in the main frame, so the DMA
		 *    engine can use the following frames.
		 * 4. Enter a loop to fill the remaining frames. Note that the
		 *    last frame contains no chain element.
		 * Some restrictions:
		 * 1. For 64-bit DMA, the simple element and chain element
		 *    are both of 3 double-words (12 bytes) in size, even
		 *    though all frames are stored in the first 4G of mem
		 *    range and the higher 32-bits of the address are always 0.
		 * 2. On some controllers (like the 1064/1068), a frame can
		 *    hold SGL elements with the last 1 or 2 double-words
		 *    (4 or 8 bytes) un-used. On these controllers, we should
		 *    recognize that there's no enough room for another SGL
		 *    element and move the sge pointer to the next frame.
		 */
		int i, j, k, l, frames, sgemax;
		int temp;
		uint8_t chainflags;
		uint16_t chainlength;

		/*
		 * Sgemax is the number of SGE's that will fit
		 * each extra frame and frames is total
		 * number of frames we'll need.  We shift the frame_size
		 * over by 2 because frame_size is the number of
		 * 32 bits words and we want number of bytes.
		 * 1 sge entry per frame is reseverd for the chain element
		 * thus the -1 below.
		 */
		sgemax = (((mpt->m_req_frame_size << 2) /
		    sizeof (sge_simple64_t)) - 1);
		temp = (cookiec - (MPT_MAX_FRAME_SGES64(mpt) - 1)) / sgemax;

		/*
		 * A little check to see if we need to round up the number
		 * of frames we need
		 */
		if ((cookiec - (MPT_MAX_FRAME_SGES64(mpt) - 1)) - (temp *
		    sgemax) > 1) {
			frames = (temp + 1);
		} else {
			frames = temp;
		}
		dma_cookie = pkt->pkt_cookies;
		sge = (sge_simple64_t *)(&frame->SGL);

		/*
		 * First fill in the main frame
		 */
		for (j = 1; j < MPT_MAX_FRAME_SGES64(mpt); j++) {
			dma_low_address = (uint32_t)
			    (dma_cookie->dmac_laddress & 0xffffffffull);
			dma_high_address = (uint32_t)
			    (dma_cookie->dmac_laddress >> 32);
			ddi_put32(acc_hdl, &sge->Address_Low,
			    dma_low_address);
			ddi_put32(acc_hdl, &sge->Address_High,
			    dma_high_address);
			ddi_put32(acc_hdl, &sge->FlagsLength,
			    dma_cookie->dmac_size);
			flags = ddi_get32(acc_hdl, &sge->FlagsLength);
			flags |= ((uint32_t)(MPI_SGE_FLAGS_SIMPLE_ELEMENT |
			    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
			    MPI_SGE_FLAGS_64_BIT_ADDRESSING) <<
			    MPI_SGE_FLAGS_SHIFT);

			/*
			 * If this is the last SGE of this frame
			 * we set the end of list flag
			 */
			if (j == (MPT_MAX_FRAME_SGES64(mpt) - 1)) {
				flags |= ((uint32_t)
				    (MPI_SGE_FLAGS_LAST_ELEMENT) <<
				    MPI_SGE_FLAGS_SHIFT);
			}
			if (cmd->cmd_flags & CFLAG_DMASEND) {
				flags |=
				    (MPI_SGE_FLAGS_HOST_TO_IOC <<
				    MPI_SGE_FLAGS_SHIFT);
			} else {
				flags |=
				    (MPI_SGE_FLAGS_IOC_TO_HOST <<
				    MPI_SGE_FLAGS_SHIFT);
			}
			ddi_put32(acc_hdl, &sge->FlagsLength, flags);
			dma_cookie++;
			sge++;
		}

		/*
		 * Fill in the chain element in the main frame.
		 * About calculation on ChainOffset:
		 * 1. Struct msg_scsi_io_request has 2 double-words (8 bytes)
		 *    in the end reserved for SGL element storage; we should
		 *    count it in our calculation. See its definition in the
		 *    header file.
		 * 2. Constant j is the counter of the current SGL element
		 *    that will be processed, and (j - 1) is the number of
		 *    SGL elements that have been processed (stored in the
		 *    main frame).
		 * 3. ChainOffset value should be in units of double-words (4
		 *    bytes) so the last value should be divided by 4.
		 */
		ddi_put8(acc_hdl, &frame->ChainOffset,
		    (sizeof (struct msg_scsi_io_request) - 8 +
		    (j - 1) * sizeof (sge_simple64_t)) >> 2);
		sgechain = (sge_chain64_t *)sge;
		chainflags = (MPI_SGE_FLAGS_CHAIN_ELEMENT |
		    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
		    MPI_SGE_FLAGS_64_BIT_ADDRESSING);
		ddi_put8(acc_hdl, &sgechain->Flags, chainflags);

		/*
		 * The size of the next frame is the accurate size of space
		 * (in bytes) used to store the SGL elements. j is the counter
		 * of SGL elements. (j - 1) is the number of SGL elements that
		 * have been processed (stored in frames).
		 */
		if (frames >= 2) {
			chainlength = (mpt->m_req_frame_size << 2) /
			    sizeof (sge_simple64_t) * sizeof (sge_simple64_t);
		} else {
			chainlength = ((cookiec - (j - 1)) *
			    sizeof (sge_simple64_t));
		}
		ddi_put16(acc_hdl, &sgechain->Length, chainlength);
		ddi_put32(acc_hdl, &sgechain->Address64_Low,
		    (fma + (mpt->m_req_frame_size << 2)));
		/* fma is allocated in the first 4G mem range */
		ddi_put32(acc_hdl, &sgechain->Address64_High, 0);

		/*
		 * If there are more than 2 frames left we have to
		 * fill in the next chain offset to the location of
		 * the chain element in the next frame.
		 * sgemax is the number of simple elements in an extra
		 * frame. Note that the value NextChainOffset should be
		 * in double-words (4 bytes).
		 */
		if (frames >= 2) {
			ddi_put8(acc_hdl, &sgechain->NextChainOffset,
			    (sgemax * sizeof (sge_simple64_t)) >> 2);
		} else {
			ddi_put8(acc_hdl, &sgechain->NextChainOffset, 0);
		}

		/* Jump to next frame */
		sge = (sge_simple64_t *)((char *)frame +
		    (int)(mpt->m_req_frame_size << 2));
		i = cookiec;

		/*
		 * Start filling in frames with SGE's.  If we
		 * reach the end of frame and still have SGE's
		 * to fill we need to add a chain element and
		 * use another frame.  j will be our counter
		 * for what cookie we are at and i will be
		 * the total cookiec. k is the current frame
		 */
		for (k = 1; k <= frames; k++) {
		for (l = 1; (l <= (sgemax + 1)) && (j <= i); j++, l++) {
			dma_low_address = (uint32_t)
			    (dma_cookie->dmac_laddress & 0xffffffffull);
			dma_high_address = (uint32_t)
			    (dma_cookie->dmac_laddress >> 32);
			ddi_put32(acc_hdl,
			    &sge->Address_Low, dma_low_address);
			ddi_put32(acc_hdl,
			    &sge->Address_High, dma_high_address);
			ddi_put32(acc_hdl,
			    &sge->FlagsLength, dma_cookie->dmac_size);
			flags = ddi_get32(acc_hdl, &sge->FlagsLength);
			flags |= ((uint32_t)
			    (MPI_SGE_FLAGS_SIMPLE_ELEMENT |
			    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
			    MPI_SGE_FLAGS_64_BIT_ADDRESSING) <<
			    MPI_SGE_FLAGS_SHIFT);

			/*
			 * If we have reached the end of frame
			 * and we have more SGE's to fill in
			 * we have to fill the final entry
			 * with a chain element and then
			 * continue to the next frame
			 */
			if ((l == (sgemax + 1)) && (k != frames)) {
				sgechain = (sge_chain64_t *)sge;
				j--;
				chainflags = (MPI_SGE_FLAGS_CHAIN_ELEMENT |
				    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
				    MPI_SGE_FLAGS_64_BIT_ADDRESSING);
				ddi_put8(acc_hdl, &sgechain->Flags,
				    chainflags);
				/*
				 * k is the frame counter and (k + 1) is the
				 * number of the next frame. Note that frames
				 * are in contiguous memory space.
				 */
				ddi_put32(acc_hdl, &sgechain->Address64_Low,
				    (fma + ((mpt->m_req_frame_size << 2) *
				    (k + 1))));
				ddi_put32(acc_hdl, &sgechain->Address64_High,
				    0);

				/*
				 * If there are more than 2 frames left
				 * we have to next chain offset to
				 * the location of the chain element
				 * in the next frame and fill in the
				 * length of the next chain
				 */
				if ((frames - k) >= 2) {
					ddi_put8(acc_hdl,
					    &sgechain->NextChainOffset,
					    (sgemax * sizeof (sge_simple64_t))
					    >> 2);
					ddi_put16(acc_hdl, &sgechain->Length,
					    (mpt->m_req_frame_size << 2) /
					    sizeof (sge_simple64_t) *
					    sizeof (sge_simple64_t));
				} else {
					/*
					 * This is the last frame. Set the
					 * NextChainOffset to 0 and Length
					 * is the total size of all remaining
					 * simple elements.
					 */
					ddi_put8(acc_hdl,
					    &sgechain->NextChainOffset, 0);
					ddi_put16(acc_hdl, &sgechain->Length,
					    (cookiec - j) *
					    sizeof (sge_simple64_t));
				}

				/* Jump to the next frame */
				sge = (sge_simple64_t *)((char *)frame +
				    (int)(mpt->m_req_frame_size << 2) *
				    (k + 1));
				continue;
			}

			/*
			 * If we are at the end of the frame and
			 * there is another frame to fill in
			 * we set the last simple element as last
			 * element
			 */
			if ((l == sgemax) && (k != frames)) {
				flags |= ((uint32_t)
				    (MPI_SGE_FLAGS_LAST_ELEMENT) <<
				    MPI_SGE_FLAGS_SHIFT);
			}

			/*
			 * If this is the final cookie we
			 * indicate it by setting the flags
			 */
			if (j == i) {
				flags |= ((uint32_t)
				    (MPI_SGE_FLAGS_LAST_ELEMENT |
				    MPI_SGE_FLAGS_END_OF_BUFFER |
				    MPI_SGE_FLAGS_END_OF_LIST) <<
				    MPI_SGE_FLAGS_SHIFT);
			}
			if (cmd->cmd_flags & CFLAG_DMASEND) {
				flags |=
				    (MPI_SGE_FLAGS_HOST_TO_IOC <<
				    MPI_SGE_FLAGS_SHIFT);
			} else {
				flags |=
				    (MPI_SGE_FLAGS_IOC_TO_HOST <<
				    MPI_SGE_FLAGS_SHIFT);
			}
			ddi_put32(acc_hdl, &sge->FlagsLength, flags);
			dma_cookie++;
			sge++;
		}
		}
	}
}

/*
 * Interrupt handling
 * Utility routine.  Poll for status of a command sent to HBA
 * without interrupts (a FLAG_NOINTR command).
 */
int
mpt_poll(mpt_t *mpt, mpt_cmd_t *poll_cmd, int polltime)
{
	int rval = TRUE;
	int adj_polltime;
	hrtime_t poll_start;

	NDBG5(("mpt_poll: cmd=0x%p", (void *)poll_cmd));

	/*
	 * If this is not a task management cmd, restart the hba.
	 * We can't start new cmds when we are polling for the
	 * completion of a doorbell cmd.  The spec says the IOC
	 * should not be sent any new cmds.
	 */
	if ((poll_cmd->cmd_flags & CFLAG_CMDPROXY) == 0) {
		mpt_restart_hba(mpt);
	}

	/*
	 * Wait, using drv_usecwait(), long enough for the command to
	 * reasonably return from the target if the target isn't
	 * "dead".  A polled command may well be sent from scsi_poll, and
	 * there are retries built in to scsi_poll if the transport
	 * accepted the packet (TRAN_ACCEPT).  scsi_poll waits 1 second
	 * and retries the transport up to scsi_poll_busycnt times
	 * (currently 60) if
	 * 1. pkt_reason is CMD_INCOMPLETE and pkt_state is 0, or
	 * 2. pkt_reason is CMD_CMPLT and *pkt_scbp has STATUS_BUSY
	 *
	 * limit the waiting to avoid a hang in the event that the
	 * cmd never gets started but we are still receiving interrupts
	 */
	adj_polltime = polltime;
	poll_start = gethrtime();
	while (!(poll_cmd->cmd_flags & CFLAG_FINISHED)) {
		if (mpt_wait_intr(mpt, adj_polltime) == FALSE) {
			NDBG5(("mpt_poll: command incomplete"));
			rval = FALSE;
			break;
		}

		/*
		 * The adjusted polltime can get zero or even negative. Then
		 * mpt_wait_intr() will immediately return FALSE.
		 */
		adj_polltime = polltime - (gethrtime() - poll_start) / 1000000;
	}

	if (rval == FALSE) {

		/*
		 * this isn't supposed to happen, the hba must be wedged
		 * Mark this cmd as a timeout.
		 */
		mpt_set_pkt_reason(mpt, poll_cmd, CMD_TIMEOUT,
		    (STAT_TIMEOUT|STAT_ABORTED));

		if (poll_cmd->cmd_queued == FALSE) {

			NDBG5(("mpt_poll: not on waitq"));

			poll_cmd->cmd_pkt->pkt_state |=
			    (STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD);
		} else {

			/* find and remove it from the waitq */
			NDBG5(("mpt_poll: delete from waitq"));
			mpt_waitq_delete(mpt, poll_cmd);
		}

	}
	NDBG5(("mpt_poll: done"));
	return (rval);
}

/*
 * Used for polling cmds.
 */
static int
mpt_wait_intr(mpt_t *mpt, int polltime)
{
	int cnt;
	uint32_t reply;

	NDBG5(("mpt_wait_intr"));

	/*
	 * Keep polling for at least (polltime * 1000) microseconds
	 */
	for (cnt = 0; cnt < polltime; cnt++) {
		/*
		 * Loop MPT_POLL_TIME times but wait 1000 usec
		 * each time around the loop
		 */
		if ((reply = MPT_GET_NEXT_REPLY(mpt)) != 0xFFFFFFFF) {
			mpt->m_polled_intr = 1;

			/*
			 * clear the next interrupt status from the hardware
			 */
			mpt_process_intr(mpt, reply);
			return (TRUE);
		}
		drv_usecwait(1000);
	}
	return (FALSE);
}

static void
mpt_process_intr(mpt_t *mpt, uint32_t rfm)
{
	uint32_t slot;
	mpt_cmd_t *cmd = NULL;
	struct scsi_pkt *pkt;
	msg_default_reply_t *reply;
	uint8_t function;
	int i;
	mpt_slots_t *slots = mpt->m_active;
	m_replyh_arg_t *args;
	int rfm_no;

	ASSERT(mutex_owned(&mpt->m_mutex));
	/*
	 * If the command is a success we want to quickly process
	 * and return back.
	 */
	if ((rfm & MPI_CONTEXT_REPLY_A_BIT) == 0) {
		rfm = (rfm >> 3);
		NDBG31(("context reply: slot=%d", rfm));

		/* Sanity check rfm */
		if (rfm > slots->m_n_slots) {
			mpt_log(mpt, CE_WARN, "?Received invalid slot %d\n",
			    rfm);
			return;
		}

		cmd = slots->m_slot[rfm];

		/*
		 * print warning and return if the slot is empty
		 */
		if (cmd == NULL) {
			mpt_log(mpt, CE_WARN, "?NULL command returned as "
			    "context reply in slot %d", rfm);
			return;
		}

		pkt = CMD2PKT(cmd);
		pkt->pkt_state |= (STATE_GOT_BUS | STATE_GOT_TARGET
		    | STATE_SENT_CMD | STATE_GOT_STATUS);
		if (cmd->cmd_flags & CFLAG_DMAVALID) {
			pkt->pkt_state |= STATE_XFERRED_DATA;
		}
		pkt->pkt_resid = 0;

		if (cmd->cmd_flags & CFLAG_PASSTHRU) {
			cmd->cmd_flags |= CFLAG_FINISHED;
			cv_broadcast(&mpt->m_passthru_cv);
			return;
		} else {
			mpt_remove_cmd(mpt, cmd);
		}

		if (cmd->cmd_flags & CFLAG_RETRY) {
			cmd->cmd_flags &= ~CFLAG_RETRY;
		} else {
			mpt_doneq_add(mpt, cmd);
		}
		return;
	} else {
		rfm = (rfm << 1);
		NDBG31(("\t\tmpt_process_intr: rfm=0x%x", rfm));

		/*
		 * If rfm is not in the proper range we should ignore this
		 * message and exit the interrupt handler.
		 */
		if ((rfm < mpt->m_reply_addr || rfm >= mpt->m_reply_addr +
		    (MPT_REPLY_FRAME_SIZE * mpt->m_max_reply_depth)) ||
		    ((rfm - mpt->m_reply_addr) % MPT_REPLY_FRAME_SIZE != 0)) {
			mpt_log(mpt, CE_WARN, "?Received invalid reply"
			    " frame address 0x%x\n", rfm);
			return;
		}

		(void) ddi_dma_sync(mpt->m_reply_dma_h, 0, 0,
		    DDI_DMA_SYNC_FORCPU);
		reply = (msg_default_reply_t *)
		    (mpt->m_reply_fifo + (rfm - mpt->m_reply_addr));
		function = mpt_get_msg_Function(mpt->m_reply_acc_h, reply);

		/*
		 * don't get slot information and command for events
		 * since these values don't exist
		 */
		if (function != MPI_FUNCTION_EVENT_NOTIFICATION) {
			slot = mpt_get_msg_MessageContext(mpt->m_reply_acc_h,
			    reply);
			slot = (slot >> 3);

			/* Sanity check slot */
			if (slot > slots->m_n_slots) {
				mpt_log(mpt, CE_WARN,
				    "?Received invalid slot %d\n", slot);
				return;
			}

			cmd = slots->m_slot[slot];

			/*
			 * print warning and return if the slot is empty
			 */
			if (cmd == NULL) {
				mpt_log(mpt, CE_WARN, "?NULL command returned"
				    "as address reply in slot %d, function:%x",
				    slot, function);
				return;
			}
			if (cmd->cmd_flags & CFLAG_PASSTHRU) {
				cmd->cmd_rfm = rfm;
				cmd->cmd_flags |= CFLAG_FINISHED;
				cv_broadcast(&mpt->m_passthru_cv);
				return;
			} else {
				mpt_remove_cmd(mpt, cmd);
			}
			NDBG31(("\t\tmpt_process_intr: slot=%d", slot));
		}

		/*
		 * Depending on the function, we need to handle
		 * the reply frame (and cmd) differently.
		 */
		switch (function) {
		case MPI_FUNCTION_SCSI_IO_REQUEST:
			mpt_check_scsi_io_error(mpt,
			    (msg_scsi_io_reply_t *)reply, cmd);
			break;
		case MPI_FUNCTION_SCSI_TASK_MGMT:
			mpt_check_task_mgt(mpt,
			    (msg_scsi_task_mgmt_reply_t *)reply, cmd);
			break;
		case MPI_FUNCTION_EVENT_NOTIFICATION:
			rfm_no = (rfm - mpt->m_reply_addr) /
			    MPT_REPLY_FRAME_SIZE;
			args = &mpt->m_replyh_args[rfm_no];
			args->mpt = (void *)mpt;
			args->rfm = rfm;

			/*
			 * Record the event if its type is enabled in
			 * this mpt instance by ioctl.
			 */
			mpt_record_event(args);

			/*
			 * Handle time critical events
			 * NOT_RESPONDING/ADDED only now
			 */
			if (mpt_handle_event_sync(args) == DDI_SUCCESS) {
			/*
			 * Would not return main process,
			 * just let taskq resolve ack action
			 * and ack would be sent in taskq thread
			 */
				NDBG20(("send mpt_handle_event_sync success"));
			}
			if ((ddi_taskq_dispatch(mpt->m_event_taskq,
			    mpt_handle_event,
			    (void *)args, DDI_NOSLEEP)) != DDI_SUCCESS) {
				mpt_log(mpt, CE_WARN, "Failed to dispatch a "
				    "task to taskq");
				ddi_put32(mpt->m_datap, &mpt->m_reg->m_reply_q,
				    rfm);
			}
			return;
		case MPI_FUNCTION_EVENT_ACK:
			/*
			 * since we don't want to add this to the doneq
			 * we return the frame to the fifo and return
			 */
			NDBG20(("Event ack reply received"));
			ddi_put32(mpt->m_datap, &mpt->m_reg->m_reply_q, rfm);
			return;
		case MPI_FUNCTION_FW_DOWNLOAD:
		{
			msg_fw_download_reply_t *fwdownload;
			uint32_t iocstat;

			pkt = CMD2PKT(cmd);
			fwdownload = (msg_fw_download_reply_t *)reply;
			iocstat = mpt_handle_ioc_status(mpt, mpt->m_reply_acc_h,
			    &fwdownload->IOCStatus, &fwdownload->IOCLogInfo,
			    "Firmware download", 0);
			if (iocstat != MPI_IOCSTATUS_SUCCESS) {
				pkt->pkt_reason = CMD_INCOMPLETE;
				mpt_log(mpt, CE_WARN, "Firmware download "
				    "failed, IOCStatus=0x%x", iocstat);
			}

			cmd->cmd_flags |= CFLAG_FINISHED;
			ddi_put32(mpt->m_datap, &mpt->m_reg->m_reply_q, rfm);
			return;
		}
		case MPI_FUNCTION_FW_UPLOAD:
		{
			msg_fw_upload_reply_t *fwupload;
			uint32_t iocstat;

			pkt = CMD2PKT(cmd);
			fwupload = (msg_fw_upload_reply_t *)reply;
			iocstat = mpt_handle_ioc_status(mpt, mpt->m_reply_acc_h,
			    &fwupload->IOCStatus, &fwupload->IOCLogInfo,
			    "Firmware upload", 0);
			if (iocstat != MPI_IOCSTATUS_SUCCESS) {
				pkt->pkt_reason = CMD_INCOMPLETE;
				mpt_log(mpt, CE_WARN, "Firmware upload "
				    "failed, IOCStatus=0x%x", iocstat);
			}

			cmd->cmd_flags |= CFLAG_FINISHED;
			ddi_put32(mpt->m_datap, &mpt->m_reg->m_reply_q, rfm);
			return;
		}
		case MPI_FUNCTION_RAID_ACTION:
		{
			msg_raid_action_reply_t *raidaction;
			uint8_t action;
			uint16_t astatus;
			uint32_t word;
			uint16_t iocstatus;
			int j, n, vol;

			pkt = CMD2PKT(cmd);
			raidaction = (msg_raid_action_reply_t *)reply;
			action = ddi_get8(mpt->m_reply_acc_h,
			    &raidaction->Action);
			iocstatus = mpt_handle_ioc_status(mpt,
			    mpt->m_reply_acc_h, &raidaction->IOCStatus,
			    &raidaction->IOCLogInfo, "Raid Action", 0);
			astatus = ddi_get16(mpt->m_reply_acc_h,
			    &raidaction->ActionStatus);
			word = ddi_get32(mpt->m_reply_acc_h,
			    &raidaction->ActionData);
			if ((astatus != MPI_RAID_VOL_ASTATUS_SUCCESS) ||
			    (iocstatus != MPI_IOCSTATUS_SUCCESS)) {
				pkt->pkt_reason = CMD_INCOMPLETE;
				mpt_log(mpt, CE_WARN, "Raid Action failed, "
				    "IOCStatus=0x%x ActionStatus=0x%x ",
				    iocstatus, astatus);
			}

			/*
			 * If this was a create physdisk action, the
			 * physdisknum will be returned as the actionword
			 * of the reply.  Since the numbers began at 0 and
			 * increase from there we can just assign the value
			 * of word to the first available position in the
			 * m_disknum array of this raidvol, which has been
			 * assigned the value of -1 in mpt_create_raid()
			 */
			if (action == MPI_RAID_ACTION_CREATE_PHYSDISK) {
				for (i = 0; i < MPT_MAX_RAIDVOLS; i++) {
					if (MPT_RAID_EXISTS(mpt, i))
						continue;
					vol = i;
					break;
				}

				n = slots->m_raidvol[vol].m_ndisks;

				for (j = 0; j < n; j++) {
					/*
					 * assign word to lowest reserved vol
					 */
					if (slots->m_raidvol[vol].m_disknum[j]
					    == 255)
						break;
				}
				slots->m_raidvol[vol].m_disknum[j]
				    = (uint8_t)(word);
			}

			cmd->cmd_flags |= CFLAG_FINISHED;
			ddi_put32(mpt->m_datap, &mpt->m_reg->m_reply_q, rfm);
			return;
		}
		default:
			mpt_log(mpt, CE_WARN,
			    "Unknown function 0x%x reply frame", function);
			(void) mpt_handle_ioc_status(mpt, mpt->m_reply_acc_h,
			    &reply->IOCStatus, &reply->IOCLogInfo, "", 1);
			break;
		}

		/*
		 * return frame to fifo for re-use
		 */
		ddi_put32(mpt->m_datap, &mpt->m_reg->m_reply_q, rfm);
	}

	/*
	 * If the cmd is not successfully returned, do the completion
	 * immediately.
	 */
	if (cmd->cmd_flags & CFLAG_RETRY) {
		/*
		 * The target returned QFULL or busy, do not add tihs
		 * pkt to the doneq since the hba will retry
		 * this cmd.
		 *
		 * The pkt has already been resubmitted in
		 * mpt_handle_qfull() or in mpt_check_scsi_io_error().
		 * Remove this cmd_flag here.
		 */
		cmd->cmd_flags &= ~CFLAG_RETRY;
	} else {
		struct scsi_pkt *pkt = CMD2PKT(cmd);

		ASSERT((cmd->cmd_flags & CFLAG_COMPLETED) == 0);
		cmd->cmd_linkp = NULL;
		cmd->cmd_flags |= CFLAG_FINISHED;
		cmd->cmd_flags &= ~CFLAG_IN_TRANSPORT;

		if (pkt->pkt_comp) {
			cmd->cmd_flags |= CFLAG_COMPLETED;
			mutex_exit(&mpt->m_mutex);
			scsi_hba_pkt_comp(pkt);
			mutex_enter(&mpt->m_mutex);
		}
	}
}

static void
mpt_check_scsi_io_error(mpt_t *mpt, msg_scsi_io_reply_t *reply,
    mpt_cmd_t *cmd)
{
	uint8_t scsi_status, scsi_state, sensebuflength;
	uint16_t ioc_status;
	uint32_t xferred, sensecount;
	struct scsi_pkt *pkt;
	struct scsi_arq_status *arqstat;
	struct buf *bp;
	int i;
	mpt_slots_t *slots = mpt->m_active;
	uint8_t *sensedata = NULL;

	if ((cmd->cmd_flags & (CFLAG_SCBEXTERN | CFLAG_EXTARQBUFVALID)) ==
	    (CFLAG_SCBEXTERN | CFLAG_EXTARQBUFVALID)) {
		bp = cmd->cmd_ext_arq_buf;
	} else {
		bp = cmd->cmd_arq_buf;
	}

	scsi_status = ddi_get8(mpt->m_reply_acc_h, &reply->SCSIStatus);
	ioc_status = mpt_handle_ioc_status(mpt, mpt->m_reply_acc_h,
	    &reply->IOCStatus, &reply->IOCLogInfo,
	    "mpt_check_scsi_io", 0);
	scsi_state = ddi_get8(mpt->m_reply_acc_h, &reply->SCSIState);
	xferred = ddi_get32(mpt->m_reply_acc_h, &reply->TransferCount);
	sensecount = ddi_get32(mpt->m_reply_acc_h, &reply->SenseCount);
	sensebuflength = ddi_get8(mpt->m_reply_acc_h,
	    &reply->SenseBufferLength);

	NDBG31(("\t\tscsi_status=0x%x, ioc_status=0x%x, scsi_state=0x%x",
	    scsi_status, ioc_status, scsi_state));

	pkt = CMD2PKT(cmd);
	*(pkt->pkt_scbp) = scsi_status;

	if ((scsi_state & MPI_SCSI_STATE_NO_SCSI_STATUS) &&
	    (ioc_status == MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE)) {
		/*
		 * Allow sd to retry command if using parallel SCSI HBA
		 * since selection timeout does not guarantee device is
		 * no longer available.
		 */
		if (MPT_IS_SCSI(mpt)) {
			pkt->pkt_reason = CMD_INCOMPLETE;
		} else {
			if ((slots->m_target[Tgt(cmd)].m_dr_flag ==
			    MPT_DR_OFFLINE_IN_PROGRESS) ||
			    (slots->m_target[Tgt(cmd)].m_deviceinfo == NULL))
				pkt->pkt_reason = CMD_DEV_GONE;
			else
				pkt->pkt_reason = CMD_INCOMPLETE;
		}

		pkt->pkt_state |= STATE_GOT_BUS;
		mpt->m_props_update &= ~(1<<Tgt(cmd));
		if (slots->m_target[Tgt(cmd)].m_reset_delay == 0) {
			mpt_set_throttle(mpt, Tgt(cmd),
			    DRAIN_THROTTLE);
		}
		return;
	}

	switch (scsi_status) {
	case MPI_SCSI_STATUS_CHECK_CONDITION:
		pkt->pkt_resid = (pkt->pkt_dma_len - xferred);
		arqstat = (void*)(pkt->pkt_scbp);
		arqstat->sts_rqpkt_status = *((struct scsi_status *)
		    (pkt->pkt_scbp));
		pkt->pkt_state |= (STATE_GOT_BUS | STATE_GOT_TARGET |
		    STATE_SENT_CMD | STATE_GOT_STATUS | STATE_ARQ_DONE);
		if (cmd->cmd_flags & CFLAG_XARQ) {
			pkt->pkt_state |= STATE_XARQ_DONE;
		}
		if (pkt->pkt_resid != pkt->pkt_dma_len) {
			pkt->pkt_state |= STATE_XFERRED_DATA;
		}
		arqstat->sts_rqpkt_reason = pkt->pkt_reason;
		arqstat->sts_rqpkt_state  = pkt->pkt_state;
		arqstat->sts_rqpkt_state |= STATE_XFERRED_DATA;
		arqstat->sts_rqpkt_statistics = pkt->pkt_statistics;
		sensedata = (uint8_t *)&arqstat->sts_sensedata;

		bcopy((uchar_t *)bp->b_un.b_addr, sensedata,
		    ((cmd->cmd_rqslen >= sensecount) ? sensecount :
		    cmd->cmd_rqslen));

		arqstat->sts_rqpkt_resid = (sensebuflength - sensecount);
		cmd->cmd_flags |= CFLAG_CMDARQ;
		/*
		 * Set proper status for pkt if autosense was valid
		 */
		if (scsi_state & MPI_SCSI_STATE_AUTOSENSE_VALID) {
			struct scsi_status zero_status = { 0 };
			arqstat->sts_rqpkt_status = zero_status;
		}

		/*
		 * ASC=0x47 is parity error
		 * ASC=0x48 is initiator detected error received
		 */
		if ((scsi_sense_key(sensedata) == KEY_ABORTED_COMMAND) &&
		    ((scsi_sense_asc(sensedata) == 0x47) ||
		    (scsi_sense_asc(sensedata) == 0x48))) {
			if (MPT_IS_SCSI(mpt)) {
				mpt_sync_wide_backoff(mpt, Tgt(cmd));
			}
		}

		/*
		 * ASC/ASCQ=0x3F/0x0E means report_luns data changed
		 * ASC/ASCQ=0x25/0x00 means invalid lun
		 */
		if (((scsi_sense_key(sensedata) == KEY_UNIT_ATTENTION) &&
		    (scsi_sense_asc(sensedata) == 0x3F) &&
		    (scsi_sense_ascq(sensedata) == 0x0E)) ||
		    ((scsi_sense_key(sensedata) == KEY_ILLEGAL_REQUEST) &&
		    (scsi_sense_asc(sensedata) == 0x25) &&
		    (scsi_sense_ascq(sensedata) == 0x00))) {
			m_dr_arg_t *args;
			int targ = Tgt(cmd);
			if (MPT_IS_SAS(mpt)) {
				args = kmem_zalloc(sizeof (m_dr_arg_t),
				    KM_NOSLEEP);
				if (args == NULL) {
					mpt_log(mpt, CE_NOTE, "No memory"
					    "resource for handle SAS dynamic"
					    "reconfigure.\n");
					break;
				}
				slots->m_target[targ].m_dr_flag =
				    MPT_DR_ONLINE_IN_PROGRESS;
				slots->m_target[targ].m_dr_online_dups++;
				args->mpt = (void *)mpt;
				args->target = targ;
				args->event = MPT_DR_EVENT_RECONFIG_TARGET;
				args->israid = 0;
				if ((ddi_taskq_dispatch(mpt->m_dr_taskq,
				    mpt_handle_dr,
				    (void *)args,
				    DDI_NOSLEEP)) != DDI_SUCCESS) {
					mpt_log(mpt, CE_NOTE, "mpt start taskq"
					    "for handle SAS dynamic reconfigure"
					    "failed. \n");
				}
			}
		}
		break;
	case MPI_SCSI_STATUS_SUCCESS:
		switch (ioc_status) {
		case MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
			/*
			 * Allow sd to retry command if using parallel SCSI HBA
			 * since selection timeout does not guarantee device is
			 * no longer available.
			 */
			if (MPT_IS_SCSI(mpt)) {
				pkt->pkt_reason = CMD_INCOMPLETE;
			} else {
				if ((slots->m_target[Tgt(cmd)].m_dr_flag ==
				    MPT_DR_OFFLINE_IN_PROGRESS) ||
				    (slots->m_target[Tgt(cmd)].m_deviceinfo ==
				    NULL))
					pkt->pkt_reason = CMD_DEV_GONE;
				else
					pkt->pkt_reason = CMD_INCOMPLETE;
			}

			pkt->pkt_state |= STATE_GOT_BUS;
			mpt->m_props_update &= ~(1<<Tgt(cmd));
			if (slots->m_target[Tgt(cmd)].m_reset_delay == 0) {
				mpt_set_throttle(mpt, Tgt(cmd),
				    DRAIN_THROTTLE);
			}
			NDBG31(("lost disk for target%d, command:%x",
			    Tgt(cmd), pkt->pkt_cdbp[0]));
			break;
		case MPI_IOCSTATUS_SCSI_DATA_OVERRUN:
			NDBG31(("data overrun: xferred=%d", xferred));
			pkt->pkt_reason = CMD_DATA_OVR;
			pkt->pkt_state |= (STATE_GOT_BUS | STATE_GOT_TARGET
			    | STATE_SENT_CMD | STATE_GOT_STATUS
			    | STATE_XFERRED_DATA);
			pkt->pkt_resid = 0;
			break;
		case MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
		case MPI_IOCSTATUS_SCSI_DATA_UNDERRUN:
			NDBG31(("data underrun: xferred=%d", xferred));
			pkt->pkt_state |= (STATE_GOT_BUS | STATE_GOT_TARGET
			    | STATE_SENT_CMD | STATE_GOT_STATUS);
			pkt->pkt_resid = (pkt->pkt_dma_len - xferred);
			if (pkt->pkt_resid != pkt->pkt_dma_len) {
				pkt->pkt_state |= STATE_XFERRED_DATA;
			}
			break;
		case MPI_IOCSTATUS_SCSI_TASK_TERMINATED:
			/*
			 * If a disconnected command timeout occurred,
			 * set pkt_reason accordingly.
			 */
			if ((cmd->cmd_pkt->pkt_time > 0) &&
			    (cmd->cmd_active_timeout < 1)) {
				mpt_set_pkt_reason(mpt, cmd, CMD_TIMEOUT,
				    (STAT_TIMEOUT | STAT_BUS_RESET));
			} else {
				mpt_set_pkt_reason(mpt, cmd, CMD_RESET,
				    STAT_BUS_RESET);
			}
			break;
		case MPI_IOCSTATUS_SCSI_IOC_TERMINATED:
		case MPI_IOCSTATUS_SCSI_EXT_TERMINATED:
			mpt_set_pkt_reason(mpt,
			    cmd, CMD_RESET, STAT_DEV_RESET);
			break;
		case MPI_IOCSTATUS_SCSI_IO_DATA_ERROR:
		case MPI_IOCSTATUS_SCSI_PROTOCOL_ERROR:
			pkt->pkt_state |= (STATE_GOT_BUS | STATE_GOT_TARGET);
			mpt_set_pkt_reason(mpt,
			    cmd, CMD_TERMINATED, STAT_TERMINATED);
			break;
		case MPI_IOCSTATUS_INSUFFICIENT_RESOURCES:
		case MPI_IOCSTATUS_BUSY:
			/*
			 * set throttles to drain
			 */
			for (i = 0; i < mpt->m_ntargets; i++) {
				mpt_set_throttle(mpt, i, DRAIN_THROTTLE);
			}

			/*
			 * retry command
			 */
			cmd->cmd_flags |= CFLAG_RETRY;
			cmd->cmd_flags &= ~CFLAG_CMD_REMOVED;
			cmd->cmd_pkt_flags |= FLAG_HEAD;

			(void) mpt_accept_pkt(mpt, cmd);
			break;
		default:
			mpt_log(mpt, CE_WARN,
			    "unknown ioc_status = %x\n", ioc_status);
			mpt_log(mpt, CE_CONT, "scsi_state = %x, transfer "
			    "count = %x, scsi_status = %x", scsi_state,
			    xferred, scsi_status);
			break;
		}
		break;
	case MPI_SCSI_STATUS_TASK_SET_FULL:
		mpt_handle_qfull(mpt, cmd);
		break;
	case MPI_SCSI_STATUS_BUSY:
		NDBG31(("scsi_status busy received"));
		break;
	case MPI_SCSI_STATUS_RESERVATION_CONFLICT:
		NDBG31(("scsi_status reservation conflict received"));
		break;
	default:
		mpt_log(mpt, CE_WARN, "scsi_status=%x, ioc_status=%x\n",
		    scsi_status, ioc_status);
		mpt_log(mpt, CE_WARN,
		    "mpt_process_intr: invalid scsi status\n");
		break;
	}
}

static void
mpt_check_task_mgt(mpt_t *mpt, msg_scsi_task_mgmt_reply_t *reply,
    mpt_cmd_t *cmd)
{
	uint8_t task_type;
	uint16_t ioc_status;
	uint16_t targ;

	task_type = ddi_get8(mpt->m_reply_acc_h, &reply->TaskType);
	ioc_status = mpt_handle_ioc_status(mpt, mpt->m_reply_acc_h,
	    &reply->IOCStatus, &reply->IOCLogInfo,
	    "mpt_check_task_mgt", 0);

	if (MPT_IS_SAS(mpt)) {
		targ = BUSTARG_TO_BT(
		    ddi_get8(mpt->m_reply_acc_h, &reply->Bus),
		    ddi_get8(mpt->m_reply_acc_h, &reply->TargetID));
	} else {
		targ = ddi_get8(mpt->m_reply_acc_h, &reply->TargetID);
	}

	if (ioc_status != MPI_IOCSTATUS_SUCCESS) {
		mpt_log(mpt, CE_WARN, "mpt_check_task_mgt: Task 0x%x failed. "
		    "IOCStatus=0x%x target=%d\n", task_type, ioc_status,
		    targ);
		return;
	}

	switch (task_type) {
	case MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK:
		break;
	case MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
	case MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
	case MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
		mpt_flush_target(mpt, Tgt(cmd), Lun(cmd), task_type);
		break;
	case MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS:
		mpt_flush_hba(mpt);
		break;
	default:
		mpt_log(mpt, CE_WARN, "Unknown task management type %d.",
		    task_type);
		mpt_log(mpt, CE_WARN, "ioc status = %x", ioc_status);
		break;
	}
}

static void
mpt_doneq_thread(mpt_doneq_thread_arg_t *arg)
{
	mpt_t *mpt = arg->mpt;
	uint64_t t = arg->t;
	mpt_cmd_t *cmd;
	struct scsi_pkt *pkt;
	mpt_doneq_thread_list_t *item = &mpt->m_doneq_thread_id[t];

	mutex_enter(&item->mutex);
	while (item->flag & MPT_DONEQ_THREAD_ACTIVE) {
		if (!item->doneq) {
			cv_wait(&item->cv, &item->mutex);
		}
		pkt = NULL;
		if ((cmd = mpt_doneq_thread_rm(mpt, t)) != NULL) {
			cmd->cmd_flags |= CFLAG_COMPLETED;
			pkt = CMD2PKT(cmd);
		}
		mutex_exit(&item->mutex);
		if (pkt) {
			scsi_hba_pkt_comp(pkt);
		}
		mutex_enter(&item->mutex);
	}
	mutex_exit(&item->mutex);
	mutex_enter(&mpt->m_doneq_mutex);
	mpt->m_doneq_thread_n--;
	cv_broadcast(&mpt->m_doneq_thread_cv);
	mutex_exit(&mpt->m_doneq_mutex);
}

/*
 * mpt interrupt handler.
 */
static uint_t
mpt_intr(caddr_t arg1, caddr_t arg2)
{
	mpt_t *mpt = (void *)arg1;
	uint32_t reply;

	NDBG1(("mpt_intr: arg1 0x%p arg2 0x%p", (void *)arg1, (void *)arg2));

	mutex_enter(&mpt->m_mutex);

	/*
	 * If interrupts are shared by two channels then
	 * check whether this interrupt is genuinely for this
	 * channel by making sure first the chip is in high
	 * power state.
	 */
	if ((mpt->m_options & MPT_OPT_PM) &&
	    (mpt->m_power_level != PM_LEVEL_D0)) {
		mutex_exit(&mpt->m_mutex);
		return (DDI_INTR_UNCLAIMED);
	}

	/*
	 * Read the istat register.
	 */
	if ((INTPENDING(mpt)) != 0) {

		/*
		 * read fifo until empty.
		 */
		while ((reply = MPT_GET_NEXT_REPLY(mpt)) != 0xFFFFFFFF) {
			/*
			 * process next reply frame.
			 */
			mpt_process_intr(mpt, reply);
		}
	} else {
		if (mpt->m_polled_intr) {
			mpt->m_polled_intr = 0;
			mutex_exit(&mpt->m_mutex);
			return (DDI_INTR_CLAIMED);
		}
		mutex_exit(&mpt->m_mutex);
		return (DDI_INTR_UNCLAIMED);
	}
	NDBG1(("mpt_intr complete"));

	/*
	 * If no helper threads are created, process the doneq in ISR.
	 * If helpers are created, use the doneq length as a metric to
	 * measure the load on the interrupt CPU. If it is long enough,
	 * which indicates the load is heavy, then we deliver the IO
	 * completions to the helpers.
	 * this measurement has some limitations although, it is simple
	 * and straightforward and works well for most of the cases at
	 * present.
	 */

	if (!mpt->m_doneq_thread_n ||
	    (mpt->m_doneq_len <= mpt->m_doneq_length_threshold)) {
		mpt_doneq_empty(mpt);
	} else {
		mpt_deliver_doneq_thread(mpt);
	}
	/*
	 * if the pending cmd queue isn't empty,
	 * we send them in here.
	 */
	if (mpt->m_ioc_event_cmdq != NULL) {
		mpt_send_pending_event_ack(mpt);
	}

	/*
	 * If there are queued cmd, start them now.
	 */
	if (mpt->m_waitq != NULL) {
		mpt_restart_waitq(mpt);
	}

	if (mpt->m_polled_intr) {
		mpt->m_polled_intr = 0;
	}

	mutex_exit(&mpt->m_mutex);
	return (DDI_INTR_CLAIMED);
}

/*
 * handle qfull condition
 */
static void
mpt_handle_qfull(mpt_t *mpt, mpt_cmd_t *cmd)
{
	int t = Tgt(cmd);
	mpt_slots_t *slots = mpt->m_active;

	if ((++cmd->cmd_qfull_retries > slots->m_target[t].m_qfull_retries) ||
	    (slots->m_target[t].m_qfull_retries == 0)) {
		/*
		 * We have exhausted the retries on QFULL, or,
		 * the target driver has indicated that it
		 * wants to handle QFULL itself by setting
		 * qfull-retries capability to 0. In either case
		 * we want the target driver's QFULL handling
		 * to kick in. We do this by having pkt_reason
		 * as CMD_CMPLT and pkt_scbp as STATUS_QFULL.
		 */
		mpt_set_throttle(mpt, t, DRAIN_THROTTLE);
	} else {
		if (slots->m_target[t].m_reset_delay == 0) {
			if (slots->m_target[t].m_t_throttle != CHOKE_THROTTLE)
				slots->m_target[t].m_t_throttle =
				    max((slots->m_target[t].m_t_ncmds - 2), 0);
		}

		cmd->cmd_pkt_flags |= FLAG_HEAD;
		cmd->cmd_flags &= ~(CFLAG_TRANFLAG | CFLAG_CMD_REMOVED);
		cmd->cmd_flags |= CFLAG_RETRY;

		(void) mpt_accept_pkt(mpt, cmd);

		/*
		 * when target gives queue full status with no commands
		 * outstanding (m_t_ncmds == 0), throttle is set to 0
		 * (HOLD_THROTTLE), and the queue full handling start
		 * (see psarc/1994/313); if there are commands outstanding,
		 * throttle is set to (m_t_ncmds - 2)
		 */
		if (slots->m_target[t].m_t_throttle == HOLD_THROTTLE) {
			/*
			 * By setting throttle to QFULL_THROTTLE, we
			 * avoid submitting new commands and in
			 * mpt_restart_cmd find out slots which need
			 * their throttles to be cleared.
			 */
			mpt_set_throttle(mpt, t, QFULL_THROTTLE);
			if (mpt->m_restart_cmd_timeid == 0) {
				mpt->m_restart_cmd_timeid =
				    timeout(mpt_restart_cmd, mpt,
				    slots->m_target[t].m_qfull_retry_interval);
			}
		}
	}
}

/*
 * hotplug handler for target devices and SMP targets
 *
 * State Transition Table: (Only for SSP/SATA devices)
 * -----------------------
 *
 *                   +- MPT_DR_INACTIVE
 *                  |
 *                  |       +- MPT_DR_PRE_OFFLINE_TIMEOUT
 *                  |       |
 *                  |       |       +- MPT_DR_PRE_OFFLINE_TIMEOUT_NO_CANCEL
 *                  |       |       |
 *                  |       |       |       +- MPT_DR_OFFLINE_IN_PROGRESS
 *                  |       |       |       |
 *                  |       |       |       |       +- MPT_DR_ONLINE_IN_PROGRESS
 *                  |       |       |       |       |
 *                  V       V       V       V       V
 *
 *                  S0      S1      S2      S3      S4
 * ------------------------------------------------------------
 *
 * MPI_EVENT        S4      S0      S4      S4      -
 * ADDED	          (open)
 *
 * MPI_EVENT        S1      -       -       -       S2
 * NOT_RESPONDING  (hold)
 *
 * m_dr_timeout     -       S3      S3      -       -
 * expires                 (open)   (open)
 *
 * online           -       -       S2      S3    S0|S4
 * finished
 *
 * offline          -       -       S2     S0|S3    S4
 * finished
 * ------------------------------------------------------------
 */
static void
mpt_handle_dr(void *args)
{
	m_dr_arg_t *dr_arg;
	int target, event;
	mpt_t *mpt;
	mpt_slots_t *slots;
	int circ = 0;
	int circ1 = 0;
	int address, rval = DDI_FAILURE;
	uint16_t dev_handle = 0;
	uint32_t dev_info = 0;
	uint64_t sas_wwn = 0;
	int tgt = 0;
	size_t size = sizeof (m_dr_arg_t);
	dev_info_t *dip = NULL;
	mdi_pathinfo_t *pip = NULL;

	dr_arg = (m_dr_arg_t *)args;
	mpt = (mpt_t *)dr_arg->mpt;
	event = dr_arg->event;

	/*
	 * Hold the nexus across the bus_config
	 */
	ndi_devi_enter(scsi_vhci_dip, &circ);
	ndi_devi_enter(mpt->m_dip, &circ1);
	switch (event) {
	case MPT_DR_EVENT_RECONFIG_TARGET:
		target = dr_arg->target;

		if (target >= MPT_MAX_TARGETS) {
			mpt_log(mpt, CE_WARN, "target number %d out of scope",
			    target);
			break;
		}

		/*
		 * Enumerate RAID volume in case this is SAS HBA and
		 * mpxio is enabled.
		 */
		if (MPT_IS_SAS(mpt) && (mpt->m_mpxio_enable == TRUE) &&
		    dr_arg->israid) {
			int vol = 0;
			dev_info_t *lundip = NULL;
			int bfound = 0;

			for (vol = 0; vol < MPT_MAX_RAIDVOLS; vol++) {
				mutex_enter(&mpt->m_mutex);
				slots = mpt->m_active;
				if (MPT_RAID_EXISTS(mpt, vol) &&
				    TGT_IS_RAID(mpt, vol, target)) {
					sas_wwn =
					    slots->m_raidvol[vol].m_raidwwid;
					mutex_exit(&mpt->m_mutex);
					(void) mpt_sas_config_raid(mpt, target,
					    sas_wwn, &lundip);
						bfound = 1;
						break;
				}
				mutex_exit(&mpt->m_mutex);
			}
			if (bfound == 1) {
				goto skip_sasdevice;
			}
		}

		/*
		 * Get sas device page 0 by BusTargetID to make sure if
		 * SSP/SATA end device exist.
		 */
		address = (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		    MPI_SAS_DEVICE_PGAD_FORM_SHIFT) | target;
		mutex_enter(&mpt->m_mutex);
		mpt->m_active->m_target[target].m_sas_wwn = NULL;
		mpt->m_active->m_target[target].m_deviceinfo = NULL;
		rval = mpt_get_sas_device_page0(mpt, address, &dev_handle,
		    &dev_info, &sas_wwn, &tgt);
		mutex_exit(&mpt->m_mutex);

		if (rval != DDI_SUCCESS) {
			mpt_log(mpt, CE_NOTE, "mpt_handle_dr: get sas device"
			    " page0 failed. \n");
		} else if ((dev_info & (MPI_SAS_DEVICE_INFO_SSP_TARGET |
		    MPI_SAS_DEVICE_INFO_SATA_DEVICE |
		    MPI_SAS_DEVICE_INFO_ATAPI_DEVICE)) == 0) {
			mpt_log(mpt, CE_NOTE, "mpt_handle_dr: target %d device"
			    " 0x%x is not a SAS/SATA device. \n", tgt,
			    dev_info);
			rval = DDI_FAILURE;
		}

		if (tgt != target) {
			rval = DDI_FAILURE;
			mpt_log(mpt, CE_WARN, "mpt_handle_dr:tgt != target, "
			    "previous hotplug operation possibly yet to"
			    "complete?");
		}

		if (sas_wwn == 0) {
			/*
			 * Direct Attached SATA device get to there
			 * adjust the sas_wwn
			 */
			sas_wwn = mpt_get_sata_device_name(mpt, target);
		}

		if (rval == DDI_SUCCESS) {
			mpt_sas_config_target(mpt, target, dev_info, sas_wwn);
			if (dr_arg->israid == 0) {
				mpt_gen_sysevent(mpt, target, SE_HINT_INSERT);
			}
		}

skip_sasdevice:
		mutex_enter(&mpt->m_mutex);
		slots = mpt->m_active;

		if (slots->m_target[target].m_dr_online_dups)
			slots->m_target[target].m_dr_online_dups--;

		/*
		 * We enter this code because a DR operation is in
		 * progress.
		 * m_dr_flag should not be reset under the following
		 * conditions:
		 * 1. If m_dr_flag is not MPT_DR_ONLINE_IN_PROGRESS,
		 *    an offline has been dispatched behind us. It
		 *    should clear the state.
		 * 2. If m_dr_online_dups is not zero, another online
		 *    is in the taskq after this. The last online should
		 *    clear the state.
		 */
		if ((slots->m_target[target].m_dr_flag ==
		    MPT_DR_ONLINE_IN_PROGRESS) &&
		    (slots->m_target[target].m_dr_online_dups == 0)) {
			slots->m_target[target].m_dr_flag =
			    MPT_DR_INACTIVE;
		}
		mutex_exit(&mpt->m_mutex);
		break;

	case MPT_DR_EVENT_OFFLINE_TARGET:
		target = dr_arg->target;
		if (target >= MPT_MAX_TARGETS) {
			mpt_log(mpt, CE_WARN, "target number %d out of scope",
			    target);
			break;
		}

		rval = mpt_sas_offline_target(mpt, target);
		if ((rval == DDI_SUCCESS) && (dr_arg->israid == 1)) {
			/*
			 * In case the target is a physical member disk
			 * of RAID volume, the path info structure should
			 * be freed.
			 * The path info of the first target device has the
			 * same address with the RAID volume, which will
			 * mislead configure operation of the RAID volume
			 * to the path info node of the first physcial
			 * disk if we do not free path info structure here.
			 */
			pip = mpt_find_path(mpt, target, 0);
			if (pip != NULL) {
				if (MDI_PI_IS_OFFLINE(pip)) {
					(void) mdi_pi_free(pip, 0);
				}
			}
		}

		if (dr_arg->israid == 0) {
			mpt_gen_sysevent(mpt, target, SE_HINT_REMOVE);
		}

		mutex_enter(&mpt->m_mutex);
		slots = mpt->m_active;
		slots->m_target[target].m_sas_wwn = NULL;
		slots->m_target[target].m_deviceinfo = NULL;
		/*
		 * m_dr_flag should not be cleared in the following conditions:
		 * 1. m_dr_flag is not MPT_DR_OFFLINE_IN_PROGRESS, an online
		 *    has been dispatched. It should clear the state.
		 * 2. m_dr_offline_dups is not zero, another offline is in the
		 *    taskq behind us. The last one should clear the state.
		 */
		if (slots->m_target[target].m_dr_offline_dups)
			slots->m_target[target].m_dr_offline_dups--;
		if ((slots->m_target[target].m_dr_flag ==
		    MPT_DR_OFFLINE_IN_PROGRESS) &&
		    (slots->m_target[target].m_dr_offline_dups == 0)) {
			slots->m_target[target].m_dr_flag = MPT_DR_INACTIVE;
		}

		mutex_exit(&mpt->m_mutex);
		break;

	case MPT_DR_EVENT_RECONFIG_SMP:
	{
		m_smp_dr_arg_t *smp_args = (m_smp_dr_arg_t *)args;
		size = sizeof (m_smp_dr_arg_t);
		sas_wwn = smp_args->wwn;

		if (mpt_sas_config_smp(mpt, sas_wwn, &dip) != DDI_SUCCESS) {
			mpt_log(mpt, CE_WARN, "!failed to configure smp "
			    "w%"PRIx64, sas_wwn);
		}

		break;
	}
	case MPT_DR_EVENT_OFFLINE_SMP:
	{
		m_smp_dr_arg_t *smp_args = (m_smp_dr_arg_t *)args;
		struct smp_device smp_sd;

		size = sizeof (m_smp_dr_arg_t);
		sas_wwn = smp_args->wwn;

		/* XXX driver should not allocate its own smp_device */
		bzero(&smp_sd, sizeof (struct smp_device));
		smp_sd.smp_sd_address.smp_a_hba_tran = mpt->m_smptran;
		bcopy(&sas_wwn,
		    smp_sd.smp_sd_address.smp_a_wwn, SAS_WWN_BYTE_SIZE);

		/*
		 * Try to probe the expander again. For multiple connections
		 * from the HBA to the expander, the expander can only be
		 * offlined after all connections are lost. If there still
		 * exists one or more online connections, the probe shall return
		 * success and the offline operation should be skipped.
		 */
		if (smp_probe(&smp_sd) != DDI_PROBE_SUCCESS) {
			(void) mpt_sas_offline_smp(mpt, sas_wwn,
			    NDI_DEVI_REMOVE);
		}

		break;
	}
	default:
		break;
	}
	ndi_devi_exit(mpt->m_dip, circ1);
	ndi_devi_exit(scsi_vhci_dip, circ);

	kmem_free(args, size);
}

/*
 * Record the event if its type is enabled in mpt instance by ioctl.
 */
static void
mpt_record_event(void *args)
{
	m_replyh_arg_t *replyh_arg;
	msg_event_notify_reply_t *eventreply;
	uint32_t event, rfm;
	mpt_t *mpt;
	int i;

	replyh_arg = (m_replyh_arg_t *)args;
	rfm = replyh_arg->rfm;
	mpt = replyh_arg->mpt;

	eventreply = (msg_event_notify_reply_t *)
	    (mpt->m_reply_fifo + (rfm - mpt->m_reply_addr));
	event = ddi_get32(mpt->m_reply_acc_h, &eventreply->Event);

	if ((event < HW_NUM_EVENT_TYPES) && (mpt->m_event_types_enabled &
	    (1 << event))) {
		i = mpt->m_event_number % HW_NUM_EVENT_ENTRIES;
		mpt->m_events[i].Type = event;
		mpt->m_events[i].Number = ++mpt->m_event_number;

		if (eventreply->EventDataLength > 0) {
			mpt->m_events[i].Data[0] = ddi_get32(mpt->m_reply_acc_h,
			    &eventreply->Data[0]);
		} else {
			mpt->m_events[i].Data[0] = 0;
		}
		if (eventreply->EventDataLength > 1) {
			mpt->m_events[i].Data[1] = ddi_get32(mpt->m_reply_acc_h,
			    &eventreply->Data[1]);
		} else {
			mpt->m_events[i].Data[1] = 0;
		}
	}
}

/*
 * handle sync events from ioc in interrupt
 * return value:
 * DDI_SUCCESS: The event is handled by this func
 * DDI_FAILURE: Event is not handled
 */
static int
mpt_handle_event_sync(void *args)
{
	m_replyh_arg_t *replyh_arg;
	msg_event_notify_reply_t *eventreply;
	uint32_t event, rfm;
	mpt_t *mpt;
	mpt_slots_t *slots;

	replyh_arg = (m_replyh_arg_t *)args;
	rfm = replyh_arg->rfm;
	mpt = replyh_arg->mpt;
	slots = mpt->m_active;

	ASSERT(mutex_owned(&mpt->m_mutex));

	eventreply = (msg_event_notify_reply_t *)
	    (mpt->m_reply_fifo + (rfm - mpt->m_reply_addr));
	event = ddi_get32(mpt->m_reply_acc_h, &eventreply->Event);

	(void) mpt_handle_ioc_status(mpt, mpt->m_reply_acc_h,
	    &eventreply->IOCStatus, &eventreply->IOCLogInfo,
	    "mpt_handle_event_sync", 1);

	/*
	 * figure out what kind of event we got and handle accordingly
	 */
	switch (event) {
	case MPI_EVENT_SAS_DEVICE_STATUS_CHANGE:
	{
		event_data_sas_device_status_change_t *statuschange;
		uint8_t rc;
		uint16_t targ;
		int revent;
		m_dr_arg_t *args;
		uint64_t wwn = 0;
		uint32_t wwn_lo, wwn_hi;

		revent = 0;
		statuschange = (event_data_sas_device_status_change_t *)
		    eventreply->Data;
		targ = BUSTARG_TO_BT(
		    ddi_get8(mpt->m_reply_acc_h, &statuschange->Bus),
		    ddi_get8(mpt->m_reply_acc_h, &statuschange->TargetID));
		rc = ddi_get8(mpt->m_reply_acc_h, &statuschange->ReasonCode);
		wwn_lo = ddi_get32(mpt->m_reply_acc_h,
		    (uint32_t *)&statuschange->SASAddress);
		wwn_hi = ddi_get32(mpt->m_reply_acc_h,
		    (uint32_t *)&statuschange->SASAddress + 1);
		wwn = ((uint64_t)wwn_hi << 32) | wwn_lo;
		NDBG13(("MPI_EVENT_SAS_DEVICE_STATUS_CHANGE wwn is %"PRIx64,
		    wwn));

		switch (rc) {
		case MPI_EVENT_SAS_DEV_STAT_RC_ADDED:
			NDBG20(("mpt%d target %d added",
			    mpt->m_instance, targ));
			if (mpt->m_active->m_target[targ].m_sas_wwn &&
			    (mpt->m_active->m_target[targ].m_sas_wwn != wwn)) {
				/*
				 * The device at this target has changed.
				 * This situation will need dealing with in
				 * the future.
				 */
				mpt_log(mpt, CE_WARN, "wwn for target has "
				    "changed");
			}
			if (slots->m_target[targ].m_dr_flag &
			    MPT_DR_PRE_OFFLINE_TIMEOUT) {
				/*
				 * If we were timing out and the device comes
				 * back we just clear the state and get things
				 * going again.
				 */
				slots->m_target[targ].m_dr_flag =
				    MPT_DR_INACTIVE;
				mpt_set_throttle(mpt, targ, DRAIN_THROTTLE);
				NDBG20(("Cancel the offline schedule"));
			} else {
				slots->m_target[targ].m_dr_flag =
				    MPT_DR_ONLINE_IN_PROGRESS;
				slots->m_target[targ].m_dr_online_dups++;
				revent = MPT_DR_EVENT_RECONFIG_TARGET;
			}
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_NOT_RESPONDING:

			if (slots->m_target[targ].m_dr_flag ==
			    MPT_DR_INACTIVE) {
				/*
				 * The I-T nexus for target[targ] lost
				 * hold throttle
				 */
				mpt_set_throttle(mpt, targ, HOLD_THROTTLE);
				slots->m_target[targ].m_dr_flag =
				    MPT_DR_PRE_OFFLINE_TIMEOUT;
			} else {
				/*
				 * This condition means a NOT_RESPONDING event
				 * interrupted the online procedure. That means
				 * it didn't complete so we use a special case
				 * of the OFFLINE_TIMEOUT that will schedule a
				 * proper online if the devices comes back
				 * before the timer expires.
				 * If we hold throttle here, the online
				 * procedure which is interrupted by the
				 * NOT_RESPONDING will hang, so we don't hold
				 * throttle.
				 */
				slots->m_target[targ].m_dr_flag =
				    MPT_DR_PRE_OFFLINE_TIMEOUT_NO_CANCEL;
			}
			NDBG20(("mpt%d target %d no responding",
			    mpt->m_instance, targ));
			slots->m_target[targ].m_dr_timeout =
			    mpt->m_offline_delay;
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_SMART_DATA:
		case MPI_EVENT_SAS_DEV_STAT_RC_NO_PERSIST_ADDED:
		default:
			break;
		}
		if (revent != MPT_DR_EVENT_RECONFIG_TARGET)
			break;

		args = kmem_zalloc(sizeof (m_dr_arg_t), KM_SLEEP);
		args->mpt = (void *)mpt;
		args->target = targ;
		args->event = revent;
		args->israid = 0;
		if ((ddi_taskq_dispatch(mpt->m_dr_taskq,
		    mpt_handle_dr,
		    (void *)args, DDI_NOSLEEP)) != DDI_SUCCESS) {
			mpt_log(mpt, CE_NOTE, "mpt start taskq for handle"
			    "SAS target reconfigure event failed. \n");
		}
		break;
	}
	case MPI_EVENT_SAS_EXPANDER_STATUS_CHANGE:
	{
		event_data_sas_expander_status_change_t *statuschange;
		uint8_t rc;
		int revent;
		m_smp_dr_arg_t *smp_args;
		uint64_t wwn = 0;
		uint32_t wwn_lo, wwn_hi;

		revent = 0;
		statuschange = (event_data_sas_expander_status_change_t *)
		    eventreply->Data;
		rc = ddi_get8(mpt->m_reply_acc_h, &statuschange->ReasonCode);
		wwn_lo = ddi_get32(mpt->m_reply_acc_h,
		    (uint32_t *)&statuschange->SASAddress);
		wwn_hi = ddi_get32(mpt->m_reply_acc_h,
		    (uint32_t *)&statuschange->SASAddress + 1);
		wwn = ((uint64_t)wwn_hi << 32) | wwn_lo;
		NDBG20(("MPI_EVENT_SAS_EXPANDER_STATUS_CHANGE wwn is %"PRIx64,
		    wwn));

		switch (rc) {
		case MPI_EVENT_SAS_EXP_RC_ADDED:
			revent = MPT_DR_EVENT_RECONFIG_SMP;
			break;
		case MPI_EVENT_SAS_EXP_RC_NOT_RESPONDING:
			revent = MPT_DR_EVENT_OFFLINE_SMP;
			break;
		default:
			break;
		}
		if (revent == 0)
			break;

		smp_args = kmem_zalloc(sizeof (m_smp_dr_arg_t), KM_SLEEP);
		smp_args->mpt = (void *)mpt;
		smp_args->event = revent;
		smp_args->wwn = wwn;

		if ((ddi_taskq_dispatch(mpt->m_dr_taskq,
		    mpt_handle_dr,
		    (void *)smp_args, DDI_NOSLEEP)) != DDI_SUCCESS) {
			mpt_log(mpt, CE_NOTE, "mpt start taskq for handle"
			    "SMP target reconfigure event failed. \n");
		}
		break;
	}
	default:
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * handle events from ioc
 */
static void
mpt_handle_event(void *args)
{
	m_replyh_arg_t *replyh_arg;
	msg_event_notify_reply_t *eventreply;
	uint32_t event, eventcntx, iocloginfo, rfm;
	uint8_t ackreq;
	mpt_t *mpt;

	replyh_arg = (m_replyh_arg_t *)args;
	rfm = replyh_arg->rfm;
	mpt = replyh_arg->mpt;

	mutex_enter(&mpt->m_mutex);

	eventreply = (msg_event_notify_reply_t *)
	    (mpt->m_reply_fifo + (rfm - mpt->m_reply_addr));
	event = ddi_get32(mpt->m_reply_acc_h, &eventreply->Event);
	eventcntx = ddi_get32(mpt->m_reply_acc_h, &eventreply->EventContext);
	ackreq = ddi_get8(mpt->m_reply_acc_h, &eventreply->AckRequired);

	(void) mpt_handle_ioc_status(mpt, mpt->m_reply_acc_h,
	    &eventreply->IOCStatus, &eventreply->IOCLogInfo,
	    "mpt_handle_event", 1);

	/*
	 * figure out what kind of event we got and handle accordingly
	 */
	switch (event) {
	case MPI_EVENT_NONE:
		/*
		 * the following events are implemented in the
		 * currently supported revision of the firmware, but
		 * are not used.  catch these and treat them as
		 * MPI_EVENT_NONE
		 */
	case MPI_EVENT_IR_RESYNC_UPDATE:
	case MPI_EVENT_IR2:
	case MPI_EVENT_SAS_DISCOVERY:
	case MPI_EVENT_LOG_ENTRY_ADDED:
	case MPI_EVENT_QUEUE_FULL:
		/*
		 * Firmware handle queue full automaticly
		 */
		break;
	case MPI_EVENT_LOG_DATA:
		iocloginfo = ddi_get32(mpt->m_reply_acc_h,
		    &eventreply->IOCLogInfo);
		NDBG20(("mpt %d log info %x received.\n", mpt->m_instance,
		    iocloginfo));
		break;
	case MPI_EVENT_STATE_CHANGE:
		NDBG20(("mpt%d state change.", mpt->m_instance));
		break;
	case MPI_EVENT_UNIT_ATTENTION:
		NDBG20(("mpt%d unit attention.", mpt->m_instance));
		break;
	case MPI_EVENT_IOC_BUS_RESET:
		iocloginfo = ddi_get32(mpt->m_reply_acc_h,
		    &eventreply->IOCLogInfo);

		/*
		 * setup the reset delay
		 */
		if (!ddi_in_panic() && !mpt->m_suspended) {
			mpt_setup_bus_reset_delay(mpt);
		} else {
			drv_usecwait(mpt->m_scsi_reset_delay * 1000);
		}

		mpt_log(mpt, CE_NOTE, "got firmware SCSI bus reset.\n"
		    "log info = %x\n", iocloginfo);
		break;
	case MPI_EVENT_EXT_BUS_RESET:
		/*
		 * setup the reset delay
		 */
		if (!ddi_in_panic() && !mpt->m_suspended) {
			mpt_setup_bus_reset_delay(mpt);
		} else {
			drv_usecwait(mpt->m_scsi_reset_delay * 1000);
		}

		mpt_log(mpt, CE_NOTE, "got external SCSI bus reset.\n");
		break;
	case MPI_EVENT_RESCAN:
	{
		uint8_t port;
		event_data_scsi_t *datascsi;

		datascsi = (event_data_scsi_t *)eventreply->Data;
		port = ddi_get8(mpt->m_reply_acc_h, &datascsi->BusPort);
		NDBG20(("mpt%d rescan for port %d.",
		    mpt->m_instance, port));
		break;
	}
	case MPI_EVENT_LINK_STATUS_CHANGE:
	{
		uint8_t port, state;
		event_data_link_status_t *datalink;

		datalink = (event_data_link_status_t *)eventreply->Data;
		port = ddi_get8(mpt->m_reply_acc_h, &datalink->Port);
		state = ddi_get8(mpt->m_reply_acc_h, &datalink->State);

		if (state == MPI_EVENT_LINK_STATUS_FAILURE) {
			NDBG20(("mpt%d port %d offline.",
			    mpt->m_instance, port));
		} else {
			NDBG20(("mpt%d port %d online.",
			    mpt->m_instance, port));
		}
		break;
	}
	case MPI_EVENT_LOOP_STATE_CHANGE:
	{
		event_data_loop_state_t *dataloop;
		uint8_t port, type, d_id, s_id;

		dataloop = (event_data_loop_state_t *)eventreply->Data;
		port = ddi_get8(mpt->m_reply_acc_h, &dataloop->Port);
		type = ddi_get8(mpt->m_reply_acc_h, &dataloop->Type);
		d_id = ddi_get8(mpt->m_reply_acc_h, &dataloop->Character3);
		s_id = ddi_get8(mpt->m_reply_acc_h, &dataloop->Character4);

		NDBG20(("mpt%d loop state change for port %d,"
		    " type %x, D_ID = %x, S_ID = %x",
		    mpt->m_instance, port, type, d_id, s_id));
		break;
	}
	case MPI_EVENT_LOGOUT:
		NDBG20(("mpt%d logout.", mpt->m_instance));
		break;
	case MPI_EVENT_EVENT_CHANGE:
		NDBG20(("mpt%d event change.", mpt->m_instance));
		break;
	case MPI_EVENT_SCSI_DEVICE_STATUS_CHANGE:
	{
		event_data_scsi_device_status_change_t *statuschange;
		uint8_t rc;
		uint16_t targ;

		statuschange = (event_data_scsi_device_status_change_t *)
		    eventreply->Data;
		targ = ddi_get8(mpt->m_reply_acc_h, &statuschange->TargetID);
		rc = ddi_get8(mpt->m_reply_acc_h, &statuschange->ReasonCode);

		switch (rc) {
		case MPI_EVENT_SCSI_DEV_STAT_RC_ADDED:
			NDBG20(("mpt%d target %d added", mpt->m_instance,
			    targ));
			break;
		case MPI_EVENT_SCSI_DEV_STAT_RC_NOT_RESPONDING:
			NDBG20(("mpt%d target %d not responding",
			    mpt->m_instance, targ));
			break;
		case MPI_EVENT_SCSI_DEV_STAT_RC_SMART_DATA:
		default:
			break;
		}
		break;
	}
	/*
	 * MPI_EVENT_SAS_DEVICE_STATUS_CHANGE is handled by
	 * mpt_handle_event_sync,in here just send ack message.
	 */
	case MPI_EVENT_SAS_DEVICE_STATUS_CHANGE:
		NDBG20(("mpt%d sas device change ack.", mpt->m_instance));
		break;
	case MPI_EVENT_SAS_EXPANDER_STATUS_CHANGE:
		NDBG20(("mpt%d sas expander change ack.", mpt->m_instance));
		break;

	case MPI_EVENT_SAS_PHY_LINK_STATUS:
	{
		event_data_sas_phy_link_status_t *linkstat;
		uint8_t phynum, linkrate;
		auto char buf[80];

		linkstat = (event_data_sas_phy_link_status_t *)
		    eventreply->Data;
		phynum = ddi_get8(mpt->m_reply_acc_h, &linkstat->PhyNum);
		linkrate = ddi_get8(mpt->m_reply_acc_h, &linkstat->LinkRates);

		(void) sprintf(&buf[0], "mpt%d SAS link status change\n"
		    "\tphy %d ", mpt->m_instance, phynum);
		switch (linkrate >> MPI_EVENT_SAS_PLS_LR_CURRENT_SHIFT) {
		case MPI_EVENT_SAS_PLS_LR_RATE_UNKNOWN:
			(void) sprintf(&buf[strlen(buf)], "linkrate "
			    "UNKNOWN");
			break;
		case MPI_EVENT_SAS_PLS_LR_RATE_PHY_DISABLED:
			(void) sprintf(&buf[strlen(buf)], "DISABLED");
			break;
		case MPI_EVENT_SAS_PLS_LR_RATE_FAILED_SPEED_NEGOTIATION:
			(void) sprintf(&buf[strlen(buf)], "FAILED "
			    "Negotiation");
			break;
		case MPI_EVENT_SAS_PLS_LR_RATE_SATA_OOB_COMPLETE:
			(void) sprintf(&buf[strlen(buf)], "SATA OOB"
			    " Complete");
			break;
		case MPI_EVENT_SAS_PLS_LR_RATE_1_5:
			(void) sprintf(&buf[strlen(buf)], "linkrate 1.5gbps");
			break;
		case MPI_EVENT_SAS_PLS_LR_RATE_3_0:
			(void) sprintf(&buf[strlen(buf)], "linkrate 3gbps");
			break;
		default:
			break;
		}
		NDBG20(("%s", buf));
		break;
	}
	case MPI_EVENT_PERSISTENT_TABLE_FULL:
	{
		NDBG20(("mpt%d persistent table full", mpt->m_instance));
		break;
	}
	case MPI_EVENT_ON_BUS_TIMER_EXPIRED:
	{
		event_data_scsi_t *eventdata;
		uint16_t targ;
		int t;
		struct scsi_address ap;

		eventdata = (event_data_scsi_t *)eventreply->Data;

		targ = ddi_get8(mpt->m_reply_acc_h, &eventdata->TargetID);

		/*
		 * An ereport could be issued here for the Target.
		 */

		mpt_log(mpt, CE_WARN, "Connected command timeout for"
		    " Target %d.\n", targ);
		NDBG20(("mpt%d target %d timer expired", mpt->m_instance,
		    targ));
		if (MPT_IS_SCSI(mpt)) {
			mpt_sync_wide_backoff(mpt, targ);
		}

		/*
		 * Reset the bus if we aren't already in the middle of a reset
		 */
		if (mpt->m_active->m_slot[MPT_PROXY_SLOT(mpt)] == NULL) {
			ap.a_hba_tran = mpt->m_tran;
			ap.a_target = (ushort_t)targ;
			ap.a_lun = 0;
			if (mpt_do_scsi_reset(&ap, RESET_ALL) == FAILED) {
				/*
				 * If the reset of all targets failed we're
				 * in trouble. Set all throttles to
				 * CHOKE_THROTTLE to stop accepting commands.
				 * Then flush the commands to force packets to
				 * complete with errors.
				 */
				mpt_log(mpt, CE_WARN, "Rejecting future "
				    "commands");
				for (t = 0; t < mpt->m_ntargets; t++)
					mpt_set_throttle(mpt, t,
					    CHOKE_THROTTLE);
				mpt_flush_hba(mpt);
				mpt_doneq_empty(mpt);
			}
		}
		break;
	}
	case MPI_EVENT_SAS_DISCOVERY_ERROR:
	{
		event_data_sas_discovery_error_t *discerr;
		uint32_t discovery_status;
		uint8_t port;
		int n;
		char buf[MPT_ERROR_LOG_LEN];



		discerr = (event_data_sas_discovery_error_t *)eventreply->Data;
		discovery_status = ddi_get32(mpt->m_reply_acc_h,
		    &discerr->DiscoveryStatus);
		port = ddi_get8(mpt->m_reply_acc_h, &discerr->Port);
		bzero(buf, MPT_ERROR_LOG_LEN);
		n = snprintf(buf, MPT_ERROR_LOG_LEN, "DiscoveryStatus is ");
		if (n < 0 || n >= MPT_ERROR_LOG_LEN) {
			goto error_end;
		}
		if (discovery_status & MPI_EVENT_SAS_DE_DS_LOOP_DETECTED) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|Loop detected|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status &
		    MPI_EVENT_SAS_DE_DS_UNADDRESSABLE_DEVICE) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|Unaddressable "
			    "device found|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status & MPI_EVENT_SAS_DE_DS_MULTIPLE_PORTS) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|Multiple ports "
			    "with the same SAS address were found|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status & MPI_EVENT_SAS_DE_DS_EXPANDER_ERR) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|Expander"
			    " error|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status & MPI_EVENT_SAS_DE_DS_SMP_TIMEOUT) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|SMP timeout|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status & MPI_EVENT_SAS_DE_DS_OUT_ROUTE_ENTRIES) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|Expander route "
			    "table out of entries|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status & MPI_EVENT_SAS_DE_DS_INDEX_NOT_EXIST) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|Route table "
			    "index does not exist|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status &
		    MPI_EVENT_SAS_DE_DS_SMP_FUNCTION_FAILED) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|SMP function "
			    "failed|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status & MPI_EVENT_SAS_DE_DS_SMP_CRC_ERR) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|SMP CRC error|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status &
		    MPI_EVENT_SAS_DE_DS_MULTIPLE_SUBTRACTIVE) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|Multiple "
			    "subtractive detected|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status & MPI_EVENT_SAS_DE_DS_TABLE_TO_TABLE) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|Table-to-table "
			    "detected|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status & MPI_EVENT_SAS_DE_DS_MULTIPLE_PATHS) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|Multiple paths "
			    "detected|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}
		if (discovery_status & MPI_EVENT_SAS_DE_DS_MAX_SATA_TARGS) {
			n = snprintf(buf + strlen(buf),
			    MPT_ERROR_LOG_LEN - strlen(buf), "|Maximum number "
			    "of supported SATA targets reached|");
			if (n < 0 || n >= (MPT_ERROR_LOG_LEN - strlen(buf))) {
				goto error_end;
			}
		}

		mpt_log(mpt, CE_WARN, "!SAS Discovery Error on "
		    "port %d. DiscoveryStatus is %s\n", port, buf);
		break;

error_end:
		mpt_log(mpt, CE_WARN,
		    "!SAS Discovery log buffer overflow for "
		    "port %d. DiscoveryStatus is %s\n", port, buf);
		break;
	}
	case MPI_EVENT_INTEGRATED_RAID:
	{
		uint16_t reason, volid, physdisknum;
		int vol;
		uint32_t settings;
		event_data_raid_t *dataraid;
		auto char buf[80];
		mpt_slots_t *slots = mpt->m_active;
		m_dr_arg_t *dr_args;

		dataraid = (event_data_raid_t *)eventreply->Data;
		reason = ddi_get8(mpt->m_reply_acc_h, &dataraid->ReasonCode);

		if (MPT_IS_SAS(mpt)) {
			volid = BUSTARG_TO_BT(
			    ddi_get8(mpt->m_reply_acc_h, &dataraid->VolumeBus),
			    ddi_get8(mpt->m_reply_acc_h, &dataraid->VolumeID));
		} else {
			volid = ddi_get8(mpt->m_reply_acc_h,
			    &dataraid->VolumeID);
		}

		settings = ddi_get32(mpt->m_reply_acc_h,
		    &dataraid->SettingsStatus);
		physdisknum = ddi_get8(mpt->m_reply_acc_h,
		    &dataraid->PhysDiskNum);
		/*
		 * determine which slot this raid volume is in
		 */
		for (vol = 0; vol < MPT_MAX_RAIDVOLS; vol++) {
			if (slots->m_raidvol[vol].m_raidtarg == volid)
				break;
		}

		switch (reason) {
		case MPI_EVENT_RAID_RC_VOLUME_CREATED:
			if (!slots->m_raidvol[vol].m_raidbuilding) {
				mpt_log(mpt, CE_NOTE, "Volume %d created.\n",
				    volid);
				/*
				 * Refresh our idea of volume state/status and
				 * various physical disk related values.
				 */
				(void) mpt_get_raid_info(mpt);
			}

			/*
			 * In case that mpxio is enabled on MPT SAS adapter,
			 * the created integrated array might not reuse the
			 * device node of first member physical disk, because
			 * the physical disk might be enumerated under scsi_vhci
			 * node as long as it is supported by scsi_vhci. But the
			 * RAID volume will always be enumerated under its
			 * parent MPT node.
			 * To handle this case, we need enumerate a new device
			 * node for RAID volume under MPT node after it is
			 * created.
			 */

			if (MPT_IS_SAS(mpt) && (mpt->m_mpxio_enable == TRUE)) {
				dr_args = kmem_zalloc(sizeof (m_dr_arg_t),
				    KM_SLEEP);
				dr_args->mpt = (void *)mpt;
				dr_args->target = volid;
				dr_args->event = MPT_DR_EVENT_RECONFIG_TARGET;
				dr_args->israid = 1;
				mpt_set_throttle(mpt, volid, MAX_THROTTLE);
				slots->m_target[volid].m_dr_flag =
				    MPT_DR_ONLINE_IN_PROGRESS;
				slots->m_target[volid].m_dr_online_dups++;
				if ((ddi_taskq_dispatch(mpt->m_dr_taskq,
				    mpt_handle_dr,
				    (void *)dr_args, DDI_NOSLEEP))
				    != DDI_SUCCESS) {
					mpt_log(mpt, CE_NOTE, "mpt start taskq "
					    "to online RAID volume %u "
					    "failed.\n", volid);
				}
			}

			break;
		case MPI_EVENT_RAID_RC_VOLUME_DELETED:
			/*
			 * In the case of a MPT SAS adapter, the
			 * device node of the RAID volume needs be offlined and
			 * removed after it is deleted.
			 */
			if (MPT_IS_SAS(mpt)) {
				dr_args = kmem_zalloc(sizeof (m_dr_arg_t),
				    KM_SLEEP);
				dr_args->mpt = (void *)mpt;
				dr_args->target = volid;
				dr_args->event = MPT_DR_EVENT_OFFLINE_TARGET;
				dr_args->israid = 1;
				slots->m_target[volid].m_dr_flag =
				    MPT_DR_OFFLINE_IN_PROGRESS;
				slots->m_target[volid].m_dr_offline_dups++;
				if ((ddi_taskq_dispatch(mpt->m_dr_taskq,
				    mpt_handle_dr,
				    (void *)dr_args, DDI_NOSLEEP)) !=
				    DDI_SUCCESS) {
					mpt_log(mpt, CE_NOTE, "mpt start taskq "
					    "to offline RAID volume %u "
					    "failed.\n", volid);
				}
				mpt_set_throttle(mpt, volid, DRAIN_THROTTLE);
				mpt_restart_hba(mpt);
			}

			if (!slots->m_raidvol[vol].m_raidbuilding) {
				mpt_log(mpt, CE_NOTE, "Volume %d deleted.\n",
				    volid);

				/* Config up all SAS/SATA disks again */
				if (MPT_IS_SAS(mpt)) {
					mpt->m_done_bus_config_all = 0;
					mutex_exit(&mpt->m_mutex);
					mpt_sas_config_all(mpt);
					mutex_enter(&mpt->m_mutex);
				}

				slots->m_raidvol[vol].m_israid = 0;
				slots->m_raidvol[vol].m_ndisks = 0;
			}
			break;
		case MPI_EVENT_RAID_RC_VOLUME_SETTINGS_CHANGED:
			mpt_log(mpt, CE_NOTE, "Volume %d settings changed.\n",
			    volid);
			break;
		case MPI_EVENT_RAID_RC_VOLUME_STATUS_CHANGED:
			/*
			 * Get the current settings of the raid volume.
			 * The settings data contains two fields of info.
			 * First byte has the status and the second has the
			 * state.  We first figure out the status of our
			 * volume and then the state
			 */
			slots->m_raidvol[vol].m_state = 0;
			slots->m_raidvol[vol].m_flags = 0;
			(void) sprintf(&buf[0], "Volume %d is ", volid);
			if (settings & MPI_RAIDVOL0_STATUS_FLAG_ENABLED) {
				slots->m_raidvol[vol].m_flags |=
				    MPI_RAIDVOL0_STATUS_FLAG_ENABLED;
				(void) sprintf(&buf[strlen(buf)], "|enabled|");
			}
			if (settings & MPI_RAIDVOL0_STATUS_FLAG_QUIESCED) {
				slots->m_raidvol[vol].m_flags |=
				    MPI_RAIDVOL0_STATUS_FLAG_QUIESCED;
				(void) sprintf(&buf[strlen(buf)], "|quiesced|");
			}
			if (settings &
			    MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS) {
				slots->m_raidvol[vol].m_flags |=
				    MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS;
				(void) sprintf(&buf[strlen(buf)],
				    "|resyncing|");
			}
			switch ((settings >> 8) & 0xFF) {
			case MPI_RAIDVOL0_STATUS_STATE_OPTIMAL:
				(void) sprintf(&buf[strlen(buf)], "|optimal|");
				break;
			case MPI_RAIDVOL0_STATUS_STATE_DEGRADED:
				slots->m_raidvol[vol].m_state |=
				    MPI_RAIDVOL0_STATUS_STATE_DEGRADED;
				(void) sprintf(&buf[strlen(buf)], "|degraded|");
				break;
			case MPI_RAIDVOL0_STATUS_STATE_FAILED:
				slots->m_raidvol[vol].m_state |=
				    MPI_RAIDVOL0_STATUS_STATE_FAILED;
				(void) sprintf(&buf[strlen(buf)], "|failed|");
				break;
			case MPI_RAIDVOL0_STATUS_STATE_MISSING:
				slots->m_raidvol[vol].m_state |=
				    MPI_RAIDVOL0_STATUS_STATE_MISSING;
				(void) sprintf(&buf[strlen(buf)], "|missing|");
				break;
			default:
				(void) sprintf(&buf[strlen(buf)], "|unknown|");
				break;
			}
			if (!slots->m_raidvol[vol].m_raidbuilding) {
				mpt_log(mpt, CE_NOTE, "%s\n", buf);
			}
			break;
		case MPI_EVENT_RAID_RC_VOLUME_PHYSDISK_CHANGED:
			mpt_log(mpt, CE_NOTE, "One or more disks on volume"
			    " %d changed.\n", volid);
			break;
		case MPI_EVENT_RAID_RC_PHYSDISK_CREATED:
			mpt_log(mpt, CE_NOTE, "Physical disk %d"
			    " created.\n", physdisknum);
			/*
			 * In case that mpxio is enabled on MPT SAS adapter,
			 * to prohibit access to each physical member disk as a
			 * separate device, all pathes to the physical member
			 * disk should be offlined.
			 */
			if (MPT_IS_SAS(mpt) && (mpt->m_mpxio_enable == TRUE)) {
				dr_args = kmem_zalloc(sizeof (m_dr_arg_t),
				    KM_SLEEP);
				dr_args->mpt = (void *)mpt;
				dr_args->target = volid;
				dr_args->event = MPT_DR_EVENT_OFFLINE_TARGET;
				dr_args->israid = 1;
				/*
				 * Set MPT_DR_OFFLINE_IN_PROGRESS because we
				 * are dispatching the offline.
				 */
				slots->m_target[volid].m_dr_flag =
				    MPT_DR_OFFLINE_IN_PROGRESS;
				slots->m_target[volid].m_dr_offline_dups++;
				NDBG20(("mpt%d scheduled offline for"
				    " target:%d", mpt->m_instance, volid));
				if ((ddi_taskq_dispatch(mpt->m_dr_taskq,
				    mpt_handle_dr,
				    (void *)dr_args, DDI_NOSLEEP)) !=
				    DDI_SUCCESS) {
					mpt_log(mpt, CE_NOTE, "mpt start taskq "
					    "to offline SAS target %u "
					    "failed.\n", volid);
				}
				mpt_set_throttle(mpt, volid, DRAIN_THROTTLE);
				mpt_restart_hba(mpt);
			}
			break;
		case MPI_EVENT_RAID_RC_PHYSDISK_DELETED:
			mpt_log(mpt, CE_NOTE, "Physical disk %d"
			    " deleted.\n", physdisknum);
			/*
			 * In case that mpxio is enabled on MPT SAS adapther,
			 * after the RAID volume is deleted, the pathes to
			 * each physical member disk should be onlined again.
			 */
			if (MPT_IS_SAS(mpt) && (mpt->m_mpxio_enable == TRUE)) {
				dr_args = kmem_zalloc(sizeof (m_dr_arg_t),
				    KM_SLEEP);
				dr_args->mpt = (void *)mpt;
				dr_args->target = volid;
				dr_args->event = MPT_DR_EVENT_RECONFIG_TARGET;
				dr_args->israid = 1;
				slots->m_target[volid].m_dr_flag =
				    MPT_DR_ONLINE_IN_PROGRESS;
				slots->m_target[volid].m_dr_online_dups++;

				/*
				 * Make sure throttle is MAX
				 */
				mpt_set_throttle(mpt, volid, MAX_THROTTLE);
				NDBG20(("mpt%d scheduled online for"
				    " target:%d", mpt->m_instance, volid));
				if ((ddi_taskq_dispatch(mpt->m_dr_taskq,
				    mpt_handle_dr,
				    (void *)dr_args, DDI_NOSLEEP))
				    != DDI_SUCCESS) {
					mpt_log(mpt, CE_NOTE, "mpt start taskq "
					    "to online SAS target %u "
					    "failed.\n", volid);
				}
			}
			break;
		case MPI_EVENT_RAID_RC_PHYSDISK_SETTINGS_CHANGED:
			mpt_log(mpt, CE_NOTE, "Physical disk %d settings "
			    "changed", physdisknum);
			break;
		case MPI_EVENT_RAID_RC_PHYSDISK_STATUS_CHANGED:
		{
			struct raid_phys_disk0_status status;
			uint16_t target;
			int disk;
			dev_info_t *tgt_dip;
			status.Flags = (settings & 0xFF);
			status.State = ((settings >> 8) & 0xFF);

			/*
			 * determine which raid volume owns this disk, and
			 * which disk it is.
			 */
			for (vol = 0; vol < MPT_MAX_RAIDVOLS; vol++) {
				for (disk = 0; disk < MPT_MAX_DISKS_IN_RAID;
				    disk++) {
					if (slots->m_raidvol[vol].
					    m_disknum[disk] == physdisknum) {
						break;
					}
				}
				if (disk < MPT_MAX_DISKS_IN_RAID)
					break;
			}
			if (vol == MPT_MAX_RAIDVOLS) {
				mpt_log(mpt, CE_NOTE, "Phydisk disk %d does "
				    "not belong to any volume", physdisknum);
				break;
			}
			volid = slots->m_raidvol[vol].m_raidtarg;
			tgt_dip = slots->m_target[volid].m_tgt_dip;

			if (MPT_IS_SAS(mpt) && status.State ==
			    MPI_PHYSDISK0_STATUS_ONLINE) {
				if (mpt_get_physdisk_settings(mpt, vol,
				    physdisknum)) {
					mpt_log(mpt, CE_WARN, "Failed getting "
					    "the settings of physical disk :%d",
					    physdisknum);
				}
			}

			target = slots->m_raidvol[vol].m_diskid[disk];
			(void) sprintf(&buf[0], "Physical disk (target %d) is ",
			    target);
			if (status.Flags &
			    MPI_PHYSDISK0_STATUS_FLAG_OUT_OF_SYNC) {
				(void) sprintf(&buf[strlen(buf)], "|out of "
				    "sync|");
			}
			if (status.Flags &
			    MPI_PHYSDISK0_STATUS_FLAG_QUIESCED) {
				(void) sprintf(&buf[strlen(buf)], "|quiesced|");
			}
			switch (status.State) {
			case MPI_PHYSDISK0_STATUS_ONLINE:
				slots->m_raidvol[vol].
				    m_diskstatus[disk] =
				    RAID_DISKSTATUS_GOOD;
				(void) sprintf(&buf[strlen(buf)], "|online|");
				break;
			case MPI_PHYSDISK0_STATUS_MISSING:
				slots->m_raidvol[vol].
				    m_diskstatus[disk] =
				    RAID_DISKSTATUS_MISSING;
				(void) sprintf(&buf[strlen(buf)], "|missing|");
				break;
			case MPI_PHYSDISK0_STATUS_NOT_COMPATIBLE:
				slots->m_raidvol[vol].
				    m_diskstatus[disk] =
				    RAID_DISKSTATUS_FAILED;
				(void) sprintf(&buf[strlen(buf)], "|not "
				    "compatible|");
				break;
			case MPI_PHYSDISK0_STATUS_FAILED:
				slots->m_raidvol[vol].
				    m_diskstatus[disk] =
				    RAID_DISKSTATUS_FAILED;
				if (tgt_dip != NULL) {
					mpt_log(mpt, CE_WARN, "mpt: target"
					    " status request failed\n");
				}
				(void) sprintf(&buf[strlen(buf)], "|failed|");
				break;
			case MPI_PHYSDISK0_STATUS_INITIALIZING:
				(void) sprintf(&buf[strlen(buf)],
				    "|initializing|");
				break;
			case MPI_PHYSDISK0_STATUS_OFFLINE_REQUESTED:
				(void) sprintf(&buf[strlen(buf)], "|offline "
				    "requested|");
				break;
			case MPI_PHYSDISK0_STATUS_FAILED_REQUESTED:
				slots->m_raidvol[vol].
				    m_diskstatus[disk] =
				    RAID_DISKSTATUS_FAILED;
				if (tgt_dip != NULL) {
					mpt_log(mpt, CE_WARN, "mpt:"
					    " target status request failed\n");
				}
				(void) sprintf(&buf[strlen(buf)], "|failed "
				    "requested|");
				break;
			case MPI_PHYSDISK0_STATUS_OTHER_OFFLINE:
				(void) sprintf(&buf[strlen(buf)], "|offline|");
				break;
			default:
				break;
			}
			if (!slots->m_raidvol[vol].m_raidbuilding) {
				mpt_log(mpt, CE_NOTE, "%s\n", buf);
			}
			break;
		}
		case MPI_EVENT_RAID_RC_DOMAIN_VAL_NEEDED:
		case MPI_EVENT_RAID_RC_SMART_DATA:
		case MPI_EVENT_RAID_RC_REPLACE_ACTION_STARTED:
		default:
			break;
		}
		break;
	}
	default:
		mpt_log(mpt, CE_NOTE, "mpt%d: unknown event %x received\n",
		    mpt->m_instance, event);
		break;
	}

	/*
	 * certain events require the driver to send and acknowledgement
	 * message back to the ioc.  We check if this is needed for this
	 * event and send the ack.
	 */
	if (ackreq == MPI_EVENT_NOTIFICATION_ACK_REQUIRED) {
		NDBG20(("mpt event notification ack sent."));
		if (mpt_send_event_ack(mpt, event, eventcntx)) {
			NDBG20(("mpt EventAck message failed."));
		}
	}
	ddi_put32(mpt->m_datap, &mpt->m_reg->m_reply_q, rfm);
	mutex_exit(&mpt->m_mutex);
}

/*
 * invoked from timeout() to restart qfull cmds with throttle == 0
 */
static void
mpt_restart_cmd(void *arg)
{
	mpt_t *mpt = arg;
	mpt_slots_t *slots;
	int t;

	mutex_enter(&mpt->m_mutex);

	slots = mpt->m_active;
	mpt->m_restart_cmd_timeid = 0;

	for (t = 0; t < mpt->m_ntargets; t++) {
		if (slots->m_target[t].m_reset_delay == 0) {
			if (slots->m_target[t].m_t_throttle == QFULL_THROTTLE) {
				mpt_set_throttle(mpt, t, MAX_THROTTLE);
			}
		}
	}
	mpt_restart_hba(mpt);
	mutex_exit(&mpt->m_mutex);
}

static void
mpt_remove_cmd(mpt_t *mpt, mpt_cmd_t *cmd)
{
	int slot;
	mpt_slots_t *slots = mpt->m_active;
	int t;

	ASSERT(cmd != NULL);
	ASSERT(cmd->cmd_queued == FALSE);

	/*
	 * proxy cmds are removed in their own routines.  Also,
	 * we don't want to modify timeout based on proxy cmds.
	 */
	if (cmd->cmd_flags & CFLAG_CMDPROXY) {
		return;
	}

	t = Tgt(cmd);
	slot = cmd->cmd_slot;

	/*
	 * remove the cmd.
	 */
	if (cmd == slots->m_slot[slot]) {
		NDBG31(("mpt_remove_cmd: removing cmd=0x%p", (void *)cmd));
		slots->m_slot[slot] = NULL;
		ASSERT((cmd->cmd_flags & CFLAG_CMD_REMOVED) == 0);
		cmd->cmd_flags |= CFLAG_CMD_REMOVED;
		mpt->m_ncmds--;

		/*
		 * only decrement per target ncmds if command
		 * has a target associated with it.
		 */
		if ((cmd->cmd_flags & CFLAG_CMDIOC) == 0) {
			slots->m_target[t].m_t_ncmds--;
		}

		/*
		 * reset throttle if we just ran an untagged command
		 * to a tagged target
		 */
		if (TAGGED(mpt, t) &&
		    (slots->m_target[t].m_t_ncmds == 0) &&
		    ((cmd->cmd_pkt_flags & FLAG_TAGMASK) == 0)) {
			mpt_set_throttle(mpt, t, MAX_THROTTLE);
		}
	}

	/*
	 * Rest of the work for pass through commands is done in
	 * mpt_do_passthru() routine
	 */

	if (cmd->cmd_flags & CFLAG_PASSTHRU)
		return;
	/*
	 * This is all we need to do for ioc commands.
	 */
	if (cmd->cmd_flags & CFLAG_CMDIOC) {
		mpt_return_to_pool(mpt, cmd);
		return;
	}

	/*
	 * Figure out what to set tag Q timeout for...
	 *
	 * Optimize: If we have duplicate's of same timeout
	 * we're using, then we'll use it again until we run
	 * out of duplicates.  This should be the normal case
	 * for block and raw I/O.
	 * If no duplicates, we have to scan through tag que and
	 * find the longest timeout value and use it.  This is
	 * going to take a while...
	 */
	if (cmd->cmd_pkt->pkt_time == slots->m_target[t].m_timebase) {
		if (--(slots->m_target[t].m_dups) == 0) {
			if (slots->m_target[t].m_t_ncmds) {
				mpt_cmd_t *ssp;
				uint_t n = 0;
				ushort_t nslots = slots->m_n_slots;
				ushort_t i;
				/*
				 * This crude check assumes we don't do
				 * this too often which seems reasonable
				 * for block and raw I/O.
				 */
				for (i = 0; i < nslots; i++) {
					ssp = slots->m_slot[i];
					if (ssp && (Tgt(ssp) == t) &&
					    (ssp->cmd_pkt->pkt_time > n)) {
						n = ssp->cmd_pkt->pkt_time;
						slots->m_target[t].m_dups = 1;
					} else if (ssp && (Tgt(ssp) == t) &&
					    (ssp->cmd_pkt->pkt_time == n)) {
						slots->m_target[t].m_dups++;
					}
				}
				slots->m_target[t].m_timebase = n;
			} else {
				slots->m_target[t].m_dups = 0;
				slots->m_target[t].m_timebase = 0;
			}
		}
	}
	slots->m_target[t].m_timeout = slots->m_target[t].m_timebase;

	ASSERT(cmd != slots->m_slot[cmd->cmd_slot]);
}

/*
 * accept all cmds on the tx_waitq if any and then
 * start a fresh request from the top of the device queue.
 *
 * since there are always cmds queued on the tx_waitq, and rare cmds on
 * the instance waitq, so this function should not be invoked in the ISR,
 * the mpt_restart_waitq() is invoked in the ISR instead. otherwise, the
 * burden belongs to the IO dispatch CPUs is moved the interrupt CPU.
 */
static void
mpt_restart_hba(mpt_t *mpt)
{
	ASSERT(mutex_owned(&mpt->m_mutex));

	mutex_enter(&mpt->m_waitq_mutex);
	if (mpt->m_tx_waitq) {
		mpt_accept_tx_waitq(mpt);
	}
	mutex_exit(&mpt->m_waitq_mutex);
	mpt_restart_waitq(mpt);
}

/*
 * start a fresh request from the top of the device queue
 */
static void
mpt_restart_waitq(mpt_t *mpt)
{
	mpt_cmd_t *cmd, *next_cmd;
	mpt_slots_t *slots = mpt->m_active;
	int t;

	NDBG1(("mpt_restart_waitq: mpt=0x%p", (void *)mpt));

	ASSERT(mutex_owned(&mpt->m_mutex));

	/*
	 * If all cmds drained from the target, back to full
	 * throttle and start submitting new cmds again.
	 */
	for (t = 0; t < mpt->m_ntargets; t++) {
		if (slots->m_target[t].m_t_throttle == DRAIN_THROTTLE &&
		    slots->m_target[t].m_t_ncmds == 0) {
			mpt_set_throttle(mpt, t, MAX_THROTTLE);
		}
	}

	/*
	 * If mpt_quiesce_delay is enabled than don't
	 * start any commands.
	 */
	if (mpt->m_bus_config_thread != NULL) {
		return;
	}

	/*
	 * If there is a reset delay, don't start any cmds.
	 * Otherwise, start as many cmds as possible.
	 */
	cmd = mpt->m_waitq;
	while (cmd != NULL) {
		next_cmd = cmd->cmd_linkp;
		t = Tgt(cmd);
		if (cmd->cmd_flags & CFLAG_PASSTHRU) {
			if (mpt_save_cmd(mpt, cmd) == TRUE) {
				/*
				 * set cmd flag as CFLAG_PREPARED
				 */
				cmd->cmd_flags |= CFLAG_PREPARED;
				mpt_waitq_delete(mpt, cmd);
				mpt_start_passthru(mpt, cmd);
			}
			cmd = next_cmd;
			continue;
		}
		if ((mpt->m_ncmds < mpt->m_max_request_depth) &&
		    (slots->m_target[t].m_reset_delay == 0) &&
		    (slots->m_target[t].m_t_ncmds <
		    slots->m_target[t].m_t_throttle)) {
			if (mpt_save_cmd(mpt, cmd) == TRUE) {
				mpt_waitq_delete(mpt, cmd);
				(void) mpt_start_cmd(mpt, cmd);
			}
		}
		cmd = next_cmd;
	}
}

/*
 * Cmds are queued if transport_start() doesn't get the lock(no wait).
 * Accept all those queued cmds before new cmd is accept so that the
 * cmds are sent in order.
 */
static void
mpt_accept_tx_waitq(mpt_t *mpt)
{
	mpt_cmd_t *cmd;

	ASSERT(mutex_owned(&mpt->m_mutex));
	ASSERT(mutex_owned(&mpt->m_waitq_mutex));

	/*
	 * A Bus Reset could occur at any time and flush the tx_waitq,
	 * so we cannot count on the tx_waitq to contain even one cmd.
	 * And when the m_waitq_mutex is released and run mpt_accept_pkt(),
	 * the tx_waitq may be flushed.
	 */
	cmd = mpt->m_tx_waitq;
	for (;;) {
		if ((cmd = mpt->m_tx_waitq) == NULL) {
			mpt->m_tx_draining = 0;
			break;
		}
		if ((mpt->m_tx_waitq = cmd->cmd_linkp) == NULL) {
			mpt->m_tx_waitqtail = &mpt->m_tx_waitq;
		}
		cmd->cmd_linkp = NULL;
		mutex_exit(&mpt->m_waitq_mutex);
		if (mpt_accept_pkt(mpt, cmd) != TRAN_ACCEPT)
			cmn_err(CE_WARN, "mpt: mpt_accept_tx_waitq: failed to "
			    "accept cmd on queue\n");
		mutex_enter(&mpt->m_waitq_mutex);
	}
}

/*
 * mpt tag type lookup
 */
static char mpt_tag_lookup[] =
	{0, MSG_HEAD_QTAG, MSG_ORDERED_QTAG, 0, MSG_SIMPLE_QTAG};

static int
mpt_start_cmd(mpt_t *mpt, mpt_cmd_t *cmd)
{
	struct scsi_pkt *pkt = CMD2PKT(cmd);
	mpt_slots_t *slots = mpt->m_active;
	uint32_t control = 0;
	int n;
	caddr_t mem;
	msg_scsi_io_request_t *frame;
	uint32_t fma;
	int t = Tgt(cmd);
	ddi_dma_handle_t dma_hdl = mpt->m_dma_hdl;
	ddi_acc_handle_t acc_hdl = mpt->m_acc_hdl;

	NDBG1(("mpt_start_cmd: cmd=0x%p", (void *)cmd));

	/*
	 * It is possible for back to back device reset to
	 * happen before the reset delay has expired.  That's
	 * ok, just let the device reset go out on the bus.
	 */
	if ((cmd->cmd_pkt_flags & FLAG_NOINTR) == 0) {
		ASSERT(slots->m_target[t].m_reset_delay == 0);
	}

	/*
	 * if a non-tagged cmd is submitted to an active tagged target
	 * then drain before submitting this cmd; SCSI-2 allows RQSENSE
	 * to be untagged
	 */
	if (((cmd->cmd_pkt_flags & FLAG_TAGMASK) == 0) &&
	    TAGGED(mpt, t) && (slots->m_target[t].m_t_ncmds > 1) &&
	    ((cmd->cmd_flags & CFLAG_CMDPROXY) == 0) &&
	    (*(cmd->cmd_pkt->pkt_cdbp) != SCMD_REQUEST_SENSE)) {
		if ((cmd->cmd_pkt_flags & FLAG_NOINTR) == 0) {
			NDBG23(("target=%d, untagged cmd, start draining\n",
			    t));

			if (slots->m_target[t].m_reset_delay == 0) {
				mpt_set_throttle(mpt, t, DRAIN_THROTTLE);
			}

			mpt_remove_cmd(mpt, cmd);
			cmd->cmd_pkt_flags |= FLAG_HEAD;
			cmd->cmd_flags &= ~CFLAG_CMD_REMOVED;
			mpt_waitq_add(mpt, cmd);
		}
		return (FALSE);
	}

	/*
	 * Set correct tag bits.
	 */
	if ((cmd->cmd_pkt_flags & FLAG_TAGMASK) && TAGGED(mpt, t)) {
		switch (mpt_tag_lookup[((cmd->cmd_pkt_flags &
		    FLAG_TAGMASK) >> 12)]) {
		case MSG_SIMPLE_QTAG:
			control |= MPI_SCSIIO_CONTROL_SIMPLEQ;
			break;
		case MSG_HEAD_QTAG:
			control |= MPI_SCSIIO_CONTROL_HEADOFQ;
			break;
		case MSG_ORDERED_QTAG:
			control |= MPI_SCSIIO_CONTROL_ORDEREDQ;
			break;
		default:
			mpt_log(mpt, CE_WARN, "mpt: Invalid tag type\n");
			break;
		}
	} else {
		if (*(cmd->cmd_pkt->pkt_cdbp) != SCMD_REQUEST_SENSE) {
			if (slots->m_target[t].m_t_throttle != CHOKE_THROTTLE)
				slots->m_target[t].m_t_throttle = 1;
		}
		control |= MPI_SCSIIO_CONTROL_UNTAGGED;
	}

	mem = MPT_GET_NEXT_FRAME(mpt, cmd->cmd_slot);
	fma = (mpt->m_fma + ((mpt->m_req_frame_size * 4 * MPT_FRAME_SIZE(mpt)) *
	    cmd->cmd_slot));
	frame = (msg_scsi_io_request_t *)mem;

	/*
	 * if this io has a large number of cookies we need
	 * to allocate extra memory on the fly
	 */
	if (pkt->pkt_numcookies > MPT_MAX_DMA_SEGS) {
		if (mpt_alloc_extra_cmd_mem(mpt, cmd) == DDI_FAILURE) {
			mpt_log(mpt, CE_WARN, "extra mem alloc failed\n");
			return (FALSE);
		}
		fma = cmd->cmd_extra_fma;
		frame = (msg_scsi_io_request_t *)cmd->cmd_extra_frame;
		dma_hdl = cmd->cmd_extra_dma_hdl;
		acc_hdl = cmd->cmd_extra_acc_hdl;
	}

	/*
	 * Init the std frame header and fill in the SCSI I/O
	 * specific fields.
	 */
	ddi_put8(acc_hdl, &frame->MsgFlags, 0);
	ddi_put8(acc_hdl, &frame->Reserved, 0);
	ddi_put32(acc_hdl, (uint32_t *)((void *)(&frame->LUN[0])), 0);
	ddi_put32(acc_hdl, (uint32_t *)((void *)(&frame->LUN[4])), 0);
	mpt_init_std_hdr(acc_hdl, frame, BT_TO_TARG(Tgt(cmd)), Lun(cmd),
	    BT_TO_BUS(Tgt(cmd)), 0, MPI_FUNCTION_SCSI_IO_REQUEST);

	(void) ddi_rep_put8(acc_hdl, (uint8_t *)pkt->pkt_cdbp,
	    frame->CDB, cmd->cmd_cdblen, DDI_DEV_AUTOINCR);
	bzero(frame->CDB + cmd->cmd_cdblen, 16 - cmd->cmd_cdblen);
	ddi_put8(acc_hdl, &frame->CDBLength, cmd->cmd_cdblen);
	mpt_put_msg_MessageContext(acc_hdl, frame,
	    (cmd->cmd_slot << 3));

	/*
	 * setup the Scatter/Gather DMA list for this request
	 */
	if (pkt->pkt_numcookies > 0) {
		mpt_sge_setup(mpt, cmd, &control, frame, fma, acc_hdl);
	} else {
		ddi_put32(acc_hdl, &frame->DataLength, 0);
		ddi_put32(acc_hdl, &frame->SGL.u1.Simple.u1.Address64_Low, 0);
		ddi_put32(acc_hdl, &frame->SGL.u1.Simple.u1.Address64_High, 0);
		ddi_put32(acc_hdl, &frame->SGL.u1.Simple.FlagsLength,
		    ((uint32_t)MPI_SGE_FLAGS_LAST_ELEMENT |
		    MPI_SGE_FLAGS_END_OF_BUFFER |
		    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);
	}

	/*
	 * save ARQ information
	 */
	ddi_put8(acc_hdl, &frame->SenseBufferLength,
	    cmd->cmd_rqslen);
	if ((cmd->cmd_flags & (CFLAG_SCBEXTERN | CFLAG_EXTARQBUFVALID)) ==
	    (CFLAG_SCBEXTERN | CFLAG_EXTARQBUFVALID)) {
		ddi_put32(acc_hdl, &frame->SenseBufferLowAddr,
		    cmd->cmd_ext_arqcookie.dmac_address);
	} else {
		ddi_put32(acc_hdl, &frame->SenseBufferLowAddr,
		    cmd->cmd_arqcookie.dmac_address);
	}

	ddi_put32(acc_hdl, &frame->Control, control);

	NDBG31(("starting message=0x%p, with cmd=0x%p",
	    (void *)(uintptr_t)fma, (void *)cmd));

	(void) ddi_dma_sync(dma_hdl, 0, 0, DDI_DMA_SYNC_FORDEV);
	/*
	 * write the FMA (frame address) to the post fifo.
	 */
	MPT_START_CMD(mpt, fma);

	/*
	 * Start timeout.
	 */
#ifdef MPT_TEST
	/*
	 * Temporarily set timebase = 0;  needed for
	 * timeout torture test.
	 */
	if (mpt_test_timeouts) {
		slots->m_target[t].m_timebase = 0;
	}
#endif
	n = pkt->pkt_time - slots->m_target[t].m_timebase;

	if (n == 0) {
		(slots->m_target[t].m_dups)++;
		slots->m_target[t].m_timeout = slots->m_target[t].m_timebase;
	} else if (n > 0) {
		slots->m_target[t].m_timeout =
		    slots->m_target[t].m_timebase = pkt->pkt_time;
		slots->m_target[t].m_dups = 1;
	}
#ifdef MPT_TEST
	/*
	 * Set back to a number higher than
	 * mpt_scsi_watchdog_tick
	 * so timeouts will happen in mpt_watchsubr
	 */
	if (mpt_test_timeouts) {
		slots->m_target[t].m_timebase = 60;
	}
#endif

	if (mpt_check_dma_handle(dma_hdl) != DDI_SUCCESS) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		return (FALSE);
	}
	return (TRUE);
}

static void
mpt_deliver_doneq_thread(mpt_t *mpt)
{
	uint64_t t, i;
	uint32_t min = 0xffffffff;
	mpt_doneq_thread_list_t *item;

	for (i = 0; i < mpt->m_doneq_thread_n; i++) {
		item = &mpt->m_doneq_thread_id[i];
		mutex_enter(&item->mutex);
		if (item->len < mpt->m_doneq_thread_threshold) {
			t = i;
			mutex_exit(&item->mutex);
			break;
		}
		if (item->len < min) {
			min = item->len;
			t = i;
		}
		mutex_exit(&item->mutex);
	}
	mutex_enter(&mpt->m_doneq_thread_id[t].mutex);
	mpt_doneq_mv(mpt, t);
	cv_signal(&mpt->m_doneq_thread_id[t].cv);
	mutex_exit(&mpt->m_doneq_thread_id[t].mutex);
}

static void
mpt_doneq_mv(mpt_t *mpt, uint64_t t)
{
	mpt_cmd_t *cmd;
	mpt_doneq_thread_list_t *item = &mpt->m_doneq_thread_id[t];

	while ((cmd = mpt->m_doneq) != NULL) {
		if ((mpt->m_doneq = cmd->cmd_linkp) == NULL) {
			mpt->m_donetail = &mpt->m_doneq;
		}
		cmd->cmd_linkp = NULL;
		*item->donetail = cmd;
		item->donetail = &cmd->cmd_linkp;
		mpt->m_doneq_len--;
		item->len++;
	}
}

/*
 * These routines manipulate the queue of commands that
 * are waiting for their completion routines to be called.
 * The queue is usually in FIFO order but on an MP system
 * it's possible for the completion routines to get out
 * of order. If that's a problem you need to add a global
 * mutex around the code that calls the completion routine
 * in the interrupt handler.
 */
static void
mpt_doneq_add(mpt_t *mpt, mpt_cmd_t *cmd)
{
	struct scsi_pkt *pkt = CMD2PKT(cmd);

	NDBG31(("mpt_doneq_add: cmd=0x%p", (void *)cmd));

	ASSERT((cmd->cmd_flags & CFLAG_COMPLETED) == 0);
	cmd->cmd_linkp = NULL;
	cmd->cmd_flags |= CFLAG_FINISHED;
	cmd->cmd_flags &= ~CFLAG_IN_TRANSPORT;

	/* Check all acc and dma handles */
	if ((mpt_check_acc_handle(mpt->m_datap) !=
	    DDI_SUCCESS) ||
	    (mpt_check_acc_handle(mpt->m_config_handle)
	    != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip,
		    DDI_SERVICE_UNAFFECTED);
		ddi_fm_acc_err_clear(mpt->m_config_handle,
		    DDI_FME_VER0);
		pkt->pkt_reason = CMD_TRAN_ERR;
		pkt->pkt_statistics = 0;
	}
	if ((mpt_check_dma_handle(mpt->m_dma_hdl) !=
	    DDI_SUCCESS) ||
	    (mpt_check_dma_handle(mpt->m_reply_dma_h)
	    != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(mpt->m_hshk_dma_hdl)
	    != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip,
		    DDI_SERVICE_UNAFFECTED);
		pkt->pkt_reason = CMD_TRAN_ERR;
		pkt->pkt_statistics = 0;
	}
	if ((pkt->pkt_handle &&
	    mpt_check_dma_handle(pkt->pkt_handle) !=
	    DDI_SUCCESS) || (cmd->cmd_extra_dma_hdl &&
	    mpt_check_dma_handle(cmd->cmd_extra_dma_hdl)
	    != DDI_SUCCESS) || (cmd->cmd_arqhandle &&
	    mpt_check_dma_handle(cmd->cmd_arqhandle)
	    != DDI_SUCCESS) || (cmd->cmd_ext_arqhandle &&
	    (mpt_check_dma_handle(cmd->cmd_ext_arqhandle)))) {
		pkt->pkt_reason = CMD_TRAN_ERR;
		pkt->pkt_statistics = 0;
	}


	/*
	 * only add scsi pkts that have completion routines to
	 * the doneq.  no intr cmds do not have callbacks.
	 * run the callback on an ARQ pkt immediately.  This
	 * frees the ARQ for other check conditions.
	 */
	if (pkt->pkt_comp && !(cmd->cmd_flags & CFLAG_CMDARQ)) {
		*mpt->m_donetail = cmd;
		mpt->m_donetail = &cmd->cmd_linkp;
		mpt->m_doneq_len++;
	} else if (pkt->pkt_comp && (cmd->cmd_flags & CFLAG_CMDARQ)) {
		cmd->cmd_flags |= CFLAG_COMPLETED;
		mutex_exit(&mpt->m_mutex);
		scsi_hba_pkt_comp(pkt);
		mutex_enter(&mpt->m_mutex);
	}
}

static mpt_cmd_t *
mpt_doneq_thread_rm(mpt_t *mpt, uint64_t t)
{
	mpt_cmd_t *cmd;
	mpt_doneq_thread_list_t *item = &mpt->m_doneq_thread_id[t];

	/* pop one off the done queue */
	if ((cmd = item->doneq) != NULL) {
		/* if the queue is now empty fix the tail pointer */
		NDBG31(("mpt_doneq_thread_rm: cmd=0x%p", (void *)cmd));
		if ((item->doneq = cmd->cmd_linkp) == NULL) {
			item->donetail = &item->doneq;
		}
		cmd->cmd_linkp = NULL;
		item->len--;
	}
	return (cmd);
}

static void
mpt_doneq_empty(mpt_t *mpt)
{
	if (mpt->m_doneq && !mpt->m_in_callback) {
		mpt_cmd_t *cmd, *next;
		struct scsi_pkt *pkt;

		mpt->m_in_callback = 1;

		/* steal the whole queue */
		cmd = mpt->m_doneq;
		mpt->m_doneq = NULL;
		mpt->m_donetail = &mpt->m_doneq;
		mpt->m_doneq_len = 0;

		mutex_exit(&mpt->m_mutex);
		/*
		 * run the completion routines of all the
		 * completed commands
		 */
		while (cmd  != NULL) {
			next = cmd->cmd_linkp;
			cmd->cmd_linkp = NULL;
			/* run this command's completion routine */
			cmd->cmd_flags |= CFLAG_COMPLETED;
			pkt = CMD2PKT(cmd);
			scsi_hba_pkt_comp(pkt);
			cmd = next;
		}
		mutex_enter(&mpt->m_mutex);
		mpt->m_in_callback = 0;
	}
}

/*
 * These routines manipulate the target's queue of pending requests
 */
static void
mpt_waitq_add(mpt_t *mpt, mpt_cmd_t *cmd)
{
	NDBG7(("mpt_waitq_add: cmd=0x%p", (void *)cmd));

	cmd->cmd_queued = TRUE;
	mpt->m_active->m_target[Tgt(cmd)].m_t_nwait++;
	if (cmd->cmd_pkt_flags & FLAG_HEAD) {
		if ((cmd->cmd_linkp = mpt->m_waitq) == NULL) {
			mpt->m_waitqtail = &cmd->cmd_linkp;
		}
		mpt->m_waitq = cmd;
	} else {
		cmd->cmd_linkp = NULL;
		*(mpt->m_waitqtail) = cmd;
		mpt->m_waitqtail = &cmd->cmd_linkp;
	}
}

static mpt_cmd_t *
mpt_waitq_rm(mpt_t *mpt)
{
	mpt_cmd_t *cmd;
	NDBG7(("mpt_waitq_rm"));

	MPT_WAITQ_RM(mpt, cmd);

	NDBG7(("mpt_waitq_rm: cmd=0x%p", (void *)cmd));

	if (cmd) {
		mpt->m_active->m_target[Tgt(cmd)].m_t_nwait--;
		ASSERT(mpt->m_active->m_target[Tgt(cmd)].m_t_nwait >= 0);
	}
	return (cmd);
}

/*
 * remove specified cmd from the middle of the wait queue.
 */
static void
mpt_waitq_delete(mpt_t *mpt, mpt_cmd_t *cmd)
{
	mpt_cmd_t *prevp = mpt->m_waitq;

	NDBG7(("mpt_waitq_delete: mpt=0x%p cmd=0x%p",
	    (void *)mpt, (void *)cmd));

	mpt->m_active->m_target[Tgt(cmd)].m_t_nwait--;
	ASSERT(mpt->m_active->m_target[Tgt(cmd)].m_t_nwait >= 0);
	if (prevp == cmd) {
		if ((mpt->m_waitq = cmd->cmd_linkp) == NULL)
			mpt->m_waitqtail = &mpt->m_waitq;

		cmd->cmd_linkp = NULL;
		cmd->cmd_queued = FALSE;
		NDBG7(("mpt_waitq_delete: mpt=0x%p cmd=0x%p",
		    (void *)mpt, (void *)cmd));
		return;
	}

	while (prevp != NULL) {
		if (prevp->cmd_linkp == cmd) {
			if ((prevp->cmd_linkp = cmd->cmd_linkp) == NULL)
				mpt->m_waitqtail = &prevp->cmd_linkp;

			cmd->cmd_linkp = NULL;
			cmd->cmd_queued = FALSE;
			NDBG7(("mpt_waitq_delete: mpt=0x%p cmd=0x%p",
			    (void *)mpt, (void *)cmd));
			return;
		}
		prevp = prevp->cmd_linkp;
	}
	cmn_err(CE_PANIC, "mpt: mpt_waitq_delete: queue botch");
}

static mpt_cmd_t *
mpt_tx_waitq_rm(mpt_t *mpt)
{
	mpt_cmd_t *cmd;
	NDBG7(("mpt_tx_waitq_rm"));

	MPT_TX_WAITQ_RM(mpt, cmd);

	NDBG7(("mpt_tx_waitq_rm: cmd=0x%p", (void *)cmd));

	return (cmd);
}

/*
 * remove specified cmd from the middle of the tx_waitq.
 */
static void
mpt_tx_waitq_delete(mpt_t *mpt, mpt_cmd_t *cmd)
{
	mpt_cmd_t *prevp = mpt->m_tx_waitq;

	NDBG7(("mpt_tx_waitq_delete: mpt=0x%p cmd=0x%p",
	    (void *)mpt, (void *)cmd));

	if (prevp == cmd) {
		if ((mpt->m_tx_waitq = cmd->cmd_linkp) == NULL)
			mpt->m_tx_waitqtail = &mpt->m_tx_waitq;

		cmd->cmd_linkp = NULL;
		cmd->cmd_queued = FALSE;
		NDBG7(("mpt_tx_waitq_delete: mpt=0x%p cmd=0x%p",
		    (void *)mpt, (void *)cmd));
		return;
	}

	while (prevp != NULL) {
		if (prevp->cmd_linkp == cmd) {
			if ((prevp->cmd_linkp = cmd->cmd_linkp) == NULL)
				mpt->m_tx_waitqtail = &prevp->cmd_linkp;

			cmd->cmd_linkp = NULL;
			cmd->cmd_queued = FALSE;
			NDBG7(("mpt_tx_waitq_delete: mpt=0x%p cmd=0x%p",
			    (void *)mpt, (void *)cmd));
			return;
		}
		prevp = prevp->cmd_linkp;
	}
	cmn_err(CE_PANIC, "mpt: mpt_tx_waitq_delete: queue botch");
}

static void
mpt_set_scsi_options(mpt_t *mpt, int target, int options)
{
	ushort_t tshift = (1<<target);
	uint32_t config = 0;
	uint32_t params = 0;
	int period, i;
	mpt_slots_t *slots = mpt->m_active;

	/*
	 * NOTE: this routine references mpt->m_active->m_raidvol[0]
	 * only, since it is only called with an LSI1030
	 */

	/*
	 * Check to make sure this target doesn't belong to a RAID device
	 * we don't want to write config pages for these disks unless a
	 * call is made to modify the RAID target.
	 */
	for (i = 0; i < mpt->m_ntargets; i++) {
		if ((MPT_RAID_EXISTS(mpt, 0)) && (TGT_IS_RAID(mpt, 0, i)) &&
		    (i != target)) {
			if ((slots->m_raidvol[0].m_diskid[0] == target) ||
			    slots->m_raidvol[0].m_diskid[1] == target) {
				return;
			}
		}
	}

	if ((options & SCSI_OPTIONS_SYNC) == 0) {
		params &= ~MPI_SCSIPORTPAGE0_CAP_MAX_SYNC_OFFSET_MASK;
	} else {
		params |= (mpt->m_scsi_params &
		    MPI_SCSIPORTPAGE0_CAP_MAX_SYNC_OFFSET_MASK);
	}

	if (options & SCSI_OPTIONS_WIDE) {
		params |= MPI_SCSIDEVPAGE1_RP_WIDE;
	} else {
		params &= ~MPI_SCSIDEVPAGE1_RP_WIDE;
	}

	if ((options & SCSI_OPTIONS_FAST160) && MPT_IS_LVD(mpt) &&
	    (MPT_REV(mpt) > 0)) {
		params |= MPI_SCSIDEVPAGE1_RP_DT | MPI_SCSIDEVPAGE1_RP_IU;
		params |= MPI_SCSIDEVPAGE1_RP_IDP;

		if (options & SCSI_OPTIONS_QAS) {
			params |= MPI_SCSIDEVPAGE1_RP_QAS;
		}
		period = FAST160_PERIOD;
	} else if ((options & SCSI_OPTIONS_FAST80) && MPT_IS_LVD(mpt) &&
	    (MPT_REV(mpt) > 0)) {
		params |= MPI_SCSIDEVPAGE1_RP_DT;
		period = FAST80_PERIOD;
	} else if (options & SCSI_OPTIONS_FAST40) {
		period = FAST40_PERIOD;
	} else if (options & SCSI_OPTIONS_FAST20) {
		period = FAST20_PERIOD;
	} else if (options & SCSI_OPTIONS_FAST) {
		period = FAST_PERIOD;
	} else {
		period = 0;
	}
	params &= ~MPI_SCSIDEVPAGE1_RP_MIN_SYNC_PERIOD_MASK;
	params |= (period << 8);

	if (mpt->m_nowide & tshift) {
		config = MPI_SCSIDEVPAGE1_CONF_WDTR_DISALLOWED;
	}

	if (MPT_RAID_EXISTS(mpt, 0) && TGT_IS_RAID(mpt, 0, target)) {
		for (i = 0; i < MPT_MAX_DISKS_IN_RAID; i++) {
			if (mpt_write_scsi_device_params(mpt,
			    slots->m_raidvol[0].m_diskid[i], params, config)) {
				mpt_log(mpt, CE_WARN, "?set scsi options "
				    "failed for target %d\n", target);
			}
		}
	} else {
		if (mpt_write_scsi_device_params(mpt, target, params,
		    config)) {
			mpt_log(mpt, CE_WARN, "?set scsi options failed for"
			    " target %d\n", target);
		}
	}
}

static void
mpt_sync_wide_backoff(mpt_t *mpt, int target)
{
	ushort_t tshift = (1<<target);
	uint32_t scsi_params;
	uint32_t config = 0;
	int i, j;
	int rval = 0;
	mpt_slots_t *slots = mpt->m_active;

	NDBG31(("mpt_sync_wide_backoff: target=%x", target));

	for (i = 0; i < MPT_MAX_RAIDVOLS; i++) {
		if (MPT_RAID_EXISTS(mpt, i) && TGT_IS_RAID(mpt, i, target)) {
			rval = mpt_get_scsi_device_params(mpt,
			    slots->m_raidvol[i].m_diskid[0],
			    &scsi_params);
			if (rval == -1)
				return;
			break;
		}
	}

	if (i == MPT_MAX_RAIDVOLS) {
		rval = mpt_get_scsi_device_params(mpt, target, &scsi_params);
		if (rval == -1)
			return;
	}

#ifdef MPT_TEST
	if (mpt_no_sync_wide_backoff) {
		return;
	}
#endif

	/*
	 * if this not the first time then disable wide or this
	 * is the first time and sync is already disabled.
	 */
	if ((mpt->m_backoff & tshift) || ((scsi_params &
	    MPI_SCSIPORTPAGE0_CAP_MAX_SYNC_OFFSET_MASK) == 0)) {
		if ((mpt->m_target_scsi_options[target] & SCSI_OPTIONS_WIDE) &&
		    (scsi_params & MPI_SCSIDEVPAGE1_RP_WIDE)) {
			mpt_log(mpt, CE_WARN,
			    "Target %d disabled wide SCSI mode", target);
			cmn_err(CE_WARN,
			    "!ID[SUNWpd.mpt.sync_wide_backoff.6012]");
			scsi_params &= ~MPI_SCSIDEVPAGE1_RP_WIDE;
			mpt->m_target_scsi_options[target] &=
			    ~(SCSI_OPTIONS_WIDE);
		}
	}

	if ((scsi_params & MPI_SCSIPORTPAGE0_CAP_MAX_SYNC_OFFSET_MASK) != 0) {

		/*
		 * If this is not the first time then go async else
		 * double the sync period.
		 */
		if ((mpt->m_backoff & tshift) && (scsi_params &
		    MPI_SCSIPORTPAGE0_CAP_MAX_SYNC_OFFSET_MASK)) {
			mpt_log(mpt, CE_WARN,
			    "Target %d reverting to async. mode", target);
			cmn_err(CE_WARN,
			    "!ID[SUNWpd.mpt.sync_wide_backoff.6013]");
			mpt->m_target_scsi_options[target] &=
			    ~(SCSI_OPTIONS_SYNC | SCSI_OPTIONS_FAST);
			scsi_params &=
			    ~MPI_SCSIPORTPAGE0_CAP_MAX_SYNC_OFFSET_MASK;
		} else {
			int period = ((scsi_params &
			    MPI_SCSIDEVPAGE1_RP_MIN_SYNC_PERIOD_MASK) >> 8);

			/*
			 * backoff sync 100%
			 */
			period = MPT_REDUCE_PERIOD(period);

			mpt_log(mpt, CE_WARN,
			    "Target %d reducing sync. transfer rate", target);
			cmn_err(CE_WARN,
			    "!ID[SUNWpd.mpt.sync_wide_backoff.6014]");

			scsi_params &=
			    ~MPI_SCSIDEVPAGE1_RP_MIN_SYNC_PERIOD_MASK;
			scsi_params |= (period << 8);

			/* update m_target_scsi_options */
			mpt->m_target_scsi_options[target] &=
			    ~SCSI_OPTIONS_FAST_MASK;
			mpt->m_target_scsi_options[target] |=
			    MPT_PERIOD_TO_OPTIONS(period);
		}
	}

	if (mpt->m_nowide & tshift) {
		config = MPI_SCSIDEVPAGE1_CONF_WDTR_DISALLOWED;
	}

	MPT_DISABLE_INTR(mpt);

	if (i == MPT_MAX_RAIDVOLS) {
		/* not a RAID volume */
		if (mpt_write_scsi_device_params(mpt, target, scsi_params,
		    config)) {
			mpt_log(mpt, CE_WARN, "?backoff failed for target %d\n",
			    target);
		}
	} else {
		for (j = 0; j < slots->m_raidvol[i].m_ndisks; j++) {
			if (mpt_write_scsi_device_params(mpt,
			    slots->m_raidvol[i].m_diskid[j],
			    scsi_params, config)) {
				mpt_log(mpt, CE_WARN, "?backoff failed for "
				    "target %d\n", target);
			}
		}
	}

	MPT_ENABLE_INTR(mpt);
	mpt->m_backoff |= tshift;
}

/*
 * device and bus reset handling
 *
 * Notes:
 *	- RESET_ALL:	reset the SCSI bus
 *	- RESET_TARGET:	reset the target specified in scsi_address
 */
static int
mpt_scsi_reset(struct scsi_address *ap, int level)
{
	mpt_t *mpt = ADDR2MPT(ap);
	int rval;

	NDBG22(("mpt_scsi_reset: target=%d level=%d",
	    ap->a_target, level));

	mutex_enter(&mpt->m_mutex);
	rval = mpt_do_scsi_reset(ap, level);
	mutex_exit(&mpt->m_mutex);

	/*
	 * The transport layer expect to only see TRUE and
	 * FALSE. Therefore, we will adjust the return value
	 * if mpt_do_scsi_reset returns FAILED.
	 */
	if (rval == FAILED)
		rval = FALSE;
	return (rval);
}

static int
mpt_do_scsi_reset(struct scsi_address *ap, int level)
{
	mpt_t		*mpt = ADDR2MPT(ap);
	mpt_slots_t	*slots = mpt->m_active;
	int		i, rval = FALSE;
	ushort_t	target = ap->a_target;

	NDBG22(("mpt_do_scsi_reset: target=%d level=%d", target, level));

	if (mpt_debug_resets) {
		mpt_log(mpt, CE_WARN, "mpt_do_scsi_reset: target=%d level=%d",
		    target, level);
	}

	switch (level) {

	case RESET_ALL:
		/*
		 * setup the reset delay
		 */
		if (!ddi_in_panic() && !mpt->m_suspended) {
			mpt_setup_bus_reset_delay(mpt);
		} else {
			drv_usecwait(mpt->m_scsi_reset_delay * 1000);
		}
		/*
		 * Reset the bus, kill all commands in progress.
		 */
		rval = mpt_ioc_task_management(mpt,
		    MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS, target,
		    ap->a_lun, 0);
		break;

	case RESET_TARGET:
		/*
		 * if we are not in panic set up a reset delay for this target
		 */
		if (!ddi_in_panic()) {
			mpt_set_throttle(mpt, target, HOLD_THROTTLE);
			slots->m_target[target].m_reset_delay
			    = mpt->m_scsi_reset_delay;
			mpt_start_watch_reset_delay();
		} else {
			drv_usecwait(mpt->m_scsi_reset_delay * 1000);
		}

		/*
		 * Issue a Device Reset message to the target/lun
		 * specified in ap but not to a disk making up a raid
		 * volume.
		 */
		for (i = 0; i < MPT_MAX_RAIDVOLS; i++) {
			if ((MPT_RAID_EXISTS(mpt, i)) &&
			    !(TGT_IS_RAID(mpt, i, target))) {
				for (i = 1; i < MPT_MAX_DISKS_IN_RAID; i++) {
					if (target ==
					    slots->m_raidvol[i].m_diskid[i]) {
						return (TRUE);
					}
				}
			}
		}
		rval = mpt_ioc_task_management(mpt,
		    MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET,
		    target, ap->a_lun, 0);
		break;
	}
	mpt_doneq_empty(mpt);
	return (rval);
}

static int
mpt_scsi_reset_notify(struct scsi_address *ap, int flag,
	void (*callback)(caddr_t), caddr_t arg)
{
	mpt_t *mpt = ADDR2MPT(ap);

	NDBG22(("mpt_scsi_reset_notify: tgt=%d", ap->a_target));

	return (scsi_hba_reset_notify_setup(ap, flag, callback, arg,
	    &mpt->m_mutex, &mpt->m_reset_notify_listf));
}

/*
 * Since mpt creates a SCSI_ADDR_TARGET_PORT/"target-port" property during node
 * creation, and that property has nothing to do with our unit-address, we must
 * implement own tran_get_name/tran_get_bus_addr functions that understand our
 * simple target/lun property unit-address (or the SCSA framework code will
 * get confused by "target-port"). If you are not going to play by the rules,
 * then the HBA driver must provide its own implementation of the private
 * tran_get_name/tran_get_bus_addr interfaces. NOTE: An alternative solution
 * would be to delay mpt's creation of the "target-port" property until
 * tran_tgt_init(9E).
 */
static int
mpt_scsi_get_name(struct scsi_device *sd, char *ua, int len)
{
	int	tgt;
	int	lun;

	tgt = scsi_device_prop_get_int(sd, SCSI_DEVICE_PROP_PATH,
	    SCSI_ADDR_PROP_TARGET, -1);
	if (tgt == -1)
		return (0);		/* no target */

	lun = scsi_device_prop_get_int(sd, SCSI_DEVICE_PROP_PATH,
	    SCSI_ADDR_PROP_LUN, 0);
	(void) snprintf(ua, len, "%x,%x", tgt, lun);
	return (1);
}

static int
mpt_scsi_get_bus_addr(struct scsi_device *sd, char *ra, int len)
{
	int	tgt;
	int	lun;

	tgt = scsi_device_prop_get_int(sd, SCSI_DEVICE_PROP_PATH,
	    SCSI_ADDR_PROP_TARGET, -1);
	if (tgt == -1)
		return (0);		/* no target */

	lun = scsi_device_prop_get_int(sd, SCSI_DEVICE_PROP_PATH,
	    SCSI_ADDR_PROP_LUN, 0);
	(void) snprintf(ra, len, "%s %x %s %x",
	    SCSI_ADDR_PROP_TARGET, tgt, SCSI_ADDR_PROP_LUN, lun);
	return (1);
}

void
mpt_set_throttle(mpt_t *mpt, int target, int what)
{
	mpt_slots_t *slots = mpt->m_active;

	NDBG25(("mpt_set_throttle: throttle=%x", what));

	/*
	 * if the bus is draining/quiesced, no changes to the throttles
	 * are allowed. Not allowing change of throttles during draining
	 * limits error recovery but will reduce draining time
	 *
	 * all throttles should have been set to HOLD_THROTTLE
	 */
	if (mpt->m_softstate & (MPT_SS_QUIESCED | MPT_SS_DRAINING)) {
		return;
	}

	/*
	 * Something catastrophic happened so we should no longer
	 * accept commands. Set the throttle to CHOKE_THROTTLE and
	 * return. This setting should only occur when resets of the
	 * IOC fail to succeed.
	 */
	if (what == CHOKE_THROTTLE) {
		slots->m_target[target].m_t_throttle = CHOKE_THROTTLE;
		return;
	}

	/*
	 * The device is not responding procedure, so don't set the throttle.
	 */
	if (mpt->m_active->m_target[target].m_dr_flag &
	    MPT_DR_PRE_OFFLINE_TIMEOUT) {
		return;
	}

	/*
	 * If we have set the throttle to choke somewhere else
	 * don't allow the throttle to be changed. Something
	 * catastrophic happened and we don't want any more
	 * commands to be sent to the target.
	 */
	if (slots->m_target[target].m_t_throttle == CHOKE_THROTTLE)
		return;

	if (what == HOLD_THROTTLE) {
		slots->m_target[target].m_t_throttle = HOLD_THROTTLE;
	} else if (slots->m_target[target].m_reset_delay == 0) {
		if (what == MAX_THROTTLE) {
			ushort_t tshift = (1 << target);
			slots->m_target[target].m_t_throttle =
			    ((mpt->m_notag & tshift) ? 1 : what);
		} else {
			slots->m_target[target].m_t_throttle = what;
		}
	}
}

/*
 * Clean up from a device reset.
 * For the case of target reset, this function clears the waitq of all
 * commands for a particular target.   For the case of abort task set, this
 * function clears the waitq of all commands for a particular target/lun.
 */
static void
mpt_flush_target(mpt_t *mpt, ushort_t target, int lun, uint8_t tasktype)
{
	mpt_slots_t *slots = mpt->m_active;
	mpt_cmd_t *cmd, *next_cmd;
	int slot;

	NDBG25(("mpt_flush_target: target=%d lun=%d", target, lun));

	/*
	 * Make sure the I/O Controller has flushed all cmds
	 * that are associated with this target for a target reset
	 * and target/lun for abort task set.
	 */
	for (slot = 0; slot < mpt->m_active->m_n_slots; slot++) {
		if ((cmd = slots->m_slot[slot]) == NULL)
			continue;

		switch (tasktype) {
			case MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
				if (Tgt(cmd) == target) {
					mpt_log(mpt, CE_NOTE, "mpt_flush_"
					    "target discovered non-NULL cmd"
					    " in slot %d, tasktype 0x%x",
					    slot, tasktype);
					mpt_dump_cmd(mpt, cmd);
					mpt_remove_cmd(mpt, cmd);
					/*
					 * If a disconnected command timeout
					 * occurred, set pkt_reason
					 * accordingly.
					 */

					if ((cmd->cmd_pkt->pkt_time > 0) &&
					    (cmd->cmd_active_timeout < 1)) {
						mpt_set_pkt_reason(mpt, cmd,
						    CMD_TIMEOUT,
						    (STAT_TIMEOUT |
						    STAT_DEV_RESET));
					} else {
						mpt_set_pkt_reason(mpt, cmd,
						    CMD_RESET, STAT_DEV_RESET);
					}
					mpt_doneq_add(mpt, cmd);
				}
				break;
			case MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
			case MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
				if ((Tgt(cmd) == target) && (Lun(cmd) == lun)) {
					mpt_log(mpt, CE_NOTE, "mpt_flush_"
					    "target discovered non-NULL cmd"
					    " in slot %d, tasktype 0x%x",
					    slot, tasktype);
					mpt_dump_cmd(mpt, cmd);
					mpt_remove_cmd(mpt, cmd);
					/*
					 * If a disconnected command timeout
					 * occurred, set pkt_reason
					 * accordingly.
					 */

					if ((cmd->cmd_pkt->pkt_time > 0) &&
					    (cmd->cmd_active_timeout < 1)) {
						mpt_set_pkt_reason(mpt, cmd,
						    CMD_TIMEOUT,
						    (STAT_TIMEOUT |
						    STAT_DEV_RESET));
					} else {
						mpt_set_pkt_reason(mpt, cmd,
						    CMD_ABORTED,
						    STAT_DEV_RESET);
					}
					mpt_doneq_add(mpt, cmd);

				}
				break;
			default:
				mpt_log(mpt, CE_WARN, "Unknown target task "
				    "management type 0x%x.", tasktype);
				break;
		}
	}

	/*
	 * Flush the waitq and tx_waitq of this target's cmds
	 */
	cmd = mpt->m_waitq;

	switch (tasktype) {
	case MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
		while (cmd != NULL) {
			next_cmd = cmd->cmd_linkp;
			if (Tgt(cmd) == target) {
				mpt_waitq_delete(mpt, cmd);
				mpt_set_pkt_reason(mpt, cmd,
				    CMD_RESET, STAT_DEV_RESET);
				mpt_doneq_add(mpt, cmd);
			}
			cmd = next_cmd;
		}
		mutex_enter(&mpt->m_waitq_mutex);
		cmd = mpt->m_tx_waitq;
		while (cmd != NULL) {
			next_cmd = cmd->cmd_linkp;
			if (Tgt(cmd) == target) {
				mpt_tx_waitq_delete(mpt, cmd);
				mutex_exit(&mpt->m_waitq_mutex);
				mpt_set_pkt_reason(mpt, cmd,
				    CMD_RESET, STAT_DEV_RESET);
				mpt_doneq_add(mpt, cmd);
				mutex_enter(&mpt->m_waitq_mutex);
			}
			cmd = next_cmd;
		}
		mutex_exit(&mpt->m_waitq_mutex);
		break;
	case MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
	case MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
		while (cmd != NULL) {
			next_cmd = cmd->cmd_linkp;
			if ((Tgt(cmd) == target) && (Lun(cmd) == lun)) {
				mpt_waitq_delete(mpt, cmd);
				mpt_set_pkt_reason(mpt, cmd,
				    CMD_ABORTED, STAT_ABORTED);
				mpt_doneq_add(mpt, cmd);
			}
			cmd = next_cmd;
		}
		mutex_enter(&mpt->m_waitq_mutex);
		cmd = mpt->m_tx_waitq;
		while (cmd != NULL) {
			next_cmd = cmd->cmd_linkp;
			if ((Tgt(cmd) == target) && (Lun(cmd) == lun)) {
				mpt_tx_waitq_delete(mpt, cmd);
				mutex_exit(&mpt->m_waitq_mutex);
				mpt_set_pkt_reason(mpt, cmd,
				    CMD_ABORTED, STAT_ABORTED);
				mpt_doneq_add(mpt, cmd);
				mutex_enter(&mpt->m_waitq_mutex);
			}
			cmd = next_cmd;
		}
		mutex_exit(&mpt->m_waitq_mutex);
		break;
	default:
		mpt_log(mpt, CE_WARN, "Unknown task management type %d.",
		    tasktype);
		break;
	}

	/*
	 * reset timeouts
	 */
	slots->m_target[target].m_timebase = 0;
	slots->m_target[target].m_timeout = 0;
	slots->m_target[target].m_dups = 0;
}

/*
 * Clean up after a Bus Reset.
 */
static void
mpt_flush_hba(mpt_t *mpt)
{
	mpt_slots_t *slots = mpt->m_active;
	mpt_cmd_t *cmd;
	int slot;
	int t;

	NDBG25(("mpt_flush_hba"));

	/*
	 * The I/O Controller should have already sent back
	 * all commands via the scsi I/O reply frame.  Make
	 * sure all commands have been flushed.
	 */
	for (slot = 0; slot < mpt->m_active->m_n_slots; slot++) {
		if ((cmd = slots->m_slot[slot]) == NULL)
			continue;

		if (cmd->cmd_flags & CFLAG_CMDIOC)
			continue;

		mpt_log(mpt, CE_NOTE, "mpt_flush_hba discovered non-NULL cmd "
		    "in slot %d", slot);
		mpt_dump_cmd(mpt, cmd);

		mpt_remove_cmd(mpt, cmd);

		/*
		 * If a disconnected command timeout occurred,
		 * set pkt_reason accordingly.
		 */
		if ((cmd->cmd_pkt->pkt_time > 0) &&
		    (cmd->cmd_active_timeout < 1)) {
			mpt_set_pkt_reason(mpt, cmd, CMD_TIMEOUT,
			    (STAT_TIMEOUT | STAT_BUS_RESET));
		} else {
			mpt_set_pkt_reason(mpt, cmd, CMD_RESET,
			    STAT_BUS_RESET);
		}

		mpt_doneq_add(mpt, cmd);
	}

	/*
	 * Flush the waitq.
	 */
	while ((cmd = mpt_waitq_rm(mpt)) != NULL) {
		mpt_set_pkt_reason(mpt, cmd, CMD_RESET, STAT_BUS_RESET);
		if (cmd->cmd_flags & CFLAG_PASSTHRU) {
			cmd->cmd_flags |= CFLAG_FINISHED;
			cv_broadcast(&mpt->m_passthru_cv);
		} else {
			mpt_doneq_add(mpt, cmd);
		}
	}

	/*
	 * Flush the tx_waitq
	 */
	mutex_enter(&mpt->m_waitq_mutex);
	while ((cmd = mpt_tx_waitq_rm(mpt)) != NULL) {
		mutex_exit(&mpt->m_waitq_mutex);
		mpt_set_pkt_reason(mpt, cmd, CMD_RESET, STAT_BUS_RESET);
		mpt_doneq_add(mpt, cmd);
		mutex_enter(&mpt->m_waitq_mutex);
	}
	mutex_exit(&mpt->m_waitq_mutex);

	/*
	 * reset timeouts
	 */
	for (t = 0; t < mpt->m_ntargets; t++) {
		slots->m_target[t].m_timebase = 0;
		slots->m_target[t].m_timeout = 0;
		slots->m_target[t].m_dups = 0;
	}

	/*
	 * perform the reset notification callbacks that are registered.
	 */
	(void) scsi_hba_reset_notify_callback(&mpt->m_mutex,
	    &mpt->m_reset_notify_listf);

}

/*
 * set pkt_reason and OR in pkt_statistics flag
 */
static void
mpt_set_pkt_reason(mpt_t *mpt, mpt_cmd_t *cmd, uchar_t reason,
    uint_t stat)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(mpt))
#endif

	NDBG25(("mpt_set_pkt_reason: cmd=0x%p reason=%x stat=%x",
	    (void *)cmd, reason, stat));

	if (cmd) {
		if (cmd->cmd_pkt->pkt_reason == CMD_CMPLT) {
			cmd->cmd_pkt->pkt_reason = reason;
		}
		cmd->cmd_pkt->pkt_statistics |= stat;
	}
}

static void
mpt_start_watch_reset_delay()
{
	NDBG22(("mpt_start_watch_reset_delay"));

	mutex_enter(&mpt_global_mutex);
	if (mpt_reset_watch == NULL && mpt_timeouts_enabled) {
		mpt_reset_watch = timeout(mpt_watch_reset_delay, NULL,
		    drv_usectohz((clock_t)
		    MPT_WATCH_RESET_DELAY_TICK * 1000));
		ASSERT(mpt_reset_watch != NULL);
	}
	mutex_exit(&mpt_global_mutex);
}

static void
mpt_setup_bus_reset_delay(mpt_t *mpt)
{
	mpt_slots_t *slots = mpt->m_active;
	int i;

	NDBG22(("mpt_setup_bus_reset_delay"));

	for (i = 0; i < mpt->m_ntargets; i++) {
		mpt_set_throttle(mpt, i, HOLD_THROTTLE);
		slots->m_target[i].m_reset_delay = mpt->m_scsi_reset_delay;
	}
	mpt_start_watch_reset_delay();
}

/*
 * mpt_watch_reset_delay(_subr) is invoked by timeout() and checks every
 * mpt instance for active reset delays
 */
static void
mpt_watch_reset_delay(void *arg)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(arg))
#endif

	mpt_t *mpt;
	int not_done = 0;

	NDBG22(("mpt_watch_reset_delay"));

	mutex_enter(&mpt_global_mutex);
	mpt_reset_watch = 0;
	mutex_exit(&mpt_global_mutex);
	rw_enter(&mpt_global_rwlock, RW_READER);
	for (mpt = mpt_head; mpt != NULL; mpt = mpt->m_next) {
		if (mpt->m_tran == 0) {
			continue;
		}
		mutex_enter(&mpt->m_mutex);
		not_done += mpt_watch_reset_delay_subr(mpt);
		mutex_exit(&mpt->m_mutex);
	}
	rw_exit(&mpt_global_rwlock);

	if (not_done) {
		mpt_start_watch_reset_delay();
	}
}

static int
mpt_watch_reset_delay_subr(mpt_t *mpt)
{
	mpt_slots_t *slots = mpt->m_active;
	int done = 0;
	int restart = 0;
	int t;

	NDBG22(("mpt_watch_reset_delay_subr: mpt=0x%p", (void *)mpt));

	ASSERT(mutex_owned(&mpt->m_mutex));

	for (t = 0; t < mpt->m_ntargets; t++) {
		if (slots->m_target[t].m_reset_delay != 0) {
			slots->m_target[t].m_reset_delay -=
			    MPT_WATCH_RESET_DELAY_TICK;
			if (slots->m_target[t].m_reset_delay <= 0) {
				slots->m_target[t].m_reset_delay = 0;
				mpt_set_throttle(mpt, t, MAX_THROTTLE);
				restart++;
			} else {
				done = -1;
			}
		}
	}

	if (restart > 0) {
		mpt_restart_hba(mpt);
	}
	return (done);
}

#ifdef MPT_TEST
static void
mpt_test_reset(mpt_t *mpt, int target)
{
	struct scsi_address ap;

	if (mpt_rtest == target) {
		ap.a_hba_tran = mpt->m_tran;
		ap.a_target = (ushort_t)target;
		ap.a_lun = 0;

		switch (mpt_rtest_type) {
		case 0:
			if (mpt_do_scsi_reset(&ap, RESET_TARGET) == TRUE) {
				mpt_rtest = -1;
			}
			break;
		case 1:
			if (mpt_do_scsi_reset(&ap, RESET_ALL) == TRUE) {
				mpt_rtest = -1;
			}
			break;
		}
		if (mpt_rtest == -1) {
			NDBG22(("mpt_test_reset success"));
		}
	}
}
#endif

/*
 * abort handling:
 *
 * Notes:
 *	- if pkt is not NULL, abort just that command
 *	- if pkt is NULL, abort all outstanding commands for target
 */
static int
mpt_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	mpt_t *mpt = ADDR2MPT(ap);
	int rval;

	NDBG23(("mpt_scsi_abort: target=%d.%d", ap->a_target, ap->a_lun));

	mutex_enter(&mpt->m_mutex);
	rval = mpt_do_scsi_abort(ap, pkt);
	mutex_exit(&mpt->m_mutex);
	return (rval);
}

static int
mpt_do_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	mpt_t *mpt = ADDR2MPT(ap);
	mpt_cmd_t *sp = NULL;
	mpt_slots_t *slots = mpt->m_active;
	int rval = FALSE;

	ASSERT(mutex_owned(&mpt->m_mutex));

	/*
	 * Abort the command pkt on the target/lun in ap.  If pkt is
	 * NULL, abort all outstanding commands on that target/lun.
	 * If you can abort them, return 1, else return 0.
	 * Each packet that's aborted should be sent back to the target
	 * driver through the callback routine, with pkt_reason set to
	 * CMD_ABORTED.
	 *
	 * abort cmd pkt on HBA hardware; clean out of outstanding
	 * command lists, etc.
	 */
	if (pkt != NULL) {
		/* abort the specified packet */
		sp = PKT2CMD(pkt);

		if (sp->cmd_queued) {
			NDBG23(("mpt_do_scsi_abort: queued sp=0x%p aborted",
			    (void *)sp));
			mpt_waitq_delete(mpt, sp);
			mpt_set_pkt_reason(mpt, sp, CMD_ABORTED, STAT_ABORTED);
			mpt_doneq_add(mpt, sp);
			rval = TRUE;
			goto done;
		}

		/*
		 * Have mpt firmware abort this command
		 */

		if (slots->m_slot[sp->cmd_slot] != NULL) {
			rval = mpt_ioc_task_management(mpt,
			    MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK,
			    ap->a_target, ap->a_lun, sp->cmd_slot);

			/*
			 * The transport layer expects only TRUE and FALSE.
			 * Therefore, if mpt_ioc_task_management returns
			 * FAILED we will return FALSE.
			 */
			if (rval == FAILED)
				rval = FALSE;
			goto done;
		}
	}

	/*
	 * If pkt is NULL then abort task set
	 */
	rval = mpt_ioc_task_management(mpt,
	    MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET, ap->a_target,
	    ap->a_lun, 0);

	/*
	 * The transport layer expects only TRUE and FALSE.
	 * Therefore, if mpt_ioc_task_management returns
	 * FAILED we will return FALSE.
	 */
	if (rval == FAILED)
		rval = FALSE;

#ifdef MPT_TEST
	if (rval && mpt_test_stop) {
		debug_enter("mpt_do_scsi_abort");
	}
#endif

done:
	mpt_doneq_empty(mpt);
	return (rval);
}

#ifdef MPT_TEST
static void
mpt_test_abort(mpt_t *mpt, int target)
{
	struct scsi_address ap;
	int rval = FALSE;
	mpt_cmd_t *cmd;

	ASSERT(mutex_owned(&mpt->m_mutex));

	if (mpt_atest == target) {
		ap.a_target = (ushort_t)target;
		ap.a_lun = 0;
		ap.a_hba_tran = mpt->m_tran;

		switch (mpt_atest_type) {
		case 0:
			/* aborting specific queued cmd (head) */
			if (mpt->m_waitq) {
				cmd = mpt->m_waitq;
				rval = mpt_do_scsi_abort(&ap, cmd->cmd_pkt);
			}
			break;
		case 1:
			/* aborting specific queued cmd (2nd) */
			if (mpt->m_waitq && mpt->m_waitq->cmd_linkp) {
				cmd = mpt->m_waitq->cmd_linkp;
				rval = mpt_do_scsi_abort(&ap, cmd->cmd_pkt);
			}
			break;
		case 2:
		{
			int tag;
			mpt_slots_t *slots = mpt->m_active;

			/* aborting specific disconnected cmd */
			if (mpt->m_ncmds != 0) {
				/*
				 * find the oldest tag
				 */
				for (tag = NTAGS-1; tag > 0; tag--) {
					if ((cmd = slots->m_slot[tag]) != 0) {
						break;
					}
				}
				if (cmd) {
					rval = mpt_do_scsi_abort(&ap,
					    cmd->cmd_pkt);
				}
			}
			break;
		}
		case 3:
			/* aborting all queued requests */
			if (mpt->m_waitq || (mpt->m_ncmds > 0)) {
				rval = mpt_do_scsi_abort(&ap, NULL);
			}
			break;
		}
		if (rval == TRUE) {
			mpt_atest = -1;
			NDBG23(("mpt_test_abort success"));
		}
	}
}
#endif

/*
 * capability handling:
 * (*tran_getcap).  Get the capability named, and return its value.
 */
static int
mpt_scsi_getcap(struct scsi_address *ap, char *cap, int tgtonly)
{
	mpt_t *mpt = ADDR2MPT(ap);
	int ckey;
	int rval = FALSE;
	int rc = 0;
	mpt_slots_t *slots;
	uint32_t scsi_params;

	NDBG24(("mpt_scsi_getcap: target=%d, cap=%s tgtonly=%x",
	    ap->a_target, cap, tgtonly));

	mutex_enter(&mpt->m_mutex);

	slots = mpt->m_active;

	if ((mpt_capchk(cap, tgtonly, &ckey)) != TRUE) {
		mutex_exit(&mpt->m_mutex);
		return (UNDEFINED);
	}

	switch (ckey) {
	case SCSI_CAP_DMA_MAX:
		rval = (int)mpt->m_msg_dma_attr.dma_attr_maxxfer;
		break;
	case SCSI_CAP_DISCONNECT:
		if (MPT_IS_SCSI(mpt)) {
			if (tgtonly &&
			    (mpt->m_target_scsi_options[ap->a_target] &
			    SCSI_OPTIONS_DR)) {
				rval = TRUE;
			}
		} else {
			rval = TRUE;
		}
		break;
	case SCSI_CAP_WIDE_XFER:
		if (MPT_IS_SCSI(mpt)) {
			if (MPT_RAID_EXISTS(mpt, 0) &&
			    TGT_IS_RAID(mpt, 0, ap->a_target)) {
				rc = mpt_get_scsi_device_params(mpt,
				    slots->m_raidvol[0].m_diskid[0],
				    &scsi_params);
			} else {
				rc = mpt_get_scsi_device_params(mpt,
				    ap->a_target, &scsi_params);
			}
			if (rc == -1)
				break;

			if (scsi_params & MPI_SCSIPORTPAGE0_CAP_WIDE) {
				rval = TRUE;
			}
		} else {
			rval = TRUE;
		}
		break;
	case SCSI_CAP_SYNCHRONOUS:
		if (MPT_IS_SCSI(mpt)) {
			if (MPT_RAID_EXISTS(mpt, 0) &&
			    TGT_IS_RAID(mpt, 0, ap->a_target)) {
				rc = mpt_get_scsi_device_params(mpt,
				    slots->m_raidvol[0].m_diskid[0],
				    &scsi_params);
			} else {
				rc = mpt_get_scsi_device_params(mpt,
				    ap->a_target, &scsi_params);
			}
			if (rc == -1)
				break;
			if (scsi_params &
			    MPI_SCSIPORTPAGE0_CAP_MAX_SYNC_OFFSET_MASK) {
				rval = TRUE;
			}
		} else {
			rval = FALSE;
		}
		break;
	case SCSI_CAP_ARQ:
		rval = TRUE;
		break;
	case SCSI_CAP_INITIATOR_ID:
		rval = mpt->m_mptid;
		break;
	case SCSI_CAP_MSG_OUT:
	case SCSI_CAP_PARITY:
	case SCSI_CAP_UNTAGGED_QING:
		rval = TRUE;
		break;
	case SCSI_CAP_TAGGED_QING:
		if (tgtonly && TAGGED(mpt, ap->a_target)) {
			rval = TRUE;
		}
		break;
	case SCSI_CAP_RESET_NOTIFICATION:
		rval = TRUE;
		break;
	case SCSI_CAP_LINKED_CMDS:
		rval = FALSE;
		break;
	case SCSI_CAP_QFULL_RETRIES:
		rval = slots->m_target[ap->a_target].m_qfull_retries;
		break;
	case SCSI_CAP_QFULL_RETRY_INTERVAL:
		rval = drv_hztousec(
		    slots->m_target[ap->a_target].m_qfull_retry_interval)
		    / 1000;
		break;
	case SCSI_CAP_CDB_LEN:
		rval = CDB_GROUP4;
		break;
	case SCSI_CAP_INTERCONNECT_TYPE:
		/*
		 * If the mpt controller is not parallel SCSI (1020/1030)
		 * controller, we return interconnect type capability
		 * as INTERCONNECT_SATA for (1064/1064E/1068/1068E) though
		 * these cards are SAS controllers.
		 */
		rval = MPT_IS_SCSI(mpt) ? INTERCONNECT_PARALLEL :
		    INTERCONNECT_SATA;
		break;
	default:
		rval = UNDEFINED;
		break;
	}

	NDBG24(("mpt_scsi_getcap: %s, rval=%x", cap, rval));

	mutex_exit(&mpt->m_mutex);
	return (rval);
}

/*
 * (*tran_setcap).  Set the capability named to the value given.
 */
static int
mpt_scsi_setcap(struct scsi_address *ap, char *cap, int value, int tgtonly)
{
	mpt_t *mpt = ADDR2MPT(ap);
	int ckey;
	int target = ap->a_target;
	ushort_t tshift = (1<<target);
	int rval = FALSE;
	mpt_slots_t *slots;
	int options = mpt->m_target_scsi_options[target];

	NDBG24(("mpt_scsi_setcap: target=%d, cap=%s value=%x tgtonly=%x",
	    ap->a_target, cap, value, tgtonly));

	if (!tgtonly) {
		return (rval);
	}

	mutex_enter(&mpt->m_mutex);

	slots = mpt->m_active;

	if ((mpt_capchk(cap, tgtonly, &ckey)) != TRUE) {
		mutex_exit(&mpt->m_mutex);
		return (UNDEFINED);
	}

	switch (ckey) {
	case SCSI_CAP_DMA_MAX:
	case SCSI_CAP_MSG_OUT:
	case SCSI_CAP_PARITY:
	case SCSI_CAP_INITIATOR_ID:
	case SCSI_CAP_LINKED_CMDS:
	case SCSI_CAP_UNTAGGED_QING:
	case SCSI_CAP_RESET_NOTIFICATION:
		/*
		 * None of these are settable via
		 * the capability interface.
		 */
		break;
	case SCSI_CAP_DISCONNECT:
		if (MPT_IS_SCSI(mpt)) {
			if (value)
				mpt->m_target_scsi_options[ap->a_target] |=
				    SCSI_OPTIONS_DR;
			else
				mpt->m_target_scsi_options[ap->a_target] &=
				    ~SCSI_OPTIONS_DR;
		}
		rval = TRUE;
		break;
	case SCSI_CAP_WIDE_XFER:
		if (MPT_IS_SCSI(mpt)) {
			if (value) {
				if (mpt->m_target_scsi_options[target] &
				    SCSI_OPTIONS_WIDE) {
					mpt->m_nowide &= ~tshift;
				} else {
					break;
				}
			} else {
				options &= ~SCSI_OPTIONS_WIDE;
			}
			MPT_DISABLE_INTR(mpt);
			mpt_set_scsi_options(mpt, target, options);
			MPT_ENABLE_INTR(mpt);
			rval = TRUE;
		} else {
			rval = TRUE;
		}
		break;
	case SCSI_CAP_SYNCHRONOUS:
		if (MPT_IS_SCSI(mpt)) {
			if (value) {
				if (mpt->m_target_scsi_options[target] &
				    SCSI_OPTIONS_SYNC) {
					options |= SCSI_OPTIONS_SYNC;
				} else {
					break;
				}
			} else {
				options &= ~SCSI_OPTIONS_SYNC;
			}
			MPT_DISABLE_INTR(mpt);
			mpt_set_scsi_options(mpt, target, options);
			MPT_ENABLE_INTR(mpt);
			rval = TRUE;
		}
		break;
	case SCSI_CAP_ARQ:
		/*
		 * We cannot turn off arq so return false if asked to
		 */
		if (value) {
			rval = TRUE;
		} else {
			rval = FALSE;
		}
		break;
	case SCSI_CAP_TAGGED_QING:
		if (MPT_IS_SCSI(mpt)) {
			if (value) {
				if (mpt->m_target_scsi_options[target] &
				    SCSI_OPTIONS_TAG) {
					NDBG9(("target %d: TQ enabled",
					    target));
					mpt->m_notag &= ~tshift;
				} else {
					break;
				}
			} else {
				NDBG9(("target %d: TQ disabled", target));
				mpt->m_notag |= tshift;
			}
			mpt->m_props_update |= tshift;
		}
		mpt_set_throttle(mpt, target, MAX_THROTTLE);
		rval = TRUE;
		break;
	case SCSI_CAP_QFULL_RETRIES:
		slots->m_target[ap->a_target].m_qfull_retries =
		    (uchar_t)value;
		rval = TRUE;
		break;
	case SCSI_CAP_QFULL_RETRY_INTERVAL:
		slots->m_target[ap->a_target].m_qfull_retry_interval =
		    drv_usectohz(value * 1000);
		rval = TRUE;
		break;
	default:
		rval = UNDEFINED;
		break;
	}
	mutex_exit(&mpt->m_mutex);
	return (rval);
}

/*
 * Utility routine for mpt_ifsetcap/ifgetcap
 */
/*ARGSUSED*/
static int
mpt_capchk(char *cap, int tgtonly, int *cidxp)
{
	NDBG24(("mpt_capchk: cap=%s", cap));

	if (!cap)
		return (FALSE);

	*cidxp = scsi_hba_lookup_capstr(cap);
	return (TRUE);
}

/*
 * property management
 * mpt_update_props:
 * create/update sync/wide/TQ/scsi-options properties for this target
 */
static void
mpt_update_props(mpt_t *mpt, int tgt)
{
	char property[32];
	int wide_enabled, tq_enabled, vol;
	uint_t xfer_rate = 0;
	uint_t sync_speed;
	uint32_t scsi_params;
	mpt_slots_t *slots = mpt->m_active;
	int rc = 0;

	NDBG2(("mpt_update_props: tgt=%d", tgt));

	for (vol = 0; vol < MPT_MAX_RAIDVOLS; vol++) {
		if (MPT_RAID_EXISTS(mpt, vol) && TGT_IS_RAID(mpt, vol, tgt)) {
			rc = mpt_get_scsi_device_params(mpt,
			    slots->m_raidvol[vol].m_diskid[0],
			    &scsi_params);
			if (rc == -1)
				return;
			break;
		}
	}

	if (vol == MPT_MAX_RAIDVOLS) {
		rc = mpt_get_scsi_device_params(mpt, tgt, &scsi_params);
		if (rc == -1)
			return;
	}

	/*
	 * If no parameters get returned then disk must not exist
	 */
	if ((scsi_params == 0) && ((mpt->m_neg_occured & (1<<tgt)) == 0)) {
		return;
	}

	/*
	 * Check to see if the negotiation has occured.
	 * If not we set a bit to check again during the next
	 * mpt_watch() execution
	 */
	if ((mpt->m_neg_occured & (1<<tgt)) == 0) {
		mpt->m_props_update |= (1<<tgt);
		return;
	}
	mpt->m_neg_occured &= ~(1<<tgt);

	wide_enabled = ((scsi_params & MPI_SCSIDEVPAGE1_RP_WIDE)? 1 : 0);

	sync_speed = ((scsi_params &
	    MPI_SCSIDEVPAGE1_RP_MIN_SYNC_PERIOD_MASK) >> 8);
	xfer_rate = MPT_GET_PERIOD(sync_speed);
	xfer_rate *= ((wide_enabled)? 2 : 1);

	(void) sprintf(property, "target%x-sync-speed", tgt);
	mpt_update_this_prop(mpt, property, xfer_rate);

	(void) sprintf(property, "target%x-wide", tgt);
	mpt_update_this_prop(mpt, property, wide_enabled);

	(void) sprintf(property, "target%x-TQ", tgt);
	tq_enabled = ((mpt->m_notag & (1<<tgt)) ? 0 : 1);
	mpt_update_this_prop(mpt, property, tq_enabled);
}

static void
mpt_update_this_prop(mpt_t *mpt, char *property, int value)
{
	dev_info_t *dip = mpt->m_dip;

	NDBG2(("mpt_update_this_prop: prop=%s, value=%x", property, value));
	ASSERT(mutex_owned(&mpt->m_mutex));

	/* Cannot hold mutex as call to ddi_prop_update_int() may block */
	mutex_exit(&mpt->m_mutex);
	if (ddi_prop_update_int(DDI_DEV_T_NONE, dip,
	    property, value) != DDI_PROP_SUCCESS) {
		NDBG2(("cannot modify/create %s property.", property));
	}
	mutex_enter(&mpt->m_mutex);
}

static int
mpt_alloc_active_slots(mpt_t *mpt, int flag)
{
	mpt_slots_t *old_active = mpt->m_active;
	mpt_slots_t *new_active;
	size_t size;
	int rval = -1;
	int i;

	if (mpt->m_ncmds) {
		NDBG9(("cannot change size of active slots array"));
		return (rval);
	}

	size = MPT_SLOTS_SIZE(mpt);
	new_active = kmem_zalloc(size, flag);
	if (new_active == NULL) {
		NDBG1(("new active alloc failed"));
	} else {
		mpt->m_active = new_active;
		mpt->m_active->m_n_slots = mpt->m_max_request_depth;
		mpt->m_active->m_size = size;
		mpt->m_active->m_tags = 0;
		if (old_active) {
			kmem_free(old_active, old_active->m_size);
		}
		rval = 0;
	}

	/*
	 * initialize the qfull retry counts
	 */
	for (i = 0; i < mpt->m_ntargets; i++) {
		mpt->m_active->m_target[i].m_qfull_retries =
		    QFULL_RETRIES;
		mpt->m_active->m_target[i].m_qfull_retry_interval =
		    drv_usectohz(QFULL_RETRY_INTERVAL * 1000);
	}

	return (rval);
}

/*
 * Error logging, printing, and debug print routines.
 */
static char *mpt_label = "mpt";

/*PRINTFLIKE3*/
void
mpt_log(mpt_t *mpt, int level, char *fmt, ...)
{
	dev_info_t *dev;
	va_list ap;

	if (mpt) {
		dev = mpt->m_dip;
	} else {
		dev = 0;
	}

	mutex_enter(&mpt_log_mutex);

	va_start(ap, fmt);
	(void) vsprintf(mpt_log_buf, fmt, ap);
	va_end(ap);

	if (level == CE_CONT) {
		scsi_log(dev, mpt_label, level, "%s\n", mpt_log_buf);
	} else {
		scsi_log(dev, mpt_label, level, "%s", mpt_log_buf);
	}

	mutex_exit(&mpt_log_mutex);
}

/*
 * This function processes the IOCStatus field included in all responses from
 * MPT. We check whether Log Info is available, if yes we log it as requested
 * in the MPT programming guide and strip the 'Log Info Available' flag. When
 * IOCStatus is available as well as Log Info, they're both listed in the log
 * entry.
 * The 'verbose' flag decides whether the stripped IOCStatus field is logged
 * in case no Log Info is present - it should be set to '1' if the calling
 * function does not handle the status in a more robust way, otherwise it
 * should be set to '0'
 */
uint16_t
mpt_handle_ioc_status(mpt_t *mpt, ddi_acc_handle_t handle, uint16_t *IOCStatus,
    uint32_t *IOCLogInfo, const char *logprefix, uint8_t verbose)
{
	uint16_t iocstatus;

	iocstatus = ddi_get16(handle, IOCStatus);
	if (iocstatus & MPI_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE) {
		/*
		 * We mask the LOG_INFO_AVAILABLE bit here - it is not set in
		 * case if the condition above is not met.
		 */
		iocstatus &= MPI_IOCSTATUS_MASK;
		if (iocstatus == MPI_IOCSTATUS_SUCCESS) {
			mpt_log(mpt, CE_NOTE,
			    "!%s: IOCLogInfo=0x%x", logprefix,
			    ddi_get32(handle, IOCLogInfo));
		} else {
			/*
			 * We unset 'verbose' here as IOCStatus is logged
			 * together with IOCLogInfo in any case
			 */
			verbose = 0;
			mpt_log(mpt, CE_NOTE,
			    "!%s: IOCStatus=0x%x IOCLogInfo=0x%x", logprefix,
			    iocstatus, ddi_get32(handle, IOCLogInfo));
		}
	}

	if (iocstatus != MPI_IOCSTATUS_SUCCESS && verbose == 1) {
		/*
		 * We log the IOCStatus here as a notification only. If it
		 * needs to be treated differently, it should occur in the
		 * calling function.
		 */
		mpt_log(mpt, CE_NOTE, "!%s: IOCStatus=0x%x", logprefix,
		    iocstatus);
	}

	return (iocstatus);
}

#ifdef MPT_DEBUG
/*PRINTFLIKE1*/
void
mpt_printf(char *fmt, ...)
{
	dev_info_t *dev = 0;
	va_list	ap;

	mutex_enter(&mpt_log_mutex);

	va_start(ap, fmt);
	(void) vsprintf(mpt_log_buf, fmt, ap);
	va_end(ap);

#ifdef PROM_PRINTF
	prom_printf("%s:\t%s\n", mpt_label, mpt_log_buf);
#else
	scsi_log(dev, mpt_label, SCSI_DEBUG, "%s\n", mpt_log_buf);
#endif
	mutex_exit(&mpt_log_mutex);
}
#endif

/*
 * timeout handling
 */
static void
mpt_watch(void *arg)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(arg))
#endif

	mpt_t *mpt;
	ushort_t props_update = 0;

	NDBG30(("mpt_watch"));

	rw_enter(&mpt_global_rwlock, RW_READER);
	for (mpt = mpt_head; mpt != (mpt_t *)NULL; mpt = mpt->m_next) {

		mutex_enter(&mpt->m_mutex);

		/* Skip device if not powered on */
		if (mpt->m_options & MPT_OPT_PM) {
			if (mpt->m_power_level == PM_LEVEL_D0) {
				(void) pm_busy_component(mpt->m_dip, 0);
				mpt->m_busy = 1;
			} else {
				mutex_exit(&mpt->m_mutex);
				continue;
			}
		}

		/*
		 * Check to see if we got reset due to a hard reset
		 * on the other channel
		 */
		if ((ddi_get32(mpt->m_datap, &mpt->m_reg->m_doorbell) &
		    MPI_IOC_STATE_OPERATIONAL) == 0) {
			if (mpt_restart_ioc(mpt)) {
				mpt_log(mpt, CE_WARN, "mpt restart ioc failed");
			}
		}

		/*
		 * For now, always call mpt_watchsubr.
		 */
		mpt_watchsubr(mpt);

		if (mpt->m_props_update) {
			int i;
			/*
			 * m_mutex is released and reentered in
			 * mpt_update_this_prop() so we save the value of
			 * m_props_update and then set it to zero indicating
			 * that a property has been updated.  This avoids a
			 * race condition with any thread that runs in interrupt
			 * context that attempts to set m_props_update to a
			 * non-zero value.  If m_props_update is modified
			 * during mpt_update_props() then at the next callout
			 * of mpt_watch() we will update the props then.
			 */
			props_update = mpt->m_props_update;
			mpt->m_props_update = 0;
			for (i = 0; i < mpt->m_ntargets; i++) {
				if (props_update & (1<<i)) {
					mpt_update_props(mpt, i);
				}
			}
		}

		if (mpt->m_options & MPT_OPT_PM) {
			mpt->m_busy = 0;
			(void) pm_idle_component(mpt->m_dip, 0);
		}

		mutex_exit(&mpt->m_mutex);
	}
	rw_exit(&mpt_global_rwlock);

	mutex_enter(&mpt_global_mutex);
	if (mpt_timeouts_enabled)
		mpt_timeout_id = timeout(mpt_watch, NULL, mpt_tick);
	mutex_exit(&mpt_global_mutex);
}

static void
mpt_watchsubr(mpt_t *mpt)
{
	mpt_slots_t *slots;
	int i, t;
	mpt_cmd_t *cmd;

	NDBG30(("mpt_watchsubr: mpt=0x%p", (void *)mpt));

#ifdef MPT_TEST
	if (mpt_enable_untagged) {
		mpt_test_untagged++;
	}
#endif

	slots = mpt->m_active;

	/*
	 * Check for commands stuck in active slot
	 */
	for (i = 0; i < mpt->m_active->m_n_slots; i++) {
		if ((cmd = mpt->m_active->m_slot[i]) != NULL) {
			if ((cmd->cmd_flags & CFLAG_CMDIOC) == 0) {
				cmd->cmd_active_timeout -=
				    mpt_scsi_watchdog_tick;
				if (cmd->cmd_active_timeout <= 0) {
				/*
				 * There seems to be a command stuck
				 * in the active slot.  Drain throttle.
				 */
					mpt_set_throttle(mpt,
					    (int)Tgt(cmd),
					    DRAIN_THROTTLE);
				}
			}

			if (cmd->cmd_flags & CFLAG_PASSTHRU) {
				cmd->cmd_active_timeout -=
				    mpt_scsi_watchdog_tick;
				if (cmd->cmd_active_timeout <= 0) {
					/*
					 * passthrough command timeout
					 */
					cmd->cmd_flags |= (CFLAG_FINISHED |
					    CFLAG_TIMEOUT);
					cv_broadcast(&mpt->m_passthru_cv);
				}
			}
		}
	}

	for (t = (mpt->m_ntargets-1); t >= 0; t--) {
		/*
		 * Check offline status
		 */
		if (slots->m_target[t].m_dr_flag &
		    (MPT_DR_PRE_OFFLINE_TIMEOUT_NO_CANCEL |
		    MPT_DR_PRE_OFFLINE_TIMEOUT)) {
			if (slots->m_target[t].m_dr_timeout <= 0) {
				m_dr_arg_t *args;
				args = kmem_zalloc(sizeof (m_dr_arg_t),
				    KM_SLEEP);
				args->mpt = (void *)mpt;
				args->target = t;
				args->event = MPT_DR_EVENT_OFFLINE_TARGET;
				args->israid = 0;
				NDBG20(("mpt%d scheduled offline for"
				    " target:%d", mpt->m_instance, t));
				/*
				 * Set MPT_DR_OFFLINE_IN_PROGRESS because we
				 * are dispatching the offline.
				 */
				slots->m_target[t].m_dr_flag =
				    MPT_DR_OFFLINE_IN_PROGRESS;
				slots->m_target[t].m_dr_offline_dups++;
				if ((ddi_taskq_dispatch(mpt->m_dr_taskq,
				    mpt_handle_dr,
				    (void *)args, DDI_NOSLEEP)) !=
				    DDI_SUCCESS) {
					mpt_log(mpt, CE_NOTE, "mpt start taskq"
					    " for handle SAS target reconfigure"
					    " event failed. \n");
				}
				mpt_set_throttle(mpt, t, DRAIN_THROTTLE);
				mpt_restart_hba(mpt);
			}
			slots->m_target[t].m_dr_timeout -=
			    mpt_scsi_watchdog_tick;

		}

		/*
		 * If we were draining due to a qfull condition,
		 * go back to full throttle.
		 */
		if (TAGGED(mpt, t) &&
		    (slots->m_target[t].m_t_throttle < MAX_THROTTLE) &&
		    (slots->m_target[t].m_t_throttle > HOLD_THROTTLE) &&
		    (slots->m_target[t].m_t_ncmds <
		    slots->m_target[t].m_t_throttle)) {
			mpt_set_throttle(mpt, t, MAX_THROTTLE);
			mpt_restart_hba(mpt);
		}

		if ((slots->m_target[t].m_t_ncmds > 0) &&
		    (slots->m_target[t].m_timebase)) {

			if (slots->m_target[t].m_timebase <=
			    mpt_scsi_watchdog_tick) {
				slots->m_target[t].m_timebase +=
				    mpt_scsi_watchdog_tick;
				continue;
			}

			slots->m_target[t].m_timeout -= mpt_scsi_watchdog_tick;

			if (slots->m_target[t].m_timeout < 0) {
				mpt_cmd_timeout(mpt, t);
				continue;
			}

			if ((slots->m_target[t].m_timeout) <=
			    mpt_scsi_watchdog_tick) {
				NDBG23(("pending timeout"));
				mpt_set_throttle(mpt, t, DRAIN_THROTTLE);
			}
		}

#ifdef MPT_TEST
		if (mpt_fail_raid == 1) {
			for (i = 0; i < MPT_MAX_RAIDVOLS; i++) {
				if (slots->m_raidvol[i].m_raidtarg == t) {
					(void) mpt_send_raid_action(mpt,
					    MPI_RAID_ACTION_FAIL_PHYSDISK,
					    slots->m_raidvol[i].m_disknum[0],
					    0, 0, 0, 0);
					mpt_fail_raid = 0;
					break;
				}
			}
		}

		if (mpt->m_instance == mpt_test_instance) {
			mpt_test_reset(mpt, t);
			mpt_test_abort(mpt, t);
		}

		if (mpt_kill_ioc == 1) {
			if (mpt_restart_ioc(mpt)) {
				mpt_log(mpt, CE_WARN, "mpt restart ioc failed");
			}
			mpt_kill_ioc = 0;
		}
#endif
	}
}

/*
 * timeout recovery
 */
static void
mpt_cmd_timeout(mpt_t *mpt, int target)
{
	int t;
	struct scsi_address ap;
	ap.a_hba_tran = mpt->m_tran;
	ap.a_target = (ushort_t)target;
	ap.a_lun = 0;

	NDBG29(("mpt_cmd_timeout: target=%d", target));
	mpt_log(mpt, CE_WARN, "Disconnected command timeout for "
	    "Target %d", target);

	/*
	 * If the current target is not the target passed in,
	 * try to reset that target.
	 */
	NDBG29(("mpt_cmd_timeout: device reset"));
	if (mpt_do_scsi_reset(&ap, RESET_TARGET) == TRUE) {
		return;
	}
	/*
	 * if the target won't listen, then a retry is useless
	 * there is also the possibility that the cmd still completed while
	 * we were trying to reset and the target driver may have done a
	 * device reset which has blown away this cmd.
	 * well, we've tried, now pull the chain
	 */
	NDBG29(("mpt_cmd_timeout: bus reset"));
	if (mpt_do_scsi_reset(&ap, RESET_ALL) == FAILED) {
		/*
		 * If the reset of all targets failed we're in trouble.
		 * Set all throttles to CHOKE_THROTTLE to stop accepting
		 * commands. Then flush the commands to force packets to
		 * complete with errors.
		 */
		mpt_log(mpt, CE_WARN, "Rejecting future commands");
		for (t = 0; t < mpt->m_ntargets; t++)
			mpt_set_throttle(mpt, t, CHOKE_THROTTLE);

		mpt_flush_hba(mpt);
		mpt_doneq_empty(mpt);
	}
}

/*
 * Device / Hotplug control
 */
static int
mpt_scsi_quiesce(dev_info_t *dip)
{
	mpt_t *mpt;
	scsi_hba_tran_t *tran;

	tran = ddi_get_driver_private(dip);
	if (tran == NULL || (mpt = TRAN2MPT(tran)) == NULL)
		return (-1);

	return (mpt_quiesce_bus(mpt));
}

static int
mpt_scsi_unquiesce(dev_info_t *dip)
{
	mpt_t *mpt;
	scsi_hba_tran_t *tran;

	tran = ddi_get_driver_private(dip);
	if (tran == NULL || (mpt = TRAN2MPT(tran)) == NULL)
		return (-1);

	return (mpt_unquiesce_bus(mpt));
}

static int
mpt_quiesce_bus(mpt_t *mpt)
{
	int i;
	int quiesce_interrupted = 0;
	int rv;

	NDBG28(("mpt_quiesce_bus"));
	mutex_enter(&mpt->m_mutex);

	/* Set all the throttles to zero */
	for (i = 0; i < mpt->m_ntargets; i++) {
		mpt_set_throttle(mpt, i, HOLD_THROTTLE);
	}
	/* If there are any outstanding commands in the queue */
	if (mpt->m_ncmds) {
		mpt->m_softstate |= MPT_SS_DRAINING;
		mpt->m_quiesce_timeid = timeout(mpt_ncmds_checkdrain,
		    mpt, (MPT_QUIESCE_TIMEOUT * drv_usectohz(1000000)));
		if (mpt->m_bus_config_thread != NULL &&
		    mpt->m_quiesce_delay_timebase > 0) {
			mpt->m_quiesce_delay_timeout =
			    mpt->m_quiesce_delay_timebase;
		}

		while (mpt->m_ncmds > 0) {
			rv = cv_wait_sig(&mpt->m_cv, &mpt->m_mutex);

			/* Check whether quiesce succeeded. */
			if ((rv > 0) && (mpt->m_ncmds == 0)) {
				break;
			}

			/* Check whether quiesce was interrupted. */
			if (rv == 0) {
				quiesce_interrupted = 1;
				break;
			}

			/*
			 * Check whether mpt_quiesce_delay expired
			 * if applicable.
			 */
			if ((mpt->m_bus_config_thread != NULL) &&
			    (mpt->m_quiesce_delay_timebase > 0) &&
			    (mpt->m_quiesce_delay_timeout < 1)) {
				quiesce_interrupted = 1;
				break;
			}
		}

		if (quiesce_interrupted == 1) {
			/*
			 * Quiesce has been interrupted
			 */
			mpt->m_softstate &= ~MPT_SS_DRAINING;
			for (i = 0; i < mpt->m_ntargets; i++) {
				mpt_set_throttle(mpt, i, MAX_THROTTLE);
			}
			mpt_restart_hba(mpt);
			if (mpt->m_quiesce_timeid != 0) {
				timeout_id_t tid = mpt->m_quiesce_timeid;
				mpt->m_quiesce_timeid = 0;
				mutex_exit(&mpt->m_mutex);
				(void) untimeout(tid);
				return (-1);
			}
			mutex_exit(&mpt->m_mutex);
			return (-1);
		} else {
			/* Bus has been quiesced */
			ASSERT(mpt->m_quiesce_timeid == 0);
			mpt->m_softstate &= ~MPT_SS_DRAINING;
			mpt->m_softstate |= MPT_SS_QUIESCED;
			mutex_exit(&mpt->m_mutex);
			return (0);
		}
	}
	/* Bus was not busy - QUIESCED */
	mutex_exit(&mpt->m_mutex);

	return (0);
}

static int
mpt_unquiesce_bus(mpt_t *mpt)
{
	int i;

	NDBG28(("mpt_unquiesce_bus"));
	mutex_enter(&mpt->m_mutex);
	mpt->m_softstate &= ~MPT_SS_QUIESCED;
	for (i = 0; i < mpt->m_ntargets; i++) {
		mpt_set_throttle(mpt, i, MAX_THROTTLE);
	}
	mpt_restart_hba(mpt);
	mutex_exit(&mpt->m_mutex);
	return (0);
}

static void
mpt_ncmds_checkdrain(void *arg)
{
	mpt_t *mpt = arg;
	int i;

	mutex_enter(&mpt->m_mutex);
	if (mpt->m_softstate & MPT_SS_DRAINING) {
		mpt->m_quiesce_timeid = 0;
		if (mpt->m_ncmds == 0) {
			/* Command queue has been drained */
			cv_signal(&mpt->m_cv);
		} else {
			/*
			 * The throttle may have been reset because
			 * of a SCSI bus reset
			 */
			for (i = 0; i < mpt->m_ntargets; i++) {
				mpt_set_throttle(mpt, i, HOLD_THROTTLE);
			}

			if ((mpt->m_bus_config_thread != NULL) &&
			    (mpt->m_quiesce_delay_timebase > 0)) {
				mpt->m_quiesce_delay_timeout -=
				    MPT_QUIESCE_TIMEOUT;

				if (mpt->m_quiesce_delay_timeout < 1) {
					cv_signal(&mpt->m_cv);
					mutex_exit(&mpt->m_mutex);
					return;
				}
			}

			mpt->m_quiesce_timeid = timeout(mpt_ncmds_checkdrain,
			    mpt, (MPT_QUIESCE_TIMEOUT * drv_usectohz(1000000)));
		}
	}
	mutex_exit(&mpt->m_mutex);
}

static void
mpt_dump_cmd(mpt_t *mpt, mpt_cmd_t *cmd)
{
	int i;
	uint8_t *cp = (uchar_t *)cmd->cmd_pkt->pkt_cdbp;
	char buf[128];

	buf[0] = '\0';
	mpt_log(mpt, CE_NOTE, "?Cmd (0x%p) dump for Target %d Lun %d:\n",
	    (void *)cmd, Tgt(cmd), Lun(cmd));
	(void) sprintf(&buf[0], "\tcdb=[");
	for (i = 0; i < (int)cmd->cmd_cdblen; i++) {
		(void) sprintf(&buf[strlen(buf)], " 0x%x", *cp++);
	}
	(void) sprintf(&buf[strlen(buf)], " ]");
	mpt_log(mpt, CE_NOTE, "?%s\n", buf);
	mpt_log(mpt, CE_NOTE,
	    "?pkt_flags=0x%x pkt_statistics=0x%x pkt_state=0x%x\n",
	    cmd->cmd_pkt->pkt_flags, cmd->cmd_pkt->pkt_statistics,
	    cmd->cmd_pkt->pkt_state);
	mpt_log(mpt, CE_NOTE, "?pkt_scbp=0x%x cmd_flags=0x%x\n",
	    *(cmd->cmd_pkt->pkt_scbp), cmd->cmd_flags);
}
static void
mpt_start_passthru(mpt_t *mpt, mpt_cmd_t *cmd) {

	caddr_t memp;
	msg_request_header_t *request_hdrp;
	struct scsi_pkt *pkt = cmd->cmd_pkt;
	mpt_pt_request_t *pt = pkt->pkt_ha_private;
	uint32_t request_size, data_size, dataout_size, direction;
	uint32_t i;
	uint8_t *request;
	uint32_t fma;
	ddi_dma_cookie_t data_cookie;
	ddi_dma_cookie_t dataout_cookie;

	request = pt->request;
	direction = pt->direction;
	request_size = pt->request_size;
	data_size = pt->data_size;
	dataout_size = pt->dataout_size;
	data_cookie = pt->data_cookie;
	dataout_cookie = pt->dataout_cookie;


	/*
	 * Store the passthrough message in memory location
	 * corresponding to our slot number
	 */
	memp = MPT_GET_NEXT_FRAME(mpt, cmd->cmd_slot);
	request_hdrp = (msg_request_header_t *)memp;
	bzero(memp, (mpt->m_req_frame_size << 2));

	for (i = 0; i < request_size; i++) {
		bcopy(request + i, memp + i, 1);
	}

	mpt_put_msg_MessageContext(mpt->m_acc_hdl, request_hdrp,
	    (cmd->cmd_slot << 3));

	if (data_size || dataout_size) {
		sge_simple32_t *sgep;
		uint32_t sge_flags;

		sgep = (sge_simple32_t *)((uint8_t *)request_hdrp +
		    request_size);
		if (dataout_size) {
			sge_flags = dataout_size |
			    ((uint32_t)(MPI_SGE_FLAGS_SIMPLE_ELEMENT |
			    MPI_SGE_FLAGS_END_OF_BUFFER |
			    MPI_SGE_FLAGS_HOST_TO_IOC |
			    MPI_SGE_FLAGS_32_BIT_ADDRESSING) <<
			    MPI_SGE_FLAGS_SHIFT);
			ddi_put32(mpt->m_acc_hdl, &sgep->FlagsLength,
			    sge_flags);
			ddi_put32(mpt->m_acc_hdl, &sgep->Address,
			    dataout_cookie.dmac_address);
			sgep++;
		}
		sge_flags = data_size;
		sge_flags |= ((uint32_t)(MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI_SGE_FLAGS_LAST_ELEMENT |
		    MPI_SGE_FLAGS_END_OF_BUFFER |
		    MPI_SGE_FLAGS_END_OF_LIST |
		    MPI_SGE_FLAGS_32_BIT_ADDRESSING) <<
		    MPI_SGE_FLAGS_SHIFT);
		if (direction == MPT_PASS_THRU_WRITE) {
			sge_flags |= ((uint32_t)(MPI_SGE_FLAGS_HOST_TO_IOC) <<
			    MPI_SGE_FLAGS_SHIFT);
		} else {
			sge_flags |= ((uint32_t)(MPI_SGE_FLAGS_IOC_TO_HOST) <<
			    MPI_SGE_FLAGS_SHIFT);
		}
		ddi_put32(mpt->m_acc_hdl, &sgep->FlagsLength, sge_flags);
		ddi_put32(mpt->m_acc_hdl, &sgep->Address,
		    data_cookie.dmac_address);
	}

	fma = (mpt->m_fma + ((mpt->m_req_frame_size * 4 *
	    MPT_FRAME_SIZE(mpt)) * cmd->cmd_slot));

	if (request_hdrp->Function == MPI_FUNCTION_SCSI_IO_REQUEST) {
		msg_scsi_io_request_t *scsi_io_req;
		scsi_io_req = (msg_scsi_io_request_t *)request_hdrp;
		/*
		 * Put SGE for data and data_out buffer at the end of
		 * scsi_io_request message header.(64 bytes in total)
		 * Following above SGEs, the residual space will be
		 * used by sense data.
		 */
		ddi_put8(mpt->m_acc_hdl, &scsi_io_req->SenseBufferLength,
		    (uint8_t)(request_size - 64));
		ddi_put32(mpt->m_acc_hdl, &scsi_io_req->SenseBufferLowAddr,
		    fma + 64);
	}

	/*
	 * We must wait till the message has been completed before
	 * beginning the next message so we wait for this one to
	 * finish.
	 */
	(void) ddi_dma_sync(mpt->m_dma_hdl, 0, 0, DDI_DMA_SYNC_FORDEV);
	cmd->cmd_rfm = NULL;
	MPT_START_CMD(mpt, fma);
}

static int
mpt_do_passthru(mpt_t *mpt, uint8_t *request, uint8_t *reply, uint8_t *data,
    uint32_t request_size, uint32_t reply_size, uint32_t data_size,
    uint32_t direction, uint8_t *dataout, uint32_t dataout_size, short timeout,
    int mode)
{
	mpt_pt_request_t pt;
	mpt_dma_alloc_state_t data_dma_state;
	mpt_dma_alloc_state_t dataout_dma_state;
	caddr_t memp = NULL;
	mpt_cmd_t *cmd;
	struct scsi_pkt *pkt;
	uint32_t reply_len = 0, sense_len = 0;
	msg_request_header_t *request_hdrp;
	msg_request_header_t *request_msg;
	msg_default_reply_t *reply_msg;
	msg_scsi_io_reply_t rep_msg;
	int i;
	int status = 0;
	int pt_flags = 0;
	int rv = 0;
	int rvalue;

	ASSERT(mutex_owned(&mpt->m_mutex));

	reply_msg = (msg_default_reply_t *)(&rep_msg);
	bzero(reply_msg, sizeof (msg_scsi_io_reply_t));
	request_msg = kmem_zalloc(request_size, KM_SLEEP);

	mutex_exit(&mpt->m_mutex);
	if (ddi_copyin(request, request_msg, request_size, mode)) {
		mutex_enter(&mpt->m_mutex);
		status = EFAULT;
		mpt_log(mpt, CE_WARN, "failed to copy request data");
		goto out;
	}
	mutex_enter(&mpt->m_mutex);

	if (request_msg->Function == MPI_FUNCTION_SCSI_TASK_MGMT) {
		msg_scsi_task_mgmt_t *task;
		task = (msg_scsi_task_mgmt_t *)request_msg;

		if (MPT_IS_SAS(mpt)) {
			rv = mpt_ioc_task_management(mpt, task->TaskType,
			    BUSTARG_TO_BT(task->Bus, task->TargetID),
			    (int)task->LUN[1], 0);
		} else {
			rv = mpt_ioc_task_management(mpt, task->TaskType,
			    task->TargetID, (int)task->LUN[1], 0);
		}

		if (rv != TRUE) {
			status = EIO;
			mpt_log(mpt, CE_WARN, "task management failed");
		}

		mpt_doneq_empty(mpt);
		goto out;
	}

	if (data_size != 0) {
		data_dma_state.size = data_size;
		if (mpt_passthru_dma_alloc(mpt, &data_dma_state) !=
		    DDI_SUCCESS) {
			status = ENOMEM;
			mpt_log(mpt, CE_WARN, "failed to alloc DMA resource");
			goto out;
		}
		pt_flags |= MPT_PT_DATA_ALLOCATED;
		if (direction == MPT_PASS_THRU_WRITE) {
			mutex_exit(&mpt->m_mutex);
			for (i = 0; i < data_size; i++) {
				if (ddi_copyin(data + i, (uint8_t *)
				    data_dma_state.memp + i, 1, mode)) {
					mutex_enter(&mpt->m_mutex);
					status = EFAULT;
					mpt_log(mpt, CE_WARN, "failed to copy"
					    " read data");
					goto out;
				}
			}
			mutex_enter(&mpt->m_mutex);
		}
	}

	if (dataout_size != 0) {
		dataout_dma_state.size = dataout_size;
		if (mpt_passthru_dma_alloc(mpt, &dataout_dma_state) !=
		    DDI_SUCCESS) {
			status = ENOMEM;
			mpt_log(mpt, CE_WARN, "failed to alloc DMA resource");
			goto out;
		}
		pt_flags |= MPT_PT_DATAOUT_ALLOCATED;
		mutex_exit(&mpt->m_mutex);
		for (i = 0; i < dataout_size; i++) {
			if (ddi_copyin(dataout + i, (uint8_t *)
			    dataout_dma_state.memp + i, 1, mode)) {
				mutex_enter(&mpt->m_mutex);
				mpt_log(mpt, CE_WARN, "failed to copy out"
				    " data");
				status = EFAULT;
				goto out;
			}
		}
		mutex_enter(&mpt->m_mutex);
	}

	if ((rvalue = (mpt_request_from_pool(mpt, &cmd, &pkt))) == -1) {
		status = EAGAIN;
		mpt_log(mpt, CE_NOTE, "event ack command pool is full");
		goto out;
	}
	pt_flags |= MPT_PT_REQUEST_POOL_CMD;

	bzero((caddr_t)cmd, sizeof (*cmd));
	bzero((caddr_t)pkt, scsi_pkt_size());
	bzero((caddr_t)&pt, sizeof (pt));

	cmd->ioc_cmd_slot = (uint32_t)(rvalue);

	pt.request = (uint8_t *)request_msg;
	pt.direction = direction;
	pt.request_size = request_size;
	pt.data_size = data_size;
	pt.dataout_size = dataout_size;
	pt.data_cookie = data_dma_state.cookie;
	pt.dataout_cookie = dataout_dma_state.cookie;

	/*
	 * Form a blank cmd/pkt to store the acknoledgement message
	 */
	pkt->pkt_cdbp		= (opaque_t)&cmd->cmd_cdb[0];
	pkt->pkt_scbp		= (opaque_t)&cmd->cmd_scb;
	pkt->pkt_ha_private	= (opaque_t)&pt;
	pkt->pkt_flags		= FLAG_HEAD;
	pkt->pkt_time		= timeout;
	cmd->cmd_pkt		= pkt;
	cmd->cmd_flags		= CFLAG_CMDIOC | CFLAG_PASSTHRU;

	/*
	 * Save the command in a slot
	 */
	if (mpt_save_cmd(mpt, cmd) == TRUE) {
		cmd->cmd_flags |= CFLAG_PREPARED;
		mpt_start_passthru(mpt, cmd);
	} else {
		mpt_waitq_add(mpt, cmd);
	}

	while ((cmd->cmd_flags & CFLAG_FINISHED) == 0) {
		(void) cv_wait(&mpt->m_passthru_cv, &mpt->m_mutex);
	}

	if (cmd->cmd_flags & CFLAG_PREPARED) {
		memp = MPT_GET_NEXT_FRAME(mpt, cmd->cmd_slot);
		request_hdrp = (msg_request_header_t *)memp;
	}

	if (cmd->cmd_flags & CFLAG_TIMEOUT) {
		status = ETIMEDOUT;
		mpt_log(mpt, CE_WARN, "passthrough command timeout");
		pt_flags |= MPT_PT_CMD_TIMEOUT;
		goto out;
	}

	if (cmd->cmd_rfm) {
		/*
		 * cmd_rfm is zero means the command reply is a CONTEXT
		 * reply and no PCI Write to post the free reply SMFA
		 * because no reply message frame is used.
		 * cmd_rfm is non-zero means the reply is a ADDRESS
		 * reply and reply message frame is used.
		 */
		pt_flags |= MPT_PT_ADDRESS_REPLY;
	}

	if (pkt->pkt_reason == CMD_RESET) {
		status = EAGAIN;
		mpt_log(mpt, CE_WARN, "ioc reset abort passthru");
		goto out;
	}

	if (pkt->pkt_reason == CMD_INCOMPLETE) {
		status = EIO;
		mpt_log(mpt, CE_WARN, "passthrough command incomplete");
		goto out;
	}

	if (pt_flags & MPT_PT_ADDRESS_REPLY) {
		(void) ddi_dma_sync(mpt->m_reply_dma_h, 0, 0,
		    DDI_DMA_SYNC_FORCPU);
		reply_msg = (msg_default_reply_t *)
		    (mpt->m_reply_fifo + (cmd->cmd_rfm - mpt->m_reply_addr));
	}

	if (request_hdrp->Function == MPI_FUNCTION_SCSI_IO_REQUEST) {
		reply_len = sizeof (msg_scsi_io_reply_t);
		sense_len = request_size - 64;
		sense_len = min(sense_len, reply_size - reply_len);
	} else {
		reply_len = min(reply_size, MPT_REPLY_FRAME_SIZE);
		sense_len = 0;
	}

	mutex_exit(&mpt->m_mutex);
	for (i = 0; i < reply_len; i++) {
		if (ddi_copyout((uint8_t *)reply_msg + i, reply + i, 1, mode)) {
			mutex_enter(&mpt->m_mutex);
			status = EFAULT;
			mpt_log(mpt, CE_WARN, "failed to copy out reply data");
			goto out;
		}
	}
	for (i = 0; i < sense_len; i++) {
		if (ddi_copyout((uint8_t *)request_hdrp + 64 + i,
		    reply + reply_len + i, 1, mode)) {
			mutex_enter(&mpt->m_mutex);
			status = EFAULT;
			mpt_log(mpt, CE_WARN, "failed to copy out sense data");
			goto out;
		}
	}

	if (data_size) {
		if (direction != MPT_PASS_THRU_WRITE) {
			for (i = 0; i < data_size; i++) {
				if (ddi_copyout((uint8_t *)(
				    data_dma_state.memp + i), data + i,  1,
				    mode)) {
					mutex_enter(&mpt->m_mutex);
					status = EFAULT;
					mpt_log(mpt, CE_WARN, "failed to copy "
					    "out the reply data");
					goto out;
				}
			}
		}
	}
	mutex_enter(&mpt->m_mutex);

out:
	if (pt_flags & MPT_PT_ADDRESS_REPLY)
		ddi_put32(mpt->m_datap, &mpt->m_reg->m_reply_q,
		    cmd->cmd_rfm);

	/*
	 * Check whether the command was saved and also whether it was
	 * already removed
	 */
	if (cmd && (cmd->cmd_flags & CFLAG_PREPARED)) {
		mpt_remove_cmd(mpt, cmd);
		/*
		 * To avoid the frame being reused to
		 * normal IO, reset the frame.
		 */
		if (memp != NULL)
			bzero(memp, mpt->m_req_frame_size << 2);
	}

	if (pt_flags & MPT_PT_REQUEST_POOL_CMD)
		mpt_return_to_pool(mpt, cmd);
	if (pt_flags & MPT_PT_DATA_ALLOCATED) {
		if (mpt_check_dma_handle(data_dma_state.handle) !=
		    DDI_SUCCESS) {
			ddi_fm_service_impact(mpt->m_dip,
			    DDI_SERVICE_UNAFFECTED);
			status = EFAULT;
		}
		mpt_passthru_dma_free(&data_dma_state);
	}
	if (pt_flags & MPT_PT_DATAOUT_ALLOCATED) {
		if (mpt_check_dma_handle(dataout_dma_state.handle) !=
		    DDI_SUCCESS) {
			ddi_fm_service_impact(mpt->m_dip,
			    DDI_SERVICE_UNAFFECTED);
			status = EFAULT;
		}
		mpt_passthru_dma_free(&dataout_dma_state);
	}
	if (pt_flags & MPT_PT_CMD_TIMEOUT) {
		if (mpt_restart_ioc(mpt)) {
			mpt_log(mpt, CE_WARN, "mpt_restart_ioc failed");
		}
	}
	if (request_msg)
		kmem_free(request_msg, request_size);
	return (status);
}

static int
mpt_pass_thru(mpt_t *mpt, mpt_pass_thru_t *data, int mode)
{
	if (((data->DataSize == 0) &&
	    (data->DataDirection == MPT_PASS_THRU_NONE)) ||
	    ((data->DataSize != 0) &&
	    ((data->DataDirection == MPT_PASS_THRU_READ) ||
	    (data->DataDirection == MPT_PASS_THRU_WRITE) ||
	    ((data->DataDirection == MPT_PASS_THRU_BOTH) &&
	    (data->DataOutSize != 0))))) {
		if (data->DataDirection == MPT_PASS_THRU_BOTH) {
			data->DataDirection = MPT_PASS_THRU_READ;
		} else {
			data->DataOutSize = 0;
		}
		/*
		 * Send passthru request messages
		 */
		return (mpt_do_passthru(mpt,
		    (uint8_t *)((uintptr_t)data->PtrRequest),
		    (uint8_t *)((uintptr_t)data->PtrReply),
		    (uint8_t *)((uintptr_t)data->PtrData),
		    data->RequestSize, data->ReplySize,
		    data->DataSize, data->DataDirection,
		    (uint8_t *)((uintptr_t)data->PtrDataOut),
		    data->DataOutSize, MPT_PASS_THRU_TIME_DEFAULT,
		    mode));
	} else {
		return (EINVAL);
	}
}

/*
 * This routine handles the "event query" ioctl.
 */
static int
mpt_event_query(mpt_t *mpt, mpt_event_query_t *data, int mode, int *rval)
{
	int status;
	mpt_event_query_t driverdata;

	driverdata.Entries = HW_NUM_EVENT_ENTRIES;

	mutex_enter(&mpt->m_mutex);
	driverdata.Types = mpt->m_event_types_enabled;
	mutex_exit(&mpt->m_mutex);

	if (ddi_copyout(&driverdata, data, sizeof (driverdata), mode) != 0) {
		status = EFAULT;
	} else {
		*rval = MPTIOCTL_STATUS_GOOD;
		status = 0;
	}

	return (status);
}

/*
 * This routine handles the "event enable" ioctl.
 */
static int
mpt_event_enable(mpt_t *mpt, mpt_event_enable_t *data, int mode, int *rval)
{
	int status;
	mpt_event_enable_t driverdata;

	if (ddi_copyin(data, &driverdata, sizeof (driverdata), mode) == 0) {
		mutex_enter(&mpt->m_mutex);
		mpt->m_event_types_enabled = driverdata.Types;
		mutex_exit(&mpt->m_mutex);

		*rval = MPTIOCTL_STATUS_GOOD;
		status = 0;
	} else {
		status = EFAULT;
	}
	return (status);
}

/*
 * This routine handles the "event report" ioctl.
 */
static int
mpt_event_report(mpt_t *mpt, mpt_event_report_t *data, int mode, int *rval)
{
	int status;
	mpt_event_report_t driverdata;

	mutex_enter(&mpt->m_mutex);

	if (ddi_copyin(&data->Size, &driverdata.Size, sizeof (driverdata.Size),
	    mode) == 0) {
		if (driverdata.Size >= sizeof (mpt->m_events)) {
			if (ddi_copyout(mpt->m_events, data->Events,
			    sizeof (mpt->m_events), mode) != 0) {
				status = EFAULT;
			} else {
				if (driverdata.Size > sizeof (mpt->m_events)) {
					driverdata.Size =
					    sizeof (mpt->m_events);
					if (ddi_copyout(&driverdata.Size,
					    &data->Size,
					    sizeof (driverdata.Size),
					    mode) != 0) {
						status = EFAULT;
					} else {
						*rval = MPTIOCTL_STATUS_GOOD;
						status = 0;
					}
				} else {
					*rval = MPTIOCTL_STATUS_GOOD;
					status = 0;
				}
			}
		} else {
			*rval = MPTIOCTL_STATUS_LEN_TOO_SHORT;
			status = 0;
		}
	} else {
		status = EFAULT;
	}

	mutex_exit(&mpt->m_mutex);
	return (status);
}

static void
mpt_lookup_pci_data(mpt_t *mpt, mpt_dmi_data_t *dmi_data)
{
	int	*reg_data;
	uint_t	reglen;

	/*
	 * Lookup the 'reg' property and extract the data from there
	 */
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, mpt->m_dip,
	    DDI_PROP_DONTPASS, "reg", &reg_data, &reglen) ==
	    DDI_PROP_SUCCESS) {
		/*
		 * Extract the PCI data from the 'reg' property first DWORD.
		 * The entry looks like the following:
		 * First DWORD:
		 * Bits 0 - 7 8-bit Register number
		 * Bits 8 - 10 3-bit Function number
		 * Bits 11 - 15 5-bit Device number
		 * Bits 16 - 23 8-bit Bus number
		 * Bits 24 - 25 2-bit Address Space type identifier
		 */
		dmi_data->PciBusNumber = (reg_data[0] & 0x00FF0000) >> 16;
		dmi_data->PciDeviceNumber = (reg_data[0] & 0x0000F800) >> 11;
		dmi_data->PciFunctionNumber = (reg_data[0] & 0x00000700) >> 8;
		ddi_prop_free((void *)reg_data);
	} else {
		/*
		 * If we can't determine the PCI data then we fill in 0xFF for
		 * the data to indicate this.
		 */
		dmi_data->PciBusNumber = 0xFF;
		dmi_data->PciDeviceNumber = 0xFF;
		dmi_data->PciFunctionNumber = 0xFF;
	}
}

static void
mpt_read_dmi_data(mpt_t *mpt, mpt_dmi_data_t *data)
{
	int status = 0;
	int tid;
	uint_t sync_speed = 0;
	char *verstr = MPT_MOD_STRING;

	if (data->StructureLength >= sizeof (mpt_dmi_data_t)) {
		data->StructureLength = (uint32_t)sizeof (mpt_dmi_data_t);
		data->MajorVersion = MPT_DMI_DATA_VERSION;
		data->MinSyncPeriodNs = 0;
		/*
		 * MaxWidth field only applies to parallel buses, so for SAS,
		 * whatever value is okay. The suggest fix from LSI is
		 * either 0 (since the field does not apply), or 255 (the
		 * largest value that will fit).
		 */
		if (mpt->m_ntargets < 256) {
			data->MaxWidth = (uint8_t)(mpt->m_ntargets);
		} else {
			data->MaxWidth = 255;
		}
		data->HostScsiId = (uint8_t)mpt->m_mptid;

		mpt_lookup_pci_data(mpt, data);
		data->PciDeviceId = mpt->m_devid;
		data->PciRevision = mpt->m_revid;
		data->HwBusMode = mpt->m_bus_type;
		if (MPT_IS_SCSI(mpt)) {
			sync_speed = (mpt->m_scsi_params >> 8) & 0xFF;
			data->MinSyncPeriodNs = MPT_PERIOD_TO_NS
			    (sync_speed);
		}
		for (tid = 0; tid < mpt->m_ntargets; tid++) {
			uint32_t scsi_params = 0;
			/*
			 * The mpt_dmi_data_t data structure is defined and
			 * used by the vendor software, we can not expand
			 * the definition to m_ntargets, so here just limit
			 * all the operations to 256 to avoid system crash
			 */
			if (tid >= 256)
				break;

			data->DevSpeed[tid] = 0;
			data->DevWidth[tid] = 8;
			data->DevFlags[tid] = 0;
			if (MPT_IS_SAS(mpt))
				continue;
			status = mpt_get_scsi_device_params(mpt, tid,
			    &scsi_params);
			if (status == -1)
				continue;
			sync_speed = (scsi_params >> 8) & 0xFF;
			data->DevSpeed[tid] = MPT_PERIOD_TO_NS(sync_speed);
			data->DevWidth[tid] = (scsi_params &
			    MPI_SCSIDEVPAGE0_NP_WIDE) ? 16 : 8;
			if (scsi_params & MPI_SCSIDEVPAGE0_NP_DT) {
				data->DevFlags[tid] |= MPT_FLAGS_DT;
			}
			if (scsi_params & MPI_SCSIDEVPAGE0_NP_AIP) {
				data->DevFlags[tid] |= MPT_FLAGS_ASYNC_PROT;
			}
		}
		(void) strcpy((char *)&data->DriverVersion[0], verstr);
	} else {
		data->StructureLength = (uint32_t)sizeof (mpt_dmi_data_t);
	}
}

static int
mpt_ioctl(dev_t dev, int cmd, intptr_t data, int mode, cred_t *credp, int *rval)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(rval))
#endif

	raid_config_t config;
	int status = 0;
	mpt_t *mpt;
	update_flash_t flashdata;
	mpt_slots_t *slots;
	uint16_t diskid;
	int ret = 0;
	int ndisks = 0;
	int i, n;
	mpt_pass_thru_t passthru_data;
	mpt_dmi_data_t dmi_data;
	int copylen;

	mpt = ddi_get_soft_state(mpt_state, MINOR2INST(getminor(dev)));
	if (mpt == NULL) {
		return (ENXIO);
	}
	if (secpolicy_sys_config(credp, B_FALSE) != 0) {
		return (EPERM);
	}

	/* Make sure power level is D0 before accessing registers */
	mutex_enter(&mpt->m_mutex);
	if (mpt->m_options & MPT_OPT_PM) {
		(void) pm_busy_component(mpt->m_dip, 0);
		if (mpt->m_power_level != PM_LEVEL_D0) {
			mutex_exit(&mpt->m_mutex);
			if (pm_raise_power(mpt->m_dip, 0, PM_LEVEL_D0) !=
			    DDI_SUCCESS) {
				mpt_log(mpt, CE_WARN,
				    "mpt%d: mpt_ioctl: Raise power request "
				    "failed.", mpt->m_instance);
				(void) pm_idle_component(mpt->m_dip, 0);
				return (ENXIO);
			}
		} else {
			mutex_exit(&mpt->m_mutex);
		}
	} else {
		mutex_exit(&mpt->m_mutex);
	}

	switch (cmd) {
		case RAID_GETCONFIG:
			if ((mpt->m_productid &
			    MPI_FW_HEADER_PID_PROD_IM_SCSI) == 0) {
				status = ENOTTY;
				break;
			}

			/*
			 * retrieve the caller's config
			 */
			if (ddi_copyin((void *)data, &config,
			    sizeof (struct raid_config), mode)) {
				status = EFAULT;
				break;
			}

			/*
			 * use unitid to index into slots->m_raidvol
			 */
			if ((n = config.unitid) < 0 || n >= MPT_MAX_RAIDVOLS) {
				status = EFAULT;
				break;
			}

			/*
			 * populate the config
			 */
			mutex_enter(&mpt->m_mutex);
			slots = mpt->m_active;
			if (MPT_RAID_EXISTS(mpt, n)) {
				ndisks = slots->m_raidvol[n].m_ndisks;
				/*
				 * highest target number is 15 per SCSI
				 * highest target number is 127 per SAS
				 * 255 is returned for a missing disk
				 *  so anything higher is invalid.
				 */
				for (i = 0; i < ndisks; i++) {
					diskid = slots->m_raidvol[n].
					    m_diskid[i];
					if ((MPT_IS_SCSI(mpt) && diskid > 15) ||
					    (MPT_IS_SAS(mpt) &&
					    (diskid > 127 && diskid != 255))) {
						status = EINVAL;
						mutex_exit(&mpt->m_mutex);
						goto invalid_targ;
					}

					config.disk[i] =
					    slots->m_raidvol[n].m_diskid[i];
					config.diskstatus[i] =
					    slots->m_raidvol[n].m_diskstatus[i];
				}

				config.ndisks = ndisks;
				config.targetid =
				    slots->m_raidvol[n].m_raidtarg;
				config.state = slots->m_raidvol[n].m_state;
				config.flags = slots->m_raidvol[n].m_flags;
				config.raid_capacity =
				    slots->m_raidvol[n].m_raidsize;
				config.raid_level =
				    slots->m_raidvol[n].m_raidlevel;
			} else {
				/*
				 * there is no volume configured
				 * with this unitid
				 */
				config.ndisks = 0;
			}

			mutex_exit(&mpt->m_mutex);

			if (ddi_copyout(&config, (void *)data,
			    sizeof (config), mode)) {
				status = EFAULT;
			}
invalid_targ:
			break;
		case RAID_UPDATEFW:
		{
#ifdef _MULTI_DATAMODEL
			if (ddi_model_convert_from(mode & FMODELS) ==
			    DDI_MODEL_ILP32) {
				update_flash_32_t flashdata32;
				if (ddi_copyin((void *)data, &flashdata32,
					sizeof (struct update_flash), mode)) {
					status = EFAULT;
					break;
				}
				flashdata.ptrbuffer =
				    (caddr_t)(uintptr_t)flashdata32.ptrbuffer;
				flashdata.size = (uint_t)flashdata32.size;
				flashdata.type = (uint8_t)flashdata32.type;
			}
#else
			if (ddi_copyin((void *)data, &flashdata,
			    sizeof (struct update_flash), mode)) {
				status = EFAULT;
			}
#endif
			mutex_enter(&mpt->m_mutex);
			if (mpt_update_flash(mpt, flashdata.ptrbuffer,
			    flashdata.size, flashdata.type, mode)) {
				status = EFAULT;
			}

			if ((status != EFAULT) && MPT_IS_SCSI(mpt)) {
				if (mpt_check_flash(mpt, flashdata.ptrbuffer,
				    flashdata.size, flashdata.type, mode)) {
					status = EFAULT;
					mpt_log(mpt, CE_WARN,
					    "Flash update data miscompare."
					    " Re-flash needed.");
				}
			}

			/*
			 * Reset the chip to start using the new
			 * firmware
			 */
			if ((status != EFAULT) && (flashdata.type ==
			    MPI_FW_UPLOAD_ITYPE_FW_FLASH)) {
				if (mpt_restart_ioc(mpt)) {
					status = EFAULT;
				}
			}
			mutex_exit(&mpt->m_mutex);
			break;
		}
		case RAID_NUMVOLUMES:
			if (MPT_IS_SCSI(mpt))
				ret = 1;
			else if (MPT_IS_SAS(mpt))
				ret = 2;

			if (ddi_copyout(&ret, (void *)data, sizeof (ret),
			    mode)) {
				status = EFAULT;
			}
			break;
		case MPTIOCTL_PASS_THRU:
			/*
			 * The user has requested to pass through a command to
			 * be executed by the MPT firmware.  Call our routine
			 * which does this.  Only allow one passthru IOCTL at
			 * one time. Other threads will block on
			 * m_passthru_mutex, which is of adaptive variant.
			 */
			if (ddi_copyin((void *)data, &passthru_data,
			    sizeof (mpt_pass_thru_t), mode)) {
				status = EFAULT;
				break;
			}
			mutex_enter(&mpt->m_passthru_mutex);
			mutex_enter(&mpt->m_mutex);
			status = mpt_pass_thru(mpt, &passthru_data, mode);
			mutex_exit(&mpt->m_mutex);
			mutex_exit(&mpt->m_passthru_mutex);
			break;
		case MPTIOCTL_GET_DMI_DATA:
			/*
			 * The user has requested to read DMI data.  Call our
			 * routine which does this.
			 */
			bzero(&dmi_data, sizeof (mpt_dmi_data_t));
			if (ddi_copyin((void *)data, (void *)&dmi_data,
			    sizeof (dmi_data.StructureLength), mode)) {
				status = EFAULT;
				break;
			}
			if (dmi_data.StructureLength >=
			    sizeof (mpt_dmi_data_t)) {
				dmi_data.StructureLength = (uint32_t)
				    sizeof (mpt_dmi_data_t);
				copylen = sizeof (mpt_dmi_data_t);
				mutex_enter(&mpt->m_mutex);
				mpt_read_dmi_data(mpt, &dmi_data);
				mutex_exit(&mpt->m_mutex);
				*rval = 0;
			} else {
				dmi_data.StructureLength = (uint32_t)
				    sizeof (mpt_dmi_data_t);
				copylen = sizeof (dmi_data.StructureLength);
				*rval = 1;
			}

			if (ddi_copyout((void *)(&dmi_data), (void *)data,
			    copylen, mode) != 0) {
				status = EFAULT;
			}
			break;
		case MPTIOCTL_EVENT_QUERY:
			/*
			 * The user has done an event query. Call our routine
			 * which does this.
			 */
			status = mpt_event_query(mpt,
			    (mpt_event_query_t *)data, mode, rval);
			break;
		case MPTIOCTL_EVENT_ENABLE:
			/*
			 * The user has done an event enable. Call our routine
			 * which does this.
			 */
			status = mpt_event_enable(mpt,
			    (mpt_event_enable_t *)data, mode, rval);
			break;
		case MPTIOCTL_EVENT_REPORT:
			/*
			 * The user has done an event report. Call our routine
			 * which does this.
			 */
			status = mpt_event_report(mpt,
			    (mpt_event_report_t *)data, mode, rval);
			break;
		default:
			status = scsi_hba_ioctl(dev, cmd, data, mode, credp,
			    rval);
			break;
	}

	/*
	 * Report idle status to pm after grace period because
	 * multiple ioctls may be queued and raising power
	 * for every ioctl is time consuming.  If a timeout is
	 * pending for the previous ioctl, cancel the timeout and
	 * report idle status to pm because calls to pm_busy_component(9F)
	 * are stacked.
	 */
	mutex_enter(&mpt->m_mutex);
	if (mpt->m_options & MPT_OPT_PM) {
		if (mpt->m_pm_timeid != 0) {
			timeout_id_t tid = mpt->m_pm_timeid;
			mpt->m_pm_timeid = 0;
			mutex_exit(&mpt->m_mutex);
			(void) untimeout(tid);
			/*
			 * Report idle status for previous ioctl since
			 * calls to pm_busy_component(9F) are stacked.
			 */
			(void) pm_idle_component(mpt->m_dip, 0);
			mutex_enter(&mpt->m_mutex);
		}
		mpt->m_pm_timeid = timeout(mpt_idle_pm, mpt,
		    drv_usectohz((clock_t)mpt->m_pm_idle_delay * 1000000));
	}
	mutex_exit(&mpt->m_mutex);

	return (status);
}

static int
mpt_set_ioc_params(mpt_t *mpt)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_ioc_1_t *iocpage;
	int recv_numbytes;
	caddr_t recv_memp, page_memp;
	int rval = -1;
	uint32_t flagslength;
	int recv_dmastate = 0;
	int page_dmastate = 0;

	if (mpt_send_config_request_msg(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_PAGETYPE_IOC, 0, 1, 0, 0, 0, 0)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular =
	    (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	} else {
		recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;
	}

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config_reply structure.");
		goto done;
	} else {
		recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;
	}

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	} else {
		recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	}

	bzero(recv_memp, sizeof (*configreply));
	configreply = (struct msg_config_reply *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	/*
	 * get config reply message
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_set_ioc_params header", 1)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular =
	    (sizeof (struct config_page_ioc_1));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	} else {
		page_dmastate |= MPT_DMA_HANDLE_ALLOCD;
	}

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (struct config_page_ioc_1)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto done;
	} else {
		page_dmastate |= MPT_DMA_MEMORY_ALLOCD;
	}

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	} else {
		page_dmastate |= MPT_DMA_HANDLE_BOUND;
	}

	bzero(page_memp, sizeof (config_page_ioc_1_t));
	iocpage = (struct config_page_ioc_1 *)page_memp;
	flagslength = sizeof (struct config_page_ioc_1);

	/*
	 * set up scatter gather element flags
	 */
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_IOC_TO_HOST |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

	/*
	 * Give reply address to IOC to store config page in and send
	 * config request out.
	 */

	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    MPI_CONFIG_PAGETYPE_IOC, 0, 1,
	    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    flagslength, page_cookie.dmac_address)) {
			goto done;
	}

	/*
	 * get reply handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_set_ioc_params config", 1)) {
		goto done;
	}

	(void) ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORCPU);
	flagslength &= ~(MPI_SGE_FLAGS_IOC_TO_HOST <<
	    MPI_SGE_FLAGS_SHIFT);
	flagslength |= (MPI_SGE_FLAGS_HOST_TO_IOC <<
	    MPI_SGE_FLAGS_SHIFT);

	/*
	 * Currently turn off coalescing since it was found to
	 * increase performance
	 */
	ddi_put32(page_accessp, &iocpage->Flags, 0);
	ddi_put32(page_accessp, &iocpage->CoalescingTimeout, 0);
	ddi_put8(page_accessp, &iocpage->CoalescingDepth, 0);

	(void) ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORDEV);
	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
	    ddi_get8(recv_accessp, &configreply->Header.PageType), 0,
	    1, ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    flagslength, page_cookie.dmac_address)) {
			goto done;
	}

	/*
	 * get reply handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_set_ioc_params update", 1)) {
		goto done;
	}

	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
	} else {
		rval = 0;
	}

done:
	/*
	 * free up memory
	 */
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);

	return (rval);
}

int
mpt_restart_ioc(mpt_t *mpt) {
	int i, slot;
	mpt_cmd_t *cmd;
	int rval = 0;

	ASSERT(mutex_owned(&mpt->m_mutex));

	/*
	 * Disable interrupts
	 */
	MPT_DISABLE_INTR(mpt);

	/*
	 * Set all throttles to HOLD
	 */
	for (i = 0; i < mpt->m_ntargets; i++) {
		mpt_set_throttle(mpt, i, HOLD_THROTTLE);
	}

	/*
	 * mark all active commands as being reset
	 */
	for (slot = 0; slot < mpt->m_active->m_n_slots; slot ++) {
		if ((cmd = mpt->m_active->m_slot[slot]) != NULL) {
			if (cmd->cmd_flags & CFLAG_PASSTHRU) {
				mpt_remove_cmd(mpt, cmd);
				continue;
			}
			mpt_remove_cmd(mpt, cmd);
			mpt_set_pkt_reason(mpt, cmd, CMD_RESET, STAT_BUS_RESET);
			mpt_doneq_add(mpt, cmd);
		}
	}

	/*
	 * Flush the waitq
	 */
	while ((cmd = mpt_waitq_rm(mpt)) != NULL) {
		mpt_set_pkt_reason(mpt, cmd, CMD_RESET, STAT_BUS_RESET);
		if (cmd->cmd_flags & CFLAG_PASSTHRU) {
			cmd->cmd_flags |= CFLAG_FINISHED;
			cv_broadcast(&mpt->m_passthru_cv);
		} else {
			mpt_doneq_add(mpt, cmd);
		}
	}

	/*
	 * Flush the tx_waitq
	 */
	mutex_enter(&mpt->m_waitq_mutex);
	while ((cmd = mpt_tx_waitq_rm(mpt)) != NULL) {
		mutex_exit(&mpt->m_waitq_mutex);
		mpt_set_pkt_reason(mpt, cmd, CMD_RESET, STAT_BUS_RESET);
		mpt_doneq_add(mpt, cmd);
		mutex_enter(&mpt->m_waitq_mutex);
	}
	mutex_exit(&mpt->m_waitq_mutex);

	/*
	 * reinitialize the chip, if the hardware on an abnormal
	 * state, then reset the ioc and initialize again to
	 * bring IOC to Operational state.
	 */
	if (mpt_init_chip(mpt, FALSE)) {
		if (mpt_ioc_reset(mpt) == DDI_FAILURE) {
			rval = -1;
		} else if (mpt_init_chip(mpt, FALSE)) {
			rval = -1;
		}
	}

	/*
	 * Reset the throttles
	 */
	for (i = 0; i < mpt->m_ntargets; i++) {
		mpt_set_throttle(mpt, i, MAX_THROTTLE);
	}

	/*
	 * Enable interrupts again
	 */
	MPT_ENABLE_INTR(mpt);

	mpt_doneq_empty(mpt);

	return (rval);
}

static int
mpt_init_chip(mpt_t *mpt, int first_time)
{
	ddi_dma_cookie_t cookie;
	int i, port, bus_time;
	int sgemax, framenum, sgllen;
	if (first_time == FALSE) {
		/*
		 * Setup configuration space
		 */
		if (mpt_config_space_init(mpt) == FALSE) {
			mpt_log(mpt, CE_WARN, "mpt_config_space_init failed");
			goto fail;
		}
	}

	/*
	 * Check to see if the firmware image is valid
	 */
	if (ddi_get32(mpt->m_datap, &mpt->m_reg->m_diag) &
	    (MPI_DIAG_FLASH_BAD_SIG | MPI_DIAG_DISABLE_ARM)) {
		if (MPT_IS_SCSI(mpt)) {
			if (!mpt_can_download_firmware(mpt)) {
				mpt_log(mpt, CE_WARN,
				    "firmware image bad or mpt ARM "
				    "disabled.  Cannot attempt to recover via "
				    "firmware download because driver's stored "
				    "firmware is incompatible with this adapter"
				    ".");
				goto fail;
			} else if (mpt_download_firmware(mpt)) {
				mpt_log(mpt, CE_WARN, "firmware load failed");
				goto fail;
			}
		} else {
			mpt_log(mpt, CE_WARN, "mpt ARM disabled");
			goto fail;
		}
	}

	/*
	 * Reset the chip
	 */
	if (mpt_ioc_reset(mpt) == DDI_FAILURE) {
		mpt_log(mpt, CE_WARN, "hard reset failed");
		return (DDI_FAILURE);
	}

	/*
	 * Do some initilization only needed during attach
	 */
	if (first_time) {
		/*
		 * Get ioc facts from adapter
		 */
		if (mpt_ioc_get_facts(mpt) == DDI_FAILURE) {
			mpt_log(mpt, CE_WARN, "mpt_ioc_get_facts failed");
			goto fail;
		}
		/*
		 * Calculate the s/g list length by frame size and
		 * chain depth. We initialize the sgllen to how many
		 * sg elements can be accomodated in main frame.
		 */
		sgllen = MPT_MAX_FRAME_SGES64(mpt);
		framenum = mpt->m_max_chain_depth;

		if (framenum > 1) {
			/*
			 * Sgemax is the number of SGE's that will fit
			 * each extra frame and framenum is total
			 * number of frames we'll need.  We shift the frame_size
			 * over by 2 because frame_size is the number of
			 * 32 bits words and we want number of bytes.
			 * 1 sge entry per frame is reseverd for the chain
			 * element thus the -1 below.
			 */
			sgemax = (((mpt->m_req_frame_size << 2) /
			    sizeof (sge_simple64_t)) - 1);
			/*
			 * Hereby we start to deal with multiple frames.
			 * The process is as follows:
			 * 1. Calculate how many sges can be accomodated in the
			 *    main frame. We need to do this separately since it
			 *    contains the SCSI IO request header.
			 *    Note that the space reserved for the last sge is
			 *    used for chain element. So the first frame can
			 *    accomodate (MPT_MAX_FRAME_SGES64(mpt) - 1) sges.
			 * 2. Calculate how many sges can be accomodated in the
			 *    following frames except last frame. Each frame can
			 *    fill (sgemax) sges.
			 * 3. Calculate how many sges can be filled in the last
			 *    frame. We need to do this separately since the
			 *    last frame need not chain element. Last frame can
			 *    fill (sgemax + 1) sges.
			 * 4. Calculate the sum of sges of all frames as below:
			 *    (MPT_MAX_FRAME_SGES64(mpt) - 1) + sgemax *
			 *    (framenum - 2) + (sgemax + 1);
			 *    By optimization, we got the following expression:
			 */
			sgllen += ((framenum - 1) * sgemax);
		}

		/*
		 * If sgllen is too big (greater than MPT_MAX_DMA_SEGS),we
		 * pre-reserve chain buffers for each command which can
		 * accommodate MPT_MAX_DMA_SEGS sges as default for avoid
		 * reserving too many DMA resources for mpt. At same time
		 * we adjust the value of m_max_chain_depth which is number
		 * of chain buffer frame to what we really needs. Once the
		 * sges needed exceed MPT_MAX_DMA_SEGS, mpt driver will
		 * allocate extra chain buffer by calling
		 * mpt_alloc_extra_cmd_mem().
		 */
		if (sgllen > MPT_MAX_DMA_SEGS) {
			if (MPT_MAX_FRAME_SGES64(mpt) >= MPT_MAX_DMA_SEGS) {
				mpt->m_max_chain_depth = 1;
			} else {
				/*
				 * framenum is the number of how many frames
				 * needed except main frame
				 */
				framenum = (MPT_MAX_DMA_SEGS -
				    (MPT_MAX_FRAME_SGES64(mpt) - 1)) / sgemax;
				/*
				 * A little check to see if we need to round
				 * up the number of frames we need, because
				 * last frame can accomodate segmax + 1 sges.
				 */
				if ((MPT_MAX_DMA_SEGS -
				    (MPT_MAX_FRAME_SGES64(mpt) - 1)) -
				    (framenum *
				    sgemax) > 1) {
					framenum += 1;
				}
				/*
				 * Here add one means framenum add main frame
				 */
				mpt->m_max_chain_depth = framenum + 1;
			}
		}
		mpt->m_io_dma_attr.dma_attr_sgllen = sgllen;

		/*
		 * Allocate message post frames
		 */
		if (mpt_alloc_post_frames(mpt) == DDI_FAILURE) {
			mpt_log(mpt, CE_WARN, "mpt_alloc_post_frames failed");
			goto fail;
		}
	}

	/*
	 * Re-Initialize ioc to operational state
	 */
	if (mpt_ioc_init(mpt) == DDI_FAILURE) {
		mpt_log(mpt, CE_WARN, "mpt_ioc_init failed");
		goto fail;
	}

	/*
	 * Fill reply fifo with addresses
	 */
	if (first_time) {
		if (mpt_ioc_init_reply_queue(mpt) == DDI_FAILURE) {
			mpt_log(mpt, CE_WARN, "mpt_init_reply_queue failed");
			goto fail;
		}
	} else {
		cookie.dmac_address = mpt->m_reply_addr;
		while (cookie.dmac_address < (mpt->m_reply_addr +
		    (MPT_REPLY_FRAME_SIZE * mpt->m_max_reply_depth))) {
			ddi_put32(mpt->m_datap, &mpt->m_reg->m_reply_q,
			    cookie.dmac_address);
			cookie.dmac_address += MPT_REPLY_FRAME_SIZE;
		}
	}

	/*
	 * Enable ports and set port params
	 */
	if (first_time) {
		mpt->m_ntargets = 0;
		for (port = 0; port < mpt->m_num_ports; port++) {
			if (mpt_ioc_get_port_facts(mpt, port) == DDI_FAILURE) {
				mpt_log(mpt, CE_WARN, "mpt_ioc_get_port_facts"
				    " failed");
				goto fail;
			}
			mpt->m_ntargets += mpt->m_maxdevices[port];
		}

		/*
		 * For SAS devices, the target number might exceed max number
		 * of targets, so we would initialize all preallocated entries
		 */
		if (MPT_IS_SAS(mpt)) {
			mpt->m_ntargets = MPT_MAX_TARGETS;
		}
		if (mpt->m_ntargets == 0) {
			mpt->m_ntargets = MPT_MAX_TARGETS;
			mpt_log(mpt, CE_NOTE,
			    "?mpt%d: m_ntargets would have been 0, using %d\n",
			    mpt->m_instance, MPT_MAX_TARGETS);
		}
	}

	if (MPT_IS_SCSI(mpt)) {
		bus_time = ddi_prop_get_int(DDI_DEV_T_ANY, mpt->m_dip,
		    0, "mpt-on-bus-time", MPT_ON_BUS_TIME_DEFAULT);
		if (bus_time == MPT_ON_BUS_TIME_DISABLED) {
			NDBG2(("%s%d: mpt-on-bus-time: disabled",
			    ddi_driver_name(mpt->m_dip),
			    ddi_get_instance(mpt->m_dip)));
		} else {
			if (bus_time > MPT_ON_BUS_TIME_MAX)
				bus_time = MPT_ON_BUS_TIME_MAX;
			else if (bus_time < MPT_ON_BUS_TIME_MIN)
				bus_time = MPT_ON_BUS_TIME_MIN;
			NDBG2(("%s%d: mpt-on-bus-time: %d",
			    ddi_driver_name(mpt->m_dip),
			    ddi_get_instance(mpt->m_dip), bus_time));
			bus_time *= MPT_ON_BUS_TIME_UNITS;
		}
	}

	for (port = 0; port < mpt->m_num_ports; port++) {
		if (mpt_ioc_enable_port(mpt, port) == DDI_FAILURE) {
			mpt_log(mpt, CE_WARN, "mpt_ioc_enable_port failed");
			goto fail;
		}
		if (MPT_IS_SCSI(mpt)) {
			if (mpt_set_port_params(mpt, port, bus_time)) {
				mpt_log(mpt, CE_WARN, "mpt_set_port_params "
				    "failed");
				goto fail;
			}
		}
	}

	/*
	 * First, make sure the HBA is set in "initiator" mode.  Once that
	 * is complete, get the base WWID.
	 */

	if (first_time && MPT_IS_SAS(mpt)) {
		if (mpt_set_init_mode(mpt)) {
			mpt_log(mpt, CE_WARN, "mpt_set_init_mode failed");
			goto fail;
		}

		mpt_log(mpt, CE_NOTE, "?NVDATA default version 0x%x, "
		    "persistent version 0x%x",
		    mpt->m_nvdata_ver_default, mpt->m_nvdata_ver_persistent);
		(void) ddi_prop_update_int(DDI_DEV_T_NONE, mpt->m_dip,
		    NVDATA_VER_DEF_PROP, mpt->m_nvdata_ver_default);
		(void) ddi_prop_update_int(DDI_DEV_T_NONE, mpt->m_dip,
		    NVDATA_VER_PERSIST_PROP, mpt->m_nvdata_ver_persistent);

		for (port = 0; port < mpt->m_num_ports; port++) {
			if (mpt_get_wwid(mpt) == DDI_FAILURE) {
				mpt_log(mpt, CE_WARN, "mpt_get_wwid failed");
				goto fail;
			}
		}
	}

	/*
	 * Reconfigure scsi parameters
	 */
	if ((first_time != TRUE) && MPT_IS_SCSI(mpt)) {
		for (i = 0; i < NTARGETS_WIDE; i++) {
			mpt_set_scsi_options(mpt, i,
			    mpt->m_target_scsi_options[i]);
		}
	}

	if (mpt_set_ioc_params(mpt)) {
		mpt_log(mpt, CE_WARN, "mpt_set_ioc_params failed");
		goto fail;
	}

	/*
	 * reset the bus and enable events
	 */
	if (first_time != TRUE) {
		/*
		 * If m_active is NULL, we're really still in the first-time
		 * init, but resetting due to just having changed from target
		 * to initiator mode.  No need to bus reset in that case.
		 */

		if (mpt->m_active) {
			(void) mpt_ioc_task_management(mpt,
			    MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS, 0, 0, 0);
		}
		if (mpt_ioc_enable_event_notification(mpt)) {
			goto fail;
		}
	}

	/*
	 * We need checks in attach and these.
	 * chip_init is called in mult. places
	 */

	if ((mpt_check_dma_handle(mpt->m_dma_hdl) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(mpt->m_reply_dma_h) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(mpt->m_hshk_dma_hdl) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_LOST);
		return (DDI_FAILURE);
	}

	/* Check all acc handles */
	if ((mpt_check_acc_handle(mpt->m_datap) != DDI_SUCCESS) ||
	    (mpt_check_acc_handle(mpt->m_config_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_LOST);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);

fail:
	return (DDI_FAILURE);
}

static int
mpt_init_pm(mpt_t *mpt)
{
	char		pmc_name[16];
	char		*pmc[] = {
				NULL,
				"0=Off (PCI D3 State)",
				"3=On (PCI D0 State)",
				NULL
			};

	/*
	 * If power management is supported by this chip, create
	 * pm-components property for the power management framework
	 */
	(void) sprintf(pmc_name, "NAME=mpt%d", mpt->m_instance);
	pmc[0] = pmc_name;
	if (ddi_prop_update_string_array(DDI_DEV_T_NONE, mpt->m_dip,
	    "pm-components", pmc, 3) != DDI_PROP_SUCCESS) {
		mpt->m_options &= ~MPT_OPT_PM;
		mpt_log(mpt, CE_WARN,
		    "mpt%d: pm-component property creation failed.",
		    mpt->m_instance);
	}

	/*
	 * Power on device.
	 */
	(void) pm_busy_component(mpt->m_dip, 0);
	if (pm_raise_power(mpt->m_dip, 0, PM_LEVEL_D0) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "mpt%d: Raise power request failed.",
		    mpt->m_instance);
		(void) pm_idle_component(mpt->m_dip, 0);
		(void) ddi_prop_remove(DDI_DEV_T_NONE, mpt->m_dip,
		    "pm-components");
		return (DDI_FAILURE);
	}

	/*
	 * Set pm idle delay.
	 */
	mpt->m_pm_idle_delay = ddi_prop_get_int(DDI_DEV_T_ANY,
	    mpt->m_dip, 0, "mpt-pm-idle-delay", MPT_PM_IDLE_TIMEOUT);

	return (DDI_SUCCESS);
}

/*
 * mpt_add_intrs:
 *
 * Register FIXED or MSI interrupts.
 */
static int
mpt_add_intrs(mpt_t *mpt, int intr_type)
{
	dev_info_t	*dip = mpt->m_dip;
	int		avail, actual, count = 0;
	int		i, flag, ret;

	NDBG6(("mpt_add_intrs:interrupt type 0x%x", intr_type));

	/* Get number of interrupts */
	ret = ddi_intr_get_nintrs(dip, intr_type, &count);
	if ((ret != DDI_SUCCESS) || (count == 0)) {
		mpt_log(mpt, CE_WARN, "ddi_intr_get_nintrs() failed, "
		    "ret %d count %d\n", ret, count);

		return (DDI_FAILURE);
	}

	/* Get number of available interrupts */
	ret = ddi_intr_get_navail(dip, intr_type, &avail);
	if ((ret != DDI_SUCCESS) || (avail == 0)) {
		mpt_log(mpt, CE_WARN, "ddi_intr_get_navail() failed, "
		    "ret %d avail %d\n", ret, avail);

		return (DDI_FAILURE);
	}

	if (avail < count) {
		mpt_log(mpt, CE_NOTE, "ddi_intr_get_nvail returned %d, "
		    "navail() returned %d", count, avail);
	}

	/* Mpt only have one interrupt routine */
	if ((intr_type == DDI_INTR_TYPE_MSI) && (count > 1)) {
		count = 1;
	}

	/* Allocate an array of interrupt handles */
	mpt->m_intr_size = count * sizeof (ddi_intr_handle_t);
	mpt->m_htable = kmem_alloc(mpt->m_intr_size, KM_SLEEP);

	flag = (intr_type == DDI_INTR_TYPE_MSI) ?
	    DDI_INTR_ALLOC_STRICT:DDI_INTR_ALLOC_NORMAL;

	/* call ddi_intr_alloc() */
	ret = ddi_intr_alloc(dip, mpt->m_htable, intr_type, 0,
	    count, &actual, flag);

	if ((ret != DDI_SUCCESS) || (actual == 0)) {
		mpt_log(mpt, CE_WARN, "ddi_intr_alloc() failed, ret %d\n", ret);

		kmem_free(mpt->m_htable, mpt->m_intr_size);
		return (DDI_FAILURE);
	}

	/* use interrupt count returned or abort? */
	if (actual < count) {
		mpt_log(mpt, CE_NOTE, "Requested: %d, Received: %d\n",
		    count, actual);
	}

	mpt->m_intr_cnt = actual;

	/*
	 * Get priority for first msi, assume remaining are all the same
	 */
	if ((ret = ddi_intr_get_pri(mpt->m_htable[0],
	    &mpt->m_intr_pri)) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "ddi_intr_get_pri() failed %d\n", ret);

		/* Free already allocated intr */
		for (i = 0; i < actual; i++) {
			(void) ddi_intr_free(mpt->m_htable[i]);
		}

		kmem_free(mpt->m_htable, mpt->m_intr_size);
		return (DDI_FAILURE);
	}

	/* Test for high level mutex */
	if (mpt->m_intr_pri >= ddi_intr_get_hilevel_pri()) {
		mpt_log(mpt, CE_WARN, "mpt_add_intrs: "
		    "Hi level interrupt not supported\n");

		/* Free already allocated intr */
		for (i = 0; i < actual; i++) {
			(void) ddi_intr_free(mpt->m_htable[i]);
		}

		kmem_free(mpt->m_htable, mpt->m_intr_size);
		return (DDI_FAILURE);
	}

	/* Call ddi_intr_add_handler() */
	for (i = 0; i < actual; i++) {
		if ((ret = ddi_intr_add_handler(mpt->m_htable[i], mpt_intr,
		    (caddr_t)mpt, (caddr_t)(uintptr_t)i)) != DDI_SUCCESS) {
			mpt_log(mpt, CE_WARN, "ddi_intr_add_handler() "
			    "failed %d\n", ret);

			/* Free already allocated intr */
			for (i = 0; i < actual; i++) {
				(void) ddi_intr_free(mpt->m_htable[i]);
			}

			kmem_free(mpt->m_htable, mpt->m_intr_size);
			return (DDI_FAILURE);
		}
	}

	if ((ret = ddi_intr_get_cap(mpt->m_htable[0], &mpt->m_intr_cap))
	    != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "ddi_intr_get_cap() failed %d\n", ret);

		/* Free already allocated intr */
		for (i = 0; i < actual; i++) {
			(void) ddi_intr_free(mpt->m_htable[i]);
		}

		kmem_free(mpt->m_htable, mpt->m_intr_size);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * mpt_rem_intrs:
 *
 * Unregister FIXED or MSI interrupts
 */
static void
mpt_rem_intrs(mpt_t *mpt)
{
	int	i;

	NDBG6(("mpt_rem_intrs"));

	/* Disable all interrupts */
	if (mpt->m_intr_cap & DDI_INTR_FLAG_BLOCK) {
		/* Call ddi_intr_block_disable() */
		(void) ddi_intr_block_disable(mpt->m_htable, mpt->m_intr_cnt);
	} else {
		for (i = 0; i < mpt->m_intr_cnt; i++) {
			(void) ddi_intr_disable(mpt->m_htable[i]);
		}
	}

	/* Call ddi_intr_remove_handler() */
	for (i = 0; i < mpt->m_intr_cnt; i++) {
		(void) ddi_intr_remove_handler(mpt->m_htable[i]);
		(void) ddi_intr_free(mpt->m_htable[i]);
	}

	kmem_free(mpt->m_htable, mpt->m_intr_size);
}


/*
 * mpt_get_wwid
 *
 * This function will retrieve the base WWID from the adapter.
 */

static int
mpt_get_wwid(mpt_t *mpt)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	uint_t recv_ncookie, page_ncookie;
	caddr_t recv_memp, page_memp;
	int recv_numbytes;
	config_page_manufacturing_5_t *m5;
	int recv_dmastate = 0;
	int page_dmastate = 0;
	uint32_t flagslength;
	int rval = DDI_SUCCESS;

	MPT_DISABLE_INTR(mpt);

	if (mpt_send_config_request_msg(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_PAGETYPE_MANUFACTURING, 0, 5, 0, 0, 0, 0)) {
		rval = DDI_FAILURE;
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular =
	    (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		rval = DDI_FAILURE;
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config_reply structure.");
		rval = DDI_FAILURE;
		goto done;
	}
	recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		rval = DDI_FAILURE;
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(recv_memp, sizeof (*configreply));
	configreply = (struct msg_config_reply *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	/*
	 * get config reply message
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		rval = DDI_FAILURE;
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_wwid update", 1)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular =
	    (sizeof (struct config_page_manufacturing_5));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		rval = DDI_FAILURE;
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (struct config_page_manufacturing_5)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate manufacturing page structure.");
		rval = DDI_FAILURE;
		goto done;
	}
	page_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		rval = DDI_FAILURE;
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(page_memp, sizeof (config_page_manufacturing_5_t));
	m5 = (struct config_page_manufacturing_5 *)page_memp;

	/*
	 * Give reply address to IOC to store config page in and send
	 * config request out.
	 */

	flagslength = sizeof (struct config_page_manufacturing_5);
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER | MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS | MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_IOC_TO_HOST |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    MPI_CONFIG_PAGETYPE_MANUFACTURING, 0, 5,
	    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    flagslength, page_cookie.dmac_address)) {
		rval = DDI_FAILURE;
		goto done;
	}

	/*
	 * get reply view handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		rval = DDI_FAILURE;
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_wwid config", 1)) {
		goto done;
	}

	(void) ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORCPU);

	/*
	 * Fusion-MPT stores fields in little-endian format.  This is
	 * why the low-order 32 bits are stored first.
	 */

	mpt->un.sasaddr.m_base_wwid_lo =
	    ddi_get32(page_accessp, (uint32_t *)&m5->BaseWWID);
	mpt->un.sasaddr.m_base_wwid_hi =
	    ddi_get32(page_accessp, (uint32_t *)&m5->BaseWWID + 1);

	if (ddi_prop_update_int64(DDI_DEV_T_NONE, mpt->m_dip,
	    "base-wwid", mpt->un.m_base_wwid) != DDI_PROP_SUCCESS) {
		NDBG2(("%s%d: failed to create base-wwid property",
		    ddi_driver_name(mpt->m_dip), ddi_get_instance(mpt->m_dip)));
	}

	/*
	 * LSI chipsets are numbered such that the last digit indicates
	 * the number of PHYs present.  Thus, we use that information to
	 * record the number of PHYs.
	 */

	switch (mpt->m_devid) {
	case MPT_1064:
	case MPT_1064E:
		mpt->m_num_phys = 4;
		break;
	case MPT_1068:
	case MPT_1068E:
	case MPT_1078IR:
		mpt->m_num_phys = 8;
		break;
	}

	if (ddi_prop_update_int(DDI_DEV_T_NONE, mpt->m_dip,
	    "num-phys", mpt->m_num_phys) != DDI_PROP_SUCCESS) {
		NDBG2(("%s%d: failed to create num-phys property",
		    ddi_driver_name(mpt->m_dip), ddi_get_instance(mpt->m_dip)));
	}

	mpt_log(mpt, CE_NOTE, "!mpt%d: Initiator WWNs: 0x%016llx-0x%016llx",
	    mpt->m_instance, (unsigned long long)mpt->un.m_base_wwid,
	    (unsigned long long)mpt->un.m_base_wwid + mpt->m_num_phys - 1);

	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		rval = DDI_FAILURE;
	}

done:
	/*
	 * free up memory
	 */
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);

	MPT_ENABLE_INTR(mpt);

	return (rval);
}


/*
 * The IO fault service error handling callback function
 */
/*ARGSUSED*/
static int
mpt_fm_error_cb(dev_info_t *dip, ddi_fm_error_t *err, const void *impl_data)
{
	/*
	 * as the driver can always deal with an error in any dma or
	 * access handle, we can just return the fme_status value.
	 */
	pci_ereport_post(dip, err, NULL);
	return (err->fme_status);
}

/*
 * mpt_fm_init - initialize fma capabilities and register with IO
 *               fault services.
 */
static void
mpt_fm_init(mpt_t *mpt)
{
	/*
	 * Need to change iblock to priority for new MSI intr
	 */
	ddi_iblock_cookie_t	fm_ibc;

	/* Only register with IO Fault Services if we have some capability */
	if (mpt->m_fm_capabilities) {
		/* Adjust access and dma attributes for FMA */
		mpt->m_reg_acc_attr.devacc_attr_access = DDI_FLAGERR_ACC;
		mpt->m_msg_dma_attr.dma_attr_flags |= DDI_DMA_FLAGERR;
		mpt->m_io_dma_attr.dma_attr_flags |= DDI_DMA_FLAGERR;

		/*
		 * Register capabilities with IO Fault Services.
		 * mpt->m_fm_capabilities will be updated to indicate
		 * capabilities actually supported (not requested.)
		 */
		ddi_fm_init(mpt->m_dip, &mpt->m_fm_capabilities, &fm_ibc);

		/*
		 * Initialize pci ereport capabilities if ereport
		 * capable (should always be.)
		 */
		if (DDI_FM_EREPORT_CAP(mpt->m_fm_capabilities) ||
		    DDI_FM_ERRCB_CAP(mpt->m_fm_capabilities)) {
			pci_ereport_setup(mpt->m_dip);
		}

		/*
		 * Register error callback if error callback capable.
		 */
		if (DDI_FM_ERRCB_CAP(mpt->m_fm_capabilities)) {
			ddi_fm_handler_register(mpt->m_dip,
			    mpt_fm_error_cb, (void *) mpt);
		}
	}
}

/*
 * mpt_fm_fini - Releases fma capabilities and un-registers with IO
 *               fault services.
 *
 */
static void
mpt_fm_fini(mpt_t *mpt)
{
	/* Only unregister FMA capabilities if registered */
	if (mpt->m_fm_capabilities) {

		/*
		 * Un-register error callback if error callback capable.
		 */

		if (DDI_FM_ERRCB_CAP(mpt->m_fm_capabilities)) {
			ddi_fm_handler_unregister(mpt->m_dip);
		}

		/*
		 * Release any resources allocated by pci_ereport_setup()
		 */

		if (DDI_FM_EREPORT_CAP(mpt->m_fm_capabilities) ||
		    DDI_FM_ERRCB_CAP(mpt->m_fm_capabilities)) {
			pci_ereport_teardown(mpt->m_dip);
		}

		/* Unregister from IO Fault Services */
		ddi_fm_fini(mpt->m_dip);

		/* Adjust access and dma attributes for FMA */
		mpt->m_reg_acc_attr.devacc_attr_access = DDI_DEFAULT_ACC;
		mpt->m_msg_dma_attr.dma_attr_flags &= ~DDI_DMA_FLAGERR;
		mpt->m_io_dma_attr.dma_attr_flags &= ~DDI_DMA_FLAGERR;

	}
}

int
mpt_check_acc_handle(ddi_acc_handle_t handle)
{
	ddi_fm_error_t de;

	if (handle == NULL)
		return (DDI_FAILURE);
	ddi_fm_acc_err_get(handle, &de, DDI_FME_VER0);
	return (de.fme_status);
}

int
mpt_check_dma_handle(ddi_dma_handle_t handle)
{
	ddi_fm_error_t de;

	if (handle == NULL)
		return (DDI_FAILURE);
	ddi_fm_dma_err_get(handle, &de, DDI_FME_VER0);
	return (de.fme_status);
}

void
mpt_fm_ereport(mpt_t *mpt, char *detail)
{
	uint64_t	ena;
	char		buf[FM_MAX_CLASS];

	(void) snprintf(buf, FM_MAX_CLASS, "%s.%s", DDI_FM_DEVICE, detail);
	ena = fm_ena_generate(0, FM_ENA_FMT1);
	if (DDI_FM_EREPORT_CAP(mpt->m_fm_capabilities)) {
		ddi_fm_ereport_post(mpt->m_dip, buf, ena, DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0, NULL);
	}
}

static int
mpt_send_inquiryVpd(mpt_t *mpt, int target, int lun, uchar_t page,
    unsigned char *buf, int len, int *reallen)
{
	struct scsi_pkt		*inq_pkt = NULL;
	struct buf		*inq_bp = NULL;
	struct scsi_address	ap;
	uchar_t *cdb;
	int ret = DDI_FAILURE;

	ASSERT(len <= 0xffff);

	ap.a_hba_tran = (struct scsi_hba_tran *)(mpt->m_tran);
	ap.a_target = (ushort_t)(target);
	ap.a_lun = (uchar_t)(lun);
	inq_bp = scsi_alloc_consistent_buf(&ap,
	    (struct buf *)NULL, len, B_READ, NULL_FUNC, NULL);
	if (inq_bp == NULL) {
		goto out;
	}
	inq_pkt = scsi_init_pkt(&ap, (struct scsi_pkt *)NULL,
	    inq_bp, CDB_GROUP0, sizeof (struct scsi_arq_status),
	    0, PKT_CONSISTENT, NULL, NULL);
	if (inq_pkt == NULL) {
		goto out;
	}
	cdb = inq_pkt->pkt_cdbp;
	bzero(cdb, CDB_GROUP0);
	cdb[0] = SCMD_INQUIRY;
	cdb[1] = 1;
	cdb[2] = page;
	cdb[3] = (len & 0xff00) >> 8;
	cdb[4] = (len & 0x00ff);
	cdb[5] = 0;
	inq_pkt->pkt_flags = FLAG_NOINTR|FLAG_NOPARITY;
	if (scsi_poll(inq_pkt) < 0) {
		goto out;
	}
	if (((struct scsi_status *)inq_pkt->pkt_scbp)->sts_chk) {
		goto out;
	}
	*reallen = len - inq_pkt->pkt_resid;
	if (*reallen > 0)
		bcopy((caddr_t)inq_bp->b_un.b_addr, buf, *reallen);

	ret = DDI_SUCCESS;
out:
	if (inq_pkt) {
		scsi_destroy_pkt(inq_pkt);
	}
	if (inq_bp) {
		scsi_free_consistent_buf(inq_bp);
	}
	return (ret);
}

/*
 * mpt_is_hex() is for verifying that a string is actually representing a
 * positive hexadecimal number.
 */
static int
mpt_is_hex(char *num)
{
	int index = 0;
	int len = strlen(num);

	for (index = 0; index < len; index++)
		if (!IS_HEX_DIGIT(num[index]))
			return (EINVAL);
	return (0);
}

/*
 * mpt_correct_obp_sas_disk_arg() is for handling the changes to SAS
 * SATA disk names from OBP.
 * NOTE: caller is responsible for deallocating the allocated memory
 */
static char *
mpt_correct_obp_sas_disk_arg(void *arg, size_t *size)
{
	char *corrected_arg = NULL;

	/*
	 * According to FWARC 2008/013 we can now have devices of the
	 * form:
	 *
	 *		/pci@0/pci@0/pci@2/scsi@0/disk@0
	 *
	 * from OBP. Since, the rest of Solaris is expecting an entry
	 * of the form:
	 *
	 *		/pci@0/pci@0/pci@2/scsi@0/disk@0,0
	 *
	 * we will correct the SAS/SATA device names here.
	 */

	*size = strlen((char *)arg) + 3;
	corrected_arg = kmem_zalloc(*size, KM_NOSLEEP);

	if (corrected_arg != NULL) {
		(void) strncpy(corrected_arg, arg, *size);

		/*
		 * We are only concerned with the first LUN
		 * so add that here.
		 */
		(void) strcat(corrected_arg, ",0");
		return (corrected_arg);
	} else {
		return (NULL);
	}
}

/*
 * While OBP pass down a WWID boot path, the bus_name should be
 * wWWID,lun
 */
static int
mpt_parse_id_lun(char *name, uint64_t *wwid, uint8_t *phy, int *lun)
{
	char *cp = NULL;
	char *ptr = NULL;
	size_t s = 0;
	char wwid_str[SCSI_MAXNAMELEN];
	char lun_str[SCSI_MAXNAMELEN];
	long lunnum;
	long phyid = -1;
	int rc = DDI_FAILURE;

	ptr = name;
	ASSERT(ptr[0] == 'w' || ptr[0] == 'p');
	ptr++;
	if ((cp = strchr(ptr, ',')) == NULL) {
		return (DDI_FAILURE);
	}

	s = (uintptr_t)cp - (uintptr_t)ptr;

	bcopy(ptr, wwid_str, s);
	wwid_str[s] = '\0';

	ptr = ++cp;

	if ((cp = strchr(ptr, '\0')) == NULL) {
		return (DDI_FAILURE);
	}
	s = (uintptr_t)cp - (uintptr_t)ptr;
	bcopy(ptr, lun_str, s);
	lun_str[s] = '\0';

	if (name[0] == 'p') {
		rc = ddi_strtol(wwid_str, NULL, 0x10, &phyid);
	} else {
		rc = scsi_wwnstr_to_wwn(wwid_str, wwid);
	}
	if (rc != DDI_SUCCESS)
		return (DDI_FAILURE);
	if (phyid != -1) {
		ASSERT(phyid < 8);
		*phy = (uint8_t)phyid;
	}
	rc = ddi_strtol(lun_str, NULL, 0x10, &lunnum);
	if (rc != 0)
		return (DDI_FAILURE);
	*lun = (int)lunnum;

	return (DDI_SUCCESS);
}

/*
 * mpt_parse_name() is to parse target ID and lun number
 * the name is a string which format is "target,lun"
 */
static int
mpt_parse_name(char *name, int *target, int *lun)
{
	char *cp = NULL;
	char *ptr = NULL;
	size_t s = 0;
	char t[SCSI_MAXNAMELEN];
	char d[SCSI_MAXNAMELEN];
	long tgt, lunnum;
	int rc = DDI_FAILURE;

	ptr = name;
	if ((cp = strchr(ptr, ',')) == NULL) {
		return (DDI_FAILURE);
	}
	s = (uintptr_t)cp - (uintptr_t)ptr;
	bcopy(ptr, t, s);
	t[s] = '\0';

	/*
	 * Validate that our target number is really a hexadecimal
	 * number.
	 */
	if (mpt_is_hex(t) != 0)
		return (DDI_FAILURE);

	ptr = ++cp;

	if ((cp = strchr(ptr, '\0')) == NULL) {
		return (DDI_FAILURE);
	}
	s = (uintptr_t)cp - (uintptr_t)ptr;
	bcopy(ptr, d, s);
	d[s] = '\0';

	/*
	 * Validate that our disk number is really a hexadecimal
	 * number
	 */
	if (mpt_is_hex(d) != 0)
		return (DDI_FAILURE);

	rc = ddi_strtol(t, NULL, 0x10, &tgt);
	if (rc != 0)
		return (DDI_FAILURE);
	*target = (int)tgt;

	/* Check for valid target number */
	if (*target < 0 || *target >= MPT_MAX_TARGETS)
		return (DDI_FAILURE);

	rc = ddi_strtol(d, NULL, 0x10, &lunnum);
	if (rc != 0)
		return (DDI_FAILURE);
	*lun = (int)lunnum;

	/* Check for valid LUN */
	if (*lun < 0)
		return (DDI_FAILURE);

	return (DDI_SUCCESS);
}

/*
 * mpt_parse_smp_name() is to parse sas wwn string
 * which format is "wWWN"
 */
static int
mpt_parse_smp_name(char *name, uint64_t *wwn)
{
	char *ptr = name;

	if (*ptr != 'w') {
		return (DDI_FAILURE);
	}

	ptr++;
	if (scsi_wwnstr_to_wwn(ptr, wwn)) {
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

static int
mpt_scsi_bus_config(dev_info_t *parent, uint_t flag,
    ddi_bus_config_op_t op, void *arg, dev_info_t **childp)
{
	int ret;
	int circ = 0;
	mpt_t *mpt;
	char *ptr = NULL;
	int target = 0;
	int lun = 0;

	/* get reference to soft state */
	mpt = (mpt_t *)ddi_get_soft_state(mpt_state,
	    ddi_get_instance(parent));
	if (mpt == NULL) {
		return (NDI_FAILURE);
	}

	/*
	 * Hold the nexus across the bus_config
	 */
	ndi_devi_enter(parent, &circ);

	/*
	 * If mpt_quiesce_delay is enabled:
	 * 1. Set m_bus_config_thread to current thread to
	 *    prevent other threads from starting SCSI commands.
	 * 2. Quiesce SCSI bus so that no SCSI commands are in
	 *    active slot.
	 * 3. Unquiesce SCSI bus so that only SCSI commands issued
	 *    by this thread will be started.
	 * 4. Clear m_bus_config_thread at end of this function to
	 *    allow all SCSI commands to be started.
	 */
	if (mpt->m_quiesce_delay_timebase > -1) {
		mutex_enter(&mpt->m_mutex);
		mpt->m_bus_config_thread = curthread;
		mutex_exit(&mpt->m_mutex);
		if (mpt_quiesce_bus(mpt) == DDI_SUCCESS) {
			(void) mpt_unquiesce_bus(mpt);
		}
	}

	switch (op) {
	case BUS_CONFIG_ONE:
		/* parse target name out of name given */
		if ((ptr = strchr((char *)arg, '@')) == NULL) {
			ret = NDI_FAILURE;
			break;
		}
		ptr++;
		ret = mpt_parse_name(ptr, &target, &lun);

		if (ret != DDI_SUCCESS) {
			ret = NDI_FAILURE;
			break;
		}

		/* Probing of HBA's own SCSI ID is not supported */
		if (target == mpt->m_mptid) {
			mpt_log(NULL, CE_NOTE,
			    "?mpt%d: probing of this HBA's own SCSI ID %d is "
			    "not supported\n", mpt->m_instance, mpt->m_mptid);
			ret = NDI_FAILURE;
			break;
		}

		/*
		 * If mpt_cache_probe is enabled than only allow
		 * targets that have been discovered during initial
		 * BUS_CONFIG_ALL to be reprobed.
		 */
		if ((mpt->m_cache_probe & MPT_CACHE_PROBE_ENABLED) &&
		    (mpt->m_cache_probe & MPT_CACHE_PROBE_COMPLETED) &&
		    ((mpt->m_cache_probe & (1 << target)) == 0)) {
			ret = NDI_FAILURE;
			break;
		}

		ret = mpt_scsi_config_one(mpt, target, lun, childp);
		break;
	case BUS_CONFIG_DRIVER:
	case BUS_CONFIG_ALL:
		mpt_scsi_config_all(mpt);
		ret = NDI_SUCCESS;
		break;
	}
	if (ret == NDI_SUCCESS) {
		ret = ndi_busop_bus_config(parent, flag,
		    op, arg, childp, 0);
	}

	if (mpt->m_quiesce_delay_timebase > -1) {
		mutex_enter(&mpt->m_mutex);
		mpt->m_bus_config_thread = NULL;
		mpt_restart_hba(mpt);
		mutex_exit(&mpt->m_mutex);
	}

	ndi_devi_exit(parent, circ);
	return (ret);
}

static int
mpt_scsi_probe_lun(mpt_t *mpt, uint16_t target, int lun, dev_info_t **dip)
{
	int rval = DDI_FAILURE;
	struct scsi_device *sd;
	int dtype;

	if (target >= MPT_MAX_TARGETS) {
		mpt_log(mpt, CE_WARN, "target number %d out of scope",
		    target);
		return (DDI_FAILURE);
	}

	/*
	 * If the LUN has already existed and the status is online,
	 * we just return the pointer of dev_info_t directly.
	 */

	*dip = mpt_find_child(mpt, target, lun);
	if (*dip != NULL) {
		return (DDI_SUCCESS);
	}
	sd = kmem_zalloc(scsi_device_size(), KM_SLEEP);
	sd->sd_address.a_hba_tran = mpt->m_tran;
	sd->sd_address.a_target = target;
	sd->sd_address.a_lun = (uchar_t)(lun);

	rval = scsi_hba_probe(sd, NULL);
	if ((rval != SCSIPROBE_EXISTS) || !MPT_VALID_LUN(sd)) {
		rval = DDI_FAILURE;
		goto out;
	}
	dtype = sd->sd_inq->inq_dtype;
	if ((dtype & DTYPE_MASK) == DTYPE_UNKNOWN) {
		rval = DDI_FAILURE;
		goto out;
	}

	rval = mpt_create_phys_lun(mpt, sd, NULL, NULL, NULL, dip);
out:
	if (sd->sd_inq)
		kmem_free(sd->sd_inq, SUN_INQSIZE);
	kmem_free(sd, scsi_device_size());
	return (rval);
}

static int
mpt_scsi_config_one(mpt_t *mpt, int target, int lun, dev_info_t **lundip)
{
	int rval = DDI_FAILURE;
	rval = mpt_scsi_probe_lun(mpt, target, lun, lundip);

	return (rval);
}

static void
mpt_scsi_config_all(mpt_t *mpt)
{
	int target;
	int rval = DDI_FAILURE;
	dev_info_t *lundip = NULL;
	struct scsi_device *sd;

	sd = kmem_alloc(scsi_device_size(), KM_SLEEP);
	for (target = 0; target < mpt->m_ntargets; target++) {
		/* Probing of HBA's own SCSI ID is not supported */
		if (target == mpt->m_mptid) {
			continue;
		}

		/*
		 * If mpt_cache_probe is enabled than only allow
		 * targets that have been discovered during initial
		 * BUS_CONFIG_ALL to be reprobed.
		 */
		if ((mpt->m_cache_probe & MPT_CACHE_PROBE_ENABLED) &&
		    (mpt->m_cache_probe & MPT_CACHE_PROBE_COMPLETED) &&
		    ((mpt->m_cache_probe & (1 << target)) == 0)) {
			continue;
		}

		bzero(sd, scsi_device_size());
		sd->sd_address.a_hba_tran = mpt->m_tran;
		sd->sd_address.a_target = (ushort_t)(target);
		sd->sd_address.a_lun = 0;
		rval = scsi_hba_probe(sd, NULL);
		if ((rval != SCSIPROBE_EXISTS) || !MPT_VALID_LUN(sd)) {
			if (sd->sd_inq)
				kmem_free(sd->sd_inq, SUN_INQSIZE);
			continue;
		}

		if (sd->sd_inq)
			kmem_free(sd->sd_inq, SUN_INQSIZE);

		/*
		 * If this is the first BUS_CONFIG_ALL and mpt_cache_probe
		 * is enabled than set bit position for target in
		 * m_cache_probe bit field to indicate that the specified
		 * target has been discovered.
		 */
		if ((mpt->m_cache_probe & MPT_CACHE_PROBE_ENABLED) &&
		    ((mpt->m_cache_probe & MPT_CACHE_PROBE_COMPLETED) == 0)) {
			mpt->m_cache_probe |= (1 << target);
		}

		rval = mpt_config_luns(mpt, target, 0, 0);
		if (rval != DDI_SUCCESS) {
			/*
			 * The return value means the SCMD_REPORT_LUNS
			 * did not execute successfully. The target maybe
			 * doesn't support such command.
			 */
			(void) mpt_scsi_probe_lun(mpt, target, 0, &lundip);
		}
	}
	kmem_free(sd, scsi_device_size());

	if ((mpt->m_cache_probe & MPT_CACHE_PROBE_ENABLED) &&
	    ((mpt->m_cache_probe & MPT_CACHE_PROBE_COMPLETED) == 0)) {
		mpt->m_cache_probe |= MPT_CACHE_PROBE_COMPLETED;
	}
}

static int
mpt_sas_bus_config(dev_info_t *parent, uint_t flag,
    ddi_bus_config_op_t op, void *arg, dev_info_t **childp)
{
	int ret = NDI_FAILURE;
	int circ = 0;
	int circ1 = 0;
	mpt_t *mpt;
	char *ptr = NULL;
	void *saved_arg = NULL;
	uint64_t wwid = 0;
	int target = 0;
	uint8_t phy = 0xFF;
	int lun = 0;
	uint_t mflags = flag;
	char issmp = 0;
	char ndi_config_flag = 0;
	size_t len = 0;

	/* get reference to soft state */
	mpt = (mpt_t *)ddi_get_soft_state(mpt_state,
	    ddi_get_instance(parent));
	if (mpt == NULL) {
		return (NDI_FAILURE);
	}

	/*
	 * Hold the nexus across the bus_config
	 */
	ndi_devi_enter(scsi_vhci_dip, &circ);
	ndi_devi_enter(parent, &circ1);
	switch (op) {
	case BUS_CONFIG_ONE:
		/* parse wwid/target name out of name given */
		if ((ptr = strchr((char *)arg, '@')) == NULL) {
			ret = NDI_FAILURE;
			break;
		}
		ptr++;
		if (strncmp((char *)arg, "smp", 3) == 0) {
			/*
			 * This is a SMP target device
			 */
			ret = mpt_parse_smp_name(ptr, &wwid);
			issmp = 1;
		} else if (mpt->m_wwid_obp && ((ptr[0] == 'w') ||
		    (ptr[0] == 'p'))) {
			ret = mpt_parse_id_lun(ptr, &wwid, &phy, &lun);
			if (ret != DDI_SUCCESS) {
				ret = NDI_FAILURE;
				break;
			}
			if (ptr[0] == 'w') {
				/*
				 * The device path is wWWID format and the
				 * device is not SMP target device.
				 */
				target = mpt_wwid_to_tgt(mpt, wwid);
				if (target < 0) {
					ret = NDI_FAILURE;
					break;
				}
			} else if (ptr[0] == 'p') {
				/*
				 * The device path is pPHY format
				 */
				target = mpt_phy_to_tgt(mpt, phy);
				if (target < 0) {
					ret = NDI_FAILURE;
					break;
				}
			}
		} else {
			if (strchr(ptr, ',') == NULL) {
				saved_arg = arg;
				arg = mpt_correct_obp_sas_disk_arg(arg, &len);
				if (arg != NULL) {
					/*
					 * Get the target, lun substring from
					 * the corrected arg. This way the
					 * string is ready for processing by
					 * mpt_parse_name().
					 */
					ptr = strchr((char *)arg, '@');
					ptr++;
				} else {
					arg = saved_arg;
					return (NDI_FAILURE);
				}
			}

			ret = mpt_parse_name(ptr, &target, &lun);
		}

		if (ret != DDI_SUCCESS) {
			ret = NDI_FAILURE;
			break;
		}
		if (issmp) {
			ret = mpt_sas_config_smp(mpt, wwid, childp);
			if (ret == NDI_SUCCESS)
				ndi_config_flag = 1;
		} else {
			ret = mpt_sas_config_one(mpt, target, lun, childp);
			if (ret == NDI_SUCCESS) {
				if (mpt->m_wwid_obp == 0) {
					/*
					 * non-WWID OBP, should behaviour as
					 * before.
					 */
					ndi_config_flag = 1;
				} else if (strncmp(arg, "disk", 4) == 0) {
					/*
					 * mpt driver uses node name "disk" to
					 * distinguish whether the operation
					 * BUS_CONFIG_ONE is issued by boot
					 * process on SPARC to configure OBP
					 * node. For OBP node, we need bypass
					 * ndi_busop_bus_config() since we have
					 * already configured the root device by
					 * mpt_sas_config_one().
					 */
					ndi_config_flag = 0;
					ndi_hold_devi(*childp);
				} else {
					ndi_config_flag = 1;
					mflags &= (~NDI_PROMNAME);
				}
			}
		}
		break;
	case BUS_CONFIG_DRIVER:
	case BUS_CONFIG_ALL:
		mpt_sas_config_all(mpt);
		ret = NDI_SUCCESS;
		ndi_config_flag = 1;
		break;
	}
	if (ndi_config_flag != 0) {
		ret = ndi_busop_bus_config(parent, mflags,
		    op, arg, childp, 0);
	}

	if (saved_arg != NULL) {
		kmem_free(arg, len);
		arg = saved_arg;
	}

	ndi_devi_exit(parent, circ1);
	ndi_devi_exit(scsi_vhci_dip, circ);
	return (ret);
}

/*
 * Request MPI configuration page SAS device page 0.
 * To get device_handler, device info and SAS address
 */
static int
mpt_get_sas_device_page0(mpt_t *mpt, int address, uint16_t *handle,
    uint32_t *info, uint64_t *sas_wwn, int *tgt)
{


	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_sas_device_0_t *sasdevpage;
	int recv_numbytes;
	caddr_t recv_memp, page_memp;
	int32_t rval = DDI_FAILURE;
	int recv_dmastate = 0;
	int page_dmastate = 0;
	uint8_t *sas_addr = NULL;
	uint8_t tmp_sas_wwn[SAS_WWN_BYTE_SIZE];
	int i;

	ASSERT(mutex_owned(&mpt->m_mutex));

	MPT_DISABLE_INTR(mpt);

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular = (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "unable to allocate dma handle.");
		goto cleanup;
	} else {
		recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;
	}

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "unable to allocate reply structure.");
		goto cleanup;
	} else {
		recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;
	}

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto cleanup;
	} else {
		recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	}

	bzero(recv_memp, sizeof (*configreply));
	configreply = (struct msg_config_reply *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	if (mpt_send_extended_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
	    address, 0, 0, 0, 0, 0)) {
		mpt_log(mpt, CE_WARN, "send message failed.");
		goto cleanup;
	}

	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		mpt_log(mpt, CE_WARN, "handshake failed.");
		goto cleanup;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_sas_device_page0 header", 0)) {
		goto cleanup;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular =
	    (sizeof (struct config_page_sas_device_0));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "(unable to allocate dma handle.");
		goto cleanup;
	} else {
		page_dmastate |= MPT_DMA_HANDLE_ALLOCD;
	}

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (struct config_page_sas_device_0)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto cleanup;
	} else {
		page_dmastate |= MPT_DMA_MEMORY_ALLOCD;
	}

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto cleanup;
	} else {
		page_dmastate |= MPT_DMA_HANDLE_BOUND;
	}

	bzero(page_memp, sizeof (config_page_sas_device_0_t));
	sasdevpage = (struct config_page_sas_device_0 *)page_memp;

	if (mpt_send_extended_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE, address, 0,
	    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get16(recv_accessp, &configreply->ExtPageLength),
	    sizeof (struct config_page_sas_device_0),
	    page_cookie.dmac_address)) {
		mpt_log(mpt, CE_WARN, "send message failed.");
		goto cleanup;
	}

	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		mpt_log(mpt, CE_WARN, "handshake failed.");
		goto cleanup;
	}

	if ((ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORCPU))
	    != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "dma sync failure.");
		goto cleanup;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_sas_device_page0 config", 0)
	    == MPI_IOCSTATUS_CONFIG_INVALID_PAGE) {
		goto cleanup;
	}

	*info = ddi_get32(page_accessp, &sasdevpage->DeviceInfo);
	*handle = ddi_get16(page_accessp, &sasdevpage->DevHandle);
	*tgt = (int)BUSTARG_TO_BT(
	    ddi_get8(page_accessp, &sasdevpage->Bus),
	    ddi_get8(page_accessp, &sasdevpage->TargetID));
	sas_addr = (uint8_t *)(&sasdevpage->SASAddress);

	for (i = 0; i < SAS_WWN_BYTE_SIZE; i++) {
		tmp_sas_wwn[i] = ddi_get8(page_accessp, sas_addr + i);
	}
	bcopy(tmp_sas_wwn, sas_wwn, SAS_WWN_BYTE_SIZE);
	*sas_wwn = LE_64(*sas_wwn);
	/*
	 * Direct attached SATA device, SASAddress value is invalid
	 * we set 0 to sas_wwn as default value.
	 */
	if ((*info & MPI_SAS_DEVICE_INFO_DIRECT_ATTACH) &&
	    (*info & (MPI_SAS_DEVICE_INFO_SATA_DEVICE |
	    MPI_SAS_DEVICE_INFO_ATAPI_DEVICE))) {
		*sas_wwn = 0;
	}
	if (*tgt >= MPT_MAX_TARGETS) {
		mpt_log(mpt, CE_WARN, "target number %d out of scope",
		    *tgt);
		goto cleanup;
	}
	if (*info & (MPI_SAS_DEVICE_INFO_SSP_TARGET |
	    MPI_SAS_DEVICE_INFO_SATA_DEVICE |
	    MPI_SAS_DEVICE_INFO_ATAPI_DEVICE)) {
		mpt->m_active->m_target[(int)*tgt].m_deviceinfo = *info;
		mpt->m_active->m_target[(int)*tgt].m_sas_wwn = *sas_wwn;
		mpt->m_active->m_target[(int)*tgt].m_phynum = ddi_get8(
		    page_accessp, &sasdevpage->PhyNum);
	}

	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		goto cleanup;
	}
	rval = DDI_SUCCESS;
cleanup:
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);

	MPT_ENABLE_INTR(mpt);
	return (rval);
}

/*
 * Request MPI configuration page SAS device page 0.
 * To get device_handler, device info and SAS address
 */
static int
mpt_get_sas_expander_page0(mpt_t *mpt, int address, uint16_t *handle,
    uint64_t *sas_wwn)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_sas_expander_0_t *expddevpage;
	int recv_numbytes;
	caddr_t recv_memp, page_memp;
	int32_t rval = DDI_FAILURE;
	int recv_dmastate = 0;
	int page_dmastate = 0;
	uint8_t *sas_addr = NULL;
	uint8_t tmp_sas_wwn[SAS_WWN_BYTE_SIZE];
	int i;

	ASSERT(mutex_owned(&mpt->m_mutex));

	MPT_DISABLE_INTR(mpt);

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt_dma_attrs;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular = (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "unable to allocate dma handle.");
		goto cleanup;
	} else {
		recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;
	}

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &dev_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "unable to allocate reply structure.");
		goto cleanup;
	} else {
		recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;
	}

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto cleanup;
	} else {
		recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	}

	bzero(recv_memp, sizeof (*configreply));
	configreply = (struct msg_config_reply *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	if (mpt_send_extended_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER,
	    address, 0, 0, 0, 0, 0)) {
		mpt_log(mpt, CE_WARN, "send message failed.");
		goto cleanup;
	}

	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		mpt_log(mpt, CE_WARN, "handshake failed.");
		goto cleanup;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt_dma_attrs;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular =
	    (sizeof (struct config_page_sas_expander_0));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "(unable to allocate dma handle.");
		goto cleanup;
	} else {
		page_dmastate |= MPT_DMA_HANDLE_ALLOCD;
	}

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (struct config_page_sas_expander_0)),
	    &dev_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto cleanup;
	} else {
		page_dmastate |= MPT_DMA_MEMORY_ALLOCD;
	}

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto cleanup;
	} else {
		page_dmastate |= MPT_DMA_HANDLE_BOUND;
	}

	bzero(page_memp, sizeof (config_page_sas_expander_0_t));
	expddevpage = (struct config_page_sas_expander_0 *)page_memp;

	if (mpt_send_extended_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER, address, 0,
	    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get16(recv_accessp, &configreply->ExtPageLength),
	    sizeof (struct config_page_sas_expander_0),
	    page_cookie.dmac_address)) {
		mpt_log(mpt, CE_WARN, "send message failed.");
		goto cleanup;
	}

	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		mpt_log(mpt, CE_WARN, "handshake failed.");
		goto cleanup;
	}

	if ((ddi_dma_sync(page_dma_handle, 0, 0, DDI_DMA_SYNC_FORCPU))
	    != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "dma sync failure.");
		goto cleanup;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_sas_expander_page0", 0)
	    == MPI_IOCSTATUS_CONFIG_INVALID_PAGE) {
		goto cleanup;
	}

	*handle = ddi_get16(page_accessp, &expddevpage->DevHandle);

	sas_addr = (uint8_t *)(&expddevpage->SASAddress);
	for (i = 0; i < SAS_WWN_BYTE_SIZE; i++) {
		tmp_sas_wwn[i] = ddi_get8(page_accessp, sas_addr + i);
	}

	bcopy(tmp_sas_wwn, sas_wwn, SAS_WWN_BYTE_SIZE);
	*sas_wwn = LE_64(*sas_wwn);

	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		goto cleanup;
	}
	rval = DDI_SUCCESS;
cleanup:
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);

	MPT_ENABLE_INTR(mpt);
	return (rval);
}

static int
mpt_sas_config_raid(mpt_t *mpt, uint16_t target, uint64_t wwn, dev_info_t **dip)
{
	int rval = DDI_FAILURE;
	struct scsi_device *sd;

	/*
	 * If the LUN has already existed and the status is online,
	 * we just return the pointer of dev_info_t directly.
	 */

	*dip = mpt_find_child(mpt, target, 0);
	if (*dip != NULL) {
		return (DDI_SUCCESS);
	}
	sd = kmem_zalloc(scsi_device_size(), KM_SLEEP);
	sd->sd_address.a_hba_tran = mpt->m_tran;
	sd->sd_address.a_target = target;
	sd->sd_address.a_lun = 0;

	rval = scsi_hba_probe(sd, NULL);
	if ((rval != SCSIPROBE_EXISTS) || !MPT_VALID_LUN(sd)) {
		rval = DDI_FAILURE;
		goto out;
	}

	rval = mpt_create_phys_lun(mpt, sd, NULL, NULL, wwn, dip);
out:
	if (sd->sd_inq)
		kmem_free(sd->sd_inq, SUN_INQSIZE);
	kmem_free(sd, scsi_device_size());
	return (rval);
}

static int
mpt_sas_probe_lun(mpt_t *mpt, uint16_t target, int lun, uint32_t dev_info,
    dev_info_t **dip, uint64_t sas_wwn)
{
	int rval = DDI_FAILURE;
	struct scsi_device *sd;
	char *old_wwn = NULL;
	char new_wwn[MPT_SAS_WWN_STRLEN];


	if (target >= MPT_MAX_TARGETS) {
		mpt_log(mpt, CE_WARN, "target number %d out of scope",
		    target);
		return (DDI_FAILURE);
	}

	/*
	 * If the LUN already exists and the status is online,
	 * we just return the pointer to dev_info_t directly.
	 * For the mdi_pathinfo node, we'll handle it in mpt_create_virt_lun()
	 */

	*dip = mpt_find_child(mpt, target, lun);
	/*
	 * At the case of replace disk, it's possible (?) that the target and
	 * lun are the same, but wwid different.
	 * We should reconfigure this disk in this case
	 */
	if (*dip != NULL) {
		(void) ddi_prop_lookup_string(DDI_DEV_T_ANY, *dip,
		    DDI_PROP_DONTPASS, SCSI_ADDR_PROP_TARGET_PORT, &old_wwn);

		if (sas_wwn && (old_wwn != NULL)) {
			(void) sprintf(new_wwn, "%016"PRIx64, sas_wwn);
			if (strcmp(old_wwn, new_wwn) == 0) {
				ddi_prop_free(old_wwn);
				return (DDI_SUCCESS);
			}
		} else if ((sas_wwn == 0) && (old_wwn == NULL)) {
			return (DDI_SUCCESS);
		}
		if (old_wwn != NULL)
			ddi_prop_free(old_wwn);
	}

	sd = kmem_zalloc(scsi_device_size(), KM_SLEEP);
	sd->sd_address.a_hba_tran = mpt->m_tran;
	sd->sd_address.a_target = target;
	sd->sd_address.a_lun = (uchar_t)(lun);

	rval = scsi_hba_probe(sd, NULL);

	if ((rval == SCSIPROBE_EXISTS) && MPT_VALID_LUN(sd)) {
		rval = mpt_sas_create_lun(mpt, sd, sas_wwn, dev_info, dip);
	} else {
		rval = DDI_FAILURE;
	}
out:
	if (sd->sd_inq)
		kmem_free(sd->sd_inq, SUN_INQSIZE);
	kmem_free(sd, scsi_device_size());
	return (rval);
}


static int
mpt_sas_config_one(mpt_t *mpt, int target, int lun, dev_info_t **lundip)
{
	int address;
	uint16_t dev_handle = 0;
	uint32_t dev_info = 0;
	int rval;
	int tgt;
	int vol;
	uint64_t sas_wwn = 0;

	if (target >= MPT_MAX_TARGETS) {
		mpt_log(mpt, CE_WARN, "target number %d out of scope",
		    target);
		return (DDI_FAILURE);
	}
	mutex_enter(&mpt->m_mutex);
	/*
	 * Check if the LUN is a RAID volume.
	 */
	for (vol = 0; vol < MPT_MAX_RAIDVOLS; vol++) {
		if (MPT_RAID_EXISTS(mpt, vol) &&
		    TGT_IS_RAID(mpt, vol, target)) {
			sas_wwn = mpt->m_active->m_raidvol[vol].m_raidwwid;
			mutex_exit(&mpt->m_mutex);
			rval = mpt_sas_config_raid(mpt, target, sas_wwn,
			    lundip);
			return (rval);
		}
	}
	if (!mpt->m_active->m_target[target].m_deviceinfo) {
		/*
		 * Get sas device page 0 by BusTargetID to make sure if
		 * SSP/SATA end device exist.
		 */
		address = (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		    MPI_SAS_DEVICE_PGAD_FORM_SHIFT) | target;
		rval = mpt_get_sas_device_page0(mpt, address, &dev_handle,
		    &dev_info, &sas_wwn, &tgt);
		if (rval != DDI_SUCCESS) {
			mutex_exit(&mpt->m_mutex);
			return (rval);
		}
		if ((dev_info & (MPI_SAS_DEVICE_INFO_SSP_TARGET |
		    MPI_SAS_DEVICE_INFO_SATA_DEVICE |
		    MPI_SAS_DEVICE_INFO_ATAPI_DEVICE)) == 0) {
			mutex_exit(&mpt->m_mutex);
			return (DDI_FAILURE);
		}
		ASSERT(tgt == target);

		if (sas_wwn == 0) {
			/*
			 * Direct Attached SATA device sas_wwn == 0
			 * then we assign device name of SATA device
			 * to sas_wwn
			 */
			mutex_exit(&mpt->m_mutex);
			(void) mpt_get_sata_device_name(mpt, target);
			mutex_enter(&mpt->m_mutex);
		}
	} else {
		dev_info = mpt->m_active->m_target[target].m_deviceinfo;
	}
	sas_wwn = mpt->m_active->m_target[target].m_sas_wwn;
probe:
	mutex_exit(&mpt->m_mutex);
	rval = mpt_sas_probe_lun(mpt, target, lun, dev_info, lundip, sas_wwn);

	return (rval);
}

static int
mpt_retrieve_lundata(int lun_cnt, uint8_t *buf, uint16_t *lun_num,
    uint8_t *lun_addr_type)
{
	uint32_t lun_idx = 0;

	ASSERT(lun_num != NULL);
	ASSERT(lun_addr_type != NULL);

	lun_idx = (lun_cnt + 1) * MPT_SCSI_REPORTLUNS_ADDRESS_SIZE;
	/* determine report luns addressing type */
	switch (buf[lun_idx] & MPT_SCSI_REPORTLUNS_ADDRESS_MASK) {
		/*
		 * Vendors in the field have been found to be concatenating
		 * bus/target/lun to equal the complete lun value instead
		 * of switching to flat space addressing
		 */
		/* 00b - peripheral device addressing method */
	case MPT_SCSI_REPORTLUNS_ADDRESS_PERIPHERAL:
		/* FALLTHRU */
		/* 10b - logical unit addressing method */
	case MPT_SCSI_REPORTLUNS_ADDRESS_LOGICAL_UNIT:
		/* FALLTHRU */
		/* 01b - flat space addressing method */
	case MPT_SCSI_REPORTLUNS_ADDRESS_FLAT_SPACE:
		/* byte0 bit0-5=msb lun byte1 bit0-7=lsb lun */
		*lun_addr_type = (buf[lun_idx] &
		    MPT_SCSI_REPORTLUNS_ADDRESS_MASK) >> 6;
		*lun_num = (buf[lun_idx] & 0x3F) << 8;
		*lun_num |= buf[lun_idx + 1];
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

static int
mpt_config_luns(mpt_t *mpt, int target, int dev_info, uint64_t sas_wwn)
{
	struct scsi_pkt		*repluns_pkt = NULL;
	struct buf		*repluns_bp = NULL;
	struct scsi_address	ap;
	uchar_t *cdb;
	int ret = DDI_FAILURE;
	int retry = 0;
	int lun_list_len = 0;
	uint16_t lun_num = 0;
	uint8_t	lun_addr_type = 0;
	uint32_t lun_cnt = 0;
	uint32_t lun_total = 0;
	dev_info_t *cdip = NULL;
	uint16_t *saved_repluns = NULL;
	char *buffer = NULL;
	int buf_len = sizeof (struct scsi_inquiry);

	do {
		ap.a_hba_tran = mpt->m_tran;
		ap.a_target = (ushort_t)(target);
		ap.a_lun = 0;
		repluns_bp = scsi_alloc_consistent_buf(&ap,
		    (struct buf *)NULL, buf_len, B_READ, NULL_FUNC, NULL);
		if (repluns_bp == NULL) {
			retry++;
			continue;
		}
		repluns_pkt = scsi_init_pkt(&ap, (struct scsi_pkt *)NULL,
		    repluns_bp, CDB_GROUP5, sizeof (struct scsi_arq_status),
		    0, PKT_CONSISTENT, NULL, NULL);
		if (repluns_pkt == NULL) {
			scsi_free_consistent_buf(repluns_bp);
			retry++;
			continue;
		}
		cdb = repluns_pkt->pkt_cdbp;
		bzero(cdb, CDB_GROUP5);
		cdb[0] = SCMD_REPORT_LUNS;
		cdb[6] = (buf_len & 0xff000000) >> 24;
		cdb[7] = (buf_len & 0x00ff0000) >> 16;
		cdb[8] = (buf_len & 0x0000ff00) >> 8;
		cdb[9] = (buf_len & 0x000000ff);

		repluns_pkt->pkt_flags = FLAG_NOINTR|FLAG_NOPARITY;

		if (scsi_poll(repluns_pkt) < 0) {
			scsi_destroy_pkt(repluns_pkt);
			scsi_free_consistent_buf(repluns_bp);
			retry++;
			continue;
		}
		if (((struct scsi_status *)repluns_pkt->pkt_scbp)->sts_chk) {
			scsi_destroy_pkt(repluns_pkt);
			scsi_free_consistent_buf(repluns_bp);
			break;
		}
		lun_list_len = BE_32(*(int *)((void *)(
		    repluns_bp->b_un.b_addr)));
		if (buf_len >= lun_list_len + 8) {
			ret = DDI_SUCCESS;
			break;
		}
		scsi_destroy_pkt(repluns_pkt);
		scsi_free_consistent_buf(repluns_bp);
		buf_len = lun_list_len + 8;

	} while (retry < 3);

	if (ret != DDI_SUCCESS)
		return (ret);
	buffer = (char *)repluns_bp->b_un.b_addr;
	/*
	 * find out the number of luns returned by the SCSI ReportLun call
	 * and allocate buffer space
	 */
	lun_total = lun_list_len / MPT_SCSI_REPORTLUNS_ADDRESS_SIZE;
	saved_repluns = kmem_zalloc(sizeof (uint16_t) * lun_total, KM_SLEEP);
	if (saved_repluns == NULL) {
		scsi_destroy_pkt(repluns_pkt);
		scsi_free_consistent_buf(repluns_bp);
		return (DDI_FAILURE);
	}
	for (lun_cnt = 0; lun_cnt < lun_total; lun_cnt++) {
		if (mpt_retrieve_lundata(lun_cnt, (uint8_t *)(buffer), &lun_num,
		    &lun_addr_type) != DDI_SUCCESS) {
			continue;
		}
		saved_repluns[lun_cnt] = lun_num;
		if (MPT_IS_SCSI(mpt)) {
			(void) mpt_scsi_probe_lun(mpt, target, lun_num, &cdip);
		} else {
			ret = mpt_sas_probe_lun(mpt, target, lun_num, dev_info,
			    &cdip, sas_wwn);
			if ((ret == DDI_SUCCESS) && (cdip != NULL)) {
				(void) ndi_prop_remove(DDI_DEV_T_NONE, cdip,
				    MPT_DEV_GONE);
			}
		}
	}
	mpt_handle_missed_luns(mpt, target, saved_repluns, lun_total);
	kmem_free(saved_repluns, sizeof (uint16_t) * lun_total);
	scsi_destroy_pkt(repluns_pkt);
	scsi_free_consistent_buf(repluns_bp);
	return (DDI_SUCCESS);
}

static void
mpt_handle_missed_luns(mpt_t *mpt, int target, uint16_t *repluns, int lun_cnt)
{
	dev_info_t *child;
	mdi_pathinfo_t *pip = NULL;
	int tgt, lun;
	int i;
	int find;
	char *addr;
	char *nodename;
	child = ddi_get_child(mpt->m_dip);
	while (child) {
		find = 0;
		tgt = lun = 0;
		nodename = ddi_node_name(child);
		if (strcmp(nodename, "smp") == 0) {
			child = ddi_get_next_sibling(child);
			continue;
		}
		/*
		 * The hotplug handling routine should skip .conf nodes. It
		 * only takes care for persistent nodes.
		 */
		if (ndi_dev_is_persistent_node(child) == 0) {
			child = ddi_get_next_sibling(child);
			continue;
		}
		addr = ddi_get_name_addr(child);
		if (addr == NULL) {
			child = ddi_get_next_sibling(child);
			continue;
		}
		if (mpt_parse_name(addr, &tgt, &lun) != DDI_SUCCESS) {
			child = ddi_get_next_sibling(child);
			continue;
		}

		child = ddi_get_next_sibling(child);
		if (tgt == target) {
			for (i = 0; i < lun_cnt; i++) {
				if (repluns[i] == lun) {
					find = 1;
					break;
				}
			}
		} else {
			continue;
		}

		if (find == 0) {
			/*
			 * The lun has not been there already
			 */
			(void) mpt_sas_unconfig_one(mpt, target, lun,
			    NDI_DEVI_REMOVE);
		}

	}
	if (MPT_IS_SCSI(mpt))
		return;
	tgt = lun = 0;
	pip = mdi_get_next_client_path(mpt->m_dip, NULL);
	while (pip) {
		find = 0;
		if (MDI_PI(pip)->pi_addr == NULL) {
			pip = mdi_get_next_client_path(mpt->m_dip, pip);
			continue;
		}

		if (mpt_parse_name(MDI_PI(pip)->pi_addr, &tgt, &lun) !=
		    DDI_SUCCESS) {
			pip = mdi_get_next_client_path(mpt->m_dip, pip);
			continue;
		}

		pip = mdi_get_next_client_path(mpt->m_dip, pip);
		if (tgt == target) {
			for (i = 0; i < lun_cnt; i++) {
				if (repluns[i] == lun) {
					find = 1;
					break;
				}
			}
		} else {
			continue;
		}
		if (find == 0) {
			/*
			 * The lun has not been there already
			 */
			(void) mpt_sas_unconfig_one(mpt, target, lun,
			    NDI_DEVI_REMOVE);
		}
	}
}

static void
mpt_sas_config_all(mpt_t *mpt)
{
	int address;
	uint16_t dev_handle = 0xFFFF;
	uint32_t dev_info;
	int target;
	dev_info_t *lundip = NULL;
	dev_info_t *smpdip = NULL;
	int vol;
	uint64_t sas_wwn = 0;
	int rval = DDI_FAILURE;

	mutex_enter(&mpt->m_mutex);

	for (; ; ) {
		smpdip = NULL;
		address = (MPI_SAS_EXPAND_PGAD_FORM_GET_NEXT_HANDLE <<
		    MPI_SAS_EXPAND_PGAD_FORM_SHIFT) | dev_handle;
		rval = mpt_get_sas_expander_page0(mpt, address,
		    &dev_handle, &sas_wwn);
		if (rval != DDI_SUCCESS)
			break;
		mutex_exit(&mpt->m_mutex);
		if (mpt_sas_config_smp(mpt, sas_wwn, &smpdip) != DDI_SUCCESS) {
			mpt_log(mpt, CE_WARN, "!failed to configure smp "
			    "w%"PRIx64, sas_wwn);
		}
		mutex_enter(&mpt->m_mutex);
	}
	dev_handle = mpt->m_dev_handle;
	sas_wwn = 0;
	/*
	 * Firstly, we need to config RAID volumes if RAID exist.
	 */
	for (vol = 0; vol < MPT_MAX_RAIDVOLS; vol++) {
		if (MPT_RAID_EXISTS(mpt, vol)) {
			target = mpt->m_active->m_raidvol[vol].m_raidtarg;
			sas_wwn = mpt->m_active->m_raidvol[vol].m_raidwwid;
			mutex_exit(&mpt->m_mutex);
			lundip = NULL;
			(void) mpt_sas_config_raid(mpt, target, sas_wwn,
			    &lundip);
			mutex_enter(&mpt->m_mutex);
		}
	}

	if (!mpt->m_done_bus_config_all) {
		mpt->m_done_bus_config_all = 1;
		/*
		 * Do loop to get sas device page 0 by GetNextHandle till the
		 * the last handle. If the sas device is a SATA/SSP target,
		 * we try to config it.
		 */
		for (; ; ) {
			lundip = NULL;
			address = (MPI_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE <<
			    MPI_SAS_DEVICE_PGAD_FORM_SHIFT) | dev_handle;
			rval = mpt_get_sas_device_page0(mpt, address,
			    &dev_handle, &dev_info, &sas_wwn, &target);
			if (rval != DDI_SUCCESS)
				break;
			if (sas_wwn != 0)
				continue;
			/*
			 * Direct Attached SATA device get to there
			 * adjust the sas_wwn to Device Name if exists
			 */
			mutex_exit(&mpt->m_mutex);
			(void) mpt_get_sata_device_name(mpt, target);
			mutex_enter(&mpt->m_mutex);
		}
	}

	for (target = 0; target < MPT_MAX_TARGETS; target++) {
		dev_info = mpt->m_active->m_target[target].m_deviceinfo;
		sas_wwn = mpt->m_active->m_target[target].m_sas_wwn;
		if (dev_info & (MPI_SAS_DEVICE_INFO_SSP_TARGET |
		    MPI_SAS_DEVICE_INFO_SATA_DEVICE |
		    MPI_SAS_DEVICE_INFO_ATAPI_DEVICE)) {
			mutex_exit(&mpt->m_mutex);
			mpt_sas_config_target(mpt, target, dev_info, sas_wwn);
			mutex_enter(&mpt->m_mutex);
		}
	}
	mutex_exit(&mpt->m_mutex);
}

static void
mpt_sas_config_target(mpt_t *mpt, int target, int dev_info, uint64_t sas_wwn)
{
	int rval = DDI_FAILURE;
	dev_info_t *tdip;

	rval = mpt_config_luns(mpt, target, dev_info, sas_wwn);
	if (rval != DDI_SUCCESS) {
		/*
		 * The return value means the SCMD_REPORT_LUNS
		 * did not execute successfully. The target maybe
		 * doesn't support such command.
		 */
		(void) mpt_sas_probe_lun(mpt, target, 0, dev_info, &tdip,
		    sas_wwn);
	}
}

static int
mpt_sas_offline_target(mpt_t *mpt, int target)
{
	struct scsi_address ap;
	dev_info_t *child, *prechild;
	mdi_pathinfo_t *pip = NULL;
	int tgt, lun, rval = DDI_SUCCESS;
	char *addr;

	ap.a_hba_tran = mpt->m_tran;
	ap.a_target = (ushort_t)target;
	ap.a_lun = 0;
	mutex_enter(&mpt->m_mutex);
	(void) mpt_do_scsi_reset(&ap, RESET_TARGET);
	mutex_exit(&mpt->m_mutex);

	child = ddi_get_child(mpt->m_dip);
	while (child) {
		addr = ddi_get_name_addr(child);
		prechild = child;
		child = ddi_get_next_sibling(child);
		if ((addr == NULL) ||
		    (ndi_dev_is_persistent_node(prechild) == 0)) {
			continue;
		}

		tgt = lun = 0;
		if (mpt_parse_name(addr, &tgt, &lun) != DDI_SUCCESS)
			continue;
		if (tgt == target) {
			rval = mpt_sas_unconfig_one(mpt, target, lun,
			    NDI_DEVI_REMOVE);
			if (rval != DDI_SUCCESS) {
				if (ndi_prop_create_boolean(DDI_DEV_T_NONE,
				    prechild, MPT_DEV_GONE) !=
				    DDI_PROP_SUCCESS) {
					mpt_log(mpt, CE_WARN, "mpt driver "
					    "unable to create property for "
					    "SAS target %d lun %d "
					    "(MPT_DEV_GONE)", target, lun);
				}
			}
		}
	}


	pip = mdi_get_next_client_path(mpt->m_dip, NULL);
	while (pip) {
		if (MDI_PI(pip)->pi_addr == NULL) {
			pip = mdi_get_next_client_path(mpt->m_dip, pip);
			continue;
		}
		tgt = lun = 0;
		if (mpt_parse_name(MDI_PI(pip)->pi_addr, &tgt, &lun) !=
		    DDI_SUCCESS) {
			pip = mdi_get_next_client_path(mpt->m_dip, pip);
			continue;
		}
		pip = mdi_get_next_client_path(mpt->m_dip, pip);
		if (tgt == target) {
			rval = mpt_sas_unconfig_one(mpt, target, lun,
			    NDI_DEVI_REMOVE);
		}
	}
	return (rval);
}

static int
mpt_sas_unconfig_one(mpt_t *mpt, int target, int lun, uint_t flags)
{
	int rval = DDI_FAILURE;
	char *devname;
	dev_info_t *cdip, *pdip;
	mdi_pathinfo_t *pip = NULL;

	pip = mpt_find_path(mpt, target, lun);
	if (pip != NULL) {
		cdip = mdi_pi_get_client(pip);
	} else {
		cdip = mpt_find_child(mpt, target, lun);
	}
	if (cdip == NULL)
		return (DDI_SUCCESS);
	/*
	 * Make sure node is attached otherwise
	 * it won't have related cache nodes to
	 * clean up.  i_ddi_devi_attached is
	 * similiar to i_ddi_node_state(cdip) >=
	 * DS_ATTACHED.
	 */
	if (i_ddi_devi_attached(cdip)) {

		/* Get parent dip */
		pdip = ddi_get_parent(cdip);

		/* Get full devname */
		devname = kmem_alloc(MAXNAMELEN + 1, KM_SLEEP);
		(void) ddi_deviname(cdip, devname);
		/* Clean cache */
		(void) devfs_clean(pdip, devname + 1,
		    DV_CLEAN_FORCE);
		kmem_free(devname, MAXNAMELEN + 1);
	}
	if (pip != NULL) {
		if (MDI_PI_IS_OFFLINE(pip)) {
			rval = DDI_SUCCESS;
		} else {
			rval = mdi_pi_offline(pip, flags);
		}
	} else {
		rval = ndi_devi_offline(cdip, flags);
	}

	return (rval);
}

static dev_info_t *
mpt_find_smp_child(mpt_t *mpt, char *str_wwn)
{
	dev_info_t *child;
	char *addr;
	char name[SCSI_MAXNAMELEN];
	bzero(name, sizeof (name));

	(void) sprintf(name, "w%s", str_wwn);
	child = ddi_get_child(mpt->m_dip);
	while (child) {
		addr = ddi_get_name_addr(child);
		if (addr == NULL)
			addr = DEVI(child)->devi_addr_buf;
		if (addr == NULL) {
			child = ddi_get_next_sibling(child);
			continue;
		}
		if (strcmp(addr, name) == 0)
			break;
		child = ddi_get_next_sibling(child);
	}
	return (child);
}

static int
mpt_sas_offline_smp(mpt_t *mpt, uint64_t wwn, uint_t flags)
{
	int rval = DDI_FAILURE;
	char *devname;
	char wwn_str[MPT_SAS_WWN_STRLEN];
	dev_info_t *cdip, *pdip;

	(void) sprintf(wwn_str, "%"PRIx64, wwn);

	cdip = mpt_find_smp_child(mpt, wwn_str);

	if (cdip == NULL)
		return (DDI_SUCCESS);

	/*
	 * Make sure node is attached otherwise
	 * it won't have related cache nodes to
	 * clean up.  i_ddi_devi_attached is
	 * similiar to i_ddi_node_state(cdip) >=
	 * DS_ATTACHED.
	 */
	if (i_ddi_devi_attached(cdip)) {

		/* Get parent dip */
		pdip = ddi_get_parent(cdip);

		/* Get full devname */
		devname = kmem_alloc(MAXNAMELEN + 1, KM_SLEEP);
		(void) ddi_deviname(cdip, devname);
		/* Clean cache */
		(void) devfs_clean(pdip, devname + 1,
		    DV_CLEAN_FORCE);
		kmem_free(devname, MAXNAMELEN + 1);
	}

	rval = ndi_devi_offline(cdip, flags);

	return (rval);
}

static dev_info_t *
mpt_find_child(mpt_t *mpt, uint16_t target, int lun)
{
	dev_info_t *child;
	char name[SCSI_MAXNAMELEN];
	char addr[MAXNAMELEN];
	bzero(name, sizeof (name));

	(void) sprintf(name, "%x,%x", (int)target, lun);
	child = ddi_get_child(mpt->m_dip);
	while (child) {
		if (ndi_dev_is_persistent_node(child) == 0) {
			child = ddi_get_next_sibling(child);
			continue;
		}
		if (mpt_name_child(child, addr, MAXNAMELEN) != DDI_SUCCESS) {
			child = ddi_get_next_sibling(child);
			continue;
		}
		if (strcmp(addr, name) == 0)
			break;
		child = ddi_get_next_sibling(child);
	}
	return (child);
}

static mdi_pathinfo_t *
mpt_find_path(mpt_t *mpt, uint16_t target, int lun)
{
	mdi_pathinfo_t *path;
	char name[SCSI_MAXNAMELEN];
	bzero(name, sizeof (name));

	(void) sprintf(name, "%x,%x", (int)target, lun);
	path = mdi_pi_find(mpt->m_dip, NULL, name);
	return (path);
}


static int
mpt_sas_create_lun(mpt_t *mpt, struct scsi_device *sd, uint64_t sas_wwn,
    uint32_t devinfo, dev_info_t **lun_dip)
{
	int i = 0;
	uchar_t *inq83 = NULL;
	int inq83_len1 = 0xFF;
	int inq83_len = 0;
	int rval = DDI_FAILURE;
	ddi_devid_t devid;
	char *guid = NULL;
	int target = sd->sd_address.a_target;
	int lun = sd->sd_address.a_lun;
	struct scsi_inquiry *inq = sd->sd_inq;
	mdi_pathinfo_t	*pip = NULL;

	/*
	 * For DVD/CD ROM and tape devices and optical
	 * devices, we won't try to enumerate them under
	 * scsi_vhci, so no need to try page83
	 */
	if (inq && (inq->inq_dtype == DTYPE_RODIRECT ||
	    inq->inq_dtype == DTYPE_OPTICAL ||
	    inq->inq_dtype == DTYPE_ESI))
		goto create_lun;

	/*
	 * The LCA returns good SCSI status, but corrupt page 83 data the first
	 * time it is queried. The solution is to keep trying to request page83
	 * and verify the GUID is not (DDI_NOT_WELL_FORMED) in
	 * mpt_inq83_retry_timeout seconds. If the timeout expires, driver give
	 * up to get VPD page at this stage and fail the enumeration.
	 */

	inq83	= kmem_zalloc(inq83_len1, KM_SLEEP);

	for (i = 0; i < mpt_inq83_retry_timeout; i++) {
		rval = mpt_send_inquiryVpd(mpt, target, lun, 0x83, inq83,
		    inq83_len1, &inq83_len);
		if (rval != 0) {
			NDBG2(("mpt request inquiry page "
			    "0x83 for dtype=%x target:%x, lun:%x failed!",
			    inq->inq_dtype, target, lun));
			if (mpt_physical_bind_failed_page_83 != B_FALSE)
				goto create_lun;
			goto out;
		}
		/*
		 * create DEVID from inquiry data
		 */
		if ((rval = ddi_devid_scsi_encode(
		    DEVID_SCSI_ENCODE_VERSION_LATEST, NULL, (uchar_t *)inq,
		    sizeof (struct scsi_inquiry), NULL, 0, inq83,
		    (size_t)inq83_len, &devid)) == DDI_SUCCESS) {
			/*
			 * extract GUID from DEVID
			 */
			guid = ddi_devid_to_guid(devid);
			/*
			 * Do not enable MPXIO for ASCII based GUIDs
			 */
			if ((DEVID_GETTYPE((impl_devid_t *)devid) ==
			    DEVID_SCSI3_VPD_T10) && (devinfo &
			    MPI_SAS_DEVICE_INFO_SATA_DEVICE)) {
				ddi_devid_free_guid(guid);
				guid = NULL;
			}

			/*
			 * devid no longer needed
			 */
			ddi_devid_free(devid);
			break;
		} else if (rval == DDI_NOT_WELL_FORMED) {
			/*
			 * return value of ddi_devid_scsi_encode equal to
			 * DDI_NOT_WELL_FORMED means DEVID_RETRY, it worth
			 * to retry inquiry page 0x83 and get GUID.
			 */
			NDBG20(("Not well formed devid, retry..."));
			delay(1 * drv_usectohz(1000000));
			continue;
		} else {
			mpt_log(mpt, CE_WARN, "!Encode devid failed for path "
			    "target:%x, lun:%x, SAS address:%"PRIx64,
			    target, lun, sas_wwn);
			rval = DDI_FAILURE;
			goto create_lun;
		}
	}
	if (i == mpt_inq83_retry_timeout) {
		mpt_log(mpt, CE_WARN, "!Repeated page83 requests timeout for "
		    "path target:%x, lun:%x, SAS address:%"PRIx64,
		    target, lun, sas_wwn);
	}

	rval = DDI_FAILURE;

create_lun:
	if (guid != NULL &&
	    mpt->m_mpxio_enable == TRUE) {
		rval = mpt_create_virt_lun(mpt, sd, guid, sas_wwn, devinfo,
		    lun_dip, &pip);
	}
	if (rval != DDI_SUCCESS) {
		rval = mpt_create_phys_lun(mpt, sd, guid, devinfo,
		    sas_wwn, lun_dip);
	}
out:
	if (guid != NULL) {
		/*
		 * guid no longer needed
		 */
		ddi_devid_free_guid(guid);
	}
	if (inq83 != NULL)
		kmem_free(inq83, inq83_len1);
	return (rval);
}

static int
mpt_create_virt_lun(mpt_t *mpt, struct scsi_device *sd, char *guid,
    uint64_t sas_wwn, uint32_t devinfo, dev_info_t **lun_dip,
    mdi_pathinfo_t **pip)
{
	int target = sd->sd_address.a_target;
	int lun = sd->sd_address.a_lun;
	struct scsi_inquiry *inq = sd->sd_inq;
	char *nodename = NULL;
	char **compatible = NULL;
	int ncompatible	= 0;
	int mdi_rtn = MDI_FAILURE;
	char *lun_addr = NULL;
	char *wwn_str = NULL;
	int rval = DDI_FAILURE;
	char *old_guid = NULL;
	char *component = NULL;
	uint8_t phynum;
	mpt_slots_t *slots = mpt->m_active;

	if ((mpt->m_sata_mpxio_enable == FALSE) &&
	    (devinfo & MPI_SAS_DEVICE_INFO_SATA_DEVICE)) {
		return (DDI_FAILURE);
	}

	*pip = mpt_find_path(mpt, target, lun);

	if (*pip != NULL) {
		*lun_dip = MDI_PI(*pip)->pi_client->ct_dip;
		ASSERT(*lun_dip != NULL);
		if (ddi_prop_lookup_string(DDI_DEV_T_ANY, *lun_dip,
		    (DDI_PROP_DONTPASS | DDI_PROP_NOTPROM),
		    MDI_CLIENT_GUID_PROP, &old_guid) == DDI_SUCCESS) {
			if (strncmp(guid, old_guid, strlen(guid)) == 0) {
				/*
				 * Same path back online again.
				 */
				(void) ddi_prop_free(old_guid);
				if (!MDI_PI_IS_ONLINE(*pip) &&
				    !MDI_PI_IS_STANDBY(*pip)) {
					rval = mdi_pi_online(*pip, 0);
				} else {
					rval = DDI_SUCCESS;
				}
				if (rval != DDI_SUCCESS) {
					mpt_log(mpt, CE_WARN, "path: target:%x,"
					    "lun:%x online failed!", target,
					    lun);
					*pip = NULL;
					*lun_dip = NULL;
				}
				return (rval);
			} else {
				/*
				 * The GUID of the LUN has changed which maybe
				 * because customer mapped another volume to the
				 * same LUN.
				 */
				mpt_log(mpt, CE_WARN, "The GUID of the target:"
				    "%x, lun:%x was changed, maybe because "
				    "someone mapped another volume to the same "
				    "LUN", target, lun);
				(void) ddi_prop_free(old_guid);
				if (!MDI_PI_IS_OFFLINE(*pip)) {
					rval = mdi_pi_offline(*pip, 0);
					if (rval != MDI_SUCCESS) {
						mpt_log(mpt, CE_WARN, "path:"
						    "target:%x, lun:%x offline "
						    "failed!", target, lun);
						*pip = NULL;
						*lun_dip = NULL;
						return (DDI_FAILURE);
					}
				}
				if (mdi_pi_free(*pip, 0) != MDI_SUCCESS) {
					mpt_log(mpt, CE_WARN, "path:target %x"
					    ", lun:%x free failed!", target,
					    lun);
					*pip = NULL;
					*lun_dip = NULL;
					return (DDI_FAILURE);
				}
			}
		} else {
			mpt_log(mpt, CE_WARN, "Can't get client-guid property"
			    " for path:target:%x, lun:%x", target, lun);
			*pip = NULL;
			*lun_dip = NULL;
			return (DDI_FAILURE);
		}
	}

	scsi_hba_nodename_compatible_get(inq, NULL,
	    inq->inq_dtype, NULL, &nodename, &compatible, &ncompatible);

	/*
	 * if nodename can't be determined then print a message and skip it
	 */
	if (nodename == NULL) {
		mpt_log(mpt, CE_WARN, "mpt sas driver found no compatible "
		    "driver for target%d lun %d dtype:0x%02x", target, lun,
		    inq->inq_dtype);
		return (DDI_FAILURE);
	}

	lun_addr = kmem_zalloc(SCSI_MAXNAMELEN, KM_SLEEP);
	(void) sprintf(lun_addr, "%x,%x", target, lun);

	mdi_rtn = mdi_pi_alloc_compatible(mpt->m_dip, nodename,
	    guid, lun_addr, compatible, ncompatible,
	    0, pip);
	if (mdi_rtn == MDI_SUCCESS) {

		if (mdi_prop_update_string(*pip, MDI_GUID,
		    guid) != DDI_SUCCESS) {
			mpt_log(mpt, CE_WARN, "mpt sas driver unable to create "
			    "property for target %d lun %d (MDI_GUID)", target,
			    lun);
			mdi_rtn = MDI_FAILURE;
			goto virt_create_done;
		}

		if (mdi_prop_update_int(*pip, TARGET_PROP,
		    target) != DDI_SUCCESS) {
			mpt_log(mpt, CE_WARN, "mpt sas driver unable to create "
			    "property for target %d lun %d (TARGET_PROP)",
			    target, lun);
			mdi_rtn = MDI_FAILURE;
			goto virt_create_done;
		}

		if (mdi_prop_update_int(*pip, LUN_PROP,
		    lun) != DDI_SUCCESS) {
			mpt_log(mpt, CE_WARN, "mpt sas driver unable to create "
			    "property for target %d lun %d (LUN_PROP)", target,
			    lun);
			mdi_rtn = MDI_FAILURE;
			goto virt_create_done;
		}

		if (mdi_prop_update_string_array(*pip, "compatible",
		    compatible, ncompatible) !=
		    DDI_PROP_SUCCESS) {
			mpt_log(mpt, CE_WARN, "mpt sas driver unable to create "
			    "property for target %d lun %d (COMPATIBLE)",
			    target, lun);
			mdi_rtn = MDI_FAILURE;
			goto virt_create_done;
		}

		wwn_str = kmem_zalloc(MPT_SAS_WWN_STRLEN, KM_SLEEP);
		/* The property is needed by MPAPI */
		(void) sprintf(wwn_str, "%016"PRIx64, sas_wwn);

		if (sas_wwn && mdi_prop_update_string(*pip,
		    SCSI_ADDR_PROP_TARGET_PORT, wwn_str) != DDI_PROP_SUCCESS) {
			mpt_log(mpt, CE_WARN, "mpt sas driver unable to create "
			    "property for target %d lun %d ("
			    SCSI_ADDR_PROP_TARGET_PORT ")",
			    target, lun);
			mdi_rtn = MDI_FAILURE;
			goto virt_create_done;
		}

		if (mpt->m_wwid_obp && (inq->inq_dtype == 0)) {
			component = kmem_zalloc(MAXPATHLEN, KM_SLEEP);
			/*
			 * set obp path for pathinfo
			 */
			if (sas_wwn) {
				(void) snprintf(component, MAXPATHLEN,
				    "disk@w%s,%x", wwn_str, lun);
			} else {
				mutex_enter(&mpt->m_mutex);
				phynum = slots->m_target[target].m_phynum;
				mutex_exit(&mpt->m_mutex);
				(void) snprintf(component, MAXPATHLEN,
				    "disk@p%x,%x", phynum, lun);
			}

			if (mdi_pi_pathname_obp_set(*pip, component) !=
			    DDI_SUCCESS) {
				mpt_log(mpt, CE_WARN, "mpt sas driver unable to"
				    " set obp-path for target %d lun %d",
				    target, lun);
				mdi_rtn = MDI_FAILURE;
				goto virt_create_done;
			}
		}

		*lun_dip = MDI_PI(*pip)->pi_client->ct_dip;
		if (devinfo & (MPI_SAS_DEVICE_INFO_SATA_DEVICE |
		    MPI_SAS_DEVICE_INFO_ATAPI_DEVICE)) {
			int cap = MPT_CAP_LOG_SENSE | MPT_CAP_START_STOP;
			if ((ndi_prop_update_int(DDI_DEV_T_NONE, *lun_dip,
			    "pm-capable", cap)) !=
			    DDI_PROP_SUCCESS) {
				mpt_log(mpt, CE_WARN, "mpt sas driver"
				    "failed to create pm-capable "
				    "property, target %d", target);
				mdi_rtn = MDI_FAILURE;
				goto virt_create_done;
			}
		}

		mdi_rtn = mdi_pi_online(*pip, 0);
		if (mdi_rtn == MDI_NOT_SUPPORTED) {
			mdi_rtn = MDI_FAILURE;
		}
virt_create_done:
		if (*pip && mdi_rtn != MDI_SUCCESS) {
			(void) mdi_pi_free(*pip, 0);
			*pip = NULL;
			*lun_dip = NULL;
		}
	}

	scsi_hba_nodename_compatible_free(nodename, compatible);
	if (lun_addr != NULL) {
		kmem_free(lun_addr, SCSI_MAXNAMELEN);
	}
	if (wwn_str != NULL) {
		kmem_free(wwn_str, MPT_SAS_WWN_STRLEN);
	}
	if (component != NULL) {
		kmem_free(component, MAXPATHLEN);
	}

	return ((mdi_rtn == MDI_SUCCESS) ? DDI_SUCCESS : DDI_FAILURE);
}

static int
mpt_create_phys_lun(mpt_t *mpt, struct scsi_device *sd,
    char *guid, uint32_t dev_info, uint64_t sas_wwn, dev_info_t **lun_dip)
{
	int target = sd->sd_address.a_target;
	int lun = sd->sd_address.a_lun;
	mpt_slots_t *slots = mpt->m_active;
	struct scsi_inquiry *inq = sd->sd_inq;
	int ndi_rtn = NDI_FAILURE;
	char *wwn_str = NULL;
	uint64_t be_sas_wwn;
	char *nodename = NULL;
	char **compatible = NULL;
	int ncompatible = 0;
	int instance = 0;
	char *component = NULL;
	uint8_t phynum;

	/*
	 * generate compatible property with binding-set "mpt"
	 */
	scsi_hba_nodename_compatible_get(inq, "mpt", inq->inq_dtype, NULL,
	    &nodename, &compatible, &ncompatible);

	/*
	 * if nodename can't be determined then print a message and skip it
	 */
	if (nodename == NULL) {
		mpt_log(mpt, CE_WARN, "mpt driver found no comptible driver "
		    "for target %d lun %d", target, lun);
		return (DDI_FAILURE);
	}

	ndi_rtn = ndi_devi_alloc(mpt->m_dip, nodename,
	    DEVI_SID_NODEID, lun_dip);

	/*
	 * if lun alloc success, set props
	 */
	if (ndi_rtn == NDI_SUCCESS) {

		if (ndi_prop_update_int(DDI_DEV_T_NONE,
		    *lun_dip, TARGET_PROP, (int)target) !=
		    DDI_PROP_SUCCESS) {
			mpt_log(mpt, CE_WARN, "mpt driver unable to create "
			    "property for target %d lun %d (TARGET_PROP)",
			    target, lun);
			ndi_rtn = NDI_FAILURE;
			goto phys_create_done;
		}

		if (ndi_prop_update_int(DDI_DEV_T_NONE,
		    *lun_dip, LUN_PROP, lun) !=
		    DDI_PROP_SUCCESS) {
			mpt_log(mpt, CE_WARN, "mpt driver unable to create "
			    "property for target %d lun %d (LUN_PROP)",
			    target, lun);
			ndi_rtn = NDI_FAILURE;
			goto phys_create_done;
		}

		if (ndi_prop_update_string_array(DDI_DEV_T_NONE,
		    *lun_dip, "compatible", compatible, ncompatible)
		    != DDI_PROP_SUCCESS) {
			mpt_log(mpt, CE_WARN, "mpt driver unable to create "
			    "property for target %d lun %d (COMPATIBLE)",
			    target, lun);
			ndi_rtn = NDI_FAILURE;
			goto phys_create_done;
		}

		if (MPT_IS_SAS(mpt)) {
			wwn_str = kmem_zalloc(MPT_SAS_WWN_STRLEN, KM_SLEEP);
			/*
			 * We need the SAS WWN for non-multipath devices, so
			 * we'll use the same property as that multipathing
			 * devices need to present for MPAPI. If we don't have
			 * a WWN (e.g. parallel SCSI), don't create the prop.
			 */
			(void) sprintf(wwn_str, "%016"PRIx64, sas_wwn);
			if (sas_wwn && ndi_prop_update_string(DDI_DEV_T_NONE,
			    *lun_dip, SCSI_ADDR_PROP_TARGET_PORT, wwn_str)
			    != DDI_PROP_SUCCESS) {
				mpt_log(mpt, CE_WARN, "mpt driver unable to "
				    "create property for SAS target %d lun %d ("
				    SCSI_ADDR_PROP_TARGET_PORT ")",
				    target, lun);
				ndi_rtn = NDI_FAILURE;
				goto phys_create_done;
			}
			be_sas_wwn = BE_64(sas_wwn);
			if (sas_wwn && ndi_prop_update_byte_array(
			    DDI_DEV_T_NONE, *lun_dip, "port-wwn",
			    (uchar_t *)&be_sas_wwn, 8) != DDI_PROP_SUCCESS) {
				mpt_log(mpt, CE_WARN, "mpt driver unable to "
				    "create property for SAS target %d lun %d "
				    "(port-wwn)", target, lun);
				ndi_rtn = NDI_FAILURE;
				goto phys_create_done;
			}
			if (ndi_prop_create_boolean(DDI_DEV_T_NONE,
			    *lun_dip, SAS_PROP) != DDI_PROP_SUCCESS) {
				mpt_log(mpt, CE_WARN, "mpt driver unable to"
				    "create property for SAS target %d lun %d"
				    " (SAS_PROP)", target, lun);
				ndi_rtn = NDI_FAILURE;
				goto phys_create_done;
			}
			if (guid && (ndi_prop_update_string(DDI_DEV_T_NONE,
			    *lun_dip, NDI_GUID, guid) != DDI_SUCCESS)) {
				mpt_log(mpt, CE_WARN, "mpt sas driver unable "
				    "to create guid property for target %d "
				    "lun %d", target, lun);
				ndi_rtn = NDI_FAILURE;
				goto phys_create_done;
			}

			/*
			 * if this is a SAS controller, and the target is a SATA
			 * drive, set the 'pm-capable' property for sd and if on
			 * an OPL platform, also check if this is an ATAPI
			 * device.
			 */
			instance = ddi_get_instance(mpt->m_dip);
			if (dev_info & (MPI_SAS_DEVICE_INFO_SATA_DEVICE |
			    MPI_SAS_DEVICE_INFO_ATAPI_DEVICE)) {
				int cap =
				    MPT_CAP_LOG_SENSE | MPT_CAP_START_STOP;
				NDBG2(("mpt%d: creating pm-capable property, "
				    "target %d", instance, target));

				if ((ndi_prop_update_int(DDI_DEV_T_NONE,
				    *lun_dip, "pm-capable", cap)) !=
				    DDI_PROP_SUCCESS) {
					mpt_log(mpt, CE_WARN, "mpt driver"
					    "failed to create pm-capable "
					    "property, target %d", target);
					ndi_rtn = NDI_FAILURE;
					goto phys_create_done;
				}

				if (mpt_scsi_tgt_atapi(sd) == TRUE) {
					if ((ndi_prop_update_int(DDI_DEV_T_NONE,
					    *lun_dip, "atapi", 1)) !=
					    DDI_PROP_SUCCESS) {
						mpt_log(mpt, CE_WARN, "mpt "
						    "driver failed to create "
						    "atapi property, for target"
						    "%d", target);
						ndi_rtn = NDI_FAILURE;
						goto phys_create_done;
					}
				}
			}

			if (mpt->m_wwid_obp && (inq->inq_dtype == 0)) {
				component = kmem_zalloc(MAXPATHLEN, KM_SLEEP);
				/*
				 * add 'obp-path' properties for devinfo
				 */
				if (sas_wwn) {
					(void) snprintf(component, MAXPATHLEN,
					    "disk@w%s,%x", wwn_str, lun);
				} else {
					mutex_enter(&mpt->m_mutex);
					phynum =
					    slots->m_target[target].m_phynum;
					mutex_exit(&mpt->m_mutex);
					(void) snprintf(component, MAXPATHLEN,
					    "disk@p%x,%x", phynum, lun);
				}
				if (ddi_pathname_obp_set(*lun_dip, component)
				    != DDI_SUCCESS) {
					mpt_log(mpt, CE_WARN, "mpt driver "
					    "unable to set obp-path for SAS "
					    "target %d lun %d ", target, lun);
					ndi_rtn = NDI_FAILURE;
					goto phys_create_done;
				}
			}
		} else {

			/*
			 * the scsi-options precedence is:
			 *	target-scsi-options		highest
			 *	device-type-scsi-options
			 *	per bus scsi-options
			 *	global scsi-options		lowest
			 */
			mutex_enter(&mpt->m_mutex);
			if ((mpt->m_target_scsi_options_defined & (1 << target))
			    == 0) {
				int options;
				options = scsi_get_device_type_scsi_options(
				    *lun_dip, sd, -1);
				if (options != -1) {
					mpt->m_target_scsi_options[target] =
					    options;
					mpt_log(mpt, CE_NOTE,
					    "?target%x-scsi-options = 0x%x\n",
					    target,
					    mpt->m_target_scsi_options[target]);
				}
				/* update max LUNs for this tgt */
				mpt_update_max_luns(mpt, target);
			}
			mutex_exit(&mpt->m_mutex);
		}

phys_create_done:
		/*
		 * If props were setup ok, online the lun
		 */
		if (ndi_rtn == NDI_SUCCESS) {
			/*
			 * Try to online the new node
			 */
			ndi_rtn = ndi_devi_online(*lun_dip, NDI_ONLINE_ATTACH);
		}

		/*
		 * If success set rtn flag, else unwire alloc'd lun
		 */
		if (ndi_rtn != NDI_SUCCESS) {
			NDBG12(("mpt driver unable to online "
			    "target %d lun %d", target, lun));
			ndi_prop_remove_all(*lun_dip);
			(void) ndi_devi_free(*lun_dip);
			*lun_dip = NULL;
		}
	}

	scsi_hba_nodename_compatible_free(nodename, compatible);

	if (wwn_str != NULL) {
		kmem_free(wwn_str, MPT_SAS_WWN_STRLEN);
	}
	if (component != NULL) {
		kmem_free(component, MAXPATHLEN);
	}

	return ((ndi_rtn == NDI_SUCCESS) ? DDI_SUCCESS : DDI_FAILURE);
}

static void
mpt_gen_sysevent(mpt_t *mpt, int target, int hint)
{
	char ap[MAXPATHLEN];
	nvlist_t *ev_attr_list = NULL;
	int err;

	/* Allocate and build sysevent attribute list */
	err = nvlist_alloc(&ev_attr_list, NV_UNIQUE_NAME_TYPE, DDI_NOSLEEP);
	if (err != 0) {
		mpt_log(mpt, CE_WARN, "cannot allocate memory for sysevent "
		    "attributes");
		return;
	}

	/* Add hint attribute */
	err = nvlist_add_string(ev_attr_list, DR_HINT, SE_HINT2STR(hint));
	if (err != 0) {
		mpt_log(mpt, CE_WARN, "failed to add DR_HINT attr for "
		    "sysevent");
		nvlist_free(ev_attr_list);
		return;
	}

	/*
	 * Add AP attribute.
	 * Get controller pathname and convert it into AP pathname by adding
	 * a target number.
	 */
	(void) snprintf(ap, MAXPATHLEN, "/devices");
	(void) ddi_pathname(mpt->m_dip, ap + strlen(ap));
	(void) snprintf(ap + strlen(ap), MAXPATHLEN - strlen(ap), ":%d",
	    target);

	err = nvlist_add_string(ev_attr_list, DR_TARGET_ID, ap);
	if (err != 0) {
		mpt_log(mpt, CE_WARN, "failed to add DR_TARGET_ID attr for "
		    "sysevent");
		nvlist_free(ev_attr_list);
		return;
	}

	/* Generate/log sysevent */
	err = ddi_log_sysevent(mpt->m_dip, DDI_VENDOR_SUNW, EC_DR,
	    ESC_DR_TARGET_STATE_CHANGE, ev_attr_list, NULL, DDI_NOSLEEP);
	if (err != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "cannot log sysevent, err code %x", err);
	}

	nvlist_free(ev_attr_list);
}

static int
mpt_sas_config_smp(mpt_t *mpt, uint64_t sas_wwn, dev_info_t **smp_dip)
{
	int ndi_rtn = NDI_FAILURE;
	char wwn_str[MPT_SAS_WWN_STRLEN];
	struct smp_device smp_sd;

	/* XXX driver should not allocate its own smp_device */
	bzero(&smp_sd, sizeof (struct smp_device));
	smp_sd.smp_sd_address.smp_a_hba_tran = mpt->m_smptran;
	bcopy(&sas_wwn, smp_sd.smp_sd_address.smp_a_wwn, SAS_WWN_BYTE_SIZE);

	if (smp_probe(&smp_sd) != DDI_PROBE_SUCCESS) {
		goto out;
	}

	(void) sprintf(wwn_str, "%"PRIx64, sas_wwn);

	if ((*smp_dip = mpt_find_smp_child(mpt, wwn_str)) != NULL) {
		ndi_rtn = NDI_SUCCESS;
		goto out;
	}

	ndi_rtn = ndi_devi_alloc(mpt->m_dip, "smp",
	    DEVI_SID_NODEID, smp_dip);

	/*
	 * if lun alloc success, set props
	 */
	if (ndi_rtn == NDI_SUCCESS) {

		/*
		 * Set the flavor of this child to be SMP flavored...
		 */
		ndi_flavor_set(*smp_dip, SCSA_FLAVOR_SMP);

		if (ndi_prop_update_string(DDI_DEV_T_NONE,
		    *smp_dip, SMP_WWN, wwn_str) !=
		    DDI_PROP_SUCCESS) {
			mpt_log(mpt, CE_WARN, "mpt driver unable to create "
			    "property for smp device %s (sas_wwn)",
			    wwn_str);
			ndi_rtn = NDI_FAILURE;
			goto smp_create_done;
		}

		if (ndi_prop_create_boolean(DDI_DEV_T_NONE,
		    *smp_dip, SMP_PROP) != DDI_PROP_SUCCESS) {
			mpt_log(mpt, CE_WARN, "mpt driver unable to "
			    "create property for SMP %s (SMP_PROP) ",
			    wwn_str);
			ndi_rtn = NDI_FAILURE;
			goto smp_create_done;
		}

smp_create_done:
		/*
		 * If props were setup ok, online the lun
		 */
		if (ndi_rtn == NDI_SUCCESS) {
			/*
			 * Try to online the new node
			 */
			ndi_rtn = ndi_devi_online(*smp_dip, NDI_ONLINE_ATTACH);
		}

		/*
		 * If success set rtn flag, else unwire alloc'd lun
		 */
		if (ndi_rtn != NDI_SUCCESS) {
			NDBG12(("mpt driver unable to online "
			    "SMP target %s", wwn_str));
			ndi_prop_remove_all(*smp_dip);
			(void) ndi_devi_free(*smp_dip);
		}
	}

out:
	return ((ndi_rtn == NDI_SUCCESS) ? DDI_SUCCESS : DDI_FAILURE);
}

/* smp transport routine */
static int
mpt_smp_start(struct smp_pkt *smp_pkt)
{
	uint64_t wwn;
	msg_smp_passthrough_t req;
	msg_smp_passthrough_reply_t rep;
	uint32_t direction = 0;
	uint16_t iocstatus;
	mpt_t *mpt;
	int ret;

	mpt = smp_pkt->smp_pkt_address->smp_a_hba_tran->smp_tran_hba_private;
	bcopy(smp_pkt->smp_pkt_address->smp_a_wwn, &wwn, SAS_WWN_BYTE_SIZE);
	/*
	 * Need to compose a SMP request message
	 * and call mpt_do_passthru() function
	 */
	bzero(&req, sizeof (req));
	bzero(&rep, sizeof (rep));
	req.Flags = 0;
	req.PhysicalPort = 0xff;
	req.ChainOffset = 0;
	req.Function = MPI_FUNCTION_SMP_PASSTHROUGH;

	if ((smp_pkt->smp_pkt_reqsize & 0xffff0000ul) != 0) {
		smp_pkt->smp_pkt_reason = ERANGE;
		return (DDI_FAILURE);
	}
	req.RequestDataLength = LE_16((uint16_t)(smp_pkt->smp_pkt_reqsize - 4));

	req.ConnectionRate = MPI_SMP_PT_REQ_CONNECT_RATE_NEGOTIATED;
	req.MsgFlags = 0;
	req.SASAddress = LE_64(wwn);
	if (smp_pkt->smp_pkt_rspsize > 0) {
		direction |= MPT_PASS_THRU_READ;
	}
	if (smp_pkt->smp_pkt_reqsize > 0) {
		direction |= MPT_PASS_THRU_WRITE;
	}

	mutex_enter(&mpt->m_mutex);
	ret = mpt_do_passthru(mpt, (uint8_t *)&req, (uint8_t *)&rep,
	    (uint8_t *)smp_pkt->smp_pkt_rsp, sizeof (req), sizeof (rep),
	    smp_pkt->smp_pkt_rspsize - 4, direction,
	    (uint8_t *)smp_pkt->smp_pkt_req, smp_pkt->smp_pkt_reqsize - 4,
	    smp_pkt->smp_pkt_timeout, FKIOCTL);
	mutex_exit(&mpt->m_mutex);
	if (ret != 0) {
		cmn_err(CE_WARN, "smp_start do passthru error %d", ret);
		smp_pkt->smp_pkt_reason = (uchar_t)(ret);
		return (DDI_FAILURE);
	}
	/* do passthrough success, check the smp status */
	iocstatus = LE_16(rep.IOCStatus);
	if (iocstatus & MPI_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE) {
		/*
		 * If the condition above is false, the flag is not set,
		 * so we can unset it here instead of the main code
		 */
		iocstatus &= MPI_IOCSTATUS_MASK;
		if (iocstatus == MPI_IOCSTATUS_SUCCESS) {
			mpt_log(mpt, CE_NOTE, "!mpt_smp_start: IOCLogInfo=0x%x",
			    LE_32(rep.IOCLogInfo));
		} else {
			mpt_log(mpt, CE_NOTE, "!mpt_smp_start: IOCStatus=0x%x"
			    " IOCLogInfo=0x%x", iocstatus,
			    LE_32(rep.IOCLogInfo));
		}
	}
	if (iocstatus != MPI_IOCSTATUS_SUCCESS) {
		switch (iocstatus) {
		case MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
			smp_pkt->smp_pkt_reason = ENODEV;
			break;
		case MPI_IOCSTATUS_SAS_SMP_DATA_OVERRUN:
			smp_pkt->smp_pkt_reason = EOVERFLOW;
			break;
		case MPI_IOCSTATUS_SAS_SMP_REQUEST_FAILED:
			smp_pkt->smp_pkt_reason = EIO;
			break;
		default:
			mpt_log(mpt, CE_NOTE, "mpt_smp_start: get unknown ioc"
			    "status:%x", iocstatus);
			smp_pkt->smp_pkt_reason = EIO;
			break;
		}
		return (DDI_FAILURE);
	}
	if (rep.SASStatus != MPI_SASSTATUS_SUCCESS) {
		mpt_log(mpt, CE_NOTE, "smp_start: get error SAS status:%x",
		    rep.SASStatus);
		smp_pkt->smp_pkt_reason = EIO;
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static void
mpt_idle_pm(void *arg)
{
	mpt_t *mpt = arg;

	(void) pm_idle_component(mpt->m_dip, 0);
	mutex_enter(&mpt->m_mutex);
	mpt->m_pm_timeid = 0;
	mutex_exit(&mpt->m_mutex);
}


/*
 * Read IO unit page 0 to find out if the PHYs are currently in target mode.
 * If they are not, we don't need to change anything.  Otherwise, we need to
 * modify the appropriate bits and write them to IO unit page 1.  Once that
 * is done, an IO unit reset is necessary to begin operating in initiator mode.
 */

static int
mpt_set_init_mode(mpt_t *mpt)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_sas_io_unit_0_t *sasioupage0;
	config_page_sas_io_unit_1_t *sasioupage1;
	int recv_numbytes;
	caddr_t recv_memp, page_memp;
	int recv_dmastate = 0;
	int page_dmastate = 0;
	int i, num_phys;
	int page0_size = sizeof (config_page_sas_io_unit_0_t) +
	    (sizeof (mpi_sas_io_unit0_phy_data_t) * 7);
	int page1_size = sizeof (config_page_sas_io_unit_1_t) +
	    (sizeof (mpi_sas_io_unit1_phy_data_t) * 7);
	uint32_t flags_length;
	uint32_t cpdi[8], reprogram = 0;
	uint16_t iocstatus;
	uint16_t extplen;
	uint8_t port_flags, page_number, action;
	uint32_t reply_size = 256;	/* Large enough for any page */
	uint_t state;
	int rval = DDI_FAILURE;

	/*
	 * Initialize our "state machine".  This is a bit convoluted,
	 * but it keeps us from having to do the ddi allocations numerous
	 * times.
	 */

	ASSERT(mutex_owned(&mpt->m_mutex));
	state = IOUC_READ_PAGE0;

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes mpt's config reply page request structure.
	 */
	recv_dma_attrs = mpt_dma_attrs;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular = (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		goto cleanup;
	}

	recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		goto cleanup;
	}

	recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		goto cleanup;
	}

	recv_dmastate |= MPT_DMA_HANDLE_BOUND;

	page_dma_attrs = mpt_dma_attrs;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular = reply_size;

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		goto cleanup;
	}

	page_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	/*
	 * Page 0 size is larger, so just use that for both.
	 */

	if (ddi_dma_mem_alloc(page_dma_handle, reply_size,
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		goto cleanup;
	}

	page_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		goto cleanup;
	}

	page_dmastate |= MPT_DMA_HANDLE_BOUND;

	/*
	 * Now we cycle through the state machine.  Here's what happens:
	 * 1. Read IO unit page 0.
	 * 2. See if changes are required.
	 * 2a. If changes are not required, simply exit, otherwise:
	 * 3. Read IO unit page 1.
	 * 4. Change the appropriate bits
	 * 5. Write the updated settings to IO unit page 1.
	 * 6. Reset the IO unit.
	 */

	sasioupage0 = (config_page_sas_io_unit_0_t *)page_memp;
	sasioupage1 = (config_page_sas_io_unit_1_t *)page_memp;

	while (state != IOUC_DONE) {
		switch (state) {
		case IOUC_READ_PAGE0:
			page_number = 0;
			action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
			flags_length = (uint32_t)page0_size;
			flags_length |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
			    MPI_SGE_FLAGS_END_OF_BUFFER |
			    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
			    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
			    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
			    MPI_SGE_FLAGS_IOC_TO_HOST |
			    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

			break;

		case IOUC_READ_PAGE1:
			page_number = 1;
			action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
			flags_length = (uint32_t)page1_size;
			flags_length |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
			    MPI_SGE_FLAGS_END_OF_BUFFER |
			    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
			    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
			    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
			    MPI_SGE_FLAGS_IOC_TO_HOST |
			    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

			break;

		case IOUC_WRITE_PAGE1:
			page_number = 1;
			action = MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM;
			flags_length = (uint32_t)page1_size;
			flags_length |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
			    MPI_SGE_FLAGS_END_OF_BUFFER |
			    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
			    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
			    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
			    MPI_SGE_FLAGS_HOST_TO_IOC |
			    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

			break;
		}

		bzero(recv_memp, sizeof (*configreply));
		configreply = (struct msg_config_reply *)recv_memp;
		recv_numbytes = sizeof (*configreply);

		if (mpt_send_extended_config_request_msg(mpt,
		    MPI_CONFIG_ACTION_PAGE_HEADER,
		    MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT,
		    0, page_number, 0, 0, 0, 0)) {
			goto cleanup;
		}

		if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
		    recv_accessp)) {
			goto cleanup;
		}

		if ((iocstatus = mpt_handle_ioc_status(mpt, recv_accessp,
		    &configreply->IOCStatus, &configreply->IOCLogInfo,
		    "mpt_set_init_mode", 0)) != MPI_IOCSTATUS_SUCCESS) {
			mpt_log(mpt, CE_WARN,
			    "mpt_set_init_mode: read page hdr iocstatus = "
			    " 0x%x", iocstatus);
			goto cleanup;
		}

		/*
		 * No more than 8 phys supported
		 */

		extplen = ddi_get16(recv_accessp, &configreply->ExtPageLength);

		if ((extplen << 2) > page0_size) {
			mpt_log(mpt, CE_WARN,
			    "mpt_set_init_mode: read page hdr extpagelength = "
			    "0x%x. The largest extpagelengh supported is "
			    "0x%x (8 phys)", extplen, (page0_size >> 2));
			goto cleanup;
		}

		if (action != MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM) {
			bzero(page_memp, reply_size);
		}

		if (mpt_send_extended_config_request_msg(mpt, action,
		    MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 0, page_number,
		    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
		    ddi_get16(recv_accessp, &configreply->ExtPageLength),
		    flags_length, page_cookie.dmac_address)) {
			goto cleanup;
		}

		if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
		    recv_accessp)) {
			goto cleanup;
		}

		if ((iocstatus = mpt_handle_ioc_status(mpt, recv_accessp,
		    &configreply->IOCStatus, &configreply->IOCLogInfo,
		    "mpt_set_init_mode", 0)) != MPI_IOCSTATUS_SUCCESS) {
			mpt_log(mpt, CE_WARN,
			    "mpt_set_init_mode: IO unit config failed for "
			    "action %d, iocstatus = 0x%x", action, iocstatus);
			goto cleanup;
		}

		switch (state) {
		case IOUC_READ_PAGE0:
			if ((ddi_dma_sync(page_dma_handle, 0, 0,
			    DDI_DMA_SYNC_FORCPU)) != DDI_SUCCESS) {
				goto cleanup;
			}

			mpt->m_nvdata_ver_default = ddi_get16(page_accessp,
			    &sasioupage0->NvdataVersionDefault);
			mpt->m_nvdata_ver_persistent = ddi_get16(page_accessp,
			    &sasioupage0->NvdataVersionPersistent);

			num_phys = ddi_get8(page_accessp,
			    &sasioupage0->NumPhys);

			/*
			 * Although every PHY should have the same settings,
			 * check each one's settings.  If any PHY requires
			 * changes, issue the SAS IO Unit Page 1 request.
			 */

			for (i = 0; i < num_phys; i++) {
				cpdi[i] = ddi_get32(page_accessp,
				    &sasioupage0->PhyData[i].
				    ControllerPhyDeviceInfo);
				port_flags = ddi_get8(page_accessp,
				    &sasioupage0->PhyData[i].PortFlags);

				if (port_flags & DISCOVERY_IN_PROGRESS) {
					mpt_log(mpt, CE_WARN,
					    "Discovery in progress, can't "
					    "verify IO unit config");
					reprogram = 0;
					break;
				}

				if (cpdi[i] & MPI_SAS_DEVICE_INFO_SSP_TARGET) {
					reprogram = 1;
					break;
				}
			}

			if (reprogram == 0) {
				state = IOUC_DONE;
				rval = DDI_SUCCESS;
				break;
			}

			state = IOUC_READ_PAGE1;
			break;

		case IOUC_READ_PAGE1:
			if ((ddi_dma_sync(page_dma_handle, 0, 0,
			    DDI_DMA_SYNC_FORCPU)) != DDI_SUCCESS) {
				goto cleanup;
			}

			/*
			 * All the PHYs should have the same settings, so we
			 * really only need to read 1 and use its config for
			 * every PHY.
			 */

			cpdi[0] = ddi_get32(page_accessp,
			    &sasioupage1->PhyData[0].ControllerPhyDeviceInfo);
			port_flags = ddi_get8(page_accessp,
			    &sasioupage1->PhyData[0].PortFlags);
			port_flags |=
			    MPI_SAS_IOUNIT1_PORT_FLAGS_AUTO_PORT_CONFIG;

			/*
			 * Write the configuration to SAS I/O unit page 1
			 */

			mpt_log(mpt, CE_NOTE,
			    "?IO unit in target mode, changing to initiator");

			/*
			 * Modify the PHY settings for initiator mode
			 */

			cpdi[0] &= ~MPI_SAS_DEVICE_INFO_SSP_TARGET;
			cpdi[0] |= (MPI_SAS_DEVICE_INFO_SSP_INITIATOR |
			    MPI_SAS_DEVICE_INFO_STP_INITIATOR |
			    MPI_SAS_DEVICE_INFO_SMP_INITIATOR);

			for (i = 0; i < num_phys; i++) {
				ddi_put32(page_accessp,
				    &sasioupage1->PhyData[i].
				    ControllerPhyDeviceInfo, cpdi[0]);
				ddi_put8(page_accessp,
				    &sasioupage1->PhyData[i].
				    PortFlags, port_flags);
			}

			if ((ddi_dma_sync(page_dma_handle, 0, 0,
			    DDI_DMA_SYNC_FORDEV)) != DDI_SUCCESS) {
				goto cleanup;
			}

			state = IOUC_WRITE_PAGE1;

			break;

		case IOUC_WRITE_PAGE1:
			/*
			 * If we're here, we wrote IO unit page 1 succesfully.
			 */
			state = IOUC_DONE;
			rval = DDI_SUCCESS;
			break;
		}
	}

	/*
	 * If we reprogrammed the IO unit, we need to do an I/O unit reset in
	 * order to activate the changes.
	 */

	if (reprogram) {
		mpt->m_softstate |= MPT_SS_IO_UNIT_RESET;
		rval = mpt_init_chip(mpt, FALSE);
	}

cleanup:
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);

	return (rval);
}

static uint64_t
mpt_get_sata_device_name(mpt_t *mpt, int target)
{
	uchar_t *inq89 = NULL;
	int inq89_len = 0x238;
	int reallen = 0;
	struct sata_id *sid = NULL;
	char *uid = NULL;
	uint64_t sas_wwn = 0;
	/*
	 * According SCSI/ATA Translation -2 (SAT-2) revision 01a
	 * chapter 12.4.2 VPD page 89h includes 512 bytes ATA IDENTIFY
	 * (PACKET) DEVICE data.
	 */
	inq89 = kmem_zalloc(inq89_len, KM_SLEEP);
	if (mpt_send_inquiryVpd(mpt, target, 0, 0x89,
	    inq89, inq89_len, &reallen) != 0) {
		goto out;
	}

	sid = (void *)(&inq89[60]);
	swab(&sid->ai_naa_ieee_oui, &sas_wwn, SAS_WWN_BYTE_SIZE);
	uid = (void *)(&sas_wwn);
	if ((*uid & 0xf0) != 0x50) {
		sas_wwn = 0;
		goto out;

	} else {

		/*
		 * Direct attached SATA device has valid NAA IEEE
		 * registered format identifier, then that's WWID
		 * for the SATA device
		 */
		mutex_enter(&mpt->m_mutex);
		/*
		 * Save the WWID for the SATA device
		 */
		mpt->m_active->m_target[target].m_sas_wwn =
		    sas_wwn;
		mutex_exit(&mpt->m_mutex);
	}
out:
	if (inq89 != NULL) {
		kmem_free(inq89, inq89_len);
	}
	return (sas_wwn);
}

/*
 * The mpt->m_active->m_target[x].m_sas_wwn contains the wwid for each disk.
 * For Raid volumes, we need to check m_raidvol[x].m_raidwwid
 * If we didn't get a match, we need to get sas page0 for each device, and
 * untill we get a match
 * If failed, return -1
 */
static int
mpt_wwid_to_tgt(mpt_t *mpt, uint64_t wwid)
{
	int i;
	int ret_tgt = -1;
	int rval = 0;
	int target;
	uint16_t cur_handle;
	int address;
	uint64_t sas_wwn = 0;
	uint32_t dev_info;

	mutex_enter(&mpt->m_mutex);
	for (i = 0; i < MPT_MAX_RAIDVOLS; i++) {
		if (mpt->m_active->m_raidvol[i].m_raidwwid == wwid) {
			ret_tgt = (int)mpt->m_active->m_raidvol[i].m_raidtarg;
			mutex_exit(&mpt->m_mutex);
			return (ret_tgt);
		}
	}

	for (i = 0; i < MPT_MAX_TARGETS; i++) {
		sas_wwn = mpt->m_active->m_target[i].m_sas_wwn;
		if (sas_wwn == wwid) {
			ret_tgt = i;
			mutex_exit(&mpt->m_mutex);
			return (ret_tgt);
		}
	}

	/* If didn't get a match, come here */
	cur_handle = mpt->m_dev_handle;
	for (; ; ) {
		address = (MPI_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE <<
		    MPI_SAS_DEVICE_PGAD_FORM_SHIFT) | cur_handle;
		rval = mpt_get_sas_device_page0(mpt, address,
		    &cur_handle, &dev_info, &sas_wwn, &target);
		if (rval != DDI_SUCCESS)
			break;
		mpt->m_dev_handle = cur_handle;
		if (sas_wwn && (sas_wwn == wwid)) {
			ret_tgt = target;
			break;
		} else if (sas_wwn) {
			continue;
		}
		/*
		 * Direct Attached SATA device get to there
		 */
		mutex_exit(&mpt->m_mutex);
		sas_wwn = mpt_get_sata_device_name(mpt, target);
		if (sas_wwn && (sas_wwn == wwid)) {
			ret_tgt = target;
			return (ret_tgt);
		}
		mutex_enter(&mpt->m_mutex);
	}

	mutex_exit(&mpt->m_mutex);
	return (ret_tgt);
}

/*
 * The mpt->m_active->m_target[x].m_phynum contains the PHYNum for each disk.
 * If we didn't get a match, we need to get sas page0 for each device, and
 * untill we get a match. If failed, return -1
 */
static int
mpt_phy_to_tgt(mpt_t *mpt, uint8_t phy)
{
	int i;
	int ret_tgt = -1;
	int rval = 0;
	int target;
	uint16_t cur_handle;
	int address;
	uint8_t phynum;
	uint32_t dev_info;
	uint64_t sas_wwn = 0;

	mutex_enter(&mpt->m_mutex);

	for (i = 0; i < MPT_MAX_TARGETS; i++) {
		phynum = mpt->m_active->m_target[i].m_phynum;
		if (phynum == phy) {
			ret_tgt = i;
			mutex_exit(&mpt->m_mutex);
			return (ret_tgt);
		}
	}

	/* If didn't get a match, come here */
	cur_handle = mpt->m_dev_handle;
	for (; ; ) {
		address = (MPI_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE <<
		    MPI_SAS_DEVICE_PGAD_FORM_SHIFT) | cur_handle;
		rval = mpt_get_sas_device_page0(mpt, address,
		    &cur_handle, &dev_info, &sas_wwn, &target);
		if (rval != DDI_SUCCESS)
			break;
		mpt->m_dev_handle = cur_handle;
		phynum = mpt->m_active->m_target[target].m_phynum;
		if ((sas_wwn == 0) && (phy == phynum)) {
			ret_tgt = target;
			break;
		}
	}

	mutex_exit(&mpt->m_mutex);
	return (ret_tgt);
}

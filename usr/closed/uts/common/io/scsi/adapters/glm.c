/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * glm - Symbios 53c810a, 53c875, 53c876,
 * 53c895 and 53c896 SCSI Processor HBA driver.
 */
#if defined(lint) && !defined(DEBUG)
#define	DEBUG 1
#define	GLM_DEBUG
#endif

/*
 * standard header files.
 */
#include <sys/note.h>
#include <sys/scsi/scsi.h>
#include <sys/pci.h>
#include <sys/file.h>

/*
 * private header files.
 */
#include <sys/scsi/adapters/glmvar.h>
#include <sys/scsi/adapters/glmreg.h>
#include <sys/scsi/impl/scsi_reset_notify.h>

/*
 * autoconfiguration data and routines.
 */
static int glm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int glm_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);
static int glm_power(dev_info_t *dip, int component, int level);

static void glm_table_init(glm_t *glm, glm_unit_t *unit);
static void glm_tear_down_unit_dsa(struct glm *glm, int targ, int lun);
static int glm_create_unit_dsa(struct glm *glm, int targ, int lun);

static int glm_config_space_init(struct glm *glm);
static void glm_setup_cmd_reg(struct glm *glm);
static int glm_hba_init(glm_t *glm);
static int glm_check_smode(struct glm *glm);
static void glm_hba_fini(glm_t *glm);
static int glm_script_alloc(glm_t *glm);
static void glm_script_free(struct glm *glm);
static void glm_cfg_fini(glm_t *glm_blkp);
static int glm_script_offset(int func);
static int glm_memory_script_init(glm_t *glm);

/*
 * SCSA function prototypes with some helper functions for DMA.
 */
static int glm_scsi_start(struct scsi_address *ap, struct scsi_pkt *pkt);
static int glm_scsi_reset(struct scsi_address *ap, int level);
static int glm_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt);
static int glm_capchk(char *cap, int tgtonly, int *cidxp);
static int glm_scsi_getcap(struct scsi_address *ap, char *cap, int tgtonly);
static int glm_scsi_setcap(struct scsi_address *ap, char *cap, int value,
    int tgtonly);
static void glm_scsi_dmafree(struct scsi_address *ap, struct scsi_pkt *pktp);
static struct scsi_pkt *glm_scsi_init_pkt(struct scsi_address *ap,
    struct scsi_pkt *pkt, struct buf *bp, int cmdlen, int statuslen,
	int tgtlen, int flags, int (*callback)(), caddr_t arg);

static void glm_scsi_sync_pkt(struct scsi_address *ap, struct scsi_pkt *pktp);
static void glm_scsi_destroy_pkt(struct scsi_address *ap,
    struct scsi_pkt *pkt);
static int glm_scsi_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd);
static int glm_scsi_tgt_probe(struct scsi_device *sd,
    int (*callback)());
static void glm_scsi_tgt_free(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd);
static int glm_scsi_reset_notify(struct scsi_address *ap, int flag,
    void (*callback)(caddr_t), caddr_t arg);
static int glm_scsi_quiesce(dev_info_t *hba_dip);
static int glm_scsi_unquiesce(dev_info_t *hba_dip);

static void glm_update_max_luns(struct glm *glm, int tgt);
static int glm_quiesce_bus(struct glm *glm);
static int glm_unquiesce_bus(struct glm *glm);
static void glm_ncmds_checkdrain(void *arg);
static int glm_check_outstanding(struct glm *glm);

/*
 * internal function prototypes.
 */
static int glm_dr_detach(dev_info_t *dev);
static glm_unit_t *glm_unit_init(glm_t *glm, int target, int lun);
static void glm_scsi_init_sgl(struct glm_scsi_cmd *cmd);
static void glm_dsa_dma_setup(struct scsi_pkt *pktp, struct glm_unit *unit,
	glm_t *glm);
static int glm_accept_pkt(struct glm *glm, struct glm_scsi_cmd *sp);
static int glm_prepare_pkt(struct glm *glm, struct glm_scsi_cmd *cmd);
static void glm_sg_update(glm_unit_t *unit, uchar_t index, uint32_t remain);
static uint32_t glm_sg_residual(struct glm *,
    glm_unit_t *unit, struct glm_scsi_cmd *cmd);
static void glm_queue_pkt(glm_t *glm, glm_unit_t *unit, ncmd_t *cmd);
static int glm_send_abort_msg(struct scsi_address *ap, glm_t *glm,
    glm_unit_t *unit);
static int glm_send_dev_reset_msg(struct scsi_address *ap, glm_t *glm);
static int glm_do_scsi_reset(struct scsi_address *ap, int level);
static int glm_do_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt);
static int glm_abort_cmd(struct glm *glm, struct glm_unit *unit,
    struct glm_scsi_cmd *cmd);
static void glm_chkstatus(glm_t *glm, glm_unit_t *unit,
    struct glm_scsi_cmd *cmd);
static void glm_handle_qfull(struct glm *glm, struct glm_scsi_cmd *cmd);
static void glm_restart_cmd(void *);
static void glm_remove_cmd(struct glm *glm, struct glm_unit *unit,
    struct glm_scsi_cmd *cmd);
static void glm_pollret(glm_t *glm, ncmd_t *poll_cmdp);
static void glm_flush_hba(struct glm *glm);
static void glm_flush_target(struct glm *glm, ushort_t target, uchar_t reason,
    uint_t stat);
static void glm_flush_lun(glm_t *glm, glm_unit_t *unit, uchar_t reason,
    uint_t stat);
static void glm_set_pkt_reason(struct glm *glm, struct glm_scsi_cmd *cmd,
    uchar_t reason, uint_t stat);
static void glm_mark_packets(struct glm *glm, struct glm_unit *unit,
    uchar_t reason, uint_t stat);
static void glm_flush_waitQ(struct glm *glm, struct glm_unit *unit);
static void glm_flush_tagQ(struct glm *glm, struct glm_unit *unit);
static void glm_process_intr(glm_t *glm, uchar_t istat);
static uint_t glm_decide(glm_t *glm, uint_t action);
static uint_t glm_ccb_decide(glm_t *glm, glm_unit_t *unit, uint_t action);
static int glm_wait_intr(glm_t *glm, int poll_time);

static int glm_pkt_alloc_extern(glm_t *glm, ncmd_t *cmd,
    int cmdlen, int tgtlen, int statuslen, int kf);
static void glm_pkt_destroy_extern(glm_t *glm, ncmd_t *cmd);

static void glm_watch(void *arg);
static void glm_watchsubr(struct glm *glm);
static void glm_cmd_timeout(struct glm *glm, struct glm_unit *unit);
static void glm_sync_wide_backoff(struct glm *glm, struct glm_unit *unit);
static void glm_force_renegotiation(struct glm *glm, int target);

static int glm_kmem_cache_constructor(void *buf, void *cdrarg, int kmflags);
static void glm_kmem_cache_destructor(void *buf, void *cdrarg);

static uint_t glm_intr(caddr_t arg);
static void glm_start_next(struct glm *glm);
static int glm_start_cmd(struct glm *glm, glm_unit_t *unit,
	struct glm_scsi_cmd *cmd);
static void glm_wait_for_reselect(glm_t *glm, uint_t action);
static void glm_restart_current(glm_t *glm, uint_t action);
static void glm_restart_hba(glm_t *glm, uint_t action);
static void glm_queue_target(glm_t *glm, glm_unit_t *unit);
static void glm_queue_target_lun(glm_t *glm, ushort_t target);
static glm_unit_t *glm_get_target(glm_t *glm);
static uint_t glm_check_intcode(glm_t *glm, glm_unit_t *unit, uint_t action);
static uint_t glm_parity_check(struct glm *glm, struct glm_unit *unit);
static void glm_addfq(glm_t	*glm, glm_unit_t *unit);
static void glm_addbq(glm_t	*glm, glm_unit_t *unit);
static void glm_doneq_add(glm_t *glm, ncmd_t *cmdp);
static ncmd_t *glm_doneq_rm(glm_t *glm);
static void glm_doneq_empty(glm_t *glm);
static void glm_waitq_add(glm_unit_t *unit, ncmd_t *cmdp);
static void glm_waitq_add_lifo(glm_unit_t *unit, ncmd_t *cmdp);
static ncmd_t *glm_waitq_rm(glm_unit_t *unit);
static void glm_waitq_delete(glm_unit_t *unit, ncmd_t *cmdp);

static void glm_syncio_state(glm_t *glm, glm_unit_t *unit, uchar_t state,
    uchar_t sxfer, uchar_t sscfX10);
static void glm_syncio_disable(glm_t *glm);
static void glm_syncio_reset_target(glm_t *glm, int target);
static void glm_syncio_reset(glm_t *glm, glm_unit_t *unit);
static void glm_syncio_msg_init(glm_t *glm, glm_unit_t *unit);
static int glm_syncio_enable(glm_t *glm, glm_unit_t *unit);
static int glm_syncio_respond(glm_t *glm, glm_unit_t *unit);
static uint_t glm_syncio_decide(glm_t *glm, glm_unit_t *unit, uint_t action);
static void glm_start_watch_reset_delay();
static void glm_setup_bus_reset_delay(struct glm *glm);
static void glm_watch_reset_delay(void *arg);
static int glm_watch_reset_delay_subr(struct glm *glm);

static int glm_max_sync_divisor(glm_t *glm, int syncioperiod,
    uchar_t *sxferp, uchar_t *sscfX10p);
static int glm_period_round(glm_t *glm, int syncioperiod);
static void glm_max_sync_rate_init(glm_t *glm);

static int glm_create_arq_pkt(struct glm_unit *unit, struct scsi_address *ap);
static int glm_delete_arq_pkt(struct glm_unit *unit, struct scsi_address *ap);
static void glm_complete_arq_pkt(struct scsi_pkt *pkt);
static int glm_handle_sts_chk(struct glm *glm, struct glm_unit *unit,
    struct glm_scsi_cmd *sp);

static void glm_set_throttles(struct glm *glm, int slot, int n, int what);
static void glm_set_all_lun_throttles(struct glm *glm, int target, int what);
static void glm_full_throttle(struct glm *glm, int target, int lun);

static void glm_make_wdtr(struct glm *glm, struct glm_unit *unit, uchar_t wide);
static void glm_set_wide_scntl3(struct glm *glm, struct glm_unit *unit,
    uchar_t width);

static void glm_update_props(struct glm *glm, int tgt);
static void glm_update_this_prop(struct glm *glm, char *property, int value);
static int glm_alloc_active_slots(struct glm *glm, struct glm_unit *unit,
    int flag);

static void glm_dump_cmd(struct glm *glm, struct glm_scsi_cmd *cmd);
/*PRINTFLIKE3*/
static void glm_log(struct glm *glm, int level, char *fmt, ...)
	__KPRINTFLIKE(3);
static uint_t glm_min_selection_timeout(uint_t time);
static void glm_make_ppr(struct glm *glm, struct glm_unit *unit);
static int glm_ppr_enable(glm_t *glm, glm_unit_t *unit);
#ifdef GLM_DEBUG
/*PRINTFLIKE1*/
static void glm_printf(char *fmt, ...)
	__KPRINTFLIKE(1);
#endif

static void	glm53c87x_reset(glm_t *glm);
static void	glm53c87x_init(glm_t *glm);
static void	glm53c87x_enable(glm_t *glm);
static void	glm53c87x_disable(glm_t *glm);
static uchar_t	glm53c87x_get_istat(glm_t *glm);
static void	glm53c87x_halt(glm_t *glm);
static void	glm53c87x_check_error(glm_unit_t *unit, struct scsi_pkt *pktp);
static uint_t	glm53c87x_dma_status(glm_t *glm);
static uint_t	glm53c87x_scsi_status(glm_t *glm);
static int	glm53c87x_save_byte_count(glm_t *glm, glm_unit_t *unit);
static int	glm53c87x_get_target(struct glm *glm, uchar_t *tp);
static void	glm53c87x_set_syncio(glm_t *glm, glm_unit_t *unit);
static void	glm53c87x_setup_script(glm_t *glm, glm_unit_t *unit);
static void	glm53c87x_bus_reset(glm_t *glm);

/*PRINTFLIKE5*/
static void glm_report_fault(dev_info_t *dip, glm_t *glm,
    ddi_fault_impact_t impact, ddi_fault_location_t location, char *fmt, ...)
	__KPRINTFLIKE(5);
static int glm_check_handle_status(glm_t *glm);

static const ddi_dma_attr_t glm_dma_attrs = {
	DMA_ATTR_V0,	/* attribute layout version		*/
	0x0ull,		/* address low - should be 0 (longlong)	*/
	0xffffffffull,	/* address high - 32-bit max range	*/
	0x00ffffffull,	/* count max - max DMA object size	*/
	4,		/* allocation alignment requirements	*/
	0x78,		/* burstsizes - binary encoded values	*/
	1,		/* minxfer - gran. of DMA engine	*/
	0x00ffffffull,	/* maxxfer - gran. of DMA engine	*/
	0xffffffffull,	/* max segment size (DMA boundary)	*/
	GLM_MAX_DMA_SEGS, /* scatter/gather list length		*/
	512,		/* granularity - device transfer size	*/
	0		/* flags, set to 0			*/
};

static ddi_device_acc_attr_t dev_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

static struct dev_ops glm_ops = {
	DEVO_REV,		/* devo_rev, */
	0,				/* refcnt  */
	ddi_no_info,	/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	glm_attach,		/* attach */
	glm_detach,		/* detach */
	nodev,			/* reset */
	NULL,			/* driver operations */
	NULL,			/* bus operations */
	glm_power,		/* power management */
	ddi_quiesce_not_supported,	/* devo_quiesce */
};

char _depends_on[] = "misc/scsi";

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module. This one is a driver */
	"GLM SCSI HBA Driver",   /* Name of the module. */
	&glm_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

/*
 * Local static data
 */
#if defined(GLM_DEBUG)
static uint32_t	glm_debug_flags = 0x0;
#endif	/* defined(GLM_DEBUG) */

static kmutex_t 	glm_global_mutex;
static void		*glm_state;		/* soft	state ptr */
static krwlock_t	glm_global_rwlock;

static kmutex_t		glm_log_mutex;
static char		glm_log_buf[256];
_NOTE(MUTEX_PROTECTS_DATA(glm_log_mutex, glm_log_buf))

static struct glm *glm_head, *glm_tail;
static clock_t glm_scsi_watchdog_tick;
static clock_t glm_tick;
static timeout_id_t glm_reset_watch;
static timeout_id_t glm_timeout_id;
static int glm_timeouts_enabled = 0;

/*
 * tunables
 */
static uchar_t	glm_default_offset = GLM_875_OFFSET;
static uint_t	glm_selection_timeout = 0;

/*
 * Include the output of the NASM program. NASM is a program
 * which takes the scr.ss file and turns it into a series of
 * C data arrays and initializers.
 */
#include <sys/scsi/adapters/scr.out>

static size_t glm_script_size = sizeof (SCRIPT);

/*
 * warlock directives
 */
_NOTE(SCHEME_PROTECTS_DATA("unique per pkt", scsi_pkt \
	glm_scsi_cmd NcrTableIndirect buf scsi_cdb scsi_status))
_NOTE(SCHEME_PROTECTS_DATA("stable data", scsi_device scsi_address))

#ifdef GLM_DEBUG
#define	GLM_TEST
static int glm_no_sync_wide_backoff;
static int glm_test_arq_enable, glm_test_arq;
static int glm_rtest, glm_rtest_type;
static int glm_atest, glm_atest_type;
static int glm_ptest;
static int glm_test_stop;
static int glm_test_instance;
static int glm_test_untagged;
static int glm_enable_untagged;
static int glm_test_timeouts;
static int glm_test_rec;
static ushort_t glm_test_rec_mask;

void debug_enter(char *);
static void glm_test_reset(struct glm *glm, struct glm_unit *unit);
static void glm_test_abort(struct glm *glm, struct glm_unit *unit);
static int glm_hbaq_check(struct glm *glm, struct glm_unit *unit);
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

	status = ddi_soft_state_init(&glm_state, sizeof (struct glm),
	    GLM_INITIAL_SOFT_SPACE);
	if (status != 0) {
		return (status);
	}

	if ((status = scsi_hba_init(&modlinkage)) != 0) {
		ddi_soft_state_fini(&glm_state);
		return (status);
	}

	mutex_init(&glm_global_mutex, NULL, MUTEX_DRIVER, NULL);
	rw_init(&glm_global_rwlock, NULL, RW_DRIVER, NULL);
	mutex_init(&glm_log_mutex, NULL, MUTEX_DRIVER, NULL);

	if ((status = mod_install(&modlinkage)) != 0) {
		mutex_destroy(&glm_log_mutex);
		rw_destroy(&glm_global_rwlock);
		mutex_destroy(&glm_global_mutex);
		ddi_soft_state_fini(&glm_state);
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
		ddi_soft_state_fini(&glm_state);
		scsi_hba_fini(&modlinkage);
		mutex_destroy(&glm_global_mutex);
		rw_destroy(&glm_global_rwlock);
		mutex_destroy(&glm_log_mutex);
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
	NDBG0(("glm _info"));

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
glm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	glm_t			*glm = NULL;
	char			*prop_template = "target%d-scsi-options";
	char			prop_str[32];
	int			instance, i, id;
	char			buf[64];
	char			intr_added = 0;
	char			script_alloc = 0;
	char			map_setup = 0;
	char			config_setup = 0;
	char			hba_init = 0;
	char			hba_attach_setup = 0;
	char			mutex_init_done = 0;
	scsi_hba_tran_t		*hba_tran;
	char			pmc_name[16];
	char			*pmc[] = {
					NULL,
					"0=Off (PCI D3 State)",
					"1=Coma(PCI D2 State)",
					"3=On (PCI D0 State)",
					NULL
				};

	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		if ((hba_tran = ddi_get_driver_private(dip)) == NULL)
			return (DDI_FAILURE);

		glm = TRAN2GLM(hba_tran);

		if (!glm) {
			return (DDI_FAILURE);
		}

		/*
		 * Restart watch thread
		 */
		mutex_enter(&glm_global_mutex);
		if (glm_timeout_id == 0) {
			glm_timeout_id = timeout(glm_watch, NULL, glm_tick);
			glm_timeouts_enabled = 1;
		}
		mutex_exit(&glm_global_mutex);

		/*
		 * Reset hardware and softc to "no outstanding commands"
		 * Note	that a check condition can result on first command
		 * to a	target.
		 */
		mutex_enter(&glm->g_mutex);

		/*
		 * g_reset_delay indicates the time period until which
		 * commands to the controller needs to be delayed.
		 * The timeout routine glm_watch_reset_delay decrements the
		 * g_reset_delay at fixed intervals and eventually g_reset_delay
		 * is set to 0.
		 * However if after g_reset_delay is set and the driver is
		 * suspended then the timeout routine is stopped and
		 * g_reset_delay is not cleared. This causes a hang when the
		 * suspended instance is resumed. There is no timeout routine
		 * that will decrement the g_reset-delay.
		 */
		for (i = 0; i < NTARGETS_WIDE; i++) {
			glm->g_reset_delay[i] = 0;
			glm_set_all_lun_throttles(glm, i, MAX_THROTTLE);
		}

		/*
		 * raise power.
		 */
		if (glm->g_options & GLM_OPT_PM) {
			int rval;
			mutex_exit(&glm->g_mutex);
			(void) pm_busy_component(dip, 0);
			rval = pm_raise_power(dip, 0, PM_LEVEL_D0);
			ASSERT(rval == DDI_SUCCESS);
			mutex_enter(&glm->g_mutex);
		} else {
			/*
			 * reset/init the chip and enable the
			 * interrupts and the interrupt handler
			 */
			GLM_RESET(glm);
			GLM_INIT(glm);
			GLM_ENABLE_INTR(glm);
		}

		if (glm_script_alloc(glm) != DDI_SUCCESS) {
			mutex_exit(&glm->g_mutex);
			if (glm->g_options & GLM_OPT_PM) {
				(void) pm_idle_component(dip, 0);
			}
			return (DDI_FAILURE);
		}

		glm->g_suspended = 0;

		glm_syncio_reset(glm, NULL);
		glm->g_wide_enabled = glm->g_wide_known = 0;
		glm->g_ppr_known = 0;

		/* start requests, if possible */
		glm_restart_hba(glm, 0);

		mutex_exit(&glm->g_mutex);

		/* report idle status to pm framework */
		if (glm->g_options & GLM_OPT_PM) {
			(void) pm_idle_component(dip, 0);
		}

		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);

	}

	instance = ddi_get_instance(dip);

	if (ddi_intr_hilevel(dip, 0)) {
		/*
		 * Interrupt number '0' is a high-level interrupt.
		 * At this point you either add a special interrupt
		 * handler that triggers a soft interrupt at a lower level,
		 * or - more simply and appropriately here - you just
		 * fail the attach.
		 */
		glm_log(NULL, CE_WARN,
		    "glm%d: Device is using a hilevel intr", instance);
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.attach.4001]");
		goto fail;
	}

	/*
	 * Allocate softc information.
	 */
	if (ddi_soft_state_zalloc(glm_state, instance) != DDI_SUCCESS) {
		glm_log(NULL, CE_WARN,
		    "glm%d: cannot allocate soft state", instance);
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.attach.4002]");
		goto fail;
	}

	glm = ddi_get_soft_state(glm_state, instance);

	if (glm == NULL) {
		glm_log(NULL, CE_WARN,
		    "glm%d: cannot get soft state", instance);
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.attach.4003]");
		goto fail;
	}

	/* Allocate a transport structure */
	hba_tran = glm->g_tran = scsi_hba_tran_alloc(dip, SCSI_HBA_CANSLEEP);
	ASSERT(glm->g_tran != NULL);

	/* Indicate that we are 'sizeof (scsi_*(9S))' clean. */
	scsi_size_clean(dip);		/* SCSI_SIZE_CLEAN_VERIFY ok */

	glm->g_dip = dip;
	glm->g_instance = instance;

	/*
	 * set host ID
	 */
	glm->g_glmid = DEFAULT_HOSTID;
	id = ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0, "initiator-id", -1);
	if (id == -1) {
		id = ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0,
		    "scsi-initiator-id", -1);
	}
	if (id != DEFAULT_HOSTID && id >= 0 && id < NTARGETS_WIDE) {
		glm_log(glm, CE_NOTE, "?initiator SCSI ID now %d\n", id);
		glm->g_glmid = (uchar_t)id;
	}

	/*
	 * map in the GLM's operating registers.
	 */
	if (ddi_regs_map_setup(dip, MEM_SPACE, &glm->g_devaddr,
	    0, 0, &dev_attr, &glm->g_datap) != DDI_SUCCESS) {
		glm_log(glm, CE_WARN, "map setup failed");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.attach.4005]");
		goto fail;
	}
	map_setup++;

	if (pci_config_setup(glm->g_dip,
	    &glm->g_config_handle) != DDI_SUCCESS) {
		glm_log(glm, CE_WARN, "cannot map configuration space.");
		goto fail;
	}
	config_setup++;


	/*
	 * Setup configuration space
	 */
	if (glm_config_space_init(glm) == FALSE) {
		/* no special clean up required for this function */
		glm_log(glm, CE_WARN, "glm_config_space_init failed");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.attach.4004]");
		goto fail;
	}

	if (glm_hba_init(glm) == DDI_FAILURE) {
		glm_log(glm, CE_WARN, "glm_hba_init failed");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.attach.4006]");
		goto fail;
	}
	hba_init++;

	/*
	 * could have outstanding interrupt  - must
	 * clear before we do get_iblock_cookie (implicit ddi_add_intr)
	 */
	GLM_RESET(glm);

	if (glm_script_alloc(glm) != DDI_SUCCESS) {
		glm_log(glm, CE_WARN, "glm_script_alloc failed");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.attach.4007]");
		goto fail;
	}
	script_alloc++;

	/*
	 * Get iblock_cookie to initialize mutexes used in the
	 * interrupt handler.
	 */
	if (ddi_get_iblock_cookie(dip, 0, &glm->g_iblock) != DDI_SUCCESS) {
		glm_log(glm, CE_WARN, "get iblock cookie failed");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.attach.4008]");
		goto fail;
	}

	mutex_init(&glm->g_mutex, NULL, MUTEX_DRIVER, glm->g_iblock);
	cv_init(&glm->g_cv, NULL, CV_DRIVER, NULL);
	mutex_init_done++;

	/*
	 * Now register the interrupt handler.
	 */
	if (ddi_add_intr(dip, 0, &glm->g_iblock,
	    (ddi_idevice_cookie_t *)0, glm_intr, (caddr_t)glm)) {
		glm_log(glm, CE_WARN, "adding interrupt failed");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.attach.4009]");
		goto fail;
	}
	intr_added++;

	/*
	 * initialize SCSI HBA transport structure
	 */
	hba_tran->tran_hba_private	= glm;
	hba_tran->tran_tgt_private	= NULL;

	hba_tran->tran_tgt_init		= glm_scsi_tgt_init;
	hba_tran->tran_tgt_probe	= glm_scsi_tgt_probe;
	hba_tran->tran_tgt_free		= glm_scsi_tgt_free;

	hba_tran->tran_start		= glm_scsi_start;
	hba_tran->tran_reset		= glm_scsi_reset;
	hba_tran->tran_abort		= glm_scsi_abort;
	hba_tran->tran_getcap		= glm_scsi_getcap;
	hba_tran->tran_setcap		= glm_scsi_setcap;
	hba_tran->tran_init_pkt		= glm_scsi_init_pkt;
	hba_tran->tran_destroy_pkt	= glm_scsi_destroy_pkt;

	hba_tran->tran_dmafree		= glm_scsi_dmafree;
	hba_tran->tran_sync_pkt		= glm_scsi_sync_pkt;
	hba_tran->tran_reset_notify	= glm_scsi_reset_notify;
	hba_tran->tran_get_bus_addr	= NULL;
	hba_tran->tran_get_name		= NULL;

	hba_tran->tran_quiesce		= glm_scsi_quiesce;
	hba_tran->tran_unquiesce	= glm_scsi_unquiesce;
	hba_tran->tran_bus_reset	= NULL;

	hba_tran->tran_add_eventcall	= NULL;
	hba_tran->tran_get_eventcookie	= NULL;
	hba_tran->tran_post_event	= NULL;
	hba_tran->tran_remove_eventcall	= NULL;

	if (scsi_hba_attach_setup(dip, &glm->g_hba_dma_attrs,
	    hba_tran, 0) != DDI_SUCCESS) {
		glm_log(glm, CE_WARN, "hba attach setup failed");
		goto fail;
	}
	hba_attach_setup++;

	glm->g_ppr_enabled = 0;
	glm->g_ppr_supported = ALL_TARGETS;
	glm->g_ppr_known = 0;
	glm->g_ppr_sent = 0;

	/*
	 * disable wide for all targets
	 * (will be enabled by target driver if required)
	 * sync is enabled by default
	 */
	glm->g_nowide = glm->g_notag = ALL_TARGETS;
	glm->g_force_narrow = ALL_TARGETS;

	/*
	 * initialize the qfull retry counts
	 */
	for (i = 0; i < NTARGETS_WIDE; i++) {
		glm->g_qfull_retries[i] = QFULL_RETRIES;
		glm->g_qfull_retry_interval[i] =
		    drv_usectohz(QFULL_RETRY_INTERVAL * 1000);
	}

	/*
	 * create kmem cache for packets
	 */
	(void) sprintf(buf, "glm%d_cache", instance);
	glm->g_kmem_cache = kmem_cache_create(buf,
	    sizeof (struct glm_scsi_cmd) + scsi_pkt_size(), 8,
	    glm_kmem_cache_constructor, glm_kmem_cache_destructor,
	    NULL, (void *)glm, NULL, 0);

	if (glm->g_kmem_cache == NULL) {
		glm_log(glm, CE_WARN, "creating kmem cache failed");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.attach.4010]");
		goto fail;
	}

	/*
	 * if scsi-options property exists, use it.
	 */
	glm->g_scsi_options = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, 0, "scsi-options", DEFAULT_SCSI_OPTIONS);

	if ((glm->g_scsi_options & SCSI_OPTIONS_SYNC) == 0) {
		glm_syncio_disable(glm);
	}

	if ((glm->g_scsi_options & SCSI_OPTIONS_WIDE) == 0) {
		glm->g_nowide = ALL_TARGETS;
	}

	/*
	 * if target<n>-scsi-options property exists, use it;
	 * otherwise use the g_scsi_options
	 */
	for (i = 0; i < NTARGETS_WIDE; i++) {
		(void) sprintf(prop_str, prop_template, i);
		glm->g_target_scsi_options[i] = ddi_prop_get_int(
		    DDI_DEV_T_ANY, dip, 0, prop_str, -1);

		if (glm->g_target_scsi_options[i] != -1) {
			glm_log(glm, CE_NOTE, "?target%x-scsi-options=0x%x\n",
			    i, glm->g_target_scsi_options[i]);
			glm->g_target_scsi_options_defined |= (1 << i);
		} else {
			glm->g_target_scsi_options[i] = glm->g_scsi_options;
		}
		if (((glm->g_target_scsi_options[i] &
		    SCSI_OPTIONS_DR) == 0) &&
		    (glm->g_target_scsi_options[i] & SCSI_OPTIONS_TAG)) {
			glm->g_target_scsi_options[i] &= ~SCSI_OPTIONS_TAG;
			glm_log(glm, CE_WARN,
			    "Target %d: disabled TQ since disconnects "
			    "are disabled", i);
		}

		if (glm->g_devid == GLM_53c810) {
			glm->g_target_scsi_options[i] &= ~SCSI_OPTIONS_WIDE;
		}

		if ((glm->g_devid == GLM_53c1010_33) ||
		    (glm->g_devid == GLM_53c1010_66)) {
			/*
			 * If SCSI cable is not LVD, don't enable PPR
			 * negotiation. Some target get wedged if it
			 * does not understand PPR protocol.
			 */
			(void) glm_check_smode(glm);
			if (glm->g_options & GLM_OPT_LVD)
				glm->g_ppr_enabled = ALL_TARGETS;
			else
				glm->g_ppr_enabled = 0;

			if ((glm->g_target_scsi_options[i] &
			    SCSI_OPTIONS_WIDE) == 0) {
				glm->g_target_scsi_options[i] &=
				    ~SCSI_OPTIONS_FAST80;
			}
		}

		/*
		 * by default, we support 8 LUNS per target.
		 */
		glm->g_max_lun[i] = NLUNS_PER_TARGET;
	}

	glm->g_scsi_tag_age_limit =
	    ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0, "scsi-tag-age-limit",
	    DEFAULT_TAG_AGE_LIMIT);

	glm->g_scsi_reset_delay	= ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, 0, "scsi-reset-delay",	SCSI_DEFAULT_RESET_DELAY);
	if (glm->g_scsi_reset_delay == 0) {
		glm_log(glm, CE_NOTE,
		    "scsi_reset_delay of 0 is not recommended,"
		    " resetting to SCSI_DEFAULT_RESET_DELAY\n");
		glm->g_scsi_reset_delay = SCSI_DEFAULT_RESET_DELAY;
	}

	glm_selection_timeout =
	    ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "scsi-selection-timeout",
	    SCSI_DEFAULT_SELECTION_TIMEOUT);
	glm_selection_timeout =
	    glm_min_selection_timeout(glm_selection_timeout);

	/*
	 * at this point, we are not going to fail the attach
	 *
	 * used for glm_watch
	 */
	rw_enter(&glm_global_rwlock, RW_WRITER);
	if (glm_head == NULL) {
		glm_head = glm;
	} else {
		glm_tail->g_next = glm;
	}
	glm_tail = glm;
	rw_exit(&glm_global_rwlock);

	mutex_enter(&glm_global_mutex);
	if (glm_scsi_watchdog_tick == 0) {
		glm_scsi_watchdog_tick = ddi_prop_get_int(DDI_DEV_T_ANY,
		    dip, 0, "scsi-watchdog-tick", DEFAULT_WD_TICK);

		glm_tick = drv_usectohz((clock_t)
		    glm_scsi_watchdog_tick * 1000000);

		glm_timeout_id = timeout(glm_watch, NULL, glm_tick);
		glm_timeouts_enabled = 1;
	}
	mutex_exit(&glm_global_mutex);

	/*
	 * If power management is support by this chip, create
	 * pm-components property for the power management framework
	 */
	if (glm->g_options & GLM_OPT_PM) {
		int power =
		    pci_config_get16(glm->g_config_handle, GLM_PM_PMC);
		(void) sprintf(pmc_name, "NAME=glm%d", instance);
		pmc[0] = pmc_name;
		if ((power & D2SUPPORT) == 0) {
			pmc[2] = pmc[3];
			pmc[3] = NULL;
		}
		if (ddi_prop_update_string_array(DDI_DEV_T_NONE, dip,
		    "pm-components", pmc, (power & D2SUPPORT) ? 4: 3)
		    != DDI_PROP_SUCCESS) {
				glm->g_options &= ~GLM_OPT_PM;
				glm_log(glm, CE_WARN,
				"glm%d: pm-component property creation failed.",
				    glm->g_instance);
		}
	}

	/*
	 * initialize the chip.
	 */
	GLM_INIT(glm);
	/*
	 * We need to set the power level before we call GLM_BUS_RESET.
	 * GLM_BUS_RESET calls glm53c87x_reset which enables interrupts.
	 * If power level is not set properly, we do not process the
	 * interrupts that we get immediatedly after we call GLM_BUS_RESET.
	 * This results is interrupts getting blocked, because the pci nexus
	 * driver blocks interrupts that are not claimed.
	 * Check and report power level.  -1 is unsupported.
	 */
	if (glm->g_options & GLM_OPT_PM) {
		int power =
		    pci_config_get16(glm->g_config_handle, GLM_PM_CSR);

		switch (power) {
		case PCI_PMCSR_D0:
			glm->g_power_level = PM_LEVEL_D0;
			(void) pm_busy_component(dip, 0);
			(void) pm_raise_power(dip, 0, PM_LEVEL_D0);
			break;
		case PCI_PMCSR_D2:
			glm->g_power_level = PM_LEVEL_D2;
			(void) pm_busy_component(dip, 0);
			(void) pm_raise_power(dip, 0, PM_LEVEL_D2);
			break;
		case PCI_PMCSR_D3HOT:
			glm->g_power_level = PM_LEVEL_D3;
			(void) pm_busy_component(dip, 0);
			(void) pm_raise_power(dip, 0, PM_LEVEL_D3);
			break;
		default:
			glm->g_options &= ~GLM_OPT_PM;
			(void) ddi_prop_remove(DDI_DEV_T_NONE,
			    dip, "pm-components");
			glm_log(glm, CE_WARN,
			    "glm%d: Can't determine power.", glm->g_instance);
			break;
		}
	}

	/*
	 * check to see if the fcode property "clock-frequency"
	 * is available.  If not, this chip/board does not have
	 * fcode, reset the SCSI bus.
	 */
	i = ddi_prop_get_int(DDI_DEV_T_ANY, glm->g_dip,
	    DDI_PROP_DONTPASS, "clock-frequency", -1);
	if (i == -1) {
		GLM_BUS_RESET(glm);
		glm_set_throttles(glm, 0, N_GLM_UNITS, HOLD_THROTTLE);
		for (i = 0; i < NTARGETS_WIDE; i++) {
			glm->g_reset_delay[i] = glm->g_scsi_reset_delay;
		}
			glm_start_watch_reset_delay();
	}

	/* check everything OK before returning */
	if (glm_check_handle_status(glm) != DDI_SUCCESS) {
		/*
		 * Disable interrupts which could have been enabled
		 * because of GLM_BUS_RESET call above.
		 */
		GLM_DISABLE_INTR(glm);
		glm_report_fault(dip, glm, DDI_SERVICE_LOST, DDI_DATAPATH_FAULT,
		    "attach - data path failed");
		(void) glm_dr_detach(dip);
		if (glm->g_options & GLM_OPT_PM) {
			(void) pm_idle_component(dip, 0);
		}
		return (DDI_FAILURE);
	}
	/* enable the interrupts and the interrupt handler */
	GLM_ENABLE_INTR(glm);

	/* report idle status to pm framework */
	if (glm->g_options & GLM_OPT_PM) {
		(void) pm_idle_component(dip, 0);
	}

	/* Print message of HBA present */
	ddi_report_dev(dip);

	return (DDI_SUCCESS);

fail:
	glm_log(glm, CE_WARN, "attach failed");
	cmn_err(CE_WARN, "!ID[SUNWpd.glm.attach.4011]");
	if (glm) {

		/* deallocate in reverse order */
		if (glm->g_kmem_cache) {
			kmem_cache_destroy(glm->g_kmem_cache);
		}
		if (hba_attach_setup) {
			(void) scsi_hba_detach(dip);
		}
		if (intr_added) {
			ddi_remove_intr(dip, 0, glm->g_iblock);
		}
		if (mutex_init_done) {
			mutex_destroy(&glm->g_mutex);
			cv_destroy(&glm->g_cv);
		}
		if (hba_init) {
			glm_hba_fini(glm);
		}
		if (script_alloc) {
			glm_script_free(glm);
		}
		if (map_setup) {
			glm_cfg_fini(glm);
		}
		if (config_setup) {
			pci_config_teardown(&glm->g_config_handle);
		}
		if (glm->g_tran) {
			scsi_hba_tran_free(glm->g_tran);
		}
		ddi_soft_state_free(glm_state, instance);
		ddi_prop_remove_all(dip);
	}
	return (DDI_FAILURE);
}

/*
 * detach(9E).	Remove all device allocations and system resources;
 * disable device interrupts.
 * Return DDI_SUCCESS if done; DDI_FAILURE if there's a problem.
 */
static int
glm_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	glm_t	*glm, *g;
	scsi_hba_tran_t *tran;

	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);
	NDBG0(("glm_detach: dip=0x%p cmd=0x%x", (void *)devi, cmd));

	switch (cmd) {
	case DDI_DETACH:
		return (glm_dr_detach(devi));

	case DDI_SUSPEND:
		if ((tran = ddi_get_driver_private(devi)) == NULL)
			return (DDI_SUCCESS);

		glm = TRAN2GLM(tran);
		if (!glm) {
			return (DDI_SUCCESS);
		}

		mutex_enter(&glm->g_mutex);

		if (glm->g_suspended++) {
			mutex_exit(&glm->g_mutex);
			return (DDI_SUCCESS);
		}

		/*
		 * Cancel timeout threads for this glm
		 */
		if (glm->g_quiesce_timeid) {
			timeout_id_t tid = glm->g_quiesce_timeid;
			glm->g_quiesce_timeid = 0;
			mutex_exit(&glm->g_mutex);
			(void) untimeout(tid);
			mutex_enter(&glm->g_mutex);
		}

		if (glm->g_restart_cmd_timeid) {
			timeout_id_t tid = glm->g_restart_cmd_timeid;
			glm->g_restart_cmd_timeid = 0;
			mutex_exit(&glm->g_mutex);
			(void) untimeout(tid);
			mutex_enter(&glm->g_mutex);
		}
		mutex_exit(&glm->g_mutex);

		/*
		 * Cancel watch threads if all glms suspended
		 */
		rw_enter(&glm_global_rwlock, RW_WRITER);
		for (g = glm_head; g != NULL; g = g->g_next) {
			if (!g->g_suspended)
				break;
		}
		rw_exit(&glm_global_rwlock);

		mutex_enter(&glm_global_mutex);
		if (g == NULL) {
			timeout_id_t tid;

			glm_timeouts_enabled = 0;
			if (glm_timeout_id) {
				tid = glm_timeout_id;
				glm_timeout_id = 0;
				mutex_exit(&glm_global_mutex);
				(void) untimeout(tid);
				mutex_enter(&glm_global_mutex);
			}
			if (glm_reset_watch) {
				tid = glm_reset_watch;
				glm_reset_watch = 0;
				mutex_exit(&glm_global_mutex);
				(void) untimeout(tid);
				mutex_enter(&glm_global_mutex);
			}
		}
		mutex_exit(&glm_global_mutex);

		mutex_enter(&glm->g_mutex);

		/*
		 * If this chip is using memory scripts,
		 * free them now.
		 */
		glm_script_free(glm);

		/*
		 * If this glm is not in full power(PM_LEVEL_D0), just return.
		 */
		if ((glm->g_options & GLM_OPT_PM) &&
		    (glm->g_power_level != PM_LEVEL_D0)) {
			mutex_exit(&glm->g_mutex);
			return (DDI_SUCCESS);
		}


		/* Set all flags so that renegotiation happens */
		glm->g_ppr_known = 0;
		glm->g_wide_known = glm->g_wide_enabled = 0;
		glm_syncio_reset(glm, NULL);

		/* Disable HBA interrupts in hardware */
		GLM_DISABLE_INTR(glm);

		mutex_exit(&glm->g_mutex);

		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
	/* NOTREACHED */
}


static int
glm_dr_detach(dev_info_t *dip)
{
	struct glm *glm, *g;
	scsi_hba_tran_t *tran;
	struct glm_unit *unit;
	int i, j;

	NDBG0(("glm_dr_detach: dip=0x%p", (void *)dip));

	if ((tran = ddi_get_driver_private(dip)) == NULL)
		return (DDI_FAILURE);

	glm = TRAN2GLM(tran);
	if (!glm) {
		return (DDI_FAILURE);
	}

	/* Make sure power level is D0 before accessing registers */
	if (glm->g_options & GLM_OPT_PM) {
		if (glm->g_power_level != PM_LEVEL_D0) {
			if (pm_raise_power(dip, 0, PM_LEVEL_D0) !=
			    DDI_SUCCESS) {
				glm_log(glm, CE_WARN,
				    "glm%d: Raise power request failed.",
				    glm->g_instance);
				return (DDI_FAILURE);
			}
		}
	}

	mutex_enter(&glm->g_mutex);
	GLM_BUS_RESET(glm);
	GLM_DISABLE_INTR(glm);
	mutex_exit(&glm->g_mutex);

	ddi_remove_intr(dip, (uint_t)0, glm->g_iblock);

	scsi_hba_reset_notify_tear_down(glm->g_reset_notify_listf);

	/*
	 * Remove device instance from the global linked list
	 */
	rw_enter(&glm_global_rwlock, RW_WRITER);
	if (glm_head == glm) {
		g = glm_head = glm->g_next;
	} else {
		for (g = glm_head; g != NULL; g = g->g_next) {
			if (g->g_next == glm) {
				g->g_next = glm->g_next;
				break;
			}
		}
		if (g == NULL) {
			cmn_err(CE_WARN, "!ID[SUNWpd.glm.detach.4012]");
			glm_log(glm, CE_PANIC, "Not in softc list!");
		}
	}

	if (glm_tail == glm) {
		glm_tail = g;
	}
	rw_exit(&glm_global_rwlock);

	/*
	 * Cancel timeout threads for this glm
	 */
	mutex_enter(&glm->g_mutex);
	if (glm->g_quiesce_timeid) {
		timeout_id_t tid = glm->g_quiesce_timeid;
		glm->g_quiesce_timeid = 0;
		mutex_exit(&glm->g_mutex);
		(void) untimeout(tid);
		mutex_enter(&glm->g_mutex);
	}

	if (glm->g_restart_cmd_timeid) {
		timeout_id_t tid = glm->g_restart_cmd_timeid;
		glm->g_restart_cmd_timeid = 0;
		mutex_exit(&glm->g_mutex);
		(void) untimeout(tid);
		mutex_enter(&glm->g_mutex);
	}
	mutex_exit(&glm->g_mutex);

	/*
	 * last glm? ... if active, CANCEL watch threads.
	 */
	mutex_enter(&glm_global_mutex);
	if (glm_head == NULL) {
		timeout_id_t tid;

		glm_timeouts_enabled = 0;
		if (glm_timeout_id) {
			tid = glm_timeout_id;
			glm_timeout_id = 0;
			mutex_exit(&glm_global_mutex);
			(void) untimeout(tid);
			mutex_enter(&glm_global_mutex);
		}
		if (glm_reset_watch) {
			tid = glm_reset_watch;
			glm_reset_watch = 0;
			mutex_exit(&glm_global_mutex);
			(void) untimeout(tid);
			mutex_enter(&glm_global_mutex);
		}

		/*
		 * Clear glm_scsi_watchdog_tick so that the watch thread
		 * gets restarted on DDI_ATTACH
		 */
		glm_scsi_watchdog_tick = 0;
	}
	mutex_exit(&glm_global_mutex);

	/*
	 * Delete ARQ pkt, nt_active, unit and DSA structures.
	 */
	for (i = 0; i < NTARGETS_WIDE; i++) {
		for (j = 0; j < NLUNS_GLM; j++) {
			if ((unit = NTL2UNITP(glm, i, j)) != NULL) {
				struct scsi_address sa;
				struct nt_slots *active = unit->nt_active;
				sa.a_hba_tran = NULL;
				sa.a_target = (ushort_t)i;
				sa.a_lun = (uchar_t)j;
				(void) glm_delete_arq_pkt(unit, &sa);
				if (active) {
					kmem_free(active, active->nt_size);
					unit->nt_active = NULL;
				}
				glm_tear_down_unit_dsa(glm, i, j);
			}
		}
	}

	/* deallocate everything that was allocated in glm_attach */
	kmem_cache_destroy(glm->g_kmem_cache);
	(void) scsi_hba_detach(dip);
	glm_hba_fini(glm);
	glm_script_free(glm);
	glm_cfg_fini(glm);

	/* Lower the power informing PM Framework */
	if (glm->g_options & GLM_OPT_PM) {
		if (pm_lower_power(dip, 0, PM_LEVEL_D3) != DDI_SUCCESS)
			glm_log(glm, CE_WARN,
			    "glm%d: Lower power request failed, ignoring.",
			    glm->g_instance);
	}

	pci_config_teardown(&glm->g_config_handle);
	scsi_hba_tran_free(glm->g_tran);
	mutex_destroy(&glm->g_mutex);
	cv_destroy(&glm->g_cv);
	ddi_soft_state_free(glm_state, ddi_get_instance(dip));
	ddi_prop_remove_all(dip);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
glm_power(dev_info_t *dip, int component, int level)
{
	struct glm *glm;
	int rval = DDI_SUCCESS;

	glm = ddi_get_soft_state(glm_state, ddi_get_instance(dip));
	if (glm == NULL) {
		return (DDI_FAILURE);
	}

	mutex_enter(&glm->g_mutex);
	switch (level) {
	case PM_LEVEL_D0:
		NDBG11(("glm%d: turning power ON.", glm->g_instance));
		/*
		 * Check what power level we are from. If it is D2 then
		 * we don't have to reinitilize chip. Just Enabling
		 * PCI Config Command register would do
		 */
		if (glm->g_power_level == PM_LEVEL_D2) {
			GLM_POWER_FROM_COMA(glm);
		} else {
			GLM_POWER_ON(glm);

			/*
			 * If device is attaching then do not enable
			 * interrupt here.  Attach will enable interrupt
			 * when it is ready.
			 */
			if (!DEVI_IS_ATTACHING(dip))
				GLM_ENABLE_INTR(glm);
		}
		break;
	case PM_LEVEL_D2:
		NDBG11(("glm%d: Coma State", glm->g_instance));
		GLM_POWER_COMA(glm);
		break;
	case PM_LEVEL_D3:
		NDBG11(("glm%d: turning power OFF.", glm->g_instance));
		GLM_POWER_OFF(glm);
		break;
	default:
		glm_log(glm, CE_WARN, "glm%d: unknown power level <%x>.\n",
		    glm->g_instance, level);
		rval = DDI_FAILURE;
		break;
	}
	mutex_exit(&glm->g_mutex);
	return (rval);
}

/*
 * Initialize configuration space and figure out which
 * chip and revision of the chip the glm driver is using.
 */
static int
glm_config_space_init(struct glm *glm)
{
	ushort_t caps_ptr, cap, cap_count;

	NDBG0(("glm_config_space_init"));



	/*
	 * First Check if capabilities list is supported and if so,
	 * get initial capabilities pointer and clear bits 0,1.
	 */
	if (pci_config_get16(glm->g_config_handle, PCI_CONF_STAT)
	    & PCI_STAT_CAP)
		caps_ptr = P2ALIGN(pci_config_get8(glm->g_config_handle,
		    PCI_CONF_CAP_PTR), 4);
	else
		caps_ptr = PCI_CAP_NEXT_PTR_NULL;

	/*
	 * Walk capabilities if supported.
	 */
	for (cap_count = 0; caps_ptr != PCI_CAP_NEXT_PTR_NULL; ) {

		/*
		 * Check that we haven't exceeded the maximum number of
		 * capabilities and that the pointer is in a valid range.
		 */
		if (++cap_count > 48) {
			glm_log(glm, CE_WARN,
			    "too many device capabilities.\n");
			return (FALSE);
		}
		if (caps_ptr < 64) {
			glm_log(glm, CE_WARN,
			    "capabilities pointer 0x%x out of range.\n",
			    caps_ptr);
			return (FALSE);
		}

		/*
		 * Get next capability and check that it is valid.
		 * For now, we only support power management.
		 */
		cap = pci_config_get8(glm->g_config_handle, caps_ptr);
		switch (cap) {
			case PCI_CAP_ID_PM:
				glm_log(glm, CE_NOTE,
				    "?glm%d supports power management.\n",
				    glm->g_instance);
				glm->g_options |= GLM_OPT_PM;
				break;
			default:
				glm_log(glm, CE_NOTE,
				    "?glm%d unrecognized capability "
				    "0x%x.\n", glm->g_instance, cap);
				break;
		}

		/*
		 * Get next capabilities pointer and clear bits 0,1.
		 */
		caps_ptr = P2ALIGN(pci_config_get8(glm->g_config_handle,
		    (caps_ptr + PCI_CAP_NEXT_PTR)), 4);
	}

	if (glm->g_options & GLM_OPT_PM) {
		/*
		 * Enable the power to D0 - So that all access to
		 * other register can work.
		 */
		pci_config_put16(glm->g_config_handle, GLM_PM_CSR,
		    PCI_PMCSR_D0);
		drv_usecwait(10000);
	}

	glm_setup_cmd_reg(glm);

	/*
	 * Get the chip device id:
	 *	1 - 53c810
	 *	f - 53c875
	 */
	glm->g_devid = pci_config_get16(glm->g_config_handle, PCI_CONF_DEVID);


	/*
	 * Get the chip revision.
	 * The driver still needs to get the revid in config space to
	 * determine if this chip is an 810 or 810a.  Only 810a chips
	 * are support by this driver.
	 *
	 * Save the revision in the lower nibble, and the chip type
	 * in the high nibble.
	 */
	glm->g_revid = ddi_get8(glm->g_datap,
	    (uint8_t *)(glm->g_devaddr + NREG_CTEST3));
	glm->g_revid >>= 4;
	glm->g_revid |=
	    (pci_config_get8(glm->g_config_handle, PCI_CONF_REVID) & 0xf0);

	/*
	 * Each chip has different capabilities, disable certain
	 * features depending on which chip is found.
	 */
	if ((glm->g_devid == GLM_53c810) && IS_810A(glm)) {
		glm->g_sync_offset = SYNC_OFFSET(glm);
		glm->g_max_div = 4;
		glm_log(glm, CE_NOTE, "?Rev. %d Symbios 53c810A found.\n",
		    GLM_REV(glm));
	} else if (glm->g_devid == GLM_53c875) {
		glm->g_options |= (GLM_OPT_WIDE_BUS | GLM_OPT_ULTRA |
		    GLM_OPT_ONBOARD_RAM | GLM_OPT_LARGE_FIFO);
		glm->g_sync_offset = glm_default_offset;
		glm->g_max_div = 5;

		/*
		 * Not all version of the 875 have the PLL.  Early versions
		 * had SCLK hardwired to 80Mhz.  glm has never supported that
		 * version of the 875.
		 */
		if (GLM_REV(glm) < REV2) {
			if (IS_876(glm)) {
				glm_report_fault(glm->g_dip, glm,
				    DDI_SERVICE_LOST,
				    DDI_DEVICE_FAULT, "Rev. %d of the Symbios"
				    " 53c876 not supported.", GLM_REV(glm));
			} else {
				glm_report_fault(glm->g_dip, glm,
				    DDI_SERVICE_LOST,
				    DDI_DEVICE_FAULT, "Rev. %d of the Symbios"
				    " 53c875 not supported.", GLM_REV(glm));
			}
			return (FALSE);
		}

		if (IS_876(glm)) {
			glm_log(glm, CE_NOTE, "?Rev. %d Symbios 53c876 found."
			    "\n", GLM_REV(glm));
		} else {
			glm_log(glm, CE_NOTE, "?Rev. %d Symbios 53c875 found."
			    "\n", GLM_REV(glm));
		}

		/*
		 * Now locate the address of the SCRIPTS ram.  This
		 * address offset is needed by the SCRIPTS processor.
		 */
		glm->g_ram_base_addr =
		    pci_config_get32(glm->g_config_handle, PCI_CONF_BASE2);
	} else if (glm->g_devid == GLM_53c895) {
		glm->g_options |= (GLM_OPT_WIDE_BUS |
		    GLM_OPT_ONBOARD_RAM | GLM_OPT_ULTRA | GLM_OPT_LARGE_FIFO);
		glm->g_sync_offset = SYNC_OFFSET(glm);
		glm->g_max_div = 7;

		/*
		 * Only support B1 (rev. 2) parts or higher.
		 */
		if (GLM_REV(glm) < REV2) {
			glm_report_fault(glm->g_dip, glm, DDI_SERVICE_LOST,
			    DDI_DEVICE_FAULT, "Rev. %d of the Symbios 53c895"
			    " not supported.", GLM_REV(glm));
			return (FALSE);
		}

		glm_log(glm, CE_NOTE, "?Rev. %d Symbios 53c895 found.\n",
		    GLM_REV(glm));

		/*
		 * Now locate the address of the SCRIPTS ram.  This
		 * address offset is needed by the SCRIPTS processor.
		 */
		glm->g_ram_base_addr =
		    pci_config_get32(glm->g_config_handle, PCI_CONF_BASE2);
	} else if (glm->g_devid == GLM_53c896) {
		glm->g_options |= (GLM_OPT_WIDE_BUS |
		    GLM_OPT_ONBOARD_RAM | GLM_OPT_ULTRA | GLM_OPT_LARGE_FIFO);
		glm->g_sync_offset = SYNC_OFFSET(glm);
		glm->g_max_div = 7;

		/*
		 * Only support D0 (rev. 7) parts or higher.
		 */
		if (GLM_REV(glm) < REV7) {
			glm_report_fault(glm->g_dip, glm, DDI_SERVICE_LOST,
			    DDI_DEVICE_FAULT, "Rev. %d of the Symbios 53c896"
			    " not supported.", GLM_REV(glm));
			return (FALSE);
		}

		glm_log(glm, CE_NOTE, "?Rev. %d Symbios 53c896 found.\n",
		    GLM_REV(glm));

		/*
		 * Now locate the address of the SCRIPTS ram.  This
		 * address offset is needed by the SCRIPTS processor.
		 */
		glm->g_ram_base_addr =
		    pci_config_get32(glm->g_config_handle, PCI_CONF_BASE3);
		glm->g_ram_base_addr &= ~0xf;
	} else if ((glm->g_devid == GLM_53c1010_33) ||
	    (glm->g_devid == GLM_53c1010_66)) {

		glm->g_options |= (GLM_OPT_WIDE_BUS |
		    GLM_OPT_ONBOARD_RAM | GLM_OPT_ULTRA | GLM_OPT_LARGE_FIFO |
		    GLM_OPT_DT);
		glm->g_sync_offset = SYNC_OFFSET(glm);
		glm->g_dt_offset = glm->g_sync_offset << 1;
		glm->g_max_div = 7;

		glm_log(glm, CE_NOTE, "?Rev. %d Symbios 53c1010-33/66 found.\n",
		    GLM_REV(glm));

		/*
		 * Now locate the address of the SCRIPTS ram.  This
		 * address offset is needed by the SCRIPTS processor.
		 */
		glm->g_ram_base_addr =
		    pci_config_get32(glm->g_config_handle, PCI_CONF_BASE3);
		glm->g_ram_base_addr &= ~0xf;
	} else {
		/*
		 * Free the configuration registers and fail.
		 */
		glm_report_fault(glm->g_dip, glm, DDI_SERVICE_LOST,
		    DDI_DEVICE_FAULT, "Symbios PCI device (1000,%x) "
		    "not supported.", glm->g_devid);
		return (FALSE);
	}

	/*
	 * Set the latency timer to 0x40 as specified by the upa -> pci
	 * bridge chip design team.  This may be done by the sparc pci
	 * bus nexus driver, but the driver should make sure the latency
	 * timer is correct for performance reasons.
	 */
	pci_config_put8(glm->g_config_handle, PCI_CONF_LATENCY_TIMER,
	    GLM_LATENCY_TIMER);


	return (TRUE);
}

static void
glm_setup_cmd_reg(struct glm *glm)
{
	ushort_t cmdreg;

	/*
	 * Set the command register to the needed values.
	 */
	cmdreg = pci_config_get16(glm->g_config_handle, PCI_CONF_COMM);
	cmdreg |= (PCI_COMM_ME | PCI_COMM_SERR_ENABLE |
	    PCI_COMM_PARITY_DETECT | PCI_COMM_MAE);
	cmdreg &= ~PCI_COMM_IO;
	pci_config_put16(glm->g_config_handle, PCI_CONF_COMM, cmdreg);
}

/*
 * Initialize the Table Indirect pointers for each target, lun
 */
static glm_unit_t *
glm_unit_init(glm_t *glm, int target, int lun)
{
	size_t alloc_len;
	ddi_dma_attr_t unit_dma_attrs;
	uint_t ncookie;
	struct glm_dsa *dsap;
	ddi_dma_cookie_t cookie;
	ddi_dma_handle_t dma_handle;
	ddi_acc_handle_t accessp;
	glm_unit_t *unit;

	NDBG0(("glm_unit_init: target=%x, lun=%x", target, lun));

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the GLM's per-target structures.
	 */
	unit_dma_attrs = glm->g_hba_dma_attrs;
	unit_dma_attrs.dma_attr_sgllen		= 1;
	unit_dma_attrs.dma_attr_granular	= sizeof (struct glm_dsa);

	/*
	 * allocate a per-target structure upon demand,
	 * in a platform-independent manner.
	 */
	if (ddi_dma_alloc_handle(glm->g_dip, &unit_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &dma_handle) != DDI_SUCCESS) {
		glm_log(glm, CE_WARN,
		    "(%d,%d): unable to allocate dma handle.", target, lun);
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.unit_init.4013]");
		return (NULL);
	}

	if (ddi_dma_mem_alloc(dma_handle, sizeof (struct glm_dsa),
	    &dev_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, (caddr_t *)&dsap, &alloc_len, &accessp) != DDI_SUCCESS) {
		ddi_dma_free_handle(&dma_handle);
		glm_log(glm, CE_WARN,
		    "(%d,%d): unable to allocate per-target structure.",
		    target, lun);
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.unit_init.4014]");
		return (NULL);
	}

	if (ddi_dma_addr_bind_handle(dma_handle, NULL, (caddr_t)dsap,
	    alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &cookie, &ncookie) != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&accessp);
		ddi_dma_free_handle(&dma_handle);
		glm_log(glm, CE_WARN, "(%d,%d): unable to bind DMA resources.",
		    target, lun);
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.unit_init.4015]");
		return (NULL);
	}

	unit = kmem_zalloc(sizeof (glm_unit_t), KM_SLEEP);
	ASSERT(unit != NULL);
	unit->nt_refcnt++;

	/* store pointer to per-target structure in HBA's array */
	NTL2UNITP(glm, target, lun) = unit;

	unit->nt_dsap	= dsap;
	unit->nt_dma_p	= dma_handle;
	unit->nt_target	= (ushort_t)target;
	unit->nt_lun	= (ushort_t)lun;
	unit->nt_accessp = accessp;
	unit->nt_dsa_addr = cookie.dmac_address;
	if (glm->g_reset_delay[target]) {
		unit->nt_throttle = HOLD_THROTTLE;
	} else {
		glm_full_throttle(glm, unit->nt_target, unit->nt_lun);
	}
	unit->nt_waitqtail = &unit->nt_waitq;

	return (unit);
}

/*
 * Initialize the Table Indirect pointers for each unit
 */
static void
glm_table_init(glm_t *glm, glm_unit_t *unit)
{
	struct glm_dsa *dsap;
	ddi_acc_handle_t accessp;
	uint32_t tbl_addr = unit->nt_dsa_addr;
	ushort_t target = unit->nt_target;

	NDBG0(("glm_table_init: unit=0x%p", (void *)unit));


	/* clear the table */
	dsap = unit->nt_dsap;
	bzero(dsap, sizeof (*dsap));

	/*
	 * initialize the sharable data structure between host and hba
	 * If the hba has already negotiated sync/wide, copy those values
	 * into the per target sync/wide parameters.
	 *
	 * perform all byte assignments
	 */
	dsap->nt_selectparm.nt_sdid = target;
	glm->g_dsa->g_reselectparm[target].g_sdid = target;

	if (glm->g_dsa->g_reselectparm[target].g_sxfer) {
		dsap->nt_selectparm.nt_scntl3 =
		    glm->g_dsa->g_reselectparm[target].g_scntl3;
		dsap->nt_selectparm.nt_sxfer =
		    glm->g_dsa->g_reselectparm[target].g_sxfer;
	} else {
		dsap->nt_selectparm.nt_scntl3 = glm->g_scntl3;
		glm->g_dsa->g_reselectparm[target].g_scntl3 = glm->g_scntl3;
		dsap->nt_selectparm.nt_sxfer = 0;
	}

	(void) ddi_dma_sync(glm->g_dsa_dma_h, 0, 0, DDI_DMA_SYNC_FORDEV);
	accessp = unit->nt_accessp;

	/* perform multi-bytes assignments */
	ddi_put32(accessp, (uint32_t *)&dsap->nt_cmd.count,
	    sizeof (dsap->nt_cdb));
	ddi_put32(accessp, (uint32_t *)&dsap->nt_cmd.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->nt_cdb - (uintptr_t)dsap)));

	ddi_put32(accessp, (uint32_t *)&dsap->nt_sendmsg.count,
	    sizeof (dsap->nt_msgoutbuf));
	ddi_put32(accessp, (uint32_t *)&dsap->nt_sendmsg.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->nt_msgoutbuf - (uintptr_t)dsap)));

	ddi_put32(accessp, (uint32_t *)&dsap->nt_rcvmsg.count,
	    sizeof (dsap->nt_msginbuf));
	ddi_put32(accessp, (uint32_t *)&dsap->nt_rcvmsg.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->nt_msginbuf - (uintptr_t)dsap)));

	ddi_put32(accessp, (uint32_t *)&dsap->nt_status.count,
	    sizeof (dsap->nt_statbuf));
	ddi_put32(accessp, (uint32_t *)&dsap->nt_status.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->nt_statbuf - (uintptr_t)dsap)));

	ddi_put32(accessp, (uint32_t *)&dsap->nt_extmsg.count,
	    sizeof (dsap->nt_extmsgbuf));
	ddi_put32(accessp, (uint32_t *)&dsap->nt_extmsg.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->nt_extmsgbuf - (uintptr_t)dsap)));

	ddi_put32(accessp, (uint32_t *)&dsap->nt_syncin.count,
	    sizeof (dsap->nt_syncibuf));
	ddi_put32(accessp, (uint32_t *)&dsap->nt_syncin.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->nt_syncibuf - (uintptr_t)dsap)));

	ddi_put32(accessp, (uint32_t *)&dsap->nt_errmsg.count,
	    sizeof (dsap->nt_errmsgbuf));
	ddi_put32(accessp, (uint32_t *)&dsap->nt_errmsg.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->nt_errmsgbuf - (uintptr_t)dsap)));

	ddi_put32(accessp, (uint32_t *)&dsap->nt_widein.count,
	    sizeof (dsap->nt_wideibuf));
	ddi_put32(accessp, (uint32_t *)&dsap->nt_widein.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->nt_wideibuf - (uintptr_t)dsap)));

	ddi_put32(accessp, (uint32_t *)&dsap->nt_pprin.count,
	    sizeof (dsap->nt_ppribuf));
	ddi_put32(accessp, (uint32_t *)&dsap->nt_pprin.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->nt_ppribuf - (uintptr_t)dsap)));
}


/*
 * glm_hba_init()
 *
 *	Set up this HBA's copy of the SCRIPT and initialize
 *	each of it's target/luns.
 */
static int
glm_hba_init(glm_t *glm)
{
	size_t alloc_len;
	struct glm_hba_dsa *dsap;
	uint_t ncookie;
	ddi_dma_cookie_t cookie;
	ddi_dma_attr_t hba_dma_attrs;
	ddi_acc_handle_t handle;
	uint32_t tbl_addr;

	NDBG0(("glm_hba_init"));

	glm->g_state = NSTATE_IDLE;

	/*
	 * Initialize the empty FIFO completion queue
	 */
	glm->g_donetail = &glm->g_doneq;

	/*
	 * Set syncio for hba to be reject, i.e. we never send a
	 * sdtr or wdtr to ourself.
	 */
	glm->g_syncstate[glm->g_glmid] = NSYNC_SDTR_REJECT;

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the GLM's per-hba structures ... we get
	 * a dma handle just in order to determine the available
	 * burstsizes, then free it again.  Note that the 53c875
	 * supports larger burstsizes than previous chips!
	 */
	glm->g_hba_dma_attrs = glm_dma_attrs;
	if (glm->g_devid == GLM_53c875) {
		glm->g_hba_dma_attrs.dma_attr_burstsizes |= 0x3f8;
	}
	if (ddi_dma_alloc_handle(glm->g_dip, &glm->g_hba_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &glm->g_dsa_dma_h) != DDI_SUCCESS) {
		glm_log(glm, CE_WARN, "Unable to allocate hba dma handle.");
		return (DDI_FAILURE);
	}
	NDBG0(("glm_hba_init: attr burstsizes %x, handle burstsizes %x",
	    glm->g_hba_dma_attrs.dma_attr_burstsizes,
	    ddi_dma_burstsizes(glm->g_dsa_dma_h)));
	glm->g_hba_dma_attrs.dma_attr_burstsizes &=
	    ddi_dma_burstsizes(glm->g_dsa_dma_h);
	ddi_dma_free_handle(&glm->g_dsa_dma_h);

	hba_dma_attrs = glm->g_hba_dma_attrs;
	hba_dma_attrs.dma_attr_align = 0x100;
	hba_dma_attrs.dma_attr_sgllen = 1;
	hba_dma_attrs.dma_attr_granular	= sizeof (struct glm_hba_dsa);

	/*
	 * allocate a per-target structure upon demand,
	 * in a platform-independent manner.
	 */
	if (ddi_dma_alloc_handle(glm->g_dip, &hba_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &glm->g_dsa_dma_h) != DDI_SUCCESS) {
		glm_log(glm, CE_WARN, "Unable to allocate hba dma handle.");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.hba_init.4016]");
		return (DDI_FAILURE);
	}

	if (ddi_dma_mem_alloc(glm->g_dsa_dma_h, sizeof (struct glm_hba_dsa),
	    &dev_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    (caddr_t *)&dsap, &alloc_len, &glm->g_dsa_acc_h) != DDI_SUCCESS) {
		ddi_dma_free_handle(&glm->g_dsa_dma_h);
		glm_log(glm, CE_WARN, "Unable to allocate hba DSA structure.");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.hba_init.4017]");
		return (DDI_FAILURE);
	}

	if (ddi_dma_addr_bind_handle(glm->g_dsa_dma_h, NULL, (caddr_t)dsap,
	    alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &cookie, &ncookie) != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&glm->g_dsa_acc_h);
		ddi_dma_free_handle(&glm->g_dsa_dma_h);
		glm_log(glm, CE_WARN, "Unable to bind DMA resources.");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.hba_init.4018]");
		return (DDI_FAILURE);
	}

	bzero(dsap, sizeof (*dsap));
	tbl_addr = glm->g_dsa_addr = cookie.dmac_address;
	glm->g_dsa = dsap;
	handle = glm->g_dsa_acc_h;

	/*
	 * Initialize hba DSA table.
	 */
	ddi_put32(handle, (uint32_t *)&dsap->g_rcvmsg.count,
	    sizeof (dsap->g_msginbuf));
	ddi_put32(handle, (uint32_t *)&dsap->g_rcvmsg.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->g_msginbuf - (uintptr_t)dsap)));

	ddi_put32(handle, (uint32_t *)&dsap->g_errmsg.count,
	    sizeof (dsap->g_errmsgbuf));
	ddi_put32(handle, (uint32_t *)&dsap->g_errmsg.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->g_errmsgbuf - (uintptr_t)dsap)));

	ddi_put32(handle, (uint32_t *)&dsap->g_moremsgin.count,
	    sizeof (dsap->g_moremsginbuf));
	ddi_put32(handle, (uint32_t *)&dsap->g_moremsgin.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->g_moremsginbuf) - (uintptr_t)dsap));

	ddi_put32(handle, (uint32_t *)&dsap->g_tagmsg.count,
	    sizeof (dsap->g_taginbuf));
	ddi_put32(handle, (uint32_t *)&dsap->g_tagmsg.address,
	    EFF_ADDR(tbl_addr, (uint32_t)
	    ((uintptr_t)&dsap->g_taginbuf - (uintptr_t)dsap)));
	(void) ddi_dma_sync(glm->g_dsa_dma_h, 0, 0, DDI_DMA_SYNC_FORDEV);

	return (DDI_SUCCESS);
}

/*
 * For Ultra2 chips, glm needs to check the scsi mode.
 */
static int
glm_check_smode(struct glm *glm)
{
	uint8_t smode;
	uint_t g_options = glm->g_options;

	smode = (ddi_get8(glm->g_datap, (uint8_t *)(glm->g_devaddr +
	    NREG_STEST4)) & 0xc0);

	if (glm->g_devid == GLM_53c895) {
		int i;
		uint8_t smode2;
		uint8_t smode3;

		/*
		 * In the 895 chip the transition from LVD to SE can
		 * take a bit of time to finish.  Thus the reason for this
		 * little loop.  Once we get 2 matches we know the
		 * transition is complete.
		 */
		for (i = 0; i < 10; i++) {
			drv_usecwait(100*1000);
			smode2 = (ddi_get8(glm->g_datap,
			    (uint8_t *)(glm->g_devaddr + NREG_STEST4)) &
			    0xc0);

			drv_usecwait(100*1000);
			smode3 = (ddi_get8(glm->g_datap,
			    (uint8_t *)(glm->g_devaddr + NREG_STEST4)) &
			    0xc0);

			if ((smode == smode2) && (smode == smode3)) {
			ddi_put8(glm->g_datap,
			    (uint8_t *)(glm->g_devaddr +
			    NREG_STEST0), smode >> 2);
			break;
			}
			smode = smode3;
		}
	}

	/*
	 * High Voltage Differential and Single Ended only support
	 * synchronous speeds up to UltraSCSI.
	 */
	switch (smode) {
	case NB_STEST4_HVD:	/* HVD */
	case NB_STEST4_SE:	/* SE */
		glm->g_options &= ~GLM_OPT_LVD;
		break;
	case NB_STEST4_LVD:	/* LVD */
		glm->g_options |= GLM_OPT_LVD;
		break;
	default:
		glm->g_options &= ~GLM_OPT_LVD;
		glm_log(glm, CE_WARN,
		    "glm%d: Invalid SCSI mode.", glm->g_instance);
		break;
	}

	/*
	 * Return status if the mode has changed or not
	 */
	return (glm->g_options != g_options);
}

static void
glm_hba_fini(glm_t *glm)
{
	NDBG0(("glm_hba_fini"));
	(void) ddi_dma_unbind_handle(glm->g_dsa_dma_h);
	(void) ddi_dma_mem_free(&glm->g_dsa_acc_h);
	ddi_dma_free_handle(&glm->g_dsa_dma_h);
}

static int
glm_script_alloc(glm_t *glm)
{
	int k;
	ddi_acc_handle_t ram_handle;

	NDBG0(("glm_script_alloc"));

	/*
	 * If the glm is on a 875, download the script to the onboard
	 * 4k scripts ram.  Otherwise, use memory scripts.
	 *
	 * In the case of memory scripts, use only one copy.  Point all
	 * memory based glm's to this copy of memory scripts.
	 */
	if (glm->g_options & GLM_OPT_ONBOARD_RAM) {
		/*
		 * Now map in the 4k SCRIPTS RAM for use by the CPU/driver.
		 */
		if (ddi_regs_map_setup(glm->g_dip, BASE_REG2,
		    &glm->g_scripts_ram, 0, 4096, &dev_attr,
		    &ram_handle) != DDI_SUCCESS) {
			return (DDI_FAILURE);
		}

		/*
		 * The reset bit in the ISTAT register can not be set
		 * if we want to write to the 4k scripts ram.
		 */
		ClrSetBits(glm->g_devaddr + NREG_ISTAT, NB_ISTAT_SRST, 0);

		/*
		 * Copy the scripts code into the local 4k RAM.
		 */
		ddi_rep_put32(ram_handle, (uint32_t *)SCRIPT,
		    (uint32_t *)glm->g_scripts_ram, (glm_script_size >> 2),
		    DDI_DEV_AUTOINCR);

		ddi_put32(glm->g_datap,
		    (uint32_t *)(glm->g_devaddr + NREG_SCRATCHB),
		    glm->g_dsa_addr);

		/* check everything OK before returning */
		if (ddi_check_acc_handle(ram_handle) != DDI_SUCCESS) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_LOST, DDI_DATAPATH_FAULT,
			    "glm_script_alloc - data path failed");
			return (DDI_FAILURE);
		}

		/*
		 * Free the 4k SRAM mapping.
		 */
		ddi_regs_map_free(&ram_handle);

		/*
		 * These are the script entry offsets.
		 */
		for (k = 0; k < NSS_FUNCS; k++)
			glm->g_glm_scripts[k] =
			    (glm->g_ram_base_addr + glm_script_offset(k));

		glm->g_do_list_end = (glm->g_ram_base_addr + Ent_do_list_end);
		glm->g_di_list_end = (glm->g_ram_base_addr + Ent_di_list_end);

		glm->g_dt_do_list_end =
		    (glm->g_ram_base_addr + Ent_dt_do_list_end);
		glm->g_dt_di_list_end =
		    (glm->g_ram_base_addr + Ent_dt_di_list_end);

	} else {
		/*
		 * Memory scripts are initialized once
		 * for each HBA.
		 */
		if (glm_memory_script_init(glm) == FALSE) {
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}

/*
 * Free the memory scripts for this HBA.
 */
static void
glm_script_free(struct glm *glm)
{
	NDBG0(("glm_script_free"));

	mutex_enter(&glm_global_mutex);
	if ((glm->g_options & GLM_OPT_ONBOARD_RAM) == 0) {
		(void) ddi_dma_unbind_handle(glm->g_script_dma_handle);
		(void) ddi_dma_mem_free(&glm->g_script_acc_handle);
		ddi_dma_free_handle(&glm->g_script_dma_handle);
	}
	mutex_exit(&glm_global_mutex);
}

static void
glm_cfg_fini(glm_t *glm)
{
	NDBG0(("glm_cfg_fini"));
	ddi_regs_map_free(&glm->g_datap);
}

/*
 * Offsets of SCRIPT routines.
 */
static int
glm_script_offset(int func)
{
	NDBG0(("glm_script_offset: func=%x", func));

	switch (func) {
	case NSS_STARTUP:	/* select a target and start a request */
		return (Ent_start_up);
	case NSS_CONTINUE:	/* continue with current target (no select) */
		return (Ent_continue);
	case NSS_WAIT4RESELECT:	/* wait for reselect */
		return (Ent_resel_m);
	case NSS_CLEAR_ACK:
		return (Ent_clear_ack);
	case NSS_EXT_MSG_OUT:
		return (Ent_ext_msg_out);
	case NSS_ERR_MSG:
		return (Ent_errmsg);
	case NSS_BUS_DEV_RESET:
		return (Ent_dev_reset);
	case NSS_ABORT:
		return (Ent_abort);
	case NSS_EXT_MSG_IN:
		return (Ent_ext_msg_in);
	case NSS_PMM:
		return (Ent_phase_mis_match);
	default:
		return (0);
	}
	/*NOTREACHED*/
}

/*
 * glm_memory_script_init()
 */
static int
glm_memory_script_init(glm_t *glm)
{
	caddr_t		memp;
	int		func;
	size_t		alloc_len;
	uint_t		ncookie;
	ddi_dma_cookie_t cookie;
	ddi_dma_attr_t	script_dma_attrs;
	size_t total_scripts = glm_script_size + GLM_HBA_DSA_ADDR_OFFSET;

	NDBG0(("glm_memory_script_init"));

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the GLM's per-target structures.
	 */
	script_dma_attrs = glm->g_hba_dma_attrs;
	script_dma_attrs.dma_attr_sgllen	= 1;
	script_dma_attrs.dma_attr_granular	= total_scripts;

	if (ddi_dma_alloc_handle(glm->g_dip, &script_dma_attrs,
	    DDI_DMA_DONTWAIT, NULL, &glm->g_script_dma_handle) != DDI_SUCCESS) {
		glm_log(glm, CE_WARN, "unable to allocate dma handle.");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.memory_script_init.4019]");
		return (FALSE);
	}

	if (ddi_dma_mem_alloc(glm->g_script_dma_handle, total_scripts,
	    &dev_attr, DDI_DMA_STREAMING, DDI_DMA_DONTWAIT, NULL,
	    &memp, &alloc_len, &glm->g_script_acc_handle) != DDI_SUCCESS) {
		glm_log(glm, CE_WARN, "unable to allocate script memory.");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.memory_script_init.4020]");
		return (FALSE);
	}

	if (ddi_dma_addr_bind_handle(glm->g_script_dma_handle, NULL, memp,
	    alloc_len, DDI_DMA_READ | DDI_DMA_STREAMING, DDI_DMA_DONTWAIT, NULL,
	    &cookie, &ncookie) != DDI_DMA_MAPPED) {
		glm_log(glm, CE_WARN, "unable to allocate script DMA.");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.memory_script_init.4021]");
		return (FALSE);
	}

	/* copy the script into the buffer we just allocated */
	ddi_rep_put32(glm->g_script_acc_handle, (uint32_t *)SCRIPT,
	    (uint32_t *)memp, glm_script_size >> 2, DDI_DEV_AUTOINCR);

	for (func = 0; func < NSS_FUNCS; func++) {
		glm->g_glm_scripts[func] =
		    cookie.dmac_address + glm_script_offset(func);
	}

	glm->g_do_list_end = (cookie.dmac_address + Ent_do_list_end);
	glm->g_di_list_end = (cookie.dmac_address + Ent_di_list_end);

	ddi_put32(glm->g_datap,
	    (uint32_t *)(glm->g_devaddr + NREG_SCRATCHB), glm->g_dsa_addr);

	return (TRUE);
}


/*
 * tran_tgt_init(9E) - target device instance initialization
 */
/*ARGSUSED*/
static int
glm_scsi_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd)
{
	/*
	 * At this point, the scsi_device structure already exists
	 * and has been initialized.
	 *
	 * Use this function to allocate target-private data structures,
	 * if needed by this HBA.  Add revised flow-control and queue
	 * properties for child here, if desired and if you can tell they
	 * support tagged queueing by now.
	 */
	glm_t *glm;
	int targ = sd->sd_address.a_target;
	int lun = sd->sd_address.a_lun;
	int rval = DDI_FAILURE;
	glm = SDEV2GLM(sd);

	NDBG0(("glm_scsi_tgt_init: hbadip=0x%p tgtdip=0x%p tgt=%x lun=%x",
	    (void *)hba_dip, (void *)tgt_dip, targ, lun));

	/* test device at entry point */
	if (ddi_get_devstate(glm->g_dip) == DDI_DEVSTATE_DOWN) {
		return (DDI_FAILURE);
	}

	mutex_enter(&glm->g_mutex);

	/*
	 * If this HBA hardware does not support wide,
	 * return DDI_FAILURE for the high targets.
	 */
	if (targ >= NTARGETS && targ < NTARGETS_WIDE &&
	    ((glm->g_options & GLM_OPT_WIDE_BUS) == 0)) {
		mutex_exit(&glm->g_mutex);
		return (rval);
	}

	if (targ < 0 || targ >= NTARGETS_WIDE ||
	    lun < 0 || lun >= NLUNS_GLM) {
		NDBG0(("%s%d: %s%d bad address <%d,%d>",
		    ddi_driver_name(hba_dip), ddi_get_instance(hba_dip),
		    ddi_driver_name(tgt_dip), ddi_get_instance(tgt_dip),
		    targ, lun));
		mutex_exit(&glm->g_mutex);
		return (rval);
	}

	rval = glm_create_unit_dsa(glm, targ, lun);

	/* check hardware status prior to passing results to caller.  */
	if (glm_check_handle_status(glm) != DDI_SUCCESS) {
		glm_report_fault(glm->g_dip, glm,
		    DDI_SERVICE_LOST, DDI_DATAPATH_FAULT,
		    "scsi tgt init - data path failed");
		glm_tear_down_unit_dsa(glm, targ, lun);
		mutex_exit(&glm->g_mutex);
		return (DDI_FAILURE);
	}
	if (ADDR2GLMUNITP(&sd->sd_address) &&
	    ADDR2GLMUNITP(&sd->sd_address)->nt_dma_p &&
	    ddi_check_dma_handle(ADDR2GLMUNITP(&sd->sd_address)->nt_dma_p) !=
	    DDI_SUCCESS) {
		glm_tear_down_unit_dsa(glm, targ, lun);
		mutex_exit(&glm->g_mutex);
		return (DDI_FAILURE);
	}

	mutex_exit(&glm->g_mutex);

	return (rval);
}

static int
glm_create_unit_dsa(struct glm *glm, int targ, int lun)
{
	glm_unit_t *unit;
	ASSERT(mutex_owned(&glm->g_mutex));

	/*
	 * Has this target already been initialized?
	 * Note that targets are probed multiple times for st, sd, etc.
	 */
	if ((unit = NTL2UNITP(glm, targ, lun)) != NULL) {
		unit->nt_refcnt++;
		return (DDI_SUCCESS);
	}

	/*
	 * allocate and initialize a unit structure
	 */
	unit = glm_unit_init(glm, targ, lun);
	if (unit == NULL) {
		return (DDI_FAILURE);
	}

	glm_table_init(glm, unit);

	if (glm_alloc_active_slots(glm, unit, KM_SLEEP)) {
		return (DDI_FAILURE);
	}

	/*
	 * if recreating a unit struct for a device that my have
	 * been deleted after target offline and arq was previously
	 * enabled, then create the arq packet
	 */
	if (glm->g_arq_mask[targ] & (1 << lun)) {
		struct scsi_address ap;

		ap.a_hba_tran = glm->g_tran;
		ap.a_target = (ushort_t)targ;
		ap.a_lun = (uchar_t)lun;
		(void) glm_create_arq_pkt(unit, &ap);
	}

	return (DDI_SUCCESS);
}

/*
 * tran_tgt_probe(9E) - target device probing
 */
static int
glm_scsi_tgt_probe(struct scsi_device *sd, int (*callback)())
{
	dev_info_t *dip = ddi_get_parent(sd->sd_dev);
	int rval = SCSIPROBE_FAILURE;
	scsi_hba_tran_t *tran;
	struct glm *glm;
	int tgt = sd->sd_address.a_target;

	NDBG0(("glm_scsi_tgt_probe: tgt=%x lun=%x",
	    tgt, sd->sd_address.a_lun));

	if (sd->sd_address.a_target >= NTARGETS_WIDE) {
		cmn_err(CE_WARN,
		    "glm_scsi_tgt_probe: invalid target (%d)",
		    sd->sd_address.a_target);
		return (rval);
	}

	tran = ddi_get_driver_private(dip);
	ASSERT(tran != NULL);
	glm = TRAN2GLM(tran);

	/* test device at entry point */
	if (ddi_get_devstate(glm->g_dip) == DDI_DEVSTATE_DOWN) {
		return (SCSIPROBE_FAILURE);
	}

	/*
	 * renegotiate because not all targets will return a
	 * check condition on inquiry
	 */
	mutex_enter(&glm->g_mutex);
	if (sd->sd_address.a_lun >= glm->g_max_lun[tgt]) {
		glm_tear_down_unit_dsa(glm, tgt, sd->sd_address.a_lun);
		mutex_exit(&glm->g_mutex);
		return (rval);
	}
	glm_force_renegotiation(glm, tgt);
	mutex_exit(&glm->g_mutex);
	rval = scsi_hba_probe(sd, callback);

	/*
	 * the scsi-options precedence is:
	 *	target-scsi-options		highest
	 *	device-type-scsi-options
	 *	per bus scsi-options
	 *	global scsi-options		lowest
	 */
	mutex_enter(&glm->g_mutex);
	if ((rval == SCSIPROBE_EXISTS) &&
	    ((glm->g_target_scsi_options_defined & (1 << tgt)) == 0)) {
		int options;

		options = scsi_get_device_type_scsi_options(dip, sd, -1);
		if (options != -1) {
			glm->g_target_scsi_options[tgt] = options;
			glm_log(glm, CE_NOTE,
			    "?target%x-scsi-options = 0x%x\n", tgt,
			    glm->g_target_scsi_options[tgt]);
			glm_force_renegotiation(glm, tgt);
		}

		/* update max LUNs for this tgt */
		glm_update_max_luns(glm, tgt);
	}

	/* check hardware status prior to passing results to caller.  */
	if (glm_check_handle_status(glm) != DDI_SUCCESS) {
		glm_report_fault(glm->g_dip, glm,
		    DDI_SERVICE_LOST, DDI_DATAPATH_FAULT,
		    "scsi tgt probe - data path failed");
		if (rval == SCSIPROBE_EXISTS) {
			scsi_unprobe(sd);
		}
		mutex_exit(&glm->g_mutex);
		return (SCSIPROBE_FAILURE);
	}
	if (ADDR2GLMUNITP(&sd->sd_address) &&
	    ADDR2GLMUNITP(&sd->sd_address)->nt_dma_p &&
	    ddi_check_dma_handle(ADDR2GLMUNITP(&sd->sd_address)->nt_dma_p) !=
	    DDI_SUCCESS) {
		if (rval == SCSIPROBE_EXISTS) {
			scsi_unprobe(sd);
		}
		mutex_exit(&glm->g_mutex);
		return (SCSIPROBE_FAILURE);
	}
	mutex_exit(&glm->g_mutex);

	return (rval);
}

/*
 * update max lun for this target
 */
static void
glm_update_max_luns(struct glm *glm, int tgt)
{
	switch (SCSI_OPTIONS_NLUNS(glm->g_target_scsi_options[tgt])) {
		case SCSI_OPTIONS_NLUNS_32:
		glm->g_max_lun[tgt] = SCSI_32LUNS_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_16:
		glm->g_max_lun[tgt] = SCSI_16LUNS_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_8:
		glm->g_max_lun[tgt] = NLUNS_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_1:
		glm->g_max_lun[tgt] = SCSI_1LUN_PER_TARGET;
		break;
	case SCSI_OPTIONS_NLUNS_DEFAULT:
		glm->g_max_lun[tgt] = (uchar_t)NLUNS_PER_TARGET;
		break;
	default:
		glm_log(glm, CE_WARN,
		    "unknown scsi-options value for max luns: using %d",
		    glm->g_max_lun[tgt]);
		break;
	}
}

/*
 * tran_tgt_free(9E) - target device instance deallocation
 */
/*ARGSUSED*/
static void
glm_scsi_tgt_free(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd)
{
	glm_t		*glm	= TRAN2GLM(hba_tran);
	int		targ	= sd->sd_address.a_target;
	int		lun	= sd->sd_address.a_lun;

	NDBG0(("glm_scsi_tgt_free: hbadip=0x%p tgtdip=0x%p tgt=%x lun=%x",
	    (void *)hba_dip, (void *)tgt_dip, targ, lun));

	mutex_enter(&glm->g_mutex);
	glm_tear_down_unit_dsa(glm, targ, lun);
	mutex_exit(&glm->g_mutex);
}

/*
 * free unit and dsa structure memory.
 */
static void
glm_tear_down_unit_dsa(struct glm *glm, int targ, int lun)
{
	glm_unit_t *unit;
	struct scsi_address sa;
	struct nt_slots *active;
	struct glm_scsi_cmd *arqsp;
	struct arq_private_data *arq_data;

	unit = NTL2UNITP(glm, targ, lun);
	if (unit == NULL) {
		return;
	}
	ASSERT(unit->nt_refcnt > 0);

	/*
	 * Decrement reference count to per-target info and
	 * if we're finished with this target, release the
	 * per-target info.
	 */
	if (--(unit->nt_refcnt) == 0) {

		/*
		 * If there are cmds to run on the waitq, a current cmd or
		 * any outstanding cmds, keep this target's unit and dsa
		 * structures around.  Only tear down these structures when
		 * there is nothing else to run for this target.
		 */
		if ((unit->nt_waitq != NULL) || (unit->nt_ncmdp != NULL) ||
		    unit->nt_tcmds) {
			unit->nt_refcnt = 1;
			return;
		}

		/*
		 * delete ARQ pkt.
		 */
		if (unit->nt_arq_pkt) {

			/*
			 * If there is a saved sp, there must be a ARQ
			 * pkt in progress.
			 */
			arqsp = unit->nt_arq_pkt;
			arq_data = (struct arq_private_data *)
			    arqsp->cmd_pkt->pkt_private;
			if (arq_data->arq_save_sp != NULL) {
				unit->nt_refcnt = 1;
				return;
			}

			sa.a_hba_tran = NULL;
			sa.a_target = (ushort_t)unit->nt_target;
			sa.a_lun = (uchar_t)unit->nt_lun;
			(void) glm_delete_arq_pkt(unit, &sa);
		}

		/*
		 * remove active slot(s).
		 */
		active = unit->nt_active;
		if (active) {
			kmem_free(active, active->nt_size);
			unit->nt_active = NULL;
		}

		(void) ddi_dma_unbind_handle(unit->nt_dma_p);
		(void) ddi_dma_mem_free(&unit->nt_accessp);
		ddi_dma_free_handle(&unit->nt_dma_p);
		kmem_free(unit, sizeof (glm_unit_t));
		NTL2UNITP(glm, targ, lun) = NULL;
	}
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
/*ARGSUSED*/
static int
glm_scsi_start(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	glm_t *glm = PKT2GLM(pkt);
	struct glm_scsi_cmd *cmd = PKT2CMD(pkt);
	int rval;

	NDBG1(("glm_scsi_start: target=%x pkt=0x%p",
	    ap->a_target, (void *)pkt));

	/* test device at entry point */
	if (ddi_get_devstate(glm->g_dip) == DDI_DEVSTATE_DOWN) {
		return (TRAN_FATAL_ERROR);
	}

	/*
	 * prepare the pkt before taking mutex.
	 */
	rval = glm_prepare_pkt(glm, cmd);
	if (rval != TRAN_ACCEPT) {
		return (rval);
	}

	/*
	 * Send the command to target/lun, however your HBA requires it.
	 * If busy, return TRAN_BUSY; if there's some other formatting error
	 * in the packet, return TRAN_BADPKT; otherwise, fall through to the
	 * return of TRAN_ACCEPT.
	 *
	 * Remember that access to shared resources, including the glm_t
	 * data structure and the HBA hardware registers, must be protected
	 * with mutexes, here and everywhere.
	 *
	 * Also remember that at interrupt time, you'll get an argument
	 * to the interrupt handler which is a pointer to your glm_t
	 * structure; you'll have to remember which commands are outstanding
	 * and which scsi_pkt is the currently-running command so the
	 * interrupt handler can refer to the pkt to set completion
	 * status, call the target driver back through pkt_comp, etc.
	 */
	mutex_enter(&glm->g_mutex);
	cmd->cmd_type = NRQ_NORMAL_CMD;
	rval = glm_accept_pkt(glm, cmd);
	mutex_exit(&glm->g_mutex);

	return (rval);
}

static int
glm_accept_pkt(struct glm *glm, struct glm_scsi_cmd *cmd)
{
	struct scsi_pkt *pkt = CMD2PKT(cmd);
	struct glm_unit *unit = PKT2GLMUNITP(pkt);
	int rval = TRAN_ACCEPT;

	NDBG1(("glm_accept_pkt: cmd=0x%p", (void *)cmd));

	ASSERT(mutex_owned(&glm->g_mutex));

	if ((cmd->cmd_flags & CFLAG_PREPARED) == 0) {
		rval = glm_prepare_pkt(glm, cmd);
		if (rval != TRAN_ACCEPT) {
			cmd->cmd_flags &= ~CFLAG_TRANFLAG;
			return (rval);
		}
	}

	/*
	 * Create unit and dsa structures on the fly if this target
	 * was never inited/probed by tran_tgt_init(9E)/tran_tgt_probe(9E)
	 */
	if (unit == NULL) {
		if (glm_create_unit_dsa(glm, Tgt(cmd), Lun(cmd)) ==
		    DDI_FAILURE) {
			return (TRAN_BADPKT);
		}
		unit = PKT2GLMUNITP(pkt);

	}

	/*
	 * tagged targets should have NTAGS slots.
	 */
	if (TAGGED(unit->nt_target) &&
	    unit->nt_active->nt_n_slots != NTAGS) {
		(void) glm_alloc_active_slots(glm, unit, KM_SLEEP);
	}

	/*
	 * reset the throttle if we were draining
	 */
	if ((unit->nt_tcmds == 0) &&
	    (unit->nt_throttle == DRAIN_THROTTLE)) {
		NDBG23(("reset throttle\n"));
		ASSERT(glm->g_reset_delay[Tgt(cmd)] == 0);
		glm_full_throttle(glm, unit->nt_target, unit->nt_lun);
	}

	if ((unit->nt_throttle > HOLD_THROTTLE) &&
	    (unit->nt_throttle > unit->nt_tcmds) &&
	    (unit->nt_waitq == NULL) &&
	    ((cmd->cmd_pkt_flags & FLAG_NOINTR) == 0) &&
	    (glm->g_state == NSTATE_IDLE)) {
		(void) glm_start_cmd(glm, unit, cmd);
	} else {
		glm_queue_pkt(glm, unit, cmd);

		/*
		 * if NO_INTR flag set, tran_start(9E) must poll
		 * device for command completion.
		 */
		if (cmd->cmd_pkt_flags & FLAG_NOINTR) {
			glm_pollret(glm, cmd);

			/*
			 * Only flush the doneq if this is not a proxy
			 * cmd.  For proxy cmds the flushing of the
			 * doneq will be done in those routines.
			 */
			if ((cmd->cmd_flags & CFLAG_CMDPROXY) == 0) {
				glm_doneq_empty(glm);
			}
		}
	}

	return (rval);
}

/*
 * allocate a tag byte and check for tag aging
 */
static char glm_tag_lookup[] =
	{0, MSG_HEAD_QTAG, MSG_ORDERED_QTAG, 0, MSG_SIMPLE_QTAG};

static int
glm_alloc_tag(struct glm *glm, struct glm_unit *unit, struct glm_scsi_cmd *cmd)
{
	struct nt_slots *tag_slots;
	int tag;

	ASSERT(mutex_owned(&glm->g_mutex));
	ASSERT(unit != NULL);

	tag_slots = unit->nt_active;
	ASSERT(tag_slots->nt_n_slots == NTAGS);

alloc_tag:
	tag = (unit->nt_active->nt_tags)++;
	if (unit->nt_active->nt_tags >= NTAGS) {
		/*
		 * we reserve tag 0 for non-tagged cmds
		 */
		unit->nt_active->nt_tags = 1;
	}

	/* Validate tag, should never fail. */
	if (tag_slots->nt_slot[tag] == 0) {
		/*
		 * Store assigned tag and tag queue type.
		 * Note, in case of multiple choice, default to simple queue.
		 */
		ASSERT(tag < NTAGS);
		cmd->cmd_tag[1] = (uchar_t)tag;
		cmd->cmd_tag[0] = glm_tag_lookup[((cmd->cmd_pkt_flags &
		    FLAG_TAGMASK) >> 12)];
		tag_slots->nt_slot[tag] = cmd;
		unit->nt_tcmds++;
		return (0);
	} else {
		int age, i;

		/*
		 * Check tag age.  If timeouts enabled and
		 * tag age greater than 1, print warning msg.
		 * If timeouts enabled and tag age greater than
		 * age limit, begin draining tag que to check for
		 * lost tag cmd.
		 */
		age = tag_slots->nt_slot[tag]->cmd_age++;
		if (age >= glm->g_scsi_tag_age_limit &&
		    tag_slots->nt_slot[tag]->cmd_pkt->pkt_time) {
			NDBG22(("tag %d in use, age= %d", tag, age));
			NDBG22(("draining tag queue"));
			if (glm->g_reset_delay[Tgt(cmd)] == 0) {
				unit->nt_throttle = DRAIN_THROTTLE;
			}
		}

		/* If tag in use, scan until a free one is found. */
		for (i = 1; i < NTAGS; i++) {
			tag = unit->nt_active->nt_tags;
			if (!tag_slots->nt_slot[tag]) {
				NDBG22(("found free tag %d\n", tag));
				break;
			}
			if (++(unit->nt_active->nt_tags) >= NTAGS) {
				/*
				 * we reserve tag 0 for non-tagged cmds
				 */
				unit->nt_active->nt_tags = 1;
			}
			NDBG22(("found in use tag %d\n", tag));
		}

		/*
		 * If no free tags, we're in serious trouble.
		 * the target driver submitted more than 255
		 * requests
		 */
		if (tag_slots->nt_slot[tag]) {
			NDBG22(("target %d: All tags in use!!!\n",
			    unit->nt_target));
			goto fail;
		}
		goto alloc_tag;
	}

fail:
	return (-1);
}

static void
glm_queue_pkt(glm_t *glm, glm_unit_t *unit, ncmd_t *cmd)
{
	NDBG1(("glm_queue_pkt: unit=0x%p cmd=0x%p", (void *)unit, (void *)cmd));

	/*
	 * Add this pkt to the target's work queue
	 */
	if (cmd->cmd_pkt_flags & FLAG_HEAD) {
		glm_waitq_add_lifo(unit, cmd);
	} else {
		glm_waitq_add(unit, cmd);
	}

	/*
	 * If this target isn't active stick it on the hba's work queue
	 */
	if ((unit->nt_state & NPT_STATE_QUEUED) == 0) {
		glm_queue_target(glm, unit);
	}
}

/*
 * prepare the pkt:
 * the pkt may have been resubmitted or just reused so
 * initialize some fields and do some checks.
 */
static int
glm_prepare_pkt(struct glm *glm, struct glm_scsi_cmd *cmd)
{
	struct scsi_pkt *pkt = CMD2PKT(cmd);

	NDBG1(("glm_prepare_pkt: cmd=0x%p", (void *)cmd));

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
	 * zero status byte.
	 */
	*(pkt->pkt_scbp) = 0;

	if (cmd->cmd_flags & CFLAG_DMAVALID) {
		pkt->pkt_resid = cmd->cmd_dmacount;

		/*
		 * consistent packets need to be sync'ed first
		 * (only for data going out)
		 */
		if ((cmd->cmd_flags & CFLAG_CMDIOPB) &&
		    (cmd->cmd_flags & CFLAG_DMASEND)) {
			(void) ddi_dma_sync(cmd->cmd_dmahandle, 0, 0,
			    DDI_DMA_SYNC_FORDEV);
		}
	}

#ifdef GLM_TEST
#ifndef __lock_lint
	if (glm_test_untagged > 0) {
		struct glm_unit *unit = NTL2UNITP(glm, Tgt(cmd), Lun(cmd));
		if (TAGGED(Tgt(cmd)) && (unit != NULL)) {
			cmd->cmd_pkt_flags &= ~FLAG_TAGMASK;
			cmd->cmd_pkt_flags &= ~FLAG_NODISCON;
			glm_log(glm, CE_NOTE,
			    "starting untagged cmd, target=%d,"
			    " tcmds=%d, cmd=0x%p, throttle=%d\n",
			    Tgt(cmd), unit->nt_tcmds, (void *)cmd,
			    unit->nt_throttle);
			glm_test_untagged = -10;
		}
	}
#endif
#endif

#ifdef GLM_DEBUG
	if ((pkt->pkt_comp == NULL) &&
	    ((pkt->pkt_flags & FLAG_NOINTR) == 0)) {
		NDBG1(("packet with pkt_comp == 0"));
		return (TRAN_BADPKT);
	}
#endif

	if ((glm->g_target_scsi_options[Tgt(cmd)] & SCSI_OPTIONS_DR) == 0) {
		cmd->cmd_pkt_flags |= FLAG_NODISCON;
	}

	cmd->cmd_flags =
	    (cmd->cmd_flags & ~(CFLAG_TRANFLAG | CFLAG_CMD_REMOVED)) |
	    CFLAG_PREPARED | CFLAG_IN_TRANSPORT;

	return (TRAN_ACCEPT);
}

/*
 * tran_init_pkt(9E) - allocate scsi_pkt(9S) for command
 *
 * One of three possibilities:
 *	- allocate scsi_pkt
 *	- allocate scsi_pkt and DMA resources
 *	- allocate DMA resources to an already-allocated pkt
 */
static struct scsi_pkt *
glm_scsi_init_pkt(struct scsi_address *ap, struct scsi_pkt *pkt,
    struct buf *bp, int cmdlen, int statuslen, int tgtlen, int flags,
    int (*callback)(), caddr_t arg)
{
	struct glm_scsi_cmd *cmd, *new_cmd;
	struct glm *glm = ADDR2GLM(ap);
	int failure = 1;
	int kf =  (callback == SLEEP_FUNC)? KM_SLEEP: KM_NOSLEEP;

	ASSERT(callback == NULL_FUNC || callback == SLEEP_FUNC);
#ifdef GLM_TEST_EXTRN_ALLOC
	statuslen *= 100; tgtlen *= 4;
#endif
	NDBG3(("glm_scsi_init_pkt:\n"
	    "\ttgt=%x in=0x%p bp=0x%p clen=%x slen=%x tlen=%x flags=%x",
	    ap->a_target, (void *)pkt, (void *)bp, cmdlen,
	    statuslen, tgtlen, flags));

	/*
	 * Allocate the new packet.
	 */
	if (pkt == NULL) {
		ddi_dma_handle_t save_dma_handle;

		cmd = kmem_cache_alloc(glm->g_kmem_cache, kf);

		if (cmd) {
			save_dma_handle = cmd->cmd_dmahandle;
			bzero(cmd, sizeof (*cmd) + scsi_pkt_size());
			cmd->cmd_dmahandle = save_dma_handle;

			pkt = (struct scsi_pkt *)((uchar_t *)cmd +
			    sizeof (struct glm_scsi_cmd));
			pkt->pkt_ha_private	= (opaque_t)cmd;
			pkt->pkt_address	= *ap;
			pkt->pkt_private = (opaque_t)cmd->cmd_pkt_private;
			pkt->pkt_scbp	= (opaque_t)&cmd->cmd_scb;
			pkt->pkt_cdbp	= (opaque_t)&cmd->cmd_cdb;
			cmd->cmd_pkt		= (struct scsi_pkt *)pkt;
			cmd->cmd_cdblen 	= (uchar_t)cmdlen;
			cmd->cmd_scblen		= statuslen;
			failure = 0;
		}

		if (failure || (cmdlen > sizeof (cmd->cmd_cdb)) ||
		    (tgtlen > PKT_PRIV_LEN) ||
		    (statuslen > EXTCMDS_STATUS_SIZE)) {
			if (failure == 0) {
				/*
				 * if extern alloc fails, all will be
				 * deallocated, including cmd
				 */
				failure = glm_pkt_alloc_extern(glm, cmd,
				    cmdlen, tgtlen, statuslen, kf);
			}
			if (failure) {
				/*
				 * if extern allocation fails, it will
				 * deallocate the new pkt as well
				 */
				return (NULL);
			}
		}
		new_cmd = cmd;

	} else {
		cmd = PKT2CMD(pkt);
		new_cmd = NULL;
		if ((bp && (bp->b_bcount != 0)) &&
		    (cmd->cmd_flags & CFLAG_PREPARED) &&
		    (cmd->cmd_flags & CFLAG_DMAVALID)) {
			/*
			 * reused pkt with vailid DMA resources.
			 * Must free and realloc these resources, to
			 * ensure the cmd sgl is reinitialized.
			 */
			glm_scsi_dmafree(ap, pkt);
			cmd->cmd_flags &= ~CFLAG_PREPARED;
		}
	}

	/*
	 * DMA resource allocation.  This version assumes your
	 * HBA has some sort of bus-mastering or onboard DMA capability, with a
	 * scatter-gather list of length GLM_MAX_DMA_SEGS, as given in the
	 * ddi_dma_attr_t structure and passed to scsi_impl_dmaget.
	 */
	if (bp && (bp->b_bcount != 0) &&
	    (cmd->cmd_flags & CFLAG_DMAVALID) == 0) {

		int rval, dma_flags;

		/*
		 * Set up DMA memory and position to the next DMA segment.
		 * Information will be in scsi_cmd on return; most usefully,
		 * in cmd->cmd_dmaseg.
		 */
		ASSERT(cmd->cmd_dmahandle != NULL);

		if (bp->b_flags & B_READ) {
			dma_flags = DDI_DMA_READ;
			cmd->cmd_flags &= ~CFLAG_DMASEND;
		} else {
			dma_flags = DDI_DMA_WRITE;
			cmd->cmd_flags |= CFLAG_DMASEND;
		}
		if (flags & PKT_CONSISTENT) {
			cmd->cmd_flags |= CFLAG_CMDIOPB;
			dma_flags |= DDI_DMA_CONSISTENT;
		}
		/*
		 * Work Around don't remove this.
		 * This is for the byte hole problem with
		 * all Psycho and Schizo bridges used in the host.
		 * Hence, run all the READ DMA in Consistent
		 * mode and not streaming mode.
		 */
		if (bp->b_flags & B_READ) {
			dma_flags |= DDI_DMA_CONSISTENT;
		}
		if (flags & PKT_DMA_PARTIAL)
			dma_flags |= DDI_DMA_PARTIAL;

		rval = ddi_dma_buf_bind_handle(cmd->cmd_dmahandle, bp,
		    dma_flags, callback, arg,
		    &cmd->cmd_cookie, &cmd->cmd_cookiec);
		if (rval == DDI_DMA_PARTIAL_MAP) {
			(void) ddi_dma_numwin(cmd->cmd_dmahandle,
			    &cmd->cmd_nwin);
			cmd->cmd_cwin = 0;
			(void) ddi_dma_getwin(cmd->cmd_dmahandle,
			    cmd->cmd_cwin, &cmd->cmd_dma_offset,
			    &cmd->cmd_dma_length, &cmd->cmd_cookie,
			    &cmd->cmd_cookiec);
		} else if (rval && (rval != DDI_DMA_MAPPED)) {
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
			cmd->cmd_flags &= ~CFLAG_DMAVALID;
			if (new_cmd) {
				glm_scsi_destroy_pkt(ap, pkt);
			}
			return ((struct scsi_pkt *)NULL);
		}
		if (rval == DDI_DMA_MAPPED) {
			cmd->cmd_nwin = 1;
			cmd->cmd_cwin = 0;
		}
		cmd->cmd_woff = 0;
		cmd->cmd_dmacount = 0;
		glm_scsi_init_sgl(cmd);
	}
	return (pkt);
}

/*
 * tran_destroy_pkt(9E) - scsi_pkt(9s) deallocation
 *
 * Notes:
 *	- also frees DMA resources if allocated
 *	- implicit DMA synchronization
 */
static void
glm_scsi_destroy_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct glm_scsi_cmd *cmd = PKT2CMD(pkt);
	struct glm *glm = ADDR2GLM(ap);

	NDBG3(("glm_scsi_destroy_pkt: target=%x pkt=0x%p",
	    ap->a_target, (void *)pkt));

	if (cmd->cmd_flags & CFLAG_DMAVALID) {
		(void) ddi_dma_unbind_handle(cmd->cmd_dmahandle);
		cmd->cmd_flags &= ~CFLAG_DMAVALID;
	}

	if ((cmd->cmd_flags &
	    (CFLAG_FREE | CFLAG_CDBEXTERN | CFLAG_PRIVEXTERN |
	    CFLAG_SCBEXTERN)) == 0) {
		cmd->cmd_flags = CFLAG_FREE;
		kmem_cache_free(glm->g_kmem_cache, (void *)cmd);
	} else {
		glm_pkt_destroy_extern(glm, cmd);
	}
}

/* ARGSUSED */
void
glm_scsi_init_sgl(struct glm_scsi_cmd *cmd)
{
	glmti_t *cmap;	/* ptr to the cmd S/G list */

	cmd->cmd_flags |= CFLAG_DMAVALID;
	ASSERT(cmd->cmd_cookiec > 0);

	cmd->cmd_scc = cmd->cmd_cookiec - cmd->cmd_woff;

	if (cmd->cmd_scc > GLM_MAX_DMA_SEGS)
		cmd->cmd_scc = GLM_MAX_DMA_SEGS;

	cmap = &cmd->cmd_sg[cmd->cmd_scc - 1];

	ASSERT(cmd->cmd_cookie.dmac_size != 0);
	/*
	 * When called there a valid cookie in cmd->cmd_cookie,
	 * fetch more as needed.
	 */

	while (cmap >= cmd->cmd_sg) {

		/*
		 * store the segment parms into the cmd S/G list
		 */
		cmap->count = cmd->cmd_cookie.dmac_size;
		cmap->address = cmd->cmd_cookie.dmac_address;
		cmd->cmd_dmacount += cmd->cmd_cookie.dmac_size;
		cmap--;

		/*
		 * Get next DMA cookie
		 */
		if (++cmd->cmd_woff < cmd->cmd_cookiec) {
			ddi_dma_nextcookie(cmd->cmd_dmahandle,
			    &cmd->cmd_cookie);
		}
	}
	cmd->cmd_saved_cookie = cmd->cmd_scc;
}

/*
 * HBA has scatter-gather DMA capability for a list of length GLM_MAX_DMA_SEGS.
 * This size is fixed by scripts.
 */

static void
glm_dsa_dma_setup(struct scsi_pkt *pktp, struct glm_unit *unit, glm_t *glm)
{
	struct glm_scsi_cmd	*cmd = PKT2CMD(pktp);

	if (cmd->cmd_scc == 0 && cmd->cmd_woff < cmd->cmd_cookiec) {
		glm_scsi_init_sgl(cmd);
	}
	if (cmd->cmd_scc == 0 && ++cmd->cmd_cwin < cmd->cmd_nwin) {
		/* XXX function return value ignored */
		(void) ddi_dma_getwin(cmd->cmd_dmahandle, cmd->cmd_cwin,
		    &cmd->cmd_dma_offset, &cmd->cmd_dma_length,
		    &cmd->cmd_cookie, &cmd->cmd_cookiec);
			cmd->cmd_woff = 0;

		/*
		 * get the sgl copy for new window
		 */
		glm_scsi_init_sgl(cmd);
	}

	/*
	 * Setup scratcha0 as the number of segments for scripts to do
	 */
	ddi_put8(glm->g_datap,
	    (uint8_t *)(glm->g_devaddr + NREG_SCRATCHA0), cmd->cmd_scc);

	if (cmd->cmd_scc) {
		glmti_t *dmap;	/* ptr to the DSA SGL */
		glmti_t *cmap;	/* ptr to the cmd SGL */

		dmap = &unit->nt_dsap->nt_data[cmd->cmd_scc - 1];
		cmap = &cmd->cmd_sg[cmd->cmd_scc - 1];

		for (; cmap >= cmd->cmd_sg; dmap--, cmap--) {
			/*
			 * Fill the DSA SGL from the cmd_sgl.
			 */
			ddi_put32(unit->nt_accessp,
			    (uint32_t *)&dmap->count,
			    (uint32_t)cmap->count);
			ddi_put32(unit->nt_accessp,
			    (uint32_t *)&dmap->address,
			    (uint32_t)cmap->address);
		}
	}
}

/*
 * kmem cache constructor and destructor:
 * When constructing, we bzero the cmd and allocate the dma handle
 * When destructing, just free the dma handle
 */
static int
glm_kmem_cache_constructor(void *buf, void *cdrarg, int kmflags)
{
	struct glm_scsi_cmd *cmd = buf;
	struct glm *glm  = cdrarg;
	int  (*callback)(caddr_t) = (kmflags == KM_SLEEP) ? DDI_DMA_SLEEP:
	    DDI_DMA_DONTWAIT;

	NDBG4(("glm_kmem_cache_constructor"));

	bzero(buf, sizeof (*cmd) + scsi_pkt_size());

	/*
	 * allocate a dma handle
	 */
	if ((ddi_dma_alloc_handle(glm->g_dip, &glm->g_hba_dma_attrs, callback,
	    NULL, &cmd->cmd_dmahandle)) != DDI_SUCCESS) {
		return (-1);
	}
	return (0);
}

/*ARGSUSED*/
static void
glm_kmem_cache_destructor(void *buf, void *cdrarg)
{
	struct glm_scsi_cmd *cmd = buf;

	NDBG4(("glm_kmem_cache_destructor"));

	if (cmd->cmd_dmahandle) {
		ddi_dma_free_handle(&cmd->cmd_dmahandle);
	}
}

/*
 * allocate and deallocate external pkt space (ie. not part of glm_scsi_cmd)
 * for non-standard length cdb, pkt_private, status areas
 * if allocation fails, then deallocate all external space and the pkt
 */
/* ARGSUSED */
static int
glm_pkt_alloc_extern(glm_t *glm, ncmd_t *cmd,
    int cmdlen, int tgtlen, int statuslen, int kf)
{
	caddr_t cdbp, scbp, tgt;
	int failure = 0;

	NDBG3(("glm_pkt_alloc_extern: "
	    "cmd=0x%p cmdlen=%x tgtlen=%x statuslen=%x kf=%x",
	    (void *)cmd, cmdlen, tgtlen, statuslen, kf));

	tgt = cdbp = scbp = NULL;
	cmd->cmd_cdblen		= (uchar_t)cmdlen;
	cmd->cmd_scblen		= statuslen;
	cmd->cmd_privlen	= (uchar_t)tgtlen;

	if (cmdlen > sizeof (cmd->cmd_cdb)) {
		if ((cdbp = kmem_zalloc((size_t)cmdlen, kf)) == NULL) {
			failure++;
		} else {
			cmd->cmd_pkt->pkt_cdbp = (opaque_t)cdbp;
			cmd->cmd_flags |= CFLAG_CDBEXTERN;
		}
	}
	if (tgtlen > PKT_PRIV_LEN) {
		if ((tgt = kmem_zalloc(tgtlen, kf)) == NULL) {
			failure++;
		} else {
			cmd->cmd_flags |= CFLAG_PRIVEXTERN;
			cmd->cmd_pkt->pkt_private = tgt;
		}
	}
	if (statuslen > EXTCMDS_STATUS_SIZE) {
		if ((scbp = kmem_zalloc((size_t)statuslen, kf)) == NULL) {
			failure++;
		} else {
			cmd->cmd_flags |= CFLAG_SCBEXTERN;
			cmd->cmd_pkt->pkt_scbp = (opaque_t)scbp;
		}
	}
	if (failure) {
		glm_pkt_destroy_extern(glm, cmd);
	}
	return (failure);
}

/*
 * deallocate external pkt space and deallocate the pkt
 */
static void
glm_pkt_destroy_extern(glm_t *glm, ncmd_t *cmd)
{

	NDBG3(("glm_pkt_destroy_extern: cmd=0x%p", (void *)cmd));

	if (cmd->cmd_flags & CFLAG_FREE) {
		cmn_err(CE_WARN,
		    "!ID[SUNWpd.glm.pkt_destroy_extern.4022]");
		glm_log(glm, CE_PANIC,
		    "glm_pkt_destroy_extern: freeing free packet");
		_NOTE(NOT_REACHED)
		/* NOTREACHED */
	}
	if (cmd->cmd_flags & CFLAG_CDBEXTERN) {
		kmem_free(cmd->cmd_pkt->pkt_cdbp, (size_t)cmd->cmd_cdblen);
	}
	if (cmd->cmd_flags & CFLAG_SCBEXTERN) {
		kmem_free(cmd->cmd_pkt->pkt_scbp, (size_t)cmd->cmd_scblen);
	}
	if (cmd->cmd_flags & CFLAG_PRIVEXTERN) {
		kmem_free(cmd->cmd_pkt->pkt_private, (size_t)cmd->cmd_privlen);
	}
	cmd->cmd_flags = CFLAG_FREE;
	kmem_cache_free(glm->g_kmem_cache, (void *)cmd);
}

/*
 * tran_sync_pkt(9E) - explicit DMA synchronization
 */
/*ARGSUSED*/
static void
glm_scsi_sync_pkt(struct scsi_address *ap, struct scsi_pkt *pktp)
{
	struct glm_scsi_cmd *cmdp = PKT2CMD(pktp);

	NDBG3(("glm_scsi_sync_pkt: target=%x, pkt=0x%p",
	    ap->a_target, (void *)pktp));

	if (cmdp->cmd_dmahandle) {
		(void) ddi_dma_sync(cmdp->cmd_dmahandle, 0, 0,
		    (cmdp->cmd_flags & CFLAG_DMASEND) ?
		    DDI_DMA_SYNC_FORDEV : DDI_DMA_SYNC_FORCPU);
	}
}

/*
 * tran_dmafree(9E) - deallocate DMA resources allocated for command
 */
/*ARGSUSED*/
static void
glm_scsi_dmafree(struct scsi_address *ap, struct scsi_pkt *pktp)
{
	struct glm_scsi_cmd *cmd = PKT2CMD(pktp);

	NDBG3(("glm_scsi_dmafree: target=%x pkt=0x%p",
	    ap->a_target, (void *)pktp));

	if (cmd->cmd_flags & CFLAG_DMAVALID) {
		(void) ddi_dma_unbind_handle(cmd->cmd_dmahandle);
		cmd->cmd_flags ^= CFLAG_DMAVALID;
	}
}

/*
 * Interrupt handling
 * Utility routine.  Poll for status of a command sent to HBA
 * without interrupts (a FLAG_NOINTR command).
 */
static void
glm_pollret(glm_t *glm, ncmd_t *poll_cmdp)
{
	int got_it = TRUE;
	int limit = 0;
	int i;

	NDBG5(("glm_pollret: cmdp=0x%p", (void *)poll_cmdp));

	/*
	 * ensure we are not accessing a target too quickly
	 * after a reset. the throttles get set back later
	 * by the reset delay watch; hopefully, we don't go
	 * thru this loop more than once
	 */
	if (glm->g_reset_delay[Tgt(poll_cmdp)]) {
		drv_usecwait(glm->g_scsi_reset_delay * 1000);
		for (i = 0; i < NTARGETS_WIDE; i++) {
			if (glm->g_reset_delay[i]) {
				int s = 0;
				glm->g_reset_delay[i] = 0;
				for (; s < NLUNS_GLM; s++) {
					glm_full_throttle(glm, i, s);
				}
				glm_queue_target_lun(glm, i);
			}
		}
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
	 * limit the waiting to 5 seconds to avoid a hang in the event that the
	 * cmd never gets started but we are still receiving interrupts
	 */
	while (!(poll_cmdp->cmd_flags & CFLAG_FINISHED)) {
		if (glm_wait_intr(glm, poll_cmdp->cmd_pkt->pkt_time ?
		    (poll_cmdp->cmd_pkt->pkt_time * 10000) :
		    GLM_POLL_TIME) == FALSE) {
			NDBG5(("glm_pollret: command incomplete"));
			got_it = FALSE;
			break;
		}
		if (glm->g_doneq) {
			limit = 0; /* things still happening */
		} else if (++limit > GLM_INTRLOOP_TIME) {
			glm_flush_hba(glm);
			glm_doneq_empty(glm);
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_LOST, DDI_DEVICE_FAULT,
			    "excessive interrupt count");
			return;
		} else {
			drv_usecwait(100);
		}
	}

	NDBG5(("glm_pollret: break"));

	if (!got_it) {
		glm_unit_t *unit = PKT2GLMUNITP(CMD2PKT(poll_cmdp));

		NDBG5(("glm_pollret: command incomplete"));

		/*
		 * this isn't supposed to happen, the hba must be wedged
		 * Mark this cmd as a timeout.
		 */
		glm_set_pkt_reason(glm, poll_cmdp, CMD_TIMEOUT,
		    (STAT_TIMEOUT|STAT_ABORTED));

		if (poll_cmdp->cmd_queued == FALSE) {

			NDBG5(("glm_pollret: not on waitq"));

			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "lost interrupt during polling"
			    " - resetting SCSI bus");
			/*
			 * it must be the active request
			 * reset the bus.
			 */
			glm->g_reset_received = 0;
			GLM_BUS_RESET(glm);

			/* wait for the interrupt to clean this up */
			while (!(poll_cmdp->cmd_flags & CFLAG_FINISHED)) {
				(void) glm_wait_intr(glm,
				    GLM_BUS_RESET_POLL_TIME);
				if (glm->g_reset_received == 0) {
					glm_report_fault(glm->g_dip, glm,
					    DDI_SERVICE_LOST, DDI_DEVICE_FAULT,
					    "timeout on bus reset interrupt");
					glm_flush_hba(glm);
					break;
				}
			}
			poll_cmdp->cmd_pkt->pkt_state |=
			    (STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD);
		} else {

			/* find and remove it from the waitq */
			NDBG5(("glm_pollret: delete from waitq"));
			glm_waitq_delete(unit, poll_cmdp);
		}

	}
	NDBG5(("glm_pollret: done"));
}

static int
glm_wait_intr(glm_t *glm, int poll_time)
{
	int cnt;
	uchar_t	istat;

	NDBG5(("glm_wait_intr"));

	/* keep trying for at least GLM_POLL_TIME/10000 seconds */
	for (cnt = 0; cnt < poll_time; cnt += 1) {
		istat = GLM_GET_ISTAT(glm);
		/*
		 * loop GLM_POLL_TIME times but wait at least 100 usec
		 * each time around the loop
		 */
		if (istat & (NB_ISTAT_DIP | NB_ISTAT_SIP)) {
			glm->g_polled_intr = 1;
			NDBG17(("glm_wait_intr: istat=0x%x", istat));
			/* process this interrupt */
			glm_process_intr(glm, istat);
			if (ddi_check_acc_handle(glm->g_datap) != DDI_SUCCESS) {
				return (FALSE);
			}
			if (ddi_get_devstate(glm->g_dip) == DDI_DEVSTATE_DOWN) {
				return (FALSE);
			}
			istat = GLM_GET_ISTAT(glm);
			if (istat & (NB_ISTAT_DIP | NB_ISTAT_SIP)) {
				drv_usecwait(100);
				continue;
			}
			return (TRUE);
		} else if (ddi_check_acc_handle(glm->g_datap) != DDI_SUCCESS) {
			return (FALSE);
		}
		drv_usecwait(100);
	}
	NDBG5(("glm_wait_intr: FAILED with istat=0x%x", istat));
	return (FALSE);
}

static void
glm_process_intr(glm_t *glm, uchar_t istat)
{
	uint_t action = 0;

	NDBG1(("glm_process_intr: g_state=0x%x istat=0x%x",
	    glm->g_state, istat));

	(void) ddi_dma_sync(glm->g_dsa_dma_h, 0, 0, DDI_DMA_SYNC_FORCPU);
	/*
	 * Always clear sigp bit if it might be set
	 */
	if (glm->g_state == NSTATE_WAIT_RESEL) {
		GLM_RESET_SIGP(glm);
	}

	/*
	 * If SCSI/DMA are set then read then in order.
	 * See page 2-53 LSI 1010 for guidelines.
	 */

	if ((istat & NB_ISTAT_DIP) && (istat & NB_ISTAT_SIP)) {
		action |= GLM_SCSI_STATUS(glm);
		action |= GLM_DMA_STATUS(glm);
	} else {

		/*
		 * Analyze DMA interrupts
		 */
		if (istat & NB_ISTAT_DIP) {
			action |= GLM_DMA_STATUS(glm);
		}

		/*
		 * Analyze SCSI errors and check for phase mismatch
		 */
		if (istat & NB_ISTAT_SIP) {
			action |= GLM_SCSI_STATUS(glm);
		}
	}

	/*
	 * If no errors, no action, just restart the HBA
	 */
	if (action != 0) {
		action = glm_decide(glm, action);
	}

	/*
	 * Restart the current, or start a new, queue item
	 */
	glm_restart_hba(glm, action);
}

/*
 * glm interrupt handler
 *
 * Read the istat register first.  Check to see if a scsi interrupt
 * or dma interrupt is pending.  If that is true, handle those conditions
 * else, return DDI_INTR_UNCLAIMED.
 */
static uint_t
glm_intr(caddr_t arg)
{
	glm_t *glm = (glm_t *)arg;
	uchar_t istat;
	int count = 0;

	/*
	 * check status before processing interrupts
	 */
	if (ddi_get_devstate(glm->g_dip) == DDI_DEVSTATE_DOWN) {
		return (DDI_INTR_UNCLAIMED);
	}

	NDBG1(("glm_intr"));

	mutex_enter(&glm->g_mutex);

	/*
	 * If interrupts are shared by two channels then
	 * check whether this interrupt is genuinely for this
	 * channel by making sure first the chip is in high
	 * power state.
	 */
	if ((glm->g_options & GLM_OPT_PM) &&
	    (glm->g_power_level != PM_LEVEL_D0)) {
		mutex_exit(&glm->g_mutex);
		return (DDI_INTR_UNCLAIMED);
	}

	/*
	 * Read the istat register.
	 */
	if ((istat = INTPENDING(glm)) != 0) {
		do {
			/*
			 * clear the next interrupt status from the hardware
			 */
			glm_process_intr(glm, istat);

			/*
			 * check status before processing any more interrupts
			 */
			if (ddi_get_devstate(glm->g_dip) == DDI_DEVSTATE_DOWN) {
				glm_flush_hba(glm);
				glm_doneq_empty(glm);
				mutex_exit(&glm->g_mutex);
				return (DDI_INTR_CLAIMED);
			}

			/*
			 * run the completion routines of all the
			 * completed commands
			 */
			if (glm->g_doneq) {
				count = 0; /* things still happening */
			} else if (++count > GLM_INTRLOOP_COUNT) {
				glm_flush_hba(glm);
				glm_doneq_empty(glm);
				glm_report_fault(glm->g_dip, glm,
				    DDI_SERVICE_LOST, DDI_DEVICE_FAULT,
				    "excessive interrupt count");
				mutex_exit(&glm->g_mutex);
				return (DDI_INTR_CLAIMED);
			}
			glm_doneq_empty(glm);

		} while ((istat = INTPENDING(glm)) != 0);
	} else {
		if (glm->g_polled_intr) {
			glm->g_polled_intr = 0;
			mutex_exit(&glm->g_mutex);
			return (DDI_INTR_CLAIMED);
		}
		mutex_exit(&glm->g_mutex);
		return (DDI_INTR_UNCLAIMED);
	}

	NDBG1(("glm_intr complete"));
	if (glm->g_polled_intr) {
		glm->g_polled_intr = 0;
	}
	mutex_exit(&glm->g_mutex);
	return (DDI_INTR_CLAIMED);
}

/*
 * Called from glm_pollret when an interrupt is pending on the
 * HBA, or from the interrupt service routine glm_intr.
 * Read status back from your HBA, determining why the interrupt
 * happened.  If it's because of a completed command, update the
 * command packet that relates (you'll have some HBA-specific
 * information about tying the interrupt to the command, but it
 * will lead you back to the scsi_pkt that caused the command
 * processing and then the interrupt).
 * If the command has completed normally,
 *  1. set the SCSI status byte into *pktp->pkt_scbp
 *  2. set pktp->pkt_reason to an appropriate CMD_ value
 *  3. set pktp->pkt_resid to the amount of data not transferred
 *  4. set pktp->pkt_state's bits appropriately, according to the
 *	information you now have; things like bus arbitration,
 *	selection, command sent, data transfer, status back, ARQ
 *	done
 */
static void
glm_chkstatus(glm_t *glm, glm_unit_t *unit, struct glm_scsi_cmd *cmd)
{
	struct scsi_pkt *pkt = CMD2PKT(cmd);
	struct scsi_status *status;

	NDBG1(("glm_chkstatus: unit=0x%p devaddr=0x%p cmd=0x%p",
	    (void *)unit, (void *)glm->g_devaddr, (void *)cmd));

	/*
	 * Get status from target.
	 */
	*(pkt->pkt_scbp) = unit->nt_dsap->nt_statbuf[0];
	status = (struct scsi_status *)pkt->pkt_scbp;

#ifdef GLM_TEST
	if ((glm_test_instance == glm->g_instance) &&
	    (glm_test_arq_enable && (glm_test_arq++ > 10000))) {
		glm_test_arq = 0;
		status->sts_chk = 1;
	}

	if (glm_test_rec && (glm_test_instance == glm->g_instance) &&
	    (glm_test_rec_mask & (1<<Tgt(cmd))) == 0) {
		if (*(cmd->cmd_pkt->pkt_cdbp) != SCMD_REQUEST_SENSE) {
			status->sts_chk = 1;
			glm_test_rec_mask |= (1<<Tgt(cmd));
		}
	}
#endif

	if (status->sts_chk) {
		glm_force_renegotiation(glm, unit->nt_target);
	}

	/*
	 * back off sync/wide if there were parity errors
	 */
	if (pkt->pkt_statistics & STAT_PERR) {
		glm_sync_wide_backoff(glm, unit);
	} else {
#ifdef GLM_TEST
		if ((glm_test_instance == glm->g_instance) &&
		    (glm_ptest & (1<<pkt->pkt_address.a_target))) {
			glm_sync_wide_backoff(glm, unit);
			glm_ptest = 0;
		}
#endif
	}

	/*
	 * The active logical unit just completed an operation,
	 * pass the status back to the requester.
	 */
	if (unit->nt_goterror) {
		/* interpret the error status */
		NDBG1(("glm_chkstatus: got error"));
		GLM_CHECK_ERROR(glm, unit, pkt);

		/*
		 * Set the throttle to DRAIN_THROTTLE to make
		 * sure any disconnected commands time out
		 * in the event of drive failure.
		 */
		if ((pkt->pkt_reason == CMD_INCOMPLETE) &&
		    (glm->g_reset_delay[Tgt(cmd)] == 0)) {
			unit->nt_throttle = DRAIN_THROTTLE;
		}
	} else {
		NDBG1(("glm_chkstatus: okay"));

		/*
		 * Get residual byte count from the S/G DMA list.
		 * sync data for consistent memory xfers
		 */
		if (cmd->cmd_flags & CFLAG_DMAVALID) {
			if ((cmd->cmd_flags & CFLAG_CMDIOPB) &&
			    ((cmd->cmd_flags & CFLAG_DMASEND) == 0) &&
			    cmd->cmd_dmahandle != NULL) {
				(void) ddi_dma_sync(cmd->cmd_dmahandle, 0,
				    (uint_t)0, DDI_DMA_SYNC_FORCPU);
			}
			if (cmd->cmd_scc != 0) {
				pkt->pkt_resid = glm_sg_residual(glm,
				    unit, cmd);
				NDBG1(("glm_chkstatus: "
				    "resid=%lx dmacount=%x\n",
				    pkt->pkt_resid, cmd->cmd_dmacount));
				if (pkt->pkt_resid > cmd->cmd_dmacount) {
					pkt->pkt_resid = cmd->cmd_dmacount;
				}
			} else {
				pkt->pkt_resid = 0;
			}
			pkt->pkt_state |= STATE_XFERRED_DATA;

			/*
			 * no data xfer.
			 */
			if (pkt->pkt_resid == cmd->cmd_dmacount) {
				pkt->pkt_state &= ~STATE_XFERRED_DATA;
			}
		}

		/*
		 * XXX- Is there a more accurate way?
		 */
		pkt->pkt_state |= (STATE_GOT_BUS | STATE_GOT_TARGET
		    | STATE_SENT_CMD
		    | STATE_GOT_STATUS);
	}

	/*
	 * start an autorequest sense if there was a check condition.
	 */
	if (status->sts_chk) {
		if (glm_handle_sts_chk(glm, unit, cmd)) {
			/*
			 * we can't start an arq because one is
			 * already in progress. the target is
			 * probably confused
			 */
			glm->g_reset_received = 0;
			GLM_BUS_RESET(glm);
			(void) glm_wait_intr(glm, GLM_BUS_RESET_POLL_TIME);
			if (glm->g_reset_received == 0) {
				glm_report_fault(glm->g_dip, glm,
				    DDI_SERVICE_LOST, DDI_DEVICE_FAULT,
				    "timeout on bus reset interrupt");
				glm_flush_hba(glm);
			}
			return;
		}
	} else if ((*(char *)status & STATUS_MASK) ==
	    STATUS_QFULL) {
		glm_handle_qfull(glm, cmd);
	}

	NDBG1(("glm_chkstatus: pkt=0x%p done", (void *)pkt));
}

/*
 * handle qfull condition
 */
static void
glm_handle_qfull(struct glm *glm, struct glm_scsi_cmd *cmd)
{
	struct glm_unit *unit = PKT2GLMUNITP(CMD2PKT(cmd));
	int target = unit->nt_target;

	if ((++cmd->cmd_qfull_retries > glm->g_qfull_retries[target]) ||
	    (glm->g_qfull_retries[target] == 0)) {
		/*
		 * We have exhausted the retries on QFULL, or,
		 * the target driver has indicated that it
		 * wants to handle QFULL itself by setting
		 * qfull-retries capability to 0. In either case
		 * we want the target driver's QFULL handling
		 * to kick in. We do this by having pkt_reason
		 * as CMD_CMPLT and pkt_scbp as STATUS_QFULL.
		 */
		glm_set_all_lun_throttles(glm, target, DRAIN_THROTTLE);
	} else {
		if (glm->g_reset_delay[Tgt(cmd)] == 0) {
			unit->nt_throttle =
			    max((unit->nt_tcmds - 2), 0);
		}
		cmd->cmd_pkt->pkt_flags |= FLAG_HEAD;
		cmd->cmd_flags &= ~(CFLAG_TRANFLAG | CFLAG_CMD_REMOVED);
		cmd->cmd_flags |= CFLAG_QFULL_STATUS;
		(void) glm_accept_pkt(glm, cmd);

		/*
		 * when target gives queue full status with no commands
		 * outstanding (nt_tcmds == 0), throttle is set to 0
		 * (HOLD_THROTTLE), and the queue full handling start
		 * (see psarc/1994/313); if there are commands outstanding,
		 * throttle is set to (nt_tcmds - 2)
		 */
		if (unit->nt_throttle == HOLD_THROTTLE) {
			/*
			 * By setting throttle to QFULL_THROTTLE, we
			 * avoid submitting new commands and in
			 * glm_restart_cmd find out slots which need
			 * their throttles to be cleared.
			 */
			glm_set_all_lun_throttles(glm, target, QFULL_THROTTLE);
			if (glm->g_restart_cmd_timeid == 0) {
				glm->g_restart_cmd_timeid =
				    timeout(glm_restart_cmd, glm,
				    glm->g_qfull_retry_interval[target]);
			}
		}
	}
}

/*
 * invoked from timeout() to restart qfull cmds with throttle == 0
 */
static void
glm_restart_cmd(void *arg)
{
	struct glm *glm = arg;
	int i;
	struct glm_unit *unit;

	mutex_enter(&glm->g_mutex);

	glm->g_restart_cmd_timeid = 0;

	for (i = 0; i < N_GLM_UNITS; i++) {
		if ((unit = glm->g_units[i]) == NULL) {
			continue;
		}
		if ((glm->g_reset_delay[i/NLUNS_GLM] == 0) &&
		    (unit->nt_throttle == QFULL_THROTTLE)) {
			glm_full_throttle(glm, unit->nt_target, unit->nt_lun);
			if (unit->nt_waitq != NULL) {
				glm_queue_target(glm, unit);
			}
		}
	}
	mutex_exit(&glm->g_mutex);
}

/*
 * Some event or combination of events has occurred. Decide which
 * one takes precedence and do the appropriate HBA function and then
 * the appropriate end of request function.
 */
static uint_t
glm_decide(glm_t *glm, uint_t action)
{
	struct glm_unit *unit = glm->g_current;

	NDBG1(("glm_decide: action=%x", action));

	if (unit) {
		(void) ddi_dma_sync(unit->nt_dma_p, 0, 0, DDI_DMA_SYNC_FORCPU);
	}

	/*
	 * If multiple errors occurred do the action for
	 * the most severe error.
	 */

	/*
	 * If we received a SIR interrupt, process here.
	 */
	if (action & NACTION_CHK_INTCODE) {
		action = glm_check_intcode(glm, unit, action);
		/*
		 * If we processed a reselection, g_current could
		 * have changed.
		 */
		unit = glm->g_current;
	}

	/* if sync i/o negotiation in progress, determine new syncio state */
	if (glm->g_state == NSTATE_ACTIVE &&
	    (action & (NACTION_GOT_BUS_RESET | NACTION_DO_BUS_RESET)) == 0) {
		if (action & NACTION_SDTR) {
			action = glm_syncio_decide(glm, unit, action);
		}
	}

	/*
	 * prevent potential never-ending loop with error messages
	 */
	if (action & (NACTION_MSG_PARITY | NACTION_MSG_REJECT |
	    NACTION_INITIATOR_ERROR)) {
		if (glm->g_errmsg_retries > GLM_ERRMSG_RETRY_COUNT) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "excessive %s%s%s messages",
			    (action & NACTION_MSG_PARITY) ? "msg_parity " : "",
			    (action & NACTION_MSG_REJECT) ? "msg_reject " : "",
			    (action & NACTION_INITIATOR_ERROR) ?
			    "initiator_error " : "");
			action &= ~(NACTION_MSG_PARITY | NACTION_MSG_REJECT |
			    NACTION_INITIATOR_ERROR);
			action |= NACTION_ERR | NACTION_DO_BUS_RESET;
		}
		glm->g_errmsg_retries++;
	} else {
		glm->g_errmsg_retries = 0;
	}

	if (action & NACTION_GOT_BUS_RESET) {
		/*
		 * clear all requests waiting for reconnection.
		 */
		glm->g_reset_received = 1;
		glm_flush_hba(glm);

#ifdef GLM_TEST
		if (glm_test_stop) {
			debug_enter("glm_decide: bus reset");
		}
#endif
	}

	if (action & NACTION_SIOP_REINIT) {
		GLM_RESET(glm);
		GLM_INIT(glm);
		GLM_ENABLE_INTR(glm);
		/* the reset clears the byte counters so can't do save */
		action &= ~NACTION_SAVE_BCNT;
		NDBG1(("HBA reset: devaddr=0x%p", (void *)glm->g_devaddr));
	}

	if (action & NACTION_CLEAR_CHIP) {
		/* Reset scsi offset. */
		RESET_SCSI_OFFSET(glm);

		/* Clear the DMA FIFO pointers */
		CLEAR_DMA(glm);

		/* Clear the SCSI FIFO pointers */
		CLEAR_SCSI_FIFO(glm);
	}

	if (action & NACTION_SIOP_HALT) {
		GLM_HALT(glm);
		NDBG1(("HBA halt: devaddr=0x%p", (void *)glm->g_devaddr));
	}

	if (action & NACTION_DO_BUS_RESET) {
		/*
		 * prevent potential never-ending loop with resets
		 */
		if (glm->g_reset_retries > GLM_RESET_RETRY_COUNT) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_LOST, DDI_DEVICE_FAULT,
			    "excessive SCSI bus resets");
			glm_flush_hba(glm);
		} else {
			glm->g_reset_retries++;
			if (glm->g_reset_recursion) {
				glm_report_fault(glm->g_dip, glm,
				    DDI_SERVICE_LOST, DDI_DEVICE_FAULT,
				    "SCSI bus reset recursion");
				glm_flush_hba(glm);
			} else {
				glm->g_reset_recursion++;
				glm->g_reset_received = 0;
				GLM_BUS_RESET(glm);
				(void) glm_wait_intr(glm,
				    GLM_BUS_RESET_POLL_TIME);
				if (glm->g_reset_received == 0) {
					glm_report_fault(glm->g_dip, glm,
					    DDI_SERVICE_LOST, DDI_DEVICE_FAULT,
					    "timeout on bus reset interrupt");
					glm_flush_hba(glm);
				}
				glm->g_reset_recursion--;
			}
		}

		/* clear invalid actions, if any */
		action &= NACTION_DONE | NACTION_ERR | NACTION_DO_BUS_RESET |
		    NACTION_BUS_FREE;

		NDBG22(("bus reset: devaddr=0x%p", (void *)glm->g_devaddr));
	} else {
		glm->g_reset_recursion = 0;
		glm->g_reset_retries = 0;
	}

	if (action & NACTION_SAVE_BCNT) {
		/*
		 * Save the state of the data transfer scatter/gather
		 * for possible later reselect/reconnect.
		 */
		if (!GLM_SAVE_BYTE_COUNT(glm, unit)) {
			/* if this isn't an interrupt during a S/G dma */
			/* then the target changed phase when it shouldn't */
			NDBG1(("glm_decide: phase mismatch: devaddr=0x%p",
			    (void *)glm->g_devaddr));
		}
	}

	/*
	 * Check to see if the current request has completed.
	 * If the HBA isn't active it can't be done, we're probably
	 * just waiting for reselection and now need to reconnect to
	 * a new target.
	 */
	if (glm->g_state == NSTATE_ACTIVE) {
		action = glm_ccb_decide(glm, unit, action);
	}
	return (action);
}

static uint_t
glm_ccb_decide(glm_t *glm, glm_unit_t *unit, uint_t action)
{
	struct glm_scsi_cmd *cmd;
	int target = unit->nt_target;
	ushort_t tshift = (1 << unit->nt_target);

	NDBG1(("glm_ccb_decide: unit=0x%p action=%x", (void *)unit, action));

	if (action & NACTION_ERR) {
		/* error completion, save all the errors seen for later */
		unit->nt_goterror = TRUE;
	} else if ((action & NACTION_DONE) == 0) {
		/* the target's state hasn't changed */
		return (action);
	}

	/*
	 * If this target has more requests and there is no reset
	 * delay pending, then requeue it fifo order
	 */
	if ((unit->nt_waitq != NULL) &&
	    (glm->g_reset_delay[target] == 0) &&
	    ((unit->nt_state & NPT_STATE_QUEUED) == 0)) {
		glm_addbq(glm, unit);
	}

	/*
	 * The unit is now idle.
	 */
	unit->nt_state &= ~NPT_STATE_ACTIVE;

	/*
	 * If no active request then just return.  This happens with
	 * a proxy cmd like Device Reset.  NINT_DEV_RESET and
	 * glm_flush_target clean up the proxy cmd and proxy cmds do not
	 * have completion routine.
	 */
	if ((cmd = unit->nt_ncmdp) == NULL) {
		goto exit;
	}

	/*
	 * If the proxy cmd did not get cleaned up in NINT_DEV_RESET,
	 * it probably failed.  Do not try to remove proxy cmd in
	 * nt_slot[] since it will not be there.
	 */
	if ((cmd->cmd_flags & CFLAG_CMDPROXY) == 0) {
		ASSERT(cmd == unit->nt_active->nt_slot[cmd->cmd_tag[1]]);
	}
	glm_remove_cmd(glm, unit, cmd);

	/* post the completion status into the scsi packet */
	ASSERT(cmd != NULL && unit != NULL);
	glm_chkstatus(glm, unit, cmd);

	/*
	 * See if an ARQ was fired off.  If so, don't complete
	 * this pkt, glm_complete_arq_pkt will do that.
	 */
	if (cmd->cmd_flags & CFLAG_ARQ_IN_PROGRESS) {
		action |= NACTION_ARQ;
	} else if (cmd->cmd_flags & CFLAG_QFULL_STATUS) {
		/*
		 * The target returned QFULL, do not add tihs
		 * pkt to the doneq since the hba will retry
		 * this cmd.
		 *
		 * The pkt has already been resubmitted in
		 * glm_handle_qfull().  Remove this cmd_flag here.
		 */
		cmd->cmd_flags &= ~CFLAG_QFULL_STATUS;
	} else {
		/*
		 * Add the completed request to end of the done queue
		 */
		glm_doneq_add(glm, cmd);

		/*
		 * if ppr_sent is still set some how the
		 * target is misbehaving. To allow
		 * this target to work with glm
		 * do not send ppr again
		 */
		if (glm->g_ppr_sent & tshift) {
			glm->g_ppr_supported &= ~tshift;
			glm->g_ppr_known |= tshift;
			glm->g_ppr_sent &= ~tshift;
			glm->g_wide_known &= ~tshift;
		}
	}
exit:
	glm->g_current = NULL;
	glm->g_state = NSTATE_IDLE;

	NDBG1(("glm_ccb_decide: end."));
	return (action);
}

static void
glm_remove_cmd(struct glm *glm, struct glm_unit *unit,
    struct glm_scsi_cmd *cmd)
{
	int tag = cmd->cmd_tag[1];
	struct nt_slots *tag_slots = unit->nt_active;

	ASSERT(cmd != NULL);
	ASSERT(cmd->cmd_queued == FALSE);

	/*
	 * clean up the tagged and untagged case.
	 */
	if (cmd == tag_slots->nt_slot[tag]) {
		tag_slots->nt_slot[tag] = NULL;
		ASSERT((cmd->cmd_flags & CFLAG_CMD_REMOVED) == 0);
		cmd->cmd_flags |= CFLAG_CMD_REMOVED;
		/*
		 * could be the current cmd.
		 */
		if (unit->nt_ncmdp == cmd) {
			unit->nt_ncmdp = NULL;
		}
		unit->nt_tcmds--;
	}

	/*
	 * clean up any proxy cmd that doesn't occupy
	 * a slot in nt_slot.
	 */
	if (unit->nt_ncmdp && (unit->nt_ncmdp == cmd) &&
	    (cmd->cmd_flags & CFLAG_CMDPROXY)) {
		ASSERT((cmd->cmd_flags & CFLAG_CMD_REMOVED) == 0);
		cmd->cmd_flags |= CFLAG_CMD_REMOVED;
		unit->nt_ncmdp = NULL;
	}

	if (cmd->cmd_flags & CFLAG_CMDDISC) {
		cmd->cmd_flags &= ~CFLAG_CMDDISC;
		glm->g_ndisc--;
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
	if (cmd->cmd_pkt->pkt_time == tag_slots->nt_timebase) {
		if (--(tag_slots->nt_dups) <= 0) {
			if (unit->nt_tcmds) {
				struct glm_scsi_cmd *ssp;
				uint_t n = 0;
				ushort_t t = tag_slots->nt_n_slots;
				ushort_t i;
				/*
				 * This crude check assumes we don't do
				 * this too often which seems reasonable
				 * for block and raw I/O.
				 */
				for (i = 0; i < t; i++) {
					ssp = tag_slots->nt_slot[i];
					if (ssp &&
					    (ssp->cmd_pkt->pkt_time > n)) {
						n = ssp->cmd_pkt->pkt_time;
						tag_slots->nt_dups = 1;
					} else if (ssp &&
					    (ssp->cmd_pkt->pkt_time == n)) {
						tag_slots->nt_dups++;
					}
				}
				tag_slots->nt_timebase = n;
			} else {
				tag_slots->nt_dups = 0;
				tag_slots->nt_timebase = 0;
			}
		}
	}
	tag_slots->nt_timeout = tag_slots->nt_timebase;

	ASSERT(cmd != unit->nt_active->nt_slot[cmd->cmd_tag[1]]);
}

static uint_t
glm_check_intcode(glm_t *glm, glm_unit_t *unit, uint_t action)
{
	struct glm_scsi_cmd *cmd;
	struct scsi_pkt *pkt;
	glm_unit_t *re_unit;
	uint_t intcode;
	char *errmsg;
	uchar_t width;
	ushort_t tag = 0;
	ushort_t tshift = 0;
	uchar_t moremsgin;

	NDBG1(("glm_check_intcode: unit=0x%p action=%x", (void *)unit, action));

	if (action & (NACTION_GOT_BUS_RESET
	    | NACTION_SIOP_HALT
	    | NACTION_SIOP_REINIT | NACTION_BUS_FREE
	    | NACTION_DONE | NACTION_ERR)) {
		return (action);
	}

	/* SCRIPT interrupt instruction */
	/* Get the interrupt vector number */
	intcode = GLM_GET_INTCODE(glm);

	if (unit == NULL && ((intcode != NINT_RESEL &&
	    intcode != NINT_SIGPROC) || glm->g_state != NSTATE_WAIT_RESEL)) {
		glm_report_fault(glm->g_dip, glm,
		    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
		    "glm invalid interrupt while disconnected");
		return (NACTION_DO_BUS_RESET | action);
	}

	NDBG1(("glm_check_intcode: intcode=%x", intcode));

	switch (intcode) {
	default:
		break;

	case NINT_OK:
		if (ddi_get8(glm->g_datap, (uint8_t *)(glm->g_devaddr +
		    NREG_SCRATCHA0)) == 0) {
			/* the dma list has completed */
			cmd = glm->g_current->nt_ncmdp;
			cmd->cmd_scc = 0;
		}
		return (NACTION_DONE | action);

	case NINT_PMM:
		if ((glm->g_devid == GLM_53c1010_33) ||
		    (glm->g_devid == GLM_53c1010_66)) {
			cmd = glm->g_current->nt_ncmdp;
			cmd->cmd_pmm_addr = ddi_get32(glm->g_datap,
			    (uint32_t *)(glm->g_devaddr + NREG_UA));
			cmd->cmd_pmm_count = ddi_get32(glm->g_datap,
			    (uint32_t *)(glm->g_devaddr + NREG_RBC));
			cmd->cmd_flags |= CFLAG_PMM_RECEIVED;
		}
		return (action);

	case NINT_SDP_MSG:
		/* Save Data Pointers msg */
		NDBG1(("\t\tintcode SDP"));
		if (ddi_get8(glm->g_datap, (uint8_t *)(glm->g_devaddr +
		    NREG_SCRATCHA0)) == 0) {
			/* the dma list has completed */
			cmd = glm->g_current->nt_ncmdp;
			cmd->cmd_scc = 0;
		}
		cmd = glm->g_current->nt_ncmdp;
		if (((glm->g_devid == GLM_53c1010_33) ||
		    (glm->g_devid == GLM_53c1010_66)) &&
		    (cmd->cmd_flags & CFLAG_PMM_RECEIVED)) {
			cmd->cmd_saved_addr = cmd->cmd_pmm_addr;
			cmd->cmd_saved_count = cmd->cmd_pmm_count;
			cmd->cmd_flags &= ~CFLAG_PMM_RECEIVED;
		} else {
			if (cmd->cmd_scc > 0) {
				/* for wx nits */
				uint_t scc = cmd->cmd_scc;
				cmd->cmd_sg[scc - 1].address =
				    ddi_get32(unit->nt_accessp,
				    &unit->nt_dsap->nt_data[scc-1].address);
				cmd->cmd_sg[scc - 1].count =
				    ddi_get32(unit->nt_accessp,
				    &unit->nt_dsap->nt_data[scc-1].count);
			}
		}
		cmd->cmd_saved_cookie = cmd->cmd_scc;
		cmd->cmd_flags |= CFLAG_RESTORE_PTRS;


		return (NACTION_ACK | action);

	case NINT_DISC:
		/* remove this target from the top of queue */
		NDBG1(("\t\tintcode DISC"));

		cmd = glm->g_current->nt_ncmdp;
		glm->g_current->nt_ncmdp = NULL;
		pkt = CMD2PKT(cmd);
		cmd->cmd_flags |= CFLAG_CMDDISC;
		pkt->pkt_statistics |= STAT_DISCON;

		if (ddi_get8(glm->g_datap, (uint8_t *)(glm->g_devaddr +
		    NREG_SCRATCHA0)) == 0) {
			/* the dma list has completed */
			cmd->cmd_scc = 0;
		}

		glm->g_ndisc++;
		unit->nt_state &= ~NPT_STATE_ACTIVE;
		glm->g_state = NSTATE_IDLE;
		glm->g_current = NULL;
		cmd->cmd_flags |= CFLAG_RESTORE_PTRS;

		/*
		 * Some disks do not send a save data ptr on disconnect
		 * if all data has been xferred.  Therefore, do not
		 * restore pointers on reconnect.
		 */
		if ((cmd->cmd_flags & CFLAG_DMAVALID) &&
		    (cmd->cmd_scc == 0) &&
		    (cmd->cmd_scc != cmd->cmd_saved_cookie)) {
			cmd->cmd_flags &= ~CFLAG_RESTORE_PTRS;
		}

		return (action);

	case NINT_RP_MSG:
		/* Restore Data Pointers */
		NDBG1(("\t\tintcode RP"));
		ASSERT(glm->g_current == unit);
		ASSERT(unit->nt_ncmdp != NULL);
		cmd = glm->g_current->nt_ncmdp;
		if ((glm->g_devid == GLM_53c1010_33) ||
		    (glm->g_devid == GLM_53c1010_66)) {
				cmd->cmd_scc = cmd->cmd_saved_cookie;
				if (cmd->cmd_scc > 0) {
					/* for wx nits */
					uint_t scc = cmd->cmd_scc;

					cmd->cmd_sg[scc - 1].address =
					    cmd->cmd_saved_addr;
					cmd->cmd_sg[scc - 1].count =
					    cmd->cmd_saved_count;
				}
				glm_dsa_dma_setup(CMD2PKT(cmd), unit, glm);
			} else {
				cmd->cmd_scc = cmd->cmd_saved_cookie;
				glm_dsa_dma_setup(CMD2PKT(cmd), unit, glm);
		}
		return (NACTION_ACK | action);

	case NINT_RESEL:
		/* reselected by a disconnected target */

		NDBG1(("\t\tintcode RESEL"));
		/*
		 * One of two situations:
		 */
		switch (glm->g_state) {
		case NSTATE_ACTIVE:
		{
			/*
			 * If glm was trying to initiate a sdtr or wdtr or ppr
			 * and got preempted, set the _known and _sent flags
			 * appropriately so that we will initiate sdtr/wdtr/ppr
			 * when the command is restarted.
			 */
			if (NSYNCSTATE(glm, unit) == NSYNC_SDTR_SENT) {
				NSYNCSTATE(glm, unit) = NSYNC_SDTR_NOTDONE;
			}
			if (glm->g_wdtr_sent) {
				glm->g_wide_known &= ~(1<<unit->nt_target);
				glm->g_wdtr_sent = 0;
			}
			if (glm->g_ppr_sent & (1<<unit->nt_target)) {
				NDBG31(("Resetting ppr flags for renegotiation"
				    " on preempted command. target=%d\n",
				    unit->nt_target));
				glm->g_ppr_known &= ~(1<<unit->nt_target);
				glm->g_ppr_sent &= ~(1<<unit->nt_target);
			}

			/*
			 * First, Remove cmd from nt_slots since it was
			 * preempted.  Then, re-queue this cmd/unit.
			 */
			cmd = unit->nt_ncmdp;

			/*
			 * remove cmd.
			 */
			ASSERT((cmd->cmd_flags & CFLAG_CMD_REMOVED) == 0);
			glm_remove_cmd(glm, unit, cmd);
			cmd->cmd_flags &= ~CFLAG_CMD_REMOVED;

			ASSERT(unit->nt_state & NPT_STATE_ACTIVE);
			unit->nt_state &= ~NPT_STATE_ACTIVE;

			/*
			 * re-queue this cmd.
			 */
			glm_waitq_add_lifo(unit, cmd);
			if ((unit->nt_state & NPT_STATE_QUEUED) == 0) {
				glm_addfq(glm, unit);
			}

			/*
			 * Pretend we were waiting for reselection
			 */
			glm->g_state = NSTATE_WAIT_RESEL;

			break;
		}
		case NSTATE_WAIT_RESEL:
			/*
			 * The DSA register has the address of the hba's
			 * dsa structure.  grab the incomming bytes and
			 * reconnect.
			 */
			break;

		default:
			/* should never happen */
			NDBG1(("\t\tintcode RESEL botched"));
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "reselect in invalid state");
			return (NACTION_DO_BUS_RESET | action);
		}

		/*
		 * Get target structure of device that wants to reconnect
		 */
		if ((re_unit = glm_get_target(glm)) == NULL) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "invalid reselection");
			return (NACTION_DO_BUS_RESET | action);
		}

		/*
		 * A target disconnected without notifying us.
		 *
		 * There could have been a parity error received on
		 * the scsi bus <msg-in, disconnect msg>.  In this
		 * case, the hba would have assert ATN and sent out
		 * a MSG_MSG_PARITY msg.  The target could have
		 * accepted this msg and gone bus free.  The target
		 * should have tried to resend the msg-in disconnect
		 * msg.  However, since the target disconnected, the
		 * the hba and target are not in agreement about the
		 * the target's state.
		 *
		 * Reset the bus.
		 */
		if (re_unit->nt_state & NPT_STATE_ACTIVE) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "invalid reselection (%d.%d)", re_unit->nt_target,
			    re_unit->nt_lun);
			cmn_err(CE_WARN,
			    "!ID[SUNWpd.glm.check_intcode.6002]");
			return (NACTION_DO_BUS_RESET | action);
		}

		unit = re_unit;

		(void) ddi_dma_sync(unit->nt_dma_p, 0, 0, DDI_DMA_SYNC_FORCPU);

		/*
		 * read moremsginbuf now so we can validate against tag
		 * before we proceed any further
		 */
		moremsgin = glm->g_dsa->g_moremsginbuf[0];

		/*
		 * If tag queueing in use, pick up the tag byte.
		 */
		if (TAGGED(unit->nt_target) && unit->nt_tcmds &&
		    (unit->nt_active->nt_slot[0] == NULL)) {
			tag = glm->g_dsa->g_taginbuf[0];
			if (tag == 0) {
				switch (moremsgin) {
				case MSG_SIMPLE_QTAG:
				case MSG_HEAD_QTAG:
				case MSG_ORDERED_QTAG:
					glm_report_fault(glm->g_dip, glm,
					    DDI_SERVICE_UNAFFECTED,
					    DDI_DEVICE_FAULT, "invalid "
					    "reselection - bad tag (%d.%d)",
					    re_unit->nt_target,
					    re_unit->nt_lun);
					return (NACTION_DO_BUS_RESET | action);
				default:
					break;
				}
			}
			glm->g_dsa->g_taginbuf[0] = 0;
		}

		/*
		 * The target is reconnecting with a cmd that
		 * we don't know about.  This could happen if a cmd
		 * that we think is a disconnect cmd timeout suddenly
		 * reconnects and tries to finish.
		 *
		 * The cmd could have been aborted software-wise, but
		 * this cmd is reconnecting from the target before the
		 * abort-msg has has gone out on the bus.
		 */
		if (unit->nt_active->nt_slot[tag] == NULL) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "invalid reselection (%d.%d)", re_unit->nt_target,
			    re_unit->nt_lun);
			return (NACTION_DO_BUS_RESET | action);
		}

		/*
		 * tag == 0 if not using tq.
		 */
		unit->nt_ncmdp = cmd = unit->nt_active->nt_slot[tag];

		/*
		 * One less outstanding disconnected target
		 */
		glm->g_ndisc--;
		cmd->cmd_flags &= ~CFLAG_CMDDISC;

		/*
		 * Implicit restore data pointers
		 */
		if (unit->nt_ncmdp->cmd_flags & CFLAG_RESTORE_PTRS) {
			if ((glm->g_devid == GLM_53c1010_33) ||
			    (glm->g_devid == GLM_53c1010_66)) {
				cmd->cmd_scc = cmd->cmd_saved_cookie;
				if (cmd->cmd_scc > 0) {
					/* for wx nits */
					uint_t scc = cmd->cmd_scc;
					cmd->cmd_sg[scc - 1].address =
					    cmd->cmd_saved_addr;
					cmd->cmd_sg[scc - 1].count =
					    cmd->cmd_saved_count;
				}
			}
			cmd->cmd_scc = cmd->cmd_saved_cookie;
			glm_dsa_dma_setup(CMD2PKT(cmd), unit, glm);
		}

		/*
		 * Make this target the active target.
		 */
		glm->g_current = unit;
		glm->g_state = NSTATE_ACTIVE;
		glm->g_wdtr_sent = 0;

		switch (moremsgin) {
		case MSG_SIMPLE_QTAG:
		case MSG_HEAD_QTAG:
		case MSG_ORDERED_QTAG:
			action |= NACTION_ACK;
			break;
		case MSG_EXTENDED:
			unit->nt_state |= NPT_STATE_ACTIVE;
			action |= NACTION_EXT_MSG_IN;
			break;
		case MSG_DISCONNECT:
			/*
			 * The target reselected and disconnected.
			 */
			glm->g_ndisc++;
			cmd->cmd_flags |= CFLAG_CMDDISC;
			glm->g_state = NSTATE_IDLE;
			glm->g_current = NULL;
			unit->nt_ncmdp = NULL;
			break;
		default:
			action |= NACTION_ACK;
			break;
		}
		glm->g_dsa->g_moremsginbuf[0] = 0;
		return (action);

	case NINT_SIGPROC:
		/* Give up waiting, start up another target */
		if (glm->g_state != NSTATE_WAIT_RESEL) {
			/* big trouble, bus reset time ? */
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "SIGPROC but not waiting for reselection");
			return (NACTION_DO_BUS_RESET | NACTION_ERR | action);
		}
		NDBG1(("%s", (GLM_GET_ISTAT(glm) & NB_ISTAT_CON)
		    ? "glm: connected after sigproc"
		    : ""));
		glm->g_state = NSTATE_IDLE;
		return (action);

	case NINT_SDTR:
		if (glm->g_state != NSTATE_ACTIVE) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "SDTR but not active");
			/* reset the bus */
			return (NACTION_DO_BUS_RESET);

		}
		switch (NSYNCSTATE(glm, unit)) {
		default:
			/* reset the bus */
			NDBG1(("\t\tintcode SDTR state botch"));
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "SDTR but invalid NSYNCSTATE");
			return (NACTION_DO_BUS_RESET | NACTION_ERR | action);

		case NSYNC_SDTR_REJECT:
			/*
			 * glm is not doing sdtr, however, the disk initiated
			 * a sdtr message, respond with async (per the scsi
			 * spec).
			 */
			NDBG31(("\t\tintcode SDTR reject"));
			break;

		case NSYNC_SDTR_DONE:
			/* target wants to renegotiate */
			NDBG31(("\t\tintcode SDTR done, renegotiating"));
			glm_syncio_reset(glm, unit);
			NSYNCSTATE(glm, unit) = NSYNC_SDTR_RCVD;
			break;

		case NSYNC_SDTR_NOTDONE:
			/* target initiated negotiation */
			NDBG31(("\t\tintcode SDTR notdone"));
			glm_syncio_reset(glm, unit);
			NSYNCSTATE(glm, unit) = NSYNC_SDTR_RCVD;
			break;

		case NSYNC_SDTR_SENT:
			/* target responded to my negotiation */
			NDBG31(("\t\tintcode SDTR sent"));
			break;
		}
		return (NACTION_SDTR | action);

	case NINT_NEG_REJECT:
		NDBG31(("\t\tintcode NEG_REJECT "));

		/*
		 * A sdtr or wdtr response was rejected.  We need to
		 * figure out what the driver was negotiating and
		 * either disable wide or disable sync.
		 */

		/*
		 * If target rejected WDTR, revert to narrow.
		 */
		if (unit->nt_msgouttype == MSG_WIDE_DATA_XFER) {
			glm_set_wide_scntl3(glm, unit, 0);
			glm->g_wdtr_sent = 0;
		}

		/*
		 * If target rejected SDTR:
		 * Set all LUNs on this target to async i/o
		 */
		if (unit->nt_msgouttype == MSG_SYNCHRONOUS) {
			glm_syncio_state(glm, unit, NSYNC_SDTR_REJECT, 0, 0);
		}
		return (NACTION_ACK | action);

	case NINT_PPR:
		NDBG31(("\t\tintcode NINT_PPR"));

		tshift = 1 << unit->nt_target;
		glm->g_ppr_sent &= ~tshift;

		if (glm_ppr_enable(glm, unit)) {
			GLM_SET_SYNCIO(glm, unit);
			/*
			 * Check to see whether PPR was initiated by us or
			 * Target. If it is target then we need to return
			 * message(s) received
			 */
			if (unit->nt_msgouttype == MSG_PARALLEL_PROTOCOL) {
				action |= NACTION_ACK;
				unit->nt_msgouttype = 0;
			} else {
				action |= NACTION_EXT_MSG_OUT;
			}
		} else {
			NSYNCSTATE(glm, unit) = NSYNC_SDTR_REJECT;
			action |= NACTION_MSG_REJECT;
		}

		return (action);

	case NINT_WDTR:
		NDBG31(("\t\tintcode NINT_WDTR"));
		/*
		 * Get the byte sent back by the target.
		 */
		width = (unit->nt_dsap->nt_wideibuf[1] & 0xff);

		/*
		 * If we negotiated wide (or narrow), sync negotiation
		 * was lost.  Re-negotiate sync after wdtr.
		 */
		if ((++(glm->g_wdtr_sent)) & 1) {
			/*
			 * send back a wdtr response if this is
			 * a wide bus, otherwise reject the wdtr
			 * message.
			 */
			if (glm->g_options & GLM_OPT_WIDE_BUS) {
				unit->nt_dsap->nt_sendmsg.count = 0;
				unit->nt_msgoutcount = 0;
				unit->nt_msgouttype = 0;
				glm_make_wdtr(glm, unit, width);
				action |= NACTION_EXT_MSG_OUT;
			} else {
				action |= NACTION_MSG_REJECT;
			}
		} else {
			/*
			 * the target returned a width we don't
			 * support.  reject this message.
			 */
			if (width > 1) {
				return (action | NACTION_MSG_REJECT);
			}
			glm_set_wide_scntl3(glm, unit, width);
			glm->g_wdtr_sent = 0;
			if (NSYNCSTATE(glm, unit) != NSYNC_SDTR_REJECT) {
				action |= NACTION_SDTR;
			} else {
				action |= NACTION_ACK;
			}
		}
		glm->g_props_update |= (1<<unit->nt_target);
		glm_syncio_reset(glm, unit);

		return (action);

	case NINT_MSGREJ:
		NDBG31(("\t\tintcode NINT_MSGREJ"));
		if (glm->g_state != NSTATE_ACTIVE) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "MSGREJ but not active");
			/* reset the bus */
			return (NACTION_DO_BUS_RESET);
		}

		tshift = 1 << unit->nt_target;

		if (glm->g_ppr_sent & tshift) {
			/* target does not support PPR */
			NDBG31(("PPR msg rejected by target."));
			glm->g_ppr_supported &= ~tshift;
			glm->g_ppr_known |= tshift;
			glm->g_ppr_sent &= ~tshift;
			glm->g_wide_known &= ~tshift;
			return (NACTION_ACK | action);
		}

		/*
		 * If the target rejects our sync msg, set the
		 * sync state to async and never negotiate sync again.
		 */
		if (NSYNCSTATE(glm, unit) == NSYNC_SDTR_SENT) {
			/* the target can't handle sync i/o */
			NDBG31(("sync msg rejected from target."));
			glm_syncio_state(glm, unit, NSYNC_SDTR_REJECT, 0, 0);
			return (NACTION_ACK | action);
		}

		/*
		 * If the target rejects our wdtr msg, set the
		 * wide state to narrow and never negotiate wide again.
		 */
		if (glm->g_wdtr_sent) {
			NDBG31(("wdtr msg rejected from target."));
			glm_set_wide_scntl3(glm, unit, 0);
			glm->g_wide_enabled &= ~(1<<unit->nt_target);
			glm->g_nowide |= (1<<unit->nt_target);
			glm->g_wdtr_sent = 0;
			return (NACTION_ACK | action);
		}

		glm_report_fault(glm->g_dip, glm,
		    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
		    "MSGREJ not expected");
		return (NACTION_DO_BUS_RESET | NACTION_ERR | action);

	case NINT_IWR:
	{
		/*
		 * If the ignore wide residue msg is received and there is
		 * more data to xfer, fix our data/count pointers.
		 */
		int index = ddi_get8(glm->g_datap,
		    (uint8_t *)(glm->g_devaddr + NREG_SCRATCHA0));
		int residue = unit->nt_dsap->nt_msginbuf[0];
		struct glm_dsa *dsap = unit->nt_dsap;

		NDBG22(("\t\tintcode NINT_IWR"));

		ASSERT(glm->g_current->nt_ncmdp != NULL);
		cmd = glm->g_current->nt_ncmdp;

		if (index < 0 || index > cmd->cmd_cookiec) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "IWR index out of range");
			return (NACTION_DO_BUS_RESET | NACTION_ERR | action);
		}

		/*
		 * if this is the last xfer and the count was even then
		 * update the pointers
		 */
		if ((index == 0) && (residue == 1) &&
		    ((dsap->nt_data[index].count & 1) == 0)) {
			glmti_t	*sgp;
			if (dsap->nt_data[index].address +
			    dsap->nt_data[index].count >
			    cmd->cmd_sg[index].address +
			    cmd->cmd_sg[index].count) {
				glm_report_fault(glm->g_dip, glm,
				    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
				    "IWR address/count out of range");
				return (NACTION_DO_BUS_RESET | NACTION_ERR |
				    action);
			}
			sgp = &dsap->nt_data[index];
			sgp->address += sgp->count - 1;
			sgp->count = 1;
			glm->g_current->nt_ncmdp->cmd_scc = 1;
			ddi_put8(glm->g_datap, (uint8_t *)(glm->g_devaddr +
			    NREG_SCRATCHA0), 1);
		/*
		 * if there is more data to be transferred, fix the address
		 * and count of the current segment
		 */
		} else if ((index != 0) && (residue == 1)) {
			glmti_t	*sgp;
			uint32_t addr;

			/*
			 * we do not need to check on even byte xfer
			 * count here
			 */
			index = cmd->cmd_cookiec - index;
			if (((glm->g_devid == GLM_53c1010_33) ||
			    (glm->g_devid == GLM_53c1010_66)) &&
			    (cmd->cmd_flags & CFLAG_PMM_RECEIVED)) {

				addr = cmd->cmd_pmm_addr;
			} else {
				addr = dsap->nt_data[index].address;
			}
			if (addr <= cmd->cmd_sg[index].address) {
				glm_report_fault(glm->g_dip, glm,
				    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
				    "IWR still at base address");
				return (NACTION_DO_BUS_RESET | NACTION_ERR |
				    action);
			}
			if (((glm->g_devid == GLM_53c1010_33) ||
			    (glm->g_devid == GLM_53c1010_66)) &&
			    (cmd->cmd_flags & CFLAG_PMM_RECEIVED)) {
				cmd->cmd_pmm_addr--;
				cmd->cmd_pmm_count++;
			} else {
				sgp = &dsap->nt_data[index];
				sgp->address--;
				sgp->count++;
			}
		}
		return (NACTION_ACK | action);
	}

	case NINT_DEV_RESET:
		NDBG22(("\t\tintcode NINT_DEV_RESET"));

		ASSERT(unit->nt_ncmdp != NULL);
		cmd = unit->nt_ncmdp;

		if (cmd->cmd_flags & CFLAG_CMDPROXY) {
			NDBG22(("proxy cmd completed"));
			cmd->cmd_cdb[GLM_PROXY_RESULT] = TRUE;
		}

		glm_remove_cmd(glm, unit, cmd);
		glm_doneq_add(glm, cmd);

		switch (cmd->cmd_type) {
		case NRQ_DEV_RESET:
			/*
			 * clear requests for all the LUNs on this device
			 */
			glm_flush_target(glm, unit->nt_target,
			    CMD_RESET, STAT_DEV_RESET);
			NDBG22((
			"glm_check_intcode: bus device reset completed"));
			break;
		case NRQ_ABORT:
			glm_flush_lun(glm, unit, CMD_ABORTED, STAT_ABORTED);
			NDBG23(("glm_check_intcode: abort completed"));
			break;
		default:
			glm_log(glm, CE_WARN,
			    "invalid interrupt for device reset or abort");
			/*
			 * XXXX not sure what to return here?
			 */
			return (NACTION_DONE | NACTION_CLEAR_CHIP | action);
		}
		return (NACTION_DONE | NACTION_CLEAR_CHIP | action);
	}

	/*
	 * All of the interrupt codes handled above are the of
	 * the "expected" non-error type. The following interrupt
	 * codes are for unusual errors detected by the SCRIPT.
	 * For now treat all of them the same, mark the request
	 * as failed due to an error and then reset the SCSI bus.
	 *
	 * Some of these errors should be getting BUS DEVICE RESET
	 * rather than bus_reset.
	 */
	switch (intcode) {
	case NINT_UNS_MSG:
		cmn_err(CE_WARN, "!ID[SUNWpd.check_intcode.6003]");
		errmsg = "got an unsupported message";
		break;
	case NINT_ILI_PHASE:
		errmsg = "got incorrect phase";
		break;
	case NINT_UNS_EXTMSG:
		cmn_err(CE_WARN, "!ID[SUNWpd.check_intcode.6004]");
		errmsg = "got unsupported extended message";
		break;
	case NINT_MSGIN:
		/*
		 * If the target dropped the bus during reselection,
		 * the dsa register has the address of the hba.  Use
		 * the SSID reg and msg-in phase to try to determine
		 * which target dropped the bus.
		 */
		if ((unit = glm_get_target(glm)) == NULL) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "invalid reselection");
			return (NACTION_DO_BUS_RESET | action);
		}
		errmsg = "Message-In was expected";
		break;
	case NINT_MSGREJ:
		errmsg = "got unexpected Message Reject";
		break;
	case NINT_REJECT:
		errmsg = "unable to send Message Reject";
		break;
	case NINT_TOOMUCHDATA:
		ASSERT(glm->g_current == unit);
		ASSERT(unit->nt_ncmdp != NULL);
		cmd = glm->g_current->nt_ncmdp;

		cmd->cmd_scc = 0;

		glm_dsa_dma_setup(CMD2PKT(cmd), unit, glm);

		if (cmd->cmd_scc > 0)
			return (action);
		else {
			errmsg = "data overrun: got too much data from target";
			break;	/* this is an error */
		}
	default:
		cmn_err(CE_WARN, "!ID[SUNWpd.check_intcode.6007]");
		glm_log(glm, CE_WARN, "invalid intcode=%u", intcode);
		errmsg = "default";
		break;
	}

	if (unit != NULL) {
		glm_report_fault(glm->g_dip, glm,
		    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
		    "Resetting scsi bus, %s from (%d,%d)",
		    errmsg, unit->nt_target, unit->nt_lun);
		glm_sync_wide_backoff(glm, unit);
	} else {
		glm_report_fault(glm->g_dip, glm,
		    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
		    "Resetting scsi bus, %s.",
		    errmsg);
	}
	return (NACTION_DO_BUS_RESET | NACTION_ERR);
}

/*
 * figure out the recovery for a parity interrupt or a SGE interrupt.
 */
static uint_t
glm_parity_check(struct glm *glm, struct glm_unit *unit)
{
	ushort_t phase;
	uint_t action = NACTION_ERR;
	struct glm_scsi_cmd *cmd;

	ASSERT(unit->nt_ncmdp != NULL);
	cmd = unit->nt_ncmdp;

	NDBG31(("glm_parity_check: cmd=0x%p", (void *)cmd));

	/*
	 * Get the phase of the parity error.
	 */
	phase = (ddi_get8(glm->g_datap,
	    (uint8_t *)(glm->g_devaddr + NREG_DCMD)) & 0x7);

	switch (phase) {
	case NSOP_MSGIN:
		glm_log(glm, CE_WARN,
		    "SCSI bus MESSAGE IN phase parity error");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.parity_check.6008]");
		glm_set_pkt_reason(glm, cmd, CMD_CMPLT, STAT_PERR);
		action = NACTION_MSG_PARITY;
		break;
	case NSOP_MSGOUT:
		glm_report_fault(glm->g_dip, glm,
		    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
		    "SCSI bus MESSAGE OUT phase parity error");
		action = (NACTION_CLEAR_CHIP |
		    NACTION_DO_BUS_RESET | NACTION_ERR);
		break;
	case NSOP_COMMAND:
		action = (NACTION_INITIATOR_ERROR);
		break;
	case NSOP_STATUS:
		glm_set_pkt_reason(glm, cmd, CMD_TRAN_ERR, STAT_PERR);
		glm_log(glm, CE_WARN, "SCSI bus STATUS phase parity error");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.parity_check.6009]");
		action = NACTION_INITIATOR_ERROR;
		break;
	case NSOP_DATAIN:
		glm_set_pkt_reason(glm, cmd, CMD_TRAN_ERR, STAT_PERR);
		glm_log(glm, CE_WARN, "SCSI bus DATA IN phase parity error");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.parity_check.6010]");
		action = (NACTION_SAVE_BCNT | NACTION_INITIATOR_ERROR);
		break;
	case NSOP_DATAOUT:
		glm_set_pkt_reason(glm, cmd, CMD_TRAN_ERR, STAT_PERR);
		glm_log(glm, CE_WARN, "SCSI bus DATA OUT phase parity error");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.parity_check.6011]");
		action = (NACTION_SAVE_BCNT | NACTION_INITIATOR_ERROR);
		break;
	case NSOP_DT_DATAIN:
		glm_set_pkt_reason(glm, cmd, CMD_TRAN_ERR, STAT_PERR);
		glm_log(glm, CE_WARN,
		    "SCSI bus DT DATA IN phase parity error");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.parity_check.6020]");
		action = (NACTION_SAVE_BCNT | NACTION_INITIATOR_ERROR);
		break;
	case NSOP_DT_DATAOUT:
		glm_set_pkt_reason(glm, cmd, CMD_TRAN_ERR, STAT_PERR);
		glm_log(glm, CE_WARN,
		    "SCSI bus DT DATA OUT phase parity error");
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.parity_check.6021]");
		action = (NACTION_SAVE_BCNT | NACTION_INITIATOR_ERROR);
		break;
	default:
		glm_report_fault(glm->g_dip, glm,
		    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
		    "SCSI bus parity error - illegal phase");
		action = (NACTION_CLEAR_CHIP |
		    NACTION_DO_BUS_RESET | NACTION_ERR);
		break;
	}
	return (action);
}

/*
 * start a fresh request from the top of the device queue
 */
static void
glm_start_next(struct glm *glm)
{
	glm_unit_t *unit;
	struct glm_scsi_cmd *cmd;

	NDBG1(("glm_start_next: glm=0x%p", (void *)glm));

again:
	GLM_RMQ(glm, unit);
	if (unit) {
		/*
		 * If all cmds drained from the Tag Q, back to full
		 * throttle and start submitting new cmds again.
		 */
		if (unit->nt_throttle == DRAIN_THROTTLE &&
		    unit->nt_tcmds == 0) {
			glm_full_throttle(glm, unit->nt_target, unit->nt_lun);
		}

		/*
		 * If there is a reset delay for this target and
		 * it happens to be queued on the hba work list, get
		 * the next target to run.  This target will be queued
		 * up later when the reset delay expires.
		 *
		 * always allow proxy cmds like device reset or abort
		 * to go out on the bus.
		 */
		if ((unit->nt_waitq != NULL) &&
		    ((unit->nt_throttle > unit->nt_tcmds) ||
		    (unit->nt_waitq->cmd_flags & CFLAG_CMDPROXY))) {
			GLM_WAITQ_RM(unit, cmd);
			if (cmd) {
				if (glm_start_cmd(glm, unit, cmd) == TRUE) {
					/*
					 * If this is a tagged target and
					 * there is more work to do,
					 * re-queue this target.
					 */
					if (TAGGED(unit->nt_target) &&
					    unit->nt_waitq != NULL) {
						glm_addbq(glm, unit);
					}
					return;
				}
			}
		}
		goto again;
	}

	/* no devs waiting for the hba, wait for disco-ed dev */
	glm_wait_for_reselect(glm, 0);
}

static int
glm_start_cmd(struct glm *glm, glm_unit_t *unit, struct glm_scsi_cmd *cmd)
{
	struct glm_dsa	*dsap;
	struct scsi_pkt *pktp = CMD2PKT(cmd);
	int n;
	struct nt_slots *slots = unit->nt_active;
	ushort_t target = unit->nt_target;
	ushort_t tshift = (1<<target);
	uint_t i = 0;
	uint_t index;

	NDBG1(("glm_start_cmd: cmd=0x%p\n", (void *)cmd));

	ASSERT(glm->g_state == NSTATE_IDLE);

	/*
	 * It is possible for back to back device reset to
	 * happen before the reset delay has expired.  That's
	 * ok, just let the device reset go out on the bus.
	 */
	if ((cmd->cmd_pkt_flags & FLAG_NOINTR) == 0) {
		ASSERT(glm->g_reset_delay[Tgt(cmd)] == 0);
	}

	unit->nt_goterror = FALSE;
	unit->nt_dma_status = 0;
	unit->nt_status0 = 0;
	unit->nt_status1 = 0;
	glm->g_wdtr_sent = 0;
	dsap = unit->nt_dsap;
	dsap->nt_statbuf[0] = 0;
	dsap->nt_errmsgbuf[0] = (uchar_t)MSG_NOP;

	/*
	 * if a non-tagged cmd is submitted to an active tagged target
	 * then drain before submitting this cmd; SCSI-2 allows RQSENSE
	 * to be untagged
	 */
	if (((cmd->cmd_pkt_flags & FLAG_TAGMASK) == 0) &&
	    TAGGED(target) && unit->nt_tcmds &&
	    ((cmd->cmd_flags & CFLAG_CMDPROXY) == 0) &&
	    (*(cmd->cmd_pkt->pkt_cdbp) != SCMD_REQUEST_SENSE)) {
		if ((cmd->cmd_pkt_flags & FLAG_NOINTR) == 0) {

			NDBG23(("untagged cmd, start draining\n"));

			if (glm->g_reset_delay[target] == 0) {
				unit->nt_throttle = DRAIN_THROTTLE;
			}
			glm_waitq_add_lifo(unit, cmd);
		}
		return (FALSE);
	}

	if (TAGGED(target) && (cmd->cmd_pkt_flags & FLAG_TAGMASK)) {
		if (glm_alloc_tag(glm, unit, cmd)) {
			glm_waitq_add_lifo(unit, cmd);
			return (FALSE);
		}
	} else {
		/*
		 * tag slot 0 is reserved for non-tagged cmds
		 * and should be empty because we have drained
		 */
		if ((cmd->cmd_flags & CFLAG_CMDPROXY) == 0) {
			ASSERT(unit->nt_active->nt_slot[0] == NULL);
			unit->nt_active->nt_slot[0] = cmd;
			cmd->cmd_tag[1] = 0;
			if (*(cmd->cmd_pkt->pkt_cdbp) != SCMD_REQUEST_SENSE) {
				ASSERT(unit->nt_tcmds == 0);

				/*
				 * don't start any other cmd until this
				 * one is finished.  The throttle is
				 * reset later in glm_watchsubr()
				 */
				unit->nt_throttle = 1;
			}
			unit->nt_tcmds++;
		}
	}

	/*
	 * Attach this target to the hba and make it active
	 */
	glm->g_current = unit;
	glm->g_state = NSTATE_ACTIVE;
	unit->nt_state |= NPT_STATE_ACTIVE;
	unit->nt_ncmdp = cmd;

	if (cmd->cmd_pkt_flags & FLAG_RENEGOTIATE_WIDE_SYNC) {
		glm_force_renegotiation(glm, Tgt(cmd));
	}

	/*
	 * check to see if target is allowed to disconnect
	 */
	if (cmd->cmd_pkt_flags & FLAG_NODISCON) {
		dsap->nt_msgoutbuf[i++] = (MSG_IDENTIFY | unit->nt_lun);
	} else {
		dsap->nt_msgoutbuf[i++] =
		    (MSG_DR_IDENTIFY | unit->nt_lun);
	}

	/*
	 * Assign tag byte to this cmd.
	 * (proxy msg's don't have tag flag set)
	 */
	if (cmd->cmd_pkt_flags & FLAG_TAGMASK) {
		ASSERT((cmd->cmd_pkt_flags & FLAG_NODISCON) == 0);
		dsap->nt_msgoutbuf[i++] = cmd->cmd_tag[0];
		dsap->nt_msgoutbuf[i++] = cmd->cmd_tag[1];
	}

	/*
	 * Single identify msg.
	 */
	dsap->nt_sendmsg.count = i;
	unit->nt_msgoutcount = i;
	unit->nt_msgouttype = 0;

	switch (cmd->cmd_type) {
	case NRQ_NORMAL_CMD:

		NDBG1(("glm_start_cmd: normal"));

		/*
		 * save the cdb length.
		 */
		dsap->nt_cmd.count = cmd->cmd_cdblen;

		/*
		 * Copy the CDB to our DSA structure for table
		 * indirect scripts access.
		 */
		(void) ddi_rep_put8(unit->nt_accessp, (uint8_t *)pktp->pkt_cdbp,
		    dsap->nt_cdb, cmd->cmd_cdblen, DDI_DEV_AUTOINCR);

		/*
		 * setup the Scatter/Gather DMA list for this request
		 */
		if (cmd->cmd_scc > 0) {
			ASSERT(cmd->cmd_flags & CFLAG_DMAVALID);

			NDBG1(("glm_start_cmd: cmd_scc=%d", cmd->cmd_scc));
				glm_dsa_dma_setup(pktp, unit, glm);
		} else if (cmd->cmd_nwin == 1 &&
		    cmd->cmd_cookiec <= GLM_MAX_DMA_SEGS) {
			cmd->cmd_scc = cmd->cmd_cookiec;
			ASSERT(cmd->cmd_flags & CFLAG_DMAVALID);
			glm_dsa_dma_setup(pktp, unit, glm);
		}
		/*
		 * Save the data pointers for disconnects/reselectset
		 */
		if (cmd->cmd_scc > 0) {
			cmd->cmd_saved_cookie = cmd->cmd_scc;
			index = (cmd->cmd_scc - 1);
			cmd->cmd_saved_addr =
			    unit->nt_dsap->nt_data[index].address;
			cmd->cmd_saved_count =
			    unit->nt_dsap->nt_data[index].count;
		}

		if (((tshift & glm->g_ppr_enabled & glm->g_ppr_supported) &&
		    (tshift & glm->g_ppr_known)) ||
		    (((glm->g_wide_known | glm->g_nowide) & tshift) &&
		    (NSYNCSTATE(glm, unit) != NSYNC_SDTR_NOTDONE))) {
startup:
			GLM_SETUP_SCRIPT(glm, unit);
			GLM_START_SCRIPT(glm, NSS_STARTUP);
			/*
			 * Start timeout.
			 */
#ifdef GLM_TEST
			/*
			 * Temporarily set timebase = 0;  needed for
			 * timeout torture test.
			 */
			if (glm_test_timeouts) {
				slots->nt_timebase = 0;
			}
#endif
			n = pktp->pkt_time - slots->nt_timebase;

			if (n == 0) {
				(slots->nt_dups)++;
				slots->nt_timeout = slots->nt_timebase;
			} else if (n > 0) {
				slots->nt_timeout =
				    slots->nt_timebase = pktp->pkt_time;
				slots->nt_dups = 1;
			}
#ifdef GLM_TEST
			/*
			 * Set back to a number higher than
			 * glm_scsi_watchdog_tick
			 * so timeouts will happen in glm_watchsubr
			 */
			if (glm_test_timeouts) {
				slots->nt_timebase = 60;
			}
#endif
			break;
		}

		if (tshift & glm->g_ppr_enabled & glm->g_ppr_supported) {
			glm_make_ppr(glm, unit);
		} else if (((glm->g_wide_known | glm->g_nowide) & tshift) ==
		    0) {
			glm_make_wdtr(glm, unit, GLM_XFER_WIDTH);
		} else if (NSYNCSTATE(glm, unit) == NSYNC_SDTR_NOTDONE) {
			NDBG31(("glm_start_cmd: try syncio"));
			/* haven't yet tried syncio on this target */
			glm_syncio_msg_init(glm, unit);
			glm->g_syncstate[unit->nt_target] = NSYNC_SDTR_SENT;
		}
		goto startup;

	case NRQ_DEV_RESET:
		NDBG22(("glm_start_cmd: bus device reset"));
		/* reset the msg out length for single message byte */
		unit->nt_msgouttype = 0;
		dsap->nt_msgoutbuf[i++] = MSG_DEVICE_RESET;
		dsap->nt_sendmsg.count = i;
		unit->nt_msgoutcount = i;
		/* no command buffer */
		goto bus_dev_reset;

	case NRQ_ABORT:
		NDBG23(("glm_start_cmd: abort"));
		/* reset the msg out length for two single */
		/* byte messages */
		unit->nt_msgouttype = 0;
		dsap->nt_msgoutbuf[i++] = MSG_ABORT;
		dsap->nt_sendmsg.count = i;
		unit->nt_msgoutcount = i;

bus_dev_reset:
		/* no command buffer */
		dsap->nt_cmd.count = 0;
		GLM_SETUP_SCRIPT(glm, unit);
		GLM_START_SCRIPT(glm, NSS_BUS_DEV_RESET);
		break;

	default:
		glm_log(glm, CE_PANIC,
		    "invalid queue entry cmd=0x%p", (void *)cmd);
		/* NOTREACHED */
	}

	return (TRUE);
}

static void
glm_wait_for_reselect(glm_t *glm, uint_t action)
{
	NDBG1(("glm_wait_for_reselect: action=%x", action));

	/*
	 * The hba's dsa structure will take care of reconnections,
	 * so NULL out g_current.  It will get set again during a
	 * valid reselection.
	 */
	glm->g_current = NULL;
	glm->g_state = NSTATE_WAIT_RESEL;
	glm->g_dsa->g_errmsgbuf[0] = (uchar_t)MSG_NOP;

	action &= NACTION_ABORT | NACTION_MSG_REJECT | NACTION_MSG_PARITY |
	    NACTION_INITIATOR_ERROR;

	/*
	 * Put hba's dsa structure address in DSA reg.
	 */
	if (action == 0 && glm->g_ndisc != 0) {
		/* wait for any disconnected targets */
		GLM_START_SCRIPT(glm, NSS_WAIT4RESELECT);
		NDBG19(("glm_wait_for_reselect: WAIT"));
		return;
	}

	if (action & NACTION_ABORT) {
		/* abort an invalid reconnect */
		glm->g_dsa->g_errmsgbuf[0] = (uchar_t)MSG_ABORT;
		GLM_START_SCRIPT(glm, NSS_ABORT);
		return;
	}

	if (action & NACTION_MSG_REJECT) {
		/* target sent me bad msg, send msg reject */
		glm->g_dsa->g_errmsgbuf[0] = (uchar_t)MSG_REJECT;
		GLM_START_SCRIPT(glm, NSS_ERR_MSG);
		NDBG19(("glm_wait_for_reselect: Message Reject"));
		return;
	}

	if (action & NACTION_MSG_PARITY) {
		/* got a parity error during message in phase */
		glm->g_dsa->g_errmsgbuf[0] = (uchar_t)MSG_MSG_PARITY;
		GLM_START_SCRIPT(glm, NSS_ERR_MSG);
		NDBG19(("glm_wait_for_reselect: Message Parity Error"));
		return;
	}

	if (action & NACTION_INITIATOR_ERROR) {
		/* catchall for other errors */
		glm->g_dsa->g_errmsgbuf[0] = (uchar_t)MSG_INITIATOR_ERROR;
		GLM_START_SCRIPT(glm, NSS_ERR_MSG);
		NDBG1(("glm_wait_for_reselect: Initiator Detected Error"));
		return;
	}

	/* no disconnected targets go idle */
	glm->g_current = NULL;
	glm->g_state = NSTATE_IDLE;
	(void) ddi_dma_sync(glm->g_dsa_dma_h, 0, 0, DDI_DMA_SYNC_FORDEV);
	NDBG1(("glm_wait_for_reselect: IDLE"));
}

/*
 * How the hba continues depends on whether sync i/o
 * negotiation was in progress and if so how far along.
 * Or there might be an error message to be sent out.
 */
static void
glm_restart_current(glm_t *glm, uint_t action)
{
	glm_unit_t	*unit = glm->g_current;
	struct glm_dsa	*dsap;

	NDBG1(("glm_restart_current: action=%x", action));

	if (unit == NULL) {
		/* the current request just finished, do the next one */
		glm_start_next(glm);
		return;
	}

	dsap = unit->nt_dsap;
	dsap->nt_errmsgbuf[0] = (uchar_t)MSG_NOP;

	if (unit->nt_state & NPT_STATE_ACTIVE) {
		NDBG1(("glm_restart_current: active"));

		action &= (NACTION_ACK | NACTION_EXT_MSG_OUT |
		    NACTION_MSG_REJECT | NACTION_MSG_PARITY |
		    NACTION_INITIATOR_ERROR | NACTION_ARQ |
		    NACTION_EXT_MSG_IN);

		(void) ddi_dma_sync(unit->nt_dma_p, 0, 0, DDI_DMA_SYNC_FORDEV);
		if (action == 0) {
			/* continue the script on the currently active target */
			GLM_START_SCRIPT(glm, NSS_CONTINUE);
			goto done;
		}

		if (action & NACTION_ACK) {
			/* just ack the last byte and continue */
			GLM_START_SCRIPT(glm, NSS_CLEAR_ACK);
			goto done;
		}

		if (action & NACTION_EXT_MSG_OUT) {
			/* send my SDTR message */
			GLM_START_SCRIPT(glm, NSS_EXT_MSG_OUT);
			goto done;
		}

		if (action & NACTION_EXT_MSG_IN) {
			/* receive extended message */
			GLM_SETUP_SCRIPT(glm, unit);
			GLM_START_SCRIPT(glm, NSS_EXT_MSG_IN);
			goto done;
		}

		if (action & NACTION_MSG_REJECT) {
			/* target sent me bad msg, send msg reject */
			dsap->nt_errmsgbuf[0] = (uchar_t)MSG_REJECT;
			GLM_START_SCRIPT(glm, NSS_ERR_MSG);
			goto done;
		}

		if (action & NACTION_MSG_PARITY) {
			/* got a parity error during message in phase */
			dsap->nt_errmsgbuf[0] = (uchar_t)MSG_MSG_PARITY;
			GLM_START_SCRIPT(glm, NSS_ERR_MSG);
			goto done;
		}

		if (action & NACTION_INITIATOR_ERROR) {
			/* catchall for other errors */
			dsap->nt_errmsgbuf[0] = (uchar_t)
			    MSG_INITIATOR_ERROR;
			GLM_START_SCRIPT(glm, NSS_ERR_MSG);
			goto done;
		}
	} else if ((unit->nt_state & NPT_STATE_ACTIVE) == 0) {
		NDBG1(("glm_restart_current: idle"));
		/*
		 * a target wants to reconnect so make
		 * it the currently active target
		 */
		GLM_SETUP_SCRIPT(glm, unit);
		GLM_START_SCRIPT(glm, NSS_CLEAR_ACK);
	}
done:
	unit->nt_state |= NPT_STATE_ACTIVE;
	NDBG1(("glm_restart_current: okay"));
}

static void
glm_restart_hba(glm_t *glm, uint_t action)
{
	NDBG1(("glm_restart_hba"));

	/*
	 * run the target at the front of the queue unless we're
	 * just waiting for a reconnect. In which case just use
	 * the first target's data structure since it's handy.
	 */
	switch (glm->g_state) {
	case NSTATE_ACTIVE:
		NDBG1(("glm_restart_hba: ACTIVE"));
		glm_restart_current(glm, action);
		break;

	case NSTATE_WAIT_RESEL:
		NDBG1(("glm_restart_hba: WAIT"));
		glm_wait_for_reselect(glm, action);
		break;

	case NSTATE_IDLE:
		NDBG1(("glm_restart_hba: IDLE"));
		/* start whatever's on the top of the queue */
		glm_start_next(glm);
		break;
	}
}

/*
 * Save the scatter/gather current-index and number-completed
 * values so when the target reconnects we can restart the
 * data in/out move instruction at the proper point. Also, if the
 * disconnect happened within a segment there's a fixup value
 * for the partially completed data in/out move instruction.
 */
static void
glm_sg_update(glm_unit_t *unit, uchar_t index, uint32_t remain)
{
	glmti_t	*sgp;
	struct glm_dsa *dsap = unit->nt_dsap;
	struct glm_scsi_cmd *cmd = unit->nt_ncmdp;

	NDBG17(("glm_sg_update: unit=0x%p index=%x remain=%x",
	    (void *)unit, index, remain));
	/*
	 * Record the number of segments left to do.
	 */
	cmd->cmd_scc = index;

	/*
	 * If interrupted between segments then don't adjust S/G table
	 */
	if (remain == 0) {
		/*
		 * Must have just completed the current segment when
		 * the interrupt occurred, restart at the next segment.
		 */
		cmd->cmd_scc--;
		return;
	}

	/*
	 * index is zero based, so to index into the
	 * scatter/gather list subtract one.
	 */
	index--;

	/*
	 * Fixup the Table Indirect entry for this segment.
	 */
	sgp = &dsap->nt_data[index];

	sgp->address += (sgp->count - remain);
	sgp->count = remain;

	NDBG17(("glm_sg_update: remain=%d", remain));
	NDBG17(("Total number of bytes to transfer was %d", sgp->count));
	NDBG17(("at address=0x%x", sgp->address));
}

/*
 * Determine if the command completed with any bytes leftover
 * in the Scatter/Gather DMA list.
 */
static uint32_t
glm_sg_residual(glm_t *glm, glm_unit_t *unit, struct glm_scsi_cmd *cmd)
{
	glmti_t	*sgp;
	uint32_t residual = 0;
	int	 index;

	/*
	 * Get the current index into the sg table.
	 */
	index = (GLM_MAX_DMA_SEGS - cmd->cmd_scc);

	NDBG17(("glm_sg_residual: unit=0x%p index=%d", (void *)unit, index));

	sgp = &unit->nt_dsap->nt_data[index];

	for (; index < GLM_MAX_DMA_SEGS; index++, sgp++) {
		if (((glm->g_devid == GLM_53c1010_33) ||
		    (glm->g_devid == GLM_53c1010_66)) &&
		    (cmd->cmd_flags & CFLAG_PMM_RECEIVED) &&
		    (index == (GLM_MAX_DMA_SEGS - cmd->cmd_scc))) {

			residual += cmd->cmd_pmm_count;
			cmd->cmd_flags &= ~CFLAG_PMM_RECEIVED;
		} else {
			residual += sgp->count;
		}
	}

	NDBG17(("glm_sg_residual: residual=%d", residual));

	return (residual);
}

static void
glm_queue_target(glm_t *glm, glm_unit_t *unit)
{
	NDBG1(("glm_queue_target"));

	glm_addbq(glm, unit);

	switch (glm->g_state) {
	case NSTATE_IDLE:
		/* the device is idle, start first queue entry now */
		glm_restart_hba(glm, 0);
		break;
	case NSTATE_ACTIVE:
		/* queue the target and return without doing anything */
		break;
	case NSTATE_WAIT_RESEL:
		/*
		 * If we're waiting for reselection of a disconnected target
		 * then set the Signal Process bit in the ISTAT register and
		 * return. The interrupt routine restarts the queue.
		 */
		GLM_SET_SIGP(glm);
		break;
	}
}

static glm_unit_t *
glm_get_target(glm_t *glm)
{
	uchar_t target, lun;

	NDBG1(("glm_get_target"));

	/*
	 * Get the LUN from the IDENTIFY message byte
	 */
	lun = glm->g_dsa->g_msginbuf[0];

	if (!(lun & MSG_IDENTIFY)) {
		return (NULL);
	}

	lun = (lun & MSG_GLM_LUNRTN);

	/*
	 * Get the target from the HBA's id register
	 */
	if (GLM_GET_TARGET(glm, &target)) {
		return (NTL2UNITP(glm, target, lun));
	}

	return (NULL);
}

/* add unit to the front of queue */
static void
glm_addfq(glm_t	*glm, glm_unit_t *unit)
{
	NDBG7(("glm_addfq: unit=0x%p", (void *)unit));

	if (unit->nt_state & NPT_STATE_QUEUED) {
#ifdef GLM_DEBUG
		if (glm_hbaq_check(glm, unit) == FALSE) {
			glm_log(glm, CE_WARN,
			    "glm_addfq: not queued, but claims it is.\n");
		}
#endif
		return;
	}

	/* See if it's already in the queue */
	if (unit->nt_linkp != NULL || unit == glm->g_backp ||
	    unit == glm->g_forwp)
		glm_log(glm, CE_PANIC, "glm_addfq: queue botch");

	if ((unit->nt_linkp = glm->g_forwp) == NULL)
		glm->g_backp = unit;
	glm->g_forwp = unit;

	unit->nt_state |= NPT_STATE_QUEUED;
}

/* add unit to the back of queue */
static void
glm_addbq(glm_t	*glm, glm_unit_t *unit)
{
	NDBG7(("glm_addbq: unit=0x%p", (void *)unit));

	/*
	 * The target is already queued, just return.
	 */
	if (unit->nt_state & NPT_STATE_QUEUED) {
#ifdef GLM_DEBUG
		if (glm_hbaq_check(glm, unit) == FALSE) {
			glm_log(glm, CE_WARN,
			    "glm_addbq: not queued, but claims it is.\n");
		}
#endif
		return;
	}

	unit->nt_linkp = NULL;

	if (glm->g_forwp == NULL)
		glm->g_forwp = unit;
	else
		glm->g_backp->nt_linkp = unit;

	glm->g_backp = unit;

	unit->nt_state |= NPT_STATE_QUEUED;
}

#ifdef GLM_DEBUG
static int
glm_hbaq_check(struct glm *glm, struct glm_unit *unit)
{
	struct glm_unit *up = glm->g_forwp;
	int rval = FALSE;
	int cnt = 0;
	int tot_cnt = 0;

	while (up) {
		if (up == unit) {
			rval = TRUE;
			cnt++;
		}
		up = up->nt_linkp;
		tot_cnt++;
	}
	if (cnt > 1) {
		glm_log(glm, CE_WARN,
		    "total queued=%d: target %d- on hba q %d times.\n",
		    tot_cnt, unit->nt_target, cnt);
	}
	return (rval);
}
#endif

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
glm_doneq_add(struct glm *glm, struct glm_scsi_cmd *cmd)
{
	struct scsi_pkt *pkt = CMD2PKT(cmd);

	NDBG7(("glm_doneq_add: cmd=0x%p", (void *)cmd));

	/*
	 * If this target did not respond to selection, remove the
	 * unit/dsa memory resources.  If the target comes back
	 * later, we will build these structures on the fly.
	 *
	 * Also, don't remove HBA's unit/dsa structure if the hba
	 * was probed.
	 */
	if (pkt->pkt_reason == CMD_INCOMPLETE) {
		glm_tear_down_unit_dsa(glm, Tgt(cmd), Lun(cmd));
	}

	ASSERT((cmd->cmd_flags & CFLAG_COMPLETED) == 0);
	cmd->cmd_linkp = NULL;
	cmd->cmd_flags |= CFLAG_FINISHED;
	cmd->cmd_flags &= ~CFLAG_IN_TRANSPORT;

	/*
	 * only add scsi pkts that have completion routines to
	 * the doneq.  no intr cmds do not have callbacks.
	 * run the callback on an ARQ pkt immediately.  This
	 * frees the ARQ for other check conditions.
	 */
	if (pkt->pkt_comp && !(cmd->cmd_flags & CFLAG_CMDARQ)) {
		*glm->g_donetail = cmd;
		glm->g_donetail = &cmd->cmd_linkp;
	} else if (pkt->pkt_comp && (cmd->cmd_flags & CFLAG_CMDARQ)) {
		glm_complete_arq_pkt(pkt);
	}
}

static ncmd_t *
glm_doneq_rm(glm_t *glm)
{
	ncmd_t	*cmdp;
	NDBG7(("glm_doneq_rm"));

	/* pop one off the done queue */
	if ((cmdp = glm->g_doneq) != NULL) {
		/* if the queue is now empty fix the tail pointer */
		if ((glm->g_doneq = cmdp->cmd_linkp) == NULL)
			glm->g_donetail = &glm->g_doneq;
		cmdp->cmd_linkp = NULL;
	}
	return (cmdp);
}

static void
glm_doneq_empty(glm_t *glm)
{
	if (glm->g_doneq && !glm->g_in_callback) {
		ncmd_t *cmd;
		struct scsi_pkt *pkt;

		glm->g_in_callback = 1;

		/*
		 * run the completion routines of all the
		 * completed commands
		 */
		while ((cmd = glm_doneq_rm(glm)) != NULL) {
			/* run this command's completion routine */
			cmd->cmd_flags |= CFLAG_COMPLETED;
			pkt = CMD2PKT(cmd);
			if (glm_check_handle_status(glm) != DDI_SUCCESS) {
				glm_report_fault(glm->g_dip, glm,
				    DDI_SERVICE_LOST, DDI_DATAPATH_FAULT,
				    "doneq_empty - data path failed");
				pkt->pkt_reason = CMD_TRAN_ERR;
				pkt->pkt_statistics = 0;
			}
			if ((PKT2GLMUNITP(pkt) && PKT2GLMUNITP(pkt)->nt_dma_p &&
			    ddi_check_dma_handle(PKT2GLMUNITP(pkt)->nt_dma_p) !=
			    DDI_SUCCESS) ||
			    (cmd->cmd_dmahandle &&
			    ddi_check_dma_handle(cmd->cmd_dmahandle) !=
			    DDI_SUCCESS)) {
				pkt->pkt_reason = CMD_TRAN_ERR;
				pkt->pkt_statistics = 0;
			}
			mutex_exit(&glm->g_mutex);
			scsi_hba_pkt_comp(pkt);
			mutex_enter(&glm->g_mutex);
		}
		glm->g_in_callback = 0;
	}
}

/*
 * These routines manipulate the target's queue of pending requests
 */
static void
glm_waitq_add(glm_unit_t *unit, ncmd_t *cmdp)
{
	NDBG7(("glm_waitq_add: cmd=0x%p", (void *)cmdp));

	cmdp->cmd_queued = TRUE;
	cmdp->cmd_linkp = NULL;
	*(unit->nt_waitqtail) = cmdp;
	unit->nt_waitqtail = &cmdp->cmd_linkp;
}

static void
glm_waitq_add_lifo(glm_unit_t *unit, ncmd_t *cmdp)
{
	NDBG7(("glm_waitq_add_lifo: cmd=0x%p", (void *)cmdp));

	cmdp->cmd_queued = TRUE;
	if ((cmdp->cmd_linkp = unit->nt_waitq) == NULL) {
		unit->nt_waitqtail = &cmdp->cmd_linkp;
	}
	unit->nt_waitq = cmdp;
}

static ncmd_t *
glm_waitq_rm(glm_unit_t *unit)
{
	ncmd_t *cmdp;
	NDBG7(("glm_waitq_rm: unit=0x%p", (void *)unit));

	GLM_WAITQ_RM(unit, cmdp);

	NDBG7(("glm_waitq_rm: unit=0x%p cmdp=0x%p",
	    (void *)unit, (void *)cmdp));

	return (cmdp);
}

/*
 * remove specified cmd from the middle of the unit's wait queue.
 */
static void
glm_waitq_delete(glm_unit_t *unit, ncmd_t *cmdp)
{
	ncmd_t	*prevp = unit->nt_waitq;

	NDBG7(("glm_waitq_delete: unit=0x%p cmd=0x%p",
	    (void *)unit, (void *)cmdp));

	if (prevp == cmdp) {
		if ((unit->nt_waitq = cmdp->cmd_linkp) == NULL)
			unit->nt_waitqtail = &unit->nt_waitq;

		cmdp->cmd_linkp = NULL;
		cmdp->cmd_queued = FALSE;
		NDBG7(("glm_waitq_delete: unit=0x%p cmdp=0x%p",
		    (void *)unit, (void *)cmdp));
		return;
	}

	while (prevp != NULL) {
		if (prevp->cmd_linkp == cmdp) {
			if ((prevp->cmd_linkp = cmdp->cmd_linkp) == NULL)
				unit->nt_waitqtail = &prevp->cmd_linkp;

			cmdp->cmd_linkp = NULL;
			cmdp->cmd_queued = FALSE;
			NDBG7(("glm_waitq_delete: unit=0x%p cmdp=0x%p",
			    (void *)unit, (void *)cmdp));
			return;
		}
		prevp = prevp->cmd_linkp;
	}
	cmn_err(CE_PANIC, "glm: glm_waitq_delete: queue botch");
}

static void
glm_hbaq_delete(struct glm *glm, struct glm_unit *unit)
{
	struct glm_unit *up = glm->g_forwp;
	int cnt = 0;

	if (unit == glm->g_forwp) {
		cnt++;
		if ((glm->g_forwp = unit->nt_linkp) == NULL) {
			glm->g_backp = NULL;
		}
		unit->nt_state &= ~NPT_STATE_QUEUED;
		unit->nt_linkp = NULL;
	}
	while (up != NULL) {
		if (up->nt_linkp == unit) {
			ASSERT(cnt == 0);
			cnt++;
			if ((up->nt_linkp = unit->nt_linkp) == NULL) {
				glm->g_backp = up;
			}
			unit->nt_linkp = NULL;
			unit->nt_state &= ~NPT_STATE_QUEUED;
		}
		up = up->nt_linkp;
	}
	if (cnt > 1) {
		glm_log(glm, CE_NOTE,
		    "glm_hbaq_delete: target %d- was on hbaq %d times.\n",
		    unit->nt_target, cnt);
	}
}

/*
 * synchronous xfer & negotiation handling
 *
 * establish a new sync i/o state for all the luns on a target
 */
static void
glm_syncio_state(glm_t *glm, glm_unit_t *unit, uchar_t state, uchar_t sxfer,
	uchar_t sscfX10)
{
	uint16_t target = unit->nt_target;
	uint16_t lun;
	uint8_t scntl3 = 0;

	NDBG31(("glm_syncio_state: unit=0x%p state=%x sxfer=%x sscfX10=%x",
	    (void *)unit, state, sxfer, sscfX10));

	/*
	 * Change state of all LUNs on this target.  We may be responding
	 * to a target initiated sdtr and we may not be using syncio.
	 * We don't want to change state of the target, but we do need
	 * to respond to the target's requestion for sdtr per the scsi spec.
	 */
	if (NSYNCSTATE(glm, unit) != NSYNC_SDTR_REJECT) {
		NSYNCSTATE(glm, unit) = state;
	}

	/*
	 * Set sync i/o clock divisor in SCNTL3 registers
	 */
	if (sxfer != 0) {
		switch (sscfX10) {
		case 10:
			scntl3 = (NB_SCNTL3_SCF1 | glm->g_scntl3);
			break;
		case 15:
			scntl3 = (NB_SCNTL3_SCF15 | glm->g_scntl3);
			break;
		case 20:
			scntl3 = (NB_SCNTL3_SCF2 | glm->g_scntl3);
			break;
		case 30:
			scntl3 = (NB_SCNTL3_SCF3 | glm->g_scntl3);
			break;
		case 40:
			scntl3 = (NB_SCNTL3_SCF4 | glm->g_scntl3);
			break;
		case 60:
			scntl3 = (NB_SCNTL3_SCF6 | glm->g_scntl3);
			break;
		case 80:
			scntl3 = (NB_SCNTL3_SCF8 | glm->g_scntl3);
			break;
		}
	}

	for (lun = 0; lun < NLUNS_GLM; lun++) {
		/* store new sync i/o parms in each per-target-struct */
		if ((unit = NTL2UNITP(glm, target, lun)) != NULL) {
			unit->nt_dsap->nt_selectparm.nt_sxfer = sxfer;
			unit->nt_sscfX10 = sscfX10;
			unit->nt_dsap->nt_selectparm.nt_scntl3 |= scntl3;
		}
	}

	glm->g_dsa->g_reselectparm[target].g_sxfer = sxfer;
	glm->g_dsa->g_reselectparm[target].g_scntl3 |= scntl3;
}

static void
glm_syncio_disable(glm_t *glm)
{
	ushort_t target;

	NDBG31(("glm_syncio_disable: devaddr=0x%p", (void *)glm->g_devaddr));

	for (target = 0; target < NTARGETS_WIDE; target++)
		glm->g_syncstate[target] = NSYNC_SDTR_REJECT;
}

static void
glm_syncio_reset_target(glm_t *glm, int target)
{
	glm_unit_t *unit;
	ushort_t lun;

	NDBG31(("glm_syncio_reset_target: target=%x", target));

	/* check if sync i/o negotiation disabled on this target */
	if (target == glm->g_glmid)
		glm->g_syncstate[target] = NSYNC_SDTR_REJECT;
	else if (glm->g_syncstate[target] != NSYNC_SDTR_REJECT)
		glm->g_syncstate[target] = NSYNC_SDTR_NOTDONE;

	for (lun = 0; lun < NLUNS_GLM; lun++) {
		if ((unit = NTL2UNITP(glm, target, lun)) != NULL) {
			/* byte assignment */
			unit->nt_dsap->nt_selectparm.nt_sxfer = 0;
		}
	}
	glm->g_dsa->g_reselectparm[target].g_sxfer = 0;
}

static void
glm_syncio_reset(glm_t *glm, glm_unit_t *unit)
{
	ushort_t	target;

	NDBG31(("glm_syncio_reset: devaddr=0x%p", (void *)glm->g_devaddr));

	if (unit != NULL) {
		/* only reset the luns on this target */
		glm_syncio_reset_target(glm, unit->nt_target);
		return;
	}

	/* set the max offset to zero to disable sync i/o */
	for (target = 0; target < NTARGETS_WIDE; target++) {
		glm_syncio_reset_target(glm, target);
	}
}

static void
glm_syncio_msg_init(glm_t *glm, glm_unit_t *unit)
{
	struct glm_dsa *dsap;
	uint_t msgcount;
	uchar_t target = unit->nt_target;
	uchar_t period = glm->g_hba_period;
	uchar_t offset;

	NDBG31(("glm_syncio_msg_init: unit=0x%p", (void *)unit));

	dsap = unit->nt_dsap;
	msgcount = unit->nt_msgoutcount;

	/*
	 * Use target's period (not the hba's period) if
	 * this target experienced a sync backoff.
	 */
	if (glm->g_backoff & (1<<unit->nt_target)) {
		period = glm->g_minperiod[unit->nt_target];
	}

	/*
	 * sanity check of period and offset
	 */
	if ((glm->g_target_scsi_options[target] & SCSI_OPTIONS_FAST40) &&
	    (glm->g_options & GLM_OPT_LVD)) {
		if (period < (uchar_t)(DEFAULT_FAST40SYNC_PERIOD)) {
			period = (uchar_t)(DEFAULT_FAST40SYNC_PERIOD);
		}
	} else if (glm->g_target_scsi_options[target] & SCSI_OPTIONS_FAST20) {
		if (period < (uchar_t)(DEFAULT_FAST20SYNC_PERIOD)) {
			period = (uchar_t)(DEFAULT_FAST20SYNC_PERIOD);
		}
	} else if (glm->g_target_scsi_options[target] & SCSI_OPTIONS_FAST) {
		if (period < (uchar_t)(DEFAULT_FASTSYNC_PERIOD)) {
			period = (uchar_t)(DEFAULT_FASTSYNC_PERIOD);
		}
	} else {
		if (period < (uchar_t)(DEFAULT_SYNC_PERIOD)) {
			period = (uchar_t)(DEFAULT_SYNC_PERIOD);
		}
	}

	if (glm->g_target_scsi_options[target] & SCSI_OPTIONS_SYNC) {
		offset = glm->g_sync_offset;
	} else {
		offset = 0;
	}

	if (glm->g_force_async & (1<<target)) {
		offset = 0;
	}

	unit->nt_msgouttype = MSG_SYNCHRONOUS;
	dsap->nt_msgoutbuf[msgcount++] = (uchar_t)MSG_EXTENDED;
	dsap->nt_msgoutbuf[msgcount++] = (uchar_t)3;
	dsap->nt_msgoutbuf[msgcount++] = (uchar_t)MSG_SYNCHRONOUS;
	dsap->nt_msgoutbuf[msgcount++] = GLM_GET_SYNC(period);
	dsap->nt_msgoutbuf[msgcount++] = offset;

	dsap->nt_sendmsg.count = msgcount;
	unit->nt_msgoutcount = msgcount;
}

/*
 * glm sent a sdtr to a target and the target responded.  Find out
 * the offset and sync period and enable sync scsi if needed.
 *
 * called from: glm_syncio_decide.
 */
static int
glm_syncio_enable(glm_t *glm, glm_unit_t *unit)
{
	uchar_t sxfer;
	uchar_t sscfX10;
	int time_ns;
	uchar_t offset;
	struct glm_dsa *dsap;

	NDBG31(("glm_syncio_enable: unit=0x%p", (void *)unit));

	dsap = unit->nt_dsap;

	/*
	 * units for transfer period factor are 4 nsec.
	 *
	 * These two values are the sync period and offset
	 * the target responded with.
	 */
	time_ns = dsap->nt_syncibuf[1];
	time_ns = GLM_GET_PERIOD(time_ns);
	offset = dsap->nt_syncibuf[2];

	/*
	 * If the target returned a 0 offset, just go asynchronous
	 * and return.
	 */
	if (offset == 0) {
		glm_syncio_state(glm, unit, NSYNC_SDTR_DONE, 0, 0);
		glm->g_props_update |= (1<<unit->nt_target);
		return (TRUE);
	}

	/*
	 * Check the period returned by the target.  Target shouldn't
	 * try to decrease my period
	 */
	if ((time_ns < CONVERT_PERIOD(glm->g_speriod)) ||
	    !glm_max_sync_divisor(glm, time_ns, &sxfer, &sscfX10)) {
		NDBG31(("glm_syncio_enable: invalid period: %d", time_ns));
		return (FALSE);
	}

	/*
	 * check the offset returned by the target.
	 */
	if (offset > glm->g_sync_offset) {
		NDBG31(("glm_syncio_enable: invalid offset=%d", offset));
		return (FALSE);
	}

	if ((glm->g_devid == GLM_53c1010_33) ||
	    (glm->g_devid == GLM_53c1010_66)) {

		sxfer = offset;
	} else {
		/* encode the divisor and offset values */
		sxfer = (((sxfer - 4) << 5) + offset);
	}

	unit->nt_fastscsi = (time_ns < 200) ? TRUE : FALSE;

	/*
	 * If this target is UltraSCSI or faster, enable UltraSCSI
	 * timing in the chip for this target.
	 */
	if (time_ns <= DEFAULT_FAST20SYNC_PERIOD) {
		if ((glm->g_devid == GLM_53c1010_33) ||
		    (glm->g_devid == GLM_53c1010_66)) {
			unit->nt_dsap->nt_selectparm.nt_scntl4 = 0;
			glm->g_dsa->g_reselectparm[unit->nt_target].g_scntl4 =
			    0;
		} else {
			unit->nt_dsap->nt_selectparm.nt_scntl3 |=
			    NB_SCNTL3_ULTRA;
			glm->g_dsa->g_reselectparm[unit->nt_target].g_scntl3 |=
			    NB_SCNTL3_ULTRA;
		}
	}

	/* set the max offset and clock divisor for all LUNs on this target */
	NDBG31(("glm_syncio_enable: target=%d sxfer=%x, sscfX10=%d",
	    unit->nt_target, sxfer, sscfX10));

	glm->g_minperiod[unit->nt_target] = time_ns;

	glm_syncio_state(glm, unit, NSYNC_SDTR_DONE, sxfer, sscfX10);
	glm->g_props_update |= (1<<unit->nt_target);
	return (TRUE);
}


/*
 * The target started the synchronous i/o negotiation sequence by
 * sending me a SDTR message. Look at the target's parms and the
 * HBA's defaults and decide on the appropriate comprise. Send the
 * larger of the two transfer periods and the smaller of the two offsets.
 */
static int
glm_syncio_respond(glm_t *glm, glm_unit_t *unit)
{
	uchar_t	sxfer;
	uchar_t	sscfX10;
	int	time_ns;
	uchar_t offset;
	struct glm_dsa *dsap;
	ushort_t targ = unit->nt_target;

	NDBG31(("glm_syncio_respond: unit=0x%p", (void *)unit));

	dsap = unit->nt_dsap;

	/*
	 * Use the smallest offset
	 */
	offset = dsap->nt_syncibuf[2];

	if ((glm->g_syncstate[targ] == NSYNC_SDTR_REJECT) ||
	    ((glm->g_target_scsi_options[targ] & SCSI_OPTIONS_SYNC) == 0)) {
		offset = 0;
	}

	if (offset > glm->g_sync_offset)
		offset = glm->g_sync_offset;

	/*
	 * units for transfer period factor are 4 nsec.
	 */
	time_ns = dsap->nt_syncibuf[1];
	time_ns = GLM_GET_PERIOD(time_ns);

	if (glm->g_backoff & (1<<targ)) {
		time_ns = glm->g_minperiod[targ];
	}

	/*
	 * Use largest time period.
	 */
	if (time_ns < glm->g_hba_period) {
		time_ns = glm->g_hba_period;
	}

	/*
	 * Target has requested a sync period slower than
	 * our max.  respond with our max sync rate.
	 */
	if (time_ns > MAX_SYNC_PERIOD(glm)) {
		time_ns = MAX_SYNC_PERIOD(glm);
	}

	if (!glm_max_sync_divisor(glm, time_ns, &sxfer, &sscfX10)) {
		NDBG31(("glm_syncio_respond: invalid period: %d,%d",
		    time_ns, offset));
		return (FALSE);
	}

	sxfer = (((sxfer - 4) << 5) + offset);

	/* set the max offset and clock divisor for all LUNs on this target */
	glm_syncio_state(glm, unit, NSYNC_SDTR_DONE, sxfer, sscfX10);

	/* report to target the adjusted period */
	if ((time_ns = glm_period_round(glm, time_ns)) == -1) {
		NDBG31(("glm_syncio_respond: round failed time=%d",
		    time_ns));
		return (FALSE);
	}

	glm->g_minperiod[targ] = time_ns;

	unit->nt_msgouttype = MSG_SYNCHRONOUS;
	dsap->nt_msgoutbuf[0] = 0x01;
	dsap->nt_msgoutbuf[1] = 0x03;
	dsap->nt_msgoutbuf[2] = 0x01;
	dsap->nt_msgoutbuf[3] = GLM_GET_SYNC(time_ns);
	dsap->nt_msgoutbuf[4] = (uchar_t)offset;
	dsap->nt_sendmsg.count = 5;
	unit->nt_msgoutcount = 5;

	unit->nt_fastscsi = (time_ns < 200) ? TRUE : FALSE;

	/*
	 * If this target is UltraSCSI or faster, enable UltraSCSI
	 * timing in the chip for this target.
	 */
	if (time_ns <= DEFAULT_FAST20SYNC_PERIOD) {
		if ((glm->g_devid == GLM_53c1010_33) ||
		    (glm->g_devid == GLM_53c1010_66)) {
			unit->nt_dsap->nt_selectparm.nt_scntl4 = 0;
			glm->g_dsa->g_reselectparm[targ].g_scntl4 = 0;
		} else {
			unit->nt_dsap->nt_selectparm.nt_scntl3 |=
			    NB_SCNTL3_ULTRA;
			glm->g_dsa->g_reselectparm[targ].g_scntl3 |=
			    NB_SCNTL3_ULTRA;
		}
	}

	glm->g_props_update |= (1<<targ);

	return (TRUE);
}

static uint_t
glm_syncio_decide(glm_t *glm, glm_unit_t *unit, uint_t action)
{
	NDBG31(("glm_syncio_decide: unit=0x%p action=%x",
	    (void *)unit, action));

	if (action & (NACTION_SIOP_HALT | NACTION_SIOP_REINIT
	    | NACTION_BUS_FREE)) {
		/* set all LUNs on this target to renegotiate syncio */
		glm_syncio_reset(glm, unit);
		return (action);
	}

	if (action & (NACTION_DONE | NACTION_ERR)) {
		/* the target finished without responding to SDTR */
		/* set all LUN's on this target to async mode */
		glm_syncio_state(glm, unit, NSYNC_SDTR_DONE, 0, 0);
		return (action);
	}

	if (action & (NACTION_MSG_PARITY | NACTION_INITIATOR_ERROR)) {
		/* allow target to try to do error recovery */
		return (action);
	}

	if ((action & NACTION_SDTR) == 0) {
		return (action);
	}

	/* if got good SDTR response, enable sync i/o */
	switch (NSYNCSTATE(glm, unit)) {
	case NSYNC_SDTR_SENT:
		if (glm_syncio_enable(glm, unit)) {
			/* reprogram the sxfer register */
			GLM_SET_SYNCIO(glm, unit);
			action |= NACTION_ACK;
		} else {
			/*
			 * The target sent us bogus sync msg.
			 * reject this msg. Disallow sync.
			 */
			NSYNCSTATE(glm, unit) = NSYNC_SDTR_REJECT;
			action |= NACTION_MSG_REJECT;
		}
		return (action);

	case NSYNC_SDTR_RCVD:
	case NSYNC_SDTR_REJECT:
		/*
		 * if target initiated SDTR handshake, send sdtr.
		 */
		if (glm_syncio_respond(glm, unit)) {
			/* reprogram the sxfer register */
			GLM_SET_SYNCIO(glm, unit);
			return (NACTION_EXT_MSG_OUT | action);
		}
		break;

	case NSYNC_SDTR_NOTDONE:
		unit->nt_dsap->nt_sendmsg.count = 0;
		unit->nt_msgoutcount = 0;
		unit->nt_msgouttype = 0;
		glm_syncio_msg_init(glm, unit);
		glm_syncio_state(glm, unit, NSYNC_SDTR_SENT, 0, 0);
		return (NACTION_EXT_MSG_OUT | action);
	}

	/* target and hba couldn't agree on sync i/o parms, so */
	/* set all LUN's on this target to async mode until */
	/* next bus reset */
	glm_syncio_state(glm, unit, NSYNC_SDTR_DONE, 0, 0);
	return (NACTION_MSG_REJECT | action);
}

/*
 * The chip uses a two stage divisor chain. The first stage can
 * divide by 1, 1.5, 2, 3, 4, 6 and 8.  The second stage can
 * divide by values from 4 to 11 (inclusive).
 *
 * glmreg.h has the clock divisor table (glm_ccf[]).  It supports
 * the clock divisors on the 810A, 875/876, and 895/896.
 *
 * 810A:    supports divide by 1, 1.5, 2, 3.
 * 875/876: supports divide by 1, 1.5, 2, 3, 4.
 * 895/896: supports divide by 1, 1.5, 2, 3, 4, 6, 8.
 *
 * see glm->g_max_div for individual chips max divisor.
 *
 * Find the clock divisor which gives a period that is at least
 * as long as syncioperiod. If an divisor can't be found that
 * gives the exactly desired syncioperiod then the divisor which
 * results in the next longer valid period will be returned.
 *
 * The divisors are scaled by a factor of ten to handle
 * the .5 fractional values.  glm could have just scaled everything
 * by a factor of two but x10 is easier to understand and easier
 * to setup.
 */
static int
glm_max_sync_divisor(glm_t *glm, int syncioperiod, uchar_t *sxferp,
    uchar_t *sscfX10p)
{
	int divX1000, j;
	uchar_t tp;
	uchar_t max_tp;

	NDBG31(("glm_max_sync_divisor: period=%x sxferp=%x sscfX10p=%x",
	    syncioperiod, *sxferp, *sscfX10p));

	divX1000 = (syncioperiod * 1000);
	divX1000 /= glm->g_speriod;

	/*
	 * Set sxferp and sscfX10p to the slowest value supported
	 * by this chip.
	 */
	if ((glm->g_devid == GLM_53c1010_33) ||
	    (glm->g_devid == GLM_53c1010_66)) {
		*sxferp = max_tp = MIN_TP;
	} else {
		max_tp = MAX_TP;
		*sxferp = MAX_TP;
	}
	*sscfX10p = glm_ccf[(glm->g_max_div - 1)];

	/*
	 * Make sure the request fits into our synchronous capabilities.
	 */
	if (divX1000 > ((*sxferp) * (*sscfX10p))) {
		return (FALSE);
	}

	/*
	 * Find the divisor (glm_ccf[j]) and transfer period (tp)
	 * for this device.
	 *
	 * The SCSI clock is divided twice.  Once by the divisor table
	 * (glm_ccf[]) for this chip, seconds by the transfer period (tp).
	 *
	 * Example:
	 *
	 * SCLK is 160Mhz, The target device is FastSCSI (100ns).
	 *
	 * divX1000 = ((syncperiod * 1000) / 625) = *160*
	 *
	 * Going through these two for loops, 100ns is found when
	 * tp = 8 and glm_ccf[] = 20, 8 * 20 = *160*.  This 160 equals
	 * divX1000 (160).
	 *
	 * If an exact match can not be found, the next highest value
	 * is used.  The LSI part can compensate for the difference.
	 */
	for (j = 0; j < glm->g_max_div; j++) {
		for (tp = MIN_TP; tp <= max_tp; tp++) {
			if (((tp * glm_ccf[j]) >= divX1000)) {
				if ((tp * glm_ccf[j]) <
				    (((*sxferp) * (*sscfX10p)))) {
					*sxferp = tp; *sscfX10p = glm_ccf[j];
				}
			}
		}
	}
	NDBG31(("glm%d: sclock=%d period=%d sxferp=%d sscfX10p=%d",
	    glm->g_instance, glm->g_sclock, syncioperiod,
	    *sxferp, *sscfX10p));

	return (TRUE);
}

static int
glm_period_round(glm_t *glm, int syncioperiod)
{
	int	clkperiod;
	uchar_t	sxfer;
	uchar_t	sscfX10;
	int	tmp;

	NDBG31(("glm_period_round: period=%x", syncioperiod));

	if (glm_max_sync_divisor(glm, syncioperiod, &sxfer, &sscfX10)) {
		clkperiod = glm->g_speriod;

		switch (sscfX10) {
		case 10:
			/* times 1 */
			tmp = (clkperiod * sxfer);
			return (tmp/100);
		case 15:
			/* times 1.5 */
			tmp = (15 * clkperiod * sxfer);
			return ((tmp + 5) / 1000);
		case 20:
			/* times 2 */
			tmp = (2 * clkperiod * sxfer);
			return (tmp/100);
		case 30:
			/* times 3 */
			tmp = (3 * clkperiod * sxfer);
			return (tmp/100);
		case 40:
			/* times 4 */
			tmp = (4 * clkperiod * sxfer);
			return (tmp/100);
		case 60:
			/* times 6 */
			tmp = (6 * clkperiod * sxfer);
			return (tmp/100);
		case 80:
			/* times 8 */
			tmp = (8 * clkperiod * sxfer);
			return (tmp/100);
		}
	}
	return (-1);
}

/*
 * Determine frequency of the HBA's clock chip and determine what
 * rate to use for synchronous i/o on each target. Because of the
 * way the chip's divisor chain works it's only possible to achieve
 * timings that are integral multiples of the clocks fundamental
 * period.
 */
static void
glm_max_sync_rate_init(glm_t *glm)
{
	int i;
	static char *prop_cfreq = "clock-frequency";

	NDBG31(("glm_max_sync_rate_init"));

	/*
	 * Determine clock frequency of attached Symbios chip.
	 */
	if ((glm->g_devid == GLM_53c1010_33) ||
	    (glm->g_devid == GLM_53c1010_66)) {
		glm->g_sclock = 40;
	} else {
		i = ddi_prop_get_int(DDI_DEV_T_ANY, glm->g_dip,
		    DDI_PROP_DONTPASS, prop_cfreq, (40 * MEG));
		glm->g_sclock = (i/(MEG));
	}

	/*
	 * Double the clock for UltraSCSI.
	 */
	if (glm->g_devid == GLM_53c875) {
		ClrSetBits(glm->g_devaddr + NREG_STEST1, 0, NB_STEST1_PLLEN);
		drv_usecwait(20);
		ClrSetBits(glm->g_devaddr + NREG_STEST3, 0, NB_STEST3_HSC);
		ClrSetBits(glm->g_devaddr + NREG_STEST1, 0, NB_STEST1_PLLSEL);
		ClrSetBits(glm->g_devaddr + NREG_STEST3, NB_STEST3_HSC, 0);
		glm->g_sclock *= 2;
	}

	/*
	 * All version of these chips support the PLL clock Quadrupler.
	 */
	if ((glm->g_devid == GLM_53c895) || (glm->g_devid == GLM_53c896) ||
	    (glm->g_devid == GLM_53c1010_33) ||
	    (glm->g_devid == GLM_53c1010_66)) {
		int loop = 0;
		uint8_t stest4;

		ClrSetBits(glm->g_devaddr + NREG_STEST1, 0, NB_STEST1_PLLEN);

		if ((glm->g_devid == GLM_53c1010_33) ||
		    (glm->g_devid == GLM_53c1010_66)) {
			drv_usecwait(50);
		} else {
			/*
			 * The manual says that the PLL will take about 100us,
			 * but testing has shown that it is usually much less.
			 *
			 * If the hardware is broken, we don't want to get into
			 * an infinite loop, so only go around 100 times.
			 */
			do {
				drv_usecwait(25);
				stest4 = ddi_get8(glm->g_datap,
				    (uint8_t *)(glm->g_devaddr + NREG_STEST4));
			} while (((stest4 & NB_STEST4_LOCK) == 0x0) &&
			    (++loop < 100));
		}

		if (loop < 100) {
			ClrSetBits(glm->g_devaddr + NREG_STEST3,
			    0, NB_STEST3_HSC);
			ClrSetBits(glm->g_devaddr + NREG_STEST1,
			    0, NB_STEST1_PLLSEL);
			ClrSetBits(glm->g_devaddr + NREG_STEST3,
			    NB_STEST3_HSC, 0);
			glm->g_sclock *= 4;
		}
	}

	/*
	 * calculate the fundamental period in nanoseconds.
	 *
	 * FAST   SCSI = 2500 (25ns * 100)
	 * Ultra  SCSI = 1250 (12.5ns * 100)
	 * Ultra2 SCSI =  625 (6.25ns * 100)
	 * This is needed so that for UltraSCSI timings, we don't
	 * lose the .5.
	 */
	glm->g_speriod = (100000 / glm->g_sclock);



	/*
	 * Round max sync rate to the closest value the hba's
	 * divisor chain can produce.
	 *
	 * equation for CONVERT_PERIOD:
	 *
	 * For FAST SCSI: ((2500 << 2) / 100) = 100ns.
	 * For FAST-20	: ((1250 << 2) / 100) =  50ns.
	 * For FAST-40	: (( 625 << 2) / 100) =  25ns.
	 */
	if ((glm->g_hba_period =
	    glm_period_round(glm, CONVERT_PERIOD(glm->g_speriod))) <= 0) {
		glm_syncio_disable(glm);
		return;
	}

	/*
	 * Set each target to the correct period.
	 */
	for (i = 0; i < NTARGETS_WIDE; i++) {
		if (glm->g_target_scsi_options[i] & SCSI_OPTIONS_FAST80 &&
		    glm->g_hba_period == DEFAULT_FAST40SYNC_PERIOD) {
			glm->g_minperiod[i] = DEFAULT_FAST40SYNC_PERIOD;
		} else if (glm->g_target_scsi_options[i] &
		    SCSI_OPTIONS_FAST40 &&
		    glm->g_hba_period == DEFAULT_FAST40SYNC_PERIOD) {
			glm->g_minperiod[i] = DEFAULT_FAST40SYNC_PERIOD;
		} else if
		    ((glm->g_target_scsi_options[i] & SCSI_OPTIONS_FAST20) &&
		    (glm->g_hba_period == DEFAULT_FAST20SYNC_PERIOD)) {
			glm->g_minperiod[i] = DEFAULT_FAST20SYNC_PERIOD;
		} else if (glm->g_target_scsi_options[i] & SCSI_OPTIONS_FAST) {
			glm->g_minperiod[i] = DEFAULT_FASTSYNC_PERIOD;
		} else {
			glm->g_minperiod[i] = DEFAULT_SYNC_PERIOD;
		}
	}
}

static void
glm_sync_wide_backoff(struct glm *glm, struct glm_unit *unit)
{
	uchar_t target = unit->nt_target;
	ushort_t tshift = (1<<target);

	NDBG31(("glm_sync_wide_backoff: unit=0x%p", (void *)unit));

#ifdef GLM_TEST
	if (glm_no_sync_wide_backoff) {
		return;
	}
#endif
	/*
	 * if this not the first time then disable wide or this
	 * is the first time and sync is already disabled.
	 */
	if (glm->g_backoff & tshift ||
	    (unit->nt_dsap->nt_selectparm.nt_sxfer & 0x1f) == 0) {
		if ((glm->g_nowide & tshift) == 0) {
			glm_log(glm, CE_WARN,
			    "Target %d disabled wide SCSI mode",
			    target);
			cmn_err(CE_WARN,
			    "!ID[SUNWpd.glm.sync_wide_backoff.6012]");
		}
		/*
		 * do not reset the bit in g_nowide because that
		 * would not force a renegotiation of wide
		 * and do not change any register value yet because
		 * we may have reconnects before the renegotiations
		 */
		glm->g_target_scsi_options[target] &= ~SCSI_OPTIONS_WIDE;
	}

	if ((unit->nt_dsap->nt_selectparm.nt_sxfer & 0x1f) != 0) {
		if (glm->g_backoff & tshift &&
		    (unit->nt_dsap->nt_selectparm.nt_sxfer & 0x1f)) {
			glm_log(glm, CE_WARN,
			    "Target %d reverting to async. mode", target);
			cmn_err(CE_WARN,
			    "!ID[SUNWpd.glm.sync_wide_backoff.6013]");
			glm->g_target_scsi_options[target] &=
			    ~(SCSI_OPTIONS_SYNC | SCSI_OPTIONS_FAST);
		} else {
			int period = glm->g_minperiod[target];

			/*
			 * backoff sync 100%.
			 */
			period = (period * 2);

			/*
			 * Backing off sync on slow devices when
			 * using Ultra timings may generate sync
			 * periods that are greater than our max sync.
			 * Adjust up to our max sync.
			 */
			if (period > MAX_SYNC_PERIOD(glm)) {
				period = MAX_SYNC_PERIOD(glm);
			}

			period = glm_period_round(glm, period);

			if (period > DEFAULT_FAST20SYNC_PERIOD) {
				glm->g_target_scsi_options[target] &=
				    ~SCSI_OPTIONS_FAST20;
			}

			glm->g_minperiod[target] = period;

			glm_log(glm, CE_WARN,
			    "Target %d reducing sync. transfer rate", target);
			cmn_err(CE_WARN,
			    "!ID[SUNWpd.glm.sync_wide_backoff.6014]");
		}
	}
	glm->g_backoff |= tshift;
	glm->g_props_update |= (1<<target);
	glm_force_renegotiation(glm, target);
}

static void
glm_force_renegotiation(struct glm *glm, int target)
{
	ushort_t tshift = (1<<target);

	NDBG31(("glm_force_renegotiation: target=%x", target));

	if (glm->g_syncstate[target] == NSYNC_SDTR_DONE) {
		glm->g_syncstate[target] = NSYNC_SDTR_NOTDONE;
	}
	glm->g_wide_known &= ~tshift;
	glm->g_wide_enabled &= ~tshift;
	glm->g_ppr_known &= ~tshift;
}

/*
 * wide data xfer negotiation handling
 */
static void
glm_make_wdtr(struct glm *glm, struct glm_unit *unit, uchar_t width)
{
	struct glm_dsa *dsap;
	uint_t msgcount;

	NDBG31(("glm_make_wdtr: unit=0x%p width=%x", (void *)unit, width));

	dsap = unit->nt_dsap;
	msgcount = unit->nt_msgoutcount;

	if (((glm->g_target_scsi_options[unit->nt_target] &
	    SCSI_OPTIONS_WIDE) == 0) ||
	    (glm->g_nowide & (1<<unit->nt_target))) {
			glm->g_nowide |= (1<<unit->nt_target);
			width = 0;
	}

	if (glm->g_force_narrow & (1<<unit->nt_target)) {
		width = 0;
	}

	width = min(GLM_XFER_WIDTH, width);

	unit->nt_msgouttype = MSG_WIDE_DATA_XFER;
	dsap->nt_msgoutbuf[msgcount++] = (uchar_t)MSG_EXTENDED;
	dsap->nt_msgoutbuf[msgcount++] = (uchar_t)2;
	dsap->nt_msgoutbuf[msgcount++] = (uchar_t)MSG_WIDE_DATA_XFER;
	dsap->nt_msgoutbuf[msgcount++] = (uchar_t)width;

	dsap->nt_sendmsg.count = msgcount;
	unit->nt_msgoutcount = msgcount;

	/*
	 * increment wdtr flag, odd value indicates that we initiated
	 * the negotiation.
	 */
	glm->g_wdtr_sent++;

	/*
	 * the target may reject the optional wide message so
	 * to avoid negotiating on every cmd, set wide known here
	 */
	glm->g_wide_known |= (1<<unit->nt_target);

	glm_set_wide_scntl3(glm, unit, width);
}


static void
glm_set_wide_scntl3(struct glm *glm, struct glm_unit *unit, uchar_t width)
{
	uint16_t t = unit->nt_target;
	uint16_t l;

	NDBG31(("glm_set_wide_scntl3: unit=0x%p width=%x",
	    (void *)unit, width));

	ASSERT(width <= 1);
	switch (width) {
	case 0:
		for (l = 0; l < NLUNS_GLM; l++) {
			if ((unit = NTL2UNITP(glm, t, l)) != NULL) {
				unit->nt_dsap->nt_selectparm.nt_scntl3 &=
				    ~NB_SCNTL3_EWS;
			}
		}
		glm->g_dsa->g_reselectparm[t].g_scntl3 &= ~NB_SCNTL3_EWS;
		ClrSetBits(glm->g_devaddr + NREG_SCNTL3, NB_SCNTL3_EWS, 0);
		break;
	case 1:
		/*
		 * The scntl3:NB_SCNTL3_EWS bit controls wide.
		 */
		for (l = 0; l < NLUNS_GLM; l++) {
			if ((unit = NTL2UNITP(glm, t, l)) != NULL) {
				unit->nt_dsap->nt_selectparm.nt_scntl3 |=
				    NB_SCNTL3_EWS;
			}
		}
		glm->g_dsa->g_reselectparm[t].g_scntl3 |= NB_SCNTL3_EWS;
		ClrSetBits(glm->g_devaddr + NREG_SCNTL3, 0, NB_SCNTL3_EWS);
		glm->g_wide_enabled |= (1<<t);
		break;
	}
}

/*
 * 87x chip handling
 */
static void
glm53c87x_reset(glm_t *glm)
{
	caddr_t	devaddr = glm->g_devaddr;
	ddi_acc_handle_t datap = glm->g_datap;

	NDBG22(("glm53c87x_reset: devaddr=0x%p", (void *)devaddr));

	/* Reset the 53c87x chip */
	(void) ddi_put8(datap, (uint8_t *)(devaddr + NREG_ISTAT),
	    NB_ISTAT_SRST);

	/* wait a tick and then turn the reset bit off */
	drv_usecwait(100);
	(void) ddi_put8(datap, (uint8_t *)(devaddr + NREG_ISTAT), 0);

	/* clear any pending SCSI interrupts */
	(void) ddi_get8(datap, (uint8_t *)(devaddr + NREG_SIST0));

	/* need short delay before reading SIST1 */
	(void) ddi_get32(datap, (uint32_t *)(devaddr + NREG_SCRATCHA));

	(void) ddi_get32(datap, (uint32_t *)(devaddr + NREG_SCRATCHA));

	(void) ddi_get8(datap, (uint8_t *)(devaddr + NREG_SIST1));

	/* need short delay before reading DSTAT */
	(void) ddi_get32(datap, (uint32_t *)(devaddr + NREG_SCRATCHA));
	(void) ddi_get32(datap, (uint32_t *)(devaddr + NREG_SCRATCHA));

	/* clear any pending DMA interrupts */
	(void) ddi_get8(datap, (uint8_t *)(devaddr + NREG_DSTAT));

	/* rewrite the SCRATCHB register that was reset */
	ddi_put32(glm->g_datap,
	    (uint32_t *)(glm->g_devaddr + NREG_SCRATCHB), glm->g_dsa_addr);

	NDBG1(("NCR53c87x: Software reset completed"));
}

static void
glm53c87x_init(glm_t *glm)
{
	int length;
	dev_info_t *devi;
	char *valuep = 0;
	caddr_t	devaddr = glm->g_devaddr;
	ddi_acc_handle_t datap = glm->g_datap;

	NDBG0(("glm53c87x_init: devaddr=0x%p", (void *)devaddr));

	/* Enable Parity checking and generation */
	ClrSetBits(devaddr + NREG_SCNTL0, 0,
	    (NB_SCNTL0_EPC | NB_SCNTL0_AAP));

	/* disable extra clock cycle of data setup so that */
	/* the hba can do 10MB/sec fast scsi */
	ClrSetBits(devaddr + NREG_SCNTL1, NB_SCNTL1_EXC, 0);

	/* Set the HBA's SCSI id, and enable reselects */
	ClrSetBits(devaddr + NREG_SCID, NB_SCID_ENC,
	    ((glm->g_glmid & NB_SCID_ENC) | NB_SCID_RRE));

	/* Disable auto switching */
	ClrSetBits(devaddr + NREG_DCNTL, 0, NB_DCNTL_COM);

	/* set the selection time-out value. */
	ClrSetBits(devaddr + NREG_STIME0, NB_STIME0_SEL,
	    (uchar_t)glm_selection_timeout);

	/* Set the scsi id bit to match the HBA's idmask */
	ddi_put16(datap, (uint16_t *)(devaddr + NREG_RESPID),
	    (1 << glm->g_glmid));

	/* disable SCSI-1 single initiator option */
	/* enable TolerANT (active negation) */
	ClrSetBits(devaddr + NREG_STEST3, 0, (NB_STEST3_TE | NB_STEST3_DSI));

	/* setup the minimum transfer period (i.e. max transfer rate) */
	/* for synchronous i/o for each of the targets */
	glm_max_sync_rate_init(glm);

	/*
	 * CCF bits are obsolete in 1010
	 */
	if ((glm->g_devid != GLM_53c1010_33) &&
	    (glm->g_devid != GLM_53c1010_66)) {
		/*
		 * Set the scsi core divisor.  glm only supports
		 * SCLK of 40, 80 and 160Mhz.
		 */
		if (glm->g_sclock == 40) {
			glm->g_scntl3 = NB_SCNTL3_CCF2;
		} else if (glm->g_sclock == 80) {
			glm->g_scntl3 = NB_SCNTL3_CCF4;
		} else if (glm->g_sclock == 160) {
			glm->g_scntl3 = NB_SCNTL3_CCF8;
		}
	}

	ddi_put8(datap, (uint8_t *)(devaddr + NREG_SCNTL3), glm->g_scntl3);

	/*
	 * If this device is the 875, enable scripts prefetching,
	 * PCI read line cmd, large DMA fifo and Cache Line Size Enable.
	 */
	if (glm->g_devid == GLM_53c810) {
		ClrSetBits(devaddr + NREG_DCNTL, 0,
		    NB_DCNTL_CLSE | NB_DCNTL_PFEN);

		ClrSetBits(devaddr + NREG_DMODE, 0,
		    (0x80 | NB_DMODE_ERL | NB_DMODE_BOF));

	} else if (glm->g_devid == GLM_53c875) {

		uchar_t	dmode;
		uchar_t	ctest5;

		ClrSetBits(devaddr + NREG_DCNTL, 0, NB_DCNTL_CLSE);

		switch (GLM_REV(glm)) {
		case REV1:
		case REV2:
		case REV3:
			break;
		default:
			ClrSetBits(devaddr + NREG_DCNTL, 0, NB_DCNTL_PFEN);
			break;
		}

		/*
		 * If this is a Symbios 53c876, disable overlapping
		 * arbitration.
		 */
		if (IS_876(glm)) {
			ClrSetBits((devaddr + NREG_CTEST0), 0, NB_CTEST0_NOARB);
		}

		/*
		 * Set dmode reg for 875
		 */
		ClrSetBits(devaddr + NREG_DMODE, 0,
		    (NB_DMODE_BL | NB_DMODE_ERL | NB_DMODE_BOF));

		/*
		 * This bit along with bits 7&8 of the dmode register are
		 * are used to determine burst size.
		 *
		 * Also enable use of larger dma fifo in the 875.
		 */
		ClrSetBits(devaddr + NREG_CTEST5, 0,
		    (NB_CTEST5_DFS | NB_CTEST5_BL2));

		/*
		 * Check for differential.
		 */
		if ((ddi_get8(glm->g_datap,
		    (uint8_t *)(glm->g_devaddr + NREG_GPREG)) &
		    NB_GPREG_GPIO3) == 0) {
				ClrSetBits(glm->g_devaddr + NREG_STEST2,
				    0, NB_STEST2_DIFF);
		}

		/*
		 * Now determine the correct burst size ...
		 */
		if (glm->g_hba_dma_attrs.dma_attr_burstsizes & 0x200) {
			dmode = NB_825_DMODE_BL & ~NB_DMODE_BL;
		} else if (glm->g_hba_dma_attrs.dma_attr_burstsizes & 0x100) {
			dmode = NB_DMODE_BL;
		} else if (glm->g_hba_dma_attrs.dma_attr_burstsizes & 0x080) {
			dmode = 0;
		} else if (glm->g_hba_dma_attrs.dma_attr_burstsizes & 0x040) {
			dmode = NB_825_DMODE_BL;
		} else if (glm->g_hba_dma_attrs.dma_attr_burstsizes & 0x020) {
			dmode = NB_825_DMODE_BL & ~NB_DMODE_BL;
		} else if (glm->g_hba_dma_attrs.dma_attr_burstsizes & 0x010) {
			dmode = NB_DMODE_BL;
		} else {
			dmode = 0;
		}

		if (glm->g_hba_dma_attrs.dma_attr_burstsizes & 0x380) {
			ctest5 = NB_CTEST5_BL2;
		} else {
			ctest5 = 0;
		}

		ClrSetBits(devaddr + NREG_DMODE, NB_825_DMODE_BL, dmode);
		ClrSetBits(devaddr + NREG_CTEST5, NB_CTEST5_BL2, ctest5);

		NDBG0(("glm53c87x_init: ctest5=0x%x", ctest5));
	} else if ((glm->g_devid == GLM_53c895) ||
	    (glm->g_devid == GLM_53c896) ||
	    (glm->g_devid == GLM_53c1010_33) ||
	    (glm->g_devid == GLM_53c1010_66)) {

		/*
		 * This code is LW8/1280 specific, to fix a hardware bug on
		 * the IO board. The interrupt pin on the 53c1010 is by
		 * default an open-drain output, which requires an external
		 * pullup resistor. That resistor is missing in the hardware
		 * design. This new code clears the 53c1010 IRQM register
		 * bit, which converts the interrupt pin to a push-pull
		 * output, which does not require the external resistor.
		 * That is OK on this HW design, since the interrupt line is
		 * not shared. We should remove this code if and when the HW
		 * is fixed. We are reading the 'name' property which would
		 * be set to SUNW,Netra-T12 on all variants of 1280 platforms
		 * where we are using 53c1010 chip.
		 */
		devi = ddi_root_node();
		if (ddi_prop_op(DDI_DEV_T_ANY, devi, PROP_LEN_AND_VAL_ALLOC,
		    DDI_PROP_DONTPASS, "name", (caddr_t)&valuep, &length)
		    == DDI_PROP_SUCCESS) {
			int rc;
			rc = strncmp(valuep, "SUNW,Netra-T12", 14);
			if (!rc) {
				ClrSetBits(devaddr + NREG_DCNTL, 0,
				    (NB_DCNTL_CLSE | NB_DCNTL_PFEN
				    | NB_DCNTL_IRQM));
			} else {
				ClrSetBits(devaddr + NREG_DCNTL, 0,
				    (NB_DCNTL_CLSE | NB_DCNTL_PFEN));
			}
			kmem_free(valuep, (size_t)length);
		}

		ClrSetBits(devaddr + NREG_DMODE, 0,
		    (NB_DMODE_BL | NB_DMODE_ERL | NB_DMODE_BOF));

		if ((glm->g_devid == GLM_53c1010_33) ||
		    (glm->g_devid == GLM_53c1010_66)) {
			/* NB_CTEST5_DFS gone for GLM_53c1010 */
			ClrSetBits(devaddr + NREG_CTEST5, 0,
			    NB_CTEST5_BL2);

			/*
			 * Enable PMM logic
			 */
			ddi_put32(glm->g_datap,
			    (uint32_t *)(glm->g_devaddr + NREG_PMJAD1),
			    glm->g_glm_scripts[NSS_PMM]);
			ddi_put32(glm->g_datap,
			    (uint32_t *)(glm->g_devaddr + NREG_PMJAD2),
			    glm->g_glm_scripts[NSS_PMM]);

			ClrSetBits(glm->g_devaddr + NREG_CCNTL0,
			    NB_CCNTL0_ENNDJ,
			    NB_CCNTL0_ENPMJ);
		} else {
			ClrSetBits(devaddr + NREG_CTEST5, 0,
			    (NB_CTEST5_DFS | NB_CTEST5_BL2));
		}

		/*
		 * Check the mode of the SCSI Bus
		 */
		(void) glm_check_smode(glm);
	} else {
		ClrSetBits(devaddr + NREG_DMODE, 0,
		    (NB_825_DMODE_BL | NB_DMODE_ERL));
	}

	NDBG0(("glm53c87x_init: devaddr=0x%p completed", (void *)devaddr));
}

static void
glm53c87x_enable(glm_t *glm)
{
	caddr_t	devaddr = glm->g_devaddr;

	NDBG0(("glm53c87x_enable"));

	/* enable all fatal interrupts, disable all non-fatal interrupts */
	ClrSetBits(devaddr + NREG_SIEN0, (NB_SIEN0_CMP | NB_SIEN0_SEL |
	    NB_SIEN0_RSL), (NB_SIEN0_MA | NB_SIEN0_SGE | NB_SIEN0_UDC |
	    NB_SIEN0_RST | NB_SIEN0_PAR));

	/* enable all fatal interrupts, disable all non-fatal interrupts */
	ClrSetBits(devaddr + NREG_SIEN1, (NB_SIEN1_GEN | NB_SIEN1_HTH),
	    NB_SIEN1_STO);

	/* enable all valid except SCRIPT Step Interrupt */
	ClrSetBits(devaddr + NREG_DIEN, NB_DIEN_SSI, (NB_DIEN_MDPE |
	    NB_DIEN_BF | NB_DIEN_ABRT | NB_DIEN_SIR | NB_DIEN_IID));

	/* enable master parity error detection logic */
	ClrSetBits(devaddr + NREG_CTEST4, 0, NB_CTEST4_MPEE);
}

static void
glm53c87x_disable(glm_t *glm)
{
	caddr_t	devaddr = glm->g_devaddr;

	NDBG0(("glm53c87x_disable"));

	/* disable all SCSI interrupts */
	ClrSetBits(devaddr + NREG_SIEN0, (NB_SIEN0_MA | NB_SIEN0_CMP |
	    NB_SIEN0_SEL | NB_SIEN0_RSL | NB_SIEN0_SGE | NB_SIEN0_UDC |
	    NB_SIEN0_RST | NB_SIEN0_PAR), 0);

	ClrSetBits(devaddr + NREG_SIEN1, (NB_SIEN1_GEN | NB_SIEN1_HTH |
	    NB_SIEN1_STO), 0);

	/* disable all DMA interrupts */
	ClrSetBits(devaddr + NREG_DIEN, (NB_DIEN_MDPE | NB_DIEN_BF |
	    NB_DIEN_ABRT | NB_DIEN_SSI | NB_DIEN_SIR | NB_DIEN_IID), 0);

	/* disable master parity error detection */
	ClrSetBits(devaddr + NREG_CTEST4, NB_CTEST4_MPEE, 0);
}

static uchar_t
glm53c87x_get_istat(glm_t *glm)
{
	NDBG1(("glm53c87x_get_istat"));

	return (ddi_get8(glm->g_datap,
	    (uint8_t *)(glm->g_devaddr + NREG_ISTAT)));
}

static void
glm53c87x_halt(glm_t *glm)
{
	caddr_t	devaddr = glm->g_devaddr;
	uchar_t	first_time = TRUE;
	int	loopcnt;
	uchar_t	istat;
	uchar_t	dstat;

	NDBG1(("glm53c87x_halt"));

	/* turn on the abort bit */
	istat = NB_ISTAT_ABRT;
	ddi_put8(glm->g_datap, (uint8_t *)(devaddr + NREG_ISTAT), istat);

	/* wait for and clear all pending interrupts */
	for (;;) {

		/* wait up to 1 sec. for a DMA or SCSI interrupt */
		for (loopcnt = 0; loopcnt < 1000; loopcnt++) {
			istat = glm53c87x_get_istat(glm);
			if (istat & (NB_ISTAT_SIP | NB_ISTAT_DIP))
				goto got_it;

			/* wait 1 millisecond */
			drv_usecwait(1000);
		}
		NDBG10(("glm53c87x_halt: 0x%p: can't halt", (void *)devaddr));
		return;

	got_it:
		/* if there's a SCSI interrupt pending clear it and loop */
		if (istat & NB_ISTAT_SIP) {
			/* reset the sip latch registers */
			(void) ddi_get8(glm->g_datap,
			    (uint8_t *)(devaddr + NREG_SIST0));

			/* need short delay before reading SIST1 */
			(void) ddi_get32(glm->g_datap,
			    (uint32_t *)(devaddr + NREG_SCRATCHA));
			(void) ddi_get32(glm->g_datap,
			    (uint32_t *)(devaddr + NREG_SCRATCHA));

			(void) ddi_get8(glm->g_datap,
			    (uint8_t *)(devaddr + NREG_SIST1));
			continue;
		}

		if (first_time) {
			/* reset the abort bit before reading DSTAT */
			ddi_put8(glm->g_datap,
			    (uint8_t *)(devaddr + NREG_ISTAT), 0);
			first_time = FALSE;
		}
		/* read the DMA status register */
		dstat = ddi_get8(glm->g_datap,
		    (uint8_t *)(devaddr + NREG_DSTAT));
		if (dstat & NB_DSTAT_ABRT) {
			/* got the abort interrupt */
			NDBG10(("glm53c87x_halt: devaddr=0x%p: okay",
			    (void *)devaddr));
			return;
		}
		/* must have been some other pending interrupt */
		drv_usecwait(1000);
	}
}

/*
 * Utility routine; check for error in execution of command in ccb,
 * handle it.
 */
static void
glm53c87x_check_error(glm_unit_t *unit, struct scsi_pkt *pktp)
{
	NDBG1(("glm53c87x_check_error: pkt=0x%p", (void *)pktp));

	/* store the default error results in packet */
	pktp->pkt_state |= STATE_GOT_BUS;

	if (unit->nt_status0 == 0 && unit->nt_status1 == 0 &&
	    unit->nt_dma_status == 0) {
		NDBG1(("glm53c87x_check_error: A"));
		pktp->pkt_statistics |= STAT_BUS_RESET;
		pktp->pkt_reason = CMD_RESET;
		return;
	}

	if (unit->nt_status1 & NB_SIST1_STO) {
		NDBG1(("glm53c87x_check_error: B"));
		pktp->pkt_state |= STATE_GOT_BUS;
	}
	if (unit->nt_status0 & NB_SIST0_UDC) {
		NDBG1(("glm53c87x_check_error: C"));
		pktp->pkt_state |= (STATE_GOT_BUS | STATE_GOT_TARGET);
		pktp->pkt_statistics = 0;
	}
	if (unit->nt_status0 & NB_SIST0_RST) {
		NDBG1(("glm53c87x_check_error: D"));
		pktp->pkt_state |= STATE_GOT_BUS;
		pktp->pkt_statistics |= STAT_BUS_RESET;
	}
	if (unit->nt_status0 & NB_SIST0_PAR) {
		NDBG1(("glm53c87x_check_error: E"));
		pktp->pkt_statistics |= STAT_PERR;
	}
	if (unit->nt_dma_status & NB_DSTAT_ABRT) {
		NDBG1(("glm53c87x_check_error: F"));
		pktp->pkt_statistics |= STAT_ABORTED;
	}


	/* Determine the appropriate error reason */

	/* watch out, on the 8xx chip the STO bit was moved */
	if (unit->nt_status1 & NB_SIST1_STO) {
		NDBG1(("glm53c87x_check_error: G"));
		pktp->pkt_reason = CMD_INCOMPLETE;
	} else if (unit->nt_status0 & NB_SIST0_UDC) {
		NDBG1(("glm53c87x_check_error: H"));
		pktp->pkt_reason = CMD_UNX_BUS_FREE;
	} else if (unit->nt_status0 & NB_SIST0_RST) {
		NDBG1(("glm53c87x_check_error: I"));
		pktp->pkt_reason = CMD_RESET;
	} else if (unit->nt_status0 & NB_SIST0_PAR) {
		pktp->pkt_reason = CMD_TRAN_ERR;
	} else {
		NDBG1(("glm53c87x_check_error: J"));
		pktp->pkt_reason = CMD_INCOMPLETE;
	}
}

/*
 * for SCSI or DMA errors I need to figure out reasonable error
 * recoveries for all combinations of (hba state, scsi bus state,
 * error type). The possible error recovery actions are (in the
 * order of least to most drastic):
 *
 *	1. send message parity error to target
 *	2. send abort
 *	3. send abort tag
 *	4. send initiator detected error
 *	5. send bus device reset
 *	6. bus reset
 */
static uint_t
glm53c87x_dma_status(glm_t *glm)
{
	glm_unit_t	*unit;
	caddr_t devaddr = glm->g_devaddr;
	uint_t	 action = 0;
	uchar_t	 dstat;

	/* read DMA interrupt status register, and clear the register */
	dstat = ddi_get8(glm->g_datap, (uint8_t *)(devaddr + NREG_DSTAT));

	NDBG21(("glm53c87x_dma_status: devaddr=0x%p dstat=0x%x",
	    (void *)devaddr, dstat));

	/*
	 * DMA errors leave the HBA connected to the SCSI bus.
	 * Need to clear the bus and reset the chip.
	 */
	switch (glm->g_state) {
	case NSTATE_IDLE:
		/* this shouldn't happen */
		glm_report_fault(glm->g_dip, glm, DDI_SERVICE_UNAFFECTED,
		    DDI_DEVICE_FAULT, "Unexpected DMA state: IDLE. dstat=%b",
		    dstat, dstatbits);
		action = (NACTION_DO_BUS_RESET | NACTION_SIOP_REINIT);
		break;

	case NSTATE_ACTIVE:
		unit = glm->g_current;
		unit->nt_dma_status |= dstat;
		if (dstat & NB_DSTAT_ERRORS) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "Unexpected DMA state: ACTIVE. dstat=%b",
			    dstat, dstatbits);
			action = (NACTION_SIOP_REINIT | NACTION_DO_BUS_RESET |
			    NACTION_ERR);
		} else if (dstat & NB_DSTAT_SIR) {
			/* SCRIPT software interrupt */
			action |= NACTION_CHK_INTCODE;
		}
		break;

	case NSTATE_WAIT_RESEL:
		if (dstat & NB_DSTAT_ERRORS) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "Unexpected DMA state: WAIT. dstat=%b",
			    dstat, dstatbits);
			action = NACTION_SIOP_REINIT | NACTION_DO_BUS_RESET;
		} else if (dstat & NB_DSTAT_SIR) {
			/* SCRIPT software interrupt */
			action |= NACTION_CHK_INTCODE;
		}
		break;
	}
	return (action);
}

static uint_t
glm53c87x_scsi_status(glm_t *glm)
{
	glm_unit_t	*unit;
	caddr_t	 devaddr = glm->g_devaddr;
	uint_t	 action = 0;
	uchar_t	 sist0;
	uchar_t	 sist1;
	uchar_t	 scntl1;
	hrtime_t tick;

	NDBG1(("glm53c87x_scsi_status"));

	/* read SCSI interrupt status register, and clear the register */
	sist0 = ddi_get8(glm->g_datap, (uint8_t *)(devaddr + NREG_SIST0));
	tick = gethrtime();
	/*
	 * Insert 12 CLK cycle delay between reading SIST0 and SIST1
	 * while we wait for interrupts to clear
	 */
	for (;;) {
		if ((gethrtime() - tick) > GLM_INTERRUPT_DELAY)
			break;
	}
	/* Insert a dummy read to force frush */
	(void) ddi_get32(glm->g_datap,
	    (uint32_t *)(devaddr + NREG_SCRATCHA));

	sist1 = ddi_get8(glm->g_datap, (uint8_t *)(devaddr + NREG_SIST1));

	NDBG1(("glm53c87x_scsi_status: devaddr=0x%p sist0=0x%x sist1=0x%x",
	    (void *)devaddr, sist0, sist1));

	/*
	 * the scsi timeout, unexpected disconnect, and bus reset
	 * interrupts leave the bus in the bus free state ???
	 *
	 * the scsi gross error and parity error interrupts leave
	 * the bus still connected ???
	 */
	switch (glm->g_state) {
	case NSTATE_IDLE:
		if ((sist0 & (NB_SIST0_SGE | NB_SIST0_PAR | NB_SIST0_UDC)) ||
		    (sist1 & NB_SIST1_STO)) {
			/* shouldn't happen, clear chip */
			action = (NACTION_CLEAR_CHIP | NACTION_DO_BUS_RESET);
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "unexpected SCSI interrupt while idle");
			break;
		}
		/*
		 * NACTION_SIOP_REINIT is used per LSI's recommendation to
		 * workaround a chip issue with issuing SCSI bus reset
		 * attached in the Bug report 4470577, instead of
		 * NACTION_CLEAR_CHIP.
		 */
		if (sist0 & NB_SIST0_RST) {
			action = (NACTION_SIOP_REINIT | NACTION_GOT_BUS_RESET);
		}

		/*
		 * The scsi bus mode has changed.  clear the chip, and
		 * reset the bus.
		 */
		if (sist1 & NB_SIST1_SBMC) {
			if (glm_check_smode(glm)) {
				action = (NACTION_CLEAR_CHIP |
				    NACTION_DO_BUS_RESET);
			}
		}

		break;

	case NSTATE_ACTIVE:
		unit = glm->g_current;
		unit->nt_status0 |= sist0;
		unit->nt_status1 |= sist1;

		/*
		 * If phase mismatch, then figure out the residual for
		 * the current dma scatter/gather segment
		 */
		if (sist0 & NB_SIST0_MA) {
			action = NACTION_SAVE_BCNT;
		}

		/*
		 * Parity error.  Determine the phase and action.
		 */
		if (sist0 & NB_SIST0_PAR) {
			action = glm_parity_check(glm, unit);
			break;
		}

		if (sist0 & NB_SIST0_SGE) {
			/* attempt recovery if selection done and connected */
			if (ddi_get8(glm->g_datap,
			    (uint8_t *)(devaddr + NREG_SCNTL1))
			    & NB_SCNTL1_CON) {
				action = glm_parity_check(glm, unit);
				break;
			} else {
				action = NACTION_ERR | NACTION_DO_BUS_RESET |
				    NACTION_CLEAR_CHIP;
				glm_report_fault(glm->g_dip, glm,
				    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
				    "gross error - not connected");
				break;
			}
		}

		/*
		 * The target dropped the bus.
		 */
		if (sist0 & NB_SIST0_UDC) {
			if (NSYNCSTATE(glm, unit) == NSYNC_SDTR_SENT) {
				NDBG31(("target dropped sdtr msg."));
				glm_syncio_state(glm,
				    unit, NSYNC_SDTR_REJECT, 0, 0);
			}
			if (glm->g_wdtr_sent) {
				NDBG31(("target dropped wdtr msg."));
				glm_set_wide_scntl3(glm, unit, 0);
				glm->g_wide_enabled &= ~(1<<unit->nt_target);
				glm->g_nowide |= (1<<unit->nt_target);
				glm->g_wdtr_sent = 0;
			}
			action = (NACTION_SAVE_BCNT | NACTION_ERR |
			    NACTION_CLEAR_CHIP);
		}

		/*
		 * selection timeout.
		 * make sure we negotiate when this target comes
		 * on line later on
		 */
		if (sist1 & NB_SIST1_STO) {
			/* bus is now idle */
			action = (NACTION_SAVE_BCNT | NACTION_ERR |
			    NACTION_CLEAR_CHIP);
			glm_force_renegotiation(glm, unit->nt_target);
			if (NSYNCSTATE(glm, unit) == NSYNC_SDTR_SENT) {
				NSYNCSTATE(glm, unit) = NSYNC_SDTR_NOTDONE;
			}
			glm->g_wdtr_sent = 0;
		}
		/*
		 * NACTION_SIOP_REINIT is used per LSI's recommendation to
		 * workaround a chip issue with issuing SCSI bus reset
		 * attached in the Bug report 4470577, instead of
		 * NACTION_CLEAR_CHIP.
		 */
		if (sist0 & NB_SIST0_RST) {
			/* bus is now idle */
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "got SCSI bus reset");
			action = (NACTION_SIOP_REINIT | NACTION_GOT_BUS_RESET |
			    NACTION_ERR);
		}

		/*
		 * The scsi bus mode has changed.  clear the chip, and
		 * reset the bus.
		 */
		if (sist1 & NB_SIST1_SBMC) {
			if (glm_check_smode(glm)) {
				action = (NACTION_CLEAR_CHIP |
				    NACTION_DO_BUS_RESET);
			}
		}
		break;

	case NSTATE_WAIT_RESEL:
		if (sist0 & NB_SIST0_PAR) {
			/* attempt recovery if reconnected */
			scntl1 = ddi_get8(glm->g_datap,
			    (uint8_t *)(glm->g_devaddr + NREG_SCNTL1));

			if (scntl1 & NB_SCNTL1_CON) {
				action = NACTION_MSG_PARITY;
				break;
			} else {
				/* don't respond */
				action = NACTION_BUS_FREE;
			}
		}

		if (sist0 & NB_SIST0_UDC) {
			/* target reselected then disconnected, ignore it */
			action = (NACTION_BUS_FREE | NACTION_CLEAR_CHIP);
		}

		if ((sist0 & NB_SIST0_SGE) || (sist1 & NB_SIST1_STO)) {
			/* shouldn't happen, clear chip and bus reset */
			action = (NACTION_CLEAR_CHIP | NACTION_DO_BUS_RESET);
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "unexpected gross error or timeout"
			    " while not active");
			break;
		}
		/*
		 * NACTION_SIOP_REINIT is used per LSI's recommendation to
		 * workaround a chip issue with issuing SCSI bus reset
		 * attached in the Bug report 4470577, instead of
		 * NACTION_CLEAR_CHIP.
		 */
		if (sist0 & NB_SIST0_RST) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "got SCSI bus reset");
			/* got bus reset, restart the wait for reselect */
			action = (NACTION_SIOP_REINIT | NACTION_GOT_BUS_RESET |
			    NACTION_BUS_FREE);
		}

		/*
		 * The scsi bus mode has changed.  clear the chip, and
		 * reset the bus.
		 */
		if (sist1 & NB_SIST1_SBMC) {
			if (glm_check_smode(glm)) {
				action = (NACTION_CLEAR_CHIP |
				    NACTION_DO_BUS_RESET);
			}
		}
		break;
	}
	NDBG1(("glm53c87x_scsi_status: action=%x", action));
	return (action);
}

/*
 * If the phase-mismatch which precedes the Save Data Pointers
 * occurs within in a Scatter/Gather segment there's a residual
 * byte count that needs to be computed and remembered. It's
 * possible for a Disconnect message to occur without a preceding
 * Save Data Pointers message, so at this point we only compute
 * the residual without fixing up the S/G pointers.
 */
static int
glm53c87x_save_byte_count(glm_t *glm, glm_unit_t *unit)
{
	caddr_t	devaddr = glm->g_devaddr;
	uint_t	dsp;
	int	index;
	uint32_t remain;
	uchar_t	opcode;
	ushort_t tmp;
	uchar_t	sstat0;
	uchar_t	sstat2;
	int	rc;
	ushort_t dfifo;
	ushort_t ctest5;
	uint32_t dbc;

	NDBG17(("glm53c87x_save_byte_count devaddr=0x%p", (void *)devaddr));

	/*
	 * Only need to do this for phase mismatch interrupts
	 * during actual data in or data out move instructions.
	 */
	if ((unit->nt_ncmdp->cmd_flags & CFLAG_DMAVALID) == 0) {
		/* since no data requested must not be S/G dma */
		rc = FALSE;
		goto out;
	}

	/*
	 * fetch the instruction pointer and back it up
	 * to the actual interrupted instruction.
	 */
	dsp = ddi_get32(glm->g_datap, (uint32_t *)(devaddr + NREG_DSP));
	dsp -= 8;

	/* check for MOVE DATA_OUT or MOVE DATA_IN instruction */
	opcode = ddi_get8(glm->g_datap, (uint8_t *)(devaddr + NREG_DCMD));

	if (opcode == (NSOP_MOVE | NSOP_DATAOUT)) {
		/* move data out */
		index = glm->g_do_list_end - dsp;

	} else if (opcode == (NSOP_MOVE | NSOP_DATAIN)) {
		/* move data in */
		index = glm->g_di_list_end - dsp;

	} else {
		/* not doing data dma so nothing to update */
		NDBG17(("glm53c87x_save_byte_count: %x not a move opcode",
		    opcode));
		rc = FALSE;
		goto out;
	}

	/*
	 * convert byte index into S/G DMA list index
	 */
	index = (index/8);

	if (index < 1 || index > GLM_MAX_DMA_SEGS) {
		/* it's out of dma list range, must have been some other move */
		NDBG17((
		    "glm53c87x_save_byte_count: devaddr=0x%p 0x%x not dma",
		    (void *)devaddr, index));
		rc = FALSE;
		goto out;
	}

	if ((glm->g_devid != GLM_53c1010_33) &&
	    (glm->g_devid != GLM_53c1010_66)) {
	/*
	 * If the DMA FIFO size has been increased, the math is different.
	 */
	if (glm->g_options & GLM_OPT_LARGE_FIFO) {
		/* read the dbc register. */
		dbc = (ddi_get32(glm->g_datap,
		    (uint32_t *)(devaddr + NREG_DBC)) & 0xffffff);

		ctest5 = ((ddi_get8(glm->g_datap,
		    (uint8_t *)(devaddr + NREG_CTEST5)) & 0x3) << 8);

		dfifo = (ctest5 |
		    ddi_get8(glm->g_datap, (uint8_t *)(devaddr + NREG_DFIFO)));

		/* actual number left untransferred. */
		remain = dbc + ((dfifo - (dbc & 0x3ff)) & 0x3ff);
	} else {

		/* get the residual from the byte count register */
		dbc =  ddi_get32(glm->g_datap,
		    (uint32_t *)(devaddr + NREG_DBC)) & 0xffffff;

		/* number of bytes stuck in the DMA FIFO */
		dfifo = (ddi_get8(glm->g_datap,
		    (uint8_t *)(devaddr + NREG_DFIFO)) & 0x7f);

		/* actual number left untransferred. */
		remain = dbc + ((dfifo - (dbc & 0x7f)) & 0x7f);
	}
	} else {
		remain = 0;
	}

	/*
	 * Add one if there's a byte stuck in the SCSI fifo
	 */
	tmp = ddi_get8(glm->g_datap, (uint8_t *)(devaddr + NREG_CTEST2));

	if (tmp & NB_CTEST2_DDIR) {
		/* transfer was incoming (SCSI -> host bus) */
		sstat0 = ddi_get8(glm->g_datap,
		    (uint8_t *)(devaddr + NREG_SSTAT0));

		sstat2 = ddi_get8(glm->g_datap,
		    (uint8_t *)(devaddr + NREG_SSTAT2));

		if (sstat0 & NB_SSTAT0_ILF)
			remain++;	/* Add 1 if SIDL reg is full */

		/* Wide byte left async. */
		if (sstat2 & NB_SSTAT2_ILF1)
			remain++;

		/* check for synchronous i/o */
		if (unit->nt_dsap->nt_selectparm.nt_sxfer != 0) {
			tmp = ddi_get8(glm->g_datap,
			    (uint8_t *)(devaddr + NREG_SSTAT1));
			remain += (tmp >> 4) & 0xf;
		}
	} else {
		/* transfer was outgoing (host -> SCSI bus) */
		sstat0 = ddi_get8(glm->g_datap,
		    (uint8_t *)(devaddr + NREG_SSTAT0));

		sstat2 = ddi_get8(glm->g_datap,
		    (uint8_t *)(devaddr + NREG_SSTAT2));

		if (sstat0 & NB_SSTAT0_OLF)
			remain++;	/* Add 1 if data is in SODL reg. */

		/* Check data for wide byte left. */
		if (sstat2 & NB_SSTAT2_OLF1)
			remain++;

		/* check for synchronous i/o */
		if ((unit->nt_dsap->nt_selectparm.nt_sxfer != 0) &&
		    (sstat0 & NB_SSTAT0_ORF)) {
			remain++;	/* Add 1 if data is in SODR */
			/* Check for Wide byte left. */
			if (sstat2 & NB_SSTAT2_ORF1)
				remain++;
		}
	}

	/* update the S/G pointers and indexes */
	glm_sg_update(unit, index, remain);
	rc = TRUE;

out:
	/* Clear the DMA FIFO pointers */
	CLEAR_DMA(glm);

	NDBG17(("glm53c87x_save_byte_count: devaddr=0x%p index=%d remain=%d",
	    (void *)devaddr, index, remain));
	return (rc);
}

static int
glm53c87x_get_target(struct glm *glm, uchar_t *tp)
{
	caddr_t devaddr = glm->g_devaddr;
	uchar_t id;

	/*
	 * get the id byte received from the reselecting target
	 */
	id = ddi_get8(glm->g_datap, (uint8_t *)(devaddr + NREG_SSID));

	NDBG1(("glm53c87x_get_target: devaddr=0x%p lcrc=0x%x",
	    (void *)devaddr, id));

	/* is it valid? */
	if (id & NB_SSID_VAL) {
		/* mask off extraneous bits */
		id &= NB_SSID_ENCID;
		NDBG1(("glm53c87x_get_target: ID %d reselected", id));
		*tp = id;
		return (TRUE);
	}
	glm_log(glm, CE_WARN,
	    "glm53c87x_get_target: invalid reselect id %d", id);
	cmn_err(CE_WARN, "!ID[SUNWpd.glm.87x_get_target.6015]");
	return (FALSE);
}

static void
glm53c87x_set_syncio(glm_t *glm, glm_unit_t *unit)
{
	caddr_t	devaddr = glm->g_devaddr;
	uchar_t	sxfer = unit->nt_dsap->nt_selectparm.nt_sxfer;
	uchar_t	scntl3 = 0;

	NDBG31(("glm53c87x_set_syncio: unit=0x%p", (void *)unit));

	/* Set SXFER register */
	ddi_put8(glm->g_datap, (uint8_t *)(devaddr + NREG_SXFER), sxfer);

	/* Set sync i/o clock divisor in SCNTL3 registers */
	if (sxfer != 0) {
		switch (unit->nt_sscfX10) {
		case 10:
			scntl3 = NB_SCNTL3_SCF1 | glm->g_scntl3;
			break;
		case 15:
			scntl3 = NB_SCNTL3_SCF15 | glm->g_scntl3;
			break;
		case 20:
			scntl3 = NB_SCNTL3_SCF2 | glm->g_scntl3;
			break;
		case 30:
			scntl3 = NB_SCNTL3_SCF3 | glm->g_scntl3;
			break;
		case 40:
			scntl3 = NB_SCNTL3_SCF4 | glm->g_scntl3;
			break;
		case 60:
			scntl3 = NB_SCNTL3_SCF6 | glm->g_scntl3;
			break;
		case 80:
			scntl3 = NB_SCNTL3_SCF8 | glm->g_scntl3;
			break;
		}
		unit->nt_dsap->nt_selectparm.nt_scntl3 |= scntl3;
	}

	ddi_put8(glm->g_datap, (uint8_t *)(devaddr + NREG_SCNTL3),
	    unit->nt_dsap->nt_selectparm.nt_scntl3);

	if ((glm->g_devid == GLM_53c1010_33) ||
	    (glm->g_devid == GLM_53c1010_66)) {
			ddi_put8(glm->g_datap,
			    (uint8_t *)(devaddr + NREG_SCNTL4),
			    unit->nt_dsap->nt_selectparm.nt_scntl4);
	}

	/* set extended filtering if not Fast-SCSI (i.e., < 5MB/sec) */
	if (sxfer == 0 || unit->nt_fastscsi == FALSE) {
		ClrSetBits(devaddr + NREG_STEST2, 0, NB_STEST2_EXT);
	} else {
		ClrSetBits(devaddr + NREG_STEST2, NB_STEST2_EXT, 0);
	}
}

static void
glm53c87x_setup_script(glm_t *glm, glm_unit_t *unit)
{
	caddr_t	devaddr = glm->g_devaddr;
	struct glm_scsi_cmd *cmd = unit->nt_ncmdp;

	uchar_t nleft = cmd->cmd_scc;

	NDBG1(("glm53c87x_setup_script: devaddr=0x%p", (void *)devaddr));

	(void) ddi_dma_sync(unit->nt_dma_p, 0, 0, DDI_DMA_SYNC_FORDEV);
	/* Set the Data Structure address register to */
	/* the physical address of the active table */
	ddi_put32(glm->g_datap,
	    (uint32_t *)(devaddr + NREG_DSA), unit->nt_dsa_addr);

	/*
	 * Setup scratcha0 as the number of segments left to do
	 */
	ddi_put8(glm->g_datap,
	    (uint8_t *)(glm->g_devaddr + NREG_SCRATCHA0), nleft);

	NDBG1(("glm53c87x_setup_script: devaddr=0x%p okay", (void *)devaddr));
}

static void
glm53c87x_bus_reset(glm_t *glm)
{
	caddr_t	devaddr = glm->g_devaddr;

	NDBG22(("glm53c87x_bus_reset"));


	/*
	 * Per Errata/ LSI recommendation attached in bug report 4470577,
	 * need to reset the chip before the issue of SCSI bus reset.
	 * The halting  of the script processor is needed because no CSR
	 * except ISTAT (or MBOX) can be read or written while the
	 * scripts are running.
	 */
	/* Stop the script */
	GLM_HALT(glm);

	/* Reset the chip */
	GLM_RESET(glm);
	GLM_INIT(glm);
	GLM_ENABLE_INTR(glm);

	/* Reset the scsi bus */
	ClrSetBits(devaddr + NREG_SCNTL1, 0, NB_SCNTL1_RST);

	/* Wait at least 25 microsecond */
	drv_usecwait((clock_t)25);

	/* Turn off the bit to complete the reset */
	ClrSetBits(devaddr + NREG_SCNTL1, NB_SCNTL1_RST, 0);
}


/*
 * device and bus reset handling
 *
 * Notes:
 *	- RESET_ALL:	reset the SCSI bus
 *	- RESET_TARGET:	reset the target specified in scsi_address
 */
static int
glm_scsi_reset(struct scsi_address *ap, int level)
{
	glm_t *glm = ADDR2GLM(ap);
	int rval;

	NDBG22(("glm_scsi_reset: target=%x level=%x",
	    ap->a_target, level));

	/* test device at entry point */
	if (ddi_get_devstate(glm->g_dip) == DDI_DEVSTATE_DOWN) {
		return (FALSE);
	}

	mutex_enter(&glm->g_mutex);
	rval = glm_do_scsi_reset(ap, level);
	mutex_exit(&glm->g_mutex);
	return (rval);
}

static int
glm_do_scsi_reset(struct scsi_address *ap, int level)
{
	glm_t *glm = ADDR2GLM(ap);
	int rval = FALSE;

	NDBG22(("glm_do_scsi_reset: target=%x level=%x",
	    ap->a_target, level));

	switch (level) {

	case RESET_ALL:
		/*
		 * Reset the SCSI bus, kill all commands in progress
		 * (remove them from lists, etc.)  Make sure you
		 * wait the specified time for the reset to settle,
		 * if your hardware doesn't do that for you somehow.
		 */
		glm->g_reset_received = 0;
		GLM_BUS_RESET(glm);
		(void) glm_wait_intr(glm, GLM_BUS_RESET_POLL_TIME);
		if (glm->g_reset_received == 0) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_LOST, DDI_DEVICE_FAULT,
			    "timeout on bus reset interrupt");
			glm_flush_hba(glm);
		}
		glm_doneq_empty(glm);
		rval = TRUE;
		break;

	case RESET_TARGET:
		/*
		 * Issue a Bus Device Reset message to the target/lun
		 * specified in ap;
		 */
		if (ADDR2GLMUNITP(ap) == NULL) {
			return (FALSE);
		}

		/*
		 * the flushing is done when NINT_DEV_RESET has been received
		 *
		 * set throttle to HOLD, don't allow any new cmds to start
		 * until the device reset is complete.
		 */
		glm_set_all_lun_throttles(glm, ap->a_target, HOLD_THROTTLE);
		rval = glm_send_dev_reset_msg(ap, glm);

		if (rval == FALSE) {
			glm_set_all_lun_throttles(glm,
			    ap->a_target, MAX_THROTTLE);
		}
		glm_doneq_empty(glm);
		break;
	}
	return (rval);
}

static int
glm_scsi_reset_notify(struct scsi_address *ap, int flag,
	void (*callback)(caddr_t), caddr_t arg)
{
	struct glm *glm = ADDR2GLM(ap);

	NDBG22(("glm_scsi_reset_notify: tgt=%x", ap->a_target));

	return (scsi_hba_reset_notify_setup(ap, flag, callback, arg,
	    &glm->g_mutex, &glm->g_reset_notify_listf));
}

static int
glm_send_dev_reset_msg(struct scsi_address *ap, glm_t *glm)
{
	auto struct glm_scsi_cmd	local_cmd;
	struct glm_scsi_cmd		*cmd = &local_cmd;
	struct scsi_pkt			*pkt;
	int				rval = FALSE;

	NDBG22(("glm_send_dev_reset_msg: tgt=%x", ap->a_target));

	bzero(cmd, sizeof (*cmd));
	pkt = kmem_zalloc(scsi_pkt_size(), KM_SLEEP);

	pkt->pkt_address	= *ap;
	pkt->pkt_cdbp		= (opaque_t)&cmd->cmd_cdb[0];
	pkt->pkt_scbp		= (opaque_t)&cmd->cmd_scb;
	pkt->pkt_ha_private	= (opaque_t)cmd;
	pkt->pkt_flags		= (FLAG_NOINTR | FLAG_HEAD);
	pkt->pkt_time		= 1;
	cmd->cmd_pkt		= pkt;
	cmd->cmd_scblen		= 1;
	cmd->cmd_flags		= CFLAG_CMDPROXY;
	cmd->cmd_type		= NRQ_DEV_RESET;
	cmd->cmd_cdb[GLM_PROXY_TYPE] = GLM_PROXY_SNDMSG;
	cmd->cmd_cdb[GLM_PROXY_RESULT] = FALSE;

	/*
	 * Send proxy cmd.
	 */
	if ((glm_accept_pkt(glm, cmd) == TRAN_ACCEPT) &&
	    (pkt->pkt_reason == CMD_CMPLT) &&
	    (cmd->cmd_cdb[GLM_PROXY_RESULT] == TRUE)) {
		rval = TRUE;
	}

	kmem_free(pkt, scsi_pkt_size());

	NDBG22(("glm_send_dev_reset_msg: rval=%x", rval));
#ifdef GLM_TEST
	if (rval && glm_test_stop) {
		debug_enter("glm_send_dev_reset_msg");
	}
#endif
	return (rval);
}

static void
glm_set_throttles(struct glm *glm, int slot, int n, int what)
{
	int i;
	struct glm_unit *unit;

	NDBG25(("glm_set_throttles: slot=%x, n=%x, what=%x",
	    slot, n, what));

	/*
	 * if the bus is draining/quiesced, no changes to the throttles
	 * are allowed. Not allowing change of throttles during draining
	 * limits error recovery but will reduce draining time
	 *
	 * all throttles should have been set to HOLD_THROTTLE
	 */
	if (glm->g_softstate & (GLM_SS_QUIESCED | GLM_SS_DRAINING)) {
		return;
	}

	ASSERT((n == 1) || (n == N_GLM_UNITS) || (n == NLUNS_GLM));
	ASSERT((slot + n) <= N_GLM_UNITS);
	if (n == NLUNS_GLM) {
		slot &= ~(NLUNS_GLM - 1);
	}

	for (i = slot; i < (slot + n); i++) {
		if ((unit = glm->g_units[i]) == NULL) {
			continue;
		}

		if (what == HOLD_THROTTLE) {
			unit->nt_throttle = HOLD_THROTTLE;
		} else if ((glm->g_reset_delay[i/NLUNS_GLM]) == 0) {
			if (what == MAX_THROTTLE) {
				int tshift = 1 << (i/NLUNS_GLM);
				unit->nt_throttle = (short)
				    ((glm->g_notag & tshift)? 1 : what);
			} else {
				unit->nt_throttle = what;
			}
		}
	}
}

static void
glm_set_all_lun_throttles(struct glm *glm, int target, int what)
{
	/*
	 * passed in is the target, slot will be lun 0.
	 */
	int slot = (target * NLUNS_GLM);

	/*
	 * glm_set_throttle will adjust slot starting at LUN 0
	 */
	glm_set_throttles(glm, slot, NLUNS_GLM, what);
}

static void
glm_full_throttle(struct glm *glm, int target, int lun)
{
	int slot = ((target * NLUNS_GLM) | lun);
	glm_set_throttles(glm, slot, 1, MAX_THROTTLE);
}

static void
glm_flush_lun(glm_t *glm, glm_unit_t *unit, uchar_t reason,
    uint_t stat)
{
	NDBG25(("glm_flush_lun: unit=0x%p reason=%x stat=%x",
	    (void *) unit, reason, stat));

	glm_mark_packets(glm, unit, reason, stat);
	glm_flush_waitQ(glm, unit);
	glm_flush_tagQ(glm, unit);
	if (unit->nt_state & NPT_STATE_QUEUED) {
		glm_hbaq_delete(glm, unit);
	}

	/*
	 * reset timeouts
	 */
	if (unit->nt_active) {
		unit->nt_active->nt_timebase = 0;
		unit->nt_active->nt_timeout = 0;
		unit->nt_active->nt_dups = 0;
	}

	ASSERT((unit->nt_state & NPT_STATE_QUEUED) == 0);

	unit->nt_state &= ~NPT_STATE_ACTIVE;
	unit->nt_linkp = NULL;
	unit->nt_ncmdp = NULL;
}

/*
 * Flush all the commands for all the LUNs on the specified target device.
 */
static void
glm_flush_target(glm_t *glm, ushort_t target, uchar_t reason,
    uint_t stat)
{
	struct glm_unit *unit;
	ushort_t lun;

	NDBG25(("glm_flush_target: target=%x", target));

	/*
	 * if we are not in panic set up a reset delay for this target
	 */
	if (!ddi_in_panic()) {
		glm_set_all_lun_throttles(glm, target, HOLD_THROTTLE);
		glm->g_reset_delay[target] = glm->g_scsi_reset_delay;
		glm_start_watch_reset_delay();
	} else {
		drv_usecwait(glm->g_scsi_reset_delay * 1000);
	}

	/*
	 * Completed Bus Device Reset, clean up
	 */
	for (lun = 0; lun < NLUNS_GLM; lun++) {
		unit = NTL2UNITP(glm, target, lun);
		if (unit == NULL) {
			continue;
		}

		/*
		 * flush the cmds from this target.
		 */
		glm_flush_lun(glm, unit, reason, stat);
		ASSERT(unit->nt_state == 0);
	}

#ifdef GLM_DEBUG
	if (glm_hbaq_check(glm, unit) == TRUE) {
		glm_log(glm, CE_WARN, "target (%d.%d) still queued.\n",
		    unit->nt_target, unit->nt_lun);
	}
#endif

	glm_force_renegotiation(glm, target);
}

/*
 * Called after a SCSI Bus Reset to find and flush all
 * the outstanding scsi requests.
 */
static void
glm_flush_hba(struct glm *glm)
{
	struct glm_unit *unit;
	int slot;

	NDBG25(("glm_flush_hba"));

	/*
	 * renegotiate wide and sync for all targets.
	 */
	glm->g_wide_known = glm->g_wide_enabled = 0;

	/*
	 * Initiate PPR again
	 */
	glm->g_ppr_known = 0;
	glm_syncio_reset(glm, NULL);

	/*
	 * setup the reset delay
	 */
	if (!ddi_in_panic() && !glm->g_suspended) {
		glm_setup_bus_reset_delay(glm);
	} else {
		drv_usecwait(glm->g_scsi_reset_delay * 1000);
	}


	/*
	 * for each slot, flush all cmds- waiting and/or outstanding.
	 */
	for (slot = 0; slot < N_GLM_UNITS; slot++) {
		if ((unit = glm->g_units[slot]) == NULL) {
			continue;
		}
		glm_flush_lun(glm, unit, CMD_RESET, STAT_BUS_RESET);
	}

	/*
	 * The current unit, and link list of unit for the hba
	 * to run have been flushed.
	 */
	glm->g_current = NULL;
	ASSERT((glm->g_forwp == NULL) && (glm->g_backp == NULL));
	glm->g_forwp = glm->g_backp = NULL;

	/*
	 * Now mark the hba as idle.
	 */
	glm->g_state = NSTATE_IDLE;

	/*
	 * perform the reset notification callbacks that are registered.
	 */
	(void) scsi_hba_reset_notify_callback(&glm->g_mutex,
	    &glm->g_reset_notify_listf);

}

/*
 * mark all packets with new reason and update statistics
 */
static void
glm_mark_packets(struct glm *glm, struct glm_unit *unit,
    uchar_t reason, uint_t stat)
{
	struct glm_scsi_cmd *sp = unit->nt_waitq;

	NDBG25(("glm_mark_packets: unit=0x%p reason=%x stat=%x",
	    (void *)unit, reason, stat));

	/*
	 * First set pkt_reason, pkt_statistics for the waitq.
	 */
	while (sp != 0) {
		glm_set_pkt_reason(glm, sp, reason, STAT_ABORTED);
		sp = sp->cmd_linkp;
	}
	if (unit->nt_tcmds) {
		int n = 0;
		ushort_t tag;

		for (tag = 0; tag < unit->nt_active->nt_n_slots; tag++) {
			if ((sp = unit->nt_active->nt_slot[tag]) != 0) {
				glm_set_pkt_reason(glm, sp, reason, stat);
				n++;
			}
		}
		ASSERT(unit->nt_tcmds == n);
	}

	/*
	 * There may be a proxy cmd.
	 */
	if ((sp = unit->nt_ncmdp) != NULL) {
		if (sp->cmd_flags & CFLAG_CMDPROXY) {
			glm_set_pkt_reason(glm, sp, reason, stat);
		}
	}
}

/*
 * put the active cmd and waitq on the doneq.
 */
static void
glm_flush_waitQ(struct glm *glm, struct glm_unit *unit)
{
	struct glm_scsi_cmd *sp;

	NDBG25(("glm_flush_waitQ: unit=0x%p", (void *)unit));

	/*
	 * Flush the waitq.
	 */
	while ((sp = glm_waitq_rm(unit)) != NULL) {
		glm_doneq_add(glm, sp);
	}

	/*
	 * Flush the proxy cmd.
	 */
	if ((sp = unit->nt_ncmdp) != NULL) {
		if (sp->cmd_flags & CFLAG_CMDPROXY) {
			glm_doneq_add(glm, sp);
		}
	}

#ifdef GLM_DEBUG
	if (unit->nt_state & NPT_STATE_QUEUED) {
		if (glm_hbaq_check(glm, unit) == FALSE) {
			glm_log(glm, CE_WARN,
			    "glm_flush_waitQ: someone is not correct.\n");
		}

	}
#endif
}

/*
 * cleanup the tag queue
 * preserve some order by starting with the oldest tag
 */
static void
glm_flush_tagQ(struct glm *glm, struct glm_unit *unit)
{
	ushort_t tag, starttag;
	struct glm_scsi_cmd *sp;
	struct nt_slots *tagque = unit->nt_active;

	if (tagque == NULL) {
		return;
	}

	tag = starttag = unit->nt_active->nt_tags;

	do {
		if ((sp = tagque->nt_slot[tag]) != 0) {
			glm_remove_cmd(glm, unit, sp);
			glm_doneq_add(glm, sp);
		}
		tag = ((ushort_t)(tag + 1)) %
		    (ushort_t)unit->nt_active->nt_n_slots;
	} while (tag != starttag);

	ASSERT(unit->nt_tcmds == 0);
}

/*
 * set pkt_reason and OR in pkt_statistics flag
 */
/*ARGSUSED*/
static void
glm_set_pkt_reason(struct glm *glm, struct glm_scsi_cmd *cmd, uchar_t reason,
    uint_t stat)
{
	NDBG25(("glm_set_pkt_reason: cmd=0x%p reason=%x stat=%x",
	    (void *)cmd, reason, stat));

	if (cmd) {
		if (cmd->cmd_pkt->pkt_reason == CMD_CMPLT) {
			cmd->cmd_pkt->pkt_reason = reason;
		}
		cmd->cmd_pkt->pkt_statistics |= stat;
	}
}

static void
glm_start_watch_reset_delay()
{
	NDBG22(("glm_start_watch_reset_delay"));

	mutex_enter(&glm_global_mutex);
	if (glm_reset_watch == 0 && glm_timeouts_enabled) {
		glm_reset_watch = timeout(glm_watch_reset_delay, NULL,
		    drv_usectohz((clock_t)
		    GLM_WATCH_RESET_DELAY_TICK * 1000));
		ASSERT(glm_reset_watch != 0);
	}
	mutex_exit(&glm_global_mutex);
}

static void
glm_setup_bus_reset_delay(struct glm *glm)
{
	int i;

	NDBG22(("glm_setup_bus_reset_delay"));

	glm_set_throttles(glm, 0, N_GLM_UNITS, HOLD_THROTTLE);
	for (i = 0; i < NTARGETS_WIDE; i++) {
		glm->g_reset_delay[i] = glm->g_scsi_reset_delay;
	}
	glm_start_watch_reset_delay();
}

/*
 * glm_watch_reset_delay(_subr) is invoked by timeout() and checks every
 * glm instance for active reset delays
 */
/*ARGSUSED*/
static void
glm_watch_reset_delay(void *arg)
{
	struct glm *glm;
	int not_done = 0;

	NDBG22(("glm_watch_reset_delay"));

	mutex_enter(&glm_global_mutex);
	glm_reset_watch = 0;
	mutex_exit(&glm_global_mutex);
	rw_enter(&glm_global_rwlock, RW_READER);
	for (glm = glm_head; glm != (struct glm *)NULL; glm = glm->g_next) {
		if (glm->g_tran == 0) {
			continue;
		}
		mutex_enter(&glm->g_mutex);
		not_done += glm_watch_reset_delay_subr(glm);
		mutex_exit(&glm->g_mutex);
	}
	rw_exit(&glm_global_rwlock);

	if (not_done) {
		glm_start_watch_reset_delay();
	}
}

static int
glm_watch_reset_delay_subr(struct glm *glm)
{
	short slot, s;
	int done = 0;

	NDBG22(("glm_watch_reset_delay_subr: glm=0x%p", (void *)glm));

	for (slot = 0; slot < N_GLM_UNITS; slot += NLUNS_GLM) {
		s = slot/NLUNS_GLM;
		if (glm->g_reset_delay[s] != 0) {
			glm->g_reset_delay[s] -= GLM_WATCH_RESET_DELAY_TICK;
			if (glm->g_reset_delay[s] <= 0) {
				glm->g_reset_delay[s] = 0;
				glm_set_all_lun_throttles(glm, s,
				    MAX_THROTTLE);
				glm_queue_target_lun(glm, s);
			} else {
				done = -1;
			}
		}
	}
	return (done);
}

/*
 * queue all target/lun with work after reset delay.
 */
static void
glm_queue_target_lun(glm_t *glm, ushort_t target)
{
	ushort_t lun;
	glm_unit_t *unit;

	NDBG22(("glm_queue_target_lun: target=%x", target));

	for (lun = 0; lun < NLUNS_GLM; lun++) {
		unit = NTL2UNITP(glm, target, lun);
		if (unit == NULL) {
			continue;
		}
		/*
		 * If there are pkts to run, queue this target/lun.
		 */
		if (unit->nt_waitq != NULL) {
			glm_queue_target(glm, unit);
		}
	}
}

#ifdef GLM_TEST
static void
glm_test_reset(struct glm *glm, struct glm_unit *unit)
{
	struct scsi_address ap;
	ushort_t target = unit->nt_target;

	if (glm_rtest & (1<<target)) {
		ap.a_hba_tran = glm->g_tran;
		ap.a_target = target;
		ap.a_lun = (uchar_t)unit->nt_lun;

		NDBG22(("glm_test_reset: glm_rtest=%x glm_rtest_type=%x",
		    glm_rtest, glm_rtest_type));

		switch (glm_rtest_type) {
		case 0:
			if (glm_do_scsi_reset(&ap, RESET_TARGET)) {
				glm_rtest = 0;
			}
			break;
		case 1:
			if (glm_do_scsi_reset(&ap, RESET_ALL)) {
				glm_rtest = 0;
			}
			break;
		}
		if (glm_rtest == 0) {
			NDBG22(("glm_test_reset success"));
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
/*ARGSUSED*/
static int
glm_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct glm *glm = ADDR2GLM(ap);
	int rval;

	NDBG23(("glm_scsi_abort: target=%d.%d", ap->a_target, ap->a_lun));

	/* test device at entry point */
	if (ddi_get_devstate(glm->g_dip) == DDI_DEVSTATE_DOWN) {
		return (FALSE);
	}

	mutex_enter(&glm->g_mutex);
	rval = glm_do_scsi_abort(ap, pkt);
	mutex_exit(&glm->g_mutex);
	return (rval);
}

static int
glm_do_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct glm *glm = ADDR2GLM(ap);
	struct glm_unit *unit;
	struct glm_scsi_cmd *sp = NULL;
	int rval = FALSE;
	int slot = ((ap->a_target * NLUNS_GLM) | ap->a_lun);

	ASSERT(mutex_owned(&glm->g_mutex));

	/*
	 * Abort the command pktp on the target/lun in ap.  If pktp is
	 * NULL, abort all outstanding commands on that target/lun.
	 * If you can abort them, return 1, else return 0.
	 * Each packet that's aborted should be sent back to the target
	 * driver through the callback routine, with pkt_reason set to
	 * CMD_ABORTED.
	 *
	 * abort cmd pktp on HBA hardware; clean out of outstanding
	 * command lists, etc.
	 */
	if ((unit = ADDR2GLMUNITP(ap)) == NULL) {
		return (rval);
	}

	if (pkt != NULL) {
		/* abort the specified packet */
		sp = PKT2CMD(pkt);

		if (sp->cmd_queued) {
			NDBG23(("glm_do_scsi_abort: queued sp=0x%p aborted",
			    (void *)sp));
			glm_waitq_delete(unit, sp);
			glm_set_pkt_reason(glm, sp, CMD_ABORTED, STAT_ABORTED);
			glm_doneq_add(glm, sp);
			rval = TRUE;
			goto done;
		}

		/*
		 * the pkt may be the active packet. if not, the packet
		 * has already been returned or may be on the doneq
		 */
		if (sp != unit->nt_active->nt_slot[sp->cmd_tag[1]]) {
			rval = TRUE;
			goto done;
		}
	}

	/*
	 * Abort one active pkt or all the packets for a particular
	 * LUN, even if no packets are queued/outstanding
	 * If it's done then it's probably already on
	 * the done queue. If it's active we can't abort.
	 */
	glm_set_throttles(glm, slot, 1, HOLD_THROTTLE);
	rval = glm_send_abort_msg(ap, glm, unit);

#ifdef GLM_TEST
	if (rval && glm_test_stop) {
		debug_enter("glm_do_scsi_abort");
	}
#endif

done:
	glm_set_throttles(glm, slot, 1, MAX_THROTTLE);
	glm_doneq_empty(glm);
	return (rval);
}

/*ARGSUSED*/
static int
glm_send_abort_msg(struct scsi_address *ap, glm_t *glm, glm_unit_t *unit)
{
	auto struct glm_scsi_cmd	local_cmd;
	struct glm_scsi_cmd		*cmd = &local_cmd;
	struct scsi_pkt			*pkt;
	int				rval = FALSE;

	NDBG23(("glm_send_abort_msg: tgt=%x", ap->a_target));

	bzero(cmd, sizeof (*cmd));
	pkt = kmem_zalloc(scsi_pkt_size(), KM_SLEEP);

	pkt->pkt_address	= *ap;
	pkt->pkt_cdbp		= (opaque_t)&cmd->cmd_cdb[0];
	pkt->pkt_scbp		= (opaque_t)&cmd->cmd_scb;
	pkt->pkt_ha_private	= (opaque_t)cmd;
	pkt->pkt_flags		= (FLAG_NOINTR | FLAG_HEAD);
	pkt->pkt_time		= 1;
	cmd->cmd_pkt		= pkt;
	cmd->cmd_scblen		= 1;
	cmd->cmd_flags		= CFLAG_CMDPROXY;
	cmd->cmd_type		= NRQ_ABORT;
	cmd->cmd_cdb[GLM_PROXY_TYPE] = GLM_PROXY_SNDMSG;
	cmd->cmd_cdb[GLM_PROXY_RESULT] = FALSE;

	/*
	 * Send proxy cmd.
	 */
	if ((glm_accept_pkt(glm, cmd) == TRAN_ACCEPT) &&
	    (pkt->pkt_reason == CMD_CMPLT) &&
	    (cmd->cmd_cdb[GLM_PROXY_RESULT] == TRUE)) {
		rval = TRUE;
	}

	kmem_free(pkt, scsi_pkt_size());

	NDBG23(("glm_send_abort_msg: rval=%x", rval));
#ifdef GLM_TEST
	if (rval && glm_test_stop) {
		debug_enter("glm_send_abort_msg");
	}
#endif
	return (rval);
}

#ifdef GLM_TEST
static void
glm_test_abort(struct glm *glm, struct glm_unit *unit)
{
	struct scsi_address ap;
	ushort_t target = unit->nt_target;
	int rval = FALSE;
	struct glm_scsi_cmd *cmd;

	ASSERT(mutex_owned(&glm->g_mutex));

	if (glm_atest & (1<<target)) {
		ap.a_hba_tran = glm->g_tran;
		ap.a_target = target;
		ap.a_lun = (uchar_t)unit->nt_lun;

		NDBG23(("glm_test_abort: glm_atest=%x glm_atest_type=%x",
		    glm_atest, glm_atest_type));

		switch (glm_atest_type) {
		case 0:
			/* aborting specific queued cmd (head) */
			if (unit->nt_waitq) {
				cmd =  unit->nt_waitq;
				rval = glm_do_scsi_abort(&ap, cmd->cmd_pkt);
			}
			break;
		case 1:
			/* aborting specific queued cmd (2nd) */
			if (unit->nt_waitq && unit->nt_waitq->cmd_linkp) {
				cmd = unit->nt_waitq->cmd_linkp;
				rval = glm_do_scsi_abort(&ap, cmd->cmd_pkt);
			}
			break;
		case 2:
		{
			int tag;
			struct nt_slots *tag_slots = unit->nt_active;

			/* aborting specific disconnected cmd */
			if (((unit->nt_state & NPT_STATE_ACTIVE) == 0) &&
			    unit->nt_tcmds != 0) {
				/*
				 * find the oldest tag.
				 */
				for (tag = NTAGS-1; tag >= 0; tag--) {
					if ((cmd = tag_slots->nt_slot[tag])
					    != 0) {
						if (cmd->cmd_flags &
						    CFLAG_CMDDISC) {
							break;
						}
					}
				}
				if (cmd) {
					rval = glm_do_scsi_abort(&ap,
					    cmd->cmd_pkt);
				}
			}
			break;
		}
		case 3:
			/* aborting all queued requests */
			if (unit->nt_waitq || unit->nt_tcmds > 0) {
				rval = glm_do_scsi_abort(&ap, NULL);
			}
			break;
		case 4:
			/* aborting disconnected cmd */
			if ((unit->nt_state & NPT_STATE_ACTIVE) == 0) {
				rval = glm_do_scsi_abort(&ap, NULL);
			}
			break;
		}
		if (rval) {
			glm_atest = 0;
			NDBG23(("glm_test_abort success"));
		}
	}
}
#endif

/*
 * capability handling:
 * (*tran_getcap).  Get the capability named, and return its value.
 */
static int
glm_scsi_getcap(struct scsi_address *ap, char *cap, int tgtonly)
{
	struct glm *glm = ADDR2GLM(ap);
	struct glm_unit *unit;
	ushort_t tshift = (1<<ap->a_target);
	int ckey;
	int rval = FALSE;

	NDBG24(("glm_scsi_getcap: target=%x, cap=%s tgtonly=%x",
	    ap->a_target, cap, tgtonly));

	mutex_enter(&glm->g_mutex);

	if ((unit = ADDR2GLMUNITP(ap)) == NULL) {
		mutex_exit(&glm->g_mutex);
		return (rval);
	}

	if ((glm_capchk(cap, tgtonly, &ckey)) != TRUE) {
		mutex_exit(&glm->g_mutex);
		return (UNDEFINED);
	}

	switch (ckey) {
	case SCSI_CAP_DMA_MAX:
		rval = (int)glm_dma_attrs.dma_attr_maxxfer;
		break;
	case SCSI_CAP_DISCONNECT:
		if (tgtonly &&
		    (glm->g_target_scsi_options[ap->a_target] &
		    SCSI_OPTIONS_DR)) {
			rval = TRUE;
		}
		break;
	case SCSI_CAP_WIDE_XFER:
		if (tgtonly && ((glm->g_nowide & tshift) == 0)) {
			rval = TRUE;
		}
		break;
	case SCSI_CAP_SYNCHRONOUS:
		if (tgtonly &&
		    (glm->g_target_scsi_options[ap->a_target] &
		    SCSI_OPTIONS_SYNC)) {
			rval = TRUE;
		}
		break;
	case SCSI_CAP_ARQ:
		if (tgtonly && unit->nt_arq_pkt) {
			rval = TRUE;
		}
		break;
	case SCSI_CAP_INITIATOR_ID:
		rval = glm->g_glmid;
		break;
	case SCSI_CAP_MSG_OUT:
	case SCSI_CAP_PARITY:
	case SCSI_CAP_UNTAGGED_QING:
		rval = TRUE;
		break;
	case SCSI_CAP_TAGGED_QING:
		if (tgtonly && ((glm->g_notag & tshift) == 0)) {
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
		rval = glm->g_qfull_retries[ap->a_target];
		break;
	case SCSI_CAP_QFULL_RETRY_INTERVAL:
		rval = drv_hztousec(
		    glm->g_qfull_retry_interval[ap->a_target]) /
		    1000;
		break;
	case SCSI_CAP_CDB_LEN:
		rval = CDB_GROUP5;
		break;
	default:
		rval = UNDEFINED;
		break;
	}

	NDBG24(("glm_scsi_getcap: %s, rval=%x", cap, rval));

	mutex_exit(&glm->g_mutex);
	return (rval);
}

/*
 * (*tran_setcap).  Set the capability named to the value given.
 */
static int
glm_scsi_setcap(struct scsi_address *ap, char *cap, int value, int tgtonly)
{
	struct glm *glm = ADDR2GLM(ap);
	struct glm_unit *unit;
	int ckey;
	int target = ap->a_target;
	ushort_t tshift = (1<<target);
	int rval = FALSE;

	NDBG24(("glm_scsi_setcap: target=%x, cap=%s value=%x tgtonly=%x",
	    ap->a_target, cap, value, tgtonly));

	if (!tgtonly) {
		return (rval);
	}

	mutex_enter(&glm->g_mutex);

	if ((unit = ADDR2GLMUNITP(ap)) == NULL) {
		mutex_exit(&glm->g_mutex);
		return (rval);
	}

	if ((glm_capchk(cap, tgtonly, &ckey)) != TRUE) {
		mutex_exit(&glm->g_mutex);
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
		if (value)
			glm->g_target_scsi_options[ap->a_target] |=
			    SCSI_OPTIONS_DR;
		else
			glm->g_target_scsi_options[ap->a_target] &=
			    ~SCSI_OPTIONS_DR;
		break;
	case SCSI_CAP_WIDE_XFER:
		if (value) {
			if ((glm->g_target_scsi_options[target] &
			    SCSI_OPTIONS_WIDE) && (glm->g_options &
			    GLM_OPT_WIDE_BUS)) {
				glm->g_nowide &= ~tshift;
				glm->g_force_narrow &= ~tshift;
			} else {
				break;
			}
		} else {
			if (glm->g_options & GLM_OPT_WIDE_BUS) {
				glm->g_force_narrow |= tshift;
			}
		}
		glm_force_renegotiation(glm, target);
		rval = TRUE;
		break;
	case SCSI_CAP_SYNCHRONOUS:
		if (value) {
			if (glm->g_target_scsi_options[target] &
			    SCSI_OPTIONS_SYNC) {
				glm->g_force_async &= ~tshift;
			} else {
				break;
			}
		} else {
			glm->g_force_async |= tshift;
		}
		glm_force_renegotiation(glm, target);
		rval = TRUE;
		break;
	case SCSI_CAP_ARQ:
		if (value) {
			if (glm_create_arq_pkt(unit, ap)) {
				break;
			}
			/*
			 * set arq bit mask for this target/lun
			 */
			glm->g_arq_mask[ap->a_target] |= (1 << ap->a_lun);
		} else {
			if (glm_delete_arq_pkt(unit, ap)) {
				break;
			}
			/*
			 * clear arq bit mask for this target/lun
			 */
			glm->g_arq_mask[ap->a_target] &= ~(1 << ap->a_lun);
		}
		rval = TRUE;
		break;
	case SCSI_CAP_TAGGED_QING:
	{
		ushort_t old_notag = glm->g_notag;

		if (value) {
			if (glm->g_target_scsi_options[target] &
			    SCSI_OPTIONS_TAG) {
				NDBG9(("target %d: TQ enabled",
				    target));
				glm->g_notag &= ~tshift;
			} else {
				break;
			}
		} else {
			NDBG9(("target %d: TQ disabled",
			    target));
			glm->g_notag |= tshift;
		}

		if (value && glm_alloc_active_slots(glm, unit, KM_NOSLEEP)) {
			glm->g_notag = old_notag;
			break;
		}

		glm->g_props_update |= (1<<target);
		rval = TRUE;
		break;
	}
	case SCSI_CAP_QFULL_RETRIES:
		glm->g_qfull_retries[ap->a_target] = (uchar_t)value;
		rval = TRUE;
		break;
	case SCSI_CAP_QFULL_RETRY_INTERVAL:
		glm->g_qfull_retry_interval[ap->a_target] =
		    drv_usectohz(value * 1000);
		rval = TRUE;
		break;
	default:
		rval = UNDEFINED;
		break;
	}
	mutex_exit(&glm->g_mutex);
	return (rval);
}

/*
 * Utility routine for glm_ifsetcap/ifgetcap
 */
/*ARGSUSED*/
static int
glm_capchk(char *cap, int tgtonly, int *cidxp)
{
	NDBG24(("glm_capchk: cap=%s", cap));

	if (!cap)
		return (FALSE);

	*cidxp = scsi_hba_lookup_capstr(cap);
	return (TRUE);
}

/*
 * property management
 * glm_update_props:
 * create/update sync/wide/TQ/scsi-options properties for this target
 */
static void
glm_update_props(struct glm *glm, int tgt)
{
	char property[32];
	int wide_enabled, tq_enabled;
	uint_t xfer_rate = 0;
	struct glm_unit *unit = glm->g_units[TL2INDEX(tgt, 0)];

	if (unit == NULL) {
		return;
	}

	NDBG2(("glm_update_props: tgt=%x", tgt));

	wide_enabled = ((glm->g_wide_enabled & (1<<tgt))? 1 : 0);

	if ((unit->nt_dsap->nt_selectparm.nt_sxfer & 0x1f) != 0) {
		xfer_rate = ((1000 * 1000)/glm->g_minperiod[tgt]);
		xfer_rate *= ((wide_enabled)? 2 : 1);
	} else {
		/* default 5 MB/sec transfer rate for Narrow SCSI */
		xfer_rate = 5000;
		xfer_rate *= ((wide_enabled)? 2 : 1);
	}
	/* Update Ultra Properites only if it is DT */
	if (((unit->nt_dsap->nt_ppribuf[5] & 0xff) & OPT_DT) && \
	    (unit->nt_dsap->nt_selectparm.nt_scntl4 & NB_SCNTL4_U3EN))
			xfer_rate *= 2;

	(void) sprintf(property, "target%x-sync-speed", tgt);
	glm_update_this_prop(glm, property, xfer_rate);

	(void) sprintf(property, "target%x-wide", tgt);
	glm_update_this_prop(glm, property, wide_enabled);

	(void) sprintf(property, "target%x-TQ", tgt);
	tq_enabled = ((glm->g_notag & (1<<tgt)) ? 0 : 1);
	glm_update_this_prop(glm, property, tq_enabled);
}

static void
glm_update_this_prop(struct glm *glm, char *property, int value)
{
	dev_info_t *dip = glm->g_dip;

	NDBG2(("glm_update_this_prop: prop=%s, value=%x", property, value));
	ASSERT(mutex_owned(&glm->g_mutex));

	/* Cannot hold mutex as call to ddi_prop_update_int() may block */
	mutex_exit(&glm->g_mutex);
	if (ddi_prop_update_int(DDI_DEV_T_NONE, dip,
	    property, value) != DDI_PROP_SUCCESS) {
		NDBG2(("cannot modify/create %s property.", property));
	}
	mutex_enter(&glm->g_mutex);
}

static int
glm_alloc_active_slots(struct glm *glm, struct glm_unit *unit, int flag)
{
	int target = unit->nt_target;
	struct nt_slots *old_active = unit->nt_active;
	struct nt_slots *new_active;
	ushort_t size;
	int rval = -1;

	ASSERT(unit != NULL);

	if (unit->nt_tcmds) {
		NDBG9(("cannot change size of active slots array"));
		return (rval);
	}

	size = ((NOTAG(target)) ? GLM_NT_SLOT_SIZE : GLM_NT_SLOTS_SIZE_TQ);
	new_active = kmem_zalloc(size, flag);
	if (new_active == NULL) {
		NDBG1(("new active alloc failed\n"));
	} else {
		unit->nt_active = new_active;
		unit->nt_active->nt_n_slots = (NOTAG(target) ? 1 : NTAGS);
		unit->nt_active->nt_size = size;
		/*
		 * reserve tag 0 for non-tagged cmds to tagged targets
		 */
		if (TAGGED(target))
			unit->nt_active->nt_tags = 1;

		if (old_active)
			kmem_free(old_active, old_active->nt_size);

		glm_full_throttle(glm, unit->nt_target, unit->nt_lun);
		rval = 0;
	}
	return (rval);
}

/*
 * Error logging, printing, and debug print routines.
 */
static char glm_label[] = "glm";

/*PRINTFLIKE3*/
static void
glm_log(struct glm *glm, int level, char *fmt, ...)
{
	dev_info_t *dev;
	va_list ap;

	if (glm) {
		dev = glm->g_dip;
	} else {
		dev = 0;
	}

	mutex_enter(&glm_log_mutex);

	va_start(ap, fmt);
	(void) vsprintf(glm_log_buf, fmt, ap);
	va_end(ap);

	if (level == CE_CONT) {
		scsi_log(dev, glm_label, level, "%s\n", glm_log_buf);
	} else {
		scsi_log(dev, glm_label, level, "%s", glm_log_buf);
	}

	mutex_exit(&glm_log_mutex);
}

#ifdef GLM_DEBUG
/*PRINTFLIKE1*/
static void
glm_printf(char *fmt, ...)
{
	dev_info_t *dev = 0;
	va_list	ap;

	mutex_enter(&glm_log_mutex);

	va_start(ap, fmt);
	(void) vsprintf(glm_log_buf, fmt, ap);
	va_end(ap);

#ifdef PROM_PRINTF
	prom_printf("%s:\t%s\n", glm_label, glm_log_buf);
#else
	scsi_log(dev, glm_label, SCSI_DEBUG, "%s\n", glm_log_buf);
#endif
	mutex_exit(&glm_log_mutex);
}
#endif

/*ARGSUSED*/
static void
glm_dump_cmd(struct glm *glm, struct glm_scsi_cmd *cmd)
{
	int i;
	uchar_t *cp = (uchar_t *)cmd->cmd_pkt->pkt_cdbp;
	auto char buf[128];

	buf[0] = '\0';
	glm_log(glm, CE_NOTE, "?Cmd (0x%p) dump for Target %d Lun %d:\n",
	    (void *)cmd, Tgt(cmd), Lun(cmd));
	(void) sprintf(&buf[0], "\tcdb=[");
	for (i = 0; i < (int)cmd->cmd_cdblen; i++) {
		(void) sprintf(&buf[strlen(buf)], " 0x%x", *cp++);
	}
	(void) sprintf(&buf[strlen(buf)], " ]");
	glm_log(glm, CE_NOTE, "?%s\n", buf);
	glm_log(glm, CE_NOTE,
	    "?pkt_flags=0x%x pkt_statistics=0x%x pkt_state=0x%x\n",
	    cmd->cmd_pkt->pkt_flags, cmd->cmd_pkt->pkt_statistics,
	    cmd->cmd_pkt->pkt_state);
	glm_log(glm, CE_NOTE, "?pkt_scbp=0x%x cmd_flags=0x%x\n",
	    *(cmd->cmd_pkt->pkt_scbp), cmd->cmd_flags);
}

/*
 * timeout handling
 */
/*ARGSUSED*/
static void
glm_watch(void *arg)
{
	struct glm *glm;
	ushort_t props_update = 0;

	NDBG30(("glm_watch"));

	rw_enter(&glm_global_rwlock, RW_READER);
	for (glm = glm_head; glm != (struct glm *)NULL; glm = glm->g_next) {
		mutex_enter(&glm->g_mutex);

		/*
		 * For now, always call glm_watchsubr.
		 */
		glm_watchsubr(glm);

		if (glm->g_props_update) {
			int i;
			/*
			 * g_mutex is released and reentered in
			 * glm_update_this_prop() so we save the value of
			 * g_props_update and then set it to zero indicating
			 * that a property has been updated.  This avoids a
			 * race condition with any thread that runs in interrupt
			 * context that attempts to set g_props_update to a
			 * non-zero value.  If g_props_update is modified
			 * during glm_update_props() then at the next callout
			 * of glm_watch() we will update the props then.
			 */
			props_update = glm->g_props_update;
			glm->g_props_update = 0;
			for (i = 0; i < NTARGETS_WIDE; i++) {
				if (props_update & (1<<i)) {
					glm_update_props(glm, i);
				}
			}
		}
		mutex_exit(&glm->g_mutex);
	}
	rw_exit(&glm_global_rwlock);

	mutex_enter(&glm_global_mutex);
	if (glm_timeouts_enabled)
		glm_timeout_id = timeout(glm_watch, NULL, glm_tick);
	mutex_exit(&glm_global_mutex);
}

static void
glm_watchsubr(struct glm *glm)
{
	struct glm_unit **unitp, *unit;
	int cnt;
	struct nt_slots *tag_slots;

	NDBG30(("glm_watchsubr: glm=0x%p\n", (void *)glm));

	/*
	 * we check in reverse order because during torture testing
	 * we really don't want to torture the root disk at 0 too often
	 */
	unitp = &glm->g_units[(N_GLM_UNITS-1)];

	for (cnt = (N_GLM_UNITS-1); cnt >= 0; cnt--) {
		if ((unit = *unitp--) == NULL) {
			continue;
		}

		if ((unit->nt_throttle > HOLD_THROTTLE) &&
		    (unit->nt_active &&
		    (unit->nt_active->nt_slot[0] == NULL))) {
			glm_full_throttle(glm, unit->nt_target, unit->nt_lun);
			if (unit->nt_waitq != NULL) {
				glm_queue_target(glm, unit);
			}
		}

#ifdef GLM_DEBUG
		if (unit->nt_state & NPT_STATE_QUEUED) {
			if (glm_hbaq_check(glm, unit) != TRUE) {
				glm_log(glm, CE_NOTE,
				    "target %d, botched state (%x).\n",
				    unit->nt_target, unit->nt_state);
			}
		}
#endif

#ifdef GLM_TEST
		if (glm_enable_untagged) {
			glm_test_untagged++;
		}
#endif

		tag_slots = unit->nt_active;

		if ((unit->nt_tcmds > 0) && (tag_slots->nt_timebase)) {
			if (tag_slots->nt_timebase <=
			    glm_scsi_watchdog_tick) {
				tag_slots->nt_timebase +=
				    glm_scsi_watchdog_tick;
				continue;
			}

			tag_slots->nt_timeout -= glm_scsi_watchdog_tick;

			if (tag_slots->nt_timeout < 0) {
				glm_cmd_timeout(glm, unit);
				return;
			}
			if ((tag_slots->nt_timeout) <=
			    glm_scsi_watchdog_tick) {
				NDBG23(("pending timeout on (%d.%d)\n",
				    unit->nt_target, unit->nt_lun));
				glm_set_throttles(glm, 0, N_GLM_UNITS,
				    DRAIN_THROTTLE);
			}
		}

#ifdef GLM_TEST
		if (glm->g_instance == glm_test_instance) {
			glm_test_reset(glm, unit);
			glm_test_abort(glm, unit);
		}
#endif
	}
}

/*
 * timeout recovery
 */
static void
glm_cmd_timeout(struct glm *glm, struct glm_unit *unit)
{
	int i, n, tag, ncmds;
	struct glm_unit *unitp;
	struct glm_scsi_cmd *sp = NULL;
	struct glm_scsi_cmd *ssp;

	/*
	 * set throttle back; no more draining necessary
	 */
	for (i = 0; i < N_GLM_UNITS; i++) {
		if ((unitp = glm->g_units[i]) == NULL) {
			continue;
		}
		if (unitp->nt_throttle == DRAIN_THROTTLE) {
			glm_full_throttle(glm,
			    unitp->nt_target, unitp->nt_lun);
		}
	}

	if (NOTAG(unit->nt_target)) {
		sp = unit->nt_active->nt_slot[0];
	}

	/*
	 * If the scripts processor is active and there is no interrupt
	 * pending for next second then the current sp must be stuck;
	 * switch to current unit and sp.
	 */
	NDBG29(("glm_cmd_timeout: g_state=%x, unit=0x%p, current=0x%p",
	    glm->g_state, (void *)unit, (void *)glm->g_current));

	if (glm->g_state == NSTATE_ACTIVE) {
		for (i = 0; (i < 10000) && (INTPENDING(glm) == 0); i++) {
			drv_usecwait(100);
		}
		if (INTPENDING(glm) == 0) {
			ASSERT(glm->g_current != NULL);
			unit = glm->g_current;
			sp = unit->nt_ncmdp;
		}
		NDBG29(("glm_cmd_timeout: new unit=0x%p", (void *)unit));
	}

#ifdef GLM_DEBUG
	/*
	 * See if we have a connected cmd timeout.
	 * if the hba is currently working on a cmd (g_current != NULL)
	 * and the cmd in g_current->nt_ncmp equal to a cmd in the unit
	 * active g_slot, this cmd is connected and timed out.
	 *
	 * This is used mainly for reset torture testing (sd_io_time=1).
	 */
	if (TAGGED(unit->nt_target) && glm->g_current &&
	    (glm->g_current == unit)) {
		if (glm->g_current->nt_ncmdp != NULL) {
			ssp = glm->g_current->nt_ncmdp;
			if (unit->nt_active->nt_slot[ssp->cmd_tag[1]] != NULL) {
				sp = unit->nt_active->nt_slot[ssp->cmd_tag[1]];
				if (sp != ssp) {
					sp = NULL;
				}
			}
		}
	}
#endif

	/*
	 * update all outstanding pkts for this unit.
	 */
	n = unit->nt_active->nt_n_slots;
	for (ncmds = tag = 0; tag < n; tag++) {
		ssp = unit->nt_active->nt_slot[tag];
		if (ssp && ssp->cmd_pkt->pkt_time) {
			glm_set_pkt_reason(glm, ssp, CMD_TIMEOUT,
			    STAT_TIMEOUT | STAT_ABORTED);
			ssp->cmd_pkt->pkt_state |= (STATE_GOT_BUS |
			    STATE_GOT_TARGET | STATE_SENT_CMD);
			glm_dump_cmd(glm, ssp);
			ncmds++;
		}
	}

	/*
	 * no timed-out cmds here?
	 */
	if (ncmds == 0) {
		return;
	}

	if (sp) {
		/*
		 * dump all we know about this timeout
		 */
		if (sp->cmd_flags & CFLAG_CMDDISC) {
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "Disconnected command timeout for Target %d.%d",
			    unit->nt_target, unit->nt_lun);
			cmn_err(CE_WARN, "!ID[SUNWpd.glm.cmd_timeout.6016]");
		} else {
			ASSERT(unit == glm->g_current);
			glm_report_fault(glm->g_dip, glm,
			    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
			    "Connected command timeout for Target %d.%d",
			    unit->nt_target, unit->nt_lun);
			cmn_err(CE_WARN, "!ID[SUNWpd.glm.cmd_timeout.6017]");
			/*
			 * connected cmd timeout are usually due to noisy buses.
			 */
			glm_sync_wide_backoff(glm, unit);
		}
	} else {
		glm_report_fault(glm->g_dip, glm,
		    DDI_SERVICE_UNAFFECTED, DDI_DEVICE_FAULT,
		    "Disconnected tagged cmd(s) (%d) timeout for Target %d.%d",
		    unit->nt_tcmds, unit->nt_target, unit->nt_lun);
		cmn_err(CE_WARN, "!ID[SUNWpd.glm.cmd_timeout.6018]");
	}
	(void) glm_abort_cmd(glm, unit, sp);
}

static int
glm_abort_cmd(struct glm *glm, struct glm_unit *unit, struct glm_scsi_cmd *cmd)
{
	struct scsi_address ap;
	ap.a_hba_tran = glm->g_tran;
	ap.a_target = unit->nt_target;
	ap.a_lun = unit->nt_lun;

	NDBG29(("glm_abort_cmd: unit=0x%p cmd=0x%p",
	    (void *)unit, (void *)cmd));

	/*
	 * If the current target is not the target passed in,
	 * try to reset that target.
	 */
	if (glm->g_current == NULL ||
	    ((glm->g_current && (glm->g_current != unit)))) {
		/*
		 * give up on this command
		 */
		NDBG29(("glm_abort_cmd device reset"));
		if (glm_do_scsi_reset(&ap, RESET_TARGET)) {
			return (TRUE);
		}
	}

	/*
	 * if the target won't listen, then a retry is useless
	 * there is also the possibility that the cmd still completed while
	 * we were trying to reset and the target driver may have done a
	 * device reset which has blown away this cmd.
	 * well, we've tried, now pull the chain
	 */
	NDBG29(("glm_abort_cmd bus reset"));
	return (glm_do_scsi_reset(&ap, RESET_ALL));
}

/*
 * auto request sense support
 * create or destroy an auto request sense packet
 */
static int
glm_create_arq_pkt(struct glm_unit *unit, struct scsi_address *ap)
{
	struct glm_scsi_cmd *rqpktp;
	struct buf *bp;
	struct arq_private_data *arq_data;

	NDBG8(("glm_create_arq_pkt: target=%x", ap->a_target));

	if (unit->nt_arq_pkt != 0) {
		return (0);
	}

	/*
	 * it would be nicer if we could allow the target driver
	 * to specify the size but this is easier and OK for most
	 * drivers to use SENSE_LENGTH
	 * Allocate a request sense packet.
	 */
	bp = scsi_alloc_consistent_buf(ap, (struct buf *)NULL,
	    SENSE_LENGTH, B_READ, SLEEP_FUNC, NULL);
	rqpktp = PKT2CMD(scsi_init_pkt(ap,
	    NULL, bp, CDB_GROUP0, 1, PKT_PRIV_LEN,
	    PKT_CONSISTENT, SLEEP_FUNC, NULL));
	arq_data =
	    (struct arq_private_data *)(rqpktp->cmd_pkt->pkt_private);
	arq_data->arq_save_bp = bp;

	RQ_MAKECOM_G0((CMD2PKT(rqpktp)),
	    FLAG_SENSING | FLAG_HEAD | FLAG_NODISCON,
	    (char)SCMD_REQUEST_SENSE, 0, (char)SENSE_LENGTH);
	rqpktp->cmd_flags |= CFLAG_CMDARQ;
	rqpktp->cmd_pkt->pkt_ha_private = rqpktp;
	unit->nt_arq_pkt = rqpktp;

	/*
	 * we need a function ptr here so abort/reset can
	 * defer callbacks; glm_doneq_add() calls
	 * glm_complete_arq_pkt() directly without releasing the lock
	 * However, since we are not calling back directly thru
	 * pkt_comp, don't check this with warlock
	 */
#ifndef __lock_lint
	rqpktp->cmd_pkt->pkt_comp =
	    (void (*)(struct scsi_pkt *))glm_complete_arq_pkt;
#endif
	return (0);
}

/*ARGSUSED*/
static int
glm_delete_arq_pkt(struct glm_unit *unit, struct scsi_address *ap)
{
	struct glm_scsi_cmd *rqpktp;

	NDBG8(("glm_delete_arq_pkt: target=%x", ap->a_target));

	/*
	 * if there is still a pkt saved or no rqpkt
	 * then we cannot deallocate or there is nothing to do
	 */
	if ((rqpktp = unit->nt_arq_pkt) != NULL) {
		struct arq_private_data *arq_data =
		    (struct arq_private_data *)(rqpktp->cmd_pkt->pkt_private);
		struct buf *bp = arq_data->arq_save_bp;
		/*
		 * is arq pkt in use?
		 */
		if (arq_data->arq_save_sp) {
			return (-1);
		}

		scsi_destroy_pkt(CMD2PKT(rqpktp));
		scsi_free_consistent_buf(bp);
		unit->nt_arq_pkt = NULL;
	}
	return (0);
}

/*
 * complete an arq packet by copying over transport info and the actual
 * request sense data.
 */
static void
glm_complete_arq_pkt(struct scsi_pkt *pkt)
{
	struct glm *glm;
	struct glm_unit *unit;
	struct glm_scsi_cmd *sp;
	struct scsi_arq_status *arqstat;
	struct arq_private_data *arq_data;
	struct glm_scsi_cmd *ssp;
	struct buf *bp;

	glm = ADDR2GLM(&pkt->pkt_address);

	unit = PKT2GLMUNITP(pkt);
	sp = pkt->pkt_ha_private;
	arq_data = (struct arq_private_data *)sp->cmd_pkt->pkt_private;
	ssp = arq_data->arq_save_sp;
	bp = arq_data->arq_save_bp;

	NDBG8(("glm_complete_arq_pkt: target=%d pkt=0x%p, ssp=0x%p",
	    unit->nt_target, (void *)pkt, (void *)ssp));

	ASSERT(unit != NULL);
	ASSERT(sp == unit->nt_arq_pkt);
	ASSERT(arq_data->arq_save_sp != NULL);
	ASSERT(ssp->cmd_flags & CFLAG_ARQ_IN_PROGRESS);

	ssp->cmd_flags &= ~CFLAG_ARQ_IN_PROGRESS;

	if (sp->cmd_pkt->pkt_resid < sp->cmd_dmacount)
		sp->cmd_pkt->pkt_state |= STATE_XFERRED_DATA;

	arqstat = (struct scsi_arq_status *)(ssp->cmd_pkt->pkt_scbp);
	arqstat->sts_rqpkt_status = *((struct scsi_status *)
	    (sp->cmd_pkt->pkt_scbp));
	arqstat->sts_rqpkt_reason = sp->cmd_pkt->pkt_reason;
	arqstat->sts_rqpkt_state  = sp->cmd_pkt->pkt_state;
	arqstat->sts_rqpkt_statistics = sp->cmd_pkt->pkt_statistics;
	arqstat->sts_rqpkt_resid  = sp->cmd_pkt->pkt_resid;
	arqstat->sts_sensedata =
	    *((struct scsi_extended_sense *)bp->b_un.b_addr);
	ssp->cmd_pkt->pkt_state |= STATE_ARQ_DONE;
	arq_data->arq_save_sp = NULL;

	/*
	 * ASC=0x47 is parity error
	 */
	if (arqstat->sts_sensedata.es_key == KEY_ABORTED_COMMAND &&
	    arqstat->sts_sensedata.es_add_code == 0x47) {
		glm_sync_wide_backoff(glm, unit);
	}

#ifdef GLM_TEST
	if (glm_test_rec && glm_test_instance == glm->g_instance) {
		arqstat->sts_sensedata.es_key = KEY_HARDWARE_ERROR;
		arqstat->sts_sensedata.es_add_code = 0x84;
		glm_test_rec_mask &= ~(1<<Tgt(ssp));
	}
#endif

	/*
	 * complete the saved sp.
	 */
	ASSERT(ssp->cmd_flags & CFLAG_CMD_REMOVED);
	glm_doneq_add(glm, ssp);
}

/*
 * handle check condition and start an arq packet
 */
static int
glm_handle_sts_chk(struct glm *glm, struct glm_unit *unit,
    struct glm_scsi_cmd *sp)
{
	struct glm_scsi_cmd *arqsp = unit->nt_arq_pkt;
	struct arq_private_data *arq_data;
	struct buf *bp;

	NDBG8(("glm_handle_sts_chk: target=%d unit=0x%p sp=0x%p",
	    unit->nt_target, (void *)unit, (void *)sp));

	if ((arqsp == NULL) || (arqsp == sp) ||
	    (sp->cmd_scblen < sizeof (struct scsi_arq_status))) {
		NDBG8(("no arq packet or cannot arq on arq pkt"));
		return (0);
	}

	arq_data = (struct arq_private_data *)arqsp->cmd_pkt->pkt_private;
	bp = arq_data->arq_save_bp;

	ASSERT(sp != unit->nt_active->nt_slot[sp->cmd_tag[1]]);

	if (arq_data->arq_save_sp != NULL) {
		NDBG8(("auto request sense already in progress\n"));
		goto fail;
	}

	arq_data->arq_save_sp = sp;
	bzero(bp->b_un.b_addr, sizeof (struct scsi_extended_sense));

	/*
	 * copy the timeout from the original packet by lack of a better
	 * value
	 * we could take the residue of the timeout but that could cause
	 * premature timeouts perhaps
	 */
	arqsp->cmd_pkt->pkt_time = sp->cmd_pkt->pkt_time;
	arqsp->cmd_flags &= ~(CFLAG_TRANFLAG | CFLAG_CMD_REMOVED);
	arqsp->cmd_type = NRQ_NORMAL_CMD;
	ASSERT(arqsp->cmd_pkt->pkt_comp != NULL);

	sp->cmd_flags |= CFLAG_ARQ_IN_PROGRESS;

	/*
	 * Make sure arq goes out on bus.
	 */
	glm_full_throttle(glm, unit->nt_target, unit->nt_lun);
	(void) glm_accept_pkt(glm, arqsp);
	return (0);
fail:
	glm_set_pkt_reason(glm, sp, CMD_TRAN_ERR, 0);
	glm_log(glm, CE_WARN, "auto request sense failed");
	cmn_err(CE_WARN, "!ID[SUNWpd.glm.handle_sts_check.6019]");
	glm_dump_cmd(glm, sp);
	return (-1);
}

/*
 * selection timeout handling.  See PSARC/1998/220
 */
struct selection_table {
	uint_t time;
	uint_t value;
} select_time_table[] = {
	{ 1, NB_STIME0_1MS },
	{ 2, NB_STIME0_2MS },
	{ 4, NB_STIME0_4MS },
	{ 8, NB_STIME0_8MS },
	{ 16, NB_STIME0_16MS },
	{ 32, NB_STIME0_32MS },
	{ 64, NB_STIME0_64MS },
	{ 128, NB_STIME0_128MS },
	{ 256, NB_STIME0_256MS },
	0x0
};

/*
 * find min selection timeout, default is 256ms.
 *
 * The Symbios 8xx hardware can't set selection timeout to
 * just any number.  This routine finds the close minimum
 * selection timeout greater than or equal to the number
 * requested.  see table above.
 */
static uint_t
glm_min_selection_timeout(uint_t time)
{
	struct selection_table *tt;
	int rval = NB_STIME0_256MS;

	for (tt = select_time_table; tt->time != 0; tt++) {
		if (tt->time >= time) {
			rval = tt->value;
			break;
		}
	}
	return (rval);
}

/*
 * Device / Hotplug control
 */
static int
glm_scsi_quiesce(dev_info_t *dip)
{
	struct glm *glm;
	scsi_hba_tran_t *tran;

	tran = ddi_get_driver_private(dip);
	if ((tran == NULL) || ((glm = TRAN2GLM(tran)) == NULL)) {
		return (-1);
	}

	return (glm_quiesce_bus(glm));
}

static int
glm_scsi_unquiesce(dev_info_t *dip)
{
	struct glm *glm;
	scsi_hba_tran_t *tran;

	tran = ddi_get_driver_private(dip);
	if ((tran == NULL) || ((glm = TRAN2GLM(tran)) == NULL)) {
		return (-1);
	}

	return (glm_unquiesce_bus(glm));
}

static int
glm_quiesce_bus(struct glm *glm)
{
	NDBG28(("glm_quiesce_bus"));
	mutex_enter(&glm->g_mutex);

	/* Set the throttles to zero */
	glm_set_throttles(glm, 0, N_GLM_UNITS, HOLD_THROTTLE);
	/* If there are any outstanding commands in the queue */
	if (glm_check_outstanding(glm)) {
		glm->g_softstate |= GLM_SS_DRAINING;
		glm->g_quiesce_timeid = timeout(glm_ncmds_checkdrain,
		    glm, (GLM_QUIESCE_TIMEOUT * drv_usectohz(1000000)));
		if (cv_wait_sig(&glm->g_cv, &glm->g_mutex) == 0) {
			/*
			 * Quiesce has been interrupted
			 */
			glm->g_softstate &= ~GLM_SS_DRAINING;
			glm_set_throttles(glm, 0, N_GLM_UNITS, MAX_THROTTLE);
			glm_start_next(glm);
			if (glm->g_quiesce_timeid != 0) {
				timeout_id_t tid = glm->g_quiesce_timeid;
				glm->g_quiesce_timeid = 0;
				mutex_exit(&glm->g_mutex);
				(void) untimeout(tid);
				return (-1);
			}
			mutex_exit(&glm->g_mutex);
			return (-1);
		} else {
			/* Bus has been quiesced */
			ASSERT(glm->g_quiesce_timeid == 0);
			glm->g_softstate &= ~GLM_SS_DRAINING;
			glm->g_softstate |= GLM_SS_QUIESCED;
			mutex_exit(&glm->g_mutex);
			return (0);
		}
	}
	/* Bus was not busy - QUIESCED */
	mutex_exit(&glm->g_mutex);

	return (0);
}

static int
glm_unquiesce_bus(struct glm *glm)
{
	int i;
	struct glm_unit *unit;

	NDBG28(("glm_unquiesce_bus"));
	mutex_enter(&glm->g_mutex);
	glm->g_softstate &= ~GLM_SS_QUIESCED;
	glm_set_throttles(glm, 0, N_GLM_UNITS, MAX_THROTTLE);
	for (i = 0; i < N_GLM_UNITS; i++) {
		if ((unit = glm->g_units[i]) == NULL) {
			continue;
		}
		if (unit->nt_waitq != NULL) {
			glm_queue_target(glm, unit);
		}
	}
	mutex_exit(&glm->g_mutex);
	return (0);
}

static void
glm_ncmds_checkdrain(void *arg)
{
	struct glm *glm = arg;

	mutex_enter(&glm->g_mutex);
	if (glm->g_softstate & GLM_SS_DRAINING) {
		glm->g_quiesce_timeid = 0;
		if (glm_check_outstanding(glm) == 0) {
			/* Command queue has been drained */
			cv_signal(&glm->g_cv);
		} else {
			/*
			 * The throttle may have been reset because
			 * of a SCSI bus reset
			 */
			glm_set_throttles(glm, 0, N_GLM_UNITS, HOLD_THROTTLE);
			glm->g_quiesce_timeid = timeout(glm_ncmds_checkdrain,
			    glm, (GLM_QUIESCE_TIMEOUT * drv_usectohz(1000000)));
		}
	}
	mutex_exit(&glm->g_mutex);
}

static int
glm_check_outstanding(struct glm *glm)
{
	uint_t i;
	int ncmds = 0;

	ASSERT(mutex_owned(&glm->g_mutex));

	/* for every unit structure in the glm structure */
	for (i = 0; i < N_GLM_UNITS; i++) {
		if (glm->g_units[i] != NULL) {
			ncmds += glm->g_units[i]->nt_tcmds;
		}
	}

	return (ncmds);
}

static void
glm_make_ppr(struct glm *glm, struct glm_unit *unit)
{
	struct glm_dsa	*dsap = NULL;
	uchar_t		target = unit->nt_target;
	uchar_t		period = glm->g_hba_period;
	uchar_t		flags = 0;
	uchar_t		offset = 0;
	uchar_t		width = 0;
	uint_t		msgcount = 0;


	NDBG31(("glm_make_ppr: unit=0x%p width=%x", (void *)unit, width));

	dsap = unit->nt_dsap;
	msgcount = dsap->nt_sendmsg.count;

	if (((glm->g_target_scsi_options[unit->nt_target] &
	    SCSI_OPTIONS_WIDE) == 0) ||
	    (glm->g_nowide & (1<<unit->nt_target))) {
			glm->g_nowide |= (1<<unit->nt_target);
			width = 0;
	} else {
		width = GLM_XFER_WIDTH;
	}

	width = min(GLM_XFER_WIDTH, width);
	glm->g_wide_known |= (1<<unit->nt_target);
	glm_set_wide_scntl3(glm, unit, width);


	/*
	 * Use target's period (not the hba's period) if
	 * this target experienced a sync backoff.
	 */
	if (glm->g_backoff & (1<<unit->nt_target)) {
		period = glm->g_minperiod[unit->nt_target];
	}

	if (((glm->g_options & (GLM_OPT_LVD | GLM_OPT_DT)) ==
	    (GLM_OPT_LVD | GLM_OPT_DT)) &&
	    (width == 1)) {
		/*
		 * DT condition is satisfied
		 * But only for speeds >= FAST80
		 */
		if (glm->g_target_scsi_options[target] &
		    (SCSI_OPTIONS_FAST80 |
		    SCSI_OPTIONS_FAST160 |
		    SCSI_OPTIONS_FAST320)) {
			flags = OPT_DT;
		}
	}

	/*
	 * sanity check of period and offset
	 */
	if ((glm->g_target_scsi_options[target] & SCSI_OPTIONS_FAST80) &&
	    ((glm->g_options & (GLM_OPT_LVD | GLM_OPT_DT)) ==
	    (GLM_OPT_LVD | GLM_OPT_DT)) &&
	    (width == 1)) {
		if (period < (uchar_t)(DEFAULT_FAST40SYNC_PERIOD)) {
			period = (uchar_t)(DEFAULT_FAST40SYNC_PERIOD);
		}
	} else if ((glm->g_target_scsi_options[target] & SCSI_OPTIONS_FAST40) &&
	    (glm->g_options & GLM_OPT_LVD)) {
		if (period < (uchar_t)(DEFAULT_FAST40SYNC_PERIOD)) {
			period = (uchar_t)(DEFAULT_FAST40SYNC_PERIOD);
		}
	} else if (glm->g_target_scsi_options[target] & SCSI_OPTIONS_FAST20) {
		if (period < (uchar_t)(DEFAULT_FAST20SYNC_PERIOD)) {
			period = (uchar_t)(DEFAULT_FAST20SYNC_PERIOD);
		}
	} else if (glm->g_target_scsi_options[target] & SCSI_OPTIONS_FAST) {
		if (period < (uchar_t)(DEFAULT_FASTSYNC_PERIOD)) {
			period = (uchar_t)(DEFAULT_FASTSYNC_PERIOD);
		}
	} else {
		if (period < (uchar_t)(DEFAULT_SYNC_PERIOD)) {
			period = (uchar_t)(DEFAULT_SYNC_PERIOD);
		}
	}

	if (glm->g_target_scsi_options[target] & SCSI_OPTIONS_SYNC) {

		if ((flags & OPT_DT) == OPT_DT) {
			offset = glm->g_dt_offset;
		} else {
			offset = glm->g_sync_offset;
		}
	} else {
		offset = 0;
	}

	unit->nt_msgouttype = MSG_PARALLEL_PROTOCOL;
	dsap->nt_msgoutbuf[msgcount++] = (uchar_t)MSG_EXTENDED;
	dsap->nt_msgoutbuf[msgcount++] = (uchar_t)6;
	dsap->nt_msgoutbuf[msgcount++] = (uchar_t)MSG_PARALLEL_PROTOCOL;
	if ((flags & OPT_DT) == OPT_DT) {
		dsap->nt_msgoutbuf[msgcount++] = GLM_GET_DT_SYNC(period);
	} else {
		dsap->nt_msgoutbuf[msgcount++] = GLM_GET_SYNC(period);
	}
	dsap->nt_msgoutbuf[msgcount++] = 0;
	dsap->nt_msgoutbuf[msgcount++] = offset;
	dsap->nt_msgoutbuf[msgcount++] = width;
	dsap->nt_msgoutbuf[msgcount++] = flags;

	dsap->nt_sendmsg.count = msgcount;
	unit->nt_msgoutcount = msgcount;

	glm->g_ppr_known |= (1 << unit->nt_target);
	glm->g_ppr_sent |= (1 << unit->nt_target);
}

static int
glm_ppr_enable(glm_t *glm, glm_unit_t *unit)
{
	struct glm_dsa	*dsap = NULL;
	uchar_t		sxfer = 0;
	uchar_t		sscfX10 = 0;
	int		time_ns = 0;
	uchar_t		offset = 0;
	uchar_t		width = 0;
	uchar_t		flags = 0;
	ushort_t	t = 0;

	dsap = unit->nt_dsap;

	flags = (unit->nt_dsap->nt_ppribuf[5] & 0xff);
	width = (unit->nt_dsap->nt_ppribuf[4] & 0xff);
	/*
	 * the target returned a width we don't
	 * support.  reject this message.
	 */
	if (width > 1) {
		return (FALSE);
	}

	/*
	 * Set appropriate width in scntl3 for the target
	 */
	glm_set_wide_scntl3(glm, unit, width);

	/*
	 * units for transfer period factor are 4 nsec.
	 *
	 * These two values are the sync period and offset
	 * the target responded with.
	 */
	time_ns = dsap->nt_ppribuf[1];
	if ((flags & OPT_DT) == OPT_DT) {
		time_ns = GLM_GET_DT_PERIOD(time_ns);
	} else {
		time_ns = GLM_GET_PERIOD(time_ns);
	}
	offset = dsap->nt_ppribuf[3];

	/*
	 * If the target returns a zero offset, go async and
	 * don't initiate sdtr again.
	 */
	if (offset == 0) {
		glm_syncio_state(glm, unit, NSYNC_SDTR_DONE, 0, 0);
		glm->g_props_update |= (1<<unit->nt_target);
		return (TRUE);
	}

	/*
	 * check the offset returned by the target.
	 */
	if (((flags & OPT_DT) == 0) && (offset > glm->g_sync_offset)) {
		/*
		 * If target rejects DT mode msg but returns an
		 * offset larger than 31 then disable DT mode.
		 * There's probably a LVD to SE converter on the bus.
		 */
		if (glm->g_options & GLM_OPT_DT) {
			glm->g_options &= ~GLM_OPT_DT;
			offset = glm->g_sync_offset;
		} else {
			NDBG31(("glm_ppr_enable: invalid offset=%d", offset));
			return (FALSE);
		}
	} else if ((flags & OPT_DT) && (offset > glm->g_dt_offset)) {
		NDBG31(("glm_ppr_enable: invalid offset=%d", offset));
		return (FALSE);
	}

	/*
	 * Check the period returned by the target.  Target shouldn't
	 * try to decrease my period
	 */
	if ((time_ns < CONVERT_PERIOD(glm->g_speriod)) ||
	    !glm_max_sync_divisor(glm, time_ns, &sxfer, &sscfX10)) {

		NDBG31(("glm_ppr_enable: invalid period: %d", time_ns));
		return (FALSE);
	}

	if ((glm->g_devid == GLM_53c1010_33) ||
	    (glm->g_devid == GLM_53c1010_66)) {
		sxfer = offset;
	} else {

		/* encode the divisor and offset values */
		sxfer = (((sxfer - 4) << 5) + offset);

	}

	unit->nt_fastscsi = (time_ns < 200) ? TRUE : FALSE;

	/*
	 * If this target is UltraSCSI or faster, enable UltraSCSI
	 * timing in the chip for this target.
	 */
	t = unit->nt_target;
	if (time_ns <= DEFAULT_FAST20SYNC_PERIOD) {
		if ((glm->g_devid != GLM_53c1010_33) &&
		    (glm->g_devid != GLM_53c1010_66)) {
			unit->nt_dsap->nt_selectparm.nt_scntl3 |=
			    NB_SCNTL3_ULTRA;
			glm->g_dsa->g_reselectparm[t].g_scntl3 |=
			    NB_SCNTL3_ULTRA;
		} else {
			if (flags & OPT_DT) {
				unit->nt_dsap->nt_selectparm.nt_scntl4 |=
				    NB_SCNTL4_U3EN;
				glm->g_dsa->g_reselectparm[t].g_scntl4 |=
				    NB_SCNTL4_U3EN;
			} else {
				unit->nt_dsap->nt_selectparm.nt_scntl4 &=
				    ~NB_SCNTL4_U3EN;
				glm->g_dsa->g_reselectparm[t].g_scntl4 &=
				    ~NB_SCNTL4_U3EN;
			}
		}
	}

	glm->g_minperiod[unit->nt_target] = time_ns;

	glm_syncio_state(glm, unit, NSYNC_SDTR_DONE, sxfer, sscfX10);
	glm->g_props_update |= (1 << unit->nt_target);

	/*
	 * Copy Extended message negotiated by target as it is. We don't need
	 * to alter if it is acceptable.
	 */
	for (t = 0; t < 6; t++)
		unit->nt_dsap->nt_msgoutbuf[t+2] = unit->nt_dsap->nt_ppribuf[t];
	/* Extended Message */
	unit->nt_dsap->nt_msgoutbuf[0] = 1;
	/* Extended Msg Length */
	unit->nt_dsap->nt_msgoutbuf[1] = 6;
	dsap->nt_sendmsg.count = 8;
	unit->nt_msgoutcount = 8;
	return (TRUE);
}

/*PRINTFLIKE5*/
static void
glm_report_fault(dev_info_t *dip, glm_t *glm,
    ddi_fault_impact_t  impact,   ddi_fault_location_t location, char *fmt, ...)
{
	va_list ap;

	mutex_enter(&glm_log_mutex);
	va_start(ap, fmt);
	if (glm->g_datap && ddi_check_acc_handle(glm->g_datap) != DDI_SUCCESS) {
		impact = DDI_SERVICE_LOST;
		location = DDI_DATAPATH_FAULT;
		(void) sprintf(glm_log_buf, "%s", "data path failed - ");
		(void) vsprintf(&glm_log_buf[22], fmt, ap);
	} else {
		(void) vsprintf(glm_log_buf, fmt, ap);
	}
	va_end(ap);
	scsi_log(dip, glm_label, CE_WARN, "%s", glm_log_buf);
	ddi_dev_report_fault(dip, impact, location, glm_log_buf);
	mutex_exit(&glm_log_mutex);
}

static int
glm_check_handle_status(glm_t *glm)
{
	/* check everything OK before returning */
	if (ddi_check_acc_handle(glm->g_datap) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	} else if (ddi_check_dma_handle(glm->g_dsa_dma_h) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	} else if (glm->g_script_dma_handle &&
	    ddi_check_dma_handle(glm->g_script_dma_handle) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	} else {
		return (DDI_SUCCESS);
	}
}

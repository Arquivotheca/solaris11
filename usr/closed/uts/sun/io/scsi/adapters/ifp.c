/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * ifp - Intelligent FC Processor driver for ISP2100
 */

/* Print identifying string at boot time */
/* #define	IFPIDENT */

/* enable target mode code */
#define	STE_TARGET_MODE

#if defined(lint) && !defined(DEBUG)
#define	DEBUG	1
#endif

#ifdef DEBUG
#define	IFPDEBUG
static int ifpdebug = 0;
static int ifp_enable_brk_fatal = 0;
static int ifp_mbox_debug = 0;
#include <sys/debug.h>
#endif

static int ifp_config_debug = 0;

#ifdef __lock_lint
#define	NPROBE
#endif

/*
 * This flag turns on the private tracing facility.
 */
#ifdef DEBUG
#define	IFPTRACE
#define	NIFPTRACE 0x10000	/* the number of trace entries to save */
#endif

#include <sys/note.h>
#include <sys/modctl.h>
#include <sys/pci.h>
#include <sys/scsi/scsi.h>

#include <sys/fc4/fcp.h>
#include <sys/fc4/fcal_linkapp.h>

#include <sys/scsi/adapters/ifpio.h>
#include <sys/scsi/adapters/ifpmail.h>
#include <sys/scsi/adapters/ifpvar.h>
#include <sys/scsi/adapters/ifpreg.h>
#include <sys/scsi/adapters/ifpcmd.h>
#include <sys/scsi/impl/scsi_reset_notify.h>

/*
 * NON-ddi compliant include files
 */
#include <sys/utsname.h>
#include <sys/kmem.h>
#include <sys/vtrace.h>
#include <sys/kstat.h>
#include <sys/proc.h>
#include <sys/devctl.h>
#include <sys/callb.h>

#ifdef IFPTRACE
/*
 * For debugging, allocate space for the trace buffer
 * Don't need to call ATRACEINIT() because we are initializing
 * the trace structure here.
 */
kmutex_t ifp_atrace_mutex;	/* lock while tracing */
struct ifptrace ifptrace_buffer[NIFPTRACE+1];
struct ifptrace *ifptrace_ptr = ifptrace_buffer;
int ifptrace_count = 0;
_NOTE(SCHEME_PROTECTS_DATA("ATrace Stuff", ifptrace_ptr))
_NOTE(SCHEME_PROTECTS_DATA("ATrace Stuff", ifptrace_count))
#endif

/*
 * the values of the following variables are used to initialize
 * the cache line size and latency timer registers in the PCI
 * configuration space.  variables are used instead of constants
 * to allow tuning.
 */
static int ifp_conf_cache_linesz = 0x10;	/* 64 bytes */
static int ifp_conf_latency_timer = 0x40;	/* 64 PCI cycles */

/*
 * patch in case of hw problems
 */
static int ifp_burst_sizes_limit = 0xff;

/*
 * This sets the maximum number of LUNs that the driver will
 * scan for in the event that the report_luns SCSI command
 * is not implemented.
 */
static int ifp_max_luns_scan = IFP_MAX_LUNS_SCAN;

/*
 * This sets the execution throttle in the Qlogic firmware.
 */
static	uint16_t	ifp_execution_throttle = 256;

/*
 * IFP firmware download options:
 *	IFP_DOWNLOAD_FW_IF_NEWER	=>
 *		download if f/w level > current f/w level
 *	IFP_DOWNLOAD_FW_ALWAYS		=> always download
 */
int ifp_download_fw = IFP_DOWNLOAD_FW_ALWAYS;


/*
 * Tables for conversion from Loop ID to AL-PA and vice versa
 */
static uchar_t ifp_loopid_to_alpa[] = {
	0xef, 0xe8, 0xe4, 0xe2, 0xe1, 0xe0, 0xdc, 0xda, 0xd9, 0xd6,
	0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xce, 0xcd, 0xcc, 0xcb, 0xca,
	0xc9, 0xc7, 0xc6, 0xc5, 0xc3, 0xbc, 0xba, 0xb9, 0xb6, 0xb5,
	0xb4, 0xb3, 0xb2, 0xb1, 0xae, 0xad, 0xac, 0xab, 0xaa, 0xa9,
	0xa7, 0xa6, 0xa5, 0xa3, 0x9f, 0x9e, 0x9d, 0x9b, 0x98, 0x97,
	0x90, 0x8f, 0x88, 0x84, 0x82, 0x81, 0x80, 0x7c, 0x7a, 0x79,
	0x76, 0x75, 0x74, 0x73, 0x72, 0x71, 0x6e, 0x6d, 0x6c, 0x6b,
	0x6a, 0x69, 0x67, 0x66, 0x65, 0x63, 0x5c, 0x5a, 0x59, 0x56,
	0x55, 0x54, 0x53, 0x52, 0x51, 0x4e, 0x4d, 0x4c, 0x4b, 0x4a,
	0x49, 0x47, 0x46, 0x45, 0x43, 0x3c, 0x3a, 0x39, 0x36, 0x35,
	0x34, 0x33, 0x32, 0x31, 0x2e, 0x2d, 0x2c, 0x2b, 0x2a, 0x29,
	0x27, 0x26, 0x25, 0x23, 0x1f, 0x1e, 0x1d, 0x1b, 0x18, 0x17,
	0x10, 0x0f, 0x08, 0x04, 0x02, 0x01
};

static uchar_t ifp_alpa_to_loopid[] = {
	0x00, 0x7d, 0x7c, 0x00, 0x7b, 0x00, 0x00, 0x00, 0x7a, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x79, 0x78, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x77, 0x76, 0x00, 0x00, 0x75, 0x00, 0x74,
	0x73, 0x72, 0x00, 0x00, 0x00, 0x71, 0x00, 0x70, 0x6f, 0x6e,
	0x00, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68, 0x00, 0x00, 0x67,
	0x66, 0x65, 0x64, 0x63, 0x62, 0x00, 0x00, 0x61, 0x60, 0x00,
	0x5f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5e, 0x00, 0x5d,
	0x5c, 0x5b, 0x00, 0x5a, 0x59, 0x58, 0x57, 0x56, 0x55, 0x00,
	0x00, 0x54, 0x53, 0x52, 0x51, 0x50, 0x4f, 0x00, 0x00, 0x4e,
	0x4d, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4b,
	0x00, 0x4a, 0x49, 0x48, 0x00, 0x47, 0x46, 0x45, 0x44, 0x43,
	0x42, 0x00, 0x00, 0x41, 0x40, 0x3f, 0x3e, 0x3d, 0x3c, 0x00,
	0x00, 0x3b, 0x3a, 0x00, 0x39, 0x00, 0x00, 0x00, 0x38, 0x37,
	0x36, 0x00, 0x35, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x33, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x31, 0x30, 0x00, 0x00, 0x2f, 0x00, 0x2e, 0x2d, 0x2c,
	0x00, 0x00, 0x00, 0x2b, 0x00, 0x2a, 0x29, 0x28, 0x00, 0x27,
	0x26, 0x25, 0x24, 0x23, 0x22, 0x00, 0x00, 0x21, 0x20, 0x1f,
	0x1e, 0x1d, 0x1c, 0x00, 0x00, 0x1b, 0x1a, 0x00, 0x19, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x17, 0x16, 0x15,
	0x00, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0f, 0x00, 0x00, 0x0e,
	0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x00, 0x00, 0x08, 0x07, 0x00,
	0x06, 0x00, 0x00, 0x00, 0x05, 0x04, 0x03, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/*
 * Fast posting
 */
int ifp_fast_post = 1;

/*
 * Enable PLOGI/PRLI/PDISC/ADISC at every LIP
 */
int ifp_plogi_on_lip = 1;

ushort_t ifp_frame_size = IFP_FRAME_LENGTH_1024; /* this is the optimal len */
int ifp_soft_reset_wait_retries = 0;

#ifdef IFPDEBUG
int ifp_report_invalid_response = 0;
int ifp_dumpslots = 0;
int ifp_dumpstate = 0;
int ifp_timeout_debug = 0;
_NOTE(SCHEME_PROTECTS_DATA("Debugging Stuff", ifp_report_invalid_response))
_NOTE(SCHEME_PROTECTS_DATA("Debugging Stuff", ifp_dumpslots))
_NOTE(SCHEME_PROTECTS_DATA("Debugging Stuff", ifp_dumpstate))
_NOTE(SCHEME_PROTECTS_DATA("Debugging Stuff", ifp_timeout_debug))
#endif

/*
 * Firmware related externs
 */

extern ushort_t ifp_risc_code_addr;

extern uint16_t ifp_pci_risc_code_2100[];
extern uint16_t ifp_pci_risc_code_length_2100;
extern uint16_t ifp_pci_risc_code_2200[];
extern uint16_t ifp_pci_risc_code_length_2200;

/*
 * cb_ops functions prototypes
 */
static int ifp_open(dev_t *, int, int, cred_t *);
static int ifp_close(dev_t, int, int, cred_t *);
static int ifp_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);
static int ifp_kstat_update(kstat_t *, int);

/*
 * dev_ops functions prototypes
 */
static int ifp_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int ifp_attach(dev_info_t *, ddi_attach_cmd_t);
static int ifp_detach(dev_info_t *, ddi_detach_cmd_t);
static int ifp_dr_detach(dev_info_t *);

/*
 * Function prototypes
 *
 * SCSA functions exported by means of the transport table
 */
static int ifp_scsi_tgt_init(dev_info_t *, dev_info_t *, scsi_hba_tran_t *,
	struct scsi_device *);
static void ifp_scsi_tgt_free(dev_info_t *, dev_info_t *, scsi_hba_tran_t *,
	struct scsi_device *);
static int ifp_scsi_bus_config(dev_info_t *parent, uint_t flag,
    ddi_bus_config_op_t op, void *arg, dev_info_t **childp);
static int ifp_scsi_bus_unconfig(dev_info_t *parent, uint_t flag,
    ddi_bus_config_op_t op, void *arg);

static int ifp_scsi_start(struct scsi_address *, struct scsi_pkt *);
static int ifp_scsi_abort(struct scsi_address *, struct scsi_pkt *);
static int ifp_scsi_reset(struct scsi_address *, int);
static int ifp_scsi_getcap(struct scsi_address *, char *, int);
static int ifp_scsi_setcap(struct scsi_address *, char *, int, int);
static struct scsi_pkt *ifp_scsi_init_pkt(struct scsi_address *,
    struct scsi_pkt *, struct buf *, int, int, int, int, int (*)(), caddr_t);
static int ifp_i_issue_rls(ifp_t *, uchar_t, struct rls_payload *);
static int ifp_i_issue_flb(ifp_t *, char *, char *, ifp_lb_frame_cmd_t *);
static void ifp_scsi_destroy_pkt(struct scsi_address *, struct scsi_pkt *);
static void ifp_scsi_dmafree(struct scsi_address *, struct scsi_pkt *);
static void ifp_scsi_sync_pkt(struct scsi_address *, struct scsi_pkt *);
static int ifp_scsi_reset_notify(struct scsi_address *, int, void (*)(caddr_t),
	caddr_t);
static int ifp_scsi_get_bus_addr(struct scsi_device *, char *, int);
static int ifp_scsi_get_name(struct scsi_device *, char *, int);
static int ifp_bus_get_eventcookie(dev_info_t *, dev_info_t *, char *,
	ddi_eventcookie_t *);
static int ifp_bus_add_eventcall(dev_info_t *, dev_info_t *,
	ddi_eventcookie_t, void (*)(), void *, ddi_callback_id_t *cb_id);
static int ifp_bus_remove_eventcall(dev_info_t *devi, ddi_callback_id_t cb);
static int ifp_bus_post_event(dev_info_t *, dev_info_t *,
	ddi_eventcookie_t, void *);

/*
 * ifp interrupt handler
 */
static uint_t ifp_intr(caddr_t);

/*
 * internal functions
 */
static int ifp_i_commoncap(struct scsi_address *, char *, int, int, int);

static void ifp_i_watch(void *);
static int ifp_i_alive(struct ifp *);
static void ifp_i_loop_updates(struct ifp *, clock_t);
static void ifp_i_watch_ifp(struct ifp *);
static void ifp_i_fatal_error(struct ifp *, int);

static void ifp_i_check_waitQ(void *);
static void ifp_i_empty_waitQ(struct ifp *);
static int ifp_i_ok_to_issue_cmd(struct ifp *, struct ifp_target *);
static int ifp_i_start_cmd(struct ifp *, struct ifp_cmd *);
static ushort_t ifp_i_find_freeslot(struct ifp *);
static int ifp_i_polled_cmd_start(struct ifp *, struct ifp_cmd *);
static void ifp_i_call_pkt_comp(struct ifp_cmd *);
static void ifp_i_call_bad_pkt_comp(struct ifp_cmd *);
static void ifp_i_handle_arq(struct ifp *, struct ifp_cmd *);

static int ifp_i_force_lip(struct ifp *);
static void ifp_i_qflush(struct ifp *);
static int ifp_i_reset_interface(struct ifp *, int);
static int ifp_i_reset_init_chip(struct ifp *);
static int ifp_i_set_marker(struct ifp *, short, short, short);

/*PRINTFLIKE3*/
static void ifp_i_log(struct ifp *, int, char *, ...);
static void ifp_i_print_state(struct ifp *);

static void ifp_i_mbox_cmd_init(struct ifp *, struct ifp_mbox_cmd *,
	uchar_t, uchar_t, ushort_t, ushort_t, ushort_t,
	ushort_t, ushort_t, ushort_t, ushort_t, ushort_t);
static void ifp_i_mbox_write_regs(struct ifp *, struct ifp_mbox_cmd *);
static int ifp_i_mbox_cmd_start(struct ifp *, struct ifp_mbox_cmd *);
static int ifp_i_get_async_info(struct ifp *, ushort_t, struct ifp_cmd **);
static void ifp_i_mbox_cmd_complete(struct ifp *);
static uchar_t ifp_i_issue_inquiry(struct ifp *, struct ifp_target *, ushort_t,
	int *, struct scsi_inquiry *);

static int ifp_i_load_ram(struct ifp *, ushort_t, ushort_t *, uint_t);
static int ifp_i_download_fw(struct ifp *);

static int ifp_i_pkt_alloc_extern(struct ifp *, struct ifp_cmd *,
	int, int, int, int);
static void ifp_i_pkt_destroy_extern(struct ifp *, struct ifp_cmd *);

static void ifp_i_mark_loop_down(struct ifp *);
static int ifp_flasher(struct ifp *, uchar_t *, int);
static int ifp_erase_sector(struct ifp *ifp, uint32_t addr,
	uint32_t sector_mask);
static uint8_t ifp_manuf_id(struct ifp *ifp);
static uint8_t ifp_fl_dev_id(struct ifp *ifp);
static int ifp_program_address(struct ifp *ifp, uint32_t addr, uint8_t data);
static int ifp_poll_device(struct ifp *ifp, uint32_t addr, uint8_t data);
static void ifp_flash_write(struct ifp *ifp, uint32_t addr, uint8_t data);
static uint8_t ifp_flash_read(struct ifp *ifp, uint32_t addr);
static void ifp_write_word(struct ifp *ifp, uint16_t address, uint8_t data);
static uint8_t ifp_read_word(struct ifp *ifp, uint16_t address);

static void ifp_i_dump_mbox(struct ifp *, struct ifp_mbox_cmd *, char *);

#ifdef IFPDEBUG
static void ifp_i_print_fcal_position_map(struct ifp *);
static void ifp_i_dump_mem(char *, void *, size_t);
static void ifp_i_test(struct ifp *, struct ifp_cmd *);
#ifdef IFPDEBUGX
static void ifp_prt_lun(ifp_lun_t *);
static void ifp_prt_target(ifp_target_t *);
#endif
#endif
/*
 * kmem cache constructor and destructor
 */
static int ifp_kmem_cache_constructor(void *, void *, int);
static void ifp_kmem_cache_destructor(void *, void *);

static void ifp_i_gather_completions(struct ifp *, struct ifp_cmd_ptrs *,
	struct ifp_cmd_ptrs *);
static int ifp_i_handle_mbox_return(struct ifp *, struct ifp_cmd **);

static ifp_target_t *ifp_i_lookup_target(struct ifp *, uchar_t *);
static ifp_lun_t *ifp_i_lookup_lun(struct ifp *, uchar_t *, ushort_t);
static struct ifp_target *ifp_i_get_target_from_dip(struct ifp *, dev_info_t *);
static int ifp_i_scan_for_luns(struct ifp *, struct ifp_target *);
static ifp_target_t *ifp_i_process_alpa(struct ifp *, uchar_t, uint_t, int,
	enum alpa_status *);
static int ifp_i_update_props(dev_info_t *, struct ifp_target *, ushort_t,
	uint_t);
static int ifp_i_get_hba_alpa(struct ifp *, uchar_t *);
static void ifp_i_offline_target(struct ifp *, struct ifp_target *);
static int ifp_i_process_lips(struct ifp *);
static void ifp_i_handle_fatal_errors(struct ifp *, int, struct ifp_cmd_ptrs *,
	struct ifp_cmd_ptrs *);
static int ifp_i_handle_resets(struct ifp *);
static int ifp_i_scan_portdb(struct ifp *, uchar_t *, uchar_t, uint_t, int *);
static int ifp_i_handle_lip(struct ifp *, uint_t);
static int ifp_i_get_alpa_map(struct ifp *, uchar_t *);
static int ifp_i_get_port_db(struct ifp *, uchar_t, ifp_portdb_t *);
static void ifp_i_finish_init(struct ifp *, uint_t);
static void ifp_i_finish_target_init(struct ifp *, ifp_target_t *, uint_t);
static void ifp_i_create_devinfo(ifp_t *, ifp_target_t *, ifp_lun_t *, uint_t);

static void ifp_hp_daemon(void *);

static int ifp_i_ok_to_run_diags(struct ifp *);
static int ifp_i_run_mboxcmds(struct ifp *, ifp_diag_cmd_t *);
static void ifp_i_diag_regdump(struct ifp *, ifp_diag_regs_t *);

static void ifp_i_rls_le_to_be(void *, int);

/*
 * waitQ macros, refer to comments on waitQ below
 */
#define	IFP_CHECK_WAITQ(ifp)				\
	mutex_enter(IFP_WAITQ_MUTEX(ifp));		\
	ifp_i_empty_waitQ(ifp);

#define	IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp)		\
	IFP_CHECK_WAITQ(ifp)				\
	mutex_exit(IFP_REQ_MUTEX(ifp));			\
	mutex_exit(IFP_WAITQ_MUTEX(ifp));

/*
 * mutex for protecting variables shared by all instances of the driver
 */
static kmutex_t ifp_global_mutex;

/*
 * mutex for protecting ifp_log_buf which is shared among instances
 */
static kmutex_t ifp_log_mutex;

/*
 * readers/writer lock to protect the integrity of the softc structure
 * linked list while being traversed (or updated).
 */
static krwlock_t ifp_global_rwlock;

/*
 * Local static data
 */

static void *ifp_state = NULL;
static struct ifp *ifp_head;	/* for linking softc structures */
static struct ifp *ifp_tail;	/* for linking softc structures */
static struct ifp *ifp_last;	/* for debugging */
static int watchdog_tick;	/* ifp_i_watch() interval in HZ */
static int watchdog_ticks_per_ifp_tick;	/* watchdog ticks per ifp_tick */
static int ifp_scsi_watchdog_tick; /* ifp_i_watch_ifp() interval in sec */
static int ifp_tick;		/* ifp_i_watch_ifp() interval in HZ */
static timeout_id_t ifp_timeout_id;	/* ifp_i_watch() timeout id */
static int timeout_initted = 0;	/* ifp_i_watch() timeout status */
static char ifp_log_buf[256];	/* buffer used in ifp_i_log */

/* these are for bus event handling */
static char *ifp_insert_ename = FCAL_INSERT_EVENT;
static char *ifp_remove_ename = FCAL_REMOVE_EVENT;

static ndi_event_definition_t ifp_ndi_event_defs[] = {
	{ IFP_EVENT_TAG_INSERT, FCAL_INSERT_EVENT, EPL_INTERRUPT, 0 },
	{ IFP_EVENT_TAG_REMOVE, FCAL_REMOVE_EVENT, EPL_INTERRUPT, 0 },
};

#define	IFP_N_NDI_EVENTS \
	(sizeof (ifp_ndi_event_defs) / sizeof (ifp_ndi_event_defs[0]))

static ddi_eventcookie_t ifp_insert_eid;
static ddi_eventcookie_t ifp_remove_eid;

/*
 * default ifp dma attr structure describes device
 * and DMA engine specific attributes/constrains necessary
 * to allocate DMA resources for IFP device.
 *
 * XXX ISP2100 is capable of doing dual access cycle addressing to
 * get 64bit address on the PCI bus. Following the PCI 2.1 spec,
 * this is triggered when the upper 32bits are non-zero. However,
 * IOMMU physical addresses are all 32bits -- so we can't use
 * the 64bit address capability of ISP2100. IOMMU should be
 * bypassed to do this. Note that once we do that, we need to use
 * scatter gather and also that DMA counters are 32bits wide.
 * There is also an ISP2100 restriction of not crossing 32 bit
 * boudary.
 * XXX try with bypassed IOMMU at some point to check the impact.
 */
static ddi_dma_attr_t dma_ifpattr = {
	DMA_ATTR_V0,				/* dma_attr_version */
	0ULL,					/* dma_attr_addr_lo */
	0xffffffffULL,				/* dma_attr_addr_hi */
	0xffffffffULL,				/* dma_attr_count_max */
	1ULL,					/* dma_attr_align */
	DEFAULT_BURSTSIZE | BURST32 | BURST64 | BURST128,
						/* dma_attr_burstsizes */
	1,					/* dma_attr_minxfer */
	0xffffffffULL,				/* dma_attr_maxxfer */
	0xffffffffULL,				/* dma_attr_seg */
	1,					/* dma_attr_sgllen */
	512,					/* dma_attr_granular */
	0					/* dma_attr_flags */
};

/*
 * autoconfiguration routines.
 */
struct cb_ops ifp_cb_ops = {
	ifp_open,		/* open */
	ifp_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	ifp_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab  */
	D_MP | D_NEW		/* Driver compatibility flag */
};

static struct dev_ops ifp_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ifp_info,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	ifp_attach,		/* attach */
	ifp_detach,		/* detach */
	nodev,			/* reset */
	&ifp_cb_ops,		/* driver operations */
	NULL,			/* bus operations */
	nulldev,		/* power management */
	ddi_quiesce_not_supported,	/* devo_quiesce */
};

char _depends_on[] = "misc/scsi";

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module. This one is a driver */
#ifndef STE_TARGET_MODE
	"ISP2100 FCAL HBA Driver", /* Name of the module. */
#else
	"ISP2100 FCAL HBA (TM supp)", /* Name of the module. */
#endif /* STE_TARGET_MODE */
	&ifp_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

/*
 * warlock directives
 */
_NOTE(MUTEX_PROTECTS_DATA(ifp_global_mutex, timeout_initted))
_NOTE(MUTEX_PROTECTS_DATA(ifp_global_mutex, ifp::ifp_next))
_NOTE(MUTEX_PROTECTS_DATA(ifp_global_mutex, ifp_timeout_id))
_NOTE(MUTEX_PROTECTS_DATA(ifp_global_mutex, ifp_head ifp_tail))
_NOTE(MUTEX_PROTECTS_DATA(ifp_log_mutex, ifp_log_buf))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", ifp_last))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", ifp_response))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", ifp_request))
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", scsi_arq_status))
_NOTE(SCHEME_PROTECTS_DATA("unique per pkt", buf scsi_pkt ifp_cmd))
_NOTE(SCHEME_PROTECTS_DATA("protected by mutexes or no competing threads",
	ifp_biu_regs ifp_risc_regs))
_NOTE(SCHEME_PROTECTS_DATA("Dynamically allocd",
	ifp_diag_cmd::ifp_cmds_current_rev))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifp_tick))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ifpdebug scsi_address))
_NOTE(DATA_READABLE_WITHOUT_LOCK(scsi_watchdog_tick))
_NOTE(DATA_READABLE_WITHOUT_LOCK(scsi_reset_delay scsi_hba_tran))
_NOTE(READ_ONLY_DATA(ifp::ifp_next ifp_head))

#ifdef IFPDEBUG
extern void	prom_printf(const char *format, ...);
#endif

int
_init(void)
{
	int ret;

	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	ret = ddi_soft_state_init(&ifp_state, sizeof (struct ifp),
	    IFP_INITIAL_SOFT_SPACE);
	if (ret != 0) {
		return (ret);
	}
	if ((ret = scsi_hba_init(&modlinkage)) != 0) {
		ddi_soft_state_fini(&ifp_state);
		return (ret);
	}

#ifdef IFPTRACE
	mutex_init(&ifp_atrace_mutex, NULL, MUTEX_DRIVER, NULL);
#endif
	mutex_init(&ifp_global_mutex, NULL, MUTEX_DRIVER, NULL);
	rw_init(&ifp_global_rwlock, NULL, RW_DRIVER, NULL);
	mutex_init(&ifp_log_mutex, NULL, MUTEX_DRIVER, NULL);

	ret = mod_install(&modlinkage);
	if (ret != 0) {
		scsi_hba_fini(&modlinkage);
		rw_destroy(&ifp_global_rwlock);
		mutex_destroy(&ifp_global_mutex);
		mutex_destroy(&ifp_log_mutex);
		ddi_soft_state_fini(&ifp_state);
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

	rw_destroy(&ifp_global_rwlock);
	mutex_destroy(&ifp_global_mutex);
	mutex_destroy(&ifp_log_mutex);

	ddi_soft_state_fini(&ifp_state);

	return (ret);
}

int
_info(struct modinfo *modinfop)
{
	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Given the device number return the devinfo pointer or instance
 */
/*ARGSUSED*/
static int
ifp_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	int		instance = IFP_MINOR2INST(getminor((dev_t)arg));
	struct ifp	*ifp;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		ifp = ddi_get_soft_state(ifp_state, instance);
		if (ifp != NULL)
			*result = ifp->ifp_dip;
		else {
			*result = NULL;
			return (DDI_FAILURE);
		}
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)(uintptr_t)instance;
		break;
	default:
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

static int
ifp_kstat_update(kstat_t *ksp, int rw)
{
	struct ifp *ifp;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	ifp = ksp->ks_private;
#if defined(lint)
	/* keep lint happy for now */
	ifp = ifp;
#endif
	return (0);
}

/* ARGSUSED */
static int
ifp_open(dev_t *dev_p, int flags, int otyp, cred_t *cred_p)
{
	struct ifp *ifp;


	/* Allow only character opens */
	if (otyp != OTYP_CHR) {
		return (EINVAL);
	}

	ifp = ddi_get_soft_state(ifp_state, IFP_MINOR2INST(getminor(*dev_p)));
	if (ifp == (struct ifp *)NULL) {
		return (ENXIO);
		}

	IFP_MUTEX_ENTER(ifp);
	if ((flags & FEXCL) && (ifp->ifp_softstate & IFP_OPEN)) {
		IFP_MUTEX_EXIT(ifp);
		return (EBUSY);
	}

	ifp->ifp_softstate |= IFP_OPEN;
	IFP_MUTEX_EXIT(ifp);
	return (0);
}

/* ARGSUSED */
static int
ifp_close(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	struct ifp *ifp;


	if (otyp != OTYP_CHR)
		return (EINVAL);

	ifp = ddi_get_soft_state(ifp_state, IFP_MINOR2INST(getminor(dev)));
	if (ifp == (struct ifp *)NULL) {
		return (ENXIO);
	}
	IFP_MUTEX_ENTER(ifp);
	ifp->ifp_softstate &= ~IFP_OPEN;
	IFP_MUTEX_EXIT(ifp);
	return (0);
}

/* ARGSUSED */
static int
ifp_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *cred_p,
	int *rval_p)
{
	uchar_t al_pa;
	int cnt, i, j;
	uchar_t loopid;
	struct ifp *ifp;
	dev_info_t *cdip;
	scsi_hba_tran_t *tran;
	struct lilpmap *lilpp;	/* XXX temporary - see ifpio.h */
	struct ifp_al_map *map;
	struct scsi_address ap;
	struct rls_payload *rls;
	ifp_al_addr_pair_t *addr;
	struct ifp_diag_cmd *diag;
	struct ifp_target *target;
	struct devctl_iocdata *dcp;
	struct adisc_payload *adisc;
	char		*buffer, tmp[80];
	int		zero = 0;

	ifp = ddi_get_soft_state(ifp_state, IFP_MINOR2INST(getminor(dev)));


	ATRACE(ifp_ioctl, 0x11111111, ifp);
	if (ifp == (struct ifp *)NULL) {
		ATRACE(ifp_ioctl, 0x11111112, ifp);
		return (ENXIO);
	}
	ATRACE(ifp_ioctl, 0x11111113, cmd);

	/*
	 * We can use the generic implementation for these ioctls
	 */
	switch (cmd) {
	case DEVCTL_DEVICE_GETSTATE:
	case DEVCTL_DEVICE_ONLINE:
	case DEVCTL_DEVICE_OFFLINE:
	case DEVCTL_BUS_GETSTATE:
		return (ndi_devctl_ioctl(ifp->ifp_dip, cmd, arg, mode, 0));
	}

	switch (cmd) {
		case DEVCTL_DEVICE_RESET:
			if (ndi_dc_allochdl((void *)arg, &dcp) !=
			    NDI_SUCCESS)
				return (EFAULT);
			if (ndi_dc_getname(dcp) == NULL ||
			    ndi_dc_getaddr(dcp) == NULL) {
				ndi_dc_freehdl(dcp);
				return (EINVAL);
			}
			cdip = ndi_devi_find(ifp->ifp_dip,
			    ndi_dc_getname(dcp), ndi_dc_getaddr(dcp));
			if (cdip == NULL) {
				ndi_dc_freehdl(dcp);
				return (ENXIO);
			}
			break;
	}

	switch (cmd) {
		case FCIO_GETMAP:
			if (ifp->ifp_state != IFP_STATE_ONLINE) {
				return (ENOENT);
			}

			lilpp = kmem_zalloc(sizeof (struct lilpmap), KM_SLEEP);

			IFP_MUTEX_ENTER(ifp);
			lilpp->lilp_myalpa = ifp->ifp_my_alpa;
			lilpp->lilp_length = ifp->ifp_loop_map[0];
			bcopy((void *)&ifp->ifp_loop_map[1],
			    (void *)lilpp->lilp_list, lilpp->lilp_length);
			IFP_MUTEX_EXIT(ifp);
			i = ddi_copyout(lilpp, (void *)arg,
			    sizeof (struct lilpmap), mode);

			kmem_free(lilpp, sizeof (struct lilpmap));

			if (i != 0)
				return (EFAULT);
			break;

		case SFIOCGMAP:
		case IFPIOCGMAP:
			map = kmem_zalloc(sizeof (struct ifp_al_map), KM_SLEEP);
			IFP_MUTEX_ENTER(ifp);
			if (ifp->ifp_state != IFP_STATE_ONLINE) {
				IFP_MUTEX_EXIT(ifp);
				kmem_free(map, sizeof (struct ifp_al_map));
				return (ENOENT);
			}

			map->ifp_count = cnt = ifp->ifp_loop_map[0];
			bcopy((void *)&ifp->ifp_my_wwn,
			    map->ifp_hba_addr.ifp_node_wwn,
			    sizeof (la_wwn_t));
			bcopy((void *)&ifp->ifp_my_port_wwn,
			    map->ifp_hba_addr.ifp_port_wwn,
			    sizeof (la_wwn_t));
			map->ifp_hba_addr.ifp_al_pa = ifp->ifp_my_alpa;
			map->ifp_hba_addr.ifp_hard_address = 0;
			map->ifp_hba_addr.ifp_inq_dtype = DTYPE_UNKNOWN;

			for (j = 1, i = 0; j <= cnt; j++, i++) {

				al_pa = ifp->ifp_loop_map[j];

				addr = &map->ifp_addr_pair[i];
				addr->ifp_al_pa = al_pa;
				target = ifp->ifp_targets[ifp_alpa_to_loopid[
				    al_pa]];
				if (target != NULL) {
					mutex_enter(&target->ifpt_mutex);
					if (!(target->ifpt_state &
					    (IFPT_TARGET_OFFLINE |
					    IFPT_TARGET_BUSY))) {
						bcopy(target->ifpt_node_wwn,
						    addr->ifp_node_wwn,
						    sizeof (la_wwn_t));
						bcopy(target->ifpt_port_wwn,
						    addr->ifp_port_wwn,
						    sizeof (la_wwn_t));
						addr->ifp_hard_address
						    = target->ifpt_hard_address;
						addr->ifp_inq_dtype
						    = target->ifpt_lun.
						    ifpl_device_type;
						mutex_exit(&target->ifpt_mutex);
						continue;
					}
					mutex_exit(&target->ifpt_mutex);
				}
				if (al_pa == ifp->ifp_my_alpa) {
					bcopy((void *)&ifp->ifp_my_wwn,
					    addr->ifp_node_wwn,
					    sizeof (la_wwn_t));
					bcopy((void *)&ifp->ifp_my_port_wwn,
					    addr->ifp_port_wwn,
					    sizeof (la_wwn_t));
					addr->ifp_hard_address = 0;
					addr->ifp_inq_dtype = DTYPE_UNKNOWN;
				} else {
					bzero(addr->ifp_node_wwn,
					    sizeof (la_wwn_t));
					bzero(addr->ifp_port_wwn,
					    sizeof (la_wwn_t));
					addr->ifp_inq_dtype = DTYPE_UNKNOWN;
				}
			}
			IFP_MUTEX_EXIT(ifp);
			i = ddi_copyout(map, (void *)arg,
			    sizeof (struct ifp_al_map), mode);

			kmem_free(map, sizeof (struct ifp_al_map));

			if (i != 0)
				return (EFAULT);
			break;

		case IFPIO_ADISC_ELS:

			adisc = kmem_zalloc(sizeof (struct adisc_payload),
			    KM_SLEEP);

			if (ddi_copyin((void *)arg, adisc,
				sizeof (struct adisc_payload), mode) != 0) {
				kmem_free(adisc, sizeof (struct adisc_payload));
				return (EFAULT);
			}

			if (adisc->adisc_dest > 0xEF) {
				kmem_free(adisc, sizeof (struct adisc_payload));
				return (ENODEV);
			}

			i = 0;
			IFP_MUTEX_ENTER(ifp);
			target = ifp->ifp_targets[ifp_alpa_to_loopid[
			    adisc->adisc_dest]];
			if (target) {
				mutex_enter(&target->ifpt_mutex);
				if (!(target->ifpt_state &
				    (IFPT_TARGET_OFFLINE |
				    IFPT_TARGET_BUSY))) {
					bcopy(target->ifpt_node_wwn,
					    adisc->adisc_nodewwn,
					    sizeof (la_wwn_t));
					bcopy(target->ifpt_port_wwn,
					    adisc->adisc_portwwn,
					    sizeof (la_wwn_t));
					adisc->adisc_hardaddr =
					    target->ifpt_hard_address;
				} else {
					i = ENODEV;
				}
				mutex_exit(&target->ifpt_mutex);
			} else if (adisc->adisc_dest == ifp->ifp_my_alpa) {
				bcopy((void *)&ifp->ifp_my_wwn,
				    adisc->adisc_nodewwn,
				    sizeof (la_wwn_t));
				bcopy((void *)&ifp->ifp_my_port_wwn,
				    adisc->adisc_portwwn,
				    sizeof (la_wwn_t));
				adisc->adisc_hardaddr = 0;
			} else
				i = ENODEV;

			IFP_MUTEX_EXIT(ifp);

			if (i == 0) {
				if (ddi_copyout(adisc, (void *)arg,
				    sizeof (struct adisc_payload),
				    mode) != 0) {

					i = EFAULT;
				}
			}

			kmem_free(adisc, sizeof (struct adisc_payload));

			if (i)
				return (i);

			break;

		case FCIO_LINKSTATUS:
		case IFPIO_LINKSTATUS:

			rls = kmem_zalloc(sizeof (struct rls_payload),
			    KM_SLEEP);

			if (ddi_copyin((void *)arg, rls,
			    sizeof (struct rls_payload), mode) != 0) {
				kmem_free(rls, sizeof (struct rls_payload));
				return (EFAULT);
			}

			if (rls->rls_portno > 0xEF) {
				kmem_free(rls, sizeof (struct rls_payload));
				return (ENODEV);
			}
			i = 0;
			IFP_MUTEX_ENTER(ifp);
			loopid = ifp_alpa_to_loopid[rls->rls_portno];
			i = ifp_i_issue_rls(ifp, loopid, rls);
			IFP_MUTEX_EXIT(ifp);

			if (i == 0) {
				if (ddi_copyout(rls, (void *)arg,
				    sizeof (struct rls_payload), mode) != 0)
				i = EFAULT;
			}

			kmem_free(rls, sizeof (struct rls_payload));

			if (i)
				return (i);

			break;

		case IFPIO_DIAG_GET_FWREV:
			{
				ifp_diag_fw_rev_t fw_rev;

				fw_rev.ifpd_major = ifp->ifp_major_rev;
				fw_rev.ifpd_minor = ifp->ifp_minor_rev;
				if (ddi_copyout(&fw_rev, (void *)arg,
				    sizeof (fw_rev), mode) != 0)
					return (EFAULT);
			}
			break;

		case IFPIO_DIAG_NOP:
		case IFPIO_DIAG_MBOXCMD:
			IFP_MUTEX_ENTER(ifp);
			i = ifp_i_ok_to_run_diags(ifp);
			IFP_MUTEX_EXIT(ifp);
			if (i) {
				return (EBUSY);
			}

			if (cmd == IFPIO_DIAG_NOP)
				break;

			diag = kmem_zalloc(sizeof (struct ifp_diag_cmd),
			    KM_SLEEP);

			if (ddi_copyin((void *)arg, diag,
				sizeof (struct ifp_diag_cmd), mode)) {
				kmem_free(diag, sizeof (struct ifp_diag_cmd));
				return (EFAULT);
			}

			if (diag->ifp_cmds_rev != IFP_DIAG_CMD_REV) {
				diag->ifp_cmds_current_rev = IFP_DIAG_CMD_REV;
				i = EINVAL;
			}

			if (i || (i = ifp_i_run_mboxcmds(ifp, diag)) == 0) {
				if (ddi_copyout(diag, (void *)arg,
					sizeof (struct ifp_diag_cmd), mode)) {
					/*
					 * always report return value from
					 * ddi_copyout()
					 */
					i = -1;
				}
			}

			kmem_free(diag, sizeof (struct ifp_diag_cmd));

			if (i == -1)
				return (EFAULT);
			else if (i)
				return (i);
			break;

		case DEVCTL_DEVICE_RESET:
			target = ifp_i_get_target_from_dip(ifp, cdip);
			ndi_dc_freehdl(dcp);
			if (target == NULL)
				return (ENXIO);
			mutex_enter(&target->ifpt_mutex);
			if (!(target->ifpt_state & IFPT_TARGET_INIT_DONE)) {
				mutex_exit(&target->ifpt_mutex);
				return (ENXIO);
			}
			tran = target->ifpt_tran;
			mutex_exit(&target->ifpt_mutex);
			ap.a_hba_tran = tran;
			ap.a_target = ifp_alpa_to_loopid[target->ifpt_al_pa];
			if (ifp_scsi_reset(&ap, RESET_TARGET) == FALSE)
				return (EIO);
			break;

		case DEVCTL_BUS_QUIESCE:
			return (ENOTSUP);

		case DEVCTL_BUS_UNQUIESCE:
			return (ENOTSUP);

		case DEVCTL_BUS_RESET:
		case DEVCTL_BUS_RESETALL:
		case IFPIO_FORCE_LIP:
		case FCIO_FORCE_LIP:
			IFP_MUTEX_ENTER(ifp);
			if (ifp_i_force_lip(ifp)) {
				mutex_exit(IFP_REQ_MUTEX(ifp));
				ifp_i_fatal_error(ifp, 0);
				mutex_exit(IFP_RESP_MUTEX(ifp));
			} else
				IFP_MUTEX_EXIT(ifp);
			break;
		case IFPIO_BOARD_INFO:
			{
				ifp_board_info_t	info;

				info.ifpd_major = ifp->ifp_major_rev;
				info.ifpd_minor = ifp->ifp_minor_rev;
				info.ifpd_subminor = ifp->ifp_subminor_rev;
				info.chip_rev = ifp->ifp_chip_rev;
				info.ctrl_id = ifp->ifp_chip_id;
				if (ddi_copyout(&info, (void *)arg,
				    sizeof (ifp_board_info_t),
				    mode) != 0) {
					return (EFAULT);
				}
			}
			break;

		case IFPIO_FCODE_DOWNLOAD:
			{
				int	rval;
				ifp_download_t dl_buf;

				/* first check the header */
				if (ddi_copyin((caddr_t)arg, &dl_buf,
				    sizeof (ifp_download_t),
				    mode) == -1) {

					return (EFAULT);
				}

				/* insure that the chip id is correct */
				if (ifp->ifp_chip_id != dl_buf.dl_chip_id) {
					return (EINVAL);
				}

				IFP_MUTEX_ENTER(ifp);

				rval = ifp_flasher(ifp,
				    &((ifp_download_t *)arg)->
				    dl_fcode[FCODE_OFFSET],
				    dl_buf.dl_fcode_len);

				(void) ifp_i_reset_interface(ifp, 0);

				IFP_MUTEX_EXIT(ifp);
				return (rval);
			}

		case FCIO_FCODE_MCODE_VERSION:
			{
				STRUCT_DECL(ifp_fm_version, ver);

				STRUCT_INIT(ver, mode);

				if (ddi_copyin((caddr_t)arg, STRUCT_BUF(ver),
				    STRUCT_SIZE(ver), mode) == -1)
					return (EFAULT);
				cdip = ifp->ifp_dip;
				i = 0;
				IFP_MUTEX_ENTER(ifp);

				if (ddi_prop_op(DDI_DEV_T_ANY, cdip,
				    PROP_LEN_AND_VAL_ALLOC, DDI_PROP_DONTPASS |
				    DDI_PROP_CANSLEEP, "version",
				    (caddr_t)&buffer, &i) != DDI_SUCCESS) {
					/* no version property */
					STRUCT_FSET(ver, fcode_ver_len, 0);
					(void) ddi_copyout((caddr_t)&zero,
					    STRUCT_FGETP(ver, fcode_ver),
					    sizeof (int), mode);
				} else {

					if (i < STRUCT_FGET(ver, fcode_ver_len))
						STRUCT_FSET(ver, fcode_ver_len,
						    i);
					if (ddi_copyout((caddr_t)buffer,
					    STRUCT_FGETP(ver, fcode_ver),
					    STRUCT_FGET(ver, fcode_ver_len),
					    mode) == -1) {
						kmem_free((caddr_t)buffer, i);
						IFP_MUTEX_EXIT(ifp);
						return (EFAULT);
					}
					kmem_free((caddr_t)buffer, i);
				}

				/* get the microcode version */
				(void) sprintf(tmp,
				    "Firmware revision: %d.%d.%d",
				    ifp->ifp_major_rev,
				    ifp->ifp_minor_rev,
				    ifp->ifp_subminor_rev);


				if (strlen(tmp) <
				    STRUCT_FGET(ver, mcode_ver_len))
					STRUCT_FSET(ver, mcode_ver_len,
					    strlen(tmp));

				if (ddi_copyout((caddr_t)tmp,
				    STRUCT_FGETP(ver, mcode_ver),
				    STRUCT_FGET(ver, mcode_ver_len), mode) ==
				    -1) {
					IFP_MUTEX_EXIT(ifp);
					return (EFAULT);
				}

				/* There is no prom */
				STRUCT_FSET(ver, prom_ver_len, 0);
				(void) ddi_copyout((caddr_t)&zero,
				    STRUCT_FGETP(ver, prom_ver),
				    sizeof (int), mode);

				IFP_MUTEX_EXIT(ifp);
				if (ddi_copyout(STRUCT_BUF(ver), (caddr_t)arg,
				    STRUCT_SIZE(ver), mode) == -1)
					return (EFAULT);
			}
			break;

		case IFPIO_DIAG_SELFTEST:
			{
				ifp_diag_selftest_t	diag_st;
				struct ifp_mbox_cmd mbox_cmd;

				IFP_MUTEX_ENTER(ifp);
				ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 1, 4,
				    0x46, 0, 0, 0, 0, 0, 0, 0);
				if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
					IFP_MUTEX_EXIT(ifp);
					return (EFAULT);
				}
				IFP_MUTEX_EXIT(ifp);
				diag_st.status = mbox_cmd.mbox_in[0];
				diag_st.test_num = mbox_cmd.mbox_in[1];
				diag_st.fail_addr = mbox_cmd.mbox_in[2];
				diag_st.fail_data = mbox_cmd.mbox_in[3];


				if (ddi_copyout((caddr_t)&diag_st, (caddr_t)arg,
				    sizeof (struct ifp_diag_selftest),
				    mode) == -1) {
					return (EFAULT);
				}
			}
			break;

		case IFPIO_LOOPBACK_FRAME:
			{
				char		*flb_out, *flb_in;
				ifp_lb_frame_cmd_t	lb_cmd_ker;
				int		again_cnt = 0;
				int		rval = 0;
				uint_t		targ_state[IFP_MAX_TARGETS];
				STRUCT_DECL(ifp_lb_frame_cmd, lb_cmd_usr);

				STRUCT_INIT(lb_cmd_usr, mode);

				/* get the loopback command, user image */
				if (ddi_copyin((caddr_t)arg,
				    STRUCT_BUF(lb_cmd_usr),
				    STRUCT_SIZE(lb_cmd_usr),
				    mode) == -1) {
					return (EFAULT);
				}

				/* copy the incoming parameters from */
				/* the user (32/64) structure into my */
				/* system native structure */
				lb_cmd_ker.options =
				    STRUCT_FGET(lb_cmd_usr, options);
				lb_cmd_ker.iter_cnt =
				    STRUCT_FGET(lb_cmd_usr, iter_cnt);
				lb_cmd_ker.xfer_cnt =
				    STRUCT_FGET(lb_cmd_usr, xfer_cnt);
				lb_cmd_ker.xmit_addr =
				    STRUCT_FGETP(lb_cmd_usr, xmit_addr);
				lb_cmd_ker.recv_addr =
				    STRUCT_FGETP(lb_cmd_usr, recv_addr);

				/* don't allow too much data */
				if (lb_cmd_ker.xfer_cnt >
				    MAX_LOOPBACK) {
					return (EINVAL);
				}

				/* two buffers, one outgoing one incoming */
				if ((flb_out =
				    (char *)kmem_zalloc(lb_cmd_ker.xfer_cnt,
				    KM_NOSLEEP)) == NULL) {
					return (ENOMEM);
				}
				if ((flb_in =
				    (char *)kmem_zalloc(lb_cmd_ker.xfer_cnt,
				    KM_NOSLEEP)) == NULL) {
					kmem_free((void *)flb_out,
					    lb_cmd_ker.xfer_cnt);
					return (ENOMEM);
				}

				/* copy xmit data into my buffer */
				if (ddi_copyin(STRUCT_FGETP(lb_cmd_usr,
				    xmit_addr),
				    flb_out,
				    lb_cmd_ker.xfer_cnt,
				    mode) == -1) {
					rval = EFAULT;
					goto lb_done;
				}

			try_again:
				IFP_MUTEX_ENTER(ifp);

				/* Mark all targets busy.   This will */
				/* cause all commands to be put on */
				/* the wait queue. */
				for (i = 0; i > IFP_MAX_TARGETS; i++) {
					target = ifp->ifp_targets[i];

					/* save previous state */
					targ_state[i] = target->ifpt_state;
					/* set current state to BUSY */
					target->ifpt_state = IFPT_TARGET_BUSY;
				}
				/* insure that there are no commands */
				/* in process. */
				for (i = 0; i < (IFP_MAX_SLOTS - 1); i++) {
					if (ifp->ifp_slots[i].slot_cmd !=
					    NULL) {
						/* cmds in process */
						/* so retry. */
						for (i = 0;
						    i > IFP_MAX_TARGETS;
						    i++) {
							target =
							    ifp->ifp_targets[i];
							/* restore prev state */
							target->ifpt_state =
							    targ_state[i];
						}

						IFP_MUTEX_EXIT(ifp);
						if (again_cnt++ > 10) {
							/* We did our best */
							/* but now we have to */
							/* give up. */
							return (EBUSY);
						}
						/* wait 1 second */
						delay(drv_usectohz(1000000));
						goto try_again;
					}
				}

				/* wait a bit for commands in progress */
				/* to complete. */
				rval = ifp_i_issue_flb(ifp, flb_out, flb_in,
				    &lb_cmd_ker);
				/* Cleanup mess from loopback cmd */
				(void) ifp_i_reset_interface(ifp, 0);

				/* copy the result parameters from */
				/* the system native structure to */
				/* the user (32/64) structure. */
				STRUCT_FSET(lb_cmd_usr, status,
				    lb_cmd_ker.status);
				STRUCT_FSET(lb_cmd_usr, crc_cnt,
				    lb_cmd_ker.crc_cnt);
				STRUCT_FSET(lb_cmd_usr, disparity_cnt,
				    lb_cmd_ker.disparity_cnt);
				STRUCT_FSET(lb_cmd_usr, frame_len_err_cnt,
				    lb_cmd_ker.frame_len_err_cnt);
				STRUCT_FSET(lb_cmd_usr, fail_iter_cnt,
				    lb_cmd_ker.fail_iter_cnt);

				for (i = 0; i > IFP_MAX_TARGETS; i++) {
					target = ifp->ifp_targets[i];

					/* restore the previous state */
					target->ifpt_state = targ_state[i];
				}

				IFP_MUTEX_EXIT(ifp);

				if (ddi_copyout(STRUCT_BUF(lb_cmd_usr),
				    (caddr_t)arg,
				    STRUCT_SIZE(lb_cmd_usr), mode) != 0) {
					rval = EFAULT;
					goto lb_done;
				}

				/* Is system memory being used? */
				if (lb_cmd_ker.options & 0x0020) {
					/* copy receive data to user */
					if (ddi_copyout(flb_in,
					    STRUCT_FGETP(lb_cmd_usr, recv_addr),
					    lb_cmd_ker.xfer_cnt,
					    mode) == -1) {
						rval = EFAULT;
					}
				}
lb_done:
				kmem_free((void *)flb_out, lb_cmd_ker.xfer_cnt);
				kmem_free((void *)flb_in, lb_cmd_ker.xfer_cnt);
				return (rval);
			}

		default:
			return (ENOTTY);
	}

	ATRACE(ifp_ioctl, 0x11111114, ifp);
	return (0);
}

static ushort_t ifp_i_get_serial_no(ifp_t *);

/*
 * Attach ifp host adapter.  Allocate data structures and link
 * to ifp_head list.  Initialize the ifp and we're
 * on the air.
 */
/*ARGSUSED*/
static int
ifp_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int rval;
	ifp_t *ifp;
	size_t rlen;
	uint_t count;
	int instance;
	char buf[64];
	int idpromlen;
	char idprombuf[32];
	int events_bound = 0;
	int minor_created = 0;
	int mutex_initted = 0;
	int interrupt_added = 0;
	int bound_dma_handle = 0;
	int hp_thread_created = 0;
	int scsi_hba_attached = 0;
	struct ifp *s_ifp, *l_ifp;
	int alloced_dma_handle = 0;
	ddi_dma_attr_t tmp_dma_attr;
	scsi_hba_tran_t *tran = NULL;
	int bound_fcal_map_handle = 0;
	int bound_fcal_port_handle = 0;
	ddi_device_acc_attr_t dev_attr;
	int alloced_fcal_map_handle = 0;
	int alloced_fcal_port_handle = 0;
	dev_info_t *pdip, *ppdip;

	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	instance = ddi_get_instance(dip);


	ATRACE(ifp_attach, 0x22222222, instance);
	switch (cmd) {
	case DDI_ATTACH:
		dev_attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
		dev_attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
		break;

	case DDI_RESUME:
		if ((tran = ddi_get_driver_private(dip)) == NULL)
			return (DDI_FAILURE);

		ifp = TRAN2IFP(tran);
		if (!ifp) {
			return (DDI_FAILURE);
		}

		/*
		 * the downloaded firmware on the card will be erased by
		 * the power cycle and a new download is needed.
		 */
		IFP_MUTEX_ENTER(ifp);

		/*
		 * Do not reset interface if shutdown is set.
		 */
		if (!ifp->ifp_shutdown) {
			rval = ifp_i_download_fw(ifp);
			if (rval) {
				IFP_MUTEX_EXIT(ifp);
				return (DDI_FAILURE);
			}
			if (ifp_i_reset_interface(ifp, IFP_FORCE_RESET_BUS)) {
				IFP_MUTEX_EXIT(ifp);
				return (DDI_FAILURE);
			}
			mutex_exit(IFP_REQ_MUTEX(ifp));
			(void) scsi_hba_reset_notify_callback(IFP_RESP_MUTEX(
			    ifp), &ifp->ifp_reset_notify_listf);
			mutex_exit(IFP_RESP_MUTEX(ifp));

			mutex_enter(IFP_REQ_MUTEX(ifp));
			IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);

		} else {
			IFP_MUTEX_EXIT(ifp);
		}

		ifp->ifp_suspended = 0;
		mutex_enter(&ifp_global_mutex);
		if (ifp_timeout_id == 0) {
			ifp_timeout_id = timeout(ifp_i_watch, NULL, ifp_tick);
			timeout_initted = 1;
		}
		mutex_exit(&ifp_global_mutex);
		return (DDI_SUCCESS);

	default:
		ifp_i_log(NULL, CE_WARN,
		    "ifp%d: Cmd != DDI_ATTACH/DDI_RESUME", instance);
		return (DDI_FAILURE);
	}

	/*
	 * Fail the attach on the UltraSPARC-III systems
	 */
	pdip = ddi_get_parent(dip);
	if (strcmp(ddi_driver_name(pdip), "pcisch") == 0) {
		ifp_i_log(NULL, CE_WARN, "!Qlogic 2100 is not supported on "
		    "UltraSPARC-III machines");
		return (DDI_FAILURE);
	} else {
		ppdip = ddi_get_parent(pdip);
		if (strcmp(ddi_driver_name(ppdip), "pcisch") == 0) {
			ifp_i_log(NULL, CE_WARN, "!Qlogic 2100 is not"
			    "supported on UltraSPARC-III machines");
			ifp_i_log(NULL, CE_WARN, "!Driver **  Name %s",
			    ddi_driver_name(ppdip));
			return (DDI_FAILURE);
		}
	}

	/*
	 * Since we know that some instantiations of this device can
	 * be plugged into slave-only SBus slots, check to see whether
	 * this is one such.
	 */
	if (ddi_slaveonly(dip) == DDI_SUCCESS) {
		ifp_i_log(NULL, CE_WARN,
		    "ifp%d: Device in slave-only slot, unused",
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
		ifp_i_log(NULL, CE_WARN,
		    "ifp%d: Device is using a hilevel intr, unused",
		    instance);
		return (DDI_FAILURE);
	}

	/*
	 * Allocate ifp data structure.
	 */
	if (ddi_soft_state_zalloc(ifp_state, instance) != DDI_SUCCESS) {
		ifp_i_log(NULL, CE_WARN, "ifp%d: Failed to alloc soft state",
		    instance);
		return (DDI_FAILURE);
	}

	ifp = (struct ifp *)ddi_get_soft_state(ifp_state, instance);
	if (ifp == (struct ifp *)NULL) {
		ifp_i_log(NULL, CE_WARN, "ifp%d: Bad soft state", instance);
		ddi_soft_state_free(ifp_state, instance);
		return (DDI_FAILURE);
	}

	/*
	 * map in pci config space
	 */
	if (pci_config_setup(dip, &ifp->ifp_pci_config_acc_handle) !=
	    DDI_SUCCESS) {
		ifp_i_log(NULL, CE_WARN,
		    "ifp%d: Unable to map pci config registers",
		    instance);
		ddi_soft_state_free(ifp_state, instance);
		return (DDI_FAILURE);
	}

	/* get the device id of this chip */
	ifp->ifp_chip_id = pci_config_get16(ifp->ifp_pci_config_acc_handle,
	    PCI_CONF_DEVID);
	if (ifp->ifp_chip_id == 0x2200) {
		/* The 2200 chip has more mailbox registers */
		ifp->ifp_chip_reg_cnt = IFP_MAX_MBOX_REGS22;
	} else {
		ifp->ifp_chip_reg_cnt = IFP_MAX_MBOX_REGS21;
	}

	/* get the revision number this chip */
	ifp->ifp_chip_rev = pci_config_get16(ifp->ifp_pci_config_acc_handle,
	    PCI_CONF_REVID);

	/*
	 * Allocate a transport structure
	 */
	ifp->ifp_tran = tran = scsi_hba_tran_alloc(dip, SCSI_HBA_CANSLEEP);
	if (tran == (struct scsi_hba_tran *)NULL) {
		ifp_i_log(ifp, CE_WARN, "ifp%d: cannot alloc tran", instance);
		goto fail;
	}

	/* Indicate that we are 'sizeof (scsi_*(9S))' clean. */
	scsi_size_clean(dip);		/* SCSI_SIZE_CLEAN_VERIFY ok */

	ifp->ifp_dip		= dip;

	tran->tran_hba_private	= ifp;
	tran->tran_tgt_private	= NULL;
	tran->tran_tgt_init	= ifp_scsi_tgt_init;
	tran->tran_tgt_probe	= NULL;
	tran->tran_tgt_free	= ifp_scsi_tgt_free;

	tran->tran_start	= ifp_scsi_start;
	tran->tran_abort	= ifp_scsi_abort;
	tran->tran_reset	= ifp_scsi_reset;
	tran->tran_getcap	= ifp_scsi_getcap;
	tran->tran_setcap	= ifp_scsi_setcap;
	tran->tran_init_pkt	= ifp_scsi_init_pkt;
	tran->tran_destroy_pkt	= ifp_scsi_destroy_pkt;
	tran->tran_dmafree	= ifp_scsi_dmafree;
	tran->tran_sync_pkt	= ifp_scsi_sync_pkt;
	tran->tran_reset_notify = ifp_scsi_reset_notify;

	tran->tran_get_bus_addr	= ifp_scsi_get_bus_addr;
	tran->tran_get_name	= ifp_scsi_get_name;
	tran->tran_bus_reset	= NULL;
	tran->tran_quiesce	= NULL;
	tran->tran_unquiesce	= NULL;

	/*
	 * register event notification routines with scsa
	 */
	tran->tran_get_eventcookie = ifp_bus_get_eventcookie;
	tran->tran_add_eventcall = ifp_bus_add_eventcall;
	tran->tran_remove_eventcall = ifp_bus_remove_eventcall;
	tran->tran_post_event = ifp_bus_post_event;

	/*
	 * register bus configure/unconfigure
	 */
	tran->tran_bus_config		= ifp_scsi_bus_config;
	tran->tran_bus_unconfig		= ifp_scsi_bus_unconfig;

	idpromlen = 32;
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, DDI_PROP_CANSLEEP,
	    "idprom", (caddr_t)idprombuf, &idpromlen) != DDI_PROP_SUCCESS) {
		ifp_i_log(ifp, CE_WARN, "Unable to read idprom property");
		goto fail;
	}

	ifp->ifp_my_wwn.raw_wwn[2] = idprombuf[2];
	ifp->ifp_my_wwn.raw_wwn[3] = idprombuf[3];
	ifp->ifp_my_wwn.raw_wwn[4] = idprombuf[4];
	ifp->ifp_my_wwn.raw_wwn[5] = idprombuf[5];
	ifp->ifp_my_wwn.raw_wwn[6] = idprombuf[6];
	ifp->ifp_my_wwn.raw_wwn[7] = idprombuf[7];
	ifp->ifp_my_wwn.w.naa_id = NAA_ID_IEEE_EXTENDED;
	ifp->ifp_my_wwn.w.nport_id = 0;

	ifp->ifp_ksp = NULL;
	if ((ifp->ifp_ksp = kstat_create("ifp", instance, "statistics",
	    "controller", KSTAT_TYPE_RAW, sizeof (struct ifp_stats),
	    KSTAT_FLAG_VIRTUAL)) == NULL) {
			ifp_i_log(ifp, CE_WARN, "failed to create kstat");
	} else {
		ifp->ifp_stats.version = 1;
		ifp->ifp_ksp->ks_data = (void *)&ifp->ifp_stats;
		ifp->ifp_ksp->ks_private = ifp;
		ifp->ifp_ksp->ks_update = ifp_kstat_update;
		ifp->ifp_ksp->ks_ndata = 1;
		ifp->ifp_ksp->ks_data_size = sizeof (ifp->ifp_stats);
		kstat_install(ifp->ifp_ksp);
	}
	if (ddi_create_minor_node(dip, "devctl", S_IFCHR,
	    IFP_INST2DEVCTL_MINOR(instance),
	    DDI_NT_NEXUS, 0) != DDI_SUCCESS) {
		ifp_i_log(ifp, CE_WARN, "ddi_create_minor_node failed");
		goto fail;
	}
	/* create fc minor node */
	if (ddi_create_minor_node(dip, "fc", S_IFCHR,
	    IFP_INST2FC_MINOR(instance),
	    DDI_NT_FC_ATTACHMENT_POINT, 0) != DDI_SUCCESS) {
		ifp_i_log(ifp, CE_WARN, "ddi_create_minor_node failed");
		goto fail;
	}
	minor_created++;

	/*
	 * Map in device registers
	 * Using register-set number 2 which is PCI-MEM space.
	 */
	dev_attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	if (ddi_regs_map_setup(dip, IFP_PCI_REG_NUMBER,
	    (caddr_t *)&ifp->ifp_biu_reg, 0,
	    (off_t)sizeof (struct ifp_biu_regs),
	    &dev_attr, &ifp->ifp_biu_acc_handle) != DDI_SUCCESS) {
		ifp_i_log(NULL, CE_WARN, "ifp%d: Unable to map biu registers",
		    instance);
		goto fail;
	}

	tmp_dma_attr = dma_ifpattr;

	if (ddi_dma_alloc_handle(dip, &tmp_dma_attr,
	    DDI_DMA_SLEEP, NULL, &ifp->ifp_dmahandle) != DDI_SUCCESS) {
		ifp_i_log(ifp, CE_WARN, "cannot alloc dma handle");
	}
	alloced_dma_handle++;

	ifp->ifp_cmdarea = NULL;
	if (ddi_dma_mem_alloc(ifp->ifp_dmahandle, (size_t)IFP_QUEUE_SIZE,
	    &dev_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, (caddr_t *)&ifp->ifp_cmdarea, &rlen,
	    &ifp->ifp_dma_acc_handle) != DDI_SUCCESS) {
			ifp_i_log(ifp, CE_WARN, "cannot alloc cmd area");
			goto fail;
	}
	if (ddi_dma_addr_bind_handle(ifp->ifp_dmahandle,
	    NULL, ifp->ifp_cmdarea, (size_t)IFP_QUEUE_SIZE,
	    DDI_DMA_RDWR|DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP, NULL, &ifp->ifp_dmacookie,
	    &count) != DDI_DMA_MAPPED) {
			ifp_i_log(ifp, CE_WARN, "cannot bind cmd area");
			goto fail;
	}
	bound_dma_handle++;

	if (ddi_dma_alloc_handle(dip, &tmp_dma_attr,
	    DDI_DMA_SLEEP, NULL, &ifp->ifp_fcal_maphandle) != DDI_SUCCESS) {
		ifp_i_log(ifp, CE_WARN, "cannot alloc fcal handle");
			goto fail;
	}
	alloced_fcal_map_handle++;

	if (ddi_dma_addr_bind_handle(ifp->ifp_fcal_maphandle,
	    NULL, (char *)ifp->ifp_loop_map, (size_t)IFP_FCAL_MAP_SIZE,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT,
	    NULL, &ifp->ifp_map_dmacookie, &count) != DDI_DMA_MAPPED) {
			ifp_i_log(ifp, CE_WARN, "cannot bind fcal map");
			goto fail;
	}
	bound_fcal_map_handle++;

	if (ddi_dma_alloc_handle(dip, &tmp_dma_attr,
	    DDI_DMA_SLEEP, NULL, &ifp->ifp_fcal_porthandle)
	    != DDI_SUCCESS) {
		ifp_i_log(ifp, CE_WARN, "cannot alloc fcal port handle");
			goto fail;
	}
	alloced_fcal_port_handle++;

	if (ddi_dma_addr_bind_handle(ifp->ifp_fcal_porthandle,
	    NULL, (char *)&ifp->ifp_portdb, (size_t)sizeof (ifp_portdb_t),
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT,
	    NULL, &ifp->ifp_portdb_dmacookie, &count) != DDI_DMA_MAPPED) {
			ifp_i_log(ifp, CE_WARN, "cannot bind portdb");
			goto fail;
	}
	bound_fcal_port_handle++;

	bzero(ifp->ifp_cmdarea, IFP_QUEUE_SIZE);
	ifp->ifp_request_dvma = ifp->ifp_dmacookie.dmac_address;
	ifp->ifp_request_base = (struct ifp_request *)ifp->ifp_cmdarea;

	ifp->ifp_response_dvma = ifp->ifp_request_dvma + (IFP_MAX_REQUESTS *
	    sizeof (struct ifp_request));
	ifp->ifp_response_base = (struct ifp_response *)
	    (ifp->ifp_request_base + IFP_MAX_REQUESTS);
	ifp->ifp_request_in = 0;
	ifp->ifp_request_out = 0;
	ifp->ifp_response_in = 0;
	ifp->ifp_response_out = 0;

	/*
	 * get cookie so we can initialize the mutexes
	 */
	if (ddi_get_iblock_cookie(dip, (uint_t)0, &ifp->ifp_iblock)
	    != DDI_SUCCESS) {
		ifp_i_log(ifp, CE_WARN, "cannot get iblock cookie");
		goto fail;
	}

	ifp->ifp_scsi_reset_delay =
	    ddi_getprop(DDI_DEV_T_ANY, dip, 0, "scsi-reset-delay",
	    scsi_reset_delay);
	IFP_DEBUG(3, (ifp, SCSI_DEBUG, "ifp scsi_reset_delay=%d, global=%d",
	    ifp->ifp_scsi_reset_delay, scsi_reset_delay));
	if (ifp->ifp_scsi_reset_delay != scsi_reset_delay) {
		ifp_i_log(ifp, CE_NOTE, "scsi-reset-delay=%d",
		    ifp->ifp_scsi_reset_delay);
	}

	/*
	 * find the burstsize and reduce ours if necessary
	 * If no burst size found, select a reasonable default.
	 */
	tmp_dma_attr.dma_attr_burstsizes &=
	    (ddi_dma_burstsizes(ifp->ifp_dmahandle) &
	    ifp_burst_sizes_limit);
	ifp->ifp_burst_size = tmp_dma_attr.dma_attr_burstsizes;


	IFP_DEBUG(3, (ifp, SCSI_DEBUG, "ifpattr burstsize=%x",
	    ifp->ifp_burst_size));

	if (ifp->ifp_burst_size == -1) {
		ifp->ifp_burst_size = DEFAULT_BURSTSIZE | BURST32 | BURST64;
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "Using default burst sizes, 0x%x", ifp->ifp_burst_size));
	} else {
		ifp->ifp_burst_size &= BURSTSIZE_MASK;
		IFP_DEBUG(3, (ifp, SCSI_DEBUG, "burst sizes= 0x%x",
		    ifp->ifp_burst_size));
	}

	/*
	 * Attach this instance of the hba
	 */
	if (scsi_hba_attach_setup(dip, &tmp_dma_attr, tran,
	    SCSI_HBA_TRAN_CLONE) != DDI_SUCCESS) {
		ifp_i_log(ifp, CE_WARN, "scsi_hba_attach failed");
		goto fail;
	}
	scsi_hba_attached++;

#ifdef STE_TARGET_MODE
	{
		ifp->ifp_serial_num = ifp_i_get_serial_no(ifp);
		ifp->ifp_tm_hba_event = NULL;
		ifp->ifp_tm_dma_attr = &dma_ifpattr;
		ifp->ifp_tm_hard_loop_id = 0xff;
	}
#endif /* STE_TARGET_MODE */

	/*
	 * allocate ndi_event_hdl
	 */
	if (ndi_event_alloc_hdl(dip, ifp->ifp_iblock,
	    &ifp->ifp_ndi_event_hdl, NDI_SLEEP) != NDI_SUCCESS) {
		goto fail;
	}

	/* bind event defs to the event handle */
	ifp->ifp_ndi_events.ndi_events_version = NDI_EVENTS_REV1;
	ifp->ifp_ndi_events.ndi_n_events = IFP_N_NDI_EVENTS;
	ifp->ifp_ndi_events.ndi_event_defs = ifp_ndi_event_defs;

	if (ndi_event_bind_set(ifp->ifp_ndi_event_hdl,
	    &ifp->ifp_ndi_events, NDI_SLEEP) != NDI_SUCCESS) {
		goto fail;
	}

	/*
	 * initialize the mbox mutex
	 */
	mutex_init(IFP_MBOX_MUTEX(ifp), NULL, MUTEX_DRIVER,
	    ifp->ifp_iblock);

	/*
	 * initialize the request & response mutex
	 */
	mutex_init(IFP_WAITQ_MUTEX(ifp), NULL, MUTEX_DRIVER,
	    ifp->ifp_iblock);

	mutex_init(IFP_REQ_MUTEX(ifp), NULL, MUTEX_DRIVER,
	    ifp->ifp_iblock);
	mutex_init(IFP_RESP_MUTEX(ifp), NULL, MUTEX_DRIVER,
	    ifp->ifp_iblock);
	cv_init(IFP_HP_DAEMON_CV(ifp), NULL, CV_DRIVER, NULL);
	mutex_init(IFP_HP_DAEMON_MUTEX(ifp), NULL, MUTEX_DRIVER,
	    ifp->ifp_iblock);
	mutex_initted = 1;

	ifp->ifp_hp_elem_head = ifp->ifp_hp_elem_tail = NULL;
	ifp->ifp_hp_thread_go_away = 0;

	(void) thread_create(NULL, 0, (void (*)())ifp_hp_daemon, ifp, 0, &p0,
	    TS_RUN, minclsyspri);

	hp_thread_created++;

	/*
	 * install the interrupt handler
	 */
	if (ddi_add_intr(dip, (uint_t)0,
	    (ddi_iblock_cookie_t *)&ifp->ifp_iblock,
	    (ddi_idevice_cookie_t *)0, ifp_intr, (caddr_t)ifp)) {
		ifp_i_log(ifp, CE_WARN, "Cannot add intr");
		goto fail;
	} else {
		interrupt_added = 1;
	}

	/*
	 * link all ifp's, for debugging
	 */
	rw_enter(&ifp_global_rwlock, RW_WRITER);
	ifp->ifp_next = NULL;

	if (ifp_head) {
		ifp_tail->ifp_next = ifp;
		ifp_tail = ifp;
	} else {
		ifp_head = ifp_tail = ifp;
	}
	ifp_last = ifp_head;

	/* to keep lint happy */
	ifp_last = ifp_last;

	rw_exit(&ifp_global_rwlock);

	/*
	 * set up watchdog per all ifp's
	 */
	mutex_enter(&ifp_global_mutex);

	if (ifp_timeout_id == 0) {
		ASSERT(timeout_initted == 0);
		ifp_scsi_watchdog_tick =
		    ddi_getprop(DDI_DEV_T_ANY, dip, 0, "scsi-watchdog-tick",
		    scsi_watchdog_tick);
		if (ifp_scsi_watchdog_tick != scsi_watchdog_tick) {
			ifp_i_log(ifp, CE_NOTE, "scsi-watchdog-tick=%d",
			    ifp_scsi_watchdog_tick);
		}
		/*
		 * ifp_scsi_watchdog_tick should not be less than pkt_time
		 */
		if (ifp_scsi_watchdog_tick < IFP_DEFLT_WATCHDOG_SECS)
			ifp_scsi_watchdog_tick = IFP_DEFLT_WATCHDOG_SECS;

		ifp_tick =
		    drv_usectohz((clock_t)ifp_scsi_watchdog_tick * 1000000);

		watchdog_tick = IFP_LOOPDOWN_TIME;
		watchdog_ticks_per_ifp_tick = ifp_tick / IFP_LOOPDOWN_TIME;

		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "ifp_scsi_watchdog_tick=%d, ifp_tick=%d",
		    ifp_scsi_watchdog_tick, ifp_tick));
		ifp_timeout_id = timeout(ifp_i_watch, NULL, watchdog_tick);
		timeout_initted = 1;
	}

	mutex_exit(&ifp_global_mutex);

	IFP_MUTEX_ENTER(ifp);

	/*
	 * create kmem cache for packets
	 */
	(void) sprintf(buf, "ifp%d_cache", instance);
	ifp->ifp_kmem_cache = kmem_cache_create(buf,
	    EXTCMDS_SIZE, 8, ifp_kmem_cache_constructor,
	    ifp_kmem_cache_destructor, NULL, (void *)ifp, NULL, 0);

	ifp->ifp_state = IFP_STATE_OFFLINE;

	/*
	 * Download the IFP firmware that has been linked in.
	 * We need the mutexes here to avoid assertion failures in
	 * the mbox cmds
	 *
	 * FW is being started twice -- once in download fw and
	 * the next time in reset_interface XXX
	 */
	rval = ifp_i_download_fw(ifp);

	if (rval) {
		IFP_MUTEX_EXIT(ifp);
		goto fail;
	}

	if (ifp_i_reset_interface(ifp,
	    IFP_FORCE_RESET_BUS|IFP_DONT_WAIT_FOR_FW_READY)) {
		IFP_MUTEX_EXIT(ifp);
		goto fail;
	}
	ifp->ifp_reset_time = ddi_get_lbolt64();

	IFP_MUTEX_EXIT(ifp);

	ddi_report_dev(dip);

	ifp_i_log(ifp, CE_NOTE,
	    "!Chip %x Rev %d; Firmware Version: %0d.%02d.%02d",
	    ifp->ifp_chip_id, ifp->ifp_chip_rev,
	    ifp->ifp_major_rev, ifp->ifp_minor_rev, ifp->ifp_subminor_rev);

	ATRACE(ifp_attach, 0x22222223, instance);
	return (DDI_SUCCESS);

fail:
	ifp_i_log(NULL, CE_WARN, "ifp%d: Unable to attach", instance);

	if (events_bound) {
		(void) ndi_event_unbind_set(ifp->ifp_ndi_event_hdl,
		    &ifp->ifp_ndi_events, NDI_SLEEP);
	}
	if (ifp->ifp_ndi_event_hdl) {
		(void) ndi_event_free_hdl(ifp->ifp_ndi_event_hdl);
	}
	if (minor_created) {
		ddi_remove_minor_node(dip, "NULL");
	}

	if (ifp->ifp_kmem_cache) {
		kmem_cache_destroy(ifp->ifp_kmem_cache);
	}
	if (ifp->ifp_cmdarea) {
		if (bound_dma_handle) {
			(void) ddi_dma_unbind_handle(ifp->ifp_dmahandle);
		}
		ddi_dma_mem_free(&ifp->ifp_dma_acc_handle);
	}
	if (timeout_initted && (ifp == ifp_head) && (ifp == ifp_tail)) {
		mutex_enter(&ifp_global_mutex);
		timeout_initted = 0;
		mutex_exit(&ifp_global_mutex);
		(void) untimeout(ifp_timeout_id);
		mutex_enter(&ifp_global_mutex);
		ifp_timeout_id = 0;
		mutex_exit(&ifp_global_mutex);
	}
	if (hp_thread_created) {
		int	i = 100;

		/* make the thread go away */
		mutex_enter(IFP_HP_DAEMON_MUTEX(ifp));
		ifp->ifp_hp_thread_go_away = 1;
		cv_signal(IFP_HP_DAEMON_CV(ifp));
		mutex_exit(IFP_HP_DAEMON_MUTEX(ifp));
		while ((ifp->ifp_hp_elem_head != NULL) ||
		    (ifp->ifp_hp_thread_go_away != 0)) {
			/* give other threads a chance to run */
			delay(drv_usectohz(100000));
			/* No infinite loops, 10 seconds should do it */
			if (i-- <= 0) {
				/* hp daemon did not die. */
				return (DDI_FAILURE);
			}
		}
	}
	if (mutex_initted) {
		mutex_destroy(IFP_WAITQ_MUTEX(ifp));
		mutex_destroy(IFP_REQ_MUTEX(ifp));
		mutex_destroy(IFP_RESP_MUTEX(ifp));
		mutex_destroy(IFP_MBOX_MUTEX(ifp));
		cv_destroy(IFP_HP_DAEMON_CV(ifp));
		mutex_destroy(IFP_HP_DAEMON_MUTEX(ifp));
	}
	if (alloced_dma_handle) {
		ddi_dma_free_handle(&ifp->ifp_dmahandle);
	}
	if (bound_fcal_map_handle) {
		(void) ddi_dma_unbind_handle(ifp->ifp_fcal_maphandle);
	}
	if (alloced_fcal_map_handle) {
		ddi_dma_free_handle(&ifp->ifp_fcal_maphandle);
	}
	if (bound_fcal_port_handle) {
		(void) ddi_dma_unbind_handle(ifp->ifp_fcal_porthandle);
	}
	if (alloced_fcal_port_handle) {
		ddi_dma_free_handle(&ifp->ifp_fcal_porthandle);
	}
	if (interrupt_added) {
		IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icr,
		    IFP_BUS_ICR_DISABLE_INTS);
		IFP_CLEAR_RISC_INT(ifp);
		ddi_remove_intr(dip, (uint_t)0, ifp->ifp_iblock);
	}

	rw_enter(&ifp_global_rwlock, RW_WRITER);
	for (l_ifp = s_ifp = ifp_head; s_ifp != NULL;
	    s_ifp = s_ifp->ifp_next) {
		if (s_ifp == ifp) {
			if (s_ifp == ifp_head) {
				ifp_head = ifp->ifp_next;
				if (ifp_tail == ifp) {
					ifp_tail = NULL;
				}
			} else {
				if (ifp_tail == ifp) {
					ifp_tail = l_ifp;
				}
				l_ifp->ifp_next = ifp->ifp_next;
			}
			break;
		}
		l_ifp = s_ifp;
	}
	rw_exit(&ifp_global_rwlock);

	if (ifp->ifp_pci_config_acc_handle) {
		pci_config_teardown(&ifp->ifp_pci_config_acc_handle);
	}
	if (ifp->ifp_biu_acc_handle) {
		ddi_regs_map_free(&ifp->ifp_biu_acc_handle);
	}
	if (ifp->ifp_ksp) {
		kstat_delete(ifp->ifp_ksp);
	}
	if (scsi_hba_attached) {
		(void) scsi_hba_detach(dip);
	}
	if (ifp->ifp_tran) {
		scsi_hba_tran_free(ifp->ifp_tran);
	}
	ddi_soft_state_free(ifp_state, instance);
	ATRACE(ifp_attach, 0x22222224, instance);
	return (DDI_FAILURE);
}

/*ARGSUSED*/
static int
ifp_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	struct ifp *ifp;
	struct ifp *nifp;
	scsi_hba_tran_t	*tran;


	ATRACE(ifp_detach, 0x33333333, cmd);
	switch (cmd) {
	case DDI_DETACH:
		ATRACE(ifp_detach, 0x33333334, cmd);
		return (ifp_dr_detach(dip));

	case DDI_SUSPEND:
		if ((tran = ddi_get_driver_private(dip)) == NULL) {
			ATRACE(ifp_detach, 0x33333335, cmd);
			return (DDI_FAILURE);
		}
		ifp = TRAN2IFP(tran);
		if (!ifp) {
			ATRACE(ifp_detach, 0x33333336, cmd);
			return (DDI_FAILURE);
		}
		/*
		 * reset ifp and bus
		 */
		IFP_MUTEX_ENTER(ifp);
		if (ifp->ifp_waitq_timeout != 0) {
			(void) untimeout(ifp->ifp_waitq_timeout);
			ifp->ifp_waitq_timeout = 0;
		}

		/*
		 * Do not reset interface if shutdown is set.
		 */
		if (!ifp->ifp_shutdown) {
			if (ifp_i_reset_interface(ifp, IFP_FORCE_RESET_BUS)) {
				IFP_MUTEX_EXIT(ifp);
				ATRACE(ifp_detach, 0x33333337, cmd);
				return (DDI_FAILURE);
			}
		}
		ifp->ifp_suspended = 1;
		IFP_MUTEX_EXIT(ifp);

		rw_enter(&ifp_global_rwlock, RW_WRITER);
		for (nifp = ifp_head; nifp; nifp = nifp->ifp_next) {
			if (!nifp->ifp_suspended) {
				rw_exit(&ifp_global_rwlock);
				ATRACE(ifp_detach, AT_ACT('c', 'm', 'd', 'X'),
				    cmd);
				return (DDI_SUCCESS);
			}
		}
		rw_exit(&ifp_global_rwlock);

		mutex_enter(&ifp_global_mutex);
		if (ifp_timeout_id != 0) {
			timeout_id_t tid = ifp_timeout_id;
			ifp_timeout_id = 0;
			timeout_initted = 0;
			mutex_exit(&ifp_global_mutex);
			(void) untimeout(tid);
		} else {
			mutex_exit(&ifp_global_mutex);
		}
		ATRACE(ifp_detach, AT_ACT('c', 'm', 'd', 'X'), cmd);
		return (DDI_SUCCESS);

	default:
		ATRACE(ifp_detach, AT_ACT('c', 'm', 'd', 'Y'), cmd);
		return (DDI_FAILURE);
	}
}

static int
ifp_dr_detach(dev_info_t *dip)
{
	int			i;
	int			cnt = 100;
	scsi_hba_tran_t		*tran;
	struct ifp_target	*target;
	struct ifp		*ifp, *nifp, *tifp;
	int			instance = ddi_get_instance(dip);

	ATRACE(ifp_dr_detach, AT_ACT('d', 'i', 'p', ' '), dip);

	if ((tran = ddi_get_driver_private(dip)) == NULL)
		return (DDI_FAILURE);

	ifp = TRAN2IFP(tran);
	if (!ifp) {
		return (DDI_FAILURE);
	}

#ifdef STE_TARGET_MODE
	IFP_MUTEX_ENTER(ifp);
	if ((ifp->ifp_softstate & IFP_IFPTM_ATTACHED) ||
	    ifp->ifp_tm_hba_event != NULL) {
		IFP_MUTEX_EXIT(ifp);
		return (DDI_FAILURE);
	}
	ifp->ifp_softstate |= IFP_IFPTM_DETACHED;
	IFP_MUTEX_EXIT(ifp);
#endif /* STE_TARGET_MODE */

	/*
	 * deallocate reset notify callback list
	 */
	scsi_hba_reset_notify_tear_down(ifp->ifp_reset_notify_listf);

	/*
	 * Remove ndi event stuff
	 */
	(void) ndi_event_unbind_set(ifp->ifp_ndi_event_hdl,
	    &ifp->ifp_ndi_events, NDI_SLEEP);
	(void) ndi_event_free_hdl(ifp->ifp_ndi_event_hdl);

	/*
	 * Force interrupts OFF and remove handler
	 */
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icr,
	    IFP_BUS_ICR_DISABLE_INTS);
	IFP_CLEAR_RISC_INT(ifp);
	ddi_remove_intr(dip, (uint_t)0, ifp->ifp_iblock);

	/*
	 * Remove device instance from the global linked list
	 */
	rw_enter(&ifp_global_rwlock, RW_WRITER);
	for (nifp = tifp = ifp_head; nifp;
	    tifp = nifp, nifp = nifp->ifp_next) {
		if (nifp == ifp)
			break;
	}

	if (nifp == ifp_head)
		ifp_head = tifp = ifp->ifp_next;
	else
		tifp->ifp_next = ifp->ifp_next;
	if (nifp == ifp_tail)
		ifp_tail = tifp;
	ifp_last = ifp_head;
	rw_exit(&ifp_global_rwlock);

	/*
	 * If active, CANCEL watch thread.
	 */
	mutex_enter(&ifp_global_mutex);
	if (timeout_initted && (ifp_head == NULL)) {
		timeout_initted = 0;
		mutex_exit(&ifp_global_mutex);
		(void) untimeout(ifp_timeout_id);
		mutex_enter(&ifp_global_mutex);
		ifp_timeout_id = 0;
	}
	mutex_exit(&ifp_global_mutex);

	/*
	 * Release miscellaneous device resources
	 */
	if (ifp->ifp_kmem_cache) {
		kmem_cache_destroy(ifp->ifp_kmem_cache);
	}

	if (ifp->ifp_cmdarea) {
		(void) ddi_dma_unbind_handle(ifp->ifp_dmahandle);
		ddi_dma_mem_free(&ifp->ifp_dma_acc_handle);
	}

	if (ifp->ifp_dmahandle)
		ddi_dma_free_handle(&ifp->ifp_dmahandle);

	(void) ddi_dma_unbind_handle(ifp->ifp_fcal_maphandle);
	if (ifp->ifp_fcal_maphandle)
		ddi_dma_free_handle(&ifp->ifp_fcal_maphandle);

	(void) ddi_dma_unbind_handle(ifp->ifp_fcal_porthandle);
	if (ifp->ifp_fcal_porthandle)
		ddi_dma_free_handle(&ifp->ifp_fcal_porthandle);

	if (ifp->ifp_pci_config_acc_handle) {
		pci_config_teardown(&ifp->ifp_pci_config_acc_handle);
	}
	if (ifp->ifp_biu_acc_handle) {
		ddi_regs_map_free(&ifp->ifp_biu_acc_handle);
	}

	/* make ifp_hp_daemon go away */
	mutex_enter(IFP_HP_DAEMON_MUTEX(ifp));
	ifp->ifp_hp_thread_go_away = 1;
	cv_signal(IFP_HP_DAEMON_CV(ifp));
	mutex_exit(IFP_HP_DAEMON_MUTEX(ifp));
	while ((ifp->ifp_hp_elem_head != NULL) ||
	    (ifp->ifp_hp_thread_go_away != 0)) {
		/* give other threads a chance to run */
		delay(drv_usectohz(100000));
		/* No infinite loops, 10 seconds should do it */
		if (cnt-- <= 0) {
			/* hp daemon did not die. */
			return (DDI_FAILURE);
		}
	}

	/*
	 * Remove device MT locks
	 */
	mutex_destroy(IFP_WAITQ_MUTEX(ifp));
	mutex_destroy(IFP_REQ_MUTEX(ifp));
	mutex_destroy(IFP_RESP_MUTEX(ifp));
	mutex_destroy(IFP_MBOX_MUTEX(ifp));
	cv_destroy(IFP_HP_DAEMON_CV(ifp));
	mutex_destroy(IFP_HP_DAEMON_MUTEX(ifp));

	/*
	 * Remove properties created during attach()
	 */
	ddi_prop_remove_all(dip);

	/*
	 * Free all target structures
	 */
	for (i = 0; i < IFP_MAX_TARGETS; i++) {
		target = ifp->ifp_targets[i];
		if (target != NULL) {
			ifp->ifp_targets[i] = 0;
			mutex_destroy(&target->ifpt_mutex);
			kmem_free(target, sizeof (struct ifp_target));
		}
	}

	/*
	 * Delete the DMA limits, transport vectors and remove the device
	 * links to the scsi_transport layer.
	 *	-- ddi_set_driver_private(dip, NULL)
	 */
	(void) scsi_hba_detach(dip);

	/*
	 * Free the scsi_transport structure for this device.
	 */
	scsi_hba_tran_free(tran);

	if (ifp->ifp_ksp)
		kstat_delete(ifp->ifp_ksp);

	ifp->ifp_dip = (dev_info_t *)NULL;
	ifp->ifp_tran = (scsi_hba_tran_t *)NULL;
	ifp->ifp_ksp = (kstat_t *)NULL;

	ddi_soft_state_free(ifp_state, instance);
	ddi_remove_minor_node(dip, "NULL");

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
ifp_scsi_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
	scsi_hba_tran_t *hba_tran, struct scsi_device *sd)
{
	int rval;
	int t_len;
	ifp_lun_t *lun_p, *lun_loop_p;
	uchar_t wwn[FC_WWN_SIZE];
	ifp_t *ifp = (struct ifp *)hba_tran->tran_hba_private;

#ifdef STE_TARGET_MODE
	char *name = ddi_get_name(tgt_dip);

	if (strcmp(name, "ifptm") == 0)
		return (DDI_SUCCESS);
#endif /* STE_TARGET_MODE */

	ATRACE(ifp_scsi_tgt_init, 0x44444440,
	    (sd->sd_address.a_target << 16) | sd->sd_address.a_lun);

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_scsi_tgt_init: target %x, lun %x, dip 0x%p",
	    sd->sd_address.a_target, sd->sd_address.a_lun,
	    (void *)tgt_dip));

	if ((sd->sd_address.a_target >= IFP_MAX_TARGETS) ||
	    (sd->sd_address.a_lun >= IFP_MAX_LUNS)) {
		ATRACE(ifp_scsi_tgt_init, 0x44444449, 0x2222);
		return (DDI_NOT_WELL_FORMED);
	}

	t_len = (int)sizeof (wwn);
	if (ddi_prop_op(DDI_DEV_T_ANY, tgt_dip, PROP_LEN_AND_VAL_BUF,
	    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "port-wwn",
	    (caddr_t)&wwn, &t_len) != DDI_SUCCESS) {
		ATRACE(ifp_scsi_tgt_init, 0x44444449, 0x3333);
		return (DDI_NOT_WELL_FORMED);
	}

	IFP_MUTEX_ENTER(ifp);
	if ((lun_p = ifp_i_lookup_lun(ifp, wwn, sd->sd_address.a_lun)) ==
	    NULL) {
		IFP_MUTEX_EXIT(ifp);
		ATRACE(ifp_scsi_tgt_init, 0x44444449, 0x5555);
		return (DDI_FAILURE);
	}

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_scsi_tgt_init: target %x, lun %x, state T %x L %x",
	    sd->sd_address.a_target, sd->sd_address.a_lun,
	    lun_p->ifpl_target->ifpt_state, lun_p->ifpl_state));

	ATRACE(ifp_scsi_tgt_init, 0x44444441,
	    (sd->sd_address.a_target << 16) | sd->sd_address.a_lun);

	mutex_enter(&lun_p->ifpl_target->ifpt_mutex);
	if (!(lun_p->ifpl_state & IFPL_LUN_INIT_DONE)) {

		ATRACE(ifp_scsi_tgt_init, 0x44444442,
		    (sd->sd_address.a_target << 16) | sd->sd_address.a_lun);

		hba_tran->tran_tgt_private = lun_p;
		lun_p->ifpl_target->ifpt_tran = hba_tran;
		lun_p->ifpl_state |= IFPL_LUN_INIT_DONE;

		/* if this is the last LUN then set the target as init done. */
		for (lun_loop_p = &lun_p->ifpl_target->ifpt_lun;
		    lun_loop_p != NULL;
		    lun_loop_p = lun_loop_p->ifpl_next) {
			if (!(lun_loop_p->ifpl_state & IFPL_LUN_INIT_DONE)) {
				/* if we found one that is not */
				/* inited then we can't */
				/* flag the target as inited */
				break;
			}
			/* are we at the end? */
			if (lun_loop_p->ifpl_next == NULL) {
				/* mark target as inited */
				lun_p->ifpl_target->ifpt_state |=
				    IFPT_TARGET_INIT_DONE;
				IFP_DEBUG(2, (ifp, SCSI_DEBUG,
		"ifp_scsi_tgt_init: target %x, lun %x, tgt init state DONE",
				    sd->sd_address.a_target,
				    sd->sd_address.a_lun));
			}
		}
		rval = DDI_SUCCESS;
	} else {
		rval = DDI_FAILURE;
	}
	mutex_exit(&lun_p->ifpl_target->ifpt_mutex);
	IFP_MUTEX_EXIT(ifp);

	ATRACE(ifp_scsi_tgt_init, 0x44444449, rval);

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_scsi_tgt_init: target %x, lun %x, state %x",
	    sd->sd_address.a_target, sd->sd_address.a_lun,
	    lun_p->ifpl_target->ifpt_state));

	return (rval);
}

/*ARGSUSED*/
static void
ifp_scsi_tgt_free(dev_info_t *hba_dip, dev_info_t *tgt_dip,
	scsi_hba_tran_t *hba_tran, struct scsi_device *sd)
{
	struct ifp *ifp = TRAN2IFP(hba_tran);
	ifp_lun_t	*lun_p = hba_tran->tran_tgt_private;
	ifp_target_t	*target;

	ATRACE(ifp_scsi_tgt_init, 0x44444450,
	    (sd->sd_address.a_target << 16) | sd->sd_address.a_lun);


	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_scsi_tgt_free: target %x, lun %x, lun_p 0x%p dip 0x%p",
	    sd->sd_address.a_target, sd->sd_address.a_lun,
	    (void *)lun_p, (void *)tgt_dip));

	if (lun_p != NULL) {
		target = lun_p->ifpl_target;
		mutex_enter(&target->ifpt_mutex);

		lun_p->ifpl_state &= ~IFPL_LUN_INIT_DONE;

		mutex_exit(&target->ifpt_mutex);

		ATRACE(ifp_scsi_tgt_init, 0x44444451, lun_p);

	}
	ATRACE(ifp_scsi_tgt_init, 0x44444459, lun_p);
}



static int
ifp_scsi_bus_config(dev_info_t *parent, uint_t flag,
    ddi_bus_config_op_t op, void *arg, dev_info_t **childp)
{
	int64_t	reset_delay;
	ifp_t	*ifp;

	ifp = ddi_get_soft_state(ifp_state, ddi_get_instance(parent));
	ASSERT(ifp);

	reset_delay = (int64_t)(USEC_TO_TICK(IFP_INIT_WAIT_TIMEOUT)) -
	    (ddi_get_lbolt64() - ifp->ifp_reset_time);
	if (reset_delay < 0)
		reset_delay = 0;

	if (ifp_config_debug > 0)
		flag |= NDI_DEVI_DEBUG;

	return (ndi_busop_bus_config(parent, flag, op, arg,
	    childp, (clock_t)reset_delay));
}

static int
ifp_scsi_bus_unconfig(dev_info_t *parent, uint_t flag,
    ddi_bus_config_op_t op, void *arg)
{
	if (ifp_config_debug > 0)
		flag |= NDI_DEVI_DEBUG;

	return (ndi_busop_bus_unconfig(parent, flag, op, arg));
}


/*
 * Function name : ifp_scsi_start()
 *
 * Return Values : TRAN_FATAL_ERROR	- ifp has been shutdown
 *		   TRAN_BUSY		- request queue is full
 *		   TRAN_ACCEPT		- pkt has been submitted to ifp
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
ifp_scsi_start(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	ifp_t *ifp;
	int cdbsize;
	clock_t local_lbolt;
	int placeonwaitQ = 0;
	int rval = TRAN_ACCEPT;
	struct ifp_request *req;
	struct ifp_cmd *sp = PKT2CMD(pkt);
	ifp_lun_t *lun_p = CMD2LUN(sp);
	ifp_target_t *target = lun_p->ifpl_target;
#ifdef IFPDEBUG
	struct ifp_response *resp;
#endif
	ATRACE(ifp_scsi_start, 0x55555550, pkt);
	ifp = ADDR2IFP(ap);

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_WAITQ_MUTEX(ifp)) == 0 || ddi_in_panic());


	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_scsi_start: pkt 0x%p id %x LUN %x cmd %x",
	    (void *)pkt, TGT(sp), LUN(sp), pkt->pkt_cdbp[0]));
	TRACE_1(TR_FAC_SCSI_ISP, TR_ISP_SCSI_START_START,
	    "ifp_scsi_start_start ifp = 0x%p", (void *)ifp);

#ifdef IFPDEBUG
	if (ifp_dumpstate) {
		if (ifp->ifp_shutdown)
			prom_printf("ifp is shutdown\n");
		else
			ifp_i_print_state(ifp);
		ifp_dumpstate = 0;
	}
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(ifp->ifp_waitf))
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(ifp->ifp_waitb))
	if (ifp_dumpslots) {
		ushort_t slot;
		struct ifp_cmd *sp;
		mutex_enter(IFP_REQ_MUTEX(ifp));
		ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
		for (slot = 0; slot < IFP_MAX_SLOTS; slot++) {
			sp = ifp->ifp_slots[slot].slot_cmd;
			if (sp) {
				struct scsi_pkt *pkt = CMD2PKT(sp);
				prom_printf("slot %x sp 0x%p time %x\n",
				    slot, (void *)sp, pkt->pkt_time);
			}
		}
		mutex_exit(IFP_REQ_MUTEX(ifp));
		prom_printf("waitf %p waitb %p\n",
		    (void *)ifp->ifp_waitf, (void *)ifp->ifp_waitb);
		ifp_dumpslots = 0;
	}
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(ifp->ifp_waitf))
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(ifp->ifp_waitb))
#endif

	/*
	 * if we have a shutdown, return packet
	 */
	if (ifp->ifp_shutdown) {
		ATRACE(ifp_scsi_start, 0x55555551, pkt);
		return (TRAN_FATAL_ERROR);
	}

	if (target->ifpt_state & (IFPT_TARGET_BUSY|IFPT_TARGET_OFFLINE)) {
		/*
		 * If target is marked busy, we will put it on the waitQ,
		 * but only if it is non-polled command
		 */
		IFP_DEBUG(2, (ifp, SCSI_DEBUG,
		"ifp_scsi_start: busy/offline pkt 0x%p id %x LUN %x state %x",
		    (void *)pkt, TGT(sp), LUN(sp), target->ifpt_state));
		if (target->ifpt_state & IFPT_TARGET_BUSY) {
			if ((pkt->pkt_flags & FLAG_NOINTR) == 0)
				placeonwaitQ = 1;
			else {
				ATRACE(ifp_scsi_start, 0x55555553, pkt);
				return (TRAN_BUSY);
			}
		} else {
			ATRACE(ifp_scsi_start, 0x55555554, pkt);
			return (TRAN_FATAL_ERROR);
		}
	}

	ASSERT(!(sp->cmd_flags & CFLAG_IN_TRANSPORT));
	sp->cmd_flags = (sp->cmd_flags & ~CFLAG_TRANFLAG) |
	    CFLAG_IN_TRANSPORT;
	pkt->pkt_reason = CMD_CMPLT;

	cdbsize = sp->cmd_cdblen;

	/*
	 * set up request in cmd_ifp_request area so it is ready to
	 * go once we have the request mutex
	 * XXX do we need to zero each time
	 */
	req = &sp->cmd_ifp_request;

	req->req_header.cq_entry_type = CQ_TYPE_REQUEST;
	req->req_header.cq_entry_count = 1;
	req->req_header.cq_flags = 0;
	req->req_header.cq_seqno = 0;
	req->req_cdblen = 0;
	req->req_reserved = 0;
	ASSERT(IFP_LOOKUP_ID(sp->cmd_id) == sp);
	req->req_token = sp->cmd_id;
	req->req_target = target->ifpt_loop_id;
	req->req_lun_trn = ap->a_lun;
	req->req_ext_lun = ap->a_lun;
	req->req_time = (ushort_t)pkt->pkt_time;
	IFP_SET_PKT_FLAGS(pkt->pkt_flags, req->req_flags);

	/*
	 * Setup dma transfers data segments.
	 *
	 * NOTE: Only 1 dataseg supported.
	 */
	if (sp->cmd_flags & CFLAG_DMAVALID) {
		/*
		 * Have to tell ifp which direction dma transfer is going.
		 */
		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_START_DMA_START,
		    "ifp_scsi_start");
		pkt->pkt_resid = sp->cmd_dmacount;

		if (sp->cmd_flags & CFLAG_CMDIOPB) {
			(void) ddi_dma_sync(sp->cmd_dmahandle, 0, 0,
			    DDI_DMA_SYNC_FORDEV);
		}

		req->req_seg_count = 1;
		req->req_byte_count = sp->cmd_dmacount;
		req->req_dataseg[0].d_count = sp->cmd_dmacount;
		req->req_dataseg[0].d_base = sp->cmd_dmacookie.dmac_address;
		if (sp->cmd_flags & CFLAG_DMASEND) {
			req->req_flags |= IFP_REQ_FLAG_DATA_WRITE;
		} else {
			req->req_flags |= IFP_REQ_FLAG_DATA_READ;
		}
		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_START_DMA_END,
		    "ifp_scsi_start");
	} else {
		req->req_seg_count = 0;
		req->req_byte_count = 0;
		req->req_dataseg[0].d_count = 0;
	}

#if defined(lint)
	/* to keep lint happy */
	cdbsize = cdbsize;
#endif
	IFP_LOAD_REQUEST_CDB(req, sp, cdbsize);

	/*
	 * Calculate deadline from pkt_time.
	 */

	local_lbolt = ddi_get_lbolt();
	sp->cmd_deadline = local_lbolt + (pkt->pkt_time << 7);
	sp->cmd_start_time = local_lbolt;

#ifdef IFPDEBUG
	resp = &sp->cmd_ifp_response;
	bzero(resp, sizeof (struct ifp_response));
#endif
	/*
	 * the normal case is a non-polled cmd, so deal with that first
	 */
	if ((pkt->pkt_flags & FLAG_NOINTR) == 0) {
		/*
		 * ifp request mutex can be held for a long time; therefore,
		 * if request mutex is held, we queue the packet in a waitQ
		 * Consequently, we now need to check the waitQ before every
		 * release of the request mutex
		 *
		 * if the waitQ is non-empty, add cmd to waitQ to preserve
		 * some order
		 */
		mutex_enter(IFP_WAITQ_MUTEX(ifp));
		if (placeonwaitQ || ifp->ifp_waitf ||
		    mutex_tryenter(IFP_REQ_MUTEX(ifp)) == 0) {
			IFP_DEBUG(2, (ifp, SCSI_DEBUG,
			"ifp_scsi_start: waitQ pkt 0x%p id %x LUN %x state %x",
			    (void *)pkt, TGT(sp), LUN(sp),
			    target->ifpt_state));
			if (ifp->ifp_waitf == NULL) {
				ifp->ifp_waitb = ifp->ifp_waitf = sp;
				sp->cmd_forw = NULL;
			} else {
				struct ifp_cmd *dp = ifp->ifp_waitb;
				dp->cmd_forw = ifp->ifp_waitb = sp;
				sp->cmd_forw = NULL;
			}

			if (placeonwaitQ == 0 &&
			    mutex_tryenter(IFP_REQ_MUTEX(ifp))) {
				ifp_i_empty_waitQ(ifp);
				mutex_exit(IFP_REQ_MUTEX(ifp));
			}
			mutex_exit(IFP_WAITQ_MUTEX(ifp));
		} else {
			mutex_exit(IFP_WAITQ_MUTEX(ifp));

			rval = ifp_i_start_cmd(ifp, sp);
			if (rval == TRAN_BUSY) {
				/*
				 * put request at the head of the waitQ
				 */
				mutex_enter(IFP_WAITQ_MUTEX(ifp));
				sp->cmd_forw = ifp->ifp_waitf;
				ifp->ifp_waitf = sp;
				if (ifp->ifp_waitb == NULL) {
					ifp->ifp_waitb = sp;
				}
				mutex_exit(IFP_WAITQ_MUTEX(ifp));
				rval = TRAN_ACCEPT;
			}
			IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);
			ifp->ifp_alive = 1;
		}
	} else {
		rval = ifp_i_polled_cmd_start(ifp, sp);
	}
done:
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_WAITQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_START_END, "ifp_scsi_start_end");
	ATRACE(ifp_scsi_start, 0x55555559, pkt);
	return (rval);
}

/*
 * Function name : ifp_scsi_reset()
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
ifp_scsi_reset(struct scsi_address *ap, int level)
{
	int rval = FALSE;
	struct ifp_mbox_cmd mbox_cmd;
	struct ifp *ifp = ADDR2IFP(ap);

	ATRACE(ifp_scsi_reset, 0xeeeeee70, level);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_RESET_START,
	    "ifp_scsi_reset_start");

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());

	/*
	 * hold off new requests, we need the req mutex anyway for mbox cmds.
	 * the waitQ must be empty since the request mutex was free
	 */
	IFP_MUTEX_ENTER(ifp);

	/*
	 * If no space in request queue, return error
	 */
	if (ifp->ifp_queue_space == 0) {
		IFP_UPDATE_QUEUE_SPACE(ifp);

		/*
		 * Check now to see if the queue is still full
		 */
		if (ifp->ifp_queue_space == 0) {
			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			    "ifp_scsi_reset: No space in Queue for Marker"));
			goto fail;
		}
	}

	if (level == RESET_TARGET) {

		ATRACE(ifp_scsi_reset,  0xeeeeee71, ap->a_target);
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "ifp_scsi_reset: reset target %d", ap->a_target));

		ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 3, 3,
		    IFP_MBOX_CMD_ABORT_TARGET, ap->a_target << 8,
		    (ushort_t)(ifp->ifp_scsi_reset_delay/1000),
		    0, 0, 0, 0, 0);
		if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
			if (MBOX_ACCEPTABLE_STATUS(ifp, mbox_cmd.mbox_in[0]))
				rval = TRUE;
			goto fail;
		}

		IFP_MUTEX_EXIT(ifp);
		if (ifp_i_set_marker(ifp, SYNCHRONIZE_TARGET,
		    (short)ap->a_target, 0)) {
			/*
			 * XXX can we do better than fatal error?
			 */
			mutex_enter(IFP_RESP_MUTEX(ifp));
			ifp_i_fatal_error(ifp, 0);
			mutex_enter(IFP_REQ_MUTEX(ifp));
			goto fail;
		}
		IFP_MUTEX_ENTER(ifp);

	} else {
		ATRACE(ifp_scsi_reset,  0xeeeeee72, ifp);
		IFP_DEBUG(3, (ifp, SCSI_DEBUG, "ifp_scsi_reset: reset bus"));

		if (ifp_i_force_lip(ifp))
			goto fail;
	}

	rval = TRUE;

fail:
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_RESET_END,
	    "ifp_scsi_reset_end");

	mutex_exit(IFP_RESP_MUTEX(ifp));
	IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);

	ATRACE(ifp_scsi_reset,  0xeeeeee79, ifp);
	return (rval);
}

/*
 * Function name : ifp_scsi_abort()
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
ifp_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	ushort_t arg, rval = FALSE;
	struct ifp_target *target;
	struct ifp_mbox_cmd mbox_cmd;
	struct ifp *ifp = ADDR2IFP(ap);
	ifp_lun_t *lun_p;

	ATRACE(ifp_scsi_abort,  0xeeeeee80, pkt);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_ABORT_START,
	    "ifp_scsi_abort_start");

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());

	/*
	 * hold off new requests, we need the req mutex anyway for mbox cmds.
	 * the waitQ must be empty since the request mutex was free
	 */
	IFP_MUTEX_ENTER(ifp);

	/*
	 * If no space in request queue, return error
	 */
	if (ifp->ifp_queue_space == 0) {
		IFP_UPDATE_QUEUE_SPACE(ifp);

		/*
		 * Check now to see if the queue is still full
		 */
		if (ifp->ifp_queue_space == 0) {
			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			    "ifp_scsi_abort: No space in Queue for Marker"));
			goto fail;
		}
	}

	if (pkt) {
		struct ifp_cmd *sp = PKT2CMD(pkt);
		lun_p = ADDR2LUN(ap);


		ASSERT(IFP_LOOKUP_ID(sp->cmd_id) == sp);
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "aborting pkt 0x%p", (void *)pkt));

		target = lun_p->ifpl_target;

		arg = ((ushort_t)target->ifpt_loop_id << 8) |
		    ((ushort_t)ap->a_lun);
		ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 4, 1,
		    IFP_MBOX_CMD_ABORT_IOCB, arg, MSW(sp->cmd_id),
		    LSW(sp->cmd_id), 0, 0, 0, 0);
		if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
			goto fail;
		}

		IFP_MUTEX_EXIT(ifp);
	} else {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG, "aborting all pkts"));

		lun_p = ADDR2LUN(ap);
		target = lun_p->ifpl_target;

		arg = ((ushort_t)target->ifpt_loop_id << 8) |
		    ((ushort_t)ap->a_lun);
		ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 2, 1,
		    IFP_MBOX_CMD_ABORT_DEVICE, arg, 0, 0, 0, 0, 0, 0);
		if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
			goto fail;
		}

		IFP_MUTEX_EXIT(ifp);
		if (ifp_i_set_marker(ifp, SYNCHRONIZE_NEXUS,
		    (ushort_t)target->ifpt_loop_id, (ushort_t)ap->a_lun)) {
			/*
			 * XXX can we do better than fatal error?
			 */
			mutex_enter(IFP_RESP_MUTEX(ifp));
			ifp_i_fatal_error(ifp, 0);
			mutex_enter(IFP_REQ_MUTEX(ifp));
			goto fail;
		}
	}

	rval = TRUE;

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_ABORT_END,
	    "ifp_scsi_abort_end");

	mutex_enter(IFP_REQ_MUTEX(ifp));
	IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);

	ATRACE(ifp_scsi_abort,  0xeeeeee89, pkt);
	return (rval);

fail:
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_ABORT_END,
	    "ifp_scsi_abort_end");

	mutex_exit(IFP_RESP_MUTEX(ifp));
	IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);

	ATRACE(ifp_scsi_abort,  0xeeeeee88, pkt);
	return (rval);
}

/*
 * Function name : ifp_scsi_init_pkt
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
ifp_scsi_init_pkt(struct scsi_address *ap, struct scsi_pkt *pkt,
	struct buf *bp, int cmdlen, int statuslen, int tgtlen,
	int flags, int (*callback)(), caddr_t arg)
{
	int kf;
	int failure = 1;
	struct ifp_cmd *sp;
	struct ifp *ifp = ADDR2IFP(ap);
	struct ifp_cmd	*new_cmd = NULL;
	uint64_t	buf_addr;
/* #define	IFP_TEST_ALLOC_EXTERN */
#ifdef IFP_TEST_ALLOC_EXTERN
	cdblen *= 3; statuslen *= 3; tgtlen *= 3;
#endif


	ATRACE(ifp_scsi_init_pkt, 0x88888880, pkt);
	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "ifp_scsi_init_pkt enter pkt=0x%p",
	    (void *)pkt));

	/*
	 * If we've already allocated a pkt once,
	 * this request is for dma allocation only.
	 * Always allocate an extended packet since ARQ is always
	 * enabled.
	 */
	if (pkt == NULL) {
		/*
		 * First step of ifp_scsi_init_pkt:  pkt allocation
		 */
		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_PKTALLOC_START,
		    "ifp_i_scsi_pktalloc_start");

		kf = (callback == SLEEP_FUNC) ? KM_SLEEP: KM_NOSLEEP;
		sp = kmem_cache_alloc(ifp->ifp_kmem_cache, kf);

		if (sp != NULL) {
			int32_t saveid;
			ddi_dma_handle_t savehandle;

			savehandle = sp->cmd_dmahandle;
			saveid = sp->cmd_id;
			bzero(sp, EXTCMDS_SIZE);
			sp->cmd_dmahandle = savehandle;
			sp->cmd_id = saveid;

			pkt = (struct scsi_pkt *)((uchar_t *)sp +
			    sizeof (struct ifp_cmd) + EXTCMDS_STATUS_SIZE);
			sp->cmd_pkt		= pkt;
			pkt->pkt_ha_private	= (opaque_t)sp;
			pkt->pkt_scbp		= (opaque_t)((uchar_t *)sp +
			    sizeof (struct ifp_cmd));
			sp->cmd_cdblen		= cmdlen;
			sp->cmd_scblen		= statuslen;
			sp->cmd_privlen		= tgtlen;
			pkt->pkt_address	= *ap;
			pkt->pkt_cdbp		= (uchar_t *)&sp->cmd_cdb;
			pkt->pkt_private = (opaque_t)sp->cmd_pkt_private;
			failure = 0;
		}
		/*
		 * cleanup or do more allocations
		 */
		if (failure ||
		    (cmdlen > sizeof (sp->cmd_cdb)) ||
		    (tgtlen > PKT_PRIV_LEN) ||
		    (statuslen > EXTCMDS_STATUS_SIZE)) {
			if (failure == 0) {
				failure = ifp_i_pkt_alloc_extern(ifp, sp,
				    cmdlen, tgtlen, statuslen, kf);
			}
			if (failure) {
				TRACE_0(TR_FAC_SCSI_ISP,
				    TR_ISP_SCSI_PKTALLOC_END,
				    "ifp_i_scsi_pktalloc_end (Error)");
				ATRACE(ifp_scsi_init_pkt, 0x88888881, pkt);
				return (NULL);
			}
		}

		new_cmd = sp;
	} else {
		sp = PKT2CMD(pkt);
		new_cmd = NULL;
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_PKTALLOC_END,
	"ifp_i_scsi_pktalloc_end");

DMAGET:
	/*
	 * Second step of ifp_scsi_init_pkt:  dma allocation
	 */
	/*
	 * Here we want to check for CFLAG_DMAVALID because some target
	 * drivers like scdk on x86 can call this routine with
	 * non-zero pkt and without freeing the DMA resources.
	 */
	if (bp && bp->b_bcount != 0 &&
	    (sp->cmd_flags & CFLAG_DMAVALID) == 0) {
		int cmd_flags, dma_flags;
		int rval;
		uint_t dmacookie_count;

		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAGET_START,
		    "ifp_i_scsi_dmaget_start");

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

		/*
		 * workaround for the byte hole problem
		 */
		buf_addr = (uint64_t)bp->b_un.b_addr;
		if ((buf_addr % 8) || (bp->b_bcount % 8)) {
			IFP_DEBUG(1, (ifp, SCSI_DEBUG, "starting address 0x%p "
			    "or count 0x%x is un-aligned, forcing consistent",
			    (void *)buf_addr, (int)bp->b_bcount));
			dma_flags |= DDI_DMA_CONSISTENT;
		}

		ASSERT(sp->cmd_dmahandle != NULL);
		rval = ddi_dma_buf_bind_handle(sp->cmd_dmahandle, bp, dma_flags,
		    callback, arg, &sp->cmd_dmacookie,
		    &dmacookie_count);
dma_failure:
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
				ifp_scsi_destroy_pkt(ap, pkt);
			}
			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			    "ifp_scsi_init_pkt error rval=%x", rval));
			TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAGET_ERROR_END,
			    "ifp_i_scsi_dmaget_end (Error)");
			ATRACE(ifp_scsi_init_pkt, 0x88888882, pkt);
			return ((struct scsi_pkt *)NULL);
		}
		sp->cmd_dmacount = (uint_t)bp->b_bcount;
		sp->cmd_flags = cmd_flags | CFLAG_DMAVALID;

		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAGET_END,
		    "ifp_i_scsi_dmaget_end");
	}

	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "ifp_scsi_init_pkt return pkt=0x%p",
	    (void *)pkt));
	ATRACE(ifp_scsi_init_pkt, 0x88888889, pkt);
	return (pkt);
}

/*
 * Function name : ifp_scsi_destroy_pkt
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
ifp_scsi_destroy_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct ifp *ifp = ADDR2IFP(ap);
	struct ifp_cmd *sp = PKT2CMD(pkt);

	ATRACE(ifp_scsi_destroy_pkt, 0x99999990, pkt);
	/*
	 * ifp_scsi_dmafree inline to make things faster
	 */
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAFREE_START,
	    "ifp_scsi_dmafree_start");
	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_scsi_destroy_pkt: 0x%p target %x lun %x",
	    (void *)pkt, TGT(sp), LUN(sp)));

	if (sp->cmd_flags & CFLAG_DMAVALID) {
		/*
		 * Free the mapping.
		 */
		(void) ddi_dma_unbind_handle(sp->cmd_dmahandle);
		sp->cmd_flags ^= CFLAG_DMAVALID;
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAFREE_END,
	    "ifp_scsi_dmafree_end");

	/*
	 * Free the pkt
	 */
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_PKTFREE_START,
	    "ifp_i_scsi_pktfree_start");

	/*
	 * first test the most common case
	 */
	if ((sp->cmd_flags &
	    (CFLAG_FREE | CFLAG_CDBEXTERN | CFLAG_PRIVEXTERN |
	    CFLAG_SCBEXTERN)) == 0) {
		sp->cmd_flags = CFLAG_FREE;
		kmem_cache_free(ifp->ifp_kmem_cache, sp);
	} else {
		ifp_i_pkt_destroy_extern(ifp, sp);
	}
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_PKTFREE_DONE,
	    "ifp_i_scsi_pktfree_done");

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_PKTFREE_END,
	    "ifp_i_scsi_pktfree_end");
	ATRACE(ifp_scsi_destroy_pkt, 0x99999999, pkt);
}

/*
 * Function name : ifp_scsi_dmafree()
 *
 * Return Values : none
 * Description	 : free dvma resources
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
/*ARGSUSED*/
static void
ifp_scsi_dmafree(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct ifp_cmd *sp = PKT2CMD(pkt);

	ATRACE(ifp_scsi_dmafree, AT_ACT('p', 'k', 't', ' '), pkt);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAFREE_START,
	    "ifp_scsi_dmafree_start");

	if (sp->cmd_flags & CFLAG_DMAVALID) {
		/*
		 * Free the mapping.
		 */
		(void) ddi_dma_unbind_handle(sp->cmd_dmahandle);
		sp->cmd_flags ^= CFLAG_DMAVALID;
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAFREE_END,
	    "ifp_scsi_dmafree_end");

}

/*
 * Function name : ifp_scsi_sync_pkt()
 *
 * Return Values : none
 * Description	 : sync dma
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
/*ARGSUSED*/
static void
ifp_scsi_sync_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	int i;
	struct ifp_cmd *sp = PKT2CMD(pkt);

	ATRACE(ifp_scsi_sync_pkt, AT_ACT('p', 'k', 't', ' '), pkt);
	if (sp->cmd_flags & CFLAG_DMAVALID) {
		i = ddi_dma_sync(sp->cmd_dmahandle, 0, 0,
		    (sp->cmd_flags & CFLAG_DMASEND) ?
		    DDI_DMA_SYNC_FORDEV : DDI_DMA_SYNC_FORCPU);
		if (i != DDI_SUCCESS) {
			ifp_i_log(NULL, CE_WARN, "ifp: sync pkt failed");
		}
	}
}

/*
 * routine for reset notification setup, to register or cancel.
 */
static int
ifp_scsi_reset_notify(struct scsi_address *ap, int flag,
	void (*callback)(caddr_t), caddr_t arg)
{
	struct ifp	*ifp = ADDR2IFP(ap);
	return (scsi_hba_reset_notify_setup(ap, flag, callback, arg,
	    IFP_REQ_MUTEX(ifp), &ifp->ifp_reset_notify_listf));
}

/* ARGSUSED */
static int
ifp_scsi_get_bus_addr(struct scsi_device *sd, char *name, int len)
{
	ifp_lun_t *lun_p = ADDR2LUN(&sd->sd_address);
	ifp_target_t *target;

	ATRACE(ifp_scsi_get_bus_addr, 0x11111170,
	    (sd->sd_address.a_target << 16) | sd->sd_address.a_lun);

	if (lun_p == NULL) {
		ATRACE(ifp_scsi_get_bus_addr, 0x11111178, sd);
		return (0);
	}

	target = lun_p->ifpl_target;
	(void) sprintf(name, "%x", target->ifpt_al_pa);
	ATRACE(ifp_scsi_get_bus_addr, 0x11111179, lun_p);
	return (1);
}

/* ARGSUSED */
static int
ifp_scsi_get_name(struct scsi_device *sd, char *name, int len)
{
	int i;
	dev_info_t *tgt_dip;
	uchar_t wwn[FC_WWN_SIZE];
	char tbuf[(FC_WWN_SIZE*2)+1];

	ATRACE(ifp_scsi_get_name,  0x11111180,
	    (sd->sd_address.a_target << 16) | sd->sd_address.a_lun);
	/* XXX pay attention to len and the first arg */
	tgt_dip = sd->sd_dev;
	i = (int)sizeof (wwn);
	if (ddi_prop_op(DDI_DEV_T_ANY, tgt_dip, PROP_LEN_AND_VAL_BUF,
	    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "port-wwn",
	    (caddr_t)&wwn, &i) != DDI_SUCCESS) {
		name[0] = '\0';
		IFP_DEBUG(2, (SDEV2IFP(sd), SCSI_DEBUG,
		    "ifp_scsi_get_name: no name, target %x lun %x dip 0x%p",
		    sd->sd_address.a_target, sd->sd_address.a_lun,
		    (void *)tgt_dip));
		ATRACE(ifp_scsi_get_name,  0x11111188, sd);
		return (0);
	}
	for (i = 0; i < FC_WWN_SIZE; i++)
		(void) sprintf(&tbuf[i << 1], "%02x", wwn[i]);
	(void) sprintf(name, "w%s,%d", tbuf, sd->sd_address.a_lun);
	IFP_DEBUG(2, (SDEV2IFP(sd), SCSI_DEBUG,
	    "ifp_scsi_get_name: name: %s, target %x lun %x dip 0x%p",
	    name, sd->sd_address.a_target, sd->sd_address.a_lun,
	    (void *)tgt_dip));
	ATRACE(ifp_scsi_get_name,  0x11111189, sd);
	return (1);
}

/*
 * Function name : ifp_i_load_ram()
 *
 * Return Values : 0  on success.
 *		   1 on error.
 *
 * Description	 : Uses the request and response queue iopb memory for dma.
 *		   Copies firmware to iopb memory.
 *		   Sends mbox cmd to IFP to (down) Load RAM.
 *		   Repeats the last 2 steps if the firmware does not fit
 *		   in request and response iopb memory.
 *
 * Context	 : Called only from ifp_i_download_fw(). The same
 *		   requirements hold good for this routine.
 */
static int
ifp_i_load_ram(struct ifp *ifp, ushort_t dest, ushort_t *src, uint_t len)
{
	int rval = 0;
	int count;
	int wcount;
	struct ifp_mbox_cmd mbox_cmd;

	IFP_DEBUG(3, (ifp, SCSI_DEBUG, "Load Ram"));

	/*
	 * copy firmware to iopb memory that was allocated for queues.
	 * Note: dest and src are word (16bit) addresses.
	 */
	while (len) {
		count = (len > IFP_QUEUE_SIZE) ? IFP_QUEUE_SIZE : len;
		wcount = count / (int)sizeof (ushort_t);

		IFP_COPY_OUT_DMA_16(ifp->ifp_dma_acc_handle, src,
		    ifp->ifp_request_base, wcount);

		/*
		 * sync memory
		 */
		(void) ddi_dma_sync(ifp->ifp_dmahandle, (off_t)0, (size_t)count,
		    DDI_DMA_SYNC_FORDEV);

		ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 5, 1, IFP_MBOX_CMD_LOAD_RAM,
		    dest, MSW(ifp->ifp_request_dvma),
		    LSW(ifp->ifp_request_dvma), wcount, 0, 0, 0);
		if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
			ifp_i_log(ifp, CE_WARN, "Load ram failed");
			rval = 1;
			break;
		}

		dest += wcount;
		src += wcount;
		len -= count;
	}

	return (rval);
}

/*
 * Function name : ifp_i_download_fw ()
 *
 * Return Values : 0  on success.
 *		   -1 on error.
 *
 * Description	 : Uses the request and response queue iopb memory for dma.
 *		   Verifies that fw fits in iopb memory.
 *		   Verifies fw checksum.
 *		   Copies firmware to iopb memory.
 *		   Sends mbox cmd to IFP to (down) Load RAM.
 *		   After command is done, resets IFP which starts it
 *			executing from new f/w.
 *
 * Context	 : Can be called ONLY from user context.
 *		 : NOT MT-safe.
 *		 : Driver must be in a quiescent state.
 */
static int
ifp_i_download_fw(struct ifp *ifp)
{
	char *startp;
	int rval = -1;
	int found = 0;
	ushort_t checksum = 0;
	char *string = " Firmware  Version ";
	int fw_len_bytes;
	char buf[10];
	int length;
	int major_rev, minor_rev;
	struct ifp_mbox_cmd mbox_cmd;
	ushort_t i;

	uint16_t risc_addr, *fw_addrp, fw_len;

	risc_addr = ifp_risc_code_addr;

	if (ifp->ifp_chip_id == 0x2200) {
		fw_addrp = ifp_pci_risc_code_2200;
		fw_len = ifp_pci_risc_code_length_2200;
	} else if (ifp->ifp_chip_id == 0x2100) {
		fw_addrp = ifp_pci_risc_code_2100;
		fw_len = ifp_pci_risc_code_length_2100;
	} else {
		ifp_i_log(ifp, CE_WARN, "Chip ID unknown %x",
		    ifp->ifp_chip_id);
		goto fail;
	}

	fw_len_bytes = fw_len * sizeof (uint16_t);

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_download_fw_start: risc=0x%x fw=0x%p, fw_len=0x%x size=0x%x",
	    risc_addr, (void *)fw_addrp, fw_len, (int)IFP_QUEUE_SIZE));

	/*
	 * if download is not necessary just return good status
	 * XXX is this ever the case?
	 */
	if (ifp_download_fw == IFP_DOWNLOAD_FW_OFF)
		goto done;

	/*
	 * verify checksum equals zero
	 */
	for (i = 0; i < fw_len; i++)
		checksum += fw_addrp[i];
	if (checksum) {
		ifp_i_log(ifp, CE_WARN, "Firmware checksum incorrect");
		goto fail;
	}

	/*
	 * get new firmware version numbers
	 */
	startp = (char *)fw_addrp;
	length = fw_len_bytes;
	while (length - strlen(string)) {
		if (strncmp(startp, string, strlen(string)) == 0) {
			found = 1;
			break;
		}
		startp++;
		length --;
	}

	if (found) {
		startp += strlen(string);
		(void) strncpy(buf, startp, 5);
		buf[2] = buf[5] = NULL;
		startp = buf;
		major_rev = stoi(&startp);
		startp++;
		minor_rev = stoi(&startp);

		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "Current f/w: major = %d minor = %d",
		    major_rev, minor_rev));
	} else {
		goto done;
	}

	/*
	 * reset and initialize ifp chip
	 */
	if (ifp_i_reset_init_chip(ifp)) {
		goto fail;
	}

	/*
	 * if we want to download only if we have newer version, we
	 * assume that there is already some firmware in the RAM that
	 * chip can use.
	 *
	 * in case we want to always download, we don't depend on having
	 * anything in the RAM and start from ROM firmware.
	 *
	 */
	if (ifp_download_fw == IFP_DOWNLOAD_FW_IF_NEWER) {
		/*
		 * start IFP Ram firmware up
		 */
		ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 2, 1,
		    IFP_MBOX_CMD_START_FW, risc_addr,
		    0, 0, 0, 0, 0, 0);
		if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
			goto fail;
		}

		/*
		 * get IFP Ram firmware version numbers
		 */
		ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 1, 3,
		    IFP_MBOX_CMD_ABOUT_PROM, 0, 0, 0, 0, 0, 0, 0);
		if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
			goto fail;
		}

		ifp_i_log(ifp, CE_NOTE, "!On-board Firmware Version: v%d.%02d",
		    mbox_cmd.mbox_in[1], mbox_cmd.mbox_in[2]);

		if (major_rev < (int)mbox_cmd.mbox_in[1] ||
		    minor_rev <= (int)mbox_cmd.mbox_in[2]) {
			goto done;
		}

		/*
		 * Send mailbox cmd to stop IFP from executing the Ram
		 * firmware and drop to executing the ROM firmware.
		 */
		IFP_DEBUG(3, (ifp, SCSI_DEBUG, "Stop Firmware"));
		ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 1, 8, IFP_MBOX_CMD_STOP_FW,
		    0, 0, 0, 0, 0, 0, 0);
		if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
			ifp_i_log(ifp, CE_WARN, "Stop firmware failed");
			goto fail;
		}
	}

	if (ifp_i_load_ram(ifp, risc_addr, fw_addrp, fw_len_bytes)) {
		ifp_i_print_state(ifp);
		goto fail;
	}

	/*
	 * reset the IFP chip so it starts with the new firmware
	 */
	IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_RESET);
	drv_usecwait(IFP_CHIP_RESET_BUSY_WAIT_TIME);
	IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_RELEASE);

	/*
	 * Start IFP firmware up.
	 */
	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 2, 1,
	    IFP_MBOX_CMD_START_FW, risc_addr,
	    0, 0, 0, 0, 0, 0);
	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
		goto fail;
	}

	/*
	 * get IFP Ram firmware version numbers
	 */
	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 1, 4,
	    IFP_MBOX_CMD_ABOUT_PROM, 0, 0, 0, 0, 0, 0, 0);
	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
		goto fail;
	}

	ifp->ifp_major_rev = mbox_cmd.mbox_in[1];
	ifp->ifp_minor_rev = mbox_cmd.mbox_in[2];
	ifp->ifp_subminor_rev = mbox_cmd.mbox_in[3];
#ifdef IFPIDENT
	ifp_i_log(ifp, CE_NOTE,
	    "Chip %x rev %x: Firmware rev, %d.%d.%d: Compiled %s",
	    ifp->ifp_chip_id, ifp->ifp_chip_rev,
	    mbox_cmd.mbox_in[1], mbox_cmd.mbox_in[2], mbox_cmd.mbox_in[3],
	    __DATE__);
#endif
done:
	rval = 0;

fail:
	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_i_download_fw: 0x%x 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox0),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox1),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox2),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox3),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox4),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox5)));

	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "ifp_download_fw_end: rval = %d", rval));

	return (rval);
}

/*
 * Function name : ifp_i_commoncap
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
 * ifp_scsi_getcap and ifp_scsi_setcap are wrappers for ifp_i_commoncap.
 */
static int
ifp_i_commoncap(struct scsi_address *ap, char *cap,
    int val, int tgtonly, int doset)
{
	int cidx;
	int rval = FALSE;
	struct ifp *ifp = ADDR2IFP(ap);

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());

	if (cap == NULL) {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG, "ifp_i_commoncap: invalid arg"));
		return (rval);
	}

	cidx = scsi_hba_lookup_capstr(cap);
	if (cidx == -1) {
		return (UNDEFINED);
	}

	IFP_MUTEX_ENTER(ifp);

	/*
	 * Process setcap request.
	 */
	if (doset) {
		/*
		 * At present, we can only set binary (0/1) values
		 */
		switch (cidx) {
		case SCSI_CAP_ARQ:
			/* we cannot turn off ARQ */
			rval = FALSE;
			break;

		default:
			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			    "ifp_i_setcap: unsupported %d", cidx));
			rval = UNDEFINED;
			break;
		}

		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		"set cap: cap=%s,val=0x%x,tgtonly=0x%x,doset=0x%x,rval=%d\n",
		    cap, val, tgtonly, doset, rval));
	/*
	 * Process getcap request.
	 */
	} else {
		switch (cidx) {
		case SCSI_CAP_DMA_MAX:
			rval = (int)dma_ifpattr.dma_attr_maxxfer;
			break;
		case SCSI_CAP_TAGGED_QING:
		case SCSI_CAP_RESET_NOTIFICATION:
			rval = TRUE;
			break;
		case SCSI_CAP_INITIATOR_ID:
			rval = ifp->ifp_my_alpa;
			break;
		case SCSI_CAP_ARQ:
			rval = TRUE;
			break;
		case SCSI_CAP_SCSI_VERSION:
			rval = 3;
			break;
		case SCSI_CAP_INTERCONNECT_TYPE:
			rval = INTERCONNECT_FIBRE;
			break;
		default:
			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			    "ifp_scsi_getcap: unsupported"));
			rval = UNDEFINED;
			break;
		}
		IFP_DEBUG(2, (ifp, SCSI_DEBUG,
		"get cap: cap=%s,val=0x%x,tgtonly=0x%x,doset=0x%x,rval=%d\n",
		    cap, val, tgtonly, doset, rval));
	}
	IFP_MUTEX_EXIT(ifp);

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());

	return (rval);
}

/*
 * Function name : ifp_scsi_getcap(), ifp_scsi_setcap()
 *
 * Return Values : see ifp_i_commoncap()
 * Description	 : wrappers for ifp_i_commoncap()
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
ifp_scsi_getcap(struct scsi_address *ap, char *cap, int whom)
{
	int e;
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_GETCAP_START,
	    "ifp_scsi_getcap_start");
	e = ifp_i_commoncap(ap, cap, 0, whom, 0);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_GETCAP_END,
	    "ifp_scsi_getcap_end");
	return (e);
}

static int
ifp_scsi_setcap(struct scsi_address *ap, char *cap, int value, int whom)
{
	int e;
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_SETCAP_START,
	    "ifp_scsi_setcap_start");
	e = ifp_i_commoncap(ap, cap, value, whom, 1);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_SETCAP_END,
	    "ifp_scsi_setcap_end");
	return (e);
}

/*
 * Issue an rls
 */
static int
ifp_i_issue_rls(ifp_t *ifp, uchar_t loopid, struct rls_payload *rlsp)
{
	uint_t count;
	int bound = 0;
	int payloadsz;
	char *payload;
	int alloced = 0;
	int rval = ENOMEM;
	ddi_dma_handle_t rls_handle;
	struct ifp_mbox_cmd mbox_cmd;
	ddi_dma_cookie_t rls_dmacookie;

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	if (ddi_dma_alloc_handle(ifp->ifp_dip, &dma_ifpattr,
	    DDI_DMA_SLEEP, NULL, &rls_handle) != DDI_SUCCESS) {
		goto fail;
	}
	alloced++;

	payload = (char *)&rlsp->rls_linkfail;
	payloadsz = sizeof (*rlsp) - sizeof (rlsp->rls_portno);

	if (ddi_dma_addr_bind_handle(rls_handle, NULL, payload,
	    (size_t)payloadsz, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, NULL, &rls_dmacookie, &count)
	    != DDI_DMA_MAPPED) {
		goto fail;
	}
	bound++;

	rval = 0;

	/*
	 * Issue rls
	 */
	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 8, 1,
	    IFP_MBOX_CMD_GET_LINK_STATUS, loopid << 8,
	    MSW(rls_dmacookie.dmac_address),
	    LSW(rls_dmacookie.dmac_address), 0, 0, 0, 0);
	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
		rval = ENXIO;
	} else {
		(void) ddi_dma_sync(rls_handle, (off_t)0, (size_t)0,
		    DDI_DMA_SYNC_FORCPU);
		ifp_i_rls_le_to_be(payload, payloadsz);

#ifdef IFPDEBUG
		if (ifpdebug > 1) {
			char buf[256];
			(void) sprintf(buf, "rls for loopid %x\n", loopid);
			ifp_i_dump_mem(buf, (void *)rlsp, sizeof (*rlsp));
		}
#endif
	}

fail:
	if (bound)
		(void) ddi_dma_unbind_handle(rls_handle);
	if (alloced)
		ddi_dma_free_handle(&rls_handle);

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	return (rval);
}

/*
 * Issue a diagnostic loopback command.
 */
static int
ifp_i_issue_flb(ifp_t *ifp, char *flb_out, char *flb_in,
		ifp_lb_frame_cmd_t *lb_cmd)
{
	int rval = ENOMEM;
	ddi_dma_handle_t flb_out_handle;
	ddi_dma_handle_t flb_in_handle;
	struct ifp_mbox_cmd mbox_cmd;
	ddi_dma_cookie_t flb_out_dmacookie;
	ddi_dma_cookie_t flb_in_dmacookie;
	uint_t count;
	uint_t alloced_o = 0;
	uint_t bound_o = 0;
	uint_t alloced_i = 0;
	uint_t bound_i = 0;

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	/* only the 2200 can do loopback */
	if (ifp->ifp_chip_id != 0x2200) {
		return (EFAULT);
	}

	if (ddi_dma_alloc_handle(ifp->ifp_dip, &dma_ifpattr,
	    DDI_DMA_SLEEP, NULL, &flb_out_handle) != DDI_SUCCESS) {
		goto fail;
	}
	alloced_o++;
	if (ddi_dma_alloc_handle(ifp->ifp_dip, &dma_ifpattr,
	    DDI_DMA_SLEEP, NULL, &flb_in_handle) != DDI_SUCCESS) {
		goto fail;
	}
	alloced_i++;

	if (ddi_dma_addr_bind_handle(flb_out_handle, NULL, flb_out,
	    (size_t)lb_cmd->xfer_cnt,
	    DDI_DMA_WRITE | DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, NULL, &flb_out_dmacookie, &count)
	    != DDI_DMA_MAPPED) {
		goto fail;
	}
	bound_o++;
	if (ddi_dma_addr_bind_handle(flb_in_handle, NULL, flb_in,
	    (size_t)lb_cmd->xfer_cnt,
	    DDI_DMA_READ | DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, NULL, &flb_in_dmacookie, &count)
	    != DDI_DMA_MAPPED) {
		goto fail;
	}
	bound_i++;

	rval = 0;

	/*
	 * Create loopback command.
	 */
	/* timeout is iteration count divided by 128 but must be > 0 */
	mbox_cmd.timeout	= (lb_cmd->iter_cnt >> 7) +1;
	mbox_cmd.retry_cnt	= IFP_MBOX_CMD_RETRY_CNT;
	mbox_cmd.n_mbox_out	= 20;
	mbox_cmd.n_mbox_in	= 20;
	mbox_cmd.mbox_in[REG0] = 0xffff;

	mbox_cmd.mbox_out[REG0] = IFP_MBOX_CMD_LOOPBACK;
	mbox_cmd.mbox_out[REG1] = lb_cmd->options;
	mbox_cmd.mbox_out[REG2] = 0;
	mbox_cmd.mbox_out[REG3] = 0;
	mbox_cmd.mbox_out[REG10] = lb_cmd->xfer_cnt & 0xffff;
	mbox_cmd.mbox_out[REG11] = lb_cmd->xfer_cnt >> 16;
	mbox_cmd.mbox_out[REG12] = 0;
	mbox_cmd.mbox_out[REG13] = 0;
	mbox_cmd.mbox_out[REG14] = LSW(flb_out_dmacookie.dmac_address);
	mbox_cmd.mbox_out[REG15] = MSW(flb_out_dmacookie.dmac_address);
	mbox_cmd.mbox_out[REG16] = LSW(flb_in_dmacookie.dmac_address);
	mbox_cmd.mbox_out[REG17] = MSW(flb_in_dmacookie.dmac_address);
	mbox_cmd.mbox_out[REG18] = lb_cmd->iter_cnt & 0xffff;
	mbox_cmd.mbox_out[REG19] = (lb_cmd->iter_cnt >> 16) & 0xffff;

	rval = 0;
	/*
	 * Issue loopback command.
	 */
	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd) == -2) {
		rval = ENXIO;
	} else {
		int i;
		i = ddi_dma_sync(flb_in_handle, (off_t)0, (size_t)0,
		    DDI_DMA_SYNC_FORCPU);
		if (i != DDI_SUCCESS) {
			ifp_i_log(NULL, CE_WARN,
			    "ifp: ERROR, sync loopback buffer failed");
		}
	}

	lb_cmd->status = mbox_cmd.mbox_in[0];
	lb_cmd->crc_cnt = mbox_cmd.mbox_in[1];
	lb_cmd->disparity_cnt = mbox_cmd.mbox_in[2];
	lb_cmd->frame_len_err_cnt = mbox_cmd.mbox_in[3];
	lb_cmd->fail_iter_cnt = mbox_cmd.mbox_in[18];
	lb_cmd->fail_iter_cnt = (mbox_cmd.mbox_in[19] << 16);

fail:
	if (bound_o)
		(void) ddi_dma_unbind_handle(flb_out_handle);
	if (bound_i)
		(void) ddi_dma_unbind_handle(flb_in_handle);
	if (alloced_o)
		ddi_dma_free_handle(&flb_out_handle);
	if (alloced_i)
		ddi_dma_free_handle(&flb_in_handle);


	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	return (rval);
}

/*
 * Function name : ifp_i_force_lip()
 *
 * Return Values : -1	failed.
 *		    0	success
 *
 * Description	 : sync's the ifp target parameters with the desired
 *		   ifp_caps for the specified target range
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
ifp_i_force_lip(struct ifp *ifp)
{
	int rval = 0;
	struct ifp_mbox_cmd mbox_cmd;

	/*
	 * if we are already handling a LIP, don't bother issuing
	 * another one. Don't issue the LIP if the loop is not up, either.
	 */
	if (ifp->ifp_lip_state & IFPL_HANDLING_LIP)
		return (rval);

	/*
	 * We don't want to check the state of the ifp while issuing lip.
	 * Simply force a lip whatever be the ifp state.
	 */
#if 0
	if (ifp->ifp_state != IFP_STATE_ONLINE)
		return (rval);
#endif

	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 1,
	    1, IFP_MBOX_CMD_ISSUE_LIP_WITH_PLOGI, 0, 0, 0,
	    0, 0, 0, 0);

	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
		if (mbox_cmd.mbox_in[0] != IFP_MBOX_STATUS_LOOP_DOWN)
			rval = -1;
	}

	return (rval);
}

/*
 * (de)allocator for non-std size cdb/pkt_private/status
 */
/*ARGSUSED*/
static int
ifp_i_pkt_alloc_extern(struct ifp *ifp, struct ifp_cmd *sp,
	int cmdlen, int tgtlen, int statuslen, int kf)
{
	int failure = 0;
	caddr_t cdbp, scbp, tgt;
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
		ifp_i_pkt_destroy_extern(ifp, sp);
	}
	return (failure);
}

static void
ifp_i_pkt_destroy_extern(struct ifp *ifp, struct ifp_cmd *sp)
{
	struct scsi_pkt *pkt = CMD2PKT(sp);

	if (sp->cmd_flags & CFLAG_FREE) {
		ifp_i_log(ifp, CE_PANIC,
		    "ifp_scsi_impl_pktfree: freeing free packet");
		_NOTE(NOT_REACHED)
		/* NOTREACHED */
	}
	if (sp->cmd_flags & CFLAG_CDBEXTERN) {
		kmem_free(pkt->pkt_cdbp, sp->cmd_cdblen);
	}
	if (sp->cmd_flags & CFLAG_SCBEXTERN) {
		kmem_free(pkt->pkt_scbp, sp->cmd_scblen);
	}
	if (sp->cmd_flags & CFLAG_PRIVEXTERN) {
		kmem_free(pkt->pkt_private, sp->cmd_privlen);
	}

	sp->cmd_flags = CFLAG_FREE;
	kmem_cache_free(ifp->ifp_kmem_cache, sp);
}

/*
 * the waitQ is used when the request mutex is held. requests will go
 * in the waitQ which will be emptied just before releasing the request
 * mutex; the waitQ reduces the contention on the request mutex significantly
 *
 * Note that the waitq mutex is released *after* the request mutex; this
 * closes a small window where we empty the waitQ but before releasing
 * the request mutex, the waitQ is filled again. ifp_scsi_start will
 * attempt to get the request mutex after adding the cmd to the waitQ
 * which ensures that after the waitQ is always emptied.
 */
#define	IFP_CHECK_WAITQ_TIMEOUT(ifp)					\
	if (ifp->ifp_waitq_timeout == 0) {				\
		ifp->ifp_waitq_timeout = timeout(ifp_i_check_waitQ,	\
		    (caddr_t)ifp, drv_usectohz((clock_t)1000000));	\
	}

static void
ifp_i_check_waitQ(void *arg)
{
	struct ifp *ifp = arg;

	mutex_enter(IFP_REQ_MUTEX(ifp));
	mutex_enter(IFP_WAITQ_MUTEX(ifp));
	ifp->ifp_waitq_timeout = 0;
	ifp_i_empty_waitQ(ifp);
	mutex_exit(IFP_REQ_MUTEX(ifp));
	if (ifp->ifp_waitf) {
		IFP_CHECK_WAITQ_TIMEOUT(ifp);
	}
	mutex_exit(IFP_WAITQ_MUTEX(ifp));
}

/*
 * Function name : ifp_i_empty_waitQ()
 *
 * Return Values : none
 *
 * Description	 : empties the waitQ
 *		   copies the head of the queue and zeroes the waitQ
 *		   calls ifp_i_start_cmd() for each packet
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
ifp_i_empty_waitQ(struct ifp *ifp)
{
	int rval;
	struct ifp_cmd *sp, *head, *tail, *busycmdshead, *busycmdstail;
	clock_t local_lbolt;

	ATRACE(ifp_i_empty_waitQ, 0xdddddd50, ifp);
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0);
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_WAITQ_MUTEX(ifp)));

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_EMPTY_WAITQ_START,
	    "ifp_i_empty_waitQ_start");

	busycmdshead = busycmdstail = NULL;

again:
	local_lbolt = ddi_get_lbolt();

	/*
	 * walk thru the waitQ and attempt to start the cmd
	 */
	while (ifp->ifp_waitf) {
		/*
		 * copy queue head, clear wait queue and release WAITQ_MUTEX
		 */
		head = ifp->ifp_waitf;
		tail = ifp->ifp_waitb;
		ifp->ifp_waitf = ifp->ifp_waitb = NULL;

		mutex_exit(IFP_WAITQ_MUTEX(ifp));

		while (head) {
			sp = head;
			head = sp->cmd_forw;
			sp->cmd_forw = NULL;

			if ((rval = ifp_i_start_cmd(ifp, sp)) != TRAN_ACCEPT) {
				IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		"ifp_i_empty_waitQ: transport failed rval %x id %x lun %x",
				    rval, TGT(sp), LUN(sp)));

				/*
				 * When rval == TRAN_BUSY (request queue couldnt
				 * take anymore cmds or target is marked busy)
				 * and our command hasn't expired yet isolate
				 * the command on our busy queue.
				 *
				 * If we took a transport error or the command
				 * timed out complete it and mark it
				 * appropriately.
				 */
				if ((rval == TRAN_BUSY) && \
				    (sp->cmd_deadline >= local_lbolt)) {
					if (!busycmdstail) {
						busycmdstail = sp;
					}

					sp->cmd_forw = busycmdshead;
					busycmdshead = sp;
				}

				mutex_enter(IFP_WAITQ_MUTEX(ifp));
				if (head) {
					if (ifp->ifp_waitf) {
						tail->cmd_forw = ifp->ifp_waitf;
						ifp->ifp_waitf = head;
					} else {
						ifp->ifp_waitf = head;
						ifp->ifp_waitb = tail;
					}
				}

				if ((rval != TRAN_BUSY) || \
				    (sp->cmd_deadline < local_lbolt)) {
					struct scsi_pkt *pkt = CMD2PKT(sp);

					pkt->pkt_reason = CMD_INCOMPLETE;

					if (sp->cmd_deadline < local_lbolt) {
						pkt->pkt_statistics =
						    STAT_TIMEOUT;
					} else {
						pkt->pkt_statistics =
						    STAT_BUS_RESET;
					}

					if (pkt->pkt_comp) {
#ifdef IFPDEBUG
						sp->cmd_flags |= CFLAG_FINISHED;
#endif
						sp->cmd_flags = (sp->cmd_flags &
						    ~CFLAG_IN_TRANSPORT) |
						    CFLAG_COMPLETED;

						ATRACE(ifp_i_empty_waitQ,
						    0xdddddd51, pkt);
						mutex_exit(
						    IFP_WAITQ_MUTEX(ifp));
						mutex_exit(IFP_REQ_MUTEX(ifp));
						scsi_hba_pkt_comp(pkt);
						mutex_enter(IFP_REQ_MUTEX(ifp));
						mutex_enter(
						    IFP_WAITQ_MUTEX(ifp));
					}
				}
				goto again;
			}
		}
		mutex_enter(IFP_WAITQ_MUTEX(ifp));
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_EMPTY_WAITQ_END,
	    "ifp_i_empty_waitQ_end");

	/*
	 * Upon exit check our busy commands queue and
	 * if there any put them back on the waitQ
	 */
	if (busycmdshead) {
		ifp->ifp_waitf = busycmdshead;
		ifp->ifp_waitb = busycmdstail;
	}

	if (ifp->ifp_waitf) {
		IFP_CHECK_WAITQ_TIMEOUT(ifp);
	}

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_WAITQ_MUTEX(ifp)));
	ATRACE(ifp_i_empty_waitQ, 0xdddddd59, ifp);
}

/*
 * ifp_i_ok_to_issue_cmd()
 */

static int
ifp_i_ok_to_issue_cmd(struct ifp *ifp, struct ifp_target *target)
{
	int rval = TRAN_ACCEPT;

	if (ifp->ifp_shutdown)
		return (TRAN_FATAL_ERROR);

	/*
	 * If we are handling a LIP, target could come back
	 * online after a little while. Return TRAN_BUSY to
	 * give the target driver a chance to retry.
	 */
	if (target->ifpt_state & (IFPT_TARGET_BUSY|IFPT_TARGET_OFFLINE)) {
		if (target->ifpt_state & IFPT_TARGET_BUSY)
			return (TRAN_BUSY);
		else
			return (TRAN_FATAL_ERROR);
	}

	/*
	 * Check to see how much space is available in the
	 * Request Queue, save this so we do not have to do
	 * a lot of PIOs
	 */
	if (ifp->ifp_queue_space < 2) {
		IFP_UPDATE_QUEUE_SPACE(ifp);

		/*
		 * Check now to see if the queue is still full
		 * Report TRAN_BUSY if we are full
		 */
		if (ifp->ifp_queue_space < 2) {
			TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_START_CMD_Q_FULL_END,
			    "ifp_i_start_cmd_end (Queue Full)");
			return (TRAN_BUSY);
		}
	}
	return (rval);
}

/*
 * Function name : ifp_i_start_cmd()
 *
 * Return Values : TRAN_ACCEPT	- request is in the ifp request queue
 *		   TRAN_BUSY	- request queue is full
 *		   TRAN_FATAL_ERROR - something terrible is going on
 *
 * Description	 : if there is space in the request queue, copy over request
 *		   enter normal requests in the ifp_slots list
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
ifp_i_start_cmd(struct ifp *ifp, struct ifp_cmd *sp)
{
	int rval;
	ushort_t slot;
	struct ifp_request *req;
	struct scsi_pkt *pkt = CMD2PKT(sp);
	ifp_lun_t *lun_p = CMD2LUN(sp);
	ifp_target_t *target = lun_p->ifpl_target;

	ATRACE(ifp_i_start_cmd, 0xaaaaaaa0, pkt);
	ATRACE(ifp_i_start_cmd, 0xaaaaaaa0, lun_p);
	ATRACE(ifp_i_start_cmd, 0xaaaaaaa0, target);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_START_CMD_START,
	    "ifp_i_start_cmd_start");

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_WAITQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(IFP_LOOKUP_ID(sp->cmd_id) == sp);



	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "ifp_i_start_cmd: sp=0x%p, pkt_time=%x",
	    (void *)sp, pkt->pkt_time));

	if ((rval = ifp_i_ok_to_issue_cmd(ifp, target)) != TRAN_ACCEPT) {
		ATRACE(ifp_i_start_cmd, 0xaaaaaaa1, rval);
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "ifp_start_cmd: not OK to issue pkt 0x%p id %x LUN %x",
		    (void *)pkt, TGT(sp), LUN(sp)));
		goto done;
	}

	/*
	 * Find a free slot to store the pkt in for crash protection for
	 * non-polled commands. Polled commands do not have to be kept
	 * track of since the busy wait loops keep track of them.
	 */
	if ((pkt->pkt_flags & FLAG_NOINTR) == 0) {
		ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
		slot = ifp->ifp_free_slot++;
		if (ifp->ifp_free_slot >= (ushort_t)IFP_MAX_SLOTS) {
			ifp->ifp_free_slot = 0;
		}
		if (ifp->ifp_slots[slot].slot_cmd) {
			slot = ifp_i_find_freeslot(ifp);
		}

		if (slot == 0xffff) {
			rval = TRAN_BUSY;
			goto done;
		}

		sp->cmd_slot = slot;

		ifp->ifp_slots[slot].slot_deadline = sp->cmd_deadline;
		ifp->ifp_slots[slot].slot_cmd = sp;
	}

	/*
	 * Put I/O request in ifp request queue to run.
	 * Get the next request in pointer.
	 */
	IFP_GET_NEXT_REQUEST_IN(ifp, req);

	/*
	 * Copy 48 of the  64 byte request into the request queue
	 * (only 1 data seg)
	 */
	IFP_COPY_OUT_REQ(ifp->ifp_dma_acc_handle, &sp->cmd_ifp_request, req);

	/*
	 * Use correct offset and size for syncing based on using
	 * ifp->ifp_request_in - 1 as macro above increments this pointer
	 */
	(void) ddi_dma_sync(ifp->ifp_dmahandle,
	    (off_t)(((ifp->ifp_request_in == 0) ? (IFP_MAX_REQUESTS - 1) :
	    (ifp->ifp_request_in - 1)) * sizeof (struct ifp_request)),
	    (size_t)sizeof (struct ifp_request),
	    DDI_DMA_SYNC_FORDEV);

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_START_CMD_AFTER_SYNC,
	    "ifp_i_start_cmd_after_sync");

	/*
	 * Tell ifp it's got a new I/O request...
	 */
	IFP_SET_REQUEST_IN(ifp);
	ifp->ifp_queue_space--;

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_START_CMD_END,
	    "ifp_i_start_cmd_end");

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_WAITQ_MUTEX(ifp)) == 0 || ddi_in_panic());
done:
	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "ifp_i_start_cmd end: sp=0x%p, rval=%d",
	    (void *)sp, rval));

	ATRACE(ifp_i_start_cmd, 0xaaaaaaa9, pkt);
	return (rval);
}

static uint_t
ifp_intr(caddr_t arg)
{
	int stat;
	int disp;
	int aen_lips;
	int aen_resets;
	struct ifp_cmd *sp;
	struct ifp *ifp = (struct ifp *)arg;
	struct ifp_cmd_ptrs normalcmds, retrycmds;


	ATRACE(ifp_intr, 0xbbbbbbb0, ifp);
	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_intr entry, ifp 0x%p", (void *)ifp));

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_START, "ifp_intr_start");

	mutex_enter(IFP_RESP_MUTEX(ifp));

	if (IFP_INT_PENDING(ifp) == 0) {
		if (ifp->ifp_polled_intr) {
			ifp->ifp_polled_intr = 0;
#ifdef IFP_PERF
			ifp->ifp_intr_count++;
#endif
			stat = DDI_INTR_CLAIMED;
		} else {
			stat = DDI_INTR_UNCLAIMED;
		}
		mutex_exit(IFP_RESP_MUTEX(ifp));
		ATRACE(ifp_intr, 0xbbbbbbb1, stat);
		return (stat);
	}

#ifdef IFP_PERF
	ifp->ifp_intr_count++;
#endif
	if (ifp->ifp_que_inited == 0 &&
	    (IFP_CHECK_SEMAPHORE_LOCK(ifp) == 0)) {
		IFP_CLEAR_RISC_INT(ifp);
		mutex_exit(IFP_RESP_MUTEX(ifp));
		ATRACE(ifp_intr, 0xbbbbbbb2, ifp);
		return (DDI_INTR_CLAIMED);
	}

	aen_lips = aen_resets = 0;
	normalcmds.head = normalcmds.tail = (struct ifp_cmd *)NULL;
	retrycmds.head = retrycmds.tail = (struct ifp_cmd *)NULL;

	do {

		ifp->ifp_alive = 1;

		/*
		 * Note on async events:
		 *   ISP2100 fw reports LIP OCCURED when the chip sees
		 *   a LIP and and acquires the AL_PA. After reporting
		 *   this, the fw logs in with all the ports, updates
		 *   its port db etc. and then posts a LOOP UP async event.
		 *   This event tells the driver that it can go ahead &
		 *   issue commands that go out on the loop. A LOOP DOWN
		 *   is reported on loss of sync, loop initialization
		 *   errors etc.
		 *
		 *   At fw initialization time, the fw reports a LIP OCCURED
		 *   followed by LOOP UP. At other times, when a LIP
		 *   occurs on the loop, only LIP OCCURED is reported.
		 *   So, our processing of LIP event depends on whether
		 *   the state is OFFLINE or not. If it is OFFLINE,
		 *   we need a LOOP UP before calling ifp_i_handle_lip().
		 */

		if (IFP_CHECK_SEMAPHORE_LOCK(ifp)) {
#ifdef IFPDEBUG
			if (ifp_mbox_debug) {
				ifp_i_log(ifp, CE_NOTE, "ifp_intr:RISC intr!");
				ifp_i_print_state(ifp);
			}
#endif
			disp = ifp_i_handle_mbox_return(ifp, &sp);

			if (disp == IFP_AEN_NOT_AN_AEN) {
				IFP_CLEAR_RISC_INT(ifp);
			} else if (disp == IFP_AEN_DMA_ERR ||
			    disp == IFP_AEN_SYS_ERR) {

				/*
				 * If there is a race and a target driver issues
				 * a reset (mbox) command before we are able to
				 * download the fw and get things going again,
				 * it will see a bad completion status and
				 * probably will escalate by resetting the
				 * board. We can avoid this by setting
				 * ifp_handling_fatal_aen. This flag will be
				 * cleared when the waters are safe again to
				 * issue mbox commands.
				 */
				ifp->ifp_handling_fatal_aen++;
				ifp_i_handle_fatal_errors(ifp, disp,
				    &normalcmds, &retrycmds);
				mutex_exit(IFP_RESP_MUTEX(ifp));
				goto bailout;
			} else if (disp == IFP_AEN_FAST_POST) {
				if (normalcmds.head) {
					normalcmds.tail->cmd_forw = sp;
					normalcmds.tail = sp;
				} else {
					normalcmds.head =
					    normalcmds.tail = sp;
				}
			} else if (disp == IFP_AEN_LIP) {
				aen_lips++;
			} else if (disp == IFP_AEN_RESET) {
				aen_resets++;
			}
		} else {
			ifp->ifp_response_in = IFP_GET_RESPONSE_IN(ifp);

			IFP_CLEAR_RISC_INT(ifp);

			if (ifp->ifp_que_inited && ifp->ifp_running_diags == 0)
				ifp_i_gather_completions(ifp, &normalcmds,
				    &retrycmds);
		}
	} while (IFP_INT_PENDING(ifp));

	if (aen_resets) {
		if (ifp_i_handle_resets(ifp)) {
			mutex_exit(IFP_RESP_MUTEX(ifp));
			if (normalcmds.head)
				ifp_i_call_pkt_comp(normalcmds.head);
			if (retrycmds.head)
				ifp_i_call_bad_pkt_comp(retrycmds.head);
			mutex_enter(IFP_RESP_MUTEX(ifp));
			ifp_i_fatal_error(ifp, 0);
			mutex_exit(IFP_RESP_MUTEX(ifp));
			goto bailout;
		}
	}

	if (aen_lips) {
		if (ifp_i_process_lips(ifp)) {
			mutex_exit(IFP_RESP_MUTEX(ifp));
			if (normalcmds.head)
				ifp_i_call_pkt_comp(normalcmds.head);
			if (retrycmds.head)
				ifp_i_call_bad_pkt_comp(retrycmds.head);
			mutex_enter(IFP_RESP_MUTEX(ifp));
			ifp_i_fatal_error(ifp, 0);
			mutex_exit(IFP_RESP_MUTEX(ifp));
			goto bailout;
		}
	}

	mutex_exit(IFP_RESP_MUTEX(ifp));

	if (retrycmds.head) {

		/* put them on waitQ list */
		mutex_enter(IFP_WAITQ_MUTEX(ifp));
		if (ifp->ifp_waitf) {
			ifp->ifp_waitb->cmd_forw = retrycmds.head;
		} else {
			ifp->ifp_waitf = retrycmds.head;
		}
		ifp->ifp_waitb = retrycmds.tail;
		mutex_exit(IFP_WAITQ_MUTEX(ifp));
	}

	if (normalcmds.head) {
		ifp_i_call_pkt_comp(normalcmds.head);
	}

bailout:
	/* flushing waitQ requires that req mutex be held */
	mutex_enter(IFP_REQ_MUTEX(ifp));
	IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_END, "ifp_intr_end");
	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "ifp_intr end"));

	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0);
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0);
	ATRACE(ifp_intr, 0xbbbbbbb9, ifp);


	return (DDI_INTR_CLAIMED);
}

static void
ifp_i_gather_completions(struct ifp *ifp, struct ifp_cmd_ptrs *good,
	struct ifp_cmd_ptrs *retry)
{
	int n;
	off_t offset;
	uint_t sync_size;
	ushort_t cq_flags;
	struct ifp_cmd *sp;
	ushort_t response_in;
	struct ifp_slot *ifp_slot;
	struct ifp_response *resp;
	struct cq_header resp_header;

	ATRACE(ifp_i_gather_completions, 0xccccccc0, ifp);
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0);

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_i_gather_comp: good 0x%p retry 0x%p",
	    (void *)good, (void *)retry));

	/*
	 * Loop through completion response queue and collect them
	 * all. Good ones go into good list, retryables go
	 * to retry list.
	 *
	 * To conserve PIO's, we only update the ifp response
	 * out index after draining it.
	 */
	response_in = ifp->ifp_response_in;
	ASSERT(!(response_in >= IFP_MAX_RESPONSES));

	if (ifp->ifp_response_in == ifp->ifp_response_out) {
		ATRACE(ifp_i_gather_completions, 0xccccccc1, ifp);
		return;
	}
	mutex_enter(IFP_REQ_MUTEX(ifp));
	/*
	 * Calculate how many requests there are in the queue
	 * If this is < 0, then we are wrapping around
	 * and syncing the packets need two separate syncs
	 */
	n = response_in - ifp->ifp_response_out;
	ASSERT(n);
	offset = (off_t)((IFP_MAX_REQUESTS *
	    sizeof (struct ifp_request)) +
	    (ifp->ifp_response_out *
	    sizeof (struct ifp_response)));

	if (n == 1) {
		sync_size = ((uint_t)sizeof (struct ifp_response));
	} else if (n > 0) {
		sync_size =
		    n * ((uint_t)sizeof (struct ifp_response));
	} else if (n < 0) {
		sync_size =
		    (IFP_MAX_RESPONSES - ifp->ifp_response_out) *
		    (uint_t)sizeof (struct ifp_response);

		/*
		 * we wrapped around and need an extra sync
		 */
		(void) ddi_dma_sync(ifp->ifp_dmahandle,
		    (off_t)(IFP_MAX_REQUESTS * sizeof (struct ifp_request)),
		    response_in * sizeof (struct ifp_response),
		    DDI_DMA_SYNC_FORKERNEL);

		n = IFP_MAX_RESPONSES - ifp->ifp_response_out +
		    response_in;
	}

	(void) ddi_dma_sync(ifp->ifp_dmahandle,
	    (off_t)offset, sync_size, DDI_DMA_SYNC_FORKERNEL);
	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "sync: n=%d, in=%d, out=%d, offset=%d, size=%d\n",
	    n, response_in,
	    ifp->ifp_response_out, (int)offset, sync_size));

	while (n-- > 0) {
#ifdef STE_TARGET_MODE
		/*
		 * If this HBA is set up for Target Mode,
		 * go process the event.
		 */
		if (ifp->ifp_tm_hba_event) {
			ifp->ifp_tm_hba_event(ifp);
			continue;
		}
#endif /* STE_TARGET_MODE */

		IFP_GET_NEXT_RESPONSE_OUT(ifp, resp);
#ifdef IFPDEBUG
		if (resp == NULL) {
			ifp_i_log(ifp, CE_WARN, "resp is NULL");
			ifp_i_print_state(ifp);
		}
#endif
		ASSERT(resp != NULL);

		/*
		 * copy over the response header
		 */
		IFP_COPY_IN_RESP_HDR(ifp->ifp_dma_acc_handle, resp,
		    &resp_header);

		if (IFP_RESPONSE_STATUS_CONTINUATION(resp_header) == 0) {

			uint32_t rptr;

			IFP_COPY_IN_TOKEN(ifp->ifp_dma_acc_handle, resp, &rptr);
			sp = IFP_LOOKUP_ID(rptr);
#ifdef IFPDEBUG
			if (rptr == NULL) {
				ifp_i_log(ifp, CE_WARN, "NULL token; resp 0x%p",
				    (void *)resp);
				ifp_i_print_state(ifp);
			}
			ASSERT(sp != NULL);

			if ((sp->cmd_flags & CFLAG_COMPLETED) != 0) {
				ifp_i_log(ifp, CE_WARN,
				    "cmd_flags comp != 0;sp 0x%p resp 0x%p",
				    (void *)sp, (void *)resp);
				ifp_i_print_state(ifp);
				debug_enter("completed != 0");
			}
			if ((sp->cmd_flags & CFLAG_FINISHED) != 0) {
				ifp_i_log(ifp, CE_WARN,
				    "cmd_flags finish != 0;sp 0x%p resp 0x%p",
				    (void *)sp, (void *)resp);
				ifp_i_print_state(ifp);
				debug_enter("finish != 0");
			}
			ASSERT((sp->cmd_flags & CFLAG_COMPLETED) == 0);
			ASSERT((sp->cmd_flags & CFLAG_FINISHED) == 0);
			sp->cmd_flags |= CFLAG_FINISHED;
			IFP_DEBUG(2, (ifp, SCSI_DEBUG,
			    "ifp_intr 0x%p done, pkt_time=%x", (void *)sp,
			    CMD2PKT(sp)->pkt_time));
#endif
			/*
			 * copy over rest of the response packet in sp
			 */
			IFP_COPY_IN_RESP(ifp->ifp_dma_acc_handle, resp,
			    &sp->cmd_ifp_response);

			/*
			 * Paranoia:  This should never happen.
			 * Should we take it out of ifp_slots even if invalid
			 * type???
			 */
			if (IFP_IS_RESPONSE_INVALID(resp_header)) {
#ifdef IFPDEBUG
				char buf[128];
				(void) sprintf(buf,
				    "invalid response:resp 0x%p",
				    (void *)&sp->cmd_ifp_response);
				if (ifp_report_invalid_response)
					debug_enter(buf);
#endif
				ifp_i_log(ifp, CE_WARN,
				    "invalid resp:in=%x, out=%x, mbx5=%x, "
				    "type=%x",
				    ifp->ifp_response_in,
				    ifp->ifp_response_out,
				    IFP_READ_MBOX_REG(ifp,
				    &ifp->ifp_biu_reg->ifp_mailbox5),
				    resp_header.cq_entry_type);
				continue;
			}

			sp->cmd_forw = NULL;
		}

		/*
		 * Check response header flags.
		 */
		cq_flags = resp_header.cq_flags;

		if (cq_flags & CQ_FLAG_ERR_MASK) {
			if (cq_flags & CQ_FLAG_FULL) {
				if (retry->head) {
					retry->tail->cmd_forw = sp;
					retry->tail = sp;
				} else {
					retry->head = retry->tail = sp;
				}
			} else {
				IFP_SET_REASON(sp, CMD_TRAN_ERR);
				if (good->head) {
					good->tail->cmd_forw = sp;
					good->tail = sp;
				} else {
					good->head = good->tail = sp;
				}
			}
		} else {
			/*
			 * Currently, the target drivers limit the
			 * sense data to 20 bytes (sizeof
			 * scsi_extended_sense) -- unfortunately, ISP fw
			 * cannot be told to alter the data returned. XXX
			 */
			if (!IFP_RESPONSE_STATUS_CONTINUATION(resp_header)) {
				if (good->head) {
					good->tail->cmd_forw = sp;
					good->tail = sp;
				} else {
					good->head = good->tail = sp;
				}
			} else {
				/* Status continuation block seen */
				continue;
			}
		}

		/*
		 * Update ifp deadman timer list.
		 * Note that polled cmds are not in ifp_slots list
		 */
		ifp_slot = &ifp->ifp_slots[sp->cmd_slot];
		if (ifp_slot->slot_cmd == sp) {
			ifp_slot->slot_deadline = 0;
			ifp_slot->slot_cmd = NULL;
		}

		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_Q_END,
		    "ifp_intr_queue_end");
	}
	mutex_exit(IFP_REQ_MUTEX(ifp));
update:
	IFP_SET_RESPONSE_OUT(ifp);
	ATRACE(ifp_i_gather_completions, 0xccccccc9, ifp);
}

static int
ifp_i_handle_mbox_return(struct ifp *ifp, struct ifp_cmd **sp)
{
	int aen;
	ushort_t event;


	ATRACE(ifp_i_handle_mbox_return, 0xdddddd60, ifp);
	mutex_enter(IFP_REQ_MUTEX(ifp));
	event = IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox0);


	/*
	 * process a mailbox event
	 */
#ifdef IFP_PERF
	ifp->ifp_rpio_count += 1;
#endif
	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "ifp_i_handle_mbox_return: event= 0x%x",
	    event));
	if (event & IFP_MBOX_EVENT_ASYNCH) {

		/*
		 * We don't want any requests coming in while we
		 * are dealing with mailbox events.
		 */
		aen = ifp_i_get_async_info(ifp, event, sp);

		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_ASYNC_END,
		    "ifp_i_handle_mbox_return_end (Async Event)");
	} else {
		aen = IFP_AEN_NOT_AN_AEN;
		ifp_i_mbox_cmd_complete(ifp);

		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_MBOX_END,
		    "ifp_i_handle_mbox_return_end (Mailbox Event)");
	}
	mutex_exit(IFP_REQ_MUTEX(ifp));
	ATRACE(ifp_i_handle_mbox_return, 0xdddddd69, ifp);
	return (aen);
}

/*
 * Function name : ifp_i_call_pkt_comp()
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
#ifdef IFPDEBUG
static int ifp_test_reason;
_NOTE(SCHEME_PROTECTS_DATA("No Mutex Needed", ifp_test_reason))
#endif

static void
ifp_i_call_pkt_comp(struct ifp_cmd *head)
{
	ushort_t tgt;
	uchar_t status;
	struct ifp *ifp;
	struct ifp_cmd *sp;
	struct scsi_pkt *pkt;
	struct ifp_response *resp;
	ifp_lun_t *lun_p;

	ifp = CMD2IFP(head);

	ATRACE(ifp_i_call_pkt_comp, 0xddddddd0, ifp);
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_WAITQ_MUTEX(ifp)) == 0 || ddi_in_panic());

	while (head) {
		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_CALL_PKT_COMP_START,
		    "ifp_i_call_pkt_comp_start");
		sp = head;
		pkt = CMD2PKT(sp);
		lun_p = CMD2LUN(sp);
		tgt = lun_p->ifpl_target->ifpt_loop_id;
		head = sp->cmd_forw;

		ATRACE(ifp_i_call_pkt_comp, 0xddddddd1, pkt);
		ASSERT(sp->cmd_flags & CFLAG_FINISHED);

		resp = &sp->cmd_ifp_response;

		status = pkt->pkt_scbp[0] = (uchar_t)resp->resp_scb;

		IFP_GET_PKT_STATE(resp->resp_state, pkt->pkt_state);
		IFP_GET_PKT_STATS(resp->resp_status_flags, pkt->pkt_statistics);


		if (pkt->pkt_reason == CMD_CMPLT) {

			pkt->pkt_reason = (uchar_t)resp->resp_reason;

			/* Check FCP protocol errors */
			if ((resp->resp_scb & IFP_RESP_INFO_LEN_VALID) &&
			    resp->resp_resp_info_len) {

				char *msg1;
				struct fcp_rsp_info *bep;

				bep = (struct fcp_rsp_info *)
				    resp->resp_fcp_resp_info;
				if (bep->rsp_code != FCP_NO_FAILURE) {
					pkt->pkt_reason = CMD_TRAN_ERR;
					switch (bep->rsp_code) {
					case FCP_CMND_INVALID:
						msg1 = "FCP_RSP FCP_CMND "
						    "fields invalid";
						break;
					case FCP_TASK_MGMT_NOT_SUPPTD:
						msg1 = "FCP_RSP Task "
					"Management Function Not Supported";
						break;
					case FCP_TASK_MGMT_FAILED:
						msg1 = "FCP_RSP Task "
						"Management Function Failed";
						ifp->ifp_stats.tstats[tgt].
						    task_mgmt_failures++;
						break;
					case FCP_DATA_RO_MISMATCH:
						msg1 =
						    "FCP_RSP FCP_DATA RO "
					"mismatch with FCP_XFER_RDY DATA_RO";
						ifp->ifp_stats.tstats[tgt].
						    data_ro_mismatches++;
						break;
					case FCP_DL_LEN_MISMATCH:
						msg1 =
						    "FCP_RSP FCP_DATA length "
						"different from BURST_LEN";
						ifp->ifp_stats.tstats[tgt].
						    dl_len_mismatches++;
						break;
					default:
						msg1 =
						    "FCP_RSP invalid RSP_CODE";
						break;
					}

					IFP_DEBUG(3, (ifp, SCSI_DEBUG,
				"%s (code %x) tgt %x lun %x cmd %x %x %x",
					    msg1, bep->rsp_code, tgt,
					    lun_p->ifpl_lun_num,
					    *((uint32_t *)&pkt->pkt_cdbp[0]),
					    *((uint32_t *)&pkt->pkt_cdbp[4]),
					    *((uint32_t *)&pkt->pkt_cdbp[8])));

				}
			}


			/* if the device returned Q full then flag */
			/* complete so ssd will requeue the cmd */
			if (resp->resp_reason == IFP_CMD_QUEUE_FULL) {
				pkt->pkt_reason = CMD_CMPLT;
			}

			/* The firmware flagged an underrun */
			if (resp->resp_reason == IFP_CMD_DATA_UNDER) {
				/* did the device flag an underrun as well? */
				if (resp->resp_scb & IFP_RESID_UNDER) {
					if (status == STATUS_GOOD) {
						/*
						 * A valid underrun is when
						 * both the target
						 * and the firmware
						 * flag underrun and the
						 * target status is good.
						 */
						pkt->pkt_reason = CMD_CMPLT;
					}
				} else {
					/*
					 * The firmware has flagged a xmit
					 * underrun.  The firmware has
					 * detected that less data
					 * was moved than was requested and
					 * the target flagged no underrun.
					 * We will call this a xmit error.
					 */
					pkt->pkt_reason = CMD_TRAN_ERR;
				}
				/*
				 * This is a kludge to handle a bug in the
				 * ses device.
				 */
				if (((lun_p->ifpl_device_type & 0x1f) ==
				    DTYPE_ESI) &&
				    (status == STATUS_GOOD)) {
					pkt->pkt_reason = CMD_CMPLT;
				}
			}

			/*
			 * PORT_UNAVAIL (0x28) is returned when a command
			 * is sent to a non-existing AL_PA (this indicates
			 * a gross bug in the driver) or a PLOGI to
			 * the node following a LIP fails.
			 *
			 * PORT_LOGGED_OUT (0x29) is when the node is
			 * logged out--either implicitly or explicitly.
			 * Implict logouts:
			 *   If the fw is unable to OPN the node for a
			 *   long time, the node is implicitly logged out
			 *   and 0x29 is returned.
			 *   Another case for implicit logouts is an ULP
			 *   timeout and node is not responding to ABTS
			 *   protocol.
			 * Explicit logouts don't happen with Seagate
			 * drives, AFAIK.
			 *
			 * With either 0x28 or 0x29, we can *try* to
			 * recover by forcing a LIP; it might cleanup better
			 * if we go through the full LIP process-PLOGI,
			 * PDISC, and the works!
			 *
			 * PORT_CONFIG_CHANGED indicates HBA couldn't get
			 * its address in LIPA phase or in a public loop,
			 * the FL_PORT forced HBA to get a different AL_PA.
			 * They are relevant only target mode operation.
			 *
			 * While handling a LIP, PORT_UNAVAIL can be
			 * returned if we send a command at a time when
			 * the port db is changing.
			 */
			if (resp->resp_reason == IFP_CMD_PORT_LOGGED_OUT ||
			    resp->resp_reason == IFP_CMD_PORT_UNAVAIL ||
			    resp->resp_reason == IFP_CMD_PORT_CONFIG_CHANGED) {

				ifp_lun_t *lun_p = PKT2LUN(pkt);
				ifp_target_t *target = lun_p->ifpl_target;

				ATRACE(ifp_i_call_pkt_comp, 0xddddddd3, target);
				if (resp->resp_reason ==
				    IFP_CMD_PORT_CONFIG_CHANGED) {
					ifp_i_log(ifp, CE_WARN,
					    "!Impossible status %x on tgt %x "
					    "lip state %x target state %x",
					    resp->resp_reason,
					    pkt->pkt_address.a_target,
					    ifp->ifp_lip_state,
					    target->ifpt_state);
				} else {
#ifdef IFPDEBUG
					ifp_i_log(ifp, CE_NOTE,
					    "?status %x on tgt %x lip state %x"
					    " target state %x",
					    resp->resp_reason,
					    pkt->pkt_address.a_target,
					    ifp->ifp_lip_state,
					    target->ifpt_state);
#endif
				}

				/*
				 * Mark the target BUSY to prevent the
				 * target driver from reissuing the command
				 * while we force a LIP.
				 */
				if (mutex_tryenter(&target->ifpt_mutex)) {
					if (!(target->ifpt_state &
					    IFPT_TARGET_OFFLINE))
						target->ifpt_state |=
						    (IFPT_TARGET_BUSY|
						    IFPT_TARGET_MARK);
					mutex_exit(&target->ifpt_mutex);
				}

				/*
				 * Force a lip and recover when we receive
				 * either 0x28 or 0x29 or 0x2a
				 */
				IFP_MUTEX_ENTER(ifp);
				ifp->ifp_lip_state |=
				    IFPL_NEED_LIP_FORCED;
				IFP_MUTEX_EXIT(ifp);
				pkt->pkt_statistics |=
				    (STAT_ABORTED|STAT_TIMEOUT);
				pkt->pkt_reason = CMD_TIMEOUT;
			}
		} else if (pkt->pkt_reason == CMD_TRAN_ERR) {

			ushort_t cq_flags = resp->resp_header.cq_flags;

			ASSERT(cq_flags);

			if (cq_flags & CQ_FLAG_BADTYPE) {
				ifp_i_log(ifp, CE_WARN, "Bad request pkt type");
			} else if (cq_flags & CQ_FLAG_BADPACKET) {
				ifp_i_log(ifp, CE_WARN, "Bad request pkt");
			} else if (cq_flags & CQ_FLAG_BADHEADER) {
				ifp_i_log(ifp, CE_WARN, "Bad request pkt hdr");
			} else if (cq_flags & CQ_FLAG_BADORDER) {
				ifp_i_log(ifp, CE_WARN, "Bad req pkt order");
			}
		}

		/*
		 * ISP2100 FW is not reliably setting resp_status_flags
		 * that go with the reason. Set it as needed.
		 */
		if (pkt->pkt_reason == CMD_RESET) {
			pkt->pkt_statistics |=
			    (STAT_BUS_RESET|STAT_DEV_RESET|STAT_TIMEOUT);
		} else if (pkt->pkt_reason == CMD_ABORTED) {
			pkt->pkt_statistics |= (STAT_TIMEOUT|STAT_ABORTED);
		}

		/*
		 * IFP always does an auto request sense.
		 */
		pkt->pkt_resid = 0;
		if ((resp->resp_scb & IFP_SENSE_LEN_VALID) &&
		    resp->resp_sense_data_len) {
#ifdef PRT_SENSE_WARN
			if (resp->resp_sense_data_len > 32)
				ifp_i_log(ifp, CE_WARN, "Sense len > 32bytes");
#endif
			pkt->pkt_state |= (STATE_GOT_STATUS|STATE_ARQ_DONE);
		}

		if (resp->resp_scb & (IFP_RESID_UNDER|IFP_RESID_OVER)) {
			pkt->pkt_resid = resp->resp_resid;
		}

#ifdef IFPDEBUG
		if (ifp_test_reason &&
		    (pkt->pkt_reason == CMD_CMPLT)) {
			pkt->pkt_reason = (uchar_t)ifp_test_reason;
			if (ifp_test_reason == CMD_ABORTED) {
				pkt->pkt_statistics |= STAT_ABORTED;
			}
			if (ifp_test_reason == CMD_RESET) {
				pkt->pkt_statistics |=
				    STAT_DEV_RESET | STAT_BUS_RESET;
			}
			ifp_test_reason = 0;
		}
		if (pkt->pkt_resid || resp->resp_scb ||
		    pkt->pkt_reason) {
			uchar_t *cp;
			char buf[128];
			int i;

			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			    "tgt %d.%d: resid=%x,SCSI Status=%x,Status=%x,"
			    "State Flg=%x,Status Flg=%x",
			    TGT(sp), LUN(sp), (int)pkt->pkt_resid,
			    resp->resp_scb,
			    resp->resp_reason,
			    resp->resp_state,
			    resp->resp_status_flags));

			cp = (uchar_t *)pkt->pkt_cdbp;
			buf[0] = '\0';
			for (i = 0; i < (int)sp->cmd_cdblen; i++) {
				(void) sprintf(
				    &buf[strlen(buf)], " 0x%x", *cp++);
				if (strlen(buf) > 124) {
					break;
				}
			}
			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			"\tcflags=%x, cdb dump: %s", sp->cmd_flags, buf));

			cp = (uchar_t *)resp->resp_request_sense;
			buf[0] = '\0';
			for (i = 0; i < resp->resp_sense_data_len; i++) {
				(void) sprintf(
				    &buf[strlen(buf)], " 0x%x", *cp++);
				if ((i & 0x0f) == 0x0f) {
					(void) sprintf(&buf[strlen(buf)], "\n");
				}
				if (strlen(buf) > 124) {
					break;
				}
			}
			(void) sprintf(&buf[strlen(buf)], "\n");
			if (resp->resp_scb & IFP_SENSE_LEN_VALID) {
				IFP_DEBUG(3, (ifp, SCSI_DEBUG,
				"\nlen %x sense dump: %s\n",
				    resp->resp_sense_data_len, buf));
			}

			if (pkt->pkt_reason == CMD_RESET) {
				ASSERT(pkt->pkt_statistics &
				    (STAT_BUS_RESET | STAT_DEV_RESET
				    | STAT_ABORTED));
			} else if (pkt->pkt_reason == CMD_ABORTED) {
				/* XXX this is true at times */
				if (!(resp->resp_status_flags &
				    IFP_STAT_ABORTED)) {
					char buf[128];
					(void) sprintf(buf, "pkt (%p) aborted,"
					    "but stat not set in resp (%p)",
					    (void *)pkt,
					    (void *)resp);
					if (ifp_report_invalid_response)
						debug_enter(buf);
				}
				ASSERT(pkt->pkt_statistics &
				    STAT_ABORTED);
			}
		}
		if (pkt->pkt_state & STATE_XFERRED_DATA) {
			if (ifpdebug > 1 && pkt->pkt_resid) {
				IFP_DEBUG(3, (ifp, SCSI_DEBUG,
				    "%d.%d finishes with %d resid",
				    tgt, LUN(sp), (int)pkt->pkt_resid));
			}
		}

		if ((sp->cmd_cdb[0] == 0x28) &&
		    ((pkt->pkt_statistics & STAT_TIMEOUT) == 0) &&
		    ((pkt->pkt_state & STATE_XFERRED_DATA) == 0)) {
				char buf[128];
				(void) sprintf(buf,
				    "xferred not set for 0x28; pkt 0x%p",
				    (void *)pkt);
				if (ifp_report_invalid_response)
					debug_enter(buf);
		}
#endif	/* IFPDEBUG */

		/*
		 * Was there a check condition and auto request sense?
		 * fake some arqstat fields
		 */
		if (status && ((pkt->pkt_state &
		    (STATE_GOT_STATUS | STATE_ARQ_DONE)) ==
		    (STATE_GOT_STATUS | STATE_ARQ_DONE))) {
			ifp_i_handle_arq(ifp, sp);
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
			    (size_t)0, DDI_DMA_SYNC_FORCPU);
		}

		ASSERT(sp->cmd_flags & CFLAG_IN_TRANSPORT);
		ASSERT(sp->cmd_flags & CFLAG_FINISHED);
		ASSERT((sp->cmd_flags & CFLAG_COMPLETED) == 0);

		sp->cmd_flags = (sp->cmd_flags & ~CFLAG_IN_TRANSPORT) |
		    CFLAG_COMPLETED;

		/*
		 * Call packet completion routine if FLAG_NOINTR is not set.
		 * If FLAG_NOINTR is set turning on CFLAG_COMPLETED in line
		 * above will cause busy wait loop in
		 * ifp_i_polled_cmd_start() to exit.
		 */
		if ((pkt->pkt_flags & FLAG_NOINTR) == 0) {
			scsi_hba_pkt_comp(pkt);
		}

		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_CALL_PKT_COMP_END,
		    "ifp_i_call_pkt_comp_end");
	}
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_WAITQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ATRACE(ifp_i_call_pkt_comp, 0xddddddd9, ifp);
}


/*
 * Function name : ifp_i_call_bad_pkt_comp()
 *
 * Return Values : none
 *
 * Description	 :
 *		   callback into target driver
 *		   argument is a  NULL-terminated list of packets
 *		   -- these are the packets that we planned on putting
 *		   on the waitQ but had to flush due to fatal errors.
 *
 * Context	 : Can be called by interrupt thread.
 */

static void
ifp_i_call_bad_pkt_comp(struct ifp_cmd *head)
{
	struct ifp *ifp;
	struct ifp_cmd *sp;
	struct scsi_pkt *pkt;
	struct ifp_response *resp;

	ifp = CMD2IFP(head);
	ATRACE(ifp_i_call_bad_pkt_comp, 0xdddddd70, ifp);
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_WAITQ_MUTEX(ifp)) == 0 || ddi_in_panic());


	while (head) {
		sp = head;
		pkt = CMD2PKT(sp);
		head = sp->cmd_forw;
		ATRACE(ifp_i_call_bad_pkt_comp, AT_ACT('p', 'k', 't', '0'),
		    pkt);

		resp = &sp->cmd_ifp_response;


		bzero(resp, sizeof (struct ifp_response));

		/* update stats in resp_status_flags */
		resp->resp_status_flags =
		    IFP_STAT_BUS_RESET | IFP_STAT_ABORTED;
		resp->resp_reason = CMD_RESET;

		ASSERT(sp->cmd_flags & CFLAG_IN_TRANSPORT);

#ifdef IFPDEBUG
		resp->resp_reason = (ushort_t)0xbabe;

		sp->cmd_flags |= CFLAG_FINISHED;
#endif
		sp->cmd_flags = (sp->cmd_flags & ~CFLAG_IN_TRANSPORT) |
		    CFLAG_COMPLETED;

		pkt->pkt_reason = CMD_RESET;
		pkt->pkt_state = STATE_GOT_BUS;
		pkt->pkt_statistics |= STAT_BUS_RESET;

		/*
		 * Call packet completion routine if FLAG_NOINTR is not set.
		 * If FLAG_NOINTR is set turning on CFLAG_COMPLETED in line
		 * above will cause busy wait loop in
		 * ifp_i_polled_cmd_start() to exit.
		 */

		if ((pkt->pkt_flags & FLAG_NOINTR) == 0) {
			scsi_hba_pkt_comp(pkt);
		}
	}
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_WAITQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ATRACE(ifp_i_call_bad_pkt_comp, 0xdddddd79, ifp);
}

static void
ifp_i_handle_fatal_errors(struct ifp *ifp, int type,
	struct ifp_cmd_ptrs *good, struct ifp_cmd_ptrs *retry)
{
	int reset_arg;

	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	reset_arg = (type == IFP_AEN_SYS_ERR) ?
	    (IFP_FIRMWARE_ERROR|IFP_DOWNLOAD_FW_ON_ERR) :
	    (IFP_FORCE_RESET_BUS);


	mutex_exit(IFP_RESP_MUTEX(ifp));
	if (good && good->head)
		ifp_i_call_pkt_comp(good->head);
	if (retry && retry->head)
		ifp_i_call_bad_pkt_comp(retry->head);

	mutex_enter(IFP_RESP_MUTEX(ifp));

	/*
	 * We don't have any completions on our hands now.
	 */
	ifp_i_fatal_error(ifp, reset_arg);
}

static int
ifp_i_process_lips(struct ifp *ifp)
{
	int rval = 0;
	uint_t cur_lip_cnt;

	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	cur_lip_cnt = ifp->ifp_lip_cnt;
	ifp->ifp_stats.lip_count++;

	IFP_DEBUG(3, (ifp, SCSI_DEBUG,
	    "ifp_i_process_lips: lip_cnt %x, lip_count %x",
	    ifp->ifp_lip_cnt, ifp->ifp_stats.lip_count));


	/*
	 * There is a bug in the previous releases of fw. The fw once in a
	 * while used to skip the marker event as a result the commands at
	 * that moment used to experience DEV_RESETs all the time and ssd
	 * returns the i/o error to the application.
	 * Now the marker event is sent again to harden the driver.
	 */
	mutex_exit(IFP_RESP_MUTEX(ifp));
	if (ifp_i_set_marker(ifp, SYNCHRONIZE_ALL, 0, 0)) {
		ifp_i_log(ifp, CE_WARN, "ifp_i_process_lips:cannot set marker");
		mutex_enter(IFP_RESP_MUTEX(ifp));
		return (-1);
	}
	mutex_enter(IFP_RESP_MUTEX(ifp));

	mutex_enter(IFP_REQ_MUTEX(ifp));
	ifp_i_mark_loop_down(ifp);
	mutex_exit(IFP_REQ_MUTEX(ifp));
	mutex_exit(IFP_RESP_MUTEX(ifp));
	if (ifp_i_handle_lip(ifp, cur_lip_cnt)) {
		mutex_enter(IFP_RESP_MUTEX(ifp));
		if (cur_lip_cnt == ifp->ifp_lip_cnt) {
			rval = 1;
		}
	} else {
		mutex_enter(IFP_RESP_MUTEX(ifp));
	}

	return (rval);
}

static int
ifp_i_handle_resets(struct ifp *ifp)
{
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));


	/*
	 * Set a marker for an internal BUS reset done
	 */
	mutex_exit(IFP_RESP_MUTEX(ifp));
	if (ifp_i_set_marker(ifp, SYNCHRONIZE_ALL, 0, 0)) {
		ifp_i_log(ifp, CE_WARN, "cannot set marker");
		mutex_enter(IFP_RESP_MUTEX(ifp));
		return (-1);
	}
	mutex_enter(IFP_RESP_MUTEX(ifp));
	(void) scsi_hba_reset_notify_callback(IFP_RESP_MUTEX(ifp),
	    &ifp->ifp_reset_notify_listf);
	return (0);

}

static int
ifp_i_scan_portdb(struct ifp *ifp, uchar_t *loop_map, uchar_t my_alpa,
	uint_t old_lip_cnt, int *try_later)
{
	int i, count;
	clock_t local_lbolt;
	ifp_target_t *target;
	enum alpa_status status;

	ATRACE(ifp_i_scan_portdb, 0x55555530, ifp);

	*try_later = 0;

	count = loop_map[0];

	if (count > 128) {
		IFP_DEBUG(2, (ifp, SCSI_DEBUG,
		    "ifp_i_scan_portdb: count too large %x",
		    count));
		return (1);
	}

	for (i = 1; i <= count; i++) {
		IFP_DEBUG(2, (ifp, SCSI_DEBUG,
		    "ifp_i_scan_portdb: al_pa %x, loop_map[%x] %x, cnt %x",
		    my_alpa, i, loop_map[i], count));

		if (my_alpa == loop_map[i]) {
			continue;
		}

		if (ifp->ifp_alpa_states[ifp_alpa_to_loopid[loop_map[i]]] ==
		    IFPA_ALPA_ONLINE)
			continue;

		target = ifp_i_process_alpa(ifp, loop_map[i], old_lip_cnt,
		    ifp->ifp_lip_state & IFPL_RETRY_SCAN_PORTDB, &status);

		ATRACE(ifp_i_scan_portdb, 0x55555531, target);

		if (old_lip_cnt != ifp->ifp_lip_cnt) {


			/*
			 * things shifted on us while we were learning
			 * about this alpa. Start all over again.
			 */
			return (-1);
		}

		if (target == (struct ifp_target *)NULL) {
			ifp_i_log(ifp, CE_NOTE,
			    "!Unable to allocate target structure "
			    "for switch setting %x\n\treason: %s",
			    ifp_alpa_to_loopid[loop_map[i]],
			    alpa_status_msg[status]);
			if ((status == IFP_PROC_ALPA_FW_OUT_OF_RESOURCES) ||
			    (status == IFP_PROC_ALPA_NO_PORTDB)) {
				local_lbolt = ddi_get_lbolt();
				ifp->ifp_lip_state |= IFPL_NEED_LIP_FORCED;
				ifp->ifp_lip_state &= ~IFPL_HANDLING_LIP;
				ifp->ifp_state = IFP_STATE_OFFLINE;
				ifp->ifp_loopdown_timeout =
				    local_lbolt + IFP_LOOPDOWN_TIME;
				return (1);
			} else if (status == IFP_PROC_ALPA_LIP_OCCURED) {
				local_lbolt = ddi_get_lbolt();
				ifp->ifp_lip_state &= ~IFPL_HANDLING_LIP;
				ifp->ifp_state = IFP_STATE_OFFLINE;
				ifp->ifp_loopdown_timeout =
				    local_lbolt + IFP_LOOPDOWN_TIME;
				return (1);
			} else if (status == IFP_PROC_ALPA_TRY_LATER) {
				ifp->ifp_lip_state &= ~IFPL_HANDLING_LIP;
				*try_later = ACTION_RETURN;
				bzero(ifp->ifp_alpa_states, IFP_FCAL_MAP_SIZE);
				return (0);
			} else if ((status == IFP_PROC_ALPA_PRLI_TIMED_OUT) ||
			    (status == IFP_PROC_ALPA_REPORT_LUNS_TIMED_OUT) ||
			    (status == IFP_PROC_ALPA_INQUIRY_TIMED_OUT)) {
				/*
				 * Ignore this target and continue
				 * with the rest. Try this target later.
				 */
				IFP_DEBUG(3, (ifp, CE_NOTE,
				    "prli timed out for this node"));
				ifp->ifp_alpa_states[ifp_alpa_to_loopid[
				    loop_map[i]]] = IFPA_ALPA_NEEDS_RETRY;
				*try_later = ACTION_CONTINUE;
			}
		} else {
			ifp->ifp_alpa_states[ifp_alpa_to_loopid[loop_map[i]]] =
			    IFPA_ALPA_ONLINE;
			if (status == IFP_PROC_ALPA_GET_DEVICE_TYPE) {
				IFP_DEBUG(2, (ifp, SCSI_DEBUG,
				    "ifp_i_scan_portdb: hard addr targ %x",
				    target->ifpt_hard_address));
			}
		}

		if (old_lip_cnt != ifp->ifp_lip_cnt) {

			return (-1);
		}
	}

	ATRACE(ifp_i_scan_portdb, 0x55555539, ifp);

	return (0);
}

static int
ifp_i_handle_lip(struct ifp *ifp, uint_t cur_lip_cnt)
{
	int i;
	int retry;
	int rval = 0;
	uchar_t my_alpa;
	uint_t old_lip_cnt;
	clock_t local_lbolt;
	uchar_t loop_map[IFP_FCAL_MAP_SIZE];

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	"ifp_i_handle_lip: ifp_stats %x, lip_state %x",
	    ifp->ifp_state, ifp->ifp_lip_state));


	ATRACE(ifp_i_handle_lip, 0xeeeeeee0, ifp);
	/*
	 * This is a very crucial function. Basically, we get into this
	 * when a LIP is seen. We tell the world about being in this routine
	 * by setting IFPL_HANDLING_LIP in ifp_lip_state. We won't get out
	 * this routine till we stopped seeing new lips. We ensure that we
	 * have seen 'em all by checking for lip_cnt value every time
	 * the locks are reacquired. The reason for this is as long as
	 * IFPL_HANDLING_LIP is set, any new LIP will assume the thread
	 * executing this function will handle it.
	 */

	IFP_MUTEX_ENTER(ifp);

	/*
	 * XXX is it ever possible cur_lip_cnt == ifp_lip_cnt &&
	 * IFPL_HANDLING_LIP set? XXX
	 */
	if (cur_lip_cnt != ifp->ifp_lip_cnt ||
	    (ifp->ifp_lip_state & IFPL_HANDLING_LIP)) {
		IFP_MUTEX_EXIT(ifp);
		ATRACE(ifp_i_handle_lip, 0xeeeeeee1, ifp);
		return (0);
	}

again:
	/* OK. Now tell the world we are in here */
	ifp->ifp_lip_state |= IFPL_HANDLING_LIP;
	ifp->ifp_lip_state &= ~IFPL_EXPECTING_LIP;

	if (ifp->ifp_state == IFP_STATE_OFFLINE ||
	    ifp->ifp_lip_state & IFPL_GET_HBA_ALPA) {
		if (ifp_i_get_hba_alpa(ifp, &my_alpa)) {
			ifp->ifp_lip_state &= ~IFPL_HANDLING_LIP;
			ifp->ifp_lip_state |= IFPL_GET_HBA_ALPA;
			local_lbolt = ddi_get_lbolt();
			ifp->ifp_state = IFP_STATE_OFFLINE;
			ifp->ifp_loopdown_timeout = local_lbolt
			    + IFP_LOOPDOWN_TIME;
			rval = 1;

			goto bail;
		} else {
			ifp->ifp_my_alpa = my_alpa;
			ifp->ifp_lip_state &= ~IFPL_GET_HBA_ALPA;
			ifp->ifp_state = IFP_STATE_ONLINE;

		}
	}

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	ifp->ifp_scandb_retry = 0;
	bzero(ifp->ifp_alpa_states, IFP_FCAL_MAP_SIZE);
	ifp->ifp_lip_state &= ~IFPL_RETRY_SCAN_PORTDB;

	my_alpa = ifp->ifp_my_alpa;
	old_lip_cnt = ifp->ifp_lip_cnt;

	if (ifp_i_get_alpa_map(ifp, loop_map)) {

		if (old_lip_cnt == ifp->ifp_lip_cnt) {
			ifp->ifp_lip_state &= ~IFPL_HANDLING_LIP;
			local_lbolt = ddi_get_lbolt();
			ifp->ifp_state = IFP_STATE_OFFLINE;
			ifp->ifp_loopdown_timeout =
			    local_lbolt + IFP_LOOPDOWN_TIME;
			rval = 1;
			goto bail;
		} else
			goto again;
	}

	/* keep trying till the LIP count doesn't change */
	while (old_lip_cnt != ifp->ifp_lip_cnt) {
		if (ifp_i_get_alpa_map(ifp, loop_map)) {
			if (old_lip_cnt == ifp->ifp_lip_cnt) {
				ifp->ifp_lip_state &= ~IFPL_HANDLING_LIP;
				local_lbolt = ddi_get_lbolt();
				ifp->ifp_state = IFP_STATE_OFFLINE;
				ifp->ifp_loopdown_timeout =
				    local_lbolt + IFP_LOOPDOWN_TIME;
				rval = 1;
				goto bail;
			}
		}
		my_alpa = ifp->ifp_my_alpa;
		old_lip_cnt = ifp->ifp_lip_cnt;
	}

	ASSERT(old_lip_cnt == ifp->ifp_lip_cnt); /* paranoia */

#ifdef STE_TARGET_MODE
	/*
	 * If the Target Mode driver is attached, return
	 */
	if (ifp->ifp_tm_hba_event) {
		ifp->ifp_state = IFP_STATE_ONLINE;
		ifp->ifp_lip_state &= ~IFPL_HANDLING_LIP;
		IFP_MUTEX_EXIT(ifp);
		return (0);
	}
#endif /* STE_TARGET_MODE */

	i = ifp_i_scan_portdb(ifp, loop_map, my_alpa, old_lip_cnt, &retry);
	if (i < 0) {
		goto again;
	} else {
		if (i > 0) {
			goto bail;
		}
	}
	if ((retry == ACTION_CONTINUE) || (retry == ACTION_RETURN)) {
		ifp->ifp_scandb_retry++;
		ifp->ifp_lip_state |= IFPL_RETRY_SCAN_PORTDB;
		if (retry == ACTION_RETURN)
			goto bail;
	}

	ASSERT(old_lip_cnt == ifp->ifp_lip_cnt);

	/*
	 * We have visited all the known targets at this LIP time.
	 * Go and create devinfo nodes as appropriate.
	 */
	if (old_lip_cnt != ifp->ifp_lip_cnt) {
		/* sigh... all is wasted. Go back and start all over */
		ifp_i_log(ifp, CE_NOTE,
		"!lip count changed before calling finish_init old %x new %x",
		    old_lip_cnt, ifp->ifp_lip_cnt);
		goto again;
	} else {
		ifp_i_finish_init(ifp, old_lip_cnt);
		if (old_lip_cnt != ifp->ifp_lip_cnt) {
			/* arrrrgh...all is wasted. Go BACK and start again */
			ifp_i_log(ifp, CE_NOTE,
			"!lip count changed while in finish_init old %x new %x",
			    old_lip_cnt, ifp->ifp_lip_cnt);
			goto again;
		}
		ifp_i_log(ifp, CE_NOTE, "!Loop reconfigure done");
	}

	ifp->ifp_state = IFP_STATE_ONLINE;
	ifp->ifp_lip_state &= ~IFPL_HANDLING_LIP;
	ASSERT(old_lip_cnt == ifp->ifp_lip_cnt);

bail:
	IFP_MUTEX_EXIT(ifp);

	ATRACE(ifp_i_handle_lip, 0xeeeeeee9, ifp);
	return (rval);
}

/*
 * Function name : ifp_i_get_alpa_map()
 *
 * Description	 : Gets alpa map
 *
 * Context	 : Can be called from interrupt or kernel context
 */
static int
ifp_i_get_alpa_map(struct ifp *ifp, uchar_t *mapptr)
{
	int rval = 0;
	struct ifp_mbox_cmd mbox_cmd;

	/*
	 * Get FC_AL Position Map
	 */
	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 8, 1,
	    IFP_MBOX_CMD_GET_FC_AL_POSITION_MAP, 0,
	    MSW(ifp->ifp_map_dmacookie.dmac_address),
	    LSW(ifp->ifp_map_dmacookie.dmac_address), 0, 0,
	    0, 0);
	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
		rval = 1;
	} else {
		(void) ddi_dma_sync(ifp->ifp_fcal_maphandle, (off_t)0,
		    (size_t)0, DDI_DMA_SYNC_FORCPU);
	}

	bcopy(ifp->ifp_loop_map, mapptr, IFP_FCAL_MAP_SIZE);
#ifdef IFPDEBUG
	if (ifpdebug > 2)
		ifp_i_print_fcal_position_map(ifp);
#endif
	return (rval);
}

/*
 * Function name : ifp_i_get_port_db()
 *
 * Description	 : Gets port db for the given loopid
 *
 * Context	 : Can be called from interrupt or kernel context
 */
static int
ifp_i_get_port_db(struct ifp *ifp, uchar_t loopid, ifp_portdb_t *portdb)
{
	int rval = 0;
	struct ifp_mbox_cmd mbox_cmd;

	/*
	 * Get Port database for the given loopid
	 */
	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 8, 2,
	    IFP_MBOX_CMD_ENH_GET_PORT_DB, (loopid << 8) | 0x1,
	    MSW(ifp->ifp_portdb_dmacookie.dmac_address),
	    LSW(ifp->ifp_portdb_dmacookie.dmac_address), 0, 0, 0, 0);

	/*
	 * The GET_PORT_DB is replaced with ENH_GET_PORT_DB as there is a
	 * bug in the f/w. When the ifp retries the get_port_db from the
	 * timer thread, the f/w doesn't retry the whole login process and
	 * used to return error always as they mark the node implicitly as
	 * offline.
	 * The new enhanced get port db can fail for different reasons.
	 * reg0 contains the return value and reg1 with the following subcodes:
	 * 1 Link is broken
	 * 2 Firmware could not allocate IOCB buffer
	 * 3 Firmware could not allocate a XCB buffer
	 * 4 PLOGI/PRLI failed due to timeout (no ACC within E_D_TOV)
	 *   or can not open target device
	 * 7 Firmware state is not READY
	 * 8 Initiator mode is disable
	 *
	 * The f/w returns subcode 0x7 while it's doing the logins (PLOGI,
	 * PRLI) and it's not ready at the moment. This can typically
	 * happen when back to back lip happens on the loop.
	 */

	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
		if (mbox_cmd.mbox_in[0] == IFP_MBOX_STATUS_LOOP_DOWN) {
			if (mbox_cmd.mbox_in[1] == 0x7) {
				rval = IFP_PROC_ALPA_LIP_OCCURED;
			} else {
				ifp_i_log(ifp, CE_WARN, "!mbox cmd failed for "
				    "alpa: 0x%x, rval: 0x%x, sub code: 0x%x",
				    ifp_loopid_to_alpa[loopid],
				    mbox_cmd.mbox_in[0],
				    mbox_cmd.mbox_in[1]);
				if (mbox_cmd.mbox_in[1] == 0x4) {
					rval = IFP_PROC_ALPA_PRLI_TIMED_OUT;
				} else {
					rval =
					    IFP_PROC_ALPA_FW_OUT_OF_RESOURCES;
				}
			}
		} else {
			ifp_i_log(ifp, CE_WARN, "!mbox cmd failed for alpa: "
			    "0x%x, rval: 0x%x", ifp_loopid_to_alpa[loopid],
			    mbox_cmd.mbox_in[0]);
			rval = IFP_PROC_ALPA_NO_PORTDB;
		}
	} else {
		(void) ddi_dma_sync(ifp->ifp_fcal_porthandle, (off_t)0,
		    (size_t)0, DDI_DMA_SYNC_FORCPU);

#ifdef IFPDEBUG
		if (ifpdebug > 1) {
			char buf[256];
			(void) sprintf(buf, "loopid %x\n", loopid);
			ifp_i_dump_mem(buf, (void *)&ifp->ifp_portdb,
			    sizeof (ifp_portdb_t));
		}
#endif
		bcopy(&ifp->ifp_portdb, portdb, sizeof (ifp_portdb_t));
	}

	return (rval);
}

/* ARGSUSED */
static struct ifp_lun *
ifp_i_lookup_lun(struct ifp *ifp, uchar_t *wwn, ushort_t lun)
{
	ifp_lun_t *lun_p;
	struct ifp_target *target;

	/* find the target for this wwn */
	target = ifp_i_lookup_target(ifp, wwn);

	if (target != NULL) {
		/* insure that this target has LUN at this LUN number */
		for (lun_p = &target->ifpt_lun; lun_p != NULL;
		    lun_p = lun_p->ifpl_next) {
			if (lun_p->ifpl_lun_num == lun) {
				IFP_DEBUG(2, (ifp, SCSI_DEBUG,
				    "ifp_i_lookup_lun: found target 0x%p "
				    "ID %x, lun %x, dip 0x%p, wwn %x%x",
				    (void *)target,
				    target->ifpt_loop_id, lun,
				    (void *)lun_p->ifpl_dip,
				    *((uint32_t *)&wwn[0]),
				    *((uint32_t *)&wwn[4])));

				return (lun_p);
			}
		}
	}
	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	"ifp_i_lookup_lun: not found lun %x, wwn %x%x",
	    lun, *((uint32_t *)&wwn[0]),  *((uint32_t *)&wwn[4])));
	return (NULL);
}


static struct ifp_target *
ifp_i_lookup_target(struct ifp *ifp, uchar_t *wwn)
{
	/* Returns NULL if there is no target with this wwn */
	int hash;
	struct ifp_target *target;
	uchar_t blank_wwn[FC_WWN_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0};

	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));


	if (bcmp(wwn, blank_wwn, FC_WWN_SIZE) == 0)
		return (NULL);

	hash = IFP_HASH(wwn);

	for (target = ifp->ifp_wwn_lists[hash]; target != NULL;
	    target = target->ifpt_next) {
		if (bcmp(wwn, target->ifpt_port_wwn,
		    sizeof (target->ifpt_port_wwn)) == 0) {
			IFP_DEBUG(2, (ifp, SCSI_DEBUG,
			    "ifp_i_lookup_target: found target 0x%p ID %x, "
			    "wwn %x%x",
			    (void *)target, target->ifpt_loop_id,
			    *((uint32_t *)&wwn[0]),
			    *((uint32_t *)&wwn[4])));

			return (target);
		}
	}
	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_i_lookup_target: not found, wwn %x%x",
	    *((uint32_t *)&wwn[0]),  *((uint32_t *)&wwn[4])));
	return (NULL);
}

static struct ifp_target *
ifp_i_get_target_from_dip(struct ifp *ifp, dev_info_t *dip)
{
	int i;
	struct ifp_target *target;
	ifp_lun_t *lun_p;

	for (i = 0; i < IFP_NUM_HASH_QUEUES; i++) {
		target = ifp->ifp_wwn_lists[i];
		while (target != NULL) {
			for (lun_p = &target->ifpt_lun; lun_p != NULL;
			    lun_p = lun_p->ifpl_next) {
				if (lun_p->ifpl_dip == dip) {
					return (target);
				}
			}
			target = target->ifpt_next;
		}
	}
	return (NULL);
}

static ifp_target_t *
ifp_i_process_alpa(struct ifp *ifp, uchar_t al_pa, uint_t old_lip_cnt,
	int retry, enum alpa_status *status)
{
	int ret;
	ifp_portdb_t portdb;
	uchar_t tnum, hard, hash, port_id;
	uchar_t *port_wwn, *node_wwn;
	struct ifp_target *ntarget, *otarget, *target;
#ifdef IFPDEBUG
	uchar_t blank_wwn[FC_WWN_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0};
#endif


	*status = IFP_PROC_ALPA_SUCCESS;

	ntarget = kmem_zalloc(sizeof (struct ifp_target), KM_NOSLEEP);

	ret = ifp_i_get_port_db(ifp, ifp_alpa_to_loopid[al_pa], &portdb);
	if (ret) {
		*status = ret;
		goto bailout;
	}

	if (old_lip_cnt != ifp->ifp_lip_cnt) {
		*status = IFP_PROC_ALPA_LIP_OCCURED;
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "ifp_i_process_alpa: bailout IFP_PROC_ALPA_LIP_OCCURED"));
		goto bailout;
	}

	port_wwn = portdb.pdb_port_name;
	node_wwn = portdb.pdb_node_name;
	hard = portdb.pdb_hard_address[2];
	tnum = ifp_alpa_to_loopid[hard];
	port_id = portdb.pdb_port_id[2];

	IFP_DEBUG(3, (ifp, SCSI_DEBUG,
	    "ifp_i_process_alpa: al_pa %x, id %x, hard %x, tnum %x port_id %x",
	    al_pa, ifp_alpa_to_loopid[al_pa], hard, tnum, port_id));

	if (IFP_PORTDB_NODE_NOTSTABLE_YET(portdb)) {
		*status = IFP_PROC_ALPA_TRY_LATER;
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "ifp_i_process_alpa: bailout IFP_PROC_ALPA_TRY_LATER"));
		goto bailout;
	}

	if (IFP_PORTDB_TARGET_MODE(portdb) == 0 &&
	    IFP_PORTDB_INITIATOR_MODE(portdb) == 0) {
		*status = IFP_PROC_ALPA_MISBEHAVING_NODE;
#ifdef IFPDEBUG
		ifp_i_dump_mem("portdb\n", &portdb, sizeof (portdb));
#endif
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
	"ifp_i_process_alpa: bailout IFP_PROC_ALPA_MISBEHAVING_NODE"));
		goto bailout;
	}

	if (IFP_PORTDB_TARGET_MODE(portdb)) {
		if ((portdb.pdb_options & PDB_HARD_ADDRESS_FROM_ADISC) == 0) {

			/*
			 * if the node supports target functionality, insist on
			 * a hard address.
			 */
#ifdef IFPDEBUG
			ifp_i_log(ifp, CE_WARN,
		"ifp_i_process_alpa: no hard addr, port_id %x, hard addr %x",
			    port_id, hard);
			ifp_i_dump_mem("\ntarget w/ no hard address: portdb",
			    &portdb, sizeof (portdb));
#endif
			*status = IFP_PROC_ALPA_NO_HARD_ADDR;
			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		"ifp_i_process_alpa: bailout IFP_PROC_ALPA_NO_HARD_ADDR"));
			goto bailout;
		} else if (hard != port_id) {
			/* target didn't get its hard address */
#ifdef IFPDEBUG
			ifp_i_log(ifp, CE_WARN,
		"ifp_i_process_alpa: ERROR port_id %x != hard addr %x",
			    port_id, hard);
			ifp_i_dump_mem("\ntarget didn't get hard addr: portdb",
			    &portdb, sizeof (portdb));
#endif
			*status = IFP_PROC_ALPA_GOT_SOFT_ADDR;
			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			    "ifp_i_process_alpa: ERR, "
			    "IFP_PROC_ALPA_GOT_SOFT_ADDR, hard %x port_id %x",
			    hard, port_id));
			goto bailout;
		}
	} else {
		ASSERT(IFP_PORTDB_INITIATOR_MODE(portdb));
#ifdef IFPDEBUG
		ifp_i_dump_mem("no target role: portdb", &portdb,
		    sizeof (portdb));
#endif
		*status = IFP_PROC_ALPA_NO_TARGET_MODE;

		hard = port_id;
		tnum = ifp_alpa_to_loopid[hard];
	}

#ifdef IFPDEBUG
	if (bcmp(port_wwn, blank_wwn, FC_WWN_SIZE) == 0) {
		ifp_i_dump_mem("portdb\n", &portdb, sizeof (portdb));
	}
#endif

	/*
	 * This is assuming ifp_i_process_alpa will not be called
	 * multiple times for the same node for the same LIP, unless
	 * retry is non-zero.
	 */
	/*
	 * Acquiring target mutex should be unnecessary as ifpt_lip_cnt
	 * will not change while IFP_MUTEX_ENTER is done. ie, while we
	 * are blocking interrupts.
	 */
	target = ifp_i_lookup_target(ifp, port_wwn);


	if (target != NULL) {
		mutex_enter(&target->ifpt_mutex);
		if ((target->ifpt_lip_cnt == ifp->ifp_lip_cnt) &&
		    (retry == 0)) {
			mutex_exit(&target->ifpt_mutex);
			ifp_i_offline_target(ifp, target);
			ifp_i_log(ifp, CE_WARN,
			    "target %x, duplicate port wwns", tnum);
#ifdef IFPDEBUG
			ifp_i_dump_mem("portdb\n", &portdb, sizeof (portdb));
#endif
			*status = IFP_PROC_ALPA_DUPLICATE_WWNS;
			goto bailout;
		}
		mutex_exit(&target->ifpt_mutex);
	} else {
		target = NULL;
	}

	if ((retry == 0) &&
	    (otarget = ifp->ifp_targets[tnum]) != (struct ifp_target *)NULL) {
		mutex_enter(&otarget->ifpt_mutex);
		if (otarget->ifpt_lip_cnt == ifp->ifp_lip_cnt) {
			mutex_exit(&otarget->ifpt_mutex);
			ifp_i_offline_target(ifp, otarget);
			if (target != (struct ifp_target *)NULL)
				ifp_i_offline_target(ifp, target);
			ifp_i_log(ifp, CE_WARN,
			    "target %x, duplicate switch settings", tnum);
#ifdef IFPDEBUG
			ifp_i_dump_mem("portdb\n", &portdb, sizeof (portdb));
#endif
			*status = IFP_PROC_ALPA_DUPLICATE_ADDRS;
			goto bailout;
		}
		mutex_exit(&otarget->ifpt_mutex);
		if (bcmp(port_wwn, otarget->ifpt_port_wwn, FC_WWN_SIZE)) {
			ifp_i_offline_target(ifp, otarget);

			/* A possible replacement drive */

			ifp_i_log(ifp, CE_WARN, "WWN changed on target %x",
							tnum);
#ifdef IFPDEBUG
			prom_printf(
				"new WWN %02x%02x%02x%02x%02x%02x%02x%02x, ",
				port_wwn[0], port_wwn[1], port_wwn[2],
				port_wwn[3], port_wwn[4], port_wwn[5],
				port_wwn[6], port_wwn[7]);
			prom_printf(
				"old WWN %02x%02x%02x%02x%02x%02x%02x%02x\n",
				otarget->ifpt_port_wwn[0],
				otarget->ifpt_port_wwn[1],
				otarget->ifpt_port_wwn[2],
				otarget->ifpt_port_wwn[3],
				otarget->ifpt_port_wwn[4],
				otarget->ifpt_port_wwn[5],
				otarget->ifpt_port_wwn[6],
				otarget->ifpt_port_wwn[7]);
			ifp_i_dump_mem("portdb\n", &portdb, sizeof (portdb));
#endif
			bzero(&ifp->ifp_stats.tstats[tnum],
			    sizeof (struct ifp_target_stats));
		}
	}

	ifp->ifp_targets[tnum] = target;

	if (target == (struct ifp_target *)NULL) {

		/*
		 * First time we are seeing a target at this switch setting,
		 * so add an entry for this switch setting.
		 */
		if (ntarget == (struct ifp_target *)NULL) {
			*status = IFP_PROC_ALPA_ALLOC_FAILED;
			goto bailout;
		}
		bcopy(node_wwn, ntarget->ifpt_node_wwn, FC_WWN_SIZE);
		bcopy(port_wwn, ntarget->ifpt_port_wwn, FC_WWN_SIZE);
		mutex_init(&ntarget->ifpt_mutex, NULL, MUTEX_DRIVER, NULL);
		hash = IFP_HASH(port_wwn);
		ntarget->ifpt_next = ifp->ifp_wwn_lists[hash];
		ifp->ifp_wwn_lists[hash] = ntarget;

		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "created wwn %02x%02x%02x%02x%02x%02x%02x%02x @tnum %x",
		    port_wwn[0], port_wwn[1], port_wwn[2], port_wwn[3],
		    port_wwn[4], port_wwn[5], port_wwn[6], port_wwn[7],
		    tnum));

		target = ntarget;
		target->ifpt_lip_cnt = old_lip_cnt;
		target->ifpt_al_pa =  portdb.pdb_port_id[2];
		target->ifpt_hard_address = hard;
		target->ifpt_lun.ifpl_device_type = DTYPE_UNKNOWN;
		target->ifpt_lun.ifpl_target = target;
		target->ifpt_lun.ifpl_lun_num = 0;

		/*
		 * On a private loop, ifpt_al_pa and ifpt_hard_address
		 * should be equal if hard addresses are picked up.
		 */
		target->ifpt_loop_id = tnum;
		target->ifpt_state = IFPT_TARGET_BUSY;
		ifp->ifp_targets[tnum] = target;

		if (IFP_PORTDB_TARGET_MODE(portdb))
			*status = IFP_PROC_ALPA_GET_DEVICE_TYPE;


		if ((ret = ifp_i_scan_for_luns(ifp, target)) != 0) {
			*status = ret;
			return (NULL);
		}
	} else {

		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "existing wwn %02x%02x%02x%02x%02x%02x%02x%02x@tnum %x",
		    port_wwn[0], port_wwn[1], port_wwn[2], port_wwn[3],
		    port_wwn[4], port_wwn[5], port_wwn[6], port_wwn[7],
		    tnum));
		/*
		 * We know of a target with this switch setting. Just need
		 * to update the info on this one.
		 */
		mutex_enter(&target->ifpt_mutex);
		target->ifpt_lip_cnt = old_lip_cnt;

		target->ifpt_state |= IFPT_TARGET_BUSY;
		target->ifpt_state &= ~(IFPT_TARGET_OFFLINE|IFPT_TARGET_MARK);
		target->ifpt_al_pa =  portdb.pdb_port_id[2];
		target->ifpt_hard_address = hard;
		target->ifpt_loop_id = tnum;
		mutex_exit(&target->ifpt_mutex);
		if (ntarget != NULL) {
			kmem_free(ntarget, sizeof (struct ifp_target));
			ntarget = NULL;
		}

		if ((ret = ifp_i_scan_for_luns(ifp, target)) != 0) {
			*status = ret;
			return (NULL);
		}
	}

	return (target);
bailout:
	if (ntarget != NULL)
		kmem_free(ntarget, sizeof (struct ifp_target));

	return (NULL);
}

/*
 * Find all luns at this SCSI ID.
 * First issue the SCSI REPORT LUNS command and use the returned
 * date to build a list of lun elements to describe the devices
 * at each lun.
 * If the REPORT LUNS command fails then it is probably an older
 * device that dies not support the command.   In this case, issue
 * an INQUIRY command to each possible lun.
 */
#define	RPT_LUN_RETRY	8	/* retry the report luns command */

static int
ifp_i_scan_for_luns(struct ifp *ifp, struct ifp_target *target)
{

	uint_t			ccount, index;
	uchar_t			status;
	struct ifp_mbox_cmd	mbox_cmd;
	ddi_device_acc_attr_t	dev_attr;
	int			retry = 0;
	size_t			real_size;
	struct ifp_request	*req = NULL;
	struct ifp_response	*resp = NULL;
	ddi_acc_handle_t	lun_acc_handle;
	rpt_luns_data_t		*lun_data = NULL;
	int			lun_pkt_bound = 0;
	int			lun_data_bound = 0;
	ddi_dma_handle_t	lun_pkt_handle = NULL;
	ddi_dma_handle_t	lun_data_handle = NULL;
	ddi_dma_cookie_t	lun_data_cookie, lun_pkt_cookie;
	ifp_lun_t		*lun_p, *lun_loop_p;
	ushort_t		lun;
	uint_t			lun_length;
	uchar_t			dtype;
	int			rval = 0;
	int			try_later = 0;
	struct scsi_inquiry	inq_data;

	struct lun_pkt {
		struct ifp_request req;
		struct ifp_response resp;
	} *lun_pkt_ptr;

	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "ifp_i_scan_for_luns: target %x",
	    target->ifpt_loop_id));

	lun_pkt_ptr = NULL;

	dev_attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	dev_attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	dev_attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;

	lun_data = kmem_zalloc(sizeof (rpt_luns_data_t), KM_NOSLEEP);
	if (lun_data == NULL)
		goto fail;

	req = kmem_zalloc(sizeof (struct ifp_request), KM_NOSLEEP);
	if (req == NULL)
		goto fail;

	resp = kmem_zalloc(sizeof (struct ifp_response), KM_NOSLEEP);
	if (resp == NULL)
		goto fail;

	if (ddi_dma_alloc_handle(ifp->ifp_dip, &dma_ifpattr,
	    DDI_DMA_DONTWAIT, NULL, &lun_pkt_handle) != DDI_SUCCESS) {
		goto fail;
	}

	if (ddi_dma_mem_alloc(lun_pkt_handle, sizeof (struct lun_pkt),
	    &dev_attr, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, NULL, (caddr_t *)&lun_pkt_ptr,
	    &real_size, &lun_acc_handle) != DDI_SUCCESS) {
		goto fail;
	}

	if (ddi_dma_addr_bind_handle(lun_pkt_handle, NULL,
	    (caddr_t)lun_pkt_ptr, sizeof (struct lun_pkt), DDI_DMA_READ |
	    DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT,
	    NULL, &lun_pkt_cookie, &ccount) != DDI_DMA_MAPPED) {
		goto fail;
	}
	lun_pkt_bound = 1;

	if (ccount != 1) {
		goto fail;
	}

	if (ddi_dma_alloc_handle(ifp->ifp_dip, &dma_ifpattr,
	    DDI_DMA_DONTWAIT, NULL, &lun_data_handle) != DDI_SUCCESS) {
		goto fail;
	}

	if (ddi_dma_addr_bind_handle(lun_data_handle, NULL,
	    (caddr_t)lun_data, sizeof (rpt_luns_data_t), DDI_DMA_READ |
	    DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT,
	    NULL, &lun_data_cookie, &ccount) != DDI_DMA_MAPPED) {
		goto fail;
	}
	lun_data_bound = 1;

	if (ccount != 1) {
		goto fail;
	}

	req->req_header.cq_entry_type = CQ_TYPE_REQUEST;
	req->req_header.cq_entry_count = 1;
	req->req_header.cq_flags = 0;
	req->req_header.cq_seqno = 0;
	req->req_cdblen = 0;
	req->req_reserved = 0;
	req->req_target = target->ifpt_loop_id;
	req->req_lun_trn = 0;
	req->req_ext_lun = 0;
	req->req_time = 60; /* seconds */

	bzero(req->req_cdb, sizeof (req->req_cdb));
	((union scsi_cdb *)req->req_cdb)->scc_cmd = 0xA0; /* SCMD_REPORT_LUNS */
	((union scsi_cdb *)req->req_cdb)->scc5_count3 =
	    (sizeof (rpt_luns_data_t) >> 24) & 0xff;
	((union scsi_cdb *)req->req_cdb)->scc5_count2 =
	    (sizeof (rpt_luns_data_t) >> 16) & 0xff;
	((union scsi_cdb *)req->req_cdb)->scc5_count1 =
	    (sizeof (rpt_luns_data_t) >> 8) & 0xff;
	((union scsi_cdb *)req->req_cdb)->scc5_count0 =
	    sizeof (rpt_luns_data_t) & 0xff;

	req->req_flags = (IFP_REQ_FLAG_SIMPLE_TAG | IFP_REQ_FLAG_DATA_READ);
	req->req_seg_count = 1;
	req->req_byte_count = (IFP_MAX_LUNS * 8) + 8;
	req->req_dataseg[0].d_count = (IFP_MAX_LUNS * 8) + 8;
	req->req_dataseg[0].d_base = lun_data_cookie.dmac_address;

	IFP_COPY_OUT_REQ(lun_acc_handle, req, &lun_pkt_ptr->req);
	(void) ddi_dma_sync(lun_pkt_handle, 0,
	    (size_t)sizeof (struct ifp_request), DDI_DMA_SYNC_FORDEV);

again:
	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_i_scan_for_luns: report_luns on targ %x",
	    target->ifpt_loop_id));

	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 4, 1,
	    IFP_MBOX_CMD_SCSI_CMD, sizeof (struct ifp_request),
	    MSW(lun_pkt_cookie.dmac_address),
	    LSW(lun_pkt_cookie.dmac_address), 0, 0, 0, 0);

#ifdef IFPDEBUG
	mbox_cmd.mbox_out[7] = target->ifpt_loop_id;
#endif
	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
		if (mbox_cmd.mbox_in[0] == IFP_MBOX_STATUS_LOOP_DOWN) {
			rval = IFP_PROC_ALPA_FW_OUT_OF_RESOURCES;
		} else {
			rval = IFP_PROC_ALPA_NO_PORTDB;
		}
		goto fail;
	}

	(void) ddi_dma_sync(lun_pkt_handle, (off_t)sizeof (struct ifp_request),
	    sizeof (struct ifp_response), DDI_DMA_SYNC_FORCPU);

	IFP_COPY_IN_RESP(lun_acc_handle, &lun_pkt_ptr->resp, resp);

	if (resp->resp_reason == IFP_CMD_PORT_LOGGED_OUT ||
	    resp->resp_reason == IFP_CMD_PORT_UNAVAIL ||
	    resp->resp_reason == IFP_CMD_PORT_CONFIG_CHANGED) {
		goto fail;
	}

	status = (uchar_t)resp->resp_scb;

	if ((status == STATUS_CHECK) &&
	    (resp->resp_scb & IFP_SENSE_LEN_VALID) &&
	    (resp->resp_sense_data_len >=
	    offsetof(struct scsi_extended_sense, es_qual_code))) {

		if (resp->resp_request_sense[2] == KEY_ILLEGAL_REQUEST) {
			if (resp->resp_request_sense[12] == 0x20) {
				/* Fake LUN 0 */
				IFP_DEBUG(2, (ifp, SCSI_DEBUG, "!REPORT_LUNS "
				    "Faking good completion for alpha %x key "
				    "%x ASC %x\n", target->ifpt_al_pa,
				    resp->resp_request_sense[2],
				    resp->resp_request_sense[12]));
				status = STATUS_GOOD;
			}
		}
		if ((resp->resp_request_sense[2] == KEY_NOT_READY) &&
		    (resp->resp_request_sense[12] == 0x04) &&
			(resp->resp_request_sense[13] == 0x01)) {
			rval = IFP_PROC_ALPA_REPORT_LUNS_TIMED_OUT;
			IFP_DEBUG(2, (ifp, SCSI_DEBUG, "REPORT_LUNS failed for"
			    " target %x key %x ASC %x ASCQ %x",
			    target->ifpt_loop_id, resp->resp_request_sense[2],
			    resp->resp_request_sense[12],
			    resp->resp_request_sense[13]));
			goto fail;
		}
	}

	if (status == STATUS_BUSY || status == STATUS_QFULL) {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG, "ifp_i_scan_for_luns: BUSY or "
		    "QFULL, retry report_luns on targ %x",
		    target->ifpt_loop_id));
		rval = IFP_PROC_ALPA_REPORT_LUNS_TIMED_OUT;
		goto fail;
	} else if (status == STATUS_GOOD) {
		/* report luns completed successfully */
		/* get the length of the returned data */
		lun_length = ((uint_t)lun_data->rpt_len_msb << 24) |
		    ((uint_t)lun_data->rpt_len_2sb << 16) |
		    ((uint_t)lun_data->rpt_len_1sb << 8) |
		    ((uint_t)lun_data->rpt_len_lsb);
		/* get the number of returned elements */
		lun_length = lun_length >> 3;

		IFP_DEBUG(2, (ifp, SCSI_DEBUG,
		    "ifp_i_scan_for_luns: report_luns num found %x",
		    lun_length));

		/* did we allocate enough space to hold all luns? */
		if (lun_length > IFP_MAX_LUNS) {
			/* truncate number of luns to read */
			lun_length = IFP_MAX_LUNS;
		}


		if (lun_length == 0) {
			/* must have one LUN */
			lun_length = 1;
		}

		/* build the linked list of lun elements */
		for (index = 0; index < lun_length; index++) {
			/* get the lun of this entry */
			lun = lun_data->rpt_luns[index].rptl_lun1;
			/* get the device type of this entry */
			dtype = ifp_i_issue_inquiry(ifp, target, lun,
			    &try_later, &inq_data);
			if (try_later) {
				rval = try_later;
				goto fail;
			}
			if (((dtype & 0xe0) == 0x00) &&
			    ((dtype & 0x1f) != 0x1f)) {
				int	found_lun = FALSE;

				mutex_enter(&target->ifpt_mutex);
				/* are we already in the lun list? */
				for (lun_loop_p = &target->ifpt_lun;
				    lun_loop_p != NULL;
				    lun_loop_p = lun_loop_p->ifpl_next) {
					if ((lun_loop_p->ifpl_lun_num ==
					    lun) &&
					    (lun_loop_p->ifpl_state &
					    IFPL_LUN_USED)) {
						/* we are here so do nothing */
						found_lun = TRUE;
						break;
					}
				}
				if (found_lun == FALSE) {
					/* Is the base lun available? */
					if (target->ifpt_lun.ifpl_state &
					    IFPL_LUN_USED) {
						/* The base lun is used */
						/* allocate new lun element */
						lun_p = kmem_zalloc(
						    sizeof (ifp_lun_t),
						    KM_NOSLEEP);
						if (lun_p == NULL) {
						mutex_exit(&target->ifpt_mutex);
							goto fail;
						}

						/* tack lun to end of list */
						for (lun_loop_p =
						    &target->ifpt_lun;
						    lun_loop_p->ifpl_next !=
						    NULL;
						    lun_loop_p =
						    lun_loop_p->ifpl_next)
							;
						lun_loop_p->ifpl_next = lun_p;
					} else {
						/* use the base LUN instance */
						lun_p = &target->ifpt_lun;
					}
					/* fill out the values for this lun */
					lun_p->ifpl_next = NULL;
					lun_p->ifpl_state = IFPL_LUN_USED;
					lun_p->ifpl_target = target;
					lun_p->ifpl_lun_num = lun;
					lun_p->ifpl_device_type = dtype;
					if (lun_p->ifpl_inq == NULL)
					lun_p->ifpl_inq = kmem_zalloc(
					    sizeof (struct scsi_inquiry),
					    KM_NOSLEEP);
					if (lun_p->ifpl_inq == NULL) {
						mutex_exit(&target->ifpt_mutex);
						goto fail;
					}
					bcopy(&inq_data, lun_p->ifpl_inq,
					    sizeof (struct scsi_inquiry));
					IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			"ifp_i_scan_for_luns: new lun %x at 0x%p target 0x%p",
					    lun_p->ifpl_lun_num,
					    (void *)lun_p,
					    (void *)lun_p->ifpl_target));
				}
				mutex_exit(&target->ifpt_mutex);
			}
		}
	} else {
#ifdef IFPDEBUG
		if ((status == STATUS_CHECK) &&
		    (resp->resp_scb & IFP_SENSE_LEN_VALID)) {
			uchar_t *cp;
			char buf[128];
			int i;

			cp = (uchar_t *)resp->resp_request_sense;
			buf[0] = '\0';
			for (i = 0; i < resp->resp_sense_data_len; i++) {
				(void) sprintf(&buf[strlen(buf)],
				" 0x%x", *cp++);
				if (strlen(buf) > 124) {
					break;
				}
				if ((i & 0x0f) == 0x0f) {
					(void) sprintf(&buf[strlen(buf)],
					"\n");
				}
			}

			IFP_DEBUG(2, (ifp, SCSI_DEBUG,
			    "report_luns CHECK COND sense dump:\n%s", buf));
		}
#endif
		if ((status == STATUS_CHECK) &&
		    (resp->resp_request_sense[2] == KEY_UNIT_ATTENTION)) {
			/* retry a few times */
			retry++;
			if (retry < RPT_LUN_RETRY) {
				IFP_DEBUG(2, (ifp, SCSI_DEBUG,
				    "ifp_i_scan_for_luns: CHECK, "
				    "retry report_luns on targ %x",
				    target->ifpt_loop_id));
				goto again;
			}
		}

		/* command might not be implemented */
		/* so scan luns using inquiry command */
		IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_i_scan_for_luns: report_luns command not implemented"));
		lun_p = &target->ifpt_lun;

		for (lun = 0; lun < ifp_max_luns_scan; lun++) {
			dtype = ifp_i_issue_inquiry(ifp, target, lun,
			    &try_later, &inq_data);
			if (try_later) {
				rval = try_later;
				goto fail;
			}
			if (((dtype & 0xe0) == 0x00) &&
			    ((dtype & 0x1f) != 0x1f)) {
				int	found_lun = FALSE;

				mutex_enter(&target->ifpt_mutex);
				/* are we already in the lun list? */
				for (lun_loop_p = &target->ifpt_lun;
				    lun_loop_p != NULL;
				    lun_loop_p = lun_loop_p->ifpl_next) {
					if ((lun_loop_p->ifpl_lun_num ==
					    lun) &&
					    (lun_loop_p->ifpl_state &
					    IFPL_LUN_USED)) {
						/* we are here so do nothing */
						found_lun = TRUE;
						break;
					}
				}
				if (found_lun == FALSE) {
					/* Is the base lun available? */
					if (target->ifpt_lun.ifpl_state &
					    IFPL_LUN_USED) {
						/* The base lun is used */
						/* allocate new lun element */
						lun_p = kmem_zalloc(
						    sizeof (ifp_lun_t),
						    KM_NOSLEEP);
						if (lun_p == NULL) {
						mutex_exit(&target->ifpt_mutex);
						goto fail;
						}

						/* tack lun to end of list */
						for (lun_loop_p =
						    &target->ifpt_lun;
						    lun_loop_p->ifpl_next !=
						    NULL;
						    lun_loop_p =
						    lun_loop_p->ifpl_next)
							;
						lun_loop_p->ifpl_next = lun_p;
					} else {
						/* use the base LUN instance */
						lun_p = &target->ifpt_lun;
					}
					/* fill out the values for this lun */
					lun_p->ifpl_next = NULL;
					lun_p->ifpl_state = IFPL_LUN_USED;
					lun_p->ifpl_target = target;
					lun_p->ifpl_lun_num = lun;
					lun_p->ifpl_device_type = dtype;
					if (lun_p->ifpl_inq == NULL)
					lun_p->ifpl_inq = kmem_zalloc(
					    sizeof (struct scsi_inquiry),
					    KM_NOSLEEP);
					if (lun_p->ifpl_inq == NULL) {
						mutex_exit(&target->ifpt_mutex);
						goto fail;
					}
					bcopy(&inq_data, lun_p->ifpl_inq,
					    sizeof (struct scsi_inquiry));
					IFP_DEBUG(3, (ifp, SCSI_DEBUG,
					    "ifp_i_scan_for_luns: new lun %x "
					    "at 0x%p target 0x%p",
					    lun_p->ifpl_lun_num,
					    (void *)lun_p,
					    (void *)lun_p->ifpl_target));
				}
				mutex_exit(&target->ifpt_mutex);
			}
			IFP_DEBUG(2, (ifp, SCSI_DEBUG,
			    "ifp_i_scan_for_luns: id %x lun %x state %x",
			    target->ifpt_loop_id, lun,
			    target->ifpt_state));
		}
	}
fail:
	if (lun_pkt_handle != NULL) {
		if (lun_pkt_bound)
			(void) ddi_dma_unbind_handle(lun_pkt_handle);
		ddi_dma_free_handle(&lun_pkt_handle);
	}
	if (lun_data_handle != NULL) {
		if (lun_data_bound)
			(void) ddi_dma_unbind_handle(lun_data_handle);
		ddi_dma_free_handle(&lun_data_handle);
	}
	if (lun_pkt_ptr != NULL) {
		ddi_dma_mem_free(&lun_acc_handle);
	}
	if (resp)
		kmem_free(resp, sizeof (struct ifp_response));
	if (req)
		kmem_free(req, sizeof (struct ifp_request));
	if (lun_data)
		kmem_free(lun_data, sizeof (rpt_luns_data_t));
	return (rval);
}

/*
 * Function name : ifp_i_handle_arq()
 *
 * Description	 : called on an autorequest sense condition, sets up arqstat
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
#define	ASC_TARGET_CHANGED	0x3f
#define	ASCQ_LUN_CHANGED	0x0e

static void
ifp_i_handle_arq(struct ifp *ifp, struct ifp_cmd *sp)
{
	char status;
	struct scsi_pkt *pkt = CMD2PKT(sp);
	struct ifp_response *resp = &sp->cmd_ifp_response;


	ATRACE(ifp_i_handle_arq, AT_ACT('p', 'k', 't', '0'), pkt);

	/* check if a new LUN has been created */
	if ((resp->resp_request_sense[2] == KEY_UNIT_ATTENTION) &&
	    (resp->resp_request_sense[12] == ASC_TARGET_CHANGED) &&
	    (resp->resp_request_sense[13] == ASCQ_LUN_CHANGED)) {
		ifp_target_t *target;

		IFP_MUTEX_ENTER(ifp);
		target = ifp->ifp_targets[TGT(sp)];
		(void) ifp_i_scan_for_luns(ifp, target);
		ifp_i_finish_target_init(ifp, target, ifp->ifp_lip_cnt);
		IFP_MUTEX_EXIT(ifp);
	}

	if (sp->cmd_scblen >= sizeof (struct scsi_arq_status)) {
		struct scsi_arq_status *arqstat;

		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "tgt %d.%d: auto request sense", TGT(sp), LUN(sp)));

		arqstat = (struct scsi_arq_status *)(pkt->pkt_scbp);
		status = pkt->pkt_scbp[0];
		bzero(arqstat, sizeof (struct scsi_arq_status));

		/*
		 * use same statistics as the original cmd
		 */
		arqstat->sts_rqpkt_statistics = pkt->pkt_statistics;
		arqstat->sts_rqpkt_state =
		    (STATE_GOT_BUS | STATE_GOT_TARGET |
		    STATE_SENT_CMD | STATE_XFERRED_DATA | STATE_GOT_STATUS);
		if (resp->resp_sense_data_len <
		    sizeof (struct scsi_extended_sense)) {
			arqstat->sts_rqpkt_resid =
				sizeof (struct scsi_extended_sense) -
				resp->resp_sense_data_len;
		}
		bcopy(resp->resp_request_sense, &arqstat->sts_sensedata,
		    sizeof (struct scsi_extended_sense));
		/*
		 * restore status which was wiped out by bzero
		 */
		pkt->pkt_scbp[0] = status;
	} else {
		/*
		 * bad packet; can't copy over ARQ data
		 * XXX need CMD_BAD_PKT
		 */
		IFP_SET_REASON(sp, CMD_TRAN_ERR);
	}
	ATRACE(ifp_i_handle_arq, AT_ACT('p', 'k', 't', '1'), pkt);
}


/*
 * Function name : ifp_i_find_free_slot()
 *
 * Return Values : empty slot  number
 *		   -1 if no slot available
 *
 * Description	 : find an empty slot in the ifp_slots list
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static ushort_t
ifp_i_find_freeslot(struct ifp *ifp)
{
	int i;
	ushort_t slot;
	char found = 0;

	/*
	 * If slot in use, scan for a free one. Walk thru
	 * ifp_slots, starting at current tag
	 * this should rarely happen.
	 */
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	slot = ifp->ifp_free_slot;
	for (i = 0; i < (IFP_MAX_SLOTS - 1); i++) {
		slot = ifp->ifp_free_slot++;
		if (ifp->ifp_free_slot >=
		    (ushort_t)IFP_MAX_SLOTS) {
			ifp->ifp_free_slot = 0;
		}
		if (ifp->ifp_slots[slot].slot_cmd == NULL) {
			found = 1;
			break;
		}
		IFP_DEBUG(2, (ifp, SCSI_DEBUG, "found in use slot %d", slot));
	}
	if (!found) {
		slot = 0xffff;
	}
	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "found free slot %d", slot));
	return (slot);
}

/*
 * Function name : ifp_i_polled_cmd_start()
 *
 * Return Values : TRAN_ACCEPT	if transaction was accepted
 *		   TRAN_BUSY	if I/O could not be started
 *		   TRAN_ACCEPT	if I/O timed out, pkt fields indicate error
 *
 * Description	 : Starts up I/O in normal fashion by calling ifp_i_start_cmd().
 *		   Busy waits for I/O to complete or timeout.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
ifp_i_polled_cmd_start(struct ifp *ifp, struct ifp_cmd *sp)
{
	int rval;
	int delay_loops;
	struct scsi_pkt *pkt = CMD2PKT(sp);

	ATRACE(ifp_i_polled_cmd_start, AT_ACT('p', 'k', 't', '0'), pkt);
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());


	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_RUN_POLLED_CMD_START,
	    "ifp_i_polled_cmd_start_start");

	/*
	 * set timeout to SCSI_POLL_TIMEOUT for non-polling
	 * commands that do not have this field set
	 */
	if (pkt->pkt_time == 0)
		pkt->pkt_time = SCSI_POLL_TIMEOUT;

	/*
	 * try and start up command
	 */
	mutex_enter(IFP_REQ_MUTEX(ifp));
	rval = ifp_i_start_cmd(ifp, sp);
	IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);
	if (rval != TRAN_ACCEPT) {
		goto done;
	}

	/*
	 * busy wait for command to finish ie. till CFLAG_COMPLETED is set
	 */
retry:
	delay_loops = IFP_TIMEOUT_DELAY(
	    (pkt->pkt_time + (IFP_GRACE << 1)), IFP_NOINTR_POLL_DELAY_TIME);

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "delay_loops=%d, delay=%d, pkt_time=%x, cdb[0]=%x\n", delay_loops,
	    IFP_NOINTR_POLL_DELAY_TIME, pkt->pkt_time,
	    *(sp->cmd_pkt->pkt_cdbp)));

	while ((sp->cmd_flags & CFLAG_COMPLETED) == 0) {
		drv_usecwait(IFP_NOINTR_POLL_DELAY_TIME);


		if (--delay_loops <= 0) {
			struct ifp_response *resp;

			/*
			 * Call ifp_scsi_abort()  to abort the I/O
			 * and if ifp_scsi_abort fails then
			 * blow everything away
			 */
			if ((ifp_scsi_reset(&pkt->pkt_address, RESET_TARGET))
			    == FALSE) {
				mutex_enter(IFP_RESP_MUTEX(ifp));
				ifp_i_fatal_error(ifp, 0);
				mutex_exit(IFP_RESP_MUTEX(ifp));
			}

			resp = &sp->cmd_ifp_response;
			bzero(resp, sizeof (struct ifp_response));

			/*
			 * update stats in resp_status_flags
			 * ifp_i_call_pkt_comp() copies this
			 * over to pkt_statistics
			 */
			resp->resp_status_flags |=
			    STAT_BUS_RESET | STAT_TIMEOUT;
			resp->resp_reason = CMD_TIMEOUT;
#ifdef IFPDEBUG
			sp->cmd_flags |= CFLAG_FINISHED;
#endif
			ifp_i_call_pkt_comp(sp);
			break;
		}

		if (IFP_INT_PENDING(ifp)) {
			(void) ifp_intr((caddr_t)ifp);
		}
	}
	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "polled cmd done\n"));

done:
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_RUN_POLLED_CMD_END,
	    "ifp_i_polled_cmd_start_end");

	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());

	ATRACE(ifp_i_polled_cmd_start, AT_ACT('p', 'k', 't', '1'), pkt);
	return (rval);
}


/*
 * Function name : ifp_i_get_async_info
 *
 * Return Values : -1	Fatal error occurred
 *		    0	normal return
 * Description	 :
 * An Event of 8002 is a Sys Err in the ISP.  This would require
 *	Chip reset.
 *
 * An Event of 8001 is a external Reset
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
ifp_i_get_async_info(struct ifp *ifp, ushort_t event, struct ifp_cmd **ret)
{
	uint32_t rptr;
	struct ifp_cmd *sp;
	clock_t local_lbolt;
	struct scsi_pkt *pkt;
	ushort_t mbox1, mbox2;
	struct ifp_response *resp;
	struct ifp_slot *ifp_slot;
	int rval = IFP_AEN_SUCCESS;
	int clear_semaphore_lock = 1;

	ATRACE(ifp_i_get_async_info, 0xdddddd80, ifp);
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));


	TRACE_1(TR_FAC_SCSI_ISP, TR_ISP_I_ASYNCH_EVENT_START,
	    "ifp_i_get_async_info_start (event = %d)", event);

#ifdef IFPDEBUG
	if (IFP_GET_MBOX_EVENT(event) != IFP_MBOX_ASYNC_FAST_IO_POST) {
		ifp_i_log(ifp, CE_NOTE, "?async event %x", event);
	}
#endif
	ASSERT(IFP_CHECK_SEMAPHORE_LOCK(ifp));

	switch (IFP_GET_MBOX_EVENT(event)) {
	case IFP_MBOX_ASYNC_FAST_IO_POST:

		/*
		 * Fast IO posting--get the handle from mbox1 and mbox2.
		 */
		mbox1 = IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox1);
		mbox2 = IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox2);
		rptr = (mbox2 << 16) | mbox1;
		sp = IFP_LOOKUP_ID(rptr);

		rval = IFP_AEN_FAST_POST;
		clear_semaphore_lock = 0;
		IFP_CLEAR_SEMAPHORE_LOCK(ifp);
		IFP_CLEAR_RISC_INT(ifp);

		/*
		 * Update ifp deadman timer list.
		 */
		pkt = CMD2PKT(sp);
		ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));
		ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
		if ((pkt->pkt_flags & FLAG_NOINTR) == 0) {
			ASSERT(sp->cmd_slot <= IFP_MAX_SLOTS);
			ifp_slot = &ifp->ifp_slots[sp->cmd_slot];
			if (ifp_slot->slot_cmd == sp) {
				ifp_slot->slot_deadline = 0;
				ifp_slot->slot_cmd = NULL;
			}
		}

		/*
		 * Set all the status fields to good
		 */
		resp = &sp->cmd_ifp_response;
		resp->resp_scb = 0;
		resp->resp_reason = 0;
		resp->resp_status_flags = 0;
		resp->resp_resid = 0;
#ifdef IFPDEBUG
		resp->resp_token = (uint_t)0xfeedface;
#endif

		/*
		 * NULL terminate the list
		 */
		sp->cmd_forw = NULL;
#ifdef IFPDEBUG
		sp->cmd_flags |= CFLAG_FINISHED;
#endif
		resp->resp_state = IFP_STATE_GOT_STATUS;
		if (sp->cmd_flags & CFLAG_DMAVALID) {
			resp->resp_state |= IFP_STATE_XFERRED_DATA;
		}
		*ret = sp;
		break;

	case IFP_MBOX_ASYNC_LOOP_UP:
		ifp_i_log(ifp, CE_NOTE, "!Loop up");

		break;

	case IFP_MBOX_ASYNC_PDB_UPDATED:
		ifp_i_log(ifp, CE_NOTE, "!Loop reconfigure in progress");
		ifp->ifp_lip_cnt++;
		rval = IFP_AEN_LIP;

		break;

	case IFP_MBOX_ASYNC_LIP_RESET:
		mbox1 = IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox1);
		ifp_i_log(ifp, CE_NOTE, "!LIP reset occurred; cause %x", mbox1);
		ifp_i_mark_loop_down(ifp);
		/*FALLTHRU*/
	case IFP_MBOX_ASYNC_RESET:

		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
	    "IFP initiated SCSI BUS Reset or external SCSI Reset, event %x",
		    event));


		rval = IFP_AEN_RESET;
		break;

	case IFP_MBOX_ASYNC_ERR:
	case IFP_MBOX_ASYNC_REQ_DMA_ERR:
	case IFP_MBOX_ASYNC_RESP_DMA_ERR:


		if (IFP_GET_MBOX_EVENT(event) == IFP_MBOX_ASYNC_ERR) {
			/*
			 * Force the current commands to timeout after
			 * resetting the chip.
			 */
			ifp_i_log(ifp, CE_WARN, "Firmware error");
			ifp_i_print_state(ifp);
			rval = IFP_AEN_SYS_ERR;
		} else {
			/*
			 *  DMA failed in the ISP chip force a Reset
			 */
			ifp_i_log(ifp, CE_WARN, "DMA Failure (%x)", event);
			rval = IFP_AEN_DMA_ERR;
		}

		/*
		 * Mark all the targets as busy since we don't want any
		 * commands going out to the devices till the fatal error
		 * recovery is done.
		 */
		ifp_i_mark_loop_down(ifp);
		break;

	case IFP_MBOX_ASYNC_LIP_OCCURED:
		mbox1 = IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox1);
		ifp_i_log(ifp, CE_NOTE, "!LIP occurred; cause %x", mbox1);
		/*FALLTHRU*/
	case IFP_MBOX_ASYNC_LOOP_DOWN:

		if (IFP_GET_MBOX_EVENT(event) == IFP_MBOX_ASYNC_LOOP_DOWN) {
			ifp_i_log(ifp, CE_NOTE, "!Loop down");
		}
		/*
		 * Walk through all our targets and mark them busy;
		 * ifp_i_finish_init() will bring them back when it is done
		 * with dealing with the LIP.
		 */
		ifp_i_mark_loop_down(ifp);
		local_lbolt = ddi_get_lbolt();
		ifp->ifp_state = IFP_STATE_OFFLINE;
		ifp->ifp_loopdown_timeout =
		    local_lbolt + IFP_LOOPDOWN_TIME;
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "ifp_i_get_async_info: offline HBA"));
		break;

	default:
		ifp_i_log(ifp, CE_NOTE,
		    "unknown async evt:0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
		    event,
		    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox1),
		    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox2),
		    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox3),
		    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox4),
		    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox5));
		break;
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_ASYNCH_EVENT_END,
	    "ifp_i_get_async_event_end");

	if (clear_semaphore_lock) {
		IFP_CLEAR_SEMAPHORE_LOCK(ifp);
		IFP_CLEAR_RISC_INT(ifp);
	}

	ATRACE(ifp_i_get_async_info, 0xdddddd89, ifp);
	return (rval);
}

/*
 * Function name : ifp_i_mbox_cmd_complete ()
 *
 * Return Values : None.
 *
 * Description	 : Sets IFP_MBOX_CMD_FLAGS_COMPLETE flag so busy wait loop
 *		   in ifp_i_mbox_cmd_start() exits.
 *
 * Context	 : Can be called by interrupt thread or process context.
 */
static void
ifp_i_mbox_cmd_complete(struct ifp *ifp)
{
	uchar_t i;
	int delay_loops;
	ushort_t *mbox_regp = (ushort_t *)&ifp->ifp_biu_reg->ifp_mailbox0;


	ATRACE(ifp_i_mbox_cmd_complete, 0xdddddd90, ifp);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_MBOX_CMD_COMPLETE_START,
	    "ifp_i_mbox_cmd_complete_start");

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_i_mbox_cmd_complete_start(cmd = 0x%x)",
	    ifp->ifp_mbox.mbox_cmd.mbox_out[0]));

	/*
	 * Check for completions that are caused by mailbox events
	 * but that do not set the mailbox status bit ie. 0x4xxx
	 * For now only the busy condition is checked, the others
	 * will automatically time out and error.
	 */
	delay_loops = IFP_TIMEOUT_DELAY(IFP_MBOX_CMD_BUSY_WAIT_TIME,
	    IFP_MBOX_CMD_BUSY_POLL_DELAY_TIME);
	while (IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox0) ==
	    IFP_MBOX_BUSY) {
		drv_usecwait(IFP_MBOX_CMD_BUSY_POLL_DELAY_TIME);
		if (--delay_loops < 0) {
			ifp->ifp_mbox.mbox_cmd.mbox_in[0] =
			    IFP_MBOX_STATUS_FIRMWARE_ERR;
			goto fail;
		}
	}

	/*
	 * save away status registers
	 */
	for (i = 0; i < ifp->ifp_mbox.mbox_cmd.n_mbox_in; i++, mbox_regp++) {
		ifp->ifp_mbox.mbox_cmd.mbox_in[i] =
		    IFP_READ_MBOX_REG(ifp, mbox_regp);

	}
#ifdef IFP_PERF
	ifp->ifp_rpio_count += ifp->ifp_mbox.mbox_cmd.n_mbox_in;
#endif

fail:
	/*
	 * set flag so that busy wait loop will detect this and return
	 */
	ifp->ifp_mbox.mbox_flags |= IFP_MBOX_CMD_FLAGS_COMPLETE;

	/*
	 * clear the semaphore lock
	 */
	IFP_CLEAR_SEMAPHORE_LOCK(ifp);

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_MBOX_CMD_COMPLETE_END,
	    "ifp_i_mbox_cmd_complete_end");

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_i_mbox_cmd_complete_end (cmd = 0x%x)",
	    ifp->ifp_mbox.mbox_cmd.mbox_out[0]));
#ifdef IFPDEBUG
	if (ifpdebug > 0) {
		ifp_i_print_state(ifp);
	}
#endif
	ATRACE(ifp_i_mbox_cmd_complete, 0xdddddd99, ifp);
}

/*
 * Function name : ifp_i_mbox_cmd_init()
 *
 * Return Values : none
 *
 * Description	 : initializes mailbox command
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
ifp_i_mbox_cmd_init(struct ifp *ifp, struct ifp_mbox_cmd *mbox_cmdp,
    uchar_t n_mbox_out, uchar_t n_mbox_in,
    ushort_t reg0, ushort_t reg1, ushort_t reg2,
    ushort_t reg3, ushort_t reg4, ushort_t reg5,
    ushort_t reg6, ushort_t reg7)
{

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_i_mbox_cmd_init r0 = 0x%x r1 = 0x%x r2 = 0x%x",
	    reg0, reg1, reg2));
	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "			 r3 = 0x%x r4 = 0x%x r5 = 0x%x",
	    reg3, reg4, reg5));
	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "			 r6 = 0x%x r7 = 0x%x",
	    reg6, reg7));



	mbox_cmdp->timeout	= IFP_MBOX_CMD_TIMEOUT;
	mbox_cmdp->retry_cnt	= IFP_MBOX_CMD_RETRY_CNT;
	mbox_cmdp->n_mbox_out	= n_mbox_out;
	mbox_cmdp->n_mbox_in	= n_mbox_in;

	ASSERT(n_mbox_out <= ifp->ifp_chip_reg_cnt);

	mbox_cmdp->mbox_in[0] = (ushort_t)0xbabe;

	if (reg0 == IFP_MBOX_CMD_INIT_FIRMWARE ||
	    reg0 == IFP_MBOX_CMD_GET_PORT_DB ||
	    reg0 == IFP_MBOX_CMD_ENH_GET_PORT_DB ||
	    reg0 == IFP_MBOX_CMD_SCSI_CMD)
		mbox_cmdp->timeout <<= 3;

	switch (n_mbox_out) {
		case ARG8:
			mbox_cmdp->mbox_out[REG7] = reg7;
			/*FALLTHRU*/
		case ARG7:
			mbox_cmdp->mbox_out[REG6] = reg6;
			/*FALLTHRU*/
		case ARG6:
			mbox_cmdp->mbox_out[REG5] = reg5;
			/*FALLTHRU*/
		case ARG5:
			mbox_cmdp->mbox_out[REG4] = reg4;
			/*FALLTHRU*/
		case ARG4:
			mbox_cmdp->mbox_out[REG3] = reg3;
			/*FALLTHRU*/
		case ARG3:
			mbox_cmdp->mbox_out[REG2] = reg2;
			/*FALLTHRU*/
		case ARG2:
			mbox_cmdp->mbox_out[REG1] = reg1;
			/*FALLTHRU*/
		case ARG1:
			mbox_cmdp->mbox_out[REG0] = reg0;
			break;
		default:
			break;
	}
}

/*
 * Function name : ifp_i_mbox_write_regs()
 *
 * Return Values : none
 *
 * Description	 : writes mailbox command to the board
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
ifp_i_mbox_write_regs(struct ifp *ifp, struct ifp_mbox_cmd *mboxcmdp)
{
	ushort_t cmd;
	int i, count, n_mbox_out;
	ushort_t *mbox_regp = (ushort_t *)&ifp->ifp_biu_reg->ifp_mailbox0;
	int	reg22_offset;

	/* calculate the offset between mailbox reg7 and reg8 */
	reg22_offset = &ifp->ifp_biu_reg->ifp_risc_reg.ifp_22mailbox8 -
	    &ifp->ifp_biu_reg->ifp_pci_mailbox7;
	reg22_offset -= 1;

	/* write outgoing registers */

	cmd = mboxcmdp->mbox_out[0];
	n_mbox_out = mboxcmdp->n_mbox_out;

	for (i = count = 0; i < ifp->ifp_chip_reg_cnt && count < n_mbox_out;
	    i++, mbox_regp++) {

		/* touch only the registers that can be touched. */

		switch (i) {

		case REG0:
		case REG2:
		case REG3:
			IFP_WRITE_MBOX_REG(ifp, mbox_regp,
			    mboxcmdp->mbox_out[i]);
			count++;
			break;

		case REG1:
			switch (cmd) {
			case IFP_MBOX_CMD_INIT_FIRMWARE:
			case IFP_MBOX_CMD_GET_INIT_CONTROL_BLOCK:
			case IFP_MBOX_CMD_GET_FC_AL_POSITION_MAP:
				break;

			default:
				IFP_WRITE_MBOX_REG(ifp, mbox_regp,
				    mboxcmdp->mbox_out[i]);
				count++;
				break;
			}
			break;

		case REG4:
			switch (cmd) {
			case IFP_MBOX_CMD_LOAD_RAM:
			case IFP_MBOX_CMD_WRAP_MAILBOXES:
			case IFP_MBOX_CMD_LOAD_RISC_RAM:
			case IFP_MBOX_CMD_DUMP_RISC_RAM:
			case IFP_MBOX_CMD_INIT_REQUEST_QUEUE:
			case IFP_MBOX_CMD_INIT_FIRMWARE:

				IFP_WRITE_MBOX_REG(ifp, mbox_regp,
				    mboxcmdp->mbox_out[i]);
				count++;
				break;

			default:
				break;
			}
			break;

		case REG5:
			switch (cmd) {
			case IFP_MBOX_CMD_WRAP_MAILBOXES:
			case IFP_MBOX_CMD_INIT_RESPONSE_QUEUE:
			case IFP_MBOX_CMD_INIT_FIRMWARE:

				IFP_WRITE_MBOX_REG(ifp, mbox_regp,
				    mboxcmdp->mbox_out[i]);
				count++;
				break;

			default:
				break;
			}
			break;

		case REG6:
			switch (cmd) {
			case IFP_MBOX_CMD_WRAP_MAILBOXES:
			case IFP_MBOX_CMD_LOAD_RISC_RAM:
			case IFP_MBOX_CMD_DUMP_RISC_RAM:
			case IFP_MBOX_CMD_SCSI_CMD64:
			case IFP_MBOX_CMD_INIT_FIRMWARE:
			case IFP_MBOX_CMD_GET_INIT_CONTROL_BLOCK:
			case IFP_MBOX_CMD_GET_FC_AL_POSITION_MAP:
			case IFP_MBOX_CMD_GET_PORT_DB:
			case IFP_MBOX_CMD_ENH_GET_PORT_DB:
			case IFP_MBOX_CMD_GET_PORT_NAME:

				IFP_WRITE_MBOX_REG(ifp, mbox_regp,
				    mboxcmdp->mbox_out[i]);
				count++;
				break;

			default:
				break;
			}
			break;

		case REG7:
			switch (cmd) {
			case IFP_MBOX_CMD_WRAP_MAILBOXES:
			case IFP_MBOX_CMD_LOAD_RISC_RAM:
			case IFP_MBOX_CMD_DUMP_RISC_RAM:
			case IFP_MBOX_CMD_SCSI_CMD64:
			case IFP_MBOX_CMD_INIT_FIRMWARE:
			case IFP_MBOX_CMD_GET_INIT_CONTROL_BLOCK:
			case IFP_MBOX_CMD_GET_FC_AL_POSITION_MAP:
			case IFP_MBOX_CMD_GET_PORT_DB:
			case IFP_MBOX_CMD_ENH_GET_PORT_DB:
			case IFP_MBOX_CMD_GET_PORT_NAME:

				IFP_WRITE_MBOX_REG(ifp, mbox_regp,
				    mboxcmdp->mbox_out[i]);
				count++;
				break;

			default:
				break;
			}
			break;

		case REG10:
		case REG11:
		case REG12:
		case REG13:
		case REG14:
		case REG15:
		case REG16:
		case REG17:
		case REG18:
		case REG19:
			switch (cmd) {
			case IFP_MBOX_CMD_LOOPBACK:
				ATRACE(ifp_i_mbox_write_regs, 0x11111162,
				    mbox_regp + reg22_offset);
				IFP_WRITE_MBOX_REG(ifp,
				    mbox_regp + reg22_offset,
				    mboxcmdp->mbox_out[i]);
				count++;
				break;

			default:
				break;
			}
			break;
		default:
			break;
		}
	}
}

/*
 * Function name : ifp_i_mbox_cmd_start()
 *
 * Return Values : 0   if no error
 *               -1  on error other than mailbox cmd timeout.
 *               -2  on a mailbox cmd timeout.
 *		   Status registers are returned in structure that is passed in.
 *
 * Description	 : Sends mailbox cmd to IFP and busy waits for completion.
 *		   Serializes accesses to the mailboxes.
 *		   Mailbox cmds are used to initialize the IFP, modify default
 *			parameters, and load new firmware among other things.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int ifp_debug_mbox;

static int
ifp_i_mbox_cmd_start(struct ifp *ifp, struct ifp_mbox_cmd *mbox_cmdp)
{
	uchar_t i;
	int aen_lips;
	int retry_cnt;
	int aen_resets;
	struct ifp_cmd *sp;
	int delay_loops, disp, rval;
	struct ifp_cmd_ptrs normalcmds;


	ATRACE(ifp_i_mbox_cmd_start, 0xdddddda0, ifp);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_MBOX_CMD_START_START,
	    "ifp_i_mbox_cmd_start_start");

	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "ifp_i_mbox_cmd_start_start(cmd = 0x%x)",
	    mbox_cmdp->mbox_out[0]));

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	if (mutex_tryenter(IFP_MBOX_MUTEX(ifp)) == 0) {
		IFP_MUTEX_EXIT(ifp);
		mutex_enter(IFP_MBOX_MUTEX(ifp));
		IFP_MUTEX_ENTER(ifp);
	}

	normalcmds.head = normalcmds.tail = (struct ifp_cmd *)NULL;

	sp = NULL;
	rval = 0;
	aen_lips = aen_resets = 0;

	if (ifp->ifp_shutdown) {
		/*
		 * Things could have changed while we were waiting for
		 * the various mutex's.
		 */
		rval = -1;
		mutex_exit(IFP_MBOX_MUTEX(ifp));
		ATRACE(ifp_i_mbox_cmd_start, 0xdddddda9, ifp);
		return (rval);
	}

	mutex_exit(IFP_REQ_MUTEX(ifp));

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0);
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	/* save away mailbox command */
	bcopy(mbox_cmdp, &ifp->ifp_mbox.mbox_cmd,
	    sizeof (struct ifp_mbox_cmd));

	retry_cnt = mbox_cmdp->retry_cnt;
retry:

#ifdef IFPDEBUG
	if (ifp_mbox_debug) {
		ifp_i_log(ifp, CE_NOTE, "issuing mbox cmd %x",
		    mbox_cmdp->mbox_out[0]);
		ifp_i_print_state(ifp);

		if (IFP_REG_GET_HOST_INT(ifp)) {
			ifp_i_log(ifp, CE_NOTE, "Gag! host int set before %x",
			    mbox_cmdp->mbox_out[0]);
		if (IFP_REG_GET_HOST_INT(ifp))
			ifp_i_log(ifp, CE_NOTE, "Gag! not set any more");
		}
	}
#endif

	ifp_i_mbox_write_regs(ifp, mbox_cmdp);

#ifdef IFP_PERF
	ifp->ifp_wpio_count += ifp->ifp_mbox.mbox_cmd.n_mbox_out;
	ifp->ifp_mail_requests++;
#endif /* IFP_PERF */

	/*
	 * Turn completed flag off indicating mbox command was issued.
	 */
	ifp->ifp_mbox.mbox_flags &= ~IFP_MBOX_CMD_FLAGS_COMPLETE;

	/* signal the fw that mailbox cmd was loaded */
	IFP_REG_SET_HOST_INT(ifp);


	/* busy wait for mailbox cmd to be processed. */
	delay_loops = IFP_TIMEOUT_DELAY(mbox_cmdp->timeout,
	    IFP_MBOX_CMD_BUSY_POLL_DELAY_TIME);
	/*
	 * Keep polling the interrupt bit in the ISP2100.
	 */
	while ((ifp->ifp_mbox.mbox_flags & IFP_MBOX_CMD_FLAGS_COMPLETE) == 0) {

		drv_usecwait(IFP_MBOX_CMD_BUSY_POLL_DELAY_TIME);
		/* if cmd does not complete retry or return error */
		if (--delay_loops <= 0) {
			if (--retry_cnt <= 0) {
				rval = -2;
				ifp_i_log(ifp, CE_NOTE, "!mbox cmd %x timedout",
				    mbox_cmdp->mbox_out[0]);
#ifdef IFPDEBUG
				ifp_i_print_state(ifp);
#endif
				break;
			} else {
				goto retry;
			}
		}

		if (IFP_INT_PENDING(ifp) == 0)
			continue;

		ifp->ifp_alive = 1;

#ifdef IFPDEBUG
		if (ifp_mbox_debug) {
			ifp_i_log(ifp, CE_NOTE, "mbox_start:RISC intr!");
			ifp_i_print_state(ifp);
		}
#endif
		/*
		 * If it is not a mbox cmd completion interrupt, clear
		 * the RISC interrupt and keep polling. ISP2100 fw won't
		 * post mbox completions if the RISC interrupt is set.
		 * If it is completion interrupt for the mbox command
		 * we issued, then don't clear the RISC interrupt (for
		 * async events, RISC interrupt gets cleared in
		 * ifp_i_get_async_info()) -- since this is a level interrupt,
		 * the system will post an interrupt at an appropriate time,
		 * and response queue updates that happened while we were
		 * polling for mbox completion will be processed at that time.
		 * XXX could be a problem if the mbox command times out
		 */

		if (IFP_CHECK_SEMAPHORE_LOCK(ifp)) {

			disp = ifp_i_handle_mbox_return(ifp, &sp);

			if (disp == IFP_AEN_DMA_ERR ||
			    disp == IFP_AEN_SYS_ERR) {
				/*
				 * see comments in ifp_intr() reg
				 * handling_fatal_aen
				 */
				ifp->ifp_que_inited = 0;
				ifp->ifp_handling_fatal_aen++;

				mutex_exit(IFP_MBOX_MUTEX(ifp));

				ifp_i_handle_fatal_errors(ifp, disp,
				    &normalcmds, NULL);
				mutex_exit(IFP_RESP_MUTEX(ifp));
				goto done;
			} else if (disp == IFP_AEN_FAST_POST) {
				if (normalcmds.head) {
					normalcmds.tail->cmd_forw = sp;
					normalcmds.tail = sp;
				} else {
					normalcmds.head =
					    normalcmds.tail = sp;
				}
			} else if (disp == IFP_AEN_LIP) {
				aen_lips++;
			} else if (disp == IFP_AEN_RESET) {
				aen_resets++;
			}
		} else {
			IFP_CLEAR_RISC_INT(ifp);
		}
	}

	mutex_exit(IFP_RESP_MUTEX(ifp));

#ifdef IFPDEBUG
	if (ifp_mbox_debug && IFP_REG_GET_HOST_INT(ifp)) {
		ifp_i_log(ifp, CE_NOTE, "Gag! host int set while exiting %x",
		    mbox_cmdp->mbox_out[0]);
	}
#endif
	if (rval != -1) {

		/*
		 * Copy registers saved by ifp_i_mbox_cmd_complete()
		 * to mbox_cmdp
		 */
		for (i = 0; i < ifp->ifp_mbox.mbox_cmd.n_mbox_in; i++) {
			mbox_cmdp->mbox_in[i] =
			    ifp->ifp_mbox.mbox_cmd.mbox_in[i];
		}

		if ((mbox_cmdp->mbox_in[0] & IFP_MBOX_STATUS_MASK) !=
		    IFP_MBOX_STATUS_OK) {
			rval = -1;
		}

		if (rval || ifp_debug_mbox) {
			ifp_i_dump_mbox(ifp, mbox_cmdp,
			    (rval ? "failed" : "succeeded"));
		}
	}

	/*
	 * At this point, FW completed our mbox command or we timed out.
	 * We can drop the mutex to let others use the mbox.
	 */
	mutex_exit(IFP_MBOX_MUTEX(ifp));

	/*
	 * Deal with async events that potentially need to use mailbox now.
	 */
	mutex_enter(IFP_RESP_MUTEX(ifp));

	if (aen_resets) {
		if (ifp_i_handle_resets(ifp)) {
			mutex_exit(IFP_RESP_MUTEX(ifp));
			if (normalcmds.head)
				ifp_i_call_pkt_comp(normalcmds.head);
			mutex_enter(IFP_RESP_MUTEX(ifp));
			ifp_i_fatal_error(ifp, 0);
			mutex_exit(IFP_RESP_MUTEX(ifp));
			goto done;
		}
	}

	if (aen_lips) {
		if (ifp_i_process_lips(ifp)) {
			mutex_exit(IFP_RESP_MUTEX(ifp));
			if (normalcmds.head)
				ifp_i_call_pkt_comp(normalcmds.head);
			mutex_enter(IFP_RESP_MUTEX(ifp));
			ifp_i_fatal_error(ifp, 0);
			mutex_exit(IFP_RESP_MUTEX(ifp));
			goto done;
		}
	}

	mutex_exit(IFP_RESP_MUTEX(ifp));

	if (normalcmds.head)
		ifp_i_call_pkt_comp(normalcmds.head);

done:
	IFP_MUTEX_ENTER(ifp);

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_MBOX_CMD_START_END,
	    "ifp_i_mbox_cmd_start_end");

	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "ifp_i_mbox_cmd_start_end (cmd = 0x%x)",
	    mbox_cmdp->mbox_out[0]));

	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));

	ATRACE(ifp_i_mbox_cmd_start, 0xdddddda9, ifp);
	return (rval);
}

static uchar_t
ifp_i_issue_inquiry(struct ifp *ifp, struct ifp_target *target, ushort_t lun,
	int *try_later, struct scsi_inquiry *inq_data)
{
	uint_t			ccount;
	uchar_t			status;
	int			rval = 1;
	struct ifp_mbox_cmd	mbox_cmd;
	ddi_device_acc_attr_t	dev_attr;
	size_t			real_size;
	struct ifp_request	*req = NULL;
	struct ifp_response	*resp = NULL;
	ddi_acc_handle_t	inq_acc_handle;
	int			inq_pkt_bound = 0;
	int			inq_data_bound = 0;
	ddi_dma_handle_t	inq_pkt_handle = NULL;
	ddi_dma_handle_t	inq_data_handle = NULL;
	ddi_dma_cookie_t	inq_data_cookie, inq_pkt_cookie;
	uchar_t			inq_dtype = 0xff;

	struct inq_pkt {
		struct ifp_request req;
		struct ifp_response resp;
	} *inq_pkt_ptr;


	*try_later = 0;
	inq_pkt_ptr = NULL;

	dev_attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	dev_attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	dev_attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;

	req = kmem_zalloc(sizeof (struct ifp_request), KM_NOSLEEP);
	if (req == NULL)
		goto fail;

	resp = kmem_zalloc(sizeof (struct ifp_response), KM_NOSLEEP);
	if (resp == NULL)
		goto fail;

	if (ddi_dma_alloc_handle(ifp->ifp_dip, &dma_ifpattr,
	    DDI_DMA_DONTWAIT, NULL, &inq_pkt_handle) != DDI_SUCCESS) {
		goto fail;
	}

	if (ddi_dma_mem_alloc(inq_pkt_handle, sizeof (struct inq_pkt),
	    &dev_attr, DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, NULL, (caddr_t *)&inq_pkt_ptr,
	    &real_size, &inq_acc_handle) != DDI_SUCCESS) {
		goto fail;
	}

	if (ddi_dma_addr_bind_handle(inq_pkt_handle, NULL,
	    (caddr_t)inq_pkt_ptr, sizeof (struct inq_pkt), DDI_DMA_READ |
	    DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT,
	    NULL, &inq_pkt_cookie, &ccount) != DDI_DMA_MAPPED) {
		goto fail;
	}
	inq_pkt_bound = 1;

	if (ccount != 1) {
		goto fail;
	}

	if (ddi_dma_alloc_handle(ifp->ifp_dip, &dma_ifpattr,
	    DDI_DMA_DONTWAIT, NULL, &inq_data_handle) != DDI_SUCCESS) {
		goto fail;
	}

	if (ddi_dma_addr_bind_handle(inq_data_handle, NULL,
	    (caddr_t)inq_data, sizeof (struct scsi_inquiry), DDI_DMA_READ |
	    DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT,
	    NULL, &inq_data_cookie, &ccount) != DDI_DMA_MAPPED) {
		goto fail;
	}
	inq_data_bound = 1;

	if (ccount != 1) {
		goto fail;
	}

	req->req_header.cq_entry_type = CQ_TYPE_REQUEST;
	req->req_header.cq_entry_count = 1;
	req->req_header.cq_flags = 0;
	req->req_header.cq_seqno = 0;
	req->req_cdblen = 0;
	req->req_reserved = 0;
	req->req_target = target->ifpt_loop_id;
	req->req_lun_trn = 0xff & lun;
	req->req_ext_lun = lun;
	req->req_time = 60; /* seconds */

	bzero(req->req_cdb, sizeof (req->req_cdb));
	((union scsi_cdb *)req->req_cdb)->scc_cmd = SCMD_INQUIRY;
	((union scsi_cdb *)req->req_cdb)->g0_count0 = SUN_INQSIZE;

	req->req_flags = (IFP_REQ_FLAG_SIMPLE_TAG|IFP_REQ_FLAG_DATA_READ);
	req->req_seg_count = 1;
	req->req_byte_count = SUN_INQSIZE;
	req->req_dataseg[0].d_count = SUN_INQSIZE;
	req->req_dataseg[0].d_base = inq_data_cookie.dmac_address;

	IFP_COPY_OUT_REQ(inq_acc_handle, req, &inq_pkt_ptr->req);
	(void) ddi_dma_sync(inq_pkt_handle, 0,
	    (size_t)sizeof (struct ifp_request), DDI_DMA_SYNC_FORDEV);

again:
	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_i_issue_inquiry: target %x lun %x",
	    target->ifpt_loop_id, lun));

	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 4, 1,
	    IFP_MBOX_CMD_SCSI_CMD, sizeof (struct ifp_request),
	    MSW(inq_pkt_cookie.dmac_address),
	    LSW(inq_pkt_cookie.dmac_address), 0, 0, 0, 0);

#ifdef IFPDEBUG
	mbox_cmd.mbox_out[7] = target->ifpt_loop_id;
#endif
	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
		goto fail;
	}

	(void) ddi_dma_sync(inq_pkt_handle, (off_t)sizeof (struct ifp_request),
	    sizeof (struct ifp_response), DDI_DMA_SYNC_FORCPU);

	IFP_COPY_IN_RESP(inq_acc_handle, &inq_pkt_ptr->resp, resp);

	if (resp->resp_reason == IFP_CMD_PORT_LOGGED_OUT ||
	    resp->resp_reason == IFP_CMD_PORT_UNAVAIL ||
	    resp->resp_reason == IFP_CMD_PORT_CONFIG_CHANGED) {
		goto fail;
	}

	status = (uchar_t)resp->resp_scb;

	if (status == STATUS_BUSY || status == STATUS_CHECK ||
	    status == STATUS_QFULL) {
		*try_later = IFP_PROC_ALPA_INQUIRY_TIMED_OUT;
		rval = 0;
		IFP_DEBUG(2, (ifp, SCSI_DEBUG,
		    "INQUIRY failed for target %x lun %x dev type %x, status"
		    " %x", target->ifpt_loop_id, lun, inq_dtype, status));
	} else if (status == STATUS_GOOD) {
		inq_dtype = inq_data->inq_dtype;
		rval = 0;
		IFP_DEBUG(2, (ifp, SCSI_DEBUG,
		    "ifp_i_issue_inquiry: OK target %x lun %x dev type %x",
		    target->ifpt_loop_id, lun, inq_dtype));
	}

fail:
	if (rval) {
		mutex_enter(&target->ifpt_mutex);
		/* take it offline */
		target->ifpt_state |= IFPT_TARGET_MARK;
		mutex_exit(&target->ifpt_mutex);
	}

	if (inq_pkt_handle != NULL) {
		if (inq_pkt_bound)
			(void) ddi_dma_unbind_handle(inq_pkt_handle);
		ddi_dma_free_handle(&inq_pkt_handle);
	}
	if (inq_data_handle != NULL) {
		if (inq_data_bound)
			(void) ddi_dma_unbind_handle(inq_data_handle);
		ddi_dma_free_handle(&inq_data_handle);
	}
	if (inq_pkt_ptr != NULL) {
		ddi_dma_mem_free(&inq_acc_handle);
	}
	if (resp)
		kmem_free(resp, sizeof (struct ifp_response));
	if (req)
		kmem_free(req, sizeof (struct ifp_request));
	return (inq_dtype);
}

/*
 * Function name : ifp_i_watch()
 *
 * Return Values : none
 * Description	 :
 * Isp deadman timer routine.
 * A hung ifp controller is detected by failure to complete
 * cmds within a timeout interval (including grace period for
 * ifp error recovery).	 All target error recovery is handled
 * directly by the ifp.
 *
 * If ifp hung, restart by resetting the ifp and flushing the
 * crash protection queue (ifp_slots) via ifp_i_qflush.
 *
 * we check only 1/8 of the slots at the time; this is really only a sanity
 * check on ifp so we know when it dropped a packet. The ifp performs
 * timeout checking and recovery on the target
 * If the ifp dropped a packet then this is a fatal error
 *
 * Context	 : Can be called by timeout thread.
 */
#ifdef IFPDEBUG
static int ifp_test_abort;
static int ifp_test_abort_all;
static int ifp_test_reset;
static int ifp_test_reset_all;
static int ifp_test_fatal;
static int ifp_debug_enter;
static int ifp_debug_enter_count;
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp_test_abort))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp_test_abort_all))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp_test_reset))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp_test_reset_all))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp_test_fatal))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp_debug_enter))
_NOTE(MUTEX_PROTECTS_DATA(ifp::ifp_response_mutex, ifp_debug_enter_count))
#endif


/*ARGSUSED*/
static void
ifp_i_watch(void *dummy)
{
	clock_t local_lbolt;
	int one_ifp_tick = 0;
	struct ifp *ifp = ifp_head;
	static int nwatchdog_ticks = 0;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(nwatchdog_ticks))

	ATRACE(ifp_i_watch, 0xddddddb0, ifp);
	/*
	 * We want to check the loop OFFLINE condition and
	 * IFPL_NEED_LIP_FORCED requests every 10sec. Bump the number
	 * of watchdog ticks each time through this function and when
	 * we cross ifp_tick time, check the fw's health.
	 */
	if (nwatchdog_ticks >= watchdog_ticks_per_ifp_tick) {
		nwatchdog_ticks = 1;
		one_ifp_tick++;
	} else {
		nwatchdog_ticks++;
	}
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(nwatchdog_ticks))

	local_lbolt = ddi_get_lbolt();

	rw_enter(&ifp_global_rwlock, RW_READER);
	for (ifp = ifp_head; ifp != NULL; ifp = ifp->ifp_next) {
		ATRACE(ifp_i_watch, 0xddddddb1, ifp);

		if (ifp->ifp_shutdown)
			continue;

#ifdef STE_TARGET_MODE
		if (ifp->ifp_tm_hba_event)
			continue;
#endif /* STE_TARGET_MODE */
		ifp_i_loop_updates(ifp, local_lbolt);

		if (one_ifp_tick)
			ifp_i_watch_ifp(ifp);
	}
	rw_exit(&ifp_global_rwlock);

	mutex_enter(&ifp_global_mutex);
	/*
	 * If timeout_initted has been cleared then somebody
	 * is trying to untimeout() this thread so no point in
	 * reissuing another timeout.
	 */
	if (timeout_initted) {
		ASSERT(ifp_timeout_id);
		ifp_timeout_id = timeout(ifp_i_watch, (caddr_t)ifp,
		    watchdog_tick);
	}
	mutex_exit(&ifp_global_mutex);
	ATRACE(ifp_i_watch, 0xddddddb9, ifp);
}

static void
ifp_i_loop_updates(struct ifp *ifp, clock_t local_lbolt)
{
	int try_later;
	int forced_finish_init = 0;

	ATRACE(ifp_i_loop_updates, 0x55555520, ifp);
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());


	/*
	 * An OFFLINE state in the watchdog timeout indicates the
	 * loop did not come up after a FW init or never came back up
	 * after loop down. We need to call ifp_i_finish_init() to
	 * simulate an end of LIP handling.
	 *
	 * When a target sees status 0x28 or 0x29, it will set
	 * IFPL_NEED_LIP_FORCED; we need to force a LIP in this case. Don't
	 * bother forcing LIP if we are already handling a LIP & clear
	 * IFPL_NEED_LIP_FORCED if we are already handling a LIP.
	 */
	IFP_MUTEX_ENTER(ifp);
	ATRACE(ifp_i_loop_updates, 0x55555523, ifp->ifp_lip_state);
	if ((ifp->ifp_lip_state & IFPL_HANDLING_LIP) == 0) {
		if (ifp->ifp_lip_state & IFPL_NEED_LIP_FORCED) {
			IFP_DEBUG(1, (ifp, CE_NOTE, "ifp state: 0x%x, "
			    "lip state: 0x%x", ifp->ifp_state,
			    ifp->ifp_lip_state));
			if (ifp_i_force_lip(ifp) == 0) {
				ifp->ifp_lip_state &= ~IFPL_NEED_LIP_FORCED;
			}
		} else if (ifp->ifp_lip_state & IFPL_RETRY_SCAN_PORTDB) {
			IFP_DEBUG(1, (ifp, CE_NOTE, "ifp state: 0x%x, "
			    "lip state: 0x%x", ifp->ifp_state,
			    ifp->ifp_lip_state));
			if (ifp_i_scan_portdb(ifp, ifp->ifp_loop_map,
			    ifp->ifp_my_alpa, ifp->ifp_lip_cnt, &try_later)
			    == 0) {
				if (try_later && ifp->ifp_scandb_retry
				    < IFP_RETRIES) {
					ifp->ifp_scandb_retry++;
				} else {
					ifp_i_finish_init(ifp,
					    ifp->ifp_lip_cnt);
					ifp_i_log(ifp, CE_NOTE,
					    "!Loop reconfigure done, daemon");
					ifp->ifp_scandb_retry = 0;
					ifp->ifp_lip_state &=
					    ~IFPL_RETRY_SCAN_PORTDB;
				}
			}
		} else if (ifp->ifp_state == IFP_STATE_OFFLINE &&
		    (ifp->ifp_loopdown_timeout - local_lbolt < 0)) {

			ifp_i_log(ifp, CE_NOTE,
			    "!Loop is down: finishing off loop reconfig");

			ifp_i_finish_init(ifp, ifp->ifp_lip_cnt);
			ifp_i_log(ifp, CE_NOTE, "!Loop reconfigure done, LIP");
			ifp->ifp_state = IFP_STATE_FORCED_FINISH_INIT;
			forced_finish_init++;
		}
	} else {
		ifp->ifp_lip_state &= ~IFPL_NEED_LIP_FORCED;
	}
	if (forced_finish_init) {
		mutex_exit(IFP_RESP_MUTEX(ifp));
		IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);
		ATRACE(ifp_i_loop_updates, 0x55555528, ifp);
	} else {
		IFP_MUTEX_EXIT(ifp);
		ATRACE(ifp_i_loop_updates, 0x55555529, ifp);
	}
}

static void
ifp_i_watch_ifp(struct ifp *ifp)
{
	ushort_t slot;
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());

	ATRACE(ifp_i_watch_ifp, 0xddddddc0, ifp);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_WATCH_START, "isp_i_watch_start");


#ifdef IFP_PERF
	ifp->ifp_perf_ticks += ifp_scsi_watchdog_tick;

	if (ifp->ifp_request_count >= 20000) {
		ifp_i_log(ifp, CE_NOTE,
	"%d reqs/sec (ticks=%d, intr=%d, req=%d, rpio=%d, wpio=%d)",
		    ifp->ifp_request_count/ifp->ifp_perf_ticks,
		    ifp->ifp_perf_ticks,
		    ifp->ifp_intr_count, ifp->ifp_request_count,
		    ifp->ifp_rpio_count, ifp->ifp_wpio_count);

		ifp->ifp_request_count = ifp->ifp_perf_ticks = 0;
		ifp->ifp_intr_count = 0;
		ifp->ifp_rpio_count = ifp->ifp_wpio_count = 0;
	}
#endif /* IFP_PERF */

	if (!ifp->ifp_alive || !IFP_INT_PENDING(ifp)) {

		if (!ifp_i_alive(ifp)) {
			ifp_i_log(ifp, CE_WARN, "watchdog cmd timedout");
			mutex_enter(IFP_RESP_MUTEX(ifp));
			ifp->ifp_que_inited = 0;
			ifp_i_fatal_error(ifp, 0);
			mutex_exit(IFP_RESP_MUTEX(ifp));
		}
	}

	ifp->ifp_alive = 0;

#ifdef IFPDEBUG
	mutex_enter(IFP_RESP_MUTEX(ifp));
	mutex_enter(IFP_REQ_MUTEX(ifp));
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	for (slot = 0; slot < IFP_MAX_SLOTS; slot++) {
		struct ifp_cmd *sp = ifp->ifp_slots[slot].slot_cmd;

		/*
		 * We don't want to inject fatal error resets
		 * if there is an interrupt pending as there
		 * a race between flushing commands as part
		 * of the fatal reset and commands for which
		 * isp is already posting completions.
		 */
		if (ifp_test_fatal && IFP_INT_PENDING(ifp))
			break;

		if (sp) {
			ifp_i_test(ifp, sp);
			break;
		}
	}
	if (ifp_debug_enter && ifp_debug_enter_count) {
		debug_enter("ifp_i_watch");
		ifp_debug_enter = 0;
	}
	mutex_exit(IFP_REQ_MUTEX(ifp));
	mutex_exit(IFP_RESP_MUTEX(ifp));
#endif

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_WATCH_END, "ifp_i_watch_end");

	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ATRACE(ifp_i_watch_ifp, 0xddddddc9, ifp);
}

static int
ifp_i_alive(struct ifp *ifp)
{
	ushort_t rval = FALSE;
	ushort_t	total_exe_io;
	ushort_t	total_queued_io;
	ushort_t	total_io_completion;
	struct ifp_mbox_cmd mbox_cmd;

	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 1, 4,
	    IFP_MBOX_CMD_GET_IFP_STAT, 0, 0, 0, 0, 0, 0, 0);
	IFP_MUTEX_ENTER(ifp);

	if (ifp->ifp_running_diags) {
		rval = TRUE;
		IFP_MUTEX_EXIT(ifp);
		return (rval);
	}

	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
		if (MBOX_ACCEPTABLE_STATUS(ifp, mbox_cmd.mbox_in[0]))
			rval = TRUE;
		goto fail;
	}

	total_io_completion = mbox_cmd.mbox_in[1];
	total_queued_io = mbox_cmd.mbox_in[3];
	total_exe_io = mbox_cmd.mbox_in[2];

#ifdef IFPDEBUG
	if (ifp_timeout_debug) {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG, "total_queued_io=%d,"
		"total_io_completion=%d, total_exe_io=%d",
		    total_queued_io, total_io_completion, total_exe_io));
	}
#endif

	if ((total_io_completion == 0) && (total_exe_io == 0) &&
	    (total_queued_io != 0)) {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG, "total_queued_io=%d",
		    total_queued_io));
	} else {
		rval = TRUE;
	}
fail:
	mutex_exit(IFP_RESP_MUTEX(ifp));
	IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);
	return (rval);
}

/*
 * Function name : ifp_i_fatal_error()
 *
 * Return Values :  none
 *
 * Description	 :
 * Isp fatal error recovery:
 * Reset the ifp and flush the active queues and attempt restart.
 * This should only happen in case of a firmware bug or hardware
 * death.  Flushing is from backup queue as IFP cannot be trusted.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
ifp_i_fatal_error(struct ifp *ifp, int flags)
{
	ATRACE(ifp_i_fatal_error, 0xfffffff0, ifp);
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());

	/*
	 * If shutdown flag is set than no need to do
	 * fatal error recovery.
	 */
	if (ifp->ifp_shutdown || ifp->ifp_running_diags) {
#ifdef IFPDEBUG
		ifp_i_log(ifp, CE_WARN, "ifp_i_fatal_error();shutdown set");
#endif
		return;
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_TIMEOUT_START,
	    "ifp_i_fatal_error_start");

	ifp_i_log(ifp, CE_WARN, "Fatal error, resetting interface");

	/*
	 * hold off starting new requests by grabbing the request
	 * mutex
	 */
	mutex_enter(IFP_REQ_MUTEX(ifp));

	ifp_i_print_state(ifp);

#ifdef IFPDEBUG
	if (ifp_enable_brk_fatal) {
		char buf[128];
		char path[128];
		(void) sprintf(buf,
		"ifp_i_fatal_error: You can now look at %s",
		    ddi_pathname(ifp->ifp_dip, path));
		debug_enter(buf);
	}
#endif

	(void) ifp_i_reset_interface(ifp, flags);

	ifp_i_qflush(ifp);

	mutex_exit(IFP_REQ_MUTEX(ifp));
	(void) scsi_hba_reset_notify_callback(IFP_RESP_MUTEX(ifp),
	    &ifp->ifp_reset_notify_listf);
	mutex_enter(IFP_REQ_MUTEX(ifp));

	mutex_exit(IFP_RESP_MUTEX(ifp));
	IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_TIMEOUT_END, "ifp_i_fatal_error_end");

	mutex_enter(IFP_RESP_MUTEX(ifp));

	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ATRACE(ifp_i_fatal_error, 0xfffffff9, ifp);
}


/*
 * Function name : ifp_i_qflush()
 *
 * Return Values : none
 * Description	 :
 *	Flush ifp queues  over range specified
 *	from start_tgt to end_tgt.  Flushing goes from oldest to newest
 *	to preserve some cmd ordering.
 *	This is used for ifp crash recovery as normally ifp takes
 *	care of target or bus problems.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
ifp_i_qflush(struct ifp *ifp)
{
	ushort_t slot;
	int i, n = 0;
	struct ifp_cmd *sp;
	struct ifp_response *resp;
	struct ifp_cmd *head, *tail;


	ATRACE(ifp_i_qflush, 0x22222200, ifp);
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_QFLUSH_START,
	    "ifp_i_qflush_start");

	head = tail = NULL;

	/*
	 * If flushing active queue, start with current free slot
	 * ie. oldest request, to preserve some order.
	 */
	slot = ifp->ifp_free_slot;

	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	for (i = 0; i < IFP_MAX_SLOTS; i++) {

		sp = ifp->ifp_slots[slot].slot_cmd;
		if (sp) {
			ifp->ifp_slots[slot].slot_cmd = NULL;
			resp = &sp->cmd_ifp_response;
			bzero(resp, sizeof (struct ifp_response));

			ATRACE(ifp_i_qflush, 0x22222201, sp);
			ATRACE(ifp_i_qflush, 0x22222202, sp->cmd_pkt);
			/*
			 * update stats in resp_status_flags
			 * ifp_i_call_pkt_comp() copies this
			 * over to pkt_statistics
			 */
			resp->resp_status_flags =
			    IFP_STAT_BUS_RESET | IFP_STAT_ABORTED;
			resp->resp_reason = CMD_RESET;
#ifdef IFPDEBUG
			resp->resp_token = (uint_t)0xf00df00d;

			sp->cmd_flags |= CFLAG_FINISHED;
#endif
			/*
			 * queue up sp
			 * we don't want to do a callback yet
			 * until we have flushed everything and
			 * can release the mutexes
			 */
			n++;
			if (head) {
				tail->cmd_forw = sp;
				tail = sp;
				tail->cmd_forw = NULL;
			} else {
				tail = head = sp;
				sp->cmd_forw = NULL;
			}
		}

		/*
		 * Wraparound
		 */
		if (++slot >= IFP_MAX_SLOTS) {
			slot = 0;
		}
	}

	/*
	 * XXX we don't worry about the waitQ since we cannot
	 * guarantee order anyway.
	 */
	if (head) {
		struct ifp_cmd *tmphead, *sp;
		tmphead = head;

		/*
		 * if we would	hold the REQ mutex and	the target driver
		 * decides to do a scsi_reset(), we will get a recursive
		 * mutex failure in ifp_i_set_marker
		 */
		IFP_DEBUG(3, (ifp, SCSI_DEBUG, "ifp_i_qflush: %d flushed", n));
		IFP_MUTEX_EXIT(ifp);
		ifp_i_call_pkt_comp(head);
		IFP_MUTEX_ENTER(ifp);

#ifdef IFPDEBUG
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(ifp->ifp_waitf))
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(ifp->ifp_waitb))
		if (ifpdebug > 2) {
			while (tmphead) {
				sp = tmphead;
				IFP_DEBUG(3, (ifp, SCSI_DEBUG,
				    "flushed 0x%p", (void *)sp));
				tmphead = sp->cmd_forw;
			}
			ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
			for (slot = 0; slot < IFP_MAX_SLOTS; slot++) {
				sp = ifp->ifp_slots[slot].slot_cmd;
				if (sp) {
					struct scsi_pkt *pkt = CMD2PKT(sp);

					IFP_DEBUG(3, (ifp, SCSI_DEBUG,
					    "slot %x pkt 0x%p time %x",
					    slot, (void *)pkt,
					    pkt->pkt_time));
				}
			}
			IFP_DEBUG(3, (ifp, SCSI_DEBUG, "waitf 0x%p waitb 0x%p",
			    (void *)ifp->ifp_waitf,
			    (void *)ifp->ifp_waitb));
		}
		_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(ifp->ifp_waitf))
		_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(ifp->ifp_waitb))
#endif
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_QFLUSH_END,
	    "ifp_i_qflush_end");

	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ATRACE(ifp_i_qflush, 0x22222209, ifp);
}

/*
 * Function name : ifp_i_set_marker()
 *
 * Return Values : none
 * Description	 :
 * Send marker request to unlock the request queue for a given target/lun
 * nexus.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
/*ARGSUSED*/
static int
ifp_i_set_marker(struct ifp *ifp, short mod, short tgt, short lun)
{
	struct ifp_request *req;
	struct ifp_request req_buf;


	ATRACE(ifp_i_set_marker, 0x22222210, ifp);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_SET_MARKER_START,
	    "ifp_i_set_marker_start");

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());

	mutex_enter(IFP_REQ_MUTEX(ifp));

	/*
	 * Check to see how much space is available in the
	 * Request Queue, save this so we do not have to do
	 * a lot of PIOs
	 */
	if (ifp->ifp_queue_space == 0) {
		IFP_UPDATE_QUEUE_SPACE(ifp);

		/*
		 * Check now to see if the queue is still full
		 */
		if (ifp->ifp_queue_space == 0) {
			IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);
			return (-1);
		}
	}

	bzero((void *)&req_buf, sizeof (struct ifp_request));
	req_buf.req_header.cq_entry_type = CQ_TYPE_MARKER;
	req_buf.req_header.cq_entry_count = 1;
	req_buf.req_header.cq_flags = 0;
	req_buf.req_header.cq_seqno = 0;
	req_buf.req_target = (uchar_t)tgt;
	req_buf.req_lun_trn = (uchar_t)lun;
	req_buf.req_ext_lun = lun;
	req_buf.req_modifier = mod;
	IFP_GET_NEXT_REQUEST_IN(ifp, req);
	IFP_COPY_OUT_REQ(ifp->ifp_dma_acc_handle, &req_buf, req);

	/*
	 * Use correct offset and size for syncing
	 */
	(void) ddi_dma_sync(ifp->ifp_dmahandle,
	    (off_t)(((ifp->ifp_request_in == 0) ? (IFP_MAX_REQUESTS - 1) :
	    (ifp->ifp_request_in - 1)) * sizeof (struct ifp_request)),
	    (size_t)sizeof (struct ifp_request),
	    DDI_DMA_SYNC_FORDEV);

	/*
	 * Tell isp it's got a new I/O request...
	 */
	IFP_SET_REQUEST_IN(ifp);
	IFP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(ifp);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_SET_MARKER_END,
	    "ifp_i_set_marker_end");

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)) == 0 || ddi_in_panic());
	ATRACE(ifp_i_set_marker, 0x22222219, ifp);
	return (0);
}

/*
 * Function name : ifp_i_reset_interface()
 *
 * Return Values : 0 - success
 *		  -1 - hw failure
 *
 * Description	 :
 * Master reset routine for hardware/software.	Resets softc struct,
 * ifp chip, and the FC-AL. The works!
 * This function is called from ifp_attach with no mutexes held or
 * from ifp_i_fatal_error with response and request mutex held
 *
 * NOTE: it is up to the caller to flush the response queue and ifp_slots
 *	 list
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
ifp_i_reset_interface(struct ifp *ifp, int action)
{
	int i;
	int mbox_ret, rval = -1;
	la_wwn_t mywwn;
	int delay_loops;
	struct ifp_icb *icb;
	clock_t local_lbolt;
	struct ifp_target *target;
	struct ifp_mbox_cmd mbox_cmd;


	ATRACE(ifp_i_reset_interface, 0x22222250, ifp);
	TRACE_1(TR_FAC_SCSI_ISP, TR_ISP_I_RESET_INTERFACE_START,
	    "ifp_i_reset_interface_start (action = %x)", action);

	/*
	 * If a firmware error is seen do not trust the firmware and do not
	 * issue any mailbox commands. Don't bother doing this if the
	 * HBA hasn't been on the loop yet -- if LIP hasn't taken place,
	 * we cannot queue commands for device on the loop, anyway.
	 */
	if ((action & IFP_FIRMWARE_ERROR) != IFP_FIRMWARE_ERROR &&
	    ifp->ifp_state == IFP_STATE_ONLINE) {

		/*
		 * Reset the SCSI bus to blow away all the commands
		 * under process
		 */
		if (action & IFP_FORCE_RESET_BUS) {

			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			    "ifp_scsi_reset: reset bus"));

			ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 2, 2,
			    IFP_MBOX_CMD_BUS_RESET,
			    (ushort_t)(ifp->ifp_scsi_reset_delay/1000),
			    0, 0, 0, 0, 0, 0);

			/*
			 * If this fails, just let it fall through. We
			 * are going to reset the hardware below anyway --
			 * it ought to recover fw timeouts and/or set
			 * ifp_shutdown if things are really hosed.
			 */
			if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd) == 0) {
				mutex_exit(IFP_REQ_MUTEX(ifp));
				(void) scsi_hba_reset_notify_callback
				    (IFP_RESP_MUTEX(ifp),
				    &ifp->ifp_reset_notify_listf);
				mutex_enter(IFP_REQ_MUTEX(ifp));
				drv_usecwait((clock_t)ifp->ifp_scsi_reset_delay
				    * 1000);
			}
		}
	}

	/*
	 * Walk through all targets and mark them busy -- the LIP that
	 * occurs as part of fw initialization will clear the busy.
	 */
	for (i = 0; i < IFP_MAX_TARGETS; i++) {
		target = ifp->ifp_targets[i];
		if (target != NULL) {
			mutex_enter(&target->ifpt_mutex);
			if (!(target->ifpt_state & IFPT_TARGET_OFFLINE))
				target->ifpt_state |= (IFPT_TARGET_BUSY
				    | IFPT_TARGET_MARK);
			mutex_exit(&target->ifpt_mutex);
		}
	}

	/* Put the Risc in pause mode */
	IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_PAUSE);

	if (action & IFP_DOWNLOAD_FW_ON_ERR) {

		/*
		 * If user wants firmware to be downloaded
		 */
		rval = ifp_i_download_fw(ifp);
		if (rval)
			goto fail;
	} else {

#ifdef IFPDEBUG
		ifp_i_log(ifp, CE_NOTE,
		    "?Calling ifp_i_reset_init: resp in %x resp out %x",
		    ifp->ifp_response_in, ifp->ifp_response_out);
#endif
		if (ifp_i_reset_init_chip(ifp))
			goto fail;

		/*
		 * Start firmware up.
		 */
		ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 2, 1,
		    IFP_MBOX_CMD_START_FW, ifp_risc_code_addr,
		    0, 0, 0, 0, 0, 0);
#ifdef IFPDEBUG
		ifp_i_log(ifp, CE_NOTE,
		    "?Calling start fw: resp in %x resp out %x",
		    ifp->ifp_response_in, ifp->ifp_response_out);
#endif
		if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd))
			goto fail;
	}

	/*
	 * We should be able to handle any target driver initiated mbox
	 * commands now. So, clear ifp_handling_fatal_aen.
	 */
	ifp->ifp_handling_fatal_aen = 0;

	/*
	 * Initialize request and response queue indexes.
	 */
	ifp->ifp_request_in  = ifp->ifp_request_out = 0;
	ifp->ifp_request_ptr = ifp->ifp_request_base;

	ifp->ifp_response_in  = ifp->ifp_response_out = 0;
	ifp->ifp_response_ptr = ifp->ifp_response_base;

#ifdef IFPDEBUG
	bzero(ifp->ifp_request_base, IFP_QUEUE_SIZE);
#endif

	icb = &ifp->ifp_icb;

	icb->icb_version = ICB_VERSION;

	icb->icb_fw_options =
	    (ICB_FAIRNESS|ICB_ENABLE_ADISC|ICB_ENABLE_PDB_UPDATE_EVENT|
	    ICB_USE_NODE_NAME|ICB_DESCENDING_LOOP_ID);

	if (ifp_fast_post) {
		icb->icb_fw_options |= ICB_FAST_STATUS;
	}

	if (ifp_plogi_on_lip) {
		icb->icb_fw_options |= ICB_ENABLE_FULL_LOGIN_ON_LIP;
	}

	icb->icb_max_frame_length = ifp_frame_size;
	icb->icb_max_resource_allocation = 256;
	icb->icb_execution_throttle = ifp_execution_throttle;
	icb->icb_retry_delay = IFP_RETRY_DELAY;
	icb->icb_retry_count = IFP_RETRIES;

#ifdef STE_TARGET_MODE
	if (ifp->ifp_tm_hba_event) {
		icb->icb_fw_options &= ~ICB_ENABLE_FULL_LOGIN_ON_LIP;
		icb->icb_fw_options |= ICB_TARGET_MODE_ENABLE |
		    ICB_INITIATOR_MODE_DISABLE;

		if (ifp->ifp_tm_hard_loop_id != 0xff) {
			icb->icb_fw_options |= ICB_HARD_ADDRESS_ENABLE;
			icb->icb_hard_address = ifp->ifp_tm_hard_loop_id;
		}
	}
#endif /* STE_TARGET_MODE */

	mywwn = ifp->ifp_my_wwn;
	mywwn.w.nport_id = (ddi_get_instance(ifp->ifp_dip) + 1) << 8;

	ifp->ifp_my_port_wwn = mywwn;

	icb->icb_request_out = ifp->ifp_request_in;
	icb->icb_response_in = ifp->ifp_response_out;
	icb->icb_request_q_length = IFP_MAX_REQUESTS;
	icb->icb_response_q_length = IFP_MAX_RESPONSES;
	icb->icb_request_q_addr[0] = LSW(ifp->ifp_request_dvma);
	icb->icb_request_q_addr[1] = MSW(ifp->ifp_request_dvma);
	icb->icb_request_q_addr[2] = 0;
	icb->icb_request_q_addr[3] = 0;
	icb->icb_response_q_addr[0] = LSW(ifp->ifp_response_dvma);
	icb->icb_response_q_addr[1] = MSW(ifp->ifp_response_dvma);
	icb->icb_response_q_addr[2] = 0;
	icb->icb_response_q_addr[3] = 0;

	IFP_COPY_OUT_DMA_16(ifp->ifp_dma_acc_handle, (ushort_t *)icb,
	    ifp->ifp_request_base,
	    sizeof (struct ifp_icb) / sizeof (ushort_t));

	bcopy(&ifp->ifp_my_wwn,
	    ((struct ifp_icb *)ifp->ifp_request_base)->icb_node_name,
	    FC_WWN_SIZE);

	bcopy(&ifp->ifp_my_port_wwn,
	    ((struct ifp_icb *)ifp->ifp_request_base)->icb_port_name,
	    FC_WWN_SIZE);

	local_lbolt = ddi_get_lbolt();

	ifp->ifp_state = IFP_STATE_OFFLINE;

	ifp->ifp_lip_state |= IFPL_EXPECTING_LIP;

	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 8, 6,
	    IFP_MBOX_CMD_INIT_FIRMWARE, 0, MSW(ifp->ifp_request_dvma),
	    LSW(ifp->ifp_request_dvma), (ushort_t)ifp->ifp_request_in,
	    (ushort_t)ifp->ifp_response_out, 0, 0);
	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
		ifp->ifp_lip_state &= ~IFPL_EXPECTING_LIP;
		goto fail;
	}

#ifdef IFPDEBUG
	ifp_i_log(ifp, CE_NOTE, "?Init fw done : resp in %x resp out %x",
	    ifp->ifp_response_in, ifp->ifp_response_out);
#endif

	ifp->ifp_que_inited++;
	ifp->ifp_last_slot_watched = 0;

	/*
	 * Get IFP RAM firmware version numbers
	 */
	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 1, 4,
	    IFP_MBOX_CMD_ABOUT_PROM, 0, 0, 0, 0, 0, 0, 0);
	if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
		goto fail;
	}

	ifp->ifp_major_rev = mbox_cmd.mbox_in[1];
	ifp->ifp_minor_rev = mbox_cmd.mbox_in[2];
	ifp->ifp_subminor_rev = mbox_cmd.mbox_in[3];

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "Firmware Version: major = %d, minor = %d, subminor = %d",
	    ifp->ifp_major_rev, ifp->ifp_minor_rev, ifp->ifp_subminor_rev));

	rval = 0;

	/*
	 * INIT_FIRMWARE does a LIP automatically --
	 * poll for FW to go ready to know LIP is done
	 */
	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 1, 2,
	    IFP_MBOX_CMD_GET_FW_STATE, 0, 0, 0, 0, 0, 0, 0);

	if (ifp_soft_reset_wait_retries == 0)
		ifp_soft_reset_wait_retries = IFP_SOFT_RESET_WAIT_RETRIES;

	delay_loops = ifp_soft_reset_wait_retries;

	/*
	 * If there is a working loop, fw should go to ready state in
	 * less than 2 seconds or so. Each iteration through the while
	 * loop below takes about 1.5ms. A delay_loops value of 2500 gives
	 * almost 3.75 seconds for the fw to go ready.
	 *
	 * XXXX on a loop with lots of errors and several nodes, it
	 * can take almost 1 minute to finish the PLOGI/PRLI/PDISC/ADISC
	 * sequence. If we are booting from such nodes, the boot will
	 * be unnecessarily freeze while we are spinning here for the
	 * fw to go ready. A better approach would be not spin here (if
	 * we are coming here from an attach) and block on first attach
	 * of any target driver in ifp_i_tgt_init. XXXX
	 */
	while (delay_loops--) {
		/* IFP_RESET_WAIT is in milliseconds */
		drv_usecwait(IFP_RESET_WAIT);

		/*
		 * Get IFP Firmware State
		 */
		if ((mbox_ret = ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) != 0) {
			/*
			 * If it a mbox timeout, we have already waited
			 * long enough.
			 */
			if ((mbox_ret == -2) || (delay_loops == 0)) {
				if (ifp->ifp_state == IFP_STATE_OFFLINE) {
					ifp->ifp_loopdown_timeout =
					    local_lbolt + IFP_LOOPDOWN_TIME;
				}
				break;
			}
			continue;
		}

		if (mbox_cmd.mbox_in[1] == IFP_FW_READY) {
			IFP_DEBUG(3, (ifp, SCSI_DEBUG, "fw ready"));
			break;
		} else if (mbox_cmd.mbox_in[1] == IFP_FW_LOSS_OF_SYNC &&
		    (action & IFP_DONT_WAIT_FOR_FW_READY)) {
			IFP_DEBUG(3, (ifp, SCSI_DEBUG, "No loop"));
			ifp->ifp_state = IFP_STATE_OFFLINE;
			ifp->ifp_loopdown_timeout =
			    local_lbolt + IFP_LOOPDOWN_TIME;
			break;
		} else if (delay_loops == 0 && ifp->ifp_state ==
		    IFP_STATE_OFFLINE) {
			ifp->ifp_loopdown_timeout =
			    local_lbolt + IFP_LOOPDOWN_TIME;
		}
	}
fail:
	if (rval) {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG, "reset interface failed"));

		ifp->ifp_que_inited = 0;
		ifp->ifp_handling_fatal_aen = 0;

		ifp->ifp_shutdown = 1;

		ifp_i_log(ifp, CE_WARN, "interface going offline");
#ifdef IFPDEBUG
		debug_enter("ifp_shutdown set");
#endif

		/*
		 * put register set in risc mode in case the
		 * reset didn't complete
		 */
		ifp->ifp_polled_intr = 1;
		IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icr,
		    IFP_BUS_ICR_DISABLE_INTS);
		IFP_CLEAR_RISC_INT(ifp);
		IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_PAUSE);
		IFP_WRITE_BIU_REG(ifp,
		    &ifp->ifp_biu_reg->ifp_risc_reg.ifp_risc_psr,
		    IFP_RISC_PSR_FORCE_TRUE | IFP_RISC_PSR_LOOP_COUNT_DONE);
		IFP_WRITE_RISC_REG(ifp,
		    &ifp->ifp_biu_reg->ifp_risc_reg.ifp_risc_pcr,
		    IFP_RISC_PCR_RESTORE_PCR);
		/* XXX what about commands that are on the waitQ? */
		ifp_i_qflush(ifp);
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_RESET_INTERFACE_END,
	    "ifp_i_reset_interface_end");

	ATRACE(ifp_i_reset_interface, 0x22222259, ifp);
	return (rval);
}

/*
 * Function name : ifp_i_reset_init_chip()
 *
 * Return Values : 0 - success
 *		  -1 - hw failure
 *
 * Description	 :
 * Reset the IFP chip and perform BIU initializations. Also enable interrupts.
 * It is assumed that EXTBOOT will be strobed low after reset.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called at attach time.
 */
static int
ifp_i_reset_init_chip(struct ifp *ifp)
{
	int rval = -1;
	int delay_loops;
	ushort_t ifp_conf_comm;


	ATRACE(ifp_i_reset_init_chip, 0x22222260, ifp);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_RESET_INIT_CHIP_START,
	    "ifp_i_reset_init_chip_start");

	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "ifp_i_reset_init_chip"));

	/*
	 * we want to respect framework's setting of PCI
	 * configuration space command register and also
	 * want to make sure that all bits of interest to us
	 * are properly set in command register.
	 */
	ifp_conf_comm = pci_config_get16(ifp->ifp_pci_config_acc_handle,
	    PCI_CONF_COMM);
	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "PCI conf command register was 0x%x", ifp_conf_comm));
	ifp_conf_comm |= PCI_COMM_IO | PCI_COMM_MAE | PCI_COMM_ME |
	    PCI_COMM_MEMWR_INVAL | PCI_COMM_PARITY_DETECT |
	    PCI_COMM_SERR_ENABLE;
	IFP_DEBUG(2, (ifp, SCSI_DEBUG, "PCI conf command register is 0x%x",
	    ifp_conf_comm));
	pci_config_put16(ifp->ifp_pci_config_acc_handle,
	    PCI_CONF_COMM, ifp_conf_comm);

	/*
	 * set cache line & latency register in pci configuration
	 * space. line register is set in units of 32-bit words.
	 */
	pci_config_put8(ifp->ifp_pci_config_acc_handle,
	    PCI_CONF_CACHE_LINESZ, (uchar_t)ifp_conf_cache_linesz);
	pci_config_put8(ifp->ifp_pci_config_acc_handle,
	    PCI_CONF_LATENCY_TIMER,
	    (uchar_t)ifp_conf_latency_timer);

	ifp->ifp_que_inited = 0;

	/*
	 * reset the isp
	 */
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icsr,
	    IFP_BUS_ICSR_SOFT_RESET);
	/*
	 * we need to wait a bit before touching the chip again,
	 * otherwise problems show up on fast machines.
	 */
	drv_usecwait(IFP_CHIP_RESET_BUSY_WAIT_TIME);
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_tdma_control,
	    IFP_TDMA_CON_RESET_INT | IFP_TDMA_CON_CLEAR_DMA_CHAN);
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_rdma_control,
	    IFP_RDMA_CON_RESET_INT | IFP_RDMA_CON_CLEAR_DMA_CHAN);
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_cdma_control,
	    IFP_CDMA_CON_RESET_INT | IFP_CDMA_CON_CLEAR_DMA_CHAN);

	/*
	 * wait for isp to fire up.
	 */
	delay_loops = IFP_TIMEOUT_DELAY(IFP_SOFT_RESET_TIME,
	    IFP_CHIP_RESET_BUSY_WAIT_TIME);
	while (IFP_READ_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icsr) &
	    IFP_BUS_ICSR_SOFT_RESET) {
		drv_usecwait(IFP_CHIP_RESET_BUSY_WAIT_TIME);
		if (--delay_loops < 0) {
			ifp_i_log(ifp, CE_WARN, "Chip reset timeout");
			goto fail;
		}
	}

	/* XXX - do we need this? Reset value is 0 for all the fields */
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icsr, 0);

	/*
	 * reset the risc processor
	 */
	IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_RESET);
	drv_usecwait(IFP_CHIP_RESET_BUSY_WAIT_TIME);

	/*
	 * clear the risc processor
	 */
	IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_RESET);
	drv_usecwait(IFP_CHIP_RESET_BUSY_WAIT_TIME);

	/* DRAM timing */
	IFP_WRITE_RISC_REG(ifp, &ifp->ifp_biu_reg->ifp_risc_reg.ifp_risc_mtr,
	    IFP_RISC_MTR_PAGE0_DEFAULT | IFP_RISC_MTR_PAGE1_DEFAULT);


	/*
	 * It is possible that the hardware already scheduled
	 * an interrupt when we reset the chip. Treat is an interrupt
	 * that is already processed should it occur.
	 */
	ifp->ifp_polled_intr = 1;

	/*
	 * make sure that BIOS is disabled
	 */
	IFP_WRITE_RISC_HCCR(ifp, IFP_PCI_HCCR_CMD_BIOS);

	/*
	 * release the RISC to resume operations
	 */
	IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_RELEASE);

	/*
	 * enable RISC interrupt
	 */
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icr,
	    IFP_BUS_ICR_ENABLE_RISC_INT | IFP_BUS_ICR_ENABLE_INTS);

#ifdef IFPDEBUG
	if (ifpdebug > 0) {
		ifp_i_print_state(ifp);
	}
#endif

	rval = 0;

fail:
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_RESET_INIT_CHIP_END,
	    "ifp_i_reset_init_chip_end");
	ATRACE(ifp_i_reset_init_chip, 0x22222269, ifp);
	return (rval);
}

#include <sys/varargs.h>

/*
 * Error logging, printing, and debug print routines
 */
/*PRINTFLIKE3*/
static void
ifp_i_log(struct ifp *ifp, int level, char *fmt, ...)
{
	va_list ap;
	dev_info_t *dip;

	ASSERT(mutex_owned(&ifp_log_mutex) == 0 || ddi_in_panic());

	if (ifp) {
		dip = ifp->ifp_dip;
	} else {
		dip = 0;
	}

	mutex_enter(&ifp_log_mutex);
	va_start(ap, fmt);
	(void) vsprintf(ifp_log_buf, fmt, ap);
	va_end(ap);

	if (level == CE_WARN) {
		scsi_log(dip, "ifp", level, "%s", ifp_log_buf);
	} else {
		scsi_log(dip, "ifp", level, "%s\n", ifp_log_buf);
	}
	mutex_exit(&ifp_log_mutex);
}

static void
ifp_i_print_state(struct ifp *ifp)
{
	/* XXX should print address regs 2 and 3 for 64 bit xfers */
	int i;
	char buf[128];
	char risc_paused = 0;
	struct ifp_biu_regs *ifp_biu = ifp->ifp_biu_reg;
	struct ifp_risc_regs *ifp_risc = &ifp->ifp_biu_reg->ifp_risc_reg;

	/* Put ifp header in buffer for later messages. */
	ifp_i_log(ifp, CE_NOTE, "!State dump from ifp registers and driver:");

	(void) sprintf(buf,
	    "!out box(0-7):0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
	    ifp->ifp_mbox.mbox_cmd.mbox_out[0],
	    ifp->ifp_mbox.mbox_cmd.mbox_out[1],
	    ifp->ifp_mbox.mbox_cmd.mbox_out[2],
	    ifp->ifp_mbox.mbox_cmd.mbox_out[3],
	    ifp->ifp_mbox.mbox_cmd.mbox_out[4],
	    ifp->ifp_mbox.mbox_cmd.mbox_out[5],
	    ifp->ifp_mbox.mbox_cmd.mbox_out[6],
	    ifp->ifp_mbox.mbox_cmd.mbox_out[7]);
	ifp_i_log(ifp, CE_CONT, buf);
	(void) sprintf(buf,
	    "!in box(0-7):0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
	    ifp->ifp_mbox.mbox_cmd.mbox_in[0],
	    ifp->ifp_mbox.mbox_cmd.mbox_in[1],
	    ifp->ifp_mbox.mbox_cmd.mbox_in[2],
	    ifp->ifp_mbox.mbox_cmd.mbox_in[3],
	    ifp->ifp_mbox.mbox_cmd.mbox_in[4],
	    ifp->ifp_mbox.mbox_cmd.mbox_in[5],
	    ifp->ifp_mbox.mbox_cmd.mbox_in[6],
	    ifp->ifp_mbox.mbox_cmd.mbox_in[7]);
	ifp_i_log(ifp, CE_CONT, buf);

	(void) sprintf(buf,
	    "!mailbox(0-7):0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox0),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox1),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox2),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox3),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox4),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox5),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_mailbox6),
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_mailbox7));
	ifp_i_log(ifp, CE_CONT, buf);

	if (IFP_READ_RISC_HCCR(ifp) ||
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_bus_sema)) {
		(void) sprintf(buf,
		    "!hccr= 0x%x, bus_sema= 0x%x", IFP_READ_RISC_HCCR(ifp),
		    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_bus_sema));
		ifp_i_log(ifp, CE_CONT, buf);
	}

#ifdef IFPDEBUG
	/*
	 * apparently, pausing and resuming RISC changes things while
	 * trying to debug mbox cmd timeouts.
	 */
	if (ifp_mbox_debug)
		goto skip_pausing;
#endif
	if ((IFP_READ_RISC_HCCR(ifp) & IFP_HCCR_PAUSE) == 0) {
		IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_PAUSE);
		risc_paused = 1;
	}
#ifdef IFPDEBUG
skip_pausing:
#endif

	(void) sprintf(buf,
	    "!bus: isr= 0x%x, icr= 0x%x, icsr= 0x%x",
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_bus_isr),
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_bus_icr),
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_bus_icsr));
	ifp_i_log(ifp, CE_CONT, buf);

	(void) sprintf(buf,
	    "!cdma: count= %d, addr= 0x%x, status= 0x%x",
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_cdma_count),
	    (IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_cdma_addr1) << 16) |
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_cdma_addr0),
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_cdma_status));
	if ((i = IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_cdma_control)) != 0) {
		(void) sprintf(&buf[strlen(buf)], ", control= 0x%x",
		    (ushort_t)i);
	}
	ifp_i_log(ifp, CE_CONT, buf);

	(void) sprintf(buf,
	    "!rdma: count= %d, addr= 0x%x, status= 0x%x",
	    (IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_count_hi) << 16) |
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_count_lo),
	    (IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_addr1) << 16) |
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_addr0),
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_status));
	if ((i = IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_control)) != 0) {
		(void) sprintf(&buf[strlen(buf)], ", control= 0x%x",
		    (ushort_t)i);
	}
	ifp_i_log(ifp, CE_CONT, buf);

	(void) sprintf(buf,
	    "!tdma: count= %d, addr= 0x%x, status= 0x%x",
	    (IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_count_hi) << 16) |
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_count_lo),
	    (IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_addr1) << 16) |
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_addr0),
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_status));
	if ((i = IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_control)) != 0) {
		(void) sprintf(&buf[strlen(buf)], ", control= 0x%x",
		    (ushort_t)i);
	}
	ifp_i_log(ifp, CE_CONT, buf);

#ifdef IFPDEBUG
	if (ifp_mbox_debug)
		goto skip_resuming_if_paused;
#endif
	/*
	 * If the risc isn't already paused, pause it now.
	 */
	if (risc_paused == 0) {
		IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_PAUSE);
		risc_paused = 1;
	}

	(void) sprintf(buf,
	    "!risc: R0-R7= 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x 0x%x",
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_acc),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r1),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r2),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r3),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r4),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r5),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r6),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r7));
	ifp_i_log(ifp, CE_CONT, buf);

	(void) sprintf(buf,
	    "!risc: R8-R15= 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x 0x%x",
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r8),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r9),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r10),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r11),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r12),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r13),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r14),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r15));
	ifp_i_log(ifp, CE_CONT, buf);

	(void) sprintf(buf,
	    "!risc: PSR= 0x%x, IVR= 0x%x, PCR=0x%x, RAR0=0x%x, RAR1=0x%x",
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_psr),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_ivr),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_pcr),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_rar0),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_rar1));
	ifp_i_log(ifp, CE_CONT, buf);

	(void) sprintf(buf,
	    "!risc: LCR= 0x%x, PC= 0x%x, MTR=0x%x, SP=0x%x",
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_lcr),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_pc),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_mtr),
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_sp));
	ifp_i_log(ifp, CE_CONT, buf);

	/*
	 * If we paused the risc, restart it.
	 */
	if (risc_paused) {
		IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_RELEASE);
	}

#ifdef IFPDEBUG
skip_resuming_if_paused:
#endif
	/*
	 * Print ifp queue settings out.
	 */
	ifp->ifp_request_out =
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox4);
	(void) sprintf(buf,
	    "!request(in/out)= %d/%d, response(in/out)= %d/%d",
	    ifp->ifp_request_in, ifp->ifp_request_out,
	    ifp->ifp_response_in, ifp->ifp_response_out);
	ifp_i_log(ifp, CE_CONT, buf);

	(void) sprintf(buf,
	    "!request_ptr(current, base)=  0x%p (0x%p)",
	    (void *)ifp->ifp_request_ptr, (void *)ifp->ifp_request_base);
	ifp_i_log(ifp, CE_CONT, buf);

	(void) sprintf(buf,
	    "!response_ptr(current, base)= 0x%p (0x%p)",
	    (void *)ifp->ifp_response_ptr, (void *)ifp->ifp_response_base);
	ifp_i_log(ifp, CE_CONT, buf);

	if (IFP_READ_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_cdma_addr1) ||
	    IFP_READ_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_cdma_addr0)) {
		(void) sprintf(buf,
		    "!dvma request_ptr= 0x%x - 0x%x",
		    (int)ifp->ifp_request_dvma,
		    (int)ifp->ifp_response_dvma);
		ifp_i_log(ifp, CE_CONT, buf);

		(void) sprintf(buf,
		    "!dvma response_ptr= 0x%x - 0x%x",
		    (int)ifp->ifp_response_dvma,
		    (int)(ifp->ifp_request_dvma + IFP_QUEUE_SIZE));
		ifp_i_log(ifp, CE_CONT, buf);
	}
}

/*
 * kmem cache constructor and destructor.
 * When constructing, we bzero the ifp cmd structure
 * When destructing, just free the dma handle
 */
/*ARGSUSED*/
static int
ifp_kmem_cache_constructor(void *buf, void *cdrarg, int kmflags)
{
	struct ifp_cmd *sp = buf;
	struct ifp *ifp = cdrarg;
	ddi_dma_attr_t	tmp_dma_attr = dma_ifpattr;

	int  (*callback)(caddr_t) = (kmflags & KM_SLEEP) ? DDI_DMA_SLEEP:
	    DDI_DMA_DONTWAIT;

	bzero(sp, EXTCMDS_SIZE);

	tmp_dma_attr.dma_attr_burstsizes = ifp->ifp_burst_size;

	if ((sp->cmd_id = IFP_GET_ID(sp, kmflags)) == 0) {
		return (-1);
	}
	if (ddi_dma_alloc_handle(ifp->ifp_dip, &tmp_dma_attr, callback,
	    NULL, &sp->cmd_dmahandle) != DDI_SUCCESS) {
		IFP_FREE_ID(sp->cmd_id);
		return (-1);
	}
	return (0);
}

/* ARGSUSED */
static void
ifp_kmem_cache_destructor(void *buf, void *cdrarg)
{
	struct ifp_cmd *sp = buf;
	if (sp->cmd_dmahandle) {
		IFP_FREE_ID(sp->cmd_id);
		ddi_dma_free_handle(&sp->cmd_dmahandle);
	}
}

/*
 * Bus event handling.
 */
/* ARGSUSED */
static int
ifp_bus_get_eventcookie(dev_info_t *dip, dev_info_t *rdip, char *name,
	ddi_eventcookie_t *cookiep)
{
	ifp_t *ifp = ddi_get_soft_state(ifp_state, ddi_get_instance(dip));

	if (ifp == (struct ifp *)NULL)
		return (DDI_FAILURE);

	return (ndi_event_retrieve_cookie(ifp->ifp_ndi_event_hdl, rdip, name,
	    cookiep, NDI_EVENT_NOPASS));
}

/* ARGSUSED */
static int
ifp_bus_add_eventcall(dev_info_t *dip, dev_info_t *rdip,
	ddi_eventcookie_t eventid, void (*callback)(), void *arg,
	ddi_callback_id_t *cb_id)
{
	ifp_t *ifp = ddi_get_soft_state(ifp_state, ddi_get_instance(dip));

	if (ifp == (struct ifp *)NULL)
		return (DDI_FAILURE);

	return (ndi_event_add_callback(ifp->ifp_ndi_event_hdl, rdip,
	    eventid, callback, arg, NDI_SLEEP, cb_id));
}

/* ARGSUSED */
static int
ifp_bus_remove_eventcall(dev_info_t *dip, ddi_callback_id_t id)
{
	ifp_t *ifp = ddi_get_soft_state(ifp_state, ddi_get_instance(dip));

	if (ifp == (struct ifp *)NULL)
		return (DDI_FAILURE);

	return (ndi_event_remove_callback(ifp->ifp_ndi_event_hdl, id));
}

/* ARGSUSED */
static int
ifp_bus_post_event(dev_info_t *dip, dev_info_t *rdip,
    ddi_eventcookie_t eventid, void *impldata)
{
	ifp_t *ifp = ddi_get_soft_state(ifp_state, ddi_get_instance(dip));

	if (ifp == (struct ifp *)NULL)
		return (DDI_FAILURE);

	return (ndi_event_run_callbacks(ifp->ifp_ndi_event_hdl, rdip,
	    eventid, impldata));
}

static void
ifp_i_finish_init(struct ifp *ifp, uint_t lip_cnt)
{
	int i;
	ifp_target_t *target;

	ATRACE(ifp_i_finish_init, 0x55555540, ifp);

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	for (i = 0; i < IFP_NUM_HASH_QUEUES; i++) {
		target = ifp->ifp_wwn_lists[i];

		/*
		 * Walk through all the known targets and offline them
		 * if marked so. We start all over if the LIP count
		 * in the adapter is different from the initial LIP count.
		 * This is so that we are dealing with only the latest LIP.
		 * Should the LIP count change, we need to get the AL-PA
		 * map, port/node names etc again.
		 */
		while (target != (ifp_target_t *)NULL) {
			if ((ifp->ifp_alpa_states[target->ifpt_loop_id] !=
			    IFPA_ALPA_NEEDS_RETRY) ||
			    (ifp->ifp_alpa_states[target->ifpt_loop_id] ==
			    IFPA_ALPA_NEEDS_RETRY &&
			    ifp->ifp_scandb_retry >= IFP_RETRIES))
				ifp_i_finish_target_init(ifp, target, lip_cnt);

			if (ifp->ifp_lip_cnt != lip_cnt)
				return;
			target = target->ifpt_next;
		}
	}
	ATRACE(ifp_i_finish_init, 0x55555549, ifp);
}

static void
ifp_i_finish_target_init(struct ifp *ifp, ifp_target_t *target, uint_t lip_cnt)
{
	int cflag;
	dev_info_t *dip;
	struct ifp_hp_elem *elem;
	ifp_lun_t	*lun_p;

	ATRACE(ifp_i_finish_target_init, 0xffffff00, target);
	ATRACE(ifp_i_finish_target_init, 0xffffff01,
	    target->ifpt_lun.ifpl_dip);

	ASSERT(mutex_owned(&target->ifpt_mutex) == 0);
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	IFP_DEBUG(3, (ifp, SCSI_DEBUG,
	    "ifp_i_finish_target_init: targ %x lip_state %x lip_state %x",
	    target->ifpt_loop_id, target->ifpt_state, ifp->ifp_lip_state));


	mutex_enter(&target->ifpt_mutex);

	if (!(target->ifpt_state & IFPT_TARGET_OFFLINE)) {
		if (ifp->ifp_lip_state & IFPL_RETRY_SCAN_PORTDB) {
			if ((target->ifpt_state & IFPT_TARGET_BUSY) == 0) {
				mutex_exit(&target->ifpt_mutex);
				goto done;
			}
		}
		if (target->ifpt_state & IFPT_TARGET_MARK) {
			mutex_exit(&target->ifpt_mutex);
			ifp_i_offline_target(ifp, target);
			goto done;
		}
		target->ifpt_state &= ~IFPT_TARGET_BUSY;

		mutex_exit(&target->ifpt_mutex);

		for (lun_p = &target->ifpt_lun; lun_p != NULL;
		    lun_p = lun_p->ifpl_next) {

			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			    "ifp_i_finish_target_init: loop targ %x lun %x",
			    target->ifpt_loop_id, lun_p->ifpl_lun_num));

			/*
			 * don't create devinfo nodes for unknown
			 * device types -- these include initiator
			 * only nodes.
			 */
			if (lun_p->ifpl_device_type == DTYPE_UNKNOWN) {
				continue;
			}

			cflag = !(lun_p->ifpl_state & IFPL_LUN_INIT_DONE);

			IFP_MUTEX_EXIT(ifp);

			dip = lun_p->ifpl_dip;

			ATRACE(ifp_i_finish_target_init, 0xffffff03, lun_p);
			ATRACE(ifp_i_finish_target_init, 0xffffff04, dip);

			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			    "ifp_i_finish_target_init: "
			    "targ %x lun %x, dip 0x%p",
			    target->ifpt_loop_id, lun_p->ifpl_lun_num,
			    (void *)dip));

			if (cflag && (dip == NULL)) {
				ifp_i_create_devinfo(ifp, target, lun_p,
				    lip_cnt);
			} else {
				if (ifp_i_update_props(dip, target,
				    lun_p->ifpl_lun_num, lip_cnt) == 1) {
					elem = kmem_zalloc(
					    sizeof (struct ifp_hp_elem),
					    KM_NOSLEEP);
					if (elem) {
						elem->dip = dip;
						elem->target = target;
						elem->what = IFP_ONLINE;
						mutex_enter(
						    IFP_HP_DAEMON_MUTEX(ifp));
						if (ifp->ifp_hp_elem_tail) {
							ifp->ifp_hp_elem_tail->
							    next = elem;
							ifp->ifp_hp_elem_tail =
							    elem;
						} else {
							ifp->ifp_hp_elem_head =
							    elem;
							ifp->ifp_hp_elem_tail =
							    elem;
						}
						cv_signal(
						    IFP_HP_DAEMON_CV(ifp));
						mutex_exit(
						    IFP_HP_DAEMON_MUTEX(ifp));
					} else if (i_ddi_devi_attached(
					    ifp->ifp_dip)) {
						/* couldn't allocate HP elem */
						if (ndi_devi_online_async(dip,
						    0) != NDI_SUCCESS) {
							ifp_i_log(ifp,
							    CE_WARN,
							    "!ndi_devi_online "
							    "failed for "
							    "tgt=0x%p"
							    "lun=%x",
							    (void *)target,
							    lun_p->
							    ifpl_lun_num);
						}
						(void)
						    ndi_event_retrieve_cookie(
						    ifp->ifp_ndi_event_hdl,
						    dip, ifp_insert_ename,
						    &ifp_insert_eid,
						    NDI_EVENT_NOPASS);
						(void) ndi_event_run_callbacks(
						    ifp->ifp_ndi_event_hdl,
						    dip, ifp_insert_eid,
						    NULL);
					} else {
						/*
						 * Cannot allocate HP elem
						 * and ifp not yet in attached
						 * state - device pm bug?
						 */
						ifp_i_log(ifp,
						    CE_WARN,
						    "!online failed for "
						    "tgt=0x%p lun=%x - ifp "
						    "not yet attached",
						    (void *)target,
						    lun_p-> ifpl_lun_num);
					}
				}
			}
			IFP_MUTEX_ENTER(ifp);
		}
	} else {
		mutex_exit(&target->ifpt_mutex);
	}
done:

	ASSERT(mutex_owned(&target->ifpt_mutex) == 0);
	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));
	ATRACE(ifp_i_finish_target_init, 0xffffff09, target);
}


static void
ifp_i_create_devinfo(struct ifp *ifp, struct ifp_target *target,
		    ifp_lun_t *lun_p, uint_t lip_cnt)
{
	dev_info_t		*cdip = NULL;
	int			tgt_id = ifp_alpa_to_loopid[target->ifpt_al_pa];
	char			*nname = NULL;
	char			**compatible = NULL;
	int			ncompatible;
	struct scsi_inquiry	*inq;
	char			*scsi_binding_set;

	ATRACE(ifp_i_create_devinfo, 0xeffffff0, cdip);

	IFP_DEBUG(2, (ifp, SCSI_DEBUG,
	    "ifp_i_create_devinfo: targ %x lun %x, dip 0x%p",
	    tgt_id, lun_p->ifpl_lun_num, (void *)lun_p->ifpl_dip));

	if (lun_p->ifpl_dip != NULL) {
		ifp_i_log(ifp, CE_WARN, "ifp_i_create_devinfo: dip 0x%p exists",
		    (void *)lun_p->ifpl_dip);
		return;
	}

	if ((inq = lun_p->ifpl_inq) == NULL) {
		ifp_i_log(ifp, CE_WARN, "ifp_i_create_devinfo: "
		    "no inquiry data for target 0x%p", (void *)target);
		return;
	}

	/* get the 'scsi-binding-set' property */
	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, ifp->ifp_dip,
	    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS, "scsi-binding-set",
	    &scsi_binding_set) != DDI_PROP_SUCCESS)
		scsi_binding_set = NULL;

	/* determine the node name and compatible */
	scsi_hba_nodename_compatible_get(inq, scsi_binding_set,
	    inq->inq_dtype, NULL, &nname, &compatible, &ncompatible);
	if (scsi_binding_set)
		ddi_prop_free(scsi_binding_set);

	/* if nodename can't be determined then print a message and skip it */
	if (nname == NULL) {
		ifp_i_log(ifp, CE_WARN, "%s%d: no driver for device @%x,%x\n"
		    "    compatible: %s", ddi_driver_name(ifp->ifp_dip),
		    ddi_get_instance(ifp->ifp_dip),
		    tgt_id, lun_p->ifpl_lun_num, *compatible);
		goto fail;
	}

	if (ndi_devi_alloc(ifp->ifp_dip, nname,
	    DEVI_SID_NODEID, &cdip) != NDI_SUCCESS) {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "ifp_i_create_devinfo: FAIL alloc cdip 0x%p",
		    (void *)cdip));
		goto fail;
	}

	/* decorate the node with compatible */
	if (ndi_prop_update_string_array(DDI_DEV_T_NONE, cdip,
	    "compatible", compatible, ncompatible) != DDI_PROP_SUCCESS) {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "ifp_i_create_devinfo: FAIL compatible props cdip 0x%p",
		    (void *)cdip));
		goto fail;
	}

	/* add addressing properties to the node */
	if (ifp_i_update_props(cdip, target,
	    lun_p->ifpl_lun_num, lip_cnt) != 1) {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "ifp_i_create_devinfo: FAIL props cdip 0x%p",
		    (void *)cdip));
		goto fail;
	}

	/*
	 * Now that we have the child devinfo in place,
	 * insert in the tree and bind the driver.
	 * If the binding fails, leave the node in the tree;
	 * a driver for this device may be added later.
	 */
	(void) ndi_devi_bind_driver_async(cdip, 0);

	mutex_enter(&target->ifpt_mutex);
	lun_p->ifpl_dip = cdip;
	mutex_exit(&target->ifpt_mutex);

	IFP_DEBUG(3, (ifp, SCSI_DEBUG,
	    "ifp_i_create_devinfo: targ %x lun %x cdip 0x%p name %s",
	    tgt_id, lun_p->ifpl_lun_num, (void *)cdip, nname));

	scsi_hba_nodename_compatible_free(nname, compatible);
	return;

fail:
	IFP_DEBUG(3, (ifp, SCSI_DEBUG, "ifp_i_create_devinfo: FAIL cdip 0x%p",
	    (void *)cdip));
	scsi_hba_nodename_compatible_free(nname, compatible);
	if (cdip != NULL) {
		(void) ndi_prop_remove(DDI_DEV_T_NONE, cdip, "node-wwn");
		(void) ndi_prop_remove(DDI_DEV_T_NONE, cdip, "port-wwn");
		(void) ndi_prop_remove(DDI_DEV_T_NONE, cdip, "lip-count");
		(void) ndi_prop_remove(DDI_DEV_T_NONE, cdip, "target");
		(void) ndi_prop_remove(DDI_DEV_T_NONE, cdip, "lun");
		if (ndi_devi_free(cdip) != NDI_SUCCESS) {
			ifp_i_log(ifp, CE_WARN,
			"ifp%x: target %x ndi_devi_free of new node failed",
			    ddi_get_instance(ifp->ifp_dip), tgt_id);
		}
	}
}

static int
ifp_i_update_props(dev_info_t *cdip, struct ifp_target *target,
    ushort_t lun, uint_t lip_cnt)
{
	int tgt_id = ifp_alpa_to_loopid[target->ifpt_al_pa];

	ATRACE(ifp_i_update_props, 0xfdddddd0, cdip);
	ATRACE(ifp_i_update_props, 0xfdddddd1, tgt_id);
	ATRACE(ifp_i_update_props, 0xfdddddd2, lun);
	IFP_DEBUG(2, (0, SCSI_DEBUG,
	"ifp_i_update_props: cdip 0x%p, id %x %x, lun %x",
	    (void *)cdip, tgt_id, target->ifpt_hard_address, lun));


	if (ndi_prop_update_byte_array(DDI_DEV_T_NONE,
	    cdip, "node-wwn", target->ifpt_node_wwn, FC_WWN_SIZE) !=
	    DDI_PROP_SUCCESS) {
		return (0);
	}

	if (ndi_prop_update_byte_array(DDI_DEV_T_NONE,
	    cdip, "port-wwn", target->ifpt_port_wwn, FC_WWN_SIZE) !=
	    DDI_PROP_SUCCESS) {
		return (0);
	}

	if (ndi_prop_update_int(DDI_DEV_T_NONE,
	    cdip, "lip-count", lip_cnt) != DDI_PROP_SUCCESS) {
		return (0);
	}

	if (ndi_prop_update_int(DDI_DEV_T_NONE,
	    cdip, "target", tgt_id) != DDI_PROP_SUCCESS) {
		return (0);
	}

	if (ndi_prop_update_int(DDI_DEV_T_NONE,
	    cdip, "lun", lun) != DDI_PROP_SUCCESS) {
		return (0);
	}

	return (1);
}

static int
ifp_i_get_hba_alpa(struct ifp *ifp, uchar_t *alpa)
{
	int rval;
	struct ifp_mbox_cmd mbox_cmd;

	/*
	 * Get loopid for the HBA
	 */
	ifp_i_mbox_cmd_init(ifp, &mbox_cmd, 1, 3,
	    IFP_MBOX_CMD_GET_ID, 0, 0, 0, 0, 0, 0, 0);
	rval = ifp_i_mbox_cmd_start(ifp, &mbox_cmd);

#ifdef IFPDEBUG
	if (rval) {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		    "Unable to get host id: mbox status %x",
		    mbox_cmd.mbox_in[0]));
	} else {
		IFP_DEBUG(2, (ifp, SCSI_DEBUG,
		    "Host id: %x", mbox_cmd.mbox_in[1]));
	}
#endif
	if (!rval)
		*alpa = ifp_loopid_to_alpa[mbox_cmd.mbox_in[1]];

	return (rval);
}

/* ARGSUSED */
static void
ifp_i_offline_target(struct ifp *ifp, struct ifp_target *target)
{
	dev_info_t *dip;
	struct ifp_hp_elem *elem;
	ifp_lun_t	*lun_p;

	ATRACE(ifp_i_call_pkt_comp, 0xaaaaaa50, ifp);
	ATRACE(ifp_i_call_pkt_comp, 0xaaaaaa51,
	    target->ifpt_loop_id);
	IFP_DEBUG(3, (ifp, SCSI_DEBUG, "ifp_i_offline_target: targ %x",
	    target->ifpt_loop_id));


	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	ASSERT(mutex_owned(IFP_RESP_MUTEX(ifp)));

	mutex_enter(&target->ifpt_mutex);

	/* proceed further only if it is not offline */
	if (target->ifpt_state & IFPT_TARGET_OFFLINE) {
		ATRACE(ifp_i_call_pkt_comp, 0xaaaaaa52, ifp);
		mutex_exit(&target->ifpt_mutex);
		return;
	}

	target->ifpt_state &= ~(IFPT_TARGET_BUSY|IFPT_TARGET_MARK);
	ifp->ifp_alpa_states[target->ifpt_loop_id] = IFPA_ALPA_OFFLINE;
	target->ifpt_state |= IFPT_TARGET_OFFLINE;
	if (target->ifpt_state & IFPT_TARGET_INIT_DONE) {

		mutex_exit(&target->ifpt_mutex);
		for (lun_p = &target->ifpt_lun; lun_p != NULL;
		    lun_p = lun_p->ifpl_next) {

			ATRACE(ifp_i_call_pkt_comp, 0xaaaaaa53,
			    lun_p->ifpl_lun_num);
			IFP_MUTEX_EXIT(ifp);
			dip = lun_p->ifpl_dip;
			if (dip != NULL) {
				(void) ndi_prop_remove(DDI_DEV_T_NONE,
				    dip, "target");

				(void) ndi_event_retrieve_cookie(
				    ifp->ifp_ndi_event_hdl, dip,
				    ifp_remove_ename, &ifp_remove_eid,
				    NDI_EVENT_NOPASS);
				(void) ndi_event_run_callbacks(
				    ifp->ifp_ndi_event_hdl,
				    dip, ifp_remove_eid, NULL);

				elem = kmem_zalloc(sizeof (struct ifp_hp_elem),
				    KM_NOSLEEP);
				if (elem) {
					elem->dip = dip;
					elem->target = target;
					elem->what = IFP_OFFLINE;
					mutex_enter(IFP_HP_DAEMON_MUTEX(ifp));
					if (ifp->ifp_hp_elem_tail) {
						ifp->ifp_hp_elem_tail->next =
						    elem;
						ifp->ifp_hp_elem_tail = elem;
					} else {
						ifp->ifp_hp_elem_head =
						    ifp->ifp_hp_elem_tail =
						    elem;
					}
					cv_signal(IFP_HP_DAEMON_CV(ifp));
					mutex_exit(IFP_HP_DAEMON_MUTEX(ifp));
				} else {
					/* don't do NDI_DEVI_REMOVE for now */
					if (ndi_devi_offline(dip, 0) !=
					    NDI_SUCCESS) {
				IFP_DEBUG(3, (ifp, CE_WARN,
				    "target %x, device offline failed",
				    ifp_alpa_to_loopid[target->ifpt_al_pa]));
					} else {
				IFP_DEBUG(3, (ifp, CE_NOTE,
				    "target %x, device offline succeeded\n",
				    ifp_alpa_to_loopid[target->ifpt_al_pa]));
					}
				}
			}
			IFP_MUTEX_ENTER(ifp);
		}
	} else {
		mutex_exit(&target->ifpt_mutex);
	}
	ATRACE(ifp_i_call_pkt_comp, 0xaaaaaa53, ifp);
}

static void
ifp_hp_daemon(void *arg)
{
	callb_cpr_t cprinfo;
	struct ifp_hp_elem *elem;
	struct ifp_target *target;
	struct ifp *ifp = (struct ifp *)arg;


	CALLB_CPR_INIT(&cprinfo, IFP_HP_DAEMON_MUTEX(ifp), callb_generic_cpr,
	    "ifp_hp_daemon");

	mutex_enter(IFP_HP_DAEMON_MUTEX(ifp));
loop:
	while (ifp->ifp_hp_elem_head) {
		elem = ifp->ifp_hp_elem_head;
		if (ifp->ifp_hp_elem_head == ifp->ifp_hp_elem_tail) {
			ifp->ifp_hp_elem_head = ifp->ifp_hp_elem_tail = NULL;
		} else
			ifp->ifp_hp_elem_head = ifp->ifp_hp_elem_head->next;
		mutex_exit(IFP_HP_DAEMON_MUTEX(ifp));

		switch (elem->what) {
			case IFP_ONLINE:
				if (!i_ddi_devi_attached(ifp->ifp_dip))
					break;
				target = elem->target;
				if (ndi_devi_online(elem->dip,
				    NDI_ONLINE_ATTACH) != NDI_SUCCESS) {
					ifp_i_log(ifp, CE_WARN,
					    "!ndi_devi_online failed for "
					    "tgt=0x%p lun=%x (HP)",
					    (void *)target,
					    target->ifpt_lun.ifpl_lun_num);
				}

				(void) ndi_event_retrieve_cookie(
				    ifp->ifp_ndi_event_hdl, elem->dip,
				    ifp_insert_ename, &ifp_insert_eid,
				    NDI_EVENT_NOPASS);
				(void) ndi_event_run_callbacks(
				    ifp->ifp_ndi_event_hdl, elem->dip,
				    ifp_insert_eid, NULL);
				break;
			case IFP_OFFLINE:
				target = elem->target;
				/* don't do NDI_DEVI_REMOVE for now */
				if (ndi_devi_offline(elem->dip, 0) !=
				    NDI_SUCCESS) {
					IFP_DEBUG(3, (ifp, CE_WARN,
					"target %x, device offline failed",
					    ifp_alpa_to_loopid[
					    target->ifpt_al_pa]));
				} else {
					IFP_DEBUG(3, (ifp, CE_NOTE,
					"target %x, device offline succeeded\n",
					    ifp_alpa_to_loopid[
					    target->ifpt_al_pa]));
				}
				break;
		}
		kmem_free(elem, sizeof (struct ifp_hp_elem));
		mutex_enter(IFP_HP_DAEMON_MUTEX(ifp));
	}

	CALLB_CPR_SAFE_BEGIN(&cprinfo);
	cv_wait(IFP_HP_DAEMON_CV(ifp), IFP_HP_DAEMON_MUTEX(ifp));
	CALLB_CPR_SAFE_END(&cprinfo, IFP_HP_DAEMON_MUTEX(ifp));

	if (ifp->ifp_hp_thread_go_away && ifp->ifp_hp_elem_head == NULL) {
		mutex_exit(IFP_HP_DAEMON_MUTEX(ifp));
		/* This must be after the mutex exit */
		ifp->ifp_hp_thread_go_away = 0;
		return;
	}

	goto loop;
}

static void
ifp_i_mark_loop_down(struct ifp *ifp)
{
	int i;
	struct ifp_target *target;

	ASSERT(mutex_owned(IFP_REQ_MUTEX(ifp)));
	for (i = 0; i < IFP_MAX_TARGETS; i++) {
		target = ifp->ifp_targets[i];
		if (target != NULL) {
			mutex_enter(&target->ifpt_mutex);
			if (!(target->ifpt_state & IFPT_TARGET_OFFLINE))
				target->ifpt_state |= (IFPT_TARGET_BUSY
				    | IFPT_TARGET_MARK);
			mutex_exit(&target->ifpt_mutex);
		}
	}
}

#define	FLASHSIZE 131072
#define	DEF2AA	0x2aaa
#define	DEF555	0x5555

int
ifp_flasher(struct ifp *ifp, uint8_t *fcode_p, int length)
{
	int err = 0, err_cnt;
	uint32_t addr, restofaddress, sector_mask;
	uint8_t   dev_id, flash_data, read_data, sector_number = 0;
	uint16_t	nvram_sel;

	/* insure that the nv ram is not enabled, restore when done */
	nvram_sel = IFP_READ_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_nvram);
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_nvram, 0);

	/*  Get the manufacture id of the flash part so */
	/*  we can determine the programming algorithm to use. */
	(void) ifp_manuf_id(ifp);
	dev_id = ifp_fl_dev_id(ifp);


	if (dev_id == 0x6d) {
		/* Am29LV001 part */
		restofaddress = 0x1fff;
		sector_mask = 0x1e000;
	} else {
		/* Am29F010 part */
		restofaddress = 0x3fff;
		sector_mask = 0x1c000;
	}
	for (addr = 0; addr < length; addr++) {
		IFP_MUTEX_EXIT(ifp);
		if ((err = copyin((caddr_t)fcode_p + addr, &flash_data, 1)) ==
		    -1) {
			IFP_MUTEX_ENTER(ifp);
			IFP_WRITE_BIU_REG(ifp,
			    &ifp->ifp_biu_reg->ifp_pci_nvram, nvram_sel);
			return (EFAULT);
		}
		IFP_MUTEX_ENTER(ifp);

		/* is this the beginning of a sector? */
		if (!(addr & restofaddress)) {
			/* then erase it! */
			if (err = ifp_erase_sector(ifp, addr, sector_mask)) {
				IFP_WRITE_BIU_REG(ifp,
				    &ifp->ifp_biu_reg->ifp_pci_nvram,
				    nvram_sel);
				return (EFAULT);
			}
			sector_number++;
		}
		if (dev_id == 0x6d) {
			if (sector_number == 1 && addr == (restofaddress -1)) {
				restofaddress = 0x0fff;
				sector_mask = 0x1f000;
			} else if (sector_number == 3 && (addr & 0x7ffe)) {
				restofaddress = 0x3fff;
				sector_mask = 0x1c000;
			}
		}
		if (err = ifp_program_address(ifp, addr, flash_data)) {
			IFP_WRITE_BIU_REG(ifp,
			    &ifp->ifp_biu_reg->ifp_pci_nvram, nvram_sel);
			return (EFAULT);
		}
	}

	for (addr = err_cnt = 0; addr < length && err_cnt < 25; addr++) {
		IFP_MUTEX_EXIT(ifp);
		if (copyin((caddr_t)fcode_p + addr, &flash_data, 1) == -1) {
			IFP_MUTEX_ENTER(ifp);
			IFP_WRITE_BIU_REG(ifp,
			    &ifp->ifp_biu_reg->ifp_pci_nvram, nvram_sel);
			return (EFAULT);
		}
		IFP_MUTEX_ENTER(ifp);
		if ((read_data = ifp_flash_read(ifp, addr)) != flash_data) {
			ifp_i_log(ifp, CE_WARN,
			    "ifp_FlashWrite: Miscompare at Address: %x "
			    "Actual: %x Expected: %x\n",
			    addr, read_data, flash_data);
			err_cnt++;
			err = EFAULT;
		}
	}
	if (!err) {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		"Flash Verified Successfully\n"));
	} else {
		IFP_DEBUG(3, (ifp, SCSI_DEBUG,
		"Error Verifying Flash\n"));
		if (err_cnt == 25) {
			IFP_DEBUG(3, (ifp, SCSI_DEBUG,
			"Verify aborted \n"));
		}
	}
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_nvram, nvram_sel);
	return (err);
}

#define	SectorMask  0x1c000

int
ifp_erase_sector(struct ifp *ifp, uint32_t addr, uint32_t sector_mask)
{

	/* This comes from the AM29F010 data book */
	/* First send a sequence of commands to the chip */
	ifp_flash_write(ifp, DEF555, 0xAA);
	ifp_flash_write(ifp, DEF2AA, 0x55);
	ifp_flash_write(ifp, DEF555, 0x80);
	ifp_flash_write(ifp, DEF555, 0xAA);
	ifp_flash_write(ifp, DEF2AA, 0x55);
	/* then send the sector address. */
	ifp_flash_write(ifp, addr & sector_mask, 0x30);
	/* Next, wait for write to complete */
	/* the erase command is complete when the addr reads a 1 */
	return (ifp_poll_device(ifp, addr, 0x80));
}

uint8_t
ifp_manuf_id(struct ifp *ifp)
{
	uint8_t temp;

	/* This comes from the AM29F010 data book */
	/* First send a sequence of commands to the chip */

	ifp_flash_write(ifp, DEF555, 0xAA);
	ifp_flash_write(ifp, DEF2AA, 0x55);
	ifp_flash_write(ifp, DEF555, 0x90);
	temp = ifp_flash_read(ifp, (uint32_t)0x0000);
	return (temp);
}

uint8_t
ifp_fl_dev_id(struct ifp *ifp)
{
	uint8_t temp;

	/* This comes from the AM29F010 data book */
	/* First send a sequence of commands to the chip */

	ifp_flash_write(ifp, DEF555, 0xAA);
	ifp_flash_write(ifp, DEF2AA, 0x55);
	ifp_flash_write(ifp, DEF555, 0x90);
	temp = ifp_flash_read(ifp, (uint32_t)0x0001);
	return (temp);
}

int
ifp_program_address(struct ifp *ifp, uint32_t addr, uint8_t data)
{

	/* This comes from the AM29F010 data book */
	/* First send a sequence of commands to the chip */
	ifp_flash_write(ifp, DEF555, 0xAA);
	ifp_flash_write(ifp, DEF2AA, 0x55);
	ifp_flash_write(ifp, DEF555, 0xA0);
	/* Next, send the addr, and data */
	ifp_flash_write(ifp, addr, data);
	/* Next, wait for write to complete */
	return (ifp_poll_device(ifp, addr, data));
}

int
ifp_poll_device(struct ifp *ifp, uint32_t addr, uint8_t data)
{
	int failPass = 3;
	int   i;
	uint8_t status;

	for (i = 0; i < 10000000; i++) {
		status = ifp_flash_read(ifp, addr);
		if ((status & 0x80) == (data & 0x80)) {
			/* if this happens, then it passed */
			return (0);	/* return 0 for success */
		}
		if (status & 0x20) {	/* uh oh */
			failPass--;	/* do it one more time, just in case */
		}
		if (failPass <= 0) {	/* if it got decremented twice, */
			return (-1);	/* then it failed. */
		}
		drv_usecwait(1); /* wait just a bit */
	}
	return (-1);
}

void
ifp_flash_write(struct ifp *ifp, uint32_t addr, uint8_t data)
{
	uint16_t riscAddress;

	riscAddress = addr;
	/* is addr in the low bank or the high bank */
	if (addr & 0xffff0000) {
		IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icsr,
		    IFP_BUS_ICSR_FLASH_ENABLE |
		    IFP_BUS_ICSR_FLASH_UP_BANK);
	} else {
		IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icsr,
		    IFP_BUS_ICSR_FLASH_ENABLE);
	}
	/* the data is really a byte, but the risc */
	ifp_write_word(ifp, riscAddress, (uint16_t)data);
}

uint8_t
ifp_flash_read(struct ifp *ifp, uint32_t addr)
{
	uint8_t data;
	uint16_t riscAddress;

	riscAddress = addr;
	/* is addr in the low bank or the high bank */
	if (addr & 0xffff0000) {
		IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icsr,
		    IFP_BUS_ICSR_FLASH_ENABLE |
		    IFP_BUS_ICSR_FLASH_UP_BANK);
	} else {
		IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icsr,
		    IFP_BUS_ICSR_FLASH_ENABLE);
	}

	/* the data is really a byte, but the risc */
	data = ifp_read_word(ifp, riscAddress);
	return (data);
}

void
ifp_write_word(struct ifp *ifp, uint16_t address, uint8_t data)
{
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_flash_addr, address);
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_flash_data, data);
}

uint8_t
ifp_read_word(struct ifp *ifp, uint16_t address)
{
	uint8_t	data;

	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_flash_addr, address);
	data = IFP_READ_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_flash_data);
	return (data);
}

static void
ifp_i_dump_mbox(struct ifp *ifp, struct ifp_mbox_cmd *mbox_cmdp, char *str)
{
#ifdef IFPDEBUG
	ifp_i_log(ifp, CE_NOTE, "mbox cmd %s:",  str);
	ifp_i_log(ifp, CE_CONT,
	    "cmd= 0x%x; 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
	    mbox_cmdp->mbox_out[0], mbox_cmdp->mbox_out[1],
	    mbox_cmdp->mbox_out[2], mbox_cmdp->mbox_out[3],
	    mbox_cmdp->mbox_out[4], mbox_cmdp->mbox_out[5],
	    mbox_cmdp->mbox_out[6], mbox_cmdp->mbox_out[7]);
	if (ifp->ifp_chip_id == 0x2200) {
	ifp_i_log(ifp, CE_CONT,
	    "     0x%x; 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
	    mbox_cmdp->mbox_out[8], mbox_cmdp->mbox_out[9],
	    mbox_cmdp->mbox_out[10], mbox_cmdp->mbox_out[11],
	    mbox_cmdp->mbox_out[12], mbox_cmdp->mbox_out[13],
	    mbox_cmdp->mbox_out[14], mbox_cmdp->mbox_out[15],
	    mbox_cmdp->mbox_out[16], mbox_cmdp->mbox_out[17]);
	}
	ifp_i_log(ifp, CE_CONT,
	    "status= 0x%x; 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
	    mbox_cmdp->mbox_in[0], mbox_cmdp->mbox_in[1],
	    mbox_cmdp->mbox_in[2], mbox_cmdp->mbox_in[3],
	    mbox_cmdp->mbox_in[4], mbox_cmdp->mbox_in[5],
	    mbox_cmdp->mbox_in[6], mbox_cmdp->mbox_in[7]);
#endif
}

static int
ifp_i_ok_to_run_diags(struct ifp *ifp)
{
	/*
	 * return 0 for OK to run, 1 otherwise. We can run diags if the loop
	 * is up & there is only one node (physical loop back, for now).
	 */
	if (ifp->ifp_state == IFP_STATE_ONLINE &&
	    ifp->ifp_loop_map[0] != 1) {
		return (1);
	} else {
		return (0);
	}
}

static int
ifp_i_run_mboxcmds(struct ifp *ifp, ifp_diag_cmd_t *diag)
{
	short i = 0;
	struct ifp_mbox_cmd mbox_cmd;

	IFP_MUTEX_ENTER(ifp);
	if (ifp_i_ok_to_run_diags(ifp)) {
		IFP_MUTEX_EXIT(ifp);
		return (EBUSY);
	}

	if (diag->ifp_cmds_count > IFP_DIAG_MAX_MBOX) {
		IFP_MUTEX_EXIT(ifp);
		return (EINVAL);
	}

	ifp->ifp_running_diags = 1;

	while (i < diag->ifp_cmds_count) {

		bcopy((const void *)&diag->ifp_mbox[i].ifp_out_mbox,
		    &mbox_cmd.mbox_out,
		    IFP_MAX_MBOX_REGS21 * sizeof (uint16_t));
		mbox_cmd.retry_cnt = IFP_MBOX_CMD_RETRY_CNT;
		mbox_cmd.timeout = IFP_MBOX_CMD_TIMEOUT;
		mbox_cmd.n_mbox_out = IFP_MAX_MBOX_REGS21;
		mbox_cmd.n_mbox_in = IFP_MAX_MBOX_REGS21;
		if (ifp_i_mbox_cmd_start(ifp, &mbox_cmd)) {
			ifp_i_diag_regdump(ifp, &diag->ifp_regs);
			break;
		}
		bcopy((const void *)&mbox_cmd.mbox_in,
		    (void *)&diag->ifp_mbox[i].ifp_in_mbox,
		    IFP_MAX_MBOX_REGS21 * sizeof (uint16_t));
		i++;
	}

	diag->ifp_cmds_done = i;

	/* reload the firmware to get to a known state */
	(void) ifp_i_reset_interface(ifp, IFP_DONT_WAIT_FOR_FW_READY);

	ifp->ifp_running_diags = 0;

	IFP_MUTEX_EXIT(ifp);

	return (0);
}

static void
ifp_i_diag_regdump(struct ifp *ifp, ifp_diag_regs_t *regs)
{
	char risc_paused = 0;
	struct ifp_biu_regs *ifp_biu = ifp->ifp_biu_reg;
	struct ifp_risc_regs *ifp_risc = &ifp->ifp_biu_reg->ifp_risc_reg;

	regs->ifpd_mailbox[0] =
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox0);
	regs->ifpd_mailbox[1] =
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox1);
	regs->ifpd_mailbox[2] =
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox2);
	regs->ifpd_mailbox[3] =
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox3);
	regs->ifpd_mailbox[4] =
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox4);
	regs->ifpd_mailbox[5] =
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox5);
	regs->ifpd_mailbox[6] =
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_mailbox6);
	regs->ifpd_mailbox[7] =
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_mailbox7);

	regs->ifpd_hccr = IFP_READ_RISC_HCCR(ifp);
	regs->ifpd_bus_sema = IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_bus_sema);

	if ((IFP_READ_RISC_HCCR(ifp) & IFP_HCCR_PAUSE) == 0) {
		IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_PAUSE);
		risc_paused = 1;
	}

	regs->ifpd_isr = IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_bus_isr);
	regs->ifpd_icr = IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_bus_icr);
	regs->ifpd_icsr = IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_bus_icsr);

	regs->ifpd_cdma_count = IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_cdma_count);
	regs->ifpd_cdma_addr =
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_cdma_addr1) << 16 |
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_cdma_addr0);
	regs->ifpd_cdma_status =
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_cdma_status);
	regs->ifpd_cdma_control =
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_cdma_control);

	regs->ifpd_rdma_count =
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_count_hi) << 16 |
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_count_lo);
	regs->ifpd_rdma_addr =
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_addr1) << 16 |
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_addr0);
	regs->ifpd_rdma_status =
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_status);
	regs->ifpd_rdma_control =
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_rdma_control);

	regs->ifpd_tdma_count = (uint_t)
	    (IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_count_hi) << 16 |
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_count_lo));
	regs->ifpd_tdma_addr =
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_addr1) << 16 |
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_addr0);
	regs->ifpd_tdma_status =
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_status);
	regs->ifpd_tdma_control =
	    IFP_READ_BIU_REG(ifp, &ifp_biu->ifp_tdma_control);

	/*
	 * If the risc isn't already paused, pause it now.
	 */
	if (risc_paused == 0) {
		IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_PAUSE);
		risc_paused = 1;
	}

	regs->ifpd_risc_reg[0] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_acc);
	regs->ifpd_risc_reg[1] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r1);
	regs->ifpd_risc_reg[2] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r2);
	regs->ifpd_risc_reg[3] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r3);
	regs->ifpd_risc_reg[4] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r4);
	regs->ifpd_risc_reg[5] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r5);
	regs->ifpd_risc_reg[6] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r6);
	regs->ifpd_risc_reg[7] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r7);
	regs->ifpd_risc_reg[8] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r8);
	regs->ifpd_risc_reg[9] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r9);
	regs->ifpd_risc_reg[10] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r10);
	regs->ifpd_risc_reg[11] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r11);
	regs->ifpd_risc_reg[12] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r12);
	regs->ifpd_risc_reg[13] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r13);
	regs->ifpd_risc_reg[14] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r14);
	regs->ifpd_risc_reg[15] =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_r15);

	regs->ifpd_risc_psr =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_psr);
	regs->ifpd_risc_ivr =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_ivr);
	regs->ifpd_risc_pcr =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_pcr);
	regs->ifpd_risc_rar0 =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_rar0);
	regs->ifpd_risc_rar1 =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_rar1);

	regs->ifpd_risc_lcr =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_lcr);
	regs->ifpd_risc_pc =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_pc);
	regs->ifpd_risc_mtr =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_mtr);
	regs->ifpd_risc_sp =
	    IFP_READ_BIU_REG(ifp, &ifp_risc->ifp_risc_sp);

	/*
	 * If we paused the risc, restart it.
	 */
	if (risc_paused) {
		IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_RELEASE);
	}

	/*
	 * Print ifp queue settings out.
	 */
	regs->ifpd_request_out =
	    IFP_READ_MBOX_REG(ifp, &ifp->ifp_biu_reg->ifp_mailbox4);
	regs->ifpd_request_in = ifp->ifp_request_in;
	regs->ifpd_response_in = ifp->ifp_response_in;
	regs->ifpd_response_out = ifp->ifp_response_out;

	regs->ifpd_current_req_ptr = (void *)ifp->ifp_request_ptr;
	regs->ifpd_base_req_ptr = (void *)ifp->ifp_request_base;

	regs->ifpd_current_resp_ptr = (void *)ifp->ifp_response_ptr;
	regs->ifpd_base_resp_ptr = (void *)ifp->ifp_response_base;
}

/* convert little endian rls data to big endian */
static void
ifp_i_rls_le_to_be(void *src, int count)
{
	uchar_t tmp;
	uchar_t *ptr = src;

	ASSERT((count % 4) == 0);

	while (count) {

		tmp = ptr[0];
		ptr[0] = ptr[3];
		ptr[3] = tmp;

		tmp = ptr[1];
		ptr[1] = ptr[2];
		ptr[2] = tmp;

		count -= 4;
		ptr += 4;
	}
}

#ifdef IFPDEBUG
static void
ifp_i_print_fcal_position_map(struct ifp *ifp)
{
	char buf[128];

	ifp_i_log(ifp, CE_NOTE, "FC-AL Position Map:");

	/*
	 * Print AL-PAs.
	 */
	(void) sprintf(buf,
	    "No of ports on the loop %x port1 %x port2 %x port3 %x\n",
	    ifp->ifp_fcal_map[0], ifp->ifp_fcal_map[1],
	    ifp->ifp_fcal_map[2], ifp->ifp_fcal_map[3]);
	ifp_i_log(ifp, CE_NOTE, buf);

	/*
	 * Print Loop IDs
	 */
	(void) sprintf(buf,
	    "Loop id1 %x id2 %x\n",
	    ifp_alpa_to_loopid[ifp->ifp_fcal_map[1]],
	    ifp_alpa_to_loopid[ifp->ifp_fcal_map[2]]);
	ifp_i_log(ifp, CE_NOTE, buf);
}

static void
ifp_i_dump_mem(char *buf, void *memp, size_t size)
{
	int j, n;
	char lbuf[80];
	uchar_t *mem = memp;
	char *lptr, *ptr = lbuf;

	ifp_i_log(NULL, CE_NOTE, buf);
	for (; size; size -= n, mem += n) {
		n = size < 16 ? size : 16;
		lptr = lbuf;
		for (j = 0; j < n; j++) {
			(void) sprintf(lptr, "%02x ", mem[j] & 0xFF);
			lptr += 3;

			if (mem[j] >= 27 && mem[j] <= 127)
				ptr[48 + j] = (char)mem[j];
			else
				ptr[48 + j] = '.';
		}
		lbuf[3 * n] = ' ';
		ifp_i_log(NULL, CE_NOTE, lbuf);
	}
}

static int controlled_resets = 1;

static void
ifp_i_test(struct ifp *ifp, struct ifp_cmd *sp)
{
	struct scsi_pkt *pkt;
	struct scsi_address ap;

	static ushort_t reset_this_target = 127;

	ASSERT(sp);
	pkt = CMD2PKT(sp);

#ifndef __lock_lint
	/*
	 * Get the address from the packet - fill in address
	 * structure from pkt on to the local scsi_address structure
	 */
	ap.a_hba_tran = pkt->pkt_address.a_hba_tran;
	ap.a_target = pkt->pkt_address.a_target;
	ap.a_lun = pkt->pkt_address.a_lun;
	ap.a_sublun = pkt->pkt_address.a_sublun;

	if (ifp_test_abort) {
		mutex_exit(IFP_RESP_MUTEX(ifp));
		(void) ifp_scsi_abort(&ap, pkt);
		mutex_enter(IFP_RESP_MUTEX(ifp));
		ifp_debug_enter_count++;
		ifp_test_abort = 0;
	}
	if (ifp_test_abort_all) {
		mutex_exit(IFP_RESP_MUTEX(ifp));
		(void) ifp_scsi_abort(&ap, NULL);
		mutex_enter(IFP_RESP_MUTEX(ifp));
		ifp_debug_enter_count++;
		ifp_test_abort_all = 0;
	}
	if (ifp_test_reset) {
		if (controlled_resets) {
			if (reset_this_target != 127)
				ap.a_target = reset_this_target;
			else
				reset_this_target = ap.a_target;
		}
		mutex_exit(IFP_RESP_MUTEX(ifp));
		(void) ifp_scsi_reset(&ap, RESET_TARGET);
		mutex_enter(IFP_RESP_MUTEX(ifp));
		ifp_debug_enter_count++;
		ifp_test_reset = 0;
	}
	if (ifp_test_reset_all) {
		mutex_exit(IFP_RESP_MUTEX(ifp));
		(void) ifp_scsi_reset(&ap, RESET_ALL);
		mutex_enter(IFP_RESP_MUTEX(ifp));
		ifp_debug_enter_count++;
		ifp_test_reset_all = 0;
	}
	if (ifp_test_fatal) {
		ifp_i_fatal_error(ifp, IFP_FORCE_RESET_BUS);
		ifp_debug_enter_count++;
		ifp_test_fatal = 0;
	}
#endif
}


#ifdef IFPDEBUGX
static void
ifp_prt_lun(ifp_lun_t *lun_p)
{
	IFP_DEBUG(3, (0, SCSI_DEBUG, "lun: ifpl_target %x at 0x%p",
	    lun_p->ifpl_target, (void *)&lun_p->ifpl_target));
	IFP_DEBUG(3, (0, SCSI_DEBUG, "lun: ifpl_next  %x at 0x%p",
	    lun_p->ifpl_next, (void *)&lun_p->ifpl_next));
	IFP_DEBUG(3, (0, SCSI_DEBUG, "lun: ifpl_lun_num  %x at 0x%p",
	    lun_p->ifpl_lun_num, (void *)&lun_p->ifpl_lun_num));
	IFP_DEBUG(3, (0, SCSI_DEBUG, "lun: ifpl_device_type %x at 0x%p",
	    lun_p->ifpl_device_type, (void *)&lun_p->ifpl_device_type));
	IFP_DEBUG(3, (0, SCSI_DEBUG, "lun: ifpl_dip  %x at 0x%p\n",
	    lun_p->ifpl_dip, (void *)&lun_p->ifpl_dip));
}

static void
ifp_prt_target(ifp_target_t *target_p)
{
	ifp_lun_t	*lun_p;

	IFP_DEBUG(3, (0, SCSI_DEBUG, "target: ifpt_tran %x at 0x%p",
	    target_p->ifpt_tran, (void *)&target_p->ifpt_tran));
	IFP_DEBUG(3, (0, SCSI_DEBUG, "target: ifpt_next  %x at 0x%p",
	    target_p->ifpt_next, (void *)&target_p->ifpt_next));
	IFP_DEBUG(3, (0, SCSI_DEBUG, "target: ifpt_lip_cnt %x at 0x%p",
	    target_p->ifpt_lip_cnt, (void *)&target_p->ifpt_lip_cnt));
	IFP_DEBUG(3, (0, SCSI_DEBUG, "target: ifpt_state %x at 0x%p",
	    target_p->ifpt_state, (void *)&target_p->ifpt_state));
	IFP_DEBUG(3, (0, SCSI_DEBUG, "target: ifpt_lun at 0x%p",
	    (void *)&target_p->ifpt_lun));
	IFP_DEBUG(3, (0, SCSI_DEBUG, "target: ifpt_al_pa %x at 0x%p",
	    target_p->ifpt_al_pa, (void *)&target_p->ifpt_al_pa));
	IFP_DEBUG(3, (0, SCSI_DEBUG, "target: ifpt_hard_address %x at 0x%p",
	    target_p->ifpt_hard_address, (void *)&target_p->ifpt_hard_address));
	IFP_DEBUG(3, (0, SCSI_DEBUG, "target: ifpt_loop_id %x at 0x%p\n",
	    target_p->ifpt_loop_id, (void *)&target_p->ifpt_loop_id));
	for (lun_p = &target_p->ifpt_lun; lun_p != NULL;
	    lun_p = lun_p->ifpl_next) {
		ifp_prt_lun(lun_p);
	}
}
#endif
#endif
#ifdef STE_TARGET_MODE

static ushort_t	ifp_i_get_serial_no(ifp_t *);
static void	ifp_i_nv_delay();
static void	ifp_i_nv_write(ifp_t *, ushort_t);
static void	ifp_i_nv_deselect(ifp_t *);
static void	ifp_i_nv_cmd(ifp_t *, uchar_t, ulong_t);
static ushort_t	ifp_i_read_nvram_short(ifp_t *ifp, uchar_t addr);

/*
 * IFP Target Mode functions
 */

ifp_t *
ifp_ifptm_attach(dev_t dev)
{
	ifp_t	*ifp;
	char	*name;

	/*
	 * If not an "ifp" device, return
	 */
	if ((name = ddi_major_to_name(getmajor(dev))) == NULL ||
	    strcmp(name, "ifp") != 0)
		return (NULL);

	/*
	 * If no soft state structure, return
	 */
	if ((ifp = ddi_get_soft_state(ifp_state,
	    IFP_MINOR2INST(getminor(dev)))) == NULL)
		return (NULL);

	/*
	 * If in the process of detaching, return
	 */
	IFP_MUTEX_ENTER(ifp);
	if (ifp->ifp_softstate & IFP_IFPTM_DETACHED) {
		IFP_MUTEX_EXIT(ifp);
		return (NULL);
	}

	/*
	 * Set the attached flag
	 */
	ifp->ifp_softstate |= IFP_IFPTM_ATTACHED;
	IFP_MUTEX_EXIT(ifp);

	ifp->ifp_serial_num = ifp_i_get_serial_no(ifp);
	ifp->ifp_tm_dma_attr = &dma_ifpattr;
	ifp->ifp_tm_hard_loop_id = 0xff;

	return (ifp);
}


int
ifp_ifptm_detach(ifp_t *ifp)
{
	IFP_MUTEX_ENTER(ifp);
	ifp->ifp_softstate &= ~IFP_IFPTM_ATTACHED;
	IFP_MUTEX_EXIT(ifp);

	return (0);
}


void
ifp_ifptm_init(ifp_t *ifp)
{
	IFP_MUTEX_ENTER(ifp);

	/* download firmware */
	(void) ifp_i_download_fw(ifp);

	/* reset the port */
	(void) ifp_i_reset_interface(ifp, 0);

	IFP_MUTEX_EXIT(ifp);
}


void
ifp_ifptm_reset(ifp_t *ifp)
{
	IFP_MUTEX_ENTER(ifp);

	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_bus_icr,
	    IFP_BUS_ICR_DISABLE_INTS);
	IFP_CLEAR_RISC_INT(ifp);

	IFP_MUTEX_EXIT(ifp);
}


void
ifp_ifptm_mbox_cmd_init(struct ifp *ifp, struct ifp_mbox_cmd *mbox_cmdp,
    uchar_t n_mbox_out, uchar_t n_mbox_in,
    uint16_t reg0, uint16_t reg1, uint16_t reg2,
    uint16_t reg3, uint16_t reg4, uint16_t reg5,
    uint16_t reg6, uint16_t reg7)
{
	ifp_i_mbox_cmd_init(ifp, mbox_cmdp, n_mbox_out, n_mbox_in,
	    reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7);
}


int
ifp_ifptm_mbox_cmd_start(struct ifp *ifp, struct ifp_mbox_cmd *mbox_cmdp)
{
	int	ret;

#ifdef __lock_lint
	IFP_MUTEX_ENTER(ifp);
#endif
	ret = ifp_i_mbox_cmd_start(ifp, mbox_cmdp);
#ifdef __lock_lint
	IFP_MUTEX_EXIT(ifp);
#endif
	return (ret);
}


int
ifp_ifptm_alive(struct ifp *ifp)
{
	return (ifp_i_alive(ifp));
}

static ushort_t
ifp_i_get_serial_no(ifp_t *ifp)
{
	ushort_t	i, j, k;

	IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_PAUSE);

	j = ifp_i_read_nvram_short(ifp, 11);
	k = ifp_i_read_nvram_short(ifp, 12);

	i = ((0xff00 & j) << 8) | k;

	IFP_WRITE_RISC_HCCR(ifp, IFP_HCCR_CMD_RELEASE);

	return (i);
}


/*
 * Routines used for reading nvram from the QLA2100 board.
 * We use this to calculate the serial number of a given board, and
 * check against the ifp.conf file to see if it should be initialized
 * in target mode.
 */
#define	NV_NUM_ADDR_BITS	8
#define	NV_START_BIT		4
#define	NV_READ_OP		(NV_START_BIT + 2 << 5)
#define	NV_DELAY_COUNT		100
#define	NV_DESELECT		0
#define	NV_CLOCK		1
#define	NV_SELECT		2
#define	NV_DATA_OUT		4
#define	NV_DATA_IN		8

/*
 * NVRAM delay loop for timing
 */
static void
ifp_i_nv_delay()
{
	ulong_t		delay;

	delay = NV_DELAY_COUNT;

	while (delay--)
		;
}


/*
 * Write and clock bit to NVRAM
 */
static void
ifp_i_nv_write(ifp_t *ifp, ushort_t i)
{
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_nvram,
	    i | NV_SELECT);
	ifp_i_nv_delay();

	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_nvram,
	    i | NV_SELECT | NV_CLOCK);
	ifp_i_nv_delay();

	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_nvram,
	    i | NV_SELECT);
	ifp_i_nv_delay();
}


/*
 * NVRAM deselect
 */
static void
ifp_i_nv_deselect(ifp_t *ifp)
{
	IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_nvram,
	    NV_DESELECT);
	ifp_i_nv_delay();
}


/*
 * Load command, address and data if desired.
 */
static void
ifp_i_nv_cmd(ifp_t *ifp, uchar_t count, ulong_t cmd)
{
	ushort_t	word1;
	ushort_t	i = 0;

	while (count--)		/* Loop until all data is transferred */
	{
		if (cmd & 0x00800000) {		/* Test for bit in data. */
			word1 = NV_DATA_OUT;	/* Set output bit for NVRAM */
			i |= 1;
		}
		else
			word1 = 0;		/* Else, zero bit */
		ifp_i_nv_write(ifp, word1);	/* Position data for next bit */
		cmd <<= 1;
		i <<= 1;
	}
}


static ushort_t
ifp_i_read_nvram_short(ifp_t *ifp, uchar_t addr)
{
	ulong_t		cmd;
	ushort_t	i, ret, bit;

	cmd = (NV_READ_OP << 3) | addr;
	cmd <<= 13;

	ifp_i_nv_cmd(ifp, 3 + NV_NUM_ADDR_BITS, cmd);
	ret = 0;
	for (bit = 0; bit < 16; bit++) {
		IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_nvram,
		    NV_SELECT | NV_CLOCK);
		ifp_i_nv_delay();		/* Go delay for chip */
		ret <<= 1;			/* Shift in zero bit */

		/*
		 * Get register data
		 */
		i = IFP_READ_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_nvram);
		if (i & NV_DATA_IN)		/* Test for one bit */
			ret |= 1;		/* Remove clock */
		IFP_WRITE_BIU_REG(ifp, &ifp->ifp_biu_reg->ifp_pci_nvram,
		    NV_SELECT);
		ifp_i_nv_delay();
	}
	ifp_i_nv_deselect(ifp);			/* Deselect the chip */
	return (ret);
}
#endif /* STE_TARGET_MODE */

/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This file may contain confidential information of Intel Corporation
 * and should not be distributed in source form without approval
 * from Oracle Legal.
 */

#ifndef _SCU_VAR_H
#define	_SCU_VAR_H

#ifdef  __cplusplus
extern "C" {
#endif

/* standard header files */
#include <sys/cpuvar.h>
#include <sys/note.h>
#include <sys/scsi/scsi.h>
#include <sys/scsi/generic/sas.h>
#include <sys/scsi/impl/scsi_reset_notify.h>
#include <sys/scsi/impl/transport.h>
#include <sys/queue.h>
#include <sys/byteorder.h>
#include <sys/pci.h>

/*
 * FMA header files
 */
#include <sys/ddifm.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/fm/io/ddi.h>

/* SCIL header files */
#include "sci_util.h"
#include "sci_types.h"
#include "sci_controller_constants.h"
#include "scif_library.h"
#include "scif_remote_device.h"
#include "scif_controller.h"
#include "sci_object.h"
#include "scif_config_parameters.h"
#include "scic_config_parameters.h"
#include "sci_logger.h"
#include "scic_library.h"
#include "sci_controller.h"
#include "sci_memory_descriptor_list.h"
#include "scif_io_request.h"
#include "scic_controller.h"
#include "scif_user_callback.h"
#include "scic_user_callback.h"
#include "scic_sds_controller.h"
#include "scif_domain.h"
#include "scif_sas_remote_device.h"
#include "scif_sas_domain.h"
#include "scic_remote_device.h"
#include "scic_phy.h"
#include "scif_sas_controller.h"
#include "sci_base_library.h"
#include "scic_io_request.h"
#include "scic_sds_library.h"
#include "sci_base_state_machine.h"
#include "intel_pci.h"

/*
 * Lint unhappy with TRUE (ie. !FALSE) defined in "sci_types.h" when used in
 * condition expression
 */
#ifdef TRUE
#undef TRUE
#define	TRUE 1
#endif

typedef struct scu_subctl scu_subctl_t;
typedef struct scu_ctl scu_ctl_t;
typedef struct scu_iport scu_iport_t;
typedef struct scu_phy scu_phy_t;
typedef struct scu_tgt scu_tgt_t;
typedef struct scu_lun scu_lun_t;
typedef struct scu_cmd scu_cmd_t;
typedef struct scu_io_slot scu_io_slot_t;
typedef	struct scu_task_arg scu_task_arg_t;

/* SCU private header files */
#include <sys/scsi/adapters/scu/scu_proto.h>
#include <sys/scsi/adapters/scu/scu_scsa.h>
#include <sys/scsi/adapters/scu/scu_smhba.h>

#define	SCU_MAX_BAR	2

#define	SCU_TGT_SSTATE_SIZE	64
#define	SCU_LUN_SSTATE_SIZE	4
#define	SCU_MAX_UA_SIZE		32
#define	SCU_MAX_SAS_QUEUE_DEPTH	32
#define	SCU_PM_MAX_NAMELEN	16

#define	SCU_INVALID_TARGET_NUM (uint16_t)-1

#define	SCU_MAX_CQ_THREADS	4

extern void *scu_softc_state;
extern void *scu_iport_softstate;
extern clock_t scu_watch_tick;

/*
 * Data struct for every memory descriptor
 */
typedef struct scu_lib_md_item {
	ddi_dma_handle_t	scu_lib_md_dma_handle;
	ddi_acc_handle_t	scu_lib_md_acc_handle;
} scu_lib_md_item_t;

/*
 * Data struct for every io slot
 */
struct scu_io_slot {
	ddi_dma_handle_t	scu_io_dma_handle;
	ddi_acc_handle_t	scu_io_acc_handle;
	caddr_t			scu_io_virtual_address;
	uint64_t		scu_io_physical_address;
	int			scu_io_active_timeout;
	scu_cmd_t		*scu_io_slot_cmdp;
	uint16_t		scu_io_lib_tag;	/* SCIL library tag */
	scu_subctl_t		*scu_io_subctlp;
};

/*
 * Timer definition for SCIL usage
 */
typedef struct scu_timer {
	timeout_id_t		timeout_id;
	SCI_TIMER_CALLBACK_T	callback;
	void			*args;
} scu_timer_t;

/*
 * thread struct for handling completed command queue
 */
typedef struct scu_cq_thread {
	kthread_t	*scu_cq_thread;
	kt_did_t	scu_cq_thread_id;
	kcondvar_t	scu_cq_thread_cv;	/* protected by scu_cq_lock */
	scu_ctl_t	*scu_cq_ctlp;
} scu_cq_thread_t;

typedef struct scu_event {
	STAILQ_ENTRY(scu_event)	ev_next;
	int			ev_events;
	int			ev_index;
} scu_event_t;

enum scu_event_id {
	SCU_EVENT_ID_QUIT,
	SCU_EVENT_ID_CONTROLLER_ERROR,
	SCU_EVENT_ID_CONTROLLER_RESET,
	SCU_EVENT_ID_DEVICE_ERROR,
	SCU_EVENT_ID_DEVICE_RESET,
	SCU_EVENT_ID_DEVICE_TIMEOUT,
	SCU_EVENT_ID_DEVICE_LUN_TIMEOUT,
	SCU_EVENT_ID_DEVICE_READY,
	SCU_EVENT_ID_NUM
};

#define	SCU_EVENT_ID2BIT(id)		(1 << (id))
#define	SCU_EVENT_QUIT			SCU_EVENT_ID2BIT(SCU_EVENT_ID_QUIT)
#define	SCU_EVENT_CONTROLLER_ERROR \
	SCU_EVENT_ID2BIT(SCU_EVENT_ID_CONTROLLER_ERROR)
#define	SCU_EVENT_CONTROLLER_RESET \
	SCU_EVENT_ID2BIT(SCU_EVENT_ID_CONTROLLER_RESET)
#define	SCU_EVENT_DEVICE_ERROR \
	SCU_EVENT_ID2BIT(SCU_EVENT_ID_DEVICE_ERROR)
#define	SCU_EVENT_DEVICE_RESET \
	SCU_EVENT_ID2BIT(SCU_EVENT_ID_DEVICE_RESET)
#define	SCU_EVENT_DEVICE_TIMEOUT \
	SCU_EVENT_ID2BIT(SCU_EVENT_ID_DEVICE_TIMEOUT)
#define	SCU_EVENT_DEVICE_LUN_TIMEOUT \
	SCU_EVENT_ID2BIT(SCU_EVENT_ID_DEVICE_LUN_TIMEOUT)
#define	SCU_EVENT_DEVICE_READY \
	SCU_EVENT_ID2BIT(SCU_EVENT_ID_DEVICE_READY_TIMEOUT)

/*
 * SCILF domain related structure
 */
typedef struct scu_domain {
	SCI_DOMAIN_HANDLE_T	scu_lib_domain_handle;
} scu_domain_t;

struct scu_subctl {
	scu_ctl_t		*scus_ctlp;
	int			scus_num;

	/* State of sub-controller */
	uint32_t
		scus_adapter_is_ready	:1,
		scus_error		:1,
		scus_failed		:1,
		scus_resetting		:1,
		scus_stopped		:1,
		scus_lib_start_timeout	:1,
		reserved		:26;

	/* SCIF controller handle */
	SCI_CONTROLLER_HANDLE_T	scus_scif_ctl_handle;

	/* SCIC controller handle */
	SCI_CONTROLLER_HANDLE_T	scus_scic_ctl_handle;

	/* SCIF Domain related structure */
	scu_domain_t		scus_domains[SCI_MAX_DOMAINS];

	/* SCIL memory descriptors */
	scu_lib_md_item_t	*scus_lib_memory_descriptors;
	int			scus_lib_memory_descriptor_count;

	/* Structure for every slot */
	uint32_t		scus_slot_num;
	scu_io_slot_t		*scus_io_slots;

	kmutex_t		scus_slot_lock;
	uint32_t		scus_slot_active_num;
	uint32_t		scus_tag_active_num;

	SCIC_CONTROLLER_HANDLER_METHODS_T	\
		scus_lib_intr_handler[SCI_MAX_MSIX_MESSAGES];
	SCIC_CONTROLLER_HANDLER_METHODS_T	\
		scus_lib_poll_handler;

	/* Mutex used by SCIL */
	kmutex_t		scus_lib_hprq_lock;

	/* Mutex used by SCIL for SCIF remote device construction/destruction */
	kmutex_t		scus_lib_remote_device_lock;

	/* Taskq used by SCIL */
	ddi_taskq_t		*scus_lib_internal_taskq;

	kcondvar_t		scus_reset_cv;
	kcondvar_t		scus_reset_complete_cv;
	kthread_t		*scus_task_start_thread;
};

/*
 * HBA node softstate
 */
struct scu_ctl {
	dev_info_t		*scu_dip;
	int			scu_instance;

	/* mutex for flag */
	kmutex_t		scu_lock;
	kcondvar_t		scu_cv;

	/*
	 * possible initiator PHY WWNs
	 */
	uint64_t
		sas_wwns[SCI_MAX_CONTROLLERS * SCI_MAX_PHYS];

	/* State of OSSL controller */
	uint32_t
		scu_started		:1,
		scu_quiesced		:1,
		scu_is_suspended	:1,
		reserved		:29;

	SCI_LIBRARY_HANDLE_T	scu_scif_lib_handle;
	SCI_LIBRARY_HANDLE_T	scu_scic_lib_handle;

	int			scu_lib_max_ctl_num;

	uint32_t		scu_lib_non_dma_needed;
	void			*scu_lib_non_dma_memory;

	int			scu_lib_ctl_num;
	scu_subctl_t		*scu_subctls;

	SCIF_USER_PARAMETERS_T	scu_scif_user_parms;
	SCIC_USER_PARAMETERS_T	scu_scic_user_parms;

	/* PCI configuration space handle */
	ddi_acc_handle_t	scu_pcicfg_handle;

	/* Data buffer DMA attribute */
	ddi_dma_attr_t		scu_data_dma_attr;

	/* SCSA related components */
	scsi_hba_tran_t		*scu_scsa_tran;
	smp_hba_tran_t		*scu_smp_tran;
	struct scsi_reset_notify_entry	*scu_reset_notify_list;

	/* iport related */
	list_t			scu_iports;
	kmutex_t		scu_iport_lock;
	scsi_hba_iportmap_t	*scu_iportmap;
	int			scu_iport_num;

	/* phy related */
	sas_phymap_t		*scu_phymap;
	int			scu_phymap_active;

	/* Root phys of the controller */
	scu_phy_t		*scu_root_phys;
	int			scu_root_phy_num;

	/* target related */
	scu_tgt_t		**scu_tgts;
	int			scu_max_dev;

	/* SCU BAR mapping */
	ddi_acc_handle_t	scu_bar_map[SCU_MAX_BAR];
	caddr_t			scu_bar_addr[SCU_MAX_BAR];

	/* Watchdog handler */
	timeout_id_t		scu_watchdog_timeid;

	/* Draining checking handler */
	timeout_id_t		scu_quiesce_timeid;

	/* Taskq for topology discovery */
	ddi_taskq_t		*scu_discover_taskq;

	/* Single linked tail queue for completed commands */
	STAILQ_HEAD(cqhead, scu_cmd)	scu_cq;
	kmutex_t		scu_cq_lock;
	kcondvar_t		scu_cq_cv;
	int			scu_cq_thread_num;
	scu_cq_thread_t		*scu_cq_thread_list;
	uint32_t		scu_cq_next_thread;
	int			scu_cq_stop;

	kcondvar_t		scu_cmd_complete_cv;

	/* Components for interrupt */
	ddi_intr_handler_t
	    *scu_intr_handler[SCI_MAX_CONTROLLERS*SCI_MAX_MSIX_MESSAGES];
	ddi_intr_handle_t	*scu_intr_htable;
	int			scu_intr_type;
	size_t			scu_intr_size;
	int			scu_intr_count;
	uint_t			scu_intr_pri;
	int			scu_intr_cap;

	/* Internal events handling */
	kmutex_t		scu_event_lock;
	kthread_t		*scu_event_thread;
	STAILQ_HEAD(eqhead, scu_event)	scu_eventq;
	int			scu_events;
	int			scu_event_num[SCU_EVENT_ID_NUM];
	kcondvar_t		scu_event_quit_cv;
	kcondvar_t		scu_event_disp_cv;
	scu_event_t		*scu_event_tgts;
	scu_event_t		*scu_event_ctls;

	/* Register Access Handles */
	ddi_device_acc_attr_t	reg_acc_attr;

	/* FMA capabilities */
	int			scu_fm_cap;

	/*
	 * Receptacle information - FMA
	 */
	char			*recept_labels[SCI_MAX_CONTROLLERS];
	char			*recept_pm[SCI_MAX_CONTROLLERS];

};

/*
 * iport softstate
 */
struct scu_iport {
	dev_info_t		*scui_dip;

	/* Pointer to hba struct */
	scu_ctl_t		*scui_ctlp;
	/* Pointer to sub-controller */
	scu_subctl_t		*scui_subctlp;

	list_node_t		list_node;

	/* Pointer to the primary phy struct */
	scu_phy_t		*scui_primary_phy;

	/* Pointer to the SCIL PORT struct */
	SCI_DOMAIN_HANDLE_T	scui_lib_domain;

	/* iport mutex */
	kmutex_t		scui_lock;

	/* Unit address */
	char			*scui_ua;
	enum {
		SCU_UA_INACTIVE,
		SCU_UA_ACTIVE
	} scui_ua_state;

	/* list of phys contained in this iport */
	list_t			scui_phys;
	int			scui_phy_num;
	scsi_hba_tgtmap_t	*scui_iss_tgtmap;
	ddi_soft_state_bystr	*scui_tgt_sstate;

};

typedef enum {
	SCU_DTYPE_NIL,
	SCU_DTYPE_SATA,
	SCU_DTYPE_SAS,
	SCU_DTYPE_EXPANDER
} scu_dtype_t;

struct scu_phy {
	list_node_t		list_node;

	uint8_t			scup_hba_index;
	uint8_t			scup_lib_index;
	int			scup_wide_port;
	scu_dtype_t		scup_dtype;

	/* Pointer to hba struct */
	scu_ctl_t		*scup_ctlp;
	/* Pointer to sub-controller */
	scu_subctl_t		*scup_subctlp;
	/* Pointer to iport struct */
	scu_iport_t		*scup_iportp;

	/* Pointer to SCIL phy struct */
	SCI_PHY_HANDLE_T	scup_lib_phyp;

	/* SCIC PHY properties */
	SCIC_PHY_PROPERTIES_T	scup_lib_phy_prop;
	uint64_t		scup_sas_address;
	uint64_t		scup_remote_sas_address;

	kmutex_t		scup_lock;

	/* OSSL phy status */
	uint32_t
		scup_ready		:1,
		reserved		:31;

	scu_tgt_t		*scup_tgtp;
	uint16_t		sysevent;
				/* Bits indicating SAS event info */
	kstat_t			*phy_stats;	/* kstats for this phy */

	char			att_port_pm_str[SCU_PM_MAX_NAMELEN + 1];
	char			tgt_port_pm_str[SCU_PM_MAX_NAMELEN + 1];
};

struct scu_tgt {
	dev_info_t		*scut_dip;
	scu_ctl_t		*scut_ctlp;
	scu_subctl_t		*scut_subctlp;
	scu_iport_t		*scut_iportp;
	scu_phy_t		*scut_phyp;

	SCI_REMOTE_DEVICE_HANDLE_T	scut_lib_remote_device;

	/* List of luns contained in this target */
	list_t			scut_luns;
	ddi_soft_state_bystr	*scut_lun_sstate;
	scu_dtype_t		scut_dtype;
	int			scut_protocol_sata;
	smp_device_t		*scut_smp_sd;

	kmutex_t		scut_lock;
	int			scut_ref_count;
	char			scut_unit_address[SCU_MAX_UA_SIZE];
	int			scut_is_da;
	int			scut_qdepth;

	/* Single linked tail queue for waiting commands */
	STAILQ_HEAD(wqhead, scu_cmd)	scut_wq;

	/* Number of pkts on waiting queue */
	int			scut_wq_pkts;
	kmutex_t		scut_wq_lock;

	char			*scut_iport_ua;
	uint8_t			scut_hba_phynum;

	uint32_t
		scut_timeout		:1,
		scut_error		:1,
		scut_draining		:1,
		scut_resetting		:1,
		scut_lib_tgt_valid	:1,
		scut_lib_tgt_ready	:1,
		scut_lib_tgt_failed	:1,
		scut_reserved		:25;

	uint16_t		scut_tgt_num;

	/* Number of pkts sent to this target */
	int			scut_active_pkts;

	kcondvar_t		scut_reset_cv;
	kcondvar_t		scut_reset_complete_cv;
	int			scut_reset_func;
	int			scut_lun_timeouts;
};

struct scu_lun {
	list_node_t		list_node;
	scu_tgt_t		*scul_tgtp;
	struct scsi_device	*scul_sd;
	char			scul_unit_address[SCU_MAX_UA_SIZE];

	scsi_lun_t		scul_scsi_lun;
	uint64_t		scul_lun_num;

	/* Protected by scut_lock */
	int			scul_timeout;
};

#define	SCU_SAS_ADDRESS(x, y)	(((uint64_t)(x) << 32) | (y))

#define	SCU_LIB2HBA_PHY_INDEX(controller_index, phy_index)	\
	(controller_index * SCI_MAX_PHYS + phy_index)

#define	SCU_HBA2LIB_PHY_INDEX(hba_index) (hba_index % SCI_MAX_PHYS)

#define	SCU_SUCCESS	1	/* successful return */
#define	SCU_FAILURE	-1	/* unsuccessful return */

#define	SCU_TASK_GOOD		1
#define	SCU_TASK_BUSY		2
#define	SCU_TASK_ERROR		3
#define	SCU_TASK_DEVICE_RESET	4
#define	SCU_TASK_TIMEOUT	5
#define	SCU_TASK_CHECK		6

/* state value for scu_attach */
#define	SCU_ATTACHSTATE_CSOFT_ALLOC		(0x1 << 0)
#define	SCU_ATTACHSTATE_PCICFG_SETUP		(0x1 << 1)
#define	SCU_ATTACHSTATE_BAR_MAPPING_SETUP	(0x1 << 2)
#define	SCU_ATTACHSTATE_LIB_NONDMA_ALLOC	(0x1 << 3)
#define	SCU_ATTACHSTATE_SUBCTL_ALLOC		(0x1 << 4)
#define	SCU_ATTACHSTATE_SUBCTL_SETUP		(0x1 << 5)
#define	SCU_ATTACHSTATE_LIB_MEMORY_DESCRIPTOR_ALLOC	(0x1 << 6)
#define	SCU_ATTACHSTATE_IO_SLOT_ALLOC		(0x1 << 7)
#define	SCU_ATTACHSTATE_IO_REQUEST_ALLOC	(0x1 << 8)
#define	SCU_ATTACHSTATE_INTR_ADDED		(0x1 << 9)
#define	SCU_ATTACHSTATE_MUTEX_INIT		(0x1 << 10)
#define	SCU_ATTACHSTATE_PHY_ALLOC		(0x1 << 11)
#define	SCU_ATTACHSTATE_TGT_ALLOC		(0x1 << 12)
#define	SCU_ATTACHSTATE_CQ_ALLOC		(0x1 << 13)
#define	SCU_ATTACHSTATE_DISCOVER_TASKQ		(0x1 << 14)
#define	SCU_ATTACHSTATE_EVENT_THREAD		(0x1 << 15)
#define	SCU_ATTACHSTATE_LIB_INTERNAL_TASKQ	(0x1 << 16)
#define	SCU_ATTACHSTATE_SCSI_ATTACH		(0x1 << 17)
#define	SCU_ATTACHSTATE_IPORTMAP_CREATE		(0x1 << 18)
#define	SCU_ATTACHSTATE_PHYMAP_CREATE		(0x1 << 19)
#define	SCU_ATTACHSTATE_CTL_STARTED		(0x1 << 20)
#define	SCU_ATTACHSTATE_INTR_ENABLED		(0x1 << 21)
#define	SCU_ATTACHSTATE_WATCHDOG_HANDLER	(0x1 << 22)
#define	SCU_ATTACHSTATE_FMA_SUPPORT		(0x1 << 23)

/* state value for scu_iport_attach */
#define	SCU_IPORT_ATTACH_ALLOC_SOFT		(0x1 << 0)
#define	SCU_IPORT_ATTACH_MUTEX_INIT		(0x1 << 1)
#define	SCU_IPORT_ATTACH_PHY_LIST_CREATE	(0x1 << 2)
#define	SCU_IPORT_ATTACH_TGT_STATE_ALLOC	(0x1 << 3)

uint_t scu_poll_intr(scu_ctl_t *);
int scu_disable_intrs(scu_ctl_t *);
int scu_enable_intrs(scu_ctl_t *);
int scu_alloc_task_request(scu_subctl_t *, scu_io_slot_t **, size_t);
void scu_free_task_request(scu_subctl_t *, scu_io_slot_t **);
int scu_timeout_recover(scu_subctl_t *, scu_tgt_t *);
void scu_timeout(scu_subctl_t *, scu_tgt_t *, scu_cmd_t *);
void scu_device_error(scu_subctl_t *, scu_tgt_t *);
void scu_controller_error(scu_subctl_t *);
void scu_event_dispatch(scu_ctl_t *, enum scu_event_id, int);
int scu_io_request_complete(scu_ctl_t *, scu_tgt_t *, scu_cmd_t *,
    SCI_IO_STATUS);
int scu_prepare_tag(scu_subctl_t *, scu_cmd_t *);
scu_io_slot_t *scu_detach_cmd(scu_cmd_t *);
void scu_free_tag(scu_subctl_t *, scu_io_slot_t *);
void scu_clear_active_task(scu_subctl_t *, scu_cmd_t *);
void scu_flush_cmd(scu_ctl_t *, scu_cmd_t *);
void scu_log(scu_ctl_t *, int, char *, ...);

extern uint32_t scu_debug_object_mask;
extern uint8_t scu_debug_level_mask;
extern int scu_domain_discover_timeout;
extern int scu_device_discover_timeout;
extern SCIC_SMP_PASSTHRU_REQUEST_CALLBACKS_T smp_passthru_cb;

#ifdef	DEBUG
#define	SCU_DEBUG
#endif

#ifdef	SCU_DEBUG
void scu_prt(scu_ctl_t *, uint8_t, char *, ...);

#define	SCUDBG(scu_ctlp, object, level, fmt, args ...)		\
	if (((scu_debug_object_mask) & (object)) &&			\
	    ((scu_debug_level_mask) & (level))) {			\
		scu_prt(scu_ctlp, level, fmt, ## args);		\
	}
#else
#define	SCUDBG(scu_ctlp, object, level, fmt, args ...)
#endif

/* debug levels */
#define	SCUDBG_ERROR		(0x01)
#define	SCUDBG_WARNING		(0x02)
#define	SCUDBG_INFO		(0x04)
#define	SCUDBG_TRACE		(0x08)
#define	SCUDBG_STATES		(0x10)

/* debug objects */
#define	SCUDBG_HBA		(0x00000001)
#define	SCUDBG_IPORT		(0x00000002)
#define	SCUDBG_PHY		(0x00000004)
#define	SCUDBG_TGT		(0x00000008)
#define	SCUDBG_LUN		(0x00000010)
#define	SCUDBG_DOMAIN		(0x00000020)
#define	SCUDBG_SCIF		(0x00000040)
#define	SCUDBG_SCIC		(0x00000080)
#define	SCUDBG_INIT		(0x00000100)
#define	SCUDBG_IO		(0x00000200)
#define	SCUDBG_HOTPLUG		(0x00000400)
#define	SCUDBG_SGPIO		(0x00000800)
#define	SCUDBG_STP		(0x00001000)
#define	SCUDBG_SMP		(0x00002000)
#define	SCUDBG_INTR		(0x00004000)
#define	SCUDBG_ADDR		(0x00008000)
#define	SCUDBG_LOCK		(0x00010000)
#define	SCUDBG_WATCH		(0x00020000)
#define	SCUDBG_TASK		(0x00040000)
#define	SCUDBG_DRAIN		(0x00080000)
#define	SCUDBG_TIMER		(0x00100000)
#define	SCUDBG_MAP		(0x00200000)
#define	SCUDBG_RECOVER		(0x00400000)
#define	SCUDBG_SMHBA		(0x00800000)
#define	SCUDBG_FMA		(0x01000000)

#ifdef  __cplusplus
}
#endif

#endif /* _SCU_VAR_H */

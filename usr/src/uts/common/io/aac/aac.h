/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 2005-06 Adaptec, Inc.
 * Copyright (c) 2005-06 Adaptec Inc., Achim Leubner
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2001 Scott Long
 * Copyright (c) 2000 BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *    $FreeBSD: src/sys/dev/aac/aacvar.h,v 1.47 2005/10/08 15:55:09 scottl Exp $
 */

#ifndef	_AAC_H_
#define	_AAC_H_

#ifdef	__cplusplus
extern "C" {
#endif

#define	AAC_ROUNDUP(x, y)		(((x) + (y) - 1) / (y) * (y))

#define	AAC_TYPE_DEVO			1
#define	AAC_TYPE_ALPHA			2
#define	AAC_TYPE_BETA			3
#define	AAC_TYPE_RELEASE		4

#ifndef	AAC_DRIVER_BUILD
#define	AAC_DRIVER_BUILD		1
#endif

#define	AAC_DRIVER_MAJOR_VERSION	2
#define	AAC_DRIVER_MINOR_VERSION	2
#define	AAC_DRIVER_BUGFIX_LEVEL		11
#define	AAC_DRIVER_TYPE			AAC_TYPE_RELEASE

#define	STR(s)				# s
#define	AAC_VERSION(a, b, c)		STR(a.b.c)
#define	AAC_DRIVER_VERSION		AAC_VERSION(AAC_DRIVER_MAJOR_VERSION, \
					AAC_DRIVER_MINOR_VERSION, \
					AAC_DRIVER_BUGFIX_LEVEL)

#define	AACOK				0
#define	AACERR				-1

#define	AAC_MAX_ADAPTERS		64

/* Definitions for mode sense */
#ifndef	SD_MODE_SENSE_PAGE3_CODE
#define	SD_MODE_SENSE_PAGE3_CODE	0x03
#endif

#ifndef	SD_MODE_SENSE_PAGE4_CODE
#define	SD_MODE_SENSE_PAGE4_CODE	0x04
#endif

#ifndef	SCMD_SYNCHRONIZE_CACHE
#define	SCMD_SYNCHRONIZE_CACHE		0x35
#endif

/*
 * The controller reports status events in AIFs. We hang on to a number of
 * these in order to pass them out to user-space management tools.
 */
#define	AAC_AIFQ_LENGTH			64

#ifdef __x86
#define	AAC_IMMEDIATE_TIMEOUT		30	/* seconds */
#else
#define	AAC_IMMEDIATE_TIMEOUT		60	/* seconds */
#endif
#define	AAC_FWUP_TIMEOUT		180	/* wait up to 3 minutes */
#define	AAC_IOCTL_TIMEOUT		900	/* wait up to 15 minutes */
#define	AAC_SYNC_TIMEOUT		900	/* wait up to 15 minutes */

/* Adapter hardware interface types */
#define	AAC_HWIF_UNKNOWN		0
#define	AAC_HWIF_I960RX			1
#define	AAC_HWIF_RKT			2

#define	AAC_TYPE_UNKNOWN		0
#define	AAC_TYPE_SCSI			1
#define	AAC_TYPE_SATA			2
#define	AAC_TYPE_SAS			3

#define	AAC_LS32(d)			((uint32_t)((d) & 0xffffffffull))
#define	AAC_MS32(d)			((uint32_t)((d) >> 32))
#define	AAC_LO32(p64)			((uint32_t *)(p64))
#define	AAC_HI32(p64)			((uint32_t *)(p64) + 1)

/*
 * Internal events that will be handled serially by aac_event_thread()
 */
#define	AAC_EVENT_AIF			(1 << 0)
#define	AAC_EVENT_TIMEOUT		(1 << 1)
#define	AAC_EVENT_SYNCTICK		(1 << 2)

/*
 * AAC_CMDQ_SYNC should be 0 and AAC_CMDQ_ASYNC be 1 for Sync FIB io
 * to be served before async FIB io, see aac_start_waiting_io().
 * So that io requests sent by interactive userland commands get
 * responded asap.
 */
enum aac_cmdq {
	AAC_CMDQ_SYNC,	/* sync FIB queue */
	AAC_CMDQ_ASYNC,	/* async FIB queue */
	AAC_CMDQ_NUM
};

/*
 * IO command flags
 */
#define	AAC_IOCMD_SYNC		(1 << AAC_CMDQ_SYNC)
#define	AAC_IOCMD_ASYNC		(1 << AAC_CMDQ_ASYNC)
#define	AAC_IOCMD_OUTSTANDING	(1 << AAC_CMDQ_NUM)
#define	AAC_IOCMD_ALL		(AAC_IOCMD_SYNC | AAC_IOCMD_ASYNC | \
				AAC_IOCMD_OUTSTANDING)

struct aac_cmd_queue {
	struct aac_cmd *q_head; /* also as the header of aac_cmd */
	struct aac_cmd *q_tail;
};

struct aac_card_type {
	uint16_t vendor;	/* PCI Vendor ID */
	uint16_t device;	/* PCI Device ID */
	uint16_t subvendor;	/* PCI Subsystem Vendor ID */
	uint16_t subsys;	/* PCI Subsystem ID */
	uint16_t hwif;		/* card chip type: i960 or Rocket */
	uint16_t quirks;	/* card odd limits */
	uint16_t type;		/* hard drive type */
	char *vid;		/* ASCII data for INQUIRY command vendor id */
	char *desc;		/* ASCII data for INQUIRY command product id */
};

/* Device types */
#define	AAC_DEV_LD		0	/* logical device */
#define	AAC_DEV_PD		1	/* physical device */

/* Device flags */
#define	AAC_DFLAG_VALID		(1 << 0)
#define	AAC_DFLAG_CONFIGURING	(1 << 1)

#define	AAC_DEV_IS_VALID(dvp)	((dvp)->flags & AAC_DFLAG_VALID)
#define	AAC_P2VTGT(softs, bus, tgt) \
		((softs)->tgt_max * (bus) + (tgt) + AAC_MAX_LD)

/*
 * Device config change events
 */
enum aac_cfg_event {
	AAC_CFG_NULL_NOEXIST = 0,	/* No change with no device */
	AAC_CFG_NULL_EXIST,		/* No change but have device */
	AAC_CFG_ADD,			/* Device added */
	AAC_CFG_DELETE,			/* Device deleted */
	AAC_CFG_CHANGE			/* Device changed */
};

struct aac_device {
	int flags;

	uint8_t type;
	dev_info_t *dip;
	int ncmds[AAC_CMDQ_NUM];	/* outstanding cmds of the device */
	int throttle[AAC_CMDQ_NUM];	/* hold IO cmds for the device */
};

/* Array description */
struct aac_container {
	struct aac_device dev;

	uint32_t cid;		/* container id */
	uint32_t uid;		/* container uid */
	uint64_t size;		/* in block */
	uint8_t locked;
	uint8_t deleted;
	uint8_t reset;		/* container is being reseted */
};

/* Non-DASD phys. device descrption, eg. CDROM or tape */
struct aac_nondasd {
	struct aac_device dev;

	uint32_t bus;
	uint32_t tid;
};

/*
 * The firmware can support a lot of outstanding commands. Each aac_slot
 * is corresponding to one of such commands. It records the command and
 * associated DMA resource for FIB command.
 */
struct aac_slot {
	struct aac_slot *next;	/* next slot in the free slot list */
	int index;		/* index of this slot */
	ddi_acc_handle_t fib_acc_handle;
	ddi_dma_handle_t fib_dma_handle;
	uint64_t fib_phyaddr;	/* physical address of FIB memory */
	struct aac_cmd *acp;	/* command using this slot */
	struct aac_fib *fibp;	/* virtual address of FIB memory */
};

/*
 * Scatter-gather list structure defined by HBA hardware
 */
struct aac_sge {
	uint32_t bcount;	/* byte count */
	union {
		uint32_t ad32;	/* 32 bit address */
		struct {
			uint32_t lo;
			uint32_t hi;
		} ad64;		/* 64 bit address */
	} addr;
};

/* aac_cmd flags */
#define	AAC_CMD_CONSISTENT		(1 << 0)
#define	AAC_CMD_DMA_PARTIAL		(1 << 1)
#define	AAC_CMD_DMA_VALID		(1 << 2)
#define	AAC_CMD_BUF_READ		(1 << 3)
#define	AAC_CMD_BUF_WRITE		(1 << 4)
#define	AAC_CMD_SYNC			(1 << 5) /* use sync FIB */
#define	AAC_CMD_NO_INTR			(1 << 6) /* poll IO, no intr */
#define	AAC_CMD_NO_CB			(1 << 7) /* sync IO, no callback */
#define	AAC_CMD_NTAG			(1 << 8)
#define	AAC_CMD_CMPLT			(1 << 9) /* cmd exec'ed by driver/fw */
#define	AAC_CMD_ABORT			(1 << 10)
#define	AAC_CMD_TIMEOUT			(1 << 11)
#define	AAC_CMD_ERR			(1 << 12)
#define	AAC_CMD_IN_SYNC_SLOT		(1 << 13)

struct aac_softstate;
typedef void (*aac_cmd_fib_t)(struct aac_softstate *, struct aac_cmd *);

struct aac_cmd {
	/*
	 * Note: should be the first member for aac_cmd_queue to work
	 * correctly.
	 */
	struct aac_cmd *next;
	struct aac_cmd *prev;

	struct scsi_pkt *pkt;
	int cmdlen;
	int flags;
	uint32_t timeout; /* time when the cmd should have completed */
	struct buf *bp;
	ddi_dma_handle_t buf_dma_handle;

	/* For non-aligned buffer and SRB */
	caddr_t abp;
	ddi_acc_handle_t abh;

	/* Data transfer state */
	ddi_dma_cookie_t cookie;
	uint_t left_cookien;
	uint_t cur_win;
	uint_t total_nwin;
	size_t total_xfer;
	uint64_t blkno;
	uint32_t bcount;	/* buffer size in byte */
	struct aac_sge *sgt;	/* sg table */

	/* FIB construct function */
	aac_cmd_fib_t aac_cmd_fib;
	/* Call back function for completed command */
	void (*ac_comp)(struct aac_softstate *, struct aac_cmd *);

	struct aac_slot *slotp;	/* slot used by this command */
	struct aac_device *dvp;	/* target device */

	/* FIB for this IO command */
	int fib_size; /* size of the FIB xferred to/from the card */
	struct aac_fib *fibp;

#ifdef DEBUG
	uint32_t fib_flags;
#endif
};

/* Flags for attach tracking */
#define	AAC_ATTACH_SOFTSTATE_ALLOCED	(1 << 0)
#define	AAC_ATTACH_CARD_DETECTED	(1 << 1)
#define	AAC_ATTACH_PCI_MEM_MAPPED	(1 << 2)
#define	AAC_ATTACH_KMUTEX_INITED	(1 << 3)
#define	AAC_ATTACH_SCSI_TRAN_SETUP	(1 << 4)
#define	AAC_ATTACH_COMM_SPACE_SETUP	(1 << 5)
#define	AAC_ATTACH_CREATE_DEVCTL	(1 << 6)
#define	AAC_ATTACH_CREATE_SCSI		(1 << 7)

/* Driver running states */
#define	AAC_STATE_STOPPED	0
#define	AAC_STATE_RUN		(1 << 0)
#define	AAC_STATE_RESET		(1 << 1)
#define	AAC_STATE_QUIESCED	(1 << 2)
#define	AAC_STATE_DEAD		(1 << 3)
#define	AAC_STATE_INTR		(1 << 4)

/*
 * Flags for aac firmware
 * Note: Quirks are only valid for the older cards. These cards only supported
 * old comm. Thus they are not valid for any cards that support new comm.
 */
#define	AAC_FLAGS_SG_64BIT	(1 << 0) /* Use 64-bit S/G addresses */
#define	AAC_FLAGS_4GB_WINDOW	(1 << 1) /* Can access host mem 2-4GB range */
#define	AAC_FLAGS_NO4GB	(1 << 2)	/* quirk: FIB addresses must reside */
					/*	  between 0x2000 & 0x7FFFFFFF */
#define	AAC_FLAGS_256FIBS	(1 << 3) /* quirk: Can only do 256 commands */
#define	AAC_FLAGS_NEW_COMM	(1 << 4) /* New comm. interface supported */
#define	AAC_FLAGS_RAW_IO	(1 << 5) /* Raw I/O interface */
#define	AAC_FLAGS_ARRAY_64BIT	(1 << 6) /* 64-bit array size */
#define	AAC_FLAGS_LBA_64BIT	(1 << 7) /* 64-bit LBA supported */
#define	AAC_FLAGS_17SG		(1 << 8) /* quirk: 17 scatter gather maximum */
#define	AAC_FLAGS_34SG		(1 << 9) /* quirk: 34 scatter gather maximum */
#define	AAC_FLAGS_NONDASD	(1 << 10) /* non-DASD device supported */
#define	AAC_FLAGS_BRKUP		(1 << 11) /* pkt breakup support */
#define	AAC_FLAGS_JBOD		(1 << 12) /* JBOD mode support */

struct aac_softstate;
struct aac_interface {
	int (*aif_get_fwstatus)(struct aac_softstate *);
	int (*aif_get_mailbox)(struct aac_softstate *, int);
	void (*aif_set_mailbox)(struct aac_softstate *, uint32_t,
	    uint32_t, uint32_t, uint32_t, uint32_t);
};

#define	AAC_CTXFLAG_FILLED	0x01	/* aifq's full for this ctx */
#define	AAC_CTXFLAG_RESETED	0x02

struct aac_fib_context {
	uint32_t unique;
	int ctx_idx;
	int ctx_filled;		/* aifq is full for this fib context */
	int ctx_flags;
	int ctx_overrun;
	struct aac_fib_context *next, *prev;
};

#define	AAC_VENDOR_LEN		8
#define	AAC_PRODUCT_LEN		16

struct aac_softstate {
	int card;		/* index to aac_cards */
	uint16_t hwif;		/* card chip type: i960 or Rocket */
	uint16_t vendid;	/* vendor id */
	uint16_t subvendid;	/* sub vendor id */
	uint16_t devid;		/* device id */
	uint16_t subsysid;	/* sub system id */
	char vendor_name[AAC_VENDOR_LEN + 1];
	char product_name[AAC_PRODUCT_LEN + 1];
	uint32_t support_opt;	/* firmware features */
	uint32_t support_opt2;
	uint32_t feature_bits;
	uint32_t atu_size;	/* actual size of PCI mem space */
	uint32_t map_size;	/* mapped PCI mem space size */
	uint32_t map_size_min;	/* minimum size of PCI mem that must be */
				/* mapped to address the card */
	int flags;		/* firmware features enabled */
	int instance;
	dev_info_t *devinfo_p;
	scsi_hba_tran_t *hba_tran;
	int slen;
	int legacy;		/* legacy device naming */
	uint32_t dma_max;	/* for buf breakup */

	/* DMA attributes */
	ddi_dma_attr_t buf_dma_attr;
	ddi_dma_attr_t addr_dma_attr;

	/* PCI spaces */
	ddi_device_acc_attr_t acc_attr;
	ddi_device_acc_attr_t reg_attr;
	ddi_acc_handle_t pci_mem_handle;
	uint8_t *pci_mem_base_vaddr;
	uint32_t pci_mem_base_paddr;

	struct aac_interface aac_if;	/* adapter hardware interface */

	struct aac_cmd sync_ac;		/* sync FIB */

	/* Communication space */
	struct aac_comm_space *comm_space;
	ddi_acc_handle_t comm_space_acc_handle;
	ddi_dma_handle_t comm_space_dma_handle;
	uint32_t comm_space_phyaddr;

	/* Old Comm. interface: message queues */
	struct aac_queue_table *qtablep;
	struct aac_queue_entry *qentries[AAC_QUEUE_COUNT];

	/* New Comm. interface */
	uint32_t aac_max_fibs;		/* max. FIB count */
	uint32_t aac_max_fib_size;	/* max. FIB size */
	uint32_t aac_sg_tablesize;	/* max. sg count from host */
	uint32_t aac_max_sectors;	/* max. I/O size from host (blocks) */

	aac_cmd_fib_t aac_cmd_fib;	/* IO cmd FIB construct function */
	aac_cmd_fib_t aac_cmd_fib_scsi;	/* SRB construct function */

	ddi_softintr_t softint_id;	/* soft intr */

	kmutex_t io_lock;
	int state;			/* driver state */

	struct aac_container containers[AAC_MAX_LD];
	int container_count;		/* max container id + 1 */
	struct aac_nondasd *nondasds;
	uint32_t bus_max;		/* max FW buses exposed */
	uint32_t tgt_max;		/* max FW target per bus */

	/*
	 * Command queues
	 * Each aac command flows through wait(or wait_sync) queue,
	 * busy queue, and complete queue sequentially.
	 */
	struct aac_cmd_queue q_wait[AAC_CMDQ_NUM];
	struct aac_cmd_queue q_busy;	/* outstanding cmd queue */
	kmutex_t q_comp_mutex;
	struct aac_cmd_queue q_comp;	/* completed io requests */

	/* I/O slots and FIBs */
	int total_slots;		/* total slots allocated */
	int total_fibs;			/* total FIBs allocated */
	struct aac_slot *io_slot;	/* static list for allocated slots */
	struct aac_slot *free_io_slot_head;

	kcondvar_t event;		/* for ioctl_send_fib() and sync IO */
	kcondvar_t sync_fib_cv;		/* for sync_fib_slot_bind/release */

	int bus_ncmds[AAC_CMDQ_NUM];	/* total outstanding async cmds */
	int bus_throttle[AAC_CMDQ_NUM];	/* hold IO cmds for the bus */
	int ndrains;			/* number of draining threads */
	timeout_id_t drain_timeid;	/* for outstanding cmd drain */
	kcondvar_t drain_cv;		/* for quiesce drain */

	/* Internal timer */
	kmutex_t time_mutex;
	timeout_id_t timeout_id;	/* for timeout daemon */
	uint32_t timebase;		/* internal timer in seconds */
	uint32_t time_sync;		/* next time to sync with firmware */
	uint32_t time_out;		/* next time to check timeout */
	uint32_t time_throttle;		/* next time to restore throttle */

	/* Internal events handling */
	kmutex_t ev_lock;
	int events;
	kthread_t *event_thread;	/* for AIF & timeout */
	kcondvar_t event_wait_cv;
	kcondvar_t event_disp_cv;

	/* AIF */
	kmutex_t aifq_mutex;		/* for AIF queue aifq */
	kcondvar_t aifq_cv;
	union aac_fib_align aifq[AAC_AIFQ_LENGTH];
	int aifq_idx;			/* slot for next new AIF */
	int aifq_wrap;			/* AIF queue has ever been wrapped */
	struct aac_fib_context aifctx;	/* sys aif ctx */
	struct aac_fib_context *fibctx_p;
	int devcfg_wait_on;		/* AIF event waited for rescan */

	int fm_capabilities;

	/* MSI specific fields */
	ddi_intr_handle_t *htable;	/* For array of interrupts */
	int intr_type;			/* What type of interrupt */
	int intr_cnt;			/* # of intrs count returned */
	int intr_size;
	uint_t intr_pri;		/* Interrupt priority   */
	int intr_cap;			/* Interrupt capabilities */

#ifdef DEBUG
	/* UART trace printf variables */
	uint32_t debug_flags;		/* debug print flags bitmap */
	uint32_t debug_fib_flags;	/* debug FIB print flags bitmap */
	uint32_t debug_fw_flags;	/* FW debug flags */
	uint32_t debug_buf_offset;	/* offset from DPMEM start */
	uint32_t debug_buf_size;	/* FW debug buffer size in bytes */
	uint32_t debug_header_size;	/* size of debug header */
#endif
};

/*
 * The following data are kept stable because they are only written at driver
 * initialization, and we do not allow them changed otherwise even at driver
 * re-initialization.
 */
_NOTE(SCHEME_PROTECTS_DATA("stable data", aac_softstate::{flags slen \
    buf_dma_attr pci_mem_handle pci_mem_base_vaddr \
    comm_space_acc_handle comm_space_dma_handle aac_max_fib_size \
    aac_sg_tablesize aac_cmd_fib aac_cmd_fib_scsi debug_flags bus_max tgt_max}))

#ifdef DEBUG

#define	AACDB_FLAGS_MASK		0x0000ffff
#define	AACDB_FLAGS_KERNEL_PRINT	0x00000001
#define	AACDB_FLAGS_FW_PRINT		0x00000002
#define	AACDB_FLAGS_NO_HEADERS		0x00000004

#define	AACDB_FLAGS_MISC		0x00000010
#define	AACDB_FLAGS_FUNC1		0x00000020
#define	AACDB_FLAGS_FUNC2		0x00000040
#define	AACDB_FLAGS_SCMD		0x00000080
#define	AACDB_FLAGS_AIF			0x00000100
#define	AACDB_FLAGS_FIB			0x00000200
#define	AACDB_FLAGS_IOCTL		0x00000400

/*
 * Flags for FIB print
 */
/* FIB sources */
#define	AACDB_FLAGS_FIB_SCMD		0x00000001
#define	AACDB_FLAGS_FIB_IOCTL		0x00000002
#define	AACDB_FLAGS_FIB_SRB		0x00000004
#define	AACDB_FLAGS_FIB_SYNC		0x00000008
/* FIB components */
#define	AACDB_FLAGS_FIB_HEADER		0x00000010
/* FIB states */
#define	AACDB_FLAGS_FIB_TIMEOUT		0x00000100

extern uint32_t aac_debug_flags;
extern int aac_dbflag_on(struct aac_softstate *, int);
extern void aac_printf(struct aac_softstate *, uint_t, const char *, ...);
extern void aac_print_fib(struct aac_softstate *, struct aac_slot *);

#define	AACDB_PRINT(s, lev, ...) { \
	if (aac_dbflag_on((s), AACDB_FLAGS_MISC)) \
		aac_printf((s), (lev), __VA_ARGS__); }

#define	AACDB_PRINT_IOCTL(s, ...) { \
	if (aac_dbflag_on((s), AACDB_FLAGS_IOCTL)) \
		aac_printf((s), CE_NOTE, __VA_ARGS__); }

#define	AACDB_PRINT_TRAN(s, ...) { \
	if (aac_dbflag_on((s), AACDB_FLAGS_SCMD)) \
		aac_printf((s), CE_NOTE, __VA_ARGS__); }

#define	DBCALLED(s, n) { \
	if (aac_dbflag_on((s), AACDB_FLAGS_FUNC ## n)) \
		aac_printf((s), CE_NOTE, "--- %s() called ---", __func__); }

#define	AACDB_PRINT_SCMD(s, x) { \
	if (aac_dbflag_on((s), AACDB_FLAGS_SCMD)) aac_print_scmd((s), (x)); }

#define	AACDB_PRINT_AIF(s, x) { \
	if (aac_dbflag_on((s), AACDB_FLAGS_AIF)) aac_print_aif((s), (x)); }

#define	AACDB_PRINT_FIB(s, x) { \
	if (aac_dbflag_on((s), AACDB_FLAGS_FIB)) aac_print_fib((s), (x)); }

#else /* DEBUG */

#define	AACDB_PRINT(s, lev, ...)
#define	AACDB_PRINT_IOCTL(s, ...)
#define	AACDB_PRINT_TRAN(s, ...)
#define	AACDB_PRINT_FIB(s, x)
#define	AACDB_PRINT_SCMD(s, x)
#define	AACDB_PRINT_AIF(s, x)
#define	DBCALLED(s, n)

#endif /* DEBUG */

#ifdef	__cplusplus
}
#endif

#endif /* _AAC_H_ */

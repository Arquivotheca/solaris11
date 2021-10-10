/*
 * Copyright (c) 1994, 2007, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_SCSI_ADAPTERS_GLMVAR_H
#define	_SYS_SCSI_ADAPTERS_GLMVAR_H

#include <sys/isa_defs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Compile options
 */
#ifdef DEBUG
#define	GLM_DEBUG		/* turn on debugging code */
#endif	/* DEBUG */

/* Delay for reading SCSI0/SCSI1 is 12 PCI CLK. Each CLK is 30 nanosec */
#define	GLM_INTERRUPT_DELAY 360

#define	NLUNS_GLM		32
#define	N_GLM_UNITS		(NTARGETS_WIDE * NLUNS_GLM)
#define	ALL_TARGETS		0xffff

#define	GLM_INITIAL_SOFT_SPACE	4	/* Used	for the	softstate_init func */

/*
 * Wide support.
 */
#define	GLM_XFER_WIDTH	1

/*
 * If your HBA supports DMA or bus-mastering, you may have your own
 * scatter-gather list for physically non-contiguous memory in one
 * I/O operation; if so, there's probably a size for that list.
 * It must be placed in the ddi_dma_lim_t structure, so that the system
 * DMA-support routines can use it to break up the I/O request, so we
 * define it here.
 */
#if defined(__sparc)
#define	GLM_MAX_DMA_SEGS	1
#else
#define	GLM_MAX_DMA_SEGS	17
#endif

/*
 * Scatter-gather list structure defined by HBA hardware
 */
typedef	struct NcrTableIndirect {	/* Table Indirect entries */
	uint32_t count;		/* 24 bit count */
	uint32_t address;	/* 32 bit address */
} glmti_t;

/*
 * preferred pkt_private length in 64-bit quantities
 */
#ifdef	_LP64
#define	PKT_PRIV_SIZE	2
#define	PKT_PRIV_LEN	16	/* in bytes */
#else /* _ILP32 */
#define	PKT_PRIV_SIZE	1
#define	PKT_PRIV_LEN	8	/* in bytes */
#endif

#define	PKT2CMD(pktp)	((struct glm_scsi_cmd *)((pktp)->pkt_ha_private))
#define	CMD2PKT(cmdp)	((struct scsi_pkt *)((cmdp)->cmd_pkt))
#define	EXTCMDS_STATUS_SIZE (sizeof (struct scsi_arq_status))


typedef struct	glm_scsi_cmd {
	uint_t			cmd_flags;	/* flags from scsi_init_pkt */
	ddi_dma_handle_t	cmd_dmahandle;	/* dma handle */
	ddi_dma_cookie_t	cmd_cookie;
	uint_t			cmd_cookiec;
	uint_t			cmd_winindex;
	uint_t			cmd_nwin;
	uint_t			cmd_scc;	/* Script cookie count */

	uint_t			cmd_cwin;	/* Current Window */
	uint_t			cmd_woff;	/* Current Window Offset */
	size_t			cmd_dma_length;
	off_t			cmd_dma_offset;

	uint32_t		cmd_saved_addr;
	uint32_t		cmd_saved_count;

	uint32_t		cmd_pmm_addr;
	uint32_t		cmd_pmm_count;

	uchar_t			cmd_saved_cookie;
	int			cmd_pkt_flags;

	struct scsi_pkt		*cmd_pkt;
	struct scsi_arq_status	cmd_scb;
	uchar_t			cmd_cdblen;	/* length of cdb */
	uchar_t			cmd_rqslen;	/* len of requested rqsense */
	uchar_t			cmd_age;
	uchar_t			cmd_privlen;
	uint_t			cmd_scblen;
	uint_t			cmd_dmacount;
	ushort_t		cmd_qfull_retries;
	uchar_t			cmd_queued;	/* true if queued */
	uchar_t			cmd_type;
	struct glm_scsi_cmd	*cmd_linkp;
	glmti_t			cmd_sg[GLM_MAX_DMA_SEGS]; /* S/G structure */
	uchar_t			cmd_cdb[CDB_SIZE];
	uint64_t		cmd_pkt_private[PKT_PRIV_LEN];
	uchar_t			cmd_tag[2];	/* command tag */
} ncmd_t;

/*
 * private data for arq pkt
 */
struct arq_private_data {
	struct buf		*arq_save_bp;
	struct glm_scsi_cmd	*arq_save_sp;
};

/*
 * These are the defined cmd_flags for this structure.
 */
#define	CFLAG_CMDDISC		0x000001 /* cmd currently disconnected */
#define	CFLAG_WATCH		0x000002 /* watchdog time for this command */
#define	CFLAG_FINISHED		0x000004 /* command completed */
#define	CFLAG_CHKSEG		0x000008 /* check cmd_data within seg */
#define	CFLAG_COMPLETED		0x000010 /* completion routine called */
#define	CFLAG_PREPARED		0x000020 /* pkt has been init'ed */
#define	CFLAG_IN_TRANSPORT	0x000040 /* in use by host adapter driver */
#define	CFLAG_RESTORE_PTRS	0x000080 /* implicit restore ptr on reconnect */
#define	CFLAG_ARQ_IN_PROGRESS	0x000100 /* auto request sense in progress */
#define	CFLAG_TRANFLAG		0x0001ff /* covers transport part of flags */
#define	CFLAG_CMDPROXY		0x000200 /* cmd is a 'proxy' command */
#define	CFLAG_CMDARQ		0x000400 /* cmd is a 'rqsense' command */
#define	CFLAG_DMAVALID		0x000800 /* dma mapping valid */
#define	CFLAG_DMASEND		0x001000 /* data	is going 'out' */
#define	CFLAG_CMDIOPB		0x002000 /* this	is an 'iopb' packet */
#define	CFLAG_CDBEXTERN		0x004000 /* cdb kmem_alloc'd */
#define	CFLAG_SCBEXTERN		0x008000 /* scb kmem_alloc'd */
#define	CFLAG_FREE		0x010000 /* packet is on free list */
#define	CFLAG_PRIVEXTERN	0x020000 /* target private kmem_alloc'd */
#define	CFLAG_DMA_PARTIAL	0x040000 /* partial xfer OK */
#define	CFLAG_QFULL_STATUS	0x080000 /* pkt got qfull status */
#define	CFLAG_CMD_REMOVED	0x100000 /* cmd has been remove */
#define	CFLAG_PMM_RECEIVED	0x200000 /* use cmd_pmm*  for saving pointers */

/*
 * Information maintained about the hba's DSA.
 */
struct glm_hba_dsa {
	struct	{
		uchar_t	g_scntl4;	/* Only 1010 init to 0 on others */
		uchar_t	g_sxfer;	/* SCSI transfer/period parms */
		uchar_t	g_sdid;		/* SCSI destination ID for SELECT */
		uchar_t	g_scntl3;	/* FAST-20 and Wide bits. */
	} g_reselectparm[16];

	glmti_t	g_rcvmsg;		/* pointer to msginbuf */
	glmti_t	g_moremsgin;		/* pointer to moremsginbuf */
	glmti_t	g_tagmsg;		/* pointer to taginbuf */
	glmti_t	g_errmsg;		/* pointer to message error buffer */

	uchar_t	g_msginbuf[1];		/* first byte of message in */
	uchar_t	g_moremsginbuf[1];	/* more msg bytes */
	uchar_t	g_taginbuf[1];		/* tag byte */
	uchar_t	g_errmsgbuf[1];		/* error message for target */
};

/*
 * Information maintained about each (target,lun) the HBA serves.
 *
 * DSA reg points here when this target is active
 * Table Indirect pointers for GLM SCRIPTS
 */
struct glm_dsa {
	struct	{
		uchar_t	nt_scntl4;	/* Only 1010 init to 0 on others */
		uchar_t	nt_sxfer;	/* SCSI transfer/period parms */
		uchar_t	nt_sdid;	/* SCSI destination ID for SELECT */
		uchar_t	nt_scntl3;	/* FAST-20 and Wide bits. */
	} nt_selectparm;
	glmti_t	nt_sendmsg;		/* pointer to sendmsg buffer */
	glmti_t	nt_rcvmsg;		/* pointer to msginbuf */
	glmti_t	nt_cmd;			/* pointer to cdb buffer */
	glmti_t	nt_status;		/* pointer to status buffer */
	glmti_t	nt_extmsg;		/* pointer to extended message buffer */
	glmti_t	nt_syncin;		/* pointer to sync in buffer */
	glmti_t	nt_errmsg;		/* pointer to message reject buffer */
	glmti_t nt_widein;		/* pointer to wide in buffer */
	glmti_t	nt_pprin;		/* pointer to ppr in buffer */
	glmti_t nt_data[GLM_MAX_DMA_SEGS];	/* current S/G data pointers */

	/* these are the buffers the HBA actually does i/o to/from */
	uchar_t nt_cdb[12];		/* scsi command description block */

	/* keep these two together so HBA can transmit in single move */
	uchar_t	nt_msgoutbuf[12];
	uchar_t	nt_msginbuf[1];		/* first byte of message in */
	uchar_t	nt_extmsgbuf[1];	/* length of extended message */
	uchar_t	nt_syncibuf[3];		/* room for sdtr inbound message */
	uchar_t	nt_statbuf[1];		/* status byte */
	uchar_t	nt_errmsgbuf[1];	/* error message for target */
	uchar_t	nt_wideibuf[2];		/* room for wdtr inbound msg. */
	uchar_t	nt_ppribuf[6];		/* room for ppr inbound msg */
};

typedef struct glm_unit {
	int		nt_refcnt;	/* reference count */
	struct glm_dsa	*nt_dsap;
	ncmd_t		*nt_ncmdp;	/* cmd for active request */
	ncmd_t		*nt_waitq;	/* wait queue link pointer */
	ncmd_t		**nt_waitqtail;	/* wait queue tail ptr */
	struct glm_unit *nt_linkp;	/* wait queue link pointer */
	uint32_t	nt_dsa_addr;	/* addr of table indirects */

	struct nt_slots	*nt_active;	/* outstanding cmds */
	short		nt_tcmds;	/* number of commands stored */

	ddi_dma_handle_t nt_dma_p;	/* per-target DMA handle */
	ddi_acc_handle_t nt_accessp;	/* handle for common access fns. */

	uchar_t		nt_state;	/* current state of this target */

	uchar_t		nt_goterror;	/* true if error occurred */
	uchar_t		nt_dma_status;	/* copy of DMA error bits */
	uchar_t		nt_status0;	/* copy of SCSI bus error bits */
	uchar_t		nt_status1;	/* copy of SCSI bus error bits */

	ushort_t	nt_target;	/* target number */
	ushort_t	nt_lun;		/* logical unit number */
	uchar_t		nt_fastscsi;	/* true if > 5MB/sec, tp < 200ns */
	uchar_t		nt_sscfX10;	/* sync i/o clock divisor */

	short		nt_throttle;	/* throttle for this targ/lun. */

	struct glm_scsi_cmd	*nt_arq_pkt; /* arq pkt. */
	uint32_t	nt_msgoutcount;	/* 24 bit count */
	uint32_t	nt_msgouttype;	/* WIDE or SYNC */
} glm_unit_t;

/*
 * This is the divisor table (x10) used for the various glm chips.
 */
#define	GLM_MAX_DIVISORS	7
uchar_t glm_ccf[GLM_MAX_DIVISORS] = { 10, 15, 20, 30, 40, 60, 80 };

/*
 * The states a HBA to (target, lun) nexus can have are:
 */
#define	NPT_STATE_ACTIVE	0x1	/* target is active */
#define	NPT_STATE_QUEUED	0x2	/* target request is queued */

/*
 * types of requests passed to glm_send_cmd(), stored in nt_type
 */
#define	NRQ_NORMAL_CMD		((uchar_t)0)	/* normal command */
#define	NRQ_ABORT		((uchar_t)1)	/* Abort message */
#define	NRQ_ABORT_TAG		((uchar_t)2)	/* Abort Tag message */
#define	NRQ_DEV_RESET		((uchar_t)3)	/* Bus Device Reset message */

/*
 * macro to get offset of unit dsa members for compiling into the SCRIPT
 */
#define	NTOFFSET(label) ((uint32_t)(long)&(((struct glm_dsa *)0)->label))

/*
 * Macro to get offset of the hba dsa members for compiling into SCRIPTS.
 */
#define	HBAOFFSET(label) ((uint32_t)(long)&(((struct glm_hba_dsa *)0)->label))

/*
 * Structure to hold active outstanding cmds
 */
struct nt_slots {
	ushort_t		nt_dups;
	ushort_t		nt_tags;
	int			nt_timeout;
	int			nt_timebase;
				/* nt_slot size is 1 for non-tagged, and */
				/* 256 for tagged targets		*/
	ushort_t		nt_n_slots; /* number of slots */
	ushort_t		nt_size;
	struct glm_scsi_cmd	*nt_slot[1];
};

#define	GLM_NT_SLOTS_SIZE_TQ	(sizeof (struct nt_slots) + \
			(sizeof (struct glm_scsi_cmd *) * (NTAGS -1)))
#define	GLM_NT_SLOT_SIZE	(sizeof (struct nt_slots))

typedef struct glm {
	int		g_instance;

	struct glm *g_next;

	scsi_hba_tran_t		*g_tran;
	kmutex_t		g_mutex;
	kcondvar_t		g_cv;
	ddi_iblock_cookie_t	g_iblock;
	dev_info_t		*g_dip;

	/*
	 * soft state flags
	 */
	uint_t		g_softstate;

	glm_unit_t	*g_units[N_GLM_UNITS];
					/* ptrs to per-target data */

	glm_unit_t	*g_current;	/* ptr to active target's DSA */
	glm_unit_t	*g_forwp;	/* ptr to front of the wait queue */
	glm_unit_t	*g_backp;	/* ptr to back of the wait queue */
	ncmd_t		*g_doneq;	/* queue of completed commands */
	ncmd_t		**g_donetail;	/* queue tail ptr */

	ddi_acc_handle_t g_datap;	/* operating regs data access handle */
	caddr_t		g_devaddr;	/* ptr to io/mem mapped-in regs */

	ushort_t	g_devid;	/* device id of chip. */
	uchar_t		g_revid;	/* revision of chip. */

	uchar_t		g_sync_offset;	/* default offset for this chip. */
	uchar_t		g_dt_offset;	/* Default DT offset for this chip */

	timeout_id_t	g_quiesce_timeid;

	/*
	 * Used for memory or onboard scripts.
	 */
	caddr_t		g_scripts_ram;
	uint32_t	g_ram_base_addr;
	ddi_acc_handle_t g_script_acc_handle;
	ddi_dma_handle_t g_script_dma_handle;
#define	NSS_FUNCS	10	/* number of defined SCRIPT functions */
	uint_t		g_glm_scripts[NSS_FUNCS];
	uint_t		g_do_list_end;
	uint_t		g_di_list_end;
	uint_t		g_dt_do_list_end;
	uint_t		g_dt_di_list_end;

	/*
	 * Used for hba DSA.
	 */
	ddi_dma_handle_t g_dsa_dma_h;	/* per-hba DMA handle */
	ddi_acc_handle_t g_dsa_acc_h;	/* handle for common access fns. */
	struct glm_hba_dsa *g_dsa;
	uint32_t	g_dsa_addr;	/* addr of table indirects */

	/*
	 * scsi_options for bus and per target
	 */
	int		g_target_scsi_options_defined;
	int		g_scsi_options;
	int		g_target_scsi_options[NTARGETS_WIDE];

	/*
	 * ppr_enabled == 1 for Ultra Scsi 3 controllers
	 * ppr_supported == 1 if the target support's ppr message
	 * ppr_known == 1 if ppr is successfull with the device
	 * ppr_sent == 1 if currently ppr is pending with target
	 */
	ushort_t	g_ppr_enabled;	/* use of ppr message enabled */
	ushort_t	g_ppr_supported;	/* ppr supported by device */
	ushort_t	g_ppr_known;	/* ppr result is known */
	ushort_t	g_ppr_sent;	/* sent a ppr message to target */

	/*
	 * These u_shorts are bit maps for targets
	 */
	ushort_t	g_wide_known;	/* wide negotiate on next cmd */
	ushort_t	g_nowide;	/* no wide for this target */
	ushort_t	g_wide_enabled;	/* wide enabled for this target */
	uchar_t		g_wdtr_sent;

	/*
	 * sync/wide backoff bit mask
	 */
	ushort_t	g_backoff;

	/*
	 * This u_short is a bit map for targets who need to have
	 * their properties update deferred.
	 */
	ushort_t	g_props_update;

	/*
	 * This u_short is a bit map for targets who don't appear
	 * to be able to support tagged commands.
	 */
	ushort_t	g_notag;

	/*
	 * tag age limit per bus
	 */
	int		g_scsi_tag_age_limit;

	/*
	 * list of reset notification requests
	 */
	struct scsi_reset_notify_entry	*g_reset_notify_listf;

	/*
	 * qfull handling
	 */
	uchar_t		g_qfull_retries[NTARGETS_WIDE];
	ushort_t	g_qfull_retry_interval[NTARGETS_WIDE];
	timeout_id_t	g_restart_cmd_timeid;

	/*
	 * scsi	reset delay per	bus
	 */
	uint_t		g_scsi_reset_delay;
	int		g_reset_delay[NTARGETS_WIDE];

	int		g_sclock;	/* hba's SCLK freq. in MHz */
	int		g_speriod;	/* hba's SCLK period, in nsec. */

	/*
	 * hba's sync period.
	 */
	uint_t		g_hba_period;

	/* sync i/o state flags */
	uchar_t		g_syncstate[NTARGETS_WIDE];
	/* min sync i/o period per tgt */
	int		g_minperiod[NTARGETS_WIDE];
	uchar_t		g_scntl3;	/* 53c8xx hba's core clock divisor */

	uchar_t		g_glmid;	/* this hba's target number and ... */
	short		g_ndisc;	/* number of diconnected cmds */

	uchar_t		g_state;	/* the HBA's current state */
	uchar_t		g_polled_intr;	/* intr was polled. */
	uchar_t		g_suspended;	/* true	if driver is suspended */

	struct kmem_cache *g_kmem_cache;

	/*
	 * hba options.
	 */
	uint_t		g_options;

	/*
	 * bit mask for arq enable
	 */
	uint32_t	g_arq_mask[NTARGETS_WIDE];

	int		g_in_callback;

	uchar_t		g_max_lun[NTARGETS_WIDE];

	int		g_power_level;	/* current power level */

	ddi_acc_handle_t g_config_handle;

	uchar_t		g_max_div;	/* max clock divisors */
	ddi_dma_attr_t  g_hba_dma_attrs;
	uint32_t	g_errmsg_retries;
	uint32_t	g_reset_retries;
	uint32_t	g_reset_recursion;
	uint32_t	g_reset_received;

	ushort_t	g_force_async;
	ushort_t	g_force_narrow;
} glm_t;

_NOTE(MUTEX_PROTECTS_DATA(glm::g_mutex, glm))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", glm::g_next))
_NOTE(SCHEME_PROTECTS_DATA("stable data", glm::g_target_scsi_options))
_NOTE(SCHEME_PROTECTS_DATA("stable data", glm::g_dip glm::g_tran))
_NOTE(SCHEME_PROTECTS_DATA("stable data", glm::g_kmem_cache))

/*
 * HBA state.
 */
#define	NSTATE_IDLE		((uchar_t)0) /* HBA is idle */
#define	NSTATE_ACTIVE		((uchar_t)1) /* HBA is processing a target */
#define	NSTATE_WAIT_RESEL	((uchar_t)2) /* HBA is waiting for reselect */

/*
 * states of the hba while negotiating synchronous i/o with a target
 */
#define	NSYNCSTATE(glmp, nptp)	(glmp)->g_syncstate[(nptp)->nt_target]
#define	NSYNC_SDTR_NOTDONE	((uchar_t)0) /* SDTR negotiation needed */
#define	NSYNC_SDTR_SENT		((uchar_t)1) /* waiting for target SDTR msg */
#define	NSYNC_SDTR_RCVD		((uchar_t)2) /* target waiting for SDTR msg */
#define	NSYNC_SDTR_ACK		((uchar_t)3) /* ack target's SDTR message */
#define	NSYNC_SDTR_REJECT	((uchar_t)4) /* send Message Reject to target */
#define	NSYNC_SDTR_DONE		((uchar_t)5) /* final state */

/*
 * action codes for interrupt decode routines in interrupt.c
 */
#define	NACTION_DONE		0x01	/* target request is done */
#define	NACTION_ERR		0x02	/* target's request got error */
#define	NACTION_GOT_BUS_RESET	0x04	/* reset the SCSI bus */
#define	NACTION_SAVE_BCNT	0x08	/* save scatter/gather byte ptr */
#define	NACTION_SIOP_HALT	0x10	/* halt the current HBA program */
#define	NACTION_SIOP_REINIT	0x20	/* totally reinitialize the HBA */
#define	NACTION_SDTR		0x40	/* got SDTR interrupt */
#define	NACTION_EXT_MSG_OUT	0x80	/* send Extended message */
#define	NACTION_ACK		0x100	/* ack the last byte and continue */
#define	NACTION_CHK_INTCODE	0x200	/* SCRIPTS software interrupt */
#define	NACTION_INITIATOR_ERROR	0x400	/* send IDE message and then continue */
#define	NACTION_MSG_PARITY	0x800	/* send MPE error and then continue */
#define	NACTION_MSG_REJECT	0x1000	/* send MR and then continue */
#define	NACTION_BUS_FREE	0x2000	/* reselect error caused disconnect */
#define	NACTION_ABORT		0x4000	/* abort */
#define	NACTION_DO_BUS_RESET	0x8000	/* reset the SCSI bus */
#define	NACTION_CLEAR_CHIP	0x10000	/* clear chip's fifo's */
#define	NACTION_ARQ		0x20000	/* running an arq. */
#define	NACTION_EXT_MSG_IN	0x40000	/* receive Extended message */

/*
 * g_options flags
 */
#define	GLM_OPT_WIDE_BUS	0x01
#define	GLM_OPT_ONBOARD_RAM	0x02
#define	GLM_OPT_PM		0x04	/* Power Management */
#define	GLM_OPT_ULTRA		0x08
#define	GLM_OPT_LVD		0x10
#define	GLM_OPT_LARGE_FIFO	0x20
#define	GLM_OPT_DT		0x40

/*
 * g_softstate flags
 */
#define	GLM_SS_DRAINING 	0x02
#define	GLM_SS_QUIESCED 	0x04

/*
 * regspec defines.
 */
#define	CONFIG_SPACE	0	/* regset[0] - configuration space */
#define	IO_SPACE	1	/* regset[1] - used for i/o mapped device */
#define	MEM_SPACE	2	/* regset[2] - used for memory mapped device */
#define	BASE_REG2	3	/* regset[3] - used for 875 scripts ram */

/*
 * Handy constants
 */
#define	FALSE		0
#define	TRUE		1
#define	UNDEFINED	-1
#define	MSG_GLM_LUNRTN	0x1f
#define	MEG		(1000 * 1000)

/*
 * power management.
 */
#define	GLM_PM_PMC	0x42
#define	GLM_PM_CSR	0x44

/* PMC Contant definition */
#define	D1SUPPORT	512
#define	D2SUPPORT	1024

#define	GLM_POWER_ON(glm) { \
	pci_config_put16(glm->g_config_handle, GLM_PM_CSR, \
	    PCI_PMCSR_D0); \
	delay(drv_usectohz(10000)); \
	glm->g_power_level = PM_LEVEL_D0; \
	(void) pci_restore_config_regs(glm->g_dip); \
	glm_setup_cmd_reg(glm); \
	GLM_RESET(glm); \
	GLM_INIT(glm); \
}

#define	GLM_POWER_FROM_COMA(glm) { \
	pci_config_put16(glm->g_config_handle, GLM_PM_CSR, \
	    PCI_PMCSR_D0); \
	drv_usecwait(100); \
	glm->g_power_level = PM_LEVEL_D0; \
	(void) pci_restore_config_regs(glm->g_dip); \
}

#define	GLM_POWER_COMA(glm) { \
	(void) pci_save_config_regs(glm->g_dip); \
	pci_config_put16(glm->g_config_handle, GLM_PM_CSR, \
	    PCI_PMCSR_D2); \
	glm->g_power_level = PM_LEVEL_D2; \
}

#define	GLM_POWER_OFF(glm) { \
	(void) pci_save_config_regs(glm->g_dip); \
	pci_config_put16(glm->g_config_handle, GLM_PM_CSR, \
	    PCI_PMCSR_D3HOT); \
	glm->g_power_level = PM_LEVEL_D3; \
}

/*
 * Default is to have 10 retries on receiving QFULL status and
 * each retry to be after 100 ms.
 */
#define	QFULL_RETRIES		10
#define	QFULL_RETRY_INTERVAL	100

/*
 * Handy macros
 */
#define	Tgt(sp)	((sp)->cmd_pkt->pkt_address.a_target)
#define	Lun(sp)	((sp)->cmd_pkt->pkt_address.a_lun)

/*
 * poll time for glm_pollret() and glm_wait_intr()
 */
#define	GLM_POLL_TIME	600000	/* 60 seconds */

/*
 * bus reset poll time (no need to wait for full 60 seconds)
 */
#define	GLM_BUS_RESET_POLL_TIME	2500	/* 0.25 seconds */

/*
 * miscellaneous retry limits
 */
#define	GLM_INTRLOOP_COUNT 100
#define	GLM_INTRLOOP_TIME 50000
#define	GLM_ERRMSG_RETRY_COUNT 50
#define	GLM_RESET_RETRY_COUNT 5

/*
 * macro to return the effective address of a given per-target field
 */
#define	EFF_ADDR(start, offset)		((start) + (offset))

#define	SDEV2ADDR(devp)		(&((devp)->sd_address))
#define	SDEV2TRAN(devp)		((devp)->sd_address.a_hba_tran)
#define	PKT2TRAN(pktp)		((pktp)->pkt_address.a_hba_tran)
#define	ADDR2TRAN(ap)		((ap)->a_hba_tran)

#define	TRAN2GLM(hba)		((glm_t *)(hba)->tran_hba_private)
#define	SDEV2GLM(sd)		(TRAN2GLM(SDEV2TRAN(sd)))
#define	PKT2GLM(pktp)		(TRAN2GLM(PKT2TRAN(pktp)))
#define	PKT2GLMUNITP(pktp)	NTL2UNITP(PKT2GLM(pktp), \
					(pktp)->pkt_address.a_target, \
					(pktp)->pkt_address.a_lun)
#define	ADDR2GLM(ap)		(TRAN2GLM(ADDR2TRAN(ap)))
#define	ADDR2GLMUNITP(ap)	NTL2UNITP(ADDR2GLM(ap), \
					(ap)->a_target, (ap)->a_lun)

#define	NTL2UNITP(glm_blkp, targ, lun)	\
				((glm_blkp)->g_units[TL2INDEX(targ, lun)])

#define	POLL_TIMEOUT		(2 * SCSI_POLL_TIMEOUT * 1000000)
#define	SHORT_POLL_TIMEOUT	(1000000)	/* in usec, about 1 secs */
#define	GLM_QUIESCE_TIMEOUT	1		/* 1 sec */

/*
 * Map (target, lun) to g_units array index
 */
#define	TL2INDEX(target, lun)	((target) * NLUNS_GLM + (lun))


#define	GLM_RESET(P)			glm53c87x_reset(P)
#define	GLM_INIT(P)			glm53c87x_init(P)
#define	GLM_ENABLE_INTR(P)		glm53c87x_enable(P)
#define	GLM_DISABLE_INTR(P)		glm53c87x_disable(P)

#define	GLM_GET_ISTAT(P)  (ddi_get8(glm->g_datap, \
			(uint8_t *)(glm->g_devaddr + NREG_ISTAT)))

#define	GLM_HALT(P)			glm53c87x_halt(P)

#define	GLM_SET_SIGP(P) \
		ClrSetBits(glm->g_devaddr + NREG_ISTAT, 0, NB_ISTAT_SIGP)

#define	GLM_RESET_SIGP(P) (void) ddi_get8(glm->g_datap, \
			(uint8_t *)(glm->g_devaddr + NREG_CTEST2))

#define	GLM_GET_INTCODE(P) (ddi_get32(glm->g_datap, \
			(uint32_t *)(glm->g_devaddr + NREG_DSPS)))

#define	GLM_CHECK_ERROR(P, nptp, pktp)	glm53c87x_check_error(nptp, \
						pktp)

#define	GLM_DMA_STATUS(P)		glm53c87x_dma_status(P)
#define	GLM_SCSI_STATUS(P)		glm53c87x_scsi_status(P)
#define	GLM_SAVE_BYTE_COUNT(P, nptp)	glm53c87x_save_byte_count(P, \
						nptp)

#define	GLM_GET_TARGET(P, tp)		glm53c87x_get_target(P, tp)

#define	GLM_SETUP_SCRIPT(P, nptp) \
				glm53c87x_setup_script(P, nptp)

#define	GLM_START_SCRIPT(P, script) \
	(void) ddi_dma_sync(glm->g_dsa_dma_h, 0, 0, DDI_DMA_SYNC_FORDEV); \
	ddi_put32(glm->g_datap, \
	(uint32_t *)(glm->g_devaddr + NREG_DSP), glm->g_glm_scripts[script])

#define	GLM_SET_SYNCIO(P, nptp)		glm53c87x_set_syncio(P, nptp)

#define	GLM_BUS_RESET(P)		glm53c87x_bus_reset(P)

#define	INTPENDING(glm) \
	(GLM_GET_ISTAT(glm) & (NB_ISTAT_DIP | NB_ISTAT_SIP))


#define	ClrSetBits(reg, clr, set) \
	ddi_put8(glm->g_datap, (uint8_t *)(reg), \
		((ddi_get8(glm->g_datap, (uint8_t *)(reg)) & ~(clr)) | (set)))

/*
 * Clear the DMA FIFO pointers
 */
#define	CLEAR_DMA(glm) \
	ddi_put8((glm)->g_datap, (uint8_t *)((glm)->g_devaddr + NREG_CTEST3), \
	    ddi_get8((glm)->g_datap, \
	    (uint8_t *)((glm)->g_devaddr + NREG_CTEST3)) | NB_CTEST3_CLF)

/*
 * Clear the SCSI FIFO pointers
 */
#define	CLEAR_SCSI_FIFO(glm) \
	ddi_put8((glm)->g_datap, (uint8_t *)((glm)->g_devaddr + NREG_STEST3), \
	    ddi_get8((glm)->g_datap, \
	    (uint8_t *)((glm)->g_devaddr + NREG_STEST3)) | NB_STEST3_CSF)

/*
 * Reset SCSI Offset
 */
#define	RESET_SCSI_OFFSET(glm) \
	ddi_put8((glm)->g_datap, (uint8_t *)((glm)->g_devaddr + NREG_STEST2), \
	    ddi_get8((glm)->g_datap, \
	    (uint8_t *)((glm)->g_devaddr + NREG_STEST2)) | NB_STEST2_ROF)

/*
 * useful macros for the critical path.
 */
#define	GLM_RMQ(glm, unit)	\
	if ((unit = glm->g_forwp) != NULL) { \
		if ((glm->g_forwp = unit->nt_linkp) == NULL) { \
			glm->g_backp = NULL; \
		} \
		unit->nt_linkp = NULL; \
		unit->nt_state &= ~NPT_STATE_QUEUED; \
	}

#define	GLM_WAITQ_RM(unit, cmdp)	\
	if ((cmdp = unit->nt_waitq) != NULL) { \
		/* If the queue is now empty fix the tail pointer */	\
		if ((unit->nt_waitq = cmdp->cmd_linkp) == NULL) \
			unit->nt_waitqtail = &unit->nt_waitq; \
		cmdp->cmd_linkp = NULL; \
		cmdp->cmd_queued = FALSE; \
	}

/*
 * All models of the NCR are assumed to have consistent definitions
 * of the following bits in the ISTAT register. The ISTAT register
 * can be at different offsets but these bits must be the same.
 */
#define	NB_ISTAT_CON		0x08	/* connected */
#define	NB_ISTAT_SIP		0x02	/* scsi interrupt pending */
#define	NB_ISTAT_DIP		0x01	/* dma interrupt pending */

/*
 * Function Codes for the SCRIPTS entry points
 */
#define	NSS_STARTUP		0	/* select target and start request */
#define	NSS_CONTINUE		1	/* continue after phase mismatch */
#define	NSS_WAIT4RESELECT	2	/* wait for reselect */
#define	NSS_CLEAR_ACK		3	/* continue after both SDTR msgs okay */
#define	NSS_EXT_MSG_OUT		4	/* send out extended msg to target */
#define	NSS_ERR_MSG		5	/* send Message Reject message */
#define	NSS_BUS_DEV_RESET	6	/* do Bus Device Reset */
#define	NSS_ABORT		7	/* abort commands */
#define	NSS_EXT_MSG_IN		8	/* receive extended msg from target */
#define	NSS_PMM			9	/* Phase mismatch */

/*
 * SCRIPTS command opcodes for Block Move instructions
 */
#define	NSOP_MOVE_MASK		0xF8	/* just the opcode bits */
#define	NSOP_MOVE		0x18	/* MOVE FROM ... */
#define	NSOP_CHMOV		0x08	/* CHMOV FROM ... */

#define	NSOP_PHASE		0x07	/* the expected phase bits */
#define	NSOP_DATAOUT		0x00	/* data out */
#define	NSOP_DATAIN		0x01	/* data in */
#define	NSOP_COMMAND		0x02	/* command */
#define	NSOP_STATUS		0x03	/* status */
#define	NSOP_DT_DATAOUT		0x04	/* DT data out */
#define	NSOP_DT_DATAIN		0x05	/* DT data in */
#define	NSOP_MSGOUT		0x06	/* message out */
#define	NSOP_MSGIN		0x07	/* message out */

/*
 * Interrupt vectors numbers that script may generate
 */
#define	NINT_OK		0xff00		/* device accepted the command */
#define	NINT_ILI_PHASE	0xff01		/* Illegal Phase */
#define	NINT_UNS_MSG	0xff02		/* Unsupported message */
#define	NINT_UNS_EXTMSG 0xff03		/* Unsupported extended message */
#define	NINT_MSGIN	0xff04		/* Message in expected */
#define	NINT_MSGREJ	0xff05		/* Message reject */
#define	NINT_RESEL	0xff06		/* C710 chip reselcted */
#define	NINT_DISC	0xff07		/* Diconnect message received */
#define	NINT_RESEL_ERR	0xff08		/* Reselect id error */
#define	NINT_SDP_MSG	0xff09		/* Save Data Pointer message */
#define	NINT_RP_MSG	0xff0a		/* Restore Pointer message */
#define	NINT_SIGPROC	0xff0b		/* Signal Process */
#define	NINT_TOOMUCHDATA 0xff0c		/* Too much data to/from target */
#define	NINT_SDTR	0xff0d		/* SDTR message received */
#define	NINT_NEG_REJECT 0xff0e		/* invalid negotiation exchange */
#define	NINT_REJECT	0xff0f		/* failed to send msg reject */
#define	NINT_DEV_RESET	0xff10		/* bus device reset completed */
#define	NINT_WDTR	0xff11		/* WDTR complete. */
#define	NINT_IWR	0xff12		/* Ignore wide residue. */
#define	NINT_PPR	0xff13		/* Parallel Protocol message */
#define	NINT_PMM	0xff14		/* Phase Mis Match interrupt (1010) */

/*
 * defaults for	the global properties
 */
#define	DEFAULT_SCSI_OPTIONS	SCSI_OPTIONS_DR
#define	DEFAULT_TAG_AGE_LIMIT	2
#define	DEFAULT_WD_TICK		10

/*
 * Offsets into the cmd_cdb[] array (in glm_scsi_cmd) for proxy data
 */
#define	GLM_PROXY_TYPE		CDB_GROUP0
#define	GLM_PROXY_RESULT	GLM_PROXY_TYPE+1

/*
 * Currently supported proxy types
 */
#define	GLM_PROXY_SNDMSG	1

/*
 * reset delay tick
 */
#define	GLM_WATCH_RESET_DELAY_TICK 50	/* specified in milli seconds */

/*
 * throttle support.
 */
#define	MAX_THROTTLE	254
#define	HOLD_THROTTLE	0
#define	DRAIN_THROTTLE	-1
#define	QFULL_THROTTLE	-2

#define	NOTAG(tgt)		(glm->g_notag & (1<<(tgt)))
#define	TAGGED(tgt)		((glm->g_notag & (1<<(tgt))) == 0)

/*
 * debugging.
 */
#if defined(GLM_DEBUG)

#define	GLM_DBGPR(m, args)	\
	if (glm_debug_flags & (m)) \
		glm_printf args
#else	/* ! defined(GLM_DEBUG) */
#define	GLM_DBGPR(m, args)
#endif	/* defined(GLM_DEBUG) */

#define	NDBG0(args)	GLM_DBGPR(0x01, args)		/* init	*/
#define	NDBG1(args)	GLM_DBGPR(0x02, args)		/* normal running */
#define	NDBG2(args)	GLM_DBGPR(0x04, args)		/* property handling */
#define	NDBG3(args)	GLM_DBGPR(0x08, args)		/* pkt handling */

#define	NDBG4(args)	GLM_DBGPR(0x10, args)		/* kmem alloc/free */
#define	NDBG5(args)	GLM_DBGPR(0x20, args)		/* polled cmds */
#define	NDBG6(args)	GLM_DBGPR(0x40, args)		/* interrupts */
#define	NDBG7(args)	GLM_DBGPR(0x80, args)		/* queue handling */

#define	NDBG8(args)	GLM_DBGPR(0x0100, args)		/* arq */
#define	NDBG9(args)	GLM_DBGPR(0x0200, args)		/* Tagged Q'ing */
#define	NDBG10(args)	GLM_DBGPR(0x0400, args)		/* halting chip */
#define	NDBG11(args)	GLM_DBGPR(0x0800, args)		/* power management */

#define	NDBG12(args)	GLM_DBGPR(0x1000, args)
#define	NDBG13(args)	GLM_DBGPR(0x2000, args)
#define	NDBG14(args)	GLM_DBGPR(0x4000, args)
#define	NDBG15(args)	GLM_DBGPR(0x8000, args)

#define	NDBG16(args)	GLM_DBGPR(0x010000, args)
#define	NDBG17(args)	GLM_DBGPR(0x020000, args)	/* scatter/gather */
#define	NDBG18(args)	GLM_DBGPR(0x040000, args)
#define	NDBG19(args)	GLM_DBGPR(0x080000, args)

#define	NDBG20(args)	GLM_DBGPR(0x100000, args)
#define	NDBG21(args)	GLM_DBGPR(0x200000, args)	/* dma */
#define	NDBG22(args)	GLM_DBGPR(0x400000, args)	/* reset */
#define	NDBG23(args)	GLM_DBGPR(0x800000, args)	/* abort */

#define	NDBG24(args)	GLM_DBGPR(0x1000000, args)	/* capabilities */
#define	NDBG25(args)	GLM_DBGPR(0x2000000, args)	/* flushing */
#define	NDBG26(args)	GLM_DBGPR(0x4000000, args)
#define	NDBG27(args)	GLM_DBGPR(0x8000000, args)

#define	NDBG28(args)	GLM_DBGPR(0x10000000, args)	/* hotplug */
#define	NDBG29(args)	GLM_DBGPR(0x20000000, args)	/* timeouts */
#define	NDBG30(args)	GLM_DBGPR(0x40000000, args)	/* glm_watch */
#define	NDBG31(args)	GLM_DBGPR(0x80000000, args)	/* negotations */

/*
 * auto request sense
 */
#define	RQ_MAKECOM_COMMON(pktp, flag, cmd) \
	(pktp)->pkt_flags = (flag), \
	((union scsi_cdb *)(pktp)->pkt_cdbp)->scc_cmd = (cmd), \
	((union scsi_cdb *)(pktp)->pkt_cdbp)->scc_lun = \
	    (pktp)->pkt_address.a_lun

#define	RQ_MAKECOM_G0(pktp, flag, cmd, addr, cnt) \
	RQ_MAKECOM_COMMON((pktp), (flag), (cmd)), \
	FORMG0ADDR(((union scsi_cdb *)(pktp)->pkt_cdbp), (addr)), \
	FORMG0COUNT(((union scsi_cdb *)(pktp)->pkt_cdbp), (cnt))


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_GLMVAR_H */

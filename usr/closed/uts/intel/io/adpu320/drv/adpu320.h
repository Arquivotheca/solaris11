/*
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 1995-2003 Adaptec, Inc.
 * All Rights Reserved.
 * PH2.0 RC 03/24/03
 */

#ifndef	_ADPU320_H
#define	_ADPU320_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * A list of doubly linked elements
 */

typedef struct L2el {
	struct	L2el	*l2_nextp;
	struct	L2el	*l2_prevp;
	void		*l2_private;
} L2el_t;

#define	L2_INIT(headp)	\
	(((headp)->l2_nextp = (headp)), ((headp)->l2_prevp = (headp)), \
	((headp)->l2_private = NULL))

#define	L2_EMPTY(headp) ((headp)->l2_nextp == (headp))

/*
 * One per target-lun nexus
 */
struct adpu320_unit
{
	unsigned au_arq : 1;		/* auto-request sense enable */
	unsigned au_tagque : 1;		/* tagged queueing enable */
	unsigned au_gt1gig : 1;		/* disk > 1 Gibabyte */
	unsigned au_resv : 5;		/* pad rest of bits */
	unsigned long au_total_sectors;
};

#define	ADPU320_PAC_RETRY_COUNT	1

#define	ADPU320_CONTIG_MEM	0
#define	ADPU320_NONCONTIG_MEM	1

#define	MAX_LUNS	64
#define	MAX_TARGETS	16
#define	MAX_EQ		(MAX_LUNS * MAX_TARGETS)	/* Max eqper */

#define	ADPU320_SCSI_ID	7		/* Default ID of controllers */

#ifndef TRUE
#define	TRUE		1
#define	FALSE		0
#endif

#define	UBYTE unsigned char
#define	UWORD unsigned short
#define	DWORD unsigned int
#define	ULONG unsigned long

#define	USECSPERSECOND	1000000

/*
 * move from adpu320 structure to adpu320_config_t
 */
#define	APP_RESP_MUTEX(cfp)	(&cfp->adpu320_response_mutex)

#if defined(ADPU320_DEBUG) || defined(ADPU320_DEBUG2)
#define		DENT	0x00001	/* Display function names on entry	*/
#define		DPKT	0x00002	/* Display packet data			*/
#define		DDATA	0x00004	/* Display all data			*/
#define		DTEMP	0x00008	/* Display wrt to currrent debugging	*/
#define		DCHN	0x00010	/* Display channel number on fn. entry	*/
#define		DSTUS	0x00020	/* Display interrupt status		*/
#define		DINIT	0x00040	/* Display init data			*/
#define		DTEST	0x00080	/* Display test data			*/
#define		DPROBE	0x00100	/* Display probe data			*/
#define		DVERBOS 0x00200	/* Display verbose data			*/
#define		DTUR    0x00400	/* Display kmem_(alloc/free) data	*/
#define		DLIST   0x00800	/* Display number of element		*/
#define		DINTR	0x02000	/* Display Interupt data		*/
#define		DIOERR	0x01000	/* Display IO status return 		*/
#define		DIOSTS	0x04000	/* Display IO status return 		*/
#define		DPKTDMP	0x08000	/* Display  pkt dump			*/
#define		DTIME	0x10000	/* Display  pkt dump			*/

#define	ISPRINT(c) (((c) >= ' ') && ((c) <= '~'))

#define	PRINT_BUF_SIZE		81920
#define	LINESIZE		512

#define	NUM_LINES	15
#define	ESC		0x1b

#define	adpu320_va_start(list, name) (void) (list = (void *)((char *)&name + \
	((sizeof (name) + (sizeof (int) - 1)) & ~(sizeof (int) - 1))))
#define	adpu320_va_end(list)

#endif

#define	ADPU320_RESET_BUS	-1

#define	ADPU320_SUCCESS		0
#define	ADPU320_FAILURE		1
#define	ADPU320_DONE		2


#define	DMA_DEFAULT_BURSTSIZE	(uint_t)0x0000007F	/* 1,2,4,8,16,32,64 */

#define	DEFAULT_NUM_SCBS	24

#define	ADAPTEC_PCI_ID		0x9005
#define	ADPU320_PHYMAX_DMASEGS	255	/* phy max of Scatter/Gather seg */
#define	ADPU320_SENSE_LEN	0x12	/* AUTO-Request sense size in bytes */
#define	MAX_CDB_LEN		12	/* Standard SCSI CDB length */


#define	ADPU320_DIP(adpu320)	(((adpu320)->cfp)->ab_dip)

#define	TRAN2ADPU320(hba)	((adpu320_soft_state_t *) \
					(hba)->tran_tgt_private)
#define	TRAN2CFP(tran)		((adpu320_config_t *)((tran)->tran_hba_private))

#define	SDEV2CFP(sd)		(TRAN2CFP(SDEV2TRAN(sd)))
#define	SDEV2ADPU320(sd)	(TRAN2ADPU320(SDEV2TRAN(sd)))

#define	SOFT2CFP(adpu320)	((adpu320_config_t *)(adpu320)->cfp)

/*
 * HBA interface macros
 */
#define	SDEV2TRAN(sd)		((sd)->sd_address.a_hba_tran)
#define	SDEV2ADDR(sd)		(&((sd)->sd_address))
#define	PKT2TRAN(pkt)		((pkt)->pkt_address.a_hba_tran)
#define	ADDR2TRAN(ap)		((ap)->a_hba_tran)
#define	ADDR2ADPU320P(ap)	(TRAN2ADPU320(ADDR2TRAN(ap)))
#define	ADDR2CFP(ap)		(TRAN2CFP(ADDR2TRAN(ap)))
#define	TRAN2ADPU320UNITP(tran)	((TRAN2ADPU320(tran))->a_unitp)
#define	ADDR2ADPU320UNITP(pktp)	(TRAN2ADPU320UNITP(ADDR2TRAN(pktp)))
#define	PKT2ADPU320UNITP(pktp)	(TRAN2ADPU320UNITP(PKT2TRAN(pktp)))
#define	PKT2ADPU320P(pktp)	(TRAN2ADPU320(PKT2TRAN(pktp)))
#define	PKT2CFP(pktp) 		(TRAN2CFP(PKT2TRAN(pktp)))

#define	SCBP(pkt)	((struct scsi_status *)(pkt)->pkt_scbp)
#define	SCBP_C(pkt)	((*(pkt)->pkt_scbp) & STATUS_MASK)

#define	INST(x)	(((adpu320_soft_state_t *)x - 1)->instance)

/* Solaris additions */
#define	ADPU320_MAX_DMA_SEGS	255	/* Max used Scatter/Gather seg	*/
#define	ADPU320_MAX_TARGETS	16

/* Scatter Gather, data length and data pointer structure. */
struct adpu320_sg {
	HIM_BUS_ADDRESS		data_addr;
#if OSM_BUS_ADDRESS_SIZE == 64
	unsigned long long	data_len;
#else
	DWORD data_len;
#endif
};

typedef struct adpu320_entry_
{
	struct adpu320_entry_	*next;
} adpu320_entry_t;


typedef struct adpu320_queue_
{
	adpu320_entry_t		*queue_head;
	adpu320_entry_t		*queue_tail;
} adpu320_queue_t;


typedef struct element_
{
	uchar_t				*virt_addr;
	HIM_BUS_ADDRESS			phys_addr;
	ushort_t			dog_bite;
	struct scsi_pkt			*pktp;
	struct element_			*next_element;
	unsigned			flag;
	struct element_			*next_abort_element;
} element_t;


typedef struct element_list_
{
	element_t			*first_element;
	element_t			*last_element;
} element_list_t;


typedef struct adpu320_dma_info_ {
	struct adpu320_dma_info_	*next;
	uchar_t				*virt_addr;
	HIM_BUS_ADDRESS			phys_addr;
	ddi_dma_handle_t		ab_dmahandle;
	ddi_acc_handle_t		ab_handle;
	int				type;
	int				size;
} adpu320_dma_info_t;


typedef struct adpu320_regs_ {
	ddi_acc_handle_t		regs_handle;
	caddr_t				baseAddress;
} adpu320_regs_t;


typedef struct sp_
{
	/*
	 * The next pointer must be first.
	 */
	struct sp_		*next;		/* for chaining requests */

	struct adpu320_scsi_cmd	*Sp_cmdp;	/* ptr to the scsi_cmd */
	struct adpu320_config_	*SP_ConfigPtr;	/* ptr to adpu320_config_t */
	HIM_BUS_ADDRESS		Sp_paddr;	/* physical address */
	struct scsi_arq_status	Sp_sense;	/* Auto Request sense */

	/*
	 * scatter/gather
	 */
	struct adpu320_sg	Sp_sg_list[ADPU320_MAX_DMA_SEGS];
	UBYTE			SP_SegCnt;
	HIM_BUS_ADDRESS		SP_SegPtr;
	HIM_BUS_ADDRESS		Sp_SensePtr;	/* Pointer to Sense Area */
	/*
	 * SCSI Command Descriptor Block
	 */
	UBYTE			Sp_CDB[MAX_CDB_LEN];
} sp_t;

#ifdef ADPU320_DEBUG
struct adpu320_stats {
	long long	total_iobs;
	unsigned	iobs_sent;
	unsigned	iobs_queued;
	unsigned	max_iobs_sent;
	unsigned	max_iobs_queued;
};
#endif


typedef struct adpu320_config_
{
#ifdef ADPU320_DEBUG
	/*
	 * The next pointer must be first.
	 */
	struct adpu320_config_	*next;
#endif

	UBYTE	ha_ScsiId;		/* Host Adapter's SCSI ID */
#if 0
	DWORD	ha_BaseAddress;		/* IO Base Address */

	union
	{
		/* Base address (I/O or memory) */
		DWORD			BaseAddress;
		struct aic7870_reg	*Base;
	} Cf_base_addr;
#endif

	UBYTE	ha_BusNumber;		/* PCI bus number */
	UBYTE	ha_DeviceNumber;	/* PCI device number */
// mdt
	UBYTE	ha_FuncNumber;		/* PCI function number */
	UBYTE	cmd_retry[MAX_TARGETS];
// mdt
	void	*adpu320_timeout_id;
	UBYTE	ha_MaxTargets;		/* Maximum number of targets on bus */

	element_list_t			*Ptr_List;

	union
	{
		DWORD ha_productID;

		struct {
#if defined(__amd64)
		/* RST - look at this */
		UWORD VendorId;		/* 9004 */
		UWORD AdapterId;	/* Host Adapter ID (ie. 0x78XX) */
#else	/* !__amd64 */
		UWORD AdapterId;	/* Host Adapter ID (ie. 0x78XX) */
		UWORD VendorId;		/* 9004 */
#endif	/* __amd64 */
		} id_struct;
	} Cf_id;

	void				*ha_adapterTSH;
	struct HIM_FUNC_PTRS_		*ha_himFuncPtr;
	struct HIM_ADAPTER_PROFILE_	*ha_profile;
	void				*ha_targetTSH[MAX_EQ];
	struct HIM_TARGET_PROFILE_	*ha_targetProfile[MAX_EQ];
	struct HIM_CONFIGURATION_	*ha_himConfig;
#ifdef ADPU320_DEBUG
	struct adpu320_stats		ha_stats[MAX_EQ];
#endif

	/* Solaris additions to block */
	dev_info_t			*ab_dip;
	adpu320_queue_t			ab_scb_start_queue;
	adpu320_queue_t			ab_scb_completed_queue;
	HIM_IOB				*ab_last_IOBp;
	kmutex_t			ab_mutex;
	ddi_intr_handle_t		*ab_htable;
	int				ab_intr_type;
	int				ab_intr_cnt;
	uint_t				ab_intr_pri;
	int				ab_intr_cap;
	ushort_t			ab_child;
	unsigned char			ab_flag;
	ddi_softintr_t			ab_softid;

	/* addition for 2.5 PCI DDI changes */
	adpu320_dma_info_t		*ab_dmainfo;

#if 0
	/* Data access handle for access the PCI data */
	ddi_acc_handle_t		acc_handle;
#endif

	/* additions for timeout functionality */
	ushort_t			ab_pkts_out;
	int				ab_ha_profilesize;

	kcondvar_t			ab_quiesce_cv;

	/* additions for reset notify functionality */
	L2el_t	adpu320_reset_notify_list;   /* list of reset notifications */
	kmutex_t adpu320_reset_notify_mutex;  /* and a mutex to protect it */

} adpu320_config_t;

/*
 * One per target-lun nexus
 */
typedef struct adpu320_soft_state_
{
	scsi_hba_tran_t			*a_tran;
	adpu320_config_t		*cfp;
	struct adpu320_unit		*a_unitp;
	int				instance;	/* DDI-2.5 */
} adpu320_soft_state_t;

/*
 * Macros to flip from scsi_pkt to adpu320_scsi_cmd (hba pkt private data)
 */
#define	PKT2CMD(pkt)	((struct adpu320_scsi_cmd *)(pkt)->pkt_ha_private)
#define	CMD2PKT(cmd)	((struct scsi_pkt *)(cmd)->cmd_pkt)

struct 	adpu320_scsi_cmd {
	struct scsi_pkt	*cmd_pkt;
	ULONG		cmd_flags;	/* flags from scsi_init_pkt */
	DWORD		cmd_cflags;	/* private hba CFLAG flags */
	UBYTE		lun;
	UBYTE		target;
	int 		watch;
	ddi_dma_handle_t sp_dmahandle; /* for scb */
	ddi_acc_handle_t sp_acchandle;
	ddi_acc_handle_t rsv_handle; /* for iob reserve */
	ddi_dma_handle_t rsv_dmahandle;
	ddi_dma_cookie_t rsv_dmacookie;

	adpu320_config_t	*cmd_cfp;	/* for easier lookup */
	void		*cmd_private;

	HIM_IOB		*cmd_IOBp;	/* Point to struct HIM_IOB */
	/* struct sequencer_ctrl_block *cmd_scbp; */
};

#define	SC_XPKTP(X)	((struct target_private *)((X)->pkt_private))

struct	target_private {
	struct scsi_pkt *x_fltpktp;	/* link to autosense packet	*/
	struct buf	*x_bp;		/* request buffer		*/
	union {
		struct buf	*xx_rqsbp; /* request sense buffer	*/
		struct uscsi_cmd *xx_scmdp; /* user scsi command 	*/
	} targ;

	daddr_t		x_srtsec;	/* starting sector		*/
	int		x_seccnt;	/* sector count			*/
	int		x_byteleft;	/* bytes left to do		*/
	int		x_bytexfer;	/* bytes xfered for this ops	*/
	int		x_tot_bytexfer;	/* total bytes xfered per cmd	*/

	ushort_t	x_cdblen;	/* cdb length			*/
	short		x_retry;	/* retry count			*/
	int		x_flags;	/* flags			*/

	opaque_t	x_sdevp;	/* backward ptr target unit	*/
	void		(*x_callback)(); /* target drv internal cb func	*/
};

struct adpu320_productInfo
{
	DWORD	id;
	DWORD	mask;
	UBYTE	index;
	UBYTE	himtype;
};

#define	PKT_PRIV_LEN		sizeof (struct target_private)

/*
 * cmd_cflags definitions
 */
#define	CFLAG_FINISHED		0x000001	/* command completed */
#define	CFLAG_DMAVALID		0x000002	/* dma mapping valid */
#define	CFLAG_DMASEND		0x000004	/* data is going 'out' */
#define	CFLAG_POLL		0x000008	/* poll for completion */
#define	CFLAG_FAILED		0x000010	/* command failed */


/*
 * ab_flag defines for Solaris in adpu320_config_
 */
#define	ADPU320_POLLING			0x0001
#define	ADPU320_RESTART			0x0002
#define	ADPU320_QUEUE_FROZEN		0x0004
#define	ADPU320_PAC_ACTIVE		0x0008
#define	ADPU320_DRAINING		0x0010

#if 0
#define	CFP_BaseAddress		Cf_base_addr.BaseAddress
#define	CFP_Base		Cf_base_addr.Base
#endif

#define	ADPU320_IOB_ABORTED	0x01


#if defined(__i386__) || defined(__amd64)
#define	APCI_FLIP(b_endian)	(ulong_t)(b_endian)
#define	APCI_FLIP24(b_endian)	(ulong_t)(b_endian)
#define	APCI_FLIP64(b_endian)	(longlong_t)(b_endian)
#else
#define	APCI_FLIP64(b_endian) (longlong_t)(\
			((longlong_t)b_endian & 0xff) << 56 | \
			((longlong_t)b_endian & 0xff00) << 40 | \
			((longlong_t)b_endian & 0xff0000) << 24 | \
			((longlong_t)b_endian & 0xff000000) << 8 | \
			((longlong_t)b_endian & 0xff00000000) >> 8 | \
			((longlong_t)b_endian & 0xff0000000000) << 24 | \
			((longlong_t)b_endian & 0xff000000000000) >> 40 | \
			((longlong_t)b_endian & 0xff00000000000000) >> 56)

#define	APCI_FLIP24(b_endian)	(ulong_t((ulong_t)b_endian & 0xff) << 8 | \
				((ulong_t)b_endian & 0xff00) >> 8 | \
				((ulong_t)b_endian & 0xffff0000))

#define	APCI_FLIP(b_endian)	(ulong_t)(((ulong_t)b_endian & 0xff) << 24 | \
				((ulong_t)b_endian & 0xff00) << 8 | \
				((ulong_t)b_endian & 0xff0000) >> 8 | \
				((ulong_t)b_endian & 0xff000000) >> 24)

#endif	/* __i386__ || __amd64 */

#define	HA_CH0_REL		0x001
#define	HA_CH1_REL		0x002
#define	HA_STRUCT_REL		0x003 /* first 2 bits used to count channels */
#define	HA_REL			0x004
#define	HA_ADDR_REL		0x005
#define	HA_CONF_REL		0x006
#define	HA_PROFILE_REL		0x008
#define	HA_IOB_REL		0x010
#define	HA_REQS_REL		0x020
#define	HA_OPTION_REL		0x030
#define	HA_DEV_REL		0x040
#define	HA_CFP_OPTION_REL	0x050
#define	HA_SG_REL		0x080
#define	HA_LUQ_REL		0x100
#define	HA_IDENT_REL		0x200
#define	HA_IOBRESV_REL		0x300
#define	HA_SENSE_REL		0x400

#define	TRUE		1
#define	FALSE		0
#define	UNDEFINED	-1

#define	SEC_INUSEC	1000000

#define	ADPU320_SETGEOM(hd, sec) (((hd) << 16) | (sec))


#define	PRF	prom_printf

/*
 *
 * External references
 */
void adpu320_mem_release(adpu320_config_t *, int flag);

HIM_UINT32 adpu320_IobSpecialCompleted(HIM_IOB *);

#if defined(ADPU320_DEBUG) || defined(ADPU320_DEBUG2)
int adpu320_dump_chars(char *, char *, int);

void adpu320_dump_config(adpu320_config_t *);

void adpu320_dump_scb(char *, adpu320_config_t *, sp_t *);

void adpu320_dump_profile(HIM_ADAPTER_PROFILE *);

void adpu320_printf(char *, ...);
#endif

/*
 * Local Function Prototypes
 */

HIM_UINT8
OSMMapIOHandle(
void HIM_PTR,
HIM_UINT8,
HIM_UINT32,
HIM_UINT32,
HIM_UINT32,
HIM_UINT16,
HIM_IO_HANDLE HIM_PTR
);

HIM_UINT8
OSMReleaseIOHandle(
void HIM_PTR,
HIM_IO_HANDLE handle
);

void
OSMIobCompleted(
HIM_IOB *
);

void
OSMEvent(
void *,
HIM_UINT16,
void *,
...
);

HIM_BUS_ADDRESS
OSMGetBusAddress(
HIM_TASK_SET_HANDLE,
HIM_UINT8,
void HIM_PTR
);

void
OSMAdjustBusAddress(
HIM_BUS_ADDRESS HIM_PTR,
int
);

HIM_UINT32
OSMGetNVSize(
HIM_TASK_SET_HANDLE
);

HIM_UINT8
OSMPutNVData(
HIM_TASK_SET_HANDLE,
HIM_UINT32,
void HIM_PTR,
HIM_UINT32
);

HIM_UINT8
OSMGetNVData(
HIM_TASK_SET_HANDLE,
void *,
HIM_UINT32,
HIM_UINT32
);

HIM_UEXACT8
OSMReadUExact8(
HIM_IO_HANDLE,
HIM_UINT32
);

HIM_UEXACT16
OSMReadUExact16(
HIM_IO_HANDLE,
HIM_UINT32
);

HIM_UEXACT32
OSMReadUExact32(
HIM_IO_HANDLE,
HIM_UINT32
);

void
OSMReadStringUExact8(
HIM_IO_HANDLE,
HIM_UINT32,
HIM_UEXACT8 HIM_PTR,
HIM_UINT32,
HIM_UINT8
);

void
OSMReadStringUExact16(
HIM_IO_HANDLE,
HIM_UINT32,
HIM_UEXACT16 HIM_PTR,
HIM_UINT32,
HIM_UINT8
);

void
OSMReadStringUExact32(
HIM_IO_HANDLE,
HIM_UINT32,
HIM_UEXACT32 HIM_PTR,
HIM_UINT32,
HIM_UINT8
);

void
OSMWriteUExact8(
HIM_IO_HANDLE,
HIM_UINT32,
HIM_UEXACT8
);

void
OSMWriteUExact16(
HIM_IO_HANDLE,
HIM_UINT32,
HIM_UEXACT16
);

void
OSMWriteUExact32(
HIM_IO_HANDLE,
HIM_UINT32,
HIM_UEXACT32
);

void
OSMWriteStringUExact8(
HIM_IO_HANDLE,
HIM_UINT32,
HIM_UEXACT8 HIM_PTR,
HIM_UINT32,
HIM_UINT8
);

void
OSMWriteStringUExact16(
HIM_IO_HANDLE,
HIM_UINT32,
HIM_UEXACT16 HIM_PTR,
HIM_UINT32,
HIM_UINT8
);

void
OSMWriteStringUExact32(
HIM_IO_HANDLE,
HIM_UINT32,
HIM_UEXACT32 HIM_PTR,
HIM_UINT32,
HIM_UINT8
);

void
OSMSynchronizeRange(
HIM_IO_HANDLE,
HIM_UINT32,
HIM_UINT32
);

void
OSMWatchdog(
HIM_TASK_SET_HANDLE,
HIM_WATCHDOG_FUNC,
HIM_UINT32
);

HIM_UINT8
OSMSaveInterruptState(
);

void
OSMSetInterruptState(
HIM_UINT8
);

HIM_UEXACT8
OSMReadPCIConfigurationByte(
HIM_TASK_SET_HANDLE,
HIM_UINT8
);

HIM_UEXACT16
OSMReadPCIConfigurationWord(
HIM_TASK_SET_HANDLE,
HIM_UINT8
);

HIM_UEXACT32
OSMReadPCIConfigurationDword(
HIM_TASK_SET_HANDLE,
HIM_UINT8
);

void
OSMWritePCIConfigurationByte(
HIM_TASK_SET_HANDLE,
HIM_UINT8,
HIM_UEXACT8
);

void
OSMWritePCIConfigurationWord(
HIM_TASK_SET_HANDLE,
HIM_UINT8,
HIM_UEXACT16
);

void
OSMWritePCIConfigurationDword(
HIM_TASK_SET_HANDLE,
HIM_UINT8,
HIM_UEXACT32
);

void
OSMDelay(
void HIM_PTR osmAdapterContext,
HIM_UINT32
);

void
OSMmemcpy(
void *,
void *,
int
);

int
OSMmemcmp(
void *,
void *,
int
);

void
OSMmemset(
void *,
int,
int
);

void
adpu320_append_element(
element_list_t *,
element_t *
);

HIM_BUS_ADDRESS
adpu320_search_element(
element_list_t *,
uchar_t *
);

int
adpu320_delete_element(
element_list_t *,
uchar_t *
);

void
adpu320_FillOutOSMPointers(
HIM_OSM_FUNC_PTRS *
);

int
adpu320_validate_target(
HIM_UINT32,
adpu320_config_t *
);

int
adpu320_issue_PAC(
adpu320_config_t *
);

int
adpu320_create_target(
adpu320_config_t *,
HIM_UINT32
);

int
adpu320_attach_PAC(
adpu320_config_t *
);

void
adpu320_set_timeout(
adpu320_config_t *
);

void
adpu320_verify_lock_held(
char *,
kmutex_t *
);

void
adpu320_send_auto_config(
adpu320_config_t *
);

void
adpu320_free_cmd(
struct adpu320_scsi_cmd *,
HIM_IOB	*
);

int
adpu320_wait_special_iob_complete(
struct adpu320_scsi_cmd *
);

uint_t
adpu320_drain_start_queue(
adpu320_config_t *
);

void
adpu320_drain_completed_queue(
adpu320_config_t *
);

int
adpu320_get_pci_id(
dev_info_t *,
ushort_t *,
ushort_t *
);

static int
adpu320_probe(
register dev_info_t *
);

static int
adpu320_ioctl(
dev_t dev,
int cmd,
intptr_t arg,
int mode,
cred_t *credp,
int *rvalp
);

void
adpu320_poll_request(
adpu320_config_t *
);

void
adpu320_restart_queues(
adpu320_config_t *
);

void
adpu320_empty_queue(
adpu320_queue_t	*
);

adpu320_entry_t *
adpu320_remove_queue(
adpu320_queue_t	*
);

void
adpu320_add_queue(
adpu320_queue_t	*,
adpu320_entry_t	*
);

int
adpu320_get_reg_prop_index(
adpu320_config_t *,
HIM_UINT8,
uint_t *
);

int
adpu320_tran_quiesce(
dev_info_t *
);

static int
adpu320_tran_unquiesce(
dev_info_t *
);

void
adpu320_queue_IOB(
HIM_IOB *
);

/*
 * reset_notify handling: these elements are on the ccc_t's
 * reset_notify_list, one for each notification requested.  The
 * gtgtp isn't needed except for debug.
 */

typedef struct adpu320_reset_notify_entry {
	adpu320_soft_state_t	*adpu320_unitp;
	void (*callback)(caddr_t);
	caddr_t	arg;
	L2el_t l2_link;
} adpu320_reset_notify_entry_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _ADPU320_H */

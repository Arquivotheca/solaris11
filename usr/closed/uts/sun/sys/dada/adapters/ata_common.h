/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_ATA_COMMON_H
#define	_ATA_COMMON_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/varargs.h>
#include <sys/dktp/dadkio.h>
#include <sys/dada/dada.h>
#include <sys/ddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/sunddi.h>
#include <sys/dada/adapters/ghd/ghd.h>

#define	ATA_DEBUG
#define	UNDEFINED	-1



/*
 * ac_flags (per-controller)
 */
#define	AC_GHD_INIT			0x02
#define	AC_ATAPI_INIT			0x04
#define	AC_DISK_INIT			0x08
#define	AC_SCSI_HBA_TRAN_ALLOC		0x1000
#define	AC_SCSI_HBA_ATTACH		0x2000
#define	AC_ATTACH_IN_PROGRESS		0x4000

/*
 * device types
 */
#define	ATA_DEV_NONE	0
#define	ATA_DEV_DISK	1
#define	ATA_DEV_ATAPI	2

/*
 * ad_flags (per-drive)
 */
#define	AD_ATAPI		0x01
#define	AD_DISK			0x02
#define	AD_MUTEX_INIT		0x04
#define	AD_ATAPI_OVERLAP	0x08
#define	AD_DSC_OVERLAP		0x10
#define	AD_NO_CDB_INTR		0x20


/*
 * generic return codes
 */
#define	SUCCESS		DDI_SUCCESS
#define	FAILURE		DDI_FAILURE

/*
 * returns from intr status routines
 */
#define	STATUS_PARTIAL		0x01
#define	STATUS_PKT_DONE		0x02
#define	STATUS_NOINTR		0x03

/*
 * max targets and luns
 */
#define	ATA_MAXTARG	4
#define	ATA_CHANNELS 	2
#define	ATA_MAXLUN	16

/*
 * port offsets from base address ioaddr1
 */
#define	AT_DATA		0x00	/* data register 			*/
#define	AT_ERROR	0x01	/* error register (read)		*/
#define	AT_FEATURE	0x01	/* features (write)			*/
#define	AT_COUNT	0x02    /* sector count 			*/
#define	AT_SECT		0x03	/* sector number 			*/
#define	AT_LCYL		0x04	/* cylinder low byte 			*/
#define	AT_HCYL		0x05	/* cylinder high byte 			*/
#define	AT_DRVHD	0x06    /* drive/head register 			*/
#define	AT_STATUS	0x07	/* status/command register 		*/
#define	AT_CMD		0x07	/* status/command register 		*/

/*
 * port offsets from base address ioaddr2
 */
#define	AT_ALTSTATUS	0x02	/* alternate status (read)		*/
#define	AT_DEVCTL	0x02	/* device control (write)		*/
#define	AT_DRVADDR	0x07 	/* drive address (read)			*/

/*
 * Device control register
 */
#define	ATDC_NIEN    	0x02    /* disable interrupts 			*/
#define	ATDC_SRST	0x04	/* controller reset			*/
#define	ATDC_D3		0x08	/* Mysterious bit, must be set		*/

/*
 * Status bits from AT_STATUS register
 */
#define	ATS_BSY		0x80    /* controller busy 			*/
#define	ATS_DRDY	0x40    /* drive ready 				*/
#define	ATS_DWF		0x20    /* write fault 				*/
#define	ATS_DSC    	0x10    /* seek operation complete 		*/
#define	ATS_DRQ		0x08	/* data request 			*/
#define	ATS_CORR	0x04    /* ECC correction applied 		*/
#define	ATS_IDX		0x02    /* disk revolution index 		*/
#define	ATS_ERR		0x01    /* error flag 				*/

#define	IGN_ERR		0x00	/* do not check error bit		*/

/*
 * Status bits from AT_ERROR register
 */
#define	ATE_AMNF	0x01    /* address mark not found		*/
#define	ATE_TKONF	0x02    /* track 0 not found			*/
#define	ATE_ABORT	0x04    /* aborted command			*/
#define	ATE_IDNF	0x10    /* ID not found				*/
#define	ATE_MC		0x20    /* Media chane				*/
#define	ATE_UNC		0x40	/* uncorrectable data error		*/
#define	ATE_BBK		0x80	/* bad block detected			*/

/*
 * Drive selectors for AT_DRVHD register
 */
#define	ATDH_LBA	0x40	/* addressing in LBA mode not chs 	*/
#define	ATDH_DRIVE0	0xa0    /* or into AT_DRVHD to select drive 0 	*/
#define	ATDH_DRIVE1	0xb0    /* or into AT_DRVHD to select drive 1 	*/

/*
 * Common ATA commands.
 */
#define	ATC_SET_FEAT	0xef	/* set features				*/
#define	ATC_SLEEP	0xe6	/* sleep				*/
#define	ATC_STANDBY	0xe0	/* standby				*/
#define	ATC_IDLE	0xe1	/* Idle Immediate			*/
/*
 * Identify Drive: common capability bits
 */
#define	ATAC_LBA_SUPPORT	0x0200

/*
 * Offset in PCI Config space
 */
#define	VENDORID	0
#define	DEVICEID	0x0001
#define	REVISION	0x0008

/*
 * CMD 646U chip specific data.
 */
#define	CMDVID	0x1095
#define	CMDDID	0x646

#define	PIOR0	0x6d
#define	PIOW0	0x6d
#define	PIOR1	0x57
#define	PIOW1	0x57
#define	PIOR2	0x43
#define	PIOW2	0x43
#define	PIOR3	0x32
#define	PIOW3	0x32
#define	PIOR4	0x3f
#define	PIOW4	0x3f
#define	DMAR0	0x87
#define	DMAW0	0x87
#define	DMAR1	0x31
#define	DMAW1	0x31
#define	DMAR2	0x3f
#define	DMAW2	0x3f

/*
 * Additional CMD chip specific values.
 */

#define	CMD649	0x649

#define	UDMA0	0x3
#define	UDMA1	0x2
#define	UDMA2	0x1
#define	UDMA3	0x2
#define	UDMA4	0x1
#define	UDMA5	0x0
#define	UDMA6	0x3	/* not used, reserved for ata-133 */

/*
 * ACER Southbridge specific data.
 */
#define	ASBVID	0x10b9
#define	ASBDID	0x5229			/* ALI M5229 */
#define	ASBDID1575		0x5288		/* ULI M1575 */
#define	ASBREV	0xc4

/*
 * ACER SB 1573 specific data
 */

#define	ASB_1573_REV	0xC7
#define	ASB_1573_RST	0x51

#define	ASBPIO0	0x00
#define	ASBPIO1	0x50
#define	ASBPIO2	0x44
#define	ASBPIO3	0x33
#define	ASBPIO4	0x32
#define	ASBDMA0	0x00
#define	ASBDMA1	0x32
#define	ASBDMA2	0x31
#define	ASBADP0	0x03
#define	ASBADP1	0x02
#define	ASBADP2	0x01
#define	ASBADP3	0x01
#define	ASBADP4	0x01
#define	ASBADM0	0x04
#define	ASBADM1	0x01
#define	ASBADM2	0x01

#define	ASBUDMA0	0x0C
#define	ASBUDMA1	0x0B
#define	ASBUDMA2	0x0A
#define	ASBUDMA3	0x09
#define	ASBUDMA4	0x08
#define	ASBUDMA5	0x0F
#define	ASBUDMA6	0x0C	/* not used, reserved for ata-133 */

/*
 * ACER SB 1573 specific initialization registers.
 */

#define	CLASSATTR	0x43
#define	CLASS1		0x09
#define	CONTROL0	0x4A
#define	CONTROL1	0x4B
#define	CONTROL2	0x50
#define	PRIUDMA		0x56
#define	SECUDMA		0x57

/*
 * Sil 680A chip specific data.
 */
#define	SIL680	0x680

#define	STF0	0x328a
#define	STF1	0x2283
#define	STF2	0x1281
#define	STF3	0x10c3
#define	STF4	0x10c1
#define	SPIOR0	0x328a
#define	SPIOW0	0x328a
#define	SPIOR1	0x2283
#define	SPIOW1	0x2283
#define	SPIOR2	0x1104
#define	SPIOW2	0x1104
#define	SPIOR3	0x10c3
#define	SPIOW3	0x10c3
#define	SPIOR4	0x10c1
#define	SPIOW4	0x10c1
#define	SDMAR0	0x2208
#define	SDMAW0	0x2208
#define	SDMAR1	0x10c2
#define	SDMAW1	0x10c2
#define	SDMAR2	0x10c1
#define	SDMAW2	0x10c1
#define	SUDMA0	0x11
#define	SUDMA1	0x7
#define	SUDMA2	0x5
#define	SUDMA3	0x4
#define	SUDMA4	0x2
#define	SUDMA5	0x1

#define	SUDMA6	0x1	/* Only can be used when using 133MHz IDE clock */

/*
 * Bitmasks used.
 */

#define	DMA_BITS	0x7
/*
 * macros from old common hba code
 */
#define	ATA_INTPROP(devi, pname, pval, plen) \
	(ddi_prop_op(DDI_DEV_T_NONE, (devi), PROP_LEN_AND_VAL_BUF, \
	DDI_PROP_DONTPASS, (pname), (caddr_t)(pval), (plen)))


/*
 * per-controller data struct
 */
#define	CTL2DRV(cp, t, l)	(cp->ac_drvp[t][l])
struct	ata_controller {
	dev_info_t		*ac_dip;
	struct ata_controller	*ac_next;
	uint32_t		ac_flags;
	int			ac_simplex;	/* 1 if in simplex else 0 */
	int			ac_actv_chnl;	/* only valid if simplex == 1 */
						/* if no cmd pending -1 */
	int			ac_pending[ATA_CHANNELS];
	kmutex_t		ac_hba_mutex;

	struct ata_pkt		*ac_active[ATA_CHANNELS];  /* active packet */
	ccc_t			*ac_cccp[ATA_CHANNELS];    /* dummy for debug */

	ddi_iblock_cookie_t	ac_iblock;

	struct ata_drive	*ac_drvp[ATA_MAXTARG][ATA_MAXLUN];
	void			*ac_atapi_tran;	/* for atapi module */
	void			*ac_ata_tran;	/* for dada module */
	int32_t			ac_dcd_options;
	int32_t			ac_atapi_dev_rst_waittime;

	ccc_t			ac_ccc[ATA_CHANNELS];	/* for GHD module */

	/* op. regs data access handle */
	ddi_acc_handle_t	ata_datap[ATA_CHANNELS];
	caddr_t			ata_devaddr[ATA_CHANNELS];

	/* operating regs data access handle */
	ddi_acc_handle_t	ata_datap1[ATA_CHANNELS];
	caddr_t			ata_devaddr1[ATA_CHANNELS];

	ddi_acc_handle_t	ata_conf_handle;
	caddr_t			ata_conf_addr;

	ddi_acc_handle_t	ata_cs_handle;
	caddr_t			ata_cs_addr;

	ddi_acc_handle_t	ata_prd_acc_handle[ATA_CHANNELS];
	ddi_dma_handle_t	ata_prd_dma_handle[ATA_CHANNELS];
	caddr_t			ac_memp[ATA_CHANNELS];

	/* port addresses associated with ioaddr1 */
	uint8_t *ioaddr1[ATA_CHANNELS];
	uint8_t	*ac_data[ATA_CHANNELS];		/* data register */
	uint8_t	*ac_error[ATA_CHANNELS];	/* error register (read) */
	uint8_t	*ac_feature[ATA_CHANNELS];	/* features (write) */
	uint8_t	*ac_count[ATA_CHANNELS];	/* sector count	*/
	uint8_t	*ac_sect[ATA_CHANNELS];		/* sector number */
	uint8_t	*ac_lcyl[ATA_CHANNELS];		/* cylinder low byte */
	uint8_t	*ac_hcyl[ATA_CHANNELS];		/* cylinder high byte */
	uint8_t	*ac_drvhd[ATA_CHANNELS];	/* drive/head register */
	uint8_t	*ac_status[ATA_CHANNELS];	/* status/command register */
	uint8_t	*ac_cmd[ATA_CHANNELS];		/* status/command register */

	/* port addresses associated with ioaddr2 */
	uint8_t	*ioaddr2[ATA_CHANNELS];
	uint8_t	*ac_altstatus[ATA_CHANNELS];	/* alternate status (read) */
	uint8_t	*ac_devctl[ATA_CHANNELS];	/* device control (write) */
	uint8_t	*ac_drvaddr[ATA_CHANNELS];	/* drive address (read)	*/

	ushort_t	ac_vendor_id;		/* Controller Vendor */
	ushort_t	ac_device_id;		/* Controller Type */
	uchar_t		ac_revision;		/* Controller Revision */
	uchar_t		ac_piortable[5];
	uchar_t		ac_piowtable[5];
	uchar_t		ac_dmartable[3];
	uchar_t		ac_dmawtable[3];
	uchar_t		ac_udmatable[7];

	uchar_t		ac_suspended;
	uint32_t	ac_saved_dmac_address[ATA_CHANNELS];
	uint32_t	ac_speed[ATA_CHANNELS];
	uint32_t	ac_polled_finish;
	uint32_t	ac_polled_count;
	uint32_t	ac_intr_unclaimed;
	uint32_t	ac_power_level;
	uint32_t	ac_reset_done;
	/*
	 * Vectors to point to chip specific functions
	 */
	void		(*init_timing_tables)(
			struct ata_controller *ata_ctlp);
	void		(*program_read_ahead)(
			struct ata_controller *ata_ctlp);
	void		(*clear_interrupt)(
			struct ata_controller *ata_ctlp, int chno);
	int		(*get_intr_status)(
			struct ata_controller *ata_ctlp, int chno);
	void		(*program_timing_reg)(
			struct ata_drive *ata_drvp);
	void		(*enable_channel)(
			struct ata_controller   *ata_ctlp, int chno);
	void		(*disable_intr)(
			struct ata_controller *ata_ctlp, int chno);
	void		(*enable_intr)(
			struct ata_controller *ata_ctlp, int chno);
	int		(*get_speed_capabilities)(
			struct ata_controller *ata_ctlp, int chno);
	int		(*power_mgmt_initialize)(void);
	int		(*power_entry_point)(
			struct ata_controller *ata_ctlp, int component,
			int level);
	void		(*nien_toggle)(
			struct ata_controller *ata_ctlp, int chno,
			uint8_t cmd);
	void		(*reset_chip)(
			struct ata_controller *ata_ctlp, int chno);
};

/*
 * per-drive data struct
 */
struct	ata_drive {

	struct ata_controller	*ad_ctlp; 	/* pointer back to ctlr */
	gtgt_t			*ad_gtgtp;
	struct dcd_identify	ad_id;  	/* IDENTIFY DRIVE data */

	uint_t			ad_flags;
	int			ad_channel;
	uchar_t			ad_targ;	/* target */
	uchar_t			ad_lun;		/* lun */
	uchar_t			ad_drive_bits;

	/*
	 * Used by atapi side only
	 */
	uchar_t			ad_cdb_len;	/* Size of ATAPI CDBs */

#ifdef DSC_OVERLAP_SUPPORT
	struct ata_pkt 		*ad_tur_pkt;	/* TUR pkt for DSC overlap */
#endif

	/*
	 * Used by disk side only
	 */
	struct dcd_device	ad_device;
	struct dcd_address	ad_address;
	struct dcd_identify	ad_inquiry;
	int32_t			ad_dcd_options;

	uchar_t			ad_dmamode;
	uchar_t			ad_piomode;

	uchar_t			ad_rd_cmd;
	uchar_t			ad_wr_cmd;

	short			ad_block_factor;
	short			ad_bytes_per_block;
	uchar_t			ad_cur_disk_mode; /* Current disk Mode */
	uchar_t			ad_run_ultra;
	ushort_t		ad_invalid;	/* Whether the device exits */
	ushort_t		ad_ref;		/* Reference count */
};

/*
 * Definitions for the power level for the controller
 */
#define	ATA_POWER_UNKNOWN	0xFF
#define	ATA_POWER_D3		PM_LEVEL_D3
#define	ATA_POWER_D0		PM_LEVEL_D0

/*
 * The following are the defines for cur_disk_mode
 */
#define	PIO_MODE	0x01
#define	DMA_MODE	0x02


/*
 * Normal DMA capability bits
 */
#define	DMA_MODE0	0x0001
#define	DMA_MODE1	0x0002
#define	DMA_MODE2	0x0004

/*
 * Ultra dma capability bits
 */
#define	UDMA_MODE0	0x0001
#define	UDMA_MODE1	0x0002
#define	UDMA_MODE2	0x0004
#define	UDMA_MODE3	0x0008
#define	UDMA_MODE4	0x0010
#define	UDMA_MODE5	0x0020

/*
 * Currently selected speed for the drive.
 */
#define	UDMA_MODE5_SELECTED	0x2000
#define	UDMA_MODE4_SELECTED	0x1000
#define	UDMA_MODE3_SELECTED	0x0800
#define	UDMA_MODE2_SELECTED	0x0400
#define	UDMA_MODE1_SELECTED	0x0200
#define	UDMA_MODE0_SELECTED	0x0100

#define	DMA_MODE2_SELECTED	0x0400
#define	DMA_MODE1_SELECTED	0x0200
#define	DMA_MODE0_SELECTED	0x0100


/*
 * ata common packet structure
 */
#define	AP_ATAPI		0x001	/* device is atapi */
#define	AP_ERROR		0x002	/* normal error */
#define	AP_TRAN_ERROR		0x004	/* transport error */
#define	AP_READ			0x008	/* read data */
#define	AP_WRITE		0x010	/* write data */
#define	AP_ABORT		0x020	/* packet aborted */
#define	AP_TIMEOUT		0x040	/* packet timed out */
#define	AP_BUS_RESET		0x080	/* bus reset */
#define	AP_DEV_RESET		0x100		/* device reset */
#define	AP_POLL			0x200	/* polling packet */
#define	AP_ATAPI_OVERLAP	0x400	/* atapi overlap enabled */
#define	AP_FREE			0x1000	/* packet is free! */
#define	AP_FATAL		0x2000	/* There is a fatal error */
#define	AP_DMA			0x4000	/* DMA operation required */

/*
 * (struct ata_pkt *) to (gcmd_t *)
 */
#define	APKT2GCMD(apktp)	(&apktp->ap_gcmd)

/*
 * (gcmd_t *) to (struct ata_pkt *)
 */
#define	GCMD2APKT(gcmdp)	((struct ata_pkt  *)gcmdp->cmd_private)

/*
 * (gtgt_t *) to (struct ata_controller *)
 */
#define	GTGTP2ATAP(gtgtp)	((struct ata_controller *)GTGTP2HBA(gtgtp))

/*
 * (gtgt_t *) to (struct ata_drive *)
 */
#define	GTGTP2ATADRVP(gtgtp)	((struct ata_drive *)GTGTP2TARGET(gtgtp))

/*
 * (struct ata_pkt *) to (struct ata_drive *)
 */
#define	APKT2DRV(apktp)		(GTGTP2ATADRVP(GCMDP2GTGTP(APKT2GCMD(apktp))))


/*
 * (struct hba_tran *) to (struct ata_controller *)
 */
#define	TRAN2ATAP(tranp)	((struct ata_controller *)TRAN2HBA(tranp))


#define	ADDR2CTLP(ap)		((struct ata_controller *) \
					TRAN2CTL(ADDR2TRAN(ap)))

#define	PKT2APKT(pkt)		(GCMD2APKT(PKTP2GCMDP(pkt)))

struct ata_pkt {
	gcmd_t			ap_gcmd;	/* GHD command struct */

	uint32_t		ap_flags;	/* packet flags */
	caddr_t			ap_v_addr;	/* I/O buffer address */


	/* preset values for task file registers */
	int			ap_chno;
	int			ap_targ;
	uchar_t			ap_sec;
	uchar_t			ap_count;
	uchar_t			ap_lwcyl;
	uchar_t			ap_hicyl;
	uchar_t			ap_hd;
	uchar_t			ap_cmd;

	/* saved status and error registers for error case */

	uchar_t			ap_status;
	uchar_t			ap_error;

	uint32_t		ap_addr;
	size_t			ap_cnt;

	/* disk/atapi callback routines */

	int			(*ap_start)(struct ata_controller *ata_ctlp,
					struct ata_pkt *ata_pktp);
	int			(*ap_intr)(struct ata_controller *ata_ctlp,
					struct ata_pkt *ata_pktp);
	void			(*ap_complete)(struct ata_pkt *ata_pktp,
					int do_callback);

	/* Used by disk side */

	char			ap_cdb;		/* disk command */
	char			ap_scb;		/* status after disk cmd */
	int32_t			ap_bytes_per_block; /* blk mode factor */

	/* Used by atapi side */

	uchar_t			ap_cdb_len;  /* length of SCSI CDB (in bytes) */
	uchar_t			ap_cdb_pad;	/* padding after SCSI CDB */
						/* (in shorts) */

	uint_t			ap_count_bytes;	/* Indicates the */
						/* count bytes for xfer */
	caddr_t			ap_buf_addr;	/* Buffer for unaligned I/O */
	caddr_t			ap_orig_addr;	/* Copy of b_addr field of bp */
};

typedef struct ata_pkt ata_pkt_t;

/*
 * debugging
 */
#define	ADBG_FLAG_ERROR		0x0001
#define	ADBG_FLAG_WARN		0x0002
#define	ADBG_FLAG_TRACE		0x0004
#define	ADBG_FLAG_INIT		0x0008
#define	ADBG_FLAG_TRANSPORT	0x0010

#ifdef	ATA_DEBUG
extern int ata_debug;
/*PRINTFLIKE1*/
void ata_err(char *fmt, ...);
#define	ADBG_FLAG_CHK(flag, fmt)	if (ata_debug & (flag)) {	\
						ata_err fmt;		\
					}
#else	/* !ATA_DEBUG */
#define	ADBG_FLAG_CHK(flag, fmt)
#endif	/* !ATA_DEBUG */


/*
 * Always print "real" error messages on non-debugging kernels
 */
#ifdef	ATA_DEBUG
#define	ADBG_ERROR(fmt)		ADBG_FLAG_CHK(ADBG_FLAG_ERROR, fmt)
#else
#define	ADBG_ERROR(fmt)	ghd_err fmt
#endif

/*
 * ... everything else is conditional on the ATA_DEBUG preprocessor symbol
 */
#define	ADBG_WARN(fmt)		ADBG_FLAG_CHK(ADBG_FLAG_WARN, fmt)
#define	ADBG_TRACE(fmt)		ADBG_FLAG_CHK(ADBG_FLAG_TRACE, fmt)
#define	ADBG_INIT(fmt)		ADBG_FLAG_CHK(ADBG_FLAG_INIT, fmt)
#define	ADBG_TRANSPORT(fmt)	ADBG_FLAG_CHK(ADBG_FLAG_TRANSPORT, fmt)

/*
 * public function prototypes
 */
int ata_wait(ddi_acc_handle_t handle, uint8_t  *port, ushort_t onbits,
    ushort_t offbits, ushort_t errbits, int usec_delay, int iterations);

int ata_set_feature(struct ata_drive *ata_drvp, uchar_t feature, uchar_t value);
void ata_write_config(struct ata_drive *ata_drvp);
void ata_write_config1(struct ata_drive *ata_drvp);

#ifdef	__cplusplus
}
#endif

#endif /* _ATA_COMMON_H */

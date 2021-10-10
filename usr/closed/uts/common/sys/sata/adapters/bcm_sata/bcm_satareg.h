/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file may contain confidential information of Broadcom
 * Semiconductor, and should not be distributed in source form
 * without approval from Sun Legal.
 */


#ifndef _BCM_SATAREG_H
#define	_BCM_SATAREG_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * vendor id and device id for ht1000 sata controller
 */
#define	BCM_VENDOR_ID	0x1166
#define	BCM_DEVICE_ID	0x24a

/* Number of ports supported by the controller */
#define	BCM_NUM_CPORTS		4

/*
 *  QDMA ring depth. It has to be bigger than 32(NCQ slots) when NCQ
 * is enabled.
 */
#define	BCM_CTL_QUEUE_DEPTH	16

/*
 * initial value for port's QPI and QCI
 */
#define	BCM_IDX_INIT_VALUE	0x0

/*
 * command descriptor size
 */
#define	BCM_CMD_DESCRIPTOR_SIZE	32
#define	BCM_CMDQ_BUFFER_SIZE	(BCM_CTL_QUEUE_DEPTH) * \
	BCM_CMD_DESCRIPTOR_SIZE

/*
 * The default value of s/g entrie is 257, at least 1MB (4KB/pg * 256) + 1
 * if misaligned.
 */
#define	BCM_PRDT_NUMBER	257

#define	BCM_BM_64K_BOUNDARY	0xffffull

/*
 * Command Descriptor Queue Base Address masks
 */
#define	BCM_CMD_QUEUE_BASE_ADDR_MASK_LOW	0xffffffffull
#define	BCM_CMD_QUEUE_BASE_ADDR_MASK_HIGH	0x7fffull


/* PCI header offset for ht1000 Base Address */
#define	BCM_PCI_RNUM		0x24

/* bcm_mop_commands flag */
#define	BCM_SATA_RESET_ALL	0
#define	BCM_SATA_ABORT_ALL	1
#define	BCM_SATA_ABORT_ONE	2

/* bcm_mop_commands slot assignment */

/* indicate no "all" operation */
#define	BCM_NO_ALL_OP		-1

/* indicate non qdma cmd */
#define	BCM_NON_QDMA_SLOT_NO	-2

/*
 * values to indicate no failed slot, timout slot, aborted slot,
 * and reset slot
 */
#define	BCM_NO_FAILED_SLOT	-1
#define	BCM_NO_TIMEOUT_SLOT	-1
#define	BCM_NO_ABORTED_SLOT	-1
#define	BCM_NO_RESET_SLOT	-1

/*
 * values to indicate the events for event handler
 */
#define	BCM_PORT_RESET_EVENT	0x0
#define	BCM_HOT_INSERT_EVENT	0x1
#define	BCM_HOT_REMOVE_EVENT	0x2

/*
 * global HBA registers definitions
 */
#define	BCM_GLOBAL_OFFSET(bcm_ctlp)	(bcm_ctlp->bcmc_bar_addr)
	/* QDMA global control register */
#define	BCM_QDMA_GCR(bcm_ctlp)	(BCM_GLOBAL_OFFSET(bcm_ctlp) + 0x1000)
	/* QDMA global status register */
#define	BCM_QDMA_GSR(bcm_ctlp)	(BCM_GLOBAL_OFFSET(bcm_ctlp) + 0x1004)
	/* MSI acknowledge register */
#define	BCM_MSI_AR(bcm_ctlp)	(BCM_GLOBAL_OFFSET(bcm_ctlp) + 0x1010)
	/* QDMA global interrupt timing register */
#define	BCM_QDMA_GITR(bcm_ctlp)	(BCM_GLOBAL_OFFSET(bcm_ctlp) + 0x1014)
	/* QDMA global interrupt mask register */
#define	BCM_QDMA_GIMR(bcm_ctlp)	(BCM_GLOBAL_OFFSET(bcm_ctlp) + 0x1018)

/*
 * bit fields for global interrupt mask register
 */
#define	BCM_GIMR_PORT0	(0x00000001)		/* mask for port 0 */
#define	BCM_GIMR_PORT1	(0x00000001 << 1)	/* mask for port 1 */
#define	BCM_GIMR_PORT2	(0x00000001 << 2)	/* mask for port 2 */
#define	BCM_GIMR_PORT3	(0x00000001 << 3)	/* mask for port 3 */
#define	BCM_GIMR_ALL	(BCM_GIMR_PORT0 | BCM_GIMR_PORT1 | BCM_GIMR_PORT2 \
			| BCM_GIMR_PORT3)	/* mask for all ports */

/*
 * bit fields for global control register
 */
#define	BCM_GCR_INT_COALESCE_EN	(0x00000001 << 1) /* int coalescing enable */

/*
 * initial value for interrupt coalescing counter
 */
#define	BCM_ICC_INIT_VALUE	0xffffffff

/*
 * bit fields for MSI Acknowledge register
 */
#define	BCM_MSI_AR_CLEAR_BIT0	(0x00000001 << 0)	/* clear bit 0 */
#define	BCM_MSI_AR_CLEAR_BIT1	(0x00000001 << 1)	/* clear bit 1 */

/* Device signatures */
#define	BCM_SIGNATURE_ATAPI		0xeb140101
#define	BCM_SIGNATURE_DISK		0x00000101
#define	BCM_SIGNATURE_PM		0x96690101
#define	BCM_SIGNATURE_NOTREADY		0x00000000

/* some descriptor field values for ATAPI command */
#define	BCM_SATA_ATAPI_DEV_VAL	0xa0
#define	BCM_SATA_ATAPI_FEAT_VAL 0x1

/*
 * bit fields for QDMA control register(QCR)
 */
#define	BCM_QDMA_QCR_ENABLE	(0x00000001 << 0)	/* QDMA ENABLE */
#define	BCM_QDMA_QCR_PAUSE	(0x00000001 << 1)	/* QDMA PAUSE */

/*
 * bit fields for QDMA status(QSR) and interrupt mask register(QMR)
 */
#define	BCM_QDMA_CMD_CPLT	(0x00000001 << 0) /* QDMA command done */
#define	BCM_QDMA_PAUSE_ACK	(0x00000001 << 1) /* QDMA pause acknowledge */
#define	BCM_QDMA_ABORT_ACK	(0x00000001 << 2) /* abort acknowledge */
#define	BCM_QDMA_RESET_ACK	(0x00000001 << 3) /* port reset acknowledge */
#define	BCM_QDMA_HOTPLUG	(0x00000001 << 4) /* device insertion/removal */
#define	BCM_QDMA_CMD_ERR	(0x00000001 << 5) /* error detedted(CMDERR) */
#define	BCM_QDMA_DISABLE_ALL	(0x0)		  /* disable qdma interrupts */
#define	BCM_QSR_CLEAR_ALL	(0xffffffff)

#define	BCM_QDMA_SATAIF_ERR	(0x00000001 << 6)  /* sata interface error */
#define	BCM_QDMA_PCIBUS_ERR	(0x00000001 << 16) /* pci bus master error */
#define	BCM_QDMA_ATACMD_ERR	(0x00000001 << 17) /* ATA command error */
/* PRD deficient length error */
#define	BCM_QDMA_PRDDFL_ERR	(0x00000001 << 18)
#define	BCM_QDMA_PRDECL_ERR	(0x00000001 << 19) /* PRD excess length error */
#define	BCM_QDMA_DATACRC_ERR	(0x00000001 << 20) /* data CRC error */
#define	BCM_QDMA_PCIMSABORT_ERR	(0x00000001 << 21) /* pci master abort */

/*
 * bit fields for SATA error register(SER) and interrupt mask register(SIMR)
 */
#define	BCM_SER_PHY_RDY_CHG	(1 << 16) /* PHY state change */
#define	BCM_SER_PHY_INT_ERR	(1 << 17) /* PHY internal err */
#define	BCM_SER_COMM_WAKE	(1 << 18) /* COM wake */
#define	BCM_SER_10B_TO_8B_ERR	(1 << 19) /* 10B-to-8B decode */
#define	BCM_SER_DISPARITY_ERR	(1 << 20) /* disparity err */
#define	BCM_SER_CRC_ERR		(1 << 21) /* CRC err */
#define	BCM_SER_HANDSHAKE_ERR	(1 << 22) /* Handshake err */
#define	BCM_SER_LINK_SEQ_ERR	(1 << 23) /* Link seq err */
#define	BCM_SER_TRANS_ERR	(1 << 24) /* Tran state err */
#define	BCM_SER_FIS_TYPE	(1 << 25) /* FIS type err */
#define	BCM_SER_EXCHANGED	(1 << 26) /* Device exchanged */
#define	BCM_SER_CLEAR_ALL	(0xffffffff) /* clear serror register */
#define	BCM_SIMR_DISABLE_ALL	(0x0) /* disable interrupts */
#define	BCM_SIMR_ENABLE_ALL	(0x3ff0000) /* enable interrupts */

/* port task file data bits */
#define	BCM_TFD_STS_BSY		(0x1 << 7)
#define	BCM_TFD_STS_DRQ		(0x1 << 3)
#define	BCM_TFD_CTL_SRST		4

/* device control register */
#define	BCM_TFD_DC_D3		0x08    /* mysterious bit */
/* high order byte to read 48-bit values */
#define	BCM_TFD_DC_HOB		0x80

/* per port registers offset */
#define	BCM_PORT_OFFSET(bcm_ctlp, port)				\
		(bcm_ctlp->bcmc_bar_addr + (port * 0x100))

/* port offsets for taskfile registers */
	/* data register */
#define	BCM_DATA(bcm_ctlp, port)					\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x00)
	/* error register (read) */
#define	BCM_ERROR(bcm_ctlp, port)					\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x04)
	/* features (write) */
#define	BCM_FEATURE(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x04)
	/* sector count */
#define	BCM_COUNT(bcm_ctlp, port)					\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x08)
	/* sector number */
#define	BCM_SECT(bcm_ctlp, port)					\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x0c)
	/* cylinder low byte */
#define	BCM_LCYL(bcm_ctlp, port)					\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x10)
	/* cylinder high byte */
#define	BCM_HCYL(bcm_ctlp, port)					\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x14)
	/* drive/head register */
#define	BCM_DRVHD(bcm_ctlp, port)					\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x18)
	/* status/command register */
#define	BCM_STATUS(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x1c)
	/* status/command register */
#define	BCM_CMD(bcm_ctlp, port)					\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x1c)
	/* alternate status (read) */
#define	BCM_ALTSTATUS(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x20)
	/* device control (write) */
#define	BCM_DEVCTL(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x20)
	/* bus master command */
#define	BCM_BMCMD(bcm_ctlp, port)					\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x30)
	/* bus master status */
#define	BCM_BMSTATUS(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x31)
	/* bus master PRD table base address register */
#define	BCM_BMDBAR(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x34)

/* port offsets for sata registers */
	/* sata status register(SCR0) */
#define	BCM_SATA_STATUS(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x40)
	/* sata error register(SCR1) */
#define	BCM_SATA_ERROR(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x44)
	/* sata control register(SCR2) */
#define	BCM_SATA_CONTROL(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x48)
	/* sata active register(SCR3) */
#define	BCM_SATA_ACTIVE(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x4c)
	/* sata interrupt mask register(SIMR) */
#define	BCM_SATA_SIMR(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0x88)

/* port offsets for QDMA registers */
	/* QDMA start address lower(QAL) */
#define	BCM_QDMA_QAL(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0xa0)
	/* QDMA start address upper(QAU) */
#define	BCM_QDMA_QAU(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0xa4)
	/* QDMA producer index(QPI) */
#define	BCM_QDMA_QPI(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0xa8)
	/* QDMA consumer index(QCI) */
#define	BCM_QDMA_QCI(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0xac)
	/* QDMA control register(QCR) */
#define	BCM_QDMA_QCR(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0xb0)
	/* QDMA queue depth register(QDR) */
#define	BCM_QDMA_QDR(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0xb4)
	/* QDMA status register(QSR) */
#define	BCM_QDMA_QSR(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0xb8)
	/* QDMA interrupt mask register(QMR) */
#define	BCM_QDMA_QMR(bcm_ctlp, port)				\
			(BCM_PORT_OFFSET(bcm_ctlp, port) + 0xbc)

/*
 * structure for a single entry in the PRD table
 * (physical region descriptor table)
 */
typedef struct bcm_prde {
	/* offset 0x00 */
	uint32_t bcmprde_baddrlo;
#define	SET_BCPRDE_BADDRLO(bcmprde, baddrlo)			\
	(bcmprde.bcmprde_baddrlo = (baddrlo & 0xffffffff))

	/* offset 0x04 */
	uint32_t bcmprde_length_baddrhi_eot;
#define	SET_BCPRDE_LENGTH(bcmprde, length)			\
	(bcmprde.bcmprde_length_baddrhi_eot |=			\
		(length & 0xffff))

#define	SET_BCPRDE_BADDRHI(bcmprde, baddrhi)			\
	(bcmprde.bcmprde_length_baddrhi_eot =			\
		((baddrhi & 0x7fff) << 16))

#define	SET_BCPRDE_EOT(bcmprde, eot)				\
	(bcmprde.bcmprde_length_baddrhi_eot |=			\
		((eot & 0x1) << 31))

} bcm_prde_t;

/*
 * bit fields for comand control flags for cmd descriptor
 */
#define	BCM_CMD_FLAGS_EIN	0x01
#define	BCM_CMD_FLAGS_DIR_READ	0x02
#define	BCM_CMD_FLAGS_QUE_NCQ	0x04
#define	BCM_CMD_FLAGS_SKIP	0x10
#define	BCM_CMD_FLAGS_ATAPI	0x40
#define	BCM_CMD_FLAGS_PIO	0x80

/*
 * D bit for cmd descriptor
 */
#define	BCM_CMD_DBIT_PRD	0x0
#define	BCM_CMD_DBIT_NO_PRD	0x1

/*
 * command descriptor in non-ncq mode
 */
typedef struct bcm_cmd_descriptor {
	/* offset 0x00 */
	uint32_t bcmcd_dtype_cflags_pmp_rsvd;

#define	SET_BCMCD_DTYPE(bcmcd, dtype)				\
	(bcmcd->bcmcd_dtype_cflags_pmp_rsvd |= (dtype & 0xff))

#define	SET_BCMCD_CFLAGS(bcmcd, cflags)				\
	(bcmcd->bcmcd_dtype_cflags_pmp_rsvd |= 			\
		((cflags & 0xff) << 8))
#define	GET_BCMCD_CFLAGS(bcmcd)					\
	((bcmcd->bcmcd_dtype_cflags_pmp_rsvd >> 8) & 0xff)

#define	SET_BCMCD_PMP(bcmcd, pmp)				\
	(bcmcd->bcmcd_dtype_cflags_pmp_rsvd |=			\
		((pmp & 0xf) << 16))

	/* offset 0x04 */
	uint32_t bcmcd_rsvd1;

	/* offset 0x08 */
	uint32_t bcmcd_dbit_rbit_prdtlo;

#define	SET_BCMCD_DBIT(bcmcd, dbit)				\
	(bcmcd->bcmcd_dbit_rbit_prdtlo |= (dbit & 0x1))

#define	GET_BCMCD_PRDTLO(bcmcd)					\
	(bcmcd->bcmcd_dbit_rbit_prdtlo & 0xfffffffc)

#define	SET_BCMCD_PRDTLO(bcmcd, prdtlo)				\
	(bcmcd->bcmcd_dbit_rbit_prdtlo |=			\
		(prdtlo & 0xffffffff))

	/* offset 0x0c */
	uint32_t bcmcd_prdthi_rsvd;

#define	GET_BCMCD_PRDTHI(bcmcd)					\
	(bcmcd->bcmcd_prdthi_rsvd & 0xffff)

#define	SET_BCMCD_PRDTHI(bcmcd, prdthi)				\
	(bcmcd->bcmcd_prdthi_rsvd |=				\
		(prdthi & 0xffff))

	/* used for ATAPI command */
#define	GET_BCMCD_PRDCOUNT(bcmcd)				\
	((bcmcd->bcmcd_prdthi_rsvd >> 16) & 0xffff)

#define	SET_BCMCD_PRDCOUNT(bcmcd, prdcount)			\
	(bcmcd->bcmcd_prdthi_rsvd |=				\
		((prdcount & 0xffff) << 16))

	/* offset 0x10 */
	uint32_t bcmcd_scmd_devhead_features;

#define	GET_BCMCD_SCMD(bcmcd)					\
	(bcmcd->bcmcd_scmd_devhead_features & 0xff)

#define	SET_BCMCD_SCMD(bcmcd, scmd)				\
	(bcmcd->bcmcd_scmd_devhead_features |=			\
		(scmd & 0xff))

#define	GET_BCMCD_DEVHEAD(bcmcd)				\
	((bcmcd->bcmcd_scmd_devhead_features >> 8) & 0xff)

#define	SET_BCMCD_DEVHEAD(bcmcd, devhead)			\
	(bcmcd->bcmcd_scmd_devhead_features |= ((devhead & 0xff) << 8))

#define	GET_BCMCD_FEATURES(bcmcd)				\
	((bcmcd->bcmcd_scmd_devhead_features >> 16) & 0xff)

#define	SET_BCMCD_FEATURES(bcmcd, features)			\
	(bcmcd->bcmcd_scmd_devhead_features |=			\
		((features & 0xff) << 16))

#define	GET_BCMCD_FEATURESEXT(bcmcd)				\
	((bcmcd->bcmcd_scmd_devhead_features >> 24) & 0xff)

#define	SET_BCMCD_FEATURESEXT(bcmcd, featuresext)		\
	(bcmcd->bcmcd_scmd_devhead_features |=			\
		((featuresext & 0xff) << 24))

	/* offset 0x14 */
	uint32_t bcmcd_sector_cyllow_cylhi_sectorext;

#define	GET_BCMCD_SECTOR(bcmcd)					\
	(bcmcd->bcmcd_sector_cyllow_cylhi_sectorext & 0xff)

#define	SET_BCMCD_SECTOR(bcmcd, sector)				\
	(bcmcd->bcmcd_sector_cyllow_cylhi_sectorext |= ((sector & 0xff)))

#define	GET_BCMCD_CYLLOW(bcmcd)					\
	((bcmcd->bcmcd_sector_cyllow_cylhi_sectorext >> 8) & 0xff)

#define	SET_BCMCD_CYLLOW(bcmcd, cyllow)				\
	(bcmcd->bcmcd_sector_cyllow_cylhi_sectorext |= ((cyllow & 0xff) << 8))

#define	GET_BCMCD_CYLHI(bcmcd)					\
	((bcmcd->bcmcd_sector_cyllow_cylhi_sectorext >> 16) & 0xff)

#define	SET_BCMCD_CYLHI(bcmcd, cylhi)				\
	(bcmcd->bcmcd_sector_cyllow_cylhi_sectorext |= ((cylhi & 0xff) << 16))

#define	GET_BCMCD_SECTOREXT(bcmcd)				\
	((bcmcd->bcmcd_sector_cyllow_cylhi_sectorext >> 24) & 0xff)

#define	SET_BCMCD_SECTOREXT(bcmcd, sectorext)			\
	(bcmcd->bcmcd_sector_cyllow_cylhi_sectorext |=		\
		((sectorext & 0xff) << 24))

	/* offset 0x18 */
	uint32_t bcmcd_cyllowext_cylhiext_sectorcnt;

#define	GET_BCMCD_CYLLOWEXT(bcmcd)				\
	(bcmcd->bcmcd_cyllowext_cylhiext_sectorcnt & 0xff)

#define	SET_BCMCD_CYLLOWEXT(bcmcd, cyllowext)			\
	(bcmcd->bcmcd_cyllowext_cylhiext_sectorcnt |= (cyllowext & 0xff))

#define	GET_BCMCD_CYLHIEXT(bcmcd)				\
	((bcmcd->bcmcd_cyllowext_cylhiext_sectorcnt >> 8) & 0xff)

#define	SET_BCMCD_CYLHIEXT(bcmcd, cylhiext)			\
	(bcmcd->bcmcd_cyllowext_cylhiext_sectorcnt |=		\
		((cylhiext & 0xff) << 8))

#define	GET_BCMCD_SECTOR_COUNT(bcmcd)				\
	((bcmcd->bcmcd_cyllowext_cylhiext_sectorcnt >> 16) & 0xff)

#define	SET_BCMCD_SECTOR_COUNT(bcmcd, sectorcnt)		\
	(bcmcd->bcmcd_cyllowext_cylhiext_sectorcnt |=		\
		((sectorcnt & 0xff) << 16))

#define	GET_BCMCD_SECTOR_COUNTEXT(bcmcd)			\
	((bcmcd->bcmcd_cyllowext_cylhiext_sectorcnt >> 24) & 0xff)

#define	SET_BCMCD_SECTOR_COUNTEXT(bcmcd, sectorcntext)		\
	(bcmcd->bcmcd_cyllowext_cylhiext_sectorcnt |=		\
		((sectorcntext & 0xff) << 24))

	/* offset 0x1c */
	uint32_t bcmcd_rsvd2;
} bcm_cmd_descriptor_t;

#ifdef	__cplusplus
}
#endif

#endif /* _BCM_SATAREG_H */

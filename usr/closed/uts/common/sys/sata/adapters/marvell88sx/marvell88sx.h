/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file may contain confidential information of Marvell
 * Semiconductor, and should not be distributed in source form
 * without approval from Sun Legal.
 */

#ifndef _MARVELL88SX_H
#define	_MARVELL88SX_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Supported Marvell chips (pci id)
 */
#define	MV_SATA_88SX5040	0x5040
#define	MV_SATA_88SX5041	0x5041
#define	MV_SATA_88SX5080	0x5080
#define	MV_SATA_88SX5081	0x5081
#define	MV_SATA_88SX6040	0x6040
#define	MV_SATA_88SX6041	0x6041
#define	MV_SATA_88SX6080	0x6080
#define	MV_SATA_88SX6081	0x6081

/* Marvell controller model types */
enum mv_models {MV_MODEL_UNKNOWN, MV_MODEL_50XX, MV_MODEL_60XX};

/* Define which register is interesting */
#define	MARVELL_BASE_REG	1	/* BAR0 is the only useful register */

/*
 * Controllers Base Address Offset
 *
 */
#define	SATAHC0_BASE_OFFSET 	0x20000 /* SATAHC0 regs base */
#define	SATAHC1_BASE_OFFSET 	0x30000 /* SATAHC1 regs base */
#define	SATAHC_REGS_LEN 	0x24	/* SATAHC regs range length */

#define	MV_FIRST_SUBCTRLR		0
#define	MV_SECOND_SUBCTRLR		1
#define	MV_MAX_NUM_SUBCTRLR		2 /* Max # of controllers per chip */
#define	MV_NUM_PORTS_PER_SUBCTRLR 	4 /* Number of ports per controller */

/*
 * SATA Host Controller Registers Offsets
 *
 */
#define	SATAHC_REQUEST_Q_OUT_PTR_OFFSET	1	/* Req Q outptr */
#define	SATAHC_RESPONSE_Q_IN_PTR_OFFSET 2	/* Res Q inprt */
#define	SATAHC_INTRC_THRESHOLD_OFFSET	3	/* intr coalescing thrshld */
#define	SATAHC_INTRT_THRESHOLD_OFFSET	4	/* intr time threshold */
#define	SATAHC_INTR_CAUSE_OFFSET	5	/* interrupt cause offset */
#define	SATAHC_BRIDGES_TEST_CTL_OFFSET	6	/* bridge test ctl */
#define	SATAHC_BRIDG_TEST_STATUS_OFFSET	7	/* bridge test status */
#define	SATAHC_BRIDG_PINS_CONFIG_OFFSET	8	/* bridge pins config */

/* SATAHC Response Queue In Pointer values */
#define	SATAHC_RESP_Q_PORT_SHIFT	8
#define	SATAHC_RESP_Q_PORT_MASK		0x1f

/* SATAHC Interrupt Cause Register values */
#define	SATAHC_INTR_CAUSE_MASK		0xf1f
#define	SATAHC_CRPB_DONE_SHIFT		0
#define	SATAHC_CRPB_DONE_MASK		(0xf << SATAHC_CRPB_DONE_SHIFT)
#define	SATAHC_DEV_INTR_SHIFT		8
#define	SATAHC_DEV_INTR_MASK		(0xf << SATAHC_DEV_INTR_SHIFT)
#define	SATAHC_CRPB_COAL_SHIFT		4

/* SATAHC Bridges Test Control Register values (50XX only) */
#define	SATAHC_BRIDGES_TEST_CTL_PORT_PHY_SHUTDOWN_MASK	(0xf << 24)
#define	SATAHC_BRIDGES_TEST_CTL_PORT_PHY_SHUTDOWN_BIT(port) (1 << (port + 24))

/*
 * SATA to PATA Bridge Control and Status Registers
 */

/*
 * SATA Bridge Port 0-3 Internal Registers
 */
/*
 * Status Bridge Port N Register
 * BAR (REG0) + SATAHCN_BASE_OFFSET + (SATA_BRIDGE_PORT_OFFSET * port [1-4])
 * + SATA_BRIDGE_REGISTER_OFFSET
 */
#define	SATA_BRIDGE_PORT_OFFSET		0x100

/*
 * S-ERROR, S-Control, PHY Control, Control and PHY Mode bridge port x
 * register offset
 */

#define	SATA_STATUS_BRIDGE_OFFSET	0	/* S-Status bridge port */
#define	SATA_SERROR_BRIDGE_OFFSET	1	/* S-Error bridge port */
#define	SATA_SCONTROL_BRIDGE_OFFSET	2	/* S-Control Bridge Port */
#define	SATA_PHY_CONTROL_BRIDGE_OFFSET	3	/* PHY Control Bridge Port */
#define	SATA_MAGIC_BRIDGE_OFFSET	12	/* Magic bridge register */
#define	SATA_CONTROL_BRIDGE_OFFSET	15	/* Control Bridge Port */
#define	SATA_PHY_MODE_BRIDGE_OFFSET	29	/* PHY Mode Bridge Port */

#define	SATA_BRIDGE_REGS_LEN		0x78	/* Covers all the bridge regs */


/*
 * Status Bridge port x Register fields
 */
#define	STATUS_BRIDGE_PORT_DET_SHIFT		0
#define	STATUS_BRIDGE_PORT_SPD_SHIFT		4
#define	STATUS_BRIDGE_PORT_IPM_SHIFT		8

#define	STATUS_BRIDGE_PORT_DET		(0xf << STATUS_BRIDGE_PORT_DET_SHIFT)
#define	STATUS_BRIDGE_PORT_SPD		(0xf << STATUS_BRIDGE_PORT_SPD_SHIFT)
#define	STATUS_BRIDGE_PORT_IPM		(0xf << STATUS_BRIDGE_PORT_IPM_SHIFT)

/*
 * Status Bridge port x register fields value
 */
#define	STATUS_BRIDGE_PORT_DET_NODEV		0 	/* No dev detected */
#define	STATUS_BRIDGE_PORT_DET_DEVPRE_NOPHYCOM	1 	/* dev detected */
							/* no PHY */
							/* communication */
							/* detected */
#define	STATUS_BRIDGE_PORT_DET_DEVPRE_PHYCOM	3	/* dev detected */
							/* PHY communication */
							/* established */
#define	STATUS_BRIDGE_PORT_DET_PHYOFFLINE	4	/* PHY is in offline */
							/* mode */

/*
 * S-Error Bridge port x register fields
 */
#define	SERROR_BRIDGEPORT_DATA_ERR_FIXED	(1 << 0) /* D integrity err */
#define	SERROR_BRIDGEPORT_COMM_ERR_FIXED	(1 << 1) /* comm err recov */
#define	SERROR_BRIDGEPORT_DATA_ERR		(1 << 8) /* D integrity err */
#define	SERROR_BRIDGEPORT_PERSISTENT_ERR	(1 << 9)  /* norecov com err */
#define	SERROR_BRIDGEPORT_PROTOCOL_ERR		(1 << 10) /* protocol err */
#define	SERROR_BRIDGEPORT_INT_ERR		(1 << 11) /* internal err */
#define	SERROR_BRIDGEPORT_PHY_RDY_CHG		(1 << 16) /* PHY state change */
#define	SERROR_BRIDGEPORT_PHY_INT_ERR		(1 << 17) /* PHY internal err */
#define	SERROR_BRIDGEPORT_COMM_WAKE		(1 << 18) /* COM wake */
#define	SERROR_BRIDGEPORT_10B_TO_8B_ERR		(1 << 19) /* 10B-to-8B decode */
#define	SERROR_BRIDGEPORT_DISPARITY_ERR		(1 << 20) /* disparity err */
#define	SERROR_BRIDGEPORT_CRC_ERR		(1 << 21) /* CRC err */
#define	SERROR_BRIDGEPORT_HANDSHAKE_ERR		(1 << 22) /* Handshake err */
#define	SERROR_BRIDGEPORT_LINK_SEQ_ERR		(1 << 23) /* Link seq err */
#define	SERROR_BRIDGEPORT_TRANS_ERR		(1 << 24) /* Tran state err */
#define	SERROR_BRIDGEPORT_FIS_TYPE		(1 << 25) /* FIS type err */
#define	SERROR_BRIDGEPORT_EXCHANGED_ERR		(1 << 26) /* Device exchanged */

/*
 * S-Control Bridge port x register fields
 */
#define	SCONTROL_BRIDGE_PORT_DET_SHIFT		0
#define	SCONTROL_BRIDGE_PORT_SPD_SHIFT		4
#define	SCONTROL_BRIDGE_PORT_IPM_SHIFT		8
#define	SCONTROL_BRIDGE_PORT_SPM_SHIFT		12	/* Not on 50XX */

#define	SCONTROL_BRIDGE_PORT_DET	(0xf << STATUS_BRIDGE_PORT_DET_SHIFT)
#define	SCONTROL_BRIDGE_PORT_SPD	(0xf << STATUS_BRIDGE_PORT_SPD_SHIFT)
#define	SCONTROL_BRIDGE_PORT_IPM	(0xf << STATUS_BRIDGE_PORT_IPM_SHIFT)
#define	SCONTROL_BRDIGE_PORT_SPM	(0xf << STATUS_BRIDGE_PORT_SPM_SHIFT)

#define	SCONTROL_BRIDGE_PORT_DET_NOACTION	0	/* Do nothing to port */
#define	SCONTROL_BRIDGE_PORT_DET_INIT		1	/* Re-initialize port */
#define	SCONTROL_BRIDGE_PORT_DET_DISABLE	4	/* Disable port */

/* Gen 2 not available on 50XX controllers */
#define	SCONTROL_BRIDGE_PORT_SPD_NOLIMIT	0	/* No speed limits */
#define	SCONTROL_BRIDGE_PORT_SPD_GEN1		1	/* Limit Gen 1 rate */
#define	SCONTROL_BRIDGE_PORT_SPD_GEN2		2	/* Limit Gen 2 rate */

#define	SCONTROL_BRIDGE_PORT_IPM_NORESTRICT		0 /* No PM limits */
#define	SCONTROL_BRIDGE_PORT_IPM_DISABLE_PARTIAL	1 /* Disable partial */
#define	SCONTROL_BRIDGE_PORT_IPM_DISABLE_SLUMBER	2 /* Disable slumber */
#define	SCONTROL_BRIDGE_PORT_IPM_DISABLE_BOTH		3 /* Disable both */

/* The values below are not available to 50XX controllers */
#define	SCONTROL_BRIDGE_PORT_SPM_NORESTRICT		0 /* No PM limits */
#define	SCONTROL_BRIDGE_PORT_SPM_DO_PARTIAL		1 /* Go to partial */
#define	SCONTROL_BRIDGE_PORT_SPM_DO_SLUMBER		2 /* Go to slumber */
#define	SCONTROL_BRIDGE_PORT_SPM_DO_ACTIVE		4 /* Go to active */


/*
 * PHY control bridge port x register fields
 */
#define	PHY_CONTROL_BRIDGE_PORT_SQ_MASK		0x3
#define	PHY_CONTROL_BRIDGE_PORT_SQ_100MV	0
#define	PHY_CONTROL_BRIDGE_PORT_SQ_150MV	1
#define	PHY_CONTROL_BRIDGE_PORT_SQ_200MV	2
#define	PHY_CONTROL_BRIDGE_PORT_SQ_RESV		3

/*
 * PHY Mode Bridge Port register offset
 */
#define	PHY_MODE_BRIDGE_PORT_AMP_SHIFT	5
#define	PHY_MODE_BRIDGE_PORT_AMP	(7 << PHY_MODE_BRIDGE_PORT_AMP_SHIFT)
#define	PHY_MODE_BRIDGE_PORT_PRE_SHIFT	11
#define	PHY_MODE_BRIDGE_PORT_PRE	(3 << PHY_MODE_BRIDGE_PORT_PRE_SHIFT)

/*
 * EDMA
 */

#define	MV_NUM_ATA_CMD_REGS	11
#define	MV_QDEPTH		32
#define	CRPB_DONE_PORT_SHIFT	0
#define	CDIR_SYSTEM_TO_DEV	0
#define	CDIR_DEV_TO_SYSTEM	1
#define	CRQB_ID_SHIFT		1
#define	CRQB_ATA_CMD_REG(regnum, data, last) \
	(((last) << 15) | (1 << 12) | (0 << 11) | ((regnum) << 8) | \
			(unsigned char)(data))

/*
 * EDMA and the Serial-ATA interface Configuration Registers Offsets
 * Port register address
 */
#define	EDMA_BASE_OFFSET	0x2000	/* Base address of EDMA regs offset */

/*
 * EDMA register offsets
 */
#define	EDMA_CONFIG_OFFSET			0
#define	EDMA_TIMER_OFFSET			1
#define	EDMA_INTERRUPT_ERROR_CAUSE_OFFSET	2
#define	EDMA_INTERRUPT_ERROR_MASK_OFFSET	3
#define	EDMA_REQUEST_Q_BASE_HIGH_OFFSET		4
#define	EDMA_REQUEST_Q_IN_PTR_OFFSET		5
#define	EDMA_REQUEST_Q_OUT_PTR_OFFSET		6
#define	EDMA_RESPONSE_Q_BASE_HIGH_OFFSET	7
#define	EDMA_RESPONSE_Q_IN_PTR_OFFSET		8
#define	EDMA_RESPONSE_Q_OUT_PTR_OFFSET		9
#define	EDMA_COMMAND_OFFSET			10
#define	EDMA_TEST_CONTROL_OFFSET		11
#define	EDMA_STATUS_OFFSET			12
#define	EDMA_IORDY_TIME_OUT_OFFSET		13
#define	EDMA_ARBITER_CONFIG_OFFSET		14
#define	EDMA_INITIAL_ATA_CMD_OFFSET		15
#define	EDMA_CMD_DELAY_THRESHOLD_OFFSET		16
#define	SERIAL_ATA_INTER_CONFIG_OFFSET		20

#define	EDMA_50XX_LAST_OFFSET	EDMA_IORDY_TIME_OUT_OFFSET
#define	EDMA_60XX_LAST_OFFSET	SERIAL_ATA_INTER_CONFIG_OFFSET

#define	EDMA_REG_SIZE	4

#define	EDMA_REGS_50XX_LEN	((EDMA_50XX_LAST_OFFSET + 1) * EDMA_REG_SIZE)
#define	EDMA_REGS_60XX_LEN	((EDMA_60XX_LAST_OFFSET + 1) * EDMA_REG_SIZE)

#define	EDMA_REGS_LEN	EDMA_REGS_60XX_LEN	/* Just use the larger value */

#define	EDMA_TASK_FILE_OFFSET			0x0100	/* Dev task file */
#define	EDMA_TASK_FILE_LEN			0x20	/* Dev task file len */
#define	EDMA_TASK_FILE_CONTROL_STATUS_OFFSET	0x0120	/* Dev ctl&ALTstatus */
#define	EDMA_TASK_FILE_CONTROL_STATUS_LEN	0x4	/* Dev ctrl/stat len */

/*
 * Device Registers offsets from Task File Registers base address
 */
#define	AT_DATA		0	/* data register		*/
#define	AT_ERROR	1	/* error register (read)	*/
#define	AT_FEATURE	1	/* features (write)		*/
#define	AT_COUNT	2	/* sector count			*/
#define	AT_SECT		3	/* sector number		*/
#define	AT_LCYL		4	/* cylinder low byte    	*/
#define	AT_HCYL		5	/* cylinder high byte		*/
#define	AT_DRVHD	6	/* drive/head register		*/
#define	AT_STATUS	7	/* status/command register 	*/
#define	AT_CMD		7	/* status/command register 	*/

#define	AT_DEVCTL	0	/* Device control 		*/
#define	AT_ALTSTATUS	0	/* Alternate status 		*/

/*
 * EDMA configuration register fields
 */
#define	EDMA_CONFIG_DEV_Q_DEPTH_MASK		0x1F
#define	EDMA_CONFIG_DEV_Q_DEPTH_SHIFT		0
#define	EDMA_CONFIG_NCQ_ENABLED_MASK		(1 << 5)
#define	EDMA_CONFIG_NCQ_ENABLED_SHIFT		5
#define	EDMA_CONFIG_Q_ENABLED_MASK		(1 << 9)
#define	EDMA_CONFIG_Q_ENABLED_SHIFT		9
#define	EDMA_CONFIG_STOP_ON_ERROR_MASK		(1 << 10)
#define	EDMA_CONFIG_STOP_ON_ERROR_SHIFT		10
#define	EDMA_CONFIG_CONT_ON_DEV_ERR_MASK	(1 << 14)
#define	EDMA_CONFIG_CONT_ON_DEV_ERR_SHIFT	14

/*
 * EDMA Interrupt Cause Register Fields
 */
#define	EDMA_INTR_ERROR_CAUSE_UDMA_BUF_PARITY	(1 << 0) /* PRTY err data xfr */
#define	EDMA_INTR_ERROR_CAUSE_UDMA_PRD_PARITY	(1 << 1) /* PRTY err PRD ftch */
#define	EDMA_INTR_ERROR_CAUSE_DEVICE_ERR	(1 << 2) /* Dev reported  */
#define	EDMA_INTR_ERROR_CAUSE_DEVICE_DISCONNECT	(1 << 3) /* Dev disconnect */
#define	EDMA_INTR_ERROR_CAUSE_DEVICE_CONNECTED	(1 << 4) /* Dev reconnected */
#define	EDMA_INTR_ERROR_CAUSE_OVERRUN		(1 << 5) /* 50XX */
#define	EDMA_INTR_ERROR_CAUSE_SERROR		(1 << 5) /* 60XX */
#define	EDMA_INTR_ERROR_CAUSE_UNDERRUN		(1 << 6)
#define	EDMA_INTR_ERROR_CAUSE_SELF_DISABLE_60XX (1 << 7) /* 60XX */
#define	EDMA_INTR_ERROR_CAUSE_SELF_DISABLE_50XX	(1 << 8) /* 50XX */
#define	EDMA_INTR_ERROR_CAUSE_BIST_ASYNC_NOTIFY	(1 << 8) /* 60XX */
#define	EDMA_INTR_ERROR_CAUSE_CRQB_PARITY	(1 << 9)
#define	EDMA_INTR_ERROR_CAUSE_CRPB_PARITY	(1 << 10)
#define	EDMA_INTR_ERROR_CAUSE_INTERNAL_PARITY	(1 << 11)
#define	EDMA_INTR_ERROR_CAUSE_IORDY_TIMEOUT	(1 << 12)
#define	EDMA_INTR_ERROR_CAUSE_LINKCTL_RXERRS	(0xF << 13) /* 60XX */
#define	EDMA_INTR_ERROR_CAUSE_LINKDATA_RXERRS	(0xF << 17) /* 60XX */
#define	EDMA_INTR_ERROR_CAUSE_LINKCTL_TXERRS	(0x1F << 21) /* 60XX */
#define	EDMA_INTR_ERROR_CAUSE_LINKDATA_TXERRS	(0x1F << 26) /* 60XX */
#define	EDMA_INTR_ERROR_CAUSE_TRANSPROT_ERR	(1 << 31) /* 60XX */

/*
 * EDMA interrupt error mask register
 */
/* Should the below be ((0x7F << 0) | (0x1F << 8))? */
#define	EDMA_INTR_ERROR_MASK_50XX	((0x7F << 0) | (0x1F << 8))
#define	EDMA_INTR_ERROR_MASK_60XX	0x1FBF

/*
 * EDMA request queue in pointer register
 */
#define	EDMA_REQUEST_Q_IN_PTR_MASK		(0x1F << 5)
#define	EDMA_REQUEST_Q_IN_PTR_SHIFT		5
#define	EDMA_REQUEST_Q_BASE_ADDR_LOW_MASK	(0x3FFFFF << 10)
#define	EDMA_REQUEST_Q_BASE_ADDR_LOW_SHIFT	10

/*
 * EDMA request queue out pointer register
 */
#define	EDMA_REQUEST_Q_OUT_PTR_MASK		(0x1F << 5)
#define	EDMA_REQUEST_Q_OUT_PTR_SHIFT		5

/*
 * EDMA response queue in pointer register
 */
#define	EDMA_RESPONSE_Q_IN_PTR_MASK		(0x1F << 3)
#define	EDMA_RESPONSE_Q_IN_PTR_SHIFT		3

/*
 * EDMA_response queue out pointer register
 */
#define	EDMA_RESPONSE_Q_OUT_PTR_MASK		(0x1F << 3)
#define	EDMA_RESPONSE_Q_OUT_PTR_SHIFT		3
#define	EDMA_RESPONSE_Q_BASE_ADDR_LOW_MASK	(0xFFFFFF << 8)
#define	EDMA_RESPONSE_Q_BASE_ADDR_LOW_SHIFT	8

/*
 * EDMA command register (fields)
 */
#define	EDMA_COMMAND_ENABLE		(1 << 0) /* Enable EDMA */
#define	EDMA_COMMAND_DISABLE		(1 << 1) /* Disable EDMA */
#define	EDMA_COMMAND_ATA_DEVICE_RESET	(1 << 2) /* ATA Device hard reset */

/*
 * EDMA status register (fields)
 */
#define	EDMA_STATUS_TAG_MASK		(0x1F << 0)
#define	EDMA_STATUS_TAG_SHIFT		0
#define	EDMA_STATUS_DEVICE_DIR		(1 << 5)
#define	EDMA_STATUS_DEVICE_INTR_REQUEST	(1 << 6)
#define	EDMA_STATUS_DEVICE_DMA_REQUEST	(1 << 7)
#define	EDMA_STATUS_STATE_MASK		(0xFF << 8)
#define	EDMA_STATUS_STATE_SHIFT		8

/*
 * EDMA I/O ready timeout register (fields)
 */
#define	EDMA_IORDY_TIMEOUT_MASK		(0xFFFF << 0)
#define	EDMA_IORDY_TIMEOUT_SHIFT	0


/*
 *
 * Serial-ATA interface Configuration Register fields (60XX register)
 */
#define	PHY_PLL_REF_CLOCK_FREQ		(0x3 << 0)
#define	PHY_PLL_REF_CLOCK_DIV		(0x3 << 2)
#define	PHY_PLL_REF_CLOCK_FEEDBACK	(0x3 << 4)
#define	SSC_ENABLED			(1 << 6)
#define	GEN_2_COMM_SPD_SUPP		(1 << 7)
#define	PHY_COMM_ENABLE			(1 << 8)
#define	PHY_SHUTDOWN			(1 << 9)
#define	TARGET_MODE			(1 << 10)
#define	COMM_CH_OPE_MODE		(1 << 11)


/*
 * PCI Interface Registers
 */
#define	PCI_IO_BAR_REMAP_OFFSET			0x00F00
#define	PCI_EXPANSION_ROM_BAR_CONTROL_OFFSET	0x00D2C
#define	PCI_BAR2_CONTROL_OFFSET			0x00C08
#define	PCI_DLL_STATUS_CONTROL_OFFSET		0x01D20
#define	PCI_PADS_CALIBRATION_OFFSET		0x01D1C
#define	PCI_COMMAND_OFFSET			0x00C00
#define	PCI_MODE_OFFSET				0x00D00
#define	PCI_RETRY_OFFSET			0x00C04
#define	PCI_DISCARD_TIMER_OFFSET		0x00D04
#define	PCI_MSI_TRIGGER_TIMER_OFFSET		0x00C38
#define	PCI_INTERFACE_CROSSBAR_TIMEOUT_OFFSET	0x01D04
#define	PCI_INTERFACE_CONTROL_OFFSET		0x01D68
#define	PCI_MAIN_COMMAND_AND_STATUS_OFFSET	0x00D30
#define	PCI_CONFIGURATION_ADDRESS_OFFSET	0x00CF8
#define	PCI_CONFIGURATION_DATA_OFFSET		0x00CFC
#define	PCI_SERR_MASK_OFFSET			0x00C28

/*
 * PCI Interrupts and Error Report Registers
 */

#define	PCI_INTERRUPT_ERROR_OFFSET	0x01D40
#define	PCI_ERROR_ADDRESS_LOW_OFFSET	0
#define	PCI_ERROR_ADDRESS_HIGH_OFFSET	1
#define	PCI_ERROR_ATTRIBUTE_OFFSET	2
/* Nothing defined			3 */
#define	PCI_ERROR_COMMAND_OFFSET	4
/* Nothing defined			5 */
#define	PCI_INTERRUPT_CAUSE_OFFSET	6
#define	PCI_INTERRUPT_MASK_OFFSET	7

#define	PCI_INTERRUPT_ERROR_LEN		0x20

#define	PCI_INTERRUPT_DISABLED		0x7fffff
#define	PCI_INTERRUPT_ENABLED		0

#define	DEVICE_MAIN_INTR_OFFSET		0x01D60	/* Main intr cause & mask reg */
#define	DEVICE_MAIN_INTR_CAUSE_OFFSET	0	/* Dev main intr cause reg */
#define	DEVICE_MAIN_INTR_MASK_OFFSET	1	/* Dev main intr mask reg */

#define	DEVICE_MAIN_INTR_REGS_LEN	0x8 /* Covers INTR_CAUSE & INTR_MASK */

#define	MV_ENABLE_ALL_PCI_INTR_MASK	((1 << 23) - 1)
/*
 * Device Main Interrupt cause register.  This register is for all ports, 4
 * ports per controller (port #s 0-3) and 2 controllers per chip.
 * (In case of 504X only one controller, thus only bits 0-8 are valid,
 * the rest are reserved.)
 *
 * Port interrupt cause mask:
 * (((MAIN_SATAERR | MAIN_SATADONE) <<
 * (port_number * MAIN_PORT_SHIFT)) <<
 * (satahc_number * MAIN_SATAHC_BASE_SHIFT))
 */

#define	MAIN_PORT_SHIFT		2	/* 2 bits per port, Error and Done */
#define	MAIN_PORT_ERR_SHIFT	0	/* Used to extract the Error bit */
#define	MAIN_PORT_DONE_SHIFT	1	/* Used to extract the Done bit */
#define	MAIN_SATAHC_BASE_SHIFT 	9	/* Used to get to the second ctl */

#define	MAIN_SATAERR		(1 << MAIN_PORT_ERR_SHIFT)
#define	MAIN_SATADONE		(1 << MAIN_PORT_DONE_SHIFT)



#define	DEVICE_MAIN_INTR_CAUSE_SATA0ERR		0x00001	/* Port 0 Error */
#define	DEVICE_MAIN_INTR_CAUSE_SATA0DONE	0x00002	/* Port 0 Cmd Done */
#define	DEVICE_MAIN_INTR_CAUSE_SATA1ERR		0x00004	/* Port 1 Error */
#define	DEVICE_MAIN_INTR_CAUSE_SATA1DONE	0x00008	/* Port 1 Cmd Done */
#define	DEVICE_MAIN_INTR_CAUSE_SATA2ERR		0x00010	/* Port 2 Error */
#define	DEVICE_MAIN_INTR_CAUSE_SATA2DONE	0x00020	/* Port 2 Cmd Done */
#define	DEVICE_MAIN_INTR_CAUSE_SATA3ERR		0x00040	/* Port 3 Error */
#define	DEVICE_MAIN_INTR_CAUSE_SATA3DONE	0x00080	/* Port 3 Cmd Done */
#define	DEVICE_MAIN_INTR_CAUSE_SATA03COALSDONE	0x00100	/* Ports 0-3 */
							/* Coalescing Done */

#define	DEVICE_MAIN_INTR_CAUSE_SATA03_INTR	0x001ff	/* SATAHC0 intr */

#define	DEVICE_MAIN_INTR_CAUSE_SATA4ERR		0x00200	/* Port 4 Error */
#define	DEVICE_MAIN_INTR_CAUSE_SATA4DONE	0x00400	/* Port 4 Cmd Done */
#define	DEVICE_MAIN_INTR_CAUSE_SATA5ERR		0x00800	/* Port 5 Error */
#define	DEVICE_MAIN_INTR_CAUSE_SATA5DONE	0x01000	/* Port 5 Cmd Done */
#define	DEVICE_MAIN_INTR_CAUSE_SATA6ERR		0x02000	/* Port 6 Error */
#define	DEVICE_MAIN_INTR_CAUSE_SATA6DONE	0x04000	/* Port 6 Cmd Done */
#define	DEVICE_MAIN_INTR_CAUSE_SATA7ERR		0x08000	/* Port 7 Error */
#define	DEVICE_MAIN_INTR_CAUSE_SATA7DONE	0x10000	/* Port 7 Cmd Done */
#define	DEVICE_MAIN_INTR_CAUSE_SATA47COALSDONE	0X20000 /* Ports 4-7 */
							/* Coalescing Done */

#define	DEVICE_MAIN_INTR_CAUSE_SATA47_INTR	0x3fe00	/* SATAHC1intr */

#define	DEVICE_MAIN_INTR_CAUSE_PCI_ERR		0x40000	/* PCI Bus Error */
#define	DEVICE_MAIN_INTR_CAUSE_TRAN_LOW_DONE	0x80000 /* Trans low done */
#define	DEVICE_MAIN_INTR_CAUSE_TRAN_HIGH_DONE	0x100000 /* Trans high done */
#define	DEVICE_MAIN_INTR_CAUSE_SATA07COALSDONE	0x200000 /* Ports 0-7 */
							/* Coalescing Done */
#define	DEVICE_MAIN_INTR_CAUSE_GPIO_INTR	0x400000 /* GPIO interrupt */
#define	DEVICE_MAIN_INTR_CAUSE_SELF_INTR	0x800000 /* Self interrupt */
#define	DEVICE_MAIN_INTR_CAUSE_TWSI_INTR	0x1000000 /* TWSI interrupt */

enum sata_io_trans {trans_low, trans_high};

/*
 * 6000 series specific registers
 */

/*
 * SATAHC arbiter registers
 */


/*
 * Serial-ATA interface Registers
 *	BAR + SATAHCn + SATA_INTERFACE_BASE_OFFSET +
 *		(port #[1-4] * SATA_INTERFACE_PORT_MULTIPLIER) +
 *			SATA_INTERFACE_xxx_OFFSET
 */

#define	SATA_INTERFACE_BASE_OFFSET		0x300
#define	SATA_INTERFACE_PORT_MULTIPLIER		0x2000

#define	SATA_INTERFACE_REG_SIZE			4

#define	SATA_INTERFACE_SSTATUS_OFFSET		(0x00 / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_SERROR_OFFSET		(0x04 / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_SCONTROL_OFFSET		(0x08 / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_LTMODE_OFFSET		(0x0C / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_PHY_MODE_4_OFFSET	(0x14 / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_PHY_MODE_1_OFFSET	(0x2C / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_PHY_MODE_2_OFFSET	(0x30 / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_BIST_CONTROL_OFFSET	(0x34 / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_BIST_DW1_OFFSET		(0x38 / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_BIST_DW2_OFFSET		(0x3C / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_CONTROL_OFFSET		(0x44 / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_TEST_CONTROL_OFFSET	(0x48 / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_STATUS_OFFSET		(0x4C / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_SACTIVE_OFFSET		(0x50 / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_LOG_OFFSET		(0x54 / SATA_INTERFACE_REG_SIZE)
#define	SATA_INTERFACE_VENDOR_UNIQUE_OFFSET	(0x5C / SATA_INTERFACE_REG_SIZE)

/*
 * LTMode Register
 */

/*
 * PHY Mode 4 Register fields
 * SATA_INTERFACE_PHY_MODE_4_OFFSET
 */
#define	SATA_PHY_INT_CONT_PARAM	(3 << 0)
#define	SATA_RX_CLOCK_SEL	(1 << 21)
#define	SATA_TX_CLOCK_SEL	(1 << 22)
#define	SATA_HOT_PLUG_TIMER	(3 << 23)

/* Value for the SATA_HOT_PLUG_TIMER */
#define	SATA_HOT_PLUG_TIMER_16MS	0
#define	SATA_HOT_PLUG_TIMER_8MS		1
#define	SATA_HOT_PLUG_TIMER_96MS	2
#define	SATA_HOT_PLUG_TIMER_500MS	3
#define	SATA_PORT_SELECTOR	(1 << 24)
#define	SATA_DIS_SWAP		(1 << 29)
#define	SATA_OO_B_BYPASS	(1 << 31)

/*
 * PHY Mode 1 Register
 */

/*
 * PHY mode 2 Register
 */
#define	SATA_PUTX_SHIFT		0
#define	SATA_PUTX		(1 << SATA_PUTX_SHIFT)
#define	SATA_PURX_SHIFT		1
#define	SATA_PURX		(1 << SATA_PURX_SHIFT)
#define	SATA_PUPLL_SHIFT	2
#define	SATA_PUPLL		(1 << SATA_PUPLL_SHIFT)
#define	SATA_PUIVREF_SHIFT	3
#define	SATA_PUIVREF		(1 << SATA_PUIVREF_SHIFT)
#define	SATA_TXPRE_SHIFT	5
#define	SATA_TXPRE		(7 << SATA_TXPRE_SHIFT)
#define	SATA_TXAMPL_SHIFT	8
#define	SATA_TXAMPL		(7 << SATA_TXAMPL_SHIFT)
#define	SATA_LOOPBACK_SHIFT	11
#define	SATA_LOOPBACK		(1 << SATA_LOOPBACK_SHIFT)
#define	SATA_MAGIC16_SHIFT	16
#define	SATA_MAGIC16		(1 << SATA_MAGIC16_SHIFT)

/*
 * BIST Control Register
 */

/*
 * Serial-ATA Interface Status Register fields
 */
#define	SATA_FIS_TYPE_RX	(0xFF << 0)
#define	SATA_PM_PORT_RX		(0xF << 8)
#define	SATA_VENDOR_UQ_DN	(1 << 12)
#define	SATA_VENDOR_UQ_ERR	(1 << 13)
#define	SATA_M_BIST_RDY		(1 << 14)
#define	SATA_M_BIST_FAIL	(1 << 15)
#define	SATA_ABORT_COMMAND	(1 << 16)
#define	SATA_L_B_PASS		(1 << 17)
#define	SATA_DMA_ACT		(1 << 18)
#define	SATA_PIO_ACT		(1 << 19)
#define	SATA_RX_HD_ACT		(1 << 20)
#define	SATA_TX_HD_ACT		(1 << 21)
#define	SATA_PLUG_IN		(1 << 22)
#define	SATA_LINK_DOWN		(1 << 23)
#define	SATA_TRANS_FSM_STS	(0x1F << 24)
#define	SATA_RX_BIST		(1 << 30)
#define	SATA_N			(1 << 31)

/*
 * BIST-DW1 Register
 */

/*
 * BIST-DW2 Register
 */

/*
 * Serial-ATA Interface Control Register
 */
#define	SATA_INTER_PMPORT_TX		0xf
#define	SATA_INTER_VENDOR_UNIQ_MODE	(1 << 8)
#define	SATA_INTER_VENDOR_UNIQ_SEND	(1 << 9)
#define	SATA_INTER_DMA_ACTIVATE		(1 << 16)
#define	SATA_INTER_CLEAR_STATUS		(1 << 24)
#define	SATA_INTER_SEND_SOFT_RESET	(1 << 25)


/*
 * Interrupt Coalescing External Registers
 */
#define	INTR_COAL_EXT_OFFSET	0x18000
#define	INTR_COAL_EXT_LEN	0xd4
#define	INTR_COAL_REG_SIZE	4

#define	SATA_PORTS_INTR_CAUSE_OFFSET	(0x08 / INTR_COAL_REG_SIZE)
#define	INTR_COAL_XBAR_TIMEOUT		(0x44 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_0		(0x48 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_1		(0x4c / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_2		(0x50 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_3		(0x54 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_4		(0x58 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_5		(0x5c / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_6		(0x60 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_7		(0x64 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_8		(0x68 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_9		(0x6c / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_10		(0x70 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_11		(0x74 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_12		(0x78 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_13		(0x7c / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_14		(0x80 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_CTRL_15		(0x84 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_INTR_LOW_CAUSE	(0x88 / INTR_COAL_REG_SIZE)
#define	SATA_IO_TRANS_INTR_HIGH_CAUSE	(0x8c / INTR_COAL_REG_SIZE)
#define	SATA_PORT_INTR_COAL_THRESH	(0xcc / INTR_COAL_REG_SIZE)
#define	SATA_PORT_INTR_COAL_TIME_THRESH	(0xd0 / INTR_COAL_REG_SIZE)

typedef struct cmd_req_queue_entry {
	uint32_t	eprd_tbl_base_low_addr;
	uint32_t	eprd_tbl_base_high_addr;
	uint16_t	ctl_flags;
	uint16_t	ata_cmd_regs[MV_NUM_ATA_CMD_REGS];
} cmd_req_queue_t[MV_QDEPTH];

typedef struct eprd {
	uint32_t	phys_mem_low;
	uint32_t	byte_count_flags;
	uint32_t	phys_mem_high;
	uint32_t	reserved;
} eprd_t;

#define	ID_REG_MASK	0x1F
#define	EPRD_EOT (1U << 31)

typedef struct cmd_resp_queue_entry {
	uint16_t	id_reg;
	struct {
		uint8_t	edma_status;
		uint8_t	device_status;
	} response_status;
	uint32_t	time_stamp;
} cmd_resp_queue_t[MV_QDEPTH];

/*
 * scatter/gather list size
 */
#define	SATA_DMA_NSEGS	257 /* at least 1MB (4KB/pg * 256) + 1 if misaligned */

#define	DMA_MEMORY_TO_DEV	0
#define	DMA_DEV_TO_MEMORY	1

/*
 * Per Marvell chip structure
 *
 * mv_ctl_mutex; mutex is used to protect all the fields within
 * struct mv_ctl and nothing else.
 */
typedef struct mv_ctl {
	dev_info_t		*mv_dip;
	struct 	sata_hba_tran	*mv_sata_hba_tran;
	int			mv_num_ports; /* per chip */
	int			mv_num_subctrls;
	struct mv_subctlr	*mv_subctl[MV_MAX_NUM_SUBCTRLR];
	uint32_t		*mv_device_main_regs;
	ddi_acc_handle_t	mv_device_main_handle;
	uint32_t		*mv_pci_intr_err_regs;
	ddi_acc_handle_t	mv_pci_intr_err_handle;
	uint32_t		*mv_intr_coal_ext_regs;
	ddi_acc_handle_t	mv_intr_coal_ext_handle;
	ddi_iblock_cookie_t	mv_iblock_cookie;
	ddi_idevice_cookie_t	mv_idevice_cookie;
	kmutex_t		mv_ctl_mutex;
	timeout_id_t		mv_timeout_id;
	boolean_t		mv_set_squelch;
	boolean_t		mv_set_magic;
	enum mv_models		mv_model;
	int			mv_flags;
	int			mv_sataframework_flags;
	ddi_acc_handle_t	mv_config_handle;
	int			mv_power_level;
	/* MSI specific fields */
	ddi_intr_handle_t	*mv_htable;	/* For array of interrupts */
	int			mv_intr_type;	/* What type of interrupt */
	int			mv_intr_cnt;	/* # of intrs count returned */
	size_t			mv_intr_size;	/* Size of intr array to */
						/* allocate */
	uint_t			mv_intr_pri;	/* Interrupt priority   */
	int			mv_intr_cap;	/* Interrupt capabilities */
	sata_pkt_t		*mv_completed_head; /* completed sata packets */
	sata_pkt_t		*mv_completed_tail; /* completed sata packets */
	ddi_softintr_t		mv_softintr_id;	/* software interrupt id */
} mv_ctl_t;
_NOTE(SCHEME_PROTECTS_DATA("no competing thread", \
    mv_ctl::mv_sataframework_flags))

/*
 * mv_subctrl_mutex; mutex is used to protect all the fields within
 * struct mv_subctlr and nothing else.
 */
struct mv_subctlr {
	struct mv_port_state	*mv_port_state[MV_NUM_PORTS_PER_SUBCTRLR];
	uint32_t		*mv_ctrl_regs;
	ddi_acc_handle_t	mv_ctrl_handle;
	kmutex_t		mv_subctrl_mutex;
};

/*
 * mv_port_mutex; mutex is used to protect all the fields within
 * struct mv_port_state and the completion field of the packet
 */
struct mv_port_state {
	/* Port register fields */
	uint32_t		*bridge_regs;
	ddi_acc_handle_t	bridge_handle;
	uint32_t		*edma_regs;
	ddi_acc_handle_t	edma_handle;
	uint32_t		*task_file1_regs;
	ddi_acc_handle_t	task_file1_handle;
	uint32_t		*task_file2_regs;
	ddi_acc_handle_t	task_file2_handle;

	/* DMA object fields */
	/* Command request queue fields */
	ddi_dma_handle_t		crqq_dma_handle;
	struct cmd_req_queue_entry	*crqq_addr;
	ddi_acc_handle_t		crqq_acc_handle;

	/* Command response queue fields */
	ddi_dma_handle_t		crsq_dma_handle;
	struct cmd_resp_queue_entry	*crsq_addr;
	ddi_acc_handle_t		crsq_acc_handle;

	/* ePRD fields */
	ddi_dma_handle_t	eprd_dma_handle[MV_QDEPTH];
	eprd_t			*eprd_addr[MV_QDEPTH];
	ddi_acc_handle_t	eprd_acc_handle[MV_QDEPTH];
	ddi_dma_cookie_t	eprd_cookie[MV_QDEPTH];

	ddi_dma_cookie_t	crqq_cookie;
	ddi_dma_cookie_t	crsq_cookie;


	/* Place to hang in flight PIO packet until it is done */
	sata_pkt_t		*mv_pio_pkt;
	sata_pkt_t		*mv_waiting_pkt;
	/* Place to hang in flight DMA packets until they are done */
	sata_pkt_t		*mv_dma_pkts[MV_QDEPTH];
	mv_ctl_t		*mv_ctlp;	/* Back ptr to controller */
	kmutex_t		mv_port_mutex;	/* Protects entire struct */
	kcondvar_t		mv_port_cv;	/* Signal pkt completion */
	kcondvar_t		mv_empty_cv;	/* DMA queue empty? */
	uint32_t		dma_in_use_map;	/* mv_dma_pkt are in use */
	int			num_dmas;	/* # DMAs currently in */
						/* flight */
	boolean_t		dma_enabled;	/* is the eDMA enabled? */
	boolean_t		dma_frozen;	/* is the DMA q frozen? */
	boolean_t		dev_restore_needed; /* device needs settings */
						    /* due to a device reset */
	uint8_t			queuing_type;	/* type of tagged queuing */
	uint8_t			port_num;	/* For diagnostic messages */
	uint8_t			pre_emphasis;	/* saved pre_emphasis value */
	uint8_t			diff_amplitude;	/*   " differential amplitude */
	/*
	 * The two entries below are present to shadow what should be
	 * in the Request Q In Pointer register and the Response Q Out Pointer
	 * regster.  It appears that reading from either of these registers
	 * can result in reading 0 rather than the correct value so we
	 * go out of our way to never read these registers.  This problem
	 * has been observed on the Sun Fire x4500.
	 */
	uint32_t		req_q_in_register;	/* hardware bug? */
	uint32_t		resp_q_out_register;    /* hardware bug? */

#define	MV_DEBUG_INFO_SIZE	64
	int mv_debug_index;
	struct mv_debug_info {
		enum {MV_START, MV_DONE} start_or_finish;
		unsigned char req_id;
		unsigned char num_dmas;
		unsigned char fill_index;
		unsigned char empty_index;
		unsigned long num_sectors;
		unsigned long long start_sector;
		unsigned long long num_bytes;
		unsigned long long dma_addrs[SATA_DMA_NSEGS];
	} mv_debug_info[MV_DEBUG_INFO_SIZE];
};

#define	MV_MSG(dip_and_cmn_err_arg) \
	mv_log dip_and_cmn_err_arg;


/*
 * PCI burst defaults for 50XX and 60XX
 */
#define	MV_DEFAULT_BURST_50XX (1 << 8)
#define	MV_DEFAULT_BURST_60XX ((1 << 11) | (1 << 13))

/* queuing_type values */
enum {
	MV_QUEUING_NONE,	/* No tagged queueing enabled */
	MV_QUEUING_TCQ,		/* Tagged command queueing enabled */
	MV_QUEUING_NCQ		/* Native command queueing enabled */
};


#if defined(DEBUG)
#define	MV_DEBUG  /* turn on debugging code */
#endif

#define	MV_DBG_RW_START	0x01
#define	MV_DBG_DEV_INTR	0x02
#define	MV_DBG_RESET	0x04
#define	MV_DBG_RESPONSE	0x08
#define	MV_DBG_TIMEOUT	0x10
#define	MV_DBG_START	0x20
#define	MV_DBG_GEN	0x40
#define	MV_DBG_CVWAIT	0x80
#define	MV_DBG_INTRSTK	0x100

#if defined(MV_DEBUG)

int mv_debug_flags = 0;

#define	MV_DEBUG_MSG(dip, flag, format, args ...)	\
	if (mv_debug_flags & (flag))			\
		mv_trace_debug(dip, format, ## args);

#else /* ! defined(MV_DEBUG) */

int mv_debug_flags = MV_DBG_RESET;

#define	MV_DEBUG_MSG(dip, flag, format, args ...)	\
	if (mv_debug_flags & (flag))			\
		sata_trace_debug(dip, format, ## args);

#endif /* defined(MV_DEBUG) */


#define	MV_POLLING_RETRIES 500	/* # of times to retry a polled request */

#define	MV_MICROSECONDS_PER_SD_PKT_TICK	1000000

/*
 * This values are due to PCI and/or root nexus limitations
 */
#if defined(__sparcv9)
#define	MV_MAX_SIZE	0x2000
#define	MV_SEG_SIZE	(MV_MAX_SIZE - 1)
#elif defined(__amd64)
#define	MV_MAX_SIZE	0x1000
#define	MV_SEG_SIZE	(MV_MAX_SIZE - 1)
#else
#error "Unknown architecture!"
#endif


#define	SCONTROL_INITIAL_STATE \
	((SCONTROL_BRIDGE_PORT_DET_INIT << SCONTROL_BRIDGE_PORT_DET_SHIFT) | \
	(SCONTROL_BRIDGE_PORT_SPD_NOLIMIT << SCONTROL_BRIDGE_PORT_SPD_SHIFT) | \
	(SCONTROL_BRIDGE_PORT_IPM_DISABLE_BOTH \
					<< SCONTROL_BRIDGE_PORT_IPM_SHIFT))


#ifdef NOTYET
#define	SCONTROL_INITIAL_STATE \
	((SCONTROL_BRIDGE_PORT_DET_INIT << SCONTROL_BRIDGE_PORT_DET_SHIFT) | \
	(SCONTROL_BRIDGE_PORT_SPD_NOLIMIT << SCONTROL_BRIDGE_PORT_SPD_SHIFT) | \
	(SCONTROL_BRIDGE_PORT_IPM_NORESTRICT << SCONTROL_BRIDGE_PORT_IPM_SHIFT))

#endif

/*
 * flags for mv_flags
 */
#define	MV_PM			0x01    /* power management is supported  */
#define	MV_ATTACH		0x02    /* Called from attach		  */
#define	MV_DETACH		0x04    /* If detaching do not except any */
					/* more commands		  */
#define	MV_NO_TIMEOUTS		0x08    /* If this flag is set time_out= 0 */
/*
 * flags for mv_sataframework_flags
 */
#define	MV_FRAMEWORK_ATTACHED	0x01	/* sata_hba_attached() called yet? */

/*
 * PM related defines
 */
#define	MV_PM_CSR		0x44
#define	MV_PM_COMPONENT_0	0x00

#define	N_PORT_MASK		0xf0
#define	N_PORT_SHIFT		4

#define	MAX_DETECT_READS	200

#if ! defined(SATA_ABORT_PACKET)
#define	SATA_ABORT_PACKET	0
#endif



#ifdef	__cplusplus
}
#endif

#endif /* _MARVELL88SX_H */

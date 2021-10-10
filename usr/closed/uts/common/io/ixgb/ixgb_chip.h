/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_IXGB_CHIP_H
#define	_IXGB_CHIP_H

#ifdef __cplusplus
extern "C" {
#endif

#define	VENDOR_ID_INTEL			0x8086
#define	SUBVENDOR_ID_INTEL		0x8086

#define	VENDOR_ID_SUN			0x108e
#define	SUBVENDOR_ID_SUN		0x108e


#define	IXGB_82597EX			0x1048
#define	IXGB_82597EX_SR			0x1A48
#define	IXGB_82597EX_CX4		0x109E

#define	IXGB_SUB_DEVID_7036		0x7036
#define	IXGB_SUB_DEVID_A11F		0xA11F
#define	IXGB_SUB_DEVID_A01F		0xA01F

#define	IXGB_SUB_DEVID_A15F		0xA15F
#define	IXGB_SUB_DEVID_A05F		0xA05F

#define	IXGB_SUB_DEVID_A12F		0xA12F
#define	IXGB_SUB_DEVID_A02F		0xA02F

#define	IXGB_TXRX_DELAY			10000	/* 10ms */
#define	IXGB_RESET_DELAY		30000	/* 30ms */
#define	IXGB_EE_RESET_DELAY		20000	/* 20ms */
#define	IXGB_LINK_RESET_DELAY		1300	/* 1300us */
#define	IXGB_MAC_RESET_DELAY		2000	/* 2000us */
#define	IXGB_SERDES_RESET_DELAY		305000	/* 305ms */

/* PCI-X Command Register */
#define	PCIX_CONF_COMM			0xe6
#define	PCIX_COMM_DP			0x0001
#define	PCIX_COMM_RO			0x0002
#define	PCIX_MAX_RCOUNT_512		0x0000
#define	PCIX_MAX_RCOUNT_1024		0x0004
#define	PCIX_MAX_RCOUNT_2048		0x0008
#define	PCIX_MAX_RCOUNT_4096		0x000c
#define	PCIX_MAX_SPLIT_1		0x0000
#define	PCIX_MAX_SPLIT_2		0x0010
#define	PCIX_MAX_SPLIT_3		0x0020
#define	PCIX_MAX_SPLIT_4		0x0030
#define	PCIX_MAX_SPLIT_8		0x0040
#define	PCIX_MAX_SPLIT_12		0x0050
#define	PCIX_MAX_SPLIT_16		0x0060
#define	PCIX_MAX_SPLIT_32		0x0070

/* Device Control Register 0 */
#define	IXGB_CTRL0			0x00000
#define	IXGB_CTRL0_DEFAULT		0x03f40000
#define	IXGB_CTRL0_VME			0x40000000
#define	IXGB_CTRL0_TPE			0x10000000
#define	IXGB_CTRL0_RPE			0x08000000
#define	IXGB_CTRL0_RST			0x04000000
#define	IXGB_CTRL0_SDP3_OUT		0x02000000
#define	IXGB_CTRL0_SDP3_IN		0x00000000
#define	IXGB_CTRL0_SDP2_OUT		0x01000000
#define	IXGB_CTRL0_SDP2_IN		0x00000000
#define	IXGB_CTRL0_SDP1_OUT		0x00800000
#define	IXGB_CTRL0_SDP1_IN		0x00000000
#define	IXGB_CTRL0_SDP0_OUT		0x00400000
#define	IXGB_CTRL0_SDP0_IN		0x00000000
#define	IXGB_CTRL0_SDP3			0x00200000
#define	IXGB_CTRL0_SDP2			0x00100000
#define	IXGB_CTRL0_SDP1			0x00080000
#define	IXGB_CTRL0_SDP0			0x00040000
#define	IXGB_CTRL0_CMDC			0x00000080
#define	IXGB_CTRL0_MDCS			0x00000040
#define	IXGB_CTRL0_XLE			0x00000020
#define	IXGB_CTRL0_JFE			0x00000010
#define	IXGB_CTRL0_LRST			0x00000008

/* Device Control Register 1 */
#define	IXGB_CTRL1			0x00008
#define	IXGB_CTRL1_PCIXHM_7_8		0x00C00000
#define	IXGB_CTRL1_PCIXHM_3_4		0x00800000
#define	IXGB_CTRL1_PCIXHM_5_8		0x00400000
#define	IXGB_CTRL1_PCIXHM_MAS		0x00C00000
#define	IXGB_CTRL1_RO_DIS		0x00020000
#define	IXGB_CTRL1_EE_RST		0x00002000
#define	IXGB_CTRL1_SDP7_OUT		0x00000800
#define	IXGB_CTRL1_SDP7_IN		0x00000000
#define	IXGB_CTRL1_SDP6_OUT		0x00000400
#define	IXGB_CTRL1_SDP6_IN		0x00000000
#define	IXGB_CTRL1_SDP5_OUT		0x00000200
#define	IXGB_CTRL1_SDP5_IN		0x00000000
#define	IXGB_CTRL1_SDP4_OUT		0x00000100
#define	IXGB_CTRL1_SDP4_IN		0x00000000
#define	IXGB_CTRL1_SDP7			0x00000080
#define	IXGB_CTRL1_SDP6			0x00000040
#define	IXGB_CTRL1_SDP5			0x00000020
#define	IXGB_CTRL1_SDP4			0x00000010
#define	IXGB_CTRL1_GPI3_EN		0x00000008
#define	IXGB_CTRL1_GPI2_EN		0x00000004
#define	IXGB_CTRL1_GPI1_EN		0x00000002
#define	IXGB_CTRL1_GPI0_EN		0x00000001
#define	IXGB_CTRL1_PCIXHM_1_2		0x00000000

/* Device Status Register */
#define	IXGB_STATUS			0x00010
#define	IXGB_STATUS_REV_ID_MASK		0x000F0000
#define	IXGB_STATUS_PCIX_SPD_MASK	0x0000C000
#define	IXGB_STATUS_PCIX_SPD_133	0x00008000
#define	IXGB_STATUS_PCIX_SPD_100	0x00004000
#define	IXGB_STATUS_PCIX_SPD_66		0x00000000
#define	IXGB_STATUS_PCIX_MODE		0x00002000
#define	IXGB_STATUS_BUS64		0x00001000
#define	IXGB_STATUS_PCI_SPD		0x00000800
#define	IXGB_STATUS_RRF			0x00000400
#define	IXGB_STATUS_RLF			0x00000200
#define	IXGB_STATUS_RIE			0x00000100
#define	IXGB_STATUS_RIS			0x00000080
#define	IXGB_STATUS_RES			0x00000040
#define	IXGB_STATUS_XAUIME		0x00000020
#define	IXGB_STATUS_TXOFF		0x00000010
#define	IXGB_STATUS_AIP			0x00000004
#define	IXGB_STATUS_LU			0x00000002
#define	IXGB_STATUS_REV_ID_SHIFT	16

/* nvmem Control/Data Register */
#define	IXGB_NVMEM			0x00018
#define	IXGB_NVMEM_FWE_MASK		0x00000030
#define	IXGB_NVMEM_FWE_EN		0x00000020
#define	IXGB_NVMEM_DO			0x00000008
#define	IXGB_NVMEM_DI			0x00000004
#define	IXGB_NVMEM_CS			0x00000002
#define	IXGB_NVMEM_SK			0x00000001

#define	IXGB_NVMEM_ADDR_WID		0x6
#define	IXGB_NVMEM_DATA_WID		16
#define	NVMEM_READ			0x6
#define	NVMEM_READ_WID			0x3
#define	NVMEM_WRITE			0x5
#define	NVMEM_WRITE_WID			0x3
#define	NVMEM_ERASE			0x7
#define	NVMEM_EREASE_WID		0x3
#define	NVMEM_EWEN			0x13
#define	NVMEM_EWEN_WID			0x5
#define	NVMEM_EWDS			0x10
#define	NVMEM_EWDS_WID			0x5
#define	NVMEM_DUMM			0x0
#define	NVMEM_DUMM_WID			0x4
#define	NVMEM_CHECKSUM			0xbaba
#define	NVMEM_SIZE			0x40
#define	NVMEM_MAC_OFFSET		0x3


/* Max length of Transmitting packet */
#define	IXGB_MFS			0x00020
#define	IXGB_MFS_SHIFT			16

/* Interrupt Reigsters */
#define	IXGB_ICR			0x00080
#define	IXGB_ICS			0x00088
#define	IXGB_IMS			0x00090
#define	IXGB_IMC			0x00098

/* Interrupt Register Bit Masks (used for ICR, ICS, IMS, and IMC) */
#define	IXGB_INT_MASK			0x00007BDF
#define	IXGB_INT_GPI3			0x00004000
#define	IXGB_INT_GPI2			0x00002000
#define	IXGB_INT_GPI1			0x00001000
#define	IXGB_INT_GPI0			0x00000800
#define	IXGB_INT_AUTOSCAN		0x00000200
#define	IXGB_INT_RXT0			0x00000080
#define	IXGB_INT_RXO			0x00000040
#define	IXGB_INT_RXDMT0			0x00000010
#define	IXGB_INT_RXSEQ			0x00000008
#define	IXGB_INT_LSC			0x00000004
#define	IXGB_INT_TXQE			0x00000002
#define	IXGB_INT_TXDW			0x00000001

#define	IXGB_INT_TX			IXGB_INT_TXQE
#define	IXGB_INT_RX			(IXGB_INT_RXSEQ | IXGB_INT_RXDMT0 | \
					    IXGB_INT_RXO | IXGB_INT_RXT0)

/* Receive control */
#define	IXGB_RCTL			0x00100
#define	IXGB_RCTL_IDLE_RX_UNIT		0
#define	IXGB_RCTL_MO_SHIFT		12
#define	IXGB_RDT_FPDB			0x80000000
#define	IXGB_RCTL_SECRC			0x04000000
#define	IXGB_RCTL_CFF			0x00800000
#define	IXGB_RCTL_MC_ONLY		0x00400000
#define	IXGB_RCTL_RPDA_MC_MAC		0x00000000
#define	IXGB_RCTL_RPDA_MASK		0x00600000
#define	IXGB_RCTL_RPDA_DEFAULT		0x00400000
#define	IXGB_RCTL_CFI			0x00100000
#define	IXGB_RCTL_CFIEN			0x00080000
#define	IXGB_RCTL_VFE			0x00040000
#define	IXGB_RCTL_BSIZE_MASK		0x00030000
#define	IXGB_RCTL_BSIZE_16384		0x00030000
#define	IXGB_RCTL_BSIZE_8192		0x00020000
#define	IXGB_RCTL_BSIZE_4096		0x00010000
#define	IXGB_RCTL_BSIZE_2048		0x00000000
#define	IXGB_RCTL_BAM			0x00008000
#define	IXGB_RCTL_MO_MASK		0x00003000
#define	IXGB_RCTL_MO_43_32		0x00003000
#define	IXGB_RCTL_MO_45_34		0x00002000
#define	IXGB_RCTL_MO_46_35		0x00001000
#define	IXGB_RCTL_MO_47_36		0x00000000
#define	IXGB_RCTL_RDMTS_MASK		0x00000300
#define	IXGB_RCTL_RDMTS_1_8		0x00000200
#define	IXGB_RCTL_RDMTS_1_4		0x00000100
#define	IXGB_RCTL_RDMTS_1_2		0x00000000
#define	IXGB_RCTL_UPE			0x00000008
#define	IXGB_RCTL_MPE			0x00000010
#define	IXGB_RCTL_SBP			0x00000004
#define	IXGB_RCTL_RXEN			0x00000002

/* some defines for controlling descriptor fetches in h/w */
#define	RXDCTL_PTHRESH_DEFAULT 		128
#define	RXDCTL_HTHRESH_DEFAULT		16
#define	RXDCTL_WTHRESH_DEFAULT		16

/* FCRTL/FCRTH - Flow Control Receive Threshold Low/High */

#define	IXGB_FCRTL			0x00108
#define	IXGB_FCRTL_XONE			0x80000000
#define	IXGB_FCRTH			0x00110

/* Rx descriptor base address */
#define	IXGB_RDBASE			0x00118

/* Rx descriptor length */
#define	IXGB_RDLEN			0x00120

/* Rx descriptor head */
#define	IXGB_RDH			0x00128

/* Rx descriptor tail */
#define	IXGB_RDT			0x00130

/* RDTR - Receive Delay Timer Register */
#define	IXGB_RDTR			0x00138
#define	IXGB_RDTR_FLUSH_PARTIAL		0x10000000
#define	IXGB_RDELAY_MIN			0
#define	IXGB_RDELAY_DEFAULT		0xa
#define	IXGB_RDELAY_MAX			0x0000ffff

/* RXDCTL - Receive Descriptor Control */
#define	IXGB_RXDCTL			0x00140
#define	IXGB_RXDCTL_WTHRESH_DEFAULT	16
#define	IXGB_RXDCTL_WTHRESH_SHIFT	18
#define	IXGB_RXDCTL_WTHRESH_MASK	0x07FC0000
#define	IXGB_RXDCTL_HTHRESH_DEFAULT	16
#define	IXGB_RXDCTL_HTHRESH_SHIFT	9
#define	IXGB_RXDCTL_HTHRESH_MASK	0x0003FE00
#define	IXGB_RXDCTL_PTHRESH_DEFAULT	128
#define	IXGB_RXDCTL_PTHRESH_SHIFT	0
#define	IXGB_RXDCTL_PTHRESH_MASK	0x000001FF

/* RAIDC - Receive Adaptive Interrupt Delay Control */
#define	IXGB_RAIDC			0x00148
#define	IXGB_RAIDC_EN			0x80000000
#define	IXGB_RAIDC_RXT_GATE		0x40000000
#define	IXGB_RAIDC_POLL_SHIFT		20
#define	IXGB_RAIDC_POLL_MASK		0x1FF00000
#define	IXGB_RAIDC_DELAY_SHIFT		11
#define	IXGB_RAIDC_DELAY_MASK		0x000FF800
#define	IXGB_RAIDC_HIGHTHRS_MASK	0x0000003F
#define	IXGB_RAIDC_POLL_20000Intr	61
#define	IXGB_RAIDC_POLL_10000Intr	122
#define	IXGB_RAIDC_POLL_5000Intr	244
#define	IXGB_RAIDC_POLL_1000Intr	1220
#define	IXGB_RAIDC_POLL_DEFAULT		122

/* RXCSUM - Receive Checksum Control */
#define	IXGB_RXCSUM			0x00158
#define	IXGB_RXCSUM_TUOFL		0x00000200
#define	IXGB_RXCSUM_IPOFL		0x00000100

/* RAL - Receive Address Low */
/* RAH - Receive Address High */
#define	IXGB_RA				0x00180
#define	IXGB_RAL			0x00180
#define	IXGB_RAH			0x00184
#define	IXGB_RAH_AV			0x80000000
#define	IXGB_RAH_ASEL_SRC		0x00010000
#define	IXGB_RAH_ASEL_DEST		0x00000000
#define	IXGB_RAH_ASEL_MASK		0x00030000
#define	IXGB_RAH_ADDR_MASK		0x0000ffff
#define	IXGB_ADDR_NUM			16
#define	MAC_ADDRESS_REG(base, n)	((base) + 8*(n))

/* Multicast table */
#define	IXGB_MCA			0x00200
#define	IXGB_MCA_ENTRY_NUM		128
#define	IXGB_MCA_BIT_NUM		4096
#define	MCA_ADDRESS_REG(n)		(0x00200 + 4*(n))
#define	HASH_REG_MASK			0x7f
#define	HASH_BIT_MASK			0x1f

/* Vlan table */
#define	IXGB_VFTA			0x00400
#define	IXGB_VFTA_ENTRY_NUM		128
#define	VLAN_ID_REG(n)			(0x00400 + 4*(n))

#define	IXGB_REQ_MULBDS			8

/* TCTL - Transmit Control Register */
#define	IXGB_TCTL			0x00600
#define	IXGB_TCTL_TPDE			0x00000004
#define	IXGB_TCTL_TXEN			0x00000002
#define	IXGB_TCTL_TCE			0x00000001
#define	IXGB_TCTL_IDLE_TX_UNIT		0

/* Tx descriptor base address */
#define	IXGB_TDBASE			0x00608
#define	IXGB_TDBAH			0x0060C

/* Tx descriptor length */
#define	IXGB_TDLEN			0x00610

/* Tx descriptor head */
#define	IXGB_TDH			0x00618

/* Tx descriptor tail */
#define	IXGB_TDT			0x00620

/* TIDV - Transmit Interrupt Delay Value */
#define	IXGB_TX_COALESCE		0x00628
#define	IXGB_TX_COALESCE_NUM		1

/* TXDCTL - Transmit Descriptor Control */
#define	IXGB_TXDCTL			0x00630
#define	IXGB_TXDCTL_WTHRESH_SHIFT	16
#define	IXGB_TXDCTL_WTHRESH_MASK	0x007F0000
#define	IXGB_TXDCTL_HTHRESH_SHIF	8
#define	IXGB_TXDCTL_HTHRESH_MASK	0x00007F00
#define	IXGB_TXDCTL_PTHRESH_MASK	0x0000007F

/* TSPMT - TCP Segmentation Pad and Minimum Threshold */
#define	IXGB_TSPMT			0x00638
#define	IXGB_TSPMT_TSMT_MASK		0x0000FFFF
#define	IXGB_TSPMT_TSPBP_MASK		0xFFFF0000
#define	IXGB_TSPMT_TSPBP_SHIFT		16

/* PAP - Pause and Pace */
#define	IXGB_PAP			0x00640
#define	IXGB_PAP_TXPV_WAN		0x000F0000
#define	IXGB_PAP_TXPV_9G		0x00090000
#define	IXGB_PAP_TXPV_8G		0x00080000
#define	IXGB_PAP_TXPV_7G		0x00070000
#define	IXGB_PAP_TXPV_6G		0x00060000
#define	IXGB_PAP_TXPV_5G		0x00050000
#define	IXGB_PAP_TXPV_4G		0x00040000
#define	IXGB_PAP_TXPV_3G		0x00030000
#define	IXGB_PAP_TXPV_2G		0x00020000
#define	IXGB_PAP_TXPV_1G		0x00010000
#define	IXGB_PAP_TXPV_10G		0x00000000
#define	IXGB_PAP_TXPV_MASK		0x000F0000
#define	IXGB_PAP_TXPC_MASK		0x0000FFFF

/* PCSC - PCS Control 1 */
#define	IXGB_PCSC1			0x00700
#define	IXGB_PCSC1_LOOPBACK		0x00004000

/* PCSC2 - PCS Control 2 */
#define	IXGB_PCSC2			0x00708
#define	IXGB_PCSC2_PCS_TYPE_MASK	0x00000003
#define	IXGB_PCSC2_PCS_TYPE_10GBX	0x00000001

/* PCSS - PCS Status 1 */
#define	IXGB_PCSS1			0x00710
#define	IXGB_PCSS1_LOCAL_FAULT		0x00000080
#define	IXGB_PCSS1_RX_LINK_STATUS	0x00000004

/* PCSS - PCS Status 2 */
#define	IXGB_PCSS2			0x00718
#define	IXGB_PCSS2_DEV_PRES_MASK	0x0000C000
#define	IXGB_PCSS2_DEV_PRES		0x00004000
#define	IXGB_PCSS2_TX_LF		0x00000800
#define	IXGB_PCSS2_RX_LF		0x00000400
#define	IXGB_PCSS2_10GBW		0x00000004
#define	IXGB_PCSS2_10GBX		0x00000002
#define	IXGB_PCSS2_10GBR		0x00000001

/* XPCSS - 10GBASE-X PCS Status */
#define	IXGB_XPCSS			0x00720
#define	IXGB_XPCSS_ALIGN_STATUS		0x00001000
#define	IXGB_XPCSS_PATTERN_TEST		0x00000800
#define	IXGB_XPCSS_LANE_3_SYNC		0x00000008
#define	IXGB_XPCSS_LANE_2_SYNC		0x00000004
#define	IXGB_XPCSS_LANE_1_SYNC		0x00000002
#define	IXGB_XPCSS_LANE_0_SYNC		0x00000001

/* UCCR - Unilink Circuit Control Register */
#define	IXGB_UCCR			0x00728
#define	IXGB_XPCSTC			0x00730
#define	IXGB_XPCSTC_BERT_TRIG		0x00200000
#define	IXGB_XPCSTC_BERT_SST		0x00100000
#define	IXGB_XPCSTC_BERT_PSZ_MASK	0x000C0000
#define	IXGB_XPCSTC_BERT_PSZ_SHIFT	17
#define	IXGB_XPCSTC_BERT_PSZ_INF	0x00000003
#define	IXGB_XPCSTC_BERT_PSZ_68		0x00000001
#define	IXGB_XPCSTC_BERT_PSZ_1028	0x00000000

/* MACA - MDI Autoscan Command and Address */
#define	IXGB_MACA			0x00738
#define	IXGB_APAE			0x00740
#define	IXGB_ARD			0x00748
#define	IXGB_AIS			0x00750

#define	IXGB_REQ_TX_MULBDS		8

/* MDI Single Command and Address */
#define	IXGB_MSCA			0x000758
#define	IXGB_MSCA_MDI_IN_PROG_EN	0x80000000
#define	IXGB_MSCA_MDI_COMMAND		0x40000000
#define	IXGB_MSCA_ST_CODE_SHIFT		28
#define	IXGB_MSCA_ST_CODE_MASK		0x30000000
#define	IXGB_MSCA_OLD_PROTOCOL		0x10000000
#define	IXGB_MSCA_NEW_PROTOCOL		0x00000000
#define	IXGB_MSCA_OP_CODE_SHIFT		26
#define	IXGB_MSCA_OP_CODE_MASK		0x0C000000
#define	IXGB_MSCA_READ			0x08000000
#define	IXGB_MSCA_WRITE			0x04000000
#define	IXGB_MSCA_ADDR_CYCLE		0x00000000
#define	IXGB_MSCA_PHY_ADDR_SHIFT	21
#define	IXGB_MSCA_PHY_ADDR_MASK		0x03E00000
#define	IXGB_MSCA_DEV_TYPE_SHIFT	16
#define	IXGB_MSCA_DEV_TYPE_MASK		0x001F0000
#define	IXGB_MSCA_REG_ADDR_SHIFT		0
#define	IXGB_MSCA_REG_ADDR_MASK		0x0000FFFF

/* MDI Single Read and write data */
#define	IXGB_MSRWD			0x00760
#define	IXGB_MSRWD_RDATA_SHIFT		16
#define	IXGB_MSRWD_RDATA_MASK		0xFFFF0000
#define	IXGB_MSRWD_WDATA_SHIFT		0
#define	IXGB_MSRWD_WDATA_MASK		0x0000FFFF

/* Diagnostic register definition */
#define	IXGB_RDFH			0x04000
#define	IXGB_RDFT			0x04008
#define	IXGB_RDFTS			0x04018
#define	IXGB_RDFPC			0x04020
#define	IXGB_TDFH			0x04028
#define	IXGB_TDFT			0x04030
#define	IXGB_TDFTS			0x04040
#define	IXGB_TDFPC			0x04048

#define	IXGB_RPR			0x04058
#define	IXGB_RPR_PAGE_MASK		0x0003f000
#define	IXGB_RPR_MEM_SEL		0x00100000

#define	IXGB_TPR			0x04060
#define	IXGB_TPR_PAGE_MASK		0x00007000
#define	IXGB_TPR_MEM_SEL		0x00100000

#define	IXGB_RPDBM			0x05000
#define	IXGB_RPDBM_BASE			0x00005000
#define	IXGB_RPBM_LEN			0x40000
#define	IXGB_RDBM_LEN			0x1000

#define	IXGB_TPDBM			0x06000
#define	IXGB_TPDBM_BASE			0x00006000
#define	IXGB_TPBM_LEN			0x8000
#define	IXGB_TDBM_LEN			0x800

#define	IXGB_PAGE_SIZE			0x00fff
#define	IXGB_PAGE_NO_SHIFT		12
#define	IXGB_REG_SIZE			0x07000

/* Kirkwood GPIO Pins, please refer to kealia spec page 3 */
#define	IXGB_XFP_RESET_PIN		IXGB_CTRL0_SDP3_OUT
#define	IXGB_XFP_RESET			IXGB_CTRL0_SDP2
#define	IXGB_LXT_RESET_PIN		IXGB_CTRL0_SDP2_OUT
#define	IXGB_LXT_RESET			0x00000000
#define	IXGB_LXT_NO_RESET		IXGB_CTRL0_SDP3
#define	IXGB_XFP_INT			IXGB_CTRL1_SDP4_IN
#define	IXGB_10G_INT			IXGB_CTRL1_SDP5_IN
#define	IXGB_XFP1_DELSEL_PIN		IXGB_CTRL1_SDP6_OUT
#define	IXGB_XFP1_DELSEL		IXGB_CTRL1_SDP6
#define	IXGB_XFP2_DELSEL_PIN		IXGB_CTRL1_SDP7_OUT
#define	IXGB_XFP2_DELSEL		IXGB_CTRL1_SDP7

/* phy registers */
#define	IXGB_PHY_ADDRESS_INTEL		0x0
#define	IXGB_PHY0_ADDRESS_SUN		0x0
#define	IXGB_PHY1_ADDRESS_SUN		0x01

/* MDIO device address, please refer to IEEE802.3AE clause 45 */
#define	MDIO_PMA			0x01
#define	MDIO_PMA_CR1			0x0000
#define	MDIO_PMA_CR1_RESET		0x8000
#define	MDIO_PMA_CR1_LOOP		0x0001

#define	MDIO_WIS			0x02
#define	MDIO_PCS			0x03
#define	MDIO_XGXS			0x04

/* Lxt12101 register define */
#define	MDIO_VENDOR_LXT			0x1f
#define	IXGB_LXT_CFG0			0x8000
#define	IXGB_LXT_CFG1			0x8001
#define	IXGB_LXT_CFG2			0x8002
#define	IXGB_LXT_CFG3			0x8003
#define	IXGB_LXT_CFG4			0x8004
#define	IXGB_LXT_CFG5			0x8005
#define	IXGB_LXT_LANECFG0		0x8009
#define	IXGB_LXT_GPIO0			0x9000
#define	IXGB_LXT_GPIO1			0x9001
#define	IXGB_LXT_GPIO2			0x9002
#define	IXGB_LXT_GPIO3			0x9003
#define	IXGB_LXT_GPIO4			0x9004
#define	IXGB_LXT_STOUT0			0x9015
#define	IXGB_LXT_STOUT1			0x9016
#define	IXGB_LXT_STOUT2			0x9017
#define	IXGB_LXT_PMAST			0x1001

/* BCM8704 register define */
#define	MDIO_VENDOR_BCM			0x3
#define	IXGB_BCM_CTRL			0xC800
#define	IXGB_BCM_ACCR			0xC801
#define	IXGB_BCM_RXCR			0xC802
#define	IXGB_BCM_TXCR			0xC803
#define	IXGB_BCM_ASR0			0xC804
#define	IXGB_BCM_AC0			0xC805
#define	IXGB_BCM_AC1			0xC806

/*
 * Hardware-defined Statistics Block Offsets
 *
 * These are given in the manual as addresses in NIC memory, starting
 * from the NIC statistics area base address of 0x2000;
 */

#define	KS_BASE				0x2000
#define	KS_ADDR(x)			(((x)-KS_BASE)/sizeof (uint64_t))

typedef enum {
	KS_ifHCInPkts = KS_ADDR(0x2000),
	KS_ifHCInGoodPkts,
	KS_ifHCInBroadcastPkts,
	KS_ifHCInMulticastPkts,
	KS_ifHCInUcastPkts,
	KS_ifHCInVlanPkts,
	KS_ifHCInJumboPkts,
	KS_ifHInGoodOctets,
	KS_ifHInOctets,
	KS_ifNoMoreRxBDs,
	KS_ifUndersizePkts,
	KS_ifOverrsizePkts,
	KS_IfRangeLengthError,
	KS_IfCRCError,
	KS_IfMidByteEllegal,
	KS_IfMidByteError,
	KS_IfLossPkts,

	KS_ifHOutPkts = KS_ADDR(0x2100),
	KS_ifHOutGoodPkts,
	KS_ifHOutBroadcastPkts,
	KS_ifHOutMulticastPkts,
	KS_ifHOutUcastPkts,
	KS_ifHOutVlanPkts,
	KS_ifHOutJumboPkts,
	KS_ifHOutGoodOctets,
	KS_ifHOutOctets,
	KS_ifHOutDefer,
	KS_ifHOutPkts64Octets,

	KS_ifHTsoCounts = KS_ADDR(0x2170),
	KS_ifHTsoErrorCounts,
	KS_IfHErrorIdleCounts,
	KS_IfHRFaultcounts,
	KS_IfHLFaultcounts,
	KS_IfHRPausecounts,
	KS_IfHTPausecounts,
	KS_IfHRControlcounts,
	KS_IfHTControlcounts,
	KS_IfHRXonCounts,
	KS_IfHTXonCounts,
	KS_IfHRXofCounts,
	KS_IfHTXofCounts,
	KS_IFHJabberCounts,

	KS_STATS_SIZE = KS_ADDR(0x21d8)

} ixgb_stats_offset_t;

/*
 * Hardware-defined Statistics Block
 *
 * Another view of the statistic block, as a array and a structure ...
 */
typedef union {
	uint64_t	a[KS_STATS_SIZE];
	struct {

		uint64_t InPkts;
		uint64_t InGoodPkts;
		uint64_t InBroadcastPkts;
		uint64_t InMulticastPkts;
		uint64_t InUcastPkts;
		uint64_t InVlanPkts;
		uint64_t InJumboPkts;
		uint64_t InGoodOctets;
		uint64_t InOctets;
		uint64_t NoMoreRxBDs;
		uint64_t UndersizePkts;
		uint64_t OverrsizePkts;
		uint64_t RangeLengthError;
		uint64_t CRCError;
		uint64_t MidByteEllegal;
		uint64_t MidByteError;
		uint64_t DiscardPkts;
		uint64_t spare0[(0x2100 - 0x2084)/sizeof (uint64_t)];

		uint64_t OutPkts;
		uint64_t OutGoodPkts;
		uint64_t OutBroadcastPkts;
		uint64_t OutMulticastPkts;
		uint64_t OutUcastPkts;
		uint64_t OutVlanPkts;
		uint64_t HOutJumboPkts;
		uint64_t OutGoodOctets;
		uint64_t OutOctets;
		uint64_t OutDefer;
		uint64_t OutPkts64Octets;

		uint64_t spare1[(0x2170-0x2150)/sizeof (uint64_t)];

		uint64_t TsoCounts;
		uint64_t TsoErrorCounts;
		uint64_t ErrorIdleCounts;
		uint64_t RFaultcounts;
		uint64_t LFaultcounts;
		uint64_t RPausecounts;
		uint64_t TPausecounts;
		uint64_t RControlcounts;
		uint64_t TControlcounts;
		uint64_t RXonCounts;
		uint64_t TXonCounts;
		uint64_t RXofCounts;
		uint64_t TXofCounts;
		uint64_t JabberCounts;
	} s;
} ixgb_hw_statistics_t;

/* Tx context descriptor */
#define	IXGB_CBD_TCP		0x01000000
#define	IXGB_CBD_IP		0x02000000
#define	IXGB_CBD_TSE		0x04000000
#define	IXGB_CBD_RS		0x08000000
#define	IXGB_CBD_IDE		0x80000000
#define	IXGB_CBD_TYPE		0x00000000
#define	IXGB_CBD_DD		0x00000001
typedef struct ixgb_cbd {
	union {
		uint32_t ip_csum;
		struct {
			uint8_t ipcss;
			uint8_t ipcso;
			uint16_t ipcse;
		}ip_csum_fields;
	} ip_csum_part;

	union {
		uint32_t tcp_csum;
		struct {
			uint8_t tucss;
			uint8_t tucso;
			uint16_t tucse;
		}tcp_csum_fields;
	} tcp_csum_part;

	uint32_t	cmd_type_len;

	union {
		uint32_t tcp_seg;
		struct {
			uint8_t status;
			uint8_t hdr_len;
			uint16_t mss;
		}tcp_seg_fileds;
	} tcp_seg_part;
}ixgb_cbd_t;

/* Rx data descriptor */
#define	IXGB_RSTATUS_PIF		0x80
#define	IXGB_RSTATUS_IPCS		0x40
#define	IXGB_RSTATUS_TCPCS		0x20
#define	IXGB_RSTATUS_VP			0x08
#define	IXGB_RSTATUS_IXSM		0x04
#define	IXGB_RSTATUS_EOP		0x02
#define	IXGB_RSTATUS_DD			0x01
#define	IXGB_RERR_RXE			0x80
#define	IXGB_RERR_IPE			0x40
#define	IXGB_RERR_TCPE			0x20
#define	IXGB_RERR_P			0x08
#define	IXGB_RERR_SE			0x02
#define	IXGB_RERR_CE			0x01
#define	IXGB_RVLAN_MASK			0x0FFF
#define	IXGB_RPRI_MASK			0xE000
#define	IXGB_RPRI_SHIFT			0x000D
#define	IXGB_RERR_MASK			(IXGB_RERR_RXE | IXGB_RERR_P | \
					    IXGB_RERR_SE | IXGB_RERR_CE)
typedef struct ixgb_rbd {
	uint64_t host_buf_addr;
	uint16_t length;
	uint16_t checksum;
	uint8_t  status;
	uint8_t  errors;
	uint16_t special;
}ixgb_rbd_t;

/* Tx data descriptor */
#define	IXGB_TBD_MASK			0xFF000000
#define	IXGB_TBD_IDE			0x80000000
#define	IXGB_TBD_VLE			0x40000000
#define	IXGB_TBD_RS			0x08000000
#define	IXGB_TBD_TSE			0x04000000
#define	IXGB_TBD_EOP			0x01000000
#define	IXGB_TBD_TYPE_MASK		0x00F00000
#define	IXGB_TBD_TYPE			0x00100000
#define	IXGB_TBD_LEN_MASK		0x000FFFFF
#define	IXGB_TBD_STATUS_DD		0x01
#define	IXGB_TBD_POPTS_IXSM		0x01
#define	IXGB_TBD_POPTS_TXSM		0x02
typedef struct {
	uint64_t host_buf_addr;
	uint32_t len_cmd;
	uint8_t  status;
	uint8_t  popts;
	uint16_t vlan_id;
} ixgb_sbd_t;

#ifdef __cplusplus
}
#endif

#endif	/* _IXGB_CHIP_H */

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SXGE_VMAC_H
#define	_SXGE_VMAC_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * TX VMAC registers
 * For all the hosts, PIO_BAR_BASE address should be added to all the
 * base addresses.
 */
#define	TXVMAC_BASE	(SXGE_HOST_VNI_BASE + SXGE_TXVMAC_BASE)
#define	TXVMAC_VNI_STP	0x10000
#define	TXVMAC_NF_STP	0x2000

/*
 * Valid vni values - 0 thru 11
 * Valid nf values - 0 thru 3
 */

/* Transmit VMAC configuration register */
#define	TXVMAC_CFG_OFF		0x0000
#define	TXVMAC_CFG(vni, nf)	((TXVMAC_BASE) + (TXVMAC_CFG_OFF) +	\
				((vni) * (TXVMAC_VNI_STP)) +	\
				((nf) * (TXVMAC_NF_STP)))
#define	TXVMAC_CFG_SW_RST_MSK		0x00000001
#define	TXVMAC_CFG_SW_RST_SH		0

/* Transmit VMAC status register */
#define	TXVMAC_STAT_OFF		0x0008
#define	TXVMAC_STAT(vni, nf)	((TXVMAC_BASE) +  (TXVMAC_STAT_OFF) +	\
				((vni) * (TXVMAC_VNI_STP)) +	\
				((nf) * (TXVMAC_NF_STP)))
/* The following 3 bits are all Write 1 to Clear */
#define	TXVMAC_STAT_SW_RST_DONE_MSK		0x00000001
#define	TXVMAC_STAT_SW_RST_DONE_SH		0
#define	TXVMAC_STAT_SW_FRMCNT_OVL_MSK		0x00000002
#define	TXVMAC_STAT_SW_FRMCNT_OVL_SH		1
#define	TXVMAC_STAT_SW_BTCNT_OVL_MSK		0x00000004
#define	TXVMAC_STAT_SW_BTCNT_OVL_SH		2

/* Transmit VMAC status mask register */
#define	TXVMAC_STMASK_OFF		0x0010
#define	TXVMAC_STMASK(vni, nf)	((TXVMAC_BASE) +  (TXVMAC_STMASK_OFF) +	\
				((vni) * (TXVMAC_VNI_STP)) +	\
				((nf) * (TXVMAC_NF_STP)))
/* Value of 1 masks the interrupt */
#define	TXVMAC_STMASK_SW_RST_DONE_MSK		0x00000001
#define	TXVMAC_STMASK_SW_RST_DONE_SH		0
#define	TXVMAC_STMASK_SW_FRMCNT_OVL_MSK		0x00000002
#define	TXVMAC_STMASK_SW_FRMCNT_OVL_SH		1
#define	TXVMAC_STMASK_SW_BTCNT_OVL_MSK		0x00000004
#define	TXVMAC_STMASK_SW_BTCNT_OVL_SH		2

/* Transmit VMAC status force register */
#define	TXVMAC_STFRC_OFF		0x0018
#define	TXVMAC_STFRC(vni, nf)	((TXVMAC_BASE) +  (TXVMAC_STFRC_OFF) +	\
				((vni) * (TXVMAC_VNI_STP)) +	\
				((nf) * (TXVMAC_NF_STP)))
/*
 * When bit is set to 1, it forces the status to indicate occurence of
 * this event.
 */
#define	TXVMAC_STFRC_SW_RST_DONE_MSK		0x00000001
#define	TXVMAC_STFRC_SW_RST_DONE_SH		0
#define	TXVMAC_STFRC_SW_FRMCNT_OVL_MSK		0x00000002
#define	TXVMAC_STFRC_SW_FRMCNT_OVL_SH		1
#define	TXVMAC_STFRC_SW_BTCNT_OVL_MSK		0x00000004
#define	TXVMAC_STFRC_SW_BTCNT_OVL_SH		2

/* Transmit VMAC Frame count register */
#define	TXVMAC_FRM_CNT_OFF		0x0020
#define	TXVMAC_FRM_CNT(vni, nf)	((TXVMAC_BASE) + (TXVMAC_FRM_CNT_OFF) +	\
				((vni) * (TXVMAC_VNI_STP)) +	\
				((nf) * (TXVMAC_NF_STP)))

#define	TXVMAC_FRM_CNT_MSK		0xffffffff
#define	TXVMAC_FRM_CNT_SH		0

/* Transmit VMAC Byte count register */
#define	TXVMAC_BYTE_CNT_OFF		0x0028
#define	TXVMAC_BYTE_CNT(vni, nf)	((TXVMAC_BASE) +	\
				(TXVMAC_BYTE_CNT_OFF) +	\
				((vni) * (TXVMAC_VNI_STP)) +	\
				((nf) * (TXVMAC_NF_STP)))

#define	TXVMAC_BYTE_CNT_MSK		0xffffffff
#define	TXVMAC_BYTE_CNT_SH		0


/*
 * RX VMAC registers
 * For all the hosts, PIO_BAR_BASE address should be added to all the
 * base addresses.
 */
#define	RXVMAC_BASE	(SXGE_HOST_VNI_BASE + SXGE_RXVMAC_BASE)
#define	RXVMAC_VNI_STP	0x10000
#define	RXVMAC_VMAC_STP	0x2000

/*
 * Valid vni values - 0 thru 11
 * Valid nf values - 0 thru 3
 */

/* Receive VMAC configuration register */
#define	RXVMAC_CFG_OFF		0x0000
#define	RXVMAC_CFG(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_CFG_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))

#define	RXVMAC_CFG_STP_ST_SH		0
#define	RXVMAC_CFG_STP_ST_MSK		0x00000001
#define	RXVMAC_CFG_STOP_SH		1
#define	RXVMAC_CFG_STOP_MSK		0x00000001
#define	RXVMAC_CFG_RST_ST_SH		2
#define	RXVMAC_CFG_RST_ST_MSK		0x00000001
#define	RXVMAC_CFG_RST_SH		3
#define	RXVMAC_CFG_RST_MSK		0x00000001
#define	RXVMAC_CFG_DROP_SH		5
#define	RXVMAC_CFG_DROP_MSK		0x00000003
#define	RXVMAC_CFG_DROP_L2CRCERR	0x02
#define	RXVMAC_CFG_DROP_CHKSUMERR	0x01
#define	RXVMAC_CFG_PROMISCMD_SH		7
#define	RXVMAC_CFG_PROMISCMD_MSK	0x00000001
#define	RXVMAC_CFG_DMA_VECT_SH		8
#define	RXVMAC_CFG_DMA_VECT_MSK		0x0000000f
#define	RXVMAC_CFG_OPCODE_SH		12
#define	RXVMAC_CFG_OPCODE_MSK		0x00000003
#define	RXVMAC_CFG_DROP_FCOE_SH		14
#define	RXVMAC_CFG_DROP_FCOE_MSK	0x00000003
#define	RXVMAC_CFG_DROPFCOE_VER_LEN	0x02
#define	RXVMAC_CFG_DROPFCOE_FCCRC	0x01

/* Receive VMAC TCP control mask register */
#define	RXVMAC_TCPCTLMSK_OFF		0x0008
#define	RXVMAC_TCPCTLMSK(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_TCPCTLMSK_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))
/*
 * For the mask bits (bits 0 thru 5), a value of 0 means that the TCP packet
 * with the corresponding flag set will be discarded.
 */
#define	RXVMAC_TCPCTL_URG_MSK		0x00000001
#define	RXVMAC_TCPCTL_URG_SH		0
#define	RXVMAC_TCPCTL_ACK_MSK		0x00000002
#define	RXVMAC_TCPCTL_ACK_SH		1
#define	RXVMAC_TCPCTL_PUSH_MSK		0x00000004
#define	RXVMAC_TCPCTL_PUSH_SH		2
#define	RXVMAC_TCPCTL_RST_MSK		0x00000008
#define	RXVMAC_TCPCTL_RST_SH		3
#define	RXVMAC_TCPCTL_SYN_MSK		0x00000010
#define	RXVMAC_TCPCTL_SYN_SH		4
#define	RXVMAC_TCPCTL_FIN_MSK		0x00000020
#define	RXVMAC_TCPCTL_FIN_SH		5
#define	RXVMAC_TCPCTL_DISCEN_MSK	0x00000040
#define	RXVMAC_TCPCTL_DISCEN_SH		6

/* Receive VMAC Frame counter register */
#define	RXVMAC_FRM_CNT_OFF		0x0060
#define	RXVMAC_FRM_CNT(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_FRM_CNT_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))
#define	RXVMAC_FRM_CNT_MSK		0xffffffff
#define	RXVMAC_FRM_CNT_SH		0

/* Receive VMAC Byte counter register */
#define	RXVMAC_BYTE_CNT_OFF		0x0068
#define	RXVMAC_BYTE_CNT(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_BYTE_CNT_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))
#define	RXVMAC_BYTE_CNT_MSK		0xffffffff
#define	RXVMAC_BYTE_CNT_SH		0

/* Receive VMAC Drop Frame counter register */
#define	RXVMAC_DROPFM_CNT_OFF		0x0070
#define	RXVMAC_DROPFM_CNT(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_DROPFM_CNT_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))
#define	RXVMAC_DROPFM_CNT_MSK		0xffffffff
#define	RXVMAC_DROPFM_CNT_SH		0

/* Receive VMAC Drop byte counter register */
#define	RXVMAC_DROPBT_CNT_OFF		0x0078
#define	RXVMAC_DROPBT_CNT(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_DROPBT_CNT_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))
#define	RXVMAC_DROPBT_CNT_MSK		0xffffffff
#define	RXVMAC_DROPBT_CNT_SH		0

/* Receive VMAC Multicast frame counter register */
#define	RXVMAC_MCASTFM_CNT_OFF		0x0080
#define	RXVMAC_MCASTFM_CNT(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_MCASTFM_CNT_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))
#define	RXVMAC_MCASTFM_CNT_MSK		0xffffffff
#define	RXVMAC_MCASTFM_CNT_SH		0

/* Receive VMAC Broadcast frame counter register */
#define	RXVMAC_BCASTFM_CNT_OFF		0x0088
#define	RXVMAC_BCASTFM_CNT(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_BCASTFM_CNT_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))
#define	RXVMAC_BCASTFM_CNT_MSK		0xffffffff
#define	RXVMAC_BCASTFM_CNT_SH		0

/* Receive VMAC interrupt status register */
#define	RXVMAC_INT_STAT_OFF		0x0040
#define	RXVMAC_INT_STAT(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_INT_STAT_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))

#define	RXVMAC_INTST_FMCNT_OVL_MSK	0x00000002
#define	RXVMAC_INTST_FMCNT_OVL_SH	1
#define	RXVMAC_INTST_BTCNT_OVL_MSK	0x00000004
#define	RXVMAC_INTST_BTCNT_OVL_SH	2
#define	RXVMAC_INTST_DRPFMCNT_OVL_MSK	0x00000008
#define	RXVMAC_INTST_DRPFMCNT_OVL_SH	3
#define	RXVMAC_INTST_DRPBTCNT_OVL_MSK	0x00000010
#define	RXVMAC_INTST_DRPBTCNT_OVL_SH	4
#define	RXVMAC_INTST_MCASTFMCNT_OVL_MSK	0x00000020
#define	RXVMAC_INTST_MCASTFMCNT_OVL_SH	5
#define	RXVMAC_INTST_BCASTFMCNT_OVL_MSK	0x00000040
#define	RXVMAC_INTST_BCASTFMCNT_OVL_SH	6
#define	RXVMAC_INTST_LINKUP_MSK		0x00000080
#define	RXVMAC_INTST_LINKUP_SH 		7
#define	RXVMAC_INTST_LINKDN_MSK		0x00000100
#define	RXVMAC_INTST_LINKDN_SH 		8
#define	RXVMAC_INTST_LINKST_MSK		0x00008000
#define	RXVMAC_INTST_LINKST_SH 		15

/* Receive VMAC interrupt mask register */
#define	RXVMAC_INT_MSK_OFF		0x0048
#define	RXVMAC_INT_MSK(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_INT_MSK_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))
/* Set to 1 to mask the interrupt */
#define	RXVMAC_INTMSK_FMCNT_OVL_MSK		0x00000002
#define	RXVMAC_INTMSK_FMCNT_OVL_SH		1
#define	RXVMAC_INTMSK_BTCNT_OVL_MSK		0x00000004
#define	RXVMAC_INTMSK_BTCNT_OVL_SH		2
#define	RXVMAC_INTMSK_DRPFMCNT_OVL_MSK		0x00000008
#define	RXVMAC_INTMSK_DRPFMCNT_OVL_SH		3
#define	RXVMAC_INTMSK_DRPBTCNT_OVL_MSK		0x00000010
#define	RXVMAC_INTMSK_DRPBTCNT_OVL_SH		4
#define	RXVMAC_INTMSK_MCASTFMCNT_OVL_MSK	0x00000020
#define	RXVMAC_INTMSK_MCASTFMCNT_OVL_SH		5
#define	RXVMAC_INTMSK_BCASTFMCNT_OVL_MSK	0x00000040
#define	RXVMAC_INTMSK_BCASTFMCNT_OVL_SH		6
#define	RXVMAC_INTMSK_LINKUP_MSK		0x00000080
#define	RXVMAC_INTMSK_LINKUP_SH			7
#define	RXVMAC_INTMSK_LINKDN_MSK		0x00000100
#define	RXVMAC_INTMSK_LINKDN_SH			8

/* Receive VMAC interrupt debug register */
#define	RXVMAC_INT_DBG_OFF		0x0050
#define	RXVMAC_INT_DBG(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_INT_DBG_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))
#define	RXVMAC_INTDBG_FMCNT_OVL_MSK		0x00000002
#define	RXVMAC_INTDBG_FMCNT_OVL_SH		1
#define	RXVMAC_INTDBG_BTCNT_OVL_MSK		0x00000004
#define	RXVMAC_INTDBG_BTCNT_OVL_SH		2
#define	RXVMAC_INTDBG_DRPFMCNT_OVL_MSK		0x00000008
#define	RXVMAC_INTDBG_DRPFMCNT_OVL_SH		3
#define	RXVMAC_INTDBG_DRPBTCNT_OVL_MSK		0x00000010
#define	RXVMAC_INTDBG_DRPBTCNT_OVL_SH		4
#define	RXVMAC_INTDBG_MCASTFMCNT_OVL_MSK	0x00000020
#define	RXVMAC_INTDBG_MCASTFMCNT_OVL_SH		5
#define	RXVMAC_INTDBG_BCASTFMCNT_OVL_MSK	0x00000040
#define	RXVMAC_INTDBG_BCASTFMCNT_OVL_SH		6
#define	RXVMAC_INTDBG_LINKUP_MSK		0x00000080
#define	RXVMAC_INTDBG_LINKUP_SH			7
#define	RXVMAC_INTDBG_LINKDN_MSK		0x00000100
#define	RXVMAC_INTDBG_LINKDN_SH			8

/* Receive VMAC clear counters register */
#define	RXVMAC_CLR_CNT_OFF		0x0058
#define	RXVMAC_CLR_CNT(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_CLR_CNT_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))

#define	RXVMAC_CLR_CNT_MSK		0x00000001
#define	RXVMAC_CLR_CNT_SH		0


/* Receive VMAC Frame counter Debug register */
#define	RXVMAC_FRM_CNT_DBG_OFF		0x0010
#define	RXVMAC_FRM_CNT_DBG(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_FRM_CNT_DBG_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))

/* Receive VMAC Byte counter Debug register */
#define	RXVMAC_BYTE_CNT_DBG_OFF		0x0018
#define	RXVMAC_BYTE_CNT_DBG(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_BYTE_CNT_DBG_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))

/* Receive VMAC Drop Frame counter Debug register */
#define	RXVMAC_DROPFM_CNT_DBG_OFF		0x0020
#define	RXVMAC_DROPFM_CNT_DBG(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_DROPFM_CNT_DBG_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))

/* Receive VMAC Drop byte counter Debug register */
#define	RXVMAC_DROPBT_CNT_DBG_OFF		0x0028
#define	RXVMAC_DROPBT_CNT_DBG(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_DROPBT_CNT_DBG_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))

/* Receive VMAC Multicast frame counter Debug register */
#define	RXVMAC_MCASTFM_CNT_DBG_OFF		0x0030
#define	RXVMAC_MCASTFM_CNT_DBG(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_MCASTFM_CNT_DBG_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))

/* Receive VMAC Broadcast frame counter Debug register */
#define	RXVMAC_BCASTFM_CNT_DBG_OFF		0x0038
#define	RXVMAC_BCASTFM_CNT_DBG(vni, vmac)	((RXVMAC_BASE) +	\
				(RXVMAC_BCASTFM_CNT_DBG_OFF) +	\
				((vni) * (RXVMAC_VNI_STP)) +	\
				((vmac) * (RXVMAC_VMAC_STP)))

#ifdef	__cplusplus
}
#endif

#endif /* _SXGE_VMAC_H */

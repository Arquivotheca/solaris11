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
 * Copyright (c) 2010 by Chelsio Communications, Inc.
 */

#ifndef _CXGE_OSDEP_H
#define	_CXGE_OSDEP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/byteorder.h>
#include <sys/cmn_err.h>
#include <sys/pcie.h>
#include <sys/inttypes.h>
#include <sys/note.h>

#define	isdigit(x) ((x) >= '0' && (x) <= '9')
#define	toupper(x) (((x) >= 'a' && (x) <= 'z') ? (x) - 'a' + 'A' : (x))
#define	fls ddi_fls

typedef struct adapter adapter_t, *p_adapter_t;
typedef struct port_info port_info_t, *p_port_info_t, pinfo_t;

enum {
	TP_TMR_RES = 200,	/* TP timer resolution in usec */
	MAX_NPORTS = 4,		/* max # of ports */
	TP_SRAM_OFFSET = 4096,	/* TP SRAM content offset in eeprom */
	TP_SRAM_LEN = 2112,	/* TP SRAM content offset in eeprom */
};

#define	promisc_rx_mode(rm)  (rm->pi->promisc)
#define	allmulti_rx_mode(rm) (rm->pi->allmulti)

#define	CH_ERR(adap,  ...)	cmn_err(CE_WARN, ##__VA_ARGS__)
#define	CH_WARN(adap, ...)	cmn_err(CE_WARN, ##__VA_ARGS__)
#define	CH_ALERT(adap, ...)	cmn_err(CE_NOTE, ##__VA_ARGS__)

#define	MII_BMCR	0x00
#define	MII_BMSR	0x01
#define	MII_PHYSID1	0x02
#define	MII_PHYSID2	0x03
#define	MII_ADVERTISE	0x04
#define	MII_LPA		0x05
#define	MII_EXPANSION	0x06
#define	MII_CTRL1000	0x09
#define	MII_DCOUNTER	0x12
#define	MII_FCSCOUNTER	0x13
#define	MII_NWAYTEST	0x14
#define	MII_RERRCOUNTER	0x15
#define	MII_SREVISION	0x16
#define	MII_RESV1	0x17
#define	MII_LBRERROR	0x18
#define	MII_PHYADDR	0x19
#define	MII_RESV2	0x1a
#define	MII_TPISTATUS	0x1b
#define	MII_NCONFIG	0x1c

#define	BMCR_RESV	0x007f
#define	BMCR_SPEED1000	0x0040
#define	BMCR_CTST	0x0080
#define	BMCR_FULLDPLX	0x0100
#define	BMCR_ANRESTART	0x0200
#define	BMCR_ISOLATE	0x0400
#define	BMCR_PDOWN	0x0800
#define	BMCR_ANENABLE	0x1000
#define	BMCR_SPEED100	0x2000
#define	BMCR_LOOPBACK	0x4000
#define	BMCR_RESET	0x8000

#define	BMSR_ERCAP		0x0001
#define	BMSR_JCD		0x0002
#define	BMSR_LSTATUS		0x0004
#define	BMSR_ANEGCAPABLE	0x0008
#define	BMSR_RFAULT		0x0010
#define	BMSR_ANEGCOMPLETE	0x0020
#define	BMSR_RESV		0x07c0
#define	BMSR_10HALF		0x0800
#define	BMSR_10FULL		0x1000
#define	BMSR_100HALF		0x2000
#define	BMSR_100FULL		0x4000
#define	BMSR_100BASE4		0x8000

#define	ADVERTISE_SLCT		0x001f
#define	ADVERTISE_CSMA		0x0001
#define	ADVERTISE_10HALF	0x0020
#define	ADVERTISE_1000XFULL	0x0020
#define	ADVERTISE_10FULL	0x0040
#define	ADVERTISE_1000XHALF	0x0040
#define	ADVERTISE_100HALF	0x0080
#define	ADVERTISE_1000XPAUSE	0x0080
#define	ADVERTISE_100FULL	0x0100
#define	ADVERTISE_1000XPSE_ASYM 0x0100
#define	ADVERTISE_100BASE4	0x0200
#define	ADVERTISE_PAUSE_CAP	0x0400
#define	ADVERTISE_PAUSE_ASYM	0x0800
#define	ADVERTISE_RESV		0x1c00
#define	ADVERTISE_RFAULT	0x2000
#define	ADVERTISE_LPACK		0x4000
#define	ADVERTISE_NPAGE		0x8000

#define	ADVERTISE_1000FULL	0x0200
#define	ADVERTISE_1000HALF	0x0100

/*
 * Linux compatibility macros
 */

#define	PCI_CAP_ID_EXP		PCI_CAP_ID_PCI_E
#define	PCI_EXP_DEVCTL		PCIE_DEVCTL
#define	PCI_EXP_DEVCTL_PAYLOAD	PCIE_DEVCTL_MAX_PAYLOAD_MASK
#define	PCI_EXP_DEVCTL_READRQ	PCIE_DEVCTL_MAX_READ_REQ_MASK
#define	PCI_EXP_LNKCTL		PCIE_LINKCTL
#define	PCI_EXP_LNKSTA		PCIE_LINKSTS

#define	PCI_VPD_ADDR	2
#define	PCI_VPD_ADDR_F	0x8000
#define	PCI_VPD_DATA	4

/* Some simple translations */
#define	__devinit
#define	udelay(x) drv_usecwait(x)
#define	msleep(x) drv_usecwait((x) * 1000)
#define	ARRAY_SIZE(x) (sizeof (x) / sizeof ((x)[0]))
#define	le32_to_cpu(x) LE_32(x)
#define	le16_to_cpu(x) LE_16(x)
#define	cpu_to_le32(x) LE_32(x)
#define	cpu_to_be32(x) BE_32(x)
#define	swab32(x) BSWAP_32(x)

#ifndef LINUX_TYPES_DEFINED
typedef uint8_t 	u8;
typedef uint16_t 	u16;
typedef uint32_t 	u32;
typedef uint64_t 	u64;

typedef uint8_t		__u8;
typedef uint16_t	__u16;
typedef uint32_t	__u32;
typedef uint8_t		__be8;
typedef uint16_t	__be16;
typedef uint32_t	__be32;
typedef uint64_t	__be64;
#endif

#if defined(__sparc)
#define	__BIG_ENDIAN_BITFIELD
#else
#define	__LITTLE_ENDIAN_BITFIELD
#endif

/* Indicates what features are supported by the interface. */
#define	SUPPORTED_10baseT_Half		(1 << 0)
#define	SUPPORTED_10baseT_Full		(1 << 1)
#define	SUPPORTED_100baseT_Half		(1 << 2)
#define	SUPPORTED_100baseT_Full		(1 << 3)
#define	SUPPORTED_1000baseT_Half	(1 << 4)
#define	SUPPORTED_1000baseT_Full	(1 << 5)
#define	SUPPORTED_Autoneg		(1 << 6)
#define	SUPPORTED_TP			(1 << 7)
#define	SUPPORTED_AUI			(1 << 8)
#define	SUPPORTED_MII			(1 << 9)
#define	SUPPORTED_FIBRE			(1 << 10)
#define	SUPPORTED_BNC			(1 << 11)
#define	SUPPORTED_10000baseT_Full	(1 << 12)
#define	SUPPORTED_Pause			(1 << 13)
#define	SUPPORTED_Asym_Pause		(1 << 14)

/* Indicates what features are advertised by the interface. */
#define	ADVERTISED_10baseT_Half		(1 << 0)
#define	ADVERTISED_10baseT_Full		(1 << 1)
#define	ADVERTISED_100baseT_Half	(1 << 2)
#define	ADVERTISED_100baseT_Full	(1 << 3)
#define	ADVERTISED_1000baseT_Half	(1 << 4)
#define	ADVERTISED_1000baseT_Full	(1 << 5)
#define	ADVERTISED_Autoneg		(1 << 6)
#define	ADVERTISED_TP			(1 << 7)
#define	ADVERTISED_AUI			(1 << 8)
#define	ADVERTISED_MII			(1 << 9)
#define	ADVERTISED_FIBRE		(1 << 10)
#define	ADVERTISED_BNC			(1 << 11)
#define	ADVERTISED_10000baseT_Full	(1 << 12)
#define	ADVERTISED_Pause		(1 << 13)
#define	ADVERTISED_Asym_Pause		(1 << 14)

/*
 * Enable or disable autonegotiation.  If this is set to enable,
 * the forced link modes above are completely ignored.
 */
#define	AUTONEG_DISABLE		0x00
#define	AUTONEG_ENABLE		0x01

#define	SPEED_10		10
#define	SPEED_100		100
#define	SPEED_1000		1000
#define	SPEED_10000		10000
#define	DUPLEX_HALF		0
#define	DUPLEX_FULL		1

#ifdef __cplusplus
}
#endif

#endif /* _CXGE_OSDEP_H */

/*
 * Copyright (c) 1995, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_PCN_H
#define	_PCN_H

/*
 * Generic PC-Net/LANCE Solaris driver
 *
 * Hardware specific driver declarations for the PC-Net Generic
 * driver conforming to the Generic LAN Driver model.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* debug flags */
#define	PCNTRACE	0x01
#define	PCNERRS		0x02
#define	PCNRECV		0x04
#define	PCNDDI		0x08
#define	PCNSEND		0x10
#define	PCNINT		0x20
#define	PCNPHY		0x40

#ifdef DEBUG
#define	PCNDEBUG 1
#endif

/* Misc */
#define	PCNHIWAT	32768		/* driver flow control high water */
#define	PCNLOWAT	4096		/* driver flow control low water */
#define	PCNMINPKT	64		/* minimum media frame size */
#define	PCNMAXPKT	1500		/* max media frame size (less LLC) */
#define	PCNIDNUM	0		/* should be a unique id; zero works */

/*
 * Refer to AMD Data sheets for the Am7990, Am79C90, Am79C960, Am79C970
 * for details on the definitions used here.
 */

/*
 * IO port address offsets
 */
#if defined(__amd64)
#define	_WIO
#else	/* !__amd64 */
#error Need to select DWIO or WIO for this machine type
#endif	/* __amd64 */

#if defined(_WIO)
#define	PCN_IO_ADDRESS	0x00	/* ether address PROM is here */
#define	PCN_IO_RDP	0x10	/* Register Data Port */
#define	PCN_IO_RAP	0x12	/* Register Address Port */
#define	PCN_IO_RESET	0x14	/* Reset */
#define	PCN_IO_IDP	0x16	/* ISA Bus Data Port */
#define	PCN_IO_BDP	0x16	/* Bus Configuration Register Data Port */
#define	PCN_IO_VENDOR	0x18	/* Vendor specific word */
#elif defined(_DWIO)
#define	PCN_IO_ADDRESS	0x00	/* ether address PROM is here */
#define	PCN_IO_RDP	0x10	/* Register Data Port */
#define	PCN_IO_RAP	0x14	/* Register Address Port */
#define	PCN_IO_RESET	0x18	/* Reset */
#define	PCN_IO_BDP	0x1C	/* Bus Configuration Register Data Port */
#else
#error Which I/O mode is this machine?
#endif

#define	REG32(reg, off)		((uint32_t *)((uintptr_t)(reg) + off))
#define	REG16(reg, off)		((uint16_t *)((uintptr_t)(reg) + off))
#define	REG8(reg, off)		((uint8_t *)((uintptr_t)(reg) + off))

/*
 * CSR indices
 */
#define	CSR0	0
#define	CSR1	1
#define	CSR2	2
#define	CSR3	3
#define	CSR4	4
#define	CSR5	5
#define	CSR15	15
#define	CSR58	58
#define	CSR80	80
#define	CSR88	88
#define	CSR89	89

/*
 * CSR0:
 */


#define	CSR0_INIT	(1<<0)
#define	CSR0_STRT	(1<<1)
#define	CSR0_STOP	(1<<2)
#define	CSR0_TDMD	(1<<3)
#define	CSR0_TXON	(1<<4)
#define	CSR0_RXON	(1<<5)
#define	CSR0_INEA	(1<<6)
#define	CSR0_INTR	(1<<7)
#define	CSR0_IDON	(1<<8)
#define	CSR0_TINT	(1<<9)
#define	CSR0_RINT	(1<<10)
#define	CSR0_MERR	(1<<11)
#define	CSR0_MISS	(1<<12)
#define	CSR0_CERR	(1<<13)
#define	CSR0_BABL	(1<<14)
#define	CSR0_ERR	(1<<15)

/*
 * CSR1: Initialization address.
 * CSR2: Initialization address.
 */

/*
 * CSR3:
 */

#define	CSR3_BCON    (1<<0)
#define	CSR3_ACON    (1<<1)
#define	CSR3_BSWP    (1<<2)
#define	CSR3_DXSUFLO (1<<6)
/* Write bits 31-16, 15, 13, 7, 1 and 0 as zero */
#define	CSR3_WRITEMASK (~0xffa83)

/*
 * CSR4: Test and Features Control
 */
#define	CSR4_EN124	(1<<15)
#define	CSR4_DMAPLUS	(1<<14)
#define	CSR4_TIMER	(1<<13)
#define	CSR4_DPOLL	(1<<12)
#define	CSR4_APAD_XMT	(1<<11)
#define	CSR4_ASTRP_RCV	(1<<10)
#define	CSR4_MFCO	(1<<9)
#define	CSR4_MFCOM	(1<<8)
#define	CSR4_UINTCMD	(1<<7)
#define	CSR4_UINT	(1<<6)
#define	CSR4_RCVCCO	(1<<5)
#define	CSR4_RCVCCOM	(1<<4)
#define	CSR4_TXSTRT	(1<<3)
#define	CSR4_TXSTRTM	(1<<2)
#define	CSR4_JAB	(1<<1)
#define	CSR4_JABM	(1<<0)

/*
 * CSR5: Extended control and interrupt
 */
#define	CSR5_SPND 1<<0

/*
 * CSR15:
 */
#define	CSR15_DRX	(1 << 0)	/* Disable receive */
#define	CSR15_DTX	(1 << 1)	/* Disable transmit */
#define	CSR15_LOOP	(1 << 2)	/* Loopback enable */
#define	CSR15_DXMTFCS	(1 << 3)	/* Disable transmit CRC */
#define	CSR15_FCOLL	(1 << 4)	/* Force collision */
#define	CSR15_DRTY	(1 << 5)	/* Disable retry */
#define	CSR15_INTL	(1 << 6)	/* Internal loopback */
#define	CSR15_PORTSEL	(1 << 7)	/* Port select (0 = AUI  1 = 10baseT) */
#define	CSR15_LRT_TSEL	(1 << 9)	/* Low recv thrs or xmit mode select */
#define	CSR15_MENDECL	(1 << 10)	/* MENDEC loopback mode */
#define	CSR15_DAPC	(1 << 11)	/* Disable auto polarity correction */
#define	CSR15_DLNKTST	(1 << 12)	/* Disable link status */
#define	CSR15_DRCVPA	(1 << 13)	/* Disable phhysical address */
#define	CSR15_DRCVBC	(1 << 14)	/* Disable broadcast */
#define	CSR15_PROM	(1 << 15)	/* Promiscuous mode */

/* Extended port selection for MII enabled PCN chips */
#define	PORTSEL_MASK 	(3<<7)
#define	PORTSEL_AUI	0
#define	PORTSEL_10BASET	(1 << 7)
#define	PORTSEL_GPSI	(2 << 7)
#define	PORTSEL_MII	(3 << 7)

/* Chip ID definitions */
#define	CSR88_GETVER(x)		(((x) >> 28) & 0xf) /* Extract bits 31-28 */
#define	CSR88_GETPARTID(x)	(((x) >> 12) & 0xffff) /* Extract bits 27-12 */
#define	CSR88_GETMANFID(x)	((x) >> 1 & 0x7ff) /* Extract bits 11-1 */

#define	PARTID_AM79C971 	0x2623
#define	MANFID_AMD		0x1


/*
 * BCR indices
 */
#define	BCR_MSRDA	0
#define	BCR_MSWRA	1
#define	BCR_MC		2
#define	BCR_LNKST	4
#define	BCR_LED1	5
#define	BCR_LED2	6
#define	BCR_LED3	7
#define	BCR_IOBASEL	16
#define	BCR_IOBASEU	17
#define	BCR_BSBC	18
#define	BCR_EECAS	19
#define	BCR_SWS		20
#define	BCR_INTCON	21
#define	BCR_MIICS	32	/* MII control and status register */
#define	BCR_MIIAR	33	/* MII Address register */
#define	BCR_MIIDR	34	/* MII Data Register */

/*
 * BCR_MC definitions
 */
#define	BCR_MC_AUTO_SELECT	2

/*
 * BCR_SWS definitions
 */
#define	BCR_SWS_PCNET_PCI	2

/*
 * BCR_MIICS definitions
 */
#define	BCR_MIICS_ANTST		(1<<15) /* Test bit. Do not use */
#define	BCR_MIICS_MIIPD		(1<<14)	/* PHY Detect */
#define	BCR_MIICS_FMDCM		(3<<12)	/* PHY Fast Management Data Clock */
#define	BCR_MIICS_APEP		(1<<11)	/* Auto Poll External PHY */
#define	BCR_MIICS_APDWM		(7<<8)	/* Auto Poll Dwell time	*/
#define	BCR_MIICS_DANAS		(1<<7)	/* Disable auto neg. on startup */
#define	BCR_MIICS_XPHYRST	(1<<6)	/* External PHY Reset */
#define	BCR_MIICS_XPHYANE	(1<<5)	/* External PHY Autoneg. enable */
#define	BCR_MIICS_XPHYFD	(1<<4)	/* External PHY Full Duplex */
#define	BCR_MIICS_XPHYSP	(1<<3)	/* External PHY speed */
#define	BCR_MIICS_MIImL		(1<<2)	/* Work with Micolinear 6692 PHY */
#define	BCR_MIICS_MIIIL		(1<<1)	/* MII Internal loopback */
#define	BCR_MIICS_FCON		(1<<0)	/* Fast Config mode */


#define	BCR_BSBC_BREADE		(1<<6)
#define	BCR_BSBC_BWRITE		(1<<5)
#define	BCR_BSBC_TSTSHDW	(3<<3)
#define	BCR_BSBC_LINBC		(3<<0)

/*
 * Structure definitions for adapter access.
 * These structures assume no padding between members.
 */

/*
 * Initialization block
 */

#if defined(_BIT_FIELDS_LTOH)
#if defined(SSIZE32)
struct PCN_InitBlock {
	ushort_t	MODE;
	uchar_t		: 4;
	uchar_t 	RLEN : 4;
	uchar_t		: 4;
	uchar_t		TLEN : 4;
	ushort_t	PADR[3];
	uint32_t	LADRF[2];
	uint32_t	RDRA;
	uint32_t	TDRA;
};
#else
struct PCN_InitBlock {
	ushort_t	MODE;
	ushort_t	PADR[3];
	ushort_t	LADRF[4];
	ushort_t	RDRAL;
	ushort_t 	RDRAU : 8;
	ushort_t	RRES: 4;
	ushort_t	RZERO : 1;
	ushort_t	RLEN : 3;
	ushort_t	TDRAL;
	ushort_t 	TDRAU : 8;
	ushort_t	TRES : 4;
	ushort_t	TZERO : 1;
	ushort_t	TLEN : 3;
};
#endif
#else /* ! defined(_BIT_FIELDS_LTOH) */
#error Only low to high bit field description present
#endif /* ! defined(_BIT_FIELDS_LTOH) */

/*
 * MODE:
 */

#define	MODE_DRX	(1<<0)
#define	MODE_DTX	(1<<1)
#define	MODE_LOOP	(1<<2)
#define	MODE_DTCR	(1<<3)
#define	MODE_COLL	(1<<4)
#define	MODE_DRTY	(1<<5)
#define	MODE_INTL	(1<<6)
#define	MODE_EMBA	(1<<7)
#define	MODE_PROM	(1<<15)

/*
 * Message Descriptor
 *
 */

#if defined(_BIT_FIELDS_LTOH)
#if defined(SSIZE32)

union PCN_RecvMsgDesc {
	struct {
		uint32_t rbadr;			/* buffer address */
		volatile uint32_t bcnt : 12;	/* buffer byte count */
		volatile uint32_t ones : 4;	/* must be ones */
		volatile uint32_t : 8;
		volatile uint32_t enp : 1;	/* end of packet */
		volatile uint32_t stp : 1;	/* start of packet */
		volatile uint32_t buff : 1;	/* buffer error */
		volatile uint32_t crc : 1;	/* CRC error */
		volatile uint32_t oflo : 1;	/* overflow error */
		volatile uint32_t fram : 1;	/* framing error */
		volatile uint32_t err : 1;	/* or'ing of error bits */
		volatile uint32_t own : 1;	/* owner */
		volatile uint32_t mcnt : 12;	/* message byte count */
		volatile uint32_t zeros : 4;	/* will be zeros */
		volatile uint32_t rpc : 8;	/* runt packet count */
		volatile uint32_t rcc : 8;	/* receive collision count */
		uint32_t reserved;
	} rmd_bits;
	uint32_t rmd[4];
};

union PCN_XmitMsgDesc {
	struct {
		uint32_t tbadr;			/* buffer address */
		volatile uint32_t bcnt: 12;	/* buffer byte count */
		volatile uint32_t ones : 4;	/* must be ones */
		volatile uint32_t : 8;
		volatile uint32_t enp : 1;	/* end of packet */
		volatile uint32_t stp : 1;	/* start of packet */
		volatile uint32_t def : 1;	/* deferred */
		volatile uint32_t one : 1;	/* one retry required */
		volatile uint32_t more : 1;	/* more than 1 retry required */
		volatile uint32_t add_fcs : 1;	/* add FCS */
		volatile uint32_t err : 1;	/* or'ing of error bits */
		volatile uint32_t own : 1;	/* owner */
		volatile uint32_t trc : 4;	/* transmit retry count */
		volatile uint32_t : 12;
		volatile uint32_t tdr : 10;	/* time domain reflectometry */
		volatile uint32_t rtry : 1;	/* retry error */
		volatile uint32_t lcar : 1; 	/* loss of carrier error */
		volatile uint32_t lcol : 1;	/* late collision error */
		volatile uint32_t exdef : 1;	/* excessive deferral */
		volatile uint32_t uflo : 1;	/* underflow */
		volatile uint32_t buff : 1;	/* buffer error */
		uint32_t reserved;
	} tmd_bits;
	uint32_t tmd[4];
};

#else

union PCN_RecvMsgDesc {
	struct {
		ushort_t rbadrl;		/* lsbs of addr */
		volatile ushort_t rbadru : 8;	/* msbs of addr */
		volatile ushort_t enp : 1;	/* end of packet */
		volatile ushort_t stp : 1;	/* start of packet */
		volatile ushort_t buff : 1;	/* buffer error */
		volatile ushort_t crc : 1;	/* CRC error */
		volatile ushort_t oflo : 1;	/* overflow error */
		volatile ushort_t fram : 1;	/* framing error */
		volatile ushort_t err : 1;	/* frame error */
		volatile ushort_t own : 1;	/* owner (0 = host 1 = PCnet) */
		volatile ushort_t bcnt : 12;	/* buffer byte count */
		ushort_t ones : 4;		/* must be ones */
		volatile ushort_t mcnt : 12;	/* message byte count */
		ushort_t zeros : 4;		/* will be zeros */
	} rmd_bits;
	ushort_t rmd[4];
};

union PCN_XmitMsgDesc {
	struct {
		ushort_t tbadrl;		/* lsbs of addr */
		volatile ushort_t tbadru : 8;	/* msbs of addr */
		volatile ushort_t enp : 1;	/* end of packet */
		volatile ushort_t stp : 1;	/* start of packet */
		volatile ushort_t def : 1;	/* deferred */
		volatile ushort_t one : 1;	/* one retry required */
		volatile ushort_t more : 1;	/* more than 1 retry required */
		volatile ushort_t add_fcs : 1;	/* add FCS */
		volatile ushort_t err : 1;	/* frame error */
		volatile ushort_t own : 1;	/* owner (0 = host 1 = PCnet) */
		volatile ushort_t bcnt : 12;	/* buffer byte count */
		ushort_t ones : 4;		/* must be ones */
		volatile ushort_t tdr : 10;	/* time domain reflectometry */
		ushort_t rtry : 1;		/* retry error */
		ushort_t lcar : 1;		/* loss of carrier */
		ushort_t lcol : 1;		/* late collision */
		ushort_t exdef : 1;		/* excessive deferral */
		ushort_t uflo : 1;		/* underflow error */
		ushort_t buff : 1;		/* buffer error */
	} tmd_bits;
	ushort_t tmd[4];
};
#endif
#else /* ! defined(_BIT_FIELDS_LTOH) */
#error Only low to high bit field description present
#endif /* ! defined(_BIT_FIELDS_LTOH) */

/*
 * PCI Constants
 */
#define	PCI_AMD_VENDOR_ID	0x1022
#define	PCI_PCNET_ID		0x2000
#define	PCI_PCNET_BASE_ADDR	0x10
#define	PCI_PCNET_IRQ_ADDR	0x3C


/*
 * Bus scan array
 */

#define	PCN_IOBASE_ARRAY_SIZE	16

struct pcnIOBase {
	ushort_t	iobase;
	int	irq;
	int	dma;
	int	bustype;
	uint32_t	cookie;
};

#define	PCN_BUS_ISA	0
#define	PCN_BUS_EISA	1
#define	PCN_BUS_PCI	2
#define	PCN_BUS_UNKNOWN -1


/*
 * LANCE/Ethernet constants
 */
#define	LANCE_FCS_SIZE	4
#define	LADRF_LEN	64

/*
 * Buffer ring definitions
 */

#define	PCN_RX_RING_VAL		7
#define	PCN_RX_RING_SIZE	(1<<PCN_RX_RING_VAL)
#define	PCN_RX_RING_MASK	(PCN_RX_RING_SIZE-1)
#define	PCN_RX_BUF_SIZE		128

#define	PCN_TX_RING_VAL		7
#define	PCN_TX_RING_SIZE	(1<<PCN_TX_RING_VAL)
#define	PCN_TX_RING_MASK	(PCN_TX_RING_SIZE-1)
#define	PCN_TX_BUF_SIZE		128

#define	NextRXIndex(index)	(((index)+1)&PCN_RX_RING_MASK)
#define	NextTXIndex(index)	(((index)+1)&PCN_TX_RING_MASK)
#define	PrevTXIndex(index)	(((index)-1)&PCN_TX_RING_MASK)

struct PCN_IOmem {
	struct PCN_IOmem    *next;
	void		*vbase;		/* virtual base address */
	void		*vptr;		/* virtual current pointer */
	uintptr_t	pbase;		/* physical base address */
	uintptr_t	pptr;		/* physical current pointer */
	size_t		avail;		/* number of bytes available */
	ddi_dma_handle_t dma_hdl;	/* DMA handle */
	ddi_acc_handle_t acc_hdl;	/* data access handle */
};

enum pcn_device_type {
	pcn_isa,
	pcn_isaplus,
	pcn_pci,
	pcn_pciII,
	pcn_fast,
	pcn_unknown
};

/*
 * Each attached PCN is described by this structure
 */
struct pcninstance {
	dev_info_t	*devinfo;
	ddi_acc_handle_t	io_handle;
	uintptr_t		io_reg;
	int	dma_attached;
	int	init_intr;
	int	irq_scan;
	int	dma_scan;

	/*
	 * Memory allocator management
	 */
	size_t	page_size;
	struct PCN_IOmem *iomemp;

	/*
	 * Multi-cast list management
	 */
	int	mcref[LADRF_LEN];

	/*
	 * Init block management
	 */
	struct PCN_InitBlock *initp;
	caddr_t	phys_initp;

	/*
	 * Receive ring management
	 */
	void	*rx_ringp;
	int	rx_index;
	uchar_t	*rx_buf[PCN_RX_RING_SIZE];

	/*
	 * Transmit ring management
	 */
	void	*tx_ringp;
	int	tx_index;	/* next ring index to use */
	int	tx_index_save;	/* save of TX index for ISR/stats collection */
	int	tx_avail;	/* # of descriptors available */
	uchar_t	*tx_buf[PCN_TX_RING_SIZE];

	/*
	 * MII interface
	 */
	int phy_supported;	/* The device supports MII like the '71 */
	enum pcn_device_type devtype;

	/* Single lock used to protect all fields */
	kmutex_t	intrlock;

	/* flag used to note when we will need to call gld_sched */
	int	need_gld_sched;

	unsigned char	macaddr[ETHERADDRL];	/* Factory mac address */

	uint32_t	glds_collisions;
	uint32_t	glds_crc;
	uint32_t	glds_defer;
	uint32_t	glds_errxmt;
	uint32_t	glds_excoll;
	uint32_t	glds_frame;
	uint32_t	glds_intr;
	uint32_t	glds_missed;
	uint32_t	glds_nocarrier;
	uint32_t	glds_norcvbuf;
	uint32_t	glds_overflow;
	uint32_t	glds_underflow;
	uint32_t	glds_xmtlatecoll;

};

#ifdef __cplusplus
}
#endif

#endif	/* _PCN_H */

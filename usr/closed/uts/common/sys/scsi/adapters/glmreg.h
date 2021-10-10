/*
 * Copyright (c) 1994, 2000, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_SCSI_ADAPTERS_GLMREG_H
#define	_SYS_SCSI_ADAPTERS_GLMREG_H

#ifdef	__cplusplus
extern "C" {
#endif

enum glm53c8xxregs {		/* To access NCR 53C8xx registers */
NREG_SCNTL0 =	0x00,	NREG_SCNTL1,	NREG_SCNTL2,	NREG_SCNTL3,
NREG_SCID,		NREG_SXFER,	NREG_SDID,	NREG_GPREG,
NREG_SFBR,		NREG_SOCL,	NREG_SSID,	NREG_SBCL,
NREG_DSTAT,		NREG_SSTAT0,	NREG_SSTAT1,	NREG_SSTAT2,
NREG_DSA,
NREG_ISTAT =	0x14,
NREG_CTEST0 =	0x18,	NREG_CTEST1,	NREG_CTEST2,	NREG_CTEST3,
NREG_TEMP,
NREG_DFIFO =	0x20,	NREG_CTEST4,	NREG_CTEST5,	NREG_CTEST6,
NREG_DBC,						NREG_DCMD = 0x27,
NREG_DNAD,
NREG_DSP =	0x2c,
NREG_DSPS =	0x30,
NREG_SCRATCHA =	0x34,
NREG_SCRATCHA0 = 0x34,	NREG_SCRATCHA1,	NREG_SCRATCHA2, NREG_SCRATCHA3,
NREG_DMODE,		NREG_DIEN,	NREG_DWT,	NREG_DCNTL,
NREG_ADDER,

NREG_SIEN0 =	0x40,	NREG_SIEN1,	NREG_SIST0,	NREG_SIST1,
NREG_SLPAR,		NREG_RESERVED,	NREG_MACNTL,	NREG_GPCNTL,
NREG_STIME0,		NREG_STIME1,	NREG_RESPID,
NREG_STEST0 = 0x4c, 	NREG_STEST1,	NREG_STEST2,	NREG_STEST3,
NREG_SIDL,
NREG_STEST4 = 0x52,
NREG_SODL = 0x54,
NREG_CCNTL0 = 0x56,
NREG_CCNTL1 = 0x57,
NREG_SBDL = 0x58,
NREG_SCRATCHB = 0x5c,
NREG_SCRATCHB0 = 0x5c,	NREG_SCRATCHB1,	NREG_SCRATCHB2, NREG_SCRATCHB3,
NREG_SCNTL4 = 0xBC,
NREG_AIPCNTL0 = 0xBE,
NREG_AIPCNTL1 = 0xBF,
NREG_PMJAD1 = 0xC0,
NREG_PMJAD2 = 0xC4,
NREG_RBC = 0xC8,
NREG_UA = 0xCC,
NREG_ESA = 0xD0,
NREG_IA = 0xD4,
NREG_SBC = 0xD8,
NREG_CSBC = 0xDC,
NREG_CRCPAD = 0xE0,
NREG_CRCCNTL0 = 0xE2,
NREG_CRCCNTL1 = 0xE3,
NREG_CRCD = 0xE4,
NREG_DFBC = 0xf0
};

/*
 * These bits are used to decode DMA/chip errors.
 */
#define	dstatbits	\
"\020\010DMA-FIFO-empty\
\07master-data-parity-error\
\06bus-fault\
\05aborted\
\04single-step-interrupt\
\03SCRIPTS-interrupt-instruction\
\02reserved\
\01illegal-instruction"

/*
 * Device ids.
 */
#define	GLM_53c810	0x1
#define	GLM_53c825	0x3
#define	GLM_53c875	0xf
#define	GLM_53c895	0xc
#define	GLM_53c896	0xb
#define	GLM_53c1010_33	0x20
#define	GLM_53c1010_66	0x21

/*
 * Revisons.
 */
#define	REV1	0x1
#define	REV2	0x2
#define	REV3	0x3
#define	REV6	0x6
#define	REV7	0x7
#define	GLM_REV(glm)	(uchar_t)(glm->g_revid & 0xf)
#define	IS_876(glm)	(uchar_t)(glm->g_revid & 0x10)
#define	IS_810A(glm)	(uchar_t)(glm->g_revid & 0x10)

/*
 * default hostid.
 */
#define	DEFAULT_HOSTID	7

/*
 * Default Synchronous offset.
 * (max # of allowable outstanding REQ)
 */
#define	GLM_895_OFFSET	31
#define	GLM_875_OFFSET	16
#define	GLM_OFFSET_8	 8

#define	SYNC_OFFSET(glm) \
	(((glm->g_devid == GLM_53c895) || \
	(glm->g_devid == GLM_53c896) || \
	(glm->g_devid == GLM_53c1010_33) || \
	(glm->g_devid == GLM_53c1010_66)) ? GLM_895_OFFSET : \
	((glm->g_devid == GLM_53c875 ? GLM_875_OFFSET : GLM_OFFSET_8)))

/*
 * Sync periods.
 */
#define	DEFAULT_SYNC_PERIOD		200	/* 5.0 MB/s */
#define	DEFAULT_FASTSYNC_PERIOD		100	/* 10.0 MB/s */
#define	DEFAULT_FAST20SYNC_PERIOD	50	/* 20.0 MB/s */
#define	DEFAULT_FAST40SYNC_PERIOD	25	/* 40.0 MB/s */

/*
 * This yields nanoseconds per input clock tick
 */
#define	CONVERT_PERIOD(time)	(((time)<<2)/100)

#define	GLM_GET_PERIOD(ns) \
	(ns == 0xa) ? 25 : \
	(ns == 0xb) ? 30 : \
	(ns == 0xc) ? ((ns * 4) + 2) : (ns * 4)

#define	GLM_GET_DT_PERIOD(ns) \
	(ns == 0x9) ? 25 : \
	(ns == 0xa) ? 30 : \
	(ns == 0xb) ? ((ns * 4) + 2) : (ns * 4)

#define	GLM_GET_SYNC(ns) \
	(ns == 25) ? 0xa : \
	(ns == 30) ? 0xb : \
	(ns == 50) ? 0xc : (ns / 4)

#define	GLM_GET_DT_SYNC(ns) \
	(ns == 25) ? 0x9 : \
	(ns == 30) ? 0xa : \
	(ns == 50) ? 0xb : (ns / 4)

/*
 * Max/Min number of clock cycles for synchronous period
 */
#define	MAX_TP			11
#define	MIN_TP			4
#define	MAX_SYNC_PERIOD(glm) \
	((glm->g_speriod * glm_ccf[(glm->g_max_div - 1)] * MAX_TP) / 1000)

/*
 * Config space.
 */
#define	GLM_LATENCY_TIMER	0x40

/*
 * defines for onboard 4k SRAM
 */
#define	GLM_HBA_DSA_ADDR_OFFSET	0xffc

/*
 * Bit definitions for the ISTAT (Interrupt Status) register
 */
#define	NB_ISTAT_ABRT		0x80	/* abort operation */
#define	NB_ISTAT_SRST		0x40	/* software reset */
#define	NB_ISTAT_SEMA		0x10	/* Semaphore bit */
#define	NB_ISTAT_SIGP		0x20	/* signal process */

/*
 * Bit definitions for the DSTAT (DMA Status) register
 */
#define	NB_DSTAT_DFE	0x80	/* DMA FIFO empty */
#define	NB_DSTAT_MDPE	0x40	/* master data parity error */
#define	NB_DSTAT_BF	0x20	/* bus fault */
#define	NB_DSTAT_ABRT	0x10	/* aborted */
#define	NB_DSTAT_SSI 	0x08	/* SCRIPT step interrupt */
#define	NB_DSTAT_SIR	0x04	/* SCRIPT interrupt instruction */
#define	NB_DSTAT_RES	0x02	/* reserved */
#define	NB_DSTAT_IID	0x01	/* illegal instruction detected */

/*
 * Just the unexpected fatal DSTAT errors
 */
#define	NB_DSTAT_ERRORS		(NB_DSTAT_MDPE | NB_DSTAT_BF | NB_DSTAT_ABRT \
				| NB_DSTAT_SSI | NB_DSTAT_IID)

/*
 * Bit definitions for the SIST0 (SCSI Interrupt Status Zero) register
 */
#define	NB_SIST0_MA	0x80	/* initiator: Phase Mismatch, or */
				/* target: ATN/ active */
#define	NB_SIST0_CMP	0x40	/* function complete */
#define	NB_SIST0_SEL	0x20	/* selected */
#define	NB_SIST0_RSL	0x10	/* reselected */
#define	NB_SIST0_SGE	0x08	/* SCSI gross error */
#define	NB_SIST0_UDC	0x04	/* unexpected disconnect */
#define	NB_SIST0_RST	0x02	/* SCSI RST/ (reset) received */
#define	NB_SIST0_PAR	0x01	/* parity error */

/*
 * Bit definitions for the SIST1 (SCSI Interrupt Status One) register
 */
#define	NB_SIST1_SBMC	0x10	/* SCSI bus mode change */
#define	NB_SIST1_STO	0x04	/* selection or reselection time-out */
#define	NB_SIST1_GEN	0x02	/* general purpose timer expired */
#define	NB_SIST1_HTH	0x01	/* handshake-to-handshake timer expired */

/*
 * Miscellaneous other bits that have to be fiddled
 */
#define	NB_SCNTL0_EPC		0x08	/* enable parity checking */
#define	NB_SCNTL0_AAP		0x02	/* Assert ATN on Parity error */

#define	NB_SCNTL1_CON		0x10	/* connected */
#define	NB_SCNTL1_RST		0x08	/* assert scsi reset signal */

#define	NB_SCID_RRE		0x40	/* enable response to reselection */
#define	NB_SCID_ENC		0x0f	/* encoded chip scsi id */

#define	NB_GPREG_GPIO3		0x08	/* low if differential board. */

#define	NB_SSID_VAL		0x80	/* scsi id valid bit */
#define	NB_SSID_ENCID		0x0f	/* encoded destination scsi id */

#define	NB_SSTAT0_ILF		0x80	/* scsi input data latch full */
#define	NB_SSTAT0_ORF		0x40	/* scsi output data register full */
#define	NB_SSTAT0_OLF		0x20	/* scsi output data latch full */

#define	NB_SSTAT2_ILF1		0x80	/* scsi input data latch1 full. */
#define	NB_SSTAT2_ORF1		0x40	/* scsi output data register1 full */
#define	NB_SSTAT2_OLF1		0x20	/* scsi output data latch1 full */

#define	NB_SSTAT1_FF		0xf0	/* scsi fifo flags */
#define	NB_SSTAT1_PHASE		0x07	/* current scsi phase */

#define	NB_CTEST0_NOARB		0x20	/* disable overlapped arbitration */

#define	NB_CTEST2_DDIR		0x80	/* data transfer direction */

#define	NB_CTEST3_VMASK		0xf0	/* chip revision level */
#define	NB_CTEST3_VERSION	0x10	/* expected chip revision level */
#define	NB_CTEST3_CLF		0x04	/* clear dma fifo */

#define	NB_CTEST4_MPEE		0x08	/* master parity error enable */

#define	NB_CTEST5_DFS		0x20	/* Sets dma fifo size to 536 bytes. */
#define	NB_CTEST5_BL2		0x04	/* Used w/DMODE reg for burst size. */

#define	NB_DIEN_MDPE		0x40	/* master data parity error */
#define	NB_DIEN_BF		0x20	/* bus fault */
#define	NB_DIEN_ABRT		0x10	/* aborted */
#define	NB_DIEN_SSI		0x08	/* SCRIPT step interrupt */
#define	NB_DIEN_SIR		0x04	/* SCRIPT interrupt instruction */
#define	NB_DIEN_IID		0x01	/* Illegal instruction detected */

#define	NB_DCNTL_COM		0x01	/* 53c700 compatibility */

#define	NB_SIEN0_MA		0x80	/* Initiator: Phase Mismatch, or */
					/* Target: ATN/ active */
#define	NB_SIEN0_CMP		0x40	/* function complete */
#define	NB_SIEN0_SEL		0x20	/* selected */
#define	NB_SIEN0_RSL		0x10	/* reselected */
#define	NB_SIEN0_SGE		0x08	/* SCSI gross error */
#define	NB_SIEN0_UDC		0x04	/* unexpected disconnect */
#define	NB_SIEN0_RST		0x02	/* SCSI reset condition */
#define	NB_SIEN0_PAR		0x01	/* SCSI parity error */

#define	NB_SIEN1_SBMC		0x10	/* SCSI bus mode change */
#define	NB_SIEN1_STO		0x04	/* selection or reselection time-out */
#define	NB_SIEN1_GEN		0x02	/* general purpose timer expired */
#define	NB_SIEN1_HTH		0x01	/* handshake-to-handshake timer */
					/* expired */
#define	NB_STIME0_SEL		0x0f	/* selection time-out bits */
#define	NB_STIME0_1MS		0x04	/* 1 ms selection timeout */
#define	NB_STIME0_2MS		0x05	/* 2 ms selection timeout */
#define	NB_STIME0_4MS		0x06	/* 4 ms selection timeout */
#define	NB_STIME0_8MS		0x07	/* 8 ms selection timeout */
#define	NB_STIME0_16MS		0x08	/* 16 ms selection timeout */
#define	NB_STIME0_32MS		0x09	/* 32 ms selection timeout */
#define	NB_STIME0_64MS		0x0a	/* 64 ms selection timeout */
#define	NB_STIME0_128MS		0x0b	/* 128 ms selection timeout */
#define	NB_STIME0_256MS		0x0c	/* 256 ms selection timeout */

#define	NB_STEST3_CSF		0x02	/* clear SCSI FIFO */
#define	NB_STEST3_DSI		0x10	/* disable single initiator response */
#define	NB_STEST3_HSC		0x20	/* Halt SCSI Clock */
#define	NB_STEST3_TE		0x80	/* tolerANT enable */

#define	NB_STEST4_LOCK		0x20	/* PLL has locked. */
#define	NB_STEST4_HVD		0x40	/* High Voltage Differential */
#define	NB_STEST4_SE		0x80	/* Single Ended */
#define	NB_STEST4_LVD		0xc0	/* Low Voltage Differential */

#define	NB_CCNTL0_DPR		0x01	/* Disable Pipe Req */
#define	NB_CCNTL0_DILS		0x02	/* Disable Internal Load/Store */
#define	NB_CCNTL0_DISFC		0x10	/* Disable Auto FIFO Clear */
#define	NB_CCNTL0_ENNDJ		0x20	/* Enable on Nondata Phase Mismatches */
#define	NB_CCNTL0_PMJCTL	0x40	/* Jump Control */
#define	NB_CCNTL0_ENPMJ		0x80	/* Enable Phase Mismatch Jump */

#define	NB_CCNTL1_EN64DBMV	0x01	/* Enable 64bit Direct BMOV */
#define	NB_CCNTL1_EN64TIBMV	0x02	/* Enable 64but Table Indirect BMOV */
#define	NB_CCNTL1_64TIMO	Dx04	/* 64bit Table Indirect Indexing Mode */
#define	NB_CCNTL1_DDAC		0x08	/* Disable Dual Address Cycle */
#define	NB_CCNTL1_ZMOD		0x80	/* High Impedance Mode */

#define	NB_SCNTL1_EXC		0x80	/* extra clock cycle of data setup */

#define	NB_SCNTL3_ULTRA		0x80	/* enable UltraSCSI timings */
#define	NB_SCNTL3_SCF		0x70	/* synch. clock conversion factor */
#define	NB_SCNTL3_SCF1		0x10	/* SCLK / 1 */
#define	NB_SCNTL3_SCF15		0x20	/* SCLK / 1.5 */
#define	NB_SCNTL3_SCF2		0x30	/* SCLK / 2 */
#define	NB_SCNTL3_SCF3		0x00	/* SCLK / 3 */
#define	NB_SCNTL3_SCF4		0x50	/* SCLK / 4 */
#define	NB_SCNTL3_SCF6		0x60	/* SCLK / 6 */
#define	NB_SCNTL3_SCF8		0x70	/* SCLK / 8 */
#define	NB_SCNTL3_CCF		0x07	/* clock conversion factor */
#define	NB_SCNTL3_CCF1		0x01	/* SCLK / 1 */
#define	NB_SCNTL3_CCF15		0x02	/* SCLK / 1.5 */
#define	NB_SCNTL3_CCF2		0x03	/* SCLK / 2 */
#define	NB_SCNTL3_CCF3		0x00	/* SCLK / 3 */
#define	NB_SCNTL3_CCF4		0x05	/* SCLK / 4 */
#define	NB_SCNTL3_CCF6		0x06	/* SCLK / 6 */
#define	NB_SCNTL3_CCF8		0x07	/* SCLK / 8 */
#define	NB_SCNTL3_EWS		0x08	/* Enable wide scsi bit. */

#define	NB_CTEST4_BDIS		0x80	/* burst disable */

#define	NB_DMODE_BOF		0x02	/* Burst Op Code Fetch Enable */
#define	NB_DMODE_ERL		0x08	/* Enable Read Line */
#define	NB_DMODE_BL		0x40	/* burst length */
#define	NB_825_DMODE_BL		0xc0	/* burst length for 53c825 */

#define	NB_DCNTL_IRQM		0x08	/* IRQ mode */
#define	NB_DCNTL_CLSE		0x80	/* Cache Line Size Enable mode */
#define	NB_DCNTL_PFEN		0x20	/* Prefetch enable mode */

/*
 * Depending on the part, the PLL (phase lock loop) will either double
 * or quadruple the clock.
 */
#define	NB_STEST1_PLLSEL	0x04	/* PLL Select */
#define	NB_STEST1_PLLEN		0x08	/* PLL Enable */
#define	NB_STEST1_SCLK		0x80	/* disable external SCSI clock */

#define	NB_STEST2_EXT		0x02	/* extend SREQ/SACK filtering */
#define	NB_STEST2_ROF		0x40	/* Reset SCSI Offset */
#define	NB_STEST2_DIFF		0x20	/* SCSI Differential Mode */

#define	NB_SCNTL4_U3EN		0x80	/* Ultra3 Transfer Enable */
#define	NB_SCNTL4_AIPEN		0x40	/* Async Infor Protection Enable */
#define	NB_SCNTL4_XCLKH_DT	0x08	/* Xtra Clk Hold On DT Transfer Edge */
#define	NB_SCNTL4_XCLKH_ST	0x04	/* Xtra Clk Hold On ST Transfer Edge */
#define	NB_SCNTL4_XCLKS_DT	0x02	/* Xtra Clk Hold On DT Setup Edge */
#define	NB_SCNTL4_XCLKS_ST	0x01	/* Xtra Clk Hold On ST Setup Edge */

#define	NB_AIPCNTL0_AIPERR_LIVE	0x04	/* AIP Error Status Live */
#define	NB_AIPCNTL0_AIPERR	0x02	/* AIP Error Status */
#define	NB_AIPCNTL0_PARITYERR	0x01	/* Parity Error Status */

#define	NB_AIPCNTL1_DISAIP	0x08	/* Disable AIP Code Generation */
#define	NB_AIPCNTL1_RAIPERR	0x04	/* Reset AIP Error */
#define	NB_AIPCNTL1_FBAIP	0x02	/* Force Bad AIP Value */
#define	NB_AIPCNTL1_RSQ		0x01	/* Reset AIP Sequence Value */

#define	NB_CRCCNTL0_DCRCC	0x80	/* Disable CRC Checkin */
#define	NB_CRCCNTL0_DCRCPC	0x40	/* Disable CRC Protocol Checking */
#define	NB_CRCCNTL0_RCRCIC	0x20	/* Reset CRC Interval Counter */

#define	NB_CRCCNTL1_CRCERR	0x80	/* CRC Error */
#define	NB_CRCCNTL1_ENAS	0x20	/* Enable CRC Auto Seed */
#define	NB_CRCCNTL1_TSTSD	0x10	/* Test CRC Seed */
#define	NB_CRCCNTL1_TSTCHK	0x08	/* Test CRC Check */
#define	NB_CRCCNTL1_TSTADD	0x04	/* Test CRC Accumulate */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_GLMREG_H */

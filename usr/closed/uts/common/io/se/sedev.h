/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_SEDEV_H
#define	_SYS_SEDEV_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Onboard high-speed serial ports.
 * Device dependent software definitions.
 */

/*
 * Chip, buffer, and register definitions for SAB 82532 ESCC2
 */

#include <sys/ksynch.h>
#include <sys/dditypes.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* number of soft state items */
#define	SE_INITIAL_SOFT_ITEMS	2

/*
 * Defines for hdlc watchdog timer
 */
#define	SE_HDLC_WD_TIMER_TICK   (2 * hz)  /* watchdog triggered every 2 secs */
#define	SE_HDLC_WD_FIRST_WARNING_CNT  5   /* show warning after this # ticks */
#define	SE_HDLC_WD_REPEAT_WARNING_CNT 155 /* repeat warning after # ticks */



/*
 * OUTLINE defines the high-order flag bit in the minor device number that
 * controls use of a tty line for dialin and dialout simultaneously.
 */
#define	ASYNC_DEVICE    (0)
#define	OUTLINE		(1 << (NBITSMINOR32 - 1))
#define	HDLC_DEVICE	(1 << (NBITSMINOR32 - 2))
#define	CLONE_DEVICE	(1 << (NBITSMINOR32 - 3))
/*
 * SSP_DEVICE defines the bit in the minor device number that specifies
 * the tty line is to be used for console/controlling a SSP device.
 */
#define	SSP_DEVICE	(1 << (NBITSMINOR32 - 4))

#define	SE_MAX_RSTANDBY 6
#define	SE_MAX_RDONE    20

/* Define the maximum number of ports per chip. This number doesn't affect */
/* any data allocations, but changes the minor number which is visible to  */
/* the end user. The largest number of ports per chip is on the ESCC8, and */
/* siemens does not expect to build a larger chip. */

#define	SE_PORTS	8	/* Number of ports per chip */
#define	SE_CURRENT_NPORTS	2 /* currently we have two ports a and b */

/*
 * Modem control commands.
 */
#define	DMSET   0
#define	DMBIS   1
#define	DMBIC   2
#define	DMGET   3

#define	SE_REG_SIZE	0x40	/* Size of escc2 register file */
#define	SAB_FIFO_SIZE   0x20	/* Size of fifo (32 bytes) */

/*
 * Define write registers for SAB 82532 chip in async mode
 * Note - should be volatile, but is always used with ddi_get/put,
 * so it doesn't matter.
 */

typedef
    struct se_regs {
	    uchar_t sab_xfifo[SAB_FIFO_SIZE]; /* transmit fifo */
	    uchar_t sab_cmdr;	/* Command register */
	    uchar_t sab_pre;	/* Preamble register (hdlc only) */
	    uchar_t sab_mode;	/* Mode register */
	    uchar_t sab_timr;	/* Timer register */
	    uchar_t sab_xon;	/* Xon character */
	    uchar_t sab_xoff;	/* xoff character */
	    uchar_t sab_tcr;	/* Termination character */
	    uchar_t sab_dafo;	/* Data format */
	    uchar_t sab_rfc;	/* rfifo control register */
	    uchar_t sab_ral2;	/* receive address low register (hdlc only) */
	    uchar_t sab_xbcl;	/* Transmit byte count low */
	    uchar_t sab_xbch;	/* transmit byte count high */
	    uchar_t sab_ccr0;	/* Channel configuration register 0 */
	    uchar_t sab_ccr1;	/* Channel configuration register 1 */
	    uchar_t sab_ccr2;	/* Channel configuration register 2 */
	    uchar_t sab_ccr3;	/* Channel configuration register 3 */
	    uchar_t sab_tsax;	/* Time-slot assignment register transmit */
	    uchar_t sab_tsar;	/* Time-slot assignment register receive */
	    uchar_t sab_xccr;	/* transmit channel capacity register */
	    uchar_t sab_rccr;	/* receive channel capacity register */
	    uchar_t sab_bgr;	/* Baud Rate generator register */
	    uchar_t sab_tic;	/* Transmit immediate character */
	    uchar_t sab_mxn;	/* Mask for XON character */
	    uchar_t sab_mxf;	/* Mask for XOFF character */
	    uchar_t sab_iva;	/* Interrupt Vector Address */
	    uchar_t sab_ipc;	/* Interrupt port configuration */
	    ushort_t sab_imr;	/* Interrupt mask (bytes 0 and 1) */
	    uchar_t sab_pvr;	/* Port value register */
	    uchar_t sab_pim;	/* Port interrupt mask */
	    uchar_t sab_pcr;	/* Port configuration register */
	    uchar_t sab_ccr4;	/* Channel configuration register 4 (hdlc) */
	} se_regs_t;

/* Define hdlc write register names where different in hdlc mode */

#define	sab_xad1 sab_xon	/* Transmit address 1 */
#define	sab_xad2 sab_xoff	/* Transmit address 2 */
#define	sab_rah1 sab_tcr	/* Receive address high 1 */
#define	sab_rah2 sab_dafo	/* Receive address high 2 */
#define	sab_ral1 sab_rfc	/* Receive address low 1 */
#define	sab_rlcr sab_tic	/* Receive frame length check */
#define	sab_aml  sab_mxn	/* Address mask low */
#define	sab_amh  sab_mxf	/* Address mask high */

/*
 * Define read registers where different than write registers in hdlc mode
 */

#define	sab_rfifo sab_xfifo	/* Receive fifo */
#define	sab_star sab_cmdr	/* status register */
#define	sab_rsta sab_pre	/* Receive status */
#define	sab_rhcr sab_ral2	/* Receive HDLC control */
#define	sab_rbcl sab_xbcl	/* Receive byte count low */
#define	sab_rbch sab_xbch	/* Receive byte count high */
#define	sab_vstr sab_bgr	/* Version status */
#define	sab_gis sab_iva		/* Global interrupt status */
#define	sab_isr sab_imr		/* Interrupt status (bytes 0 and 1) */
#define	sab_pis sab_pim		/* Port interrupt status */

/*
 * Define bits in individual registers
 */

/* Status register */
#define	SAB_STAR_XDOV 0x80	/* Transmit data overflow */
#define	SAB_STAR_XFW  0x40	/* Transmit FIFO write enable */
#define	SAB_STAR_RFNE 0x20	/* RFIFO not empty */
#define	SAB_STAR_FCS  0x10	/* Flow control status */
#define	SAB_STAR_TEC  0x08	/* Transmit immediate in progress */
#define	SAB_STAR_CEC  0x04	/* Command executing */
#define	SAB_STAR_CTS  0x02	/* Clear to Send status */
				/* bit 0x01 mbz */

/* Command register */
#define	SAB_CMDR_RMC  0x80	/* Receive Message Complete - release RFIFO */
#define	SAB_CMDR_RRES 0x40	/* Receiver reset - wipe RFIFO */
#define	SAB_CMDR_RFRD 0x20	/* RFIFO read enable - make data readable */
#define	SAB_CMDR_STI  0x10	/* Start Timer */
#define	SAB_CMDR_XF   0x08	/* Transmit frame */
#define	SAB_CMDR_XRES 0x01	/* Transmit reset - wipe XFIFO */
/* ... hdlc mode */
#define	SAB_CMDR_RHR  0x40	/* Reset HDLC receiver */
#define	SAB_CMDR_XTF  0x08	/* Transmit transparent frame */
#define	SAB_CMDR_XME  0x02	/* Transmit message end */

/* Receive status register (hdlc only) */
#define	SAB_RSTA_VFR  0x80	/* Valid frame */
#define	SAB_RSTA_RDO  0x40	/* Receive data overflow */
#define	SAB_RSTA_CRC  0x20	/* CRC compare check o.k. */
#define	SAB_RSTA_RAB  0x10	/* Receive Message Aborted */
#define	SAB_RSTA_HAM  0x0c	/* High byte address compare mask */
#define	SAB_RSTA_CR   0x02	/* Command/Response */
#define	SAB_RSTA_LA   0x01	/* Low byte address compare (1=ral1) */

/* mode register */

#define	SAB_MODE_FRTS 0x40	/* Flow control RTS (v3.1 and later) */
#define	SAB_MODE_FCTS 0x20	/* Ignore CTS */
#define	SAB_MODE_FLON 0x10	/* Flow control on */
#define	SAB_MODE_RAC  0x08	/* Receiver Active */
#define	SAB_MODE_RTS  0x04	/* Request To Send */
#define	SAB_MODE_TRS  0x02	/* Timer resolution */
#define	SAB_MODE_TLP  0x01	/* Test loop */
/* ... hdlc mode */
#define	SAB_MODE_MDSM 0xc0	/* Mode select mask */
#define	SAB_MODE_TRAN 0x80	/* Transparent mode */
#define	SAB_MODE_ADM  0x20	/* Address mode - 1=16 bit address */
#define	SAB_MODE_TMD  0x10	/* Timer mode 1=internal */

/* Data format register */
#define	SAB_DAFO_XBRK 0x40	/* Send break */
#define	SAB_DAFO_STOP 0x20	/* stop bits - set indicates 2 stop bits */
#define	SAB_DAFO_PARM 0x18	/* Parity mask */
#define	SAB_DAFO_SPAC 0x00	/* Space parity */
#define	SAB_DAFO_ODD  0x08	/* Odd parity */
#define	SAB_DAFO_EVEN 0x10	/* Even parity */
#define	SAB_DAFO_MARK 0x18	/* Mark parity */
#define	SAB_DAFO_PARE 0x04	/* Parity Enable - check receive parity */
#define	SAB_DAFO_CHLM 0x03	/* Character length mask */
#define	SAB_DAFO_8BIT 0x00	/* 8 bit characters */
#define	SAB_DAFO_7BIT 0x01	/* 7 bit characters */
#define	SAB_DAFO_6BIT 0x02	/* 6 bit characters */
#define	SAB_DAFO_5BIT 0x03	/* 5 bit characters (ha!) */

/* Receive FIFO control register */
#define	SAB_RFC_DPS   0x40	/* Disable parity storage */
#define	SAB_RFC_RFDF  0x10	/* RFIFO data format includes status */
#define	SAB_RFC_RFM   0x0c	/* RFIFO threshold mask */
#define	SAB_RFC_RF1   0x00	/* RFIFO threshold level 1 */
#define	SAB_RFC_RF4   0x04	/* RFIFO threshold level 4 */
#define	SAB_RFC_RF16  0x08	/* RFIFO threshold level 16 */
#define	SAB_RFC_RF32  0x0c	/* RFIFO threshold level 32 */
#define	SAB_RFC_TCDE  0x01	/* Termination character detection enable */

/* channel configuration register 0 */
#define	SAB_CCR0_PU   0x80	/* Power up (active). Zero causes power down */
#define	SAB_CCR0_MCE  0x40	/* Master clock enable */
#define	SAB_CCR0_NRZ  0x00	/* NRZ data encoding */
#define	SAB_CCR0_NRZI 0x08	/* NRZI data encoding */
#define	SAB_CCR0_FM0  0x10	/* FM0 data encoding */
#define	SAB_CCR0_FM1  0x14	/* FM1 data encoding */
#define	SAB_CCR0_MAN  0x18	/* Manchester encoding */
#define	SAB_CCR0_HDLC 0x00	/* HDLC/SDLC mode */
#define	SAB_CCR0_SDLC 0x01	/* SDLC loop mode */
#define	SAB_CCR0_BSYN 0x02	/* Bisync mode */
#define	SAB_CCR0_ASY  0x03	/* Async mode */

/* channel configuration register 1 */
#define	SAB_CCR1_ODS  0x10	/* Output driver select - push-pull */
#define	SAB_CCR1_BCR  0x08	/* Bit clock rate - div 16 */
#define	SAB_CCR1_CM0  0x00	/* clock mode 0 - external clocks */
#define	SAB_CCR1_CM1  0x01	/* clock mode 1 - external clocks, cd strobe */
#define	SAB_CCR1_CM2  0x02	/* clock mode 2 - external clocks, dpll */
#define	SAB_CCR1_CM3  0x03	/* clock mode 3 - external clocks from rxc */
#define	SAB_CCR1_CM4  0x04	/* clock mode 4 - clock direct from crystal */
#define	SAB_CCR1_CM5  0x05	/* clock mode 5 - time sliced */
#define	SAB_CCR1_CM6  0x06	/* clock mode 6 - dpll clocks */
#define	SAB_CCR1_CM7  0x07	/* clock mode 7 - internal baud generator */
/* ... hdlc mode */
#define	SAB_CCR1_SFLG 0x80	/* Enable shared flags */
#define	SAB_CCR1_ITF  0x08	/* Iterframe time fill 1=continuous flag */

/* channel configuration register 2 (for clock mode 7) */

#define	SAB_CCR2_BR9  0x80	/* high order bit of baud rate divisor */
#define	SAB_CCR2_BR8  0x40	/* high order bit of baud rate divisor */
#define	SAB_CCR2_BDF  0x20	/* Baud rate divide factor - active */
#define	SAB_CCR2_SSEL 0x10	/* Clock select (BRG vs DPLL) */
#define	SAB_CCR2_TOE  0x08	/* TxCLK output enable */
#define	SAB_CCR2_RWX  0x04	/* read-write exchange. Not used. */
#define	SAB_CCR2_DIV  0x01	/* Data inversion. Not used */

/* channel configuration register 3 */
#define	SAB_CCR3_RADD 0x10	/* Receive address pushed to rfifo */
#define	SAB_CCR3_CRL  0x08	/* CRC reset level (0=ffff, 1=0000) */
#define	SAB_CCR3_RCRC 0x04	/* Receive CRC eanble */
#define	SAB_CCR3_XCRC 0x02	/* Transmit CRC enable */
#define	SAB_CCR3_PSD  0x01	/* Phase shift disable */
/* ... hdlc mode */
#define	SAB_CCR3_PREM 0xc0	/* preamble mask */
#define	SAB_CCR3_PRE2 0x40	/* preamble repeat - twice. */
#define	SAB_CCR3_EPT  0x20	/* enable preamble transmission */

/* global interrupt status */
#define	SAB_GIS_PI    0x80	/* Port interrupt (DSR or DTR change) */
#define	SAB_GIS_ISA1  0x08	/* isr1 on channel A */
#define	SAB_GIS_ISA0  0x04	/* isr0 on channel A */
#define	SAB_GIS_ISB1  0x02	/* isr1 on channel B */
#define	SAB_GIS_ISB0  0x01	/* isr0 on channel B */

/* Interrupt port configuration */
#define	SAB_IPC_VIS   0x80	/* Masked off bits are visible */
#define	SAB_IPC_IOCM  0x03	/* Interrupt port configuration mask */
#define	SAB_IPC_HIGH  0x03	/* Push/Pull output, active high */
#define	SAB_IPC_LOW   0x01	/* Push/Pull output, active low */
#define	SAB_IPC_ODRN  0x00	/* Open drain output */

/* interrupt status and mask registers (same bits, use isr name) */
#define	SAB_ISR_BRK   0x8000	/* Break detected */
#define	SAB_ISR_BRKT  0x4000	/* End of break detected */
#define	SAB_ISR_ALLS  0x2000	/* All sent - XFIFO empty */
#define	SAB_ISR_XOFF  0x1000	/* XOFF character received */
#define	SAB_ISR_TIN   0x0800	/* Timer interrupt */
#define	SAB_ISR_CSC   0x0400	/* CTS status changed */
#define	SAB_ISR_XON   0x0200	/* XON character received */
#define	SAB_ISR_XPR   0x0100	/* Transmit pool ready */
#define	SAB_ISR_TCD   0x0080	/* Termination character detected */
#define	SAB_ISR_TIME  0x0040	/* Timeout */
#define	SAB_ISR_PERR  0x0020	/* Parity Error */
#define	SAB_ISR_FERR  0x0010	/* Framing error */
#define	SAB_ISR_PLLA  0x0008	/* DPLL Asynchronous */
#define	SAB_ISR_CDSC  0x0004	/* CD status changed */
#define	SAB_ISR_RFO   0x0002	/* RFIFO overflow */
#define	SAB_ISR_RPF   0x0001	/* Receive Pool Full */
/* ... hdlc mode */
#define	SAB_ISR_EOP   0x8000	/* End of Poll sequence detected */
#define	SAB_ISR_RDO   0x4000	/* Receive data overflow */
#define	SAB_ISR_XDU   0x1000	/* Transmit data underrun */
#define	SAB_ISR_XMR   0x0200	/* Transmit Message Repeat */
#define	SAB_ISR_RME   0x0080	/* Receive Message End */
#define	SAB_ISR_RFS   0x0040	/* Receive Frame start */
#define	SAB_ISR_RSC   0x0020	/* Receive Status change */
#define	SAB_ISR_PCE   0x0010	/* Protocol Error */

/* Port value register */
#define	SAB_PVR_DSRA  0x01	/* DSR for port A (input) */
#define	SAB_PVR_DTRA  0x02	/* DTR for port A (output) */
#define	SAB_PVR_DTRB  0x04	/* DTR fpr port B (output) */
#define	SAB_PVR_DSRB  0x08	/* DSR for port B (input) */
#define	SAB_PVR_FAST  0x10	/* Select 10V/uS vs 4.2V/uS transitions */
#define	SAB_PVR_MODE  0x20	/* Select RS232 vs. RS423 */
#define	SAB_PVR_RTSA  0x40	/* RTS for port A (output, v2.2 chip only) */
#define	SAB_PVR_RTSB  0x80	/* RTS for port B (output, v2.2 chip only) */
/*
 * This bit is also used to control the reset of the SSP device.
 */
#define	SAB_PVR_SSP   0x80

/* ccr4 */
#define	SAB_CCR4_MCK4 0x80	/* Master clock divide by 4 */
#define	SAB_CCR4_EBRG 0x40	/* Extended baud rate generator */

/* Version register */
#define	SAB_VSTR_CD    0x80	/* Carrier detect */
#define	SAB_VSTR_DPLA  0x40	/* DPLL asynchronous */
#define	SAB_VSTR_VMASK 0x0F	/* Version mask */

/* rlcr */
#define	SAB_RLCR_RCE   0x80	/* Receive Length Check feature enable */

/* Async-specific datastructure */

typedef
    struct se_asyncl {
	struct {
		unsigned zas_wopen    : 1; /* waiting for open to complete */
		unsigned zas_isopen   : 1; /* open is complete */
		unsigned zas_out    :	1; /* line being used for dialout */
		unsigned zas_carr_on  : 1; /* carrier on last time we looked */
		unsigned zas_stopped  : 1; /* output is stopped */
		unsigned zas_break    : 1; /* waiting for break to finish */
		unsigned zas_break_rcv: 1; /* Received a break */
		unsigned zas_draining : 1; /* waiting for output to finish */
		unsigned zas_do_esc   : 1; /* Escape 0xff receive with 0xff. */
		unsigned zas_parerr   : 1; /* Parity error received */
		unsigned zas_flowctl  : 1; /* Input is stopped  */
		unsigned zas_pps   :	1; /* PPS on or off */
		unsigned zas_delay   :	1; /* waiting for delay to finish */
		unsigned zas_cantflow : 1; /* flow control changes disabled */
	} zas;				/* was za_flags */
	int		za_sw_overrun;	/* No buffers to take data from chip */
	int		za_hw_overrun;	/* Chip told us that we lost data */
	tty_common_t    za_ttycommon;	/* tty driver common data */
	unsigned long	za_iflag;	/* saved iflag from ttycommon */
	unsigned long	za_cflag;	/* saved cflag from ttycommon */
	uchar_t		za_stopc;
	uchar_t		za_startc;
	/*
	 * The following fields are protected by the se_hi_excl lock.
	 */
	timeout_id_t	za_break_timer;	/* Timerid for sending break */
	timeout_id_t	za_delay_timer;	/* Timerid for delay */
	uchar_t		za_rcv_mask;	/* Mask parity from incoming chars */
	uchar_t		za_rcv_flag;	/* Receive flags of various types */
	uchar_t		za_ext;		/* modem status change flag */
	uchar_t		za_flon_mask;	/* Work around bug in v2.2 chip */
	queue_t		*za_writeq;	/* Write-side STREAMS queue */
} se_asyncl_t;

/* HDLC-specific datastructure */

typedef
    struct se_hdlcl {
	queue_t		*sl_readq;	/* Pointer to read queue */
	queue_t		*sl_writeq;	/* Pointer to write queue */
	struct scc_mode sl_mode;	/* clock, etc. modes */
	struct sl_stats sl_st;		/* Data and error statistics */
	mblk_t		*sl_mstat;	/* most recent modem status change */
	timeout_id_t	sl_wd_id;	/* watchdog timeout ID */
	int		sl_wd_txcnt_start; /* watchdog tx counter start value */
	int		sl_wd_txcnt;	/* watchdog counts packets sent */
	int		sl_wd_count;	/* watchdog counter */
	uchar_t		sl_clockmode;	/* clock to enable for xmit/rcv */
	struct {
	    unsigned sf_initialized : 1; /* se_hdlc_program has been called */
	    unsigned sf_fdxptp    :   1; /* Full duplex AND Point-To-Point */
	    unsigned sf_setrts    :   1; /* HDX: set RTS */
	    unsigned sf_wcts	  :   1; /* HDX: wait for CTS */
	    unsigned sf_ctsok	  :   1; /* HDX: received CTS */
	} zhf;
} se_hdlcl_t;

/* Software per-port header datastructure. */

typedef
    struct se_ctl_hdr {
	unsigned int	sc_protocol;	/* async, outline, hdlc, clone */
	struct se_ops	*sc_ops;	/* per-protocol operations structure */
	kmutex_t	sc_excl;	/* per-line mutex */
	int		sc_unit;	/* which channel (0:NSE_LINE) */
} se_ctl_hdr_t;			/* Header is shared with clone device. */

#define	NULL_PROTOCOL 0
#define	ASYN_PROTOCOL 1
#define	OUTD_PROTOCOL 2
#define	HDLC_PROTOCOL 3
#define	CLON_PROTOCOL 4

/* Software per-port datastructure */

typedef
    struct se_ctl {
	se_ctl_hdr_t	h;		/* Header describes protocol */
	struct se_ctl	*sc_next;	/* Pointer to next se_ctl structure */
	dev_t		sc_dev;		/* Device info */
	struct se_chip	*sc_chip;	/* pointer to chip-wide data */
	se_regs_t	*sc_reg;	/* address of port registers */
	ddi_acc_handle_t sc_handle;	/* Handle from common_regs_setup */
	unsigned char	*sc_wr_cur;	/* current read and write pointers */
	unsigned char	*sc_wr_lim;
	unsigned char	*sc_rd_cur;
	unsigned char	*sc_rd_lim;
	kcondvar_t	sc_flags_cv;	/* condition variable for flags */
	time_t		sc_dtrlow;	/* time dtr went low */
	int		sc_rstandby_ptr; /* stack pointer into rstandby */
	mblk_t *sc_rstandby[SE_MAX_RSTANDBY]; /* 6 standby buffers */
	int		sc_rdone_count;	/* Count of buffers on sc_rdone list */
	mblk_t		*sc_rdone_head;	/* Head of list of completed buffers */
	mblk_t		*sc_rdone_tail;	/* Tail of above list */
	mblk_t		*sc_rcvhead;	/* receive: head of active message */
	mblk_t		*sc_rcvblk;	/* receive: active msg block */
	mblk_t		*sc_xmitblk;	/* transmit: active msg block */
	mblk_t		*sc_xmithead;	/* transmit: head of active message */
	bufcall_id_t	sc_bufcid;
	timeout_id_t	sc_kick_rcv_id;	/* Callback id for kick_rcv */

	short		sc_bufsize;	/* Buffer size to use (async,hdlc) */
	ushort_t	sc_mru;		/* the MRU size used */

	uchar_t		sc_chipv23;	/* Chip version number (2.2 vs 2.3) */
	uchar_t		sc_char_pending; /* Pending flow control character */
	uchar_t		sc_progress;	/* transmit progress being made */
	uchar_t		sc_flag_softint; /* Need software interrupt flag */
	uchar_t		sc_suspend;	/* Device is suspended for CPR */
	uchar_t		sc_xmit_active;	/* Transmit is known to be active */
	uchar_t		sc_soft_active;	/* Software interrupt active */
	uchar_t		sc_softint_pending; /* Software intr pending flag */
	ushort_t	sc_imr;		/* Interrupt mask as set */
	uchar_t		sc_softcar;	/* Softcarrier */
	uchar_t		sc_closing;	/* Device is closing */

	uchar_t		sc_xmit_done;	/* Flag indicating xmit need softint */
	uchar_t		sc_dtrbit;	/* Locations of shared bits  */
	uchar_t		sc_dsrbit;
	uchar_t		sc_rtsbit;
	struct {
	    se_asyncl_t	za;		/* async-specific protocol area */
	    se_hdlcl_t	zh;		/* hdlc-specific protocol area */
	} z;
	kcondvar_t	sc_suspend_cv;	/* condition variable for suspend */
	timeout_id_t	sc_close_timer;	/* Timerid for close progress */
	uchar_t		sc_disable_rfifo; /* flag if rfifo is to be disabled */
	uint_t		sc_char_in_rfifo; /* # of chars in receive fifo */
} se_ctl_t;

/* Per-clone datastructure */
typedef
    struct se_clone {
	se_ctl_hdr_t	h;		/* Header structure */
	struct se_clone *scl_next;	/* Link to next element in list */
	se_ctl_t	*scl_sc;	/* Pointer to actual device */
	queue_t		*scl_rq;	/* Read queue pointer */
	int		scl_in_use;	/* Available for use */
} se_clone_t;


/* Software per-chip datastructure. */

typedef
    struct se_chip {
	struct se_chip	*sec_next;	/* next se_chip data structure */
	se_ctl_t	*sec_porta;	/* Pointer to port A data structure */
	se_ctl_t	*sec_portb;	/* Pointer to port B data structure */
	dev_info_t	*sec_dip;	/* dev_info pointer from attach */
	short		sec_chipno;	/* instance number from attach */
	uchar_t		sec_ipc;	/* Interrupt port configuration */
	uchar_t		sec_pim;	/* Port interrupt mask */
	uchar_t		sec_pvr;	/* (p)port value register */
	uchar_t		sec_pcr;	/* (p)port configuration register */
	boolean_t	sec_is_ssp;	/* chip is controlling a SSP device */
	se_ctl_t	*sec_ssp_console;	/* SSP console port */
	se_ctl_t	*sec_ssp_control;	/* SSP control port */
	ddi_softintr_t	se_softintr_id; /* for the soft interrupt. */
} se_chip_t;

/* Protocol specific operations */

typedef struct se_ops {
	void (*portint)(se_ctl_t *, ushort_t); /* per-port interrupt hdlr */
	void (*softint)();	/* Where to service soft interrupt request */
	void (*dsrint)();	/* DSR has changed for this line */
	int (*open)();		/* Streams has called us for an open */
	int (*close)();		/* Streams has called us to close device */
	int (*wput)();		/* Streams has a package for us to handle */
	void (*resume)();	/* We need to suspend device for CPR */
	void (*suspend)();	/* We need to recover from CPR */
} se_ops_t;

#define	SE_ON	TIOCM_DTR|TIOCM_RTS	/* Turn line on */
#define	SE_OFF	0			/* Turn line off */

/*
 * Driver ioctl command for reseting the SSP device.  This command is only
 * valid for the minor node dedicated to the SSP control line.
 */
#define	SE_IOC_SSP_RESET	(int)(_IO('u', 80))

extern void dlerrorack();
extern void dlokack();

#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_SEDEV_H */

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MI2CV_H
#define	_MI2CV_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * states for the I2C state machine.
 */
enum tran_state {
	TRAN_STATE_NULL,
	TRAN_STATE_WR,
	TRAN_STATE_RD,
	TRAN_STATE_WR_RD,
	TRAN_STATE_START,
	TRAN_STATE_DUMMY_DATA,
	TRAN_STATE_DUMMY_RD,
	TRAN_STATE_REPEAT_START
};

/*
 * MI2CV chip hardware registers
 */
typedef struct mi2cv_regs {
	uint64_t slave_addr_reg;
	uint64_t extra_slave_addr_reg;
	uint64_t data_byte_reg;
	uint64_t control_reg;
	uint64_t status_reg;
	uint64_t clock_control_reg;
	uint64_t sw_reset_reg;
} mi2cv_regs_t;

typedef struct mi2cv {
	dev_info_t		*mi2cv_dip;	   /* dip for this device */
	int			mi2cv_attachflags; /* progress through attach */
	kcondvar_t		mi2cv_cv;	   /* serializes bus access */
	kmutex_t		mi2cv_imutex;	   /* protects mi2cv_t data */
	kcondvar_t		mi2cv_icv;	   /* serializes bus xfers */
	ddi_iblock_cookie_t	mi2cv_icookie;	   /* used by intr framework */
	int			mi2cv_mode;	   /* polled or interrupt */
	uint_t			mi2cv_open;	   /* exclusive access */
	int			mi2cv_busy;	   /* serializes bus access */
	int			mi2cv_cur_status;  /* xfer status - see below */
	dev_info_t		*mi2cv_nexus_dip;  /* for i2c framework */
	i2c_transfer_t		*mi2cv_cur_tran;   /* current xfer struct */
	dev_info_t		*mi2cv_cur_dip;    /* client device dip */
	mi2cv_regs_t		*mi2cv_regs;	   /* chip registers */
	ddi_acc_handle_t	mi2cv_rhandle;	   /* used for regs_map_setup */
	enum tran_state		mi2cv_tran_state;  /* current xfer state */
	char			mi2cv_name[12];	   /* name of this device */
} mi2cv_t;

/*
 * i2c_parent_pvt contains info that is chip specific
 * and is stored on the child's devinfo parent private data.
 */
typedef struct mi2cv_ppvt {
	int mi2cv_ppvt_bus;  /* this tells it what bus to use  */
	int mi2cv_ppvt_addr; /* address of I2C device */
} mi2cv_ppvt_t;

#define	MI2CV_PIL			4
#define	MI2CV_INITIAL_SOFT_SPACE	4
#define	MI2CV_POLL_MODE			1
#define	MI2CV_INTR_MODE			2

/*
 * generic interrupt return values
 */
#define	I2C_COMPLETE	2
#define	I2C_PENDING	3

/*
 * These value put together in the chip equate out to a sampling frequency of
 * 20Mhz and a bus clock speed of 1MHZ
 */

#define	CLK_N	0x04	/* CLK_N can be a value between 0 and 7 */
#define	CLK_M	0x01	/* CLK_M can be a value between 0 and 15 */

#define	CLK_SHIFT	3	/* shift CLK_M value by this many bits */

/* Control Register definitions */
#define	CNTR_IEN	0x80
#define	CNTR_ENAB	0x40
#define	CNTR_START	0x20
#define	CNTR_STOP	0x10
#define	CNTR_IFLG	0x08
#define	CNTR_ACK	0x04

/*
 * Attach flags
 */
#define	ADD_INTR	0x01
#define	SETUP_REGS	0x02
#define	NEXUS_REGISTER	0x04
#define	PROP_CREATE	0x08
#define	IMUTEX		0x10
#define	MINOR_NODE	0x20

/*
 * Transfer status values
 */
#define	MI2CV_TRANSFER_NEW	1
#define	MI2CV_TRANSFER_ON	2
#define	MI2CV_TRANSFER_OVER	3

/*
 * This has to be OR'ed in with the address for
 * I2C read transactions.
 */
#define	I2C_READ	0x01

/*
 * MI2CV status register values
 */
#define	BUS_ERROR		0x00
#define	START_TRANSMITED	0x08
#define	REP_START_TRANS		0x10
#define	ADDR_WR_W_ACK		0x18
#define	ADDR_WR_NO_ACK		0x20
#define	DATA_SENT_W_ACK		0x28
#define	DATA_SENT_NO_ACK	0x30
#define	ARB_LOST		0x38
#define	ADDR_RD_W_ACK		0x40
#define	ADDR_RD_NO_ACK		0x48
#define	DATA_RECV_W_ACK		0x50
#define	DATA_RECV_NO_ACK	0x58
#define	NO_RELEVANT_INFO	0xf8

/*
 * Write any value to the reset register for a software reset
 */
#define	MI2CV_RESET	0x01


#ifdef	__cplusplus
}
#endif

#endif /* _MI2CV_H */

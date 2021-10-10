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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_SMC_I2C_H
#define	_SYS_SMC_I2C_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/i2c/misc/i2c_svc.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SMC I2C NEXUS
 */
#define	SMC_POLL_MODE	0x01

#define		SMC_MAX_DATA		33
#define		NEXUS_REGISTER		0x08

#define		SMC_I2C_WR_RD		0x90
#define		SMC_I2C_BP_BUSID	0x06 /* bit 3:1 bus ID is 3 */
#define		SMC_I2C_LOC_BUSID	0x04 /* bit 3:1 bus ID is 2 */
#define		SMC_I2C_PRIV_BUSTYPE	0x01 /* bit 0 bus Type is 1 */

typedef struct {
	struct {
		uchar_t		ctsmc_i2c_busid;
		uchar_t		ctsmc_i2c_i2caddr;
		uchar_t		ctsmc_i2c_rdcount;
	} head;
	uchar_t		ctsmc_i2c_data[SMC_MAX_DATA];
} ctsmc_i2c_wr_t;

typedef struct {
	int		ctsmc_i2c_attachflags;
	kmutex_t  	ctsmc_i2c_mutex;
	kcondvar_t	ctsmc_i2c_cv;
	int		ctsmc_i2c_busy;
	i2c_transfer_t	*ctsmc_i2c_cur_tran;
	dev_info_t	*ctsmc_i2c_cur_dip;
} ctsmc_i2c_state_t;

/*
 * i2c_parent_pvt contains info about the device slave address
 * and the i2c bus its residing
 */
typedef struct ctsmc_i2c_ppvt {
	int ctsmc_i2c_bus; /* i2c bus */
	int ctsmc_i2c_addr; /* slave address of I2C device */
} ctsmc_i2c_ppvt_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SMC_I2C_H */

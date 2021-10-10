/* BEGIN CSTYLED */

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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef __DRM_I2C_H__
#define __DRM_I2C_H__

#include <sys/ksynch.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include "drm.h"

struct i2c_adapter;
struct i2c_msg;

struct i2c_algorithm {
	int (*master_xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
	u32 (*functionality) (struct i2c_adapter *);
};

struct i2c_adapter {
	struct i2c_algorithm *algo;
	kmutex_t bus_lock;
	clock_t timeout;
	int retries;
	char name[64];
	void *data;
	void (*setsda) (void *data, int state);
	void (*setscl) (void *data, int state);
	int  (*getsda) (void *data);
	int  (*getscl) (void *data);
	void *algo_data;       
	clock_t udelay;
};

#define I2C_M_RD		0x01
#define I2C_M_NOSTART		0x02

struct i2c_msg {
	u16 addr;
	u16 flags;
	u16 len;
	u8 *buf;
};

extern int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
extern int i2c_bit_add_bus(struct i2c_adapter *adap);

#endif /* __DRM_I2C_H__ */

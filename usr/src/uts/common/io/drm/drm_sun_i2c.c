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

#include "drmP.h"

#define mutex_lock_nested(a,b) mutex_enter(a)

#define I2C_HI_CLOCK(adap, ret)                          \
do {                                                     \
	ret = i2c_setscl(adap);                          \
	if (ret) {                                       \
		/* Other master keeps the clock low. */  \
		/* Free the bus. */                      \
		i2c_setsda(adap);                        \
		i2c_udelay(adap);                        \
		return ret;                              \
	}                                                \
} while (*"\0");

static inline void
i2c_udelay(struct i2c_adapter *adap)
{
	udelay((adap->udelay + 1) >> 1);
}

static inline int
i2c_getsda(struct i2c_adapter *adap)
{
	return adap->getsda(adap->data) ? 1 : 0;
}

static inline void
i2c_clrsda(struct i2c_adapter *adap)
{
	adap->setsda(adap->data, 0);
}

static inline void
i2c_setsda(struct i2c_adapter *adap)
{
	adap->setsda(adap->data, 1);
}

static inline int
i2c_getscl(struct i2c_adapter *adap)
{
	return adap->getscl(adap->data) ? 1 : 0;
}

static inline void
i2c_clrscl(struct i2c_adapter *adap)
{
	adap->setscl(adap->data, 0);
}

static int
i2c_setscl(struct i2c_adapter *adap)
{
	clock_t start;

	adap->setscl(adap->data, 1);

	/* Clock Synchronization */
	start = ddi_get_lbolt();
	while (!i2c_getscl(adap)) {
		/* FIXME: Does ddi_get_lbolt() return negative
		 *        value? If so, leave me.
		 */
		if ((ddi_get_lbolt() - start) > adap->timeout)
			return -ETIMEDOUT;
	}

	return 0;
}

static int
i2c_start(struct i2c_adapter *adap)
{
	int ret = 0;

	/* Step 1: free the bus. */
	i2c_setsda(adap);
	i2c_udelay(adap);
	ret = i2c_setscl(adap);
	if (ret) {
		/* Other master keeps the clock low.
		 * The bus is busy.
		 */
		return ret;
	}
	if (!i2c_getsda(adap)) {
		/* The bus is busy. */
		return -EBUSY;
	}
	i2c_udelay(adap);

	/* Step 2: (S/Sr) condition. */
	i2c_clrsda(adap);
	i2c_udelay(adap);

	/* Step 3: free the clock. */
	i2c_clrscl(adap);
	i2c_udelay(adap);

	return 0;
}

static int
i2c_stop(struct i2c_adapter *adap)
{
	int ret;

	if (i2c_getscl(adap)) {
		/* Stop() must be called after start() or any
		 * transfer routines, which all free the clock
		 * before returning.
		 */
		return -ENOTSUP;
	}

	/* Step 1: Free the data */
	i2c_clrsda(adap);
	i2c_udelay(adap);

	/* Step 2: Hold the clock */
	I2C_HI_CLOCK(adap, ret);
	i2c_udelay(adap);

	/* Step 3: (P) condition */
	i2c_setsda(adap);
	i2c_udelay(adap);
	if (!i2c_getsda(adap)) {
		/* Other master keeps the data low.
		 * The bus is busy.
		 */
		return -EBUSY;
	}

	return 0;
}

static int
i2c_write_byte(struct i2c_adapter *adap, unsigned char c)
{
	int needARB = 0;
	int ret = 0, i;

	if (i2c_getscl(adap)) {
		/* Write() must be called after start() or any
		 * transfer routines, which all free the clock
		 * before returning.
		 */
		return -ENOTSUP;
	}

	for (i = 7; i >= 0; i--) {
		/* Step 1: set data. */
		if (c & (1 << i)) {
			needARB = 1;
			i2c_setsda(adap);
		} else {
			needARB = 0;
			i2c_clrsda(adap);
		}
		i2c_udelay(adap);

		/* Step 2: hold the clock. */
		I2C_HI_CLOCK(adap, ret);
		if (needARB && !i2c_getsda(adap)) {
			/* Do arbitration: lose the bus. */
			return -EBUSY;
		}
		/* Double delay. */
		i2c_udelay(adap);
		i2c_udelay(adap);
		if (needARB && !i2c_getsda(adap)) {
			/* Do arbitration: someone performs (S) condition. */
			return -EBUSY;
		}

		/* Step 3: free the clock. */
		i2c_clrscl(adap);
		i2c_udelay(adap);
	}

	return 0;
}

static int
i2c_read_byte(struct i2c_adapter *adap, unsigned char *cp)
{
	int ret, r, i;

	if (i2c_getscl(adap)) {
		/* Read() must be called after start() or any
		 * transfer routines, which all free the clock
		 * before returning.
		 */
		return -ENOTSUP;
	}

	i2c_setsda(adap);
	i2c_udelay(adap);

	*cp = 0;
	for (i = 7; i >= 0; i--) {
		/* Step 1: hold the clock. */
		I2C_HI_CLOCK(adap, ret);
		r = i2c_getsda(adap);
		i2c_udelay(adap);

		/* Step 2: read the data. */
		if (r != i2c_getsda(adap)) {
			/* Do arbitration: someone performs (S/Sr/P) condition. */
			return -EBUSY;
		}
		if (r)
			*cp |= (1 << i);
		i2c_udelay(adap);

		/* Step 3: free the clock */
		i2c_clrscl(adap);
		i2c_udelay(adap);
	}

	return 0;
}

static int
i2c_ack(struct i2c_adapter *adap)
{
	int ret;

	if (i2c_getscl(adap)) {
		/* Ack() must be called after start() or any
		 * transfer routines, which all free the clock
		 * before returning.
		 */
		return -ENOTSUP;
	}

	/* Step 1: free the data. */
	i2c_clrsda(adap);
	i2c_udelay(adap);

	/* Step 2: hold the clock. */
	I2C_HI_CLOCK(adap, ret);
	i2c_udelay(adap);

	/* Step 3: free the clock */
	i2c_clrscl(adap);
	i2c_udelay(adap);

	return 0;
}

static int
i2c_no_ack(struct i2c_adapter *adap)
{
	int ret;

	if (i2c_getscl(adap)) {
		/* Nak() must be called after start() or any
		 * transfer routines, which all free the clock
		 * before returning.
		 */
		return -ENOTSUP;
	}

	/* Step 1: hold the data. */
	i2c_setsda(adap);
	i2c_udelay(adap);

	/* Step 2: hold the clock. */
	I2C_HI_CLOCK(adap, ret);
	if (!i2c_getsda(adap)) {
		/* Other master keeps the data low. */
		return -EBUSY;
	}
	i2c_udelay(adap);
	if (!i2c_getsda(adap)) {
		/* Do arbitration: someone performs (S/Sr) condition. */
		return -EBUSY;
	}

	/* Step 3: free the clock */
	i2c_clrscl(adap);
	i2c_udelay(adap);

	return 0;
}

static int
i2c_wait_ack(struct i2c_adapter *adap)
{
	int ret;

	if (i2c_getscl(adap)) {
		/* Wack() must be called after start() or any
		 * transfer routines, which all free the clock
		 * before returning.
		 */
		return -ENOTSUP;
	}

	/* Step 1: hold the data. */
	i2c_setsda(adap);
	i2c_udelay(adap);

	/* Step 2: hold the clock. */
	I2C_HI_CLOCK(adap, ret);
	i2c_udelay(adap);

	/* Step 3: read the data. */
	ret = i2c_getsda(adap) ? 0 : 1;

	/* Step 4: free the clock */
	i2c_clrscl(adap);
	i2c_udelay(adap);

	return ret;
}

static int
i2c_write_msg(struct i2c_adapter *adap, struct i2c_msg *msg)
{
	int i, ret;

	for (i = 0; i < msg->len; i++) {
		ret = i2c_write_byte(adap, msg->buf[i]);
		if (ret)
			return ret;

		ret = i2c_wait_ack(adap);
		if (ret == 1)
			continue;
		else if (ret == 0)
			return -ENXIO;
		else
			return ret;
	}

	return 0;
}

static int
i2c_read_msg(struct i2c_adapter *adap, struct i2c_msg *msg)
{
	unsigned char c;
	int i, ret;

	for (i = 0; i < msg->len; i++) {
		ret = i2c_read_byte(adap, &c);
		if (ret)
			return ret;

		msg->buf[i] = c;

		if (i < msg->len - 1)
			ret = i2c_ack(adap);
		else
			ret = i2c_no_ack(adap);
		if (ret)
			return ret;
	}

	return 0;
}

static int
i2c_address(struct i2c_adapter *adap, struct i2c_msg *msg)
{
	unsigned char addr;
	int ret;

	addr = msg->addr << 1;
	if (msg->flags & I2C_M_RD)
		addr |= 1;

	ret = i2c_write_byte(adap, addr);
	if (ret)
		return ret;

	ret = i2c_wait_ack(adap);
	if (ret == 1)
		return 0;
	else if (ret == 0)
		return -ENXIO;

	return ret;
}

static int
i2c_do_transfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct i2c_msg *msg;
	int i, ret = 0;

	for (i = 0; i < num; i++) {
		msg = &msgs[i];

		if (!(i && (msg->flags & I2C_M_NOSTART))) {
			ret = i2c_start(adap);
			if (ret)
				return ret;

			ret = i2c_address(adap, msg);
			if (ret)
				return ret;
		}

		if (msg->flags & I2C_M_RD)
			ret = i2c_read_msg(adap, msg);
		else
			ret = i2c_write_msg(adap, msg);
		if (ret)
			return ret;
	}

	ret = i2c_stop(adap);
	if (ret)
		return ret;

	return num;
}

static const struct i2c_algorithm i2c_bit_algo = {
	.master_xfer = i2c_do_transfer,
	.functionality = NULL,
};

int
i2c_bit_add_bus(struct i2c_adapter *adap)
{
	if (!adap->setscl || !adap->getscl || !adap->setsda || !adap->getsda)
		return -EINVAL;

	adap->algo = (struct i2c_algorithm *)&i2c_bit_algo;
	adap->retries = 3;

	return 0;
}

int
i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	clock_t start;
	int i, ret;

	mutex_enter(&adap->bus_lock);
	start = ddi_get_lbolt();
	for (i = 0; i <= adap->retries; i++) {
		ret = adap->algo->master_xfer(adap, msgs, num);
		switch (ret) {
		case 0:
		case ETIMEDOUT:
			goto do_exit;
		default:
			break;
		}

		/* FIXME: Does ddi_get_lbolt() return negative
		 *        value? If so, leave me.
		 */
		if ((ddi_get_lbolt() - start) > adap->timeout)
			break;
	}

do_exit:
	mutex_exit(&adap->bus_lock);
	return ret;
}

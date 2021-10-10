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
 * This file is part of the Chelsio T3 Ethernet driver.
 *
 * Copyright (C) 2007-2010 Chelsio Communications.  All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 * release for licensing terms and conditions.
 */

#include "cxge_common.h"

enum {
	ELMR_ADDR	= 0,
	ELMR_STAT	= 1,
	ELMR_DATA_LO	= 2,
	ELMR_DATA_HI	= 3,

	ELMR_THRES0	= 0xe000,
	ELMR_BW		= 0xe00c,
	ELMR_FIFO_SZ	= 0xe00d,
	ELMR_STATS	= 0xf000,

	ELMR_MDIO_ADDR	= 10
};

#define	VSC_REG(block, subblock, reg) \
	((reg) | ((subblock) << 8) | ((block) << 12))

int
t3_elmr_blk_write(adapter_t *adap, int start, const u32 *vals, int n)
{
	int ret;
	const struct mdio_ops *mo = adapter_info(adap)->mdio_ops;

	ELMR_LOCK(adap);
	ret = mo->write(adap, ELMR_MDIO_ADDR, 0, ELMR_ADDR, start);
	for (; !ret && n; n--, vals++) {
		ret = mo->write(adap, ELMR_MDIO_ADDR, 0, ELMR_DATA_LO,
		    *vals & 0xffff);
		if (!ret)
			ret = mo->write(adap, ELMR_MDIO_ADDR, 0, ELMR_DATA_HI,
			    *vals >> 16);
	}
	ELMR_UNLOCK(adap);
	return (ret);
}

static int elmr_write(adapter_t *adap, int addr, u32 val)
{
	return (t3_elmr_blk_write(adap, addr, &val, 1));
}

int
t3_elmr_blk_read(adapter_t *adap, int start, u32 *vals, int n)
{
	int i, ret;
	unsigned int v;
	const struct mdio_ops *mo = adapter_info(adap)->mdio_ops;

	ELMR_LOCK(adap);

	ret = mo->write(adap, ELMR_MDIO_ADDR, 0, ELMR_ADDR, start);
	if (ret)
		goto out;

	for (i = 0; i < 5; i++) {
		ret = mo->read(adap, ELMR_MDIO_ADDR, 0, ELMR_STAT, &v);
		if (ret)
			goto out;
		if (v == 1)
			break;
		udelay(5);
	}
	if (v != 1) {
		ret = -ETIME;
		goto out;
	}

	for (; !ret && n; n--, vals++) {
		ret = mo->read(adap, ELMR_MDIO_ADDR, 0, ELMR_DATA_LO, vals);
		if (!ret) {
			ret = mo->read(adap, ELMR_MDIO_ADDR, 0, ELMR_DATA_HI,
			    &v);
			*vals |= v << 16;
		}
	}
out:	ELMR_UNLOCK(adap);
	return (ret);
}

int
t3_vsc7323_init(adapter_t *adap, int nports)
{
	static struct addr_val_pair sys_avp[] = {
		{ VSC_REG(7, 15, 0xf),  2 },
		{ VSC_REG(7, 15, 0x19), 0xd6 },
		{ VSC_REG(7, 15, 7),    0xc },
		{ VSC_REG(7, 1, 0),	0x220 },
	};
	static struct addr_val_pair fifo_avp[] = {
		{ VSC_REG(2, 0, 0x2f), 0 },
		{ VSC_REG(2, 0, 0xf),  0xa0010291 },
		{ VSC_REG(2, 1, 0x2f), 1 },
		{ VSC_REG(2, 1, 0xf),  0xa026301 }
	};
	static struct addr_val_pair xg_avp[] = {
		{ VSC_REG(1, 10, 0),    0x600b },
		/* QUANTA = 96 * 1024 * 8 / 512 */
		{ VSC_REG(1, 10, 1),    0x70600 },
		{ VSC_REG(1, 10, 2),    0x2710 },
		{ VSC_REG(1, 10, 5),    0x65 },
		{ VSC_REG(1, 10, 7),    0x23 },
		{ VSC_REG(1, 10, 0x23), 0x800007bf },
		{ VSC_REG(1, 10, 0x23), 0x000007bf },
		{ VSC_REG(1, 10, 0x23), 0x800007bf },
		{ VSC_REG(1, 10, 0x24), 4 }
	};

	int i, ret, ing_step, egr_step, ing_bot, egr_bot;

	for (i = 0; i < ARRAY_SIZE(sys_avp); i++)
		if ((ret = t3_elmr_blk_write(adap, sys_avp[i].reg_addr,
		    &sys_avp[i].val, 1)))
			return (ret);

	ing_step = 0xc0 / nports;
	egr_step = 0x40 / nports;
	ing_bot = egr_bot = 0;
//	ing_wm = ing_step * 64;
//	egr_wm = egr_step * 64;

	/* {ING,EGR}_CONTROL.CLR = 1 here */
	for (i = 0; i < nports; i++) {
		if (
		    /* LINTED - E_EQUALITY_NOT_ASSIGNMENT */
		    (ret = elmr_write(adap, VSC_REG(2, 0, 0x10 + i),
		    ((ing_bot + ing_step) << 16) | ing_bot)) ||
		    (ret = elmr_write(adap, VSC_REG(2, 0, 0x40 + i),
		    0x6000bc0)) ||
		    (ret = elmr_write(adap, VSC_REG(2, 0, 0x50 + i), 1)) ||
		    (ret = elmr_write(adap, VSC_REG(2, 1, 0x10 + i),
		    ((egr_bot + egr_step) << 16) | egr_bot)) ||
		    (ret = elmr_write(adap, VSC_REG(2, 1, 0x40 + i),
		    0x2000280)) ||
		    (ret = elmr_write(adap, VSC_REG(2, 1, 0x50 + i), 0)))
			return (ret);
		ing_bot += ing_step;
		egr_bot += egr_step;
	}

	for (i = 0; i < ARRAY_SIZE(fifo_avp); i++)
		if ((ret = t3_elmr_blk_write(adap, fifo_avp[i].reg_addr,
		    &fifo_avp[i].val, 1)))
			return (ret);

	for (i = 0; i < ARRAY_SIZE(xg_avp); i++)
		if ((ret = t3_elmr_blk_write(adap, xg_avp[i].reg_addr,
		    &xg_avp[i].val, 1)))
			return (ret);

	for (i = 0; i < nports; i++)
		/* LINTED - E_EQUALITY_NOT_ASSIGNMENT */
		if ((ret = elmr_write(adap, VSC_REG(1, i, 0), 0xa59c)) ||
		    (ret = elmr_write(adap, VSC_REG(1, i, 5),
		    (i << 12) | 0x63)) ||
		    (ret = elmr_write(adap, VSC_REG(1, i, 0xb), 0x96)) ||
		    (ret = elmr_write(adap, VSC_REG(1, i, 0x15), 0x21)) ||
		    (ret = elmr_write(adap, ELMR_THRES0 + i, 768)))
			return (ret);

	if ((ret = elmr_write(adap, ELMR_BW, 7)))
		return (ret);

	return (ret);
}

int
t3_vsc7323_set_speed_fc(adapter_t *adap, int speed, int fc, int port)
{
	int mode, clk, r;

	if (speed >= 0) {
		if (speed == SPEED_10)
			mode = clk = 1;
		else if (speed == SPEED_100)
			mode = 1, clk = 2;
		else if (speed == SPEED_1000)
			mode = clk = 3;
		else
			return (-EINVAL);

		/* LINTED - E_EQUALITY_NOT_ASSIGNMENT */
		if ((r = elmr_write(adap, VSC_REG(1, port, 0),
		    0xa590 | (mode << 2))) ||
		    (r = elmr_write(adap, VSC_REG(1, port, 0xb),
		    0x91 | (clk << 1))) ||
		    (r = elmr_write(adap, VSC_REG(1, port, 0xb),
		    0x90 | (clk << 1))) ||
		    (r = elmr_write(adap, VSC_REG(1, port, 0),
		    0xa593 | (mode << 2))))
			return (r);
	}

	/* QUANTA = 32 * 1024 * 8 / 512 */
	r = (fc & PAUSE_RX) ? 0x60200 : 0x20200;
	if (fc & PAUSE_TX)
		r |= (1 << 19);
	return (elmr_write(adap, VSC_REG(1, port, 1), r));
}

int
t3_vsc7323_set_mtu(adapter_t *adap, unsigned int mtu, int port)
{
	return (elmr_write(adap, VSC_REG(1, port, 2), mtu));
}

int
t3_vsc7323_set_addr(adapter_t *adap, u8 addr[6], int port)
{
	int ret;

	ret = elmr_write(adap, VSC_REG(1, port, 3),
	    (addr[0] << 16) | (addr[1] << 8) | addr[2]);
	if (!ret)
		ret = elmr_write(adap, VSC_REG(1, port, 4),
		    (addr[3] << 16) | (addr[4] << 8) | addr[5]);
	return (ret);
}

int
t3_vsc7323_enable(adapter_t *adap, int port, int which)
{
	int ret;
	unsigned int v, orig;

	ret = t3_elmr_blk_read(adap, VSC_REG(1, port, 0), &v, 1);
	if (!ret) {
		orig = v;
		if (which & MAC_DIRECTION_TX)
			v |= 1;
		if (which & MAC_DIRECTION_RX)
			v |= 2;
		if (v != orig)
			ret = elmr_write(adap, VSC_REG(1, port, 0), v);
	}
	return (ret);
}

int
t3_vsc7323_disable(adapter_t *adap, int port, int which)
{
	int ret;
	unsigned int v, orig;

	ret = t3_elmr_blk_read(adap, VSC_REG(1, port, 0), &v, 1);
	if (!ret) {
		orig = v;
		if (which & MAC_DIRECTION_TX)
			v &= ~1;
		if (which & MAC_DIRECTION_RX)
			v &= ~2;
		if (v != orig)
			ret = elmr_write(adap, VSC_REG(1, port, 0), v);
	}
	return (ret);
}

#define	STATS0_START 1
#define	STATS1_START 0x24
#define	NSTATS0 (0x1d - STATS0_START + 1)
#define	NSTATS1 (0x2a - STATS1_START + 1)

#define	ELMR_STAT(port, reg) (ELMR_STATS + port * 0x40 + reg)

const struct mac_stats *
t3_vsc7323_update_stats(struct cmac *mac)
{
	int ret;
	u64 rx_ucast, tx_ucast;
	u32 stats0[NSTATS0], stats1[NSTATS1];

	ret = t3_elmr_blk_read(mac->adapter,
	    ELMR_STAT(mac->ext_port, STATS0_START),
	    stats0, NSTATS0);
	if (!ret)
		ret = t3_elmr_blk_read(mac->adapter,
		    ELMR_STAT(mac->ext_port, STATS1_START),
		    stats1, NSTATS1);
	if (ret)
		goto out;

	/*
	 * HW counts Rx/Tx unicast frames but we want all the frames.
	 */
	rx_ucast = mac->stats.rx_frames - mac->stats.rx_mcast_frames -
	    mac->stats.rx_bcast_frames;
	rx_ucast += (u64)(stats0[6 - STATS0_START] - (u32)rx_ucast);
	tx_ucast = mac->stats.tx_frames - mac->stats.tx_mcast_frames -
	    mac->stats.tx_bcast_frames;
	tx_ucast += (u64)(stats0[27 - STATS0_START] - (u32)tx_ucast);

#define	RMON_UPDATE(mac, name, hw_stat) \
	mac->stats.name += (u64)((hw_stat) - (u32)(mac->stats.name))

	RMON_UPDATE(mac, rx_octets, stats0[4 - STATS0_START]);
	RMON_UPDATE(mac, rx_frames, stats0[6 - STATS0_START]);
	RMON_UPDATE(mac, rx_frames, stats0[7 - STATS0_START]);
	RMON_UPDATE(mac, rx_frames, stats0[8 - STATS0_START]);
	RMON_UPDATE(mac, rx_mcast_frames, stats0[7 - STATS0_START]);
	RMON_UPDATE(mac, rx_bcast_frames, stats0[8 - STATS0_START]);
	RMON_UPDATE(mac, rx_fcs_errs, stats0[9 - STATS0_START]);
	RMON_UPDATE(mac, rx_pause, stats0[2 - STATS0_START]);
	RMON_UPDATE(mac, rx_jabber, stats0[16 - STATS0_START]);
	RMON_UPDATE(mac, rx_short, stats0[11 - STATS0_START]);
	RMON_UPDATE(mac, rx_symbol_errs, stats0[1 - STATS0_START]);
	RMON_UPDATE(mac, rx_too_long, stats0[15 - STATS0_START]);

	RMON_UPDATE(mac, rx_frames_64,		stats0[17 - STATS0_START]);
	RMON_UPDATE(mac, rx_frames_65_127,	stats0[18 - STATS0_START]);
	RMON_UPDATE(mac, rx_frames_128_255,	stats0[19 - STATS0_START]);
	RMON_UPDATE(mac, rx_frames_256_511,	stats0[20 - STATS0_START]);
	RMON_UPDATE(mac, rx_frames_512_1023,	stats0[21 - STATS0_START]);
	RMON_UPDATE(mac, rx_frames_1024_1518,	stats0[22 - STATS0_START]);
	RMON_UPDATE(mac, rx_frames_1519_max,	stats0[23 - STATS0_START]);

	RMON_UPDATE(mac, tx_octets, stats0[26 - STATS0_START]);
	RMON_UPDATE(mac, tx_frames, stats0[27 - STATS0_START]);
	RMON_UPDATE(mac, tx_frames, stats0[28 - STATS0_START]);
	RMON_UPDATE(mac, tx_frames, stats0[29 - STATS0_START]);
	RMON_UPDATE(mac, tx_mcast_frames, stats0[28 - STATS0_START]);
	RMON_UPDATE(mac, tx_bcast_frames, stats0[29 - STATS0_START]);
	RMON_UPDATE(mac, tx_pause, stats0[25 - STATS0_START]);

	/* LINTED - E_UNEXPECTED_UINT_PROMOTION */
	RMON_UPDATE(mac, tx_underrun, 0);

	RMON_UPDATE(mac, tx_frames_64,		stats1[36 - STATS1_START]);
	RMON_UPDATE(mac, tx_frames_65_127,	stats1[37 - STATS1_START]);
	RMON_UPDATE(mac, tx_frames_128_255,	stats1[38 - STATS1_START]);
	RMON_UPDATE(mac, tx_frames_256_511,	stats1[39 - STATS1_START]);
	RMON_UPDATE(mac, tx_frames_512_1023,	stats1[40 - STATS1_START]);
	RMON_UPDATE(mac, tx_frames_1024_1518,	stats1[41 - STATS1_START]);
	RMON_UPDATE(mac, tx_frames_1519_max,	stats1[42 - STATS1_START]);

#undef RMON_UPDATE

	mac->stats.rx_frames = rx_ucast + mac->stats.rx_mcast_frames +
	    mac->stats.rx_bcast_frames;
	mac->stats.tx_frames = tx_ucast + mac->stats.tx_mcast_frames +
	    mac->stats.tx_bcast_frames;
out:    return &mac->stats;
}

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
 * Copyright (C) 2003-2005 Chelsio Communications.  All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/* Driver for Vitesse VSC7326 (Schaumburg) MAC */

#include "gmac.h"
#include "elmer0.h"
#include "vsc7326_reg.h"

FILE_IDENT("@(#) $Id: vsc7326.c,v 1.17 2005/10/29 05:42:36 sbardone Exp $");

/* Update fast changing statistics every 15 seconds */
#define STATS_TICK_SECS 15
/* 30 minutes for full statistics update */
#define MAJOR_UPDATE_TICKS (1800 / STATS_TICK_SECS)

#define MAX_MTU 9600

struct init_table {
	u32 addr;
	u32 data;
};

struct _cmac_instance {
	u32 index;
	u32 ticks;
};

#define INITBLOCK_SLEEP	0xffffffff

static void vsc_read(adapter_t *adapter, u32 addr, u32 *val)
{
	u32 status, vlo, vhi;
	int i;

	MAC_LOCK(adapter->mac_lock);
	(void) t1_tpi_read(adapter, (addr << 2) + 4, &vlo);
	i = 0;
	do {
		(void) t1_tpi_read(adapter, (REG_LOCAL_STATUS << 2) + 4, &vlo);
		(void) t1_tpi_read(adapter, REG_LOCAL_STATUS << 2, &vhi);
		status = (vhi << 16) | vlo;
		i++;
	} while (((status & 1) == 0) && (i < 50));
	if (i == 50)
		CH_ERR("Invalid tpi read from MAC, breaking loop.\n");

	(void) t1_tpi_read(adapter, (REG_LOCAL_DATA << 2) + 4, &vlo);
	(void) t1_tpi_read(adapter, REG_LOCAL_DATA << 2, &vhi);

	*val = (vhi << 16) | vlo;

	/* CH_ERR("rd: block: 0x%x  sublock: 0x%x  reg: 0x%x  data: 0x%x\n",
		((addr&0xe000)>>13), ((addr&0x1e00)>>9),
		((addr&0x01fe)>>1), *val); */
	MAC_UNLOCK(adapter->mac_lock);
}

static void vsc_write(adapter_t *adapter, u32 addr, u32 data)
{
	MAC_LOCK(adapter->mac_lock);
	(void) t1_tpi_write(adapter, (addr << 2) + 4, data & 0xFFFF);
	(void) t1_tpi_write(adapter, addr << 2, (data >> 16) & 0xFFFF);
	/* CH_ERR("wr: block: 0x%x  sublock: 0x%x  reg: 0x%x  data: 0x%x\n",
		((addr&0xe000)>>13), ((addr&0x1e00)>>9),
		((addr&0x01fe)>>1), data); */
	MAC_UNLOCK(adapter->mac_lock);
}

/* Hard reset the MAC.  This wipes out *all* configuration. */
static void vsc7326_full_reset(adapter_t* adapter)
{
	u32 val;
	u32 result = 0xffff;

	(void) t1_tpi_read(adapter, A_ELMER0_GPO, &val);
	val &= ~1;
	(void) t1_tpi_write(adapter, A_ELMER0_GPO, val);
	DELAY_US(2);
	val |= 0x1;	/* Enable mac MAC itself */
	val |= 0x800;	/* Turn off the red LED */
	(void) t1_tpi_write(adapter, A_ELMER0_GPO, val);
	DELAY_MS(1);
	vsc_write(adapter, REG_SW_RESET, 0x80000001);
	do {
	DELAY_MS(1);
		vsc_read(adapter, REG_SW_RESET, &result);
	} while (result != 0x0);
}

static struct init_table vsc7326_reset[] = {
	{      REG_IFACE_MODE, 0x00000000 },
	{         REG_CRC_CFG, 0x00000020 },
	{   REG_PLL_CLK_SPEED, 0x00050c00 },
	{   REG_PLL_CLK_SPEED, 0x00050c00 },
	{            REG_MSCH, 0x00002f14 },
	{       REG_SPI4_MISC, 0x00040409 },
	{     REG_SPI4_DESKEW, 0x00080000 },
	{ REG_SPI4_ING_SETUP2, 0x08080004 },
	{ REG_SPI4_ING_SETUP0, 0x04111004 },
	{ REG_SPI4_EGR_SETUP0, 0x80001a04 },
	{ REG_SPI4_ING_SETUP1, 0x02010000 },
	{      REG_AGE_INC(0), 0x00000000 },
	{      REG_AGE_INC(1), 0x00000000 },
	{     REG_ING_CONTROL, 0x0a200011 },
	{     REG_EGR_CONTROL, 0xa0010091 },
};

static struct init_table vsc7326_portinit[4][22] = {
	{	/* Port 0 */
			/* FIFO setup */
		{           REG_DBG(0), 0x000004f0 },
		{           REG_HDX(0), 0x00073101 },
		{        REG_TEST(0,0), 0x00000022 },
		{        REG_TEST(1,0), 0x00000022 },
		{  REG_TOP_BOTTOM(0,0), 0x003f0000 },
		{  REG_TOP_BOTTOM(1,0), 0x00120000 },
		{ REG_HIGH_LOW_WM(0,0), 0x07460757 },
		{ REG_HIGH_LOW_WM(1,0), 0x01a01fff },
		{   REG_CT_THRHLD(0,0), 0x00000000 },
		{   REG_CT_THRHLD(1,0), 0x00000000 },
		{         REG_BUCKE(0), 0x0002ffff },
		{         REG_BUCKI(0), 0x0002ffff },
		{        REG_TEST(0,0), 0x00000020 },
		{        REG_TEST(1,0), 0x00000020 },
			/* Port config */
		{       REG_MAX_LEN(0), 0x00002710 },
		{     REG_PORT_FAIL(0), 0x00000002 },
		{    REG_NORMALIZER(0), 0x00000a64 },
		{        REG_DENORM(0), 0x00000010 },
		{     REG_STICK_BIT(0), 0x03baa370 },
		{     REG_DEV_SETUP(0), 0x00000083 },
		{     REG_DEV_SETUP(0), 0x00000082 },
		{      REG_MODE_CFG(0), 0x0200259f },
	},
	{	/* Port 1 */
			/* FIFO setup */
		{           REG_DBG(1), 0x000004f0 },
		{           REG_HDX(1), 0x00073101 },
		{        REG_TEST(0,1), 0x00000022 },
		{        REG_TEST(1,1), 0x00000022 },
		{  REG_TOP_BOTTOM(0,1), 0x007e003f },
		{  REG_TOP_BOTTOM(1,1), 0x00240012 },
		{ REG_HIGH_LOW_WM(0,1), 0x07460757 },
		{ REG_HIGH_LOW_WM(1,1), 0x01a01fff },
		{   REG_CT_THRHLD(0,1), 0x00000000 },
		{   REG_CT_THRHLD(1,1), 0x00000000 },
		{         REG_BUCKE(1), 0x0002ffff },
		{         REG_BUCKI(1), 0x0002ffff },
		{        REG_TEST(0,1), 0x00000020 },
		{        REG_TEST(1,1), 0x00000020 },
			/* Port config */
		{       REG_MAX_LEN(1), 0x00002710 },
		{     REG_PORT_FAIL(1), 0x00000002 },
		{    REG_NORMALIZER(1), 0x00000a64 },
		{        REG_DENORM(1), 0x00000010 },
		{     REG_STICK_BIT(1), 0x03baa370 },
		{     REG_DEV_SETUP(1), 0x00000083 },
		{     REG_DEV_SETUP(1), 0x00000082 },
		{      REG_MODE_CFG(1), 0x0200259f },
	},
	{	/* Port 2 */
			/* FIFO setup */
		{           REG_DBG(2), 0x000004f0 },
		{           REG_HDX(2), 0x00073101 },
		{        REG_TEST(0,2), 0x00000022 },
		{        REG_TEST(1,2), 0x00000022 },
		{  REG_TOP_BOTTOM(0,2), 0x00bd007e },
		{  REG_TOP_BOTTOM(1,2), 0x00360024 },
		{ REG_HIGH_LOW_WM(0,2), 0x07460757 },
		{ REG_HIGH_LOW_WM(1,2), 0x01a01fff },
		{   REG_CT_THRHLD(0,2), 0x00000000 },
		{   REG_CT_THRHLD(1,2), 0x00000000 },
		{         REG_BUCKE(2), 0x0002ffff },
		{         REG_BUCKI(2), 0x0002ffff },
		{        REG_TEST(0,2), 0x00000020 },
		{        REG_TEST(1,2), 0x00000020 },
			/* Port config */
		{       REG_MAX_LEN(2), 0x00002710 },
		{     REG_PORT_FAIL(2), 0x00000002 },
		{    REG_NORMALIZER(2), 0x00000a64 },
		{        REG_DENORM(2), 0x00000010 },
		{     REG_STICK_BIT(2), 0x03baa370 },
		{     REG_DEV_SETUP(2), 0x00000083 },
		{     REG_DEV_SETUP(2), 0x00000082 },
		{      REG_MODE_CFG(2), 0x0200259f },
	},
	{	/* Port 3 */
			/* FIFO setup */
		{           REG_DBG(3), 0x000004f0 },
		{           REG_HDX(3), 0x00073101 },
		{        REG_TEST(0,3), 0x00000022 },
		{        REG_TEST(1,3), 0x00000022 },
		{  REG_TOP_BOTTOM(0,3), 0x00fc00bd },
		{  REG_TOP_BOTTOM(1,3), 0x00480036 },
		{ REG_HIGH_LOW_WM(0,3), 0x07460757 },
		{ REG_HIGH_LOW_WM(1,3), 0x01a01fff },
		{   REG_CT_THRHLD(0,3), 0x00000000 },
		{   REG_CT_THRHLD(1,3), 0x00000000 },
		{         REG_BUCKE(3), 0x0002ffff },
		{         REG_BUCKI(3), 0x0002ffff },
		{        REG_TEST(0,3), 0x00000020 },
		{        REG_TEST(1,3), 0x00000020 },
			/* Port config */
		{       REG_MAX_LEN(3), 0x00002710 },
		{     REG_PORT_FAIL(3), 0x00000002 },
		{    REG_NORMALIZER(3), 0x00000a64 },
		{        REG_DENORM(3), 0x00000010 },
		{     REG_STICK_BIT(3), 0x03baa370 },
		{     REG_DEV_SETUP(3), 0x00000083 },
		{     REG_DEV_SETUP(3), 0x00000082 },
		{      REG_MODE_CFG(3), 0x0200259f },
	},
};

static void run_table(adapter_t *adapter, struct init_table *ib, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (ib[i].addr == INITBLOCK_SLEEP) {
			DELAY_US( ib[i].data );
			CH_ERR("sleep %d us\n",ib[i].data);
		} else {
			vsc_write( adapter, ib[i].addr, ib[i].data );
		}
	}
}

static int bist_rd(adapter_t *adapter, int moduleid, int address)
{
	int data=0;
	u32 result=0;

	if(	(address != 0x0) &&
		(address != 0x1) &&
		(address != 0x2) &&
		(address != 0xd) &&
		(address != 0xe))
			CH_ERR("No bist address: 0x%x\n", address);

	data = ((0x00 << 24) | ((address & 0xff) << 16) | (0x00 << 8) |
		((moduleid & 0xff) << 0));
	vsc_write(adapter, REG_RAM_BIST_CMD, data);

	DELAY_US(10);

	vsc_read(adapter, REG_RAM_BIST_RESULT, &result);
	if((result & (1<<9)) != 0x0)
		CH_ERR("Still in bist read: 0x%x\n", result);
	else if((result & (1<<8)) != 0x0)
		CH_ERR("bist read error: 0x%x\n", result);

	return(result & 0xff);
}

static int bist_wr(adapter_t *adapter, int moduleid, int address, int value)
{
	int data=0;
	u32 result=0;

	if(	(address != 0x0) &&
		(address != 0x1) &&
		(address != 0x2) &&
		(address != 0xd) &&
		(address != 0xe))
			CH_ERR("No bist address: 0x%x\n", address);

	if( value>255 )
		CH_ERR("Suspicious write out of range value: 0x%x\n", value);

	data = ((0x01 << 24) | ((address & 0xff) << 16) | (value << 8) |
		((moduleid & 0xff) << 0));
	vsc_write(adapter, REG_RAM_BIST_CMD, data);

	DELAY_US(5);

	vsc_read(adapter, REG_RAM_BIST_CMD, &result);
	if((result & (1<<27)) != 0x0)
		CH_ERR("Still in bist write: 0x%x\n", result);
	else if((result & (1<<26)) != 0x0)
		CH_ERR("bist write error: 0x%x\n", result);

	return(0);
}

static int run_bist(adapter_t *adapter, int moduleid)
{
	/*run bist*/
	(void) bist_wr(adapter,moduleid, 0x00, 0x02);
	(void) bist_wr(adapter,moduleid, 0x01, 0x01);

	return(0);
}

static int check_bist(adapter_t *adapter, int moduleid)
{
	int result=0;
	int column=0;
	/*check bist*/
	result = bist_rd(adapter,moduleid, 0x02);
	column = ((bist_rd(adapter,moduleid, 0x0e)<<8) +
			(bist_rd(adapter,moduleid, 0x0d)));
	if ((result & 3) != 0x3)
		CH_ERR("Result: 0x%x  BIST error in ram %d, column: 0x%04x\n",
			result, moduleid, column);
	return(0);
}

static int enable_mem(adapter_t *adapter, int moduleid)
{
	/*enable mem*/
	(void) bist_wr(adapter,moduleid, 0x00, 0x00);
	return(0);
}

static int run_bist_all(adapter_t *adapter)
{
	int port=0;
	u32 val=0;

	vsc_write(adapter, REG_MEM_BIST, 0x5);
	vsc_read(adapter, REG_MEM_BIST, &val);

	for(port=0; port<12; port++){
		vsc_write(adapter, REG_DEV_SETUP(port), 0x0);
	}

	DELAY_US(300);
	vsc_write(adapter, REG_SPI4_MISC, 0x00040409);
	DELAY_US(300);

	(void) run_bist(adapter,13);
	(void) run_bist(adapter,14);
	(void) run_bist(adapter,20);
	(void) run_bist(adapter,21);
	DELAY_MS(200);
	(void) check_bist(adapter,13);
	(void) check_bist(adapter,14);
	(void) check_bist(adapter,20);
	(void) check_bist(adapter,21);
	DELAY_US(100);
	(void) enable_mem(adapter,13);
	(void) enable_mem(adapter,14);
	(void) enable_mem(adapter,20);
	(void) enable_mem(adapter,21);
	DELAY_US(300);
	vsc_write(adapter, REG_SPI4_MISC, 0x60040400);
	DELAY_US(300);
	for(port=0; port<12; port++){
		vsc_write(adapter, REG_DEV_SETUP(port), 0x1);
	}
	DELAY_US(300);
	vsc_write(adapter, REG_MEM_BIST, 0x0);
	DELAY_MS(10);
	return(0);
}

/* ARGSUSED */
static int mac_intr_handler(struct cmac *mac)
{
	return 0;
}

/* ARGSUSED */
static int mac_intr_enable(struct cmac *mac)
{
	return 0;
}

/* ARGSUSED */
static int mac_intr_disable(struct cmac *mac)
{
	return 0;
}

/* ARGSUSED */
static int mac_intr_clear(struct cmac *mac)
{
	return 0;
}

/* Expect MAC address to be in network byte order. */
static int mac_set_address(struct cmac* mac, u8 addr[6])
{
	u32 val;
	int port = mac->instance->index;

	vsc_write(mac->adapter, REG_MAC_LOW_ADDR(port),
		  (addr[3] << 16) | (addr[4] << 8) | addr[5]);
	vsc_write(mac->adapter, REG_MAC_HIGH_ADDR(port),
		  (addr[0] << 16) | (addr[1] << 8) | addr[2]);

	vsc_read(mac->adapter, REG_ING_FFILT_UM_EN, &val);
	val &= ~0xf0000000;
	vsc_write(mac->adapter, REG_ING_FFILT_UM_EN, val | (port << 28));
	
	vsc_write(mac->adapter, REG_ING_FFILT_MASK0,
		  0xffff0000 | (addr[4] << 8) | addr[5]);
	vsc_write(mac->adapter, REG_ING_FFILT_MASK1,
		  0xffff0000 | (addr[2] << 8) | addr[3]);
	vsc_write(mac->adapter, REG_ING_FFILT_MASK2,
		  0xffff0000 | (addr[0] << 8) | addr[1]);
	return 0;
}

static int mac_get_address(struct cmac *mac, u8 addr[6])
{
	u32 addr_lo, addr_hi;
	int port = mac->instance->index;

	vsc_read(mac->adapter, REG_MAC_LOW_ADDR(port), &addr_lo);
	vsc_read(mac->adapter, REG_MAC_HIGH_ADDR(port), &addr_hi);

	addr[0] = (u8) (addr_hi >> 16);
	addr[1] = (u8) (addr_hi >> 8);
	addr[2] = (u8) addr_hi;
	addr[3] = (u8) (addr_lo >> 16);
	addr[4] = (u8) (addr_lo >> 8);
	addr[5] = (u8) addr_lo;
	return 0;
}

/* This is intended to reset a port, not the whole MAC */
static int mac_reset(struct cmac *mac)
{
	int index = mac->instance->index;

	run_table(mac->adapter, vsc7326_portinit[index],
		  DIMOF(vsc7326_portinit[index]));

	return 0;
}

static int mac_set_rx_mode(struct cmac *mac, struct t1_rx_mode *rm)
{
	u32 v;
	int port = mac->instance->index;

	vsc_read(mac->adapter, REG_ING_FFILT_UM_EN, &v);
	v |= 1 << 12;
	
	if (t1_rx_mode_promisc(rm))
		v &= ~(1 << (port + 16));
	else
		v |= 1 << (port + 16);

	vsc_write(mac->adapter, REG_ING_FFILT_UM_EN, v);
	return 0;
}

static int mac_set_mtu(struct cmac *mac, int mtu)
{
	int port = mac->instance->index;

	if (mtu > MAX_MTU)
		return -EINVAL;

	/* max_len includes header and FCS */
	vsc_write(mac->adapter, REG_MAX_LEN(port), mtu + 14 + 4);
	return 0;
}

static int mac_set_speed_duplex_fc(struct cmac *mac, int speed, int duplex,
				   int fc)
{
	u32 v;
	int enable, port = mac->instance->index;

	if (speed >= 0 && speed != SPEED_10 && speed != SPEED_100 &&
	    speed != SPEED_1000)
		return -1;
	if (duplex > 0 && duplex != DUPLEX_FULL)
		return -1;

	if (speed >= 0) {
		vsc_read(mac->adapter, REG_MODE_CFG(port), &v);
		enable = v & 3;             /* save tx/rx enables */
		v &= ~0xf;
		v |= 4;                     /* full duplex */
		if (speed == SPEED_1000)
			v |= 8;             /* GigE */
		enable |= v;
		vsc_write(mac->adapter, REG_MODE_CFG(port), v);
		
		if (speed == SPEED_1000)
			v = 0x82;
		else if (speed == SPEED_100)
			v = 0x84;
		else	/* SPEED_10 */
			v = 0x86; 
		vsc_write(mac->adapter, REG_DEV_SETUP(port), v | 1); /* reset */
		vsc_write(mac->adapter, REG_DEV_SETUP(port), v);
		vsc_read(mac->adapter, REG_DBG(port), &v);
		v &= ~0xff00;
		if (speed == SPEED_1000)
			v |= 0x400;
		else if (speed == SPEED_100)
			v |= 0x2000;
		else	/* SPEED_10 */
			v |= 0xff00;
		vsc_write(mac->adapter, REG_DBG(port), v);

		vsc_write(mac->adapter, REG_TX_IFG(port),
			  speed == SPEED_1000 ? 5 : 0x11);
		if (duplex == DUPLEX_HALF)
			enable = 0x0;	/* 100 or 10 */
		else if (speed == SPEED_1000)
			enable = 0xc;
		else	/* SPEED_100 or 10 */
			enable = 0x4;
		enable |= 0x9 << 10;	/* IFG1 */
		enable |= 0x6 << 6;	/* IFG2 */
		enable |= 0x1 << 4;	/* VLAN */
		enable |= 0x3;		/* RX/TX EN */
		vsc_write(mac->adapter, REG_MODE_CFG(port), enable);
		
	}

	vsc_read(mac->adapter, REG_PAUSE_CFG(port), &v);
	v &= 0xfff0ffff;
	v |= 0x20000;      /* xon/xoff */
	if (fc & PAUSE_RX)
		v |= 0x40000;
	if (fc & PAUSE_TX)
		v |= 0x80000;
	if (fc == (PAUSE_RX | PAUSE_TX))
		v |= 0x10000;
	vsc_write(mac->adapter, REG_PAUSE_CFG(port), v);
	return 0;
}

static int mac_enable(struct cmac *mac, int which)
{
	u32 val;
	int port = mac->instance->index;

	vsc_read(mac->adapter, REG_MODE_CFG(port), &val);
	if (which & MAC_DIRECTION_RX)
		val |= 0x2;
	if (which & MAC_DIRECTION_TX)
		val |= 1;
	vsc_write(mac->adapter, REG_MODE_CFG(port), val);
	return 0;
}

static int mac_disable(struct cmac *mac, int which)
{
	u32 val;
	int i, port = mac->instance->index;

	/* Reset the port */
	(void) mac_reset(mac);

	vsc_read(mac->adapter, REG_MODE_CFG(port), &val);
	if (which & MAC_DIRECTION_RX)
		val &= ~0x2;
	if (which & MAC_DIRECTION_TX)
		val &= ~0x1;
	vsc_write(mac->adapter, REG_MODE_CFG(port), val);
	vsc_read(mac->adapter, REG_MODE_CFG(port), &val);

	/* Clear stats */
	for (i = 0; i <= 0x3a; ++i)
		vsc_write(mac->adapter, CRA(4, port, i), 0);

	/* Clear sofware counters */
	memset(&mac->stats, 0, sizeof(struct cmac_statistics));

	return 0;
}

static void rmon_update(struct cmac *mac, unsigned int addr, u64 *stat)
{
	u32 v, lo;

	vsc_read(mac->adapter, addr, &v);
	lo = *stat;
	*stat = *stat - lo + v;

	if (v == 0)
		return;

	if (v < lo)
		*stat += (1ULL << 32);
}

static void port_stats_update(struct cmac *mac)
{
	int port = mac->instance->index;

	/* Rx stats */
	rmon_update(mac, REG_RX_OK_BYTES(port), &mac->stats.RxOctetsOK);
	rmon_update(mac, REG_RX_BAD_BYTES(port), &mac->stats.RxOctetsBad);
	rmon_update(mac, REG_RX_UNICAST(port), &mac->stats.RxUnicastFramesOK);
	rmon_update(mac, REG_RX_MULTICAST(port),
		    &mac->stats.RxMulticastFramesOK);
	rmon_update(mac, REG_RX_BROADCAST(port),
		    &mac->stats.RxBroadcastFramesOK);
	rmon_update(mac, REG_CRC(port), &mac->stats.RxFCSErrors);
	rmon_update(mac, REG_RX_ALIGNMENT(port), &mac->stats.RxAlignErrors);
	rmon_update(mac, REG_RX_OVERSIZE(port),
		    &mac->stats.RxFrameTooLongErrors);
	rmon_update(mac, REG_RX_PAUSE(port), &mac->stats.RxPauseFrames);
	rmon_update(mac, REG_RX_JABBERS(port), &mac->stats.RxJabberErrors);
	rmon_update(mac, REG_RX_FRAGMENTS(port), &mac->stats.RxRuntErrors);
	rmon_update(mac, REG_RX_UNDERSIZE(port), &mac->stats.RxRuntErrors);
	rmon_update(mac, REG_RX_SYMBOL_CARRIER(port),
		    &mac->stats.RxSymbolErrors);
	rmon_update(mac, REG_RX_SIZE_1519_TO_MAX(port), 
            &mac->stats.RxJumboFramesOK);

	/* Tx stats (skip collision stats as we are full-duplex only) */
	rmon_update(mac, REG_TX_OK_BYTES(port), &mac->stats.TxOctetsOK);
	rmon_update(mac, REG_TX_UNICAST(port), &mac->stats.TxUnicastFramesOK);
	rmon_update(mac, REG_TX_MULTICAST(port),
		    &mac->stats.TxMulticastFramesOK);
	rmon_update(mac, REG_TX_BROADCAST(port),
		    &mac->stats.TxBroadcastFramesOK);
	rmon_update(mac, REG_TX_PAUSE(port), &mac->stats.TxPauseFrames);
	rmon_update(mac, REG_TX_UNDERRUN(port), &mac->stats.TxUnderrun);
	rmon_update(mac, REG_TX_SIZE_1519_TO_MAX(port), 
            &mac->stats.TxJumboFramesOK);
}

/*
 * This function is called periodically to accumulate the current values of the
 * RMON counters into the port statistics.  Since the counters are only 32 bits
 * some of them can overflow in less than a minute at GigE speeds, so this
 * function should be called every 30 seconds or so.
 *
 * To cut down on reading costs we update only the octet counters at each tick
 * and do a full update at major ticks, which can be every 30 minutes or more.
 */
static const struct cmac_statistics *mac_update_statistics(struct cmac *mac,
							   int flag)
{
	if (flag == MAC_STATS_UPDATE_FULL ||
	    mac->instance->ticks >= MAJOR_UPDATE_TICKS) {
		port_stats_update(mac);
		mac->instance->ticks = 0;
	} else {
		int port = mac->instance->index;

		rmon_update(mac, REG_RX_OK_BYTES(port),
			    &mac->stats.RxOctetsOK);
		rmon_update(mac, REG_RX_BAD_BYTES(port),
			    &mac->stats.RxOctetsBad);
		rmon_update(mac, REG_TX_OK_BYTES(port),
			    &mac->stats.TxOctetsOK);
		mac->instance->ticks++;
	}
	return &mac->stats;
}

static void mac_destroy(struct cmac *mac)
{
	t1_os_free((void *)mac, sizeof(*mac) + sizeof(cmac_instance));
}

#ifdef C99_NOT_SUPPORTED
static struct cmac_ops vsc7326_ops = {
	mac_destroy,
	mac_reset,
	mac_intr_enable,
	mac_intr_disable,
	mac_intr_clear,
	mac_intr_handler,
	mac_enable,
	mac_disable,
	NULL,
	NULL,
	mac_set_mtu,
	mac_set_rx_mode,
	mac_set_speed_duplex_fc,
	NULL,
	mac_update_statistics,
	mac_get_address,
	mac_set_address
};
#else
static struct cmac_ops vsc7326_ops = {
	.destroy                  = mac_destroy,
	.reset                    = mac_reset,
	.interrupt_handler        = mac_intr_handler,
	.interrupt_enable         = mac_intr_enable,
	.interrupt_disable        = mac_intr_disable,
	.interrupt_clear          = mac_intr_clear,
	.enable                   = mac_enable,
	.disable                  = mac_disable,
	.set_mtu                  = mac_set_mtu,
	.set_rx_mode              = mac_set_rx_mode,
	.set_speed_duplex_fc      = mac_set_speed_duplex_fc,
	.statistics_update        = mac_update_statistics,
	.macaddress_get           = mac_get_address,
	.macaddress_set           = mac_set_address,
};
#endif

static struct cmac *vsc7326_mac_create(adapter_t *adapter, int index)
{
	struct cmac *mac;
	u32 val;
	int i;

	mac = t1_os_malloc_wait_zero(sizeof(*mac) + sizeof(cmac_instance));
	if (!mac) return NULL;

	mac->ops = &vsc7326_ops;
	mac->instance = (cmac_instance *)(mac + 1);
	mac->adapter  = adapter;

	mac->instance->index = index;
	mac->instance->ticks = 0;

	i = 0;
	do {
		u32 vhi, vlo;

		vhi = vlo = 0;
		(void) t1_tpi_read(adapter, (REG_LOCAL_STATUS << 2) + 4, &vlo);
		DELAY_US(1);
		(void) t1_tpi_read(adapter, REG_LOCAL_STATUS << 2, &vhi);
		DELAY_US(5);
		val = (vhi << 16) | vlo;
	} while ((++i < 10000) && (val == 0xffffffff));

	return mac;
}

static int vsc7326_mac_reset(adapter_t *adapter)
{
	vsc7326_full_reset(adapter);
	(void) run_bist_all(adapter);
	run_table(adapter, vsc7326_reset, DIMOF(vsc7326_reset));
	return 0;
}

struct gmac t1_vsc7326_ops = {
	STATS_TICK_SECS,
	vsc7326_mac_create,
	vsc7326_mac_reset
};

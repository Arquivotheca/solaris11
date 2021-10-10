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
 * Copyright (C) 2005-2010 Chelsio Communications.  All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 * release for licensing terms and conditions.
 */

#include "cxge_common.h"

int
t3_wait_op_done(adapter_t *adapter, int reg, u32 mask,
	int polarity, int attempts, int delay)
{
	return (t3_wait_op_done_val(adapter, reg, mask, polarity, attempts,
	    delay, NULL));
}

unsigned int
is_pcie(const adapter_t *adap)
{
	return (adap->params.pci.variant == PCI_VARIANT_PCIE);
}

unsigned int
core_ticks_per_usec(const adapter_t *adap)
{
	return (adap->params.vpd.cclk / 1000);
}

unsigned int
dack_ticks_to_usec(const adapter_t *adap, unsigned int ticks)
{
	return ((ticks << adap->params.tp.dack_re) / core_ticks_per_usec(adap));
}

int
is_offload(const adapter_t *adap)
{
#if defined(CONFIG_CHELSIO_T3_CORE)
	return (adap->params.offload);
#else
	return (0);
#endif
}

int
is_10G(const adapter_t *adap)
{
	return (adapter_info(adap)->caps & SUPPORTED_10000baseT_Full);
}

int
uses_xaui(const adapter_t *adap)
{
	return (adapter_info(adap)->caps & SUPPORTED_AUI);
}

/* Convenience initializer */
void
cphy_init(struct cphy *phy, p_adapter_t adapter, pinfo_t *pinfo,
	int phy_addr, struct cphy_ops *phy_ops,
	const struct mdio_ops *mdio_ops, unsigned int caps, const char *desc)
{
	phy->addr	= (u8)phy_addr;
	phy->caps	= caps;
	phy->adapter	= adapter;
	phy->pinfo	= pinfo;
	phy->desc	= desc;
	phy->ops	= phy_ops;
	if (mdio_ops) {
		phy->mdio_read  = mdio_ops->read;
		phy->mdio_write = mdio_ops->write;
	}
}

/* Convenience MDIO read/write wrappers */
int
mdio_read(struct cphy *phy, int mmd, int reg, unsigned int *valp)
{
	return (phy->mdio_read(phy->adapter, phy->addr, mmd, reg, valp));
}

int
mdio_write(struct cphy *phy, int mmd, int reg, unsigned int val)
{
	return (phy->mdio_write(phy->adapter, phy->addr, mmd, reg, val));
}

unsigned int
t3_mc7_size(const struct mc7 *p)
{
	return (p->size);
}

unsigned int
t3_mc5_size(const struct mc5 *p)
{
	return (p->tcam_size);
}

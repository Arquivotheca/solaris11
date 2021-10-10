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
 * Copyright (c) 2010 by Chelsio Communications, Inc.
 */

#include "cxge_common.h"

void
t3_os_set_hw_addr(p_adapter_t adapter, int port_idx, u8 hw_addr[])
{
	bcopy(hw_addr, adapter->port[port_idx].hw_addr, ETHERADDRL);
}

/* ARGSUSED */
int
t3_os_pci_save_state(p_adapter_t adapter)
{
	t3_unimplemented("pci_save_state");
	return (0);
}

/* ARGSUSED */
int
t3_os_pci_restore_state(struct adapter *adapter)
{
	t3_unimplemented("pci_restore_state");
	return (0);
}

uint8_t *
t3_get_next_mcaddr(struct t3_rx_mode *rm)
{
	struct port_info *pi = rm->pi;
	struct mcaddr_list *m = rm->mc_bucket;
	long bidx = rm->mc_bucket_idx;
	uint8_t *rc;

	/* should have a read lock on this throughout */
	ASSERT(rw_read_locked(&pi->rxmode_lock));

	if (rm->idx == pi->mcaddr_count)
		return (NULL);

	ASSERT(bidx > 0 && bidx <= MC_BUCKET_SIZE);
	rc = (uint8_t *)&m->addr[bidx - 1];

	/*
	 * Find next valid addr in this bucket by clearing the rightmost bidx
	 * bits and running the result through ffs again.
	 */
	bidx = ddi_ffs(m->valid & ~((1 << bidx) - 1));

	/* Try the next bucket if no more bits set for this one */
	if (bidx == 0 && (m = m->next)) {
		ASSERT(m->valid); /* m has to have something */
		bidx = ddi_ffs(m->valid);
	}
	rm->mc_bucket = m;
	rm->mc_bucket_idx = bidx;
	rm->idx++;

#ifdef DEBUG
	/* Some healthy paranoia: rx_mode should always make sense. */
	ASSERT(rm->idx <= pi->mcaddr_count);
	if (rm->idx == pi->mcaddr_count) {
		ASSERT(m == NULL);
		ASSERT(bidx == 0);
	} else {
		ASSERT(bidx > 0 && bidx <= MC_BUCKET_SIZE);
	}
#endif

	return (rc);
}

void
t3_init_rx_mode(struct t3_rx_mode *rm, struct port_info *pi)
{
	rm->idx = 0;
	rm->pi = pi;
	rm->mc_bucket = pi->mcaddr_list;
	if (rm->mc_bucket) {
		ASSERT(pi->mcaddr_count);
		rm->mc_bucket_idx = ddi_ffs(rm->mc_bucket->valid);
		ASSERT(rm->mc_bucket_idx > 0);
	} else {
		ASSERT(pi->mcaddr_count == 0);
		rm->mc_bucket_idx = 0;
	}
}

void
t3_os_pci_read_config_1(p_adapter_t adapter, int reg, uint8_t *val)
{
	*val = pci_config_get8(adapter->pci_regh, reg);
}

void
t3_os_pci_read_config_2(p_adapter_t adapter, int reg, uint16_t *val)
{
	*val = pci_config_get16(adapter->pci_regh, reg);
}

void
t3_os_pci_write_config_2(p_adapter_t adapter, int reg, uint16_t val)
{
	pci_config_put16(adapter->pci_regh, reg, val);
}

void
t3_os_pci_read_config_4(p_adapter_t adapter, int reg, uint32_t *val)
{
	*val = pci_config_get32(adapter->pci_regh, reg);
}

void
t3_os_pci_write_config_4(p_adapter_t adapter, int reg, uint32_t val)
{
	pci_config_put32(adapter->pci_regh, reg, val);
}

struct port_info *
adap2pinfo(p_adapter_t adapter, int idx)
{
	return (&adapter->port[idx]);
}

uint32_t
t3_read_reg(p_adapter_t adapter, uint32_t reg_addr)
{
	return (ddi_get32(adapter->regh,
	    /* LINTED - E_BAD_PTR_CAST_ALIGN */
	    (uint32_t *)(adapter->regp + reg_addr)));
}

void
t3_write_reg(p_adapter_t adapter, uint32_t reg_addr, uint32_t val)
{
	/* LINTED - E_BAD_PTR_CAST_ALIGN */
	ddi_put32(adapter->regh, (uint32_t *)(adapter->regp + reg_addr), val);
}

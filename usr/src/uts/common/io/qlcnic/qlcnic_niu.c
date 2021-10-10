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
 * Copyright 2010 QLogic Corporation. All rights reserved.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strlog.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/vtrace.h>
#include <sys/dlpi.h>
#include <sys/strsun.h>
#include <sys/ethernet.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/dditypes.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>

#include <sys/pci.h>

#include "qlcnic_inc.h"
#include "qlcnic.h"

static long phy_lock_timeout = 100000000;

static int phy_lock(struct qlcnic_adapter_s *adapter)
{
	u32 done = 0;
	int timeout = 0;

	while (!done) {
		/* acquire semaphore3 from PCI HW block */
		adapter->qlcnic_pci_read_immediate(adapter,
		    QLCNIC_PCIE_REG(PCIE_SEM3_LOCK), &done);
		if (done == 1)
			break;
		if (timeout >= phy_lock_timeout)
			return (-1);
		timeout++;
	}

	adapter->qlcnic_crb_writelit_adapter(adapter, QLCNIC_PHY_LOCK_ID,
	    PHY_LOCK_DRIVER);
	return (0);
}

static void
phy_unlock(struct qlcnic_adapter_s *adapter)
{
	u32 val;

	/* release semaphore3 */
	adapter->qlcnic_pci_read_immediate(adapter,
	    QLCNIC_PCIE_REG(PCIE_SEM3_UNLOCK), &val);
}

/*
 * qlcnic_niu_gbe_phy_read - read a register from the GbE PHY via
 * mii management interface.
 *
 * Note: The MII management interface goes through port 0.
 *	   Individual phys are addressed as follows:
 *	   [15:8]  phy id
 *	   [7:0]   register number
 *
 * Returns:  0 success
 *	  -1 error
 *
 */
int
qlcnic_niu_gbe_phy_read(struct qlcnic_adapter_s *adapter, u32 reg,
    qlcnic_crbword_t *readval)
{
	u32 phy = adapter->physical_port;
	qlcnic_niu_gb_mii_mgmt_address_t address;
	qlcnic_niu_gb_mii_mgmt_command_t command;
	qlcnic_niu_gb_mii_mgmt_indicators_t status;
	u32 timeout = 0;
	u32 result = 0;
	u32 restore = 0;
	qlcnic_niu_gb_mac_config_0_t mac_cfg0;

	if (phy_lock(adapter) != 0)
		return (-1);

	/*
	 * MII mgmt all goes through port 0 MAC interface, so it cannot be
	 * in reset
	 */
	adapter->qlcnic_hw_read_wx(adapter, QLCNIC_NIU_GB_MAC_CONFIG_0(0),
	    &mac_cfg0, 4);
	if (mac_cfg0.soft_reset) {
		qlcnic_niu_gb_mac_config_0_t temp;
		*(qlcnic_crbword_t *)&temp = 0;
		temp.tx_reset_pb = 1;
		temp.rx_reset_pb = 1;
		temp.tx_reset_mac = 1;
		temp.rx_reset_mac = 1;
		adapter->qlcnic_hw_write_wx(adapter,
		    QLCNIC_NIU_GB_MAC_CONFIG_0(0), &temp, 4);
		restore = 1;
	}

	*(qlcnic_crbword_t *)&address = 0;
	address.reg_addr = (qlcnic_crbword_t)reg;
	address.phy_addr = (qlcnic_crbword_t)phy;
	adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_GB_MII_MGMT_ADDR(0),
	    &address, 4);

	*(qlcnic_crbword_t *)&command = 0; /* turn off any prior activity */
	adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_GB_MII_MGMT_COMMAND(0),
	    &command, 4);

	/* send read command */
	command.read_cycle = 1;
	adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_GB_MII_MGMT_COMMAND(0),
	    &command, 4);

	*(qlcnic_crbword_t *)&status = 0;
	do {
		adapter->qlcnic_hw_read_wx(adapter,
		    QLCNIC_NIU_GB_MII_MGMT_INDICATE(0), &status, 4);
		timeout++;
	} while ((status.busy || status.notvalid) &&
	    (timeout++ < QLCNIC_NIU_PHY_WAITMAX));

	if (timeout < QLCNIC_NIU_PHY_WAITMAX) {
		adapter->qlcnic_hw_read_wx(adapter,
		    QLCNIC_NIU_GB_MII_MGMT_STATUS(0), readval, 4);
		result = 0;
	} else
		result = (u32)-1;

	if (restore)
		adapter->qlcnic_hw_write_wx(adapter,
		    QLCNIC_NIU_GB_MAC_CONFIG_0(0), &mac_cfg0, 4);

	phy_unlock(adapter);

	return (result);
}

/*
 * Return the current station MAC address.
 * Note that the passed-in value must already be in network byte order.
 */
int
qlcnic_niu_macaddr_get(struct qlcnic_adapter_s *adapter, unsigned char *addr)
{
	uint64_t result;
	int phy = adapter->physical_port;

	if (addr == NULL)
		return (-1);
	if ((phy < 0) || (phy > 3))
		return (-1);

	QLCNIC_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);
	if (adapter->ahw.crb_win != 0) {
		adapter->qlcnic_pci_change_crbwindow(adapter, 0);
	}

	result = QLCNIC_PCI_READ_32((void *)(uptr_t)pci_base_offset(adapter,
	    QLCNIC_NIU_GB_STATION_ADDR_1(phy))) >> 16;
	result |= ((uint64_t)QLCNIC_PCI_READ_32((void *)(uptr_t)pci_base_offset(
	    adapter, QLCNIC_NIU_GB_STATION_ADDR_0(phy)))) << 16;

	(void) memcpy(addr, &result, sizeof (qlcnic_ethernet_macaddr_t));

	adapter->qlcnic_pci_change_crbwindow(adapter, QLCNIC_WINDOW_ONE);

	QLCNIC_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);

	return (0);
}

/*
 * Set the station MAC address.
 * Note that the passed-in value must already be in network byte order.
 */
int
qlcnic_niu_macaddr_set(struct qlcnic_adapter_s *adapter,
    qlcnic_ethernet_macaddr_t addr)
{
	qlcnic_crbword_t temp = 0;
	int phy = adapter->physical_port;

	if ((phy < 0) || (phy > 3))
		return (-1);

	(void) memcpy(&temp, addr, 2);
	temp <<= 16;
	adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_GB_STATION_ADDR_1(phy),
	    &temp, 4);
	temp = 0;
	(void) memcpy(&temp, ((uint8_t *)addr)+2, sizeof (qlcnic_crbword_t));
	adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_GB_STATION_ADDR_0(phy),
	    &temp, 4);
	return (0);
}

/* Enable a GbE interface */
/* ARGSUSED */
native_t qlcnic_niu_enable_gbe_port(struct qlcnic_adapter_s *adapter,
    qlcnic_niu_gbe_ifmode_t mode_dont_care)
{
	qlcnic_niu_gb_mac_config_0_t mac_cfg0;
	qlcnic_niu_gb_mac_config_1_t mac_cfg1;
	qlcnic_niu_gb_mii_mgmt_config_t mii_cfg;
	native_t port = adapter->physical_port;
	int zero = 0;
	int one = 1;
	u32 port_mode = 0;

	mode_dont_care = 0;

	if ((port < 0) || (port > QLCNIC_NIU_MAX_GBE_PORTS)) {
		return (-1);
	}

	if (adapter->link_speed != MBPS_10 &&
	    adapter->link_speed != MBPS_100 &&
	    adapter->link_speed != MBPS_1000) {

		if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
/*
 * Do NOT fail this call because the cable is unplugged.
 * Updated when the link comes up...
 */
		adapter->link_speed = MBPS_1000;
		} else {
			return (-1);
		}
	}

	port_mode = adapter->qlcnic_pci_read_normalize(adapter,
	    QLCNIC_PORT_MODE_ADDR);
	if (port_mode == QLCNIC_PORT_MODE_802_3_AP) {
		*(qlcnic_crbword_t *)&mac_cfg0 = 0x0000003f;
		*(qlcnic_crbword_t *)&mac_cfg1 = 0x0000f2df;
		qlcnic_crb_write_adapter(QLCNIC_NIU_AP_MAC_CONFIG_0(port),
		    &mac_cfg0, adapter);
		qlcnic_crb_write_adapter(QLCNIC_NIU_AP_MAC_CONFIG_1(port),
		    &mac_cfg1, adapter);
	} else {
		*(qlcnic_crbword_t *)&mac_cfg0 = 0;
		mac_cfg0.soft_reset = 1;
		qlcnic_crb_write_adapter(QLCNIC_NIU_GB_MAC_CONFIG_0(port),
		    &mac_cfg0, adapter);

		*(qlcnic_crbword_t *)&mac_cfg0 = 0;
		mac_cfg0.tx_enable = 1;
		mac_cfg0.rx_enable = 1;
		mac_cfg0.rx_flowctl = 0;
		mac_cfg0.tx_reset_pb = 1;
		mac_cfg0.rx_reset_pb = 1;
		mac_cfg0.tx_reset_mac = 1;
		mac_cfg0.rx_reset_mac = 1;

		qlcnic_crb_write_adapter(QLCNIC_NIU_GB_MAC_CONFIG_0(port),
		    &mac_cfg0, adapter);

		*(qlcnic_crbword_t *)&mac_cfg1 = 0;
		mac_cfg1.preamblelen = 0xf;
		mac_cfg1.duplex = 1;
		mac_cfg1.crc_enable = 1;
		mac_cfg1.padshort = 1;
		mac_cfg1.checklength = 1;
		mac_cfg1.hugeframes = 1;

		switch (adapter->link_speed) {
			case MBPS_10:
			case MBPS_100: /* Fall Through */
				mac_cfg1.intfmode = 1;
				qlcnic_crb_write_adapter(
				    QLCNIC_NIU_GB_MAC_CONFIG_1(port),
				    &mac_cfg1, adapter);

				/* set mii mode */
				qlcnic_crb_write_adapter(
				    QLCNIC_NIU_GB0_GMII_MODE+(port<<3),
				    &zero, adapter);
				qlcnic_crb_write_adapter(
				    QLCNIC_NIU_GB0_MII_MODE+(port<< 3),
				    &one, adapter);
				break;

			case MBPS_1000:
				mac_cfg1.intfmode = 2;
				qlcnic_crb_write_adapter(
				    QLCNIC_NIU_GB_MAC_CONFIG_1(port),
				    &mac_cfg1, adapter);

				/* set gmii mode */
				qlcnic_crb_write_adapter(
				    QLCNIC_NIU_GB0_MII_MODE+(port << 3),
				    &zero, adapter);
				qlcnic_crb_write_adapter(
				    QLCNIC_NIU_GB0_GMII_MODE+(port << 3),
				    &one, adapter);
				break;

			default:
				/* Will not happen */
				break;
		}

		*(qlcnic_crbword_t *)&mii_cfg = 0;
		mii_cfg.clockselect = 7;
		qlcnic_crb_write_adapter(QLCNIC_NIU_GB_MII_MGMT_CONFIG(port),
		    &mii_cfg, adapter);

		*(qlcnic_crbword_t *)&mac_cfg0 = 0;
		mac_cfg0.tx_enable = 1;
		mac_cfg0.rx_enable = 1;
		mac_cfg0.tx_flowctl = 0;
		mac_cfg0.rx_flowctl = 0;
		qlcnic_crb_write_adapter(QLCNIC_NIU_GB_MAC_CONFIG_0(port),
		    &mac_cfg0, adapter);
	}

	return (0);
}

/* Disable a GbE interface */
native_t
qlcnic_niu_disable_gbe_port(struct qlcnic_adapter_s *adapter)
{
	native_t port = adapter->physical_port;
	qlcnic_niu_gb_mac_config_0_t mac_cfg0;

	if ((port < 0) || (port > QLCNIC_NIU_MAX_GBE_PORTS))
		return (-1);

	*(qlcnic_crbword_t *)&mac_cfg0 = 0;
	mac_cfg0.soft_reset = 1;

	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id))
		adapter->qlcnic_hw_write_wx(adapter,
		    QLCNIC_NIU_GB_MAC_CONFIG_0(port), &mac_cfg0, 0);
	else
		adapter->qlcnic_hw_write_wx(adapter,
		    QLCNIC_NIU_GB_MAC_CONFIG_0(port), &mac_cfg0, 4);
	return (0);
}

/* Disable an XG interface */
native_t
qlcnic_niu_disable_xg_port(struct qlcnic_adapter_s *adapter)
{
	native_t port = adapter->physical_port;
	qlcnic_niu_xg_mac_config_0_t mac_cfg;

	*(qlcnic_crbword_t *)&mac_cfg = 0;
	mac_cfg.soft_reset = 1;

	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
		if (port != 0)
			return (-1);
		adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_XGE_CONFIG_0,
		    &mac_cfg, 4);
	} else {
		if ((port < 0) || (port >= QLCNIC_NIU_MAX_XG_PORTS))
			return (-1);
		adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_XGE_CONFIG_0 +
		    (port * 0x10000), &mac_cfg, 4);
	}
	return (0);
}


/* Set promiscuous mode for a GbE interface */
native_t
qlcnic_niu_set_promiscuous_mode(struct qlcnic_adapter_s *adapter,
    qlcnic_niu_prom_mode_t mode)
{
	native_t port = adapter->physical_port;
	qlcnic_niu_gb_drop_crc_t reg;
	qlcnic_niu_gb_mac_config_0_t mac_cfg;
	qlcnic_crbword_t data;
	int cnt = 0, ret = 0;
	u32 val;

	if ((port < 0) || (port > QLCNIC_NIU_MAX_GBE_PORTS))
		return (-1);

	/* Turn off mac */
	adapter->qlcnic_hw_read_wx(adapter, QLCNIC_NIU_GB_MAC_CONFIG_0(port),
	    &mac_cfg, 4);
	mac_cfg.rx_enable = 0;
	adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_GB_MAC_CONFIG_0(port),
	    &mac_cfg, 4);

	/* wait until mac is drained by sre */
	/* Port 0 rx fifo bit 5 */
	val = (0x20 << port);
	adapter->qlcnic_crb_writelit_adapter(adapter,
	    QLCNIC_NIU_FRAME_COUNT_SELECT, val);

	do {
		adapter->qlcnic_hw_read_wx(adapter, QLCNIC_NIU_FRAME_COUNT,
		    &val, 4);
		cnt++;
		if (cnt > 2000) {
			ret = -1;
			break;
		}
		drv_usecwait(10);
	} while (val);

	/* now set promiscuous mode */
	if (ret != -1) {
		if (mode == QLCNIC_NIU_PROMISCOUS_MODE)
			data = 0;
		else
			data = 1;

		adapter->qlcnic_hw_read_wx(adapter,
		    QLCNIC_NIU_GB_DROP_WRONGADDR, &reg, 4);
		switch (port) {
		case 0:
			reg.drop_gb0 = data;
			break;
		case 1:
			reg.drop_gb1 = data;
			break;
		case 2:
			reg.drop_gb2 = data;
			break;
		case 3:
			reg.drop_gb3 = data;
			break;
		default:
			ret  = -1;
			break;
		}
		adapter->qlcnic_hw_write_wx(adapter,
		    QLCNIC_NIU_GB_DROP_WRONGADDR,
		    &reg, 4);
	}

	/* turn the mac on back */
	mac_cfg.rx_enable = 1;
	adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_GB_MAC_CONFIG_0(port),
	    &mac_cfg, 4);

	return (ret);
}

/*
 * Set the MAC address for an XG port
 * Note that the passed-in value must already be in network byte order.
 */
int
qlcnic_niu_xg_macaddr_set(struct qlcnic_adapter_s *adapter,
    qlcnic_ethernet_macaddr_t addr)
{
	int phy = adapter->physical_port;
	qlcnic_crbword_t temp = 0;
	u32 port_mode = 0;

	if ((phy < 0) || (phy > 3))
		return (-1);

	switch (phy) {
	case 0:
		(void) memcpy(&temp, addr, 2);
		temp <<= 16;
		port_mode = adapter->qlcnic_pci_read_normalize(adapter,
		    QLCNIC_PORT_MODE_ADDR);
		if (port_mode == QLCNIC_PORT_MODE_802_3_AP) {
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_NIU_AP_STATION_ADDR_1(phy), &temp, 4);
			temp = 0;
			(void) memcpy(&temp, ((uint8_t *)addr) + 2,
			    sizeof (qlcnic_crbword_t));
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_NIU_AP_STATION_ADDR_0(phy), &temp, 4);
		} else {
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_NIU_XGE_STATION_ADDR_0_1, &temp, 4);
			temp = 0;
			(void) memcpy(&temp, ((uint8_t *)addr) + 2,
			    sizeof (qlcnic_crbword_t));
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_NIU_XGE_STATION_ADDR_0_HI, &temp, 4);
		}
		break;

	case 1:
		(void) memcpy(&temp, addr, 2);
		temp <<= 16;
		port_mode = adapter->qlcnic_pci_read_normalize(adapter,
		    QLCNIC_PORT_MODE_ADDR);
		if (port_mode == QLCNIC_PORT_MODE_802_3_AP) {
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_NIU_AP_STATION_ADDR_1(phy), &temp, 4);
			temp = 0;
			(void) memcpy(&temp, ((uint8_t *)addr) + 2,
			    sizeof (qlcnic_crbword_t));
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_NIU_AP_STATION_ADDR_0(phy), &temp, 4);
		} else {
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_NIU_XGE_STATION_ADDR_0_1, &temp, 4);
			temp = 0;
			(void) memcpy(&temp, ((uint8_t *)addr) + 2,
			    sizeof (qlcnic_crbword_t));
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_NIU_XGE_STATION_ADDR_0_HI, &temp, 4);
		}
		break;

	default:
		cmn_err(CE_WARN, "Unknown port %d", phy);
		return (DDI_FAILURE);
	}

	return (0);
}

native_t
qlcnic_niu_xg_set_promiscuous_mode(struct qlcnic_adapter_s *adapter,
    qlcnic_niu_prom_mode_t mode)
{
	u32 reg;
	qlcnic_niu_xg_mac_config_0_t mac_cfg;
	native_t port = adapter->physical_port;
	int cnt = 0;
	int result = 0;
	u32 port_mode = 0;

	if ((port < 0) || (port > QLCNIC_NIU_MAX_XG_PORTS))
		return (-1);

	port_mode = adapter->qlcnic_pci_read_normalize(adapter,
	    QLCNIC_PORT_MODE_ADDR);

	if (port_mode == QLCNIC_PORT_MODE_802_3_AP) {
		reg = 0;
		adapter->qlcnic_hw_write_wx(adapter,
		    QLCNIC_NIU_GB_DROP_WRONGADDR, (void*)&reg, 4);
	} else {
		/* Turn off mac */
		adapter->qlcnic_hw_read_wx(adapter, QLCNIC_NIU_XGE_CONFIG_0 +
		    (0x10000 * port), &mac_cfg, 4);
		mac_cfg.rx_enable = 0;
		adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_XGE_CONFIG_0 +
		    (0x10000 * port), &mac_cfg, 4);

		/* single port case bit 9 */
		reg = 0x0200;
		adapter->qlcnic_crb_writelit_adapter(adapter,
		    QLCNIC_NIU_FRAME_COUNT_SELECT, reg);

		do {
			adapter->qlcnic_hw_read_wx(adapter,
			    QLCNIC_NIU_FRAME_COUNT, &reg, 4);
			cnt++;
			if (cnt > 2000) {
				result = -1;
				break;
			}
			drv_usecwait(10);
		} while (reg);

		/* now set promiscuous mode */
		if (result != -1) {
			adapter->qlcnic_hw_read_wx(adapter,
			    QLCNIC_NIU_XGE_CONFIG_1 + (0x10000 * port),
			    &reg, 4);
			if (mode == QLCNIC_NIU_PROMISCOUS_MODE) {
				reg = (reg | 0x2000UL);
			} else { /* FIXME  use the correct mode value here */
				reg = (reg & ~0x2000UL);
			}
			adapter->qlcnic_crb_writelit_adapter(adapter,
			    QLCNIC_NIU_XGE_CONFIG_1 + (0x10000 * port), reg);
		}

		/* turn the mac back on */
		mac_cfg.rx_enable = 1;
		adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_XGE_CONFIG_0 +
		    (0x10000 * port), &mac_cfg, 4);
	}

	return (result);
}

int
qlcnic_niu_xg_set_tx_flow_ctl(struct qlcnic_adapter_s *adapter, int enable)
{
	int port = adapter->physical_port;
	qlcnic_niu_xg_pause_ctl_t reg;

	if ((port < 0) || (port > QLCNIC_NIU_MAX_XG_PORTS))
		return (-1);

	adapter->qlcnic_hw_read_wx(adapter, QLCNIC_NIU_XG_PAUSE_CTL, &reg, 4);
	if (port == 0)
		reg.xg0_mask = !enable;
	else
		reg.xg1_mask = !enable;

	adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_XG_PAUSE_CTL, &reg, 4);

	return (0);
}

int
qlcnic_niu_gbe_set_tx_flow_ctl(struct qlcnic_adapter_s *adapter, int enable)
{
	int port = adapter->physical_port;
	qlcnic_niu_gb_pause_ctl_t reg;

	if ((port < 0) || (port > QLCNIC_NIU_MAX_GBE_PORTS))
		return (-1);

	adapter->qlcnic_hw_read_wx(adapter, QLCNIC_NIU_GB_PAUSE_CTL, &reg, 4);
	switch (port) {
	case (0):
		reg.gb0_mask = !enable;
		break;
	case (1):
		reg.gb1_mask = !enable;
		break;
	case (2):
		reg.gb2_mask = !enable;
		break;
	case (3):
	default:
		reg.gb3_mask = !enable;
		break;
	}
	adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_GB_PAUSE_CTL, &reg, 4);

	return (0);
}

int
qlcnic_niu_gbe_set_rx_flow_ctl(struct qlcnic_adapter_s *adapter, int enable)
{
	int port = adapter->physical_port;
	qlcnic_niu_gb_mac_config_0_t reg;

	if ((port < 0) || (port > QLCNIC_NIU_MAX_GBE_PORTS))
		return (-1);

	adapter->qlcnic_hw_read_wx(adapter, QLCNIC_NIU_GB_MAC_CONFIG_0(port),
	    &reg, 4);
	reg.rx_flowctl = enable;
	adapter->qlcnic_hw_write_wx(adapter, QLCNIC_NIU_GB_MAC_CONFIG_0(port),
	    &reg, 4);

	return (0);
}

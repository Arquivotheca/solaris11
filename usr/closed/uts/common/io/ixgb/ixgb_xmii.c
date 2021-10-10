/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "ixgb.h"

/*
 * MSA (PHY) register get/set access routines
 *
 * These use the chip's MSA auto-access method, controlled by the
 * MSA Communication register at 0x000758, so the CPU doesn't have
 * to fiddle with the individual bits.
 */

#undef	IXGB_DBG
#define	IXGB_DBG	IXGB_DBG_MII	/* debug flag for this code	*/

/*
 * ixgb_mdio_read - Reads a word from a device over the
 * Management Data Interface (MDI) bus.
 *
 * ixgbp    - pointer to h/w structure.
 * phy - Address of device on MDI.
 * regno - Offset of device register being read.
 * dev - Also known as the Device ID or DID. (PMA/PMD for this device)
 * value    -  Data word (16 bits) from MDI device.
 * opcmd - read/write command
 * The 82597EX has support for several MDI access methods.  This routine
 * uses the new protocol MDI Single Command and Address Operation.
 * This requires that first an address cycle command is sent, followed by a
 * read/write command.
 */

static uint16_t
ixgb_mii_access(ixgb_t *ixgbp, ixgb_regno_t phy, ixgb_regno_t dev,
    ixgb_regno_t regno,	uint16_t data, uint32_t opcmd);
#pragma	no_inline(ixgb_mii_access)

static uint16_t
ixgb_mii_access(ixgb_t *ixgbp, ixgb_regno_t phy, ixgb_regno_t dev,
    ixgb_regno_t regno,	uint16_t data, uint32_t opcmd)
{
	boolean_t rdata;
	uint32_t timeout;
	uint32_t regval1;
	uint32_t regval2;
	uint32_t addrcmd;

	IXGB_TRACE(("ixgb_mii_access($%p, 0x%lx, 0x%x, 0x%x)",
	    (void *)ixgbp, regno, data, opcmd));

	ASSERT(mutex_owned(ixgbp->genlock));

	/*
	 * Assemble the command ...
	 */
	switch (opcmd) {
	/* this should not happen */
	default:
		ixgb_log(ixgbp, "invalid cmd (%x) ", opcmd);
		return (0xffff);

	case IXGB_MSCA_READ:
		rdata = B_TRUE;
		regval2 = ixgb_reg_get32(ixgbp, IXGB_MSRWD);
		break;

	case IXGB_MSCA_WRITE:
		rdata = B_FALSE;
		data &= IXGB_MSRWD_WDATA_MASK;
		ixgb_reg_put32(ixgbp, IXGB_MSRWD, data);
		break;
	}

	/* Setup and write the address cycle command */
	addrcmd = regno << IXGB_MSCA_REG_ADDR_SHIFT;
	addrcmd |= dev << IXGB_MSCA_DEV_TYPE_SHIFT;
	addrcmd |= phy << IXGB_MSCA_PHY_ADDR_SHIFT;
	addrcmd |= IXGB_MSCA_ADDR_CYCLE;
	addrcmd |= IXGB_MSCA_MDI_COMMAND;

	/* Address cycle complete, setup and write the write command */
	opcmd |= regno << IXGB_MSCA_REG_ADDR_SHIFT;
	opcmd |= dev << IXGB_MSCA_DEV_TYPE_SHIFT;
	opcmd |= phy << IXGB_MSCA_PHY_ADDR_SHIFT;
	opcmd |= IXGB_MSCA_MDI_COMMAND;

	ixgb_reg_put32(ixgbp, IXGB_MSCA, addrcmd);

	/*
	 * Check every 10 usec to see if the address cycle completed
	 * The mdi_cmd bit is self-clearing.
	 * This may take as long as 64 usecs (we'll wait 100 usecs max)
	 * from the CPU Write to the Ready bit assertion.
	 */
	for (timeout = 10; ; ) {
		regval2 = ixgb_reg_get32(ixgbp, IXGB_MSCA);
		if ((regval2 & IXGB_MSCA_MDI_COMMAND) == 0) {
			ixgb_reg_put32(ixgbp, IXGB_MSCA, opcmd);
			break;
		}
		if (--timeout == 0)
			break;
		drv_usecwait(10);
	}

	/*
	 * If the timer is out, it indicates
	 * the operation fails
	 */
	if (timeout == 0) {
		ixgb_log(ixgbp, "MSA addr cycle fails");
		return ((uint16_t)~0);
	}
	regval1 = ixgb_reg_get32(ixgbp, IXGB_MSCA);
	for (timeout = 10; ; ) {
		if ((regval1 & IXGB_MSCA_MDI_COMMAND) == 0)
			break;
		if (--timeout == 0)
			break;
		drv_usecwait(10);
		regval1 = ixgb_reg_get32(ixgbp, IXGB_MSCA);
	}
	if (timeout == 0) {
		ixgb_log(ixgbp, "MSA data cycle fails");
		return ((uint16_t)~0);
	}
	drv_usecwait(5);

	/*
	 * Operation is complete, get the data from the MDIO Read Data
	 * register and return.
	 */
	if (rdata == B_TRUE) {
		regval2 = ixgb_reg_get32(ixgbp, IXGB_MSRWD);
		return ((regval2 &
		    IXGB_MSRWD_RDATA_MASK) >> IXGB_MSRWD_RDATA_SHIFT);
	}
	return ((uint16_t)~0);
}

/*
 * Reads a word from a device over the
 * Management Data Interface (MDI) bus
 */
uint16_t ixgb_mii_get16(ixgb_t *ixgbp, ixgb_regno_t phy,
    ixgb_regno_t dev, ixgb_regno_t regno);
#pragma	no_inline(ixgb_mii_get16)

uint16_t ixgb_mii_get16(ixgb_t *ixgbp, ixgb_regno_t phy,
    ixgb_regno_t dev, ixgb_regno_t regno)
{
	IXGB_TRACE(("ixgb_mii_get16($%p, 0x%lx)", (void *)ixgbp, regno));

	ASSERT(mutex_owned(ixgbp->genlock));

	return (ixgb_mii_access(ixgbp, phy, dev, regno, 0, IXGB_MSCA_READ));
}

/*
 * Put a word from a device over the
 * Management Data Interface (MDI) bus
 */

void ixgb_mii_put16(ixgb_t *ixgbp, ixgb_regno_t phy,
    ixgb_regno_t dev, ixgb_regno_t regno, uint16_t data);
#pragma	no_inline(ixgb_mii_put16)

void ixgb_mii_put16(ixgb_t *ixgbp, ixgb_regno_t phy,
    ixgb_regno_t dev, ixgb_regno_t regno, uint16_t data)
{
	IXGB_TRACE(("ixgb_mii_put16($%p, 0x%lx, 0x%x)", (void *)ixgbp,
	    regno, data));

	ASSERT(mutex_owned(ixgbp->genlock));

	(void) ixgb_mii_access(ixgbp, phy, dev, regno, data, IXGB_MSCA_WRITE);
}

/*
 * per intel's 10G adapter, reset the phy of intel's adapter
 * please refer to PDM, chapter 9
 */
int
ixgb_serdes_restart_intel(ixgb_t *ixgbp)
{
	uint16_t data;
	uint16_t tries;
	uint32_t reg_val;

	ixgb_mii_put16(ixgbp, IXGB_PHY_ADDRESS_INTEL, MDIO_PMA,
	    MDIO_PMA_CR1, MDIO_PMA_CR1_RESET);

	/*
	 * This chip is flaky and needs to be reset
	 * several times to achieve link up.
	 * We attempt this up to 20 times.
	 */
	for (tries = 0; tries < 20; tries++) {
		data = ixgb_mii_get16(ixgbp, IXGB_PHY_ADDRESS_INTEL,
		    MDIO_PMA, MDIO_PMA_CR1);
		if (!(data & MDIO_PMA_CR1_RESET))
			break;
		drv_usecwait(10);
	}

	switch (ixgbp->param_loop_mode) {

	default:
	case IXGB_LOOP_NONE:
		break;

	case IXGB_LOOP_EXTERNAL_XAUI:
		reg_val = MDIO_PMA_CR1_LOOP;
		ixgb_mii_put16(ixgbp, IXGB_PHY_ADDRESS_INTEL,
		    MDIO_PMA, MDIO_PMA_CR1, reg_val);
		break;

	case IXGB_LOOP_EXTERNAL_XGMII:
		break;

	case IXGB_LOOP_INTERNAL_XGMII:
		reg_val = IXGB_CTRL0_XLE;
		ixgb_reg_set32(ixgbp, IXGB_CTRL0, reg_val);
		break;
	}

	return (0);
}

/*
 * Per Sun's10G adapter, reset the phy of Sun's adapter
 * please refer to kealia spec and lxt12101 spec
 */

int
ixgb_serdes_restart_sun(ixgb_t *ixgbp)
{
	uint32_t reg_val;

	switch (ixgbp->chipinfo.subdev) {
	case IXGB_SUB_DEVID_A11F:
		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_GPIO2, 0xe000);
		ixgb_mii_put16(ixgbp, IXGB_PHY1_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_GPIO2, 0xe000);

		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_CFG0, 0x0020);
		ixgb_mii_put16(ixgbp, IXGB_PHY1_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_CFG0, 0x0020);

		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_CFG1, 0x081e);
		ixgb_mii_put16(ixgbp, IXGB_PHY1_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_CFG1, 0x0091e);

		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_CFG4, 0x4002);
		ixgb_mii_put16(ixgbp, IXGB_PHY1_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_CFG4, 0x4002);

		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_CFG5, 0x002c);
		ixgb_mii_put16(ixgbp, IXGB_PHY1_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_CFG5, 0x002c);

		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, 0x900a, 0xf400);
		ixgb_mii_put16(ixgbp, IXGB_PHY1_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, 0x900a, 0xf400);

		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, 0x900d, 0x7500);
		ixgb_mii_put16(ixgbp, IXGB_PHY1_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, 0x900d, 0x7500);

		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, 0x900e, 0x0009);
		ixgb_mii_put16(ixgbp, IXGB_PHY1_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, 0x900e, 0x0009);

		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_LANECFG0, 0x0088);
		ixgb_mii_put16(ixgbp, IXGB_PHY1_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_LANECFG0, 0x0088);
		break;

	case IXGB_SUB_DEVID_7036:
		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_BCM, IXGB_BCM_TXCR, 0x164);
		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_BCM, IXGB_BCM_CTRL, 0x7fbf);
		(void) ixgb_mii_get16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_BCM, IXGB_BCM_TXCR);
		(void) ixgb_mii_get16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_BCM, IXGB_BCM_TXCR);
		break;

	default:
		ixgb_problem(ixgbp, "Unknown SerDes");
		break;
	}

	drv_usecwait(IXGB_SERDES_RESET_DELAY);

	switch (ixgbp->param_loop_mode) {

	default:
	case IXGB_LOOP_NONE:
		break;

	case IXGB_LOOP_EXTERNAL_XAUI:
		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_CFG2, 0x0300);
		drv_usecwait(100);

		ixgb_mii_put16(ixgbp, IXGB_PHY0_ADDRESS_SUN,
		    MDIO_VENDOR_LXT, IXGB_LXT_CFG0, 0x002c);
		drv_usecwait(100);
		break;

	case IXGB_LOOP_EXTERNAL_XGMII:
		break;

	case IXGB_LOOP_INTERNAL_XGMII:
		reg_val = IXGB_CTRL0_XLE;
		ixgb_reg_set32(ixgbp, IXGB_CTRL0, reg_val);
		break;
	}
	return (IXGB_SUCCESS);
}

/*
 * Read the link status and determine whether anything's changed ...
 */

int
ixgb_serdes_check(ixgb_t *ixgbp)
{
	boolean_t link_up;
	boolean_t link_align;
	boolean_t lnchg;
	uint32_t reg;
	uint32_t link_state;

	lnchg = B_FALSE;
	reg = ixgb_reg_get32(ixgbp, IXGB_XPCSS);
	link_align = BIS(reg, IXGB_XPCSS_ALIGN_STATUS);
	reg = ixgb_reg_get32(ixgbp, IXGB_STATUS);
	link_up = BIS(reg, IXGB_STATUS_LU);
	IXGB_DEBUG(("link_align = 0x%x, link_up = 0x%x, link_state = 0x%x",
	    link_align, link_up, ixgbp->link_state));

	if ((link_align) && (link_up)) {
		link_state = LINK_STATE_UP;
	} else {
		if (ixgb_chip_reset_link(ixgbp))
			link_state = LINK_STATE_UP;
		else
			link_state = LINK_STATE_DOWN;
	}

	if (ixgbp->link_state != link_state) {
		ixgbp->link_state = link_state;
		lnchg = B_TRUE;
	}

	if (lnchg) {
		ixgbp->watchdog = 0;
	}

	return (lnchg);
}

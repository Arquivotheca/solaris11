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
 * Copyright(c) 2007-2011 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* IntelVersion: 1.17 scm_011511_003853 */

#include "ixgbe_type.h"
#include "ixgbe_api.h"
#include "ixgbe_common.h"
#include "ixgbe_phy.h"

s32 ixgbe_init_ops_X540(struct ixgbe_hw *hw);
s32 ixgbe_get_link_capabilities_X540(struct ixgbe_hw *hw,
    ixgbe_link_speed *speed,  bool *autoneg);
enum ixgbe_media_type ixgbe_get_media_type_X540(struct ixgbe_hw *hw);
s32 ixgbe_setup_mac_link_X540(struct ixgbe_hw *hw,
    ixgbe_link_speed speed,  bool autoneg, bool link_up_wait_to_complete);
s32 ixgbe_reset_hw_X540(struct ixgbe_hw *hw);
s32 ixgbe_start_hw_X540(struct ixgbe_hw *hw);
u32 ixgbe_get_supported_physical_layer_X540(struct ixgbe_hw *hw);

s32 ixgbe_init_eeprom_params_X540(struct ixgbe_hw *hw);
s32 ixgbe_read_eerd_X540(struct ixgbe_hw *hw, u16 offset, u16 *data);
s32 ixgbe_write_eewr_X540(struct ixgbe_hw *hw, u16 offset, u16 data);
s32 ixgbe_update_eeprom_checksum_X540(struct ixgbe_hw *hw);
s32 ixgbe_validate_eeprom_checksum_X540(struct ixgbe_hw *hw, u16 *checksum_val);
u16 ixgbe_calc_eeprom_checksum_X540(struct ixgbe_hw *hw);

s32 ixgbe_acquire_swfw_sync_X540(struct ixgbe_hw *hw, u16 mask);
void ixgbe_release_swfw_sync_X540(struct ixgbe_hw *hw, u16 mask);

static s32 ixgbe_update_flash_X540(struct ixgbe_hw *hw);
static s32 ixgbe_poll_flash_update_done_X540(struct ixgbe_hw *hw);
static s32 ixgbe_get_swfw_sync_semaphore(struct ixgbe_hw *hw);
static void ixgbe_release_swfw_sync_semaphore(struct ixgbe_hw *hw);

/*
 * ixgbe_init_ops_X540 - Inits func ptrs and MAC type
 * @hw: pointer to hardware structure
 *
 * Initialize the function pointers and assign the MAC type for 82599.
 * Does not touch the hardware.
 */
s32
ixgbe_init_ops_X540(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_phy_info *phy = &hw->phy;
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	s32 ret_val;

	DEBUGFUNC("ixgbe_init_ops_X540");

	ret_val = ixgbe_init_phy_ops_generic(hw);
	ret_val = ixgbe_init_ops_generic(hw);


	/* EEPROM */
	eeprom->ops.init_params = &ixgbe_init_eeprom_params_X540;
	eeprom->ops.read = &ixgbe_read_eerd_X540;
	eeprom->ops.write = &ixgbe_write_eewr_X540;
	eeprom->ops.update_checksum = &ixgbe_update_eeprom_checksum_X540;
	eeprom->ops.validate_checksum = &ixgbe_validate_eeprom_checksum_X540;
	eeprom->ops.calc_checksum = &ixgbe_calc_eeprom_checksum_X540;

	/* PHY */
	phy->ops.init = &ixgbe_init_phy_ops_generic;

	/* MAC */
	mac->ops.reset_hw = &ixgbe_reset_hw_X540;
	mac->ops.enable_relaxed_ordering = &ixgbe_enable_relaxed_ordering_gen2;
	mac->ops.get_media_type = &ixgbe_get_media_type_X540;
	mac->ops.get_supported_physical_layer =
	    &ixgbe_get_supported_physical_layer_X540;
	mac->ops.read_analog_reg8 = NULL;
	mac->ops.write_analog_reg8 = NULL;
	mac->ops.start_hw = &ixgbe_start_hw_X540;
	mac->ops.get_san_mac_addr = &ixgbe_get_san_mac_addr_generic;
	mac->ops.set_san_mac_addr = &ixgbe_set_san_mac_addr_generic;
	mac->ops.get_device_caps = &ixgbe_get_device_caps_generic;
	mac->ops.get_wwn_prefix = &ixgbe_get_wwn_prefix_generic;
	mac->ops.get_fcoe_boot_status = &ixgbe_get_fcoe_boot_status_generic;

	mac->ops.acquire_swfw_sync = &ixgbe_acquire_swfw_sync_X540;
	mac->ops.release_swfw_sync = &ixgbe_release_swfw_sync_X540;

	/* RAR, Multicast, VLAN */
	mac->ops.set_vmdq = &ixgbe_set_vmdq_generic;
	mac->ops.clear_vmdq = &ixgbe_clear_vmdq_generic;
	mac->ops.insert_mac_addr = &ixgbe_insert_mac_addr_generic;
	mac->rar_highwater = 1;
	mac->ops.set_vfta = &ixgbe_set_vfta_generic;
	mac->ops.clear_vfta = &ixgbe_clear_vfta_generic;
	mac->ops.init_uta_tables = &ixgbe_init_uta_tables_generic;
	mac->ops.set_mac_anti_spoofing = &ixgbe_set_mac_anti_spoofing;
	mac->ops.set_vlan_anti_spoofing = &ixgbe_set_vlan_anti_spoofing;

	/* Link */
	mac->ops.get_link_capabilities =
	    &ixgbe_get_copper_link_capabilities_generic;
	mac->ops.setup_link = &ixgbe_setup_mac_link_X540;
	mac->ops.check_link = &ixgbe_check_mac_link_generic;

	mac->mcft_size = 128;
	mac->vft_size = 128;
	mac->num_rar_entries = 128;
	mac->rx_pb_size = 384;
	mac->max_tx_queues = 128;
	mac->max_rx_queues = 128;
	mac->max_msix_vectors = ixgbe_get_pcie_msix_count_generic(hw);

	return (ret_val);
}

/*
 * ixgbe_get_link_capabilities_X540 - Determines link capabilities
 * @hw: pointer to hardware structure
 * @speed: pointer to link speed
 * @negotiation: true when autoneg or autotry is enabled
 *
 * Determines the link capabilities by reading the AUTOC register.
 */
s32
ixgbe_get_link_capabilities_X540(struct ixgbe_hw *hw,
    ixgbe_link_speed *speed, bool *negotiation)
{
	(void) ixgbe_get_copper_link_capabilities_generic(hw,
	    speed, negotiation);

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_get_media_type_X540 - Get media type
 * @hw: pointer to hardware structure
 *
 * Returns the media type (fiber, copper, backplane)
 */
enum ixgbe_media_type
ixgbe_get_media_type_X540(struct ixgbe_hw *hw)
{
	UNREFERENCED_PARAMETER(hw);
	return (ixgbe_media_type_copper);
}

/*
 * ixgbe_setup_mac_link_X540 - Sets the auto advertised capabilities
 * @hw: pointer to hardware structure
 * @speed: new link speed
 * @autoneg: true if autonegotiation enabled
 * @autoneg_wait_to_complete: true when waiting for completion is needed
 */
s32
ixgbe_setup_mac_link_X540(struct ixgbe_hw *hw,
    ixgbe_link_speed speed, bool autoneg, bool autoneg_wait_to_complete)
{
	DEBUGFUNC("ixgbe_setup_mac_link_X540");
	return hw->phy.ops.setup_link_speed(hw, speed, autoneg,
	    autoneg_wait_to_complete);
}

/*
 * ixgbe_reset_hw_X540 - Perform hardware reset
 * @hw: pointer to hardware structure
 *
 * Resets the hardware by resetting the transmit and receive units, masks
 * and clears all interrupts, and perform a reset.
 */
s32
ixgbe_reset_hw_X540(struct ixgbe_hw *hw)
{
	ixgbe_link_speed link_speed;
	s32 status = IXGBE_SUCCESS;
	u32 ctrl, ctrl_ext, reset_bit;
	u32 i;
	bool link_up = false;

	DEBUGFUNC("ixgbe_reset_hw_X540");

	/* Call adapter stop to disable tx/rx and clear interrupts */
	hw->mac.ops.stop_adapter(hw);

	/*
	 * Prevent the PCI-E bus from from hanging by disabling PCI-E master
	 * access and verify no pending requests before reset
	 */
	(void) ixgbe_disable_pcie_master(hw);

mac_reset_top:
	/*
	 * Issue global reset to the MAC. Needs to be SW reset if link is up.
	 * If link reset is used when link is up, it might reset the PHY when
	 * mng is using it.  If link is down or the flag to force full link
	 * reset is set, then perform link reset.
	 */
	if (hw->force_full_reset) {
		reset_bit = IXGBE_CTRL_LNK_RST;
	} else {
		hw->mac.ops.check_link(hw, &link_speed, &link_up, false);
		if (!link_up)
			reset_bit = IXGBE_CTRL_LNK_RST;
		else
			reset_bit = IXGBE_CTRL_RST;
	}

	ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
	IXGBE_WRITE_REG(hw, IXGBE_CTRL, (ctrl | reset_bit));
	IXGBE_WRITE_FLUSH(hw);

	/* Poll for reset bit to self-clear indicating reset is complete */
	for (i = 0; i < 10; i++) {
		usec_delay(1);
		ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
		if (!(ctrl & reset_bit))
			break;
	}


	if (ctrl & reset_bit) {
		status = IXGBE_ERR_RESET_FAILED;
		DEBUGOUT("Reset polling failed to complete.\n");
	}

	/*
	 * Double resets are required for recovery from certain error
	 * conditions.  Between resets, it is necessary to stall to allow time
	 * for any pending HW events to complete.  We use 1usec since that is
	 * what is needed for ixgbe_disable_pcie_master().  The second reset
	 * then clears out any effects of those events.
	 */
	if (hw->mac.flags & IXGBE_FLAGS_DOUBLE_RESET_REQUIRED) {
		hw->mac.flags &= ~IXGBE_FLAGS_DOUBLE_RESET_REQUIRED;
		usec_delay(1);
		goto mac_reset_top;
	}

	/* Clear PF Reset Done bit so PF/VF Mail Ops can work */
	ctrl_ext = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	ctrl_ext |= IXGBE_CTRL_EXT_PFRSTD;
	IXGBE_WRITE_REG(hw, IXGBE_CTRL_EXT, ctrl_ext);

	msec_delay(50);

	/* Set the Rx packet buffer size. */
	IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(0), 384 << IXGBE_RXPBSIZE_SHIFT);

	/* Store the permanent mac address */
	hw->mac.ops.get_mac_addr(hw, hw->mac.perm_addr);

	/*
	 * Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table.  Also reset num_rar_entries to 128,
	 * since we modify this value when programming the SAN MAC address.
	 */
	hw->mac.num_rar_entries = 128;
	hw->mac.ops.init_rx_addrs(hw);

	/* Store the permanent SAN mac address */
	hw->mac.ops.get_san_mac_addr(hw, hw->mac.san_addr);

	/* Add the SAN MAC address to the RAR only if it's a valid address */
	if (ixgbe_validate_mac_addr(hw->mac.san_addr) == 0) {
		hw->mac.ops.set_rar(hw, hw->mac.num_rar_entries - 1,
		    hw->mac.san_addr, 0, IXGBE_RAH_AV);

		/* Reserve the last RAR for the SAN MAC address */
		hw->mac.num_rar_entries--;
	}

	return (status);
}

/*
 * ixgbe_start_hw_X540 - Prepare hardware for Tx/Rx
 * @hw: pointer to hardware structure
 *
 * Starts the hardware using the generic start_hw function
 * and the generation start_hw function.
 * Then performs revision-specific operations, if any.
 */
s32
ixgbe_start_hw_X540(struct ixgbe_hw *hw)
{
	s32 ret_val = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_start_hw_X540");

	ret_val = ixgbe_start_hw_generic(hw);
	if (ret_val != IXGBE_SUCCESS)
		goto out;

	ret_val = ixgbe_start_hw_gen2(hw);

out:
	return (ret_val);
}

/*
 * ixgbe_get_supported_physical_layer_X540 - Returns physical layer type
 * @hw: pointer to hardware structure
 *
 * Determines physical layer capabilities of the current configuration.
 */
u32
ixgbe_get_supported_physical_layer_X540(struct ixgbe_hw *hw)
{
	u32 physical_layer = IXGBE_PHYSICAL_LAYER_UNKNOWN;
	u16 ext_ability = 0;

	DEBUGFUNC("ixgbe_get_supported_physical_layer_X540");

	hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_EXT_ABILITY,
	    IXGBE_MDIO_PMA_PMD_DEV_TYPE, &ext_ability);
	if (ext_ability & IXGBE_MDIO_PHY_10GBASET_ABILITY)
		physical_layer |= IXGBE_PHYSICAL_LAYER_10GBASE_T;
	if (ext_ability & IXGBE_MDIO_PHY_1000BASET_ABILITY)
		physical_layer |= IXGBE_PHYSICAL_LAYER_1000BASE_T;
	if (ext_ability & IXGBE_MDIO_PHY_100BASETX_ABILITY)
		physical_layer |= IXGBE_PHYSICAL_LAYER_100BASE_TX;

	return (physical_layer);
}

/*
 * ixgbe_init_eeprom_params_X540 - Initialize EEPROM params
 * @hw: pointer to hardware structure
 *
 * Initializes the EEPROM parameters ixgbe_eeprom_info within the
 * ixgbe_hw struct in order to set up EEPROM access.
 */
s32
ixgbe_init_eeprom_params_X540(struct ixgbe_hw *hw)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	u32 eec;
	u16 eeprom_size;

	DEBUGFUNC("ixgbe_init_eeprom_params_X540");

	if (eeprom->type == ixgbe_eeprom_uninitialized) {
		eeprom->semaphore_delay = 10;
		eeprom->type = ixgbe_flash;

		eec = IXGBE_READ_REG(hw, IXGBE_EEC);
		eeprom_size = (u16)((eec & IXGBE_EEC_SIZE) >>
		    IXGBE_EEC_SIZE_SHIFT);
		eeprom->word_size = 1 << (eeprom_size +
		    IXGBE_EEPROM_WORD_SIZE_BASE_SHIFT);

		DEBUGOUT2("Eeprom params: type = %d, size = %d\n",
		    eeprom->type, eeprom->word_size);
	}

	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_read_eerd_X540- Read EEPROM word using EERD
 * @hw: pointer to hardware structure
 * @offset: offset of  word in the EEPROM to read
 * @data: word read from the EEPROM
 *
 * Reads a 16 bit word from the EEPROM using the EERD register.
 */
s32
ixgbe_read_eerd_X540(struct ixgbe_hw *hw, u16 offset, u16 *data)
{
	s32 status = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_read_eerd_X540");
	if (ixgbe_acquire_swfw_sync_X540(hw,
	    IXGBE_GSSR_EEP_SM) == IXGBE_SUCCESS)
		status = ixgbe_read_eerd_generic(hw, offset, data);
	else
		status = IXGBE_ERR_SWFW_SYNC;

	ixgbe_release_swfw_sync_X540(hw, IXGBE_GSSR_EEP_SM);
	return (status);
}

/*
 * ixgbe_write_eewr_X540 - Write EEPROM word using EEWR
 * @hw: pointer to hardware structure
 * @offset: offset of  word in the EEPROM to write
 * @data: word write to the EEPROM
 *
 * Write a 16 bit word to the EEPROM using the EEWR register.
 */
s32
ixgbe_write_eewr_X540(struct ixgbe_hw *hw, u16 offset, u16 data)
{
	s32 status = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_write_eewr_X540");
	if (ixgbe_acquire_swfw_sync_X540(hw, IXGBE_GSSR_EEP_SM) ==
	    IXGBE_SUCCESS)
		status = ixgbe_write_eewr_generic(hw, offset, data);
	else
		status = IXGBE_ERR_SWFW_SYNC;

	ixgbe_release_swfw_sync_X540(hw, IXGBE_GSSR_EEP_SM);
	return (status);
}

/*
 * ixgbe_calc_eeprom_checksum_X540 - Calculates and returns the checksum
 *
 * This function does not use synchronization for EERD and EEWR. It can
 * be used internally by function which utilize ixgbe_acquire_swfw_sync_X540.
 *
 * @hw: pointer to hardware structure
 */
u16
ixgbe_calc_eeprom_checksum_X540(struct ixgbe_hw *hw)
{
	u16 i;
	u16 j;
	u16 checksum = 0;
	u16 length = 0;
	u16 pointer = 0;
	u16 word = 0;

	/*
	 * Do not use hw->eeprom.ops.read because we do not want to take
	 * the synchronization semaphores here. Instead use
	 * ixgbe_read_eerd_generic
	 */

	DEBUGFUNC("ixgbe_calc_eeprom_checksum_X540");

	/* Include 0x0-0x3F in the checksum */
	for (i = 0; i < IXGBE_EEPROM_CHECKSUM; i++) {
		if (ixgbe_read_eerd_generic(hw, i, &word) != IXGBE_SUCCESS) {
			DEBUGOUT("EEPROM read failed\n");
			break;
		}
		checksum += word;
	}

	/*
	 * Include all data from pointers 0x3, 0x6-0xE.  This excludes the
	 * FW, PHY module, and PCIe Expansion/Option ROM pointers.
	 */
	for (i = IXGBE_PCIE_ANALOG_PTR; i < IXGBE_FW_PTR; i++) {
		if (i == IXGBE_PHY_PTR || i == IXGBE_OPTION_ROM_PTR)
			continue;

		if (ixgbe_read_eerd_generic(hw, i, &pointer) != IXGBE_SUCCESS) {
			DEBUGOUT("EEPROM read failed\n");
			break;
		}

		/* Skip pointer section if the pointer is invalid. */
		if (pointer == 0xFFFF || pointer == 0 ||
		    pointer >= hw->eeprom.word_size)
			continue;

		if (ixgbe_read_eerd_generic(hw, pointer, &length) !=
		    IXGBE_SUCCESS) {
			DEBUGOUT("EEPROM read failed\n");
			break;
		}

		/* Skip pointer section if length is invalid. */
		if (length == 0xFFFF || length == 0 ||
		    (pointer + length) >= hw->eeprom.word_size)
			continue;

		for (j = pointer+1; j <= pointer+length; j++) {
			if (ixgbe_read_eerd_generic(hw, j, &word) !=
			    IXGBE_SUCCESS) {
				DEBUGOUT("EEPROM read failed\n");
				break;
			}
			checksum += word;
		}
	}

	checksum = (u16)IXGBE_EEPROM_SUM - checksum;

	return (checksum);
}

/*
 * ixgbe_validate_eeprom_checksum_X540 - Validate EEPROM checksum
 * @hw: pointer to hardware structure
 * @checksum_val: calculated checksum
 *
 * Performs checksum calculation and validates the EEPROM checksum.  If the
 * caller does not need checksum_val, the value can be NULL.
 */
s32
ixgbe_validate_eeprom_checksum_X540(struct ixgbe_hw *hw, u16 *checksum_val)
{
	s32 status;
	u16 checksum;
	u16 read_checksum = 0;

	DEBUGFUNC("ixgbe_validate_eeprom_checksum_X540");

	/*
	 * Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	status = hw->eeprom.ops.read(hw, 0, &checksum);

	if (status != IXGBE_SUCCESS)
		DEBUGOUT("EEPROM read failed\n");

	if (ixgbe_acquire_swfw_sync_X540(hw, IXGBE_GSSR_EEP_SM) ==
	    IXGBE_SUCCESS) {
		checksum = hw->eeprom.ops.calc_checksum(hw);

		/*
		 * Do not use hw->eeprom.ops.read because we do not want to take
		 * the synchronization semaphores twice here.
		 */
		(void) ixgbe_read_eerd_generic(hw, IXGBE_EEPROM_CHECKSUM,
		    &read_checksum);

		/*
		 * Verify read checksum from EEPROM is the same as
		 * calculated checksum
		 */
		if (read_checksum != checksum)
			status = IXGBE_ERR_EEPROM_CHECKSUM;

		/* If the user cares, return the calculated checksum */
		if (checksum_val)
			*checksum_val = checksum;
	} else {
		status = IXGBE_ERR_SWFW_SYNC;
	}

	ixgbe_release_swfw_sync_X540(hw, IXGBE_GSSR_EEP_SM);
	return (status);
}

/*
 * ixgbe_update_eeprom_checksum_X540 - Updates the EEPROM checksum and flash
 * @hw: pointer to hardware structure
 *
 * After writing EEPROM to shadow RAM using EEWR register, software calculates
 * checksum and updates the EEPROM and instructs the hardware to update
 * the flash.
 */
s32
ixgbe_update_eeprom_checksum_X540(struct ixgbe_hw *hw)
{
	s32 status;
	u16 checksum;

	DEBUGFUNC("ixgbe_update_eeprom_checksum_X540");

	/*
	 * Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	status = hw->eeprom.ops.read(hw, 0, &checksum);

	if (status != IXGBE_SUCCESS)
		DEBUGOUT("EEPROM read failed\n");

	if (ixgbe_acquire_swfw_sync_X540(hw, IXGBE_GSSR_EEP_SM) ==
	    IXGBE_SUCCESS) {
		checksum = hw->eeprom.ops.calc_checksum(hw);

		/*
		 * Do not use hw->eeprom.ops.write because we do not want to
		 * take the synchronization semaphores twice here.
		 */
		status = ixgbe_write_eewr_generic(hw, IXGBE_EEPROM_CHECKSUM,
		    checksum);

		if (status == IXGBE_SUCCESS)
			status = ixgbe_update_flash_X540(hw);
		else
			status = IXGBE_ERR_SWFW_SYNC;
	}

	ixgbe_release_swfw_sync_X540(hw, IXGBE_GSSR_EEP_SM);

	return (status);
}

/*
 * ixgbe_update_flash_X540 - Instruct HW to copy EEPROM to Flash device
 * @hw: pointer to hardware structure
 *
 * Set FLUP (bit 23) of the EEC register to instruct Hardware to copy
 * EEPROM from shadow RAM to the flash device.
 */
static s32
ixgbe_update_flash_X540(struct ixgbe_hw *hw)
{
	u32 flup;
	s32 status = IXGBE_ERR_EEPROM;

	DEBUGFUNC("ixgbe_update_flash_X540");

	status = ixgbe_poll_flash_update_done_X540(hw);
	if (status == IXGBE_ERR_EEPROM) {
		DEBUGOUT("Flash update time out\n");
		goto out;
	}

	flup = IXGBE_READ_REG(hw, IXGBE_EEC) | IXGBE_EEC_FLUP;
	IXGBE_WRITE_REG(hw, IXGBE_EEC, flup);

	status = ixgbe_poll_flash_update_done_X540(hw);
	if (status == IXGBE_SUCCESS)
		DEBUGOUT("Flash update complete\n");
	else
		DEBUGOUT("Flash update time out\n");

	if (hw->revision_id == 0) {
		flup = IXGBE_READ_REG(hw, IXGBE_EEC);

		if (flup & IXGBE_EEC_SEC1VAL) {
			flup |= IXGBE_EEC_FLUP;
			IXGBE_WRITE_REG(hw, IXGBE_EEC, flup);
		}

		status = ixgbe_poll_flash_update_done_X540(hw);
		if (status == IXGBE_SUCCESS)
			DEBUGOUT("Flash update complete\n");
		else
			DEBUGOUT("Flash update time out\n");
	}
out:
	return (status);
}

/*
 * ixgbe_poll_flash_update_done_X540 - Poll flash update status
 * @hw: pointer to hardware structure
 *
 * Polls the FLUDONE (bit 26) of the EEC Register to determine when the
 * flash update is done.
 */
static s32
ixgbe_poll_flash_update_done_X540(struct ixgbe_hw *hw)
{
	u32 i;
	u32 reg;
	s32 status = IXGBE_ERR_EEPROM;

	DEBUGFUNC("ixgbe_poll_flash_update_done_X540");

	for (i = 0; i < IXGBE_FLUDONE_ATTEMPTS; i++) {
		reg = IXGBE_READ_REG(hw, IXGBE_EEC);
		if (reg & IXGBE_EEC_FLUDONE) {
			status = IXGBE_SUCCESS;
			break;
		}
		usec_delay(5);
	}
	return (status);
}

/*
 * ixgbe_acquire_swfw_sync_X540 - Acquire SWFW semaphore
 * @hw: pointer to hardware structure
 * @mask: Mask to specify which semaphore to acquire
 *
 * Acquires the SWFW semaphore thought the SW_FW_SYNC register for
 * the specified function (CSR, PHY0, PHY1, NVM, Flash)
 */
s32
ixgbe_acquire_swfw_sync_X540(struct ixgbe_hw *hw, u16 mask)
{
	u32 swfw_sync;
	u32 swmask = mask;
	u32 fwmask = mask << 5;
	u32 hwmask = 0;
	u32 timeout = 200;
	u32 i;

	DEBUGFUNC("ixgbe_acquire_swfw_sync_X540");

	if (swmask == IXGBE_GSSR_EEP_SM)
		hwmask = IXGBE_GSSR_FLASH_SM;

	for (i = 0; i < timeout; i++) {
		/*
		 * SW NVM semaphore bit is used for access to all
		 * SW_FW_SYNC bits (not just NVM)
		 */
		if (ixgbe_get_swfw_sync_semaphore(hw))
			return (IXGBE_ERR_SWFW_SYNC);

		swfw_sync = IXGBE_READ_REG(hw, IXGBE_SWFW_SYNC);
		if (!(swfw_sync & (fwmask | swmask | hwmask))) {
			swfw_sync |= swmask;
			IXGBE_WRITE_REG(hw, IXGBE_SWFW_SYNC, swfw_sync);
			ixgbe_release_swfw_sync_semaphore(hw);
			break;
		} else {
			/*
			 * Firmware currently using resource (fwmask),
			 * hardware currently using resource (hwmask),
			 * or other software thread currently
			 * using resource (swmask)
			 */
			ixgbe_release_swfw_sync_semaphore(hw);
			msec_delay(5);
		}
	}

	/*
	 * If the resource is not released by the FW/HW the SW can assume that
	 * the FW/HW malfunctions. In that case the SW should sets the SW bit(s)
	 * of the requested resource(s) while ignoring the corresponding FW/HW
	 * bits in the SW_FW_SYNC register.
	 */
	if (i >= timeout) {
		swfw_sync = IXGBE_READ_REG(hw, IXGBE_SWFW_SYNC);
		if (swfw_sync & (fwmask| hwmask)) {
			if (ixgbe_get_swfw_sync_semaphore(hw))
				return (IXGBE_ERR_SWFW_SYNC);

			swfw_sync |= swmask;
			IXGBE_WRITE_REG(hw, IXGBE_SWFW_SYNC, swfw_sync);
			ixgbe_release_swfw_sync_semaphore(hw);
		}
	}

	msec_delay(5);
	return (IXGBE_SUCCESS);
}

/*
 * ixgbe_release_swfw_sync_X540 - Release SWFW semaphore
 * @hw: pointer to hardware structure
 * @mask: Mask to specify which semaphore to release
 *
 * Releases the SWFW semaphore throught the SW_FW_SYNC register
 * for the specified function (CSR, PHY0, PHY1, EVM, Flash)
 */
void
ixgbe_release_swfw_sync_X540(struct ixgbe_hw *hw, u16 mask)
{
	u32 swfw_sync;
	u32 swmask = mask;

	DEBUGFUNC("ixgbe_release_swfw_sync_X540");

	(void) ixgbe_get_swfw_sync_semaphore(hw);

	swfw_sync = IXGBE_READ_REG(hw, IXGBE_SWFW_SYNC);
	swfw_sync &= ~swmask;
	IXGBE_WRITE_REG(hw, IXGBE_SWFW_SYNC, swfw_sync);

	ixgbe_release_swfw_sync_semaphore(hw);
	msec_delay(5);
}

/*
 * ixgbe_get_nvm_semaphore - Get hardware semaphore
 * @hw: pointer to hardware structure
 *
 * Sets the hardware semaphores so SW/FW can gain control of shared resources
 */
static s32
ixgbe_get_swfw_sync_semaphore(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_ERR_EEPROM;
	u32 timeout = 2000;
	u32 i;
	u32 swsm;

	DEBUGFUNC("ixgbe_get_swfw_sync_semaphore");

	/* Get SMBI software semaphore between device drivers first */
	for (i = 0; i < timeout; i++) {
		/*
		 * If the SMBI bit is 0 when we read it, then the bit will be
		 * set and we have the semaphore
		 */
		swsm = IXGBE_READ_REG(hw, IXGBE_SWSM);
		if (!(swsm & IXGBE_SWSM_SMBI)) {
			status = IXGBE_SUCCESS;
			break;
		}
		usec_delay(50);
	}

	/* Now get the semaphore between SW/FW through the REGSMP bit */
	if (status == IXGBE_SUCCESS) {
		for (i = 0; i < timeout; i++) {
			swsm = IXGBE_READ_REG(hw, IXGBE_SWFW_SYNC);
			if (!(swsm & IXGBE_SWFW_REGSMP))
				break;

			usec_delay(50);
		}

		/*
		 * Release semaphores and return error if SW NVM semaphore
		 * was not granted because we don't have access to the EEPROM
		 */
		if (i >= timeout) {
			DEBUGOUT("REGSMP Software NVM semaphore "
			    "not granted.\n");
			ixgbe_release_swfw_sync_semaphore(hw);
			status = IXGBE_ERR_EEPROM;
		}
	} else {
		DEBUGOUT("Software semaphore SMBI between device drivers "
		    "not granted.\n");
	}

	return (status);
}

/*
 * ixgbe_release_nvm_semaphore - Release hardware semaphore
 * @hw: pointer to hardware structure
 *
 * This function clears hardware semaphore bits.
 */
static void
ixgbe_release_swfw_sync_semaphore(struct ixgbe_hw *hw)
{
	u32 swsm;

	DEBUGFUNC("ixgbe_release_swfw_sync_semaphore");

	/* Release both semaphores by writing 0 to the bits REGSMP and SMBI */

	swsm = IXGBE_READ_REG(hw, IXGBE_SWSM);
	swsm &= ~IXGBE_SWSM_SMBI;
	IXGBE_WRITE_REG(hw, IXGBE_SWSM, swsm);

	swsm = IXGBE_READ_REG(hw, IXGBE_SWFW_SYNC);
	swsm &= ~IXGBE_SWFW_REGSMP;
	IXGBE_WRITE_REG(hw, IXGBE_SWFW_SYNC, swsm);

	IXGBE_WRITE_FLUSH(hw);
}

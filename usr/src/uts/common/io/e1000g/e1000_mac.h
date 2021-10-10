/*
 * This file is provided under a CDDLv1 license.  When using or
 * redistributing this file, you may do so under this license.
 * In redistributing this file this license must be included
 * and no other modification of this header file is permitted.
 *
 * CDDL LICENSE SUMMARY
 *
 * Copyright(c) 1999 - 2009 Intel Corporation. All rights reserved.
 *
 * The contents of this file are subject to the terms of Version
 * 1.0 of the Common Development and Distribution License (the "License").
 *
 * You should have received a copy of the License with this software.
 * You can obtain a copy of the License at
 *	http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 */

/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * IntelVersion: 1.33 v2010-08-31
 */
#ifndef _E1000_MAC_H_
#define	_E1000_MAC_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Functions that should not be called directly from drivers but can be used
 * by other files in this 'shared code'
 */
void e1000_init_mac_ops_generic(struct e1000_hw *hw);
void e1000_null_mac_generic(struct e1000_hw *hw);
s32 e1000_null_ops_generic(struct e1000_hw *hw);
s32 e1000_null_link_info(struct e1000_hw *hw, u16 *s, u16 *d);
bool e1000_null_mng_mode(struct e1000_hw *hw);
void e1000_null_update_mc(struct e1000_hw *hw, u8 *h, u32 a);
void e1000_null_write_vfta(struct e1000_hw *hw, u32 a, u32 b);
void e1000_null_rar_set(struct e1000_hw *hw, u8 *h, u32 a);
s32 e1000_blink_led_generic(struct e1000_hw *hw);
s32 e1000_check_for_copper_link_generic(struct e1000_hw *hw);
s32 e1000_check_for_fiber_link_generic(struct e1000_hw *hw);
s32 e1000_check_for_serdes_link_generic(struct e1000_hw *hw);
s32 e1000_cleanup_led_generic(struct e1000_hw *hw);
s32 e1000_commit_fc_settings_generic(struct e1000_hw *hw);
s32 e1000_poll_fiber_serdes_link_generic(struct e1000_hw *hw);
s32 e1000_config_fc_after_link_up_generic(struct e1000_hw *hw);
s32 e1000_disable_pcie_master_generic(struct e1000_hw *hw);
s32 e1000_force_mac_fc_generic(struct e1000_hw *hw);
s32 e1000_get_auto_rd_done_generic(struct e1000_hw *hw);
s32 e1000_get_bus_info_pci_generic(struct e1000_hw *hw);
s32 e1000_get_bus_info_pcie_generic(struct e1000_hw *hw);
void e1000_set_lan_id_single_port(struct e1000_hw *hw);
void e1000_set_lan_id_multi_port_pci(struct e1000_hw *hw);
s32 e1000_get_hw_semaphore_generic(struct e1000_hw *hw);
s32 e1000_get_speed_and_duplex_copper_generic(struct e1000_hw *hw, u16 *speed,
    u16 *duplex);
s32 e1000_get_speed_and_duplex_fiber_serdes_generic(struct e1000_hw *hw,
    u16 *speed, u16 *duplex);
s32 e1000_id_led_init_generic(struct e1000_hw *hw);
s32 e1000_led_on_generic(struct e1000_hw *hw);
s32 e1000_led_off_generic(struct e1000_hw *hw);
void e1000_update_mc_addr_list_generic(struct e1000_hw *hw,
    u8 *mc_addr_list, u32 mc_addr_count);
s32 e1000_set_default_fc_generic(struct e1000_hw *hw);
s32 e1000_set_fc_watermarks_generic(struct e1000_hw *hw);
s32 e1000_setup_fiber_serdes_link_generic(struct e1000_hw *hw);
s32 e1000_setup_led_generic(struct e1000_hw *hw);
s32 e1000_setup_link_generic(struct e1000_hw *hw);

u32 e1000_hash_mc_addr_generic(struct e1000_hw *hw, u8 *mc_addr);

void e1000_clear_hw_cntrs_base_generic(struct e1000_hw *hw);
void e1000_clear_vfta_generic(struct e1000_hw *hw);
void e1000_config_collision_dist_generic(struct e1000_hw *hw);
void e1000_init_rx_addrs_generic(struct e1000_hw *hw, u16 rar_count);
void e1000_pcix_mmrbc_workaround_generic(struct e1000_hw *hw);
void e1000_put_hw_semaphore_generic(struct e1000_hw *hw);
void e1000_rar_set_generic(struct e1000_hw *hw, u8 *addr, u32 index);
s32 e1000_check_alt_mac_addr_generic(struct e1000_hw *hw);
void e1000_reset_adaptive_generic(struct e1000_hw *hw);
void e1000_set_pcie_no_snoop_generic(struct e1000_hw *hw, u32 no_snoop);
void e1000_update_adaptive_generic(struct e1000_hw *hw);
void e1000_write_vfta_generic(struct e1000_hw *hw, u32 offset, u32 value);

#ifdef __cplusplus
}
#endif

#endif	/* _E1000_MAC_H_ */

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
 * IntelVersion: 1.20 v2010-08-31
 */
#ifndef _E1000_NVM_H_
#define	_E1000_NVM_H_

#ifdef __cplusplus
extern "C" {
#endif

void e1000_init_nvm_ops_generic(struct e1000_hw *hw);
s32 e1000_null_read_nvm(struct e1000_hw *hw, u16 a, u16 b, u16 *c);
void e1000_null_nvm_generic(struct e1000_hw *hw);
s32 e1000_null_led_default(struct e1000_hw *hw, u16 *data);
s32 e1000_null_write_nvm(struct e1000_hw *hw, u16 a, u16 b, u16 *c);
s32 e1000_acquire_nvm_generic(struct e1000_hw *hw);

s32 e1000_poll_eerd_eewr_done(struct e1000_hw *hw, int ee_reg);
s32 e1000_read_mac_addr_generic(struct e1000_hw *hw);
s32 e1000_read_pba_string_generic(struct e1000_hw *hw, u8 *pba_num,
    u32 pba_num_size);
s32 e1000_read_pba_length_generic(struct e1000_hw *hw, u32 *pba_num_size);
s32 e1000_read_nvm_spi(struct e1000_hw *hw, u16 offset, u16 words, u16 *data);
s32 e1000_read_nvm_microwire(struct e1000_hw *hw, u16 offset,
    u16 words, u16 *data);
s32 e1000_read_nvm_eerd(struct e1000_hw *hw, u16 offset, u16 words, u16 *data);
s32 e1000_valid_led_default_generic(struct e1000_hw *hw, u16 *data);
s32 e1000_validate_nvm_checksum_generic(struct e1000_hw *hw);
s32 e1000_write_nvm_eewr(struct e1000_hw *hw, u16 offset,
    u16 words, u16 *data);
s32 e1000_write_nvm_microwire(struct e1000_hw *hw, u16 offset,
    u16 words, u16 *data);
s32 e1000_write_nvm_spi(struct e1000_hw *hw, u16 offset, u16 words, u16 *data);
s32 e1000_update_nvm_checksum_generic(struct e1000_hw *hw);
void e1000_stop_nvm(struct e1000_hw *hw);
void e1000_release_nvm_generic(struct e1000_hw *hw);

#define	E1000_STM_OPCODE	0xDB00

#ifdef __cplusplus
}
#endif

#endif	/* _E1000_NVM_H_ */

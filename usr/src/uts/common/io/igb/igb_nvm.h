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

/* IntelVersion: 1.21 scm_120210_003723 */

#ifndef _IGB_NVM_H
#define	_IGB_NVM_H

#ifdef __cplusplus
extern "C" {
#endif

void e1000_init_nvm_ops_generic(struct e1000_hw *hw);
s32 e1000_acquire_nvm_generic(struct e1000_hw *hw);

s32 e1000_poll_eerd_eewr_done(struct e1000_hw *hw, int ee_reg);
s32 e1000_read_mac_addr_generic(struct e1000_hw *hw);
s32 e1000_read_pba_length_generic(struct e1000_hw *hw, u32 *pba_num_size);
s32 e1000_read_pba_string_generic(struct e1000_hw *hw, u8 *pba_num,
    u32 pba_num_size);
s32 e1000_read_nvm_microwire(struct e1000_hw *hw, u16 offset,
    u16 words, u16 *data);
s32 e1000_read_nvm_eerd(struct e1000_hw *hw, u16 offset, u16 words,
    u16 *data);
s32 e1000_valid_led_default_generic(struct e1000_hw *hw, u16 *data);
s32 e1000_validate_nvm_checksum_generic(struct e1000_hw *hw);
s32 e1000_write_nvm_eewr(struct e1000_hw *hw, u16 offset,
    u16 words, u16 *data);
s32 e1000_write_nvm_microwire(struct e1000_hw *hw, u16 offset,
    u16 words, u16 *data);
s32 e1000_write_nvm_spi(struct e1000_hw *hw, u16 offset, u16 words,
    u16 *data);
s32 e1000_update_nvm_checksum_generic(struct e1000_hw *hw);
void e1000_release_nvm_generic(struct e1000_hw *hw);

#define	E1000_STM_OPCODE	0xDB00

#ifdef __cplusplus
}
#endif

#endif	/* _IGB_NVM_H */

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

/* IntelVersion: 1.18 scm_120210_003723 */

#ifndef _IGB_MANAGE_H
#define	_IGB_MANAGE_H

#ifdef __cplusplus
extern "C" {
#endif

bool e1000_check_mng_mode_generic(struct e1000_hw *hw);
bool e1000_enable_tx_pkt_filtering_generic(struct e1000_hw *hw);
s32  e1000_mng_enable_host_if_generic(struct e1000_hw *hw);
s32  e1000_mng_host_if_write_generic(struct e1000_hw *hw, u8 *buffer,
    u16 length, u16 offset, u8 *sum);
s32  e1000_mng_write_cmd_header_generic(struct e1000_hw *hw,
    struct e1000_host_mng_command_header *hdr);
s32  e1000_mng_write_dhcp_info_generic(struct e1000_hw *hw,
    u8 *buffer, u16 length);
bool e1000_enable_mng_pass_thru(struct e1000_hw *hw);

enum e1000_mng_mode {
	e1000_mng_mode_none = 0,
	e1000_mng_mode_asf,
	e1000_mng_mode_pt,
	e1000_mng_mode_ipmi,
	e1000_mng_mode_host_if_only
};

#define	E1000_FACTPS_MNGCG    0x20000000

#define	E1000_FWSM_MODE_MASK  0xE
#define	E1000_FWSM_MODE_SHIFT 1

#define	E1000_MNG_IAMT_MODE			0x3
#define	E1000_MNG_DHCP_COOKIE_LENGTH		0x10
#define	E1000_MNG_DHCP_COOKIE_OFFSET		0x6F0
#define	E1000_MNG_DHCP_COMMAND_TIMEOUT		10
#define	E1000_MNG_DHCP_TX_PAYLOAD_CMD		64
#define	E1000_MNG_DHCP_COOKIE_STATUS_PARSING	0x1
#define	E1000_MNG_DHCP_COOKIE_STATUS_VLAN	0x2

#define	E1000_VFTA_ENTRY_SHIFT			5
#define	E1000_VFTA_ENTRY_MASK			0x7F
#define	E1000_VFTA_ENTRY_BIT_SHIFT_MASK		0x1F

#define	E1000_HI_MAX_BLOCK_BYTE_LENGTH		1792 /* Num of bytes in range */
#define	E1000_HI_MAX_BLOCK_DWORD_LENGTH		448 /* Num of dwords in range */
/* Process HI command limit */
#define	E1000_HI_COMMAND_TIMEOUT		500

#define	E1000_HICR_EN			0x01  /* Enable bit - RO */
/* Driver sets this bit when done to put command in RAM */
#define	E1000_HICR_C			0x02
#define	E1000_HICR_SV			0x04  /* Status Validity */
#define	E1000_HICR_FW_RESET_ENABLE	0x40
#define	E1000_HICR_FW_RESET		0x80

/* Intel(R) Active Management Technology signature */
#define	E1000_IAMT_SIGNATURE  0x544D4149

#ifdef __cplusplus
}
#endif

#endif	/* _IGB_MANAGE_H */

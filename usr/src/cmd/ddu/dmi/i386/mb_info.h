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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * SMBIOS base board table structure
 * pls refer SMBIOS 2.4 spec
 */

#ifndef	_MB_INFO_H
#define	_MB_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct motherboard_info {
	uint8_t		manu;
	uint8_t		product;
	uint8_t		version;
	uint8_t		serial_no;
	uint8_t		asset_tag;
	uint8_t		features_flag;
	uint8_t		chassis;
	uint8_t		chassis_hdl_l;
	uint8_t		chassis_hdl_h;
	uint8_t		board_type;
} *motherboard_info_t;

void print_motherboard_info(smbios_hdl_t smb_hdl);

#ifdef __cplusplus
}
#endif

#endif /* _MB_INFO_H */

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
 * SMBIOS bios table structure
 * Pls refer SMBIOS 2.4 spec
 */

#ifndef	_BIOS_INFO_H
#define	_BIOS_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * bios_info structure define SMBIOS bios table
 * Pls refer SMBIOS spec
 */
typedef struct bios_info {
	uint8_t	vendor;
	uint8_t	version;
	uint16_t	start_addr;
	uint8_t	release_date;
	uint8_t	rom_size;
	uint16_t	bios_support1;
	uint16_t	bios_support2;
	uint16_t	bios_support3;
	uint16_t	bios_support4;
	uint8_t	bios_support_ext1;
	uint8_t	bios_support_ext2;
	uint8_t	bios_major_rev;
	uint8_t	bios_minor_rev;
	uint8_t	firm_major_rel;
	uint8_t	firm_minor_rel;
} *bios_info_t;

void print_bios_info(smbios_hdl_t smb_hdl);

#ifdef __cplusplus
}
#endif

#endif /* _BIOS_INFO_H */

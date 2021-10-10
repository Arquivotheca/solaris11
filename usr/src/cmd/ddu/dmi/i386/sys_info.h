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
 * SMBIOS system table structure
 * pls refer SMBIOS 2.4 spec
 */

#ifndef	_SYS_INFO_H
#define	_SYS_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct system_info {
	uint8_t		manu;
	uint8_t		product;
	uint8_t		version;
	uint8_t		serial_no;
	uint32_t	suuid[4];
	uint8_t		wake_type;
	uint8_t		sku_num;
	uint8_t		family;
} *system_info_t;

void print_system_info(smbios_hdl_t smb_hdl);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_INFO_H */

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

#ifndef	_MEM_INFO_H
#define	_MEM_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SMBIOS memory array and memory device table structure
 * pls refer SMBIOS 2.4 spec
 */

typedef struct mem_array_info {
	uint8_t		location;
	uint8_t		use;
	uint8_t		error_correct;
	uint8_t		max_capacity[4];
	uint8_t		error_info_handle[2];
	uint8_t		num_mem_devices[2];
} *mem_array_info_t;

typedef struct mem_device_info {
	uint16_t	array_handle;
	uint16_t	error_handle;
	uint16_t	total_width;
	uint16_t	data_width;
	uint16_t	size;
	uint8_t		form_factor;
	uint8_t		device_set;
	uint8_t		device_locator;
	uint8_t		bank_locator;
	uint8_t		mem_type;
	uint8_t		type_detail[2];
	uint8_t		speed[2];
	uint8_t		manu;
	uint8_t		serial;
	uint8_t		asset_tag;
	uint8_t		part_num;
} *mem_device_info_t;

void print_memory_subsystem_info(smbios_hdl_t smb_hdl);

#ifdef __cplusplus
}
#endif

#endif /* _MEM_INFO_H */

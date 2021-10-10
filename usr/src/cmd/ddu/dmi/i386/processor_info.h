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
 * SMBIOS processor table structure
 * pls refer SMBIOS 2.4 spec
 */

#ifndef	_PROCESSOR_INFO_H
#define	_PROCESSOR_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct psocket_info {
	uint8_t		socket_type;
	uint8_t		processor_type;
	uint8_t		processor_family;
	uint8_t		processor_manu;
	uint32_t	processor_id_l;
	uint32_t	processor_id_h;
	uint8_t		processor_version;
	uint8_t		voltage;
	uint16_t	external_clock;
	uint16_t	max_speed;
	uint16_t	cur_speed;
	uint8_t		status;
	uint8_t		upgrade;
	uint16_t	l1_cache_handle;
	uint16_t	l2_cache_handle;
	uint16_t	l3_cache_handle;
	uint8_t		serial_num;
	uint8_t		asset_tag;
	uint8_t		part_number;
	uint8_t		core_count;
	uint8_t		core_enable;
	uint8_t		thread_count;
	uint16_t	Characteristics;
} *psocket_info_t;

void print_processor_info(smbios_hdl_t smb_hdl);

#ifdef __cplusplus
}
#endif

#endif /* _PROCESSOR_INFO_H */

/*
 * Sun elects to have this file available under and governed by the BSD license
 * (see below for full license text).  However, the following notice
 * accompanied the original version of this file:
 */

/*
 * Copyright (c) 2010, Intel Corporation
 * All rights reserved.
 */

/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2010 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2010 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_IWP_FIRMWARE_H
#define	_IWP_FIRMWARE_H

/*
 * firmware image header
 */
typedef struct iwp_firmware_hdr {
	uint32_t	version;
	uint32_t	bld_nu;
	uint32_t	textsz;
	uint32_t	datasz;
	uint32_t	init_textsz;
	uint32_t	init_datasz;
	uint32_t	bootsz;
} iwp_firmware_hdr_t;

#define	IWP_FW_TLV_MAGIC	(0x0a4c5749)

typedef	struct iwp_firmware_hdr_tlv {
	uint32_t	zero;
	uint32_t	magic;
	uint8_t		description[64];
	uint32_t	version;
	uint32_t	bld;
	uint64_t	valid_bits;
} iwp_firmware_hdr_tlv_t;

#define	IWP_FW_TLV_INVALID	(0)
#define	IWP_FW_TLV_INST		(1)
#define	IWP_FW_TLV_DATA		(2)
#define	IWP_FW_TLV_INIT		(3)
#define	IWP_FW_TLV_INIT_DATA	(4)
#define	IWP_FW_TLV_BOOT		(5)

typedef	struct iwp_fw_sub_hdr_tlv {
	uint16_t	type;
	uint16_t	alt;
	uint32_t	len;
} iwp_fw_sub_hdr_tlv_t;

#endif	/* _IWP_FIRMWARE_H */

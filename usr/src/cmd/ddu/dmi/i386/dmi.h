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

#ifndef	_DMI_H
#define	_DMI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define	SMB_XSVC_DEVICE		"/dev/xsvc"
#define	SMB_SMBIOS_DEVICE	"/dev/smbios"

#define	SMB_ENTRY_START	0xf0000
#define	SMB_ENTRY_END	0xfffff

#define	ANCHOR_STR	"_SM_"
#define	ANCHOR_LEN	4

#define	HEADER_LEN	4

#define	SMB_NODE_ANY_TYPE	127
#define	SMB_EOL			127

/* SMBIOS table type */
#define	BIOS_INFO		0
#define	SYSTEM_INFO		1
#define	MOTHERBOARD_INFO	2
#define	PROCESSOR_INFO		4
#define	MEMORY_CONTROLLER_INFO	5
#define	MEMORY_MODULE_INFO	6
#define	ONBOARD_DEVICES_INFO	10
#define	MEMORY_ARRAY_INFO	16
#define	MEMORY_DEVICE_INFO	17

#define	INVAILD_HANDLE		0xffff

/* SMBIOS Structure Table Entry Point */
typedef struct smbios_entry {
	char		smb_anchor[4];
	uint8_t		entry_chksum;
	uint8_t		entry_len;
	uint8_t		smb_major;
	uint8_t		smb_minor;
	uint16_t	smb_maxsize;
	uint8_t		smb_rev;
	uint8_t		smb_format[5];
	uint8_t		smb_ianchor[5];
	uint8_t		smb_ichksum;
	uint16_t	table_len;
	uint32_t	table_addr;
	uint16_t	table_num;
	uint8_t		smb_bcdrev;
	uint8_t		rev;
} *smbios_entry_t;

/* Each SMBIOS structure begins with a 4-byte header */
typedef struct node_header {
	uint8_t		type;
	uint8_t		len;
	uint8_t		handle_l;
	uint8_t		handle_h;
} *node_header_t;

/* SMBIOS table */
typedef struct smbios_node {
	node_header_t		header;
	void 			*info;
	uint32_t		info_len;
	char 			*str_start;
	char 			*str_end;
	uint32_t		nstr;
	void 			*next;
} *smbios_node_t;

#define	header_type		header->type
#define	header_len		header->len
#define	header_handle_h		header->handle_h
#define	header_handle_l		header->handle_l

typedef struct smbios_hdl {
	smbios_entry_t		ent;
	void 			*smb_buf;
	smbios_node_t		smb_nodes;
	uint32_t		nnodes;
} *smbios_hdl_t;

#define	entry_smb_anchor	ent->smb_anchor
#define	entry_entry_chksum	ent->entry_chksum
#define	entry_entry_len		ent->entry_len
#define	entry_smb_major		ent->smb_major
#define	entry_smb_minor		ent->smb_minor
#define	entry_smb_maxsize	ent->smb_maxsize
#define	entry_smb_format	ent->smb_format
#define	entry_smb_ianchor	ent->smb_ianchor
#define	entry_smb_ichksum	ent->smb_ichksum
#define	entry_table_len		ent->table_len
#define	entry_table_addr	ent->table_addr
#define	entry_table_num		ent->table_num
#define	entry_smb_bcdrev	ent->smb_bcdrev

smbios_hdl_t smbios_open();
void smbios_close(smbios_hdl_t smb_hdl);
uint16_t smbios_version(smbios_hdl_t smb_hdl);
smbios_node_t smb_get_node_by_type(
smbios_hdl_t smb_hdl, smbios_node_t smb_node, uint8_t node_type);
smbios_node_t smb_get_node_by_handle(smbios_hdl_t smb_hdl, uint16_t node_hdl);
char *smb_get_node_str(smbios_node_t smb_node, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* _DMI_H */

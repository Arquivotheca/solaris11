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
 * Open, load and parse SMBIOS
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "libddudev.h"
#include "dmi.h"

/*
 * Get SMBIOS table node string
 *
 * smb_node: point to SMBIOS table node
 * index: string index
 *
 * Return
 * If successful return node string, else return NULL
 */
char
*smb_get_node_str(smbios_node_t smb_node, uint32_t index)
{
	char 		*str;
	uint32_t	i;
	int		len;

	if (index == 0 || index > smb_node->nstr) {
		return (NULL);
	}

	str = smb_node->str_start;

	for (i = 1; i < index; i++) {
		len = strlen(str);
		str = str + len + 1;
	}

	return (str);
}

/*
 * Get first SMBIOS table node by node type
 * If smb_node is NULL, get the table node from
 * SMBIOS tables head else from next node
 *
 * smb_hdl: Point to smbios_hdl
 * smb_node: If not NULL, start looking from this node
 * node_type: node type
 *
 * Return
 * If successful, return smbios_node pointer else
 * return NULL
 */
smbios_node_t
smb_get_node_by_type(smbios_hdl_t smb_hdl,
smbios_node_t smb_node, uint8_t node_type)
{
	smbios_node_t	node;

	if ((smb_hdl == NULL) || (smb_hdl->nnodes == 0)) {
		return (NULL);
	}

	if (smb_node == NULL) {
		node = smb_hdl->smb_nodes;
	} else {
		node = (smbios_node_t)smb_node->next;
	}

	if (node_type == SMB_NODE_ANY_TYPE) {
		return (node);
	}

	while (node != NULL) {
		if (node->header_type == node_type) {
			break;
		}
		node = node->next;
	}

	return (node);
}

/*
 * Parse the SMBIOS tables
 * SMBIOS table format:
 * --SMBIOS table header(size is 4)
 * --SMBIOS table info
 * SMBIOS header indicate table size.
 * For each table allocate a smbios_node to point it
 * smbios_node_t->header: point to table header
 * smbios_node_t->info: point to table info
 * smbios_node_t->info_len: table info length
 * smbios_node_t->str_start: string start address.(pls refer SMBIOS spec)
 * smbios_node_t->str_end: string end address.(pls refer SMBIOS spec)
 * smbios_node_t->nstr: number of string for this table(pls refer SMBIOS spec)
 * smbios_node_t->next: next table pointer
 *
 * Return
 * If successful return 0, else return 1
 */
int
init_smbbuf(smbios_hdl_t smb_hdl)
{
	char 		*p;
	char 		*buf_end;
	uint8_t		len;
	uint32_t	nhr, nstr;
	int		i;
	smbios_node_t	node;

	if (smb_hdl->smb_buf == NULL) {
		return (1);
	}

	if (smb_hdl->entry_table_len == 0) {
		return (1);
	}

	/*
	 * Check SMBIOS table number and allocate smbios_node structure
	 * for it
	 * If SMBIOS table number is 0, it is a empty SMBIOS, return 1
	 */
	nhr = smb_hdl->entry_table_num;
	if (nhr > 0) {
		smb_hdl->smb_nodes = calloc(nhr, sizeof (struct smbios_node));
		if (smb_hdl->smb_nodes == NULL) {
			perror("fail to allocate space for smbios node");
			return (1);
		}
	} else {
		return (1);
	}

	p = smb_hdl->smb_buf;
	buf_end = p + smb_hdl->entry_table_len;

	for (i = 0; i < nhr; i++) {

		nstr = 0;

		if ((p + HEADER_LEN) > buf_end) {
			return (1);
		}

		node = &smb_hdl->smb_nodes[i];
		node->header = (node_header_t)p;

		len = node->header_len;

		if ((p + len + 2) > buf_end) {
			return (1);
		}

		node->info = p + HEADER_LEN;
		node->info_len = len - HEADER_LEN;

		p = p + len;
		node->str_start = p;

		while (p < (buf_end - 1)) {
			if ((p[0] == '\0') && (p[1] == '\0')) {
				break;
			}

			if (p[0] == '\0') {
				nstr++;
			}
			p = p + 1;
		}

		if (p >= (buf_end - 1)) {
			return (1);
		}

		if (p > node->str_start) {
			nstr++;
		}

		node->str_end = p;
		node->nstr = nstr;

		if (i > 0) {
			smb_hdl->smb_nodes[i-1].next = node;
		}

		smb_hdl->nnodes++;

		if (node->header_type == SMB_EOL) {
			break;
		}

		p = p + 2;
	}
	node->next = NULL;

	return (0);
}

/*
 * Open and load SMBIOS from "/dev/xsvc"
 * 1. Memory map "/dev/xsvc" address from 0xf0000 to 0xfffff
 * 2. Lookup keyword "_SM_" in mapped space.
 * 3. If found keyword "_SM_", read the SMBIOS entry
 * 4. Get SMBIOS tables start address and size from SMBIOS entry
 * 5. Read SMBIOS tables
 * 6. Parse SMBIOS tables
 *
 * fd: "/dev/xsvd" handle
 *
 * Return:
 * Retrun smbios_hdl structure pointer, which contain
 * SMBIOS table content
 * If fail, return NULL
 */
smbios_hdl_t
smb_xsvcopen(int fd)
{
	smbios_hdl_t	hdl;
	char 		*bios;
	char 		*p;
	char 		*end;
	long		pgsize, pgmask, pgoff;
	int		ret;

	hdl = malloc(sizeof (struct smbios_hdl));

	if (hdl == NULL) {
		perror("fail to allocate memory space for smbios handle");
		return (NULL);
	}

	hdl->ent = NULL;
	hdl->smb_buf = NULL;
	hdl->smb_nodes = NULL;
	hdl->nnodes = 0;

	/* Memory map "/dev/xsvc" address from 0xf0000 to 0xfffff */
	bios = mmap(NULL, SMB_ENTRY_END - SMB_ENTRY_START + 1,
	    PROT_READ, MAP_SHARED, fd, (uint32_t)SMB_ENTRY_START);

	if (bios == MAP_FAILED) {
		perror("fail to memory map /dev/xsvc");
		smbios_close(hdl);
		return (NULL);
	}

	end = bios + SMB_ENTRY_END - SMB_ENTRY_START + 1;

	/* Lookup keyword "_SM_" in mapped space */
	for (p = bios; p < end; p = p + 16) {
		if (strncmp(p, ANCHOR_STR, ANCHOR_LEN) == 0) {
			break;
		}
	}

	if (p >= end) {
		FPRINTF(stderr,
		    "can not find smbios table in /dev/xsvc\n");
		(void) munmap(bios, SMB_ENTRY_END - SMB_ENTRY_START + 1);
		smbios_close(hdl);
		return (NULL);
	}

	hdl->ent = malloc(sizeof (struct smbios_entry));

	if (hdl->ent == NULL) {
		perror("fail to allocate memory space for smbios entry");
		(void) munmap(bios, SMB_ENTRY_END - SMB_ENTRY_START + 1);
		smbios_close(hdl);
		return (NULL);
	}

	/* Read SMBIOS entry */
	(void) memcpy(hdl->ent, p, sizeof (struct smbios_entry));
	(void) munmap(bios, SMB_ENTRY_END - SMB_ENTRY_START + 1);

	pgsize = sysconf(_SC_PAGESIZE);
	pgmask = ~(pgsize - 1);
	pgoff = hdl->entry_table_addr & ~pgmask;

	/* Memory map SMBIOS tables */
	bios = mmap(NULL, hdl->entry_table_len + pgoff,
	    PROT_READ, MAP_SHARED, fd, hdl->entry_table_addr & pgmask);

	if (bios == MAP_FAILED) {
		perror("fail to memory map smbios tables");
		smbios_close(hdl);
		return (NULL);
	}

	hdl->smb_buf = malloc(hdl->entry_table_len);

	if (hdl->smb_buf == NULL) {
		perror("fail to allocate memory space for smbios tables");
		smbios_close(hdl);
		return (NULL);
	}

	/* Read SMBIOS tables */
	(void) memcpy(hdl->smb_buf, bios+pgoff, hdl->entry_table_len);
	(void) munmap(bios, hdl->ent->table_len + pgoff);

	ret = init_smbbuf(hdl);
	if (ret) {
		FPRINTF(stderr,
		    "fail to parse smbios tables\n");
		smbios_close(hdl);
		return (NULL);
	}

	return (hdl);
}

/*
 * Open and load SMBIOS from "/dev/smbios"
 * 1. Read SMBIOS entry structure
 * 2. Get SMBIOS tables start address and size from SMBIOS entry
 * 3. Read SMBIOS tables
 * 4. Parse SMBIOS tables
 *
 * fd: "/dev/smbios" handle
 *
 * Return:
 * Retrun smbios_hdl structure pointer, which contain
 * SMBIOS table content
 * If fail, return NULL
 */
smbios_hdl_t
smb_biosopen(int fd)
{
	smbios_hdl_t	hdl;
	ssize_t		n;
	int		ret;

	hdl = malloc(sizeof (struct smbios_hdl));

	if (hdl == NULL) {
		perror("fail to allocate memory space for smbios handle");
		return (NULL);
	} else {
		hdl->ent = NULL;
		hdl->smb_buf = NULL;
		hdl->smb_nodes = NULL;
		hdl->nnodes = 0;
	}

	hdl->ent = malloc(sizeof (struct smbios_entry));

	if (hdl->ent == NULL) {
		perror("fail to allocate memory space for smbios entry");
		smbios_close(hdl);
		return (NULL);
	}

	/* Read SMBIOS entry from "/dev/smbios" */
	n = pread(fd, hdl->ent, sizeof (struct smbios_entry), 0);

	if (n != sizeof (struct smbios_entry)) {
		FPRINTF(stderr,
		    "fail to read SMBIOS entry from /dev/smbios\n");
		smbios_close(hdl);
		return (NULL);
	}

	if (strncmp(hdl->entry_smb_anchor, ANCHOR_STR, ANCHOR_LEN) != 0) {
		FPRINTF(stderr,
		    "mismatch smbios anchor string\n");
		smbios_close(hdl);
		return (NULL);
	}

	hdl->smb_buf = malloc(hdl->entry_table_len);

	if (hdl->smb_buf == NULL) {
		perror("fail to allocate memory space for smbios tables");
		smbios_close(hdl);
		return (NULL);
	}

	/* Read SMBIOS tables and store in hdl->smb_buf */
	n = pread(fd, hdl->smb_buf, hdl->entry_table_len,
	    hdl->entry_table_addr);

	if (n != hdl->entry_table_len) {
		perror("fail to read smbios tables from /dev/smbios");
		smbios_close(hdl);
		return (NULL);
	}

	/* Parse SMBIOS tables */
	ret = init_smbbuf(hdl);
	if (ret) {
		FPRINTF(stderr,
		    "fail to parse smbios tables\n");
		smbios_close(hdl);
		return (NULL);
	}

	return (hdl);
}

/*
 * Get SMBIOS version information
 *
 * smb_hdl: Point to smbios_hdl structure,
 * which contain SMBIOS version information.
 *
 * Return:
 * Return SMBIOS version
 */
uint16_t
smbios_version(smbios_hdl_t smb_hdl)
{
	uint16_t	version;
	uint8_t	major;
	uint8_t	minor;
	uint8_t	revision;

	major = smb_hdl->entry_smb_major;
	minor = smb_hdl->entry_smb_minor;

	if (minor > 10) {
		revision = minor % 10;
		minor = minor / 10;
	} else {
		revision = 0;
	}

	version = (major << 8) | ((minor & 0xf) << 4) | (revision & 0xf);

	return (version);
}

/*
 * Open, load and parse SMBIOS
 * Open SMBIOS from "/dev/smbios", if no such device,
 * Open "/dev/xsvc"(solaris low version) to lookup SMBIOS
 *
 * Return:
 * smbios_hdl_t: Point to smbios_hdl structure
 * smbios_hdl_t->ent: store SMBIOS Entry structure, which
 * indicate smbios tables length, version, table start address, etc.
 * smbios_hdl_t->smb_buf: store SMBIOS tables raw data.
 * smbios_hdl_t->smb_nodes: store SMBIOS tables nodes array.
 * smbios_hdl_t->nnodes: number of SMBIOS table node.
 * If fail to get SMBIOS, then return NULL.
 */

smbios_hdl_t
smbios_open()
{
	int		fd;
	smbios_hdl_t	hdl = NULL;

	fd = open(SMB_SMBIOS_DEVICE, O_RDONLY);

	if (fd == -1) {
		fd = open(SMB_XSVC_DEVICE, O_RDONLY);
		if (fd != -1) {
			hdl = smb_xsvcopen(fd);
		}
	} else {
		hdl = smb_biosopen(fd);
	}

	if (fd != -1) {
		(void) close(fd);
	}

	return (hdl);
}

/*
 * Free smbios resource
 */
void
smbios_close(smbios_hdl_t smb_hdl)
{
	if (smb_hdl == NULL) {
		return;
	}

	if (smb_hdl->ent) {
		free(smb_hdl->ent);
	}

	if (smb_hdl->smb_nodes) {
		free(smb_hdl->smb_nodes);
	}

	if (smb_hdl->smb_buf) {
		free(smb_hdl->smb_buf);
	}

	free(smb_hdl);
}

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
 * Print memory array and devices information
 * Pls refer SMBIOS 2.4 spec
 */

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include "libddudev.h"
#include "dmi.h"
#include "mem_info.h"

/* Memory Array - Use */
static const char *array_use[] = {
	"Other",
	"Unknown",
	"System memory",
	"Video memory",
	"Flash memory",
	"Non-volatile RAM",
	"Cache memory"
};

/* Memory Array - Error Correction Types */
static const char *array_err_correct_types[] = {
	"Other",
	"Unknown",
	"None",
	"Parity",
	"Single-bit ECC",
	"Multi-bit ECC",
	"CRC"
};

/* Memory Device - Type */
static const char *mem_dev_types[] = {
	"Other",
	"Unknown",
	"DRAM",
	"EDRAM",
	"VRAM",
	"SRAM",
	"RAM",
	"ROM",
	"FLASH",
	"EEPROM",
	"FEPROM",
	"EPROM",
	"CDRAM",
	"3DRAM",
	"SDRAM",
	"SGRAM",
	"RDRAM",
	"DDR",
	"DDR2",
	"DDR2 FB-DIMM"
};

/*
 * Get memory size
 *
 * info: point mem_array_info structure
 * info->max_capacity[3]: memory size [24 ~ 31]
 * info->max_capacity[2]: memory size [16 ~ 23]
 * info->max_capacity[1]: memory size [8 ~ 15]
 * info->max_capacity[0]: memory size [0 ~ 7]
 *
 * Return:
 * Return memory max capacity size
 */
uint32_t
get_mem_max_capacity(mem_array_info_t info)
{
	uint32_t	size;

	size = (((uint32_t)info->max_capacity[3]) << 24) |
	    (((uint32_t)info->max_capacity[2]) << 16) |
	    (((uint32_t)info->max_capacity[1]) << 8) |
	    info->max_capacity[0];

	return (size);
}

/*
 * Print Memory Device Information
 * From SMBIOS Memory Device Table(type is 17),
 * list memory device information.
 *
 * smb_node: point to SMBIOS memory device table
 * version: current SMBIOS version
 */
void
print_memory_device_info(smbios_node_t smb_node, uint16_t version)
{
	mem_device_info_t	info;
	uint16_t		data16;
	char 			*str;

	/* Get Memory Device Table */
	info = (mem_device_info_t)smb_node->info;

	if (info->size == 0) {
		return;
	}

	PRINTF("   Memory Device Locator:");
	str = smb_get_node_str(smb_node, info->device_locator);
	if (str) {
		PRINTF("%s", str);
	}
	PRINTF("\n");

	PRINTF("   Total Width:");
	if (info->total_width == 0xffff) {
		PRINTF("Unknown\n");
	} else {
		PRINTF("%u\n", info->total_width);
	}

	PRINTF("   Data Width:");
	if (info->data_width == 0xffff) {
		PRINTF("Unknown\n");
	} else {
		PRINTF("%u\n", info->data_width);
	}

	/* Get memory device size */
	PRINTF("   Installed Size:");
	switch (info->size) {
		case 0:
			PRINTF("Not Installed\n");
			break;
		case 0xffff:
			PRINTF("Unknown\n");
			break;
		default:
			data16 = info->size &0x7fff;

			if (info->size & 0x8000) {
				PRINTF("%uK\n",
				    data16);
			} else {
				PRINTF("%uM\n",
				    data16);
			}
	}

	PRINTF("   Memory Device Type:");
	if ((info->mem_type > 0) && (info->mem_type < 0x15)) {
		PRINTF("%s\n", mem_dev_types[info->mem_type - 1]);
	} else {
		PRINTF("Unknown\n");
	}

	PRINTF("   Speed:");
	/* If SMBIOS version >=2.3, then get memory speed */
	if (version >= 0x230) {
		data16 = (info->speed[1] << 8) | info->speed[0];
		if (data16 != 0) {
			PRINTF("%uMHZ\n", data16);
		} else {
			PRINTF("Unknown\n");
		}
	} else {
		PRINTF("Unknown\n");
	}
}

/*
 * Print Memory Array memory devices information
 * For a Memory Array, according its table handle to
 * lookup its memory devices, and print memory devices
 * information
 *
 * smb_hdl: point SMBIOS structure
 * array_hdl: Memory array table handle
 */
void
print_array_devices_info(smbios_hdl_t smb_hdl, uint16_t array_hdl)
{
	smbios_node_t		node;
	mem_device_info_t	info;
	uint16_t		version;
	int			index;
	int			unins;
	int			i;

	/* Get memory devices table from SMBIOS */
	version = smbios_version(smb_hdl);
	node = smb_get_node_by_type(smb_hdl, NULL, MEMORY_DEVICE_INFO);

	index = 0;
	unins = 0;

	while (node) {
		info = (mem_device_info_t)node->info;

		/* Match memory devices array handle with array handle */
		/* If matched, print memory devices information */
		if (info->array_handle == array_hdl) {
			if (info->size) {
				PRINTF("\n  Memory Device %d:\n", index);
				index++;
				print_memory_device_info(node, version);
			} else {
				unins++;
			}
		}
		node = smb_get_node_by_type(smb_hdl, node, MEMORY_DEVICE_INFO);
	}

	for (i = 0; i < unins; i++) {
		PRINTF("\n  Memory Device %d:\n", index + i);
		PRINTF("   [Not Installed]\n");
	}
}

/*
 * Print Memory Array information
 * From SMBIOS Memory Array Table(type is 16),
 * list memory array information, and search
 * its memory devices to list memory device
 * information
 *
 * smb_node: point to SMBIOS memory array table
 * index: record number of Memory array
 */
void
print_memory_array_info(smbios_node_t smb_node, int index)
{
	mem_array_info_t	info;
	uint32_t		data32;
	uint16_t		data16;

	info = (mem_array_info_t)smb_node->info;

	PRINTF(" Memory Subsystem %d:\n", index);

	PRINTF("  Array Used Function:");
	if ((info->use > 0) && (info->use < 8)) {
		PRINTF("%s", array_use[info->use - 1]);
	}
	PRINTF("\n");

	PRINTF("  Memory Error Correction Supported:");
	if ((info->error_correct > 0) && (info->error_correct < 8)) {
		PRINTF("%s",
		    array_err_correct_types[info->error_correct - 1]);
	}
	PRINTF("\n");

	/* Calculate max capacity which array support */
	PRINTF("  Maximum Array Capacity:");
	data32 = get_mem_max_capacity(info);

	if (data32 == 0x80000000) {
		PRINTF("Unknown\n");
	} else {
		data32 = data32 / 1024;

		if (data32 < 1024) {
			PRINTF("%uM\n",
			    data32);
		} else {
			PRINTF("%uG\n",
			    data32 / 1024);
		}
	}

	data16 = (info->num_mem_devices[1] << 8) | info->num_mem_devices[0];
	PRINTF("  Number of Memory Devices:%u\n", data16);
}

/*
 * If found Memory array table (SMBIOS version >= 2.1)
 * Print memory array and memory devices information
 * Lookup each memory array table(type is 16), print
 * array information and get its memory devices.
 *
 * smb_hdl: point to SMBIOS structure
 */
void
print_memory_array_device_info(smbios_hdl_t smb_hdl)
{
	smbios_node_t		node;
	uint16_t		header_handle;
	int			i;

	node = smb_get_node_by_type(smb_hdl, NULL, MEMORY_ARRAY_INFO);

	if (node == NULL) {
		return;
	}

	i = 0;
	while (node) {
		header_handle =
		    (uint16_t)(((uint16_t)node->header_handle_h << 8) |
		    (uint16_t)node->header_handle_l);
		if (i) {
			PRINTF("\n");
		}
		/* Found Memory Array handle and print its information */
		print_memory_array_info(node, i);
		/* Lookup memory array devices and print memory device info */
		print_array_devices_info(smb_hdl, header_handle);
		/* Lookup next memory array */
		node = smb_get_node_by_type(smb_hdl, node, MEMORY_ARRAY_INFO);
		i++;
	}
}

/*
 * Match the memory device by header and get memory device size
 * The granularity is K
 *
 * node: memory device node handle
 * header: device header
 *
 * Return:
 * Return memory device size, the granularity is K
 */
uint32_t
get_mem_dev_size_by_header(smbios_node_t node, uint16_t header)
{
	uint32_t		size;
	mem_device_info_t	info;
	char 			*str;

	info = (mem_device_info_t)node->info;
	if (info->array_handle != header) {
		return (0);
	}

	str = smb_get_node_str(node, info->device_locator);

	/*
	 * If this device is
	 * SYSTEM ROM, size is 0
	 */
	if (str) {
		if (strcmp(str, "SYSTEM ROM") == 0) {
			return (0);
		}
	}

	if ((info->size == 0xffff) || (info->size == 0)) {
			return (0);
	}

	size = info->size & 0x7fff;

	/*
	 * if info->size bit 15 is 0, the granularity is M.
	 * multiply by 1024 to change to K
	 */
	if ((info->size & 0x8000) == 0) {
		size = size << 10;
	}

	return (size);
}

/*
 * Print each memory devices installed size
 * For each array, lookup its memory devices and
 * print installed size
 *
 * smb_hdl: point to SMBIOS structure
 */
void
prt_phy_slot_size(smbios_hdl_t smb_hdl)
{
	smbios_node_t		array_node;
	smbios_node_t		slot_node;
	mem_array_info_t	array_info;
	uint32_t		size;
	char			c;
	uint32_t		value, mod;
	uint16_t		header_handle;
	int			first;

	first = 0;

	array_node = smb_get_node_by_type(smb_hdl, NULL, MEMORY_ARRAY_INFO);

	/* Get each memory array */
	while (array_node) {
		header_handle =
		    (uint16_t)(((uint16_t)array_node->header_handle_h << 8) |
		    (uint16_t)array_node->header_handle_l);
		array_info = (mem_array_info_t)array_node->info;
		/* If array used for video memory(4) or cache memory(7) */
		/* skip it */
		if ((array_info->use != 4) && (array_info->use != 7)) {
			slot_node = smb_get_node_by_type(smb_hdl, NULL,
			    MEMORY_DEVICE_INFO);
			/* Display each memory devices size in this array */
			while (slot_node) {
				/* Get memory array devices size */
				size = get_mem_dev_size_by_header(slot_node,
				    header_handle);

				if (size > 0) {
					if (first == 0) {
						first = 1;
						PRINTF("(");
					} else {
						PRINTF(" + ");
					}

					if (size < 1024) {
						value = size;
						mod = 0;
						c = 'K';
					} else {
						value = size / 1024;
						mod = size % 1024;

						if (value > 1024) {
							value = size /
							    (1024 * 1024);
							mod = size %
							    (1024 * 1024);
							c = 'G';
						} else {
							c = 'M';
						}
					}

					if (mod) {
						PRINTF("%u.%u%c",
						    value,
						    (mod * 10) / 1024, c);
					} else {
						PRINTF("%u%c", value, c);
					}
				}

				slot_node = smb_get_node_by_type(smb_hdl,
				    slot_node,
				    MEMORY_DEVICE_INFO);
			}
		}
		array_node = smb_get_node_by_type(smb_hdl, array_node,
		    MEMORY_ARRAY_INFO);
	}

	if (first) {
		PRINTF(")\n");
	}
}

/*
 * Get Memory array installed size
 * Calculate each memory device size in array(not include SYSTEM ROM)
 *
 * smb_hdl: Point to Memory array table
 * array_hdl: Memory array handle
 *
 * Return:
 * Return memory array installed size
 */
uint32_t
get_mem_array_size(smbios_hdl_t smb_hdl, uint16_t array_hdl)
{
	smbios_node_t		node;
	uint32_t		array_size, size;

	array_size = 0;
	node = smb_get_node_by_type(smb_hdl, NULL, MEMORY_DEVICE_INFO);

	while (node) {
		size = get_mem_dev_size_by_header(node, array_hdl);
		array_size = array_size + size;
		node = smb_get_node_by_type(smb_hdl, node, MEMORY_DEVICE_INFO);
	}

	return (array_size);
}

/*
 * Get System Memory Installed Size
 * Calculate each memory array installed size.(not include
 * video memory, cache memory, system rom)
 *
 * smb_hdl: point SMBIOS structure
 *
 * Return:
 * Return system memory installed size
 */
uint32_t
get_phy_mem_size(smbios_hdl_t smb_hdl)
{
	smbios_node_t		node;
	mem_array_info_t	info;
	uint32_t		size;
	uint16_t		header_handle;

	size = 0;
	node = smb_get_node_by_type(smb_hdl, NULL, MEMORY_ARRAY_INFO);

	/* For each memory array, calculate installed size */
	while (node) {
		info = (mem_array_info_t)node->info;
		header_handle =
		    (uint16_t)(((uint16_t)node->header_handle_h << 8) |
		    (uint16_t)node->header_handle_l);

		/* Doesn't include video, cache memory size */
		if ((info->use != 4) && (info->use != 7)) {
			size = size +
			    get_mem_array_size(smb_hdl, header_handle);
		}
		node = smb_get_node_by_type(smb_hdl, node, MEMORY_ARRAY_INFO);
	}

	return (size);
}

/*
 * Get System Max Support Memory Size
 * Calculate each memory array max support size.(not include
 * video memory, cache memory)
 *
 * smb_hdl: point SMBIOS structure
 *
 * Return:
 * Return system max support memory size
 */
unsigned int
get_max_mem_size(smbios_hdl_t smb_hdl)
{
	smbios_node_t		node;
	mem_array_info_t	info;
	unsigned int		size;
	uint32_t		data32;

	size = 0;
	node = smb_get_node_by_type(smb_hdl, NULL, MEMORY_ARRAY_INFO);

	while (node) {
		info = (mem_array_info_t)node->info;

		if ((info->use != 4) && (info->use != 7)) {
			data32 = get_mem_max_capacity(info);

			if (data32 == 0x80000000) {
				data32 = 0;
			} else {
				data32 = data32 / 1024;
			}
			size = size + data32;
		}
		node = smb_get_node_by_type(smb_hdl, node, MEMORY_ARRAY_INFO);
	}

	return (size);
}

/*
 * Print system memory information
 * - System memory max support size
 * - System memory installed size
 * - Memory Array information
 * - Memory Devices information
 *
 * smb_hdl: point to SMBIOS structure
 */
void
print_memory_subsystem_info(smbios_hdl_t smb_hdl)
{
	unsigned int		size;
	unsigned int		max_size;
	unsigned int		value, mod;
	char			c;

	size = get_phy_mem_size(smb_hdl);
	max_size = get_max_mem_size(smb_hdl);

	/*
	 * Prefer to get actual installed memory size from SMBIOS.
	 * If that fails (size=0), get memory size from sysconf(3C).
	 * Note that sysconf returns the amount of memory the system
	 * can use, not the total amount of physical memory.
	 */
	if (size == 0) {
		u_longlong_t		pages;
		u_longlong_t		pagesize;

		pagesize = sysconf(_SC_PAGESIZE);
		pages = sysconf(_SC_PHYS_PAGES);

		PRINTF("Physical Memory: %lluM\n",
		    pages*pagesize >> 20);
	} else {
		if (size < 1024) {
			value = size;
			mod = 0;
			c = 'K';
		} else {
			value = size / 1024;
			mod = size % 1024;

			if (value > 1024) {
				value = size / (1024 * 1024);
				mod = size % (1024 * 1024);
				c = 'G';
			} else {
				c = 'M';
			}
		}

		if (mod) {
			PRINTF("Physical Memory: %u.%u%c ",
			    value, (mod * 10) / 1024, c);
		} else {
			PRINTF("Physical Memory: %u%c ",
			    value, c);
		}
		/* print each memory device size */
		prt_phy_slot_size(smb_hdl);
	}

	/* Print max support size */
	if (max_size) {
		if (max_size < size) {
			max_size = size;
		}

		if (max_size >= 1024) {
			mod = max_size % 1024;

			if (mod) {
			PRINTF("Maximum Memory Support: %u.%uG\n",
			    max_size /1024, (mod * 10) / 1024);
			} else {
			PRINTF("Maximum Memory Support: %uG\n",
			    max_size /1024);
			}
		} else {
			PRINTF("Maximum Memory Support: %uM\n",
			    max_size);
		}
	}

	PRINTF("\n");

	/* Print memory array and devices information */
	print_memory_array_device_info(smb_hdl);
}

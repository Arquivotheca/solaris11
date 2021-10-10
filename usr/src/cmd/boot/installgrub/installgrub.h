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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_INSTALLGRUB_H
#define	_INSTALLGRUB_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/multiboot.h>
#include "./../common/bblk_einfo.h"
#include "./../common/boot_utils.h"

typedef struct _device_data {
	char		*path;
	char		*path_p0;
	uint8_t		type;
	int		part_fd;
	int		disk_fd;
	int		slice;
	int		partition;
	uint32_t	start_sector;
	char		boot_sector[SECTOR_SIZE];
} ig_device_t;

typedef struct _stage2_data {
	char			*buf;
	char			*file;
	char			*extra;
	multiboot_header_t	*mboot;
	uint32_t		mboot_off;
	uint32_t		file_size;
	uint32_t		buf_size;
	uint32_t		first_sector;
	uint32_t		pcfs_first_sectors[2];
} ig_stage2_t;

typedef struct _ig_data {
	char		stage1_buf[SECTOR_SIZE];
	ig_stage2_t	stage2;
	ig_device_t	device;
} ig_data_t;

enum ig_devtype_t {
	IG_DEV_X86BOOTPAR = 1,
	IG_DEV_SOLVTOC
};

#define	is_bootpar(type)	(type == IG_DEV_X86BOOTPAR)

#define	STAGE2_MEMADDR		(0x8000)	/* loading addr of stage2 */

#define	STAGE1_BPB_OFFSET	(0x3)
#define	STAGE1_BPB_SIZE		(0x3B)
#define	STAGE1_BOOT_DRIVE	(0x40)
#define	STAGE1_FORCE_LBA	(0x41)
#define	STAGE1_STAGE2_ADDRESS	(0x42)
#define	STAGE1_STAGE2_SECTOR	(0x44)
#define	STAGE1_STAGE2_SEGMENT	(0x48)

#define	STAGE2_BLOCKLIST	(SECTOR_SIZE - 0x8)
#define	STAGE2_INSTALLPART	(SECTOR_SIZE + 0x8)
#define	STAGE2_FORCE_LBA	(SECTOR_SIZE + 0x11)
#define	STAGE2_BLKOFF		(50)	/* offset from start of fdisk part */

#ifdef	__cplusplus
}
#endif

#endif /* _INSTALLGRUB_H */

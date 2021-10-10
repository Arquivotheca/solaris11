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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */
#ifndef	_GFXP_VGA_H
#define	_GFXP_VGA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/vgareg.h>
#include <sys/vgasubr.h>
#include <sys/pci.h>

#define	TEXT_ROWS		25
#define	TEXT_COLS		80

#define	VGA_BRIGHT_WHITE	0x0f
#define	VGA_BLACK		0x00

#define	VGA_REG_ADDR		0x3c0
#define	VGA_REG_SIZE		0x20

#define	VGA_MEM_ADDR		0xa0000
#define	VGA_MEM_SIZE		0x20000

#define	VGA_MMAP_FB_BASE	VGA_MEM_ADDR

#define	VGA_REG_MEM		(0)
#define	VGA_REG_IO		(1)

struct gfxp_vga_console {
	struct vgaregmap	regs;
	struct vgaregmap 	fb;
	int			fb_regno;
	off_t			fb_size;
	char			shadow[TEXT_ROWS*TEXT_COLS*2];
	caddr_t			text_base;	/* hardware text base */
	caddr_t			current_base;	/* hardware or shadow */
	char			happyface_boot;
	struct {
		boolean_t visible;
		int row;
		int col;
	} 			cursor;
	struct {
		unsigned char red;
		unsigned char green;
		unsigned char blue;
	}			colormap[VGA8_CMAP_ENTRIES];
	unsigned char attrib_palette[VGA_ATR_NUM_PLT];
};

#ifdef __cplusplus
}
#endif

#endif /* _GFXP_VGA_H */

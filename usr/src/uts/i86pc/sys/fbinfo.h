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

#ifndef _FBINFO_H
#define	_FBINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/font.h>
#include <sys/rgb.h>

typedef uint16_t	fb_p_size_t;
typedef uint16_t	fb_c_size_t;

typedef struct fb_info_pix_coord {
	fb_p_size_t	x;
	fb_p_size_t	y;
} fb_info_p_coord_t;

typedef struct fb_info_char_coord {
	fb_c_size_t	cols;
	fb_c_size_t	rows;
} fb_info_c_coord_t;

/*
 * This structure acts as a glue for the various components that have to deal
 * with the frame buffer. In particular:
 * - keeps track of what has been decided/done at boot time once we move to
 *   tem/coherent console.
 * - connects the VESA/EFI/etc specific code to the boot framebuffer console.
 */
typedef struct	fb_info {
	/* mandatory information to handle the framebuffer. */
	fb_info_p_coord_t	screen_pix;
	uint8_t			depth;
	uint8_t			bpp;
	uintptr_t		phys_addr;
	uint64_t		phys_mem;
	uint64_t		used_mem;
	/* color information. */
	rgb_t			rgb;
	rgb_color_t		rsvd;
	/* effective number of rows and cols (set_font might modify this). */
	fb_info_c_coord_t	screen_char;
	/* requested number of rows and cols. */
	fb_info_c_coord_t	requested_char;
	/* track cursor position between boot and standard console. */
	fb_info_p_coord_t	cursor_pix;
	fb_info_c_coord_t	cursor_char;
	/* selected font width and height. */
	fb_p_size_t		font_width;
	fb_p_size_t		font_height;
	/* starting offset of the terminal window. */
	fb_info_p_coord_t	start_pix;
	/* boot time flags. */
	uint32_t		flags;
} fb_info_t;

extern fb_info_t	fb_info;

#ifdef __cplusplus
}
#endif

#endif /* _FBINFO_H */

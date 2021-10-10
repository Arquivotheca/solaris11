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
/*
 * This code is partially based on the pnglite project. Copyright follows.
 */
/* BEGIN CSTYLED */
/*  pnglite.h - Interface for pnglite library
    Copyright (c) 2007 Daniel Karling

     This software is provided 'as-is', without any express or implied
     warranty. In no event will the authors be held liable for any damages
     arising from the use of this software.

     Permission is granted to anyone to use this software for any purpose,
     including commercial applications, and to alter it and redistribute it
     freely, subject to the following restrictions:

     1. The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software
        in a product, an acknowledgment in the product documentation would be
        appreciated but is not required.  

     2. Altered source versions must be plainly marked as such, and must not be
        misrepresented as being the original software.

     3. This notice may not be removed or altered from any source
        distribution.

     Daniel Karling
     daniel.karling@gmail.com
*/
/* END CSTYLED */

#ifndef	_SYS_PNGLIB_H
#define	_SYS_PNGLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/rgb.h>

/*
 * Enumerations for pnglite.
 * Negative numbers are error codes and 0 and up are okay responses.
 */
enum {
	PNG_DONE		= 1,
	PNG_NO_ERROR		= 0,
	PNG_FILE_ERROR		= -1,
	PNG_HEADER_ERROR	= -2,
	PNG_IO_ERROR		= -3,
	PNG_EOF_ERROR		= -4,
	PNG_CRC_ERROR		= -5,
	PNG_MEMORY_ERROR	= -6,
	PNG_ZLIB_ERROR		= -7,
	PNG_UNKNOWN_FILTER	= -8,
	PNG_NOT_SUPPORTED	= -9,
	PNG_WRONG_ARGUMENTS	= -10,
	PNG_INIT_ERROR		= -11
};

/*
 * Vertical positioning for when the image does not fit in the screen:
 * PNG_VTOP:     paint from top of the image up to the end of the screen space.
 * PNG_VCENTER:  center the image vertically.
 * PNG_VBOTTOM:  paint from the bottom of the image up to the top of the screen
 *               space.
 */
enum {
	PNG_VTOP		= 0,
	PNG_VCENTER,
	PNG_VBOTTOM,
	PNG_VLAST
};

/*
 * Horizontal positioning for when the image does not fit in the screen:
 * PNG_HLEFT:    paint the left part of the image up to the right-end of the
 *		 screen space.
 * PNG_HCENTER:  center the image horizontally.
 * PNG_HRIGHT:   paint the right part of the image up the left-end of the
 *               screen space.
 */

enum {
	PNG_HLEFT		= PNG_VLAST,
	PNG_HCENTER,
	PNG_HRIGHT
};

/*
 * png_screen_t describes the target screen where the image will be painted.
 * The caller uses this structure to inform the png library code on how to
 * fill the destination buffer passed with 'dstbuf'. 'h_pos' and 'v_pos' are
 * one of the constants described above.
 */
typedef struct _png_screen_info {
	uint32_t	width;
	uint32_t	height;
	uint32_t	depth;
	uint8_t		bpp;
	uint8_t		h_pos, v_pos;
	rgb_t		rgb;
	uint8_t		*dstbuf;
} png_screen_t;

/*
 * png_image_t holds the information relative to the loaded png file.
 * It reflects the png header.
 */
typedef struct _png_img_info {
	uint32_t	width;
	uint32_t	height;
	uint8_t		depth;
	uint8_t		color_type;
	uint8_t		compression_method;
	uint8_t		filter_method;
	uint8_t		interlace_method;
	uint8_t		bpp;
} png_image_t;

extern int png_load_file(char *file, png_screen_t *screen);
extern int png_get_info(char *file, png_image_t *info);
extern char *png_strerror(int error);

#ifdef __cplusplus
}
#endif

#endif /* !_SYS_PNGLIB_H */

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

#ifndef	_SYS_PNGLIB_PRIVATE_H
#define	_SYS_PNGLIB_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/pnglib.h>

/*
 * The five different kinds of color storage in PNG files.
 */
enum {
	PNG_GREYSCALE		= 0,
	PNG_TRUECOLOR		= 2,
	PNG_INDEXED		= 3,
	PNG_GREYSCALE_ALPHA	= 4,
	PNG_TRUECOLOR_ALPHA	= 6
};


enum {
	SMALLER_THAN_SCREEN		= 0,
	LARGER_THAN_SCREEN
};

typedef struct _png_conversion_info {
	uint32_t	offset;
	uint32_t	before_gap;
	uint32_t	after_gap;
	uint8_t		type;
} png_conv_info_t;


typedef struct _png_image {
	png_image_t		image;
	png_screen_t		screen;
	struct {
		png_conv_info_t	width;
		png_conv_info_t	height;
	}			conv_info;
	/* open and read from the filesystem via kobj interfaces. */
	struct _buf		*kobj_handle;
	uint64_t		kobj_offset;
	/*
	 * All the IDAT chunks are merged together in a single compressed buffer
	 * for z_uncompress().
	 */
	char			*gz_image;
	uint64_t		gz_image_offset;
	uint64_t		gz_image_size;
	/* The uncompressed (but yet unfiltered) image gets stored here. */
	uint8_t			*raw_data;
	uint64_t		raw_datalen;
} png_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_PNGLIB_PRIVATE_H */

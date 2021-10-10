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

#ifndef _RGB_H
#define	_RGB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define	RGB_MASK_16(off)	(0xff >> off)

typedef struct _rgb_color_info {
	uint8_t		size;
	uint8_t		shift;
} rgb_color_t;

typedef struct _rgb_info {
	rgb_color_t	red, green, blue;
} rgb_t;

#ifdef __cplusplus
}
#endif

#endif /* !_RGB_H */

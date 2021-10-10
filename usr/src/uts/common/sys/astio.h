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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ASTIO_H
#define	_ASTIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/fbio.h>

#define	ASTIOC			('Y' << 8)

#define	AST_SET_IO_REG		(ASTIOC | 1)
#define	AST_GET_IO_REG		(ASTIOC | 2)
#define	AST_ENABLE_ROM		(ASTIOC | 3)
#define	AST_DISABLE_ROM		(ASTIOC | 4)
#define	AST_DEBUG_VIS_TEST	(ASTIOC | 5)
#define	AST_DEBUG_GET_VIS_BUF	(ASTIOC | 6)
#define	AST_DEBUG_GET_VIS_IMAGE	(ASTIOC | 7)
#define	AST_DEBUG_TEST		(ASTIOC | 8)
#define	AST_GET_STATUS_FLAGS	(ASTIOC | 10)

#define	AST_STATUS_HW_INITIALIZED	0x01


typedef struct {
	uchar_t offset;
	uchar_t value;
} ast_io_reg;

struct ast_vis_cmd_buf {
	int			cmd;
	int			row;
	int			col;
	int			width;
	int			height;
	int			pad0;
	unsigned long		word1;
	unsigned long		word2;
};

#ifdef __cplusplus
}
#endif

#endif /* _ASTIO_H */

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

#ifndef _KERNEL_FP_USE_H
#define	_KERNEL_FP_USE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define	VIS_BLOCKSIZE		64

typedef struct {
	uint64_t	gsr;
	uint32_t	fprs;
	uint8_t		fpregs[5 * VIS_BLOCKSIZE];
} fp_save_t;

#define	SAVE_FP \
	if (save_fp) start_kernel_fp_use(&fp_save_buf);

#define	RESTORE_FP \
	if (save_fp) end_kernel_fp_use(&fp_save_buf);

void start_kernel_fp_use(fp_save_t *fp_save_buf);
void end_kernel_fp_use(fp_save_t *fp_save_buf);

#ifdef	__cplusplus
}
#endif

#endif	/* _KERNEL_FP_USE_H */

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
 * Copyright (c) 1999, 2004, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_IA32_SYS_REG_H
#define	_IA32_SYS_REG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This file only exists for i386 backwards compatibility.
 * Kernel code should not include it.
 */

#ifdef _KERNEL
#error "kernel include of reg.h"
#else
#include <sys/regset.h>
#endif	/* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _IA32_SYS_REG_H */

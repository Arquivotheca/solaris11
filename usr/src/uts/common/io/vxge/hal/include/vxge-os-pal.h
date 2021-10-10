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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :   vxge-os-pal.h
 *
 * Description:  top-level header file. works just like switching between
 *              os-depndent parts
 *
 * Created:       27 December 2006
 */

#ifndef	VXGE_OS_PAL_H
#define	VXGE_OS_PAL_H

__EXTERN_BEGIN_DECLS

/* --------------------------- platform switch ------------------------------ */

/* platform specific header */
#include "vxge_osdep.h"
#define	IN
#define	OUT

#if !defined(VXGE_OS_PLATFORM_64BIT) && !defined(VXGE_OS_PLATFORM_32BIT)
#error "either 32bit or 64bit switch must be defined!"
#endif

#if !defined(VXGE_OS_HOST_BIG_ENDIAN) && !defined(VXGE_OS_HOST_LITTLE_ENDIAN)
#error "either little endian or big endian switch must be defined!"
#endif

#if defined(VXGE_OS_PLATFORM_64BIT)
#define	VXGE_OS_MEMORY_DEADCODE_PAT		0x5a5a5a5a5a5a5a5a
#else
#define	VXGE_OS_MEMORY_DEADCODE_PAT		0x5a5a5a5a
#endif

#ifdef	VXGE_DEBUG_ASSERT

/*
 * vxge_assert
 * @test: C-condition to check
 * @fmt: printf like format string
 *
 * This function implements traditional assert. By default assertions
 * are enabled. It can be disabled by defining VXGE_DEBUG_ASSERT macro in
 * compilation
 * time.
 */
#define	vxge_assert(test) { \
	if (!(test)) vxge_os_bug("bad cond: "#test" at %s:%d\n", \
	__FILE__, __LINE__); }
#else
#define	vxge_assert(test)
#endif /* end of VXGE_DEBUG_ASSERT */

__EXTERN_END_DECLS

#endif /* VXGE_OS_PAL_H */

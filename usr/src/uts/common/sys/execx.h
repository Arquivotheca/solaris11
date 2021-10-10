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

#ifndef _SYS_EXECX_H
#define	_SYS_EXECX_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Flags passed to the execvex() system call (SYS_execve).
 */
#define	EXEC_DESCRIPTOR	0x01
#define	EXEC_RETAINNAME	0x02
#define	EXEC_ARGVNAME	0x04

#if !defined(_KERNEL)
#if defined(__STDC__)
extern int execvex(uintptr_t, char *const *, char *const *, int);
#else
extern int execvex();
#endif
#endif /* !defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_EXECX_H */

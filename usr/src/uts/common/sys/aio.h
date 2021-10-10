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
 * Copyright (c) 1994, 2006, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_AIO_H
#define	_SYS_AIO_H

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct aio_result_t {
	ssize_t aio_return;	/* return value of read or write */
	int aio_errno;		/* errno generated by the IO */
} aio_result_t;

#ifdef	_SYSCALL32
typedef struct aio_result32_t {
	int aio_return;		/* return value of read or write */
	int aio_errno;		/* errno generated by the IO */
} aio_result32_t;
#endif /* _SYSCALL32 */

#if !defined(__XOPEN_OR_POSIX) || defined(__EXTENSIONS__)
#define	AIOREAD		0	/* opcodes for aio calls */
#define	AIOWRITE	1
#define	AIOWAIT		2
#define	AIOCANCEL	3
#define	AIONOTIFY	4
#define	AIOINIT		5
#define	AIOSTART	6
#define	AIOLIO		7
#define	AIOSUSPEND	8
#define	AIOERROR	9
#define	AIOLIOWAIT	10
#define	AIOAREAD	11
#define	AIOAWRITE	12
#define	AIOFSYNC	20
#define	AIOWAITN	21
#define	AIORESERVED1	23	/* reserved for the aio implementation */
#define	AIORESERVED2	24
#define	AIORESERVED3	25
#ifdef _LARGEFILE64_SOURCE
#if	defined(_LP64) && !defined(_KERNEL)
#define	AIOLIO64	AIOLIO
#define	AIOSUSPEND64	AIOSUSPEND
#define	AIOERROR64	AIOERROR
#define	AIOLIOWAIT64	AIOLIOWAIT
#define	AIOAREAD64	AIOAREAD
#define	AIOAWRITE64	AIOAWRITE
#define	AIOCANCEL64	AIOCANCEL
#define	AIOFSYNC64	AIOFSYNC
#else
#define	AIOLIO64	13
#define	AIOSUSPEND64	14
#define	AIOERROR64	15
#define	AIOLIOWAIT64	16
#define	AIOAREAD64	17
#define	AIOAWRITE64	18
#define	AIOCANCEL64	19
#define	AIOFSYNC64	22
#endif /* defined(_LP64) && !defined(_KERNEL) */
#endif /* _LARGEFILE64_SOURCE */
#endif /* !defined(__XOPEN_OR_POSIX) || defined(__EXTENSIONS__) */

#define	AIO_POLL_BIT	0x20	/* opcode filter for AIO_INPROGRESS */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_AIO_H */

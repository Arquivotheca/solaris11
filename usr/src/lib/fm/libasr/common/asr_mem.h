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

#ifndef _ASR_MEM_H
#define	_ASR_MEM_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define	ASR_MEM_MAX_SIZE	(20*1024*1024) /* Limit allocations to 20MB */

extern void *asr_alloc(size_t size);
extern void *asr_zalloc(size_t size);
extern char *asr_strdup(const char *src);
extern void asr_strfree_secure(char *str);

#ifdef	__cplusplus
}
#endif

#endif	/* _ASR_MEM_H */

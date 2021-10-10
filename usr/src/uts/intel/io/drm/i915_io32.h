/* BEGIN CSTYLED */

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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _I915_IO32_H_
#define _I915_IO32_H_

#ifdef _MULTI_DATAMODEL

extern int copyin32_i915_mem_alloc(void *dest, void *src);
extern int copyin32_i915_irq_emit(void *dest, void *src);
extern int copyin32_i915_getparam(void *dest, void *src);
extern int copyin32_i915_cmdbuffer(void * dest, void * src);
extern int copyin32_i915_batchbuffer(void * dest, void * src);

#endif

#endif /* _I915_IO32_H_ */

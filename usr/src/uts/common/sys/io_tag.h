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

#ifndef _SYS_IO_TAG_H
#define	_SYS_IO_TAG_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif


typedef enum {
	IO_TAG_TIMESTAMP = 0,
	IO_TAG_STATE = 1
} io_tag_type_t;

/* opaque IO tag state */
typedef struct io_tag_set *io_tags_t;


void io_tag(io_tags_t *tags, io_tag_type_t type, const char *label,
    uint64_t val);
void io_tag_destroy(io_tags_t *tags);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IO_TAG_H */

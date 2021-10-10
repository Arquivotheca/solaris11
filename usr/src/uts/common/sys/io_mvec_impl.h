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

#ifndef _SYS_IO_MVEC_IMPL_H
#define	_SYS_IO_MVEC_IMPL_H

#include <sys/types.h>
#include <sys/io_mvec.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct io_mvec_bshadow {
	io_mvector_t	*ms_mvector;
	void		*ms_buf;
	size_t		ms_bufsize;
} io_mvec_bshadow_t;


#ifdef	_KERNEL

io_mvector_t *i_io_mvector_clone(io_mvector_t *mvector, offset_t offset,
    size_t len);
void i_io_mvector_set_insync(io_mvector_t *mvector);
boolean_t i_io_mvector_insync(io_mvector_t *mvector);
boolean_t i_io_mvector_is_clone(io_mvector_t *mvector);

int io_mvector_lock(io_mvector_t *mvector);

#endif	/* defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IO_MVEC_IMPL_H */

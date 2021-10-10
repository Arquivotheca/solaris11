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

#ifndef _SYS_SUSPEND_H
#define	_SYS_SUSPEND_H

#ifdef	__cplusplus
extern "C" {
#endif

int suspend_pre(char *error_reason, size_t max_reason_length,
    boolean_t *recovered);
int suspend_start(char *error_reason, size_t max_reason_length);
int suspend_post(char *error_reason, size_t max_reason_length);
void suspend_sync_tick_stick_npt(void);
boolean_t suspend_supported(void);
boolean_t suspend_memdr_allowed(void);
boolean_t suspend_static_cpu_lgroups(void);

#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_SUSPEND_H */

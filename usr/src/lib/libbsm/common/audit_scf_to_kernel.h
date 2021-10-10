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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _AUDIT_SCF_TO_KERNEL_H
#define	_AUDIT_SCF_TO_KERNEL_H

#ifdef	__cplusplus
extern "C" {
#endif

boolean_t scf_to_kernel_flags(char **);
boolean_t scf_to_kernel_naflags(char **);
boolean_t scf_to_kernel_policy(char **);
boolean_t scf_to_kernel_qctrl(char **);

#ifdef	__cplusplus
}
#endif

#endif	/* _AUDIT_SCF_TO_KERNEL_H */

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
 * Copyright (c) 2007, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_FM_IO_DISK_H
#define	_SYS_FM_IO_DISK_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	DISK_ERROR_CLASS		"io.disk"

#define	FM_FAULT_DISK_PREDFAIL		"predictive-failure"
#define	FM_FAULT_DISK_OVERTEMP		"over-temperature"
#define	FM_FAULT_DISK_TESTFAIL		"self-test-failure"

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FM_IO_DISK_H */

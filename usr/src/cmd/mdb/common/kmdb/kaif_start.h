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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _KAIF_START_H
#define	_KAIF_START_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <kmdb/kaif.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int kaif_main_loop(kaif_cpusave_t *);

extern int kaif_master_cpuid;

#define	KAIF_MASTER_CPUID_UNSET		-1

#define	KAIF_CPU_CMD_RESUME		0
#define	KAIF_CPU_CMD_RESUME_MASTER	1
#define	KAIF_CPU_CMD_SWITCH		2

#ifdef __cplusplus
}
#endif

#endif /* _KAIF_START_H */

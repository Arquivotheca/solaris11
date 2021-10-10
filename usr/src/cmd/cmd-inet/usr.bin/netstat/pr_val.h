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
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 */
#ifndef _PR_VAL_H
#define	_PR_VAL_H

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <inet/mib2.h>

extern void prval_init(void);
extern void prval(char *, Counter);
extern void prval64(char *, Counter64);
extern void pr_int_val(char *, int);
extern void pr_sctp_rtoalgo(char *, int);
extern void prval_end(void);

#endif	/* _PR_VAL_H */

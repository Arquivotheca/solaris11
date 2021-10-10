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

#ifndef _INDEX_H
#define	_INDEX_H

#include "indexReader.h"

#define	MAXDIR		200
#define	MAXFILE		200
typedef unsigned int IN_int;
typedef unsigned short int IN_sint;

int makeindex(const char *);
int makesymbindex();
int queryindex(char *, char *, ScoreList **, const char *);
int querysymbindex(char *, ScoreList **, const char *);

#endif /* _INDEX_H */

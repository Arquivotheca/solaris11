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

#include "pr_val.h"

static int odd;

void
prval_init(void)
{
	odd = 0;
}

void
prval(char *str, Counter val)
{
	(void) printf("\t%-20s=%6u", str, val);
	if (odd++ & 1)
		(void) putchar('\n');
}

void
prval64(char *str, Counter64 val)
{
	(void) printf("\t%-20s=%6llu", str, val);
	if (odd++ & 1)
		(void) putchar('\n');
}

void
pr_int_val(char *str, int val)
{
	(void) printf("\t%-20s=%6d", str, val);
	if (odd++ & 1)
		(void) putchar('\n');
}

void
pr_sctp_rtoalgo(char *str, int val)
{
	(void) printf("\t%-20s=", str);
	switch (val) {
		case MIB2_SCTP_RTOALGO_OTHER:
			(void) printf("%6.6s", "other");
			break;

		case MIB2_SCTP_RTOALGO_VANJ:
			(void) printf("%6.6s", "vanj");
			break;

		default:
			(void) printf("%6d", val);
			break;
	}
	if (odd++ & 1)
		(void) putchar('\n');
}

void
prval_end(void)
{
	if (odd++ & 1)
		(void) putchar('\n');
}

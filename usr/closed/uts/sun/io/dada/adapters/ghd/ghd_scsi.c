/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright (c) 1996, Sun Microsystems, Inc.
 * All Rights Reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/byteorder.h>


/*
 * functions to convert between host format and scsi format
 */
void
scsi_htos_3byte(uchar_t *ap, ulong_t nav)
{
	*(ushort_t *)ap = (ushort_t)(((nav & 0xff0000) >> 16) | (nav & 0xff00));
	ap[2] = (uchar_t)nav;
}

void
scsi_htos_long(uchar_t *ap, ulong_t niv)
{
	*(ulong_t *)ap = htonl(niv);
}

void
scsi_htos_short(uchar_t *ap, ushort_t nsv)
{
	*(ushort_t *)ap = htons(nsv);
}

ulong_t
scsi_stoh_3byte(uchar_t *ap)
{
	register ulong_t av = *(ulong_t *)ap;

	return (((av & 0xff) << 16) | (av & 0xff00) | ((av & 0xff0000) >> 16));
}

ulong_t
scsi_stoh_long(ulong_t ai)
{
	return (ntohl(ai));
}

ushort_t
scsi_stoh_short(ushort_t as)
{
	return (ntohs(as));
}

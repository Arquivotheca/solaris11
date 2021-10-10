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

#ifndef	_SYS_DS_SNMP_H_
#define	_SYS_DS_SNMP_H_

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ioctl info for ds_snmp device
 */

#define	DSSNMPIOC		('d' << 24 | 's' << 16 | 'p' << 8)

#define	DSSNMP_GETINFO		(DSSNMPIOC | 1)	/* Get SNMP size */
#define	DSSNMP_CLRLNKRESET	(DSSNMPIOC | 2)	/* Clear link reset flag */

/*
 * DSSNMP_GETINFO
 * Datamodel invariant.
 */
struct dssnmp_info {
	uint64_t size;
	uint64_t token;
};

#ifdef __cplusplus
}
#endif

#endif /* _SYS_DS_SNMP_H_ */

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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SXGE_HW_DEFS_H
#define	_SXGE_HW_DEFS_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sxge_pio.h>
/* #include <sxge_pfc.h> */
#include <sxge_vmac.h>
#include <sxge_intr.h>
#include <sxge_mbx.h>

#define	SXGE_MAX_RDCS	16
#define	SXGE_MAX_TDCS	16
#define	SXGE_MAX_RXGRPS	1
#define	SXGE_MAX_TXGRPS	1

#define	SXGE_EHEADER_VLAN	(sizeof (struct ether_header) + ETHERFCSL)
#define	SXGE_EHEADER_VLAN_CRC	(sizeof (struct ether_header) + ETHERFCSL + 4)

#ifdef	__cplusplus
}
#endif

#endif	/* _SXGE_HW_DEFS_H */

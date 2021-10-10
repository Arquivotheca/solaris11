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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_PCIEX_PCI_INTEL_NB5000_H
#define	_PCIEX_PCI_INTEL_NB5000_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	INTEL_VENDOR_ID	0x8086

#define	INTEL_NB5000_PCIE_DEV_ID(did) (((did) >= 0x3600 && (did) <= 0x360a) || \
	((did) == 0x25d8 || (did) == 0x25d4 || (did) == 0x25c0 || \
	(did) == 0x25d0 || ((did) >= 0x25e2 && (did) <= 0x25e7)) || \
	((did) >= 0x25f7 && (did) <= 0x25fa) || \
	(did) == 0x4000 || (did) == 0x4001 || (did) == 0x4003 || \
	((did) >= 0x4021 && (did) <= 0x402e))

#define	INTEL_NB5100_PCIE_DEV_ID(did) (((did) >= 0x65e2 && (did) <= 0x65e7) || \
	((did) >= 0x65f7 && (did) <= 0x65fa))

#define	INTEL_NB5500_PCIE_DEV_ID(did) (((did) >= 0x3408 && (did) <= 0x3411) || \
	(did) == 0x3420 || (did) == 0x3403)

#define	INTEL_NB5520_PCIE_DEV_ID(did) (((did) >= 0x3408 && (did) <= 0x3411) || \
	(did) == 0x3421 || (did) == 0x3406)

#define	INTEL_SNB_PCIE_DEV_ID(did) ((did) >= 0x3C00 && (did) <= 0x3C0B)

#define	INTEL_ESB2_SW_PCIE_DEV_ID(did) (((did) >= 0x3500 && (did) <= 0x3503) ||\
	((did) >= 0x3510 && (did) <= 0x351B))

#define	INTEL_NB5000_PEXCTRL_OFFSET		0x48
#define	INTEL_NB5000_PEXCTRL_COALESCE_EN	0x400

#ifdef	__cplusplus
}
#endif

#endif	/* _PCIEX_PCI_INTEL_NB5000_H */

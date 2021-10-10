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

#ifndef _SXGE_PIO_H
#define	_SXGE_PIO_H

/*
 * EPS address space.
 * Base addresses for all the EPS accessed registers.
 */
#define	EPS_STD_BASE		0xC0000
#define	EPS_PFC_BASE		0xB0000
#define	EPS_DPM_BASE		0xB1000
#define	EPS_MAC_BASE		0xB2000
#define	EPS_NMI_BASE		0xB3000
#define	EPS_VTX_ARB_BASE	0xB4000
#define	EPS_VNI_CMN_BASE	0xB4400
#define	EPS_TXVNI_CMN_BASE	0xB5000
#define	EPS_RXVNI_CMN_BASE	0xB6000
#define	EPS_MB_CMN_BASE		0xB7000
#define	EPS_VNI_BASE		0x80000
#define	EPS_RXVMAC_BASE		0x82000

/*
 * Host Address space.
 * For all the hosts, PIO_BAR_BASE address should be added to all the
 * base addresses.
 */
#define	SXGE_STD_RES_BASE	0xF0000
#define	SXGE_CMN_RES_BASE	0xC0000	/* MAC, PFC other common resources */
#define	SXGE_HOST_VNI_BASE	0x0000
#define	SXGE_TXDMA_BASE		0x0
#define	SXGE_RXDMA_BASE		0x0400
#define	SXGE_RXVMAC_BASE	0x8000
#define	SXGE_TXVMAC_BASE	0x8200
#define	SXGE_INTR_BASE		0x8400

#define	STAND_RESOURCE_BASE	(SXGE_STD_RES_BASE)

/*
 * RDAT
 */
#define	SXGE_NIU_AVAILABLE			0xFE010
#define	SXGE_NIU_AV_MAX_TRY			10000
#define	SXGE_RDAT_LOW				0xFE000
#define	SXGE_RDAT_HIGH				0xFE008
#define	SXGE_RDAT_VNI_MASK			0xFF
#define	SXGE_RDAT_HIGHT_VNI_MASK		0x0000FFFF
#define	SXGE_MAX_VNI_RXDMA_NUM			0x4
#define	SXGE_MAX_VNI_TXDMA_NUM			0x4
#define	SXGE_MAX_VNI_DMA_NUM			0x4
#define	SXGE_MAX_VNI_VMAC_NUM			0x4
#define	SXGE_MAX_VNI_NUM			0xC
#define	SXGE_MAX_PIOBAR_RESOURCE		0x10
#define	SXGE_PIOBAR_RESOURCE_DMA_MASK		0xF
#define	SXGE_PIOBAR_RESOURCE_DMA_SHIFT		0
#define	SXGE_PIOBAR_RESOURCE_VMAC_MASK		0xF0
#define	SXGE_PIOBAR_RESOURCE_VMAC_SHIFT		4

#endif /* _SXGE_PIO_H */

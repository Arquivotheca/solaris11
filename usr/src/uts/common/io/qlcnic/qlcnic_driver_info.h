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
 * Copyright 2010 QLogic Corporation. All rights reserved.
 */

#ifndef _DRIVER_INFO_H_
#define	_DRIVER_INFO_H_

#ifdef __cplusplus
extern "C" {
#endif

static const qlcnic_brdinfo_t qlcnic_boards[] = {
	{QLCNIC_BRDTYPE_P3_REF_QG, 4, QLCNIC_P3_MN_TYPE_ROMIMAGE,
			"Reference card - Quad Gig "},
	{QLCNIC_BRDTYPE_P3_HMEZ, 2, QLCNIC_P3_CT_TYPE_ROMIMAGE,
			"Dual XGb HMEZ"},
	{QLCNIC_BRDTYPE_P3_10G_CX4_LP, 2, QLCNIC_P3_CT_TYPE_ROMIMAGE,
			"Dual XGb CX4 LP"},
	{QLCNIC_BRDTYPE_P3_4_GB, 4, QLCNIC_P3_CT_TYPE_ROMIMAGE,
			"Quad Gig LP"},
	{QLCNIC_BRDTYPE_P3_IMEZ, 2, QLCNIC_P3_CT_TYPE_ROMIMAGE,
			"Dual XGb IMEZ"},
	{QLCNIC_BRDTYPE_P3_10G_SFP_PLUS, 2, QLCNIC_P3_CT_TYPE_ROMIMAGE,
			"Dual XGb SFP+ LP"},
	{QLCNIC_BRDTYPE_P3_10000_BASE_T, 2, QLCNIC_P3_CT_TYPE_ROMIMAGE,
			"XGB 10G BaseT LP"},
	{QLCNIC_BRDTYPE_P3_XG_LOM, 2, QLCNIC_P3_CT_TYPE_ROMIMAGE,
			"Dual XGb LOM"},
	{QLCNIC_BRDTYPE_P3_4_GB_MM, 4, QLCNIC_P3_CT_TYPE_ROMIMAGE,
			"NX3031 with Gigabit Ethernet"},
	{QLCNIC_BRDTYPE_P3_10G_CX4, 2, QLCNIC_P3_CT_TYPE_ROMIMAGE,
			"Reference card - Dual CX4 Option"},
	{QLCNIC_BRDTYPE_P3_10G_XFP, 1, QLCNIC_P3_CT_TYPE_ROMIMAGE,
			"Reference card - Single XFP Option"},
	{QLCNIC_BRDTYPE_P3_10G_TRP, 2, QLCNIC_P3_CT_TYPE_ROMIMAGE,
			"NX3031 with 1/10 Gigabit Ethernet"},
};

#ifdef __cplusplus
}
#endif

#endif /* !_DRIVER_INFO_H_ */

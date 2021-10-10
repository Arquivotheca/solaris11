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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _NHMEX_CPU_H
#define	_NHMEX_CPU_H

#ifdef __cplusplus
extern "C" {
#endif

#define	FM_EREPORT_PAYLOAD_POISON	"poison"
#define	FM_EREPORT_PAYLOAD_LQPI_PORT_NUM	"left_qpi_port_number"
#define	FM_EREPORT_PAYLOAD_RQPI_PORT_NUM	"right_qpi_port_number"
#define	FM_EREPORT_PAYLOAD_REC_CAP	"cap_support_recovery"
#define	FM_EREPORT_PAYLOAD_SIGNAL_MCE	"signal_mce"
#define	FM_EREPORT_PAYLOAD_ATT_REC	"attention_to_recover"

#define	FM_EREPORT_PAYLOAD_NHMEX_MBANK	"mem_bank"
#define	FM_EREPORT_PAYLOAD_NHMEX_OFFSET	"offset"

#define	FM_EREPORT_PAYLOAD_NHMEX_M0ERR_RNK0	"m0_rnk_0_err_cnt"
#define	FM_EREPORT_PAYLOAD_NHMEX_M0ERR_RNK1	"m0_rnk_1_err_cnt"
#define	FM_EREPORT_PAYLOAD_NHMEX_M0ERR_RNK2	"m0_rnk_2_err_cnt"
#define	FM_EREPORT_PAYLOAD_NHMEX_M0ERR_RNK3	"m0_rnk_3_err_cnt"
#define	FM_EREPORT_PAYLOAD_NHMEX_M1ERR_RNK0	"m1_rnk_0_err_cnt"
#define	FM_EREPORT_PAYLOAD_NHMEX_M1ERR_RNK1	"m1_rnk_1_err_cnt"
#define	FM_EREPORT_PAYLOAD_NHMEX_M1ERR_RNK2	"m1_rnk_2_err_cnt"
#define	FM_EREPORT_PAYLOAD_NHMEX_M1ERR_RNK3	"m1_rnk_3_err_cnt"

#define	INTEL_NHMEX	0x2b6a8086
#define	INTEL_BXB	0x24238086

#define	INTEL_MCA_SW_RECOVERY_PRESENT	0x01000000

/*
 * Macros to extract S bit and AR bit in error code
 */
#define	MCAX86_SCODE(stat)		(((stat) >> 56) & 0x1)
#define	MCAX86_ARCODE(stat)		(((stat) >> 55) & 0x1)

#ifdef __cplusplus
}
#endif

#endif /* _NHMEX_CPU_H */

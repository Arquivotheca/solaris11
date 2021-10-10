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

#ifndef _SXGE_MBX_H
#define	_SXGE_MBX_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Common the the EPS and the Host.
 */
#define	NIU_MB_MAX_LEN			0x7	/* Number of 64-bit words */
#define	NIU_MB_WAITING_PERIOD		1000000 /* 1 Second --??? */
#define	HASH_BITS			2

/*
 * Host Related definitions for the NIU Mailboxes.
 */
#define	NIU_OMB_BASE			(SXGE_STD_RES_BASE + 0x80)
#define	NIU_OMB_ENTRY(entry) \
	(NIU_OMB_BASE + ((entry) << 3))

#define	NIU_MB_STAT_BASE		(SXGE_STD_RES_BASE + 0xC0)
#define	NIU_MB_STAT			(NIU_MB_STAT_BASE)

#define	NIU_MB_MSK_BASE			(SXGE_STD_RES_BASE + 0xC8)
#define	NIU_MB_MSK			(NIU_MB_MSK_BASE)

#define	NIU_IMB_BASE			(SXGE_STD_RES_BASE + 0xD0)
#define	NIU_IMB_ENTRY(entry)		(NIU_IMB_BASE + ((entry) << 3))

#define	NIU_IMB_ACK_BASE		(SXGE_STD_RES_BASE + 0x110)
#define	NIU_IMB_ACK			(NIU_IMB_ACK_BASE)

#define	NIU_MB_STAT_FORCE_BASE		(SXGE_STD_RES_BASE + 0x118)
#define	NIU_MB_STAT_FORCE		(NIU_MB_STAT_FORCE_BASE)

/*
 * Outbound Mailbox Register.
 */
typedef uint64_t	niu_omb_reg_t;

/*
 * NIU MB Status Register.
 */
typedef union _niu_mb_stat {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t	rsrvd_h:32;
		uint32_t	rsrvd:23;
		uint32_t	omb_ecc_err:1;
		uint32_t	imb_ecc_err:1;
		uint32_t	func_rst:1;
		uint32_t	func_rst_done:1;
		uint32_t	omb_ovl:1;
		uint32_t	imb_full:1;
		uint32_t	omb_acked:1;
		uint32_t	omb_failed:1;
		uint32_t	omb_full:1;
#else
		uint32_t	omb_full:1;
		uint32_t	omb_failed:1;
		uint32_t	omb_acked:1;
		uint32_t	imb_full:1;
		uint32_t	omb_ovl:1;
		uint32_t	func_rst_done:1;
		uint32_t	func_rst:1;
		uint32_t	imb_ecc_err:1;
		uint32_t	omb_ecc_err:1;
		uint32_t	rsrvd:23;
		uint32_t	rsrvd_h:32;
#endif
	} bits;
} niu_mb_status_t;

/*
 * NIU MB Mask Register
 */
typedef union _niu_mb_int_msk {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t	rsrvd4:32;
		uint32_t	rsrvd3:23;
		uint32_t	omb_err_ecc_msk:1;
		uint32_t	imb_err_ecc_msk:1;
		uint32_t	rsrvd2:1;
		uint32_t	func_rst_done_msk:1;
		uint32_t	omb_ovl_msk:1;
		uint32_t	imb_full_msk:1;
		uint32_t	omb_acked_msk:1;
		uint32_t	omb_failed_msk:1;
		uint32_t	rsrvd1:1;
#else
		uint32_t	rsrvd1:1;
		uint32_t	omb_failed_msk:1;
		uint32_t	omb_acked_msk:1;
		uint32_t	imb_full_msk:1;
		uint32_t	omb_ovl_msk:1;
		uint32_t	func_rst_done_msk:1;
		uint32_t	rsrvd2:1;
		uint32_t	imb_err_ecc_msk:1;
		uint32_t	omb_err_ecc_msk:1;
		uint32_t	rsrvd3:23;
		uint32_t	rsrvd4:32;
#endif
	} bits;
} niu_mb_int_msk_t;

/*
 * NIU Inbound MB Register.
 */
typedef uint64_t	niu_imb_reg_t;

/*
 * NIU IMB Ack Register.
 */
typedef union _niu_imb_ack {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t	rsrvd:32;
		uint32_t	rsrvd_l:30;
		uint32_t	imb_nack:1;
		uint32_t	imb_ack:1;
#else
		uint32_t	imb_ack:1;
		uint32_t	imb_nack:1;
		uint32_t	rsrvd_l:30;
		uint32_t	rsrvd:32;
#endif
	} bits;
} niu_imb_ack_t;

/*
 * NIU MB Status Force Register.
 */
typedef union _niu_imb_dbg {
	uint64_t value;
	struct {
#if defined(_BIG_ENDIAN)
		uint32_t	rsrvd:32;
		uint32_t	rsrvd_l:23;
		uint32_t	omb_ecc_err_force:1;
		uint32_t	imb_ecc_err_force:1;
		uint32_t	rsrvd1:2;
		uint32_t	omb_ovl_force:1;
		uint32_t	imb_full_force:1;
		uint32_t	omb_acked_force:1;
		uint32_t	omb_failed_force:1;
		uint32_t	omb_full_force:1;
#else
		uint32_t	omb_full_force:1;
		uint32_t	omb_failed_force:1;
		uint32_t	omb_acked_force:1;
		uint32_t	imb_full_force:1;
		uint32_t	omb_ovl_force:1;
		uint32_t	rsrvd1:2;
		uint32_t	imb_ecc_err_force:1;
		uint32_t	omb_ecc_err_force:1;
		uint32_t	rsrvd_l:23;
		uint32_t	rsrvd:32;
#endif
	} bits;
} niu_imb_dbg_t;

#ifdef __cplusplus
}
#endif

#endif /* _SXGE_MBX_H */

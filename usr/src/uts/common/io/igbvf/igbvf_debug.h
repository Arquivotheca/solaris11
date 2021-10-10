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
 * Copyright(c) 2007-2010 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_IGBVF_DEBUG_H
#define	_IGBVF_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif


#ifdef DEBUG
#define	IGBVF_DEBUG
#endif

#ifdef IGBVF_DEBUG

#define	IGBVF_DEBUGLOG_0(adapter, fmt)	\
	igbvf_log((adapter), (fmt))
#define	IGBVF_DEBUGLOG_1(adapter, fmt, d1)	\
	igbvf_log((adapter), (fmt), (d1))
#define	IGBVF_DEBUGLOG_2(adapter, fmt, d1, d2)	\
	igbvf_log((adapter), (fmt), (d1), (d2))
#define	IGBVF_DEBUGLOG_3(adapter, fmt, d1, d2, d3)	\
	igbvf_log((adapter), (fmt), (d1), (d2), (d3))

#define	IGBVF_DEBUG_STAT_COND(val, cond)	if (cond) (val)++
#define	IGBVF_DEBUG_STAT(val)			(val)++

#else

#define	IGBVF_DEBUGLOG_0(adapter, fmt)
#define	IGBVF_DEBUGLOG_1(adapter, fmt, d1)
#define	IGBVF_DEBUGLOG_2(adapter, fmt, d1, d2)
#define	IGBVF_DEBUGLOG_3(adapter, fmt, d1, d2, d3)

#define	IGBVF_DEBUG_STAT_COND(val, cond)
#define	IGBVF_DEBUG_STAT(val)

#endif	/* IGBVF_DEBUG */

#define	IGBVF_STAT(val)		(val)++

#ifdef IGBVF_DEBUG
void igbvf_write_reg(void *, uint32_t, uint32_t);
uint32_t igbvf_read_reg(void *, uint32_t);
void igbvf_write_reg_array(void *, uint32_t, uint32_t, uint32_t);
uint32_t igbvf_read_reg_array(void *, uint32_t, uint32_t);
#endif

void igbvf_log(void *, const char *, ...);

#ifdef __cplusplus
}
#endif

#endif	/* _IGBVF_DEBUG_H */

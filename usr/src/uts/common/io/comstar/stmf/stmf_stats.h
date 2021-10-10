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
#ifndef _STMF_STATS_H
#define	_STMF_STATS_H

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct stmf_kstat_lu_info {
	kstat_named_t		i_lun_guid;
	kstat_named_t		i_lun_alias;
} stmf_kstat_lu_info_t;

typedef struct stmf_kstat_tgt_info {
	kstat_named_t		i_tgt_name;
	kstat_named_t		i_tgt_alias;
	kstat_named_t		i_protocol;
} stmf_kstat_tgt_info_t;

#ifdef	__cplusplus
}
#endif

#endif /* _STMF_STATS_H */

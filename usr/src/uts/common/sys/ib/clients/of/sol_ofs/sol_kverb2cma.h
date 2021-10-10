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

#ifndef _SYS_IB_CLIENTS_OF_SOL_OFS_SOL_KVERB2CMA_H
#define	_SYS_IB_CLIENTS_OF_SOL_OFS_SOL_KVERB2CMA_H

#ifdef __cplusplus
extern "C" {
#endif

#define	KVERBS_QP_DISCONNECT	0x00
#define	KVERBS_QP_CONNECTED	0x01
#define	KVERBS_QP_FREE_CALLED	0x04

void kverbs_map_qp_to_cmid(struct ib_qp *, void *);
void kverbs_notify_qp_connect_state(struct ib_qp *, int);
int kverbs_get_qp_connect_state(struct ib_qp *);
int kverbs_set_free_state_for_connected(struct ib_qp *);
void *kverbs_device2ibt_hdl(void *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IB_CLIENTS_OF_SOL_OFS_SOL_KVERB2CMA_H */

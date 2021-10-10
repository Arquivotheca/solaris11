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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :   vxgehal-lro.h
 *
 * Description:  LRO offload related include
 *
 * Created:       11 January 2006
 */

#ifndef	VXGE_HAL_LRO_H
#define	VXGE_HAL_LRO_H

__EXTERN_BEGIN_DECLS


#if defined(VXGE_HAL_USE_SW_LRO)

#define	VXGE_HAL_TS_SAVE	2
#define	VXGE_HAL_TS_VERIFY	1
#define	VXGE_HAL_TS_UPDATE	0


#define	VXGE_HAL_SW_LRO_SESSION_SIZE		(sizeof (vxge_hal_sw_lro_t)+\
						    sizeof (vxge_list_t))

#define	VXGE_HAL_SW_LRO_SESSION_ITEM(lro) ((lro == NULL) ? NULL : ((lro) + 1))

#define	VXGE_HAL_SW_LRO_SESSION(item) \
		    ((item == NULL) ? NULL : (((vxge_hal_sw_lro_t *)item)-1))
#define	VXGE_HAL_INET_ECN_MASK		3
#define	VXGE_HAL_INET_ECN_CE		3

vxge_hal_status_e
__vxge_hal_sw_lro_init(__vxge_hal_ring_t *ring);

vxge_hal_status_e
__vxge_hal_sw_lro_reset(__vxge_hal_ring_t *ring);

vxge_hal_status_e
__vxge_hal_sw_lro_terminate(__vxge_hal_ring_t *ring);

vxge_hal_status_e
__vxge_hal_lro_capable(__vxge_hal_virtualpath_t *vpath,
		    u8 *buffer,
		    vxge_hal_ip_hdr_t **ip,
		    vxge_hal_tcp_hdr_t **tcp,
		    vxge_hal_ring_rxd_info_t *ext_info);

#endif

__EXTERN_END_DECLS

#endif /* VXGE_HAL_LRO_H */

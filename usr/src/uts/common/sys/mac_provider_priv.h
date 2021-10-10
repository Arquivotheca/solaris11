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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */
#ifndef	_SYS_MAC_PROVIDER_PRIV_H
#define	_SYS_MAC_PROVIDER_PRIV_H

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stream.h>
#include <sys/mkdev.h>
#include <sys/mac.h>

/*
 * MAC Provider Private Interface
 */

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * MAC Block Flags -- used mac_block_alloc()
 */
#define	MAC_BM_BLOCK_PACKET		0x0001	/* Memory is for packets */
#define	MAC_BM_BLOCK_DESCRIPTOR		0x0002	/* Memory is for descriptors */

/*
 * MAC Block Memory Interface.
 */
extern mac_block_handle_t 	mac_block_alloc(mac_handle_t mh,
				    mac_ring_handle_t mrh,
				    uint64_t flags,
				    size_t length);

extern void			mac_block_free(mac_block_handle_t mbh);

extern size_t			mac_block_length_get(mac_block_handle_t mbh);

extern caddr_t			mac_block_kaddr_get(mac_block_handle_t mbh);

extern uint64_t			mac_block_ioaddr_get(mac_block_handle_t mbh);

extern ddi_dma_cookie_t		mac_block_dma_cookie_get(
				    mac_block_handle_t mbh);

extern ddi_dma_handle_t		mac_block_dma_handle_get(
				    mac_block_handle_t mbh);

extern ddi_acc_handle_t		mac_block_acc_handle_get(
				    mac_block_handle_t mbh);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_MAC_PROVIDER_PRIV_H */

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

#ifndef _SYS_IB_IBTL_IMPL_IBC_NODEDESC_H
#define	_SYS_IB_IBTL_IMPL_IBC_NODEDESC_H

/*
 * ibc_nodedesc.h
 *
 * All data structures and function prototypes that are private to the
 * nodedescriptor persistent storage implementation.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	IBC_NODE_DESC		"IBC_NODE_DESCRIPTOR"
#define	IBC_NODE_DESC_HCA_STR	"IBC_HCA_NODE_DESCRIPTION"
#define	IBC_NODE_DESC_HCA_INFO	"IBC_HCA_NODE_INFO"
#define	IBC_NODE_DESC_LIST	"IBC_NODE_DESCRIPTOR_LIST"

extern void	ibc_impl_devcache_init();
extern void	ibc_impl_devcache_fini();

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IB_IBTL_IMPL_IBC_NODEDESC_H */

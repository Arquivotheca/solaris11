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

#ifndef	_SYS_MAC_GVRP_IMPL_H
#define	_SYS_MAC_GVRP_IMPL_H

#include <sys/mac.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

#define	MAC_GVRP_TIMEOUT_DEFAULT    250 /* (ms) */

typedef struct mac_gvrp_vid_list mac_gvrp_vid_list_t;
typedef struct mac_gvrp_active_link mac_gvrp_active_link_t;

extern void mac_vlan_announce_init();
extern void mac_vlan_announce_fini();
extern int mac_vlan_announce_register(mac_handle_t *mh, uint16_t vid);
extern int mac_vlan_announce_deregister(mac_handle_t *mh, uint16_t vid);

extern void mac_gvrp_enable(mac_handle_t mh);
extern void mac_gvrp_disable(mac_handle_t mh);
extern int mac_gvrp_set_timeout(mac_handle_t mh, uint32_t to);
extern uint32_t mac_gvrp_get_timeout_min();
extern uint32_t mac_gvrp_get_timeout_max();

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_MAC_GVRP_IMPL_H */

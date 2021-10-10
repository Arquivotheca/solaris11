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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */
#ifndef	_FCOE_ETH_H_
#define	_FCOE_ETH_H_

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

extern int fcoe_open_mac(fcoe_mac_t *, int, fcoeio_stat_t *);
extern int fcoe_close_mac(fcoe_mac_t *);
extern int fcoe_enable_callback(fcoe_mac_t *);
extern int fcoe_disable_callback(fcoe_mac_t *);
extern int fcoe_mac_set_address(fcoe_port_t *, uint8_t *, boolean_t);
extern void fcoe_free_fip(fcoe_fip_frm_t *, uint8_t);
extern fcoe_fip_frm_t *fcoe_allocate_fip(fcoe_mac_t *, uint32_t, void *);
extern fcoe_fip_frm_t *fcoe_initiate_fip_req(fcoe_mac_t *, uint32_t);
extern void fcoe_process_fip_resp(fcoe_fip_frm_t *);
extern void fcoe_mac_notify(void *, mac_notify_type_t);
extern boolean_t fcoe_select_fcf(fcoe_mac_t *);
extern int fcoe_mac_set_address(fcoe_port_t *, uint8_t *, boolean_t);

#endif	/* _KERNEL */
#ifdef	__cplusplus
}
#endif

#endif	/* _FCOE_ETH_H_ */

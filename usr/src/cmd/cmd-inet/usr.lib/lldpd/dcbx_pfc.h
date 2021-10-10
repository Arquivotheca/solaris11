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

#ifndef _DCBX_PFC_H
#define	_DCBX_PFC_H

#include "dcbx_impl.h"

extern int	dcbx_pfc_tlv_init(lldp_agent_t *, nvlist_t *);
extern void	dcbx_pfc_tlv_fini(lldp_agent_t *);
extern int	lldpd_link2pfcparam(datalink_id_t, lldp_pfc_t *);

#ifdef __cplusplus
}
#endif

#endif /* _DCBX_PFC_H */

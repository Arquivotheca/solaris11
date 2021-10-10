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

#ifndef	_IOVCFG_4V_H
#define	_IOVCFG_4V_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/mdeg.h>

typedef struct iov_vf_plat	iov_vf_plat_t;

struct iov_vf_plat {
	/* MDEG callback info */
	mdeg_handle_t		ivp_mdeg_hdl;	/* MDEG handle */
	mdeg_node_spec_t	*ivp_nspecp;	/* MDEG node spec */
};

int iovcfg_mdeg_register(iov_vf_t *, mdeg_cb_t);
void iovcfg_mdeg_unreg(iov_vf_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _IOVCFG_4V_H */

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
 *  Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MPMUL_VT_H
#define	_MPMUL_VT_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>

#ifdef _KERNEL
#include <sys/ddi.h>
#include <sys/mdesc.h>
#include <sys/crypto/common.h>

#include <sys/kmem.h>
#include <sys/param.h>
#include <sys/sunddi.h>

#else

#include <stddef.h>

#endif

typedef void (mpm_yf_func_t)(void);

typedef struct
{
	void	*load_m1_func;
	void	*load_m2_func;
	void	*store_res_func;
	void	*mpmul_func;
}	mpm_yf_funcs_t;

extern mpm_yf_funcs_t mpm_yf_functions_table[];

typedef struct mpm_yf_pars mpm_yf_pars_t;
struct mpm_yf_pars
{
	uint64_t	*m1;
	uint64_t	*m2;
	uint64_t	*res;
	mpm_yf_funcs_t	functions;
};

extern int mpm_yf_mpmul(mpm_yf_pars_t *params);

#define	MPM_YF_PARS_M1_OFFS	(offsetof(mpm_yf_pars_t, m1))
#define	MPM_YF_PARS_M2_OFFS	(offsetof(mpm_yf_pars_t, m2))
#define	MPM_YF_PARS_RES_OFFS	(offsetof(mpm_yf_pars_t, res))
#define	MPM_YF_PARS_LOAD_M1_OFFS	(offsetof(mpm_yf_pars_t, functions) + \
	offsetof(mpm_yf_funcs_t, load_m1_func))
#define	MPM_YF_PARS_LOAD_M2_OFFS	(offsetof(mpm_yf_pars_t, functions) + \
	offsetof(mpm_yf_funcs_t, load_m2_func))
#define	MPM_YF_PARS_STORE_RES_OFFS	(offsetof(mpm_yf_pars_t, functions) + \
	offsetof(mpm_yf_funcs_t, store_res_func))
#define	MPM_YF_PARS_MPMUL_OFFS	(offsetof(mpm_yf_pars_t, functions) + \
	offsetof(mpm_yf_funcs_t, mpmul_func))

#ifdef	__cplusplus
}
#endif

#endif	/* _MPMUL_VT_H */

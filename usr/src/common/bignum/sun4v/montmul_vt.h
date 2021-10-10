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

#ifndef _MONTMUL_VT_H
#define	_MONTMUL_VT_H

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

typedef void (mm_yf_func_t)(void);

typedef struct
{
	void	*load_a_func;
	void	*store_a_func;
	void	*load_n_func;
	void	*load_b_func;
	void	*montmul_func;
	void	*montsqr_func;
}	mm_yf_funcs_t;

extern mm_yf_funcs_t mm_yf_functions_table[];

typedef struct mm_yf_pars mm_yf_pars_t;
struct mm_yf_pars
{
	uint64_t	*a;
	uint64_t	*b;
	uint64_t	*n;
	uint64_t	*ret;
	uint64_t	*nprime;
	mm_yf_funcs_t	functions;
};

extern int mm_yf_montmul(mm_yf_pars_t *params);
extern int mm_yf_montsqr(mm_yf_pars_t *params);
extern int mm_yf_execute_slp(void *slp);
extern int mm_yf_ret_from_mont_func(void);
extern int mm_yf_restore_func(void);

#define	MM_YF_PARS_A_OFFS	(offsetof(mm_yf_pars_t, a))
#define	MM_YF_PARS_B_OFFS	(offsetof(mm_yf_pars_t, b))
#define	MM_YF_PARS_N_OFFS	(offsetof(mm_yf_pars_t, n))
#define	MM_YF_PARS_RET_OFFS	(offsetof(mm_yf_pars_t, ret))
#define	MM_YF_PARS_NPRIME_OFFS	(offsetof(mm_yf_pars_t, nprime))
#define	MM_YF_PARS_LOAD_A_OFFS	(offsetof(mm_yf_pars_t, functions) + \
	offsetof(mm_yf_funcs_t, load_a_func))
#define	MM_YF_PARS_STORE_A_OFFS	(offsetof(mm_yf_pars_t, functions) + \
	offsetof(mm_yf_funcs_t, store_a_func))
#define	MM_YF_PARS_LOAD_N_OFFS	(offsetof(mm_yf_pars_t, functions) + \
	offsetof(mm_yf_funcs_t, load_n_func))
#define	MM_YF_PARS_LOAD_B_OFFS	(offsetof(mm_yf_pars_t, functions) + \
	offsetof(mm_yf_funcs_t, load_b_func))
#define	MM_YF_PARS_MONTMUL_OFFS	(offsetof(mm_yf_pars_t, functions) + \
	offsetof(mm_yf_funcs_t, montmul_func))
#define	MM_YF_PARS_MONTSQR_OFFS	(offsetof(mm_yf_pars_t, functions) + \
	offsetof(mm_yf_funcs_t, montsqr_func))

#ifdef	__cplusplus
}
#endif

#endif	/* _MONTMUL_VT_H */

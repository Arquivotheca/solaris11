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
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_MCXNEX_TYPEDEF_H
#define	_MCXNEX_TYPEDEF_H

/*
 * mcxnex_typedef.h
 *    Contains all the common typedefs used throughout the Mcxnex driver.
 *    Because the mcxnex.h header file (which all source files include) brings
 *    this header file in first (before any of the other Mcxnex header files),
 *    the typedefs defined here can be used throughout the source and header
 *    files in the rest of the driver.
 */

typedef	struct mcxnex_state_s		mcxnex_state_t;
typedef	struct mcxnex_qalloc_info_s	mcxnex_qalloc_info_t;
typedef struct mcxnex_rsrc_pool_info_s	mcxnex_rsrc_pool_info_t;
typedef	struct mcxnex_rsrc_s		mcxnex_rsrc_t;
typedef struct mcxnex_workq_avl_s	mcxnex_workq_avl_t;
typedef struct mcxnex_workq_hdr_s	mcxnex_workq_hdr_t;
typedef struct mcxnex_icm_table_s	mcxnex_icm_table_t;
typedef	struct mcxnex_dma_info_s	mcxnex_dma_info_t;
typedef struct mcxnex_hw_vpm_s 		mcxnex_hw_vpm_t;
typedef struct mcxnex_hw_hcr_s		mcxnex_hw_hcr_t;
typedef struct mcxnex_hw_querydevlim_s	mcxnex_hw_querydevlim_t;
typedef struct mcxnex_hw_query_port_s	mcxnex_hw_query_port_t;
typedef struct mcxnex_hw_set_port_s	mcxnex_hw_set_port_t;
typedef struct mcxnex_hw_queryfw_s	mcxnex_hw_queryfw_t;
typedef struct mcxnex_hw_queryadapter_s		mcxnex_hw_queryadapter_t;
typedef struct mcxnex_hw_initqueryhca_s		mcxnex_hw_initqueryhca_t;
typedef struct mcxnex_hw_dmpt_s		mcxnex_hw_dmpt_t;
typedef struct mcxnex_hw_cmpt_s		mcxnex_hw_cmpt_t;
typedef struct mcxnex_hw_mtt_s		mcxnex_hw_mtt_t;
typedef struct mcxnex_hw_eqc_s		mcxnex_hw_eqc_t;
typedef struct mcxnex_hw_eqe_s		mcxnex_hw_eqe_t;
typedef struct mcxnex_hw_cqc_s		mcxnex_hw_cqc_t;
typedef struct mcxnex_hw_srqc_s		mcxnex_hw_srqc_t;
typedef struct mcxnex_hw_uar_s		mcxnex_hw_uar_t;
typedef struct mcxnex_hw_cqe_s		mcxnex_hw_cqe_t;
typedef struct mcxnex_hw_addr_path_s	mcxnex_hw_addr_path_t;
typedef	struct mcxnex_hw_mod_stat_cfg_s		mcxnex_hw_mod_stat_cfg_t;
typedef struct mcxnex_hw_udav_s		mcxnex_hw_udav_t;
typedef struct mcxnex_hw_qpc_s		mcxnex_hw_qpc_t;
typedef struct mcxnex_hw_mcg_s		mcxnex_hw_mcg_t;
typedef struct mcxnex_hw_mcg_qp_list_s	mcxnex_hw_mcg_qp_list_t;
typedef struct mcxnex_hw_sm_perfcntr_s	mcxnex_hw_sm_perfcntr_t;
typedef struct mcxnex_hw_sm_extperfcntr_s	mcxnex_hw_sm_extperfcntr_t;
typedef struct mcxnex_hw_snd_wqe_ctrl_s		mcxnex_hw_snd_wqe_ctrl_t;
typedef struct mcxnex_hw_srq_wqe_next_s		mcxnex_hw_srq_wqe_next_t;
typedef struct mcxnex_hw_snd_wqe_ud_s		mcxnex_hw_snd_wqe_ud_t;
typedef struct mcxnex_hw_snd_wqe_bind_s		mcxnex_hw_snd_wqe_bind_t;
typedef struct mcxnex_hw_snd_wqe_remaddr_s	mcxnex_hw_snd_wqe_remaddr_t;
typedef struct mcxnex_hw_snd_wqe_atomic_s	mcxnex_hw_snd_wqe_atomic_t;
typedef struct mcxnex_hw_mlx_wqe_nextctrl_s	mcxnex_hw_mlx_wqe_nextctrl_t;
typedef struct mcxnex_hw_wqe_sgl_s	mcxnex_hw_wqe_sgl_t;

typedef struct mcxnex_sw_mr_s		*mcxnex_mrhdl_t;
typedef struct mcxnex_sw_mr_s		*mcxnex_mwhdl_t;
typedef struct mcxnex_sw_pd_s		*mcxnex_pdhdl_t;
typedef struct mcxnex_sw_eq_s		*mcxnex_eqhdl_t;
typedef struct mcxnex_sw_cq_s		*mcxnex_cqhdl_t;
typedef struct mcxnex_sw_srq_s		*mcxnex_srqhdl_t;
typedef struct mcxnex_sw_fmr_s		*mcxnex_fmrhdl_t;
typedef struct mcxnex_sw_ah_s		*mcxnex_ahhdl_t;
typedef struct mcxnex_sw_qp_s		*mcxnex_qphdl_t;
typedef struct mcxnex_sw_mcg_list_s	*mcxnex_mcghdl_t;

#endif	/* _MCXNEX_TYPEDEF_H */

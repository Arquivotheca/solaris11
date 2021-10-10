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

/*
 * This file contains preset event names from the Performance Application
 * Programming Interface v3.5 which included the following notice:
 *
 *                             Copyright (c) 2005,6
 *                           Innovative Computing Labs
 *                         Computer Science Department,
 *                            University of Tennessee,
 *                                 Knoxville, TN.
 *                              All Rights Reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the University of Tennessee nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * This open source software license conforms to the BSD License template.
 */

#ifndef _SYS_NHM_PCBE_H
#define	_SYS_NHM_PCBE_H

#include "fam6_pcbe.h"
#include "wm_pcbe.h"

#ifdef __cplusplus
extern "C" {
#endif

#define	GENERICEVENTS_FAM6_NHM						       \
{ 0x0B, 0x01, C0|C1|C2|C3, "PAPI_ld_ins" },  /* mem_inst_retired.loads	    */ \
{ 0x0B, 0x02, C0|C1|C2|C3, "PAPI_sr_ins" },  /* mem_inst_retired.stores	    */ \
				/* mem_inst_retired.loads and stores	    */ \
{ 0x17, 0x01, C0|C1|C2|C3, "PAPI_tot_iis" },  /* inst_queue_writes	    */ \
{ 0x24, 0x02, C0|C1|C2|C3, "PAPI_l2_ldm" },  /* l2_rqsts.ld_miss	    */ \
{ 0x24, 0x03, C0|C1|C2|C3, "PAPI_l1_dcm" },  /* l2_rqsts. loads and rfos    */ \
{ 0x24, 0x08, C0|C1|C2|C3, "PAPI_l2_stm" },  /* l2_rqsts.rfo_miss	    */ \
{ 0x24, 0x0C, C0|C1|C2|C3, "PAPI_l2_tcw" },  /* l2_rqsts.rfos		    */ \
			/* l2_rqsts. ld_miss, rfo_miss and ifetch_miss */      \
{ 0x24, 0xFF, C0|C1|C2|C3, "PAPI_l1_tcm" },  /* l2_rqsts.references	    */ \
				/* l2_rqsts. loads, rfos and ifetches */       \
{ 0x26, 0xF0, C0|C1|C2|C3, "PAPI_prf_dm" },  /* l2_data_rqsts.prefetch.mesi */ \
{ 0x2E, 0x41, C0|C1|C2|C3, "PAPI_l3_tcm" },  /* l3_lat_cache.misses	    */ \
{ 0x2E, 0x4F, C0|C1|C2|C3, "PAPI_l3_tca" },  /* l3_lat_cache.reference	    */ \
{ 0x49, 0x01, C0|C1|C2|C3, "PAPI_tlb_dm" },  /* dtlb_misses.any		    */ \
{ 0x80, 0x01, C0|C1|C2|C3, "PAPI_l1_ich" },  /* l1i.hits		    */ \
{ 0x80, 0x02, C0|C1|C2|C3, "PAPI_l1_icm" },  /* l1i.misses		    */ \
{ 0x80, 0x03, C0|C1|C2|C3, "PAPI_l1_ica" },  /* l1i.reads		    */ \
{ 0x80, 0x03, C0|C1|C2|C3, "PAPI_l1_icr" },  /* l1i.reads		    */ \
{ 0x85, 0x01, C0|C1|C2|C3, "PAPI_tlb_im" },  /* itlb_misses.any		    */ \
{ 0xC4, 0x01, C0|C1|C2|C3, "PAPI_br_cn" }   /* br_inst_retired.conditional */

#define	EVENTS_FAM6_NHM							\
									\
{ 0x04, 0x07, C0|C1|C2|C3, "sb_drain.any" },				\
{ 0x06, 0x04, C0|C1|C2|C3, "store_blocks.at_ret" },			\
{ 0x06, 0x08, C0|C1|C2|C3, "store_blocks.l1d_block" },			\
{ 0x07, 0x01, C0|C1|C2|C3, "partial_address_alias" },			\
									\
{ 0x08, 0x01, C0|C1|C2|C3, "dtlb_load_misses.any" },			\
{ 0x08, 0x02, C0|C1|C2|C3, "dtlb_load_misses.walk_completed" },		\
{ 0x08, 0x10, C0|C1|C2|C3, "dtlb_load_misses.stlb_hit" },		\
{ 0x08, 0x20, C0|C1|C2|C3, "dtlb_load_misses.pde_miss" },		\
{ 0x08, 0x80, C0|C1|C2|C3, "dtlb_load_misses.large_walk_completed" },	\
									\
{ 0x0B, 0x01, C0|C1|C2|C3, "mem_inst_retired.loads" },			\
{ 0x0B, 0x02, C0|C1|C2|C3, "mem_inst_retired.stores" },			\
{ 0x0C, 0x01, C0|C1|C2|C3, "mem_store_retired.dtlb_miss" },		\
{ 0x0E, 0x01, C0|C1|C2|C3, "uops_issued.any" },				\
{ 0x0E, 0x02, C0|C1|C2|C3, "uops_issued.fused" },			\
									\
{ 0x0F, 0x01, C0|C1|C2|C3, "mem_uncore_retired.llc_data_miss_unknown" }, \
{ 0x0F, 0x80, C0|C1|C2|C3, "mem_uncore_retired.uncacheable" },		\
									\
{ 0x10, 0x01, C0|C1|C2|C3, "fp_comp_ops_exe.x87" },			\
{ 0x10, 0x02, C0|C1|C2|C3, "fp_comp_ops_exe.mmx" },			\
{ 0x10, 0x04, C0|C1|C2|C3, "fp_comp_ops_exe.sse_fp" },			\
{ 0x10, 0x08, C0|C1|C2|C3, "fp_comp_ops_exe.sse2_integer" },		\
{ 0x10, 0x10, C0|C1|C2|C3, "fp_comp_ops_exe.sse_fp_packed" },		\
{ 0x10, 0x20, C0|C1|C2|C3, "fp_comp_ops_exe.sse_fp_scalar" },		\
{ 0x10, 0x40, C0|C1|C2|C3, "fp_comp_ops_exe.sse_single_precision" },	\
{ 0x10, 0x80, C0|C1|C2|C3, "fp_comp_ops_exe.sse_double_precision" },	\
									\
{ 0x12, 0x01, C0|C1|C2|C3, "simd_int_128.packed_mpy" },			\
{ 0x12, 0x02, C0|C1|C2|C3, "simd_int_128.packed_shift" },		\
{ 0x12, 0x04, C0|C1|C2|C3, "simd_int_128.pack" },			\
{ 0x12, 0x08, C0|C1|C2|C3, "simd_int_128.unpack" },			\
{ 0x12, 0x10, C0|C1|C2|C3, "simd_int_128.packed_logical" },		\
{ 0x12, 0x20, C0|C1|C2|C3, "simd_int_128.packed_arith" },		\
{ 0x12, 0x40, C0|C1|C2|C3, "simd_int_128.shuffle_move" },		\
									\
{ 0x13, 0x01, C0|C1|C2|C3, "load_dispatch.rs" },			\
{ 0x13, 0x02, C0|C1|C2|C3, "load_dispatch.rs_delayed" },		\
{ 0x13, 0x04, C0|C1|C2|C3, "load_dispatch.mob" },			\
{ 0x13, 0x07, C0|C1|C2|C3, "load_dispatch.any" },			\
									\
{ 0x14, 0x01, C0|C1|C2|C3, "arith.cycles_div_busy" },			\
{ 0x14, 0x02, C0|C1|C2|C3, "arith.mul" },				\
									\
{ 0x17, 0x01, C0|C1|C2|C3, "inst_queue_writes" },			\
{ 0x18, 0x01, C0|C1|C2|C3, "inst_decoded.dec0" },			\
{ 0x19, 0x01, C0|C1|C2|C3, "two_uop_insts_decoded" },			\
{ 0x1E, 0x01, C0|C1|C2|C3, "inst_queue_write_cycles" },			\
{ 0x20, 0x01, C0|C1|C2|C3, "lsd_overflow" },				\
									\
{ 0x24, 0x01, C0|C1|C2|C3, "l2_rqsts.ld_hit" },				\
{ 0x24, 0x02, C0|C1|C2|C3, "l2_rqsts.ld_miss" },			\
{ 0x24, 0x03, C0|C1|C2|C3, "l2_rqsts.loads" },				\
{ 0x24, 0x04, C0|C1|C2|C3, "l2_rqsts.rfo_hit" },			\
{ 0x24, 0x08, C0|C1|C2|C3, "l2_rqsts.rfo_miss" },			\
{ 0x24, 0x0C, C0|C1|C2|C3, "l2_rqsts.rfos" },				\
{ 0x24, 0x10, C0|C1|C2|C3, "l2_rqsts.ifetch_hit" },			\
{ 0x24, 0x20, C0|C1|C2|C3, "l2_rqsts.ifetch_miss" },			\
{ 0x24, 0x30, C0|C1|C2|C3, "l2_rqsts.ifetches" },			\
{ 0x24, 0x40, C0|C1|C2|C3, "l2_rqsts.prefetch_hit" },			\
{ 0x24, 0x80, C0|C1|C2|C3, "l2_rqsts.prefetch_miss" },			\
{ 0x24, 0xAA, C0|C1|C2|C3, "l2_rqsts.miss" },				\
{ 0x24, 0xC0, C0|C1|C2|C3, "l2_rqsts.prefetches" },			\
{ 0x24, 0xFF, C0|C1|C2|C3, "l2_rqsts.references" },			\
									\
{ 0x26, 0x01, C0|C1|C2|C3, "l2_data_rqsts.demand.i_state" },		\
{ 0x26, 0x02, C0|C1|C2|C3, "l2_data_rqsts.demand.s_state" },		\
{ 0x26, 0x04, C0|C1|C2|C3, "l2_data_rqsts.demand.e_state" },		\
{ 0x26, 0x08, C0|C1|C2|C3, "l2_data_rqsts.demand.m_state" },		\
{ 0x26, 0x0F, C0|C1|C2|C3, "l2_data_rqsts.demand.mesi" },		\
{ 0x26, 0x10, C0|C1|C2|C3, "l2_data_rqsts.prefetch.i_state" },		\
{ 0x26, 0x20, C0|C1|C2|C3, "l2_data_rqsts.prefetch.s_state" },		\
{ 0x26, 0x40, C0|C1|C2|C3, "l2_data_rqsts.prefetch.e_state" },		\
{ 0x26, 0x80, C0|C1|C2|C3, "l2_data_rqsts.prefetch.m_state" },		\
{ 0x26, 0xF0, C0|C1|C2|C3, "l2_data_rqsts.prefetch.mesi" },		\
{ 0x26, 0xFF, C0|C1|C2|C3, "l2_data_rqsts.any" },			\
									\
{ 0x27, 0x01, C0|C1|C2|C3, "l2_write.rfo.i_state" },			\
{ 0x27, 0x02, C0|C1|C2|C3, "l2_write.rfo.s_state" },			\
{ 0x27, 0x08, C0|C1|C2|C3, "l2_write.rfo.m_state" },			\
{ 0x27, 0x0E, C0|C1|C2|C3, "l2_write.rfo.hit" },			\
{ 0x27, 0x0F, C0|C1|C2|C3, "l2_write.rfo.mesi" },			\
{ 0x27, 0x10, C0|C1|C2|C3, "l2_write.lock.i_state" },			\
{ 0x27, 0x20, C0|C1|C2|C3, "l2_write.lock.s_state" },			\
{ 0x27, 0x40, C0|C1|C2|C3, "l2_write.lock.e_state" },			\
{ 0x27, 0x80, C0|C1|C2|C3, "l2_write.lock.m_state" },			\
{ 0x27, 0xE0, C0|C1|C2|C3, "l2_write.lock.hit" },			\
{ 0x27, 0xF0, C0|C1|C2|C3, "l2_write.lock.mesi" },			\
									\
{ 0x28, 0x01, C0|C1|C2|C3, "l1d_wb_l2.i_state" },			\
{ 0x28, 0x02, C0|C1|C2|C3, "l1d_wb_l2.s_state" },			\
{ 0x28, 0x04, C0|C1|C2|C3, "l1d_wb_l2.e_state" },			\
{ 0x28, 0x08, C0|C1|C2|C3, "l1d_wb_l2.m_state" },			\
{ 0x28, 0x0F, C0|C1|C2|C3, "l1d_wb_l2.mesi" },				\
									\
{ 0x40, 0x01, C0|C1, "l1d_cache_ld.i_state" },				\
{ 0x40, 0x02, C0|C1, "l1d_cache_ld.s_state" },				\
{ 0x40, 0x04, C0|C1, "l1d_cache_ld.e_state" },				\
{ 0x40, 0x08, C0|C1, "l1d_cache_ld.m_state" },				\
{ 0x40, 0x0F, C0|C1, "l1d_cache_ld.mesi" },				\
									\
{ 0x41, 0x02, C0|C1, "l1d_cache_st.s_state" },				\
{ 0x41, 0x04, C0|C1, "l1d_cache_st.e_state" },				\
{ 0x41, 0x08, C0|C1, "l1d_cache_st.m_state" },				\
									\
{ 0x42, 0x01, C0|C1, "l1d_cache_lock.hit" },				\
{ 0x42, 0x02, C0|C1, "l1d_cache_lock.s_state" },			\
{ 0x42, 0x04, C0|C1, "l1d_cache_lock.e_state" },			\
{ 0x42, 0x08, C0|C1, "l1d_cache_lock.m_state" },			\
									\
{ 0x43, 0x01, C0|C1, "l1d_all_ref.any" },				\
{ 0x43, 0x02, C0|C1, "l1d_all_ref.cacheable" },				\
									\
{ 0x49, 0x01, C0|C1|C2|C3, "dtlb_misses.any" },				\
{ 0x49, 0x02, C0|C1|C2|C3, "dtlb_misses.walk_completed" },		\
{ 0x49, 0x10, C0|C1|C2|C3, "dtlb_misses.stlb_hit" },			\
{ 0x49, 0x20, C0|C1|C2|C3, "dtlb_misses.pde_miss" },			\
{ 0x49, 0x80, C0|C1|C2|C3, "dtlb_misses.large_walk_completed" },	\
									\
{ 0x4C, 0x01, C0|C1, "load_hit_pre" },					\
{ 0x4E, 0x01, C0|C1, "l1d_prefetch.requests" },				\
{ 0x4E, 0x02, C0|C1, "l1d_prefetch.miss" },				\
{ 0x4E, 0x04, C0|C1, "l1d_prefetch.triggers" },				\
									\
{ 0x51, 0x01, C0|C1, "l1d.repl" },					\
{ 0x51, 0x02, C0|C1, "l1d.m_repl" },					\
{ 0x51, 0x04, C0|C1, "l1d.m_evict" },					\
{ 0x51, 0x08, C0|C1, "l1d.m_snoop_evict" },				\
									\
{ 0x52, 0x01, C0|C1, "l1d_cache_prefetch_lock_fb_hit" },		\
{ 0x63, 0x01, C0|C1, "cache_lock_cycles.l1d_l2" },			\
{ 0x63, 0x02, C0|C1, "cache_lock_cycles.l1d" },				\
{ 0x6C, 0x01, C0|C1|C2|C3, "io_transactions" },				\
									\
{ 0x80, 0x01, C0|C1|C2|C3, "l1i.hits" },				\
{ 0x80, 0x02, C0|C1|C2|C3, "l1i.misses" },				\
{ 0x80, 0x03, C0|C1|C2|C3, "l1i.reads" },				\
{ 0x80, 0x04, C0|C1|C2|C3, "l1i.cycles_stalled" },			\
									\
{ 0x82, 0x01, C0|C1|C2|C3, "large_itlb.hit" },				\
{ 0x85, 0x01, C0|C1|C2|C3, "itlb_misses.any" },				\
{ 0x85, 0x02, C0|C1|C2|C3, "itlb_misses.walk_completed" },		\
									\
{ 0x87, 0x01, C0|C1|C2|C3, "ild_stall.lcp" },				\
{ 0x87, 0x02, C0|C1|C2|C3, "ild_stall.mru" },				\
{ 0x87, 0x04, C0|C1|C2|C3, "ild_stall.iq_full" },			\
{ 0x87, 0x08, C0|C1|C2|C3, "ild_stall.regen" },				\
{ 0x87, 0x0F, C0|C1|C2|C3, "ild_stall.any" },				\
									\
{ 0x88, 0x01, C0|C1|C2|C3, "br_inst_exec.cond" },			\
{ 0x88, 0x02, C0|C1|C2|C3, "br_inst_exec.direct" },			\
{ 0x88, 0x04, C0|C1|C2|C3, "br_inst_exec.indirect_non_call" },		\
{ 0x88, 0x07, C0|C1|C2|C3, "br_inst_exec.non_calls" },			\
{ 0x88, 0x08, C0|C1|C2|C3, "br_inst_exec.return_near" },		\
{ 0x88, 0x10, C0|C1|C2|C3, "br_inst_exec.direct_near_call" },		\
{ 0x88, 0x20, C0|C1|C2|C3, "br_inst_exec.indirect_near_call" },		\
{ 0x88, 0x30, C0|C1|C2|C3, "br_inst_exec.near_calls" },			\
{ 0x88, 0x40, C0|C1|C2|C3, "br_inst_exec.taken" },			\
{ 0x88, 0x7F, C0|C1|C2|C3, "br_inst_exec.any" },			\
									\
{ 0x89, 0x01, C0|C1|C2|C3, "br_misp_exec.cond" },			\
{ 0x89, 0x02, C0|C1|C2|C3, "br_misp_exec.direct" },			\
{ 0x89, 0x04, C0|C1|C2|C3, "br_misp_exec.indirect_non_call" },		\
{ 0x89, 0x07, C0|C1|C2|C3, "br_misp_exec.non_calls" },			\
{ 0x89, 0x08, C0|C1|C2|C3, "br_misp_exec.return_near" },		\
{ 0x89, 0x10, C0|C1|C2|C3, "br_misp_exec.direct_near_call" },		\
{ 0x89, 0x20, C0|C1|C2|C3, "br_misp_exec.indirect_near_call" },		\
{ 0x89, 0x30, C0|C1|C2|C3, "br_misp_exec.near_calls" },			\
{ 0x89, 0x40, C0|C1|C2|C3, "br_misp_exec.taken" },			\
{ 0x89, 0x7F, C0|C1|C2|C3, "br_misp_exec.any" },			\
									\
{ 0xA2, 0x01, C0|C1|C2|C3, "resource_stalls.any" },			\
{ 0xA2, 0x02, C0|C1|C2|C3, "resource_stalls.load" },			\
{ 0xA2, 0x04, C0|C1|C2|C3, "resource_stalls.rs_full" },			\
{ 0xA2, 0x08, C0|C1|C2|C3, "resource_stalls.store" },			\
{ 0xA2, 0x10, C0|C1|C2|C3, "resource_stalls.rob_full" },		\
{ 0xA2, 0x20, C0|C1|C2|C3, "resource_stalls.fpcw" },			\
{ 0xA2, 0x40, C0|C1|C2|C3, "resource_stalls.mxcsr" },			\
{ 0xA2, 0x80, C0|C1|C2|C3, "resource_stalls.other" },			\
									\
{ 0xA6, 0x01, C0|C1|C2|C3, "macro_insts.fusions_decoded" },		\
{ 0xA7, 0x01, C0|C1|C2|C3, "baclear_force_iq" },			\
{ 0xA8, 0x01, C0|C1|C2|C3, "lsd.uops" },				\
{ 0xAE, 0x01, C0|C1|C2|C3, "itlb_flush" },				\
{ 0xB0, 0x40, C0|C1|C2|C3, "offcore_requests.l1d_writeback" },		\
									\
{ 0xB1, 0x01, C0|C1|C2|C3, "uops_executed.port0" },			\
{ 0xB1, 0x02, C0|C1|C2|C3, "uops_executed.port1" },			\
{ 0xB1, 0x04, C0|C1|C2|C3, "uops_executed.port2_core" },		\
{ 0xB1, 0x08, C0|C1|C2|C3, "uops_executed.port3_core" },		\
{ 0xB1, 0x10, C0|C1|C2|C3, "uops_executed.port4_core" },		\
{ 0xB1, 0x1F, C0|C1|C2|C3, "uops_executed.core_active_cycles_np5" },	\
{ 0xB1, 0x20, C0|C1|C2|C3, "uops_executed.port5" },			\
{ 0xB1, 0x3F, C0|C1|C2|C3, "uops_executed.core_active_cycles" },	\
{ 0xB1, 0x40, C0|C1|C2|C3, "uops_executed.port015" },			\
{ 0xB1, 0x80, C0|C1|C2|C3, "uops_executed.port234" },			\
									\
{ 0xB2, 0x01, C0|C1|C2|C3, "offcore_requests_sq_full" },		\
{ 0xB8, 0x01, C0|C1|C2|C3, "snoop_response.hit" },			\
{ 0xB8, 0x02, C0|C1|C2|C3, "snoop_response.hite" },			\
{ 0xB8, 0x04, C0|C1|C2|C3, "snoop_response.hitm" },			\
									\
{ 0xC0, 0x01, C0|C1|C2|C3, "inst_retired.any_p" },			\
{ 0xC0, 0x02, C0|C1|C2|C3, "inst_retired.x87" },			\
{ 0xC0, 0x04, C0|C1|C2|C3, "inst_retired.mmx" },			\
									\
{ 0xC2, 0x01, C0|C1|C2|C3, "uops_retired.any" },			\
{ 0xC2, 0x02, C0|C1|C2|C3, "uops_retired.retire_slots" },		\
{ 0xC2, 0x04, C0|C1|C2|C3, "uops_retired.macro_fused" },		\
									\
{ 0xC3, 0x01, C0|C1|C2|C3, "machine_clears.cycles" },			\
{ 0xC3, 0x02, C0|C1|C2|C3, "machine_clears.mem_order" },		\
{ 0xC3, 0x04, C0|C1|C2|C3, "machine_clears.smc" },			\
									\
{ 0xC4, 0x01, C0|C1|C2|C3, "br_inst_retired.conditional" },		\
{ 0xC4, 0x02, C0|C1|C2|C3, "br_inst_retired.near_call" },		\
{ 0xC4, 0x04, C0|C1|C2|C3, "br_inst_retired.all_branches" },		\
{ 0xC5, 0x02, C0|C1|C2|C3, "br_misp_retired.near_call" },		\
									\
{ 0xC7, 0x01, C0|C1|C2|C3, "ssex_uops_retired.packed_single" },		\
{ 0xC7, 0x02, C0|C1|C2|C3, "ssex_uops_retired.scalar_single" },		\
{ 0xC7, 0x04, C0|C1|C2|C3, "ssex_uops_retired.packed_double" },		\
{ 0xC7, 0x08, C0|C1|C2|C3, "ssex_uops_retired.scalar_double" },		\
{ 0xC7, 0x10, C0|C1|C2|C3, "ssex_uops_retired.vector_integer" },	\
{ 0xC8, 0x20, C0|C1|C2|C3, "itlb_miss_retired" },			\
									\
{ 0xCB, 0x01, C0|C1|C2|C3, "mem_load_retired.l1d_hit" },		\
{ 0xCB, 0x02, C0|C1|C2|C3, "mem_load_retired.l2_hit" },			\
{ 0xCB, 0x04, C0|C1|C2|C3, "mem_load_retired.llc_unshared_hit" },	\
{ 0xCB, 0x08, C0|C1|C2|C3, "mem_load_retired.other_core_l2_hit_hitm" },	\
{ 0xCB, 0x10, C0|C1|C2|C3, "mem_load_retired.llc_miss" },		\
{ 0xCB, 0x40, C0|C1|C2|C3, "mem_load_retired.hit_lfb" },		\
{ 0xCB, 0x80, C0|C1|C2|C3, "mem_load_retired.dtlb_miss" },		\
									\
{ 0xCC, 0x01, C0|C1|C2|C3, "fp_mmx_trans.to_fp" },			\
{ 0xCC, 0x02, C0|C1|C2|C3, "fp_mmx_trans.to_mmx" },			\
{ 0xCC, 0x03, C0|C1|C2|C3, "fp_mmx_trans.any" },			\
									\
{ 0xD0, 0x01, C0|C1|C2|C3, "macro_insts.decoded" },			\
{ 0xD1, 0x02, C0|C1|C2|C3, "uops_decoded.ms" },		\
{ 0xD1, 0x04, C0|C1|C2|C3, "uops_decoded.esp_folding" },		\
{ 0xD1, 0x08, C0|C1|C2|C3, "uops_decoded.esp_sync" },			\
									\
{ 0xD2, 0x01, C0|C1|C2|C3, "rat_stalls.flags" },			\
{ 0xD2, 0x02, C0|C1|C2|C3, "rat_stalls.registers" },			\
{ 0xD2, 0x04, C0|C1|C2|C3, "rat_stalls.rob_read_port" },		\
{ 0xD2, 0x08, C0|C1|C2|C3, "rat_stalls.scoreboard" },			\
{ 0xD2, 0x0F, C0|C1|C2|C3, "rat_stalls.any" },				\
									\
{ 0xD4, 0x01, C0|C1|C2|C3, "seg_rename_stalls" },			\
{ 0xD5, 0x01, C0|C1|C2|C3, "es_reg_renames" },				\
{ 0xDB, 0x01, C0|C1|C2|C3, "uop_unfusion" },				\
									\
{ 0xE0, 0x01, C0|C1|C2|C3, "br_inst_decoded" },				\
{ 0xE5, 0x01, C0|C1|C2|C3, "bpu_missed_call_ret" },			\
{ 0xE6, 0x01, C0|C1|C2|C3, "baclear.clear" },				\
{ 0xE6, 0x02, C0|C1|C2|C3, "baclear.bad_target" },			\
									\
{ 0xE8, 0x01, C0|C1|C2|C3, "bpu_clears.early" },			\
{ 0xE8, 0x02, C0|C1|C2|C3, "bpu_clears.late" },				\
									\
{ 0xF0, 0x01, C0|C1|C2|C3, "l2_transactions.load" },			\
{ 0xF0, 0x02, C0|C1|C2|C3, "l2_transactions.rfo" },			\
{ 0xF0, 0x04, C0|C1|C2|C3, "l2_transactions.ifetch" },			\
{ 0xF0, 0x08, C0|C1|C2|C3, "l2_transactions.prefetch" },		\
{ 0xF0, 0x10, C0|C1|C2|C3, "l2_transactions.l1d_wb" },			\
{ 0xF0, 0x20, C0|C1|C2|C3, "l2_transactions.fill" },			\
{ 0xF0, 0x40, C0|C1|C2|C3, "l2_transactions.wb" },			\
{ 0xF0, 0x80, C0|C1|C2|C3, "l2_transactions.any" },			\
									\
{ 0xF1, 0x02, C0|C1|C2|C3, "l2_lines_in.s_state" },			\
{ 0xF1, 0x04, C0|C1|C2|C3, "l2_lines_in.e_state" },			\
{ 0xF1, 0x07, C0|C1|C2|C3, "l2_lines_in.any" },				\
									\
{ 0xF2, 0x01, C0|C1|C2|C3, "l2_lines_out.demand_clean" },		\
{ 0xF2, 0x02, C0|C1|C2|C3, "l2_lines_out.demand_dirty" },		\
{ 0xF2, 0x04, C0|C1|C2|C3, "l2_lines_out.prefetch_clean" },		\
{ 0xF2, 0x08, C0|C1|C2|C3, "l2_lines_out.prefetch_dirty" },		\
{ 0xF2, 0x0F, C0|C1|C2|C3, "l2_lines_out.any" },			\
									\
{ 0xF4, 0x10, C0|C1|C2|C3, "sq_misc.split_lock" },			\
{ 0xF6, 0x01, C0|C1|C2|C3, "sq_full_stall_cycles" },			\
									\
{ 0xF7, 0x01, C0|C1|C2|C3, "fp_assist.all" },				\
{ 0xF7, 0x02, C0|C1|C2|C3, "fp_assist.output" },			\
{ 0xF7, 0x04, C0|C1|C2|C3, "fp_assist.input" },				\
									\
{ 0xFD, 0x01, C0|C1|C2|C3, "simd_int_64.packed_mpy" },			\
{ 0xFD, 0x02, C0|C1|C2|C3, "simd_int_64.packed_shift" },		\
{ 0xFD, 0x04, C0|C1|C2|C3, "simd_int_64.pack" },			\
{ 0xFD, 0x08, C0|C1|C2|C3, "simd_int_64.unpack" },			\
{ 0xFD, 0x10, C0|C1|C2|C3, "simd_int_64.packed_logical" },		\
{ 0xFD, 0x20, C0|C1|C2|C3, "simd_int_64.packed_arith" },		\
{ 0xFD, 0x40, C0|C1|C2|C3, "simd_int_64.shuffle_move" }

#define	EVENTS_FAM6_NHM_EP_ONLY						\
									\
{ 0x0F, 0x02, C0|C1|C2|C3, "mem_uncore_retired.other_core_l2_hitm" },	\
{ 0x0F, 0x08, C0|C1|C2|C3, "mem_uncore_retired.remote_cache_local_home_hit" }, \
{ 0x0F, 0x10, C0|C1|C2|C3, "mem_uncore_retired.remote_dram" },		\
{ 0x0F, 0x20, C0|C1|C2|C3, "mem_uncore_retired.local_dram" }

#define	EVENTS_FAM6_NHM_MSR						\
									\
{{ 0xB7, 0x01, C2, "off_core_response_0" }, 0x1A6, 0xFFFF}

#define	NHM_OFFCORE_MASK	0xFFFF
#define	NHM_OFFCORE_RSP_MASK	0xFF00	/* Response bits 8-10/11, 12-15 */
#define	NHM_OFFCORE_REQ_MASK	0x00FF	/* Request bits 0-7 */

typedef struct nhm_pcbe_config {
	uint64_t	core_rawpic;	/* Counter bits */
	uint64_t	core_pmc;	/* Counter register address */
	uint64_t	core_ctl;	/* Event Select bits */
	uint64_t	core_pes;	/* Event Select register address */
	uint64_t	msr_val;	/* MSR bits */
	uint64_t	msr_adr;	/* MSR register address */
	uint_t		core_picno;	/* Counter# in cpc_ncounters */
	uint8_t		core_pictype;	/* CORE_GPC, CORE_FFC */
} nhm_pcbe_config_t;

typedef struct msr_events {
	struct events_table_t   event;
	uint16_t msr_adr;
	uint16_t msr_val;
} msr_events_table_t;


extern const struct events_table_t events_fam6_nhm[];
extern const struct events_table_t events_fam6_nhm_ex[];
extern const struct msr_events events_fam6_nhm_msr[];

#ifdef __cplusplus
}
#endif

#endif /* _SYS_NHM_PCBE_H */

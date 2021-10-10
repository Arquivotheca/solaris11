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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Performance Counter Back-End for Intel processors supporting Architectural
 * Performance Monitoring.
 */
#include <sys/cpuvar.h>
#include <sys/param.h>
#include <sys/modctl.h>
#include <sys/inttypes.h>
#include "pcbe_utils.h"
#include "intel_pcbe_utils.h"
#include "snb_pcbe.h"

/*
 * The following table has been created from Appendix A in Intel's SDM.
 */

const snb_pcbe_events_table_t snb_gpc_events_tbl[] = {
	{ { 0x03, 0x01, C_ALL, "ld_blocks.data_unknown" }, 0x0 },
	{ { 0x03, 0x02, C_ALL, "ld_blocks.store_forward" }, 0x0 },
	{ { 0x03, 0x08, C_ALL, "ld_blocks.no_sr" }, 0x0 },
	{ { 0x03, 0x10, C_ALL, "ld_blocks.all_block" }, 0x0 },
	{ { 0x05, 0x01, C_ALL, "misalign_mem_ref.loads" }, 0x0 },
	{ { 0x05, 0x02, C_ALL, "misalign_mem_ref.stores" }, 0x0 },
	{ { 0x07, 0x01, C_ALL, "ld_blocks_partial.address_alias" }, 0x0 },
	{ { 0x07, 0x08, C_ALL, "ld_blocks_partial.all_sta_block" }, 0x0 },
	{ { 0x08, 0x01, C_ALL, "dtlb_load_misses.miss_causes_a_walk" }, 0x0 },
	{ { 0x08, 0x02, C_ALL, "dtlb_load_misses.walk_completed" }, 0x0 },
	{ { 0x08, 0x04, C_ALL, "dtlb_load_misses.walk_duration" }, 0x0 },
	{ { 0x08, 0x10, C_ALL, "dtlb_load_misses.stlb_hit" }, 0x0 },
	/* Set Cmask=1. Set Edge=1 to count occurrences */
	{ { 0x0D, 0x03, C_ALL, "int_misc.recovery_cycles" }, 0x0 },
	{ { 0x0D, 0x40, C_ALL, "int_misc.rat_stall_cycles" }, 0x0 },
	/*
	 * Set Cmask=1, Inv=1, Any=1 to count stalled cycles of this core.
	 * Set Cmask=1, Inv=1 to count stalled cycles.
	 */
	{ { 0x0E, 0x01, C_ALL, "uops_issued.any" }, 0x0 },
	{ { 0x10, 0x01, C_ALL, "fp_comp_ops_exe.x87" }, 0x0 },
	{ { 0x10, 0x10, C_ALL, "fp_comp_ops_exe.sse_fp_packed_double" }, 0x0 },
	{ { 0x10, 0x20, C_ALL, "fp_comp_ops_exe.sse_fp_scalar_single" }, 0x0 },
	{ { 0x10, 0x40, C_ALL, "fp_comp_ops_exe.sse_packed single" }, 0x0 },
	{ { 0x10, 0x80, C_ALL, "fp_comp_ops_exe.sse_scalar_double" }, 0x0 },
	{ { 0x11, 0x01, C_ALL, "simd_fp_256.packed_single" }, 0x0 },
	{ { 0x11, 0x02, C_ALL, "simd_fp_256.packed_double" }, 0x0 },
	/* Set Edge=1, Cmask=1 to count the number of divides. */
	{ { 0x14, 0x01, C_ALL, "arith.fpu_div_active" }, 0x0 },
	{ { 0x17, 0x01, C_ALL, "insts_written_to_iq.insts" }, 0x0 },
	{ { 0x24, 0x01, C_ALL, "l2_rqsts.demand_data_rd_hit" }, 0x0 },
	{ { 0x24, 0x03, C_ALL, "l2_rqsts.all_demand_data_rd" }, 0x0 },
	{ { 0x24, 0x04, C_ALL, "l2_rqsts.rfo_hits" }, 0x0 },
	{ { 0x24, 0x08, C_ALL, "l2_rqsts.rfo_miss" }, 0x0 },
	{ { 0x24, 0x0C, C_ALL, "l2_rqsts.all_rfo" }, 0x0 },
	{ { 0x24, 0x10, C_ALL, "l2_rqsts.code_rd_hit" }, 0x0 },
	{ { 0x24, 0x20, C_ALL, "l2_rqsts.code_rd_miss" }, 0x0 },
	{ { 0x24, 0x30, C_ALL, "l2_rqsts.all_code_rd" }, 0x0 },
	{ { 0x24, 0x40, C_ALL, "l2_rqsts.pf_hit" }, 0x0 },
	{ { 0x24, 0x80, C_ALL, "l2_rqsts.pf_miss" }, 0x0 },
	{ { 0x24, 0xC0, C_ALL, "l2_rqsts.all_pf" }, 0x0 },
	{ { 0x27, 0x01, C_ALL, "l2_store_lock_rqsts.miss" }, 0x0 },
	{ { 0x27, 0x04, C_ALL, "l2_store_lock_rqsts.hit_e" }, 0x0 },
	{ { 0x27, 0x08, C_ALL, "l2_store_lock_rqsts.hit_m" }, 0x0 },
	{ { 0x27, 0x0F, C_ALL, "l2_store_lock_rqsts.all" }, 0x0 },
	{ { 0x28, 0x04, C_ALL, "l2_l1d_wb_rqsts.hit_e" }, 0x0 },
	{ { 0x28, 0x08, C_ALL, "l2_l1d_wb_rqsts.hit_m" }, 0x0 },
	/*
	 * Set Cmask=1 and Edge=1 to count occurrences.
	 * Set Cmask=1 to count cycles.
	 */
	{ { 0x48, 0x01, C(2), "l1d_pend_miss.pending" }, 0x0 },
	{ { 0x49, 0x01, C_ALL, "dtlb_store_misses.miss_causes_a_walk" }, 0x0 },
	{ { 0x49, 0x02, C_ALL, "dtlb_store_misses.walk_completed" }, 0x0 },
	{ { 0x49, 0x04, C_ALL, "dtlb_store_misses.walk_duration" }, 0x0 },
	{ { 0x49, 0x10, C_ALL, "dtlb_store_misses.stlb_hit" }, 0x0 },
	{ { 0x4C, 0x01, C_ALL, "load_hit_pre.sw_pf" }, 0x0 },
	{ { 0x4C, 0x02, C_ALL, "load_hit_pre.hw_pf" }, 0x0 },
	{ { 0x4E, 0x02, C_ALL, "hw_pre_req.dl1_miss" }, 0x0 },
	{ { 0x51, 0x01, C_ALL, "l1d.replacement" }, 0x0 },
	{ { 0x51, 0x02, C_ALL, "l1d.allocated_in_m" }, 0x0 },
	{ { 0x51, 0x04, C_ALL, "l1d.eviction" }, 0x0 },
	{ { 0x51, 0x08, C_ALL, "l1d.all_m_replacement" }, 0x0 },
	/* Set Cmask=1 to count cycles. */
	{ { 0x59, 0x20, C_ALL, "partial_rat_stalls.flags_merge_uop" }, 0x0 },
	{ { 0x59, 0x40, C_ALL, "partial_rat_stalls.slow_lea_window" }, 0x0 },
	{ { 0x59, 0x80, C_ALL, "partial_rat_stalls.mul_single_uop" }, 0x0 },
	{ { 0x5B, 0x0C, C_ALL, "resource_stalls2.all_fl_empty" }, 0x0 },
	{ { 0x5B, 0x0F, C_ALL, "resource_stalls2.all_prf_control" }, 0x0 },
	{ { 0x5B, 0x40, C_ALL, "resource_stalls2.bob_full" }, 0x0 },
	{ { 0x5B, 0x4F, C_ALL, "resource_stalls2.ooo_rsrc" }, 0x0 },
	/* Set Edge=1 to count transition */
	{ { 0x5C, 0x01, C_ALL, "cpl_cycles.ring0" }, 0x0 },
	{ { 0x5C, 0x02, C_ALL, "cpl_cycles.ring123" }, 0x0 },
	{ { 0x5E, 0x01, C_ALL, "rs_events.empty_cycles" }, 0x0 },
	/* Set Cmask=1 to count cycles. */
	{ { 0x60, 0x01, C_ALL, "offcore_requests_outstanding.demand_data_rd" },
	    0x0 },
	/* Set Cmask=1 to count cycles. */
	{ { 0x60, 0x04, C_ALL, "offcore_requests_outstanding.demand_rfo" },
	    0x0 },
	/* Set Cmask=1 to count cycles. */
	{ { 0x60, 0x08, C_ALL, "offcore_requests_outstanding.all_data_rd" },
	    0x0 },
	{ { 0x63, 0x01, C_ALL, "lock_cycles.split_lock_uc_lock_duration" },
	    0x0 },
	{ { 0x63, 0x02, C_ALL, "lock_cycles.cache_lock_duration" }, 0x0 },
	{ { 0x79, 0x02, C_ALL, "idq.empty" }, 0x0 },
	/* Set Cmask=1 to count cycles. Can combine Umask 04H and 20H */
	{ { 0x79, 0x04, C_ALL, "idq.mite_uops" }, 0x0 },
	{ { 0x79, 0x24, C_ALL, "idq.all_mite_uops" }, 0x0 },
	/* Set Cmask=1 to count cycles. Can combine Umask 08H and 10H */
	{ { 0x79, 0x08, C_ALL, "idq.dsb_uops" }, 0x0 },
	{ { 0x79, 0x18, C_ALL, "idq.all_dsb_uops" }, 0x0 },
	/*
	 * Set Cmask=1 to count cycles MS is busy.
	 * Set Cmask=1 and Edge=1 to count MS activations.
	 * Can combine Umask 08H and 10H
	 */
	{ { 0x79, 0x10, C_ALL, "idq.ms_dsb_uops" }, 0x0 },
	/* Set Cmask=1 to count cycles. Can combine Umask 04H and 20H */
	{ { 0x79, 0x20, C_ALL, "idq.ms_mite_uops" }, 0x0 },
	/* Set Cmask=1 to count cycles. Can combine Umask 04H, 08H and 30H */
	{ { 0x79, 0x30, C_ALL, "idq.ms_uops" }, 0x0 },
	{ { 0x80, 0x02, C_ALL, "icache.misses" }, 0x0 },
	{ { 0x85, 0x01, C_ALL, "itlb_misses.miss_causes_a_walk" }, 0x0 },
	{ { 0x85, 0x02, C_ALL, "itlb_misses.walk_completed" }, 0x0 },
	{ { 0x85, 0x04, C_ALL, "itlb_misses.walk_duration" }, 0x0 },
	{ { 0x85, 0x10, C_ALL, "itlb_misses.stlb_hit" }, 0x0 },
	{ { 0x87, 0x01, C_ALL, "ild_stall.lcp" }, 0x0 },
	{ { 0x87, 0x04, C_ALL, "ild_stall.iq_full" }, 0x0 },
	/* 0x88 Events must combine with umask 0x40 or 0x80 see SDB Spec */
	{ { 0x88, 0x41, C_ALL, "br_inst_exec.nontaken_cond" }, 0x0 },
	{ { 0x88, 0x81, C_ALL, "br_inst_exec.taken_cond" }, 0x0 },
	{ { 0x88, 0x82, C_ALL, "br_inst_exec.taken_direct_jmp" }, 0x0 },
	{ { 0x88, 0x84, C_ALL, "br_inst_exec.taken_indirect_jmp_non_call_ret" },
	    0x0 },
	{ { 0x88, 0x88, C_ALL, "br_inst_exec.taken_return_near" }, 0x0 },
	{ { 0x88, 0x90, C_ALL, "br_inst_exec.taken_direct_near_call" }, 0x0 },
	{ { 0x88, 0xA0, C_ALL, "br_inst_exec.taken_indirect_near_call" }, 0x0 },
	{ { 0x88, 0xFF, C_ALL, "br_inst_exec.all_branches" }, 0x0 },
	/* 0x88 Events must combine with umask 0x40 or 0x80 see SDB Spec */
	{ { 0x89, 0x41, C_ALL, "br_misp_exec.nontaken_cond" }, 0x0 },
	{ { 0x89, 0x81, C_ALL, "br_misp_exec.taken_cond" }, 0x0 },
	{ { 0x89, 0x84, C_ALL, "br_misp_exec.taken_indirect_jmp_non_call_ret" },
	    0x0 },
	{ { 0x89, 0x88, C_ALL, "br_misp_exec.taken_return_near" }, 0x0 },
	{ { 0x89, 0x90, C_ALL, "br_misp_exec.taken_direct_near_call" }, 0x0 },
	{ { 0x89, 0xA0, C_ALL, "br_misp_exec.taken_indirect_near_call" }, 0x0 },
	{ { 0x89, 0xFF, C_ALL, "br_misp_exec.all_branches" }, 0x0 },
	/* Use Cmask to qualify uop b/w */
	{ { 0x9C, 0x01, C_ALL, "idq_uops_not_delivered.core" }, 0x0 },
	{ { 0xA1, 0x01, C_ALL, "uops_dispatched_port.port_0" }, 0x0 },
	{ { 0xA1, 0x02, C_ALL, "uops_dispatched_port.port_1" }, 0x0 },
	{ { 0xA1, 0x04, C_ALL, "uops_dispatched_port.port_2_ld" }, 0x0 },
	{ { 0xA1, 0x08, C_ALL, "uops_dispatched_port.port_2_sta" }, 0x0 },
	{ { 0xA1, 0x0C, C_ALL, "uops_dispatched_port.port_2" }, 0x0 },
	{ { 0xA1, 0x10, C_ALL, "uops_dispatched_port.port_3_ld" }, 0x0 },
	{ { 0xA1, 0x20, C_ALL, "uops_dispatched_port.port_3_sta" }, 0x0 },
	{ { 0xA1, 0x30, C_ALL, "uops_dispatched_port.port_3" }, 0x0 },
	{ { 0xA1, 0x40, C_ALL, "uops_dispatched_port.port_4" }, 0x0 },
	{ { 0xA1, 0x80, C_ALL, "uops_dispatched_port.port_5" }, 0x0 },
	{ { 0xA2, 0x01, C_ALL, "resource_stalls.any" }, 0x0 },
	{ { 0xA2, 0x02, C_ALL, "resource_stalls.lb" }, 0x0 },
	{ { 0xA2, 0x04, C_ALL, "resource_stalls.rs" }, 0x0 },
	{ { 0xA2, 0x08, C_ALL, "resource_stalls.sb" }, 0x0 },
	{ { 0xA2, 0x10, C_ALL, "resource_stalls.rob" }, 0x0 },
	{ { 0xA2, 0x20, C_ALL, "resource_stalls.fcsw" }, 0x0 },
	{ { 0xA2, 0x40, C_ALL, "resource_stalls.mxcsr" }, 0x0 },
	{ { 0xA2, 0x80, C_ALL, "resource_stalls.other" }, 0x0 },
	{ { 0xAB, 0x01, C_ALL, "dsb2mite_switches.count" }, 0x0 },
	{ { 0xAB, 0x02, C_ALL, "dsb2mite_switches.penalty_cycles" }, 0x0 },
	{ { 0xAC, 0x02, C_ALL, "dsb_fill.other_cancel" }, 0x0 },
	{ { 0xAC, 0x08, C_ALL, "dsb_fill.exceed_dsb_lines" }, 0x0 },
	{ { 0xAC, 0x0A, C_ALL, "dsb_fill.all_cancel" }, 0x0 },
	{ { 0xAE, 0x01, C_ALL, "itlb.itlb_flush" }, 0x0 },
	{ { 0xB0, 0x01, C_ALL, "offcore_requests.demand_data_rd" }, 0x0 },
	{ { 0xB0, 0x04, C_ALL, "offcore_requests.demand_rfo" }, 0x0 },
	{ { 0xB0, 0x08, C_ALL, "offcore_requests.all_data_rd" }, 0x0 },
	/* Set Cmask=1, INV=1 to count stall cycles. */
	{ { 0xB1, 0x01, C_ALL, "uops_dispatched.thread" }, 0x0 },
	{ { 0xB1, 0x02, C_ALL, "uops_dispatched.core" }, 0x0 },
	{ { 0xB2, 0x01, C_ALL, "offcore_requests_buffer.sq_full" }, 0x0 },
	{ { 0xB6, 0x01, C_ALL, "agu_bypass_cancel.count" }, 0x0 },
	/*
	 * See Section "Off-core Response Performance Monitoring"
	 *
	 * Though these two off_core events support all counters, only 1 of them
	 * can be used at any given time.  This is due to the extra MSR
	 * programming required.
	 */
	{ { 0xB7, 0x01, C_ALL, "off_core_response_0" }, OFFCORE_RSP_0 },
	{ { 0xBB, 0x01, C_ALL, "off_core_response_1" }, OFFCORE_RSP_1 },
	{ { 0xBD, 0x01, C_ALL, "tlb_flush.dtlb_thread" }, 0x0 },
	{ { 0xBD, 0x20, C_ALL, "tlb_flush.stlb_any" }, 0x0 },
	{ { 0xBF, 0x05, C_ALL, "l1d_blocks.bank_conflict_cycles" }, 0x0 },
	/* Must quiesce other PMCs. */
	{ { 0xC0, 0x01, C(1), "inst_retired.prec_dist" }, 0x0 },
	{ { 0xC0, 0x02, C_ALL, "inst_retired.x87" }, 0x0 },
	{ { 0xC1, 0x02, C_ALL, "other_assists.itlb_miss_retired" }, 0x0 },
	{ { 0xC1, 0x08, C_ALL, "other_assists.avx_store" }, 0x0 },
	{ { 0xC1, 0x10, C_ALL, "other_assists.avx_to_sse" }, 0x0 },
	{ { 0xC1, 0x20, C_ALL, "other_assists.sse_to_avx" }, 0x0 },
	/* Use cmask=1 and invert to count active cycles or stalled cycles. */
	{ { 0xC2, 0x01, C_ALL, "uops_retired.all" }, 0x0 },
	{ { 0xC2, 0x02, C_ALL, "uops_retired.retire_slots" }, 0x0 },
	{ { 0xC3, 0x02, C_ALL, "machine_clears.memory_ordering" }, 0x0 },
	{ { 0xC3, 0x04, C_ALL, "machine_clears.smc" }, 0x0 },
	{ { 0xC3, 0x20, C_ALL, "machine_clears.maskmov" }, 0x0 },
	{ { 0xC4, 0x01, C_ALL, "br_inst_retired.conditional" }, 0x0 },
	{ { 0xC4, 0x02, C_ALL, "br_inst_retired.near_call" }, 0x0 },
	{ { 0xC4, 0x04, C_ALL, "br_inst_retired.all_branches" }, 0x0 },
	{ { 0xC4, 0x08, C_ALL, "br_inst_retired.near_return" }, 0x0 },
	{ { 0xC4, 0x10, C_ALL, "br_inst_retired.not_taken" }, 0x0 },
	{ { 0xC4, 0x20, C_ALL, "br_inst_retired.near_taken" }, 0x0 },
	{ { 0xC4, 0x40, C_ALL, "br_inst_retired.far_branch" }, 0x0 },
	{ { 0xC5, 0x01, C_ALL, "br_misp_retired.conditional" }, 0x0 },
	{ { 0xC5, 0x02, C_ALL, "br_misp_retired.near_call" }, 0x0 },
	{ { 0xC5, 0x04, C_ALL, "br_misp_retired.all_branches" }, 0x0 },
	{ { 0xC5, 0x10, C_ALL, "br_misp_retired.not_taken" }, 0x0 },
	{ { 0xC5, 0x20, C_ALL, "br_misp_retired.taken" }, 0x0 },
	{ { 0xCA, 0x02, C_ALL, "fp_assist.x87_output" }, 0x0 },
	{ { 0xCA, 0x04, C_ALL, "fp_assist.x87_input" }, 0x0 },
	{ { 0xCA, 0x08, C_ALL, "fp_assist.simd_output" }, 0x0 },
	{ { 0xCA, 0x10, C_ALL, "fp_assist.simd_input" }, 0x0 },
	{ { 0xCA, 0x1E, C_ALL, "fp_assist.any" }, 0x0 },
	{ { 0xCC, 0x20, C_ALL, "rob_misc_events.lbr_inserts" }, 0x0 },
	/* See Section "MSR_PEBS_LD_LAT_THRESHOLD" */
	{ { 0xCD, 0x01, C(3), "mem_trans_retired.load_latency" },
	    PEBS_LD_LAT_THRESHOLD },
	/* See Section "Precise Store Facility" */
	{ { 0xCD, 0x02, C(3), "mem_trans_retired.precise_store" }, 0x0 },
	/* Event 0xD0 must be combined with umasks 0x1(loads) or 0x2(stores) */
	{ { 0xD0, 0x11, C_ALL, "mem_uop_retired.stlb_miss_loads" }, 0x0 },
	{ { 0xD0, 0x12, C_ALL, "mem_uop_retired.stlb_miss_stores" }, 0x0 },
	{ { 0xD0, 0x21, C_ALL, "mem_uop_retired.lock_loads" }, 0x0 },
	{ { 0xD0, 0x22, C_ALL, "mem_uop_retired.lock_stores" }, 0x0 },
	{ { 0xD0, 0x41, C_ALL, "mem_uop_retired.split_loads" }, 0x0 },
	{ { 0xD0, 0x42, C_ALL, "mem_uop_retired.split_stores" }, 0x0 },
	{ { 0xD0, 0x81, C_ALL, "mem_uop_retired.all_loads" }, 0x0 },
	{ { 0xD0, 0x82, C_ALL, "mem_uop_retired.all_stores" }, 0x0 },
	{ { 0xD1, 0x01, C_ALL, "mem_load_uops_retired.l1_hit" }, 0x0 },
	{ { 0xD1, 0x02, C_ALL, "mem_load_uops_retired.l2_hit" }, 0x0 },
	{ { 0xD1, 0x04, C_ALL, "mem_load_uops_retired.llc_hit" }, 0x0 },
	{ { 0xD1, 0x40, C_ALL, "mem_load_uops_retired.hit_lfb" }, 0x0 },
	{ { 0xD2, 0x01, C_ALL, "mem_load_uops_llc_hit_retired.xsnp_miss" },
	    0x0 },
	{ { 0xD2, 0x02, C_ALL, "mem_load_uops_llc_hit_retired.xsnp_hit" },
	    0x0 },
	{ { 0xD2, 0x04, C_ALL, "mem_load_uops_llc_hit_retired.xsnp_hitm" },
	    0x0 },
	{ { 0xD2, 0x08, C_ALL, "mem_load_uops_llc_hit_retired.xsnp_none" },
	    0x0 },
	{ { 0xD4, 0x02, C_ALL, "mem_load_uops_misc_retired.llc_miss" }, 0x0 },
	{ { 0xF0, 0x01, C_ALL, "l2_trans.demand_data_rd" }, 0x0 },
	{ { 0xF0, 0x02, C_ALL, "l2_trans.rfo" }, 0x0 },
	{ { 0xF0, 0x04, C_ALL, "l2_trans.code_rd" }, 0x0 },
	{ { 0xF0, 0x08, C_ALL, "l2_trans.all_pf" }, 0x0 },
	{ { 0xF0, 0x10, C_ALL, "l2_trans.l1d_wb" }, 0x0 },
	{ { 0xF0, 0x20, C_ALL, "l2_trans.l2_fill" }, 0x0 },
	{ { 0xF0, 0x40, C_ALL, "l2_trans.l2_wb" }, 0x0 },
	{ { 0xF0, 0x80, C_ALL, "l2_trans.all_requests" }, 0x0 },
	{ { 0xF1, 0x01, C_ALL, "l2_lines_in.i" }, 0x0 },
	{ { 0xF1, 0x02, C_ALL, "l2_lines_in.s" }, 0x0 },
	{ { 0xF1, 0x04, C_ALL, "l2_lines_in.e" }, 0x0 },
	{ { 0xF1, 0x07, C_ALL, "l2_lines_in.all" }, 0x0 },
	{ { 0xF2, 0x01, C_ALL, "l2_lines_out.demand_clean" }, 0x0 },
	{ { 0xF2, 0x02, C_ALL, "l2_lines_out.demand_dirty" }, 0x0 },
	{ { 0xF2, 0x04, C_ALL, "l2_lines_out.pf_clean" }, 0x0 },
	{ { 0xF2, 0x08, C_ALL, "l2_lines_out.pf_dirty" }, 0x0 },
	{ { 0xF2, 0x0A, C_ALL, "l2_lines_out.dirty_all" }, 0x0 },
	{ { 0xF4, 0x10, C_ALL, "sq_misc.split_lock" }, 0x0 }
};
int snb_pcbe_events_num = PCBE_TABLE_SIZE(snb_gpc_events_tbl);

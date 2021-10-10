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

#ifndef _SYS_ATOM_PCBE_H
#define	_SYS_ATOM_PCBE_H

#include "fam6_pcbe.h"

#ifdef __cplusplus
extern "C" {
#endif

#define	GENERICEVENTS_FAM6_MOD28					       \
{ 0xc4, 0x00, C0|C1, "PAPI_br_ins" },	/* br_inst_retired.any */	       \
{ 0xc5, 0x00, C0|C1, "PAPI_br_msp" },	/* br_inst_retired.mispred */	       \
{ 0xc4, 0x03, C0|C1, "PAPI_br_ntk" },					       \
			/* br_inst_retired.pred_not_taken|mispred_not_taken */ \
{ 0xc4, 0x05, C0|C1, "PAPI_br_prc" },					       \
			/* br_inst_retired.pred_not_taken|pred_taken */	       \
{ 0xc8, 0x00, C0|C1, "PAPI_hw_int" },	/* hw_int_rcv */	      	       \
{ 0xaa, 0x03, C0|C1, "PAPI_tot_iis" },	/* macro_insts.all_decoded */	       \
{ 0x40, 0x23, C0|C1, "PAPI_l1_dca" },	/* l1d_cache.l1|st */	      	       \
{ 0x2a, 0x41, C0|C1, "PAPI_l2_stm" },	/* l2_st.self.i_state */	       \
{ 0x2e, 0x4f, C0|C1, "PAPI_l2_tca" },	/* longest_lat_cache.reference */      \
{ 0x2e, 0x4e, C0|C1, "PAPI_l2_tch" },   /* l2_rqsts.mes */		       \
{ 0x2e, 0x41, C0|C1, "PAPI_l2_tcm" },	/* longest_lat_cache.miss */	       \
{ 0x2a, 0x4f, C0|C1, "PAPI_l2_tcw" },	/* l2_st.self.mesi */		       \
{ 0x08, 0x07, C0|C1, "PAPI_tlb_dm" },	/* data_tlb_misses.dtlb.miss */	       \
{ 0x82, 0x02, C0|C1, "PAPI_tlb_im" }	/* itlb.misses */

#define	EVENTS_FAM6_MOD28						\
	{ 0x2,  0x81, C0|C1, "store_forwards.good" },                   \
	{ 0x6,  0x0,  C0|C1, "segment_reg_loads.any" },                 \
	{ 0x7,  0x1,  C0|C1, "prefetch.prefetcht0" },                   \
	{ 0x7,  0x6,  C0|C1, "prefetch.sw_l2" },                        \
	{ 0x7,  0x8,  C0|C1, "prefetch.prefetchnta" },                  \
	{ 0x8,  0x7,  C0|C1, "data_tlb_misses.dtlb_miss" },             \
	{ 0x8,  0x5,  C0|C1, "data_tlb_misses.dtlb_miss_ld" },          \
	{ 0x8,  0x9,  C0|C1, "data_tlb_misses.l0_dtlb_miss_ld" },	\
	{ 0x8,  0x6,  C0|C1, "data_tlb_misses.dtlb_miss_st" },          \
	{ 0xC,  0x3,  C0|C1, "page_walks.cycles" },                     \
	{ 0x10, 0x1,  C0|C1, "x87_comp_ops_exe.any.s" },                \
	{ 0x10, 0x81, C0|C1, "x87_comp_ops_exe.any.ar" },               \
	{ 0x11, 0x1,  C0|C1, "fp_assist" },                             \
	{ 0x11, 0x81, C0|C1, "fp_assist.ar" },                          \
	{ 0x12, 0x1,  C0|C1, "mul.s" },                                 \
	{ 0x12, 0x81, C0|C1, "mul.ar" },                                \
	{ 0x13, 0x1,  C0|C1, "div.s" },                                 \
	{ 0x13, 0x81, C0|C1, "div.ar" },                                \
	{ 0x14, 0x1,  C0|C1, "cycles_div_busy" },                       \
	{ 0x21, 0x0,  C0|C1, "l2_ads" },                      		\
	{ 0x22, 0x0,  C0|C1, "l2_dbus_busy" },                		\
	{ 0x24, 0x0,  C0|C1, "l2_lines_in" },   			\
	{ 0x25, 0x0,  C0|C1, "l2_m_lines_in" },               		\
	{ 0x26, 0x0,  C0|C1, "l2_lines_out" },  			\
	{ 0x27, 0x0,  C0|C1, "l2_m_lines_out" },			\
	{ 0x28, 0x0,  C0|C1, "l2_ifetch" },  				\
	{ 0x29, 0x0,  C0|C1, "l2_ld" },					\
	{ 0x2A, 0x0,  C0|C1, "l2_st" },      				\
	{ 0x2B, 0x0,  C0|C1, "l2_lock" },    				\
	{ 0x2E, 0x0,  C0|C1, "l2_rqsts" },             			\
	{ 0x2E, 0x41, C0|C1, "l2_rqsts.self.demand.i_state" },		\
	{ 0x2E, 0x4F, C0|C1, "l2_rqsts.self.demand.mesi" },		\
	{ 0x30, 0x0,  C0|C1, "l2_reject_bus_q" },			\
	{ 0x32, 0x0,  C0|C1, "l2_no_req" },                   		\
	{ 0x3A, 0x0,  C0|C1, "eist_trans" },                            \
	{ 0x3B, 0xC0, C0|C1, "thermal_trip" },                          \
	{ 0x3C, 0x0,  C0|C1, "cpu_clk_unhalted.core_p" },               \
	{ 0x3C, 0x1,  C0|C1, "cpu_clk_unhalted.bus" },                  \
	{ 0x3C, 0x2,  C0|C1, "cpu_clk_unhalted.no_other" },             \
	{ 0x40, 0x21, C0|C1, "l1d_cache.ld" },                          \
	{ 0x40, 0x22, C0|C1, "l1d_cache.st" },                          \
	{ 0x60, 0x0,  C0|C1, "bus_request_outstanding" },		\
	{ 0x61, 0x0,  C0|C1, "bus_bnr_drv" },                		\
	{ 0x62, 0x0,  C0|C1, "bus_drdy_clocks" },            		\
	{ 0x63, 0x0,  C0|C1, "bus_lock_clocks" },  			\
	{ 0x64, 0x0,  C0|C1, "bus_data_rcv" },                		\
	{ 0x65, 0x0,  C0|C1, "bus_trans_brd" },    			\
	{ 0x66, 0x0,  C0|C1, "bus_trans_rfo" },    			\
	{ 0x67, 0x0,  C0|C1, "bus_trans_wb" },     			\
	{ 0x68, 0x0,  C0|C1, "bus_trans_ifetch" }, 			\
	{ 0x69, 0x0,  C0|C1, "bus_trans_inval" },  			\
	{ 0x6A, 0x0,  C0|C1, "bus_trans_pwr" },				\
	{ 0x6B, 0x0,  C0|C1, "bus_trans_p" },      			\
	{ 0x6C, 0x0,  C0|C1, "bus_trans_io" },     			\
	{ 0x6D, 0x0,  C0|C1, "bus_trans_def" },    			\
	{ 0x6E, 0x0,  C0|C1, "bus_trans_burst" },  			\
	{ 0x6F, 0x0,  C0|C1, "bus_trans_mem" },    			\
	{ 0x70, 0x0,  C0|C1, "bus_trans_any" },    			\
	{ 0x77, 0x0,  C0|C1, "ext_snoop" },     			\
	{ 0x7A, 0x0,  C0|C1, "bus_hit_drv" },                		\
	{ 0x7B, 0x0,  C0|C1, "bus_hitm_drv" },               		\
	{ 0x7D, 0x0,  C0|C1, "busq_empty" },                  		\
	{ 0x7E, 0x0,  C0|C1, "snoop_stall_drv" },  			\
	{ 0x7F, 0x0,  C0|C1, "bus_io_wait" },				\
	{ 0x80, 0x3,  C0|C1, "icache.accesses" },                       \
	{ 0x80, 0x2,  C0|C1, "icache.misses" },                         \
	{ 0x82, 0x4,  C0|C1, "itlb.flush" },                            \
	{ 0x82, 0x2,  C0|C1, "itlb.misses" },                           \
	{ 0xAA, 0x2,  C0|C1, "macro_insts.cisc_decoded" },              \
	{ 0xAA, 0x3,  C0|C1, "macro_insts.all_decoded" },               \
	{ 0xB0, 0x0,  C0|C1, "simd_uops_exec.s" },                      \
	{ 0xB0, 0x80, C0|C1, "simd_uops_exec.ar" },                     \
	{ 0xB1, 0x0,  C0|C1, "simd_sat_uop_exec.s" },                   \
	{ 0xB1, 0x80, C0|C1, "simd_sat_uop_exec.ar" },                  \
	{ 0xB3, 0x1,  C0|C1, "simd_uop_type_exec.mul.s" },              \
	{ 0xB3, 0x81, C0|C1, "simd_uop_type_exec.mul.ar" },             \
	{ 0xB3, 0x02, C0|C1, "simd_uop_type_exec.shift.s" },            \
	{ 0xB3, 0x82, C0|C1, "simd_uop_type_exec.shift.ar" },           \
	{ 0xB3, 0x04, C0|C1, "simd_uop_type_exec.pack.s" },             \
	{ 0xB3, 0x84, C0|C1, "simd_uop_type_exec.pack.ar" },            \
	{ 0xB3, 0x08, C0|C1, "simd_uop_type_exec.unpack.s" },           \
	{ 0xB3, 0x88, C0|C1, "simd_uop_type_exec.unpack.ar" },          \
	{ 0xB3, 0x10, C0|C1, "simd_uop_type_exec.logical.s" },          \
	{ 0xB3, 0x90, C0|C1, "simd_uop_type_exec.logical.ar" },         \
	{ 0xB3, 0x20, C0|C1, "simd_uop_type_exec.arithmetic.s" },       \
	{ 0xB3, 0xA0, C0|C1, "simd_uop_type_exec.arithmetic.ar" },      \
	{ 0xC2, 0x10, C0|C1, "uops_retired.any" },                      \
	{ 0xC3, 0x1,  C0|C1, "machine_clears.smc" },                    \
	{ 0xC4, 0x0,  C0|C1, "br_inst_retired.any" },                   \
	{ 0xC4, 0x1,  C0|C1, "br_inst_retired.pred_not_taken" },        \
	{ 0xC4, 0x2,  C0|C1, "br_inst_retired.mispred_not_taken" },     \
	{ 0xC4, 0x4,  C0|C1, "br_inst_retired.pred_taken" },            \
	{ 0xC4, 0x8,  C0|C1, "br_inst_retired.mispred_taken" },         \
	{ 0xC4, 0xA,  C0|C1, "br_inst_retired.mispred" },               \
	{ 0xC4, 0xC,  C0|C1, "br_inst_retired.taken" },                 \
	{ 0xC4, 0xF,  C0|C1, "br_inst_retired.any1" },                  \
	{ 0xC6, 0x1,  C0|C1, "cycles_int_masked.cycles_int_masked" },   \
	{ 0xC6, 0x2,  C0|C1,						\
		"cycles_int_masked.cycles_int_pending_and_masked" },	\
	{ 0xC7, 0x1,  C0|C1, "simd_inst_retired.packed_single" },       \
	{ 0xC7, 0x2,  C0|C1, "simd_inst_retired.scalar_single" },      	\
	{ 0xC7, 0x4,  C0|C1, "simd_inst_retired.packed_double" },       \
	{ 0xC7, 0x8,  C0|C1, "simd_inst_retired.scalar_double" },       \
	{ 0xC7, 0x10, C0|C1, "simd_inst_retired.vector" },              \
	{ 0xC7, 0x1F, C0|C1, "simd_inst_retired.any" },                 \
	{ 0xC8, 0x00, C0|C1, "hw_int_rcv" },                            \
	{ 0xCA, 0x1,  C0|C1, "simd_comp_inst_retired.packed_single" },  \
	{ 0xCA, 0x2,  C0|C1, "simd_comp_inst_retired.scalar_single" }, 	\
	{ 0xCA, 0x4,  C0|C1, "simd_comp_inst_retired.packed_double" },  \
	{ 0xCA, 0x8,  C0|C1, "simd_comp_inst_retired.scalar_double" },  \
	{ 0xCB, 0x1,  C0|C1, "mem_load_retired.l2_hit" },               \
	{ 0xCB, 0x2,  C0|C1, "mem_load_retired.l2_miss" },              \
	{ 0xCB, 0x4,  C0|C1, "mem_load_retired.dtlb_miss" },           	\
	{ 0xCD, 0x0,  C0|C1, "simd_assist" },                           \
	{ 0xCE, 0x0,  C0|C1, "simd_instr_retired" },                    \
	{ 0xCF, 0x0,  C0|C1, "simd_sat_instr_retired" },                \
	{ 0xE0, 0x1,  C0|C1, "br_inst_decoded" },                       \
	{ 0xE4, 0x1,  C0|C1, "bogus_br" },                             	\
	{ 0xE6, 0x1,  C0|C1, "baclears.any" }

extern const struct events_table_t events_fam6_mod28[];

#ifdef __cplusplus
}
#endif

#endif /* _SYS_ATOM_PCBE_H */

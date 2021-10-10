/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/trap.h>
#include <sys/mca_x86.h>
#include <sys/mca_amd.h>

#include <mtst_cpumod_api.h>
#include "opt.h"

static void
opt_fini(void)
{
}

static const mtst_cmd_t opt_cmds[] = {
	/*
	 * Data cache
	 */
	{ "dc.inf_sys_ecc1", "addr,syndrome,priv", opt_dc_inf_sys_ecc1, 0,
	"Correctable D$ data infill from system memory" },

	{ "dc.inf_sys_eccm", "addr,syndrome,priv", opt_dc_inf_sys_eccm, 0,
	"Uncorrectable D$ data infill from system memory" },

	{ "dc.inf_l2_ecc1", "addr,priv", opt_dc_inf_l2_ecc1, 0,
	"Correctable D$ data infill from L2$" },

	{ "dc.inf_l2_eccm", "addr,priv", opt_dc_inf_l2_eccm, 0,
	"Uncorrectable D$ data infill from L2$" },

	{ "dc.data_ecc1_uc:ld", "addr,priv", opt_dc_data_ecc1_uc_ld, 0,
	"Uncorrectable single-bit D$ data array error from load" },

	{ "dc.data_ecc1_uc:st", "addr,priv", opt_dc_data_ecc1_uc_st, 0,
	"Uncorrectable single-bit D$ data array error from store" },

	{ "dc.data_ecc1_uc:cpb", "addr,priv", opt_dc_data_ecc1_uc_cpb, 0,
	"Uncorrectable single-bit D$ data array error from copyback" },

	{ "dc.data_ecc1_uc:snp", "addr,priv", opt_dc_data_ecc1_uc_snp, 0,
	"Uncorrectable single-bit D$ data array error from snoop" },

	{ "dc.data_ecc1:scr", "addr,priv", opt_dc_data_ecc1_scr, 0,
	"Correctable single-bit D$ data array error from scrub" },

	{ "dc.data_eccm:ld", "addr,priv", opt_dc_data_eccm_ld, 0,
	"Uncorrectable multi-bit D$ data array error from load" },

	{ "dc.data_eccm:st", "addr,priv", opt_dc_data_eccm_st, 0,
	"Uncorrectable multi-bit D$ data array error from store" },

	{ "dc.data_eccm:cpb", "addr,priv", opt_dc_data_eccm_cpb, 0,
	"Uncorrectable multi-bit D$ data array error from copyback" },

	{ "dc.data_eccm:snp", "addr,priv", opt_dc_data_eccm_snp, 0,
	"Uncorrectable multi-bit D$ data array error from snoop" },

	{ "dc.data_eccm:scr", "addr,priv", opt_dc_data_eccm_scr, 0,
	"Uncorrectable multi-bit D$ data array error from scrub" },

	{ "dc.tag_par:ld", "addr,priv", opt_dc_tag_par_ld, 0,
	"Main tag array parity from load" },

	{ "dc.tag_par:st", "addr,priv", opt_dc_tag_par_st, 0,
	"Main tag array parity from store" },

	{ "dc.stag_par:snp", "addr,priv", opt_dc_stag_par_snp, 0,
	"Snoop tag array parity from snoop" },

	{ "dc.stag_par:evct", "addr,priv", opt_dc_stag_par_evct, 0,
	"Snoop tag array parity from eviction" },

	{ "dc.l1tlb_par", "addr,priv", opt_dc_l1tlb_par, 0,
	"L1 DTLB parity" },

	{ "dc.l1tlb_par:multi", "addr,priv", opt_dc_l1tlb_par_multi, 0,
	"L1 DTLB parity (multimatch)" },

	{ "dc.l2tlb_par", "addr,priv", opt_dc_l2tlb_par, 0,
	"L2 DTLB parity" },

	{ "dc.l2tlb_par:multi", "addr,priv", opt_dc_l2tlb_par_multi, 0,
	"L2 DTLB parity (multimatch)" },

	/*
	 * Instruction cache
	 */
	{ "ic.inf_sys_ecc1", "addr,priv", opt_ic_inf_sys_ecc1, 0,
	"Correctable D$ data infill from system memory" },

	{ "ic.inf_sys_eccm", "addr,priv", opt_ic_inf_sys_eccm, 0,
	"Uncorrectable D$ data infill from system memory" },

	{ "ic.inf_l2_ecc1", "addr,priv", opt_ic_inf_l2_ecc1, 0,
	"Correctable D$ data infill from L2$" },

	{ "ic.inf_l2_eccm", "addr,priv", opt_ic_inf_l2_eccm, 0,
	"Uncorrectable D$ data infill from L2$" },

	{ "ic.data_par", "addr,priv", opt_ic_data_par, 0,
	"Data array parity" },

	{ "ic.tag_par:evct", "addr,priv", opt_ic_tag_par_evct, 0,
	"Main tag array parity from eviction" },

	{ "ic.stag_par:snp", "addr,priv", opt_ic_stag_par_snp, 0,
	"Snoop tag array parity from snoop" },

	{ "ic.stag_par:evct", "addr,priv", opt_ic_stag_par_evct, 0,
	"Snoop tag array parity from eviction" },

	{ "ic.l1tlb_par", "addr,priv", opt_ic_l1tlb_par, 0,
	"L1 ITLB parity" },

	{ "ic.l1tlb_par:multi", "addr,priv", opt_ic_l1tlb_par_multi, 0,
	"L1 ITLB parity (multimatch)" },

	{ "ic.l2tlb_par", "addr,priv", opt_ic_l2tlb_par, 0,
	"L2 ITLB parity" },

	{ "ic.l2tlb_par:multi", "addr,priv", opt_ic_l2tlb_par_multi, 0,
	"L2 ITLB parity (multimatch)" },

	{ "ic.rdde", "priv", opt_ic_rdde, 0,
	"System Data Read Error" },

	/*
	 * Bus Unit
	 */
	{ "bu.l2d_ecc1:tlb", "addr,priv", opt_bu_l2d_ecc1_tlb, 0,
	"L2$ data array single-bit ECC error from TLB reload" },

	{ "bu.l2d_ecc1:snp", "addr,priv", opt_bu_l2d_ecc1_snp, 0,
	"L2$ data array single-bit ECC error from snoop" },

	{ "bu.l2d_ecc1:cpb", "addr,priv", opt_bu_l2d_ecc1_cpb, 0,
	"L2$ data array single-bit ECC error from copyback" },

	{ "bu.l2d_eccm:tlb", "addr,priv", opt_bu_l2d_eccm_tlb, 0,
	"L2$ data array multi-bit ECC error from TLB reload" },

	{ "bu.l2d_eccm:snp", "addr,priv", opt_bu_l2d_eccm_snp, 0,
	"L2$ data array multi-bit ECC error from snoop" },

	{ "bu.l2d_eccm:cpb", "addr,priv", opt_bu_l2d_eccm_cpb, 0,
	"L2$ data array multi-bit ECC error from copyback" },

	{ "bu.l2t_ecc1", "addr,priv", opt_bu_l2t_ecc1, 0,
	"L2$ tag array single-bit ECC error from scrubber" },

	{ "bu.l2t_eccm", "addr,priv", opt_bu_l2t_eccm, 0,
	"L2$ tag array multi-bit ECC error from scrubber" },

	{ "bu.l2t_par:if", "addr,priv", opt_bu_l2t_par_if, 0,
	"L2$ tag array parity error from I$ fetch" },

	{ "bu.l2t_par:df", "addr,priv", opt_bu_l2t_par_df, 0,
	"L2$ tag array parity error from D$ fetch" },

	{ "bu.l2t_par:tlb", "addr,priv", opt_bu_l2t_par_tlb, 0,
	"L2$ tag array parity error from TLB reload" },

	{ "bu.l2t_par:snp", "addr,priv", opt_bu_l2t_par_snp, 0,
	"L2$ tag array parity error from snoop" },

	{ "bu.l2t_par:cpb", "addr,priv", opt_bu_l2t_par_cpb, 0,
	"L2$ tag array parity error from copyback" },

	{ "bu.l2t_par:scr", "addr,priv", opt_bu_l2t_par_scr, 0,
	"L2$ tag array parity error from scrub" },

	{ "bu.s_ecc1:pf", "priv", opt_bu_s_ecc1_pf, 0,
	"System data single-bit ECC error from hardware prefetch" },

	{ "bu.s_ecc1:tlb", "addr,syndrome,priv", opt_bu_s_ecc1_tlb, 0,
	"System data single-bit ECC error from TLB reload" },

	{ "bu.s_eccm:pf", "priv", opt_bu_s_eccm_pf, 0,
	"System data multi-bit ECC error from hardware prefetch" },

	{ "bu.s_eccm:tlb", "addr,syndrome,priv", opt_bu_s_eccm_tlb, 0,
	"System data multi-bit ECC error from TLB reload" },

	{ "bu.s_rde:pf", "priv", opt_bu_s_rde_pf, 0,
	"System read data error from hardware prefetch" },

	{ "bu.s_rde:tlb", "addr,priv", opt_bu_s_rde_tlb, 0,
	"System read data error from TLB reload" },

	/*
	 * Load/Store Unit
	 */
	{ "ls.s_rde:ld", "addr,priv", opt_ls_s_rde_ld, 0,
	"System read data error from load" },

	{ "ls.s_rde:st", "addr,priv", opt_ls_s_rde_st, 0,
	"System read data error from store" },

	/*
	 * NorthBridge
	 */
	{ "nb.mem_ce:ld", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ce_ld, 0,
	"Correctable ECC error detected by NB on load" },

	{ "nb.mem_ce:st", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ce_st, 0,
	"Correctable ECC error detected by NB on store" },

	{ "nb.mem_ce:scr", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ce_scr, 0,
	"Correctable ECC error detected by NB on scrub" },

	{ "nb.mem_ue:srcld", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ue_srcld, 0,
	"Uncorrectable ECC error detected by NB on load as source" },

	{ "nb.mem_ue:srcst", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ue_srcst, 0,
	"Uncorrectable ECC error detected by NB on store as source" },

	{ "nb.mem_ue:rspld", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ue_rspld, 0,
	"Uncorrectable ECC error detected by NB on load as responder" },

	{ "nb.mem_ue:rspst", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ue_rspst, 0,
	"Uncorrectable ECC error detected by NB on store as responder" },

	{ "nb.mem_ue:scr", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ue_scr, 0,
	"Uncorrectable ECC error detected by NB on scrub" },

	{ "nb.mem_ce:ckld", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ce_ckld, 0,
	"Correctable ChipKill ECC error detected by NB on load" },

	{ "nb.mem_ce:ckst", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ce_ckst, 0,
	"Correctable ChipKill ECC error detected by NB on store" },

	{ "nb.mem_ce:ckscr", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ce_ckscr, 0,
	"Correctable ChipKill ECC error detected by NB on scrub" },

	{ "nb.mem_ue:cksrcld", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ue_cksrcld, 0,
	"Uncorrectable ChipKill ECC error detected by NB on load as source" },

	{ "nb.mem_ue:cksrcst", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ue_cksrcst, 0,
	"Uncorrectable ChipKill ECC error detected by NB on store as source" },

	{ "nb.mem_ue:ckrspld", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ue_ckrspld, 0,
	"Uncorrectable ChipKill ECC error detected by NB on load as resp" },

	{ "nb.mem_ue:ckrspst", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ue_ckrspst, 0,
	"Uncorrectable ChipKill ECC error detected by NB on store as resp" },

	{ "nb.mem_ue:ckscr", "addr,syndrome,syndrome-type,priv",
	opt_nb_mem_ue_ckscr, 0,
	"Uncorrectable ChipKill ECC error detected by NB on scrub" },

	{ "nb.ht_crc", NULL, opt_nb_ht_crc, 0,
	"HyperTransport CRC error detected by NB" },

	{ "nb.ht_sync", NULL, opt_nb_ht_sync, 0,
	"HyperTransport Sync packet error detected by NB" },

	{ "nb.ma:srcld", "addr", opt_nb_ma_srcld, 0,
	"Master Abort detected by NB on load as source" },

	{ "nb.ma:srcst", "addr", opt_nb_ma_srcst, 0,
	"Master Abort detected by NB on store as source" },

	{ "nb.ma:obsld", "addr", opt_nb_ma_obsld, 0,
	"Master Abort detected by NB on load as observer" },

	{ "nb.ma:obsst", "addr", opt_nb_ma_obsst, 0,
	"Master Abort detected by NB on store as observer" },

	{ "nb.ta:srcld", "addr", opt_nb_ta_srcld, 0,
	"Target Abort detected by NB on load as source" },

	{ "nb.ta:srcst", "addr", opt_nb_ta_srcst, 0,
	"Target Abort detected by NB on store as source" },

	{ "nb.ta:obsld", "addr", opt_nb_ta_obsld, 0,
	"Target Abort detected by NB on load as observer" },

	{ "nb.ta:obsst", "addr", opt_nb_ta_obsst, 0,
	"Target Abort detected by NB on store as observer" },

	{ "nb.gart_walk:src", "addr", opt_nb_gart_walk_src, 0,
	"GART Table Walk error detected by NB as source" },

	{ "nb.gart_walk:obs", "addr", opt_nb_gart_walk_obs, 0,
	"GART Table Walk error detected by NB as observer" },

	{ "nb.rmw", "addr", opt_nb_rmw, 0,
	"GART Table Walk error detected by NB" },

	{ "nb.wdog", NULL, opt_nb_wdog, 0,
	"Northbridge watchdog timeout error" },

	{ "nb.dramaddr_par", "channel", opt_nb_dramaddr_par, 0,
	"DRAM Address Parity Error" },		/* revs F/G only */

	{ "nb.raw", "status,addr", opt_nb_raw, 0,
	"Northbridge raw hard-coded error" },

	NULL
};

static const mtst_cpumod_ops_t opt_cpumod_ops = {
	opt_fini
};

static const mtst_cpumod_t opt_cpumod = {
	MTST_CPUMOD_VERSION,
	"AMD Opteron (family 15)",
	&opt_cpumod_ops,
	opt_cmds
};

const mtst_cpumod_t *
_mtst_cpumod_init(void)
{
	return (&opt_cpumod);
}

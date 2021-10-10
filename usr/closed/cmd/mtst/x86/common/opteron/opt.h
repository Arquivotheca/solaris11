/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _OPT_H
#define	_OPT_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>

#include <mtst_cpumod_api.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (opt_injfn_t)(mtst_cpuid_t *, uint_t, const mtst_argspec_t *,
    int, uint64_t);

extern opt_injfn_t opt_dc_inf_sys_ecc1;
extern opt_injfn_t opt_dc_inf_sys_eccm;
extern opt_injfn_t opt_dc_inf_l2_ecc1;
extern opt_injfn_t opt_dc_inf_l2_eccm;
extern opt_injfn_t opt_dc_data_ecc1_uc_ld;
extern opt_injfn_t opt_dc_data_ecc1_uc_st;
extern opt_injfn_t opt_dc_data_ecc1_uc_cpb;
extern opt_injfn_t opt_dc_data_ecc1_uc_snp;
extern opt_injfn_t opt_dc_data_ecc1_scr;
extern opt_injfn_t opt_dc_data_eccm_ld;
extern opt_injfn_t opt_dc_data_eccm_st;
extern opt_injfn_t opt_dc_data_eccm_cpb;
extern opt_injfn_t opt_dc_data_eccm_snp;
extern opt_injfn_t opt_dc_data_eccm_scr;
extern opt_injfn_t opt_dc_tag_par_ld;
extern opt_injfn_t opt_dc_tag_par_st;
extern opt_injfn_t opt_dc_stag_par_snp;
extern opt_injfn_t opt_dc_stag_par_evct;
extern opt_injfn_t opt_dc_l1tlb_par;
extern opt_injfn_t opt_dc_l1tlb_par_multi;
extern opt_injfn_t opt_dc_l2tlb_par;
extern opt_injfn_t opt_dc_l2tlb_par_multi;

extern opt_injfn_t opt_ic_inf_sys_ecc1;
extern opt_injfn_t opt_ic_inf_sys_eccm;
extern opt_injfn_t opt_ic_inf_l2_ecc1;
extern opt_injfn_t opt_ic_inf_l2_eccm;
extern opt_injfn_t opt_ic_data_par;
extern opt_injfn_t opt_ic_tag_par_evct;
extern opt_injfn_t opt_ic_stag_par_snp;
extern opt_injfn_t opt_ic_stag_par_evct;
extern opt_injfn_t opt_ic_l1tlb_par;
extern opt_injfn_t opt_ic_l1tlb_par_multi;
extern opt_injfn_t opt_ic_l2tlb_par;
extern opt_injfn_t opt_ic_l2tlb_par_multi;
extern opt_injfn_t opt_ic_rdde;

extern opt_injfn_t opt_bu_l2d_ecc1_tlb;
extern opt_injfn_t opt_bu_l2d_ecc1_snp;
extern opt_injfn_t opt_bu_l2d_ecc1_cpb;
extern opt_injfn_t opt_bu_l2d_eccm_tlb;
extern opt_injfn_t opt_bu_l2d_eccm_snp;
extern opt_injfn_t opt_bu_l2d_eccm_cpb;
extern opt_injfn_t opt_bu_l2t_ecc1;
extern opt_injfn_t opt_bu_l2t_eccm;
extern opt_injfn_t opt_bu_l2t_par_if;
extern opt_injfn_t opt_bu_l2t_par_df;
extern opt_injfn_t opt_bu_l2t_par_tlb;
extern opt_injfn_t opt_bu_l2t_par_snp;
extern opt_injfn_t opt_bu_l2t_par_cpb;
extern opt_injfn_t opt_bu_l2t_par_scr;
extern opt_injfn_t opt_bu_s_ecc1_pf;
extern opt_injfn_t opt_bu_s_ecc1_tlb;
extern opt_injfn_t opt_bu_s_eccm_pf;
extern opt_injfn_t opt_bu_s_eccm_tlb;
extern opt_injfn_t opt_bu_s_rde_pf;
extern opt_injfn_t opt_bu_s_rde_tlb;

extern opt_injfn_t opt_ls_s_rde_ld;
extern opt_injfn_t opt_ls_s_rde_st;

extern opt_injfn_t opt_nb_mem_ce_ld;
extern opt_injfn_t opt_nb_mem_ce_st;
extern opt_injfn_t opt_nb_mem_ce_scr;
extern opt_injfn_t opt_nb_mem_ue_srcld;
extern opt_injfn_t opt_nb_mem_ue_srcst;
extern opt_injfn_t opt_nb_mem_ue_rspld;
extern opt_injfn_t opt_nb_mem_ue_rspst;
extern opt_injfn_t opt_nb_mem_ue_scr;
extern opt_injfn_t opt_nb_mem_ce_ckld;
extern opt_injfn_t opt_nb_mem_ce_ckst;
extern opt_injfn_t opt_nb_mem_ce_ckscr;
extern opt_injfn_t opt_nb_mem_ue_cksrcld;
extern opt_injfn_t opt_nb_mem_ue_cksrcst;
extern opt_injfn_t opt_nb_mem_ue_ckrspld;
extern opt_injfn_t opt_nb_mem_ue_ckrspst;
extern opt_injfn_t opt_nb_mem_ue_ckscr;
extern opt_injfn_t opt_nb_ht_crc;
extern opt_injfn_t opt_nb_ht_sync;
extern opt_injfn_t opt_nb_ma_srcld;
extern opt_injfn_t opt_nb_ma_srcst;
extern opt_injfn_t opt_nb_ma_obsld;
extern opt_injfn_t opt_nb_ma_obsst;
extern opt_injfn_t opt_nb_ta_srcld;
extern opt_injfn_t opt_nb_ta_srcst;
extern opt_injfn_t opt_nb_ta_obsld;
extern opt_injfn_t opt_nb_ta_obsst;
extern opt_injfn_t opt_nb_gart_walk_src;
extern opt_injfn_t opt_nb_gart_walk_obs;
extern opt_injfn_t opt_nb_rmw;
extern opt_injfn_t opt_nb_wdog;
extern opt_injfn_t opt_nb_dramaddr_par;
extern opt_injfn_t opt_nb_raw;

extern void opt_mis_init_pci(mtst_inj_stmt_t *, int, uint_t, uint_t, uint32_t,
    uint_t);

#define	OPT_SYNDTYPE_ECC	0x1
#define	OPT_SYNDTYPE_CK		0x2


/*
 * We have to define our own mapping between OPT_STAT_x flags and their
 * AMD_BANK_STAT_x equivalents because we need bits that aren't represented
 * in the AMD_BANK_STAT_x namespace.  OPT_STAT_SYNDV, for example, doesn't
 * exist in the AMD_BANK_STAT_x namespace -- it's merely a signal to the
 * injection routine that a syndrome is to be included in the status field.
 */

#define	OPT_STAT_VALID		0x0001
#define	OPT_STAT_EN		0x0002
#define	OPT_STAT_ADDRV		0x0004
#define	OPT_STAT_UC		0x0008
#define	OPT_STAT_CECC		0x0010
#define	OPT_STAT_UECC		0x0020
#define	OPT_STAT_PCC		0x0040
#define	OPT_STAT_SCRUB		0x0080
#define	OPT_STAT_SYNDV		0x8000

#define	OPT_FLAGS_COMMON \
	(OPT_STAT_VALID | OPT_STAT_EN)

#define	OPT_FLAGS_1(f1)	\
	(OPT_FLAGS_COMMON | (OPT_STAT_##f1))
#define	OPT_FLAGS_2(f1, f2) \
	(OPT_FLAGS_COMMON | (OPT_STAT_##f1) | (OPT_STAT_##f2))
#define	OPT_FLAGS_3(f1, f2, f3) \
	(OPT_FLAGS_COMMON | (OPT_STAT_##f1) | (OPT_STAT_##f2) | (OPT_STAT_##f3))
#define	OPT_FLAGS_4(f1, f2, f3, f4) \
	(OPT_FLAGS_COMMON | (OPT_STAT_##f1) | (OPT_STAT_##f2) | \
	(OPT_STAT_##f3) | (OPT_STAT_##f4))
#define	OPT_FLAGS_5(f1, f2, f3, f4, f5) \
	(OPT_FLAGS_COMMON | (OPT_STAT_##f1) | (OPT_STAT_##f2) | \
	(OPT_STAT_##f3) | (OPT_STAT_##f4) | (OPT_STAT_##f5))
#define	OPT_FLAGS_6(f1, f2, f3, f4, f5, f6) \
	(OPT_FLAGS_COMMON | (OPT_STAT_##f1) | (OPT_STAT_##f2) | \
	(OPT_STAT_##f3) | (OPT_STAT_##f4) | (OPT_STAT_##f5) | (OPT_STAT_##f6))

/*
 * Some additional bits for the NB status register
 */
#define	OPT_NBSTAT_CHANNELA	0x0001
#define	OPT_NBSTAT_CHANNELB	0x0002

extern int opt_synthesize_common(mtst_cpuid_t *, uint_t,
    const mtst_argspec_t *, int, uint64_t, uint64_t, uint_t, uint_t,
    uint_t, uint64_t);

/*
 * Rather than typing the same setup function 1000 times, we use the
 * following macros.  Why does every macro end with a prototype?  Without
 * them, an instance of one of these macros couldn't end with a semicolon.
 * Yes, really.
 */
#define	OPT_SYNTHESIZE_TLB(unit, name, ext, sbits, tt, ll) \
int									\
opt_##unit##_##name(mtst_cpuid_t *cpi, uint_t flags,			\
    const mtst_argspec_t *args, int nargs, uint64_t cmdpriv)		\
{									\
	uint64_t errcode = ((ext) << AMD_ERREXT_SHIFT) |		\
	    AMD_ERRCODE_MKTLB(MCAX86_ERRCODE_TT_##tt,			\
	    MCAX86_ERRCODE_LL_##ll);					\
	return (opt_synthesize_common(cpi, flags, args, nargs,	\
	    errcode, sbits, OPT_FUNCUNIT_STATUS_MSR,			\
	    OPT_FUNCUNIT_ADDR_MSR, 0, cmdpriv));			\
}									\
									\
extern int opt_##unit##_##name(mtst_cpuid_t *, uint_t,			\
    const mtst_argspec_t *, int, uint64_t)

#define	OPT_SYNTHESIZE_EXTMEM(unit, name, ext, sbits, r4, tt, ll) \
int									\
opt_##unit##_##name(mtst_cpuid_t *cpi, uint_t flags,			\
    const mtst_argspec_t *args, int nargs, uint64_t cmdpriv)		\
{									\
	uint64_t errcode = ((ext) << AMD_ERREXT_SHIFT) |		\
	    AMD_ERRCODE_MKMEM(MCAX86_ERRCODE_RRRR_##r4,			\
	    MCAX86_ERRCODE_TT_##tt, MCAX86_ERRCODE_LL_##ll);		\
	return (opt_synthesize_common(cpi, flags, args, nargs,	\
	    errcode, sbits, OPT_FUNCUNIT_STATUS_MSR,			\
	    OPT_FUNCUNIT_ADDR_MSR, OPT_SYNDTYPE_ECC, cmdpriv));		\
}									\
									\
extern int opt_##unit##_##name(mtst_cpuid_t *, uint_t,			\
    const mtst_argspec_t *, int, uint64_t)

#define	OPT_SYNTHESIZE_STBUS(unit, name, ext, sbits, pp, t, r4, ii, ll, styp) \
int									\
opt_##unit##_##name(mtst_cpuid_t *cpi, uint_t flags,			\
    const mtst_argspec_t *args, int nargs, uint64_t cmdpriv)		\
{									\
	uint64_t errcode = ((ext) << AMD_ERREXT_SHIFT) |		\
	    AMD_ERRCODE_MKBUS(MCAX86_ERRCODE_PP_##pp,			\
	    MCAX86_ERRCODE_T_##t, MCAX86_ERRCODE_RRRR_##r4,		\
	    MCAX86_ERRCODE_II_##ii, MCAX86_ERRCODE_LL_##ll);		\
	return (opt_synthesize_common(cpi, flags, args, nargs,	\
	    errcode, sbits, OPT_FUNCUNIT_STATUS_MSR,			\
	    OPT_FUNCUNIT_ADDR_MSR, styp, cmdpriv));			\
}									\
									\
extern int opt_##unit##_##name(mtst_cpuid_t *, uint_t,			\
    const mtst_argspec_t *, int, uint64_t)

#define	OPT_SYNTHESIZE_BUS(unit, name, ext, sbits, pp, t, r4, ii, ll) \
	OPT_SYNTHESIZE_STBUS(unit, name, ext, sbits, pp, t, r4, ii, ll,	\
	OPT_SYNDTYPE_ECC)

#ifdef __cplusplus
}
#endif

#endif /* _OPT_H */

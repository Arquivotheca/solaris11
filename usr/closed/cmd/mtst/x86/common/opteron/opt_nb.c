/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * NorthBridge error synthesis routines
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/trap.h>
#include <sys/mca_x86.h>
#include <sys/mca_amd.h>

#include "opt.h"

#define	NB_SYNTHESIZE_BUS(name, ext, sbits, pp, t, r4, ii, ll)	\
	OPT_SYNTHESIZE_STBUS(nb, name, ext, sbits, pp, t, r4, ii, ll, \
	    OPT_SYNDTYPE_ECC)

#define	NB_SYNTHESIZE_CKBUS(name, ext, sbits, pp, t, r4, ii, ll) \
	OPT_SYNTHESIZE_STBUS(nb, name, ext, sbits, pp, t, r4, ii, ll, \
	    OPT_SYNDTYPE_CK)

#define	NB_SYNTHESIZE_TLB(name, ext, sbits, tt, ll) \
	OPT_SYNTHESIZE_TLB(nb, name, ext, sbits, tt, ll)

#define	OPT_FUNCUNIT_STATUS_MSR	AMD_MSR_NB_STATUS
#define	OPT_FUNCUNIT_ADDR_MSR	AMD_MSR_NB_ADDR

NB_SYNTHESIZE_BUS(mem_ce_ld, 0, OPT_FLAGS_3(ADDRV, SYNDV, CECC),
    SRC, NONE, RD, MEM, LG);
NB_SYNTHESIZE_BUS(mem_ce_st, 0, OPT_FLAGS_3(ADDRV, SYNDV, CECC),
    SRC, NONE, WR, MEM, LG);
NB_SYNTHESIZE_BUS(mem_ce_scr, 0, OPT_FLAGS_4(ADDRV, SYNDV, CECC, SCRUB),
    SRC, NONE, RD, MEM, LG);

NB_SYNTHESIZE_BUS(mem_ue_srcld, 0, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, UECC),
    SRC, NONE, RD, MEM, LG);
NB_SYNTHESIZE_BUS(mem_ue_srcst, 0, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, UECC),
    SRC, NONE, RD, MEM, LG);
NB_SYNTHESIZE_BUS(mem_ue_rspld, 0, OPT_FLAGS_4(UC, ADDRV, SYNDV, UECC),
    SRC, NONE, RD, MEM, LG);
NB_SYNTHESIZE_BUS(mem_ue_rspst, 0, OPT_FLAGS_4(UC, ADDRV, SYNDV, UECC),
    SRC, NONE, RD, MEM, LG);
NB_SYNTHESIZE_BUS(mem_ue_scr, 0, OPT_FLAGS_6(UC, ADDRV, PCC, SYNDV, UECC,
    SCRUB), SRC, NONE, RD, MEM, LG);

NB_SYNTHESIZE_CKBUS(mem_ce_ckld, 8, OPT_FLAGS_3(ADDRV, SYNDV, CECC),
    SRC, NONE, RD, MEM, LG);
NB_SYNTHESIZE_CKBUS(mem_ce_ckst, 8, OPT_FLAGS_3(ADDRV, SYNDV, CECC),
    SRC, NONE, WR, MEM, LG);
NB_SYNTHESIZE_CKBUS(mem_ce_ckscr, 8, OPT_FLAGS_3(ADDRV, SYNDV, CECC),
    SRC, NONE, WR, MEM, LG);

NB_SYNTHESIZE_CKBUS(mem_ue_cksrcld, 8, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV,
    UECC), SRC, NONE, RD, MEM, LG);
NB_SYNTHESIZE_CKBUS(mem_ue_cksrcst, 8, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV,
    UECC), SRC, NONE, RD, MEM, LG);
NB_SYNTHESIZE_CKBUS(mem_ue_ckrspld, 8, OPT_FLAGS_4(UC, ADDRV, SYNDV, UECC),
    SRC, NONE, RD, MEM, LG);
NB_SYNTHESIZE_CKBUS(mem_ue_ckrspst, 8, OPT_FLAGS_4(UC, ADDRV, SYNDV, UECC),
    SRC, NONE, RD, MEM, LG);
NB_SYNTHESIZE_CKBUS(mem_ue_ckscr, 8, OPT_FLAGS_6(UC, ADDRV, PCC, SYNDV, UECC,
    SCRUB), SRC, NONE, RD, MEM, LG);

NB_SYNTHESIZE_BUS(ht_crc, 1, OPT_FLAGS_2(UC, PCC),
    OBS, NONE, ERR, GEN, LG);
NB_SYNTHESIZE_BUS(ht_sync, 2, OPT_FLAGS_2(UC, PCC),
    OBS, NONE, ERR, GEN, LG);

NB_SYNTHESIZE_BUS(ma_srcld, 3, OPT_FLAGS_3(UC, ADDRV, PCC),
    OBS, NONE, RD, MEM, LG);
NB_SYNTHESIZE_BUS(ma_srcst, 3, OPT_FLAGS_3(UC, ADDRV, PCC),
    OBS, NONE, WR, MEM, LG);
NB_SYNTHESIZE_BUS(ma_obsld, 3, OPT_FLAGS_2(UC, ADDRV),
    OBS, NONE, RD, MEM, LG);
NB_SYNTHESIZE_BUS(ma_obsst, 3, OPT_FLAGS_2(UC, ADDRV),
    OBS, NONE, WR, MEM, LG);

NB_SYNTHESIZE_BUS(ta_srcld, 4, OPT_FLAGS_3(UC, ADDRV, PCC),
    OBS, NONE, RD, MEM, LG);
NB_SYNTHESIZE_BUS(ta_srcst, 4, OPT_FLAGS_3(UC, ADDRV, PCC),
    OBS, NONE, WR, MEM, LG);
NB_SYNTHESIZE_BUS(ta_obsld, 4, OPT_FLAGS_2(UC, ADDRV),
    OBS, NONE, RD, MEM, LG);
NB_SYNTHESIZE_BUS(ta_obsst, 4, OPT_FLAGS_2(UC, ADDRV),
    OBS, NONE, WR, MEM, LG);

NB_SYNTHESIZE_TLB(gart_walk_src, 5, OPT_FLAGS_3(UC, ADDRV, PCC),
    GEN, LG);
NB_SYNTHESIZE_TLB(gart_walk_obs, 5, OPT_FLAGS_2(UC, ADDRV),
    GEN, LG);

NB_SYNTHESIZE_BUS(rmw, 6, OPT_FLAGS_2(UC, ADDRV),
    OBS, NONE, ERR, IO, LG);

/*
 * At present, we make no attempt to synthesize the MCA NB Address High and Low
 * Registers for our injected Watchdog error.  This limitation should be fixed
 * once we put watchdog diagnosis into place.  See BKDG 3.29 Section 3.6.4.7.
 */
NB_SYNTHESIZE_BUS(wdog, 7, OPT_FLAGS_2(UC, PCC),
    GEN, TIMEOUT, ERR, GEN, LG);

NB_SYNTHESIZE_BUS(dramaddr_par, 13, OPT_FLAGS_2(UC, PCC),
    OBS, NONE, ERR, MEM, LG);

int
opt_nb_raw(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t injarg)
{
	if (nargs != 1 || strcmp(args->mas_argnm, "status") != 0 ||
	    args->mas_argtype != MTST_ARGTYPE_VALUE)
		return (MTST_CMD_USAGE);

	return (opt_synthesize_common(cpi, flags, NULL, 0,
	    args->mas_argval, 0, AMD_MSR_NB_STATUS, AMD_MSR_NB_ADDR, 0,
	    injarg));
}

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Bus Unit error synthesis routines
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/trap.h>
#include <sys/mca_x86.h>
#include <sys/mca_amd.h>

#include "opt.h"

#define	BU_SYNTHESIZE_EXTMEM(name, ext, sbits, r4, tt, ll) \
	OPT_SYNTHESIZE_EXTMEM(bu, name, ext, sbits, r4, tt, ll)

#define	BU_SYNTHESIZE_MEM(name, sbits, r4, tt, ll) \
	OPT_SYNTHESIZE_EXTMEM(bu, name, 0, sbits, r4, tt, ll)

#define	BU_SYNTHESIZE_BUS(name, sbits, pp, t, r4, ii, ll) \
	OPT_SYNTHESIZE_BUS(bu, name, 0, sbits, pp, t, r4, ii, ll)

#define	OPT_FUNCUNIT_STATUS_MSR	AMD_MSR_BU_STATUS
#define	OPT_FUNCUNIT_ADDR_MSR	AMD_MSR_BU_ADDR

BU_SYNTHESIZE_MEM(l2d_ecc1_tlb, OPT_FLAGS_3(ADDRV, SYNDV, CECC),
    RD, GEN, L2);
BU_SYNTHESIZE_MEM(l2d_ecc1_snp, OPT_FLAGS_3(ADDRV, SYNDV, CECC),
    SNOOP, GEN, L2);
BU_SYNTHESIZE_MEM(l2d_ecc1_cpb, OPT_FLAGS_3(ADDRV, SYNDV, CECC),
    EVICT, GEN, L2);
BU_SYNTHESIZE_MEM(l2d_eccm_tlb, OPT_FLAGS_5(UC, PCC, ADDRV, SYNDV, UECC),
    RD, GEN, L2);
BU_SYNTHESIZE_MEM(l2d_eccm_snp, OPT_FLAGS_5(UC, PCC, ADDRV, SYNDV, UECC),
    SNOOP, GEN, L2);
BU_SYNTHESIZE_MEM(l2d_eccm_cpb, OPT_FLAGS_5(UC, PCC, ADDRV, SYNDV, UECC),
    EVICT, GEN, L2);

BU_SYNTHESIZE_EXTMEM(l2t_par_if, 2, OPT_FLAGS_3(UC, ADDRV, PCC),
    IRD, INSTR, L2);
BU_SYNTHESIZE_EXTMEM(l2t_par_df, 2, OPT_FLAGS_3(UC, ADDRV, PCC),
    DRD, DATA, L2);
BU_SYNTHESIZE_EXTMEM(l2t_par_tlb, 2, OPT_FLAGS_3(UC, ADDRV, PCC),
    RD, GEN, L2);
BU_SYNTHESIZE_EXTMEM(l2t_par_snp, 2, OPT_FLAGS_3(UC, ADDRV, PCC),
    SNOOP, GEN, L2);
BU_SYNTHESIZE_EXTMEM(l2t_par_cpb, 2, OPT_FLAGS_3(UC, ADDRV, PCC),
    EVICT, GEN, L2);
BU_SYNTHESIZE_EXTMEM(l2t_par_scr, 2, OPT_FLAGS_3(UC, ADDRV, SCRUB),
    ERR, INSTR, L2);

BU_SYNTHESIZE_EXTMEM(l2t_ecc1, 2, OPT_FLAGS_4(ADDRV, SYNDV, CECC, SCRUB),
    ERR, INSTR, L2);
BU_SYNTHESIZE_EXTMEM(l2t_eccm, 2, OPT_FLAGS_5(UC, ADDRV, SYNDV, UECC, SCRUB),
    ERR, INSTR, L2);

BU_SYNTHESIZE_BUS(s_rde_pf, OPT_FLAGS_2(UC, PCC),
    SRC, NONE, PREFETCH, MEM, LG);
BU_SYNTHESIZE_BUS(s_rde_tlb, OPT_FLAGS_3(UC, ADDRV, PCC),
    SRC, NONE, RD, MEM, LG);

BU_SYNTHESIZE_BUS(s_ecc1_pf, OPT_FLAGS_2(SYNDV, CECC),
    SRC, NONE, PREFETCH, MEM, LG);
BU_SYNTHESIZE_BUS(s_ecc1_tlb, OPT_FLAGS_3(ADDRV, SYNDV, CECC),
    SRC, NONE, RD, MEM, LG);

BU_SYNTHESIZE_BUS(s_eccm_pf, OPT_FLAGS_4(UC, PCC, SYNDV, UECC),
    SRC, NONE, PREFETCH, MEM, LG);
BU_SYNTHESIZE_BUS(s_eccm_tlb, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, UECC),
    SRC, NONE, RD, MEM, LG);

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Data Cache error synthesis routines
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/trap.h>
#include <sys/mca_x86.h>
#include <sys/mca_amd.h>

#include "opt.h"

#define	DC_SYNTHESIZE_TLB(name, ext, sbits, tt, ll) \
	OPT_SYNTHESIZE_TLB(dc, name, ext, sbits, tt, ll)

#define	DC_SYNTHESIZE_MEM(name, sbits, r4, tt, ll) \
	OPT_SYNTHESIZE_EXTMEM(dc, name, 0, sbits, r4, tt, ll)

#define	DC_SYNTHESIZE_BUS(name, sbits, pp, t, r4, ii, ll) \
	OPT_SYNTHESIZE_BUS(dc, name, 0, sbits, pp, t, r4, ii, ll)

#define	OPT_FUNCUNIT_STATUS_MSR	AMD_MSR_DC_STATUS
#define	OPT_FUNCUNIT_ADDR_MSR	AMD_MSR_DC_ADDR

DC_SYNTHESIZE_BUS(inf_sys_ecc1, OPT_FLAGS_3(ADDRV, SYNDV, CECC),
    SRC, NONE, DRD, MEM, LG);
DC_SYNTHESIZE_BUS(inf_sys_eccm, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, UECC),
    SRC, NONE, DRD, MEM, LG);

DC_SYNTHESIZE_MEM(inf_l2_ecc1, OPT_FLAGS_3(ADDRV, SYNDV, CECC),
    DRD, DATA, L2);
DC_SYNTHESIZE_MEM(inf_l2_eccm, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, UECC),
    DRD, DATA, L2);

DC_SYNTHESIZE_MEM(data_ecc1_scr, OPT_FLAGS_4(ADDRV, SYNDV, CECC, SCRUB),
    ERR, DATA, L1);
DC_SYNTHESIZE_MEM(data_ecc1_uc_ld, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, CECC),
    DRD, DATA, L1);
DC_SYNTHESIZE_MEM(data_ecc1_uc_st, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, CECC),
    DWR, DATA, L1);
DC_SYNTHESIZE_MEM(data_ecc1_uc_cpb, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, CECC),
    EVICT, DATA, L1);
DC_SYNTHESIZE_MEM(data_ecc1_uc_snp, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, CECC),
    SNOOP, DATA, L1);

DC_SYNTHESIZE_MEM(data_eccm_ld, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, UECC),
    DRD, DATA, L1);
DC_SYNTHESIZE_MEM(data_eccm_st, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, UECC),
    DRD, DATA, L1);
DC_SYNTHESIZE_MEM(data_eccm_cpb, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, UECC),
    DRD, DATA, L1);
DC_SYNTHESIZE_MEM(data_eccm_snp, OPT_FLAGS_5(UC, ADDRV, PCC, SYNDV, UECC),
    DRD, DATA, L1);
DC_SYNTHESIZE_MEM(data_eccm_scr, OPT_FLAGS_5(UC, ADDRV, SYNDV, UECC, SCRUB),
    ERR, DATA, L1);

DC_SYNTHESIZE_MEM(tag_par_ld, OPT_FLAGS_3(UC, ADDRV, PCC),
    DRD, DATA, L1);
DC_SYNTHESIZE_MEM(tag_par_st, OPT_FLAGS_3(UC, ADDRV, PCC),
    DWR, DATA, L1);

DC_SYNTHESIZE_MEM(stag_par_snp, OPT_FLAGS_3(UC, ADDRV, PCC),
    SNOOP, DATA, L1);
DC_SYNTHESIZE_MEM(stag_par_evct, OPT_FLAGS_3(UC, ADDRV, PCC),
    EVICT, DATA, L1);

DC_SYNTHESIZE_TLB(l1tlb_par, 0, OPT_FLAGS_3(UC, ADDRV, PCC),
    DATA, L1);
DC_SYNTHESIZE_TLB(l1tlb_par_multi, 1, OPT_FLAGS_3(UC, ADDRV, PCC),
    DATA, L1);
DC_SYNTHESIZE_TLB(l2tlb_par, 0, OPT_FLAGS_3(UC, ADDRV, PCC),
    DATA, L2);
DC_SYNTHESIZE_TLB(l2tlb_par_multi, 1, OPT_FLAGS_3(UC, ADDRV, PCC),
    DATA, L2);

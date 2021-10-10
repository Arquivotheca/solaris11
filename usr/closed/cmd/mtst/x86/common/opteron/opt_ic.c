/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Instruction Cache error synthesis routines
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/trap.h>
#include <sys/mca_x86.h>
#include <sys/mca_amd.h>

#include "opt.h"

#define	IC_SYNTHESIZE_TLB(name, ext, sbits, tt, ll) \
	OPT_SYNTHESIZE_TLB(ic, name, ext, sbits, tt, ll)

#define	IC_SYNTHESIZE_MEM(name, sbits, r4, tt, ll) \
	OPT_SYNTHESIZE_EXTMEM(ic, name, 0, sbits, r4, tt, ll)

#define	IC_SYNTHESIZE_BUS(name, sbits, pp, t, r4, ii, ll) \
	OPT_SYNTHESIZE_BUS(ic, name, 0, sbits, pp, t, r4, ii, ll)

#define	OPT_FUNCUNIT_STATUS_MSR	AMD_MSR_IC_STATUS
#define	OPT_FUNCUNIT_ADDR_MSR	AMD_MSR_IC_ADDR

IC_SYNTHESIZE_BUS(inf_sys_ecc1, OPT_FLAGS_2(ADDRV, CECC),
    SRC, NONE, IRD, MEM, LG);
IC_SYNTHESIZE_BUS(inf_sys_eccm, OPT_FLAGS_4(UC, ADDRV, PCC, UECC),
    SRC, NONE, IRD, MEM, LG);

IC_SYNTHESIZE_MEM(inf_l2_ecc1, OPT_FLAGS_2(ADDRV, CECC),
    IRD, INSTR, L2);
IC_SYNTHESIZE_MEM(inf_l2_eccm, OPT_FLAGS_4(UC, ADDRV, PCC, UECC),
    IRD, INSTR, L2);

IC_SYNTHESIZE_MEM(data_par, OPT_FLAGS_1(ADDRV),
    IRD, INSTR, L1);

IC_SYNTHESIZE_MEM(tag_par_evct, OPT_FLAGS_COMMON,
    EVICT, INSTR, L1);

IC_SYNTHESIZE_MEM(stag_par_snp, OPT_FLAGS_3(UC, ADDRV, PCC),
    SNOOP, INSTR, L1);
IC_SYNTHESIZE_MEM(stag_par_evct, OPT_FLAGS_2(UC, PCC),
    EVICT, INSTR, L1);

IC_SYNTHESIZE_TLB(l1tlb_par, 0, OPT_FLAGS_1(ADDRV),
    INSTR, L1);
IC_SYNTHESIZE_TLB(l1tlb_par_multi, 1, OPT_FLAGS_1(ADDRV),
    INSTR, L1);
IC_SYNTHESIZE_TLB(l2tlb_par, 0, OPT_FLAGS_1(ADDRV),
    INSTR, L2);
IC_SYNTHESIZE_TLB(l2tlb_par_multi, 1, OPT_FLAGS_1(ADDRV),
    INSTR, L2);

IC_SYNTHESIZE_BUS(rdde, OPT_FLAGS_1(UC),
    SRC, NONE, IRD, MEM, LG);

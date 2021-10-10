/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Load/Store Unit error synthesis routines
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/trap.h>
#include <sys/mca_x86.h>
#include <sys/mca_amd.h>

#include "opt.h"

#define	LS_SYNTHESIZE_BUS(name, sbits, pp, t, r4, ii, ll) \
	OPT_SYNTHESIZE_BUS(ls, name, 0, sbits, pp, t, r4, ii, ll)

#define	OPT_FUNCUNIT_STATUS_MSR	AMD_MSR_LS_STATUS
#define	OPT_FUNCUNIT_ADDR_MSR	AMD_MSR_LS_ADDR

LS_SYNTHESIZE_BUS(s_rde_ld, OPT_FLAGS_1(UC),
    SRC, NONE, RD, MEM, LG);
LS_SYNTHESIZE_BUS(s_rde_st, OPT_FLAGS_1(UC),
    SRC, NONE, WR, MEM, LG);

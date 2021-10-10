/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/fm/cpu/GMCA.h>

#include "gcpu.h"

/*
 * The following command names need to be kept in sync with the ereport
 * names in <sys/fm/cpu/GCPU.h> - we should be able to inject every ereport
 * that we can generate using a command of matching name.  We can also
 * inject some "raw" data here.
 *
 */
#define	_GCPU_PFX FM_EREPORT_CPU_GENERIC
#define	_GCPU_CMDNAME(s) _GCPU_PFX "." s
#define	_GCPU_NAMECOMP(sfx) FM_EREPORT_CPU_GENERIC_##sfx

/*
 * Command options.
 */
#define	_GCPU_REQOPT "*bank"
#define	_GCPU_OPTOPT  "en,over,uc,pcc,addrv,privaddr,tbes,otherinfo," \
	"ripv,mserrcode,addr,misc"
#define	_GCPU_CMNOPT _GCPU_REQOPT "," _GCPU_OPTOPT
#define	_GCPU_OPTS(cmdextra) _GCPU_REQOPT "," cmdextra "," _GCPU_OPTOPT

static const mtst_cmd_t gcpu_cmds[] = {
	/*
	 * Raw value injection
	 */
	{ _GCPU_CMDNAME("raw"), _GCPU_OPTS("*status"),
	    gcpu_synthesize_cmn, 0,
	    "Inject raw values into MCi_{STATUS,ADDR,MISC} of a bank" },

	/*
	 * Simple error types.  We generate a status value with the
	 * appropriate error code but do not otherwise set bits such
	 * as UC or PCC which may well apply to many of these error
	 * is they occured for real.  Use the command options to
	 * add those in where desired.  For internal unclassified
	 * errors at least one of the lower 10 bits of the error code
	 * must be set, so we constrict an error code with bit 0 set;
	 * again a command line option ("unclassval") can specify a
	 * different value.
	 */
#define	_GCPU_SIMPLE(s) _GCPU_CMDNAME(_GCPU_NAMECOMP(s))

	{ _GCPU_SIMPLE(UNCLASSIFIED), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, MCAX86_SIMPLE_UNCLASSIFIED_MASKON,
	    "'Unclassified' simple error encoding" },

	{ _GCPU_SIMPLE(MC_CODE_PARITY), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, MCAX86_SIMPLE_MC_CODE_PARITY_MASKON,
	    "'Microcode ROM Parity Error' simple error encoding" },

	{ _GCPU_SIMPLE(EXTERNAL), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, MCAX86_SIMPLE_EXTERNAL_MASKON,
	    "'BINIT# from another processor' simple error encoding" },

	{ _GCPU_SIMPLE(FRC), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, MCAX86_SIMPLE_FRC_MASKON,
	    "'Functional Redundancy Check' simple error encoding" },

	{ _GCPU_SIMPLE(INTERNAL_TIMER), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, MCAX86_SIMPLE_INTERNAL_TIMER_MASKON,
	    "'Internal timer error' simple error encoding" },

	{ _GCPU_SIMPLE(INTERNAL_UNCLASS), _GCPU_OPTS("unclassval"),
	    gcpu_synthesize_cmn, MCAX86_MKERRCODE_INTERNAL_UNCLASS(0x1),
	    "'Internal Unclassified' simple error encoding" },

	/*
	 * Compound errors: raw
	 */
	{ _GCPU_CMDNAME("rawcompound_gen_memhier"), _GCPU_OPTS("*LL"),
	    gcpu_synthesize_cmn, MCAX86_MKERRCODE_GENERIC_MEMHIER(0),
	    "Raw compound error - generic memory hierarchy" },
	{ _GCPU_CMDNAME("rawcompound_tlb"), _GCPU_OPTS("*TT,*LL"),
	    gcpu_synthesize_cmn, MCAX86_MKERRCODE_TLB(0, 0),
	    "Raw compound error - TLB" },
	{ _GCPU_CMDNAME("rawcompound_memhier"), _GCPU_OPTS("*TT,*LL,*RRRR"),
	    gcpu_synthesize_cmn, MCAX86_MKERRCODE_MEMHIER(0, 0, 0),
	    "Raw compound error - memory hierarchy" },
	{ _GCPU_CMDNAME("rawcompound_businterconnect"),
	    _GCPU_OPTS("*LL,*PP,*RRRR,*II,*T"),
	    gcpu_synthesize_cmn,
	    MCAX86_MKERRCODE_BUS_INTERCONNECT(0, 0, 0, 0, 0),
	    "Raw compound error - bus and interconnect" },

	/*
	 * Compound errors: Generic Memory Hierarchy
	 */
#define	_GCPU_CMDNAME_GENMEMHIER(ll) \
	(_GCPU_CMDNAME(_GCPU_NAMECOMP(ll) "cache"))
#define	_GCPU_MKGENMEMHIER(ll) \
	MCAX86_MKERRCODE_GENERIC_MEMHIER(MCAX86_ERRCODE_##ll)

	{ _GCPU_CMDNAME_GENMEMHIER(LL_L0), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKGENMEMHIER(LL_L0),
	    "Generic Memory Hierarchy (CPU Cache) Compound Error L0" },
	{ _GCPU_CMDNAME_GENMEMHIER(LL_L1), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKGENMEMHIER(LL_L1),
	    "Generic Memory Hierarchy (CPU Cache) Compound Error L1" },
	{ _GCPU_CMDNAME_GENMEMHIER(LL_L2), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKGENMEMHIER(LL_L2),
	    "Generic Memory Hierarchy (CPU Cache) Compound Error L2" },
	{ _GCPU_CMDNAME_GENMEMHIER(LL_LG), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKGENMEMHIER(LL_LG),
	    "Generic Memory Hierarchy (CPU Cache) Compound Error LG" },

	/*
	 * Compound errors: TLB Errors
	 */
#define	_GCPU_CMDNAME_TLB(tt, ll) \
	(_GCPU_CMDNAME(_GCPU_NAMECOMP(ll) _GCPU_NAMECOMP(tt) "tlb"))
#define	_GCPU_MKTLB(tt, ll) \
	MCAX86_MKERRCODE_TLB(MCAX86_ERRCODE_##tt, MCAX86_ERRCODE_##ll)

	{ _GCPU_CMDNAME_TLB(TT_INSTR, LL_L0), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_INSTR, LL_L0),
	    "L0 Instruction TLB Error" },
	{ _GCPU_CMDNAME_TLB(TT_INSTR, LL_L1), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_INSTR, LL_L1),
	    "L1 Instruction TLB Error" },
	{ _GCPU_CMDNAME_TLB(TT_INSTR, LL_L2), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_INSTR, LL_L2),
	    "L2 Instruction TLB Error" },
	{ _GCPU_CMDNAME_TLB(TT_INSTR, LL_LG), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_INSTR, LL_LG),
	    "LG Instruction TLB Error" },
	{ _GCPU_CMDNAME_TLB(TT_DATA, LL_L0), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_DATA, LL_L0),
	    "L0 Data TLB Error" },
	{ _GCPU_CMDNAME_TLB(TT_DATA, LL_L1), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_DATA, LL_L1),
	    "L1 Data TLB Error" },
	{ _GCPU_CMDNAME_TLB(TT_DATA, LL_L2), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_DATA, LL_L2),
	    "L2 Data TLB Error" },
	{ _GCPU_CMDNAME_TLB(TT_DATA, LL_LG), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_DATA, LL_LG),
	    "LG Data TLB Error" },
	{ _GCPU_CMDNAME_TLB(TT_GEN, LL_L0), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_GEN, LL_L0),
	    "Generic L0 TLB Error" },
	{ _GCPU_CMDNAME_TLB(TT_GEN, LL_L1), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_GEN, LL_L1),
	    "Generic L1 TLB Error" },
	{ _GCPU_CMDNAME_TLB(TT_GEN, LL_L2), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_GEN, LL_L2),
	    "Generic L2 TLB Error" },
	{ _GCPU_CMDNAME_TLB(TT_GEN, LL_LG), _GCPU_CMNOPT,
	    gcpu_synthesize_cmn, _GCPU_MKTLB(TT_GEN, LL_LG),
	    "Generic LG TLB Error" },

	/*
	 * Compound errors: Memory Hierarchy
	 */
#define	_GCPU_CMDNAME_MEMHIER(tt, ll) \
	_GCPU_CMDNAME(_GCPU_NAMECOMP(ll) _GCPU_NAMECOMP(tt) "cache")
#define	_GCPU_MKMEMHIER(tt, ll) \
	MCAX86_MKERRCODE_MEMHIER(MCAX86_ERRCODE_RRRR_DRD, \
	MCAX86_ERRCODE_##tt, MCAX86_ERRCODE_##ll)

	{ _GCPU_CMDNAME_MEMHIER(TT_INSTR, LL_L0), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_INSTR, LL_L0),
	    "L0 Instruction Cache Error" },
	{ _GCPU_CMDNAME_MEMHIER(TT_INSTR, LL_L1), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_INSTR, LL_L1),
	    "L1 Instruction Cache Error" },
	{ _GCPU_CMDNAME_MEMHIER(TT_INSTR, LL_L2), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_INSTR, LL_L2),
	    "L2 Instruction Cache Error" },
	{ _GCPU_CMDNAME_MEMHIER(TT_INSTR, LL_LG), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_INSTR, LL_LG),
	    "LG Instruction Cache Error" },
	{ _GCPU_CMDNAME_MEMHIER(TT_DATA, LL_L0), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_DATA, LL_L0),
	    "L0 Data Cache Error" },
	{ _GCPU_CMDNAME_MEMHIER(TT_DATA, LL_L1), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_DATA, LL_L1),
	    "L1 Data Cache Error" },
	{ _GCPU_CMDNAME_MEMHIER(TT_DATA, LL_L2), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_DATA, LL_L2),
	    "L2 Data Cache Error" },
	{ _GCPU_CMDNAME_MEMHIER(TT_DATA, LL_LG), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_DATA, LL_LG),
	    "LG Data Cache Error" },
	{ _GCPU_CMDNAME_MEMHIER(TT_GEN, LL_L0), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_GEN, LL_L0),
	    "Generic L0 Cache Error" },
	{ _GCPU_CMDNAME_MEMHIER(TT_GEN, LL_L1), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_GEN, LL_L1),
	    "Generic L1 Cache Error" },
	{ _GCPU_CMDNAME_MEMHIER(TT_GEN, LL_L2), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_GEN, LL_L2),
	    "Generic L2 Cache Error" },
	{ _GCPU_CMDNAME_MEMHIER(TT_GEN, LL_LG), _GCPU_OPTS("RRRR"),
	    gcpu_synthesize_cmn, _GCPU_MKMEMHIER(TT_GEN, LL_LG),
	    "Generic LG Cache Error" },

	/*
	 * Compound errors: Bus and Interconnect
	 */
#define	_GCPU_CMDNAME_BUS(ii) \
	_GCPU_CMDNAME("bus_interconnect" _GCPU_NAMECOMP(ii))
#define	_GCPU_MKBUS(rrrr, ii) \
	MCAX86_MKERRCODE_BUS_INTERCONNECT(MCAX86_ERRCODE_PP_SRC, \
	MCAX86_ERRCODE_T_NONE, MCAX86_ERRCODE_##rrrr, \
	MCAX86_ERRCODE_##ii, MCAX86_ERRCODE_LL_LG)

	{ _GCPU_CMDNAME_BUS(II_MEM), _GCPU_OPTS("PP,T,RRRR,LL"),
	    gcpu_synthesize_cmn,
	    _GCPU_MKBUS(RRRR_RD, II_MEM) | MSR_MC_STATUS_ADDRV,
	    "Bus/interconnect error - memory" },
	{ _GCPU_CMDNAME_BUS(II_IO), _GCPU_OPTS("PP,T,RRRR,LL"),
	    gcpu_synthesize_cmn, _GCPU_MKBUS(RRRR_RD, II_IO),
	    "Bus/interconnect error - I/O" },
	{ _GCPU_CMDNAME_BUS(II_GEN), _GCPU_OPTS("PP,T,RRRR,LL"),
	    gcpu_synthesize_cmn, _GCPU_MKBUS(RRRR_ERR, II_GEN),
	    "Bus/interconnect error - other" },

	/*
	 * Add new commands above this line
	 */
	{ NULL, NULL, NULL, NULL, NULL }
};

static void
gcpu_fini(void)
{
}

static const mtst_cpumod_ops_t gcpu_cpumod_ops = {
	gcpu_fini
};

static const mtst_cpumod_t gcpu_cpumod = {
	MTST_CPUMOD_VERSION,
	"Generic x86",
	&gcpu_cpumod_ops,
	gcpu_cmds
};

const mtst_cpumod_t *
_mtst_cpumod_init(void)
{
	return (&gcpu_cpumod);
}

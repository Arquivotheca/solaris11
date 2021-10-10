/*
 * INTEL CONFIDENTIAL
 * SPECIAL INTEL MODIFICATIONS
 *
 * Copyright 2008 Intel Corporation All Rights Reserved.
 *
 * Approved for Solaris or OpenSolaris use only.
 * Approved for binary distribution only.
 *
 */

#ifndef	_5000MTRANS_H
#define	_5000MTRANS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nb_log.h"

#define	BIT0    0x00000001ull
#define	BIT1    0x00000002ull
#define	BIT2    0x00000004ull
#define	BIT3    0x00000008ull
#define	BIT4    0x00000010ull
#define	BIT5    0x00000020ull
#define	BIT6    0x00000040ull
#define	BIT7    0x00000080ull
#define	BIT8    0x00000100ull
#define	BIT9    0x00000200ull
#define	BIT10   0x00000400ull
#define	BIT11   0x00000800ull
#define	BIT12   0x00001000ull
#define	BIT13   0x00002000ull
#define	BIT14   0x00004000ull
#define	BIT15   0x00008000ull
#define	BIT16   0x00010000ull
#define	BIT17   0x00020000ull
#define	BIT18   0x00040000ull
#define	BIT19   0x00080000ull
#define	BIT20   0x00100000ull
#define	BIT21   0x00200000ull
#define	BIT22   0x00400000ull
#define	BIT23   0x00800000ull
#define	BIT24   0x01000000ull
#define	BIT25   0x02000000ull
#define	BIT26   0x04000000ull
#define	BIT27   0x08000000ull
#define	BIT28   0x10000000ull
#define	BIT29   0x20000000ull
#define	BIT30   0x40000000ull
#define	BIT31   0x80000000ull
#define	BIT32   0x100000000ull
#define	BIT33   0x200000000ull

#define	CGL_FOURGIG 		0x100000000ULL
#define	CGL_CACHELINE  		0x40ULL
#define	CGL_CACHELINE_MASK	0xffffffffffffffc0ULL
#define	DRAM_GRANULARITY_MASK	0xfffffffULL
#define	DRAM_GRANULARITY	28

#define	MAX_DMIR_INTERLEAVE	0x4
#define	DMIR_MAX_RANK_NUMBER	32
#define	DMIR_LIMIT_MASK	((nb_chipset == INTEL_NB_5000P || \
	nb_chipset == INTEL_NB_5000V || \
	nb_chipset == INTEL_NB_5000X || \
	nb_chipset == INTEL_NB_5000Z) ?  0xff : 0x7ff)
#define	MAX_DMIR_NUMBER	7
#define	MAX_MIR_NUMBER	0x3
#define	MAX_MIR_INTERLEAVE	0x2

#define	DMIR_LIMIT(reg)	((nb_chipset == INTEL_NB_5000P || \
	nb_chipset == INTEL_NB_5000V || \
	nb_chipset == INTEL_NB_5000X || \
	nb_chipset == INTEL_NB_5000Z) ? \
	((reg >> 16) & 0xff) :  ((reg >> 16) & 0x7ff))


#endif	/* _5000MTRANS_H */

#ifdef __cplusplus
}
#endif

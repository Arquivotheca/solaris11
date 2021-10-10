/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_U_H
#define	_MEMTEST_U_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * CPU/Memory error injector sun4u driver header file.
 */

#include <sys/memtest.h>

/*
 * Required sun4u definitions not found in any kernel header file.
 */

/*
 * L2$ size definitions.
 */
#define	EC_SIZE_HALF_MB		0x080000
#define	EC_SIZE_1MB		0x100000
#define	EC_SIZE_2MB		0x200000
#define	EC_SIZE_4MB		0x400000
#define	EC_SIZE_8MB		0x800000

/*
 * Dispatch Control Register (DCR) definitions.
 */
#define	DCR_IPE		0x0000000000000004ULL
#define	DCR_DPE		0x0000000000001000ULL
#define	DCR_ITPE	0x0000000000010000ULL	/* I-TLB Parity bit<16> */
#define	DCR_DTPE	0x0000000000020000ULL	/* D-TLB Parity bit<17> */

/*
 * Definitions associated with the E$ Control Register (ECCR).
 */
#define	ECCR_SIZE_SHIFT		13		/* e$ size bit shift */
#define	ECCR_SIZE_MASK		0x3		/* e$ size bit mask */
#define	ECCR_ASSOC_SHIFT	24		/* e$ assoc bit shift */
#define	ECCR_ASSOC_MASK		1		/* e$ assoc bit mask */
#define	ECCR_ET_ECC_EN		0x02000000ULL	/* enable ecc on tag */
#define	ECCR_EC_PAR_EN		0x00000800ULL	/* enable parity on tag */
#define	ECCR_EC_ECC_EN		0x00000400ULL	/* enable ecc on data */
#define	ECCR_SIZE(x)		((x >> ECCR_SIZE_SHIFT) & ECCR_SIZE_MASK)
#define	ECCR_ASSOC(x)		((x >> ECCR_ASSOC_SHIFT) & ECCR_ASSOC_MASK)

/*
 * Macros for calling the sun4u operation vector (opsvec) routines.
 *
 * The common, sun4u, and sun4v opsvec tables are defined in memtest.h.
 */
#define	OP_GEN_ECC(mdatap, paddr) \
		((mdatap)->m_sopvp->op_gen_ecc)((paddr))

#define	OP_INJECT_DTPHYS(mdatap) \
		((mdatap)->m_sopvp->op_inject_dtphys)((mdatap))

#define	OP_INJECT_ITPHYS(mdatap) \
		((mdatap)->m_sopvp->op_inject_itphys)((mdatap))

#define	OP_INJECT_L2TPHYS(mdatap) \
		((mdatap)->m_sopvp->op_inject_l2tphys)((mdatap))

#define	OP_INJECT_UMEMORY(mdatap, paddr, vaddr, ecc) \
		((mdatap)->m_sopvp->op_inject_memory) \
		((mdatap), (paddr), (vaddr), (ecc))

#define	OP_INJECT_MTAG(mdatap, paddr, vaddr) \
		((mdatap)->m_sopvp->op_inject_mtag)((mdatap), (paddr), (vaddr))

#define	OP_INJECT_PC(mdatap) \
		((mdatap)->m_sopvp->op_inject_pc) ((mdatap))

/*
 * The following are the sun4u definitions for the memtest_flags global var
 * (declared in the common file memtest.h) which controls certain internal
 * operations of the error injector.
 */
#define	MFLAGS_CH_USE_FLUSH_WC			(MFLAGS_COMMON_MAX << 1)
#define	MFLAGS_CH_DISABLE_WC			(MFLAGS_COMMON_MAX << 2)

/*
 * Test routines located in memtest_u.c.
 */
extern	int		memtest_k_pcache_err(mdata_t *);
extern	int		memtest_dtphys(mdata_t *);
extern	int		memtest_itphys(mdata_t *);
extern	int		memtest_l2tphys(mdata_t *);

/* Second level test routines. */
extern	int		memtest_inject_dtphys(mdata_t *);
extern	int		memtest_inject_itphys(mdata_t *);
extern	int		memtest_inject_l2tphys(mdata_t *);

/*
 * Support routines located in memtest_u.c
 */
extern	int		gen_flushall_ic(cpu_info_t *);
extern	int		gen_flushall_l2(cpu_info_t *);
extern	int		gen_flush_ic_entry(cpu_info_t *, caddr_t);
extern	int		gen_flush_l2_entry(cpu_info_t *, caddr_t);

extern	int		memtest_check_afsr(mdata_t *, char *);
extern	int		memtest_conv_misc(uint64_t);
extern	int		memtest_dc_is_enabled(void);
extern	void		memtest_disable_dc(void);
extern	void		memtest_disable_ic(void);
extern	void		memtest_enable_dc(void);
extern	void		memtest_enable_ic(void);
extern	int		memtest_ic_is_enabled(void);

/*
 * Routines located in memtest_u_asm.s.
 */
extern	int		gen_flush_l2(uint64_t, int);
extern	uint64_t	memtest_get_afar(void);
extern	uint64_t	memtest_get_afsr(void);
extern	uint64_t	memtest_get_afsr_ext(void);
extern	uint64_t	memtest_get_dcr(void);
extern	uint64_t	memtest_get_dcucr(void);
extern	uint64_t	memtest_get_eer(void);
extern	uint64_t	memtest_get_cpu_ver_asm(void);

extern	uint64_t	memtest_set_afsr(uint64_t);
extern	uint64_t	memtest_set_afsr_ext(uint64_t);
extern	uint64_t	memtest_set_dcr(uint64_t);
extern	uint64_t	memtest_set_dcucr(uint64_t);
extern	uint64_t	memtest_set_ecr(uint64_t);
extern	uint64_t	memtest_set_eer(uint64_t);

/*
 * Sun4u generic error types (commands supported by most sun4u cpus).
 */
static cmd_t sun4u_generic_cmds[] = {
	G4U_KD_UE,		memtest_k_mem_err,	"memtest_k_mem_err",
	G4U_KI_UE,		memtest_k_mem_err,	"memtest_k_mem_err",
	G4U_UD_UE,		memtest_u_mem_err,	"memtest_u_mem_err",
	G4U_UI_UE,		memtest_u_mem_err,	"memtest_u_mem_err",
	G4U_IO_UE,		memtest_u_mem_err,	"memtest_u_mem_err",
	G4U_KD_CE,		memtest_k_mem_err,	"memtest_k_mem_err",
	G4U_KD_CETL1,		memtest_k_mem_err,	"memtest_k_mem_err",
	G4U_KD_CESTORM,		memtest_k_mem_err,	"memtest_k_mem_err",
	G4U_KI_CE,		memtest_k_mem_err,	"memtest_k_mem_err",
	G4U_KI_CETL1,		memtest_k_mem_err,	"memtest_k_mem_err",
	G4U_UD_CE,		memtest_u_mem_err,	"memtest_u_mem_err",
	G4U_UI_CE,		memtest_u_mem_err,	"memtest_u_mem_err",
	G4U_IO_CE,		memtest_u_mem_err,	"memtest_u_mem_err",
	G4U_MPHYS,		memtest_mphys,		"memtest_mphys",
	G4U_KMVIRT,		memtest_k_mvirt,	"memtest_k_mvirt",
	G4U_KMPEEK,		memtest_k_mpeekpoke,	"memtest_k_mpeekpoke",
	G4U_KMPOKE,		memtest_k_mpeekpoke,	"memtest_k_mpeekpoke",

	G4U_DPHYS,		memtest_dphys,		"memtest_dphys",
	G4U_IPHYS,		memtest_iphys,		"memtest_iphys",
	G4U_L2PHYS,		memtest_l2phys,		"memtest_l2phys",
	G4U_L2TPHYS,		memtest_l2tphys,	"memtest_l2tphys",
	G4U_KL2VIRT,		memtest_k_l2virt,	"memtest_k_l2virt",

	NULL,			NULL,			NULL,
};

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_U_H */

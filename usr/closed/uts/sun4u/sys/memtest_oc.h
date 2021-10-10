/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_OC_H
#define	_MEMTEST_OC_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * MC-provided error injection routine and its flags
 */

int	(*mc_inject_err)(int, uint64_t, uint32_t);

#define	OC_FLAG_PTRL	(MC_INJECT_FLAG_RESET | MC_INJECT_FLAG_RESTART \
			| MC_INJECT_FLAG_POLL)
#define	OC_FLAG_MI_CE	(MC_INJECT_FLAG_RESET | MC_INJECT_FLAG_POLL \
			| MC_INJECT_FLAG_LD)
#define	OC_FLAG_MI_UE	(OC_FLAG_MI_CE | MC_INJECT_FLAG_NO_TRAP)

/* Just inject an error w/out triggering it. */
#define	OC_FLAG_MPHYS	(MC_INJECT_FLAG_RESET)

/* Sync_UE always causes the system to panic */
#define	OC_FLAG_SYNC_UE	(MC_INJECT_FLAG_RESET | MC_INJECT_FLAG_LD)

/* User_UE doesn't do load. */
#define	OC_FLAG_USER_UE	(MC_INJECT_FLAG_RESET)

#define	OC_FLAG_UE_TL1	(MC_INJECT_FLAG_RESET)

#define	OC_FLAG_CPU	0
#define	OC_FLAG_CMP	OC_FLAG_PTRL

/*
 * Routines located in memtest_oc.c.
 */
extern	void	oc_init(mdata_t *);
extern	int 	oc_inject_memory(mdata_t *, uint64_t, caddr_t, uint_t);
extern	void	oc_resume_xtfunc(uint64_t, uint64_t);
extern	void	oc_suspend_xtfunc(uint64_t, uint64_t);
extern	void	oc_sys_trap(uint64_t, uint64_t);
extern	void	oc_xc_chip(processorid_t, int);
extern	void	oc_xc_core(processorid_t, int, int);
extern	void	oc_xc_cpu(processorid_t, int);

/*
 * Routines located in memtest_oc_asm.s.
 */
extern	void	oc_dtlb_ld(caddr_t, int, tte_t *);
extern	int	oc_get_sec_ctx(void);
extern	void	oc_inj_err_rn(void);
extern	void	oc_inv_err_rn(void);
extern	void	oc_inv_l12uetl1(caddr_t, uint_t);
extern	void	oc_itlb_ld(caddr_t, int, tte_t *);
extern	void	oc_load(caddr_t, uint_t);
extern	void	oc_set_err_injct_l1d(void);
extern	void	oc_set_err_injct_sx(void);
extern	void	oc_susp(void);

/*
 * OC commands structure.
 */
extern	cmd_t	olympusc_cmds[];

/*
 * Macros and defs
 */
#define	OC_STRAND_SHIFT			0x1
#define	OC_STRAND_MASK			0x1
#define	OC_CORE_MASK			0x6
#define	OC_CHIP_MASK			0x7

#define	OC_CORE_ID(cpuid)		(((cpuid) & OC_CORE_MASK) \
					>> OC_STRAND_SHIFT)
#define	OC_SIBLING_STRAND(cpuid)	((cpuid) ^ OC_STRAND_MASK)
#define	OC_CORE_CPU0(cpuid, coreid)	(((cpuid) & ~(OC_CHIP_MASK))\
					| ((coreid) << OC_STRAND_SHIFT))

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_OC_H */

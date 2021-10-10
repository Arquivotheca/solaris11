/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_JA_H
#define	_MEMTEST_JA_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Jalapeno specific header file for memtest loadable driver.
 */

/*
 * Definitions associated with the E$ Error Enable register.
 * XXX	Move these to cheetahregs.h?
 */
#define	EN_REG_SCDE	0x0000000100000000ULL	/* enable Jbus control perr */
#define	EN_REG_IAEN	0x0000000000800000ULL	/* trap on illegal pa */
#define	EN_REG_IERREN	0x0000000000400000ULL	/* reset on internal errors */
#define	EN_REG_PERREN	0x0000000000200000ULL	/* reset on protocol errors */
#define	EN_REG_SCEN	0x0000000000100000ULL	/* reset on control perr */

/*
 * Definitions associated with the E$ Control register.
 */
#define	JA_ECCR_SIZE_SHIFT	14		/* e$ size bit shift */
#define	JA_ECCR_SIZE(x)		((x >> JA_ECCR_SIZE_SHIFT) & ECCR_SIZE_MASK)

/*
 * Routines located in memtest_ja.c.
 */
extern	int		ja_write_ecache(mdata_t *);
extern	int		ja_write_ephys(mdata_t *);
extern	void		ja_init(mdata_t *);
extern	int		ja_write_memory(mdata_t *, uint64_t, caddr_t, uint_t);
extern	int		ja_k_bus_err(mdata_t *);
extern	int		ja_k_isap_err(mdata_t *);
extern	int		ja_k_wbp_err(mdata_t *);
extern	int		ja_k_bp_err(mdata_t *);

/*
 * Routines located in memtest_ja_asm.s
 */
extern	void		ja_disable_intrs(void);
extern	void		ja_enable_intrs(void);
extern	void		ja_ic_stag_tgt(void);
extern	void		ja_isap(uint64_t, uint_t, uint64_t *);
extern	void		ja_wbp(uint64_t, uint_t, uint64_t *);
extern	int		ja_wr_ecache(uint64_t, uint64_t, uint32_t,
					uint64_t, uint64_t *);
extern	void		ja_wr_ecache_tag(uint64_t, uint64_t, uint64_t *);
extern	void		ja_wr_ephys(uint64_t, int, uint64_t, uint64_t);
extern	void		ja_wr_etphys(uint64_t, uint64_t, uint64_t);
extern	uint64_t	ja_wr_memory(uint64_t, uint_t, uint64_t, uint64_t *);

/*
 * US3i generic and Jalapeno commands structure.
 */
extern	cmd_t	jalapeno_cmds[];
extern	cmd_t	us3i_generic_cmds[];

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_JA_H */

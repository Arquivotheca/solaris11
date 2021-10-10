/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_JG_H
#define	_MEMTEST_JG_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Jaguar specific header file for memtest loadable driver.
 */

/*
 * Routines located in memtest_jg.c
 */
extern	int	jg_write_ecache(mdata_t *);
extern	int	jg_write_etphys(mdata_t *, uint64_t);
extern	int	jg_write_memory(mdata_t *, uint64_t, caddr_t, uint_t);
extern	void	jg_init(mdata_t *);

/*
 * Routines located in memtest_jg_asm.s
 */
extern	uint64_t	jg_get_secr(void);
extern	int		jg_wr_memory(caddr_t, uint_t, uint64_t);
extern	int		jg_wr_ecache(uint64_t, uint64_t, uint32_t, int);
extern  void		jg_wr_ecache_tag_data(uint64_t, uint64_t, uint32_t);
extern  void		jg_wr_ecache_tag_ecc(uint64_t, uint64_t, uint32_t);

/*
 * The following are used to set the appropiate bits in
 * the E$ error enable register (ASI 0x4B)
 */
#define	PPE_ERR_REG	0x400000	/* CPORT data parity */
#define	DPE_ERR_REG	0x200000	/* CPORT LSB data pairty */
#define	SAF_ERR_REG	0x100000	/* Safari address parity */
#define	FMD_ERR_REG	0x2000		/* force system data ECC */

/*
 * Jaguar commands structure.
 */
extern	cmd_t	jaguar_cmds[];

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_JG_H */

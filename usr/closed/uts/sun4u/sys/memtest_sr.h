/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_SR_H
#define	_MEMTEST_SR_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Serrano specific header file for memtest loadable driver.
 */

/*
 * Definitions associated with the E$ Control Register
 */
#define	SR_ECCR_ET_ECC_EN	0x00000800ULL	/* enable ecc on tag */
#define	SR_ECCR_EC_OFF		0x04000000ULL	/* enable L2$ */
#define	SR_ECCR_CFG_SIZE_SHIFT	27		/* e$ size bit shift */
#define	SR_ECCR_CFG_SIZE(x)	((x >> SR_ECCR_CFG_SIZE_SHIFT) & ECCR_SIZE_MASK)

/*
 * Definitions associated with the E$ Error Enable Register
 */
#define	SR_EN_REG_ETEEN		0x00000010ULL	/* enable FERR on tag UE */

/*
 * Routines located in memtest_sr.c
 */
extern	void	sr_init(mdata_t *);

/*
 * Routines located in memtest_sr_asm.s
 */
extern  void	sr_wr_ecache_tag(uint64_t, uint64_t, uint64_t *);
extern  void	sr_wr_ephys(uint64_t, int, uint64_t, uint64_t);
extern  void	sr_wr_etphys(uint64_t, uint64_t, uint64_t);

/*
 * Serrano commands structure.
 */
extern	cmd_t	serrano_cmds[];

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_SR_H */

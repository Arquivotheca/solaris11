/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_SF_H
#define	_MEMTEST_SF_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Spitfire specific header file for memtest loadable driver.
 */

/*
 * Routines located in memtest_sf.c.
 */
extern	int	hb_write_memory(mdata_t *, uint64_t, caddr_t, uint_t);
extern	void	sf_init(mdata_t *);
extern	int 	sf_write_memory(mdata_t *, uint64_t, caddr_t, uint_t);

/*
 * Routines located in memtest_sf_asm.s.
 */
extern	int	hb_wr_ecache_tag(uint64_t, uint_t, uint64_t);
extern	int	hb_wr_memory(uint64_t, uint_t, uint_t);
extern	int	sf_flush_dc(caddr_t);
extern	int	sf_flushall_dc(int, int);
extern	void	sf_wr_ecache(uint64_t);
extern	void	sf_wr_ecache_tag(uint64_t, uint64_t);
extern	void	sf_wr_ephys(uint64_t);
extern	void	sf_wr_etphys(uint64_t, uint64_t);
extern	int	sf_wr_memory(uint64_t, uint_t,  uint_t);

/*
 * Spitfire commands structure.
 */
extern	cmd_t	spitfire_cmds[];

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_SF_H */

/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_CH_H
#define	_MEMTEST_CH_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Cheetah specific header file for memtest loadable driver.
 */

/*
 * This is the cheetah e$ data structure which is used to read/write
 * the E$ staging and tag/state registers.
 */
typedef struct  ch_ec {
	uint64_t	ec_data[4];	/* 32 bytes of data (staging reg 0-3) */
	uint64_t	ec_ecc;		/* ECC high and low (staging reg 4) */
	uint64_t	ec_tag;		/* E$ tag and state */
} ch_ec_t;

/*
 * Test routines located in memtest_ch.c.
 */
extern	int	memtest_k_mtag_err(mdata_t *);

/*
 * Other routines located in memtest_ch.c.
 */
extern	void	ch_dump_ec_data(char *, ch_ec_t *);
extern	void	ch_disable_wc(mdata_t *);
extern	void	ch_enable_wc(void);
extern	int	ch_enable_errors(mdata_t *);
extern	int	ch_flushall_caches(cpu_info_t *);
extern	int	ch_flushall_dcache(cpu_info_t *);
extern	int	ch_flushall_icache(cpu_info_t *);
extern	int	ch_flush_dc_entry(cpu_info_t *, caddr_t);
extern	int	ch_gen_ecc_pa(uint64_t);
extern	int	ch_get_cpu_info(cpu_info_t *);
extern	void	ch_init(mdata_t *);
extern	int	ch_wc_is_enabled(void);
extern	int	ch_write_memory(mdata_t *, uint64_t, caddr_t, uint_t);
extern	int	ch_write_ecache(mdata_t *);
extern	int	ch_write_ephys(mdata_t *);
extern	int	ch_write_etphys(mdata_t *);
extern	int	ch_write_dphys(mdata_t *);
extern	int	ch_write_iphys(mdata_t *);

/*
 * Routines located in memtest_ch_asm.s.
 */
extern	int		ch_flush_dc(caddr_t);
extern	void		ch_flush_wc(uint64_t);
extern	void		ch_flush_wc_va(caddr_t);
extern	int		ch_flushall_dc(int, int);
extern	int		ch_flushall_ic(int, int);
extern	uint64_t	ch_get_ecr(void);
extern	void		ch_rd_ecache(uint64_t, ch_ec_t *);
extern	uint64_t	ch_rd_dcache(uint64_t);
extern	uint64_t	ch_rd_icache(uint64_t);
extern	void		ch_set_ecr(uint64_t);
extern	void		ch_wr_ecache(uint64_t, int, uint64_t);
extern	int		ch_wr_memory(uint64_t, uint_t,  uint_t);
extern	void		ch_wr_mtag(uint64_t, uint_t,  uint_t);
extern	void		ch_wr_ephys(uint64_t, int, uint64_t);
extern	void		ch_wr_etphys(uint64_t, uint64_t, uint64_t);
extern	void		ch_wr_dphys(uint64_t, uint64_t, uint64_t);
extern	void		ch_wr_iphys(uint64_t, uint64_t, uint64_t);

/*
 * US3 generic and Cheetah commands structure.
 */
extern	cmd_t	cheetah_cmds[];
extern	cmd_t	us3_generic_cmds[];

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_CH_H */

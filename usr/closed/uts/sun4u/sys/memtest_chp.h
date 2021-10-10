/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_CHP_H
#define	_MEMTEST_CHP_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Cheetah-Plus specific header file for memtest loadable driver.
 */

typedef struct cache_vals {
	uint64_t	val[8];
} cache_vals_t;

/*
 * Routines located in memtest_chp.c.
 */
extern	int	chp_dc_data_err(mdata_t *, caddr_t);
extern	int	chp_dc_snoop_err(mdata_t *);
extern	int	chp_ic_instr_err(mdata_t *, caddr_t);
extern	int	chp_ic_snoop_err(mdata_t *);
extern	void	chp_init(mdata_t *);
extern	int	chp_internal_err(mdata_t *);
extern	int	chp_write_dcache(mdata_t *);
extern	int	chp_write_dtphys(mdata_t *);
extern	int	chp_write_ecache(mdata_t *);
extern	int	chp_write_etphys(mdata_t *);
extern	int	chp_write_icache(mdata_t *);
extern	int	chp_write_itphys(mdata_t *);
extern	int	chp_write_memory(mdata_t *, uint64_t, caddr_t, uint_t);
extern	int	chp_write_mtag(mdata_t *, uint64_t, caddr_t);

/*
 * Routines located in memtest_chp_asm.s
 */
extern	void	chp_ic_stag_tgt(void);
extern	void	chp_rd_dcache(uint64_t, uint64_t, uint64_t *);
extern	int	chp_wr_dcache_data(uint64_t, uint64_t, uint64_t,
						uint64_t, caddr_t);
extern	int	chp_wr_dcache_data_parity(uint64_t, uint64_t,
						uint64_t, caddr_t);
extern	int	chp_wr_dcache_ptag(uint64_t, uint64_t, uint64_t, caddr_t);
extern	void	chp_wr_dcache_stag(uint64_t, uint64_t, uint64_t, uint64_t);
extern	void	chp_wr_dcache_snoop(uint64_t, caddr_t);
extern	void	chp_wr_dtphys(uint64_t, uint64_t, uint64_t);
extern	void	chp_wr_dup_ecache_tag(uint64_t, int);
extern	int	chp_wr_ecache(uint64_t, uint64_t, uint32_t, int);
extern	void	chp_wr_ecache_tag_data(uint64_t, uint64_t, uint32_t);
extern	void	chp_wr_ecache_tag_ecc(uint64_t, uint64_t, uint32_t);
extern	void	chp_wr_etphys(uint64_t, uint64_t, uint64_t);
extern	int	chp_wr_icache_instr(uint64_t, uint64_t, uint64_t,
				uint64_t, int);
extern	int	chp_wr_icache_ptag(uint64_t, uint64_t, uint64_t, int);
extern	void	chp_wr_itphys(uint64_t, uint64_t, uint64_t);
extern	void	chp_wr_icache_stag(uint64_t, uint64_t,
				uint64_t, uint64_t, uint64_t, uint64_t *);
extern	int	chp_wr_mtag(caddr_t, uint_t);
extern	int	chp_wr_memory(caddr_t, uint_t);

/*
 * The following are used to adjust addresses when
 * accessing E$ ASI diagnostic registers.
 */
#define	EC_SHIFT_1MB	0x13	/* 19 */
#define	EC_SHIFT_4MB	0x15	/* 21 */
#define	EC_SHIFT_8MB	0x16	/* 22 */

#define	EC_TAG_MASK	0x7FFFFF	/* <22:0> */

/*
 * The following are used to adjust addresses when
 * accessing d$ ASI diagnostic registers.
 */
#define	DC_PA_SHIFT	13
#define	DC_WAY_SHIFT	14
#define	DC_ADDR_MASK	0x3FF8	/* 13:3 */
#define	DC_ASI_MASK	0x3FE0	/* 13:5 */
#define	DC_PA_MASK	INT64_C(0x3FFFFFFE000)	/* PA<41:13> */
#define	DC_UTAG_MASK	0x3FC000	/* VA<21:14> */

#define	DC_DATA_ADDR(way, vaddr)		\
		(uint64_t)((way << DC_WAY_SHIFT) | \
		(((uint64_t)vaddr) & DC_ADDR_MASK))

#define	DC_ASI_ADDR(way, addr) \
		(uint64_t)((way << DC_WAY_SHIFT) | \
		(((uint64_t)addr) & DC_ASI_MASK))

#define	DC_UTAG_VAL(addr) \
		((addr & DC_UTAG_MASK) >> 14)

/*
 * The following defines/macros are used to adjust addresses when
 * accessing i$/d$ ASI diagnostic registers.
 * The macros (and the C code) have been modified to handle
 * the Panther CPU register addresses as well.  Hence, the Panther
 * defines show up here.
 */
#define	PN_IC_WAY_SHIFT		15
#define	PN_IC_PTAG_RD_MASK	0x3FE0		/* 0x3FF0=13:6 */
#define	PN_IC_INSTR_RD_MASK	0x3FFC		/* 13:2 */
#define	PN_IC_UTAG_MASK		0x3FC000	/* VA<21:14> */

#define	IC_WAY_SHIFT		14
#define	IC_ADDR_SHIFT		1
#define	IC_TAG_SHIFT		3
#define	IC_PTAG_RD_MASK		0x1FF0		/* 12:4 */
#define	IC_INSTR_RD_MASK	0x1FFC		/* 12:2 */
#define	IC_UTAG_MASK		0x1FE000	/* VA<20:13> */
#define	IC_P			0x0		/* IC_tag value to read Ptag */
#define	IC_PA_MASK		INT64_C(0x3FFFFFFE000)	/* PA<41:13> */
#define	IC_TAG_VALID_MASK	INT64_C(0x4000000000000)


#define	IC_PTAG_ADDR(way, addr, impl)				\
	((impl) == PANTHER_IMPL) ? 				\
	((uint64_t)((way << PN_IC_WAY_SHIFT) | 	 		\
	((addr & PN_IC_PTAG_RD_MASK) << IC_ADDR_SHIFT) | 	\
	(IC_P << IC_TAG_SHIFT)))		:	 	\
	((uint64_t)((way << IC_WAY_SHIFT) | 			\
	((addr & IC_PTAG_RD_MASK) << IC_ADDR_SHIFT) | 		\
	(IC_P << IC_TAG_SHIFT)))

#define	IC_INSTR_ADDR(way, addr, impl)				\
	((impl) == PANTHER_IMPL) ? 				\
	((uint64_t)((way << PN_IC_WAY_SHIFT) | 			\
	((addr & PN_IC_INSTR_RD_MASK) << IC_ADDR_SHIFT)))	\
	:	 						\
	((uint64_t)((way << IC_WAY_SHIFT) | 			\
	((addr & IC_INSTR_RD_MASK) << IC_ADDR_SHIFT)))


/*
 * For Panther  8 bits VA<21:14> get stuffed into reg VA<45:38>
 * For Cheetah+ 8 bits VA<20:13> get stuffed into reg VA<45:38>
 */
#define	IC_UTAG_VAL(addr, impl) \
	((impl) == PANTHER_IMPL) ? 			\
	(uint64_t)((addr & PN_IC_UTAG_MASK) << 24)	\
	:						\
	(uint64_t)((addr & IC_UTAG_MASK) << 25)

#ifndef ASI_MCU_CTRL
#define	ASI_MCU_CTRL	0x72
#endif

/*
 * Memory starvation Internal processor error.
 * Value to set in rfr_int<11:3> field of MCU control register.
 */
#define	CHP_NO_REFSH_VAL	0x1

/*
 * Cheetah+ commands structure.
 */
extern	cmd_t	cheetahp_cmds[];

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_CHP_H */

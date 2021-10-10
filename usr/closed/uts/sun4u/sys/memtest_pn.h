/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_PN_H
#define	_MEMTEST_PN_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _ASM
#else

/*
 * Panther specific header file for memtest injector driver.
 */

/*
 * Routines located in memtest_pn.c.
 */
extern	void	pn_init(mdata_t *mdatap);
extern	int	pn_flushall_caches(cpu_info_t *cip);
extern	int	pn_flushall_dcache(cpu_info_t *cip);
extern	int	pn_flushall_icache(cpu_info_t *cip);
extern 	int 	pn_write_dphys(mdata_t *);
extern 	int 	pn_write_ecache(mdata_t *);
extern 	int 	pn_write_ephys(mdata_t *);
extern 	int 	pn_write_iphys(mdata_t *);
extern 	int 	pn_write_pcache(mdata_t *);
extern 	int 	pn_write_tlb(mdata_t *);

/*
 * Routines located in panther_asm.s.
 */
extern	void	pn_flushall_l2();
extern	void	pn_flushall_l3();
extern	void	pn_flush_l2_line(uint64_t paddr);
extern	int	pn_flushall_dc(int cachesize, int linesize);
extern	int	pn_flushall_ic(int cachesize, int linesize);
extern	void	pn_flush_wc(uint64_t paddr);
extern	int	pn_wr_dcache_data_parity(uint64_t tag_addr,
		    uint64_t data_addr, uint64_t tag_value, uint64_t xorpat,
		    caddr_t vaddr);
extern	void	pn_wr_dphys_parity(uint64_t offset, uint64_t xorpat);

/* TLB related functions */
extern 	int 		pn_get_dtlb_entry(int index, int way, uint64_t mask,
			    uint64_t *tagp, uint64_t *datap);
extern 	int		pn_get_itlb_entry(int index, int way, uint64_t mask,
			    uint64_t *tagp, uint64_t *datap);
extern 	uint64_t	pn_wr_itlb_parity_idx(uint32_t index, uint64_t va,
			    uint64_t pa, uint64_t mask, uint64_t xorpat,
			    uint32_t ctxnum);
extern 	uint64_t	pn_wr_dtlb_parity_idx(uint32_t index, uint64_t va,
			    uint64_t pa, uint64_t mask, uint64_t xorpat,
			    uint32_t ctxnum);
/* Cache injection functions */
extern	int		pn_wr_dcache_data_parity(uint64_t tag_addr,
			    uint64_t data_addr, uint64_t tag_value,
			    uint64_t xorpat, caddr_t vaddr);
extern void 		pn_wr_icache_stag(uint64_t addr, uint64_t tag_addr,
			    uint64_t utag_val, uint64_t xorpat,
			    uint64_t store_addr, uint64_t *ptr);
extern 	uint64_t	pn_pcache_load(uint64_t va);
extern 	uint64_t	pn_pcache_status_entry(uint64_t va);
extern 	uint64_t	pn_load_fp(uint64_t va);
extern 	uint64_t	pn_pcache_write_parity(uint64_t va, uint64_t pa);
extern	uint64_t	pn_enable_pcache();
extern	uint64_t	pn_disable_pcache();

extern 	uint64_t 	pn_wr_l2_data(uint64_t tag_addr, uint64_t data_addr,
			    uint64_t tag_cmp, uint64_t xorpat, int reg_select);
extern 	uint64_t 	pn_wr_l3_data(uint64_t tag_addr, uint64_t data_addr,
			    uint64_t tag_cmp, uint64_t xorpat, int reg_select);
extern 	uint64_t 	pn_wr_l2_tag(uint64_t paddr, uint64_t xorpat);
extern 	uint64_t 	pn_wr_l3_tag(uint64_t paddr, uint64_t xorpat);
extern 	uint64_t 	pn_wr_l2phys_data(uint64_t offset, uint64_t xorpat,
			    int reg_select);
extern 	uint64_t 	pn_wr_l3phys_data(uint64_t offset, uint64_t xorpat,
			    int reg_select);
extern	uint64_t	pn_wr_l2phys_tag(uint64_t offset, uint64_t xorpat);
extern	uint64_t	pn_wr_l3phys_tag(uint64_t offset, uint64_t xorpat);

extern	void 		pn_ipb_asmld(caddr_t addr, caddr_t addr1);
extern	uint64_t	pn_wr_ipb(uint64_t paddr, int xorpat);
extern 	int 		pn_ipb_err(mdata_t *mdatap);
extern	uint64_t	pn_wr_dup_l2_tag(uint64_t paddr);
extern	uint64_t	pn_wr_ill_l2_tag(uint64_t paddr);
extern	uint64_t	pn_wr_dup_l3_tag(uint64_t paddr);
extern	uint64_t	pn_wr_ill_l3_tag(uint64_t paddr);

extern	void		pn_split_cache(int l2l3_flag);
extern	void		pn_unsplit_cache(int l2l3_flag);
extern	uint64_t	pn_set_l2_na(uint64_t paddr, uint_t targ_way,
			    uint_t state_value);
extern	uint64_t	pn_set_l3_na(uint64_t paddr, uint_t targ_way,
			    uint_t state_value);
extern	uint64_t	pn_rd_l2_tag(uint64_t paddr, uint_t targ_way);
extern	uint64_t	pn_rd_l3_tag(uint64_t paddr, uint_t targ_way);

/*
 * Panther commands list.
 */
extern	cmd_t	panther_cmds[];

#endif  /* _ASM */

/* I-D TLB macros */
#define	MAX_TLB_512_0		512
#define	MAX_TLB_512_1		512
#define	TLB_512_0_ID		0x2
#define	TLB_512_1_ID		0x3
#define	TLB_ENTRY_SHIFT		0x3

/* I-D Diagnostic read masks */
#define	TLB_512_0_DIAGMASK	0x60000	/* 1 << 18 | 2 << 16 */
#define	TLB_512_1_DIAGMASK	0x70000	/* 1 << 18 | 3 << 16 */
/* Data Access Masks */
#define	TLB_512_0_DATAMASK	0x20000	/* 0 << 18 | 2 << 16 */
#define	TLB_512_1_DATAMASK	0x30000	/* 0 << 18 | 3 << 16 */

/* TLB index macros */
/*	Size <2:0> = [ Size<2>=TTE<48> | Size<1:0>=TTE<62:61> ] */
#define	TTE2PAGEBITS(tte)      \
	((((tte >> 48) & 0x01) << 2) | ((tte >> 61) & 0x03))

/*
 * The index for the D/I TLB depends on the pagesize, which we fetch from the
 * TTE. The index uses 8 bits(256 entries) for a given 512_{0,1}, I-D TLB
 * for each way.
 * The index is calculated on a subset of the VA bits as follows:
 *	Page	Index Bits	Page Size Encoding
 *	----    -----------     ------------------
 *	8KB  	VA<20:13>	000
 *	64KB 	VA<20:16>	001
 *	512KB 	VA<20:19>	010
 *	4MB 	VA<20:22>	011
 *	32MB 	VA<20:25>	100
 *	256MB 	VA<20:28>	101
 *
 * Thus, given a base page size of 8KB with a a shift of 13, the page
 * index shifts for other page sizes  = (page-size-encoding * 3) + 13
 * Example: 4MB shift = (3 * 3) + 13 = 22
 */
#define	VA2INDEX(va, tte)	\
	(((uint64_t)(va) >> ((int)(TTE2PAGEBITS(tte) * 3) + 13)) & 0xff)

#define	PN_L3_DATA_RD_MASK	0x7fffe0	/* l3_tag = PA<22:5> */
#define	PN_L3_GEN_ECC_BIT	0x2000000	/* bit<25> = hw_gen_ecc */

#define	PN_L2_TAG_SHIFT		19		/* shift to get PA<42:19> */
#define	PN_L3_TAG_SHIFT		23		/* shift to get PA<42:23> */

#define	ET_ECC_EN		0x2000000	/* bit<25> = ET_ECC_En */
#define	EC_ECC_EN		0x400		/* bit<10> = EC_ECC_En */

#define	PN_IC_PTAG_ADDR(way, addr) \
	(uint64_t)((way << IC_WAY_SHIFT) | \
	((addr & PN_IC_PTAG_RD_MASK) << PN_IC_ADDR_SHIFT) | \
	(IC_P << PN_IC_TAG_SHIFT))

#define	PN_IC_INSTR_ADDR(way, addr) \
	(uint64_t)((way << IC_WAY_SHIFT) | \
	((addr & PN_IC_INSTR_RD_MASK) << PN_IC_ADDR_SHIFT))

#define	PN_C_UTAG_VAL(addr) \
	(uint64_t)((addr & PN_IC_UTAG_MASK) << 25)

#define	L3_PA_MASK			INT64_C(0x7ffff800000)	/* PA<42:23> */
#define	L3_TAG_ADDR(way, paddr) \
	(uint64_t)((way << L3_WAY_SHIFT) | (paddr & L3_TAG_RD_MASK))

/* Panther IPB stride masks */
#define	PN_IPS_64	INT64_C(0x10000000000000)  /* DCUCR<53:52> = 2'b00 */
#define	PN_IPS_128	INT64_C(0x20000000000000)  /* DCUCR<53:52> = 2'b10 */
#define	PN_IPS_192	INT64_C(0x30000000000000)  /* DCUCR<53:52> = 2'b11 */
#define	PN_IPS_MASK	INT64_C(0x30000000000000)  /* DCUCR<53:52> = 2'b11 */


#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_PN_H */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains Panther specific injector code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_chp.h>
#include <sys/memtestio_jg.h>
#include <sys/memtestio_pn.h>
#include <sys/memtest_u.h>
#include <sys/memtest_u_asm.h>
#include <sys/memtest_ch.h>
#include <sys/memtest_chp.h>
#include <sys/memtest_jg.h>
#include <sys/memtest_pn.h>
#include <sys/memtest_cmp.h>
#include <sys/cmpregs.h>
#include <sys/kobj_impl.h>
#include <sys/cheetahasm.h>
#include <sys/cheetahregs.h>

/*
 * Static routines located in this file.
 */
static void	pn_dump_dtlb(int index);
static void	pn_dump_itlb(int index);
static int	pn_get_cpu_info(cpu_info_t *cip);
static int	pn_write_dtlb(mdata_t *mdatap, caddr_t va,
			uint64_t tteval, uint32_t ctxnum);
static int	pn_write_itlb(mdata_t *mdatap, caddr_t va,
			uint64_t tteval, uint32_t ctxnum);
static uint64_t	pn_write_l2l3_data_stky(mdata_t *, uint64_t, uint64_t,
			uint64_t, uint64_t, uint64_t, int);
static uint64_t	pn_write_l2l3_tag_stky(mdata_t *, uint64_t, uint64_t);

/*
 * Panther operations vector tables.
 */
static opsvec_u_t panther_uops = {
	/* sun4u injection ops vectors */
	pn_write_dphys,		/* corrupt d$ tag at offset */
	pn_write_iphys,		/* corrupt i$ tag at offset */
	pn_write_ephys,		/* corrupt l2/l3$ tag at offset */
	jg_write_memory,	/* corrupt memory */
	chp_write_mtag,		/* corrupt mtag */
	pn_write_pcache,	/* P-Cache parity error */

	/* sun4u support ops vectors */
	ch_gen_ecc_pa,		/* generate ecc for data at paddr */
};

static opsvec_c_t panther_cops = {
	/* common injection ops vectors */
	chp_write_dcache,	/* corrupt d$ at paddr */
	pn_write_dphys,		/* corrupt d$ at offset */
	notsup,			/* no corrupt fp reg */
	chp_write_icache,	/* corrupt i$ at paddr */
	chp_internal_err,	/* internal processor errors */
	pn_write_iphys,		/* corrupt i$ at offset */
	notsup,			/* no corrupt int reg */
	pn_write_ecache,	/* corrupt l2/l3$ at paddr */
	pn_write_ephys,		/* corrupt l2/l3$ at offset */
	notsup,			/* corrupt l3$ at paddr (using l2 opsvec) */
	notsup,			/* corrupt l3$ at offset (using l2 opsvec) */
	pn_write_tlb,		/* I-D TLB parity errors */

	/* common support ops vectors */
	notsup,			/* no fp reg access */
	notsup,			/* no int reg access */
	memtest_check_afsr,	/* check ESRs */
	ch_enable_errors,	/* enable AFT errors */
	notimp,			/* no enable/disable L2 or memory scrubbers */
	pn_get_cpu_info,	/* put cpu info into struct */
	pn_flushall_caches,	/* flush all caches */
	pn_flushall_dcache,	/* flush all d$ */
	pn_flushall_icache,	/* flush all i$ */
	gen_flushall_l2,	/* flush all l2$ */
	notsup,			/* no flush all l3$ */
	notsup,			/* flush d$ entry */
	gen_flush_ic_entry,	/* flush i$ entry */
	gen_flush_l2_entry,	/* flush l2$ entry - not used */
	notsup,			/* no flush l3$ entry */
};

/*
 * These Panther errors are grouped according to the
 * definitions in the header file.
 */
cmd_t	panther_cmds[] = {
	PN_L2_MH,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_L2_ILLSTATE,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_L3_MH,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_L3_ILLSTATE,		memtest_k_l2_err,	"memtest_k_l2_err",

	PN_KD_L3UCU,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KU_L3UCUCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	PN_KD_L3UCUTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KI_L3UCU,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KI_L3UCUTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KI_L3OUCU,		memtest_k_mem_err,	"memtest_k_mem_err",
	PN_UD_L3UCU,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_UI_L3UCU,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_OBPD_L3UCU,		memtest_obp_err,	"memtest_obp_err",
	PN_KD_L3UCC,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KU_L3UCCCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	PN_KD_L3UCCTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KI_L3UCC,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KI_L3UCCTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_UD_L3UCC,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_UI_L3UCC,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_OBPD_L3UCC,		memtest_obp_err,	"memtest_obp_err",

	PN_KD_L3EDUL,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KD_L3EDUS,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KD_L3EDUPR,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_UD_L3EDUL,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_UD_L3EDUS,		memtest_u_l2_err,	"memtest_u_l2_err",

	PN_KD_L3EDCL,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KD_L3EDCS,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KD_L3EDCPR,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_UD_L3EDCL,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_UD_L3EDCS,		memtest_u_l2_err,	"memtest_u_l2_err",

	/*
	 * L3 cache data and tag errors injected by address/offset/index
	 * Note that the L2 verions are defined as generic commands, and
	 * that the L3 code goes through the same higher-level L2 routines.
	 */
	PN_L3PHYS,		memtest_l2phys,		"memtest_l2phys",
	PN_L3TPHYS,		memtest_l2phys,		"memtest_l2phys",

	/* Write-back tests */
	PN_KD_L3WDU,		memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	PN_KI_L3WDU,		memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	PN_UD_L3WDU,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_UI_L3WDU,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_KD_L3WDC,		memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	PN_KI_L3WDC,		memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	PN_UD_L3WDC,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_UI_L3WDC,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_KD_L3CPU,		memtest_k_cp_err,	"memtest_k_cp_err",
	PN_KI_L3CPU,		memtest_k_cp_err,	"memtest_k_cp_err",
	PN_UD_L3CPU,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_UI_L3CPU,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_KD_L3CPC,		memtest_k_cp_err,	"memtest_k_cp_err",
	PN_KI_L3CPC,		memtest_k_cp_err,	"memtest_k_cp_err",
	PN_UD_L3CPC,		memtest_u_l2_err,	"memtest_u_l2_err",

	PN_KD_L3ETHCE,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KI_L3ETHCE,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_UD_L3ETHCE,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_UI_L3ETHCE,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_KD_L3ETHUE,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KI_L3ETHUE,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_UD_L3ETHUE,		memtest_u_l2_err,	"memtest_u_l2_err",
	PN_UI_L3ETHUE,		memtest_u_l2_err,	"memtest_u_l2_err",

	PN_KD_EDC_STKY,		memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KD_L3EDC_STKY,	memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KD_THCE_STKY,	memtest_k_l2_err,	"memtest_k_l2_err",
	PN_KD_L3THCE_STKY,	memtest_k_l2_err,	"memtest_k_l2_err",

	PN_KD_TLB,		memtest_k_tlb_err, 	"memtest_k_tlb_err",
	PN_KD_TLBTL1,		memtest_k_tlb_err, 	"memtest_k_tlb_err",
	PN_KI_TLB,		memtest_k_tlb_err, 	"memtest_k_tlb_err",
	PN_UD_TLB,		memtest_k_tlb_err, 	"memtest_k_tlb_err",
	PN_UI_TLB,		memtest_k_tlb_err, 	"memtest_k_tlb_err",

	PN_KD_PC,		memtest_k_pcache_err, 	"memtest_k_pcache_err",

	PN_KI_IPB,		memtest_k_ic_err, 	"memtest_k_ic_err",
	NULL,			NULL,			NULL,
};

static cmd_t *commands[] = {
	panther_cmds,
	jaguar_cmds,
	cheetahp_cmds,
	cheetah_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

void
pn_init(mdata_t *mdatap)
{
	mdatap->m_sopvp = &panther_uops;
	mdatap->m_copvp = &panther_cops;
	mdatap->m_cmdpp = commands;
}

static int
pn_get_cpu_info(cpu_info_t *cip)
{
	cip->c_dc_size = CH_DCACHE_SIZE;
	cip->c_dc_linesize = CH_DCACHE_LSIZE;
	cip->c_dc_assoc = CH_DCACHE_NWAY;

	cip->c_ic_size = PN_ICACHE_SIZE;
	cip->c_ic_linesize = PN_ICACHE_LSIZE;
	cip->c_ic_assoc = CH_ICACHE_NWAY;

	cip->c_ecr = ch_get_ecr();	/* common ASI 0x75 */

	/*
	 * Panther is a CMP - initialize its core id
	 * bit <0> for 2 cores = core id{0,1}
	 */
	cip->c_core_id = (cmp_get_core_id() & 0x01);

	/* L2-cache parameters */
	cip->c_l2_size =  PN_L2_SIZE;
	cip->c_l2_sublinesize = PN_L2_LINESIZE;
	cip->c_l2_linesize = PN_L2_LINESIZE;
	cip->c_l2_assoc = PN_L2_NWAYS;
	cip->c_l2_flushsize = cip->c_l2_size * cip->c_l2_assoc * 2;

	/* L3-cache parameters */
	cip->c_l3_size =  PN_L3_SIZE;
	cip->c_l3_sublinesize = PN_L3_LINESIZE;
	cip->c_l3_linesize = PN_L3_LINESIZE;
	cip->c_l3_assoc = PN_L3_NWAYS;
	cip->c_l3_flushsize = cip->c_l3_size * cip->c_l3_assoc * 2;

	return (0);
}

/*
 * Flush the entire I, D, L2 and L3 caches.
 * Further, in Panther, since the I$ and D$ are included in the L2$
 * there really is no need to flush the I$, D$ separately but we
 * play safe and do it anyway.
 */
int
pn_flushall_caches(cpu_info_t *cip)
{
	/*
	 * The assembly routines are just wrappers around the
	 * macros in cheetahasm.h that do the real work.
	 */
	DPRINTF(4, "pn_flushall_caches: calling pn_flushall_ic()\n");
	(void) pn_flushall_ic(cip->c_ic_size, cip->c_ic_linesize);

	DPRINTF(4, "pn_flushall_caches: calling pn_flushall_dc()\n");
	(void) pn_flushall_dc(cip->c_dc_size, cip->c_dc_linesize);

	DPRINTF(4, "pn_flushall_caches: calling pn_flush_l2/l3()\n");
	pn_flushall_l2();
	pn_flushall_l3();

	return (0);
}

/*
 * This routine flushed the entire data cache.
 */
int
pn_flushall_dcache(cpu_info_t *cip)
{
	DPRINTF(4, "pn_flushall_dcache: calling pn_flushall_dc()\n");
	(void) pn_flushall_dc(cip->c_dc_size, cip->c_dc_linesize);
	return (0);
}

/*
 * This routine flushed the entire instruction cache.
 */
int
pn_flushall_icache(cpu_info_t *cip)
{
	DPRINTF(4, "pn_flushall_icache: calling pn_flushall_ic()\n");
	(void) pn_flushall_ic(cip->c_ic_size, cip->c_ic_linesize);
	return (0);
}

/*
 * The macros selects the L2/L3 tag/data/NA routines based on user command.
 */
#define	PN_L2L3_TAG(E, P, X)	\
	(ERR_LEVEL_L3(E)) ? (pn_wr_l3_tag(P, X)) : (pn_wr_l2_tag(P, X))

#define	PN_L2L3_DATA(E, T, D, C, X, R)	\
	(ERR_LEVEL_L3(E)) ? (pn_wr_l3_data(T, D, C, X, R)) : \
			(pn_wr_l2_data(T, D, C, X, R))

#define	PN_L2L3_NA(E, P, W, S)	\
	(ERR_LEVEL_L3(E)) ? (pn_set_l3_na(P, W, S)) : (pn_set_l2_na(P, W, S))

#define	PN_L2L3_READ_TAG(E, P, W)	\
	(ERR_LEVEL_L3(E)) ? (pn_rd_l3_tag(P, W)) : (pn_rd_l2_tag(P, W))

#define	PN_L2L3_STATE_INV	0
#define	PN_L2L3_STATE_NA	5

/*
 * This routine inserts an error into either the ecache, the
 * ecache tags or the ECC protecting the data or tags.
 *
 * Note that "ecache" for this routine refers to one of the L2 or L3
 * cache depending on the command that this routine was called from.
 *
 * NOTE: the asm routines called by this function have not
 *	 been modified for the index/way/cache-split code.
 *	 They still search all four ways of the L2 or L3 looking for
 *	 a tag match in order to find the data that was loaded in
 *	 (to be corrupted and later accessed).
 *	 This can possibly be optimized in the future though it works as-is.
 *
 * NOTE: the asm routines discard matches that are of state invalid,
 *	 this could be enhanced to also discard matches with state NA.
 */
int
pn_write_ecache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	paddr_aligned, xorpat;
	uint64_t	tag_addr, data_addr, tag_cmp;
	uint64_t	na_tag_value;
	uint_t		cache_way_mask;
	int		reg_offset;
	int		wc_was_disabled = 0;
	uint64_t	ret = ENOTSUP;
	char		*fname = "pn_write_ecache";

	paddr_aligned = P2ALIGN(paddr, 32);  /* staging regs alignment */

	DPRINTF(3, "%s: mdatap=0x%p, paddr=0x%llx, paddr_aligned=0x%llx\n",
	    fname, mdatap, paddr, paddr_aligned);

	if (ERR_LEVEL_L2(iocp->ioc_command)) { /* L2 E$ */
		tag_addr = paddr_aligned & PN_L2_INDEX_MASK;
		data_addr = paddr_aligned & PN_L2_INDEX_MASK;
		tag_cmp = paddr_aligned >> PN_L2_TAG_SHIFT; /* PA<42:19> */
	} else if (ERR_LEVEL_L3(iocp->ioc_command)) { /* L3 E$ */
		tag_addr = paddr_aligned & PN_L3_TAG_RD_MASK;
		data_addr = paddr_aligned & PN_L3_DATA_RD_MASK;
		tag_cmp = paddr_aligned >> PN_L3_TAG_SHIFT;
	} else {
		DPRINTF(2, "%s: invalid ERR_LEVEL\n", fname);
		return (ENOTSUP);
	}

	DPRINTF(4, "%s: tag_addr=%llx, data_addr=%llx, tag_cmp=%llx\n",
	    fname, tag_addr, data_addr, tag_cmp);

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * If the user specified a particular cache way to inject into then
	 * the other ways are made unavailable before the data is brought into
	 * the cache.  This is done by first splitting the cache (L2 or L3)
	 * so only two ways per core are available, then marking the tag of
	 * the remaining other way NA (not available).
	 *
	 * The line is marked NA here instead of in a higher-level routine so
	 * the various cache command types will all benefit from this feature
	 * (such as write-back and copy-back tests, etc.).
	 */
	if (F_CACHE_WAY(iocp)) {
		if (ERR_LEVEL_L2(iocp->ioc_command)) { /* L2 E$ */
			cache_way_mask = mdatap->m_cip->c_l2_assoc - 1;
			if (iocp->ioc_cache_way >= mdatap->m_cip->c_l2_assoc) {
				DPRINTF(0, "%s: specified cache way is out "
				    "of range, masking it to %d\n", fname,
				    iocp->ioc_cache_way & cache_way_mask);
				iocp->ioc_cache_way &= cache_way_mask;
			}
		} else { /* L3 E$ */
			cache_way_mask = mdatap->m_cip->c_l3_assoc - 1;
			if (iocp->ioc_cache_way >= mdatap->m_cip->c_l3_assoc) {
				DPRINTF(0, "%s: specified cache way is out "
				    "of range, masking it to %d\n", fname,
				    iocp->ioc_cache_way & cache_way_mask);
				iocp->ioc_cache_way &= cache_way_mask;
			}
		}

		/*
		 * Put the line into the NA state but don't allow both
		 * lines in this half of the cache to be NA'd as this is
		 * illegal.  The targets cache state is checked before
		 * the other cacheline is placed into the NA state.
		 */
		na_tag_value = PN_L2L3_READ_TAG(iocp->ioc_command,
		    paddr_aligned, iocp->ioc_cache_way);
		if ((na_tag_value &= CH_ECSTATE_MASK) == PN_L2L3_STATE_NA) {
			DPRINTF(0, "%s: unable to inject into target cacheline "
			    "at paddr=0x%llx and way=%d because it's in the "
			    "NA state. To inject into this line one of the "
			    "PHYS commands can be used\n", fname,
			    paddr_aligned, iocp->ioc_cache_way);
			return (-1);
		}

		na_tag_value = PN_L2L3_NA(iocp->ioc_command, paddr_aligned,
		    (iocp->ioc_cache_way ^ 1), PN_L2L3_STATE_NA);
	}

	pn_flushall_l2();
	pn_flushall_l3();

	/*
	 * Place the data in the cache into the modified (M) state for
	 * non-instruction L2 errors (this is required for write-back
	 * and is preferable for normal L2$ errors).
	 *
	 * To ensure that the store (to modify) happens before the
	 * error is injected into the L2/L3, the w$ is disabled.
	 *
	 * Also honor the user options to force a cacheline state.
	 */
	if (F_CACHE_DIRTY(iocp)) {
		if (ch_wc_is_enabled()) {
			if (memtest_flags & MFLAGS_CH_USE_FLUSH_WC) {
				pn_flush_wc(paddr);
			} else {
				ch_disable_wc(mdatap);
				wc_was_disabled = 1;
			}
		}
		stphys(paddr_aligned, ldphys(paddr_aligned));
	} else if (F_CACHE_CLN(iocp)) {
		DPRINTF(1, "%s: not changing L2 cache line state "
		    "to modified due to user option\n", fname);
		(void) ldphys(paddr_aligned);
	} else if ((ERR_ACC_ISFETCH(iocp->ioc_command) ||
	    ERR_ACC_ISPFETCH(iocp->ioc_command)) &&
	    !ERR_CLASS_ISL2WB(iocp->ioc_command) &&
	    !ERR_CLASS_ISL3WB(iocp->ioc_command)) {
		DPRINTF(1, "%s: not changing L2 cache line state "
		    "to modified due to test definition\n", fname);
		(void) ldphys(paddr_aligned);
	} else { /* modify by default */
		if (ch_wc_is_enabled()) {
			if (memtest_flags & MFLAGS_CH_USE_FLUSH_WC) {
				pn_flush_wc(paddr);
			} else {
				ch_disable_wc(mdatap);
				wc_was_disabled = 1;
			}
		}
		stphys(paddr_aligned, ldphys(paddr_aligned));
	}

	/*
	 * The Panther L2/L3 semantics has the load bypassing the L3
	 * and allocating in L2. Therefore in order to inject into L3
	 * the line has to be flushed from L2 to L3.
	 */
	if (ERR_LEVEL_L3(iocp->ioc_command)) {
		pn_flush_l2_line(paddr_aligned);
	}

	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command) ||
	    ERR_SUBCLASS_ISMH(iocp->ioc_command)) { /* L2/L3 tag */
		switch (IOC_COMMAND(iocp)) {
		case PN_L2_MH:
			ret = pn_wr_dup_l2_tag(paddr_aligned);
			break;

		case PN_L2_ILLSTATE:
			ret = pn_wr_ill_l2_tag(paddr_aligned);
			break;

		case PN_L3_MH:
			pn_flush_l2_line(paddr_aligned);
			ret = pn_wr_dup_l3_tag(paddr_aligned);
			break;

		case PN_L3_ILLSTATE:
			pn_flush_l2_line(paddr_aligned);
			ret = pn_wr_ill_l3_tag(paddr_aligned);
			break;

		default:
			DPRINTF(3, "%s: calling pn_wr_%s_tag(), "
			    "paddr_aligned=0x%llx, xorpat=0x%llx\n", fname,
			    (ERR_LEVEL_L3(iocp->ioc_command)) ? "l3":"l2",
			    paddr_aligned, xorpat);

			if (ERR_MISC_ISSTICKY(iocp->ioc_command)) {
				ret = pn_write_l2l3_tag_stky(mdatap,
				    paddr_aligned, xorpat);
			} else {
				ret = PN_L2L3_TAG(iocp->ioc_command,
				    paddr_aligned, xorpat);
			}

			/*
			 * DEBUG: perform the tag access right here
			 * if the way was specified since with only
			 * one open way the line gets victimized before
			 * the normal access code is run.
			 *
			 * This for some reason affects the tag test
			 * more than data version (issue is L2 only).
			 */
			if (F_CACHE_WAY(iocp) && !F_NOERR(iocp)) {
				*mdatap->m_kvaddr_a = (uchar_t)0xff;
			}
			break;
		}

	} else if (ERR_SUBCLASS_ISDATA(iocp->ioc_command)) {
		/*
		 * Corrupt ecache data or ecc and set the offset value
		 * to get the correct 64-bit chunk of the cacheline based
		 * on the paddr sent in.  Note that the L3 access ASI uses
		 * a different addressing width (32-byte vs. 64-byte for L2).
		 */
		if (F_CHKBIT(iocp)) {
			reg_offset =
			    (ERR_LEVEL_L3(iocp->ioc_command)) ? 4 : 8;

			/*
			 * The L3 has 9-bit ECC on each 16-byte chunk, so
			 * to inject into the higher ECC the xorpat is shifted
			 * to line up with the returned ECC from the ASI access.
			 */
			if (ERR_LEVEL_L3(iocp->ioc_command)) {
				if ((paddr & 0x10) == 0) {
					xorpat = xorpat << 9;
				}
			}
		} else {
			reg_offset =
			    (ERR_LEVEL_L3(iocp->ioc_command)) ? 32 : 64;
			reg_offset = ((paddr) % reg_offset) / sizeof (uint64_t);
		}

		DPRINTF(3, "%s: calling pn_wr_%s_data(), "
		    "reg_offset=%d, paddr_aligned=0x%llx, "
		    "xorpat=0x%llx\n", fname,
		    (ERR_LEVEL_L3(iocp->ioc_command)) ? "l3":"l2",
		    reg_offset, paddr_aligned, xorpat);

		if (ERR_MISC_ISSTICKY(iocp->ioc_command)) {
			ret = pn_write_l2l3_data_stky(mdatap, paddr_aligned,
			    tag_addr, data_addr, tag_cmp, xorpat, reg_offset);
		} else {
			ret = PN_L2L3_DATA(iocp->ioc_command, tag_addr,
			    data_addr, tag_cmp, xorpat, reg_offset);
		}
	} else {
		DPRINTF(0, "%s: invalid command subclass in "
		    "command 0x%llx\n", fname, iocp->ioc_command);
		/* fall through to cleanup code */
	}

	/*
	 * Re-enable any caches disabled above, and return the caches/lines
	 * to their normal/previous operating states.
	 */
	if (wc_was_disabled)
		ch_enable_wc();

	if (F_CACHE_WAY(iocp)) {
		if ((na_tag_value &= CH_ECSTATE_MASK) != PN_L2L3_STATE_NA) {
			na_tag_value = PN_L2L3_STATE_INV;
		}

		na_tag_value = PN_L2L3_NA(iocp->ioc_command, paddr_aligned,
		    (iocp->ioc_cache_way ^ 1), na_tag_value);
	}

	/*
	 * Check return value from the low-level routine for possible error.
	 */
	if (ret == 0xfeccf) {
		DPRINTF(0, "%s: TEST FAILED (ret 0x%x) could not "
		    "locate data in cache\n", fname, ret);
		return (EIO);
	} else if (ret == 0xffccd) {
		DPRINTF(0, "%s: TEST FAILED (ret 0x%x) could not "
		    "locate data in cache (sticky test)\n", fname, ret);
		return (EIO);
	} else {
		return (0);
	}
}

/*
 * This routine inserts an error into the L2 or L3 cache data, the
 * L2 or L3 cache tags, or the ECC protecting one of the data or the
 * tags in a location specified by the byte offset in the ioc_addr member
 * of the mdata struct.
 *
 * This routine is similar to the above pn_write_ecache() routine.
 */
int
pn_write_ephys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	offset_aligned;
	uint64_t	xorpat;
	uint_t		cache_way_mask;
	int		reg_offset;
	int		ret;
	char		*fname = "pn_write_ephys";

	DPRINTF(3, "%s: iocp=0x%p, offset=0x%llx\n", fname, iocp, offset);
	offset_aligned = P2ALIGN(offset, 32);  /* staging regs alignment */

	/*
	 * If the user specified a particular tag in addition to the byte
	 * offset into the cache the way bits of the offset are overwritten
	 * with the specified value so the intended way is targetted.
	 */
	if (F_CACHE_WAY(iocp)) {
		if (ERR_LEVEL_L2(iocp->ioc_command)) {
			cache_way_mask = mdatap->m_cip->c_l2_assoc - 1;
			if (iocp->ioc_cache_way >= mdatap->m_cip->c_l2_assoc) {
				DPRINTF(0, "%s: specified cache way is out "
				    "of range, masking it to %d\n", fname,
				    iocp->ioc_cache_way & cache_way_mask);
				iocp->ioc_cache_way &= cache_way_mask;
			}
			offset_aligned &= ~((uint64_t)PN_L2_WAY_MASK);
			offset_aligned |= (iocp->ioc_cache_way <<
			    PN_L2_WAY_SHIFT);
		} else if (ERR_LEVEL_L3(iocp->ioc_command)) {
			cache_way_mask = mdatap->m_cip->c_l3_assoc - 1;
			if (iocp->ioc_cache_way >= mdatap->m_cip->c_l3_assoc) {
				DPRINTF(0, "%s: specified cache way is out "
				    "of range, masking it to %d\n", fname,
				    iocp->ioc_cache_way & cache_way_mask);
				iocp->ioc_cache_way &= cache_way_mask;
			}
			offset_aligned &= ~((uint64_t)PN_L3_WAY_MASK);
			offset_aligned |= (iocp->ioc_cache_way <<
			    PN_L3_WAY_SHIFT);
		} else {
			DPRINTF(0, "%s: invalid ERR_LEVEL\n", fname);
			return (ENOTSUP);
		}
	}

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * If the IOCTL specified the tag, corrupt the tag or the tag ECC.
	 *
	 * The xorpat determines if the tag data or the ECC is the target
	 * since the field is contiguous.  The format is as follows:
	 *	[24:19]	24-bit tag = PA[42:19]
	 *	[18:15]	unused
	 *	[14:6]	9-bit tag ecc
	 *	[5:3]	LRU
	 *	[2:0]	state
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		DPRINTF(3, "%s: calling pn_wr_%sphys_tag() with offset=0x%llx "
		    "and xorpat=0x%llx\n",
		    fname, (ERR_LEVEL_L3(iocp->ioc_command)) ? "l3":"l2",
		    offset_aligned, xorpat);

		if (ERR_LEVEL_L2(iocp->ioc_command)) {
			ret = pn_wr_l2phys_tag(offset_aligned, xorpat);
		} else if (ERR_LEVEL_L3(iocp->ioc_command)) {
			ret = pn_wr_l3phys_tag(offset_aligned, xorpat);
		} else {
			DPRINTF(0, "%s: invalid ERR_LEVEL\n", fname);
			return (ENOTSUP);
		}
	} else {
		/*
		 * Corrupt ecache data or ecc and set the offset value
		 * to get the correct 64-bit chunk of the cacheline based
		 * on the offset sent in.  Note that the L3 access ASI uses
		 * a different addressing width (32-byte vs. 64-byte for L2).
		 */
		if (F_CHKBIT(iocp)) {
			reg_offset =
			    (ERR_LEVEL_L3(iocp->ioc_command)) ? 4 : 8;

			/*
			 * The L3 has 9-bit ECC on each 16-byte chunk, so
			 * to inject into the higher ECC the xorpat is shifted
			 * to line up with the returned ECC from the ASI access.
			 */
			if (ERR_LEVEL_L3(iocp->ioc_command)) {
				if ((offset & 0x10) == 0) {
					xorpat = xorpat << 9;
				}
			}
		} else {
			reg_offset =
			    (ERR_LEVEL_L3(iocp->ioc_command)) ? 32 : 64;
			reg_offset = ((offset) % reg_offset) /
			    sizeof (uint64_t);
		}

		DPRINTF(3, "%s: calling pn_wr_%sphys_data(), reg_offset=%d, "
		    "offset=%llx, and xorpat=0x%llx\n",
		    fname, (ERR_LEVEL_L3(iocp->ioc_command)) ? "l3":"l2",
		    reg_offset, offset_aligned, xorpat);

		if (ERR_LEVEL_L2(iocp->ioc_command)) {
			ret = pn_wr_l2phys_data(offset_aligned, xorpat,
			    reg_offset);
		} else if (ERR_LEVEL_L3(iocp->ioc_command)) {
			ret = pn_wr_l3phys_data(offset_aligned, xorpat,
			    reg_offset);
		} else {
			DPRINTF(0, "%s: invalid ERR_LEVEL\n", fname);
			return (ENOTSUP);
		}
	}

	return (ret);
}

/*
 * This routine inserts an error into the L1 I-cache data (instr), or
 * cache tag, or the parity bit(s) protecting either the data or the
 * tag in a location specified by the byte offset in the ioc_addr member
 * of the mdata struct.
 */
int
pn_write_iphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	offset_aligned;
	uint64_t	xorpat;
	uint64_t	instr, tag = 0;
	char		*fname = "pn_write_iphys";

	DPRINTF(3, "%s: iocp=0x%p, offset=0x%llx\n", fname, iocp, offset);
	offset_aligned = P2ALIGN(offset, 8);

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * If the IOCTL specified the tag, corrupt the tag or the tag parity.
	 *
	 * The xorpat determines if the tag data or the parity is the target
	 * since the bit fields are contiguous (bit 42 is the parity bit).
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		if (F_CHKBIT(iocp)) {
			xorpat = (1ULL << 42);
		}

		/*
		 * If the xor pattern is explicitly specified to be 0, then
		 * rather than corrupting the existing i$ tag, it will be
		 * overwritten with the pattern in misc1 (by asm routine).
		 */
		if (F_MISC1(iocp)) {
			tag = iocp->ioc_misc1;
		}

		if ((xorpat == 0) && !F_MISC1(iocp)) {
			cmn_err(CE_WARN, "%s: when an xorpat of zero is used "
			    "the value to overwrite the i$ tag with should be "
			    "provided via the misc1 option.\nBy default the "
			    "tag will be overwritten with zeros.\n", fname);
		}

		memtest_disable_intrs();
		chp_wr_itphys(offset_aligned, xorpat, tag);
		memtest_enable_intrs();
	} else {
		/*
		 * Corrupt i-cache data or data parity.
		 * The xorpat determines if the tag data or the parity is
		 * the target since the bit fields are contiguous (bit 37
		 * is the tag parity bit).
		 * This is set in the command definition defaults.
		 */

		/*
		 * If the xor pattern is explicitly specified to be 0, then
		 * rather than corrupting the existing i$ data, it will be
		 * overwritten with the following pattern (by asm routine).
		 */
		if (F_MISC1(iocp)) {
			instr = iocp->ioc_misc1;
		} else {
			/*
			 * 0x91D0207c = "ta 0x7c" instruction
			 * 0x6 = predecode bits for this instruction
			 */
			instr = 0x691D0207c;
		}

		/*
		 * Note that SME recommended that we do not modify i$ entries
		 * by overwriting them since the predecode bits can not be set
		 * to a "safe" pattern. If someone asks for this warn them.
		 */
		if (xorpat == 0) {
			cmn_err(CE_WARN, "%s: overwriting i$ instructions or "
			    "predecode bits is not recommended\n", fname);
		}

		ch_wr_iphys(offset_aligned, xorpat, instr);
	}

	return (0);
}

/*
 * This routine inserts an error into the L1 D-cache data, or
 * cache tag, or the parity bit(s) protecting either the data or the
 * tag in a location specified by the byte offset in the ioc_addr member
 * of the mdata struct.
 */
int
pn_write_dphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	offset_aligned;
	uint64_t	xorpat;
	uint64_t	data, tag = 0;
	char		*fname = "pn_write_dphys";

	DPRINTF(3, "%s: iocp=0x%p, offset=0x%llx\n", fname, iocp, offset);
	offset_aligned = P2ALIGN(offset, 8);

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * If the IOCTL specified the tag, corrupt the tag or the tag parity.
	 *
	 * The xorpat determines if the tag data or the parity is the target
	 * since the bit fields are contiguous (bit 30 is the parity bit).
	 * This is set in the command definition defaults.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {

		/*
		 * If the xor pattern is explicitly specified to be 0, then
		 * rather than corrupting the existing d$ tag, it will be
		 * overwritten with the pattern in misc1 (by asm routine).
		 */
		if (F_MISC1(iocp)) {
			tag = iocp->ioc_misc1;
		}

		if ((xorpat == 0) && !F_MISC1(iocp)) {
			cmn_err(CE_WARN, "%s: when an xorpat of zero is used "
			    "the value to overwrite the d$ tag with should be "
			    "provided via the misc1 option.\nBy default the "
			    "tag will be overwritten with zeros.\n", fname);
		}

		chp_wr_dtphys(offset_aligned, xorpat, tag);
	} else {
		/*
		 * Corrupt d-cache data or data parity (bit 16 of the D-cache
		 * ASI allows the eight parity bits to be returned).
		 */
		if (F_CHKBIT(iocp)) {
			pn_wr_dphys_parity(offset_aligned, xorpat);
		} else {
			/*
			 * If the xor pattern is explicitly specified to be 0,
			 * rather than corrupting the existing d$ data, it will
			 * be overwritten with the following pattern.
			 * (by asm routine).
			 */
			if (F_MISC1(iocp)) {
				data = iocp->ioc_misc1;
			} else {
				data = 0xbaddc123baddc123;
			}

			ch_wr_dphys(offset_aligned, xorpat, data);
		}
	}

	return (0);
}

/*
 * The following two routines are used to cause HW corrected cache data or tag
 * errors that are detected as "sticky" by the error handler.
 *
 * This is accomplished using the following algorithm:
 *	1) Disable interrupts
 *	2) Inject an error
 *	3) Trigger the error
 *	4) Inject the same error again
 *	5) Enable interrupts
 *
 * Injecting and triggering a HW corrected and cleared error while interrupts
 * are disabled will result in the error being corrected and the AFSR or
 * AFSR_EXT being set but no disrupting trap will occur.  Instead, the
 * disrupting trap will be taken when interrupts are re-enabled.  By injecting
 * the error again before interrupts are re-enabled, the error handler is
 * tricked into thinking that the error is sticky and cannot be cleared.
 */
static uint64_t
pn_write_l2l3_data_stky(mdata_t *mdatap, uint64_t paddr, uint64_t tag_addr,
    uint64_t data_addr, uint64_t tag_cmp, uint64_t xorpat, int reg_offset)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		pstate;
	uint64_t	ret = 0;

	pstate = getpstate();
	setpstate(pstate & ~PSTATE_IE);

	ret = PN_L2L3_DATA(iocp->ioc_command, tag_addr, data_addr,
	    tag_cmp, xorpat, reg_offset);

	if (ret == 0xfeccf) {
		goto pn_data_stky_exit;
	}

	mdatap->m_blkld(mdatap->m_kvaddr_a);

	pn_flushall_l2();
	pn_flushall_l3();

	if (F_CACHE_DIRTY(iocp)) {
		if (ch_wc_is_enabled()) {
			if (memtest_flags & MFLAGS_CH_USE_FLUSH_WC) {
				pn_flush_wc(paddr);
			}
		}
		stphys(paddr, ldphys(paddr));
	} else if (F_CACHE_CLN(iocp)) {
		(void) ldphys(paddr);
	} else if ((ERR_ACC_ISFETCH(iocp->ioc_command) ||
	    ERR_ACC_ISPFETCH(iocp->ioc_command)) &&
	    !ERR_CLASS_ISL2WB(iocp->ioc_command) &&
	    !ERR_CLASS_ISL3WB(iocp->ioc_command)) {
		(void) ldphys(paddr);
	} else { /* modify by default */
		if (ch_wc_is_enabled()) {
			if (memtest_flags & MFLAGS_CH_USE_FLUSH_WC) {
				pn_flush_wc(paddr);
			}
		}
		stphys(paddr, ldphys(paddr));
	}

	/*
	 * The Panther L2/L3 semantics has the load bypassing the L3
	 * and allocating in L2. Therefore in order to inject into L3
	 * the line has to be flushed from L2 to L3.
	 */
	if (ERR_LEVEL_L3(iocp->ioc_command)) {
		pn_flush_l2_line(paddr);
	}

	ret = PN_L2L3_DATA(iocp->ioc_command, tag_addr, data_addr,
	    tag_cmp, xorpat, reg_offset);

	/*
	 * If an error was returned, differentiate it from the first
	 * error injection with a new error code.
	 */
	if (ret == 0xfeccf)
		ret = 0xffccd;

pn_data_stky_exit:
	setpstate(pstate | PSTATE_IE);
	membar_sync();
	return (ret);
}

static uint64_t
pn_write_l2l3_tag_stky(mdata_t *mdatap, uint64_t paddr, uint64_t xorpat)
{
	ioc_t			*iocp = mdatap->m_iocp;
	volatile caddr_t	vaddr = mdatap->m_kvaddr_a;
	uint_t			pstate;
	uint64_t		ret = 0;

	pstate = getpstate();
	setpstate(pstate & ~PSTATE_IE);

	ret = PN_L2L3_TAG(iocp->ioc_command, paddr, xorpat);

	if (ret == 0xfeccf) {
		goto pn_tag_stky_exit;
	}

	*vaddr = (uchar_t)0xff;
	membar_sync();

	pn_flushall_l2();
	pn_flushall_l3();

	if (F_CACHE_DIRTY(iocp)) {
		if (ch_wc_is_enabled()) {
			if (memtest_flags & MFLAGS_CH_USE_FLUSH_WC) {
				pn_flush_wc(paddr);
			}
		}
		stphys(paddr, ldphys(paddr));
	} else if (F_CACHE_CLN(iocp)) {
		(void) ldphys(paddr);
	} else if ((ERR_ACC_ISFETCH(iocp->ioc_command) ||
	    ERR_ACC_ISPFETCH(iocp->ioc_command)) &&
	    !ERR_CLASS_ISL2WB(iocp->ioc_command) &&
	    !ERR_CLASS_ISL3WB(iocp->ioc_command)) {
		(void) ldphys(paddr);
	} else { /* modify by default */
		if (ch_wc_is_enabled()) {
			if (memtest_flags & MFLAGS_CH_USE_FLUSH_WC) {
				pn_flush_wc(paddr);
			}
		}
		stphys(paddr, ldphys(paddr));
	}

	/*
	 * The Panther L2/L3 semantics has the load bypassing the L3
	 * and allocating in L2. Therefore in order to inject into L3
	 * the line has to be flushed from L2 to L3.
	 */
	if (ERR_LEVEL_L3(iocp->ioc_command)) {
		pn_flush_l2_line(paddr);
	}

	ret = PN_L2L3_TAG(iocp->ioc_command, paddr, xorpat);

	/*
	 * If an error was returned, differentiate it from the first
	 * error injection with a new error code.
	 */
	if (ret == 0xfeccf)
		ret = 0xffccd;

pn_tag_stky_exit:
	setpstate(pstate | PSTATE_IE);
	membar_sync();
	return (ret);
}

/*
 * Panther I-D TLB error injector. Calls the specific I/D injectors
 * depending on user command.
 */
int
pn_write_tlb(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		err_acc = ERR_ACC(iocp->ioc_command);
	int		err_mode =  ERR_MODE(iocp->ioc_command);
	caddr_t		va, va_aligned;
	struct hat 	*sfmmup;
	proc_t		*procp;
	uint32_t	ctxnum;
	tte_t		tte;

	DPRINTF(3, "pn_write_tlb: \n");

	if (err_mode == ERR_MODE_KERN)
		va = mdatap->m_kvaddr_c;
	else if (err_mode == ERR_MODE_USER)
		va = mdatap->m_uvaddr_c;

	/* addr of base page for sfmmu_gettte()  must be 8k aligned */
	va_aligned = (caddr_t)((uint64_t)va & (uint64_t)(~MMU_PAGEOFFSET));

	if (err_mode == ERR_MODE_KERN) {
		sfmmup = ksfmmup;
		ctxnum = KCONTEXT;
	} else if (err_mode == ERR_MODE_USER) {
		/* fetch user hat struct from curproc/context */
		procp = curproc;
		sfmmup = procp->p_as->a_hat;
		ctxnum = sfmmup->sfmmu_ctxs[CPU_MMU_IDX(CPU)].cnum;
	}

	DPRINTF(2, "pn_write_tlb: va_aligned (8k): %llx \tctxnum = %x\n",
	    va_aligned, ctxnum);

	if (memtest_get_tte(sfmmup, (caddr_t)va_aligned, &tte) != 0) {
		DPRINTF(0, "pn_write_tlb: memtest_get_tte() FAILED!\n");
		return (ENXIO);
	}

	DPRINTF(2, "pn_write_tlb: tte = %llx\n", tte.ll);

	if (err_acc == ERR_ACC_LOAD)
		return (pn_write_dtlb(mdatap, va, tte.ll, ctxnum));
	else if (err_acc == ERR_ACC_FETCH)
		return (pn_write_itlb(mdatap, va, tte.ll, ctxnum));
	else {
		DPRINTF(0,
		    "pn_write_tlb: Unknown TLB errror command! Exiting...\n");
		return (ENXIO);
	}
}

/*
 * Inject an error in the DTLB 512{0, 1} arrays for the TTE
 * corresponding to the VA to be corrupted.
 */
static int
pn_write_dtlb(mdata_t *mdatap, caddr_t va, uint64_t tteval, uint32_t ctxnum)
{
	uint64_t		pa;
	uint64_t		ret;
	uint32_t		index;
	uint64_t		x64;
	uint32_t		uval32;
	ioc_t			*iocp = mdatap->m_iocp;
	int			err_mode =  ERR_MODE(iocp->ioc_command);
	uint64_t		xorpat;

	xorpat = IOC_XORPAT(iocp);

	/*
	 * Fetch the pagesize for the <VA,TTE> tuple. This is used to index
	 * into the 512_{0,1} DTLB arrays based on the page size of
	 * the missed page.
	 * See Panther PRM F-1.1.0 and F-1.1.2 for relevant information.
	 *
	 * NOTE:
	 * This injector needs to be more sophisticated:
	 * 1. Right now 8k page size for 512_0{1} is
	 *   assumed. It needs to look at the DMMU Primary Context Register and
	 *   fetch the pagesize bits for the Primary context pagesize bits and
	 *   compare it with the pagesize bits of the TTE to look at
	 *   the appropriate 512_{0,1} array.
	 *   We just probe both 512_{0,1} arrays for a match for
	 *   the given TTE page size and return failure if not found.
	 * 	This seems to work on all current tests.
	 *
	 * 2. Further, if the TTE is not found in  512_{0,1}arrays,
	 *    we need to demap an existing entry, try reloading the TTE into
	 *    the 512s and retry the probes, instead of giving up.
	 */

	index =	VA2INDEX(va, tteval);
	DPRINTF(2, "pn_write_dtlb: index = 0x%x  0t%d\n", index, index);

	pa = mdatap->m_paddr_c;
	DPRINTF(2, "pn_write_dtlb: va->pa = %llx -> %llx m_paddr_c=%llx\n",
	    va, pa, mdatap->m_paddr_c);

	/*
	 * reload data addr. to make sure we have the entry
	 * in the TLB
	 */
	if (err_mode == ERR_MODE_KERN) {
		*(uint64_t *)va = (uint64_t)0xB0B0B0B0B0B0B0B0;
		x64 = (uint64_t)(*(uint64_t *)va);
	} else { /* peek at user addr. from kernel land */
		uval32 = memtest_get_uval((uint64_t)va);
	}

	DPRINTF(3, "pn_write_dtlb: kern/user data=%llx %llx\n", x64, uval32);

	/* probe 512_0 */
	ret = pn_wr_dtlb_parity_idx(index, (uint64_t)va, pa,
	    TLB_512_0_DATAMASK, xorpat, ctxnum);

	if (ret == 0xfeecf) {
		DPRINTF(2,
		"pn_write_dtlb: va=%llx NOT found in DTLB:512_0:0x[%x]:\n",
		    va, index);
	} else {
		DPRINTF(1, "pn_write_dtlb: ret = %llx, %p->%p @ 512_0[%x]\n",
		    ret, va, pa, index);
		return (0);
	}

	/* probe 512_1 */
	ret = pn_wr_dtlb_parity_idx(index, (uint64_t)va, pa,
	    TLB_512_1_DATAMASK, xorpat, ctxnum);

	if (ret == 0xfeecf) {
		DPRINTF(0,
		"pn_write_dtlb:  va=%llx NOT found in 512_1[%llx]:\n",
		    va, index);
		ret = ENXIO;
	} else {
		DPRINTF(2, "pn_write_dtlb: ret = %llx %p->%p @ 512_1:0x[%lx]\n",
		    va, pa, index);
		ret = 0;
	}

	if (memtest_debug) {
		pn_dump_dtlb(index);
	}

	return (ret);
}

/*
 * Place an entry directly into the instruction TLB.
 */
static int
pn_write_itlb(mdata_t *mdatap, caddr_t va, uint64_t tteval, uint32_t ctxnum)
{
	uint64_t	pa;
	uint64_t	ret;
	uint32_t	index;
	ioc_t		*iocp = mdatap->m_iocp;
	int		err_mode =  ERR_MODE(iocp->ioc_command);
	uint64_t	xorpat;

	xorpat = IOC_XORPAT(iocp);

	/*
	 * Fetch the pagesize for the <VA,TTE> tuple. This is used to index
	 * into the 512{0} ITLB arrays based on the page size of the
	 * missed instruction page.
	 * See Panther PRM F-1.1.0 and F-1.1.2 for relevant information.
	 *
	 * NOTE:
	 * This injector needs to be more sophisticated:
	 * 1. Right now 8k page size for 512_{0} is
	 *   assumed. It needs to look at the IMMU Primary Context Register and
	 *   fetch the pagesize bits for the Primary context pagesize bits and
	 *   compare it with the pagesize bits of the TTE to look at the
	 *   appropriate 512 {0,1} array.
	 *   Right now, we just probe both 512_{0} array for a match for
	 *   the given TTE page size and return failure if not found.
	 * 	This seems to work on all current tests.
	 *
	 * 2. Further, if the TTE is not found in  512_{0}arrays,
	 *    we need to demap an existing entry, try reloading the TTE into
	 *    the 512s and retry the probe instead of giving up.
	 */
	index =	VA2INDEX(va, tteval);
	DPRINTF(2, "pn_write_itlb: index = 0x%x  0t%d   tteval=%llx\n",
	    index, index, tteval);

	pa = mdatap->m_paddr_c;
	DPRINTF(2,
	    "pn_write_itlb: va->pa : %llx -> %llx ctxnum=%x m_paddr_c=%llx\n",
	    va, pa, ctxnum, mdatap->m_paddr_c);

	/*
	 * reexec in kernel mode to make sure its in I-TLB
	 * just in case ; User error: we just have to hope that this
	 * entry was not evicted, yet.
	 */
	if (err_mode == ERR_MODE_KERN)
		mdatap->m_asmld(mdatap->m_kvaddr_c);

	ret = pn_wr_itlb_parity_idx(index, (uint64_t)va, pa,
	    TLB_512_0_DATAMASK, xorpat, ctxnum);

	if (ret == 0xfeecf) {
		DPRINTF(0, "pn_write_itlb: va=%p NOt found in 512_0[%llx]\n",
		    va, index);
		ret = ENXIO;
	} else {
		DPRINTF(2, "pn_write_itlb: ret=%llx Found %p->%p @ 512_0[%x]\n",
		    ret, va, pa, index);
		ret = 0;
	}

	if (memtest_debug) {
		pn_dump_itlb(index);
	}

	return (ret);
}

/*
 * Inject an error in the P-Cache data
 */
int
pn_write_pcache(mdata_t *mdatap)
{
	uint64_t	va;
	uint64_t	pa;
	uint64_t	ret = 0;
	uint64_t	data, tag;
	uint64_t	kva;
	ioc_t		*iocp = mdatap->m_iocp;
	int		err_acc = ERR_ACC(iocp->ioc_command);

	kva = va = (uint64_t)(mdatap->m_kvaddr_c);
	pa = mdatap->m_paddr_c;
	DPRINTF(2, "pn_write_pcache: va->pa : %llx -> %llx m_paddr_c=%llx\n",
	    va, pa, mdatap->m_paddr_c);

	memtest_disable_intrs();
	OP_FLUSHALL_CACHES(mdatap);

	/* Disable P$ -- invalidates all entries == flush */
	data = pn_disable_pcache();
	tag = pn_enable_pcache();
	DPRINTF(2, "pn_write_pcache: dcu vals = %llx    %llx\n", data, tag);

	/*
	 * Load a specific debug pattern into the line. This is
	 * especially useful in diagnosing the finicky P$ injection.
	 */
	*(uint64_t *)kva = (uint64_t)0xB0B0B0B0B0B0B0B0; kva += 8;
	*(uint64_t *)kva = (uint64_t)0xB1B1B1B1B1B1B1B1; kva += 8;
	*(uint64_t *)kva = (uint64_t)0xB2B2B2B2B2B2B2B2; kva += 8;
	*(uint64_t *)kva = (uint64_t)0xB3B3B3B3B3B3B3B3; kva += 8;
	*(uint64_t *)kva = (uint64_t)0xB4B4B4B4B4B4B4B4; kva += 8;
	*(uint64_t *)kva = (uint64_t)0xB5B5B5B5B5B5B5B5; kva += 8;
	*(uint64_t *)kva = (uint64_t)0xB6B6B6B6B6B6B6B6; kva += 8;
	*(uint64_t *)kva = (uint64_t)0xB7B7B7B7B7B7B7B7;

	/* do a prefetch load ; this should get the line into P$ */
	(void) pn_pcache_load((uint64_t)va);

	/* induce P$ parity error */
	ret = pn_pcache_write_parity((uint64_t)va, pa);

	/* invoke the error by fp load */
	if (err_acc == ERR_ACC_LOAD) {
		if (F_NOERR(iocp)) { /* Don't invoke error */
			DPRINTF(2, "pn_write_pcache: not invoking error\n");
			goto pn_pc_exit;
		}
		/* invoke error */
		(void) pn_load_fp((uint64_t)(va));
	} else {
		DPRINTF(0,
		    "pn_write_pcache: Invalid P-Cache command! Exiting\n");
		ret = ENXIO;
	}
	DPRINTF(2, "pn_write_pcache: ret = %llx\n", ret);

pn_pc_exit:
	/* enable P$::SPE on way out */
	(void) memtest_set_dcucr(memtest_get_dcucr() | DCU_SPE);
	memtest_enable_intrs();

	if (ret == 0xfeecf)
		return (ret);
	else
		return (0);	/* return tag on success */
}

/*
 * Debug function that dumps the D-TLB.
 */
static void
pn_dump_dtlb(int index)
{
	uint64_t tag, data;

	/*
	 * This is only called when diagnosing problems.
	 * So leave at debug level 0.
	 */
	DPRINTF(0, "pn_dump_dtlb: Srch Index: 0x%x, %d\n", index, index);
	(void) pn_get_dtlb_entry(index, 0, TLB_512_0_DATAMASK, &tag, &data);
	DPRINTF(0, "512_0[%x]w0: Tag, Data =  %llx, %llx\n", index, tag, data);
	(void) pn_get_dtlb_entry(index, 1, TLB_512_0_DATAMASK, &tag, &data);
	DPRINTF(0, "512_0[%x]w1: Tag, Data =  %llx, %llx\n", index, tag, data);

	(void) pn_get_dtlb_entry(index, 0, TLB_512_1_DATAMASK, &tag, &data);
	DPRINTF(0, "512_1[%x]w0: Tag, Data = %llx, %llx\n", index, tag, data);
	(void) pn_get_dtlb_entry(index, 1, TLB_512_1_DATAMASK, &tag, &data);
	DPRINTF(0, "512_1[%x]w1: Tag, Data = %llx, %llx\n", index, tag, data);
}

/*
 * Debug function that dumps the I-TLB.
 */
static void
pn_dump_itlb(int index)
{
	uint64_t tag, data;

	/*
	 * This is only called when diagnosing problems.
	 * So leave at debug level 0.
	 */
	DPRINTF(0, "pn_dump_itlb: Srch Index: 0x%x, %d\n", index, index);
	(void) pn_get_itlb_entry(index, 0, TLB_512_0_DATAMASK, &tag, &data);
	DPRINTF(0, "512_0[%x]w0: Tag, Data =  %llx, %llx\n", index, tag, data);
	(void) pn_get_itlb_entry(index, 1, TLB_512_0_DATAMASK, &tag, &data);
	DPRINTF(0, "512_0[%x]w1: Tag, Data =  %llx, %llx\n", index, tag, data);
}

/*
 * I$ prefetch buffer error; Currently only on Panther chips
 */
int
pn_ipb_err(mdata_t *mdatap)
{
	caddr_t		vaddr;
	uint64_t	dcu_val = memtest_get_dcucr();
	uint64_t	junk;
	uint64_t	paddr;
	uint64_t	ret = 0x00;
	ioc_t		*iocp = mdatap->m_iocp;
	int		xorpat = IOC_XORPAT(iocp);

	/*
	 * Set IPS to new stride value first.
	 * See comments in pn_wr_ipb() for stride value set.
	 */
	(void) memtest_set_dcucr((dcu_val & ~PN_IPS_MASK) | PN_IPS_192);

	/* Get paddr of instr buffer */
	vaddr = (caddr_t)&pn_ipb_asmld;
	if ((paddr = memtest_kva_to_pa(vaddr)) == (uint64_t)-1) {
		DPRINTF(0, "pn_ipb_err: memtest_kva_to_pa(%x%p) FAILED!\n",
		    vaddr);
		return (1);
	}

	DPRINTF(2, "pn_ipb_err: va-pa = %llx -> %llx\n", vaddr, paddr);

	DPRINTF(2, "pn_ipb_err: dcu_val=%llx xor=%x\n", dcu_val, xorpat);

	memtest_disable_intrs();

	/* Check line in IPB and corrupt */
	ret = pn_wr_ipb(paddr, xorpat);

	/* Reload to trigger parity trap */
	pn_ipb_asmld((caddr_t)&junk, (caddr_t)&junk);

	/* Restore original IPS settings */
	(void) memtest_set_dcucr(dcu_val);

	memtest_enable_intrs();

	if (ret == 0xfeccf) {
		DPRINTF(0, "pn_ipb_err: TEST FAILED: IPB parity\n");
		return (ENXIO);
	}

	return (0);
}

/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains spitfire specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_sf.h>
#include <sys/memtest_u.h>
#include <sys/memtest_sf.h>

/*
 * Routines located in this file.
 */
static	int	sf_enable_errors(mdata_t *);
static	int	sf_flushall_caches(cpu_info_t *);
static	int	sf_flushall_dcache(cpu_info_t *);
static	int	sf_flush_dc_entry(cpu_info_t *cip, caddr_t vaddr);
static	int	sf_gen_ecc(uint64_t);
static	int	sf_get_cpu_info(cpu_info_t *);
	void	sf_init(mdata_t *);
static	int	sf_write_ecache(mdata_t *);
static	int	sf_write_ephys(mdata_t *);
static	int	sf_write_etphys(mdata_t *);

/*
 * These Spitfire errors are grouped according to the definitions
 * in the header file.
 */
cmd_t spitfire_cmds[] = {
	SF_KD_BE,		memtest_k_bus_err,	"memtest_k_bus_err",
	SF_KD_BEPEEK,		memtest_k_bus_err,	"memtest_k_bus_err",

	SF_KD_EDP,		memtest_k_l2_err,	"memtest_k_l2_err",
	SF_KU_EDPCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	SF_KD_EDPTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	SF_KI_EDP,		memtest_k_l2_err,	"memtest_k_l2_err",
	SF_KI_EDPTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	SF_UD_EDP,		memtest_u_l2_err,	"memtest_u_l2_err",
	SF_UI_EDP,		memtest_u_l2_err,	"memtest_u_l2_err",
	SF_OBPD_EDP,		memtest_obp_err,	"memtest_obp_err",
	SF_KD_ETP,		memtest_k_l2_err,	"memtest_k_l2_err",

	SF_KD_WP,		memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	SF_KI_WP,		memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	SF_UD_WP,		memtest_u_l2_err,	"memtest_u_l2_err",

	SF_KD_CP,		memtest_k_cp_err,	"memtest_k_cp_err",
	SF_UD_CP,		memtest_u_l2_err,	"memtest_u_l2_err",

	NULL,			NULL,			NULL,
};

static cmd_t *commands[] = {
	spitfire_cmds,
	sun4u_generic_cmds,
	NULL
};

/*
 * Spitfire operations vector tables.
 */
static opsvec_u_t spitfire_uops = {
	/* sun4u injection ops vectors */
	notsup,			/* corrupt d$ tag at offset */
	notsup,			/* corrupt i$ tag at offset */
	sf_write_etphys,	/* corrupt e$ tag at offset */
	sf_write_memory,	/* corrupt memory at paddr */
	notsup,			/* no mtag routine */
	notsup,			/* no p-cache routine */

	/* sun4u support ops vectors */
	sf_gen_ecc,		/* generate ecc for data passed in */
};

static opsvec_c_t spitfire_cops = {
	/* common injection ops vectors */
	notsup,			/* no dcache routine */
	notsup,			/* no dcache offset routine */
	notsup,			/* no corrupt fp reg */
	notsup,			/* no icache routine */
	notsup,			/* no corrupt internal */
	notsup,			/* no iphys routine */
	notsup,			/* no corrupt int reg */
	sf_write_ecache,	/* corrupt e$ at paddr */
	sf_write_ephys,		/* corrupt e$ at offset */
	notsup,			/* no corrupt l3$ at paddr */
	notsup,			/* no corrupt l3$ at offset */
	notsup,			/* no TLB parity errors */

	/* common support ops vectors */
	notsup,			/* no fp reg access */
	notsup,			/* no int reg access */
	memtest_check_afsr,	/* check ESRs */
	sf_enable_errors,	/* enable AFT errors */
	notimp,			/* no enable/disable L2 or memory scrubbers */
	sf_get_cpu_info,	/* put cpu info into struct */
	sf_flushall_caches,	/* flush all caches */
	sf_flushall_dcache,	/* flush all d$ */
	notsup,			/* no flush all i$ */
	gen_flushall_l2,	/* flush all l2$ */
	notsup,			/* no flush all l3$ */
	sf_flush_dc_entry,	/* flush d$ entry */
	gen_flush_ic_entry,	/* flush i$ entry (prefetch buffers only) */
	gen_flush_l2_entry,	/* flush l2$ entry - not used */
	notsup,			/* no flush l3$ entry */
};

/*
 * Sabre operations vector tables.
 */
static opsvec_u_t sabre_uops = {
	/* sun4u injection ops vectors */
	notsup,			/* corrupt d$ tag at offset */
	notsup,			/* corrupt i$ tag at offset */
	sf_write_etphys,	/* corrupt e$ tag at offset */
	sf_write_memory,	/* corrupt memory at paddr */
	notsup,			/* no mtag routine */
	notsup,			/* no p-cache routine */

	/* sun4u support ops vectors */
	sf_gen_ecc,		/* generate ecc for data passed in */
};

static opsvec_c_t sabre_cops = {
	/* common injection ops vectors */
	notsup,			/* no dcache routine */
	notsup,			/* no dcache offset routine */
	notsup,			/* no corrupt fp reg */
	notsup,			/* no icache routine */
	notsup,			/* no corrupt internal */
	notsup,			/* no iphys routine */
	notsup,			/* no corrupt int reg */
	sf_write_ecache,	/* corrupt e$ at paddr */
	sf_write_ephys,		/* corrupt e$ at offset */
	notsup,			/* no corrupt l3$ at paddr */
	notsup,			/* no corrupt l3$ at offset */
	notsup,			/* no TLB parity errors */

	/* common support ops vectors */
	notsup,			/* no fp reg access */
	notsup,			/* no int reg access */
	memtest_check_afsr,	/* check ESRs */
	sf_enable_errors,	/* enable AFT errors */
	notimp,			/* no enable/disable L2 or memory scrubbers */
	sf_get_cpu_info,	/* put cpu info into struct */
	sf_flushall_caches,	/* flush all caches */
	sf_flushall_dcache,	/* flush all d$ */
	notsup,			/* no flush all i$ */
	gen_flushall_l2,	/* flush all l2$ */
	notsup,			/* no flush all l3$ */
	sf_flush_dc_entry,	/* flush d$ entry */
	gen_flush_ic_entry,	/* flush i$ entry (prefetch buffers only) */
	gen_flush_l2_entry,	/* flush l2$ entry - not used */
	notsup,			/* no flush l3$ entry */
};

/*
 * Hummingbird operations vector table.
 */
static opsvec_u_t hummingbird_uops = {
	/* sun4u injection ops vectors */
	notsup,			/* corrupt d$ tag at offset */
	notsup,			/* corrupt i$ tag at offset */
	sf_write_etphys,	/* corrupt e$ tag at offset */
	hb_write_memory,	/* corrupt memory at paddr */
	notsup,			/* no mtag routine */
	notsup,			/* no p-cache routine */

	/* sun4u support ops vectors */
	sf_gen_ecc,		/* generate ecc for data passed in */
};

static opsvec_c_t hummingbird_cops = {
	/* common injection ops vectors */
	notsup,			/* no dcache routine */
	notsup,			/* no dcache offset routine */
	notsup,			/* no corrupt fp reg */
	notsup,			/* no icache routine */
	notsup,			/* no corrupt internal */
	notsup,			/* no iphys routine */
	notsup,			/* no corrupt int reg */
	sf_write_ecache,	/* corrupt e$ at paddr */
	sf_write_ephys,		/* corrupt e$ at offset */
	notsup,			/* no corrupt l3$ at paddr */
	notsup,			/* no corrupt l3$ at offset */
	notsup,			/* no TLB parity errors */

	/* common support ops vectors */
	notsup,			/* no fp reg access */
	notsup,			/* no int reg access */
	memtest_check_afsr,	/* check ESRs */
	sf_enable_errors,	/* enable AFT errors */
	notimp,			/* no enable/disable L2 or memory scrubbers */
	sf_get_cpu_info,	/* put cpu info into struct */
	sf_flushall_caches,	/* flush all caches */
	sf_flushall_dcache,	/* flush all d$ */
	notsup,			/* no flush all i$ */
	gen_flushall_l2,	/* flush all l2$ */
	notsup,			/* no flush all l3$ */
	sf_flush_dc_entry,	/* flush d$ entry */
	gen_flush_ic_entry,	/* flush i$ entry (prefetch buffers only) */
	gen_flush_l2_entry,	/* flush l2$ entry - not used */
	notsup,			/* no flush l3$ entry */
};

/*
 * The following spitfire ECC generation routine and its table are an
 * implementation of the algorithm defined in the IEEE paper "A Class
 * of Odd-Weight-Column SEC-DED-S4ED Codes for Memory System Applications"
 * which appeared in volume c-33 of Transactions on Computers in August 1984.
 * It has been verified against the Verilog implementation for the UPA.
 */

static uint64_t sf_code[8] = {
	0xee55de2316161161ULL,
	0x55eede9361612212ULL,
	0xbb557b8c49494494ULL,
	0x55bb7b6c94948848ULL,
	0x16161161ee55de23ULL,
	0x6161221255eede93ULL,
	0x49494494bb557b8cULL,
	0x9494884855bb7b6cULL
};

/*
 * The routine injects an error into memory at the specified paddr.
 */
int
hb_write_memory(mdata_t *mdatap, uint64_t paddr, caddr_t kvaddr, uint_t ecc)
{
	DPRINTF(3, "hb_write_memory: mdatap=0x%p, paddr=0x%llx, "
		"kvaddr=0x%p, ecc=0x%x\n", mdatap, paddr, kvaddr, ecc);

	return (hb_wr_memory(paddr, mdatap->m_cip->c_l2_size, ecc));
}

/*
 * The routine injects an error into memory at the specified paddr.
 */
int
sf_write_memory(mdata_t *mdatap, uint64_t paddr, caddr_t kvaddr, uint_t ecc)
{
	DPRINTF(3, "sf_write_memory: mdatap=0x%p, paddr=0x%llx, "
		"kvaddr=0x%p, ecc=0x%x\n",
		mdatap, paddr, kvaddr, ecc);

	return (sf_wr_memory(paddr, mdatap->m_cip->c_l2_size, ecc));
}

/*
 * This routine enables errors traps and E$ ECC checking.
 */
int
sf_enable_errors(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	uint64_t	exp, obs, tmpl;

	switch (CPU_IMPL(cip->c_cpuver)) {
	case SPITFIRE_IMPL:
	case BLACKBIRD_IMPL:
		exp = EER_ISAPEN | EN_REG_NCEEN | EN_REG_CEEN;
		break;
	case SABRE_IMPL:
	case HUMMBRD_IMPL:
		exp = EER_EPEN | EER_UEEN | EER_NCEEN | EER_CEEN;
		break;
	default:
		DPRINTF(0, "sf_enable_errors: unsupported CPU type, "
			"impl=0x%llx\n", CPU_IMPL(cip->c_cpuver));
		return (ENOTSUP);
	}

	obs = memtest_get_eer();
	DPRINTF(1, "sf_enable_errors: exp_ec_err=0x%x, "
		"obs_ec_err=0x%x\n", exp, obs);
	if ((obs & exp) != exp) {
		cmn_err(CE_WARN, "E$ error traps were not enabled "
			"(e$_err_enable_reg=0x%08x.%08x)!\n",
			PRTF_64_TO_32(obs));
		(void) memtest_set_eer(obs | exp);
		tmpl = memtest_get_eer();
		cmn_err(CE_WARN, "E$ error traps are now enabled "
			"(e$_err_enable_reg=0x%08x.%08x)\n",
			PRTF_64_TO_32(tmpl));
	}

	return (0);
}

/*ARGSUSED*/
static int
sf_gen_ecc(uint64_t paddr)
{
	uint64_t	data;
	uint64_t	masked_data[8];
	uint64_t	paddr_aligned;
	uint_t		i, j, bit_mask;
	int		check_bits = 0;

	paddr_aligned = P2ALIGN(paddr, 8);
	data = lddphys(paddr_aligned);

	for (i = 0; i < 8; i++)
		masked_data[i] = data & sf_code[i];

	for (i = 0; i < 8; i++) {
		bit_mask = 1 << i;
		for (j = 0; j < 64; j++) {
			if (masked_data[i] & 1)
				check_bits ^= bit_mask;
			masked_data[i] >>= 1;
		}
	}

	return (check_bits);
}

int
sf_get_cpu_info(cpu_info_t *cip)
{
	cip->c_dc_size = 16 * 1024;
	cip->c_dc_linesize = 16;
	cip->c_dc_assoc = 1;

	cip->c_ic_size = 16 * 1024;
	cip->c_ic_linesize = 32;
	cip->c_ic_assoc = 1;

	cip->c_l2_size = cpunodes[cip->c_cpuid].ecache_size;
	cip->c_l2_linesize = cip->c_l2_sublinesize = 64;

	switch (CPU_IMPL(cip->c_cpuver)) {
	case SPITFIRE_IMPL:
	case BLACKBIRD_IMPL:
	case SABRE_IMPL:
		cip->c_l2_assoc = 1;
		break;
	case HUMMBRD_IMPL:
		cip->c_l2_assoc = 4;
		break;
	default:
		DPRINTF(0, "sf_get_cpu_info: unsupported CPU type, "
			"impl=0x%llx\n", CPU_IMPL(cip->c_cpuver));
		return (ENOTSUP);
	}

	cip->c_l2_flushsize = cip->c_l2_size * cip->c_l2_assoc * 2;

	return (0);
}

void
sf_init(mdata_t *mdatap)
{
	switch (CPU_IMPL(mdatap->m_cip->c_cpuver)) {
	case SABRE_IMPL:
		mdatap->m_sopvp = &sabre_uops;
		mdatap->m_copvp = &sabre_cops;
		break;
	case HUMMBRD_IMPL:
		mdatap->m_sopvp = &hummingbird_uops;
		mdatap->m_copvp = &hummingbird_cops;
		break;
	default:
		mdatap->m_sopvp = &spitfire_uops;
		mdatap->m_copvp = &spitfire_cops;
		break;
	}
	mdatap->m_cmdpp = commands;
}

static int
sf_write_ecache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	xorpat;

	DPRINTF(3, "sf_write_ecache: mdatap=0x%p, paddr=0x%llx\n",
		mdatap, paddr);

	/*
	 * An xor corruption pattern is only used for e$ tag errors.
	 * For e$ data errors, a different corruption offset address
	 * can be used to cause corruption on a different byte.
	 * If however, we wanted to cause corruption on combinations
	 * of e$ data bytes then we would have to add xor pattern
	 * support to sf_wr_ecache().
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {
		xorpat = IOC_XORPAT(iocp);
		if (CPU_IMPL(mdatap->m_cip->c_cpuver) == HUMMBRD_IMPL) {
			DPRINTF(3, "sf_write_ecache: calling "
				"hb_wr_ecache_tag(paddr=0x%llx, "
				"xorpat=0x%llx)\n", paddr, xorpat);
			if ((hb_wr_ecache_tag(paddr, mdatap->m_cip->c_l2_size,
			    xorpat)) != 0) {
				DPRINTF(0, "sf_write_ecache: No matching tag "
					"found\n");
				return (1);
			}
		} else {
			DPRINTF(3, "sf_write_ecache: calling "
				"sf_wr_ecache_tag(paddr=0x%llx, "
				"xorpat=0x%llx)\n", paddr, xorpat);
			sf_wr_ecache_tag(paddr, xorpat);
		}
	} else {
		DPRINTF(3, "sf_write_ecache: calling "
			"sf_wr_ecache(paddr=0x%llx)\n", paddr);
		sf_wr_ecache(paddr);
	}

	return (0);
}

/*ARGSUSED*/
static int
sf_write_ephys(mdata_t *mdatap)
{
	uint64_t	offset = mdatap->m_iocp->ioc_addr;

	sf_wr_ephys(offset);

	return (0);
}

/*ARGSUSED*/
static int
sf_write_etphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;

	DPRINTF(3, "sf_write_etphys: calling sf_wr_etphys(pa=0x%llx, "
		"xor=0x%llx)\n", offset, IOC_XORPAT(iocp));

	sf_wr_etphys(offset, IOC_XORPAT(iocp));

	return (0);
}

/*ARGSUSED*/
static int
sf_flushall_caches(cpu_info_t *cip)
{
	cpu_flush_ecache();
	return (0);
}

static int
sf_flushall_dcache(cpu_info_t *cip)
{
	(void) sf_flushall_dc(cip->c_dc_size, cip->c_dc_linesize);
	return (0);
}

/*ARGSUSED*/
static int
sf_flush_dc_entry(cpu_info_t *cip, caddr_t vaddr)
{
	/*
	 * Note that the called asm routine aligns the address.
	 */
	(void) sf_flush_dc(vaddr);
	return (0);
}

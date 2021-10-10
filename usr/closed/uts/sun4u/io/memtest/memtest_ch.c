/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains cheetah specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_ch.h>
#include <sys/memtest_u.h>
#include <sys/memtest_ch.h>
#include <sys/memtest_ja.h>
#include <sys/memtest_sr.h>

/*
 * Cheetah specific routines located in this file.
 */
	void	ch_disable_wc(mdata_t *);
	void	ch_dump_ec_data(char *, ch_ec_t *);
	int	ch_enable_errors(mdata_t *);
	void	ch_enable_wc(void);
	int	ch_flushall_caches(cpu_info_t *);
	int	ch_flushall_dcache(cpu_info_t *);
	int	ch_flushall_icache(cpu_info_t *);
	int	ch_flush_dc_entry(cpu_info_t *, caddr_t);
	int	ch_gen_ecc_pa(uint64_t);
static	int	ch_gen_ecc_data(uint64_t, uint64_t);
	int	ch_get_cpu_info(cpu_info_t *);
	void	ch_init(mdata_t *);
	int	ch_wc_is_enabled(void);
	int	ch_write_ecache(mdata_t *);
	int	ch_write_ephys(mdata_t *);
	int	ch_write_etphys(mdata_t *);
	int	ch_write_dphys(mdata_t *);
	int	ch_write_iphys(mdata_t *);
	int	ch_write_mtag(mdata_t *, uint64_t, caddr_t);

/*
 * These US3 generic and Cheetah errors are grouped according to the
 * definitions in the header file.
 */
cmd_t us3_generic_cmds[] = {
	CH_KD_CEPR,		memtest_k_mem_err,	"memtest_k_mem_err",
	CH_KD_UEPR,		memtest_k_mem_err,	"memtest_k_mem_err",

	CH_KD_UCU,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_KU_UCUCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	CH_KD_UCUTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_KI_UCU,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_KI_UCUTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_KI_OUCU,		memtest_k_mem_err,	"memtest_k_mem_err",
	CH_UD_UCU,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_UI_UCU,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_OBPD_UCU,		memtest_obp_err,	"memtest_obp_err",
	CH_KD_UCC,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_KU_UCCCOPYIN,	memtest_copyin_l2_err,	"memtest_copyin_l2_err",
	CH_KD_UCCTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_KI_UCC,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_KI_UCCTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_UD_UCC,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_UI_UCC,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_OBPD_UCC,		memtest_obp_err,	"memtest_obp_err",

	CH_KD_EDUL,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_KD_EDUS,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_KD_EDUPR,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_UD_EDUL,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_UD_EDUS,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_KD_EDCL,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_KD_EDCS,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_KD_EDCPR,		memtest_k_l2_err,	"memtest_k_l2_err",
	CH_UD_EDCL,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_UD_EDCS,		memtest_u_l2_err,	"memtest_u_l2_err",

	CH_KD_WDU,		memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	CH_KI_WDU,		memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	CH_UD_WDU,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_UI_WDU,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_KD_WDC,		memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	CH_KI_WDC,		memtest_k_l2wb_err,	"memtest_k_l2wb_err",
	CH_UD_WDC,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_UI_WDC,		memtest_u_l2_err,	"memtest_u_l2_err",

	CH_KD_CPU,		memtest_k_cp_err,	"memtest_k_cp_err",
	CH_KI_CPU,		memtest_k_cp_err,	"memtest_k_cp_err",
	CH_UD_CPU,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_UI_CPU,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_KD_CPC,		memtest_k_cp_err,	"memtest_k_cp_err",
	CH_KI_CPC,		memtest_k_cp_err,	"memtest_k_cp_err",
	CH_UD_CPC,		memtest_u_l2_err,	"memtest_u_l2_err",
	CH_UI_CPC,		memtest_u_l2_err,	"memtest_u_l2_err",

	NULL,			NULL,			NULL,
};

cmd_t cheetah_cmds[] = {
	CH_KD_EMU,		memtest_k_mtag_err,	"memtest_k_mtag_err",
	CH_KD_EMC,		memtest_k_mtag_err,	"memtest_k_mtag_err",

	CH_KD_TO,		memtest_k_bus_err,	"memtest_k_bus_err",
	CH_KD_TOPEEK,		memtest_k_bus_err,	"memtest_k_bus_err",

	NULL,			NULL,			NULL,
};

static cmd_t *commands[] = {
	cheetah_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

/*
 * Cheetah operations vector tables.
 */
static opsvec_u_t cheetah_uops = {
	/* sun4u injection ops vectors */
	notsup,			/* no corrupt d$ tag at offset */
	notsup,			/* no corrupt i$ tag at offset */
	ch_write_etphys,	/* corrupt e$ tag at offset */
	ch_write_memory,	/* corrupt memory at paddr */
	ch_write_mtag,		/* corrupt mtag */
	notsup,			/* p-cache error */

	/* sun4u support ops vectors */
	ch_gen_ecc_pa,		/* generate ecc for data at paddr */
};

static opsvec_c_t cheetah_cops = {
	/* common injection ops vectors */
	notsup,			/* no corrupt d$ at vaddr */
	ch_write_dphys,		/* corrupt d$ at offset */
	notsup,			/* no corrupt fp reg */
	notsup,			/* no corrupt i$ at vaddr */
	notsup,			/* no corrupt internal */
	ch_write_iphys,		/* corrupt i$ at offset */
	notsup,			/* no corrupt int reg */
	ch_write_ecache,	/* corrupt e$ at paddr */
	ch_write_ephys,		/* corrupt e$ at offset */
	notsup,			/* no corrupt l3$ at paddr */
	notsup,			/* no corrupt l3$ at offset */
	notsup,			/* no I-D TLB parity errors */

	/* common support ops vectors */
	notsup,			/* no fp reg access */
	notsup,			/* no int reg access */
	memtest_check_afsr,	/* check ESRs */
	ch_enable_errors,	/* enable AFT errors */
	notimp,			/* no enable/disable L2 or memory scrubbers */
	ch_get_cpu_info,	/* put cpu info into struct */
	ch_flushall_caches,	/* flush all caches */
	ch_flushall_dcache,	/* flush all d$ */
	ch_flushall_icache,	/* flush all i$ */
	gen_flushall_l2,	/* flush all l2$ */
	notsup,			/* no flush all l3$ */
	ch_flush_dc_entry,	/* flush d$ entry */
	gen_flush_ic_entry,	/* flush i$ entry */
	gen_flush_l2_entry,	/* flush l2$ entry */
	notsup,			/* no flush l3$ entry */
};

/*
 * This routine injects an error into memory at the specified paddr.
 */
int
ch_write_memory(mdata_t *mdatap, uint64_t paddr, caddr_t kvaddr, uint_t ecc)
{
	DPRINTF(3, "ch_write_memory: mdatap=0x%p, paddr=0x%llx, "
		"kvaddr=0x%p, ecc=0x%x\n",
		mdatap, paddr, kvaddr, ecc);

	return (ch_wr_memory(paddr, mdatap->m_cip->c_l2_size, ecc));
}

/*
 * This routine disables the wcache.
 */
void
ch_disable_wc(mdata_t *mdatap)
{
	uint64_t	dcucr;

	/*
	 * The W$ must be flushed before disabling it.
	 * Interrupts are disabled to avoid unwanted code from being
	 * executed between flushing the e$ and disabling the w$.
	 */
	dcucr = memtest_get_dcucr() & ~DCU_WE;
	memtest_disable_intrs();
	(void) gen_flushall_l2(mdatap->m_cip);
	(void) memtest_set_dcucr(dcucr);
	memtest_enable_intrs();
}

/*
 * This routine does formatted output of cheetah staging registers data.
 */
void
ch_dump_ec_data(char *msg, ch_ec_t *regs)
{
	static	char *ch_ec_states[] =
		{ "i", "s", "e", "o", "m", "r", "os", "r" };

	if (msg != (char *)NULL)
		DPRINTF(0, "%s", msg);
	DPRINTF(0, "\tdata:\t0x%16lx 0x%16lx\n\t\t0x%16lx 0x%16lx\n",
		regs->ec_data[0], regs->ec_data[1],
		regs->ec_data[2], regs->ec_data[3]);
	DPRINTF(0, "\tecc:\t0x%16lx\n", regs->ec_ecc);
	DPRINTF(0, "\ttag:\t0x%16lx (p=0x%llx, s=%s,%s,%s,%s,%s,%s,%s,%s)\n",
		regs->ec_tag, (regs->ec_tag & ~0xffffff) >> 1,
		ch_ec_states[(regs->ec_tag >> 21) & 7],
		ch_ec_states[(regs->ec_tag >> 18) & 7],
		ch_ec_states[(regs->ec_tag >> 15) & 7],
		ch_ec_states[(regs->ec_tag >> 12) & 7],
		ch_ec_states[(regs->ec_tag >>  9) & 7],
		ch_ec_states[(regs->ec_tag >>  6) & 7],
		ch_ec_states[(regs->ec_tag >>  3) & 7],
		ch_ec_states[(regs->ec_tag >>  0) & 7]);
}

/*
 * This routine enables errors traps and E$ ECC checking.
 */
int
ch_enable_errors(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	exp_ecr_set, exp_ecr_clr, exp_ecr, obs_ecr;
	uint64_t	exp_eer_set, exp_eer_clr, exp_eer, obs_eer;
	uint64_t	exp_dcr_set, exp_dcr_clr, exp_dcr, obs_dcr;
	uint64_t	exp_dcucr_set, exp_dcucr_clr, exp_dcucr, obs_dcucr;

	/*
	 * Common settings.
	 */
	exp_eer_set = EN_REG_UCEEN | EN_REG_NCEEN | EN_REG_CEEN;
	exp_eer_clr = 0;
	exp_ecr_set = ECCR_EC_ECC_EN;
	exp_ecr_clr = 0;
	exp_dcr_set = 0;
	exp_dcr_clr = 0;
	exp_dcucr_set = DCU_IC | DCU_DC;
	exp_dcucr_clr = 0;

	/*
	 * These are the bits that are required to be set
	 * in the EER and DCR. Others may also be set.
	 */
	switch (CPU_IMPL(cip->c_cpuver)) {
	case CHEETAH_IMPL:
		break;
	case CHEETAH_PLUS_IMPL:
	case JAGUAR_IMPL:
	case PANTHER_IMPL:
		exp_ecr_set |= ECCR_ET_ECC_EN;
		exp_dcr_set = DCR_IPE | DCR_DPE;
		break;
	case JALAPENO_IMPL:
		exp_eer_set |= EN_REG_IAEN | EN_REG_SCDE;
		exp_ecr_set |= ECCR_EC_PAR_EN;
		exp_dcr_set = DCR_IPE | DCR_DPE;
		break;
	case SERRANO_IMPL:
		exp_eer_set |= EN_REG_IAEN | EN_REG_SCDE;
		exp_ecr_set |= SR_ECCR_ET_ECC_EN;
		exp_ecr_clr |= SR_ECCR_EC_OFF;
		exp_dcr_set = DCR_IPE | DCR_DPE;
		break;
	default:
		DPRINTF(0, "ch_enable_errors: unsupported CPU type, "
			"impl=0x%llx\n", cip->c_cpuver);
		return (ENOTSUP);
	}

	DPRINTF(2, "ch_enable_errors: exp_eer_set=0x%llx, exp_ecr_set=0x%llx, "
		"exp_dcr_set=0x%llx, exp_dcucr_set=0x%llx\n",
		exp_eer_set, exp_ecr_set, exp_dcr_set, exp_dcucr_set);

	DPRINTF(2, "ch_enable_errors: exp_eer_clr=0x%llx, exp_ecr_clr=0x%llx, "
		"exp_dcr_clr=0x%llx, exp_dcucr_clr=0x%llx\n",
		exp_eer_clr, exp_ecr_clr, exp_dcr_clr, exp_dcucr_clr);

	/*
	 * Get the current values of the registers.
	 */
	obs_eer = memtest_get_eer();
	obs_ecr = ch_get_ecr();
	obs_dcr = memtest_get_dcr();
	obs_dcucr = memtest_get_dcucr();

	DPRINTF(1, "ch_enable_errors: obs_eer=0x%llx, obs_ecr=0x%llx, "
		"obs_dcr=0x%llx, obs_dcucr=0x%llx\n",
		obs_eer, obs_ecr, obs_dcr, obs_dcucr);

	/*
	 * The expected register values are either specified via command line
	 * options or are a combination of the existing values plus the bits
	 * required to be set minus the bits required to be clear.
	 */
	if (F_SET_EER(iocp))
		exp_eer = iocp->ioc_eer;
	else
		exp_eer = (obs_eer | exp_eer_set) & ~exp_eer_clr;
	if (F_SET_ECCR(iocp))
		exp_ecr = iocp->ioc_ecr;
	else
		exp_ecr = (obs_ecr | exp_ecr_set) & ~exp_ecr_clr;
	if (F_SET_DCR(iocp))
		exp_dcr = iocp->ioc_dcr;
	else
		exp_dcr = (obs_dcr | exp_dcr_set) & ~exp_dcr_clr;
	if (F_SET_DCUCR(iocp))
		exp_dcucr = iocp->ioc_dcucr;
	else
		exp_dcucr = (obs_dcucr | exp_dcucr_set) & ~exp_dcucr_clr;

	DPRINTF(1, "ch_enable_errors: exp_eer=0x%llx, exp_ecr=0x%llx, "
		"exp_dcr=0x%llx, exp_dcucr=0x%llx\n",
		exp_eer, exp_ecr, exp_dcr, exp_dcucr);

	/*
	 * Verify E$ Error Enable register setting.
	 */
	if (obs_eer != exp_eer) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting EER to new value "
				"(obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
				PRTF_64_TO_32(obs_eer),
				PRTF_64_TO_32(exp_eer));
		}
		(void) memtest_set_eer(exp_eer);
		obs_eer = memtest_get_eer();
		if (obs_eer != exp_eer) {
			cmn_err(CE_WARN, "couldn't set EER to desired value "
				"(obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
				PRTF_64_TO_32(obs_eer),
				PRTF_64_TO_32(exp_eer));
			return (-1);
		}
	}

	/*
	 * Verify E$ Control register setting.
	 */
	if (obs_ecr != exp_ecr) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting ECCR to new value "
				"(obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
				PRTF_64_TO_32(obs_ecr),
				PRTF_64_TO_32(exp_ecr));
		}
		(void) memtest_set_ecr(exp_ecr);
		obs_ecr = ch_get_ecr();
		if (obs_ecr != exp_ecr) {
			cmn_err(CE_WARN, "couldn't set ECCR to desired value "
				"(obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
				PRTF_64_TO_32(obs_ecr),
				PRTF_64_TO_32(exp_ecr));
			return (-1);
		}
	}

	/*
	 * Verify Dispatch Control register setting.
	 */
	if (obs_dcr != exp_dcr) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting DCR to new value "
				"(obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
				PRTF_64_TO_32(obs_dcr),
				PRTF_64_TO_32(exp_dcr));
		}
		(void) memtest_set_dcr(exp_dcr);
		obs_dcr = memtest_get_dcr();
		if (obs_dcr != exp_dcr) {
			cmn_err(CE_WARN, "couldn't set DCR to desired value "
				"(obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
				PRTF_64_TO_32(obs_dcr),
				PRTF_64_TO_32(exp_dcr));
			return (-1);
		}
	}

	/*
	 * Verify D$ Unit Control register setting.
	 */
	if (obs_dcucr != exp_dcucr) {
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "setting DCUCR to new value "
				"(obs=0x%08x.%08x, exp=0x%08x.%08x)\n",
				PRTF_64_TO_32(obs_dcucr),
				PRTF_64_TO_32(exp_dcucr));
		}
		(void) memtest_set_dcucr(exp_dcucr);
		obs_dcucr = memtest_get_dcucr();
		if (obs_dcucr != exp_dcucr) {
			cmn_err(CE_WARN, "couldn't set DCUCR to desired value "
				"(obs=0x%08x.%08x, exp=0x%08x.%08x)!\n",
				PRTF_64_TO_32(obs_dcucr),
				PRTF_64_TO_32(exp_dcucr));
			return (-1);
		}
	}

	return (0);
}

/*
 * This routine enables the wcache.
 */
void
ch_enable_wc(void)
{
	(void) memtest_set_dcucr(memtest_get_dcucr() | DCU_WE);
}

/*
 * Flush the entire I, D, and E caches.
 * On cheetah, flushing the e$ does not invalidate the i$ and d$
 * so we must do them separately. Flushing the e$ however does
 * invalidate the w$.
 */
int
ch_flushall_caches(cpu_info_t *cip)
{
	DPRINTF(4, "ch_flushall_caches: calling cpu_flush_ecache()\n");
	cpu_flush_ecache();

	DPRINTF(4, "ch_flushall_caches: calling ch_flushall_dc()\n");
	(void) ch_flushall_dc(cip->c_dc_size, cip->c_dc_linesize);

	DPRINTF(4, "ch_flushall_caches: calling ch_flushall_ic()\n");
	(void) ch_flushall_ic(cip->c_ic_size, cip->c_ic_linesize);

	return (0);
}

/*
 * Flush the entire data cache.
 */
int
ch_flushall_dcache(cpu_info_t *cip)
{
	DPRINTF(4, "ch_flushall_dcache: calling ch_flushall_dc()\n");
	(void) ch_flushall_dc(cip->c_dc_size, cip->c_dc_linesize);
	return (0);
}

/*
 * Flush the entire instruction cache.
 */
int
ch_flushall_icache(cpu_info_t *cip)
{
	DPRINTF(4, "ch_flushall_icache: calling ch_flushall_ic()\n");
	(void) ch_flushall_ic(cip->c_ic_size, cip->c_ic_linesize);
	return (0);
}

/*
 * Flush a single data cache entry (line).
 */
/*ARGSUSED*/
int
ch_flush_dc_entry(cpu_info_t *cip, caddr_t vaddr)
{
	/*
	 * Note that the vaddr to use is aligned by the called asm routine.
	 */
	DPRINTF(4, "ch_flush_dc_entry: calling ch_flush_dc()\n");
	(void) ch_flush_dc(vaddr);
	return (0);
}

/*
 * Cheetah ECC calculation.
 *
 * We only need to do the calculation on the data bits and can ignore check
 * bit and Mtag bit terms in the calculation.
 */
static uint64_t ch_ecc_table[9][2] = {
	/*
	 * low order 64-bits   high-order 64-bits
	 */
	{ 0x46bffffeccd1177f, 0x488800022100014c },
	{ 0x42fccc81331ff77f, 0x14424f1010249184 },
	{ 0x8898827c222f1ffe, 0x22c1222808184aaf },
	{ 0xf7632203e131ccf1, 0xe1241121848292b8 },
	{ 0x7f5511421b113809, 0x901c88d84288aafe },
	{ 0x1d49412184882487, 0x8f338c87c044c6ef },
	{ 0xf552181014448344, 0x7ff8f4443e411911 },
	{ 0x2189240808f24228, 0xfeeff8cc81333f42 },
	{ 0x3280008440001112, 0xfee88b337ffffd62 },
};

/*
 * Generate the 9 ECC bits for the 128-bit aligned address based on the table
 * above.  Note that xor'ing an odd number of 1 bits == 1 and xor'ing an even
 * number of 1 bits == 0, so we can just use the least significant bit of the
 * popcnt instead of doing all the xor's.
 */
int
ch_gen_ecc_pa(uint64_t paddr)
{
	uint64_t	data_msw, data_lsw;
	uint64_t	paddr_aligned;

	paddr_aligned = P2ALIGN(paddr, 16);
	data_msw = lddphys(paddr_aligned);
	data_lsw = lddphys(paddr_aligned + 8);

	return (ch_gen_ecc_data(data_msw, data_lsw));
}

/*
 * Generate the 9 ECC bits for the 128-bit chunk of data passed in
 * using the table above.
 */
static int
ch_gen_ecc_data(uint64_t data_msw, uint64_t data_lsw)
{
	int		bitno, s, synd = 0;

	for (bitno = 0; bitno < 9; bitno++) {
		s = (memtest_popc64(data_lsw & ch_ecc_table[bitno][0]) +
		    memtest_popc64(data_msw & ch_ecc_table[bitno][1])) & 1;
		synd |= (s << bitno);
	}
	return (synd);

}

/*
 * Initialize the processor specific pieces of the system info
 * structure which is passed in.
 *
 * The initialization is done locally instead of using global
 * kernel variables to maintain as much compatibility with
 * the standalone environment as possible.
 */
int
ch_get_cpu_info(cpu_info_t *cip)
{
	cip->c_dc_size = 64 * 1024;
	cip->c_dc_linesize = 32;
	cip->c_dc_assoc = 4;

	cip->c_ic_size = 32 * 1024;
	cip->c_ic_linesize = 32;
	cip->c_ic_assoc = 4;

	cip->c_ecr = ch_get_ecr();

	switch (ECCR_SIZE(cip->c_ecr)) {
	case 0:
		cip->c_l2_size = EC_SIZE_1MB;
		break;
	case 1:
		cip->c_l2_size = EC_SIZE_4MB;
		break;
	case 2:
		cip->c_l2_size = EC_SIZE_8MB;
		break;
	default:
		DPRINTF(0, "ch_get_cpu_info: unsupported E$ size in "
			"ecr=0x%llx", cip->c_ecr);
		return (ENOTSUP);
	}

	switch (CPU_IMPL(cip->c_cpuver)) {
	case CHEETAH_IMPL:
		cip->c_l2_assoc = 1;
		break;
	case CHEETAH_PLUS_IMPL:
		cip->c_l2_assoc = ECCR_ASSOC(cip->c_ecr) + 1;
		break;
	default:
		DPRINTF(0, "ch_get_cpu_info: unsupported CPU type, "
			"impl=0x%llx\n", cip->c_cpuver);
		return (ENOTSUP);
	}

	cip->c_l2_sublinesize = 64;
	cip->c_l2_linesize = cip->c_l2_sublinesize *
		(cip->c_l2_size / (1024 * 1024));
	cip->c_l2_flushsize = cip->c_l2_size * cip->c_l2_assoc * 2;

	cip->c_mem_flags = 0;

	return (0);
}

/*
 * Initialize the processor specific pieces of the driver data
 * structure passed in.
 */
void
ch_init(mdata_t *mdatap)
{
	mdatap->m_sopvp = &cheetah_uops;
	mdatap->m_copvp = &cheetah_cops;
	mdatap->m_cmdpp = commands;
}

/*
 * This routine injects an mtag error at the specified physical address.
 */
int
ch_write_mtag(mdata_t *mdatap, uint64_t paddr, caddr_t kvaddr)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip  = mdatap->m_cip;
	uint64_t	paddr_aligned, xorpat;
	uint_t		ecc;

	paddr_aligned = P2ALIGN(paddr, 8);

	/*
	 * Generate correct ecc for the mtag.
	 * The mtag state is assumed to be gM (0) since this is the only node
	 * writing to the data. The corresponding mtag ecc value is also 0.
	 */
	ecc = 0;

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	DPRINTF(2, "ch_write_mtag: paddr_aligned=0x%llx, kvaddr=0x%p, "
		"data=0x%llx, ecc=0x%x, xorpat=0x%llx\n",
		paddr_aligned, kvaddr, lddphys(paddr_aligned), ecc, xorpat);

	/*
	 * Corrupt the check bits since it is not possible to corrupt
	 * mtags via diagnostic asi.
	 */
	ecc ^= xorpat;

	/*
	 * Write the data to make sure its mtag is in the gM state.
	 */
	stdphys(paddr_aligned, lddphys(paddr_aligned));

	DPRINTF(2, "ch_write_mtag: calling\n"
			"\txx_wr_mtag(paddr_aligned=0x%llx, ec_size=0x%x, "
			"ecc=0x%x)\n", paddr_aligned, cip->c_l2_size, ecc);

	/*
	 * Flushing caches keeps latency low when they are flushed
	 * again in lower level assembly routines.
	 */
	(void) ch_flushall_caches(cip);

	/*
	 * Write out corrupted data with ECC generated from above.
	 */
	ch_wr_mtag(paddr_aligned, cip->c_l2_size, ecc);

	return (0);

}

/*
 * This routine returns a flag indicating whether the
 * wcache is currently enabled.
 */
int
ch_wc_is_enabled(void)
{
	return ((memtest_get_dcucr() & DCU_WE) == DCU_WE);
}

/*
 * These are used for debug purposes.
 * They hold the staging register data before/after corrupting the e$.
 */
ch_ec_t	sregs_before, sregs_after;

/*
 * This routine inserts an error into the ecache at the specified kernel
 * physical address.  This is done by reading e$ data via the staging
 * registers, modifying (xoring) one of the data or ecc regs and writing
 * the data back to the e$.
 */
int
ch_write_ecache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	paddr_aligned, xorpat;
	uint_t		e_cache_ecc_lo = 0;
	uint_t		e_cache_ecc_hi = 0;
	int		reg_offset;		/* staging reg offset */
	int		wc_was_disabled = 0;

	paddr_aligned = P2ALIGN(paddr, 32);

	DPRINTF(3, "ch_write_ecache: mdatap=0x%p, paddr=0x%llx, "
		"paddr_aligned=0x%llx\n",
		mdatap, paddr, paddr_aligned);

	/*
	 * Touch the ecache data to get it into the modified state except
	 * for a UC? or CP? type of instruction error (e.g. kiucu, kiucc,
	 * uiucu, uiucc, kicpu, etc.) This is done in order to verify that
	 * the kernel can recover from those errors.
	 */
	if ((ERR_ACC_ISFETCH(iocp->ioc_command) &&
	    ERR_TRAP_ISPRE(iocp->ioc_command)) ||
	    (ERR_ACC_ISPFETCH(iocp->ioc_command))) {
		DPRINTF(3, "\nch_write_ecache: not modifying the cache line\n");
	} else {
		/*
		 * To ensure that this store happens before we inject
		 * the error into the e$, we disable the w$.
		 */
		if (ch_wc_is_enabled()) {
			if (memtest_flags & MFLAGS_CH_USE_FLUSH_WC) {
				ch_flush_wc(paddr);
			} else {
				ch_disable_wc(mdatap);
				wc_was_disabled = 1;
			}
		}
		stphys(paddr_aligned, ldphys(paddr_aligned));
	}

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * Select the register offset for forcing either a checkbit
	 * error or data error.
	 */
	if (F_CHKBIT(iocp))
		reg_offset = 4;
	else
		reg_offset = ((paddr) % 32) / sizeof (uint64_t);

	if (memtest_debug >= 4) {
		bzero(&sregs_before, sizeof (ch_ec_t));
		bzero(&sregs_after, sizeof (ch_ec_t));
		ch_rd_ecache(paddr_aligned, &sregs_before);
	}

	/*
	 * Read the e$ data, corrupt the appropriate data or check bits
	 * and write the data back to e$ using ASI staging regs.
	 */
	ch_wr_ecache(paddr_aligned, reg_offset, xorpat);

	if (memtest_debug >= 4)
		ch_rd_ecache(paddr_aligned, &sregs_after);

	/*
	 * Re-enable the w$ if it was disabled above.
	 */
	if (wc_was_disabled)
		ch_enable_wc();

	/*
	 * Debug code to dump e$ data before/after corrupting it.
	 */
	if (memtest_debug >= 4) {
		e_cache_ecc_hi = ch_gen_ecc_data(sregs_before.ec_data[0],
						sregs_before.ec_data[1]);
		e_cache_ecc_lo = ch_gen_ecc_data(sregs_before.ec_data[2],
						sregs_before.ec_data[3]);
		DPRINTF(0, "ch_write_ecache: called ch_wr_ecache( "
			"pa_aligned=0x%llx, off=0%llx, xor=0x%llx)\n",
			paddr_aligned, reg_offset, xorpat);
		DPRINTF(0, "ch_write_ecache: calculated ecc=0x%llx "
			"(0x%llx 0x%llx)\n",
			(e_cache_ecc_hi << 9) | e_cache_ecc_lo,
			e_cache_ecc_hi, e_cache_ecc_lo);
		ch_dump_ec_data("ch_write_ecache: data before:\n",
				&sregs_before);
		ch_dump_ec_data("ch_write_ecache: data after:\n",
				&sregs_after);
	}

	return (0);
}

/*
 * This routine injects an error into the ecache at the specified
 * physical offset.
 */
int
ch_write_ephys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	offset_aligned;
	uint64_t	xorpat;
	uint_t		e_cache_ecc_lo = 0;
	uint_t		e_cache_ecc_hi = 0;
	int		reg_offset;		/* staging reg offset */

	offset_aligned = P2ALIGN(offset, 32);

	DPRINTF(3, "\nch_write_ephys: offset=0x%llx, offset_aligned=0x%llx\n",
		offset, offset_aligned);

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * Select the register offset for forcing either a checkbit
	 * error or data error.
	 */
	if (F_CHKBIT(iocp))
		reg_offset = 4;
	else
		reg_offset = ((offset) % 32) / sizeof (uint64_t);

	DPRINTF(3, "ch_write_ephys: calling ch_wr_ephys(pa=0x%llx, "
		"off=0%llx, xor=0x%llx)\n", offset_aligned,
		reg_offset, xorpat);

	if (memtest_debug >= 4) {
		bzero(&sregs_before, sizeof (ch_ec_t));
		bzero(&sregs_after, sizeof (ch_ec_t));
		ch_rd_ecache(offset_aligned, &sregs_before);
	}

	/*
	 * Place an error into the e$ without modifying its state.
	 */
	ch_wr_ephys(offset_aligned, reg_offset, xorpat);

	if (memtest_debug >= 4) {
		ch_rd_ecache(offset_aligned, &sregs_after);
		ch_dump_ec_data("ch_write_ephys: data before:\n",
				&sregs_before);
		e_cache_ecc_hi = ch_gen_ecc_data(sregs_before.ec_data[0],
						sregs_before.ec_data[1]);
		e_cache_ecc_lo = ch_gen_ecc_data(sregs_before.ec_data[2],
						sregs_before.ec_data[3]);
		DPRINTF(0, "ch_write_ephys: calculated ecc=0x%llx\n",
			e_cache_ecc_lo | (e_cache_ecc_hi << 9));
		ch_dump_ec_data("ch_write_ecache: data after:\n",
				&sregs_after);
	}

	return (0);
}

/*
 * This routine injects an error into the ecache tags at the specified
 * physical offset. Since cheetah does not have protection on its e$
 * tags, the error is simulated by simply corrupting the tag.
 */
int
ch_write_etphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	offset_aligned;
	uint64_t	xorpat, data;
	uint_t		e_cache_ecc_lo = 0;
	uint_t		e_cache_ecc_hi = 0;
	ch_ec_t		sregs_before, sregs_after;

	offset_aligned = P2ALIGN(offset, 32);

	DPRINTF(3, "\nch_write_etphys: offset=0x%llx, offset_aligned=0x%llx\n",
		offset, offset_aligned);

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * Generate data to corrupt.
	 * If the xor pattern is explicitly specified to be 0, then
	 * rather than corrupting the existing e$ tag, it will be
	 * overwritten with this data.
	 */
	if (F_MISC1(iocp))
		data = iocp->ioc_misc1;
	else
		data = 0xbadec123badec123;

	DPRINTF(3, "ch_write_etphys: calling ch_wr_etphys(pa=0x%llx, "
		"xor=0x%llx, data=0x%llx)\n",
		offset_aligned, xorpat, data);

	if (memtest_debug >= 4) {
		bzero(&sregs_before, sizeof (ch_ec_t));
		bzero(&sregs_after, sizeof (ch_ec_t));
		ch_rd_ecache(offset_aligned, &sregs_before);
	}

	/*
	 * Place an error into the e$ tag possibly modifying state.
	 */
	ch_wr_etphys(offset_aligned, xorpat, data);

	if (memtest_debug >= 4) {
		ch_rd_ecache(offset_aligned, &sregs_after);
		ch_dump_ec_data("ch_write_etphys: data before:\n",
				&sregs_before);
		e_cache_ecc_hi = ch_gen_ecc_data(sregs_before.ec_data[0],
						sregs_before.ec_data[1]);
		e_cache_ecc_lo = ch_gen_ecc_data(sregs_before.ec_data[2],
						sregs_before.ec_data[3]);
		DPRINTF(0, "ch_write_etphys: calculated ecc=0x%llx\n",
			e_cache_ecc_lo | (e_cache_ecc_hi << 9));
		ch_dump_ec_data("ch_write_ecache: data after:\n",
			&sregs_after);
	}

	return (0);
}

/*
 * This routine injects an error into the dcache data at the specified
 * physical offset. Since cheetah does not have protection on its d$
 * data, the error is simulated by simply corrupting the data.
 */
int
ch_write_dphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	offset_aligned;
	uint64_t	xorpat, data;
	uint64_t	dc_data_before, dc_data_after;

	offset_aligned = P2ALIGN(offset, 8);

	DPRINTF(3, "\nch_write_dphys: offset=0x%llx, offset_aligned=0x%llx\n",
		offset, offset_aligned);

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * Generate data to corrupt.
	 * If the xor pattern is explicitly specified to be 0, then
	 * rather than corrupting the existing d$ data, it will be
	 * overwritten with this data.
	 */
	if (F_MISC1(iocp))
		data = iocp->ioc_misc1;
	else
		data = 0xbaddc123baddc123;

	DPRINTF(3, "ch_write_dphys: calling ch_wr_dphys(0x%llx, 0x%llx, "
		"0x%llx)\n", offset_aligned, xorpat, data);

	if (memtest_debug >= 4)
		dc_data_before = ch_rd_dcache(offset_aligned);

	/*
	 * Place an error into the d$ without modifying state.
	 */
	ch_wr_dphys(offset_aligned, xorpat, data);

	if (memtest_debug >= 4) {
		dc_data_after = ch_rd_dcache(offset_aligned);
		DPRINTF(0, "ch_write_dphys: dcache data before/after = "
			"0x%llx, 0x%llx\n", dc_data_before, dc_data_after);
	}

	return (0);
}

/*
 * This routine injects an error into the icache data at the specified
 * physical offset. Since cheetah does not have protection on its i$
 * data, the error is simulated by simply corrupting the data.
 */
int
ch_write_iphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	offset_aligned;
	uint64_t	xorpat, instr;
	uint64_t	ic_data_before, ic_data_after;

	offset_aligned = P2ALIGN(offset, 8);

	DPRINTF(3, "\nch_write_iphys: offset=0x%llx, offset_aligned=0x%llx\n",
		offset, offset_aligned);

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * Generate data to corrupt.
	 * If the xor pattern is explicitly specified to be 0, then
	 * rather than corrupting the existing i$ data, it will be
	 * overwritten with this data.
	 */
	if (F_MISC1(iocp))
		instr = iocp->ioc_misc1;
	else
		/*
		 * 0x91D0207c = "ta 0x7c" instruction
		 * 0x6 = predecode bits for this instruction
		 */
		instr = 0x691D0207c;

	/*
	 * Note that SME recommended that we do not modify i$ entries
	 * by overwriting them since the predecode bits can not be set
	 * to a "safe" pattern. If someone tries to do this warn them.
	 */
	if (xorpat == 0) {
		cmn_err(CE_WARN, "ch_write_iphys: overwriting i$ "
			"instructions/predecode is not recommended\n");
	}

	DPRINTF(3, "ch_write_iphys: calling ch_wr_iphys(0x%llx, 0x%llx, "
		"0x%llx)\n", offset_aligned, xorpat, instr);

	if (memtest_debug >= 4)
		ic_data_before = ch_rd_icache(offset_aligned);

	ch_wr_iphys(offset_aligned, xorpat, instr);

	if (memtest_debug >= 4) {
		ic_data_after = ch_rd_icache(offset_aligned);
		DPRINTF(0, "ch_write_iphys: icache data before/after = "
			"0x%llx, 0x%llx\n", ic_data_before, ic_data_after);
	}

	return (0);
}

/*
 * This routine generates a kernel mtag error.
 */
int
memtest_k_mtag_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		myid;
	int		ret;

	if (F_VERBOSE(iocp)) {
		cmn_err(CE_NOTE, "memtest_k_mtag_err: injecting error at "
			"physical address 0x%08x.%08x\n",
			PRTF_64_TO_32(mdatap->m_paddr_c));
	}

	myid = getprocessorid();

	/*
	 * Inject the error.
	 */
	if (ret = OP_INJECT_MTAG(mdatap, mdatap->m_paddr_c,
	    mdatap->m_kvaddr_c)) {
		DPRINTF(0, "memtest_k_mtag_err: write_mtag failed\n");
		return (ret);
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "memtest_k_mtag_err: not invoking error\n");
		return (0);
	}

	/*
	 * Invoke the error.
	 */
	if (ERR_MISC_ISTL1(iocp->ioc_command))
		xt_one(myid, (xcfunc_t *)mdatap->m_asmld_tl1,
			(uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
	else
		(mdatap->m_asmld)(mdatap->m_kvaddr_a);


	if (!ERR_PROT_ISCE(iocp->ioc_command)) {
		cmn_err(CE_WARN, "memtest_k_mtag_err: should have gotten "
			"a system reset!\n");
		return (EIO);
	}

	DPRINTF(2, "memtest_k_mtag_err: corrected_data=0x%llx\n",
			*(uint64_t *)mdatap->m_kvaddr_c);

	return (0);
}

/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains Serrano (US3i+) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_chp.h>
#include <sys/memtestio_ja.h>
#include <sys/memtestio_sr.h>
#include <sys/memtest_u.h>
#include <sys/memtest_u_asm.h>
#include <sys/memtest_ch.h>
#include <sys/memtest_chp.h>
#include <sys/memtest_ja.h>
#include <sys/memtest_sr.h>

/*
 * Routines located in this file.
 */
	void	sr_init(mdata_t *);
static	int	sr_get_cpu_info(cpu_info_t *);

/*
 * Serrano operations vector tables.
 */
static opsvec_u_t serrano_uops = {
	/* sun4u injection ops vectors */
	chp_write_dtphys,	/* corrupt d$ tag at offset */
	chp_write_itphys,	/* corrupt i$ tag at offset */
	ja_write_ephys,		/* corrupt e$ tag at offset */
	ja_write_memory,	/* corrupt memory */
	notsup,			/* corrupt mtag - not supported */
	notsup,			/* p-cache error */

	/* sun4u support ops vectors */
	ch_gen_ecc_pa,		/* generate ecc for data at paddr */
};

static opsvec_c_t serrano_cops = {
	/* common injection ops vectors */
	chp_write_dcache,	/* corrupt d$ at paddr */
	ch_write_dphys,		/* corrupt d$ at offset */
	notsup,			/* no corrupt fp reg */
	chp_write_icache,	/* corrupt i$ at paddr */
	notsup,			/* no corrupt internal */
	ch_write_iphys,		/* corrupt i$ at offset */
	notsup,			/* no corrupt int reg */
	ja_write_ecache,	/* corrupt e$ at paddr */
	ja_write_ephys,		/* corrupt e$ at offset */
	notsup,			/* no corrupt l3$ at paddr */
	notsup,			/* no corrupt l3$ at offset */
	notsup,			/* TLB error */

	/* common support ops vectors */
	notsup,			/* no fp reg access */
	notsup,			/* no int reg access */
	memtest_check_afsr,	/* check ESRs */
	ch_enable_errors,	/* enable AFT errors */
	notimp,			/* no enable/disable L2 or memory scrubbers */
	sr_get_cpu_info,	/* put cpu info into system_info_t struct */
	ch_flushall_caches,	/* flush all caches */
	ch_flushall_dcache,	/* flush all d$ */
	ch_flushall_icache,	/* flush all i$ */
	gen_flushall_l2,	/* flush all l2$ */
	notsup,			/* no flush all l3$ */
	ch_flush_dc_entry,	/* flush d$ entry */
	gen_flush_ic_entry,	/* flush i$ entry */
	gen_flush_l2_entry,	/* flush l2$ entry - not used */
	notsup,			/* no flush l3$ entry */
};

/*
 * These Serrano errors are grouped according to the definitions
 * in the header file.
 */
cmd_t serrano_cmds[] = {
	SR_KD_ETU,		memtest_k_l2_err,	"memtest_k_l2_err",
	SR_KI_ETU,		memtest_k_l2_err,	"memtest_k_l2_err",
	SR_UD_ETU,		memtest_u_l2_err,	"memtest_u_l2_err",
	SR_UI_ETU,		memtest_u_l2_err,	"memtest_u_l2_err",
	SR_KD_ETC,		memtest_k_l2_err,	"memtest_k_l2_err",
	SR_KD_ETCTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	SR_KI_ETC,		memtest_k_l2_err,	"memtest_k_l2_err",
	SR_KI_ETCTL1,		memtest_k_l2_err,	"memtest_k_l2_err",
	SR_UD_ETC,		memtest_u_l2_err,	"memtest_u_l2_err",
	SR_UI_ETC,		memtest_u_l2_err,	"memtest_u_l2_err",
	SR_KD_ETI,		memtest_k_l2_err,	"memtest_k_l2_err",
	SR_KD_ETS,		memtest_k_l2_err,	"memtest_k_l2_err",
	NULL,			NULL,			NULL,
};

static cmd_t *commands[] = {
	serrano_cmds,
	us3i_generic_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

void
sr_init(mdata_t *mdatap)
{
	mdatap->m_sopvp = &serrano_uops;
	mdatap->m_copvp = &serrano_cops;
	mdatap->m_cmdpp = commands;
}


/*
 * Initialize the processor specific pieces of the system info
 * structure which is passed in.
 *
 * The initialization is done locally instead of using global
 * kernel variables to maintain as much compatibility with
 * the standalone environment as possible.
 */
static int
sr_get_cpu_info(cpu_info_t *cip)
{
	cip->c_dc_size = 64 * 1024;
	cip->c_dc_linesize = 32;
	cip->c_dc_assoc = 4;

	cip->c_ic_size = 32 * 1024;
	cip->c_ic_linesize = 32;
	cip->c_ic_assoc = 4;

	cip->c_ecr = ch_get_ecr();

	switch (SR_ECCR_CFG_SIZE(cip->c_ecr)) {
	case 0:
		cip->c_l2_size = EC_SIZE_4MB;
		break;
	case 1:
		cip->c_l2_size = EC_SIZE_1MB;
		break;
	case 2:
		cip->c_l2_size = EC_SIZE_2MB;
		break;
	default:
		DPRINTF(0, "sr_get_cpu_info: unsupported E$ size in "
			"ecr=0x%llx", cip->c_ecr);
		return (ENOTSUP);
	}
	cip->c_l2_sublinesize = 64;
	cip->c_l2_linesize = 64;
	cip->c_l2_assoc = 4;
	cip->c_l2_flushsize = cip->c_l2_size * 2;

	cip->c_mem_flags = MEMFLAGS_LOCAL;
	cip->c_mem_start = (uint64_t)cip->c_cpuid << 36;
	cip->c_mem_size = 1ULL << 36;

	return (0);
}

/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains Jaguar specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_chp.h>
#include <sys/memtestio_jg.h>
#include <sys/memtest_u.h>
#include <sys/memtest_u_asm.h>
#include <sys/memtest_ch.h>
#include <sys/memtest_chp.h>
#include <sys/memtest_jg.h>

/*
 * Routines located in this file.
 */
	void		jg_init(mdata_t *);
static	int		jg_get_cpu_info(cpu_info_t *);
static	uint32_t	jg_get_ec_shift(mdata_t *);

/*
 * Jaguar operations vector tables.
 */
static opsvec_u_t jaguar_uops = {
	/* sun4u injection ops vectors */
	chp_write_dtphys,	/* corrupt d$ tag at offset */
	chp_write_itphys,	/* corrupt i$ tag at offset */
	chp_write_etphys,	/* corrupt e$ tag at offset */
	jg_write_memory,	/* corrupt memory */
	chp_write_mtag,		/* corrupt mtag */
	notsup,			/* p-cache error */

	/* sun4u support ops vectors */
	ch_gen_ecc_pa,		/* generate ecc for data at paddr */
};

static opsvec_c_t jaguar_cops = {
	/* common injection ops vectors */
	chp_write_dcache,	/* corrupt d$ at paddr */
	ch_write_dphys,		/* corrupt d$ at offset */
	notsup,			/* no corrupt fp reg */
	chp_write_icache,	/* corrupt i$ at paddr */
	chp_internal_err,	/* internal processor errors */
	ch_write_iphys,		/* corrupt i$ at offset */
	notsup,			/* no corrupt int reg */
	jg_write_ecache,	/* corrupt e$ at paddr */
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
	jg_get_cpu_info,	/* put cpu info into struct */
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

cmd_t	jaguar_cmds[] = {
	JG_KD_PPE,		memtest_k_mem_err,	"memtest_k_mem_err",
	JG_KD_DPE,		memtest_k_mem_err,	"memtest_k_mem_err",
	JG_KD_SAF,		memtest_k_mem_err,	"memtest_k_mem_err",
	NULL,			NULL,			NULL,
};

static cmd_t *commands[] = {
	jaguar_cmds,
	cheetahp_cmds,
	cheetah_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

void
jg_init(mdata_t *mdatap)
{
	mdatap->m_sopvp = &jaguar_uops;
	mdatap->m_copvp = &jaguar_cops;
	mdatap->m_cmdpp = commands;
}

static int
jg_get_cpu_info(cpu_info_t *cip)
{
	cip->c_dc_size = 64 * 1024;
	cip->c_dc_linesize = 32;
	cip->c_dc_assoc = 4;

	cip->c_ic_size = 32 * 1024;
	cip->c_ic_linesize = 32;
	cip->c_ic_assoc = 4;

	cip->c_ecr = ch_get_ecr();
	cip->c_secr = jg_get_secr();

	switch (ECCR_SIZE(cip->c_secr)) {
	case 1:
		cip->c_l2_size = EC_SIZE_4MB;
		break;
	case 2:
		cip->c_l2_size = EC_SIZE_8MB;
		break;
	default:
		DPRINTF(0, "jg_get_cpu_info: unsupported E$ size in "
			"ecr=0x%llx", cip->c_ecr);
		return (ENOTSUP);
	}

	cip->c_l2_sublinesize = 64;
	cip->c_l2_linesize = cip->c_l2_sublinesize *
		(cip->c_l2_size / (4 * 1024 * 1024));
	cip->c_l2_assoc = ECCR_ASSOC(cip->c_secr) + 1;
	cip->c_l2_flushsize = cip->c_l2_size * cip->c_l2_assoc * 2;

	return (0);
}

/*
 * Calculate the appropriate ASI shift value for E$ diagnostic
 * registers depending on the size of the E$.
 */
uint32_t
jg_get_ec_shift(mdata_t *mdatap)
{
	if (mdatap->m_cip->c_l2_size == EC_SIZE_4MB) {
		return (EC_SHIFT_4MB);
	} else if (mdatap->m_cip->c_l2_size == EC_SIZE_8MB) {
		return (EC_SHIFT_8MB);
	} else {
		DPRINTF(0, "jg_get_ec_shift: invalid "
			"si_ec_size=0x%llx\n", mdatap->m_cip->c_l2_size);
		return (0);
	}
}

/*
 * This routine inserts and error into either the ecache, the
 * ecache tags or the ECC protecting the data or tags.
 */
int
jg_write_ecache(mdata_t *mdatap)
{
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	paddr_aligned, xorpat;
	ioc_t		*iocp = mdatap->m_iocp;
	int		reg_offset, ec_shift;
	int		wc_was_disabled = 0;
	int		ret = 0;

	paddr_aligned = P2ALIGN(paddr, 32);

	DPRINTF(3, "jg_write_ecache: paddr=0x%llx, "
		"paddr_aligned=0x%llx\n", paddr, paddr_aligned);

	/*
	 * Touch the ecache data to get it into the modified state but only
	 * if this is not a UC? type of instruction error (e.g. kiucu, kiucc,
	 * kiucutl1, uiucu, and kicpu). This is done in order to verify that
	 * the kernel can recover from those errors.
	 */
	if (ERR_ACC_ISFETCH(iocp->ioc_command) &&
	    ERR_TRAP_ISPRE(iocp->ioc_command)) {
		DPRINTF(3, "\njg_write_ecache: not modifying "
						"the cache line\n");
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
	 * Get the appropriate shift value for ASI access
	 */
	if ((ec_shift = jg_get_ec_shift(mdatap)) == 0) {
		DPRINTF(0, "jg_write_ecache: couldn't determine correct ASI "
			"shift value\n");

		if (wc_was_disabled)
			ch_enable_wc();

		return (EFAULT);
	}

	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {
		/*
		 * Corrupt either the tag or the tag ECC.
		 */
		if (F_CHKBIT(iocp)) {
			jg_wr_ecache_tag_ecc(paddr_aligned,
					xorpat, ec_shift);
		} else {
			jg_wr_ecache_tag_data(paddr_aligned,
						xorpat, ec_shift);
		}
	} else if (ERR_SUBCLASS_ISMH(iocp->ioc_command)) { /* E$ multiway-tag */
		chp_wr_dup_ecache_tag(paddr_aligned, ec_shift);
	} else {
		/*
		 * Corrupt ecache data.
		 */
		if (F_CHKBIT(iocp))
			reg_offset = 4;
		else
			reg_offset = ((paddr) % 32) / sizeof (uint64_t);

		ret = jg_wr_ecache(paddr_aligned, xorpat,
					ec_shift, reg_offset);
	}

	/*
	 * Re-enable the w$ if it was disabled above.
	 */
	if (wc_was_disabled)
		ch_enable_wc();

	/*
	 * Check the return value from the low level routine
	 * for possible error.
	 */
	if (ret == 0xfeccf) {
		DPRINTF(0, "jg_wr_ecache: TEST FAILED (0x%x) could not "
						"locate data in ecache\n", ret);
		return (ENXIO);
	} else {
		return (0);
	}
}

/*
 * This function inserts an error into main memory.
 *
 * This function uses virtual addressing
 * unlike the other memory routines because the underlying
 * assembly routines use the floating point registers as a staging
 * area for the data rather than the E$. This is due to issues with
 * the way the displacement flush ASI works on Cheetah+. Whenever
 * that problem is resolved this code can revert back to using
 * physical addresses.
 */
int
jg_write_memory(mdata_t *mdatap, uint64_t paddr, caddr_t kvaddr, uint_t ecc)
{
	ioc_t		*iocp = mdatap->m_iocp;
	caddr_t		kvaddr_aligned;
	uint64_t	err_ctl_reg = 0;

	DPRINTF(3, "jg_write_memory: mdatap=0x%p, paddr=0x%llx, "
		"kvaddr=0x%p, ecc=0x%x\n",
		mdatap, paddr, kvaddr, ecc);

	/*
	 * Select the correct force bit to set in the
	 * E$ error control register depending on the type
	 * of error desired.
	 */
	switch (iocp->ioc_command) {
	case JG_KD_PPE:
		err_ctl_reg = PPE_ERR_REG;
		break;
	case JG_KD_DPE:
		err_ctl_reg = DPE_ERR_REG;
		break;
	case JG_KD_SAF:
		err_ctl_reg = SAF_ERR_REG;
		break;
	default:
		err_ctl_reg = FMD_ERR_REG;
		break;
	}

	/*
	 * Make sure the address is properly aligned for
	 * use with the fp regs.
	 */
	kvaddr_aligned = (caddr_t)P2ALIGN((uint64_t)kvaddr, 64);

	DPRINTF(3, "jg_write_memory: paddr=0x%llx, kvaddr=0x%p "
		"kvaddr_aligned=0x%p, ecc=0x%x, err_reg=0x%llx\n",
		paddr, kvaddr, kvaddr_aligned, ecc, err_ctl_reg);

	/*
	 * Inject the error.
	 */
	(void) jg_wr_memory(kvaddr_aligned, ecc, err_ctl_reg);

	return (0);
}

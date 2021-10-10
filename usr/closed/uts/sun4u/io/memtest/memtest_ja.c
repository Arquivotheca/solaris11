/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains Jalapeno (us3i) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_chp.h>
#include <sys/memtestio_ja.h>
#include <sys/memtest_u.h>
#include <sys/memtest_u_asm.h>
#include <sys/memtest_ch.h>
#include <sys/memtest_chp.h>
#include <sys/memtest_ja.h>
#include <sys/memtest_sr.h>

/*
 * Routines located in this file.
 */
static	int	ja_get_cpu_info(cpu_info_t *);
	void	ja_init(mdata_t *);
static	int	ja_k_bp_consumer(mdata_t *);
static	int	ja_k_bp_producer(mdata_t *);
static	int	ja_k_fr_consumer(mdata_t *);
static	int	ja_k_fr_producer(mdata_t *);
static	int	ja_k_pc_err(mdata_t *);
static	void	ja_producer(mdata_t *);
static	int	ja_consumer(mdata_t *);

/* snoop sync variable */
volatile int ja_dc_thread_snoop = 0;

/*
 * Debug buffer passed to some assembly routines.
 */
#define		DEBUG_BUF_SIZE	32
uint64_t	debug_buf[DEBUG_BUF_SIZE];

/*
 * Jalapeno operations vector tables.
 */
static opsvec_u_t jalapeno_uops = {
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

static opsvec_c_t jalapeno_cops = {
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
	notsup,			/* no I-D TLB parity errors */

	/* common support ops vectors */
	notsup,			/* no fp reg access */
	notsup,			/* no int reg access */
	memtest_check_afsr,	/* check ESRs */
	ch_enable_errors,	/* enable AFT errors */
	notimp,			/* no enable/disable L2 or memory scrubbers */
	ja_get_cpu_info,	/* put cpu info into system_info_t struct */
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
 * These US3i generic and Jalapeno errors are grouped according to the
 * definitions in the header file.
 */
cmd_t us3i_generic_cmds[] = {
	JA_KD_FRUE,		ja_k_pc_err,		"ja_k_pc_err",
	JA_KI_FRUE,		ja_k_pc_err,		"ja_k_pc_err",
	JA_UD_FRUE,		memtest_u_mem_err,	"memtest_u_mem_err",
	JA_UI_FRUE,		memtest_u_mem_err,	"memtest_u_mem_err",
	JA_KD_FRCE,		ja_k_pc_err,		"ja_k_pc_err",
	JA_KD_FRCETL1,		ja_k_pc_err,		"ja_k_pc_err",
	JA_KD_FRCESTORM,	ja_k_pc_err,		"ja_k_pc_err",
	JA_KI_FRCE,		ja_k_pc_err,		"ja_k_pc_err",
	JA_KI_FRCETL1,		ja_k_pc_err,		"ja_k_pc_err",
	JA_UD_FRCE,		memtest_u_mem_err,	"memtest_u_mem_err",
	JA_UI_FRCE,		memtest_u_mem_err,	"memtest_u_mem_err",

	JA_KD_BE,		ja_k_bus_err,		"ja_k_bus_err",
	JA_KD_BEPEEK,		ja_k_bus_err,		"ja_k_bus_err",
	JA_KD_BEPR,		ja_k_bus_err,		"ja_k_bus_err",
	JA_KD_TO,		ja_k_bus_err,		"ja_k_bus_err",
	JA_KD_TOPR,		ja_k_bus_err,		"ja_k_bus_err",
	JA_KD_BP,		ja_k_pc_err,		"ja_k_pc_err",
	JA_KD_OM,		ja_k_bus_err,		"ja_k_bus_err",
	JA_KD_UMS,		ja_k_bus_err,		"ja_k_bus_err",
	JA_KD_WBP,		ja_k_wbp_err,		"ja_k_wbp_err",
	JA_KD_JETO,		ja_k_bus_err,		"ja_k_bus_err",
	JA_KD_SCE,		ja_k_bus_err,		"ja_k_bus_err",
	JA_KD_JEIC,		ja_k_bus_err,		"ja_k_bus_err",
	JA_KD_ISAP,		ja_k_isap_err,		"ja_k_isap_err",

	JA_KD_ETP,		memtest_k_l2_err,	"memtest_k_l2_err",

	CHP_KD_DDSPEL,		memtest_k_dc_err,	"memtest_k_dc_err",
	CHP_KD_DDSPELTL1,	memtest_k_dc_err,	"memtest_k_dc_err",
	CHP_KD_DTSPEL,		memtest_k_dc_err, 	"memtest_k_dc_err",
	CHP_KD_DTSPELTL1,	memtest_k_dc_err, 	"memtest_k_dc_err",
	CHP_KD_DTHPEL,		memtest_k_dc_err,	"memtest_k_dc_err",
	G4U_DTPHYS,		memtest_dtphys,		"memtest_dtphys",

	CHP_KI_IDSPE,		memtest_k_ic_err, 	"memtest_k_ic_err",
	CHP_KI_IDSPETL1,	memtest_k_ic_err, 	"memtest_k_ic_err",
	CHP_KI_IDSPEPCR,	memtest_k_ic_err,	"memtest_k_ic_err",
	CHP_KI_ITSPE,		memtest_k_ic_err,	"memtest_k_ic_err",
	CHP_KI_ITSPETL1,	memtest_k_ic_err,	"memtest_k_ic_err",
	CHP_KI_ITHPE,		memtest_k_ic_err,	"memtest_k_ic_err",
	G4U_ITPHYS,		memtest_itphys,		"memtest_itphys",
	NULL,			NULL,			NULL,
};

cmd_t jalapeno_cmds[] = {
	JA_KD_ETP,		memtest_k_l2_err,	"memtest_k_l2_err",
	NULL,			NULL,			NULL,
};

static cmd_t *commands[] = {
	jalapeno_cmds,
	us3i_generic_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

void
ja_debug_init()
{
	int	i;

	for (i = 0; i < DEBUG_BUF_SIZE; i++)
		debug_buf[i] = 0xeccdeb46eccdeb46;
}

void
ja_debug_dump()
{
	int	i;

	for (i = 0; i < DEBUG_BUF_SIZE; i++) {
		DPRINTF(0, "ja_debug_dump: debug_buf[0x%2x]=0x%llx\n",
			i*8, debug_buf[i]);
	}
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
ja_get_cpu_info(cpu_info_t *cip)
{
	cip->c_dc_size = 64 * 1024;
	cip->c_dc_linesize = 32;
	cip->c_dc_assoc = 4;

	cip->c_ic_size = 32 * 1024;
	cip->c_ic_linesize = 32;
	cip->c_ic_assoc = 4;

	cip->c_ecr = ch_get_ecr();

	switch (JA_ECCR_SIZE(cip->c_ecr)) {
	case 0:
		cip->c_l2_size = EC_SIZE_HALF_MB;
		break;
	case 1:
		cip->c_l2_size = EC_SIZE_1MB;
		break;
	case 2:
		cip->c_l2_size = EC_SIZE_2MB;
		break;
	case 3:
		cip->c_l2_size = EC_SIZE_4MB;
		break;
	default:
		DPRINTF(0, "ja_get_cpu_info: unsupported E$ size in "
			"ecr=0x%llx", cip->c_ecr);
		return (ENOTSUP);
	}
	cip->c_l2_sublinesize = 64;
	cip->c_l2_linesize = 64;
	cip->c_l2_assoc = 4;
	cip->c_l2_flushsize = cip->c_l2_size * 5;

	cip->c_mem_flags = MEMFLAGS_LOCAL;
	cip->c_mem_start = (uint64_t)cip->c_cpuid << 36;
	cip->c_mem_size = 1ULL << 36;

	return (0);
}

void
ja_init(mdata_t *mdatap)
{
	mdatap->m_sopvp = &jalapeno_uops;
	mdatap->m_copvp = &jalapeno_cops;
	mdatap->m_cmdpp = commands;
}

/*
 * This routine generate a various JBus errors.
 */
int
ja_k_bus_err(mdata_t *mdatap)
{

	memtest_t	*memtestp = mdatap->m_memtestp;
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr, tmpl;
	int		ret = 0;
	caddr_t		kvaddr;

	/*
	 * Note that all of these tests are bound to CPUs
	 * such that the data buffer is local memory.
	 */
	switch (iocp->ioc_command) {
	case JA_KD_BE:
	case JA_KD_BEPEEK:
	case JA_KD_BEPR:
		/*
		 * JBus "Bus Error" or "Timeout" error response.
		 * This can be caused by Tomatillo due to:
		 *	1) Read from non-existant pci memory space.
		 *	2) Illigal read from IO space.
		 *	3) According to Tomatillo spec:
		 *		- pio read pci master abort
		 *		- pio read pci retry limit error
		 *		- pio read pci target abort
		 *		- pio read pci data parity error
		 */
		/*
		 * Method 1.
		 * Read from a non-existant pci device's memory space.
		 * This is done by reding from pci device #31's configuration
		 * space off of the master Tomatillo's 66Mhz pci bus. Note
		 * that pci device #31 does not exist on any 66Mhz pci bus
		 * which is why we get the error.
		 */
		paddr = 0x7f60000f800;
		if ((kvaddr = memtest_map_p2kvaddr(mdatap, paddr, MMU_PAGESIZE,
				SFMMU_UNCACHEPTTE, 0)) == NULL) {
			return (ENOMEM);
		}
		DPRINTF(3, "ja_k_bus_err: BERR: paddr=0x%llx, kvaddr=0x%p\n",
			paddr, kvaddr);
		if (ERR_MISC_ISDDIPEEK(iocp->ioc_command)) {
			if (ddi_peek8(memtestp->m_dip, kvaddr,
			    (char *)&tmpl) != DDI_SUCCESS) {
				cmn_err(CE_NOTE, "ja_k_bus_err: peek failed "
					"as expected\n");
			} else {
				cmn_err(CE_WARN, "ja_k_bus_err: unexpected "
					"peek success occurred!\n");
				ret = EIO;
			}
		} else if (ERR_ACC_ISPFETCH(iocp->ioc_command)) {
			memtest_prefetch_rd_access(kvaddr);
			cmn_err(CE_NOTE, "ja_k_bus_err: prefetch succeeded "
				"as expected\n");
		} else {
			memtest_asmld(kvaddr);
		}
		break;
	case JA_KD_JEIC:
		/*
		 * System interface protocol error due to illegal command.
		 * This can be caused by:
		 *	1) A non-cacheable read to cacheable space.
		 *	2) A cacheable read to non-cacheable space.
		 */
		/*
		 * Method 1.
		 */
		paddr = va_to_pa(mdatap->m_databuf);
		(void) peek_asi64(ASI_IO, paddr);
		break;
	case JA_KD_JETO:
		/*
		 * System interface protocol error due to hardware timeout.
		 * This can be caused by:
		 *	1) hardware counter time out
		 *	2) read to (unsupported) CPU non-cacheable space
		 *
		 * Note that PA[42:39] and PA[35:34] should be zero to fit
		 * into the L2 tag and not cause an OM.
		 */
		break;
	case JA_KD_OM:
		/*
		 * Out of range memory error.
		 * This can be caused by:
		 *	1) Cacheable read with unsupported L2 tag bits set.
		 *	   PA[40:39] or PA[35:34]
		 */

		/*
		 * Method 1.
		 * Access a local physical address, but set one of
		 * the unsupported bits (34).
		 */
		paddr = va_to_pa(mdatap->m_databuf) | 0x400000000;
		DPRINTF(3, "ja_k_bus_err: OM: paddr=0x%llx\n", paddr);
		(void) peek_asi64(ASI_MEM, paddr);
		break;
	case JA_KD_TO:
	case JA_KD_TOPR:
		/*
		 * "Unmapped" JBus error response.
		 * JBus non-existant port or address errors.
		 * This can be caused by:
		 *	1) Read to a non-existant JBus port.
		 *	2) Write to non-existant port, including block store,
		 *	   store to IO, and block store to IO, or E$ writeback.
		 *	3) Interrupt to non-existant JBus port.
		 *	4) Read to existing port with an unsupported address.
		 */

		/*
		 * Method 1.
		 * We use port ID 7 since it is known to not exist.
		 */
		paddr = 0x7000000000;
		if ((kvaddr = memtest_map_p2kvaddr(mdatap, paddr, MMU_PAGESIZE,
		    0, 0)) == NULL) {
			return (ENOMEM);
		}
		DPRINTF(3, "ja_k_bus_err: TO: reading paddr=0x%llx, "
			"kvaddr=0x%p\n", paddr, kvaddr);
		/*
		 * Due to Jalapeno Errata #52 this also causes a JETO
		 * error therefore we must disable protocol errors.
		 */
		(void) memtest_set_eer(memtest_get_eer() & ~EN_REG_PERREN);
		if (ERR_ACC_ISPFETCH(iocp->ioc_command)) {
			memtest_prefetch_rd_access(kvaddr);
			cmn_err(CE_NOTE, "ja_k_bus_err: prefetch succeeded "
				"as expected\n");
		} else {
			memtest_asmld(kvaddr);
		}
		break;
	case JA_KD_UMS:
		/*
		 * Unsupported store.
		 * This can be caused by:
		 *	1) non-cacheable write to a CPU
		 *	2) cacheable write with nonzero bits in PA[40:39,35:34]
		 *	3) cacheable write to non-existant memory
		 */

		/*
		 * Method 1.
		 */
		paddr = va_to_pa(mdatap->m_databuf);
		poke_asi64(ASI_IO, paddr, 0);
		break;
	default:
		DPRINTF(0, "ja_k_bus_err: unsupported command 0x%llx\n",
			iocp->ioc_command);
		ret = ENOTSUP;
	}

	return (ret);
}

/*
 * This routine inserts an error into either the ecache, the
 * ecache tags or the ECC protecting the data or tags.
 */
int
ja_write_ecache(mdata_t *mdatap)
{
	uint64_t	paddr = mdatap->m_paddr_c;
	cpu_info_t	*cip = mdatap->m_cip;
	uint64_t	ec_set_size = cip->c_l2_size / cip->c_l2_assoc;
	uint64_t	paddr_aligned, xorpat;
	ioc_t		*iocp = mdatap->m_iocp;
	int		reg_offset, ret;
	int		wc_was_disabled = 0;

	paddr_aligned = P2ALIGN(paddr, 32);

	DPRINTF(3, "ja_write_ecache: paddr=0x%llx, "
		"paddr_aligned=0x%llx\n", paddr, paddr_aligned);

	/*
	 * Touch the ecache data to get it into the modified state except
	 * for a UC? or CP? type of instruction error (e.g. kiucu, kiucc,
	 * uiucu, uiucc, kicpu, etc.) This is done in order to verify that
	 * the kernel can recover from those errors.
	 */
	if ((ERR_ACC_ISFETCH(iocp->ioc_command) &&
	    ERR_TRAP_ISPRE(iocp->ioc_command)) ||
	    (ERR_ACC_ISPFETCH(iocp->ioc_command))) {
		DPRINTF(0, "\nja_write_ecache: not modifying cache line\n");
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

	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {
		if (CPU_ISSERRANO(cip)) {
			sr_wr_ecache_tag(paddr_aligned, xorpat,
				    (uint64_t *)debug_buf);
		} else {
			ja_wr_ecache_tag(paddr_aligned, xorpat,
			    (uint64_t *)debug_buf);
		}
	} else {
		/*
		 * Corrupt ecache data.
		 */
		if (F_CHKBIT(iocp))
			reg_offset = 4;
		else
			reg_offset = ((paddr) % 32) / sizeof (uint64_t);

		DPRINTF(3, "ja_write_ecache: calling ja_wr_ecache("
			"paddr_aligned=0x%llx, xorpat=0x%llx, "
			"reg_offset=0x%x, debug_buf=0x%p)\n",
			paddr_aligned, xorpat, reg_offset,
			(uint64_t *)debug_buf);

		ret = ja_wr_ecache(paddr_aligned, xorpat, reg_offset,
			ec_set_size, (uint64_t *)debug_buf);

#ifdef	DEBUG_WR_ECACHE
		ja_debug_dump();
#endif

		if (ret != 0) {
			DPRINTF(0, "ja_write_ecache: ja_wr_ecache() failed, "
				"ret=0x%x\n", ret);
			if (wc_was_disabled)
				ch_enable_wc();
			return (ret);
		}
	}

	/*
	 * Re-enable the w$ if it was disabled above.
	 */
	if (wc_was_disabled)
		ch_enable_wc();

	return (0);
}

/*
 * This routine injects an error into the physical
 * tags of the ecache at the specified physical offset.
 */
int
ja_write_ephys(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	offset_aligned, xorpat, data;
	int		reg_offset;

	DPRINTF(2, "ja_write_ephys: iocp=0x%p, offset=0x%llx)\n", iocp, offset);

	offset_aligned = P2ALIGN(offset, 32);
	xorpat = IOC_XORPAT(iocp);

	/*
	 * For Jalapeno we need to make sure that PA<24> is
	 * not set as this is interpreted as the disp_flush
	 * bit.
	 */
	offset_aligned = (offset_aligned & EC_TAG_MASK);

	DPRINTF(3, "\nja_write_ephys: offset=0x%llx, "
		"offset_aligned=0x%llx\n", offset, offset_aligned);

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

	/*
	 * Place an error into the e$ data/tag possibly modifying state.
	 */
	if (ERR_SUBCLASS_ISTAG(iocp->ioc_command)) {
		if (CPU_ISSERRANO(cip)) {
			DPRINTF(3, "ja_write_ephys: calling "
				"sr_wr_etphys(pa=0x%llx, "
				"xor=0x%llx, data=0x%llx)\n",
				offset_aligned, xorpat, data);
			sr_wr_etphys(offset_aligned, xorpat, data);
		} else {
			DPRINTF(3, "ja_write_ephys: calling "
				"ja_wr_etphys(pa=0x%llx, "
				"xor=0x%llx, data=0x%llx)\n",
				offset_aligned, xorpat, data);
			ja_wr_etphys(offset_aligned, xorpat, data);
		}
	} else {
		/*
		 * Select the register offset for forcing either a
		 * checkbit error or data error.
		 */
		if (F_CHKBIT(iocp))
			reg_offset = 4;
		else
			reg_offset = ((offset) % 32) / sizeof (uint64_t);

		if (CPU_ISSERRANO(cip)) {
			DPRINTF(3, "ja_write_ephys: calling "
				"sr_wr_ephys(pa=0x%llx, "
				"off=0%llx, xor=0x%llx, data=0x%llx)\n",
				offset_aligned, reg_offset, xorpat, data);
			sr_wr_ephys(offset_aligned, reg_offset, xorpat, data);
		} else {
			DPRINTF(3, "ja_write_ephys: calling "
				"ja_wr_ephys(pa=0x%llx, "
				"off=0%llx, xor=0x%llx, data=0x%llx)\n",
				offset_aligned, reg_offset, xorpat, data);
			ja_wr_ephys(offset_aligned, reg_offset, xorpat, data);
		}
	}

	return (0);
}

/*
 * This function inserts an error into main memory.
 */
/*ARGSUSED*/
int
ja_write_memory(mdata_t *mdatap, uint64_t paddr, caddr_t kvaddr, uint_t ecc)
{
	cpu_info_t	*cip = mdatap->m_cip;
	uint64_t	ec_set_size = cip->c_l2_size / cip->c_l2_assoc;
	int		ret;

	DPRINTF(3, "ja_write_memory: paddr=0x%llx, kvaddr=0x%p, ecc=0x%x)\n",
		paddr, kvaddr, ecc);

	/*
	 * Sanity check.
	 * We can only inject memory errors into local memory.
	 */
	if ((paddr >> 36) != getprocessorid()) {
		DPRINTF(0, "ja_write_memory: not local memory, "
			"paddr=0x%llx, cpuid=%d\n", paddr, getprocessorid());
		return (-1);
	}

#ifdef	DEBUG_WR_MEMORY
	ja_debug_init();
#endif

	ret = ja_wr_memory(paddr, ecc, ec_set_size, (uint64_t *)debug_buf);

#ifdef	DEBUG_WR_MEMORY
	ja_debug_dump();
#endif

	DPRINTF(3, "ja_write_memory: ret = 0x%d\n", ret);
	return (ret);
}

/*
 * This routine generates correct JBus parity for the given
 * address high, address jow, and adtype data.
 */
uint_t
jbus_parity(uint64_t j_ad_h, uint64_t j_ad_l, uint_t j_adtype)
{
	int	count;
	uint_t	j_adp = 0;

	/*
	 * j_adp[0] = odd parity over j_ad[31:0]
	 * j_adp[1] = odd parity over j_ad[63:32]
	 * j_adp[2] = odd parity over j_ad[95:64]
	 * j_adp[3] = odd parity over j_ad[127:96] + j_adtype[7:0]
	 */
	count = memtest_popc64(j_ad_l & 0xffffffff);
	j_adp |= (~count & 1);

	count = memtest_popc64(j_ad_l >> 32);
	j_adp |= (~count & 1) << 1;

	count = memtest_popc64(j_ad_h & 0xffffffff);
	j_adp |= (~count & 1) << 2;

	count = memtest_popc64(j_ad_h >> 32) + memtest_popc64(j_adtype);
	j_adp |= (~count & 1) << 3;

	DPRINTF(3, "jbus_parity: return=0x%x\n", j_adp);

	return (j_adp);
}

#define	FIX_TT

/*
 * This routine generates a kernel Jbus parity error on writeback.
 */
int
ja_k_wbp_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	j_ad_h, j_ad_l;
	uint_t		j_adp = 0, j_adtype;
	int		ret = 0;

#ifdef	FIX_TT
	int		i;
	caddr_t		ttp, nttp;
	extern	void	ja_tt63();
	extern	caddr_t	trap_table0;

	ttp = (caddr_t)(&trap_table0) + (0x63 * 32);
	nttp = (caddr_t)(&ja_tt63);
	DPRINTF(2, "ja_k_wbp_err: ttp=0x%p, nttp=0x%p\n", ttp, nttp);
	for (i = 0; i < 32; i += 4) {
		DPRINTF(2, "ja_k_wbp_err: orig tt entry: [0x%p]=0x%x\n",
			(ttp + i), *(uint_t *)(ttp + i));
	}
	bcopy(nttp, ttp, 32);
	for (i = 0; i < 32; i += 4) {
		DPRINTF(2, "ja_k_wbp_err: new tt entry: [0x%p]=0x%x\n",
			(ttp + i), *(uint_t *)(ttp + i));
	}
#endif	/* FIX_TT */

#ifdef	DEBUG_WBP
	ja_debug_init();
#endif

	/*
	 * Note that jalapeno does not issue just a wb, it issues
	 * a rd+wb. Even for flushes, a rd is issued. So we calculate
	 * correct parity based on the expected address cycle in order
	 * to avoid an address parity error and just cause a data
	 * parity error.
	 *
	 * Calculate the j_ad for the address cycle.
	 *	j_ad[42:0]  = physical address
	 *	j_ad[47:43] = transaction type = WRB = 0xc
	 *	j_ad[63:48] = 16 byte enables = all on
	 *	j_ad[127:64] = j_ad[63:0]
	 */
	j_ad_l = mdatap->m_paddr_c;
	j_ad_l |= 0xcULL << 43;
	j_ad_l |= 0xffffULL << 48;
	j_ad_h = j_ad_l;

	/*
	 * Calculate expected j_adtype for the address cycle;
	 *	j_adtype[1:0] = 0 (write transaction)
	 *	j_adtype[5:2] = agent ID = cpu ID
	 *	j_adtype[7:6] = 3
	 */
	j_adtype = CPU->cpu_id << 2;
	j_adtype |= 3 << 6;

	DPRINTF(0, "ja_k_wbp_err: j_ad_h=0x%llx, j_ad_l=0x%llx, "
		"j_adtype=0x%llx, j_adp=0x%x\n",
		j_ad_h, j_ad_l, j_adtype, j_adp);

	/*
	 * We must calculate the parity such that we don't get
	 * an address parity (ISAP) error.
	 */
	j_adp = jbus_parity(j_ad_h, j_ad_l, j_adtype);

	DPRINTF(0, "ja_k_wbp_err: calling ja_wbp(pa=0x%llx, parity=0x%x, "
		"debug=0x%p)\n", mdatap->m_paddr_c, j_adp, debug_buf);

	if (F_NOERR(iocp)) {
		DPRINTF(2, "ja_k_wbp_err: not invoking error\n");
		return (0);
	}

	if (!F_NOERR(iocp)) {
		ja_wbp(mdatap->m_paddr_c, j_adp, (uint64_t *)debug_buf);
#ifdef	DEBUG_WBP
		ja_debug_dump();
#endif
	}

	return (ret);
}

/*
 * This routine generates a kernel Jbus parity error on copyout.
 * XXX This is not fully implemented.
 */
int
ja_k_isap_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	j_ad_h, j_ad_l;
	uint_t		j_adp = 0, j_adtype;
	int		ret = 0;

#ifdef	DEBUG_ISAP
	ja_debug_init();
#endif

	/*
	 * We calculate correct parity based on the expected address cycle
	 * in order to guarantee an address parity error.
	 *
	 * Calculate the j_ad for the address cycle.
	 *	j_ad[42:0]  = physical address
	 *	j_ad[47:43] = transaction type = WRB = 0xc
	 *	j_ad[63:48] = 16 byte enables = all on
	 *	j_ad[127:64] = j_ad[63:0]
	 */
	j_ad_l = mdatap->m_paddr_c;
	j_ad_l |= 0xcULL << 43;
	j_ad_l |= 0xffffULL << 48;
	j_ad_h = j_ad_l;

	/*
	 * Calculate expected j_adtype for the address cycle;
	 *	j_adtype[1:0] = 0 (write transaction)
	 *	j_adtype[5:2] = agent ID = cpu ID
	 *	j_adtype[7:6] = 3
	 */
	j_adtype = CPU->cpu_id << 2;
	j_adtype |= 3 << 6;

	DPRINTF(0, "ja_k_isap_err: j_ad_h=0x%llx, j_ad_l=0x%llx, "
		"j_adtype=0x%llx, j_adp=0x%x\n",
		j_ad_h, j_ad_l, j_adtype, j_adp);

	/*
	 * We must calculate the parity such that we do get
	 * an address parity (ISAP) error. Therefore we flip
	 * j_adp[0].
	 */
	j_adp = jbus_parity(j_ad_h, j_ad_l, j_adtype) ^ 1;

	DPRINTF(0, "ja_k_isap_err: calling ja_bp(pa=0x%llx, parity=0x%x, "
		"debug=0x%p)\n", mdatap->m_paddr_c, j_adp, debug_buf);

	if (F_NOERR(iocp)) {
		DPRINTF(2, "ja_k_isap_err: not invoking error\n");
		return (0);
	}

	if (!F_NOERR(iocp)) {
		ja_wbp(mdatap->m_paddr_c, j_adp, (uint64_t *)debug_buf);
#ifdef	DEBUG_ISAP
		ja_debug_dump();
#endif
	}

	return (ret);
}

/*
 * This is the consumer thread for the kernel Jbus parity
 * error on writeback test.
 */
static int
ja_bp_consumer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;

	/*
	 * Invoke the error and store a value to the sync variable
	 * indicating that we've invoked the error.
	 */
	if (!F_NOERR(iocp)) {
		DPRINTF(3, "ja_bp_consumer: invoking error at vaddr=0x%p, "
			"paddr=0x%llx\n",
			mdatap->m_kvaddr_a, mdatap->m_paddr_a);
		mdatap->m_asmldst(mdatap->m_kvaddr_a, (caddr_t)syncp, 3);
	} else
		*syncp = 3;

	return (0);
}

/*
 * This is the producer thread for the kernel Jbus parity
 * error on writeback test.
 */
static int
ja_bp_producer(mdata_t *mdatap)
{
	uint64_t		new_eer, old_eer, j_ad_h, j_ad_l;
	volatile uint64_t	*datal;
	uint_t			j_adp = 0, j_adtype;
	volatile int		*syncp = mdatap->m_syncp;
	int			ret = 0;

	/*
	 * Calculate correct parity based on the expected data.
	 */
	datal = (uint64_t *)mdatap->m_kvaddr_c;
	j_ad_l = datal[0];
	j_ad_h = datal[1];

	/*
	 * Calculate expected j_adtype for the address cycle;
	 *	j_adtype[1:0] = 2 (RDS)
	 *	j_adtype[5:2] = agent ID = cpu ID
	 *	j_adtype[7:6] = 3
	 */
	j_adtype = 2;
	j_adtype |= (mdatap->m_cip->c_cpuid << 2);
	j_adtype |= 3 << 6;

	DPRINTF(3, "ja_bp_producer: j_ad_h=0x%llx, j_ad_l=0x%llx, "
		"j_adtype=0x%llx, j_adp=0x%x\n",
		j_ad_h, j_ad_l, j_adtype, j_adp);

	/*
	 * Calculate correct parity and then flip j_adp[0]
	 * to ensure a parity error.
	 */
	j_adp = jbus_parity(j_ad_h, j_ad_l, j_adtype) ^ 1;

	DPRINTF(3, "ja_bp_producer: calling ja_bp(pa=0x%llx, parity=0x%x, "
		"debug=0x%p)\n", mdatap->m_paddr_c, j_adp, debug_buf);

	/*
	 * Set EER to generate bad parity.
	 * And disable addr parity errors on the cpu.
	 */
	old_eer = memtest_get_eer();
	new_eer = old_eer & ~EN_REG_FPD;
	new_eer |= (j_adp << EN_REG_FPD_SHIFT);
	new_eer |= EN_REG_FSP;
	new_eer &= ~EN_REG_ISAPEN;

	OP_FLUSHALL_L2(mdatap);
	memtest_disable_intrs();

	/*
	 * Force the data into a modified state.
	 */
	datal[0] = datal[0];

	DPRINTF(2, "ja_bp_producer: setting EER to 0x%llx\n", new_eer);
	(void) memtest_set_eer(new_eer);

	/*
	 * Tell the consumer that we've injected the error.
	 */
	DPRINTF(3, "ja_bp_producer: setting sync flag to 2\n");
	*syncp = 2;

	DPRINTF(3, "ja_bp_producer: waiting for consumer to "
		"invoke the error\n");

	/*
	 * Wait for consumer to invoke the error but don't wait forever.
	 */
	if (memtest_wait_sync(syncp, 3, SYNC_WAIT_MAX, "ja_bp_producer") !=
	    SYNC_STATUS_OK)
		ret = EIO;

	/*
	 * Restore EER and re-enable interrupts.
	 */
	(void) memtest_set_eer(old_eer);
	memtest_enable_intrs();

	DPRINTF(3, "ja_bp_producer: ret=%d, sync=%d\n", ret, *syncp);
	return (ret);

}

/*
 * This is the consumer thread for the foreign/remote tests.
 */
static int
ja_fr_consumer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	uint64_t	pa_2access = mdatap->m_paddr_a;
	uint64_t	paddr;
	uint_t		myid;
	int		stride = 0x40;
	int		count, i;
	int		err_acc = ERR_ACC(iocp->ioc_command);
	caddr_t		kva_2access = mdatap->m_kvaddr_a;
	caddr_t		vaddr;

	/*
	 * Check if this is a "storm" command and set the
	 * error count accordingly.
	 */
	if (ERR_PROT_ISCE(iocp->ioc_command) &&
	    ERR_MISC_ISSTORM(iocp->ioc_command)) {
		if (F_MISC1(iocp))
			count = iocp->ioc_misc1;
		else
			count = 64;
		if (count > (iocp->ioc_bufsize / stride))
			count = iocp->ioc_bufsize / stride;

		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "ja_fr_consumer: accessing %d "
				"CE errors: paddr=0x%08x.%08x, stride=0x%x\n",
				count, PRTF_64_TO_32(pa_2access), stride);
		}
	} else {
		count = 1;
	}

	myid = getprocessorid();

	for (i = 0, vaddr = kva_2access, paddr = pa_2access;
	    i < count; i++, vaddr += stride, paddr += stride) {
		/*
		 * If we do not want to invoke the error(s) then continue.
		 */
		if (F_NOERR(iocp)) {
			DPRINTF(2, "ja_fr_consumer: not invoking error %d "
				"at vaddr=0x%p, paddr=0x%llx\n",
				i, vaddr, paddr);
			continue;
		}
		if (memtest_debug >= 2) {
			DPRINTF(0, "ja_fr_consumer: invoking error %d at "
				"vaddr=0x%p, paddr=0x%llx\n",
				i, vaddr, paddr);
		}
		switch (err_acc) {
		case ERR_ACC_LOAD:
		case ERR_ACC_FETCH:
			if (ERR_MISC_ISTL1(iocp->ioc_command))
				xt_one(myid, (xcfunc_t *)mdatap->m_asmld_tl1,
					(uint64_t)vaddr, (uint64_t)0);
			else
				(mdatap->m_asmld)(vaddr);
			break;
		case ERR_ACC_PFETCH:
			memtest_prefetch_access(iocp, vaddr);
			DELAY(100);
			break;
		case ERR_ACC_BLOAD:
			if (ERR_MISC_ISTL1(iocp->ioc_command))
				xt_one(myid, (xcfunc_t *)mdatap->m_blkld_tl1,
					(uint64_t)vaddr, (uint64_t)0);
			else
				mdatap->m_blkld(vaddr);
			break;
		case ERR_ACC_STORE:
			if (ERR_MISC_ISTL1(iocp->ioc_command))
				xt_one(myid, (xcfunc_t *)mdatap->m_asmst_tl1,
					(uint64_t)vaddr, (uint64_t)0xff);
			else {
				DPRINTF(0, "ja_fr_consumer: storing"
				" to invoke error\n");
				*vaddr = (uchar_t)0xff;
			}
			membar_sync();
			break;

		}
	}

	/*
	 * Notify the producer thread to exit.
	 */
	*syncp = 3;

	return (0);
}

/*
 * This is the producer thread for the foreign/remote tests.
 */
static int
ja_fr_producer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	uint64_t	pa_2corrupt = mdatap->m_paddr_c;
	uint64_t	paddr;
	int		stride = 0x40;
	int		ret = 0;
	int		count, i;
	caddr_t		kva_2corrupt = mdatap->m_kvaddr_c;
	caddr_t		vaddr;

	/*
	 * Check if this is a "storm" command and set the
	 * error count accordingly.
	 */
	if (ERR_PROT_ISCE(iocp->ioc_command) &&
	    ERR_MISC_ISSTORM(iocp->ioc_command)) {
		if (F_MISC1(iocp))
			count = iocp->ioc_misc1;
		else
			count = 64;
		if (count > (iocp->ioc_bufsize / stride))
			count = iocp->ioc_bufsize / stride;
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "ja_fr_producer: injecting %d "
				"CE errors: paddr=0x%08x.%08x, stride=0x%x\n",
				count, PRTF_64_TO_32(pa_2corrupt), stride);
		}
	} else {
		count = 1;
	}

	DPRINTF(2, "ja_fr_producer: injecting the error(s)\n");

	/*
	 * Inject the error(s).
	 */
	for (i = 0, vaddr = kva_2corrupt, paddr = pa_2corrupt;
	    i < count; i++, vaddr += stride, paddr += stride) {
		mdatap->m_kvaddr_c = vaddr;
		mdatap->m_paddr_c = paddr;
		DPRINTF(2, "ja_fr_producer: injecting error %d at "
			"vaddr=0x%p, paddr=0x%llx\n", i, vaddr, paddr);
		if ((ret = memtest_inject_memory(mdatap)) != 0)
			return (ret);
	}

	/*
	 * Tell the consumer that we've injected the error.
	 */
	*syncp = 2;

	DPRINTF(3, "ja_fr_producer: waiting for consumer to "
		"invoke the error\n");

	/*
	 * Wait for consumer to invoke the error but don't wait forever.
	 */
	if (memtest_wait_sync(syncp, 3, SYNC_WAIT_MAX, "ja_fr_producer") !=
	    SYNC_STATUS_OK)
		ret = EIO;

	DPRINTF(3, "ja_fr_producer: ret=%d, sync=%d\n", ret, *syncp);

	return (ret);
}

/*
 * This is the common producer thread routine for the
 * producer/consumer tests.
 */
static void
ja_producer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	int		ret;

	/*
	 * Disable preemption in case it isn't already disabled.
	 */
	kpreempt_disable();

	/*
	 * Wait for OK to inject the error, but don't wait forever.
	 */
	if (memtest_wait_sync(syncp, 1, SYNC_WAIT_MAX, "ja_producer") !=
	    SYNC_STATUS_OK) {
		*syncp = -1;
		kpreempt_enable();
		thread_exit();
	}

	/*
	 * Inject the error.
	 */
	if (ERR_JBUS_ISFR(iocp->ioc_command)) {
		ret = ja_fr_producer(mdatap);
	} else if (IOC_COMMAND(iocp) == JA_KD_BP) {
		ret = ja_bp_producer(mdatap);
	} else {
		DPRINTF(3, "ja_producer: unsupported command=0x%llx\n",
			IOC_COMMAND(iocp));
		ret = EIO;
	}

	/*
	 * If there were any errors set the
	 * sync variable to the error value.
	 */
	if (ret)
		*syncp = -1;

	kpreempt_enable();
	thread_exit();
}

/*
 * This is the common consumer thread routine for the
 * producer/consumer tests.
 */
static int
ja_consumer(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	int		ret;

	DPRINTF(3, "ja_consumer: mdatap=0x%p, syncp=0x%p\n",
		mdatap, syncp);

	/*
	 * Release the producer thread and have it inject the error(s).
	 */
	*syncp = 1;

	DPRINTF(3, "ja_consumer: waiting for producer to inject error\n");

	/*
	 * Wait for the producer thread to inject the error(s),
	 * but don't wait forever.
	 */
	if (memtest_wait_sync(syncp, 2, SYNC_WAIT_MAX, "ja_consumer") !=
	    SYNC_STATUS_OK) {
		*syncp = -1;
		return (EIO);
	}

	/*
	 * We mustn't disable preemption prior to the producer injecting
	 * the error since the producer may try to offline all cpus
	 * while injecting the error.
	 */
	kpreempt_disable();

	if (ERR_JBUS_ISFR(iocp->ioc_command)) {
		ret = ja_fr_consumer(mdatap);
	} else if (IOC_COMMAND(iocp) == JA_KD_BP) {
		ret = ja_bp_consumer(mdatap);
	} else {
		DPRINTF(3, "ja_consumer: unsupported command=0x%llx\n",
			IOC_COMMAND(iocp));
		ret = EIO;
	}

	/*
	 * Make sure we give the producer thread a chance to
	 * notice the update to the sync flag and exit.
	 */
	delay(1 * hz);

	kpreempt_enable();
	return (ret);
}

/*
 * This routine is the main producer/consumer test routine.
 * It invokes threads for the producer and consumer.
 */
static int
ja_k_pc_err(mdata_t *mdatap)
{
	memtest_t	*mp = mdatap->m_memtestp;
	mdata_t		*producer_mdatap, *consumer_mdatap;
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	sync;

	/*
	 * Sanity check.
	 */
	if (iocp->ioc_nthreads != 2) {
		DPRINTF(0, "ja_k_pc_err: nthreads=%d should be 2\n",
			iocp->ioc_nthreads);
		return (EIO);
	}

	consumer_mdatap = mp->m_mdatap[0];
	producer_mdatap = mp->m_mdatap[1];
	consumer_mdatap->m_syncp = &sync;
	producer_mdatap->m_syncp = &sync;

	DPRINTF(2, "ja_k_pc_err: consumer_mdatap=0x%p, producer_mdatap=0x%p, "
		"consumer_cpu=%d, producer_cpu=%d, &sync=0x%p\n",
		consumer_mdatap, producer_mdatap,
		consumer_mdatap->m_cip->c_cpuid,
		producer_mdatap->m_cip->c_cpuid, &sync);

	/*
	 * Start the producer.
	 */
	DPRINTF(2, "ja_k_pc_err: starting producer\n");
	if (memtest_start_thread(producer_mdatap, ja_producer, "ja_k_pc_err")
			!= 0) {
		cmn_err(CE_WARN, "memtet_start_thread: failed\n");
		return (-1);
	}

	/*
	 * Start the consumer.
	 */
	DPRINTF(2, "ja_k_pc_err: starting consumer\n");
	if (ja_consumer(consumer_mdatap) != 0) {
		cmn_err(CE_WARN, "ja_consumer: failed \n");
		return (-1);
	}

	return (0);
}

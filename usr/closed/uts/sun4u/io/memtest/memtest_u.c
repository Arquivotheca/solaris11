/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This is the sun4u portion of the CPU/Memory Error Injector driver.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtest_u.h>
#include <sys/memtest_sf.h>
#include <sys/memtest_ch.h>
#include <sys/memtest_chp.h>
#include <sys/memtest_jg.h>
#include <sys/memtest_ja.h>
#include <sys/memtest_oc.h>
#include <sys/memtest_pn.h>
#include <sys/memtest_sr.h>
#include <sys/memtest_cmp.h>

/*
 * Static routines located in this file.
 */

/*
 * The following definitions are used to decode the test command for
 * debug purposes. Note that these definitions must be kept in sync with
 * the command field definitions in both memtestio.h and memtestio_u.h.
 */
static	char	*sun4u_cputypes[]	= {"INVALID", "GENERIC", "SPITFIRE",
					"BLACKBIRD", "SAPHIRE", "HUMMINGBIRD",
					"CHEETAH", "CHEETAH+", "JALAPENO",
					"JAGUAR", "PANTHER", "SERRANO",
					"OLYMPUS_C", "JUPITER"};

static	char	*sun4u_classes[]	= {"INVALID", "MEM", "BUS", "DC",
					"IC", "IPB", "PC", "L2", "L2WB",
					"L2CP", "L3", "L3WB", "L3CP", "DTLB",
					"ITLB", "INT", "NULL", "NULL", "NULL",
					"NULL", "NULL", "NULL", "NULL", "NULL",
					"NULL", "NULL", "NULL", "NULL", "NULL",
					"NULL", "NULL", "UTIL"};

static	char	*sun4u_subclasses[]	= {"INVALID", "NONE", "IVEC", "DATA",
					"TAG", "MH", "IREG", "FREG", "STAG",
					"MTAG", "ADDR"};

static	char	*sun4u_traps[]		= {"INVALID", "NONE", "PRECISE",
					"DISRUPTING", "DEFERRED", "FATAL"};

static	char	*sun4u_prots[]		= {"INVALID", "NONE", "UE", "CE",
					"PE", "BE"};

static	char	*sun4u_modes[]		= {"INVALID", "NONE", "HYPR", "KERN",
					"USER", "DMA", "OBP"};

static	char	*sun4u_accs[]		= {"INVALID", "NONE", "LOAD", "BLOAD",
					"STORE", "FETCH", "PFETCH"};

static	char	*sun4u_miscs[]		= {"INVALID", "NONE", "COPYIN", "TL1",
					"DDIPEEK", "PHYS", "VIRT", "STORM",
					"ORPHAN", "PCR", "PEEK", "POKE",
					"NULL", "NULL", "NULL", "NULL",
					"NULL", "NULL", "NULL", "NULL",
					"NIMP/NSUP"};

/*
 * ***********************************************************************
 * The following block of routines are the sun4u high level test routines.
 * ***********************************************************************
 */

/*
 * P-Cache Parity Error routine.
 */
int
memtest_k_pcache_err(mdata_t *mdatap)
{
	int 	ret = 0;

	if (ret = OP_INJECT_PC(mdatap)) { /* inject P-Cache parity error */
		DPRINTF(0, "P-Cache parity injection failed!\n");
		return (ret);
	}

	return (ret);
}

/*
 * This routine injects an error into the data cache tag at the
 * physical offset specified in the mdata struct.
 */
int
memtest_dtphys(mdata_t *mdatap)
{
	return (memtest_cphys(mdatap, memtest_inject_dtphys, "dcache tags"));
}

/*
 * This routine injects an error into the instruction cache line
 * tags at the physical offset specified in the mdata struct.
 */
int
memtest_itphys(mdata_t *mdatap)
{
	return (memtest_cphys(mdatap, memtest_inject_itphys, "icache tag"));
}

/*
 * This routine injects an error into the L2 cache tag at the specified
 * physical offset without modifying the line state. This simulates
 * a real (random) error.
 */
int
memtest_l2tphys(mdata_t *mdatap)
{
	return (memtest_cphys(mdatap, memtest_inject_l2tphys, "l2cache tag"));
}

/*
 * *************************************************************************
 * The following block of routines are the sun4u second level test routines.
 * *************************************************************************
 */

/*
 * This routine injects an error into the d$ tags at a specified cache offset.
 */
int
memtest_inject_dtphys(mdata_t *mdatap)
{
	return (OP_INJECT_DTPHYS(mdatap));
}

/*
 * This routine injects an error into i$ tags at a specified cache offset.
 */
int
memtest_inject_itphys(mdata_t *mdatap)
{
	return (OP_INJECT_ITPHYS(mdatap));
}

/*
 * This routine injects an L2 cache tag error at a specified cache offset.
 */
int
memtest_inject_l2tphys(mdata_t *mdatap)
{
	return (OP_INJECT_L2TPHYS(mdatap));
}

/*
 * This routine injects a memory (DRAM) error via the processor specific
 * opsvec routine.
 *
 * Note that sun4u processors can't inject CEs at an arbitrary
 * memory address (only UEs) since the HW mechanism which is used to
 * inject the error writes out identical ECC throughout a cache line.
 * The only way to ensure a CE is to also have identical data
 * throughout the cache line.
 */
int
memtest_inject_memory(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		l2_size = mdatap->m_cip->c_l2_size;
	uint64_t	paddr_aligned;
	caddr_t		kvaddr_aligned;
	uint64_t	xorpat;
	uint_t		ecc;
	int		ret = 0;

	DPRINTF(3, "memtest_inject_memory: injecting memory error: "
	    "cpuid=%d, kvaddr=0x%08x.%08x, paddr=0x%08x.%08x\n",
	    getprocessorid(),
	    PRTF_64_TO_32((uint64_t)mdatap->m_kvaddr_c),
	    PRTF_64_TO_32(mdatap->m_paddr_c));

	/*
	 * Sanity check the kvaddr to paddr mapping.
	 */
	if ((mdatap->m_kvaddr_a == NULL) || (mdatap->m_kvaddr_c == NULL) ||
	    (memtest_kva_to_pa(mdatap->m_kvaddr_c) != mdatap->m_paddr_c)) {
		DPRINTF(0, "memtest_inject_memory: vaddr=0x%p does not map to "
		    "paddr=0x%llx\n",
		    mdatap->m_kvaddr_c, mdatap->m_paddr_c);
		return (EIO);
	}

	paddr_aligned = P2ALIGN(mdatap->m_paddr_c, 8);
	kvaddr_aligned = (caddr_t)P2ALIGN((uint64_t)mdatap->m_kvaddr_c, 8);

	/*
	 * Generate correct ecc for the data.
	 */
	ecc = OP_GEN_ECC(mdatap, paddr_aligned);

	if (ecc == (uint_t)-1) {
		DPRINTF(0, "memtest_inject_memory: ecc calculation failed\n");
		return (EIO);
	}

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	DPRINTF(3, "memtest_inject_memory: paddr_aligned=0x%llx, data=0x%llx, "
	    "ecc=0x%x, xorpat=0x%llx\n",
	    paddr_aligned, lddphys(paddr_aligned), ecc, xorpat);

	/*
	 * Either corrupt the data or check bits.
	 */
	if (F_CHKBIT(iocp))
		/* corrupt the check bits */
		ecc ^= xorpat;
	else
		/* corrupt the data */
		stdphys(paddr_aligned, (lddphys(paddr_aligned) ^ xorpat));

	DPRINTF(3, "memtest_inject_memory: corrupted data=0x%llx, ecc=0x%llx\n",
	    lddphys(paddr_aligned), ecc);

	DPRINTF(3, "memtest_inject_memory: calling processor specific memory\n"
	    "injection routine (paddr=0x%llx, kvaddr=0x%p, "
	    "l2_size=0x%x, ecc=0x%x)\n",
	    paddr_aligned, kvaddr_aligned, l2_size, ecc);

	/*
	 * Flushing caches keeps latency low when they are flushed
	 * again in lower level assembly routines.
	 */
	OP_FLUSHALL_CACHES(mdatap);

	/*
	 * Write out corrupted data/ecc generated above.
	 */
	if (ret = OP_INJECT_UMEMORY(mdatap, paddr_aligned, kvaddr_aligned,
	    ecc)) {
		DPRINTF(0, "memtest_inject_memory: processor specific memory "
		    "injection routine FAILED!\n");
	}

	if (ret) {
		return (ret);
	}

	/*
	 * Check ESRs for unexpected errors that may have occured
	 * as a result of injecting the error
	 */
	if (memtest_flags & MFLAGS_CHECK_ESRS_MEMORY_ERROR) {
		if (ret = OP_CHECK_ESRS(mdatap, "memtest_inject_memory")) {
			DPRINTF(0, "memtest_inject_memory: call to "
			    "OP_CHECK_ESRS FAILED!\n");
		}
	}

	return (ret);
}

/*
 * ***************************************************************
 * The following block of routines are the sun4u support routines.
 * ***************************************************************
 */

/*
 * This routine flushes the entire i-cache via a kernel routine call.
 */
int
gen_flushall_ic(cpu_info_t *cip)
{
	flush_instr_mem(0, (size_t)cip->c_ic_size);
	return (0);
}

/*
 * This routine flushes the entire L2 cache via a kernel routine call.
 */
/*ARGSUSED*/
int
gen_flushall_l2(cpu_info_t *cip)
{
	cpu_flush_ecache();
	return (0);
}

/*
 * This routine flushes a single i-cache entry (line) via a kernel routine call.
 */
int
gen_flush_ic_entry(cpu_info_t *cip, caddr_t vaddr)
{
	/*
	 * Align the vaddr with the cache linesize before call.
	 */
	vaddr = (caddr_t)P2ALIGN((uint64_t)vaddr, cip->c_ic_linesize);

	flush_instr_mem(vaddr, (size_t)cip->c_ic_linesize);
	return (0);
}

/*
 * This routine flushes a single L2 cache entry (line) via a call
 * to a memtest local routine.
 */
int
gen_flush_l2_entry(cpu_info_t *cip, caddr_t paddr)
{
	(void) gen_flush_l2((uint64_t)paddr, cip->c_l2_size);
	return (0);
}

/*
 * This routine handles sun4u architecture specific requests from userland.
 * Currently there are no such requests defined.
 */
/*ARGSUSED*/
int
memtest_arch_mreq(mem_req_t *mrp)
{
	return (EINVAL);
}

/*
 * This routine checks the AFSR(s) for non-zero values
 * and attempts to clear them if necessary.
 */
/*ARGSUSED*/
int
memtest_check_afsr(mdata_t *mdatap, char *msg)
{
	uint64_t	afsr = 0;
	uint64_t	afsr_ext = 0;
	uint64_t	afar = 0;
	int		have_afsr_ext;

	/*
	 * Can't just check the cip->c_cpuver because we are being
	 * cross-called and cip may not be valid for us (if this
	 * is a mixed CPU system).
	 */
	have_afsr_ext = (CPU_IMPL(memtest_get_cpu_ver()) == PANTHER_IMPL);

	if (have_afsr_ext)
		afsr_ext = memtest_get_afsr_ext();
	afsr = memtest_get_afsr();
	if (afsr | afsr_ext) {
		afar = memtest_get_afar();
		if (afsr) {
			cmn_err(CE_WARN, "%s: CPU%d AFSR is non-zero, "
			    "afsr=0x%08x.%08x, afar=0x%08x.%08x\n",
			    msg, getprocessorid(),
			    PRTF_64_TO_32(afsr), PRTF_64_TO_32(afar));
		}
		if (afsr_ext) {
			cmn_err(CE_WARN, "%s: CPU%d AFSR_EXT is non-zero, "
			    "afsr_ext=0x%08x.%08x, afar=0x%08x.%08x\n",
			    msg, getprocessorid(),
			    PRTF_64_TO_32(afsr_ext), PRTF_64_TO_32(afar));
		}

		if (!(memtest_flags & MFLAGS_DISABLE_ESR_CLEAR)) {
			cmn_err(CE_WARN, "%s: CPU%d Clearing AFSR(s)\n",
			    msg, getprocessorid());
			/*
			 * Have to clear afsr_ext first because errors
			 * in in it may prevent the synd field in the
			 * afsr from clearing.
			 */
			if (have_afsr_ext) {
				(void) memtest_set_afsr_ext(afsr_ext);
				afsr_ext = memtest_get_afsr_ext();
			}
			(void) memtest_set_afsr(afsr);
			afsr = memtest_get_afsr();
			if (afsr) {
				cmn_err(CE_WARN, "%s: CPU%d AFSR failed to "
				    "clear, afsr=0x%08x.%08x\n", msg,
				    getprocessorid(), PRTF_64_TO_32(afsr));
				return (EIO);
			}
			if (afsr_ext) {
				cmn_err(CE_WARN, "%s: CPU%d AFSR_EXT failed "
				    "to clear, afsr_ext=0x%08x.%08x\n",
				    msg, getprocessorid(),
				    PRTF_64_TO_32(afsr_ext));
				return (EIO);
			}
		}
	}
	return (0);
}

/*
 * This routine returns the index into the sun4u_miscs array
 * for a given command.
 */
int
memtest_conv_misc(uint64_t command)
{
	int misc = ERR_MISC(command);
	int mask = ERR_MISC_MASK;
	int nbits = 0, index = 0;

	while (mask) {
		nbits++;
		mask >>= 1;
	}

	while (misc && (index < nbits)) {
		index++;
		misc >>= 1;
	}
	return (index);
}

/*
 * This routine does a sanity check on the command definition to ensure
 * that no required bit-fields are blank.
 */
int
memtest_check_command(uint64_t command)
{
	/*
	 * First print out the command contents for DEBUG.
	 */
	DPRINTF(1, "memtest_check_command: command=0x%llx\n"
	    "\tcpu=%s, class=%s, subclass=%s, trap=%s\n"
	    "\tprot=%s, mode=%s, access=%s, misc=%s\n",
	    command,
	    sun4u_cputypes[ERR_CPU(command)],
	    sun4u_classes[ERR_CLASS(command)],
	    sun4u_subclasses[ERR_SUBCLASS(command)],
	    sun4u_traps[ERR_TRAP(command)],
	    sun4u_prots[ERR_PROT(command)],
	    sun4u_modes[ERR_MODE(command)],
	    sun4u_accs[ERR_ACC(command)],
	    sun4u_miscs[memtest_conv_misc(command)]);

	/*
	 * Ensure the contents are non-NULL for all required fields.
	 */
	if ((ERR_CLASS(command) == NULL) ||
	    (ERR_SUBCLASS(command) == NULL) ||
	    (ERR_PROT(command) == NULL) ||
	    (ERR_MODE(command) == NULL) ||
	    (ERR_ACC(command) == NULL) ||
	    (ERR_MISC(command) == NULL)) {
		DPRINTF(0, "memtest_check_command: invalid command 0x%llx\n",
		    command);
		return (EINVAL);
	}
	return (0);
}

/*
 * This routine calls the cpu specific initialization routine
 * which in turn fills in some pointers in the mdata_t struct.
 */
int
memtest_cpu_init(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	opsvec_c_t	*cops;
	opsvec_u_t	*uops;

	/*
	 * Call processor specific initialization routine to fill in the
	 * ops vector table and commands list in memtest data structure.
	 */
	switch (CPU_IMPL(cip->c_cpuver)) {
	case SPITFIRE_IMPL:
	case BLACKBIRD_IMPL:
	case SABRE_IMPL:
	case HUMMBRD_IMPL:
		sf_init(mdatap);
		break;
	case CHEETAH_IMPL:
		ch_init(mdatap);
		break;
	case CHEETAH_PLUS_IMPL:
		chp_init(mdatap);
		break;
	case JALAPENO_IMPL:
		ja_init(mdatap);
		break;
	case JAGUAR_IMPL:
		jg_init(mdatap);
		break;
	case PANTHER_IMPL:
		pn_init(mdatap);
		break;
	case SERRANO_IMPL:
		sr_init(mdatap);
		break;
	case OLYMPUS_C_IMPL:
	case JUPITER_IMPL:
		oc_init(mdatap);
		break;
	default:
		DPRINTF(0, "memtest_cpu_init: unsupported CPU type, "
		    "impl=0x%llx\n", CPU_IMPL(cip->c_cpuver));
		return (ENOTSUP);
	}

	/*
	 * Check that the ops vector tables and command list were filled in.
	 */
	if ((mdatap->m_copvp == NULL) || (mdatap->m_sopvp == NULL) ||
	    (mdatap->m_cmdpp == NULL)) {
		DPRINTF(0, "memtest_cpu_init: main memtest data structure "
		    "(mdata) failed to initialize properly!\n");
		return (EIO);
	}

	cops = mdatap->m_copvp;
	uops = mdatap->m_sopvp;

	/*
	 * Sanity check the ops vector functions to ensure all the required
	 * opsvecs are filled in.
	 */
	if ((uops->op_inject_memory == NULL) ||
	    (uops->op_gen_ecc == NULL)) {
		DPRINTF(0, "memtest_cpu_init: one or more required sun4u ops "
		    "vectors are NULL!\n");
		return (EIO);
	}

	if ((cops->op_inject_l2cache == NULL) ||
	    (cops->op_flushall_caches == NULL) ||
	    (cops->op_flushall_l2 == NULL) ||
	    (cops->op_flush_dc_entry == NULL) ||
	    (cops->op_flush_ic_entry == NULL)) {
		DPRINTF(0, "memtest_cpu_init: one or more required common ops "
		    "vectors are NULL!\n");
		return (EIO);
	}

	return (0);
}

/*
 * This routine returns a flag indicating whether the dcache
 * is currently enabled.
 */
int
memtest_dc_is_enabled(void)
{
	return ((memtest_get_dcucr() & DCU_DC) == DCU_DC);
}

/*
 * This routine disables the dcache.
 */
void
memtest_disable_dc(void)
{
	(void) memtest_set_dcucr(memtest_get_dcucr() & ~DCU_DC);
}

/*
 * This routine disables the icache.
 */
void
memtest_disable_ic(void)
{
	(void) memtest_set_dcucr(memtest_get_dcucr() & ~DCU_IC);
}

/*
 * This routine enables the dcache.
 */
void
memtest_enable_dc(void)
{
	(void) memtest_set_dcucr(memtest_get_dcucr() | DCU_DC);
}

/*
 * This routine enables the icache.
 */
void
memtest_enable_ic(void)
{
	(void) memtest_set_dcucr(memtest_get_dcucr() | DCU_IC);
}

/*
 * This routine fills in cpu specific information for the current cpu.
 */
int
memtest_get_cpu_info(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	int		ret;

	/*
	 * Sanity check.
	 */
	if (cip == NULL) {
		DPRINTF(0, "memtest_get_cpu_info: cip is NULL!\n");
		return (EIO);
	}

	/*
	 * Get some common registers.
	 */
	cip->c_cpuid = getprocessorid();
	cip->c_eer = memtest_get_eer();
	cip->c_dcr = memtest_get_dcr();
	cip->c_dcucr = memtest_get_dcucr();

	/*
	 * Call processor specific routine to get the rest of the info.
	 */
	if ((ret = OP_GET_CPU_INFO(mdatap)) != 0) {
		DPRINTF(0, "memtest_get_cpu_info: OP_GET_CPU_INFO() failed\n");
		return (ret);
	}

	/*
	 * Sanity check.
	 */
	if ((cip->c_dc_size == 0) || (cip->c_dc_linesize == 0) ||
	    (cip->c_dc_assoc == 0)) {
		DPRINTF(0, "memtest_get_cpu_info: NULL D$ info=\n");
		return (EIO);
	}
	if ((cip->c_ic_size == 0) || (cip->c_ic_linesize == 0) ||
	    (cip->c_ic_assoc == 0)) {
		DPRINTF(0, "memtest_get_cpu_info: NULL I$ info=\n");
		return (EIO);
	}
	if ((cip->c_l2_size <= 0) ||
	    ((cip->c_l2_size & ((256 * 1024) - 1)) != 0)) {
		DPRINTF(0, "memtest_get_cpu_info: invalid L2$ size 0x%x\n",
		    cip->c_l2_size);
		/*
		 * If not in debug mode return an error,
		 * else try to recover.
		 */
		if (!memtest_debug)
			return (EIO);
		else
			cip->c_l2_size = ecache_size;
	}

	return (0);
}

/*
 * This routine returns the cpu version for a sun4u CPU.
 */
uint64_t
memtest_get_cpu_ver(void)
{
	return (memtest_get_cpu_ver_asm());
}

/*
 * This routine returns a flag indicating whether the icache
 * is currently enabled.
 */
int
memtest_ic_is_enabled(void)
{
	return ((memtest_get_dcucr() & DCU_IC) == DCU_IC);
}

/*
 * This routine handles the memory request ioctl that requests a
 * physical address based on a cache index which has been passed in.
 *
 * The phys_avail memlist is searched for the first physical address that
 * maps to the requested cache index and that is also on a page that can be
 * subsequently acquired for use by the error injector.
 *
 * Knowledge of the index sizes for the supported processors and cache
 * types is required.  This information is stored in the cip struct.
 *
 * The type of cache that the index is used for is determined by
 * the mem_req subcommand.
 */
/*ARGSUSED*/
int
memtest_idx_to_paddr(memtest_t *memtestp, uint64_t *paddr1p, uint64_t *paddr2p,
		uint64_t req_cache_index, uint_t req_cache_way, int cache_type)
{
	extern struct memlist *phys_avail; /* total available physical memory */
	struct memlist *phys_mem;

	cpu_info_t	*cip = memtestp->m_mdatap[0]->m_cip; /* primary thr */
	uint_t		cache_size, cache_linesize, cache_assoc;
	uint64_t	req_paddr_bits;
	uint64_t	cache_index_mask, cache_max_index;
	uint64_t	paddr_ba, paddr;
	uint64_t	paddr_ba_index, paddr_index;
	uint64_t	found_paddr_start_masked, found_paddr_end_masked;
	page_t		*pp;
	char		*fname = "memtest_idx_to_paddr";

	DPRINTF(2, "%s: memtestp=0x%p, req_cache_index=0x%llx, "
	    "cache_type=%d\n", fname, memtestp, req_cache_index, cache_type);

	/*
	 * The subcommand is checked to determine the type of cache in order
	 * to fill in the cache attributes used below.
	 */
	switch (cache_type) {
	case ERR_CLASS_DC:
		cache_size = cip->c_dc_size;
		cache_linesize = cip->c_dc_linesize;
		cache_assoc = cip->c_dc_assoc;
		break;
	case ERR_CLASS_IC:
		cache_size = cip->c_ic_size;
		cache_linesize = cip->c_ic_linesize;
		cache_assoc = cip->c_ic_assoc;
		break;
	case ERR_CLASS_L2:
	case ERR_CLASS_L2WB:
	case ERR_CLASS_L2CP:
		cache_size = cip->c_l2_size;
		cache_linesize = cip->c_l2_linesize;
		cache_assoc = cip->c_l2_assoc;
		break;
	case ERR_CLASS_L3:
	case ERR_CLASS_L3WB:
	case ERR_CLASS_L3CP:
		cache_size = cip->c_l3_size;
		cache_linesize = cip->c_l3_linesize;
		cache_assoc = cip->c_l3_assoc;
		break;
	default:
		DPRINTF(0, "%s: only specific cache commands can use the "
		    "index option, this class = %d\n", fname, cache_type);
		*paddr1p = *paddr2p = -1;
		return (EINVAL);
	}

	/*
	 * Check that the requested index is valid for the cache type.
	 */
	cache_max_index = (cache_size / (cache_linesize * cache_assoc)) - 1;
	if (req_cache_index > cache_max_index) {
		DPRINTF(0, "%s: the requested index = 0x%llx is larger than "
		    "the max index for this cache type = 0x%llx\n", fname,
		    req_cache_index, cache_max_index);
		*paddr1p = *paddr2p = -1;
		return (EINVAL);
	}

	/*
	 * Calculate the cache index bitmask (to be applied to the
	 * physical address) based on the cache attributes.
	 */
	cache_index_mask = cache_max_index * cache_linesize;
	req_paddr_bits = req_cache_index * cache_linesize;

	DPRINTF(3, "%s: cache attributes are size=0x%llx, "
	    "max_index=0x%llx, cache_index_bitmask=0x%llx\n", fname,
	    cache_size, cache_max_index, cache_index_mask);

	/*
	 * Search phys_avail for a range of memory in which the specified
	 * index will fall between the start and end addresses.
	 * For each range found, iterate through the possible physical
	 * addresses that correspond to the requested cache index and
	 * return the first address that also falls on a page that
	 * is not long-term locked and will otherwise be subsequently
	 * available for use by the EI.
	 *
	 * XXX	accesses to phys_mem really should have some
	 *	mutex/locking around it.  This is because the available
	 *	mem is a moving target, might be ok for EI since we
	 *	lock it and make sure it's ours before it's ever used.
	 */
	for (phys_mem = phys_avail; phys_mem; phys_mem = phys_mem->ml_next) {

		/*
		 * Skip the memory range if its base address is zero.
		 */
		if ((phys_mem->ml_address == NULL) ||
		    (phys_mem->ml_size < MIN_DATABUF_SIZE)) {
			continue;
		}

		found_paddr_start_masked = phys_mem->ml_address &
		    cache_index_mask;
		found_paddr_end_masked = (phys_mem->ml_address +
		    phys_mem->ml_size - 1) & cache_index_mask;

		if (((req_paddr_bits >= found_paddr_start_masked) &&
		    (req_paddr_bits <= found_paddr_end_masked)) ||
		    (phys_mem->ml_size >= cache_size)) {

			DPRINTF(3, "%s: found candidate mem chunk "
			    "mem->addr=0x%llx, mem->next=0x%p, "
			    "mem->size=0x%llx, req_paddr_bits=0x%llx, "
			    "cache_index_mask=0x%llx, "
			    "found_paddr_start_masked=0x%llx, "
			    "found_paddr_end_masked=0x%llx\n",
			    fname, phys_mem->ml_address, phys_mem->ml_next,
			    phys_mem->ml_size, req_paddr_bits, cache_index_mask,
			    found_paddr_start_masked, found_paddr_end_masked);

			/*
			 * Determine the cache index that the found base
			 * address would land in.
			 */
			paddr_ba = phys_mem->ml_address;
			paddr_ba_index = (paddr_ba & cache_index_mask) /
			    cache_linesize;

			/*
			 * Determine the first exact address within the chunk
			 * of mem that will correspond to the requested cache
			 * index by adding an offset to the base address.
			 *
			 * If this chunk is larger than the cache_size,
			 * may need to add cache_size to paddr since the
			 * index of the chunks BA can be larger than the
			 * requested index.
			 */
			if (paddr_ba_index > req_cache_index) {
				paddr = paddr_ba + ((req_cache_index +
				    (cache_max_index + 1) - paddr_ba_index) *
				    (cache_linesize));
			} else {
				paddr = paddr_ba + ((req_cache_index -
				    paddr_ba_index) * (cache_linesize));
			}

			/*
			 * Check the page the physical address falls on and
			 * make sure it is not long-term locked before
			 * requesting it from the system.  Iterate through
			 * the candidate physical addresses in this memory
			 * range looking for first address that also falls
			 * on a page that the EI can use.
			 */
			for (;
			    paddr < (phys_mem->ml_address + phys_mem->ml_size);
			    paddr += cache_size) {

				if ((pp = page_numtopp_nolock(btop(
				    (size_t)paddr))) == NULL) {
					continue;
				}

				if (pp->p_vnode == &promvp) {
					continue;
				}

				if (PP_ISNORELOCKERNEL(pp)) {
					continue;
				}

				/*
				 * The EI will attempt to acquire this page
				 * later.  If it's free now, we reduce the
				 * chance of having to try to relocate the
				 * page later (via the physmem driver) which
				 * may fail, and could fail repeatedly if we
				 * keep picking the same page here.  If the
				 * page is allocated after this check and
				 * before we grab it, one only needs to rerun
				 * the test to look for another free page.
				 */
				if (!PP_ISFREE(pp)) {
					continue;
				}

				break;
			}

			/*
			 * If no suitable address was found, go to the next
			 * memory range.
			 */
			if (paddr >
			    (phys_mem->ml_address + phys_mem->ml_size)) {
				continue;
			}

			/*
			 * Otherwise the found address checks out.
			 */
			break;
		}
	}

	if (phys_mem == NULL) {
		DPRINTF(0, "%s: No usable physical address found in any "
		    "memory range that matches the requested cache "
		    "index (0x%llx)\n", fname, req_cache_index);
		*paddr1p = *paddr2p = -1;
		return (ENOMEM);
	}

	/*
	 * Verify that the index for the found paddr matches the
	 * requested index.
	 */
	paddr_index = (paddr & cache_index_mask) / cache_linesize;
	if (paddr_index != req_cache_index) {
		DPRINTF(0, "%s: ERROR: found paddr=0x%llx "
		    "(with index=0x%llx) does not match "
		    "requested cache index=0x%llx!\n",
		    fname, paddr, paddr_index, req_cache_index);
		*paddr1p = *paddr2p = -1;
		return (EFAULT);
	}

	DPRINTF(2, "%s: matched cache index=0x%llx to paddr=0x%llx\n",
	    fname, paddr_index, paddr);

	*paddr1p = *paddr2p = paddr;
	return (0);
}

/*
 * This routine does test initialization prior to a kernel level test.
 */
int
memtest_pre_test_kernel(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip = mdatap->m_cip;
	uint64_t	paddr;
	uint64_t	paddr_end;
	int		len;
	proc_t		*procp;
	uint64_t	*llptr;
	caddr_t		kvaddr, tmpvaddr;
	uint64_t	paddr_mask, buffer_data;
	int		i;
	char		*fname = "memtest_pre_test_kernel";

	DPRINTF(1, "%s: sun4u version\n", fname);

	/*
	 * If the user specified the use of a kernel allocated data
	 * buffer, allocate and map it here.  By default tests will
	 * use the buffer already allocated in userland, which will be
	 * mapped to here in kernel space.
	 *
	 * Note that the user allocated buffer must be used for tests where
	 * the user specified an address to be used for the buffer allocation
	 * since it has already been allocated and locked in userland.
	 *
	 * Also the user allocated buffer must be used for DMA (IO), USER,
	 * and copyin tests in order for them to work as expected.
	 */
	if (F_MAP_KERN_BUF(iocp)) {
		DPRINTF(2, "%s: allocating and mapping kernel buffer, "
		    "ignoring user buffer\n", fname);

		/*
		 * Allocate and use a new buffer in kernel space.
		 */
		mdatap->m_databuf = kmem_alloc(iocp->ioc_bufsize, KM_SLEEP);

		/*
		 * Find the paddr from the buffers kvaddr.
		 */
		if ((paddr = memtest_kva_to_pa((void *)mdatap->m_databuf))
		    == -1) {
			return (ENXIO);
		}

		/*
		 * Lock the kernel data buffer so it does not move.
		 */
		paddr_end = paddr + iocp->ioc_bufsize;
		(void) memtest_mem_request(mdatap->m_memtestp, &paddr,
		    &paddr_end, (iocp->ioc_bufsize / MMU_PAGESIZE),
		    MREQ_LOCK_PAGES);

		/*
		 * Set the correct attributes for the kernel buffer.
		 */
		if ((procp = ttoproc(curthread)) == NULL) {
			DPRINTF(0, "%s: NULL procp\n, fname");
			return (EIO);
		}

		rw_enter(&procp->p_as->a_lock, RW_READER);
		hat_chgattr(procp->p_as->a_hat, (caddr_t)mdatap->m_databuf,
		    iocp->ioc_bufsize, PROT_READ | PROT_WRITE | PROT_EXEC);
		rw_exit(&procp->p_as->a_lock);

		/*
		 * Initialize the data buffer to a debug friendly data pattern,
		 * that is different than the user and io buffer patterns.
		 */
		llptr = (void *)mdatap->m_databuf;
		paddr_mask = 0xffffffff & ~(cip->c_l2_sublinesize - 1);
		buffer_data = 0x0eccfeed00000000;
		for (i = 0; i < (iocp->ioc_bufsize / sizeof (uint64_t)); i++,
		    paddr += sizeof (uint64_t))
			llptr[i] = buffer_data | (paddr & paddr_mask);
	} else {
		/*
		 * Make sure that the user allocated the minimum space
		 * required for the data buffer.
		 */
		if (iocp->ioc_bufsize < MIN_DATABUF_SIZE) {
			DPRINTF(0, "%s: user buffer is too small, "
			    "size=0x%llx, minsize=0x%llx\n",
			    fname, iocp->ioc_bufsize, MIN_DATABUF_SIZE);
			return (EINVAL);
		}

		/*
		 * Setup a kernel virtual mapping to the user data buffer.
		 * Note that this call may lock the physical pages in memory.
		 */
		if ((mdatap->m_databuf = memtest_map_u2kvaddr(mdatap,
		    (caddr_t)iocp->ioc_databuf, 0, 0, iocp->ioc_bufsize))
		    == NULL) {
			DPRINTF(0, "%s: couldn't map user data buffer\n",
			    fname);
			return (ENXIO);
		}
	}

	/*
	 * Use the second half of the data buffer for copying/executing
	 * instructions to/from.
	 */
	mdatap->m_instbuf = mdatap->m_databuf + (iocp->ioc_bufsize / 2);

	DPRINTF(1, "%s: copying asm routines to 0x%p\n", fname,
	    mdatap->m_instbuf);

	/*
	 * Copy the assembly routines to the instruction buffer area
	 * so that they can have errors inserted into them and the
	 * system can recover from those errors.  The assembly
	 * routines are all less than 256 bytes in length.
	 */
	len = 256;
	tmpvaddr = mdatap->m_instbuf;
	bcopy((caddr_t)memtest_asmld, tmpvaddr, len);
	mdatap->m_asmld = (asmld_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_asmld_tl1, tmpvaddr, len);
	mdatap->m_asmld_tl1 = (asmld_tl1_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_asmldst, tmpvaddr, len);
	mdatap->m_asmldst = (asmldst_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_asmst_tl1, tmpvaddr, len);
	mdatap->m_asmst_tl1 = (asmst_tl1_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_blkld, tmpvaddr, len);
	mdatap->m_blkld = (blkld_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_blkld_tl1, tmpvaddr, len);
	mdatap->m_blkld_tl1 = (blkld_tl1_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)memtest_pcrel, tmpvaddr, len);
	mdatap->m_pcrel = (pcrel_t *)(tmpvaddr);

	/*
	 * Initialize the default virtual address to corrupt/access.
	 */
	if (ERR_MISC_ISVIRT(IOC_COMMAND(iocp))) {
		kvaddr = (caddr_t)(iocp->ioc_addr);
	} else {
		switch (ERR_ACC(IOC_COMMAND(iocp))) {
		case ERR_ACC_FETCH:
			if (ERR_MISC_ISTL1(IOC_COMMAND(iocp))) {
				kvaddr = (caddr_t)(mdatap->m_asmld_tl1);
			} else if (ERR_MISC_ISPCR(iocp->ioc_command)) {
				kvaddr = (caddr_t)(mdatap->m_pcrel);
			} else {
				if (ERR_CLASS_ISL2CP(IOC_COMMAND(iocp)) ||
				    ERR_CLASS_ISL3CP(IOC_COMMAND(iocp))) {
					kvaddr = (caddr_t)(mdatap->m_asmldst);
				} else {
					kvaddr = (caddr_t)(mdatap->m_asmld);
				}
			}
			break;
		default:
			kvaddr = mdatap->m_databuf;
			break;
		}
	}

	if ((paddr = memtest_kva_to_pa(kvaddr)) == -1)
		return (ENXIO);

	mdatap->m_kvaddr_a = kvaddr + memtest_get_a_offset(iocp);
	mdatap->m_kvaddr_c = kvaddr + memtest_get_c_offset(iocp);

	mdatap->m_paddr_a = paddr + memtest_get_a_offset(iocp);
	mdatap->m_paddr_c = paddr + memtest_get_c_offset(iocp);

	mdatap->m_uvaddr_a = (caddr_t)iocp->ioc_addr +
	    memtest_get_a_offset(iocp);
	mdatap->m_uvaddr_c = (caddr_t)iocp->ioc_addr +
	    memtest_get_c_offset(iocp);

	if (memtest_pre_init_threads(mdatap) != 0) {
		DPRINTF(0, "%s: failed to pre-initialize threads\n", fname);
		return (EIO);
	}

	if (memtest_init_threads(mdatap) != 0) {
		DPRINTF(0, "%s: failed to initialize threads\n", fname);
		return (EIO);
	}

	/*
	 * Bind the primary (consumer) thread now.
	 * Note that we don't really need to do this since we
	 * should already have been bound in user land.
	 */
	if (memtest_bind_thread(mdatap) != 0) {
		DPRINTF(0, "%s: failed to bind primary thread\n", fname);
		return (EIO);
	}

	/*
	 * Print addresses and contents of buffer(s) being used.
	 */
	if (F_VERBOSE(iocp) || (memtest_debug > 0)) {
		cmn_err(CE_NOTE, "%s: buffer addresses and data contents "
		    "being used in kernel mode:\n", fname);
		cmn_err(CE_NOTE, "memtest_asmld: kvaddr=0x%p "
		    "(*=0x%lx, paddr=0x%08x.%08x)\n",
		    (void *)memtest_asmld,
		    mdatap->m_asmld ? *(uint64_t *)memtest_asmld : 0,
		    PRTF_64_TO_32(mdatap->m_asmld ? \
		    memtest_kva_to_pa((void *)memtest_asmld) : 0));
		cmn_err(CE_NOTE, "memtest_asmld_tl1: kvaddr=0x%p "
		    "(*=0x%lx, paddr=0x%08x.%08x)\n",
		    (void *)memtest_asmld_tl1,
		    mdatap->m_asmld_tl1 ? *(uint64_t *)memtest_asmld_tl1 : 0,
		    PRTF_64_TO_32(mdatap->m_asmld_tl1 ? \
		    memtest_kva_to_pa((void *)memtest_asmld_tl1) : 0));
		cmn_err(CE_NOTE, "m_databuf: kvaddr=0x%p "
		    "(*=0x%lx, paddr=0x%08x.%08x)\n",
		    (void *)mdatap->m_databuf,
		    *((uint64_t *)mdatap->m_databuf),
		    PRTF_64_TO_32(memtest_kva_to_pa((void *)
		    mdatap->m_databuf)));
		cmn_err(CE_NOTE, "m_instbuf: kvaddr=0x%p "
		    "(*=0x%lx, paddr=0x%08x.%08x)\n",
		    (void *)mdatap->m_instbuf,
		    *((uint64_t *)mdatap->m_instbuf),
		    PRTF_64_TO_32(memtest_kva_to_pa((void *)
		    mdatap->m_instbuf)));
		cmn_err(CE_NOTE, "m_pcrel: kvaddr=0x%p "
		    "(*=0x%lx, paddr=0x%08x.%08x)\n",
		    (void *)mdatap->m_pcrel,
		    mdatap->m_pcrel ? *((uint64_t *)mdatap->m_pcrel) : 0,
		    PRTF_64_TO_32(mdatap->m_pcrel ? \
		    memtest_kva_to_pa((void *)mdatap->m_pcrel) : 0));
	}

	return (0);
}

/*
 * This routine does test initialization prior to a test which
 * has no mode.
 */
int
memtest_pre_test_nomode(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = iocp->ioc_addr;
	caddr_t		kvaddr;

	DPRINTF(1, "memtest_pre_test_nomode(): sun4u version\n");

	/*
	 * Only "phys" tests currently have an error mode of none.
	 */
	if (ERR_MISC(IOC_COMMAND(iocp)) != ERR_MISC_PHYS) {
		DPRINTF(0, "memtest_pre_test_nomode: ERR_MISC=0x%x "
		    "is not ERR_MISC_PHYS=0x%x\n",
		    ERR_MISC(IOC_COMMAND(iocp)), ERR_MISC_PHYS);
		return (EINVAL);
	}
	mdatap->m_paddr_a = paddr + memtest_get_a_offset(iocp);
	mdatap->m_paddr_c = paddr + memtest_get_c_offset(iocp);
	iocp->ioc_addr = paddr + memtest_get_a_offset(iocp);

	/*
	 * If this is the mphys command we must also initialize
	 * a kernel virtual address since some of the low-level
	 * injection routines may need it instead of a physical
	 * address.
	 */
	if (IOC_COMMAND(iocp) == G4U_MPHYS) {
		if (!pf_is_memory(paddr >> MMU_PAGESHIFT)) {
			DPRINTF(0, "memtest_pre_test_nomode: paddr=0x%llx "
			    "is not a valid memory address\n", paddr);
			return (ENXIO);
		}
		if ((kvaddr = memtest_map_p2kvaddr(mdatap, paddr, PAGESIZE,
		    0, HAT_LOAD_NOCONSIST)) == NULL) {
			DPRINTF(0, "memtest_pre_test_nomode: "
			    "memtest_map_p2kvaddr(paddr=0x%llx, "
			    "size=0x%x, attr=0x%x, flags=0x%x) failed\n",
			    paddr, PAGESIZE, 0, HAT_LOAD_NOCONSIST);
			return (ENXIO);
		}
		mdatap->m_kvaddr_a = kvaddr + memtest_get_a_offset(iocp);
		mdatap->m_kvaddr_c = kvaddr + memtest_get_c_offset(iocp);
	}

	return (0);
}

/*
 * This routine does test initialization prior to a user level test.
 */
int
memtest_pre_test_user(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr;
	caddr_t		kvaddr;
	char		*fname = "memtest_pre_test_user";

	DPRINTF(1, "%s: sun4u version\n", fname);

	if (F_MAP_KERN_BUF(iocp)) {
		DPRINTF(0, "%s: cannot use a kernel buffer for a "
		    "user mode test, defaulting to user buffer\n", fname);
	}

	if ((kvaddr = memtest_map_u2kvaddr(mdatap, (caddr_t)iocp->ioc_addr, 0,
	    0, PAGESIZE)) == NULL)
		return (-1);
	/*
	 * Find the paddr from the kvaddr.
	 */
	if ((paddr = memtest_kva_to_pa(kvaddr)) == -1)
		return (ENXIO);

	mdatap->m_kvaddr_a = kvaddr + memtest_get_a_offset(iocp);
	mdatap->m_kvaddr_c = kvaddr + memtest_get_c_offset(iocp);

	mdatap->m_paddr_a = paddr + memtest_get_a_offset(iocp);
	mdatap->m_paddr_c = paddr + memtest_get_c_offset(iocp);

	mdatap->m_uvaddr_a = (caddr_t)iocp->ioc_addr +
	    memtest_get_a_offset(iocp);
	mdatap->m_uvaddr_c = (caddr_t)iocp->ioc_addr +
	    memtest_get_c_offset(iocp);

	return (0);
}

/*
 * This routine is called after executing a test in kernel/obp mode.
 */
int
memtest_post_test_kernel(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr;
	int		ret = 0;
	char		*fname = "memtest_post_test_kernel";

	DPRINTF(1, "%s(mdatap=0x%p)\n", fname, mdatap);

	/*
	 * Free the kernel allocated data buffer for those tests which
	 * have allocated it.
	 */
	if (F_MAP_KERN_BUF(iocp) && (mdatap->m_databuf != NULL)) {
		/*
		 * Wait a bit for the error to propagate through the system
		 * so freeing the buffer does not trip the error again before
		 * it can be handled.
		 */
		delay(iocp->ioc_delay * hz);

		/*
		 * Determine the address to search for in the linked list.
		 */
		if ((paddr = memtest_kva_to_pa((void *)mdatap->m_databuf))
		    == -1) {
			return (paddr);
		}

		ret = memtest_free_kernel_memory(mdatap, paddr);
	}

	return (ret);
}

/*
 * Stub routine.  sun4u does not support this feature.
 */
/*ARGSUSED*/
int
memtest_is_local_mem(mdata_t *mdatap, uint64_t paddr)
{
	return (1);
}

/*
 * Stub routine.  sun4u does not support this feature.
 */
/*ARGSUSED*/
int
memtest_restore_scrubbers(mdata_t *mdatap)
{
	return (0);
}

/*
 * Stub routine.  sun4u does not support this feature.
 */
/*ARGSUSED*/
int
memtest_set_scrubbers(mdata_t *mdatap)
{
	return (0);
}

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file contains cheetah+ specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_chp.h>
#include <sys/memtest_u.h>
#include <sys/memtest_u_asm.h>
#include <sys/memtest_ch.h>
#include <sys/memtest_chp.h>
#include <sys/memtest_pn.h>

/*
 * Routines located in this file.
 */
	void	chp_init(mdata_t *);
static	int	chp_get_ec_shift(mdata_t *);
static	int	chp_no_refresh(mdata_t *);

/* snoop sync variable */
volatile int chp_dc_thread_snoop = 0;

/*
 * Cheetah+ operations vector tables.
 */
static opsvec_u_t cheetahplus_uops = {
	/* sun4u injection ops vectors */
	chp_write_dtphys,	/* corrupt d$ tag at offset */
	chp_write_itphys,	/* corrupt i$ tag at offset */
	chp_write_etphys,	/* corrupt e$ tag at offset */
	chp_write_memory,	/* corrupt memory */
	chp_write_mtag,		/* corrupt mtag */
	notsup,			/* p-cache error */

	/* sun4u support ops vectors */
	ch_gen_ecc_pa,		/* generate ecc for data at paddr */
};

static opsvec_c_t cheetahplus_cops = {
	/* common injection ops vectors */
	chp_write_dcache,	/* corrupt d$ at paddr */
	ch_write_dphys,		/* corrupt d$ at offset */
	notsup,			/* no corrupt fp reg */
	chp_write_icache,	/* corrupt i$ at paddr */
	chp_internal_err,	/* internal processor errors */
	ch_write_iphys,		/* corrupt i$ at offset */
	notsup,			/* no corrupt int reg */
	chp_write_ecache,	/* corrupt e$ at paddr */
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
	gen_flush_l2_entry,	/* flush l2$ entry - not used */
	notsup,			/* no flush l3$ entry */
};

cmd_t	cheetahp_cmds[] = {
	CHP_NO_REFSH,		memtest_internal_err,	"memtest_internal_err",
	CHP_EC_MH,		memtest_k_l2_err,	"memtest_k_l2_err",

	CHP_KD_DUE,		memtest_k_mem_err,	"memtest_k_mem_err",
	CHP_KD_ETSCE,		memtest_k_l2_err,	"memtest_k_l2_err",
	CHP_KD_ETSCETL1,	memtest_k_l2_err, 	"memtest_k_l2_err",
	CHP_KI_ETSCE,		memtest_k_l2_err, 	"memtest_k_l2_err",
	CHP_KI_ETSCETL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	CHP_UD_ETSCE,		memtest_u_l2_err,	"memtest_u_l2_err",
	CHP_UI_ETSCE,		memtest_u_l2_err,	"memtest_u_l2_err",
	CHP_KD_ETSUE,		memtest_k_l2_err,	"memtest_k_l2_err",
	CHP_KD_ETSUETL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	CHP_KI_ETSUE,		memtest_k_l2_err,	"memtest_k_l2_err",
	CHP_KI_ETSUETL1,	memtest_k_l2_err,	"memtest_k_l2_err",
	CHP_UD_ETSUE,		memtest_u_l2_err,	"memtest_u_l2_err",
	CHP_UI_ETSUE,		memtest_u_l2_err,	"memtest_u_l2_err",
	CHP_KD_ETHCE,		memtest_k_l2_err,	"memtest_k_l2_err",
	CHP_KI_ETHCE,		memtest_k_l2_err,	"memtest_k_l2_err",
	CHP_UD_ETHCE,		memtest_u_l2_err,	"memtest_u_l2_err",
	CHP_UI_ETHCE,		memtest_u_l2_err,	"memtest_u_l2_err",
	CHP_KD_ETHUE,		memtest_k_l2_err,	"memtest_k_l2_err",
	CHP_KI_ETHUE,		memtest_k_l2_err,	"memtest_k_l2_err",
	CHP_UD_ETHUE,		memtest_u_l2_err,	"memtest_u_l2_err",
	CHP_UI_ETHUE,		memtest_u_l2_err,	"memtest_u_l2_err",

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

static cmd_t *commands[] = {
	cheetahp_cmds,
	cheetah_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

void
chp_init(mdata_t *mdatap)
{
	mdatap->m_sopvp = &cheetahplus_uops;
	mdatap->m_copvp = &cheetahplus_cops;
	mdatap->m_cmdpp = commands;
}

/*
 * Calculate the appropiate ASI shift value for E$ diagnostic
 * registers depending on the size of the E$.
 */
static int
chp_get_ec_shift(mdata_t *mdatap)
{
	if (mdatap->m_cip->c_l2_size == EC_SIZE_1MB)
		return (EC_SHIFT_1MB);
	else if (mdatap->m_cip->c_l2_size == EC_SIZE_4MB)
		return (EC_SHIFT_4MB);
	else if (mdatap->m_cip->c_l2_size == EC_SIZE_8MB)
		return (EC_SHIFT_8MB);
	else {
		DPRINTF(0, "chp_get_ec_shift: invalid "
			"si_ec_size=0x%llx\n", mdatap->m_cip->c_l2_size);
		return (NULL);
	}
}

/*
 * This routine inserts and error into either the ecache, the
 * ecache tags or the ECC protecting the data or tags.
 */
int
chp_write_ecache(mdata_t *mdatap)
{
	uint64_t	paddr = mdatap->m_paddr_c;
	uint64_t	paddr_aligned, xorpat;
	ioc_t		*iocp = mdatap->m_iocp;
	int		reg_offset, ec_shift;
	int		wc_was_disabled = 0;
	int		ret = 0;

	paddr_aligned = P2ALIGN(paddr, 32);

	DPRINTF(3, "chp_write_ecache: mdatap=0x%p, paddr=0x%llx, "
		"paddr_aligned=0x%llx\n",
		mdatap, paddr, paddr_aligned);

	/*
	 * Touch the ecache data to get it into the modified state but only
	 * if this is not a UC? type of instruction error (e.g. kiucu, kiucc,
	 * kiucutl1, uiucu, and kicpu). This is done in order to verify that
	 * the kernel can recover from those errors.
	 */
	if ((ERR_ACC_ISFETCH(iocp->ioc_command) &&
	    ERR_TRAP_ISPRE(iocp->ioc_command)) ||
	    (ERR_ACC_ISPFETCH(iocp->ioc_command))) {
		DPRINTF(3, "\nchp_write_ecache: not modifying "
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
	 * Get the appropiate shift value for ASI access
	 */
	if ((ec_shift = chp_get_ec_shift(mdatap)) == NULL) {
		DPRINTF(0, "chp_write_ecache: couldn't determine correct ASI "
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
			chp_wr_ecache_tag_ecc(paddr_aligned,
					xorpat, ec_shift);
		} else {
			chp_wr_ecache_tag_data(paddr_aligned,
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

		ret = chp_wr_ecache(paddr_aligned, xorpat,
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
		DPRINTF(0, "chp_wr_ecache: TEST FAILED (0x%x) could not "
						"locate data in ecache\n", ret);
		return (ENXIO);
	} else {
		return (0);
	}
}

/*
 * This routine injects an error into the physical
 * tags of the ecache at the specified physical
 * offset.
 */
int
chp_write_etphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	xorpat, data;

	DPRINTF(3, "chp_write_etphys: iocp=0x%p, offset=0x%llx)\n",
			iocp, offset);

	/*
	 * For Ch+ we need to make sure that PA<23> is
	 * not set as this is interpreted as the disp_flush
	 * bit.
	 */
	offset = (offset & EC_TAG_MASK);

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

	DPRINTF(3, "chp_write_etphys: calling chp_wr_etphys(offset=0x%llx, "
		"xor=0x%llx, data=0x%llx)\n", offset, xorpat, data);

	/*
	 * Place an error into the e$ tag possibly modifying state.
	 */
	chp_wr_etphys(offset, xorpat, data);

	return (0);
}

/*
 * This routine injects an error into the dcache tags
 * at the specified physical offset.
 */
int
chp_write_dtphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	xorpat = 0;
	uint64_t	data = 0;

	DPRINTF(2, "\nchp_write_dtphys: iocp=0x%p, offset=0x%llx)\n",
				iocp, offset);

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * If the xor pattern is explicitly specified to be 0, then
	 * rather than corrupting the existing d$ data, it will be
	 * overwritten with this data.
	 */
	if (F_MISC1(iocp))
		data = iocp->ioc_misc1;

	if (!F_MISC1(iocp) && (xorpat == 0)) {
		DPRINTF(0, "chp_write_dtphys: non-zero xorpat must be"
			" specified\n");
		return (EFAULT);
	}

	/*
	 * Place an error into the d$.
	 */
	chp_wr_dtphys(offset, xorpat, data);

	return (0);
}

/*
 * This routine injects an error into the dcache.
 */
int
chp_write_dcache(mdata_t *mdatap)
{
	caddr_t	vaddr = mdatap->m_kvaddr_c;
	ioc_t	*iocp = mdatap->m_iocp;

	if (ERR_TRAP_ISPRE(iocp->ioc_command))
		return (chp_dc_data_err(mdatap, vaddr));
	else
		return (chp_dc_snoop_err(mdatap));
}

/*
 * This routine inserts an error into the dcache data
 * or physical tags.
 */
int
chp_dc_data_err(mdata_t *mdatap, caddr_t vaddr)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip = mdatap->m_cip;
	int		impl = CPU_IMPL(cip->c_cpuver);
	uint64_t	xorpat;
	uint64_t	tag_value;
	uint64_t	tag_addr, data_addr;
	uint64_t	paddr;
	int		ret_val;

	DPRINTF(2, "chp_dc_data_err: iocp=0x%p, vaddr=0x%llx\n", iocp, vaddr);

	/*
	 * Calculate physical address for tag comparison.
	 */
	if ((paddr = memtest_kva_to_pa(vaddr)) == (uint64_t)-1) {
		DPRINTF(0, "chp_dc_data_err: va_to_pa(0x%p) failed\n", vaddr);
		return (EFAULT);
	}

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * Dcache physical tag contains PA<41:13> in bits <29:1>. We
	 * shift the address so that PA<41:13> is located in <28:0>
	 * to make comparison in the asm routines easier.
	 *
	 * Format the address for way 0. The asm routines will adjust
	 * this for way 1 if needed.
	 */
	tag_value = ((paddr & DC_PA_MASK) >> DC_PA_SHIFT);
	tag_addr = DC_ASI_ADDR(0, vaddr);
	data_addr = DC_DATA_ADDR(0, vaddr);

	memtest_disable_intrs();

	if (ERR_SUBCLASS_ISDATA(iocp->ioc_command)) {
		/*
		 * dcache data
		 */
		if (F_CHKBIT(iocp)) {
			if (impl == PANTHER_IMPL) {
				/*
				 * PRM 10.7.1: Panther DC_parity = bits <7:0>
				 * Shift default(Ch+) pattern bits<15:7>
				 */
				xorpat = xorpat >> 8;
				ret_val = pn_wr_dcache_data_parity(tag_addr,
					data_addr, tag_value, xorpat, vaddr);
			} else
				ret_val = chp_wr_dcache_data_parity(tag_addr,
					tag_value, xorpat, vaddr);
		}
		else
			ret_val = chp_wr_dcache_data(tag_addr, data_addr,
					tag_value, xorpat, vaddr);
	} else {
		/*
		 * dcache physical tag
		 */
		ret_val = chp_wr_dcache_ptag(tag_addr, tag_value,
							xorpat, vaddr);
	}

	memtest_enable_intrs();

	/*
	 * Check the return value from the low level routine
	 * for possible error.
	 */
	if (ret_val == 0xfeccf) {
		DPRINTF(0, "chp_dc_data_err: TEST FAILED (0x%x) could not "
					"locate data in dcache\n", ret_val);

		return (ENXIO);
	} else {
		return (0);
	}
}

/*
 * This routine and chp_dc_snoop_err() work in concert to test
 * the detection and correction of a dcache snoop tag error. As
 * this type of error does not generate a trap we display the
 * contents of the cache to allow for visual verification of
 * correct behaviour.
 */
void
chp_dc_thread(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;

	DPRINTF(2, "chp_dc_thread: mdatap=0x%p, kvaddr_a=0x%p\n",
		mdatap, mdatap->m_kvaddr_a);

	/* Indicate to parent we are ready to snoop */
	*syncp = 1;

	/*
	 * Wait for OK to start snooping. Don't use
	 * normal thread sync methods as we need to keep
	 * the total code path as small as possible, and
	 * we also don't want the thread to block. If we
	 * timeout we signal the main thread to continue
	 * and then exit.
	 */
	if (memtest_wait_sync(syncp, 2, SYNC_WAIT_MAX, "chp_dc_thread") !=
	    SYNC_STATUS_OK) {
		chp_dc_thread_snoop = 0;
		thread_exit();
	}

	if (!F_NOERR(iocp)) {
		chp_wr_dcache_snoop(0x5555555555555555, mdatap->m_kvaddr_a);
	} else {
		DPRINTF(2, "chp_dc_thread: not invoking error(s)\n");
	}

	/* Tell parent we are finished. */
	chp_dc_thread_snoop = 0;

	thread_exit();
	/* NOTREACHED */
}

/*
 * This function corrupts a snoop tag in the dcache and
 * verifies that the eror has been correctly detected and
 * fixed.
 */
int
chp_dc_snoop_err(mdata_t *mdatap)
{
	memtest_t	*mp = mdatap->m_memtestp;
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip  = mdatap->m_cip;
	mdata_t		*snoop_mdatap;
	uint64_t	utag_val, tag_addr, xorpat, offset;
	uint64_t	data_asi_addr;
	caddr_t		data_ptr, start_addr;
	uint64_t	data[] = {0x1111111111111111, 0x2222222222222222,
					0x3333333333333333, 0x4444444444444444};
	int32_t		myid;
	cache_vals_t	dc_vals;
	kthread_id_t	tp;
	extern proc_t	p0;
	struct cpu	*snoop_cp = NULL;
	int		i, j = 0;
	int		found_invalid = 0;
	volatile int	sync;

	/*
	 * Sanity check.
	 */
	if (iocp->ioc_nthreads != 2) {
		DPRINTF(0, "chp_dc_snoop_err: nthreads=%d should be 2\n",
			iocp->ioc_nthreads);
		return (EIO);
	}

	snoop_mdatap = mp->m_mdatap[1];
	snoop_mdatap->m_syncp = &sync;

	/*
	 * Allocate a buffer which is twice the size of the D$.
	 */
	if ((data_ptr = kmem_alloc(mdatap->m_cip->c_dc_size * 2, KM_NOSLEEP))
							== NULL) {
		DPRINTF(0, "chp_dc_snoop_err: couldn't allocate data "
				"buffer\n");
		return (ENOMEM);
	}

	DPRINTF(2, "chp_dc_snoop_err: mdatap=0x%p, snoop_mdatap=0x%p, "
		"data_ptr=0x%p\n", mdatap, snoop_mdatap, data_ptr);

	start_addr = data_ptr;

	/*
	 * Store known data patterns into the data buffer at
	 * the correct offsets to land in each of the different
	 * ways of the dcache.
	 */
	offset = cip->c_dc_size/cip->c_dc_assoc;
	for (i = 0; i < 4; i++) {
		*(uint64_t *)data_ptr = data[i];
		data_ptr += offset;
	}

	/*
	 * Calculate the utag value for the line in way 0. This value
	 * is required to prime the cache line. See memtest_chp_asm.s
	 * for a more detailed explanation.
	 */
	utag_val = DC_UTAG_VAL((uint64_t)start_addr);

	/* tag asi addr */
	tag_addr = DC_ASI_ADDR(0, (uint64_t)start_addr);

	/* data asi addr */
	data_asi_addr = DC_DATA_ADDR(0, (uint64_t)start_addr);

	/* Address to which the other cpu will perform a write. */
	snoop_mdatap->m_kvaddr_a = (data_ptr - offset);

	/*
	 * Get the corruption pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	myid = getprocessorid();

	/*
	 * Create the snoop thread on the one other active cpu.
	 *
	 * Note that this test requires that the other (unused) cpus in
	 * the system be offlined prior to this test for reliability.
	 */
	mutex_enter(&cpu_lock);
	snoop_cp = cpu_get(snoop_mdatap->m_cip->c_cpuid);
	mutex_exit(&cpu_lock);

	tp = thread_create(NULL, PAGESIZE, chp_dc_thread,
		(caddr_t)snoop_mdatap, 0, &p0, TS_STOPPED, MAXCLSYSPRI-1);

	thread_lock(tp);
	tp->t_preempt = 1;
	tp->t_bound_cpu = snoop_cp;
	tp->t_affinitycnt = 1;
	tp->t_cpu = snoop_cp;
	thread_unlock(tp);

	/* Make the thread runnable */
	THREAD_SET_STATE(tp, TS_RUN, &transition_lock);
	CL_SETRUN(tp);
	thread_unlock(tp);

	/*
	 * Wait for snooping thread to indicate it's ready, but don't
	 * wait forever.
	 */
	DPRINTF(2, "chp_dc_snoop_err: starting snooping thread\n");
	if (memtest_wait_sync(&sync, 1, SYNC_WAIT_MAX, "chp_dc_snoop_err") !=
	    SYNC_STATUS_OK) {
		sync = -1;
		kmem_free(start_addr, mdatap->m_cip->c_dc_size * 2);
		return (EIO);
	}

	chp_dc_thread_snoop = 1;

	/*
	 * Load the 4 lines in the cache and corrupt one of the
	 * snoop tags.
	 */
	memtest_disable_intrs();
	chp_wr_dcache_stag(tag_addr, utag_val, (uint64_t)start_addr, xorpat);

	/*
	 * Due to the small size of the D$ we need to have as little code
	 * as possible between corrupting the snoop tag and re-reading the
	 * D$. Hence we place the timeout code in the snooping thread. If
	 * after a certain time interval the thread has not been signaled to
	 * do its work, it sets chp_dc_thread_snoop to zero (thus allowing
	 * the following code to complete), and exits.
	 */
	sync = 2;

	/* Wait for snoop thread to indicate it is finished */
	while (chp_dc_thread_snoop != 0)
		;

	/*
	 * Read the 4 lines back from the d$.
	 */
	(void) chp_rd_dcache(data_asi_addr, tag_addr, (uint64_t *)&dc_vals);

	/*
	 * Restart everything we had stopped, and free allocated mem.
	 */
	memtest_enable_intrs();
	kmem_free(start_addr, mdatap->m_cip->c_dc_size * 2);

	/*
	 * Display cache lines for visual inspection. At least one
	 * should now be invalid.
	 */
	for (i = 0; i < 4; i++) {
		cmn_err(CE_CONT, "[cpu %d][way %d] = 0x%llx : "
			"tag = 0x%llx : [%s]\n", myid, i,
			(long long)dc_vals.val[j],
			(long long)dc_vals.val[j+1],
			(dc_vals.val[j+1] & 0x1) ? "valid" : "invalid");

		if (!(dc_vals.val[j+1] & 0x1))
			found_invalid = 1;

		j += 2;
	}

	if (found_invalid == 0) {
		cmn_err(CE_WARN, "chp_dc_snoop_err: Test Failed. No invalid"
			" line found.\n");
		return (ENXIO);
	}

	return (0);
}

/*
 * This routine injects an error into the icache.
 */
int
chp_write_icache(mdata_t *mdatap)
{
	caddr_t	vaddr = mdatap->m_kvaddr_c;
	ioc_t	*iocp = mdatap->m_iocp;

	/* Currently only Panther has IPB parity */
	if (ERR_CLASS_ISIPB(IOC_COMMAND(iocp)))
		return (pn_ipb_err(mdatap));
	else if (ERR_TRAP_ISPRE(iocp->ioc_command))
		return (chp_ic_instr_err(mdatap, vaddr));
	else
		return (chp_ic_snoop_err(mdatap));
}

/*
 * This function inserts an error into the icache
 * instructions or physical tags.
 */
int
chp_ic_instr_err(mdata_t *mdatap, caddr_t vaddr)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip = mdatap->m_cip;
	uint64_t	instr_addr, tag_addr0, instr_addr0;
	uint64_t	instr_pa, xorpat;
	uint_t		myid;
	int		ret_val;
	int		impl = CPU_IMPL(cip->c_cpuver);

	instr_addr = (uint64_t)vaddr;

	/*
	 * Get the corruption pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	DPRINTF(2, "chp_ic_instr_err: instr_addr=0x%llx, "
			"va_2corrupt=0x%llx, xorpat=0x%llx impl=%x\n",
			instr_addr, mdatap->m_kvaddr_c, xorpat, impl);

	/*
	 * We calculate the VA for reading the tag and instr fields
	 * for way 0. The asm code starts at way 0 and increments
	 * itself through the 4 ways of the cache.
	 */
	tag_addr0 = IC_PTAG_ADDR(0, instr_addr, impl);
	instr_addr0 = IC_INSTR_ADDR(0, instr_addr, impl);

	/*
	 * Calculate the tag value we need to search for and
	 * format address to allow for easy comparison with value
	 * contained in physical tag.
	 */
	instr_pa = (mdatap->m_paddr_c & IC_PA_MASK) >> 13;

	DPRINTF(2, "chp/pn_ic_instr_err: tag_addr0=0x%llx, "
		"ic_addr0=0x%llx, instr_pa=0x%llx\n",
		tag_addr0, instr_addr0, instr_pa);

	memtest_disable_intrs();

	myid = getprocessorid();

	/*
	 * Cause the target instructions to be loaded into
	 * the Icache.
	 */
	if (ERR_MISC_ISTL1(iocp->ioc_command)) {
		xt_one(myid, (xcfunc_t *)mdatap->m_asmld_tl1,
				(uint64_t)mdatap->m_kvaddr_c, (uint64_t)0);
	} else {
		if (ERR_MISC_ISPCR(iocp->ioc_command))
			(mdatap->m_pcrel)();
		else
			(mdatap->m_asmld)(mdatap->m_kvaddr_c);
	}

	/*
	 * Inject the error.
	 */
	if (ERR_SUBCLASS_ISDATA(iocp->ioc_command)) {
		ret_val = chp_wr_icache_instr(tag_addr0, instr_addr0,
						instr_pa, xorpat, impl);
	} else {
		ret_val = chp_wr_icache_ptag(tag_addr0, instr_pa, xorpat, impl);
	}

	/*
	 * Re-execute the instruction to trigger the error. We do
	 * this here rather than in a higher level routine as we
	 * want to reduce the chance that the instruction will be
	 * displaced before we have a chance to re-execute it.
	 */
	if (ret_val != 0xfeccf) {
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)mdatap->m_asmld_tl1,
				(uint64_t)mdatap->m_kvaddr_c, (uint64_t)0);
		} else {
			if (ERR_MISC_ISPCR(iocp->ioc_command))
				(mdatap->m_pcrel)();
			else
				(mdatap->m_asmld)(mdatap->m_kvaddr_c);
		}
	}

	memtest_enable_intrs();

	/*
	 * Check the return value from the low level routine
	 * for possible error.
	 */
	if (ret_val == 0xfeccf) {
		DPRINTF(0, "chp_ic_instr_err: TEST FAILED (0x%x) "
				"unable to locate instruction in icache\n",
				ret_val);
		return (ENXIO);
	} else {
		return (0);
	}
}

/*
 * Size in bytes (rounded up to the nearest Icache line) of
 * chp_wr_icache_stag() routine in memtest_chp_asm.s. If that
 * routine is ever altered then this figure will need to be
 * changed also.
 */
#define	CHP_SNP_SZ	256

/*
 * This function corrupts a snoop tag in the icache and
 * verifies that the error has been correctly detected
 * and fixed.
 */
int
chp_ic_snoop_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip  = mdatap->m_cip;
	caddr_t		data_ptr1, data_ptr2, way0_addr, store_addr;
	uint64_t	tag0_addr, utag0_val, xorpat, offset;
	cache_vals_t	ic_vals;
	int		inj_cpu, i, j = 0;
	int		found_invalid = 0;
	uint64_t	tgt_offset, tgt_ln, end_line;
	uint64_t	way_size = mdatap->m_cip->c_ic_size;
	uint64_t	ic_ln_size = mdatap->m_cip->c_ic_linesize;
	int		impl = CPU_IMPL(cip->c_cpuver);

	/*
	 * Allocate the two buffers which will hold the instrs
	 * at specified offsets.
	 */
	if ((data_ptr1 = kmem_alloc(way_size * 5, KM_NOSLEEP)) == NULL) {
		DPRINTF(0, "chp_ic_snoop_err: couldn't allocate memory\n");
		return (ENOMEM);
	}

	if ((data_ptr2 = kmem_alloc(way_size * 5, KM_NOSLEEP)) == NULL) {
		DPRINTF(0, "chp_ic_snoop_err: couldn't allocate memory\n");
		kmem_free(data_ptr1, (way_size * 5));
		return (ENOMEM);
	}

	/*
	 * We need to ensure that the main Icache snooping routine
	 * chp_wr_icache_stag() doesn't clash with and overwrite the
	 * target routines. To this end we calculate which cache lines
	 * the main routine will occupy and adjust the target routines
	 * locations appropiately.
	 */
	if (impl == PANTHER_IMPL)
		end_line = (((((uint64_t)&pn_wr_icache_stag) + CHP_SNP_SZ)
						& PN_ICACHE_IDX_MASK) >> 6);
	else
		end_line = (((((uint64_t)&chp_wr_icache_stag) + CHP_SNP_SZ)
						& CH_ICACHE_IDX_MASK) >> 5);
	tgt_ln = (end_line + 1) % CHP_SNP_SZ;
	tgt_offset = tgt_ln * ic_ln_size;

	DPRINTF(3, "chp_ic_snoop_err: end_ln=%d, tgt_ln=%d, "
		"tgt_offset=0x%llx\n", end_line, tgt_ln, tgt_offset);

	way0_addr = data_ptr1;
	way0_addr += tgt_offset;

	/*
	 * Address to which we will perform a store to
	 * cause the snoop error to occur.
	 */
	store_addr = data_ptr2;
	store_addr += tgt_offset;

	/*
	 * Calculate the address of the physical tag for way 0 line.
	 * The asm routines will start at way 0 and increment through
	 * the other 3 ways.
	 */
	tag0_addr = IC_PTAG_ADDR(0, (uint64_t)way0_addr, impl);

	/*
	 * Calculate the utag value for the line in way0. This
	 * is used by the asm routines to 'prime' a particular
	 * cache line.
	 */
	utag0_val = IC_UTAG_VAL((uint64_t)way0_addr, impl);

	/*
	 * Copy target function into data buffer at correct offsets
	 * to map into each of the 4 ways of the cache.
	 */
	offset = cip->c_ic_size/cip->c_ic_assoc;
	for (i = 0; i < 4; i++) {
		bcopy((uint64_t *)chp_ic_stag_tgt,
			(way0_addr + (i * offset)), 32);
		DPRINTF(3, "chp_ic_snoop_err : [way %d] addr = 0x%llx\n",
			i, (uint64_t)(way0_addr + (i * offset)));
	}

	/*
	 * Get the corruption pattern
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * As diagnostic access to the I$ tags blocks all
	 * other access, we need to stop all the other cpus
	 * to prevent problems with the caches.
	 */
	inj_cpu = CPU->cpu_id;
	memtest_disable_intrs();

	/*
	 * Load the 4 instrs into the icache, corrupt one snoop tag
	 * perform a write to the snoop addr, then return the contents
	 * of the 4 cache lines for display.
	 */
	if (impl == PANTHER_IMPL)
		pn_wr_icache_stag((uint64_t)way0_addr, tag0_addr,
		    utag0_val, xorpat, (uint64_t)store_addr,
		    (uint64_t *)&ic_vals);
	else
		chp_wr_icache_stag((uint64_t)way0_addr, tag0_addr,
		    utag0_val, xorpat, (uint64_t)store_addr,
		    (uint64_t *)&ic_vals);

	/* restart everything we had stopped */
	memtest_enable_intrs();

	kmem_free(data_ptr1, (way_size * 5));
	kmem_free(data_ptr2, (way_size * 5));

	/*
	 * The only way to prove that the test worked is to display
	 * the contents/valid bits for the four lines. One of the lines
	 * should be invalid or have different contents to the others.
	 */
	for (i = 0; i < 4; i++) {
		cmn_err(CE_CONT, "[cpu %d][way %d] instr = 0x%llx : "
			"vtag = 0x%llx [%s]\n", inj_cpu, i,
			(long long)ic_vals.val[j],
			(long long)ic_vals.val[j+1],
			(ic_vals.val[j+1] & IC_TAG_VALID_MASK) ?
						"valid" : "invalid");

		if (!(ic_vals.val[j+1] & IC_TAG_VALID_MASK))
			found_invalid = 1;

		j += 2;
	}

	if (found_invalid == 0) {
		cmn_err(CE_WARN, "chp_ic_snoop_err: Test Failed. No"
			" invalid line found.\n");
		return (ENXIO);
	}

	return (0);
}

/*
 * This function inserts an error into the physical tag
 * at the specified offset within the icache.
 */
int
chp_write_itphys(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	offset = mdatap->m_iocp->ioc_addr;
	uint64_t	xorpat;
	uint64_t	tag = 0;

	DPRINTF(3, "\nchp_write_itphys: iocp=0x%p, offset=0x%llx)\n",
				iocp, offset);

	/*
	 * Get the corruption (xor) pattern.
	 */
	xorpat = IOC_XORPAT(iocp);

	/*
	 * If the xor pattern is explicitly specified to be 0, then
	 * rather than corrupting the existing i$ tag, it will be
	 * overwritten with the following.
	 */
	if (F_MISC1(iocp))
		tag = iocp->ioc_misc1;

	if (!F_MISC1(iocp) && (xorpat == 0)) {
		DPRINTF(0, "chp_write_itphys: non-zero xorpat must"
			" be specified\n");
		return (EFAULT);
	}

	memtest_disable_intrs();

	/*
	 * Insert the error.
	 */
	chp_wr_itphys(offset, xorpat, tag);

	memtest_enable_intrs();
	return (0);
}

/*
 * This function inserts an error into main memory.
 *
 * This function and chp_wr_mtag() use virtual addressing
 * unlike the other memory routines because the underlying
 * assembly routines use the floating point registers as a staging
 * area for the data rather than the E$. This is due to issues with
 * the way the displacement flush ASI works on Cheetah+. Whenever
 * that problem is resolved this code can revert back to using
 * physical addresses.
 */
int
chp_write_memory(mdata_t *mdatap, uint64_t paddr, caddr_t kvaddr, uint_t ecc)
{
	caddr_t		kvaddr_aligned;

	/*
	 * Make sure the address is properly aligned for
	 * use with the fp regs.
	 */
	kvaddr_aligned = (caddr_t)P2ALIGN((uint64_t)kvaddr, 64);

	DPRINTF(3, "chp_write_memory: mdatap=0x%p, paddr=0x%llx, kvaddr=0x%p, "
		"kvaddr_aligned=0x%p, ecc=0x%x\n",
		mdatap, paddr, kvaddr, kvaddr_aligned, ecc);

	/*
	 * Inject the error.
	 */
	return (chp_wr_memory(kvaddr_aligned, ecc));
}

/*
 * This routine corrupts the Mtag.
 */
int
chp_write_mtag(mdata_t *mdatap, uint64_t paddr, caddr_t kvaddr)
{
	uint64_t	paddr_aligned, xorpat;
	caddr_t		kvaddr_aligned;
	uint_t		ecc;
	cpu_info_t	*cip  = mdatap->m_cip;
	ioc_t		*iocp = mdatap->m_iocp;

	paddr_aligned = P2ALIGN(paddr, 8);
	kvaddr_aligned = (caddr_t)P2ALIGN((uint64_t)kvaddr, 64);

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

	DPRINTF(2, "chp_write_mtag: paddr_aligned=0x%llx, kvaddr=0x%p, "
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

	DPRINTF(2, "chp_write_mtag: calling\n"
			"\tchp_wr_mtag(vaddr=0x%p, ecc=0x%x)\n",
			kvaddr_aligned, ecc);

	/*
	 * Flushing caches keeps latency low when they are flushed
	 * again in lower level assembly routines.
	 */
	if (CPU_IMPL(cip->c_cpuver) == PANTHER_IMPL) {
		(void) pn_flushall_caches(cip);
	} else {
		(void) ch_flushall_caches(cip);
	}

	/*
	 * Write out corrupted data with ECC generated from above.
	 */
	(void) chp_wr_mtag(kvaddr_aligned, ecc);

	return (0);
}

/*
 * Internal Processor error handlers for Ch+ processors.
 */
int
chp_internal_err(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;

	if (iocp->ioc_command == CHP_NO_REFSH)
		return (chp_no_refresh(mdatap));
	else {
		DPRINTF(0, "chp_internal_err():Invalid command\n");
		return (EIO);
	}
}

/*
 * Induce a SDRAM Refresh Starvation Protocol error on Ch+ family of procs.
 * The Memory Controller Unit (MCU) Control register field rfr_int<11:3>
 * governs the RAS refresh interval in cpu cycles.
 * By setting this to a very low value (0x1), we basically tie up the
 * MCU by constantly refreshing the SDRAM banks, thereby leaving the MCU
 * no CPU cycles (the MCU is on the CPU on Ch+) to serve actual
 * memory requests to any SDRAM bank. This leads to to memory
 * starvation timeout == NO_REFSH:(IERR) Refresh starvation on an SDRAM bank
 * Internal error.
 *
 * NOTE: This does not set the AFAR.
 *	 It sets IERR in the AFSR and:
 *		EmuShadow<213> for Jag/Ch+
 *		EmuShadow<223> for Panther
 * So, this can only be discerned on platforms where the EMU shadow register
 * can be seen through the JTAG scan chain (typically through an SC).
 */
static int
chp_no_refresh(mdata_t *mdatap)
{
	uint64_t	mem_ctrl_reg = 0x00;
	uint64_t	mem_ctrl_reg1 = 0x00;
	caddr_t		kva = mdatap->m_kvaddr_a;
	int		i;

	mem_ctrl_reg = peek_asi64(ASI_MCU_CTRL, 0x0);
	DPRINTF(2, "chp_no_refresh: mem_ctrl_reg =%llx\n", mem_ctrl_reg);

	/*
	 * set mem_ctrl_reg rfr_int<11:3> to CHP_NO_REFSH_VAL(0x1).
	 */
	mem_ctrl_reg1 = (mem_ctrl_reg >> 12) << 9;	/* Clear <11:3> */
	mem_ctrl_reg1 = ((mem_ctrl_reg1 | CHP_NO_REFSH_VAL) << 3) |
			(mem_ctrl_reg & 0x7);
	DPRINTF(2, "chp_no_refresh: New starv mem_ctrl_reg = 0x%llx\n",
		mem_ctrl_reg1);

	/*
	 * Set new value.
	 */
	poke_asi64(ASI_MCU_CTRL, 0x0, mem_ctrl_reg1);

	/*
	 * Pound on memory to induce error.
	 * In theory this is needed; in practice reset happens by now.
	 * We use cas(X) to access memory directly.
	 */
	for (i = 0; i < 1024; i++)
		(void) cas64((uint64_t *)((kva + 64)), (uint64_t)kva,
		    *(kva + 128));

	/*
	 * FATAL reset should have happened by now.
	 * So, any return from this function signifies failure.
	 */
	return (EIO);
}

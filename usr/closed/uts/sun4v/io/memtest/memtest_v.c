/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This is the sun4v portion of the CPU/Memory Error Injector driver.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_v.h>
#include <sys/memtest_v.h>
#include <sys/memtest_ni.h>
#include <sys/memtest_n2.h>
#include <sys/memtest_vf.h>
#include <sys/memtest_kt.h>
#include <sys/memtest_v_asm.h>
#include <sys/memtest_n2_asm.h>
#include <sys/memtest_vf_asm.h>
#include <sys/memtest_kt_asm.h>

/*
 * Static routines located in this file.
 */
static uint64_t	memtest_hv_exec(char *, void *vaddr, uint64_t, uint64_t,
					uint64_t, uint64_t);

/*
 * The following definitions are used to decode the test command for
 * debug purposes. Note that these definitions must be kept in sync with
 * the command field definitions in both memtestio.h and memtestio_v.h.
 */
static	char	*sun4v_cputypes[]	= {"INVALID", "GENERIC", "NIAGARA",
					"NIAGARA-II", "VFALLS", "RFALLS"};

static	char	*sun4v_classes[]	= {"INVALID", "MEM", "BUS", "DC",
					"IC", "IPB", "PC", "L2", "L2WB",
					"L2CP", "L3", "L3WB", "L3CP", "DTLB",
					"ITLB", "INT", "SOC", "STB", "NULL",
					"NULL", "NULL", "NULL", "NULL", "NULL",
					"NULL", "NULL", "NULL", "NULL", "NULL",
					"NULL", "NULL", "UTIL"};

static	char	*sun4v_subclasses[]	= {"INVALID", "NONE", "IVEC", "DATA",
					"TAG", "MH", "IREG", "FREG", "L2VD",
					"L2UA", "MA", "CWQ", "MMU"};

static	char	*sun4v_traps[]		= {"INVALID", "NONE", "PRECISE",
					"DISRUPTING", "DEFERRED", "FATAL"};

static	char	*sun4v_prots[]		= {"INVALID", "NONE", "UE", "CE",
					"PE", "BE", "NOTDATA", "VALIDBIT",
					"ADDR_PE"};

static	char	*sun4v_modes[]		= {"INVALID", "NONE", "HYPR", "KERN",
					"USER", "DMA", "OBP", "UDMA"};

static	char	*sun4v_accs[]		= {"INVALID", "NONE", "LOAD", "BLOAD",
					"STORE", "FETCH", "PFETCH", "ASI",
					"ASR", "PCX", "SCRUB", "OP", "MAL",
					"MAS", "CWQ", "DTLB", "ITLB", "PRICE"};

static	char	*sun4v_miscs[]		= {"INVALID", "NONE", "COPYIN", "TL1",
					"DDIPEEK", "PHYS", "REAL", "VIRT",
					"STORM", "ORPHAN", "PCR", "PEEK",
					"POKE", "ENABLE", "FLUSH", "RAND",
					"PIO", "NULL", "NULL", "NULL",
					"NIMP/NSUP"};

/*
 * ***********************************************************************
 * The following block of routines are the sun4v high level test routines.
 * ***********************************************************************
 */

/*
 * This routine generates data cache error(s) in hypervisor mode.
 *
 * NOTE: the processor specific hypervisor routine that is called to inject
 *	 the error also invokes the error. This way there is little time
 *	 for the cache line to be evicted. Also whether this test supports the
 *	 NOERR option or not is up to the assembly routine.
 */
int
memtest_h_dc_err(mdata_t *mdatap)
{
	int		ret = 0;
	char		*fname = "memtest_h_dc_err";

	if (!F_FLUSH_DIS(mdatap->m_iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Call the processor specific hypervisor dcache inject routine.
	 */
	if (ret = OP_INJECT_HVDCACHE(mdatap)) {
		DPRINTF(0, "%s: call to OP_INJECT_HVDCACHE FAILED!\n", fname);
	}

	return (ret);
}

/*
 * This routine generates instruction cache error(s) in hypervisor mode.
 *
 * NOTE: the processor specific hypervisor routine that is called to inject
 *	 the error also invokes the error. This way there is little time
 *	 for the cache line to be evicted. Also whether this test supports the
 *	 NOERR option or not is up to the assembly routine.
 */
int
memtest_h_ic_err(mdata_t *mdatap)
{
	int		ret = 0;
	char		*fname = "memtest_h_ic_err";

	/*
	 * Call the processor specific hypervisor icache inject routine.
	 */
	if (ret = OP_INJECT_HVICACHE(mdatap)) {
		DPRINTF(0, "%s: call to OP_INJECT_HVICACHE FAILED!\n", fname);
	}

	return (ret);
}

/*
 * This routine generates text/data L2 cache error(s) in hypervisor mode.
 */
int
memtest_h_l2_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		err_acc = ERR_ACC(iocp->ioc_command);
	int		ret = 0;
	char		*fname = "memtest_h_l2_err";

	/*
	 * If STORE was specified change the MA access type.
	 */
	if (F_STORE(iocp) && (ERR_ACC(iocp->ioc_command) == ERR_ACC_MAL)) {
		DPRINTF(3, "%s: setting MA access type to STORE\n", fname);
		err_acc = ERR_ACC_MAS;
	}

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error by calling chain of memtest routines (ND or normal).
	 */
	if (ERR_PROT_ISND(iocp->ioc_command)) {
		if (ret = OP_INJECT_L2ND(mdatap)) {
			DPRINTF(0, "%s: processor specific l2cache NotData "
			    "injection routine FAILED!\n", fname);
			return (ret);
		}
	} else {
		if (ret = memtest_inject_l2cache(mdatap)) {
			return (ret);
		}
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (ret);
	}

	/*
	 * Otherwise invoke the error.
	 */
	switch (err_acc) {
	case ERR_ACC_LOAD:
		(void) memtest_hv_util("hv_paddr_load32",
		    (void *)hv_paddr_load32, mdatap->m_paddr_a, NULL,
		    NULL, NULL);
		break;
	case ERR_ACC_FETCH:
		(void) memtest_hv_util("hv_paddr_load64",
		    (void *)mdatap->m_asmld, mdatap->m_paddr_a, NULL,
		    NULL, NULL);
		break;
	case ERR_ACC_STORE:
		(void) memtest_hv_util("hv_paddr_store8",
		    (void *)hv_paddr_store8, mdatap->m_paddr_a,
		    (uchar_t)0xff, NULL, NULL);
		break;
	case ERR_ACC_MAL:	/* using polled mode */
		ret = OP_ACCESS_MA(mdatap, MA_OP_LOAD, (uint_t)0);
		break;
	case ERR_ACC_MAS:	/* using polled mode */
		ret = OP_ACCESS_MA(mdatap, MA_OP_STORE, (uint_t)0);
		break;
	case ERR_ACC_CWQ:	/* using polled mode */
		ret = OP_ACCESS_CWQ(mdatap, CWQ_OP_COPY, (uint_t)0);
		break;
	case ERR_ACC_PRICE:
		if (CPU_ISKT(mdatap->m_cip)) {
			ret = kt_flush_l2_entry_ice(mdatap->m_cip,
			    (caddr_t)mdatap->m_paddr_a);
		} else {
			ret = n2_flush_l2_entry_ice(mdatap->m_cip,
			    (caddr_t)mdatap->m_paddr_a);
		}
		break;
	default:
		DPRINTF(0, "%s: unsupported access type %d\n", fname, err_acc);
		ret = ENOTSUP;
		break;
	}

	return (0);
}

/*
 * This routine generates text/data L2 buffer cache error(s) in
 * hypervisor mode.  Buffer errors include:
 *	Directory errors
 *	fill buffer errors
 *	miss buffer errors
 *	write buffer errors
 *
 * Because of the small size of these buffers hypervisor mode errors
 * are triggered before reaching this routine so there is no access
 * section included here.
 */
int
memtest_h_l2buf_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		ret = 0;
	char		*fname = "memtest_h_l2buf_err";

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error via the opsvec routine.
	 */
	if (ret = OP_INJECT_L2DIR(mdatap)) {
		DPRINTF(0, "%s: OP_INJECT_L2DIR opsvec call "
		    "FAILED, ret = 0x%x\n", fname, ret);
		return (ret);
	}

	return (0);
}

/*
 * This routine generates text/data L2 cache V(U)AD error(s) in
 * hypervisor mode.
 */
int
memtest_h_l2vad_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		err_acc = ERR_ACC(iocp->ioc_command);
	int		ret = 0;
	char		*fname = "memtest_h_l2vad_err";

	/*
	 * If STORE was specified change the MA access type.
	 */
	if (F_STORE(iocp) && (ERR_ACC(iocp->ioc_command) == ERR_ACC_MAL)) {
		DPRINTF(3, "%s: setting MA access type to STORE\n", fname);
		err_acc = ERR_ACC_MAS;
	}

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error via the opsvec routine.
	 */
	if (ret = OP_INJECT_L2VAD(mdatap)) {
		DPRINTF(0, "%s: OP_INJECT_L2VAD opsvec call "
		    "FAILED, ret = 0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (0);
	}

	/*
	 * Otherwise invoke the error.
	 */
	switch (err_acc) {
	case ERR_ACC_LOAD:
		(void) memtest_hv_util("hv_paddr_load32",
		    (void *)hv_paddr_load32, mdatap->m_paddr_a, NULL,
		    NULL, NULL);
		break;
	case ERR_ACC_FETCH:
		(void) memtest_hv_util("hv_paddr_load64",
		    (void *)mdatap->m_asmld, mdatap->m_paddr_a, NULL,
		    NULL, NULL);
		break;
	case ERR_ACC_STORE:
		(void) memtest_hv_util("hv_paddr_store8",
		    (void *)hv_paddr_store8, mdatap->m_paddr_a,
		    (uchar_t)0xff, NULL, NULL);
		break;
	case ERR_ACC_MAL:	/* using polled mode */
		ret = OP_ACCESS_MA(mdatap, MA_OP_LOAD, (uint_t)0);
		break;
	case ERR_ACC_MAS:	/* using polled mode */
		ret = OP_ACCESS_MA(mdatap, MA_OP_STORE, (uint_t)0);
		break;
	case ERR_ACC_CWQ:	/* using polled mode */
		ret = OP_ACCESS_CWQ(mdatap, CWQ_OP_COPY, (uint_t)0);
		break;
	case ERR_ACC_PRICE:
		if (CPU_ISKT(mdatap->m_cip)) {
			ret = kt_flush_l2_entry_ice(mdatap->m_cip,
			    (caddr_t)mdatap->m_paddr_a);
		} else {
			ret = n2_flush_l2_entry_ice(mdatap->m_cip,
			    (caddr_t)mdatap->m_paddr_a);
		}
		break;
	default:
		DPRINTF(0, "%s: unsupported access type %d\n", fname, err_acc);
		ret = ENOTSUP;
		break;
	}

	return (0);
}

/*
 * This routine generates a L2 cache write-back error in hypervisor mode,
 * or for certain processor types in kernel mode.
 */
int
memtest_h_l2wb_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		ret;
	char		*fname = "memtest_h_l2wb_err";

	DPRINTF(2, "%s: corruption raddr=0x%llx, paddr=0x%llx\n",
	    fname, mdatap->m_raddr_c, mdatap->m_paddr_c);

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_L2_HVMODE(mdatap);
	}

	/*
	 * Inject the error by calling chain of memtest routines (ND or normal).
	 */
	if (ERR_PROT_ISND(iocp->ioc_command)) {
		if (ret = OP_INJECT_L2ND(mdatap)) {
			DPRINTF(0, "%s: processor specific l2cache NotData "
			    "injection routine FAILED!\n", fname);
			return (ret);
		}
	} else {
		if (ret = memtest_inject_l2cache(mdatap)) {
			return (ret);
		}
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (ret);
	}

	/*
	 * Check if this hyperpriv routine was called to produce a kernel mode
	 * write-back error, if so do a single entry kernel mode write-back.
	 */
	if (ERR_MODE_ISKERN(iocp->ioc_command)) {
		OP_FLUSH_L2_ENTRY(mdatap, (caddr_t)mdatap->m_raddr_a);
	} else {
		/*
		 * Flush the line from the L2-cache in hypervisor mode to
		 * produce a hyperpriv write-back error using either a
		 * displacement flush or the prefetch-ICE instr.
		 */
		if (ERR_ACC_ISPRICE(iocp->ioc_command) ||
		    ERR_MISC_ISPIO(iocp->ioc_command)) {
			if (CPU_ISKT(mdatap->m_cip)) {
				ret = kt_flush_l2_entry_ice(mdatap->m_cip,
				    (caddr_t)mdatap->m_paddr_a);
			} else {
				(void) n2_flush_l2_entry_ice(mdatap->m_cip,
				    (caddr_t)mdatap->m_paddr_a);
			}
		} else {
			OP_FLUSH_L2_ENTRY_HVMODE(mdatap,
			    (caddr_t)mdatap->m_paddr_a);
		}
	}

	return (ret);
}

/*
 * This routine generates a modular arithmetic parity error in hypervisor
 * context since the required registers are only available in hyperpriv mode.
 */
int
memtest_h_ma_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		err_acc = ERR_ACC(iocp->ioc_command);
	uint_t		acc_type;
	uint_t		intr_flag = 0;
	int		hvret;
	int		ret = 0;
	char		*fname = "memtest_h_ma_err";

	/*
	 * Determine the type of operation to use for error access, note
	 * that a LOAD will not invoke an error as per PRM definition.
	 *
	 * The flag values are used for the injection type, the access values
	 * are used for the access type (used in this routine).
	 */
	if (err_acc == ERR_ACC_STORE) {
		acc_type = MA_OP_STORE;
	} else if (err_acc == ERR_ACC_OP) {
		acc_type = MA_OP_MULT;
	} else {
		acc_type = MA_OP_LOAD;
	}

	/*
	 * If the IOCTL has a misc argument, use interrupt mode for the access.
	 */
	if (F_MISC1(iocp))
		intr_flag = 1;

	/*
	 * Inject an MA parity error via opsvec routine.
	 */
	if ((ret = OP_INJECT_MA(mdatap)) == -1) {
		DPRINTF(0, "%s:  OP_INJECT_MA opsvec call FAILED, ret = 0x%x\n",
		    fname, ret);
		return (ret);
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (ret);
	}

	/*
	 * Otherwise invoke the error via hypervisor routine.
	 *
	 * The intr_flag determines if the operation will be run in
	 * interrupt or polled mode.
	 */
	if ((hvret = OP_ACCESS_MA(mdatap, acc_type, intr_flag)) == -1) {
		DPRINTF(0, "%s:  OP_ACCESS_MA access call "
		    "FAILED, ret = 0x%x\n", fname, hvret);
	}

	return (ret);
}

/*
 * This routine generates a hypervisor mode memory error.
 */
int
memtest_h_mem_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		err_acc = ERR_ACC(iocp->ioc_command);
	int		ret = 0;
	char		*fname = "memtest_h_mem_err";

	/*
	 * If STORE was specified change the MA access type.
	 */
	if (F_STORE(iocp) && (ERR_ACC(iocp->ioc_command) == ERR_ACC_MAL)) {
		DPRINTF(3, "%s: setting MA access type to STORE\n", fname);
		err_acc = ERR_ACC_MAS;
	}

	/*
	 * Inject the error by calling processor specific memtest routines
	 * (either ND or normal).
	 */
	if (ERR_PROT_ISND(iocp->ioc_command)) {
		if (ret = OP_INJECT_MEMND(mdatap)) {
			DPRINTF(0, "%s: processor specific memory NotData "
			    "injection routine FAILED!\n", fname);
			return (ret);
		}
	} else {
		if (ret = memtest_inject_memory(mdatap)) {
			return (ret);
		}
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (ret);
	}

	/*
	 * Otherwise invoke the error.
	 */
	switch (err_acc) {
	case ERR_ACC_LOAD:
		(void) memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, mdatap->m_paddr_a, NULL,
		    NULL, NULL);
		break;
	case ERR_ACC_FETCH:
		(void) memtest_hv_util("hv_paddr_load64",
		    (void *)mdatap->m_asmld, mdatap->m_paddr_a, NULL,
		    NULL, NULL);
		break;
	case ERR_ACC_STORE:
		(void) memtest_hv_util("hv_paddr_store16",
		    (void *)hv_paddr_store16, mdatap->m_paddr_a, 0xff,
		    NULL, NULL);
		break;
	case ERR_ACC_MAL:	/* using polled mode */
		ret = OP_ACCESS_MA(mdatap, MA_OP_LOAD, (uint_t)0);
		break;
	case ERR_ACC_MAS:	/* using polled mode */
		ret = OP_ACCESS_MA(mdatap, MA_OP_STORE, (uint_t)0);
		break;
	case ERR_ACC_CWQ:	/* using polled mode */
		ret = OP_ACCESS_CWQ(mdatap, CWQ_OP_COPY, (uint_t)0);
		break;
	default:
		DPRINTF(0, "%s: unsupported access type %d\n",
		    fname, err_acc);
		ret = ENOTSUP;
	}

	return (ret);
}

/*
 * This routine generates a register file error in hypervisor mode.
 *
 * The other cpus in the processor/system should be paused since the
 * injection mechanism is implemented per core and not per virtual cpu,
 * other cpus on the same core can end up injecting the register
 * file error if they are left running.
 */
int
memtest_h_reg_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	maxoffset;
	uint64_t	offset;
	int		ret = 0;
	char		*fname = "memtest_h_reg_err";

	/*
	 * Get the offset to use for specific register access.
	 */
	if (ERR_SUBCLASS_ISIREG(iocp->ioc_command)) {
		maxoffset = IREG_MAX_OFFSET;
	} else {
		maxoffset = FREG_MAX_OFFSET;
	}

	offset = (F_MISC1(iocp) ? (iocp->ioc_misc1) : REG_DEFAULT_OFFSET);
	if ((offset <= 0) || (offset > maxoffset)) {
		DPRINTF(0, "%s: invalid offset argument using default 0x%x\n",
		    fname, REG_DEFAULT_OFFSET);
		offset = REG_DEFAULT_OFFSET;
	}

	/*
	 * Inject the error via appropriate opsvec routine.
	 */
	if (ERR_SUBCLASS_ISIREG(iocp->ioc_command)) {
		/*
		 * Inject an integer register file error.
		 */
		if ((ret = OP_INJECT_IREG(mdatap, offset)) == -1) {
			DPRINTF(0, "%s: OP_INJECT_IREG opsvec call FAILED, "
			    "ret = 0x%x\n", fname, ret);
			return (ret);
		}
	} else if (ERR_SUBCLASS_ISFREG(iocp->ioc_command)) {
		/*
		 * Inject a floating point register file error.
		 */
		if ((ret = OP_INJECT_FREG(mdatap, offset)) == -1) {
			DPRINTF(0, "%s: OP_INJECT_FREG opsvec call FAILED, "
			    "ret = 0x%x\n", fname, ret);
			return (ret);
		}
	}

	/*
	 * Only invoke the error if specified by the command.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (0);
	}

	/*
	 * Invoke only the FREG errors (via the appropriate register accesses)
	 * here since the IREG errors are invoked in the injection routine.
	 * The FREG tests could be made to match, but there is no need.
	 */
	if (ERR_SUBCLASS_ISFREG(iocp->ioc_command)) {
		if ((ret = OP_ACCESS_FREG(mdatap, offset)) != 0) {
			DPRINTF(0, "%s: OP_ACCESS_FREG opsvec call FAILED, "
			    "ret = 0x%x\n", fname, ret);
		}
	} else if (!ERR_SUBCLASS_ISIREG(iocp->ioc_command)) {
		DPRINTF(0, "%s: unsupported subclass 0x%x\n", fname,
		    ERR_SUBCLASS(iocp->ioc_command));
		ret = ENOTSUP;
	}

	return (0);
}

/*
 * This routine places an SP failed epacket on the resumable
 * error queue.
 *
 * The xorpat is used to determine the SP field of the epkt,
 * for the SP failed message the field is set to zero:
 *	0b00	SP is physically present but is faulted
 *		and currently unavailable
 *	0b01	SP is available
 *	0b10	SP is not physically present in the system
 */
int
memtest_h_spfail(mdata_t *mdatap)
{
	struct machcpu	*mcpup = &CPU->cpu_m;
	errh_er_t	epkt;
	uint64_t	epkt_pa;
	uint64_t	rq_base_pa;
	int		ret = 0;
	char		*fname = "memtest_h_spfail";

	bzero(&epkt, sizeof (errh_er_t));

	epkt.ehdl = 1;
	epkt.desc = ERRH_DESC_SP;
	epkt.attr = (IOC_XORPAT(mdatap->m_iocp) << ERRH_SP_SHIFT) &
	    ERRH_SP_MASK;

	epkt_pa = memtest_kva_to_ra(&epkt);
	epkt_pa = memtest_ra_to_pa(epkt_pa);

	rq_base_pa = memtest_ra_to_pa(mcpup->cpu_rq_base_pa);

	if ((ret = memtest_hv_util("hv_queue_resumable_epkt",
	    (void *)hv_queue_resumable_epkt, epkt_pa, rq_base_pa,
	    NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to hv_queue_resumable_epkt() "
		    "FAILED!\n", fname);
	}

	return (ret);
}

/*
 * This routine generates kernel text/data L2-cache NotData error(s).
 */
int
memtest_k_l2nd_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		err_acc = ERR_ACC(iocp->ioc_command);
	uint_t		myid = getprocessorid();
	int		ret = 0;
	char		*fname = "memtest_k_l2nd_err";

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error by calling processor specific routine(s).
	 */
	if (ret = OP_INJECT_L2ND(mdatap)) {
		DPRINTF(0, "%s: processor specific l2cache NotData "
		    "injection routine FAILED!\n", fname);
		return (ret);
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (ret);
	}

	/*
	 * Otherwise invoke the error.
	 */
	switch (err_acc) {
	case ERR_ACC_LOAD:
	case ERR_ACC_FETCH:
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)mdatap->m_asmld_tl1,
			    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
		} else {
			mdatap->m_asmld(mdatap->m_kvaddr_a);
		}
		break;
	case ERR_ACC_STORE:
		/*
		 * This store should get merged with the corrupted
		 * data injected above and cause a store merge error.
		 */
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)mdatap->m_asmst_tl1,
			    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0xff);
		} else {
			*mdatap->m_kvaddr_a = (uchar_t)0xff;
		}
		membar_sync();
		break;
	case ERR_ACC_PFETCH:
		memtest_prefetch_access(iocp, mdatap->m_kvaddr_a);
		DELAY(100);
		break;
	case ERR_ACC_BLOAD:
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)mdatap->m_blkld_tl1,
			    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
		} else {
			mdatap->m_blkld(mdatap->m_kvaddr_a);
		}
		break;
	default:
		DPRINTF(0, "%s: unsupported access type %d\n", fname, err_acc);
		ret = ENOTSUP;
	}

	return (ret);
}

/*
 * This routine generates a kernel text/data L2 directory error.
 *
 * NOTE: the access types used in this routine are ignored by Niagara since
 *	 dir errors are detected only by an always on hardware dir scrubber.
 *
 * NOTE: other processors come through this routine for other L2 buffer
 *	 errors as well.  In these cases the access routine is used.
 */
int
memtest_k_l2dir_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		err_acc = ERR_ACC(iocp->ioc_command);
	int		ret;
	char		*fname = "memtest_k_l2dir_err";

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error via the opsvec routine.
	 */
	if (ret = OP_INJECT_L2DIR(mdatap)) {
		DPRINTF(0, "%s: OP_INJECT_L2DIR opsvec call "
		    "FAILED, ret = 0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (ret);
	}

	/*
	 * Otherwise invoke the error.
	 */
	switch (err_acc) {
	case ERR_ACC_LOAD:
	case ERR_ACC_FETCH:
		mdatap->m_asmld(mdatap->m_kvaddr_a);
		break;
	case ERR_ACC_STORE:
		*mdatap->m_kvaddr_a = (uchar_t)0xff;
		membar_sync();
		break;
	default:
		DPRINTF(0, "%s: unsupported access type 0x%lx\n",
		    fname, err_acc);
		ret = ENOTSUP;
	}

	return (ret);
}

/*
 * This routine generates a kernel text/data L2 V(U)AD error.
 */
int
memtest_k_l2vad_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		err_acc = ERR_ACC(iocp->ioc_command);
	int		ret;
	char		*fname = "memtest_k_l2vad_err";

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error via the opsvec routine.
	 */
	if (ret = OP_INJECT_L2VAD(mdatap)) {
		DPRINTF(0, "%s: OP_INJECT_L2VAD opsvec call "
		    "FAILED, ret = 0x%x\n", fname, ret);
		return (ret);
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (0);
	}

	/*
	 * Otherwise invoke the error.
	 */
	switch (err_acc) {
	case ERR_ACC_LOAD:
	case ERR_ACC_FETCH:
		mdatap->m_asmld(mdatap->m_kvaddr_a);
		break;
	case ERR_ACC_STORE:
		*mdatap->m_kvaddr_a = (uchar_t)0xff;
		membar_sync();
		break;
	default:
		DPRINTF(0, "%s: unsupported access type %d\n", fname, err_acc);
		ret = ENOTSUP;
	}

	return (ret);
}

/*
 * This routine generates kernel text/data memory/DRAM and
 * L2-cache NotData error(s).
 */
int
memtest_k_nd_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint_t		err_acc = ERR_ACC(iocp->ioc_command);
	uint_t		myid = getprocessorid();
	int		ret = 0;
	char		*fname = "memtest_k_nd_err";

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error by calling processor specific routine(s).
	 */
	if (ERR_CLASS_ISMEM(iocp->ioc_command)) {
		if (ret = OP_INJECT_MEMND(mdatap)) {
			DPRINTF(0, "%s: processor specific memory NotData "
			    "injection routine FAILED!\n", fname);
			return (ret);
		}
	} else {
		if (ret = OP_INJECT_L2ND(mdatap)) {
			DPRINTF(0, "%s: processor specific l2cache NotData "
			    "injection routine FAILED!\n", fname);
			return (ret);
		}
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (ret);
	}

	/*
	 * Otherwise invoke the error.
	 */
	switch (err_acc) {
	case ERR_ACC_LOAD:
	case ERR_ACC_FETCH:
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)mdatap->m_asmld_tl1,
			    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
		} else {
			mdatap->m_asmld(mdatap->m_kvaddr_a);
		}
		break;
	case ERR_ACC_STORE:
		/*
		 * This store should get merged with the corrupted
		 * data injected above and cause a store merge error.
		 */
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)mdatap->m_asmst_tl1,
			    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0xff);
		} else {
			*mdatap->m_kvaddr_a = (uchar_t)0xff;
		}
		membar_sync();
		break;
	case ERR_ACC_PFETCH:
		memtest_prefetch_access(iocp, mdatap->m_kvaddr_a);
		DELAY(100);
		break;
	case ERR_ACC_BLOAD:
		if (ERR_MISC_ISTL1(iocp->ioc_command)) {
			xt_one(myid, (xcfunc_t *)mdatap->m_blkld_tl1,
			    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
		} else {
			mdatap->m_blkld(mdatap->m_kvaddr_a);
		}
		break;
	default:
		DPRINTF(0, "%s: unsupported access type %d\n", fname, err_acc);
		ret = ENOTSUP;
	}

	return (ret);
}

/*
 * This routine generates a kernel register file error.
 */
int
memtest_k_reg_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip = mdatap->m_cip;
	uint64_t	maxoffset;
	uint64_t	offset;
	int		ret = 0;
	char		*fname = "memtest_k_reg_err";

	/*
	 * Get the offset to use for specific register access.
	 */
	if (ERR_SUBCLASS_ISIREG(iocp->ioc_command)) {
		maxoffset = IREG_MAX_OFFSET;
	} else {
		maxoffset = FREG_MAX_OFFSET;
	}

	offset = (F_MISC1(iocp) ? (iocp->ioc_misc1) : REG_DEFAULT_OFFSET);
	if ((offset <= 0) || (offset > maxoffset)) {
		DPRINTF(0, "%s: invalid offset argument using default 0x%x\n",
		    fname, REG_DEFAULT_OFFSET);
		offset = REG_DEFAULT_OFFSET;
	}

	/*
	 * Inject the error via appropriate opsvec routine.
	 */
	if (ERR_SUBCLASS_ISIREG(iocp->ioc_command)) {
		/*
		 * Inject an integer register file error.
		 */
		if ((ret = OP_INJECT_IREG(mdatap, offset)) == -1) {
			DPRINTF(0, "%s: OP_INJECT_IREG opsvec call FAILED, "
			    "ret = 0x%x\n", fname, ret);
			return (ret);
		}
	} else if (ERR_SUBCLASS_ISFREG(iocp->ioc_command)) {
		/*
		 * Inject a floating point register file error.
		 */
		if ((ret = OP_INJECT_FREG(mdatap, offset)) == -1) {
			DPRINTF(0, "%s: OP_INJECT_FREG opsvec call FAILED, "
			    "ret = 0x%x\n", fname, ret);
			return (ret);
		}
	}

	/*
	 * N1, N2, and VF integer register injection implementations will
	 * have already invoked the error.
	 */
	if (ERR_SUBCLASS_ISIREG(iocp->ioc_command) &&
	    (CPU_ISNIAGARA(cip) || CPU_ISNIAGARA2(cip) || CPU_ISVFALLS(cip))) {
		return (ret);
	}

	/*
	 * Only invoke the error if specified by the command.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (ret);
	}

	/*
	 * Invoke the errors via the appropriate register accesses.
	 */
	if (ERR_SUBCLASS_ISIREG(iocp->ioc_command)) {
		if ((ret = OP_ACCESS_IREG(mdatap, offset)) != 0) {
			DPRINTF(0, "%s: OP_ACCESS_IREG opsvec call FAILED, "
			    "ret = 0x%x\n", fname, ret);
		}
	} else if (ERR_SUBCLASS_ISFREG(iocp->ioc_command)) {
		if ((ret = OP_ACCESS_FREG(mdatap, offset)) != 0) {
			DPRINTF(0, "%s: OP_ACCESS_FREG opsvec call FAILED, "
			    "ret = 0x%x\n", fname, ret);
		}
	} else {
		DPRINTF(0, "%s: unsupported subclass 0x%x\n",
		    fname, ERR_SUBCLASS(iocp->ioc_command));
		ret = ENOTSUP;
	}

	return (ret);
}

/*
 * This routine injects an L2 directory error using a byte offset.
 */
int
memtest_l2dir_phys(mdata_t *mdatap)
{
	/*
	 * Inject the error via the opsvec routine.
	 */
	return (memtest_cphys(mdatap, mdatap->m_sopvp->op_inject_l2dir,
	    "l2cache dir"));
}

/*
 * This routine injects an L2 VA(U)D error at a byte offset.
 */
int
memtest_l2vad_phys(mdata_t *mdatap)
{
	/*
	 * Inject the error via the opsvec routine.
	 */
	return (memtest_cphys(mdatap, mdatap->m_sopvp->op_inject_l2vad,
	    "l2cache VAD"));
}

/*
 * This routine generates a user text/data L2 directory error.
 */
int
memtest_u_l2dir_err(mdata_t *mdatap)
{
	int	ret = 0;
	char	*fname = "memtest_u_l2dir_err";

	/*
	 * Inject the error via the opsvec routine.
	 */
	if (ret = OP_INJECT_L2DIR(mdatap)) {
		DPRINTF(0, "%s: OP_INJECT_L2DIR opsvec call FAILED, "
		    "ret = 0x%x\n", fname, ret);
	}

	return (ret);
}

/*
 * This routine generates a user text/data L2 V(U)AD error.
 */
int
memtest_u_l2vad_err(mdata_t *mdatap)
{
	int	ret = 0;
	char	*fname = "memtest_u_l2vad_err";

	/*
	 * Inject the error via the opsvec routine.
	 */
	if (ret = OP_INJECT_L2VAD(mdatap)) {
		DPRINTF(0, "%s: OP_INJECT_L2VAD opsvec call FAILED, "
		    "ret = 0x%x\n", fname, ret);
	}

	return (ret);
}

/*
 * This routine injects a memory or L2 cache NotData error at the
 * specified user virtual address.  The user program is responsible
 * for invoking the error in user mode.
 */
int
memtest_u_nd_err(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;
	int	ret;
	char	*fname = "memtest_u_nd_err";

	/*
	 * For sun4v the data which is already in the cache must be
	 * flushed out so it can be brought in again AFTER DM mode
	 * has been enabled so it can be found in the expected way.
	 */
	if (!F_FLUSH_DIS(mdatap->m_iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error by calling processor specific routine(s).
	 */
	if (ERR_CLASS_ISMEM(iocp->ioc_command)) {
		if (ret = OP_INJECT_MEMND(mdatap)) {
			DPRINTF(0, "%s: processor specific memory NotData "
			    "injection routine FAILED!\n", fname);
			return (ret);
		}
	} else {
		if (ret = OP_INJECT_L2ND(mdatap)) {
			DPRINTF(0, "%s: processor specific l2cache "
			    "NotData injection routine FAILED!\n", fname);
			return (ret);
		}
	}

	return (0);
}

/*
 * This routine injects an I/D-TLB error at the specified user
 * virtual address.
 */
int
memtest_u_tlb_err(mdata_t *mdatap)
{
	int	ret;
	char	*fname = "memtest_u_tlb_err";

	DPRINTF(2, "%s: injecting user TLB error\n", fname);

	/*
	 * Inject the TLB error (without using memtest_u_cmn_err).
	 */
	ret = OP_INJECT_TLB(mdatap);

	return (ret);
}

/*
 * *************************************************************************
 * The following block of routines are the sun4v second level test routines.
 * *************************************************************************
 */

/*
 * This routine injects a memory (DRAM) error via a processor specific
 * opsvec routine.
 */
int
memtest_inject_memory(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		ret = 0;
	char		*fname = "memtest_inject_memory";

	DPRINTF(3, "%s: injecting memory error on "
	    "cpuid=%d, kvaddr=0x%08x.%08x, raddr=0x%08x.%08x, "
	    "paddr=0x%08x.%08x\n", fname,
	    getprocessorid(),
	    PRTF_64_TO_32((uint64_t)mdatap->m_kvaddr_c),
	    PRTF_64_TO_32(mdatap->m_raddr_c),
	    PRTF_64_TO_32(mdatap->m_paddr_c));

	/*
	 * Sanity check the kvaddr to raddr mapping, but skip this for
	 * injections that do not allocate a buffer.
	 */
	if (!ERR_MISC_ISLOWIMPACT(iocp->ioc_command)) {
		if ((mdatap->m_kvaddr_a == NULL) ||
		    (mdatap->m_kvaddr_c == NULL) ||
		    (memtest_kva_to_ra(mdatap->m_kvaddr_c) !=
		    mdatap->m_raddr_c)) {
			DPRINTF(0, "%s: kernel vaddr=0x%p does not "
			    "map to raddr in mdata struct raddr=0x%llx\n",
			    fname, mdatap->m_kvaddr_c, mdatap->m_raddr_c);
			return (EIO);
		}
	}

	/*
	 * Flushing caches here keeps latency low when they are flushed
	 * again in lower level routines.
	 */
	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject and access (if req'd) the error via the opsvec routine.
	 */
	if (ret = OP_INJECT_VMEMORY(mdatap)) {
		DPRINTF(0, "%s: processor specific memory "
		    "injection routine FAILED!\n", fname);
		return (ret);
	}

	/*
	 * Check the ESRs for any unexpected error return.
	 */
	if (memtest_flags & MFLAGS_CHECK_ESRS_MEMORY_ERROR) {
		if (ret = OP_CHECK_ESRS(mdatap, fname)) {
			DPRINTF(0, "%s: call to OP_CHECK_ESRS FAILED!\n",
			    fname);
		}
	}

	return (ret);
}

/*
 * ***************************************************************
 * The following block of routines are the sun4v support routines.
 * ***************************************************************
 */

/*
 * Handle sun4v architecture specific requests from userland.
 */
int
memtest_arch_mreq(mem_req_t *mrp)
{
	char	*fname = "memtest_arch_mreq";

	switch (mrp->m_cmd) {
	case MREQ_RA_TO_PA:
		DPRINTF(1, "%s: raddr=0x%llx\n", fname, mrp->m_vaddr);
		mrp->m_paddr1 = memtest_ra_to_pa(mrp->m_vaddr);
		if (mrp->m_paddr1 == -1) {
			DPRINTF(0, "%s: couldn't translate user "
			    "kernel real address 0x%llx\n",
			    fname, mrp->m_vaddr);
			return (ENXIO);
		}
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

/*
 * This routine does a peek or poke to the specified asi
 * register at a specific virtual address via the hypervisor.
 *
 * If the xorpat is non-zero the poke command will ignore
 * the specified data to write and will read the contents,
 * modify with the xorpat and write them back.  Simple
 * non-directed injections can be achieved this way.
 */
int
memtest_asi_peekpoke(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		asi = (int)iocp->ioc_addr;
	uint64_t	xor = iocp->ioc_xorpat;
	uint64_t	vaddr = iocp->ioc_misc1;
	uint64_t	data = iocp->ioc_misc2;
	uint64_t	ret;
	char		*fname = "memtest_asi_peekpoke";

	if (F_VERBOSE(iocp)) {
		cmn_err(CE_NOTE, "%s: will perform %s to asi=0x%x, "
		    "vaddr=0x%08x.%08x, using xorpat=0x%lx, "
		    "data=0x%08x.%08x\n", fname,
		    (ERR_MISC_ISPEEK(iocp->ioc_command) ? "peek" : "poke"),
		    asi, PRTF_64_TO_32(vaddr), xor, PRTF_64_TO_32(data));
	}

	if (ERR_MISC_ISPEEK(iocp->ioc_command)) {

		ret = memtest_hv_util("hv_asi_load64",
		    (void *)hv_asi_load64, asi, vaddr, NULL, NULL);
		cmn_err(CE_NOTE, "%s: asi 0x%x, at vaddr=0x%08x.%08x "
		    "contents are 0x%08x.%08x\n", fname, asi,
		    PRTF_64_TO_32(vaddr), PRTF_64_TO_32(ret));

	} else if (ERR_MISC_ISPOKE(iocp->ioc_command)) {

		/*
		 * If an xor argument is specified and non-zero
		 * then do not simply write to the ASI, do a
		 * read, modify with xor, store sequence instead.
		 */
		if (IOC_XORPAT(iocp)) {
			data = memtest_hv_util("hv_asi_load64",
			    (void *)hv_asi_load64, asi, vaddr, NULL, NULL);
			data ^= xor;
		}

		ret = memtest_hv_util("hv_asi_store64",
		    (void *)hv_asi_store64, asi, vaddr, data, NULL);
		cmn_err(CE_NOTE, "%s: wrote data=0x%08x.%08x to "
		    "asi 0x%x, at vaddr=0x%08x.%08x\n", fname,
		    PRTF_64_TO_32(data), asi, PRTF_64_TO_32(vaddr));

	} else {
		cmn_err(CE_NOTE, "%s: invalid misc type for asi peek/poke!\n",
		    fname);
		return (EINVAL);
	}

	return (0);
}

/*
 * This routine does a sanity check on the command definition to ensure
 * that no required bit-fields are blank.
 */
int
memtest_check_command(uint64_t command)
{
	char	*fname = "memtest_check_command";

	/*
	 * First print out the command contents for DEBUG.
	 */
	DPRINTF(1, "%s: command=0x%llx\n"
	    "\tcpu=%s, class=%s, subclass=%s, trap=%s\n"
	    "\tprot=%s, mode=%s, access=%s, misc=%s\n",
	    fname, command,
	    sun4v_cputypes[ERR_CPU(command)],
	    sun4v_classes[ERR_CLASS(command)],
	    sun4v_subclasses[ERR_SUBCLASS(command)],
	    sun4v_traps[ERR_TRAP(command)],
	    sun4v_prots[ERR_PROT(command)],
	    sun4v_modes[ERR_MODE(command)],
	    sun4v_accs[ERR_ACC(command)],
	    sun4v_miscs[memtest_check_misc(command)]);
	/*
	 * Ensure the contents are non-NULL for all required fields.
	 */
	if ((ERR_CLASS(command) == NULL) ||
	    (ERR_SUBCLASS(command) == NULL) ||
	    (ERR_PROT(command) == NULL) ||
	    (ERR_MODE(command) == NULL) ||
	    (ERR_ACC(command) == NULL) ||
	    (ERR_MISC(command) == NULL)) {
		DPRINTF(0, "%s: invalid command 0x%llx\n",
		    fname, command);
		return (EINVAL);
	}
	return (0);
}

/*
 * This routine returns the index into the sun4v_miscs array
 * for a given command.  This is required for the encoded
 * misc value(s) because the misc field is encoded as a bit
 * field and not a sequential set of values.  If any other
 * command fields are changed to be bit-fields a similar
 * operation will need to be performed.
 *
 * XXX	this routine only finds the first misc field set
 *	even if multiple misc bits are set.  It is essentially
 *	the same as the memtest_conv_misc() routine in the
 *	memtest_u.c file that was added by OPL.  Really both
 *	routines should be combined into one and moved to memtest.c,
 *	and the ability to return a list of indexes should be added.
 */
int
memtest_check_misc(uint64_t command)
{
	uint64_t	misc = ERR_MISC(command);
	uint64_t	mask;
	uint64_t	nbits;
	int		i;

	/*
	 * First count the number of bits set in the field.
	 */
	for (mask = ERR_MISC_MASK, nbits = 0; mask > 0; nbits++, mask >>= 1)
		;

	/*
	 * Then determine which misc index should be returned.
	 */
	for (i = 0; misc && (i < nbits); i++, misc >>= 1)
		;

	return (i);
}

/*
 * The following two functions are stub routines for sun4v so that
 * common (sun4) code can be used for both sun4u and sun4v.
 *
 * If CMP/sibling/core/strand quiescing is required for sun4v in
 * future it can be added to these routines.
 */
/*ARGSUSED*/
int
memtest_cmp_quiesce(mdata_t *mdatap)
{
	return (0);
}

/*ARGSUSED*/
int
memtest_cmp_unquiesce(mdata_t *mdatap)
{
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
	opsvec_v_t	*vops;
	char		*fname = "memtest_cpu_init";

	/*
	 * Call processor specific initialization routine to fill in the
	 * ops vector table and commands list in memtest data structure.
	 */
	switch (CPU_IMPL(cip->c_cpuver)) {
	case NIAGARA_IMPL:
		ni_init(mdatap);
		break;
	case NIAGARA2_IMPL:
		if (CPU_ISVFALLS(cip)) {
			vf_init(mdatap);
		} else {
			n2_init(mdatap);
		}
		break;
	case KT_IMPL:
		kt_init(mdatap);
		break;
	default:
		DPRINTF(0, "%s: unsupported sun4v CPU type, impl=0x%llx, "
		    "mask=0x%lx\n", fname, CPU_IMPL(cip->c_cpuver),
		    CPU_MASK(cip->c_cpuver));
		return (ENOTSUP);
	}

	/*
	 * Check that the ops vector tables and command list were filled in.
	 */
	if ((mdatap->m_copvp == NULL) || (mdatap->m_sopvp == NULL) ||
	    (mdatap->m_cmdpp == NULL)) {
		DPRINTF(0, "%s: main memtest data structure "
		    "(mdata) failed to initialize properly!\n", fname);
		return (EIO);
	}

	cops = mdatap->m_copvp;
	vops = mdatap->m_sopvp;

	/*
	 * Sanity check the ops vector functions to ensure all the required
	 * opsvecs are filled in.
	 */
	if ((vops->op_inject_memory == NULL)) {
		DPRINTF(0, "%s: one or more required sun4v ops "
		    "vectors are NULL!\n", fname);
		return (EIO);
	}
	if ((cops->op_inject_dcache == NULL) ||
	    (cops->op_inject_icache == NULL) ||
	    (cops->op_inject_l2cache == NULL) ||
	    (cops->op_enable_errors == NULL) ||
	    (cops->op_flushall_caches == NULL) ||
	    (cops->op_flushall_l2 == NULL) ||
	    (cops->op_get_cpu_info == NULL)) {
		DPRINTF(0, "%s: one or more required common ops "
		    "vectors are NULL!\n", fname);
		return (EIO);
	}

	return (0);
}

/*
 * This routine fills in cpu specific information for the current cpu
 * in the cpu_info struct (mdatap->m_cip).
 */
int
memtest_get_cpu_info(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	int		ret = 0;
	char		*fname = "memtest_get_cpu_info";

	/*
	 * Sanity check.
	 */
	if (cip == NULL) {
		DPRINTF(0, "%s: cip is NULL!\n", fname);
		return (EIO);
	}

	/*
	 * Get some common registers.
	 */
	cip->c_cpuid = getprocessorid();

	/*
	 * Call processor specific routine to get the rest of the info.
	 */
	if ((ret = OP_GET_CPU_INFO(mdatap)) != 0) {
		DPRINTF(0, "%s: OP_GET_CPU_INFO() FAILED!\n", fname);
		return (ret);
	}

	/*
	 * Sanity check.
	 * Maybe this should go into the OPS VEC also.
	 */
	if ((cip->c_dc_size == 0) || (cip->c_dc_linesize == 0) ||
	    (cip->c_dc_assoc == 0)) {
		DPRINTF(0, "%s: NULL D$ info=\n", fname);
		return (EIO);
	}
	if ((cip->c_ic_size == 0) || (cip->c_ic_linesize == 0) ||
	    (cip->c_ic_assoc == 0)) {
		DPRINTF(0, "%s: NULL I$ info=\n", fname);
		return (EIO);
	}
	if ((cip->c_l2_size <= 0) ||
	    ((cip->c_l2_size & ((256 * 1024) - 1)) != 0)) {
		DPRINTF(0, "%s: invalid L2$ size 0x%x\n", fname,
		    cip->c_l2_size);
		return (EIO);
	}

	return (ret);
}

/*
 * This routine returns the cpu version for a sun4v CPU.  The version reg
 * is hyperprivileged so a trap to a hypervisor routine is required.
 */
uint64_t
memtest_get_cpu_ver(void)
{
	uint64_t	ret;
	char		*fname = "memtest_get_cpu_ver";

	if ((ret = memtest_hv_util("memtest_get_cpu_ver_asm",
	    (void *)memtest_get_cpu_ver_asm,
	    NULL, NULL, NULL, NULL)) == -1) {
		DPRINTF(0, "%s: trap to memtest_get_cpu_ver_asm() "
		    "FAILED!\n", fname);
	}

	DPRINTF(3, "%s: memtest_get_cpu_ver_asm returned hver=0x%lx\n",
	    fname, ret);
	return (ret);
}

/*
 * Based on the corruption paddr the DRAM channel to use for the injection
 * must be determined, this is complicated by the HW architecture.
 *
 * A check is done to see if the channel that would normally handle the
 * paddr is in the disabled state.  If it is, the DRAM injection
 * register to use must be changed in order to use the correct one.
 *
 * For N2 normally the set of DRAM registers is dependent on paddr[8:7]
 * but in one of the 2-channel modes the mapping relies only on bit 7
 * (if there is only one bank/channel active then there is no check).
 * VF is similar to N2 but uses only paddr[8] to choose between its
 * two DRAM registers (one for each branch).
 *
 * Addresses for disabled channels are handled by using the L2-cache
 * offsets (L2 banks are hard wired to DRAM branches) as follows:
 *
 * Note that shifts are required because the DRAM registers have
 * a branch stride of 0x1000 (4096).
 *
 * The KT/RF PRM puts it this way:
 * MCU selection within KT is done using PA[6] in all interleaving modes.
 * But Configuration space for MCU is selected using PA[12] with mapping
 * PA[12]=0 or 1 selects the MCU0 or MCU1 configuration space.
 *
 * N2 channel description:
 * paddr[8:6]		normal (4 br)	disabled (2 br)
 * ====================================================
 * 0x000 and 0x040	0		-> 2
 * 0x080 and 0x0c0	1		-> 3
 * 0x100 and 0x140	2		-> 0
 * 0x180 and 0x1c0	3		-> 1
 *
 * VF channel description:
 * paddr[8]	   normal (2 br)   disabled (1 br)
 * ====================================================
 * 0x000		0		-> 1
 * 0x100		1		-> 0
 *
 * KT/RF channel description:
 * paddr[6]	   normal (2 br)   disabled (1 br/MCU)
 * ====================================================
 * 0x000		0		-> 1
 * 0x040		1		-> 0
 *
 * This routine uses the L2 enabled registers to determine if
 * the DRAM banks are available in order to work around a HW bug where
 * the processor will hang if any registers are used from a disabled
 * DRAM bank (including the bank disabled regs, brilliant as that is).
 *
 * NOTE: this routine currently handles N2, VF, and KT/RF processors.
 *
 * NOTE: KT/RF also has to check the "plane flip" value to choose
 *	 which set of MCU registers to use.  If "plane flip" is set
 *	 PA[6] is flipped.
 */
uint64_t
memtest_get_dram_bank_offset(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	uint64_t	dram_bank_offset = 0;
	uint64_t	bank_compare;
	uint64_t	l2_bank_enabled_val;
	uint64_t	l2_bank_enabled_reg;
	uint64_t	l2_bank_enabled_mask;
	uint64_t	l2_plane_flip_val;
	char		*fname = "memtest_get_dram_bank_offset";

	if (CPU_ISKT(cip)) {
		dram_bank_offset = (mdatap->m_paddr_c & KT_DRAM_BRANCH_MASK) <<
		    KT_DRAM_BRANCH_PA_SHIFT;
		l2_bank_enabled_reg = KT_L2_BANK_EN_FULL;
		l2_bank_enabled_mask = KT_L2_BANK_EN_ALLEN;
	} else if (CPU_ISVFALLS(cip)) {
		dram_bank_offset = (mdatap->m_paddr_c & VF_DRAM_BRANCH_MASK) <<
		    VF_DRAM_BRANCH_PA_SHIFT;
		l2_bank_enabled_reg = N2_L2_BANK_EN_STATUS_FULL;
		l2_bank_enabled_mask = N2_L2_BANK_EN_STATUS_ALLEN;
	} else { /* default to N2 */
		dram_bank_offset = (mdatap->m_paddr_c & N2_DRAM_BRANCH_MASK) <<
		    N2_DRAM_BRANCH_PA_SHIFT;
		l2_bank_enabled_reg = N2_L2_BANK_EN_STATUS_FULL;
		l2_bank_enabled_mask = N2_L2_BANK_EN_STATUS_ALLEN;
	}

	l2_bank_enabled_val = memtest_hv_util("hv_paddr_load64",
	    (void *)hv_paddr_load64, l2_bank_enabled_reg,
	    NULL, NULL, NULL);
	l2_bank_enabled_val &= l2_bank_enabled_mask;

	/*
	 * Ensure the bank/branch we want to use is available, if not
	 * (bit not set) then choose fallback bank/branch instead.
	 */
	if (CPU_ISKT(cip)) {
		/*
		 * KT/RF has a "plane flip" feature that sends
		 * accesses from one MCU to the other.  If this bit is
		 * set then the other MCU register set must be used.
		 */
		l2_plane_flip_val = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, KT_SYS_MODE_REG,
		    NULL, NULL, NULL);
		l2_plane_flip_val &= KT_SYS_MODE_PFLIP;

		if (l2_plane_flip_val) {
			dram_bank_offset ^= (0x40 << KT_DRAM_BRANCH_PA_SHIFT);
		}

		bank_compare = dram_bank_offset >> 12;

		/*
		 * The check is complicated for KT/RF because of the
		 * increased number of L2 banks, though there are only
		 * two DRAM branches (MCUs).
		 *
		 * Refer to table 23-36 in the KT/RF PRM (version 0.4)
		 * for details (note that only a few configs are valid).
		 */
		if (((l2_bank_enabled_val & 0x55) == 0x0) &&
		    (bank_compare == 0)) { /* PA[6] clear but only MCU1 avail */
			dram_bank_offset ^= (0x40 << KT_DRAM_BRANCH_PA_SHIFT);
			DPRINTF(2, "%s: KT DRAM MCU %d disabled, using regs "
			    "for MCU %d (bank_enabled=0x%llx)\n", fname,
			    bank_compare, (dram_bank_offset >> 12),
			    l2_bank_enabled_val);
		} else if (((l2_bank_enabled_val & 0xaa) == 0x0) &&
		    (bank_compare != 0)) { /* PA[6] set but only MCU0 avail */
			dram_bank_offset ^= (0x40 << KT_DRAM_BRANCH_PA_SHIFT);
			DPRINTF(2, "%s: KT DRAM MCU %d disabled, using regs "
			    "for MCU %d (bank_enabled=0x%llx)\n", fname,
			    bank_compare, (dram_bank_offset >> 12),
			    l2_bank_enabled_val);
		}
	} else if (CPU_ISVFALLS(cip)) {
		bank_compare = dram_bank_offset >> 11;
		if (((l2_bank_enabled_val >> bank_compare) & 0x3) == 0) {
			dram_bank_offset ^= (0x100 << VF_DRAM_BRANCH_PA_SHIFT);
			DPRINTF(2, "%s: VF DRAM channel %d disabled, using "
			    "regs for channel %d\n", fname, bank_compare,
			    (dram_bank_offset >> 11));
		}
	} else { /* default to N2 */
		bank_compare = dram_bank_offset >> 12;
		if (((l2_bank_enabled_val >> bank_compare) & 0x1) == 0) {
			dram_bank_offset ^= (0x100 << N2_DRAM_BRANCH_PA_SHIFT);
			DPRINTF(2, "%s: DRAM channel %d disabled, using regs "
			    "for channel %d\n", fname, bank_compare,
			    (dram_bank_offset >> 12));

			/*
			 * Go through the same procedure again in case the new
			 * bank/branch also disabled (only one enabled).
			 */
			bank_compare = dram_bank_offset >> 12;
			if (((l2_bank_enabled_val >> bank_compare) & 0x1)
			    == 0) {
				dram_bank_offset ^= (0x80 <<
				    N2_DRAM_BRANCH_PA_SHIFT);
				DPRINTF(2, "%s: DRAM channel %d also disabled,"
				    " using regs for channel %d\n", fname,
				    bank_compare, (dram_bank_offset >> 12));
			}
		}
	}

	return (dram_bank_offset);
}

/*
 * This routine is used to ensure that the hypervisor running on the system
 * has the HV_EXEC trap enabled so the injector can access hyperprivileged
 * resources.
 *
 * Since the kernel enables the hypervisor services/resources the injector
 * needs, the injector only needs to check that the major number version
 * matches the expected value.
 *
 * This func is based on code from the sun4v px driver (in file px_tools_4v.c).
 */
int
memtest_hv_diag_svc_check(void)
{
	uint64_t	diag_maj_ver;
	uint64_t	diag_min_ver;
	int		ret = MEMTEST_HYP_VER_BAD;
	char		*fname = "memtest_hv_diag_svc_check";

	/*
	 * Verify that hypervisor DIAG API has been negotiated
	 * already by the kernel.
	 */
	if ((ret = hsvc_version(HSVC_GROUP_DIAG, &diag_maj_ver,
	    &diag_min_ver)) != 0) {
		DPRINTF(0, "%s: hypervisor svc not negotiated, grp=0x%lx, "
		    "errno=%d\n", fname, HSVC_GROUP_DIAG, ret);
		return (ret);

	} else if (diag_maj_ver == 1) {
		/*
		 * Only a major version of 1 is OK.
		 *
		 * Code maintainers: if the version changes, check for
		 * API changes in hv_ra2pa() and hv_hpriv() before
		 * accepting the new version.
		 */
		ret = MEMTEST_HYP_VER_OK;
		DPRINTF(3, "%s: HV services check successful "
		    "grp:0x%lx, maj:0x%lx, min:0x%lx\n", fname);
	} else {
		DPRINTF(0, "%s: bad major number for hypervisor svc: "
		    "grp:0x%lx, maj:0x%lx, min:0x%lx\n", fname,
		    HSVC_GROUP_DIAG, diag_maj_ver, diag_min_ver);
		ret = EIO;
	}

	/*
	 * Even when the hvsc version code returns the correct major
	 * number the service can be unavailable (for example, not enabled
	 * via the vbsc prompt) so another check has to be made.
	 */
	if (ret == MEMTEST_HYP_VER_OK) {
		if ((ret = memtest_hv_util("memtest_hv_trap_check_asm",
		    (void *)memtest_hv_trap_check_asm, 0xa, 0xb, 0xc, 0xd))
		    == 0xa55) {
			DPRINTF(3, "%s: trap to hypervisor mode PASSED, "
			    "ret=0x%lx\n", fname, ret);
			ret = MEMTEST_HYP_VER_OK;
		} else {
			DPRINTF(0, "%s: trap to hypervisor mode FAILED, "
			    "ret=0x%lx\n", fname, ret);
			ret = EIO;
		}
	}

	return (ret);
}

/*
 * This routine does a peek or poke to the specified physical
 * address via the hypervisor.
 *
 * If the xorpat is non-zero the poke command will ignore
 * the specified data to write and will read the contents,
 * modify with the xorpat and write them back.  Simple
 * non-directed injections can be achieved this way.
 */
int
memtest_hv_mpeekpoke(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = iocp->ioc_addr;
	uint64_t	xor = iocp->ioc_xorpat;
	uint64_t	data = iocp->ioc_misc1;
	uint64_t	ret;
	char		*fname = "memtest_hv_mpeekpoke";

	if (F_VERBOSE(iocp)) {
		cmn_err(CE_NOTE, "%s: will perform %s to paddr=0x%08x.%08x, "
		    "using xorpat=0x%lx, data=0x%08x.%08x\n", fname,
		    (ERR_MISC_ISPEEK(iocp->ioc_command) ? "peek" : "poke"),
		    PRTF_64_TO_32(paddr), xor, PRTF_64_TO_32(data));
	}

	if (ERR_MISC_ISPEEK(iocp->ioc_command)) {

		ret = memtest_hv_util("hv_paddr_load64",
		    (void *)hv_paddr_load64, paddr, NULL, NULL, NULL);
		cmn_err(CE_NOTE, "%s: paddr=0x%08x.%08x "
		    "contents are 0x%08x.%08x\n", fname,
		    PRTF_64_TO_32(paddr), PRTF_64_TO_32(ret));

	} else if (ERR_MISC_ISPOKE(iocp->ioc_command)) {

		/*
		 * If an xor argument is specified and non-zero
		 * then do not simply write to the paddr, do a
		 * read, modify with xor, store sequence instead.
		 */
		if (IOC_XORPAT(iocp)) {
			data = memtest_hv_util("hv_paddr_load64",
			    (void *)hv_paddr_load64, paddr, NULL, NULL, NULL);
			data ^= xor;
		}

		ret = memtest_hv_util("hv_paddr_store64",
		    (void *)hv_paddr_store64, paddr, data, NULL, NULL);
		cmn_err(CE_NOTE, "%s: wrote data=0x%08x.%08x to "
		    "paddr=0x%08x.%08x\n", fname,
		    PRTF_64_TO_32(data), PRTF_64_TO_32(paddr));

	} else {
		cmn_err(CE_NOTE, "%s: invalid misc type for HV peek/poke!\n",
		    fname);
		return (EINVAL);
	}

	return (0);
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
 * The type of cache that the index is used for is chosen via
 * the mem_req subcommand (inpar).
 *
 * NOTE: for sun4v the physical address memlists are actually real addresses,
 *	 and the matching real address is returned via inpar &paddr2p.
 *
 * NOTE: the ioc struct is not filled in before the buffer is requested
 *	 so this routine cannot check any flags or values found in the
 *	 ioc struct (available from memtestp->m_mdatap[0]->m_iocp).
 *
 * NOTE: ideally this routine would work when provided just the index
 *	 (no way), just the way (no index), or both index and way.
 *	 In practice this is difficult due to the index hashing
 *	 algorithms so this routine requires both the index and the
 *	 way to be specified.  The check for this is done in userland.
 */
int
memtest_idx_to_paddr(memtest_t *memtestp, uint64_t *paddr1p, uint64_t *paddr2p,
		uint64_t req_cache_index, uint_t req_cache_way, int cache_type)
{
	extern struct memlist *phys_avail; /* total available physical memory */
	struct memlist *phys_mem;

	cpu_info_t	*cip = memtestp->m_mdatap[0]->m_cip; /* primary thr */
	uint_t		cache_size, cache_size_pow2;
	uint_t		cache_linesize, cache_assoc;
	uint_t		cache_way_mask, cache_way_shift;
	uint64_t	req_paddr_bits, req_idx_paddr_bits;
	uint64_t	req_fullidx_paddr_bits;
	uint64_t	cache_index_mask, cache_max_index;
	uint64_t	paddr, idx_paddr;
	uint64_t	paddr_masked, idx_paddr_masked;
	uint64_t	found_raddr_start, found_raddr_end;
	uint64_t	found_paddr_start, found_paddr_end;
	uint64_t	found_paddr_start_masked, found_paddr_end_masked;
	int		idx_enabled_flag = 0;
	page_t		*pp;
	char		*fname = "memtest_idx_to_paddr";

	DPRINTF(2, "%s: memtestp=0x%p, req_cache_index=0x%llx, "
	    "req_cache_way=0x%lx, cache_type=%d\n", fname, memtestp,
	    req_cache_index, req_cache_way, cache_type);

	/*
	 * The subcommand is checked to determine the type of cache in order
	 * to fill in the cache attributes used below.
	 *
	 * NOTE: the sublinesize is used for L2 and L3 caches (on sun4v)
	 *	 because the linesize is defined as the sublinesize
	 *	 multiplied by the cache size in MB for some reason.
	 */
	switch (cache_type) {
	case ERR_CLASS_DC:
		cache_size = cip->c_dc_size;
		cache_linesize = cip->c_dc_linesize;
		cache_assoc = cip->c_dc_assoc;
		cache_way_mask = cip->c_dc_assoc - 1;
		break;
	case ERR_CLASS_IC:
		cache_size = cip->c_ic_size;
		cache_linesize = cip->c_ic_linesize;
		cache_assoc = cip->c_ic_assoc;
		cache_way_mask = cip->c_ic_assoc - 1;
		break;
	case ERR_CLASS_L2:
	case ERR_CLASS_L2WB:
	case ERR_CLASS_L2CP:
		cache_size = cip->c_l2_size;
		cache_linesize = cip->c_l2_sublinesize;
		cache_assoc = cip->c_l2_assoc;
		cache_way_mask = cip->c_l2_assoc - 1;
		break;
	case ERR_CLASS_L3:
	case ERR_CLASS_L3WB:
	case ERR_CLASS_L3CP:
		cache_size = cip->c_l3_size;
		cache_linesize = cip->c_l3_sublinesize;
		cache_assoc = cip->c_l3_assoc;
		cache_way_mask = cip->c_l3_assoc - 1;
		break;
	default:
		DPRINTF(0, "%s: only specific cache commands can use the "
		    "index option, this class = %d\n", fname, cache_type);
		*paddr1p = *paddr2p = -1;
		return (EINVAL);
	}

	/*
	 * The way bitmasks are calculated to be the next highest
	 * power of two since not all sun4v caches are a
	 * power of two in size (the method below works for
	 * values up to 16-bits = 0xffff = 65535).
	 */
	cache_way_mask |= cache_way_mask >> 1;
	cache_way_mask |= cache_way_mask >> 2;
	cache_way_mask |= cache_way_mask >> 4;
	cache_way_mask |= cache_way_mask >> 8;

	/*
	 * For caches that are not a power of two in size the next
	 * power of two must be determined so that memory segments can
	 * be incremented through without clobbering important bits.
	 */
	cache_size_pow2 = cache_size;
	if ((cache_size & (cache_size - 1)) != 0) {
		/*
		 * If the cache size is not a power of two already
		 * round up to the next power of two.
		 *
		 * The following handles cachesize values that are
		 * 32-bits or less = 0xffff.ffff = 4294967295.
		 */
		cache_size_pow2 |= cache_size_pow2 >> 1;
		cache_size_pow2 |= cache_size_pow2 >> 2;
		cache_size_pow2 |= cache_size_pow2 >> 4;
		cache_size_pow2 |= cache_size_pow2 >> 8;
		cache_size_pow2 |= cache_size_pow2 >> 16;
		cache_size_pow2++;
	}

	/*
	 * Check that the requested index is valid for the cache type.
	 */
	cache_max_index = (cache_size / (cache_linesize * cache_assoc)) - 1;
	if (req_cache_index > cache_max_index) {
		DPRINTF(0, "%s: the requested index=0x%llx is larger than "
		    "the max index for this cache type=0x%llx\n", fname,
		    req_cache_index, cache_max_index);
		*paddr1p = *paddr2p = -1;
		return (EINVAL);
	}

	/*
	 * Check that the requested way is valid for the cache type.
	 */
	if (req_cache_way >= cache_assoc) {
		DPRINTF(0, "%s: requested cache way=%d is out "
		    "of range for this cache type=%d\n", fname,
		    req_cache_way, cache_assoc);
		*paddr1p = *paddr2p = -1;
		return (EINVAL);
	}

	/*
	 * Calculate the cache index bitmask (to be applied to the
	 * physical address) based on the cache attributes.
	 *
	 * Since sun4v injections are done in direct-map (DM) mode
	 * the requested way bits are tacked onto the requested index
	 * bits to produce the complete address to search for.
	 */
	cache_index_mask = cache_max_index * cache_linesize;

	DPRINTF(3, "%s: cache attributes are size=0x%llx "
	    "(rounded up to next power of two =0x%llx), max_index=0x%llx, "
	    "cache_index_bitmask=0x%llx, cache_way_mask=0x%lx\n",
	    fname, cache_size, cache_size_pow2, cache_max_index,
	    cache_index_mask, cache_way_mask);

	/*
	 * Determine the highest set bit in the cache_index_mask and
	 * add the cache_way_mask above it (this is how sun4v cache
	 * register accesses are laid out).
	 *
	 * NOTE: the linesize will cause some LSB bits to be zero so
	 *	 the count must skip over these.  This is why the count
	 *	 starts at bit 10.
	 */
	for (cache_way_shift = 10; cache_index_mask & (1 << cache_way_shift);
	    cache_way_shift++)
		;

	cache_index_mask |= (cache_way_mask << cache_way_shift);

	DPRINTF(3, "%s: complete cache index plus way mask=0x%llx\n",
	    fname, cache_index_mask);

	req_paddr_bits = req_cache_index * cache_linesize;

	req_paddr_bits |= ((req_cache_way & cache_way_mask) <<
	    cache_way_shift);

	/*
	 * If this is an L2-cache then perform the idx hash (if enabled)
	 * on the requested index+way combination.  This is done to the
	 * requested index+way because the way bits are part of the hash
	 * algorithm and because the hash is symmetrical.  Candidate
	 * physical addresses could also be hashed to compare to an
	 * unhashed index but that is much more work for the same result.
	 *
	 * This operation here can be thought of as un-hashing the index
	 * and way combo provided by the caller to match to an address.
	 *
	 * NOTE: if IDX mode is disabled the idx_paddr will match the
	 *	 original paddr.
	 *
	 * NOTE: this is actually just a half-hash (idx) because the
	 *	 top bits (above the way bits) are of course zero
	 *	 in the requested index+way and they likely will not
	 *	 be zero in an actual physical address.
	 *
	 * XXX	index hashing is processor specific, must modify for VT/YF.
	 *	also below when the "full hash" is performed.
	 */
	if ((cache_type == ERR_CLASS_L2) ||
	    (cache_type == ERR_CLASS_L2WB) ||
	    (cache_type == ERR_CLASS_L2CP)) {

		idx_enabled_flag = OP_CHECK_L2_IDX_MODE(memtestp->m_mdatap[0],
		    req_paddr_bits, &req_idx_paddr_bits);
	} else {
		req_idx_paddr_bits = req_paddr_bits;
	}

	DPRINTF(3, "%s: original (req_cache_index * linesize)="
	    "0x%llx, with req_cache_way becomes req_paddr_bits=0x%llx, "
	    "and lower idx'd becomes req_idx_paddr_bits=0x%llx\n",
	    fname, (req_cache_index * cache_linesize), req_paddr_bits,
	    req_idx_paddr_bits);

	/*
	 * Search phys_avail for a range of memory in which the specified
	 * index+way will fall between the start and end addresses.
	 * For each range found, iterate through the possible physical
	 * addresses that correspond to the requested cache index and
	 * return the first address that also falls on a page that
	 * is not long-term locked and will otherwise be subsequently
	 * available for use by the EI.
	 */
	memlist_read_lock();
	for (phys_mem = phys_avail; phys_mem; phys_mem = phys_mem->ml_next) {

		/*
		 * Skip the memory range if its base address is zero.
		 */
		if ((phys_mem->ml_address == NULL) ||
		    (phys_mem->ml_size < MIN_DATABUF_SIZE)) {
			continue;
		}

		/*
		 * Convert the found physmem start and end raddrs to paddrs
		 * via HV API call.
		 */
		found_raddr_start = phys_mem->ml_address;
		found_raddr_end = phys_mem->ml_address + phys_mem->ml_size - 1;

		if ((found_paddr_start = memtest_ra_to_pa(found_raddr_start))
		    == (uint64_t)-1) {
			DPRINTF(0, "%s: ra to pa translation "
			    "FAILED for raddr = 0x%lx\n", fname,
			    found_raddr_start);
			memlist_read_unlock();
			return (found_paddr_start);
		}

		if ((found_paddr_end = memtest_ra_to_pa(found_raddr_end))
		    == (uint64_t)-1) {
			DPRINTF(0, "%s: ra to pa translation "
			    "FAILED for raddr = 0x%lx\n", fname,
			    found_raddr_end);
			memlist_read_unlock();
			return (found_paddr_end);
		}

		/*
		 * Check if the range is contiguous in the physical
		 * space by looking at start and end physical addresses.
		 * If it isn't then all best are off and this routine
		 * will not produce the intended results.
		 */
		if ((found_paddr_end - found_paddr_start) !=
		    (found_raddr_end - found_raddr_start)) {
			DPRINTF(0, "%s: physical address range is not "
			    "contiguous within the real address block!\n"
			    "found_raddr_start=0x%p, found_raddr_end=0x%p,\n"
			    "found_paddr_start=0x%p, found_paddr_end=0x%p\n",
			    fname, found_raddr_start, found_raddr_end,
			    found_paddr_start, found_paddr_end);
			*paddr1p = *paddr2p = -1;
			memlist_read_unlock();
			return (EFAULT);
		}

		/*
		 * The method used to find a matching address is different
		 * if the target cache has index hashing enabled or not.
		 *
		 * Note that the loop below could increment cache_size but
		 * that only works for caches that are a power of two in
		 * size.  Because of the index hashing the loop must
		 * increment one larger than the index+way mask so that
		 * caches which are not a power of two will not lose
		 * the increment when the requested bits are placed
		 * into 'paddr'.
		 *
		 * XXX	index hashing is processor specific, must modify
		 *	cache_index_mask increment for VT/YF.
		 */
		if (idx_enabled_flag) {

			for (paddr = found_paddr_start;
			    paddr <= found_paddr_end;
			    paddr += cache_size_pow2) {

				/*
				 * Perform the top half of the IDX hash which
				 * will allow the candidate address to be
				 * compared to the requested index.
				 *
				 * The partial (lower) hash that was done the
				 * requested index+way is completed with the
				 * specific bits in the current paddr range
				 * (upper), this will allow the address to be
				 * compared to the requested index.
				 *
				 * NOTE: for N2/VF/RF the upper hash mask is
				 *	 N2_L2_IDX_TOPBITS = 0x1.f0000000.
				 *	 since bits[17:13] =
				 *		PA[32:28] xor PA[17:13]
				 *
				 * XXX	index hashing is processor specific,
				 *	must modify for VT/YF.
				 */
				req_fullidx_paddr_bits = ((paddr &
				    0x1f0000000) >> 15) ^ req_idx_paddr_bits;

				DPRINTF(3, "%s: found candidate mem "
				    "chunk mem->addr=0x%llx, mem->next=0x%p, "
				    "mem->size=0x%llx, "
				    "req_idx_paddr_bits=0x%llx, "
				    "cache_index_mask=0x%llx, "
				    "found_paddr_start=0x%llx, "
				    "found_paddr_end=0x%llx, "
				    "candidate_paddr=0x%llx, "
				    "fully idx'd req_paddr_bits=0x%llx\n",
				    fname, phys_mem->ml_address,
				    phys_mem->ml_next,
				    phys_mem->ml_size, req_idx_paddr_bits,
				    cache_index_mask, found_paddr_start,
				    found_paddr_end, paddr,
				    req_fullidx_paddr_bits);

				/*
				 * Need to find a paddr within the range that
				 * will map to the requested index+way.
				 *
				 * Basic algorithm:
				 * (start_paddr + x) -> {idx full hash} ->
				 *	masked == requested_index_bits
				 */
				paddr &= ~(cache_index_mask);
				paddr += req_fullidx_paddr_bits;

				/*
				 * Ensure the found address falls within the
				 * current memory segment.
				 */
				if ((paddr > (found_paddr_start +
				    phys_mem->ml_size)) ||
				    (paddr < found_paddr_start)) {
					continue;
				}

				/*
				 * Check if the candidate paddr will be
				 * installed at the requested index.
				 */
				(void) OP_CHECK_L2_IDX_MODE(
				    memtestp->m_mdatap[0], paddr, &idx_paddr);
				idx_paddr_masked = idx_paddr & cache_index_mask;

				if (idx_paddr_masked == req_paddr_bits) {
					DPRINTF(3, "%s: found candidate "
					    "paddr=0x%llx, its idx hash=0x%llx,"
					    " and idx masked=0x%llx\n", fname,
					    paddr, idx_paddr, idx_paddr_masked);
				} else {
					DPRINTF(3, "%s: found candidate "
					    "paddr=0x%llx, its idx hash=0x%llx,"
					    " (idx masked=0x%llx) does not "
					    "match requested paddr masked="
					    "0x%llx\n", fname, paddr, idx_paddr,
					    idx_paddr_masked, req_paddr_bits);
					continue;
				}

				/*
				 * Check the page the physical address falls
				 * on and make sure it is not long-term locked
				 * before requesting it from the system.
				 */
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

				/*
				 * If the found matching address is available
				 * then return success.
				 */
				DPRINTF(2, "%s: successfully matched cache "
				    "index=0x%llx (with way bits =0x%llx, "
				    "shifted with way bits =0x%llx) to "
				    "paddr=0x%llx via partially idx'd "
				    "address=0x%llx\n", fname,
				    req_cache_index,
				    req_paddr_bits/cache_linesize,
				    req_paddr_bits, paddr, req_idx_paddr_bits);

				*paddr1p = paddr;
				*paddr2p = found_raddr_start +
				    (paddr - found_paddr_start);
				memlist_read_unlock();
				return (0);
			}

		} else {
			/*
			 * If cache index hashing not required the old method
			 * can be used (similar to sun4u method).
			 */
			found_paddr_start_masked = found_paddr_start &
			    cache_index_mask;
			found_paddr_end_masked = found_paddr_end &
			    cache_index_mask;

			if (((req_paddr_bits >= found_paddr_start_masked) &&
			    (req_paddr_bits <= found_paddr_end_masked)) ||
			    (phys_mem->ml_size >= cache_size_pow2)) {

				DPRINTF(3, "%s: found candidate mem "
				    "chunk mem->addr=0x%llx, mem->next=0x%p, "
				    "mem->size=0x%llx, req_paddr_bits=0x%llx, "
				    "cache_index_mask=0x%llx, "
				    "found_paddr_start_masked=0x%llx, "
				    "found_paddr_end_masked=0x%llx\n",
				    fname, phys_mem->ml_address,
				    phys_mem->ml_next,
				    phys_mem->ml_size, req_paddr_bits,
				    cache_index_mask, found_paddr_start_masked,
				    found_paddr_end_masked);

				/*
				 * Determine the first exact address within
				 * the mem chunk that will correspond to the
				 * requested cache index by adding an offset
				 * to the base address.
				 *
				 * If this chunk is larger than the cache_size,
				 * may need to add cache_size to paddr since
				 * the index of the chunks BA can be larger
				 * than the requested index.
				 */
				if ((phys_mem->ml_size > cache_size_pow2) &&
				    (found_paddr_start_masked >
				    req_paddr_bits)) {
					paddr = found_paddr_start +
					    cache_size_pow2;
				} else {
					paddr = found_paddr_start;
				}

				paddr = paddr - found_paddr_start_masked +
				    req_paddr_bits;

				/*
				 * Check the page the physical address falls
				 * on and make sure it is not long-term locked
				 * before requesting it from the system.
				 *
				 * Iterate through the candidate physical
				 * addresses in this memory range looking for
				 * the first address that also falls on a page
				 * that the EI can use.
				 */
				for (; paddr < found_paddr_end;
				    paddr += cache_size_pow2) {

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
					 * The EI will attempt to acquire this
					 * page later.  If it's free now, we
					 * reduce the chance of having to try
					 * to relocate the page later (via the
					 * physmem driver) which may fail, and
					 * could fail repeatedly if we keep
					 * picking the same page here.  If the
					 * page is allocated after this check
					 * and before we grab it, one only
					 * needs to rerun the test to look for
					 * another free page.
					 */
					if (!PP_ISFREE(pp)) {
						continue;
					}

					break;
				}

				/*
				 * If no suitable address was found in this
				 * memory segment, go to the next memory range.
				 */
				if (paddr > (found_paddr_start +
				    phys_mem->ml_size)) {
					continue;
				}

				/*
				 * Verify that the index for the candidate
				 * paddr matches the requested index.
				 */
				paddr_masked = (paddr & cache_index_mask);

				if (paddr_masked == req_paddr_bits) {
					DPRINTF(2, "%s: successfully "
					    "matched cache index=0x%llx to "
					    "paddr=0x%llx via index=0x%llx\n",
					    fname, req_cache_index, paddr,
					    req_paddr_bits/cache_linesize);

					*paddr1p = paddr;
					*paddr2p = found_raddr_start +
					    (paddr - found_paddr_start);
					memlist_read_unlock();
					return (0);
				} else {
					DPRINTF(3, "%s: found paddr=0x%llx "
					    "(masked=0x%llx) does not match "
					    "requested paddr masked=0x%llx\n",
					    fname, paddr, paddr_masked,
					    req_paddr_bits);
					continue;
				}
			}
		}
	}

	memlist_read_unlock();

	/*
	 * If all available memory segments were already checked, fail.
	 */
	if (phys_mem == NULL) {
		DPRINTF(0, "%s: no usable physical address found in "
		    "any memory range that matches the requested "
		    "cache index+way (0x%llx)\n", fname,
		    req_paddr_bits/cache_linesize);
		*paddr1p = *paddr2p = -1;
		return (ENOMEM);
	}

	/* NOTREACHED */
	*paddr1p = *paddr2p = -1;
	return (-1);
}

/*
 * This routine is used to determine if the specified physical address
 * is in memory local to the cpu specified in the cpu_info_t in the
 * mdata_t structure.  The Victoria Falls platforms are currently the
 * only ones that need this check.  All others automatically return
 * true.
 */
int
memtest_is_local_mem(mdata_t *mdatap, uint64_t paddr)
{
	if (CPU_ISVFALLS(mdatap->m_cip)) {
		return (vf_is_local_mem(mdatap, paddr));
	} else {
		return (1);
	}
}

/*
 * This routine is simply a wrapper for the kernel va_to_pa routine.
 * It displays an error message if the translation fails.
 *
 * NOTE: for sun4v the physical address is actually a real address,
 *	 though common kernel routines retain the "pa" names.
 */
uint64_t
memtest_kva_to_ra(void *kvaddr)
{
	uint64_t	raddr;
	char		*fname = "memtest_kva_to_ra";

	if ((raddr = va_to_pa(kvaddr)) == -1) {
		DPRINTF(0, "%s: va_to_pa translation failed for "
		    "kvaddr=0x%p\n", fname, kvaddr);
		return (-1);
	}

	DPRINTF(3, "%s: returning: kvaddr=0x%p is mapped to raddr=0x%llx\n",
	    fname, kvaddr, raddr);

	return (raddr);
}

/*
 * Copy the assembly routines to the instruction buffer
 * area so that they can have errors inserted into them
 * and the system can recover from those errors.  The
 * assembly routines are all less than 256 bytes in length.
 */
int
memtest_pre_test_copy_asm(mdata_t *mdatap)
{
	caddr_t		tmpvaddr;
	int		len;
	char	*fname = "memtest_pre_test_copy_asm";

	DPRINTF(1, "%s: copying asm routines to 0x%p\n", fname,
	    mdatap->m_instbuf);

	if (CPU_ISRF(mdatap->m_cip)) {
		return (kt_pre_test_copy_asm(mdatap));
	} else if (CPU_ISVFALLS(mdatap->m_cip)) {
		return (vf_pre_test_copy_asm(mdatap));
	} else {
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

		return (0);
	}
}

/*
 * This routine does test initialization prior to an OBP, hypervisor or
 * kernel mode injection test.
 */
int
memtest_pre_test_kernel(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip = mdatap->m_cip;
	proc_t		*procp;
	caddr_t		kvaddr;
	uint64_t	raddr, raddr_end;
	uint64_t	paddr;
	uint64_t	*llptr;
	uint64_t	paddr_mask, buffer_data;
	int		i;
	char		*fname = "memtest_pre_test_kernel";

	DPRINTF(1, "\n%s(): sun4v version\n", fname);

	/*
	 * This must be done before copying asm functions into
	 * a buffer so that Victoria Falls code does the right thing.
	 */
	if (memtest_pre_init_threads(mdatap) != 0) {
		DPRINTF(0, "%s: failed to pre-initialize threads\n", fname);
		return (EIO);
	}

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
		 * Find the raddr and paddr from the buffers kvaddr.
		 */
		if ((raddr = memtest_kva_to_ra((void *)mdatap->m_databuf))
		    == -1) {
			return (ENXIO);
		}

		if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
			DPRINTF(0, "%s: ra to pa translation FAILED "
			    "for raddr = 0x%lx\n", fname, raddr);
			return (paddr);
		}

		/*
		 * Lock the kernel data buffer so it does not move.
		 */
		raddr_end = raddr + iocp->ioc_bufsize;
		(void) memtest_mem_request(mdatap->m_memtestp, &raddr,
		    &raddr_end, (iocp->ioc_bufsize / MMU_PAGESIZE),
		    MREQ_LOCK_PAGES);

		/*
		 * Set the correct attributes for the kernel buffer.
		 */
		if ((procp = ttoproc(curthread)) == NULL) {
			DPRINTF(0, "%s: NULL procp\n", fname);
			return (EIO);
		}

		rw_enter(&procp->p_as->a_lock, RW_READER);
		hat_chgattr(procp->p_as->a_hat, (caddr_t)mdatap->m_databuf,
		    iocp->ioc_bufsize, PROT_READ | PROT_WRITE | PROT_EXEC);
		rw_exit(&procp->p_as->a_lock);

		/*
		 * Initialize the data buffer to a debug friendly data pattern,
		 * that is different than the user and io buffer patterns
		 * (and which also includes the paddr of the data).
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
			    "size=0x%llx minsize=0x%llx\n",
			    fname, iocp->ioc_bufsize, MIN_DATABUF_SIZE);
			return (EINVAL);
		}

		/*
		 * Setup a kernel virtual mapping to the user data buffer.
		 * Note that this call may lock the physical pages in memory.
		 */
		if ((mdatap->m_databuf = memtest_map_u2kvaddr(mdatap,
		    (caddr_t)iocp->ioc_bufbase, 0, 0, iocp->ioc_bufsize))
		    == NULL) {
			DPRINTF(0, "%s: couldn't map user data buffer\n",
			    fname);
			return (ENXIO);
		}
	}

	/*
	 * Adjust the user or kernel allocated databuf pointer by the
	 * amount determined in the user program to account for memory
	 * interleaving and other factors.
	 */
	mdatap->m_databuf = (caddr_t)((uint64_t)mdatap->m_databuf &
	    ~(0xfffULL));
	mdatap->m_databuf += (iocp->ioc_databuf - iocp->ioc_bufbase);

	/*
	 * Use the second half of the data buffer for copying/executing
	 * instructions to/from.
	 */
	mdatap->m_instbuf = mdatap->m_databuf + (iocp->ioc_bufsize / 2);

	if (memtest_pre_test_copy_asm(mdatap) < 0) {
		DPRINTF(0, "%s: failed to copy asm to instr buffer at 0x%p\n",
		    fname, mdatap->m_instbuf);
		return (EIO);
	}

	/*
	 * Initialize the default virtual address to corrupt/access.
	 */
	if (ERR_MISC_ISVIRT(iocp->ioc_command)) {
		kvaddr = (caddr_t)(iocp->ioc_addr);
	} else {
		switch (ERR_ACC(iocp->ioc_command)) {
		case ERR_ACC_FETCH:
			if (ERR_MISC_ISTL1(iocp->ioc_command)) {
				kvaddr = (caddr_t)(mdatap->m_asmld_tl1);
			} else if (ERR_MISC_ISPCR(iocp->ioc_command)) {
				kvaddr = (caddr_t)(mdatap->m_pcrel);
			} else {
				if (ERR_CLASS_ISL2CP(iocp->ioc_command) ||
				    ERR_CLASS_ISL3CP(iocp->ioc_command)) {
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

	/*
	 * Find the (new) raddr and paddr from the kvaddr.
	 */
	if ((raddr = memtest_kva_to_ra((void *)kvaddr)) == -1) {
		return (ENXIO);
	}

	if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
		DPRINTF(0, "%s: ra to pa translation "
		    "FAILED for raddr = 0x%lx\n", fname, raddr);
		return (paddr);
	}

	mdatap->m_kvaddr_a = kvaddr + memtest_get_a_offset(iocp);
	mdatap->m_kvaddr_c = kvaddr + memtest_get_c_offset(iocp);
	mdatap->m_raddr_a = raddr + memtest_get_a_offset(iocp);
	mdatap->m_raddr_c = raddr + memtest_get_c_offset(iocp);
	mdatap->m_paddr_a = paddr + memtest_get_a_offset(iocp);
	mdatap->m_paddr_c = paddr + memtest_get_c_offset(iocp);

	mdatap->m_uvaddr_a = (caddr_t)iocp->ioc_addr +
	    memtest_get_a_offset(iocp);
	mdatap->m_uvaddr_c = (caddr_t)iocp->ioc_addr +
	    memtest_get_c_offset(iocp);

	if (memtest_init_threads(mdatap) != 0) {
		DPRINTF(0, "%s: failed to initialize threads\n", fname);
		return (EIO);
	}

	/*
	 * If the injection is a hyperpriv instruction error then
	 * modify the instruction buffer contents so the instructions
	 * can be run in hyperpriv mode.  Most commands will expect
	 * that the "normal" unaltered asmld routine will be used.
	 *
	 * The 15th and 16th (offset 56 and 60) instructions are changed to:
	 *	done			! = 0x81f0.0000
	 *	  nop			! = 0x0100.0000
	 *
	 * This is appropriate because most HV accesses that will use this
	 * buffer are called separately with a drill down to HV mode from
	 * a kernel mode access routine.
	 *
	 * Otherwise for an instruction buffer that can be called from
	 * HV mode and which will return to the caller:
	 * The 15th and 16th (offset 56 and 60) instructions are changed to:
	 *	jmp	%g7 + 4		! = jmpl %g7, %g0 (+ imm of 0x4)
	 *				!	= 0x81c1.e004
	 *	  nop			! = 0x0100.0000
	 *
	 * By convention the PC is stored in %g7 for HV mode calls
	 * that do a return within HV mode.
	 */
	if (ERR_MODE_ISHYPR(iocp->ioc_command) &&
	    ERR_ACC_ISFETCH(iocp->ioc_command)) {
		*(uint64_t *)(kvaddr + 56) = 0x81f0000001000000;
		membar_sync();
	}

	/*
	 * Print addresses and contents of buffer(s) being used.
	 */
	if (F_VERBOSE(iocp) || (memtest_debug > 0)) {
		cmn_err(CE_NOTE, "%s: buffer addresses and data contents "
		    "being used in hyper/kernel mode:\n", fname);
		cmn_err(CE_NOTE, "memtest_asmld: kvaddr=0x%p "
		    "(*=0x%lx, raddr=0x%08x.%08x)\n",
		    (void *)mdatap->m_asmld,
		    mdatap->m_asmld ? *(uint64_t *)memtest_asmld : 0,
		    PRTF_64_TO_32(mdatap->m_asmld ? \
		    memtest_kva_to_ra((void *)mdatap->m_asmld) : 0));
		cmn_err(CE_NOTE, "memtest_asmld_tl1: kvaddr=0x%p "
		    "(*=0x%lx, raddr=0x%08x.%08x)\n",
		    (void *)mdatap->m_asmld_tl1,
		    mdatap->m_asmld_tl1 ? *(uint64_t *)memtest_asmld_tl1 : 0,
		    PRTF_64_TO_32(mdatap->m_asmld_tl1 ? \
		    memtest_kva_to_ra((void *)mdatap->m_asmld_tl1) : 0));
		cmn_err(CE_NOTE, "m_databuf: kvaddr=0x%p "
		    "(*=0x%lx, raddr=0x%08x.%08x)\n",
		    (void *)mdatap->m_databuf,
		    *((uint64_t *)mdatap->m_databuf),
		    PRTF_64_TO_32(memtest_kva_to_ra((void *)
		    mdatap->m_databuf)));
		cmn_err(CE_NOTE, "m_instbuf: kvaddr=0x%p "
		    "(*=0x%lx, raddr=0x%08x.%08x)\n",
		    (void *)mdatap->m_instbuf,
		    *((uint64_t *)mdatap->m_instbuf),
		    PRTF_64_TO_32(memtest_kva_to_ra((void *)
		    mdatap->m_instbuf)));
		cmn_err(CE_NOTE, "m_pcrel: kvaddr=0x%p "
		    "(*=0x%lx, raddr=0x%08x.%08x)\n",
		    (void *)mdatap->m_pcrel,
		    mdatap->m_pcrel ? *((uint64_t *)mdatap->m_pcrel) : 0,
		    PRTF_64_TO_32(mdatap->m_pcrel ? \
		    memtest_kva_to_ra((void *)mdatap->m_pcrel) : 0));
	}

	return (0);
}

/*
 * This routine does test initialization prior to running a command which
 * has no mode.
 *
 * NOTE: the PHYS and REAL tests can use the address argument as an offset
 *	 into a cache or memory buffer.  Therefore the raddr and paddr
 *	 members are not always valid addreses.
 */
int
memtest_pre_test_nomode(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	caddr_t		kvaddr;
	uint64_t	raddr;
	uint64_t	paddr;
	char		*fname = "memtest_pre_test_nomode";

	DPRINTF(1, "%s(): sun4v version\n", fname);

	/*
	 * Only the PHYS, REAL, VIRT, and RAND tests, and utility commands
	 * are considered to have no mode.
	 */
	if (!ERR_CLASS_ISUTIL(iocp->ioc_command) &&
	    !ERR_MISC_ISPHYS(iocp->ioc_command) &&
	    !ERR_MISC_ISREAL(iocp->ioc_command) &&
	    !ERR_MISC_ISVIRT(iocp->ioc_command) &&
	    !ERR_MISC_ISRAND(iocp->ioc_command)) {
		DPRINTF(0, "%s: ERR_MISC=0x%x "
		    "is not ERR_MISC_PHYS=0x%x or "
		    "ERR_MISC_REAL=0x%x or ERR_MISC_VIRT=0x%x or "
		    "ERR_MISC_RAND=0x%x and ERR_CLASS=0x%x "
		    "is not ERR_CLASS_UTIL and so should "
		    "not be using the nomode pre-test routine\n", fname,
		    ERR_MISC(iocp->ioc_command), ERR_MISC_PHYS, ERR_MISC_REAL,
		    ERR_MISC_VIRT, ERR_MISC_RAND, ERR_CLASS(iocp->ioc_command));
		return (EINVAL);
	}

	/*
	 * Different commands utilize the "ioc_addr" address for different
	 * types of address.  Determine if it should be used as a kernel
	 * virtual, physical, or real address (real is the sun4v default).
	 *
	 * NOTE: of the four modes of address that the injector deals with,
	 *	 only those that are "lower" than the specified address
	 *	 mode can be determined.  The other address fields in the
	 *	 mdatap struct will be left as NULL since they are unused.
	 *
	 * NOTE: USER mode commands which are of type VIRT/PHYS must be
	 *	 defined as USER and they will go through the
	 *	 memtest_pre_test_user() routine.
	 */
	if (ERR_CLASS_ISUTIL(iocp->ioc_command)) {
		/*
		 * Utility (UTIL) commands do not use an address so no
		 * addresses should be manipulated, force the struct
		 * members to zero and exit.
		 */
		mdatap->m_uvaddr_a = NULL;
		mdatap->m_uvaddr_c = NULL;
		mdatap->m_kvaddr_a = NULL;
		mdatap->m_kvaddr_c = NULL;
		mdatap->m_raddr_a = NULL;
		mdatap->m_raddr_c = NULL;
		mdatap->m_paddr_a = NULL;
		mdatap->m_paddr_c = NULL;

		return (0);

	} else if (ERR_MISC_ISVIRT(iocp->ioc_command)) {

		kvaddr = (caddr_t)(iocp->ioc_addr);

		/*
		 * Find the raddr and paddr from the kvaddr.
		 */
		if ((raddr = memtest_kva_to_ra((void *)kvaddr)) == -1) {
			return (ENXIO);
		}

		if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
			DPRINTF(0, "%s: ra to pa translation failed "
			    "for raddr = 0x%lx\n", fname, raddr);
			return (paddr);
		}

		mdatap->m_uvaddr_a = NULL;
		mdatap->m_uvaddr_c = NULL;
		mdatap->m_kvaddr_a = kvaddr + memtest_get_a_offset(iocp);
		mdatap->m_kvaddr_c = kvaddr + memtest_get_c_offset(iocp);
		mdatap->m_raddr_a = raddr + memtest_get_a_offset(iocp);
		mdatap->m_raddr_c = raddr + memtest_get_c_offset(iocp);
		mdatap->m_paddr_a = paddr + memtest_get_a_offset(iocp);
		mdatap->m_paddr_c = paddr + memtest_get_c_offset(iocp);

	} else if (ERR_MISC_ISPHYS(iocp->ioc_command)) {

		/*
		 * Treat the address inpar as a physical address.
		 * The EI can't map a given paddr to an raddr (only
		 * the other way around) so there is no work to do here.
		 */
		paddr = iocp->ioc_addr;
		mdatap->m_paddr_a = paddr + memtest_get_a_offset(iocp);
		mdatap->m_paddr_c = paddr + memtest_get_c_offset(iocp);

	} else {

		/*
		 * By default use the address argument as a real address.
		 */
		raddr = iocp->ioc_addr;

		if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
			DPRINTF(0, "%s: ra to pa translation failed "
			    "for raddr = 0x%lx\n", fname, raddr);
			return (paddr);
		}

		mdatap->m_uvaddr_a = NULL;
		mdatap->m_uvaddr_c = NULL;
		mdatap->m_raddr_a = raddr + memtest_get_a_offset(iocp);
		mdatap->m_raddr_c = raddr + memtest_get_c_offset(iocp);
		mdatap->m_paddr_a = paddr + memtest_get_a_offset(iocp);
		mdatap->m_paddr_c = paddr + memtest_get_c_offset(iocp);

		/*
		 * If this is a memory command (such as mphys) we must also
		 * initialize a kernel virtual address since some of the
		 * low-level injection routines may need it as well as the
		 * physical (and real) address.
		 *
		 * NOTE: common kernel routines like pf_is_memory retain
		 *	 the "pa" names though they are using real addresses
		 *	 for sun4v.
		 */
		if (ERR_CLASS_ISMEM(iocp->ioc_command)) {

			if (!pf_is_memory(raddr >> MMU_PAGESHIFT)) {
				DPRINTF(0, "%s: raddr=0x%llx is not a valid "
				    "memory address\n", fname, raddr);
				return (ENXIO);
			}

			if ((kvaddr = memtest_map_p2kvaddr(mdatap, raddr,
			    PAGESIZE, 0, HAT_LOAD_NOCONSIST)) == NULL) {
				DPRINTF(0, "%s: memtest_map_p2kvaddr(paddr="
				    "0x%llx, size=0x%x, attr=0x%x, flags=0x%x) "
				    "failed\n", fname, raddr, PAGESIZE, 0,
				    HAT_LOAD_NOCONSIST);
				return (ENXIO);
			}

			mdatap->m_kvaddr_a = kvaddr +
			    memtest_get_a_offset(iocp);
			mdatap->m_kvaddr_c = kvaddr +
			    memtest_get_c_offset(iocp);
		} else {
			mdatap->m_kvaddr_a = NULL;
			mdatap->m_kvaddr_c = NULL;
		}
	}

	return (0);
}

/*
 * This routine does test initialization prior to a user mode test.
 */
int
memtest_pre_test_user(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	caddr_t		kvaddr;
	uint64_t	raddr;
	uint64_t	paddr;
	char		*fname = "memtest_pre_test_user";

	DPRINTF(1, "%s(): sun4v version\n", fname);

	if (F_MAP_KERN_BUF(iocp)) {
		DPRINTF(0, "%s: cannot use a kernel buffer for a user mode "
		    "test, defaulting to user buffer\n", fname);
	}

	/*
	 * If this is a VIRT command and a PID was specified set a flag so
	 * it will be used when the the kernel virtual mapping is created.
	 */
	if (ERR_MISC_ISVIRT(iocp->ioc_command)) {
		if (F_MISC1(iocp) && (iocp->ioc_misc1)) {
			IOC_FLAGS(iocp) |= FLAGS_PID;
			iocp->ioc_pid = iocp->ioc_misc1;

			/*
			 * Clear the MISC1 flag and value in case a
			 * processor specific routine uses it for something.
			 */
			IOC_FLAGS(iocp) &= ~((uint64_t)FLAGS_MISC1);
			iocp->ioc_misc1 = NULL;
		}
	}

	/*
	 * Setup a kernel mapping to the user virtual address.
	 */
	if ((kvaddr = memtest_map_u2kvaddr(mdatap, (caddr_t)iocp->ioc_addr, 0,
	    0, PAGESIZE)) == NULL)
		return (-1);

	/*
	 * Find the raddr and paddr from the kvaddr.
	 */
	if ((raddr = memtest_kva_to_ra((void *)kvaddr)) == -1) {
		return (ENXIO);
	}

	if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
		DPRINTF(0, "%s: ra to pa translation failed "
		    "for raddr = 0x%lx\n", fname, raddr);
		return (paddr);
	}

	mdatap->m_kvaddr_a = kvaddr + memtest_get_a_offset(iocp);
	mdatap->m_kvaddr_c = kvaddr + memtest_get_c_offset(iocp);
	mdatap->m_raddr_a = raddr + memtest_get_a_offset(iocp);
	mdatap->m_raddr_c = raddr + memtest_get_c_offset(iocp);
	mdatap->m_paddr_a = paddr + memtest_get_a_offset(iocp);
	mdatap->m_paddr_c = paddr + memtest_get_c_offset(iocp);

	mdatap->m_uvaddr_a = (caddr_t)iocp->ioc_addr +
	    memtest_get_a_offset(iocp);
	mdatap->m_uvaddr_c = (caddr_t)iocp->ioc_addr +
	    memtest_get_c_offset(iocp);

	return (0);
}

/*
 * This routine is called after executing a test in kernel/hyper/obp mode.
 */
int
memtest_post_test_kernel(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	raddr;
	int		ret = 0;
	char		*fname = "memtest_post_test_kernel";

	DPRINTF(1, "%s(mdatap=0x%p)\n", fname, mdatap);

	/*
	 * Free the kernel allocated data buffer for those tests which
	 * have allocated it.
	 */
	if (F_MAP_KERN_BUF(iocp)) {
		/*
		 * Wait a bit for the error to propagate through the system
		 * so freeing the buffer does not trip the error again before
		 * it can be handled.
		 */
		delay(iocp->ioc_delay * hz);

		/*
		 * Determine the address to search for in the linked list,
		 * undo an adjustments that were made for memory interleave.
		 */
		mdatap->m_databuf -= (iocp->ioc_databuf - iocp->ioc_bufbase);

		if ((raddr = memtest_kva_to_ra((void *)mdatap->m_databuf))
		    == -1) {
			return (raddr);
		}

		ret = memtest_free_kernel_memory(mdatap, raddr);
	}

	return (ret);
}

/*
 * Restore the DRAM and/or L2 cache scrubber for the test types which
 * disabled scrubbers in memtest_pre_test_kernel.
 *
 * Conditionals must agree with the memtest_set_scrubbers() routine,
 * perhaps a scrub flag can be used to make this simpler and less prone
 * to breakage if the memtest_set_scrubbers() routine is modified.
 */
int
memtest_restore_scrubbers(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	scrub_flags;
	char		*fname = "memtest_restore_scrubbers";

	/*
	 * Restore DRAM scrubbers
	 */
	scrub_flags = 0;

	if (!(F_MEMSCRUB_ASIS(iocp))) {

		if ((F_MEMSCRUB_EN(iocp) || F_MEMSCRUB_DIS(iocp)) ||
		    (ERR_CLASS_ISMEM(iocp->ioc_command) &&
		    !(ERR_MISC_ISLOWIMPACT(iocp->ioc_command)))) {
			DPRINTF(1, "%s: restoring DRAM scrubber(s)\n", fname);
			scrub_flags = MDATA_SCRUB_DRAM | MDATA_SCRUB_RESTORE;
		}

		if (scrub_flags) {
			if (OP_CONTROL_SCRUB(mdatap, scrub_flags)) {
				DPRINTF(0, "%s: DRAM scrub restore opsvec "
				    "call FAILED!\n", fname);
			}
		}
	}

	/*
	 * Restore L2 scrubbers
	 */
	scrub_flags = 0;

	if (!(F_L2SCRUB_ASIS(iocp))) {

		if ((F_L2SCRUB_EN(iocp) || F_L2SCRUB_DIS(iocp)) ||
		    ((ERR_CLASS_ISL2(iocp->ioc_command) ||
		    ERR_CLASS_ISL2WB(iocp->ioc_command)) &&
		    !(ERR_MISC_ISLOWIMPACT(iocp->ioc_command)))) {
			DPRINTF(1, "%s: restoring L2$ scrubber(s)\n", fname);
			scrub_flags = MDATA_SCRUB_L2 | MDATA_SCRUB_RESTORE;
		}

		if (scrub_flags) {
			if (OP_CONTROL_SCRUB(mdatap, scrub_flags)) {
				DPRINTF(0, "%s: L2$ scrub restore opsvec call "
				    "FAILED!\n", fname);
			}
		}
	}

	return (0);
}

/*
 * Disable/enable the DRAM and/or L2 cache scrubber(s) based on
 * the specified user options.  By default a single DRAM or L2-cache
 * banks scrubber is disabled for a command so that it does not
 * trip planted errors before they can be accessed.
 */
int
memtest_set_scrubbers(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	scrub_flags;
	char		*fname = "memtest_set_scrubbers";

	/*
	 * Enable/disable/do nothing to the DRAM scrubbers
	 */
	scrub_flags = 0;

	if (!(F_MEMSCRUB_ASIS(iocp))) {

		if (F_MEMSCRUB_EN(iocp)) {
			DPRINTF(1, "%s: enabling DRAM scrubber(s)\n", fname);
			scrub_flags = MDATA_SCRUB_DRAM | MDATA_SCRUB_ENABLE;
		} else if (F_MEMSCRUB_DIS(iocp)) {
			DPRINTF(1, "%s: disabling DRAM scrubber(s)\n", fname);
			scrub_flags = MDATA_SCRUB_DRAM | MDATA_SCRUB_DISABLE;
		} else if (ERR_CLASS_ISMEM(iocp->ioc_command) &&
		    !(ERR_MISC_ISLOWIMPACT(iocp->ioc_command))) {
			DPRINTF(1, "%s: disabling DRAM scrubber(s)\n", fname);
			scrub_flags = MDATA_SCRUB_DRAM | MDATA_SCRUB_DISABLE;
		}

		if (scrub_flags) {
			mdatap->m_scrubp->s_mem_offset = mdatap->m_paddr_c;
			if (OP_CONTROL_SCRUB(mdatap, scrub_flags)) {
				DPRINTF(0, "%s: DRAM scrub opsvec call "
				    "FAILED!\n", fname);
			}
		}
	}

	/*
	 * Enable/disable/do nothing to the L2 scrubbers
	 */
	scrub_flags = 0;

	if (!(F_L2SCRUB_ASIS(iocp))) {

		if (F_L2SCRUB_EN(iocp)) {
			DPRINTF(1, "%s: enabling L2$ scrubber(s)\n", fname);
			scrub_flags = MDATA_SCRUB_L2 | MDATA_SCRUB_ENABLE;
		} else if (F_L2SCRUB_DIS(iocp)) {
			DPRINTF(1, "%s: disabling L2$ scrubber(s)\n", fname);
			scrub_flags = MDATA_SCRUB_L2 | MDATA_SCRUB_DISABLE;
		} else if ((ERR_CLASS_ISL2(iocp->ioc_command) ||
		    ERR_CLASS_ISL2WB(iocp->ioc_command)) &&
		    !(ERR_MISC_ISLOWIMPACT(iocp->ioc_command))) {
			DPRINTF(1, "%s: disabling L2$ scrubber(s)\n", fname);
			scrub_flags = MDATA_SCRUB_L2 | MDATA_SCRUB_DISABLE;
		}

		if (scrub_flags) {
			mdatap->m_scrubp->s_l2_offset = mdatap->m_paddr_c;
			if (OP_CONTROL_SCRUB(mdatap, scrub_flags)) {
				DPRINTF(0, "%s: L2$ scrub opsvec call "
				    "FAILED!\n", fname);
			}
		}
	}

	return (0);
}

/*
 * *******************************************************************
 * The following block of routines are the hypervisor access routines.
 * *******************************************************************
 */

/*
 * Routine to run an injector routine as hyperpriv via the hypervisor
 * diagnostic API.
 *
 * The code to be run should be contiguous in the real address space and
 * must not cross a page boundary.  Also it must adhere to all the rules
 * for a hypervisor trap function.
 *	- do not use new register window
 *	- do not cross a page boundary
 *	- only use output and global regs
 *	- only use relative branches/jumps (position independent)
 *	- execute done when finished
 */
static uint64_t
memtest_hv_exec(char *func_name, void *vaddr, uint64_t a1, uint64_t a2,
			uint64_t a3, uint64_t a4)
{
	uint64_t	raddr;
	uint64_t	paddr;
	uint64_t	ret;
	char		*fname = "memtest_hv_exec";

	DPRINTF(3, "%s: routine %s at vaddr = 0x%p, arg1 = 0x%lx,"
	    " arg2 = 0x%lx, arg3 = 0x%lx, arg4 = 0x%lx\n",
	    fname, func_name, vaddr, a1, a2, a3, a4);

	/*
	 * Find the raddr from the vaddr.
	 */
	if ((raddr = memtest_kva_to_ra((void *)vaddr)) == -1) {
		return (raddr);
	}

	/*
	 * Translate the raddr of routine to paddr via hypervisor.
	 */
	if ((paddr = memtest_ra_to_pa(raddr)) == (uint64_t)-1) {
		DPRINTF(0, "%s: ra to pa translation failed for "
		    "raddr=0x%lx\n", fname, raddr);
		return (paddr);
	}

	DPRINTF(3, "%s: trapping to %s at paddr=0x%lx (raddr=0x%lx)\n",
	    fname, func_name, paddr, raddr);

	ret = memtest_run_hpriv((uint64_t)paddr, a1, a2, a3, a4);

	DPRINTF(3, "%s: returned from HV with 0x%lx\n", fname, ret);
	return (ret);
}

/*
 * Wrapper routines to run an injection or a memtest utility routine via the
 * hypervisor diagnostic API.  This allows the driver code to be more
 * descriptive and they should get compiled out of the memtest binary to
 * avoid the extra call overhead.
 */
uint64_t
memtest_hv_inject_error(char *func_name, void *vaddr, uint64_t a1, uint64_t a2,
			uint64_t a3, uint64_t a4)
{
	return (memtest_hv_exec(func_name, vaddr, a1, a2, a3, a4));
}

uint64_t
memtest_hv_util(char *func_name, void *vaddr, uint64_t a1, uint64_t a2,
			uint64_t a3, uint64_t a4)
{
	return (memtest_hv_exec(func_name, vaddr, a1, a2, a3, a4));
}

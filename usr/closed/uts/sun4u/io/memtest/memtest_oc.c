/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file contains OC specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_oc.h>
#include <sys/memtest_u.h>
#include <sys/memtest_oc.h>
#include <sys/opl_olympus_regs.h>
#include <sys/opl_module.h>
#include <vm/hat_sfmmu.h>
#include <sys/mc-opl.h>

/*
 * Static routines located in this file.
 */
static	int	oc_flushall_caches(cpu_info_t *);
static	int	oc_get_cpu_info(cpu_info_t *);
static	int	oc_inject_mtlb(mdata_t *);
static	int	oc_iugr(mdata_t *);
static	int	oc_l1due(mdata_t *);
static	int	oc_l2ue(mdata_t *);

extern	void	affinity_set(int);
extern	void	affinity_clear(void);

/*
 * These OC errors are grouped according to the definitions
 * in the header file.
 */
cmd_t olympusc_cmds[] = {
	/* CPU-detected and MAC-detected errors */
	G4U_KD_UE,	memtest_k_mem_err,	"memtest_k_mem_err",
	G4U_KI_UE,	memtest_k_mem_err,	"memtest_k_mem_err",
	G4U_UD_UE,	memtest_u_mem_err,	"memtest_u_mem_err",
	G4U_UI_UE,	memtest_u_mem_err,	"memtest_u_mem_err",
	G4U_MPHYS,	memtest_mphys,		"memtest_mphys",
	G4U_KMPEEK,	memtest_k_mpeekpoke,	"memtest_k_mpeekpoke",
	G4U_KMPOKE,	memtest_k_mpeekpoke,	"memtest_k_mpeekpoke",

	/* CPU-detected errors */
	OC_KD_UETL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	OC_KI_UETL1,	memtest_k_mem_err,	"memtest_k_mem_err",
	OC_L1DUE,	memtest_k_dc_err,	"memtest_k_dc_err",
	OC_L1DUETL1,	memtest_k_dc_err,	"memtest_k_dc_err",
	OC_L2UE,	oc_l2ue,		"oc_l2ue",
	OC_L2UETL1,	oc_l2ue,		"oc_l2ue",
	OC_IUGR,	oc_iugr,		"oc_iugr",
	OC_KD_MTLB,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	OC_UD_MTLB,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	OC_KI_MTLB,	memtest_k_tlb_err,	"memtest_k_tlb_err",
	OC_UI_MTLB,	memtest_k_tlb_err,	"memtest_k_tlb_err",

	/* MAC-detected errors, normal mode */
	OC_ICE,		memtest_k_mem_err,	"memtest_k_mem_err",
	OC_PCE,		memtest_k_mem_err,	"memtest_k_mem_err",
	OC_PUE,		memtest_k_mem_err,	"memtest_k_mem_err",

	/* MAC-detected errors, mirror mode */
	OC_KD_CMPE,	memtest_k_mem_err,	"memtest_k_mem_err",
	OC_KI_CMPE,	memtest_k_mem_err,	"memtest_k_mem_err",
	OC_UD_CMPE,	memtest_u_mem_err,	"memtest_u_mem_err",
	OC_UI_CMPE,	memtest_u_mem_err,	"memtest_u_mem_err",
	OC_CMPE,	memtest_k_mem_err,	"memtest_k_mem_err",
	OC_MUE,		memtest_k_mem_err,	"memtest_k_mem_err",

	NULL,		NULL,			NULL,
};

static cmd_t *commands[] = {
	olympusc_cmds,
	NULL
};

/*
 * OC operations vector table.
 */
static opsvec_u_t olympusc_uops = {
	/* sun4u injection ops vectors */
	notsup,			/* corrupt d$ tag at offset */
	notsup,			/* corrupt i$ tag at offset */
	notsup,			/* corrupt e$ tag at offset */
	oc_inject_memory,	/* corrupt memory at paddr */
	notsup,			/* no mtag routine */
	notsup,			/* no p-cache routine */

	/* sun4u support ops vectors */
	notimp,			/* generate ecc for data passed in */
};

static opsvec_c_t olympusc_cops = {
	/* common injection ops vectors */
	oc_l1due,		/* L1D cache routine */
	notsup,			/* no dcache offset routine */
	notsup,			/* no corrupt fp reg */
	notsup,			/* no icache routine */
	notsup,			/* no corrupt internal */
	notsup,			/* no iphys routine */
	notsup,			/* no corrupt int reg */
	notimp,			/* corrupt e$ at paddr */
	notsup,			/* no corrupt e$ at offset */
	notsup,			/* no corrupt l3$ at paddr */
	notsup,			/* no corrupt l3$ at offset */
	oc_inject_mtlb,		/* multiple TLB hits */

	/* common support ops vectors */
	notsup,			/* no fp reg access */
	notsup,			/* no int reg access */
	notimp,			/* check ESRs */
	notimp,			/* enable AFT errors */
	notimp,			/* no enable/disable L2 or memory scrubbers */
	oc_get_cpu_info,	/* put cpu info into struct */
	oc_flushall_caches,	/* flush all caches */
	notsup,			/* flush all d$ */
	notsup,			/* no flush all i$ */
	notsup,			/* flush all l2$ */
	notsup,			/* no flush all l3$ */
	notimp,			/* flush d$ entry */
	notsup,			/* flush i$ entry (prefetch buffers only) */
	notimp,			/* flush l2$ entry - not used */
	notsup,			/* no flush l3$ entry */
};

/*
 * The routine inserts multiple TLB entries.
 */
static int
oc_inject_mtlb(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	command;
	caddr_t		va, tva;
	tte_t		tte, ttte;
	pfn_t		pfn, tpfn;
	int		ctx;
	char		*fname = "oc_inject_mtlb";

	if (iocp == NULL) {
		DPRINTF(0, "%s: null iocp\n", fname);
		return (EIO);
	}
	tva = kmem_alloc(MMU_PAGESIZE, KM_SLEEP);

	command = IOC_COMMAND(iocp);
	if (command == OC_KD_MTLB || command == OC_KI_MTLB) {
		ctx = KCONTEXT;
		va = mdatap->m_kvaddr_a;
	} else {
		ctx = oc_get_sec_ctx();
		va = mdatap->m_uvaddr_a;
	}

	/* Use kvaddr in va_to_pfn for both kernel and user cases. */
	if ((pfn = va_to_pfn(mdatap->m_kvaddr_a)) == PFN_INVALID) {
		DPRINTF(0, "%s: invalid pfn\n", fname);
		return (EIO);
	}
	if ((tpfn = va_to_pfn(tva)) == PFN_INVALID) {
		DPRINTF(0, "%s: invalid pfn\n", fname);
		return (EIO);
	}

	affinity_set(CPU->cpu_id);

	/*
	 * Insert fTLB entries.
	 */
	bzero(&tte, sizeof (tte));
	sfmmu_memtte(&tte, pfn, PROT_WRITE | HAT_NOSYNC, TTE8K);
	sfmmu_memtte(&ttte, tpfn, PROT_WRITE | HAT_NOSYNC, TTE8K);
	if (command == OC_KD_MTLB || command == OC_UD_MTLB) {
		tte.tte_intlo |= TTE_LCK_INT;
		oc_dtlb_ld(tva, ctx, &ttte);
		membar_sync();
		oc_dtlb_ld(va, ctx, &tte);
		membar_sync();
		oc_dtlb_ld(va, ctx, &tte);
		membar_sync();
	} else {
		tte.tte_intlo |= TTE_LCK_INT;
		oc_itlb_ld(tva, ctx, &ttte);
		membar_sync();
		oc_itlb_ld(va, ctx, &tte);
		membar_sync();
		oc_itlb_ld(va, ctx, &tte);
		membar_sync();
	}
	vtag_flushpage(tva, (uint64_t)ksfmmup);
	return (0);
}

/*
 * The routine injects an error into memory at the specified paddr.
 * It also triggers the error in the case of MI CE or MI UE.
 */
/* ARGSUSED */
int
oc_inject_memory(mdata_t *mdatap, uint64_t paddr, caddr_t kvaddr, uint_t ecc)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	command, misc1, misc2;
	uint32_t	flags;
	int		err_type;
	char		*fname = "oc_inject_memory";

	if (iocp == NULL) {
		DPRINTF(0, "%s: null iocp\n", fname);
		return (EIO);
	}

	command = IOC_COMMAND(iocp);

	switch (command) {
	case OC_ICE:
	case OC_PCE:
		misc1 = (F_MISC1(iocp)? iocp->ioc_misc1 : 0);
		switch (misc1) {	/* PTRL (default) or MI */
		case 0:
			flags = OC_FLAG_PTRL;
			break;
		case 1:
			flags = OC_FLAG_MI_CE;
			break;
		default:
			DPRINTF(0, "%s: unsupported misc1 value %d\n",
			    fname, misc1);
			return (EINVAL);
		}

		misc2 = (F_MISC2(iocp)? iocp->ioc_misc2 : 0);
		switch (misc2) {	/* 1- or 2-sided */
		case 0:
			if (command == OC_ICE) {
				/* intermittent CE */
				err_type = MC_INJECT_INTERMITTENT_CE;
			} else {
				/* permanent CE */
				err_type = MC_INJECT_PERMANENT_CE;
			}
			break;
		case 1:
			if (command == OC_ICE) {
				/* intermittent CE */
				err_type = MC_INJECT_INTERMITTENT_MCE;
			} else {
				/* permanent CE */
				err_type = MC_INJECT_PERMANENT_MCE;
			}
			break;
		default:
			DPRINTF(0, "%s: unsupported misc2 value %d\n",
			    fname, misc2);
			return (EINVAL);
		}
		break;

	case OC_PUE: /* normal mode/1-sided mirror mode PTRL UEs */
	case OC_MUE: /* 2-sided mirror mode PTRL UEs */
		if (command == OC_PUE) {
			err_type = MC_INJECT_UE;
		} else {
			err_type = MC_INJECT_MUE;
		}
		flags = OC_FLAG_PTRL;
		break;

	case G4U_KD_UE:
	case G4U_KI_UE:
	case G4U_UD_UE:
	case G4U_UI_UE:
	case G4U_MPHYS:
	case OC_KD_UETL1:	/* CPU-detected only */
	case OC_KI_UETL1:	/* CPU-detected only */

		/*
		 * Specify parameters for OC_K[D|I]_UETL1.
		 * (Note they do not have misc1 or misc2 option).
		 */
		/* specify flag */
		flags = OC_FLAG_UE_TL1;
		/* specify error type */
		err_type = MC_INJECT_UE;

		if (command == OC_KD_UETL1 || command == OC_KI_UETL1)
			break;

		misc1 = (F_MISC1(iocp)? iocp->ioc_misc1 : 0);
		switch (misc1) {
		case 0:
			if (command == G4U_UD_UE || command == G4U_UI_UE)
				flags = OC_FLAG_USER_UE;
			else if (command == G4U_MPHYS)
				flags = OC_FLAG_MPHYS;
			else
				flags = OC_FLAG_SYNC_UE;
			break;
		case 1:
		case 2:
			flags = OC_FLAG_MI_UE;
			if (misc1 == 2)
				err_type = MC_INJECT_MUE;
			break;
		default:
			DPRINTF(0, "%s: not supported\n", fname);
			return (EINVAL);
		}
		break;

	case OC_KD_CMPE:	/* MI */
	case OC_KI_CMPE:	/* MI */
	case OC_UD_CMPE:	/* MI */
	case OC_UI_CMPE:	/* MI */
	case OC_CMPE:		/* PTRL */

		flags = ((command == OC_CMPE)? OC_FLAG_PTRL :
		    OC_FLAG_MI_UE);
		err_type = MC_INJECT_CMPE;

		break;

	default:
		DPRINTF(0, "%s: unsupported oc command 0x%llx\n",
		    fname, command);
		return (EINVAL);
	}

	DPRINTF(3, "%s: misc1=0x%llx, misc2=0x%llx, err_type=%d, "
	    "flags=%u\n", fname, misc1, misc2, err_type, flags);

	/* inject the error */
	if ((*mc_inject_err)(err_type, paddr, flags) != 0) {
		DPRINTF(0, "%s: mc_inject_error() failed.\n", fname);
		return (EIO);
	}

	return (0);
}

/* This routine gets Olympus C cpu info */
int
oc_get_cpu_info(cpu_info_t *cip)
{
	cip->c_dc_size = OPL_DCACHE_SIZE;
	cip->c_dc_linesize = OPL_DCACHE_LSIZE;
	cip->c_dc_assoc = OPL_ECACHE_NWAY;

	cip->c_ic_size = OPL_ICACHE_SIZE;
	cip->c_ic_linesize = OPL_ICACHE_LSIZE;
	cip->c_ic_assoc = OPL_ECACHE_NWAY;

	cip->c_l2_size = OPL_ECACHE_SIZE;
	cip->c_l2_linesize = 256;
	cip->c_l2_sublinesize = 64;
	cip->c_l2_assoc = 12;

	cip->c_l2_flushsize = cip->c_l2_size * cip->c_l2_assoc * 2;
	cip->c_mem_flags = 0;

	return (0);
}

/* This routine assigns OC error injection commands and ops vectors. */
void
oc_init(mdata_t *mdatap)
{
	char		*fname = "oc_init";

	if (mc_inject_err == NULL) {
		mc_inject_err = (int (*)(int, uint64_t, uint32_t))
		    modgetsymvalue("mc_inject_error", 0);
		if (mc_inject_err == NULL) {
			DPRINTF(0, "%s: mc_inject_error undefined\n", fname);
			return;
		}
	}

	mdatap->m_sopvp = &olympusc_uops;
	mdatap->m_copvp = &olympusc_cops;
	mdatap->m_cmdpp = commands;
}

/*ARGSUSED*/
static int
oc_flushall_caches(cpu_info_t *cip)
{
	cpu_flush_ecache();
	return (0);
}

/*
 * This routine offlines/suspends or rsumes/onlines
 * either the sibling cpu or the sibling core.
 */
void
oc_xc_core(processorid_t cpuid, int coreid, int offline)
{
	processorid_t	cid;
	processorid_t	sibl;

	if (OC_CORE_ID(cpuid) == coreid) {
		/* same core */
		sibl = OC_SIBLING_STRAND(cpuid);
		oc_xc_cpu(sibl, offline);
	} else {
		/* different core */
		cid = OC_CORE_CPU0(cpuid, coreid);
		sibl = OC_SIBLING_STRAND(cid);
		oc_xc_cpu(cid, offline);
		oc_xc_cpu(sibl, offline);
	}
}

/*
 * This routine offlines/suspends or resumes/onlines
 * all the other cpu's in a chip.
 */
void
oc_xc_chip(processorid_t cpuid, int offline)
{
	oc_xc_core(cpuid, 0, offline);
	oc_xc_core(cpuid, 1, offline);
	if (IS_JUPITER(cpunodes[cpuid].implementation)) {
		oc_xc_core(cpuid, 2, offline);
		oc_xc_core(cpuid, 3, offline);
	}
}

void
oc_xc_sync(processorid_t cpuid)
{
	cpuset_t cpuset;

	CPUSET_ZERO(cpuset);
	CPUSET_ADD(cpuset, cpuid);
	xt_sync(cpuset);
}

/*
 * This routine offlines and suspends or resumes and online
 * the specified cpu, depending on the value of the flag offline.
 * If offline is non-zero, offline and suspend the specified cpu.
 * Otherwise, resume and online the specified cpu.
 */
void
oc_xc_cpu(processorid_t cpuid, int offline)
{
	uint64_t	xcf;
	char		*fname = "oc_xc_cpu";

	if (offline) {
		xcf = (uint64_t)oc_suspend_xtfunc;
		mutex_enter(&cpu_lock);
		if (cpu_offline(cpu[cpuid], CPU_OFFLINE)) {
			DPRINTF(0, "%s: couldn't offline %d\n", fname, cpuid);
			mutex_exit(&cpu_lock);
			return;
		}
		mutex_exit(&cpu_lock);
	} else {
		xcf = (uint64_t)oc_resume_xtfunc;
	}

	xt_one(cpuid, (xcfunc_t *)oc_sys_trap, xcf, 0);
	oc_xc_sync(cpuid);

	if (offline == 0) {
		mutex_enter(&cpu_lock);
		if (cpu_online(cpu[cpuid]))
			DPRINTF(0, "%s: couldn't online %d\n", fname, cpuid);
		mutex_exit(&cpu_lock);
	}
}

/*
 * This cross-call-tl1 routine suspends the specified cpu.
 */
/*ARGSUSED*/
void
oc_suspend_xtfunc(uint64_t dummy1, uint64_t dummy2)
{
	oc_susp();
}

/*
 * This cross-call-tl1 routine resumes the specified cpu.
 */
/*ARGSUSED*/
void
oc_resume_xtfunc(uint64_t dummy1, uint64_t dummy2)
{}

/*
 * This routine injects the following CPU-detected UEs:
 *	OC_L1DUE, OC_L1DUETL1
 */
int
oc_l1due(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	command = iocp->ioc_command;
	uint_t		*writep, myid, i, buf_sz = 256;
	int		ret = 0;
	caddr_t		buf;
	char		*fname = "oc_l1due";

	buf = mdatap->m_kvaddr_a;
	writep = (uint_t *)buf;

	myid = getprocessorid();

	/*
	 * Inject the error.
	 *
	 * First, flush L2$ and fill it with random data to inject
	 * a L2$ error.  Olympus-C keeps L2$ and L1$ in sync.
	 * Flushing L2$ is equivalent to flushing L1D$.
	 *
	 * Set the appropriate field of ASI_ERR_INJCT register:
	 *	Bit	Field	Description
	 *	1	L1D	L1D$ UE
	 */
	cpu_flush_ecache();
	oc_set_err_injct_l1d();

	/*
	 * Complete the injection of the L1D$ error by writing to
	 * the following byte locations in the buffer: <23:16>, <31:24>,
	 * <55:48>, and <63:56>.
	 * Note that we don't exactly know where in the buffer the
	 * error was injected. We only know that it is in one of
	 * the remaining locations: <7:0>, <15:8>, <39:32>, and <47:40>.
	 */
	for (i = 4; i < buf_sz/4; i += 8) {
		writep[i] = 1;
		writep[i + 2] = 1;
	}

	membar_sync();

	/*
	 * Invoke the error.
	 */
	switch (command) {
	case OC_L1DUE:
		/*
		 * Since we don't know where in the buffer the error
		 * was injected, we load the entire buffer to trigger
		 * the error. This is done by an assembly routine
		 * to avoid the compiler optimizing out the load
		 * instructions.
		 */
		oc_load(buf, buf_sz);
		break;
	case OC_L1DUETL1:
		xt_one(myid, (xcfunc_t *)oc_inv_l12uetl1,
		    (uint64_t)buf, (uint64_t)buf_sz);
		break;
	default:
		DPRINTF(0, "%s: unsupported command\n", fname);
		ret = EINVAL;
	}
	/* Should not reach here! */

	IOC_FLAGS(iocp) |= FLAGS_NOERR;
	return (ret);
}

/*
 * This routine injects the following CPU-detected UEs:
 *	OC_L2UE, OC_L2UETL1
 */
int
oc_l2ue(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip = mdatap->m_cip;
	uint_t		myid, o_buf_sz, buf_sz;
	int		ret = 0;
	uint64_t	command = iocp->ioc_command;
	caddr_t		buf, o_buf;
	uint_t		*writep, *readp, i;
	/* LINTED */
	uint_t		tmp;
	char		*fname = "oc_l2ue";

	/* allocate a buffer of size twice the L2 linesize */
	o_buf_sz = cip->c_l2_linesize << 1;
	if ((o_buf = kmem_alloc(o_buf_sz, KM_NOSLEEP)) == NULL) {
		DPRINTF(0, "%s: couldn't allocate memory\n", fname);
		return (ENOMEM);
	}
	buf_sz = o_buf_sz >> 1;
	/* get a L2 linesize aligned address */
	buf = (caddr_t)(((uintptr_t)o_buf + buf_sz) & ~(buf_sz - 1));
	writep = (uint_t *)buf;
	readp = writep;

	kpreempt_disable();

	myid = getprocessorid();

	/*
	 * Suspend sibling strands and sibling core strands.
	 */
	oc_xc_cpu(OC_SIBLING_STRAND(CPU->cpu_id), 1);
	oc_xc_chip(CPU->cpu_id, 1);

	/*
	 * Inject the error.
	 *
	 * First, flush L2$ and fill it with random data to inject
	 * a L2$ error.
	 *
	 * Set the appropriate field of ASI_ERR_INJCT register:
	 *	Bit	Field	Description
	 *	2	SX 	L2$ UE
	 */
	cpu_flush_ecache();
	oc_set_err_injct_sx();

	/*
	 * write 4 bytes to the buffer
	 */
	writep[1] = 1;

	membar_sync();

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		goto resume;
	}

	/*
	 * Invoke the error.
	 * Since we don't know exactly where in the L2$
	 * the error was injected, we access the entire
	 * buffer to trigger the error.
	 */
	switch (command) {
	case OC_L2UE:
		for (i = 0; i < buf_sz/sizeof (uint_t); i++)
			tmp = readp[i];
		break;

	case OC_L2UETL1:
		xt_one(myid, (xcfunc_t *)oc_inv_l12uetl1,
		    (uint64_t)buf, (uint64_t)buf_sz);
		break;
	default:
		DPRINTF(0, "%s: unsupported command\n", fname);
		ret = EINVAL;
		goto resume;
	}

resume:
	/* resume strands */
	oc_xc_cpu(OC_SIBLING_STRAND(CPU->cpu_id), 0);
	oc_xc_chip(CPU->cpu_id, 0);

	kpreempt_enable();
	kmem_free(o_buf, o_buf_sz);

	return (ret);
}

/*
 * This routine injects the following CPU-detected UEs:
 *	OC_IUGR
 */
/*ARGSUSED*/
int
oc_iugr(mdata_t *mdatap)
{
	int		ret = 0;
	uint_t		i;

	kpreempt_disable();

	/*
	 * Inject and invoke the error.
	 *
	 * Set the appropriate field of ASI_ERR_INJCT register:
	 *	Bit	Field	Description
	 *	0	GR	%rn parity error
	 */
	for (i = 0; i < 10; i++) {
		oc_inj_err_rn();
		oc_inv_err_rn();
	}

	kpreempt_enable();

	return (ret);
}

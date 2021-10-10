/*
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This is the common file for the memtest (CPU/Memory Error Injector) driver.
 * Its main purpose is to inject and optionally invoke processor specific
 * memory, cache, and some other errors.
 *
 * Each error generation function follows the same basic scheme for
 * injecting errors. The basic scheme is to retrieve any parameters
 * passed in, disable kernel preemption, flush the caches, inject
 * the error, then, if we're interested in causing kernel data or
 * instruction errors, invoke the error by doing a read, write or
 * instruction execution. In the case of user address space errors
 * we just return and let the companion user space program do the
 * appropriate access to invoke the error.
 *
 * See "uts/sun4u/sys/memtestio*.h", "uts/sun4v/sys/memtestio*.h" and/or
 * the command arrays in the processor specific files for a list of error
 * cases that can currently be generated.
 */

#include <sys/memtestio.h>
#include <sys/memtest.h>

extern const int	_ncpu;

/*
 * Prototypes for the basic driver module functions.
 */
int memtest_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
int memtest_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
int memtest_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
			void **result);
int memtest_open(dev_t *dev, int openflags, int otyp, cred_t *credp);
int memtest_close(dev_t dev, int openflags, int otyp, cred_t *credp);
int memtest_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
			int *rvalp);

/*
 * Static routines located in this file.
 */
static	void		memtest_do_cmd_xcfunc(uint64_t arg1, uint64_t arg2);
static	void		memtest_enable_errors_xcfunc(uint64_t arg1,
				uint64_t arg2);
static	int		memtest_free_kernel_mappings(kmap_t *);
static	int		memtest_free_resources(mdata_t *mdatap);
static	void		memtest_k_cp_producer(mdata_t *mdatap);

/*
 * For debug purposes.
 */
static int	memtest_use_prom_printf	= 0;

/*
 * Opaque handle top of state structs.
 */
static	void	*state_head;

/*
 * This global controls certain internal operations of the injector.
 */
uint_t	memtest_flags = MFLAGS_DEFAULT;

/*
 * The value of this debug variable determines the level of
 * debugging enabled in the driver.
 */
uint_t	memtest_debug = 0;

/*
 * Debug decode strings for the sub command types for MEMTEST_MEMREQ ioctl.
 */
static char		*m_subcmds[]	= {"INVALID", "UVA_TO_PA",
					"UVA_GET_ATTR", "UVA_SET_ATTR",
					"UVA_LOCK", "UVA_UNLOCK",
					"KVA_TO_PA", "PA_TO_UNUM",
					"FIND_FREE_PAGES", "LOCK_FREE_PAGES",
					"LOCK_PAGES", "UNLOCK_PAGES",
					"IDX_TO_PA", "RA_TO_PA"};

static struct cb_ops memtest_cb_ops = {
	memtest_open,
	memtest_close,
	nulldev,			/* not a block driver	*/
	nodev,				/* no print routine	*/
	nodev,				/* no dump routine	*/
	nodev,				/* no read routine	*/
	nodev,				/* no write		*/
	memtest_ioctl,
	nodev,				/* no devmap routine	*/
	nulldev,			/* no mmap routine	*/
	nulldev,			/* no segmap routine	*/
	nochpoll,			/* no chpoll routine	*/
	ddi_prop_op,
	0,				/* not a STREAMS driver	*/
	D_NEW | D_MP,			/* safe for mt/mp	*/
};

struct dev_ops memtest_ops = {
	DEVO_REV,
	0,				/* device reference count	*/
	memtest_getinfo,
	nulldev,
	nulldev,			/* device probe for non-self-id */
	memtest_attach,
	memtest_detach,
	nodev,				/* device reset routine */
	&memtest_cb_ops,
	NULL,				/* bus operations	*/
	NULL,				/* power */
	ddi_quiesce_not_needed,		/* quiesce */
};

static  struct modldrv modldrv = {
	&mod_driverops,				/* driver module */
	"CPU/Memory Error Injector",		/* module name */
	&memtest_ops				/* driver ops */
};

static  struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv, NULL
};


int
_init(void)
{
	int	status;

	if ((status = ddi_soft_state_init(&state_head,
	    sizeof (memtest_t), 1)) != 0)
		return (status);

	if ((status = mod_install(&modlinkage)) != 0)
		ddi_soft_state_fini(&state_head);

	return (status);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	int	status;

	if ((status = mod_remove(&modlinkage)) != 0)
		return (status);

	ddi_soft_state_fini(&state_head);

	return (status);
}

int
memtest_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	memtest_t	*memtestp;
	int		i, instance;
	char		str[40];

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	instance = ddi_get_instance(dip);

	if (ddi_soft_state_zalloc(state_head, instance) != 0)
		return (DDI_FAILURE);

	if ((memtestp = (memtest_t *)ddi_get_soft_state(state_head,
	    instance)) == NULL) {
		/*
		 * Free soft state here.
		 */
		ddi_soft_state_free(state_head, instance);
		return (DDI_FAILURE);
	}

	ddi_set_driver_private(dip, (caddr_t)memtestp);
	memtestp->m_dip = dip;

	memtestp->m_cip = kmem_zalloc(sizeof (cpu_info_t) * _ncpu, KM_SLEEP);

	memtestp->m_iocp = kmem_zalloc(sizeof (ioc_t), KM_SLEEP);

	memtestp->m_sip = kmem_zalloc(sizeof (system_info_t), KM_SLEEP);

	/*
	 * Allocate room for all thread data structures and
	 * initialize what we can at this point.
	 */
	for (i = 0; i < MAX_NTHREADS; i++) {
		memtestp->m_mdatap[i] = kmem_zalloc(sizeof (mdata_t), KM_SLEEP);
		memtestp->m_mdatap[i]->m_iocp = memtestp->m_iocp;
		memtestp->m_mdatap[i]->m_sip = memtestp->m_sip;
		memtestp->m_mdatap[i]->m_cip = kmem_zalloc(sizeof (cpu_info_t),
		    KM_SLEEP);
		memtestp->m_mdatap[i]->m_scrubp = kmem_zalloc(
		    sizeof (scrub_info_t), KM_SLEEP);
		memtestp->m_mdatap[i]->m_threadno = i;
		memtestp->m_mdatap[i]->m_memtestp = memtestp;

		if (memtest_debug > 0) {
			(void) sprintf(str, "memtest_attach: thread=%d", i);
			memtest_dump_mdata(memtestp->m_mdatap[i], str);
		}
	}

	/*
	 * Initialize the mutex which is used later to lock
	 * our instance structure.
	 */
	mutex_init(&memtestp->m_mutex, NULL, MUTEX_DRIVER, NULL);

	/*
	 * Create the device node.
	 */
	if (ddi_create_minor_node(dip, ddi_get_name(dip), S_IFCHR, instance,
	    DDI_PSEUDO, NULL) == DDI_FAILURE) {
		mutex_destroy(&memtestp->m_mutex);
		ddi_soft_state_free(state_head, instance);
		return (DDI_FAILURE);
	}

	ddi_report_dev(dip);

	return (DDI_SUCCESS);
}

int
memtest_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	memtest_t	*memtestp;
	int		instance, i;

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	instance = ddi_get_instance(dip);
	if ((memtestp = (memtest_t *)ddi_get_soft_state(state_head,
	    instance)) == NULL) {
		cmn_err(CE_CONT, "memtest_detach: NULL soft state for "
		    "instance %d\n", instance);
		return (DDI_FAILURE);
	}

	mutex_enter(&memtestp->m_mutex);

	/*
	 * Remove the minor node created in attach
	 */
	ddi_remove_minor_node(dip, NULL);

	(void) memtest_free_kernel_mappings(memtestp->m_kmapp);
	(void) memtest_unlock_user_pages(memtestp->m_uplockp);
	(void) memtest_unlock_kernel_pages(memtestp->m_kplockp);
	mutex_exit(&memtestp->m_mutex);

	/*
	 * Free data structures which were allocated in the attach.
	 */
	kmem_free(memtestp->m_cip, sizeof (cpu_info_t) * _ncpu);

	kmem_free(memtestp->m_iocp, sizeof (ioc_t));
	kmem_free(memtestp->m_sip, sizeof (system_info_t));

	for (i = 0; i < MAX_NTHREADS; i++) {
		kmem_free(memtestp->m_mdatap[i]->m_scrubp,
		    sizeof (scrub_info_t));
		kmem_free(memtestp->m_mdatap[i]->m_cip, sizeof (cpu_info_t));
		kmem_free(memtestp->m_mdatap[i], sizeof (mdata_t));
	}

	/*
	 * Free the mutex and soft state structs.
	 */
	mutex_destroy(&memtestp->m_mutex);
	ddi_soft_state_free(state_head, instance);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
int
memtest_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd,
	void *arg, void **result)
{
	memtest_t	*memtestp;
	int		ret;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		memtestp = (memtest_t *)ddi_get_soft_state(state_head,
		    getminor((dev_t)arg));

		if (memtestp == NULL) {
			*result = NULL;
			ret = DDI_FAILURE;
		} else {
			mutex_enter(&memtestp->m_mutex);
			*result = memtestp->m_dip;
			mutex_exit(&memtestp->m_mutex);
			ret = DDI_SUCCESS;
		}
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)(uintptr_t)getminor((dev_t)arg);
		ret = DDI_SUCCESS;
		break;

	default:
		*result = NULL;
		ret = DDI_FAILURE;
	}

	return (ret);
}

/*ARGSUSED*/
int
memtest_open(dev_t *dev, int openflags, int otyp, cred_t *credp)
{
	memtest_t	*memtestp;

	memtestp = (memtest_t *)ddi_get_soft_state(state_head, getminor(*dev));

	if (memtestp == NULL)
		return (ENXIO);

	if (otyp != OTYP_CHR)
		return (EINVAL);

	mutex_enter(&memtestp->m_mutex);

	/*
	 * Only allow a single open for now.
	 */
	if (memtestp->m_open != FALSE) {
		mutex_exit(&memtestp->m_mutex);
		return (EBUSY);
	}

	memtestp->m_open = TRUE;
	mutex_exit(&memtestp->m_mutex);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
int
memtest_close(dev_t dev, int openflags, int otyp, cred_t *credp)
{
	memtest_t	*memtestp;

	memtestp = (memtest_t *)ddi_get_soft_state(state_head, getminor(dev));

	mutex_enter(&memtestp->m_mutex);

	memtestp->m_open = FALSE;

	mutex_exit(&memtestp->m_mutex);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
int
memtest_ioctl(dev_t dev, int ioctl_cmd, intptr_t arg, int mode,
	cred_t *credp, int *rvalp)
{
	memtest_t	*memtestp;
	mdata_t		*mdatap;
	ioc_t		*iocp;
	cpu_info_t	*cip, ci;
	system_info_t	*sip;
	mem_req_t	mr;
	proc_t		*procp;
	cmd_t		*cmdp, **cmdpp;
	kplock_t	*kplockp;
	uplock_t	*uplockp;
	uint64_t	command, afsr[2];
	uint_t		debug;
	config_file_t	cfilep;

	int		found, len, cpuid, npages;
	int		*ip;
	int		ret = 0;
	char		str[80];

	(void) sprintf(str, "memtest_ioctl");

	DPRINTF(3, "%s: ioctl_cmd=0x%x, arg=0x%x, mode=0x%x\n",
	    str, ioctl_cmd, arg, mode);

	/*
	 * Get primary thread's data pointer.
	 */
	memtestp = (memtest_t *)ddi_get_soft_state(state_head, getminor(dev));
	mdatap = memtestp->m_mdatap[0];

	iocp = mdatap->m_iocp;
	sip = mdatap->m_sip;
	cip = mdatap->m_cip;

	DPRINTF(3, "%s: memtestp=0x%p, mdatap=0x%p, iocp=0x%p, sip=0x%p, "
	    "cip=0x%p\n", str, memtestp, mdatap, iocp, sip, cip);

	/*
	 * First set debug if requested.
	 */
	if (ioctl_cmd == MEMTEST_SETDEBUG) {
		(void) sprintf(str, "memtest_ioctl: SETDEBUG");
		if (ddi_copyin((void *)arg, &debug, sizeof (debug), 0) != 0) {
			DPRINTF(0, "%s: copyin failed\n", str);
			return (EFAULT);
		}
		if (debug != memtest_debug) {
			memtest_debug = debug;
			DPRINTF(0, "%s: debug=%d\n", str, memtest_debug);
		}
		return (0);
	}

	/*
	 * Initialize common system information structure.
	 */
	sip->s_ncpus = ncpus;
	sip->s_ncpus_online = ncpus_online;
	sip->s_maxcpuid = _ncpu;

	/*
	 * Initialize some thread specific information for the primary thread.
	 * This fills in the opsvecs, command, and cpuinfo structs.
	 */
	if (ret = memtest_init(mdatap))
		return (ret);

	switch (ioctl_cmd) {
	case MEMTEST_GETCPUINFO:

		/*
		 * Prevent this thread possibly being preempted and
		 * migrating to a diferent cpu.
		 */
		kpreempt_disable();

		(void) sprintf(str, "memtest_ioctl: GETCPUINFO");

		/*
		 * Get the user data struct to see which cpu
		 * the request is for.
		 */
		if (ddi_copyin((void *)arg, &ci, sizeof (cpu_info_t), 0) != 0) {
			DPRINTF(0, "%s: copyin failed\n", str);
			kpreempt_enable();
			return (EFAULT);
		}

		cpuid = ci.c_cpuid;
		DPRINTF(3, "%s: info requested for cpuid=%d\n", str, cpuid);

		/*
		 * If the request is for a different cpu than we already got
		 * info for above then do a cross-call to get the info.
		 */
		if ((cpuid != -1) && (cpuid != cip->c_cpuid)) {
			DPRINTF(3, "%s: cross calling cpuid=%d, "
			    "mdatap=0x%p\n", str, cpuid, mdatap);
			xc_one(cpuid, (xcfunc_t *)memtest_init_thread_xcfunc,
			    (uint64_t)mdatap, (uint64_t)&ret);
			if (ret != 0) {
				kpreempt_enable();
				return (ret);
			}
			/*
			 * Sanity check.
			 * Verify that the info is for the cpu requested.
			 */
			if (cip->c_cpuid != cpuid) {
				DPRINTF(0, "%s: cpuid of data=%d does not "
				    "match what was requested=%d\n",
				    str, cip->c_cpuid, cpuid);
				kpreempt_enable();
				return (EIO);
			}
		}

		kpreempt_enable();

		DPRINTF(3, "%s: cip=0x%p, cpuid=%d, cpuver=0x%llx "
		    "(mfg=0x%x, impl=0x%llx, mask=0x%x)\n",
		    str, cip, cip->c_cpuid, cip->c_cpuver,
		    CPU_MFG(cip->c_cpuver), CPU_IMPL(cip->c_cpuver),
		    CPU_MASK(cip->c_cpuver));
		DPRINTF(3, "%s: D$: size=0x%x, linesize=0x%x, assoc=%d\n",
		    str, cip->c_dc_size, cip->c_dc_linesize,
		    cip->c_dc_assoc);
		DPRINTF(3, "%s: I$: size=0x%x, linesize=0x%x, assoc=%d\n",
		    str, cip->c_ic_size, cip->c_ic_linesize,
		    cip->c_ic_assoc);
		DPRINTF(3, "%s: L2$: size=0x%x, linesize=0x%x, "
		    "sublinesize=0x%x, assoc=%d, flushsize=0x%x\n",
		    str, cip->c_l2_size, cip->c_l2_linesize,
		    cip->c_l2_sublinesize,
		    cip->c_l2_assoc, cip->c_l2_flushsize);
		DPRINTF(3, "%s: L3$: size=0x%x, linesize=0x%x, "
		    "sublinesize=0x%x, assoc=%d, flushsize=0x%x\n",
		    str, cip->c_l3_size, cip->c_l3_linesize,
		    cip->c_l3_sublinesize,
		    cip->c_l3_assoc, cip->c_l3_flushsize);
		DPRINTF(3, "%s: Mem: flags=0x%llx, start=0x%llx, size=0x%llx\n",
		    str, cip->c_mem_flags, cip->c_mem_start,
		    cip->c_mem_size);
		DPRINTF(3, "%s: sys mode reg=0x%llx\n", str, cip->c_sys_mode);
		DPRINTF(3, "%s: L2 ctl reg=0x%llx\n", str, cip->c_l2_ctl);

		/*
		 * Copy this cpus info into the driver level cip array then
		 * send the data to the user program.
		 */
		memtestp->m_cip[cpuid].c_cpuid = cip->c_cpuid;
		memtestp->m_cip[cpuid].c_core_id = cip->c_core_id;
		memtestp->m_cip[cpuid].c_cpuver = cip->c_cpuver;
		memtestp->m_cip[cpuid].c_ecr = cip->c_ecr;
		memtestp->m_cip[cpuid].c_secr = cip->c_secr;
		memtestp->m_cip[cpuid].c_eer = cip->c_eer;
		memtestp->m_cip[cpuid].c_dcr = cip->c_dcr;
		memtestp->m_cip[cpuid].c_dcucr = cip->c_dcucr;
		memtestp->m_cip[cpuid].c_dc_size = cip->c_dc_size;
		memtestp->m_cip[cpuid].c_dc_linesize = cip->c_dc_linesize;
		memtestp->m_cip[cpuid].c_dc_assoc = cip->c_dc_assoc;
		memtestp->m_cip[cpuid].c_ic_size = cip->c_ic_size;
		memtestp->m_cip[cpuid].c_ic_linesize = cip->c_ic_linesize;
		memtestp->m_cip[cpuid].c_ic_assoc = cip->c_ic_assoc;
		memtestp->m_cip[cpuid].c_l2_size = cip->c_l2_size;
		memtestp->m_cip[cpuid].c_l2_linesize = cip->c_l2_linesize;
		memtestp->m_cip[cpuid].c_l2_sublinesize =
		    cip->c_l2_sublinesize;
		memtestp->m_cip[cpuid].c_l2_assoc = cip->c_l2_assoc;
		memtestp->m_cip[cpuid].c_l2_flushsize = cip->c_l2_flushsize;
		memtestp->m_cip[cpuid].c_l3_size = cip->c_l3_size;
		memtestp->m_cip[cpuid].c_l3_linesize = cip->c_l3_linesize;
		memtestp->m_cip[cpuid].c_l3_sublinesize =
		    cip->c_l3_sublinesize;
		memtestp->m_cip[cpuid].c_l3_assoc = cip->c_l3_assoc;
		memtestp->m_cip[cpuid].c_l3_flushsize = cip->c_l3_flushsize;
		memtestp->m_cip[cpuid].c_shared_caches = cip->c_shared_caches;
		memtestp->m_cip[cpuid].c_already_chosen =
		    cip->c_already_chosen;
		memtestp->m_cip[cpuid].c_offlined = cip->c_offlined;
		memtestp->m_cip[cpuid].c_mem_flags = cip->c_mem_flags;
		memtestp->m_cip[cpuid].c_mem_start = cip->c_mem_start;
		memtestp->m_cip[cpuid].c_mem_size = cip->c_mem_size;
		memtestp->m_cip[cpuid].c_sys_mode = cip->c_sys_mode;
		memtestp->m_cip[cpuid].c_l2_ctl = cip->c_l2_ctl;

		if (ddi_copyout(cip, (void *)arg,
		    sizeof (cpu_info_t), 0) != 0) {
			DPRINTF(0, "%s: copyout failed\n", str);
			return (EFAULT);
		} else {
			return (0);
		}

	case MEMTEST_GETSYSINFO:

		(void) sprintf(str, "memtest_ioctl: GETSYSINFO");

		DPRINTF(1, "%s: ncpus=%d, ncpus_online=%d, maxcpuid=%d\n",
		    str, sip->s_ncpus, sip->s_ncpus_online,
		    sip->s_maxcpuid);

		if (ddi_copyout(sip, (void *)arg, sizeof (system_info_t),
		    0) != 0) {
			DPRINTF(0, "%s: copyout failed\n", str);
			return (EFAULT);
		} else {
			return (0);
		}

	case MEMTEST_INJECT_ERROR:

		(void) sprintf(str, "memtest_ioctl: INJECT_ERROR");

		/*
		 * Get commands list array.
		 */
		cmdpp = mdatap->m_cmdpp;

		if (ddi_copyin((caddr_t)arg, (caddr_t)iocp,
		    sizeof (ioc_t), mode) != 0) {
			DPRINTF(0, "%s: ddi_copyin() failed\n", str);
			return (EFAULT);
		}

		command = iocp->ioc_command;

		/*
		 * Sanity check the command.
		 */
		if (ret = memtest_check_command(IOC_COMMAND(iocp)))
			return (ret);

		/*
		 * Look up the command.
		 */
		found = 0;
		for (cmdpp = mdatap->m_cmdpp; (*cmdpp != NULL) && !found;
		    cmdpp++) {
			DPRINTF(4, "%s: cmdpp=0x%p\n", str, cmdpp);
			for (cmdp = *cmdpp; cmdp->c_func != NULL; cmdp++) {
				DPRINTF(4, "%s: cmdp=0x%p, "
				    "cmdp->c_command=0x%llx\n",
				    str, cmdp, cmdp->c_command);
				if (cmdp->c_command == command) {
					found++;
					break;
				}
			}
		}

		if (!found) {
			DPRINTF(0, "%s: invalid command 0x%llx\n",
			    str, command);
			return (EINVAL);
		}

		mdatap->m_cmdp = cmdp;

		/*
		 * Do some pre-test initialization.
		 */
		if (ret = memtest_pre_test(mdatap))
			return (ret);

		/*
		 * Check the command info again since it may
		 * have changed due to option overrides.
		 */
		if (ret = memtest_check_command(IOC_COMMAND(iocp)))
			return (ret);

#ifndef	sun4v
		/*
		 * Enable kernel verbose/debug flags if requested.
		 */
		if (F_VERBOSE(iocp)) {
			if (ce_show_data < 1)
				ce_show_data = 1;
			if (aft_verbose < 1)
				aft_verbose = 1;
			if (ce_verbose_memory < 2)
				ce_verbose_memory = 2;
			if (ce_verbose_other < 2)
				ce_verbose_other = 2;
		}

		if (F_KERN_DEBUG(iocp)) {
			if (ce_debug < 1)
				ce_debug = 1;
			if (ue_debug < 1)
				ue_debug = 1;
		}
#endif	/* sun4v */

		/*
		 * Execute the command.
		 */
		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "memtest_ioctl: executing cmd=%s="
			    "0x%08x.%08x on CPU 0x%x\n",
			    iocp->ioc_cmdstr,
			    PRTF_64_TO_32(iocp->ioc_command),
			    getprocessorid());
		}

		if (memtest_debug > 0) {
			memtest_dump_mdata(mdatap, "memtest_ioctl");
		}

		if (ret = memtest_do_cmd(mdatap))
			(void) memtest_post_test(mdatap);
		else
			ret = memtest_post_test(mdatap);

		return (ret);

	case MEMTEST_MEMREQ:

		if (ddi_copyin((void *)arg, &mr, sizeof (mem_req_t), 0) != 0) {
			DPRINTF(0, "memtest_ioctl: MEMREQ: copyin "
			    "failed\n", str);
			return (EFAULT);
		}

		(void) sprintf(str, "memtest_ioctl: MEMREQ %s",
		    m_subcmds[mr.m_cmd]);

		DPRINTF(1, "%s: pid=%d\n", str, mr.m_pid);

		/*
		 * pid of -1 means use current process.
		 */
		if (mr.m_pid == -1) {
			if ((procp = ttoproc(curthread)) == NULL) {
				DPRINTF(0, "%s: NULL procp\n", str);
				return (EIO);
			}
		} else {
			mutex_enter(&pidlock);
			procp = prfind(mr.m_pid);
			mutex_exit(&pidlock);
			if (procp == NULL) {
				DPRINTF(0, "%s: invalid pid=%d\n",
				    str, mr.m_pid);
				return (EINVAL);
			}
		}

		DPRINTF(1, "%s: procp=0x%p\n", str, procp);

		npages = mr.m_size / MMU_PAGESIZE;

		switch (mr.m_cmd) {
		case MREQ_UVA_TO_PA:
			DPRINTF(1, "%s: uvaddr=0x%llx\n", str, mr.m_vaddr);
			mr.m_paddr1 = (uint64_t)memtest_uva_to_pa(
			    (caddr_t)mr.m_vaddr, procp);
			if (mr.m_paddr1 == -1) {
				DPRINTF(0, "%s: couldn't translate user "
				    "virtual address 0x%llx\n",
				    str, mr.m_vaddr);
				return (ENXIO);
			}
			break;
		case MREQ_UVA_GET_ATTR:
			DPRINTF(1, "%s: uvaddr=0x%llx, as=0x%p, hat=0x%p\n",
			    str, mr.m_vaddr, procp->p_as,
			    procp->p_as->a_hat);
			if (hat_getattr(procp->p_as->a_hat,
			    (caddr_t)mr.m_vaddr, &mr.m_attr) == -1) {
				DPRINTF(0, "%s: couldn't get attributes "
				    "for user virtual address 0x%llx\n",
				    str, mr.m_vaddr);
				return (ENXIO);
			}
			break;
		case MREQ_UVA_SET_ATTR:
			DPRINTF(1, "%s: uvaddr=0x%llx, as=0x%p, hat=0x%p, "
			    "size=0x%llx, attr=0x%x\n",
			    str, mr.m_vaddr, procp->p_as,
			    procp->p_as->a_hat, mr.m_size, mr.m_attr);
			rw_enter(&procp->p_as->a_lock, RW_READER);
			hat_chgattr(procp->p_as->a_hat, (caddr_t)mr.m_vaddr,
			    mr.m_size, mr.m_attr);
			rw_exit(&procp->p_as->a_lock);
			break;
		case MREQ_KVA_TO_PA:
			DPRINTF(1, "%s: kvaddr=0x%llx\n", str, mr.m_vaddr);
			if ((mr.m_paddr1 =
			    memtest_kva_to_pa((caddr_t)mr.m_vaddr)) == -1) {
				DPRINTF(0, "%s: couldn't translate "
				    "kernel virtual address 0x%llx\n",
				    str, mr.m_vaddr);
				return (ENXIO);
			}
			break;
		case MREQ_LOCK_FREE_PAGES:
			DPRINTF(1, "%s: paddr1=0x%llx, paddr2=0x%llx, "
			    "npages=%d\n",
			    str, mr.m_paddr1, mr.m_paddr2, npages);
			ret = memtest_mem_request(memtestp, &mr.m_paddr1,
			    &mr.m_paddr2, npages, MREQ_LOCK_FREE_PAGES);
			break;
		case MREQ_FIND_FREE_PAGES:
			DPRINTF(1, "%s: paddr1=0x%llx, paddr2=0x%llx, "
			    "npages=%d\n",
			    str, mr.m_paddr1, mr.m_paddr2, npages);
			ret = memtest_mem_request(memtestp, &mr.m_paddr1,
			    &mr.m_paddr2, npages, MREQ_FIND_FREE_PAGES);
			break;
		case MREQ_IDX_TO_PA:
			DPRINTF(1, "%s: find physical range for index=0x%llx, "
			    "npages=%d\n", str, mr.m_index, npages);
			ret = memtest_idx_to_paddr(memtestp, &mr.m_paddr1,
			    &mr.m_paddr2, mr.m_index, mr.m_way, mr.m_subcmd);
			break;
		case MREQ_PA_TO_UNUM:
			afsr[0] = (uint64_t)-1;
			afsr[1] = 0;
			DPRINTF(1, "%s: synd=0x%x, afsr=0x%lx, afar=0x%lx, "
			    "max_len=%d\n", str, mr.m_synd, afsr[0],
			    mr.m_paddr1, UNUM_NAMLEN);
			ret = cpu_get_mem_name(mr.m_synd, afsr, mr.m_paddr1,
			    mr.m_str, UNUM_NAMLEN, &len);
			DPRINTF(1, "%s: len=%d, unum=%s\n",
			    str, len, (ret == 0 ? mr.m_str : "unknown"));
			break;
		case MREQ_LOCK_PAGES:
			DPRINTF(1, "%s: paddr1=0x%llx, paddr2=0x%llx, "
			    "npages=%d\n",
			    str, mr.m_paddr1, mr.m_paddr2, npages);
			ret = memtest_mem_request(memtestp, &mr.m_paddr1,
			    &mr.m_paddr2, npages, MREQ_LOCK_PAGES);
			break;
		case MREQ_UNLOCK_PAGES:
			DPRINTF(1, "%s: paddr1=0x%llx npages=%d\n",
			    str, mr.m_paddr1, npages);
			mutex_enter(&memtestp->m_mutex);
			kplockp = memtestp->m_kplockp;
			/*
			 * Search thru the master list looking for a match.
			 */
			while (kplockp != NULL) {
				DPRINTF(2, "%s: kplockp=0x%p, k_paddr=0x%llx, "
				    "k_npages=0x%d\n", str, kplockp,
				    kplockp->k_paddr, kplockp->k_npages);
				if ((kplockp->k_paddr == mr.m_paddr1) &&
				    (kplockp->k_npages == npages))
					break;
				kplockp = kplockp->k_next;
			}
			/*
			 * No match, return error.
			 */
			if (kplockp == NULL) {
				DPRINTF(0, "%s: couldn't find locked kernel "
				    "page, paddr=0x%llx, npages=0x%d\n",
				    str, mr.m_paddr1, npages);
				mutex_exit(&memtestp->m_mutex);
				return (ENXIO);
			}
			/*
			 * Got a match.
			 * Unlink the entry from the master list.
			 */
			if (kplockp->k_prev != NULL)
				kplockp->k_prev->k_next = kplockp->k_next;
			if (kplockp->k_next != NULL)
				kplockp->k_next->k_prev = kplockp->k_prev;
			if (kplockp == memtestp->m_kplockp)
				memtestp->m_kplockp = kplockp->k_next;
			/*
			 * Finally, unlock the pages.
			 */
			if (memtest_unlock_kernel_pages(kplockp) != 0) {
				mutex_exit(&memtestp->m_mutex);
				return (EIO);
			}
			mutex_exit(&memtestp->m_mutex);
			break;
		case MREQ_UVA_LOCK:
			DPRINTF(1, "%s: uvaddr=0x%llx size=0x%llx\n",
			    str, mr.m_vaddr, mr.m_size);
			ret = memtest_lock_user_pages(memtestp,
			    (caddr_t)mr.m_vaddr, mr.m_size, NULL);
			break;
		case MREQ_UVA_UNLOCK:
			DPRINTF(1, "%s: uvaddr=0x%llx size=0x%llx\n",
			    str, mr.m_vaddr, mr.m_size);
			mutex_enter(&memtestp->m_mutex);
			for (uplockp = memtestp->m_uplockp; uplockp != NULL;
			    uplockp = uplockp->p_next) {
				if ((mr.m_vaddr ==
				    (uint64_t)uplockp->p_uvaddr) &&
				    (mr.m_size == (uint64_t)uplockp->p_size) &&
				    (ttoproc(curthread) == uplockp->p_procp))
					break;
			}
			if (uplockp == NULL) {
				DPRINTF(0, "%s: couldn't find locked user "
				    "page, uvaddr=0x%llx, size=0x%x, "
				    "procp=0x%p\n", str,
				    mr.m_vaddr, mr.m_size,
				    ttoproc(curthread));
				mutex_exit(&memtestp->m_mutex);
				return (ENXIO);
			}
			ret = memtest_unlock_user_pages(uplockp);
			mutex_exit(&memtestp->m_mutex);
			break;
		default:
			ret = memtest_arch_mreq(&mr);
			if (ret == EINVAL) {
				DPRINTF(0, "%s: invalid memory request = %d\n",
				    str, mr.m_cmd);
			}
		}
		if (ret == 0) {
			if (ddi_copyout(&mr, (void *)arg, sizeof (mem_req_t),
			    0) != 0) {
				DPRINTF(0, "%s: copyout failed\n", str);
				return (EFAULT);
			}
		}
		return (ret);

	case MEMTEST_SETKVARS:

		(void) sprintf(str, "memtest_ioctl: SETKVARS");

		if (ddi_copyin((void *)arg, &cfilep,
		    sizeof (config_file_t), 0) != 0) {
			DPRINTF(0, "%s: ddi_copyin() failed\n", str);
			return (EFAULT);
		}

		/*
		 * Save or restore the kernel vars by copying to/from struct.
		 */
		if (cfilep.rw == B_FALSE) {
			DPRINTF(1, "%s: saving kernel variables\n", str);

			cfilep.kv_ce_debug = ce_debug;
			cfilep.kv_ce_show_data = ce_show_data;
			cfilep.kv_ce_verbose_memory = ce_verbose_memory;
			cfilep.kv_ce_verbose_other = ce_verbose_other;
			cfilep.kv_ue_debug = ue_debug;
			cfilep.kv_aft_verbose = aft_verbose;

			/*
			 * Save and set this variable to allow tests like
			 * "kdbe" to use non-cached memory translations to
			 * generate errors. Otherwise, the mmu hat code may
			 * panic on DEBUG kernels.
			 */
			if ((ip = (int *)kobj_getsymvalue(
			    "sfmmu_allow_nc_trans", 1)) != NULL) {
				cfilep.kv_sfmmu_allow_nc_trans = *ip;
				*ip = 1;
			}

			cfilep.saved = B_TRUE;

		} else if (cfilep.rw == B_TRUE) {
			DPRINTF(1, "%s: restoring kernel variables\n", str);

			ce_debug = cfilep.kv_ce_debug;
			ce_show_data = cfilep.kv_ce_show_data;
			ce_verbose_memory = cfilep.kv_ce_verbose_memory;
			ce_verbose_other = cfilep.kv_ce_verbose_other;
			ue_debug = cfilep.kv_ue_debug;
			aft_verbose = cfilep.kv_aft_verbose;

			if ((ip = (int *)kobj_getsymvalue(
			    "sfmmu_allow_nc_trans", 1)) != NULL) {
				*ip = cfilep.kv_sfmmu_allow_nc_trans;
			}

			cfilep.saved = B_FALSE;
		} else {
			DPRINTF(0, "%s: Unrecognized command = 0x%d\n",
			    str, cfilep.rw);
			return (EINVAL);
		}

		if (ddi_copyout(&cfilep, (void *)arg,
		    sizeof (config_file_t), 0) != 0) {
			DPRINTF(0, "%s: ddi_copyout() failed\n", str);
			return (EFAULT);
		}

		return (0);

	case MEMTEST_ENABLE_ERRORS:

		(void) sprintf(str, "memtest_ioctl: ENABLE_ERRORS");

		/*
		 * Get commands list array.
		 */
		cmdpp = mdatap->m_cmdpp;

		if (ddi_copyin((caddr_t)arg, (caddr_t)iocp,
		    sizeof (ioc_t), mode) != 0) {
			DPRINTF(0, "%s: ddi_copyin() failed\n", str);
			return (EIO);
		}

		command = iocp->ioc_command;

		/*
		 * Enable all errors on all procs.
		 */
		if (memtest_enable_errors(mdatap) != 0) {
			DPRINTF(0, "%s: memtest_enable_errors() FAILED\n", str);
			return (EIO);
		}

		return (0);
	/*
	 * Debug ioctl to flush and/or clear processor caches.
	 */
	case MEMTEST_FLUSH_CACHE:
		/*
		 * Perform a flushall on the processor we are running on.
		 */
		OP_FLUSHALL_CACHES(mdatap);
		return (0);
	default:
		DPRINTF(0, "%s: invalid ioctl = 0x%x\n", str, ioctl_cmd);
		return (ENOTTY);
	}
}

/*
 * ************************************************************************
 * The following block of routines are the common high level test routines.
 * ************************************************************************
 */

/*
 * This routine generates an L2 cache error while copying in user data.
 */
int
memtest_copyin_l2_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	tmp;
	int		ret;
	char		*fname = "memtest_copyin_l2_err";

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error.
	 */
	if (ret = memtest_inject_l2cache(mdatap)) {
		return (ret);
	}

	/*
	 * Return now if we don't want to invoke the error.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		return (0);
	}

	/*
	 * Do the copyin to invoke the error.
	 * Note that the copyin() routine does not return an error
	 * indication if the error was corrected.
	 */
	ret = copyin(mdatap->m_uvaddr_a, &tmp, sizeof (uint64_t));

	DPRINTF(2, "%s: copyin(uvaddr=0x%p, kvaddr=0x%p, size=%d) return=%d\n",
	    fname, mdatap->m_uvaddr_a, &tmp, sizeof (uint64_t), ret);

	if (ret != 0) {
		if (ERR_PROT_ISCE(iocp->ioc_command) || F_CACHE_CLN(iocp)) {
			cmn_err(CE_WARN, "%s: unexpected "
			    "copyin failure occurred!\n", fname);
			ret = EIO;
		} else {
			cmn_err(CE_NOTE, "%s: copyin "
			    "failed as expected\n", fname);
			ret = 0;
		}
	} else {
		if (ERR_PROT_ISCE(iocp->ioc_command) || F_CACHE_CLN(iocp)) {
			cmn_err(CE_NOTE, "%s: copyin "
			    "succeeded as expected\n", fname);
			ret = 0;
		} else {
			cmn_err(CE_WARN, "%s: unexpected "
			    "copyin success occurred!\n", fname);
			ret = EIO;
		}
	}

	return (ret);
}

/*
 * This is a common routine called by other routines to inject
 * errors into cache/memory at a physical offset.
 */
int
memtest_cphys(mdata_t *mdatap, int (*func)(mdata_t *), char *str)
{
	ioc_t	*iocp = mdatap->m_iocp;
	int	ret;

	if (F_VERBOSE(iocp)) {
		cmn_err(CE_NOTE, "memtest_cphys: injecting %s error: "
		    "cpu=%d, offset=0x%08x.%08x, delay=%d\n",
		    str, getprocessorid(),
		    PRTF_64_TO_32(iocp->ioc_addr),
		    iocp->ioc_delay);
	}

	if (F_DELAY(iocp)) {
		DELAY(iocp->ioc_delay * MICROSEC);
	}

	ret = (*func)(mdatap);

	return (ret);
}

/*
 * This routine generates a processor internal error (IERR).
 */
int
memtest_internal_err(mdata_t *mdatap)
{
	int 	ret;

	ret = OP_INJECT_INTERNAL(mdatap);

	/*
	 * Should never get here as internal errors are always Fatal
	 * (the system resets immediately).
	 */
	DPRINTF(0, "memtest_internal_err: call to OP_INTERNAL_ERR FAILED!\n");

	return (ret);
}

/*
 * This routine injects an error into the data cache at the physical offset
 * in the mdata struct without modifying the line state. This simulates
 * a real (random) error.
 */
int
memtest_dphys(mdata_t *mdatap)
{
	return (memtest_cphys(mdatap, memtest_inject_dphys, "dcache"));
}

/*
 * This routine injects an error into the instruction cache at the physical
 * offset in the mdata struct without modifying the line state. This simulates
 * a real (random) error.
 */
int
memtest_iphys(mdata_t *mdatap)
{
	return (memtest_cphys(mdatap, memtest_inject_iphys, "icache"));
}

/*
 * This routine injects an error into the L2 cache at the physical offset
 * in the mdata struct without modifying the line state. This simulates
 * a real (random) error.
 */
int
memtest_l2phys(mdata_t *mdatap)
{
	return (memtest_cphys(mdatap, memtest_inject_l2phys, "l2cache"));
}

/*
 * This routine generates a kernel bus error.
 *
 * This is done by mapping a kernel virtual address as non-cacheable and
 * accessing it.  Note that the response to this is platform specific.
 */
int
memtest_k_bus_err(mdata_t *mdatap)
{
	memtest_t	*memtestp;
	ioc_t		*iocp = mdatap->m_iocp;
	volatile caddr_t	kva_2access;
	char		tmp;
	int		ret = 0;
	char		*fname = "memtest_k_bus_err";

	/*
	 * Get the soft state structure.
	 */
	memtestp = (memtest_t *)ddi_get_soft_state(state_head, 0);

	/*
	 * Get a kernel virtual mapping to the same physical address
	 * as the the user mapping. This is a special mapping that
	 * needs to be non-cacheable so it is set up here in addition
	 * to the one already set up in the memtest_pre_test() routine
	 * (which is not used).
	 *
	 * XXX The mapping code should be reworked to avoid this second
	 * mapping (i.e. set up the desired mapping the first time)
	 */
	if ((kva_2access = memtest_map_u2kvaddr(mdatap,
	    (caddr_t)iocp->ioc_databuf, SFMMU_UNCACHEPTTE,
	    HAT_LOAD_NOCONSIST, MMU_PAGESIZE)) == NULL) {
		cmn_err(CE_WARN, "%s: memtest_map_u2kvaddr() FAILED\n", fname);
		return (ENXIO);
	}

	DPRINTF(2, "%s: kva_2access=0x%p\n", fname, kva_2access);

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Invoke the error.
	 */
	if (ERR_MISC_ISDDIPEEK(iocp->ioc_command)) {
		if (ddi_peek8(memtestp->m_dip, kva_2access, &tmp) !=
		    DDI_SUCCESS) {
			cmn_err(CE_NOTE, "%s: peek failed "
			    "as expected", fname);
		} else {
			cmn_err(CE_WARN, "%s: unexpected peek "
			    "success occurred!\n", fname);
			/*
			 * This is a test failure.
			 */
			ret = EIO;
		}
	} else {
		tmp = *kva_2access;
	}

	return (ret);
}

/*
 * The two routines below work together to produce an L2 cache copy-back
 * error on kernel data.
 *
 * The function memtest_k_cp_producer() is the producer thread and creates
 * an error in its cache.
 *
 * The function memtest_k_cp_err() is the consumer thread and accesses
 * the corrupted data which causes a copy-back error on the producer.
 *
 * The function memtest_wait_sync() is used by the producer and consumer
 * threads to wait for the thread synchronization variable to become an
 * expected value. It returns 0 on success, all other returns are errors.
 *
 * A variable is used to synchronize code execution between producer
 * and consumer threads and has the following values/meanings.
 *
 *	0	This is the initial value.
 *
 *	1	Prod:	Waits for this value before injecting the error.
 *		Cons:	Sets this value which tells the producer to go
 *			ahead and inject the error.
 *
 *	2	Prod:	Sets this value after injecting the error to tell
 *			the consumer that it can invoke the error.
 *		Con:	Waits for this value before invoking the error.
 *
 *	3	Prod:	Waits for this value after injecting the error
 *			and before exiting the thread.
 *		Con:	Sets this value after invoking the error to tell
 *			the producer that it may exit.
 *
 *	-1	This value may be set by either the producer or consumer
 *		and indicates that some sort of error has occurred. Both
 *		producer and consumer should abort the test immediately.
 */
int
memtest_k_cp_err(mdata_t *mdatap)
{
	memtest_t	*memtestp = mdatap->m_memtestp;
	ioc_t		*iocp = mdatap->m_iocp;
	mdata_t		*producer_mdatap, *consumer_mdatap;
	int		consumer_cpu, producer_cpu, ret = 0;
	volatile int	sync;
	char		*fname = "memtest_k_cp_err";

	/*
	 * Sanity check.
	 */
	if (iocp->ioc_nthreads != 2) {
		DPRINTF(0, "%s: nthreads=%d should be 2\n",
		    fname, iocp->ioc_nthreads);
		return (EIO);
	}

	consumer_mdatap = memtestp->m_mdatap[0];
	producer_mdatap = memtestp->m_mdatap[1];
	consumer_mdatap->m_syncp = &sync;
	producer_mdatap->m_syncp = &sync;
	consumer_cpu = consumer_mdatap->m_cip->c_cpuid;
	producer_cpu = producer_mdatap->m_cip->c_cpuid;

	DPRINTF(2, "%s: consumer_mdatap=0x%p, "
	    "producer_mdatap=0x%p, consumer_cpu=%d, producer_cpu=%d, "
	    "&sync=0x%p\n", fname, consumer_mdatap, producer_mdatap,
	    consumer_cpu, producer_cpu, &sync);

	/*
	 * Sanity check.
	 */
	if (consumer_cpu == producer_cpu) {
		DPRINTF(0, "%s: consumer_cpu == producer_cpu!\n", fname);
		return (EIO);
	}

	if (F_VERBOSE(iocp))
		cmn_err(CE_NOTE, "%s: consumer_cpuid=%d, producer_cpuid=%d\n",
		    fname, consumer_cpu, producer_cpu);

	/*
	 * Start the producer thread.
	 */
	if (memtest_start_thread(producer_mdatap, memtest_k_cp_producer,
	    fname) != 0) {
		cmn_err(CE_WARN, "%s: couldn't start "
		    "producer thread\n", fname);
		return (EIO);
	}

	(void) memtest_cmp_quiesce(mdatap);
	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Release the producer thread and have it inject the error.
	 */
	sync = 1;

	/*
	 * Wait for the producer thread to inject the error,
	 * but don't wait forever.
	 */
	if (memtest_wait_sync(&sync, 2, SYNC_WAIT_MAX, fname) !=
	    SYNC_STATUS_OK) {
		sync = -1;
		(void) memtest_cmp_unquiesce(mdatap);
		return (EIO);
	}

	/*
	 * We mustn't disable preemption prior to the producer injecting
	 * the error since the producer may try to offline all cpus
	 * while injecting the error.
	 */
	kpreempt_disable();

	/*
	 * Invoke the error and store a value to the sync variable
	 * indicating that we've invoked the error.
	 */
	if (!F_NOERR(iocp)) {
		DPRINTF(3, "%s: invoking error\n", fname);
		mdatap->m_asmldst(mdatap->m_kvaddr_a, (caddr_t)&sync, 3);
	} else {
		sync = 3;
	}

	/*
	 * Make sure we give the producer thread a chance to
	 * notice the update to the sync flag and exit.
	 */
	delay(1 * hz);

	/*
	 * Check for errors if we get this far.
	 */
	if (ERR_PROT_ISUE(iocp->ioc_command)) {
		cmn_err(CE_WARN, "%s: ue case should not get this far!\n",
		    fname);
		ret = EIO;
	} else {
		DPRINTF(2, "%s: normal exit, sync=%d\n", fname, sync);
	}

	(void) memtest_cmp_unquiesce(mdatap);
	kpreempt_enable();
	return (ret);
}

/*
 * This routine is the producer routine to cause a kernel copy-back error.
 * See the above comment for it's partner routine memtest_k_cp_err for
 * details.
 */
static void
memtest_k_cp_producer(mdata_t *mdatap)
{
	volatile int	*syncp = mdatap->m_syncp;
	char		*fname = "memtest_k_cp_producer";

	/*
	 * Disable preemption in case it isn't already disabled.
	 */
	kpreempt_disable();

	/*
	 * Wait for OK to inject the error, but don't wait forever.
	 */
	if (memtest_wait_sync(syncp, 1, SYNC_WAIT_MAX, fname) !=
	    SYNC_STATUS_OK) {
		*syncp = -1;
		kpreempt_enable();
		thread_exit();
	}

	DPRINTF(2, "%s: injecting the error\n", fname);

	/*
	 * Inject the error.
	 */
	if (memtest_inject_l2cache(mdatap)) {
		*syncp = -1;
		thread_exit();
	}

	/*
	 * Tell the consumer thread that we've injected the error.
	 */
	*syncp = 2;

	DPRINTF(2, "%s: waiting for consumer to "
	    "invoke the error\n", fname);

	/*
	 * Wait for consumer to invoke the error but don't wait forever.
	 */
	if (memtest_wait_sync(syncp, 3, SYNC_WAIT_MAX, fname) !=
	    SYNC_STATUS_OK) {
		*syncp = -1;
	} else {
		DPRINTF(2, "%s: normal exit, sync=%d\n", fname, *syncp);
	}

	kpreempt_enable();
	thread_exit();
}

/*
 * This routine generates a dcache data/parity error.
 */
int
memtest_k_dc_err(mdata_t *mdatap)
{
	int		ret;
	uint_t		myid;
	ioc_t		*iocp = mdatap->m_iocp;

	myid = getprocessorid();

	/*
	 * Inject the error by calling chain of memtest routines.
	 */
	if (ret = memtest_inject_dcache(mdatap)) {
		return (ret);
	}

	/*
	 * If we do not want to trigger the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "memtest_k_dc_err: not invoking error\n");
		return (0);
	}

	if (ERR_MISC_ISTL1(iocp->ioc_command)) {
		xt_one(myid, (xcfunc_t *)mdatap->m_asmld_tl1,
		    (uint64_t)mdatap->m_kvaddr_a, (uint64_t)0);
	} else {
		mdatap->m_asmld(mdatap->m_kvaddr_a);
	}

	return (0);
}

/*
 * This routine generates a icache instr/tag ecc/parity error.
 */
int
memtest_k_ic_err(mdata_t *mdatap)
{
	int		ret;

	/*
	 * Inject the error by calling the chain of memtest routines.
	 */
	if (ret = memtest_inject_icache(mdatap)) {
		return (ret);
	}

	return (0);
}

/*
 * This routine generates a kernel text/data L2 cache error(s).
 */
int
memtest_k_l2_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		err_acc = ERR_ACC(iocp->ioc_command);
	uint_t		myid = getprocessorid();
	int		ret = 0;
	char		*fname = "memtest_k_l2_err";

	/*
	 * First quiesce any other CMP cores/strands (if necessary)
	 * and flush caches.
	 */
	(void) memtest_cmp_quiesce(mdatap);

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error by calling processor specific routines.
	 * If error returned from injection routine unquiesce any
	 * quiesced cores/strands and exit.
	 */
	if (ret = memtest_inject_l2cache(mdatap)) {
		(void) memtest_cmp_unquiesce(mdatap);
		return (ret);
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		(void) memtest_cmp_unquiesce(mdatap);
		return (ret);
	}

	/*
	 * Online all system cpus that were disabled by the injector here,
	 * but only if the QF_ONLINE_CPUS_BFI (before invoke) flag is set.
	 *
	 * Other types of tests can also add these lines before the invoke.
	 */
	if (!QF_OFFLINE_CPUS_DIS(iocp) && QF_ONLINE_CPUS_BFI(iocp)) {
		/*
		 * First unquiesce any quiesced sibling cores/strands.
		 */
		(void) memtest_cmp_unquiesce(mdatap);
		if (memtest_online_cpus(mdatap) != 0) {
			DPRINTF(0, "%s: couldn't online system cpu(s)\n",
			    fname);
			return (EIO);
		}
	}

	/*
	 * Invoke the error.
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
	default:
		DPRINTF(0, "%s: unsupported access type %d\n", fname, err_acc);
		ret = ENOTSUP;
	}

	(void) memtest_cmp_unquiesce(mdatap);
	return (ret);
}

/*
 * This routine injects an L2 cache error at a user specified
 * kernel virtual address.
 */
int
memtest_k_l2virt(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;

	if (F_VERBOSE(iocp))
		cmn_err(CE_NOTE, "memtest_k_l2virt: injecting L2 error: "
		    "cpu=%d, kvaddr=0x%p\n",
		    getprocessorid(), (void *)mdatap->m_kvaddr_c);

	/*
	 * Inject the error.
	 */
	return (memtest_inject_l2cache(mdatap));
}

/*
 * This routine generates a kernel L2 cache write-back error.
 */
int
memtest_k_l2wb_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		ret;
	char		*fname = "memtest_k_l2wb_err";

	DPRINTF(2, "%s: corruption raddr=0x%llx, paddr=0x%llx\n",
	    fname, mdatap->m_raddr_c, mdatap->m_paddr_c);

	/*
	 * First quiesce any other CMP cores/strands (if necessary)
	 * and flush caches.
	 */
	(void) memtest_cmp_quiesce(mdatap);

	if (!F_FLUSH_DIS(iocp)) {
		/*
		 * The call to OP_FLUSHALL_L2 does a kernel mode L2 flush.
		 */
		OP_FLUSHALL_L2(mdatap);
	}

	/*
	 * Inject the error by calling chain of memtest routines.
	 */
	if (ret = memtest_inject_l2cache(mdatap)) {
		(void) memtest_cmp_unquiesce(mdatap);
		return (ret);
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "%s: not invoking error\n", fname);
		(void) memtest_cmp_unquiesce(mdatap);
		return (ret);
	}

	/*
	 * Online all system cpus that were disabled by the injector here,
	 * but only if the QF_ONLINE_CPUS_BFI (before invoke) flag is set.
	 *
	 * Other types of tests can also add these lines before the invoke.
	 */
	if (!QF_OFFLINE_CPUS_DIS(iocp) && QF_ONLINE_CPUS_BFI(iocp)) {
		/*
		 * First unquiesce any quiesced sibling cores/strands.
		 */
		(void) memtest_cmp_unquiesce(mdatap);
		if (memtest_online_cpus(mdatap) != 0) {
			DPRINTF(0, "%s: couldn't online system cpu(s)\n",
			    fname);
			return (EIO);
		}
	}

	/*
	 * Displacement flush the entire L2 cache to trigger write-back error.
	 *
	 * NOTE: this is not a single-line flush since that opsvec is not
	 *	 available on all processor types.
	 */
	OP_FLUSHALL_L2(mdatap);

	(void) memtest_cmp_unquiesce(mdatap);
	return (ret);
}

/*
 * This routine generates one or more kernel memory errors.
 */
int
memtest_k_mem_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	pa_2access = mdatap->m_paddr_a;
	uint64_t	pa_2corrupt = mdatap->m_paddr_c;
	uint64_t	ra_2corrupt = mdatap->m_raddr_c;
	uint64_t	paddr;
	uint64_t	paddr_end;
	uint64_t	raddr;
	uint_t		myid, i;
	int		count, stride;
	int		ret;
	int		err_acc = ERR_ACC(iocp->ioc_command);
	caddr_t		kva_2access = mdatap->m_kvaddr_a;
	caddr_t		kva_2corrupt = mdatap->m_kvaddr_c;
	caddr_t		vaddr;
	char		*fname = "memtest_k_mem_err";

	/*
	 * Check if the command is a "ce storm" and set the
	 * error count and memory stride values based on user
	 * options or set to default values (both=0x40=64).
	 *	count	= number of errors to inject, then invoke quickly
	 *	stride	= distance between errors in bytes (note that it
	 *		  must be 8-byte aligned and is checked in userland)
	 */
	if (ERR_PROT_ISCE(iocp->ioc_command) &&
	    ERR_MISC_ISSTORM(iocp->ioc_command)) {
		if (F_I_STRIDE(iocp)) {
			stride = iocp->ioc_i_stride;
		} else {
			stride = 64;
		}

		if (stride > iocp->ioc_bufsize)
			stride = iocp->ioc_bufsize;
		stride &= ~0x7ULL;

		if (F_I_COUNT(iocp)) {
			count = iocp->ioc_i_count;
		} else {
			count = 64;
		}

		if (count > (iocp->ioc_bufsize / stride))
			count = iocp->ioc_bufsize / stride;

		if (F_VERBOSE(iocp)) {
			cmn_err(CE_NOTE, "%s: injecting %d CE errors: "
			    "paddr=0x%08x.%08x, stride=0x%x\n", fname,
			    count, PRTF_64_TO_32(pa_2corrupt), stride);
		}
	} else {
		count = 1;
	}

	myid = getprocessorid();

	/*
	 * Inject the error(s).
	 */
	paddr_end = P2ALIGN(pa_2corrupt, PAGESIZE) + PAGESIZE;

	for (i = 0, vaddr = kva_2corrupt, raddr = ra_2corrupt,
	    paddr = pa_2corrupt; (i < count && paddr < paddr_end);
	    vaddr += stride, raddr += stride, paddr += stride) {

		/*
		 * Some platforms may interleave local memory with
		 * remote memory.  Only inject into local memory
		 * for storm tests since the injection mechanism
		 * (as it currently works) can only inject into
		 * local memory.  For non-storm tests, checking
		 * and setting of the injection address is assumed
		 * to have already been done at a higher level.
		 *
		 * Note that this means that it is possible that
		 * the number of errors injected will be less
		 * than count depending on how much local memory
		 * is available.
		 */
		if (ERR_MISC_ISSTORM(iocp->ioc_command) &&
		    !memtest_is_local_mem(mdatap, paddr)) {
			continue;
		}

		i++;
		mdatap->m_kvaddr_c = vaddr;
		mdatap->m_raddr_c = raddr;
		mdatap->m_paddr_c = paddr;
		DPRINTF(2, "%s: injecting error %d at "
		    "vaddr=0x%p, paddr=0x%llx\n", fname, i, vaddr, paddr);
		if (ret = memtest_inject_memory(mdatap)) {
			return (ret);
		}
	}

	/*
	 * Online all system cpus that were disabled by the injector here,
	 * but only if the ONLINE_BEFORE_INV flag is set.
	 *
	 * Other types of tests can also add these lines before the invoke.
	 */
	if (!QF_OFFLINE_CPUS_DIS(iocp) && QF_ONLINE_CPUS_BFI(iocp)) {
		if (memtest_online_cpus(mdatap) != 0) {
			DPRINTF(0, "%s: could not re-online all system "
			    "cpu(s)\n", fname);
			return (EIO);
		}
	}

	/*
	 * Invoke the error(s).
	 */
	for (i = 0, vaddr = kva_2access, paddr = pa_2access;
	    (i < count && paddr < paddr_end);
	    vaddr += stride, paddr += stride) {

		/*
		 * Skip non-local memory addresses as they
		 * will have been skipped when the errors
		 * were injected.
		 */
		if (ERR_MISC_ISSTORM(iocp->ioc_command) &&
		    !memtest_is_local_mem(mdatap, paddr)) {
			continue;
		}

		i++;

		/*
		 * If we do not want to invoke the error(s) then continue.
		 */
		if (F_NOERR(iocp)) {
			DPRINTF(2, "%s: not invoking error\n", fname);
			continue;
		}

		DPRINTF(2, "%s: invoking error %d at vaddr=0x%p, "
		    "paddr=0x%llx\n", fname, i, vaddr, paddr);

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
				DPRINTF(2, "%s: storing to invoke error\n",
				    fname);
				*vaddr = (uchar_t)0xff;
			}
			membar_sync();
			break;
		default:
			DPRINTF(0, "%s: unsupported access type %d\n",
			    fname, err_acc);
			return (ENOTSUP);
		}
	}

	return (0);
}

/*
 * This routine injects a memory error at a user specified
 * kernel virtual address.
 */
int
memtest_k_mvirt(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;

	if (F_VERBOSE(iocp))
		cmn_err(CE_NOTE, "memtest_k_mvirt: injecting memory error: "
		    "cpu=%d, kvaddr=0x%p\n",
		    getprocessorid(), (void *)mdatap->m_kvaddr_c);

	/*
	 * Inject the error.
	 */
	return (memtest_inject_memory(mdatap));
}

/*
 * This routine injects an I/D-TLB error at the specified kernel
 * virtual address.
 *
 * Note that the hyperpriv level sun4v tests also use this routine
 * as their entry point.  Although not intuitive, using this routine
 * reduces code duplication.
 */
int
memtest_k_tlb_err(mdata_t *mdatap)
{
	int 		ret;
	ioc_t		*iocp = mdatap->m_iocp;
	int		err_acc = ERR_ACC(iocp->ioc_command);
	int		err_mode = ERR_MODE(iocp->ioc_command);
	uint_t		myid = getprocessorid();
	char		*fname = "memtest_k_tlb_err";

	/*
	 * Inject I/D-TLB error(s) via processor specific routine.
	 */
	if (ret = OP_INJECT_TLB(mdatap)) {
		DPRINTF(0, "%s: I/D-TLB parity injection "
		    "FAILED! ret=0x%lx\n", fname, ret);
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
	 * Only invoke errors that do not specify ASI access.
	 */
	if (!ERR_ACC_ISASI(iocp->ioc_command)) {
		if (err_mode == ERR_MODE_KERN) {
			switch (err_acc) {
			case ERR_ACC_LOAD:
				if (ERR_MISC_ISTL1(iocp->ioc_command)) {
					xt_one(myid,
					    (xcfunc_t *)mdatap->m_asmld_tl1,
					    (uint64_t)mdatap->m_kvaddr_a,
					    (uint64_t)0);
				} else {
					mdatap->m_asmld(mdatap->m_kvaddr_a);
				}
				break;
			case ERR_ACC_STORE:
				*mdatap->m_kvaddr_a = (uchar_t)0xff;
				membar_sync();
				break;
			case ERR_ACC_FETCH:
				mdatap->m_asmld(mdatap->m_kvaddr_a);
				break;
			default:
				DPRINTF(0, "%s: unknown TLB access type! "
				    "Exiting without invoking error.\n",
				    fname);
				ret = ENOTSUP;
			}
		}
	}

	return (ret);
}

/*
 * This routine injects an error into memory at the user specified
 * physical or real (sun4v) address with a minimum of overhead.
 */
int
memtest_mphys(mdata_t *mdatap)
{
	int		ret;

	/*
	 * Inject the error.
	 */
	ret = memtest_inject_memory(mdatap);

	return (ret);
}

/*
 * This routine generates an L2 cache error due to an OBP access
 * to kernel data.
 */
int
memtest_obp_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		ret;
	static char	tst_string[] = "1 2 swap +";

	bcopy(tst_string, mdatap->m_kvaddr_a, strlen(tst_string) + 1);

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error.
	 */
	if (ret = memtest_inject_l2cache(mdatap))  {
		return (ret);
	}

	/*
	 * If we do not want to invoke the error then return now.
	 */
	if (F_NOERR(iocp)) {
		DPRINTF(2, "memtest_obp_err: not invoking error\n");
		return (0);
	}

	/*
	 * Have OBP interpret the string to invoke the error.
	 */
	(void) prom_interpret(mdatap->m_kvaddr_a, 0, 0, 0, 0, 0);

	return (0);
}

/*
 * This is a common routine called by other routines to inject
 * a user mode error.
 */
int
memtest_u_cmn_err(mdata_t *mdatap, int (*func)(mdata_t *), char *str)
{
	int		ret;

	DPRINTF(2, "memtest_u_cmn_err: injecting %s error\n", str);

	/*
	 * Inject the error.
	 */
	ret = (*func)(mdatap);

	return (ret);
}

/*
 * This routine injects an L2 cache error at the specified user virtual address.
 * The user program is responsible for invoking the error in user mode.
 */
int
memtest_u_l2_err(mdata_t *mdatap)
{
	/*
	 * For sun4v the data which is already in the cache must be
	 * flushed out so it can be brought in again AFTER DM mode
	 * has been enabled so it can be found in the expected way.
	 *
	 * This flush does not break sun4u systems because the data
	 * is always brought into the cache by the low-level routine.
	 */
	if (!F_FLUSH_DIS(mdatap->m_iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	DPRINTF(3, "memtest_u_l2_err()\n");
	return (memtest_u_cmn_err(mdatap, memtest_inject_l2cache, "l2cache"));
}

/*
 * This routine injects a memory error at the specified user
 * virtual address.
 */
int
memtest_u_mem_err(mdata_t *mdatap)
{
	return (memtest_u_cmn_err(mdatap, memtest_inject_memory, "memory"));
}

/*
 * This routine injects a memory error at a user specified
 * user virtual address.
 */
int
memtest_u_mvirt(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;

	if (F_VERBOSE(iocp))
		cmn_err(CE_NOTE, "memtest_u_mvirt: injecting memory error: "
		    "cpu=%d, uvaddr=0x%p\n",
		    getprocessorid(), (void *)mdatap->m_uvaddr_c);

	/*
	 * Inject the error.
	 */
	return (memtest_inject_memory(mdatap));
}

/*
 * **************************************************************************
 * The following block of routines are the common second level test routines.
 * **************************************************************************
 */

/*
 * This routine injects an error into the d-cache via the processor specific
 * opsvec routine.
 */
int
memtest_inject_dcache(mdata_t *mdatap)
{
	int		ret = 0;

	if (!F_FLUSH_DIS(mdatap->m_iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error via the opsvec routine.
	 */
	if (ret = OP_INJECT_DCACHE(mdatap)) {
		DPRINTF(0, "memtest_inject_dcache: processor specific dcache "
		    "injection routine FAILED!\n");
	}

	return (ret);
}

/*
 * This routine injects an error into d$ data/tag at a specified cache offset.
 */
int
memtest_inject_dphys(mdata_t *mdatap)
{
	return (OP_INJECT_DPHYS(mdatap));
}

/*
 * This routine injects an error into the i-cache via the processor specific
 * opsvec routine.
 */
int
memtest_inject_icache(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		ret;

	if (!F_FLUSH_DIS(iocp)) {
		OP_FLUSHALL_CACHES(mdatap);
	}

	/*
	 * Inject the error via the opsvec routine.
	 */
	if (ret = OP_INJECT_ICACHE(mdatap)) {
		DPRINTF(0, "memtest_inject_icache: processor specific icache "
		    "injection routine FAILED!\n");
	}

	return (ret);
}

/*
 * This routine injects an error into i$ data/tag at a specified cache offset.
 */
int
memtest_inject_iphys(mdata_t *mdatap)
{
	return (OP_INJECT_IPHYS(mdatap));
}

/*
 * This routine injects an error into the L2 cache via the processor specific
 * opsvec routine.
 */
int
memtest_inject_l2cache(mdata_t *mdatap)
{
	int		ret;
	char		*fname = "memtest_inject_l2cache";

	DPRINTF(3, "%s: injecting l2cache error on "
	    "cpuid=%d, kvaddr=0x%08x.%08x, raddr=0x%08x.%08x, "
	    "paddr=0x%08x.%08x\n",
	    fname, getprocessorid(),
	    PRTF_64_TO_32((uint64_t)mdatap->m_kvaddr_c),
	    PRTF_64_TO_32(mdatap->m_raddr_c),
	    PRTF_64_TO_32(mdatap->m_paddr_c));

	/*
	 * Inject the error via the opsvec routine.
	 */
	if (ret = OP_INJECT_L2CACHE(mdatap)) {
		DPRINTF(0, "%s: processor specific l2cache "
		    "injection routine FAILED!\n", fname);
		return (ret);
	}

	/*
	 * Check error status registers for unexpected errors that may
	 * have occured as a result of injecting the L2 cache error.
	 */
	if (memtest_flags & MFLAGS_CHECK_ESRS_L2CACHE_ERROR) {
		if (ret = OP_CHECK_ESRS(mdatap, fname)) {
			DPRINTF(0, "%s: call to OP_CHECK_ESRS "
			    "FAILED!\n", fname);
			return (ret);
		}
	}

	return (0);
}

/*
 * This routine injects an error into L2$ data/tag at a specified cache offset.
 */
int
memtest_inject_l2phys(mdata_t *mdatap)
{
	return (OP_INJECT_L2PHYS(mdatap));
}

/*
 * ****************************************************************
 * The following block of routines are the common support routines.
 * ****************************************************************
 */

/*
 * This routine binds the current thread to the processor
 * for the specified thread number.
 */
int
memtest_bind_thread(mdata_t *mdatap)
{
	proc_t		*pp;
	processorid_t	obind;
	kthread_t	*tp = curthread;
	int		threadno = mdatap->m_threadno;
	int		cpu = mdatap->m_cip->c_cpuid;
	int		err = 0;
	int		berr = 0;
	char		*fname = "memtest_bind_thread";

	DPRINTF(4, "%s:  mdatap=0x%p, thread=%d, cpu=0x%p\n",
	    fname, mdatap, threadno, cpu);
	DPRINTF(4, "%s: calling cpu_bind_thread(tp=0x%p, bind=%d)\n",
	    fname, tp, cpu);

	mutex_enter(&cpu_lock);
	pp = ttoproc(tp);
	mutex_enter(&pp->p_lock);

	err = cpu_bind_thread(tp, cpu, &obind, &berr);
	if (err != 0) {
		cmn_err(CE_WARN, "cpu_bind_thread() failed \n");
		mutex_exit(&pp->p_lock);
		mutex_exit(&cpu_lock);
		return (err);
	}

	DPRINTF(4, "%s: after calling cpu_bind_thread(): bind=%d, "
	    "obind=%d, err=%d\n", fname, tp->t_bind_cpu, obind, berr);

	mutex_exit(&pp->p_lock);
	mutex_exit(&cpu_lock);

	/*
	 * Switch cpu if we're not already on the desired cpu
	 * (the delay function causes the switch to the new cpu).
	 */
	if (cpu != CPU->cpu_id) {
		delay(1);
		if (cpu != CPU->cpu_id) {
			DPRINTF(0, "%s: failed to switch cpu, ",
			    "current=%d, desired=%d\n",
			    fname, CPU->cpu_id, cpu);
			return (EIO);
		}
	}

	return (0);
}

/*
 * Check offline/online status for the indicated CPU.
 */
int
memtest_check_cpu_status(cpu_t *cp)
{
	ASSERT(MUTEX_HELD(&cpu_lock));
	if (cp->cpu_flags & CPU_POWEROFF) {
		return (P_POWEROFF);
	} else if ((cp->cpu_flags & (CPU_READY | CPU_OFFLINE)) != CPU_READY) {
		return (P_OFFLINE);
	} else if (cp->cpu_flags & CPU_ENABLE) {
		return (P_ONLINE);
	} else {
		return (P_NOINTR);
	}
}

/*
 * This routine checks the Error Status Registers (ESRs) on all CPUs
 * via cross-calls.
 */
int
memtest_check_esrs(mdata_t *mdatap, char *msg)
{
	int	size1, size2;
	char	buf[MDATA_MSGBUF_SIZE];
	char	*func_to_call = "OP_CHECK_ESRS: ";

	size1 = strlen(func_to_call);
	size2 = strlen(msg);

	/*
	 * Truncate the string if its too big.
	 */
	if ((size1 + size2) > MDATA_MSGBUF_SIZE) {
		msg[MDATA_MSGBUF_SIZE - size1] = NULL;
	}

	(void) strcpy(buf, func_to_call);
	(void) strcat(buf, msg);

	return (memtest_xc_cpus(mdatap, memtest_check_esrs_xcfunc, buf));
}

/*
 * This is the cross-call routine called by memtest_check_esrs().
 */
void
memtest_check_esrs_xcfunc(uint64_t arg1, uint64_t arg2)
{
	mdata_t	*mdatap = (mdata_t *)arg1;
	int	*statusp = (int *)arg2;

	/*
	 * Checks for latent errors in the ESRs and clear them.
	 */
	if ((*statusp = OP_CHECK_ESRS(mdatap, mdatap->m_msgp)) != 0)
		DPRINTF(0, "memtest_check_esrs_xcfunc: failed to init ESRs\n");
}

/*
 * This routine calls the specified command function.
 * If the cross-call option was specified then a cross-call is done.
 */
int
memtest_do_cmd(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;
	int	i, count, stride;
	int	ret;
	char	*fname = "memtest_do_cmd";

	ASSERT(mdatap->m_cmdp);

	if (F_I_COUNT(iocp)) {
		count = IOC_I_COUNT(iocp);
	} else {
		count = 1;
	}

	if (F_I_STRIDE(iocp)) {
		stride = IOC_I_STRIDE(iocp);
	} else {
		stride = 0;
	}

	for (i = 0; i < count; i++) {
		if (F_XCALL(iocp) && (iocp->ioc_xc_cpu != getprocessorid())) {
			DPRINTF(1, "%s: calling function %s via "
			    "cross-call to cpu %d\n", fname,
			    mdatap->m_cmdp->c_fname, iocp->ioc_xc_cpu);
			xc_one(iocp->ioc_xc_cpu,
			    (xcfunc_t *)memtest_do_cmd_xcfunc,
			    (uint64_t)mdatap, (uint64_t)&ret);
		} else {
			DPRINTF(1, "%s: calling function %s\n", fname,
			    mdatap->m_cmdp->c_fname);
			ret = mdatap->m_cmdp->c_func(mdatap);
		}
		DPRINTF(1, "%s: function %s return=%d\n", fname,
		    mdatap->m_cmdp->c_fname, ret);

		if (ret) {
			break;
		}

		if (i < count) {
			if (F_DELAY(iocp)) {
				DELAY(iocp->ioc_delay * MICROSEC);
			}

			if (stride) {
				mdatap->m_kvaddr_a += stride;
				mdatap->m_raddr_a += stride;
				mdatap->m_paddr_a += stride;
				mdatap->m_uvaddr_a += stride;

				mdatap->m_kvaddr_c += stride;
				mdatap->m_raddr_c += stride;
				mdatap->m_paddr_c += stride;
				mdatap->m_uvaddr_c += stride;
			}
		}
	}

	return (ret);
}

/*
 * This cross-call routine calls the specified command function.
 */
static void
memtest_do_cmd_xcfunc(uint64_t arg1, uint64_t arg2)
{
	mdata_t	*mdatap = (mdata_t *)arg1;
	int	*statusp = (int *)arg2;

	DPRINTF(2, "memtest_do_cmd_xcfunc: calling function %s\n",
	    mdatap->m_cmdp->c_fname);
	*statusp = mdatap->m_cmdp->c_func(mdatap);
	DPRINTF(2, "memtest_do_cmd_xcfunc: function %s return=%d\n",
	    mdatap->m_cmdp->c_fname, *statusp);
}

/*
 * This routine displays a debug message if the debug level is
 * higher than or equal to the level specified.
 */
void
memtest_dprintf(int level, char *fmt, ...)
{
	va_list	adx;
	char	buf[256];

	if (memtest_debug < level)
		return;

	va_start(adx, fmt);
	(void) vsprintf(buf, fmt, adx);
	va_end(adx);

	(memtest_use_prom_printf ? prom_printf : printf)
	    ("debug[%d/cpu%d]: %s", level, getprocessorid(), buf);
}

/*
 * This routine dumps the location (kvaddr) of various structures used
 * by the memtest driver.
 */
void
memtest_dump_mdata(mdata_t *mdatap, caddr_t str)
{
	DPRINTF(0, "%s: mdatap=0x%p\n", str, mdatap);
	DPRINTF(0, "%s: m_iocp=0x%p\n", str, mdatap->m_iocp);
	DPRINTF(0, "%s: m_sip=0x%p\n", str, mdatap->m_sip);
	DPRINTF(0, "%s: m_cip=0x%p\n", str, mdatap->m_cip);
	DPRINTF(0, "%s: m_copvp=0x%p\n", str, mdatap->m_copvp);
	DPRINTF(0, "%s: m_sopvp=0x%p\n", str, mdatap->m_sopvp);
	DPRINTF(0, "%s: m_cmdpp=0x%p\n", str, mdatap->m_cmdpp);
	DPRINTF(0, "%s: m_memtestp=0x%p\n", str, mdatap->m_memtestp);
	DPRINTF(0, "%s: m_databuf=0x%p\n", str, mdatap->m_databuf);
	DPRINTF(0, "%s: m_instbuf=0x%p\n", str, mdatap->m_instbuf);
	DPRINTF(0, "%s: m_kvaddr_a=0x%p, m_kvaddr_c=0x%p\n", str,
	    mdatap->m_kvaddr_a, mdatap->m_kvaddr_c);
	DPRINTF(0, "%s: m_uvaddr_a=0x%p, m_uvaddr_c=0x%p\n", str,
	    mdatap->m_uvaddr_a, mdatap->m_uvaddr_c);
	DPRINTF(0, "%s: m_raddr_a=0x%llx, m_raddr_c=0x%llx\n", str,
	    mdatap->m_raddr_a, mdatap->m_raddr_c);
	DPRINTF(0, "%s: m_paddr_a=0x%llx, m_paddr_c=0x%llx\n", str,
	    mdatap->m_paddr_a, mdatap->m_paddr_c);
	DPRINTF(0, "%s: m_threadno=%d\n", str, mdatap->m_threadno);
	DPRINTF(0, "%s: m_syncp=0x%p\n", str, mdatap->m_syncp);
}

/*
 * This routine enables errors on all CPUs via cross-calls.
 *
 * Note that this routine was changed so that only the registers
 * which control the error reporting/traps on the consumer (target)
 * cpuid are modified.  Previously all cpuids had their registers
 * modified which on systems with a large number of strands/cpus
 * was very invasive.
 *
 * There could be some types of error in the future which would
 * benefit from having the errors enabled on any strands on
 * which the injector runs threads.  This can be done by walking
 * the mdatap array.
 *
 * The previous routine simply called the xcall function:
 *	return (memtest_xc_cpus(mdatap, memtest_enable_errors_xcfunc,
 *	    "enable_errors"));
 */
int
memtest_enable_errors(mdata_t *mdatap)
{
	int	ret;

	if ((ret = OP_ENABLE_ERRORS(mdatap)) != 0)
		DPRINTF(0, "memtest_enable_errors: failed to "
		    "enable errors for consumer thread!\n");

	return (ret);
}

/*
 * This is the cross-call routine called by memtest_enable_errors().
 */
static void
memtest_enable_errors_xcfunc(uint64_t arg1, uint64_t arg2)
{
	mdata_t	*mdatap = (mdata_t *)arg1;
	int	*statusp = (int *)arg2;

	/*
	 * Make sure that errors are enabled on this CPU.
	 */
	if ((*statusp = OP_ENABLE_ERRORS(mdatap)) != 0)
		DPRINTF(0, "memtest_enable_errors_xcfunc: failed to "
		    "enable errors\n");
}

/*
 * This routine frees all kernel virtual mappings on the linked list.
 * It expects to be called with the memtest mutex held.
 */
static int
memtest_free_kernel_mappings(kmap_t *kmapp)
{
	kmap_t		*nextp;
	caddr_t		kvaddr_align;
	char		*fname = "memtest_free_kernel_mappings";

	DPRINTF(2, "%s(kmapp=0x%p)\n", fname, kmapp);

	while (kmapp != NULL) {
		nextp = kmapp->k_next;
		DPRINTF(3, "%s: freeing mapping, kmapp=0x%p, nextp=0x%p\n",
		    fname, kmapp, nextp);

		/*
		 * Tear down the mapping.
		 */
		kvaddr_align = (caddr_t)((uint64_t)kmapp->k_kvaddr &
		    MMU_PAGEMASK);
		DPRINTF(3, "%s: calling hat_unload"
		    "(hat=0x%p, kvaddr=0x%p, size=0x%x, flags=0x%x)\n",
		    fname, kas.a_hat, kvaddr_align,
		    kmapp->k_size, HAT_UNLOAD_UNLOCK);
		/*
		 * Sanity check.
		 */
		if ((kmapp->k_size & MMU_PAGEOFFSET) != 0) {
			DPRINTF(0, "%s: invalid "
			    "kmap_t data kmapp=0x%p, kvaddr=0x%p, "
			    "size=0x%p, lockpp=0x%p\n", fname,
			    kmapp, kmapp->k_kvaddr, kmapp->k_size,
			    kmapp->k_lockpp);
			return (EIO);
		}
		hat_unload(kas.a_hat, kvaddr_align, kmapp->k_size,
		    HAT_UNLOAD_UNLOCK);

		/*
		 * Release the kernel virtual address.
		 */
		DPRINTF(3, "%s: calling vmem_free"
		    "(mp=0x%p, kvaddr=0x%p size=0x%x)\n", fname,
		    heap_arena, kvaddr_align, kmapp->k_size);
		vmem_free(heap_arena, kvaddr_align, kmapp->k_size);

		if (kmapp->k_lockpp != NULL) {
			if (kmapp->k_lk_upgrade) {
				page_downgrade(kmapp->k_lockpp);
			} else {
				page_unlock(kmapp->k_lockpp);
			}
		}

		/*
		 * Free the mapping struct itself.
		 */
		DPRINTF(3, "%s: freeing struct 0x%p, len=0x%x\n",
		    fname, kmapp, sizeof (kmap_t));
		kmem_free(kmapp, sizeof (kmap_t));

		kmapp = nextp;
	}

	return (0);
}

/*
 * This routine unlocks and frees the kernel allocated data buffer, using
 * the memory pages linked list.
 */
int
memtest_free_kernel_memory(mdata_t *mdatap, uint64_t addr)
{
	ioc_t		*iocp = mdatap->m_iocp;
	kplock_t	*kplockp;
	int		npages;
	char		*fname = "memtest_free_kernel_memory";

	DPRINTF(2, "%s(mdatap=0x%p, addr=0x%llx)\n", fname, mdatap, addr);

	/*
	 * First unlock the locked kernel buffer pages by looking
	 * through the list of locked pages for an address match.
	 */
	npages = iocp->ioc_bufsize / MMU_PAGESIZE;
	mutex_enter(&mdatap->m_memtestp->m_mutex);
	kplockp = mdatap->m_memtestp->m_kplockp;

	while (kplockp != NULL) {
		DPRINTF(2, "%s: kplockp=0x%p, k_paddr=0x%llx, "
		    "k_npages=0x%d\n", fname, kplockp,
		    kplockp->k_paddr, kplockp->k_npages);
		if ((kplockp->k_paddr == addr) &&
		    (kplockp->k_npages == npages))
			break;
		kplockp = kplockp->k_next;
	}

	/*
	 * If no match found, pages were not locked, so free buffer
	 * and return an error.
	 */
	if (kplockp == NULL) {
		DPRINTF(2, "%s: couldn't find locked kernel "
		    "page, addr=0x%llx, npages=0x%d\n",
		    fname, addr, npages);
		mutex_exit(&mdatap->m_memtestp->m_mutex);
		kmem_free(mdatap->m_databuf, iocp->ioc_bufsize);
		return (EIO);
	}

	/*
	 * Otherwise found match, unlink entry from the master list.
	 */
	if (kplockp->k_prev != NULL)
		kplockp->k_prev->k_next = kplockp->k_next;
	if (kplockp->k_next != NULL)
		kplockp->k_next->k_prev = kplockp->k_prev;
	if (kplockp == mdatap->m_memtestp->m_kplockp)
		mdatap->m_memtestp->m_kplockp = kplockp->k_next;
	/*
	 * Finally, unlock the pages.
	 */
	if (memtest_unlock_kernel_pages(kplockp) != 0) {
		DPRINTF(2, "%s: couldn't unlock kernel pages\n", fname);
		mutex_exit(&mdatap->m_memtestp->m_mutex);
		return (EIO);
	}
	mutex_exit(&mdatap->m_memtestp->m_mutex);

	/*
	 * Actually free the kernel data buffer.
	 */
	kmem_free(mdatap->m_databuf, iocp->ioc_bufsize);
	mdatap->m_databuf = NULL;

	return (0);
}

static int
memtest_free_resources(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;
	int	ret;

	/*
	 * Perform tear-down specific to the test mode (if required).
	 */
	if (ERR_MODE_ISKERN(iocp->ioc_command) ||
	    ERR_MODE_ISHYPR(iocp->ioc_command) ||
	    ERR_MODE_ISOBP(iocp->ioc_command)) {
		(void) memtest_post_test_kernel(mdatap);
	}

	/*
	 * Free kernel mapping(s) and any locked pages.
	 */
	mutex_enter(&mdatap->m_memtestp->m_mutex);
	ret = memtest_free_kernel_mappings(mdatap->m_memtestp->m_kmapp);
	mdatap->m_memtestp->m_kmapp = NULL;
	mutex_exit(&mdatap->m_memtestp->m_mutex);

	return (ret);
}

/*
 * This routine returns the access offset for a particular command.
 *
 * XXX	The random code is using modulo 64 for memory (good) but modulo 32
 *	for everything else.  This may not work correctly for L1 caches and
 *	other test types, perhaps the modulo should use the cache linesize.
 */
int
memtest_get_a_offset(ioc_t *iocp)
{
	int	ret;

	/*
	 * Both offset and random options are global with
	 * the random option having precedence.
	 */
	if (F_RANDOM(iocp))
		ret = gethrtime() %
		    (ERR_CLASS_ISMEM(iocp->ioc_command) ? 64 : 32);
	else if (F_A_OFFSET(iocp))
		ret = iocp->ioc_a_offset;
	else
		ret = 0;

	DPRINTF(3, "memtest_get_a_offset: returning offset=0x%x\n", ret);
	return (ret);
}

/*
 * This routine returns the corruption offset for a particular command.
 *
 * XXX	The random code is using modulo 64 for memory (good) but modulo 32
 *	for everything else.  This may not work correctly for L1 caches and
 *	other test types, perhaps the modulo should use the cache linesize.
 */
int
memtest_get_c_offset(ioc_t *iocp)
{
	int	ret;

	/*
	 * Both offset and random options are global with
	 * the random option having precedence.
	 */
	if (F_RANDOM(iocp))
		ret = gethrtime() %
		    (ERR_CLASS_ISMEM(iocp->ioc_command) ? 64 : 32);
	else if (F_C_OFFSET(iocp))
		ret = iocp->ioc_c_offset;
	else
		ret = 0;

	DPRINTF(3, "memtest_get_c_offset: returning offset=0x%x\n", ret);
	return (ret);
}

/*
 * This routine is used to obtain the tte for a virtual address given the
 * address and a pointer to the struct hat for the address space.
 *
 * If a valid mapping for the address exists, *ttep will be set to the value
 * of the tte.  The function called to obtain the tte also returns the pfn
 * that is mapped, but we ignore this value unless it is PFN_INVALID which
 * indicates an invalid mapping and tte.
 *
 * Note that the function for obtaining a tte for a user virtual address is
 * actually a static function and the only way it can be used here is to
 * look up its address via kobj_getsymvalue().  If a developer changes or
 * removes this function they may not notice its usage here and some
 * injector tests may break.  The number of these tests is typically very
 * small, though, since they are limited to user space tests that require
 * a tte for error injection.
 */
int
memtest_get_tte(struct hat *sfmmup, caddr_t addr, tte_t *ttep)
{
	int	(*sfmmu_uvatopfn_func)(caddr_t, struct hat *, tte_t *);
	char	*fname = "memtest_get_tte";

	if (sfmmup == ksfmmup) {
		if (sfmmu_vatopfn(addr, sfmmup, ttep) == PFN_INVALID) {
			DPRINTF(0, "%s: sfmmu_vatopfn() returned PFN_INVALID\n",
			    fname);
			return (-1);
		}
	} else {
		sfmmu_uvatopfn_func = (int(*)(caddr_t, struct hat *, tte_t *))
		    kobj_getsymvalue("sfmmu_uvatopfn", 1);

		if (sfmmu_uvatopfn_func == NULL) {
			DPRINTF(0, "%s: kobj_getsymvalue for sfmmu_uvatopfn "
			    "FAILED!\n", fname);
			return (-1);
		}

		if (sfmmu_uvatopfn_func(addr, sfmmup, ttep) == PFN_INVALID) {
			DPRINTF(0, "%s: sfmmu_uvatopfn() returned "
			    "PFN_INVALID\n", fname);
			return (-1);
		}
	}

	ASSERT(TTE_IS_VALID(ttep));
	return (0);
}

/*
 * This routine handles some of the memory request ioctls.
 *
 * Return 0 on success, otherwise return -1.
 * On success, paddr1 is set to to the physical address of the pages.
 *
 * NOTE: for sun4v the physical address is actually a real addres.
 */
uint64_t
memtest_mem_request(memtest_t *memtestp, uint64_t *paddr1p, uint64_t *paddr2p,
		int npages, int type)
{
	pfn_t		pfn;
	page_t		*pp;
	kplock_t	new, *newp;
	uint64_t	paddr;
	uint64_t	start_paddr = (uint64_t)-1;
	uint64_t	paddr1 = *paddr1p;
	uint64_t	paddr2 = *paddr2p;
	int		pages_found, pages_locked;
	int		lock_pages = 0;
	int		find_free_pages = 0;
	char		*fname = "memtest_mem_request";

	DPRINTF(2, "%s: memtestp=0x%p, paddr1=0x%llx, "
	    "paddr2=0x%llx, npages=%d, type=%d\n",
	    fname, memtestp, paddr1, paddr2, npages, type);

	/*
	 * Set flags based on request type.
	 */
	switch (type) {
	case MREQ_FIND_FREE_PAGES:
		find_free_pages = 1;
		break;
	case MREQ_LOCK_FREE_PAGES:
		find_free_pages = 1;
		lock_pages = 1;
		break;
	case MREQ_LOCK_PAGES:
		lock_pages = 1;
		break;
	default:
		DPRINTF(0, "%s: unsupported mreq type=0x%x\n", fname, type);
		return (ENOTSUP);
	}

	bzero(&new, sizeof (kplock_t));

	/*
	 * Search physical address range and try to fulfill
	 * the request.
	 */
	for (paddr = paddr1; paddr < paddr2; paddr += MMU_PAGESIZE) {
		if (start_paddr == -1) {
			start_paddr = paddr;
			new.k_paddr = start_paddr;
			new.k_npages = pages_found = pages_locked = 0;
		}

		/*
		 * Check that the physical address is memory.
		 */
		pfn = paddr >> MMU_PAGESHIFT;
		if (pf_is_memory(pfn) == 0) {
			DPRINTF(0, "%s: paddr=0x%llx is not memory\n",
			    fname, paddr);
			if (pages_locked)
				(void) memtest_unlock_kernel_pages(&new);
			return (ENXIO);
		}
		/*
		 * Get the page structure.
		 */
		pp = page_numtopp_nolock(pfn);

		/*
		 * Dump some debug info.
		 */
		DPRINTF(3, "%s: checking page: paddr=0x%llx, "
		    "pfn=0x%x, pp=0x%p\n", fname, paddr, pfn, pp);
		if (pp != NULL) {
			DPRINTF(3, "%s: p_selock=0x%x, "
			    "p_lckcnt=0x%x, p_cowcnt=0x%x\n", fname,
			    pp->p_selock, pp->p_lckcnt, pp->p_cowcnt);
			DPRINTF(3, "%s: p_iolock_state=0x%x, p_state=0x%x\n",
			    fname, pp->p_iolock_state, pp->p_state);
			DPRINTF(3, "%s: p_vnode=0x%p, "
			    "p_hash=0x%p, p_vpnext=0x%p, p_vpprev=0x%p\n",
			    fname, pp->p_vnode, pp->p_hash, pp->p_vpnext,
			    pp->p_vpprev);
		}

		/*
		 * Check that the page has a valid page struct and
		 * is free if that is what was requested.
		 */
		if ((pp == NULL) ||
		    (find_free_pages && (PP_ISFREE(pp) == 0))) {
			DPRINTF(3, "%s: skipping page due to "
			    "NULL page struct or page not free\n", fname);
			if (pages_locked) {
				if (memtest_unlock_kernel_pages(&new))
					return (EIO);
			}
			start_paddr = -1;
			continue;
		}
		/*
		 * Check that the page is not already locked.
		 */
		if (page_trylock(pp, SE_EXCL)) {
			/*
			 * If we get this far then the page met all
			 * of the requrements. Either unlock it now
			 * or keep it locked if so requested.
			 */
			new.k_npages = ++pages_found;
			if (lock_pages)
				pages_locked++;
			else
				page_unlock(pp);
			DPRINTF(3, "%s: page locked succesfully\n", fname);
		} else {
			DPRINTF(3, "%s: skipping page due to "
			    "lock failure\n", fname);
			if (pages_locked) {
				if (memtest_unlock_kernel_pages(&new))
					return (EIO);
			}
			start_paddr = -1;
			continue;
		}

		DPRINTF(3, "%s: pages_found=%d, pages_locked=%d, paddr="
		    "0x%llx\n", fname, pages_found, pages_locked, paddr);

		/*
		 * See if the request was fulfilled and create
		 * a locking info struct if necessary.
		 */
		if (pages_found >= npages) {
			*paddr1p = start_paddr;
			if (pages_locked) {
				DPRINTF(2, "%s: %d pages found "
				    "and locked at paddr=0x%llx\n",
				    fname, pages_found, start_paddr);
				/*
				 * Add the locking info structure to the linked
				 * list so it can be freed later.
				 */
				newp = kmem_zalloc(sizeof (kplock_t), KM_SLEEP);
				newp->k_npages = new.k_npages;
				newp->k_paddr = new.k_paddr;
				mutex_enter(&memtestp->m_mutex);
				newp->k_next = memtestp->m_kplockp;
				if (newp->k_next != NULL)
					newp->k_next->k_prev = newp;
				memtestp->m_kplockp = newp;
				mutex_exit(&memtestp->m_mutex);
				DPRINTF(2, "%s: newp=0x%p, k_paddr=0x%llx, "
				    "k_npages=%d, k_next=0x%p\n",
				    fname, newp, newp->k_paddr,
				    newp->k_npages, newp->k_next);
				DPRINTF(2, "%s: m_kplockp=0x%p\n", fname,
				    memtestp->m_kplockp);
			} else {
				DPRINTF(2, "%s: %d pages found "
				    "at paddr=0x%llx\n", fname,
				    pages_found, start_paddr);
			}
			return (0);
		}
	}

	DPRINTF(3, "%s: couldn't fulfill request\n", fname);
	*paddr1p = -1;
	return (ENXIO);
}

/*
 * This routine initializes thread specific information.
 * It fills in the commands list, ops vector table, and
 * the thread cpu specific structure.
 */
int
memtest_init(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	int		ret;
	char		*fname = "memtest_init";

	/*
	 * Start by clearing out the cpu info structure.
	 * It gets filled in below (note c_offlined = zero = online).
	 */
	bzero(cip, sizeof (cpu_info_t));

#ifdef	sun4v
	/*
	 * Ensure that the system has a version of hypervisor with the
	 * HV_EXEC trap required by the injector, and that it is enabled.
	 */
	if ((ret = memtest_hv_diag_svc_check()) != 0) {
		delay(1 * hz);
		DPRINTF(0, "%s: memtest_hv_diag_svc_check() failed\n", fname);
		DPRINTF(0, "%s: this system does not have the required "
		    "\n\thypervisor services enabled.  These must be "
		    "\n\tnegotiated before the cpu/mem error injector "
		    "\n\tcan operate.  Please contact the FW, Solaris, "
		    "\n\tor EI groups for more information\n", fname);
		return (ret);
	}
#endif	/* sun4v */

	/*
	 * Chicken and the egg.
	 * We need to initialize the cpu version before we can
	 * call either of the other routines below.
	 */
	cip->c_cpuver = memtest_get_cpu_ver();

	/*
	 * Call processor specific init routine.
	 */
	if ((ret = memtest_cpu_init(mdatap)) != 0) {
		DPRINTF(0, "%s: memtest_cpu_init() failed\n", fname);
		return (ret);
	}

	/*
	 * Get current cpu's information.
	 */
	if ((ret = memtest_get_cpu_info(mdatap)) != 0) {
		DPRINTF(0, "%s: memtest_get_cpu_info() failed\n", fname);
		return (ret);
	}

	return (0);
}

/*
 * This routine initializes the mdata_t data structures
 * for each thread. It expects to be called with the
 * data pointer for thread 0.
 */
int
memtest_init_threads(mdata_t *mdatap)
{
	memtest_t	*memtestp = mdatap->m_memtestp;
	ioc_t		*iocp = mdatap->m_iocp;
	int		nthreads = iocp->ioc_nthreads;
	int		i;

	/*
	 * Sanity check.
	 */
	if (mdatap != memtestp->m_mdatap[0]) {
		DPRINTF(0, "memtest_init_threads: not called with pointer "
		    "to thread 0 data structure\n");
		return (1);
	}

	/*
	 * Initialize common info for secondary threads by
	 * copying it from the primary thread.
	 */
	for (i = 1; i < nthreads; i++) {
		memtestp->m_mdatap[i]->m_asmld = mdatap->m_asmld;
		memtestp->m_mdatap[i]->m_asmldst = mdatap->m_asmldst;
		memtestp->m_mdatap[i]->m_asmld_tl1 = mdatap->m_asmld_tl1;
		memtestp->m_mdatap[i]->m_asmst_tl1 = mdatap->m_asmst_tl1;
		memtestp->m_mdatap[i]->m_blkld = mdatap->m_blkld;
		memtestp->m_mdatap[i]->m_blkld_tl1 = mdatap->m_blkld_tl1;
		memtestp->m_mdatap[i]->m_pcrel = mdatap->m_pcrel;
		memtestp->m_mdatap[i]->m_databuf = mdatap->m_databuf;
		memtestp->m_mdatap[i]->m_instbuf = mdatap->m_instbuf;
		memtestp->m_mdatap[i]->m_kvaddr_a = mdatap->m_kvaddr_a;
		memtestp->m_mdatap[i]->m_kvaddr_c = mdatap->m_kvaddr_c;
		memtestp->m_mdatap[i]->m_uvaddr_a = mdatap->m_uvaddr_a;
		memtestp->m_mdatap[i]->m_uvaddr_c = mdatap->m_uvaddr_c;
		memtestp->m_mdatap[i]->m_raddr_a = mdatap->m_raddr_a;
		memtestp->m_mdatap[i]->m_raddr_c = mdatap->m_raddr_c;
		memtestp->m_mdatap[i]->m_paddr_a = mdatap->m_paddr_a;
		memtestp->m_mdatap[i]->m_paddr_c = mdatap->m_paddr_c;
	}

	return (0);
}

/*
 * This cross-call function initializes thread specific information
 * by calling the memtest_init() routine.
 */
void
memtest_init_thread_xcfunc(uint64_t arg1, uint64_t arg2)
{
	mdata_t	*mdatap = (mdata_t *)arg1;
	int	*statusp = (int *)arg2;
	int	threadno = mdatap->m_threadno;
	int	ret;

	DPRINTF(2, "memtest_init_thread_xcfunc(mdatap=0x%p, statusp=0x%p): "
	    "threadno=%d\n", mdatap, statusp, threadno);

	/*
	 * Initialize the thread specific information.
	 */
	if ((ret = memtest_init(mdatap)) != 0) {
		DPRINTF(0, "memtest_init_thread_xcfunc: call to memtest_init() "
		    "failed\n");
		*statusp = ret;
		return;
	}

	*statusp = 0;
}

/*
 * This routine does a peek or poke to the specified
 * physical address in kernel mode.
 *
 * NOTE: for sun4v the physical address is actually a real address, though
 *	 common kernel routines retain the "paddr" names.
 */
int
memtest_k_mpeekpoke(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	paddr = iocp->ioc_addr;
	uint64_t	data = iocp->ioc_xorpat;
	int		asi;
	char		*fname = "memtest_k_mpeekpoke";

	/*
	 * If miscellaneous argument one is specified, it means
	 * the access should be non-cacheable.
	 */
	if (F_MISC1(iocp) && (iocp->ioc_misc1))
		asi = 0x15;
	else
		asi = 0x14;

	if (F_VERBOSE(iocp)) {
		cmn_err(CE_NOTE, "%s: %s "
		    "%s to paddr/raddr=0x%08x.%08x\n", fname,
		    ((asi == 0x14) ? "cacheable" : "non-cacheable"),
		    (ERR_MISC_ISPEEK(iocp->ioc_command) ? "peek" : "poke"),
		    PRTF_64_TO_32(paddr));
	}

	/*
	 * Access the location.
	 */
	if (ERR_MISC_ISPEEK(iocp->ioc_command)) {
		DPRINTF(3, "%s: peeking paddr/raddr=0x%llx\n", fname, paddr);
		data = peek_asi64(asi, paddr);
		cmn_err(CE_NOTE, "%s: paddr/raddr 0x%08x.%08x "
		    "contents are 0x%08x.%08x\n", fname, PRTF_64_TO_32(paddr),
		    PRTF_64_TO_32(data));
	} else if (ERR_MISC_ISPOKE(iocp->ioc_command)) {
		DPRINTF(3, "%s: poking paddr/raddr=0x%llx\n", fname, paddr);
		poke_asi64(asi, paddr, data);
	} else {
		cmn_err(CE_NOTE, "%s: invalid misc type for kern peek/poke!\n",
		    fname);
		return (EINVAL);
	}

	return (0);
}

/*
 * This routine does a peek or poke to the specified
 * kernel virtual address in kernel mode.
 */
int
memtest_k_vpeekpoke(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	caddr_t		kvaddr = (caddr_t)iocp->ioc_addr;
	uint64_t	data = iocp->ioc_xorpat;
	uint64_t	paddr;
	char		*fname = "memtest_k_vpeekpoke";

	if (F_VERBOSE(iocp)) {
		cmn_err(CE_NOTE, "%s: %s "
		    "to kvaddr=0x%08x.%08x\n", fname,
		    (ERR_MISC_ISPEEK(iocp->ioc_command) ? "peek" : "poke"),
		    PRTF_64_TO_32((uint64_t)kvaddr));
	}

	/*
	 * Ensure that the kernel virtual address actually maps to
	 * an underlying physical/real address.
	 */
	if ((paddr = va_to_pa((void *)kvaddr)) == -1) {
		DPRINTF(0, "%s: va_to_pa translation failed for "
		    "kvaddr=0x%p, cannot access value\n", fname, kvaddr);
		return (ENXIO);
	}

	if (!pf_is_memory(paddr >> MMU_PAGESHIFT)) {
		DPRINTF(0, "%s: paddr/raddr=0x%llx that maps to kvaddr=0x%p "
		    "is not a valid memory address\n", fname, paddr, kvaddr);
		return (ENXIO);
	}

	/*
	 * Access the location.
	 */
	if (ERR_MISC_ISPEEK(iocp->ioc_command)) {
		DPRINTF(3, "%s: peeking kvaddr=0x%llx\n", fname, kvaddr);
		data = *(uint64_t *)kvaddr;
		cmn_err(CE_NOTE, "%s: kvaddr 0x%p "
		    "contents are 0x%08x.%08x\n", fname, (void *)kvaddr,
		    PRTF_64_TO_32(data));
	} else if (ERR_MISC_ISPOKE(iocp->ioc_command)) {
		DPRINTF(3, "%s: poking kvaddr=0x%p\n", fname, kvaddr);
		*(uint64_t *)kvaddr = data;
	} else {
		cmn_err(CE_NOTE, "%s: invalid misc type for kern virtual "
		    "peek/poke!\n", fname);
		return (EINVAL);
	}

	return (0);
}

/*
 * This routine is simply a wrapper for the kernel va_to_pa routine.
 * It displays an error message if the translation fails.
 *
 * NOTE: for sun4v the physical address is actually a real address, though
 *	 common kernel routines retain the "paddr" names.
 */
uint64_t
memtest_kva_to_pa(void *kvaddr)
{
	uint64_t	paddr;

	if ((paddr = va_to_pa(kvaddr)) == -1) {
		DPRINTF(0, "memtest_kva_to_pa: translation failed for "
		    "kvaddr=0x%p\n", kvaddr);
		return (-1);
	}

	DPRINTF(3, "memtest_kva_to_pa: returning: kvaddr=0x%p is mapped to "
	    "paddr=0x%llx\n", kvaddr, paddr);

	return (paddr);
}

/*
 * This routine locks down the user page(s) associated
 * with the user virtual address passed in.
 */
int
memtest_lock_user_pages(memtest_t *memtestp, caddr_t uvaddr, int size,
				proc_t *procp)
{
	uplock_t	*newp;
	int		ret;
	char		*fname = "memtest_lock_user_pages";

	DPRINTF(2, "%s: memtestp=0x%p, uvaddr=0x%p, procp=0x%p\n",
	    fname, memtestp, uvaddr, procp);

	if (procp == NULL) {
		procp = ttoproc(curthread);
	}

	/*
	 * Allocate/initialize a new locking info structure.
	 */
	newp = kmem_zalloc(sizeof (uplock_t), KM_SLEEP);
	DPRINTF(3, "%s: allocated new struct 0x%p, len=0x%x\n",
	    fname, newp, sizeof (uplock_t));
	newp->p_procp = procp;
	newp->p_asp = procp->p_as;
	newp->p_size = size;
	newp->p_uvaddr = uvaddr;

	/*
	 * Lock the page(s).
	 */
	DPRINTF(3, "%s: calling as_pagelock(asp=0x%p, "
	    "&pplist=0x%p, uvaddr=0x%p, size=0x%x, rw=0x%x)\n",
	    fname, newp->p_asp, &newp->p_pplist, newp->p_uvaddr,
	    newp->p_size, S_WRITE);
	ret = as_pagelock(newp->p_asp, &newp->p_pplist, newp->p_uvaddr,
	    newp->p_size, S_WRITE);

	if (ret != 0) {
		DPRINTF(0, "%s: as_pagelock() failed, uvaddr=0x%p\n",
		    fname, newp->p_uvaddr);
		kmem_free(newp, sizeof (uplock_t));
		return (ret);
	}

	/*
	 * Add the locking info structure to the linked list
	 * so it can be freed later.
	 */
	mutex_enter(&memtestp->m_mutex);
	newp->p_next = memtestp->m_uplockp;
	if (newp->p_next != NULL)
		newp->p_next->p_prev = newp;
	memtestp->m_uplockp = newp;
	mutex_exit(&memtestp->m_mutex);

	DPRINTF(2, "%s: newp=0x%p, asp=0x%p, uvaddr=0x%p, "
	    "procp=0x%p, size=0x%x, pplist=0x%p, next=0x%p, prev=0x%p\n",
	    fname, newp, newp->p_asp, newp->p_uvaddr, newp->p_procp,
	    newp->p_size, newp->p_pplist, newp->p_next, newp->p_prev);

	return (0);
}

/*
 * This routine sets up a kernel virtual mapping to the given
 * physical address. It also adds information to a linked list
 * so that the resources used can be freed up later.
 *
 * NOTE: for sun4v the physical address is actually a real address.
 */
caddr_t
memtest_map_p2kvaddr(mdata_t *mdatap, uint64_t paddr, int size, uint64_t attr,
		uint_t flags)
{
	memtest_t	*memtestp = mdatap->m_memtestp;
	ioc_t		*iocp = mdatap->m_iocp;
	pfn_t		pfn;
	kmap_t		*newp;
	uint64_t	paddr2;
	struct page	*pp;
	int		locked_kernel_page = 0;
	int		page_lock_failed = 0;
	int		page_lock_upgraded = 0;
	caddr_t		kvaddr;
	char		*fname = "memtest_map_p2kvaddr";

	DPRINTF(3, "%s: paddr=0x%llx, size=0x%x, attr=0x%x, "
	    "flags=0x%x\n", fname, paddr, size, attr, flags);

	kvaddr = (caddr_t)vmem_alloc(heap_arena, size, VM_NOSLEEP);
	if (kvaddr == NULL) {
		DPRINTF(0, "%s: vmem_alloc(vmp=heap_arena, size=0x%x,"
		    "vmflag=VM_NOSLEEP) failed\n",  fname, size);
		return (NULL);
	}

	/*
	 * Sanity check.
	 * Virtual mapping must be page aligned.
	 */
	if (((uint64_t)kvaddr & ~MMU_PAGEMASK) != 0) {
		DPRINTF(0, "%s: kvaddr=0x%p is not 0x%llx aligned!\n",
		    fname, kvaddr, MMU_PAGEMASK);
		vmem_free(heap_arena, kvaddr, size);
		return (NULL);
	}

	DPRINTF(3, "%s: kvaddr=0x%p\n", fname, kvaddr);

	pfn = paddr >> MMU_PAGESHIFT;
	attr |= PROT_READ | PROT_WRITE | PROT_EXEC;
	flags |= HAT_LOAD_LOCK;

	/*
	 * There are two primary reasons the page which maps the buffer used
	 * for error injection and triggering is exclusive locked here.
	 *
	 * 1) Prevent the page from being retired and possibly relocated or
	 *    otherwise acted upon by some other entity until the test is
	 *    complete.  This is especially important for tests like the
	 *    CE storm test which may cause a page retire and relocation
	 *    before completion of the test which can cause a panic when
	 *    attempting to trigger them since the location where the errors
	 *    were injected will no longer be mapped.
	 *
	 * 2) When using the page to inject an error into a virtually indexed
	 *    cache, it is required that the TTE which maps the page have its
	 *    CV bit set (cacheable in virtually indexed cache).  To do this
	 *    requires that SFMMU_UNCACHEVTTE attribute _not_ be set which
	 *    requires that the mapping to the page be set up _without_ the
	 *    HAT_LOAD_NOCONSIST flag.  In this situation hat_devload() expects
	 *    the page being mapped to be locked.
	 *    Currently, all D-caches and some I-caches (i.e. Panther) are
	 *    virtually indexed.
	 *
	 *    Additionally, the current prefetch tests for some platforms
	 *    (i.e. US-IIIi) require a virtually cacheable memory mapping to
	 *    work.
	 *
	 *    NOTE 1: Setting up a kernel mapping to a user page without
	 *    specifying the HAT_LOAD_NOCONSIST flag is not supported by the OS
	 *    and is done here as a hack out of necessity in order to inject the
	 *    error.  To avoid exposing this unorthodox mapping outside of the
	 *    injector, the page must be kept exclusive locked until the mapping
	 *    is removed.
	 *
	 *    NOTE 2: There is currently no harm in setting up this sort of
	 *    kernel mapping for injection of error types that don't need it
	 *    (i.e. cheetah i-cache, memory, etc), but we avoid it where
	 *    possible to lessen the possibility of future problems should
	 *    the way the OS deals with mappings change.
	 *
	 * Note that if the HAT_LOAD_NOCONSIST flag was specified by the caller,
	 * then the page being mapped will not be locked here.  This is to
	 * support "no mode" injections which may be injected into memory not
	 * represented with page structures.
	 *
	 * XXX At some future point it would nice to separate in the code the
	 * semantics of locking a page for injection and triggering from the
	 * semantics of needing to lock a page while it has a kernel mapping.
	 */

	/*
	 * With some exceptions, set the HAT_LOAD_NOCONSIST flag
	 */
	if (!(flags & HAT_LOAD_NOCONSIST) && pf_is_memory(pfn)) {
		switch (ERR_CLASS(iocp->ioc_command)) {
		case ERR_CLASS_DC:
		case ERR_CLASS_IC:
			break;
		default:
			if (ERR_ACC(iocp->ioc_command) != ERR_ACC_PFETCH)
				flags |= HAT_LOAD_NOCONSIST;
		}

		pp = page_numtopp_nolock(pfn);
		if (pp == NULL) {
			DPRINTF(0, "%s: internal error: pp is null\n", fname);
			page_lock_failed = 1;
		}

		/*
		 * If the page is not already exclusive locked, try to lock it
		 * or upgrade the lock if it is already share locked.  A page
		 * will be share locked if it was obtained through /dev/physmem.
		 */
		if (!page_lock_failed && !PAGE_EXCL(pp)) {
			if (PAGE_SHARED(pp)) {
				if (!page_tryupgrade(pp)) {
					page_lock_failed = 1;
				} else {
					page_lock_upgraded = 1;
				}
			} else {
				/*
				 * page not locked
				 */
				if (PP_ISFREE(pp)) {
					DPRINTF(0,
					    "%s: internal error: page 0x%p "
					    "is free\n", fname, pp);
					page_lock_failed = 1;
				}
				if (!page_lock_failed &&
				    page_trylock(pp, SE_EXCL) == 0) {
					DPRINTF(0, "%s: unable to lock page "
					    "0x%p\n", fname, pp);
					page_lock_failed = 1;
				}
			}
			if (!page_lock_failed)
				locked_kernel_page = 1;
		}
	}

	if (page_lock_failed) {
		vmem_free(heap_arena, kvaddr, size);
		return (NULL);
	}

	DPRINTF(3, "%s: calling hat_devload(hat=0x%p, "
	    "kvaddr=0x%p, len=0x%x, pfn=0x%x, attr=0x%x, "
	    "flags=0x%x\n", fname, kas.a_hat, kvaddr, size,
	    pfn, attr, flags);

	/*
	 * Setup the mapping.
	 */
	hat_devload(kas.a_hat, kvaddr, size, pfn, attr, flags);

	/*
	 * The call to hat_devload() may have caused the user mapping to the
	 * page to be unloaded.  Release the lock now for any kernel mode
	 * injections that trigger the error through access of a user virtual
	 * address in order to avoid a deadlock should the access result in
	 * page fault which would try to acquire the page lock.  Currently,
	 * only copyin errors fall into this category.
	 */
	if (ERR_MISC_ISCOPYIN(iocp->ioc_command) && locked_kernel_page) {
		ASSERT(flags & HAT_LOAD_NOCONSIST);
		if (page_lock_upgraded) {
			page_downgrade(pp);
			page_lock_upgraded = 0;
		} else {
			page_unlock(pp);
		}
		locked_kernel_page = 0;
	}

	/*
	 * Allocate/initialize a new mapping info structure.
	 */
	newp = kmem_zalloc(sizeof (kmap_t), KM_SLEEP);
	newp->k_size = size;
	if (locked_kernel_page) {
		newp->k_lockpp = pp;
		if (page_lock_upgraded) {
			newp->k_lk_upgrade = 1;
		}
	} else {
		newp->k_lockpp = NULL;
	}
	newp->k_kvaddr = kvaddr;

	/*
	 * Add the mapping info structure to the linked list.
	 */
	mutex_enter(&memtestp->m_mutex);
	newp->k_next = memtestp->m_kmapp;
	if (newp->k_next != NULL)
		newp->k_next->k_prev = newp;
	memtestp->m_kmapp = newp;
	DPRINTF(3, "%s: newp=0x%p, (next=0x%p, prev=0x%p)\n",
	    fname, newp, newp->k_next, newp->k_prev);
	DPRINTF(3, "%s: memtestp->m_kmapp=0x%p (next=0x%p, prev=0x%p)\n",
	    fname, memtestp->m_kmapp, memtestp->m_kmapp->k_next,
	    memtestp->m_kmapp->k_prev);
	mutex_exit(&memtestp->m_mutex);

	/*
	 * Add the page offset.
	 */
	kvaddr = (caddr_t)((uint64_t)kvaddr + (paddr & PAGEOFFSET));

	/*
	 * Sanity check.
	 */
	if ((paddr2 = memtest_kva_to_pa(kvaddr)) !=  paddr) {
		DPRINTF(0, "%s: derived paddr2=0x%lx != "
		    "original paddr=0x%lx\n", fname, paddr2, paddr);
		return (NULL);
	}

	DPRINTF(3, "%s: returning: kvaddr=0x%p, paddr=0x%llx\n",
	    fname, kvaddr, paddr2);
	return (kvaddr);
}

/*
 * This routine sets up a kernel virtual mapping to the same
 * physical address as the user virtual address passed in.
 *
 * NOTE: for sun4v the physical address is actually a real address.
 */
caddr_t
memtest_map_u2kvaddr(mdata_t *mdatap, caddr_t uvaddr, uint64_t attr,
		uint_t flags, uint_t size)
{
	ioc_t		*iocp = mdatap->m_iocp;
	proc_t		*procp;
	uint64_t	paddr;
	caddr_t		kvaddr;
	char		*fname = "memtest_map_u2kvaddr";

	DPRINTF(3, "%s: uvaddr=0x%p, attr=0x%x, flags=0x%x, size=0x%x\n",
	    fname, uvaddr, attr, flags, size);

	/*
	 * NOTE: this routine can currently only handle mapping one page.
	 */
	if (size > MMU_PAGESIZE) {
		DPRINTF(0, "%s: size=0x%x is greater than 0x%x\n",
		    fname, size, MMU_PAGESIZE);
		return (NULL);
	}

	/*
	 * Get the physical page and page frame number
	 * for the user virtual address.
	 */
	if (F_PID(iocp)) {
		DPRINTF(3, "%s: using process id=0x%x (%d decimal)\n",
		    fname, iocp->ioc_pid, iocp->ioc_pid);
		mutex_enter(&pidlock);
		procp = prfind(iocp->ioc_pid);
		mutex_exit(&pidlock);
	} else {
		DPRINTF(3, "%s: using current/mtst process\n", fname);
		procp = ttoproc(curthread);
	}

	if ((paddr = memtest_uva_to_pa(uvaddr, procp)) == -1)
		return (NULL);

	/*
	 * Setup the mapping.
	 */
	if ((kvaddr = memtest_map_p2kvaddr(mdatap, paddr, size, attr, flags))
	    == NULL) {
		DPRINTF(0, "%s: memtest_map_p2kvaddr("
		    "paddr=0x%llx, size=0x%x, attr=0x%x, flags=0x%x) "
		    "FAILED\n", fname, paddr, size, attr, flags);
		return (NULL);
	}

	DPRINTF(3, "%s: returning kvaddr=0x%p\n", fname, kvaddr);
	return (kvaddr);
}

/*
 * Offline all cpus which are not required by the current test.
 */
int
memtest_offline_cpus(mdata_t *mdatap)
{
	memtest_t	*memtestp = mdatap->m_memtestp;
	ioc_t		*iocp = mdatap->m_iocp;
	struct cpu	*cp = NULL;
	int		i, j;
	int		req_cpu_flag;
	char		*fname = "memtest_offline_cpus";

	/*
	 * If test is single threaded simply offline all other cpus.
	 * Multi-threaded tests bind to different cpus, so all of these
	 * must remain online.  Each required thread has an mdata struct
	 * which can be checked.
	 */
	mutex_enter(&cpu_lock);
	for (i = 0; i < _ncpu; i++) {
		if ((cp = cpu_get(i)) == NULL)
			continue;

		req_cpu_flag = FALSE;
		for (j = 0; j < iocp->ioc_nthreads; j++) {
			if (i == memtestp->m_mdatap[j]->m_cip->c_cpuid) {
				req_cpu_flag = TRUE;
				break;
			}
		}

		if (req_cpu_flag == FALSE) {
			if (memtest_check_cpu_status(cp) == P_ONLINE) {
				if (cpu_offline(cp, 0) != 0) {
					DPRINTF(0, "%s: unable to offline cpu "
					    "%d\n", fname, i);
					mutex_exit(&cpu_lock);
					return (-1);
				}
				/*
				 * Update this cpus cpu_info struct to reflect
				 * it's now offline.
				 */
				memtestp->m_cip[cp->cpu_id].c_offlined = 1;
				DPRINTF(3, "%s: cpu %d offlined\n", fname, i);
			}
		}
	}

	mutex_exit(&cpu_lock);
	return (0);
}

/*
 * Online a single cpu specified by it's cpu struct.
 *
 * This will online a cpu even if it was not offlined by the injector,
 * it will however set the status in the injectors cpu_info struct.
 */
int
memtest_online_cpu(mdata_t *mdatap, struct cpu *cp)
{
	memtest_t	*memtestp = mdatap->m_memtestp;
	char		*fname = "memtest_online_cpu";

	mutex_enter(&cpu_lock);
	if (memtest_check_cpu_status(cp) == P_OFFLINE) {
		if (cpu_online(cp) != 0) {
			DPRINTF(0, "%s: couldn't bring cpu %d online\n",
			    fname, cp->cpu_id);
			mutex_exit(&cpu_lock);
			return (-1);
		}
		DPRINTF(3, "%s: cpu %d brought online\n", fname, cp->cpu_id);
	}

	mutex_exit(&cpu_lock);

	/*
	 * Update this cpus cpu_info struct to reflect it's now online.
	 */
	memtestp->m_cip[cp->cpu_id].c_offlined = 0;

	return (0);
}

/*
 * Online all offlined cpus which were previously offlined by the injector.
 *
 * The cpus that the injector offlined are marked via the c_offlined flag in
 * the cpu_info struct that is maintained in the driver for each system cpu.
 */
int
memtest_online_cpus(mdata_t *mdatap)
{
	memtest_t	*memtestp = mdatap->m_memtestp;
	struct cpu	*cp = NULL;
	int		i;
	char		*fname = "memtest_online_cpus";

	mutex_enter(&cpu_lock);
	for (i = 0; i < _ncpu; i++) {
		if ((cp = cpu_get(i)) == NULL)
			continue;

		if ((memtest_check_cpu_status(cp) == P_OFFLINE) &&
		    (memtestp->m_cip[cp->cpu_id].c_offlined == 1)) {
			if (cpu_online(cp) != 0) {
				DPRINTF(0, "%s: unable to bring cpu %d "
				    "online\n", fname, cp->cpu_id);
				mutex_exit(&cpu_lock);
				return (-1);
			}
			/*
			 * Update this cpus cpu_info struct to reflect
			 * it's now back online.
			 */
			memtestp->m_cip[cp->cpu_id].c_offlined = 0;
			DPRINTF(3, "%s: cpu %d back online\n", fname, i);
		}
	}

	mutex_exit(&cpu_lock);
	return (0);
}

/*
 * 64-bit population count, use well-known popcnt trick.
 * We could use the UltraSPARC V9 POPC instruction, but some
 * CPUs including Cheetahplus and Jaguar do not support that
 * instruction.
 */
int
memtest_popc64(uint64_t val)
{
	int cnt;

	for (cnt = 0; val != 0; val &= val - 1)
		cnt++;
	return (cnt);
}

/*
 * This routine is called after executing a test in any mode.
 */
int
memtest_post_test(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		ret;
	char		*fname = "memtest_post_test";

	DPRINTF(1, "%s(mdatap=0x%p)\n", fname, mdatap);

	/*
	 * Re-enable kpreempt for the tests which disabled it (multi-threads).
	 */
	if (iocp->ioc_nthreads == 1) {
		if (QF_CYC_SUSPEND_EN(iocp)) {
			mutex_enter(&cpu_lock);
			cyclic_resume();
			mutex_exit(&cpu_lock);
		}
		kpreempt_enable();
	}

	/*
	 * Online all system cpus that were disabled by the injector.
	 * Note that it is safe to run the memtest_online_cpus()
	 * routine even if no CPUs were disabled by the injector.
	 */
	if (!QF_OFFLINE_CPUS_DIS(iocp) && !QF_ONLINE_CPUS_BFI(iocp)) {
		if (memtest_online_cpus(mdatap) != 0) {
			DPRINTF(0, "%s: could not re-online all system "
			    "cpu(s)\n", fname);
		}
	}

	/*
	 * If the injector paused cpus in pre_test, start them again here.
	 * Note that cpus are only paused if the "pause" option is specified.
	 */
	if (QF_PAUSE_CPUS_EN(iocp) && !QF_UNPAUSE_CPUS_BFI(iocp) &&
	    !QF_OFFLINE_CPUS_EN(iocp) && (iocp->ioc_nthreads == 1)) {
		mutex_enter(&cpu_lock);
		(void) start_cpus();
		mutex_exit(&cpu_lock);
	}

	(void) memtest_restore_scrubbers(mdatap);

	ret = memtest_free_resources(mdatap);

	return (ret);
}

/*
 * This routine pre-initializes the mdata_t data structures
 * for each thread with thread-unique information.
 * It expects to be called with the data pointer for thread 0.
 */
int
memtest_pre_init_threads(mdata_t *mdatap)
{
	memtest_t	*memtestp = mdatap->m_memtestp;
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	status;
	int		nthreads = iocp->ioc_nthreads;
	int		i, cpuid;
	char		str[40];
	char		*fname = "memtest_pre_init_threads";

	/*
	 * Sanity check.
	 */
	if (mdatap != memtestp->m_mdatap[0]) {
		DPRINTF(0, "%s: not called with pointer "
		    "to thread 0 data structure\n", fname);
		return (1);
	}

	/*
	 * Initialize unique info for each thread.
	 */
	for (i = 0; i < nthreads; i++) {
		cpuid = iocp->ioc_thr2cpu_binding[i];
		DPRINTF(2, "%s: cross calling cpuid=%d, thread=%d, "
		    "mdatap=0x%p\n", fname, cpuid, i, memtestp->m_mdatap[i]);
		xc_one(cpuid, (xcfunc_t *)memtest_init_thread_xcfunc,
		    (uint64_t)memtestp->m_mdatap[i], (uint64_t)&status);
		if (memtest_debug > 0) {
			(void) sprintf(str, "%s: thread=%d", fname, i);
			memtest_dump_mdata(memtestp->m_mdatap[i], str);
		}
	}

	return (0);
}

/*
 * This routine is called prior to executing a test.
 */
int
memtest_pre_test(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		offline_cpus = 0;
	int		ret = 0;
	char		*fname = "memtest_pre_test";

	/*
	 * If the store flag is set and this is a load or fetch access
	 * command modify the command access type to be a store AFTER
	 * the command has been looked up and verified.
	 */
	if (F_STORE(iocp) && ((ERR_ACC(iocp->ioc_command) == ERR_ACC_LOAD) ||
	    (ERR_ACC(iocp->ioc_command) == ERR_ACC_FETCH))) {
		DPRINTF(2, "%s: setting access type to STORE\n", fname);
		iocp->ioc_command &= ~(ERR_ACC_MASK << ERR_ACC_SHIFT);
		iocp->ioc_command |= (ERR_ACC_STORE << ERR_ACC_SHIFT);
	}

	/*
	 * Check ESRs on all CPUs for any latent (pre-injection) errors.
	 */
	if (memtest_flags & MFLAGS_CHECK_ESRS_PRE_TEST1) {
		if (ret = memtest_check_esrs(mdatap, "memtest_pre_test: 1")) {
			DPRINTF(0, "%s: call to memtest_check_esrs() "
			    "FAILED!\n", fname);
			return (EIO);
		}
	}

	/*
	 * Ensure errors are enabled on the strand/cpuid which will
	 * consume the injected error.
	 */
	if (!F_NOSET_EERS(iocp)) {
		if (memtest_enable_errors(mdatap) != 0)
			return (EIO);
	}

	/*
	 * Check ESRs on all CPUs again for any latent errors that have been
	 * detected after enabling the default set of ESR regs.
	 */
	if (memtest_flags & MFLAGS_CHECK_ESRS_PRE_TEST2) {
		if (ret = memtest_check_esrs(mdatap, "memtest_pre_test: 2")) {
			DPRINTF(0, "%s: call to "
			    "memtest_check_esrs() FAILED!\n", fname);
			return (EIO);
		}
	}

	DPRINTF(1, "%s: ioc_addr=0x%llx\n, ioc_databuf=0x%llx", fname,
	    iocp->ioc_addr, iocp->ioc_databuf);
	DPRINTF(1, "%s: ioc_xorpat=0x%llx, ioc_c_offset=0x%x, "
	    "ioc_a_offset=0x%x\n", fname, iocp->ioc_xorpat,
	    iocp->ioc_c_offset, iocp->ioc_a_offset);

	/*
	 * Do initialization specific to the different error modes.
	 */
	switch (ERR_MODE(iocp->ioc_command)) {
	case ERR_MODE_NONE:
		ret = memtest_pre_test_nomode(mdatap);
		break;
	case ERR_MODE_HYPR:
	case ERR_MODE_KERN:
	case ERR_MODE_OBP:
		ret = memtest_pre_test_kernel(mdatap);
		break;
	case ERR_MODE_DMA:
	case ERR_MODE_UDMA:
	case ERR_MODE_USER:
		ret = memtest_pre_test_user(mdatap);
		break;
	default:
		DPRINTF(0, "%s: invalid ERR_MODE=0x%x\n",
		    fname, ERR_MODE(iocp->ioc_command));
		return (EINVAL);
	}

	/*
	 * Check for specific pre_test routine error.
	 */
	if (ret != 0) {
		DPRINTF(0, "%s: initialization for mode=0x%x FAILED!\n",
		    fname, ERR_MODE(iocp->ioc_command));
		(void) memtest_free_resources(mdatap);
		return (ret);
	}

	(void) memtest_set_scrubbers(mdatap);

	/*
	 * Offline all system cpus that are not required for the test,
	 * but only for the types of tests that need it and if the user
	 * did not override this behavior.
	 *
	 * CPU pausing and offlining are mutually exclusive, offlining
	 * is done by default for certain test types, but can be forced
	 * via a user option.  Pausing is only available via an option,
	 * and if both offlining and pausing were specified, offlining
	 * takes precedence.
	 *
	 * Note that USER mode tests do not do cpu offlining because of
	 * the extra overhead involved in this, in future a USER level
	 * offline routine (and matching ioctl) can be added to support
	 * this if it becomes necessary.
	 *
	 * Also note that pausing of CPUs must be done after initial memory
	 * allocations, page lockings, and mappings have been set up in order
	 * to avoid executing code that may block while CPUs are paused.
	 */
	if (QF_OFFLINE_CPUS_EN(iocp)) {
		DPRINTF(2, "%s: offlining system cpus because user option "
		    "set\n", fname);
		offline_cpus = 1;
	} else if (QF_PAUSE_CPUS_EN(iocp) && (iocp->ioc_nthreads == 1)) {
		DPRINTF(2, "%s: pausing system cpus because user option set\n",
		    fname);
		mutex_enter(&cpu_lock);
		(void) pause_cpus(NULL);
		mutex_exit(&cpu_lock);
	} else if (!QF_OFFLINE_CPUS_DIS(iocp)) {
		if ((ERR_CLASS_ISMEM(iocp->ioc_command) ||
		    ERR_CLASS_ISCACHE(iocp->ioc_command) ||
		    ERR_CLASS_ISINT(iocp->ioc_command)) &&
		    !ERR_MISC_ISLOWIMPACT(iocp->ioc_command) &&
		    !ERR_MODE_ISUSER(iocp->ioc_command) &&
		    !ERR_MODE_ISUDMA(iocp->ioc_command)) {
			DPRINTF(2, "%s: offlining system cpus due to default "
			    "command behavior\n", fname);
			offline_cpus = 1;
		}
	}

	if (offline_cpus != 0) {
		if (memtest_offline_cpus(mdatap) != 0) {
			DPRINTF(0, "%s: could not offline system cpu(s)\n",
			    fname);
			(void) memtest_restore_scrubbers(mdatap);
			(void) memtest_free_resources(mdatap);
			return (EIO);
		}
	}

	/*
	 * Disable kpreempt for single-threaded tests only,
	 * multi-thread tests will do their own kpreempt_disables.
	 */
	if (iocp->ioc_nthreads == 1) {
		kpreempt_disable();
		if (QF_CYC_SUSPEND_EN(iocp)) {
			mutex_enter(&cpu_lock);
			cyclic_suspend();
			mutex_exit(&cpu_lock);
		}
	}

	/*
	 * Print out the addresses being used.
	 */
	if (F_VERBOSE(iocp) || (memtest_debug > 0)) {
		cmn_err(CE_NOTE, "%s: addresses being used for corruption "
		    "and access (not accurate for TLB miss tests):\n", fname);
		cmn_err(CE_NOTE, "uvaddr_c=0x%p, kvaddr_c=0x%p,\n"
		    "\t\traddr_c=0x%08x.%08x, paddr_c=0x%08x.%08x\n",
		    (void *)mdatap->m_uvaddr_c, (void *)mdatap->m_kvaddr_c,
		    PRTF_64_TO_32(mdatap->m_raddr_c),
		    PRTF_64_TO_32(mdatap->m_paddr_c));
		cmn_err(CE_NOTE, "uvaddr_a=0x%p, kvaddr_a=0x%p,\n"
		    "\t\traddr_a=0x%08x.%08x, paddr_a=0x%08x.%08x\n",
		    (void *)mdatap->m_uvaddr_a, (void *)mdatap->m_kvaddr_a,
		    PRTF_64_TO_32(mdatap->m_raddr_a),
		    PRTF_64_TO_32(mdatap->m_paddr_a));
	}

	return (ret);
}

/*
 * This routine calls different kernel asm prefetch routines based
 * on the flag set in the ioc_t structure.
 */
void
memtest_prefetch_access(ioc_t *iocp, caddr_t vaddr)
{
	if (F_STORE(iocp)) {
		memtest_prefetch_wr_access(vaddr);
	} else {
		memtest_prefetch_rd_access(vaddr);
	}
}

/*
 * This routine starts a thread running the specified function
 * on the cpu specified by the thread structure passed in.
 */
int
memtest_start_thread(mdata_t *mdatap, void (*function)(), char *caller)
{
	struct cpu	*cp;
	kthread_t	*tp;
	char		*fname = "memtest_start_thread";

	/*
	 * Get the cpu structure for the producer cpu.
	 */
	mutex_enter(&cpu_lock);
	if ((cp = cpu_get(mdatap->m_cip->c_cpuid)) == NULL) {
		mutex_exit(&cpu_lock);
		cmn_err(CE_WARN, "%s: %s: failed to get cpu "
		    "structure for thread %d\n", fname,
		    caller, mdatap->m_threadno);
		return (EIO);
	}
	mutex_exit(&cpu_lock);

	DPRINTF(3, "%s: %s: mdatap=0x%x, cp=0x%p, function=0x%p\n",
	    fname, caller, mdatap, cp, function);

	/*
	 * Create the thread in the stopped state.
	 */
	tp = thread_create(NULL, 0, function, (caddr_t)mdatap, 0, &p0,
	    TS_STOPPED, MAXCLSYSPRI - 1);

	/*
	 * Bind the thread to the desired cpu.
	 */
	thread_lock(tp);
	tp->t_preempt = 1;
	tp->t_bound_cpu = cp;
	tp->t_affinitycnt = 1;
	tp->t_cpu = cp;
	thread_unlock(tp);

	DPRINTF(3, "%s: %s: starting thread\n", fname, caller);

	/*
	 * Now make the producer thread runnable.
	 */
	THREAD_SET_STATE(tp, TS_RUN, &transition_lock);
	CL_SETRUN(tp);
	thread_unlock(tp);

	return (0);
}

/*
 * This routine unlocks all the kernel pages on the linked list passed in.
 * It expects to be called with the memtest mutex held.
 */
int
memtest_unlock_kernel_pages(kplock_t *kplockp)
{
	kplock_t	*nextp;
	pfn_t		pfn;
	page_t		*pp;
	uint64_t	paddr;
	int		pages;
	int		errcnt = 0;
	char		*fname = "memtest_unlock_kernel_pages";

	DPRINTF(2, "%s(kplockp=0x%p)\n", fname, kplockp);

	while (kplockp != NULL) {
		paddr = kplockp->k_paddr;
		pages = kplockp->k_npages;
		nextp = kplockp->k_next;
		DPRINTF(3, "%s: kplockp=0x%p, paddr=0x%llx, pages=%d\n",
		    fname, kplockp, paddr, pages);

		/*
		 * Free the pages.
		 */
		if (pages <= 0) {
			DPRINTF(0, "%s: invalid pages=%d\n", fname, pages);
			errcnt++;
			break;
		} else {
			while (pages) {
				pfn = paddr >> MMU_PAGESHIFT;
				if (pf_is_memory(pfn) == 0) {
					DPRINTF(0, "%s: paddr=0x%llx is not "
					    "memory\n", fname, paddr);
					errcnt++;
					break;
				}
				pp = page_numtopp_nolock(pfn);
				page_unlock(pp);
				pages--;
			}
		}

		/*
		 * Free mapping structure itself.
		 */
		DPRINTF(3, "%s: freeing locking struct 0x%p, len=0x%x\n",
		    fname, kplockp, sizeof (kplock_t));
		kmem_free(kplockp, sizeof (kplock_t));

		kplockp = nextp;
	}
	return (errcnt);
}

/*
 * This function unlocks all the user pages on the linked list passed in.
 * It expects to be called with the memtest mutex held.
 */
int
memtest_unlock_user_pages(uplock_t *uplockp)
{
	void		*nextp;
	char		*fname = "memtest_unlock_user_pages";

	DPRINTF(2, "%s(uplockp=0x%p)\n", fname, uplockp);

	while (uplockp != NULL) {
		nextp = uplockp->p_next;
		DPRINTF(3, "%s: unlocking pages uplockp=0x%p, nextp=0x%p\n",
		    fname, uplockp, nextp);

		/*
		 * Sanity check.
		 */
		if ((uplockp->p_pplist == NULL) ||
		    (uplockp->p_uvaddr == NULL)) {
			DPRINTF(0, "%s: one or more NULL arguments: "
			    "uplockp=0x%p, pplist=0x%p, uvaddr=0x%p\n",
			    fname, uplockp, uplockp->p_pplist,
			    uplockp->p_uvaddr);
			return (EIO);
		}

		/*
		 * Unlock the pages.
		 */
		DPRINTF(3, "%s: calling as_pageunlock"
		    "(asp=0x%p, pplist=0x%p, uvaddr=0x%p, size=0x%x, "
		    "rw=0x%x)\n", fname, uplockp->p_asp, uplockp->p_pplist,
		    uplockp->p_uvaddr, uplockp->p_size, S_WRITE);
		as_pageunlock(uplockp->p_asp, uplockp->p_pplist,
		    uplockp->p_uvaddr, uplockp->p_size, S_WRITE);

		/*
		 * Free mapping structure itself.
		 */
		DPRINTF(3, "%s: freeing locking struct 0x%p, len=0x%x\n",
		    fname, uplockp, sizeof (uplock_t));
		kmem_free(uplockp, sizeof (uplock_t));

		uplockp = nextp;
	}

	return (0);
}

/*
 * This routine translates a user virtual address into a physical address.
 */
uint64_t
memtest_uva_to_pa(caddr_t uvaddr, proc_t *procp)
{
	pfn_t		pfn;
	uint64_t	paddr, data;
	char		*fname = "memtest_uva_to_pa";

	/*
	 * If the process pointer inpar is NULL use the current process.
	 */
	if (procp == NULL) {
		procp = ttoproc(curthread);
	}

	/*
	 * Get the page frame number for the user virtual address.
	 * Need to hold lock for hat_getpfnum()
	 */
	AS_LOCK_ENTER(procp->p_as, &procp->p_as->a_lock, RW_READER);
	pfn = hat_getpfnum(procp->p_as->a_hat, uvaddr);
	AS_LOCK_EXIT(procp->p_as, &procp->p_as->a_lock);

	/*
	 * The lookup may fail because the user mapping is not loaded.
	 * Try a copyin to get it loaded.
	 */
	if (pfn == -1) {
		DPRINTF(3, "%s: translation failed for uvaddr=0x%p, "
		    "procp=0x%p, trying copyin...\n", fname, uvaddr, procp);
		if (ddi_copyin((void *)uvaddr, &data, sizeof (data), 0)
		    != 0) {
			DPRINTF(0, "%s: translation and subsequent copyin "
			    "attempt failed for uvaddr=0x%p, procp=0x%p "
			    "likely the uvaddr is not owned by the target "
			    "process, try using the \"pmap\" command\n",
			    fname, uvaddr, procp);
			return (-1);
		}

		/*
		 * Try getting the pfn again.
		 */
		AS_LOCK_ENTER(procp->p_as, &procp->p_as->a_lock, RW_READER);
		pfn = hat_getpfnum(procp->p_as->a_hat, uvaddr);
		AS_LOCK_EXIT(procp->p_as, &procp->p_as->a_lock);
		if (pfn == -1) {
			DPRINTF(0, "%s: translation failed for uvaddr=0x%p, "
			    "procp=0x%p\n", fname, uvaddr, procp);
			return (-1);
		}
	}

	paddr = (((uint64_t)pfn << MMU_PAGESHIFT) |
	    ((uint64_t)uvaddr & MMU_PAGEOFFSET));

	DPRINTF(3, "%s: returning: uvaddr=0x%p is mapped to paddr=0x%llx\n",
	    fname, uvaddr, paddr);

	return (paddr);
}

/*
 * This function is used to synchronize between threads.
 */
int
memtest_wait_sync(volatile int *syncp, int exp_sync, int usec_timeout,
			char *str)
{
	uint64_t	nsec_start, nsec_current, nsec_2wait;
	int		obs_sync;
	int		ret = SYNC_STATUS_OK;
	char		*fname = "memtest_wait_sync";

	DPRINTF(3, "%s: syncp=0x%p, exp_sync=%d, "
	    "usec_timeout=0x%x, char=%s\n", fname,
	    syncp, exp_sync, usec_timeout, str);

	nsec_2wait = usec_timeout * 1000;

	nsec_current = nsec_start = gethrtime();

	/*
	 * Wait for sync variable to become the expected value.
	 */
	obs_sync = *syncp;
	while ((obs_sync != exp_sync) && (obs_sync != -1)) {
		/*
		 * Check for timeout and take into account counter wrap.
		 */
		nsec_current = gethrtime();
		if (nsec_current < nsec_start)
			nsec_start = nsec_current;
		if ((nsec_2wait != 0) &&
		    ((nsec_current - nsec_start) >= nsec_2wait)) {
			cmn_err(CE_WARN, "%s: time out waiting for sync=%d "
			    "(observed=%d) (currently=%d)\n",
			    str, exp_sync, obs_sync, *syncp);
			break;
		}
		obs_sync = *syncp;
	}

	if (obs_sync == exp_sync)
		ret = SYNC_STATUS_OK;
	else if (obs_sync == -1)
		ret = SYNC_STATUS_ERROR;
	else
		ret = SYNC_STATUS_TIMEOUT;

	switch (ret) {
	case SYNC_STATUS_OK:
		DPRINTF(3, "%s: %s: SYNC_STATUS_OK\n", fname, str);
		break;
	case SYNC_STATUS_TIMEOUT:
		DPRINTF(0, "%s: %s: SYNC_STATUS_TIMEOUT "
		    "waiting for sync=%d\n", fname, str, exp_sync);
		break;
	case SYNC_STATUS_ERROR:
		DPRINTF(0, "%s: %s: SYNC_STATUS_ERROR "
		    "waiting for sync=%d\n", fname, str, exp_sync);
		break;
	default:
		DPRINTF(0, "%s: %s: invalid sync value %d "
		    "waiting for sync=%d\n", fname, str, ret, exp_sync);
		break;
	}

	return (ret);
}

/*
 * This is a dummy "not implemented" routine used to fill in ops tables.
 */
int
notimp(/* typeless */)
{
	DPRINTF(1, "notimp: unimplemented vector routine called!\n");
	return (0);
}

/*
 * This is a dummy "not supported" routine used to fill in ops tables.
 *
 * The difference between this routine and the above is that this will
 * cause a failure if called to flag that an unsupported opsvec was called.
 */
int
notsup(/* typeless */)
{
	DPRINTF(0, "notsup: unsupported vector routine called!\n");
	return (-1);
}

/*
 * This routine cross calls all cpus with the
 * specified function and arguments.
 */
int
memtest_xc_cpus(mdata_t *mdatap, void (*func)(uint64_t, uint64_t), char *str)
{
	system_info_t	*sip = mdatap->m_sip;
	cpu_t		*cp;
	int		i, status = 0;
	char		*fname = "memtest_xc_cpus";

	ASSERT(func != NULL);

	mdatap->m_msgp = str;
	for (i = 0; i < sip->s_maxcpuid; i++) {
		mutex_enter(&cpu_lock);
		if (CPU_XCALL_READY(i)) {
			if ((cp = cpu_get(i)) == NULL) {
				mutex_exit(&cpu_lock);
				continue;
			}
			DPRINTF(4, "%s: %s: cross calling "
			    "cpuid=%d, func=0x%p, mdatap=0x%p\n",
			    fname, str, cp->cpu_id, func, mdatap);
			xc_one(cp->cpu_id, (xcfunc_t *)func, (uint64_t)mdatap,
			    (uint64_t)&status);
			DPRINTF(4, "%s: %s: cross call function "
			    "returned status=%d\n", fname, str, status);
			if (status != 0) {
				DPRINTF(0, "%s: %s: CPU %d: cross "
				    "call function returned an error\n",
				    fname, str, cp->cpu_id);
				mutex_exit(&cpu_lock);
				return (status);
			}
		}
		mutex_exit(&cpu_lock);
	}
	return (0);
}

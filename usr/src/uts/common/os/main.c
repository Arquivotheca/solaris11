/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/pcb.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/priocntl.h>
#include <sys/procset.h>
#include <sys/disp.h>
#include <sys/callo.h>
#include <sys/callb.h>
#include <sys/debug.h>
#include <sys/conf.h>
#include <sys/bootconf.h>
#include <sys/utsname.h>
#include <sys/cmn_err.h>
#include <sys/vmparam.h>
#include <sys/modctl.h>
#include <sys/vm.h>
#include <sys/callb.h>
#include <sys/ddi_timer.h>
#include <sys/kmem.h>
#include <sys/vmem.h>
#include <sys/cpuvar.h>
#include <sys/cladm.h>
#include <sys/corectl.h>
#include <sys/exec.h>
#include <sys/syscall.h>
#include <sys/reboot.h>
#include <sys/task.h>
#include <sys/exacct.h>
#include <sys/autoconf.h>
#include <sys/errorq.h>
#include <sys/class.h>
#include <sys/stack.h>
#include <sys/brand.h>
#include <sys/mmapobj.h>
#include <sys/numaio_priv.h>
#include <sys/var.h>
#include <sys/ksynch.h>
#include <sys/crypto/impl.h>

#include <vm/as.h>
#include <vm/seg_kmem.h>
#include <vm/vmtask.h>
#include <sys/dc_ki.h>

#include <c2/audit.h>
#include <sys/bootprops.h>

#if defined(__x86)
#include <sys/boot_console.h>
#endif	/* __x86 */

/* well known processes */
proc_t *proc_sched;		/* memory scheduler */
proc_t *proc_init;		/* init */
proc_t *proc_pageout;		/* pageout daemon */
proc_t *proc_fsflush;		/* fsflush daemon */

pgcnt_t	maxmem;		/* Maximum available memory in pages.	*/
pgcnt_t	freemem;	/* Current available memory in pages.	*/
int	interrupts_unleashed;	/* set when we do the first spl0() */

kmem_cache_t *process_cache;	/* kmem cache for proc structures */

/*
 * Indicates whether the auditing module (c2audit) is loaded. Possible
 * values are:
 * 0 - c2audit module is excluded in /etc/system and cannot be loaded
 * 1 - c2audit module is not loaded but can be anytime
 * 2 - c2audit module is loaded
 */
int audit_active = C2AUDIT_DISABLED;

/*
 * Process 0's lwp directory and lwpid hash table.
 */
lwpdir_t p0_lwpdir[2];
tidhash_t p0_tidhash[2];
lwpent_t p0_lep;

/*
 * Machine-independent initialization code
 * Called from cold start routine as
 * soon as a stack and segmentation
 * have been established.
 * Functions:
 *	clear and free user core
 *	turn on clock
 *	hand craft 0th process
 *	call all initialization routines
 *	fork	- process 0 to schedule
 *		- process 1 execute bootstrap
 *		- process 2 to page out
 *	create system threads
 */

int cluster_bootflags = 0;

void
cluster_wrapper(void)
{
	cluster();
	panic("cluster()  returned");
}

char initname[INITNAME_SZ] = "/usr/sbin/init";	/* also referenced by zone0 */
char initargs[BOOTARGS_MAX] = "";		/* also referenced by zone0 */

/*
 * Construct a stack for init containing the arguments to it, then
 * pass control to exec_common.
 */
int
exec_init(const char *initpath, const char *args)
{
	caddr32_t ucp;
	caddr32_t *uap;
	caddr32_t *argv;
	caddr32_t exec_fnamep;
	char *scratchargs;
	int i, sarg;
	size_t argvlen, alen;
	boolean_t in_arg;
	int argc = 0;
	int error = 0, count = 0;
	proc_t *p = ttoproc(curthread);
	klwp_t *lwp = ttolwp(curthread);
	int brand_action;

	if (args == NULL)
		args = "";

	alen = strlen(initpath) + 1 + strlen(args) + 1;
	scratchargs = kmem_alloc(alen, KM_SLEEP);
	(void) snprintf(scratchargs, alen, "%s %s", initpath, args);

	/*
	 * We do a quick two state parse of the string to sort out how big
	 * argc should be.
	 */
	in_arg = B_FALSE;
	for (i = 0; i < strlen(scratchargs); i++) {
		if (scratchargs[i] == ' ' || scratchargs[i] == '\0') {
			if (in_arg) {
				in_arg = B_FALSE;
				argc++;
			}
		} else {
			in_arg = B_TRUE;
		}
	}
	argvlen = sizeof (caddr32_t) * (argc + 1);
	argv = kmem_zalloc(argvlen, KM_SLEEP);

	/*
	 * We pull off a bit of a hack here.  We work our way through the
	 * args string, putting nulls at the ends of space delimited tokens
	 * (boot args don't support quoting at this time).  Then we just
	 * copy the whole mess to userland in one go.  In other words, we
	 * transform this: "init -s -r\0" into this on the stack:
	 *
	 *	-0x00 \0
	 *	-0x01 r
	 *	-0x02 -  <--------.
	 *	-0x03 \0	  |
	 *	-0x04 s		  |
	 *	-0x05 -  <------. |
	 *	-0x06 \0	| |
	 *	-0x07 t		| |
	 *	-0x08 i 	| |
	 *	-0x09 n		| |
	 *	-0x0a i  <---.  | |
	 *	-0x10 NULL   |  | |	(argv[3])
	 *	-0x14   -----|--|-'	(argv[2])
	 *	-0x18  ------|--'	(argv[1])
	 *	-0x1c -------'		(argv[0])
	 *
	 * Since we know the value of ucp at the beginning of this process,
	 * we can trivially compute the argv[] array which we also need to
	 * place in userland: argv[i] = ucp - sarg(i), where ucp is the
	 * stack ptr, and sarg is the string index of the start of the
	 * argument.
	 */
	ucp = (caddr32_t)(uintptr_t)p->p_usrstack;

	argc = 0;
	in_arg = B_FALSE;
	sarg = 0;

	for (i = 0; i < alen; i++) {
		if (scratchargs[i] == ' ' || scratchargs[i] == '\0') {
			if (in_arg == B_TRUE) {
				in_arg = B_FALSE;
				scratchargs[i] = '\0';
				argv[argc++] = ucp - (alen - sarg);
			}
		} else if (in_arg == B_FALSE) {
			in_arg = B_TRUE;
			sarg = i;
		}
	}
	ucp -= alen;
	error |= copyout(scratchargs, (caddr_t)(uintptr_t)ucp, alen);

	uap = (caddr32_t *)P2ALIGN((uintptr_t)ucp, sizeof (caddr32_t));
	uap--;	/* advance to be below the word we're in */
	uap -= (argc + 1);	/* advance argc words down, plus one for NULL */
	error |= copyout(argv, uap, argvlen);

	if (error != 0) {
		zcmn_err(p->p_zone->zone_id, CE_WARN,
		    "Could not construct stack for init.\n");
		kmem_free(argv, argvlen);
		kmem_free(scratchargs, alen);
		return (EFAULT);
	}

	exec_fnamep = argv[0];
	kmem_free(argv, argvlen);
	kmem_free(scratchargs, alen);

	/*
	 * Point at the arguments.
	 */
	lwp->lwp_ap = lwp->lwp_arg;
	lwp->lwp_arg[0] = (uintptr_t)exec_fnamep;
	lwp->lwp_arg[1] = (uintptr_t)uap;
	lwp->lwp_arg[2] = NULL;
	lwp->lwp_arg[3] = 0;
	curthread->t_post_sys = 1;
	curthread->t_sysnum = SYS_execve;

	/*
	 * If we are executing init from zsched, we may have inherited its
	 * parent process's signal mask.  Clear it now so that we behave in
	 * the same way as when started from the global zone.
	 */
	sigemptyset(&curthread->t_hold);

	brand_action = ZONE_HAS_BRANDOPS(p->p_zone) ? EBA_BRAND : EBA_NONE;
again:
	error = exec_common(-1, (const char *)(uintptr_t)exec_fnamep,
	    (const char **)(uintptr_t)uap, NULL, brand_action, 0);

	/*
	 * Normally we would just set lwp_argsaved and t_post_sys and
	 * let post_syscall reset lwp_ap for us.  Unfortunately,
	 * exec_init isn't always called from a system call.  Instead
	 * of making a mess of trap_cleanup, we just reset the args
	 * pointer here.
	 */
	reset_syscall_args();

	switch (error) {
	case 0:
		return (0);

	case ENOENT:
		zcmn_err(p->p_zone->zone_id, CE_WARN,
		    "exec(%s) failed (file not found).\n", initpath);
		return (ENOENT);

	case EAGAIN:
	case EINTR:
		++count;
		if (count < 5) {
			zcmn_err(p->p_zone->zone_id, CE_WARN,
			    "exec(%s) failed with errno %d.  Retrying...\n",
			    initpath, error);
			goto again;
		}
	}

	zcmn_err(p->p_zone->zone_id, CE_WARN,
	    "exec(%s) failed with errno %d.", initpath, error);
	return (error);
}

/*
 * This routine does all of the common setup for invoking init; global
 * and non-global zones employ this routine for the functionality which is
 * in common.
 *
 * This program (init, presumably) must be a 32-bit process.
 */
int
start_init_common()
{
	proc_t *p = curproc;
	ASSERT_STACK_ALIGNED();
	p->p_zone->zone_proc_initpid = p->p_pid;

	p->p_cstime = p->p_stime = p->p_cutime = p->p_utime = 0;
	p->p_usrstack = (caddr_t)USRSTACK32;
	p->p_model = DATAMODEL_ILP32;
	p->p_stkprot = PROT_ZFOD & ~PROT_EXEC;
	p->p_datprot = PROT_ZFOD & ~PROT_EXEC;
	p->p_stk_ctl = INT32_MAX;

	p->p_as = as_alloc(p);
	p->p_as->a_userlimit = (caddr_t)USERLIMIT32;
	(void) hat_setup(p->p_as->a_hat, HAT_INIT);

	init_core();

	init_mstate(curthread, LMS_SYSTEM);
	return (exec_init(p->p_zone->zone_initname, p->p_zone->zone_bootargs));
}

/*
 * Start the initial user process for the global zone; once running, if
 * init should subsequently fail, it will be automatically be caught in the
 * exit(2) path, and restarted by restart_init().
 */
static void
start_init(void)
{
	proc_init = curproc;

	ASSERT(curproc->p_zone->zone_initname != NULL);

	if (start_init_common() != 0)
		halt("unix: Could not start init");
	lwp_rtt();
}

void
main(void)
{
	proc_t		*p = ttoproc(curthread);	/* &p0 */
	int		(**initptr)();
	extern void	sched();
	extern void	fsflush();
	extern int	(*init_tbl[])();
	extern int	(*mp_init_tbl[])();
	extern id_t	syscid, defaultcid;
	extern int	swaploaded;
	extern int	netboot;
	extern ib_boot_prop_t *iscsiboot_prop;
	extern void	vm_init(void);
	extern void	cbe_init_pre(void);
	extern void	cbe_init(void);
	extern void	clock_tick_intr_init(void);
	extern void	clock_init(void);
	extern void	physio_bufs_init(void);
	extern void	io_mvec_init(void);
	extern void	io_tag_init(void);
	extern void	pm_cfb_setup_intr(void);
	extern int	pm_adjust_timestamps(dev_info_t *, void *);
	extern void	start_other_cpus(int);
	extern void	sysevent_evc_thrinit();
	extern void	io_balancer(void);
	extern kmutex_t	ualock;
	extern ksema_t	fsflush_sema;
#if defined(__x86)
	extern void	fastboot_post_startup(void);
	extern int	splash_boot_start(void);
#endif
#pragma weak	active_dma_list_build
	extern void active_dma_list_build(void);
	/*
	 * In the horrible world of x86 in-lines, you can't get symbolic
	 * structure offsets a la genassym.  This assertion is here so
	 * that the next poor slob who innocently changes the offset of
	 * cpu_thread doesn't waste as much time as I just did finding
	 * out that it's hard-coded in i86/ml/i86.il.  Similarly for
	 * curcpup.  You're welcome.
	 */
	ASSERT(CPU == CPU->cpu_self);
	ASSERT(curthread == CPU->cpu_thread);
	ASSERT_STACK_ALIGNED();

	sigcld_list_create(p);

	/*
	 * We take the ualock until we have completed the startup
	 * to prevent kadmin() from disrupting this work. In particular,
	 * we don't want kadmin() to bring the system down while we are
	 * trying to start it up.
	 */
	mutex_enter(&ualock);

	/*
	 * Setup root lgroup and leaf lgroup for CPU 0
	 */
	lgrp_init(LGRP_INIT_STAGE2);

	/*
	 * Once 'startup()' completes, the thread_reaper() daemon would be
	 * created(in thread_init()). After that, it is safe to create threads
	 * that could exit. These exited threads will get reaped.
	 */
	startup();
	segkmem_gc();
	callb_init();
	cbe_init_pre();	/* x86 must initialize gethrtimef before timer_init */
	timer_init();	/* timer must be initialized before cyclic starts */
	cbe_init();
	callout_init();	/* callout table MUST be init'd after cyclics */
	clock_tick_intr_init(); /* must be called before clock_init() */
	clock_init();

#if defined(__x86)
	/*
	 * The boot animation thread uses cv_reltimedwait() and hence must get
	 * started after the callout mechanism has been initialized.
	 */
	if (console == CONS_SCREEN_GRAPHICS) {
		if (splash_boot_start() != 0)
			console = CONS_SCREEN_FB;
	}
#endif

	if (&active_dma_list_build) {
		/* build PROM IOMMU active DMA list */
		active_dma_list_build();
	}

	/*
	 * On some platforms, clkinitf() changes the timing source that
	 * gethrtime_unscaled() uses to generate timestamps.  cbe_init() calls
	 * clkinitf(), so re-initialize the microstate counters after the
	 * timesource has been chosen.
	 */
	init_mstate(&t0, LMS_SYSTEM);
	init_cpu_mstate(CPU, CMS_SYSTEM);

	/*
	 * May need to probe to determine latencies from CPU 0 after
	 * gethrtime() comes alive in cbe_init() and before enabling interrupts
	 * and copy and release any temporary memory allocated with BOP_ALLOC()
	 * before release_bootstrap() frees boot memory
	 */
	lgrp_init(LGRP_INIT_STAGE3);

	/*
	 * Call all system initialization functions.
	 */
	for (initptr = &init_tbl[0]; *initptr; initptr++)
		(**initptr)();
	/*
	 * Load iSCSI boot properties
	 */
	ld_ib_prop();
	/*
	 * initialize vm related stuff.
	 */
	vm_init();

	/*
	 * memory related I/O initialization
	 */
	physio_bufs_init();
	io_mvec_init();
	io_tag_init();

	ttolwp(curthread)->lwp_error = 0; /* XXX kludge for SCSI driver */

	/*
	 * Drop the interrupt level and allow interrupts.  At this point
	 * the DDI guarantees that interrupts are enabled.
	 */
	(void) spl0();
	interrupts_unleashed = 1;

	/*
	 * Need Kernel Cryptographic Framework (KCF) running before mounting
	 * root since ZFS needs KCF as many other methods of getting to
	 * the root filesystem.
	 */
	kcf_init();

	/*
	 * Create kmem cache for proc structures
	 */
	process_cache = kmem_cache_create("process_cache", sizeof (proc_t),
	    0, NULL, NULL, NULL, NULL, NULL, 0);

	/*
	 * NUMAIO_INIT_STAGE1 should be done before post_startup()
	 * and vfs_mountroot(). post_startup() loads machine
	 * class drivers and some of them (vswitch is one example)
	 * can make calls into numaio APIs
	 */
	numaio_init(NUMAIO_INIT_STAGE1);

	vfs_mountroot();	/* Mount the root file system */
	errorq_init();		/* after vfs_mountroot() so DDI root is ready */
	cpu_kstat_init(CPU);	/* after vfs_mountroot() so TOD is valid */
	ddi_walk_devs(ddi_root_node(), pm_adjust_timestamps, NULL);
				/* after vfs_mountroot() so hrestime is valid */

	post_startup();
	swaploaded = 1;

	/*
	 * Initialize Solaris Audit Subsystem
	 */
	audit_init();

	/*
	 * Plumb the protocol modules and drivers only if we are not
	 * networked booted, in this case we already did it in rootconf().
	 */
	if (netboot == 0 && iscsiboot_prop == NULL)
		(void) strplumb();

	gethrestime(&PTOU(curproc)->u_start);
	curthread->t_start = PTOU(curproc)->u_start.tv_sec;
	p->p_mstart = gethrtime();

	/*
	 * Perform setup functions that can only be done after root
	 * and swap have been set up.
	 */
	consconfig();
#ifndef	__sparc
	release_bootstrap();
#endif

	/*
	 * attach drivers with ddi-forceattach prop
	 * It must be done early enough to load hotplug drivers (e.g.
	 * pcmcia nexus) so that devices enumerated via hotplug is
	 * available before I/O subsystem is fully initialized.
	 */
	i_ddi_forceattach_drivers();

	/*
	 * Set the scan rate and other parameters of the paging subsystem.
	 */
	setupclock(0);

	/*
	 * Initialize process 0's lwp directory and lwpid hash table.
	 */
	p->p_lwpdir = p->p_lwpfree = p0_lwpdir;
	p->p_lwpdir->ld_next = p->p_lwpdir + 1;
	p->p_lwpdir_sz = 2;
	p->p_tidhash = p0_tidhash;
	p->p_tidhash_sz = 2;
	p0_lep.le_thread = curthread;
	p0_lep.le_lwpid = curthread->t_tid;
	p0_lep.le_start = curthread->t_start;
	lwp_hash_in(p, &p0_lep, p0_tidhash, 2, 0);

	/*
	 * Initialize extended accounting.
	 */
	exacct_init();

	/*
	 * Initialize threads of sysevent event channels
	 */
	sysevent_evc_thrinit();

	/*
	 * This must be done after post_startup() but before
	 * start_other_cpus()
	 */
	lgrp_init(LGRP_INIT_STAGE4);

	/*
	 * Perform MP initialization, if any.
	 */
	start_other_cpus(0);

	/*
	 * Notify the PG framework that all CPUs are online so any new
	 * class can configure the priority to utilization mapping.
	 */
	pg_priority_setup(v.v_nglobpris, v.v_nglobpris, disp_pri_util_map);

#ifdef	__sparc
	/*
	 * Release bootstrap here since PROM interfaces are
	 * used to start other CPUs above.
	 */
	release_bootstrap();
#endif

	/*
	 * Finish lgrp initialization after all CPUS are brought online.
	 */
	lgrp_init(LGRP_INIT_STAGE5);

	/*
	 * NUMAIO_INIT_STAGE2 should be done after all CPUs are started
	 */
	numaio_init(NUMAIO_INIT_STAGE2);

	/*
	 * After mp_init(), number of cpus are known (this is
	 * true for the time being, when there are actually
	 * hot pluggable cpus then this scheme  would not do).
	 * Any per cpu initialization is done here.
	 */
	kmem_mp_init();
	vmem_update(NULL);

	for (initptr = &mp_init_tbl[0]; *initptr; initptr++)
		(**initptr)();

	/*
	 * These must be called after start_other_cpus
	 */
	pm_cfb_setup_intr();
#if defined(__x86)
	fastboot_post_startup();
#endif
	/*
	 * fsflush_sema is used to synchronize between fsflush() thread and
	 * threads asking for a shutdown through kadmin(). As we don't know
	 * which one will run first, we have to initialize this semaphore
	 * before any of them run.
	 */
	sema_init(&fsflush_sema, 0, NULL, SEMA_DEFAULT, NULL);

	/*
	 * Make init process; enter scheduling loop with system process.
	 *
	 * Note that we manually assign the pids for these processes, for
	 * historical reasons.  If more pre-assigned pids are needed,
	 * FAMOUS_PIDS will have to be updated.
	 */

	/* create init process */
	if (newproc(start_init, NULL, defaultcid, 59, NULL,
	    FAMOUS_PID_INIT))
		panic("main: unable to fork init.");

	/* create pageout daemon */
	if (newproc(pageout, NULL, syscid, maxclsyspri - 1, NULL,
	    FAMOUS_PID_PAGEOUT))
		panic("main: unable to fork pageout()");

	/* create fsflush daemon */
	if (newproc(fsflush, NULL, syscid, minclsyspri, NULL,
	    FAMOUS_PID_FSFLUSH))
		panic("main: unable to fork fsflush()");

	/* create the load balancer daemon */
	if (newproc(io_balancer, NULL, syscid, minclsyspri, NULL, 0))
		panic("main: unable to fork io_balancer()");

	/* create cluster process if we're a member of one */
	if (cluster_bootflags & CLUSTER_BOOTED) {
		if (newproc(cluster_wrapper, NULL, syscid, minclsyspri,
		    NULL, 0)) {
			panic("main: unable to fork cluster()");
		}
	}

	/* create vmtasks helper process */
	vmtask_init();

	/*
	 * Create system threads (threads are associated with p0)
	 */

	/* create module uninstall daemon */
	/* BugID 1132273. If swapping over NFS need a bigger stack */
	(void) thread_create(NULL, 0, (void (*)())mod_uninstall_daemon,
	    NULL, 0, &p0, TS_RUN, minclsyspri);

	(void) thread_create(NULL, 0, seg_pasync_thread,
	    NULL, 0, &p0, TS_RUN, minclsyspri);

	pid_setmin();

	/* system is now ready */
	mutex_exit(&ualock);

	bcopy("sched", PTOU(curproc)->u_psargs, 6);
	bcopy("sched", PTOU(curproc)->u_comm, 5);
	sched();
	/* NOTREACHED */
}

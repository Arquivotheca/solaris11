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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This module contains the guts of checkpoint-resume mechanism.
 * All code in this module is platform independent.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/callb.h>
#include <sys/processor.h>
#include <sys/machsystm.h>
#include <sys/clock.h>
#include <sys/vfs.h>
#include <sys/kmem.h>
#include <nfs/lm.h>
#include <sys/systm.h>
#include <sys/cpr.h>
#include <sys/bootconf.h>
#include <sys/cyclic.h>
#include <sys/filio.h>
#include <sys/fs/ufs_filio.h>
#include <sys/epm.h>
#include <sys/modctl.h>
#include <sys/reboot.h>
#include <sys/kdi.h>
#include <sys/promif.h>
#include <sys/srn.h>
#include <sys/cpr_impl.h>

#define	PPM(dip) ((dev_info_t *)DEVI(dip)->devi_pm_ppm)

extern struct cpr_terminator cpr_term;

extern int cpr_verify_statefile(void);
extern void cpr_start_kernel_threads(void);
extern void cpr_abbreviate_devpath(char *, char *);
extern void cpr_convert_promtime(cpr_time_t *);
extern void cpr_set_bitmap_size(void);
extern void cpr_stat_init();
extern void cpr_statef_close(void);
extern int (*srn_signal)(int, int);
extern void init_cpu_syscall(struct cpu *);
extern void i_cpr_pre_resume_cpus();
extern void i_cpr_post_resume_cpus();

extern int pm_powering_down;
extern kmutex_t srn_clone_lock;
extern int srn_inuse;

static int cpr_suspend(void);
static int cpr_resume(void);
static void cpr_suspend_init(void);
#if defined(__x86)
static int cpr_suspend_cpus(void);
static void cpr_resume_cpus(void);
#endif
static int cpr_all_online(void);
static void cpr_restore_offline(void);

cpr_time_t wholecycle_tv;
int cpr_suspend_succeeded;
pfn_t curthreadpfn;
int curthreadremapped;

extern cpuset_t cpu_ready_set;
extern void *(*cpu_pause_func)(void *);

extern processorid_t i_cpr_bootcpuid(void);
extern cpu_t *i_cpr_bootcpu(void);
extern void tsc_adjust_delta(hrtime_t tdelta);
extern void tsc_resume(void);
extern int tsc_resume_in_cyclic;

/*
 * save or restore abort_enable;  this prevents a drop
 * to kadb or prom during cpr_resume_devices() when
 * there is no kbd present;  see abort_sequence_enter()
 */
static void
cpr_sae(int stash)
{
	static int saved_ae = -1;

	if (stash) {
		saved_ae = abort_enable;
		abort_enable = 0;
	} else if (saved_ae != -1) {
		abort_enable = saved_ae;
		saved_ae = -1;
	}
}


/*
 * The main switching point for cpr, this routine starts the ckpt
 * and state file saving routines; on resume the control is
 * returned back to here and it then calls the resume routine.
 */
int
cpr_main(void)
{
	int rc, rc2;
	label_t saveq;
	klwp_t *tlwp = ttolwp(curthread);

	/*
	 * If saving a statefile, we need to setup and configure
	 * the default file.
	 */
	if (pm_cpr_save_state != 0) {
		if ((rc = cpr_default_setup(1)) != 0)
			return (rc);
		ASSERT(tlwp);
		saveq = tlwp->lwp_qsav;
	}

	rc = cpr_suspend();
	PMD(PMD_SX, ("cpr_suspend rets %x\n", rc))
	if (rc == 0) {
		/*
		 * From this point on, we should be at a high
		 * spl, interrupts disabled, and all but one
		 * cpu's paused (effectively UP/single threaded).
		 * So this is where we want to put ASSERTS()
		 * to let us know otherwise.
		 */
		ASSERT(cpr_cpus_parked());

		/*
		 * Now do the work of actually putting this
		 * machine to sleep!
		 * Note that return vectors need to be part of the
		 * i_cpr_power_down() code, as different platforms
		 * might have a different view on how to return.  So
		 * the success/failure of suspend and resume from this
		 * point is the ability of the platform to appropriately
		 * save the state *and* "power off" the machine.
		 */
		rc = i_cpr_power_down();
		if (rc == 0) {
			PMD(PMD_SX, ("back from successful suspend\n"))
			tlwp->lwp_qsav = saveq;
		} else {
			PMD(PMD_SX, ("i_cpr_power_down failed\n"))
		}
		CPR->c_flags &= ~C_SUSPENDING;
		CPR->c_flags |= C_RESUMING;
		/*
		 * We do care about the return value from cpr_resume
		 * at this point, as it will tell us if one of the
		 * resume functions failed (cpr_resume_devices())
		 * However, for this to return and _not_ panic, means
		 * that we must be in one of the test functions.  So
		 * check for that and return an appropriate message.
		 */
		rc2 = cpr_resume();
		if (rc2 != 0) {
			/*
			 * This is technically a bad assert, as it
			 * is possible to get a resume error and
			 * _not_ have the test point set.  However,
			 * this kind of failure is bad, and we would
			 * like to be made aware of it on debug
			 * kernels before we can go further and
			 * potentially corrupt debugable data.
			 */
			ASSERT(cpr_test_point > 0);
			cmn_err(CE_NOTE,
			    "cpr_resume returned non-zero: %d\n", rc2);
			PMD(PMD_SX, ("cpr_resume rets %x\n", rc2))
		}
		if (cpr_reusable_mode) {
			/*
			 * For "Reusable" mode, we haven't really
			 * powered off, so we want at least some
			 * message indicating this mode.
			 */
			PMD(PMD_SX, ("back from reusable mode\n"))
		}
		ASSERT(!cpus_paused());
	} else {
		PMD(PMD_SX, ("failed cpr_suspend, resuming\n"))
		CPR->c_flags &= ~C_SUSPENDING;
		CPR->c_flags |= C_RESUMING;
		rc = cpr_resume();
	}

	/* Cleanup the "default" information. */
	(void) cpr_default_setup(0);

	/* Return the appropriate error code. */
	return (rc);
}


/*
 * Check if klmmod is loaded and call a lock manager service; if klmmod
 * is not loaded, the services aren't needed and a call would trigger a
 * modload, which would block since another thread would never run.
 */
static void
cpr_lock_mgr(void (*service)(void))
{
	if (mod_find_by_filename(NULL, "misc/klmmod") != NULL)
		(*service)();
}

int
cpr_suspend_cpus(void)
{
	int	ret = 0;
	extern void *i_cpr_save_context(void *arg);

	mutex_enter(&cpu_lock);

	/*
	 * the machine could not have booted without a bootcpu
	 */
	ASSERT(i_cpr_bootcpu() != NULL);

	/*
	 * bring all the offline cpus online
	 */
	if ((ret = cpr_all_online())) {
		mutex_exit(&cpu_lock);
		return (ret);
	}

	/*
	 * Set the affinity to be the boot processor
	 * This is cleared in either cpr_resume_cpus() or cpr_unpause_cpus()
	 */
	affinity_set(i_cpr_bootcpuid());

	ASSERT(CPU->cpu_id == 0);

	PMD(PMD_SX, ("curthread running on bootcpu\n"))

	/*
	 * pause all other running CPUs and save the CPU state at the sametime
	 */
	cpu_pause_func = i_cpr_save_context;
	pause_cpus(NULL);

	PMD(PMD_SX, ("aux cpus stopped\n"))

	mutex_exit(&cpu_lock);

	return (0);
}

/*
 * Take the system down to a checkpointable state and write
 * the state file, the following are sequentially executed:
 *
 *    - Request all user threads to stop themselves
 *    - push out and invalidate user pages
 *    - bring statefile inode incore to prevent a miss later
 *    - request all daemons to stop
 *    - check and make sure all threads are stopped
 *    - sync the file system
 *    - suspend all devices
 *    - block intrpts
 *    - dump system state and memory to state file
 *    - SPARC code will not be called with CPR_TORAM, caller filters
 */
static int
cpr_suspend(void)
{
	int	rc = 0;
	int	skt_rc = 0;

	PMD(PMD_SX, ("cpr_suspend: method: S%x, save state: %s\n",
	    pm_cpr_state, (pm_cpr_save_state ? "yes" : "no")))
	cpr_set_substate(C_ST_SUSPEND_BEGIN);

	/*
	 * Init local variables and structures.
	 */
	cpr_suspend_init();

	/* Get the timestamp for when we start suspend */
	cpr_tod_get(&wholecycle_tv);
	CPR_STAT_EVENT_START("Suspend Total");

	i_cpr_alloc_cpus();

	if (!cpr_reusable_mode) {
		/*
		 * We need to validate default file before fs
		 * functionality is disabled.
		 */
		if (rc = cpr_validate_definfo(0))
			return (rc);
	}
	i_cpr_save_machdep_info();

	PMD(PMD_SX, ("cpr_suspend: stop scans\n"))
	/* Stop PM scans ASAP */
	(void) callb_execute_class(CB_CL_CPR_PM, CB_CODE_CPR_CHKPT);

	pm_dispatch_to_dep_thread(PM_DEP_WK_CPR_SUSPEND,
	    NULL, NULL, PM_DEP_WAIT, NULL, 0);

#if defined(__sparc)
	/*
	 * Due to the different methods of resuming the system between
	 * Sparc (boot cprboot, which reloads kernel image) and x64
	 * (restart via reset or longjump into existing kernel image)
	 * cpus are not paused and unpaused in the SPARC case, since it
	 * is necessary to restart the cpus and pause them before
	 * restoring the OBP image.
	 */

	cpr_set_substate(C_ST_MP_OFFLINE);
	if (rc = cpr_mp_offline())
		return (rc);
#endif
	/*
	 * Ask srn clients to suspend.
	 * srn will wait for clients to respond, and return a failure
	 * if they don't.  On suspend, we terminate the suspend as
	 * this is not good.  On resume, we log the failure and
	 * continue, since we are mostly back by then (the clients
	 * can be managed by the user at this point).
	 */
	cpr_set_substate(C_ST_SRN_SIGNAL);
	mutex_enter(&srn_clone_lock);
	if (srn_signal) {
		PMD(PMD_SX, ("cpr_suspend: (*srn_signal)(..., "
		    "SRN_SUSPEND_REQ)\n"))
		srn_inuse = 1;	/* because *(srn_signal) cv_waits */
		rc = (*srn_signal)(SRN_TYPE_APM, SRN_SUSPEND_REQ);
		srn_inuse = 0;
		if (rc) {
			PMD(PMD_SX, ("cpr_suspend: (*srn_signal)(..., "
			    "SRN_SUSPEND_REQ) failed\n"))
			return (DDI_FAILURE);
		}
	} else {
		PMD(PMD_SX, ("cpr_suspend: no srn_signal device.\n"))
	}
	mutex_exit(&srn_clone_lock);

	/*
	 * Ask the user threads to stop by themselves, but
	 * if they don't or can't after 3 retries, we give up on CPR.
	 * The 3 retry is not a random number because 2 is possible if
	 * a thread has been forked before the parent thread is stopped.
	 */
	CPR_DEBUG(CPR_DEBUG1, "\nstopping user threads...");
	CPR_STAT_EVENT_START("  stop users");
	cpr_set_substate(C_ST_STOP_USER_THREADS);
	PMD(PMD_SX, ("cpr_suspend: stop user threads\n"))
	if (rc = cpr_stop_user_threads())
		return (rc);
	CPR_STAT_EVENT_END("  stop users");
	CPR_DEBUG(CPR_DEBUG1, "done\n");

	PMD(PMD_SX, ("cpr_suspend: save direct levels\n"))
	pm_save_direct_levels();

	/*
	 * User threads are stopped.  We will start communicating with the
	 * user via prom_printf (some debug output may have already happened)
	 * so let anybody who cares know about this (bug 4096122)
	 */
	(void) callb_execute_class(CB_CL_CPR_PROMPRINTF, CB_CODE_CPR_CHKPT);

	PMD(PMD_SX, ("cpr_suspend: send notice\n"))
#ifndef DEBUG
	/*
	 * Could use a better mechanism for sending a banner.  Possibly
	 * something from the userland service that could determine the
	 * actual usage of the machine.  But on DEBUG machines, this
	 * message is *really* annoying.
	 */
	cpr_send_notice();
	if (cpr_debug)
		prom_printf("\n");
#endif

	PMD(PMD_SX, ("cpr_suspend: POST USER callback\n"))
	(void) callb_execute_class(CB_CL_CPR_POST_USER, CB_CODE_CPR_CHKPT);

	/*
	 * Reattach any drivers which originally exported the
	 * no-involuntary-power-cycles property.  We need to do this before
	 * stopping kernel threads because modload is implemented using
	 * a kernel thread.
	 */
	cpr_set_substate(C_ST_PM_REATTACH_NOINVOL);
	PMD(PMD_SX, ("cpr_suspend: reattach noinvol\n"))
	if (!pm_reattach_noinvol())
		return (ENXIO);

	if (pm_cpr_save_state != 0) {
		/*
		 * Use sync_all to swap out all user pages and find out
		 * how much extra space needed for user pages that don't
		 * have back store space left.
		 */
		PMD(PMD_SX, ("cpr_suspend: swapout upages\n"))
		CPR_STAT_EVENT_START("  swapout upages");
		vfs_sync(SYNC_ALL);
		CPR_STAT_EVENT_END("  swapout upages");

		/*
		 * Sync the filesystem to preserve its integrity.
		 *
		 * This sync is also used to flush out all B_DELWRI buffers
		 * (fs cache) which are mapped and neither dirty nor
		 * referenced before cpr_invalidate_pages destroys them.
		 * fsflush does similar thing.
		 */
		sync();

		/*
		 * destroy all clean file mapped kernel pages
		 */
		CPR_STAT_EVENT_START("  clean pages");
		CPR_DEBUG(CPR_DEBUG1, ("cleaning up mapped pages..."));
		(void) callb_execute_class(CB_CL_CPR_VM, CB_CODE_CPR_CHKPT);
		CPR_DEBUG(CPR_DEBUG1, ("done\n"));
		CPR_STAT_EVENT_END("  clean pages");
	}


	/*
	 * Hooks needed by lock manager prior to suspending.
	 * Refer to code for more comments.
	 */
	PMD(PMD_SX, ("cpr_suspend: lock mgr\n"))
	cpr_lock_mgr(lm_cprsuspend);

	/*
	 * Now suspend all the devices
	 */
	CPR_STAT_EVENT_START("  stop drivers");
	CPR_DEBUG(CPR_DEBUG1, "suspending drivers...");
	cpr_set_substate(C_ST_SUSPEND_DEVICES);
	pm_powering_down = 1;
	PMD(PMD_SX, ("cpr_suspend: suspending devices\n"))
	rc = cpr_suspend_devices(ddi_root_node());
	pm_powering_down = 0;
	if (rc)
		return (rc);
	CPR_DEBUG(CPR_DEBUG1, "done\n");
	CPR_STAT_EVENT_END("  stop drivers");

	/*
	 * Stop all daemon activities
	 */
	cpr_set_substate(C_ST_STOP_KERNEL_THREADS);
	PMD(PMD_SX, ("cpr_suspend: stopping kernel threads\n"))
	if (skt_rc = cpr_stop_kernel_threads())
		return (skt_rc);

	PMD(PMD_SX, ("cpr_suspend: POST KERNEL callback\n"))
	(void) callb_execute_class(CB_CL_CPR_POST_KERNEL, CB_CODE_CPR_CHKPT);

	PMD(PMD_SX, ("cpr_suspend: reattach noinvol fini\n"))
	pm_reattach_noinvol_fini();

	cpr_sae(1);

	PMD(PMD_SX, ("cpr_suspend: CPR CALLOUT callback\n"))
	(void) callb_execute_class(CB_CL_CPR_CALLOUT, CB_CODE_CPR_CHKPT);

	/*
	 * it's time to ignore the outside world, stop the real time
	 * clock and disable any further intrpt activity.
	 */
	PMD(PMD_SX, ("cpr_suspend: handle xc\n"))
	i_cpr_handle_xc(1);	/* turn it on to disable xc assertion */

	mutex_enter(&cpu_lock);
	PMD(PMD_SX, ("cpr_suspend: cyclic suspend\n"))
	cyclic_suspend();
	mutex_exit(&cpu_lock);

#if !defined(__sparc)
	/* pause aux cpus */
	PMD(PMD_SX, ("pause aux cpus\n"))
	cpr_set_substate(C_ST_MP_PAUSED);
	if ((rc = cpr_suspend_cpus()) != 0)
		return (rc);
#endif

	PMD(PMD_SX, ("cpr_suspend: stop intr\n"))
	i_cpr_stop_intr();
	CPR_DEBUG(CPR_DEBUG1, "interrupt is stopped\n");

	/*
	 * Since we will now disable the mechanism that causes prom_printfs
	 * to power up (if needed) the console fb/monitor, we assert that
	 * it must be up now.
	 */
	ASSERT(pm_cfb_is_up());
	PMD(PMD_SX, ("cpr_suspend: prom suspend prepost\n"))
	prom_suspend_prepost();

#if defined(__sparc)
	/*
	 * getting ready to write ourself out, flush the register
	 * windows to make sure that our stack is good when we
	 * come back on the resume side.
	 */
	flush_windows();
#endif

	/*
	 * Set the substate based on pm_cpr_save_state, and return.
	 */
	if (pm_cpr_save_state == 0) {
		cpr_set_substate(C_ST_NODUMP);
	} else {
		cpr_set_substate(C_ST_DUMP);
	}
	PMD(PMD_SX, ("cpr_suspend rets %x\n", rc))
	return (rc);
}

void
cpr_resume_cpus(void)
{
	/*
	 * this is a cut down version of start_other_cpus()
	 * just do the initialization to wake the other cpus
	 */

#if defined(__x86)
	/*
	 * Initialize our syscall handlers
	 */
	init_cpu_syscall(CPU);

#endif

	i_cpr_pre_resume_cpus();

	/*
	 * Restart the paused cpus
	 */
	mutex_enter(&cpu_lock);
	start_cpus();
	mutex_exit(&cpu_lock);

	i_cpr_post_resume_cpus();

	mutex_enter(&cpu_lock);
	/*
	 * Restore this cpu to use the regular cpu_pause(), so that
	 * online and offline will work correctly
	 */
	cpu_pause_func = NULL;

	/*
	 * clear the affinity set in cpr_suspend_cpus()
	 */
	affinity_clear();

	/*
	 * offline all the cpus that were brought online during suspend
	 */
	cpr_restore_offline();

	mutex_exit(&cpu_lock);
}

void
cpr_unpause_cpus(void)
{
	/*
	 * Now restore the system back to what it was before we suspended
	 */

	PMD(PMD_SX, ("cpr_unpause_cpus: restoring system\n"))

	mutex_enter(&cpu_lock);

	/*
	 * Restore this cpu to use the regular cpu_pause(), so that
	 * online and offline will work correctly
	 */
	cpu_pause_func = NULL;

	/*
	 * Restart the paused cpus
	 */
	start_cpus();

	/*
	 * clear the affinity set in cpr_suspend_cpus()
	 */
	affinity_clear();

	/*
	 * offline all the cpus that were brought online during suspend
	 */
	cpr_restore_offline();

	mutex_exit(&cpu_lock);
}

/*
 * Bring the system back up from a checkpoint, at this point
 * the VM has been minimally restored by boot, the following
 * are executed sequentially:
 *
 *    - machdep setup and enable interrupts (mp startup if it's mp)
 *    - resume all devices
 *    - restart daemons
 *    - put all threads back on run queue
 */
static int
cpr_resume(void)
{
	cpr_time_t pwron_tv, *ctp;
	char *str;
	int rc = 0;

	/*
	 * The following switch is used to resume the system
	 * that was suspended to a different level.
	 */
	CPR_DEBUG(CPR_DEBUG1, "\nEntering cpr_resume...\n");
	PMD(PMD_SX, ("cpr_resume %x\n", pm_cpr_state))

	/*
	 * Note:
	 *
	 * The rollback labels rb_xyz do not represent the cpr resume
	 * state when event 'xyz' has happened. Instead they represent
	 * the state during cpr suspend when event 'xyz' was being
	 * entered (and where cpr suspend failed). The actual call that
	 * failed may also need to be partially rolled back, since they
	 * aren't atomic in most cases.  In other words, rb_xyz means
	 * "roll back all cpr suspend events that happened before 'xyz',
	 * and the one that caused the failure, if necessary."
	 */
	switch (CPR->c_substate) {
	case C_ST_DUMP:
		/*
		 * This is most likely a full-fledged cpr_resume after
		 * a complete and successful cpr suspend. Just roll back
		 * everything.
		 */
		ASSERT(pm_cpr_save_state != 0);
		break;

	case C_ST_REUSABLE:
	case C_ST_DUMP_NOSPC:
	case C_ST_SETPROPS_0:
	case C_ST_SETPROPS_1:
		/*
		 * C_ST_REUSABLE and C_ST_DUMP_NOSPC are the only two
		 * special switch cases here. The other two do not have
		 * any state change during cpr_suspend() that needs to
		 * be rolled back. But these are exit points from
		 * cpr_suspend, so theoretically (or in the future), it
		 * is possible that a need for roll back of a state
		 * change arises between these exit points.
		 */
		ASSERT(pm_cpr_save_state != 0);
		goto rb_dump;

	case C_ST_NODUMP:
		/*
		 * In this case, we didn't dump the system state,
		 * either due to error, or RAM state was not lost
		 * (i.e. Suspend to RAM).
		 */
		PMD(PMD_SX, ("cpr_resume: NODUMP\n"))
		goto rb_nodump;

	case C_ST_STOP_KERNEL_THREADS:
		PMD(PMD_SX, ("cpr_resume: STOP_KERNEL_THREADS\n"))
		goto rb_stop_kernel_threads;

	case C_ST_SUSPEND_DEVICES:
		PMD(PMD_SX, ("cpr_resume: SUSPEND_DEVICES\n"))
		goto rb_suspend_devices;

	case C_ST_PM_REATTACH_NOINVOL:
		PMD(PMD_SX, ("cpr_resume: REATTACH_NOINVOL\n"))
		goto rb_pm_reattach_noinvol;

	case C_ST_STOP_USER_THREADS:
		PMD(PMD_SX, ("cpr_resume: STOP_USER_THREADS\n"))
		goto rb_stop_user_threads;

	case C_ST_SRN_SIGNAL:
		PMD(PMD_SX, ("cpr_resume: SRN_SIGNAL\n"))
		goto rb_srn_signal;

	case C_ST_MP_OFFLINE:
		PMD(PMD_SX, ("cpr_resume: MP_OFFLINE\n"))
		goto rb_mp_offline;

	case C_ST_MP_PAUSED:
		PMD(PMD_SX, ("cpr_resume: MP_PAUSED\n"))
		goto rb_mp_paused;
	default:
		PMD(PMD_SX, ("cpr_resume: others\n"))
		goto rb_others;
	}

rb_all:
	/*
	 * perform platform-dependent initialization
	 */
	if (cpr_suspend_succeeded)
		i_cpr_machdep_setup();

	/*
	 * system did not really go down if we jump here
	 */
rb_dump:
	/*
	 * IMPORTANT:  SENSITIVE RESUME SEQUENCE
	 *
	 * DO NOT ADD ANY INITIALIZATION STEP BEFORE THIS POINT!!
	 */
rb_nodump:
	/*
	 * If we did suspend to RAM, we didn't generate a dump
	 */
	PMD(PMD_SX, ("cpr_resume: CPR DMA callback\n"))
	(void) callb_execute_class(CB_CL_CPR_DMA, CB_CODE_CPR_RESUME);
	if (cpr_suspend_succeeded) {
		PMD(PMD_SX, ("cpr_resume: CPR RPC callback\n"))
		(void) callb_execute_class(CB_CL_CPR_RPC, CB_CODE_CPR_RESUME);
	}

	prom_resume_prepost();

#if defined(__sparc)
	if (cpr_suspend_succeeded && (boothowto & RB_DEBUG))
		kdi_dvec_cpr_restart();
#endif


rb_mp_paused:
#if !defined(__sparc)
	/* See note in cpr_suspend about Sparc offlining cpu's */
	PT(PT_RMPO);

	if (cpr_suspend_succeeded) {
		PMD(PMD_SX, ("resume aux cpus\n"))
		cpr_resume_cpus();
	} else {
		PMD(PMD_SX, ("unpause aux cpus\n"))
		cpr_unpause_cpus();
	}
#endif

	/*
	 * let the tmp callout catch up.
	 */
	PMD(PMD_SX, ("cpr_resume: CPR CALLOUT callback\n"))
	(void) callb_execute_class(CB_CL_CPR_CALLOUT, CB_CODE_CPR_RESUME);

	i_cpr_enable_intr();

	mutex_enter(&cpu_lock);
	PMD(PMD_SX, ("cpr_resume: cyclic resume\n"))
	cyclic_resume();
	mutex_exit(&cpu_lock);

	PMD(PMD_SX, ("cpr_resume: handle xc\n"))
	i_cpr_handle_xc(0);	/* turn it off to allow xc assertion */

	PMD(PMD_SX, ("cpr_resume: CPR POST KERNEL callback\n"))
	(void) callb_execute_class(CB_CL_CPR_POST_KERNEL, CB_CODE_CPR_RESUME);

	/*
	 * statistics gathering
	 */
	if (cpr_suspend_succeeded) {
		/*
		 * Prevent false alarm in tod_validate() due to tod
		 * value change between suspend and resume
		 */
		cpr_tod_status_set(TOD_CPR_RESUME_DONE);

		cpr_convert_promtime(&pwron_tv);

		ctp = &cpr_term.tm_shutdown;
		if (pm_cpr_save_state != 0)
			CPR_STAT_EVENT_END_TMZ("  write statefile", ctp);
		CPR_STAT_EVENT_END_TMZ("Suspend Total", ctp);

		CPR_STAT_EVENT_START_TMZ("Resume Total", &pwron_tv);

		str = "  prom time";
		CPR_STAT_EVENT_START_TMZ(str, &pwron_tv);
		ctp = &cpr_term.tm_cprboot_start;
		CPR_STAT_EVENT_END_TMZ(str, ctp);

		str = "  read statefile";
		CPR_STAT_EVENT_START_TMZ(str, ctp);
		ctp = &cpr_term.tm_cprboot_end;
		CPR_STAT_EVENT_END_TMZ(str, ctp);
	}

rb_stop_kernel_threads:
	/*
	 * Put all threads back to where they belong; get the kernel
	 * daemons straightened up too. Note that the callback table
	 * locked during cpr_stop_kernel_threads() is released only
	 * in cpr_start_kernel_threads(). Ensure modunloading is
	 * disabled before starting kernel threads, we don't want
	 * modunload thread to start changing device tree underneath.
	 */
	PMD(PMD_SX, ("cpr_resume: modunload disable\n"))
	modunload_disable();
	PMD(PMD_SX, ("cpr_resume: start kernel threads\n"))
	cpr_start_kernel_threads();

rb_suspend_devices:
	CPR_DEBUG(CPR_DEBUG1, "resuming devices...");
	CPR_STAT_EVENT_START("  start drivers");

	/*
	 * The policy here is to continue resume everything we can if we did
	 * not successfully finish suspend; and panic if we are coming back
	 * from a fully suspended system.
	 */
	PMD(PMD_SX, ("cpr_resume: resume devices\n"))
	rc = cpr_resume_devices(ddi_root_node(), 0);

	cpr_sae(0);

	str = "Failed to resume one or more devices.";

	if (rc) {
		/*
		 * If we have actually suspended, this is a serious problem.
		 * But if we were resuming from a failed suspend, this is
		 * mostly a warnable condition.
		 */
		if (CPR->c_substate == C_ST_DUMP ||
		    (pm_cpr_state == SYSTEM_POWER_S4 &&
		    CPR->c_substate == C_ST_NODUMP)) {
			PMD(PMD_SX, ("cpr_resume: resume device "
			    "panic\n"))
			cpr_err(CE_PANIC, str);
		} else {
			PMD(PMD_SX, ("cpr_resume: resume device warn\n"))
			cpr_err(CE_WARN, str);
		}
	}

	CPR_STAT_EVENT_END("  start drivers");
	CPR_DEBUG(CPR_DEBUG1, "done\n");

	/*
	 * If we had disabled modunloading in this cpr resume cycle (i.e. we
	 * resumed from a state earlier than C_ST_SUSPEND_DEVICES), re-enable
	 * modunloading now.
	 */
	if (CPR->c_substate != C_ST_SUSPEND_DEVICES) {
		PMD(PMD_SX, ("cpr_resume: modload enable\n"))
		modunload_enable();
	}

	/*
	 * Hooks needed by lock manager prior to resuming.
	 * Refer to code for more comments.
	 */
	PMD(PMD_SX, ("cpr_resume: lock mgr\n"))
	cpr_lock_mgr(lm_cprresume);

rb_statef_alloc:
	if (pm_cpr_save_state != 0 && cpr_suspend_succeeded) {
		cpr_statef_close();

	}
rb_pm_reattach_noinvol:
	/*
	 * When pm_reattach_noinvol() succeeds, modunload_thread will
	 * remain disabled until after cpr suspend passes the
	 * C_ST_STOP_KERNEL_THREADS state. If any failure happens before
	 * cpr suspend reaches this state, we'll need to enable modunload
	 * thread during rollback.
	 */
	if (CPR->c_substate == C_ST_SUSPEND_DEVICES ||
	    CPR->c_substate == C_ST_STOP_KERNEL_THREADS) {
		PMD(PMD_SX, ("cpr_resume: reattach noinvol fini\n"))
		pm_reattach_noinvol_fini();
	}

	PMD(PMD_SX, ("cpr_resume: CPR POST USER callback\n"))
	(void) callb_execute_class(CB_CL_CPR_POST_USER, CB_CODE_CPR_RESUME);
	PMD(PMD_SX, ("cpr_resume: CPR PROMPRINTF callback\n"))
	(void) callb_execute_class(CB_CL_CPR_PROMPRINTF, CB_CODE_CPR_RESUME);

	PMD(PMD_SX, ("cpr_resume: restore direct levels\n"))
	pm_restore_direct_levels();

rb_stop_user_threads:
	CPR_DEBUG(CPR_DEBUG1, "starting user threads...");
	PMD(PMD_SX, ("cpr_resume: starting user threads\n"))
	cpr_start_user_threads();
	CPR_DEBUG(CPR_DEBUG1, "done\n");
	/*
	 * Ask Xorg to resume the frame buffer, and wait for it to happen
	 */
	mutex_enter(&srn_clone_lock);
rb_srn_signal:
	if (srn_signal) {
		PMD(PMD_SX, ("cpr_suspend: (*srn_signal)(..., "
		    "SRN_NORMAL_RESUME)\n"))
		srn_inuse = 1;		/* because (*srn_signal) cv_waits */
		rc = (*srn_signal)(SRN_TYPE_APM, SRN_NORMAL_RESUME);
		srn_inuse = 0;
		if (rc) {
			/*
			 * In resume, srn_signal() is a soft failure,
			 * since it is less likely to impact the rest
			 * of the resume.  However, since something
			 * failed, it is important to note this error.
			 */
			CPR_DEBUG(CPR_DEBUG2, "One or more srn clients "
			    "failed to resume...");
			PMD(PMD_SX, ("cpr_suspend: (*srn_signal)(..., "
			    "SRN_NORMAL_RESUME) returned %d\n", rc))
			cpr_err(CE_WARN, "One or more srn clients "
			    "failed to resume.");
		}
	} else {
		PMD(PMD_SX, ("cpr_suspend: srn_signal NULL\n"))
	}
	mutex_exit(&srn_clone_lock);

rb_mp_offline:
#if defined(__sparc)
	if (cpr_mp_online())
		cpr_err(CE_WARN, "Failed to online all the processors.");
#endif

rb_others:
	PMD(PMD_SX, ("cpr_resume: dep thread\n"))
	pm_dispatch_to_dep_thread(PM_DEP_WK_CPR_RESUME, NULL, NULL,
	    PM_DEP_WAIT, NULL, 0);

	PMD(PMD_SX, ("cpr_resume: CPR PM callback\n"))
	(void) callb_execute_class(CB_CL_CPR_PM, CB_CODE_CPR_RESUME);

	if (cpr_suspend_succeeded) {
		cpr_stat_record_events();
	}

#if defined(__sparc)
	/*
	 * definfo is currently only used on Sparc, and there isn't
	 * any thoughts of it having value on x86.  If this assumption
	 * changes, the #if above should be revisited.
	 */
	if (pm_cpr_state == SYSTEM_POWER_S4 && !cpr_reusable_mode)
		cpr_clear_definfo();
#endif

	i_cpr_free_cpus();
	CPR_DEBUG(CPR_DEBUG1, "Sending SIGTHAW...");
	PMD(PMD_SX, ("cpr_resume: SIGTHAW\n"))
	cpr_signal_user(SIGTHAW);
	CPR_DEBUG(CPR_DEBUG1, "done\n");

	CPR_STAT_EVENT_END("Resume Total");

	CPR_STAT_EVENT_START_TMZ("WHOLE CYCLE", &wholecycle_tv);
	CPR_STAT_EVENT_END("WHOLE CYCLE");

	if (cpr_debug & CPR_DEBUG1)
		cmn_err(CE_CONT, "\nThe system is back where you left!\n");

	CPR_STAT_EVENT_START("POST CPR DELAY");

#ifdef CPR_STAT
	ctp = &cpr_term.tm_shutdown;
	CPR_STAT_EVENT_START_TMZ("PWROFF TIME", ctp);
	CPR_STAT_EVENT_END_TMZ("PWROFF TIME", &pwron_tv);

	CPR_STAT_EVENT_PRINT();
#endif /* CPR_STAT */

	PMD(PMD_SX, ("cpr_resume returns %x\n", rc))
	return (rc);
}

static void
cpr_suspend_init(void)
{
	cpr_time_t *ctp;

	cpr_stat_init();

	/*
	 * If cpr_suspend() failed before cpr_dump() gets a chance
	 * to reinitialize the terminator of the statefile,
	 * the values of the old terminator will still linger around.
	 * Since the terminator contains information that we need to
	 * decide whether suspend succeeded or not, we need to
	 * reinitialize it as early as possible.
	 */
	cpr_term.real_statef_size = 0;
	ctp = &cpr_term.tm_shutdown;
	bzero(ctp, sizeof (*ctp));
	ctp = &cpr_term.tm_cprboot_start;
	bzero(ctp, sizeof (*ctp));
	ctp = &cpr_term.tm_cprboot_end;
	bzero(ctp, sizeof (*ctp));

	/*
	 * If we are expecting to save our state, we need to do more to
	 * validate our thread's condition.
	 */
	if (pm_cpr_save_state != 0) {
		/*
		 * Lookup the physical address of our thread structure.
		 * This should never be invalid and the entire thread structure
		 * is expected to reside within the same pfn.
		 */
		curthreadpfn = hat_getpfnum(kas.a_hat, (caddr_t)curthread);
		ASSERT(curthreadpfn != PFN_INVALID);
		ASSERT(curthreadpfn == hat_getpfnum(kas.a_hat,
		    (caddr_t)curthread + sizeof (kthread_t) - 1));
	}

	cpr_suspend_succeeded = 0;
}

/*
 * bring all the offline cpus online
 */
static int
cpr_all_online(void)
{
	int	rc = 0;

#ifdef	__sparc
	/*
	 * do nothing
	 */
#else

	cpu_t	*cp;

	ASSERT(MUTEX_HELD(&cpu_lock));

	cp = cpu_list;
	do {
		cp->cpu_cpr_flags &= ~CPU_CPR_ONLINE;
		if (!CPU_ACTIVE(cp)) {
			if ((rc = cpu_online(cp)) != 0)
				break;
			CPU_SET_CPR_FLAGS(cp, CPU_CPR_ONLINE);
		}
	} while ((cp = cp->cpu_next) != cpu_list);

	if (rc) {
		/*
		 * an online operation failed so offline the cpus
		 * that were onlined above to restore the system
		 * to its original state
		 */
		cpr_restore_offline();
	}
#endif
	return (rc);
}

/*
 * offline all the cpus that were brought online by cpr_all_online()
 */
static void
cpr_restore_offline(void)
{

#ifdef	__sparc
	/*
	 * do nothing
	 */
#else

	cpu_t	*cp;
	int	rc = 0;

	ASSERT(MUTEX_HELD(&cpu_lock));

	cp = cpu_list;
	do {
		if (CPU_CPR_IS_ONLINE(cp)) {
			rc =  cpu_offline(cp, 0);
			/*
			 * this offline should work, since the cpu was
			 * offline originally and was successfully onlined
			 * by cpr_all_online()
			 */
			ASSERT(rc == 0);
			cp->cpu_cpr_flags &= ~CPU_CPR_ONLINE;
		}
	} while ((cp = cp->cpu_next) != cpu_list);

#endif

}

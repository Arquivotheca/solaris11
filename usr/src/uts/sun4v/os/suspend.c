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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/mutex.h>
#include <sys/cpuvar.h>
#include <sys/cyclic.h>
#include <sys/disp.h>
#include <sys/ddi.h>
#include <sys/wdt.h>
#include <sys/callb.h>
#include <sys/cmn_err.h>
#include <sys/hypervisor_api.h>
#include <sys/membar.h>
#include <sys/x_call.h>
#include <sys/promif.h>
#include <sys/systm.h>
#include <sys/mach_descrip.h>
#include <sys/cpu_module.h>
#include <sys/pg.h>
#include <sys/lgrp.h>
#include <sys/sysmacros.h>
#include <sys/sunddi.h>
#include <sys/cpupart.h>
#include <sys/hsvc.h>
#include <sys/mpo.h>
#include <sys/types.h>
#include <vm/hat_sfmmu.h>
#include <sys/time.h>
#include <sys/clock.h>
#include <sys/cmt.h>
#include <sys/mdesc.h>
#include <sys/mdesc_impl.h>
#include <sys/machsystm.h>

/*
 * Sun4v OS Suspend
 *
 * Provides a means to suspend a sun4v guest domain by pausing CPUs and then
 * calling into the HV to initiate a suspension. Suspension is sequenced
 * externally by calling suspend_pre, suspend_start, and suspend_post.
 * suspend_pre and suspend_post are meant to perform any special operations
 * that should be done before or after a suspend/resume operation. e.g.,
 * callbacks to cluster software to disable heartbeat monitoring before the
 * system is suspended. suspend_start prepares kernel services to be suspended
 * and then suspends the domain by calling hv_guest_suspend.
 *
 * Special Handling for %tick and %stick Registers
 *
 * After a suspend/resume operation, the %tick and %stick registers may have
 * jumped forwards or backwards. The delta is assumed to be consistent across
 * all CPUs, within the negligible level of %tick and %stick variation
 * acceptable on a cold boot. In order to maintain increasing %tick and %stick
 * counter values without exposing large positive or negative jumps to kernel
 * or user code, a %tick and %stick offset is used. Kernel reads of these
 * counters return the sum of the hardware register counter and offset
 * variable.
 *
 * Additionally, after a suspend/resume operation, the %tick and %stick
 * frequency may have changed (for example, when the resume occurs on a
 * machine with different %stick/%tick frequencies after a migration.)
 * The change in %stick frequency is assumed to be consistent across all
 * CPUs. In order to maintain a monotonically increasing %stick register,
 * frequency scaling is used. Kernel reads of the %stick return a scaled
 * %stick value matching the pre-suspend frequency. Note that %stick frequency
 * scaling can be enabled when no suspend/resume operations have occurred.
 * This is to support a mode where an invariant 1GHz %stick frequency is
 * used and %stick reads are normalized to 1GHz starting at boot time.
 *
 * After a suspend/resume operation, user reads of %tick or %stick
 * are emulated. Emulation allows the %stick and %tick offset as well as
 * %stick frequency scaling to be applied to user code reading these
 * registers after a suspend resume operation. Suspend code enables emulation
 * by setting the %{tick,stick}.NPT fields which trigger a privileged
 * instruction access trap whenever the registers are read from user mode.
 * If emulation has been enabled, the trap handler emulates the instruction.
 * Emulation is enabled during a successful suspend/resume operation.
 * When emulation is enabled, CPUs that are DR'd into the system will have
 * their %{tick,stick}.NPT bits set to 1 as well. When frequency scaling
 * is being used to provide a normalized 1GHz %stick, emulation will be
 * enabled at boot time, prior to any suspend/resume operations.
 */

extern uint64_t getstick_raw(void);	/* returns %stick */
extern uint64_t gettick_counter(void);	/* returns %tick */
extern int mach_descrip_update(void);
extern uint64_t migration_tickscale(uint64_t, uint64_t);
extern cpuset_t cpu_ready_set;
extern uint64_t native_tick_offset;
extern uint64_t native_stick_offset;
extern uint64_t sys_tick_freq;
extern uint64_t mig_stick_normscale;

/*
 * Global Sun Cluster pre/post callbacks.
 */
const char *(*cl_suspend_error_decode)(int);
int (*cl_suspend_pre_callback)(void);
int (*cl_suspend_post_callback)(void);
#define	SC_PRE_FAIL_STR_FMT	"Sun Cluster pre-suspend failure: %d"
#define	SC_POST_FAIL_STR_FMT	"Sun Cluster post-suspend failure: %d"
#define	SC_FAIL_STR_MAX		256

/*
 * n2rng pre/post callbacks.
 */
const char *(*n2rng_suspend_error_decode)(int);
int (*n2rng_suspend_pre_callback)(void);
int (*n2rng_suspend_post_callback)(void);
#define	N2RNG_PRE_FAIL_STR_FMT	"RNG pre-suspend failure: %d"
#define	N2RNG_POST_FAIL_STR_FMT	"RNG post-suspend failure: %d"
#define	N2RNG_FAIL_STR_MAX		256

/*
 * pcbe pre/post callbacks.
 */
const char *(*pcbe_suspend_error_decode)(int);
int (*pcbe_suspend_pre_callback)(void);
int (*pcbe_suspend_post_callback)(void);
#define	PCBE_PRE_FAIL_STR_FMT	"PCBE pre-suspend failure: %d"
#define	PCBE_POST_FAIL_STR_FMT	"PCBE post-suspend failure: %d"
#define	PCBE_FAIL_STR_MAX		256

/*
 * The minimum major and minor version of the HSVC_GROUP_CORE API group
 * required in order to use OS suspend.
 */
#define	SUSPEND_CORE_MAJOR	1
#define	SUSPEND_CORE_MINOR	2

/*
 * By default, sun4v OS suspend is supported if the required HV version
 * is present. suspend_disabled should be set on platforms that do not
 * allow OS suspend regardless of whether or not the HV supports it.
 * It can also be set in /etc/system.
 */
static int suspend_disabled = 0;

/*
 * Controls whether or not user-land tick and stick register emulation
 * will be enabled following a successful suspend operation.
 */
static int enable_user_tick_stick_emulation = 1;

/*
 * Indicates whether or not tick and stick emulation is currently active.
 * After a successful suspend operation, if emulation is enabled, this
 * variable is set to B_TRUE. Global scope to allow emulation code to
 * check if emulation is active.
 */
boolean_t tick_stick_emulation_active = B_FALSE;

/*
 * When non-zero, after a successful suspend and resume, cpunodes, CPU HW
 * sharing data structures, and processor groups will be updated using
 * information from the updated MD.
 */
static int suspend_update_cpu_mappings = 1;

/*
 * The maximum number of microseconds by which the %tick or %stick register
 * can vary between any two CPUs in the system. To calculate the
 * native_stick_offset and native_tick_offset, we measure the change in these
 * registers on one CPU over a suspend/resume. Other CPUs may experience
 * slightly larger or smaller changes. %tick and %stick should be synchronized
 * between CPUs, but there may be some variation. So we add an additional value
 * derived from this variable to ensure that these registers always increase
 * over a suspend/resume operation, assuming all %tick and %stick registers
 * are synchronized (within a certain limit) across CPUs in the system. The
 * delta between %sticks on different CPUs should be a small number of cycles,
 * not perceptible to readers of %stick that migrate between CPUs. We set this
 * value to 1 millisecond which means that over a suspend/resume operation,
 * all CPU's %tick and %stick will advance forwards as long as, across all
 * CPUs, the %tick and %stick are synchronized to within 1 ms. This applies to
 * CPUs before the suspend and CPUs after the resume. 1 ms is conservative,
 * but small enough to not trigger TOD faults.
 */
static uint64_t suspend_tick_stick_max_delta = 1000; /* microseconds */

/*
 * The number of times the system has been suspended and resumed.
 */
static uint64_t suspend_count = 0;

/*
 * Physical %stick frequency, set at boot if using a normalized 1GHz stick
 * and updated after a suspend/resume.
 */
uint64_t phys_stick_freq = 0;

/*
 * Stick value saved at resume-time.
 */
static uint64_t resume_start_stick = 0;

/*
 * Ratio of original %stick frequency to current physical %stick frequency.
 */
uint_t tick_per_rawtick = 0;

/*
 * Routines and data structures for in-situ parsing of an MD, given only its
 * base address. This avoids doing memory allocations at potentially dangerous
 * times, e.g. during suspend/resume operations while CPUs are paused, before
 * the kernel is fully able to do memory allocations.
 */

/*
 * The MD access structure.
 * Simplifies calculations of offsets.
 */
typedef struct {
	md_header_t	*md_headerp;
	int		md_nelements;
	md_element_t	*md_elements;
	char		*md_names;
	char		*md_data;
} mda_t;

#define	MD_NELEMENTS(md)	((md)->md_nelements)

#define	MD_ELEMENT_TAG(md, n)		\
	((md)->md_elements[n].tag)

#define	MD_ELEMENT_NAME(md, n)		\
	((md)->md_names + (md)->md_elements[n].name_offset)

#define	MD_ELEMENT_NEXT(md, n)		\
	((md)->md_elements[n].d.prop_idx)

#define	MD_ELEMENT_PROP_VAL(md, n)	\
	((md)->md_elements[n].d.prop_val)

#define	MD_MAX_SIZE	(512 * 1024)

static char *migmd_base = NULL;
static uint64_t migmd_base_pa;
static uint64_t migmd_size = 0;
static int migmd_setup_mda(mda_t *, char *, int);
static int migmd_alloc_hv_md(void);
static void migmd_free_hv_md(void);
static int migmd_get_hv_md(mda_t *);
static int migmd_get_next_node_by_name(mda_t *, int, char *);
static int migmd_node_get_prop_val(mda_t *, int, char *, uint64_t *);
static int migmd_get_node_prop_val(char *, char *, uint64_t *);
static int migmd_get_cpu_freq(uint64_t *);
static int migmd_get_platform_stick_freq(uint64_t *);

/*
 * Routines and data structures used to update the kernel shadow prom
 * tree after a suspend/resume.
 */
static int update_stree_root_dev(void);
static int update_stree_cpu(md_t *, mde_cookie_t);

typedef struct _suspend_info {
	char	banner_name[SYS_NMLN];
} suspend_info_t;

suspend_info_t	pre_suspend_info;
suspend_info_t	post_suspend_info;

static int	set_suspend_info(suspend_info_t *);

/*
 * DBG and DBG_PROM() macro.
 */
#ifdef	DEBUG

static int suspend_debug_flag = 0;

#define	DBG_PROM		\
if (suspend_debug_flag)		\
	prom_printf

#define	DBG			\
if (suspend_debug_flag)		\
	suspend_debug

static void
suspend_debug(const char *fmt, ...)
{
	char	buf[512];
	va_list	ap;

	va_start(ap, fmt);
	(void) vsprintf(buf, fmt, ap);
	va_end(ap);

	cmn_err(CE_NOTE, "%s", buf);
}

#else /* DEBUG */

#define	DBG_PROM
#define	DBG

#endif /* DEBUG */


/*
 * Return true if the HV supports OS suspend and if suspend has not been
 * disabled on this platform.
 */
boolean_t
suspend_supported(void)
{
	uint64_t major, minor;

	if (suspend_disabled)
		return (B_FALSE);

	if (hsvc_version(HSVC_GROUP_CORE, &major, &minor) != 0)
		return (B_FALSE);

	return ((major == SUSPEND_CORE_MAJOR && minor >= SUSPEND_CORE_MINOR) ||
	    (major > SUSPEND_CORE_MAJOR));
}

/*
 * Memory DR is not permitted if the system has been suspended and resumed.
 * It is the responsibility of the caller of suspend_start and the DR
 * subsystem to serialize DR operations and suspend_memdr_allowed() checks.
 */
boolean_t
suspend_memdr_allowed(void)
{
	return (suspend_count == 0);
}

/*
 * Returns true if CPU lgroups need to become static. Until there is a
 * comprehensive way to dynamically update lgroups and other memory-
 * related structures, we must return true after the first suspend/resume.
 */
boolean_t
suspend_static_cpu_lgroups(void)
{
	return (suspend_count != 0);
}

/*
 * Given a source tick, stick, and tod value, set the tick and stick offsets
 * such that the (current physical register value) + offset == (source value)
 * and in addition account for some variation between the %tick/%stick on
 * different CPUs. We account for this variation by adding in double the value
 * of suspend_tick_stick_max_delta. The following is an explanation of why
 * suspend_tick_stick_max_delta must be multiplied by two and added to
 * native_stick_offset.
 *
 * Consider a guest instance that is yet to be suspended with CPUs p0 and p1
 * with physical "source" %stick values s0 and s1 respectively. When the guest
 * is first resumed, the physical "target" %stick values are t0 and t1
 * respectively. The virtual %stick values after the resume are v0 and v1
 * respectively. Let x be the maximum difference between any two CPU's %stick
 * register at a given point in time and let the %stick values be assigned
 * such that
 *
 *     s1 = s0 + x and
 *     t1 = t0 - x
 *
 * Let us assume that p0 is driving the suspend and resume. Then, we will
 * calculate the stick offset f and the virtual %stick on p0 after the
 * resume as follows.
 *
 *      f = s0 - t0 and
 *     v0 = t0 + f
 *
 * We calculate the virtual %stick v1 on p1 after the resume as
 *
 *     v1 = t1 + f
 *
 * Substitution yields
 *
 *     v1 = t1 + (s0 - t0)
 *     v1 = (t0 - x) + (s0 - t0)
 *     v1 = -x + s0
 *     v1 = s0 - x
 *     v1 = (s1 - x) - x
 *     v1 = s1 - 2x
 *
 * Therefore, in this scenario, without accounting for %stick variation in
 * the calculation of the native_stick_offset f, the virtual %stick on p1
 * is less than the value of the %stick on p1 before the suspend which is
 * unacceptable. By adding 2x to v1, we guarantee it will be equal to s1
 * which means the %stick on p1 after the resume will always be greater
 * than or equal to the %stick on p1 before the suspend. Since v1 = t1 + f
 * at any point in time, we can accomplish this by adding 2x to f. This
 * guarantees any processes bound to CPU P0 or P1 will not see a %stick
 * decrease across a suspend/resume. Hence, in the code below, we multiply
 * suspend_tick_stick_max_delta by two in the calculation for
 * native_stick_offset, native_tick_offset, and target_hrtime.
 */
static void
set_tick_offsets(uint64_t source_tick, uint64_t source_stick, timestruc_t *tsp)
{
	uint64_t target_tick;
	uint64_t target_stick;
	uint64_t prev_stick_offset;
	hrtime_t source_hrtime;
	hrtime_t target_hrtime;

	prev_stick_offset = native_stick_offset;

	/*
	 * Temporarily set the offsets to zero so that the following reads
	 * of the registers will yield physical unadjusted counter values.
	 */
	native_tick_offset = 0;
	native_stick_offset = 0;

	target_tick = gettick_counter();
	target_stick = getstick_raw();

	/*
	 * If the %stick frequency has changed since boot, compute the
	 * scaling factor for normalizing the new %stick freq to be the
	 * same as the original boot stick freq of the guest. Use this freq
	 * scaling to normalize the target stick value.
	 */
	if (sys_tick_freq != phys_stick_freq) {
		mig_stick_normscale = (uint_t)((sys_tick_freq <<
		    (32 - MIG_FREQ_SHIFT)) / phys_stick_freq) & ~1;
		target_stick = migration_tickscale(target_stick,
		    mig_stick_normscale);
		tick_per_rawtick = sys_tick_freq/phys_stick_freq;
	}

	/*
	 * Calculate the new offsets. In addition to the delta observed on
	 * this CPU, add an additional value. Multiply the %tick/%stick
	 * frequency by suspend_tick_stick_max_delta (us). Then, multiply by 2
	 * to account for a delta between CPUs before the suspend and a
	 * delta between CPUs after the resume.
	 */
	native_tick_offset = (source_tick - target_tick) +
	    (CPU->cpu_curr_clock * suspend_tick_stick_max_delta * 2 / MICROSEC);
	native_stick_offset = (source_stick - target_stick) +
	    (sys_tick_freq * suspend_tick_stick_max_delta * 2 / MICROSEC);

	/*
	 * Paranoid check for the unlikely case where the new
	 * stick offset's low-32 bits are identical to the old stick
	 * offset's low 32-bits. We can't allow that since we encode the low
	 * order 32-bit stick offset in the mig_stick_normscale to detect
	 * a change in stick/freq during stick reads. See machclock.h
	 */
	if ((prev_stick_offset << 32) == (native_stick_offset << 32))
		native_stick_offset ^= 1;

	/*
	 * Encode the info we need to normalize the target stick value.
	 * When the %stick frequency has changed, we encode the %stick
	 * offset into the invariant stick freq. See machclock.h
	 */
	if (sys_tick_freq != phys_stick_freq) {
		mig_stick_normscale |= native_stick_offset << 32;
	} else {
		mig_stick_normscale = native_stick_offset;
	}

	/*
	 * We've effectively increased %stick and %tick by twice the value
	 * of suspend_tick_stick_max_delta to account for variation across
	 * CPUs. Now adjust the preserved TOD by the same amount.
	 */
	source_hrtime = ts2hrt(tsp);
	target_hrtime = source_hrtime +
	    (suspend_tick_stick_max_delta * 2 * (NANOSEC/MICROSEC));
	hrt2ts(target_hrtime, tsp);

	resume_start_stick = gethrtime_unscaled();
}

/*
 * Set the {tick,stick}.NPT field to 1 on this CPU.
 */
static void
enable_tick_stick_npt(void)
{
	(void) hv_stick_set_npt(1);
	(void) hv_tick_set_npt(1);
}

/*
 * Convert a scaled stick value to a raw stick value.
 */
static uint64_t
conv_stick2rawstick(uint64_t stick)
{
	uint64_t q = stick / sys_tick_freq;
	uint64_t r = stick - (q * sys_tick_freq);

	return (q * phys_stick_freq +
	    ((r * phys_stick_freq) / sys_tick_freq));
}

/*
 * After migration into a target machine that has a different
 * stick frequency, we normalized the raw stick value so that
 * the stick value appeared to count at the same rate (freq) as
 * the original boot stick freq, thus maintaining a boot stick
 * freq invariant. This routine is needed when we need to convert
 * the normalized stick value back to the raw stick value for the
 * purpose of programming the stick comparison register. This
 * routine should only be used by the cyclic backend logic.
 * Cyclic subsystem is suspended during migration which may change
 * the globals used here.
 */
uint64_t
unscale2rawstick(uint64_t stick)
{
	uint64_t unscale = 0;
	uint64_t rescale;
	uint64_t diff;
	uint64_t freqscale = mig_stick_normscale;

	/*
	 * Apply the native stick offset before
	 * normalizing.
	 */
	diff = stick - native_stick_offset;

	if (native_stick_offset == freqscale) {
		/*
		 * No need for freq normalization
		 * See machclock.h
		 */
		return (diff);
	} else if (native_stick_offset != 0) {
		freqscale = freqscale << 32;
		freqscale = freqscale >> 32;
	}

	/*
	 * Make sure we start at a reasonable value
	 * for the unscale to work correctly
	 */
	if (stick < resume_start_stick)
		stick = diff = gethrtime_unscaled() - native_stick_offset;
	else
		stick = diff;
	while (diff > tick_per_rawtick) {
		unscale += conv_stick2rawstick(diff);
		rescale = unscale;
		rescale = migration_tickscale(rescale, freqscale);
		diff = stick - rescale;
	}

	return (unscale);
}

/*
 * Synchronize a CPU's {tick,stick}.NPT fields with the current state
 * of the system. This is used when a CPU is DR'd into the system.
 */
void
suspend_sync_tick_stick_npt(void)
{
	if (tick_stick_emulation_active) {
		DBG("enabling {%%tick/%%stick}.NPT on CPU 0x%x", CPU->cpu_id);
		(void) hv_stick_set_npt(1);
		(void) hv_tick_set_npt(1);
	}
}

/*
 * Obtain an updated MD from the hypervisor and update cpunodes, CPU HW
 * sharing data structures, and processor groups.
 */
static void
update_cpu_mappings(void)
{
	md_t		*mdp;
	processorid_t	id;
	cpu_t		*cp;
	cpu_pg_t	*pgps[NCPU];
	mde_cookie_t	*cpulist;
	int		num_cpus;
	int		i;

	if ((mdp = md_get_handle()) == NULL) {
		DBG("suspend: md_get_handle failed");
		return;
	}

	DBG("suspend: updating CPU mappings");

	mutex_enter(&cpu_lock);

	/* Re-initialize cpu node info from updated MD */
	num_cpus = md_alloc_scan_dag(mdp,
	    md_root_node(mdp), "cpu", "fwd", &cpulist);
	for (i = 0; i < num_cpus; i++) {
		fill_cpu(mdp, cpulist[i]);
		(void) update_stree_cpu(mdp, cpulist[i]);
	}
	md_free_scan_dag(mdp, &cpulist);

	setup_chip_mappings(mdp);
	setup_exec_unit_mappings(mdp);
	for (id = 0; id < NCPU; id++) {
		processor_info_t *pi;
		struct cpu_node *cpunode;

		if ((cp = cpu_get(id)) == NULL)
			continue;
		cpu_map_exec_units(cp);

		pi = &cp->cpu_type_info;
		cpunode = &cpunodes[id];

		/*
		 * Get clock-frequency property from cpunodes[] for the CPU.
		 */
		pi->pi_clock = (cpunode->clock_freq + 500000) / 1000000;

		/*
		 * Current frequency in Hz.
		 */
		cp->cpu_curr_clock = cpunode->clock_freq;

		/*
		 * Supported frequencies.
		 */
		cpu_set_supp_freqs(cp, NULL);

		/*
		 * Free cpu ID string and brand string.
		 */
		if (cp->cpu_idstr) {
			kmem_free(cp->cpu_idstr, strlen(cp->cpu_idstr) + 1);
		}
		if (cp->cpu_brandstr) {
			kmem_free(cp->cpu_brandstr,
			    strlen(cp->cpu_brandstr) + 1);
		}

		/*
		 * Set cpu ID string and brand string.
		 */
		populate_idstr(cp);
	}

	/*
	 * Re-calculate processor groups.
	 *
	 * First tear down all PG information before adding any new PG
	 * information derived from the MD we just downloaded. We must
	 * call pg_cpu_inactive and pg_cpu_active with CPUs paused and
	 * we want to minimize the number of times pause_cpus is called.
	 * Inactivating all CPUs would leave PGs without any active CPUs,
	 * so while CPUs are paused, call pg_cpu_inactive and swap in the
	 * bootstrap PG structure saving the original PG structure to be
	 * fini'd afterwards. This prevents the dispatcher from encountering
	 * PGs in which all CPUs are inactive. Offline CPUs are already
	 * inactive in their PGs and shouldn't be reactivated, so we must
	 * not call pg_cpu_inactive or pg_cpu_active for those CPUs. In
	 * addition, offline CPUs run the idle thread and therefore don't
	 * require a call to pg_cmt_cpu_startup after the new PGs are
	 * created.
	 */
	pause_cpus(NULL);
	for (id = 0; id < NCPU; id++) {
		if ((cp = cpu_get(id)) == NULL)
			continue;
		if ((cp->cpu_flags & CPU_OFFLINE) == 0)
			pg_cpu_inactive(cp);
		pgps[id] = cp->cpu_pg;
		pg_cpu_bootstrap(cp);
	}
	start_cpus();

	/*
	 * pg_cpu_fini* and pg_cpu_init* must be called while CPUs are
	 * not paused. Use two separate loops here so that we do not
	 * initialize PG data for CPUs until all the old PG data structures
	 * are torn down.
	 */
	for (id = 0; id < NCPU; id++) {
		if ((cp = cpu_get(id)) == NULL)
			continue;
		pg_cpu_fini(cp, pgps[id]);
		mpo_cpu_remove(id);
	}

	/*
	 * Initialize PG data for each CPU, but leave the bootstrapped
	 * PG structure in place to avoid running with any PGs containing
	 * nothing but inactive CPUs.
	 */
	for (id = 0; id < NCPU; id++) {
		if ((cp = cpu_get(id)) == NULL)
			continue;
		mpo_cpu_add(mdp, id);
		pgps[id] = pg_cpu_init(cp, B_TRUE);
	}

	/*
	 * Now that PG data has been initialized for all CPUs in the
	 * system, replace the bootstrapped PG structure with the
	 * initialized PG structure and call pg_cpu_active for each CPU.
	 */
	pause_cpus(NULL);
	for (id = 0; id < NCPU; id++) {
		if ((cp = cpu_get(id)) == NULL)
			continue;
		cp->cpu_pg = pgps[id];
		if ((cp->cpu_flags & CPU_OFFLINE) == 0) {
			pg_cpu_active(cp);
			pg_cmt_cpu_startup(cp);
		}
	}
	start_cpus();

	mutex_exit(&cpu_lock);

	(void) md_fini_handle(mdp);
}

/*
 * Wrapper for the Sun Cluster error decoding function.
 */
static int
cluster_error_decode(int error, char *error_reason, size_t max_reason_len)
{
	const char	*decoded;
	size_t		decoded_len;

	ASSERT(error_reason != NULL);
	ASSERT(max_reason_len > 0);

	max_reason_len = MIN(max_reason_len, SC_FAIL_STR_MAX);

	if (cl_suspend_error_decode == NULL)
		return (-1);

	if ((decoded = (*cl_suspend_error_decode)(error)) == NULL)
		return (-1);

	/* Get number of non-NULL bytes */
	if ((decoded_len = strnlen(decoded, max_reason_len - 1)) == 0)
		return (-1);

	bcopy(decoded, error_reason, decoded_len);

	/*
	 * The error string returned from cl_suspend_error_decode
	 * should be NULL-terminated, but set the terminator here
	 * because we only copied non-NULL bytes. If the decoded
	 * string was not NULL-terminated, this guarantees that
	 * error_reason will be.
	 */
	error_reason[decoded_len] = '\0';

	return (0);
}

/*
 * Wrapper for the Sun Cluster pre-suspend callback.
 */
static int
cluster_pre_wrapper(char *error_reason, size_t max_reason_len)
{
	int rv = 0;

	if (cl_suspend_pre_callback != NULL) {
		rv = (*cl_suspend_pre_callback)();
		DBG("suspend: cl_suspend_pre_callback returned %d", rv);
		if (rv != 0 && error_reason != NULL && max_reason_len > 0) {
			if (cluster_error_decode(rv, error_reason,
			    max_reason_len)) {
				(void) snprintf(error_reason, max_reason_len,
				    SC_PRE_FAIL_STR_FMT, rv);
			}
		}
	}

	return (rv);
}

/*
 * Wrapper for the Sun Cluster post-suspend callback.
 */
static int
cluster_post_wrapper(char *error_reason, size_t max_reason_len)
{
	int rv = 0;

	if (cl_suspend_post_callback != NULL) {
		rv = (*cl_suspend_post_callback)();
		DBG("suspend: cl_suspend_post_callback returned %d", rv);
		if (rv != 0 && error_reason != NULL && max_reason_len > 0) {
			if (cluster_error_decode(rv, error_reason,
			    max_reason_len)) {
				(void) snprintf(error_reason,
				    max_reason_len, SC_POST_FAIL_STR_FMT, rv);
			}
		}
	}

	return (rv);
}

/*
 * Wrapper for the n2rng error decoding function.
 */
static int
n2rng_error_decode(int error, char *error_reason, size_t max_reason_len)
{
	const char	*decoded;
	size_t		decoded_len;

	ASSERT(error_reason != NULL);
	ASSERT(max_reason_len > 0);

	max_reason_len = MIN(max_reason_len, N2RNG_FAIL_STR_MAX);

	if (n2rng_suspend_error_decode == NULL)
		return (-1);

	if ((decoded = (*n2rng_suspend_error_decode)(error)) == NULL)
		return (-1);

	/* Get number of non-NULL bytes */
	if ((decoded_len = strnlen(decoded, max_reason_len - 1)) == 0)
		return (-1);

	bcopy(decoded, error_reason, decoded_len);

	/*
	 * The error string returned from n2rng_suspend_error_decode
	 * should be NULL-terminated, but set the terminator here
	 * because we only copied non-NULL bytes. If the decoded
	 * string was not NULL-terminated, this guarantees that
	 * error_reason will be.
	 */
	error_reason[decoded_len] = '\0';

	return (0);
}

/*
 * Wrapper for the n2rng pre-suspend callback.
 */
static int
n2rng_pre_wrapper(char *error_reason, size_t max_reason_len)
{
	int rv = 0;

	if (n2rng_suspend_pre_callback != NULL) {
		rv = (*n2rng_suspend_pre_callback)();
		DBG("suspend: n2rng_suspend_pre_callback returned %d", rv);
		if (rv != 0 && error_reason != NULL && max_reason_len > 0) {
			if (n2rng_error_decode(rv, error_reason,
			    max_reason_len)) {
				(void) snprintf(error_reason, max_reason_len,
				    N2RNG_PRE_FAIL_STR_FMT, rv);
			}
		}
	}

	return (rv);
}

/*
 * Wrapper for the n2rng post-suspend callback.
 */
static int
n2rng_post_wrapper(char *error_reason, size_t max_reason_len)
{
	int rv = 0;

	if (n2rng_suspend_post_callback != NULL) {
		rv = (*n2rng_suspend_post_callback)();
		DBG("suspend: n2rng_suspend_post_callback returned %d", rv);
		if (rv != 0 && error_reason != NULL && max_reason_len > 0) {
			if (n2rng_error_decode(rv, error_reason,
			    max_reason_len)) {
				(void) snprintf(error_reason,
				    max_reason_len, N2RNG_POST_FAIL_STR_FMT,
				    rv);
			}
		}
	}

	return (rv);
}

/*
 * Wrapper for the pcbe error decoding function.
 */
static int
pcbe_error_decode(int error, char *error_reason, size_t max_reason_len)
{
	const char	*decoded;
	size_t		decoded_len;

	ASSERT(error_reason != NULL);
	ASSERT(max_reason_len > 0);

	max_reason_len = MIN(max_reason_len, PCBE_FAIL_STR_MAX);

	if (pcbe_suspend_error_decode == NULL)
		return (-1);

	if ((decoded = (*pcbe_suspend_error_decode)(error)) == NULL)
		return (-1);

	/* Get number of non-NULL bytes */
	if ((decoded_len = strnlen(decoded, max_reason_len - 1)) == 0)
		return (-1);

	bcopy(decoded, error_reason, decoded_len);

	/*
	 * The error string returned from pcbe_suspend_error_decode
	 * should be NULL-terminated, but set the terminator here
	 * because we only copied non-NULL bytes. If the decoded
	 * string was not NULL-terminated, this guarantees that
	 * error_reason will be.
	 */
	error_reason[decoded_len] = '\0';

	return (0);
}

/*
 * Wrapper for the pcbe pre-suspend callback.
 */
static int
pcbe_pre_wrapper(char *error_reason, size_t max_reason_len)
{
	int rv = 0;

	if (pcbe_suspend_pre_callback != NULL) {
		rv = (*pcbe_suspend_pre_callback)();
		DBG("suspend: pcbe_suspend_pre_callback returned %d", rv);
		if (rv != 0 && error_reason != NULL && max_reason_len > 0) {
			if (pcbe_error_decode(rv, error_reason,
			    max_reason_len)) {
				(void) snprintf(error_reason, max_reason_len,
				    PCBE_PRE_FAIL_STR_FMT, rv);
			}
		}
	}

	return (rv);
}

/*
 * Wrapper for the pcbe post-suspend callback.
 */
static int
pcbe_post_wrapper(char *error_reason, size_t max_reason_len)
{
	int rv = 0;

	if (pcbe_suspend_post_callback != NULL) {
		rv = (*pcbe_suspend_post_callback)();
		DBG("suspend: pcbe_suspend_post_callback returned %d", rv);
		if (rv != 0 && error_reason != NULL && max_reason_len > 0) {
			if (pcbe_error_decode(rv, error_reason,
			    max_reason_len)) {
				(void) snprintf(error_reason,
				    max_reason_len, PCBE_POST_FAIL_STR_FMT,
				    rv);
			}
		}
	}

	return (rv);
}

/*
 * Execute pre-suspend callbacks preparing the system for a suspend operation.
 * Returns zero on success, non-zero on failure. Sets the recovered argument
 * to indicate whether or not callbacks could be undone in the event of a
 * failure--if callbacks were successfully undone, *recovered is set to B_TRUE,
 * otherwise *recovered is set to B_FALSE. Must be called successfully before
 * suspend_start can be called. Callers should first call suspend_support to
 * determine if OS suspend is supported.
 */
int
suspend_pre(char *error_reason, size_t max_reason_len, boolean_t *recovered)
{
	int cl_rv, n2rng_rv, pcbe_rv;

	ASSERT(recovered != NULL);

	/*
	 * Return an error if suspend_pre is erroneously called
	 * when OS suspend is not supported.
	 */
	ASSERT(suspend_supported());
	if (!suspend_supported()) {
		DBG("suspend: suspend_pre called without suspend support");
		*recovered = B_TRUE;
		return (ENOTSUP);
	}
	DBG("suspend: %s", __func__);

	cl_rv = cluster_pre_wrapper(error_reason, max_reason_len);

	/*
	 * If this pre-suspend operation fails,
	 * no recovery needs to be done.
	 */
	if (cl_rv != 0 && recovered != NULL) {
		*recovered = B_TRUE;
		return (cl_rv);
	}

	n2rng_rv = n2rng_pre_wrapper(error_reason, max_reason_len);

	if (n2rng_rv != 0 && recovered != NULL) {
		cl_rv = cluster_post_wrapper(error_reason, max_reason_len);
		*recovered = B_TRUE;
		return (n2rng_rv);
	}

	pcbe_rv = pcbe_pre_wrapper(error_reason, max_reason_len);

	if (pcbe_rv != 0 && recovered != NULL) {
		n2rng_rv = n2rng_post_wrapper(error_reason, max_reason_len);
		cl_rv = cluster_post_wrapper(error_reason, max_reason_len);
		*recovered = B_TRUE;
		return (pcbe_rv);
	}

	return (0);
}

/*
 * Execute post-suspend callbacks. Returns zero on success, non-zero on
 * failure. Must be called after suspend_start is called, regardless of
 * whether or not suspend_start is successful.
 */
int
suspend_post(char *error_reason, size_t max_reason_len)
{
	int	rv;

	ASSERT(suspend_supported());
	DBG("suspend: %s", __func__);

	rv = pcbe_post_wrapper(error_reason, max_reason_len);

	rv = n2rng_post_wrapper(error_reason, max_reason_len);

	rv = cluster_post_wrapper(error_reason, max_reason_len);

	return (rv);
}


/*
 * Per-cpu clock frequencies and inter-cpu-latency may have changed,
 * update and recompute delays and timeout values that depend on these
 * freqs/property.
 */
void
update_timeouts()
{
	extern void recalc_xc_timeouts(void);

	/*
	 * Re-initialize xc timeouts and adjust for
	 * per-cpu clock freq changes
	 */
	mutex_enter(&cpu_lock);
	xc_compute_timeouts();
	mutex_exit(&cpu_lock);

	/* Adjust for inter-cpu-latency change */
	recalc_xc_timeouts();
}


/*
 * Suspends the OS by pausing CPUs and calling into the HV to initiate
 * the suspend. When the HV routine hv_guest_suspend returns, the system
 * will be resumed. Must be called after a successful call to suspend_pre.
 * suspend_post must be called after suspend_start, whether or not
 * suspend_start returns an error. A non-zero sim_suspend_delay argument
 * indicates this is a simulated suspend/resume operation and that the
 * call to hv_guest_suspend should be replaced with a busy wait for
 * sim_suspend_delay microseconds. The sim_suspend_result argument is the
 * simulated return value of hv_guest_suspend. Simulated suspend/resume
 * operations are used for testing only.
 */
/*ARGSUSED*/
static int
suspend_start_common(char *error_reason, size_t max_reason_len,
    clock_t sim_suspend_delay, uint64_t sim_suspend_result)
{
	uint64_t	source_tick;
	uint64_t	source_stick;
	uint64_t	rv;
	timestruc_t	source_tod;
	int		spl;

	ASSERT(suspend_supported());
	DBG("suspend: %s", __func__);

	/* Preallocate md buffer */
	if (migmd_alloc_hv_md() != 1)
		return (1);

	(void) set_suspend_info(&pre_suspend_info);

	cmn_err(CE_CONT, "?suspending on %s", pre_suspend_info.banner_name);

	mutex_enter(&cpu_lock);

	/* Suspend the watchdog */
	watchdog_suspend();

	/* Record the TOD */
	mutex_enter(&tod_lock);
	source_tod = tod_get();
	mutex_exit(&tod_lock);

	/*
	 * kmem cannot be allocated in any thread after domains are locked, due
	 * to lock ordering, so do this last, just before pausing CPUs.
	 */
	sfmmu_ctxdoms_lock();

	/* Pause all other CPUs */
	pause_cpus(NULL);
	DBG_PROM("suspend: CPUs paused\n");

	/* Suspend cyclics */
	cyclic_suspend();
	DBG_PROM("suspend: cyclics suspended\n");

	/* Disable interrupts */
	spl = spl8();
	DBG_PROM("suspend: spl8()\n");

	source_tick = gettick_counter();
	source_stick = gettick();
	DBG_PROM("suspend: source_tick: 0x%lx\n", source_tick);
	DBG_PROM("suspend: source_stick: 0x%lx\n", source_stick);

	/*
	 * Call into the HV to initiate the suspend. hv_guest_suspend()
	 * returns after the guest has been resumed or if the suspend
	 * operation failed or was cancelled. After a successful suspend,
	 * the %tick and %stick registers may have changed by an amount
	 * that is not proportional to the amount of time that has passed.
	 * They may have jumped forwards or backwards. Some variation is
	 * allowed and accounted for using suspend_tick_stick_max_delta,
	 * but otherwise this jump must be uniform across all CPUs and we
	 * operate under the assumption that it is (maintaining two global
	 * offset variables--one for %tick and one for %stick.)
	 */
	DBG_PROM("suspend: suspending... \n");

	if (sim_suspend_delay == 0) {
		rv = hv_guest_suspend();
	} else {
		/* Simulated suspend/resume request */
		drv_usecwait(sim_suspend_delay);
		rv = sim_suspend_result;
	}

	if (rv != 0) {
		splx(spl);
		sfmmu_ctxdoms_unlock();
		cyclic_resume();
		start_cpus();
		watchdog_resume();
		mutex_exit(&cpu_lock);
		migmd_free_hv_md();
		DBG("suspend: failed, rv: %ld\n", rv);
		return (rv);
	}

	suspend_count++;

	if (!migmd_get_platform_stick_freq(&phys_stick_freq))
		cmn_err(CE_PANIC, "suspend: get stick freq failed");

	migmd_free_hv_md();

	/* Update the global tick and stick offsets and the preserved TOD */
	set_tick_offsets(source_tick, source_stick, &source_tod);

	/* Ensure new offsets are globally visible before resuming CPUs */
	membar_sync();

	/* Enable interrupts */
	splx(spl);

	/* Set the {%tick,%stick}.NPT bits on all CPUs */
	if (enable_user_tick_stick_emulation) {
		xc_all((xcfunc_t *)enable_tick_stick_npt, NULL, NULL);
		xt_sync(cpu_ready_set);
	}

	/* If emulation is enabled, but not currently active, enable it */
	if (enable_user_tick_stick_emulation && !tick_stick_emulation_active) {
		tick_stick_emulation_active = B_TRUE;
	}

	sfmmu_ctxdoms_remove();
	sfmmu_ctxdoms_unlock();

	/* Resume cyclics, unpause CPUs */
	cyclic_resume();
	start_cpus();

	/* Set the TOD */
	mutex_enter(&tod_lock);
	tod_set(source_tod);
	mutex_exit(&tod_lock);

	/* Re-enable the watchdog */
	watchdog_resume();

	/* Download the latest MD */
	if ((rv = mach_descrip_update()) != 0)
		cmn_err(CE_PANIC, "suspend: mach_descrip_update failed: %ld",
		    rv);

	/*
	 * Continue to hold cpu_lock while domains are updated to protect
	 * against a concurrent CPU online operation.
	 */
	sfmmu_ctxdoms_update();
	mutex_exit(&cpu_lock);

	/* Get new MD, update CPU mappings/relationships */
	if (suspend_update_cpu_mappings)
		update_cpu_mappings();

	update_timeouts();

	(void) update_stree_root_dev();

	DBG("suspend: target tick: 0x%lx", gettick_counter());
	DBG("suspend: target stick: 0x%llx", gettick());
	DBG("suspend: user %%tick/%%stick emulation is %d",
	    tick_stick_emulation_active);
	DBG("suspend: finished");

	(void) set_suspend_info(&post_suspend_info);

	cmn_err(CE_CONT, "?resumed on %s", post_suspend_info.banner_name);

	return (0);
}


/*
 * Given a MD base address, verify the MD and setup the MD access struct.
 */
static int
migmd_setup_mda(mda_t *md, char *md_base, int md_size)
{
	md_header_t *mdhp = (md_header_t *)md_base;
	int total_size;

	/*
	 * Verify MD is valid.
	 */
	if (mdhp->transport_version != MD_TRANSPORT_VERSION) {
		return (0);
	}

	total_size = sizeof (md_header_t) + mdhp->node_blk_sz +
	    mdhp->name_blk_sz + mdhp->data_blk_sz;
	if (md_size < sizeof (md_header_t) || total_size > md_size) {
		return (0);
	}

	/*
	 * Setup the internal pointers/values in the MD access structure.
	 */
	md->md_headerp = mdhp;
	md->md_nelements = mdhp->node_blk_sz / sizeof (md_element_t);
	md->md_elements = (md_element_t *)(md_base + sizeof (md_header_t));
	md->md_names = md_base + sizeof (md_header_t) + mdhp->node_blk_sz;
	md->md_data = md->md_names + mdhp->name_blk_sz;
	return (1);
}

/*
 * Pre-allocate a buffer large enough for the largest Guest MD.
 */
static int
migmd_alloc_hv_md()
{
	if (migmd_base != NULL) {
		return (1);
	}

	if ((migmd_base =
	    contig_mem_alloc_align(MD_MAX_SIZE, PAGESIZE)) == NULL) {
		return (0);
	}
	migmd_base_pa = va_to_pa(migmd_base);
	return (1);
}

/*
 * Free the buffer allocated by migmd_alloc_hv_md.
 */
static void
migmd_free_hv_md()
{
	if (migmd_base != NULL) {
		contig_mem_free(migmd_base, MD_MAX_SIZE);
		migmd_base = NULL;
		migmd_base_pa = 0;
		migmd_size = 0;
	}
}

static int
migmd_get_hv_md(mda_t *md)
{
	uint64_t	hvret;

	/*
	 * Check if HV MD has already been fetched.  If not, fetch it.
	 */
	if (migmd_size == 0) {
		/*
		 * Need to allocate MD buffer prior to migration.
		 */
		if (migmd_base == NULL) {
			return (0);
		}

		migmd_size = MD_MAX_SIZE;
		hvret = hv_mach_desc(migmd_base_pa, &migmd_size);

		if (hvret != H_EOK) {
			migmd_free_hv_md();
			return (0);
		}
	}

	/*
	 * Setup the MD access structure.
	 */
	if (migmd_setup_mda(md, migmd_base, migmd_size) == 0) {
		migmd_free_hv_md();
		return (0);
	}

	return (1);
}

/*
 * Find the first node from a given point in an MD that has the given name.
 */
static int
migmd_get_next_node_by_name(mda_t *md, int nodeid, char *name)
{
	int i;

	for (i = nodeid; i < MD_NELEMENTS(md) &&
	    MD_ELEMENT_TAG(md, i) == MDET_NODE; i = MD_ELEMENT_NEXT(md, i)) {

		if (strcmp(MD_ELEMENT_NAME(md, i), name) == 0)
			return (i);
	}
	return (-1);
}

/*
 * Find the value property within the given node in the MD and return its value
 */
static int
migmd_node_get_prop_val(mda_t *md, int nodeid, char *name, uint64_t *valp)
{
	int i;

	if (MD_ELEMENT_TAG(md, nodeid) != MDET_NODE) {
		return (0);
	}

	for (i = nodeid; i < MD_NELEMENTS(md) &&
	    MD_ELEMENT_TAG(md, i) != MDET_NODE_END; i++) {

		if (MD_ELEMENT_TAG(md, i) == MDET_PROP_VAL &&
		    strcmp(MD_ELEMENT_NAME(md, i), name) == 0) {
			*valp = MD_ELEMENT_PROP_VAL(md, i);
			return (1);
		}
	}
	return (0);
}

/*
 * Return a property value given a nodename and property name.
 */
static int
migmd_get_node_prop_val(char *nodename, char *propname, uint64_t *valp)
{
	mda_t mdt, *md = &mdt;
	int i;

	if (!migmd_get_hv_md(md)) {
		return (0);
	}
	for (i = 0; i < MD_NELEMENTS(md); i = MD_ELEMENT_NEXT(md, i)) {
		if ((i = migmd_get_next_node_by_name(md, i, nodename)) < 0) {
			break;
		}
		if (migmd_node_get_prop_val(md, i, propname, valp)) {
			return (1);
		}
	}
	return (0);
}

/*
 * Return the value of the first found "cpu" node's "clock-frequency" property.
 */
static int
migmd_get_cpu_freq(uint64_t *freqp)
{
	return (migmd_get_node_prop_val("cpu", "clock-frequency", freqp));
}

/*
 * Return the value of the "platform" node's "stick-frequency" property.
 */
static int
migmd_get_platform_stick_freq(uint64_t *freqp)
{
	return (migmd_get_node_prop_val("platform", "stick-frequency", freqp));
}

extern int promif_stree_setprop(pnode_t, char *name, void *value, int len);
extern void *promif_stree_getroot(void);

typedef struct {
	processorid_t	cpuid;
	pnode_t		nodeid;
} mig_search_arg_t;

/*
 * Helper function passed to ddi_walk_devs() to find the device tree
 * node that corresponds to the cpuid also passed to ddi_walk_devs()
 * in a mig_search_arg_t.
 */
static int
mig_cpu_check_node(dev_info_t *dip, void *arg)
{
	char	    *name;
	processorid_t   cpuid;
	mig_search_arg_t *sarg = (mig_search_arg_t *)arg;

	if (dip == ddi_root_node()) {
		return (DDI_WALK_CONTINUE);
	}

	name = ddi_node_name(dip);

	if (strcmp(name, "cpu") != 0) {
		return (DDI_WALK_PRUNECHILD);
	}

	cpuid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", -1);

	cpuid = PROM_CFGHDL_TO_CPUID(cpuid);

	if (cpuid == sarg->cpuid) {
		sarg->nodeid = DEVI(dip)->devi_nodeid;
		return (DDI_WALK_TERMINATE);
	}

	return (DDI_WALK_CONTINUE);
}

/*
 * Walk the device tree to find the nodeid corresponding to the cpuid
 * passed in. If present, the nodeid is returned.  If no matching device
 * node if found, OBP_NONODE is returned.
 */
static pnode_t
mig_cpu_find_nodeid(processorid_t cpuid)
{
	mig_search_arg_t	arg;

	arg.cpuid = cpuid;
	arg.nodeid = OBP_NONODE;

	ddi_walk_devs(ddi_root_node(), mig_cpu_check_node, &arg);

	return (arg.nodeid);
}

/*
 * Update the 'banner-name' and 'name' properties of the root node of the
 * kernel  shadow prom tree with current values from the MD.
 */
static int
update_stree_root_dev(void)
{
	pnode_t		*rpnode;
	md_t		*mdp;
	mde_cookie_t	*platlist, mdroot;
	char		*namebuf;

	if ((mdp = md_get_handle()) == NULL) {
		DBG("update_stree_root_dev: md_get_handle failed");
		return (-1);
	}

	rpnode = (pnode_t *)promif_stree_getroot();

	mdroot = md_root_node(mdp);

	if (md_alloc_scan_dag(mdp, mdroot, "platform", "fwd",
	    &platlist) < 0) {
		DBG("update_stree_root_dev: md_alloc_scan_dag failed");
		(void) md_fini_handle(mdp);
		return (-1);
	}

	if (md_get_prop_str(mdp, platlist[0], "banner-name",
	    &namebuf) != 0) {
		md_free_scan_dag(mdp, &platlist);
		(void) md_fini_handle(mdp);
		DBG("update_stree_root_dev: failed to get banner-name prop");
		return (-1);
	}

	(void) promif_stree_setprop(*rpnode, "banner-name", namebuf,
	    strlen(namebuf) + 1);

	if (md_get_prop_str(mdp, platlist[0],
	    "name", &namebuf) != 0) {
		md_free_scan_dag(mdp, &platlist);
		(void) md_fini_handle(mdp);
		DBG("update_stree_root_dev: failed to get name prop");
		return (-1);
	}

	(void) promif_stree_setprop(*rpnode, "name", namebuf,
	    strlen(namebuf)+1);

	md_free_scan_dag(mdp, &platlist);
	(void) md_fini_handle(mdp);

	return (0);
}

/*
 * Update the 'compatible' and 'clock-frequency' properties of the Solaris
 * shadow prom tree node with values from corresponding  MD CPU node passed to
 * the function.
 */
static int
update_stree_cpu(md_t *mdp, mde_cookie_t cpuc)
{
	pnode_t		nodeid;
	uint64_t	cpuid;
	uint64_t	clk_freq64;
	uint32_t	clk_freq32;
	char		*namebuf;
	int		namelen;

	if (md_get_prop_val(mdp, cpuc, "id", &cpuid)) {
		return (-1);
	}

	if (cpuid >= NCPU) {
		return (-1);
	}

	if ((nodeid = mig_cpu_find_nodeid(cpuid)) == OBP_NONODE) {
		return (-1);
	}

	if (md_get_prop_data(mdp, cpuc,
	    "compatible", (uint8_t **)&namebuf, &namelen)) {
		return (-1);
	}

	if (md_get_prop_val(mdp, cpuc,
	    "clock-frequency", &clk_freq64)) {
		return (-1);
	}

	clk_freq32 = (uint32_t)(clk_freq64 & UINT32_MAX);

	(void) promif_stree_setprop(nodeid, "compatible", namebuf, namelen);

	(void) promif_stree_setprop(nodeid, "clock-frequency", &clk_freq32,
	    sizeof (uint32_t));

	return (0);
}

int
suspend_start(char *error_reason, size_t max_reason_len)
{
	return (suspend_start_common(error_reason, max_reason_len, 0, 0));
}

/*
 * Perform a simulated suspend operation by running through the
 * suspend/resume sequencing without calling into the HV to initiate an
 * actual suspend of this guest. Instead, delay for sim_suspend_delay
 * microseconds and then continue as if hv_guest_suspend had been called
 * and returned sim_suspend_rv. Used for testing purposes only, allowing
 * simulated suspend/resume operations to be performed in debug and non-
 * debug modes.
 */
int
suspend_start_simulated(char *error_reason, size_t max_reason_len,
    clock_t sim_suspend_delay, uint64_t sim_suspend_rv)
{
	if (sim_suspend_delay == 0)
		sim_suspend_delay = 1;

	return (suspend_start_common(error_reason, max_reason_len,
	    sim_suspend_delay, sim_suspend_rv));
}

static int
set_suspend_info(suspend_info_t *sinfop)
{
	pnode_t	node;
	size_t	size;
	char *buf = sinfop->banner_name;

	node = prom_rootnode();
	if ((node == OBP_NONODE) || (node == OBP_BADNODE)) {
		cmn_err(CE_CONT, "get_banner_name: node 0x%x\n", (uint_t)node);
		return (-1);
	}

	if (((size = prom_getproplen(node, "banner-name")) != -1) &&
	    (size <= SYS_NMLN) &&
	    (prom_getprop(node, "banner-name", buf) != -1)) {
		return (0);
	}

	return (-1);
}

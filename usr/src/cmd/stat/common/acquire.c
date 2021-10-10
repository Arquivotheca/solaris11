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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "statcommon.h"
#include "dsr.h"

#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>

#define	ARRAY_SIZE(a)	(sizeof (a) / sizeof (*a))

/*
 * The time we delay before retrying after an allocation
 * failure, in milliseconds
 */
#define	RETRY_DELAY 200

/*
 * The index field is for a fast lookup on the string.
 * A -1 tells stat_data_lookup() that we need to locate
 * the string.  Once stat_data_lookup() has located the
 * string, it will set the index of were we found it.
 */
struct cpu_states_info {
	char *field_name;
	int index;
	};

#define	CPU_STATES 4
static struct cpu_states_info cpu_states[CPU_STATES] = {
	{"cpu_ticks_idle", -1},
	{"cpu_ticks_user", -1},
	{"cpu_ticks_kernel", -1},
	{"cpu_ticks_wait", -1}
};

struct snapshot_state ss_gl_info = {
	NULL, NULL, 0, 0, 0, NULL, NULL, 0, NULL, 0, NULL
};

static kstat_t *
kstat_lookup_read(kstat_ctl_t *kc, char *module,
		int instance, char *name)
{
	kstat_t *ksp = kstat_lookup(kc, module, instance, name);
	if (ksp == NULL)
		return (NULL);
	if (kstat_read(kc, ksp, NULL) == -1)
		return (NULL);
	return (ksp);
}

/*
 * The snapshot has changed for whatever reason.  We need to regenerate
 * all information pertaining to the cpus including the record offsets.
 */
static int
generate_cpu_information(kstat_ctl_t *kc)
{
	size_t i;
	struct kstat_cpu_records *kstat_cpu_information;
	int max_cpus;

	max_cpus = sysconf(_SC_CPUID_MAX) + 1;

	if (ss_gl_info.kstat_cpu_information)
		free(ss_gl_info.kstat_cpu_information);
	if ((ss_gl_info.kstat_cpu_information =
	    (struct  kstat_cpu_records *)calloc(max_cpus,
	    sizeof (struct  kstat_cpu_records))) == NULL)
		return (errno);
	kstat_cpu_information = ss_gl_info.kstat_cpu_information;

	/*
	 * Walk the cpu snapshot chain, looking for the information we desire.
	 */

	ss_gl_info.kstat_cpus_active = 0;
	for (i = 0; i < max_cpus; i++) {
		kstat_cpu_information[i].cs_state = p_online(i, P_STATUS);
		/* If no valid CPU is present, move on to the next one */
		if (kstat_cpu_information[i].cs_state == -1) {
			kstat_cpu_information[i].cs_id = ID_NO_CPU;
			continue;
		}
		kstat_cpu_information[i].cs_id = i;

		if (!(CPU_ONLINE(kstat_cpu_information[i].cs_state)))
			continue;

		ss_gl_info.kstat_cpus_active++;

		if ((kstat_cpu_information[i].cpu_vm_ksp_ptr =
		    kstat_lookup(kc, "cpu", i, "vm")) == NULL)
			return (errno);

		if ((kstat_cpu_information[i].cpus_sys_ksp_ptr =
		    kstat_lookup(kc, "cpu", i, "sys")) == NULL)
			return (errno);

	}
	return (0);
}

/*
 * Note: the following helpers do not clean up on the failure case,
 * because it is left to the free_snapshot() in the acquire_snapshot()
 * failure path.
 */
static int
acquire_cpus(struct snapshot *ss, kstat_ctl_t *kc)
{
	size_t i;
	struct kstat_cpu_records *kstat_cpu_information = NULL;
	int rtc;

	ss->s_nr_cpus = sysconf(_SC_CPUID_MAX) + 1;

	if ((ss->s_cpus = calloc(ss->s_nr_cpus,
	    sizeof (struct cpu_snapshot))) == NULL)
		return (errno);

	/*
	 * If the snap has changed or we have not obainted the
	 * kstat_cpu_information, then go and get the cpu information.
	 */
	if ((ss->snap_changed || !ss_gl_info.kstat_cpu_information) &&
	    (rtc = generate_cpu_information(kc)) != 0)
			return (rtc);

	kstat_cpu_information = ss_gl_info.kstat_cpu_information;

	/*
	 * Read the data in that changes between the intervals.
	 */
	ss_gl_info.kstat_cpus_active = 0;
	for (i = 0; i < ss->s_nr_cpus; i++) {
		kstat_t *ksp;

		ss->s_cpus[i].cs_id = kstat_cpu_information[i].cs_id;
		ss->s_cpus[i].cs_state =  kstat_cpu_information[i].cs_state;
		/* If no valid CPU is present, move on to the next one */
		if (ss->s_cpus[i].cs_state == -1)
			continue;

		ss->s_cpus[i].cs_pset_id = kstat_cpu_information[i].cs_pset_id;

		if (!CPU_ACTIVE(&ss->s_cpus[i]))
			continue;
		ss_gl_info.kstat_cpus_active++;

		ksp = kstat_cpu_information[i].cpu_vm_ksp_ptr;
		if (kstat_read(kc, ksp, NULL) == -1)
			return (errno);
		if (kstat_copy(ksp, &ss->s_cpus[i].cs_vm))
			return (errno);

		ksp = kstat_cpu_information[i].cpus_sys_ksp_ptr;
		if (kstat_read(kc, ksp, NULL) == -1)
			return (errno);
		if (kstat_copy(ksp, &ss->s_cpus[i].cs_sys))
			return (errno);
	}

	errno = 0;
	return (errno);
}

static int
acquire_psets(struct snapshot *ss)
{
	int next_pset;
	struct pset_snapshot *ps;
	int psetidx, cpuidx;
	uint_t npsets, oldnpsets;
	psetid_t *psetlist;
	uint_t numcpus, cpus_in_set;
	processorid_t *cpus;

	/*
	 *  Assume no cpu sets presented
	 */
	for (cpuidx = 0; cpuidx < ss->s_nr_cpus; cpuidx++)
		ss->s_cpus[cpuidx].cs_pset_id = ID_NO_PSET;

	/*
	 * Obtain the number of psrsets, if 0, we are done.
	 * Careful, the psetlist count can change between
	 * when we get the number and the list allocation.
	 */
	for (;;) {
		if (pset_list(NULL, &npsets) != 0)
			return (errno);
		psetlist = malloc(sizeof (psetid_t) * npsets);
		if (psetlist == NULL)
			return (errno);

		if (ss->s_psets)
			SAFE_FREE(ss->s_psets, 1);
		ss->s_psets = calloc(npsets + 1,
		    sizeof (struct pset_snapshot));
		if (ss->s_psets == NULL) {
			SAFE_FREE(psetlist, sizeof (psetid_t));
			return (errno);
		}
		ss->s_nr_psets = npsets + 1;
		oldnpsets = npsets;
		(void) pset_list(psetlist, &npsets);

		if (npsets <= oldnpsets)
			break;
		SAFE_FREE(psetlist, sizeof (psetid_t));
		SAFE_FREE(ss->s_psets, sizeof (struct pset_snapshot));
	}

	numcpus = (uint_t)sysconf(_SC_NPROCESSORS_MAX);
	cpus = (processorid_t *)
	    calloc(numcpus, sizeof (processorid_t));
	if (!cpus) {
		SAFE_FREE(psetlist, sizeof (psetid_t));
		SAFE_FREE(ss->s_psets, sizeof (struct pset_snapshot));
		return (errno);
	}

	next_pset = 1;
	for (psetidx = 0; psetidx < npsets; psetidx++) {
		cpus_in_set = numcpus;
		ps = &ss->s_psets[next_pset];

		if (!ps->ps_cpus)
			ps->ps_cpus = calloc(ss->s_nr_cpus,
			    sizeof (struct cpu_snapshot *));
		else
			ps->ps_nr_cpus = 0;
		if (pset_info(psetlist[psetidx], NULL,
		    &cpus_in_set, cpus) != 0) {
			SAFE_FREE(ps->ps_cpus, sizeof (struct cpu_snapshot *));
			ps->ps_cpus = NULL;
			continue;
		}

		ps->ps_id = psetlist[psetidx];
		next_pset++;
		for (cpuidx = 0; cpuidx < cpus_in_set; cpuidx++) {
			ss->s_cpus[cpus[cpuidx]].cs_pset_id = psetlist[psetidx];
			ps->ps_cpus[ps->ps_nr_cpus++] =
			    &ss->s_cpus[cpus[cpuidx]];
		}
	}

	/* Now handle prset 0 */
	ps = &ss->s_psets[0];
	cpus_in_set = numcpus;
	if (pset_info(PS_NONE, NULL,  &cpus_in_set, cpus)) {
		/*
		 * We have no pset 0.  This can occur when
		 * dealing with zones and cpu pools.
		 * CR 7042161.  So simply set cpus_in_set to
		 * 0, do not error.
		 */
		cpus_in_set = 0;
	}

	if (!ps->ps_cpus)
		ps->ps_cpus = calloc(numcpus,
		    sizeof (struct cpu_snapshot *));
	else {
		ps->ps_nr_cpus = 0;
		ps->ps_id = 0;
	}

	if (ps->ps_cpus == NULL) {
		SAFE_FREE(cpus, sizeof (processorid_t));
		SAFE_FREE(psetlist, sizeof (psetid_t));
		return (errno);
	}
	for (cpuidx = 0; cpuidx < cpus_in_set; cpuidx++) {
		if (CPU_ACTIVE(&ss->s_cpus[cpus[cpuidx]]))
			ps->ps_cpus[ps->ps_nr_cpus++] =
			    &ss->s_cpus[cpus[cpuidx]];
	}

	SAFE_FREE(cpus, sizeof (processorid_t));
	SAFE_FREE(psetlist, sizeof (psetid_t));

	errno = 0;
	return (errno);
}
#define	INTR_INCR 512

static int
acquire_intrs(struct snapshot *ss, kstat_ctl_t *kc)
{
	kstat_t *ksp, **inter_ksp_ptr;
	size_t i = 1;
	kstat_t *sys_misc;
	kstat_named_t *clock;
	int inters_assume, j;
	kstat_intr_t *ki;

	if (ss->snap_changed || !ss_gl_info.sys_misc_ptr ||
	    !ss_gl_info.inter_ksp_ptr || !ss->s_intrs) {
		/*
		 * For what ever reason we need to regenerate the interrupt
		 * records.
		 */

		/* Do not forget the clock interrupt */
		ss->s_nr_intrs = 1;

		/* Assume INTR_INCR interrupts. */
		inters_assume = INTR_INCR;
		if (ss->s_intrs)
			SAFE_FREE(ss->s_intrs, sizeof (struct intr_snapshot));
		ss->s_intrs = (struct intr_snapshot *)calloc(inters_assume,
		    sizeof (struct intr_snapshot));
		if (ss->s_intrs == NULL)
			return (errno);
		if (ss_gl_info.inter_ksp_ptr)
			SAFE_FREE(ss_gl_info.inter_ksp_ptr,
			    sizeof (kstat_t **));
		inter_ksp_ptr = ss_gl_info.inter_ksp_ptr = (kstat_t **)
		    calloc(inters_assume, sizeof (kstat_t *));
		if (!ss_gl_info.inter_ksp_ptr)
			return (errno);

		/*
		 * Walk the kc chain looking for interrupts.  If we
		 * find a record, save the ksp record address for
		 * future use.
		 */
		for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
			if (ksp->ks_type != KSTAT_TYPE_INTR)
				continue;
			if (ss->s_nr_intrs == inters_assume) {
				/* Need more entries to work with. */
				inters_assume += INTR_INCR;
				inter_ksp_ptr = ss_gl_info.inter_ksp_ptr =
				    (kstat_t **)
				    realloc(ss_gl_info.inter_ksp_ptr,
				    inters_assume * sizeof (kstat_t *));
				if (!ss_gl_info.inter_ksp_ptr)
					goto error_out;
				ss->s_intrs = realloc(ss->s_intrs,
				    inters_assume *
				    sizeof (struct intr_snapshot));
				if (!ss->s_intrs)
					goto error_out;
				bzero(&ss->s_intrs[ss->s_nr_intrs],
				    INTR_INCR * sizeof (struct intr_snapshot));
			}
			if (kstat_read(kc, ksp, NULL) == -1)
				goto error_out;

			ss->s_nr_intrs++;
			(void) strlcpy(ss->s_intrs[i].is_name, ksp->ks_name,
			    KSTAT_STRLEN);
			ss->s_intrs[i].is_total = 0;

			ki = KSTAT_INTR_PTR(ksp);
			for (j = 0; j < KSTAT_NUM_INTRS; j++)
				ss->s_intrs[i].is_total += ki->intrs[j];
			inter_ksp_ptr[i] = ksp;
			i++;
		}
		sys_misc = kstat_lookup_read(kc, "unix", 0, "system_misc");
		if (sys_misc == NULL) {
			ss_gl_info.sys_misc_ptr = NULL;
			goto error_out;
		}
		ss_gl_info.sys_interrupts = ss->s_nr_intrs;
	} else {
		inter_ksp_ptr = ss_gl_info.inter_ksp_ptr;
		ss->s_nr_intrs = ss_gl_info.sys_interrupts;
		sys_misc = ss_gl_info.sys_misc_ptr;

		/*
		 * We use the inter_ksp_ptr list to get us quickly
		 * to the interrupt record we are interested in obtaining
		 * data for.
		 */
		for (; i < ss_gl_info.sys_interrupts; i++) {
			kstat_intr_t *ki;
			int j;

			ksp = inter_ksp_ptr[i];
			if (kstat_read(kc, ksp, NULL) == -1)
				return (errno);

			ki = KSTAT_INTR_PTR(ksp);

			(void) strlcpy(ss->s_intrs[i].is_name, ksp->ks_name,
			    KSTAT_STRLEN);
			ss->s_intrs[i].is_total = 0;

			for (j = 0; j < KSTAT_NUM_INTRS; j++)
				ss->s_intrs[i].is_total += ki->intrs[j];
			i++;
		}
	}


	clock = (kstat_named_t *)kstat_data_lookup(sys_misc, "clk_intr");
	if (clock == NULL)
		return (errno);

	(void) strlcpy(ss->s_intrs[0].is_name, "clock", KSTAT_STRLEN);
	ss->s_intrs[0].is_total = clock->value.ui32;

	errno = 0;
	return (errno);

error_out:
	if (ss_gl_info.inter_ksp_ptr)
		SAFE_FREE(ss_gl_info.inter_ksp_ptr, sizeof (kstat_t **));
	ss_gl_info.inter_ksp_ptr = NULL;
	if (ss->s_intrs)
		SAFE_FREE(ss->s_intrs, sizeof (struct intr_snapshot *));
	ss->s_intrs = NULL;
	return (errno);
}

int
acquire_sys(struct snapshot *ss, kstat_ctl_t *kc)
{
	size_t i;
	kstat_named_t *knp;
	kstat_t *ksp;

	if ((ksp = kstat_lookup(kc, "unix", 0, "sysinfo")) == NULL)
		return (errno);

	if (kstat_read(kc, ksp, &ss->s_sys.ss_sysinfo) == -1)
		return (errno);

	if ((ksp = kstat_lookup(kc, "unix", 0, "vminfo")) == NULL)
		return (errno);

	if (kstat_read(kc, ksp, &ss->s_sys.ss_vminfo) == -1)
		return (errno);

	if ((ksp = kstat_lookup(kc, "unix", 0, "dnlcstats")) == NULL)
		return (errno);

	if (kstat_read(kc, ksp, &ss->s_sys.ss_nc) == -1)
		return (errno);

	if ((ksp = kstat_lookup(kc, "unix", 0, "system_misc")) == NULL)
		return (errno);

	if (kstat_read(kc, ksp, NULL) == -1)
		return (errno);

	knp = (kstat_named_t *)kstat_data_lookup(ksp, "clk_intr");
	if (knp == NULL)
		return (errno);

	ss->s_sys.ss_ticks = knp->value.l;

	knp = (kstat_named_t *)kstat_data_lookup(ksp, "deficit");
	if (knp == NULL)
		return (errno);

	ss->s_sys.ss_deficit = knp->value.l;

	for (i = 0; i < ss->s_nr_cpus; i++) {
		if (!CPU_ACTIVE(&ss->s_cpus[i]))
			continue;

		if (kstat_add(&ss->s_cpus[i].cs_sys, &ss->s_sys.ss_agg_sys))
			return (errno);
		if (kstat_add(&ss->s_cpus[i].cs_vm, &ss->s_sys.ss_agg_vm))
			return (errno);
	}

	errno = 0;
	return (errno);
}

struct snapshot *
acquire_snapshot(kstat_ctl_t **kc_passed, int types,
    struct iodev_filter *iodev_filter)
{
	struct snapshot *ss = NULL;
	kstat_ctl_t *kc = *kc_passed;
	int err;

retry:
	err = 0;
	/* ensure any partial resources are freed on a retry */
	free_snapshot(ss);

	ss = safe_alloc(sizeof (struct snapshot));

	(void) memset(ss, 0, sizeof (struct snapshot));

	ss->s_types = types;

	/* wait for a possibly up-to-date chain */
	while ((ss->snap_changed = kstat_chain_update(kc)) == -1) {
		if (errno == EAGAIN)
			(void) poll(NULL, 0, RETRY_DELAY);
		else
			fail(1, "kstat_chain_update failed");
	}

	if (!err && (types & SNAP_INTERRUPTS))
		err = acquire_intrs(ss, kc);

	if (!err && (types & (SNAP_CPUS | SNAP_SYSTEM | SNAP_PSETS)))
		err = acquire_cpus(ss, kc);

	if (!err && (types & SNAP_PSETS))
		err = acquire_psets(ss);

	if (!err && (types & (SNAP_IODEVS | SNAP_CONTROLLERS |
	    SNAP_IOPATHS_LI | SNAP_IOPATHS_LTI)))
		err = acquire_iodevs(ss, kc, iodev_filter);

	if (!err && (types & SNAP_SYSTEM))
		err = acquire_sys(ss, kc);

	switch (err) {
		case 0:
			break;
		case EAGAIN:
			(void) poll(NULL, 0, RETRY_DELAY);
		/* a kstat disappeared from under us */
		/*FALLTHRU*/
		case ENXIO:
		case ENOENT:
			goto retry;
		default:
			fail(1, "acquiring snapshot failed");
	}

	return (ss);
}

void
free_snapshot(struct snapshot *ss)
{
	size_t i;
	int j;

	if (ss == NULL)
		return;

	if (ss->s_cpus) {
		for (i = 0; i < ss->s_nr_cpus; i++) {
			SAFE_FREE(ss->s_cpus[i].cs_vm.ks_data,
			    sizeof (ss->s_cpus[i].cs_vm.ks_data));
			SAFE_FREE(ss->s_cpus[i].cs_sys.ks_data,
			    sizeof (ss->s_cpus[i].cs_sys.ks_data));
		}
		SAFE_FREE(ss->s_cpus, sizeof (struct cpu_snapshot *));
	}

	while (ss->s_iodevs) {
		struct iodev_snapshot *tmp = ss->s_iodevs;
		ss->s_iodevs = ss->s_iodevs->is_next;
		free_iodev(tmp);
	}

	if (ss->s_psets) {
		for (j = 0; j < ss->s_nr_psets; j++)
			SAFE_FREE(ss->s_psets[j].ps_cpus,
			    sizeof (struct cpu_snapshot **));
		SAFE_FREE(ss->s_psets, sizeof (struct pset_snapshot *));
	}

	SAFE_FREE(ss->s_sys.ss_agg_sys.ks_data,
	    sizeof (ss->s_sys.ss_agg_sys.ks_data));
	SAFE_FREE(ss->s_sys.ss_agg_vm.ks_data,
	    sizeof (ss->s_sys.ss_agg_vm.ks_data));
	SAFE_FREE(ss, sizeof (struct snapshot));
}

kstat_ctl_t *
open_kstat(void)
{
	kstat_ctl_t *kc;

	while ((kc = kstat_open()) == NULL) {
		if (errno == EAGAIN)
			(void) poll(NULL, 0, RETRY_DELAY);
		else
			fail(1, "kstat_open failed");
	}

	return (kc);
}

void *
safe_alloc(size_t size)
{
	void *ptr;

	while ((ptr = malloc(size)) == NULL) {
		if (errno == EAGAIN)
			(void) poll(NULL, 0, RETRY_DELAY);
		else
			fail(1, "malloc failed");
	}
	return (ptr);
}

char *
safe_strdup(const char *str)
{
	char *ret;

	if (str == NULL)
		return (NULL);

	while ((ret = strdup(str)) == NULL) {
		if (errno == EAGAIN)
			(void) poll(NULL, 0, RETRY_DELAY);
		else
			fail(1, "malloc failed");
	}
	return (ret);
}

uint64_t
kstat_delta(kstat_t *old, kstat_t *new, char *name, int *index)
{
	kstat_named_t *knew = stat_data_lookup(new, name, index);
	if (old && old->ks_data) {
		kstat_named_t *kold = stat_data_lookup(old, name, index);
		return (knew->value.ui64 - kold->value.ui64);
	}
	return (knew->value.ui64);
}

int
kstat_copy(const kstat_t *src, kstat_t *dst)
{
	*dst = *src;

	if (src->ks_data != NULL) {
		if ((dst->ks_data = malloc(src->ks_data_size)) == NULL)
			return (-1);
		bcopy(src->ks_data, dst->ks_data, src->ks_data_size);
	} else {
		dst->ks_data = NULL;
		dst->ks_data_size = 0;
	}
	return (0);
}

int
kstat_add(const kstat_t *src, kstat_t *dst)
{
	size_t i;
	kstat_named_t *from;
	kstat_named_t *to;

	if (dst->ks_data == NULL)
		return (kstat_copy(src, dst));

	from = src->ks_data;
	to = dst->ks_data;

	for (i = 0; i < src->ks_ndata; i++) {
		/* "addition" makes little sense for strings */
		if (from->data_type != KSTAT_DATA_CHAR &&
		    from->data_type != KSTAT_DATA_STRING)
			(to)->value.ui64 += (from)->value.ui64;
		from++;
		to++;
	}

	return (0);
}

uint64_t
cpu_ticks_delta(kstat_t *old, kstat_t *new)
{
	uint64_t ticks = 0;
	size_t i;

	for (i = 0; i < CPU_STATES; i++) {
		ticks += kstat_delta(old, new, cpu_states[i].field_name,
		    &cpu_states[i].index);
	}
	return (ticks);
}

int
nr_active_cpus()
{
	return (ss_gl_info.kstat_cpus_active);
}

/*
 * Return the number of ticks delta between two hrtime_t
 * values. Attempt to cater for various kinds of overflow
 * in hrtime_t - no matter how improbable.
 */
uint64_t
hrtime_delta(hrtime_t old, hrtime_t new)
{
	uint64_t del;

	if ((new >= old) && (old >= 0L))
		return (new - old);
	else {
		/*
		 * We've overflowed the positive portion of an
		 * hrtime_t.
		 */
		if (new < 0L) {
			/*
			 * The new value is negative. Handle the
			 * case where the old value is positive or
			 * negative.
			 */
			uint64_t n1;
			uint64_t o1;

			n1 = -new;
			if (old > 0L)
				return (n1 - old);
			else {
				o1 = -old;
				del = n1 - o1;
				return (del);
			}
		} else {
			/*
			 * Either we've just gone from being negative
			 * to positive *or* the last entry was positive
			 * and the new entry is also positive but *less*
			 * than the old entry. This implies we waited
			 * quite a few days on a very fast system between
			 * iostat displays.
			 */
			if (old < 0L) {
				uint64_t o2;

				o2 = -old;
				del = UINT64_MAX - o2;
			} else {
				del = UINT64_MAX - old;
			}
			del += new;
			return (del);
		}
	}
}

/*
 * If index_ptr integer value is > -1 then the index points to the
 * string entry in the ks_data that we are interested in. Otherwise
 * we will need to walk the array.
 */
void *
stat_data_lookup(kstat_t *ksp, char *name, int *index_ptr)
{
	int i;
	int size;
	int index;
	char *namep, *datap;

	switch (ksp->ks_type) {
		case KSTAT_TYPE_NAMED:
			size = sizeof (kstat_named_t);
			namep = KSTAT_NAMED_PTR(ksp)->name;
			break;
		case KSTAT_TYPE_TIMER:
			size = sizeof (kstat_timer_t);
			namep = KSTAT_TIMER_PTR(ksp)->name;
			break;
		default:
			errno = EINVAL;
			return (NULL);
	}

	index = *index_ptr;
	if (index >= 0) {
		/* Short cut to the information. */
		datap = ksp->ks_data;
		datap = &datap[size*index];
		return (datap);
	}

	/* Need to go find the string. */
	datap = ksp->ks_data;
	for (i = 0; i < ksp->ks_ndata; i++) {
		if (strcmp(name, namep) == 0) {
			*index_ptr = i;
			return (datap);
		}
		namep += size;
		datap += size;
	}
	errno = ENOENT;
	return (NULL);
}

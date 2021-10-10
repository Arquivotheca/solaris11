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
 * Copyright (c) 2011,  Intel Corporation.
 * All Rights Reserved.
 */

/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * ACPI Processor Aggregator Device driver.
 */

#include <sys/x86_archext.h>
#include <sys/machsystm.h>
#include <sys/archsystm.h>
#include <sys/x_call.h>
#include <sys/acpi/acpi.h>
#include <sys/acpica.h>
#include <sys/cpupm_pur.h>
#include <sys/cpu_acpi.h>
#include <sys/cpupm.h>
#include <sys/dtrace.h>
#include <sys/callb.h>
#include <sys/spl.h>
#include <sys/sdt.h>
#include <sys/var.h>

#define	CPUPM_PUR_STATUS_TRUE	0
#define	CPUPM_PUR_STATUS_FALSE	1

#define	CPUPM_PUR_ONLINE	0
#define	CPUPM_PUR_OFFLINE	1

/*
 * ACPI4.0 introduces the optional Processor Aggregator device.
 * The Processor Aggregator Device provides a control point that
 * enables the platform to perform specific processor configuration
 * and control that applies to all processors in the platform.
 *
 * The Plug and Play ID of the Processor Aggregator Device is ACPI000C.
 *
 * In order to reduce the platform's power consumption, the platform
 * may direct OSPM to remove a logical processor from the operating
 * system scheduler's list of processors where non-processor affinitized
 * work is dispathced. This capability is known as Logical Processor
 * Idling and provides a means to reduce platform power consumption
 * without undergoing processor ejection / insertion processing overhead.
 * Interrupts directed to a logical processor and processor affinitized
 * workloads will impede the effectiveness of logical processor idling
 * in reducing power consumption as OSPM is not expected to retarget this
 * work when a logical processor is idled.
 */
static int pur_init(cpu_t *);
static void pur_fini(cpu_t *);
static void pur_stop(cpu_t *);

/*
 * Interfaces for modules implementing Processor Aggregator Device support
 */
cpupm_state_ops_t pur_ops = {
	"Processor Aggregator Device driver",
	pur_init,
	pur_fini,
	NULL,
	pur_stop
};

/*
 * supported CPU topology type
 */
typedef enum {
	CPUPM_PUR_SOCKET,
	CPUPM_PUR_CORE,
	CPUPM_TYPE_NUM
} cpupm_topology_type_t;

/*
 * CPU topology data structure
 */
typedef struct cpupm_topology_unit {
	struct cpupm_topology_unit	*next;
	struct cpupm_topology_unit	*children;
	uint32_t			unit_type;
	uint32_t			unit_id;
	uint32_t			num_cpus;
	uint32_t			num_offline;
	cpuset_t			cpuset;
} cpupm_topology_unit_t;

static uint32_t		cpupm_pur_idling(uint32_t *);
static uint32_t		cpupm_pur_idling_nolock(uint32_t *);
static uint32_t		cpupm_pur_online(uint32_t);
static uint32_t		cpupm_pur_offline(uint32_t);

static void		cpupm_topology_alloc(cpu_t *);
static void		cpupm_topology_remove(cpu_t *);
static void		cpupm_topology_free(cpu_t *);
static void		cpupm_topology_sort(cpupm_topology_unit_t *, int);
static void		cpupm_topology_update(void);

cpupm_topology_unit_t	*cpupm_pur_topology = NULL;
extern cpuset_t cpu_ready_set;

/*
 * If non-zero, pur will not be supported.
 */
int cpupm_no_pur = 0;

/*
 * Build CPU topology
 * OSPM uses internal logical processor to physical core and package
 * topology knowledge to idle logical processors successively in an
 * order that maximizes power reduction benefit from idling requests.
 */
static void
cpupm_topology_alloc(cpu_t *cp)
{
	cpupm_topology_type_t	type;
	cpupm_topology_unit_t	**unit_list, *unit_p;
	uint32_t		unit_id;

	ASSERT(MUTEX_HELD(&cpu_lock));

	unit_list = &cpupm_pur_topology;

	for (type = CPUPM_PUR_SOCKET; type < CPUPM_TYPE_NUM; type++) {
		switch (type) {
		case CPUPM_PUR_SOCKET:
			unit_id = cpuid_get_chipid(cp);
			break;
		case CPUPM_PUR_CORE:
			unit_id = cpuid_get_coreid(cp);
			break;
		default:
			return;
		}

		/*
		 * search the matched existing topology unit
		 */
		for (unit_p = *unit_list; unit_p; unit_p = unit_p->next) {
			if (unit_p->unit_id == unit_id) {
				break;
			}
		}

		/*
		 * create a new topology unit if no found
		 */
		if (unit_p == NULL) {
			unit_p = kmem_zalloc(sizeof (cpupm_topology_unit_t),
			    KM_SLEEP);
			unit_p->unit_id = unit_id;
			unit_p->unit_type = type;
			unit_p->num_cpus = 0;
			unit_p->num_offline = 0;
			unit_p->next = *unit_list;
			*unit_list = unit_p;
			unit_p->children = NULL;
			CPUSET_ZERO(unit_p->cpuset);
		}

		CPUSET_ADD(unit_p->cpuset, cp->cpu_id);
		unit_p->num_cpus++;
		unit_list = &unit_p->children;	/* go to the next level */
	}
}

/*
 * Remove CPU from its topology unit list
 */
static void
cpupm_topology_remove(cpu_t *cp)
{
	cpupm_topology_type_t   type;
	cpupm_topology_unit_t   **unit_list, *unit_p;
	uint32_t		unit_id;

	ASSERT(MUTEX_HELD(&cpu_lock));

	unit_list = &cpupm_pur_topology;

	for (type = CPUPM_PUR_SOCKET; type < CPUPM_TYPE_NUM; type++) {
		switch (type) {
			case CPUPM_PUR_SOCKET:
				unit_id = cpuid_get_chipid(cp);
				break;
			case CPUPM_PUR_CORE:
				unit_id = cpuid_get_coreid(cp);
				break;
			default:
				return;
		}

		for (unit_p = *unit_list; unit_p; unit_p = unit_p->next) {
			if (unit_p->unit_id == unit_id) {
				break;
			}
		}

		if (unit_p == NULL) {
			return;
		}

		if (CPU_IN_SET(unit_p->cpuset, cp->cpu_id))
			CPUSET_DEL(unit_p->cpuset, cp->cpu_id);

		unit_p->num_cpus--;
		unit_list = &unit_p->children;
	}
}

/*
 * Free the resource of CPU topology list
 */
static void
cpupm_topology_free(cpu_t *cp)
{
	_NOTE(ARGUNUSED(cp));
	cpupm_topology_unit_t	*this_unit, *next_unit;

	ASSERT(MUTEX_HELD(&cpu_lock));

	this_unit = cpupm_pur_topology;

	while (this_unit != NULL) {
		cpupm_topology_unit_t	*this_child, *next_child;

		this_child = this_unit->children;
		while (this_child != NULL) {

			next_child = this_child->next;
			kmem_free((void *)this_child,
			    sizeof (cpupm_topology_unit_t));
			this_child = next_child;
		}
		this_unit->children = NULL;
		next_unit = this_unit->next;
		kmem_free((void *)this_unit, sizeof (cpupm_topology_unit_t));
		this_unit = next_unit;
	}

	cpupm_pur_topology = NULL;
}

/*
 * CPU topology list sort in an order by offline CPU number
 */
static void
cpupm_topology_sort_ops(cpupm_topology_unit_t *unit_p, int flag)
{
	cpupm_topology_unit_t *unit_q, *unit_key, unit_swap;

	ASSERT(MUTEX_HELD(&cpu_lock));

	while (unit_p != NULL) {
		unit_key = unit_p;
		unit_q = unit_p->next;
		if (flag == CPUPM_PUR_OFFLINE) {
			while (unit_q != NULL) {
				if (unit_q->num_offline >
				    unit_key->num_offline)
					unit_key = unit_q;
				unit_q = unit_q->next;
			}
		} else if (flag == CPUPM_PUR_ONLINE) {
			while (unit_q != NULL) {
				if (unit_q->num_offline <
				    unit_key->num_offline)
					unit_key = unit_q;
				unit_q = unit_q->next;
			}
		}
		if (unit_key != unit_p) {
			cpupm_topology_unit_t *swap_next;
			(void) memcpy(&unit_swap, unit_p,
			    sizeof (cpupm_topology_unit_t));
			(void) memcpy(unit_p, unit_key,
			    sizeof (cpupm_topology_unit_t));
			(void) memcpy(unit_key, &unit_swap,
			    sizeof (cpupm_topology_unit_t));
			swap_next = unit_p->next;
			unit_p->next = unit_key->next;
			unit_key->next = swap_next;
		}
		unit_p = unit_p->next;
	}
}

/*
 * Sort both socket and core topology by offline CPU number
 */
static void
cpupm_topology_sort(cpupm_topology_unit_t *unit_p, int flag)
{
	cpupm_topology_unit_t	*unit_child;

	ASSERT(MUTEX_HELD(&cpu_lock));

	cpupm_topology_sort_ops(unit_p, flag);

	while (unit_p != NULL) {
		unit_child = unit_p->children;
		cpupm_topology_sort_ops(unit_child, flag);
		unit_p = unit_p->next;
	}
}

/*
 * Update offline CPU number in all CPU topology units
 */
static void
cpupm_topology_update(void)
{
	cpupm_topology_unit_t   *unit_p, *unit_child;
	cpuset_t		unit_cpuset;
	processorid_t		cpu_id;
	cpu_t			*cp;
	int			result = 0;

	ASSERT(MUTEX_HELD(&cpu_lock));

	unit_p = cpupm_pur_topology;
	while (unit_p != NULL) {
		unit_p->num_offline = 0;
		unit_child = unit_p->children;
		while (unit_child != NULL) {
			unit_child->num_offline = 0;
			unit_cpuset = unit_child->cpuset;
			do {
				CPUSET_FIND(unit_cpuset, cpu_id);
				if (cpu_id == CPUSET_NOTINSET)
					break;
				if (!CPU_IN_SET(cpu_ready_set, (cpu_id)))
					break;
				ASSERT(cpu_id >= 0 && cpu_id < NCPU);
				cp = cpu[cpu_id];
				if (!CPU_ACTIVE(cp)) {
					unit_child->num_offline++;
					unit_p->num_offline++;
				}
				CPUSET_ATOMIC_XDEL(unit_cpuset, cpu_id, result);
			} while (result == 0);
			unit_child = unit_child->next;
		}
		unit_p = unit_p->next;
	}
}

/*
 * Increment the number of logical processors placed in the idle
 * state to equal the argument as possible.
 * Return the number of successful operations.
 */
static uint32_t
cpupm_pur_offline(uint32_t num)
{
	cpupm_topology_unit_t *unit_p, *unit_child;
	cpu_t *cp;
	processorid_t	cpu_id;
	int cpu_count = 0;
	int result = 0;

	ASSERT(MUTEX_HELD(&cpu_lock));

	unit_p = cpupm_pur_topology;
	cpupm_topology_sort(unit_p, CPUPM_PUR_OFFLINE);

	for (unit_p = cpupm_pur_topology; unit_p;
	    unit_p = unit_p->next) {
		if (unit_p->num_offline == unit_p->num_cpus)
			continue;
		for (unit_child = unit_p->children; unit_child;
		    unit_child = unit_child->next) {
			cpuset_t unit_cpuset = unit_child->cpuset;

			do {
				CPUSET_FIND(unit_cpuset, cpu_id);
				if (cpu_id == CPUSET_NOTINSET)
					break;
				if (!CPU_IN_SET(cpu_ready_set, (cpu_id)))
					break;
				ASSERT(cpu_id >= 0 && cpu_id < NCPU);
				cp = cpu[cpu_id];
				if (CPU_ACTIVE(cp) &&
				    (cpu_offline(cp, 0) == 0)) {
					unit_child->num_offline++;
					unit_p->num_offline++;
					ASSERT(cp->cpu_flags & CPU_OFFLINE);
					cp->cpu_flags |= CPU_POWERIDLE;
					cpu_set_state(cp);
					if (++cpu_count == num)
						return (cpu_count);
				}
				CPUSET_ATOMIC_XDEL(unit_cpuset, cpu_id, result);
			} while (result == 0);
		}
	}
	return (cpu_count);
}

/*
 * Decrement the number of logical processors placed in the idle
 * state to equal the argument as possible.
 * Return the number of successful operations.
 */
static uint32_t
cpupm_pur_online(uint32_t num)
{
	cpupm_topology_unit_t *unit_p, *unit_child;
	cpu_t *cp;
	processorid_t cpu_id;
	int cpu_count = 0;
	int result = 0;

	ASSERT(MUTEX_HELD(&cpu_lock));

	unit_p = cpupm_pur_topology;
	cpupm_topology_sort(unit_p, CPUPM_PUR_ONLINE);

	for (unit_p = cpupm_pur_topology; unit_p;
	    unit_p = unit_p->next) {
		if (unit_p->num_offline == 0)
			continue;
		for (unit_child = unit_p->children; unit_child;
		    unit_child = unit_child->next) {
			cpuset_t unit_cpuset = unit_child->cpuset;

			do {
				CPUSET_FIND(unit_cpuset, cpu_id);
				if (cpu_id == CPUSET_NOTINSET)
					break;
				if (!CPU_IN_SET(cpu_ready_set, (cpu_id)))
					break;
				ASSERT(cpu_id >= 0 && cpu_id < NCPU);
				cp = cpu[cpu_id];
				if (!CPU_ACTIVE(cp) &&
				    (cpu_online(cp) == 0)) {
					unit_child->num_offline--;
					unit_p->num_offline--;
					cp->cpu_flags &= ~CPU_POWERIDLE;
					cpu_set_state(cp);
					if (++cpu_count == num)
						return (cpu_count);
				}
				CPUSET_ATOMIC_XDEL(unit_cpuset, cpu_id, result);
			} while (result == 0);
		}
	}
	return (cpu_count);
}

/*
 * Processor utilization request notification handler
 */
void
cpupm_pur_notification(void *ctx)
{
	cpu_t			*cp = ctx;
	cpupm_mach_state_t	*mach_state =
	    (cpupm_mach_state_t *)cp->cpu_m.mcpu_pm_mach_state;
	cpu_acpi_handle_t	handle;
	uint32_t		status, num_pur;

	if (mach_state == NULL)
		return;

	handle = mach_state->ms_acpi_handle;
	ASSERT(handle != NULL);

	if (cpu_acpi_pur(handle) == CPUPM_PUR_STATUS_FALSE)
		return;

	/*
	 * Obtain the number of processor requested to put in idle
	 */
	num_pur = CPU_ACPI_PUR_NUM(handle);
	/*
	 * Place the processor in idle state, here num_pur will
	 * return the successful number and as an argument to evaluate
	 * _OST object.
	 */
	status = cpupm_pur_idling(&num_pur);
	/*
	 * Convey _PUR evaluation status to the platform
	 */

	cpu_acpi_ost(status, num_pur);
}

/*
 * Place the number of processors in an idle state.
 * Return 0 if the action is successful, otherwise, return 1 to indicate
 * no action was performed.
 */
static uint32_t
cpupm_pur_idling(uint32_t *num_pur)
{
	uint32_t ret;

	if (MUTEX_HELD(&cpu_lock)) {
		ret = cpupm_pur_idling_nolock(num_pur);
	} else {
		mutex_enter(&cpu_lock);
		ret = cpupm_pur_idling_nolock(num_pur);
		mutex_exit(&cpu_lock);
	}
	return (ret);
}

/*
 * Place the number of processors in an idle state, no lock protection
 */
static uint32_t
cpupm_pur_idling_nolock(uint32_t *num_pur)
{
	uint32_t requested_cpus = *num_pur;
	uint32_t cur_cpus;

	cur_cpus = ncpus - ncpus_online;
	ASSERT(cur_cpus >= 0);

	cpupm_topology_update();

	if (requested_cpus == cur_cpus) {
		return (CPUPM_PUR_STATUS_FALSE);
	} else if (requested_cpus > cur_cpus) {
		uint32_t num = requested_cpus - cur_cpus;
		*num_pur = cpupm_pur_offline(num);
	} else {
		uint32_t num = cur_cpus - requested_cpus;
		*num_pur = cur_cpus - cpupm_pur_online(num);
	}
	return (CPUPM_PUR_STATUS_TRUE);
}

static int
pur_init(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);
	cpu_acpi_handle_t handle;
	int	ret = 1;

	if (cpupm_no_pur)
		return (ret);

	if (mach_state == NULL)
		return (ret);

	handle = mach_state->ms_acpi_handle;
	ASSERT(handle != NULL);

	ret = cpu_acpi_pur(handle);
	if (ret == 0) {
		if (MUTEX_HELD(&cpu_lock)) {
			cpupm_topology_alloc(cp);
		} else {
			mutex_enter(&cpu_lock);
			cpupm_topology_alloc(cp);
			mutex_exit(&cpu_lock);
		}
	}

	return (ret);
}

static void
pur_fini(cpu_t *cp)
{
	if (MUTEX_HELD(&cpu_lock)) {
		cpupm_topology_free(cp);
	} else {
		mutex_enter(&cpu_lock);
		cpupm_topology_free(cp);
		mutex_exit(&cpu_lock);
	}
}

static void
pur_stop(cpu_t *cp)
{
	if (MUTEX_HELD(&cpu_lock)) {
		cpupm_topology_remove(cp);
	} else {
		mutex_enter(&cpu_lock);
		cpupm_topology_remove(cp);
		mutex_exit(&cpu_lock);
	}
}

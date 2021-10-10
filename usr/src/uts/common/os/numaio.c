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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include <sys/sysmacros.h>
#include <sys/ksynch.h>
#include <sys/list.h>
#include <sys/lgrp.h>
#include <sys/numaio.h>
#include <sys/numaio_priv.h>
#include <sys/cpuvar.h>
#include <sys/pool.h>
#include <sys/pool_pset.h>
#include <sys/sunndi.h>
#include <sys/pghw.h>
#include <sys/balance.h>
#include <sys/sdt.h>

kmem_cache_t	*i_numaio_obj_cachep;
kmem_cache_t	*i_numaio_grp_cachep;

/*
 * numaio_bind_objects: tunable to turn off the action of binding a thread
 * or an interrupt to a CPU. By default, binding is enabled.
 */
boolean_t numaio_bind_objects = B_TRUE;

#define	afo_kthread		afo_object.kthread
#define	afo_dip			afo_object.dip
#define	afo_intr		afo_object.intr
#define	afo_proc		afo_object.cookie

static list_t numaio_object_glist;
static list_t numaio_group_glist;

/*
 * When numaio_group_map() is called, it first reserves CPUs (using
 * numaio_reserve_excl_cpus()) in the constraint for objects
 * requesting dedicated CPUs. Those CPUs will have
 * AFC_CPU_EXCL_RESERVED bit set and nc_dedi_count set to a value
 * that will indicate the number of dedicated objects on that CPU.
 */
#define	AFF_RESET_RESERVE(aff_cpup)			\
	(aff_cpup->nc_flags &= ~AFC_CPU_EXCL_RESERVED)

#define	AFF_EXCL_RESERVED(aff_cpup)			\
	(aff_cpup->nc_flags & AFC_CPU_EXCL_RESERVED)

/* ARGSUSED */
static void
numaio_increment_cpu(numaio_object_t *obj, cpu_t *cp)
{
	/* obj is passed here for tracing purposes */
	ASSERT(MUTEX_HELD(&cpu_lock));
	cp->cpu_numaio_info->ni_cpu_refcnt++;
	cp->cpu_lpl->lpl_lgrp->lgrp_numaio_refs++;
}

/* ARGSUSED */
static void
numaio_decrement_cpu(numaio_object_t *obj, cpu_t *cp)
{
	/* obj is passed here for tracing purposes */
	ASSERT(MUTEX_HELD(&cpu_lock));
	cp->cpu_numaio_info->ni_cpu_refcnt--;
	cp->cpu_lpl->lpl_lgrp->lgrp_numaio_refs--;
}

/*
 * affinity generation number. Used to avoid repeatedly setting affinity
 * on an object for which affinity had already been set.
 */
static uint_t numaio_gen_num = 0;

/*
 * Affinity between a pair of objects. An affinity instance is linked
 * in the lists of numaio_affinity_link_t's of two objects.
 */
typedef struct numaio_affinity_s {
	numaio_object_t		*afa_obj1;
	numaio_object_t		*afa_obj2;
	long			afa_strength;
} numaio_affinity_t;

typedef struct numaio_affinity_link_s {
	numaio_affinity_t	*afl_affinity;
	list_node_t		afl_next;
} numaio_affinity_link_t;

/*
 * NUMA IO information associated with a device.
 */
typedef struct numaio_dev_info_s {
	uint_t		niodev_nlgrps;	/* # of lgrps for devices */
	lgrp_t		**niodev_lgrps;	/* array of lgrps */
	uint_t		niodev_alloc_nlgrps;
	int		niodev_index;	/* round robin index */
} numaio_dev_info_t;

/*
 * A single RW lock should be sufficient for now since it is only
 * used for control operations.
 */
static krwlock_t numaio_glock;

/*
 * Prototypes for local static functions
 */
static numaio_affinity_t *numaio_create_affinity(void);
static void numaio_destroy_affinity(numaio_affinity_t *);
static int numaio_set_lgrp_constraint(numaio_constraint_t *, lgrp_id_t *, int);
static void numaio_bind_two_objects(numaio_object_t *, numaio_object_t *,
    numaio_affinity_t *);
static void numaio_group_map_locked(numaio_group_t *);
static void numaio_object_bind_async(numaio_object_t *, processorid_t);
static void i_numaio_get_effective_cpus(numaio_group_t *, processorid_t *,
    size_t, size_t *);
static void numaio_object_unbind(numaio_object_t *);
static void numaio_clear_affinity_locked(numaio_object_t *, numaio_object_t *,
    uint_t);

/*
 * Affinity objects manipulation.
 *
 * Each resource that needs to be bound is associated with an
 * affinity object. Affinity objects are created by the
 * numaio_object_create*() calls.
 *
 * The caller can associate each affinity object with an optional
 * name, which can be used for debugging and observability.
 */

/*
 * Create an affinity object node that is not directly tied to a
 * particular resource. This type of object can be used to indirectly
 * define affinities between objects.
 */
static numaio_object_t *
numaio_object_create(const char *name, uint_t flags)
{
	numaio_object_t *obj;

	obj = kmem_cache_alloc(i_numaio_obj_cachep, KM_SLEEP);

	if (name != NULL)
		(void) strlcpy(obj->afo_name, name, sizeof (obj->afo_name));

	obj->afo_cpuid = -1;
	obj->afo_cpuid_saved = -1;

	obj->afo_flags = 0;

	/*
	 * If two objects having NUMAIO_AFF_STRENGTH_CPU affinity
	 * between them request for a dedicated CPU, then an extra CPU
	 * will end up getting reserved. The caller should specify a
	 * dedicated CPU for only one of the created objets in this
	 * case.
	 */
	if (flags & NUMAIO_OBJECT_FLAG_DEDICATED_CPU)
		obj->afo_flags |= NUMAIO_AFO_DEDICATED_CPU;

	/*
	 * Flag used by functionality test module.
	 */
	if (flags & NUMAIO_OBJECT_FLAG_TEST_ONLY)
		obj->afo_flags |= NUMAIO_AFO_TEST_ONLY;

	rw_enter(&numaio_glock, RW_WRITER);
	list_insert_tail(&numaio_object_glist, obj);
	obj->afo_ref++;
	rw_exit(&numaio_glock);

	return (obj);
}

/*
 * Create an affinity object for the kernel thread specified by the
 * kthread argument. The caller is responsible to destroy the object
 * before the kernel thread exists.
 */
numaio_object_t *
numaio_object_create_thread(kthread_t *kthread, const char *name, uint_t flags)
{
	numaio_object_t *obj;

	ASSERT(!(flags & NUMAIO_OBJECT_FLAG_TEST_ONLY));
	obj = numaio_object_create(name, flags);

	ASSERT(kthread != NULL);
	obj->afo_kthread = kthread;

	obj->afo_type = NUMAIO_OBJ_KTHREAD;

	return (obj);
}

/*
 * Create an affinity object for the device corresponding to the
 * specified dev_info. The caller is responsible to ensure that
 * the affinity object is destroyed before the device is detached.
 */
numaio_object_t *
numaio_object_create_dev_info(dev_info_t *dip, const char *name, uint_t flags)
{
	numaio_object_t *obj;

	ASSERT(!(flags & NUMAIO_OBJECT_FLAG_TEST_ONLY));
	obj = numaio_object_create(name, flags);

	ASSERT(dip != NULL);
	obj->afo_dip = dip;

	obj->afo_type = NUMAIO_OBJ_DEVINFO;

	return (obj);
}

numaio_object_t *
numaio_object_create_interrupt(ddi_intr_handle_t intr, const char *name,
    uint_t flags)
{
	numaio_object_t *obj;

	obj = numaio_object_create(name, flags);

	ASSERT(intr != NULL || (flags & NUMAIO_OBJECT_FLAG_TEST_ONLY));
	obj->afo_intr = intr;

	obj->afo_type = NUMAIO_OBJ_DDI_INTR;

	return (obj);
}

numaio_object_t *
numaio_object_create_proc(numaio_proc_cookie_t cookie, const char *name,
    uint_t flags)
{
	numaio_object_t *obj;

	obj = numaio_object_create(name, flags);

	obj->afo_proc = cookie;

	obj->afo_type = NUMAIO_OBJ_PROCINFO;

	return (obj);
}

numaio_group_t *
numaio_object_get_group(numaio_object_t *obj)
{
	return (obj->afo_grp);
}

void
numaio_object_destroy(numaio_object_t *obj)
{
	/* First clear all affinities if it has any */
	numaio_clear_affinity(obj, NULL, 0);

	/*
	 * unbind the object and remove it from the glist. glist
	 * is accessed by cpu callback functions to access all
	 * numaio objects. So it is best to remove it early.
	 */
	mutex_enter(&cpu_lock);
	rw_enter(&numaio_glock, RW_WRITER);
	ASSERT(list_is_empty(&obj->afo_affinities));
	if ((obj->afo_type == NUMAIO_OBJ_KTHREAD ||
	    obj->afo_type == NUMAIO_OBJ_DDI_INTR) && obj->afo_cpuid != -1) {
		numaio_object_unbind(obj);
	}
	if (obj->afo_grp != NULL) {
		list_remove(&obj->afo_grp->afg_objects, obj);
		obj->afo_grp = NULL;
	}
	list_remove(&numaio_object_glist, obj);
	rw_exit(&numaio_glock);
	mutex_exit(&cpu_lock);

	/*
	 * We decrement our reference (which we added at creation
	 * time) and wait for reference count to drop to 0. Once
	 * it drops to 0, we are safe to drop the object lock and
	 * free the object.
	 */
	mutex_enter(&obj->afo_lock);

	ASSERT(obj->afo_ref > 0);
	obj->afo_ref--;
	while (obj->afo_ref > 0)
		cv_wait(&obj->afo_cv, &obj->afo_lock);

	mutex_exit(&obj->afo_lock);

	kmem_cache_free(i_numaio_obj_cachep, obj);
}

numaio_group_t *
numaio_group_create(const char *name)
{
	numaio_group_t *grp;

	grp = kmem_cache_alloc(i_numaio_grp_cachep, KM_SLEEP);

	(void) strlcpy(grp->afg_name, name, sizeof (grp->afg_name));

	list_create(&grp->afg_objects, sizeof (numaio_object_t),
	    offsetof(numaio_object_t, afo_next_grp_obj));

	rw_enter(&numaio_glock, RW_WRITER);
	list_insert_tail(&numaio_group_glist, grp);
	rw_exit(&numaio_glock);

	return (grp);
}

/*
 * Called before freeing a dev_info structure. Free the NUMA IO
 * information that was attached to the dev_info by numaio_dev_init().
 */
void
numaio_dev_fini(dev_info_t *dip)
{
	struct dev_info *devi = DEVI(dip);
	numaio_dev_info_t *dnio = devi->devi_numaio;

	if (dnio == NULL)
		return;

	kmem_free(dnio->niodev_lgrps,
	    dnio->niodev_alloc_nlgrps * sizeof (lgrp_t *));
	kmem_free(dnio, sizeof (numaio_dev_info_t));
	devi->devi_numaio = NULL;
}

/*
 * If it was determined that the specified device is closest to a subset
 * of lgrps, return an array of these lgrps and the number of these lgrps
 * through the <arg_lgrps> and <arg_nlgrps>, respectively.
 * Otherwise return -1.
 *
 * Callers of this routine are expected to validate the returned lgrps. The
 * memory allocated for the lgrp structures are never freed although the
 * state of the lgrp may change.
 */
/* ARGSUSED */
int
numaio_get_lgrp_info(dev_info_t *dip, lgrp_t ***arg_lgrps, uint_t *arg_nlgrps)
{
	/*
	 * Disable numa allocation for SPARC until issues with allocating
	 * outside of the kernel cage is resolved.
	 */

#if !defined(__sparc)
	numaio_dev_info_t *dnio;

	if (nlgrps > 1 && ((dnio = DEVI(dip)->devi_numaio) != NULL)) {
		int li;

		/*
		 * ### proximity info needed if the proximity of lgrps in
		 * the array are not all equal.
		 */

		*arg_lgrps = dnio->niodev_lgrps;
		*arg_nlgrps = dnio->niodev_nlgrps;

		/* round robin the starting index used */
		li = dnio->niodev_index;
		if (dnio->niodev_nlgrps > 1) {
			dnio->niodev_index =
			    (dnio->niodev_index + 1) % dnio->niodev_nlgrps;
		}
		return (li);
	}
#endif /* !defined(__sparc) */

	return (-1);
}

/*
 * Initializes the NUMA IO information for the specified device. This
 * function is called before the driver's attach entry point is called.
 */
void
numaio_dev_init(dev_info_t *dip)
{
	struct dev_info *devi = DEVI(dip);
	lgrp_id_t *lgrp_ids = NULL;
	numaio_dev_info_t *dnio;
	uint_t alloc_nlgrps, dev_nlgrps = 0;
	lgrp_t **lgrps = NULL, *lgrp;
	lgrp_handle_t *lh;
	int i;

	if (nlgrps == 1) {
		/* not a NUMA host */
		return;
	}

	/*
	 * The lgrp information is accessed without taking the cpu_lock,
	 * which would result in a lock ordering violation since this
	 * function is invoked at device attach time while the devi_lock
	 * for the device is held.
	 *
	 * Access of the lgrp information without holding the cpu_lock
	 * is safe, because the access to lgrp information is done either
	 * through:
	 *
	 * 1) platform lgrp handles and lgrp ids which can be safely
	 *    validated, or
	 * 2) pointers to lgrp_t's which are never freed, even when an lgrp
	 *    goes away. LGRP_EXISTS() can be used by the consumers of the
	 *    per-device instance NUMA IO information to ensure that the
	 *    lgrp is still valid.
	 */

	if ((alloc_nlgrps = lgrp_plat_dev_init(dip, &lh)) == 0) {
		/* no NUMA nodes associated with device */
		return;
	}

	lgrps = kmem_alloc(alloc_nlgrps * sizeof (lgrp_t *), KM_SLEEP);
	lgrp_ids = kmem_alloc(alloc_nlgrps * sizeof (lgrp_id_t), KM_SLEEP);

	/* translate pxms to lgrp ids, and validate the lgrps */
	for (i = 0, dev_nlgrps = 0; i < alloc_nlgrps; i++) {
		if ((lgrp = lgrp_hand_to_lgrp(lh[i])) == NULL)
			continue;
		lgrps[dev_nlgrps] = lgrp;
		lgrp_ids[dev_nlgrps++] = lgrp->lgrp_id;
	}

	kmem_free(lh, alloc_nlgrps * sizeof (lgrp_handle_t));

	if (dev_nlgrps == 0) {
		kmem_free(lgrps, alloc_nlgrps * sizeof (lgrp_t *));
		kmem_free(lgrp_ids, alloc_nlgrps * sizeof (lgrp_id_t));
		return;
	}

	/* found lgrps for this device */
	ASSERT(lgrps != NULL);
	ASSERT(lgrp_ids != NULL);

	/* set the device lgrp info */
	dnio = devi->devi_numaio = kmem_zalloc(sizeof (numaio_dev_info_t),
	    KM_SLEEP);
	dnio->niodev_nlgrps = dev_nlgrps;
	dnio->niodev_lgrps = lgrps;
	dnio->niodev_alloc_nlgrps = alloc_nlgrps;
	dnio->niodev_index = 0;

	/* create property to capture lgrp ids */
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    "numaio-lgrps", lgrp_ids, dev_nlgrps);
	kmem_free(lgrp_ids, alloc_nlgrps * sizeof (lgrp_id_t));
}

/*
 * Build a CPU constraint from the specified device. Use the
 * numaio-spec-cpus or numaio-spec-lgrps properties, if they have been
 * configured by the administrator. Otherwise use the lgrps that have
 * been discovered from the machine description or ACPI when the
 * device was being attached.
 */
static numaio_constraint_t *
numaio_constraint_from_dev_info(dev_info_t *dip)
{
	numaio_constraint_t *constraint = NULL;
	int *props, ret_val = -1;
	uint_t nprops;

	constraint = numaio_constraint_create();
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "numaio-spec-cpus", &props, &nprops) == DDI_PROP_SUCCESS) {
		ret_val = numaio_constraint_set_cpus(constraint, props, nprops);
		/* free memory allocated for properties */
		ddi_prop_free(props);
	} else if ((ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "numaio-spec-lgrps", &props, &nprops) ==
	    DDI_PROP_SUCCESS) ||
	    (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "numaio-lgrps", &props, &nprops) ==
	    DDI_PROP_SUCCESS)) {
		ret_val = numaio_set_lgrp_constraint(constraint,
		    (lgrp_id_t *)props, nprops);
		ddi_prop_free(props);
	}

	if (ret_val != 0) {
		numaio_constraint_destroy(constraint);
		constraint = NULL;
	}

	return (constraint);
}

/*
 * Build a CPU constraint from the specified NUMA lgrps.
 */
static int
numaio_set_lgrp_constraint(numaio_constraint_t *constraint,
    lgrp_id_t *lids, int nlids)
{
	int i, j, cpu_count, core_id, core_cnt, total_cpus = 0;
	lgrp_t *lgrp;
	processorid_t *cpuid_list;
	int *coreid_list;
	cpu_t *cpu;
	int ret_val;

	mutex_enter(&cpu_lock);
	for (i = 0; i < nlids; i++) {
		if ((lgrp = lgrp_get(lids[i])) == NULL) {
			mutex_exit(&cpu_lock);
			cmn_err(CE_WARN, "numaio_group_add_object: lgroup id "
			    "%d not valid\n", (int)lids[i]);
			return (NULL);
		}
		total_cpus += lgrp->lgrp_cpucnt;
	}
	cpuid_list = kmem_zalloc(total_cpus * sizeof (processorid_t), KM_SLEEP);
	coreid_list = kmem_zalloc(total_cpus * sizeof (int), KM_SLEEP);
	cpu_count = 0;
	core_cnt = 0;
	for (i = 0; i < nlids; i++) {
		lgrp = lgrp_get(lids[i]);
		ASSERT(lgrp != NULL);
		cpu = lgrp->lgrp_cpu;
		if (cpu != NULL) {
			do {
				if (!lgrp_plat_use_all_hw_threads()) {
					/*
					 * Each CPU core can have more than
					 * one hardware threads (or
					 * cpuids). On some platforms, only
					 * one thread in the core is
					 * desired. So before adding a
					 * cpuid to cpuid_list, get the
					 * CPU's core_id and check if that
					 * core_id is already present in
					 * coreid_list[].
					 */
					core_id = cpu->cpu_physid->cpu_coreid;
					for (j = 0; j < core_cnt; j++) {
						if (coreid_list[j] == core_id)
							break;
					}
					if (j == core_cnt) {
						coreid_list[core_cnt] = core_id;
						core_cnt++;
						cpuid_list[cpu_count] =
						    cpu->cpu_id;
						cpu_count++;
					}
				} else {
					cpuid_list[cpu_count] = cpu->cpu_id;
					cpu_count++;
				}
				cpu = cpu->cpu_next_lgrp;
			} while (cpu != lgrp->lgrp_cpu);
		}
	}
	mutex_exit(&cpu_lock);

	constraint->afc_flags |= NUMAIO_CONSTRAINT_IMPLICIT;
	ret_val = numaio_constraint_set_cpus(constraint, cpuid_list, cpu_count);
	kmem_free(cpuid_list, total_cpus * sizeof (processorid_t));
	kmem_free(coreid_list, total_cpus * sizeof (int));
	return (ret_val);
}

void
numaio_group_add_object(numaio_group_t *grp, numaio_object_t *obj, uint_t flags)
{
	numaio_constraint_t *constraint;

	/* object should not be part of a group */
	ASSERT(obj->afo_grp == NULL);

	/*
	 * A DEVINFO type object is used as a special case to
	 * build constraint to the group.
	 */
	rw_enter(&numaio_glock, RW_WRITER);
	if (obj->afo_type == NUMAIO_OBJ_DEVINFO &&
	    grp->afg_constraint == NULL) {
		/*
		 * Drop the lock here as numaio_constraint_from_dev_info()
		 * will be grabbing cpu_lock and will cause lock ordering
		 * issues.
		 */
		rw_exit(&numaio_glock);
		constraint = numaio_constraint_from_dev_info(obj->afo_dip);
		rw_enter(&numaio_glock, RW_WRITER);
		if (constraint != NULL) {
			grp->afg_constraint = constraint;
			grp->afg_constraint_self = B_TRUE;
		}
	}


	list_insert_tail(&grp->afg_objects, obj);

	obj->afo_grp = grp;

	rw_exit(&numaio_glock);

	if (!(flags & NUMAIO_FLAG_BIND_DEFER))
		numaio_group_map(grp);
}

static void
numaio_group_remove_object_one(numaio_group_t *grp, numaio_object_t *obj)
{
	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(RW_WRITE_HELD(&numaio_glock));
	ASSERT(grp == obj->afo_grp);
	numaio_clear_affinity_locked(obj, NULL, 0);
	if (obj->afo_cpuid != -1)
		numaio_object_unbind(obj);
	list_remove(&grp->afg_objects, obj);
	obj->afo_grp = NULL;
}

void
numaio_group_remove_object(numaio_group_t *grp, numaio_object_t *obj)
{
	mutex_enter(&cpu_lock);
	rw_enter(&numaio_glock, RW_WRITER);
	numaio_group_remove_object_one(grp, obj);
	rw_exit(&numaio_glock);
	mutex_exit(&cpu_lock);
}

/*
 * Remove all objects from the group after clearing their affinities.
 */
void
numaio_group_remove_all_objects(numaio_group_t *grp)
{
	numaio_object_t *obj;

	mutex_enter(&cpu_lock);
	rw_enter(&numaio_glock, RW_WRITER);
	while ((obj = list_head(&grp->afg_objects)) != NULL)
		numaio_group_remove_object_one(grp, obj);
	rw_exit(&numaio_glock);
	mutex_exit(&cpu_lock);

}

/*
 * Given an object, remove it from its numa group.
 */
void
numaio_clear_group_reference(numaio_object_t *obj)
{
	numaio_group_t *grp;

	if ((grp = obj->afo_grp) != NULL) {
		mutex_enter(&cpu_lock);
		rw_enter(&numaio_glock, RW_WRITER);
		numaio_group_remove_object_one(grp, obj);
		rw_exit(&numaio_glock);
		mutex_exit(&cpu_lock);
	}
}

void
numaio_group_destroy(numaio_group_t *grp)
{
	ASSERT(list_is_empty(&grp->afg_objects));

	rw_enter(&numaio_glock, RW_WRITER);
	list_remove(&numaio_group_glist, grp);
	rw_exit(&numaio_glock);

	if (grp->afg_constraint != NULL && grp->afg_constraint_self)
		numaio_constraint_destroy(grp->afg_constraint);
	grp->afg_constraint = NULL;
	grp->afg_constraint_self = B_FALSE;
	kmem_cache_free(i_numaio_grp_cachep, grp);
}

/*
 * Return TRUE if there is atleast one CPU in the lgroup that belongs
 * to default partition and that CPU should not be the CPU passed in
 * the second argument.
 */
static boolean_t
numaio_check_default_part(lgrp_t *lgrp, cpu_t *cp_avoid)
{
	cpu_t *lgrp_cp;

	if ((lgrp_cp = lgrp->lgrp_cpu) == NULL)
		return (B_FALSE);

	do {
		if (lgrp_cp->cpu_part == &cp_default) {
			if (cp_avoid == NULL ||
			    (cp_avoid != NULL && cp_avoid != lgrp_cp)) {
				return (B_TRUE);
			}
		}
		lgrp_cp = lgrp_cp->cpu_next_lgrp;
	} while (lgrp_cp != lgrp->lgrp_cpu);

	return (B_FALSE);
}

/*
 * Returns the lgroup that has least number of numarefs and has CPUs
 * in the default partition other than the passed CPU.
 */
static lgrp_t *
numaio_find_best_lgrp(cpu_t *cp_avoid)
{
	lgrp_t *lgrp, *best_lgrp = NULL;
	int i;

	ASSERT(MUTEX_HELD(&cpu_lock));

	/*
	 * Find the leaf lgroup that has the least numa references and
	 * return that lgroup.
	 */
	for (i = 0; i <= lgrp_alloc_max; i++) {
		lgrp = lgrp_table[i];

		if (!LGRP_EXISTS(lgrp))
			continue;

		/*
		 * We are interested only in leaf lgroups (leaf
		 * lgroups do not have children).
		 */
		if (lgrp->lgrp_childcnt != 0)
			continue;

		/* Is there any CPUs belonging to the default partition? */
		if (!numaio_check_default_part(lgrp, cp_avoid))
			continue;

		if (best_lgrp == NULL) {
			best_lgrp = lgrp;
			continue;
		}

		if (lgrp->lgrp_numaio_refs < best_lgrp->lgrp_numaio_refs)
			best_lgrp = lgrp;
	}

	return (best_lgrp);
}

static boolean_t
numaio_cpu_already_used(processorid_t *cpu_list, size_t cpu_count, cpu_t *cp)
{
	processorid_t cpuid = cp->cpu_id;
	int i;

	for (i = 0; i < cpu_count; i++) {
		if (cpuid == cpu_list[i])
			return (B_TRUE);
	}

	return (B_FALSE);
}

static cpu_t *
numaio_cpu_get(processorid_t id)
{
	cpu_t *cp;

	ASSERT(MUTEX_HELD(&cpu_lock));
	cp = cpu_get(id);
	if (cp != NULL && cpu_is_online(cp))
		return (cp);

	return (NULL);
}

/*
 * There  are systems  with 2 or more dual  core CPUs and each dual
 * core is an  lgroup. Use this macro on those systems to avoid all
 * threads going to just one lgroup.
 */
#define	LGRP_CPU_MIN	4

/*
 * Get the list of CPUs used by the objects in the numaio_group_t.
 * Then use the list to select the next best CPU that is not
 * already part of the group.
 * If group is NULL, use near_cpu (2nd argument) to find a CPU
 * which will lie in the same lgroup as near_cpu. The CPU that will
 * be returned will be from the default partiion (cp_default).
 */
static cpu_t *
numaio_find_best_cpu(numaio_group_t *grp, cpu_t *near_cpu, boolean_t get_new)
{
	lgrp_t *lgrp;
	cpu_t *cp, *lgrp_cp, *best_cpu = NULL;
	processorid_t cpuid, *cpu_list = NULL;
	size_t cpu_count = 0;
	boolean_t best_cp_used;
	int i;

	ASSERT(MUTEX_HELD(&cpu_lock));
	cpu_list = kmem_zalloc(sizeof (processorid_t) * GROUP_MAX_CPUS,
	    KM_SLEEP);

again:
	if (grp != NULL) {
		if (near_cpu != NULL) {
			cpu_list[0] = near_cpu->cpu_id;
			cpu_count = 1;
		} else {
			i_numaio_get_effective_cpus(grp, cpu_list,
			    GROUP_MAX_CPUS, &cpu_count);
		}
		if (cpu_count != 0) {
			/*
			 * Get the lgrp using one of the CPUs in the cpu_list
			 * and from that lgroup, pick the best CPU.
			 */
			for (i = 0; i < cpu_count; i++) {
				cpuid = cpu_list[i];
				cp = numaio_cpu_get(cpuid);
				if (cp != NULL) {
					lgrp = cp->cpu_lpl->lpl_lgrp;
					/*
					 * There  are systems  with 2 or
					 * more dual  core CPUs and each
					 * dual core is an  lgroup. This
					 * check is to avoid all threads
					 * going to just those 2 CPUs on
					 * those systems.
					 */
					if (lgrp->lgrp_cpucnt < LGRP_CPU_MIN &&
					    near_cpu == NULL) {
						goto next;
					}
					lgrp_cp = lgrp->lgrp_cpu;
					/*
					 * If new cpu is requested, check
					 * if there is atleast a CPU from
					 * default partition available in
					 * lgrp.
					 */
					if (get_new &&
					    !numaio_check_default_part(lgrp,
					    near_cpu)) {
						goto next;
					}
					break;
				}
			}
			/*
			 * If none of the CPUs in cpu_list are valid, then
			 * get a CPU from the best lgroup available.
			 */
			if (i == cpu_count)
				goto next;
		} else {
next:
			/*
			 * If a new CPU is requested, then pass near_cpu
			 * so that an lgroup containing only that CPU
			 * won't be returned. It is possible and ok for
			 * numaio_find_best_lgrp() to return an lgroup
			 * that contain near_cpu along with other CPUs
			 * that belong to default partition.
			 */
			lgrp = numaio_find_best_lgrp(get_new ? near_cpu : NULL);
			if (lgrp == NULL)
				return (NULL);

			lgrp_cp = lgrp->lgrp_cpu;
		}
	}

	do {
		/*
		 * First iteration of the loop. best_cpu is NULL. Store
		 * the current CPU as the best_cpu. The current CPU
		 * could very well be the only CPU available in this
		 * lgroup.
		 */
		if (lgrp_cp->cpu_part != &cp_default)
			goto next_cpu;

		if (get_new && lgrp_cp == near_cpu)
			goto next_cpu;

		if (best_cpu == NULL) {
			best_cpu = lgrp_cp;
			best_cp_used = numaio_cpu_already_used(cpu_list,
			    cpu_count, best_cpu);
			goto next_cpu;
		}

		/*
		 * Check if the current best CPU is already used in the
		 * group. If so, save the new CPU (in lgrp_cp) as the
		 * best CPU if it has a lesser ref count.
		 *
		 * If the current best_cpu is not used, then check if
		 * the new CPU (in lgrp_cp) is also not used and if
		 * so, save the lgrp_cp as the best_cpu.
		 */
		if (lgrp_cp->cpu_numaio_info->ni_cpu_refcnt <
		    best_cpu->cpu_numaio_info->ni_cpu_refcnt) {
			if (best_cp_used) {
				best_cpu = lgrp_cp;
				best_cp_used = numaio_cpu_already_used(cpu_list,
				    cpu_count, best_cpu);
			} else if (!numaio_cpu_already_used(cpu_list, cpu_count,
			    lgrp_cp)) {
				best_cpu = lgrp_cp;
			}
		}
next_cpu:
		lgrp_cp = lgrp_cp->cpu_next_lgrp;
	} while (lgrp_cp != lgrp->lgrp_cpu);

	/*
	 * It could be possible that the default partition may have
	 * only one CPU. In such a case, there is no chance to get
	 * a different CPU. Change the get_new flag to FALSE and
	 * try again.
	 */
	if (best_cpu == NULL && get_new) {
		get_new = B_FALSE;
		goto again;
	}

	kmem_free(cpu_list, sizeof (processorid_t) * GROUP_MAX_CPUS);
	return (best_cpu);
}

/*
 * Pick the best CPU from within an lgroup in a constraint. The best CPU
 * is that has the least number of bindings (ni_cpu_refcnt). If excl CPU
 * is specified, then pick and exclusive CPU which is reserved for that
 * purpose. If all the CPUs in the lgroup has been reserved for use in
 * the group, then mv_lgrpindex is set to TRUE and returned to the
 * caller.
 */
static processorid_t
numaio_constraint_find_best_cpu(numaio_group_t *grp,
    numaio_constraint_t *constraint, boolean_t excl_cpu,
    boolean_t *mv_lgrpindex)
{
	int i, lgrp_index, afl_cpu_index;
	boolean_t cp_used, best_cp_used = B_FALSE;
	cpu_t *best_cpu = NULL, *cp;
	size_t cpu_count = 0;
	processorid_t id, *cpu_list = NULL;
	numaio_cpuid_t *aff_cpup, *nxt_cpup;

	ASSERT(MUTEX_HELD(&cpu_lock));
	cpu_list = kmem_zalloc(sizeof (processorid_t) * GROUP_MAX_CPUS,
	    KM_SLEEP);

	/* Get the list of CPUs currently used in the group */
	i_numaio_get_effective_cpus(grp, cpu_list, GROUP_MAX_CPUS, &cpu_count);

	lgrp_index = constraint->afc_lgrp_index;
	for (i = 0; i < constraint->afc_lgrp[lgrp_index].afl_ncpus; i++) {
		afl_cpu_index = constraint->afc_lgrp[lgrp_index].afl_cpu_index;
		aff_cpup = constraint->afc_lgrp[lgrp_index].afl_cpus;

		if (++constraint->afc_lgrp[lgrp_index].afl_cpu_index >=
		    constraint->afc_lgrp[lgrp_index].afl_ncpus) {
			constraint->afc_lgrp[lgrp_index].afl_cpu_index = 0;
		}

		if (aff_cpup[afl_cpu_index].nc_flags & AFC_CPU_NOT_AVAIL)
			continue;
		/*
		 * If excl_cpu is requested, pick a CPU that has
		 * been reserved for objects requesting dedicated
		 * CPUs. The reserved CPU is one that has
		 * AFC_CPU_EXCL_RESERVED flag set and
		 * nc_dedi_count > 0. Once the CPU is given to
		 * the object, nc_dedi_count is decremented.
		 */
		nxt_cpup = &aff_cpup[afl_cpu_index];
		if (excl_cpu) {
			if (AFF_EXCL_RESERVED(nxt_cpup) &&
			    nxt_cpup->nc_dedi_count > 0) {
				nxt_cpup->nc_dedi_count--;
				id = nxt_cpup->nc_cpuid;
				if ((best_cpu = numaio_cpu_get(id)) != NULL)
					break;
			}
			continue;
		} else if (AFF_EXCL_RESERVED(nxt_cpup)) {
			/*
			 * We are not exclusive and we don't
			 * want a CPU reserved for exclusive
			 * use.
			 */
			continue;
		}

		id = nxt_cpup->nc_cpuid;
		cp = numaio_cpu_get(id);
		if (cp == NULL)
			continue;

		/*
		 * best_cpu is NULL on the first iteration. Save whatever
		 * is the current CPU as the best. Also note down if it
		 * is a CPU already used in the group.
		 */
		if (best_cpu == NULL) {
			best_cpu = cp;
			/* No cpu_refcnt means this is the best one */
			if (best_cpu->cpu_numaio_info->ni_cpu_refcnt == 0)
				break;
			best_cp_used = numaio_cpu_already_used(cpu_list,
			    cpu_count, best_cpu);
			continue;
		}
		/*
		 * If it is a already used CPU, check if the current CPU
		 * is unused in the group. If so, save the current CPU
		 * as the best and continue looping.
		 */
		cp_used = numaio_cpu_already_used(cpu_list, cpu_count, cp);
		if (best_cp_used && !cp_used) {
			best_cpu = cp;
			best_cp_used = B_FALSE;
			continue;
		}
		/*
		 * The current CPU is already used but not the best_cpu.
		 * So look for an unused CPU to do comparison.
		 */
		if (!best_cp_used && cp_used)
			continue;
		/*
		 * Either one of the following is true.
		 * 1) best_cpu and cp have both been used.
		 * 2) best_cpu and cp are both unused.
		 * Find the better of the two.
		 */
		if (cp->cpu_numaio_info->ni_cpu_refcnt <
		    best_cpu->cpu_numaio_info->ni_cpu_refcnt) {
			best_cpu = cp;
			best_cp_used = cp_used;
		}
	}

	kmem_free(cpu_list, sizeof (processorid_t) * GROUP_MAX_CPUS);
	if (best_cpu != NULL) {
		*mv_lgrpindex = best_cp_used ? B_TRUE : B_FALSE;
		return (best_cpu->cpu_id);
	}

	return (-1);
}

/*
 * Set afc_lgrp_index in the contraint to match the lgrp_hint if a valid
 * hint is passed. If 'begin' is set, set afc_lgrp_index to point to the
 * best lgroup in the constraint. Best lgroup is one that has the least
 * lgrp_numaio_refs.
 */
static void
numaio_constraint_set_best_lgroup(numaio_constraint_t *constraint,
    uint64_t lgrp_hint, boolean_t *lgrp_match, boolean_t begin)
{
	int i;
	lgrp_t *lgrp, *best_lgrp = NULL;

	for (i = 0; i < NLGRPS_MAX; i++) {
		if (constraint->afc_lgrp[i].afl_ncpus == 0)
			continue;

		if (lgrp_hint != 0) {
			if (klgrpset_ismember(lgrp_hint, i)) {
				/*
				 * Set afc_lgrp_index to match the
				 * lgrp_hint. Next CPU will be
				 * picked from the new numaio_lgrp_t.
				 */
				constraint->afc_lgrp_index = i;
				*lgrp_match = B_TRUE;
				break;
			}
		}
		/*
		 * If lgrp_hint is not matched, and 'begin' is set,
		 * then keep updating afc_lgrp_index to point to the
		 * best lgroup. This will help return the constraint
		 * pointing to the best lgroup in case the lgrp_hint
		 * doesn't find a matching lgroup in the constraint.
		 */
		if (begin) {
			lgrp = lgrp_table[i];
			if (!LGRP_EXISTS(lgrp))
				continue;

			if (best_lgrp == NULL) {
				best_lgrp = lgrp;
				constraint->afc_lgrp_index = i;
				continue;
			}

			if (lgrp->lgrp_numaio_refs <
			    best_lgrp->lgrp_numaio_refs) {
				best_lgrp = lgrp;
				constraint->afc_lgrp_index = i;
			}
		}
	}
}

/*
 * Get the next cpu. If 'begin' is specified, we start from the first
 * cpu available from the passed constraint. 'lgrp_hint' is a bitmap
 * of lgroups (klgrpset_t) that gives additional hint as to the
 * lgroup from which the next cpu is desired. 'excl_cpu' is a request
 * to get a reserved CPU which will be completely dedicated for use
 * to an object.
 * Returns the cpuid on success. On failure, -1 is returned. This
 * could happen in the case where none of the CPUs in the constraint
 * is online.
 */
static processorid_t
numaio_get_next_cpu(numaio_group_t *grp, numaio_constraint_t *constraint,
    boolean_t begin, uint64_t lgrp_hint, cpu_t *near_cpu, boolean_t excl_cpu)
{
	int cpuid = -1;
	cpu_t *cp = NULL;
	numaio_constraint_type_t constraint_type;
	int i, lgrp_index, nlgroups, saved_lgrp_indx;
	boolean_t mv_lgrpindex = B_FALSE, lgrp_match = B_FALSE;
	numaio_lgrp_t *numa_lgrp;

	ASSERT(MUTEX_HELD(&cpu_lock));
	if (constraint == NULL)
		constraint_type = NUMAIO_CONSTRAINT_NONE;
	else
		constraint_type = constraint->afc_type;

	switch (constraint_type) {
	case NUMAIO_CONSTRAINT_NONE:
		cp = numaio_find_best_cpu(grp, near_cpu, B_FALSE);
		if (cp != NULL)
			cpuid = cp->cpu_id;
		break;

	case NUMAIO_CONSTRAINT_CPUS:
	case NUMAIO_CONSTRAINT_POOL:
		/*
		 * A comment on 'all_over_again' label: If an excl_cpu
		 * is requested and we don't find any reserved for
		 * exclusive use, we come here and try again for a
		 * non-exclusive CPU.
		 */
all_over_again:
		/*
		 * If lgrp_hint is specified, set the constraint's
		 * afc_lgrp_index to point to the lgrp specified
		 * in the hint. Otherwise if 'begin' is set, make
		 * the afc_lgrp_index point to the best lgrp in
		 * the constraint.
		 */
		saved_lgrp_indx = constraint->afc_lgrp_index;
		if (lgrp_hint != 0 || begin) {
			numaio_constraint_set_best_lgroup(constraint,
			    lgrp_hint, &lgrp_match, begin);
		}

		lgrp_index = constraint->afc_lgrp_index;
		if (begin) {
			begin = B_FALSE;
			constraint->afc_lgrp[lgrp_index].afl_cpu_index = 0;
			saved_lgrp_indx = lgrp_index;
		}
		/*
		 * loop through all lgroups to find a CPU that matches
		 * all requirements.
		 */
		for (nlgroups = 0;
		    nlgroups < constraint->afc_nlgrps; nlgroups++) {
			cpuid = numaio_constraint_find_best_cpu(grp, constraint,
			    excl_cpu, &mv_lgrpindex);
			/*
			 * If we have used up all the CPUs in this lgrp,
			 * set the afc_lgrp_index to point to the next
			 * lgrp that has CPUs.
			 */
			if (mv_lgrpindex || cpuid == -1) {
				if (constraint->afc_nlgrps == 1)
					break;

				mv_lgrpindex = B_FALSE;
				lgrp_index = constraint->afc_lgrp_index + 1;
				for (i = 0; i < NLGRPS_MAX; i++) {
					if (lgrp_index >= NLGRPS_MAX)
						lgrp_index = 0;

					numa_lgrp =
					    &constraint->afc_lgrp[lgrp_index];
					if (numa_lgrp->afl_ncpus > 0) {
						constraint->afc_lgrp_index =
						    lgrp_index;
						break;
					}
					lgrp_index++;
				}
				/*
				 * 1) We did not find a unique CPU in the
				 * 2nd lgroup also. This means that all
				 * the CPUs in all the lgroups have been
				 * used up. So return this cpuid.
				 * 2) If lgrp_match is true, it means the
				 * the caller specifically asked for a
				 * CPU from this lgroup.
				 */
				if (cpuid != -1 && (nlgroups > 0 || lgrp_match))
					break;
			} else {
				break;
			}
		}
		/*
		 * If cpuid is -1, check other numaio_lgrps to
		 * see if a valid CPU is present. If that
		 * fails too, then reset excl_cpu flag and try
		 * for a non-dedicated CPU.
		 */
		if (cpuid == -1 && excl_cpu) {
			excl_cpu = B_FALSE;
			goto all_over_again;
		}
		/*
		 * If there was an lgrp match, then afc_lgrp_index would
		 * have been moved to get to the matching lgrp. Restore
		 * it back to the previous index.
		 */
		if (lgrp_match)
			constraint->afc_lgrp_index = saved_lgrp_indx;

		break;

	default:
		break;
	}

	return (cpuid);
}

/*
 * Objects that requested a dedicated CPU will cause exclusive flag
 * (AFC_CPU_EXCL_RESERVED) to be set. When those objects go away
 * (numaio_object_unbind() is one such instance), AFC_CPU_EXCL_RESERVED
 * bit should be reset for the CPU. If the caller passes a cpuid of
 * -1 (numaio_group_map() is one such caller), then all CPUs
 * exclusive flag bit in the constraint will be reset.
 */
static void
numaio_constraint_reset_cpu(numaio_constraint_t *constraint,
    processorid_t cpuid, boolean_t excl_cpu)
{
	int nlgroups, i, afl_ncpus;
	numaio_cpuid_t *aff_cpup, *nxt_cpup;

	ASSERT(RW_WRITE_HELD(&numaio_glock));

	if (constraint == NULL || constraint->afc_type !=
	    NUMAIO_CONSTRAINT_CPUS)
		return;

	for (nlgroups = 0; nlgroups < NLGRPS_MAX; nlgroups++) {
		if ((afl_ncpus = constraint->afc_lgrp[nlgroups].afl_ncpus) == 0)
			continue;

		aff_cpup = constraint->afc_lgrp[nlgroups].afl_cpus;
		for (i = 0; i < afl_ncpus; i++) {
			nxt_cpup = &aff_cpup[i];
			if (nxt_cpup->nc_cpuid == cpuid || cpuid == -1) {
				if (excl_cpu) {
					AFF_RESET_RESERVE(nxt_cpup);
					nxt_cpup->nc_dedi_count = 0;
				}
			}
		}
	}
}

/*
 * Unbind an object
 */
static void
numaio_object_unbind(numaio_object_t *obj)
{
	numaio_constraint_t *constraint = NULL;
	cpu_t *cp = NULL;

	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(RW_WRITE_HELD(&numaio_glock));

	/* If object is not bound to any CPU, just return */
	if (obj->afo_cpuid == -1)
		return;

	/*
	 * unset exclusive cpu flag if set.
	 */
	constraint = (obj->afo_grp != NULL) ?
	    obj->afo_grp->afg_constraint : NULL;
	numaio_constraint_reset_cpu(constraint,
	    obj->afo_cpuid, OBJ_HAS_DEDICATED_CPU(obj));

	if (obj->afo_type == NUMAIO_OBJ_KTHREAD)
		thread_affinity_clear(obj->afo_kthread);
	else if (obj->afo_type == NUMAIO_OBJ_DDI_INTR)
		bal_numa_managed_clear(obj->afo_intr, obj->afo_cpuid);

	cp = cpu_get(obj->afo_cpuid);
	if (cp != NULL)
		numaio_decrement_cpu(obj, cp);
	obj->afo_cpuid = -1;
}

/*
 * Binds the object to the passed cpu
 */
static void
numaio_object_bind_to_cpu(numaio_object_t *obj, processorid_t cpuid)
{
	cpu_t *numaio_cpu;

	ASSERT(MUTEX_HELD(&cpu_lock));

	numaio_cpu = numaio_cpu_get(cpuid);
	if (numaio_cpu == NULL)
		return;

	ASSERT(obj->afo_type == NUMAIO_OBJ_KTHREAD);
	ASSERT(obj->afo_cpuid == -1);
	thread_affinity_set(obj->afo_kthread, cpuid);
	obj->afo_cpuid = cpuid;
	numaio_increment_cpu(obj, numaio_cpu);
}

static void
aff_reserve_a_cpu(numaio_object_t *obj)
{
	uint64_t lgrp_hint;
	int i, lgrp_index, nlgroups;
	boolean_t first_time;
	numaio_cpuid_t *aff_cpup, *best_aff_cpup = NULL;
	processorid_t id;
	cpu_t *cp;
	numaio_constraint_t *constraint;

	ASSERT(MUTEX_HELD(&cpu_lock));
	constraint = ((obj->afo_grp != NULL) ?
	    obj->afo_grp->afg_constraint : NULL);

	if (constraint == NULL)
		return;

	lgrp_hint = obj->afo_lgrp_hint;

	/*
	 * If lgrp_hint is present, try to reserve a dedicated CPU
	 * on the specified lgrp.
	 */
	if (lgrp_hint != 0) {
		for (i = 0; i < NLGRPS_MAX; i++) {
			if (constraint->afc_lgrp[i].afl_ncpus == 0)
				continue;

			if (klgrpset_ismember(lgrp_hint, i)) {
				constraint->afc_lgrp_index = i;
				break;
			}
		}
	}

	nlgroups = 0;
again:
	first_time = B_TRUE;
	lgrp_index = constraint->afc_lgrp_index;

	aff_cpup = constraint->afc_lgrp[lgrp_index].afl_cpus;

	for (i = 0; i < constraint->afc_lgrp[lgrp_index].afl_ncpus; i++) {
		if (aff_cpup[i].nc_flags & AFC_CPU_NOT_AVAIL)
			continue;

		id = aff_cpup[i].nc_cpuid;
		cp = numaio_cpu_get(id);
		if (cp == NULL)
			continue;

		/*
		 * Save the first CPU of the constraint for
		 * objects that don't require exclusive use.
		 *
		 * Note that if the number of objects
		 * requesting dedicated CPUs are more than
		 * the number of available CPUs, then
		 * multiple objects will share a CPU.
		 */
		if (first_time) {
			first_time = B_FALSE;
			continue;
		}

		/* Found an unused CPU, use it */
		if (aff_cpup[i].nc_dedi_count == 0) {
			best_aff_cpup = &aff_cpup[i];
			break;
		}

		/*
		 * Continue iterating through the CPUs, tracking
		 * the CPU with the least number of objects
		 * requiring a dedicated CPU.
		 */
		if (best_aff_cpup == NULL) {
			best_aff_cpup = &aff_cpup[i];
			continue;
		}

		if (best_aff_cpup->nc_dedi_count >
		    aff_cpup[i].nc_dedi_count) {
			best_aff_cpup = &aff_cpup[i];
		}
	}

	/*
	 * If we did not find a CPU with nc_dedi_count = 0, then check
	 * if there are more lgroups with CPUs available and if so try
	 * to find a CPU with 0 dedi_count there.
	 */
	if (i == constraint->afc_lgrp[lgrp_index].afl_ncpus) {
		nlgroups++;
		if (nlgroups < constraint->afc_nlgrps) {
			for (i = 0, lgrp_index = constraint->afc_lgrp_index + 1;
			    i < NLGRPS_MAX; i++, lgrp_index++) {
				if (lgrp_index >= NLGRPS_MAX)
					lgrp_index = 0;

				if (constraint->afc_lgrp[lgrp_index].afl_ncpus >
				    0) {
					constraint->afc_lgrp_index = lgrp_index;
					goto again;
				}
			}
		}
	}

	/*
	 * There are few cases where best_aff_cpup can be NULL.
	 * 1) when there is only one CPU in the constraint.
	 * 2) all the CPUs in the constraint are offline.
	 * 3) all the CPUs in the implicit constraint becomes
	 * part of a pset.
	 */
	if (best_aff_cpup != NULL) {
		best_aff_cpup->nc_dedi_count++;
		best_aff_cpup->nc_flags |= AFC_CPU_EXCL_RESERVED;
	}
}

/*
 * Before mapping objects to CPUs, reserve CPUs in the constraint for
 * objects requesting dedicated CPUs. This is needed or otherwise
 * objects not requiring dedicated CPUs may use up all the CPUs in
 * the constraint.
 */
static void
numaio_reserve_excl_cpus(numaio_group_t *grp)
{
	numaio_object_t *obj;

	for (obj = list_head(&grp->afg_objects); obj != NULL;
	    obj = list_next(&grp->afg_objects, obj)) {
		/*
		 * Dedicated CPUs are not required for fixed objects
		 * which are used as hints or to derive an implicit
		 * constraint.
		 */
		if (obj->afo_type == NUMAIO_OBJ_DEVINFO ||
		    obj->afo_type == NUMAIO_OBJ_PROCINFO) {
			continue;
		}
		if (obj->afo_flags & NUMAIO_AFO_DEDICATED_CPU)
			aff_reserve_a_cpu(obj);
	}
}

/*
 * Assigns the objects of the specified group to hardware resources.
 */
void
numaio_group_map(numaio_group_t *grp)
{
	mutex_enter(&cpu_lock);
	rw_enter(&numaio_glock, RW_WRITER);

	numaio_group_map_locked(grp);

	rw_exit(&numaio_glock);
	mutex_exit(&cpu_lock);
}

/*
 * Private version of numaio_group_map_locked() which assumes that the
 * CPU and NUMA IO global lock are already held.
 */
static void
numaio_group_map_locked(numaio_group_t *grp)
{
	numaio_object_t *obj, *obj2;
	numaio_affinity_t *aff;
	numaio_affinity_link_t *aff_link;
	processorid_t cpuid;
	boolean_t excl_flag, first_time, begin = B_TRUE;
	uint_t gen_num;
	uint64_t lgrp_hint;
	cpu_t *cp;

	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(RW_WRITE_HELD(&numaio_glock));

	/*
	 * The group has the list of objects. Get each object and walk
	 * the object's affinity list establishing the affinities
	 * between the objects. If the object does not have affinity
	 * with another object, then pick a CPU from the specified
	 * constraint and bind the object to that CPU.
	 */
	gen_num = ++numaio_gen_num;

	/* reset exclusive CPU flag */
	numaio_constraint_reset_cpu(grp->afg_constraint, -1, B_TRUE);

	/* reserve exclusive CPUs for the group first */
	numaio_reserve_excl_cpus(grp);

	for (obj = list_head(&grp->afg_objects); obj != NULL;
	    obj = list_next(&grp->afg_objects, obj)) {
		first_time = B_TRUE;
		lgrp_hint = 0;

		/*
		 * Don't map fixed objects which are used as hints or
		 * to derive an implicit constraint.
		 */
		if (obj->afo_type == NUMAIO_OBJ_DEVINFO ||
		    obj->afo_type == NUMAIO_OBJ_PROCINFO) {
			continue;
		}

		aff_link = list_head(&obj->afo_affinities);

		do {
			if (aff_link != NULL) {
				aff = aff_link->afl_affinity;
				if (aff->afa_obj1 == obj) {
					obj2 = aff->afa_obj2;
				} else {
					obj2 = aff->afa_obj1;
					ASSERT(aff->afa_obj2 == obj);
				}
				ASSERT(obj2 != NULL);
				if (obj->afo_flags &  NUMAIO_AFO_LGRP_HINT)
					lgrp_hint = obj->afo_lgrp_hint;
				else if (obj2->afo_flags & NUMAIO_AFO_LGRP_HINT)
					lgrp_hint = obj2->afo_lgrp_hint;
				/*
				 * If the second object in the affinity is
				 * a DEVINFO or PROCINFO type, then that
				 * object cannot be assigned a CPU. Set
				 * the object to NULL so it won't get
				 * used.
				 */
				if (obj2->afo_type == NUMAIO_OBJ_DEVINFO ||
				    obj2->afo_type == NUMAIO_OBJ_PROCINFO) {
					obj2 = NULL;
				}
				aff_link = list_next(&obj->afo_affinities,
				    aff_link);
			} else {
				obj2 = NULL;
			}

			/*
			 * If afo_gen_num is equal to gen_num, it means
			 * that binding for that object was done
			 * earlier as part of walking another object's
			 * affinity list.
			 */
			if (first_time && obj->afo_gen_num != gen_num) {
				excl_flag = OBJ_HAS_DEDICATED_CPU(obj);
				cpuid = numaio_get_next_cpu(grp,
				    grp->afg_constraint, begin, lgrp_hint,
				    NULL, excl_flag);
				begin = B_FALSE;
				if (obj->afo_cpuid != cpuid)
					numaio_object_bind_async(obj, cpuid);
				obj->afo_gen_num = gen_num;
			}

			first_time = B_FALSE;
			if (obj2  != NULL && obj2->afo_gen_num != gen_num) {
				obj2->afo_gen_num = gen_num;
				if (aff->afa_strength ==
				    NUMAIO_AFF_STRENGTH_CPU) {
					cpuid = obj->afo_cpuid;
				} else {
					/*
					 * We default to socket strength
					 * affinity.
					 */
					excl_flag = OBJ_HAS_DEDICATED_CPU(
					    obj2);
					/*
					 * If lgrp hint is 0, we need to get
					 * a CPU which is within the
					 * socket/lgrp as object1's (obj)
					 * CPU.
					 */
					if (lgrp_hint == 0 &&
					    (cp =
					    numaio_cpu_get(obj->afo_cpuid)) !=
					    NULL) {
						klgrpset_add(lgrp_hint,
						    cp->cpu_lpl->lpl_lgrpid);
					}
					cpuid = numaio_get_next_cpu(grp,
					    grp->afg_constraint, B_FALSE,
					    lgrp_hint, cp, excl_flag);
				}

				if (obj2->afo_cpuid != cpuid)
					numaio_object_bind_async(obj2, cpuid);
			}
		} while (aff_link != NULL);
	}
	grp->afg_mapped = B_TRUE;
}

/*
 * Set the affinity between two objects. Multiple affinities can
 * be defined between two objects. All affinities between two objects
 * can be cleared with aff_clear_affinity();
 */
static numaio_affinity_t *
numaio_create_affinity(void)
{
	numaio_affinity_t *aff;

	aff = kmem_zalloc(sizeof (numaio_affinity_t), KM_SLEEP);

	return (aff);
}

static void
numaio_destroy_affinity(numaio_affinity_t *aff)
{
	kmem_free(aff, sizeof (numaio_affinity_t));
}

/*
 * Link the specified affinity with the specified object.
 */
static void
numaio_link_affinity(numaio_object_t *obj, numaio_affinity_t *aff)
{
	numaio_affinity_link_t *aff_link;

	aff_link = kmem_zalloc(sizeof (numaio_affinity_link_t), KM_SLEEP);

	aff_link->afl_affinity = aff;
	list_insert_tail(&obj->afo_affinities, aff_link);
}

/*
 * If one of the objects is a proc object, it is an additional hint
 * as to where the other object should be bound even when there is
 * an existing constraint.
 */
static void
numaio_set_proc_hint(numaio_object_t *obj1, numaio_object_t *obj2)
{
	numaio_object_t *proc_obj, *other_obj = NULL;
	numaio_affinity_link_t *aff_link;
	numaio_affinity_t *aff;

	if (obj1->afo_type == NUMAIO_OBJ_PROCINFO) {
		proc_obj = obj1;
		other_obj = obj2;
	} else if (obj2->afo_type == NUMAIO_OBJ_PROCINFO) {
		proc_obj = obj2;
		other_obj = obj1;
	}

	/*
	 * If other_obj is non-NULL, then save the lgrp_hint passed
	 * in the proc object in afo_lgrp_hint. Then mark every
	 * object associated with other_obj with the passed proc
	 * hint. This needs to be done only when an object gets
	 * associated with a proc object.
	 *
	 * For the 'else' cases, if one of the object has LGRP_HINT
	 * set, then associate this hint with the new object too.
	 */
	if (other_obj != NULL) {
		other_obj->afo_flags |= NUMAIO_AFO_LGRP_HINT;
		other_obj->afo_lgrp_hint = proc_obj->afo_proc;
		aff_link = list_head(&other_obj->afo_affinities);
		while (aff_link != NULL) {
			aff = aff_link->afl_affinity;
			if (aff->afa_obj1 == other_obj) {
				obj2 = aff->afa_obj2;
			} else {
				obj2 = aff->afa_obj1;
				ASSERT(aff->afa_obj2 == other_obj);
			}
			if (obj2->afo_type != NUMAIO_OBJ_DEVINFO &&
			    obj2->afo_type != NUMAIO_OBJ_PROCINFO) {
				/*
				 * At present we only have CPU
				 * and SOCKET affinities, so setting the
				 * lgrp_hint without checking for
				 * strength.
				 */
				obj2->afo_flags |= NUMAIO_AFO_LGRP_HINT;
				obj2->afo_lgrp_hint = other_obj->afo_lgrp_hint;
			}
			aff_link = list_next(&other_obj->afo_affinities,
			    aff_link);
		}
	} else if (obj1->afo_flags & NUMAIO_AFO_LGRP_HINT) {
		obj2->afo_flags |= NUMAIO_AFO_LGRP_HINT;
		obj2->afo_lgrp_hint = obj1->afo_lgrp_hint;
	} else if (obj2->afo_flags & NUMAIO_AFO_LGRP_HINT) {
		obj1->afo_flags |= NUMAIO_AFO_LGRP_HINT;
		obj1->afo_lgrp_hint = obj2->afo_lgrp_hint;
	}
}

static void
numaio_apply_affinity(numaio_object_t *obj1, numaio_object_t *obj2,
    numaio_affinity_t *aff)
{
	numaio_constraint_t *constraint;
	cpu_t *cp;
	processorid_t cpuid;
	uint64_t lgrp_hint = 0;
	boolean_t excl_flag;

	excl_flag = OBJ_HAS_DEDICATED_CPU(obj2);
	constraint = ((obj2->afo_grp != NULL) ?
	    obj2->afo_grp->afg_constraint : NULL);
	lgrp_hint = obj2->afo_lgrp_hint;

	if (aff->afa_strength == NUMAIO_AFF_STRENGTH_CPU) {
		cpuid = obj1->afo_cpuid;
	} else {
		/*
		 * We default to socket strength affinity.
		 */
		excl_flag = OBJ_HAS_DEDICATED_CPU(obj2);
		/*
		 * If lgrp hint is 0, we need to get a CPU
		 * which is within the socket/lgrp as
		 * object1's (obj) CPU.
		 */
		if (lgrp_hint == 0 &&
		    (cp = numaio_cpu_get(obj1->afo_cpuid)) != NULL) {
			klgrpset_add(lgrp_hint, cp->cpu_lpl->lpl_lgrpid);
		}
		constraint = ((obj2->afo_grp != NULL) ?
		    obj2->afo_grp->afg_constraint : NULL);
		cpuid = numaio_get_next_cpu(obj2->afo_grp, constraint,
		    B_FALSE, lgrp_hint, cp, excl_flag);
	}

	if (obj2->afo_cpuid != cpuid)
		numaio_object_bind_async(obj2, cpuid);
}

/*
 * aff_set_affinity() and aff_clear_affinity() call this function when
 * AFF_BIND_DEFER flag is not specified. It binds the two objects
 * depending upon the affinity strength and the lgrp hint associated
 * with the objects.
 */
static void
numaio_bind_two_objects(numaio_object_t *obj1, numaio_object_t *obj2,
    numaio_affinity_t *aff)
{
	numaio_constraint_t *constraint;
	processorid_t cpuid;
	uint64_t lgrp_hint = 0;
	boolean_t excl_flag;

	ASSERT(RW_WRITE_HELD(&numaio_glock));

	if (obj1->afo_flags &  NUMAIO_AFO_LGRP_HINT)
		lgrp_hint = obj1->afo_lgrp_hint;
	else if (obj2->afo_flags & NUMAIO_AFO_LGRP_HINT)
		lgrp_hint = obj2->afo_lgrp_hint;

	if (obj1->afo_type == NUMAIO_OBJ_DEVINFO ||
	    obj1->afo_type == NUMAIO_OBJ_PROCINFO) {
		obj1 = NULL;
	}

	if (obj2->afo_type == NUMAIO_OBJ_DEVINFO ||
	    obj2->afo_type == NUMAIO_OBJ_PROCINFO) {
		obj2 = NULL;
	}

	if (obj1 == NULL && obj2 == NULL)
		return;

	/*
	 * Check if CPU is already assigned to one of the objects.
	 * If so, then use that to apply affinity to the other
	 * object.
	 */
	if (obj1->afo_cpuid != -1) {
		numaio_apply_affinity(obj1, obj2, aff);
		return;
	} else if (obj2->afo_cpuid != -1) {
		numaio_apply_affinity(obj2, obj1, aff);
		return;
	}

	/*
	 * Both the objects do not have a CPU assigned. Get a
	 * CPU to obj1 and then obj2.
	 */
	excl_flag = OBJ_HAS_DEDICATED_CPU(obj1);
	constraint = ((obj1->afo_grp != NULL) ?
	    obj1->afo_grp->afg_constraint : NULL);
	cpuid = numaio_get_next_cpu(obj1->afo_grp, constraint, B_FALSE,
	    lgrp_hint, NULL, excl_flag);
	if (obj1->afo_cpuid != cpuid)
		numaio_object_bind_async(obj1, cpuid);

	numaio_apply_affinity(obj1, obj2, aff);
}

void
numaio_set_affinity(numaio_object_t *obj1, numaio_object_t *obj2,
    numaio_aff_strength_t strength, uint_t flags)
{
	numaio_affinity_link_t *aff_link;
	numaio_affinity_t *aff;

	/*
	 * If both objects have prior affinities, check if the call
	 * is just modifying the strength between the two objects.
	 */
	mutex_enter(&cpu_lock);
	rw_enter(&numaio_glock, RW_WRITER);
	if (obj1->afo_aff_refcnt > 0 && obj2->afo_aff_refcnt > 0) {
		for (aff_link = list_head(&obj1->afo_affinities);
		    aff_link != NULL; aff_link =
		    list_next(&obj1->afo_affinities, aff_link)) {
			aff = aff_link->afl_affinity;
			if ((aff->afa_obj1 == obj1 && aff->afa_obj2 == obj2) ||
			    (aff->afa_obj2 == obj1 && aff->afa_obj1 == obj2)) {
				/*
				 * update the affinity between these 2 objects
				 */
				if (aff->afa_strength == strength) {
					rw_exit(&numaio_glock);
					mutex_exit(&cpu_lock);
					return;
				}
				aff->afa_strength = strength;
				goto bind_objects;
			}
		}
	}

	aff = numaio_create_affinity();

	aff->afa_obj1 = obj1;
	aff->afa_obj2 = obj2;

	obj1->afo_aff_refcnt++;
	obj2->afo_aff_refcnt++;

	ASSERT(strength == NUMAIO_AFF_STRENGTH_CPU ||
	    strength == NUMAIO_AFF_STRENGTH_SOCKET);
	aff->afa_strength = strength;


	/* link the affinity with its two objects */
	numaio_link_affinity(obj1, aff);
	numaio_link_affinity(obj2, aff);

	numaio_set_proc_hint(obj1, obj2);

bind_objects:
	if (flags & NUMAIO_FLAG_BIND_DEFER) {
		rw_exit(&numaio_glock);
		mutex_exit(&cpu_lock);
		return;
	}

	/*
	 * If objects ask for dedicated CPUs, it will not work in this
	 * case. This is because the call to numaio_get_next_cpu() from
	 * aff_bind_two_objects() will not be able to find a CPU
	 * reserved for exclusive use (Exclusive CPUs have
	 * AFC_CPU_EXCL_RESERVED bit set). The CPUs for exclusive use is
	 * reserved first with the call to aff_reserve_excl_cpus() and
	 * this presently works only for objects that are grouped
	 * together and use numaio_group_map() call.
	 */
	numaio_bind_two_objects(obj1, obj2, aff);

	rw_exit(&numaio_glock);
	mutex_exit(&cpu_lock);
}

static void
numaio_clear_affinity_locked(numaio_object_t *obj1, numaio_object_t *obj2,
    uint_t flags)
{
	numaio_affinity_link_t *aff_link, *aff_link2, *nxt_link;
	numaio_affinity_t *aff;
	boolean_t clear_all = B_FALSE;

	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(RW_WRITE_HELD(&numaio_glock));

	if (obj1 == NULL && obj2 != NULL) {
		obj1 = obj2;
		obj2 = NULL;
	}

	if (obj2 == NULL)
		clear_all = B_TRUE;

	/*
	 * Walk the list of affinities of obj1 and obj2 and remove  the
	 * affinity element between these two objects.
	 */
	nxt_link = list_head(&obj1->afo_affinities);
	while (nxt_link != NULL) {
		aff_link = nxt_link;
		nxt_link = list_next(&obj1->afo_affinities, aff_link);
		aff = aff_link->afl_affinity;
		if (clear_all) {
			if (aff->afa_obj1 == obj1)
				obj2 = aff->afa_obj2;
			else
				obj2 = aff->afa_obj1;
		}

		if (aff->afa_obj1 == obj1 && aff->afa_obj2 == obj2 ||
		    aff->afa_obj1 == obj2 && aff->afa_obj2 == obj1) {

			/* look for this affinity in the obj2 list */
			aff_link2 = list_head(&obj2->afo_affinities);
			while (aff_link2 != NULL) {
				if (aff_link2->afl_affinity == aff)
					break;
				aff_link2 = list_next(&obj2->afo_affinities,
				    aff_link2);
			}

			ASSERT(aff_link2 != NULL);

			/* unlink affinity from the obj1 list */
			list_remove(&obj1->afo_affinities, aff_link);
			kmem_free(aff_link, sizeof (numaio_affinity_link_t));

			/* unlink affinity from the obj2 list */
			list_remove(&obj2->afo_affinities, aff_link2);
			kmem_free(aff_link2, sizeof (numaio_affinity_link_t));

			numaio_destroy_affinity(aff);
			obj1->afo_aff_refcnt--;
			obj2->afo_aff_refcnt--;

			/*
			 * if object does not have any affinities, then
			 * unbind the the object.
			 */
			if (!(flags & NUMAIO_FLAG_BIND_DEFER)) {
				if (obj1->afo_aff_refcnt == 0)
					numaio_object_unbind(obj1);
				if (obj2->afo_aff_refcnt == 0)
					numaio_object_unbind(obj2);
			}

			if (!clear_all)
				break;
		}
	}
}

/*
 * Clear affinity between obj1 and obj2.
 * If one of the objects is NULL, then clear all affinities that the
 * non-NULL object has with other objects.
 */
void
numaio_clear_affinity(numaio_object_t *obj1, numaio_object_t *obj2,
    uint_t flags)
{
	mutex_enter(&cpu_lock);
	rw_enter(&numaio_glock, RW_WRITER);
	numaio_clear_affinity_locked(obj1, obj2, flags);
	rw_exit(&numaio_glock);
	mutex_exit(&cpu_lock);
}

/*
 * Constraints.
 */

numaio_constraint_t *
numaio_constraint_create(void)
{
	numaio_constraint_t *constraint;

	constraint = kmem_zalloc(sizeof (numaio_constraint_t), KM_SLEEP);

	return (constraint);
}

void
numaio_constraint_destroy(numaio_constraint_t *constraint)
{
	int afl_ncpus, i;

	for (i = 0; i < NLGRPS_MAX; i++) {
		if ((afl_ncpus =
		    constraint->afc_lgrp[i].afl_ncpus) != 0) {
			kmem_free(constraint->afc_lgrp[i].afl_cpus,
			    sizeof (numaio_cpuid_t) * afl_ncpus);
		}
	}
	kmem_free(constraint, sizeof (numaio_constraint_t));
}

static int
numaio_populate_constraint(numaio_constraint_t *constraint, processorid_t *cpus,
    size_t ncpus)
{
	int i, afl_ncpus, afl_cpu_index;
	cpu_t *cp;
	lgrp_id_t lgrpid;
	numaio_cpuid_t *aff_cpup;

	/*
	 * Three iterations of 'for' loop here.
	 * The CPU list may contain CPUs belonging to different
	 * lgrps. Go through the list and identify the lgroup
	 * (numaio_lgrp_t) to which the CPU belongs and then
	 * increment afl_ncpus count. At the same time, note
	 * down how many numaio_lgrp_t contain CPUs
	 * (in afc_nlgrps).
	 */
	mutex_enter(&cpu_lock);
	for (i = 0; i < ncpus; i++) {
		if ((cp = numaio_cpu_get(cpus[i])) == NULL)
			continue;

		lgrpid = cp->cpu_lpl->lpl_lgrpid;
		if (constraint->afc_lgrp[lgrpid].afl_ncpus == 0)
			constraint->afc_nlgrps++;
		constraint->afc_lgrp[lgrpid].afl_ncpus++;
	}
	mutex_exit(&cpu_lock);
	if (constraint->afc_nlgrps == 0)
		return (EINVAL);
	/*
	 * Allocate memory to store the cpuids using afl_ncpus obtained
	 * from the first 'for' loop.
	 */
	for (i = 0; i < NLGRPS_MAX; i++) {
		if ((afl_ncpus = constraint->afc_lgrp[i].afl_ncpus) != 0) {
			constraint->afc_lgrp[i].afl_cpus =
			    kmem_zalloc(sizeof (numaio_cpuid_t) * afl_ncpus,
			    KM_SLEEP);
		}
	}

	/*
	 * Now store the CPUs themselves in each of the lgroup to which
	 * they belong. afl_cpu_index is (mis)used (but that is ok) to keep
	 * track of where to add each new cpuid in the afl_cpus[] array.
	 */
	mutex_enter(&cpu_lock);
	for (i = 0; i < ncpus; i++) {
		if ((cp = numaio_cpu_get(cpus[i])) == NULL)
			continue;

		lgrpid = cp->cpu_lpl->lpl_lgrpid;
		afl_cpu_index = constraint->afc_lgrp[lgrpid].afl_cpu_index;
		aff_cpup = constraint->afc_lgrp[lgrpid].afl_cpus;
		aff_cpup[afl_cpu_index].nc_cpuid = cpus[i];
		/*
		 * In the case of implicit constraint, if the CPU is not
		 * part of default partition, then mark is as not
		 * available.
		 */
		if (constraint->afc_type == NUMAIO_CONSTRAINT_CPUS &&
		    constraint->afc_flags & NUMAIO_CONSTRAINT_IMPLICIT &&
		    cp->cpu_part != &cp_default) {
			aff_cpup[afl_cpu_index].nc_flags = AFC_CPU_NOT_AVAIL;
		}

		constraint->afc_lgrp[lgrpid].afl_cpu_index++;
		/*
		 * Once the afl_ncpus worth of CPUs are filled up,
		 * reset afl_cpu_index back to 0.
		 */
		if (constraint->afc_lgrp[lgrpid].afl_cpu_index ==
		    constraint->afc_lgrp[lgrpid].afl_ncpus) {
			constraint->afc_lgrp[lgrpid].afl_cpu_index = 0;
		}
	}
	mutex_exit(&cpu_lock);
	/*
	 * Initialize afc_lgrp_index to an lgroup that has CPUs in it.
	 */
	i = 0;
	while (constraint->afc_lgrp[i].afl_ncpus == 0)
		i++;
	constraint->afc_lgrp_index = i;

	return (0);
}

int
numaio_constraint_set_pool(numaio_constraint_t *constraint, char *pool_name,
    boolean_t delayed_ok)
{
	cpupart_t *cpupart;
	processorid_t *cpuid_list;
	cpu_t *c;
	pool_t *pool;
	int i, ret_val = EINVAL;

	_NOTE(ARGUNUSED(delayed_ok));

	/*
	 * Pool default could be NULL if pool is not enabled or
	 * has not been brought online.
	 */
	pool_lock();
	pool = pool_lookup_pool_by_name(pool_name);
	pool_unlock();

	if (pool != NULL) {
		cpuid_list =
		    kmem_zalloc(max_ncpus * sizeof (processorid_t), KM_SLEEP);

		mutex_enter(&cpu_lock);
		rw_enter(&numaio_glock, RW_WRITER);
		constraint->afc_type = NUMAIO_CONSTRAINT_POOL;
		(void) strlcpy(constraint->afc_pool_name, pool_name,
		    sizeof (constraint->afc_pool_name));
		rw_exit(&numaio_glock);

		cpupart = cpupart_find(pool->pool_pset->pset_id);
		c = cpupart->cp_cpulist;
		for (i = 0; i < cpupart->cp_ncpus; i++) {
			cpuid_list[i] = c->cpu_id;
			c = c->cpu_next_part;
		}

		constraint->afc_pool_cpupart = cpupart;
		mutex_exit(&cpu_lock);
	}

	if (pool != NULL) {
		ret_val = numaio_populate_constraint(constraint,
		    cpuid_list, cpupart->cp_ncpus);
		kmem_free(cpuid_list, max_ncpus * sizeof (processorid_t));
	}

	return (ret_val);
}

int
numaio_constraint_set_cpus(numaio_constraint_t *constraint, processorid_t *cpus,
    size_t ncpus)
{
	constraint->afc_type = NUMAIO_CONSTRAINT_CPUS;
	return (numaio_populate_constraint(constraint, cpus, ncpus));
}

void
numaio_constraint_apply_group(numaio_constraint_t *constraint,
    numaio_group_t *grp, uint_t flags)
{
	rw_enter(&numaio_glock, RW_WRITER);
	if (grp->afg_constraint_self) {
		numaio_constraint_destroy(grp->afg_constraint);
		grp->afg_constraint_self = B_FALSE;
	}
	grp->afg_constraint = constraint;
	rw_exit(&numaio_glock);

	if (!(flags & NUMAIO_FLAG_BIND_DEFER))
		numaio_group_map(grp);
}

/* ARGSUSED */
void
numaio_constraint_clear_group(numaio_constraint_t *constraint,
    numaio_group_t *grp, uint_t flags)
{
	rw_enter(&numaio_glock, RW_WRITER);

	ASSERT(grp->afg_constraint != NULL);
	grp->afg_constraint = NULL;

	rw_exit(&numaio_glock);

	if (!(flags & NUMAIO_FLAG_BIND_DEFER))
		numaio_group_map(grp);
}

/*
 * Returns a proc cookie corresponding to that proc. Currently the cookie
 * consists of the lgrp associated with the specific proc. In the future,
 * we may want to hold the proc directly.
 */
numaio_proc_cookie_t
numaio_get_proc_cookie(proc_t *pp)
{
	return (pp->p_lgrpset);
}

/*
 * Interrupt objects need to drop cpu_lock before calling set_intr_affinity().
 * Since cpu_lock cannot be dropped during call from CPU callback functions
 * (numaio_object_walk_and_bind() and numaio_object_walk_and_unbind()), this
 * function which is dispatched via taskq takes care of it.
 */
static void
numaio_object_bind_taskq(numaio_object_t *obj)
{
	cpu_t *cp;
	processorid_t cpuid;
	numaio_constraint_t *constraint;
	int retval = 0;

	mutex_enter(&cpu_lock);
	if ((cpuid = obj->afo_cpuid) == -1)
		goto bail_out;
	mutex_exit(&cpu_lock);

again:
	if (!(obj->afo_flags & NUMAIO_AFO_TEST_ONLY)) {
		bal_numa_managed_int(obj->afo_intr, cpuid);
		retval = set_intr_affinity(obj->afo_intr, cpuid);
	}

	mutex_enter(&cpu_lock);
	if (obj->afo_cpuid == -1) {
		/*
		 * Some other thread unbound that cpu while we
		 * had released the cpu lock. No need to call
		 * bal_numa_managed_clear() or decrement the
		 * CPU ref count here as function that set
		 * afo_cpuid to -1 would have done that.
		 */
		goto bail_out;
	}

	if (retval != 0) {
		/*
		 * Retageting failed, decrement cpu ref count that
		 * we incremented in numaio_object_bind_async().
		 * Do this only if some other thread has not
		 * already set afo_cpuid to -1 and decremented the
		 * CPU ref count.
		 */
		bal_numa_managed_clear(obj->afo_intr, cpuid);
		obj->afo_cpuid = -1;
		cp = numaio_cpu_get(cpuid);
		if (cp != NULL)
			numaio_decrement_cpu(obj, cp);

		/* Reset excl flag */
		rw_enter(&numaio_glock, RW_WRITER);
		constraint = (obj->afo_grp != NULL) ?
		    obj->afo_grp->afg_constraint : NULL;
		numaio_constraint_reset_cpu(constraint,
		    cpuid, OBJ_HAS_DEDICATED_CPU(obj));
		rw_exit(&numaio_glock);
	} else {
		if (obj->afo_cpuid != cpuid) {
			/*
			 * When we dropped cpu_lock to execute
			 * set_intr_affinity(), the object asked to
			 * retarget the interrupt to a different CPU.
			 */
			bal_numa_managed_clear(obj->afo_intr, cpuid);
			cpuid = obj->afo_cpuid;
			mutex_exit(&cpu_lock);
			goto again;
		}
	}
bail_out:
	mutex_enter(&obj->afo_lock);
	obj->afo_taskqid = NULL;
	obj->afo_ref--;
	if (obj->afo_ref == 0)
		cv_signal(&obj->afo_cv);
	mutex_exit(&obj->afo_lock);
	mutex_exit(&cpu_lock);
}

/*
 * We cannot directly call numaio_object_bind_to_cpu() for DDI_INTR
 * object type functions because we would need to drop cpu_lock
 * before calling set_intr_affinity(). So we start a taskq thread
 * to the bind in a separate context.
 */
static void
numaio_object_bind_async(numaio_object_t *obj, processorid_t cpuid)
{
	cpu_t *cp, *prev_cp;

	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(RW_WRITE_HELD(&numaio_glock));

	if (obj->afo_cpuid != -1) {
		/*
		 * Interrupt was previously re-targeted.
		 * Decrement cpu ref on afo_cpuid.
		 */
		prev_cp = numaio_cpu_get(obj->afo_cpuid);
		if (prev_cp != NULL)
			numaio_decrement_cpu(obj, prev_cp);

		if (obj->afo_type == NUMAIO_OBJ_KTHREAD)
			thread_affinity_clear(obj->afo_kthread);

		obj->afo_cpuid = -1;
	}

	if (!numaio_bind_objects)
		return;

	/*
	 * cpuid could be -1 in which case numaio_cpu_get()
	 * will return NULL
	 */
	if ((cp = numaio_cpu_get(cpuid)) == NULL)
		return;

	if (obj->afo_type == NUMAIO_OBJ_DDI_INTR) {
		mutex_enter(&obj->afo_lock);
		obj->afo_cpuid = cpuid;
		numaio_increment_cpu(obj, cp);
		if (obj->afo_taskqid == NULL) {
			obj->afo_taskqid = taskq_dispatch(system_taskq,
			    (task_func_t *)numaio_object_bind_taskq,
			    obj, TQ_NOSLEEP);
			if (obj->afo_taskqid != NULL) {
				obj->afo_ref++;
			} else {
				DTRACE_PROBE1(numaio__taskq__fail,
				    numaio_object_t *, obj);
				obj->afo_cpuid = -1;
				numaio_decrement_cpu(obj, cp);
			}
		}
		mutex_exit(&obj->afo_lock);
	} else {
		numaio_object_bind_to_cpu(obj, cpuid);
	}
}

/*
 * CPU callback is informing addition of a CPU to a partition.
 * Add the CPU to the constraint and do a group map.
 */
static void
numaio_map_new_cpu(numaio_group_t *grp, cpu_t *cp)
{
	lgrp_id_t lgrpid;
	processorid_t cpuid;
	numaio_constraint_t *constraint;
	numaio_cpuid_t *aff_cpup, *cpu_list;
	int afl_ncpus, i;
	boolean_t need_map = B_FALSE;

	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(RW_WRITE_HELD(&numaio_glock));

	constraint = grp->afg_constraint;
	/*
	 * If the group does not have a constraint, then no
	 * need to do anything special. Just return.
	 */
	if (constraint == NULL)
		return;

	lgrpid = cp->cpu_lpl->lpl_lgrpid;
	cpuid = cp->cpu_id;

	aff_cpup = constraint->afc_lgrp[lgrpid].afl_cpus;
	afl_ncpus = constraint->afc_lgrp[lgrpid].afl_ncpus;


	/*
	 * In the case of implicit CPU constraint, if the CPU
	 * belongs to an lgroup that is part of the constraint,
	 * then add the CPU to the implicit constraint. Do this
	 * only if the CPU belongs to the default partition
	 * (cp_default).
	 *
	 * If it is an explicit constraint, check if the CPU is
	 * part of the constraint. If it is, then unset
	 * AFC_CPU_NOT_AVAIL flag.
	 */
	if ((constraint->afc_type == NUMAIO_CONSTRAINT_CPUS &&
	    constraint->afc_flags & NUMAIO_CONSTRAINT_IMPLICIT &&
	    cp->cpu_part == &cp_default) || (constraint->afc_type ==
	    NUMAIO_CONSTRAINT_CPUS &&
	    !(constraint->afc_flags & NUMAIO_CONSTRAINT_IMPLICIT))) {
		/*
		 * Check if cpuid is present in the constraint. If
		 * present, then unset NOT_AVAIL flag.
		 */
		for (i = 0; i < afl_ncpus; i++) {
			if (aff_cpup[i].nc_cpuid == cpuid) {
				aff_cpup[i].nc_flags &= ~AFC_CPU_NOT_AVAIL;
				need_map = B_TRUE;
				break;
			}
		}

		if (need_map) {
			numaio_group_map_locked(grp);
			return;
		}
	}

	if (constraint->afc_type == NUMAIO_CONSTRAINT_POOL &&
	    cp->cpu_part == constraint->afc_pool_cpupart) {
		/*
		 * Check if cpuid is present in the constraint. If
		 * present, then unset NOT_AVAIL flag.
		 */
		for (i = 0; i < afl_ncpus; i++) {
			if (aff_cpup[i].nc_cpuid == cpuid) {
				aff_cpup[i].nc_flags &= ~AFC_CPU_NOT_AVAIL;
				need_map = B_TRUE;
				break;
			}
		}

		/*
		 * If need_map is not TRUE, then a new CPU is added to
		 * the partition. Allocate memory to add the new CPU
		 * in the constraint.
		 */
		if (!need_map) {
			/*
			 * Since this is called from CPU callback function,
			 * call kmem_zalloc() with KM_NOSLEEP.
			 */
			cpu_list = kmem_zalloc(sizeof (numaio_cpuid_t) *
			    (afl_ncpus + 1), KM_NOSLEEP);
			/*
			 * If kmem_zalloc() returns NULL, we won't be
			 * considering the new CPU in the constraint.
			 * It is not the end of the world.
			 */
			if (cpu_list == NULL)
				return;

			/* Copy cpus from the aff_cpup to cpu_list */
			for (i = 0; i < afl_ncpus; i++)
				cpu_list[i].nc_cpuid = aff_cpup[i].nc_cpuid;

			/* Add the new CPU at the end of the list */
			cpu_list[i].nc_cpuid = cpuid;

			if (aff_cpup != NULL) {
				kmem_free(constraint->afc_lgrp[lgrpid].afl_cpus,
				    sizeof (numaio_cpuid_t) * afl_ncpus);
			}

			constraint->afc_lgrp[lgrpid].afl_cpus = cpu_list;
			constraint->afc_lgrp[lgrpid].afl_ncpus++;
		}

		numaio_group_map_locked(grp);
		return;
	}
}

static void
numaio_object_walk_and_bind(processorid_t cpuid)
{
	numaio_group_t *grp;
	cpu_t *cp;

	ASSERT(MUTEX_HELD(&cpu_lock));
	rw_enter(&numaio_glock, RW_WRITER);

	/*
	 * When CPU subsystem calls numaio_cpu_setup() to notify of
	 * a CPU coming online, it may not have set CPU_ENABLE flag
	 * yet. It will set this bit only after informing the
	 * registered callbacks. So Use cpu_get() instead of
	 * numaio_cpu_get() here.
	 */
	cp = cpu_get(cpuid);

	/*
	 * Walk all numa groups and do re-mapping of this CPU
	 * depending up the group's constraint type.
	 *
	 * If the group does not have a constraint, then no
	 * need to do anything special. Just return.
	 *
	 * In the case of implicit CPU constraint, if the CPU
	 * belongs to an lgroup that is part of the constraint,
	 * then add the CPU to the implicit constraint. Do this
	 * only if the CPU belongs to the default partition
	 * (cp_default).
	 *
	 * Same will apply in case of pool constraint. The CPU
	 * will be added to the constraint if it is part of the
	 * CPU partition that pool belongs to.
	 *
	 * If it is an explicit constraint, check if the CPU is
	 * part of the constraint. If it is, then unset
	 * AFC_CPU_NOT_AVAIL flag.
	 */
	for (grp = list_head(&numaio_group_glist);
	    grp != NULL; grp = list_next(&numaio_group_glist, grp)) {
		numaio_map_new_cpu(grp, cp);
	}

	rw_exit(&numaio_glock);
}

/*
 * CPU callback is informing removal of a CPU from a partition.
 * Remove the CPU from the constraint and do a group map.
 */
static void
numaio_unmap_a_cpu(numaio_group_t *grp, processorid_t cpuid)
{
	lgrp_id_t lgrpid;
	numaio_constraint_t *constraint;
	numaio_cpuid_t *aff_cpup;
	int afl_ncpus, i;
	cpu_t *new_cp;
	numaio_object_t *obj;

	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(RW_WRITE_HELD(&numaio_glock));

	constraint = grp->afg_constraint;
	/*
	 * If the group does not have a constraint, then walk the
	 * objects in the group and move away any object bound to
	 * the CPU to a different CPU.
	 */
	if (constraint == NULL) {
		new_cp =
		    numaio_find_best_cpu(grp, numaio_cpu_get(cpuid), B_TRUE);

		for (obj = list_head(&grp->afg_objects); obj != NULL;
		    obj = list_next(&grp->afg_objects, obj)) {
			if (obj->afo_cpuid == cpuid)
				numaio_object_bind_async(obj,
				    new_cp != NULL ? new_cp->cpu_id : -1);
		}
		return;
	}

	for (lgrpid = 0; lgrpid < NLGRPS_MAX; lgrpid++) {
		/*
		 * If no CPUs are present in afc_lgrp[], then
		 * there is no CPU to remove.
		 */
		if (constraint->afc_lgrp[lgrpid].afl_ncpus == 0)
			continue;

		aff_cpup = constraint->afc_lgrp[lgrpid].afl_cpus;
		afl_ncpus = constraint->afc_lgrp[lgrpid].afl_ncpus;
		/*
		 * Confirm that cpuid is indeed present in the constraint.
		 * If present, then mark it as not available.
		 */
		for (i = 0; i < afl_ncpus; i++) {
			if (aff_cpup[i].nc_cpuid == cpuid) {
				aff_cpup[i].nc_flags |= AFC_CPU_NOT_AVAIL;
				break;
			}
		}

		/* we found our CPU */
		if (i < afl_ncpus) {
			/* map the new group */
			numaio_group_map_locked(grp);
			break;
		}
	}
}

static void
numaio_object_walk_and_unbind(processorid_t cpuid)
{
	numaio_group_t *grp;

	ASSERT(MUTEX_HELD(&cpu_lock));
	rw_enter(&numaio_glock, RW_WRITER);

	/*
	 * Walk all numa groups and move objects if they are bound
	 * to this CPU. Additionally depending upon the constraint
	 * type, the CPU may need to be removed from constraint.
	 *
	 * If the group does not have a constraint, then walk the
	 * objects in the group and move away any object bound to
	 * the CPU to a different CPU.
	 *
	 * In the case of implicit CPU constraint, the CPU should
	 * be removed from the constraint. Same will apply in case
	 * of pool constraint. The CPU will be removed from the
	 * CPU partition and it should be reflected (by its
	 * removal) in the constraint.
	 *
	 * If it is an explicit constraint, mark the CPU as not
	 * available but don't remove it from the constraint. Later
	 * when it becomes available, it should be marked as
	 * available for use.
	 */
	for (grp = list_head(&numaio_group_glist);
	    grp != NULL; grp = list_next(&numaio_group_glist, grp)) {
		numaio_unmap_a_cpu(grp, cpuid);
	}

	rw_exit(&numaio_glock);
}

/* ARGSUSED */
static int
numaio_cpu_setup(cpu_setup_t what, int id, void *arg)
{
	cpu_t *cp;

	cp = cpu_get(id);
	ASSERT(MUTEX_HELD(&cpu_lock));
	switch (what) {
	case CPU_CONFIG:
	case CPU_ON:
	case CPU_CPUPART_IN:
		if (cp->cpu_numaio_info == NULL) {
			cp->cpu_numaio_info =
			    kmem_zalloc(sizeof (numaio_info_t), KM_SLEEP);
		}

		numaio_object_walk_and_bind(id);
		break;

	case CPU_UNCONFIG:
	case CPU_OFF:
	case CPU_CPUPART_OUT:
		numaio_object_walk_and_unbind(id);
		if (cp->cpu_numaio_info != NULL) {
			kmem_free(cp->cpu_numaio_info, sizeof (numaio_info_t));
			cp->cpu_numaio_info = NULL;
		}

		break;

	default:
		break;
	}
	return (0);
}

static void
i_numaio_get_effective_cpus(numaio_group_t *grp, processorid_t *cpus,
    size_t ncpus, size_t *rcount)
{
	numaio_object_t *obj;
	int i;

	*rcount = 0;
	for (obj = list_head(&grp->afg_objects); obj != NULL;
	    obj = list_next(&grp->afg_objects, obj)) {
		if (obj->afo_cpuid == -1)
			continue;

		for (i = 0; i < *rcount; i++) {
			if (cpus[i] == obj->afo_cpuid)
				break;
		}

		if (i == *rcount) {
			cpus[i] = obj->afo_cpuid;
			*rcount = *rcount + 1;
			if (*rcount == ncpus)
				break;
		}
	}
}

/*
 * Returns the effective CPUs that are used by the objects in this group.
 * The CPUs are stored in cpus[] (2nd argument) and CPU count in rcount
 * (4th argument).
 */
void
numaio_get_effective_cpus(numaio_group_t *grp, processorid_t *cpus,
    size_t ncpus, size_t *rcount)
{
	rw_enter(&numaio_glock, RW_READER);
	i_numaio_get_effective_cpus(grp, cpus, ncpus, rcount);
	rw_exit(&numaio_glock);
}

void
numaio_group_get_lgrp_cpu_count(numaio_group_t *grp,
    numaio_lgrps_cpus_t *numa_lgrp_cpu)
{
	numaio_constraint_t *constraint;
	int nlgrps, i;

	constraint = grp->afg_constraint;
	numa_lgrp_cpu->nlc_nlgrps = 0;

	if (constraint == NULL)
		return;

	nlgrps = 0;
	for (i = 0; i < NLGRPS_MAX; i++) {
		if (constraint->afc_lgrp[i].afl_ncpus == 0)
			continue;

		numa_lgrp_cpu->nlc_lgrp[nlgrps].lgc_lgrp_id = i;
		numa_lgrp_cpu->nlc_lgrp[nlgrps].lgc_lgrp_cpucnt =
		    constraint->afc_lgrp[i].afl_ncpus;
		nlgrps++;
	}

	numa_lgrp_cpu->nlc_nlgrps = nlgrps;
}

/* ARGSUSED */
static int
i_numaio_group_constructor(void *buf, void *arg, int kmflag)
{
	bzero(buf, sizeof (numaio_group_t));

	return (0);
}

/* ARGSUSED */
static void
i_numaio_group_destructor(void *buf, void *arg)
{}


/* ARGSUSED */
static int
i_numaio_object_constructor(void *buf, void *arg, int kmflag)
{
	numaio_object_t *obj = (numaio_object_t *)buf;

	bzero(buf, sizeof (numaio_object_t));
	list_create(&obj->afo_affinities, sizeof (numaio_affinity_link_t),
	    offsetof(numaio_affinity_link_t, afl_next));
	mutex_init(&obj->afo_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&obj->afo_cv, NULL, CV_DEFAULT, NULL);

	return (0);
}

/* ARGSUSED */
static void
i_numaio_object_destructor(void *buf, void *arg)
{}

void
numaio_init(numaio_init_stages_t stage)
{
	int i;

	switch (stage) {
	case NUMAIO_INIT_STAGE1:
		rw_init(&numaio_glock, NULL, RW_DRIVER, NULL);
		i_numaio_obj_cachep = kmem_cache_create("numaio_obj_cache",
		    sizeof (numaio_object_t), 0, i_numaio_object_constructor,
		    i_numaio_object_destructor, NULL, NULL, NULL, 0);
		ASSERT(i_numaio_obj_cachep != NULL);
		i_numaio_grp_cachep = kmem_cache_create("numaio_grp_cache",
		    sizeof (numaio_group_t), 0, i_numaio_group_constructor,
		    i_numaio_group_destructor, NULL, NULL, NULL, 0);
		ASSERT(i_numaio_grp_cachep != NULL);

		list_create(&numaio_object_glist, sizeof (numaio_object_t),
		    offsetof(numaio_object_t, afo_next_obj));
		list_create(&numaio_group_glist, sizeof (numaio_group_t),
		    offsetof(numaio_group_t, afg_next_grp));
		/* LINTED E_CASE_FALLTHRU */
	case NUMAIO_INIT_STAGE2:
		mutex_enter(&cpu_lock);
		for (i = 0; i < NCPU; i++) {
			cpu_t *cp = cpu_get(i);

			if (cp != NULL && cp->cpu_numaio_info == NULL) {
				cp->cpu_numaio_info =
				    kmem_zalloc(sizeof (numaio_info_t),
				    KM_SLEEP);
			}
		}

		if (stage == NUMAIO_INIT_STAGE1)
			register_cpu_setup_func(numaio_cpu_setup, NULL);
		mutex_exit(&cpu_lock);
		break;
	default:
		break;
	}
}

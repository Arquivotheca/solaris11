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

#ifndef	_SYS_NUMAIO_H
#define	_SYS_NUMAIO_H

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stream.h>
#include <sys/mkdev.h>
#include <sys/mac.h>
#include <sys/list.h>
#include <sys/lgrp.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * NUMA IO Affinity objects. An object instance is associated with
 * each entity to be mapped (kthread, interrupt), components of the
 * system (devices), etc. The rest of the API allows the caller to
 * specify affinities between these objects, and constraints for the
 * mapping of these objects on hardware.
 */

typedef struct numaio_object_s numaio_object_t;

/*
 * Objects can be optionally grouped. All objects of the same group
 * have an implicit NUMAIO_AFF_STRENGTH_SOCKET affinity with each other.
 * CPU constraints can be applied either on individual objects or
 * object groups. When a constraint is applied to a group, it is
 * applied to every object of the group.
 */

typedef struct numaio_group_s numaio_group_t;

/*
 * Affinity strength. Specifies the strength of the affinity between
 * two objects. Higher values means stronger affinity, i.e. closer
 * objects. The following affinities are currently supported, and more
 * may be added in the future (for example NUMAIO_AFF_STRENGTH_CORE
 * and NUMAIO_AFF_STRENGTH_MEMORY).
 *
 * NUMAIO_AFF_STRENGTH_SOCKET:	Objects should share the same CPU socket
 * NUMAIO_AFF_STRENGTH_CPU:	Objects should share the same hardware thread
 */

typedef enum numaio_aff_strength {
	NUMAIO_AFF_STRENGTH_SOCKET	= 50,
	NUMAIO_AFF_STRENGTH_CPU		= 100
} numaio_aff_strength_t;

/*
 * Each object or group of objects can be associated with an optional
 * name. The following defines the maximum length of that name,
 * including the terminating NULL character.
 */
#define	NUMAIO_MAX_NAME_LEN		32

/*
 * Constraints allow the caller to specify a subset of the CPUs that
 * should be used for mapping objects onto the underlying
 * hardware. A constraint can currently be specified as a list of CPU
 * ids. A constraint can be shared by multiple objects.
 */

typedef struct numaio_constraint_s numaio_constraint_t;

#define	NUMAIO_MAX_POOL_NAME		256

/*
 * The process cookie is specified as argument for creating an
 * affinity object corresponding to a process.
 */

typedef uintptr_t numaio_proc_cookie_t;

extern numaio_proc_cookie_t numaio_get_proc_cookie(proc_t *);

/*
 * Values for 'flags' argument in numaio_set_affinity() and
 * numaio_constraint_apply_group().
 */

#define	NUMAIO_FLAG_BIND_DEFER	0x01	/* defer binding until */
					/* numaio_group_map() */

/*
 * Flags passed to the numaio_object_create_*() functions.
 */

#define	NUMAIO_OBJECT_FLAG_DEDICATED_CPU	0x01
						/* object should have */
						/* its dedicated CPU */
#define	NUMAIO_OBJECT_FLAG_TEST_ONLY		0x02	/* object for test */
							/* module's use */
typedef struct numaio_lgrpid_cpucnt_s {
	int	lgc_lgrp_id;
	int	lgc_lgrp_cpucnt;
} numaio_lgrpid_cpucnt_t;

typedef struct numaio_lgrps_cpus_s {
	int			nlc_nlgrps;
	numaio_lgrpid_cpucnt_t	nlc_lgrp[NLGRPS_MAX];
} numaio_lgrps_cpus_t;

extern void numaio_group_get_lgrp_cpu_count(numaio_group_t *,
    numaio_lgrps_cpus_t *);

/*
 * Affinity objects manipulation.
 *
 * Each resource that needs to be bound is associated with an affinity
 * object. Affinity objects are created by the numaio_object_create*()
 * calls.
 *
 * The caller can associate each affinity object with an optional
 * name, which can be used for debugging and observability.
 */

extern numaio_object_t *numaio_object_create_thread(kthread_t *, const char *,
    uint_t);
extern numaio_object_t *numaio_object_create_dev_info(dev_info_t *,
    const char *, uint_t);
extern numaio_object_t *numaio_object_create_interrupt(ddi_intr_handle_t,
    const char *, uint_t);
extern numaio_object_t *numaio_object_create_proc(numaio_proc_cookie_t,
    const char *, uint_t);
extern numaio_group_t *numaio_object_get_group(numaio_object_t *);
extern void numaio_object_destroy(numaio_object_t *);

/*
 * Object groups management routines. See above for a comment on groups.
 */

extern numaio_group_t *numaio_group_create(const char *);
extern void numaio_group_add_object(numaio_group_t *, numaio_object_t *,
    uint_t);
extern void numaio_group_remove_object(numaio_group_t *, numaio_object_t *);
extern void numaio_group_remove_all_objects(numaio_group_t *);
extern void numaio_clear_group_reference(numaio_object_t *);
extern void numaio_group_destroy(numaio_group_t *);
extern void numaio_group_map(numaio_group_t *);

/*
 * Set the affinity between two objects. Multiple affinities can
 * be defined between two objects. All affinities between two objects
 * can be cleared with numaio_clear_affinity();
 */

extern void numaio_set_affinity(numaio_object_t *, numaio_object_t *,
    numaio_aff_strength_t, uint_t);
extern void numaio_clear_affinity(numaio_object_t *, numaio_object_t *, uint_t);

/*
 * Constraints specify a subset of CPUs which should be used to bind a
 * particular object or group of objects.  Multiple objects or groups
 * can share the same constraint.  An object that is part of a group
 * cannot have its own constraint.
 *
 * numaio_constraint_create() creates a constraint.
 * numaio_constraint_destroy() destroys a constraint.
 * numaio_constraint_set_cpus() associates a set of CPUs with a constraint.
 * numaio_constraint_set_pool() associates a CPU pool with a constraint.
 * numaio_constraint_apply_object() applies a constraint to an object.
 * numaio_constraint_apply_group() applies a constraint to a group of objects.
 * numaio_constraint_clear_group() removes a constraint from a numaio group.
 */

extern numaio_constraint_t *numaio_constraint_create();
extern void numaio_constraint_destroy(numaio_constraint_t *);
extern int numaio_constraint_set_pool(numaio_constraint_t *, char *, boolean_t);
extern int numaio_constraint_set_cpus(numaio_constraint_t *, processorid_t *,
    size_t);
extern void numaio_constraint_apply_group(numaio_constraint_t *,
    numaio_group_t *, uint_t);
extern void numaio_constraint_clear_group(numaio_constraint_t *,
    numaio_group_t *, uint_t);

extern void numaio_get_effective_cpus(numaio_group_t *, processorid_t *,
    size_t, size_t *);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_NUMAIO_H */

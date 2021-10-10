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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_NUMAIO_PRIV_H
#define	_SYS_NUMAIO_PRIV_H

#include <sys/numaio.h>
#include <sys/cpupart.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	GROUP_MAX_CPUS		256

/*
 * Groups of affinity objects. An object belongs in at most one group, and
 * objects of the same group are linked through the afo_field of the
 * object structure.
 */
struct numaio_group_s {
	char			afg_name[NUMAIO_MAX_NAME_LEN];
				/* list of numaio_object_t's in that group */
	list_t			afg_objects;
	numaio_constraint_t	*afg_constraint;
	boolean_t		afg_constraint_self;
	boolean_t		afg_mapped;
	list_node_t		afg_next_grp;
};

typedef enum {
	NUMAIO_OBJ_KTHREAD,
	NUMAIO_OBJ_DDI_INTR,
	NUMAIO_OBJ_DEVINFO,
	NUMAIO_OBJ_PROCINFO
} numaio_obj_type_t;


/* Values for afo_flags in numaio_object_t */
#define	NUMAIO_AFO_LGRP_HINT		0x01	/* object has a lgrp hint */
#define	NUMAIO_AFO_DEDICATED_CPU	0x02	/* object wants its own CPU */
#define	NUMAIO_AFO_TEST_ONLY		0x04	/* object for func. testing */

#define	OBJ_HAS_DEDICATED_CPU(obj)	(((obj)->afo_flags & \
    NUMAIO_AFO_DEDICATED_CPU) ? B_TRUE : B_FALSE)

/*
 * Objects.
 */
struct numaio_object_s {
	numaio_obj_type_t	afo_type;
	union {
		kthread_t	*kthread;
		dev_info_t	*dip;
		ddi_intr_handle_t intr;
		numaio_proc_cookie_t cookie;
	} afo_object;

	char			afo_name[NUMAIO_MAX_NAME_LEN];
	struct numaio_group_s	*afo_grp;
	int			afo_cpuid;
	/*
	 * afo_cpuid_saved saves the afo_cpuid in case the CPU is
	 * offined, removed, etc from a partition. Later if the
	 * CPU becomes available, the object is bound back to the
	 * saved CPU.
	 */
	int			afo_cpuid_saved;
	uint_t			afo_flags;
	uint64_t		afo_lgrp_hint;
	uint_t			afo_gen_num;
	int			afo_ref;
	int			afo_aff_refcnt;
				/* list of affinities with other objects */
	list_t			afo_affinities;
				/* next object in the group */
	list_node_t 		afo_next_grp_obj;
	list_node_t		afo_next_obj;
	taskqid_t		afo_taskqid;
	kmutex_t		afo_lock;
	kcondvar_t		afo_cv;
};

/*
 * numaio_constraint_s contains NLGRPS_MAX number of numaio_lgrp_t's.
 * All of these may not necessarily have CPUs in them. The ones with
 * afl_ncpus > 0 will have afl_ncpus worth of cpuids and these
 * cpuids are stored in a numaio_cpuid_t.  numaio_lgrp_t gets filled
 * with CPUs in numaio_constraint_set_cpus().
 *
 * afc_grpindex will point to the lgrp id from where CPUs will get
 * picked up on the next invocation of the numaio_get_next_cpu()
 * call. afc_nlgrps contains the number of numaio_lgrp_t structures
 * that have CPUs in them.
 */

/* Values that nc_flags can take */
#define	AFC_CPU_EXCL_RESERVED		0x01
#define	AFC_CPU_EXCL_TAKEN		0x02
#define	AFC_CPU_NOT_AVAIL		0x04

typedef struct numaio_cpuid_s {
	processorid_t	nc_cpuid;
	uint_t		nc_flags;
	uint_t		nc_dedi_count;
} numaio_cpuid_t;

typedef struct numaio_lgrp_s {
	size_t		afl_ncpus;	/* total number of CPUs available */
	numaio_cpuid_t	*afl_cpus;	/* CPUs and other stuff */
	int		afl_cpu_index;	/* points to next CPU to be returned */
	int		afl_ref;	/* threads/intrs bound to this lgrp */
} numaio_lgrp_t;

/*
 * Constraints specify a list of CPU which restricts the CPUs
 * to which affinity objects should be bound.
 */
typedef enum {
	NUMAIO_CONSTRAINT_NONE,
	NUMAIO_CONSTRAINT_CPUS,
	NUMAIO_CONSTRAINT_POOL
} numaio_constraint_type_t;

/*
 * Values that afc_flags in numaio_constraint_t can take.
 */
#define	NUMAIO_CONSTRAINT_IMPLICIT	0x01

struct numaio_constraint_s {
	numaio_constraint_type_t afc_type;
	uint_t			afc_flags;
	char			afc_pool_name[NUMAIO_MAX_POOL_NAME];
	cpupart_t		*afc_pool_cpupart;
	size_t			afc_nlgrps; /* lgrps in this constraint */
	numaio_lgrp_t		afc_lgrp[NLGRPS_MAX];
	int			afc_lgrp_index;
};

typedef struct numaio_info_s {
	int	ni_cpu_refcnt;
	uint_t	ni_cpu_flags;	/* like dedicated cpu, intr cpu, etc */
} numaio_info_t;

typedef enum numaio_init_stages {
	NUMAIO_INIT_STAGE1,
	NUMAIO_INIT_STAGE2
} numaio_init_stages_t;

extern void numaio_init(numaio_init_stages_t);
extern void numaio_dev_init(dev_info_t *);
extern void numaio_dev_fini(dev_info_t *);
extern int numaio_get_lgrp_info(dev_info_t *, lgrp_t ***, uint_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_NUMAIO_PRIV_H */

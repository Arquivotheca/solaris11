/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MTST_CPU_H
#define	_MTST_CPU_H

#include <mtst_list.h>
#include <mtst_cpumod_api.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mtst_cpu_info mtst_cpu_info_t;

typedef struct mtst_cpumod_impl {
	mtst_list_t mcpu_list;			/* list of CPU modules */
	void *mcpu_hdl;				/* dlopen() handle */
	const char *mcpu_name;			/* name of module */
	const mtst_cpumod_ops_t *mcpu_ops;	/* ops for this module */
} mtst_cpumod_impl_t;

extern mtst_cpu_info_t *mtst_cpuinfo_read_idtuple(uint64_t, uint64_t, uint64_t);
extern mtst_cpu_info_t *mtst_cpuinfo_read_logicalid(uint64_t);

extern void mtst_ntv_cpuinfo_destroy(void);

extern mtst_cpuid_t *mtst_cpuid(void);

extern void mtst_cpumod_load(void);
extern void mtst_cpumod_unload(void);

#ifdef __cplusplus
}
#endif

#endif /* _MTST_CPU_H */

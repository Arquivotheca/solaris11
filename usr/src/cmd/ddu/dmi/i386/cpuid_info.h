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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_CPUID_INFO_H
#define	_CPUID_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#define	INTEL_TYPE	0
#define	AMD_TYPE	1

/*
 * cpuid_data structure store CPUID information
 */
typedef struct cpuid_data {
	uint32_t	eax;
	uint32_t	ebx;
	uint32_t	ecx;
	uint32_t	edx;
} *cpuid_data_t;

/*
 * virtual_cpu_info structure define each system cpu id information.
 * cpu_id: index for each cpu.
 * core_id: cpu core id.
 * thread_id: cpu thread id.
 */
typedef struct virtual_cpu_info {
	int	cpu_id;
	int	core_id;
	int	thread_id;
} *virtual_cpu_info_t;

/*
 * processor_pkg_info structure define processor package information.
 *
 * name: processor name.
 * pkg_id: processor package id.
 * feature: processor feature information.
 * clock: processor clock.
 * pkg_type: processor type AMD or Intel.
 * num_core: number of core in processor package.
 * num_thread: number of thread in processor package.
 * max_core: max core number supported in processor package.
 * max_thread: max thread number supported in processor package.
 * bVM: Virtual Machine ability.
 * v_cpu: each cpu information in processor package.
 * next: next processor package.
 */
typedef struct processor_pkg_info {
	char		name[52];
	uint32_t	pkg_id;
	uint32_t	feature;
	int		clock;
	int		pkg_type;
	int		num_core;
	int		num_thread;
	int		max_core;
	int		max_thread;
	int		bVM;
	virtual_cpu_info_t	v_cpu;
	struct processor_pkg_info	*next;
} *processor_pkg_info_t;

int get_processor_name(char *cpu_name, int size);
uint32_t get_cpu_identifers();
uint16_t get_max_core_num();
uint16_t get_max_thread_num();
int get_cpu_type();
int cpuid_info(uint32_t func, cpuid_data_t pdata);
processor_pkg_info_t scan_cpu_info();
void free_cpu_info(processor_pkg_info_t proc_pkg_info);

#ifdef __cplusplus
}
#endif

#endif /* _CPUID_INFO_H */

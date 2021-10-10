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

/*
 * Get CPU information.
 * Include: CPU name, CPU core number, CPU thread number.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/processor.h>
#include <sys/types.h>
#include "cpuid_info.h"

/* execute CPUID instructure */
extern uint32_t get_cpuid(uint32_t, uint32_t *, uint32_t *, uint32_t *);
/* execute CPUID instructure, eax=4, and ecx=0 */
extern uint32_t get_cpuid4(uint32_t *, uint32_t *, uint32_t *);

/*
 * Get CPU name
 * use CPUID 0x80000002 ~ 0x80000004 to get cpu name
 *
 * cpu_name: to store cpu name
 * size: buf size
 *
 * Return:
 * If successful return 0, else 1
 */
int
get_processor_name(char *cpu_name, int size)
{
	int	ret;
	struct cpuid_data	data1, data2, data3;
	int	i, j;

	if (size < 49) {
		return (1);
	}

	ret = cpuid_info(0x80000002, &data1);

	if (ret) {
		return (1);
	}

	ret = cpuid_info(0x80000003, &data2);

	if (ret) {
		return (1);
	}

	ret = cpuid_info(0x80000004, &data3);

	if (ret) {
		return (1);
	}

	(void) snprintf(cpu_name, size,
	"%.4s%.4s%.4s%.4s%.4s%.4s%.4s%.4s%.4s%.4s%.4s%.4s",
	    (char *)&data1.eax, (char *)&data1.ebx,
	    (char *)&data1.ecx, (char *)&data1.edx,
	    (char *)&data2.eax, (char *)&data2.ebx,
	    (char *)&data2.ecx, (char *)&data2.edx,
	    (char *)&data3.eax, (char *)&data3.ebx,
	    (char *)&data3.ecx, (char *)&data3.edx);

	i = 0;
	j = 0;

	while (isblank(cpu_name[i])) {
		i++;
	}

	while (cpu_name[i] != '\0') {
		cpu_name[j] = cpu_name[i++];

		if (isblank(cpu_name[j])) {
			while (isblank(cpu_name[i])) {
				i++;
			}
		}
		j++;
	}

	cpu_name[j] = '\0';

	return (0);
}
/*
 * Get AMD CPU max core number
 */
uint16_t
get_amd_core_num()
{
	int	ret;
	struct cpuid_data	data;
	uint16_t	num, i;

	ret = cpuid_info(0x80000008, &data);

	/*
	 * If this cpu support CPUID 0x80000008,
	 * if ecx[12 ~ 15]>0, max core number=1 << ecx[12 ~ 15]
	 * if ecx[12 ~ 15]=0, max core number=ecx[0 ~ 7] + 1
	 */
	if (ret == 0) {
		num = (data.ecx >> 12) &0xf;

		if (num == 0) {
			num = (data.ecx & 0xff) + 1;
		} else {
			i = 1;
			num = i << num;
		}

		return (num);
	}

	ret = cpuid_info(0x1, &data);

	/* If this cpu don't support CPUID 1, max core nubmer is 1 */
	if (ret) {
		return (1);
	}

	/* If CPUID 1, edx bit 28 is 0, max core number is 1 */
	if ((data.edx & (1 << 28)) == 0) {
		return (1);
	}

	num = (data.ebx >> 16) & 0xff;

	ret = cpuid_info(0x80000001, &data);

	/* If this cpu don't support CPUID 0x80000001, max core nubmer is 1 */
	if (ret) {
		return (1);
	}

	/*
	 * If CPUID 0x80000001 ecx bit 1 is 1,
	 * max core number=CPUID 1, ebx[15 ~ 22]
	 * else max core number is 1
	 */
	if (data.ecx & 0x2) {
		return (num);
	} else {
		return (1);
	}
}

/*
 * Get Intel CPU core number
 */
uint16_t
get_intel_core_num()
{
	uint32_t	eax, ebx, ecx, edx;
	uint16_t	num;

	/* If this cpu don't support CPUID 4, max core number is 1 */
	eax = get_cpuid(0x0, &ebx, &ecx, &edx);

	if (eax < 0x4) {
		return (1);
	}

	/* max core number= CPUID 4, eax[26 ~ 31] + 1 */
	eax = get_cpuid4(&ebx, &ecx, &edx);
	num = ((eax >> 26) & 0x3f) + 1;

	return (num);
}

/*
 * Get CPU max core number
 */
uint16_t
get_max_core_num()
{
	uint32_t	eax, ebx, ecx, edx;
	char	sig[13];
	uint16_t	num;

	eax = get_cpuid(0, &ebx, &ecx, &edx);

	if (eax < 1) {
		return (0);
	}

	(void) snprintf(sig, 13, "%.4s%.4s%.4s",
	    (char *)&ebx, (char *)&edx, (char *)&ecx);

	if (strncmp(sig, "AuthenticAMD", 12)) {
		/* get intel cpu max core number */
		num = get_intel_core_num();
	} else {
		/* get amd cpu max core number */
		num = get_amd_core_num();
	}

	return (num);
}

/*
 * Get cpu max thread number
 */
uint16_t
get_max_thread_num()
{
	int	ret;
	struct cpuid_data	data;
	uint16_t	num;

	ret = cpuid_info(0x1, &data);

	/* If this cpu don't support CPUID 1, max thread number is 1 */
	if (ret) {
		return (1);
	}

	/* CPUID 1 edx[28] is 0, max thread number is 1 */
	if ((data.edx & (1 << 28)) == 0) {
		return (1);
	}

	/* max thread number=CPUID 1 ebx[16 ~ 19] */
	num = (data.ebx >> 16) & 0xff;

	return (num);
}

/*
 * Get CPU type, intel or amd
 *
 * Return:
 * If fail to identify cpu type, return -1
 */
int
get_cpu_type()
{
	uint32_t	ebx, ecx, edx;
	char	sig[13];

	(void) get_cpuid(0, &ebx, &ecx, &edx);

	(void) snprintf(sig, 13, "%.4s%.4s%.4s",
	    (char *)&ebx, (char *)&edx, (char *)&ecx);

	if (strcmp(sig, "AuthenticAMD") == 0) {
		return (AMD_TYPE);
	} else {
		if (strcmp(sig, "GenuineIntel") == 0) {
			return (INTEL_TYPE);
		}
	}

	return (-1);
}

/*
 * Execute CPUID instructure
 *
 * func: CPUID instructure
 * pdata: to store eax, ebx, ecx, edx register content
 *
 * Return:
 * If successful return 0, else -1
 */
int
cpuid_info(uint32_t func, cpuid_data_t pdata)
{
	uint32_t	ebx, ecx, edx;
	uint32_t	basic_func, ext_func;

	basic_func = get_cpuid(0, &ebx, &ecx, &edx);
	ext_func = get_cpuid(0x80000000, &ebx, &ecx, &edx);

	/* Check if this func support by CPU */
	if (((func <= basic_func)) ||
	    ((func >= 0x80000000) && (func <= ext_func))) {
		pdata->eax = get_cpuid(func, &pdata->ebx,
		    &pdata->ecx, &pdata->edx);
		return (0);
	} else {
		return (-1);
	}
}

/*
 * Insert cpu info into processor_pkg_info structure
 * to calculate actual CPU core number and thread number.
 *
 * cpuid: cpuid number in system
 * apicid: CPU APICID
 * info: cpu info
 */
void
insert_vcpu_info(long cpuid, uint8_t apicid, processor_pkg_info_t info)
{
	int	i, j, off, core_id;

	if (info->num_thread == info->max_thread) {
		return;
	}

	j = info->max_thread / info->max_core;

	off = 0;
	for (i = 1; i < j; i = 1 << off) {
		off++;
	}

	/* from cpu APICID get cpu core id */
	core_id = apicid >> off;

	for (i = 0; i < info->num_thread; i++) {
		if (info->v_cpu[i].core_id == core_id) {
			break;
		}
	}

	if (i == info->num_thread) {
		info->num_core++;
	}

	i = info->num_thread;
	info->v_cpu[i].cpu_id = cpuid;
	info->v_cpu[i].core_id = core_id;
	info->v_cpu[i].thread_id = apicid;

	info->num_thread++;
}

/*
 * Get processor package information
 * include cpu name, cpu type, VM attribute
 */
void
get_pkg_info(processor_pkg_info_t info)
{
	int	ret;
	struct cpuid_data	data;

	ret = get_processor_name(info->name, 52);

	if (ret) {
		(void) sprintf(info->name, "unknown");
	}

	info->pkg_type = get_cpu_type();

	info->bVM = 0;
	switch (info->pkg_type) {
		case INTEL_TYPE:
			ret = cpuid_info(1, &data);

			if (!ret) {
				if (data.ecx & (1 << 5)) {
					info->bVM = 1;
				}
			}
			break;
		case AMD_TYPE:
			ret = cpuid_info(0x80000001, &data);

			if (!ret) {
				if (data.ecx & (1 << 2)) {
					info->bVM = 1;
				}
			}
			break;
		default:
			break;
	}
}

/*
 * Free processor package resource
 */
void
free_cpu_info(processor_pkg_info_t proc_pkg_info)
{
	processor_pkg_info_t	info;

	while (proc_pkg_info) {
		info = proc_pkg_info;
		proc_pkg_info = proc_pkg_info->next;
		if (info->v_cpu) {
			(void) free(info->v_cpu);
		}
		(void) free(info);
	}
}

/*
 * Scan system cpus and get information
 * Bind to each cpu, get cpu information,
 * identify cpu core id, thread id, package id
 * get cpu information
 */
processor_pkg_info_t
scan_cpu_info()
{
	int	ret, err;
	long	i, j, off, nconf;
	uint8_t	apic_id;
	uint16_t	max_core, max_thread;
	uint32_t	pkg_id;
	processor_pkg_info_t	pkg_info;
	processor_pkg_info_t	info;
	processor_pkg_info_t	p;
	processor_info_t	p_info;
	struct cpuid_data	data;

	nconf = sysconf(_SC_NPROCESSORS_CONF);

	if (nconf <= 0) {
		return (NULL);
	}

	pkg_info = NULL;
	err = 0;

	for (i = 0; i < nconf; i++) {
		ret = processor_info(i, &p_info);

		if (ret) {
			continue;
		}

		if ((p_info.pi_state != P_ONLINE) &&
		    (p_info.pi_state != P_NOINTR)) {
			continue;
		}

		if (processor_bind(P_PID, P_MYID, i, NULL) < 0) {
			continue;
		}

		ret = cpuid_info(0x1, &data);

		if (ret) {
			continue;
		}

		apic_id = (data.ebx >> 24) & 0xff;
		max_core = get_max_core_num();
		max_thread = get_max_thread_num();

		off = 0;
		for (j = 1; j < max_thread; j = 1 << off) {
			off++;
		}

		pkg_id = apic_id >> off;

		info = pkg_info;
		while (info) {
			if (info->pkg_id == pkg_id) {
				break;
			} else {
				info = info->next;
			}
		}

		if (info == NULL) {
			info = (processor_pkg_info_t)
			    malloc(sizeof (struct processor_pkg_info));

			if (info == NULL) {
				err = 1;
				break;
			}

			info->v_cpu = (virtual_cpu_info_t)
			    malloc(sizeof (struct virtual_cpu_info) *
			    max_thread);

			if (info->v_cpu == NULL) {
				err = 1;
				break;
			}

			info->clock = p_info.pi_clock;
			info->feature = data.eax;
			info->pkg_id = pkg_id;
			info->max_core = max_core;
			info->max_thread = max_thread;
			info->num_core = 0;
			info->num_thread = 0;
			info->next = NULL;
			get_pkg_info(info);

			if (pkg_info) {
				p = pkg_info;
				while (p->next) {
					p = p->next;
				}
				p->next = info;
			} else {
				pkg_info = info;
			}
		}

		insert_vcpu_info(i, apic_id, info);
	}

	if (err) {
		free_cpu_info(pkg_info);
		pkg_info = NULL;
	}

	return (pkg_info);
}

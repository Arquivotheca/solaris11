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

#include <mdb/mdb_modapi.h>
#include <mdb/mdb_ks.h>

#include <ctype.h>
#include <sys/types.h>
#include <sys/numaio.h>
#include <sys/numaio_priv.h>


/* arguments passed to numaio_group dee-command */
#define	NUMAIO_NONE		0x00
#define	NUMAIO_OBJECTS		0x01
#define	NUMAIO_CPUS		0x02

#define	MAKE_PRINT_STR(object, cpuid_arr, cpuid_str) {			\
	if (object.afo_type == NUMAIO_OBJ_DEVINFO ||			\
	    object.afo_type == NUMAIO_OBJ_PROCINFO) {			\
		cpuid_str = "NA";					\
	} else if ((object.afo_type == NUMAIO_OBJ_KTHREAD ||		\
	    object.afo_type == NUMAIO_OBJ_DDI_INTR) &&			\
	    object.afo_cpuid < 0) {					\
		cpuid_str = "-1";					\
	} else {							\
		cpuid_arr[3] = '\0';					\
		cpuid_str = lltostr(object.afo_cpuid, &cpuid_arr[3]);	\
		if (object.afo_flags & NUMAIO_AFO_DEDICATED_CPU)	\
			strcat(cpuid_str, "(DED)");			\
	}								\
}

#define	list_d2l(a, obj) ((list_node_t *)(((char *)obj) + (a)->list_offset))
#define	list_object(a, node) ((void *)(((char *)node) - (a)->list_offset))

static char *
lltostr(longlong_t value, char *ptr)
{
	longlong_t t;

	do {
		*--ptr = (char)('0' + value - 10 * (t = value / 10));
	} while ((value = t) != 0);

	return (ptr);
}

static void *
head_object(list_node_t *list_head, list_t *list)
{
	if (list->list_head.list_next == list_head)
		return (NULL);

	return (list_object(list, list->list_head.list_next));
}

static void *
next_object(list_t *list, list_node_t *list_head, void *object)
{
	list_node_t *node;

	/*LINTED E_BAD_PTR_CAST_ALIGN*/
	node = list_d2l(list, object);

	if (node->list_next != list_head)
		return (list_object(list, node->list_next));

	return (NULL);
}

void
numaio_group_help(void)
{
	mdb_printf("Print numaio_group information for a given numaio_group "
	    "pointer.\n\n"
	    "If address of a \"numaio_group_t\" structure is not provided, then"
	    "\nprint information about all numaio_groups in the"
	    " \"numaio_grp_cache\".\n\n"
	    "The \"object-type\" in -o option can take either of \"thread\","
	    " \"intr\",\n \"proc\", \"devinfo\", \"all\" or a combination of "
	    "the first four.\n\n"
	    "Options:\n"
	    "	-c:\tprint CPUs in the constraint for each group\n"
	    "	-o:\tprints only specified objects, can be combined with\n"
	    "\t\tmultiple objects like \"::numaio_group -o proc,thread\".\n"
	    "\t\tAll objects can be printed by specifying \"::numaio_group "
	    "-o all\"");
}

int
numaio_group(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint_t			args = NUMAIO_NONE;
	numaio_group_t		grp;
	const char *optO = NULL;
	char *thread, *proc, *intr, *devinfo, *all;

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("genunix`numaio_grp_cache",
		    "genunix`numaio_group", argc, argv) == -1) {
			mdb_warn("FAILED to walk group cache");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	if (mdb_getopts(argc, argv,
	    'o', MDB_OPT_STR, &optO,
	    'c', MDB_OPT_SETBITS, NUMAIO_CPUS, &args) != argc) {
		return (DCMD_USAGE);
	}

	/*
	 * Testing for object type whether it is a thread or an interrupt
	 * or something else is pretty loose here.
	 */
	if (optO != NULL) {
		thread = strstr(optO, "thread");
		proc = strstr(optO, "proc");
		intr = strstr(optO, "intr");
		devinfo = strstr(optO, "devinfo");
		all = strstr(optO, "all");
		if (thread == NULL && proc == NULL && intr == NULL &&
		    devinfo == NULL && all == NULL) {
			return (DCMD_USAGE);
		}
		args = NUMAIO_OBJECTS;
	}

	if (mdb_vread(&grp, sizeof (grp), addr) == -1) {
		mdb_warn("failed to read struct numaio_group_s at %p",
		    addr);
		return (DCMD_ERR);
	}

	switch (args) {
	case NUMAIO_NONE: {
		numaio_constraint_t constraint;
		int i;
		boolean_t first_time = B_TRUE;

		if (DCMD_HDRSPEC(flags)) {
			mdb_printf("%<u>%?s %-30s %-20s%</u>\n",
			    "ADDR", "GROUP_NAME", "CONSTRAINT");
		}

		mdb_printf("%?p %-20s  ", addr, grp.afg_name);

		if (grp.afg_constraint == NULL)
			break;

		if (mdb_vread(&constraint, sizeof (constraint),
		    (uintptr_t)grp.afg_constraint) == -1) {
			mdb_warn("failed to read numaio_constraint_t at %p",
			    (uintptr_t)grp.afg_constraint);
			return (DCMD_ERR);
		}

		for (i = 0; i < NLGRPS_MAX; i++) {
			if (constraint.afc_lgrp[i].afl_ncpus == 0)
				continue;
			if (first_time) {
				mdb_printf("lgrp : %d", i);
				first_time = B_FALSE;
			} else {
				mdb_printf(", %d", i);
			}
		}
		break;
	}

	case NUMAIO_CPUS: {
		numaio_constraint_t constraint;
		numaio_cpuid_t cpu_list[256];
		int cpu_cnt, i, j, n;
		uintptr_t cpu_list_addr;
		boolean_t first_time = B_TRUE;

		if (DCMD_HDRSPEC(flags)) {
			mdb_printf("%<u>%?s %-16s %-30s%</u>\n",
			    "ADDR", "CONSTRAINT", "EFFECTIVE CPUS");
		}

		mdb_printf("%?p ", addr);

		if (grp.afg_constraint == NULL)
			break;

		if (mdb_vread(&constraint, sizeof (constraint),
		    (uintptr_t)grp.afg_constraint) == -1) {
			mdb_warn("failed to read numaio_constraint_t at %p",
			    (uintptr_t)grp.afg_constraint);
			return (DCMD_ERR);
		}

		for (i = 0; i < NLGRPS_MAX; i++) {
			if (constraint.afc_lgrp[i].afl_ncpus == 0)
				continue;

			if (first_time) {
				mdb_printf("lgrp : %-3d", i);
				first_time = B_FALSE;
			} else {
				mdb_printf("%?s lgrp : %-3d", "", i);
			}

			cpu_cnt = constraint.afc_lgrp[i].afl_ncpus;
			cpu_list_addr =
			    (uintptr_t)constraint.afc_lgrp[i].afl_cpus;

			if (mdb_vread(&cpu_list, sizeof (numaio_cpuid_t) *
			    cpu_cnt, cpu_list_addr) == -1) {
				mdb_warn("failed to read  at %p",
				    grp.afg_constraint);
				return (DCMD_ERR);
			}

			mdb_printf("%6s ", "");
			for (j = 0, n = 0; j < cpu_cnt; j++) {
				if (cpu_list[j].nc_flags & AFC_CPU_NOT_AVAIL)
					continue;

				mdb_printf("%-3d ", cpu_list[j].nc_cpuid);
				n++;
				if (n%8 == 0 && j != (cpu_cnt -1))
					mdb_printf("\n%?s %16s ", "", "");
			}

			mdb_printf("\n");
		}
		break;
	}

	case NUMAIO_OBJECTS: {
		numaio_object_t *obj_addr;
		numaio_object_t object;
		numaio_group_t *grp_addr = (numaio_group_t *)addr;
		char *type = NULL;
		boolean_t first_time = B_TRUE;
		char cpuid_arr[12], *cpuid_str;

		if (DCMD_HDRSPEC(flags)) {
			mdb_printf("%<u>%?s %?s %-4s %-7s %-20s %</u>\n",
			    "GROUP ADDR", "OBJECT ADDR", "TYPE", "CPU",
			    "OBJECT NAME");
		}

		obj_addr = head_object(&grp_addr->afg_objects.list_head,
		    &grp.afg_objects);

		while (obj_addr != NULL) {
			if (mdb_vread(&object, sizeof (object),
			    (uintptr_t)obj_addr) == -1) {
				mdb_warn("failed to read numaio_object_t at %p",
				    addr);
				return (DCMD_ERR);
			}
			switch (object.afo_type) {
			case NUMAIO_OBJ_KTHREAD:
				if (thread != NULL || all != NULL)
					type = "KTHR";
				MAKE_PRINT_STR(object, cpuid_arr, cpuid_str);
				break;
			case NUMAIO_OBJ_DDI_INTR:
				if (intr != NULL || all != NULL)
					type = "INTR";
				MAKE_PRINT_STR(object, cpuid_arr, cpuid_str);
				break;
			case NUMAIO_OBJ_DEVINFO:
				if (devinfo != NULL || all != NULL)
					type = "DEVI";
				MAKE_PRINT_STR(object, cpuid_arr, cpuid_str);
				break;
			case NUMAIO_OBJ_PROCINFO:
				if (proc != NULL || all != NULL)
					type = "PROC";
				MAKE_PRINT_STR(object, cpuid_arr, cpuid_str);
				break;
			default:
				break;
			}
			if (type != NULL) {
				if (first_time) {
					mdb_printf("%?p ", grp_addr);
					first_time = B_FALSE;
				} else {
					mdb_printf("%?s ", "");
				}
				mdb_printf("%?p %-4s %-8s "
				    "%-20s\n", obj_addr, type,
				    cpuid_str, object.afo_name);
			}
			type = NULL;
			obj_addr = next_object(&grp.afg_objects,
			    &grp_addr->afg_objects.list_head, &object);
		}
		break;
	}
	default:
		return (DCMD_USAGE);
	}
	return (DCMD_OK);
}

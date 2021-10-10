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
 * Get SPARC system hardware information from picl
 */

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <picl.h>
#include <sys/processor.h>
#include <kstat.h>
#include "libddudev.h"
#include "dmi_info.h"

static const char *system_prop[] = {
	"PlatformGroup",
	"banner-name",
	"model",
	NULL
};

static const char *system_info[] = {
	"PlatformGroup",
	"Product",
	"Model",
	NULL
};

static const char *prom_prop[] = {
	"model",
	"version",
	NULL
};

static const char *prom_info[] = {
	"Model",
	"Version",
	NULL
};

static const char *processor_socket_prop[] = {
	"name",
	"ProcessorType",
	"FPUType",
	"sparc-version",
	"clock-frequency",
	"State",
	"portid",
	"cpuid",
	"ecache-size",
	"icache-size",
	"dcache-size",
	NULL
};

static const char *processor_socket_info[] = {
	"Processor Name",
	"Processor Type",
	"FPU Type",
	"Sparc Version",
	"Clock Frequency",
	"State",
	"PortID",
	"CPUID",
	"ecache-size",
	"icache-size",
	"dcache-size",
	NULL
};

static const char *mem_prop[] = {
	"ID",
	"Size",
	NULL
};

static const char *mem_info[] = {
	"Memory Device Locator",
	"Memory Size",
	NULL
};

static int	cpu_num;

/* dmi_info usage */
void
usage()
{
	FPRINTF(stderr, "Usage: dmi_info [ABMmPSVv]\n");
	FPRINTF(stderr, "       -A: print all information\n");
	FPRINTF(stderr, "       -B: print BIOS information \n");
	FPRINTF(stderr, "       -M: print Motherboard information\n");
	FPRINTF(stderr, "       -m: print memory subsystem information\n");
	FPRINTF(stderr, "       -P: print Processor information\n");
	FPRINTF(stderr, "       -p: print Processor number and core number\n");
	FPRINTF(stderr, "       -V: print SMBIOS version\n");
	FPRINTF(stderr, "       -v: print with verbose mode\n");
	exit(1);
}

/*
 * Print property information
 *
 * proph: property handle
 * pinfo: to store property value
 * granu: output granularity
 */
void
prt_prop_val(picl_prophdl_t proph, picl_propinfo_t *pinfo, int granu)
{
	int	ret;
	void 	*data;
	char 	*str;

	if (!(pinfo->accessmode & PICL_READ)) {
		PRINTF("N/A");
		return;
	}

	if (pinfo->size == 0) {
		return;
	}

	data = malloc(pinfo->size);

	if (data == NULL) {
		perror("can not allocate memory space");
		exit(1);
	}

	ret = picl_get_propval(proph, data, pinfo->size);

	if (ret) {
		perror("fail to get property value\n");
		PRINTF("Error");
		(void) free(data);
		return;
	}

	switch (pinfo->type) {
		case PICL_PTYPE_CHARSTRING:
			PRINTF("%s", (char *)data);
			break;

		case PICL_PTYPE_INT:
			switch (pinfo->size) {
				case sizeof (char):
					PRINTF("%d",
					    *(char *)data / granu);
					break;
				case sizeof (short):
					PRINTF("%hd",
					    *(short *)data / granu);
					break;
				case sizeof (int):
					PRINTF("%d",
					    *(int *)data / granu);
					break;
				case sizeof (long long):
					PRINTF("%lld",
					    *(long long *)data / granu);
					break;
				default:
					PRINTF("Invalid int size");
			}
			break;

		case PICL_PTYPE_UNSIGNED_INT:
			switch (pinfo->size) {
				case sizeof (unsigned char):
					PRINTF("%x",
					    *(unsigned char *)data / granu);
					break;
				case sizeof (unsigned short):
					PRINTF("%hx",
					    *(unsigned short *)data / granu);
					break;
				case sizeof (unsigned int):
					PRINTF("%u",
					    *(unsigned int *)data / granu);
					break;
				case sizeof (unsigned long long):
					PRINTF("%llu",
					    *(unsigned long long *)data /
					    granu);
					break;
				default:
					PRINTF("Invalid uint size");
			}
			break;

		case PICL_PTYPE_FLOAT:
			switch (pinfo->size) {
				case sizeof (float):
					PRINTF("%f", *(float *)data);
					break;
				case sizeof (double):
					PRINTF("%f", *(double *)data);
					break;
				default:
					PRINTF("Invalid float size");
			}
			break;

		case PICL_PTYPE_TIMESTAMP:
			str = ctime((time_t *)data);

			if (str) {
				str[strlen(str) - 1] = '\0';
				PRINTF("%s", str);
			}
			break;

		case PICL_PTYPE_TABLE:
			PRINTF("TBL");
			break;
		case PICL_PTYPE_REFERENCE:
			PRINTF("REF");
			break;
		case PICL_PTYPE_BYTEARRAY:
			PRINTF("BIN");
			break;
		default:
			PRINTF("Unknown type");
	}

	(void) free(data);
}

/*
 * Print picl node properties
 * get picl node properties and print properties value
 *
 * nodeh: picl node handle
 */
void
prt_node_props(picl_nodehdl_t nodeh)
{
	int	ret;
	picl_prophdl_t proph;
	picl_propinfo_t pinfo;

	ret = picl_get_first_prop(nodeh, &proph);

	while (!ret) {
		ret = picl_get_propinfo(proph, &pinfo);

		if (!ret) {
			if (ret != 0) {
				PRINTF("\t%s = ", pinfo.name);
				prt_prop_val(proph, &pinfo, 1);
				PRINTF("\n");
			}
		}

		ret = picl_get_next_prop(proph, &proph);
	}
}

/*
 * Get property information by name
 *
 * nodeh: node handle
 * pname: property name
 * pinfo: store property information
 *
 * Return:
 * If sucessful return buffer which store property value, else return NULL
 */
void *
get_prop_info(picl_nodehdl_t nodeh, const char *pname,
picl_propinfo_t *pinfo)
{
	void 		*data;
	picl_prophdl_t	proph;

	if (picl_get_propinfo_by_name(nodeh, pname, pinfo, &proph) != 0) {
		/* can not get property info */
		FPRINTF(stderr, "can not get property \"%s\"info\n", pname);
		return (NULL);
	}

	if (!(pinfo->accessmode & PICL_READ)) {
		/* can not access property info */
		FPRINTF(stderr, "can not access property \"%s\"info\n", pname);
		return (NULL);
	}

	if (pinfo->size == 0) {
		/* the info is empty */
		FPRINTF(stderr, "the property \"%s\"info is empty\n", pname);
		return (NULL);
	}

	data = malloc(pinfo->size);

	if (data == NULL) {
		FPRINTF(stderr, "fail to allocate memory space to store "
		    "property \"%s\" value\n", pname);
		return (NULL);
	}

	if (picl_get_propval(proph, data, pinfo->size) != 0) {
		FPRINTF(stderr, "fail to get property \"%s\" value\n", pname);
		(void) free(data);
		return (NULL);
	}

	return (data);
}

/*
 * Print bios information
 *
 * nodeh: picl openprom node handle
 * verbose: if set, print node all properties
 *
 * output (printed):
 * Vendor:
 * Model:
 * Version:
 * Release Date:
 * BIOS Revision:
 * Firmware Revision:
 */
void
print_bios_info(picl_nodehdl_t  nodeh, int verbose)
{
	int		ret, i;
	picl_prophdl_t	proph;
	picl_propinfo_t	pinfo;

	if (verbose) {
		prt_node_props(nodeh);
		return;
	}

	PRINTF(" Vendor:\n");
	i = 1;

	while (prom_prop[i]) {
		if (prom_info[i]) {
			PRINTF(" %s:", prom_info[i]);
		} else {
			PRINTF(" %s:", prom_prop[i]);
		}

		ret = picl_get_propinfo_by_name(nodeh, prom_prop[i],
		    &pinfo, &proph);

		if (ret == 0) {
			prt_prop_val(proph, &pinfo, 1);
		}
		PRINTF("\n");

		i++;
	}
	PRINTF(" Release Date:\n");
	PRINTF(" BIOS Revision:\n");
	PRINTF(" Firmware Revision:\n");
}

/*
 * Print system information
 *
 * nodeh: picl platform node handle
 * verbose: if set, print node all properties
 *
 * output (printed):
 * Manufacturer:
 * PlatformGroup:
 * Product:
 * Model:
 */
void
print_system_info(picl_nodehdl_t  nodeh, int verbose)
{
	int		ret, i;
	picl_prophdl_t	proph;
	picl_propinfo_t	pinfo;

	if (verbose) {
		prt_node_props(nodeh);
		return;
	}

	PRINTF(" Manufacturer:Sun/Oracle\n");
	i = 1;

	while (system_prop[i]) {
		if (system_info[i]) {
			PRINTF(" %s:", system_info[i]);
		} else {
			PRINTF(" %s:", system_prop[i]);
		}

		ret = picl_get_propinfo_by_name(nodeh, system_prop[i],
		    &pinfo, &proph);

		if (ret == 0) {
			prt_prop_val(proph, &pinfo, 1);
		}
		PRINTF("\n");

		i++;
	}
}

/*
 * Print processor information
 * This is called indirectly via the picl_walk_tree_by_class
 *
 * nodeh: picl cpu node handle
 * *args: if set, print node all properties
 *
 * output (printed):
 * Processor Name:
 * Processor Type:
 * FPU Type:
 * Sparc Version:
 * Clock Frequency:
 * State:
 * PortID:
 * CPUID:
 * ecache-size:
 * icache-size:
 * dcache-siz:
 */
int
print_processor_info(picl_nodehdl_t nodeh, void *args)
{
	int		ret, i, granu = 1;
	picl_prophdl_t	proph;
	picl_propinfo_t	pinfo;

	PRINTF(" Processor %d:\n", cpu_num);
	cpu_num++;

	i = *(int *)args;

	if (i != 0) {
		prt_node_props(nodeh);
		return (PICL_WALK_CONTINUE);
	}

	while (processor_socket_prop[i]) {
		if (processor_socket_info[i]) {
			PRINTF("  %s: ", processor_socket_info[i]);
		} else {
			PRINTF("  %s: ", processor_socket_prop[i]);
		}

		ret = picl_get_propinfo_by_name(nodeh,
		    processor_socket_prop[i], &pinfo, &proph);

		if (ret == 0) {
			if (i == 4) {
				granu = 1000000;
			}

			prt_prop_val(proph, &pinfo, granu);

			if (i == 4) {
				PRINTF("MHZ");
			}
		}
		PRINTF("\n");

		i++;
	}
	PRINTF("\n");

	return (PICL_WALK_CONTINUE);
}

/*
 * Scan cpus and get cpu package number, core number and thread number
 */
void
scan_cpu_info()
{
	int			i, j, ret;
	int			npkg = 0;
	long			nconf, nonline;
	processor_pkg_info_t	pkg_info = NULL;
	processor_pkg_info_t	info;
	processor_pkg_info_t	p;
	kstat_ctl_t		*kc;
	kstat_t			*ksp;
	kstat_named_t		*k;
	virtual_cpu_info_t	v_info;
	processor_info_t	p_info;

	/* get number of cpu configured */
	nconf = sysconf(_SC_NPROCESSORS_CONF);

	if (nconf <= 0) {
		nconf = 0;
	}

	/* get number of cpu online */
	nonline = sysconf(_SC_NPROCESSORS_ONLN);

	if (nonline <= 0) {
		nonline = 0;
	}

	/* open kstat to check cpu information */
	v_info = NULL;
	kc = kstat_open();

	if (kc) {
		v_info = (virtual_cpu_info_t)
		    malloc(sizeof (struct virtual_cpu_info) * nconf);

		if (v_info == NULL) {
			perror("can not allocate space for cpu information");
			(void) kstat_close(kc);
			kc = NULL;
		}
	}

	if (kc) {
		for (i = 0; i < nconf; i++) {
			/*
			 * lookup each cpu instance in kstat
			 * by keyword "cpu_info"
			 */
			ksp = kstat_lookup(kc, "cpu_info", i, NULL);

			if (ksp == NULL) {
				break;
			}

			/* read each cpu information from kstat */
			ret = kstat_read(kc, ksp, NULL);

			if (ret == -1) {
				break;
			}

			/* get cpu "chip_id" information */
			k = (kstat_named_t *)
			    kstat_data_lookup(ksp, "chip_id");

			if (k == NULL) {
				break;
			}

			v_info[i].chip_id = k->value.i32;

			/* get cpu "core_id" information */
			k = (kstat_named_t *)
			    kstat_data_lookup(ksp, "core_id");

			if (k == NULL) {
				break;
			}

			v_info[i].core_id = k->value.i32;

			info = pkg_info;

			/*
			 * according "chip_id",
			 * insert cpu info into cpu info list
			 */
			while (info) {
				if (info->pkg_id == v_info[i].chip_id) {
					break;
				}
				info = info->next;
			}

			/*
			 * If in cpu info list, can not find target "chip_id"
			 * it is a new cpu chip, create new cpu info and insert
			 * it in cpu info list
			 */
			if (info == NULL) {
				info = (processor_pkg_info_t)
				    malloc(sizeof (struct processor_pkg_info));

				if (info == NULL) {
					break;
				}

				info->pkg_id = v_info[i].chip_id;
				info->cpu_id = i;
				info->num_core = 0;
				info->num_thread = 0;
				info->brand = NULL;
				info->next = NULL;

				k = (kstat_named_t *)
				    kstat_data_lookup(ksp, "brand");

				if (k) {
					info->brand = k->value.str.addr.ptr;
				}

				if (pkg_info) {
					p = pkg_info;
					while (p->next) {
						p = p->next;
					}
					p->next = info;
				} else {
					pkg_info = info;
				}

				/* new cpu package */
				npkg++;
			}

			info->num_thread++;

			/*
			 * Check cpu core id information
			 * In cpu info list, if cannot find same
			 * chip id and core id, it is new cpu core
			 */
			for (j = 0; j < i; j++) {
				if ((v_info[j].chip_id == v_info[i].chip_id) &&
				    (v_info[j].core_id == v_info[i].core_id))
				break;
			}

			if (j >= i) {
				info->num_core++;
			}
		}
		(void) free(v_info);
	} else {
		npkg = nconf;
	}

	if (pkg_info && pkg_info->brand) {
		PRINTF("CPU Type:%s", pkg_info->brand);
	} else {
		PRINTF("CPU Type:cpu");
	}

	ret = processor_info(info->cpu_id, &p_info);

	if (ret) {
		PRINTF("\n");
	} else {
		PRINTF(",%s\n", p_info.pi_processor_type);
	}

	/*
	 * print number of cpu, number of core number per processor,
	 * number of thread number per processor
	 */
	if (pkg_info) {
		PRINTF("CPU Number:%d\n", npkg);
		PRINTF("Number of cores per processor:%d\n",
		    pkg_info->num_core);
		PRINTF("Number of threads per processor:%d\n",
		    pkg_info->num_thread);
	} else {
		PRINTF("CPU Number:%ld\n", nconf);
		PRINTF("Number of cores per processor:1\n");
		PRINTF("Number of threads per processor:1\n");
	}


	while (pkg_info) {
		info = pkg_info;
		pkg_info = pkg_info->next;
		(void) free(info);
	}

	if (kc) {
		(void) kstat_close(kc);
	}
}

/*
 * Print memory information
 * This is called indirectly via the picl_walk_tree_by_class
 *
 * nodeh: picl memory-bank node handle
 * *args: if set, print node all properties
 *
 * output (printed):
 * Memory Device Locator:
 * Memory Size:
 */
int
print_memory_info(picl_nodehdl_t nodeh, void *args)
{
	int		ret, i, granu = 1;
	picl_prophdl_t	proph;
	picl_propinfo_t	pinfo;

	i = *(int *)args;

	if (i) {
		prt_node_props(nodeh);
		return (PICL_WALK_CONTINUE);
	}

	while (mem_prop[i]) {
		if (mem_info[i]) {
			PRINTF(" %s: ", mem_info[i]);
		} else {
			PRINTF(" %s: ", mem_prop[i]);
		}

		ret = picl_get_propinfo_by_name(nodeh, mem_prop[i],
		    &pinfo, &proph);

		if (ret == 0) {
			/* if print memory size, set granu with Mbyte */
			if (i == 1) {
				granu = 1024 * 1024;
			}
			prt_prop_val(proph, &pinfo, granu);

			/* after printed memory size, restore granu to 1 */
			if (i == 1) {
				PRINTF("M");
			}
		}
		PRINTF("\n");

		i++;
	}
	PRINTF("\n");

	return (PICL_WALK_CONTINUE);
}

/*
 * print memory bank size
 * This is called indirectly via the picl_walk_tree_by_class
 *
 * nodeh: memory bank node handle
 * *args: memory bank index
 */
int
prt_mem_bank_size(picl_nodehdl_t nodeh, void *args)
{
	void 		*data;
	u_longlong_t	value;
	unsigned long	size;
	picl_propinfo_t	pinfo;
	char		c;

	/* get memory bank "Size" property */
	data = get_prop_info(nodeh, mem_prop[1], &pinfo);
	if (data == NULL) {
		return (PICL_WALK_CONTINUE);
	}

	if ((pinfo.type == PICL_PTYPE_INT) ||
	    (pinfo.type == PICL_PTYPE_UNSIGNED_INT)) {
		switch (pinfo.size) {
			case sizeof (char):
				value = *(unsigned char *)data;
				break;
			case sizeof (short):
				value = *(unsigned short *)data;
				break;
			case sizeof (int):
				value =	*(unsigned int *)data;
				break;
			case sizeof (long long):
				value =	*(unsigned long long *)data;
				break;
			default:
				break;
		}
	}

	if (value == 0) {
		(void) free(data);
		return (PICL_WALK_CONTINUE);
	}

	if (*(int *)args == 0) {
		PRINTF("(");
	} else {
		PRINTF(" + ");
	}

	value = value >> 10;

	if (value >= 1024) {
		value = value >> 10;

		if (value >= 1024) {
			size = (unsigned long) (value & 0x3ff);
			value = value >> 10;
			c = 'G';
		} else {
			size = 0;
			c = 'M';
		}
	} else {
		size = 0;
		c = 'K';
	}

	if (size) {
		PRINTF("%llu.%lu%c", value, (size * 10) / 1024, c);
	} else {
		PRINTF("%llu%c", value, c);
	}

	*(int *)args = *(int *)args + 1;

	(void) free(data);
	return (PICL_WALK_CONTINUE);
}

/*
 * Caculate memory bank size
 * for each memory bank, add memory size
 * This is called indirectly via the picl_walk_tree_by_class
 *
 * nodeh: memory bank node handle
 * *args: memory bank size
 */
int
get_memory_bank_size(picl_nodehdl_t nodeh, void *args)
{
	void 		*data;
	picl_propinfo_t	pinfo;

	data = get_prop_info(nodeh, mem_prop[1], &pinfo);
	if (data == NULL) {
		return (PICL_WALK_CONTINUE);
	}

	if ((pinfo.type == PICL_PTYPE_INT) ||
	    (pinfo.type == PICL_PTYPE_UNSIGNED_INT)) {
		switch (pinfo.size) {
			case sizeof (char):
				*(u_longlong_t *)args =
				    *(u_longlong_t *)args +
				    *(unsigned char *)data;
				break;
			case sizeof (short):
				*(u_longlong_t *)args =
				    *(u_longlong_t *)args +
				    *(unsigned short *)data;
				break;
			case sizeof (int):
				*(u_longlong_t *)args =
				    *(u_longlong_t *)args +
				    *(unsigned int *)data;
				break;
			case sizeof (long long):
				*(u_longlong_t *)args =
				    *(u_longlong_t *)args +
				    *(unsigned long long *)data;
				break;
			default:
				break;
		}
	}

	(void) free(data);
	return (PICL_WALK_CONTINUE);
}

/*
 * Get system physical memory size
 * Walk picl tree, found each memory bank node, and caculate memory size
 *
 * nodeh: picl node handle
 *
 * Return:
 * Return memory size
 */
u_longlong_t
get_phy_mem_size(picl_nodehdl_t nodeh)
{
	u_longlong_t	size;

	size = 0;
	(void) picl_walk_tree_by_class(nodeh, CLASS_MEMBANK, &size,
	    get_memory_bank_size);

	return (size);
}

int
main(int argc, char **argv)
{
	int		c;
	int		verbose = 0;
	int		opt_bios = 0;
	int		opt_sys = 0;
	int		opt_mb = 0;
	int		opt_cpu = 0;
	int		opt_pro = 0;
	int		opt_mem = 0;
	int		operate = 0;
	int		ret;
	picl_nodehdl_t  rooth;
	picl_nodehdl_t  nodeh;

	while ((c = getopt(argc, argv, "ABMmPCSv")) != EOF) {
		switch (c) {
		case 'A':
			opt_bios = 1;
			opt_sys = 1;
			opt_mb = 1;
			opt_cpu = 1;
			opt_mem = 1;
			operate++;
			break;
		case 'B':
			opt_bios = 1;
			operate++;
			break;
		case 'M':
			opt_mb = 1;
			operate++;
			break;
		case 'm':
			opt_mem = 1;
			operate++;
			break;
		case 'P':
			opt_cpu = 1;
			operate++;
			break;
		case 'C':
			opt_pro = 1;
			operate++;
			break;
		case 'S':
			opt_sys = 1;
			operate++;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}

	if (operate == 0) {
		opt_bios = 1;
		opt_sys = 1;
		opt_mb = 1;
		opt_cpu = 1;
		opt_mem = 1;
	}

	ret = picl_initialize();

	if (ret) {
		perror("error open PICL");
		return (1);
	}

	ret = picl_get_root(&rooth);

	if (ret) {
		perror("error get PICL root");
		(void) picl_shutdown();

		return (1);
	}

	if (opt_bios) {
		ret = picl_find_node(rooth, PICL_PROP_CLASSNAME,
		    PICL_PTYPE_CHARSTRING, CLASS_PROM,
		    sizeof (CLASS_PROM), &nodeh);

		if (ret == 0) {
			PRINTF("BIOS Information:\n");
			print_bios_info(nodeh, verbose);
			PRINTF("\n");
		}
	}

	if (opt_sys) {
		ret = picl_find_node(rooth, PICL_PROP_NAME,
		    PICL_PTYPE_CHARSTRING, NODE_PLATFORM,
		    sizeof (NODE_PLATFORM), &nodeh);

		if (ret == 0) {
			PRINTF("System Information:\n");
			print_system_info(nodeh, verbose);
			PRINTF("\n");
		}
	}

	/*
	 * For sync x86 dmi_info motherboard output,
	 * just list motherboard title
	 */
	if (opt_mb) {
		PRINTF("MotherBoard Information:\n");
		PRINTF(" Product:\n");
		PRINTF(" Manufacturer:\n");
		PRINTF(" Version:\n");
		PRINTF(" Onboard Devices:\n");
		PRINTF("\n");
	}

	if (opt_cpu) {
		PRINTF("CPU Information:\n");
		cpu_num = 0;
		(void) picl_walk_tree_by_class(rooth, CLASS_CPU, &verbose,
		    print_processor_info);
	}

	if (opt_mem) {
		u_longlong_t	mem_size;
		int		item;

		PRINTF("Memory Information:\n");
		mem_size = get_phy_mem_size(rooth);

		/*
		 * prefer get actual installed memory banks size,
		 * memory size get from picl "memory-bank" szie.
		 * if fail to get physical memory banks size(mem_size=0),
		 * get memory size from syscall sysconf, it is not memory
		 * physical size, it is system can used memory size.
		 */
		if (mem_size) {
			mem_size = mem_size >> 10;

			if (mem_size > 1024) {
				mem_size = mem_size >> 10;

				if (mem_size > 1024) {
					PRINTF("Physical Memory: %lluG ",
					    mem_size >> 10);
				} else {
					PRINTF("Physical Memory: %lluM ",
					    mem_size);
				}
			} else {
				PRINTF("Physical Memory: %lluK",
				    mem_size);
			}

			item = 0;
			(void) picl_walk_tree_by_class(rooth, CLASS_MEMBANK,
			    &item, prt_mem_bank_size);

			if (item > 0) {
				PRINTF(")");
			}
			PRINTF("\n");
		} else {
			u_longlong_t		pages;
			u_longlong_t		pagesize;

			pagesize = sysconf(_SC_PAGESIZE);
			pages = sysconf(_SC_PHYS_PAGES);

			PRINTF("Physical Memory: %lluM\n",
			    pages*pagesize >> 20);
		}

		PRINTF("\n");

		(void) picl_walk_tree_by_class(rooth, CLASS_MEMBANK, &verbose,
		    print_memory_info);
	}

	if (opt_pro) {
		scan_cpu_info();
	}

	(void) picl_shutdown();
	return (0);
}

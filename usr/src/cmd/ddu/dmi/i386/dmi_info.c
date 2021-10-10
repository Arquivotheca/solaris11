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
 * Get system hardware information from SMBIOS
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "libddudev.h"
#include "dmi.h"
#include "dmi_info.h"
#include "processor_info.h"
#include "cpuid_info.h"
#include "bios_info.h"
#include "mb_info.h"
#include "mem_info.h"
#include "sys_info.h"

/*
 * Print SMBIOS BIOS, Motherboard, System,
 * Processor, Memory information.
 * Print system controller devices information
 */
void
usage()
{
	FPRINTF(stderr, "Usage: dmi_info [BCDMmPS]\n");
	FPRINTF(stderr, "       -B: print BIOS information \n");
	FPRINTF(stderr, "       -C: print Processor number and core number\n");
	FPRINTF(stderr, "       -M: print Motherboard information\n");
	FPRINTF(stderr, "       -m: print memory subsystem information\n");
	FPRINTF(stderr, "       -P: print Processor information\n");
	FPRINTF(stderr, "       -S: print System information\n");
	FPRINTF(stderr, "       -h: for help\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	smbios_hdl_t	hdl = NULL;
	int		c;
	uint16_t	version;
	uint32_t	operate = 0;

	while ((c = getopt(argc, argv, "BCDMmPS")) != EOF) {
		switch (c) {
			case 'B':
				operate = operate | OPT_BIOS;
				break;
			case 'C':
				operate = operate | OPT_CPU;
				break;
			case 'S':
				operate = operate | OPT_SYS;
				break;
			case 'M':
				operate = operate | OPT_MB;
				break;
			case 'P':
				operate = operate | OPT_PRO;
				break;
			case 'm':
				operate = operate | OPT_MEM;
				break;
			default:
				usage();
				break;
		}
	}

	if (!operate) {
		operate = OPT_SYS | OPT_BIOS | OPT_MB | OPT_PRO | OPT_MEM;
	}

	if ((operate & OPT_DMI)) {
		hdl = smbios_open();

		if (hdl == NULL) {
			FPRINTF(stderr, "error open smbios\n");
			return (1);
		}

		version = smbios_version(hdl);
		if (version < 0x210) {
			FPRINTF(stderr, "error smbios version\n");
			smbios_close(hdl);
			return (1);
		}
	}

	if ((operate & OPT_CPU)) {
		processor_pkg_info_t	info;
		processor_pkg_info_t	pkg;
		int	nprocessor, ncore, nthread;

		info = scan_cpu_info();

		nprocessor = 0;
		ncore = 0;
		nthread = 0;

		if (!info) {
			/*
			 * Can not get CPU information (older cpu type).
			 * Display CPU type is "i386"
			 * the number of core and number of thread is 1
			 */
			PRINTF("CPU Type:i386\n");
			nprocessor = 1;
			ncore = 1;
			nthread = 1;
		} else {
			PRINTF("CPU Type:%s\n", info->name);
			pkg = info;
			while (pkg) {
				if (pkg->num_core > ncore) {
					ncore = pkg->num_core;
				}

				if (pkg->num_thread > nthread) {
					nthread = pkg->num_thread;
				}

				nprocessor++;
				pkg = pkg->next;
			}

			free_cpu_info(info);
		}

		PRINTF("CPU Number:%d\n", nprocessor);
		PRINTF("Number of cores per processor:%d\n", ncore);
		PRINTF("Number of threads per processor:%d\n", nthread);
	}

	if ((operate & OPT_SYS)) {
		PRINTF("System Information:\n");
		print_system_info(hdl);
		PRINTF("\n");
	}

	if ((operate & OPT_BIOS)) {
		PRINTF("BIOS Information:\n");
		print_bios_info(hdl);
		PRINTF("\n");
	}

	if ((operate & OPT_MB)) {
		PRINTF("MotherBoard Information:\n");
		print_motherboard_info(hdl);
		PRINTF("\n");
	}

	if ((operate & OPT_PRO)) {
		PRINTF("CPU Information:\n");
		print_processor_info(hdl);
		PRINTF("\n");
	}

	if ((operate & OPT_MEM)) {
		PRINTF("Memory Information:\n");
		print_memory_subsystem_info(hdl);
		PRINTF("\n");
	}

	if (hdl) {
		smbios_close(hdl);
	}

	return (0);
}

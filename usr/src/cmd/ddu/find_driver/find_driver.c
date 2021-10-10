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
 * Find device driver from local driver database by
 * device compatible name.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <libgen.h>
#include <sys/utsname.h>
#include "libddudev.h"

static char 	*drvinfo_file = "/usr/ddu/data/driver.ddu";
static char 	*drvinfo_file_1 = "/usr/ddu/data/driver_a.ddu";
static char 	*nothing	= "";

static char *comp_name = NULL;
static int	num_name = 0;


/* define the index in result array (comparray): */
#define	i_driver_name	1
#define	i_driver_location	2
#define	i_driver_bit	4
#define	i_driver_type	5
#define	i_driver_dlink	6

#define	PRINTF		(void) printf

static char 	*comparray[7];

/* find_driver usage */
void
usage()
{
	FPRINTF(stderr, "Usage: find_driver [-p {device pci path}] "
	    "[-n {compatible names}]");
	exit(1);
}

/*
 * Get argument compatible name number
 * and split each compatible with '\0'
 *
 * arg: point compatible name argument
 */
void
check_name_option(char *arg)
{
	char *str = arg;

	while ((str = strchr(str, ' ')) != NULL) {
		*str = '\0';
		str = str + 1;
		num_name++;
	}

	comp_name = arg;
	num_name++;
}

/*
 * Check target device node
 * target device node specify by arg (*dev_info)
 * get target device node compatible names
 * This is called indirectly via di_walk_node.
 *
 * node: device node handle
 * arg: point dev_info struct, specify target device id.
 *
 * return:
 * DI_WALK_TERMINATE: found the device.
 * DI_WALK_CONTINUE: continue lookup.
 */
int
check_dev(di_node_t node, void *arg)
{
	int		ret;
	dev_info 	*info;

	info = (dev_info *)arg;
	ret = dev_match(node, info);

	if (ret == 0) {
		num_name = di_compatible_names(node, &comp_name);
		return (DI_WALK_TERMINATE);
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Find device driver information from DDU driver db
 * According device node compatible names to lookup
 * driver information in DDU driver db.
 * device compatible names:
 *  7."pci vendor_id, device_id, subvendor_id, subsys_id, rev_id"
 *  6."pci vendor_id, device_id, subvendor_id, subsys_id"
 *  5."pci subvendor_id, subsys_id"
 *  4."pci vendor_id, device_id, rev_id"
 *  3. "pci vendor_id, device_id"
 *  2. "pci class_code&interface"
 *  1. "pci class_code"
 * Note: the compatible name has priority, 7>6>5....
 * It means, No.7 is the most the accurate name to identify the device driver.
 *
 * Policy:
 * a:  Prefer get device driver information by highest compatible name.
 * b:  Prefer get solaris device driver("driver_type" is "S") for device.
 *
 * Return:
 * 0: Found device driver information
 * 1: Fail to get device driver information
 */
int
find_driver(char *driverinfo)
{
	FILE *fd_drv_info;
	char line[LINE_MAX];
	char line_tok[LINE_MAX];
	int  found, i, j;
	char *comp_str;
	char *str;
	char *record_items[3];

	if (num_name == 0) {
		return (1);
	}

	if ((fd_drv_info = fopen(drvinfo_file, "r")) == NULL) {
		(void) fprintf(stderr, "\n");
		perror(drvinfo_file);
		exit(1);
	}

	found = 0;

	while (fgets(line, LINE_MAX, fd_drv_info) != NULL) {
		/*
		 * Done with the 3rd-party section if we encounter the
		 * "---video driver--- line.
		 */
		if (strstr(line, "---video driver") != NULL) {
			break;
		}

		/* Discard comment lines starting with - or #. */
		if (line[0] == '#' || line[0] == '-') {
			continue;
		}

		str = strchr(line, '\n');
		if (str) {
			*str = '\0';
		}

		(void) strcpy(line_tok, line);
		/*
		 * Split the line by '\t'
		 * record_items[0]: "pci_string"
		 * record_items[1]: driver_name
		 * record_items[2]: driver_type
		 * 		'S' Solaris driver
		 * 		'T' 3rd-party driver
		 */
		record_items[0] = strtok(line_tok, "\t");
		record_items[1] = strtok(NULL, "\t");
		record_items[2] = strtok(NULL, "\t");

		/*
		 * All Solaris driver records have been searched,
		 * and the first 3rd-party record is being read.
		 */
		if ((record_items[2] != NULL) &&
		    (strcmp(record_items[2], "T") == 0)) {
			/*
			 * A match was already found before entering
			 * the third-party section.
			 * If the match is not "vgatext",
			 * it is valid and we're done.
			 * ("vgatext" is not a specific driver for a
			 * device and so it is invalid.)
			 */
			if ((found > 0) &&
			    (strstr(driverinfo, "\tvgatext\t") ==
			    NULL)) {
				break;
			}
		}

		/*
		 * Match the highest priority compatible name.
		 * Names are listed here by highest priority names first.
		 */
		i = num_name;
		comp_str = comp_name;

		while (i > 0) {
			if ((strcmp(record_items[0], comp_str) == 0)) {
				break;
			}

			j = strlen(comp_str);
			comp_str = comp_str + j + 1;
			i = i -1;
		}

		/*
		 * Matched higher prority compatible name
		 * Store this line in "driverinfo"
		 */
		if (i > found) {
			found = i;
			(void) strcpy(driverinfo, line);
		}

		/*
		 * Matched the highest compatible name
		 * Stop search
		 */
		if (found == num_name) {
			break;
		}
	}

	(void) fclose(fd_drv_info);

	if (found > 0) {
		return (0);
	} else {
		return (1);
	}
}

/*
 * Find driver link information from DDU driver link db
 */
void
find_link(char *linkinfo)
{
	char *str;
	FILE *link_file;
	char *driver_name, *driver_type, *driver_dlink;

	if ((link_file = fopen(drvinfo_file_1, "r")) == NULL) {
		/* it's ok, as it's additional file */
		perror(drvinfo_file_1);
		return;
	}

	while (fgets(linkinfo, LINE_MAX, link_file) != NULL) {

		if (linkinfo[0] == '#' || linkinfo[0] == '-') {
			continue;
		}

		str = strchr(linkinfo, '\n');
		if (str) {
			*str = '\0';
		}

		if ((driver_name = strtok(linkinfo, "\t")) == NULL) {
			continue;
		}

		driver_type = strtok(NULL, "\t");
		driver_dlink = strtok(NULL, "\t");

		if (strcmp(comparray[i_driver_name], driver_name) == 0) {
			comparray[i_driver_type] = driver_type;
			comparray[i_driver_dlink] = driver_dlink;
			break;
		}
	}
	(void) fclose(link_file);
}

int
main(int argc, char **argv)
{
	int    c;
	di_node_t    root_node = DI_NODE_NIL;
	dev_info 	arg_dev_info;
	int i, ret, status;
	char *tmparray[5];
	char	drv_info[LINE_MAX];
	char	link_info[LINE_MAX];

	arg_dev_info.pci_path = NULL;
	arg_dev_info.devfs_path = NULL;

	while ((c = getopt(argc, argv, "p:n:")) != EOF) {
		switch (c) {
		case 'p':
			root_node = di_init("/",
			    DINFOSUBTREE|DINFOMINOR|DINFOPROP|DINFOLYR);

			if (root_node == DI_NODE_NIL) {
				perror("di_init() failed");
				return (1);
			}
			process_arg(optarg, &arg_dev_info);
			(void) di_walk_node(root_node, DI_WALK_CLDFIRST,
			    &arg_dev_info, check_dev);
			break;
		case 'n':
			check_name_option(optarg);
			break;
		default:
			usage();
		}
	}

	for (i = 0; i < 7; i++) {
		comparray[i] = NULL;
	}

	ret = find_driver(drv_info);

	if (root_node != DI_NODE_NIL) {
		di_fini(root_node);
	}

	if (ret) {
		/*
		 * No driver found, status is 4.
		 * driver name, driver location, driver bit,
		 * driver type is blank and
		 * driver link is "N"(not available)
		 */
		PRINTF("4|||||N\n");
		return (1);
	}

	tmparray[0] = strtok(drv_info, "\t");
	tmparray[1] = strtok(NULL, "\t");
	tmparray[2] = strtok(NULL, "\t");
	tmparray[3] = strtok(NULL, "\t");
	tmparray[4] = strtok(NULL, "\t");

	comparray[i_driver_name] = tmparray[1];
	comparray[i_driver_bit] = tmparray[3];
	comparray[i_driver_location] = tmparray[4];

	if (strcmp(tmparray[2], "T") == 0) {
		/* third-party driver */
		status = 2;
	} else if (strcmp(tmparray[2], "S") == 0) {
		/* solaris driver */
		status = 1;
	} else if (strcmp(tmparray[2], "O") == 0) {
		/* OpenSolaris driver */
		status = 3;
	} else {
		/* bad driver.db */
		(void) fprintf(stderr, "Error: bad driver.db file.\n");
		exit(1);
	}

	/*
	 * If it's 3rd party driver, find the driver type and download link
	 */
	if ((status == 2) || (status == 3)) {
		find_link(link_info);
	}

	for (i = 0; i < 7; i++) {
		if (comparray[i] == NULL) {
			comparray[i] = nothing;
		}
	}

	PRINTF("%d|%s|%s|%s|%s|%s\n", status, comparray[i_driver_name],
	    comparray[i_driver_location], comparray[i_driver_bit],
	    comparray[i_driver_type], comparray[i_driver_dlink]);
	return (0);
}

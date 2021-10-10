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
 * List storage devices in the system.
 * Get storage device information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sunddi.h>

#include "libddudev.h"
#include "disk_info.h"
#include "mpath.h"

/* get devices name information functions */
char *(*get_name_info[])(di_node_t) = {
	get_inq_ven_info,
	get_usb_pro_info,
	get_devid_info,
	NULL
};

/* devices name functions index */
enum name_index {
	INQUIRY_INFO,
	USB_PRO_INFO,
	DEVID_INFO,
	INFO_END
};

/* devices link path handle */
static di_devlink_handle_t	hDevLink = NULL;

/* hd_detect usage */
void
usage()
{
	FPRINTF(stderr,
	    "Usage: hd_detect [lr] [c {devfs path}] [-m {disk path}]\n");
	FPRINTF(stderr, "       -l: ");
	FPRINTF(stderr, "List block devices\n");
	FPRINTF(stderr, "       -r: ");
	FPRINTF(stderr, "List remove medias\n");
	FPRINTF(stderr, "       -c {devfs path}: ");
	FPRINTF(stderr, "List block devices in this controller\n");
	FPRINTF(stderr, "       -m {disk path}: ");
	FPRINTF(stderr, "Print disk information\n");
	exit(1);
}

/*
 * Print information, replace ':" with ' '
 *
 * info:  information string
 */
void
prt_info(const char *info)
{
	char 	*str;
	char 	*pos;

	if (strchr(info, ':')) {
		str = strdup(info);
		if (str) {
			pos = strchr(str, ':');
			while (pos) {
				*pos = ' ';
				pos = strchr(pos, ':');
			}

			PRINTF("%s", str);
			free(str);
		}
	} else {
		PRINTF("%s", info);
	}
}

/*
 * Print device type information
 *
 * node: device node handle
 * wides: indentation level.
 *
 * output (printed):
 * Device name:DEVID:CLASS:devfs_path:driver_name:
 * driver_instance:driver_state:VENDOR
 */
void
prt_hd_info(di_node_t node, const char *disk_path)
{
	char 		*str;
	char 		*str1;
	char 		*node_name;
	int		ret;
	int		i;

	node_name = di_node_name(node);

	for (i = INQUIRY_INFO; i < INFO_END; i++) {
		str = get_name_info[i](node);

		if (str) {
			break;
		}
	}

	if (i < INFO_END) {
		if (i == DEVID_INFO) {
			str1 = strstr(str, "@A");
			if (str1) {
				str = str1 + 2;
			}

			if (strchr(str, '=')) {
				str1 = strdup(str);

				if (str1) {
					str = str1;
					str1 = strchr(str, '=');
					*str1 = '\0';
					prt_info(str);
					free(str);
				} else {
					prt_info(str);
				}
			} else {
				prt_info(str);
			}
		} else {
			prt_info(str);
		}
		ret = 0;
	} else {
		ret = 1;
	}

	switch (i) {
		case INQUIRY_INFO:
			str = get_inq_pro_info(node);
			if (str) {
				PRINTF(" ");
				prt_info(str);
			}
			break;
		default:
		;
	}

	if (ret) {
		if (node_name) {
			prt_info(node_name);
		} else {
			PRINTF("unknown");
		}
	}
	PRINTF(":");

	/* print devfs path */
	str = di_devfs_path(node);
	if (str != NULL) {
		PRINTF("%s:", str);
		di_devfs_path_free(str);
	} else {
		PRINTF("unknown:");
	}

	PRINTF("%s", disk_path);

	PRINTF("\n");
}

/*
 * Print block devices in this initiator port
 * According initiator port to match multipath logical unit list
 * If matched, print disk info
 *
 * port: initiator port
 */
void
prt_mpath_devices(char *port)
{
	lu_obj 		*obj;

	obj = getInitiaPortDevices(port, NULL);

	while (obj) {
		if (obj->node != DI_NODE_NIL) {
			prt_hd_info(obj->node, obj->path);
		}

		obj = getInitiaPortDevices(port, obj);
	}
}

/*
 * Print block device information
 * Check node device link, if the device link's format is:
 * /dev/rdsk/...s2, then get its information and print it.
 * This is called indirectly via di_devlink_walk
 *
 * devlink: node device link
 * arg: device node handle
 *
 * Return:
 * DI_WALK_CONTINUE: not found available device link, continue walk tree.
 * DI_WALK_TERMINATE: found device link, stop walk
 */
int
check_hdlink(di_devlink_t devlink, void *arg)
{
	const char *link_path;
	di_node_t node;

	link_path = di_devlink_path(devlink);

	if (strncmp(link_path, "/dev/rdsk/", 10) == 0) {
		if (strcmp("s2", strrchr(link_path, 's')) == 0) {
			node = (di_node_t)arg;
			prt_hd_info(node, link_path);
			return (DI_WALK_TERMINATE);
		}
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Determine remove media
 * Check node properties, to find "removable-media" property
 * If found, it is a remove media.
 *
 * node: device node handle
 *
 * Return:
 * If found, return 0, else return 1
 */
boolean_t
is_remove_media(di_node_t node)
{
	di_prop_t		prop;
	char 			*str;
	int			ret;

	prop = di_prop_next(node, DI_PROP_NIL);

	while (prop != DI_PROP_NIL) {
		str = di_prop_name(prop);
		ret = strcmp(REMOVE_MEDIA, str);

		if (ret == 0) {
			return (B_TRUE);
		}

		ret = strcmp(HOTPLUG_ABLE, str);

		if (ret == 0) {
			return (B_TRUE);
		}
		prop = di_prop_next(node, prop);
	}

	return (B_FALSE);
}

/*
 * Print remove media information
 * Check node device link, if the device link's format is:
 * /dev/rdsk/...s2, then determine whether it is a remove media.
 * If it is a remove media, display its information.
 * This is called indirectly via di_devlink_walk
 *
 * devlink: node device link
 * arg: device node handle
 *
 * Return:
 * DI_WALK_CONTINUE: not found available device link, continue walk tree.
 * DI_WALK_TERMINATE: found device link, stop walk
 */
int
check_rmlink(di_devlink_t devlink, void *arg)
{
	const char *link_path;
	di_node_t node;

	link_path = di_devlink_path(devlink);

	if (strncmp(link_path, "/dev/rdsk/", 10) == 0) {
		if (strcmp("s2", strrchr(link_path, 's')) == 0) {
			node = (di_node_t)arg;

			if (is_remove_media(node)) {
				prt_hd_info(node, link_path);
			}

			return (DI_WALK_TERMINATE);
		}
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Lookup which disk is multipath logical unit
 * For each disk, match its device link with multipath logical unit
 * list, if matched, record disk node.
 * This is called indirectly via di_walk_minor
 *
 * node: device node handle
 * minor: device minor node handle
 * arg: device link handle
 *
 * Return:
 * DI_WALK_CONTINUE: continue walk
 */
int
/* LINTED E_FUNC_ARG_UNUSED */
lookup_mpath_dev(di_node_t node, di_minor_t minor, void *arg)
{
	char 	*minor_path;
	int	ret;

	ret = strncmp(DDI_NT_CD, di_minor_nodetype(minor),
	    strlen(DDI_NT_CD));
	/* Don't check cd device */
	if (ret != 0) {
		minor_path = di_devfs_minor_path(minor);
		if (minor_path) {
			(void) di_devlink_walk(hDevLink, NULL, minor_path, 0,
			    node, check_mpath_link);
			di_devfs_path_free(minor_path);
		}
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Lookup devices with "initiator-port" property
 * If found list block devices under it.
 *
 * node: device node handle
 */
void
lookup_child(di_node_t node)
{
	int	ret;
	char 	*prop_str;

	/* if this is multipath initiator port node, print mpath devices */
	ret = lookup_node_strings(node, PROP_INIT_PORT, (char **)&prop_str);

	if (ret > 0) {
		prt_mpath_devices(prop_str);
	}

	/* recursion lookup node child and sibling */
	for (node = di_child_node(node); node != DI_NODE_NIL;
	    node = di_sibling_node(node)) {
		lookup_child(node);
	}
}

/*
 * List block devices in system
 * According minor node type "ddi_block", to
 * list block devices in system
 * This is called indirectly via di_walk_minor
 *
 * node: device node handle
 * minor: device minor node handle
 * arg: device link handle
 *
 * Return:
 * DI_WALK_CONTINUE: continue walk
 */
int
/* LINTED E_FUNC_ARG_UNUSED */
check_hddev(di_node_t node, di_minor_t minor, void *arg)
{
	char *minor_path;
	int	ret;

	ret = strncmp(DDI_NT_CD, di_minor_nodetype(minor),
	    strlen(DDI_NT_CD));
	/* Don't check cd device */
	if (ret != 0) {
		minor_path = di_devfs_minor_path(minor);
		if (minor_path) {
			(void) di_devlink_walk(hDevLink, NULL, minor_path,
			    DI_PRIMARY_LINK, node, check_hdlink);
			di_devfs_path_free(minor_path);
		}
	}

	return (DI_WALK_CONTINUE);
}

/*
 * List remove media in system
 * According minor node type "ddi_block", to
 * lookup remove media in system
 * This is called indirectly via di_walk_minor
 *
 * node: device node handle
 * minor: device minor node handle
 * arg: device link handle
 *
 * Return:
 * DI_WALK_CONTINUE: continue walk
 */
int
/* LINTED E_FUNC_ARG_UNUSED */
check_rmdev(di_node_t node, di_minor_t minor, void *arg)
{
	char *minor_path;

	minor_path = di_devfs_minor_path(minor);
	if (minor_path) {
		(void) di_devlink_walk(hDevLink, NULL, minor_path,
		    DI_PRIMARY_LINK, node, check_rmlink);
		di_devfs_path_free(minor_path);
	}

	return (DI_WALK_CONTINUE);
}

/*
 * List block devices attached in specified controller
 * Note: If this controller is a multipath controller
 * ( controller has "initiator-port" property)
 * follow its "initiator-port" number to get its logical units (child devices).
 *
 * node: device node handle
 * arg: point dev_info struct, specify target device id.
 *
 * Return:
 * DI_WALK_TERMINATE: found the device.
 * DI_WALK_CONTINUE: continue lookup.
 */
int
check_dev(di_node_t node, void *arg)
{
	int		ret;
	char 		*prop_str;
	dev_info 	*info;

	info = (dev_info *)arg;
	ret = dev_match(node, info);
	if (ret == 0) {
		ret = lookup_node_strings(node,
		    PROP_INIT_PORT, (char **)&prop_str);

		if (ret > 0) {
			prt_mpath_devices(prop_str);
		}

		lookup_child(node);

		(void) di_walk_minor(node, DDI_NT_BLOCK,
		    0, NULL, check_hddev);
		return (DI_WALK_TERMINATE);
	}

	return (DI_WALK_CONTINUE);
}

int
main(int argc, char **argv)
{
	int		c, ret;
	dev_info	arg_dev_info;
	di_node_t	root_node = DI_NODE_NIL;	/* root node handle */

	if (argc < 2) {
		usage();
	}

	arg_dev_info.pci_path = NULL;
	arg_dev_info.devfs_path = NULL;

	c = getopt(argc, argv, "lrc:m:");

	switch (c) {
		case 'l':
		case 'r':
		case 'c':
			root_node = di_init("/", DINFOSUBTREE|DINFOMINOR|
			    DINFOPROP|DINFOLYR);
			if (root_node == DI_NODE_NIL) {
				perror("di_init() failed");
				return (1);
			}
			hDevLink = di_devlink_init(NULL, 0);
			if (hDevLink == NULL) {
				perror("di_devlink_init() failed");
				di_fini(root_node);
				return (1);
			}
			break;
		case 'm':
			ret = prt_disk_info(optarg);
			return (ret);
		default:
			usage();
	}

	switch (c) {
		case 'l':
			ret = di_walk_minor(root_node, DDI_NT_BLOCK,
			    0, NULL, check_hddev);
			break;
		case 'r':
			ret = di_walk_minor(root_node, DDI_NT_BLOCK,
			    0, NULL, check_rmdev);
			break;
		case 'c':
			if (mpath_init() == 0) {
				(void) di_walk_minor(root_node, DDI_NT_BLOCK,
				    0, hDevLink, lookup_mpath_dev);
			}
			process_arg(optarg, &arg_dev_info);
			ret = di_walk_node(root_node, DI_WALK_CLDFIRST,
			    &arg_dev_info, check_dev);
			mpath_fini();
			break;
	}

	if (hDevLink != NULL) {
		(void) di_devlink_fini(&hDevLink);
	}

	if (root_node != DI_NODE_NIL) {
		di_fini(root_node);
	}

	return (ret);
}

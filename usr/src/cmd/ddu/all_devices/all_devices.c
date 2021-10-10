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

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>

#include "libddudev.h"
#include "all_devices.h"
#include "ids.h"

/* record number of controller */
static int		num_con = 0;

/* get devices id information functions */
int (*get_dev_id[])(di_node_t, dev_id*) = {
	get_con_id,
	get_usb_id,
	NULL
};

/* devices id functions index */
enum id_index {
	CON_ID,
	USB_ID,
	ID_END
};

/* get devices name information functions */
char *(*get_name_info[])(di_node_t) = {
	get_model_info,
	get_inq_ven_info,
	get_usb_pro_info,
	get_devid_info,
	NULL
};

/* devices name functions index */
enum name_index {
	MODEL_INFO,
	INQUIRY_INFO,
	USB_PRO_INFO,
	DEVID_INFO,
	INFO_END
};

/* all_devices options */
static int		o_all = 0;
static int		o_list = 0;
static int		o_verbose = 0;
static char		o_indent = '\t';

/* all_devices usage */
void
usage()
{
	FPRINTF(stderr, "Usage: alldevices [ -acmsuv ] [-F {format}] ");
	FPRINTF(stderr, "[-dilLnpr {devfs path}] [-t {devices type}]\n");
	FPRINTF(stderr, "       -a: ");
	FPRINTF(stderr, "all devices tree\n");
	FPRINTF(stderr, "       -c: ");
	FPRINTF(stderr, "print controller information\n");
	FPRINTF(stderr, "       -C: ");
	FPRINTF(stderr, "print controller id\n");
	FPRINTF(stderr, "       -d {devfs path}: ");
	FPRINTF(stderr, "Print device information\n");
	FPRINTF(stderr, "       -F {format}: ");
	FPRINTF(stderr, "Print device tree shrink format\n");
	FPRINTF(stderr, "       -i (devfs path}: ");
	FPRINTF(stderr, "Print device minor path and path link\n");
	FPRINTF(stderr, "       -l {devfs path}: ");
	FPRINTF(stderr, "List device tree\n");
	FPRINTF(stderr, "       -L {devfs path}: ");
	FPRINTF(stderr, "List device tree leaves node\n");
	FPRINTF(stderr, "       -n {devfs path}: ");
	FPRINTF(stderr, "List device compatible names\n");
	FPRINTF(stderr, "       -p {devfs path}: ");
	FPRINTF(stderr, "Print device paraent controller information\n");
	FPRINTF(stderr, "       -r {devfs path}: ");
	FPRINTF(stderr, "Print children devices information\n");
	FPRINTF(stderr, "       -s: ");
	FPRINTF(stderr, "Rescan the system, clean cache and force "
	    "attach driver\n");
	FPRINTF(stderr, "       -t {devices type}: ");
	FPRINTF(stderr, "Print devices list by device type\n");
	FPRINTF(stderr, "       -v: ");
	FPRINTF(stderr, "Print controller or device verbose information, ");
	FPRINTF(stderr, "combine with -d and -t\n");
}

/*
 * Output indentation level
 *
 * wides: indentation level
 * default indentation is TAB
 */
void
prt_form(int wides)
{
	int	i;

	for (i = 0; i < wides; i++) {
		PRINTF("%c", o_indent);
	}
}

/*
 * Print information, replacing ':" with ' '
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
 * Print node property information,
 * property type: int, byte, int64,
 * strings, or boolean.
 *
 * prop: device node property handle
 * wides: indentation level.
 */
void
prt_prop(di_prop_t prop, int wides)
{
	int	type;
	int	ret;
	char 	*str_type;
	int 	*int_type;
	int64_t 	*int64_type;
	uchar_t		*byte_type;


	if (prop == DI_PROP_NIL) {
		return;
	}

	prt_form(wides);

	/* print node property name */
	str_type = di_prop_name(prop);
	if (str_type != NULL) {
		PRINTF("%s:", str_type);
	} else {
		PRINTF("unknown:");
	}

	/*
	 * print node property value
	 * First get node property value type:
	 * int, byte, int64, bool or string
	 */

	type = di_prop_type(prop);
	switch (type) {
		case DI_PROP_TYPE_INT:
			ret = di_prop_ints(prop, &int_type);
			if (ret >= 0) {
				PRINTF("%x", *int_type);
			}
			break;
		case DI_PROP_TYPE_BYTE:
			ret = di_prop_bytes(prop, &byte_type);
			if (ret >= 0) {
				PRINTF("%x", *byte_type);
			}
			break;
		case DI_PROP_TYPE_INT64:
			ret = di_prop_int64(prop, &int64_type);
			if (ret >= 0) {
				PRINTF("%llx", *int64_type);
			}
			break;
		case DI_PROP_TYPE_STRING:
			ret = di_prop_strings(prop, &str_type);
			if (ret >= 0) {
				while (*str_type == ' ') {
					str_type++;
				}
				PRINTF("%s", str_type);
			}
			break;
		case DI_PROP_TYPE_BOOLEAN:
			PRINTF("TRUE");
			break;
		default:
			PRINTF("unknown");
	}
	PRINTF("\n");
}

/*
 * Print node PROM property information
 *
 * prop_prop: device node PROM property handle
 * wides: indentation level.
 */
void
prt_prom_prop(di_prom_prop_t prom_prop, int wides)
{
	uchar_t 	*data;
	char 	*str;
	int	ret;
	int	i;

	if (prom_prop == DI_PROM_PROP_NIL) {
		return;
	}

	prt_form(wides);

	/* print prom property name */
	str = di_prom_prop_name(prom_prop);
	if (str != NULL) {
		PRINTF("%s:", str);
	} else {
		PRINTF("unknown:");
	}

	/* print prom property value */
	ret = di_prom_prop_data(prom_prop, &data);
	for (i = 0; i < ret; i++) {
		PRINTF("%d", data[i]);
	}

	PRINTF("\n");
}

/*
 * Print node compatible names
 *
 * node: device node handle
 * wides: indentation level
 */
void
prt_compatible_names(di_node_t node, int wides)
{
	char 	*str;
	int	ret, i, j;

	/* print compatible names */
	ret = di_compatible_names(node, &str);
	for (i = 0; i < ret; i++) {
		prt_form(wides);
		PRINTF("%s\n", str);
		j = strlen(str);
		str = str + j + 1;
	}
}

/*
 * Print node information
 *
 * node: device node handle
 * wides: indentation level
 *
 * output (printed):
 * node name
 * binding name
 * devfs path
 * compatible name
 * driver name
 * driver instance(if instance >=0)
 * driver state (if has driver)
 */
void
prt_node_info(di_node_t node, int wides)
{
	char 	*str;
	int	ret, i, j;
	unsigned int	bus, dev, func;
	dev_id	id;
	dev_id_name	name;

	prt_form(wides);
	PRINTF("node name:");

	/* print node name */
	str = di_node_name(node);
	if (str != NULL) {
		PRINTF("%s\n", str);
	} else {
		PRINTF("unknown\n");
	}

	/* get devices id */
	/* try device as a PCI device and then as a USB device. */
	for (i = 0; i < ID_END; i++) {
		ret = get_dev_id[i](node, &id);
		if (ret) {
			break;
		}
	}

	/* if vendor id, print vendor name */
	if ((i < ID_END) && (id.ven_id != NOVENDOR)) {
		/*
		 * get vendor, device, subvendor and subsytem
		 * description form pci.ids or usb.ids
		 */
		switch (i) {
			case CON_ID:
				ret = FindPciNames(id.ven_id, id.dev_id,
				    id.sven_id, id.subsys_id, &name.vname,
				    &name.dname, &name.svname,
				    &name.sysname);
				break;
			case USB_ID:
				ret = FindUsbNames(id.ven_id, id.dev_id,
				    &name.vname, &name.dname);
				break;
			default:
				ret = 0;
		}

		if (ret) {
			prt_form(wides);
			PRINTF("Vendor: %s\n", name.vname);

			if (name.dname) {
				prt_form(wides);
				PRINTF("Device: %s\n", name.dname);
			}

			if (name.svname) {
				prt_form(wides);
				PRINTF("Sub-Vendor: %s\n", name.svname);
			}

			if (name.sysname) {
				prt_form(wides);
				PRINTF("Sub-System: %s\n", name.sysname);
			}
		}
	}

	prt_form(wides);
	PRINTF("binding name:");

	/* print binding name */
	str = di_binding_name(node);
	if (str != NULL) {
		PRINTF("%s\n", str);
	} else {
		PRINTF("unknown\n");
	}

	prt_form(wides);
	PRINTF("devfs path:");

	/* print devfs path */
	str = di_devfs_path(node);
	if (str != NULL) {
		PRINTF("%s\n", str);
		di_devfs_path_free(str);
	} else {
		PRINTF("unknown\n");
	}

	/* PRINTF bus addr */
	str = di_bus_addr(node);
	if (str) {
		PRINTF("bus addr: %s\n", str);
	}

	/* PRINTF pci path */
	ret = get_pci_path(node, &bus, &dev, &func);

	if (ret == 0) {
		PRINTF("pci path:%x,%x,%x\n", bus, dev, func);
	}

	prt_form(wides);
	PRINTF("compatible name:");

	/* print compatible names */
	/* (name1)(name2)... */
	ret = di_compatible_names(node, &str);
	for (i = 0; i < ret; i++) {
		PRINTF("(%s)", str);
		j = strlen(str);
		str = str + j + 1;
	}
	PRINTF("\n");

	prt_form(wides);
	PRINTF("driver name:");

	/* print driver name and instance */
	str = di_driver_name(node);
	if (str != NULL) {
		PRINTF("%s\n", str);
	} else {
		PRINTF("unknown\n");
	}

	/* print driver instance number, if instance >=0 */
	ret = di_instance(node);
	if (ret >= 0) {
		prt_form(wides);
		PRINTF("instance: %d\n", ret);
	}

	/* print driver state: Attached/Detached */
	if (str != NULL) {
		prt_form(wides);
		PRINTF("driver state:");

		if (di_state(node) & DI_DRIVER_DETACHED) {
			PRINTF("Detached\n");
		} else {
			PRINTF("Attached\n");
		}
	}
}

/*
 * Print controller information
 *
 * node: controller node handle
 * wides: indentation level
 *
 * output (printed):
 * controller-des-information:DEVID:CLASS:devfs_path:driver_name:
 * driver_instance:driver_state:VENDOR
 *
 * return:
 * 0: print controller information
 * 1: the node is not controller
 */
int
prt_con_info(di_node_t node, int wides)
{
	char 	*str;
	char 	*model_info;
	int	ret, flag;
	dev_id	id;
	dev_id_name	name;
	unsigned int	bus_id, dev_id, func_id;

	/* get controller id */
	ret = get_dev_id[CON_ID](node, &id);

	/* lookup node model property */
	model_info = get_name_info[MODEL_INFO](node);

	/*
	 * if no such model and no vendor_id, device_id, class_code,
	 * this is not controller node
	 */
	flag = B_VEN_ID | B_DEV_ID | B_CLASS_CODE;
	if (((ret & flag) != flag) && (model_info == NULL)) {
		return (1);
	}

	if (((ret & B_CLASS_CODE) == B_CLASS_CODE) &&
	    (id.class_code >= 0x120000)) {
		return (1);
	}

	prt_form(wides);

	/* print controller information */
	if (o_all || o_list) {
		PRINTF("(Controller)");
	}

	/* if no vendor id, print system model description */
	if (id.ven_id == NOVENDOR) {
		prt_info(model_info);
		PRINTF(":");
	} else {
		/*
		 * get controller vendor, device
		 * description form pci.ids
		 */
		ret = FindPciNames(id.ven_id, id.dev_id,
		    id.sven_id, id.subsys_id, &name.vname,
		    &name.dname, &name.svname,
		    &name.sysname);
		if (ret > 1) {
			prt_info(name.vname);
			if (name.dname) {
				PRINTF(" ");
				prt_info(name.dname);
			}
			PRINTF(":");
		} else {
			if (model_info) {
				prt_info(model_info);
				PRINTF(":");
			} else {
				PRINTF("unknown:");
			}
		}
	}

	/* print device id information */
	if (id.dev_id != NODEVICE) {
		PRINTF("DEVID=0x%04x:", id.dev_id);
	} else {
		PRINTF("DEVID=unknown:");
	}

	/* print class code */
	if (id.class_code != UNDEF_CLASS) {
		PRINTF("CLASS=%08x:", id.class_code);
	} else {
		PRINTF("CLASS=unknown:");
	}

	ret = get_pci_path(node, &bus_id, &dev_id, &func_id);

	if (ret == 0) {
		PRINTF("[%x,%x,%x]:", bus_id, dev_id, func_id);
	} else {
		/* print devfs path */
		str = di_devfs_path(node);
		if (str != NULL) {
			PRINTF("%s:", str);
			di_devfs_path_free(str);
		} else {
			PRINTF("unknown:");
		}
	}

	/* print driver name and state */
	str = di_driver_name(node);
	ret = di_instance(node);
	if (str != NULL) {
		prt_info(str);
		PRINTF(":");
		PRINTF("%d:", ret);

		if (di_state(node) & DI_DRIVER_DETACHED) {
			PRINTF("Detached:");
		} else {
			PRINTF("Attached:");
		}
	} else {
		PRINTF("unknown:%d:Detached:", ret);
	}

	/* print vendor id information */
	if (id.ven_id != NOVENDOR) {
		PRINTF("VENDOR=0x%04x", id.ven_id);
	} else {
		PRINTF("VENDOR=unknown");
	}

	PRINTF("\n");

	return (0);
}

/*
 * Print controller device id
 * This is called indirectly via the di_walk_node
 *
 * node: device node handle
 * arg: NULL
 *
 * Return:
 * DI_WALK_CONTINUE: continue walk
 */
int
/* LINTED E_FUNC_ARG_UNUSED */
prt_con_id(di_node_t node, void *arg)
{
	int	ret, flag;
	dev_id	id;

	/* get controller id */
	ret = get_dev_id[CON_ID](node, &id);

	/* If found device node vendor id and device id, prt this device */
	flag = B_VEN_ID | B_DEV_ID;
	if ((ret & flag) == flag) {
			if (num_con > 0) {
				PRINTF("\n PCI Controller %d:\n",
				    num_con);
			} else {
				PRINTF(" PCI Controller %d:\n",
				    num_con);
			}

			PRINTF("     Vendor ID:%04x\n", id.ven_id);
			PRINTF("     Device ID:%04x\n", id.dev_id);
			PRINTF("     Class Code:%08x\n", id.class_code);
			PRINTF("     Sub VID:%04x\n", id.sven_id);
			PRINTF("     Sub DID:%04x\n", id.subsys_id);
			PRINTF("     Revision ID:%02x\n", id.rev_id);
			num_con++;
	}
	return (DI_WALK_CONTINUE);
}

/*
 * Print device name
 * for pci device, get vendor id and device id then get device name
 * from pci.ids
 * for usb device, get vendor id and device id then get device name
 * from usb.ids, or get inquiry name from node property
 *
 * node: device node handle
 *
 * output (printed):
 * vendor_name device_name
 */
void
prt_dev_name(di_node_t node)
{
	char 	*str;
	char 	*node_name;
	int	ret;
	int	i;
	dev_id	id;
	dev_id_name	name;

	node_name = di_node_name(node);

	/* get device vendor, device id */
	for (i = 0; i < ID_END; i++) {
		ret = get_dev_id[i](node, &id);
		if (ret) {
			break;
		}
	}

	switch (i) {
		case CON_ID:
			ret = FindPciNames(id.ven_id, id.dev_id,
			    id.sven_id, id.subsys_id, &name.vname,
			    &name.dname, &name.svname,
			    &name.sysname);
			break;
		case USB_ID:
			ret = FindUsbNames(id.ven_id, id.dev_id,
			    &name.vname, &name.dname);
			if ((ret) && (name.dname == NULL)) {
				ret = 0;
			}
			break;
		default:
			ret = 0;
	}

	if (!ret) {
		for (i = INQUIRY_INFO; i < INFO_END; i++) {
			str = get_name_info[i](node);
			if (str) {
				prt_info(str);
				ret = 1;
				break;
			}
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
				break;
		}
	} else {
		prt_info(name.vname);
		if (name.dname) {
			PRINTF(" ");
			prt_info(name.dname);
		}
	}

	if (!ret) {
		if (node_name) {
			prt_info(node_name);
		} else {
			PRINTF("unknown");
		}
	}
}

/*
 * Print device information
 *
 * node: device node handle
 * wides: indentation level
 *
 * output (printed):
 * Device name:binding_name:devfs_path:driver_name:
 * driver_instance:driver_state
 */
void
prt_dev_info(di_node_t node, int wides)
{
	char 	*str;
	int	ret;
	unsigned int	bus_id, dev_id, func_id;

	prt_form(wides);

	/* if list all devices, indicate it is a device not a controller */
	if (o_all || o_list) {
		PRINTF("(Dev)");
	}

	/* print device name */
	prt_dev_name(node);
	PRINTF(":");

	/* print binding name */
	str = di_binding_name(node);
	if (str != NULL) {
		prt_info(str);
		PRINTF(":");
	} else {
		PRINTF("unknown:");
	}

	ret = get_pci_path(node, &bus_id, &dev_id, &func_id);

	if (ret == 0) {
		PRINTF("[%x,%x,%x]:", bus_id, dev_id, func_id);
	} else {
		/* print devfs path */
		str = di_devfs_path(node);
		if (str != NULL) {
			PRINTF("%s:", str);
			di_devfs_path_free(str);
		} else {
			PRINTF("unknown:");
		}
	}

	/* print driver name, driver instance and state */
	str = di_driver_name(node);
	ret = di_instance(node);
	if (str != NULL)	{
		prt_info(str);
		PRINTF(":");
		PRINTF("%d:", ret);

		if (di_state(node) & DI_DRIVER_DETACHED) {
			PRINTF("Detached");
		} else {
			PRINTF("Attached");
		}
	} else {
		PRINTF("unknown:%d:Detached", ret);
	}

	PRINTF("\n");
}

/*
 * Print device type information
 *
 * node: device node handle
 * wides: indentation level
 *
 * output (printed):
 * Device name:DEVID:CLASS:devfs_path:driver_name:driver_instance:
 * driver_state:VENDOR
 */
void
prt_type_info(di_node_t node, int wides)
{
	char 	*str;
	int 	*prop_int;
	int	ret;
	int	i;
	dev_id	id;

	prt_form(wides);

	/* print device name */
	prt_dev_name(node);
	PRINTF(":");

	/* get device vendor, device id */
	for (i = 0; i < ID_END; i++) {
		ret = get_dev_id[i](node, &id);
		if (ret) {
			break;
		}
	}

	/* print device id information */
	if (id.dev_id != NODEVICE) {
		PRINTF("DEVID=0x%04x:", id.dev_id);
	} else {
		PRINTF("DEVID=unknown:");
	}

	/* print class code */
	ret = lookup_node_ints(node, PROM_CLASS, (int **)&prop_int);
	if (ret > 0) {
		PRINTF("CLASS=%08x:", prop_int[0]);
	} else {
		PRINTF("CLASS=unknown:");
	}

	/* print devfs path */
	str = di_devfs_path(node);
	if (str != NULL) {
		PRINTF("%s:", str);
		di_devfs_path_free(str);
	} else {
		PRINTF("unknown:");
	}

	/* print driver name and state */
	str = di_driver_name(node);
	ret = di_instance(node);
	if (str != NULL) {
		prt_info(str);
		PRINTF(":");
		PRINTF("%d:", ret);

		if (di_state(node) & DI_DRIVER_DETACHED) {
			PRINTF("Detached:");
		} else {
			PRINTF("Attached:");
		}
	} else {
		PRINTF("unknown:%d:Detached:", ret);
	}

	/* print vendor id information */
	if (id.ven_id != NOVENDOR) {
		PRINTF("VENDOR=0x%04x", id.ven_id);
	} else {
		PRINTF("VENDOR=unknown");
	}

	PRINTF("\n");
}

/*
 * Print node property information
 *
 * node:  device node handle
 * wides: indentation level
 */
void
prt_node_prop(di_node_t node, int wides)
{
	di_prop_t	prop;

	prop = di_prop_next(node, DI_PROP_NIL);

	while (prop != DI_PROP_NIL) {
		prt_prop(prop, wides);
		prop = di_prop_next(node, prop);
	}
}

/*
 * Print node PROM property information
 *
 * node: device node handle
 * wides: indentation level
 */
void
prt_node_prom_prop(di_node_t node, int wides)
{
	di_prom_prop_t	prom_prop;
	di_prom_handle_t	hProm;

	hProm = di_prom_init();

	if (hProm == DI_PROM_HANDLE_NIL) {
		return;
	}

	prom_prop = di_prom_prop_next(hProm, node, DI_PROM_PROP_NIL);

	while (prom_prop != DI_PROM_PROP_NIL) {
		prt_prom_prop(prom_prop, wides);
		prom_prop = di_prom_prop_next(hProm, node, prom_prop);
	}

	di_prom_fini(hProm);
}

/*
 * Print minor device path and link path
 * This is called indirectly via the di_devlink_walk().
 *
 * devlink: device minor link handle.
 * args: device minor handle
 *
 * output (printed):
 * [device minor type]device link=device minor path
 *
 * return:
 * DI_WALK_CONTINUE: Continue next minor
 */
int
prt_minor_links(di_devlink_t devlink, void *arg)
{
	char 	*str;
	if (di_devlink_path(devlink) != NULL) {
		str = di_minor_nodetype(arg);
		PRINTF("[%s]", (str != NULL) ? str : "unknown");
		PRINTF("%s=%s\n", di_devlink_path(devlink),
		    di_devlink_content(devlink));
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Lookup node child and sibling.
 * If "o_all" or "o_list" is set, list all child tree
 * else just list direct child.
 *
 * node: device node handle
 * wides: indentation level
 */
void
lookup_child(di_node_t node, int wides)
{
	int	ret;

	/* lookup node child and sibling */
	for (node = di_child_node(node); node != DI_NODE_NIL;
	    node = di_sibling_node(node)) {
		ret = prt_con_info(node, wides);
		if (ret > 0) {
			prt_dev_info(node, wides);
		}

		/* recursion lookup node child and sibling */
		if (o_all || o_list) {
			lookup_child(node, wides+1);
		}
	}
}

/*
 * Lookup device tree leaf
 *
 * node: device node handle
 * wides: indentation level
 */
void
lookup_leaf(di_node_t node, int wides)
{
	int	ret;
	di_node_t	ch_node;

	/* recursion lookup node child and sibling */
	for (node = di_child_node(node); node != DI_NODE_NIL;
	    node = di_sibling_node(node)) {
		ch_node = di_child_node(node);

		if (ch_node == DI_NODE_NIL) {
			ret = prt_con_info(node, wides);
			if (ret > 0) {
				prt_dev_info(node, wides);
			}
		}

		lookup_leaf(node, wides);
	}
}

/*
 * Print device information by pci path or devfs path
 * This is called indirectly via di_walk_node.
 *
 * node: device node handle
 * arg: point dev_info struct, specify target device id.
 *
 * output (printed):
 * print device node attributes.
 * if set "verbose", print node soft and hardware attributes.
 *
 * return:
 * DI_WALK_TERMINATE: found the device.
 * DI_WALK_CONTINUE: continue lookup.
 */
int
check_dev(di_node_t node, void *arg)
{
	int	ret;
	dev_info	*info;

	info = (dev_info *)arg;
	ret = dev_match(node, info);
	if (ret == 0) {
		prt_node_info(node, 0);
		if (o_verbose) {
			prt_node_prop(node, 0);
			prt_node_prom_prop(node, 0);
		}
		return (DI_WALK_TERMINATE);
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Lookup device's child by devfs path
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
check_child(di_node_t node, void *arg)
{
	int	ret;
	dev_info 	*info;

	info = (dev_info *)arg;

	ret = dev_match(node, info);
	if (ret == 0) {
		lookup_child(node, 0);
		return (DI_WALK_TERMINATE);
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Lookup device's parent controller by devfs path
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
check_parent_con(di_node_t node, void *arg)
{
	int	ret;
	dev_info 	*info;
	di_node_t	p_node;

	info = (dev_info *)arg;
	ret = dev_match(node, info);
	if (ret == 0) {
		p_node = di_parent_node(node);
		while (p_node != DI_NODE_NIL) {
			ret = prt_con_info(p_node, 0);
			if (ret) {
				p_node = di_parent_node(p_node);
			} else {
				break;
			}
		}
		return (DI_WALK_TERMINATE);
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Lookup device's leaf by devfs path
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
check_leaf(di_node_t node, void *arg)
{
	int	ret;
	dev_info 	*info;

	info = (dev_info *)arg;
	ret = dev_match(node, info);
	if (ret == 0) {
		lookup_leaf(node, 0);
		return (DI_WALK_TERMINATE);
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Print device minor information by devfs path
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
check_minor(di_node_t node, void *arg)
{
	int	ret;
	dev_info 	*info;
	di_devlink_handle_t 	hDevLink = NULL;

	info = (dev_info *)arg;

	ret = dev_match(node, info);
	if (ret == 0) {
		di_minor_t	minor = DI_MINOR_NIL;
		char 		*minor_path;

		hDevLink = di_devlink_init(NULL, 0);
		if (hDevLink == NULL) {
			perror("di_devlink_init failed");
			return (DI_WALK_TERMINATE);
		}

		while ((minor = di_minor_next(node, minor)) != DI_MINOR_NIL) {
			if ((minor_path = di_devfs_minor_path(minor)) ==
			    NULL) {
				perror("failed to allocate minor path");
				return (DI_WALK_TERMINATE);
			}

			(void) di_devlink_walk(hDevLink, NULL, minor_path, 0,
			    minor, prt_minor_links);
			di_devfs_path_free(minor_path);
		}

		(void) di_devlink_fini(&hDevLink);
		return (DI_WALK_TERMINATE);
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Lookup device and list device's compatible names by devfs path
 * This is called indirectly via di_walk_node.
 *
 * node: device node handle
 * arg: point dev_info struct, specify target device id.
 *
 * DI_WALK_TERMINATE: found the device.
 * DI_WALK_CONTINUE: continue lookup.
 */
int
check_name(di_node_t node, void *arg)
{
	int	ret;
	dev_info 	*info;

	info = (dev_info *)arg;
	ret = dev_match(node, info);
	if (ret == 0) {
		prt_compatible_names(node, 0);
		return (DI_WALK_TERMINATE);
	}

	return (DI_WALK_CONTINUE);
}

/*
 * List system all devices information
 * This is called indirectly via di_walk_node.
 *
 * node: device node handle
 * arg:	NULL
 *
 * return:
 * DI_WALK_PRUNECHILD: Continue another device subtree
 */
int
/* LINTED E_FUNC_ARG_UNUSED */
check_all(di_node_t node, void *arg)
{
	lookup_child(node, 0);
	return (DI_WALK_PRUNECHILD);
}

/*
 * List system controller information.
 * This is called indirectly via di_walk_node.
 * If get device vendor id and device id or get "model" property,
 * the node is controller.
 * Print controller devfs and controller information.
 *
 * node: device node
 * arg:	NULL
 *
 * return:
 * DI_WALK_PRUNECHILD: if "o_all" is set, list controller subtree,
 * and continue to another subtree
 * DI_WALK_CONTINUE: continue lookup.
 */
int
/* LINTED E_FUNC_ARG_UNUSED */
check_con(di_node_t node, void *arg)
{
	int	ret;

	ret = prt_con_info(node, 0);
	if (ret == 0) {
		if (o_all) {
			lookup_child(node, 1);
			return (DI_WALK_PRUNECHILD);
		}
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Get device information by device type.
 * If set "o_verbose", list minor type.
 * This is called indirectly via di_walk_minor.
 *
 * node: device node
 * minor: device minor node
 * arg:	device type
 *
 * return:
 * DI_WALK_CONTINUE: continue lookup.
 */
int
check_type(di_node_t node, di_minor_t minor, void *arg)
{
	int	ret;

	if (o_verbose) {
		PRINTF("(%s)", di_minor_nodetype(minor));
	} else {
		ret = strcmp(arg, di_minor_nodetype(minor));
		if (ret != 0) {
			return (DI_WALK_CONTINUE);
		}
	}

	ret = prt_con_info(node, 0);

	if (ret) {
		prt_type_info(node, 0);
	}

	return (DI_WALK_CONTINUE);
}

int
main(int argc, char **argv)
{
	int	c, ret;
	dev_info	arg_dev_info;
	di_node_t	root_node = DI_NODE_NIL;

	root_node = di_init("/", DINFOSUBTREE|DINFOMINOR|
	    DINFOPROP|DINFOLYR);

	if (root_node == DI_NODE_NIL) {
		perror("di_init() failed");
		return (1);
	}

	if (init_pci_ids() != 0) {
		/*
		 * If fail to open and load pci.ids then
		 * get the pci controller name from system device tree
		 */
		FPRINTF(stderr, "can not open and load pci.ids\n");
	}

	if (init_usb_ids() != 0) {
		/*
		 * If fail to open and load usb.ids then
		 * get the usb device name from system device tree
		 */
		FPRINTF(stderr, "can not open and load usb.ids\n");
	}

	arg_dev_info.pci_path = NULL;
	arg_dev_info.devfs_path = NULL;
	ret = 0;

	while ((c = getopt(argc, argv, "acCsvd:F:i:l:L:n:p:r:t:")) != EOF) {
		switch (c) {
		case 'a':
			o_all = 1;
			ret = di_walk_node(root_node, DI_WALK_CLDFIRST,
			    NULL, check_all);
			o_all = 0;
			PRINTF("\n");
			break;
		case 'c':
			ret = di_walk_node(root_node, DI_WALK_CLDFIRST,
			    NULL, check_con);
			PRINTF("\n");
			break;
		case 'C':
			num_con = 0;
			PRINTF("PCI Controllers Information:\n");
			ret = di_walk_node(root_node, DI_WALK_CLDFIRST,
			    NULL, prt_con_id);
			PRINTF("\n");
			break;
		case 'v':
			o_verbose = 1;
			break;
		case 'd':
			process_arg(optarg, &arg_dev_info);
			ret = di_walk_node(root_node, DI_WALK_CLDFIRST,
			    &arg_dev_info, check_dev);
			o_verbose = 0;
			PRINTF("\n");
			break;
		case 'F':
			o_indent = optarg[0];
			break;
		case 'i':
			process_arg(optarg, &arg_dev_info);
			ret = di_walk_node(root_node, DI_WALK_CLDFIRST,
			    &arg_dev_info, check_minor);
			PRINTF("\n");
			break;
		case 'l':
			o_list = 1;
			process_arg(optarg, &arg_dev_info);
			ret = di_walk_node(root_node, DI_WALK_CLDFIRST,
			    &arg_dev_info, check_child);
			o_list = 0;
			PRINTF("\n");
			break;
		case 'L':
			process_arg(optarg, &arg_dev_info);
			ret = di_walk_node(root_node, DI_WALK_CLDFIRST,
			    &arg_dev_info, check_leaf);
			PRINTF("\n");
			break;
		case 'n':
			process_arg(optarg, &arg_dev_info);
			ret = di_walk_node(root_node, DI_WALK_CLDFIRST,
			    &arg_dev_info, check_name);
			PRINTF("\n");
			break;
		case 'p':
			process_arg(optarg, &arg_dev_info);
			ret = di_walk_node(root_node, DI_WALK_CLDFIRST,
			    &arg_dev_info, check_parent_con);
			PRINTF("\n");
			break;
		case 'r':
			process_arg(optarg, &arg_dev_info);
			ret = di_walk_node(root_node, DI_WALK_CLDFIRST,
			    &arg_dev_info, check_child);
			PRINTF("\n");
			break;
		case 's':
			di_fini(root_node);
			root_node = di_init("/", DINFOFORCE|DINFOSUBTREE|
			    DINFOMINOR|DINFOPROP|DINFOPATH|DINFOLYR);
			if (root_node == DI_NODE_NIL) {
				perror("di_init DINFOFORCE failed");
				ret = 1;
			}
			break;
		case 't':
			ret = di_walk_minor(root_node, optarg, 0,
			    optarg, check_type);
			break;
		default:
			usage();
		}
		if (ret != 0) {
			break;
		}
	}

	if (argc < 2) {
		o_all = 1;
		ret = di_walk_node(root_node, DI_WALK_CLDFIRST, NULL,
		    check_con);
		PRINTF("\n");
	}

	fini_pci_ids();
	fini_usb_ids();

	if (root_node != DI_NODE_NIL) {
		di_fini(root_node);
	}
	return (ret);
}

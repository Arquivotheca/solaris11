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
#include <libdevinfo.h>
#include <unistd.h>
#include <libddudev.h>

/*
 * SCSI command on a device
 *
 * fd: device handle.
 * scmd: scsi command.
 *
 * Return:
 * >=0: successful
 */
int
scsi_cmd(int fd, struct uscsi_cmd *scmd, char *rqbuf, int rqsize)
{
	int	i, ret;

	scmd->uscsi_flags |= USCSI_RQENABLE;
	scmd->uscsi_rqlen = rqsize;
	scmd->uscsi_rqbuf = rqbuf;

	for (i = 0; i < RETRIES; i++) {
		scmd->uscsi_status = 0;
		(void) memset(rqbuf, 0, rqsize);

		ret = ioctl(fd, USCSICMD, scmd);

		/* Error I/O */
		if ((ret == 0) && (scmd->uscsi_status == 2)) {
			ret = -1;
			break;
		}

		if ((ret < 0) && (scmd->uscsi_status == 2)) {
			/*
			 * The drive not ready to accept command.
			 * Sleep then retry
			 */
			/*
			 * rqbuf[2](SENSE)=2, rqbuf[12](ASC)=4:
			 * device not ready
			 * rqbuf[13](ASCQ)=0: Not Reportable
			 * rqbuf[13](ASCQ)=1: Becoming ready
			 * rqbuf[13](ASCQ)=4: FORMAT in progress
			 * rqbuf[13](ASCQ)=7: Operation in progress
			 * rqbuf[13](ASCQ)=8: Long write in progress
			 */

			if ((rqbuf[2] == 2) && (rqbuf[12] == 4) &&
			    ((rqbuf[13] == 0) || (rqbuf[13] == 1) ||
			    (rqbuf[13] == 4)) || (rqbuf[13] == 7) ||
			    (rqbuf[13] == 8)) {
				(void) sleep(WAIT_TIME);
				continue;
			}

			/*
			 * Device is not ready to transmit or a device reset
			 * has occurred.
			 */

			if ((rqbuf[2] == 6) && ((rqbuf[12] == 0x28) ||
			    (rqbuf[12] == 0x29))) {
				(void) sleep(WAIT_TIME);
				continue;
			}
		}

		break;
	}

	return (ret);
}

/*
 * Process device id argument.
 * The device specify by pci id or devfs path, vendor id and device id.
 *
 * arg: the argument specify device.
 * info: to store pci id or devfs path.
 * the device identify by pci id or devfs path, vendor id and device id
 * if the device identify by pci id, the argument format is:
 * "[bus id, device id, func id]"
 * else the format is:
 * devfs path:vendor id:device id
 */
void
process_arg(char *arg, dev_info *info)
{
	char 	*str;

	info->pci_path = NULL;
	info->devfs_path = NULL;

	if (arg == NULL) {
		return;
	}

	if (arg[0] == '[') {
		info->pci_path = arg;
		info->pci_bus_id = 0;
		info->pci_dev_id = 0;
		info->pci_func_id = 0;
		(void) sscanf(info->pci_path, "[%x,%x,%x]",
		    &info->pci_bus_id, &info->pci_dev_id, &info->pci_func_id);
	} else {
		info->devfs_path = arg;
		info->con_ven_id = NOVENDOR;
		info->con_dev_id = NODEVICE;

		str = strchr(info->devfs_path, ':');
		if (str == NULL) {
			return;
		}

		*str = '\0';
		str++;
		(void) sscanf(str, "0x%x:0x%x",
		    &info->con_ven_id, &info->con_dev_id);
	}
}

/*
 * lookup node ints prop in firmware or software.
 *
 * hProm: PROM handle
 * node: device node handle
 * name: prop name
 * data: ints buffer pointer
 *
 * Upon successful return ( > 0)
 * property info is returned in the "data" argument
 */
int
lookup_node_ints(di_node_t node, char *name, int **data)
{
	int	ret;
	di_prom_handle_t	hProm;

	ret = -1;

	if (node == DI_NODE_NIL) {
		return (ret);
	}

	ret = di_prop_lookup_ints(DDI_DEV_T_ANY, node, name, data);

	if (ret <= 0) {
		hProm = di_prom_init();
		if (hProm != DI_PROM_HANDLE_NIL) {
			ret = di_prom_prop_lookup_ints(hProm,
			    node, name, data);
			di_prom_fini(hProm);
		}
	}

	return (ret);
}

/*
 * Lookup node strings prop in firmware or software.
 *
 * node: device node handle
 * name: prop name
 * str: string buffer pointer
 *
 * Upon successful return ( > 0)
 * property info is returned in the "str" argument
 */
int
lookup_node_strings(di_node_t node, char *name, char **str)
{
	int	ret;
	di_prom_handle_t	hProm;

	ret = -1;

	if (node == DI_NODE_NIL) {
		return (ret);
	}

	ret = di_prop_lookup_strings(DDI_DEV_T_ANY, node, name, str);

	if (ret <= 0) {
		hProm = di_prom_init();
		if (hProm != DI_PROM_HANDLE_NIL) {
			ret = di_prom_prop_lookup_strings(hProm, node,
			    name, str);
			di_prom_fini(hProm);
		}
	}

	return (ret);
}

/*
 * Get device node prop string
 *
 * node: device node handle
 * name: prop name
 *
 */
char *
get_str_prop_info(di_node_t node, char *prop_name)
{
	char    *prop_str;

	if (node == DI_NODE_NIL) {
		return (NULL);
	}

	if (lookup_node_strings(node, prop_name,
	    (char **)&prop_str) > 0) {
		return (prop_str);
	}

	return (NULL);
}
/*
 * Get node model name
 *
 * node: device node handle
 *
 * Return model name string, if fail return NULL.
 */
char *
get_model_info(di_node_t node)
{
	return (get_str_prop_info(node, PROM_MODEL));
}

/*
 * Get node inquery vendor name
 *
 * node: device node handle
 *
 * Return inquery vendor name string, if fail return NULL.
 */
char *
get_inq_ven_info(di_node_t node)
{
	return (get_str_prop_info(node, PROM_INQUIRY_VENDOR_ID));
}

/*
 * Get node inquery product name
 *
 * node: device node handle
 *
 * return inquery product name string, if fail return NULL.
 */
char *
get_inq_pro_info(di_node_t node)
{
	return (get_str_prop_info(node, PROM_INQUIRY_PRODUCT_ID));
}

/*
 * Get usb product name
 *
 * node: device node handle
 *
 * Return usb product name string, if fail return NULL.
 */
char *
get_usb_pro_info(di_node_t node)
{
	return (get_str_prop_info(node, USB_PRODUCT_NAME));
}

/*
 * Get devid name
 *
 * node: device node handle
 *
 * Return devid name string, if fail return NULL.
 */
char *
get_devid_info(di_node_t node)
{
	return (get_str_prop_info(node, PROM_DEVID));
}

/*
 * Get device pci id information.(bus id, dev id, func id)
 * For bus id, get from device's parent node by keyword "bus-range"
 * For dev id and func id, get from device node by keyword "unit-address"
 *
 * node: device node handle
 * wides: indentation level
 *
 * If get device pci id information then return 0.
 */
int
get_pci_path(di_node_t node, unsigned int *bus_id,
unsigned int *dev_id, unsigned int *func_id)
{
	int 	*prop_int;
	char 	*prop_str;
	di_node_t	p_node;
	int	ret;

	if (node == DI_NODE_NIL) {
		return (1);
	}

	p_node = di_parent_node(node);

	if (p_node == DI_NODE_NIL) {
		return (1);
	}

	ret = lookup_node_ints(p_node, "bus-range", &prop_int);

	if (ret <= 0) {
		return (1);
	}

	*bus_id = *prop_int;

	ret = lookup_node_strings(node, "unit-address", &prop_str);

	if (ret <= 0) {
		return (1);
	}

	*dev_id = 0;
	*func_id = 0;
	(void) sscanf(prop_str, "%x,%x", dev_id, func_id);
	return (0);
}

/*
 * Get controller device id information
 * Include "vendor_id", "device_id",
 * "subvendor_id", "subsystem_id", "revision_id"
 *
 * node: controller device node handle
 * pId:  device id structure.
 *
 * The return value indicate which controller device id is gotten
 * B_VEN_ID: get vendor id
 * B_DEV_ID: get device id
 * B_SUBVEN_ID: get subvendor id
 * B_SUBSYS_ID: get subsystem id
 * B_REV_ID: get revision id
 * B_CLASS_CODE: get classcode
 */
int
get_con_id(di_node_t node, dev_id *pId)
{
	int	ret;
	int	info;
	int 	*prop_int;

	info = 0;

	pId->ven_id = NOVENDOR;
	pId->dev_id = NODEVICE;
	pId->sven_id = NOVENDOR;
	pId->subsys_id = NODEVICE;
	pId->class_code = UNDEF_CLASS;

	if (node == DI_NODE_NIL) {
		return (info);
	}

	ret = lookup_node_ints(node, PROM_VENDOR, (int **)&prop_int);
	if (ret > 0) {
		pId->ven_id = prop_int[0];
		info = info | B_VEN_ID;
	} else {
		return (info);
	}

	ret = lookup_node_ints(node, PROM_DEVICE, (int **)&prop_int);
	if (ret > 0) {
		pId->dev_id = prop_int[0];
		info = info | B_DEV_ID;
	}

	ret = lookup_node_ints(node, PROM_SVENDOR, (int **)&prop_int);
	if (ret > 0) {
		pId->sven_id = prop_int[0];
		info = info | B_SUBVEN_ID;
	}

	ret = lookup_node_ints(node, PROM_SUBSYS, (int **)&prop_int);
	if (ret > 0) {
		pId->subsys_id = prop_int[0];
		info = info | B_SUBSYS_ID;
	}

	ret = lookup_node_ints(node, PROM_REVID, (int **)&prop_int);
	if (ret > 0) {
		pId->rev_id = prop_int[0];
		info = info | B_REV_ID;
	}

	ret = lookup_node_ints(node, PROM_CLASS, (int **)&prop_int);
	if (ret > 0) {
		pId->class_code = prop_int[0];
		info = info | B_CLASS_CODE;
	}

	return (info);
}

/*
 * Get usb device id information.
 * Include "vendor_id", "product_id",
 * "revision_id"
 *
 * node: usb device node handle
 * pId:  device id structure.
 *
 * The return value indicate which usb device id is gotten
 * bit 0: get vendor id
 * bit 1: get device id
 * bit 4: get revision id
 */
int
get_usb_id(di_node_t node, dev_id *pId)
{
	int	ret;
	int	info;
	int 	*prop_int;

	info = 0;

	pId->ven_id = NOVENDOR;
	pId->dev_id = NODEVICE;
	pId->sven_id = NOVENDOR;
	pId->subsys_id = NODEVICE;

	if (node == DI_NODE_NIL) {
		return (info);
	}

	ret = lookup_node_ints(node, PROM_USB_VENDOR, (int **)&prop_int);
	if (ret > 0) {
		pId->ven_id = prop_int[0];
		info = info | B_VEN_ID;
	} else {
		return (info);
	}

	ret = lookup_node_ints(node, PROM_USB_DEVICE, (int **)&prop_int);
	if (ret > 0) {
		pId->dev_id = prop_int[0];
		info = info | B_DEV_ID;
	}

	ret = lookup_node_ints(node, PROM_USB_REVID, (int **)&prop_int);
	if (ret > 0) {
		pId->rev_id = prop_int[0];
		info = info | B_REV_ID;
	}

	return (info);
}

/*
 * Match a device by pci path(bus id, dev id, func id)
 *
 * node: device node handle
 * info: contain device pci bus id, dev id, func id
 *
 * return:
 * 0: successful match a device
 * 1: mismatch
 */
int
match_by_pci_path(di_node_t node, dev_info *info)
{
	int	ret;
	unsigned int bus_id, dev_id, func_id;

	ret = get_pci_path(node, &bus_id, &dev_id, &func_id);

	if (ret) {
		return (1);
	}

	if ((bus_id == info->pci_bus_id) &&
	    (dev_id == info->pci_dev_id) &&
	    (func_id == info->pci_func_id)) {
		return (0);
	}
	return (1);
}

/*
 * Match a device by devfspath
 *
 * node: device node handle
 * info: contain compared device devfspath, vendor id, device id.
 *
 * return:
 * 0: successful match a device
 * 1: mismatch
 */
int
match_by_dev_path(di_node_t node, dev_info *info)
{
	int	ret;
	char 	*str;
	int 	*prop_int;

	if (info->devfs_path == NULL) {
		return (1);
	}

	str = di_devfs_path(node);

	if (str != NULL) {
		if (strcmp(str, info->devfs_path) != 0) {
			di_devfs_path_free(str);
			return (1);
		}

		di_devfs_path_free(str);

		if (info->con_ven_id != NOVENDOR) {
			ret = lookup_node_ints(node, PROM_VENDOR,
			    (int **)&prop_int);

			if (ret > 0) {
				if (info->con_ven_id != *prop_int) {
					return (1);
				}
			} else {
				return (1);
			}
		}

		if (info->con_dev_id != NODEVICE) {
			ret = lookup_node_ints(node, PROM_DEVICE,
			    (int **)&prop_int);

			if (ret > 0) {
				if (info->con_ven_id != *prop_int) {
					return (1);
				}
			} else {
				return (1);
			}
		}
	} else {
		return (1);
	}
	return (0);
}

/*
 * Compare device node attributes with demand arguments.
 * If provide "pci_path", match the device by pci path(bus id, dev id, func id)
 * else match the device by devfs path
 *
 * node: device node handle
 * info: store compared device info, if info->pci_path is not NULL,
 * the device compared by pci id(bus id, dev id, func id).
 * if info->devfs_path is not NULL, the device compared by devfs path,
 * vendor id and device id.
 *
 * return:
 * 0: match the device
 * 1: match failed
 */
int
dev_match(di_node_t node, dev_info *info)
{
	int	ret;

	if (node == DI_NODE_NIL) {
		return (1);
	}

	if (info->pci_path) {
		ret = match_by_pci_path(node, info);
	} else {
		if (info->devfs_path) {
			ret = match_by_dev_path(node, info);
		} else {
			ret = 1;
		}
	}

	return (ret);
}

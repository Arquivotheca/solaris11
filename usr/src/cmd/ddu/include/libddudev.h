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

#ifndef _LIBDDUDEV_H
#define	_LIBDDUDEV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libdevinfo.h>
#include <sys/scsi/impl/uscsi.h>

/* pci vendor, device, subsys, class default value */
#define	NOVENDOR	0xFFFF
#define	NODEVICE	0xFFFF
#define	NOSUBSYS	0xFFFF
#define	UNDEF_CLASS	0xFFFFFFFF

/* indicate which device id get */
#define	B_VEN_ID	0x1
#define	B_DEV_ID	0x2
#define	B_SUBVEN_ID	0x4
#define	B_SUBSYS_ID	0x8
#define	B_REV_ID	0x10
#define	B_CLASS_CODE	0x20

/* some device property name from libdevinfo */
#define	PROM_MODEL	"model"
#define	PROM_DEVICE	"device-id"
#define	PROM_VENDOR	"vendor-id"
#define	PROM_SVENDOR	"subsystem-vendor-id"
#define	PROM_SUBSYS	"subsystem-id"
#define	PROM_REVID	"revision-id"
#define	PROM_CLASS	"class-code"
#define	PROP_INIT_PORT	"initiator-port"

#define	PROM_USB_VENDOR	"usb-vendor-id"
#define	PROM_USB_DEVICE	"usb-product-id"
#define	PROM_USB_REVID	"usb-revision-id"

#define	PROM_DEVID	"devid"
#define	PROM_INQUIRY_VENDOR_ID	"inquiry-vendor-id"
#define	PROM_INQUIRY_PRODUCT_ID	"inquiry-product-id"

#define	USB_PRODUCT_NAME	"usb-product-name"
#define	REMOVE_MEDIA		"removable-media"
#define	HOTPLUG_ABLE		"hotpluggable"

/* some device property name from picl */
#define	NODE_PLATFORM		"platform"
#define	CLASS_PROM		"openprom"
#define	CLASS_CPU		"cpu"
#define	CLASS_MEMBANK		"memory-bank"

/* SCSI command retries and wait time */
#define	RETRIES		10
#define	WAIT_TIME	3

#define	PRINTF		(void) printf
#define	FPRINTF		(void) fprintf

typedef struct {
	unsigned short	ven_id;
	unsigned short	dev_id;
	unsigned short	sven_id;
	unsigned short	subsys_id;
	unsigned int	class_code;
	unsigned char	rev_id;
} dev_id;

typedef struct {
	char 	*pci_path;
	char 	*devfs_path;
	union {
		struct {
			unsigned int	bus_id;
			unsigned int	dev_id;
			unsigned int	func_id;
		} pci_id;
		struct {
			unsigned int	ven_id;
			unsigned int	dev_id;
		} con_id;
	} id;
} dev_info;

#define	pci_bus_id	id.pci_id.bus_id
#define	pci_dev_id	id.pci_id.dev_id
#define	pci_func_id	id.pci_id.func_id
#define	con_ven_id	id.con_id.ven_id
#define	con_dev_id	id.con_id.dev_id

extern int scsi_cmd(int fd, struct uscsi_cmd *scmd, char *rqbuf, int rqsize);
extern void process_arg(char *arg, dev_info *info);
extern int lookup_node_ints(di_node_t node, char *name, int **data);
extern int lookup_node_strings(di_node_t node, char *name, char **str);
extern char *get_model_info(di_node_t node);
extern char *get_inq_ven_info(di_node_t node);
extern char *get_inq_pro_info(di_node_t node);
extern char *get_usb_pro_info(di_node_t node);
extern char *get_devid_info(di_node_t node);
extern int get_pci_path(di_node_t node, unsigned int *bus_id,
unsigned int *dev_id, unsigned int *func_id);
extern int get_con_id(di_node_t node, dev_id *pId);
extern int get_usb_id(di_node_t node, dev_id *pId);
extern int dev_match(di_node_t node, dev_info *info);

#ifdef __cplusplus
}
#endif

#endif /* _LIBDDUDEV_H */

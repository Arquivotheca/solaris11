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

#ifndef	_IDS_H
#define	_IDS_H


#ifdef __cplusplus
extern "C" {
#endif

#define	DDU_PCI_IDS	"/usr/ddu/data/pci.ids"
#define	DDU_USB_IDS	"/usr/ddu/data/usb.ids"
#define	LOCAL_PCI_IDS	"./pci.ids"
#define	LOCAL_USB_IDS	"./usb.ids"
#define	SYS_PCI_IDS	"/usr/share/hwdata/pci.ids"
#define	SYS_USB_IDS	"/usr/share/hwdata/usb.ids"

/*
 * SubsystemInfo structure store device subsystem information
 * in ids file.
 *
 * VendorID: device vendor id.
 * SubsystemID: device subsystem id.
 * SubsystemName: store subsystem name from ids file.
 */
typedef struct {
	unsigned short	VendorID;
	unsigned short	SubsystemID;
	char 		*SubsystemName;
} SubsystemInfo;

/*
 * DeviceInfo structure store device information
 * in ids file.
 *
 * DeviceName: store device name from ids file.
 * Subsystem: store device subsystem information array.
 * nSubsystem: number device subsystem providers.
 * DeviceID: device id.
 */
typedef struct {
	char 			*DeviceName;
	SubsystemInfo 		*Subsystem;
	unsigned int		nSubsystem;
	unsigned short		DeviceID;
} DeviceInfo;

/*
 * VendorInfo structure store vendor information
 * in ids file.
 *
 * VendorName: store vendor name from ids file.
 * Device: devices information array in this vendor.
 * nDevice: number devices in this vendor.
 * DeviceID: vendor id.
 */
typedef struct {
	char 			*VendorName;
	DeviceInfo		*Device;
	unsigned int		nDevice;
	unsigned short		VendorID;
} VendorInfo;

/*
 * IdsInfo structure store ids file information.
 *
 * ids: location about ids information start.
 * ids_end: location about ids information end.
 * nVendor: number vendor in ids file.
 * nDevice: number device in ids file.
 * ven_info: store vendor information in ids file.
 * dev_info: store device information in ids file.
 * sub_info: store subsystem information in ids file.
 */
typedef struct {
	char 		*ids;
	char 		*ids_end;
	unsigned int	nVendor;
	unsigned int	nDevice;
	unsigned int	nSubsystem;
	VendorInfo 	*ven_info;
	DeviceInfo 	*dev_info;
	SubsystemInfo	*sub_info;
} IdsInfo;

int init_pci_ids();
int init_usb_ids();
void fini_pci_ids();
void fini_usb_ids();
int FindPciNames(unsigned long vendor_id,
	unsigned long device_id,
	unsigned long svendor_id,
	unsigned long subsys_id,
	const char **vname,
	const char **dname,
	const char **svname,
	const char **sname);
int FindUsbNames(unsigned long vendor_id,
	unsigned long device_id,
	const char **vname,
	const char **dname);

#ifdef __cplusplus
}
#endif

#endif /* _IDS_H */

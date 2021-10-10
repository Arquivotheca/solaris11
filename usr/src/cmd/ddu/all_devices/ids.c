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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include "libddudev.h"
#include "ids.h"

static IdsInfo		*pci_ids_info;
static IdsInfo		*usb_ids_info;

/*
 * Compare VendorInfo items.
 * This is called by qsort to sort VendorInfo array.
 *
 * p1, p2: Compared VendorInfo items.
 *
 * return:
 * 1: p1->VendorID > p2->VendorID
 * -1: p1->VendorID < p2->VendorID
 * 0: equal
 */
int
veninfo_compare(const void *p1,	const void *p2)
{
	VendorInfo	*pVen_Info1;
	VendorInfo	*pVen_Info2;

	pVen_Info1 = (VendorInfo *)p1;
	pVen_Info2 = (VendorInfo *)p2;

	if (pVen_Info1->VendorID > pVen_Info2->VendorID) {
		return (1);
	}

	if (pVen_Info1->VendorID < pVen_Info2->VendorID) {
		return (-1);
	}

	return (0);
}

/*
 * Compare vendor id.
 * This is called by bsearch.
 *
 * p1: Compared vendor id
 * p2: Compared VendorInfo items.
 *
 * return:
 * 1: *p1 > p2->VendorID
 * -1: *p1 < p2->VendorID
 * 0: equal
 */
int
venid_compare(const void *p1, const void *p2)
{
	unsigned long		vid;
	VendorInfo		*pVen;

	vid = *(unsigned long *)p1;
	pVen = (VendorInfo *)p2;

	if (vid	> pVen->VendorID) {
		return (1);
	}

	if (vid	< pVen->VendorID) {
		return (-1);
	}

	return (0);
}

/*
 * Find pci device vendor name, device name, sub-system vendor,
 * sub-system name
 * Match these ID after load pci.ids.
 *
 * vendor_id: pci device vendor id.
 * device_id: pci device id.
 * svendor_id: sub-system vendor id.
 * sbusys_id:	sub-system id.
 *
 * vname: store device vendor name.
 * dname: store device name.
 * svname: store sub-system vendor name.
 * sname: stroe sub-system name.
 *
 * return value:
 * -2: pci.ids not intialized
 * -1: Invalid Vendor ID
 * 0: Vendor ID not matched, doesn't do other match, just return 0.
 * 1: Vendor ID matched, but device ID not matched.
 * 2: Vendor ID and device ID matched, sub-system ID not matched
 * 3: Vendor ID, device ID, sub-system ID matched,
 * subsys vendor ID not matched
 * 4: All matched.
 */
int
FindPciNames(unsigned long vendor_id,
	unsigned long device_id,
	unsigned long svendor_id,
	unsigned long subsys_id,
	const char **vname,
	const char **dname,
	const char **svname,
	const char **sname)
{
	int i, j;
	DeviceInfo *pDev;
	SubsystemInfo *pSub;
	VendorInfo *pVen;

	/* Initialize returns requested/provided to NULL */
	if (vname) {
		*vname = NULL;
	}

	if (dname) {
		*dname = NULL;
	}

	if (svname) {
		*svname	= NULL;
	}

	if (sname) {
		*sname = NULL;
	}

	if (pci_ids_info == NULL) {
		return (-2);
	}

	/* It's	an error to not	provide	the Vendor */
	if (vendor_id == NOVENDOR) {
		return (-1);
	}

	/* Search Vendor ID */
	pVen = (VendorInfo *)bsearch(&vendor_id,
	    pci_ids_info->ven_info,
	    pci_ids_info->nVendor,
	    sizeof (VendorInfo),
	    (int (*)(const void*, const void*))
	    venid_compare);

	/* No vendor match, return 0 */
	if (!pVen) {
		return (0);
	}

	if (vname) {
		*vname = pVen->VendorName;
	}

	if (device_id == NODEVICE) {
		return (1);
	}

	pDev = pVen->Device;
	if (!pDev) {
		return (1);
	}

	for (i = 0; i < pVen->nDevice; i++) {
		if (device_id == pDev[i].DeviceID) {
			if (dname) {
				*dname = pDev[i].DeviceName;
			}

			if (svendor_id == NOVENDOR || svendor_id == 0) {
				return (2);
			}

			/* search subsystem vendor id */
			pVen = (VendorInfo *)bsearch(&svendor_id,
			    pci_ids_info->ven_info,
			    pci_ids_info->nVendor,
			    sizeof (VendorInfo),
			    (int (*)(const void*, const void*))
			    venid_compare);

			/* No subsystem	vendor match, return 2 */
			if (!pVen) {
				return (2);
			}

			if (svname) {
				*svname	= pVen->VendorName;
			}

			if (subsys_id == NOSUBSYS) {
				return (3);
			}

			pSub = pDev[i].Subsystem;
			if (!pSub) {
				return (3);
			}

			for (j = 0; j <	pDev[i].nSubsystem; j++) {
				if ((svendor_id	== pSub[j].VendorID) &&
				    subsys_id == pSub[j].SubsystemID) {
					if (sname) {
						*sname =
						    pSub[j].SubsystemName;
					}
					return (4);
				}
			}
			return (3);
		}
	}
	return (1);
}

/*
 * Find usb device vendor name, device name,
 * Match these ID after load usb.ids.
 *
 * vendor_id: usb device vendor id.
 * device_id: usb device id.
 *
 * vname: store usb device vendor name.
 * dname: store usb device name.
 *
 * return value:
 * -2: usb.ids not intialized
 * -1: Invalid Vendor ID
 * 0: Vendor ID not matched, doesn't do other match, just return 0.
 * 1: Vendor ID matched, but device ID not matched.
 * 2: All matched.
 */
int
FindUsbNames(unsigned long vendor_id,
	unsigned long device_id,
	const char **vname,
	const char **dname)
{
	int i;
	DeviceInfo *pDev;
	VendorInfo *pVen;

	/* Initialize returns requested/provided to NULL */
	if (vname) {
		*vname = NULL;
	}

	if (dname) {
		*dname = NULL;
	}

	if (usb_ids_info == NULL) {
		return (-2);
	}

	/* It's	an error to not	provide	the Vendor */
	if (vendor_id == NOVENDOR) {
		return (-1);
	}

	/* Search Vendor ID */
	pVen = (VendorInfo *)bsearch(&vendor_id,
	    usb_ids_info->ven_info,
	    usb_ids_info->nVendor,
	    sizeof (VendorInfo),
	    (int (*)(const void*, const void*))
	    venid_compare);

	/* No vendor match, return 0 */
	if (!pVen) {
		return (0);
	}

	if (vname) {
		*vname = pVen->VendorName;
	}

	if (device_id == NODEVICE) {
		return (1);
	}

	pDev = pVen->Device;
	if (!pDev) {
		return (1);
	}

	for (i = 0; i <	pVen->nDevice; i++) {
		if (device_id == pDev[i].DeviceID) {
			if (dname) {
				*dname = pDev[i].DeviceName;
			}
			return (2);
		}
	}
	return (1);
}

/*
 * Get Vendor ID and Vendor Name from string.
 *
 * pVen_Info: VendorInfo structure
 * str: Contain vendor information, format:
 * "vendor_id vendor_name"
 */
void
get_vendor_info(VendorInfo *pVen_Info, char *str)
{
	char	*p;

	pVen_Info->nDevice = 0;
	pVen_Info->Device = NULL;
	pVen_Info->VendorName =	NULL;
	(void) sscanf(str, "%hx ", &pVen_Info->VendorID);

	p = strchr(str,	' ');

	if (p) {
		while (isblank(*(++p)))
			;
	}

	pVen_Info->VendorName = p;
}

/*
 * Get Device ID and Device Name from string.
 *
 * pVen_Info: The device belonged VendorInfo structure
 * pDev_Info: DeviceInfo structure.
 * str: Contain device information, format:
 * "device_id device_name"
 */
void
get_device_info(VendorInfo *pVen_Info, DeviceInfo *pDev_Info, char *str)
{
	char	*p;

	pDev_Info->nSubsystem =	0;
	pDev_Info->Subsystem = NULL;
	pDev_Info->DeviceName =	NULL;
	(void) sscanf(str, "%hx ", &pDev_Info->DeviceID);

	p = strchr(str,	' ');

	if (p) {
		while (isblank(*(++p)))
			;
	}

	pDev_Info->DeviceName =	p;

	pVen_Info->nDevice++;
	if (pVen_Info->Device == NULL) {
		pVen_Info->Device = pDev_Info;
	}
}

/*
 * Get Subsystem information from string.
 *
 * pDev_Info: The Subsystem belonged DeviceInfo structure
 * pSubsys_Info: SubsystemInfo structure.
 * str: Contain device information, format:
 * "SubVendor_id Subsystem_id Subsystem_name"
 */
void
get_subsys_info(DeviceInfo *pDev_Info,
SubsystemInfo *pSubsys_Info, char *str)
{
	char	*p;

	pSubsys_Info->SubsystemName = NULL;

	(void) sscanf(str, "%hx ", &pSubsys_Info->VendorID);

	p = strchr(str,	' ');

	if (p) {
		while (isblank(*(++p)))
			;

		(void) sscanf(p, "%hx ", &pSubsys_Info->SubsystemID);
		p = strchr(p, ' ');

		if (p) {
			while (isblank(*(++p)))
			;
		}
		pSubsys_Info->SubsystemName = p;
	}

	pDev_Info->nSubsystem++;
	if (pDev_Info->Subsystem == NULL) {
		pDev_Info->Subsystem = pSubsys_Info;
	}
}

/*
 * Free ids structure and ids content
 *
 * pInfo: IdsInfo structure, which contain
 * ids content.
 */
void
free_ids_info(IdsInfo *pInfo)
{
	if (pInfo == NULL) {
		return;
	}

	if (pInfo->ven_info) {
		free(pInfo->ven_info);
	}

	if (pInfo->dev_info) {
		free(pInfo->dev_info);
	}

	if (pInfo->sub_info) {
		free(pInfo->sub_info);
	}

	if (pInfo->ids)	{
		free(pInfo->ids);
	}

	free(pInfo);
}

/*
 * Open ids file and load into memory
 * Allocate IdsInfo structure for store ids file content.
 *
 * ids_name: ids file name.
 * ids_sub: if ids_sub is 1, then get subsystem information from
 * ids file
 *
 * Return:
 * Return IdsInfo structure, which include ids content
 * If can not open file or file is bad, then return NULL.
 */
IdsInfo	*
open_ids(char *ids_name, int ids_sub)
{
	FILE		*fd;
	size_t		len;
	long		flen;
	int		ntab;
	char		*p;
	IdsInfo		*pInfo;

	pInfo =	(IdsInfo *)malloc(sizeof (IdsInfo));

	if (pInfo == NULL) {
		return (NULL);
	}

	pInfo->ids = NULL;
	pInfo->ids_end = NULL;
	pInfo->ven_info	= NULL;
	pInfo->dev_info	= NULL;
	pInfo->sub_info	= NULL;
	pInfo->nVendor = 0;
	pInfo->nDevice = 0;
	pInfo->nSubsystem = 0;

	fd = fopen(ids_name, "r");

	if (fd == NULL)	{
		free_ids_info(pInfo);
		return (NULL);
	}

	(void) fseek(fd, 0, SEEK_END);
	flen = ftell(fd);
	(void) fseek(fd, 0, SEEK_SET);

	pInfo->ids = malloc(flen + 1);

	if (pInfo->ids == NULL)	{
		perror("Can not	allocate ids memory space");
		free_ids_info(pInfo);
		(void) fclose(fd);
		return (NULL);
	}

	len = fread(pInfo->ids,	sizeof (char), flen, fd);

	if (flen != len) {
		perror("Can not	read ids file");
		(void) fclose(fd);
		free_ids_info(pInfo);
		return (NULL);
	}

	(void) fclose(fd);

	pInfo->ids_end = &pInfo->ids[flen];

	for (p = pInfo->ids; p < pInfo->ids_end; p++) {
		if ((*p	== '\n') || (*p	== '\r')) {
			*p = '\0';
		}
	}
	*p = '\0';

	p = pInfo->ids;

	/*
	 * Count home many vendor, device, and subsystem items in ids
	 * ids file format:
	 * vendor  vendor_name
	 *	device  device_name	<-- single tab
	 *		subvendor subdevice  subsystem_name	<-- two tabs
	 *
	 * If the line begin with "C ", it is classcode information, stop
	 * scan the file
	 */

	while (p < pInfo->ids_end) {
		if ((*p	== 'C')	&& (p[1] == ' ')) {
			pInfo->ids_end = p - 1;
			break;
		}

		len = strlen(p);
		ntab = 0;

		while (*p == '\t') {
			ntab++;
			p++;
			len--;
		}

		if ((isxdigit(*p) == 0) || (len < 4)) {
			ntab = -1;
		}

		switch (ntab) {
			case 0:
				pInfo->nVendor++;
				break;
			case 1:
				pInfo->nDevice++;
				break;
			case 2:
				pInfo->nSubsystem++;
				break;
		}

		p = p +	len + 1;
	}

	/*
	 * Allocate VendorInfo and DeviceInfo structure for store ids
	 * information
	 * In ids file, vendor number and device number must be great 0,
	 * else this is a bad file, return NULL
	 */
	if ((pInfo->nVendor > 0) && (pInfo->nDevice > 0)) {
		pInfo->ven_info = (VendorInfo *)malloc(
		    sizeof (VendorInfo) * pInfo->nVendor);
		pInfo->dev_info = (DeviceInfo *)malloc(
		    sizeof (DeviceInfo) * pInfo->nDevice);

		if ((pInfo->ven_info == NULL) || (pInfo->dev_info == NULL)) {
			perror("Can not allocate ids memory space");
			free_ids_info(pInfo);
			return (NULL);
		}
	} else {
		free_ids_info(pInfo);
		return (NULL);
	}

	if (ids_sub == 0) {
		pInfo->nSubsystem = 0;
	}

	/*
	 * If has device subsystem information, allocate SubsystemInfo
	 * structure for store device subsystem information.
	 */
	if (pInfo->nSubsystem) {
		pInfo->sub_info	=
		    (SubsystemInfo *)malloc(
		    sizeof (SubsystemInfo) * pInfo->nSubsystem);
	}

	return (pInfo);
}

/*
 * Parse device vendor id, device id,, vendor name, device name,
 * subsystem id subsystem name from ids file
 *
 * pInfo: IdsInfo structure, which contain ids file content.
 * parsed result store in this structure.
 */
void
load_ids(IdsInfo *pInfo)
{
	char		*p;
	int		ntab;
	unsigned int	i_ven, i_dev, i_sub;
	unsigned int	n_ven, n_dev, n_sub;
	size_t		len;

	p = pInfo->ids;

	n_ven =	pInfo->nVendor;
	n_dev =	pInfo->nDevice;
	n_sub =	pInfo->nSubsystem;
	i_ven =	0;
	i_dev =	0;
	i_sub =	0;

	/*
	 * ids file format:
	 * vendor  vendor_name
	 *	device  device_name	<-- single tab
	 *		subvendor subdevice  subsystem_name	<-- two tabs
	 *
	 * If the line begin with "C ", it is classcode information, stop
	 * scan the file
	 */
	while (p < pInfo->ids_end) {
		len = strlen(p);
		ntab = 0;

		while (*p == '\t') {
			ntab++;
			p++;
			len--;
		}

		if ((isxdigit(*p) == 0) || (len < 4)) {
			ntab = -1;
		}

		switch (ntab) {
			case 0:
				if (i_ven == n_ven) {
					return;
				}

				get_vendor_info(&pInfo->ven_info[i_ven], p);

				i_ven++;
				break;
			case 1:
				if (i_ven == 0)	{
					break;
				}

				if (i_dev == n_dev) {
					return;
				}

				get_device_info(
				    &pInfo->ven_info[i_ven - 1],
				    &pInfo->dev_info[i_dev],
				    p);

				i_dev++;
				break;
			case 2:
				if ((pInfo->sub_info !=	NULL) && (i_dev	> 0)) {
					if (n_sub == i_sub) {
						return;
					}
					get_subsys_info(
					    &pInfo->dev_info[i_dev - 1],
					    &pInfo->sub_info[i_sub],
					    p);
					i_sub++;
				}
				break;
		}

		p = p +	len + 1;
	}

	qsort(pInfo->ven_info, n_ven, sizeof (VendorInfo), veninfo_compare);
}

/*
 * Open, load and parse pci ids file.
 * The result store in IdsInfo structure.
 * Select the newest pci.ids file to open.
 * Since pci.ids only allow add item into file,
 * so select the biggest file size to open.
 * DDU_PCI_IDS: /usr/ddu/data/pci.ids
 * SYS_PCI_IDS	"/usr/share/hwdata/pci.ids"
 * LOCAL_PCI_IDS "./pci.ids" (for user update)
 *
 * Return:
 * 1: Can not open, load or parse pci.ids
 * 0: Successful open, load and parse pci.ids
 */
int
init_pci_ids()
{
	int		ret;
	char		*pci_ids_file;
	struct stat	l_ids_stat;
	struct stat	s_ids_stat;

	pci_ids_file = NULL;

	ret = stat(DDU_PCI_IDS, &l_ids_stat);

	if (ret	== 0) {
		pci_ids_file = DDU_PCI_IDS;
	} else {
		ret = stat(LOCAL_PCI_IDS, &l_ids_stat);

		if (ret	== 0) {
			pci_ids_file = LOCAL_PCI_IDS;
		}
	}

	ret = stat(SYS_PCI_IDS,	&s_ids_stat);

	if (ret	== 0) {
		if (pci_ids_file) {
			if (s_ids_stat.st_size > l_ids_stat.st_size) {
				pci_ids_file = SYS_PCI_IDS;
			}
		} else {
			pci_ids_file = SYS_PCI_IDS;
		}
	}

	if (pci_ids_file) {
		pci_ids_info = open_ids(pci_ids_file, 1);
	} else {
		return (1);
	}

	if (pci_ids_info) {
		load_ids(pci_ids_info);
	} else {
		return (1);
	}

	return (0);
}

/*
 * Open, load and parse usb ids file.
 * The result store in IdsInfo structure.
 * Select the newest usb.ids file to open.
 * Since usb.ids only allow add item into file,
 * so select the biggest file size to open.
 * DDU_USB_IDS: /usr/ddu/data/usb.ids
 * SYS_USB_IDS	"/usr/share/hwdata/usb.ids"
 * LOCAL_USB_IDS "./usb.ids" (for user update)
 *
 * Return:
 * 1: Can not open, load or parse usb.ids
 * 0: Successful open, load and parse usb.ids
 */
int
init_usb_ids()
{
	int		ret;
	char		*usb_ids_file;
	struct stat	l_ids_stat;
	struct stat	s_ids_stat;

	usb_ids_file = NULL;

	ret = stat(DDU_USB_IDS, &l_ids_stat);

	if (ret	== 0) {
		usb_ids_file = DDU_USB_IDS;
	} else {
		ret = stat(LOCAL_USB_IDS, &l_ids_stat);

		if (ret	== 0) {
			usb_ids_file = LOCAL_USB_IDS;
		}
	}

	ret = stat(SYS_USB_IDS,	&s_ids_stat);

	if (ret	== 0) {
		if (usb_ids_file) {
			if (s_ids_stat.st_size > l_ids_stat.st_size) {
				usb_ids_file = SYS_USB_IDS;
			}
		} else {
			usb_ids_file = SYS_USB_IDS;
		}
	}

	if (usb_ids_file) {
		usb_ids_info = open_ids(usb_ids_file, 0);
	} else {
		return (1);
	}

	if (usb_ids_info) {
		load_ids(usb_ids_info);
	} else {
		return (1);
	}

	return (0);
}

/*
 * Free pci.ids information which store in pci_ids_info
 */
void
fini_pci_ids()
{
	free_ids_info(pci_ids_info);
	pci_ids_info = NULL;
}

/*
 * Free usb.ids information which store in usb_ids_info
 */
void
fini_usb_ids()
{
	free_ids_info(usb_ids_info);
	usb_ids_info = NULL;
}

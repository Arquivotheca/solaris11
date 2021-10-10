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
 *
 */
/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * SM-HBA interfaces/definitions for PMC-S driver.
 */
#ifndef _PMCS_SMHBA_H
#define	_PMCS_SMHBA_H
#ifdef	__cplusplus
extern "C" {
#endif

/* Leverage definition of data_type_t in nvpair.h */
#include <sys/nvpair.h>

#define	PMCS_NUM_PHYS		"num-phys"
#define	PMCS_NUM_PHYS_HBA	"num-phys-hba"
#define	PMCS_SMHBA_SUPPORTED	"sm-hba-supported"
#define	PMCS_DRV_VERSION	"driver-version"
#define	PMCS_HWARE_VERSION	"hardware-version"
#define	PMCS_FWARE_VERSION	"firmware-version"
#define	PMCS_SUPPORTED_PROTOCOL	"supported-protocol"

#define	PMCS_MANUFACTURER	"Manufacturer"
#define	PMCS_SERIAL_NUMBER	"SerialNumber"
#define	PMCS_MODEL_NAME		"ModelName"

/* Bit definitions for sysevent(uint16_t) member of pmcs_phy_t */

#define	PMCS_SAS_PHY_ONLINE		0x1
#define	PMCS_SAS_PHY_OFFLINE		0x2
#define	PMCS_SAS_PHY_REMOVE		0x4
#define	PMCS_SAS_PORT_BROADCAST_CHANGE	0x8
#define	PMCS_SAS_PORT_BROADCAST_SES	0x10
#define	PMCS_SAS_PORT_BROADCAST_D24_0	0x20
#define	PMCS_SAS_PORT_BROADCAST_D27_4	0x40
#define	PMCS_SAS_PORT_BROADCAST_D01_4	0x80
#define	PMCS_SAS_PORT_BROADCAST_D04_7	0x100
#define	PMCS_SAS_PORT_BROADCAST_D16_7	0x200
#define	PMCS_SAS_PORT_BROADCAST_D29_7	0x400

/*
 * Interfaces to add properties required for SM-HBA
 *
 * _add_xxx_prop() interfaces add only 1 prop that is specified in the args.
 * _set_xxx_props() interfaces add more than 1 prop for a set of phys/devices.
 */
void pmcs_smhba_add_hba_prop(pmcs_hw_t *, data_type_t, char *, void *);
void pmcs_smhba_add_iport_prop(pmcs_iport_t *, data_type_t, char *, void *);
void pmcs_smhba_add_tgt_prop(pmcs_xscsi_t *, data_type_t, char *, void *);

void pmcs_smhba_set_scsi_device_props(pmcs_hw_t *, pmcs_phy_t *,
    struct scsi_device *);
void pmcs_smhba_set_phy_props(pmcs_iport_t *);

/*
 * Misc routines supporting SM-HBA
 */
void pmcs_smhba_log_sysevent(pmcs_hw_t *, char *, char *, pmcs_phy_t *);


#ifdef	__cplusplus
}
#endif
#endif	/* _PMCS_SMHBA_H */

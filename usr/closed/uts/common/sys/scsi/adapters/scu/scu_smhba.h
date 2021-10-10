/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This file may contain confidential information of Intel Corporation
 * and should not be distributed in source form without approval
 * from Oracle Legal.
 */

#ifndef _SCU_SMHBA_H
#define	_SCU_SMHBA_H
#ifdef	__cplusplus
extern "C" {
#endif

/* Leverage definition of data_type_t in nvpair.h */
#include <sys/nvpair.h>

#define	SCU_NUM_PHYS		"num-phys"
#define	SCU_NUM_PHYS_HBA	"num-phys-hba"
#define	SCU_SMHBA_SUPPORTED	"sm-hba-supported"
#define	SCU_DRV_VERSION		"driver-version"
#define	SCU_HWARE_VERSION	"hardware-version"
#define	SCU_FWARE_VERSION	"firmware-version"
#define	SCU_SUPPORTED_PROTOCOL	"supported-protocol"

#define	SCU_MANUFACTURER	"Manufacturer"
#define	SCU_SERIAL_NUMBER	"SerialNumber"
#define	SCU_MODEL_NAME		"ModelName"

/* Bit definitions for sysevent(uint16_t) member of scu_phy_t */

#define	SCU_SAS_PHY_ONLINE		0x1
#define	SCU_SAS_PHY_OFFLINE		0x2
#define	SCU_SAS_PHY_REMOVE		0x4
#define	SCU_SAS_PORT_BROADCAST_CHANGE	0x8
#define	SCU_SAS_PORT_BROADCAST_SES	0x10
#define	SCU_SAS_PORT_BROADCAST_D24_0	0x20
#define	SCU_SAS_PORT_BROADCAST_D27_4	0x40
#define	SCU_SAS_PORT_BROADCAST_D01_4	0x80
#define	SCU_SAS_PORT_BROADCAST_D04_7	0x100
#define	SCU_SAS_PORT_BROADCAST_D16_7	0x200
#define	SCU_SAS_PORT_BROADCAST_D29_7	0x400

/*
 * Receptacle information
 */
#define	SCU_RECEPT_LABEL_0		"SAS0"
#define	SCU_RECEPT_LABEL_1		"SAS1"
#define	SCU_RECEPT_PM_0			"f0"
#define	SCU_RECEPT_PM_1			"f"

/*
 * Interfaces to add properties required for SM-HBA
 *
 * _add_xxx_prop() interfaces add only 1 prop that is specified in the args.
 * _set_xxx_props() interfaces add more than 1 prop for a set of phys/devices.
 */
void scu_smhba_add_hba_prop(scu_ctl_t *, data_type_t, char *, void *);
void scu_smhba_add_iport_prop(scu_iport_t *, data_type_t, char *, void *);
void scu_smhba_add_tgt_prop(scu_tgt_t *, data_type_t, char *, void *);
void scu_smhba_set_scsi_device_props(scu_ctl_t *, scu_tgt_t *,
    struct scsi_device *);
void scu_smhba_set_phy_props(scu_iport_t *);

/*
 * Misc routines supporting SM-HBA
 */
void scu_smhba_log_sysevent(scu_ctl_t *, char *, char *, scu_phy_t *);
void scu_smhba_create_one_phy_stats(scu_iport_t *, scu_phy_t *);
void scu_smhba_create_all_phy_stats(scu_iport_t *);
int scu_smhba_update_phy_stats(kstat_t *, int);
void scu_smhba_update_tgt_pm_props(scu_tgt_t *);

#ifdef	__cplusplus
}
#endif
#endif	/* _SCU_SMHBA_H */

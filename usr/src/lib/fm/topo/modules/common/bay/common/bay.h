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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _BAY_H
#define	_BAY_H

#include <fm/topo_mod.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Path for bay configuration file:
 * /usr/platform/<PLATFORM>/lib/fm/topo/maps/
 *
 * Default/prefered name for bay configuration file:
 * <PRODUCT>,<CHASSIS-SN>,bay_labels
 */
#define	BAY_CONFIG "/usr/platform/%s/lib/fm/topo/maps/%s.%s,bay_labels"

/*
 * Config file path/name when the chassis S/N is not available:
 * /usr/platform/<PLATFORM>/lib/fm/topo/maps/<PRODUCT>,bay_labels
 *
 * Config file path/name when the product name is not available:
 * /usr/platform/<PLATFORM>/lib/fm/topo/maps/<PLATFORM>,bay_labels
 */
#define	BAY_CONF_FILE_1	"/usr/platform/%s/lib/fm/topo/maps/%s,bay_labels"

/* <alias-id> for 'internal' chassis receptacles */
#define	INTERNAL		"SYS"

/* /devices */
#define	DEVICES			"/devices"

/* SAS 'model' property values */
#define	MODEL_SAS		"Serial Attached SCSI Controller"
#define	MODEL_SCSI		"SCSI bus controller"

#define	BAY_SCSI_AP		DDI_NT_SCSI_ATTACHMENT_POINT

/* SAS property names */
#define	PHY_NUM			"phy-num"
#define	NUM_PHYS_HBA		"num-phys-hba"
#define	NUM_PHYS		"num-phys"

#define	BAY_PROP_UNKNOWN	"unknown"

#define	MAX_PM_SHIFT		16
#define	MAX_HBAS		4
#define	DFLT_DIR_AT_PHY		4	/* default w/no 'attached-phys-pm' */
#define	DFLT_NUM_PHYS		(DFLT_DIR_AT_PHY * 2)
#define	MAX_BAYS		DFLT_NUM_PHYS

/* bay structure */
typedef struct bay_s {
	int		phy;		/* HBA PHY */
	int		inst;		/* bay instance */
	char		*label;		/* bay label */
	int		hba_inst;	/* HBA driver instance */
	char		*hba_nm;	/* HBA driver name */
	char		*ch_prod;	/* chassis product */
	char		*ch_label;	/* chassis name */
	char		*ch_serial;	/* external chassis serial number */
	di_node_t	hba_dnode;	/* HBA devinfo node */
} bay_t;

/* callback structure for PCIe label topo walk */
typedef struct tw_pcie_cbs {
	char		*devfs_path;
	char		*label;
	topo_mod_t	*mod;
	topo_hdl_t	*hdl;
} tw_pcie_cbs_t;

/* bay_util.c prototypes */
boolean_t	 cmp_str(const char *, const char *);
char		*ctbl(char *);
int		 gather_sas_hba(di_node_t, void *);
void		 gen_ofile_name(char *, char *, char *);
int		 get_int_prop(di_node_t, di_path_t, char *);
int		 get_phy(di_node_t, di_path_t);
int		 get_str_prop(di_node_t, di_path_t, char *, char *);
boolean_t	 sas_direct(di_node_t);
int		 th_hba_l(topo_hdl_t *, tnode_t *, void *);

#ifdef __cplusplus
}
#endif

#endif /* _BAY_H */

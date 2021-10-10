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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Create bay topology node from SMBIOS Type 136 structure, call the disk
 * enumerator to enumerate a SATA direct attached disk.
 */

#include <sys/types.h>
#include <strings.h>
#include <fm/topo_mod.h>
#include <fm/topo_hc.h>
#include <sys/systeminfo.h>
#include <sys/smbios_impl.h>
#include <x86pi_impl.h>

#define	DEVICES			"/devices"
#define	HBA_DRV_NAME		"ahci"

#define	BDF(b, df) ((uint16_t)((((uint16_t)(b) << 8) & 0xFF00) | \
				((uint16_t)(df) & 0x00FF)));

static const topo_pgroup_info_t io_pgroup = {
	TOPO_PGROUP_IO,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

static const topo_pgroup_info_t binding_pgroup = {
	TOPO_PGROUP_BINDING,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

/*
 * Return PCI Bus/Dev/Func
 */
int
bay_bdf(topo_mod_t *mod, smbios_port_ext_t *epp, uint16_t *bdf)
{
	int	devt;
	id_t	dev_id;
	uint8_t	bus, dev_funct;

	char	*f = "bay_bdf";
	smbios_hdl_t *shp;

	shp = topo_mod_smbios(mod);
	if (shp == NULL) {
		topo_mod_dprintf(mod, "%s: failed to load SMBIOS\n", f);
		return (-1);
	}
	/*
	 * Depending on device type, BDF comes from either slot (type-9) or
	 * on-board (type-41) SMBIOS structure.
	 */
	devt = epp->smbporte_dtype;
	dev_id = epp->smbporte_devhdl;

	if (devt == SMB_TYPE_SLOT) {
		smbios_slot_t slot;
		(void) smbios_info_slot(shp, dev_id, &slot);
		bus = slot.smbl_bus;
		dev_funct = slot.smbl_df;
	} else if (devt == SMB_TYPE_OBDEVEXT) {
		smbios_obdev_ext_t ob;
		(void) smbios_info_obdevs_ext(shp, dev_id, &ob);
		bus = ob.smboe_bus;
		dev_funct = ob.smboe_df;
	} else {
		topo_mod_dprintf(mod, "%s: unknown device type: %d\n",
		    f, devt);
		return (-1);
	}
	topo_mod_dprintf(mod, "%s: %s: bus(0x%02x) dev/func(0x%02x)\n", f,
	    devt == SMB_TYPE_SLOT ? "slot" : "ob dev", bus, dev_funct);

	*bdf = BDF(bus, dev_funct);

	return (0);
}

/*
 * Decorate topo node with pgroups.
 */
int
bay_pgroups(topo_mod_t *mod, tnode_t *tnp, di_node_t *dnp, di_node_t *sibp,
    char *minor_name)
{
	int		rv, err;
	char		*ap_path, *oc_path;
	char		*devfs_path = di_devfs_path(*dnp);

	char		*f = "bay_pgoups";

	/*
	 * Create "io" pgroup and attachment point path.
	 */
	rv = topo_pgroup_create(tnp, &io_pgroup, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod,
		    "%s: failed to create \"io\" pgroup: %s\n",
		    f, topo_strerror(err));
		(void) topo_mod_seterrno(mod, err);
		return (err);
	}

	ap_path = topo_mod_alloc(mod, MAXPATHLEN);
	if (ap_path == NULL) {
		topo_mod_dprintf(mod, "%s: ap_path alloc failed\n");
		return (topo_mod_seterrno(mod, EMOD_NOMEM));
	}
	(void) snprintf(ap_path, MAXPATHLEN, "%s%s:%s", DEVICES,
	    devfs_path, minor_name);
	topo_mod_dprintf(mod, "%s: ap_path(%s)\n", f, ap_path);
	di_devfs_path_free(devfs_path);

	/* add ap-path */
	rv = topo_prop_set_string(tnp, TOPO_PGROUP_IO, TOPO_IO_AP_PATH,
	    TOPO_PROP_IMMUTABLE, ap_path, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to set ap-path: %s\n",
		    f, topo_strerror(err));
		topo_mod_free(mod, ap_path, MAXPATHLEN);
		(void) topo_mod_seterrno(mod, err);
		return (err);
	}
	topo_mod_free(mod, ap_path, MAXPATHLEN);

	/*
	 * Create "binding" pgroup and occupant path.
	 */
	rv = topo_pgroup_create(tnp, &binding_pgroup, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod,
		    "%s: failed to create \"io\" pgroup: %s\n",
		    f, topo_strerror(err));
		(void) topo_mod_seterrno(mod, err);
		return (err);
	}

	oc_path = di_devfs_path(*sibp);
	if (oc_path == NULL) {
		topo_mod_dprintf(mod, "%s: no occupant path\n", f);
		return (-1);
	}
	topo_mod_dprintf(mod, "%s: oc_path(%s)\n", f, oc_path);

	/* add ocupant-path */
	rv = topo_prop_set_string(tnp, TOPO_PGROUP_BINDING,
	    TOPO_BINDING_OCCUPANT, TOPO_PROP_IMMUTABLE, oc_path,
	    &err);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to set ap-path: %s\n",
		    f, topo_strerror(err));
		di_devfs_path_free(oc_path);
		(void) topo_mod_seterrno(mod, err);
		return (err);
	}
	di_devfs_path_free(oc_path);

	return (0);
}

int
bay_update_tnode(topo_mod_t *mod, tnode_t *tnodep, uint16_t bdf, int phy)
{
	int		rv;
	int		minor_cnt = 0;
	char		*minor_name = NULL;
	di_node_t	devtree, dnode, sib;
	di_minor_t	minor = DI_MINOR_NIL;

	char		*f = "bay_update_tnode";

	/*
	 * Find HBA device node from BDF.
	 */
	devtree = topo_mod_devinfo(mod);
	if (devtree == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "%s: failed to get dev tree\n", f);
		return (-1);
	}
	for (dnode = di_drv_first_node(HBA_DRV_NAME, devtree);
	    dnode != DI_NODE_NIL;
	    dnode = di_drv_next_node(dnode)) {
		if (bdf == x86pi_bdf(mod, dnode)) {
			/*
			 * Match child node from PHY.
			 */
			sib = di_child_node(dnode);
			while (sib != DI_NODE_NIL) {
				if (phy == x86pi_phy(mod, sib))
					break;
				sib = di_sibling_node(sib);
			}
			break;
		}
	}
	if (dnode == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "%s: no HBA di_node\n", f);
		return (topo_mod_seterrno(mod, EMOD_PARTIAL_ENUM));
	}

	/*
	 * HBA attachment point minor node name.
	 */
	while ((minor = di_minor_next(dnode, minor)) != DI_MINOR_NIL) {
		if (strncmp(DDI_NT_SATA_ATTACHMENT_POINT,
		    di_minor_nodetype(minor),
		    strlen(DDI_NT_SATA_ATTACHMENT_POINT)) == 0) {
			if (phy == minor_cnt++) {
				minor_name = di_minor_name(minor);
				topo_mod_dprintf(mod,
				    "%s: phy(%d) minor name(%s)\n",
				    f, phy, minor_name);
				break;
			}
		}
	}

	rv = bay_pgroups(mod, tnodep, &dnode, &sib, minor_name);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to add pgroups\n", f);
		return (-1);
	}


	return (0);
}

/*
 * x86pi_gen_bay:
 *  enumerate all bays associated with this system:
 *    x86pi_gen_sas_bay(): enumerate SAS direct attached bays
 *    x86pi_gen_sata_bay(): enumerate SATA direct attached bays
 *    x86pi_gen_expander_bay(): enumerate bays behind an internal ses
 */
void
x86pi_gen_bay(topo_mod_t *mod, tnode_t *chassis_node, int ch_smbid, int nch)
{
	int		i, rv;
	int		max_inst_exp_bay = 0;
	int		disk_inst = 0;
	smbs_cnt_t	*smbc;
	smbios_port_ext_t export;
	smbios_hdl_t 	*shp;
	char		*f = "x86pi_gen_bay";

	/*
	 * Calls a routine that initiates enumeration of expander
	 * attached internal drives.
	 * The max_inst_exp_bay value indicates that the max instance
	 * number used in the ses enumerator if any expander attached
	 * internal drives are processed.
	 */
	rv = x86pi_gen_expander_bay(mod, chassis_node,
	    &max_inst_exp_bay);
	if (rv != 0) {
		topo_mod_dprintf(mod,
		    "%s: Failed to enumerate expander attached "
		    "internal bays.  Continue on.", f);
	} else {
		/*
		 * some expadner attached bay is enumerated.
		 */
		if (max_inst_exp_bay != 0) {
			disk_inst = max_inst_exp_bay + 1;
		}
	}

	stypes[SMB_TYPE_CHASSIS].ids[nch].node = chassis_node;

	/* count SMBIOS extended port connector structures */
	smbc = &stypes[SUN_OEM_EXT_PORT];
	smbc->type = SUN_OEM_EXT_PORT;
	x86pi_smb_strcnt(mod, smbc);

	/*
	 * enumerate direct attached SATA disks if we found a
	 * SUN_OEM_EXT_PORT record.
	 */
	if (smbc->count > 0) {
		/* get smbios handle */
		shp = topo_mod_smbios(mod);
		if (shp == NULL) {
			topo_mod_dprintf(mod, "%s: failed to load SMBIOS\n", f);
			return;
		}
		rv = topo_node_range_create(mod, chassis_node, BAY, 0,
		    smbc->count + 1);
		if (rv == 0) {
			for (i = 0; i < smbc->count; i++) {
				rv = smbios_info_extport(shp, smbc->ids[i].id,
				    &export);
				if (rv == 0) {
					/* belong to this chassis? */
					if (export.smbporte_chassis ==
					    ch_smbid) {
						/*
						 * x86pi_gen_sata_bay:
						 *   create "bay" node
						 *   call "disk" enum passing
						 *   in "bay" node
						 */
						rv = x86pi_gen_sata_bay(mod,
						    chassis_node, &export,
						    disk_inst);
						if (rv != 0) {
							topo_mod_dprintf(mod,
							    "%s: Failed to "
							    "create disk %d\n",
							    f, i);
						}
						disk_inst++;
					}
				} else {
					topo_mod_dprintf(mod,
					    "%s: smbios_info_export failed: id "
					    "= %d\n", f, (int)smbc->ids[i].id);
				}
			}
		} else {
			topo_mod_dprintf(mod,
			    "%s: Failed to create %s range: %s\n",
			    f, BAY, topo_mod_errmsg(mod));
		}
	}

	/*
	 * Call the bay enumerator to enumerate any SAS direct
	 * attached disks.
	 */
	rv = x86pi_gen_sas_bay(mod, chassis_node, &disk_inst);
	if (rv != 0) {
		topo_mod_dprintf(mod,
		    "%s: Failed to enumerate sas direct attached "
		    "bays.", f);
	}
}

/*
 * x86pi_gen_expander_bay:
 *   call "ses" enum with a flag indicating that the request is related
 *	to internal enclosure.
 */
int
x86pi_gen_expander_bay(topo_mod_t *mod, tnode_t *t_parent, int *max_inst)
{
	int		rv;
	int		min = 0, max = 0;
	char		*f = "x86pi_gen_expander_bay";

	if (topo_mod_load(mod, SES, TOPO_VERSION) == NULL) {
		topo_mod_dprintf(mod, "%s: Failed to load %s module: %s\n",
		    f, SES, topo_strerror(topo_mod_errno(mod)));
		return (topo_mod_errno(mod));
	}

	/*
	 * Call the ses enumerator to process expander attached bays.
	 * By passing SES_ENCLOSURE node name here the ses enumerator will
	 * process an internal enclousure and find expander attached internal
	 * drives through SES ARRAY device elements.
	 *
	 * Currently other places that ses enumerator is called are
	 * i86pc-hc-topology.xml, i86pc-legacy-hc-topology.xml and
	 * some platform specific mapfiles.  Those calls are intended
	 * to enumerate an external enclosure or disk nodes through
	 * legacy enumeration.  They end up in topo_xml.c which sets
	 * the arg for the flag to NULL.
	 *
	 * If there is any expander attached internal bays the highest
	 * instance nubmer is returned to the max inst var.
	 */
	rv = topo_mod_enumerate(mod, t_parent, SES, SES_ENCLOSURE,
	    min, max, max_inst);

	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: %s internal enclosure "
		    "enumeration failed: %s\n", f,
		    SES, topo_strerror(topo_mod_errno(mod)));
		return (topo_mod_errno(mod));
	}

	topo_mod_dprintf(mod,
	    "%s: done. Max instance for expander attached bays: %d.\n", f,
	    *max_inst);

	return (0);
}


/*
 * x86pi_gen_sas_bay:
 *  Call "bay" enum to enumerate SAS direct attached disks passing in the
 *  'chassis' topo parent node and the current 'bay' instance.
 */
int
x86pi_gen_sas_bay(topo_mod_t *mod, tnode_t *t_parent, int *inst)
{
	int		rv;
	int		min = 0, max = 0;
	char		*f = "x86pi_gen_sas_bay";

	if (topo_mod_load(mod, BAY, TOPO_VERSION) == NULL) {
		topo_mod_dprintf(mod, "%s: Failed to load %s module: %s\n",
		    f, BAY, topo_strerror(topo_mod_errno(mod)));
		return (topo_mod_errno(mod));
	}

	rv = topo_mod_enumerate(mod, t_parent, BAY, BAY, min, max, inst);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: %s enumeration failed: %s\n", f,
		    BAY, topo_strerror(topo_mod_errno(mod)));
		return (topo_mod_errno(mod));
	}

	return (0);
}

/*
 * x86pi_gen_bay:
 *   create "bay" node
 *   call "disk" enum passing in "bay" node
 */
int
x86pi_gen_sata_bay(topo_mod_t *mod, tnode_t *t_parent, smbios_port_ext_t *eport,
    int instance)
{
	int		rv;
	int		min = 0, max = 0;
	id_t		port_id;
	uint16_t	bdf;
	smbios_port_t	smb_port;
	x86pi_hcfmri_t	hcfmri = {0};
	tnode_t		*tn_bay;

	char		*f = "x86pi_gen_sata_bay";
	smbios_hdl_t *shp;

	shp = topo_mod_smbios(mod);
	if (shp == NULL) {
		topo_mod_dprintf(mod, "%s: failed to load SMBIOS\n", f);
		return (topo_mod_seterrno(mod, EMOD_PARTIAL_ENUM));
	}

	/*
	 * Label comes from the port (type-8) SMBIOS structure.
	 */
	port_id = eport->smbporte_port;

	rv = smbios_info_port(shp, port_id, &smb_port);
	if (rv != 0) {
		topo_mod_dprintf(mod,
		    "%s: failed to get port %d SMBIOS struct\n",
		    f, port_id);
		return (topo_mod_seterrno(mod, EMOD_PARTIAL_ENUM));
	}

	/*
	 * Fill in hcfmri info.
	 */
	hcfmri.hc_name = BAY;
	hcfmri.instance = instance;
	hcfmri.location = x86pi_cleanup_smbios_str(mod, smb_port.smbo_eref,
	    LABEL);

	/*
	 * Create "bay" node.
	 */
	rv = x86pi_enum_generic(mod, &hcfmri, t_parent, t_parent, &tn_bay, 0);
	if (rv != 0) {
		topo_mod_dprintf(mod,
		    "%s: failed to create %s topo node: %d\n",
		    f, BAY, instance);
		return (topo_mod_seterrno(mod, EMOD_PARTIAL_ENUM));
	}

	/* free up location string */
	if (hcfmri.location != NULL) {
		topo_mod_strfree(mod, (char *)hcfmri.location);
	}

	/*
	 * Determine the bay BDF.
	 */
	rv = bay_bdf(mod, eport, &bdf);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to get BDF\n", f);
		return (topo_mod_seterrno(mod, EMOD_PARTIAL_ENUM));
	}
	topo_mod_dprintf(mod, "%s: BDF(0x%04x)\n", f, bdf);

	/*
	 * Decorate bay topo node.
	 */
	rv = bay_update_tnode(mod, tn_bay, bdf, eport->smbporte_phy);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to decorate bay node\n", f);
		return (topo_mod_seterrno(mod, EMOD_PARTIAL_ENUM));
	}

	/*
	 * Call disk enum passing in decorated bay topo node.
	 */
	if (topo_mod_load(mod, DISK, TOPO_VERSION) == NULL) {
		topo_mod_dprintf(mod, "%s: Failed to load %s module: %s\n",
		    f, DISK, topo_strerror(topo_mod_errno(mod)));
		return (topo_mod_errno(mod));
	}

	rv = topo_node_range_create(mod, tn_bay, DISK, min, max);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to create range: %s\n", f,
		    topo_strerror(topo_mod_errno(mod)));
		return (topo_mod_errno(mod));
	}

	rv = topo_mod_enumerate(mod, tn_bay, DISK, DISK, min, max, NULL);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: %s enumeration failed: %s\n", f,
		    DISK, topo_strerror(topo_mod_errno(mod)));
		return (topo_mod_errno(mod));
	}

	topo_mod_dprintf(mod, "%s: done.\n", f);

	return (0);
}

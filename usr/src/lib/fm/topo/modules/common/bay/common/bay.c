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

#include <strings.h>
#include <devid.h>
#include <inttypes.h>
#include <sys/systeminfo.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fm/topo_mod.h>
#include <fm/topo_list.h>
#include <sys/fm/protocol.h>
#include <bay_impl.h>

static int bay_enum(topo_mod_t *, tnode_t *, const char *,
	topo_instance_t, topo_instance_t, void *, void *);

static const topo_modops_t bay_ops =
	{ bay_enum, NULL };

static const topo_modinfo_t bay_info =
	{ BAY, FM_FMRI_SCHEME_HC, BAY_VERSION, &bay_ops };

static const topo_method_t bay_fac_methods[] = {
	{ TOPO_METH_FAC_ENUM, TOPO_METH_FAC_ENUM_DESC, 0,
	TOPO_STABILITY_INTERNAL, bay_enum_facility },
	{ NULL }
};

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

/* place to keep bays */
bay_t bays[MAX_HBAS][MAX_BAYS];

/* place to keep hba nodes */
di_node_t hba_nodes[MAX_HBAS];
int hba_node_cnt;

/* external chassis count */
int ch_ext;

/* internal bay instance count */
int bay_icnt;

/*
 * Create io and binding pgroups and add ap_path/oc_path to them.
 */
void
bay_add_pgroups(topo_mod_t *mod, tnode_t *tn, bay_t *bp, char *ap_path,
    char *oc_path)
{
	int	rv;
	int	err;
	char	*devid = NULL;

	char	*f = "bay_add_pgroups";

	/* create io pgroup */
	rv = topo_pgroup_create(tn, &io_pgroup, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to create io pgroup: %s\n",
		    f, topo_strerror(err));
		(void) topo_mod_seterrno(mod, err);
		return;
	}

	/* add ap-path to io pgroup */
	if (ap_path != NULL) {
		rv = topo_prop_set_string(tn, TOPO_PGROUP_IO, TOPO_IO_AP_PATH,
		    TOPO_PROP_IMMUTABLE, ap_path, &err);
		if (rv != 0) {
			topo_mod_dprintf(mod,
			    "%s: failed to set ap-path: %s\n",
			    f, topo_strerror(err));
			(void) topo_mod_seterrno(mod, err);
			return;
		}
	}

	/* create binding pgroup */
	rv = topo_pgroup_create(tn, &binding_pgroup, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod,
		    "%s: failed to create binding pgroup: %s\n",
		    f, topo_strerror(err));
		(void) topo_mod_seterrno(mod, err);
		return;
	}

	if (oc_path != NULL) {
		/* add oc-path to binding pgroup */
		rv = topo_prop_set_string(tn, TOPO_PGROUP_BINDING,
		    TOPO_BINDING_OCCUPANT, TOPO_PROP_IMMUTABLE, oc_path,
		    &err);
		if (rv != 0) {
			topo_mod_dprintf(mod,
			    "%s: failed to set oc_path: %s\n",
			    f, topo_strerror(err));
			(void) topo_mod_seterrno(mod, err);
			return;
		}

		/* since there's an occupant, add devid to the io pgroup */
		devid = get_devid(mod, oc_path, bp);
		if (devid != NULL) {
			rv = topo_prop_set_string(tn, TOPO_PGROUP_IO,
			    TOPO_IO_DEVID, TOPO_PROP_IMMUTABLE, devid, &err);
			if (rv != 0) {
				topo_mod_dprintf(mod,
				    "%s: failed to set devid: %s\n",
				    f, topo_strerror(err));
				(void) topo_mod_seterrno(mod, err);
			}
			topo_mod_strfree(mod, devid);
		}
	}
}

/*
 * Generate the attachment-point path bay property.
 */
void
bay_gen_ap_path(topo_mod_t *mod, bay_t *bp, char **ap_path)
{
	int		rv;
	char		*mpxd = NULL;
	char		*path = NULL;
	di_minor_t	minor = DI_MINOR_NIL;
	di_node_t	cnode = DI_NODE_NIL;
	di_path_t	pnode = DI_PATH_NIL;

	rv = find_child(mod, bp->hba_dnode, &cnode, &pnode, bp->phy);
	if (rv != 0) {
		*ap_path = NULL;
		topo_mod_dprintf(mod, "bay_gen_ap_path: child node NIL.\n");
		return;
	}

	if (cnode != DI_NODE_NIL) {
		minor = find_minor_ap(mod, cnode);
	}
	if (minor == DI_MINOR_NIL) {
		/* look in grandchildren (!mpxio) */
		minor = find_minor_ap(mod, bp->hba_dnode);
		if (minor == DI_MINOR_NIL) {
			*ap_path = NULL;
			topo_mod_dprintf(mod,
			    "bay_gen_ap_path: minor node NIL.\n");
			return;
		}
	}

	/* check MPxIO */
	mpxd = topo_mod_alloc(mod, MAXNAMELEN);
	(void) get_str_prop(bp->hba_dnode, DI_PATH_NIL, "mpxio-disable", mpxd);

	/* create attachment-point path */
	if (mpxd != NULL && cmp_str(mpxd, "yes")) {
		path = di_devfs_minor_path(minor);
	} else {
		path = gen_ap(mod, minor);
	}

	/* return path */
	*ap_path = topo_mod_strdup(mod, path);

	if (mpxd != NULL && cmp_str(mpxd, "yes")) {
		di_devfs_path_free(path);
	} else {
		topo_mod_strfree(mod, path);
	}

	topo_mod_free(mod, mpxd, MAXNAMELEN);
}

/*
 * Generate the occupant path bay property.
 */
void
bay_gen_oc_path(topo_mod_t *mod,  bay_t *bp, char **oc_path)
{
	int		rv;
	char		*path = NULL;
	char		*mpxd = NULL;
	di_node_t	cnode = DI_NODE_NIL;
	di_path_t	pnode = DI_PATH_NIL;
	boolean_t	path_free = B_FALSE;

	/* find the phy matching child node */
	rv = find_child(mod, bp->hba_dnode, &cnode, &pnode, bp->phy);
	if (rv !=  0) {
		topo_mod_dprintf(mod, "bay_gen_oc_path: child node NIL.\n");
		return;
	}

	/* check MPxIO */
	mpxd = topo_mod_alloc(mod, MAXNAMELEN);
	(void) get_str_prop(bp->hba_dnode, DI_PATH_NIL, "mpxio-disable", mpxd);

	/* create occupant path */
	if (cnode != DI_NODE_NIL && mpxd != NULL && cmp_str(mpxd, "yes")) {
		path = di_devfs_path(di_child_node(cnode));
		path_free = B_TRUE;
	} else {
		path = gen_oc(mod, cnode, pnode);
	}

	/* sometimes 'mpxio-disable' just can't be trusted */
	if (path == NULL) {
		if (cnode != DI_NODE_NIL) {
			path = di_devfs_path(cnode);
			path_free = B_TRUE;
		} else if (pnode != DI_PATH_NIL) {
			path = di_path_devfs_path(pnode);
			path_free = B_TRUE;
		}
	}

	/* return path */
	if (path != NULL) {
		*oc_path = topo_mod_strdup(mod, path);
	}

	if (path_free) {
		di_devfs_path_free(path);
	} else {
		topo_mod_strfree(mod, path);
	}

	topo_mod_free(mod, mpxd, MAXNAMELEN);
}

/*
 * Add properties to the bay node.
 */
void
bay_decorate(topo_mod_t *mod, tnode_t *tn, bay_t *bp)
{
	char	*ap_path = NULL;
	char	*oc_path = NULL;

	/* generate attachment-point path */
	bay_gen_ap_path(mod, bp, &ap_path);

	/* generate occupant path */
	bay_gen_oc_path(mod, bp, &oc_path);

	topo_mod_dprintf(mod,
	    "bay_decorate: %s:%d ap_path %s oc_path %s\n",
	    bp->hba_nm, bp->phy,
	    ap_path != NULL ? ap_path : "NULL",
	    oc_path != NULL ? oc_path : "NULL");

	/* add ap_path/oc_path to the bay topo node */
	bay_add_pgroups(mod, tn, bp, ap_path, oc_path);

	/* cleanup */
	if (ap_path != NULL) {
		topo_mod_strfree(mod, ap_path);
	}
	if (oc_path != NULL) {
		topo_mod_strfree(mod, oc_path);
	}
}

/*
 * Drive the generation of the bay topo node.
 */
void
bay_gen_bay(topo_mod_t *mod, tnode_t *pnode, bay_t *bp)
{
	int		rv;
	tnode_t		*bay_tn;
	topo_mod_t	*dmod;

	char		*f = "bay_gen_bay";

	if (bp == NULL) {
		topo_mod_dprintf(mod, "%s: NULL bay\n", f);
		return;
	}

	/* create bay topo node */
	rv = bay_create_tnode(mod, pnode, &bay_tn, bp);
	if (rv != 0) {
		topo_mod_dprintf(mod,
		    "%s: failed to enumerate %s HBA bay #%d\n",
		    f, bp->hba_nm, bp->inst);
		return;
	}

	/* set tnode specifics for LED control */
	topo_node_setspecific(bay_tn, (void *)bp);

	/* see if HBA supports SGPIO before registering fac methods */
	rv = bay_led_ctl(mod, bay_tn, BAY_PROP_IDENT, 0, BAY_INDICATOR_GET);
	if (rv != -1) {
		/* register bay facility node methods */
		rv = topo_method_register(mod, bay_tn, bay_fac_methods);
		if (rv != 0) {
			topo_mod_dprintf(mod,
			    "%s: failed to register bay_fac_methods: %s.\n",
			    f, topo_mod_errmsg(mod));
		}
	}

	/* decorate bay topo ndoe */
	bay_decorate(mod, bay_tn, bp);

	/* load the disk module */
	dmod = topo_mod_load(mod, DISK, TOPO_VERSION);
	if (dmod == NULL) {
		topo_mod_dprintf(mod,
		    "%s: failed to load disk enum for %s:%d: (%s)\n",
		    f, bp->hba_nm, bp->phy,
		    topo_strerror(topo_mod_errno(mod)));
		return;
	}

	/* call the disk.so enum w/bay topo node as parent */
	rv = topo_node_range_create(mod, bay_tn, DISK, 0, 0);
	if (rv != 0 && topo_mod_errno(mod) != EMOD_NODE_DUP) {
		topo_mod_dprintf(mod,
		    "%s: failed to create &s range: %s\n",
		    f, BAY, topo_mod_errmsg(mod));
		return;
	}

	rv = topo_mod_enumerate(mod, bay_tn, DISK, DISK, 0, 0, NULL);
	if (rv != 0) {
		topo_mod_dprintf(mod,
		    "%s: failed to enum disk for %s:%d: (%s)\n",
		    f, bp->hba_nm, bp->phy,
		    topo_strerror(topo_mod_errno(mod)));
		return;
	}

	/* create 'target-port-l0ids' property(s) */
	rv = create_l0ids(mod, bay_tn, bp);
	if (rv != 0) {
		topo_mod_dprintf(mod,
		    "%s: failed to create %s properties for %s:%d: (%s)\n",
		    f, TOPO_STORAGE_TARGET_PORT_L0IDS, bp->hba_nm, bp->phy,
		    topo_strerror(topo_mod_errno(mod)));
	}
}

/*
 * Create an external chassis (e.g. JBOD) topo node. This is different than
 * a bay topo node becasue it's authority info is different and the resource
 * and FRU fmris should be the same.
 */
static int
bay_create_xch(topo_mod_t *mod, topo_instance_t instance, tnode_t *pnode,
    tnode_t **tnode, bay_t *bayp)
{
	int		rv;
	int		err;
	char		*serial = NULL;
	char		*label = NULL;
	char		*prod = NULL;
	nvlist_t	*fmri = NULL;
	nvlist_t	*frufmri = NULL;
	nvlist_t	*auth = NULL;

	char		*f = "bay_create_xch";

	serial = topo_mod_strdup(mod, (const char *)bayp->ch_serial);
	label = topo_mod_strdup(mod, (const char *)bayp->ch_label);
	prod = topo_mod_strdup(mod, (const char *)bayp->ch_prod);

	/* create auth */
	rv = topo_mod_nvalloc(mod, &auth, NV_UNIQUE_NAME);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to alloc auth\n", f);
		goto bad;
	}

	rv = nvlist_add_string(auth, FM_FMRI_AUTH_CHASSIS, serial);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to add auth props\n", f);
		goto bad;
	}

	rv = nvlist_add_string(auth, FM_FMRI_AUTH_PRODUCT, prod);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to add auth props\n", f);
		goto bad;
	}

	rv = nvlist_add_string(auth, FM_FMRI_AUTH_SERVER, label);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to add auth props\n", f);
		goto bad;
	}

	/* create fmri */
	fmri = topo_mod_hcfmri(mod, NULL, FM_HC_SCHEME_VERSION,
	    EXTERNALCHASSIS, instance, NULL, auth, NULL, NULL, serial);
	if (fmri == NULL) {
		topo_mod_dprintf(mod, "%s: failed to create fmri: %s\n",
		    f, topo_strerror(topo_mod_errno(mod)));
		goto bad;
	}

	/* bind to parent */
	*tnode = topo_node_bind(mod, pnode, EXTERNALCHASSIS, instance, fmri);
	if (*tnode == NULL) {
		topo_mod_dprintf(mod, "%s: failed to bind node: %s\n",
		    f, topo_strerror(topo_mod_errno(mod)));
		goto bad;
	}
	nvlist_free(auth);
	nvlist_free(fmri);

	/*
	 * Set the authority; use ext chassis s/n for chassis-id.
	 */
	if (topo_prop_set_string(*tnode, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_PRODUCT, TOPO_PROP_IMMUTABLE, prod, &err) != 0 ||
	    topo_prop_set_string(*tnode, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_PRODUCT_SN, TOPO_PROP_IMMUTABLE, serial, &err) != 0 ||
	    topo_prop_set_string(*tnode, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_CHASSIS, TOPO_PROP_IMMUTABLE, serial, &err) != 0 ||
	    topo_prop_set_string(*tnode, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_SERVER, TOPO_PROP_IMMUTABLE, label, &err) != 0) {
		topo_mod_dprintf(mod, "%s: failed to add auth props: %s\n",
		    f, topo_strerror(err));
		return (topo_mod_seterrno(mod, err));
	}

	/* copy resource fmri and set as FRU fmri */
	rv = topo_node_resource(*tnode, &frufmri, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to get resource: %s\n",
		    f, topo_strerror(err));
		return (topo_mod_seterrno(mod, err));
	}

	rv = topo_node_fru_set(*tnode, frufmri, 0, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to set FRU fmri: %s\n",
		    f, topo_strerror(err));
		nvlist_free(frufmri);
		return (topo_mod_seterrno(mod, err));
	}
	nvlist_free(frufmri);

	/* set the label */
	rv = topo_node_label_set(*tnode, label, &err);
	if (rv != 0) {
		(void) topo_mod_seterrno(mod, err);
		topo_mod_dprintf(mod, "%s: failed to set label: %s\n",
		    f, topo_strerror(err));
		return (topo_mod_seterrno(mod, err));
	}

	/* clean-up */
	topo_mod_strfree(mod, serial);
	topo_mod_strfree(mod, label);
	topo_mod_strfree(mod, prod);

	return (0);
bad:
	if (auth != NULL) {
		nvlist_free(auth);
	}
	if (fmri != NULL) {
		nvlist_free(fmri);
	}

	return (-1);
}

/*
 * Go find direct attached bays (children of the hba).
 */
static void
bay_direct(topo_mod_t *mod, tnode_t *t_parent, di_node_t hba_dnode,
    int hba_idx, char *conf_file)
{
	int		nbays = 0;
	int		i;
	int		 rv;
	tnode_t		*root_tn = t_parent;
	tnode_t		*ch_tn;
	tnode_t		*exch_tn;
	boolean_t	i_ch = B_TRUE;

	char		*f = "bay_direct";

	/* fill in bays from the training mode config file */
	rv = read_config(mod, hba_dnode, conf_file, bays[hba_idx], &nbays);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to read config file\n", f);
		return;
	}
	if (nbays == 0) {
		topo_mod_dprintf(mod, "%s: no bays found for %s\n", f,
		    di_driver_name(hba_dnode));
		return;
	}

	/*
	 * Sanity check at least the first child to make sure the bays are
	 * direct attached.
	 */
	if (!sas_direct(bays[hba_idx][0].hba_dnode)) {
		topo_mod_dprintf(mod, "%s: %s bays not direct attached\n",
		    f, di_driver_name(bays[hba_idx][0].hba_dnode));
		return;
	}

	/*
	 * Each hba connects to either the server internal bays (system
	 * chassis) or to an external JBOD type external chassis.
	 */
	i_ch = internal_ch(&bays[hba_idx][0]);
	if (!i_ch) {
		/* create an external-chassis */
		rv = topo_node_range_create(mod, t_parent, EXTERNALCHASSIS,
		    0, ch_ext + 1);
		if (rv == 0 || topo_mod_errno(mod) == EMOD_NODE_DUP) {
			rv = bay_create_xch(mod, ch_ext, t_parent, &exch_tn,
			    &bays[hba_idx][0]);
			if (rv == 0 && exch_tn != NULL) {
				root_tn = exch_tn;
				++ch_ext;
			} else {
				topo_mod_dprintf(mod,
				    "%s: failed to create external "
				    "chassis topo node %d.\n", f, ch_ext);
			}
		} else {
			topo_mod_dprintf(mod,
			    "%s: failed to create %s range: %s\n",
			    f, EXTERNALCHASSIS, topo_mod_errmsg(mod));
		}
	} else {
		/* internal bays go under 'chassis' nodes */
		if (!cmp_str(topo_node_name(t_parent), CHASSIS)) {
			/* lookup 'chassis=0' */
			ch_tn = topo_node_lookup(t_parent, CHASSIS, 0);
			if (ch_tn != NULL) {
				root_tn = ch_tn;
			} else {
				topo_mod_dprintf(mod, "%s: failed to find "
				    "chassis=0 tnode. Using %s parent tnode\n",
				    f, topo_node_name(root_tn));
			}
		}
	}

	topo_mod_dprintf(mod, "%s: HBA %s: %d bays root node(%s)\n",
	    f, bays[hba_idx][0].hba_nm, nbays, topo_node_name(root_tn));

	/* create the range */
	rv = topo_node_range_create(mod, root_tn, BAY, 0,
	    MAX_HBAS * MAX_BAYS);
	if (rv != 0 && topo_mod_errno(mod) != EMOD_NODE_DUP) {
		topo_mod_dprintf(mod,
		    "%s: failed to create &s range: %s\n",
		    BAY, topo_mod_errmsg(mod));
		return;
	}

	/* go through the bays and enumerate */
	for (i = 0; i < nbays; i++) {
		/* set the bay instance */
		bays[hba_idx][i].inst = i_ch ? bay_icnt : i;

		bay_gen_bay(mod, root_tn, &bays[hba_idx][i]);

		/* internal chassis count */
		if (i_ch) {
			++bay_icnt;
		}
	}
}


/*ARGSUSED*/
static int
bay_enum(topo_mod_t *mod, tnode_t *t_parent, const char *name,
    topo_instance_t min, topo_instance_t max, void *arg, void *priv)
{
	int		i;
	int		n;
	struct stat	st;
	int		bay_inst = 0;
	char		*conf_fnm = NULL;
	char		*prod = NULL;
	char		*ch_sn = NULL;
	di_node_t	devtree;

	char		*f = "bay_enum";

	/* see who's calling us */
	topo_mod_dprintf(mod, "%s: parent: (%s) topo node\n", f,
	    topo_node_name(t_parent));

	/*
	 * If we're being called with a parent 'bay' node, then the node has
	 * been created and we only need to create the facility indicator and
	 * sensor nodes for it. Currently only expecting sun4vpi.so.
	 */
	if (cmp_str(topo_node_name(t_parent), BAY)) {
		/* register fac methods for existing "bay" topo node */
		if (topo_method_register(mod, t_parent, bay_fac_methods) != 0) {
			topo_mod_dprintf(mod,
			    "%s: topo_method_register() failed: %s",
			    f, topo_mod_errmsg(mod));
			return (-1);
		}
		return (0);
	}

	/*
	 * Create topology for direct attached bays and call the disk enum to
	 * create topop nodes for disk drive occupants.
	 *
	 * Look into the devinfo snapshot to see if there are any SAS HBAs
	 * with direct attaeched bays/disks.
	 */
	devtree = topo_mod_devinfo(mod);
	if (devtree != DI_NODE_NIL) {
		/* walk the devtree looking for SAS HBA nodes */
		hba_node_cnt = 0;
		(void) di_walk_node(devtree, DI_WALK_CLDFIRST,
		    (void *)hba_nodes, gather_sas_hba);
	} else {
		topo_mod_dprintf(mod,
		    "%s: failed to get devinfo snapshot.\n", f);
		return (-1);
	}
	if (hba_node_cnt == MAX_HBAS) {
		topo_mod_dprintf(mod, "%s: HBA count: MAX\n", f);
	}
	if (hba_node_cnt == 0) {
		topo_mod_dprintf(mod, "%s: No HBAs found.\n", f);
		return (0);
	}

	/* sort HBA devinfo nodes releative to their PICe slot ID */
	if (sort_hba_nodes(mod, t_parent, hba_nodes) != 0) {
		topo_mod_dprintf(mod, "%s: failed to sort HBA di_nodes\n", f);
	}

	/* make some space */
	conf_fnm = topo_mod_alloc(mod, MAXNAMELEN);
	prod = topo_mod_alloc(mod, MAXNAMELEN);
	ch_sn = topo_mod_alloc(mod, MAXNAMELEN);
	if (conf_fnm == NULL || prod == NULL || ch_sn == NULL) {
		topo_mod_dprintf(mod, "%s: No Memory.\n", f);
		(void) topo_mod_seterrno(mod, ENOMEM);
		if (ch_sn != NULL)
			topo_mod_free(mod, ch_sn, MAXNAMELEN);
		if (prod != NULL)
			topo_mod_free(mod, prod, MAXNAMELEN);
		if (conf_fnm != NULL)
			topo_mod_free(mod, conf_fnm, MAXNAMELEN);
		return (-1);
	}

	/* get the product name and chassis S/N */
	if (get_prod(mod, t_parent, prod, ch_sn) != 0) {
		topo_mod_dprintf(mod, "%s: failed to get identity info\n", f);
		topo_mod_free(mod, ch_sn, MAXNAMELEN);
		topo_mod_free(mod, prod, MAXNAMELEN);
		topo_mod_free(mod, conf_fnm, MAXNAMELEN);
		return (-1);
	}

	/* create the output config file name */
	gen_ofile_name(prod, ch_sn, conf_fnm);
	topo_mod_dprintf(mod, "%s: config file name %s\n", f, conf_fnm);

	/* make sure the file is presnt */
	while (stat(conf_fnm, &st)  < 0) {
		if (strlen(prod) > 0 && strlen(ch_sn) > 0) {
			/* try only product name */
			*ch_sn = '\0';		/* zero length */
			gen_ofile_name(prod, ch_sn, conf_fnm);
			topo_mod_dprintf(mod,
			    "%s: secondary config file name %s\n",
			    f, conf_fnm);
		} else if (strlen(prod) > 0) {
			/* try only platfrom name */
			*prod = '\0';
			gen_ofile_name(prod, ch_sn, conf_fnm);
			topo_mod_dprintf(mod,
			    "%s: plat only config file name %s\n",
			    f, conf_fnm);
		} else {
			/* no file */
			topo_mod_dprintf(mod, "%s: NO config file found\n", f);
			topo_mod_free(mod, ch_sn, MAXNAMELEN);
			topo_mod_free(mod, prod, MAXNAMELEN);
			topo_mod_free(mod, conf_fnm, MAXNAMELEN);
			return (0);
		}
	}

	topo_mod_free(mod, ch_sn, MAXNAMELEN);
	topo_mod_free(mod, prod, MAXNAMELEN);

	/* set bay instance if not passed in NULL */
	if (priv != NULL) {
		bay_inst = *(int *)priv;
	}

	/* enumerate */
	bay_icnt	= bay_inst;	/* init internal bay count */
	ch_ext		= 0;		/* init external chassis count */
	for (i = 0; i < hba_node_cnt; i ++) {
		/* find and create direcct attached bay nodes */
		n = get_num_phys(hba_nodes[i]);
		topo_mod_dprintf(mod, "%s: %s contains max: %d bays\n", f,
		    di_driver_name(hba_nodes[i]), n);
		if (n == 0) {
			topo_mod_dprintf(mod, "%s: no bays for %s\n", f,
			    di_driver_name(hba_nodes[i]));
			continue;
		}
		bay_direct(mod, t_parent, hba_nodes[i], i, conf_fnm);
	}

	topo_mod_dprintf(mod, "%s: done.\n", f);
	topo_mod_free(mod, conf_fnm, MAXNAMELEN);
	return (0);
}

/*ARGSUSED*/
int
_topo_init(topo_mod_t *mod, topo_version_t version)
{
	int	result;

	if (getenv("TOPOBAYDEBUG") != NULL) {
		/* Turn on module debugging output */
		topo_mod_setdebug(mod);
	}
	topo_mod_dprintf(mod, "_topo_init: "
	    "initializing %s enumerator\n", BAY);

	if (version != TOPO_VERSION) {
		(void) topo_mod_seterrno(mod, EMOD_VER_NEW);
		topo_mod_dprintf(mod, "incompatible topo version %d\n",
		    version);
		return (-1);
	}

	result = topo_mod_register(mod, &bay_info, TOPO_VERSION);
	if (result < 0) {
		topo_mod_dprintf(mod, "_topo_init: "
		    "%s registration failed: %s\n",
		    BAY, topo_mod_errmsg(mod));
		/* module errno already set */
		return (-1);
	}

	topo_mod_dprintf(mod, "_topo_init: "
	    "%s enumerator initialized\n", BAY);

	return (0);
}

void
_topo_fini(topo_mod_t *mod)
{
	topo_mod_dprintf(mod, "_topo_fini: "
	    "%s enumerator uninitialized\n", BAY);

	topo_mod_unregister(mod);
}

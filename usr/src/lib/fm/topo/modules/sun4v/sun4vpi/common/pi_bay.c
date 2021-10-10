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
 * Create a topology node for a PRI node of type 'bay'. Call the disk
 * enumerator to enumerate any disks that may be attached.
 */

#include <sys/types.h>
#include <strings.h>
#include <sys/fm/protocol.h>
#include <fm/topo_mod.h>
#include <fm/topo_hc.h>
#include <libdevinfo.h>
#include <sys/pci.h>
#include <sys/mdesc.h>
#include <devid.h>
#include <bay.h>
#include "pi_impl.h"

#define	_ENUM_NAME	"enum_bay"
#define	HBA_DRV_NAME	"mpt_sas"
#define	DEVICES		"/devices"
#define	DEVIDLEN	32

#define	PI_BAY_AP	DDI_NT_SCSI_ATTACHMENT_POINT
#define	PI_MAX_LUN	255

static boolean_t MPxIO_ENABLED = B_FALSE;

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
 * Return the MPxIO occupant path bay property.
 *
 * The string must be freed with topo_mod_strfree().
 */
static char *
pi_bay_ocpath(topo_mod_t *mod, di_node_t dnode)
{

	int		lun;
	boolean_t	got_w;
	char		buf[MAXPATHLEN];
	char		*di_path = NULL;
	char		*tgt_port = NULL;

	/* 'target-port' property */
	tgt_port = pi_get_target_port(mod, dnode);
	if (tgt_port == NULL) {
		topo_mod_dprintf(mod, "pi_bay_ocpath: failed to get "
		    "'target-port' property\n");
		return (NULL);
	}

	/* 'lun' property */
	lun = pi_get_lun(mod, dnode);
	if (lun < 0 || lun > PI_MAX_LUN) {
		topo_mod_dprintf(mod, "pi_bay_ocpath: failed to get 'lun' "
		    "property\n");
		topo_mod_strfree(mod, tgt_port);
		return (NULL);
	}

	/* 'target-port' leading 'w' is not consistent */
	got_w = tgt_port[0] == 'w' ? B_TRUE : B_FALSE;

	/*
	 * Build occupatnt path:
	 * 'devfs_path' + "/disk@w" + 'target-port' + "," + 'lun'
	 */
	di_path = di_devfs_path(dnode);
	if (di_path == NULL) {
		topo_mod_dprintf(mod,
		    "pi_bay_ocpath: failed to get devfs path\n");
		topo_mod_strfree(mod, tgt_port);
		return (NULL);
	}

	(void) snprintf(buf, MAXPATHLEN, "%s%s%s,%x", di_path,
	    (got_w ? "/disk@" : "/disk@w"), tgt_port, lun);

	di_devfs_path_free(di_path);
	topo_mod_strfree(mod, tgt_port);
	return (topo_mod_strdup(mod, buf));
}


/*
 * Create bay "io" pgroup, create and add "ap_path", "devid" properties.
 * Create bay "binding" pgroup, create and add "oc_path" property.
 */
static int
pi_bay_pgroups(topo_mod_t *mod, tnode_t *t_node, di_node_t cnode,
    di_minor_t cminor)
{
	int		rv;
	int		err;
	char		*ap_path;
	char		*minor_path = NULL;
	char		*oc_path = NULL;
	char		*strdevid = NULL;

	/* Create "io" pgroup. */
	rv = topo_pgroup_create(t_node, &io_pgroup, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod, "pi_bay_pgroups: failed to create "
		    "\"io\" pgroup: %s\n", topo_strerror(err));
		rv = topo_mod_seterrno(mod, err);
		goto out;
	}

	/* Allocate what we need first, then create the properties */
	ap_path = topo_mod_alloc(mod, MAXPATHLEN);
	if (ap_path == NULL) {
		topo_mod_dprintf(mod, "pi_bay_pgroups: EMOD_NOMEM for "
		    "ap_path\n");
		rv = topo_mod_seterrno(mod, EMOD_NOMEM);
		goto out;
	}

	/* attachment point path: "/devices" + minor node path */
	minor_path = di_devfs_minor_path(cminor);
	if (minor_path == NULL) {
		topo_mod_dprintf(mod,
		    "pi_bay_pgroups: failed to get minor path\n");
		rv = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	(void) snprintf(ap_path, MAXPATHLEN, "%s%s", DEVICES,
	    minor_path);
	di_devfs_path_free(minor_path);
	topo_mod_dprintf(mod, "pi_bay_pgroups: ap_path (%s)\n", ap_path);

	/* add ap_path prop to io pgroup */
	rv = topo_prop_set_string(t_node, TOPO_PGROUP_IO, TOPO_IO_AP_PATH,
	    TOPO_PROP_IMMUTABLE, ap_path, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod, "pi_bay_pgroups: failed to set "
		    "ap-path: %s\n", topo_strerror(err));
		topo_mod_free(mod, ap_path, MAXPATHLEN);
		rv = topo_mod_seterrno(mod, err);
		goto out;
	}
	topo_mod_free(mod, ap_path, MAXPATHLEN);

	/* Create "binding" pgroup. */
	rv = topo_pgroup_create(t_node, &binding_pgroup, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod, "pi_bay_pgroups: failed to "
		    "create \"binding\" pgroup: %s\n", topo_strerror(err));
		rv = topo_mod_seterrno(mod, err);
		goto out;
	}

	/*
	 * Create the oc_path property:
	 */
	if (MPxIO_ENABLED) {
		oc_path = pi_bay_ocpath(mod, cnode);
	} else {
		oc_path = di_devfs_path(cnode);
	}
	if (oc_path == NULL) {
		topo_mod_dprintf(mod, "pi_bay_pgroups: no occupant path\n");
		rv = -1;
		goto out;
	}
	topo_mod_dprintf(mod, "pi_bay_proups: oc_path (%s)\n", oc_path);

	/* add oc_path to binding pgroup */
	rv = topo_prop_set_string(t_node, TOPO_PGROUP_BINDING,
	    TOPO_BINDING_OCCUPANT, TOPO_PROP_IMMUTABLE, oc_path, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod, "pi_bay_pgroups: failed to set "
		    "oc_path: %s\n", topo_strerror(err));
		rv = topo_mod_seterrno(mod, err);
		goto out;
	}

	/*
	 * Now that we know there's a bay occupant, grab the devid.
	 * We don't allocate space for strdevid because libdevinfo
	 * handles it for us.
	 */
	rv = pi_get_devid(mod, oc_path, &strdevid);
	if (rv != 0) {
		topo_mod_dprintf(mod,
		    "pi_bay_pgroups: failed to retrieve devid for: %s\n",
		    oc_path);
		goto out;
	}
	topo_mod_dprintf(mod, "pi_bay_pgroups: devid %s\n", strdevid);

	rv = topo_prop_set_string(t_node, TOPO_PGROUP_IO, TOPO_IO_DEVID,
	    TOPO_PROP_IMMUTABLE, strdevid, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod, "pi_bay_pgroups: failed to set "
		    "devid : (%d) : %s\n", err, topo_strerror(err));
		rv = topo_mod_seterrno(mod, err);
	}

out:
	if (oc_path != NULL) {
		if (MPxIO_ENABLED) {
			topo_mod_strfree(mod, oc_path);
		} else {
			di_devfs_path_free(oc_path);
		}
	}

	return (rv);

}


/*
 * Find the child devinfo node of the HBA that matches the PHY, capture the
 * minor attachment point node.
 */
static void
pi_bay_find_nodes(topo_mod_t *mod, di_node_t *nodep, di_node_t *sibp,
    di_minor_t *minorp, int phy)
{
	di_node_t	sib = DI_NODE_NIL;
	di_node_t	gsib = DI_NODE_NIL;
	di_minor_t	minor = DI_MINOR_NIL;

	/*
	 * When MPxIO is enabled the child node of the HBA (iport) contains
	 * the pathinfo property we're looking for; when MPxIO is disabled
	 * the grand-child of the HBA (disk) contains the devinfo property
	 * we're looking for.
	 */
	sib = di_child_node(*nodep);
	while (sib != DI_NODE_NIL) {
		/* match the PHY */
		if (phy == pi_get_phynum(mod, sib)) {
			while ((minor = di_minor_next(sib, minor)) !=
			    DI_MINOR_NIL) {
				/* scsi attachment point */
				if (strncmp(di_minor_nodetype(minor),
				    PI_BAY_AP,
				    strlen(di_minor_nodetype(minor))) == 0) {
					goto out;
				}
			}
		} else {
			/* look in grandchildren */
			gsib = di_child_node(sib);
			while (gsib != DI_NODE_NIL) {
				/* match the PHY */
				if (phy == pi_get_phynum(mod, gsib)) {
					while ((minor = di_minor_next(sib,
					    minor)) != DI_MINOR_NIL) {
						/* scsi attachment point */
						if (strncmp(
						    di_minor_nodetype(minor),
						    PI_BAY_AP,
						    strlen(di_minor_nodetype(
						    minor))) == 0) {
							sib = gsib;
							goto out;
						}
					}
				}
				gsib = di_sibling_node(gsib);
			}
		}
		sib = di_sibling_node(sib);
	}
out:
	if (sib == DI_NODE_NIL) {
		*sibp = DI_NODE_NIL;
	} else {
		bcopy(&sib, sibp, sizeof (di_node_t));
	}

	if (minor == DI_MINOR_NIL) {
		*minorp = DI_MINOR_NIL;
	} else {
		bcopy(&minor, minorp, sizeof (di_minor_t));
	}
}


/*
 * Find the HBA devinfo node associated with the path.
 */
static di_node_t
pi_bay_hba_node(topo_mod_t *mod, char *pri_path)
{
	char		*hba_path;
	di_node_t	devtree = DI_NODE_NIL;
	di_node_t	dnode = DI_NODE_NIL;

	/*
	 * The hba path and bay PHY come from the PRI; find the
	 * driver node that coresponds to the PHY and it's minor
	 * node name and create the occupant path/attachmeent_point
	 * path
	 */
	devtree = topo_mod_devinfo(mod);
	if (devtree == DI_NODE_NIL) {
		topo_mod_dprintf(mod,
		    "pi_bay_hba_node: failed to get devtree\n");
		goto bad;
	}
	for (dnode = di_drv_first_node(HBA_DRV_NAME, devtree);
	    dnode != DI_NODE_NIL;
	    dnode = di_drv_next_node(dnode)) {
		/* find the dnode path that matches the pri path */
		hba_path = pi_get_dipath(mod, dnode);
		if (strcmp(pri_path, hba_path) == 0) {
			/* found our dnode */
			topo_mod_strfree(mod, hba_path);
			break;
		}
		topo_mod_strfree(mod, hba_path);
	}
	if (dnode == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "pi_bay_hba_node: failed to find "
		    "devinfo path.\n");
		goto bad;
	}
	return (dnode);
bad:
	return (DI_NODE_NIL);
}


/*
 * Decorate "bay" node with required properties for disk enumerator.
 */
static int
pi_bay_update_node(topo_mod_t *mod, tnode_t *t_node, di_node_t dnode,
    uint8_t phy, char **tgtport)
{
	int		rv;
	char		*mpxio_prop;
	di_node_t	sib;
	di_minor_t	minor = DI_MINOR_NIL;

	/*
	 * The "mpxio-disable" variable determines if MPxIO (multipathing)
	 * is disabled (or enabled).
	 */
	if (di_prop_lookup_strings(DDI_DEV_T_ANY, dnode, "mpxio-disable",
	    &mpxio_prop) < 0) {
		/* no way to determine if MPxIO is enabled */
		topo_mod_dprintf(mod,
		    "pi_bay_update_node: no \"mpxio-disable\" property\n");
		goto bad;
	}

	/* set MPxIO_ENABLED inverse to "mpxio-disable" */
	topo_mod_dprintf(mod, "\"mpxio-disable\" = (%s)\n", mpxio_prop);
	MPxIO_ENABLED = strncmp("no", mpxio_prop, strlen(mpxio_prop)) == 0 ?
	    B_TRUE : B_FALSE;
	topo_mod_dprintf(mod, "MPxIO_ENABLED: %s\n", MPxIO_ENABLED ? "TRUE" :
	    "FALSE");

	/*
	 * Find the child node matching the PRI phy_number and determine the
	 * minor attachment point.
	 */
	pi_bay_find_nodes(mod, &dnode, &sib, &minor, phy);
	if (sib == DI_NODE_NIL || minor == DI_MINOR_NIL) {
		topo_mod_dprintf(mod, "pi_bay_update_node: no disk on "
		    "PHY %d.\n", phy);
		goto bad;
	}

	/* add pgroups */
	rv = pi_bay_pgroups(mod, t_node, sib, minor);
	if (rv != 0) {
		topo_mod_dprintf(mod, "pi_bay_update_node: failed to add "
		    "pgroups.\n", _ENUM_NAME);
		goto bad;
	}
	/* By this stage of the process we know this will succeed */
	*tgtport = pi_get_target_port(mod, sib);
	if (*tgtport == NULL) {
		topo_mod_dprintf(mod, "pi_bay_update_node: failed to "
		    "retrieve target-port property\n");
	}
	return (0);
bad:
	return (-1);
}

/* ARGSUSED */
int
pi_enum_bay(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node,
    topo_instance_t inst, tnode_t *t_parent, const char *hc_name,
    tnode_t **t_node)
{
	int		i, rv, err;
	int		min = 0, max = 0;
	int		num_arcs;
	int		nphy;
	size_t		arcsize;
	uint8_t		*phy = NULL;
	char		*hba_pri_path;
	char		**tgtport;
	mde_cookie_t	*arcp;
	tnode_t		*child;
	di_node_t	hba_dnode;
	bay_t		*bp;

	/* count how many PHYs the bay node has */
	nphy = pi_get_priphy(mod, mdp, mde_node, phy);
	if (nphy <= 0) {
		topo_mod_dprintf(mod, "%s: node_0x%llx has no PHY\n",
		    _ENUM_NAME, (uint64_t)mde_node);
		rv = -1;
		goto out;
	}

	phy = topo_mod_alloc(mod, (nphy * sizeof (uint8_t)));
	if (phy == NULL) {
		topo_mod_dprintf(mod, "%s: node_0x%llx ENOMEM\n",
		    _ENUM_NAME, (uint64_t)mde_node);
		rv = -1;
		goto out;
	}

	/* get the PHY(s) for this bay node */
	rv = pi_get_priphy(mod, mdp, mde_node, phy);
	if (rv != nphy) {
		topo_mod_dprintf(mod, "%s: node_0x%llx failed to get PHY\n",
		    _ENUM_NAME, (uint64_t)mde_node);
		rv = -1;
		goto out;
	}
	topo_mod_dprintf(mod, "%s: node_0x%llx PHY: %d\n", _ENUM_NAME,
	    mde_node, *phy);

	/* determine how many parent (HBA) nodes */
	num_arcs = md_get_prop_arcs(mdp, mde_node, MD_STR_BACK, NULL, 0);
	if (num_arcs == 0) {
		topo_mod_dprintf(mod, "%s: node_0x%llx has no \"back\" arcs\n",
		    _ENUM_NAME, (uint64_t)mde_node);
		rv = -1;
		goto out;
	}
	topo_mod_dprintf(mod, "%s: node_0x%llx has %d \"back\" arcs\n",
	    _ENUM_NAME, mde_node, num_arcs);

	/* get the "back" nodes */
	arcsize = sizeof (mde_cookie_t) * num_arcs;
	arcp = topo_mod_zalloc(mod, arcsize);
	if (arcp == NULL) {
		topo_mod_dprintf(mod, "%s: no memory\n", _ENUM_NAME);
		rv = topo_mod_seterrno(mod, EMOD_NOMEM);
		goto out;
	}
	num_arcs = md_get_prop_arcs(mdp, mde_node, MD_STR_BACK, arcp, arcsize);

	/* make sure there are as many HBA nodes as PHYs */
	if (num_arcs != nphy) {
		topo_mod_dprintf(mod, "%s: %d PHYs for %d back arcs.\n",
		    _ENUM_NAME, nphy, num_arcs);
		rv = -1;
		goto out;
	}

	tgtport = topo_mod_alloc(mod, DEVIDLEN);
	if (tgtport == NULL) {
		topo_mod_dprintf(mod, "%s: no memory\n", _ENUM_NAME);
		rv = topo_mod_seterrno(mod, EMOD_NOMEM);
		goto out;
	}

	/* create topo bay node for each HBA attached to this bay */
	for (i = 0; i < num_arcs; i++) {
		/* skip if topo-hc-skip = 1 */
		if (pi_skip_node(mod, mdp, arcp[i])) {
			topo_mod_dprintf(mod, "%s: skipping node_0x%llx\n",
			    (uint64_t)arcp[i]);
			continue;
		}

		/* must be an ses expander if no path property - skip */
		rv = md_get_prop_str(mdp, arcp[i], MD_STR_PATH, &hba_pri_path);
		if (rv != 0 || hba_pri_path == NULL ||
		    strlen(hba_pri_path) == 0) {
			topo_mod_dprintf(mod, "%s: node_0x%llx: no path "
			    "property\n", _ENUM_NAME, (uint64_t)arcp[i]);
			continue;
		}

		/*
		 * Create a generic "bay" node; decorate below.
		 *
		 * If we have more than one HBA the bay inst here will be
		 * the same for both. This is okay since the paths will
		 * be different for each HBA.
		 */
		rv = pi_enum_generic_impl(mod, mdp, mde_node, inst, t_parent,
		    t_parent, hc_name, _ENUM_NAME, t_node, 0);
		if (rv != 0 || *t_node == NULL) {
			topo_mod_dprintf(mod,
			    "%s: node_0x%llx failed to create topo node: %s\n",
			    _ENUM_NAME, (uint64_t)mde_node,
			    topo_strerror(topo_mod_errno(mod)));
			rv = -1;
			goto out;
		}

		/* grab the hba dnode */
		hba_dnode = pi_bay_hba_node(mod, hba_pri_path);
		if (hba_dnode == DI_NODE_NIL) {
			topo_mod_dprintf(mod,
			    "%s: failed to find HBA devinfo node\n");
			continue;
		}

		/*
		 * Call the bay enum to create bay topo facility nodes.
		 */
		if (topo_mod_load(mod, BAY, TOPO_VERSION) != NULL) {
			/* create tnode specific bay_t */
			bp = topo_mod_alloc(mod, sizeof (bay_t));
			if (bp == NULL) {
				topo_mod_dprintf(mod,
				    "%s: no memory for bay_t\n", _ENUM_NAME);
				rv = topo_mod_seterrno(mod, EMOD_NOMEM);
				goto out;
			}
			bp->phy = phy[i];
			bp->hba_dnode = hba_dnode;
			topo_node_setspecific(*t_node, (void *)bp);
			rv = topo_mod_enumerate(mod, *t_node, BAY, BAY,
			    0, 0, NULL);
			if (rv != 0) {
				topo_mod_dprintf(mod,
				    "FAILED TO ENUM BAY: %s\n",
				    topo_strerror(topo_mod_errno(mod)));
				/* drive on */
			}
		} else {
			topo_mod_dprintf(mod,
			    "FAILED TO LOAD BAY MODULE: %s.\n",
			    topo_strerror(topo_mod_errno(mod)));
			/* drive on */
		}

		/* Decorate the bay tnode */
		rv = pi_bay_update_node(mod, *t_node, hba_dnode, phy[i],
		    &tgtport[0]);
		if (rv != 0) {
			topo_mod_dprintf(mod, "%s: failed to update "
			    "node_0x%llx for target-port property\n",
			    _ENUM_NAME, (uint64_t)mde_node);
			continue;
		}
		topo_mod_dprintf(mod, "pi_enum_bay: tgtport is %s\n",
		    ((tgtport[0] != NULL) ? tgtport[0] : "(null)"));


		/*
		 * Call the disk enum passing in decorated bay tnode.
		 */
		if (topo_mod_load(mod, DISK, TOPO_VERSION) == NULL) {
			topo_mod_dprintf(mod,
			    "%s: Failed to load %s module: %s\n",
			    _ENUM_NAME, DISK,
			    topo_strerror(topo_mod_errno(mod)));
			rv = topo_mod_errno(mod);
			topo_mod_strfree(mod, *tgtport);
			goto out;
		}

		rv = topo_node_range_create(mod, *t_node, DISK, min, max);
		if (rv != 0) {
			topo_mod_dprintf(mod,
			    "%s: failed to create range: %s\n", _ENUM_NAME,
			    topo_strerror(topo_mod_errno(mod)));
			rv = topo_mod_errno(mod);
			topo_mod_strfree(mod, *tgtport);
			goto out;
		}

		rv = topo_mod_enumerate(mod, *t_node, DISK, DISK, min, max,
		    NULL);
		if (rv != 0) {
			topo_mod_dprintf(mod,
			    "%s: %s enumeration failed: %s\n", _ENUM_NAME,
			    DISK, topo_strerror(topo_mod_errno(mod)));
			rv = topo_mod_errno(mod);
			topo_mod_strfree(mod, *tgtport);
			goto out;
		}

		/*
		 * Finally, add the target-port-l0ids property to the
		 * storage pgroup now that the DISK enumerator has created
		 * it for us.
		 */
		child = topo_node_lookup(*t_node, DISK, 0);
		rv = topo_prop_set_string_array(child, TOPO_PGROUP_STORAGE,
		    TOPO_STORAGE_TARGET_PORT_L0IDS, TOPO_PROP_IMMUTABLE,
		    (const char **)tgtport, (uint_t)1, &err);
		if (rv != 0) {
			topo_mod_dprintf(mod, "pi_enum_bay: failed to add "
			    "target-port-l0ids property to DISK child of %s "
			    "phy %d : %s\n",
			    hba_pri_path, phy[i], topo_strerror(err));
			rv = err;
			topo_mod_strfree(mod, *tgtport);
			goto out;
		}
		topo_mod_strfree(mod, *tgtport);
	}

	rv = 0;
out:
	/* cleanup */
	if (arcp != NULL) {
		topo_mod_free(mod, arcp, arcsize);
	}
	if (phy != NULL) {
		topo_mod_free(mod, phy, (nphy * sizeof (uint8_t)));
	}
	if (tgtport != NULL) {
		topo_mod_free(mod, tgtport, DEVIDLEN);
	}

	return (rv);
}

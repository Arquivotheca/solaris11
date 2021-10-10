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

#include <devid.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <stropts.h>
#include <strings.h>
#include <stddef.h>
#include <pthread.h>
#include <inttypes.h>
#include <fm/topo_mod.h>
#include <fm/libtopo.h>
#include <fm/topo_list.h>
#include <sys/scsi/scsi_address.h>
#include <sys/fm/protocol.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/pci_tools.h>
#include <bay_impl.h>

extern int hba_node_cnt;

/*
 * Routines used by the bay,so topo enumerator.
 */

/*
 * Create "target-port-l0ids" property.
 */
int
create_l0ids(topo_mod_t *mod, tnode_t *bay_tn, bay_t *bp)
{
	int		err;
	int		rv;
	tnode_t		*disk_tn;
	char		*tgt_port = NULL;
	di_node_t	cnode = DI_NODE_NIL;
	di_path_t	pnode = DI_PATH_NIL;

	char		*f = "create_l0ids";

	rv = find_child(mod, bp->hba_dnode, &cnode, &pnode, bp->phy);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to find child node\n", f);
		goto out;
	}

	tgt_port = topo_mod_alloc(mod, MAXNAMELEN);
	if (tgt_port == NULL) {
		topo_mod_dprintf(mod, "%s: failed to get memory for tgt-port\n",
		    f);
		goto out;
	}
	rv = get_str_prop(cnode, pnode, SCSI_ADDR_PROP_TARGET_PORT, tgt_port);
	if (rv != 0) {
		/* look at the child */
		rv = get_str_prop(di_child_node(cnode), DI_PATH_NIL,
		    SCSI_ADDR_PROP_TARGET_PORT, tgt_port);
		if (rv != 0) {
			topo_mod_dprintf(mod,
			    "%s: failed to get \'target-port\'\n", f);
			goto out;
		}
	}

	disk_tn = topo_node_lookup(bay_tn, DISK, 0);
	if (disk_tn == NULL) {
		topo_mod_dprintf(mod, "%s: failed to lookup disk tnode\n", f);
		goto out;
	}

	rv = topo_prop_set_string_array(disk_tn, TOPO_PGROUP_STORAGE,
	    TOPO_STORAGE_TARGET_PORT_L0IDS, TOPO_PROP_IMMUTABLE,
	    (const char **)&tgt_port, (uint_t)1, &err);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to add %s property: %s\n",
		    f, TOPO_STORAGE_TARGET_PORT_L0IDS, topo_strerror(err));
		goto out;
	}

	topo_mod_free(mod, tgt_port, MAXNAMELEN);
	return (0);
out:
	if (tgt_port != NULL) {
		topo_mod_free(mod, tgt_port, MAXNAMELEN);
	}
	return (-1);
}

/*
 * Return the minor attachment point node.
 */
/* ARGSUSED */
di_minor_t
find_minor_ap(topo_mod_t *mod, di_node_t cnode)
{
	di_minor_t	minor = DI_MINOR_NIL;

	while ((minor = di_minor_next(cnode, minor)) != DI_MINOR_NIL) {
		if (cmp_str(BAY_SCSI_AP, di_minor_nodetype(minor))) {
			return (minor);
		}
	}
	return (DI_MINOR_NIL);
}

/*
 * return the child node which matches the phy.
 */
/* ARGSUSED */
int
find_child(topo_mod_t *mod, di_node_t dnode, di_node_t *cn, di_path_t *pn,
    int phy)
{
	di_node_t	cnode = di_child_node(dnode);
	di_path_t	pnode = DI_PATH_NIL;

	while (cnode != DI_NODE_NIL) {
		if (get_phy(cnode, DI_PATH_NIL) == phy) {
			bcopy(&cnode, cn, sizeof (di_node_t));
			return (0);
		}
		cnode = di_sibling_node(cnode);
	}

	while ((pnode = di_path_phci_next_path(dnode, pnode)) != DI_PATH_NIL) {
		if (get_phy(DI_NODE_NIL, pnode) == phy) {
			bcopy(&pnode, pn, sizeof (di_path_t));
			return (0);
		}
	}

	/* no PHY found */
	return (-1);
}

/*
 * Create the attachent point path.
 */
char *
gen_ap(topo_mod_t *mod, di_minor_t minor)
{
	char	path[MAXPATHLEN];
	char	*devfs_path = di_devfs_minor_path(minor);

	/* ap_path = "/devices" + minor node path */
	(void) snprintf(path, MAXPATHLEN, "%s%s", DEVICES, devfs_path);
	di_devfs_path_free(devfs_path);

	return (topo_mod_strdup(mod, path));
}

/*
 * Return TRUE if chassis label is an 'internal' chassis.
 */
boolean_t
internal_ch(bay_t *bp)
{
	if (cmp_str(bp->ch_label, INTERNAL)) {
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Generte the occupant path.
 */
char *
gen_oc(topo_mod_t *mod, di_node_t dnode, di_path_t pnode)
{
	int		rv;
	int		lun;
	boolean_t	got_w;
	char		path[MAXPATHLEN];
	char		*devfs_path = NULL;
	char		*target_port = NULL;

	char		*f = "gen_oc";

	/* dev path */
	if (pnode != DI_PATH_NIL) {
		devfs_path = di_path_devfs_path(pnode);
		bcopy(devfs_path, path, strlen(devfs_path) + 1);
		di_devfs_path_free(devfs_path);
		topo_mod_dprintf(mod, "%s: pathinfo path (%s)\n", f, path);
		goto done;
	}

	if (dnode != DI_NODE_NIL) {
		devfs_path = di_devfs_path(dnode);
		topo_mod_dprintf(mod, "%s: devinfo path (%s)\n", f,
		    devfs_path);
	} else {
		topo_mod_dprintf(mod, "%s: no path\n", f);
		goto out;
	}

	/* 'target-port' prop */
	target_port = topo_mod_alloc(mod, MAXNAMELEN);
	if (target_port == NULL) {
		topo_mod_dprintf(mod, "%s: no memory for target-port\n", f);
		goto out;
	}
	rv = get_str_prop(dnode, pnode, SCSI_ADDR_PROP_TARGET_PORT,
	    target_port);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to get target-port\n", f);
		goto out;
	}

	/* 'lun' proo */
	lun = get_int_prop(dnode, pnode, "lun");
	if (lun < 0 || lun > 255) {
		topo_mod_dprintf(mod, "%s: invalid lun (%d)\n", f, lun);
		goto out;
	}

	/* leading 'w' is not always consistent */
	got_w = target_port[0] == 'w' ? B_TRUE : B_FALSE;

	/* oc_path = devfs path + "/disk@w' + 'target-port' + "," + 'lun' */
	(void) snprintf(path, MAXPATHLEN, "%s%s%s,%x", devfs_path,
	    (got_w ? "/disk@" : "/disk@w"), target_port, lun);
	topo_mod_free(mod, target_port, MAXNAMELEN);
done:
	return (topo_mod_strdup(mod, path));
out:
	if (target_port != NULL) {
		topo_mod_free(mod, target_port, MAXNAMELEN);
	}
	if (devfs_path != NULL) {
		di_devfs_path_free(devfs_path);
	}
	return (NULL);
}

/*
 * Get the devid property from the disk node matching the path passed in.
 */
char *
get_devid(topo_mod_t *mod, char *oc_path, bay_t *bp)
{
	int		rv;
	di_path_t	dpath = DI_PATH_NIL;
	di_node_t	dnode = DI_NODE_NIL;
	di_node_t	cnode = DI_NODE_NIL;
	char		*path = NULL;
	char		*devid = NULL;

	char		*f = "get_devid";

	dnode = di_drv_first_node("sd", topo_mod_devinfo(mod));
	while (dnode != DI_NODE_NIL) {
		while ((dpath = di_path_client_next_path(dnode, dpath)) !=
		    NULL) {
			path = di_path_devfs_path(dpath);
			if (cmp_str(path, oc_path)) {
				rv = di_prop_lookup_strings(DDI_DEV_T_ANY,
				    dnode, DEVID_PROP_NAME, &devid);
				if (rv < 0) {
					topo_mod_dprintf(mod, "%s: no devid "
					    "for path: %s\n", f, path);
					di_devfs_path_free(path);
					goto nopath;
				}
				di_devfs_path_free(path);
				goto done;
			}
			di_devfs_path_free(path);
		}
		dnode = di_drv_next_node(dnode);
	}
nopath:
	/* find the child matching the phy and grab the devid prop */
	dpath = DI_PATH_NIL;
	rv = find_child(mod, bp->hba_dnode, &cnode, &dpath, bp->phy);
	if (rv == 0) {
		char *ndevid = topo_mod_alloc(mod, MAXNAMELEN);
		char buf[MAXNAMELEN];

		rv = get_str_prop(cnode, dpath, DEVID_PROP_NAME, ndevid);
		if (rv != 0) {
			topo_mod_free(mod, ndevid, MAXNAMELEN);
			return (NULL);
		}

		bcopy(ndevid, buf, strlen(ndevid) + 1);
		topo_mod_free(mod, ndevid, MAXNAMELEN);
		topo_mod_dprintf(mod, "%s: ndevid for %s:%d: %s\n", f,
		    bp->hba_nm, bp->phy, buf);
		return (topo_mod_strdup(mod, buf));
	}
done:
	if (devid != NULL) {
		topo_mod_dprintf(mod, "%s: devid for %s:%d: %s\n", f,
		    bp->hba_nm, bp->phy, devid);
		return (topo_mod_strdup(mod, devid));
	}

	topo_mod_dprintf(mod, "%s: failed to get devid for path: %s\n", f,
	    oc_path);

	return (NULL);
}

/*
 * Get the total number of possible direct attached PHYs (bays). It's either
 * the 'num-phys-hba' or 'num-phys' integer property of the HBA di_node.
 */
int
get_num_phys(di_node_t dnode)
{
	int	num_phys;

	/* first look for 'num-phys' */
	num_phys = get_int_prop(dnode, DI_PATH_NIL, NUM_PHYS);
	if (num_phys == -1) {
		/* next look for 'num-phys-hba' */
		num_phys = get_int_prop(dnode, DI_PATH_NIL, NUM_PHYS_HBA);
		if (num_phys == -1) {
			num_phys = DFLT_NUM_PHYS;
		}
	}

	return (num_phys);
}

/*
 * Get the product name (FM_FMRI_AUTH_PRODUCT) and chassis S/N
 * (FM_FMRI_AUTH_CHASSIS) from the authority fmri.
 */
int
get_prod(topo_mod_t *mod, tnode_t *tparent, char *prod, char *ch_sn)
{
	int		rv;
	char		*val = NULL;
	tnode_t		*tnp = NULL;
	nvlist_t	*auth;

	if (cmp_str(topo_node_name(tparent), "chassis")) {
		tnp = tparent;
	} else {
		/* everyone has either a chassis or a mother(board) */
		tnp = topo_node_lookup(tparent, CHASSIS, 0);
		if (tnp == NULL) {
			tnp = topo_node_lookup(tparent, MOTHERBOARD, 0);
		}
		if (tnp == NULL) {
			topo_mod_dprintf(mod,
			    "No chassis or motherboard node.\n");
			return (-1);
		}
	}

	/* get auth list from topo node */
	auth = topo_mod_auth(mod, tnp);

	/* get product-id */
	rv = nvlist_lookup_string(auth, FM_FMRI_AUTH_PRODUCT, &val);
	if (rv == 0 && val != NULL) {
		bcopy(val, prod, strlen(val) + 1);
		topo_mod_dprintf(mod, "get_prod: %s: %s\n",
		    FM_FMRI_AUTH_PRODUCT, prod);
	} else {
		bcopy(BAY_PROP_UNKNOWN, prod, strlen(BAY_PROP_UNKNOWN) + 1);
	}

	/* chassis-id */
	val = NULL;
	rv = nvlist_lookup_string(auth, FM_FMRI_AUTH_CHASSIS, &val);
	if (rv == 0 && val != NULL) {
		bcopy(val, ch_sn, strlen(val) + 1);
		topo_mod_dprintf(mod, "get_prod: %s: %s\n",
		    FM_FMRI_AUTH_CHASSIS, ch_sn);
	} else {
		bcopy(BAY_PROP_UNKNOWN, ch_sn, strlen(BAY_PROP_UNKNOWN) + 1);
	}
	nvlist_free(auth);

	return (0);
}

/*
 * Callback for mod walk routine; calls the shared routine to find the
 * hba label.
 */
/* ARGSUSED */
static int
get_slotid_cb(topo_mod_t *mod, tnode_t *tnp, void *arg)
{
	return (th_hba_l(NULL, tnp, arg));
}

/*
 * Get the slot id by parsing the pci label for the slot, or return 0 if
 * it's an on-board device (label == "MB").
 */
int
get_slotid_from_pcilabel(topo_mod_t *mod, tnode_t *tnp, di_node_t dnode)
{
	int		rv;
	int		err;
	int		i;
	int		j;
	int		id;
	char		n[3];
	tw_pcie_cbs_t	cbs;
	topo_walk_t	*twp;

	char		*f = "get_slotid_from_pcilabel";

	/* init walk */
	cbs.devfs_path = di_devfs_path(dnode);
	cbs.label = topo_mod_alloc(mod, MAXNAMELEN);
	cbs.mod = mod;
	cbs.hdl = NULL;
	twp = topo_mod_walk_init(mod, tnp, get_slotid_cb, &cbs, &err);
	if (twp == NULL) {
		topo_mod_dprintf(mod, "%s: topo_walk_inti() failed\n", f);
		id = -1;
		goto out;
	}

	/* walk */
	rv = topo_walk_step(twp, TOPO_WALK_CHILD);
	if (rv == TOPO_WALK_ERR) {
		topo_walk_fini(twp);
		topo_mod_dprintf(mod, "%s: failed to walk topology\n", f);
		id = -1;
		goto out;
	}
	topo_walk_fini(twp);

	/* no label */
	if (strlen(cbs.label) == 0) {
		topo_mod_dprintf(mod, "%s: no label for %s\n", f,
		    cbs.devfs_path);
		id = -1;
		goto out;
	}
	topo_mod_dprintf(mod, "%s: topo label (%s)\n", f, cbs.label);

	/* on-board - slot id is 0 by pcie spec */
	if (cmp_str(cbs.label, "MB")) {
		topo_mod_dprintf(mod, "%s: on-board (%s)\n", f,
		    cbs.devfs_path);
		id = 0;
		goto out;
	}

	/* extract the slot id from the label */
	for (i = (strlen(cbs.label) + 1), j = 2; i >= 0; i--, j--) {
		if (cbs.label[i] < '0' && cbs.label[i] > '9' &&
		    cbs.label[i] != '\0')
			continue;
		n[j] = cbs.label[i];
	}

	/* convert to an int */
	id = atoi((const char *)n);

	/* 'PCIe 0' is really slot id 1 */
	id++;
out:
	topo_mod_free(mod, cbs.label, MAXNAMELEN);
	di_devfs_path_free(cbs.devfs_path);
	return (id);
}

/*
 * Parse a line of the bay config file to extract data.
 */
static int
parse_line(topo_mod_t *mod, di_node_t dnode, bay_t *bp, char *buf)
{
	int	i;
	int	phy;
	int	drv_inst;
	char	*prod = NULL;
	char	*drv_name = NULL;
	char	*ch_l = NULL;
	char	*ch_sn = NULL;
	char	*label = NULL;
	char	*token;
	char	*hba_nm = di_driver_name(dnode);
	int	hba_inst = di_instance(dnode);

	/* skip lines starting with '#' */
	if (*buf == '#' || *buf == '\n') {
		return (-1);
	}

	/* parse the line e.g. */
	/* Sans Digital TR4X:pmcs:0:JBOD 0:812BDA443-184:5:BAY2 */
	if ((token = strtok(buf, ":")) != NULL) {
		prod = token;
		if ((token = strtok(NULL, ":")) != NULL) {
			drv_name = token;
			if ((token = strtok(NULL, ":")) != NULL) {
				drv_inst = atoi(token);
				if ((token = strtok(NULL, ":")) != NULL) {
					ch_l = token;
					if ((token = strtok(NULL, ":")) !=
					    NULL) {
						ch_sn = token;
						if ((token = strtok(NULL, ":"))
						    != NULL) {
							phy = atoi(token);
							if ((token =
							    strtok(NULL, ":"))
							    != NULL) {
								label = token;
							}
						}
					}
				}
			}
		}
	}

	/* check strings */
	if (prod == NULL || drv_name == NULL || ch_l == NULL ||
	    ch_sn == NULL || label == NULL) {
		topo_mod_dprintf(mod, "parse_line: bad format\n");
		return (-1);
	}

	/* check the hba driver name and instance */
	if (!cmp_str(hba_nm, drv_name) || hba_inst != drv_inst) {
		/* not what we're looking for */
		return (-1);
	}

	/* cut off the last newline */
	for (i = 0; i < strlen(label); i++) {
		if (label[i] == '\n') {
			label[i] = '\0';
		}
	}

	topo_mod_dprintf(mod, "parse_line: product(%s) drv name(%s) inst(%d) "
	    "chassis label(%s) chassis S/N(%s) phy(%d) bay label(%s)\n",
	    prod, drv_name, drv_inst, ch_l, ch_sn, phy, label);

	/* fill in the bay struct */
	bp->hba_dnode = dnode;
	bp->hba_nm = topo_mod_strdup(mod, drv_name);
	bp->hba_inst = drv_inst;
	bp->ch_prod = topo_mod_strdup(mod, prod);
	bp->ch_label = topo_mod_strdup(mod, ch_l);
	bp->ch_serial = topo_mod_strdup(mod, ch_sn);
	bp->phy = phy;
	bp->label = topo_mod_strdup(mod, label);

	return (0);
}

/*
 * Read the config file and fill in the bays array. Return the number of
 * structs (bays) filled in.
 */
int
read_config(topo_mod_t *mod, di_node_t dnode, char *f, bay_t *bp, int *n)
{
	int	rv;
	int	cnt = 0;
	char	s[MAXNAMELEN];
	FILE	*fp;

	/* open the config file and read how many bays for this hba */
	fp = fopen(f, "r");
	if (fp == NULL) {
		topo_mod_dprintf(mod,
		    "read_config: failed to open config file (%s)\n", f);
		return (-1);
	}

	/*
	 * config file format:
	 * "product:driver name:instance:product name:product s/n:PHY:label"
	 * "%s:%s:%d:%s:%d:%s"
	 */
	while (fgets(s, MAXNAMELEN, fp) != NULL) {
		rv = parse_line(mod, dnode, &bp[cnt], s);
		if (rv == 0) {
			cnt++;
		}
	}

	(void) fclose(fp);
	*n = cnt;
	return (0);
}

/*
 * Sort the hba_nodes[] array relative to their slot id. If the slot id is
 * not obtainable; sort in the order seen.
 *
 * Return: 0 - successfully sorted
 *         1 - failed to sort (usually due to lack of PCIe slotid)
 */
int
sort_hba_nodes(topo_mod_t *mod, tnode_t *tnp, di_node_t *hba_nodes)
{
	int		i;
	int		j;
	int		slot_id;
	di_node_t	tmp_d = DI_NODE_NIL;

	struct s {
		di_node_t d;
		int id;
	} ss[MAX_HBAS];

	topo_mod_dprintf(mod, "sort_hba_nodes: hba_node_cnt = %d\n",
	    hba_node_cnt);

	/* fill in the sorting struct */
	for (i = 0; i < hba_node_cnt; i++) {
		/*
		 * Get the pci slot id from the HBA pcibus topo label.
		 * The pcibus enum has already done all the twizzle
		 * required to figure out the correct slot-id/label.
		 */
		slot_id = get_slotid_from_pcilabel(mod, tnp, hba_nodes[i]);
		if (slot_id == -1) {
			return (-1);
		}
		topo_mod_dprintf(mod, "sort_hba_nodes: slotid = %d\n",
		    slot_id);

		ss[i].d = hba_nodes[i];
		ss[i].id = slot_id;
	}

	/* get ya sort on */
	for (j = 0; j < hba_node_cnt; j++) {
		for (i = 0; i < (hba_node_cnt - 1); i++) {
			if (ss[i + 1].id < ss[i].id) {
				tmp_d = ss[i].d;
				ss[i].d = ss[i+1].d;
				ss[i + 1].d = tmp_d;
			}
		}
	}

	/* refill hba_nodes now in relative order */
	for (i = 0; i < hba_node_cnt; i++) {
		hba_nodes[i] = ss[i].d;
	}

	return (0);
}

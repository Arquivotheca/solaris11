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
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <stropts.h>
#include <pthread.h>
#include <inttypes.h>
#include <fm/topo_mod.h>
#include <fm/topo_list.h>
#include <sys/scsi/scsi_address.h>
#include <sys/scsi/impl/scsi_sas.h>
#include <sys/fm/protocol.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/pci_tools.h>
#include <libdevinfo.h>
#include <bay.h>

extern int hba_node_cnt;

/*
 * These are routines that are used by both the bay.so topo enumerator and the
 * $SRC/cmd/fm/fmti topo interactive utility.
 */


/*
 * Compare two strings.
 */
boolean_t
cmp_str(const char *s1, const char *s2)
{
	if (strlen(s1) == strlen(s2) &&
	    strncmp(s1, s2, strlen(s2)) == 0) {
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Clear trailing blanks from product name/serial numbers.
 */
char *
ctbl(char *s)
{
	int	i;

	/* remove any trailing blank spaces */
	for (i = strlen(s) - 1; i > 0; i--) {
		if (s[i] == ' ') {
			s[i] = '\0';
		} else {
			/* stop at the first non-blank */
			break;
		}
	}

	return (s);
}

/*
 * Callback for devtree walk.
 *
 * Look for SAS HBA nodes.
 */
int
gather_sas_hba(di_node_t dnode, void *arg)
{
	int		rv;
	di_node_t	parent_node = DI_NODE_NIL;
	di_node_t	*hba_nodes = (di_node_t *)arg;
	char		strp[MAXNAMELEN];
	char		model[MAXNAMELEN];

	rv = get_str_prop(dnode, DI_PATH_NIL, "model", model);
	if (rv == 0) {
		if (cmp_str(model, MODEL_SAS) ||
		    cmp_str(model, MODEL_SCSI)) {
			/* make sure it's SAS direct attached */
			if (sas_direct(dnode)) {
				/* This is a SAS HBA node */
				hba_nodes[hba_node_cnt] = dnode;
				hba_node_cnt++;
			}
		}
	} else {
		/* look to see if we already captured it's parent */
		parent_node = di_parent_node(dnode);
		if (parent_node != DI_NODE_NIL) {
			rv = get_str_prop(parent_node, DI_PATH_NIL, "model",
			    model);
			if (rv == 0) {
				return (DI_WALK_CONTINUE);
			}
			rv = get_str_prop(parent_node, DI_PATH_NIL,
			    "ddi-vhci-class", model);
			if (rv == 0) {
				return (DI_WALK_CONTINUE);
			}
		}

		/* check for 'ddi-vhci-class' */
		rv = get_str_prop(dnode, DI_PATH_NIL, "ddi-vhci-class", strp);
		if (rv == 0 && cmp_str(strp, "scsi_vhci")) {
			/* make sure it's SAS direct attached */
			if (sas_direct(dnode)) {
				/* This is a SAS HBA node */
				hba_nodes[hba_node_cnt] = dnode;
				hba_node_cnt++;
			}
		}
	}

	if (hba_node_cnt == MAX_HBAS) {
		return (DI_WALK_TERMINATE);
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Create the configuration file path/name. Attempt to create in this order:
 *  1. ../<product>-<chassis-sn>,bay_labels
 *  2. ../<product>,bay_labels
 *  3. ../<platform>,bay_labels
 */
void
gen_ofile_name(char *prod, char *csn, char *filenm)
{
	char	platform[MAXNAMELEN];
	char	*prod_name = malloc(MAXNAMELEN);
	char	*ch_sn = malloc(MAXNAMELEN);
	int	prod_len = 0;
	int	sn_len = 0;

	/* retain raw name/sn */
	bcopy(prod, prod_name, strlen(prod) + 1);
	bcopy(csn, ch_sn, strlen(csn) + 1);

	/* clean strings */
	prod_name	= di_cro_strclean(prod_name, 1, 1);
	ch_sn		= di_cro_strclean(ch_sn, 1, 1);

	/* get platform name */
	platform[0] = '\0';
	(void) sysinfo(SI_PLATFORM, platform, sizeof (platform));

	if (!cmp_str(prod_name, BAY_PROP_UNKNOWN)) {
		prod_len = strlen(prod_name);
	}
	if (!cmp_str(ch_sn, BAY_PROP_UNKNOWN)) {
		sn_len = strlen(ch_sn);
	}

	if (prod_len > 0 && sn_len > 0) {
		/* ../<product>-<product-sn>,bay_labels */
		(void) snprintf(filenm, MAXNAMELEN, BAY_CONFIG,
		    platform, prod_name, ch_sn);
	} else if (prod_len > 0 && sn_len == 0) {
		/* ../<product>,bay_labels */
		(void) snprintf(filenm, MAXNAMELEN, BAY_CONF_FILE_1,
		    platform, prod_name);
	} else {
		/* ../<platform>,bay_labels */
		(void) snprintf(filenm, MAXNAMELEN, BAY_CONF_FILE_1,
		    platform, platform);
	}

	/* clean up */
	free(prod_name);
	free(ch_sn);
}

/*
 * Get integer property.
 */
int
get_int_prop(di_node_t dnode, di_path_t pnode, char *prop_name)
{
	int		prop_val;
	int		*buf;
	unsigned char	*chbuf;
	di_path_t	path_node = DI_PATH_NIL;
	di_prop_t	di_prop = DI_PROP_NIL;
	di_path_prop_t	di_path_prop = DI_PROP_NIL;

	if (prop_name == NULL ||
	    (dnode == DI_NODE_NIL && pnode == DI_PATH_NIL)) {
		return (-1);
	}

	/* look for pathinfo property */
	if (pnode != DI_PATH_NIL) {
		while ((di_path_prop =
		    di_path_prop_next(pnode, di_path_prop)) != DI_PROP_NIL) {
			if (strcmp(prop_name,
			    di_path_prop_name(di_path_prop)) == 0) {
				(void) di_path_prop_ints(di_path_prop, &buf);
				bcopy(buf, &prop_val, sizeof (int));
				goto found;
			}
		}
	}

	/* look for devinfo property */
	if (dnode != DI_NODE_NIL) {
		for (di_prop = di_prop_next(dnode, DI_PROP_NIL);
		    di_prop != DI_PROP_NIL;
		    di_prop = di_prop_next(dnode, di_prop)) {
			if (cmp_str(prop_name, di_prop_name(di_prop))) {
				if (di_prop_bytes(di_prop, &chbuf) <
				    sizeof (uint_t)) {
					continue;
				}
				bcopy(chbuf, &prop_val, sizeof (uint_t));
				goto found;
			}
		}

		/* dig some more */
		while ((path_node = di_path_phci_next_path(dnode, path_node)) !=
		    DI_PATH_NIL) {
			while ((di_path_prop = di_path_prop_next(path_node,
			    di_path_prop)) != DI_PROP_NIL) {
				if (strcmp(prop_name,
				    di_path_prop_name(di_path_prop)) == 0) {
					(void) di_path_prop_ints(di_path_prop,
					    &buf);
					bcopy(buf, &prop_val, sizeof (int));
					goto found;
				}
			}
		}
	}

	/* property not found */
	return (-1);
found:
	return (prop_val);
}

/*
 * Get the phy from the "attached-port-pm" property which is
 * stored as a bit mask (1 << phy-num) string.
 * If there is no phy mask property look for 'phy-num' property which
 * is the PHY (potentially depricated at some point). SATA mpt drives
 * PHY is the target value.
 */
int
get_phy(di_node_t dnode, di_path_t pnode)
{
	int	rv;
	int	phy = -1;
	char	p[MAXNAMELEN];

	/* first look for (SAS) 'attached-port-pm' */
	rv = get_str_prop(dnode, pnode, SCSI_ADDR_PROP_ATTACHED_PORT_PM, p);
	if (rv != 0 && dnode != DI_NODE_NIL) {
		/* look at the child devinfo node */
		rv = get_str_prop(di_child_node(dnode), DI_PATH_NIL,
		    SCSI_ADDR_PROP_ATTACHED_PORT_PM, p);
	}
	if (rv == 0) {
		int i, phy_mask;
		char *endp;
		phy_mask = (int)strtol(p, &endp, 16);
		for (i = 1; i < MAX_PM_SHIFT; i++) {
			if ((phy_mask >> i) == 0x0) {
				phy = (int)(i - 1);
				break;
			}
		}
		goto found;
	}

	/* next look for (SAS) 'phy-num' */
	phy = get_int_prop(dnode, pnode, PHY_NUM);
	if (phy == -1 && dnode != DI_NODE_NIL) {
		/* look at the child devinfo node */
		phy = get_int_prop(di_child_node(dnode), DI_PATH_NIL, PHY_NUM);
	}
	if (phy != -1) {
		goto found;
	}

	/* must be a SATA drive, first look for 'sata-phy' */
	phy = get_int_prop(dnode, pnode, SCSI_ADDR_PROP_SATA_PHY);
	if (phy != -1) {
		goto found;
	}

	/* lastly for SATA drive, look for 'target' */
	phy = get_int_prop(dnode, pnode, SCSI_ADDR_PROP_TARGET);
	if (phy != -1) {
		goto found;
	}

	return (-1);
found:
	return (phy);
}

/*
 * Get string property.
 */
int
get_str_prop(di_node_t dnode, di_path_t pnode, char *prop_name, char *prop_out)
{
	char		*prop_val = NULL;
	di_path_t	path_node = DI_PATH_NIL;
	di_prop_t	di_prop = DI_PROP_NIL;
	di_path_prop_t	di_path_prop = DI_PROP_NIL;

	if (prop_name == NULL ||
	    (dnode == DI_NODE_NIL && pnode == DI_PATH_NIL)) {
		return (-1);
	}

	/* look for pathinfo property */
	if (pnode != DI_PATH_NIL) {
		while ((di_path_prop =
		    di_path_prop_next(pnode, di_path_prop)) != DI_PROP_NIL) {
			if (strcmp(prop_name,
			    di_path_prop_name(di_path_prop)) == 0) {
				(void) di_path_prop_strings(di_path_prop,
				    &prop_val);
				goto found;
			}
		}
	}

	/* look for devinfo property */
	if (dnode != DI_NODE_NIL) {
		for (di_prop = di_prop_next(dnode, DI_PROP_NIL);
		    di_prop != DI_PROP_NIL;
		    di_prop = di_prop_next(dnode, di_prop)) {
			if (cmp_str(prop_name, di_prop_name(di_prop))) {
				if (di_prop_strings(di_prop, &prop_val) < 0) {
					continue;
				}
				goto found;
			}
		}

		/* dig some more */
		while ((path_node = di_path_phci_next_path(dnode, path_node)) !=
		    DI_PATH_NIL) {
			while ((di_path_prop = di_path_prop_next(path_node,
			    di_path_prop)) != DI_PROP_NIL) {
				if (strcmp(prop_name,
				    di_path_prop_name(di_path_prop)) == 0) {
					(void)
					    di_path_prop_strings(di_path_prop,
					    &prop_val);
					goto found;
				}
			}
		}
	}

	/* property not found */
	return (-1);
found:
	bcopy(prop_val, prop_out, strlen(prop_val) + 1);
	return (0);
}

/*
 * All iport devi nodes should have the 'initiator-port' property. Look for
 * a matching 'attached-port' property. But tha's not always the case so we
 * use node1 and node2 notation.
 */
boolean_t
i_direct(di_node_t dnode, char *node1_prop_str, char *node2_prop_str)
{
	int		rv;
	di_node_t	cnode = DI_NODE_NIL;
	di_path_t	pnode = DI_PATH_NIL;
	char		ap[MAXNAMELEN];
	char		ip[MAXNAMELEN];

	/* look for devi 'initiator-port' (or equivalent) */
	rv = get_str_prop(dnode, DI_PATH_NIL, node1_prop_str, ip);
	if (rv != 0) {
		return (B_FALSE);
	}

	/*
	 * look for either a child devinfo or a pathinfo node that
	 * contains a 'attached-port' (or equivalent) property to match.
	 */
	cnode = di_child_node(dnode);
	if (cnode != DI_NODE_NIL) {
		rv = get_str_prop(cnode, DI_PATH_NIL, node2_prop_str, ap);
		if (rv == 0) {
			if (cmp_str(ip, ap)) {
				/* make sure it's not smp */
				if (cmp_str("sd", di_node_name(cnode)) ||
				    cmp_str("disk", di_node_name(cnode)))
					return (B_TRUE);
			}
		}
	}

	pnode = di_path_phci_next_path(dnode, DI_PATH_NIL);
	if (pnode != DI_PATH_NIL) {
		rv = get_str_prop(DI_NODE_NIL, pnode, node2_prop_str, ap);
		if (rv == 0) {
			if (cmp_str(ip, ap)) {
				/* make sure it's not smp */
				if (cmp_str("sd", di_path_node_name(pnode)) ||
				    cmp_str("disk", di_path_node_name(pnode)))
					return (B_TRUE);
			}
		}
	}
	return (B_FALSE);
}

/*
 * For HBAs "other" than ones that support iport:
 *  - first check that it's not attached to an expander
 *  - then see if it's direct attached
 */
boolean_t
o_direct(di_node_t dnode)
{
	int		cnt, nphys;
	boolean_t	ret = B_FALSE;
	di_node_t	cnode = DI_NODE_NIL;
	di_path_t	pnode = DI_PATH_NIL;

	/*
	 * Unfortunately some HBA drivers don't differentiate between direct
	 * attached and expander attached storage via properties. Need to
	 * determine if this HBA is connected to an expander.
	 */
	nphys = get_int_prop(dnode, DI_PATH_NIL, "num-phys");

	/* count devinfo child nodes */
	if ((cnode = di_child_node(dnode)) != DI_NODE_NIL) {
		/* count devinfo children */
		cnt = 1;
		while ((cnode = di_sibling_node(cnode)) != DI_NODE_NIL) {
			++cnt;
		}
		if (cnt > nphys) {
			/* expander */
			goto out;
		}
	}

	/* count pathinfo nodes */
	if ((pnode = di_path_phci_next_path(dnode, DI_PATH_NIL)) !=
	    DI_PATH_NIL) {
		/* count pathinfo nodes */
		cnt = 1;
		while ((pnode = di_path_phci_next_path(dnode, pnode)) !=
		    DI_PATH_NIL) {
			++cnt;
		}
		if (cnt > nphys) {
			/* expander */
			goto out;
		}
	}

	/* check for 'sd' or 'disk' devinfo nodes */
	cnode = di_child_node(dnode);
	if (cnode != DI_NODE_NIL &&
	    (cmp_str("sd", di_node_name(cnode)) ||
	    cmp_str("disk", di_node_name(cnode)))) {
		ret = B_TRUE;
		goto out;
	}

	/* final check for 'sd' or 'disk' pathinfo nodes */
	pnode = di_path_phci_next_path(dnode, DI_PATH_NIL);
	if (pnode != DI_PATH_NIL &&
	    (cmp_str("sd", di_path_node_name(pnode)) ||
	    cmp_str("disk", di_path_node_name(pnode)))) {
		ret = B_TRUE;
	}
out:
	return (ret);
}

/*
 * Different SAS HBA driver (children) report they're direct attached
 * differently. MPxIO also determines how to decide if this is sas direct.
 *
 * For HBAs that support iport, each will contain the '*-port' properties which
 * determine direct attached.
 *
 * For HBAs that don't support iport, the direct attached hba will have a
 * "disk"/"sd" devinfo child or pathinfo node.
 *
 * Direct Attached:
 * mpt         : !expander && child/path node name == "sd" || "disk"
 * mpt_sas/scu : iport 'initator-port' ==> device 'attached-port'
 * pmcs        : iport 'attached-port' ==> device 'target-port'
 *
 */
boolean_t
sas_direct(di_node_t dnode)
{
	di_node_t	iport_dnode = DI_NODE_NIL;

	if (dnode == DI_NODE_NIL) {
		return (B_FALSE);
	}

	/* look for child iport node */
	iport_dnode = di_child_node(dnode);
	while (iport_dnode != DI_NODE_NIL) {
		if (cmp_str("iport", di_node_name(iport_dnode))) {
			break;
		}
		iport_dnode = di_sibling_node(iport_dnode);
	}

	/* check for iport */
	if (iport_dnode != DI_NODE_NIL) {
		/* 'initiator-port' == 'attached-port' */
		if (i_direct(iport_dnode, SCSI_ADDR_PROP_INITIATOR_PORT,
		    SCSI_ADDR_PROP_ATTACHED_PORT) != B_TRUE) {
			/* 'attached-port' == 'target-port' */
			if (i_direct(iport_dnode, SCSI_ADDR_PROP_ATTACHED_PORT,
			    SCSI_ADDR_PROP_TARGET_PORT) != B_TRUE) {
				return (B_FALSE);
			}
		}
		return (B_TRUE);
	}

	/* no iport; check other indicators */
	return (o_direct(dnode));
}

/*
 * Callback for topo walk looking for hba label by matching the devfs path
 * passed in with the 'dev' property of the topo node.
 */
/* ARGSUSED */
int
th_hba_l(topo_hdl_t *thp, tnode_t *tnp, void *arg)
{
	int		rv;
	int		err;
	int		ret;
	char		*dpath = NULL;
	char		*label = NULL;
	char		*nodename = NULL;
	tw_pcie_cbs_t	*cbp = (tw_pcie_cbs_t *)arg;
	char		*hba_path = cbp->devfs_path;

	/* see if we're as PCIe function node */
	nodename = topo_node_name(tnp);
	if (!cmp_str(nodename, PCIEX_FUNCTION)) {
		ret = TOPO_WALK_NEXT;
		goto out;
	}

	/* look for dev (TOPO_IO_DEV) path in (TOPO_PGROUP_IO) property */
	rv = topo_prop_get_string(tnp, TOPO_PGROUP_IO, TOPO_IO_DEV,
	    &dpath, &err);
	if (rv != 0) {
		ret = TOPO_WALK_NEXT;
		goto out;
	}

	/* check for matching paths */
	if (!cmp_str(dpath, hba_path)) {
		ret = TOPO_WALK_NEXT;
		goto out;
	}

	/* path matches, get the label */
	rv = topo_prop_get_string(tnp, TOPO_PGROUP_PROTOCOL,
	    TOPO_PROP_LABEL, &label, &err);
	if (rv != 0 || label == NULL) {
		ret = TOPO_WALK_NEXT;
		goto out;
	}

	/* copy the label */
	bcopy(label, cbp->label, strlen(label) + 1);
out:
	/* clean up */
	if (cbp->mod != NULL) {
		if (dpath != NULL) {
			topo_mod_strfree(cbp->mod, dpath);
		}
		if (label != NULL) {
			topo_mod_strfree(cbp->mod, label);
		}
	} else if (cbp->hdl != NULL) {
		if (dpath != NULL) {
			topo_hdl_strfree(cbp->hdl, dpath);
		}
		if (label != NULL) {
			topo_hdl_strfree(cbp->hdl, label);
		}
	}
	return (ret);
}

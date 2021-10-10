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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */


/*
 * Subroutines used by various components of the Sun4v PI enumerator
 */

#include <sys/types.h>
#include <sys/systeminfo.h>
#include <sys/utsname.h>
#include <string.h>
#include <strings.h>
#include <sys/fm/protocol.h>
#include <fm/topo_mod.h>
#include <fm/topo_hc.h>
#include <sys/mdesc.h>
#include <libnvpair.h>
#include <devid.h>

#include "pi_impl.h"

/* max pci path = 256 */
#define	MAX_PATH_DEPTH	(MAXPATHLEN / 256)

/* max pci path + child + minor */
#define	MAX_DIPATH_DEPTH	(MAX_PATH_DEPTH + 2)

static const topo_pgroup_info_t sys_pgroup = {
	TOPO_PGROUP_SYSTEM,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

static const topo_pgroup_info_t auth_pgroup = {
	FM_FMRI_AUTHORITY,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};


/*
 * Search the PRI for MDE nodes using md_scan_dag.  Using this routine
 * consolodates similar searches used in a few places within the sun4vpi
 * enumerator.
 *
 * The routine returns the number of nodes found, or -1.  If the node array
 * is non-NULL on return, then it must be freed:
 *	topo_mod_free(mod, nodes, nsize);
 *
 */
int
pi_find_mdenodes(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_start,
    char *type_str, char *arc_str, mde_cookie_t **nodes, size_t *nsize)
{
	int	result;
	int	total_mdenodes;

	mde_str_cookie_t	start_cookie;
	mde_str_cookie_t	arc_cookie;

	/* Prepare to scan the PRI using the start string and given arc */
	total_mdenodes	= md_node_count(mdp);
	start_cookie	= md_find_name(mdp, type_str);
	arc_cookie	= md_find_name(mdp, arc_str);

	/* Allocate an array to hold the results of the scan */
	*nsize		= sizeof (mde_cookie_t) * total_mdenodes;
	*nodes		= topo_mod_zalloc(mod, *nsize);
	if (*nodes == NULL) {
		/* We have no memory.  Set an error code and return failure */
		*nsize = 0;
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		return (-1);
	}

	result = md_scan_dag(mdp, mde_start, start_cookie, arc_cookie, *nodes);
	if (result <= 0) {
		/* No nodes found.  Free the node array before returning */
		topo_mod_free(mod, *nodes, *nsize);
		*nodes = NULL;
		*nsize = 0;
	}

	return (result);
}


/*
 * Determine if this node should be skipped by finding the topo-skip property.
 */
int
pi_skip_node(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node)
{
	int		result;
	uint64_t	skip;

	if (mod == NULL || mdp == NULL) {
		/*
		 * These parameters are required.  Tell the caller to skip
		 * all nodes.
		 */
		return (1);
	}

	skip = 0;	/* do not skip by default */
	result = md_get_prop_val(mdp, mde_node, MD_STR_TOPO_SKIP, &skip);
	if (result != 0) {
		/*
		 * There is no topo-skip property.  Assume we are not skipping
		 * the mde node.
		 */
		skip = 0;
	}

	/*
	 * If skip is present and non-zero we want to skip this node.  We
	 * return 1 to indicate this.
	 */
	if (skip != 0) {
		return (1);
	}
	return (0);
}


/*
 * Build a device path without device names (PRI like) from a devinfo
 * node. Return the PRI like path.
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_dipath(topo_mod_t *mod, di_node_t dnode)
{
	int	i, j, rv;
	int	depth = 0;
	char	*bus_addr[MAX_DIPATH_DEPTH] = { NULL };
	char	*dev_path[MAX_DIPATH_DEPTH] = { NULL };
	char	*path = NULL;
	char	*di_path = NULL;
	size_t	path_len = 0;

	/* loop through collecting bus addresses */
	do {
		/* stop at '/' */
		di_path = di_devfs_path(dnode);
		if (strcmp(di_path, "/") == 0) {
			di_devfs_path_free(di_path);
			break;
		}
		di_devfs_path_free(di_path);

		if (depth < MAX_DIPATH_DEPTH) {
			bus_addr[depth] = topo_mod_strdup(mod,
			    di_bus_addr(dnode));
			++depth;
		} else {
			topo_mod_dprintf(mod, "pi_get_dipath: path too "
			    "long (%d)\n", depth);
			return (NULL);
		}
	} while ((dnode = di_parent_node(dnode)) != DI_NODE_NIL);

	/* prepend '/@' to each bus address */
	for (i = (depth - 1), j = 0; i >= 0; --i, j++) {
		int len = strlen(bus_addr[i]) + strlen("/@") + 1;
		path_len += len;
		dev_path[j] = (char *)topo_mod_alloc(mod, len);
		rv = snprintf(dev_path[j], len, "/@%s", bus_addr[i]);
		if (rv < 0) {
			return (NULL);
		}
	}

	/*
	 * Build the path from the bus addresses.
	 */
	path_len -= (depth - 1); /* leave room for one null char */
	path = (char *)topo_mod_alloc(mod, path_len);
	path = strcpy(path, dev_path[0]);

	for (i = 1; i < depth; i++) {
		path = strncat(path, dev_path[i], strlen(dev_path[i]) + 1);
	}

	for (i = 0; i < depth; i++) {
		if (bus_addr[i] != NULL) {
			topo_mod_strfree(mod, bus_addr[i]);
		}
		if (dev_path[i] != NULL) {
			topo_mod_strfree(mod, dev_path[i]);
		}
	}

	topo_mod_dprintf(mod, "pi_get_dipath: path (%s)\n", path);
	return (path);
}


/*
 * Get the product serial number (the ID as far as the topo authority is
 * concerned) either from the current node, if it is of type 'product', or
 * search for a product node in the PRI.
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_productsn(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node)
{
	int		result;
	int		idx;
	int		num_nodes;
	char		*id = NULL;
	char		*type;
	size_t		size;
	mde_cookie_t	*nodes = NULL;

	topo_mod_dprintf(mod, "pi_get_productsn: enter\n");

	result = md_get_prop_str(mdp, mde_node, MD_STR_TYPE, &type);
	if (result == 0 && strcmp(type, MD_STR_PRODUCT) == 0) {
		/*
		 * This is a product node.  We need only search for the serial
		 * number property on this node to return the ID.
		 */
		result = md_get_prop_str(mdp, mde_node, MD_STR_SERIAL_NUMBER,
		    &id);
		if (result != 0 || id == NULL || strlen(id) == 0)
			return (NULL);

		topo_mod_dprintf(mod, "pi_get_productsn: product-sn = %s\n",
		    id);
		return (topo_mod_strdup(mod, id));
	}

	/*
	 * Search the PRI for nodes of type MD_STR_COMPONENT and find the
	 * first element with type of MD_STR_PRODUCT.  This node
	 * will contain the MD_STR_SERIAL_NUMBER property to use as the
	 * product-sn.
	 */
	num_nodes = pi_find_mdenodes(mod, mdp, MDE_INVAL_ELEM_COOKIE,
	    MD_STR_COMPONENT, MD_STR_FWD, &nodes, &size);
	if (num_nodes <= 0 || nodes == NULL) {
		/* We did not find any component nodes */
		return (NULL);
	}
	topo_mod_dprintf(mod, "pi_get_productsn: found %d %s nodes\n",
	    num_nodes, MD_STR_COMPONENT);

	idx = 0;
	while (id == NULL && idx < num_nodes) {
		result = md_get_prop_str(mdp, nodes[idx], MD_STR_TYPE, &type);
		if (result == 0 && strcmp(type, MD_STR_PRODUCT) == 0) {
			/*
			 * This is a product node.  Get the serial number
			 * property from the node.
			 */
			result = md_get_prop_str(mdp, nodes[idx],
			    MD_STR_SERIAL_NUMBER, &id);
			if (result != 0)
				topo_mod_dprintf(mod, "pi_get_productsn: "
				    "failed to read %s from node_0x%llx\n",
				    MD_STR_SERIAL_NUMBER,
				    (uint64_t)nodes[idx]);
		}
		/* Search the next node, if necessary */
		idx++;
	}
	topo_mod_free(mod, nodes, size);

	/* Everything is freed up and it's time to return the product-sn */
	if (result != 0 || id == NULL || strlen(id) == 0) {
		return (NULL);
	}
	topo_mod_dprintf(mod, "pi_get_productsn: product-sn %s\n", id);

	return (topo_mod_strdup(mod, id));
}


/*
 * Get the chassis serial number (the ID as far as the topo authority is
 * concerned) either from the current node, if it is of type 'chassis', or
 * search for a chassis node in the PRI.
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_chassisid(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node)
{
	int		result;
	int		idx;
	int		num_nodes;
	char		*id = NULL;
	char		*hc_name = NULL;
	size_t		chassis_size;
	mde_cookie_t	*chassis_nodes = NULL;

	topo_mod_dprintf(mod, "pi_get_chassis: enter\n");

	hc_name = pi_get_topo_hc_name(mod, mdp, mde_node);
	if (hc_name != NULL && strcmp(hc_name, MD_STR_CHASSIS) == 0) {
		topo_mod_strfree(mod, hc_name);

		/*
		 * This is a chassis node.  We need only search for the serial
		 * number property on this node to return the ID.
		 */
		result = md_get_prop_str(mdp, mde_node, MD_STR_SERIAL_NUMBER,
		    &id);
		if (result != 0 || id == NULL || strlen(id) == 0) {
			return (NULL);
		}
		topo_mod_dprintf(mod, "pi_get_chassis: chassis-id = %s\n", id);
		return (topo_mod_strdup(mod, id));
	}
	if (hc_name != NULL) {
		topo_mod_strfree(mod, hc_name);
	}

	/*
	 * Search the PRI for nodes of type MD_STR_COMPONENT and find the
	 * first element with topo-hc-type of MD_STR_CHASSIS.  This node
	 * will contain the MD_STR_SERIAL_NUMBER property to use as the
	 * chassis-id.
	 */
	num_nodes = pi_find_mdenodes(mod, mdp, MDE_INVAL_ELEM_COOKIE,
	    MD_STR_COMPONENT, MD_STR_FWD, &chassis_nodes, &chassis_size);
	if (num_nodes <= 0 || chassis_nodes == NULL) {
		/* We did not find any chassis nodes */
		return (NULL);
	}
	topo_mod_dprintf(mod, "pi_get_chassisid: found %d %s nodes\n",
	    num_nodes, MD_STR_COMPONENT);

	idx = 0;
	while (id == NULL && idx < num_nodes) {
		hc_name = pi_get_topo_hc_name(mod, mdp, chassis_nodes[idx]);
		if (hc_name != NULL && strcmp(hc_name, MD_STR_CHASSIS) == 0) {
			/*
			 * This is a chassis node.  Get the serial number
			 * property from the node.
			 */
			result = md_get_prop_str(mdp, chassis_nodes[idx],
			    MD_STR_SERIAL_NUMBER, &id);
			if (result != 0) {
				topo_mod_dprintf(mod, "pi_get_chassisid: "
				    "failed to read %s from node_0x%llx\n",
				    MD_STR_SERIAL_NUMBER,
				    (uint64_t)chassis_nodes[idx]);
			}
		}
		topo_mod_strfree(mod, hc_name);

		/* Search the next node, if necessary */
		idx++;
	}
	topo_mod_free(mod, chassis_nodes, chassis_size);

	/* Everything is freed up and it's time to return the platform-id */
	if (result != 0 || id == NULL || strlen(id) == 0) {
		return (NULL);
	}
	topo_mod_dprintf(mod, "pi_get_chassis: chassis-id %s\n", id);

	return (topo_mod_strdup(mod, id));
}


/*
 * Determine if the node is a FRU by checking for the existance and non-zero
 * value of the 'fru' property on the mde node.
 */
int
pi_get_fru(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node, int *is_fru)
{
	int		result;
	uint64_t	fru;

	if (mod == NULL || mdp == NULL || is_fru == NULL) {
		return (-1);
	}
	fru = 0;
	*is_fru = 0;

	result = md_get_prop_val(mdp, mde_node, MD_STR_FRU, &fru);
	if (result != 0) {
		/* The node is not a FRU. */
		return (-1);
	}
	if (fru != 0) {
		*is_fru = 1;
	}
	return (0);
}


/*
 * Get the id property value from the given PRI node
 */
int
pi_get_instance(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node,
    topo_instance_t *ip)
{
	int		result;
	uint64_t	id;

	id = 0;
	result = md_get_prop_val(mdp, mde_node, MD_STR_ID, &id);
	if (result != 0) {
		/*
		 * There is no id property.
		 */
		topo_mod_dprintf(mod, "node_0x%llx has no id property\n",
		    (uint64_t)mde_node);
		return (-1);
	}
	*ip = id;

	return (0);
}


/*
 * If the given MDE node is a FRU return the 'nac' property, if it exists,
 * to use as the label.
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_label(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node)
{
	int	result;
	int	is_fru;
	char	*lp = NULL;
	char	*hc_name = pi_get_topo_hc_name(mod, mdp, mde_node);

	/*
	 * The disk enumerator will set the "bay" node as a FRU and
	 * expect the label from the PRI. The "fru" property can not
	 * be set because hostconfig has no way to set the S/N, P/N,
	 * etc.. in the PRI.
	 */
	if (strncmp(hc_name, BAY, strlen(BAY)) != 0) {
		result = pi_get_fru(mod, mdp, mde_node, &is_fru);
		if (result != 0 || is_fru == 0) {
			/* This node is not a FRU.  It has no label */
			topo_mod_strfree(mod, hc_name);
			return (NULL);
		}
	}
	topo_mod_strfree(mod, hc_name);

	/*
	 * The node is a FRU.  Get the NAC name to use as a label.
	 */
	result = md_get_prop_str(mdp, mde_node, MD_STR_NAC, &lp);
	if (result != 0 || lp == NULL || strlen(lp) == 0) {
		/* No NAC label.  Return NULL */
		return (NULL);
	}

	/* Return a copy of the label */
	return (topo_mod_strdup(mod, lp));
}


/*
 * Return the "lun" property.
 */
int
pi_get_lun(topo_mod_t *mod, di_node_t node)
{
	int		lun;
	int		*buf;
	unsigned char	*chbuf;
	di_prop_t	di_prop = DI_PROP_NIL;
	di_path_t	dpath = DI_PATH_NIL;
	di_path_prop_t	di_path_prop = DI_PROP_NIL;

	/* look for pathinfo property */
	while ((dpath = di_path_phci_next_path(node, dpath)) != DI_PATH_NIL) {
		while ((di_path_prop =
		    di_path_prop_next(dpath, di_path_prop)) != DI_PROP_NIL) {
			if (strcmp("lun",
			    di_path_prop_name(di_path_prop)) == 0) {
				(void) di_path_prop_ints(di_path_prop, &buf);
				bcopy(buf, &lun, sizeof (int));
				goto found;
			}
		}
	}

	/* look for devinfo property */
	for (di_prop = di_prop_next(node, DI_PROP_NIL);
	    di_prop != DI_PROP_NIL;
	    di_prop = di_prop_next(node, di_prop)) {
		if (strncmp("lun", di_prop_name(di_prop),
		    strlen(di_prop_name(di_prop))) == 0) {
			if (di_prop_bytes(di_prop, &chbuf) < sizeof (uint_t)) {
				continue;
			}
			bcopy(chbuf, &lun, sizeof (uint_t));
			goto found;
		}
	}

	if (di_prop == DI_PROP_NIL && (dpath == DI_PATH_NIL ||
	    di_path_prop == DI_PROP_NIL)) {
		return (-1);
	}
found:
	topo_mod_dprintf(mod, "pi_get_lun: lun = (%d)\n", lun);
	return (lun);
}


/*
 * Return the complete part number string to the caller.  The complete part
 * number is made up of the part number attribute concatenated with the dash
 * number attribute of the mde node.
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_part(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node)
{
	int	result;
	size_t	bufsize;
	char	*buf  = NULL;
	char	*part = NULL;
	char	*dash = NULL;

	result = md_get_prop_str(mdp, mde_node, MD_STR_PART_NUMBER, &part);
	if (result != 0) {
		part = NULL;
	}
	result = md_get_prop_str(mdp, mde_node, MD_STR_DASH_NUMBER, &dash);
	if (result != 0) {
		dash = NULL;
	}
	bufsize = 1 + (part ? strlen(part) : 0) + (dash ? strlen(dash) : 0);
	if (bufsize == 1) {
		return (NULL);
	}

	/* Construct the part number from the part and dash values */
	buf = topo_mod_alloc(mod, bufsize);
	if (buf != NULL) {
		(void) snprintf(buf, bufsize, "%s%s", (part ? part : ""),
		    (dash ? dash : ""));
	}

	return (buf);
}


/*
 * Return the path string to the caller.
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_path(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node)
{
	int	result;
	int	i = 0;
	size_t	max_addrs;
	size_t	path_len = 0;
	size_t	buf_len;
	char	*propbuf = NULL;
	char	*buf = NULL;
	char	*buffree;
	char	*path = NULL;
	char	*token;
	char	*dev_addr[MAX_PATH_DEPTH] = { NULL };
	char	*dev_path[MAX_PATH_DEPTH] = { NULL };
	char	*lastp;

	/*
	 * Get the path property from PRI; should look something
	 * like "/@600/@0".
	 */
	result = md_get_prop_str(mdp, mde_node, MD_STR_PATH, &propbuf);
	if (result != 0 || propbuf == NULL || strlen(propbuf) == 0) {
		topo_mod_dprintf(mod, "pi_get_path: failed to get path\n");
		return (NULL);
	}
	buf_len = strlen(propbuf) + 1;
	buf = topo_mod_alloc(mod, buf_len);
	if (buf == NULL) {
		topo_mod_dprintf(mod, "pi_get_path: no memory\n");
		return (NULL);
	}
	buffree = buf; /* strtok_r is destructive */
	(void) strcpy(buf, propbuf);

	/*
	 * Grab the address(es) from the path property.
	 */
	if ((token = strtok_r(buf, "/@", &lastp)) != NULL) {
		dev_addr[i] = topo_mod_strdup(mod, token);
		while ((token = strtok_r(NULL, "/@", &lastp)) != NULL) {
			if (++i < MAX_PATH_DEPTH) {
				dev_addr[i] = topo_mod_strdup(mod, token);
			} else {
				topo_mod_dprintf(mod, "pi_get_path: path "
				    "too long (%d)\n", i);
				topo_mod_free(mod, buffree, buf_len);
				return (NULL);
			}
		}
	} else {
		topo_mod_dprintf(mod, "pi_get_path: path wrong\n");
		topo_mod_free(mod, buffree, buf_len);
		return (NULL);
	}
	max_addrs = ++i;
	topo_mod_free(mod, buffree, buf_len);

	/*
	 * Construct the path to look something like "/pci@600/pci@0".
	 */
	for (i = 0; i < max_addrs; i++) {
		int len = strlen(dev_addr[i]) + strlen("/pci@") + 1;
		path_len += len;
		dev_path[i] = (char *)topo_mod_alloc(mod, len);
		result = snprintf(dev_path[i], len, "/pci@%s", dev_addr[i]);
		if (result < 0) {
			return (NULL);
		}
	}

	path_len -= (i - 1); /* leave room for one null char */
	path = (char *)topo_mod_alloc(mod, path_len);
	path = strcpy(path, dev_path[0]);

	/* put the parts together */
	for (i = 1; i < max_addrs; i++) {
		path = strncat(path, dev_path[i], strlen(dev_path[i]) + 1);
	}

	/*
	 * Cleanup
	 */
	for (i = 0; i < max_addrs; i++) {
		if (dev_addr[i] != NULL) {
			topo_mod_free(mod, dev_addr[i],
			    strlen(dev_addr[i]) + 1);
		}
		if (dev_path[i] != NULL) {
			topo_mod_free(mod, dev_path[i],
			    strlen(dev_path[i]) + 1);
		}
	}

	topo_mod_dprintf(mod, "pi_get_path: path = (%s)\n", path);
	return (path);
}


/*
 * Get the product ID from the 'platform' node in the PRI
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_productid(topo_mod_t *mod, md_t *mdp)
{
	int		result;
	char		*id = NULL;
	size_t		platform_size;
	mde_cookie_t	*platform_nodes = NULL;

	topo_mod_dprintf(mod, "pi_get_product: enter\n");

	/*
	 * Search the PRI for nodes of type MD_STR_PLATFORM, which contains
	 * the product-id in it's MD_STR_NAME property.
	 */
	result = pi_find_mdenodes(mod, mdp, MDE_INVAL_ELEM_COOKIE,
	    MD_STR_PLATFORM, MD_STR_FWD, &platform_nodes, &platform_size);
	if (result <= 0 || platform_nodes == NULL) {
		/* We did not find any platform nodes */
		return (NULL);
	}
	topo_mod_dprintf(mod, "pi_get_productid: found %d platform nodes\n",
	    result);

	/*
	 * There should only be 1 platform node, so we will always
	 * use the first if we find any at all.
	 */
	result = md_get_prop_str(mdp, platform_nodes[0], MD_STR_NAME, &id);
	topo_mod_free(mod, platform_nodes, platform_size);

	/* Everything is freed up and it's time to return the platform-id */
	if (result != 0 || id == NULL || strlen(id) == 0) {
		return (NULL);
	}
	topo_mod_dprintf(mod, "pi_get_product: returning %s\n", id);

	return (topo_mod_strdup(mod, id));
}


/*
 * If the phy pointer is NULL just return the number of 'phy_number' properties
 * from the PRI; otherwise pass the 'phy_number' property values back to the
 * caller.
 *
 * The caller is responsible for managing allocated memory.
 */
int
pi_get_priphy(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node, uint8_t *phyp)
{
	int	result;
	uint8_t	*phy_num;
	int	nphy;

	result = md_get_prop_data(mdp, mde_node, MD_STR_PHY_NUMBER,
	    &phy_num, &nphy);
	if (result != 0) {
		/* no phy_number property */
		topo_mod_dprintf(mod,
		    "node_0x%llx has no phy_number property\n",
		    (uint64_t)mde_node);
		return (-1);
	}

	if (phyp != NULL) {
		bcopy(phy_num, phyp, nphy);
	}
	return (nphy);
}


/*
 * Return "phy-num" devinfo/pathinfo property.
 */
int
pi_get_phynum(topo_mod_t *mod, di_node_t node)
{
	int		phy;
	int		*buf;
	unsigned char	*chbuf;
	di_prop_t	di_prop = DI_PROP_NIL;
	di_path_t	dpath = DI_PATH_NIL;
	di_path_prop_t	di_path_prop = DI_PROP_NIL;


	/* look for pathinfo property */
	while ((dpath = di_path_phci_next_path(node, dpath)) != DI_PATH_NIL) {
		while ((di_path_prop =
		    di_path_prop_next(dpath, di_path_prop)) != DI_PROP_NIL) {
			if (strcmp("phy-num",
			    di_path_prop_name(di_path_prop)) == 0) {
				(void) di_path_prop_ints(di_path_prop, &buf);
				bcopy(buf, &phy, sizeof (int));
				goto found;
			}
		}
	}

	/* look for devinfo property */
	for (di_prop = di_prop_next(node, DI_PROP_NIL);
	    di_prop != DI_PROP_NIL;
	    di_prop = di_prop_next(node, di_prop)) {
		if (strncmp("phy-num", di_prop_name(di_prop),
		    strlen("phy-num")) == 0) {
			if (di_prop_bytes(di_prop, &chbuf) < sizeof (uint_t)) {
				continue;
			}
			bcopy(chbuf, &phy, sizeof (uint_t));
			goto found;
		}
	}

	if (di_prop == DI_PROP_NIL && (dpath == DI_PATH_NIL ||
	    di_path_prop == DI_PROP_NIL)) {
		return (-1);
	}
found:
	topo_mod_dprintf(mod, "pi_get_phynum: phy = %d\n", phy);
	return (phy);
}


/*
 * Return the revision string to the caller.
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_revision(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node)
{
	int	result;
	char	*rp = NULL;

	result = md_get_prop_str(mdp, mde_node, MD_STR_REVISION_NUMBER, &rp);
	if (result != 0 || rp == NULL || strlen(rp) == 0) {
		return (NULL);
	}

	return (topo_mod_strdup(mod, rp));
}


/*
 * Return the serial number string to the caller.
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_serial(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node)
{
	int		result;
	uint64_t	sn;
	char		*sp = NULL;
	char		buf[MAXNAMELEN];

	result = md_get_prop_str(mdp, mde_node, MD_STR_SERIAL_NUMBER, &sp);
	if (result != 0 || sp == NULL || strlen(sp) == 0) {
		/* Is this a uint64_t type serial number? */
		result = md_get_prop_val(mdp, mde_node, MD_STR_SERIAL_NUMBER,
		    &sn);
		if (result != 0) {
			/* No.  We have failed to find a serial number */
			return (NULL);
		}
		topo_mod_dprintf(mod, "pi_get_serial: node_0x%llx numeric "
		    "serial number %llx\n", (uint64_t)mde_node, sn);

		/* Convert the acquired value to a string */
		result = snprintf(buf, sizeof (buf), "%llu", sn);
		if (result < 0) {
			return (NULL);
		}
		sp = buf;
	}
	topo_mod_dprintf(mod, "pi_get_serial: node_0x%llx = %s\n",
	    (uint64_t)mde_node, (sp == NULL ? "NULL" : sp));

	return (topo_mod_strdup(mod, sp));
}


/*
 * Get the server hostname (the ID as far as the topo authority is
 * concerned) from sysinfo and return a copy to the caller.
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_serverid(topo_mod_t *mod)
{
	int	result;
	char	hostname[MAXNAMELEN];

	topo_mod_dprintf(mod, "pi_get_serverid: enter\n");

	result = sysinfo(SI_HOSTNAME, hostname, sizeof (hostname));
	/* Everything is freed up and it's time to return the platform-id */
	if (result == -1) {
		return (NULL);
	}
	topo_mod_dprintf(mod, "pi_get_serverid: hostname = %s\n", hostname);

	return (topo_mod_strdup(mod, hostname));
}


/*
 * Return the "target-port" property.
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_target_port(topo_mod_t *mod, di_node_t node)
{
	char		*tport;
	di_prop_t	di_prop = DI_PROP_NIL;
	di_path_t	dpath = DI_PATH_NIL;
	di_path_prop_t	di_path_prop = DI_PROP_NIL;

	/* look for pathinfo property */
	while ((dpath = di_path_phci_next_path(node, dpath)) != DI_PATH_NIL) {
		while ((di_path_prop =
		    di_path_prop_next(dpath, di_path_prop)) != DI_PROP_NIL) {
			if (strcmp("target-port",
			    di_path_prop_name(di_path_prop)) == 0) {
				(void) di_path_prop_strings(di_path_prop,
				    &tport);
				goto found;
			}
		}
	}

	/* look for devinfo property */
	for (di_prop = di_prop_next(node, DI_PROP_NIL);
	    di_prop != DI_PROP_NIL;
	    di_prop = di_prop_next(node, di_prop)) {
		if (strcmp("target-port", di_prop_name(di_prop)) == 0) {
			if (di_prop_strings(di_prop, &tport) < 0) {
				continue;
			}
			goto found;
		}
	}

	if (di_prop == DI_PROP_NIL && (dpath == DI_PATH_NIL ||
	    di_path_prop == DI_PROP_NIL)) {
		return (NULL);
	}
found:
	topo_mod_dprintf(mod, "pi_get_target_port: 'target-port' = (%s)\n",
	    tport);
	return (topo_mod_strdup(mod, tport));
}


/*
 * Get the hc scheme name for the given node.
 *
 * The string must be freed with topo_mod_strfree()
 */
char *
pi_get_topo_hc_name(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node)
{
	int	result;
	char	*hc_name;

	/*
	 * Request the hc name from the node.
	 */
	hc_name = NULL;
	result = md_get_prop_str(mdp, mde_node, MD_STR_TOPO_HC_NAME, &hc_name);
	if (result != 0 || hc_name == NULL) {
		topo_mod_dprintf(mod,
		    "failed to get property %s from node_0x%llx\n",
		    MD_STR_TOPO_HC_NAME, (uint64_t)mde_node);
		return (NULL);
	}

	/* Return a copy of the type string */
	return (topo_mod_strdup(mod, hc_name));
}


/*
 * Calculate the authority information for a node.  Inherit the data if
 * possible, but always create an appropriate property group.
 */
int
pi_set_auth(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node,
    tnode_t *t_parent, tnode_t *t_node)
{
	int 		result;
	int		err;
	nvlist_t	*auth;
	char		*val = NULL;
	char		*prod = NULL;
	char		*psn = NULL;
	char		*csn = NULL;
	char		*server = NULL;

	if (mod == NULL || mdp == NULL || t_parent == NULL || t_node == NULL) {
		return (-1);
	}

	result = topo_pgroup_create(t_node, &auth_pgroup, &err);
	if (result != 0 && err != ETOPO_PROP_DEFD) {
		/*
		 * We failed to create the property group and it was not
		 * already defined.  Set the err code and return failure.
		 */
		(void) topo_mod_seterrno(mod, err);
		return (-1);
	}

	/* Get the authority information already available from the parent */
	auth = topo_mod_auth(mod, t_parent);

	/*
	 * Set the authority data, inheriting it if possible, but creating it
	 * if necessary.
	 */

	/* product-id */
	result = topo_prop_inherit(t_node, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_PRODUCT, &err);
	if (result != 0 && err != ETOPO_PROP_DEFD) {
		val = NULL;
		result = nvlist_lookup_string(auth, FM_FMRI_AUTH_PRODUCT,
		    &val);
		if (result != 0 || val == NULL) {
			/*
			 * No product information in the parent node or auth
			 * list.  Find the product information in the PRI.
			 */
			prod = pi_get_productid(mod, mdp);
			if (prod == NULL) {
				topo_mod_dprintf(mod, "pi_set_auth: product "
				    "name not found for node_0x%llx\n",
				    (uint64_t)mde_node);
			}
		} else {
			/*
			 * Dup the string.  If we cannot find it in the auth
			 * nvlist we will need to free it, so this lets us
			 * have a single code path.
			 */
			prod = topo_mod_strdup(mod, val);
		}

		/*
		 * We continue even if the product information is not available
		 * to enumerate as much as possible.
		 */
		if (prod != NULL) {
			result = topo_prop_set_string(t_node, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_PRODUCT, TOPO_PROP_IMMUTABLE, prod,
			    &err);
			if (result != 0) {
				/* Preserve the error and continue */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod, "pi_set_auth: failed to "
				    "set property %s (%d) : %s\n",
				    FM_FMRI_AUTH_CHASSIS, err,
				    topo_strerror(err));
			}
			topo_mod_strfree(mod, prod);
		}
	}

	/* product-sn */
	result = topo_prop_inherit(t_node, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_PRODUCT_SN, &err);
	if (result != 0 && err != ETOPO_PROP_DEFD) {
		val = NULL;
		result = nvlist_lookup_string(auth, FM_FMRI_AUTH_PRODUCT_SN,
		    &val);
		if (result != 0 || val == NULL) {
			/*
			 * No product-sn information in the parent node or auth
			 * list.  Find the product-sn information in the PRI.
			 */
			psn = pi_get_productsn(mod, mdp, mde_node);
			if (psn == NULL) {
				topo_mod_dprintf(mod, "pi_set_auth: psn "
				    "name not found for node_0x%llx\n",
				    (uint64_t)mde_node);
			} else
				topo_mod_auth_update_psn(mod, t_parent, psn);
		} else {
			/*
			 * Dup the string.  If we cannot find it in the auth
			 * nvlist we will need to free it, so this lets us
			 * have a single code path.
			 */
			psn = topo_mod_strdup(mod, val);
		}

		/*
		 * We continue even if the product information is not available
		 * to enumerate as much as possible.
		 */
		if (psn != NULL) {
			result = topo_prop_set_string(t_node, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_PRODUCT_SN, TOPO_PROP_IMMUTABLE, psn,
			    &err);
			if (result != 0) {
				/* Preserve the error and continue */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod, "pi_set_auth: failed to "
				    "set property %s (%d) : %s\n",
				    FM_FMRI_AUTH_PRODUCT_SN, err,
				    topo_strerror(err));
			}
			topo_mod_strfree(mod, psn);
		}
	}

	/* chassis-id */
	result = topo_prop_inherit(t_node, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_CHASSIS, &err);
	if (result != 0 && err != ETOPO_PROP_DEFD) {
		val = NULL;
		result = nvlist_lookup_string(auth, FM_FMRI_AUTH_CHASSIS,
		    &val);
		if (result != 0 || val == NULL) {
			/*
			 * No product information in the parent node or auth
			 * list.  Find the product information in the PRI.
			 */
			csn = pi_get_chassisid(mod, mdp, mde_node);
			if (csn == NULL) {
				topo_mod_dprintf(mod, "pi_set_auth: csn "
				    "name not found for node_0x%llx\n",
				    (uint64_t)mde_node);
			} else
				topo_mod_auth_update_csn(mod, t_parent, csn);
		} else {
			/*
			 * Dup the string.  If we cannot find it in the auth
			 * nvlist we will need to free it, so this lets us
			 * have a single code path.
			 */
			csn = topo_mod_strdup(mod, val);
		}

		/*
		 * We continue even if the product information is not available
		 * to enumerate as much as possible.
		 */
		if (csn != NULL) {
			result = topo_prop_set_string(t_node, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_CHASSIS, TOPO_PROP_IMMUTABLE, csn,
			    &err);
			if (result != 0) {
				/* Preserve the error and continue */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod, "pi_set_auth: failed to "
				    "set property %s (%d) : %s\n",
				    FM_FMRI_AUTH_CHASSIS, err,
				    topo_strerror(err));
			}
			topo_mod_strfree(mod, csn);
		}
	}

	/* server-id */
	result = topo_prop_inherit(t_node, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_SERVER, &err);
	if (result != 0 && err != ETOPO_PROP_DEFD) {
		val = NULL;
		result = nvlist_lookup_string(auth, FM_FMRI_AUTH_SERVER,
		    &val);
		if (result != 0 || val == NULL) {
			/*
			 * No product information in the parent node or auth
			 * list.  Find the product information in the PRI.
			 */
			server = pi_get_serverid(mod);
			if (server == NULL) {
				topo_mod_dprintf(mod, "pi_set_auth: server "
				    "name not found for node_0x%llx\n",
				    (uint64_t)mde_node);
			}
		} else {
			/*
			 * Dup the string.  If we cannot find it in the auth
			 * nvlist we will need to free it, so this lets us
			 * have a single code path.
			 */
			server = topo_mod_strdup(mod, val);
		}

		/*
		 * We continue even if the product information is not available
		 * to enumerate as much as possible.
		 */
		if (server != NULL) {
			result = topo_prop_set_string(t_node, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_SERVER, TOPO_PROP_IMMUTABLE, server,
			    &err);
			if (result != 0) {
				/* Preserve the error and continue */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod, "pi_set_auth: failed to "
				    "set property %s (%d) : %s\n",
				    FM_FMRI_AUTH_SERVER, err,
				    topo_strerror(err));
			}
			topo_mod_strfree(mod, server);
		}
	}

	nvlist_free(auth);

	return (0);
}


/*
 * Calculate a generic FRU for the given node.  If the node is not a FRU,
 * then inherit the FRU data from the nodes parent.
 */
int
pi_set_frufmri(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node,
    const char *name, topo_instance_t inst, tnode_t *t_parent, tnode_t *t_node)
{
	int		result;
	int		err;
	int		is_fru;
	char		*part;
	char		*rev;
	char		*serial;
	nvlist_t	*auth = NULL;
	nvlist_t	*frufmri = NULL;

	if (t_node == NULL || mod == NULL || mdp == NULL) {
		return (-1);
	}

	/*
	 * Determine if this node is a FRU
	 */
	result = pi_get_fru(mod, mdp, mde_node, &is_fru);
	if (result != 0 || is_fru == 0) {
		/* This node is not a FRU.  Inherit from parent and return */
		(void) topo_node_fru_set(t_node, NULL, 0, &result);
		return (0);
	}

	/*
	 * This node is a FRU.  Create an FMRI.
	 */
	part	= pi_get_part(mod, mdp, mde_node);
	rev	= pi_get_revision(mod, mdp, mde_node);
	serial	= pi_get_serial(mod, mdp, mde_node);
	auth	= topo_mod_auth(mod, t_parent);
	frufmri	= topo_mod_hcfmri(mod, t_parent, FM_HC_SCHEME_VERSION, name,
	    inst, NULL, auth, part, rev, serial);
	if (frufmri == NULL) {
		topo_mod_dprintf(mod, "failed to create FRU: %s\n",
		    topo_strerror(topo_mod_errno(mod)));
	}
	nvlist_free(auth);
	topo_mod_strfree(mod, part);
	topo_mod_strfree(mod, rev);
	topo_mod_strfree(mod, serial);

	/* Set the FRU, whether NULL or not */
	result = topo_node_fru_set(t_node, frufmri, 0, &err);
	if (result != 0)  {
		(void) topo_mod_seterrno(mod, err);
	}
	nvlist_free(frufmri);

	return (result);
}


int
pi_set_label(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node, tnode_t *t_node)
{
	int	result;
	int	err;
	char	*label;

	if (mod == NULL || mdp == NULL) {
		return (-1);
	}

	/*
	 * Get the label, if any, from the mde node and apply it as the label
	 * for this topology node.  Note that a NULL label will inherit the
	 * label from topology node's parent.
	 */
	label = pi_get_label(mod, mdp, mde_node);
	result = topo_node_label_set(t_node, label, &err);
	topo_mod_strfree(mod, label);
	if (result != 0) {
		(void) topo_mod_seterrno(mod, err);
		topo_mod_dprintf(mod, "pi_set_label: failed with label %s "
		    "on node_0x%llx: %s\n", (label == NULL ? "NULL" : label),
		    (uint64_t)mde_node, topo_strerror(err));
	}

	return (result);
}


/*
 * Calculate the system information for a node.  Inherit the data if
 * possible, but always create an appropriate property group.
 */
int
pi_set_system(topo_mod_t *mod, tnode_t *t_node)
{
	int		result;
	int		err;
	struct utsname	uts;
	char		isa[MAXNAMELEN];

	if (mod == NULL || t_node == NULL) {
		return (-1);
	}

	result = topo_pgroup_create(t_node, &sys_pgroup, &err);
	if (result != 0 && err != ETOPO_PROP_DEFD) {
		/*
		 * We failed to create the property group and it was not
		 * already defined.  Set the err code and return failure.
		 */
		(void) topo_mod_seterrno(mod, err);
		return (-1);
	}

	result = topo_prop_inherit(t_node, TOPO_PGROUP_SYSTEM, TOPO_PROP_ISA,
	    &err);
	if (result != 0 && err != ETOPO_PROP_DEFD) {
		isa[0] = '\0';
		result = sysinfo(SI_ARCHITECTURE, isa, sizeof (isa));
		if (result == -1) {
			/* Preserve the error and continue */
			topo_mod_dprintf(mod, "pi_set_system: failed to "
			    "read SI_ARCHITECTURE: %d\n", errno);
		}
		if (strnlen(isa, MAXNAMELEN) > 0) {
			result = topo_prop_set_string(t_node,
			    TOPO_PGROUP_SYSTEM, TOPO_PROP_ISA,
			    TOPO_PROP_IMMUTABLE, isa, &err);
			if (result != 0) {
				/* Preserve the error and continue */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod, "pi_set_auth: failed to "
				    "set property %s (%d) : %s\n",
				    TOPO_PROP_ISA, err, topo_strerror(err));
			}
		}
	}

	result = topo_prop_inherit(t_node, TOPO_PGROUP_SYSTEM,
	    TOPO_PROP_MACHINE, &err);
	if (result != 0 && err != ETOPO_PROP_DEFD) {
		result = uname(&uts);
		if (result == -1) {
			/* Preserve the error and continue */
			(void) topo_mod_seterrno(mod, errno);
			topo_mod_dprintf(mod, "pi_set_system: failed to "
			    "read uname: %d\n", errno);
		}
		if (strnlen(uts.machine, sizeof (uts.machine)) > 0) {
			result = topo_prop_set_string(t_node,
			    TOPO_PGROUP_SYSTEM, TOPO_PROP_MACHINE,
			    TOPO_PROP_IMMUTABLE, uts.machine, &err);
			if (result != 0) {
				/* Preserve the error and continue */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod, "pi_set_auth: failed to "
				    "set property %s (%d) : %s\n",
				    TOPO_PROP_MACHINE, err, topo_strerror(err));
			}
		}
	}

	return (0);
}


tnode_t *
pi_node_bind(topo_mod_t *mod, md_t *mdp, mde_cookie_t mde_node,
    tnode_t *t_parent, const char *hc_name, topo_instance_t inst,
    nvlist_t *fmri)
{
	int	result;
	tnode_t	*t_node;

	if (t_parent == NULL) {
		topo_mod_dprintf(mod,
		    "cannot bind for node_0x%llx instance %d type %s\n",
		    (uint64_t)mde_node, inst, hc_name);
		return (NULL);
	}

	/* Bind this node to the parent */
	t_node = topo_node_bind(mod, t_parent, hc_name, inst, fmri);
	if (t_node == NULL) {
		topo_mod_dprintf(mod,
		    "failed to bind node_0x%llx instance %d: %s\n",
		    (uint64_t)mde_node, (uint32_t)inst,
		    topo_strerror(topo_mod_errno(mod)));
		return (NULL);
	}
	topo_mod_dprintf(mod, "bound node_0x%llx instance %d type %s\n",
	    (uint64_t)mde_node, inst, hc_name);

	/*
	 * We have bound the node.  Now decorate it with an appropriate
	 * FRU and label (which may be inherited from the parent).
	 *
	 * The disk enumerator requires that 'bay' nodes not set their
	 * fru property.
	 */
	if (strncmp(hc_name, BAY, strlen(BAY)) != 0) {
		result = pi_set_frufmri(mod, mdp, mde_node, hc_name, inst,
		    t_parent, t_node);
		if (result != 0) {
			/*
			 * Though we have failed to set the FRU FMRI we still
			 * continue. The module errno is set by the called
			 * routine, so we report the problem and move on.
			 */
			topo_mod_dprintf(mod,
			    "failed to set FRU FMRI for node_0x%llx\n",
			    (uint64_t)mde_node);
		}
	}

	result = pi_set_label(mod, mdp, mde_node, t_node);
	if (result != 0) {
		/*
		 * Though we have failed to set the label, we still continue.
		 * The module errno is set by the called routine, so we report
		 * the problem and move on.
		 */
		topo_mod_dprintf(mod, "failed to set label for node_0x%llx\n",
		    (uint64_t)mde_node);
	}

	result = pi_set_auth(mod, mdp, mde_node, t_parent, t_node);
	if (result != 0) {
		/*
		 * Though we have failed to set the authority, we still
		 * continue. The module errno is set by the called routine, so
		 * we report the problem and move on.
		 */
		topo_mod_dprintf(mod, "failed to set authority for "
		    "node_0x%llx\n", (uint64_t)mde_node);
	}

	result = pi_set_system(mod, t_node);
	if (result != 0) {
		/*
		 * Though we have failed to set the system group, we still
		 * continue. The module errno is set by the called routine, so
		 * we report the problem and move on.
		 */
		topo_mod_dprintf(mod, "failed to set system for node_0x%llx\n",
		    (uint64_t)mde_node);
	}

	return (t_node);
}

/* Return the devid for the supplied path. */
int
pi_get_devid(topo_mod_t *mod, char *oc_path, char **devid)
{
	int		rv = 0;
	di_path_t	tpath = NULL;
	di_node_t	disknode = DI_NODE_NIL;
	char		*strbuf;

	/*
	 * We only need 'sd' nodes, because we're doing direct-attached
	 * enumeration of sun4v physical inventory. Any ssd(7d)-attached
	 * nodes in this system will be covered by a ses-enclosure node
	 * elsewhere.
	 */
	disknode = di_drv_first_node("sd", topo_mod_devinfo(mod));
	while (disknode != DI_NODE_NIL) {
		while ((tpath = di_path_client_next_path(disknode,
		    tpath)) != NULL) {
			strbuf = di_path_devfs_path(tpath);
			rv = strncmp(strbuf, oc_path, MAXPATHLEN);
			if (rv == 0) {
				/* libdevinfo manages the space for devid */
				rv = di_prop_lookup_strings(DDI_DEV_T_ANY,
				    disknode, DEVID_PROP_NAME, devid);
				if (rv < 0)
					topo_mod_dprintf(mod,
					    "pi_get_devid: path match "
					    "(%s cf %s), but no devid (%d)\n",
					    strbuf, oc_path, errno);
				di_devfs_path_free(strbuf);
				return ((rv < 0) ? -1 : 0);
			}
			di_devfs_path_free(strbuf);
		}
		disknode = di_drv_next_node(disknode);
	}
	topo_mod_dprintf(mod, "pi_get_devid: dropped out, no devid found which "
	    "matches path %s\n", oc_path);
	return (-1);
}

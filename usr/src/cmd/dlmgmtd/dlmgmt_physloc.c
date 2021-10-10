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

/*
 *
 * Ordering Physical Network Devices By Location
 *
 * In order to compare physical location of devices to create an ordering of
 * devices that can be used to assign vanity names net0, net1 etc, a method
 * of assigning a value to a location is required.  This is done using a
 * location vector.  A location vector is an array of uint32_t values, with a
 * specific format associated with a given devices/bus types.  These structures
 * are designed to support comparison, with a view to ordering network devices
 * appropriately.  When comparing two location vectors, the corresponding value
 * in each vector is compared from left to right and if one value is less than
 * the other, the vector with the lesser value is considered to be "prior".
 * If a pair of values match, we move on to the next pair of values.  Vectors
 * can vary in length, but in practice - unless the vectors represent the
 * same device - they will never be exactly equal in both length and value.
 *
 * The basic location vector structure is as follows, where each field is a
 * uint32_t:
 *
 * 	|devicetype|index|mboard/ioboard|hostbridge|rootcomplex|bdf|bdf|...
 *
 * PCI and PCIe devices fill in these values via a combination of libtopo's FRU
 * FMRI representation (providing the motherboard, hostbridge and rootcomplex),
 * while the Bus/Dev/Function (BDF) values can be retrieved via libdevinfo
 * (but are retrieved from the libtopo FMRI where available).
 *
 * The bus/def/function (BDF) values are filled in backwards, starting from the
 * network device itself, and moving up the device tree towards the
 * host-to-PCI[e] driver.  The BDF value for a PCI[e] device is available as a
 * DDI property.
 *
 * This ordering is designed to favour Ethernet devices over IP over InfiniBand,
 * Ethernet over InfiniBand and WiFi devices.  Within a devicetype, devices with
 * lower motherboard/ioboard, hostbridge, PCIe rootcomplex, bus, device or
 * function are preferred.  For all PCI[e] devices, the index value is 0.  To
 * favour motherboards over ioboards, a high-order bit is OR-ed in to the
 * ioboard value.
 *
 * InfiniBand devices (IPoIB and EoIB) utilize the physical location of their
 * parent HCA in combination with port and gateway data to determine their
 * place in the ordering.
 *
 * In edge cases - requiring support but less critical - we utilize the order
 * in which the devices was encountered in the device tree, setting and
 * incrementing the device index value in the order in which devices are
 * encountered in a breadth-first walk of the device tree.  So for example, a
 * PCMCIA network card would be assigned index 1, and thus any PCI[e]
 * devices would be prior to it since all of those devices have an index value
 * of 0.
 *
 * The ordering of devices is represented by the location AVL tree, which
 * we will now discuss.
 *
 * Device Location AVL Tree
 *
 * The location AVL tree is an ordered set of dlmgmt_physloc_t structures.
 * If a request for location information for a device that is not in the list is
 * made, the tree must be created or updated.  The location AVL tree is
 * consulted in a number of cases:
 *
 *	- on datalink create upcalls for physical devices (whether vanity naming
 *	  is in operation or not)
 *
 *	- to handle dladm_walk_phys_attr() DLMGMT_CMD_GETNEXTPHYSATTR door
 *	  calls
 *
 * In either of those cases, the location AVL tree can be nonexistent or stale.
 * The tree is stale if a datalink create upcall occurs for a physical device
 * not in the tree (this occurs for DR/hotplug events).  Such events force
 * a refresh of the location AVL tree.  The tree is also refreshed in response
 * to a dladm_walk_phys_attr() call which specifies the DLADM_OPT_FORCE flag
 * (as is done by "dladm init-phys" on a reconfigure boot).
 *
 * A dedicated detatched thread is used to create or refresh the location AVL
 * tree.  The thread is created on demand, and pthread_cond_timedwait() is used
 * to handle timeout conditions.
 *
 * Datalink Location AVL Tree - Locking Model
 *
 * When a datalink creation upcall is received, the dlmgmt write table lock
 * is acquired and not dropped until completion of the creation.  This
 * ensures that creation events are single-threaded.  If the datalink to be
 * created is not in the location AVL tree, the detached "physloc" thread is
 * created.  It initiates creation of a DINFOFORCE libtopo snapshot with a view
 * to ordering devices in the location AVL tree.  The datalink creation thread
 * waits for the "physloc" condition variable (signalling completion)
 * before continuing.
 *
 * Similarly, the dladm_walk_phys_attr() DLMGMT_CMD_GETNEXTPHYSATTR door call
 * will either create, update or consult location AVL tree.  Updates are forced
 * by calling dladm_walk_phys_attr() with the DLADM_OPT_FORCE flag.
 *
 * "dladm init-phys" (run on reconfigure boots) also calls
 * dladm_walk_phys_attr() to generate a location AVL tree if one does not exist.
 *
 * As well as determining the physical order of devices, the physloc thread will
 * fill in location information for existing links if it is missing or outdated.
 * The libtopo FRU label is used.
 *
 * If libtopo enumeration times out, we fall back to a purely libdevinfo-based
 * location strategy.
 */

#include <errno.h>
#include <fm/libtopo.h>
#include <fm/topo_hc.h>
#include <fm/topo_mod.h>
#include <libdevinfo.h>
#include <libdlpi.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <syslog.h>
#include <sys/avl.h>
#include <sys/dld.h>
#include <sys/dls_mgmt.h>
#include <sys/fm/protocol.h>
#include <sys/param.h>
#include <sys/pci.h>
#include <sys/time.h>
#include <sys/types.h>

#include "dlmgmt_impl.h"

typedef struct dlmgmt_physloc_walkinfo_s {
	di_node_t		plw_root;
	di_prom_handle_t	plw_prom_hdl;
	topo_hdl_t		*plw_topo_hdl;
	avl_tree_t		*plw_pl_avl;
	uint32_t		plw_devindex;
} dlmgmt_physloc_walkinfo_t;

/*
 * Location vector fields.
 */
typedef enum dlmgmt_loc_field {
	DLMGMT_LOC_DEVTYPE = 0,
	DLMGMT_LOC_INDEX,
	DLMGMT_LOC_MBOARD,
	DLMGMT_LOC_HOSTBRIDGE,
	DLMGMT_LOC_ROOTCOMPLEX,
	DLMGMT_LOC_BDFSTART
} dlmgmt_loc_field_t;

/* Logical-OR in a high-order bit to order motherboards before ioboards. */
#define	DLMGMT_PHYSLOC_IOBOARD(i)	(i | (1 << 30))

/* Location AVL tree, ordered by location vector. */
avl_tree_t *dlmgmt_physloc_avlp = NULL;
pthread_mutex_t dlmgmt_physloc_avl_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * This condition variable is used to wait for snapshot enumeration to complete
 * in the detached "physloc" thread.
 */
pthread_mutex_t dlmgmt_physloc_thread_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dlmgmt_physloc_thread_cv = PTHREAD_COND_INITIALIZER;

/* Timeout for snapshot enumeration (seconds). */
#define	DLMGMT_PHYSLOC_TIMEOUT		600

/*
 * Use topological snapshots to locate devices.  If libtopo enumeration times
 * out, this value is set to B_FALSE and location is based exclusively on
 * libdevinfo information.
 */
boolean_t usetopo = B_TRUE;

static void
dlmgmt_physloc_avl_destroy(avl_tree_t *avlp)
{
	dlmgmt_physloc_t *pl;
	void *cookie = NULL;

	if (avlp == NULL)
		return;

	while ((pl = avl_destroy_nodes(avlp, &cookie)) != NULL) {
		if (--pl->pl_refcnt == 0)
			free(pl);
	}
	avl_destroy(avlp);
	free(avlp);
}

/*
 * Compare location vectors element-by-element to determine ordering of
 * physical network device location in AVL tree. Return -1 if location
 * represented by v1 is prior to v2, 1 if v2 is prior to v1 and 0 if locations
 * are equal.
 */
static int
dlmgmt_cmp_dev_by_physloc(const void *v1, const void *v2)
{
	const dlmgmt_physloc_t *loc1 = v1;
	const dlmgmt_physloc_t *loc2 = v2;
	uint_t i;
	char prefix1[MAXLINKNAMELEN];
	char prefix2[MAXLINKNAMELEN];
	uint_t ppa1, ppa2;

	/* Same device - no need for vector comparison */
	if (strcmp(loc1->pl_dev, loc2->pl_dev) == 0)
		return (0);

	for (i = 0; i < DLMGMT_PHYSLOC_VECTOR_SIZE; i++) {
		if (loc1->pl_vector[i] == loc2->pl_vector[i])
			continue;
		if (loc1->pl_vector[i] > loc2->pl_vector[i])
			return (1);
		if (loc1->pl_vector[i] < loc2->pl_vector[i])
			return (-1);
	}
	/*
	 * A tie - this can happen if we attempt to add the same
	 * instance twice, or if a device supports multiple
	 * instances per PCI function. In this case, use instance
	 * ppa to order.
	 */
	(void) dlpi_parselink(loc1->pl_dev, prefix1, sizeof (prefix1), &ppa1);
	(void) dlpi_parselink(loc2->pl_dev, prefix2, sizeof (prefix2), &ppa2);

	if (ppa1 < ppa2)
		return (-1);
	else if (ppa1 > ppa2)
		return (1);

	return (strcmp(prefix1, prefix2));
}

static avl_tree_t *
dlmgmt_physloc_avl_create(void)
{
	avl_tree_t *ret = calloc(1, sizeof (avl_tree_t));

	if (ret == NULL) {
		dlmgmt_log(LOG_ERR, "dlmgmt_physloc_avl_create: out of memory");
	} else {
		avl_create(ret, dlmgmt_cmp_dev_by_physloc,
		    sizeof (dlmgmt_physloc_t), offsetof(dlmgmt_physloc_t,
		    pl_node));
	}
	return (ret);
}

/*
 * Retrieve motherboard/ioboard, hostbridge, rootcomplex and bus/dev/function
 * values from the FRU FMRI representation.
 */
static void
dlmgmt_topo_fmri_locate(topo_hdl_t *thp, tnode_t *node, dlmgmt_physloc_t *pl)
{
	nvlist_t *fmri = NULL, **hcls = NULL;
	nvpair_t *nvp;
	uint_t hclnum, i, j;
	int err;
	char *l;
	uint_t vlen = DLMGMT_LOC_BDFSTART - 1;
	uint32_t *v = pl->pl_vector;
	char *propname, *name, *val;
	boolean_t found_mb = B_FALSE;
	boolean_t found_hb = B_FALSE;
	boolean_t found_bdf = B_FALSE;

	if (topo_prop_get_fmri(node, TOPO_PGROUP_PROTOCOL,
	    TOPO_PROP_RESOURCE, &fmri, &err) != 0 ||
	    nvlist_lookup_nvlist_array(fmri, FM_FMRI_HC_LIST, &hcls,
	    &hclnum) != 0) {
		if (fmri != NULL)
			nvlist_free(fmri);
		return;
	}

	for (i = 0; i < hclnum; i++) {
		nvp = NULL;
		while ((nvp = nvlist_next_nvpair(hcls[i], nvp)) != NULL) {
			if ((propname = nvpair_name(nvp)) == NULL ||
			    strcmp(propname, "hc-name") != 0 ||
			    nvpair_value_string(nvp, &name) != 0)
				continue;

			if (strcmp(name, MOTHERBOARD) == 0) {
				nvp = nvlist_next_nvpair(hcls[i], nvp);
				if (nvpair_value_string(nvp, &val) == 0) {
					v[DLMGMT_LOC_MBOARD] = atoi(val);
					found_mb = B_TRUE;
				}
			}
			if (strcmp(name, IOBOARD) == 0) {
				nvp = nvlist_next_nvpair(hcls[i], nvp);
				if (nvpair_value_string(nvp, &val) == 0) {
					v[DLMGMT_LOC_MBOARD] =
					    DLMGMT_PHYSLOC_IOBOARD(atoi(val));
					found_mb = B_TRUE;
				}
			}
			if (strcmp(name, HOSTBRIDGE) == 0)  {
				nvp = nvlist_next_nvpair(hcls[i], nvp);
				if (nvpair_value_string(nvp, &val) == 0) {
					v[DLMGMT_LOC_HOSTBRIDGE] = atoi(val);
					found_hb = B_TRUE;
				}
			}
			if (strcmp(name, PCIEX_ROOT) == 0) {
				nvp = nvlist_next_nvpair(hcls[i], nvp);
				if (nvpair_value_string(nvp, &val) == 0)
					v[DLMGMT_LOC_ROOTCOMPLEX] = atoi(val);
			}
			if (strcmp(name, PCI_BUS) == 0 ||
			    strcmp(name, PCIEX_BUS) == 0) {
				nvp = nvlist_next_nvpair(hcls[i], nvp);
				if (nvpair_value_string(nvp, &val) == 0) {
					v[++vlen] = atoi(val) << 8;
					found_bdf = B_TRUE;
				}
			}
			if (strcmp(name, PCI_DEVICE) == 0 ||
			    strcmp(name, PCIEX_DEVICE) == 0) {
				nvp = nvlist_next_nvpair(hcls[i], nvp);
				if (nvpair_value_string(nvp, &val) == 0)
					v[vlen] |= atoi(val) << 3;
			}
			if (strcmp(name, PCI_FUNCTION) == 0 ||
			    strcmp(name, PCIEX_FUNCTION) == 0) {
				nvp = nvlist_next_nvpair(hcls[i], nvp);
				if (nvpair_value_string(nvp, &val) == 0)
					v[vlen] |= atoi(val);
			}
		}
	}
	if (topo_node_label(node, &l, &err) == 0) {
		(void) snprintf(pl->pl_label, sizeof (pl->pl_label),
		    "%s%s", l, pl->pl_label_suffix);
		topo_hdl_strfree(thp, l);
		/* Replace invalid characters. */
		for (j = 0; pl->pl_label[j] != '\0'; j++) {
			switch (pl->pl_label[j]) {
			case ' ':
			case '\t':
				pl->pl_label[j] = '-';
				break;
			case ',':
			case ':':
			case ';':
				pl->pl_label[j] = '.';
				break;
			}
		}
	}

	if (found_mb && found_hb && found_bdf) {
		pl->pl_located = B_TRUE;
		pl->pl_vector_len = vlen + 1;
	}
	nvlist_free(fmri);
}

/*
 * Retrieve physical /devices path associated with topo node (if any) - if it
 * matches pl_phys_path - which is either the network device physical path or
 * the physical path of the IB HCA - retrieve associated motherboard/ioboard,
 * hostbridge, rootcomplex and bus/dev/function (BDF) values.
 */
static int
dlmgmt_topo_walker(topo_hdl_t *thp, tnode_t *node, void *arg)
{
	dlmgmt_physloc_t *pl = arg;
	char *phys_path;
	int ret = TOPO_WALK_NEXT;
	int err = 0;

	if (topo_prop_get_string(node, TOPO_PGROUP_IO, TOPO_IO_DEV,
	    &phys_path, &err) != 0)
		return (TOPO_WALK_NEXT);
	if (strcmp(pl->pl_phys_path, phys_path) == 0) {
		dlmgmt_topo_fmri_locate(thp, node, pl);
		ret = TOPO_WALK_TERMINATE;
	}
	topo_hdl_strfree(thp, phys_path);
	return (ret);
}

/*
 * This function adds bus/dev/fn values (bdf) for devices and their parent
 * nexus nodes.
 */
static int
dlmgmt_pci_node_locate(dlmgmt_physloc_t *pl, di_node_t node,
    di_prom_handle_t prom_hdl)
{
	di_node_t	n;
	char		*devtype;
	uint_t		*regbuf = NULL;
	uint_t		*codebuf = NULL;
	boolean_t	got_bdf = B_FALSE;
	int		len, j, i = 0;
	int		vlen = DLMGMT_LOC_BDFSTART;
	uint32_t	v[DLMGMT_PHYSLOC_VECTOR_SIZE];

	for (n = node; n != NULL; n = di_parent_node(n)) {
		if (di_prop_lookup_strings(DDI_DEV_T_ANY, n, "device_type",
		    (char **)&devtype) > 0) {
			if (strcmp(devtype, "pci") != 0 &&
			    strcmp(devtype, "pciex") != 0) {
				dlmgmt_log(LOG_DEBUG, "dlmgmt_pci_node_locate: "
				    "unexpected device type %s", devtype);
				return (ENODEV);
			}
		}
		len = di_prop_lookup_ints(DDI_DEV_T_ANY, n, "class-code",
		    (int **)&codebuf);
		if (len <= 0) {
			len = di_prom_prop_lookup_ints(prom_hdl, n,
			    "class-code", (int **)&codebuf);
		}
		if (len <= 0) {
			/*
			 * No class-code is specified - must have reached a
			 * host-to-PCI[e] driver, which does not have a BDF
			 * value of course.  We are done.
			 */
			if (!got_bdf)
				return (ENODEV);
			break;
		}

		len = di_prop_lookup_ints(DDI_DEV_T_ANY, n, "reg",
		    (int **)&regbuf);
		if (len <= 0) {
			len = di_prom_prop_lookup_ints(prom_hdl, n, "reg",
			    (int **)&regbuf);
		}
		if (len <= 0) {
			if (!got_bdf)
				return (ENODEV);
			break;
		}
		v[i++] = PCI_REG_BDFR_G(regbuf[0]) >> 8;
		got_bdf = B_TRUE;
	}
	/* Copy BDF values from root -> device into location vector */
	for (j = i - 1; j >= 0; j--)
		pl->pl_vector[vlen++] = v[j];

	pl->pl_vector_len = vlen;
	pl->pl_located = B_TRUE;

	return (0);
}

/*
 * Try to locate dev node via libtopo, falling back to using device tree
 * data if that fails.
 */
static int
dlmgmt_dev_node_locate(dlmgmt_physloc_t *pl, di_node_t node,
    di_prom_handle_t prom_hdl, topo_hdl_t *thp)
{
	topo_walk_t *twp = NULL;
	int err;

	if (thp != NULL) {
		if ((twp = topo_walk_init(thp, FM_FMRI_SCHEME_HC,
		    dlmgmt_topo_walker, pl, &err)) == NULL) {
			dlmgmt_log(LOG_ERR, "dlmgmt_dev_node_locate: could not "
			    "init topo walk: %s", topo_strerror(err));
		} else if (topo_walk_step(twp, TOPO_WALK_CHILD)
		    == TOPO_WALK_ERR) {
			dlmgmt_log(LOG_ERR, "dlmgmt_dev_node_locate: could not "
			    "do topo walk");
		}
		if (twp != NULL)
			topo_walk_fini(twp);
		if (pl->pl_located)
			return (0);
	}

	return (dlmgmt_pci_node_locate(pl, node, prom_hdl));
}

static int
dlmgmt_net_node_locate(dlmgmt_physloc_t *pl, di_node_t node, di_node_t root,
    di_prom_handle_t prom_hdl, topo_hdl_t *thp)
{
	di_node_t	parent = di_parent_node(node);
	di_node_t	n;
	char		*phys_path;
	int		*port;
	int		*gw_port;
	int64_t		*hca_guid;
	int64_t		*gw_guid;
	char		*gwname;
	char		*portname;
	char		*bus_addr;
	char		hca_drvname[MAXNAMELEN];
	int		hca_drvinst;
	int		i;
	int		err;

	/* Is this an IPoIB port ? */
	if (di_prop_lookup_ints(DDI_DEV_T_ANY, node, "port-number",
	    &port) > 0) {
		(void) snprintf(pl->pl_ib_hca_dev, MAXNAMELEN, "%s%d",
		    di_driver_name(parent), di_instance(parent));
		(void) snprintf(pl->pl_label_suffix, DLD_LOC_STRSIZE,
		    "/PORT%d", port[0]);
		if ((phys_path = di_devfs_path(parent)) == NULL)
			return (errno);
		(void) strlcpy(pl->pl_phys_path, phys_path, MAXPATHLEN);
		di_devfs_path_free(phys_path);
		if ((err = dlmgmt_dev_node_locate(pl, parent, prom_hdl, thp))
		    == 0) {
			pl->pl_vector[DLMGMT_LOC_DEVTYPE] =
			    DLMGMT_DEVTYPE_IPOIB;
			pl->pl_vector[pl->pl_vector_len++] = (uint32_t)port[0];
		}
		return (err);
	}
	/* Is this an EoIB vIOA ? */
	if (di_prop_lookup_int64(DDI_DEV_T_ANY, node, "hca-guid",
	    &hca_guid) > 0 &&
	    di_prop_lookup_ints(DDI_DEV_T_ANY, node, "hca-port#",
	    &port) > 0 &&
	    di_prop_lookup_ints(DDI_DEV_T_ANY, node, "gw-portid",
	    &gw_port) > 0 &&
	    di_prop_lookup_int64(DDI_DEV_T_ANY, node, "gw-system-guid",
	    &gw_guid) > 0 &&
	    di_prop_lookup_strings(DDI_DEV_T_ANY, node, "gw-port-name",
	    &portname) > 0 &&
	    di_prop_lookup_strings(DDI_DEV_T_ANY, node, "gw-system-name",
	    &gwname) > 0 &&
	    (bus_addr = di_bus_addr(node)) != NULL &&
	    sscanf(bus_addr, "%[a-zA-z]%d", hca_drvname, &hca_drvinst) == 2) {
		for (i = 0, n = di_drv_first_node(hca_drvname, root);
		    n != NULL;
		    i++, n = di_drv_next_node(n)) {
			if (hca_drvinst == i) {
				(void) snprintf(pl->pl_ib_hca_dev, MAXNAMELEN,
				    "%s%d", hca_drvname, hca_drvinst);
				(void) snprintf(pl->pl_label_suffix,
				    DLD_LOC_STRSIZE,
				    "/PORT%d/%s/%s", port[0], gwname, portname);
				if ((phys_path = di_devfs_path(n)) == NULL)
					continue;
				(void) strlcpy(pl->pl_phys_path, phys_path,
				    MAXPATHLEN);
				di_devfs_path_free(phys_path);

				if ((err = dlmgmt_dev_node_locate(pl, n,
				    prom_hdl, thp)) == 0) {
					pl->pl_vector[DLMGMT_LOC_DEVTYPE] =
					    DLMGMT_DEVTYPE_EOIB;
					pl->pl_vector[pl->pl_vector_len++] =
					    (uint32_t)port[0];
					pl->pl_vector[pl->pl_vector_len++] =
					    (uint32_t)(gw_guid[0] >> 32);
					pl->pl_vector[pl->pl_vector_len++] =
					    (uint32_t)(gw_guid[0] & 0xffffffff);
				}
			}
		}
		return (ENODEV);
	}
	/* Must be an Ethernet/WiFi node */
	return (dlmgmt_dev_node_locate(pl, node, prom_hdl, thp));
}

/*ARGSUSED*/
static int
dlmgmt_dev_node_is_wifi(di_node_t node, di_minor_t minor, void *arg)
{
	dlmgmt_physloc_t		*pl = arg;
	char				dev[MAXLINKNAMELEN];

	(void) snprintf(dev, MAXLINKNAMELEN, "%s%d", di_driver_name(node),
	    di_instance(node));

	if (strcmp(dev, pl->pl_dev) == 0) {
		pl->pl_vector[DLMGMT_LOC_DEVTYPE] = DLMGMT_DEVTYPE_WIFI;
		return (DI_WALK_TERMINATE);
	}
	return (DI_WALK_CONTINUE);
}

/*ARGSUSED*/
static int
dlmgmt_net_node_add(di_node_t node, di_minor_t minor, void *arg)
{
	dlmgmt_physloc_walkinfo_t	*plw = arg;
	di_node_t			root = plw->plw_root;
	di_prom_handle_t		prom_hdl = plw->plw_prom_hdl;
	char				*phys_path;
	topo_hdl_t			*thp = plw->plw_topo_hdl;
	avl_tree_t			*physloc_avl = plw->plw_pl_avl;
	avl_index_t			loc_where;
	dlmgmt_physloc_t		*pl;
	dlmgmt_link_t			*linkp;
	dlmgmt_obj_t			objp;
	int				err;

	if ((pl = calloc(1, sizeof (dlmgmt_physloc_t))) == NULL) {
		dlmgmt_log(LOG_ERR, "dlmgmt_net_node_add: cannot locate dev: "
		    "out of memory");
		return (DI_WALK_TERMINATE);
	}

	bzero(pl, sizeof (dlmgmt_physloc_t));
	(void) snprintf(pl->pl_dev, MAXLINKNAMELEN, "%s%d",
	    di_driver_name(node), di_instance(node));

	dlmgmt_log(LOG_DEBUG, "dlmgmt_net_node_add: locating %s", pl->pl_dev);

	if ((phys_path = di_devfs_path(node)) == NULL) {
		free(pl);
		return (DI_WALK_CONTINUE);
	}
	(void) strlcpy(pl->pl_phys_path, phys_path, MAXPATHLEN);
	di_devfs_path_free(phys_path);

	/*
	 * For devices located via libtopo or via libdevinfo BDF triples the
	 * the index value is 0.  For all other devices, the index
	 * value is >= 1, based on the order in which the device is encountered
	 * during the walk of the DDI_NT_NET minor nodes.
	 */
	if (dlmgmt_net_node_locate(pl, node, root, prom_hdl, thp) != 0) {
		pl->pl_vector[DLMGMT_LOC_INDEX] = ++plw->plw_devindex;
		pl->pl_vector_len = 2;
	}

	/* Finally identify WiFi devices. */
	(void) di_walk_minor(plw->plw_root, DDI_NT_NET_WIFI, DI_CHECK_ALIAS, pl,
	    dlmgmt_dev_node_is_wifi);

	/*
	 * Check if link already exists for this device.  If so, update the
	 * location information persistently.
	 */
	for (linkp = avl_first(&dlmgmt_id_avl); linkp != NULL;
	    linkp = AVL_NEXT(&dlmgmt_id_avl, linkp)) {
		if (linkp->ll_class == DATALINK_CLASS_PHYS &&
		    objattr_equal(&(linkp->ll_head), FDEVNAME, pl->pl_dev,
		    strlen(pl->pl_dev) + 1) &&
		    !objattr_equal(&(linkp->ll_head), FPHYLOC, pl->pl_label,
		    strlen(pl->pl_label) + 1)) {
			if (strlen(pl->pl_label) == 0) {
				dlmgmt_log(LOG_DEBUG, "dlmgmt_net_node_add: "
				    "%s: no loc info", linkp->ll_link);
				break;
			}
			(void) objattr_set(&(linkp->ll_head), FPHYLOC,
			    pl->pl_label, strlen(pl->pl_label) + 1,
			    DLADM_TYPE_STR);
			dlmgmt_log(LOG_DEBUG, "dlmgmt_net_node_add: "
			    "%s : loc %s", linkp->ll_link, pl->pl_label);
			objp.olink = linkp;
			objp.otype = DLMGMT_OBJ_LINK;
			if ((err = dlmgmt_write_db_entry(linkp->ll_link, &objp,
			    linkp->ll_flags, NULL)) != 0) {
				dlmgmt_log(LOG_ERR, "dlmgmt_net_node_add: "
				    "could not update location information "
				    "%s for %s: %s", pl->pl_dev,
				    pl->pl_label, strerror(err));
			}
			break;
		}
	}

	if (avl_find(physloc_avl, pl, &loc_where) != NULL) {
		free(pl);
		return (DI_WALK_CONTINUE);
	}
	avl_insert(physloc_avl, pl, loc_where);

	return (DI_WALK_CONTINUE);
}

/*ARGSUSED*/
static void *
dlmgmt_physloc_thread(void *arg)
{
	dlmgmt_physloc_walkinfo_t	*plwp = arg;
	dlmgmt_physloc_t		*pl;
	int				err = 0;
	char				*uuid = NULL;
	uint_t				i = 0;
	hrtime_t			start;
	boolean_t			avl_in_use = B_FALSE;
	boolean_t			topo_done = B_FALSE;

	if ((err = dlmgmt_elevate_privileges()) != 0) {
		dlmgmt_log(LOG_ERR, "dlmgmt_physloc_thread: unable to "
		    "elevate privileges: %s", strerror(err));
		(void) pthread_cond_signal(&dlmgmt_physloc_thread_cv);
		free(plwp);
		return (NULL);
	}

	if ((plwp->plw_pl_avl = dlmgmt_physloc_avl_create()) == NULL)
		goto done;
	plwp->plw_devindex = 0;

	plwp->plw_topo_hdl = NULL;

	if (usetopo) {
		/* Exclude unneeded libtopo plugin modules, schemes */
		(void) putenv("TOPOMODEXCLUDE=disk,ses,usb,xfp");
		(void) putenv("TOPOBUILTINEXCLUDE=cpu,mem,pkg,svc,sw,zfs");
		/* Specify DINFOFORCE snapshot. */
		if (debug)
			(void) putenv("TOPO_DEBUG=error,devinfoforce");
		else
			(void) putenv("TOPO_DEBUG=devinfoforce");

		dlmgmt_log(LOG_DEBUG, "dlmgmt_physloc_thread: opening topo");
		if ((plwp->plw_topo_hdl = topo_open(TOPO_VERSION, NULL, &err))
		    != NULL) {
			start = gethrtime();
			dlmgmt_log(LOG_DEBUG, "dlmgmt_physloc_thread: taking "
			    "topo snap");
			uuid = topo_snap_hold(plwp->plw_topo_hdl, NULL, &err);
			if (uuid != NULL) {
				dlmgmt_log(LOG_DEBUG, "dlmgmt_physloc_thread: "
				    "snap took %lld msec",
				    (gethrtime() - start)/1000000);
				plwp->plw_root = topo_hdl_devinfo
				    (plwp->plw_topo_hdl);
				plwp->plw_prom_hdl = topo_hdl_prominfo
				    (plwp->plw_topo_hdl);
				topo_done = B_TRUE;
			} else {
				dlmgmt_log(LOG_ERR, "dlmgmt_physloc_thread: "
				    "could not take topo snap: %s",
				    topo_strerror(err));
				topo_close(plwp->plw_topo_hdl);
				plwp->plw_topo_hdl = NULL;
			}
		} else {
			dlmgmt_log(LOG_ERR, "dlmgmt_physloc_thread: could not "
			    "open topo: %s", topo_strerror(err));
		}
	}
	/*
	 * Fall back to libdevinfo snapshot if usetopo is false or if
	 * libtopo enumeration fails.
	 */
	if (plwp->plw_topo_hdl == NULL) {
		dlmgmt_log(LOG_DEBUG, "dlmgmt_physloc_thread: taking devinfo "
		    "snapshot");
		usetopo = B_FALSE;
		plwp->plw_root = di_init("/", DINFOFORCE | DINFOSUBTREE |
		    DINFOMINOR | DINFOPROP | DINFOPATH);
		if (plwp->plw_root == DI_NODE_NIL) {
			dlmgmt_log(LOG_ERR, "dlmgmt_physloc_thread: could not "
			    "take libdevinfo snap: %s", strerror(errno));
			goto done;
		}
		plwp->plw_prom_hdl = di_prom_init();
		if (plwp->plw_prom_hdl == DI_PROM_HANDLE_NIL) {
			di_fini(plwp->plw_root);
			dlmgmt_log(LOG_ERR, "dlmgmt_physloc_thread: could not "
			    "get prom info: %s", strerror(errno));
			goto done;
		}
	}

	(void) di_walk_minor(plwp->plw_root, DDI_NT_NET, DI_CHECK_ALIAS, plwp,
	    dlmgmt_net_node_add);

	if (plwp->plw_topo_hdl != NULL) {
		topo_hdl_strfree(plwp->plw_topo_hdl, uuid);
		topo_snap_release(plwp->plw_topo_hdl);
		topo_close(plwp->plw_topo_hdl);
	} else {
		di_fini(plwp->plw_root);
		di_prom_fini(plwp->plw_prom_hdl);
	}

	/* Tree has been created.  Add index values, refcnts to each entry */
	for (i = 0, pl = avl_first(plwp->plw_pl_avl); pl != NULL;
	    i++, pl = AVL_NEXT(plwp->plw_pl_avl, pl)) {
		pl->pl_refcnt = 1;
		pl->pl_index = i;
		dlmgmt_log(LOG_DEBUG, "dlmgmt_physloc_thread: %s is device #%d",
		    pl->pl_dev, i);
	}
	/* Success - update global physloc avlp. */
	(void) pthread_mutex_lock(&dlmgmt_physloc_avl_mutex);
	if (dlmgmt_physloc_avlp != NULL)
		dlmgmt_physloc_avl_destroy(dlmgmt_physloc_avlp);
	dlmgmt_physloc_avlp = plwp->plw_pl_avl;
	(void) pthread_mutex_unlock(&dlmgmt_physloc_avl_mutex);
	avl_in_use = B_TRUE;

done:
	if (plwp->plw_pl_avl != NULL && !avl_in_use)
		dlmgmt_physloc_avl_destroy(plwp->plw_pl_avl);
	free(plwp);

	(void) dlmgmt_drop_privileges();

	/*
	 * topological enumeration was initially requested, but it is no longer
	 * wanted.  This thread must have timed out.  Avoid signalling since
	 * the waiting thread is no longer waiting for us.
	 */
	if (topo_done && !usetopo)
		return (NULL);

	(void) pthread_cond_signal(&dlmgmt_physloc_thread_cv);
	return (NULL);
}

void
dlmgmt_loc_vector_print(dlmgmt_physloc_t *pl)
{
	char		s[MAXNAMELEN];
	char		tmp[MAXNAMELEN];
	int		i;

	tmp[0] = '\0';

	for (i = 0; i < pl->pl_vector_len; i++) {
		(void) snprintf(s, MAXNAMELEN, "%s|%x", tmp, pl->pl_vector[i]);
		(void) strlcpy(tmp, s, MAXNAMELEN);
	}
	(void) snprintf(s, MAXNAMELEN, "%s|", tmp);
	dlmgmt_log(LOG_DEBUG, "dlmgmt_loc_vector_print: vector %s -> %s",
	    pl->pl_dev, s);
}

/*
 * Retrieve either
 * - location information for the first device (dev == NULL, after == B_TRUE)
 * - location information for the device (dev == devicename, after == B_FALSE)
 * - location information for next device (dev = devicebefore, after == B_TRUE)
 */
static dlmgmt_physloc_t *
dlmgmt_physloc_avl_find(const char *dev, boolean_t after,
    uint_t *physloc_countp)
{
	dlmgmt_physloc_t *pl, *ret = NULL;

	(void) pthread_mutex_lock(&dlmgmt_physloc_avl_mutex);

	if (dev == NULL && after) {
		ret = avl_first(dlmgmt_physloc_avlp);
		if (physloc_countp == NULL)
			goto done;
	}

	for (pl = avl_first(dlmgmt_physloc_avlp); pl != NULL;
	    pl = AVL_NEXT(dlmgmt_physloc_avlp, pl)) {
		if (physloc_countp != NULL)
			(*physloc_countp)++;
		if (dev == NULL)
			continue;
		if (strcmp(pl->pl_dev, dev) == 0) {
			ret = after ?
			    AVL_NEXT(dlmgmt_physloc_avlp, pl) : pl;
			if (physloc_countp == NULL)
				break;
		}
	}
done:
	(void) pthread_mutex_unlock(&dlmgmt_physloc_avl_mutex);
	return (ret);
}

/*
 * Either:
 *  - retrieve a specific dlmgmt_physloc_t for "dev" (or after "dev" if "after"
 *    is true)
 *  - retrieve the first dlmgmt_physloc_t (if "dev" is NULL, "after" true.
 *  - if "dev" is NULL and "after" is false, simply refresh the list, returning
 *    NULL.
 *
 * If "force" is true, location information is always refreshed.
 *
 * Should always be called with the writer table lock held.
 */
static dlmgmt_physloc_t *
dlmgmt_physloc_get_impl(const char *dev, boolean_t after,
    boolean_t force_refresh, uint_t *physloc_countp)
{
	dlmgmt_physloc_walkinfo_t *plwp;
	dlmgmt_physloc_t *ret = NULL;
	pthread_attr_t attr;
	int err;
	struct timespec timeout;
	struct timeval now;
	pthread_t physloc_thread;

	if (physloc_countp != NULL)
		*physloc_countp = 0;

	dlmgmt_log(LOG_DEBUG, "dlmgmt_physloc_get_impl: get loc %s %s, %s",
	    after ? "after" : "of", dev ? dev : "null",
	    force_refresh ? "refresh" : "no refresh");

	/*
	 * First try existing list if a refresh is not forced.
	 *
	 */
	if (!force_refresh && dlmgmt_physloc_avlp != NULL) {
		if ((ret = dlmgmt_physloc_avl_find(dev, after, physloc_countp))
		    != NULL) {
			dlmgmt_log(LOG_DEBUG, "dlmgmt_physloc_get_impl: "
			    "found %s in cache, no need  to take snap",
			    ret->pl_dev);
			goto done;
		} else if (dev != NULL && after) {
			/* Got NULL because no phys devices left. */
			goto done;
		}
	}

	if ((plwp = calloc(1, sizeof (dlmgmt_physloc_walkinfo_t))) == NULL) {
		dlmgmt_log(LOG_ERR, "dlmgmt_physloc_get_impl: out of memory");
		goto done;
	}

	(void) pthread_attr_init(&attr);
	(void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	err = pthread_create(&physloc_thread, &attr, dlmgmt_physloc_thread,
	    plwp);
	(void) pthread_attr_destroy(&attr);
	if (err != 0) {
		dlmgmt_log(LOG_ERR, "dlmgmt_physloc_get_impl: pthread_create "
		    "error: %s", strerror(err));
		free(plwp);
		return (ret);
	}
	(void) gettimeofday(&now, NULL);
	timeout.tv_sec = now.tv_sec + DLMGMT_PHYSLOC_TIMEOUT;
	timeout.tv_nsec = now.tv_usec / 1000;
	(void) pthread_mutex_lock(&dlmgmt_physloc_thread_mutex);
	err = pthread_cond_timedwait(&dlmgmt_physloc_thread_cv,
	    &dlmgmt_physloc_thread_mutex, &timeout);
	if (err == ETIMEDOUT && usetopo) {
		pthread_t physloc_thread_notopo;

		/*
		 * libtopo snapshots are timing out.  Fall back to using
		 * libdevinfo exclusively.
		 */
		dlmgmt_log(LOG_DEBUG, "dlmgmt_physloc_get_impl: libtopo "
		    "snapshot timeout, reverting to libdevinfo");
		usetopo = B_FALSE;
		if ((plwp = calloc(1, sizeof (dlmgmt_physloc_walkinfo_t)))
		    == NULL) {
			dlmgmt_log(LOG_ERR, "dlmgmt_physloc_get_impl: out of "
			    "memory");
			(void) pthread_mutex_unlock
			    (&dlmgmt_physloc_thread_mutex);
			goto done;
		}
		(void) pthread_attr_init(&attr);
		(void) pthread_attr_setdetachstate(&attr,
		    PTHREAD_CREATE_DETACHED);
		err = pthread_create(&physloc_thread_notopo, &attr,
		    dlmgmt_physloc_thread, plwp);
		(void) pthread_attr_destroy(&attr);
		if (err == 0) {
			err = pthread_cond_wait(&dlmgmt_physloc_thread_cv,
			    &dlmgmt_physloc_thread_mutex);
		} else {
			dlmgmt_log(LOG_ERR, "dlmgmt_physloc_get_impl: "
			    "pthread_create error: %s", strerror(err));
			free(plwp);
		}
	}
	(void) pthread_mutex_unlock(&dlmgmt_physloc_thread_mutex);
	if (err != 0) {
		dlmgmt_log(LOG_ERR, "dlmgmt_physloc_get_impl: error waiting "
		    "for physloc thread: %s", strerror(err));
		return (ret);
	}

	ret = dlmgmt_physloc_avl_find(dev, after, physloc_countp);

done:
	if (ret != NULL) {
		if (debug)
			dlmgmt_loc_vector_print(ret);
		ret->pl_refcnt++;
	}
	return (ret);
}

/* Should be called with dlmgmt table lock held. */
dlmgmt_physloc_t *
dlmgmt_physloc_get(const char *dev, boolean_t after,
    boolean_t force_refresh, uint_t *physloc_countp)
{
	return (dlmgmt_physloc_get_impl(dev, after, force_refresh,
	    physloc_countp));
}

void
dlmgmt_physloc_free(dlmgmt_physloc_t *pl)
{
	if (pl != NULL && --pl->pl_refcnt == 0)
		free(pl);
}

void
dlmgmt_physloc_init(void)
{
	dlmgmt_table_lock(B_TRUE);
	(void) dlmgmt_physloc_get_impl(NULL, B_FALSE, B_TRUE, NULL);
	dlmgmt_table_unlock();
}

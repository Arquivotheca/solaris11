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
#include <pthread.h>
#include <inttypes.h>
#include <sys/pci.h>
#include <fm/topo_mod.h>
#include <fm/topo_list.h>
#include <sys/fm/protocol.h>
#include <fm/topo_mod.h>
#include <libdevinfo.h>

#define	MAX_USB_DEVS	127
#define	MAX_USB_INTERFACE	127

/* Global Definition */
typedef struct usb_enum_dev {
	topo_list_t		ued_link;
	topo_mod_t		*ued_mod;
	di_node_t		ued_dnode;
	int			ued_inst; /* device instance */
	char			ued_name[8];
	di_node_t		ued_pnode; /* most recent pci bridge node */
	int			ued_enum; /* whether be enumed */
	int			ued_bus;
	int			ued_dev;
	int			ued_fun;
} usb_enum_dev_t;

typedef struct usb_enum_data {
	topo_list_t		ue_devs; /* host controller list */
	topo_mod_t		*ue_mod;
	topo_instance_t		ue_instance;
} usb_enum_data_t;

typedef struct usb_cbdata {
	topo_mod_t		*dcb_mod;
	usb_enum_data_t		*dcb_data;
} usb_cbdata_t;

/* Static Definition */
static const topo_pgroup_info_t io_pgroup =
	{ TOPO_PGROUP_IO, TOPO_STABILITY_PRIVATE, TOPO_STABILITY_PRIVATE, 1 };
static const topo_pgroup_info_t usb_pgroup =
	{ TOPO_PGROUP_USB, TOPO_STABILITY_PRIVATE, TOPO_STABILITY_PRIVATE, 1 };

static int usb_is_from_pci = 0;

/* Function Entry */
static tnode_t	*
usb_tnode_create(topo_mod_t *mp, tnode_t *pn, char *name,
    topo_instance_t i, void *priv);

static int
usb_populate_prop(topo_mod_t *mod, tnode_t *tn, di_node_t dn);

static int
usb_children_instantiate(topo_mod_t *mod, tnode_t *pnode,
    di_node_t pn, int depth);

static int
usb_set_asru(topo_mod_t *mod, tnode_t *tn, di_node_t dn);

static int
usb_enum_from_pci(topo_mod_t *mod, tnode_t *pnode,
    const char *name, topo_instance_t min, topo_instance_t max,
    void *arg, void *data);

static int
usb_promprop2int(topo_mod_t *mod, di_node_t n,
    const char *propnm, int **val);

static void
usb_list_cleanup(topo_mod_t *mod, usb_enum_data_t *prvdata);

static int
usb_enum_from_hostbridge(topo_mod_t *mod, tnode_t *pnode,
    usb_enum_data_t *data);

/* Topo pluggin enum entry */
static int usb_enum(topo_mod_t *, tnode_t *, const char *,
	topo_instance_t, topo_instance_t, void *, void *);

static void usb_release(topo_mod_t *mod, tnode_t *tn);

/* Topo pluggin enum ops struct */
static const topo_modops_t usb_ops =
	{ usb_enum, usb_release};

static const topo_modinfo_t usb_info =
	{USBTOPO, FM_FMRI_SCHEME_HC, TOPO_VERSION, &usb_ops};


static int
usb_hwprop2uint(di_node_t n, const char *propnm, uint_t **val)
{
	di_prop_t hp = DI_PROP_NIL;
	uchar_t *buf;

	while ((hp = di_prop_next(n, hp)) != DI_PROP_NIL) {
		if (strncmp(di_prop_name(hp), propnm, 32) == 0) {
			if (di_prop_bytes(hp, &buf) < sizeof (uint_t))
				continue;
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			*val = (uint_t *)buf;

			return (0);
		}
	}

	return (-1);
}

/*
 * Get uint property from di_node_t
 */
int
usb_di_uintprop_get(topo_mod_t *mod, di_node_t n, const char *pnm, uint_t **pv)
{
	if (usb_hwprop2uint(n, pnm, pv) < 0)
		if (usb_promprop2int(mod, n, pnm, (int **)pv) < 0)
			return (-1);

	return (0);
}

static int
usb_host_di_node_add(di_node_t node, usb_cbdata_t *cbp)
{
	usb_enum_dev_t	*controller;
	di_node_t	pdn;
	topo_mod_t	*mod = cbp->dcb_mod;
	uint_t		*reg;
	int		bus, dev, fn;
	topo_list_t	*listp = &(cbp->dcb_data->ue_devs);

	if (usb_di_uintprop_get(mod, node, "reg", &reg) < 0) {
		topo_mod_dprintf(mod, "usb_host_di_node_add: "
		    "fail to get BDF of %s%d node", di_driver_name(node),
		    di_instance(node));

		return (-1);
	}

	controller = topo_mod_zalloc(cbp->dcb_mod, sizeof (usb_enum_dev_t));
	if (controller == NULL) {

		return (-1);
	}

	bus = PCI_REG_BUS_G(*reg);
	dev = PCI_REG_DEV_G(*reg);
	fn = PCI_REG_FUNC_G(*reg);

	topo_mod_dprintf(mod, "usb_host_di_node_add: BDF of %s%d node"
	    " : %02x %02x %02x", di_driver_name(node), di_instance(node),
	    bus, dev, fn);

	pdn = di_parent_node(node);

	controller->ued_dnode = node;
	controller->ued_mod = cbp->dcb_mod;
	controller->ued_inst = di_instance(node);
	controller->ued_bus = bus;
	controller->ued_dev = dev;
	controller->ued_fun = fn;

	(void) snprintf(controller->ued_name, 8, "%s", di_driver_name(node));

	do {
		topo_mod_dprintf(cbp->dcb_mod, "usb_host_di_node_add: %s",
		    di_driver_name(pdn));

		if ((strncmp(di_driver_name(pdn), "pci_pci", 16) == 0) ||
		    (strncmp(di_driver_name(pdn), "pcieb", 16) == 0) ||
		    (strncmp(di_driver_name(pdn), "npe", 16) == 0)) {
			controller->ued_pnode = pdn;

			break;
		}
	} while (pdn = di_parent_node(pdn));

	topo_list_append(listp, controller);

	return (0);
}

/*
 * Find the Hc in host link stored in private data according to their reg.
 */
static int
usb_host_di_node_search(topo_mod_t *mod, di_node_t node, usb_enum_data_t *data,
    usb_enum_dev_t **host)
{
	int		bus, dev, fn;
	usb_enum_dev_t	*controller = NULL;
	uint_t		*reg;
	topo_list_t	*listp = &(data->ue_devs);

	topo_mod_dprintf(mod, "usb_host_di_node_search: "
	    "search for %s%d node", di_driver_name(node),
	    di_instance(node));

	if (usb_di_uintprop_get(mod, node, "reg", &reg) < 0) {
		topo_mod_dprintf(mod, "usb_host_di_node_search: "
		    "fail to get BDF of %s%d node", di_driver_name(node),
		    di_instance(node));

		return (-1);
	}

	bus = PCI_REG_BUS_G(*reg);
	dev = PCI_REG_DEV_G(*reg);
	fn = PCI_REG_FUNC_G(*reg);

	*host = NULL;

	/*
	 * Travel through the ue_devs list. ue_devs is the header
	 * node, search starts from the next
	 */
	while ((listp = topo_list_next(listp)) != NULL) {
		controller = (usb_enum_dev_t *)listp;
		if (controller->ued_bus == bus &&
		    controller->ued_dev == dev &&
		    controller->ued_fun == fn) {
			*host = controller;

			break;
		}
		controller = NULL;
	}

	topo_mod_dprintf(mod, "usb_host_di_node_search: %s found BDF "
	    "= %02x %02x %02x\n", (controller != NULL) ? "":"not", bus,
	    dev, fn);

	return (0);
}

/*
 * Callback entry for di_walk_node
 *    find the USB host controllers di_node and register to the host link
 *    in private data.
 */
static int
usb_dev_walk_di_nodes(di_node_t node, void *arg)
{
	char			*driver_name;
	usb_cbdata_t		*cbp = (usb_cbdata_t *)arg;
	topo_mod_t		*mod = cbp->dcb_mod;

	driver_name = di_driver_name(node);
	if (driver_name == NULL) {

		return (DI_WALK_CONTINUE);
	}

	if ((strncmp(driver_name, "ehci", 8) == 0) ||
	    (strncmp(driver_name, "ohci", 8) == 0) ||
	    (strncmp(driver_name, "uhci", 8) == 0) ||
	    (strncmp(driver_name, "xhci", 8) == 0)) {
		topo_mod_dprintf(mod, "usb_dev_walk_di_nodes: "
		    "find %s device", driver_name);

		if (usb_host_di_node_add(node, arg) != 0) {
			topo_mod_dprintf(mod, "usb_dev_walk_di_nodes: "
			    "fail to add %s%d node", driver_name,
			    di_instance(node));

			return (DI_WALK_CONTINUE);
		}
	}

	return (DI_WALK_CONTINUE);
}

/*
 * To collect all the host controllers in the system
 */
int
usb_host_list_gather(topo_mod_t *mod, usb_enum_data_t *data)
{
	di_node_t		devtree;
	usb_cbdata_t		dcb;

	topo_mod_dprintf(mod, "usb_host_list_gather: entry");

	/* Get the devinfo tree */
	if ((devtree = topo_mod_devinfo(mod)) == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "usb_host_list_gather: "
		    "topo_mod_devinfo() failed");

		return (-1);
	}

	dcb.dcb_mod = mod;
	dcb.dcb_data = data;
	/* walk the devinfo snapshot looking for host controller nodes */
	(void) di_walk_node(devtree, DI_WALK_CLDFIRST, &dcb,
	    usb_dev_walk_di_nodes);

	return (0);
}

/*
 * Use the parent's FRU as the FRU of host controllers enumerated from PCI
 */
int
usb_set_host_fru(topo_mod_t *mod, tnode_t *pnode, tnode_t *tn)
{
	int		err;
	nvlist_t	*fru = NULL;

	topo_mod_dprintf(mod, "usb_set_host_fru: %s", topo_node_name(tn));

	if (topo_node_fru(pnode, &fru, NULL, &err) != 0) {
		topo_mod_dprintf(mod, "usb_set_host_fru: can't find the"
		    " motherboard");

		return (-1);

	} else {
		(void) topo_node_fru_set(tn, fru, 0, &err);
		nvlist_free(fru);

		return (0);
	}
}

int
usb_process_host_controllers(topo_mod_t *mod, tnode_t *pnode,
    usb_enum_data_t *data)
{
	usb_enum_dev_t	*phost;
	int		i = 0;
	tnode_t		*rn;	/* root hub node */
	int		err = 0;
	nvlist_t	*pfmri = NULL;
	di_node_t	cdn;

	topo_mod_dprintf(mod, "usb_process_host_controllers: parent %s",
	    topo_node_name(pnode));

	if (topo_node_resource(pnode, &pfmri, &err) < 0) {
		topo_mod_dprintf(mod, "usb_process_host_controllers: "
		    "can't find the parent FMRI");
	} else {
		nvlist_free(pfmri);
		pfmri = NULL;
	}

	/* Enumerated from the host controller we gathered */
	for (phost = topo_list_next(&data->ue_devs); phost != NULL;
	    phost = topo_list_next(phost)) {
		/* If it is enumerated from PCI before, skip to next */
		if (phost->ued_enum) {
			topo_mod_dprintf(mod,
			    "usb_process_host_controllers: %s enumed before"
			    "goto next  parent =%s", phost->ued_name,
			    (phost->ued_pnode) ?
			    di_driver_name(phost->ued_pnode) : "no parent");

			continue;
		}

		topo_mod_dprintf(mod, "usb_process_host_controllers: %s, p=%s",
		    phost->ued_name,
		    (phost->ued_pnode) ? di_driver_name(phost->ued_pnode):
		    "no parent");

		/*
		 * we will not use the device instance here, because different
		 * host controller may have the same instance
		 */
		if ((rn = usb_tnode_create(mod, pnode, USB_BUS, i++,
		    phost->ued_dnode)) == NULL) {
			topo_mod_dprintf(mod, "usb_process_host_controllers:"
			"fail to create topo node for %s", phost->ued_name);

			return (-1);
		}

		/* Set it as enumed */
		phost->ued_enum = 1;

		(void) usb_populate_prop(mod, rn, phost->ued_dnode);

		cdn = di_child_node(phost->ued_dnode);
		if (cdn == DI_NODE_NIL) {

			continue;
		}

		/* create node range for devices under the root hub */
		if (topo_node_range_create(mod, rn, USB_DEV, 0,
		    MAX_USB_DEVS) < 0) {
			topo_mod_dprintf(mod, "usb_process_host_controllers:"
			    " can't create range");

			return (-1);
		}

		/* Also create node range for hubs under root hub */
		if (topo_node_range_create(mod, rn, USB_HUB, 0,
		    MAX_USB_DEVS) < 0) {
			topo_mod_dprintf(mod, "usb_process_host_controllers:"
			    " can't create range");

			return (-1);
		}

		/* walk and instantiate each child node of the root hub */
		if (usb_children_instantiate(mod, rn, cdn, 0) != 0) {
			topo_mod_dprintf(mod, "usb_process_host_controllers:"
			    " fail to instantiate children");

			return (-1);
		}
	}

	return (0);
}

int
usb_process_single_host(topo_mod_t *mod, tnode_t *pnode,
    usb_enum_data_t *data)
{
	tnode_t		*rn;	/* root hub node */
	int		err = 0;
	nvlist_t	*pfmri = NULL;
	di_node_t	cdn;
	di_node_t	pdn;
	usb_enum_dev_t	*host = NULL;

	topo_mod_dprintf(mod, "usb_process_single_host: parent %s",
	    topo_node_name(pnode));

	if (topo_node_resource(pnode, &pfmri, &err) < 0) {
		topo_mod_dprintf(mod, "usb_process_single_host: "
		    "can't find the parent FMRI");
	} else {
		nvlist_free(pfmri);
		pfmri = NULL;
	}

	/* Parent topo_node need provide di_node from its specific data */
	if ((pdn = topo_node_getspecific(pnode)) == DI_NODE_NIL) {
		topo_mod_dprintf(mod,
		    "Parent %s node missing private data.\n"
		    "Unable to proceed with %s enumeration.",
		    topo_node_name(pnode), USBTOPO);

		return (-1);
	}

	/* topo range create for pnode */
	if (topo_node_range_create(mod, pnode, USB_BUS, 0, MAX_USB_DEVS) < 0) {
		topo_mod_dprintf(mod, "usb_process_single_host: "
		    "failed to create range for %s", topo_node_name(pnode));

		return (-1);
	}

	/*
	 * we will not use the device instance here, because different
	 * host controller may have the same instance
	 */
	if ((rn = usb_tnode_create(mod, pnode, USB_BUS, 0,
	    pdn)) == NULL) {
		topo_mod_dprintf(mod, "usb_process_single_host:"
		    "fail to create topo node for %s", topo_node_name(pnode));

		return (-1);
	}

	/* Set topo property for the Host Controller */
	(void) usb_populate_prop(mod, rn, pdn);

	/*
	 * Default PCI enumeratedd before hostbridge enum, we set the host
	 * enumerated state here to avoid repeating enumeration in hostbridge
	 * enum.
	 */
	(void) usb_host_di_node_search(mod, pdn, data, &host);
	if (host != NULL) {
		host->ued_enum = 1;
	} else {
		topo_mod_dprintf(mod, "usb_process_single_host:"
		    "fail to search host node for %s",
		    topo_node_name(pnode));
	}

	/*
	 * Check if HC has children device. If so, enumerate the children
	 * to generate the topo
	 */
	cdn = di_child_node(pdn);
	if (cdn == DI_NODE_NIL) {

		return (0);
	}

	/* create node range for devices under the root hub */
	if (topo_node_range_create(mod, rn, USB_DEV, 0,
	    MAX_USB_DEVS) < 0) {
		topo_mod_dprintf(mod, "usb_process_host_controllers:"
		    " can't create range");

		return (-1);
	}

	/* Also create node range for hubs under root hub */
	if (topo_node_range_create(mod, rn, USB_HUB, 0,
	    MAX_USB_DEVS) < 0) {
		topo_mod_dprintf(mod, "usb_process_host_controllers:"
		    " can't create range");

		return (-1);
	}

	/* walk and instantiate each child node of the root hub */
	if (usb_children_instantiate(mod, rn, cdn, 0)
	    != 0) {
		topo_mod_dprintf(mod, "usb_process_host_controllers:"
		    " fail to instantiate children");

		return (-1);
	}

	return (0);
}

/*
 * USB topo enum entry
 */
/* ARGSUSED */
static int
usb_enum(topo_mod_t *mod, tnode_t *rnode, const char *name,
    topo_instance_t min, topo_instance_t max, void *arg, void *notused)
{
	usb_enum_data_t *data;
	char *rname;

	topo_mod_dprintf(mod, "usb_enum: %s %d %d nodename:%s ",
	    name, min, max, topo_node_name(rnode));

	/*
	 * Check to make sure we're being invoked sensibly, and that we're not
	 * being invoked as part of a post-processing step.
	 */
	if (strncmp(name, USB_BUS, 8) != 0) {

		return (0);
	}

	if ((data = topo_mod_getspecific(mod)) == NULL) {
		topo_mod_dprintf(mod, "usb_enum: specific private "
		    "data not set");

		return (-1);
	}

	/*
	 * Common enum start from PCI not xml. The rare case happens when the
	 * system has both on-chip pci devices and non-on-chip pci USB hosts.
	 * In this case, some of the host controllers will be enumerated by
	 * the parent PCI nodes, while others need to be enumerated by XML(from
	 * hostbridge). Please refer to *.xml under /usr/platform/'uname -i'/
	 * lib/fm/topo/maps
	 */
	rname = topo_node_name(rnode);
	topo_mod_dprintf(mod, "usb_enum: parent name %s", rname);

	if (strncmp(rname, HOSTBRIDGE, 16) == 0) {
		topo_mod_dprintf(mod, "usb_enum: enum from hostbridge."
		    " PCI has %s enumerated", usb_is_from_pci ?
		    "" : " NOT");

		/* Enum from PCI, here only one HC enumed each time */
		(void) usb_enum_from_hostbridge(mod, rnode, data);
	} else if (strncmp(rname, PCI_FUNCTION, 16) == 0 ||
	    strncmp(rname, PCIEX_FUNCTION, 16) == 0) {
		/* Enum from hostbridge, all HCs enumed together */
		topo_mod_dprintf(mod, "usb_enum: enum from pci");

		(void) usb_enum_from_pci(mod, rnode, name, min, max, arg, data);
	} else {
		topo_mod_dprintf(mod, "usb_enum: enum error from ",
		    "invalid parent %s", rname);

		return (-1);
	}

	return (0);
}

static void
usb_list_cleanup(topo_mod_t *mod, usb_enum_data_t *prvdata)
{
	topo_list_t *tpnode = NULL;
	topo_list_t *cpl;

	cpl = &prvdata->ue_devs;

	/*
	 * Travel through the ue_devs list. ue_devs is the header
	 * node, search starts from the next
	 */
	while ((tpnode = topo_list_next(cpl)) != NULL) {
		topo_list_delete(cpl, tpnode);
		topo_mod_free(mod, tpnode, sizeof (usb_enum_dev_t));
	}
}

static void
usb_release(topo_mod_t *mod, tnode_t *tn)
{
	topo_mod_dprintf(mod, "usb_release: entry  nodename:%s ",
	    topo_node_name(tn));

	topo_mod_dprintf(mod, "usb_release: end");
}

/* ARGSUSED */
static int
usb_enum_from_pci(topo_mod_t *mod, tnode_t *pnode,
    const char *name, topo_instance_t min, topo_instance_t max,
    void *arg, void *data)
{
	int		err = 0;

	topo_mod_dprintf(mod, "usb_enum_from_pci: %s %d %d nodename:%s ",
	    name, min, max, topo_node_name(pnode));

	usb_is_from_pci++;

	err = usb_process_single_host(mod, pnode, data);
	if (err != 0) {
		topo_mod_dprintf(mod, "usb_enum_from_pci: process host fail");
	}

	return (err);
}

int
usb_enum_from_hostbridge(topo_mod_t *mod, tnode_t *pnode,
    usb_enum_data_t *data)
{
	return (usb_process_host_controllers(mod, pnode, data));
}

/* ARGSUSED */
int
_topo_init(topo_mod_t *mod, topo_version_t version)
{
	usb_enum_data_t *data = NULL;
	/*
	 * Turn on module debugging output
	 */
	if (getenv("TOPOUSBDEBUG") != NULL)
		topo_mod_setdebug(mod);
	topo_mod_dprintf(mod, "_topo_init: "
	    "initializing %s enumerator", USBTOPO);

	if (topo_mod_register(mod, &usb_info, TOPO_VERSION) != 0) {
		topo_mod_dprintf(mod, "_topo_init: "
		    "%s registration failed: %s",
		    USBTOPO, topo_mod_errmsg(mod));

		return (-1);
	}

	if ((data = topo_mod_getspecific(mod)) == NULL) {
		topo_mod_dprintf(mod, "_topo_init:  %s enumerator alloc "
		    "specific private data", USBTOPO);
		if ((data = topo_mod_zalloc(mod, sizeof (usb_enum_data_t))) ==
		    NULL) {

			return (-1);
		}

		data->ue_mod = mod;
		topo_mod_setspecific(mod, data);

		/*
		 * Gather all host controllers in system
		 */
		if (usb_host_list_gather(mod, data) != 0) {
			usb_list_cleanup(mod, data);
			topo_mod_free(mod, data, sizeof (usb_enum_data_t));

			return (-1);
		}
		topo_mod_dprintf(mod, "_topo_init: %s enumerator usb host "
		    "controller first time gathered!", USBTOPO);
	}
	topo_mod_dprintf(mod, "_topo_init: "
	    "%s enumerator initialized", USBTOPO);

	return (0);
}

void
_topo_fini(topo_mod_t *mod)
{
	usb_enum_data_t *data = NULL;
	if ((data = topo_mod_getspecific(mod)) != NULL) {
		topo_mod_dprintf(mod, "_topo_fini: %s enumerator "
		    "free private data for mod", USBTOPO);
		usb_list_cleanup(mod, data);
		topo_mod_free(mod, data, sizeof (usb_enum_data_t));

		/* Avoid duplicate mem free */
		topo_mod_setspecific(mod, NULL);
	}

	topo_mod_unregister(mod);
	topo_mod_dprintf(mod, "_topo_fini: "
	    "%s enumerator uninitialized\n", USBTOPO);
}

static int
get_usb_vpid(topo_mod_t *mp, di_node_t dn, char *tname, char **serial,
    char **part)
{
	char	tmp[32];
	char	*s = NULL;
	int	*vid = NULL, *pid = NULL;
	di_node_t pdn;

	/*
	 * Get Part value in HC scheme
	 * Part is composed of Pid-Vid value pair
	 */

	if (strncmp(tname, USB_BUS, 8) == 0 ||
	    strncmp(tname, USB_HUB, 8) == 0) {
		if (di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "root-hub",
		    &vid) == 0) {
			topo_mod_dprintf(mp,
			    "get_usb_vpid: root-hub");

			if ((di_prop_lookup_ints(DDI_DEV_T_ANY, dn,
			    "vendor-id", &vid) < 0) &&
			    (usb_promprop2int(mp, dn, "vendor-id", &vid) < 0)) {
				topo_mod_dprintf(mp,
				    "get_usb_vpid: can't get host vid: %s\n");
			}

			if ((di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "device-id",
			    &pid) < 0) &&
			    (usb_promprop2int(mp, dn, "device-id", &pid) < 0)) {
				topo_mod_dprintf(mp,
				    "get_usb_vpid: can't get host pid\n");
			}
		} else {
			if (di_prop_lookup_ints(DDI_DEV_T_ANY, dn,
			    "usb-vendor-id", &vid) < 0) {
				topo_mod_dprintf(mp,
				    "get_usb_vpid: fail to get vid: %s\n");
			}
			if (di_prop_lookup_ints(DDI_DEV_T_ANY, dn,
			    "usb-product-id", &pid) < 0) {
				topo_mod_dprintf(mp,
				    "get_usb_vpid: fail to get pid\n");
			}
		}
	} else if (strncmp(tname, USB_DEV, 8) == 0) {
		if (di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "usb-vendor-id",
		    &vid) < 0) {
			topo_mod_dprintf(mp,
			    "get_usb_vpid: fail to get vid: %s\n");
		}
		if (di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "usb-product-id",
		    &pid) < 0) {
			topo_mod_dprintf(mp,
			    "get_usb_vpid: fail to get pid\n");
		}
	} else if (strncmp(tname, USB_IFC, 8) == 0) {
		pdn = di_parent_node(dn);

		if (di_prop_lookup_ints(DDI_DEV_T_ANY, pdn, "usb-vendor-id",
		    &vid) < 0) {
			topo_mod_dprintf(mp,
			    "get_usb_vpid: fail to get vid: %s\n");
		}

		if (di_prop_lookup_ints(DDI_DEV_T_ANY, pdn, "usb-product-id",
		    &pid) < 0) {
			topo_mod_dprintf(mp,
			    "get_usb_vpid: fail to get pid\n");
		}
	}

	bzero(tmp, sizeof (tmp));
	(void) snprintf(tmp, 20, "%04x-%04x\0", pid ? *pid : 0, vid ? *vid : 0);

	*part = topo_mod_strdup(mp, tmp);

	/*
	 * Get serial no from usb-vendor-id
	 */
	(void) di_prop_lookup_strings(DDI_DEV_T_ANY, dn,
	    "usb-serialno", &s);
	*serial = topo_mod_strdup(mp, s);

	return (0);
}

static tnode_t *
usb_tnode_create(topo_mod_t *mp, tnode_t *pn, char *name,
    topo_instance_t i, void *priv)
{
	tnode_t *ntn;
	nvlist_t *fmri;
	nvlist_t *auth;
	char *serial = NULL, *part = NULL;
	char *str;

	topo_mod_dprintf(mp, "usb_tnode_create entry\n");

	auth = topo_mod_auth(mp, pn);

	(void) get_usb_vpid(mp, priv, name, &serial, &part);

	topo_mod_dprintf(mp, "usb_tnode_create:serial=%s,part=%s \n",
	    serial?serial:"NULL", part?part:"NULL");

	fmri = topo_mod_hcfmri(mp, pn, FM_HC_SCHEME_VERSION, name,
	    i, NULL, auth, part, NULL, serial);

	nvlist_free(auth);
	topo_mod_strfree(mp, serial);
	topo_mod_strfree(mp, part);

	if (fmri != NULL) {
		(void) topo_mod_nvl2str(mp, fmri, &str);
		topo_mod_dprintf(mp, "usb_tnode_create new fmri: %s \n", str);
		topo_mod_strfree(mp, str);
	} else {
		topo_mod_dprintf(mp,
		    "Unable to make nvlist for %s bind.%s\n", name,
		    topo_mod_errmsg(mp));

		return (NULL);
	}

	/* Bind the fmri to topo_node */
	ntn = topo_node_bind(mp, pn, name, i, fmri);
	if (ntn == NULL) {
		topo_mod_dprintf(mp,
		    "topo_node_bind (%s%d/%s%d) failed for %s: %s\n",
		    topo_node_name(pn), topo_node_instance(pn), name, i,
		    di_node_name(priv), topo_strerror(topo_mod_errno(mp)));
		nvlist_free(fmri);

		return (NULL);
	}
	nvlist_free(fmri);
	topo_node_setspecific(ntn, priv);

	topo_mod_dprintf(mp, "usb_tnode_create end\n");

	return (ntn);
}

static int
usb_set_asru(topo_mod_t *mod, tnode_t *tn, di_node_t dn)
{
	char *devpath;
	nvlist_t *fmri;
	int e;

	devpath = di_devfs_path(dn);

	fmri = topo_mod_devfmri(mod, FM_DEV_SCHEME_VERSION, devpath, NULL);
	if (fmri == NULL) {
		topo_mod_dprintf(mod,
		    "usb_set_asru: fail to create dev scheme for %s: %s\n",
		    devpath, topo_strerror(topo_mod_errno(mod)));
		di_devfs_path_free(devpath);

		return (-1);
	}

	if (topo_node_asru_set(tn, fmri, 0, &e) < 0) {
		nvlist_free(fmri);
		topo_mod_dprintf(mod,
		    "usb_set_asru: fail to set ASRU for %s\n", devpath);

		di_devfs_path_free(devpath);

		return (topo_mod_seterrno(mod, e));
	}
	nvlist_free(fmri);
	di_devfs_path_free(devpath);

	return (0);
}

/* Set FRU */
int
usb_set_fru(topo_mod_t *mod, tnode_t *tn, di_node_t dn)
{
	int *vid;
	di_node_t pdn;
	tnode_t *ptn;
	nvlist_t *fmri;
	int ret = 0;

	topo_mod_dprintf(mod, "usb_set_fru: %s\n", topo_node_name(tn));
	/*
	 * if the dnode is bound to an interface, we have to find
	 * its parent device. The parent device can be set as FRU.
	 */
	pdn = dn;
	ptn = tn;
	do {
		ret = di_prop_lookup_ints(DDI_DEV_T_ANY, pdn,
		    "usb-vendor-id", &vid);
		if (ret > 0) {
		/* Only device node has "vendor-id" property */
			break;
		}

		pdn = di_parent_node(pdn);
		ptn = topo_node_parent(ptn);
	} while (ret <= 0);

	if (ret < 0) {
		topo_mod_dprintf(mod, "usb_set_fru: find device error\n");

		return (-1);
	}

	if (topo_node_resource(ptn, &fmri, &ret) < 0) {
		topo_mod_dprintf(mod, "usb_set_fru: resource error %s\n",
		    topo_strerror(ret));

		return (ret);
	}

	if (topo_node_fru_set(tn, fmri, 0, &ret) < 0) {
		topo_mod_dprintf(mod, "usb_set_fru: fru_set error %s\n",
		    topo_strerror(ret));
		nvlist_free(fmri);

		return (ret);
	}

	nvlist_free(fmri);

	return (0);
}

/*
 * some of the properties can only be retrieved from PROM,
 * specifically on SPARC provided by OBP.
 */
static int
usb_promprop2int(topo_mod_t *mod, di_node_t n, const char *propnm, int **val)
{
	di_prom_handle_t ptp = DI_PROM_HANDLE_NIL;
	di_prom_prop_t pp = DI_PROM_PROP_NIL;
	uchar_t *buf;

	if ((ptp = topo_mod_prominfo(mod)) == DI_PROM_HANDLE_NIL) {

		return (-1);
	}

	while ((pp = di_prom_prop_next(ptp, n, pp)) != DI_PROM_PROP_NIL) {
		if (strncmp(di_prom_prop_name(pp), propnm, 32) == 0) {
			if (di_prom_prop_data(pp, &buf) < sizeof (int)) {
				continue;
			}

			/* LINTED E_BAD_PTR_CAST_ALIGN */
			*val = (int *)buf;

			return (0);
		}
	}

	return (-1);
}

/*
 * set properties of a tnode
 * tn - the topology node
 * dn - the corresponding device node
 */
static int
usb_populate_prop(topo_mod_t *mod, tnode_t *tn, di_node_t dn)
{
	int *vid = NULL, *pid = NULL;
	char *drivname, *nodename;
	int instance;
	char *path;
	char str[32];
	char *model = NULL;
	char *vname = NULL, *pname = NULL, *serialno = NULL;
	int e;
	int isroot = 0;
	char vpstr[256]; /* vendor/product/serial name or no */

	topo_mod_dprintf(mod, "usb_populate_prop: %s\n", di_node_name(dn));

	if (topo_pgroup_create(tn, &io_pgroup, &e) < 0) {
		topo_mod_dprintf(mod,
		    "usb_populate_prop: fail to create io pgroup: %s\n",
		    topo_strerror(e));

		return (-1);
	}

	if (topo_pgroup_create(tn, &usb_pgroup, &e) < 0) {
		topo_mod_dprintf(mod,
		    "usb_populate_prop: fail to create usb pgroup: %s\n",
		    topo_strerror(e));

		return (-1);
	}

	if (di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "root-hub", &vid) == 0) {
		topo_mod_dprintf(mod,
		    "usb_populate_prop: root-hub \n");

		isroot = 1;
	}

	/* if this is an interface node, do not try to get vid/pid */
	if (di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "interface", &vid) > 0) {
		if (topo_prop_set_int32(tn, TOPO_PGROUP_USB, "interface",
		    TOPO_PROP_IMMUTABLE, topo_node_instance(tn), &e) < 0) {
			topo_mod_dprintf(mod, "usb_populate_prop: fail"
			    " to set interface,%s\n", topo_strerror(e));

			return (topo_mod_seterrno(mod, e));
		}

		goto skip_vpid;
	}

	if (isroot) {
		/*
		 * these properties are not present on SPARC
		 * and may be optional on X86
		 */

		if ((di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "vendor-id",
		    &vid) < 0) &&
		    (usb_promprop2int(mod, dn, "vendor-id", &vid) < 0)) {
			topo_mod_dprintf(mod,
			    "usb_populate_prop: can't get host vid: %s\n",
			    strerror(errno));

		}

		if ((di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "device-id",
		    &pid) < 0) &&
		    (usb_promprop2int(mod, dn, "device-id", &pid) < 0)) {
			topo_mod_dprintf(mod,
			    "usb_populate_prop: can't get host pid\n");

		}

		if (di_prop_lookup_strings(DDI_DEV_T_ANY, dn, "model",
		    &model) < 0) {
			topo_mod_dprintf(mod,
			    "usb_populate_prop: can't get host model\n");

		}
	} else {
		/* set usb group properties */
		if (di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "usb-vendor-id",
		    &vid) < 0) {
			topo_mod_dprintf(mod,
			    "usb_populate_prop: fail to get vid: %s\n",
			    strerror(errno));

			return (-1);
		}
		if (di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "usb-product-id",
		    &pid) < 0) {
			topo_mod_dprintf(mod,
			    "usb_populate_prop: fail to get pid\n");

			return (-1);
		}
		(void) di_prop_lookup_strings(DDI_DEV_T_ANY, dn,
		    "usb-vendor-name", &vname);
		(void) di_prop_lookup_strings(DDI_DEV_T_ANY, dn,
		    "usb-product-name", &pname);
		(void) di_prop_lookup_strings(DDI_DEV_T_ANY, dn,
		    "usb-serialno", &serialno);
	}

	if (vid) {
		(void) snprintf(str, 20, "%x", topo_node_instance(tn));
		if (topo_prop_set_string(tn, TOPO_PGROUP_USB, "label",
		    TOPO_PROP_IMMUTABLE, str, &e) < 0) {

			topo_mod_dprintf(mod, "usb_populate_prop: fail"
			    " to set parent-port,%s\n", topo_strerror(e));

			return (topo_mod_seterrno(mod, e));
		}

		(void) snprintf(str, 20, "%x", *vid);
		if (topo_prop_set_string(tn, TOPO_PGROUP_USB, "vendor-id",
		    TOPO_PROP_IMMUTABLE, str, &e) < 0) {
			topo_mod_dprintf(mod, "usb_populate_prop: fail"
			    " to set vid,%s\n", topo_strerror(e));

			return (topo_mod_seterrno(mod, e));
		}
	}

	if (pid) {
		(void) snprintf(str, 20, "%x", *pid);
		if (topo_prop_set_string(tn, TOPO_PGROUP_USB, "product-id",
		    TOPO_PROP_IMMUTABLE, str, &e) < 0) {
			topo_mod_dprintf(mod, "usb_populate_prop: fail to"
			    " set pid, %s\n", topo_strerror(e));

			return (topo_mod_seterrno(mod, e));
		}
	}

	/* vendor name possibly present only for USB devices */
	if (vname) {
		(void) snprintf(vpstr, 256, "%s", vname);
		if (topo_prop_set_string(tn, TOPO_PGROUP_USB, "vendor-name",
		    TOPO_PROP_IMMUTABLE, vpstr, &e) < 0) {
			topo_mod_dprintf(mod, "usb_populate_prop: fail to set"
			    " vname,%s\n", topo_strerror(e));

			return (topo_mod_seterrno(mod, e));
		}
	}

	/* product name possibly present only for USB devices */
	if (pname) {
		(void) snprintf(vpstr, 256, "%s", pname);
		if (topo_prop_set_string(tn, TOPO_PGROUP_USB, "product-name",
		    TOPO_PROP_IMMUTABLE, vpstr, &e) < 0) {
			topo_mod_dprintf(mod, "usb_populate_prop: fail to set"
			    " pname,%s\n", topo_strerror(e));

			return (topo_mod_seterrno(mod, e));
		}
	}

	/* serial no possibly present only for USB devices */
	if (serialno) {
		(void) snprintf(vpstr, 256, "%s", serialno);
		if (topo_prop_set_string(tn, TOPO_PGROUP_USB, "serial-number",
		    TOPO_PROP_IMMUTABLE, vpstr, &e) < 0) {
			topo_mod_dprintf(mod, "usb_populate_prop: fail to set"
			    " serial, %s\n", topo_strerror(e));

			return (topo_mod_seterrno(mod, e));
		}
	}

skip_vpid:
	/* set dev group properties */

	/* driver name */
	drivname = di_driver_name(dn);
	if (topo_prop_set_string(tn, TOPO_PGROUP_IO, "driver",
	    TOPO_PROP_IMMUTABLE, drivname, &e) < 0) {
		topo_mod_dprintf(mod,
		    "usb_populate_prop: fail to set driver, %s\n",
		    topo_strerror(e));

		return (topo_mod_seterrno(mod, e));
	}

	topo_mod_dprintf(mod, "usb_populate_prop: bus_addr, %s\n",
	    di_bus_addr(dn));

	instance = di_instance(dn);
	if (topo_prop_set_int32(tn, TOPO_PGROUP_IO, "instance",
	    TOPO_PROP_IMMUTABLE, instance, &e) < 0) {
		topo_mod_dprintf(mod,
		    "usb_populate_prop: fail to set instance, %s\n",
		    topo_strerror(e));

		return (topo_mod_seterrno(mod, e));
	}

	if (isroot) {
		if (model && (topo_prop_set_string(tn, TOPO_PGROUP_IO, "model",
		    TOPO_PROP_IMMUTABLE, model, &e) < 0)) {
			topo_mod_dprintf(mod,
			    "usb_populate_prop: fail to set devtype, %s\n",
			    topo_strerror(e));

			return (topo_mod_seterrno(mod, e));
		}
	} else {
		nodename = di_node_name(dn);
		if (topo_prop_set_string(tn, TOPO_PGROUP_IO, "devtype",
		    TOPO_PROP_IMMUTABLE, nodename, &e) < 0) {
			topo_mod_dprintf(mod,
			    "usb_populate_prop: fail to set devtype, %s\n",
			    topo_strerror(e));

			return (topo_mod_seterrno(mod, e));
		}
	}

	/* device path */
	path = di_devfs_path(dn);
	if (topo_prop_set_string(tn, TOPO_PGROUP_IO, "dev",
	    TOPO_PROP_IMMUTABLE, path, &e) < 0) {
		topo_mod_dprintf(mod,
		    "usb_populate_prop: fail to set dev path %s, %s\n",
		    path, topo_strerror(e));
		di_devfs_path_free(path);

		return (topo_mod_seterrno(mod, e));
	}
	di_devfs_path_free(path);

	if (!isroot) {
		/* set FRU */
		(void) usb_set_fru(mod, tn, dn);
	} else {
		tnode_t *pnode = topo_node_parent(tn);

		if (usb_set_host_fru(mod, pnode, tn) < 0) {
			topo_mod_dprintf(mod, "usb_populate_prop:"
			    "fail to set FRU for %s", drivname);

			return (-1);
		}
	}

	/* Set ASRU */
	(void) usb_set_asru(mod, tn, dn);

	topo_mod_dprintf(mod, "usb_populate_prop: %s end\n",
	    di_node_name(dn));

	return (0);
}

/*
 * pnode -- parent tnode
 * pdn -- the device node which we're to create tnode for
 */
static void
usb_declare_dev_and_if(topo_mod_t *mod, tnode_t *pnode, di_node_t pdn, int seq)
{
	tnode_t *ntn;
	di_node_t cdn;
	int i = 0, n;
	int *data;
	char *nodename;

	topo_mod_dprintf(mod, "usb_declare_dev_and_if: entry, %s\n",
	    di_node_name(pdn));

	/*
	 * skip scsa2usb children, the disk(sd), since
	 * they're not handled by USB stack.
	 */
	if (strncmp(di_node_name(pdn), "disk", 8) == 0) {
		topo_mod_dprintf(mod, "usb_declare_dev_and_if: skip %s\n",
		    di_node_name(pdn));

		return;
	}

	n = di_prop_lookup_ints(DDI_DEV_T_ANY, pdn, "reg", &data);

	if (n < 0) {
		topo_mod_dprintf(mod, "usb_declare_dev_and_if: can't find"
		    " reg for %s\n", di_node_name(pdn));

		return;
	}
	/* see the logic in common/io/usb/usba/usba.c */
	if ((n == 1) || ((n > 1) && (data[1] == 1))) {
		/*
		 * n=1, the port number
		 * n>1, data[0] is the interface # and data[1] is the cfg #
		 */
		seq = data[0];
	} else {
		/* Need verify if config is not 1 */
		topo_mod_dprintf(mod, "usb_declare_dev_and_if: interface "
		    "reg %d.%d", data[0], data[1]);

		seq = data[0];
	}

	/* if this is an interface node, do not try to get vid/pid */
	if (di_prop_lookup_ints(DDI_DEV_T_ANY, pdn, "interface", &data) > 0) {
		nodename = USB_IFC;
	} else if (di_prop_lookup_ints(DDI_DEV_T_ANY, pdn, "usb-port-count",
	    &data) > 0) {
		nodename = USB_HUB;
	} else {
		nodename = USB_DEV;
	}

	if ((ntn = usb_tnode_create(mod, pnode,
	    nodename, seq, pdn)) == NULL) {
		topo_mod_dprintf(mod, "dev_declare: can't create node");

		return;
	}

	(void) usb_populate_prop(mod, ntn, pdn);

	/*
	 * if we don't have child, return. Otherwise, process each child.
	 */
	cdn = di_child_node(pdn);
	if (cdn == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "%s don't have child devices\n",
		    di_node_name(pdn));

		return;
	}

	if (strncmp(nodename, USB_HUB, 8) == 0) {
		if (topo_node_range_create(mod, ntn, USB_HUB, 0,
		    MAX_USB_DEVS) < 0) {
			topo_mod_dprintf(mod, "child_range_add for"
			    "USB failed: %s\n",
			    topo_strerror(topo_mod_errno(mod)));

			return;
		}

		if (topo_node_range_create(mod, ntn, USB_DEV, 0,
		    MAX_USB_DEVS) < 0) {
			topo_mod_dprintf(mod, "child_range_add for"
			    "USB failed: %s\n",
			    topo_strerror(topo_mod_errno(mod)));

			return;
		}
	} else if (strncmp(nodename, USB_DEV, 8) == 0) {
		if (topo_node_range_create(mod, ntn, USB_IFC, 0,
		    MAX_USB_INTERFACE) < 0) {
			topo_mod_dprintf(mod, "child_range_add for"
			    "USB failed: %s\n",
			    topo_strerror(topo_mod_errno(mod)));

			return;
		}
	} else {
		/*
		 * there may be separate drivers/nodes for individual
		 * interface of this IA
		 */
		if (strncmp(di_driver_name(pdn), "usb_ia", 8) == 0) {
			if (topo_node_range_create(mod, ntn, USB_IFC, 0,
			    MAX_USB_INTERFACE) < 0) {
				topo_mod_dprintf(mod, "child_range_add for"
				    "USB failed: %s\n",
				    topo_strerror(topo_mod_errno(mod)));

				return;
			}
		}
	}

	/* instantiate each child sequencely */
	i = 0;
	cdn = di_child_node(pdn);
	if (cdn != DI_NODE_NIL) {
		topo_mod_dprintf(mod, "usb_declare_dev_and_if:"
		    " instantiate child %s\n", di_node_name(cdn));

		if (usb_children_instantiate(mod, ntn, cdn, i++) != 0) {
			topo_mod_dprintf(mod, "dev_declare: can't instantiate"
			    " further");
		}
	}

	topo_mod_dprintf(mod, "usb_declare_dev_and_if: %s end\n",
	    di_node_name(pdn));
}

/*
 * instantiate every child
 *
 * pnode - parent tnode
 */
static int
usb_children_instantiate(topo_mod_t *mod, tnode_t *pnode,
    di_node_t pn, int depth)
{
	di_node_t	cdn, tdn;

	topo_mod_dprintf(mod, "usb_children_instantiate: entry, process %s\n",
	    di_node_name(pn));

	if (di_prop_lookup_strings(DDI_DEV_T_ANY, pn, "root-hub", NULL) == 0) {
		/* do nothing */
		topo_mod_dprintf(mod,
		    "usb_children_instantiate: root hub %s\n",
		    di_node_name(pn));
	} else {
		topo_mod_dprintf(mod,
		    "usb_children_instantiate: declare %s\n",
		    di_node_name(pn));
	}

	cdn = pn;
	while (cdn != DI_NODE_NIL) {
		topo_mod_dprintf(mod, "usb_children_instantiate: child %s\n",
		    di_node_name(cdn));

		/* depth first */
		usb_declare_dev_and_if(mod, pnode, cdn, depth++);

		tdn = cdn;
		/* breadth next */
		cdn = di_sibling_node(cdn);

		topo_mod_dprintf(mod, "usb_children_instantiate: child %s, "
		    "sibling %s\n", di_node_name(tdn),
		    (cdn == DI_NODE_NIL)?"nosibling":di_node_name(cdn));
	}

	topo_mod_dprintf(mod, "usb_children_instantiate: %s end \n",
	    di_node_name(pn));

	return (0);
}

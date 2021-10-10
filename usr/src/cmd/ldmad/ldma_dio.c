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

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <alloca.h>
#include <sys/stat.h>
#include <malloc.h>
#include <fcntl.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <sys/pci.h>
#include <sys/mdesc.h>
#include <sys/mdesc_impl.h>
#include <sys/iovcfg.h>
#include <libdevinfo.h>
#include <sys/pci_param.h>
#include <sys/iov_param.h>
#include <libsysevent.h>
#include "libiov.h"
#include "ldma.h"
#include "mdesc_mutable.h"

extern void ldma_md_free(void *buf, size_t n);
extern void ldma_md_fini(void *md);

static int get_devinfo(uint8_t **mdpp, size_t *size);
static boolean_t is_root_complex(di_prom_handle_t ph, di_node_t di);
static md_node_t *link_device_node(mmd_t *mdp,
    di_prom_handle_t ph, di_node_t di, md_node_t *node, char *path);
static int create_children(mmd_t *mdp,
    di_prom_handle_t ph, md_node_t *node, di_node_t parent);
static int create_peers(mmd_t *mdp, di_prom_handle_t ph, md_node_t *md_parent,
    di_node_t di_node);
static int device_tree_to_md(mmd_t *mdp, md_node_t *top);
static int get_iovdev_info(uint8_t **mdpp, size_t *size);
static int iov_pflist_to_md(nvlist_t *nvlp, mmd_t *mdp, md_node_t *md_parent);
static int iov_create_iov_device_node(nvlist_t *nvlp, mmd_t *mdp,
    md_node_t *md_parentp);
static int iov_add_std_props(nvlist_t *nvlp, mmd_t *mdp, md_node_t *np);
static int iov_add_pci_props(nvlist_t *nvlp, mmd_t *mdp, md_node_t *np,
    md_node_t *iovmp);
static int iov_add_dev_props(iov_param_desc_t *pdp, mmd_t *mdp,
    md_node_t *nodep);
static int ldma_validate_iov_devprops(md_t *mdp, char **reasonp);
static int iov_validate_devprops(md_t *mdp, mde_cookie_t iovdevsp,
    char **reasonp);
static int iov_read_vf_devprops(md_t *mdp, mde_cookie_t nodep,
    nvlist_t **nvlpp);
static int iov_read_devprops(md_t *mdp, mde_cookie_t nodep, nvlist_t *nvlp);
static nvlist_t **iov_init_validate_nvlists(int num_vfs);
static int iov_get_validate_nvlist(nvlist_t **nvlpp, int index);
static int iov_add_nvlist(nvlist_t *nvlp, char *propname, data_type_t type,
    uint64_t val, char *str);
static char *pci_classcode_to_iovclass(uint_t class_code);

#define	PCIEX		"pciex"
#define	LDMA_MODULE	LDMA_NAME_DIO
#define	LDMA_IOV_VALIDATE_GEN_ERR	"Device Param Validation Failed: %d\n"
/*
 * MD Node names
 */
static char iov_devices_node[] = "iov-devices";
static char iov_dev_node[] = "iov-device";
static char iov_identity_node[] = "iov-device-identity";
static char iov_privprop_list_node[] = "iov-device-param-list";
static char iov_privprop_desc_node[] = "iov-device-param";
static char iov_privprops_node[] = "iov-device-private-props";

/*
 * Property names in nvlists
 */
static char nv_pflist_name[] = "pf_list";
static char nv_pfpath_name[] = "path";
static char nv_pfmgmt_name[] = "pf-mgmt-supported";
static char nv_ari_name[] = "ari-enabled";

/*
 * Property names in MD nodes
 */
static char iov_path_propname[] = "path";
static char iov_numvfs_propname[] = "numvfs";
static char iov_vfid_propname[] = "vf-id";
static char iov_ari_propname[] = "ari-enabled";
static char iov_pfmgmt_propname[] = "pf-mgmt-supported";
static char iov_class_propname[] = "config-class";

/*
 * IOV standard properties
 */
static char *iov_std_props[] =
{
	"#vfs",
	"initial-vfs",
	"total-vfs",
	"first-vf-offset",
	"vf-stride",
	"ari-enabled"
};

/*
 * Device specific property description names added in MD.
 */
static char iov_devprop_name[] = "name";
static char iov_devprop_description[] = "description";
static char iov_devprop_flag[] = "dev-flags";
static char iov_devprop_type[] = "data-type";
static char iov_devprop_minval[] = "min";
static char iov_devprop_maxval[] = "max";
static char iov_devprop_defval[] = "default";
static char iov_devprop_defstr[] = "default-string";

/*
 * Structure for props to be read from PROM dev tree.
 */
typedef struct promprops {
	enum { PROP_INT, PROP_STRING } prop_type;
	char *prop_name;
	char *alt_prop_name;
} promprops_t;

/*
 * PCI identity properties
 */
promprops_t pci_idprops[] = {
	{ PROP_STRING, "dev_path", NULL },
	{ PROP_STRING, "compatible", NULL },
	{ PROP_INT, "device-id", "real-device-id" },
	{ PROP_INT, "vendor-id", "real-vendor-id" },
	{ PROP_INT, "subsystem-id", "real-subsystem-id" },
	{ PROP_INT, "subsystem-vendor-id", "real-subsystem-vendor-id" },
	{ PROP_INT, "revision-id", "real-revision-id" },
	{ PROP_INT, "class-code", "real-class-code" }};

/* Device specific property flag values */
typedef enum devprop_flags {
	DEVPROP_FLAG_PF = 0x1,		/* devprop applies to PF */
	DEVPROP_FLAG_VF = 0x2,		/* devprop applies to VF */
	DEVPROP_FLAG_READONLY = 0x4	/* devprop is read only */
} devprop_flags_t;

/* Device specific property data types */
typedef enum devprop_data_type {
	DEVPROP_TYPE_INT8 = 0x1,
	DEVPROP_TYPE_UINT8 = 0x2,
	DEVPROP_TYPE_INT16 = 0x3,
	DEVPROP_TYPE_UINT16 = 0x4,
	DEVPROP_TYPE_INT32 = 0x5,
	DEVPROP_TYPE_UINT32 = 0x6,
	DEVPROP_TYPE_INT64 = 0x7,
	DEVPROP_TYPE_UINT64 = 0x8,
	DEVPROP_TYPE_STRING = 0x9
} devprop_data_type_t;

/*
 * PCI class and subclass masks
 */
#define	PCI_CLASS_CODE_MASK	0xFF00
#define	PCI_CLASS_CODE_SHIFT	8
#define	PCI_SUBCLASS_CODE_MASK	0xFF

/*
 * Structure to map standard pci class/subclass to basic iov device class.
 */
typedef struct pci2iov_class {
	uint8_t		class;			/* pci class */
	uint8_t		subclass;		/* pci subclass */
	char		*iovclass;		/* IOV class mapped to */
} pci2iov_class_t;

/*
 * Table for quick lookup and mapping of some of the standard pci classes and
 * subclasses to basic iov device classes. Keep the table limited to a few
 * commonly found device types for now.
 */
static pci2iov_class_t pci2iov_class[] = {
	{ PCI_CLASS_NET, PCI_NET_ENET, "iov-network"}
};

static char	iov_class_generic[] = "iov-generic";

/*
 * Max # of entries in pci2iov_class table
 */
#define	IOV_MAX_CLASS (sizeof (pci2iov_class) / sizeof (pci2iov_class_t))

/*
 * Direct IO agent versions supported:
 * 	1.0:	SDIO
 * 	1.1:	SRIOV
 */
static ds_ver_t ldma_dio_vers[] = { {1, 1}, {1, 0} };

static uint16_t	iov_ver_max = 1;
static uint16_t	iov_ver_min = 1;

#define	LDMA_DIO_NVERS	(sizeof (ldma_dio_vers) / sizeof (ds_ver_t))
#define	LDMA_DIO_NHANDLERS  (sizeof (ldma_dio_handlers) /		\
    sizeof (ldma_msg_handler_t))

static ldm_msg_func_t ldma_dio_pcidev_info_handler;
static ldm_msg_func_t ldma_dio_iovdev_info_handler;
static ldm_msg_func_t ldma_dio_iovdev_validate_handler;

static ldma_msg_handler_t ldma_dio_handlers[] = {
	{ MSGDIO_PCIDEV_INFO, LDMA_MSGFLG_ACCESS_CONTROL,
	    ldma_dio_pcidev_info_handler },
	{ MSGDIO_IOVDEV_INFO, LDMA_MSGFLG_ACCESS_CONTROL,
	    ldma_dio_iovdev_info_handler },
	{ MSGDIO_IOVDEV_CFG_VALIDATE, LDMA_MSGFLG_ACCESS_CONTROL,
	    ldma_dio_iovdev_validate_handler }
};

static ds_hdl_t dio_ds_hdl = DS_INVALID_HDL;

static void ldma_dio_init_cb(void);
static void ldma_dio_reg_cb(ds_ver_t *ver, ds_domain_hdl_t dhdl, ds_hdl_t hdl);
static void ldma_dio_unreg_cb(ds_hdl_t hdl);

ldma_agent_info_t ldma_dio_info = {
	LDMA_NAME_DIO,
	ldma_dio_vers, LDMA_DIO_NVERS,
	ldma_dio_init_cb, ldma_dio_reg_cb, ldma_dio_unreg_cb,
	ldma_dio_handlers, LDMA_DIO_NHANDLERS
};

/* ARGSUSED */
static ldma_request_status_t
ldma_dio_pcidev_info_handler(ds_ver_t *ver, ldma_message_header_t *request,
    size_t request_dlen, ldma_message_header_t **replyp, size_t *reply_dlenp)
{
	ldma_message_header_t *reply;
	char *data;
	uint8_t *md_bufp = NULL;
	size_t md_size;
	int rv;

	LDMA_DBG("%s: PCI device info request", __func__);
	rv  = get_devinfo(&md_bufp, &md_size);
	if (rv != 0) {
		LDMA_ERR("Failed to generate devinfo MD");
		return (LDMA_REQ_FAILED);
	}
	reply = ldma_alloc_result_msg(request, md_size);
	if (reply == NULL) {
		LDMA_ERR("Memory allocation failure");
		free(md_bufp);
		return (LDMA_REQ_FAILED);
	}

	reply->msg_info = md_size;
	data = LDMA_HDR2DATA(reply);
	(void) memcpy(data, md_bufp, md_size);
	*replyp = reply;
	*reply_dlenp = md_size;
	free(md_bufp);

	LDMA_DBG("%s: sending PCI device info", __func__);
	return (LDMA_REQ_COMPLETED);
}

/*
 * Obtain the list of PFs and return it in MD format to the client.
 */
/* ARGSUSED */
static ldma_request_status_t
ldma_dio_iovdev_info_handler(ds_ver_t *ver, ldma_message_header_t *request,
    size_t request_dlen, ldma_message_header_t **replyp, size_t *reply_dlenp)
{
	int			rv;
	ldma_message_header_t	*reply;
	char			*data;
	size_t			md_size;
	uint8_t			*md_bufp = NULL;

	LDMA_DBG("%s: SRIOV device info request", __func__);

	if (!VER_GTEQ(ver, iov_ver_max, iov_ver_min)) {
		LDMA_DBG("%s: Invalid Protocol Version (%d, %d) for SRIOV\n",
		    __func__, ver->major, ver->minor);
		return (LDMA_REQ_FAILED);

	}

	rv  = get_iovdev_info(&md_bufp, &md_size);
	if (rv != 0) {
		LDMA_ERR("Failed to generate iovdev info MD");
		return (LDMA_REQ_FAILED);
	}
	reply = ldma_alloc_result_msg(request, md_size);
	if (reply == NULL) {
		LDMA_ERR("Memory allocation failure");
		free(md_bufp);
		return (LDMA_REQ_FAILED);
	}

	reply->msg_info = md_size;
	data = LDMA_HDR2DATA(reply);
	(void) memcpy(data, md_bufp, md_size);
	*replyp = reply;
	*reply_dlenp = md_size;
	free(md_bufp);
	LDMA_DBG("%s: sending SRIOV device info", __func__);
	return (LDMA_REQ_COMPLETED);
}

/*
 * Validate the device specific props of the PF and its VFs, in the MD provided
 * by LDom Manager.
 */
/* ARGSUSED */
static ldma_request_status_t
ldma_dio_iovdev_validate_handler(ds_ver_t *ver, ldma_message_header_t *request,
    size_t request_dlen, ldma_message_header_t **replyp, size_t *reply_dlenp)
{
	int			rv;
	ldma_message_header_t	*reply;
	char			*data;
	size_t			dlen = 0;
	md_t			*mdp = NULL;
	char			*reasonp = NULL;
	char			generr[MAX_REASON_LEN + 1];

	LDMA_DBG("%s: SRIOV validate request", __func__);

	if (!VER_GTEQ(ver, iov_ver_max, iov_ver_min)) {
		LDMA_DBG("%s: Invalid Protocol Version (%d, %d) for SRIOV\n",
		    ver->major, ver->minor);
		return (LDMA_REQ_FAILED);
	}

	data = LDMA_HDR2DATA(request);
	mdp = md_init_intern((uint64_t *)data, malloc, ldma_md_free);
	if (mdp == NULL) {
		LDMA_ERR("Failed to scan validate MD");
		return (LDMA_REQ_FAILED);
	}

	rv = ldma_validate_iov_devprops(mdp, &reasonp);
	if (rv != 0) {
		LDMA_DBG("%s: Requested devprops are Invalid\n", __func__);
		if (reasonp != NULL && reasonp[0] != '\0') {
			dlen = strlen(reasonp) + 1;
		} else {
			(void) sprintf(generr, LDMA_IOV_VALIDATE_GEN_ERR, rv);
			dlen = strlen(generr) + 1;
		}
	} else {
		LDMA_DBG("%s: Requested devprops are Valid\n", __func__);
	}

	reply = ldma_alloc_result_msg(request, dlen);
	if (reply == NULL) {
		LDMA_ERR("Memory allocation failure");
		ldma_md_fini(mdp);
		return (LDMA_REQ_FAILED);
	}

	reply->msg_info = dlen;
	if (dlen > 0) {
		data = LDMA_HDR2DATA(reply);
		if (reasonp != NULL) {
			(void) strcpy(data, reasonp);
			free(reasonp);
		} else {
			(void) strcpy(data, generr);
		}
	}
	*replyp = reply;
	*reply_dlenp = dlen;
	ldma_md_fini(mdp);
	LDMA_DBG("%s: sending SRIOV validate response", __func__);
	return (LDMA_REQ_COMPLETED);
}

static boolean_t
is_root_complex(di_prom_handle_t ph, di_node_t di)
{
	int	len;
	char	*type;

	len = di_prom_prop_lookup_strings(ph, di, "device_type", &type);
	if ((len == 0) || (type == NULL))
		return (B_FALSE);

	if (strcmp(type, PCIEX) != 0)
		return (B_FALSE);

	/*
	 * A root complex node is directly under the root node.  So, if
	 * 'di' is not the root node, and its parent has no parent,
	 * then 'di' represents a root complex node.
	 */
	return ((di_parent_node(di) != DI_NODE_NIL) &&
	    (di_parent_node(di_parent_node(di)) == DI_NODE_NIL));
}

/*
 * String properties in the prom can contain multiple null-terminated
 * strings which are concatenated together.  We must represent them in
 * an MD as a data property.  This function retrieves such a property
 * and adds it to the MD.  If the 'alt_name' PROM property exists then
 * the MD property is created with the value of the PROM 'alt_name'
 * property, otherwise it is created with the value of the PROM 'name'
 * property.
 */
static int
add_prom_string_prop(di_prom_handle_t ph,
    mmd_t *mdp, md_node_t *np, di_node_t di, char *name, char *alt_name)
{
	int		count;
	char		*pp_data = NULL;
	char		*str;
	int		rv = 0;

	if (alt_name != NULL) {
		count = di_prom_prop_lookup_strings(ph, di, alt_name, &pp_data);
	}
	if (pp_data == NULL) {
		count = di_prom_prop_lookup_strings(ph, di, name, &pp_data);
	}

	if (count > 0 && pp_data != NULL) {
		for (str = pp_data; count > 0; str += strlen(str) + 1)
			count--;
		rv = md_add_data_property(mdp,
		    np, name, str - pp_data, (uint8_t *)pp_data);
	}
	return (rv);
}

/*
 * Add an int property 'name' to an MD from an existing PROM property. If
 * the 'alt_name' PROM property exists then the MD property is created with
 * the value of the PROM 'alt_name' property, otherwise it is created with
 * the value of the PROM 'name' property.
 */
static int
add_prom_int_prop(di_prom_handle_t ph,
    mmd_t *mdp, md_node_t *np, di_node_t di, char *name, char *alt_name)
{
	int		count;
	int		rv = 0;
	int		*pp_data = NULL;

	if (alt_name != NULL) {
		count = di_prom_prop_lookup_ints(ph, di, alt_name, &pp_data);
	}
	if (pp_data == NULL) {
		count = di_prom_prop_lookup_ints(ph, di, name, &pp_data);
	}

	/*
	 * Note: We know that the properties of interest contain a
	 * a single int.
	 */
	if (count > 0 && pp_data != NULL) {
		ASSERT(count == 1);
		rv = md_add_value_property(mdp, np, name, *pp_data);
	}
	return (rv);
}

/*
 * Create a new MD node and link it under the given parent MD node. Update the
 * props in the new MD node from its corresponding prom tree node.
 * Returns:
 * 	Success: Newly created MD node
 * 	Failure: NULL
 */
static md_node_t *
link_device_node(mmd_t *mdp,
    di_prom_handle_t ph, di_node_t di, md_node_t *md_parent, char *path)
{
	md_node_t	*np;
	int		i;

	np = md_link_new_node(mdp, "iodevice", md_parent, "fwd", "back");
	if (np == NULL)
		return (NULL);

	/* Add the properties from the devinfo node. */
	if (md_add_string_property(mdp, np, "dev_path", path) != 0)
		goto fail;
	for (i = 0; i < sizeof (pci_idprops) / sizeof (promprops_t); i++) {
		if (pci_idprops[i].prop_type == PROP_INT) {
			if (add_prom_int_prop(ph, mdp, np,
			    di, pci_idprops[i].prop_name,
			    pci_idprops[i].alt_prop_name) != 0)
				goto fail;
		} else {
			if (add_prom_string_prop(ph, mdp, np, di,
			    pci_idprops[i].prop_name,
			    pci_idprops[i].alt_prop_name) != 0)
				goto fail;
		}
	}
	return (np);

fail:
	md_free_node(mdp, np);
	return (NULL);
}

static int
create_children(mmd_t *mdp,
    di_prom_handle_t ph, md_node_t *md_parent, di_node_t di_node)
{
	md_node_t	*md_node;
	md_node_t	*md_child;
	di_node_t	di_child;
	char		*path;
	int		rv;

	path = di_devfs_path(di_node);
	if (path == NULL)
		return (EIO);

	/*
	 * Create a MD node corresponding to the given prom node (di_node)
	 */
	md_node = link_device_node(mdp, ph, di_node, md_parent, path);
	di_devfs_path_free(path);
	if (md_node == NULL) {
		return (ENOMEM);
	}

	/*
	 * Now discover the children of the given md_node/di_node.
	 */
	while ((di_child = di_child_node(di_node)) != DI_NODE_NIL) {
		path = di_devfs_path(di_child);
		if (path != NULL) {
			md_child = link_device_node(mdp,
			    ph, di_child, md_node, path);
			di_devfs_path_free(path);
			if (md_child == NULL) {
				return (ENOMEM);
			}
		}

		/*
		 * Discover peers of the newly created child node
		 */
		rv = create_peers(mdp, ph, md_node, di_child);
		if (rv != 0)
			return (rv);

		md_node = md_child;
		di_node = di_child;
	}
	return (0);
}

static int
create_peers(mmd_t *mdp, di_prom_handle_t ph, md_node_t *md_parent,
    di_node_t di_node)
{
	di_node_t	di_peer;
	int		rv;

	while ((di_peer = di_sibling_node(di_node)) != DI_NODE_NIL) {
		rv = create_children(mdp, ph, md_parent, di_peer);
		if (rv != 0)
			return (rv);
		di_node = di_peer;
	}
	return (0);
}

static int
device_tree_to_md(mmd_t *mdp, md_node_t *top)
{
	di_node_t		node;
	di_node_t		root;
	di_prom_handle_t	ph;
	int			rv = 0;

	root = di_init("/", DINFOSUBTREE | DINFOPROP);

	if (root == DI_NODE_NIL) {
		LDMA_ERR("di_init cannot find device tree root node.");
		return (errno);
	}

	ph = di_prom_init();
	if (ph == DI_PROM_HANDLE_NIL) {
		LDMA_ERR("di_prom_init failed.");
		di_fini(root);
		return (errno);
	}

	node = di_child_node(root);
	while (node != NULL) {
		if (is_root_complex(ph, node)) {
			rv = create_children(mdp, ph, top, node);
			if (rv != 0)
				break;
		}
		node = di_sibling_node(node);
	}

	di_prom_fini(ph);
	di_fini(root);
	return (rv);
}

static int
get_devinfo(uint8_t **mdpp, size_t *size)
{
	mmd_t		*mdp;
	md_node_t	*rootp;
	size_t		md_size;
	uint8_t		*md_bufp;

	mdp = md_new_md();
	if (mdp == NULL) {
		return (ENOMEM);
	}
	rootp = md_new_node(mdp, "root");
	if (rootp == NULL) {
		md_destroy(mdp);
		return (ENOMEM);
	}

	if (device_tree_to_md(mdp, rootp) != 0) {
		md_destroy(mdp);
		return (ENOMEM);
	}
	md_size = (int)md_gen_bin(mdp, &md_bufp);

	if (md_size == 0) {
		md_destroy(mdp);
		return (EIO);
	}
	*mdpp = md_bufp;
	*size = md_size;

	md_destroy(mdp);
	return (0);
}

/*
 * Get information about all the PFs in the root domain.
 */
static int
get_iovdev_info(uint8_t **mdpp, size_t *size)
{
	mmd_t		*mdp;
	md_node_t	*rootp;
	md_node_t	*md_nodep;
	size_t		md_size;
	uint8_t		*md_bufp;
	int		rv;
	nvlist_t	*nvlp = NULL;

	mdp = md_new_md();
	if (mdp == NULL) {
		rv = ENOMEM;
		goto cleanup;
	}

	/* Create the "root" node */
	rootp = md_new_node(mdp, "root");
	if (rootp == NULL) {
		rv = ENOMEM;
		goto cleanup;
	}

	/* Create an iov-devices node under the root node. */
	md_nodep = md_link_new_node(mdp, iov_devices_node, rootp,
	    "fwd", "back");
	if (md_nodep == NULL) {
		rv = EIO;
		goto cleanup;
	}

	/* Get the list of PFs */
	rv = iov_get_pf_list(&nvlp);
	if (rv != 0) {
		LDMA_DBG("%s: iov_get_pf_list() failed (%d)\n", __func__, rv);
		goto cleanup;
	}

	/* Generate a MD representing the PFs */
	rv = iov_pflist_to_md(nvlp, mdp, md_nodep);
	if (rv != 0) {
		goto cleanup;
	}

	/* Covert to binary MD format */
	md_size = (int)md_gen_bin(mdp, &md_bufp);
	if (md_size == 0) {
		rv = EIO;
		goto cleanup;
	}

	*mdpp = md_bufp;
	*size = md_size;
	rv = 0;

	LDMA_DBG("%s: Done\n", __func__);

cleanup:
	if (mdp != NULL) {
		md_destroy(mdp);
	}

	if (nvlp != NULL) {
		nvlist_free(nvlp);
	}

	return (rv);
}

/*
 * Given a list of PFs gathered from libiov, build an MD representing these PFs
 * to be returned back to LDom Manager. The nvlist consists of a single nvpair
 * with an array of nvlists as its value. Each nvlist in this array contains
 * the properties of the PF. The properties consist of standard IOV properties
 * (#vfs, initial_vfs...) and the PCI ID properties (Vendor ID, Device ID ...).
 * Returns:
 * 	Success: 0
 * 	Failure: errno
 */
static int
iov_pflist_to_md(nvlist_t *lp, mmd_t *mdp, md_node_t *md_parentp)
{
	int		i;
	int		rv;
	nvlist_t	**nvl_array;
	nvpair_t	*nvp;
	uint_t		nelem;

	if (nvlist_empty(lp)) {
		LDMA_DBG("%s: No PFs found in the nvlist\n", __func__);
		return (EINVAL);
	}

	rv = nvlist_lookup_nvpair(lp, nv_pflist_name, &nvp);
	if (rv != 0) {
		return (rv);
	}

	if (nvpair_type(nvp) != DATA_TYPE_NVLIST_ARRAY) {
		return (EINVAL);
	}

	rv = nvpair_value_nvlist_array(nvp, &nvl_array, &nelem);
	if (rv != 0) {
		return (rv);
	}

	if (nelem == 0) {
		/* iov_get_pf_list() succeeded, but there are no PFs ? */
		return (EINVAL);
	}

	for (i = 0; i < nelem; i++) {
		(void) iov_create_iov_device_node(nvl_array[i], mdp,
		    md_parentp);
	}

	return (0);
}

/*
 * Create an iov-device MD node for the PF specified by the nvlist and link it
 * under the parent iov-devices node. Read all its properties from the nvlist
 * and add them in the MD node.
 * Returns:
 * 	Success: 0
 * 	Failure: errno
 */
static int
iov_create_iov_device_node(nvlist_t *nvlp, mmd_t *mdp, md_node_t *md_parentp)
{
	md_node_t		*iovmp;
	md_node_t		*idprop;
	md_node_t		*devproplist;
	nvpair_t		*nvp;
	int			rv;
	int			i;
	uint32_t		version;
	uint32_t		num_params = 0;
	char			*pf_path = NULL;
	iov_param_desc_t	*pdp = NULL;
	boolean_t		pf_mgmt_supported = B_FALSE;

	/* Create an iov-device node under the parent node. */
	iovmp = md_link_new_node(mdp, iov_dev_node, md_parentp, "fwd", "back");
	if (iovmp == NULL) {
		return (ENOMEM);
	}

	/* Read the PF path property */
	rv = nvlist_lookup_nvpair(nvlp, nv_pfpath_name, &nvp);
	if (rv != 0) {
		goto cleanup;
	}
	rv = nvpair_value_string(nvp, &pf_path);
	if (rv != 0) {
		goto cleanup;
	}

	/* Add the path property to the iov-device node. */
	rv = md_add_string_property(mdp, iovmp, iov_path_propname, pf_path);
	if (rv != 0) {
		goto cleanup;
	}

	LDMA_DBG("Found PF (%s)\n", pf_path);

	/* add "pf-mgmt-supported" property only if its reported */
	rv = nvlist_lookup_nvpair(nvlp, nv_pfmgmt_name, &nvp);
	if (rv == 0) {
		pf_mgmt_supported = B_TRUE;
		rv = md_add_value_property(mdp, iovmp, iov_pfmgmt_propname, 1);
		if (rv != 0) {
			goto cleanup;
		}
	}

	/*
	 * Create an iovprops node node under the
	 * iov-device node and update its properties.
	 */
	idprop = md_link_new_node(mdp, iov_identity_node, iovmp,
	    "fwd", "back");
	if (idprop == NULL) {
		rv = ENOMEM;
		goto cleanup;
	}

	rv = iov_add_std_props(nvlp, mdp, idprop);
	if (rv != 0) {
		goto cleanup;
	}

	LDMA_DBG("Added std iov props to md (%s)\n", pf_path);

	rv = iov_add_pci_props(nvlp, mdp, idprop, iovmp);
	if (rv != 0) {
		goto cleanup;
	}

	LDMA_DBG("Added PCI id props to md (%s)\n", pf_path);

	/*
	 * Read device specific parameters for this PF from libiov,
	 * but only if the PF driver support is available.
	 */
	if (pf_mgmt_supported == B_TRUE) {
		rv = iov_devparam_get(pf_path, &version,
		    &num_params, (void **)&pdp);
		if (rv != 0) {
			/*
			 * We don't fail the iov-device creation
			 * if we can't get dev params.
			 */
			LDMA_DBG("%s: Get Dev Props for PF (%s) Failed (%d)\n",
			    __func__, pf_path, rv);
		}
	}

	/*
	 * Create a priv prop list node and update props.
	 */
	devproplist = md_link_new_node(mdp, iov_privprop_list_node, iovmp,
	    "fwd", "back");
	if (devproplist != NULL) {

		/* Add version property to the param list node */
		rv = md_add_value_property(mdp, devproplist, "version",
		    (uint64_t)version);
		if (rv != 0) {
			return (rv);
		}

		LDMA_DBG("%s: PF(%s) Number of Priv Props(%d)\n",
		    __func__, pf_path, num_params);

		for (i = 0; i < num_params; i++) {
			(void) iov_add_dev_props(&pdp[i], mdp, devproplist);
		}
		LDMA_DBG("Added dev props to md (%s)\n", pf_path);
	}

	/* Success */
	return (0);

cleanup:	/* Error */
	md_delete_node(mdp, iovmp);
	if (pf_path != NULL) {
		LDMA_DBG("Failed to create iov-device node for PF (%s)\n",
		    pf_path);
	}
	return (rv);
}

/*
 * Read standard IOV props from the nvlist and add them to the MD node.
 * Returns:
 * 	Success: 0
 * 	Failure: errno
 */
static int
iov_add_std_props(nvlist_t *nvlp, mmd_t *mdp, md_node_t *np)
{
	int		i;
	int		rv;
	char		*propname;
	nvpair_t	*nvp;
	uint64_t	val;
	int		nelem;

	nelem = sizeof (iov_std_props)/sizeof (char *);

	for (i = 0; i < nelem; i++) {

		propname = iov_std_props[i];

		if (strcmp(propname, nv_ari_name) == 0) {
			rv = nvlist_lookup_nvpair(nvlp, propname, &nvp);
			if (rv != 0)
				continue;
			rv = md_add_value_property(mdp, np, iov_ari_propname,
			    1);
			if (rv != 0) {
				return (rv);
			}
			continue;
		}

		/* Lookup the IOV prop by its name in the nvlist */
		rv = nvlist_lookup_nvpair(nvlp, propname, &nvp);
		if (rv != 0) {
			return (rv);
		}

		/* Read its value */
		rv = nvpair_value_uint64(nvp, &val);
		if (rv != 0) {
			return (rv);
		}

		/* Add the prop to the MD node */
		rv = md_add_value_property(mdp, np, propname, val);
		if (rv != 0) {
			return (rv);
		}
	}
	return (0);
}

/*
 * Read PCI ID props from the nvlist and add them to the MD node.
 * Returns:
 * 	Success: 0
 * 	Failure: errno
 */
static int
iov_add_pci_props(nvlist_t *nvlp, mmd_t *mdp, md_node_t *np, md_node_t *iovmp)
{
	int		i;
	int		rv;
	uint64_t	val;
	char		*str;
	char		*propname;
	nvpair_t	*nvp;
	data_type_t	nvp_type;
	int		nelem;
	char		*iovclass;

	nelem =  sizeof (pci_idprops) / sizeof (promprops_t);
	for (i = 0; i < nelem; i++) {

		propname = pci_idprops[i].prop_name;
#ifdef DBG_PROPS
		LDMA_DBG("%s: propname(%s)\n", __func__, propname);
#endif

		if (strcmp(propname, "dev_path") == 0 ||
		    strcmp(propname, "compatible") == 0) {
			/*
			 * No need to add compatible prop for IOV devices;
			 * device path has already been added to the parent MD
			 * node. Skip these props.
			 */
			continue;
		}

		/* Lookup the PCI prop by its name in the nvlist */
		rv = nvlist_lookup_nvpair(nvlp, propname, &nvp);
		if (rv != 0) {
			return (rv);
		}

		/* Determine prop type */
		nvp_type = nvpair_type(nvp);

		/* Read its value based on type and add the prop to MD node */
		switch (nvp_type) {
		case DATA_TYPE_UINT64:
			rv = nvpair_value_uint64(nvp, &val);
			if (rv != 0) {
				return (rv);
			}
#ifdef DBG_PROPS
			LDMA_DBG("%s: prop(%s), val(0x%llx)\n",
			    __func__, propname, val);
#endif
			rv = md_add_value_property(mdp, np, propname, val);
			if (rv != 0) {
				return (rv);
			}
			if (strcmp(propname, "class-code") == 0) {
				iovclass =
				    pci_classcode_to_iovclass((uint_t)val);
				ASSERT(iovclass != NULL);
#ifdef DBG_PROPS
			LDMA_DBG("%s: prop(%s), val(%s)\n",
			    __func__, iov_class_propname, iovclass);
#endif
				rv = md_add_string_property(mdp, iovmp,
				    iov_class_propname, iovclass);
				if (rv != 0) {
					return (rv);
				}
			}
			break;

		case DATA_TYPE_STRING:
			rv = nvpair_value_string(nvp, &str);
			if (rv != 0) {
				return (rv);
			}
#ifdef DBG_PROPS
			LDMA_DBG("%s: prop(%s), val(%s)\n",
			    __func__, propname, str);
#endif
			rv = md_add_string_property(mdp, np, propname, str);
			if (rv != 0) {
				return (rv);
			}
			break;

		default:
			return (EINVAL);

		}
	}

	return (0);
}

/*
 * Read device specific priv props from the param
 * description structure and add them to the MD.
 * Returns:
 * 	Success: 0
 * 	Failure: errno
 */
static int
iov_add_dev_props(iov_param_desc_t *pdp, mmd_t *mdp, md_node_t *nodep)
{
	int		rv;
	md_node_t	*np;
	uint64_t	type = 0;
	boolean_t	int_type = B_TRUE;

	/*
	 * We expect the prop name to show up correctly. Otherwise, we reject
	 * the devprop. We simply return success and ignore the devprop.
	 */
	if (pdp->pd_name == NULL || pdp->pd_name[0] == '\0') {
		return (0);
	}

	/*
	 * Create a priv prop description MD node;
	 * add all device specific props to it.
	 */
	np = md_link_new_node(mdp, iov_privprop_desc_node, nodep,
	    "fwd", "back");
	if (np == NULL) {
		return (ENOMEM);
	}

	/* Name of the dev prop */
	rv = md_add_string_property(mdp, np, iov_devprop_name,
	    pdp->pd_name);
	if (rv != 0) {
		goto cleanup;
	}

#ifdef DBG_PROPS
	LDMA_DBG("%s: Reading details of PrivProp (%s)\n",
	    __func__, pdp->pd_name);
#endif

	/* Description */
	rv = md_add_string_property(mdp, np, iov_devprop_description,
	    pdp->pd_desc);
	if (rv != 0) {
		goto cleanup;
	}

	rv = md_add_value_property(mdp, np, iov_devprop_flag,
	    pdp->pd_flag);
	if (rv != 0) {
		goto cleanup;
	}

	switch (pdp->pd_data_type) {
	case PCI_PARAM_DATA_TYPE_INT8:
		type = DEVPROP_TYPE_INT8;
		break;
	case PCI_PARAM_DATA_TYPE_UINT8:
		type = DEVPROP_TYPE_UINT8;
		break;
	case PCI_PARAM_DATA_TYPE_INT16:
		type = DEVPROP_TYPE_INT16;
		break;
	case PCI_PARAM_DATA_TYPE_UINT16:
		type = DEVPROP_TYPE_UINT16;
		break;
	case PCI_PARAM_DATA_TYPE_INT32:
		type = DEVPROP_TYPE_INT32;
		break;
	case PCI_PARAM_DATA_TYPE_UINT32:
		type = DEVPROP_TYPE_UINT32;
		break;
	case PCI_PARAM_DATA_TYPE_INT64:
		type = DEVPROP_TYPE_INT64;
		break;
	case PCI_PARAM_DATA_TYPE_UINT64:
		type = DEVPROP_TYPE_UINT64;
		break;
	case PCI_PARAM_DATA_TYPE_STRING:
		type = DEVPROP_TYPE_STRING;
		int_type = B_FALSE;
		break;
	default:
		LDMA_DBG("%s: Ignoring device prop(%s), datatype(%d)\n",
		    __func__, pdp->pd_name, pdp->pd_data_type);
		rv = EINVAL;
		goto cleanup;
	}

	if (int_type == B_TRUE) {
		/* Data type */
		rv = md_add_value_property(mdp, np, iov_devprop_type, type);
		if (rv != 0) {
			goto cleanup;
		}

		if ((pdp->pd_flag & DEVPROP_FLAG_READONLY) == 0) {
			/* Min/Max values are not relevant for RO params */

			/* Min val */
			rv = md_add_value_property(mdp, np, iov_devprop_minval,
			    pdp->pd_min64);
			if (rv != 0) {
				goto cleanup;
			}

			/* Max val */
			rv = md_add_value_property(mdp, np, iov_devprop_maxval,
			    pdp->pd_max64);
			if (rv != 0) {
				goto cleanup;
			}
		}

		/* Default val */
		rv = md_add_value_property(mdp, np, iov_devprop_defval,
		    pdp->pd_default_value);
		if (rv != 0) {
			goto cleanup;
		}
	} else {
		/* Data type */
		rv = md_add_value_property(mdp, np, iov_devprop_type, type);
		if (rv != 0) {
			goto cleanup;
		}
		LDMA_DBG("%s: \t\tProp (%s), Str(%s)\n",
		    __func__, pdp->pd_name, pdp->pd_default_string);
		rv = md_add_string_property(mdp, np, iov_devprop_defstr,
		    pdp->pd_default_string);
		if (rv != 0) {
			goto cleanup;
		}
	}
	return (0);

cleanup:
	md_delete_node(mdp, np);
	return (rv);

}

/*
 * Validate the IOV configuration specified in the given MD.
 * Returns:
 * 	Success: 0
 * 	Failure: errno
 */
static int
ldma_validate_iov_devprops(md_t *mdp, char **reasonp)
{
	mde_cookie_t	*nodelistp;
	mde_cookie_t	rootnode;
	mde_cookie_t	iovdevsp;
	int		num_nodes;
	boolean_t	rv;

	rootnode = md_root_node(mdp);

	num_nodes = md_node_count(mdp);
	nodelistp = malloc(num_nodes * sizeof (mde_cookie_t));
	if (nodelistp == NULL) {
		rv = ENOMEM;
		goto cleanup;
	}

	num_nodes = md_scan_dag(mdp, rootnode,
	    md_find_name(mdp, iov_devices_node),
	    md_find_name(mdp, "fwd"), nodelistp);

	/* We expect only 1 iov-devices node for now */
	ASSERT(num_nodes == 1);
	iovdevsp = nodelistp[0];

	rv = iov_validate_devprops(mdp, iovdevsp, reasonp);

cleanup:
	if (nodelistp != NULL) {
		free(nodelistp);
	}
	return (rv);
}

/*
 * Scan the given MD for the IOV device (PF) to be validated. Read its device
 * specific private properties passed for validation. Scan any VFs that are
 * configured and read their private properties. Package these properties into
 * nvlists and pass them to libiov for validation.
 * Returns:
 * 	Success: 0
 * 	Failure: errno
 */
static int
iov_validate_devprops(md_t *mdp, mde_cookie_t iovdevsp, char **reasonp)
{
	int		i;
	int		rv;
	int		num;
	uint64_t	numvfs;
	char		*pf_pathname;
	mde_cookie_t	nodep;
	nvlist_t	**nvlpp = NULL;
	mde_cookie_t	*nodes = NULL;
	int		no_priv_props = B_TRUE;

	nodes = malloc(sizeof (mde_cookie_t) * md_node_count(mdp));
	if (nodes == NULL) {
		return (ENOMEM);
	}

	/* Search for iov device nodes */
	num = md_scan_dag(mdp, iovdevsp, md_find_name(mdp, iov_dev_node),
	    md_find_name(mdp, "fwd"), nodes);

	if (num <= 0) {
		/* No iov-device node ? Bad config */
		rv = EINVAL;
		goto cleanup;
	}

	for (i = 0; i < num; i++) {

		nodep = nodes[i];

		/* Is this a PF ? */
		rv = md_get_prop_val(mdp, nodep, iov_numvfs_propname, &numvfs);
		if (rv != 0) {
			/*
			 * May be a VF; as md_scan_dag() would have
			 * found all iov device nodes, including VFs.
			 */
			rv = 0;
			continue;
		}

		/* Read PF path */
		rv = md_get_prop_str(mdp, nodep, iov_path_propname,
		    &pf_pathname);
		if (rv != 0) {
			goto cleanup;
		}

		/* Initialize nvlists area for validation */
		nvlpp = iov_init_validate_nvlists(numvfs);
		if (nvlpp == NULL) {
			rv = ENOMEM;
			goto cleanup;
		}

		LDMA_DBG("%s: Reading validate props for PF(%s)\n",
		    __func__, pf_pathname);

		/* Get the nvlist for PF */
		rv = iov_get_validate_nvlist(nvlpp, 0);
		if (rv != 0) {
			goto cleanup;
		}

		/* Read the priv props of PF and populate them in the nvlist */
		rv = iov_read_devprops(mdp, nodep, nvlpp[0]);
		if (rv != 0) {
			goto cleanup;
		}

		/* Scan for VFs and read their priv props */
		if (numvfs > 0) {
			rv = iov_read_vf_devprops(mdp, nodep, nvlpp);
			if (rv != 0) {
				goto cleanup;
			}
		}

		/*
		 * For now, we break out of the loop after finding a PF. We
		 * expect only 1 PF to be present in the MD sent by the LDom
		 * manager. Also, the libiov call (below) supports validating
		 * per PF.
		 */
		break;
	}

	/*
	 * Check if we have at least 1 non-empty nvlist; PF's nvlist is at
	 * index 0, followed by VFs' nvlists. Otherwise, there is nothing to
	 * validate, bail out.
	 */
	for (i = 0; i < numvfs + 1; i++) {
		if (nvlpp[i] != NULL) {
			if (!nvlist_empty(nvlpp[i])) {
				no_priv_props = B_FALSE;
				break;
			}
		}
	}
	if (no_priv_props == B_TRUE) {
		LDMA_DBG("%s: PF(%s): Empty Validation Request\n",
		    __func__, pf_pathname);
		rv = 0;
		goto cleanup;
	}

	/*
	 * Finished processing priv props of PF and/or VFs. We can now submit
	 * all these nvlists to the kernel for validation; tell the library.
	 */
	rv = iov_devparam_validate(pf_pathname, numvfs, reasonp, nvlpp);
	if (rv != 0) {
		LDMA_DBG("%s: Prop Validation Failed: error (%d) reason(%s)\n",
		    __func__, errno, *reasonp);
	}

cleanup:
	if (nodes != NULL) {
		free(nodes);
	}
	if (nvlpp != NULL) {
		for (i = 0; i < numvfs; i++) {
			if (nvlpp[i] != NULL) {
				nvlist_free(nvlpp[i]);
			}
		}
	}
	return (rv);
}

/*
 * Scan the MD for any VFs to be validated under the given PF. Read their priv
 * props and build the corresponding nvlists for validation.
 * Returns:
 * 	Success: 0
 * 	Failure: errno
 */
static int
iov_read_vf_devprops(md_t *mdp, mde_cookie_t pf_node, nvlist_t **nvlpp)
{
	int		i;
	char		*path;
	uint64_t	vfid;
	int		rv = 0;
	int		listsz;
	int		num_devs;
	int		num_nodes;
	mde_cookie_t	*listp;

	num_nodes = md_node_count(mdp);
	listsz = num_nodes * sizeof (mde_cookie_t);
	listp = (mde_cookie_t *)malloc(listsz);
	if (listp == NULL) {
		return (ENOMEM);
	}

	/* Search for all iov device nodes (VFs), under the PF */
	num_devs = md_scan_dag(mdp, pf_node,
	    md_find_name(mdp, iov_dev_node),
	    md_find_name(mdp, "fwd"), listp);
	if (num_devs <= 0) {
		/* No VF props to be validated */
		goto cleanup;
	}


	for (i = 0; i < num_devs; i++) {
		/*
		 * md_scan_dag() returns the same node if the subnode name
		 * happens to be the same; in this case as the given PF and its
		 * VFs are of the same node name, it returns the PF itself in
		 * the list. Skip that node.
		 */
		if (listp[i] == pf_node) {
			continue;
		}

		/* Is this a VF ? */
		rv = md_get_prop_val(mdp, listp[i], iov_vfid_propname, &vfid);
		if (rv != 0) {
			goto cleanup;
		}

		/* Read VF path */
		rv = md_get_prop_str(mdp, listp[i], iov_path_propname, &path);
		if (rv != 0) {
			goto cleanup;
		}

		LDMA_DBG("%s: Reading validate props for VF(%s) ID(0x%llx)\n",
		    __func__, path, vfid);

		/* Get the nvlist for this VF */
		rv = iov_get_validate_nvlist(nvlpp, vfid + 1);
		if (rv != 0) {
			goto cleanup;
		}

		/* Read the priv props of the VF and populate them in nvlist */
		rv = iov_read_devprops(mdp, listp[i], nvlpp[vfid + 1]);
		if (rv != 0) {
			LDMA_DBG("%s: VF(%s): Failed to read devprops (%d)\n",
			    __func__, path, rv);
			goto cleanup;
		}
	}

cleanup:
	if (listp != NULL) {
		free(listp);
	}
	return (rv);
}

/*
 * Scan the the given iov device node (PF or VF), for the priv props node under
 * it. If there is a priv props node, then read all the priv props specified
 * and their values. Update the nvlist provided with these props.
 * Returns:
 * 	Success: 0
 * 	Failure: errno
 */
static int
iov_read_devprops(md_t *mdp, mde_cookie_t nodep, nvlist_t *nvlp)
{
	char		prop[MAXNAMELEN];
	char		nextprop[MAXNAMELEN];
	uint64_t	nextval;
	int		i;
	int		rv;
	int		listsz;
	int		num_devs;
	int		num_arcs;
	int		num_nodes;
	uint8_t		type;
	mde_cookie_t	arcp;
	char		*nextstr;
	mde_cookie_t	*listp = NULL;
	char		*curprop = NULL;

	num_nodes = md_node_count(mdp);
	listsz = num_nodes * sizeof (mde_cookie_t);
	listp = (mde_cookie_t *)malloc(listsz);
	if (listp == NULL) {
		return (ENOMEM);
	}

	/* search for iov privprops sub node */
	num_devs = md_scan_dag(mdp, nodep,
	    md_find_name(mdp, iov_privprops_node),
	    md_find_name(mdp, "fwd"), listp);
	if (num_devs <= 0) {
		/* No privprops node under this iov device */
		rv = 0;
		goto cleanup;
	}

	/*
	 * When we scan for privprop nodes under a PF, we also get such nodes
	 * under any VFs that are present. This is due to the way md_scan_dag()
	 * works, as it returns all nodes of a given type in the entire dag
	 * starting at the given parent node. We read the "back" arc of the
	 * privprop nodes to eliminate privprop that are not of interest to us.
	 */
	for (i = 0; i < num_devs; i++) {
		num_arcs = md_get_prop_arcs(mdp, listp[i], "back",
		    &arcp, sizeof (arcp));
		if (num_arcs != 1) {
			/* We expect only 1 "back" arc */
#ifdef DBG_PROPS
			LDMA_DBG("%s: num_arcs(0x%x)\n",
			    __func__, num_arcs);
#endif
			rv = EIO;
			goto cleanup;
		}
#ifdef DBG_PROPS
		LDMA_DBG("%s: Node(0x%llx), ARC(0x%llx)\n",
		    __func__, nodep, arcp);
#endif
		if (arcp == nodep) {
			/* found the right child privprop node */
			break;
		}
		/* skip this privprop node and continue */
	}

	if (i == num_devs) {
		/* No privprops node directly under this iov-device */
		rv = 0;
		goto cleanup;
	}

	/*
	 * Read all type device specific props.
	 */
	while (md_get_next_prop_type(mdp, listp[i],
	    curprop, MAXNAMELEN, nextprop, &type) == 0) {
#ifdef DBG_PROPS
		LDMA_DBG("%s: \t\tProp (%s), Type(0x%x)\n",
		    __func__, nextprop, type);
#endif
		switch (type) {
		case MDET_PROP_VAL:
			rv = md_get_prop_val(mdp, listp[i], nextprop, &nextval);
			if (rv != 0) {
				LDMA_DBG("%s: md_get_prop_val() failed"
				    " (%d)\n", __func__, rv);
				goto cleanup;
			}
#ifdef DBG_PROPS
			LDMA_DBG("%s: \t\tProp (%s), Val(0x%llx)\n",
			    __func__, nextprop, nextval);
#endif
			/*
			 * Add the prop/val read from MD,
			 * to the nvlist, as an nvpair.
			 */
			rv = iov_add_nvlist(nvlp, nextprop, DATA_TYPE_UINT64,
			    (uint64_t)nextval, NULL);
			if (rv != 0) {
				LDMA_DBG("%s: iovcfg_add_nvlist() failed"
				    " (%d)\n", __func__, rv);
				goto cleanup;
			}
			break;
		case MDET_PROP_STR:
			rv = md_get_prop_str(mdp, listp[i], nextprop, &nextstr);
			if (rv != 0) {
				LDMA_DBG("%s: md_get_prop_str() failed"
				    " (%d)\n", __func__, rv);
				goto cleanup;
			}
#ifdef DBG_PROPS
			LDMA_DBG("%s: \t\tProp (%s), Str(0x%s)\n",
			    __func__, nextprop, nextstr);
#endif
			/*
			 * Add the prop/val read from MD,
			 * to the nvlist, as an nvpair.
			 */
			rv = iov_add_nvlist(nvlp, nextprop, DATA_TYPE_STRING,
			    0, nextstr);
			if (rv != 0) {
				LDMA_DBG("%s: iovcfg_add_nvlist() failed"
				    " (%d)\n", __func__, rv);
				goto cleanup;
			}
			break;

		case MDET_PROP_ARC:
			/* Ignore ARCs */
			break;

		default:
			rv = EINVAL;
			goto cleanup;
		}
		(void) strncpy(prop, nextprop, strlen(nextprop) + 1);
		curprop = prop;
	}

	LDMA_DBG("%s: iov-device-priv-props reading completed\n", __func__);

cleanup:
	if (listp != NULL) {
		free(listp);
	}
	return (rv);
}

/*
 * This function allocates memory to hold nvlists for device private property
 * validation. We then need to alloc nvlists for PF and each of the VFs to be
 * validated, that can used to add the props to be validated.
 */
static nvlist_t **
iov_init_validate_nvlists(int num_vfs)
{
	return ((nvlist_t **)calloc(num_vfs + 1, sizeof (nvlist_t *)));
}

/*
 * Allocate an nvlist at the given index in the array.
 */
static int
iov_get_validate_nvlist(nvlist_t **nvlpp, int index)
{
	nvlist_t **lp = &nvlpp[index];

	/* nvlist already exists at index ? */
	if (*lp != NULL) {
		return (0);
	}

	/* allocate a nvlist */
	if (nvlist_alloc(lp, NV_UNIQUE_NAME, 0) != 0) {
		*lp = NULL;
		return (ENOMEM);
	}

#ifdef DBG_PROPS
	LDMA_DBG("%s: allocated nvlist(0x%x) index(0x%x)\n",
	    __func__, (uintptr_t)(*lp), index);
#endif
	return (0);
}

/*
 * Add a prop/val pair to be validated to the nvlist.
 */
static int
iov_add_nvlist(nvlist_t *nvlp, char *propname, data_type_t type, uint64_t val,
    char *str)
{
	int	rv;
	char	string[MAXNAMELEN];

	if (nvlp == NULL || propname == NULL) {
		return (EINVAL);
	}
	switch (type) {
	case DATA_TYPE_UINT64:
		/*
		 * XXX: Until pcie_plist_lookup() is fixed to handle int types
		 * in the nvlist passed to the driver, cook up a string and
		 * pass it.
		 */
		(void) snprintf(string, sizeof (string), "%llu", val);
#if 0
		rv = nvlist_add_uint64(nvlp, propname, val);
#endif
		rv = nvlist_add_string(nvlp, propname, string);
		break;
	case DATA_TYPE_STRING:
		rv = nvlist_add_string(nvlp, propname, str);
		break;
	default:
		rv = EINVAL;
		break;
	}

	return (rv);
}

/*
 * Handler for sysevents about configuration changes.  Send an unsolicited
 * message to the client.  Client will request snapshot of the config
 * as needed.
 */
/* ARGSUSED */
static void
dio_sysevent_handler(sysevent_t *ev)
{
	ldma_message_header_t *msg;

	if (dio_ds_hdl == DS_INVALID_HDL)
		return;

	msg = ldma_alloc_msg(LDMA_MSGNUM_UNSOLICITED,
	    MSGDIO_STATE_CHANGE_EVT, 0);
	(void) ldma_send_unsolicited_msg(dio_ds_hdl,
	    msg, LDMA_MESSAGE_HEADER_SIZE);
}

static int
dio_init_sysevent(void)
{
	const char *subclasses[] = { ESC_DR_AP_STATE_CHANGE };
	sysevent_handle_t evh;

	evh = sysevent_bind_handle(dio_sysevent_handler);
	if (evh == NULL)
		return (-1);

	return (sysevent_subscribe_event(evh,
	    EC_DR, subclasses, sizeof (subclasses) / sizeof (char *)));
}

static void
ldma_dio_init_cb(void)
{
	if (dio_init_sysevent() != 0) {
		LDMA_ERR("DIO failed to subscribe to sysevents");
	}
}

/*
 * If the client is connecting from the control domain, store the
 * ds handle.  This allows the agent to send unsolicited messages
 * to that client.
 */
static void
ldma_dio_reg_cb(ds_ver_t *ver, ds_domain_hdl_t dhdl, ds_hdl_t hdl)
{
	if (VER_GTEQ(ver, iov_ver_max, iov_ver_min) &&
	    (dhdl == LDMA_CONTROL_DOMAIN_DHDL))
		dio_ds_hdl = hdl;
}

/* ARGSUSED */
static void
ldma_dio_unreg_cb(ds_hdl_t hdl)
{
	dio_ds_hdl = DS_INVALID_HDL;
}

static char *
pci_classcode_to_iovclass(uint_t class_code)
{
	uint8_t		cc;
	uint8_t		subcc;
	int		i;
	int		tblsz;
	pci2iov_class_t	*clp;

	cc = (class_code & PCI_CLASS_CODE_MASK)
	    >> PCI_CLASS_CODE_SHIFT;
	subcc = (class_code & PCI_SUBCLASS_CODE_MASK);

	tblsz = sizeof (pci2iov_class) / sizeof (pci2iov_class_t);

	for (i = 0; i < tblsz; i++) {
		clp = &pci2iov_class[i];
		if (clp->class == cc && clp->subclass == subcc) {
			return (clp->iovclass);
		}
	}
	return (iov_class_generic);
}

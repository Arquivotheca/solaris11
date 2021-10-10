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

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/sunndi.h>
#include <sys/mach_descrip.h>
#include <sys/iovcfg.h>
#include <sys/iovcfg_4v.h>

/* Internal functions */
static int iov_discover_pfs(void);
static int iov_discover_vfs(md_t *mdp, mde_cookie_t node, iov_pf_t *pfp);
static int iovcfg_read_priv_props(md_t *mdp, mde_cookie_t node,
    nvlist_t **nvlp);
static int iovcfg_read_class_props(iov_vf_t *vfp, md_t *mdp, mde_cookie_t node);

/*
 * Property names
 */
static char iovdev_propname[] = "iov-device";
static char numvfs_propname[] = "numvfs";
static char vfid_propname[] = "vf-id";
static char loaned_propname[] = "loaned";
static char iovdev_priv_propname[] = "iov-device-private-props";
static char iovdev_class_propname[] = "iov-device-class-props";

/* List of iov-device nodes */
extern iov_pf_t *iovcfg_pf_listp;

/*
 * Matching criteria passed to MDEG to register interest in changes to
 * 'iov-device-class-props' nodes identified by their 'name' property.
 */
static md_prop_match_t class_prop_match[] = {
	{ MDET_PROP_VAL, "primary-mac-addr" },
	{ MDET_LIST_END, NULL }
};

static mdeg_node_match_t class_node_match = { "iov-device-class-props",
	class_prop_match };

/*
 * Specification of an MD node passed to the MDEG to filter any class-props
 * nodes that do not belong to the specified node. This template is copied for
 * each VF iov-device instance and filled in with the appropriate 'path' value
 * before being passed to MDEG.
 */
static mdeg_prop_spec_t vf_prop_template[] = {
	{ MDET_PROP_VAL, "vf-id", NULL},
	{ MDET_PROP_STR, "path", NULL },
	{ MDET_LIST_END, NULL, NULL }
};

#define	IOV_SET_MDEG_VF_ID(specp, id)		((specp)[0].ps_val = (id))
#define	IOV_SET_MDEG_VF_PATH(specp, path)	((specp)[1].ps_str = (path))
#define	IOV_MDEG_VF_PATH(specp)			((specp)[1].ps_str)

/*
 * Scan the MD and build a list of PFs.
 */
int
iovcfg_plat_init(void)
{
	(void) iov_discover_pfs();
	return (0);
}

int
iovcfg_update_pflist(void)
{
	return (0);
}

/* ARGSUSED */
int
iovcfg_plat_refresh_vflist(iov_pf_t *pfp)
{
	return (0);
}

/*
 * Create platform specific part of VF.
 */
void
iovcfg_plat_alloc_vf(iov_vf_t *vfp)
{
	if (vfp)
		vfp->ivf_plat_data =
		    kmem_zalloc(sizeof (iov_vf_plat_t), KM_SLEEP);
}

/*
 * Destroy platform specific part of VF.
 */
void
iovcfg_plat_free_vf(iov_vf_t *vfp)
{
	if (vfp && vfp->ivf_plat_data != NULL) {
		kmem_free(vfp->ivf_plat_data, sizeof (iov_vf_plat_t));
		vfp->ivf_plat_data = NULL;
	}
}

/*
 * Register for platform specific refconfiguration updates.
 */
void
iovcfg_plat_config(iov_pf_t *pfp)
{
	if (pfp == NULL || pfp->ipf_cl_ops == NULL) {
		return;
	}
	pfp->ipf_cl_ops->iop_class_reconfig_reg_pf(pfp);
}

/*
 * Scan the guest machine description for devices of type 'iov-device'. If
 * the iov-device is a PF, create an instance of iov_pf_t and add it to the
 * list of PFs that are found. This list will be later searched when we need to
 * read its properties and update the corresponding dev_info node.
 * Returns:
 * 	Success:  0	- found PFs in the guest MD.
 * 	Failure:  EIO	- Error while processing MD.
 *		  ENODEV - No PFs in the guest MD.
 */
static int
iov_discover_pfs(void)
{
	md_t		*mdp = NULL;
	mde_cookie_t	rootnode;
	mde_cookie_t	*listp = NULL;
	iov_pf_t	*pfp;
	char		*path;
	char		*cl_str;
	uint64_t	numvfs;
	nvlist_t	*nvlp;
	int		i;
	int		rv = EIO;
	int		num_nodes = 0;
	int		num_devs = 0;
	int		listsz = 0;
	char		nv_string[MAX_NVSTRING_LEN];

	if ((mdp = md_get_handle()) == NULL) {
		return (rv);
	}

	num_nodes = md_node_count(mdp);
	ASSERT(num_nodes > 0);

	listsz = num_nodes * sizeof (mde_cookie_t);
	listp = (mde_cookie_t *)kmem_zalloc(listsz, KM_SLEEP);

	rootnode = md_root_node(mdp);
	num_devs = md_scan_dag(mdp, rootnode,
	    md_find_name(mdp, iovdev_propname),
	    md_find_name(mdp, "fwd"), listp);
	if (num_devs <= 0) {
		goto cleanup;
	}

	/*
	 * Now loop through the list of iov-devices looking for devices with
	 * the property "numvfs" and for each such device create a node and
	 * add it to the list of PF nodes.
	 */
	for (i = 0; i < num_devs; i++) {
		/*
		 * If the #vfs prop is present, this is a PF. Note that we
		 * add the PF to our list even if its #vfs is 0. This is
		 * because we need to be able to allow privprop configuration
		 * of the PF only, while no VFs are configured in it.
		 */
		if (md_get_prop_val(mdp, listp[i], numvfs_propname, &numvfs)
		    != 0)
			continue;

		/* Read path name */
		if (md_get_prop_str(mdp, listp[i], "path", &path) != 0)
			continue;

		/* Read device class */
		if (md_get_prop_str(mdp, listp[i], "config-class", &cl_str)
		    != 0)
			continue;
		DBG1("PF(%s) numvfs(%d)\n", path, (uint_t)numvfs);

		/*
		 * The node contains "numvfs" property;
		 * i.e, it is a PF; create a node for it.
		 */
		pfp = iovcfg_alloc_pf(cl_str, path);
		ASSERT(pfp != NULL);

		/*
		 * Read the device specific properties of the PF.
		 */
		rv = iovcfg_read_priv_props(mdp, listp[i], &nvlp);
		if (rv != 0) {
			/*
			 * We will skip futher processing of this
			 * PF (and its VFs); we destroy its pf device
			 * node and continue with the next PF.
			 */
			ASSERT(nvlp == NULL);
			cmn_err(CE_WARN,
			    "!PF(%s): failed to read PF device specific "
			    "properties rv=%d\n", path, rv);
			iovcfg_free_pf(pfp);
			continue;
		}

		/* Save PF priv props */
		ASSERT(nvlp != NULL);
		pfp->ipf_params = nvlp;

		/*
		 * Discover the VFs under the PF.
		 */
		rv = iov_discover_vfs(mdp, listp[i], pfp);
		if (rv != 0) {
			iovcfg_free_pf(pfp);
			continue;
		}

		/*
		 * Verify the numvfs read and discovered.
		 */
		if (pfp->ipf_numvfs != numvfs) {
			cmn_err(CE_WARN,
			    "!PF(%s): numvfs(%d) != discovered(%d)\n",
			    path, (uint_t)numvfs, pfp->ipf_numvfs);
		}
		(void) sprintf(nv_string, "0x%x", pfp->ipf_numvfs);
		rv = nvlist_add_string(nvlp, NUM_VF_NVLIST_NAME, nv_string);
		if (rv != 0) {
			cmn_err(CE_WARN,
			    "!PF(%s): could not add nv_string %s err=0x%x\n",
			    path, nv_string, rv);
		}
		/*
		 * Add the PF to the list of PFs.
		 */
		pfp->ipf_nextp = iovcfg_pf_listp;
		iovcfg_pf_listp = pfp;
	}

	if (iovcfg_pf_listp != NULL) {
		rv = 0;
	} else {
		rv = ENODEV;
	}

cleanup:
	if (listp != NULL) {
		kmem_free(listp, listsz);
	}
	(void) md_fini_handle(mdp);
	return (rv);
}

/*
 * Scan the MD for the VFs under the given PF MD node. For each VF node found,
 * read its device private properties and build a corresponding nvlist.
 */
static int
iov_discover_vfs(md_t *mdp, mde_cookie_t pf_node, iov_pf_t *pfp)
{
	int		i;
	int		rv = -1;
	int		num_nodes;
	int		listsz;
	int		num_devs;
	uint64_t	vfid;
	uint64_t	loaned;
	char		*path;
	iov_vf_t	*vfp;
	mde_cookie_t	*listp = NULL;
	nvlist_t	**vf_nvlist;

	num_nodes = md_node_count(mdp);
	/* Allocate node list for 'num_nodes' nodes */
	listsz = num_nodes * sizeof (mde_cookie_t);
	listp = (mde_cookie_t *)kmem_zalloc(listsz, KM_SLEEP);

	/* Search for all "iov-device" nodes (VFs), under the PF */
	num_devs = md_scan_dag(mdp, pf_node,
	    md_find_name(mdp, iovdev_propname),
	    md_find_name(mdp, "fwd"), listp);
	if (num_devs <= 0) {
		rv = 0;
		goto cleanup;
	}
	vf_nvlist = kmem_zalloc(num_devs * sizeof (void *), KM_SLEEP);

	DBG1("PF(%s), numvfs(%d), num_devs(%d)\n",
	    pfp->ipf_pathname, pfp->ipf_numvfs, num_devs);

	for (i = 0; i < num_devs; i++) {
		/*
		 * md_scan_dag() returns the same node if the subnode name
		 * happens to be the same; in this case as the given PF and its
		 * VFs are of the same node name "iov-device", it returns the
		 * PF itself in the list. Skip that node.
		 */
		if (listp[i] == pf_node) {
			continue;
		}

		/* Is this a "VF" iov device? */
		rv = md_get_prop_val(mdp, listp[i], vfid_propname, &vfid);
		if (rv != 0) {
			rv = ENOENT;
			goto cleanup;
		}

		if (vfid >= num_devs) {
			cmn_err(CE_WARN, "PF(%s): Invalid vfid=0x%lx\n",
			    pfp->ipf_pathname, vfid);
			rv = EINVAL;
			goto cleanup;
		}

		rv = md_get_prop_str(mdp, listp[i], "path", &path);
		if (rv != 0) {
			rv = ENOENT;
			goto cleanup;
		}

		vfp = iovcfg_alloc_vf(pfp, path, vfid);

		if (md_get_prop_val(mdp, listp[i], loaned_propname, &loaned)
		    == 0) {
			/*
			 * The property exists; this means the VF is loaned out
			 * and should not be used by the Root Domain.
			 */
			vfp->ivf_loaned = B_TRUE;
		}

		DBG1("VF(%s) VF-ID(%lx)\n", path, vfid);

		rv = iovcfg_read_priv_props(mdp, listp[i], &vf_nvlist[vfid]);
		if (rv != 0) {
			cmn_err(CE_WARN,
			    "!VF(%s) VF-ID(%lx): failed to read device "
			    "specific properties rv=%d\n", path, vfid, rv);
			goto cleanup;
		}

		ASSERT(vf_nvlist[vfid] != NULL);

		(void) iovcfg_read_class_props(vfp, mdp, listp[i]);

		/* Add the VF to the list */
		vfp->ivf_nextp = pfp->ipf_vfp;
		pfp->ipf_vfp = vfp;
		pfp->ipf_numvfs++;
	}
	rv = nvlist_add_nvlist_array(pfp->ipf_params,
	    VF_PARAM_NVLIST_ARRAY_NAME, vf_nvlist, pfp->ipf_numvfs);

cleanup:
	if (vf_nvlist != NULL) {
		for (i = 0; i < num_devs; i++) {
			if (vf_nvlist[i] != NULL)
				nvlist_free(vf_nvlist[i]);
		}
		kmem_free(vf_nvlist, num_devs * sizeof (void *));
	}
	if (listp != NULL) {
		kmem_free(listp, listsz);
	}
	return (rv);
}

/*
 * Given a MD node for a PF/VF, read all the device specific properties from
 * the MD. Create a corresponding nvlist of these properties and return it to
 * the caller.
 * Returns:
 * 	Success: 0 and the nvlist in nvlp. An empty nvlist returned
 *		 when no parameters found.
 * 	Failure: appropriate error value and NULL for nvlist.
 */
static int
iovcfg_read_priv_props(md_t *mdp, mde_cookie_t node, nvlist_t **nvlp)
{
	int		i;
	int		rv = 0;
	int		num_arcs;
	uint8_t		type;
	char		prop[MAXNAMELEN];
	char		nextprop[MAXNAMELEN];
	uint64_t	nextval;
	mde_cookie_t	arcp;
	char		*nextstr;
	nvlist_t	*lp = NULL;
	char		*curprop = NULL;
	mde_cookie_t	*listp = NULL;
	int		listsz = 0;
	int		num_devs = 0;
	int		num_nodes = 0;
	char		nv_string[MAX_NVSTRING_LEN];

	/* Allocate an nvlist to store the properties */
	if (nvlist_alloc(&lp, NV_UNIQUE_NAME, KM_SLEEP)) {
		*nvlp = NULL;
		return (EIO);
	}

	num_nodes = md_node_count(mdp);
	ASSERT(num_nodes > 0);

	listsz = num_nodes * sizeof (mde_cookie_t);
	listp = (mde_cookie_t *)kmem_zalloc(listsz, KM_SLEEP);

	/* search for "iov-device-priv-props" sub node */
	num_devs = md_scan_dag(mdp, node,
	    md_find_name(mdp, iovdev_priv_propname),
	    md_find_name(mdp, "fwd"), listp);
	if (num_devs <= 0) {
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
			rv = EIO;
			goto cleanup;
		}
		if (arcp == node) {
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
	 * Read all PROP_VAL type device specific props.
	 */
	while (md_get_next_prop_type(mdp, listp[i], curprop, MAXNAMELEN,
	    nextprop, &type) == 0) {
		switch (type) {
		case MDET_PROP_VAL:
			rv = md_get_prop_val(mdp, listp[i], nextprop, &nextval);
			if (rv != 0)
				goto cleanup;

			DBG1("Prop(%s) Val(%lx)\n", nextprop, nextval);
			/*
			 * Add the prop/val read from MD,
			 * to the nvlist, as an nvpair.
			 */
			(void) sprintf(nv_string, "0x%lx", nextval);
			rv = nvlist_add_string(lp, nextprop, nv_string);
			if (rv != 0)
				goto cleanup;
			break;

		case MDET_PROP_STR:
			rv = md_get_prop_str(mdp, listp[i], nextprop, &nextstr);
			if (rv != 0)
				goto cleanup;

			DBG1("Prop(%s) Str(%s)\n", nextprop, nextstr);
			/*
			 * Add the prop/val read from MD,
			 * to the nvlist, as an nvpair.
			 */
			rv = nvlist_add_string(lp, nextprop, nextstr);
			if (rv != 0)
				goto cleanup;
			break;

		case MDET_PROP_ARC:
			/* Ignore ARCs */
			break;

		default:
			/* Other types are not supported */
			DBG1("Prop(%s) - unsupported type(%d)\n",
			    nextprop, type);
			rv = ENOTSUP;
			goto cleanup;
		}

		/* Advance to next property */
		(void) strncpy(prop, nextprop, strlen(nextprop) + 1);
		curprop = prop;
	}

cleanup:
	if (listp != NULL) {
		kmem_free(listp, listsz);
	}
	if (rv != 0) {
		nvlist_free(lp);
		*nvlp = NULL;
	} else {
		/* Return the nvlist to the caller */
		*nvlp = lp;
	}
	return (rv);
}

/*
 * Given a MD node for a VF, read all the class specific properties.
 * Returns:
 * 	Success: 0
 * 	Failure: ENOENT - No class props
 */
static int
iovcfg_read_class_props(iov_vf_t *vfp, md_t *mdp, mde_cookie_t node)
{
	int		rv;
	mde_cookie_t	*listp = NULL;
	int		listsz;
	int		num_devs;
	int		num_nodes;
	iov_class_ops_t	*cl_ops;

	if ((cl_ops = vfp->ivf_pfp->ipf_cl_ops) == NULL) {
		return (ENOENT);
	}

	num_nodes = md_node_count(mdp);
	ASSERT(num_nodes > 0);

	listsz = num_nodes * sizeof (mde_cookie_t);
	listp = (mde_cookie_t *)kmem_zalloc(listsz, KM_SLEEP);

	/* search for "iov-device-class-props" sub node */
	num_devs = md_scan_dag(mdp, node,
	    md_find_name(mdp, iovdev_class_propname),
	    md_find_name(mdp, "fwd"), listp);
	if (num_devs <= 0)
		rv = ENOENT;
	else
		rv = cl_ops->iop_class_read_props(vfp, (void *)mdp,
		    (void *)listp[0]);

	kmem_free(listp, listsz);
	return (rv);
}

/*
 * Register an mdeg callback for the given VF.
 */
int
iovcfg_mdeg_register(iov_vf_t *vfp, mdeg_cb_t cb)
{
	mdeg_prop_spec_t	*pspecp;
	mdeg_node_spec_t	*nspecp;
	mdeg_handle_t		mdeg_hdl;
	size_t			templatesz;
	char			*path;
	size_t			pathsz;
	iov_vf_plat_t		*ivp;
	int			rv;
	int			(*mdeg_register_p)(mdeg_node_spec_t *,
	    mdeg_node_match_t *, mdeg_cb_t,
	    void *, mdeg_handle_t *) = NULL;
	if (vfp == NULL) {
		return (1);
	}
	/*
	 * We expect mdeg support to be loaded at this point.
	 * If not, dynamic VF configuration changes cannot supported
	 * load_iov_modules() executed during startup would have loaded
	 * platsvc module that contains mdeg routines when we enter here.
	 * However there could be situations where the platsvc module
	 * may not get loaded because of FW versions and use of modgetsymvalue
	 * permits us to determine this condition and proceed.
	 */
	mdeg_register_p = (int (*)(mdeg_node_spec_t *, mdeg_node_match_t *,
	    mdeg_cb_t, void *, mdeg_handle_t *))(modgetsymvalue(
	    "mdeg_register", 0));
	if (mdeg_register_p == NULL)
		return (1);
	ivp = (iov_vf_plat_t *)vfp->ivf_plat_data;

	/*
	 * Allocate and initialize a per instance copy of the prop spec array
	 * that will uniquely identify this VF instance.
	 */
	templatesz = sizeof (vf_prop_template);
	pspecp = kmem_zalloc(templatesz, KM_SLEEP);

	bcopy(vf_prop_template, pspecp, templatesz);

	/* copy in the path property */
	pathsz = strlen(vfp->ivf_pathname) + 1;
	path = kmem_alloc(pathsz, KM_SLEEP);

	bcopy(vfp->ivf_pathname, path, pathsz);
	IOV_SET_MDEG_VF_ID(pspecp, vfp->ivf_id);
	IOV_SET_MDEG_VF_PATH(pspecp, path);

	/* initialize the nodespec structure */
	nspecp = kmem_alloc(sizeof (mdeg_node_spec_t), KM_SLEEP);
	nspecp->namep = "iov-device";
	nspecp->specp = pspecp;

	/* Register with MDEG */
	rv = (*mdeg_register_p)(nspecp, &class_node_match, cb, vfp, &mdeg_hdl);
	if (rv != MDEG_SUCCESS) {
		cmn_err(CE_NOTE, "?%s: mdeg_register failed: VF(%s), rv(%d)\n",
		    __func__, vfp->ivf_pathname, rv);
		kmem_free(path, pathsz);
		kmem_free(pspecp, templatesz);
		kmem_free(nspecp, sizeof (mdeg_node_spec_t));
		return (1);
	}

	/* save off data that will be needed later */
	ivp->ivp_nspecp = nspecp;
	ivp->ivp_mdeg_hdl = mdeg_hdl;
	return (0);
}

/*
 * Unregister the mdeg callback handler for the given VF.
 */
void
iovcfg_mdeg_unreg(iov_vf_t *vfp)
{
	char		*path;
	iov_vf_plat_t	*ivp;
	void		(*mdeg_unregister_p)(mdeg_handle_t) = NULL;

	if (vfp == NULL)
		return;
	mdeg_unregister_p = (void(*)(mdeg_handle_t))(modgetsymvalue(
	    "mdeg_unregister", 0));
	if (mdeg_unregister_p == NULL)
		return;
	ivp = (iov_vf_plat_t *)vfp->ivf_plat_data;
	if (ivp == NULL)
		return;

	(void) (*mdeg_unregister_p)(ivp->ivp_mdeg_hdl);
	ivp->ivp_mdeg_hdl = NULL;
	/*
	 * Clean up cached MDEG data
	 */
	path = IOV_MDEG_VF_PATH(ivp->ivp_nspecp->specp);
	if (path != NULL) {
		kmem_free(path, strlen(path)+1);
	}
	kmem_free(ivp->ivp_nspecp->specp, sizeof (vf_prop_template));
	ivp->ivp_nspecp->specp = NULL;

	kmem_free(ivp->ivp_nspecp, sizeof (mdeg_node_spec_t));
	ivp->ivp_nspecp = NULL;
}

#ifdef IOVCFG_UNCONFIG_SUPPORTED

void
iovcfg_plat_fini(void)
{
	iovcfg_free_pfs();
}

/*
 * Unregister platform specific reconfiguration updates.
 */
void
iovcfg_plat_unconfig(iov_pf_t *pfp)
{
	if (pfp && pfp->ipf_cl_ops != NULL) {
		pfp->ipf_cl_ops->iop_class_reconfig_unreg_pf(pfp);
	}
}

#endif

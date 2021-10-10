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
#include <sys/inttypes.h>
#include <sys/stdbool.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/disp.h>
#include <sys/conf.h>
#include <sys/bootconf.h>
#include <sys/sysconf.h>
#include <sys/sunddi.h>
#include <sys/esunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/kmem.h>
#include <sys/vmem.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/hwconf.h>
#include <sys/modctl.h>
#include <sys/cmn_err.h>
#include <sys/kobj.h>
#include <sys/kobj_lex.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/autoconf.h>
#include <sys/callb.h>
#include <sys/sysmacros.h>
#include <sys/dacf.h>
#include <vm/seg_kmem.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/pathname.h>
#include <sys/ndi_impldefs.h>
#include <sys/pciconf.h>
#include <sys/pci_cap.h>
#include <sys/pci_impl.h>
#include <sys/iovcfg.h>
#include <sys/pcie_impl.h>
#include <sys/pci_cfgacc.h>
#include <sys/pci_param.h>
#include <sys/iov_param.h>

#ifdef DEBUG
extern int	pciconf_debug;
#endif

int	override_plat_config = 0;
void dump_nvlist(nvlist_t *, int);
void get_filter_func(char *, int (**f)(struct filter *, dev_info_t *));
void get_action_func(char *, int (**f)(struct action *actionp, dev_info_t *));

#define	PF_NVLIST_NAME_LEN	80

static int
add_pf_to_nvlist(dev_info_t *dip, void *arg)
{
	pcie_bus_t	*bus_p = NULL;
	char		*path = NULL;
	nvlist_t	*nvl;
	char		name[PF_NVLIST_NAME_LEN];
	nvlist_t	*pf_nvlist = NULL;
	int		rval;
	dev_info_t	*rcdip;
	uint16_t	sriov_cap_ptr, value;
	pcie_req_id_t	bdf = 0;
	uint32_t	numvfs, devid, venid, rev_id;
#if !defined(__sparc)
	uint32_t	dev_ven_id;
#endif

	nvl = (nvlist_t *)arg;
	bus_p =  PCIE_DIP2BUS(dip);
	if (bus_p == NULL)
		return (DDI_WALK_CONTINUE);
	if (bus_p && PCIE_IS_RC(bus_p))
		return (DDI_WALK_CONTINUE);
	rcdip = pcie_get_rc_dip(dip);
	if (pcie_cap_locate(dip,
	    PCI_CAP_XCFG_SPC(PCIE_EXT_CAP_ID_SRIOV), &sriov_cap_ptr) !=
	    DDI_SUCCESS)
		return (DDI_WALK_CONTINUE);
	if (pcie_get_bdf_from_dip(dip, &bdf) != DDI_SUCCESS)
		return (DDI_WALK_CONTINUE);
	(void) sprintf(name, "PF_%x_%x", DEVI(rcdip)->devi_instance, bdf);
	rval = nvlist_alloc(&pf_nvlist, NV_UNIQUE_NAME, 0);
	if (rval)
		return (DDI_WALK_CONTINUE);
	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	(void) pcie_pathname(dip, path);
	rval = nvlist_add_string(pf_nvlist, "path", path);
	value = pci_cfgacc_get16(rcdip, bdf,
	    (sriov_cap_ptr + PCIE_EXT_CAP_SRIOV_INITIAL_VFS_OFFSET));
	rval = nvlist_add_uint64(pf_nvlist, "initial-vfs",
	    (uint64_t)value);
	value = pci_cfgacc_get16(rcdip, bdf,
	    (sriov_cap_ptr + PCIE_EXT_CAP_SRIOV_TOTAL_VFS_OFFSET));
	rval = nvlist_add_uint64(pf_nvlist, "total-vfs",
	    (uint64_t)value);
	value = pci_cfgacc_get16(rcdip, bdf,
	    (sriov_cap_ptr + PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET));
	rval = nvlist_add_uint64(pf_nvlist, "first-vf-offset",
	    (uint64_t)value);
	value = pci_cfgacc_get16(rcdip, bdf,
	    (sriov_cap_ptr + PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET));
	rval = nvlist_add_uint64(pf_nvlist, "vf-stride",
	    (uint64_t)value);
	rval = nvlist_add_uint64(pf_nvlist, "subsystem-vendor-id",
	    (uint64_t)pci_cfgacc_get16(rcdip, bdf, PCI_CONF_SUBVENID));
	rval = nvlist_add_uint64(pf_nvlist, "subsystem-id",
	    (uint64_t)pci_cfgacc_get16(rcdip, bdf, PCI_CONF_SUBSYSID));
	rval = nvlist_add_uint64(pf_nvlist, "class-code",
	    (uint64_t)pci_cfgacc_get16(rcdip, bdf, PCI_CONF_SUBCLASS));
#if defined(__sparc)
	devid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device-id", -1);
	venid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "vendor-id", -1);
	rev_id = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "revision-id", -1);
	numvfs = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "#vfs", -1);
#else
	dev_ven_id = pci_cfgacc_get32(rcdip, bdf, PCI_CONF_VENID);
	venid = dev_ven_id & 0xffff;
	devid = (dev_ven_id >> 16);
	rev_id = pci_cfgacc_get8(rcdip, bdf, PCI_CONF_REVID);
	numvfs = 0;
#endif /* __sparc */
	rval = nvlist_add_uint64(pf_nvlist, "vendor-id",
	    (uint64_t)venid);
	rval = nvlist_add_uint64(pf_nvlist, "device-id",
	    (uint64_t)devid);
	rval = nvlist_add_uint64(pf_nvlist, "revision-id",
	    (uint64_t)rev_id);
	if (bus_p)
		numvfs = bus_p->num_vf;
	rval = nvlist_add_uint64(pf_nvlist, "#vfs",
	    (uint64_t)numvfs);
	if (bus_p == NULL)
		goto exit;
	/*
	 * Check if a IOV capable device driver is avaliable for this device
	 */
	if (bus_p->has_iov_capable_driver)
		rval = nvlist_add_boolean(pf_nvlist, "pf-mgmt-supported");
	if (bus_p->bus_ari)
		rval = nvlist_add_boolean(pf_nvlist, "ari-enabled");
exit:
	rval = nvlist_add_nvlist(nvl, name, pf_nvlist);
	nvlist_free(pf_nvlist);
	kmem_free(path, MAXPATHLEN);
	return (DDI_WALK_CONTINUE);
}

int
pcie_get_pf_list(nvlist_t **nvlp)
{
	int		rval;
	nvpair_t	*curr_nvp, *next_nvp;
	int		num_pfs, i;
	nvlist_t	**pf_list_nvlist;
	nvlist_t	*pf_nvlist;

	*nvlp = NULL;
	rval = nvlist_alloc(nvlp, NV_UNIQUE_NAME, 0);
	if (rval)
		return (rval);
	ddi_walk_devs(ddi_root_node(), add_pf_to_nvlist, (void *)*nvlp);
	/*
	 * first count the num of nvpairs
	 */
	num_pfs = 0;
	curr_nvp = NULL;
	while ((curr_nvp = nvlist_next_nvpair(*nvlp, curr_nvp))) {
		num_pfs++;
	}
	pf_list_nvlist = kmem_zalloc(num_pfs * sizeof (void *), KM_SLEEP);
	i = 0;
	curr_nvp = nvlist_next_nvpair(*nvlp, NULL);
	while (curr_nvp) {
		next_nvp = nvlist_next_nvpair(*nvlp, curr_nvp);
		rval = nvpair_value_nvlist(curr_nvp, &pf_nvlist);
		if (rval == 0)
			rval = nvlist_dup(pf_nvlist, &pf_list_nvlist[i++], 0);
		rval = nvlist_remove_all(*nvlp, nvpair_name(curr_nvp));
		curr_nvp = next_nvp;
	}
	rval = nvlist_add_nvlist_array(*nvlp, "pf_list", pf_list_nvlist,
	    num_pfs);
	for (i = 0; i < num_pfs; i++) {
		nvlist_free(pf_list_nvlist[i]);
	}
	kmem_free(pf_list_nvlist, num_pfs * sizeof (void *));
	return (rval);
}

static int
add_vf_to_nvlist(char *pf_path, void *arg)
{
	pcie_bus_t	*bus_p, *cdip_busp;
	char		*path = NULL;
	nvlist_t	*nvl;
	char		name[80];
	nvlist_t	*vf_nvlist = NULL;
	int		rval;
	dev_info_t	*rcdip;
	pcie_req_id_t	pf_bdf = 0;
	uint32_t	devid, venid;
	dev_info_t	*pf_dip, *cdip, *pdip;
	pcie_req_id_t	vf_bdf;

	nvl = (nvlist_t *)arg;
	pf_dip = pcie_find_dip_by_unit_addr(pf_path);
	if (pf_dip == NULL)
		return (DDI_FAILURE);
	bus_p =  PCIE_DIP2BUS(pf_dip);
	if (bus_p == NULL)
		return (DDI_FAILURE);
	rcdip = pcie_get_rc_dip(pf_dip);
	if (pcie_get_bdf_from_dip(pf_dip, &pf_bdf) != DDI_SUCCESS)
		return (DDI_FAILURE);
	pdip = ddi_get_parent(pf_dip);
	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	for (cdip = ddi_get_child(pdip); cdip;
	    cdip = ddi_get_next_sibling(cdip)) {
		cdip_busp = PCIE_DIP2UPBUS(cdip);
		if (!cdip_busp || (cdip_busp->bus_pf_dip != pf_dip))
			continue;
		vf_bdf = cdip_busp->bus_bdf;
		(void) sprintf(name, "VF_%x", vf_bdf);
		rval = nvlist_alloc(&vf_nvlist, NV_UNIQUE_NAME, 0);
		if (rval)
			return (DDI_FAILURE);
		(void) pcie_pathname(cdip, path);
		rval = nvlist_add_string(vf_nvlist, "path", path);
		rval = nvlist_add_uint64(vf_nvlist, "subsystem-vendor-id",
		    (uint64_t)pci_cfgacc_get16(rcdip, vf_bdf,
		    PCI_CONF_SUBVENID));
		rval = nvlist_add_uint64(vf_nvlist, "subsystem-id",
		    (uint64_t)pci_cfgacc_get16(rcdip, vf_bdf,
		    PCI_CONF_SUBSYSID));
		rval = nvlist_add_uint64(vf_nvlist, "class-code",
		    (uint64_t)pci_cfgacc_get16(rcdip, vf_bdf,
		    PCI_CONF_SUBCLASS));
		rval = nvlist_add_uint64(vf_nvlist, "revision-id",
		    (uint64_t)pci_cfgacc_get8(rcdip, vf_bdf,
		    PCI_CONF_REVID));
		venid = cdip_busp->bus_dev_ven_id & 0xffff;
		devid = (cdip_busp->bus_dev_ven_id >> 16);
		rval = nvlist_add_uint64(vf_nvlist, "vendor-id",
		    (uint64_t)venid);
		rval = nvlist_add_uint64(vf_nvlist, "device-id",
		    (uint64_t)devid);
		rval = nvlist_add_nvlist(nvl, name, vf_nvlist);
		nvlist_free(vf_nvlist);
	}
	if (path)
		kmem_free(path, MAXPATHLEN);
	return (DDI_SUCCESS);
}

int
pcie_get_vf_list(char *pf_path, nvlist_t **nvlp)
{
	int		rval;
	nvpair_t	*curr_nvp, *next_nvp;
	int		num_vfs, i;
	nvlist_t	**vf_list_nvlist;
	nvlist_t	*vf_nvlist;

	*nvlp = NULL;
	rval = nvlist_alloc(nvlp, NV_UNIQUE_NAME, 0);
	if (rval)
		return (rval);
	(void) add_vf_to_nvlist(pf_path, *nvlp);
	/*
	 * first count the num of nvpairs
	 */
	num_vfs = 0;
	curr_nvp = NULL;
	while ((curr_nvp = nvlist_next_nvpair(*nvlp, curr_nvp))) {
		num_vfs++;
	}
	vf_list_nvlist = kmem_zalloc(num_vfs * sizeof (void *), KM_SLEEP);
	i = 0;
	curr_nvp = nvlist_next_nvpair(*nvlp, NULL);
	while (curr_nvp) {
		next_nvp = nvlist_next_nvpair(*nvlp, curr_nvp);
		rval = nvpair_value_nvlist(curr_nvp, &vf_nvlist);
		if (rval == 0)
			rval = nvlist_dup(vf_nvlist, &vf_list_nvlist[i++], 0);
		rval = nvlist_remove_all(*nvlp, nvpair_name(curr_nvp));
		curr_nvp = next_nvp;
	}
	rval = nvlist_add_nvlist_array(*nvlp, "vf_list", vf_list_nvlist,
	    num_vfs);
	for (i = 0; i < num_vfs; i++) {
		nvlist_free(vf_list_nvlist[i]);
	}
	kmem_free(vf_list_nvlist, num_vfs * sizeof (void *));
	return (rval);
}

static int
add_vf_param_nvlist(dev_info_t *dip, nvlist_t *nvl)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	nvlist_t	**vf_nvlist, *vf_nvl;
	nvpair_t	*nvp;
	char		*nvp_name, *namep;
	int		i, vf_start, vf_end;
	int		rval;
	char		*path;

	if (bus_p->num_vf == 0)
		return (0);
	vf_nvlist = kmem_zalloc(bus_p->num_vf * sizeof (void *), KM_SLEEP);
	for (i = 0; i < bus_p->num_vf; i++)
		vf_nvlist[i] = NULL;
	path = kmem_zalloc(MAXPATHLEN + 1, KM_SLEEP);
	(void) pcie_pathname(dip, path);
	/*
	 * Scan the nvl looking for nvlists named VF[x] or VF[x,y]
	 */
	nvp = NULL;
	while (nvp = nvlist_next_nvpair(nvl, nvp)) {
		nvp_name = nvpair_name(nvp);
		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;
		namep = nvp_name;
		if (strncmp(VF_PREFIX, namep, strlen(VF_PREFIX)) != 0)
			continue;
		/*
		 * found a possible match
		 */
		vf_start = 0;
		namep += strlen(VF_PREFIX);
		if (!isdigit(*namep))
			continue;
		while (isdigit(*namep))
			vf_start = 10 * vf_start + (*namep++ - '0');
		vf_end = vf_start;
		if (*namep == '-') {
			vf_end = 0;
			namep++;
			if (!isdigit(*namep))
				continue;
			while (isdigit(*namep))
				vf_end = 10 * vf_end + (*namep++ - '0');

		}
		if ((*namep++ == ']') && (*namep == '\0')) {
			/*
			 * found a match
			 */
			PCICONF_DBG("VF start = %d VF end = %d PF is %s\n",
			    vf_start, vf_end, path);
			if (vf_start < 0) {
				cmn_err(CE_WARN,
				"vf_start in %s is less than 0. PF is %s\n",
				    nvp_name, path);
				continue;
			}
			if (vf_end >= bus_p->num_vf) {
				PCICONF_DBG("vf_end in %s exceeds num_vf of %d"
				" truncating vf_end to num_vf - 1. PF is %s\n",
				    nvp_name, bus_p->num_vf, path);
				vf_end = bus_p->num_vf - 1;
			}
			if (vf_start > vf_end) {
				PCICONF_DBG("vf_start is greater than vf_end"
				    " in %s PF is %s ignoring this\n",
				    nvp_name, path);
				continue;
			}
			for (i = vf_start; i <= vf_end; i++) {
				rval = nvpair_value_nvlist(nvp, &vf_nvl);
				rval = nvlist_dup(vf_nvl,
				    &vf_nvlist[i], 0);
				if (rval)
					cmn_err(CE_WARN,
					    "Error in creating nvlist for VF"
					    " %d of PF %s\n", i, path);

			}
		}
	}
	/*
	 * scan the vf_nvlist array and create a empty nvlist for the VF's
	 * that do not have a nvlist created.
	 */
	for (i = 0; i < bus_p->num_vf; i++) {
		if (vf_nvlist[i] != NULL)
			continue;
		rval = nvlist_alloc(&vf_nvlist[i], NV_UNIQUE_NAME, KM_SLEEP);
		if (rval)
			cmn_err(CE_WARN,
			    "Error in creating an empty nvlist for VF %d."
			    " PF is %s\n", i + 1, path);
	}
	rval = nvlist_add_nvlist_array(nvl,
	    VF_PARAM_NVLIST_ARRAY_NAME, vf_nvlist, bus_p->num_vf);
	if (rval)
		cmn_err(CE_WARN, "Error in adding %s for PF %s\n",
		    VF_PARAM_NVLIST_ARRAY_NAME, path);
	/*
	 * free up the nvlist in the vf_nvlist
	 */
	for (i = 0; i < bus_p->num_vf; i++) {
		(void) nvlist_free(vf_nvlist[i]);
	}
	kmem_free(vf_nvlist, bus_p->num_vf * sizeof (void *));
	kmem_free(path, MAXPATHLEN + 1);
	return (0);
}

int
pcie_param_get(char *path, nvlist_t **nvlp)
{
	struct filter	*filterp;
	struct config_data *config_datap;
	int	(*filter_func)(struct filter *filterp, dev_info_t *);
	int	filter_match = 0;
	dev_info_t	*dip;
#ifdef	DEBUG
	int	filter_found = 0;
	int	i;
	char	filter_names[256];
#endif

	PCICONF_DBG("pci_param_get:path is %s\n", path);
	ASSERT(nvlp != NULL);
	*nvlp = NULL;
	dip = pcie_find_dip_by_unit_addr(path);
	if (dip == NULL)
		return (DDI_FAILURE);
	(void) pciconf_parse_now(PCICONF_FILE);

	config_datap = pciconf_data_hd.config_datap[DEVICE_CONFIG];
	if (config_datap == NULL)
		return (1);
	for (; config_datap; config_datap = config_datap->next) {
		filterp = config_datap->filterp;
		if (!filterp)
			break;
		for (; filterp; filterp = filterp->next) {
#ifdef DEBUG
			filter_found = 1;
#endif
			/*
			 * match the filter against the dip
			 * If there is no match continue to next filter.
			 */
			get_filter_func(filterp->name, &filter_func);
			if (filter_func == NULL)
				continue;
			filter_match = (*filter_func)(filterp, dip);
			if (!filter_match)
				break;
		}
		if (filter_match)
			break;
	}
	/*
	 * If config_datap is NULL then a filter was found and it did
	 * not match this dip.
	 * so simply return;
	 */
	if (config_datap == NULL)
		return (1);
	/*
	 * We come here if no filter was found OR
	 * filter matched the dip.
	 */
#ifdef	DEBUG
	if (filter_found && pciconf_debug) {
		filter_names[0] = '\0';
		for (i = 0, filterp = config_datap->filterp; filterp;
		    filterp = filterp->next, i++) {
			if (i)
				(void) snprintf(filter_names,
				    sizeof (filter_names),
				    "%s AND", filter_names);
			(void) snprintf(filter_names, sizeof (filter_names),
			    "%s %s", filter_names, filterp->name);
		}
		PCICONF_DBG("filter_names %s matched for path %s\n",
		    filter_names,
		    path);
	}
#endif
	if (nvlist_dup(config_datap->datap,
	    nvlp, 0) != 0) {
		return (DDI_FAILURE);
	}
	(void) add_vf_param_nvlist(dip, *nvlp);
exit:
	return (DDI_SUCCESS);
}

int
pcie_get_numvfs(char *pf_path, uint_t *num_vfs_p)
{
	pcie_bus_t	*bus_p;
	dev_info_t	*dip;
	struct filter	*filterp;
	struct action	*actionp;
	struct config_data *config_datap;
	int	(*filter_func)(struct filter *filterp, dev_info_t *);
	int	(*action_func)(struct action *actionp, dev_info_t *);
	int	filter_match;
#ifdef	DEBUG
	int	filter_found = 0;
	int	i;
	char	filter_names[256];
#endif

	*num_vfs_p = 0;
	dip = pcie_find_dip_by_unit_addr(pf_path);
	if (dip == NULL)
		return (DDI_FAILURE);
	for (config_datap = pciconf_data_hd.
	    config_datap[SYSTEM_CONFIG]; config_datap;
	    config_datap = config_datap->next) {
		filterp = config_datap->filterp;
		actionp = (struct action *)config_datap->datap;
		for (; filterp; filterp = filterp->next) {
#ifdef DEBUG
			filter_found = 1;
#endif
			/*
			 * match the filter against the dip
			 * If there is no match continue to next filter.
			 */
			get_filter_func(filterp->name, &filter_func);
			if (filter_func == NULL)
				continue;
			filter_match = (*filter_func)(filterp, dip);
			if (!filter_match)
				break;
		}
		if (!filter_match)
			continue;
		/*
		 * We come here if no filter was specified or
		 * filter matched the dip.
		 */
#ifdef	DEBUG
		if (filter_found && pciconf_debug) {
			filter_names[0] = '\0';
			for (i = 0, filterp = config_datap->filterp; filterp;
			    filterp = filterp->next, i++) {
				if (i)
					(void) snprintf(filter_names,
					    sizeof (filter_names),
					    "%s AND", filter_names);
				(void) snprintf(filter_names,
				    sizeof (filter_names),
				    "%s %s", filter_names, filterp->name);
			}
			PCICONF_DBG("filter_names %s matched for path %s\n",
			    filter_names,
			    pf_path);
		}
#endif
		for (; actionp; actionp = actionp->next) {
			get_action_func(actionp->name, &action_func);
			if (action_func == NULL)
				continue;
			if ((strncmp(actionp->name, "num_vf_action",
			    strlen(actionp->name)) == 0))
				(void) (*action_func)(actionp, dip);
		}
	}
	bus_p = PCIE_DIP2BUS(dip);
	if (bus_p)
		*num_vfs_p = bus_p->num_vf;
	return (DDI_SUCCESS);
}

int
pci_param_get(dev_info_t *dip, pci_param_t *param_p)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	nvlist_t	*nvl;
	int		rval;
	char	*path = NULL;
	pci_param_hdl_t		*param_hdl = NULL;

	path = kmem_zalloc(MAXPATHLEN + 1, KM_SLEEP);
	(void) pcie_pathname(dip, path);
	PCICONF_DBG("pci_param_get:path is %s\n", path);
	if (bus_p && bus_p->sriov_cap_ptr && (!override_plat_config)) {
		/*
		 * The params will be obtained from MD
		 */
		rval = iovcfg_param_get(path, &nvl);
	} else {
		rval = pcie_param_get(path, &nvl);
	}
	if (rval)
		goto exit;
	param_hdl = kmem_zalloc(sizeof (pci_param_hdl_t), KM_SLEEP);
	param_hdl->dip = dip;
	param_hdl->plist = nvl;
	param_hdl->scratch_list = NULL;
#ifdef DEBUG
	if (pciconf_debug)
		dump_nvlist(nvl, 0);
#endif
exit:
	if (path)
		kmem_free(path, MAXPATHLEN + 1);
	*param_p = (void *)param_hdl;
	if (param_hdl)
		return (DDI_SUCCESS);
	return (DDI_FAILURE);
}

int
pci_plist_get(pci_param_t param, pci_plist_t *plist_p)
{
	if (param == NULL) {
		if (plist_p)
			*plist_p = NULL;
		return (DDI_FAILURE);
	}
	ASSERT(((pci_param_hdl_t *)param)->dip != NULL);
	ASSERT(plist_p != NULL);
	*plist_p = (void *)((((pci_param_hdl_t *)param))->plist);
	return (DDI_SUCCESS);
}

int
pci_plist_getvf(pci_param_t param, uint16_t vf_index, pci_plist_t *vflist_p)
{
	pcie_bus_t	*bus_p;
	nvlist_t	**vf_nvlist;
	int		rval;
	uint_t		nelem = 0;

	ASSERT(((pci_param_hdl_t *)param)->dip != NULL);
	ASSERT(vflist_p != NULL);
	*vflist_p = NULL;
	bus_p = PCIE_DIP2BUS(((pci_param_hdl_t *)param)->dip);
	if (bus_p->sriov_cap_ptr == NULL)
		return (DDI_ENOTSUP);
	rval = nvlist_lookup_nvlist_array((((pci_param_hdl_t *)param))->plist,
	    VF_PARAM_NVLIST_ARRAY_NAME, &vf_nvlist, &nelem);
	if (rval)
		return (DDI_FAILURE);
	if (vf_index < nelem)
		*vflist_p = (pci_plist_t)(vf_nvlist[vf_index]);
	return (DDI_SUCCESS);
}

int
pci_param_free(pci_param_t param)
{
	if (((pci_param_hdl_t *)param)->plist) {
		nvlist_free(((pci_param_hdl_t *)param)->plist);
	}
	if (((pci_param_hdl_t *)param)->scratch_list) {
		nvlist_free(((pci_param_hdl_t *)param)->scratch_list);
	}
	kmem_free(param, sizeof (pci_param_hdl_t));
	return (DDI_SUCCESS);
}

static int
add_array_to_plist(nvlist_t *nvl, const char *name, void *val, uint_t nelem,
    data_type_t type, void **new_val)
{
	int	rval;

	switch (type) {
	case DATA_TYPE_BYTE_ARRAY:
		rval = nvlist_add_byte_array(nvl, name, (uchar_t *)val,
		    nelem);
		if (rval == 0)
			rval = nvlist_lookup_byte_array(nvl, name,
			    (uchar_t **)(new_val), &nelem);
		break;
	case DATA_TYPE_INT8_ARRAY:
		rval = nvlist_add_int8_array(nvl, name, (int8_t *)val,
		    nelem);
		if (rval == 0)
			rval = nvlist_lookup_int8_array(nvl, name,
			    (int8_t **)(new_val), &nelem);
		break;
	case DATA_TYPE_UINT8_ARRAY:
		rval = nvlist_add_uint8_array(nvl, name, (uint8_t *)val,
		    nelem);
		if (rval == 0)
			rval = nvlist_lookup_uint8_array(nvl, name,
			    (uint8_t **)(new_val), &nelem);
		break;
	case DATA_TYPE_INT16_ARRAY:
		rval = nvlist_add_int16_array(nvl, name, (int16_t *)val,
		    nelem);
		if (rval == 0)
			rval = nvlist_lookup_int16_array(nvl, name,
			    (int16_t **)(new_val), &nelem);
		break;
	case DATA_TYPE_UINT16_ARRAY:
		rval = nvlist_add_uint16_array(nvl, name, (uint16_t *)val,
		    nelem);
		if (rval == 0)
			rval = nvlist_lookup_uint16_array(nvl, name,
			    (uint16_t **)(new_val), &nelem);
		break;
	case DATA_TYPE_INT32_ARRAY:
		rval = nvlist_add_int32_array(nvl, name, (int32_t *)val,
		    nelem);
		if (rval == 0)
			rval = nvlist_lookup_int32_array(nvl, name,
			    (int32_t **)(new_val), &nelem);
		break;
	case DATA_TYPE_UINT32_ARRAY:
		rval = nvlist_add_uint32_array(nvl, name, (uint32_t *)val,
		    nelem);
		if (rval == 0)
			rval = nvlist_lookup_uint32_array(nvl, name,
			    (uint32_t **)(new_val), &nelem);
		break;
	case DATA_TYPE_INT64_ARRAY:
		rval = nvlist_add_int64_array(nvl, name, (int64_t *)val,
		    nelem);
		if (rval == 0)
			rval = nvlist_lookup_int64_array(nvl, name,
			    (int64_t **)(new_val), &nelem);
		break;
	case DATA_TYPE_UINT64_ARRAY:
		rval = nvlist_add_uint64_array(nvl, name, (uint64_t *)val,
		    nelem);
		if (rval == 0)
			rval = nvlist_lookup_uint64_array(nvl, name,
			    (uint64_t **)(new_val), &nelem);
		break;
	}
	return (rval);

}

static void
get_array_name(char *array_name, char *curr_name, data_type_t type)
{
	switch (type) {
	case DATA_TYPE_BYTE_ARRAY:
		(void) sprintf(array_name, "%s_BYTE_ARRAY", curr_name);
		break;
	case DATA_TYPE_INT8_ARRAY:
		(void) sprintf(array_name, "%s_INT8_ARRAY", curr_name);
		break;
	case DATA_TYPE_UINT8_ARRAY:
		(void) sprintf(array_name, "%s_UINT8_ARRAY", curr_name);
		break;
	case DATA_TYPE_INT16_ARRAY:
		(void) sprintf(array_name, "%s_INT16_ARRAY", curr_name);
		break;
	case DATA_TYPE_UINT16_ARRAY:
		(void) sprintf(array_name, "%s_UINT16_ARRAY", curr_name);
		break;
	case DATA_TYPE_INT32_ARRAY:
		(void) sprintf(array_name, "%s_INT32_ARRAY", curr_name);
		break;
	case DATA_TYPE_UINT32_ARRAY:
		(void) sprintf(array_name, "%s_UNIT32_ARRAY", curr_name);
		break;
	case DATA_TYPE_INT64_ARRAY:
		(void) sprintf(array_name, "%s_INT64_ARRAY", curr_name);
		break;
	case DATA_TYPE_UINT64_ARRAY:
		(void) sprintf(array_name, "%s_UINT64_ARRAY", curr_name);
		break;
	}
}

static void
alloc_array(void **val, uint_t nelem, uint_t *array_size, data_type_t type)
{

	switch (type) {
	case DATA_TYPE_BYTE_ARRAY:
		*array_size = nelem * sizeof (uchar_t);
		break;
	case DATA_TYPE_INT8_ARRAY:
		*array_size = nelem * sizeof (int8_t);
		break;
	case DATA_TYPE_UINT8_ARRAY:
		*array_size = nelem * sizeof (uint8_t);
		break;
	case DATA_TYPE_INT16_ARRAY:
		*array_size = nelem * sizeof (int16_t);
		break;
	case DATA_TYPE_UINT16_ARRAY:
		*array_size = nelem * sizeof (uint16_t);
		break;
	case DATA_TYPE_INT32_ARRAY:
		*array_size = nelem * sizeof (int32_t);
		break;
	case DATA_TYPE_UINT32_ARRAY:
		*array_size = nelem * sizeof (uint32_t);
		break;
	case DATA_TYPE_INT64_ARRAY:
		*array_size = nelem * sizeof (int64_t);
		break;
	case DATA_TYPE_UINT64_ARRAY:
		*array_size = nelem * sizeof (uint64_t);
		break;
	}
	*val = kmem_alloc(*array_size, KM_SLEEP);

}

static int
string_to_int(char *string, void *val, data_type_t type)
{
	u_longlong_t	value;
	int	rval;

	rval = kobj_getvalue(string, &value);
	if (rval)
		return (rval);
	switch (type) {
	case DATA_TYPE_INT8:
	case DATA_TYPE_INT8_ARRAY:
		*((int8_t *)val) = value;
		break;
	case DATA_TYPE_BYTE:
	case DATA_TYPE_UINT8:
	case DATA_TYPE_UINT8_ARRAY:
		*((uint8_t *)val) = value;
		break;
	case DATA_TYPE_INT16:
	case DATA_TYPE_INT16_ARRAY:
		*((int16_t *)val) = value;
		break;
	case DATA_TYPE_UINT16:
	case DATA_TYPE_UINT16_ARRAY:
		*((uint16_t *)val) = value;
		break;
	case DATA_TYPE_INT32:
	case DATA_TYPE_INT32_ARRAY:
		*((int32_t *)val) = value;
		break;
	case DATA_TYPE_UINT32:
	case DATA_TYPE_UINT32_ARRAY:
		*((uint32_t *)val) = value;
		break;
	case DATA_TYPE_INT64:
	case DATA_TYPE_INT64_ARRAY:
		*((int64_t *)val) = value;
		break;
	case DATA_TYPE_UINT64:
	case DATA_TYPE_UINT64_ARRAY:
		*((uint64_t *)val) = value;
		break;
	}
	return (0);
}

static int
string_to_int_array(char **string_array, void *val, uint_t nelem,
    data_type_t type)
{
	u_longlong_t	value;
	int	rval, i;

	for (i = 0; i < nelem; i++) {
		if (type == DATA_TYPE_BYTE_ARRAY) {
			*((uchar_t *)val + i) = (uchar_t)(*string_array[i]);
			continue;
		}
		rval = kobj_getvalue(string_array[i], &value);
		if (rval)
			return (rval);
		switch (type) {
		case DATA_TYPE_INT8_ARRAY:
			*((int8_t *)val + i) = (int8_t)value;
			break;
		case DATA_TYPE_UINT8_ARRAY:
			*((uint8_t *)val + i) = (uint8_t)value;
			break;
		case DATA_TYPE_INT16_ARRAY:
			*((int16_t *)val + i) = (int16_t)value;
			break;
		case DATA_TYPE_UINT16_ARRAY:
			*((uint16_t *)val + i) = (uint16_t)value;
			break;
		case DATA_TYPE_INT32_ARRAY:
			*((int32_t *)val + i) = (int32_t)value;
			break;
		case DATA_TYPE_UINT32_ARRAY:
			*((uint32_t *)val + i) = (uint32_t)value;
			break;
		case DATA_TYPE_INT64_ARRAY:
			*((int64_t *)val + i) = value;
			break;
		case DATA_TYPE_UINT64_ARRAY:
			*((uint64_t *)val + i) = value;
			break;
		}
	}
	return (0);
}

int
pcie_plist_lookup(pci_plist_t plist, va_list ap)
{
	char		*name;
	int		ret = 0;
	data_type_t	type;
	void		*val;
	void		*val_array;
	uint_t		*nelemp, array_size;
	char		*value_string;
	char		**string_array;
	char		array_name[LABEL_LEN + 16];

	if (plist == NULL)
		return (DDI_EINVAL);
	while (((ret == 0) || (ret == ENOENT)) &&
	    (name = va_arg(ap, char *)) != NULL) {

		type = va_arg(ap, data_type_t);
		if (type != DATA_TYPE_BOOLEAN)
			val = va_arg(ap, void *);
		switch (type) {
		case DATA_TYPE_BOOLEAN:
		case DATA_TYPE_BYTE:
		case DATA_TYPE_INT8:
		case DATA_TYPE_UINT8:
		case DATA_TYPE_INT16:
		case DATA_TYPE_UINT16:
		case DATA_TYPE_INT32:
		case DATA_TYPE_UINT32:
		case DATA_TYPE_INT64:
		case DATA_TYPE_UINT64:
			ret = nvlist_lookup_string((nvlist_t *)plist,
			    name, &value_string);
#ifdef DEBUG
			if (pciconf_debug && (ret != 0) && (ret != ENOENT)) {
				cmn_err(CE_WARN,
				    "lookup string for %s failed. ret = 0x%x\n",
				    name, ret);
			}
#endif
			if (ret != 0)
				continue;
			if (type == DATA_TYPE_BOOLEAN) {
				continue;
			}
			ret = string_to_int(value_string, val, type);
			break;
		case DATA_TYPE_NVLIST:
		case DATA_TYPE_STRING:
			ret = nvlist_lookup_pairs((nvlist_t *)plist,
			    0,
			    name, type, val, NULL);
			break;

		case DATA_TYPE_BYTE_ARRAY:
		case DATA_TYPE_INT8_ARRAY:
		case DATA_TYPE_UINT8_ARRAY:
		case DATA_TYPE_INT16_ARRAY:
		case DATA_TYPE_UINT16_ARRAY:
		case DATA_TYPE_INT32_ARRAY:
		case DATA_TYPE_UINT32_ARRAY:
		case DATA_TYPE_INT64_ARRAY:
		case DATA_TYPE_UINT64_ARRAY:
			/*
			 * A comma separated values are treated as
			 * an array during parsing. If the user enters
			 * just one value for the array the parset will treat
			 * it as a simple string.
			 * Hence we will first lookup a string, if we do not
			 * find it then we will lookup string_array.
			 */
			val_array = NULL;
			nelemp = va_arg(ap, uint_t *);
			if (nelemp == NULL) {
				ret = DDI_EINVAL;
				break;
			}
			ret = nvlist_lookup_string((nvlist_t *)plist,
			    name, &value_string);
			if (ret == 0) {
				*nelemp = 1;
				alloc_array(&val_array, *nelemp, &array_size,
				    type);
				if (val_array == NULL)
					continue;
				ret = string_to_int(value_string, val_array,
				    type);
				if (ret == 0) {
					/*
					 * add the new array to the exiting
					 * nvlist
					 */
					get_array_name(array_name, name, type);
					ret = add_array_to_plist(
					    (nvlist_t *)plist, array_name,
					    val_array, *nelemp, type,
					    (void **)val);
				}
				kmem_free(val_array, array_size);
				break;
			}
			ret = nvlist_lookup_string_array((nvlist_t *)plist,
			    name, &string_array, nelemp);
#ifdef DEBUG
			if (pciconf_debug && (ret != 0) && (ret != ENOENT)) {
				cmn_err(CE_WARN,
			"lookup string_array for %s failed. ret = 0x%x\n",
				    name, ret);
			}
#endif
			if ((ret != 0) || (*nelemp == 0))
				continue;
			/*
			 * Allocate memory for array
			 */
			alloc_array(&val_array, *nelemp, &array_size, type);
			if (val_array == NULL)
				continue;
			ret = string_to_int_array(string_array, val_array,
			    *nelemp, type);
			if (ret == 0) {
				/*
				 * add the new array to the exiting nvlist
				 */
				get_array_name(array_name, name, type);
				ret = add_array_to_plist((nvlist_t *)plist,
				    array_name, val_array, *nelemp, type,
				    (void **)val);
			}
			kmem_free(val_array, array_size);
			break;
		case DATA_TYPE_STRING_ARRAY:
			nelemp = va_arg(ap, uint_t *);
			(*(void **)val) = NULL;
			ret = nvlist_lookup_string_array((nvlist_t *)plist,
			    name, (char ***)val, nelemp);
			break;
		case DATA_TYPE_NVLIST_ARRAY:
			nelemp = va_arg(ap, uint_t *);
			(*(void **)val) = NULL;
			ret = nvlist_lookup_nvlist_array((nvlist_t *)plist,
			    name, (nvlist_t ***)val, nelemp);
			break;
		default:
			ret = DDI_EINVAL;

		}

	}

	return (ret);
}

int
pci_plist_lookup(pci_plist_t plist, ...)
{
	va_list	alist;
	int	ret;

	va_start(alist, plist);
	ret = pcie_plist_lookup(plist, alist);
	va_end(alist);
	return (ret);
}

int
pci_plist_lookup_int8(pci_plist_t plist, const char *name, int8_t *val)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_INT8, val,
	    NULL));
}

int
pci_plist_lookup_int16(pci_plist_t plist, const char *name, int16_t *val)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_INT16, val,
	    NULL));
}

int
pci_plist_lookup_int32(pci_plist_t plist, const char *name, int32_t *val)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_INT32, val,
	    NULL));
}

int
pci_plist_lookup_int64(pci_plist_t plist, const char *name, int64_t *val)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_INT64, val,
	    NULL));
}

int
pci_plist_lookup_uint8(pci_plist_t plist, const char *name, uint8_t *val)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_UINT8, val,
	    NULL));
}

int
pci_plist_lookup_uint16(pci_plist_t plist, const char *name, uint16_t *val)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_UINT16, val,
	    NULL));
}

int
pci_plist_lookup_uint32(pci_plist_t plist, const char *name, uint32_t *val)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_UINT32, val,
	    NULL));
}

int
pci_plist_lookup_uint64(pci_plist_t plist, const char *name, uint64_t *val)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_UINT64, val,
	    NULL));
}

int
pci_plist_lookup_string(pci_plist_t plist, const char *name, char **val)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_STRING, val,
	    NULL));
}

int
pci_plist_lookup_plist(pci_plist_t plist, const char *name, pci_plist_t *val)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_PLIST, val,
	    NULL));
}

int
pci_plist_lookup_byte_array(pci_plist_t plist, const char *name,
    uchar_t **val, uint_t *nelem)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_BYTE_ARRAY,
	    val, nelem, NULL));
}

int
pci_plist_lookup_int8_array(pci_plist_t plist, const char *name,
    int8_t **val, uint_t *nelem)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_INT8_ARRAY,
	    val, nelem, NULL));
}

int
pci_plist_lookup_int16_array(pci_plist_t plist, const char *name,
    int16_t **val, uint_t *nelem)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_INT16_ARRAY,
	    val, nelem, NULL));
}

int
pci_plist_lookup_int32_array(pci_plist_t plist, const char *name,
    int32_t **val, uint_t *nelem)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_INT32_ARRAY,
	    val, nelem, NULL));
}

int
pci_plist_lookup_int64_array(pci_plist_t plist, const char *name,
    int64_t **val, uint_t *nelem)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_INT64_ARRAY,
	    val, nelem, NULL));
}

int
pci_plist_lookup_uint8_array(pci_plist_t plist, const char *name,
    uint8_t **val, uint_t *nelem)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_UINT8_ARRAY,
	    val, nelem, NULL));
}

int
pci_plist_lookup_uint16_array(pci_plist_t plist, const char *name,
    uint16_t **val, uint_t *nelem)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_UINT16_ARRAY,
	    val, nelem, NULL));
}

int
pci_plist_lookup_uint32_array(pci_plist_t plist, const char *name,
    uint32_t **val, uint_t *nelem)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_UINT32_ARRAY,
	    val, nelem, NULL));
}

int
pci_plist_lookup_uint64_array(pci_plist_t plist, const char *name,
    uint64_t **val, uint_t *nelem)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_UINT64_ARRAY,
	    val, nelem, NULL));
}

int
pci_plist_lookup_string_array(pci_plist_t plist, const char *name,
    char ***val, uint_t *nelem)
{
	return (pci_plist_lookup(plist, name, PCI_PARAM_DATA_TYPE_STRING_ARRAY,
	    val, nelem, NULL));
}

int
pci_param_get_ioctl(dev_info_t *dip, intptr_t ioctl_arg, int mode,
    pci_param_t *param_p)
{
	int			rval;
	nvlist_t		*nvl;
	pci_param_hdl_t		*param_hdl;
	char			null_string[1];
	char			*param_buf = NULL;
	int32_t			param_buflen;

	ASSERT(dip != NULL);
	ASSERT(param_p != NULL);
	*param_p = NULL;
	/*
	 * set the reason string to 0 length.
	 * This makes sure that we do not return a non zero
	 * reason string if the param validation succeeds.
	 */
	null_string[0] = '\0';
	(void) ddi_copyout(((iov_param_validate_t *)ioctl_arg)->pv_reason,
	    null_string, sizeof (null_string), mode);
	if (ddi_copyin(&((iov_param_validate_t *)ioctl_arg)->pv_buflen,
	    &param_buflen, sizeof (param_buflen), mode)
	    != DDI_SUCCESS)
		return (EFAULT);
	if (param_buflen == 0)
		return (DDI_EINVAL);
	param_buf = kmem_alloc(param_buflen, KM_SLEEP);
	if (ddi_copyin(((iov_param_validate_t *)ioctl_arg)->pv_buf,
	    param_buf, param_buflen, mode)
	    != DDI_SUCCESS) {
		rval = EFAULT;
		goto exit;
	}
	rval = nvlist_unpack(param_buf, (size_t)param_buflen, &nvl, 0);
	if (rval)
		goto exit;
	param_hdl = kmem_zalloc(sizeof (pci_param_hdl_t), KM_SLEEP);
	param_hdl->dip = dip;
	param_hdl->plist = nvl;
	param_hdl->scratch_list = NULL;
	*param_p = (pci_param_t)param_hdl;
	rval = DDI_SUCCESS;
exit:
	if (param_buf)
		kmem_free(param_buf, param_buflen);
	return (rval);
}

int
pcie_plist_getvf(nvlist_t *nvl, uint16_t vf_index, pci_plist_t *vflist_p)
{
	nvlist_t	**vf_nvlist;
	int		rval;
	uint_t		nelem = 0;

	ASSERT(vflist_p != NULL);
	*vflist_p = NULL;
	rval = nvlist_lookup_nvlist_array(nvl, VF_PARAM_NVLIST_ARRAY_NAME,
	    &vf_nvlist, &nelem);
	if (rval)
		return (rval);
	if (vf_index < nelem) {
		*vflist_p = (pci_plist_t)(vf_nvlist[vf_index]);
		return (DDI_SUCCESS);
	}
	return (DDI_FAILURE);
}

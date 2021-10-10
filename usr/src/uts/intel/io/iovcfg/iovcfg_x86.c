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
#include <sys/sunndi.h>
#include <sys/iovcfg.h>

/* Internal functions */
static int iov_discover_pfs(void);
static int iov_discover_vfs(iov_pf_t *);
static int iovcfg_read_class_props(iov_vf_t *vfp);
static void dump_nvlist(nvlist_t *nvl, int depth);

/* List of iov-device nodes */
extern struct iov_pf *iovcfg_pf_listp;

/*
 * On x86 this does nothing because we need pcie module loaded.
 */
int
iovcfg_plat_init(void)
{
	return (0);
}

/*
 * Discover PFs.
 */
int
iovcfg_update_pflist(void)
{
	(void) iov_discover_pfs();
	return (0);
}

/*
 * Discover VFs.
 */
int
iovcfg_plat_refresh_vflist(iov_pf_t *pfp)
{
	if (pfp)
		(void) iov_discover_vfs(pfp);
	return (0);
}

/*
 * Create platform specific part of VF.
 */
/* ARGSUSED */
void
iovcfg_plat_alloc_vf(iov_vf_t *vfp)
{
}

/*
 * Destroy platform specific part of VF.
 */
/* ARGSUSED */
void
iovcfg_plat_free_vf(iov_vf_t *vfp)
{
}

/*
 * Register for platform specific refconfiguration updates.
 */
/* ARGSUSED */
void
iovcfg_plat_config(iov_pf_t *pfp)
{
}

/*
 * Return the #vfs, for the PF specified by its path.
 * Returns:
 * 	Success:
 * 		rv:		0
 * 		num_vfs:	#vfs configued for the PF
 * 	Failure:
 * 		rv:		EINVAL - specified PF path is invalid.
 */
int
iovcfg_plat_get_numvfs(char *pf_pathname, uint_t *num_vfs_p)
{
	*num_vfs_p = 0;
	return (pciv_get_numvfs(pf_pathname, num_vfs_p));
}

/*
 * Given the PF pathname and a VF index, determine if the VF is loaned out.
 * 	Success:
 * 		rv:		0
 * 		loaned:		B_TRUE: VF is loaned out.
 *	Failure:
 * 		rv:		EINVAL - specified PF path is invalid.
 */
/*ARGSUSED*/
int
iovcfg_plat_is_vf_loaned(char *pf_pathname, uint_t vf_index, boolean_t *loaned)
{
	*loaned = B_FALSE;
	return (0);
}

static int
iov_discover_pfs(void)
{
	nvlist_t *nvl = NULL;
	int	rv;
	nvlist_t	**nvlist_array;
	uint_t		nelem;
	iov_pf_t	*pfp, *existing_pfp;
	char		*pf_path;
	int		i, j;
	uint_t		numvfs;
	nvlist_t	*pf_nvl = NULL;
	pci_plist_t	vfplist;

	rv = pciv_get_pf_list(&nvl);
	if (rv)
		return (rv);
	/*
	 * dump_nvlist(nvl, 0);
	 */
	rv = nvlist_lookup_nvlist_array(nvl, "pf_list", &nvlist_array, &nelem);
	if (rv) {
		nvlist_free(nvl);
		return (rv);
	}
	if (nelem == 0) {
		nvlist_free(nvl);
		return (0);
	}
	for (i = 0; i < nelem; i++) {
		rv = nvlist_lookup_string(nvlist_array[i],
		    "path", &pf_path);
		if (rv == 0) {
			rv = pciv_get_numvfs(pf_path, &numvfs);
			DBGx86("Found IOV device %s numvfs = %d\n",
			    pf_path, numvfs);
			/*
			 * If PF already exists in iovcfg_pf_lisp
			 * skip and look at the next one
			 */
			existing_pfp = iovcfg_pf_lookup(pf_path);
			if (existing_pfp)
				continue;
			pfp = iovcfg_alloc_pf("iov-network", pf_path);
			if (pfp == NULL)
				continue;
			pfp->ipf_numvfs = numvfs;
			rv = pciv_param_get(pf_path, &pf_nvl);
			if (pf_nvl) {
				pfp->ipf_params = pf_nvl;
				DBGx86("Found param list for device %s\n",
				    pf_path);
#ifdef DEBUG
				if (iovcfg_dbg & 0x4)
					for (j = 0; j < numvfs; j++) {
						rv = pciv_plist_getvf(pf_nvl,
						    j, &vfplist);
						if (rv)
							continue;
						dump_nvlist(
						    (nvlist_t *)vfplist, 0);
				}
#endif
			}
		}

		/*
		 * Add the PF to the list of PFs.
		 */
		pfp->ipf_nextp = iovcfg_pf_listp;
		iovcfg_pf_listp = pfp;
	}
	nvlist_free(nvl);
	return (0);
}

static int
iov_discover_vfs(iov_pf_t *pfp)
{
	nvlist_t *nvl = NULL;
	int	rv;
	nvlist_t	**nvlist_array;
	uint_t		nelem;
	iov_vf_t	*vfp;
	char		*vf_path;
	int		i;

	rv = pciv_get_vf_list(pfp->ipf_pathname, &nvl);
	if (rv) {
		nvlist_free(nvl);
		return (rv);
	}
#ifdef DEBUG
	if (iovcfg_dbg & 0x4)
		dump_nvlist(nvl, 0);
#endif
	rv = nvlist_lookup_nvlist_array(nvl, "vf_list", &nvlist_array, &nelem);
	if (rv) {
		nvlist_free(nvl);
		return (rv);
	}
	if (nelem == 0)
		return (0);
	for (i = 0; i < nelem; i++) {
		rv = nvlist_lookup_string(nvlist_array[i],
		    "path", &vf_path);
		if (rv == 0) {
			DBGx86("Found VF device %s \n", vf_path);
			vfp = iovcfg_alloc_vf(pfp, vf_path, i);
			if (vfp) {
				/* Add the VF to the list */
				vfp->ivf_nextp = pfp->ipf_vfp;
				pfp->ipf_vfp = vfp;
				(void) iovcfg_read_class_props(vfp);
			}
		}
	}
	nvlist_free(nvl);
	return (0);
}

/*
 * Given a node for a VF, read all the class specific properties.
 * Returns:
 * 	Success: 0
 * 	Failure: ENOENT - No class props
 */
static int
iovcfg_read_class_props(iov_vf_t *vfp)
{
	int		rv;
	iov_class_ops_t	*cl_ops;

	if ((cl_ops = vfp->ivf_pfp->ipf_cl_ops) == NULL) {
		return (ENOENT);
	}
	rv = cl_ops->iop_class_read_props(vfp, NULL, NULL);
	return (rv);
}

#ifdef DEBUG
static void
dump_nvlist(nvlist_t *nvl, int depth)
{
	nvpair_t	*nvp;
	data_type_t	nvp_type;
	uint32_t	nvp_value32;
	uint64_t	nvp_value64;
	char		*value_string;
	nvlist_t	*value_nvl;
	char		*nvp_name;
	int		i, err;
	char		prefix_string[40];
	nvlist_t	**nvl_array;
	uint_t		nelem;
	char		**string_array;

	if (nvlist_empty(nvl))
		return;
	i = depth;
	prefix_string[0] = '\0';
	while (i--)
		(void) sprintf(prefix_string, "%s\t", prefix_string);
	nvp = NULL;
	while (nvp = nvlist_next_nvpair(nvl, nvp)) {
		nvp_type = nvpair_type(nvp);
		nvp_name = nvpair_name(nvp);
		switch (nvp_type) {
		case DATA_TYPE_UINT64:
			err = nvpair_value_uint64(nvp,
			    &nvp_value64);
			if (!err)
				printf("%s%s = 0x%lx \n",
				    prefix_string, nvp_name,
				    (long unsigned)nvp_value64);
			break;
		case DATA_TYPE_UINT32:
			err = nvpair_value_uint32(nvp,
			    &nvp_value32);
			if (!err)
				printf("%s%s = 0x%x \n",
				    prefix_string, nvp_name, nvp_value32);
			break;
		case DATA_TYPE_INT32:
			err = nvpair_value_int32(nvp,
			    (int32_t *)&nvp_value32);
			if (!err)
				printf("%s%s = 0x%x \n",
				    prefix_string, nvp_name, nvp_value32);
			break;
		case DATA_TYPE_STRING:
			err = nvpair_value_string(nvp,
			    &value_string);
			if (!err)
				printf("%s%s = %s \n",
				    prefix_string, nvp_name, value_string);
			break;
		case DATA_TYPE_BOOLEAN:
			printf("%s%s \n",
			    prefix_string, nvp_name);
			break;
		case DATA_TYPE_NVLIST:
			err = nvpair_value_nvlist(nvp,
			    &value_nvl);
			if (err)
				break;
			printf("%s%s is a nvlist:\n",
			    prefix_string, nvp_name);
			dump_nvlist(value_nvl, depth + 1);
			break;
		case DATA_TYPE_NVLIST_ARRAY:
			err = nvpair_value_nvlist_array(nvp,
			    &nvl_array, &nelem);
			if (err)
				break;
			printf("%s%s is a nvlist_array of %d items:\n",
			    prefix_string, nvp_name, nelem);
			for (i = 0; i < nelem; i++) {
				printf("\n%s\t item %d:\n\n",
				    prefix_string, i + 1);
				dump_nvlist(nvl_array[i], depth + 1);
			}
			break;
		case DATA_TYPE_STRING_ARRAY:
			err = nvpair_value_string_array(nvp,
			    &string_array, &nelem);
			if (err)
				break;
			cmn_err(CE_NOTE,
			    "%s%s is a string_array of %d items:\n",
			    prefix_string, nvp_name, nelem);
			for (i = 0; i < nelem; i++) {
				cmn_err(CE_NOTE, "%s\t item %d:%s\n",
				    prefix_string, i + 1, string_array[i]);
			}
			break;
		default:
			printf("Unsupported type %d found\n", nvp_type);
			break;
		}
	}
}
#endif

#ifdef IOVCFG_UNCONFIG_SUPPORTED

void
iovcfg_plat_fini(void)
{
	iovcfg_free_pfs();
}

/*
 * Unregister platform specific reconfiguration updates.
 */
/* ARGSUSED */
void
iovcfg_plat_unconfig(iov_pf_t *pfp)
{
}

#endif

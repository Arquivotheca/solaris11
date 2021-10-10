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
 * This file contains functions related to IB persistent device cache, which
 * are private to IBTL.
 */
#include <sys/types.h>
#include <sys/sunddi.h>
#include <sys/ib/ibtl/impl/ibtl.h>
#include <sys/ib/ibtl/impl/ibc_nodedesc.h>
#include <sys/devcache.h>

#ifndef	offsetof
#define	offsetof(t, m)	(int)((&((t *)0L)->m))
#endif

/* Node descriptor is max of 64. */
#define	MAX_NODEDESC_LEN	64

static char		ibc_nodedesc_dbg[] = "ibc_nodedesc_devcache";
static nvf_handle_t	ibc_nodedesc_nvf_hdl;
static char		ibc_nodedesc[MAX_NODEDESC_LEN];
static size_t		ibc_nodedesc_len;
static boolean_t	ibc_nodedesc_inited = B_FALSE;
static kmutex_t		ibc_nodedesc_mutex;

static int ibc_impl_devcache_unpack_nvlist(nvf_handle_t, nvlist_t *, char *);
static int ibc_impl_devcache_pack_nvlist(nvf_handle_t, nvlist_t **);
static void ibc_impl_devcache_free_nvlist(nvf_handle_t);

/*
 * Per-HCA strings and Per HCA information in core info is stored in
 * nodedesc_hca_info_t structure
 */

typedef struct nodedesc_hca_info_s {
	char		nodedesc_hca_str[MAX_NODEDESC_LEN];
	size_t		nodedesc_hca_strlen;

	/*
	 * nodedesc_dip is NULL for info read from persistent
	 * store but attach not yet called. The fields :
	 * 	nodedesc_drvr_name & nodedesc_drvr_instance
	 * will be initialized.
	 */
	dev_info_t	*nodedesc_dip;
	char		*nodedesc_drvr_name;
	uint32_t	nodedesc_drvr_instance;

	char		nodedesc_compl_str[MAX_NODEDESC_LEN];
	size_t		nodedesc_compl_strlen;

	list_node_t	nodedesc_list_node;
} nodedesc_hca_info_t;

list_t	nodedesc_hca_list;
uint_t	nodedesc_hca_info_cnt;

static nodedesc_hca_info_t *find_hca_infop(dev_info_t *, uint64_t);
static boolean_t update_hca_infop(dev_info_t *, char *,
    nodedesc_hca_info_t **);
static void update_nodedesc_comp_str_all();
static void update_nodedesc_comp_str(nodedesc_hca_info_t *);
static uint_t get_hca_arrays(char ***, uint64_t **);
static void set_from_hca_arrays(char **, uint64_t *, uint_t);
static void free_hca_arrays(char **, uint64_t *, uint_t);

static nvf_ops_t ibc_devcache_ops = {
	"/etc/devices/infiniband_hca_persistent_cache",
	ibc_impl_devcache_unpack_nvlist,
	ibc_impl_devcache_pack_nvlist,
	ibc_impl_devcache_free_nvlist,
	NULL
};

void
ibc_impl_devcache_init()
{
	ibc_nodedesc_nvf_hdl = nvf_register_file(&ibc_devcache_ops);
	ASSERT(ibc_nodedesc_nvf_hdl);
	mutex_init(&ibc_nodedesc_mutex, NULL, MUTEX_DEFAULT, NULL);
	list_create(&nodedesc_hca_list, sizeof (nodedesc_hca_info_t),
	    offsetof(nodedesc_hca_info_t, nodedesc_list_node));
}

void
ibc_impl_devcache_fini()
{
	mutex_destroy(&ibc_nodedesc_mutex);
	list_destroy(&nodedesc_hca_list);
	/* nvf_unregister_file() to free the memory. */
}


/*
 * This function is called to parse the nvlist format when reading
 * /etc/devices/infiniband_hca_persistent_cache. This function is
 * used to initialize or update the incore common Node Descriptor
 * string and per HCA data if any.
 *
 * Format of the infiniband_hca_persistent _cache:
 *	a. Contains the common Node Descriptor string
 * 	b. Per HCA Nodedescription string array
 * 	c. Per HCA information uint64_t array
 * 	Bits 0  to 31 - Driver instance
 * 	Bits 32 to 39 - HCA driver types - See defines below
 * 	Bits 40 to 63 - OFED HCA number (TBD)
 */
#define	NODEDESC_HCA_DRIVER_NONE	0x00
#define	NODEDESC_HCA_DRIVER_TAVOR	0x01
#define	NODEDESC_HCA_DRIVER_HERMON	0x02
static int
ibc_impl_devcache_unpack_nvlist(nvf_handle_t fd, nvlist_t *nvp, char *name)
{
	int		rval;
	char		*node_desc_string = NULL;
	char		**hca_str_arr;
	uint64_t	*hca_info_arr;
	uint_t		nstrs = 0, ncnts = 0;

	ASSERT(ibc_nodedesc_nvf_hdl == fd);
#ifndef __lock_lint
	ASSERT(RW_WRITE_HELD(nvf_lock(ibc_nodedesc_nvf_hdl)));
#endif
	ASSERT(MUTEX_HELD(&ibc_nodedesc_mutex));
	ASSERT(ibc_nodedesc_inited == B_FALSE);

	IBTF_DPRINTF_L5(ibc_nodedesc_dbg, "unpack_nvlist(%x, %p, %s",
	    fd, nvp, name);

	/* Check the name of the sublist with the name created */
	if (strcmp(name, IBC_NODE_DESC_LIST) != 0)
		return (-1);

	rval = nvlist_lookup_string(nvp, IBC_NODE_DESC,
	    &node_desc_string);
	if (rval != 0) {
		IBTF_DPRINTF_L2(ibc_nodedesc_dbg,
		    "Common Node Descriptor NV not found");
		return (-1);
	}

	IBTF_DPRINTF_L5(ibc_nodedesc_dbg, "Get HCA_STR");
	rval = nvlist_lookup_string_array(nvp, IBC_NODE_DESC_HCA_STR,
	    &hca_str_arr, &nstrs);
	if (rval != 0) {
		IBTF_DPRINTF_L3(ibc_nodedesc_dbg,
		    "HCA Node Descriptor NV not found");
		goto unpack_common;
	}

	rval = nvlist_lookup_uint64_array(nvp, IBC_NODE_DESC_HCA_INFO,
	    &hca_info_arr, &ncnts);
	if (rval != 0) {
		IBTF_DPRINTF_L2(ibc_nodedesc_dbg,
		    "HCA Node Info NV not found");
		return (-1);
	}

	IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
	    "unpack_nvlist: str_arr %p, info_arr %p",
	    hca_str_arr, hca_info_arr);

	if (ncnts != nstrs) {
		IBTF_DPRINTF_L2(ibc_nodedesc_dbg,
		    "HCA Description cnt %d, info cnt %d, mismatch!",
		    nstrs, ncnts);
		return (-1);
	}

unpack_common:
	if (ibc_nodedesc_inited == B_TRUE) {
		return (DDI_SUCCESS);
	}
	ibc_nodedesc_inited = B_TRUE;
	(void) strncpy(ibc_nodedesc, node_desc_string,
	    MAX_NODEDESC_LEN);

	if (nstrs)
		set_from_hca_arrays(hca_str_arr,
		    hca_info_arr, nstrs);

	return (DDI_SUCCESS);
}

/*
 * Pack the node descriptor string to a NV string. This function is called while
 * writing to /etc/devices/infiniband_hca_persistent_cache file.
 */
static int
ibc_impl_devcache_pack_nvlist(nvf_handle_t fd, nvlist_t **ret_nvl)
{
	int		rval;
	nvlist_t	*main_nvl, *sub_nvl;
	char		**hca_desc_arr;
	uint64_t	*hca_info_arr;
	uint_t		nelems;

	ASSERT(ibc_nodedesc_nvf_hdl == fd);

	/* Alloc the main NV Pair */
	rval = nvlist_alloc(&main_nvl, NV_UNIQUE_NAME, KM_SLEEP);
	if (rval != 0) {
		IBTF_DPRINTF_L2(ibc_nodedesc_dbg, "nvlist_alloc fail!!");
		return (DDI_FAILURE);
	}

	/* Allocate the sub NV Pair to stuff Node descriptor string */
	rval = nvlist_alloc(&sub_nvl, NV_UNIQUE_NAME, KM_SLEEP);
	if (rval != 0) {
		IBTF_DPRINTF_L2(ibc_nodedesc_dbg,
		    "nvlist_alloc for sub_nvl failed");
		sub_nvl = NULL;
		goto pack_error;
	}

	mutex_enter(&ibc_nodedesc_mutex);
	if (nvlist_add_string(sub_nvl, IBC_NODE_DESC,
	    ibc_nodedesc) != 0) {
		mutex_exit(&ibc_nodedesc_mutex);
		IBTF_DPRINTF_L2(ibc_nodedesc_dbg,
		    "nvlist_add_common_string fail!!");
		goto pack_error;
	}

	nelems = get_hca_arrays(&hca_desc_arr, &hca_info_arr);
	if (!nelems) {
		IBTF_DPRINTF_L3(ibc_nodedesc_dbg,
		    "ibc_get_hca_arrays ret 0");
		goto add_nvlist;
	}
	IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
	    "ibc_get_hca_arrays ret %x, %p, %p",
	    nelems, hca_desc_arr, hca_info_arr);

	if (nvlist_add_string_array(sub_nvl, IBC_NODE_DESC_HCA_STR,
	    hca_desc_arr, nelems) != 0) {
		mutex_exit(&ibc_nodedesc_mutex);
		IBTF_DPRINTF_L3(ibc_nodedesc_dbg,
		    "add_string_array ret failed");
		free_hca_arrays(hca_desc_arr,  hca_info_arr, nelems);
		goto pack_error;
	}
	if (nvlist_add_uint64_array(sub_nvl, IBC_NODE_DESC_HCA_INFO,
	    hca_info_arr, nelems) != 0) {
		mutex_exit(&ibc_nodedesc_mutex);
		IBTF_DPRINTF_L3(ibc_nodedesc_dbg,
		    "add_string_array ret failed");
		free_hca_arrays(hca_desc_arr,  hca_info_arr, nelems);
		goto pack_error;
	}
	free_hca_arrays(hca_desc_arr,  hca_info_arr, nelems);

add_nvlist:
	mutex_exit(&ibc_nodedesc_mutex);

	rval = nvlist_add_nvlist(main_nvl, IBC_NODE_DESC_LIST, sub_nvl);
	if (rval != 0) {
		IBTF_DPRINTF_L2(ibc_nodedesc_dbg,
		    "nvlist_alloc for sub failed");
		goto pack_error;
	}
	nvlist_free(sub_nvl);
	*ret_nvl = main_nvl;
	return (DDI_SUCCESS);

pack_error:
	if (sub_nvl)
		nvlist_free(sub_nvl);
	nvlist_free(main_nvl);
	*ret_nvl = NULL;
	return (DDI_FAILURE);
}

/*
 * We do not use nvlist. Nothing top free. Just return.
 */
static void
ibc_impl_devcache_free_nvlist(nvf_handle_t fd)
{
	ASSERT(ibc_nodedesc_nvf_hdl == fd);
#ifndef __lock_lint
	ASSERT(RW_WRITE_HELD(nvf_lock(ibc_nodedesc_nvf_hdl)));
#endif
}

/*
 * Functions exported to HCA drivers.
 */
char *
ibc_read_nodedesc(dev_info_t *dip, size_t *len)
{
	char	*ret_nodedesc;
	int	rval;
	nodedesc_hca_info_t *hca_infop;

	IBTF_DPRINTF_L5(ibc_nodedesc_dbg, "read_nodedesc(%p, %p)",
	    dip, len);

	mutex_enter(&ibc_nodedesc_mutex);
	if (!ibc_nodedesc_inited) {
#ifndef __lock_lint
		rw_enter(nvf_lock(ibc_nodedesc_nvf_hdl), RW_WRITER);
#else
		(void) ibc_impl_devcache_unpack_nvlist(
		    ibc_nodedesc_nvf_hdl, NULL, NULL);
#endif
		rval = nvf_read_file(ibc_nodedesc_nvf_hdl);
		if (rval) {
			/*
			 * If the persistent file does not exist,
			 * just log a L5 message. Errors other than
			 * ENOENT indicate a issue in the persistent
			 * file, flag this as an L2 message.
			 */
			if (rval == ENOENT)
				IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
				    "nvf_read_file ret %x", rval);
			else
				IBTF_DPRINTF_L2(ibc_nodedesc_dbg,
				    "nvf_read_file ret %x", rval);
#ifndef __lock_lint
			rw_exit(nvf_lock(ibc_nodedesc_nvf_hdl));
#endif
			mutex_exit(&ibc_nodedesc_mutex);
			*len = 0;
			return (NULL);
		}
#ifndef __lock_lint
		rw_exit(nvf_lock(ibc_nodedesc_nvf_hdl));
#endif

		/*
		 * Node Descriptor will have been inited while reading the
		 * data. Return the same.
		 */
		ASSERT(ibc_nodedesc_inited);
		ibc_nodedesc_len = strlen(ibc_nodedesc) + 1;
	}

	hca_infop = find_hca_infop(dip, 0);
	IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
	    "read_nodedesc: find_hca_infop ret %p", hca_infop);
	if (!hca_infop) {
		mutex_exit(&ibc_nodedesc_mutex);
		*len = 0;
		return (NULL);
	}
	IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
	    "read_nodedesc: compl nodedesc len %x",
	    hca_infop->nodedesc_compl_strlen);
	ret_nodedesc = kmem_zalloc(
	    hca_infop->nodedesc_compl_strlen, KM_NOSLEEP);
	if (!ret_nodedesc) {
		mutex_exit(&ibc_nodedesc_mutex);
		*len = 0;
		return (NULL);
	}
	*len = hca_infop->nodedesc_compl_strlen;
	(void) strcpy(ret_nodedesc,
	    hca_infop->nodedesc_compl_str);
	mutex_exit(&ibc_nodedesc_mutex);
	return (ret_nodedesc);
}

ibt_status_t
ibc_write_nodedesc(dev_info_t *dip, char *node_desc, uint32_t update_flag)
{
	IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
	    "ibc_write_nodedesc(%p, %s, %x)", dip, node_desc,
	    update_flag);

	mutex_enter(&ibc_nodedesc_mutex);
	if (update_flag & IBC_NODEDESC_UPDATE_HCA_STRING) {
		boolean_t	updated;

		if (ibc_nodedesc_inited != B_TRUE) {
			mutex_exit(&ibc_nodedesc_mutex);
			IBTF_DPRINTF_L2(ibc_nodedesc_dbg,
			    "per HCA string can be set only if "
			    "common string is set");
			return (IBT_ILLEGAL_OP);
		}
		updated = update_hca_infop(dip, node_desc, NULL);

		if (updated == B_TRUE) {
#ifndef __lock_lint
			rw_enter(nvf_lock(ibc_nodedesc_nvf_hdl),
			    RW_WRITER);
#endif
			nvf_mark_dirty(ibc_nodedesc_nvf_hdl);
#ifndef __lock_lint
			rw_exit(nvf_lock(ibc_nodedesc_nvf_hdl));
#endif
			nvf_wake_daemon();
		}
		mutex_exit(&ibc_nodedesc_mutex);
		return (IBT_SUCCESS);
	}

	if (((update_flag & IBC_NODEDESC_UPDATE_STRING) == 0) &&
	    (ibc_nodedesc_inited == B_FALSE)) {
		mutex_exit(&ibc_nodedesc_mutex);
		return (IBT_ILLEGAL_OP);
	} else if (((update_flag & IBC_NODEDESC_UPDATE_STRING) == 0) &&
	    (ibc_nodedesc_inited == B_TRUE))  {
		mutex_exit(&ibc_nodedesc_mutex);
		return (IBT_SUCCESS);
	}


	/*
	 * If the node descriptor has not been initialized or
	 * if the node descriptor is been modified:
	 *	Change the node descriptor string
	 * Mark the nvf handle dirty. This will trigger the
	 * Node descriptor string to be written to persistent
	 * storage. Write is done even if the string has not
	 * been modified, to overwrite the persistent file, if
	 * corrupted.
	 */
	if (ibc_nodedesc_inited || strncmp((char *)ibc_nodedesc,
	    node_desc, MAX_NODEDESC_LEN) != 0) {
		(void) strncpy(ibc_nodedesc, node_desc,
		    MAX_NODEDESC_LEN);
		ibc_nodedesc_inited = B_TRUE;
		ibc_nodedesc_len = strlen(ibc_nodedesc) + 1;
		update_nodedesc_comp_str_all();
	}
#ifndef __lock_lint
	rw_enter(nvf_lock(ibc_nodedesc_nvf_hdl), RW_WRITER);
#endif
	nvf_mark_dirty(ibc_nodedesc_nvf_hdl);
#ifndef __lock_lint
	rw_exit(nvf_lock(ibc_nodedesc_nvf_hdl));
#endif
	nvf_wake_daemon();
	mutex_exit(&ibc_nodedesc_mutex);
	return (IBT_SUCCESS);
}

static nodedesc_hca_info_t *
find_hca_infop(dev_info_t *dip, uint64_t hca_info)
{
	nodedesc_hca_info_t	*ret_hca_infop;
	char			*driver_name = NULL;
	uint8_t			drv_name_type;
	uint32_t		drv_instance;
	dev_info_t		*nd_dip;

	IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
	    "find_hca_infop(%p, %llx)", dip, hca_info);

	ASSERT(MUTEX_HELD(&ibc_nodedesc_mutex));
	for (ret_hca_infop = list_head(&nodedesc_hca_list); ret_hca_infop;
	    ret_hca_infop = list_next(&nodedesc_hca_list, ret_hca_infop)) {
		nd_dip = ret_hca_infop->nodedesc_dip;
		if (dip && nd_dip == dip) {
			return (ret_hca_infop);
		} else if (dip && nd_dip == NULL &&
		    strcmp(ret_hca_infop->nodedesc_drvr_name,
		    ddi_driver_name(dip)) == 0 &&
		    ret_hca_infop->nodedesc_drvr_instance ==
		    ddi_get_instance(dip)) {
			ret_hca_infop->nodedesc_dip = dip;
			update_nodedesc_comp_str(ret_hca_infop);
			return (ret_hca_infop);
		} else if (dip == NULL && nd_dip) {
			drv_instance = hca_info & 0xffffffff;
			drv_name_type = (hca_info  >> 32) & 0xff;
			if (drv_name_type == NODEDESC_HCA_DRIVER_TAVOR)
				driver_name = "tavor";
			else if (drv_name_type == NODEDESC_HCA_DRIVER_HERMON)
				driver_name = "hermon";
			if (driver_name && (strcmp(driver_name,
			    ddi_driver_name(nd_dip)) == 0) &&
			    (drv_instance == ddi_get_instance(nd_dip))) {
				return (ret_hca_infop);
			}
		}
	}

	/* No matching HCA infop, create new one */
	if (dip && update_hca_infop(dip, NULL, &ret_hca_infop) !=
	    B_TRUE)
		return (NULL);
	return (ret_hca_infop);
}

static void
update_nodedesc_comp_str_all()
{
	nodedesc_hca_info_t	*scan;

	ASSERT(MUTEX_HELD(&ibc_nodedesc_mutex));
	for (scan = list_head(&nodedesc_hca_list); scan;
	    scan = list_next(&nodedesc_hca_list, scan)) {
		update_nodedesc_comp_str(scan);
	}
}

static void
update_nodedesc_comp_str(nodedesc_hca_info_t *hcap)
{
	size_t		total_len;

	IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
	    "update_nodedesc_comp_str(%p) , %x", hcap,
	    hcap->nodedesc_hca_strlen);

	ASSERT(MUTEX_HELD(&ibc_nodedesc_mutex));
	total_len = (ibc_nodedesc_len + hcap->nodedesc_hca_strlen) >
	    MAX_NODEDESC_LEN ? MAX_NODEDESC_LEN :
	    (ibc_nodedesc_len + hcap->nodedesc_hca_strlen);
	(void) strcpy(hcap->nodedesc_compl_str, ibc_nodedesc);
	if (hcap->nodedesc_hca_strlen)
		(void) strncat(hcap->nodedesc_compl_str,
		    hcap->nodedesc_hca_str,
		    total_len - ibc_nodedesc_len);
	hcap->nodedesc_compl_strlen = total_len;
}

static void
add_to_hca_nodesc_list(nodedesc_hca_info_t *new)
{
	ASSERT(MUTEX_HELD(&ibc_nodedesc_mutex));
	list_insert_tail(&nodedesc_hca_list, new);
	nodedesc_hca_info_cnt++;
}

static boolean_t
update_hca_infop(dev_info_t *dip, char *hca_nodedesc,
    nodedesc_hca_info_t **ret_hcap)
{
	nodedesc_hca_info_t	*scan, *new;

	IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
	    "update_hca_infop(%p, %s, %p)",
	    dip, hca_nodedesc, ret_hcap);
	ASSERT(MUTEX_HELD(&ibc_nodedesc_mutex));
	for (scan = list_head(&nodedesc_hca_list); scan;
	    scan = list_next(&nodedesc_hca_list, scan)) {
		if (scan->nodedesc_dip == dip) {
			IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
			    "update_hca_infop: dip matched - %p",
			    scan);
			if (strncmp(scan->nodedesc_hca_str,
			    hca_nodedesc, MAX_NODEDESC_LEN) != 0) {
				(void) strncpy(scan->nodedesc_hca_str,
				    hca_nodedesc,
				    MAX_NODEDESC_LEN);
				scan->nodedesc_hca_strlen =
				    strlen(scan->nodedesc_hca_str) + 1;
				update_nodedesc_comp_str(scan);
				if (ret_hcap)
					*ret_hcap = scan;
				return (B_TRUE);
			} else
				return (B_FALSE);
		}
	}

	new = kmem_zalloc(sizeof (nodedesc_hca_info_t), KM_SLEEP);
	new->nodedesc_dip = dip;
	if (hca_nodedesc) {
		(void) strncpy(new->nodedesc_hca_str, hca_nodedesc,
		    MAX_NODEDESC_LEN);
		new->nodedesc_hca_strlen =
		    strlen(new->nodedesc_hca_str) + 1;
	} else {
		new->nodedesc_hca_strlen = 0;
	}
	update_nodedesc_comp_str(new);
	add_to_hca_nodesc_list(new);
	if (ret_hcap)
		*ret_hcap = new;
	return (B_TRUE);
}

static uint_t
get_hca_arrays(char ***hca_nd_strp, uint64_t **hca_infop)
{
	int		i;
	char		**hca_str_arr;
	uint64_t	*hca_info_arr;
	nodedesc_hca_info_t	*scan;
	char		*driver_name;
	uint32_t	driver_inst;

	ASSERT(MUTEX_HELD(&ibc_nodedesc_mutex));
	if (!nodedesc_hca_info_cnt)
		return (0);

	hca_str_arr = kmem_zalloc(nodedesc_hca_info_cnt *
	    sizeof (char *), KM_SLEEP);
	hca_info_arr = kmem_zalloc(nodedesc_hca_info_cnt *
	    sizeof (uint64_t), KM_SLEEP);
	for (i = 0, scan = list_head(&nodedesc_hca_list);
	    i < nodedesc_hca_info_cnt && scan;
	    i++, scan = list_next(&nodedesc_hca_list, scan)) {
		hca_str_arr[i] = i_ddi_strdup(scan->nodedesc_hca_str,
		    KM_SLEEP);
		if (scan->nodedesc_dip) {
			driver_name =
			    (char *)ddi_driver_name(scan->nodedesc_dip);
			driver_inst = ddi_get_instance(scan->nodedesc_dip) &
			    0xffffffff;
		} else {
			driver_name = scan->nodedesc_drvr_name;
			driver_inst = scan->nodedesc_drvr_instance;
		}
		if (strcmp(driver_name, "tavor") == 0)
			hca_info_arr[i] = (uint64_t)
			    ((uint8_t)NODEDESC_HCA_DRIVER_TAVOR) << 32;
		else if (strcmp(driver_name, "hermon") == 0)
			hca_info_arr[i] = (uint64_t)
			    ((uint8_t)NODEDESC_HCA_DRIVER_HERMON) << 32;
		hca_info_arr[i] |= driver_inst;
	}
	*hca_nd_strp = hca_str_arr;
	*hca_infop = hca_info_arr;
	return (i);
}

static void
set_from_hca_arrays(char **hca_nd_strp, uint64_t *hca_infop,
    uint_t nelems)
{
	uint_t		i;
	uint8_t		drvr_type;
	nodedesc_hca_info_t *nd_hcap;
	dev_info_t	*dip;

	IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
	    "set_from_hca_arrays(%p, %p, %x)",
	    hca_nd_strp, hca_infop, nelems);

	ASSERT(MUTEX_HELD(&ibc_nodedesc_mutex));
	for (i = 0; i < nelems; i++) {
		nd_hcap = find_hca_infop(NULL, hca_infop[i]);
		IBTF_DPRINTF_L5(ibc_nodedesc_dbg,
		    "set_from_hca_arrays: find_hca_infop ret %p",
		    nd_hcap);
		if (nd_hcap) {
			(void) strncpy(nd_hcap->nodedesc_hca_str,
			    hca_nd_strp[i], MAX_NODEDESC_LEN);
			nd_hcap->nodedesc_hca_strlen =
			    strlen(nd_hcap->nodedesc_hca_str) + 1;
			dip = nd_hcap->nodedesc_dip;
			ASSERT(dip);
			update_nodedesc_comp_str(nd_hcap);
		}
		nd_hcap = kmem_zalloc(sizeof (nodedesc_hca_info_t),
		    KM_SLEEP);
		(void) strncpy(nd_hcap->nodedesc_hca_str,
		    hca_nd_strp[i], MAX_NODEDESC_LEN);
		nd_hcap->nodedesc_hca_strlen =
		    strlen(nd_hcap->nodedesc_hca_str) + 1;
		drvr_type = (hca_infop[i] >> 32) & 0xff;
		if (drvr_type == NODEDESC_HCA_DRIVER_TAVOR)
			nd_hcap->nodedesc_drvr_name = "tavor";
		else if (drvr_type == NODEDESC_HCA_DRIVER_HERMON)
			nd_hcap->nodedesc_drvr_name = "hermon";
		else {
			kmem_free(nd_hcap, sizeof (nodedesc_hca_info_t));
			continue;
		}
		nd_hcap->nodedesc_drvr_instance = hca_infop[i] & 0xffffffff;
		add_to_hca_nodesc_list(nd_hcap);
	}
}

static void
free_hca_arrays(char **hca_nd_strp, uint64_t *hca_infop, uint_t nelems)
{
	uint_t	i;
	size_t	len;

	for (i = 0; i < nelems; i++) {
		len = strlen(hca_nd_strp[i]) + 1;
		kmem_free(hca_nd_strp[i], len);
	}
	kmem_free(hca_nd_strp, sizeof (char *) * nelems);
	kmem_free(hca_infop, sizeof (uint64_t) * nelems);
}

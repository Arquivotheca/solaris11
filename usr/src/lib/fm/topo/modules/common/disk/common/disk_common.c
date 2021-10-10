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
 * Functions in this file are shared between the disk and ses enumerators.
 *
 * A topo_list_t of all disks is returned by a successful disk_list_gather()
 * call, and the list is freed by a disk_list_free(). To create a 'disk' topo
 * node below a specific 'bay' parent node either disk_declare_path() or
 * disk_declare_addr() are called. The caller determines which 'disk' is
 * in which 'bay'. A disk's 'label' and 'authority' information come from
 * its parent 'bay' node.
 */

#include <ctype.h>
#include <strings.h>
#include <libdevinfo.h>
#include <devid.h>
#include <sys/libdevid.h>
#include <pthread.h>
#include <inttypes.h>
#include <sys/dkio.h>
#include <sys/scsi/scsi_types.h>
#include <fm/topo_mod.h>
#include <fm/topo_list.h>
#include <sys/fm/protocol.h>
#include <sys/scsi/generic/inquiry.h>
#include "disk.h"

/* common callback information for di_walk_node() and di_devlink_walk */
typedef struct disk_cbdata {
	topo_mod_t		*dcb_mod;
	topo_list_t		*dcb_list;
	di_devlink_handle_t	dcb_devhdl;
	dev_di_node_t		*dcb_dnode;	/* for di_devlink_walk only */
} disk_cbdata_t;


static const topo_pgroup_info_t io_pgroup = {
	TOPO_PGROUP_IO,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

static const topo_pgroup_info_t disk_auth_pgroup = {
	FM_FMRI_AUTHORITY,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

static const topo_pgroup_info_t storage_pgroup = {
	TOPO_PGROUP_STORAGE,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

/*
 * Set the properties of the disk node, from dev_di_node_t data.
 * Properties include:
 *	group: protocol	 properties: resource, asru, label, fru
 *	group: authority properties: product-id, chasis-id, server-id
 *	group: io	 properties: devfs-path, devid
 *	group: storage	 properties:
 *		- logical-disk, disk-model, disk-manufacturer, serial-number
 *		- firmware-revision, capacity-in-bytes
 *
 * NOTE: the io and storage groups won't be present if the dnode passed in is
 * NULL. This happens when a disk is found through ses, but is not enumerated
 * in the devinfo tree.
 */
static int
disk_set_props(topo_mod_t *mod, tnode_t *parent,
    tnode_t *dtn, dev_di_node_t *dnode)
{
	char		*label = NULL;
	nvlist_t	*fmri = NULL;
	nvlist_t	*asru = NULL;
	int		err;

	/* pull the label property down from our parent 'bay' node */
	if (topo_node_label(parent, &label, &err) != 0) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "label error %s\n", topo_strerror(err));
		goto error;
	}
	if (topo_node_label_set(dtn, label, &err) != 0) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "label_set error %s\n", topo_strerror(err));
		goto error;
	}

	/* get the resource fmri, and use it as the fru */
	if (topo_node_resource(dtn, &fmri, &err) != 0) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "resource error: %s\n", topo_strerror(err));
		goto error;
	}
	if (topo_node_fru_set(dtn, fmri, 0, &err) != 0) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "fru_set error: %s\n", topo_strerror(err));
		goto error;
	}

	/* create/set the authority group */
	if ((topo_pgroup_create(dtn, &disk_auth_pgroup, &err) != 0) &&
	    (err != ETOPO_PROP_DEFD)) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "create disk_auth error %s\n", topo_strerror(err));
		goto error;
	}

	/* create the storage group */
	if (topo_pgroup_create(dtn, &storage_pgroup, &err) != 0) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "create storage error %s\n", topo_strerror(err));
		goto error;
	}

	/* no dnode was found for this disk - skip the io and storage groups */
	if (dnode == NULL) {
		err = 0;
		goto out;
	}

	/* form and set the asru */
	if ((asru = topo_mod_devfmri(mod, FM_DEV_SCHEME_VERSION,
	    dnode->ddn_dpaths[0], dnode->ddn_devid)) == NULL) {
		err = ETOPO_FMRI_UNKNOWN;
		topo_mod_dprintf(mod, "disk_set_props: "
		    "asru error %s\n", topo_strerror(err));
		goto error;
	}
	if (topo_node_asru_set(dtn, asru, 0, &err) != 0) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "asru_set error %s\n", topo_strerror(err));
		goto error;
	}

	/* create/set the devfs-path and devid in the io group */
	if (topo_pgroup_create(dtn, &io_pgroup, &err) != 0) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "create io error %s\n", topo_strerror(err));
		goto error;
	}

	if (topo_prop_set_string(dtn, TOPO_PGROUP_IO, TOPO_IO_DEV_PATH,
	    TOPO_PROP_IMMUTABLE, dnode->ddn_dpaths[0], &err) != 0) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "set dev error %s\n", topo_strerror(err));
		goto error;
	}

	if (dnode->ddn_devid && topo_prop_set_string(dtn, TOPO_PGROUP_IO,
	    TOPO_IO_DEVID, TOPO_PROP_IMMUTABLE, dnode->ddn_devid, &err) != 0) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "set devid error %s\n", topo_strerror(err));
		goto error;
	}

	if (dnode->ddn_ppaths_n &&
	    topo_prop_set_string_array(dtn, TOPO_PGROUP_IO, TOPO_IO_PHYS_PATH,
	    TOPO_PROP_IMMUTABLE, (const char **)dnode->ddn_ppaths,
	    dnode->ddn_ppaths_n, &err)) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "set phys-path error %s\n", topo_strerror(err));
		goto error;
	}

	/* set the storage group public /dev name */
	if (dnode->ddn_lpaths && dnode->ddn_lpaths[0] &&
	    topo_prop_set_string(dtn, TOPO_PGROUP_STORAGE,
	    TOPO_STORAGE_LOGICAL_DISK_NAME, TOPO_PROP_IMMUTABLE,
	    dnode->ddn_lpaths[0], &err)) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "set disk_name error %s\n", topo_strerror(err));
		goto error;
	}

	/* populate other misc storage group properties */
	if (dnode->ddn_mfg &&
	    topo_prop_set_string(dtn, TOPO_PGROUP_STORAGE,
	    TOPO_STORAGE_MANUFACTURER, TOPO_PROP_IMMUTABLE,
	    dnode->ddn_mfg, &err)) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "set mfg error %s\n", topo_strerror(err));
		goto error;
	}
	if (dnode->ddn_model &&
	    topo_prop_set_string(dtn, TOPO_PGROUP_STORAGE,
	    TOPO_STORAGE_MODEL, TOPO_PROP_IMMUTABLE,
	    dnode->ddn_model, &err)) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "set model error %s\n", topo_strerror(err));
		goto error;
	}
	if (dnode->ddn_serial &&
	    topo_prop_set_string(dtn, TOPO_PGROUP_STORAGE,
	    TOPO_STORAGE_SERIAL_NUM, TOPO_PROP_IMMUTABLE,
	    dnode->ddn_serial, &err)) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "set serial error %s\n", topo_strerror(err));
		goto error;
	}
	if (dnode->ddn_firm &&
	    topo_prop_set_string(dtn, TOPO_PGROUP_STORAGE,
	    TOPO_STORAGE_FIRMWARE_REV, TOPO_PROP_IMMUTABLE,
	    dnode->ddn_firm, &err)) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "set firm error %s\n", topo_strerror(err));
		goto error;
	}
	if (dnode->ddn_cap &&
	    topo_prop_set_string(dtn, TOPO_PGROUP_STORAGE,
	    TOPO_STORAGE_CAPACITY, TOPO_PROP_IMMUTABLE,
	    dnode->ddn_cap, &err)) {
		topo_mod_dprintf(mod, "disk_set_props: "
		    "set cap error %s\n", topo_strerror(err));
		goto error;
	}
	err = 0;

out:	if (asru)
		nvlist_free(asru);
	if (fmri)
		nvlist_free(fmri);
	if (label)
		topo_mod_strfree(mod, label);
	return (err);

error:	err = topo_mod_seterrno(mod, err);
	goto out;
}

/*ARGSUSED*/
static int
disk_cro_mk(topo_mod_t *mod, tnode_t *receptacle, dev_di_node_t *dnode)
{
	char		*product = NULL;
	char		*chassis = NULL;
	char		*label = NULL;
	nvlist_t	*fmri_nvl;
	char		*fmri = NULL;
	int		err = 0;

	/*
	 * Pull the product and chassis down from our receptacle 'bay' node
	 * (with value of UNKNOWN if not found).
	 */
	if (topo_prop_get_string(receptacle, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_PRODUCT, &product, &err))
		product = topo_mod_strdup(mod, "unknown");
	if (topo_prop_get_string(receptacle, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_CHASSIS, &chassis, &err))
		chassis = topo_mod_strdup(mod, "unknown");
	if (topo_node_label(receptacle, &label, &err) != 0) {
		topo_mod_dprintf(mod, "disk_cro_mk: "
		    "label error %s\n", topo_strerror(err));
		goto error;
	}

	/* Get string fmri of receptacle. */
	if (topo_node_resource(receptacle, &fmri_nvl, &err) == 0) {
		(void) topo_mod_nvl2str(mod, fmri_nvl, &fmri);
		nvlist_free(fmri_nvl);
	}
	err = 0;

	if (dnode == NULL) {
		/* make cro for empty receptacle (no occupant info) */
		(void) di_cromk_recadd(topo_mod_cromk_hdl(mod), 0,
		    product, chassis, NULL, label,
		    "bay", fmri, NULL, NULL, NULL,
		    NULL, 0, NULL, 0, NULL, 0,
		    NULL, NULL, NULL, NULL, NULL, NULL,
		    NULL, 0, NULL, 0, NULL, 0);
	} else {
		(void) di_cromk_recadd(topo_mod_cromk_hdl(mod), 0,
		    product, chassis, NULL, label,
		    "bay", fmri, "disk", NULL, NULL,
		    dnode->ddn_dpaths, dnode->ddn_dpaths_n,
		    dnode->ddn_ppaths, dnode->ddn_ppaths_n,
		    dnode->ddn_lpaths, dnode->ddn_lpaths_n,
		    dnode->ddn_devid, dnode->ddn_mfg, dnode->ddn_model,
		    dnode->ddn_part, dnode->ddn_serial, dnode->ddn_firm,
		    &dnode->ddn_cap, 1,
		    dnode->ddn_target_ports, dnode->ddn_target_ports_n,
		    dnode->ddn_attached_ports, dnode->ddn_attached_ports_n);
	}

out:	if (fmri)
		topo_mod_strfree(mod, fmri);
	if (label)
		topo_mod_strfree(mod, label);
	if (chassis)
		topo_mod_strfree(mod, chassis);
	if (product)
		topo_mod_strfree(mod, product);
	return (err);

error:	err = topo_mod_seterrno(mod, err);
	goto out;
}

/*
 * Trim leading and trailing whitespace from the string.
 */
static char *
disk_trim_whitespace(topo_mod_t *mod, const char *begin)
{
	const char *end;
	char *buf;
	size_t count;

	if (begin == NULL)
		return (NULL);

	end = begin + strlen(begin);

	while (begin < end && isspace(*begin))
		begin++;
	while (begin < end && isspace(*(end - 1)))
		end--;

	count = end - begin;
	if ((buf = topo_mod_alloc(mod, count + 1)) == NULL)
		return (NULL);

	(void) strlcpy(buf, begin, count + 1);

	return (buf);
}

/*
 * Manufacturing strings can contain characters that are invalid for use in hc
 * authority names.  This trims leading and trailing whitespace, and
 * substitutes any characters known to be bad.
 */
char *
disk_auth_clean(topo_mod_t *mod, const char *str)
{
	char *buf, *p;

	if (str == NULL)
		return (NULL);

	if ((buf = topo_mod_strdup(mod, str)) == NULL)
		return (NULL);

	while ((p = strpbrk(buf, " :=")) != NULL)
		*p = '-';

	return (buf);
}

/* create the disk topo node */
static int
disk_tnode_create(topo_mod_t *mod, tnode_t *parent,
    dev_di_node_t *dnode, const char *name, topo_instance_t i, tnode_t **dtnp)
{
	nvlist_t	*fmri;
	tnode_t		*dtn = NULL;
	nvlist_t	*auth;
	char		*mfg, *model, *part, *serial, *firm;

	*dtnp = NULL;
	if (dnode != NULL) {
		mfg = disk_auth_clean(mod, dnode->ddn_mfg);
		model = disk_auth_clean(mod, dnode->ddn_model);
		part = disk_auth_clean(mod, dnode->ddn_part);
		serial = disk_auth_clean(mod, dnode->ddn_serial);
		firm = disk_auth_clean(mod, dnode->ddn_firm);
	} else
		mfg = model = part = serial = firm = NULL;

	auth = topo_mod_auth(mod, parent);
	fmri = topo_mod_hcfmri(mod, parent, FM_HC_SCHEME_VERSION, name, i, NULL,
	    auth, part ? part : model, firm, serial);
	if (dnode != NULL && dnode->ddn_devid != NULL)
		(void) nvlist_add_string(fmri, FM_FMRI_HC_DEVID,
		    dnode->ddn_devid);
	nvlist_free(auth);

	topo_mod_strfree(mod, firm);
	topo_mod_strfree(mod, serial);
	topo_mod_strfree(mod, part);
	topo_mod_strfree(mod, model);
	topo_mod_strfree(mod, mfg);

	if (fmri == NULL) {
		topo_mod_dprintf(mod, "disk_tnode_create: "
		    "hcfmri (%s%d/%s%d) error %s\n",
		    topo_node_name(parent), topo_node_instance(parent),
		    name, i, topo_strerror(topo_mod_errno(mod)));
		return (-1);
	}

	if ((dtn = topo_node_bind(mod, parent, name, i, fmri)) == NULL) {
		if (topo_mod_errno(mod) == EMOD_NODE_BOUND) {
			/*
			 * if disk 0 is already there then we're done
			 */
			nvlist_free(fmri);
			return (0);
		}
		topo_mod_dprintf(mod, "disk_tnode_create: "
		    "bind (%s%d/%s%d) error %s\n",
		    topo_node_name(parent), topo_node_instance(parent),
		    name, i, topo_strerror(topo_mod_errno(mod)));
		nvlist_free(fmri);
		return (-1);
	}
	nvlist_free(fmri);

	/* add the properties of the disk */
	if (disk_set_props(mod, parent, dtn, dnode) != 0) {
		topo_mod_dprintf(mod, "disk_tnode_create: "
		    "disk_set_props (%s%d/%s%d) error %s\n",
		    topo_node_name(parent), topo_node_instance(parent),
		    name, i, topo_strerror(topo_mod_errno(mod)));
		topo_node_unbind(dtn);
		return (-1);
	}

	if (disk_cro_mk(mod, parent, dnode) != 0) {
		topo_mod_dprintf(mod, "disk_tnode_create: "
		    "disk_cro_mk (%s%d/%s%d) error %s\n",
		    topo_node_name(parent), topo_node_instance(parent),
		    name, i, topo_strerror(topo_mod_errno(mod)));
	}

	*dtnp = dtn;
	return (0);
}

static int
disk_declare(topo_mod_t *mod, tnode_t *parent, dev_di_node_t *dnode,
    tnode_t **childp)
{
	tnode_t		*dtn = NULL;
	int		rval;

	rval = disk_tnode_create(mod, parent, dnode, DISK, 0, &dtn);
	if (dtn == NULL) {
		if (rval == 0)
			return (0);
		topo_mod_dprintf(mod, "disk_declare: "
		    "disk_tnode_create error %s\n",
		    topo_strerror(topo_mod_errno(mod)));
		return (-1);
	}

	if (childp != NULL)
		*childp = dtn;
	return (0);
}

int
disk_declare_path(topo_mod_t *mod, tnode_t *parent, topo_list_t *listp,
    const char *path)
{
	dev_di_node_t		*dnode;
	int i;

	/*
	 * Check for match using physical phci (ddn_ppaths). Use
	 * di_devfs_path_match so generic.vs.non-generic names match.
	 */
	for (dnode = topo_list_next(listp); dnode != NULL;
	    dnode = topo_list_next(dnode)) {
		if (dnode->ddn_ppaths == NULL)
			continue;
		for (i = 0; i < dnode->ddn_ppaths_n; i++)
			if (di_devfs_path_match(dnode->ddn_ppaths[i], path))
				return (disk_declare(mod, parent, dnode, NULL));
	}

	topo_mod_dprintf(mod, "disk_declare_path: "
	    "failed to find disk matching path %s", path);

	/* could be non_enumerated or empty, choose non_enumerated */
	return (disk_declare_non_enumerated(mod, parent, NULL));
}

int
disk_declare_addr(topo_mod_t *mod, tnode_t *parent, topo_list_t *listp,
    const char *addr, tnode_t **childp)
{
	dev_di_node_t *dnode;
	int i;

	/* Check for match using addr. */
	for (dnode = topo_list_next(listp); dnode != NULL;
	    dnode = topo_list_next(dnode)) {
		if (dnode->ddn_target_ports == NULL)
			continue;
		for (i = 0; i < dnode->ddn_target_ports_n; i++) {
			if (dnode->ddn_target_ports[i] &&
			    (strncmp(dnode->ddn_target_ports[i], addr,
			    strcspn(dnode->ddn_target_ports[i], ":"))) == 0) {
				topo_mod_dprintf(mod, "disk_declare_addr: "
				    "found disk matching addr %s", addr);
				return (disk_declare(mod, parent, dnode,
				    childp));
			}
		}
	}

	topo_mod_dprintf(mod, "disk_declare_addr: "
	    "failed to find disk matching target_port addr %s", addr);

	/*
	 * caller may know about another target_port, so he is responsible
	 * for calling disk_declare_non_enumerated (or disk_devlare_empty)
	 * when all the specified target_port values fail.
	 */
	return (1);
}

/*
 * Used to declare a disk that has been discovered through other means (usually
 * ses), that is not enumerated in the solaris devinfo tree.
 */
int
disk_declare_non_enumerated(topo_mod_t *mod, tnode_t *parent, tnode_t **childp)
{
	return (disk_declare(mod, parent, NULL, childp));
}

/*
 * Used to declare an empty receptacle.
 */
int
disk_declare_empty(topo_mod_t *mod, tnode_t *parent)
{
	return (disk_cro_mk(mod, parent, NULL));
}

/*
 * This function adds a new item to a ddm the array-of-pointers-to-string.
 * An example call would look like:
 *	aps_add(mod, &dnode->ddn_lpaths, &dnode->ddn_lpaths_n, new);
 * We don't have topo_mod_realloc, so we need to do the realloc by hand.
 */
static int
aps_add(topo_mod_t *mod, char ***p_aps, int *p_n, const char *new)
{
	int	n = *p_n;	/* current number of elements */
	char	**aps;		/* new aps */

	if ((p_aps == NULL) || (p_n == NULL))
		return (-1);		/* bad call */
	aps = topo_mod_zalloc(mod, (n + 1) * sizeof (new));
	if (aps == NULL)
		return (-1);		/* out of memory */
	if (n && *p_aps) {
		bcopy(*p_aps, aps, n * sizeof (new));
		topo_mod_free(mod, *p_aps, n * sizeof (new));
	}
	aps[n] = new ? topo_mod_strdup(mod, new) : NULL;
	*p_aps = aps;
	*p_n = (n + 1);
	return (0);
}

static int
aps_findadd(topo_mod_t *mod, char ***p_aps, int *p_n, const char *new,
    int (*cmp)(const char *, const char *), int cmp_match)
{
	int	n = *p_n;
	int	i;

	if ((p_aps == NULL) || (p_n == NULL))
		return (-1);			/* bad call */
	for (i = 0; i < n; i++)
		if ((*cmp)((*p_aps)[i], new) == cmp_match)
			return (0);
	return (aps_add(mod, p_aps, p_n, new));
}

static void
aps_free(topo_mod_t *mod, char **aps, int n)
{
	int	i;

	if ((aps == NULL) || (n == 0))
		return;
	for (i = 0; i < n; i++)
		topo_mod_strfree(mod, aps[i]);
	topo_mod_free(mod, aps, n * sizeof (char *));
}

/* di_devlink callback to add an lpath to a dnode */
static int
disk_devlink_callback(di_devlink_t dl, void *arg)
{
	disk_cbdata_t	*cbp = (disk_cbdata_t *)arg;
	topo_mod_t	*mod = cbp->dcb_mod;
	dev_di_node_t	*dnode = cbp->dcb_dnode;
	const char	*devpath;
	char		*ctds, *slice;
	int		i, n;

	devpath = di_devlink_path(dl);
	if ((dnode == NULL) || (devpath == NULL))
		return (DI_WALK_TERMINATE);

	/* trim the dsk/rdsk and slice off the public name */
	if (((ctds = strrchr(devpath, '/')) != NULL) &&
	    ((slice = strrchr(ctds, 's')) != NULL))
		*slice = '\0';

	n = dnode->ddn_lpaths_n;
	for (i = 0; i < n; i++) {
		if (ctds && (strcmp(dnode->ddn_lpaths[i], ctds) == 0))
			break;
		if (devpath && (strcmp(dnode->ddn_lpaths[i], devpath) == 0))
			break;
	}
	if (i >= n)
		(void) aps_add(mod, &dnode->ddn_lpaths,
		    &dnode->ddn_lpaths_n, ctds ? ctds + 1 : devpath);

out:	if (ctds && slice)
		*slice = 's';
	return (DI_WALK_TERMINATE);
}

/* Add devinfo information to a dnode */
static int
dev_di_node_add_devinfo(dev_di_node_t *dnode, di_node_t node,
    disk_cbdata_t *cbp)
{
	topo_mod_t	*mod = cbp->dcb_mod;
	char		*dpath = NULL;
	di_minor_t	minor;
	char		*part;
	char		*minorpath;
	int64_t		*nblocksp;
	uint64_t	nblocks;
	int		*dblksizep;
	uint_t		dblksize;
	char		capbuf[MAXPATHLEN];
	int		*inq_dtypep;
	char		*s;
	int 		i;
	int		ret = -1;

	/* cache various bits of new information about the device. */
	if ((dnode->ddn_mfg == NULL) && di_prop_lookup_strings(DDI_DEV_T_ANY,
	    node, INQUIRY_VENDOR_ID, &s) > 0) {
		if ((dnode->ddn_mfg = disk_trim_whitespace(mod, s)) == NULL)
			goto error;
	}
	if ((dnode->ddn_model == NULL) && di_prop_lookup_strings(DDI_DEV_T_ANY,
	    node, INQUIRY_PRODUCT_ID, &s) > 0) {
		if ((dnode->ddn_model = disk_trim_whitespace(mod, s)) == NULL)
			goto error;
	}
	if ((dnode->ddn_part == NULL) && dnode->ddn_mfg && dnode->ddn_model) {
		/* form synthetic part number for disks */
		i = strlen(dnode->ddn_mfg) + 1 + strlen(dnode->ddn_model) + 1;
		if ((part = topo_mod_alloc(mod, i)) == NULL)
			goto error;
		(void) snprintf(part, i, "%s-%s",
		    dnode->ddn_mfg, dnode->ddn_model);
		dnode->ddn_part = part;
	}
	if ((dnode->ddn_serial == NULL) && di_prop_lookup_strings(DDI_DEV_T_ANY,
	    node, INQUIRY_SERIAL_NO, &s) > 0) {
		if ((dnode->ddn_serial = disk_trim_whitespace(mod, s)) == NULL)
			goto error;
	}
	if ((dnode->ddn_firm == NULL) && di_prop_lookup_strings(DDI_DEV_T_ANY,
	    node, INQUIRY_REVISION_ID, &s) > 0) {
		if ((dnode->ddn_firm = disk_trim_whitespace(mod, s)) == NULL)
			goto error;
	}
	if ((dnode->ddn_cap == NULL) && di_prop_lookup_int64(DDI_DEV_T_ANY,
	    node, "device-nblocks", &nblocksp) > 0) {
		nblocks = (uint64_t)*nblocksp;
		/*
		 * To save kernel memory, the driver may not define
		 * "device-blksize" when its value is default DEV_BSIZE.
		 */
		if (di_prop_lookup_ints(DDI_DEV_T_ANY, node,
		    "device-blksize", &dblksizep) > 0)
			dblksize = (uint_t)*dblksizep;
		else
			dblksize = DEV_BSIZE;		/* default value */
		(void) snprintf(capbuf, sizeof (capbuf),
		    "%" PRIu64, nblocks * dblksize);
		if ((dnode->ddn_cap = topo_mod_strdup(mod, capbuf)) == NULL)
			goto error;
	}
	if ((dnode->ddn_dtype == DTYPE_UNKNOWN) &&
	    di_prop_lookup_ints(DDI_DEV_T_ANY, node, "inquiry-device-type",
	    &inq_dtypep) > 0)
		dnode->ddn_dtype = *inq_dtypep;

	/* A devinfo node may add a dpath */
	dpath = di_devfs_path(node);
	if (dpath && aps_findadd(mod, &dnode->ddn_dpaths, &dnode->ddn_dpaths_n,
	    dpath, strcmp, 0))
		goto error;

	/* A non-scsi_vhci devinfo node may also be a ppath */
	if (dpath && (strstr(dpath, "/scsi_vhci/") == NULL) &&
	    aps_findadd(mod, &dnode->ddn_ppaths, &dnode->ddn_ppaths_n,
	    dpath, strcmp, 0))
		goto error;

	/*
	 * A devinfo node may also add an lpath, pick any minor name so
	 * that we don't have a dependency on a specific minor name having
	 * been created duringing attach.
	 */
	minor = di_minor_next(node, DI_MINOR_NIL);
	s = minor ? di_minor_name(minor) : NULL;
	if (dpath && s && ((dnode->ddn_dtype & DTYPE_MASK) == DTYPE_DIRECT)) {
		i = strlen(dpath) + 1 + strlen(s) + 1;
		if ((minorpath = topo_mod_alloc(mod, i)) == NULL)
			goto error;
		(void) snprintf(minorpath, i, "%s:%s", dpath, s);
		cbp->dcb_dnode = dnode;
		(void) di_devlink_walk(cbp->dcb_devhdl, "^r?dsk/",
		    minorpath, DI_PRIMARY_LINK, cbp, disk_devlink_callback);
		topo_mod_free(mod, minorpath, i);
	}

	/* A devinfo may add a target_port. */
	if (((di_prop_lookup_strings(DDI_DEV_T_ANY, node,
	    SCSI_ADDR_PROP_TARGET_PORT, &s)) == 1) &&
	    aps_findadd(mod, &dnode->ddn_target_ports,
	    &dnode->ddn_target_ports_n, scsi_wwnstr_skip_ua_prefix(s),
	    strcmp, 0))
		goto error;

	/* A devinfo may add a attached_port. */
	if (((di_prop_lookup_strings(DDI_DEV_T_ANY, node,
	    SCSI_ADDR_PROP_ATTACHED_PORT, &s)) == 1) &&
	    aps_findadd(mod, &dnode->ddn_attached_ports,
	    &dnode->ddn_attached_ports_n, scsi_wwnstr_skip_ua_prefix(s),
	    strcmp, 0))
		goto error;

	/* A devinfo may add a bridge_port. */
	if (((di_prop_lookup_strings(DDI_DEV_T_ANY, node,
	    SCSI_ADDR_PROP_BRIDGE_PORT, &s)) == 1) &&
	    aps_findadd(mod, &dnode->ddn_bridge_ports,
	    &dnode->ddn_bridge_ports_n, scsi_wwnstr_skip_ua_prefix(s),
	    strcmp, 0))
		goto error;
	ret = 0;

error:	if (dpath)
		di_devfs_path_free(dpath);
	return (ret);
}

/* Add pathinfo information to a dnode */
static int
dev_di_node_add_pathinfo(dev_di_node_t *dnode, di_path_t pnode,
    disk_cbdata_t *cbp)
{
	topo_mod_t	*mod = cbp->dcb_mod;
	char		*path;
	char		*s;

	/* A pathinfo may add a ppath */
	if (((path = di_path_devfs_path(pnode)) != NULL) &&
	    aps_findadd(mod, &dnode->ddn_ppaths, &dnode->ddn_ppaths_n, path,
	    strcmp, 0)) {
		di_devfs_path_free(path);
		goto error;
	}
	if (path)
		di_devfs_path_free(path);

	/* A pathinfo may add a target_port. */
	if (((di_path_prop_lookup_strings(pnode,
	    SCSI_ADDR_PROP_TARGET_PORT, &s)) == 1) &&
	    aps_findadd(mod, &dnode->ddn_target_ports,
	    &dnode->ddn_target_ports_n, scsi_wwnstr_skip_ua_prefix(s),
	    strcmp, 0))
		goto error;

	/* A pathinfo may add a attached_port. */
	if (((di_path_prop_lookup_strings(pnode,
	    SCSI_ADDR_PROP_ATTACHED_PORT, &s)) == 1) &&
	    aps_findadd(mod, &dnode->ddn_attached_ports,
	    &dnode->ddn_attached_ports_n, scsi_wwnstr_skip_ua_prefix(s),
	    strcmp, 0))
		goto error;

	/* A pathinfo may add a bridge_port. */
	if (((di_path_prop_lookup_strings(pnode,
	    SCSI_ADDR_PROP_BRIDGE_PORT, &s)) == 1) &&
	    aps_findadd(mod, &dnode->ddn_bridge_ports,
	    &dnode->ddn_bridge_ports_n, scsi_wwnstr_skip_ua_prefix(s),
	    strcmp, 0))
		goto error;

	return (0);

error:	return (-1);
}

static void
dev_di_node_free(topo_mod_t *mod, dev_di_node_t *dnode)
{
	if (dnode == NULL)
		return;

	/* free the stuff we point to */
	/* NOTE: topo_mod_strfree does NULL checking. */
	topo_mod_strfree(mod, dnode->ddn_devid);
	topo_mod_strfree(mod, dnode->ddn_mfg);
	topo_mod_strfree(mod, dnode->ddn_model);
	topo_mod_strfree(mod, dnode->ddn_part);
	topo_mod_strfree(mod, dnode->ddn_serial);
	topo_mod_strfree(mod, dnode->ddn_firm);
	topo_mod_strfree(mod, dnode->ddn_cap);

	/* free array of pointers to strings */
	aps_free(mod, dnode->ddn_dpaths, dnode->ddn_dpaths_n);
	aps_free(mod, dnode->ddn_ppaths, dnode->ddn_ppaths_n);
	aps_free(mod, dnode->ddn_lpaths, dnode->ddn_lpaths_n);
	aps_free(mod, dnode->ddn_target_ports, dnode->ddn_target_ports_n);
	aps_free(mod, dnode->ddn_attached_ports, dnode->ddn_attached_ports_n);
	aps_free(mod, dnode->ddn_bridge_ports, dnode->ddn_bridge_ports_n);

	/* free self */
	topo_mod_free(mod, dnode, sizeof (*dnode));
}

static int
dev_di_node_add(di_node_t node, char *devid, disk_cbdata_t *cbp)
{
	topo_mod_t	*mod = cbp->dcb_mod;
	dev_di_node_t	*dnode = NULL;
	int		dnode_new = 0;
	di_path_t	pnode;
	int		i;

	/*
	 * NOTE: if there is no devid, then we can end up with duplicate
	 * dnodes, but this doesn't do any harm.
	 */
	if (devid) {
		/* Check for duplicate using devid search. */
		for (dnode = topo_list_next(cbp->dcb_list);
		    dnode != NULL; dnode = topo_list_next(dnode)) {
			if (dnode->ddn_devid &&
			    devid_str_compare(dnode->ddn_devid, devid) == 0) {
				topo_mod_dprintf(mod, "dev_di_node_add: "
				    "already there %s\n", devid);
				break;
			}
		}
	}

	if (dnode == NULL) {
		dnode_new++;		/* Allocate/append a new dnode. */
		dnode = topo_mod_zalloc(mod, sizeof (*dnode));
		if (dnode == NULL)
			return (-1);

		dnode->ddn_dtype = DTYPE_UNKNOWN;
		if (devid) {
			dnode->ddn_devid = topo_mod_strdup(mod, devid);
			if (dnode->ddn_devid == NULL)
				goto error;
		}
	}

	/* Add devinfo information to dnode. */
	if (dev_di_node_add_devinfo(dnode, node, cbp))
		goto error;

	/*
	 * Establish the physical ppath and target ports. If the device is
	 * non-mpxio then dpath and ppath are the same, and the target port is a
	 * property of the device node.
	 *
	 * If dpath is a client node under scsi_vhci, then iterate over all
	 * paths and get their physical paths and target port properrties.
	 * di_path_client_next_path call below will
	 * return non-NULL, and ppath is set to the physical path to the first
	 * pathinfo node.
	 *
	 * NOTE: It is possible to get a generic.vs.non-generic path
	 * for di_devfs_path.vs.di_path_devfs_path like:
	 *    xml: /pci@7b,0/pci1022,7458@11/pci1000,3060@2/sd@2,0
	 *  pnode: /pci@7b,0/pci1022,7458@11/pci1000,3060@2/disk@2,0
	 * To resolve this issue disk_declare_path() needs to use the
	 * special di_devfs_path_match() interface.
	 */
	/* Add pathinfo information to dnode */
	for (pnode = di_path_client_next_path(node, NULL); pnode;
	    pnode = di_path_client_next_path(node, pnode))
		if (dev_di_node_add_pathinfo(dnode, pnode, cbp))
			goto error;

	/* Print some information about dnode for debug. */
	topo_mod_dprintf(mod, "dev_di_node_add: adding %s\n",
	    devid ? dnode->ddn_devid : "NULL devid");
	for (i = 0; i < dnode->ddn_dpaths_n; i++)
		topo_mod_dprintf(mod, "\t\tdpath %d: %s\n",
		    i, dnode->ddn_dpaths[i]);
	for (i = 0; i < dnode->ddn_ppaths_n; i++)
		topo_mod_dprintf(mod, "\t\tppath %d: %s\n",
		    i, dnode->ddn_ppaths[i]);
	for (i = 0; i < dnode->ddn_lpaths_n; i++)
		topo_mod_dprintf(mod, "\t\tlpath %d: %s\n",
		    i, dnode->ddn_lpaths[i]);

	if (dnode_new)
		topo_list_append(cbp->dcb_list, dnode);
	return (0);

error:	dev_di_node_free(mod, dnode);
	return (-1);
}

/* di_walk_node callback for disk_list_gather */
static int
dev_walk_di_nodes(di_node_t node, void *arg)
{
	char			*devidstr = NULL;
	char			*s;
	int			*val;

	/*
	 * If it's not a scsi_vhci client and doesn't have a target_port
	 * property and doesn't have a target property then it's not a storage
	 * device and we're not interested.
	 */
	if (di_path_client_next_path(node, NULL) == NULL &&
	    di_prop_lookup_strings(DDI_DEV_T_ANY, node,
	    SCSI_ADDR_PROP_TARGET_PORT, &s) <= 0 &&
	    di_prop_lookup_ints(DDI_DEV_T_ANY, node,
	    SCSI_ADDR_PROP_TARGET, &val) <= 0) {
		return (DI_WALK_CONTINUE);
	}
	(void) di_prop_lookup_strings(DDI_DEV_T_ANY, node,
	    DEVID_PROP_NAME, &devidstr);

	/* create/find the devid scsi topology node */
	(void) dev_di_node_add(node, devidstr, arg);

	return (DI_WALK_CONTINUE);
}

/*
 * NOTE: dev_list_gather should be moved out and done just after
 * di_init devinfo snapshot so that we only do this once per snapshot
 * instead of once per ses/internal-xml storage root.
 */
int
dev_list_gather(topo_mod_t *mod, topo_list_t *listp)
{
	di_node_t		devtree;
	di_devlink_handle_t	devhdl;
	disk_cbdata_t		dcb;

	if ((devtree = topo_mod_devinfo(mod)) == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "disk_list_gather: "
		    "topo_mod_devinfo() failed");
		return (-1);
	}

	/*
	 * NOTE: Until we can clean up (i.e. remove) topo dependencies on /dev
	 * public names, we ensure that name creation has completed by
	 * passing the DI_MAKE_LINK flag.  Doing this lets us ignore
	 * EC_DEV_ADD and EC_DEV_REMOVE events in fmd_dr.c code.
	 */
	if ((devhdl = di_devlink_init(NULL, DI_MAKE_LINK)) == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "disk_list_gather: "
		    "di_devlink_init() failed");
		return (-1);
	}

	dcb.dcb_mod = mod;
	dcb.dcb_list = listp;
	dcb.dcb_devhdl = devhdl;

	/* walk the devinfo snapshot looking for disk nodes */
	(void) di_walk_node(devtree, DI_WALK_CLDFIRST, &dcb,
	    dev_walk_di_nodes);

	(void) di_devlink_fini(&devhdl);

	return (0);
}

void
dev_list_free(topo_mod_t *mod, topo_list_t *listp)
{
	dev_di_node_t	*dnode;

	while ((dnode = topo_list_next(listp)) != NULL) {
		/* order of delete/free is important */
		topo_list_delete(listp, dnode);
		dev_di_node_free(mod, dnode);
	}
}

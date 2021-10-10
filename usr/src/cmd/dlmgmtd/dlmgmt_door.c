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
 * Main door handler functions used by dlmgmtd to process the different door
 * call requests. Door call requests can come from the user-land applications,
 * or from the kernel.
 *
 * Note on zones handling:
 *
 * There are two zoneids associated with a link.  One is the zone which
 * created the link (a.k.a owner_zoneid) and the other is the zone for which
 * the link is created for (e.g. automatic vnics) or the zone to which the
 * link is currently assigned (a.k.a target_zoneid). The two zoneids can be
 * same or different. Only the owner is allowed to make changes to the
 * datalink and delete it.
 *
 * Automatic vnics are directly created by the owner in the namespace
 * of the target zone. These links never exists in the namespace of the
 * owner. This allows multiple zones including the owner to have automatic
 * vnics with the same name. Today, only the GZ can create automatic vnics.
 *
 * Links can be created by a owner in its namespace and later assigned to
 * another zone. Such links are said to be onloan to the target zone and
 * they need to be re-assigned back to the owner when the target zone halts.
 * Today, only the GZ can assign links to another zone.
 *
 * Door clients can see links that were created by their zone (iptuns),
 * created in their zone (autovnics) by another zone, and links that are
 * currently assigned to their zone (onloan links). Door clients in a zone
 * can only modify links that were created by their zone. Exception: Door
 * clients in the GZ can pass the ALLZONES flag and view datalinks in all
 * zones.
 *
 * The datalink ID space is global, while each zone has its own datalink name
 * space.  This allows each zone to have complete freedom over the names that
 * they assign to links created within the zone.
 */

#include <assert.h>
#include <alloca.h>
#include <errno.h>
#include <priv_utils.h>
#include <stdlib.h>
#include <strings.h>
#include <syslog.h>
#include <sys/sysevent/eventdefs.h>
#include <unistd.h>
#include <zone.h>
#include <libsysevent.h>
#include <libdlmgmt.h>
#include <libdlflow.h>
#include <librcm.h>
#include <liblldp.h>
#include "dlmgmt_impl.h"

typedef void dlmgmt_door_handler_t(void *, void *, size_t *, zoneid_t,
    ucred_t *);

typedef struct dlmgmt_door_info_s {
	uint_t			di_cmd;
	size_t			di_reqsz;
	size_t			di_acksz;
	dlmgmt_door_handler_t	*di_handler;
} dlmgmt_door_info_t;

/*
 * Check if the caller has the required privileges to operate on a flow
 */
static int
dlmgmt_flow_checkprivs(ucred_t *cred)
{
	const priv_set_t *eset;

	eset = ucred_getprivset(cred, PRIV_EFFECTIVE);
	if (eset != NULL &&
	    priv_ismember(eset, PRIV_SYS_FLOW_CONFIG) ||
	    priv_ismember(eset, PRIV_SYS_DL_CONFIG) ||
	    priv_ismember(eset, PRIV_SYS_NET_CONFIG))
		return (0);
	return (EACCES);
}

/*
 * Check if the caller has the required privileges to operate on a link of the
 * given class.
 */
static int
dlmgmt_link_checkprivs(datalink_class_t class, ucred_t *cred)
{
	const priv_set_t *eset;

	eset = ucred_getprivset(cred, PRIV_EFFECTIVE);
	if (eset != NULL && ((class == DATALINK_CLASS_IPTUN &&
	    priv_ismember(eset, PRIV_SYS_IPTUN_CONFIG)) ||
	    priv_ismember(eset, PRIV_SYS_DL_CONFIG) ||
	    priv_ismember(eset, PRIV_SYS_NET_CONFIG)))
		return (0);
	return (EACCES);
}

static dlmgmt_link_t *
dlmgmt_getlink_by_dev(char *devname, zoneid_t zoneid)
{
	dlmgmt_link_t *linkp = avl_first(&dlmgmt_id_avl);

	for (; linkp != NULL; linkp = AVL_NEXT(&dlmgmt_id_avl, linkp)) {
		if (link_is_visible(linkp, zoneid, 0) &&
		    (linkp->ll_class == DATALINK_CLASS_PHYS) &&
		    objattr_equal(&(linkp->ll_head), FDEVNAME, devname,
		    strlen(devname) + 1)) {
			return (linkp);
		}
	}
	return (NULL);
}

/*
 * Post the EC_DATALINK sysevent for the given linkid. This sysevent will
 * be consumed by the datalink sysevent module.
 */
static void
dlmgmt_post_rcmsysevent(const char *subclass, datalink_id_t linkid,
    boolean_t reconfigured)
{
	nvlist_t	*nvl = NULL;
	sysevent_id_t	eid;
	int		err;

	if (((err = nvlist_alloc(&nvl, NV_UNIQUE_NAME_TYPE, 0)) != 0) ||
	    ((err = nvlist_add_uint64(nvl, RCM_NV_LINKID, linkid)) != 0) ||
	    ((err = nvlist_add_boolean_value(nvl, RCM_NV_RECONFIGURED,
	    reconfigured)) != 0)) {
		goto done;
	}

	if (sysevent_post_event(EC_DATALINK, (char *)subclass, SUNW_VENDOR,
	    (char *)progname, nvl, &eid) == -1) {
		err = errno;
	}

done:
	if (err != 0) {
		dlmgmt_log(LOG_WARNING,
		    "dlmgmt_post_rcmsysevent: (%d) failed: %s",
		    linkid, strerror(err));
	}
	nvlist_free(nvl);
}

static void
dlmgmt_post_lldpsysevent(datalink_id_t linkid, datalink_id_t vlinkid,
    uint16_t vid, boolean_t isprim, boolean_t added)
{
	nvlist_t	*nvl = NULL;
	sysevent_id_t	eid;
	int		err;

	if (((err = nvlist_alloc(&nvl, NV_UNIQUE_NAME_TYPE, 0)) != 0) ||
	    ((err = nvlist_add_uint32(nvl, LLDP_NV_LINKID, linkid)) != 0) ||
	    ((err = nvlist_add_uint32(nvl, LLDP_NV_VLINKID, vlinkid)) != 0) ||
	    ((err = nvlist_add_uint16(nvl, LLDP_NV_VID, vid)) != 0) ||
	    ((err = nvlist_add_boolean_value(nvl, LLDP_NV_ISPRIM,
	    isprim)) != 0) ||
	    ((err = nvlist_add_boolean_value(nvl, LLDP_NV_ADDED,
	    added)) != 0)) {
		goto done;
	}

	if (sysevent_post_event(EC_DATALINK, ESC_DATALINK_VLINK_UPDATE,
	    SUNW_VENDOR, (char *)progname, nvl, &eid) == -1) {
		err = errno;
	}

done:
	if (err != 0) {
		dlmgmt_log(LOG_WARNING,
		    "dlmgmt_post_lldpsysevent: (%d) failed: %s", linkid,
		    strerror(err));
	}
	nvlist_free(nvl);
}

/* ARGSUSED */
static void
dlmgmt_upcall_create(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_upcall_arg_create_t *create = argp;
	dlmgmt_create_retval_t	*retvalp = retp;
	datalink_class_t	class;
	uint32_t		media;
	boolean_t		novanity;
	dlmgmt_link_t		*linkp;
	char			link[MAXLINKNAMELEN];
	uint32_t		flags;
	int			err = 0;
	boolean_t		created = B_FALSE;
	boolean_t		reconfigured = B_FALSE;
	dlmgmt_obj_t		objp;

	/*
	 * Determine whether this link is persistent. Note that this request
	 * is coming from kernel so this link must be active.
	 */
	flags = DLMGMT_ACTIVE | (create->ld_persist ? DLMGMT_PERSIST : 0);

	class = create->ld_class;
	media = create->ld_media;
	novanity = create->ld_novanity;

	/*
	 * Hold the writer lock to update the link table.
	 */
	dlmgmt_table_lock(B_TRUE);

	if ((err = dlmgmt_link_checkprivs(class, cred)) != 0)
		goto done;

	/*
	 * Check to see whether this is the reattachment of an existing
	 * physical link. If so, return its linkid.
	 */
	if ((class == DATALINK_CLASS_PHYS) && (linkp =
	    dlmgmt_getlink_by_dev(create->ld_devname, zoneid)) != NULL) {
		if (objattr_equal(&(linkp->ll_head), FPHYMAJ,
		    &create->ld_phymaj, sizeof (uint64_t)) &&
		    objattr_equal(&(linkp->ll_head), FPHYINST,
		    &create->ld_phyinst, sizeof (uint64_t)) &&
		    (linkp->ll_flags & flags) == flags) {
			/*
			 * If nothing has been changed, directly return.
			 */
			goto noupdate;
		}

		err = objattr_set(&(linkp->ll_head), FPHYMAJ,
		    &create->ld_phymaj, sizeof (uint64_t), DLADM_TYPE_UINT64);
		if (err != 0)
			goto done;

		err = objattr_set(&(linkp->ll_head), FPHYINST,
		    &create->ld_phyinst, sizeof (uint64_t), DLADM_TYPE_UINT64);
		if (err != 0)
			goto done;

		/*
		 * This is a device that is dynamic reconfigured.
		 */
		if ((linkp->ll_flags & DLMGMT_ACTIVE) == 0)
			reconfigured = B_TRUE;

		if ((err = link_activate(linkp)) != 0)
			goto done;
		linkp->ll_flags |= flags;
		linkp->ll_gen++;

		goto done;
	}

	if ((err = dlmgmt_create_common(create->ld_devname, class, media,
	    novanity, zoneid, zoneid, flags, &linkp)) == EEXIST) {
		/*
		 * The link name already exists. Return error if this is a
		 * non-physical link (in that case, the link name must be
		 * the same as the given name).
		 */
		if (class != DATALINK_CLASS_PHYS)
			goto done;

		/*
		 * The physical link's name already exists, request
		 * a suggested link name: net<nextppa>
		 */
		err = dlmgmt_generate_name("net", link, MAXLINKNAMELEN, zoneid);
		if (err != 0)
			goto done;

		err = dlmgmt_create_common(link, class, media, novanity, zoneid,
		    zoneid, flags, &linkp);
	}

	if (err != 0)
		goto done;

	created = B_TRUE;

	dlmgmt_log(LOG_DEBUG, "dlmgmt_upcall_create: created new link %s",
	    linkp->ll_link);

	/*
	 * This is a new link.  Only need to persist link attributes for
	 * physical links.
	 */
	if (class == DATALINK_CLASS_PHYS &&
	    (((err = objattr_set(&linkp->ll_head, FDEVNAME, create->ld_devname,
	    strlen(create->ld_devname) + 1, DLADM_TYPE_STR)) != 0) ||
	    ((err = objattr_set(&linkp->ll_head, FPHYMAJ, &create->ld_phymaj,
	    sizeof (uint64_t), DLADM_TYPE_UINT64)) != 0) ||
	    ((err = objattr_set(&linkp->ll_head, FPHYINST, &create->ld_phyinst,
	    sizeof (uint64_t), DLADM_TYPE_UINT64)) != 0) ||
	    (strlen(linkp->ll_physloc) > 0 &&
	    (err = objattr_set(&linkp->ll_head, FPHYLOC, linkp->ll_physloc,
	    strlen(linkp->ll_physloc) + 1, DLADM_TYPE_STR)) != 0))) {
		(void) dlmgmt_destroy_common(linkp, flags);
	}

done:
	objp.olink = linkp;
	objp.otype = DLMGMT_OBJ_LINK;
	if ((err == 0) && ((err = dlmgmt_write_db_entry(linkp->ll_link, &objp,
	    linkp->ll_flags, NULL)) != 0) && created) {
		(void) dlmgmt_destroy_common(linkp, flags);
	}

noupdate:
	if (err == 0)
		retvalp->lr_linkid = linkp->ll_linkid;

	dlmgmt_table_unlock();

	if ((err == 0) && (class == DATALINK_CLASS_PHYS)) {
		/*
		 * Post the ESC_DATALINK_PHYS_ADD sysevent. This sysevent
		 * is consumed by the datalink sysevent module which in
		 * turn generates the RCM_RESOURCE_LINK_NEW RCM event.
		 */
		dlmgmt_post_rcmsysevent(ESC_DATALINK_PHYS_ADD,
		    retvalp->lr_linkid, reconfigured);
	}

	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_upcall_update(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_upcall_arg_update_t	*update = argp;
	dlmgmt_update_retval_t		*retvalp = retp;
	uint32_t			media = update->ld_media;
	dlmgmt_link_t			*linkp;
	int				err = 0;

	/*
	 * Hold the writer lock to update the link table.
	 */
	dlmgmt_table_lock(B_TRUE);

	/*
	 * Check to see whether this is the reattachment of an existing
	 * physical link. If so, return its linkid.
	 */
	if ((linkp = dlmgmt_getlink_by_dev(update->ld_devname, zoneid)) ==
	    NULL) {
		err = ENOENT;
		goto done;
	}

	if ((err = dlmgmt_link_checkprivs(linkp->ll_class, cred)) != 0)
		goto done;

	retvalp->lr_linkid = linkp->ll_linkid;
	retvalp->lr_media = media;
	if (linkp->ll_media != media && linkp->ll_media != DL_OTHER) {
		/*
		 * Assume a DL_ETHER link ce0, a DL_WIFI link ath0
		 * 1. # dladm rename-link ce0 net0
		 * 2. DR out ce0. net0 is down.
		 * 3. use rename-link to have the ath0 device inherit
		 *    the configuration from net0
		 *    # dladm rename-link ath0 net0
		 * 4. DR in ath0.
		 * As ath0 and ce0 do not have the same media type, ath0
		 * cannot inherit the configuration of net0.
		 */
		err = EEXIST;

		/*
		 * Return the media type of the existing link to indicate the
		 * reason for the name conflict.
		 */
		retvalp->lr_media = linkp->ll_media;
		goto done;
	}

	if (update->ld_novanity &&
	    (strcmp(update->ld_devname, linkp->ll_link) != 0)) {
		/*
		 * Return an error if this is a physical link that does not
		 * support vanity naming, but the link name is not the same
		 * as the given device name.
		 */
		err = EEXIST;
		goto done;
	}

	if (linkp->ll_media != media) {
		dlmgmt_obj_t	objp;

		linkp->ll_media = media;
		linkp->ll_gen++;
		objp.olink = linkp;
		objp.otype = DLMGMT_OBJ_LINK;
		(void) dlmgmt_write_db_entry(linkp->ll_link, &objp,
		    linkp->ll_flags, NULL);
	}

done:
	dlmgmt_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_upcall_destroy(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_upcall_arg_destroy_t	*destroy = argp;
	dlmgmt_destroy_retval_t		*retvalp = retp;
	datalink_id_t			linkid = destroy->ld_linkid;
	dlmgmt_link_t			*linkp = NULL;
	dlmgmt_obj_t			objp;
	uint32_t			flags, dflags = 0;
	int				err = 0;

	flags = DLMGMT_ACTIVE | (destroy->ld_persist ? DLMGMT_PERSIST : 0);

	/*
	 * Hold the writer lock to update the link table.
	 */
	dlmgmt_table_lock(B_TRUE);

	if ((linkp = link_by_id(linkid, zoneid)) == NULL) {
		err = ENOENT;
		goto done;
	}

	if ((err = dlmgmt_link_checkprivs(linkp->ll_class, cred)) != 0)
		goto done;

	objp.otype = DLMGMT_OBJ_LINK;
	objp.olink = linkp;
	if (((linkp->ll_flags & flags) & DLMGMT_ACTIVE) != 0) {
		if ((err = dlmgmt_delete_db_entry(&objp, DLMGMT_ACTIVE, NULL))
		    != 0)
			goto done;
		dflags |= DLMGMT_ACTIVE;
	}

	if (((linkp->ll_flags & flags) & DLMGMT_PERSIST) != 0) {
		if ((err = dlmgmt_delete_db_entry(&objp, DLMGMT_PERSIST, NULL))
		    != 0)
			goto done;
		dflags |= DLMGMT_PERSIST;
	}

	err = dlmgmt_destroy_common(linkp, flags);

done:
	if (err != 0 && dflags != 0)
		(void) dlmgmt_write_db_entry(linkp->ll_link, &objp, dflags,
		    NULL);

	dlmgmt_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_getname(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_getname_t	*getname = argp;
	dlmgmt_getname_retval_t	*retvalp = retp;
	dlmgmt_link_t		*linkp;
	int			err;

	/*
	 * Hold the reader lock to access the link
	 */
	dlmgmt_table_lock(B_FALSE);
	if ((linkp = link_by_id(getname->ld_linkid, zoneid)) == NULL) {
		dlmgmt_table_unlock();
		retvalp->lr_err = ENOENT;
		return;
	}

	err = dlmgmt_zonelinkname(linkp, zoneid, getname->ld_zoneprefix,
	    B_FALSE, retvalp->lr_link, sizeof (retvalp->lr_link));
	if (err == 0) {
		retvalp->lr_flags = linkp->ll_flags;
		retvalp->lr_class = linkp->ll_class;
		retvalp->lr_media = linkp->ll_media;
		retvalp->lr_zoneid = linkp->ll_zoneid;
		retvalp->lr_onloan = linkp->ll_onloan;
	}

	dlmgmt_table_unlock();
	retvalp->lr_err = err;
}

/*
 * Returns the datalink ID from the given link name. Supports zonename prefixed
 * linknames. link_by_name call performs the datalink lookup by name.
 */

/* ARGSUSED */
static void
dlmgmt_getlinkid(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_getlinkid_t	*getlinkid = argp;
	dlmgmt_getlinkid_retval_t *retvalp = retp;
	dlmgmt_link_t		*linkp = NULL;
	int			err = 0;

	/*
	 * Hold the reader lock to access the link
	 */
	dlmgmt_table_lock(B_FALSE);

	if ((linkp = link_by_name(getlinkid->ld_link, zoneid)) == NULL) {
		/*
		 * The link does not exist in caller zone nor in the
		 * specified zonename prefix as part of the linkname.
		 */
		err = ENOENT;
		goto done;
	}

	retvalp->lr_linkid = linkp->ll_linkid;
	retvalp->lr_flags = linkp->ll_flags;
	retvalp->lr_class = linkp->ll_class;
	retvalp->lr_media = linkp->ll_media;
	retvalp->lr_onloan = linkp->ll_onloan;
done:
	dlmgmt_table_unlock();
	retvalp->lr_err = err;
}

/* get phys prefix string */
/*ARGSUSED*/
static void
dlmgmt_getphys_prefix(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_getphys_prefix_retval_t	*retvalp = retp;
	char				phys_prefix[MAXLINKNAMELEN];

	phys_prefix[0] = '\0';
	(void) dlmgmt_smf_get_string_property(DLMGMT_FMRI, DLMGMT_LNP_PG,
	    DLMGMT_LNP_PHYS_PREFIX_PROP, phys_prefix, MAXLINKNAMELEN);
	(void) strlcpy(retvalp->lr_phys_prefix, phys_prefix, MAXLINKNAMELEN);
	retvalp->lr_err = 0;
}

/* get flow info from DB */
/* ARGSUSED */
static void
dlmgmt_flowgetnext(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_flowgetnext_t	*getflownext = argp;
	dlmgmt_flowgetnext_retval_t	*retvalp = retp;
	dlmgmt_flowconf_t		flow, *flowp;
	avl_index_t			tree_where, flow_where;
	int				err = 0;
	dlmgmt_flowavl_t		f_avl, *f_avlp;

	/* only global zone can query non-global zone's flow */
	if (zoneid != GLOBAL_ZONEID && getflownext->ld_zoneid != zoneid) {
		err = ENOTSUP;
		goto done;
	}

	f_avl.la_zoneid = getflownext->ld_zoneid;
	/*
	 * Hold the reader lock to access the flow table
	 */
	dlmgmt_flowconf_table_lock(B_FALSE);
	f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, &tree_where);
	if (f_avlp == NULL) {
		err = ENOENT;
		goto done;
	}

	if (getflownext->ld_flow[0] == '\0') {
		flowp = avl_first(&f_avlp->la_flowtree);
	} else {
		(void) strlcpy(flow.ld_flow, getflownext->ld_flow,
		    MAXFLOWNAMELEN);
		flowp = avl_find(&f_avlp->la_flowtree, &flow, &flow_where);
		if (flowp != NULL)
			flowp = AVL_NEXT(&f_avlp->la_flowtree, flowp);
	}

	if (flowp != NULL) {
		(void) strlcpy(retvalp->lr_name, flowp->ld_flow,
		    MAXFLOWNAMELEN);
		retvalp->lr_linkid = flowp->ld_linkid;
		retvalp->lr_zoneid = flowp->ld_zoneid;
	} else {
		bzero(retvalp->lr_name, MAXFLOWNAMELEN);
		err = ENOENT;
	}

done:
	dlmgmt_flowconf_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_getnext(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_getnext_t	*getnext = argp;
	dlmgmt_getnext_retval_t	*retvalp = retp;
	dlmgmt_link_t		link, *linkp;
	avl_index_t		where;
	int			err = 0;

	/*
	 * Hold the reader lock to access the link
	 */
	dlmgmt_table_lock(B_FALSE);

	link.ll_linkid = (getnext->ld_linkid + 1);
	if ((linkp = avl_find(&dlmgmt_id_avl, &link, &where)) == NULL)
		linkp = avl_nearest(&dlmgmt_id_avl, where, AVL_AFTER);

	for (; linkp != NULL; linkp = AVL_NEXT(&dlmgmt_id_avl, linkp)) {
		if (!link_is_visible(linkp, zoneid, getnext->ld_flags))
			continue;
		if ((linkp->ll_class & getnext->ld_class) &&
		    (linkp->ll_flags & getnext->ld_flags) &&
		    DATALINK_MEDIA_ACCEPTED(getnext->ld_dmedia,
		    linkp->ll_media))
			break;
	}

	if (linkp == NULL) {
		err = ENOENT;
		goto fail;
	}

	if (strlcpy(retvalp->lr_link, linkp->ll_link,
	    sizeof (retvalp->lr_link)) >= sizeof (retvalp->lr_link)) {
		err = ENOSPC;
		goto fail;
	}

	err = dlmgmt_zonelinkname(linkp, zoneid, getnext->ld_zoneprefix, B_TRUE,
	    retvalp->lr_zoneprefixlink, sizeof (retvalp->lr_zoneprefixlink));
	if (err != 0)
		goto fail;

	retvalp->lr_linkid = linkp->ll_linkid;
	retvalp->lr_class = linkp->ll_class;
	retvalp->lr_media = linkp->ll_media;
	retvalp->lr_flags = linkp->ll_flags;
	retvalp->lr_onloan = linkp->ll_onloan;
	retvalp->lr_zoneid = linkp->ll_zoneid;
fail:
	dlmgmt_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_getnextphysattr(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_getnextphysattr_t	*getnext = argp;
	dlmgmt_getnextphysattr_retval_t	*retvalp = retp;
	dlmgmt_link_t			*linkp;
	datalink_id_t			linkid = DATALINK_INVALID_LINKID;
	dlmgmt_physloc_t		*pl;
	int				err = 0;
	char				dev[MAXLINKNAMELEN];
	char				*pdev = NULL;
	uint32_t			flags = getnext->ld_flags;

	/*
	 * Hold the writer lock to access the link
	 */
	dlmgmt_table_lock(B_TRUE);

	if (strlen(getnext->ld_dev) > 0)
		pdev = getnext->ld_dev;

	for (;;) {
		pl = dlmgmt_physloc_get(pdev, B_TRUE,
		    (flags & DLADM_OPT_FORCE) != 0, NULL);
		if (pl == NULL) {
			err = ENOENT;
			goto fail;
		}
		flags &= ~DLADM_OPT_FORCE;
		linkp = dlmgmt_getlink_by_dev(pl->pl_dev, zoneid);
		if (linkp != NULL)
			break;
		/* If device is not visibile in this zone, skip it. */
		(void) strlcpy(dev, pl->pl_dev, sizeof (dev));
		dlmgmt_physloc_free(pl);
		pdev = dev;
	}

	if (strlcpy(retvalp->lr_dev, pl->pl_dev, sizeof (retvalp->lr_dev))
	    >= sizeof (retvalp->lr_dev))
		err = ENOSPC;
	else
		linkid = linkp->ll_linkid;

	dlmgmt_physloc_free(pl);

fail:
	dlmgmt_table_unlock();
	retvalp->lr_linkid = linkid;
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_reinit_phys(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_reinit_phys_t	*reinit = argp;
	dlmgmt_reinit_phys_retval_t	*retvalp = retp;

	dlmgmt_table_lock(B_TRUE);
	if ((retvalp->lr_err = dlmgmt_elevate_privileges()) != 0) {
		dlmgmt_table_unlock();
		return;
	}
	if ((retvalp->lr_err = dlmgmt_set_linkname_policy
	    (reinit->ld_phys_prefix, B_FALSE)) == 0) {
		dlmgmt_link_t *linkp, *nextp;
		dlmgmt_obj_t objp;

		for (linkp = avl_first(&dlmgmt_id_avl); linkp != NULL; ) {
			nextp = AVL_NEXT(&dlmgmt_id_avl, linkp);
			if (linkp->ll_class != DATALINK_CLASS_PHYS) {
				linkp = nextp;
				continue;
			}
			objp.otype = DLMGMT_OBJ_LINK;
			objp.olink = linkp;
			(void) dlmgmt_delete_db_entry(&objp, DLMGMT_ACTIVE,
			    NULL);
			(void) dlmgmt_delete_db_entry(&objp, DLMGMT_PERSIST,
			    NULL);
			(void) dlmgmt_destroy_common(linkp,
			    DLMGMT_ACTIVE|DLMGMT_PERSIST);
				linkp->ll_flags = 0;
			linkp = nextp;
		}
	}
	(void) dlmgmt_drop_privileges();
	dlmgmt_table_unlock();
}

/* ARGSUSED */
static void
dlmgmt_upcall_getattr(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_upcall_arg_getattr_t	*getattr = argp;
	dlmgmt_getattr_retval_t		*retvalp = retp;
	dlmgmt_link_t			*linkp;

	/*
	 * Hold the reader lock to access the link
	 */
	dlmgmt_table_lock(B_FALSE);
	if ((linkp = link_by_id(getattr->ld_linkid, zoneid)) == NULL) {
		retvalp->lr_err = ENOENT;
	} else {
		retvalp->lr_err = dlmgmt_getattr_common(&linkp->ll_head,
		    getattr->ld_attr, retvalp);
	}
	dlmgmt_table_unlock();
}

/*
 * Refer to notes on zones handling at the start of this
 * file for description of owner zoneid and target zoneid.
 */

/* ARGSUSED */
static void
dlmgmt_createid(void *argp, void *retp, size_t *sz,
    zoneid_t client_zoneid, ucred_t *cred)
{
	dlmgmt_door_createid_t	*createid = argp;
	dlmgmt_createid_retval_t *retvalp = retp;
	dlmgmt_link_t		*linkp;
	datalink_id_t		linkid = DATALINK_INVALID_LINKID;
	char			link[MAXLINKNAMELEN];
	zoneid_t		target_zoneid;
	int			err;

	/*
	 * Hold the writer lock to update the dlconf table.
	 */
	dlmgmt_table_lock(B_TRUE);

	/*
	 * Check if the client has explicitly specified a target zoneid,
	 * otherwise use the client's zoneid as the target zoneid.
	 */
	if (createid->ld_zoneid >= 0) {
		/*
		 * Right now, we only allow GZ to specify a different
		 * target zoneid.
		 */
		if (client_zoneid != GLOBAL_ZONEID &&
		    client_zoneid != createid->ld_zoneid) {
			err = EACCES;
			goto done;
		}
		target_zoneid = createid->ld_zoneid;
	} else {
		target_zoneid = client_zoneid;
	}

	if ((err = dlmgmt_link_checkprivs(createid->ld_class, cred)) != 0)
		goto done;

	if (createid->ld_prefix) {
		err = dlmgmt_generate_name(createid->ld_link, link,
		    MAXLINKNAMELEN, target_zoneid);
		if (err != 0)
			goto done;

		err = dlmgmt_create_common(link, createid->ld_class,
		    createid->ld_media, B_FALSE, client_zoneid, target_zoneid,
		    createid->ld_flags, &linkp);
	} else {
		err = dlmgmt_create_common(createid->ld_link,
		    createid->ld_class, createid->ld_media, B_FALSE,
		    client_zoneid, target_zoneid, createid->ld_flags, &linkp);
	}

	if (err == 0) {
		/*
		 * Keep the active mapping.
		 */
		linkid = linkp->ll_linkid;
		if (createid->ld_flags & DLMGMT_ACTIVE) {
			dlmgmt_obj_t	objp;

			objp.otype = DLMGMT_OBJ_LINK;
			objp.olink = linkp;
			(void) dlmgmt_write_db_entry(linkp->ll_link, &objp,
			    DLMGMT_ACTIVE, NULL);
		}
	}

done:
	dlmgmt_table_unlock();
	retvalp->lr_linkid = linkid;
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_destroyid(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_destroyid_t	*destroyid = argp;
	dlmgmt_destroyid_retval_t *retvalp = retp;
	datalink_id_t		linkid = destroyid->ld_linkid;
	uint32_t		flags = destroyid->ld_flags;
	dlmgmt_link_t		*linkp = NULL;
	int			err = 0;

	/*
	 * Hold the writer lock to update the link table.
	 */
	dlmgmt_table_lock(B_TRUE);
	if ((linkp = link_by_id(linkid, zoneid)) == NULL) {
		err = ENOENT;
		goto done;
	}

	if ((err = dlmgmt_link_checkprivs(linkp->ll_class, cred)) != 0)
		goto done;

	/*
	 * Delete the active mapping.
	 */
	if (flags & DLMGMT_ACTIVE) {
		dlmgmt_obj_t	objp;

		objp.otype = DLMGMT_OBJ_LINK;
		objp.olink = linkp;
		err = dlmgmt_delete_db_entry(&objp, DLMGMT_ACTIVE, NULL);
	}
	if (err == 0)
		err = dlmgmt_destroy_common(linkp, flags);
done:
	dlmgmt_table_unlock();
	retvalp->lr_err = err;
}

/*
 * Remap a linkid to a given link name, i.e., rename an existing link1
 * (ld_linkid) to a non-existent link2 (ld_link): rename link1's name to
 * the given link name.
 */
/* ARGSUSED */
static void
dlmgmt_remapid(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_remapid_t	*remapid = argp;
	dlmgmt_remapid_retval_t	*retvalp = retp;
	dlmgmt_link_t		*linkp;
	dlmgmt_obj_t		objp;
	char			oldname[MAXLINKNAMELEN];
	boolean_t		renamed = B_FALSE;
	int			err = 0;

	if (!dladm_valid_linkname(remapid->ld_link)) {
		retvalp->lr_err = EINVAL;
		return;
	}

	/*
	 * Hold the writer lock to update the link table.
	 */
	dlmgmt_table_lock(B_TRUE);
	if ((linkp = link_by_id(remapid->ld_linkid, zoneid)) == NULL) {
		err = ENOENT;
		goto done;
	}

	if ((err = dlmgmt_link_checkprivs(linkp->ll_class, cred)) != 0)
		goto done;

	if (link_by_name(remapid->ld_link, linkp->ll_zoneid) != NULL) {
		err = EEXIST;
		goto done;
	}

	(void) strlcpy(oldname, linkp->ll_link, MAXLINKNAMELEN);
	avl_remove(&dlmgmt_name_avl, linkp);
	(void) strlcpy(linkp->ll_link, remapid->ld_link, MAXLINKNAMELEN);
	avl_add(&dlmgmt_name_avl, linkp);
	renamed = B_TRUE;

	objp.olink = linkp;
	objp.otype = DLMGMT_OBJ_LINK;
	if (linkp->ll_flags & DLMGMT_ACTIVE) {
		err = dlmgmt_write_db_entry(oldname, &objp, DLMGMT_ACTIVE,
		    NULL);
		if (err != 0)
			goto done;
	}
	if (linkp->ll_flags & DLMGMT_PERSIST) {
		err = dlmgmt_write_db_entry(oldname, &objp, DLMGMT_PERSIST,
		    NULL);
		if (err != 0) {
			if (linkp->ll_flags & DLMGMT_ACTIVE) {
				(void) dlmgmt_write_db_entry(remapid->ld_link,
				    &objp, DLMGMT_ACTIVE, NULL);
			}
			goto done;
		}
	}

	dlmgmt_advance(linkp);
	linkp->ll_gen++;
done:
	if (err != 0 && renamed) {
		avl_remove(&dlmgmt_name_avl, linkp);
		(void) strlcpy(linkp->ll_link, oldname, MAXLINKNAMELEN);
		avl_add(&dlmgmt_name_avl, linkp);
	}
	dlmgmt_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_upid(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_upid_t	*upid = argp;
	dlmgmt_upid_retval_t	*retvalp = retp;
	dlmgmt_link_t		*linkp;
	int			err = 0;

	/*
	 * Hold the writer lock to update the link table.
	 */
	dlmgmt_table_lock(B_TRUE);
	if ((linkp = link_by_id(upid->ld_linkid, zoneid)) == NULL) {
		err = ENOENT;
		goto done;
	}

	if ((err = dlmgmt_link_checkprivs(linkp->ll_class, cred)) != 0)
		goto done;

	if (linkp->ll_flags & DLMGMT_ACTIVE) {
		err = EINVAL;
		goto done;
	}

	if ((err = link_activate(linkp)) == 0) {
		dlmgmt_obj_t	objp;

		objp.otype = DLMGMT_OBJ_LINK;
		objp.olink = linkp;
		(void) dlmgmt_write_db_entry(linkp->ll_link, &objp,
		    DLMGMT_ACTIVE, NULL);
	}
done:
	dlmgmt_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_createflowconf(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_createflowconf_t *createflowconf = argp;
	dlmgmt_createflowconf_retval_t *retvalp = retp;
	dlmgmt_flowconf_t	*flowconfp, flowconf;
	dlmgmt_link_t		*linkp;
	int			err;
	dlmgmt_flowavl_t	f_avl, *f_avlp;

	dlmgmt_log(LOG_DEBUG, "dlmgmt_createflowconf: flow name %s",
	    createflowconf->ld_flow);

	/*
	 * If the datalink is not assigned to this zone, cannot create flow on
	 * this datalink.
	 */
	if ((linkp = link_by_id(createflowconf->ld_linkid, zoneid)) == NULL) {
		err = ENOENT;
		goto done;
	}
	if (linkp->ll_zoneid != zoneid) {
		err = ENOTSUP;
		goto done;
	}

	if ((err = dlmgmt_flow_checkprivs(cred)) != 0) {
		retvalp->lr_err = err;
		return;
	}

	/*
	 * Hold the writer lock to update the objconf table.
	 */
	f_avl.la_zoneid = zoneid;
	dlmgmt_flowconf_table_lock(B_TRUE);
	f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
	/*
	 * create this zone's flow avl tree and insert it to the global flowconf
	 * avl
	 */
	if (f_avlp == NULL) {
		if ((err = flowconf_avl_create(zoneid, &f_avlp)) != 0)
			goto done;
		avl_add(&dlmgmt_flowconf_avl, f_avlp);
	}

	assert(f_avlp != NULL);
	(void) strlcpy(flowconf.ld_flow, createflowconf->ld_flow,
	    MAXFLOWNAMELEN);
	if ((flowconfp = avl_find(&f_avlp->la_flowtree, &flowconf, NULL)) !=
	    NULL) {
		err = EEXIST;
		goto done;
	}

	err = flowconf_create(flowconf.ld_flow,
	    createflowconf->ld_linkid, zoneid, &flowconfp);
	if (err == 0)
		avl_add(&f_avlp->la_flowtree, flowconfp);

done:
	dlmgmt_flowconf_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_createconf(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_createconf_t	*createconf = argp;
	dlmgmt_createconf_retval_t	*retvalp = retp;
	dlmgmt_dlconf_t			*dlconfp;
	int				err;

	/*
	 * Hold the writer lock to update the dlconf table.
	 */
	dlmgmt_dlconf_table_lock(B_TRUE);

	if ((err = dlmgmt_link_checkprivs(createconf->ld_class, cred)) != 0)
		goto done;

	err = dlconf_create(createconf->ld_link, createconf->ld_linkid,
	    createconf->ld_class, createconf->ld_media, zoneid, &dlconfp);
	if (err == 0) {
		avl_add(&dlmgmt_dlconf_avl, dlconfp);
		dlmgmt_advance_dlconfid(dlconfp);
		retvalp->lr_confid = dlconfp->ld_id;
	}
done:
	dlmgmt_dlconf_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_setflowattr(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_setflowattr_t	*flowattr = argp;
	dlmgmt_setflowattr_retval_t	*retvalp = retp;
	dlmgmt_flowconf_t		flowconf, *flowconfp;
	int				err = 0;
	dlmgmt_flowavl_t		f_avl, *f_avlp;

	dlmgmt_log(LOG_DEBUG,
	    "dlmgmt_setflowattr: flow name %s, attr name %s",
	    flowattr->ld_flow, flowattr->ld_attr);

	if ((err = dlmgmt_flow_checkprivs(cred)) != 0) {
		retvalp->lr_err = err;
		return;
	}

	/*
	 * Hold the writer lock to update the flowconf table.
	 */
	f_avl.la_zoneid = zoneid;
	dlmgmt_flowconf_table_lock(B_FALSE);
	f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
	if (f_avlp == NULL) {
		err = ENOENT;
		goto done;
	}

	(void) strlcpy(flowconf.ld_flow, flowattr->ld_flow, MAXFLOWNAMELEN);
	flowconfp = avl_find(&f_avlp->la_flowtree, &flowconf, NULL);
	if (flowconfp == NULL || zoneid != flowconfp->ld_zoneid) {
		err = ENOENT;
		goto done;
	}

	err = objattr_set(&(flowconfp->ld_head), flowattr->ld_attr,
	    &flowattr->ld_attrval, flowattr->ld_attrsz, flowattr->ld_type);
done:
	dlmgmt_flowconf_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_setattr(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_setattr_t	*setattr = argp;
	dlmgmt_setattr_retval_t	*retvalp = retp;
	dlmgmt_dlconf_t		dlconf, *dlconfp;
	int			err = 0;

	/*
	 * Hold the writer lock to update the dlconf table.
	 */
	dlmgmt_dlconf_table_lock(B_TRUE);

	dlconf.ld_id = setattr->ld_confid;
	dlconfp = avl_find(&dlmgmt_dlconf_avl, &dlconf, NULL);
	if (dlconfp == NULL || zoneid != dlconfp->ld_zoneid) {
		err = ENOENT;
		goto done;
	}

	if ((err = dlmgmt_link_checkprivs(dlconfp->ld_class, cred)) != 0)
		goto done;

	err = objattr_set(&(dlconfp->ld_head), setattr->ld_attr,
	    &setattr->ld_attrval, setattr->ld_attrsz, setattr->ld_type);

done:
	dlmgmt_dlconf_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_unsetconfattr(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_unsetattr_t		*unsetattr = argp;
	dlmgmt_unsetattr_retval_t	*retvalp = retp;
	dlmgmt_dlconf_t			dlconf, *dlconfp;
	int				err = 0;

	/*
	 * Hold the writer lock to update the dlconf table.
	 */
	dlmgmt_dlconf_table_lock(B_TRUE);

	dlconf.ld_id = unsetattr->ld_confid;
	dlconfp = avl_find(&dlmgmt_dlconf_avl, &dlconf, NULL);
	if (dlconfp == NULL || zoneid != dlconfp->ld_zoneid) {
		err = ENOENT;
		goto done;
	}

	if ((err = dlmgmt_link_checkprivs(dlconfp->ld_class, cred)) != 0)
		goto done;

	objattr_unset(&(dlconfp->ld_head), unsetattr->ld_attr);

done:
	dlmgmt_dlconf_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_writeflowconf(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_writeflowconf_t	*writeflowconf = argp;
	dlmgmt_writeflowconf_retval_t	*retvalp = retp;
	dlmgmt_flowconf_t		flowconf, *flowconfp;
	dlmgmt_flowavl_t		f_avl, *f_avlp;
	dlmgmt_obj_t			objp;
	int				err = 0;

	dlmgmt_log(LOG_DEBUG, "dlmgmt_writeflowconf: flow name %s",
	    writeflowconf->ld_flow);

	if ((err = dlmgmt_flow_checkprivs(cred)) != 0) {
		retvalp->lr_err = err;
		return;
	}

	f_avl.la_zoneid = zoneid;
	/*
	 * Hold the write lock to access the flowconf table.
	 */
	dlmgmt_flowconf_table_lock(B_TRUE);
	f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
	if (f_avlp == NULL) {
		err = ENOENT;
		goto done;
	}

	(void) strlcpy(flowconf.ld_flow, writeflowconf->ld_flow,
	    MAXFLOWNAMELEN);
	flowconfp = avl_find(&f_avlp->la_flowtree, &flowconf, NULL);
	if (flowconfp == NULL || zoneid != flowconfp->ld_zoneid) {
		err = ENOENT;
		goto done;
	}
	objp.otype = DLMGMT_OBJ_FLOW;
	objp.oflow = flowconfp;
	if (writeflowconf->ld_root[0] == '\0')
		err = dlmgmt_write_db_entry(writeflowconf->ld_flow, &objp,
		    DLMGMT_PERSIST, NULL);
	else
		err = dlmgmt_write_db_entry(writeflowconf->ld_flow, &objp,
		    DLMGMT_PERSIST, writeflowconf->ld_root);
done:
	dlmgmt_flowconf_table_unlock();
	retvalp->lr_err = err;
}

/*
 * Note that dlmgmt_openconf() returns a conf ID of a conf AVL tree entry,
 * which is managed by dlmgmtd.  The ID is used to find the conf entry when
 * dlmgmt_write_conf() is called.  The conf entry contains an ld_gen value
 * (which is the generation number - ll_gen) of the dlmgmt_link_t at the time
 * of dlmgmt_openconf(), and ll_gen changes every time the dlmgmt_link_t
 * changes its attributes.  Therefore, dlmgmt_write_conf() can compare ld_gen
 * in the conf entry against the latest dlmgmt_link_t ll_gen value to see if
 * anything has changed between the dlmgmt_openconf() and dlmgmt_writeconf()
 * calls.  If so, EAGAIN is returned.  This mechanism can ensures atomicity
 * across the pair of dladm_read_conf() and dladm_write_conf() calls.
 */
/* ARGSUSED */
static void
dlmgmt_writeconf(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_writeconf_t	*writeconf = argp;
	dlmgmt_writeconf_retval_t *retvalp = retp;
	dlmgmt_dlconf_t	dlconf, *dlconfp;
	dlmgmt_link_t		*linkp;
	dlmgmt_objattr_t	*attrp, *next;
	dlmgmt_obj_t		objp;
	int			err = 0;

	/*
	 * Hold the lock to access the dlconf table.
	 */
	dlmgmt_dlconf_table_lock(B_TRUE);

	dlconf.ld_id = writeconf->ld_confid;
	dlconfp = avl_find(&dlmgmt_dlconf_avl, &dlconf, NULL);
	if (dlconfp == NULL || zoneid != dlconfp->ld_zoneid) {
		err = ENOENT;
		goto done;
	}

	if ((err = dlmgmt_link_checkprivs(dlconfp->ld_class, cred)) != 0)
		goto done;

	/*
	 * Hold the writer lock to update the link table.
	 */
	dlmgmt_table_lock(B_TRUE);
	linkp = link_by_id(dlconfp->ld_linkid, zoneid);
	if ((linkp == NULL) || (linkp->ll_class != dlconfp->ld_class) ||
	    (linkp->ll_media != dlconfp->ld_media) ||
	    (strcmp(linkp->ll_link, dlconfp->ld_link) != 0)) {
		/*
		 * The link does not exist.
		 */
		dlmgmt_table_unlock();
		err = ENOENT;
		goto done;
	}

	if (linkp->ll_gen != dlconfp->ld_gen) {
		/*
		 * Something has changed the link configuration; try again.
		 */
		dlmgmt_table_unlock();
		err = EAGAIN;
		goto done;
	}

	/*
	 * Delete the old attribute list.
	 */
	for (attrp = linkp->ll_head; attrp != NULL; attrp = next) {
		next = attrp->lp_next;
		free(attrp->lp_val);
		free(attrp);
	}
	linkp->ll_head = NULL;

	/*
	 * Set the new attribute.
	 */
	for (attrp = dlconfp->ld_head; attrp != NULL; attrp = attrp->lp_next) {
		if ((err = objattr_set(&(linkp->ll_head), attrp->lp_name,
		    attrp->lp_val, attrp->lp_sz, attrp->lp_type)) != 0) {
			dlmgmt_table_unlock();
			goto done;
		}
	}

	linkp->ll_gen++;
	objp.otype = DLMGMT_OBJ_LINK;
	objp.olink = linkp;
	err = dlmgmt_write_db_entry(linkp->ll_link, &objp, DLMGMT_PERSIST,
	    NULL);
	dlmgmt_table_unlock();
done:
	dlmgmt_dlconf_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_removeflowconf(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_removeflowconf_t 	*removeflowconf = argp;
	dlmgmt_removeflowconf_retval_t	*retvalp = retp;
	dlmgmt_flowconf_t		*flowp, flowconf;
	dlmgmt_flowavl_t		f_avl, *f_avlp;
	dlmgmt_obj_t			objp;
	int				err;

	if ((err = dlmgmt_flow_checkprivs(cred)) != 0) {
		retvalp->lr_err = err;
		return;
	}

	dlmgmt_flowconf_table_lock(B_TRUE);
	f_avl.la_zoneid = zoneid;
	f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
	if (f_avlp == NULL) {
		err = ENOENT;
		goto done;
	}

	(void) strlcpy(flowconf.ld_flow, removeflowconf->ld_flow,
	    MAXFLOWNAMELEN);
	if ((flowp = avl_find(&f_avlp->la_flowtree, &flowconf, NULL)) == NULL) {
		err = ENOENT;
		goto done;
	}

	/* if the datalink is assigned to a ngz, gz cannot remove the flow */
	if ((zoneid == GLOBAL_ZONEID) &&
	    (link_by_id(flowp->ld_linkid, zoneid) == NULL)) {
		err = ENOENT;
		goto done;
	}

	objp.otype = DLMGMT_OBJ_FLOW;
	objp.oflow = flowp;
	err = dlmgmt_delete_db_entry(&objp, DLMGMT_PERSIST,
	    removeflowconf->ld_root);
	avl_remove(&f_avlp->la_flowtree, flowp);
	flowconf_destroy(flowp);

done:
	dlmgmt_flowconf_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_removeconf(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_removeconf_t 	*removeconf = argp;
	dlmgmt_removeconf_retval_t	*retvalp = retp;
	dlmgmt_link_t			*linkp;
	dlmgmt_obj_t			objp;
	int				err;

	dlmgmt_table_lock(B_TRUE);
	if ((linkp = link_by_id(removeconf->ld_linkid, zoneid)) == NULL) {
		err = ENOENT;
		goto done;
	}
	if (zoneid != GLOBAL_ZONEID && linkp->ll_onloan) {
		/*
		 * A non-global zone cannot remove the persistent
		 * configuration of a link that is on loan from the global
		 * zone.
		 */
		err = EACCES;
		goto done;
	}
	if ((err = dlmgmt_link_checkprivs(linkp->ll_class, cred)) != 0)
		goto done;

	objp.otype = DLMGMT_OBJ_LINK;
	objp.olink = linkp;
	err = dlmgmt_delete_db_entry(&objp, DLMGMT_PERSIST, NULL);

done:
	dlmgmt_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_destroyconf(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_destroyconf_t	*destroyconf = argp;
	dlmgmt_destroyconf_retval_t	*retvalp = retp;
	dlmgmt_dlconf_t			dlconf, *dlconfp;
	int				err = 0;

	/*
	 * Hold the writer lock to update the dlconf table.
	 */
	dlmgmt_dlconf_table_lock(B_TRUE);

	dlconf.ld_id = destroyconf->ld_confid;
	dlconfp = avl_find(&dlmgmt_dlconf_avl, &dlconf, NULL);
	if (dlconfp == NULL || zoneid != dlconfp->ld_zoneid) {
		err = ENOENT;
		goto done;
	}

	if ((err = dlmgmt_link_checkprivs(dlconfp->ld_class, cred)) != 0)
		goto done;

	avl_remove(&dlmgmt_dlconf_avl, dlconfp);
	dlconf_destroy(dlconfp);

done:
	dlmgmt_dlconf_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_readflowconf(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_readflowconf_t	*readflowconf = argp;
	dlmgmt_readflowconf_retval_t	*retvalp = retp;
	dlmgmt_flowconf_t		flowconf, *flowp;
	dlmgmt_flowavl_t		f_avl, *f_avlp;
	int				err = 0;

	if ((err = dlmgmt_flow_checkprivs(cred)) != 0)
		goto done;
	if (zoneid != GLOBAL_ZONEID && zoneid != readflowconf->ld_zoneid) {
		err = ENOENT;
		goto done;
	}

	f_avl.la_zoneid = readflowconf->ld_zoneid;
	dlmgmt_flowconf_table_lock(B_FALSE);
	f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
	if (f_avlp == NULL) {
		err = ENOENT;
		goto done;
	}

	(void) strlcpy(flowconf.ld_flow, readflowconf->ld_flow, MAXFLOWNAMELEN);
	flowp = avl_find(&f_avlp->la_flowtree, &flowconf, NULL);
	if (flowp == NULL)
		err = ENOENT;
	else
		retvalp->lr_onloan = flowp->ld_onloan;

done:
	dlmgmt_flowconf_table_unlock();
	retvalp->lr_err = err;
}

/*
 * dlmgmt_openconf() returns a handle of the current configuration, which
 * is then used to update the configuration by dlmgmt_writeconf(). Therefore,
 * it requires privileges.
 *
 * Further, please see the comments above dladm_write_conf() to see how
 * ld_gen is used to ensure atomicity across the {dlmgmt_openconf(),
 * dlmgmt_writeconf()} pair.
 */
/* ARGSUSED */
static void
dlmgmt_openconf(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_openconf_t	*openconf = argp;
	dlmgmt_openconf_retval_t *retvalp = retp;
	dlmgmt_link_t 		*linkp;
	datalink_id_t		linkid = openconf->ld_linkid;
	dlmgmt_dlconf_t		*dlconfp;
	dlmgmt_objattr_t	*attrp;
	int			err = 0;

	/*
	 * Hold the writer lock to update the dlconf table.
	 */
	dlmgmt_dlconf_table_lock(B_TRUE);

	/*
	 * Hold the reader lock to access the link
	 */
	dlmgmt_table_lock(B_FALSE);
	linkp = link_by_id(linkid, zoneid);
	if ((linkp == NULL) || !(linkp->ll_flags & DLMGMT_PERSIST)) {
		/* The persistent link configuration does not exist. */
		err = ENOENT;
		goto done;
	}
	if (linkp->ll_onloan && zoneid != GLOBAL_ZONEID) {
		/*
		 * The caller is in a non-global zone and the persistent
		 * configuration belongs to the global zone.
		 */
		err = EACCES;
		goto done;
	}

	if ((err = dlmgmt_link_checkprivs(linkp->ll_class, cred)) != 0)
		goto done;

	if ((err = dlconf_create(linkp->ll_link, linkp->ll_linkid,
	    linkp->ll_class, linkp->ll_media, zoneid, &dlconfp)) != 0)
		goto done;

	for (attrp = linkp->ll_head; attrp != NULL; attrp = attrp->lp_next) {
		if ((err = objattr_set(&(dlconfp->ld_head), attrp->lp_name,
		    attrp->lp_val, attrp->lp_sz, attrp->lp_type)) != 0) {
			dlconf_destroy(dlconfp);
			goto done;
		}
	}
	dlconfp->ld_gen = linkp->ll_gen;
	avl_add(&dlmgmt_dlconf_avl, dlconfp);
	dlmgmt_advance_dlconfid(dlconfp);

	retvalp->lr_confid = dlconfp->ld_id;
done:
	dlmgmt_table_unlock();
	dlmgmt_dlconf_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_getflowattr(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_getflowattr_t	*getflowattr = argp;
	dlmgmt_getattr_retval_t		*retvalp = retp;
	dlmgmt_flowconf_t		flowconf, *flowconfp;
	dlmgmt_flowavl_t		f_avl, *f_avlp;

	/*
	 * Hold the read lock to access the flowconf table.
	 */
	f_avl.la_zoneid = getflowattr->ld_zoneid;
	dlmgmt_flowconf_table_lock(B_FALSE);
	f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
	if (f_avlp == NULL) {
		retvalp->lr_err = ENOENT;
		goto done;
	}

	(void) strlcpy(flowconf.ld_flow, getflowattr->ld_flow, MAXFLOWNAMELEN);
	if ((flowconfp = avl_find(&f_avlp->la_flowtree, &flowconf, NULL)) ==
	    NULL ||
	    (zoneid != GLOBAL_ZONEID && zoneid != flowconfp->ld_zoneid)) {
		retvalp->lr_err = ENOENT;
	} else {
		retvalp->lr_err = dlmgmt_getattr_common(&flowconfp->ld_head,
		    getflowattr->ld_attr, retvalp);
	}

done:
	dlmgmt_flowconf_table_unlock();
}

/*
 * dlmgmt_getconfsnapshot() returns a read-only snapshot of all the
 * configuration, and requires no privileges.
 *
 * If the given size cannot hold all the configuration, set the size
 * that is needed, and return ENOSPC.
 */
/* ARGSUSED */
static void
dlmgmt_getconfsnapshot(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_getconfsnapshot_t	*snapshot = argp;
	dlmgmt_getconfsnapshot_retval_t	*retvalp = retp;
	dlmgmt_link_t 			*linkp;
	datalink_id_t			linkid = snapshot->ld_linkid;
	dlmgmt_objattr_t		*attrp;
	char				*buf;
	size_t				nvlsz;
	nvlist_t			*nvl = NULL;
	int				err = 0;

	assert(*sz >= sizeof (dlmgmt_getconfsnapshot_retval_t));

	/*
	 * Hold the reader lock to access the link
	 */
	dlmgmt_table_lock(B_FALSE);
	linkp = link_by_id(linkid, zoneid);
	if ((linkp == NULL) || !(linkp->ll_flags & DLMGMT_PERSIST)) {
		/* The persistent link configuration does not exist. */
		err = ENOENT;
		goto done;
	}
	if (zoneid != GLOBAL_ZONEID && zoneid != linkp->ll_zoneid) {
		/*
		 * The caller is in a non-global zone and is trying to access
		 * information that doesn't belong to the caller zone.
		 */
		err = EACCES;
		goto done;
	}

	err = nvlist_alloc(&nvl, NV_UNIQUE_NAME_TYPE, 0);
	if (err != 0)
		goto done;

	for (attrp = linkp->ll_head; attrp != NULL; attrp = attrp->lp_next) {
		if ((err = nvlist_add_byte_array(nvl, attrp->lp_name,
		    attrp->lp_val, attrp->lp_sz)) != 0) {
			goto done;
		}
	}

	if ((err = nvlist_size(nvl, &nvlsz, NV_ENCODE_NATIVE)) != 0)
		goto done;

	if (nvlsz + sizeof (dlmgmt_getconfsnapshot_retval_t) > *sz) {
		*sz = nvlsz + sizeof (dlmgmt_getconfsnapshot_retval_t);
		err = ENOSPC;
		goto done;
	}

	/*
	 * pack the the nvlist into the return value.
	 */
	*sz = nvlsz + sizeof (dlmgmt_getconfsnapshot_retval_t);
	retvalp->lr_nvlsz = nvlsz;
	buf = (char *)retvalp + sizeof (dlmgmt_getconfsnapshot_retval_t);
	err = nvlist_pack(nvl, &buf, &nvlsz, NV_ENCODE_NATIVE, 0);

done:
	dlmgmt_table_unlock();
	nvlist_free(nvl);
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_getattr(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_getattr_t	*getattr = argp;
	dlmgmt_getattr_retval_t	*retvalp = retp;
	dlmgmt_dlconf_t		dlconf, *dlconfp;
	int			err;

	/*
	 * Hold the read lock to access the dlconf table.
	 */
	dlmgmt_dlconf_table_lock(B_FALSE);

	dlconf.ld_id = getattr->ld_confid;
	if ((dlconfp = avl_find(&dlmgmt_dlconf_avl, &dlconf, NULL)) == NULL ||
	    zoneid != dlconfp->ld_zoneid) {
		retvalp->lr_err = ENOENT;
	} else {
		if ((err = dlmgmt_link_checkprivs(dlconfp->ld_class, cred))
		    != 0) {
			retvalp->lr_err = err;
		} else {
			retvalp->lr_err = dlmgmt_getattr_common(
			    &dlconfp->ld_head, getattr->ld_attr, retvalp);
		}
	}

	dlmgmt_dlconf_table_unlock();
}

/* ARGSUSED */
static void
dlmgmt_upcall_linkprop_init(void *argp, void *retp, size_t *sz,
    zoneid_t zoneid, ucred_t *cred)
{
	dlmgmt_door_linkprop_init_t	*lip = argp;
	dlmgmt_linkprop_init_retval_t	*retvalp = retp;
	dlmgmt_link_t			*linkp;
	int				err;

	dlmgmt_table_lock(B_FALSE);
	if ((linkp = link_by_id(lip->ld_linkid, zoneid)) == NULL)
		err = ENOENT;
	else
		err = dlmgmt_link_checkprivs(linkp->ll_class, cred);
	dlmgmt_table_unlock();

	if (err == 0) {
		dladm_status_t	s;
		char		buf[DLADM_STRSIZE];

		s = dladm_init_linkprop(dld_handle, lip->ld_linkid, B_TRUE);
		if (s != DLADM_STATUS_OK) {
			dlmgmt_log(LOG_WARNING,
			    "linkprop initialization failed on link %d: %s",
			    lip->ld_linkid, dladm_status2str(s, buf));
			err = EINVAL;
		}
	}
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_setzoneid(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_setzoneid_t	*setzoneid = argp;
	dlmgmt_setzoneid_retval_t *retvalp = retp;
	dlmgmt_link_t		*linkp;
	dlmgmt_link_t		*otherlinkp;
	datalink_id_t		linkid = setzoneid->ld_linkid;
	zoneid_t		oldzoneid, newzoneid;
	int			err = 0;
	dlmgmt_flowconf_t	*flowp = NULL, *pre_flowp = NULL;

	dlmgmt_table_lock(B_TRUE);

	/* We currently only allow changing zoneid's from the global zone. */
	if (zoneid != GLOBAL_ZONEID) {
		err = EACCES;
		goto done;
	}

	if ((linkp = link_by_id(linkid, zoneid)) == NULL) {
		err = ENOENT;
		goto done;
	}

	if ((err = dlmgmt_link_checkprivs(linkp->ll_class, cred)) != 0)
		goto done;

	/* We can only assign an active link to a zone. */
	if (!(linkp->ll_flags & DLMGMT_ACTIVE)) {
		err = EINVAL;
		goto done;
	}

	oldzoneid = linkp->ll_zoneid;
	newzoneid = setzoneid->ld_zoneid;
	if (oldzoneid == newzoneid)
		goto done;

	/*
	 * Before we remove the link from its current zone, make sure that
	 * there isn't a link with the same name in the destination zone.
	 */
	if ((otherlinkp = link_by_name(linkp->ll_link, newzoneid)) != NULL &&
	    otherlinkp->ll_linkid != linkid) {
		err = EEXIST;
		goto done;
	}

	if (oldzoneid != GLOBAL_ZONEID) {
		if (zone_remove_datalink(oldzoneid, linkid) != 0) {
			err = errno;
			dlmgmt_log(LOG_WARNING, "unable to remove link %d from "
			    "zone %d: %s", linkid, oldzoneid, strerror(err));
			goto done;
		}
		avl_remove(&dlmgmt_loan_avl, linkp);
		linkp->ll_onloan = B_FALSE;

		/* set flow's fd_onloan to B_FALSE */
		flowp = flow_by_linkid(linkp->ll_linkid, newzoneid, pre_flowp);
		while ((flowp != NULL) && (flowp->ld_onloan == B_TRUE)) {
			dlmgmt_flowavl_t f_avl, *f_avlp;
			dlmgmt_flowconf_t *newflow, flow_to_rm;

			/* change flow's onloan value in the new zone */
			f_avl.la_zoneid = newzoneid;
			f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
			assert(f_avlp != NULL);
			flowp->ld_onloan = B_FALSE;

			/* remove flow from the old zone */
			f_avl.la_zoneid = oldzoneid;
			f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
			assert(f_avlp != NULL);
			(void) snprintf(flow_to_rm.ld_flow, MAXFLOWNAMELEN,
			    "%s/%s", "global", flowp->ld_flow);
			newflow = avl_find(&f_avlp->la_flowtree, &flow_to_rm,
			    NULL);
			if (newflow != NULL)
				avl_remove(&f_avlp->la_flowtree, newflow);
			pre_flowp = flowp;
			flowp = flow_by_linkid(linkp->ll_linkid,
			    linkp->ll_zoneid, pre_flowp);
		}
	}
	if (newzoneid != GLOBAL_ZONEID) {
		if (zone_add_datalink(newzoneid, linkid) != 0) {
			err = errno;
			dlmgmt_log(LOG_WARNING, "unable to add link %d to zone "
			    "%d: %s", linkid, newzoneid, strerror(err));
			(void) zone_add_datalink(oldzoneid, linkid);
			goto done;
		}
		avl_add(&dlmgmt_loan_avl, linkp);
		linkp->ll_onloan = B_TRUE;

		/* set flow's fd_onloan to B_TRUE */
		flowp = flow_by_linkid(linkp->ll_linkid, oldzoneid, pre_flowp);
		while (flowp != NULL) {
			dlmgmt_flowavl_t f_avl, *f_avlp;
			dlmgmt_flowconf_t *newflow;

			/* change flow's onloan value in the old zone */
			flowp->ld_onloan = B_TRUE;

			/* add flow to the new zone */
			f_avl.la_zoneid = newzoneid;
			f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
			if (f_avlp == NULL) {
				(void) flowconf_avl_create(newzoneid, &f_avlp);
				if (f_avlp != NULL)
					avl_add(&dlmgmt_flowconf_avl, f_avlp);
			}
			assert(f_avlp != NULL);

			newflow = calloc(1, sizeof (*newflow));
			if (newflow == NULL) {
				err = ENOMEM;
				break;
			}
			bcopy(flowp, newflow, sizeof (*newflow));
			/*
			 * The new flowconf added to the ngz's avl tree keeps
			 * the global zone's id, and onloan is set to B_TRUE
			 */
			(void) snprintf(newflow->ld_flow, MAXFLOWNAMELEN,
			    "%s/%s", "global", flowp->ld_flow);
			avl_add(&f_avlp->la_flowtree, newflow);

			/* add global kstat to non-global zone */
			(void) dladm_flow_create_ngz_kstat(dld_handle,
			    flowp->ld_flow, newzoneid);

			pre_flowp = flowp;
			flowp = flow_by_linkid(linkp->ll_linkid,
			    linkp->ll_zoneid, pre_flowp);
		}
	}

	avl_remove(&dlmgmt_name_avl, linkp);
	linkp->ll_zoneid = newzoneid;
	avl_add(&dlmgmt_name_avl, linkp);

done:
	dlmgmt_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_zoneboot(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	int			err;
	dlmgmt_door_zoneboot_t	*zoneboot = argp;
	dlmgmt_zoneboot_retval_t *retvalp = retp;

	dlmgmt_table_lock(B_TRUE);
	if ((err = dlmgmt_link_checkprivs(0, cred)) != 0)
		goto done;

	if (zoneid != GLOBAL_ZONEID) {
		err = EACCES;
		goto done;
	}
	if (zoneboot->ld_zoneid == GLOBAL_ZONEID) {
		err = EINVAL;
		goto done;
	}

	if ((err = dlmgmt_elevate_privileges()) == 0) {
		err = dlmgmt_zone_init(zoneboot->ld_zoneid);
		(void) dlmgmt_drop_privileges();
	}
done:
	dlmgmt_table_unlock();
	retvalp->lr_err = err;
}

/* ARGSUSED */
static void
dlmgmt_zonehalt(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	int			err = 0;
	dlmgmt_door_zonehalt_t	*zonehalt = argp;
	dlmgmt_zonehalt_retval_t *retvalp = retp;

	if ((err = dlmgmt_link_checkprivs(0, cred)) == 0) {
		if (zoneid != GLOBAL_ZONEID) {
			err = EACCES;
		} else if (zonehalt->ld_zoneid == GLOBAL_ZONEID) {
			err = EINVAL;
		} else {
			dlmgmt_table_lock(B_TRUE);
			dlmgmt_db_fini(zonehalt->ld_zoneid);
			dlmgmt_table_unlock();
		}
	}
	retvalp->lr_err = err;
}

/*
 * Only used to send an event to the datalink module to signify
 * creation/deletion of a virtual link.
 */
/* ARGSUSED */
static void
dlmgmt_update_vlink(void *argp, void *retp, size_t *sz, zoneid_t zoneid,
    ucred_t *cred)
{
	dlmgmt_door_vlink_update_t	*uvlink = argp;
	dlmgmt_retval_t			*retvalp = retp;

	dlmgmt_post_lldpsysevent(uvlink->ld_linkid, uvlink->ld_vlinkid,
	    uvlink->ld_vid, uvlink->ld_isprim, uvlink->ld_added);
	retvalp->lr_err = 0;
}

static dlmgmt_door_info_t i_dlmgmt_door_info_tbl[] = {
	{ DLMGMT_CMD_DLS_CREATE, sizeof (dlmgmt_upcall_arg_create_t),
	    sizeof (dlmgmt_create_retval_t), dlmgmt_upcall_create },
	{ DLMGMT_CMD_DLS_GETATTR, sizeof (dlmgmt_upcall_arg_getattr_t),
	    sizeof (dlmgmt_getattr_retval_t), dlmgmt_upcall_getattr },
	{ DLMGMT_CMD_DLS_DESTROY, sizeof (dlmgmt_upcall_arg_destroy_t),
	    sizeof (dlmgmt_destroy_retval_t), dlmgmt_upcall_destroy },
	{ DLMGMT_CMD_GETNAME, sizeof (dlmgmt_door_getname_t),
	    sizeof (dlmgmt_getname_retval_t), dlmgmt_getname },
	{ DLMGMT_CMD_GETLINKID, sizeof (dlmgmt_door_getlinkid_t),
	    sizeof (dlmgmt_getlinkid_retval_t), dlmgmt_getlinkid },
	{ DLMGMT_CMD_GETPHYS_PREFIX, sizeof (dlmgmt_door_getphys_prefix_t),
	    sizeof (dlmgmt_getphys_prefix_retval_t), dlmgmt_getphys_prefix },
	{ DLMGMT_CMD_GETNEXT, sizeof (dlmgmt_door_getnext_t),
	    sizeof (dlmgmt_getnext_retval_t), dlmgmt_getnext },
	{ DLMGMT_CMD_GETNEXTPHYSATTR, sizeof (dlmgmt_door_getnextphysattr_t),
	    sizeof (dlmgmt_getnextphysattr_retval_t), dlmgmt_getnextphysattr },
	{ DLMGMT_CMD_REINIT_PHYS, sizeof (dlmgmt_door_reinit_phys_t),
	    sizeof (dlmgmt_reinit_phys_retval_t), dlmgmt_reinit_phys },
	{ DLMGMT_CMD_DLS_UPDATE, sizeof (dlmgmt_upcall_arg_update_t),
	    sizeof (dlmgmt_update_retval_t), dlmgmt_upcall_update },
	{ DLMGMT_CMD_CREATE_LINKID, sizeof (dlmgmt_door_createid_t),
	    sizeof (dlmgmt_createid_retval_t), dlmgmt_createid },
	{ DLMGMT_CMD_DESTROY_LINKID, sizeof (dlmgmt_door_destroyid_t),
	    sizeof (dlmgmt_destroyid_retval_t), dlmgmt_destroyid },
	{ DLMGMT_CMD_REMAP_LINKID, sizeof (dlmgmt_door_remapid_t),
	    sizeof (dlmgmt_remapid_retval_t), dlmgmt_remapid },
	{ DLMGMT_CMD_CREATECONF, sizeof (dlmgmt_door_createconf_t),
	    sizeof (dlmgmt_createconf_retval_t), dlmgmt_createconf },
	{ DLMGMT_CMD_OPENCONF, sizeof (dlmgmt_door_openconf_t),
	    sizeof (dlmgmt_openconf_retval_t), dlmgmt_openconf },
	{ DLMGMT_CMD_WRITECONF, sizeof (dlmgmt_door_writeconf_t),
	    sizeof (dlmgmt_writeconf_retval_t), dlmgmt_writeconf },
	{ DLMGMT_CMD_UP_LINKID, sizeof (dlmgmt_door_upid_t),
	    sizeof (dlmgmt_upid_retval_t), dlmgmt_upid },
	{ DLMGMT_CMD_SETATTR, sizeof (dlmgmt_door_setattr_t),
	    sizeof (dlmgmt_setattr_retval_t), dlmgmt_setattr },
	{ DLMGMT_CMD_UNSETATTR, sizeof (dlmgmt_door_unsetattr_t),
	    sizeof (dlmgmt_unsetattr_retval_t), dlmgmt_unsetconfattr },
	{ DLMGMT_CMD_REMOVECONF, sizeof (dlmgmt_door_removeconf_t),
	    sizeof (dlmgmt_removeconf_retval_t), dlmgmt_removeconf },
	{ DLMGMT_CMD_DESTROYCONF, sizeof (dlmgmt_door_destroyconf_t),
	    sizeof (dlmgmt_destroyconf_retval_t), dlmgmt_destroyconf },
	{ DLMGMT_CMD_GETATTR, sizeof (dlmgmt_door_getattr_t),
	    sizeof (dlmgmt_getattr_retval_t), dlmgmt_getattr },
	{ DLMGMT_CMD_GETCONFSNAPSHOT, sizeof (dlmgmt_door_getconfsnapshot_t),
	    sizeof (dlmgmt_getconfsnapshot_retval_t), dlmgmt_getconfsnapshot },
	{ DLMGMT_CMD_LINKPROP_INIT, sizeof (dlmgmt_door_linkprop_init_t),
	    sizeof (dlmgmt_linkprop_init_retval_t),
	    dlmgmt_upcall_linkprop_init },
	{ DLMGMT_CMD_FLOWGETNEXT, sizeof (dlmgmt_door_flowgetnext_t),
	    sizeof (dlmgmt_flowgetnext_retval_t), dlmgmt_flowgetnext },
	{ DLMGMT_CMD_CREATEFLOWCONF, sizeof (dlmgmt_door_createflowconf_t),
	    sizeof (dlmgmt_createflowconf_retval_t), dlmgmt_createflowconf },
	{ DLMGMT_CMD_READFLOWCONF, sizeof (dlmgmt_door_readflowconf_t),
	    sizeof (dlmgmt_readflowconf_retval_t), dlmgmt_readflowconf },
	{ DLMGMT_CMD_WRITEFLOWCONF, sizeof (dlmgmt_door_writeflowconf_t),
	    sizeof (dlmgmt_writeflowconf_retval_t), dlmgmt_writeflowconf },
	{ DLMGMT_CMD_SETFLOWATTR, sizeof (dlmgmt_door_setflowattr_t),
	    sizeof (dlmgmt_setflowattr_retval_t), dlmgmt_setflowattr },
	{ DLMGMT_CMD_REMOVEFLOWCONF, sizeof (dlmgmt_door_removeflowconf_t),
	    sizeof (dlmgmt_removeflowconf_retval_t), dlmgmt_removeflowconf },
	{ DLMGMT_CMD_GETFLOWATTR, sizeof (dlmgmt_door_getflowattr_t),
	    sizeof (dlmgmt_getattr_retval_t), dlmgmt_getflowattr },
	{ DLMGMT_CMD_SETZONEID, sizeof (dlmgmt_door_setzoneid_t),
	    sizeof (dlmgmt_setzoneid_retval_t), dlmgmt_setzoneid },
	{ DLMGMT_CMD_ZONEBOOT, sizeof (dlmgmt_door_zoneboot_t),
	    sizeof (dlmgmt_zoneboot_retval_t), dlmgmt_zoneboot },
	{ DLMGMT_CMD_ZONEHALT, sizeof (dlmgmt_door_zonehalt_t),
	    sizeof (dlmgmt_zonehalt_retval_t), dlmgmt_zonehalt },
	{ DLMGMT_CMD_VLINK_UPDATE, sizeof (dlmgmt_door_vlink_update_t),
	    sizeof (dlmgmt_retval_t), dlmgmt_update_vlink },
	{ 0, 0, 0, NULL }
};

static dlmgmt_door_info_t *
dlmgmt_getcmdinfo(int cmd)
{
	dlmgmt_door_info_t	*infop = i_dlmgmt_door_info_tbl;

	while (infop->di_handler != NULL) {
		if (infop->di_cmd == cmd)
			break;
		infop++;
	}
	return (infop);
}

/* ARGSUSED */
void
dlmgmt_handler(void *cookie, char *argp, size_t argsz, door_desc_t *dp,
    uint_t n_desc)
{
	dlmgmt_door_arg_t	*door_arg = (dlmgmt_door_arg_t *)(void *)argp;
	dlmgmt_door_info_t	*infop = NULL;
	dlmgmt_retval_t		retval;
	ucred_t			*cred = NULL;
	zoneid_t		zoneid;
	void			*retvalp = NULL;
	size_t			sz, acksz;
	int			err = 0;

	infop = dlmgmt_getcmdinfo(door_arg->ld_cmd);
	if (infop == NULL || argsz != infop->di_reqsz) {
		err = EINVAL;
		goto done;
	}

	if (door_ucred(&cred) != 0 || (zoneid = ucred_getzoneid(cred)) == -1) {
		err = errno;
		goto done;
	}

	/*
	 * Note that malloc() cannot be used here because door_return
	 * never returns, and memory allocated by malloc() would get leaked.
	 * Use alloca() instead.
	 */
	acksz = infop->di_acksz;

again:
	retvalp = alloca(acksz);
	sz = acksz;
	infop->di_handler(argp, retvalp, &acksz, zoneid, cred);
	if (acksz > sz) {
		/*
		 * If the specified buffer size is not big enough to hold the
		 * return value, reallocate the buffer and try to get the
		 * result one more time.
		 */
		assert(((dlmgmt_retval_t *)retvalp)->lr_err == ENOSPC);
		goto again;
	}

done:
	if (cred != NULL)
		ucred_free(cred);
	if (err == 0) {
		(void) door_return(retvalp, acksz, NULL, 0);
	} else {
		retval.lr_err = err;
		(void) door_return((char *)&retval, sizeof (retval), NULL, 0);
	}
}

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
 * Utility functions used by the dlmgmtd daemon.
 */

#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <string.h>
#include <stropts.h>
#include <syslog.h>
#include <sys/dld.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stropts.h>
#include <sys/scsi/adapters/iscsi_if.h>
#include <unistd.h>
#include <zone.h>
#include <errno.h>
#include <libdlpi.h>
#include <libdlflow.h>
#include <libinetutil.h>
#include <libscf.h>
#include "dlmgmt_impl.h"

/*
 * There are three datalink AVL tables.  The dlmgmt_name_avl tree contains all
 * datalinks and is keyed by zoneid and link name.  The dlmgmt_id_avl also
 * contains all datalinks, and it is keyed by link ID.  The dlmgmt_loan_avl is
 * keyed by link name, and contains the set of global-zone links that are
 * currently on loan to non-global zones.
 */
avl_tree_t	dlmgmt_name_avl;
avl_tree_t	dlmgmt_id_avl;
avl_tree_t	dlmgmt_loan_avl;

avl_tree_t	dlmgmt_dlconf_avl;
/*
 * There is a flow AVL tree "dlmgmt_flowconf_avl" for all flows on the system.
 * The nodes of this AVL tree are keyed by zoneid. Each node is in turn also
 * a AVL that is keyed by zoneid and holds that zone's flows. The flow entries
 * in a zone is stored in this AVL, keyed by flowname.
 */
avl_tree_t	dlmgmt_flowconf_avl; /* flow avl, keyed by zoneid */

static pthread_rwlock_t	dlmgmt_avl_lock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t  dlmgmt_avl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	dlmgmt_avl_cv = PTHREAD_COND_INITIALIZER;
static pthread_rwlock_t	dlmgmt_dlconf_lock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t	dlmgmt_flowconf_lock = PTHREAD_RWLOCK_INITIALIZER;

typedef struct dlmgmt_prefix {
	struct dlmgmt_prefix	*lp_next;
	char			lp_prefix[MAXLINKNAMELEN];
	zoneid_t		lp_zoneid;
	uint_t			lp_nextppa;
} dlmgmt_prefix_t;
static dlmgmt_prefix_t	dlmgmt_prefixlist;

char			dlmgmt_phys_prefix[MAXLINKNAMELEN];
datalink_id_t		dlmgmt_nextlinkid;
static datalink_id_t	dlmgmt_nextconfid = 1;

/*
 * A holder for all the resources needed to get a property value
 * using libscf.
 */
typedef struct scf_resources {
	scf_handle_t		*sr_handle;
	scf_instance_t		*sr_inst;
	scf_propertygroup_t	*sr_pg;
	scf_property_t		*sr_prop;
	scf_value_t		*sr_val;
	scf_transaction_t	*sr_tx;
	scf_transaction_entry_t	*sr_ent;
} scf_resources_t;

static void		dlmgmt_advance_linkid(dlmgmt_link_t *);
static void		dlmgmt_advance_ppa(dlmgmt_link_t *);

void
dlmgmt_log(int pri, const char *fmt, ...)
{
	va_list alist;

	va_start(alist, fmt);
	if (debug && fg) {
		(void) vfprintf(stderr, fmt, alist);
		(void) fputc('\n', stderr);
	} else if (msglog_fp) {
		(void) vfprintf(msglog_fp, fmt, alist);
		(void) fputc('\n', msglog_fp);
	} else {
		vsyslog(pri, fmt, alist);
	}
	va_end(alist);
}

static int
cmp_link_by_name(const void *v1, const void *v2)
{
	const dlmgmt_link_t *link1 = v1;
	const dlmgmt_link_t *link2 = v2;
	int cmp;

	cmp = strcmp(link1->ll_link, link2->ll_link);
	return ((cmp == 0) ? 0 : ((cmp < 0) ? -1 : 1));
}

/*
 * Note that the zoneid associated with a link is effectively part of its
 * name.  This is essentially what results in having each zone have disjoint
 * datalink namespaces.
 */
static int
cmp_link_by_zname(const void *v1, const void *v2)
{
	const dlmgmt_link_t *link1 = v1;
	const dlmgmt_link_t *link2 = v2;

	if (link1->ll_zoneid < link2->ll_zoneid)
		return (-1);
	if (link1->ll_zoneid > link2->ll_zoneid)
		return (1);
	return (cmp_link_by_name(link1, link2));
}

static int
cmp_link_by_id(const void *v1, const void *v2)
{
	const dlmgmt_link_t *link1 = v1;
	const dlmgmt_link_t *link2 = v2;

	if ((uint64_t)(link1->ll_linkid) == (uint64_t)(link2->ll_linkid))
		return (0);
	else if ((uint64_t)(link1->ll_linkid) < (uint64_t)(link2->ll_linkid))
		return (-1);
	else
		return (1);
}

static int
cmp_flowavl_by_zoneid(const void *v1, const void *v2)
{
	const dlmgmt_flowavl_t *favlp1 = v1;
	const dlmgmt_flowavl_t *favlp2 = v2;

	if ((uint64_t)(favlp1->la_zoneid) == (uint64_t)(favlp2->la_zoneid))
		return (0);
	else if ((uint64_t)(favlp1->la_zoneid) < (uint64_t)(favlp2->la_zoneid))
		return (-1);
	else
		return (1);
}

static int
cmp_flowconf_by_name(const void *v1, const void *v2)
{
	const dlmgmt_flowconf_t *flowconfp1 = v1;
	const dlmgmt_flowconf_t *flowconfp2 = v2;
	int cmp;

	cmp = strcmp(flowconfp1->ld_flow, flowconfp2->ld_flow);
	return ((cmp == 0) ? 0 : ((cmp < 0) ? -1 : 1));
}

static int
cmp_dlconf_by_id(const void *v1, const void *v2)
{
	const dlmgmt_dlconf_t *dlconfp1 = v1;
	const dlmgmt_dlconf_t *dlconfp2 = v2;

	if (dlconfp1->ld_id == dlconfp2->ld_id)
		return (0);
	else if (dlconfp1->ld_id < dlconfp2->ld_id)
		return (-1);
	else
		return (1);
}

void
dlmgmt_linktable_init(void)
{
	/*
	 * Initialize the prefix list. First add the "net" prefix for the
	 * global zone to the list.
	 */
	dlmgmt_prefixlist.lp_next = NULL;
	dlmgmt_prefixlist.lp_zoneid = GLOBAL_ZONEID;
	dlmgmt_prefixlist.lp_nextppa = 0;
	(void) strlcpy(dlmgmt_prefixlist.lp_prefix, "net", MAXLINKNAMELEN);

	avl_create(&dlmgmt_name_avl, cmp_link_by_zname, sizeof (dlmgmt_link_t),
	    offsetof(dlmgmt_link_t, ll_name_node));
	avl_create(&dlmgmt_id_avl, cmp_link_by_id, sizeof (dlmgmt_link_t),
	    offsetof(dlmgmt_link_t, ll_id_node));
	avl_create(&dlmgmt_loan_avl, cmp_link_by_name, sizeof (dlmgmt_link_t),
	    offsetof(dlmgmt_link_t, ll_loan_node));
	avl_create(&dlmgmt_dlconf_avl, cmp_dlconf_by_id,
	    sizeof (dlmgmt_dlconf_t), offsetof(dlmgmt_dlconf_t, ld_node));
	avl_create(&dlmgmt_flowconf_avl, cmp_flowavl_by_zoneid,
	    sizeof (dlmgmt_flowavl_t), offsetof(dlmgmt_flowavl_t, la_node));
	dlmgmt_nextlinkid = 1;
}

void
dlmgmt_linktable_fini(void)
{
	dlmgmt_prefix_t *lpp, *next;

	for (lpp = dlmgmt_prefixlist.lp_next; lpp != NULL; lpp = next) {
		next = lpp->lp_next;
		free(lpp);
	}

	avl_destroy(&dlmgmt_flowconf_avl);
	avl_destroy(&dlmgmt_dlconf_avl);
	avl_destroy(&dlmgmt_name_avl);
	avl_destroy(&dlmgmt_loan_avl);
	avl_destroy(&dlmgmt_id_avl);
}

static void
dlmgmt_release_scf_resources(scf_resources_t *res)
{
	scf_entry_destroy(res->sr_ent);
	scf_transaction_destroy(res->sr_tx);
	scf_value_destroy(res->sr_val);
	scf_property_destroy(res->sr_prop);
	scf_pg_destroy(res->sr_pg);
	scf_instance_destroy(res->sr_inst);
	(void) scf_handle_unbind(res->sr_handle);
	scf_handle_destroy(res->sr_handle);
}


static int
dlmgmt_create_scf_resources(const char *fmri, scf_resources_t *res)
{
	res->sr_tx = NULL;
	res->sr_ent = NULL;
	res->sr_inst = NULL;
	res->sr_pg = NULL;
	res->sr_prop = NULL;
	res->sr_val = NULL;

	if ((res->sr_handle = scf_handle_create(SCF_VERSION)) == NULL)
		return (-1);

	if (scf_handle_bind(res->sr_handle) != 0) {
		scf_handle_destroy(res->sr_handle);
		return (-1);
	}
	if ((res->sr_inst = scf_instance_create(res->sr_handle)) != NULL &&
	    scf_handle_decode_fmri(res->sr_handle, fmri, NULL, NULL,
	    res->sr_inst, NULL, NULL, SCF_DECODE_FMRI_REQUIRE_INSTANCE) == 0 &&
	    (res->sr_pg = scf_pg_create(res->sr_handle)) != NULL &&
	    (res->sr_prop = scf_property_create(res->sr_handle)) != NULL &&
	    (res->sr_val = scf_value_create(res->sr_handle)) != NULL &&
	    (res->sr_tx = scf_transaction_create(res->sr_handle)) != NULL &&
	    (res->sr_ent = scf_entry_create(res->sr_handle)) != NULL) {
		return (0);
	}
	dlmgmt_log(LOG_DEBUG, "dlmgmt_create_scf_resources: failed: %s",
	    scf_strerror(scf_error()));
	dlmgmt_release_scf_resources(res);
	return (-1);
}

static int
dlmgmt_smf_get_property_value(const char *fmri, const char *pg,
    const char *prop, scf_resources_t *res)
{
	if (dlmgmt_create_scf_resources(fmri, res) != 0)
		return (-1);

	if (scf_instance_get_pg_composed(res->sr_inst, NULL, pg, res->sr_pg)
	    == 0 &&
	    scf_pg_get_property(res->sr_pg, prop, res->sr_prop) == 0 &&
	    scf_property_get_value(res->sr_prop, res->sr_val) == 0)
		return (0);

	dlmgmt_release_scf_resources(res);
	return (-1);
}

int
dlmgmt_smf_get_string_property(const char *lfmri, const char *lpg,
    const char *lprop, char *buf, size_t bufsz)
{
	int result = -1;
	scf_resources_t res;

	if (dlmgmt_smf_get_property_value(lfmri, lpg, lprop, &res) != 0)
		return (-1);

	if (scf_value_get_astring(res.sr_val, buf, bufsz) > 0)
		result = 0;
	dlmgmt_release_scf_resources(&res);
	return (result);
}

int
dlmgmt_smf_get_boolean_property(const char *lfmri, const char *lpg,
    const char *lprop, boolean_t *answer)
{
	int result = -1;
	scf_resources_t res;
	uint8_t prop_val;

	if (dlmgmt_smf_get_property_value(lfmri, lpg, lprop, &res) != 0)
		return (result);

	if (scf_value_get_boolean(res.sr_val, &prop_val) == 0) {
		*answer = (boolean_t)prop_val;
		result = 0;
	}
	dlmgmt_release_scf_resources(&res);
	return (result);
}

static int
dlmgmt_smf_set_property_value(scf_resources_t *res, const char *propname,
    scf_type_t proptype)
{
	int result = -1;
	boolean_t new;

retry:
	new = (scf_pg_get_property(res->sr_pg, propname, res->sr_prop) != 0);

	if (scf_transaction_start(res->sr_tx, res->sr_pg) == -1)
		return (-1);
	if (new) {
		if (scf_transaction_property_new(res->sr_tx, res->sr_ent,
		    propname, proptype) == -1)
			return (-1);
	} else {
		if (scf_transaction_property_change(res->sr_tx, res->sr_ent,
		    propname, proptype) == -1)
			return (-1);
	}

	if (scf_entry_add_value(res->sr_ent, res->sr_val) != 0)
		return (-1);

	result = scf_transaction_commit(res->sr_tx);
	if (result == 0) {
		scf_transaction_reset(res->sr_tx);
		if (scf_pg_update(res->sr_pg) == -1)
			return (-1);
		dlmgmt_log(LOG_DEBUG, "dlmgmt_smf_set_property_value: "
		    "transaction commit failed for %s; retrying", propname);
		goto retry;
	}
	if (result == -1)
		return (-1);
	return (0);
}

int
dlmgmt_smf_set_string_property(const char *fmri, const char *pg,
    const char *prop, const char *str)
{
	scf_resources_t res;
	int result = -1;

	if (dlmgmt_create_scf_resources(fmri, &res) != 0)
		return (-1);

	if (scf_instance_get_pg_composed(res.sr_inst, NULL, pg,
	    res.sr_pg) == 0 &&
	    scf_value_set_astring(res.sr_val, str) == 0 &&
	    dlmgmt_smf_set_property_value(&res, prop, SCF_TYPE_ASTRING) == 0)
		result = 0;
	if (result == -1) {
		dlmgmt_log(LOG_DEBUG, "dlmgmt_smf_set_string_property: "
		    "scf failure %s while setting %s",
		    scf_strerror(scf_error()), prop);
	}
	dlmgmt_release_scf_resources(&res);
	return (result);
}

int
dlmgmt_smf_set_boolean_property(const char *fmri, const char *pg,
    const char *prop, boolean_t bool)
{
	scf_resources_t res;
	int result = -1;

	if (dlmgmt_create_scf_resources(fmri, &res) != 0)
		return (-1);

	if (scf_instance_get_pg_composed(res.sr_inst, NULL, pg,
	    res.sr_pg) == 0) {
		scf_value_set_boolean(res.sr_val, (uint8_t)bool);
		if (dlmgmt_smf_set_property_value(&res, prop,
		    SCF_TYPE_BOOLEAN) == 0)
			result = 0;
	}
	if (result == -1) {
		dlmgmt_log(LOG_DEBUG, "dlmgmt_smf_set_boolean_property: "
		    "scf failure %s while setting %s",
		    scf_strerror(scf_error()), prop);
	}
	dlmgmt_release_scf_resources(&res);
	return (result);
}

/*
 * insert an attribute to the attribute list
 * the attribute list can be a link or flow's attr list
 */
static void
objattr_add(dlmgmt_objattr_t **headp, dlmgmt_objattr_t *attrp)
{
	if (*headp == NULL) {
		*headp = attrp;
	} else {
		(*headp)->lp_prev = attrp;
		attrp->lp_next = *headp;
		*headp = attrp;
	}
}

/*
 * delete an attribute from the attribute list
 * the attribute list can be a link or flow's attr list
 */
static void
objattr_rm(dlmgmt_objattr_t **headp, dlmgmt_objattr_t *attrp)
{
	dlmgmt_objattr_t *next, *prev;

	next = attrp->lp_next;
	prev = attrp->lp_prev;
	if (next != NULL)
		next->lp_prev = prev;
	if (prev != NULL)
		prev->lp_next = next;
	else
		*headp = next;
}

/*
 * find an attribute from the attribute list
 * the attribute list can be a link or flow's attr list
 */
dlmgmt_objattr_t *
objattr_find(dlmgmt_objattr_t *headp, const char *attr)
{
	dlmgmt_objattr_t *attrp;

	for (attrp = headp; attrp != NULL; attrp = attrp->lp_next) {
		if (strcmp(attrp->lp_name, attr) == 0)
			break;
	}
	return (attrp);
}

/*
 * change an attribute in the attribute list
 * the attribute list can be a link or flow's attr list
 */
int
objattr_set(dlmgmt_objattr_t **headp, const char *attr, void *attrval,
    size_t attrsz, dladm_datatype_t type)
{
	dlmgmt_objattr_t	*attrp;
	void			*newval;
	boolean_t		new;

	attrp = objattr_find(*headp, attr);
	if (attrp != NULL) {
		/*
		 * It is already set.  If the value changed, update it.
		 */
		if (objattr_equal(headp, attr, attrval, attrsz))
			return (0);
		new = B_FALSE;
	} else {
		/*
		 * It is not set yet, allocate the objattr and prepend to the
		 * list.
		 */
		if ((attrp = calloc(1, sizeof (dlmgmt_objattr_t))) == NULL)
			return (ENOMEM);

		(void) strlcpy(attrp->lp_name, attr, MAXLINKATTRLEN);
		new = B_TRUE;
	}
	if ((newval = calloc(1, attrsz)) == NULL) {
		if (new)
			free(attrp);
		return (ENOMEM);
	}

	if (!new)
		free(attrp->lp_val);
	attrp->lp_val = newval;
	bcopy(attrval, attrp->lp_val, attrsz);
	attrp->lp_sz = attrsz;
	attrp->lp_type = type;
	attrp->lp_linkprop = dladm_attr_is_linkprop(attr);
	if (new)
		objattr_add(headp, attrp);
	return (0);
}

void
objattr_unset(dlmgmt_objattr_t **headp, const char *attr)
{
	dlmgmt_objattr_t *attrp;

	if ((attrp = objattr_find(*headp, attr)) != NULL) {
		objattr_rm(headp, attrp);
		free(attrp->lp_val);
		free(attrp);
	}
}

int
objattr_get(dlmgmt_objattr_t **headp, const char *attr, void **attrvalp,
    size_t *attrszp, dladm_datatype_t *typep)
{
	dlmgmt_objattr_t *attrp;

	if ((attrp = objattr_find(*headp, attr)) == NULL)
		return (ENOENT);

	*attrvalp = attrp->lp_val;
	*attrszp = attrp->lp_sz;
	if (typep != NULL)
		*typep = attrp->lp_type;
	return (0);
}

boolean_t
objattr_equal(dlmgmt_objattr_t **headp, const char *attr, void *attrval,
    size_t attrsz)
{
	void	*saved_attrval;
	size_t	saved_attrsz;

	if (objattr_get(headp, attr, &saved_attrval, &saved_attrsz, NULL) != 0)
		return (B_FALSE);

	return ((saved_attrsz == attrsz) &&
	    (memcmp(saved_attrval, attrval, attrsz) == 0));
}

void
linkattr_destroy(dlmgmt_link_t *linkp)
{
	dlmgmt_objattr_t *next, *attrp;

	for (attrp = linkp->ll_head; attrp != NULL; attrp = next) {
		next = attrp->lp_next;
		free(attrp->lp_val);
		free(attrp);
	}
}

static char *
link_devname(dlmgmt_link_t *linkp)
{
	void *attrval;
	size_t attrsz;
	dladm_datatype_t attrtype = DLADM_TYPE_STR;
	char *ret = NULL;

	if (objattr_get(&(linkp->ll_head), FDEVNAME, &attrval, &attrsz,
	    &attrtype) == 0 && attrtype == DLADM_TYPE_STR && attrsz > 0)
		ret = attrval;

	return (ret);
}

static int
dlmgmt_table_readwritelock(boolean_t write)
{
	if (write)
		return (pthread_rwlock_trywrlock(&dlmgmt_avl_lock));
	else
		return (pthread_rwlock_tryrdlock(&dlmgmt_avl_lock));
}

void
dlmgmt_table_lock(boolean_t write)
{
	(void) pthread_mutex_lock(&dlmgmt_avl_mutex);
	while (dlmgmt_table_readwritelock(write) == EBUSY)
		(void) pthread_cond_wait(&dlmgmt_avl_cv, &dlmgmt_avl_mutex);

	(void) pthread_mutex_unlock(&dlmgmt_avl_mutex);
}

void
dlmgmt_table_unlock(void)
{
	(void) pthread_rwlock_unlock(&dlmgmt_avl_lock);
	(void) pthread_mutex_lock(&dlmgmt_avl_mutex);
	(void) pthread_cond_broadcast(&dlmgmt_avl_cv);
	(void) pthread_mutex_unlock(&dlmgmt_avl_mutex);
}

void
link_destroy(dlmgmt_link_t *linkp)
{
	linkattr_destroy(linkp);
	free(linkp);
}

/*
 * Set the DLMGMT_ACTIVE flag on the link to note that it is active.  When a
 * link becomes active and it belongs to a non-global zone, it is also added
 * to that zone.
 */
int
link_activate(dlmgmt_link_t *linkp)
{
	int		err = 0;
	zoneid_t	zoneid = ALL_ZONES;

	if (zone_check_datalink(&zoneid, linkp->ll_linkid) == 0) {
		/*
		 * This link was already added to a non-global zone.
		 * This can happen if dlmgmtd is restarted.
		 * See comments inside process_obj_line_req() regarding
		 * how the link's current zoneid (i.e. linkp->ll_zoneid)
		 * is initialized.
		 */
		if (zoneid != linkp->ll_zoneid) {
			if (link_by_name(linkp->ll_link, zoneid) != NULL) {
				err = EEXIST;
				goto done;
			}

			if (avl_find(&dlmgmt_name_avl, linkp, NULL) != NULL)
				avl_remove(&dlmgmt_name_avl, linkp);

			linkp->ll_zoneid = zoneid;
			avl_add(&dlmgmt_name_avl, linkp);
			avl_add(&dlmgmt_loan_avl, linkp);
			linkp->ll_onloan = B_TRUE;
		}
	} else if (linkp->ll_zoneid != GLOBAL_ZONEID) {
		err = zone_add_datalink(linkp->ll_zoneid, linkp->ll_linkid);
	}
done:
	if (err == 0)
		linkp->ll_flags |= DLMGMT_ACTIVE;
	return (err);
}

/*
 * Is linkp visible from the caller's zoneid?  It is if the link is in the
 * same zone as the caller, or if the caller's zone is the link's owner or
 * caller from the global zone requested links in all zones.
 */
boolean_t
link_is_visible(dlmgmt_link_t *linkp, zoneid_t zoneid, uint32_t flags)
{
	if (linkp->ll_zoneid == zoneid || linkp->ll_owner_zoneid == zoneid)
		return (B_TRUE);

	if (zoneid == GLOBAL_ZONEID && (flags & DLMGMT_ALLZONES) != 0)
		return (B_TRUE);

	return (B_FALSE);
}

dlmgmt_link_t *
link_by_id(datalink_id_t linkid, zoneid_t zoneid)
{
	dlmgmt_link_t link, *linkp;

	link.ll_linkid = linkid;
	linkp = avl_find(&dlmgmt_id_avl, &link, NULL);
	if (linkp != NULL && zoneid != GLOBAL_ZONEID &&
	    linkp->ll_zoneid != zoneid)
		return (NULL);
	return (linkp);
}

/* Walk over the flow entries whose linkid is "linkid" */
dlmgmt_flowconf_t *
flow_by_linkid(datalink_id_t linkid, zoneid_t zoneid,
    dlmgmt_flowconf_t *pre_flow)
{
	dlmgmt_flowavl_t f_avl, *f_avlp;
	dlmgmt_flowconf_t *flowp;

	f_avl.la_zoneid = zoneid;
	f_avlp = avl_find(&dlmgmt_flowconf_avl, &f_avl, NULL);
	if (f_avlp == NULL)
		return (NULL);

	if (pre_flow == NULL) {
		flowp = avl_first(&f_avlp->la_flowtree);
	} else {
		flowp = avl_walk(&f_avlp->la_flowtree, pre_flow, AVL_AFTER);
	}

	while (flowp != NULL) {
		if (flowp->ld_linkid == linkid)
			return (flowp);
		flowp = avl_walk(&f_avlp->la_flowtree, flowp, AVL_AFTER);
	}
	return (flowp);
}

/*
 * Looks up the link structure by linkname. If the link name is prefixed with a
 * zone name then the link ID lookup is performed only within the given zone and
 * not across all zones. zoneid passed to the function is the caller zone ID.
 * Only if the caller is in the global zone do we support querying for link ID
 * of datalinks in other non-global zones.
 */
dlmgmt_link_t *
link_by_name(const char *name, zoneid_t zoneid)
{
	dlmgmt_link_t	link, *linkp;
	char		linkname[MAXLINKNAMELEN];
	zoneid_t	linkzoneid;

	if (!dlparse_zonelinkname(name, linkname, &linkzoneid))
		return (NULL);
	/*
	 * Callers in non-global zones can only perform lookups for links in
	 * their own zone. So we do not allow lookups in the NGZ using zonename
	 * prefixed linknames. linkzoneid != ALL_ZONES indicates a zonename
	 * prefix was used in the linkname.
	 */
	if (zoneid != GLOBAL_ZONEID && linkzoneid != ALL_ZONES)
		return (NULL);

	(void) strlcpy(link.ll_link, linkname, MAXLINKNAMELEN);
	link.ll_zoneid = (linkzoneid == ALL_ZONES) ? zoneid:linkzoneid;
	linkp = avl_find(&dlmgmt_name_avl, &link, NULL);

	/*
	 * If link was not found in the caller zone when no zone was specified
	 * in the link name nor in the specified zone then we check if the link
	 * is on loan to another zone. We only do this if caller is in the
	 * global zone and when either there is no zonename prefix in the given
	 * linkname or the zonename prefix is the global zone.
	 */
	if (linkp == NULL && zoneid == GLOBAL_ZONEID && (linkzoneid ==
	    ALL_ZONES || linkzoneid == GLOBAL_ZONEID))
		linkp = avl_find(&dlmgmt_loan_avl, &link, NULL);

	return (linkp);
}

/*
 * Sets the datalink name with a zonename prefix in the passed zone variable
 * if the caller is in the global zone and the datalink is part of a
 * non-global zone. If the caller sets zoneprefix to false then no prefix
 * is included in the name. Caller can also choose to set the zonename
 * prefix for on-loan datalinks but the default is to not set a zonename
 * prefix for datalinks on loan to non-global zones.
 */
int
dlmgmt_zonelinkname(dlmgmt_link_t *linkp, zoneid_t caller_zoneid,
    boolean_t zoneprefix, boolean_t zoneprefixonloan,  char *zoneprefixlink,
    size_t zoneprefixlinklen)
{
	int err = 0;

	if (zoneprefix && caller_zoneid == GLOBAL_ZONEID &&
	    (!linkp->ll_onloan || zoneprefixonloan) && linkp->ll_zoneid !=
	    GLOBAL_ZONEID) {
		char zone[ZONENAME_MAX];

		/*
		 * When looking up non-global zone datalinks from the global
		 * zone and the ld_zoneprefix option is set, prefix the
		 * datalink's zone name in the returned link name. We do not do
		 * this for datalinks that are on loan from the global zone.
		 */
		if (getzonenamebyid(linkp->ll_zoneid, zone,
		    sizeof (zone)) < 0) {
			err = errno;
		} else if (snprintf(zoneprefixlink, zoneprefixlinklen, "%s/%s",
		    zone, linkp->ll_link) >= zoneprefixlinklen) {
			err = ENOSPC;
		}
	} else if (strlcpy(zoneprefixlink, linkp->ll_link, zoneprefixlinklen) >=
	    zoneprefixlinklen) {
		err = ENOSPC;
	}

	return (err);
}

/*
 * If vanity naming is in operation - linkname-policy/phys-prefix property is
 * present and is a valid linkname prefix, and the link supports vanity names -
 * use the prefix in combination with the physical location index.  If that
 * linkname is taken, start from after the last vanity name index looking for
 * an available name.
 *
 * If vanity naming is disabled, get location information for the benefit of
 * location labels.
 */
/*ARGSUSED*/
static int
dlmgmt_phys_link_name_attr(const char *name, datalink_class_t class,
    uint32_t media, boolean_t novanity, zoneid_t owner_zoneid, zoneid_t zoneid,
    uint32_t flags, char *linkname, dlmgmt_physloc_t **plp)
{
	uint_t physloc_count, i;
	dlmgmt_physloc_t *pl = NULL;
	dlmgmt_link_t *linkp;
	char *dev;

	if ((*plp = dlmgmt_physloc_get(name, B_FALSE, B_FALSE,
	    &physloc_count)) == NULL)
		return (ENODEV);

	if (novanity || strlen(dlmgmt_phys_prefix) == 0) {
		dlmgmt_log(LOG_DEBUG, "dlmgmt_phys_link_name_attr: %s "
		    "vanity naming not enabled", name);
		return (EINVAL);
	}

	dlmgmt_log(LOG_DEBUG, "dlmgmt_phys_link_name_attr: attempting to "
	    "assign index %d to %s\n", (*plp)->pl_index, name);
	(void) snprintf(linkname, MAXLINKNAMELEN, "%s%d",
	    dlmgmt_phys_prefix, (*plp)->pl_index);
	if (!dladm_valid_linkname(linkname))
		return (EINVAL);
	if ((linkp = link_by_name(linkname, zoneid)) == NULL)
		return (0);

	/*
	 * Check if original linkname-owning device has gone.  If the original
	 * device has gone, reuse its name (and hence configuration).
	 */
	if ((dev = link_devname(linkp)) != NULL &&
	    (pl = dlmgmt_physloc_get(dev, B_FALSE, B_FALSE, NULL)) == NULL) {
		(*plp)->pl_link = linkp;
		return (EEXIST);
	}
	if (pl != NULL)
		dlmgmt_physloc_free(pl);

	for (i = physloc_count; i != (uint_t)-1; i++) {
		(void) snprintf(linkname, MAXLINKNAMELEN, "%s%d",
		    dlmgmt_phys_prefix, i);
		if (link_by_name(linkname, zoneid) == NULL)
			return (0);
	}
	return (ENOSPC);
}

int
dlmgmt_create_common(const char *name, datalink_class_t class, uint32_t media,
    boolean_t novanity, zoneid_t owner_zoneid, zoneid_t zoneid, uint32_t flags,
    dlmgmt_link_t **linkpp)
{
	char			vanityname[MAXLINKNAMELEN];
	dlmgmt_link_t		*linkp = NULL;
	avl_index_t		name_where, id_where;
	int			err = 0;
	char			*linkname = (char *)name;
	dlmgmt_physloc_t	*pl = NULL;
	char			label[DLD_LOC_STRSIZE];
	boolean_t		created = B_TRUE;

	dlmgmt_log(LOG_DEBUG, "dlmgmt_create common: %s, %s vanity naming",
	    name, novanity ? "do not use" : "use");
	if (class == DATALINK_CLASS_PHYS) {
		err = dlmgmt_phys_link_name_attr(name, class, media, novanity,
		    owner_zoneid, zoneid, flags, vanityname, &pl);
		if (pl != NULL && strlen(pl->pl_label) > 0)
			(void) strlcpy(label, pl->pl_label, DLD_LOC_STRSIZE);
		else
			label[0] = '\0';
		switch (err) {
		case EEXIST:
			linkp = pl->pl_link;
			created = B_FALSE;
			/* FALLTHRU */
		case 0:
			linkname = vanityname;
			break;
		default:
			break;
		}
		if (pl != NULL)
			dlmgmt_physloc_free(pl);
		err = 0;
	}

	if (!dladm_valid_linkname(linkname))
		return (EINVAL);

	if (created) {
		if (dlmgmt_nextlinkid == DATALINK_INVALID_LINKID)
			return (ENOSPC);

		if ((linkp = calloc(1, sizeof (dlmgmt_link_t))) == NULL) {
			err = ENOMEM;
			goto done;
		}
		/*
		 * Set link attributes that are preserved when a link is
		 * reused here.
		 */
		linkp->ll_linkid = dlmgmt_nextlinkid;
		linkp->ll_owner_zoneid = owner_zoneid;
		linkp->ll_zoneid = zoneid;
		linkp->ll_onloan = B_FALSE;
	}

	(void) strlcpy(linkp->ll_link, linkname, MAXLINKNAMELEN);
	linkp->ll_class = class;
	linkp->ll_media = media;
	linkp->ll_gen = 0;
	if (class == DATALINK_CLASS_PHYS) {
		(void) strlcpy(linkp->ll_physloc, label, DLD_LOC_STRSIZE);
		dlmgmt_log(LOG_DEBUG, "dlmgmt_create_common: link %s, label %s",
		    linkp->ll_link, linkp->ll_physloc);
	}
	if (created) {
		if (avl_find(&dlmgmt_name_avl, linkp, &name_where) != NULL) {
			err = EEXIST;
			goto done;
		}

		/*
		 * Auto VNICs are created in GZ namespace however they are not
		 * visible or usable in the global zone so they shouldn't be
		 * included in any naming checks in the global zone. Therefore,
		 * we exempt Auto VNICs from following check in the GZ to detect
		 * a link with the same name that is on loan to another zone
		 * (which is always the case with Auto VNICs created by default
		 * in multiple zones).
		 */
		if ((flags & DLMGMT_AUTOVNIC) == 0 &&
		    owner_zoneid == GLOBAL_ZONEID &&
		    avl_find(&dlmgmt_loan_avl, linkp, NULL) != NULL) {
			err = EEXIST;
			goto done;
		}

		if (avl_find(&dlmgmt_id_avl, linkp, &id_where) != NULL) {
			err = EEXIST;
			goto done;
		}
		avl_insert(&dlmgmt_name_avl, linkp, name_where);
		avl_insert(&dlmgmt_id_avl, linkp, id_where);
	}

	if ((flags & DLMGMT_ACTIVE) && (err = link_activate(linkp)) != 0) {
		avl_remove(&dlmgmt_name_avl, linkp);
		avl_remove(&dlmgmt_id_avl, linkp);
		if (!created)
			free(linkp);
		goto done;
	}

	linkp->ll_flags = flags;
	if (created)
		dlmgmt_advance(linkp);
	*linkpp = linkp;

done:
	if (err != 0 && created)
		free(linkp);
	return (err);
}

int
dlmgmt_destroy_common(dlmgmt_link_t *linkp, uint32_t flags)
{
	if ((linkp->ll_flags & flags) == 0) {
		/*
		 * The link does not exist in the specified space.
		 */
		return (ENOENT);
	}

	linkp->ll_flags &= ~flags;
	if (flags & DLMGMT_PERSIST) {
		dlmgmt_objattr_t *next, *attrp;

		for (attrp = linkp->ll_head; attrp != NULL; attrp = next) {
			next = attrp->lp_next;
			free(attrp->lp_val);
			free(attrp);
		}
		linkp->ll_head = NULL;
	}

	if ((flags & DLMGMT_ACTIVE) && linkp->ll_zoneid != GLOBAL_ZONEID) {
		(void) zone_remove_datalink(linkp->ll_zoneid, linkp->ll_linkid);
		if (linkp->ll_onloan)
			avl_remove(&dlmgmt_loan_avl, linkp);
	}

	if (linkp->ll_flags == 0) {
		avl_remove(&dlmgmt_id_avl, linkp);
		avl_remove(&dlmgmt_name_avl, linkp);
		link_destroy(linkp);
	}

	return (0);
}

int
dlmgmt_getattr_common(dlmgmt_objattr_t **headp, const char *attr,
    dlmgmt_getattr_retval_t *retvalp)
{
	int			err;
	void			*attrval;
	size_t			attrsz;
	dladm_datatype_t	attrtype;

	err = objattr_get(headp, attr, &attrval, &attrsz, &attrtype);
	if (err != 0)
		return (err);

	assert(attrsz > 0);
	if (attrsz > MAXLINKATTRVALLEN)
		return (EINVAL);

	retvalp->lr_type = attrtype;
	retvalp->lr_attrsz = attrsz;
	bcopy(attrval, retvalp->lr_attrval, attrsz);
	return (0);
}

void
dlmgmt_dlconf_table_lock(boolean_t write)
{
	if (write)
		(void) pthread_rwlock_wrlock(&dlmgmt_dlconf_lock);
	else
		(void) pthread_rwlock_rdlock(&dlmgmt_dlconf_lock);
}

void
dlmgmt_dlconf_table_unlock(void)
{
	(void) pthread_rwlock_unlock(&dlmgmt_dlconf_lock);
}

void
dlmgmt_flowconf_table_lock(boolean_t write)
{
	if (write)
		(void) pthread_rwlock_wrlock(&dlmgmt_flowconf_lock);
	else
		(void) pthread_rwlock_rdlock(&dlmgmt_flowconf_lock);
}

void
dlmgmt_flowconf_table_unlock(void)
{
	(void) pthread_rwlock_unlock(&dlmgmt_flowconf_lock);
}

int
flowconf_avl_create(zoneid_t zoneid, dlmgmt_flowavl_t **flowavlpp)
{
	dlmgmt_flowavl_t	*flowavlp = NULL;

	if ((flowavlp = calloc(1, sizeof (dlmgmt_flowavl_t))) == NULL)
		return (ENOMEM);

	flowavlp->la_zoneid = zoneid;
	avl_create(&flowavlp->la_flowtree, cmp_flowconf_by_name,
	    sizeof (dlmgmt_flowconf_t), offsetof(dlmgmt_flowconf_t, ld_node));

	*flowavlpp = flowavlp;
	return (0);
}

int
flowconf_create(const char *name, datalink_id_t linkid, zoneid_t zoneid,
    dlmgmt_flowconf_t **flowconfpp)
{
	dlmgmt_flowconf_t	*flowconfp = NULL;
	int			err = 0;

	if ((flowconfp = calloc(1, sizeof (dlmgmt_flowconf_t))) == NULL)
		return (ENOMEM);

	(void) strlcpy(flowconfp->ld_flow, name, MAXFLOWNAMELEN);
	flowconfp->ld_linkid = linkid;
	flowconfp->ld_zoneid = zoneid;
	flowconfp->ld_onloan = 0;

	*flowconfpp = flowconfp;
	return (err);
}

void
flowconf_destroy(dlmgmt_flowconf_t *flowconfp)
{
	dlmgmt_objattr_t *next, *attrp;

	for (attrp = flowconfp->ld_head; attrp != NULL; attrp = next) {
		next = attrp->lp_next;
		free(attrp->lp_val);
		free(attrp);
	}
	free(flowconfp);
}

int
dlconf_create(const char *name, datalink_id_t linkid, datalink_class_t class,
    uint32_t media, zoneid_t zoneid, dlmgmt_dlconf_t **dlconfpp)
{
	dlmgmt_dlconf_t	*dlconfp = NULL;
	int			err = 0;

	if (dlmgmt_nextconfid == 0) {
		err = ENOSPC;
		goto done;
	}

	if ((dlconfp = calloc(1, sizeof (dlmgmt_dlconf_t))) == NULL) {
		err = ENOMEM;
		goto done;
	}

	(void) strlcpy(dlconfp->ld_link, name, MAXLINKNAMELEN);
	dlconfp->ld_linkid = linkid;
	dlconfp->ld_class = class;
	dlconfp->ld_media = media;
	dlconfp->ld_id = dlmgmt_nextconfid;
	dlconfp->ld_zoneid = zoneid;

done:
	*dlconfpp = dlconfp;
	return (err);
}

void
dlconf_destroy(dlmgmt_dlconf_t *dlconfp)
{
	dlmgmt_objattr_t *next, *attrp;

	for (attrp = dlconfp->ld_head; attrp != NULL; attrp = next) {
		next = attrp->lp_next;
		free(attrp->lp_val);
		free(attrp);
	}
	free(dlconfp);
}

int
dlmgmt_generate_name(const char *prefix, char *name, size_t size,
    zoneid_t zoneid)
{
	dlmgmt_prefix_t	*lpp, *prev = NULL;
	dlmgmt_link_t	link, *linkp;

	/*
	 * See whether the requested prefix is already in the list.
	 */
	for (lpp = &dlmgmt_prefixlist; lpp != NULL;
	    prev = lpp, lpp = lpp->lp_next) {
		if (lpp->lp_zoneid == zoneid &&
		    strcmp(prefix, lpp->lp_prefix) == 0)
			break;
	}

	/*
	 * Not found.
	 */
	if (lpp == NULL) {
		assert(prev != NULL);

		/*
		 * First add this new prefix into the prefix list.
		 */
		if ((lpp = malloc(sizeof (dlmgmt_prefix_t))) == NULL)
			return (ENOMEM);

		prev->lp_next = lpp;
		lpp->lp_next = NULL;
		lpp->lp_zoneid = zoneid;
		lpp->lp_nextppa = 0;
		(void) strlcpy(lpp->lp_prefix, prefix, MAXLINKNAMELEN);

		/*
		 * Now determine this prefix's nextppa.
		 */
		(void) snprintf(link.ll_link, MAXLINKNAMELEN, "%s%d",
		    prefix, 0);
		link.ll_zoneid = zoneid;
		if ((linkp = avl_find(&dlmgmt_name_avl, &link, NULL)) != NULL)
			dlmgmt_advance_ppa(linkp);
	}

	if (lpp->lp_nextppa == (uint_t)-1)
		return (ENOSPC);

	(void) snprintf(name, size, "%s%d", prefix, lpp->lp_nextppa);
	return (0);
}

/*
 * Advance the next available ppa value if the name prefix of the current
 * link is in the prefix list.
 */
static void
dlmgmt_advance_ppa(dlmgmt_link_t *linkp)
{
	dlmgmt_prefix_t	*lpp;
	char		prefix[MAXLINKNAMELEN];
	char		linkname[MAXLINKNAMELEN];
	uint_t		start, ppa;

	(void) dlpi_parselink(linkp->ll_link, prefix, sizeof (prefix), &ppa);

	/*
	 * See whether the requested prefix is already in the list.
	 */
	for (lpp = &dlmgmt_prefixlist; lpp != NULL; lpp = lpp->lp_next) {
		if (lpp->lp_zoneid == linkp->ll_zoneid &&
		    strcmp(prefix, lpp->lp_prefix) == 0)
			break;
	}

	/*
	 * If the link name prefix is in the list, advance the
	 * next available ppa for the <prefix>N name.
	 */
	if (lpp == NULL || lpp->lp_nextppa != ppa)
		return;

	start = lpp->lp_nextppa++;
	linkp = AVL_NEXT(&dlmgmt_name_avl, linkp);
	while (lpp->lp_nextppa != start) {
		if (lpp->lp_nextppa == (uint_t)-1) {
			/*
			 * wrapped around. search from <prefix>1.
			 */
			lpp->lp_nextppa = 0;
			(void) snprintf(linkname, MAXLINKNAMELEN,
			    "%s%d", lpp->lp_prefix, lpp->lp_nextppa);
			linkp = link_by_name(linkname, lpp->lp_zoneid);
			if (linkp == NULL)
				return;
		} else {
			if (linkp == NULL)
				return;
			(void) dlpi_parselink(linkp->ll_link, prefix,
			    sizeof (prefix), &ppa);
			if ((strcmp(prefix, lpp->lp_prefix) != 0) ||
			    (ppa != lpp->lp_nextppa)) {
				return;
			}
		}
		linkp = AVL_NEXT(&dlmgmt_name_avl, linkp);
		lpp->lp_nextppa++;
	}
	lpp->lp_nextppa = (uint_t)-1;
}

/*
 * Advance to the next available linkid value.
 */
static void
dlmgmt_advance_linkid(dlmgmt_link_t *linkp)
{
	datalink_id_t	start;

	if (linkp->ll_linkid != dlmgmt_nextlinkid)
		return;

	start = dlmgmt_nextlinkid;
	linkp = AVL_NEXT(&dlmgmt_id_avl, linkp);

	do {
		if (dlmgmt_nextlinkid == DATALINK_MAX_LINKID) {
			/*
			 * wrapped around. search from 1.
			 */
			dlmgmt_nextlinkid = 1;
			if ((linkp = link_by_id(1, GLOBAL_ZONEID)) == NULL)
				return;
		} else {
			dlmgmt_nextlinkid++;
			if (linkp == NULL)
				return;
			if (linkp->ll_linkid != dlmgmt_nextlinkid)
				return;
		}

		linkp = AVL_NEXT(&dlmgmt_id_avl, linkp);
	} while (dlmgmt_nextlinkid != start);

	dlmgmt_nextlinkid = DATALINK_INVALID_LINKID;
}

/*
 * Advance various global values, for example, next linkid value, next ppa for
 * various prefix etc.
 */
void
dlmgmt_advance(dlmgmt_link_t *linkp)
{
	dlmgmt_advance_linkid(linkp);
	dlmgmt_advance_ppa(linkp);
}

/*
 * Advance to the next available dlconf id.
 */
void
dlmgmt_advance_dlconfid(dlmgmt_dlconf_t *dlconfp)
{
	uint_t	start;

	start = dlmgmt_nextconfid++;
	dlconfp = AVL_NEXT(&dlmgmt_dlconf_avl, dlconfp);
	while (dlmgmt_nextconfid != start) {
		if (dlmgmt_nextconfid == 0) {
			dlmgmt_dlconf_t	dlconf;

			/*
			 * wrapped around. search from 1.
			 */
			dlconf.ld_id = dlmgmt_nextconfid = 1;
			dlconfp = avl_find(&dlmgmt_dlconf_avl, &dlconf, NULL);
			if (dlconfp == NULL)
				return;
		} else {
			if ((dlconfp == NULL) ||
			    (dlconfp->ld_id != dlmgmt_nextconfid)) {
				return;
			}
		}
		dlconfp = AVL_NEXT(&dlmgmt_dlconf_avl, dlconfp);
		dlmgmt_nextconfid++;
	}
	dlmgmt_nextconfid = 0;
}

boolean_t
dlmgmt_is_netboot(void)
{
	int reqsz, s, numifs = 0;
	struct ifreq *reqbuf = NULL, *ifr, *ifrlim;
	struct ifconf ifconf;
	boolean_t ret = B_FALSE;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		return (B_FALSE);
	if (ioctl(s, SIOCGIFNUM, (char *)&numifs) == -1)
		goto done;

	reqsz = numifs * sizeof (struct ifreq);
	reqbuf = (struct ifreq *)malloc(reqsz);
	ifconf.ifc_len = reqsz;
	ifconf.ifc_buf = (caddr_t)reqbuf;

	if (ioctl(s, SIOCGIFCONF, &ifconf) < 0)
		goto done;

	ifrlim = &ifconf.ifc_req[ifconf.ifc_len / sizeof (struct ifreq)];
	for (ifr = ifconf.ifc_req; ifr < ifrlim; ifr++) {
		if (strcmp(ifr->ifr_name, LOOPBACK_IF) != 0) {
			ret = B_TRUE;
			dlmgmt_log(LOG_DEBUG, "dlmgmt_is_netboot: found "
			    "interface %s", ifr->ifr_name);
			break;
		}
	}
done:
	if (reqbuf != NULL)
		free(reqbuf);
	(void) close(s);
	return (ret);
}

boolean_t
dlmgmt_is_iscsiboot(void)
{
	uchar_t iscsiboot_hwaddr[DLPI_PHYSADDR_MAX];
	uchar_t null_hwaddr[DLPI_PHYSADDR_MAX];
	int fd, err;
	boolean_t ret = B_FALSE;

	if ((err = dlmgmt_elevate_privileges()) != 0) {
		dlmgmt_log(LOG_DEBUG, "dlmgmt_is_iscsiboot: could not elevate "
		    "privileges: %s", strerror(err));
		return (B_FALSE);
	}
	bzero(iscsiboot_hwaddr, DLPI_PHYSADDR_MAX);
	bzero(null_hwaddr, DLPI_PHYSADDR_MAX);

	if ((fd = open(ISCSI_DRIVER_DEVCTL, O_RDONLY)) == -1) {
		dlmgmt_log(LOG_DEBUG, "dlmgmt_is_iscsiboot: open of %s "
		    "failed: %s", ISCSI_DRIVER_DEVCTL, strerror(errno));
	} else {
		(void) ioctl(fd, ISCSI_BOOT_MAC_ADDR_GET, iscsiboot_hwaddr);
		(void) close(fd);
		ret = (bcmp(iscsiboot_hwaddr, null_hwaddr, DLPI_PHYSADDR_MAX)
		    != 0);
	}
	(void) dlmgmt_drop_privileges();
	return (ret);
}

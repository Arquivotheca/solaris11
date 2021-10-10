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
 * Functions to maintain a table of datalink configuration information.
 */

#ifndef	_DLMGMT_IMPL_H
#define	_DLMGMT_IMPL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <door.h>
#include <libdllink.h>
#include <pthread.h>
#include <sys/avl.h>
#include <sys/dld.h>
#include <sys/param.h>

#define	MAXLINELEN	1024

#define	DLMGMT_FMRI		"svc:/network/datalink-management:default"
#define	DLMGMT_CFG_PG			"config"
#define	DLMGMT_CFG_DEBUG_PROP		"debug"
#define	DLMGMT_LNP_PG			"linkname-policy"
#define	DLMGMT_LNP_PHYS_PREFIX_PROP	"phys-prefix"
#define	DLMGMT_LNP_INITIALIZED_PROP	"initialized"

#define	RESTARTER_FMRI		"svc:/system/svc/restarter:default"
#define	SYSTEM_PG		"system"
#define	RECONFIGURE_PROP	"reconfigure"

#define	LOOPBACK_IF		"lo0"
/*
 * link or flow attribute structure
 */
typedef struct dlmgmt_objattr_s {
	struct dlmgmt_objattr_s	*lp_next;
	struct dlmgmt_objattr_s	*lp_prev;
	char			lp_name[MAXLINKATTRLEN];
	void			*lp_val;
	dladm_datatype_t	lp_type;
	uint_t			lp_sz;
	boolean_t		lp_linkprop;
} dlmgmt_objattr_t;

/*
 * datalink structure
 *
 * A datalink can be either
 * (1) created by a zone for itself
 * (2) created by a zone for some other zone (E.g. automatic vnics)
 * (3) created by a zone and then assigned to some other zone.
 * The creating zone is the link's owner and the zone for which
 * it is created for or to which it is currently assigned is the
 * current or target zone.
 */
typedef struct dlmgmt_link_s {
	dlmgmt_objattr_t	*ll_head;
	char			ll_link[MAXLINKNAMELEN];
	datalink_class_t	ll_class;
	uint32_t		ll_media;
	datalink_id_t		ll_linkid;
	char			ll_physloc[DLD_LOC_STRSIZE];
	zoneid_t		ll_owner_zoneid; /* link's owner */
	zoneid_t		ll_zoneid; /* link's current zone */
	boolean_t		ll_onloan; /* loaned by owner */
	avl_node_t		ll_name_node;
	avl_node_t		ll_id_node;
	avl_node_t		ll_loan_node;
	uint32_t		ll_flags;
	uint32_t		ll_gen;		/* generation number */
} dlmgmt_link_t;

/*
 * link configuration request structure
 */
typedef struct dlmgmt_dlconf_s {
	dlmgmt_objattr_t	*ld_head;
	char			ld_link[MAXLINKNAMELEN];
	datalink_id_t		ld_linkid;
	datalink_class_t	ld_class;
	uint32_t		ld_media;
	int			ld_id;
	zoneid_t		ld_zoneid;
	uint32_t		ld_gen;
	avl_node_t		ld_node;
} dlmgmt_dlconf_t;

/*
 * per zone flow avl tree
 */
typedef struct dlmgmt_flowavl_s {
	zoneid_t		la_zoneid;
	avl_tree_t		la_flowtree;
	avl_node_t		la_node;
} dlmgmt_flowavl_t;

/*
 * flow configuration request structure
 */
typedef struct dlmgmt_flowconf_s {
	dlmgmt_objattr_t	*ld_head;
	char			ld_flow[MAXFLOWNAMELEN];
	datalink_id_t		ld_linkid;
	zoneid_t		ld_zoneid;
	boolean_t		ld_onloan;
	avl_node_t		ld_node;
} dlmgmt_flowconf_t;

typedef enum {
	DLMGMT_OBJ_LINK = 0,
	DLMGMT_OBJ_FLOW
} dlmgmt_obj_type_t;

typedef struct dlmgmt_obj_s {
	dlmgmt_obj_type_t		otype;
	union {
		dlmgmt_link_t		*ou_link;
		dlmgmt_flowconf_t	*ou_flow;
	} ounion;
} dlmgmt_obj_t;

#define	olink	ounion.ou_link
#define	oflow	ounion.ou_flow

#define	DLMGMT_PHYSLOC_VECTOR_SIZE	64

typedef enum dlmgmt_devtype {
	DLMGMT_DEVTYPE_ETHERNET = 0,
	DLMGMT_DEVTYPE_IPOIB,
	DLMGMT_DEVTYPE_EOIB,
	DLMGMT_DEVTYPE_WIFI
} dlmgmt_devtype_t;


typedef struct dlmgmt_physloc_s {
	char			pl_dev[MAXLINKNAMELEN];
	char			pl_phys_path[MAXPATHLEN];
	uint32_t		pl_vector[DLMGMT_PHYSLOC_VECTOR_SIZE];
	uint_t			pl_vector_len;
	boolean_t		pl_located;
	char			pl_ib_hca_dev[MAXNAMELEN];
	char			pl_label[DLD_LOC_STRSIZE];
	char			pl_label_suffix[DLD_LOC_STRSIZE];
	uint_t			pl_index;
	uint_t			pl_refcnt;
	dlmgmt_link_t		*pl_link;
	avl_node_t		pl_node;
} dlmgmt_physloc_t;

extern boolean_t	debug;
extern boolean_t	fg;
extern boolean_t	reconfigure;
extern FILE		*msglog_fp;
extern const char	*progname;
extern char		cachefile[];
extern dladm_handle_t	dld_handle;
extern datalink_id_t	dlmgmt_nextlinkid;
extern avl_tree_t	dlmgmt_name_avl;
extern avl_tree_t	dlmgmt_id_avl;
extern avl_tree_t	dlmgmt_loan_avl;
extern avl_tree_t	dlmgmt_dlconf_avl;
extern avl_tree_t	dlmgmt_flowconf_avl;
extern char		dlmgmt_phys_prefix[MAXLINKNAMELEN];

boolean_t	objattr_equal(dlmgmt_objattr_t **, const char *, void *,
		    size_t);
dlmgmt_objattr_t *objattr_find(dlmgmt_objattr_t *, const char *);
void		objattr_unset(dlmgmt_objattr_t **, const char *);
int		objattr_set(dlmgmt_objattr_t **, const char *, void *,
		    size_t, dladm_datatype_t);
int		objattr_get(dlmgmt_objattr_t **, const char *, void **,
		    size_t *, dladm_datatype_t *);
void		linkattr_destroy(dlmgmt_link_t *);

void		link_destroy(dlmgmt_link_t *);
int		link_activate(dlmgmt_link_t *);
boolean_t	link_is_visible(dlmgmt_link_t *, zoneid_t, uint32_t);
dlmgmt_link_t	*link_by_id(datalink_id_t, zoneid_t);
dlmgmt_link_t	*link_by_name(const char *, zoneid_t);
int		dlmgmt_zonelinkname(dlmgmt_link_t *, zoneid_t, boolean_t,
		    boolean_t, char *, size_t);

int		dlmgmt_create_common(const char *, datalink_class_t,
		    uint32_t, boolean_t, zoneid_t, zoneid_t, uint32_t,
		    dlmgmt_link_t **);
int		dlmgmt_destroy_common(dlmgmt_link_t *, uint32_t);
int		dlmgmt_getattr_common(dlmgmt_objattr_t **, const char *,
		    dlmgmt_getattr_retval_t *);

void		dlmgmt_advance(dlmgmt_link_t *);
void		dlmgmt_table_lock(boolean_t);
void		dlmgmt_table_unlock();

int		dlconf_create(const char *, datalink_id_t, datalink_class_t,
		    uint32_t, zoneid_t, dlmgmt_dlconf_t **);
void		dlconf_destroy(dlmgmt_dlconf_t *);
void		dlmgmt_advance_dlconfid(dlmgmt_dlconf_t *);
void		dlmgmt_dlconf_table_lock(boolean_t);
void		dlmgmt_dlconf_table_unlock(void);

int		dlmgmt_generate_name(const char *, char *, size_t, zoneid_t);

void		dlmgmt_linktable_init(void);
void		dlmgmt_linktable_fini(void);

int		dlmgmt_zone_init(zoneid_t);
int		dlmgmt_elevate_privileges(void);
int		dlmgmt_drop_privileges();
void		dlmgmt_handler(void *, char *, size_t, door_desc_t *, uint_t);
void		dlmgmt_log(int, const char *, ...);
int		dlmgmt_write_db_entry(const char *, dlmgmt_obj_t *, uint32_t,
		    const char *);
int		dlmgmt_delete_db_entry(dlmgmt_obj_t *, uint32_t, const char *);
int 		dlmgmt_db_init(zoneid_t);
void		dlmgmt_db_fini(zoneid_t);
int		dlmgmt_set_linkname_policy(char *, boolean_t);
void		dlmgmt_flowconf_table_lock(boolean_t);
void		dlmgmt_flowconf_table_unlock(void);
int		flowconf_avl_create(zoneid_t, dlmgmt_flowavl_t **);
void		flowconf_destroy(dlmgmt_flowconf_t *);
int		flowconf_create(const char *, datalink_id_t, zoneid_t,
		    dlmgmt_flowconf_t **);
dlmgmt_flowconf_t	*flow_by_linkid(datalink_id_t, zoneid_t,
			    dlmgmt_flowconf_t *);

void		dlmgmt_physloc_init(void);
dlmgmt_physloc_t *
		dlmgmt_physloc_get(const char *, boolean_t, boolean_t,
		    uint_t *);
void		dlmgmt_physloc_free(dlmgmt_physloc_t *);

boolean_t	dlmgmt_is_netboot(void);
boolean_t	dlmgmt_is_iscsiboot(void);
int		dlmgmt_smf_get_string_property(const char *, const char *,
		    const char *, char *, size_t);
int		dlmgmt_smf_get_boolean_property(const char *, const char *,
		    const char *, boolean_t *);
int		dlmgmt_smf_set_string_property(const char *, const char *,
		    const char *, const char *);
int		dlmgmt_smf_set_boolean_property(const char *, const char *,
		    const char *, boolean_t);

#ifdef  __cplusplus
}
#endif

#endif	/* _DLMGMT_IMPL_H */

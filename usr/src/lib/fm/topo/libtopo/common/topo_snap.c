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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Snapshot Library Interfaces
 *
 * Consumers of topology data may use the interfaces in this file to open,
 * snapshot and close a topology exported by FMRI scheme (hc, mem and cpu)
 * builtin plugins and their helper modules.  A topology handle is obtained
 * by calling topo_open().  Upon a successful return, the caller may use this
 * handle to open a new snapshot.  Each snapshot is assigned a Universally
 * Unique Identifier that in a future enchancement to the libtopo API will be
 * used as the file locator in /var/fm/topo to persist new snapshots or lookup
 * a previously captured snapshot.  topo_snap_hold() will capture the current
 * system topology.  All consumers of the topo_hdl_t argument will be
 * blocked from accessing the topology trees until the snapshot completes.
 *
 * A snapshot may be cleared by calling topo_snap_rele().  As with
 * topo_snap_hold(), all topology accesses are blocked until the topology
 * trees have been released and deallocated.
 *
 * Walker Library Interfaces
 *
 * Once a snapshot has been taken with topo_snap_hold(), topo_hdl_t holders
 * may initiate topology tree walks on a scheme-tree basis.  topo_walk_init()
 * will initiate the data structures required to walk any one one of the
 * FMRI scheme trees.  The walker data structure, topo_walk_t, is an opaque
 * handle passed to topo_walk_step to begin the walk.  At each node in the
 * topology tree, a callback function is called with access to the node at
 * which our current walk falls.  The callback function is passed in during
 * calls to topo_walk_init() and used throughout the walk_step of the
 * scheme tree.  At any time, the callback may terminate the walk by returning
 * TOPO_WALK_TERMINATE or TOPO_WALK_ERR.  TOPO_WALK_NEXT will continue the walk.
 *
 * The type of walk through the tree may be sibling first or child first by
 * respectively passing in TOPO_WALK_SIBLING or TOPO_WALK_CHILD to
 * the topo_walk_step() function.  Topology nodes
 * associated with an outstanding walk are held in place and will not be
 * deallocated until the walk through that node completes.
 *
 * Once the walk has terminated, the walking process should call
 * topo_walk_fini() to clean-up resources created in topo_walk_init()
 * and release nodes that may be still held.
 */

#include <alloca.h>
#include <ctype.h>
#include <pthread.h>
#include <limits.h>
#include <assert.h>
#include <fcntl.h>
#include <smbios.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>
#include <sys/utsname.h>
#include <uuid/uuid.h>
#include <unistd.h>
#include <zone.h>

#include <fm/libtopo.h>
#include <sys/fm/protocol.h>

#include <topo_alloc.h>
#include <topo_builtin.h>
#include <topo_string.h>
#include <topo_error.h>
#include <topo_subr.h>

static void topo_snap_destroy(topo_hdl_t *);

static topo_hdl_t *
set_open_errno(topo_hdl_t *thp, int *errp, int err)
{
	if (thp != NULL) {
		topo_close(thp);
	}
	if (errp != NULL)
		*errp = err;
	return (NULL);
}

topo_hdl_t *
topo_open(int version, const char *rootdir, int *errp)
{
	struct stat	st;
	topo_hdl_t	*thp = NULL;
	topo_alloc_t	*tap;

	char		platform[MAXNAMELEN];
	char		isa[MAXNAMELEN];
	struct utsname	uts;
	char		*product;
	const char	*psn;
	const char	*csn;
	char		*bufp;

	di_node_t	rooth;
	di_prom_handle_t promh;
	smbios_hdl_t	*shp = NULL;
	smbios_system_t	s1;
	smbios_info_t	s2;
	id_t		id;

	char		*dbflags;
	char		*dbout;

	if (version != TOPO_VERSION)
		return (set_open_errno(thp, errp, ETOPO_HDL_ABIVER));

	if ((rootdir != NULL) && (stat(rootdir, &st) < 0))
		return (set_open_errno(thp, errp, ETOPO_HDL_INVAL));

	if ((thp = topo_zalloc(sizeof (topo_hdl_t), 0)) == NULL)
		return (set_open_errno(thp, errp, ETOPO_NOMEM));

	(void) pthread_mutex_init(&thp->th_lock, NULL);

	dbflags	 = getenv("TOPO_DEBUG");
	dbout = getenv("TOPO_DEBUG_OUT");
	if (dbflags != NULL)
		topo_debug_set(thp, dbflags, dbout);

	if ((tap = topo_zalloc(sizeof (topo_alloc_t), 0)) == NULL)
		return (set_open_errno(thp, errp, ETOPO_NOMEM));

	/*
	 * Install default allocators
	 */
	tap->ta_flags = 0;
	tap->ta_alloc = topo_alloc;
	tap->ta_zalloc = topo_zalloc;
	tap->ta_free = topo_free;
	tap->ta_nvops.nv_ao_alloc = topo_nv_alloc;
	tap->ta_nvops.nv_ao_free = topo_nv_free;
	(void) nv_alloc_init(&tap->ta_nva, &tap->ta_nvops);
	thp->th_alloc = tap;

	if ((thp->th_modhash = topo_modhash_create(thp)) == NULL)
		return (set_open_errno(thp, errp, ETOPO_NOMEM));

	/*
	 * Set-up system information and search paths for modules
	 * and topology map files
	 */
	if (rootdir == NULL) {
		rootdir = topo_hdl_strdup(thp, "/");
		thp->th_rootdir = (char *)rootdir;
	} else {
		int len;
		char *rpath;

		len = strlen(rootdir);
		if (len >= PATH_MAX)
			return (set_open_errno(thp, errp, EINVAL));

		if (rootdir[len - 1] != '/') {
			rpath = alloca(len + 2);
			(void) snprintf(rpath, len + 2, "%s/", rootdir);
		} else {
			rpath = (char *)rootdir;
		}
		thp->th_rootdir = topo_hdl_strdup(thp, rpath);
	}

	/* setup sysinfo based authority */
	platform[0] = '\0';
	isa[0] = '\0';
	uts.machine[0] = '\0';
	uts.nodename[0] = '\0';
	(void) sysinfo(SI_PLATFORM, platform, sizeof (platform));
	(void) sysinfo(SI_ARCHITECTURE, isa, sizeof (isa));
	(void) uname(&uts);
	thp->th_platform = topo_hdl_strdup(thp, platform);
	thp->th_isa = topo_hdl_strdup(thp, isa);
	thp->th_machine = topo_hdl_strdup(thp, uts.machine);
	thp->th_server = topo_cleanup_auth_str(thp, uts.nodename);

	/* get handles to soures of authority */
	promh = di_prom_init();
	rooth = di_init("/", DINFOPROP);
	shp = smbios_open(NULL, SMB_VERSION, 0, NULL);

	/* determine product authority */
	product = NULL;
	if (shp &&
	    ((id = smbios_info_system(shp, &s1)) != SMB_ERR) &&
	    (smbios_info_common(shp, id, &s2) != SMB_ERR) &&
	    (strcmp(s2.smbi_product, SMB_DEFAULT1) != 0) &&
	    (strcmp(s2.smbi_product, SMB_DEFAULT2) != 0))
		product = (char *)s2.smbi_product;
	if ((product == NULL) && promh && rooth &&
	    (di_prom_prop_lookup_bytes(promh, rooth, "name",
	    (unsigned char **)&bufp) != -1))
		product = bufp;
	if (product == NULL)
		product = thp->th_platform;

	/* determine psn authority */
	psn = NULL;
	if (shp)
		psn = smbios_psn(shp);

	/* determine csn authority */
	csn = NULL;
	if (shp)
		csn = smbios_csn(shp);
	if ((csn == NULL) && promh && rooth &&
	    (di_prom_prop_lookup_bytes(promh, rooth, "chassis-sn",
	    (unsigned char **)&bufp) != -1))
		csn = bufp;

	/*
	 * Cleanup the authority information we found. Note that
	 * topo_cleanup_auth_str imposes a MAXNAMELEN limit on the
	 * input/result.
	 */
	if (product)
		thp->th_product = topo_cleanup_auth_str(thp, product);
	if (psn)
		thp->th_psn = topo_cleanup_auth_str(thp, psn);
	if (csn)
		thp->th_csn = topo_cleanup_auth_str(thp, csn);

	/* release handles to soures of authority */
	if (shp)
		smbios_close(shp);
	if (rooth != DI_NODE_NIL)
		di_fini(rooth);
	if (promh != DI_PROM_HANDLE_NIL)
		di_prom_fini(promh);

	if (thp->th_rootdir == NULL || thp->th_platform == NULL ||
	    thp->th_machine == NULL)
		return (set_open_errno(thp, errp, ETOPO_NOMEM));

	if (topo_builtin_create(thp, thp->th_rootdir) != 0) {
		topo_dprintf(thp, TOPO_DBG_ERR,
		    "%s: failed to load builtin modules: %s\n",
		    __func__, topo_hdl_errmsg(thp));
		return (set_open_errno(thp, errp, topo_hdl_errno(thp)));
	}

	return (thp);
}

void
topo_close(topo_hdl_t *thp)
{
	ttree_t *tp;

	topo_hdl_lock(thp);

	if (thp->th_rootdir != NULL)
		topo_hdl_strfree(thp, thp->th_rootdir);
	if (thp->th_platform != NULL)
		topo_hdl_strfree(thp, thp->th_platform);
	if (thp->th_isa != NULL)
		topo_hdl_strfree(thp, thp->th_isa);
	if (thp->th_machine != NULL)
		topo_hdl_strfree(thp, thp->th_machine);
	if (thp->th_server != NULL)
		topo_hdl_strfree(thp, thp->th_server);
	if (thp->th_product != NULL)
		topo_hdl_strfree(thp, thp->th_product);
	if (thp->th_psn != NULL)
		topo_hdl_strfree(thp, thp->th_psn);
	if (thp->th_csn != NULL)
		topo_hdl_strfree(thp, thp->th_csn);

	if (thp->th_ipmi != NULL)
		ipmi_close(thp->th_ipmi);
	if (thp->th_smbios != NULL)
		smbios_close(thp->th_smbios);

	/*
	 * Clean-up snapshot
	 */
	topo_snap_destroy(thp);

	/*
	 * Clean-up trees
	 */
	while ((tp = topo_list_next(&thp->th_trees)) != NULL) {
		topo_list_delete(&thp->th_trees, tp);
		topo_tree_destroy(tp);
	}

	/*
	 * Unload all plugins
	 */
	topo_modhash_unload_all(thp);

	if (thp->th_modhash != NULL)
		topo_modhash_destroy(thp);
	if (thp->th_alloc != NULL)
		topo_free(thp->th_alloc, sizeof (topo_alloc_t));

	topo_hdl_unlock(thp);

	topo_free(thp, sizeof (topo_hdl_t));
}

static char *
topo_snap_create(topo_hdl_t *thp, int *errp, boolean_t need_force)
{
	uuid_t uuid;
	char *ustr = NULL;
	boolean_t load_snapshot;

	topo_hdl_lock(thp);
	if (thp->th_uuid != NULL) {
		*errp = ETOPO_HDL_UUID;
		topo_dprintf(thp, TOPO_DBG_ERR,
		    "%s: th_uuid should be NULL\n", __func__);
		topo_hdl_unlock(thp);
		return (NULL);
	}

	/*
	 * If we're loading a topo snapshot rather than generating
	 * a new one from scratch, then thp->th_snap_dir will be
	 * non-null and the name of a sub-directory under the top
	 * level topo snapshots directory.
	 */
	load_snapshot = ((thp->th_snap_dir == NULL) ? B_FALSE : B_TRUE);

	topo_dprintf(thp, TOPO_DBG_SNAP, "%s: thp=0x%p, load_snapshot=%s\n",
	    __func__, (void *)thp,
	    (load_snapshot == B_TRUE) ? "TRUE" : "FALSE");

	/*
	 * If we're not loading a topo snapshot then generate a uuid,
	 * otherwise, one will be read in from the snapshot.
	 * We also don't need the devinfo or prominfo handles
	 * when loading a snapshot since those are only used
	 * by enumerators.
	 */
	if (load_snapshot == B_FALSE) {

		thp->th_timestamp = time(NULL);

		if ((thp->th_uuid = topo_hdl_zalloc(thp, TOPO_UUID_SIZE))
		    == NULL) {
			*errp = ETOPO_NOMEM;
			topo_dprintf(thp, TOPO_DBG_ERR, "%s: unable to "
			    "allocate uuid: %s\n", __func__,
			    topo_strerror(*errp));
			topo_hdl_unlock(thp);
			return (NULL);
		}

		/*
		 * Use uuid_generate_time() if debug option is set.
		 */
		if (thp->th_debug & TOPO_DBG_UUID) {
			struct timeval uuid_timeval;
			time_t time;

			uuid_generate_time(uuid);
			time = uuid_time(uuid, &uuid_timeval);
			topo_dprintf(thp, TOPO_DBG_SNAP,
			    "%s: using uuid_generate_time(), time=0x%lx(%s)\n",
			    __func__, (ulong_t)time, ctime(&time));

		} else {
			uuid_generate(uuid);
		}

		uuid_unparse(uuid, thp->th_uuid);

		topo_dprintf(thp, TOPO_DBG_SNAP, "%s: new snapshot uuid=%s, "
		    "timestamp=0x%lx(%s)\n", __func__, thp->th_uuid,
		    (ulong_t)thp->th_timestamp, ctime(&(thp->th_timestamp)));

		if (need_force) {
			topo_dprintf(thp, TOPO_DBG_FORCE,
			    "%s: taking a DINFOFORCE snapshot\n", __func__);
			thp->th_di = di_init("/", DINFOFORCE |
			    DINFOSUBTREE | DINFOMINOR | DINFOPROP | DINFOPATH);
		} else {
			thp->th_di = di_init("/", DINFOCACHE);
		}
		thp->th_pi = di_prom_init();
	} else {
		topo_dprintf(thp, TOPO_DBG_SNAP, "%s: th_snap_dir=%s\n",
		    __func__, thp->th_snap_dir);
	}

	if (topo_tree_enum_all(thp) < 0) {
		if (topo_hdl_errno(thp) == ETOPO_ENUM_FATAL) {
			topo_dprintf(thp, TOPO_DBG_ERR, "%s: fatal "
			    "enumeration error: %s\n",
			    __func__, topo_hdl_errmsg(thp));
			*errp = thp->th_errno;
			if (thp->th_di != DI_NODE_NIL) {
				di_fini(thp->th_di);
				thp->th_di = DI_NODE_NIL;
			}
			if (thp->th_pi != DI_PROM_HANDLE_NIL) {
				di_prom_fini(thp->th_pi);
				thp->th_pi = DI_PROM_HANDLE_NIL;
			}
			topo_hdl_unlock(thp);
			return (NULL);
		} else {
			topo_dprintf(thp, TOPO_DBG_ERR, "%s: non-fatal "
			    "enumeration error: %s\n",
			    __func__, topo_hdl_errmsg(thp));
		}
	}

	if (thp->th_ipmi != NULL &&
	    ipmi_sdr_changed(thp->th_ipmi) &&
	    ipmi_sdr_refresh(thp->th_ipmi) != 0) {
		topo_dprintf(thp, TOPO_DBG_ERR,
		    "%s: failed to refresh IPMI sdr repository: %s\n",
		    __func__, ipmi_errmsg(thp->th_ipmi));
	}

	if (thp->th_uuid == NULL) {
		*errp = ETOPO_HDL_UUID;
		topo_dprintf(thp, TOPO_DBG_ERR,
		    "%s: th_uuid should not be NULL\n", __func__);
		topo_hdl_unlock(thp);
		return (NULL);
	}

	if ((ustr = topo_hdl_strdup(thp, thp->th_uuid)) == NULL) {
		*errp = ETOPO_NOMEM;
		topo_dprintf(thp, TOPO_DBG_ERR, "%s: unable to allocate "
		    "ustr: %s\n", __func__, topo_strerror(*errp));
		topo_hdl_unlock(thp);
		return (NULL);
	}

	topo_hdl_unlock(thp);
	topo_dprintf(thp, TOPO_DBG_SNAP, "%s: returning ustr=%s\n",
	    __func__, ustr);
	return (ustr);
}

/*ARGSUSED*/
static char *
topo_snap_log_create(topo_hdl_t *thp, const char *uuid, int *errp)
{
	return ((char *)uuid);
}

/*ARGSUSED*/
static int
fac_walker(topo_hdl_t *thp, tnode_t *node, void *arg)
{
	int err;
	nvlist_t *out;

	if (topo_method_supported(node, TOPO_METH_FAC_ENUM, 0)) {
		/*
		 * If the facility enumeration method fails, note the failure,
		 * but continue on with the walk.
		 */
		if (topo_method_invoke(node, TOPO_METH_FAC_ENUM, 0, NULL, &out,
		    &err) != 0) {
			topo_dprintf(thp, TOPO_DBG_ERR,
			    "%s: facility enumeration method failed on "
			    "node %s=%d (%s)\n", __func__, topo_node_name(node),
			    topo_node_instance(node), topo_strerror(err));
		}
	}
	return (TOPO_WALK_NEXT);
}

/*
 * Return snapshot id
 */
char *
topo_snap_hold(topo_hdl_t *thp, const char *uuid, int *errp)
{
	return (topo_snap_hold_flag(thp, uuid, errp, 0));
}

char *
topo_snap_hold_flag(topo_hdl_t *thp, const char *uuid, int *errp, int flag)
{
	topo_walk_t	*twp;
	char		*ret;

	if (thp == NULL)
		return (NULL);

	if (uuid)
		return (topo_snap_log_create(thp, uuid, errp));

	/* In global zone, bracket snapshot with di_cromk_begin/end. */
	if ((getzoneid() == 0) && (flag & TOPO_SNAP_HOLD_CROMK))
		thp->th_cromk_hdl = di_cromk_begin(0);

	ret = topo_snap_create(thp, errp,
	    (thp->th_debug & TOPO_DBG_FORCE) ? B_TRUE : B_FALSE);

	if (ret == NULL) {
		if (thp->th_cromk_hdl) {
			di_cromk_end(thp->th_cromk_hdl, DI_CROMK_END_ABANDON,
			    NULL, NULL, NULL, 0);
			thp->th_cromk_hdl = NULL;
		}
		return (NULL);
	}

	if (getzoneid())
		return (ret);

	/* global zone: walk and invoke facility enumeration methods. */
	twp = topo_walk_init(thp,
	    FM_FMRI_SCHEME_HC, fac_walker, (void *)0, errp);
	if (twp) {
		(void) topo_walk_step(twp, TOPO_WALK_CHILD);
		topo_walk_fini(twp);
	}

	if (thp->th_cromk_hdl) {
		di_cromk_end(thp->th_cromk_hdl, DI_CROMK_END_COMMIT,
		    thp->th_server, thp->th_product, thp->th_csn,
		    thp->th_di ? di_cna_dev(thp->th_di) : 0);
		thp->th_cromk_hdl = NULL;
	}
	return (ret);
}

/*ARGSUSED*/
static int
topo_walk_destroy(topo_hdl_t *thp, tnode_t *node, void *notused)
{
	tnode_t *cnode;

	cnode = topo_child_first(node);

	if (cnode != NULL)
		return (TOPO_WALK_NEXT);

	topo_node_unbind(node);

	return (TOPO_WALK_NEXT);
}

static void
topo_snap_destroy(topo_hdl_t *thp)
{
	int i;
	ttree_t *tp;
	topo_walk_t *twp;
	tnode_t *root;
	topo_nodehash_t *nhp;
	topo_mod_t *mod;

	for (tp = topo_list_next(&thp->th_trees); tp != NULL;
	    tp = topo_list_next(tp)) {

		root = tp->tt_root;
		twp = tp->tt_walk;
		/*
		 * Clean-up tree nodes from the bottom-up
		 */
		if ((twp->tw_node = topo_child_first(root)) != NULL) {
			twp->tw_cb = topo_walk_destroy;
			topo_node_hold(root);
			topo_node_hold(twp->tw_node); /* released at walk end */
			(void) topo_walk_bottomup(twp, TOPO_WALK_CHILD);
			topo_node_rele(root);
		}

		/*
		 * Tidy-up the root node
		 */
		while ((nhp = topo_list_next(&root->tn_children)) != NULL) {
			for (i = 0; i < nhp->th_arrlen; i++) {
				assert(nhp->th_nodearr[i] == NULL);
			}
			mod = nhp->th_enum;
			topo_mod_strfree(mod, nhp->th_name);
			topo_mod_free(mod, nhp->th_nodearr,
			    nhp->th_arrlen * sizeof (tnode_t *));
			topo_list_delete(&root->tn_children, nhp);
			topo_mod_free(mod, nhp, sizeof (topo_nodehash_t));
			topo_mod_rele(mod);
		}

	}

	/*
	 * Clean-up our cached devinfo and prom tree handles.
	 */
	if (thp->th_di != DI_NODE_NIL) {
		di_fini(thp->th_di);
		thp->th_di = DI_NODE_NIL;
	}
	if (thp->th_pi != DI_PROM_HANDLE_NIL) {
		di_prom_fini(thp->th_pi);
		thp->th_pi = DI_PROM_HANDLE_NIL;
	}

	if (thp->th_uuid != NULL) {
		topo_hdl_free(thp, thp->th_uuid, TOPO_UUID_SIZE);
		thp->th_uuid = NULL;
	}

	topo_hdl_strfree(thp, thp->th_snap_dir);
	thp->th_snap_dir = NULL;
}

void
topo_snap_release(topo_hdl_t *thp)
{
	if (thp == NULL)
		return;

	topo_hdl_lock(thp);
	topo_snap_destroy(thp);
	topo_hdl_unlock(thp);
}

topo_walk_t *
topo_walk_init(topo_hdl_t *thp, const char *scheme, topo_walk_cb_t cb_f,
    void *pdata, int *errp)
{
	ttree_t *tp;
	topo_walk_t *wp;

	topo_dprintf(thp, TOPO_DBG_SNAP, "%s: thp=0x%p, uuid=%s\n",
	    __func__, (void *)thp, thp->th_uuid);

	for (tp = topo_list_next(&thp->th_trees); tp != NULL;
	    tp = topo_list_next(tp)) {
		if (strcmp(scheme, tp->tt_scheme) == 0) {

			/*
			 * Hold the root node and start walk at the first
			 * child node
			 */
			assert(tp->tt_root != NULL);

			if ((wp = topo_node_walk_init(thp, NULL, tp->tt_root,
			    cb_f, pdata, errp)) == NULL) /* errp set */
				return (NULL);

			return (wp);
		}
	}

	*errp = ETOPO_WALK_NOTFOUND;
	return (NULL);
}

static int
step_child(tnode_t *cnp, topo_walk_t *wp, int flag, int bottomup)
{
	int status;
	tnode_t *nnp;

	nnp = topo_child_first(cnp);

	if (nnp == NULL) {
		topo_dprintf(wp->tw_thp, TOPO_DBG_WALK,
		    "%s: TOPO_WALK_TERMINATE for %s=%d\n",
		    __func__, cnp->tn_name, cnp->tn_instance);
		return (TOPO_WALK_TERMINATE);
	}

	topo_dprintf(wp->tw_thp, TOPO_DBG_WALK,
	    "%s: walk through node %s=%d to %s=%d\n", __func__,
	    cnp->tn_name, cnp->tn_instance, nnp->tn_name, nnp->tn_instance);

	topo_node_hold(nnp); /* released on return from walk_step */
	wp->tw_node = nnp;
	if (bottomup == 1)
		status = topo_walk_bottomup(wp, flag);
	else
		status = topo_walk_step(wp, flag);

	return (status);
}

static int
step_sibling(tnode_t *cnp, topo_walk_t *wp, int flag, int bottomup)
{
	int status;
	tnode_t *nnp;

	nnp = topo_child_next(cnp->tn_parent, cnp);

	if (nnp == NULL) {
		topo_dprintf(wp->tw_thp, TOPO_DBG_WALK,
		    "%s: TOPO_WALK_TERMINATE for %s=%d\n", __func__,
		    cnp->tn_name, cnp->tn_instance);
		return (TOPO_WALK_TERMINATE);
	}

	topo_dprintf(wp->tw_thp, TOPO_DBG_WALK,
	    "%s: through sibling node %s=%d to %s=%d\n", __func__,
	    cnp->tn_name, cnp->tn_instance, nnp->tn_name, nnp->tn_instance);

	topo_node_hold(nnp); /* released on return from walk_step */
	wp->tw_node = nnp;
	if (bottomup == 1)
		status = topo_walk_bottomup(wp, flag);
	else
		status = topo_walk_step(wp, flag);

	return (status);
}

int
topo_walk_byid(topo_walk_t *wp, const char *name, topo_instance_t inst)
{
	int status;
	tnode_t *nnp, *cnp;

	cnp = wp->tw_node;
	nnp = topo_node_lookup(cnp, name, inst);
	if (nnp == NULL)
		return (TOPO_WALK_TERMINATE);

	topo_node_hold(nnp);
	wp->tw_node = nnp;
	if (wp->tw_mod != NULL)
		status = wp->tw_cb(wp->tw_mod, nnp, wp->tw_pdata);
	else
		status = wp->tw_cb(wp->tw_thp, nnp, wp->tw_pdata);
	topo_node_rele(nnp);
	wp->tw_node = cnp;

	return (status);
}

int
topo_walk_bysibling(topo_walk_t *wp, const char *name, topo_instance_t inst)
{
	int status;
	tnode_t *cnp, *pnp;

	cnp = wp->tw_node;
	pnp = topo_node_parent(cnp);
	assert(pnp != NULL);

	topo_node_hold(pnp);
	wp->tw_node = pnp;
	status = topo_walk_byid(wp, name, inst);
	topo_node_rele(pnp);
	wp->tw_node = cnp;

	return (status);
}

int
topo_walk_step(topo_walk_t *wp, int flag)
{
	int status;
	tnode_t *cnp = wp->tw_node;

	if (flag != TOPO_WALK_CHILD && flag != TOPO_WALK_SIBLING) {
		topo_node_rele(cnp);
		return (TOPO_WALK_ERR);
	}

	/*
	 * No more nodes to walk
	 */
	if (cnp == NULL) {
		topo_dprintf(wp->tw_thp, TOPO_DBG_WALK,
		    "%s: walk terminated\n", __func__);
		topo_node_rele(cnp);
		return (TOPO_WALK_TERMINATE);
	}


	if (wp->tw_mod != NULL)
		status = wp->tw_cb(wp->tw_mod, cnp, wp->tw_pdata);
	else
		status = wp->tw_cb(wp->tw_thp, cnp, wp->tw_pdata);

	/*
	 * Walker callback says we're done
	 */
	if (status != TOPO_WALK_NEXT) {
		topo_node_rele(cnp);
		return (status);
	}

	if (flag == TOPO_WALK_CHILD)
		status = step_child(cnp, wp, flag, 0);
	else
		status = step_sibling(cnp, wp, flag, 0);

	/*
	 * No more nodes in this hash, skip to next node hash by stepping
	 * to next sibling (child-first walk) or next child (sibling-first
	 * walk).
	 */
	if (status == TOPO_WALK_TERMINATE) {
		if (flag == TOPO_WALK_CHILD)
			status = step_sibling(cnp, wp, flag, 0);
		else
			status = step_child(cnp, wp, flag, 0);
	}

	topo_node_rele(cnp); /* done with current node */

	return (status);
}

void
topo_walk_fini(topo_walk_t *wp)
{
	if (wp == NULL)
		return;

	topo_node_rele(wp->tw_root);

	topo_hdl_free(wp->tw_thp, wp, sizeof (topo_walk_t));
}

int
topo_walk_bottomup(topo_walk_t *wp, int flag)
{
	int status;
	tnode_t *cnp;

	if (wp == NULL)
		return (TOPO_WALK_ERR);

	cnp = wp->tw_node;
	if (flag != TOPO_WALK_CHILD && flag != TOPO_WALK_SIBLING) {
		topo_node_rele(cnp);
		return (TOPO_WALK_ERR);
	}

	/*
	 * End of the line
	 */
	if (cnp == NULL) {
		topo_dprintf(wp->tw_thp, TOPO_DBG_WALK,
		    "%s: walk terminated\n", __func__);
		topo_node_rele(cnp);
		return (TOPO_WALK_TERMINATE);
	}

	topo_dprintf(wp->tw_thp, TOPO_DBG_WALK,
	    "%s: %s through node %s=%d\n", __func__,
	    (flag == TOPO_WALK_CHILD ? "TOPO_WALK_CHILD" : "TOPO_WALK_SIBLING"),
	    cnp->tn_name, cnp->tn_instance);

	if (flag == TOPO_WALK_CHILD)
		status = step_child(cnp, wp, flag, 1);
	else
		status = step_sibling(cnp, wp, flag, 1);

	/*
	 * At a leaf, run the callback
	 */
	if (status == TOPO_WALK_TERMINATE) {
		if ((status = wp->tw_cb(wp->tw_thp, cnp, wp->tw_pdata))
		    != TOPO_WALK_NEXT) {
			topo_node_rele(cnp);
			return (status);
		}
	}

	/*
	 * Try next child or sibling
	 */
	if (status == TOPO_WALK_NEXT) {
		if (flag == TOPO_WALK_CHILD)
			status = step_sibling(cnp, wp, flag, 1);
		else
			status = step_child(cnp, wp, flag, 1);
	}

	topo_node_rele(cnp); /* done with current node */

	return (status);
}

di_node_t
topo_hdl_devinfo(topo_hdl_t *thp)
{
	return (thp == NULL ? DI_NODE_NIL : thp->th_di);
}

di_prom_handle_t
topo_hdl_prominfo(topo_hdl_t *thp)
{
	return (thp == NULL ? DI_PROM_HANDLE_NIL : thp->th_pi);
}

/*
 * Load in a saved topology snapshot.
 */
char *
topo_snap_load(topo_hdl_t *thp, const char *snap_dir, int *errp)
{
	char *ustr;

	if (thp == NULL) {
		topo_dprintf(thp, TOPO_DBG_SNAP, "%s: NULL handle\n",
		    __func__);
		return (NULL);
	}

	if (snap_dir == NULL) {
		topo_dprintf(thp, TOPO_DBG_SNAP, "%s: NULL directory\n",
		    __func__);
		return (NULL);
	}

	topo_dprintf(thp, TOPO_DBG_SNAP, "%s: thp=0x%p, snap_dir=%s\n",
	    __func__, (void *)thp, snap_dir);

	thp->th_snap_dir = topo_hdl_strdup(thp, snap_dir);
	ustr = topo_snap_create(thp, errp, B_FALSE);

	return (ustr);
}

di_cromk_hdl_t
topo_hdl_cromk_hdl(topo_hdl_t *thp)
{
	return (thp == NULL ? NULL : thp->th_cromk_hdl);
}

void
topo_cleanup()
{
	/* fmd(1M) is about to exit, call di_cromk_cleanup(). */
	di_cromk_cleanup();
}

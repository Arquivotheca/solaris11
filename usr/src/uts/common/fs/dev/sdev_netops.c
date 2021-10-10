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
 * vnode ops for the /dev/net directory
 *
 *	The lookup is based on the internal vanity naming node table.  We also
 *	override readdir in order to delete net nodes no longer	in-use.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/sunndi.h>
#include <fs/fs_subr.h>
#include <sys/fs/dv_node.h>
#include <sys/fs/sdev_impl.h>
#include <sys/policy.h>
#include <sys/zone.h>
#include <sys/dls.h>

#define	DEVNET_DIR		"/dev/net"
#define	DEVNET_ZONE_DIR		"/dev/net/zone"
#define	DEVNET_ZONE_FULL_DIR	DEVNET_ZONE_DIR "/"

struct vnodeops		*devnet_vnodeops;

/*
 * Returns true if passed zone is found in running state
 * and also sets the zid to the zone ID.
 */
static boolean_t
zone_name2id(char *zname, zoneid_t *zid)
{
	zone_t *zone;
	boolean_t rc;

	if ((zone = zone_find_by_name(zname)) == NULL)
		return (B_FALSE);

	if (zone_status_get(zone) != ZONE_IS_RUNNING) {
		rc = B_FALSE;
		goto fail;
	}

	if (zid != NULL)
		*zid = zone->zone_id;
	rc = B_TRUE;
fail:
	zone_rele(zone);
	return (rc);
}

/*
 * Returns zone ID from the given /dev/net dir path. In the GZ we support both
 * /dev/net and /dev/net/zone/zname dirs. In the NGZ we only support /dev/net.
 * ALL_ZONES is returned when the path is not supported. For the /dev/net case
 * we return the zone ID that owns the sdev node and for /dev/net/zone/zname
 * we return the zone ID from the zname in the path. If sdevzid is not NULL,
 * the zone ID of the passed sdev is returned.
 */
static zoneid_t
devnetpath_zoneid(struct sdev_node *ddv, zoneid_t *sdevzid)
{
	zoneid_t zid = VTOZONE(SDEVTOV(ddv))->zone_id;

	if (sdevzid != NULL)
		*sdevzid = zid;

	if (strcmp(ddv->sdev_path, DEVNET_DIR) == 0)
		return (zid);

	if (strncmp(ddv->sdev_path, DEVNET_ZONE_FULL_DIR,
	    strlen(DEVNET_ZONE_FULL_DIR)) == 0) {
		char *ptr;

		/*
		 * If the sdev_node is not in the global zone then we don't
		 * support lookups of /dev/net/zone and entries under
		 * /dev/net/zone.
		 */
		if (VTOZONE(SDEVTOV(ddv))->zone_id != GLOBAL_ZONEID)
			return (ALL_ZONES);

		/* Retrieve the zone ID from the /dev/net/zone/zname path */
		ptr = ddv->sdev_path + strlen(DEVNET_ZONE_FULL_DIR);
		if (zone_name2id(ptr, &zid))
			return (zid);
	}
	return (ALL_ZONES);
}

/*
 * Check if a net sdev_node is still valid - i.e. it represents a current
 * network link in a zone on the system.
 * This serves two purposes
 *	- only valid net nodes are returned during lookup() and readdir().
 *	- since net sdev_nodes are not actively destroyed when a network link
 *	  goes away, we use the validator to do deferred cleanup i.e. when such
 *	  nodes are encountered during subsequent lookup() and readdir().
 */
int
devnet_validate(struct sdev_node *dv)
{
	datalink_id_t linkid = DATALINK_INVALID_LINKID;
	char zonename[ZONENAME_MAX];
	zoneid_t zoneid;
	char *ptr;
	char *ch;
	size_t size;

	ASSERT(!(dv->sdev_flags & SDEV_STALE));
	ASSERT(dv->sdev_state == SDEV_READY);
	ASSERT(strlen(dv->sdev_path) > strlen(DEVNET_DIR));

	/* Validate the dev node under /dev/net/zone/ */
	if (strncmp(dv->sdev_path, DEVNET_ZONE_FULL_DIR,
	    strlen(DEVNET_ZONE_FULL_DIR)) == 0) {

		/* If node not in GZ dev tree, only valid path is /dev/net */
		if (VTOZONE(SDEVTOV(dv))->zone_id != GLOBAL_ZONEID)
			return (SDEV_VTOR_INVALID);

		ptr = dv->sdev_path + strlen(DEVNET_ZONE_FULL_DIR);

		/*
		 * Determine the zone name and the link name when the given
		 * dev node path is as follows: /dev/net/zone/zname/link0
		 */
		if ((ch = strchr(ptr, '/')) != NULL) {
			size = (ptrdiff_t)ch - (ptrdiff_t)ptr + 1;
			(void) strlcpy(zonename, ptr, size);

			/*
			 * Validate zonename and link in the zonename/link0
			 * path by looking up the zonename prefixed link.
			 */
			if (dls_mgmt_get_linkid(ptr, &linkid) != 0)
				return (SDEV_VTOR_INVALID);
		} else {
			/*
			 * Otherwise just copy the zonename from the path
			 * /dev/net/zone/zonename, dv->sdev_name points to
			 * the zone name.
			 */
			(void) strlcpy(zonename, dv->sdev_name, ZONENAME_MAX);
		}

		if (!zone_name2id(zonename, &zoneid))
			return (SDEV_VTOR_INVALID);
	} else {
		/*
		 * Dev node is under /dev/net. Omit verifying the link ID if
		 * the node is /dev/net/zone. Otherwise, verify the link ID.
		 */
		if (strcmp(dv->sdev_path, DEVNET_ZONE_DIR) != 0 &&
		    dls_mgmt_get_linkid(dv->sdev_name, &linkid) != 0)
			return (SDEV_VTOR_INVALID);
	}

	return (SDEV_VTOR_VALID);
}

/*
 * This callback is invoked from devname_lookup_func() to create
 * a net entry when the node is not found in the cache. ddhp parameter
 * is null if the callback is invoked when creating directory nodes
 * under /dev/net such as /dev/net/zone and /dev/net/zone/{zone}.
 */
static int
devnet_create_rvp(const char *nm, struct vattr *vap, dls_dl_handle_t *ddhp)
{
	timestruc_t now;
	dev_t dev;
	int error;

	/* ddhp is NULL when we are not creating a node for a network device. */
	if (ddhp != NULL) {
		if ((error = dls_devnet_open(nm, ddhp, &dev)) != 0) {
			sdcmn_err12(("devnet_create_rvp: not a valid vanity "
			    "name network node: %s\n", nm));
			return (error);
		}

		/*
		 * This is a valid network device (at least at this point in
		 * time). Create the node by setting the attribute; the rest is
		 * taken care of by devname_lookup_func().
		 */
		*vap = sdev_vattr_chr;
		vap->va_mode |= 0666;
		vap->va_rdev = dev;
	} else {
		/* Return attributes for directory entry */
		*vap = sdev_vattr_dir;
	}

	gethrestime(&now);
	vap->va_atime = now;
	vap->va_mtime = now;
	vap->va_ctime = now;
	return (0);
}

/*
 * Lookup for /dev/net directory
 *	If the entry does not exist, the devnet_create_rvp() callback
 *	is invoked to create it.  Nodes do not persist across reboot.
 */
/*ARGSUSED3*/
static int
devnet_lookup(struct vnode *dvp, char *nm, struct vnode **vpp,
    struct pathname *pnp, int flags, struct vnode *rdir, struct cred *cred,
    caller_context_t *ct, int *direntflags, pathname_t *realpnp)
{
	struct sdev_node *ddv = VTOSDEV(dvp);
	struct sdev_node *dv = NULL;
	dls_dl_handle_t ddh = NULL;
	struct vattr vattr;
	zoneid_t zid;
	int nmlen;
	int error = ENOENT;

	if (SDEVTOV(ddv)->v_type != VDIR)
		return (ENOTDIR);

	/*
	 * Empty name or ., return node itself.
	 */
	nmlen = strlen(nm);
	if ((nmlen == 0) || ((nmlen == 1) && (nm[0] == '.'))) {
		*vpp = SDEVTOV(ddv);
		VN_HOLD(*vpp);
		return (0);
	}

	/*
	 * .., return the parent directory
	 */
	if ((nmlen == 2) && (strcmp(nm, "..") == 0)) {
		*vpp = SDEVTOV(ddv->sdev_dotdot);
		VN_HOLD(*vpp);
		return (0);
	}

	rw_enter(&ddv->sdev_contents, RW_WRITER);

	/*
	 * directory cache lookup:
	 */
	if ((dv = sdev_cache_lookup(ddv, nm)) != NULL) {
		if (dv->sdev_state == SDEV_READY) {
			if (!(dv->sdev_flags & SDEV_ATTR_INVALID))
				goto found;
		} else {
			ASSERT(dv->sdev_state == SDEV_ZOMBIE);
			goto failed;
		}
	}

	/*
	 * ZOMBIED parent does not allow new node creation, bail out early.
	 */
	if (ddv->sdev_state == SDEV_ZOMBIE)
		goto failed;

	if (strcmp(ddv->sdev_path, DEVNET_DIR) == 0) {
		if (strcmp((char *)nm, "zone") != 0) {
			error = devnet_create_rvp(nm, &vattr, &ddh);
		} else if (VTOZONE(SDEVTOV(ddv))->zone_id == GLOBAL_ZONEID) {
			/*
			 * In the global zone we create the /dev/net/zone dir
			 */
			error = devnet_create_rvp(nm, &vattr, NULL);
		}
	} else if (VTOZONE(SDEVTOV(ddv))->zone_id == GLOBAL_ZONEID) {
		/*
		 * In the global zone for paths /dev/net/zone/.. we verify
		 * the zone is valid before creating the zone node and the
		 * datalink under a zone node.
		 */
		if (strncmp(ddv->sdev_path, DEVNET_ZONE_FULL_DIR,
		    strlen(DEVNET_ZONE_FULL_DIR)) == 0) {
			char *ptr;
			char link[MAXLINKNAMESPECIFIER];

			ptr = ddv->sdev_path + strlen(DEVNET_ZONE_FULL_DIR);
			if (zone_name2id(ptr, NULL)) {
				(void) snprintf(link, sizeof (link), "%s/%s",
				    ptr, nm);
				error = devnet_create_rvp(link, &vattr, &ddh);
			}
		} else if (zone_name2id((char *)nm, &zid)) {
			error = devnet_create_rvp(nm, &vattr, NULL);
		}
	} else {
		error = ENOENT;
	}

	if (error != 0)
		goto failed;

	error = sdev_mknode(ddv, nm, &dv, &vattr, NULL, NULL, cred,
	    SDEV_READY);
	if (error != 0) {
		ASSERT(dv == NULL);
		if (ddh != NULL)
			dls_devnet_close(ddh);
		goto failed;
	}

	ASSERT(dv != NULL);

	rw_enter(&dv->sdev_contents, RW_WRITER);
	if (dv->sdev_flags & SDEV_ATTR_INVALID) {
		/*
		 * SDEV_ATTR_INVALID means that this device has been
		 * detached, and its dev_t might've been changed too.
		 * Therefore, sdev_node's 'vattr' needs to be updated.
		 */
		SDEVTOV(dv)->v_rdev = vattr.va_rdev;
		ASSERT(dv->sdev_attr != NULL);
		dv->sdev_attr->va_rdev = vattr.va_rdev;
		dv->sdev_flags &= ~SDEV_ATTR_INVALID;
	}
	ASSERT(dv->sdev_private == NULL);
	dv->sdev_private = ddh;
	rw_exit(&dv->sdev_contents);

found:
	ASSERT(SDEV_HELD(dv));
	rw_exit(&ddv->sdev_contents);
	return (sdev_to_vp(dv, vpp));

failed:
	rw_exit(&ddv->sdev_contents);

	if (dv != NULL)
		SDEV_RELE(dv);

	*vpp = NULL;
	return (error);
}

/*
 * Callback from zone walker to create an entry for the given zone under
 * the /dev/net/zone directory in the global zone.
 */
static int
devnetzone_fillzone(zone_t *zone, void *arg)
{
	struct sdev_node	*ddv = arg;
	struct vattr		vattr;
	struct sdev_node	*dv;

	ASSERT(RW_WRITE_HELD(&ddv->sdev_contents));

	/*
	 * We only create the zone directory node for zones that are
	 * running to avoid creating a number of empty directories.
	 */
	if (zone_status_get(zone) != ZONE_IS_RUNNING)
		return (0);

	if ((dv = sdev_cache_lookup(ddv, zone->zone_name)) != NULL)
		goto found;

	if (devnet_create_rvp(zone->zone_name, &vattr, NULL) != 0)
		return (0);

	if (sdev_mknode(ddv, zone->zone_name, &dv, &vattr, NULL, NULL, kcred,
	    SDEV_READY) != 0) {
		return (0);
	}

	/*
	 * As there is no reference holding the zone, it could be destroyed.
	 * Set SDEV_ATTR_INVALID so that the 'vattr' will be updated later.
	 */
	rw_enter(&dv->sdev_contents, RW_WRITER);
	dv->sdev_flags |= SDEV_ATTR_INVALID;
	rw_exit(&dv->sdev_contents);

found:
	SDEV_SIMPLE_RELE(dv);
	return (0);
}

static int
devnet_filldir_datalink(datalink_id_t linkid, void *arg)
{
	struct sdev_node	*ddv = arg;
	struct vattr		vattr;
	struct sdev_node	*dv;
	dls_dl_handle_t		ddh = NULL;
	char			link[MAXLINKNAMESPECIFIER];
	char			lname[MAXLINKNAMELEN];
	zone_t			*zone;
	zoneid_t		zid;
	zoneid_t		caller_zid;

	ASSERT(RW_WRITE_HELD(&ddv->sdev_contents));

	if (dls_mgmt_get_linkinfo(linkid, lname, NULL, NULL, NULL) != 0)
		return (0);

	if ((dv = sdev_cache_lookup(ddv, (char *)lname)) != NULL)
		goto found;

	/*
	 * Given the parent directory path from the sdev_node retrieve the zone
	 * directory and check whether lookup of the zone is valid from the
	 * caller zone. If the zone in the path is not the current zone then
	 * build the zonename prefixed linkname for use by devnet_create_rvp
	 * to open the link.
	 */
	zid = devnetpath_zoneid(ddv, &caller_zid);
	if (zid == ALL_ZONES) {
		return (0);
	} else if (zid != GLOBAL_ZONEID && zid != caller_zid) {
		ASSERT(caller_zid == GLOBAL_ZONEID);

		if ((zone = zone_find_by_id(zid)) == NULL)
			return (0);
		(void) snprintf(link, sizeof (link), "%s/%s",
		    zone->zone_name, lname);
		zone_rele(zone);
	} else {
		ASSERT(zid == caller_zid);
		(void) strlcpy(link, lname, sizeof (link));
	}

	if (devnet_create_rvp(link, &vattr, &ddh) != 0)
		return (0);

	ASSERT(ddh != NULL);
	dls_devnet_close(ddh);

	if (sdev_mknode(ddv, (char *)lname, &dv, &vattr, NULL, NULL, kcred,
	    SDEV_READY) != 0) {
		return (0);
	}

	/*
	 * As there is no reference holding the network device, it could be
	 * detached. Set SDEV_ATTR_INVALID so that the 'vattr' will be updated
	 * later.
	 */
	rw_enter(&dv->sdev_contents, RW_WRITER);
	dv->sdev_flags |= SDEV_ATTR_INVALID;
	rw_exit(&dv->sdev_contents);

found:
	SDEV_SIMPLE_RELE(dv);
	return (0);
}

/*
 * Validates device and directory nodes in the given directory and
 * cleans up any invalid or stale nodes in the directory. Function
 * expects the caller to hold a read-only lock on sdev_contents.
 * Caller should downgrade the sdev_contents lock to read-only after
 * the call.
 */
static void
devnet_refreshdir(struct sdev_node *ddv)
{
	sdev_node_t	*dv, *next;

	ASSERT(RW_READ_HELD(&ddv->sdev_contents));
	if (rw_tryupgrade(&ddv->sdev_contents) == NULL) {
		rw_exit(&ddv->sdev_contents);
		rw_enter(&ddv->sdev_contents, RW_WRITER);
	}

	for (dv = SDEV_FIRST_ENTRY(ddv); dv; dv = next) {
		next = SDEV_NEXT_ENTRY(ddv, dv);

		/* validate and prune only ready nodes */
		if (dv->sdev_state != SDEV_READY)
			continue;

		switch (devnet_validate(dv)) {
		case SDEV_VTOR_VALID:
		case SDEV_VTOR_SKIP:
			continue;
		case SDEV_VTOR_INVALID:
		case SDEV_VTOR_STALE:
			sdcmn_err12(("devnet_refreshdir: destroy invalid "
			    "node: %s(%p)\n", dv->sdev_name, (void *)dv));
			break;
		}

		if (SDEVTOV(dv)->v_type == VDIR &&
		    (sdev_cleandir(dv, NULL, 0) != 0))
			continue;

		if (SDEVTOV(dv)->v_count > 0)
			continue;

		SDEV_HOLD(dv);
		/* remove the cache node */
		(void) sdev_cache_update(ddv, &dv, dv->sdev_name,
		    SDEV_CACHE_DELETE);
	}
}

/*
 * Called from readdir() on the /dev/net/zone directory in the global zone.
 * Populate the /dev/net/zone directory with a list of directories: a directory
 * for each running zone on the system.
 */
static void
devnetzone_filldir(struct sdev_node *ddv)
{
	ASSERT(VTOZONE(SDEVTOV(ddv))->zone_id == GLOBAL_ZONEID);

	devnet_refreshdir(ddv);
	(void) zone_walk(devnetzone_fillzone, ddv);
	ddv->sdev_flags &= ~SDEV_BUILD;
	rw_downgrade(&ddv->sdev_contents);
}

/*
 * Routine to add datalinks either under /dev/net or /dev/net/zone/{zonename}
 * Passed directory path must be either one of the dirs above.
 */
static void
devnet_filldir(struct sdev_node *ddv)
{
	datalink_id_t	linkid;
	zoneid_t	zid;

	if ((zid = devnetpath_zoneid(ddv, NULL)) == ALL_ZONES)
		return;

	devnet_refreshdir(ddv);

	if (zid == GLOBAL_ZONEID) {
		linkid = DATALINK_INVALID_LINKID;
		do {
			linkid = dls_mgmt_get_next(linkid, DATALINK_CLASS_ALL,
			    DATALINK_ANY_MEDIATYPE, DLMGMT_ACTIVE);
			if (linkid != DATALINK_INVALID_LINKID)
				(void) devnet_filldir_datalink(linkid, ddv);
		} while (linkid != DATALINK_INVALID_LINKID);
	} else {
		(void) zone_datalink_walk(zid, devnet_filldir_datalink, ddv);
	}

	ddv->sdev_flags &= ~SDEV_BUILD;
	rw_downgrade(&ddv->sdev_contents);
}

/*
 * Display all instantiated network datalink device nodes.
 * A /dev/net entry will be created only after the first lookup of
 * the network datalink device succeeds.
 */
/*ARGSUSED4*/
static int
devnet_readdir(struct vnode *dvp, struct uio *uiop, struct cred *cred,
    int *eofp, caller_context_t *ct, int flags)
{
	struct sdev_node *sdvp = VTOSDEV(dvp);
	struct vnode *vp;

	ASSERT(sdvp);

	if (uiop->uio_offset != 0)
		return (devname_readdir_func(dvp, uiop, cred, eofp, 0));

	/* In the NGZ, open /dev/net directory only */
	if (VTOZONE(SDEVTOV(sdvp))->zone_id != GLOBAL_ZONEID) {
		if (strcmp(sdvp->sdev_path, DEVNET_DIR) == 0)
			devnet_filldir(sdvp);
		return (devname_readdir_func(dvp, uiop, cred, eofp, 0));
	}

	/*
	 * In the GZ we support /dev/net, /dev/net/zone and
	 * /dev/net/zone/zname directories. If the given path
	 * is /dev/net/zone then populate the directory with
	 * a listing of zones running on the system.
	 */
	if (strcmp(sdvp->sdev_path, DEVNET_ZONE_DIR) == 0) {
		devnetzone_filldir(sdvp);
	} else {
		if (strcmp(sdvp->sdev_path, DEVNET_DIR) == 0) {
			/*
			 * Create the /dev/net/zone dir node through
			 * a devnet_lookup call.
			 */
			rw_exit(&sdvp->sdev_contents);
			if (devnet_lookup(dvp, "zone", &vp, NULL, 0,
			    NULL, cred, NULL, NULL, NULL) == 0)
				VN_RELE(vp);
			rw_enter(&sdvp->sdev_contents, RW_READER);
		}

		devnet_filldir(sdvp);
	}

	return (devname_readdir_func(dvp, uiop, cred, eofp, 0));
}

/*
 * This callback is invoked from devname_inactive_func() to release
 * the net entry which was held in devnet_create_rvp().
 */
static void
devnet_inactive_callback(struct vnode *dvp)
{
	struct sdev_node *sdvp = VTOSDEV(dvp);
	dls_dl_handle_t ddh;

	if (dvp->v_type == VDIR || dvp->v_type == VLNK)
		return;

	ASSERT(dvp->v_type == VCHR);
	rw_enter(&sdvp->sdev_contents, RW_WRITER);
	ddh = sdvp->sdev_private;
	sdvp->sdev_private = NULL;
	sdvp->sdev_flags |= SDEV_ATTR_INVALID;
	rw_exit(&sdvp->sdev_contents);

	/*
	 * "ddh" (sdev_private) could be NULL if devnet_lookup fails.
	 */
	if (ddh != NULL)
		dls_devnet_close(ddh);
}

/*ARGSUSED*/
static void
devnet_inactive(struct vnode *dvp, struct cred *cred, caller_context_t *ct)
{
	devname_inactive_func(dvp, cred, devnet_inactive_callback);
}

/*
 * We override lookup and readdir to build entries based on the
 * in kernel vanity naming node table.
 */
const fs_operation_def_t devnet_vnodeops_tbl[] = {
	VOPNAME_READDIR,	{ .vop_readdir = devnet_readdir },
	VOPNAME_LOOKUP,		{ .vop_lookup = devnet_lookup },
	VOPNAME_INACTIVE,	{ .vop_inactive = devnet_inactive },
	VOPNAME_CREATE,		{ .error = fs_nosys },
	VOPNAME_REMOVE,		{ .error = fs_nosys },
	VOPNAME_MKDIR,		{ .error = fs_nosys },
	VOPNAME_RMDIR,		{ .error = fs_nosys },
	VOPNAME_SYMLINK,	{ .error = fs_nosys },
	VOPNAME_SETSECATTR,	{ .error = fs_nosys },
	NULL,			NULL
};

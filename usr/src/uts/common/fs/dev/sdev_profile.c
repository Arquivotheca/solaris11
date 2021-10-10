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
 * This file implements /dev filesystem operations for non-global
 * instances. Three major entry points:
 * devname_profile_update()
 *   Update matching rules determining which names to export
 * prof_filldir()
 *   Build the directory contents
 * prof_lookup()
 *   Implements lookup
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/dirent.h>
#include <sys/pathname.h>
#include <sys/fs/dv_node.h>
#include <sys/fs/sdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/sunndi.h>
#include <sys/modctl.h>

enum {
	PROFILE_TYPE_INCLUDE,
	PROFILE_TYPE_EXCLUDE,
	PROFILE_TYPE_MAP,
	PROFILE_TYPE_SYMLINK,
	PROFILE_TYPE_ANNOTATE
};

enum {
	WALK_DIR_CONTINUE = 0,
	WALK_DIR_TERMINATE
};

static const char *sdev_nvp_val_err = "nvpair_value error %d, %s\n";

static void process_rule(struct sdev_node *, struct sdev_node *,
    char *, char *, char *, int);
static void walk_dir(struct vnode *, void *, int (*)(char *, void *));

static void
prof_getattr(struct sdev_node *dir, char *name, struct vnode *gdv,
    struct vattr *vap, struct vnode **avpp, int *no_fs_perm)
{
	struct vnode *advp;
	*avpp = NULLVP;

	/* get attribute from shadow, if present; else get default */
	advp = dir->sdev_attrvp;
	if (advp && VOP_LOOKUP(advp, name, avpp, NULL, 0, NULL, kcred,
	    NULL, NULL, NULL) == 0) {
		(void) VOP_GETATTR(*avpp, vap, 0, kcred, NULL);
	} else if (gdv == NULL || gdv->v_type == VDIR) {
		/* always create shadow directory */
		*vap = sdev_vattr_dir;
		if (advp && VOP_MKDIR(advp, name, &sdev_vattr_dir,
		    avpp, kcred, NULL, 0, NULL) != 0) {
			sdcmn_err10(("prof_getattr: failed to create "
			    "shadow directory %s/%s\n", dir->sdev_path, name));
		}
	} else {
		/*
		 * get default permission from devfs
		 * Before calling devfs_get_defattr, we need to get
		 * the realvp (the dv_node). If realvp is not a dv_node,
		 * devfs_get_defattr() will return a system-wide default
		 * attr for device nodes.
		 */
		struct vnode *rvp;

		if (VOP_REALVP(gdv, &rvp, NULL) != 0)
			rvp = gdv;
		devfs_get_defattr(rvp, vap, no_fs_perm);
	}

	/* ignore dev_t and vtype from backing store */
	if (gdv) {
		vap->va_type = gdv->v_type;
		vap->va_rdev = gdv->v_rdev;
	}
}

static void
apply_glob_pattern(struct sdev_node *pdir, struct sdev_node *cdir)
{
	char *name;
	nvpair_t *nvp = NULL;
	nvlist_t *nvl;
	struct vnode *vp = SDEVTOV(cdir);
	int rv = 0;

	if (vp->v_type != VDIR)
		return;
	name = cdir->sdev_name;
	nvl = pdir->sdev_prof.dev_glob_incdir;
	while (nvp = nvlist_next_nvpair(nvl, nvp)) {
		char *pathleft;
		char *expr = nvpair_name(nvp);
		if (!gmatch(name, expr))
			continue;
		rv = nvpair_value_string(nvp, &pathleft);
		if (rv != 0) {
			cmn_err(CE_WARN, sdev_nvp_val_err,
			    rv, nvpair_name(nvp));
			break;
		}
		process_rule(cdir, cdir->sdev_origin,
		    pathleft, NULL, NULL, PROFILE_TYPE_INCLUDE);
	}

	nvl = pdir->sdev_prof.dev_glob_ann;
	while (nvp = nvlist_next_nvpair(nvl, nvp)) {
		char **array;
		uint_t nelem;
		char *expr = nvpair_name(nvp);
		if (!gmatch(name, expr))
			continue;
		rv = nvpair_value_string_array(nvp, &array, &nelem);
		if (rv != 0) {
			cmn_err(CE_WARN, sdev_nvp_val_err,
			    rv, nvpair_name(nvp));
			break;
		}
		process_rule(cdir, cdir->sdev_origin,
		    array[0], NULL, array[1], PROFILE_TYPE_ANNOTATE);
	}
}

/*
 * Some commonality here with sdev_mknode(), could be simplified.
 * NOTE: prof_mknode returns with *newdv held once, if success.
 */
static int
prof_mknode(struct sdev_node *dir, char *name, struct sdev_node **newdv,
    vattr_t *vap, vnode_t *avp, void *arg, cred_t *cred)
{
	struct sdev_node *dv;
	int rv;

	ASSERT(RW_WRITE_HELD(&dir->sdev_contents));

	/* check cache first */
	if (dv = sdev_cache_lookup(dir, name)) {
		*newdv = dv;
		return (0);
	}

	/* allocate node and insert into cache */
	rv = sdev_nodeinit(dir, name, &dv, NULL);
	if (rv != 0) {
		*newdv = NULL;
		return (rv);
	}

	rv = sdev_cache_update(dir, &dv, name, SDEV_CACHE_ADD);
	*newdv = dv;

	/* put it in ready state */
	rv = sdev_nodeready(*newdv, vap, avp, arg, cred);

	/* handle glob pattern in the middle of a path */
	if (rv == 0) {
		if (SDEVTOV(*newdv)->v_type == VDIR)
			sdcmn_err10(("sdev_origin for %s set to 0x%p\n",
			    name, arg));
		apply_glob_pattern(dir, *newdv);
	}
	return (rv);
}

/*
 * Create a directory node in a non-global dev instance.
 * Always create shadow vnode. Set sdev_origin to the corresponding
 * global directory sdev_node if it exists. This facilitates the
 * lookup operation.
 */
static int
prof_make_dir(char *name, struct sdev_node **gdirp, struct sdev_node **dirp)
{
	struct sdev_node *dir = *dirp;
	struct sdev_node *gdir = *gdirp;
	struct sdev_node *newdv;
	struct vnode *avp, *gnewdir = NULL;
	struct vattr vattr;
	int error;

	/* see if name already exists */
	rw_enter(&dir->sdev_contents, RW_READER);
	if (newdv = sdev_cache_lookup(dir, name)) {
		*dirp = newdv;
		*gdirp = newdv->sdev_origin;
		SDEV_RELE(dir);
		rw_exit(&dir->sdev_contents);
		return (0);
	}
	rw_exit(&dir->sdev_contents);

	/* find corresponding dir node in global dev */
	if (gdir) {
		error = VOP_LOOKUP(SDEVTOV(gdir), name, &gnewdir,
		    NULL, 0, NULL, kcred, NULL, NULL, NULL);
		if (error == 0) {
			/*
			 * Reference dropped in sdev_prof_free() via
			 * ->sdev_origin.
			 */
			*gdirp = VTOSDEV(gnewdir);
		} else { 	/* it's ok if there no global dir */
			*gdirp = NULL;
		}
	}

	/* get attribute from shadow, also create shadow dir */
	prof_getattr(dir, name, gnewdir, &vattr, &avp, NULL);

	/* create dev directory vnode */
	rw_enter(&dir->sdev_contents, RW_WRITER);
	error = prof_mknode(dir, name, &newdv, &vattr, avp, (void *)*gdirp,
	    kcred);
	rw_exit(&dir->sdev_contents);
	if (error == 0) {
		ASSERT(newdv);
		*dirp = newdv;
	}
	SDEV_RELE(dir);
	return (error);
}

static int
get_nvstr(const char *str, char **name, char **value)
{
	char *p;
	size_t namelen;

	if ((p = strchr(str, '=')) == NULL)
		return (EINVAL);

	*value = strdup(p + 1);

	/* LINTED: E_PTRDIFF_OVERFLOW */
	namelen = p - str;

	*name = kmem_alloc(namelen + 1, KM_SLEEP);

	(void) strlcpy(*name, str, namelen + 1);

	if (**name == '\0' || **value == '\0') {
		strfree(*name);
		strfree(*value);
		return (EINVAL);
	}

	return (0);
}

static void
prof_apply_devann(struct sdev_node *dv)
{
	nvlist_t *nvl;
	nvpair_t *nvp = NULL;
	enum vtype vtype;
	char *str;
	char *name;
	char *value;
	int rv;

	vtype = SDEVTOV(dv)->v_type;

	if (vtype != VCHR && vtype != VBLK)
		return;
	if (zone_devann_present(VTOZONE(SDEVTOV(dv)), SDEVTOV(dv)->v_rdev))
		return;

	nvl = dv->sdev_dotdot->sdev_prof.dev_name_ann;

	while (nvp = nvlist_next_nvpair(nvl, nvp)) {
		if (!gmatch(dv->sdev_name, nvpair_name(nvp)))
			continue;

		rv = nvpair_value_string(nvp, &str);
		if (rv != 0) {
			cmn_err(CE_WARN, sdev_nvp_val_err,
			    rv, nvpair_name(nvp));
			break;
		}

		if (get_nvstr(str, &name, &value) != 0)
			continue;

		(void) zone_devann_insert(VTOZONE(SDEVTOV(dv)),
		    SDEVTOV(dv)->v_rdev, name, value);

		strfree(name);
		strfree(value);

		dv->sdev_flags |= SDEV_ANNOTATED;
	}
}

void
sdev_apply_devann(struct sdev_node *dv)
{
	ASSERT(dv->sdev_flags & SDEV_ANNOTATED);

	rw_enter(&dv->sdev_dotdot->sdev_contents, RW_WRITER);
	prof_apply_devann(dv);
	rw_exit(&dv->sdev_dotdot->sdev_contents);
}

/*
 * Look up a logical name in the global zone.
 * Provides the ability to map the global zone's device name to an alternate
 * name within a zone.  There are a few common use cases:
 *
 *   - The virtual console device /dev/zcons/[zonename]/zconsole is mapped to
 *     /[zonename]/root/dev/zconsole. In such a case, we don't try to
 *     invalidate the node: prof_validate_node() will think the global node has
 *     gone away, so we just punt and assume it's always there.  - Symlinks are
 *     also never invalidated.
 *   - Dataset aliasing causes /dev/vol entries in the aliased non-global zone
 *     path to reference the unalias global zone path.  Because these are
 *     somewhat likely to change, we want prof_validate_node() to validate they
 *     are not stale.
 *
 * See SDEV_NAME_REMAPPED handling in prof_validate_node().
 */
static void
prof_lookup_globaldev(struct sdev_node *dir, struct sdev_node *gdir,
    char *name, char *rename)
{
	int error;
	struct vnode *avp, *gdv, *gddv;
	struct sdev_node *newdv;
	struct vattr vattr = {0};
	struct pathname pn;

	/* check if node already exists */
	newdv = sdev_cache_lookup(dir, rename);
	if (newdv) {
		SDEV_SIMPLE_RELE(newdv);
		return;
	}

	/* sanity check arguments */
	if (!gdir || pn_get(name, UIO_SYSSPACE, &pn))
		return;

	/* perform a relative lookup of the global /dev instance */
	gddv = SDEVTOV(gdir);
	VN_HOLD(gddv);
	error = lookuppnvp(&pn, NULL, FOLLOW, NULLVPP, &gdv,
	    rootdir, gddv, kcred);
	pn_free(&pn);
	if (error) {
		sdcmn_err10(("prof_lookup_globaldev: %s not found\n", name));
		return;
	}

	ASSERT(gdv != NULL && gdv->v_vfsp != NULL);
	if (gdv->v_type == VLNK) {
		sdcmn_err10(("prof_lookup_globaldev: %s is a symlink\n", name));
		VN_RELE(gdv);
		return;
	}

	/*
	 * Found the entry in global /dev, figure out attributes
	 * by looking at backing store. Call into devfs for default.
	 * Note, mapped device is persisted under the new name.
	 *
	 * prof_getattr() can deal with other fs types (eg fdfs), so
	 * the vfs_fstype check is done later.
	 */
	prof_getattr(dir, rename, gdv, &vattr, &avp, NULL);

	if (gdv->v_type != VDIR || gdv->v_vfsp->vfs_fstype != devtype) {
		VN_RELE(gdv);
		gdir = NULL;
	} else {
		/*
		 * Reference is dropped in sdev_prof_free().
		 */
		gdir = VTOSDEV(gdv);
	}

	if (prof_mknode(dir, rename, &newdv, &vattr, avp,
	    (void *)gdir, kcred) == 0) {
		prof_apply_devann(newdv);
		/* Be sure /dev/zvol/... doesn't get SDEV_NAME_REMAPPED */
		if (name != rename && strncmp(dir->sdev_path, ZVOL_DIR "/",
		    sizeof (ZVOL_DIR)) != 0) {
			newdv->sdev_flags |= SDEV_NAME_REMAPPED;
		}
		ASSERT(newdv->sdev_state != SDEV_ZOMBIE);
		SDEV_SIMPLE_RELE(newdv);
	}
}

static void
prof_make_sym(struct sdev_node *dir, char *lnm, char *tgt)
{
	struct sdev_node *newdv;

	if (prof_mknode(dir, lnm, &newdv, &sdev_vattr_lnk, NULL,
	    (void *)tgt, kcred) == 0) {
		newdv->sdev_flags |= SDEV_NAME_REMAPPED;
		ASSERT(newdv->sdev_state != SDEV_ZOMBIE);
		SDEV_SIMPLE_RELE(newdv);
	}
}

/*
 * Create symlinks in the current directory based on profile
 */
static void
prof_make_symlinks(struct sdev_node *dir)
{
	char *tgt, *lnm;
	nvpair_t *nvp = NULL;
	nvlist_t *nvl = dir->sdev_prof.dev_symlink;
	int rv;

	ASSERT(RW_WRITE_HELD(&dir->sdev_contents));

	if (nvl == NULL)
		return;

	while (nvp = nvlist_next_nvpair(nvl, nvp)) {
		lnm = nvpair_name(nvp);
		rv = nvpair_value_string(nvp, &tgt);
		if (rv != 0) {
			cmn_err(CE_WARN, sdev_nvp_val_err,
			    rv, nvpair_name(nvp));
			break;
		}
		prof_make_sym(dir, lnm, tgt);
	}
}

static void
prof_make_maps(struct sdev_node *dir)
{
	nvpair_t *nvp = NULL;
	nvlist_t *nvl = dir->sdev_prof.dev_map;
	int rv;

	ASSERT(RW_WRITE_HELD(&dir->sdev_contents));

	if (nvl == NULL)
		return;

	while (nvp = nvlist_next_nvpair(nvl, nvp)) {
		char *name;
		char *rename = nvpair_name(nvp);
		rv = nvpair_value_string(nvp, &name);
		if (rv != 0) {
			cmn_err(CE_WARN, sdev_nvp_val_err,
			    rv, nvpair_name(nvp));
			break;
		}
		sdcmn_err10(("map %s -> %s\n", name, rename));
		(void) prof_lookup_globaldev(dir, sdev_origins->sdev_root,
		    name, rename);
	}
}

struct match_arg {
	char *expr;
	int match;
};

static int
match_name(char *name, void *arg)
{
	struct match_arg *margp = (struct match_arg *)arg;

	if (gmatch(name, margp->expr)) {
		margp->match = 1;
		return (WALK_DIR_TERMINATE);
	}
	return (WALK_DIR_CONTINUE);
}

static int
is_nonempty_dir(char *name, char *pathleft, struct sdev_node *dir)
{
	struct match_arg marg;
	struct pathname pn;
	struct vnode *gvp;
	struct sdev_node *gdir = dir->sdev_origin;

	if (gdir == NULL)
		return (0);

	if (VOP_LOOKUP(SDEVTOV(gdir), name, &gvp, NULL, 0, NULL, kcred,
	    NULL, NULL, NULL) != 0)
		return (0);

	if (gvp->v_type != VDIR) {
		VN_RELE(gvp);
		return (0);
	}

	if (pn_get(pathleft, UIO_SYSSPACE, &pn) != 0) {
		VN_RELE(gvp);
		return (0);
	}

	marg.expr = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	(void) pn_getcomponent(&pn, marg.expr);
	marg.match = 0;

	walk_dir(gvp, &marg, match_name);
	VN_RELE(gvp);
	kmem_free(marg.expr, MAXNAMELEN);
	pn_free(&pn);

	return (marg.match);
}


/* Check if name passes matching rules */
static int
prof_name_matched(char *name, struct sdev_node *dir)
{
	int type, match = 0;
	char *expr;
	nvlist_t *nvl;
	nvpair_t *nvp = NULL;
	int rv;

	/* check against nvlist for leaf include/exclude */
	nvl = dir->sdev_prof.dev_name;
	while (nvp = nvlist_next_nvpair(nvl, nvp)) {
		expr = nvpair_name(nvp);
		rv = nvpair_value_int32(nvp, &type);
		if (rv != 0) {
			cmn_err(CE_WARN, sdev_nvp_val_err,
			    rv, nvpair_name(nvp));
			break;
		}

		if (type == PROFILE_TYPE_EXCLUDE) {
			if (gmatch(name, expr))
				return (0);	/* excluded */
		} else if (!match) {
			match = gmatch(name, expr);
		}
	}
	if (match) {
		sdcmn_err10(("prof_name_matched: %s\n", name));
		return (match);
	}

	/* check for match against directory globbing pattern */
	nvl = dir->sdev_prof.dev_glob_incdir;
	while (nvp = nvlist_next_nvpair(nvl, nvp)) {
		char *pathleft;
		expr = nvpair_name(nvp);
		if (gmatch(name, expr) == 0)
			continue;
		rv = nvpair_value_string(nvp, &pathleft);
		if (rv != 0) {
			cmn_err(CE_WARN, sdev_nvp_val_err,
			    rv, nvpair_name(nvp));
			break;
		}
		if (is_nonempty_dir(name, pathleft, dir)) {
			sdcmn_err10(("prof_name_matched: dir %s\n", name));
			return (1);
		}
	}

	return (0);
}

static void
walk_dir(struct vnode *dvp, void *arg, int (*callback)(char *, void *))
{
	char    *nm;
	int eof, error;
	struct iovec iov;
	struct uio uio;
	struct dirent64 *dp;
	dirent64_t *dbuf;
	size_t dbuflen, dlen;

	ASSERT(dvp);

	dlen = 4096;
	dbuf = kmem_zalloc(dlen, KM_SLEEP);

	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_fmode = 0;
	uio.uio_extflg = UIO_COPY_CACHED;
	uio.uio_loffset = 0;
	uio.uio_llimit = MAXOFFSET_T;

	eof = 0;
	error = 0;
	while (!error && !eof) {
		uio.uio_resid = dlen;
		iov.iov_base = (char *)dbuf;
		iov.iov_len = dlen;
		(void) VOP_RWLOCK(dvp, V_WRITELOCK_FALSE, NULL);
		error = VOP_READDIR(dvp, &uio, kcred, &eof, NULL, 0);
		VOP_RWUNLOCK(dvp, V_WRITELOCK_FALSE, NULL);

		dbuflen = dlen - uio.uio_resid;
		if (error || dbuflen == 0)
			break;
		for (dp = dbuf; ((intptr_t)dp <
		    (intptr_t)dbuf + dbuflen);
		    dp = (dirent64_t *)((intptr_t)dp + dp->d_reclen)) {
			nm = dp->d_name;

			if (strcmp(nm, ".") == 0 ||
			    strcmp(nm, "..") == 0)
				continue;

			if (callback(nm, arg) == WALK_DIR_TERMINATE)
				goto end;
		}
	}

end:
	kmem_free(dbuf, dlen);
}

/*
 * Last chance for a zone to see a node.  If our parent dir is
 * SDEV_ZONED, then we look up the "zone" property for the node.  If the
 * property is found and matches the mount's zone name, we allow it.
 */
static int
prof_zone_matched(char *name, struct sdev_node *dir)
{
	vnode_t *gvn;
	struct pathname pn;
	vnode_t *vn = NULL;
	char zonename[ZONENAME_MAX];
	int znlen = ZONENAME_MAX;
	zone_t *zone;
	int ret;

	ASSERT((dir->sdev_flags & SDEV_ZONED) != 0);

	sdcmn_err10(("sdev_node %p is zoned, looking for %s\n",
	    (void *)dir, name));

	if (dir->sdev_origin == NULL)
		return (0);

	if (pn_get(name, UIO_SYSSPACE, &pn))
		return (0);

	gvn = SDEVTOV(dir->sdev_origin);
	VN_HOLD(gvn);

	ret = lookuppnvp(&pn, NULL, FOLLOW, NULLVPP, &vn, rootdir, gvn, kcred);

	pn_free(&pn);

	if (ret != 0) {
		sdcmn_err10(("prof_zone_matched: %s not found\n", name));
		return (0);
	}

	if (vn->v_type != VBLK && vn->v_type != VCHR) {
		sdcmn_err10(("prof_zone_matched: %s is not a device\n", name));
		VN_RELE(vn);
		return (0);
	}

	/*
	 * VBLK doesn't matter, and the property name is in fact treated
	 * as a const char *.
	 */
	ret = e_ddi_getlongprop_buf(vn->v_rdev, VBLK, (char *)"zone",
	    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS, (caddr_t)zonename, &znlen);

	VN_RELE(vn);

	if (ret == DDI_PROP_NOT_FOUND) {
		sdcmn_err10(("vnode %p: no zone prop\n", (void *)vn));
		return (0);
	} else if (ret != DDI_PROP_SUCCESS) {
		sdcmn_err10(("vnode %p: zone prop error: %d\n",
		    (void *)vn, ret));
		return (0);
	}

	sdcmn_err10(("vnode %p zone prop: %s\n", (void *)vn, zonename));

	zone = zone_find_by_name(zonename);
	if (zone == NULL)
		return (0);

	if (VTOZONE(SDEVTOV(dir))->zone_id == zone->zone_id) {
		zone_rele(zone);
		return (1);
	}

	zone_rele(zone);
	return (0);
}

static int
prof_make_name_glob(char *nm, void *arg)
{
	struct sdev_node *ddv = (struct sdev_node *)arg;

	if (prof_name_matched(nm, ddv))
		prof_lookup_globaldev(ddv, ddv->sdev_origin, nm, nm);

	return (WALK_DIR_CONTINUE);
}

static int
prof_make_name_zone(char *nm, void *arg)
{
	struct sdev_node *ddv = (struct sdev_node *)arg;

	if (prof_zone_matched(nm, ddv))
		prof_lookup_globaldev(ddv, ddv->sdev_origin, nm, nm);

	return (WALK_DIR_CONTINUE);
}

static void
prof_make_names_walk(struct sdev_node *ddv, int (*cb)(char *, void *))
{
	struct sdev_node *gdir;

	gdir = ddv->sdev_origin;
	if (gdir == NULL)
		return;
	walk_dir(SDEVTOV(gdir), (void *)ddv, cb);
}

static void
prof_make_names(struct sdev_node *dir)
{
	char *name;
	nvpair_t *nvp = NULL;
	nvlist_t *nvl = dir->sdev_prof.dev_name;
	int rv;

	ASSERT(RW_WRITE_HELD(&dir->sdev_contents));

	if ((dir->sdev_flags & SDEV_ZONED) != 0)
		prof_make_names_walk(dir, prof_make_name_zone);

	if (nvl == NULL)
		return;

	if (dir->sdev_prof.has_glob) {
		prof_make_names_walk(dir, prof_make_name_glob);
		return;
	}

	/* Walk nvlist and lookup corresponding device in global inst */
	while (nvp = nvlist_next_nvpair(nvl, nvp)) {
		int type;
		rv = nvpair_value_int32(nvp, &type);
		if (rv != 0) {
			cmn_err(CE_WARN, sdev_nvp_val_err,
			    rv, nvpair_name(nvp));
			break;
		}
		if (type == PROFILE_TYPE_EXCLUDE)
			continue;
		name = nvpair_name(nvp);
		(void) prof_lookup_globaldev(dir, dir->sdev_origin,
		    name, name);
	}
}

static int
prof_dir_needs_rebuilding(struct sdev_node *dv)
{
	struct sdev_node *gdir = dv->sdev_origin;

	if ((dv->sdev_flags & SDEV_BUILD))
		return (1);

	if (dv->sdev_devtree_gen != devtree_gen)
		return (1);

	if (gdir == NULL)
		return (0);

	return (gdir->sdev_gdir_gen != dv->sdev_ldir_gen);
}

/*
 * Validate the node.  Returns as follows:
 *
 * SDEV_VTOR_VALID
 *
 *	Node doesn't exist, is sticky, or matches the global /dev.
 *
 * SDEV_VTOR_INVALID
 *
 *	Validator returned _INVALID, or the node doesn't exist at all in
 *	the global /dev.
 *
 * SDEV_VTOR_STALE
 *
 *	Validator returned _STALE, or the device of the node in the
 *	global /dev doesn't match.  If quick is true, this may just mean
 *	that the dir needs rebuilding.
 */
static int
prof_validate_node(struct sdev_node *ddv, struct sdev_node *dv, int quick)
{
	vnode_t *gvn;
	struct pathname pn;
	vnode_t *vn = NULL;
	dev_t rdev;
	int ret;

	ASSERT(RW_LOCK_HELD(&ddv->sdev_contents));

	if (dv == NULL || (dv->sdev_flags & SDEV_NAME_REMAPPED))
		return (SDEV_VTOR_VALID);

	if ((ddv->sdev_flags & SDEV_VTOR)) {
		int (*vtor)(struct sdev_node *);
		vtor = (int (*)(struct sdev_node *))sdev_get_vtor(ddv);
		ASSERT(vtor);
		switch (vtor(dv)) {
		case SDEV_VTOR_VALID:
			return (SDEV_VTOR_VALID);
		case SDEV_VTOR_STALE:
			return (SDEV_VTOR_STALE);
		default:
			return (SDEV_VTOR_INVALID);
		}
	}

	if (quick) {
		if (prof_dir_needs_rebuilding(ddv)) {
			sdcmn_err10(("needs rebuild: %s (%p)",
			    ddv->sdev_name, (void *)ddv));
			return (SDEV_VTOR_STALE);
		}
		return (SDEV_VTOR_VALID);
	}

	if (ddv->sdev_origin == NULL)
		return (SDEV_VTOR_VALID);
	gvn = SDEVTOV(ddv->sdev_origin);

	if (pn_get(dv->sdev_name, UIO_SYSSPACE, &pn))
		return (SDEV_VTOR_INVALID);

	VN_HOLD(gvn);

	ret = lookuppnvp(&pn, NULL, FOLLOW, NULLVPP, &vn, rootdir, gvn, kcred);

	pn_free(&pn);

	if (ret != 0)
		return (SDEV_VTOR_INVALID);

	rdev = vn->v_rdev;

	VN_RELE(vn);

	if ((SDEVTOV(dv)->v_type == VBLK ||
	    SDEVTOV(dv)->v_type == VCHR) &&
	    rdev != SDEVTOV(dv)->v_rdev) {
		sdcmn_err10(("%s invalid: stale v_rdev\n", dv->sdev_name));
		return (SDEV_VTOR_INVALID);
	}

	return (SDEV_VTOR_VALID);
}

static struct sdev_node *
prof_clean_node(struct sdev_node *ddv, struct sdev_node *dv)
{
	struct sdev_node *sdev_next = SDEV_NEXT_ENTRY(ddv, dv);
	int err;

	ASSERT(RW_WRITE_HELD(&ddv->sdev_contents));

	sdcmn_err10(("prof_clean_node: %s, dir %s\n",
	    dv->sdev_name, ddv->sdev_name));

	if ((SDEVTOV(dv)->v_type == VDIR) &&
	    (sdev_cleandir(dv, NULL, 0) != 0)) {
		sdcmn_err10(("sdev_cleandir(%s) failed\n",
		    dv->sdev_name));
		return (sdev_next);
	}

	SDEV_HOLD(dv);

	sdcmn_err10(("prof_clean_node: held %s: %d\n",
	    dv->sdev_name, SDEVTOV(dv)->v_count));

	err = sdev_cache_update(ddv, &dv, dv->sdev_name,
	    SDEV_CACHE_DELETE);

	if (err == 0) {
		sdev_next = SDEV_FIRST_ENTRY(ddv);
	} else {
		ASSERT(err == EBUSY);
		sdcmn_err10(("sdev_cache_delete(%s) failed: v_count %d\n",
		    dv->sdev_name, SDEVTOV(dv)->v_count));
	}

	return (sdev_next);
}

static void
prof_prunedir(struct sdev_node *ddv)
{
	struct sdev_node *dv;

	ASSERT(RW_WRITE_HELD(&ddv->sdev_contents));

	sdcmn_err10(("prof_prunedir: %s\n", ddv->sdev_name));

	for (dv = SDEV_FIRST_ENTRY(ddv); dv != NULL; ) {
		if ((dv->sdev_flags & SDEV_STALE)) {
			dv = SDEV_NEXT_ENTRY(ddv, dv);
			continue;
		}

		switch (prof_validate_node(ddv, dv, 0)) {
		case SDEV_VTOR_VALID:
			dv = SDEV_NEXT_ENTRY(ddv, dv);
			break;
		default:
			sdcmn_err10(("prof_prunedir: %s invalid\n",
			    dv->sdev_name));
			dv = prof_clean_node(ddv, dv);
			break;
		}
	}
}

/*
 * Build directory vnodes based on the profile and the global
 * dev instance.
 */
void
prof_filldir(struct sdev_node *ddv)
{
	int firsttime = 1;
	struct sdev_node *gdir = ddv->sdev_origin;

	ASSERT(RW_READ_HELD(&ddv->sdev_contents));

check_build:
	if (!prof_dir_needs_rebuilding(ddv))
		return;

	if (firsttime && rw_tryupgrade(&ddv->sdev_contents) == 0) {
		rw_exit(&ddv->sdev_contents);
		firsttime = 0;
		rw_enter(&ddv->sdev_contents, RW_WRITER);
		goto check_build;
	}

	prof_prunedir(ddv);

	sdcmn_err10(("devtree_gen (%s): %ld -> %ld\n",
	    ddv->sdev_path, ddv->sdev_devtree_gen, devtree_gen));
	if (gdir)
		sdcmn_err10(("sdev_dir_gen (%s): %ld -> %ld\n",
		    ddv->sdev_path, ddv->sdev_ldir_gen,
		    gdir->sdev_gdir_gen));

	/* update flags and generation number so next filldir is quick */
	ddv->sdev_flags &= ~SDEV_BUILD;
	ddv->sdev_devtree_gen = devtree_gen;
	if (gdir)
		ddv->sdev_ldir_gen = gdir->sdev_gdir_gen;

	prof_make_symlinks(ddv);
	prof_make_maps(ddv);
	prof_make_names(ddv);
	rw_downgrade(&ddv->sdev_contents);
}

/*
 * Allow a node to appear even if it doesn't match anything in the
 * profile.
 */
static void
prof_force_node(struct sdev_node *ddv, char *name)
{
	char *dsname = NULL;
	char *gzname = name;		/* GZ equivalent of name */
	char dsk[] = ZVOL_DIR "/dsk";
	char rdsk[] = ZVOL_DIR "/rdsk";
	struct sdev_node *origin = ddv->sdev_origin;

	ASSERT(RW_READ_HELD(&ddv->sdev_contents));

	if (origin == NULL)
		return;

	if (strncmp(ddv->sdev_path, dsk, strlen(dsk)) == 0) {
		/* Walk up the origin tree to find /dev/zvol/dsk */
		while (strcmp(origin->sdev_path, dsk) != 0) {
			origin = origin->sdev_dotdot;
			ASSERT(strlen(origin->sdev_path) >= strlen(dsk));
		}
	} else if (strncmp(ddv->sdev_path, rdsk, strlen(rdsk)) == 0) {
		/* Walk up the origin tree to find /dev/zvol/rdsk */
		while (strcmp(origin->sdev_path, rdsk) != 0) {
			origin = origin->sdev_dotdot;
			ASSERT(strlen(origin->sdev_path) >= strlen(rdsk));
		}
	} else {
		goto force_node;
	}

	if ((dsname = devzvol_make_dsname(ddv->sdev_path, name,
	    VTOZONE(SDEVTOV(ddv)))) == NULL)
		goto force_node;

	gzname = dsname;

force_node:
	if (rw_tryupgrade(&ddv->sdev_contents) == 0) {
		rw_exit(&ddv->sdev_contents);
		rw_enter(&ddv->sdev_contents, RW_WRITER);
	}

	(void) prof_lookup_globaldev(ddv, origin, gzname, name);

	rw_downgrade(&ddv->sdev_contents);
	if (dsname != NULL)
		strfree(dsname);
}

/* apply include/exclude pattern to existing directory content */
static void
apply_dir_pattern(struct sdev_node *dir, char *expr, char *pathleft,
    char *ann, int type)
{
	struct sdev_node *dv;

	/* leaf pattern */
	if (pathleft == NULL) {
		if (type == PROFILE_TYPE_INCLUDE ||
		    type == PROFILE_TYPE_ANNOTATE)
			return;
		(void) sdev_cleandir(dir, expr, SDEV_ENFORCE);
		return;
	}

	/* directory pattern */
	rw_enter(&dir->sdev_contents, RW_WRITER);

	for (dv = SDEV_FIRST_ENTRY(dir); dv; dv = SDEV_NEXT_ENTRY(dir, dv)) {
		if (gmatch(dv->sdev_name, expr) == 0 ||
		    SDEVTOV(dv)->v_type != VDIR)
			continue;
		process_rule(dv, dv->sdev_origin,
		    pathleft, NULL, ann, type);
	}
	rw_exit(&dir->sdev_contents);
}

/*
 * Add a profile rule.
 * tgt represents a device name matching expression,
 * matching device names are to be either included or excluded.
 */
static void
prof_add_rule(char *name, char *tgt, struct sdev_node *dir,
    int type, char *ann)
{
	int error;
	nvlist_t **nvlp = NULL;
	int rv;

	ASSERT(SDEVTOV(dir)->v_type == VDIR);

	rw_enter(&dir->sdev_contents, RW_WRITER);

	switch (type) {
	case PROFILE_TYPE_INCLUDE:
		if (tgt)
			nvlp = &(dir->sdev_prof.dev_glob_incdir);
		else
			nvlp = &(dir->sdev_prof.dev_name);
		break;
	case PROFILE_TYPE_EXCLUDE:
		if (tgt)
			nvlp = &(dir->sdev_prof.dev_glob_excdir);
		else
			nvlp = &(dir->sdev_prof.dev_name);
		break;
	case PROFILE_TYPE_ANNOTATE:
		if (tgt)
			nvlp = &(dir->sdev_prof.dev_glob_ann);
		else
			nvlp = &(dir->sdev_prof.dev_name_ann);
		break;
	case PROFILE_TYPE_MAP:
		nvlp = &(dir->sdev_prof.dev_map);
		break;
	case PROFILE_TYPE_SYMLINK:
		nvlp = &(dir->sdev_prof.dev_symlink);
		break;
	};

	/* initialize nvlist */
	if (*nvlp == NULL) {
		error = nvlist_alloc(nvlp, 0, KM_SLEEP);
		ASSERT(error == 0);
	}

	if (type == PROFILE_TYPE_ANNOTATE) {
		if (tgt) {
			char *array[2] = { tgt, ann };
			rv = nvlist_add_string_array(*nvlp, name, array, 2);
		} else {
			rv = nvlist_add_string(*nvlp, name, ann);
		}
	} else {
		if (tgt) {
			rv = nvlist_add_string(*nvlp, name, tgt);
		} else {
			rv = nvlist_add_int32(*nvlp, name, type);
		}
	}

	ASSERT(rv == 0);
	/* rebuild directory content */
	dir->sdev_flags |= SDEV_BUILD;

	if ((type == PROFILE_TYPE_INCLUDE) &&
	    (strpbrk(name, "*?[]") != NULL)) {
		dir->sdev_prof.has_glob = 1;
	}

	rw_exit(&dir->sdev_contents);

	/* additional details for glob pattern and exclusion */
	switch (type) {
	case PROFILE_TYPE_INCLUDE:
	case PROFILE_TYPE_EXCLUDE:
	case PROFILE_TYPE_ANNOTATE:
		apply_dir_pattern(dir, name, tgt, ann, type);
		break;
	};
}

/*
 * Parse path components and apply requested matching rule at
 * directory level.
 */
static void
process_rule(struct sdev_node *dir, struct sdev_node *gdir,
    char *path, char *tgt, char *ann, int type)
{
	char *name;
	struct pathname	pn;
	int rv = 0;

	if ((strlen(path) > 5) && (strncmp(path, "/dev/", 5) == 0)) {
		path += 5;
	}

	if (pn_get(path, UIO_SYSSPACE, &pn) != 0)
		return;

	name = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	(void) pn_getcomponent(&pn, name);
	pn_skipslash(&pn);
	SDEV_HOLD(dir);

	while (pn_pathleft(&pn)) {
		/* If this is pattern, just add the pattern */
		if (strpbrk(name, "*?[]") != NULL &&
		    (type == PROFILE_TYPE_INCLUDE ||
		    type == PROFILE_TYPE_EXCLUDE ||
		    type == PROFILE_TYPE_ANNOTATE)) {
			ASSERT(tgt == NULL);
			tgt = pn.pn_path;
			break;
		}

		if ((rv = prof_make_dir(name, &gdir, &dir)) != 0) {
			cmn_err(CE_CONT, "process_rule: %s error %d\n",
			    path, rv);
			break;
		}
		(void) pn_getcomponent(&pn, name);
		pn_skipslash(&pn);
	}

	/* process the leaf component */
	if (rv == 0) {
		prof_add_rule(name, tgt, dir, type, ann);
		SDEV_SIMPLE_RELE(dir);
	}

	kmem_free(name, MAXPATHLEN);
	pn_free(&pn);
}

static int
copyin_nvlist(char *packed_usr, size_t packed_sz, nvlist_t **nvlp)
{
	int err = 0;
	char *packed;
	nvlist_t *profile = NULL;

	/* simple sanity check */
	if (packed_usr == NULL || packed_sz == 0)
		return (NULL);

	/* copyin packed profile nvlist */
	packed = kmem_alloc(packed_sz, KM_NOSLEEP);
	if (packed == NULL)
		return (ENOMEM);
	err = copyin(packed_usr, packed, packed_sz);

	/* unpack packed profile nvlist */
	if (err)
		cmn_err(CE_WARN, "copyin_nvlist: copyin failed with "
		    "err %d\n", err);
	else if (err = nvlist_unpack(packed, packed_sz, &profile, KM_NOSLEEP))
		cmn_err(CE_WARN, "copyin_nvlist: nvlist_unpack "
		    "failed with err %d\n", err);

	kmem_free(packed, packed_sz);
	if (err == 0)
		*nvlp = profile;
	return (err);
}

/*
 * Process profile passed down from libdevinfo. There are four types
 * of matching rules:
 *  include: export a name or names matching a pattern
 *  exclude: exclude a name or names matching a pattern
 *  symlink: create a local symlink
 *  map:     export a device with a name different from the global zone
 * Note: We may consider supporting VOP_SYMLINK in non-global instances,
 *	because it does not present any security risk. For now, the fs
 *	instance is read only.
 */
static void
sdev_process_profile(struct sdev_data *sdev_data, nvlist_t *profile)
{
	nvpair_t *nvpair;
	char *nvname, *dname;
	struct sdev_node *dir, *gdir;
	char **pair;	/* for symlinks, maps, and annotations */
	uint_t nelem;
	int rv;

	gdir = sdev_origins->sdev_root;	/* root of global /dev */
	dir = sdev_data->sdev_root;	/* root of current instance */

	ASSERT(profile);

	/* process nvpairs in the list */
	nvpair = NULL;
	while (nvpair = nvlist_next_nvpair(profile, nvpair)) {
		nvname = nvpair_name(nvpair);
		ASSERT(nvname != NULL);

		if (strcmp(nvname, SDEV_NVNAME_INCLUDE) == 0) {
			rv = nvpair_value_string(nvpair, &dname);
			if (rv != 0) {
				cmn_err(CE_WARN, sdev_nvp_val_err,
				    rv, nvpair_name(nvpair));
				break;
			}
			process_rule(dir, gdir, dname, NULL, NULL,
			    PROFILE_TYPE_INCLUDE);
		} else if (strcmp(nvname, SDEV_NVNAME_EXCLUDE) == 0) {
			rv = nvpair_value_string(nvpair, &dname);
			if (rv != 0) {
				cmn_err(CE_WARN, sdev_nvp_val_err,
				    rv, nvpair_name(nvpair));
				break;
			}
			process_rule(dir, gdir, dname, NULL, NULL,
			    PROFILE_TYPE_EXCLUDE);
		} else if (strcmp(nvname, SDEV_NVNAME_SYMLINK) == 0) {
			rv = nvpair_value_string_array(nvpair, &pair, &nelem);
			if (rv != 0) {
				cmn_err(CE_WARN, sdev_nvp_val_err,
				    rv, nvpair_name(nvpair));
				break;
			}
			ASSERT(nelem == 2);
			process_rule(dir, gdir, pair[0], pair[1], NULL,
			    PROFILE_TYPE_SYMLINK);
		} else if (strcmp(nvname, SDEV_NVNAME_MAP) == 0) {
			rv = nvpair_value_string_array(nvpair, &pair, &nelem);
			if (rv != 0) {
				cmn_err(CE_WARN, sdev_nvp_val_err,
				    rv, nvpair_name(nvpair));
				break;
			}
			process_rule(dir, gdir, pair[1], pair[0], NULL,
			    PROFILE_TYPE_MAP);
		} else if (strcmp(nvname, SDEV_NVNAME_ANNOTATE) == 0) {
			rv = nvpair_value_string_array(nvpair, &pair, &nelem);
			if (rv != 0) {
				cmn_err(CE_WARN, sdev_nvp_val_err,
				    rv, nvpair_name(nvpair));
				break;
			}
			process_rule(dir, gdir, pair[0], NULL, pair[1],
			    PROFILE_TYPE_ANNOTATE);
		} else if (strcmp(nvname, SDEV_NVNAME_MOUNTPT) != 0) {
			cmn_err(CE_WARN, "sdev_process_profile: invalid "
			    "nvpair %s\n", nvname);
		}
	}
}

/*ARGSUSED*/
int
prof_lookup(vnode_t *dvp, char *nm, vnode_t **vpp, struct cred *cred, int force)
{
	struct sdev_node *ddv = VTOSDEV(dvp);
	struct sdev_node *dv;
	int nmlen;

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

	rw_enter(&ddv->sdev_contents, RW_READER);
	dv = sdev_cache_lookup(ddv, nm);

	switch (prof_validate_node(ddv, dv, 1)) {
	case SDEV_VTOR_VALID:
		break;
	case SDEV_VTOR_INVALID:
		sdcmn_err10(("prof_lookup: %s invalid\n", nm));
		SDEV_SIMPLE_RELE(dv);
		dv = NULL;
		rw_exit(&ddv->sdev_contents);
		return (ENOENT);
	case SDEV_VTOR_STALE:
		sdcmn_err10(("prof_lookup: %s stale\n", nm));
		SDEV_SIMPLE_RELE(dv);
		dv = NULL;
		break;
	}

	if (dv == NULL) {
		prof_filldir(ddv);
		if (force)
			prof_force_node(ddv, nm);
		dv = sdev_cache_lookup(ddv, nm);
	}

	rw_exit(&ddv->sdev_contents);

	if (dv == NULL) {
		sdcmn_err10(("prof_lookup: %s not found\n", nm));
		return (ENOENT);
	}

	sdcmn_err10(("prof_lookup: %s -> %p\n", nm, (void *)dv));

	if ((dv->sdev_state == SDEV_ZOMBIE)) {
		sdcmn_err10(("prof_lookup: %s zombified\n", nm));
		SDEV_RELE(dv);
		return (ENOENT);
	}

	return (sdev_to_vp(dv, vpp));
}

/*
 * This is invoked after a new filesystem is mounted to define the
 * name space. It is also invoked during normal system operation
 * to update the name space.
 *
 * Applications call di_prof_commit() in libdevinfo, which invokes
 * modctl(). modctl calls this function. The input is a packed nvlist.
 */
int
devname_profile_update(char *packed, size_t packed_sz)
{
	char *mntpt;
	nvlist_t *nvl;
	nvpair_t *nvp;
	struct sdev_data *mntinfo;
	int err;
	int rv;

	nvl = NULL;
	if ((err = copyin_nvlist(packed, packed_sz, &nvl)) != 0)
		return (err);
	ASSERT(nvl);

	/* The first nvpair must be the mount point */
	nvp = nvlist_next_nvpair(nvl, NULL);
	if (strcmp(nvpair_name(nvp), SDEV_NVNAME_MOUNTPT) != 0) {
		cmn_err(CE_NOTE,
		    "devname_profile_update: mount point not specified");
		nvlist_free(nvl);
		return (EINVAL);
	}

	/* find the matching filesystem instance */
	rv = nvpair_value_string(nvp, &mntpt);
	if (rv != 0) {
		cmn_err(CE_WARN, sdev_nvp_val_err,
		    rv, nvpair_name(nvp));
	} else {
		mntinfo = sdev_find_mntinfo(mntpt);
		if (mntinfo == NULL) {
			cmn_err(CE_NOTE, "devname_profile_update: "
			    " mount point %s not found", mntpt);
			nvlist_free(nvl);
			return (EINVAL);
		}

		/* now do the hardwork to process the profile */
		sdev_process_profile(mntinfo, nvl);

		sdev_mntinfo_rele(mntinfo);
	}

	nvlist_free(nvl);
	return (0);
}

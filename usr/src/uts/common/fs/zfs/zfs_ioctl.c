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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/modctl.h>
#include <sys/open.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/priv_impl.h>
#include <sys/dmu.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_deleg.h>
#include <sys/dmu_objset.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/policy.h>
#include <sys/zone.h>
#include <sys/nvpair.h>
#include <sys/pathname.h>
#include <sys/mount.h>
#include <sys/sdt.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_onexit.h>
#include <sys/zvol.h>
#include <sys/dsl_scan.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_crypto.h>
#include <sharefs/share.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "zfs_deleg.h"
#include "zfs_comutil.h"

extern struct modlfs zfs_modlfs;

extern void zfs_init(void);
extern void zfs_fini(void);

extern uint32_t vdev_active_threads;

ldi_ident_t zfs_li = NULL;
dev_info_t *zfs_dip;

typedef int zfs_ioc_func_t(zfs_cmd_t *);
typedef int zfs_secpolicy_func_t(zfs_cmd_t *, cred_t *);

typedef enum {
	NO_NAME,
	POOL_NAME,
	DATASET_NAME
} zfs_ioc_namecheck_t;

typedef enum {
	/* Fast path to do nothing */
	DATASET_ALIAS_NONE	= 1 << 0,
	/* First four fields of zfs_cmd_t */
	DATASET_ALIAS_NAME	= 1 << 1,
	DATASET_ALIAS_VALUE	= 1 << 2,
	DATASET_ALIAS_STRING 	= 1 << 3,
	DATASET_ALIAS_TOP_DS 	= 1 << 4,
	/*
	 * zc_objset_stats.dds_origin only needs to be aliased before
	 * returning from zfsdev_ioctl().  Any value passed into the kernel
	 * is ignored.
	 */
	DATASET_ALIAS_ORIGIN	= 1 << 5,
	/*
	 * zc_crypto.zic_inherit_dsname - Note special handling in
	 * zfs_unalias() if it contains "$globalzone".
	 */
	DATASET_ALIAS_CRYPTO	= 1 << 6
} zfs_zc_alias_t;

typedef struct zfs_ioc_vec {
	zfs_ioc_func_t		*zvec_func;
	zfs_secpolicy_func_t	*zvec_secpolicy;
	zfs_ioc_namecheck_t	zvec_namecheck;
	boolean_t		zvec_his_log;
	zfs_ioc_poolcheck_t	zvec_pool_check;
	zfs_zc_alias_t		zvec_zc_alias;
} zfs_ioc_vec_t;

/* This array is indexed by zfs_userquota_prop_t */
static const char *userquota_perms[] = {
	ZFS_DELEG_PERM_USERUSED,
	ZFS_DELEG_PERM_USERQUOTA,
	ZFS_DELEG_PERM_GROUPUSED,
	ZFS_DELEG_PERM_GROUPQUOTA,
};

static int zfs_ioc_userspace_upgrade(zfs_cmd_t *zc);
static int zfs_check_settable(const char *name, nvpair_t *property,
    cred_t *cr);
static int zfs_check_clearable(char *dataset, nvlist_t *props,
    nvlist_t **errors);
static int zfs_fill_zplprops_root(uint64_t, nvlist_t *, nvlist_t *);
static int get_nvlist(uint64_t nvl, uint64_t size, int iflag, nvlist_t **nvp);
int zfs_set_prop_nvlist(const char *, zprop_source_t, nvlist_t *, nvlist_t **,
    const zprop_setflags_t);
static int zfs_get_crypto_ctx(zfs_cmd_t *zc, dsl_crypto_ctx_t *dcc);

/* _NOTE(PRINTFLIKE(4)) - this is printf-like, but lint is too whiney */
void
__dprintf(const char *file, const char *func, int line, const char *fmt, ...)
{
	const char *newfile;
	char buf[512];
	va_list adx;

	/*
	 * Get rid of annoying "../common/" prefix to filename.
	 */
	newfile = strrchr(file, '/');
	if (newfile != NULL) {
		newfile = newfile + 1; /* Get rid of leading / */
	} else {
		newfile = file;
	}

	va_start(adx, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, adx);
	va_end(adx);

	/*
	 * To get this data, use the zfs-dprintf probe as so:
	 * dtrace -q -n 'zfs-dprintf \
	 *	/stringof(arg0) == "dbuf.c"/ \
	 *	{printf("%s: %s", stringof(arg1), stringof(arg3))}'
	 * arg0 = file name
	 * arg1 = function name
	 * arg2 = line number
	 * arg3 = message
	 */
	DTRACE_PROBE4(zfs__dprintf,
	    char *, newfile, char *, func, int, line, char *, buf);
}

static void
history_str_free(char *buf)
{
	kmem_free(buf, HIS_MAX_RECORD_LEN);
}

static char *
history_str_get(zfs_cmd_t *zc)
{
	char *buf;

	if (zc->zc_history == NULL)
		return (NULL);

	buf = kmem_alloc(HIS_MAX_RECORD_LEN, KM_SLEEP);
	if (copyinstr((void *)(uintptr_t)zc->zc_history,
	    buf, HIS_MAX_RECORD_LEN, NULL) != 0) {
		history_str_free(buf);
		return (NULL);
	}

	buf[HIS_MAX_RECORD_LEN -1] = '\0';

	return (buf);
}

/*
 * Check to see if the named dataset is currently defined as bootable
 */
static boolean_t
zfs_is_bootfs(const char *name)
{
	objset_t *os;

	if (dmu_objset_hold(name, FTAG, &os) == 0) {
		boolean_t ret;
		ret = (dmu_objset_id(os) == spa_bootfs(dmu_objset_spa(os)));
		dmu_objset_rele(os, FTAG);
		return (ret);
	}
	return (B_FALSE);
}

/*
 * zfs_earlier_version
 *
 *	Return non-zero if the spa version is less than requested version.
 */
static int
zfs_earlier_version(const char *name, int version)
{
	spa_t *spa;

	if (spa_open(name, &spa, FTAG) == 0) {
		if (spa_version(spa) < version) {
			spa_close(spa, FTAG);
			return (1);
		}
		spa_close(spa, FTAG);
	}
	return (0);
}

/*
 * zpl_earlier_version
 *
 * Return TRUE if the ZPL version is less than requested version.
 * Return FALSE for any other objset type because it isn't relevant to
 * them.
 */
static boolean_t
zpl_earlier_version(const char *name, int version)
{
	objset_t *os;
	boolean_t rc = B_TRUE;

	if (dmu_objset_hold(name, FTAG, &os) == 0) {
		uint64_t zplversion;

		if (dmu_objset_type(os) != DMU_OST_ZFS) {
			dmu_objset_rele(os, FTAG);
			return (B_FALSE);
		}
		/* XXX reading from non-owned objset */
		if (zfs_get_zplprop(os, ZFS_PROP_VERSION, &zplversion) == 0)
			rc = zplversion < version;
		dmu_objset_rele(os, FTAG);
	}
	return (rc);
}

static void
zfs_log_history(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *buf;

	if ((buf = history_str_get(zc)) == NULL)
		return;

	if (spa_open(zc->zc_name, &spa, FTAG) == 0) {
		if (spa_version(spa) >= SPA_VERSION_ZPOOL_HISTORY)
			(void) spa_history_log(spa, buf, LOG_CMD_NORMAL);
		spa_close(spa, FTAG);
	}
	history_str_free(buf);
}

typedef boolean_t nvpair_test_f(nvpair_t *);

static boolean_t
prop_readonly_test(nvpair_t *pair)
{
	const char *propname = nvpair_name(pair);
	zfs_prop_t prop = zfs_name_to_prop(propname);
	return (prop != ZPROP_INVAL && zfs_prop_readonly(prop));
}

static void
props_filter(nvlist_t *props, nvpair_test_f is_exclude)
{
	nvpair_t *pair, *next_pair;

	pair = nvlist_next_nvpair(props, NULL);
	while (pair != NULL) {
		next_pair = nvlist_next_nvpair(props, pair);

		if (is_exclude(pair))
			(void) nvlist_remove_nvpair(props, pair);

		pair = next_pair;
	}
}

/*
 * Policy for top-level read operations (list pools).  Requires no privileges,
 * and can be used in the local zone, as there is no associated dataset.
 */
/* ARGSUSED */
static int
zfs_secpolicy_none(zfs_cmd_t *zc, cred_t *cr)
{
	return (0);
}

/*
 * Policy for dataset read operations (list children, get statistics).  Requires
 * no privileges, but must be visible in the local zone.
 */
/* ARGSUSED */
static int
zfs_secpolicy_read(zfs_cmd_t *zc, cred_t *cr)
{
	if (zone_dataset_visible(crgetzone(cr), zc->zc_name, NULL))
		return (0);

	return (ENOENT);
}

static int
zfs_dozonecheck_impl(const char *dataset, uint64_t zoned, const cred_t *cr)
{
	int writable = 1;

	/*
	 * The dataset must be visible by this zone -- check this first
	 * so they don't see EPERM on something they shouldn't know about.
	 */
	if (!zone_dataset_visible(crgetzone(cr), dataset, &writable))
		return (ENOENT);

	if (crgetzoneid(cr) == GLOBAL_ZONEID) {
		/*
		 * If the fs is zoned, only root can access it from the
		 * global zone.
		 */
		if (secpolicy_zfs(cr) && zoned)
			return (EPERM);
	} else {
		/*
		 * If we are in a local zone, the 'zoned' property must be set.
		 */
		if (!zoned)
			return (EPERM);

		/* must be writable by this zone */
		if (!writable)
			return (EPERM);
	}
	return (0);
}

static int
zfs_dozonecheck(const char *dataset, const cred_t *cr)
{
	uint64_t zoned;

	/*
	 * Fast path: if we are root in the global zone, it doesn't
	 * matter what the "zoned" property is, so we can avoid reading
	 * it off disk.
	 */
	if (crgetzoneid(cr) == GLOBAL_ZONEID && secpolicy_zfs(cr) == 0)
		return (0);

	if (dsl_prop_get_integer(dataset, "zoned", &zoned, NULL))
		return (ENOENT);

	return (zfs_dozonecheck_impl(dataset, zoned, cr));
}

static int
zfs_dozonecheck_ds(const char *dataset, dsl_dataset_t *ds, cred_t *cr)
{
	uint64_t zoned;

	/* Fast path, see comment in zfs_dozonecheck(). */
	if (crgetzoneid(cr) == GLOBAL_ZONEID && secpolicy_zfs(cr) == 0)
		return (0);

	rw_enter(&DS_POOL(ds)->dp_config_rwlock, RW_READER);
	if (dsl_prop_get_ds(ds, "zoned", 8, 1, &zoned, NULL,
	    DSL_PROP_GET_EFFECTIVE) != 0) {
		rw_exit(&DS_POOL(ds)->dp_config_rwlock);
		return (ENOENT);
	}
	rw_exit(&DS_POOL(ds)->dp_config_rwlock);

	return (zfs_dozonecheck_impl(dataset, zoned, cr));
}

int
zfs_secpolicy_write_perms(const char *name, const char *perm, cred_t *cr)
{
	int error;

	error = zfs_dozonecheck(name, cr);
	if (error == 0) {
		error = secpolicy_zfs(cr);
		if (error)
			error = dsl_deleg_access(name, perm, cr);
	}
	return (error);
}

int
zfs_secpolicy_write_perms_ds(const char *name, dsl_dataset_t *ds,
    const char *perm, cred_t *cr)
{
	int error;

	error = zfs_dozonecheck_ds(name, ds, cr);
	if (error == 0) {
		error = secpolicy_zfs(cr);
		if (error)
			error = dsl_deleg_access_impl(ds, perm, cr);
	}
	return (error);
}

/*
 * Policy for setting the security label property.
 *
 * Returns 0 for success, non-zero for access and other errors.
 */
static int
zfs_set_mlslabel_policy(const char *name, char *strval, cred_t *cr)
{
	objset_t	*os;
	char		ds_mlslabel[MAXLABELSTR];
	bslabel_t	ds_slabel;		/* existing dataset label */
	bslabel_t	new_slabel;		/* new label being set */
	boolean_t	set_default = B_FALSE;	/* new label is "none" */
	uint64_t	zoned;
	int		needed_priv = -1;
	int		error;

	if (strval == NULL || strlen(strval) >= MAXLABELSTR)
		return (EINVAL);

	/* First get existing dataset label (will be default for new ds) */
	error = dmu_objset_hold(name, FTAG, &os);
	if (error)
		return (error);
	error = zfs_get_mlslabel(os, ds_mlslabel, sizeof (ds_mlslabel));
	dmu_objset_rele(os, FTAG);

	if (error)
		return (EPERM);
	if (strcasecmp(strval, ds_mlslabel) == 0)
		return (0);		/* no effective change */

	set_default = (strcasecmp(strval, ZFS_MLSLABEL_NONE) == 0);

	/* The label must be translatable */
	if (!set_default && (hexstr_to_label(strval, &new_slabel) != 0))
		return (EINVAL);

	/*
	 * In a non-global zone, disallow attempts to set a label
	 * that doesn't match that of the zone; otherwise no other
	 * checks are needed.
	 */
	if (crgetzoneid(cr) != GLOBAL_ZONEID) {
		if (set_default || !blequal(&new_slabel, CR_SL(cr)))
			return (EPERM);
		return (0);
	}

	/*
	 * For global-zone datasets (i.e., those whose zoned property
	 * is "off", verify that the specified new label is valid for
	 * the global zone.
	 */
	if (dsl_prop_get_integer(name,
	    zfs_prop_to_name(ZFS_PROP_ZONED), &zoned, NULL))
		return (EPERM);
	if (!zoned) {
		if (zfs_check_global_label(name, strval) != 0)
			return (EPERM);
	}

	/*
	 * If the existing dataset label is nondefault, check if the
	 * dataset is mounted (label cannot be changed while mounted).
	 * Get the zfsvfs; if there isn't one, then the dataset isn't
	 * mounted (or isn't a dataset, doesn't exist, ...).
	 */
	if (strcasecmp(ds_mlslabel, ZFS_MLSLABEL_NONE) != 0) {
		objset_t *os;
		static char *setsl_tag = "setsl_tag";

		/*
		 * Try to own the dataset; abort if there is any error,
		 * (e.g., already mounted, in use, or other error).
		 */
		error = dmu_objset_own(name, DMU_OST_ZFS, B_TRUE,
		    setsl_tag, &os);
		if (error)
			return (EPERM);

		dmu_objset_disown(os, setsl_tag);

		if (set_default) {
			needed_priv = PRIV_FILE_DOWNGRADE_SL;
			goto out_check;
		}
		if (hexstr_to_label(strval, &new_slabel) != 0)
			return (EPERM);

		if (blstrictdom(&ds_slabel, &new_slabel))
			needed_priv = PRIV_FILE_DOWNGRADE_SL;
		else if (blstrictdom(&new_slabel, &ds_slabel))
			needed_priv = PRIV_FILE_UPGRADE_SL;
	} else {
		/* dataset currently has a default label */
		needed_priv = PRIV_FILE_UPGRADE_SL;
	}

out_check:
	if (needed_priv != -1)
		return (PRIV_POLICY(cr, needed_priv, B_FALSE, EPERM, NULL));
	return (0);
}

static int
zfs_secpolicy_setprop(const char *dsname, zfs_prop_t prop, nvpair_t *propval,
    cred_t *cr)
{
	char *strval;

	/*
	 * Check permissions for special properties.
	 */
	switch (prop) {
	case ZFS_PROP_ZONED:
		/*
		 * Disallow setting of 'zoned' from within a local zone.
		 */
		if (crgetzoneid(cr) != GLOBAL_ZONEID)
			return (EPERM);
		break;

	case ZFS_PROP_QUOTA:
		if (crgetzoneid(cr) != GLOBAL_ZONEID) {
			uint64_t zoned;
			char setpoint[MAXNAMELEN];
			/*
			 * Unprivileged users are allowed to modify the
			 * quota on things *under* (ie. contained by)
			 * the thing they own.
			 */
			if (dsl_prop_get_integer(dsname, "zoned", &zoned,
			    setpoint))
				return (EPERM);
			if (!zoned || strlen(dsname) <= strlen(setpoint))
				return (EPERM);
		}
		break;

	case ZFS_PROP_MLSLABEL:
		if (!is_system_labeled())
			return (EPERM);

		if (nvpair_value_string(propval, &strval) == 0) {
			int err;

			err = zfs_set_mlslabel_policy(dsname, strval, CRED());
			if (err != 0)
				return (err);
		}
		break;
	}

	return (zfs_secpolicy_write_perms(dsname, zfs_prop_to_name(prop), cr));
}

int
zfs_secpolicy_fsacl(zfs_cmd_t *zc, cred_t *cr)
{
	int error;

	error = zfs_dozonecheck(zc->zc_name, cr);
	if (error)
		return (error);

	/*
	 * permission to set permissions will be evaluated later in
	 * dsl_deleg_can_allow()
	 */
	return (0);
}

int
zfs_secpolicy_rollback(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_ROLLBACK, cr));
}

int
zfs_secpolicy_send(zfs_cmd_t *zc, cred_t *cr)
{
	spa_t *spa;
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	char *cp;
	int error;

	/*
	 * Generate the current snapshot name from the given objsetid, then
	 * use that name for the secpolicy/zone checks.
	 */
	cp = strchr(zc->zc_name, '@');
	if (cp == NULL)
		return (EINVAL);
	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error)
		return (error);

	dp = spa_get_dsl(spa);
	rw_enter(&dp->dp_config_rwlock, RW_READER);
	error = dsl_dataset_hold_obj(dp, zc->zc_sendobj, FTAG, &ds);
	rw_exit(&dp->dp_config_rwlock);
	spa_close(spa, FTAG);
	if (error)
		return (error);

	dsl_dataset_name(ds, zc->zc_name);

	error = zfs_secpolicy_write_perms_ds(zc->zc_name, ds,
	    ZFS_DELEG_PERM_SEND, cr);
	dsl_dataset_rele(ds, FTAG);

	return (error);
}

int
zfs_secpolicy_share(char *path, cred_t *cr)
{
	vnode_t *vp;
	char ds_name[MAXPATHLEN];
	int error;

	if ((error = lookupname(path, UIO_SYSSPACE,
	    NO_FOLLOW, NULL, &vp)) != 0)
		return (error);

	/* Now make sure mntpnt and dataset are ZFS */
	if (vp->v_vfsp->vfs_fstype != zfsfstype) {
		VN_RELE(vp);
		return (EPERM);
	}

	(void) strlcpy(ds_name,
	    refstr_value(vp->v_vfsp->vfs_resource),
	    sizeof (ds_name));
	VN_RELE(vp);

	return (dsl_deleg_access(ds_name,
	    ZFS_DELEG_PERM_SHARE, cr));
}

static int
zfs_secpolicy_deleg_share(zfs_cmd_t *zc, cred_t *cr)
{
	vnode_t *vp;
	int error;

	if ((error = lookupname(zc->zc_value, UIO_SYSSPACE,
	    NO_FOLLOW, NULL, &vp)) != 0)
		return (error);

	/* Now make sure mntpnt and dataset are ZFS */

	if (vp->v_vfsp->vfs_fstype != zfsfstype ||
	    (strcmp((char *)refstr_value(vp->v_vfsp->vfs_resource),
	    zc->zc_name) != 0)) {
		VN_RELE(vp);
		return (EPERM);
	}

	VN_RELE(vp);
	return (dsl_deleg_access(zc->zc_name,
	    ZFS_DELEG_PERM_SHARE, cr));
}

/*
 * requires PRIV_SYS_SHARE or delegated authority
 * to manage zfs share files.
 */
int
zfs_secpolicy_share_resource(zfs_cmd_t *zc, cred_t *cr)
{
	if (secpolicy_share(cr) == 0) {
		return (0);
	} else {
		return (zfs_secpolicy_deleg_share(zc, cr));
	}
}

static int
zfs_get_parent(const char *datasetname, char *parent, int parentsize)
{
	char *cp;

	/*
	 * Remove the @bla or /bla from the end of the name to get the parent.
	 */
	(void) strncpy(parent, datasetname, parentsize);
	cp = strrchr(parent, '@');
	if (cp != NULL) {
		cp[0] = '\0';
	} else {
		cp = strrchr(parent, '/');
		if (cp == NULL)
			return (ENOENT);
		cp[0] = '\0';
	}

	return (0);
}

int
zfs_secpolicy_destroy_perms(const char *name, cred_t *cr)
{
	int error;

	if ((error = zfs_secpolicy_write_perms(name,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	return (zfs_secpolicy_write_perms(name, ZFS_DELEG_PERM_DESTROY, cr));
}

static int
zfs_secpolicy_destroy(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_destroy_perms(zc->zc_name, cr));
}

/*
 * Destroying snapshots with delegated permissions requires
 * descendent mount and destroy permissions.
 * Reassemble the full filesystem@snap name so dsl_deleg_access()
 * can do the correct permission check.
 *
 * Since this routine is used when doing a recursive destroy of snapshots
 * and destroying snapshots requires descendent permissions, a successfull
 * check of the top level snapshot applies to snapshots of all descendent
 * datasets as well.
 */
static int
zfs_secpolicy_destroy_snaps(zfs_cmd_t *zc, cred_t *cr)
{
	int error;
	char *dsname;

	dsname = kmem_asprintf("%s@%s", zc->zc_name, zc->zc_value);

	error = zfs_secpolicy_destroy_perms(dsname, cr);

	strfree(dsname);
	return (error);
}

int
zfs_secpolicy_rename_perms(const char *from, const char *to, cred_t *cr)
{
	char	parentname[MAXNAMELEN];
	int	error;

	if ((error = zfs_secpolicy_write_perms(from,
	    ZFS_DELEG_PERM_RENAME, cr)) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(from,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	if ((error = zfs_get_parent(to, parentname,
	    sizeof (parentname))) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_CREATE, cr)) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	return (error);
}

static int
zfs_secpolicy_rename(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_rename_perms(zc->zc_name, zc->zc_value, cr));
}

static int
zfs_secpolicy_promote(zfs_cmd_t *zc, cred_t *cr)
{
	char	parentname[MAXNAMELEN];
	objset_t *clone;
	int error;

	error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_PROMOTE, cr);
	if (error)
		return (error);

	error = dmu_objset_hold(zc->zc_name, FTAG, &clone);

	if (error == 0) {
		dsl_dataset_t *pclone = NULL;
		dsl_dir_t *dd;
		dd = clone->os_dsl_dataset->ds_dir;

		rw_enter(&dd->dd_pool->dp_config_rwlock, RW_READER);
		error = dsl_dataset_hold_obj(dd->dd_pool,
		    dd->dd_phys->dd_origin_obj, FTAG, &pclone);
		rw_exit(&dd->dd_pool->dp_config_rwlock);
		if (error) {
			dmu_objset_rele(clone, FTAG);
			return (error);
		}

		if (dmu_objset_type(clone) != DMU_OST_ZVOL)
			error = zfs_secpolicy_write_perms(zc->zc_name,
			    ZFS_DELEG_PERM_MOUNT, cr);

		dsl_dataset_name(pclone, parentname);
		dmu_objset_rele(clone, FTAG);
		dsl_dataset_rele(pclone, FTAG);
		if (error == 0)
			error = zfs_secpolicy_write_perms(parentname,
			    ZFS_DELEG_PERM_PROMOTE, cr);
	}
	return (error);
}

static int
zfs_secpolicy_receive(zfs_cmd_t *zc, cred_t *cr)
{
	int error;

	if ((error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_RECEIVE, cr)) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_CREATE, cr));
}

int
zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(name,
	    ZFS_DELEG_PERM_SNAPSHOT, cr));
}

static int
zfs_secpolicy_snapshot(zfs_cmd_t *zc, cred_t *cr)
{

	return (zfs_secpolicy_snapshot_perms(zc->zc_name, cr));
}

static int
zfs_secpolicy_create(zfs_cmd_t *zc, cred_t *cr)
{
	char	parentname[MAXNAMELEN];
	int	error;

	if ((error = zfs_get_parent(zc->zc_name, parentname,
	    sizeof (parentname))) != 0)
		return (error);

	if (zc->zc_value[0] != '\0') {
		if ((error = zfs_secpolicy_write_perms(zc->zc_value,
		    ZFS_DELEG_PERM_CLONE, cr)) != 0)
			return (error);
	}

	if ((error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_CREATE, cr)) != 0)
		return (error);

	error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_MOUNT, cr);

	return (error);
}

static int
zfs_secpolicy_umount(zfs_cmd_t *zc, cred_t *cr)
{
	int error;

	error = secpolicy_fs_unmount(cr, NULL);
	if (error) {
		error = dsl_deleg_access(zc->zc_name, ZFS_DELEG_PERM_MOUNT, cr);
	}
	return (error);
}

/*
 * Policy for pool operations - create/destroy pools, add vdevs, etc.  Requires
 * SYS_CONFIG privilege, which is not available in a local zone.
 */
/* ARGSUSED */
static int
zfs_secpolicy_config(zfs_cmd_t *zc, cred_t *cr)
{
	if (secpolicy_sys_config(cr, B_FALSE) != 0)
		return (EPERM);

	return (0);
}

/*
 * Policy for object to name lookups.
 */
/* ARGSUSED */
static int
zfs_secpolicy_diff(zfs_cmd_t *zc, cred_t *cr)
{
	int error;

	if ((error = secpolicy_sys_config(cr, B_FALSE)) == 0)
		return (0);

	error = zfs_secpolicy_write_perms(zc->zc_name, ZFS_DELEG_PERM_DIFF, cr);
	return (error);
}

/*
 * Policy for fault injection.  Requires all privileges.
 */
/* ARGSUSED */
static int
zfs_secpolicy_inject(zfs_cmd_t *zc, cred_t *cr)
{
	return (secpolicy_zinject(cr));
}

static int
zfs_secpolicy_inherit(zfs_cmd_t *zc, cred_t *cr)
{
	zfs_prop_t prop = zfs_name_to_prop(zc->zc_value);

	if (prop == ZPROP_INVAL) {
		if (!zfs_prop_user(zc->zc_value))
			return (EINVAL);
		return (zfs_secpolicy_write_perms(zc->zc_name,
		    ZFS_DELEG_PERM_USERPROP, cr));
	} else {
		return (zfs_secpolicy_setprop(zc->zc_name, prop,
		    NULL, cr));
	}
}

static int
zfs_secpolicy_userspace_one(zfs_cmd_t *zc, cred_t *cr)
{
	int err = zfs_secpolicy_read(zc, cr);
	if (err)
		return (err);

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (EINVAL);

	if (zc->zc_value[0] == 0) {
		/*
		 * They are asking about a posix uid/gid.  If it's
		 * themself, allow it.
		 */
		if (zc->zc_objset_type == ZFS_PROP_USERUSED ||
		    zc->zc_objset_type == ZFS_PROP_USERQUOTA) {
			if (zc->zc_guid == crgetuid(cr))
				return (0);
		} else {
			if (groupmember(zc->zc_guid, cr))
				return (0);
		}
	}

	return (zfs_secpolicy_write_perms(zc->zc_name,
	    userquota_perms[zc->zc_objset_type], cr));
}

static int
zfs_secpolicy_userspace_many(zfs_cmd_t *zc, cred_t *cr)
{
	int err = zfs_secpolicy_read(zc, cr);
	if (err)
		return (err);

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (EINVAL);

	return (zfs_secpolicy_write_perms(zc->zc_name,
	    userquota_perms[zc->zc_objset_type], cr));
}

static int
zfs_secpolicy_userspace_upgrade(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_setprop(zc->zc_name, ZFS_PROP_VERSION,
	    NULL, cr));
}

static int
zfs_secpolicy_hold(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_HOLD, cr));
}

static int
zfs_secpolicy_release(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_RELEASE, cr));
}

/*
 * Policy for allowing temporary snapshots to be taken or released
 */
static int
zfs_secpolicy_tmp_snapshot(zfs_cmd_t *zc, cred_t *cr)
{
	/*
	 * A temporary snapshot is the same as a snapshot,
	 * hold, destroy and release all rolled into one.
	 * Delegated diff alone is sufficient that we allow this.
	 */
	int error;

	if ((error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_DIFF, cr)) == 0)
		return (0);

	error = zfs_secpolicy_snapshot(zc, cr);
	if (!error)
		error = zfs_secpolicy_hold(zc, cr);
	if (!error)
		error = zfs_secpolicy_release(zc, cr);
	if (!error)
		error = zfs_secpolicy_destroy(zc, cr);
	return (error);
}

/*
 * The crypto opertations on datasets (including those at
 * create/recv time) fall into two security policies,
 * key use (protected by the "key" delegation):
 * 	load, unload, inherit
 * and key change * (protected by the "keychange" delegation:
 * 	wrapping key change, new data encryption key
 */
static int
zfs_secpolicy_crypto_keyuse(zfs_cmd_t *zc, cred_t *cr)
{
	int err;
	char val[MAXNAMELEN];		/* value ignored */
	char setpoint[MAXNAMELEN];	/* dataset where keysource is set */


	if ((err = zfs_secpolicy_write_perms(zc->zc_name, ZFS_DELEG_PERM_KEY,
	    cr)) != 0)
		return (err);

	if (crgetzone(cr) == global_zone)
		return (0);

	/*
	 * If not in the global zone, be sure the key is not inherited
	 * from the global zone
	 */
	if (dsl_prop_get(zc->zc_name, zfs_prop_to_name(ZFS_PROP_KEYSOURCE), 1,
	    sizeof (val), val, setpoint) != 0)
		return (ENOENT);
	if (strcmp(setpoint, ZONE_INVISIBLE_SOURCE) == 0)
		return (EPERM);

	return (0);
}

static int
zfs_secpolicy_crypto_keychange(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_KEYCHANGE, cr));
}

/*
 * Zone Dataset Aliasing
 *
 * The datasets that are delegated to a zone are aliased such that they appear
 * as virtual zpools within the zone.  zfs_unalias() and zfs_alias() are
 * responsible for translating dataset names in the various members (other than
 * nvlists) of the zfs_cmd_t structure.
 *
 * zfs_unalias()	Called just after entering zfsdev_ioctl() - before
 * 			any other zfs_ioc_* functions are called.  It
 * 			translates from the aliased namespace to the real
 * 			namespace.
 * zfs_alias()		Called just before leaving zfsdev_ioctl() - after
 * 			any other zfs_ioc_* functions are called.  It
 * 			translates from the real namespace to the aliased
 * 			namespace.
 *
 * zfs_ioc_*() functions that return dataset or pool names within nvlists are
 * responsible for calling zone_dataset_alias() on strings returned as names
 * or values in nvlists.  Note that there may not be a one to one mapping
 * between real and virtual pools: a zone may have several datasets delegated
 * from the same real pool.  Each of those delegated datasets will appear as
 * a virtual pool within the zone.
 *
 * The /dev pseudo file system is sensitive to how the 'zone' argument to
 * zone_dataset_unalias() and zone_dataset_alias() is derived.  If curzone
 * is used, access to a zone's /dev/zvol/{dsk,rdsk} directories via the
 * global zone will cause those directories to be generated improperly.  It
 * is essential to the /dev file system that zc_name and any nvlist of pools
 * be generated using the the zone associated with the cred passed to
 * zfsdev_ioctl().
 */
static void
zfs_unalias(zfs_cmd_t *zc, cred_t *cr, zfs_zc_alias_t flags) {
	zone_t *zone = crgetzone(cr);

	if (zone == global_zone || flags == DATASET_ALIAS_NONE)
		return;

	/* See "The /dev pseudo file system" in the block comment above. */
	if ((flags & DATASET_ALIAS_NAME) != 0)
		(void) zone_dataset_unalias(zone, zc->zc_name, zc->zc_name,
		    sizeof (zc->zc_name));

	if ((flags & DATASET_ALIAS_VALUE) != 0)
		(void) zone_dataset_unalias(zone, zc->zc_value, zc->zc_value,
		    sizeof (zc->zc_value));

	if ((flags & DATASET_ALIAS_TOP_DS) != 0)
		(void) zone_dataset_unalias(zone, zc->zc_top_ds, zc->zc_top_ds,
		    sizeof (zc->zc_top_ds));

	if ((flags & DATASET_ALIAS_STRING) != 0)
		(void) zone_dataset_unalias(zone, zc->zc_string, zc->zc_string,
		    sizeof (zc->zc_string));

	/*
	 * Any value passed in via zc_objset_setats.dds_origin is ignored and
	 * as such  DATASET_ALIAS_ORIGIN is not implemented in zfs_unalias().
	 * It is implemented for zfs_alias();
	 */

	/*
	 * Crypto is special.  If called from a zone, the keysource property
	 * may have been inherited from the global zone and as such, the real
	 * source dataset name is hidden from the caller.  That is, the zone
	 * previously performed a ZFS_IOC_OBJSET_STATS and got back a keysource
	 * property with source "$globalzone", then passed that in a subsequent
	 * ZFS_IOC_CREATE call.  In such a case, we need to perform a lookup of
	 * the real inherited dataset here.
	 */
	if ((flags & DATASET_ALIAS_CRYPTO) != 0) {
		if (strcmp(zc->zc_crypto.zic_inherit_dsname,
		    ZONE_INVISIBLE_SOURCE) == 0) {
			char parent[MAXNAMELEN];
			char val[MAXNAMELEN];
			char *slash;

			(void) strlcpy(parent, zc->zc_name, sizeof (parent));
			if ((slash = strrchr(parent, '/')) != NULL) {
				*slash = '\0';
			}

			(void) dsl_prop_get_zone(parent,
			    zfs_prop_to_name(ZFS_PROP_KEYSOURCE), 1,
			    sizeof (val), val,
			    zc->zc_crypto.zic_inherit_dsname, global_zone);
		} else {
			(void) zone_dataset_unalias(zone,
			    zc->zc_crypto.zic_inherit_dsname,
			    zc->zc_crypto.zic_inherit_dsname,
			    sizeof (zc->zc_crypto.zic_inherit_dsname));
		}
	}

}

/* See comment above zfs_unalias() */
static void
zfs_alias(zfs_cmd_t *zc, cred_t *cr, zfs_zc_alias_t flags) {
	zone_t *zone = crgetzone(cr);

	if (zone == global_zone || flags == DATASET_ALIAS_NONE)
		return;

	/* See "The /dev pseudo file system" in the block comment above. */
	if ((flags & DATASET_ALIAS_NAME) != 0)
		(void) zone_dataset_alias(zone, zc->zc_name, zc->zc_name,
		    sizeof (zc->zc_name));

	if ((flags & DATASET_ALIAS_VALUE) != 0)
		(void) zone_dataset_alias(zone, zc->zc_value, zc->zc_value,
		    sizeof (zc->zc_value));

	if ((flags & DATASET_ALIAS_TOP_DS) != 0)
		(void) zone_dataset_alias(zone, zc->zc_top_ds, zc->zc_top_ds,
		    sizeof (zc->zc_top_ds));

	if ((flags & DATASET_ALIAS_STRING) != 0)
		(void) zone_dataset_alias(zone, zc->zc_string, zc->zc_string,
		    sizeof (zc->zc_string));

	if ((flags & DATASET_ALIAS_ORIGIN) != 0) {
		(void) zone_dataset_alias(zone, zc->zc_objset_stats.dds_origin,
		    zc->zc_objset_stats.dds_origin,
		    sizeof (zc->zc_objset_stats.dds_origin));
	}

	if ((flags & DATASET_ALIAS_CRYPTO) != 0) {
		(void) zone_dataset_alias(zone,
		    zc->zc_crypto.zic_inherit_dsname,
		    zc->zc_crypto.zic_inherit_dsname,
		    sizeof (zc->zc_crypto.zic_inherit_dsname));
	}
}

/*
 * Returns the nvlist as specified by the user in the zfs_cmd_t.
 */
static int
get_nvlist(uint64_t nvl, uint64_t size, int iflag, nvlist_t **nvp)
{
	char *packed;
	int error;
	nvlist_t *list = NULL;

	/*
	 * Read in and unpack the user-supplied nvlist.
	 */
	if (size == 0)
		return (EINVAL);

	packed = kmem_alloc(size, KM_SLEEP);

	if ((error = ddi_copyin((void *)(uintptr_t)nvl, packed, size,
	    iflag)) != 0) {
		kmem_free(packed, size);
		return (error);
	}

	if ((error = nvlist_unpack(packed, size, &list, 0)) != 0) {
		kmem_free(packed, size);
		return (error);
	}

	kmem_free(packed, size);

	*nvp = list;
	return (0);
}

static int
fit_error_list(zfs_cmd_t *zc, nvlist_t **errors)
{
	size_t size;

	VERIFY(nvlist_size(*errors, &size, NV_ENCODE_NATIVE) == 0);

	if (size > zc->zc_nvlist_dst_size) {
		nvpair_t *more_errors;
		int n = 0;

		if (zc->zc_nvlist_dst_size < 1024)
			return (ENOMEM);

		VERIFY(nvlist_add_int32(*errors, ZPROP_N_MORE_ERRORS, 0) == 0);
		more_errors = nvlist_prev_nvpair(*errors, NULL);

		do {
			nvpair_t *pair = nvlist_prev_nvpair(*errors,
			    more_errors);
			VERIFY(nvlist_remove_nvpair(*errors, pair) == 0);
			n++;
			VERIFY(nvlist_size(*errors, &size,
			    NV_ENCODE_NATIVE) == 0);
		} while (size > zc->zc_nvlist_dst_size);

		VERIFY(nvlist_remove_nvpair(*errors, more_errors) == 0);
		VERIFY(nvlist_add_int32(*errors, ZPROP_N_MORE_ERRORS, n) == 0);
		ASSERT(nvlist_size(*errors, &size, NV_ENCODE_NATIVE) == 0);
		ASSERT(size <= zc->zc_nvlist_dst_size);
	}

	return (0);
}

static int
put_nvlist(zfs_cmd_t *zc, nvlist_t *nvl)
{
	char *packed = NULL;
	int error = 0;
	size_t size;

	VERIFY(nvlist_size(nvl, &size, NV_ENCODE_NATIVE) == 0);

	if (size > zc->zc_nvlist_dst_size) {
		error = ENOMEM;
	} else {
		packed = kmem_alloc(size, KM_SLEEP);
		VERIFY(nvlist_pack(nvl, &packed, &size, NV_ENCODE_NATIVE,
		    KM_SLEEP) == 0);
		if (ddi_copyout(packed, (void *)(uintptr_t)zc->zc_nvlist_dst,
		    size, zc->zc_iflags) != 0)
			error = EFAULT;
		kmem_free(packed, size);
	}

	zc->zc_nvlist_dst_size = size;
	return (error);
}

static int
getzfsvfs(const char *dsname, zfsvfs_t **zfvp)
{
	objset_t *os;
	int error;

	error = dmu_objset_hold(dsname, FTAG, &os);
	if (error)
		return (error);
	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		dmu_objset_rele(os, FTAG);
		return (EINVAL);
	}

	mutex_enter(&os->os_user_ptr_lock);
	*zfvp = dmu_objset_get_user(os);
	if (*zfvp) {
		VFS_HOLD((*zfvp)->z_vfs);
	} else {
		error = ESRCH;
	}
	mutex_exit(&os->os_user_ptr_lock);
	dmu_objset_rele(os, FTAG);
	return (error);
}

static void
zfsvfs_rele(zfsvfs_t *zfsvfs, void *tag)
{
	rrw_exit(&zfsvfs->z_teardown_lock, tag);

	if (zfsvfs->z_vfs) {
		VFS_RELE(zfsvfs->z_vfs);
	} else {
		dmu_objset_disown(zfsvfs->z_os, zfsvfs);
		zfsvfs_free(zfsvfs);
	}
}

/*
 * Find a zfsvfs_t for a mounted filesystem, or create our own, in which
 * case its z_vfs will be NULL, and it will be opened as the owner.
 */
static int
zfsvfs_hold(const char *name, void *tag, zfsvfs_t **zfvp, boolean_t writer)
{
	int error = 0;

	if (getzfsvfs(name, zfvp) != 0)
		error = zfsvfs_create(name, zfvp);
	if (error == 0) {
		rrw_enter(&(*zfvp)->z_teardown_lock, (writer) ? RW_WRITER :
		    RW_READER, tag);
		if ((*zfvp)->z_unmounted) {
			/*
			 * XXX we could probably try again, since the unmounting
			 * thread should be just about to disassociate the
			 * objset from the zfsvfs.
			 */
			zfsvfs_rele(*zfvp, tag);
			return (EBUSY);
		}
	}
	return (error);
}

static int
zfs_ioc_pool_create(zfs_cmd_t *zc)
{
	int error;
	nvlist_t *config, *props = NULL;
	nvlist_t *rootprops = NULL;
	nvlist_t *zplprops = NULL;
	char *buf;
	dsl_crypto_ctx_t dcc = { 0 };

	if (error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config))
		return (error);

	if (zc->zc_nvlist_src_size != 0 && (error =
	    get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))) {
		nvlist_free(config);
		return (error);
	}

	if ((error = zfs_get_crypto_ctx(zc, &dcc)) != 0) {
		return (error);
	}

	if (props) {
		nvlist_t *nvl = NULL;
		uint64_t version = SPA_VERSION;

		(void) nvlist_lookup_uint64(props,
		    zpool_prop_to_name(ZPOOL_PROP_VERSION), &version);
		if (version < SPA_VERSION_INITIAL || version > SPA_VERSION) {
			error = EINVAL;
			goto pool_props_bad;
		}
		(void) nvlist_lookup_nvlist(props, ZPOOL_ROOTFS_PROPS, &nvl);
		if (nvl) {
			error = nvlist_dup(nvl, &rootprops, KM_SLEEP);
			if (error != 0) {
				nvlist_free(config);
				nvlist_free(props);
				return (error);
			}
			(void) nvlist_remove_all(props, ZPOOL_ROOTFS_PROPS);
		}
		VERIFY(nvlist_alloc(&zplprops, NV_UNIQUE_NAME, KM_SLEEP) == 0);
		error = zfs_fill_zplprops_root(version, rootprops, zplprops);
		if (error)
			goto pool_props_bad;
	}

	buf = history_str_get(zc);

	error = spa_create(zc->zc_name, config, props, buf, &dcc, zplprops);

	/*
	 * Set the remaining root properties
	 */
	if (!error && (error = zfs_set_prop_nvlist(zc->zc_name,
	    ZPROP_SRC_LOCAL, rootprops, NULL, 0)) != 0)
		(void) spa_destroy(zc->zc_name);

	if (buf != NULL)
		history_str_free(buf);

pool_props_bad:
	nvlist_free(rootprops);
	nvlist_free(zplprops);
	nvlist_free(config);
	nvlist_free(props);

	return (error);
}

static int
zfs_ioc_pool_destroy(zfs_cmd_t *zc)
{
	int error;
	zfs_log_history(zc);
	error = spa_destroy(zc->zc_name);
	if (error == 0)
		zvol_remove_minors(zc->zc_name);
	return (error);
}

static int
zfs_ioc_pool_import(zfs_cmd_t *zc)
{
	nvlist_t *config, *props = NULL;
	uint64_t guid;
	int error;

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)) != 0)
		return (error);

	if (zc->zc_nvlist_src_size != 0 && (error =
	    get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))) {
		nvlist_free(config);
		return (error);
	}

	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID, &guid) != 0 ||
	    guid != zc->zc_guid)
		error = EINVAL;
	else
		error = spa_import(zc->zc_name, config, props, zc->zc_cookie);

	if (zc->zc_nvlist_dst != 0) {
		int err;

		if ((err = put_nvlist(zc, config)) != 0)
			error = err;
	}

	nvlist_free(config);

	if (props)
		nvlist_free(props);

	return (error);
}

static int
zfs_ioc_pool_export(zfs_cmd_t *zc)
{
	int error;
	boolean_t force = (boolean_t)zc->zc_cookie;
	boolean_t hardforce = (boolean_t)zc->zc_guid;

	zfs_log_history(zc);
	error = spa_export(zc->zc_name, NULL, force, hardforce);
	if (error == 0)
		zvol_remove_minors(zc->zc_name);
	return (error);
}

static int
zfs_ioc_pool_configs(zfs_cmd_t *zc)
{
	nvlist_t *configs;
	int error;

	configs = spa_all_configs(&zc->zc_cookie,
	    (cred_t *)(uintptr_t)zc->zc_cred);

	if (configs == NULL)
		return (EEXIST);

	error = put_nvlist(zc, configs);

	nvlist_free(configs);

	return (error);
}

static int
zfs_ioc_pool_stats(zfs_cmd_t *zc)
{
	nvlist_t *config;
	int error;
	int ret = 0;

	error = spa_get_stats(zc->zc_name, &config, zc->zc_value,
	    sizeof (zc->zc_value));

	if (config != NULL) {
		zone_t *zone = crgetzone((cred_t *)(uintptr_t)zc->zc_cred);
		if (zone != global_zone) {
			char pool[ZFS_MAXNAMELEN];
			char *slash;

			/* Copy the aliased pool name to property list */
			(void) zone_dataset_alias(zone, zc->zc_name, pool,
			    sizeof (pool));
			if ((slash = strchr(pool, '/')) != NULL)
				*slash = '\0';
			VERIFY(nvlist_add_string(config,
			    ZPOOL_CONFIG_POOL_NAME, pool) == 0);

			/* Hide the hostname of the global zone */
			VERIFY(nvlist_add_string(config,
			    ZPOOL_CONFIG_HOSTNAME,
			    ZONE_INVISIBLE_SOURCE) == 0);
		}

		ret = put_nvlist(zc, config);
		nvlist_free(config);

		/*
		 * The config may be present even if 'error' is non-zero.
		 * In this case we return success, and preserve the real errno
		 * in 'zc_cookie'.
		 */
		zc->zc_cookie = error;
	} else {
		ret = error;
	}

	return (ret);
}

/*
 * Try to import the given pool, returning pool stats as appropriate so that
 * user land knows which devices are available and overall pool health.
 */
static int
zfs_ioc_pool_tryimport(zfs_cmd_t *zc)
{
	nvlist_t *tryconfig, *config;
	int error;
	boolean_t trusted = (boolean_t)zc->zc_cookie;

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &tryconfig)) != 0)
		return (error);

	config = spa_tryimport(tryconfig, trusted);

	nvlist_free(tryconfig);

	if (config == NULL)
		return (EINVAL);

	error = put_nvlist(zc, config);
	nvlist_free(config);

	return (error);
}

/*
 * inputs:
 * zc_name              name of the pool
 * zc_cookie            scan func (pool_scan_func_t)
 */
static int
zfs_ioc_pool_scan(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (zc->zc_cookie == POOL_SCAN_NONE)
		error = spa_scan_stop(spa);
	else
		error = spa_scan(spa, zc->zc_cookie);

	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_pool_freeze(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error == 0) {
		spa_freeze(spa);
		spa_close(spa, FTAG);
	}
	return (error);
}

static int
zfs_ioc_pool_upgrade(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (zc->zc_cookie < spa_version(spa) || zc->zc_cookie > SPA_VERSION) {
		spa_close(spa, FTAG);
		return (EINVAL);
	}

	spa_upgrade(spa, zc->zc_cookie);
	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_pool_get_history(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *hist_buf;
	uint64_t size;
	int error;

	if ((size = zc->zc_history_len) == 0)
		return (EINVAL);

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (spa_version(spa) < SPA_VERSION_ZPOOL_HISTORY) {
		spa_close(spa, FTAG);
		return (ENOTSUP);
	}

	hist_buf = kmem_alloc(size, KM_SLEEP);
	if ((error = spa_history_get(spa, &zc->zc_history_offset,
	    &zc->zc_history_len, hist_buf)) == 0) {
		error = ddi_copyout(hist_buf,
		    (void *)(uintptr_t)zc->zc_history,
		    zc->zc_history_len, zc->zc_iflags);
	}

	spa_close(spa, FTAG);
	kmem_free(hist_buf, size);
	return (error);
}

static int
zfs_ioc_dsobj_to_dsname(zfs_cmd_t *zc)
{
	int error;

	if (error = dsl_dsobj_to_dsname(zc->zc_name, zc->zc_obj, zc->zc_value))
		return (error);

	return (0);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_obj		object to find
 *
 * outputs:
 * zc_value		name of object
 */
static int
zfs_ioc_obj_to_path(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;

	/* XXX reading from objset not owned */
	if ((error = dmu_objset_hold(zc->zc_name, FTAG, &os)) != 0)
		return (error);
	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		dmu_objset_rele(os, FTAG);
		return (EINVAL);
	}
	error = zfs_obj_to_path(os, zc->zc_obj, zc->zc_value,
	    sizeof (zc->zc_value));
	dmu_objset_rele(os, FTAG);

	return (error);
}


/*
 * Get stats (and optionally names) for a range of object numbers
 * The stats entries are stored in the front of the buffer
 * The object names are stored at the end of the buffer
 * When the buffer is full or the range is exhausted,
 * return to the caller with various fields updated.
 * Inputs:
 *   zc_name		name of filesystem
 *   zc_fromobj		first object to scan
 *   zc_obj		last object to scan (inclusive)
 *   zc_needname	true if need name as well as stats
 *   zc_stat_buf	pointer to stats buffer
 *   zc_stat_buflen	length of stats buffer
 *
 * Outputs:
 *   zc_stat_buf	filled in with stat and path info
 *   zc_cookie		number of records returned
 *   zc_fromobj         first object not yet scanned
 */
static int
zfs_ioc_bulk_obj_to_stats(zfs_cmd_t *zc)
{
	objset_t *os;
	zfs_stat_t *zs_buf, *zs_rec;
	void *endbuf, *strbuf;
	int error;
	uintptr_t buflen;
	uintptr_t len;
	zfsvfs_t *zfsvfs;
	char pathbuf[MAXPATHLEN * 2];
	int pathbuflen = zc->zc_needname ? MAXPATHLEN * 2 : 0;

	buflen = zc->zc_stat_buflen;
	if (buflen < sizeof (zfs_stat_t)+ MAXPATHLEN*2 ||
	    buflen > MAX_ZFS_BULK_STATS_BUF_LEN)
		return (EINVAL);

	error = zfsvfs_hold(zc->zc_name, FTAG, &zfsvfs, B_FALSE);
	if (error != 0) {
		return (error);
	}
	ASSERT(dmu_objset_type(zfsvfs->z_os) == DMU_OST_ZFS);
	os = zfsvfs->z_os;

	zs_rec = zs_buf = kmem_alloc(buflen, KM_SLEEP);
	strbuf = endbuf = (void *)((uintptr_t)zs_rec + buflen);

	/* Start with an initial next-object scan to find where to start */
	if (zc->zc_fromobj != 0)
		zc->zc_fromobj--;

	/*
	 * Loop until the stats area in low addresses and the names area in
	 * higher address have met in the middle.
	 * Note: We always do at least one pass through this loop.
	 */
	while ((error = dmu_object_next(os, &zc->zc_fromobj,
	    B_FALSE, 0)) == 0) {
		/* If past the end of the range, then we are all done */
		if (zc->zc_fromobj > zc->zc_obj ||
		    (uintptr_t)zs_rec >= ((uintptr_t)strbuf -
		    sizeof (zfs_stat_t)))
			break;

		/* Get the stats and path info for this object */
		error = zfs_obj_to_stats(zfsvfs, zc->zc_fromobj,
		    zs_rec, pathbuf, pathbuflen);

		if ((error == ENOENT) || (error == ENOTSUP)) {
			/*
			 * If we get ENOENT, then the object just doesn't exist.
			 * If we get ENOTSUP, then we tried to get info on a
			 * non-ZPL object, which we don't care about anyway.
			 */
			continue;
		} else if (error != 0) {
			break;
		} else {
			/* We have an object. See if it fits in output buf. */
			zs_rec->zs_nameoff = 0;
			if (pathbuflen && zs_rec->zs_nameerr == 0) {
				int newlen = strlen(pathbuf) + 1;
				if ((uintptr_t)zs_rec >=
				    (uintptr_t)strbuf -
				    (newlen + sizeof (zfs_stat_t))) {
					/* Name plus zs_rec will not fit */
					break;
				}
				strbuf = (void *)((uintptr_t)strbuf - newlen);
				(void) memcpy(strbuf, pathbuf, newlen);
				zs_rec->zs_nameoff = (uint64_t)
				    ((uintptr_t)strbuf -
				    (uintptr_t)zs_buf);
			}

			/*
			 * Successfully retrieved the stats+path, so record the
			 * object ID in the record and move to next record.
			 */
			zs_rec->zs_obj = zc->zc_fromobj;
			zs_rec++;
		}
	}
	if (error == ESRCH) {
		zc->zc_fromobj = DN_MAX_OBJECT;
		error = 0;
	}
	ASSERT((uintptr_t)zs_buf <= (uintptr_t)zs_rec &&
	    (uintptr_t)zs_rec <= (uintptr_t)strbuf &&
	    (uintptr_t)strbuf <= (uintptr_t)endbuf);

	if (!error && (zs_rec != zs_buf)) {
		zc->zc_cookie = zs_rec - zs_buf; /* Number of records */

		if (ddi_copyout(zs_buf, (void *)(uintptr_t)zc->zc_stat_buf,
		    zc->zc_cookie * sizeof (zfs_stat_t),
		    zc->zc_iflags) != 0)
			error = EFAULT;

		len = (uintptr_t)endbuf - (uintptr_t)strbuf;
		if (!error && len != 0 &&
		    (ddi_copyout(strbuf, (void *)((uintptr_t)zc->zc_stat_buf +
		    buflen - len), len, zc->zc_iflags) != 0))
		error = EFAULT;
	}
	kmem_free(zs_buf, buflen);
	zfsvfs_rele(zfsvfs, FTAG);

	return (error);
}

static int
zfs_ioc_vdev_add(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	nvlist_t *config, **l2cache, **spares;
	uint_t nl2cache = 0, nspares = 0;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config);
	(void) nvlist_lookup_nvlist_array(config, ZPOOL_CONFIG_L2CACHE,
	    &l2cache, &nl2cache);

	(void) nvlist_lookup_nvlist_array(config, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares);

	/*
	 * A root pool with concatenated devices is not supported.
	 * Thus, can not add a device to a root pool.
	 *
	 * Intent log device can not be added to a rootpool because
	 * during mountroot, zil is replayed, a separated log device
	 * can not be accessed during the mountroot time.
	 *
	 * l2cache and spare devices are ok to be added to a rootpool.
	 */
	if (spa_bootfs(spa) != 0 && nl2cache == 0 && nspares == 0) {
		nvlist_free(config);
		spa_close(spa, FTAG);
		return (EDOM);
	}

	if (error == 0) {
		error = spa_vdev_add(spa, config);
		nvlist_free(config);
	}
	spa_close(spa, FTAG);
	return (error);
}

/*
 * inputs:
 * zc_name		name of the pool
 * zc_nvlist_conf	nvlist of devices to remove
 * zc_cookie		to stop the remove?
 */
static int
zfs_ioc_vdev_remove(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);
	error = spa_vdev_remove(spa, zc->zc_guid, B_FALSE);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_set_state(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	vdev_state_t newstate = VDEV_STATE_UNKNOWN;
	nvlist_t *policy;

	VERIFY(nvlist_alloc(&policy, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint32(policy, ZPOOL_LOAD_RETRY, 1) == 0);

	error = spa_open_policy(zc->zc_name, &spa, FTAG, policy, NULL);

	nvlist_free(policy);

	if (error != 0)
		return (error);

	if (!spa_writeable(spa)) {
		spa_close(spa, FTAG);
		return (EROFS);
	}

	switch (zc->zc_cookie) {
	case VDEV_STATE_ONLINE:
		error = vdev_online(spa, zc->zc_guid, zc->zc_obj, &newstate);
		break;

	case VDEV_STATE_OFFLINE:
		error = vdev_offline(spa, zc->zc_guid, zc->zc_obj);
		break;

	case VDEV_STATE_FAULTED:
		if (zc->zc_obj != VDEV_AUX_ERR_EXCEEDED &&
		    zc->zc_obj != VDEV_AUX_EXTERNAL)
			zc->zc_obj = VDEV_AUX_ERR_EXCEEDED;

		error = vdev_fault(spa, zc->zc_guid, zc->zc_obj);
		break;

	case VDEV_STATE_DEGRADED:
		if (zc->zc_obj != VDEV_AUX_ERR_EXCEEDED &&
		    zc->zc_obj != VDEV_AUX_EXTERNAL)
			zc->zc_obj = VDEV_AUX_ERR_EXCEEDED;

		error = vdev_degrade(spa, zc->zc_guid, zc->zc_obj);
		break;

	default:
		error = EINVAL;
	}
	zc->zc_cookie = newstate;
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_attach(zfs_cmd_t *zc)
{
	spa_t *spa;
	int replacing = zc->zc_cookie;
	nvlist_t *config;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)) == 0) {
		error = spa_vdev_attach(spa, zc->zc_guid, config, replacing);
		nvlist_free(config);
	}

	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_detach(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	error = spa_vdev_detach(spa, zc->zc_guid, 0, B_FALSE);

	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_split(zfs_cmd_t *zc)
{
	spa_t *spa;
	nvlist_t *config, *props = NULL;
	int error;
	boolean_t exp = !!(zc->zc_cookie & ZPOOL_EXPORT_AFTER_SPLIT);

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)) {
		spa_close(spa, FTAG);
		return (error);
	}

	if (zc->zc_nvlist_src_size != 0 && (error =
	    get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))) {
		spa_close(spa, FTAG);
		nvlist_free(config);
		return (error);
	}

	error = spa_vdev_split_mirror(spa, zc->zc_string, config, props, exp);

	spa_close(spa, FTAG);

	nvlist_free(config);
	nvlist_free(props);

	return (error);
}

static int
zfs_ioc_vdev_setpath(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *path = zc->zc_value;
	uint64_t guid = zc->zc_guid;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = spa_vdev_setpath(spa, guid, path);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_setfru(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *fru = zc->zc_value;
	uint64_t guid = zc->zc_guid;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = spa_vdev_setfru(spa, guid, fru);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_objset_stats_impl(zfs_cmd_t *zc, objset_t *os)
{
	int error = 0;
	nvlist_t *nv;

	dmu_objset_fast_stat(os, &zc->zc_objset_stats);

	if (zc->zc_nvlist_dst != 0 &&
	    (error = dsl_prop_get_all(os, &nv)) == 0) {
		dmu_objset_stats(os, nv);
		/*
		 * NB: zvol_get_stats() will read the objset contents,
		 * which we aren't supposed to do with a
		 * DS_MODE_USER hold, because it could be
		 * inconsistent.  So this is a bit of a workaround...
		 * XXX reading with out owning
		 */
		if (!zc->zc_objset_stats.dds_inconsistent) {
			if (dmu_objset_type(os) == DMU_OST_ZVOL)
				VERIFY(zvol_get_stats(os, nv) == 0);
		}
		error = put_nvlist(zc, nv);
		nvlist_free(nv);
	}

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_dst_size	size of buffer for property nvlist
 *
 * outputs:
 * zc_objset_stats	stats
 * zc_nvlist_dst	property nvlist
 * zc_nvlist_dst_size	size of property nvlist
 */
static int
zfs_ioc_objset_stats(zfs_cmd_t *zc)
{
	objset_t *os = NULL;
	int error;

	if (error = dmu_objset_hold(zc->zc_name, FTAG, &os))
		return (error);

	error = zfs_ioc_objset_stats_impl(zc, os);

	dmu_objset_rele(os, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_dst_size	size of buffer for property nvlist
 *
 * outputs:
 * zc_nvlist_dst	received property nvlist
 * zc_nvlist_dst_size	size of received property nvlist
 *
 * Gets received properties (distinct from local properties on or after
 * SPA_VERSION_RECVD_PROPS) for callers who want to differentiate received from
 * local property values.
 */
static int
zfs_ioc_objset_recvd_props(zfs_cmd_t *zc)
{
	objset_t *os = NULL;
	int error;
	nvlist_t *nv;

	if (error = dmu_objset_hold(zc->zc_name, FTAG, &os))
		return (error);

	/*
	 * Without this check, we would return local property values if the
	 * caller has not already received properties on or after
	 * SPA_VERSION_RECVD_PROPS.
	 */
	if (!dsl_prop_get_hasrecvd(os)) {
		dmu_objset_rele(os, FTAG);
		return (ENOTSUP);
	}

	if (zc->zc_nvlist_dst != 0 &&
	    (error = dsl_prop_get_received(os, &nv)) == 0) {
		error = put_nvlist(zc, nv);
		nvlist_free(nv);
	}

	dmu_objset_rele(os, FTAG);
	return (error);
}

static int
nvl_add_zplprop(objset_t *os, nvlist_t *props, zfs_prop_t prop)
{
	uint64_t value;
	int error;

	if (prop == ZFS_PROP_MLSLABEL) {
		char	mlslabel[MAXLABELSTR];
		const char	*pname = zfs_prop_to_name(prop);

		if (os == NULL) {
			(void) strcpy(mlslabel, ZFS_MLSLABEL_NONE);
			VERIFY(nvlist_add_string(props, pname, mlslabel) == 0);
			return (0);
		}

		error = zfs_get_mlslabel(os, mlslabel, sizeof (mlslabel));
		if (error)
			return (error);

		VERIFY(nvlist_add_string(props, pname, mlslabel) == 0);
		return (0);
	}

	/*
	 * zfs_get_zplprop() will either find a value or give us
	 * the default value (if there is one).
	 */
	if ((error = zfs_get_zplprop(os, prop, &value)) != 0)
		return (error);
	VERIFY(nvlist_add_uint64(props, zfs_prop_to_name(prop), value) == 0);
	return (0);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_dst_size	size of buffer for zpl property nvlist
 *
 * outputs:
 * zc_nvlist_dst	zpl property nvlist
 * zc_nvlist_dst_size	size of zpl property nvlist
 */
static int
zfs_ioc_objset_zplprops(zfs_cmd_t *zc)
{
	zfsvfs_t *zfsvfs;
	objset_t *os;
	int err;

	boolean_t mounted = (getzfsvfs(zc->zc_name, &zfsvfs) == 0);

	/*
	 * If the dataset is mounted, acquire the teardown lock
	 * before getting zpl properties in order to avoid race
	 * between this thread and unmount thread.
	 */
	if (mounted)
		rrw_enter(&zfsvfs->z_teardown_lock, RW_READER, FTAG);

	/* XXX reading without owning */
	if (err = dmu_objset_hold(zc->zc_name, FTAG, &os)) {
		if (mounted) {
			rrw_exit(&zfsvfs->z_teardown_lock, FTAG);
			VFS_RELE(zfsvfs->z_vfs);
		}
		return (err);
	}

	dmu_objset_fast_stat(os, &zc->zc_objset_stats);

	/*
	 * NB: nvl_add_zplprop() will read the objset contents,
	 * which we aren't supposed to do with a DS_MODE_USER
	 * hold, because it could be inconsistent.
	 */
	if (zc->zc_nvlist_dst != NULL &&
	    !zc->zc_objset_stats.dds_inconsistent &&
	    dmu_objset_type(os) == DMU_OST_ZFS) {
		nvlist_t *nv;

		VERIFY(nvlist_alloc(&nv, NV_UNIQUE_NAME, KM_SLEEP) == 0);
		if ((err = nvl_add_zplprop(os, nv, ZFS_PROP_VERSION)) == 0 &&
		    (err = nvl_add_zplprop(os, nv, ZFS_PROP_NORMALIZE)) == 0 &&
		    (err = nvl_add_zplprop(os, nv, ZFS_PROP_UTF8ONLY)) == 0 &&
		    (err = nvl_add_zplprop(os, nv, ZFS_PROP_CASE)) == 0 &&
		    (err = nvl_add_zplprop(os, nv, ZFS_PROP_MLSLABEL)) == 0)
			err = put_nvlist(zc, nv);
		nvlist_free(nv);
	} else {
		err = ENOENT;
	}
	dmu_objset_rele(os, FTAG);

	if (mounted) {
		rrw_exit(&zfsvfs->z_teardown_lock, FTAG);
		VFS_RELE(zfsvfs->z_vfs);
	}
	return (err);
}

static boolean_t
dataset_name_hidden(zfs_cmd_t *zc)
{
	/*
	 * Skip over datasets that are not visible in this zone,
	 * internal datasets (which have a $ in their name), and
	 * temporary datasets (which have a % in their name).
	 */
	if (strchr(zc->zc_name, '$') != NULL)
		return (B_TRUE);
	if (strchr(zc->zc_name, '%') != NULL)
		return (B_TRUE);
	if (!zone_dataset_visible(crgetzone((cred_t *)(uintptr_t)zc->zc_cred),
	    zc->zc_name, NULL))
		return (B_TRUE);
	return (B_FALSE);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_cookie		zap cursor
 * zc_nvlist_dst_size	size of buffer for property nvlist
 * zc_cred		cred pointer
 *
 * outputs:
 * zc_name		name of next filesystem
 * zc_cookie		zap cursor
 * zc_objset_stats	stats
 * zc_nvlist_dst	property nvlist
 * zc_nvlist_dst_size	size of property nvlist
 */
static int
zfs_ioc_dataset_list_next(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;
	char *p;
	size_t orig_len = strlen(zc->zc_name);

top:
	if (error = dmu_objset_hold(zc->zc_name, FTAG, &os)) {
		if (error == ENOENT)
			error = ESRCH;
		return (error);
	}

	p = strrchr(zc->zc_name, '/');
	if (p == NULL || p[1] != '\0')
		(void) strlcat(zc->zc_name, "/", sizeof (zc->zc_name));
	p = zc->zc_name + strlen(zc->zc_name);

	/*
	 * Pre-fetch the datasets.  dmu_objset_prefetch() always returns 0
	 * but is not declared void because its called by dmu_objset_find().
	 */
	if (zc->zc_cookie == 0) {
		uint64_t cookie = 0;
		int len = sizeof (zc->zc_name) - (p - zc->zc_name);

		while (dmu_dir_list_next(os, len, p, NULL, &cookie) == 0)
			(void) dmu_objset_prefetch(p, NULL);
	}

	do {
		error = dmu_dir_list_next(os,
		    sizeof (zc->zc_name) - (p - zc->zc_name), p,
		    NULL, &zc->zc_cookie);
		if (error == ENOENT)
			error = ESRCH;
	} while (error == 0 && dataset_name_hidden(zc));

	dmu_objset_rele(os, FTAG);

	/*
	 * If it's an internal dataset (ie. with a '$' in its name),
	 * don't try to get stats for it, otherwise we'll return ENOENT.
	 */
	if (error == 0 && strchr(zc->zc_name, '$') == NULL) {
		error = zfs_ioc_objset_stats(zc); /* fill in the stats */
		if (error == ENOENT) {
			/* We lost a race with destroy, get the next one. */
			zc->zc_name[orig_len] = '\0';
			goto top;
		}
	}
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_cookie		zap cursor
 * zc_nvlist_dst_size	size of buffer for property nvlist
 *
 * outputs:
 * zc_name		name of next snapshot
 * zc_objset_stats	stats
 * zc_nvlist_dst	property nvlist
 * zc_nvlist_dst_size	size of property nvlist
 */
static int
zfs_ioc_snapshot_list_next(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;

top:
	if (zc->zc_cookie == 0)
		(void) dmu_objset_find(zc->zc_name, dmu_objset_prefetch,
		    NULL, DS_FIND_SNAPSHOTS);

	error = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (error)
		return (error == ENOENT ? ESRCH : error);

	/*
	 * A dataset name of maximum length cannot have any snapshots,
	 * so exit immediately.
	 */
	if (strlcat(zc->zc_name, "@", sizeof (zc->zc_name)) >= MAXNAMELEN) {
		dmu_objset_rele(os, FTAG);
		return (ESRCH);
	}

	error = dmu_snapshot_list_next(os,
	    sizeof (zc->zc_name) - strlen(zc->zc_name),
	    zc->zc_name + strlen(zc->zc_name), &zc->zc_obj, &zc->zc_cookie,
	    NULL);

	if (error == 0) {
		dsl_dataset_t *ds;
		dsl_pool_t *dp = os->os_dsl_dataset->ds_dir->dd_pool;

		/*
		 * Since we probably don't have a hold on this snapshot,
		 * it's possible that the objsetid could have been destroyed
		 * and reused for a new objset. It's OK if this happens during
		 * a zfs send operation, since the new createtxg will be
		 * beyond the range we're interested in.
		 */
		rw_enter(&dp->dp_config_rwlock, RW_READER);
		error = dsl_dataset_hold_obj(dp, zc->zc_obj, FTAG, &ds);
		rw_exit(&dp->dp_config_rwlock);
		if (error) {
			if (error == ENOENT) {
				/* Racing with destroy, get the next one. */
				*strchr(zc->zc_name, '@') = '\0';
				dmu_objset_rele(os, FTAG);
				goto top;
			}
		} else {
			objset_t *ossnap;

			error = dmu_objset_from_ds(ds, &ossnap);
			if (error == 0)
				error = zfs_ioc_objset_stats_impl(zc, ossnap);
			dsl_dataset_rele(ds, FTAG);
		}
	} else if (error == ENOENT) {
		error = ESRCH;
	}

	dmu_objset_rele(os, FTAG);
	/* if we failed, undo the @ that we tacked on to zc_name */
	if (error)
		*strchr(zc->zc_name, '@') = '\0';
	return (error);
}

static int
zfs_prop_set_userquota(const char *dsname, nvpair_t *pair)
{
	const char *propname = nvpair_name(pair);
	uint64_t *valary;
	unsigned int vallen;
	const char *domain;
	char *dash;
	zfs_userquota_prop_t type;
	uint64_t rid;
	uint64_t quota;
	zfsvfs_t *zfsvfs;
	int err;

	if ((err = dsl_prop_decode_value(&pair)) != 0)
		return (err);

	/*
	 * A correctly constructed propname is encoded as
	 * userquota@<rid>-<domain>.
	 */
	if ((dash = strchr(propname, '-')) == NULL ||
	    nvpair_value_uint64_array(pair, &valary, &vallen) != 0 ||
	    vallen != 3)
		return (EINVAL);

	domain = dash + 1;
	type = valary[0];
	rid = valary[1];
	quota = valary[2];

	err = zfsvfs_hold(dsname, FTAG, &zfsvfs, B_FALSE);
	if (err == 0) {
		err = zfs_set_userquota(zfsvfs, type, domain, rid, quota);
		zfsvfs_rele(zfsvfs, FTAG);
	}

	return (err);
}

/*
 * If the named property is one that has a special function to set its value,
 * return 0 on success and a positive error code on failure; otherwise if it is
 * not one of the special properties handled by this function, return -1.
 *
 * XXX: It would be better for callers of the property interface if we handled
 * these special cases in dsl_prop.c (in the dsl layer).
 */
static int
zfs_prop_set_special(const char *dsname, zprop_source_t source,
    nvpair_t *pair, zprop_setflags_t flags)
{
	const char *propname = nvpair_name(pair);
	zfs_prop_t prop = zfs_name_to_prop(propname);
	uint64_t intval;
	char *strval;
	int err;

	if (prop == ZPROP_INVAL) {
		if (zfs_prop_userquota(propname))
			return (zfs_prop_set_userquota(dsname, pair));
		return (-1);
	}

	dsl_prop_decode_flags(pair, &flags);
	VERIFY(0 == dsl_prop_decode_value(&pair));

	if (zfs_prop_get_type(prop) == PROP_TYPE_STRING)
		VERIFY(0 == nvpair_value_string(pair, &strval));
	else if (nvpair_type(pair) == DATA_TYPE_BOOLEAN)
		intval = 0;
	else
		VERIFY(0 == nvpair_value_uint64(pair, &intval));

	if ((source == ZPROP_SRC_LOCAL) &&
	    (flags & ZPROP_SET_DESCENDANT) && (intval != 0)) {
		/*
		 * Non-default size properties already apply to the entire
		 * subtree, so it doesn't make sense to set them recursively.
		 */
		switch (prop) {
		case ZFS_PROP_QUOTA:
		case ZFS_PROP_REFQUOTA:
		case ZFS_PROP_RESERVATION:
		case ZFS_PROP_REFRESERVATION:
			return (0); /* do nothing */
		}
	}

	switch (prop) {
	case ZFS_PROP_QUOTA:
		err = dsl_dir_set_quota(dsname, source, flags, intval);
		break;
	case ZFS_PROP_REFQUOTA:
		err = dsl_dataset_set_quota(dsname, source, flags, intval);
		break;
	case ZFS_PROP_RESERVATION:
		err = dsl_dir_set_reservation(dsname, source, flags, intval);
		break;
	case ZFS_PROP_REFRESERVATION:
		err = dsl_dataset_set_reservation(dsname, source, flags,
		    intval);
		break;
	case ZFS_PROP_VOLSIZE:
		err = zvol_set_volsize(dsname, ddi_driver_major(zfs_dip),
		    intval);
		break;
	case ZFS_PROP_VERSION:
	{
		zfsvfs_t *zfsvfs;

		if ((err = zfsvfs_hold(dsname, FTAG, &zfsvfs, B_TRUE)) != 0)
			break;

		err = zfs_set_version(zfsvfs, intval);
		zfsvfs_rele(zfsvfs, FTAG);

		if (err == 0 && intval >= ZPL_VERSION_USERSPACE) {
			zfs_cmd_t *zc;

			zc = kmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);
			(void) strcpy(zc->zc_name, dsname);
			(void) zfs_ioc_userspace_upgrade(zc);
			kmem_free(zc, sizeof (zfs_cmd_t));
		}
		break;
	}
	case ZFS_PROP_MLSLABEL:
		err = zfs_set_mlslabel(dsname, strval);
		break;
	default:
		err = -1;
	}

	return (err);
}

/*
 * Get properties that cannot be restored simply by restoring the raw contents
 * of the ZAP. Quota and reservation were not stored in the ZAP before
 * SPA_VERSION_RECVD_PROPS, and although refquota and refreservation were always
 * stored in the ZAP, all of these properties require extra processing at set
 * time. This extra processing is only necessary on the effective value cached
 * in the dataset. If the special property has both a local and a received
 * value, special processing is only needed for whichever value is effective.
 */
static int
zfs_prop_save_special(objset_t *os, nvlist_t **special)
{
	const char *names[] = { "quota", "reservation", "refquota",
		"refreservation", NULL };

	dsl_dataset_t *ds = os->os_dsl_dataset;
	char setpoint[MAXNAMELEN];
	nvlist_t *attrs;
	nvlist_t *stats;
	int err = 0;
	int i;

	VERIFY(nvlist_alloc(special, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_alloc(&attrs, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_alloc(&stats, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	dsl_dataset_stats(ds, stats);

	rw_enter(&DS_POOL(ds)->dp_config_rwlock, RW_READER);

	for (i = 0; names[i] != NULL; i++) {
		uint64_t intval;
		zprop_source_t source;

		err = dsl_prop_get_ds(ds, names[i], 8, 1, &intval, setpoint,
		    DSL_PROP_GET_EFFECTIVE);

		if (err != 0)
			break;

		if (setpoint[0] == '\0') {
			nvpair_t *propval;

			source = ZPROP_SRC_NONE;

			/*
			 * See if the property is set outside the ZAP.
			 */
			err = nvlist_lookup_nvpair(stats, names[i], &propval);
			if (err == 0) {
				err = dsl_prop_decode_value(&propval);
				if (err == 0) {
					err = nvpair_value_uint64(propval,
					    &intval);
					if (err == 0 && intval != 0)
						source = ZPROP_SRC_LOCAL;
				}
			}
		} else if (strcmp(setpoint, ZPROP_SOURCE_VAL_RECVD) == 0) {
			source = ZPROP_SRC_RECEIVED;
		} else {
			/*
			 * These special properties are not inheritable, so the
			 * setpoint must be local.
			 */
			source = ZPROP_SRC_LOCAL;
		}

		nvlist_clear(attrs);
		VERIFY(nvlist_add_int32(attrs, ZPROP_SOURCE, source) == 0);
		VERIFY(nvlist_add_uint64(attrs, ZPROP_VALUE, intval) == 0);
		VERIFY(nvlist_add_nvlist(*special, names[i], attrs) == 0);
	}

	rw_exit(&DS_POOL(ds)->dp_config_rwlock);

	nvlist_free(attrs);
	nvlist_free(stats);

	if (err != 0)
		nvlist_free(*special);

	return (err);
}

/*
 * Can only be called with a list obtained from zfs_prop_save_special().
 */
static int
zfs_prop_restore_special(objset_t *os, nvlist_t *special)
{
	char dsname[MAXNAMELEN];
	nvpair_t *pair = NULL;
	int err = 0;

	dsl_dataset_name(os->os_dsl_dataset, dsname);

	while ((pair = nvlist_next_nvpair(special, pair)) != NULL) {
		zprop_source_t source;
		nvlist_t *attrs;

		VERIFY(0 == nvpair_value_nvlist(pair, &attrs));
		VERIFY(0 == nvlist_lookup_int32(attrs, ZPROP_SOURCE,
		    (int32_t *)&source));

		err |= zfs_prop_set_special(dsname, source, pair, 0);
	}

	return (err);
}

/*
 * Handle user quota before it reaches dsl_prop.c, where it is invalid.
 */
static int
zfs_prop_validate(nvpair_t *pair, zprop_source_t source)
{
	const char *propname = nvpair_name(pair);
	nvpair_t *propval = pair;
	int err;

	if ((err = dsl_prop_decode_value(&propval)) != 0)
		return (err);

	if (zfs_prop_userquota(propname)) {
		if (strlen(propname) >= ZAP_MAXNAMELEN)
			return (ENAMETOOLONG);
		if (nvpair_type(propval) != DATA_TYPE_UINT64_ARRAY)
			return (EINVAL);
		if (source == ZPROP_SRC_INHERITED)
			return (EINVAL);
		return (0);
	}

	return (dsl_prop_validate_nvpair(pair, source));
}

/*
 * This function is best effort. If it fails to set any of the given properties,
 * it continues to set as many as it can and returns the first error
 * encountered. If the caller provides a non-NULL errlist, it also gives the
 * complete list of names of all the properties it failed to set along with the
 * corresponding error numbers. The caller is responsible for freeing the
 * returned errlist.
 *
 * If every property is set successfully, zero is returned and the list pointed
 * at by errlist is NULL.
 */
int
zfs_set_prop_nvlist(const char *dsname, zprop_source_t source, nvlist_t *nvl,
    nvlist_t **errlist, const zprop_setflags_t flags)
{
	nvpair_t *pair;
	int rv = 0;
	uint64_t intval;
	char *strval;
	nvlist_t *genericnvl;
	nvlist_t *errors;
	nvlist_t *retrynvl;

	ASSERT(!(flags & ZPROP_SET_RECEIVED)); /* use source parameter */

	VERIFY(nvlist_alloc(&genericnvl, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_alloc(&errors, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_alloc(&retrynvl, NV_UNIQUE_NAME, KM_SLEEP) == 0);

retry:
	pair = NULL;
	while ((pair = nvlist_next_nvpair(nvl, pair)) != NULL) {
		const char *propname = nvpair_name(pair);
		int err = zfs_prop_validate(pair, source);

		/* validate permissions */
		if (err == 0)
			err = zfs_check_settable(dsname, pair, CRED());

		if (err == 0) {
			err = zfs_prop_set_special(dsname, source, pair, flags);
			if (err == -1) {
				/*
				 * For better performance we build up a list of
				 * properties to set in a single transaction.
				 */
				err = nvlist_add_nvpair(genericnvl, pair);
			} else if (err != 0 && nvl != retrynvl) {
				/*
				 * This may be a spurious error caused by
				 * receiving quota and reservation out of order.
				 * Try again in a second pass.
				 */
				err = nvlist_add_nvpair(retrynvl, pair);
			}
		}

		if (err != 0)
			VERIFY(nvlist_add_int32(errors, propname, err) == 0);
	}

	if (nvl != retrynvl && !nvlist_empty(retrynvl)) {
		nvl = retrynvl;
		goto retry;
	}

	if (!nvlist_empty(genericnvl) &&
	    dsl_props_set(dsname, source, genericnvl, flags) != 0) {
		/*
		 * If this fails, we still want to set as many properties as we
		 * can, so try setting them individually.
		 */
		pair = NULL;
		while ((pair = nvlist_next_nvpair(genericnvl, pair)) != NULL) {
			const char *propname = nvpair_name(pair);
			nvpair_t *propval = pair;
			zprop_setflags_t setflags = flags;
			int err = 0;

			dsl_prop_decode_flags(pair, &setflags);
			VERIFY(0 == dsl_prop_decode_value(&propval));

			if (nvpair_type(propval) == DATA_TYPE_BOOLEAN) {
				err = dsl_prop_set(dsname, propname, source, 0,
				    0, NULL, setflags);
			} else if (nvpair_type(propval) == DATA_TYPE_STRING) {
				VERIFY(nvpair_value_string(propval,
				    &strval) == 0);
				err = dsl_prop_set(dsname, propname, source, 1,
				    strlen(strval) + 1, strval, setflags);
			} else {
				VERIFY(nvpair_value_uint64(propval,
				    &intval) == 0);
				err = dsl_prop_set(dsname, propname, source, 8,
				    1, &intval, setflags);
			}

			if (err != 0) {
				VERIFY(nvlist_add_int32(errors, propname,
				    err) == 0);
			}
		}
	}
	nvlist_free(genericnvl);
	nvlist_free(retrynvl);

	if ((pair = nvlist_next_nvpair(errors, NULL)) == NULL) {
		nvlist_free(errors);
		errors = NULL;
	} else {
		VERIFY(nvpair_value_int32(pair, &rv) == 0);
	}

	if (errlist == NULL)
		nvlist_free(errors);
	else
		*errlist = errors;

	return (rv);
}

/*
 * Check that all the properties are valid user properties.
 */
static int
zfs_check_userprops(char *fsname, nvlist_t *nvl)
{
	nvpair_t *pair = NULL;
	int error = 0;

	while ((pair = nvlist_next_nvpair(nvl, pair)) != NULL) {
		const char *propname = nvpair_name(pair);
		char *valstr;

		if (!zfs_prop_user(propname) ||
		    nvpair_type(pair) != DATA_TYPE_STRING)
			return (EINVAL);

		if (error = zfs_secpolicy_write_perms(fsname,
		    ZFS_DELEG_PERM_USERPROP, CRED()))
			return (error);

		if (strlen(propname) >= ZAP_MAXNAMELEN)
			return (ENAMETOOLONG);

		VERIFY(nvpair_value_string(pair, &valstr) == 0);
		if (strlen(valstr) >= ZAP_MAXVALUELEN)
			return (E2BIG);
	}
	return (0);
}

static void
props_skip(nvlist_t *props, nvlist_t *skipped, nvlist_t **newprops)
{
	nvpair_t *pair;

	VERIFY(nvlist_alloc(newprops, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	pair = NULL;
	while ((pair = nvlist_next_nvpair(props, pair)) != NULL) {
		if (nvlist_exists(skipped, nvpair_name(pair)))
			continue;

		VERIFY(nvlist_add_nvpair(*newprops, pair) == 0);
	}
}

static int
clear_received_props(objset_t *os, const char *fs, nvlist_t *props,
    nvlist_t *skipped)
{
	int err = 0;
	nvlist_t *cleared_props = NULL;
	props_skip(props, skipped, &cleared_props);
	if (!nvlist_empty(cleared_props)) {
		/*
		 * Acts on local properties until the dataset has received
		 * properties at least once on or after SPA_VERSION_RECVD_PROPS.
		 */
		zprop_source_t flags = (ZPROP_SRC_NONE |
		    (dsl_prop_get_hasrecvd(os) ? ZPROP_SRC_RECEIVED : 0));
		err = zfs_set_prop_nvlist(fs, flags, cleared_props, NULL, 0);
	}
	nvlist_free(cleared_props);
	return (err);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_src{_size}	nvlist of properties to apply
 * zc_cookie		zprop_setflags_t
 *
 * outputs:
 * zc_nvlist_dst{_size} error for each unapplied received property
 */
static int
zfs_ioc_set_prop(zfs_cmd_t *zc)
{
	nvlist_t *nvl;
	zprop_source_t source;
	nvlist_t *errors = NULL;
	zprop_setflags_t flags;
	int error;

	source = (zc->zc_cookie & ZPROP_SET_RECEIVED ?
	    ZPROP_SRC_RECEIVED : ZPROP_SRC_LOCAL);

	/* done handling ZPROP_SET_RECEIVED */
	flags = ((zprop_setflags_t)zc->zc_cookie & ~ZPROP_SET_RECEIVED);

	if ((error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &nvl)) != 0)
		return (error);

	if (source == ZPROP_SRC_RECEIVED) {
		objset_t *os;

		if (dmu_objset_hold(zc->zc_name, FTAG, &os) == 0) {
			nvlist_t *origprops;
			if (dsl_prop_get_received(os, &origprops) == 0) {
				(void) clear_received_props(os,
				    zc->zc_name, origprops, nvl);
				nvlist_free(origprops);
			}

			dsl_prop_set_hasrecvd(os);
			dmu_objset_rele(os, FTAG);
		}
	}

	error = zfs_set_prop_nvlist(zc->zc_name, source, nvl, &errors, flags);

	if (zc->zc_nvlist_dst != NULL && errors != NULL) {
		(void) put_nvlist(zc, errors);
	}

	nvlist_free(errors);
	nvlist_free(nvl);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_value		name of property to inherit
 * zc_cookie		revert to received value if TRUE
 *
 * outputs:		none
 */
static int
zfs_ioc_inherit_prop(zfs_cmd_t *zc)
{
	const char *propname = zc->zc_value;
	zfs_prop_t prop = zfs_name_to_prop(propname);
	boolean_t received = zc->zc_cookie;
	zprop_source_t source = (received
	    ? ZPROP_SRC_NONE		/* revert to received value, if any */
	    : ZPROP_SRC_INHERITED);	/* explicitly inherit */

	if (received) {
		nvlist_t *dummy;
		nvpair_t *pair;
		zprop_type_t type;
		int err;

		/*
		 * zfs_prop_set_special() expects properties in the form of an
		 * nvpair with type info.
		 */
		if (prop == ZPROP_INVAL) {
			if (!zfs_prop_user(propname))
				return (EINVAL);

			type = PROP_TYPE_STRING;
		} else if (prop == ZFS_PROP_VOLSIZE ||
		    prop == ZFS_PROP_VERSION) {
			return (EINVAL);
		} else {
			type = zfs_prop_get_type(prop);
		}

		VERIFY(nvlist_alloc(&dummy, NV_UNIQUE_NAME, KM_SLEEP) == 0);

		switch (type) {
		case PROP_TYPE_STRING:
			VERIFY(0 == nvlist_add_string(dummy, propname, ""));
			break;
		case PROP_TYPE_NUMBER:
		case PROP_TYPE_INDEX:
			VERIFY(0 == nvlist_add_uint64(dummy, propname, 0));
			break;
		default:
			nvlist_free(dummy);
			return (EINVAL);
		}

		pair = nvlist_next_nvpair(dummy, NULL);
		err = zfs_prop_set_special(zc->zc_name, source, pair, 0);
		nvlist_free(dummy);
		if (err != -1)
			return (err); /* special property already handled */
	} else {
		/*
		 * Only check this in the non-received case. We want to allow
		 * 'inherit -S' to revert non-inheritable properties like quota
		 * and reservation to the received or default values even though
		 * they are not considered inheritable.
		 */
		if (prop != ZPROP_INVAL && !zfs_prop_inheritable(prop))
			return (EINVAL);
	}

	/* the property name has been validated by zfs_secpolicy_inherit() */
	return (dsl_prop_set(zc->zc_name, zc->zc_value, source, 0, 0, NULL, 0));
}

static int
zfs_ioc_pool_set_props(zfs_cmd_t *zc)
{
	nvlist_t *props;
	spa_t *spa;
	int error;
	nvpair_t *pair;

	if (error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))
		return (error);

	/*
	 * If the only property is the configfile, then just do a spa_lookup()
	 * to handle the faulted case.
	 */
	pair = nvlist_next_nvpair(props, NULL);
	if (pair != NULL && strcmp(nvpair_name(pair),
	    zpool_prop_to_name(ZPOOL_PROP_CACHEFILE)) == 0 &&
	    nvlist_next_nvpair(props, pair) == NULL) {
		mutex_enter(&spa_namespace_lock);
		if ((spa = spa_lookup(zc->zc_name)) != NULL) {
			spa_configfile_set(spa, props, B_FALSE);
			spa_config_sync(spa, B_FALSE, B_TRUE);
		}
		mutex_exit(&spa_namespace_lock);
		if (spa != NULL) {
			nvlist_free(props);
			return (0);
		}
	}

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0) {
		nvlist_free(props);
		return (error);
	}

	error = spa_prop_set(spa, props);

	nvlist_free(props);
	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_pool_get_props(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	nvlist_t *nvp = NULL;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0) {
		/*
		 * If the pool is faulted, there may be properties we can still
		 * get (such as altroot and cachefile), so attempt to get them
		 * anyway.
		 */
		mutex_enter(&spa_namespace_lock);
		if ((spa = spa_lookup(zc->zc_name)) != NULL)
			error = spa_prop_get(spa, &nvp);
		mutex_exit(&spa_namespace_lock);
	} else {
		error = spa_prop_get(spa, &nvp);
		spa_close(spa, FTAG);
	}

	if (error == 0 && zc->zc_nvlist_dst != NULL) {
		zone_t *zone = crgetzone((cred_t *)zc->zc_cred);
		if (zone != global_zone) {
			char aliased[MAXPATHLEN];
			nvlist_t *namenv;

			(void) zone_dataset_alias(zone, zc->zc_name, aliased,
			    sizeof (aliased));
			if (nvlist_lookup_nvlist(nvp, "name", &namenv) == 0 &&
			    nvlist_add_string(namenv, "value", aliased) == 0) {
				error = put_nvlist(zc, nvp);
			} else {
				error = EFAULT;
			}
		} else {
			error = put_nvlist(zc, nvp);
		}
	} else {
		error = EFAULT;
	}

	nvlist_free(nvp);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_src{_size}	nvlist of delegated permissions
 * zc_perm_action	allow/unallow flag
 *
 * outputs:		none
 */
static int
zfs_ioc_set_fsacl(zfs_cmd_t *zc)
{
	int error;
	nvlist_t *fsaclnv = NULL;

	if ((error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &fsaclnv)) != 0)
		return (error);

	/*
	 * Verify nvlist is constructed correctly
	 */
	if ((error = zfs_deleg_verify_nvlist(fsaclnv)) != 0) {
		nvlist_free(fsaclnv);
		return (EINVAL);
	}

	/*
	 * If we don't have PRIV_SYS_MOUNT, then validate
	 * that user is allowed to hand out each permission in
	 * the nvlist(s)
	 */

	error = secpolicy_zfs(CRED());
	if (error) {
		if (zc->zc_perm_action == B_FALSE) {
			error = dsl_deleg_can_allow(zc->zc_name,
			    fsaclnv, CRED());
		} else {
			error = dsl_deleg_can_unallow(zc->zc_name,
			    fsaclnv, CRED());
		}
	}

	if (error == 0)
		error = dsl_deleg_set(zc->zc_name, fsaclnv, zc->zc_perm_action);

	nvlist_free(fsaclnv);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 *
 * outputs:
 * zc_nvlist_src{_size}	nvlist of delegated permissions
 */
static int
zfs_ioc_get_fsacl(zfs_cmd_t *zc)
{
	nvlist_t *nvp;
	int error;

	if ((error = dsl_deleg_get(zc->zc_name, &nvp)) == 0) {
		error = put_nvlist(zc, nvp);
		nvlist_free(nvp);
	}

	return (error);
}

/*
 * inputs:
 * zc_name		Name of filesystem (checked by zfsdev_ioctl)
 * zc_guid		Value to use for fixed FSID, if ZFS_FSID_PARAM is set
 * zc_cookie		Flags:
 *				ZFS_FSID_CREATE
 *					TRUE  = Create a fixed FSID
 *					FALSE = Modify existing fixed FSID
 *				ZFS_FSID_PARAM
 * 					TRUE  = Use zc_guid for FSID value
 *					FALSE = Use system generated value
 *
 * outputs:
 * return code: 0 on success, else error
 */
static int
zfs_ioc_set_fsid(zfs_cmd_t *zc)
{
	zfsvfs_t *zfsvfsp;
	uint64_t fsid = 0;
	uint64_t flags = 0;
	int error;

	flags = ((zfs_fsid_flag_t)zc->zc_cookie & ZFS_FSID_ALL);
	fsid = (flags & ZFS_FSID_PARAM) ? zc->zc_guid : 0;
	error = zfsvfs_hold(zc->zc_name, FTAG, &zfsvfsp, B_FALSE);
	if (error == 0) {
		error = zfs_set_fixed_fsid(zfsvfsp, fsid, flags);
		zfsvfs_rele(zfsvfsp, FTAG);
	}
	return (error);
}

/*
 * inputs:
 * zc_name		Name of filesystem (checked by zfsdev_ioctl)
 *
 * outputs:
 * zc_guid		On success, value retrieved for fixed FSID
 *
 * return code: 0 on success, else error
 */
static int
zfs_ioc_get_fsid(zfs_cmd_t *zc)
{
	zfsvfs_t *zfsvfsp;
	int error;

	error = zfsvfs_hold(zc->zc_name, FTAG, &zfsvfsp, B_FALSE);
	if (error == 0) {
		error = zfs_get_fixed_fsid(zfsvfsp, &zc->zc_guid);
		zfsvfs_rele(zfsvfsp, FTAG);
	}

	return (error);
}

/*
 * Sets up the call to remove the fsid attribute from the master node.
 * If non-NULL, oldfsidp will point to the value of the fsid before it
 * was removed.
 */
static int
zfs_remove_fsid_impl(char *name, uint64_t *oldfsidp)
{
	zfsvfs_t *zfsvfsp;
	int error;

	error = zfsvfs_hold(name, FTAG, &zfsvfsp, B_FALSE);
	if (error == 0) {
		error = zfs_remove_fixed_fsid(zfsvfsp, oldfsidp);
		zfsvfs_rele(zfsvfsp, FTAG);
	}

	return (error);
}

/*
 * inputs:
 * zc_name		Name of filesystem (checked by zfsdev_ioctl)
 *
 * outputs:
 * zc_guid		On success, value retrieved for fixed FSID
 *
 * return code: 0 on success, else error
 */
static int
zfs_ioc_remove_fsid(zfs_cmd_t *zc)
{
	return (zfs_remove_fsid_impl(zc->zc_name, &zc->zc_guid));
}


#define	ZFS_PROP_UNDEFINED	((uint64_t)-1)

/*
 * inputs:
 * createprops		list of properties requested by creator
 * default_zplver	zpl version to use if unspecified in createprops
 * fuids_ok		fuids allowed in this version of the spa?
 * os			parent objset pointer (NULL if root fs)
 *
 * outputs:
 * zplprops	values for the zplprops we attach to the master node object
 * is_ci	true if requested file system will be purely case-insensitive
 *
 * Determine the settings for utf8only, normalization and
 * casesensitivity.  Specific values may have been requested by the
 * creator and/or we can inherit values from the parent dataset.  If
 * the file system is of too early a vintage, a creator can not
 * request settings for these properties, even if the requested
 * setting is the default value.  We don't actually want to create dsl
 * properties for these, so remove them from the source nvlist after
 * processing.
 */
static int
zfs_fill_zplprops_impl(objset_t *os, uint64_t zplver,
    boolean_t fuids_ok, boolean_t sa_ok, nvlist_t *createprops,
    nvlist_t *zplprops, boolean_t *is_ci)
{
	uint64_t sense = ZFS_PROP_UNDEFINED;
	uint64_t norm = ZFS_PROP_UNDEFINED;
	uint64_t u8 = ZFS_PROP_UNDEFINED;
	int error = 0;

	ASSERT(zplprops != NULL);

	/*
	 * Pull out creator prop choices, if any.
	 */
	if (createprops) {
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_VERSION), &zplver);
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_NORMALIZE), &norm);
		(void) nvlist_remove_all(createprops,
		    zfs_prop_to_name(ZFS_PROP_NORMALIZE));
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_UTF8ONLY), &u8);
		(void) nvlist_remove_all(createprops,
		    zfs_prop_to_name(ZFS_PROP_UTF8ONLY));
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_CASE), &sense);
		(void) nvlist_remove_all(createprops,
		    zfs_prop_to_name(ZFS_PROP_CASE));
	}

	/*
	 * If the zpl version requested is whacky or the file system
	 * or pool is version is too "young" to support normalization
	 * and the creator tried to set a value for one of the props,
	 * error out.
	 */
	if ((zplver < ZPL_VERSION_INITIAL || zplver > ZPL_VERSION) ||
	    (zplver >= ZPL_VERSION_FUID && !fuids_ok) ||
	    (zplver >= ZPL_VERSION_SA && !sa_ok) ||
	    (os != NULL && os->os_crypt != ZIO_CRYPT_OFF &&
	    zplver < ZPL_VERSION_SA) ||
	    (zplver < ZPL_VERSION_NORMALIZATION &&
	    (norm != ZFS_PROP_UNDEFINED || u8 != ZFS_PROP_UNDEFINED ||
	    sense != ZFS_PROP_UNDEFINED)))
		return (ENOTSUP);

	/*
	 * Put the version in the zplprops
	 */
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_VERSION), zplver) == 0);

	if (norm == ZFS_PROP_UNDEFINED &&
	    (error = zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &norm)) != 0) {
		return (error);
	}
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_NORMALIZE), norm) == 0);

	/*
	 * If we're normalizing, names must always be valid UTF-8 strings.
	 */
	if (norm)
		u8 = 1;
	if (u8 == ZFS_PROP_UNDEFINED &&
	    (error = zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &u8)) != 0) {
		return (error);
	}
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_UTF8ONLY), u8) == 0);

	if (sense == ZFS_PROP_UNDEFINED &&
	    (error = zfs_get_zplprop(os, ZFS_PROP_CASE, &sense)) != 0) {
		return (error);
	}
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_CASE), sense) == 0);

	if (is_ci)
		*is_ci = (sense == ZFS_CASE_INSENSITIVE);

	return (0);
}

static int
zfs_fill_zplprops(const char *dataset, nvlist_t *createprops,
    nvlist_t *zplprops, boolean_t *is_ci)
{
	boolean_t fuids_ok, sa_ok;
	uint64_t zplver = ZPL_VERSION;
	objset_t *os = NULL;
	char parentname[MAXNAMELEN];
	char *cp;
	spa_t *spa;
	uint64_t spa_vers;
	int error;

	(void) strlcpy(parentname, dataset, sizeof (parentname));
	cp = strrchr(parentname, '/');
	ASSERT(cp != NULL);
	cp[0] = '\0';

	if ((error = spa_open(dataset, &spa, FTAG)) != 0)
		return (error);

	spa_vers = spa_version(spa);
	spa_close(spa, FTAG);

	zplver = zfs_zpl_version_map(spa_vers);
	fuids_ok = (zplver >= ZPL_VERSION_FUID);
	sa_ok = (zplver >= ZPL_VERSION_SA);

	/*
	 * Open parent object set so we can inherit zplprop values.
	 */
	if ((error = dmu_objset_hold(parentname, FTAG, &os)) != 0)
		return (error);

	error = zfs_fill_zplprops_impl(os, zplver, fuids_ok, sa_ok, createprops,
	    zplprops, is_ci);
	dmu_objset_rele(os, FTAG);
	return (error);
}

static int
zfs_fill_zplprops_root(uint64_t spa_vers, nvlist_t *createprops,
    nvlist_t *zplprops)
{
	boolean_t fuids_ok;
	boolean_t sa_ok;
	uint64_t zplver = ZPL_VERSION;
	int error;

	zplver = zfs_zpl_version_map(spa_vers);
	fuids_ok = (zplver >= ZPL_VERSION_FUID);
	sa_ok = (zplver >= ZPL_VERSION_SA);

	error = zfs_fill_zplprops_impl(NULL, zplver, fuids_ok, sa_ok,
	    createprops, zplprops, NULL);
	return (error);
}

static int
zvol_fill_props(nvlist_t *nvprops, nvlist_t *cbprops)
{
	int error;
	uint64_t volsize, volblocksize;

	if (nvprops == NULL || nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE), &volsize) != 0) {
		return (EINVAL);
	}

	if ((error = nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
	    &volblocksize)) != 0 && error != ENOENT) {
		return (EINVAL);
	}

	if (error != 0)
		volblocksize = zfs_prop_default_numeric(
		    ZFS_PROP_VOLBLOCKSIZE);

	error = nvlist_add_uint64(cbprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE), volblocksize);
	if (error != 0)
		return (error);
	(void) nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE));

	error = nvlist_add_uint64(cbprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE), volsize);

	(void) nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE));

	return (error);
}

static int
zfs_get_crypto_ctx(zfs_cmd_t *zc, dsl_crypto_ctx_t *dcc)
{
	int error = 0;
	dsl_dataset_t *ids;
	zcrypt_key_t *wkey;

	if (zc->zc_crypto.zic_cmd != 0 &&
	    zfs_earlier_version(zc->zc_name, SPA_VERSION_CRYPTO))
		return (ENOTSUP);

	/*
	 * Special check aes-128-ctr which isn't settable from
	 * userland or at create time, it is only allowed to be
	 * set via the zvol_preallocate_init() path.
	 */

	if (zc->zc_crypto.zic_crypt == ZIO_CRYPT_AES_128_CTR) {
		return (ENOTSUP);
	}
	switch (zc->zc_crypto.zic_cmd) {
	case ZFS_IOC_CRYPTO_KEY_LOAD:
		error = zcrypt_key_from_ioc(&zc->zc_crypto,
		    &dcc->dcc_wrap_key);
		if (error != 0) {
			return (error);
		}
		dcc->dcc_clone_newkey = zc->zc_crypto.zic_clone_newkey;
		dcc->dcc_crypt = zc->zc_crypto.zic_crypt;
		break;
	case ZFS_IOC_CRYPTO_KEY_INHERIT:
		if (dataset_namecheck(zc->zc_crypto.zic_inherit_dsname,
		    NULL, NULL) != 0) {
			return (EINVAL);
		}
		error = dsl_dataset_hold(zc->zc_crypto.zic_inherit_dsname,
		    FTAG, &ids);
		if (error != 0) {
			return (error);
		}

		wkey = zcrypt_keystore_find_wrappingkey(
		    dsl_dataset_get_spa(ids), ids->ds_object);
		dsl_dataset_rele(ids, FTAG);
		if (wkey == NULL) {
			return (ENOTSUP);
		}
		dcc->dcc_wrap_key = zcrypt_key_copy(wkey);
		dcc->dcc_clone_newkey = zc->zc_crypto.zic_clone_newkey;
		dcc->dcc_crypt = zc->zc_crypto.zic_crypt;
		break;
	}

	dcc->dcc_salt = zc->zc_crypto.zic_salt;

	return (error);
}

/*
 * inputs:
 * zc_objset_type	type of objset to create (fs vs zvol)
 * zc_name		name of new objset
 * zc_value		name of snapshot to clone from (may be empty)
 * zc_nvlist_src{_size}	nvlist of properties to apply
 *
 * outputs: none
 */
static int
zfs_ioc_create(zfs_cmd_t *zc)
{
	objset_t *clone;
	int error = 0;
	dsl_crypto_ctx_t dcc = { 0 };
	nvlist_t *nvprops = NULL;
	int (*cbchkfunc)(spa_t *spa, void *arg) = NULL;
	void (*cbfunc)(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx);
	dmu_objset_type_t type = zc->zc_objset_type;

	switch (type) {

	case DMU_OST_ZFS:
		cbfunc = zfs_create_fs;
		break;

	case DMU_OST_ZVOL:
		cbchkfunc = zvol_check_cb;
		cbfunc = zvol_create_cb;
		break;

	default:
		cbfunc = NULL;
		break;
	}
	if (strchr(zc->zc_name, '@') ||
	    strchr(zc->zc_name, '%'))
		return (EINVAL);

	if (zc->zc_nvlist_src != NULL &&
	    (error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &nvprops)) != 0)
		return (error);

	if ((error = zfs_get_crypto_ctx(zc, &dcc)) != 0) {
		return (error);
	}

	if (zc->zc_value[0] != '\0') {
		/*
		 * We're creating a clone of an existing snapshot.
		 */
		zc->zc_value[sizeof (zc->zc_value) - 1] = '\0';
		if (dataset_namecheck(zc->zc_value, NULL, NULL) != 0) {
			nvlist_free(nvprops);
			return (EINVAL);
		}

		error = dmu_objset_hold(zc->zc_value, FTAG, &clone);
		if (error) {
			nvlist_free(nvprops);
			return (error);
		}

		error = dmu_objset_clone(zc->zc_name, dmu_objset_ds(clone),
		    &dcc, 0);
		dmu_objset_rele(clone, FTAG);
		if (error) {
			nvlist_free(nvprops);
			return (error);
		}

		/*
		 * If the clone inherited a fixed FSID, remove it.
		 * If not, just ignore the error.
		 */
		(void) zfs_remove_fsid_impl(zc->zc_name, NULL);
	} else {
		boolean_t is_insensitive = B_FALSE;
		nvlist_t *cbprops = NULL;

		if (cbfunc == NULL) {
			nvlist_free(nvprops);
			return (EINVAL);
		}
		VERIFY(nvlist_alloc(&cbprops, NV_UNIQUE_NAME, KM_SLEEP) == 0);

		if (type == DMU_OST_ZVOL) {
			error = zvol_fill_props(nvprops, cbprops);
			if (error != 0) {
				nvlist_free(nvprops);
				nvlist_free(cbprops);
				return (error);
			}
		} else if (type == DMU_OST_ZFS) {
			int error;

			/*
			 * We have to have normalization and
			 * case-folding flags correct when we do the
			 * file system creation, so go figure them out
			 * now.
			 */
			error = zfs_fill_zplprops(zc->zc_name, nvprops,
			    cbprops, &is_insensitive);
			if (error != 0) {
				nvlist_free(nvprops);
				nvlist_free(cbprops);
				return (error);
			}
		}
		error = dmu_objset_create(zc->zc_name, type,
		    is_insensitive ? DS_FLAG_CI_DATASET : 0, &dcc,
		    cbchkfunc, cbfunc, cbprops);
		nvlist_free(cbprops);
	}

	/*
	 * It would be nice to do this atomically.
	 */
	if (error == 0) {
		error = zfs_set_prop_nvlist(zc->zc_name, ZPROP_SRC_LOCAL,
		    nvprops, NULL, 0);
		if (error != 0)
			(void) dmu_objset_destroy(zc->zc_name, B_FALSE);
	}
	nvlist_free(nvprops);
	return (error);
}

/*
 * inputs:
 * zc_name	name of filesystem
 * zc_value	short name of snapshot
 * zc_cookie	recursive flag
 * zc_nvlist_src[_size] property list
 *
 * outputs:
 * zc_value	short snapname (i.e. part after the '@')
 */
static int
zfs_ioc_snapshot(zfs_cmd_t *zc)
{
	nvlist_t *nvprops = NULL;
	int error;
	boolean_t recursive = zc->zc_cookie;

	if (snapshot_namecheck(zc->zc_value, NULL, NULL) != 0)
		return (EINVAL);

	if (zc->zc_nvlist_src != NULL &&
	    (error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &nvprops)) != 0)
		return (error);

	error = zfs_check_userprops(zc->zc_name, nvprops);
	if (error)
		goto out;

	if (!nvlist_empty(nvprops) &&
	    zfs_earlier_version(zc->zc_name, SPA_VERSION_SNAP_PROPS)) {
		error = ENOTSUP;
		goto out;
	}

	error = dmu_objset_snapshot(zc->zc_name, zc->zc_value, NULL,
	    nvprops, recursive, B_FALSE, -1);

out:
	nvlist_free(nvprops);
	return (error);
}

int
zfs_unmount_snap(const char *name, void *arg)
{
	vfs_t *vfsp = NULL;

	if (arg) {
		char *snapname = arg;
		char *fullname = kmem_asprintf("%s@%s", name, snapname);
		vfsp = zfs_get_vfs(fullname);
		strfree(fullname);
	} else if (strchr(name, '@')) {
		vfsp = zfs_get_vfs(name);
	}

	if (vfsp) {
		/*
		 * Always force the unmount for snapshots.
		 */
		int flag = MS_FORCE;
		int err;

		if ((err = vn_vfswlock(vfsp->vfs_vnodecovered)) != 0) {
			VFS_RELE(vfsp);
			return (err);
		}
		VFS_RELE(vfsp);
		if ((err = dounmount(vfsp, flag, kcred)) != 0)
			return (err);
	}
	return (0);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_value		short name of snapshot
 * zc_defer_destroy	mark for deferred destroy
 *
 * outputs:	none
 */
static int
zfs_ioc_destroy_snaps(zfs_cmd_t *zc)
{
	int err;

	if (snapshot_namecheck(zc->zc_value, NULL, NULL) != 0)
		return (EINVAL);
	err = dmu_objset_find(zc->zc_name,
	    zfs_unmount_snap, zc->zc_value, DS_FIND_CHILDREN);
	if (err)
		return (err);
	return (dmu_snapshots_destroy(zc->zc_name, zc->zc_value,
	    zc->zc_defer_destroy));
}

/*
 * inputs:
 * zc_name		name of dataset to destroy
 * zc_objset_type	type of objset
 * zc_defer_destroy	mark for deferred destroy
 *
 * outputs:		none
 */
static int
zfs_ioc_destroy(zfs_cmd_t *zc)
{
	int err;
	if (strchr(zc->zc_name, '@') && zc->zc_objset_type == DMU_OST_ZFS) {
		err = zfs_unmount_snap(zc->zc_name, NULL);
		if (err)
			return (err);
	}

	err = dmu_objset_destroy(zc->zc_name, zc->zc_defer_destroy);
	if (zc->zc_objset_type == DMU_OST_ZVOL && err == 0)
		(void) zvol_remove_minor(zc->zc_name);
	return (err);
}

/*
 * inputs:
 * zc_name	name of dataset to rollback (to most recent snapshot)
 *
 * outputs:	none
 */
static int
zfs_ioc_rollback(zfs_cmd_t *zc)
{
	dsl_dataset_t *ds, *clone;
	int error;
	zfsvfs_t *zfsvfs;
	char *clone_name;
	dsl_crypto_ctx_t dcc = { 0 };
	zfs_crypt_key_status_t keystatus;

	error = dsl_dataset_hold(zc->zc_name, FTAG, &ds);
	if (error)
		return (error);

	/* must not be a snapshot */
	if (dsl_dataset_is_snapshot(ds)) {
		dsl_dataset_rele(ds, FTAG);
		return (EINVAL);
	}

	/* must have a most recent snapshot */
	if (ds->ds_phys->ds_prev_snap_txg < TXG_INITIAL) {
		dsl_dataset_rele(ds, FTAG);
		return (EINVAL);
	}

	/*
	 * For encrypted datasets we need the wrapping key
	 * since rollback is implemented via cloning.
	 */
	keystatus = dsl_dataset_keystatus(ds, B_FALSE);
	if (keystatus == ZFS_CRYPT_KEY_UNAVAILABLE) {
		dsl_dataset_rele(ds, FTAG);
		return (ENOKEY);
	} else if (keystatus == ZFS_CRYPT_KEY_AVAILABLE) {
		dcc.dcc_wrap_key = zcrypt_key_copy(
		    zcrypt_keystore_find_wrappingkey(
		    dsl_dataset_get_spa(ds), ds->ds_object));
	}

	/*
	 * Create clone of most recent snapshot, passing in
	 * the wrapping key for ds if there is one.
	 */
	clone_name = kmem_asprintf("%s/%%rollback", zc->zc_name);
	error = dmu_objset_clone(clone_name, ds->ds_prev, &dcc,
	    DS_FLAG_INCONSISTENT);
	if (error)
		goto out;

	error = dsl_dataset_own(clone_name, B_TRUE, FTAG, &clone);
	if (error)
		goto out;

	/*
	 * Do clone swap.
	 */
	if (getzfsvfs(zc->zc_name, &zfsvfs) == 0) {
		error = zfs_suspend_fs(zfsvfs);
		if (error == 0) {
			int resume_err;

			if (dsl_dataset_tryown(ds, B_FALSE, FTAG)) {
				error = dsl_dataset_clone_swap(clone, ds,
				    B_TRUE);
				dsl_dataset_disown(ds, FTAG);
				ds = NULL;
			} else {
				error = EBUSY;
			}
			resume_err = zfs_resume_fs(zfsvfs, zc->zc_name);
			error = error ? error : resume_err;
		}
		VFS_RELE(zfsvfs->z_vfs);
	} else {
		if (dsl_dataset_tryown(ds, B_FALSE, FTAG)) {
			error = dsl_dataset_clone_swap(clone, ds, B_TRUE);
			dsl_dataset_disown(ds, FTAG);
			ds = NULL;
		} else {
			error = EBUSY;
		}
	}

	/*
	 * Destroy clone (which also closes it).
	 */
	(void) dsl_dataset_destroy(clone, FTAG, B_FALSE);

out:
	strfree(clone_name);
	if (ds)
		dsl_dataset_rele(ds, FTAG);
	return (error);
}

/*
 * inputs:
 * zc_name	old name of dataset
 * zc_value	new name of dataset
 * zc_cookie	recursive flag (only valid for snapshots)
 *
 * outputs:	none
 */
static int
zfs_ioc_rename(zfs_cmd_t *zc)
{
	boolean_t recursive = zc->zc_cookie & 1;
	objset_t *os;
	int err;

	zc->zc_value[sizeof (zc->zc_value) - 1] = '\0';
	if (dataset_namecheck(zc->zc_value, NULL, NULL) != 0 ||
	    strchr(zc->zc_value, '%'))
		return (EINVAL);

	/*
	 * Because of a bug, earlier software did not set ZPROP_HASRECVD
	 * recursively. If a dataset with received properties no longer inherits
	 * ZPROP_HASRECVD because of a rename, the loss of ZPROP_HASRECVD
	 * leaves it in an invalid state. Set the property locally before the
	 * rename to avoid that.
	 */
	if ((err = dmu_objset_hold(zc->zc_name, FTAG, &os)) != 0)
		return (err);
	if (dsl_prop_get_hasrecvd(os))
		dsl_prop_set_hasrecvd(os);
	dmu_objset_rele(os, FTAG);

	/*
	 * Unmount snapshot unless we're doing a recursive rename,
	 * in which case the dataset code figures out which snapshots
	 * to unmount.
	 */
	if (!recursive && strchr(zc->zc_name, '@') != NULL &&
	    zc->zc_objset_type == DMU_OST_ZFS) {
		err = zfs_unmount_snap(zc->zc_name, NULL);
		if (err)
			return (err);
	}
	if (zc->zc_objset_type == DMU_OST_ZVOL)
		(void) zvol_remove_minor(zc->zc_name);

	return (dmu_objset_rename(zc->zc_name, zc->zc_value, recursive));
}

static int
zfs_check_settable(const char *dsname, nvpair_t *pair, cred_t *cr)
{
	const char *propname = nvpair_name(pair);
	boolean_t issnap = (strchr(dsname, '@') != NULL);
	zfs_prop_t prop = zfs_name_to_prop(propname);
	uint64_t intval;
	int err;

	if (prop == ZPROP_INVAL) {
		if (zfs_prop_user(propname)) {
			if (err = zfs_secpolicy_write_perms(dsname,
			    ZFS_DELEG_PERM_USERPROP, cr))
				return (err);
			return (0);
		}

		if (!issnap && zfs_prop_userquota(propname)) {
			const char *perm = NULL;
			const char *uq_prefix =
			    zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA];
			const char *gq_prefix =
			    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA];

			if (strncmp(propname, uq_prefix,
			    strlen(uq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_USERQUOTA;
			} else if (strncmp(propname, gq_prefix,
			    strlen(gq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_GROUPQUOTA;
			} else {
				/* USERUSED and GROUPUSED are read-only */
				return (EINVAL);
			}

			if (err = zfs_secpolicy_write_perms(dsname, perm, cr))
				return (err);
			return (0);
		}

		return (EINVAL);
	}

	if (issnap)
		return (EINVAL);

	if ((err = dsl_prop_decode_value(&pair)) != 0)
		return (err);

	/*
	 * Check that the value is valid for this pool version
	 */
	switch (prop) {
	case ZFS_PROP_COMPRESSION:
		/*
		 * If the user specified gzip compression, make sure
		 * the SPA supports it. We ignore any errors here since
		 * we'll catch them later.
		 */
		if (nvpair_type(pair) == DATA_TYPE_UINT64 &&
		    nvpair_value_uint64(pair, &intval) == 0) {
			if (intval >= ZIO_COMPRESS_GZIP_1 &&
			    intval <= ZIO_COMPRESS_GZIP_9 &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_GZIP_COMPRESSION)) {
				return (ENOTSUP);
			}

			if (intval == ZIO_COMPRESS_ZLE &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_ZLE_COMPRESSION))
				return (ENOTSUP);

			/*
			 * If this is a bootable dataset then
			 * verify that the compression algorithm
			 * is supported for booting. We must return
			 * something other than ENOTSUP since it
			 * implies a downrev pool version.
			 */
			if (zfs_is_bootfs(dsname) &&
			    !BOOTFS_COMPRESS_VALID(intval)) {
				return (ERANGE);
			}
		}
		break;

	case ZFS_PROP_COPIES:
		if (zfs_earlier_version(dsname, SPA_VERSION_DITTO_BLOCKS))
			return (ENOTSUP);
		break;

	case ZFS_PROP_DEDUP:
		if (zfs_earlier_version(dsname, SPA_VERSION_DEDUP))
			return (ENOTSUP);
		break;

	case ZFS_PROP_ENCRYPTION:
		if (zfs_earlier_version(dsname, SPA_VERSION_CRYPTO))
			return (ENOTSUP);
		if (zpl_earlier_version(dsname, ZPL_VERSION_SA))
			return (ENOTSUP);
		if (zfs_is_bootfs(dsname) && !BOOTFS_CRYPT_VALID(intval))
			return (ERANGE);
		break;

	case ZFS_PROP_SHARE2:
		/*
		 * We do not expose the $share2 property in the UI but
		 * we do need it to be read/write from userland.
		 * Since this property is updated the first time the
		 * dataset is mounted/shared, convert the $share2 case
		 * into a check for mount permission.
		 */
		return (zfs_secpolicy_write_perms(dsname, ZFS_DELEG_PERM_MOUNT,
		    CRED()));

	case ZFS_PROP_SHARESMB:
		if (zpl_earlier_version(dsname, ZPL_VERSION_FUID))
			return (ENOTSUP);
		break;

	case ZFS_PROP_ACLINHERIT:
		if (nvpair_type(pair) == DATA_TYPE_UINT64 &&
		    nvpair_value_uint64(pair, &intval) == 0) {
			if (intval == ZFS_ACL_PASSTHROUGH_X &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_PASSTHROUGH_X))
				return (ENOTSUP);
		}
		break;
	}

	if (zfs_prop_readonly(prop) && !zfs_prop_setonce(prop))
		return (EINVAL);

	return (zfs_secpolicy_setprop(dsname, prop, pair, CRED()));
}

/*
 * Removes properties from the given props list that fail permission checks
 * needed to clear them and to restore them in case of a receive error. For each
 * property, make sure we have both set and inherit permissions.
 *
 * Returns the first error encountered if any permission checks fail. If the
 * caller provides a non-NULL errlist, it also gives the complete list of names
 * of all the properties that failed a permission check along with the
 * corresponding error numbers. The caller is responsible for freeing the
 * returned errlist.
 *
 * If every property checks out successfully, zero is returned and the list
 * pointed at by errlist is NULL.
 */
static int
zfs_check_clearable(char *dataset, nvlist_t *props, nvlist_t **errlist)
{
	zfs_cmd_t *zc;
	nvpair_t *pair, *next_pair;
	nvlist_t *errors;
	int err, rv = 0;

	if (props == NULL)
		return (0);

	VERIFY(nvlist_alloc(&errors, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	zc = kmem_alloc(sizeof (zfs_cmd_t), KM_SLEEP);
	(void) strcpy(zc->zc_name, dataset);
	pair = nvlist_next_nvpair(props, NULL);
	while (pair != NULL) {
		next_pair = nvlist_next_nvpair(props, pair);

		(void) strcpy(zc->zc_value, nvpair_name(pair));
		if ((err = zfs_check_settable(dataset, pair, CRED())) != 0 ||
		    (err = zfs_secpolicy_inherit(zc, CRED())) != 0) {
			VERIFY(nvlist_remove_nvpair(props, pair) == 0);
			VERIFY(nvlist_add_int32(errors,
			    zc->zc_value, err) == 0);
		}
		pair = next_pair;
	}
	kmem_free(zc, sizeof (zfs_cmd_t));

	if ((pair = nvlist_next_nvpair(errors, NULL)) == NULL) {
		nvlist_free(errors);
		errors = NULL;
	} else {
		VERIFY(nvpair_value_int32(pair, &rv) == 0);
	}

	if (errlist == NULL)
		nvlist_free(errors);
	else
		*errlist = errors;

	return (rv);
}

static boolean_t
propval_equals(nvpair_t *p1, nvpair_t *p2)
{
	VERIFY(0 == dsl_prop_decode_value(&p1));
	VERIFY(0 == dsl_prop_decode_value(&p2));

	if (nvpair_type(p1) != nvpair_type(p2))
		return (B_FALSE);

	if (nvpair_type(p1) == DATA_TYPE_STRING) {
		char *valstr1, *valstr2;

		VERIFY(nvpair_value_string(p1, (char **)&valstr1) == 0);
		VERIFY(nvpair_value_string(p2, (char **)&valstr2) == 0);
		return (strcmp(valstr1, valstr2) == 0);
	} else {
		uint64_t intval1, intval2;

		VERIFY(nvpair_value_uint64(p1, &intval1) == 0);
		VERIFY(nvpair_value_uint64(p2, &intval2) == 0);
		return (intval1 == intval2);
	}
}

/*
 * Remove properties from props if they are not going to change (as determined
 * by comparison with origprops). Remove them from origprops as well, since we
 * do not need to clear or restore properties that won't change.
 */
static void
props_reduce(nvlist_t *props, nvlist_t *origprops)
{
	nvpair_t *pair, *next_pair;

	if (nvlist_empty(origprops))
		return; /* all props need to be received */

	pair = nvlist_next_nvpair(props, NULL);
	while (pair != NULL) {
		const char *propname = nvpair_name(pair);
		nvpair_t *match;

		next_pair = nvlist_next_nvpair(props, pair);

		if ((nvlist_lookup_nvpair(origprops, propname,
		    &match) != 0) || !propval_equals(pair, match))
			goto next; /* need to set received value */

		/* don't clear the existing received value */
		(void) nvlist_remove_nvpair(origprops, match);
		/* don't bother receiving the property */
		(void) nvlist_remove_nvpair(props, pair);
next:
		pair = next_pair;
	}
}

/*
 * Inputs:
 *
 * props	properties in the send stream:
 *              These never include elements of DATA_TYPE_BOOLEAN.
 *
 * cmdprops	properties specified with 'zfs receive -o' and 'zfs receive -x':
 *              These override properties in the send stream. Properties
 *              specified with -x are represented by elements of
 *              DATA_TYPE_BOOLEAN.
 *
 * origprops	existing received properties:
 *              These are cleared in the received dataset if they are not found
 *              in the props list from the send stream, so that the remaining
 *              set of properties in the dataset matches the set of what was
 *              received. The original properties are also stashed away before
 *              attempting the receive so they can be restored in case of an
 *              error. Before SPA_VERSION_RECVD_PROPS, received properties are
 *              not distinguished from local settings, and are in fact the local
 *              settings. To preserve the current effective values of existing
 *              local settings specified with 'zfs receive -x', we must remove
 *              those settings from origprops to avoid clearing them in the
 *              received dataset. Properties specified with 'zfs receive -o' can
 *              also be removed from origprops, since there is no need to clear
 *              them if we are going to set them locally to the specified value
 *              anyway. (Existing local settings are stored in a separate list
 *              whether or not they are the same as the existing received
 *              settings, so removing them from origprops does not affect our
 *              ability to restore the values they had before 'zfs receive -o'
 *              if the receive fails.)
 *
 * version	spa version
 *
 * The input property lists are modified as follows:
 *
 * If version < SPA_VERSION_RECVD_PROPS:
 *
 *     1. Properties specified with -x are removed from both props and cmdprops.
 *     2. Properties specified with -o are only removed from props so the values
 *        in cmdprops are applied to the received dataset.
 *     3. All properties specified on the command line, whether by -x or -o,
 *        are removed from origprops to avoid clearing them.
 *
 * If version >= SPA_VERSION_RECVD_PROPS:
 *
 *     We need to set received property values in spite of overriding them
 *     locally, so no properties are removed from props. Properties specified
 *     with -x are converted to flags in props and removed from cmdprops.
 *
 * Regardless of version, properties specified with -x (in cmdprops) are
 * guaranteed to retain their current effective values after the receive,
 * whether or not they are also present in the send stream (in props). At the
 * end of this function, cmdprops no longer contains any elements of
 * DATA_TYPE_BOOLEAN.
 *
 * After this function modifies the input property lists, the caller passes
 * cmdprops to zfs_set_prop_nvlist() to complete the following behavior:
 *
 * -- zfs receive -o:
 *
 * Override properties specified with -o. These properties are sent as
 * DATA_TYPE_UINT64 or DATA_TYPE_STRING because a value was specified to
 * override the value in the send stream. Set them on the top level dataset, and
 * if the send stream is recursive, do the following depending on the property:
 *
 * 1) If the property is inheritable, explicitly inherit the property in
 *    descendant datasets, so the setting is in one place.
 * 2) If the property is not inheritable, apply the local setting recursively.
 * 3) If the non-inheritable property is a size property like quota or
 *    reservation, it does not make sense to set the property recursively, since
 *    the size value already applies to the entire subtree. In that case, do
 *    nothing to descendant datasets, unless the value is the default size of
 *    zero, when it does make sense to apply the setting recursively (e.g.
 *    quota=none).
 *
 * -- zfs receive -x:
 *
 * "Exclude" properties specified with -x. These properties are sent as
 * DATA_TYPE_BOOLEAN because they do not have a value. The idea is to ensure
 * that the received property does not change the effective value of the
 * property on the received dataset; that is, to get the behavior that you would
 * have gotten if the property had been excluded from the send stream (meanwhile
 * not failing to set the received values), and to do that recursively in the
 * case of a recursive stream. How that is accomplished depends on the property:
 *
 * 1) If the property is inheritable, explicitly inherit the property to
 *    override the received value. The effect is the same as 'zfs inherit -r' if
 *    the send stream is recursive.
 * 2) If the property is not inheritable, set the current effective value
 *    locally (or the default in the case of a dataset newly created by the
 *    receive); do that recursively if the send stream is recursive. If the
 *    property has no default value (e.g. volsize), fail with an error unless
 *    the send stream is an incremental update.
 *
 * In the case of an incremental update, do nothing if the received property is
 * already overridden by explicit inheritance or a local setting. Checking for
 * an existing setting and updating the property is atomic.
 *
 * If the property is not present in the send stream, do nothing.
 *
 * -- Uneditable, set-once, and special properties:
 *
 * Specifying an uneditable property with 'receive -o' or 'receive -x' fails the
 * command and prints an error message. Even set-once properties normally
 * settable by 'zfs create -o' fail with an error message when specified with
 * 'zfs receive -o' because they are bound to the sent data. These include
 *
 *      normalization
 *      casesensitivity
 *      utf8only
 *      volblocksize
 *
 * A set-once property that is independent of the sent data may be allowed.
 *
 * The following property is editable, but modifications to the property only
 * affect subsequent writes, not subsequent receives:
 *
 *      recordsize
 *
 * Specifying recordsize with 'receive -o' or 'receive -x' (default is 128K)
 * succeeds without a warning message and has no effect on received data.
 */
static int
props_override(nvlist_t *props, nvlist_t *origprops, nvlist_t *cmdprops,
    uint64_t version, zprop_setflags_t setflags)
{
	nvpair_t *pair, *next_pair;
	int err;

	pair = nvlist_next_nvpair(cmdprops, NULL);
	while (pair != NULL) {
		const char *propname = nvpair_name(pair);
		zfs_prop_t prop = zfs_name_to_prop(propname);
		nvpair_t *match;

		next_pair = nvlist_next_nvpair(cmdprops, pair);

		if ((nvlist_lookup_nvpair(origprops, propname, &match) == 0)) {
			if (version < SPA_VERSION_RECVD_PROPS) {
				/*
				 * Remove the overridden property from origprops
				 * to avoid clearing it.
				 */
				(void) nvlist_remove_nvpair(origprops, match);
			} else if (nvpair_type(pair) == DATA_TYPE_BOOLEAN) {
				/*
				 * If an existing received property is not
				 * overridden locally, and we are about to clear
				 * it because it is not present in the send
				 * stream, we may need to promote the received
				 * value to a local value first to preserve the
				 * effective value. We can't simply avoid
				 * clearing it by removing it from origprops,
				 * because the dataset's set of received
				 * properties must come to equal what is in the
				 * send stream and not retain anything not in
				 * the send stream if we want 'send -b' to work
				 * correctly.
				 */
				if ((err = dsl_prop_encode_flag(origprops,
				    &match, ZPROP_PRESERVE)) != 0)
					return (err);
			}
		}

		if ((nvlist_lookup_nvpair(props, propname, &match) == 0)) {
			/* overrides a property in the send stream */
			if (version < SPA_VERSION_RECVD_PROPS) {
				nvpair_t *propval = pair;
				uint64_t intval;

				/*
				 * Unless this is a special property that we do
				 * not override recursively, don't bother
				 * receiving the overridden property. Before
				 * SPA_VERSION_RECVD_PROPS, received properties
				 * are not distinct from local settings, so we
				 * can ignore them when they are overridden.
				 */
				if (!((prop == ZFS_PROP_QUOTA ||
				    prop == ZFS_PROP_RESERVATION ||
				    prop == ZFS_PROP_REFQUOTA ||
				    prop == ZFS_PROP_REFRESERVATION) &&
				    (setflags & ZPROP_SET_DESCENDANT) &&
				    (nvpair_type(pair) != DATA_TYPE_BOOLEAN) &&
				    (dsl_prop_decode_value(&propval) == 0) &&
				    (nvpair_value_uint64(propval,
				    &intval) == 0) && (intval != 0))) {
					(void) nvlist_remove_nvpair(props,
					    match);
				}

				if (nvpair_type(pair) == DATA_TYPE_BOOLEAN) {
					/*
					 * Since the property specified with -x
					 * has been removed from the received
					 * props list, it has effectively been
					 * excluded from the send stream and
					 * there is nothing we could do to
					 * exclude it further. We're done with
					 * it.
					 */
					(void) nvlist_remove_nvpair(cmdprops,
					    pair);
				}
			} else if (nvpair_type(pair) == DATA_TYPE_BOOLEAN) {
				/*
				 * Specifying a property with -x prevents the
				 * property's recevied value from changing its
				 * effective value. The idea is to set the
				 * received value, so it remains available, but
				 * treat the effective value as if the property
				 * had been excluded from the send stream.  We
				 * encode, along with the received value, the
				 * fact that its effective value must be
				 * preserved so that dsl_prop_set_sync() can
				 * carry out the necessary steps atomically.
				 */
				if ((err = dsl_prop_encode_flag(props, &match,
				    ZPROP_PRESERVE)) != 0)
					return (err);

				(void) nvlist_remove_nvpair(cmdprops, pair);
			}
		} else {
			/*
			 * A property specified with -o is still applied locally
			 * even if it does not override a property in the send
			 * stream.
			 */
			if (nvpair_type(pair) == DATA_TYPE_BOOLEAN) {
				/*
				 * If the property is not present in the send
				 * stream, specifying a property with -x has no
				 * effect beyond the changes already made to
				 * origprops.
				 */
				(void) nvlist_remove_nvpair(cmdprops, pair);
			}
		}

		pair = next_pair;
	}

	return (0);
}

/*
 * When encryption is enabled the checksum property must always be sha256-mac.
 * We need to make sure that any cmdline props don't override that. We need to
 * do this only for checksum because encryption is a setonce property so can't
 * be overridden anyway. So remove checksum from cmdprops but also add it to
 * the errors list so the user is informed we "ignored" their request - the recv
 * will work. This is better than failing the whole recv (which would happen if
 * we returned an error from this function) since it lets us do a recursive
 * receive changing checksum for non encrypted datasets from what is in the
 * stream, while not clobbering the checksum value for encrypted ones.
 */
static void
props_handle_encryption(nvlist_t *props, nvlist_t *origprops,
    nvlist_t *cmdprops, nvlist_t *errors)
{
	nvpair_t *pair, *next_pair;

	pair = nvlist_next_nvpair(cmdprops, NULL);
	while (pair != NULL) {
		const char *propname = nvpair_name(pair);
		const char *encrypt = zfs_prop_to_name(ZFS_PROP_ENCRYPTION);
		zfs_prop_t prop = zfs_name_to_prop(propname);

		next_pair = nvlist_next_nvpair(cmdprops, pair);

		if (prop == ZFS_PROP_CHECKSUM &&
		    (nvlist_exists(origprops, encrypt) ||
		    nvlist_exists(props, encrypt))) {
			(void) nvlist_add_int32(errors, propname, ERANGE);
			(void) nvlist_remove_nvpair(cmdprops, pair);
		}

		pair = next_pair;
	}
}

#ifdef	DEBUG
static boolean_t zfs_ioc_recv_inject_err;
#endif

/*
 * inputs:
 * zc_name		name of containing filesystem
 * zc_top_ds		top level received dataset
 * zc_nvlist_src{_size}	nvlist of properties to receive
 * zc_nvlist_conf{_size} nvlist of properties to exclude or set locally
 * zc_value		name of snapshot to create
 * zc_string		name of clone origin (if DRR_FLAG_CLONE)
 * zc_cookie		file descriptor to recv from
 * zc_begin_record	the BEGIN record of the stream (not byteswapped)
 * zc_guid		force flag
 * zc_cleanup_fd	cleanup-on-exit file descriptor
 * zc_action_handle	handle for this guid/ds mapping (or zero on first call)
 *
 * outputs:
 * zc_cookie		number of bytes read
 * zc_nvlist_dst{_size} error for each unapplied received property
 * zc_obj		zprop_errflags_t
 * zc_action_handle	handle for this guid/ds mapping
 */
static int
zfs_ioc_recv(zfs_cmd_t *zc)
{
	file_t *fp;
	objset_t *os;
	dmu_recv_cookie_t drc;
	boolean_t force = (boolean_t)zc->zc_guid;
	int fd;
	int error = 0;
	int props_error = 0;
	nvlist_t *errors = NULL;
	offset_t off;
	nvlist_t *props = NULL; /* sent properties */
	nvlist_t *origprops = NULL; /* existing received properties */
	nvlist_t *cmdprops = NULL; /* props specified by 'zfs recv' */
	nvlist_t *rawprops = NULL; /* to restore in case of error */
	nvlist_t *special = NULL; /* need special handling to restore */
	objset_t *origin = NULL;
	char *tosnap;
	char tofs[ZFS_MAXNAMELEN];
	boolean_t first_recvd_props = B_FALSE;
	spa_t *spa;
	uint64_t version;
	zprop_setflags_t setflags = 0;
	char *end;
	dsl_crypto_ctx_t dcc = { 0 };

	if (dataset_namecheck(zc->zc_value, NULL, NULL) != 0 ||
	    strchr(zc->zc_value, '@') == NULL ||
	    strchr(zc->zc_value, '%'))
		return (EINVAL);

	(void) strcpy(tofs, zc->zc_value);
	tosnap = strchr(tofs, '@');
	*tosnap++ = '\0';

	/*
	 * Determine whether this is a descendant of the top level dataset
	 * specified on the command line. Ignore the snapshot name if a snapshot
	 * was specified as an exact path.
	 */
	end = strrchr(zc->zc_top_ds, '@');
	if (end != NULL)
		*end = '\0';
	if (strcmp(tofs, zc->zc_top_ds) != 0)
		setflags |= ZPROP_SET_DESCENDANT;

	/*
	 * The path of the received dataset must start with the path of the top
	 * level dataset.
	 */
	if (strstr(zc->zc_value, zc->zc_top_ds) != zc->zc_value)
		return (EINVAL);

	fd = zc->zc_cookie;
	fp = getf(fd);
	if (fp == NULL)
		return (EBADF);

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error)
		goto out;

	version = spa_version(spa);
	spa_close(spa, FTAG);

	if (zc->zc_nvlist_src != NULL &&
	    (error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props)) != 0)
		goto out;

	if (zc->zc_nvlist_conf != NULL &&
	    (error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &cmdprops)) != 0)
		goto out;

	if ((error = zfs_get_crypto_ctx(zc, &dcc)) != 0)
		return (error);

	VERIFY(nvlist_alloc(&errors, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	error = dmu_objset_hold(tofs, FTAG, &os);
	if (error && error != ENOENT)
		goto out;
	if (error == 0) {
		error = dsl_prop_get_all_raw(os, &rawprops);
		error |= zfs_prop_save_special(os, &special);
		dmu_objset_rele(os, FTAG);
		if (error != 0)
			goto out;
	}

	if (props && dmu_objset_hold(tofs, FTAG, &os) == 0) {
		if (version >= SPA_VERSION_RECVD_PROPS) {
			if (!dsl_prop_get_hasrecvd(os))
				first_recvd_props = B_TRUE;
		}

		/*
		 * If new received properties are supplied, they are to
		 * completely replace the existing received properties, so stash
		 * away the existing ones.
		 */
		if (dsl_prop_get_received(os, &origprops) == 0) {
			/*
			 * Don't bother writing a property if its value won't
			 * change (and avoid the unnecessary security checks).
			 *
			 * The first receive after SPA_VERSION_RECVD_PROPS is a
			 * special case where we blow away all local properties
			 * regardless.
			 */
			if (!first_recvd_props)
				props_reduce(props, origprops);
		}

		dmu_objset_rele(os, FTAG);
	}

	props_handle_encryption(props, origprops, cmdprops, errors);

	if (!nvlist_empty(cmdprops)) {
		/*
		 * Try to avoid calling dmu_recv_begin() if any of the specified
		 * properties are invalid, since otherwise we are committed to
		 * calling dmu_recv_stream(). We can't validate permissions on a
		 * dataset newly created by this receive, since the dataset
		 * won't exist until after dmu_recv_begin(), so we check
		 * permissions on the containing filesystem instead. We repeat
		 * this validation when we actually set the properties.
		 */
		nvpair_t *pair = NULL;
		while ((pair = nvlist_next_nvpair(cmdprops, pair)) != NULL) {
			zprop_source_t source;

			/*
			 * Properties without a value (DATA_TYPE_BOOLEAN) are
			 * those specified by 'zfs receive -x' and are converted
			 * to flags in the received property nvlist by
			 * props_override(), so we don't really need to validate
			 * them here. (They will be removed from cmdprops before
			 * we set anything.) Those that do not have a
			 * corresponding pair in the received property nvlist
			 * are simply dropped, so we want to validate the
			 * specified names before that happens (simply for the
			 * sake of helpful feedback; dsl_props_set() will not
			 * see any value-less pairs from zfs_ioc_recv()).
			 * ZPROP_SRC_NONE happens to be a valid source for a
			 * property without a value.
			 */
			source = ((nvpair_type(pair) == DATA_TYPE_BOOLEAN) ?
			    ZPROP_SRC_NONE : ZPROP_SRC_LOCAL);

			if ((error = zfs_prop_validate(pair, source)) != 0)
				goto out;

			/* validate permissions */
			if ((error = zfs_check_settable(zc->zc_name, pair,
			    CRED())) != 0)
				goto out;
		}
	}

	/*
	 * Do this only after validating cmdprops so we don't neglect to
	 * validate properties specified with -x, which props_override() removes
	 * from cmdprops (converting them into flags in the props nvlist).
	 */
	if ((error = props_override(props, origprops, cmdprops, version,
	    setflags)) != 0)
		goto out;

	/*
	 * Remove readonly properties from origprops so we don't try to clear
	 * them, but wait until after props_handle_encryption() since that
	 * function uses origprops to check for the encryption property.
	 */
	if (origprops != NULL) {
		nvlist_t *errlist = NULL;

		props_filter(origprops, prop_readonly_test);
		/*
		 * Wait until after props_override() and props_filter() so we
		 * don't check any properties that we aren't going to clear.
		 */
		if (zfs_check_clearable(tofs, origprops,
		    &errlist) != 0)
			(void) nvlist_merge(errors, errlist, 0);
		nvlist_free(errlist);
	}

	if (zc->zc_string[0]) {
		error = dmu_objset_hold(zc->zc_string, FTAG, &origin);
		if (error)
			goto out;
	}

	error = dmu_recv_begin(tofs, tosnap, zc->zc_top_ds,
	    &zc->zc_begin_record, force, origin, &drc, &dcc);
	if (origin)
		dmu_objset_rele(origin, FTAG);
	if (error)
		goto out;

	/*
	 * Set received properties before we receive the stream so that they are
	 * applied to the new data. Note that we must call dmu_recv_stream() if
	 * dmu_recv_begin() succeeds.
	 */
	if (props) {
		nvlist_t *errlist;

		if (dmu_objset_from_ds(drc.drc_logical_ds, &os) == 0) {
			if (drc.drc_newfs) {
				if (version >= SPA_VERSION_RECVD_PROPS)
					first_recvd_props = B_TRUE;
			} else if (origprops != NULL) {
				if (clear_received_props(os, tofs, origprops,
				    first_recvd_props ? NULL : props) != 0)
					zc->zc_obj |= ZPROP_ERR_NOCLEAR;
			} else {
				zc->zc_obj |= ZPROP_ERR_NOCLEAR;
			}
			dsl_prop_set_hasrecvd(os);
		} else if (!drc.drc_newfs) {
			zc->zc_obj |= ZPROP_ERR_NOCLEAR;
		}

		/*
		 * Set local properties specified on the command line with -o
		 * and -x before setting received properties, but after setting
		 * $hasrecvd so that explicit inheritance in descendant datasets
		 * works correctly.
		 */
		if (!nvlist_empty(cmdprops)) {
			(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_LOCAL,
			    cmdprops, NULL, setflags);
		}

		(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_RECEIVED,
		    props, &errlist, 0);
		(void) nvlist_merge(errors, errlist, 0);
		nvlist_free(errlist);
	} else if (!nvlist_empty(cmdprops)) {
		(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_LOCAL,
		    cmdprops, NULL, setflags);
	}

	if (fit_error_list(zc, &errors) != 0 || put_nvlist(zc, errors) != 0) {
		/*
		 * Caller made zc->zc_nvlist_dst less than the minimum expected
		 * size or supplied an invalid address.
		 */
		props_error = EINVAL;
	}

	off = fp->f_offset;
	error = dmu_recv_stream(&drc, fp->f_vnode, &off, zc->zc_cleanup_fd,
	    &zc->zc_action_handle);

	if (error == 0) {
		zfsvfs_t *zfsvfs = NULL;

		if (getzfsvfs(tofs, &zfsvfs) == 0) {
			/* online recv */
			int end_err;

			error = zfs_suspend_fs(zfsvfs);
			/*
			 * If the suspend fails, then the recv_end will
			 * likely also fail, and clean up after itself.
			 */
			end_err = dmu_recv_end(&drc);
			if (error == 0)
				error = zfs_resume_fs(zfsvfs, tofs);
			error = error ? error : end_err;
			VFS_RELE(zfsvfs->z_vfs);
		} else {
			error = dmu_recv_end(&drc);
		}
	}

	zc->zc_cookie = off - fp->f_offset;
	if (VOP_SEEK(fp->f_vnode, fp->f_offset, &off, NULL) == 0)
		fp->f_offset = off;

#ifdef	DEBUG
	if (zfs_ioc_recv_inject_err) {
		zfs_ioc_recv_inject_err = B_FALSE;
		error = 1;
	}
#endif
	/*
	 * On error, restore the original props.
	 */
	if (error && rawprops) {
		if (dmu_objset_hold(tofs, FTAG, &os) == 0) {
			if (dsl_prop_set_all_raw(os, rawprops) != 0 ||
			    zfs_prop_restore_special(os, special) != 0)
				zc->zc_obj |= ZPROP_ERR_NORESTORE;
			dmu_objset_rele(os, FTAG);
		} else {
			/* We failed to restore the properties. */
			zc->zc_obj |= ZPROP_ERR_NORESTORE;
		}
	}

out:
	nvlist_free(props);
	nvlist_free(origprops);
	nvlist_free(cmdprops);
	nvlist_free(rawprops);
	nvlist_free(special);
	nvlist_free(errors);
	releasef(fd);

	if (error == 0)
		error = props_error;

	return (error);
}

/*
 * inputs:
 * zc_name	name of snapshot to send
 * zc_cookie	file descriptor to send stream to
 * zc_obj	fromorigin flag (mutually exclusive with zc_fromobj)
 * zc_sendobj	objsetid of snapshot to send
 * zc_fromobj	objsetid of incremental fromsnap (may be zero)
 *
 * outputs: none
 */
static int
zfs_ioc_send(zfs_cmd_t *zc)
{
	objset_t *fromsnap = NULL;
	objset_t *tosnap;
	file_t *fp;
	int error;
	offset_t off;
	dsl_dataset_t *ds;
	dsl_dataset_t *dsfrom = NULL;
	spa_t *spa;
	dsl_pool_t *dp;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error)
		return (error);

	dp = spa_get_dsl(spa);
	rw_enter(&dp->dp_config_rwlock, RW_READER);
	error = dsl_dataset_hold_obj(dp, zc->zc_sendobj, FTAG, &ds);
	rw_exit(&dp->dp_config_rwlock);
	if (error) {
		spa_close(spa, FTAG);
		return (error);
	}

	error = dmu_objset_from_ds(ds, &tosnap);
	if (error) {
		dsl_dataset_rele(ds, FTAG);
		spa_close(spa, FTAG);
		return (error);
	}

	if (zc->zc_fromobj != 0) {
		rw_enter(&dp->dp_config_rwlock, RW_READER);
		error = dsl_dataset_hold_obj(dp, zc->zc_fromobj, FTAG, &dsfrom);
		rw_exit(&dp->dp_config_rwlock);
		spa_close(spa, FTAG);
		if (error) {
			dsl_dataset_rele(ds, FTAG);
			return (error);
		}
		error = dmu_objset_from_ds(dsfrom, &fromsnap);
		if (error) {
			dsl_dataset_rele(dsfrom, FTAG);
			dsl_dataset_rele(ds, FTAG);
			return (error);
		}
	} else {
		spa_close(spa, FTAG);
	}

	fp = getf(zc->zc_cookie);
	if (fp == NULL) {
		dsl_dataset_rele(ds, FTAG);
		if (dsfrom)
			dsl_dataset_rele(dsfrom, FTAG);
		return (EBADF);
	}

	dsl_dataset_start_send(ds);
	if (dsfrom)
		dsl_dataset_start_send(dsfrom);
	off = fp->f_offset;
	error = dmu_sendbackup(tosnap, fromsnap, zc->zc_obj, fp->f_vnode, &off);

	if (VOP_SEEK(fp->f_vnode, fp->f_offset, &off, NULL) == 0)
		fp->f_offset = off;
	releasef(zc->zc_cookie);
	if (dsfrom) {
		dsl_dataset_end_send(dsfrom);
		dsl_dataset_rele(dsfrom, FTAG);
	}
	dsl_dataset_end_send(ds);
	dsl_dataset_rele(ds, FTAG);
	return (error);
}

static int
zfs_ioc_inject_fault(zfs_cmd_t *zc)
{
	int id, error;

	error = zio_inject_fault(zc->zc_name, (int)zc->zc_guid, &id,
	    &zc->zc_inject_record);

	if (error == 0)
		zc->zc_guid = (uint64_t)id;

	return (error);
}

static int
zfs_ioc_clear_fault(zfs_cmd_t *zc)
{
	return (zio_clear_fault((int)zc->zc_guid));
}

static int
zfs_ioc_inject_list_next(zfs_cmd_t *zc)
{
	int id = (int)zc->zc_guid;
	int error;

	error = zio_inject_list_next(&id, zc->zc_name, sizeof (zc->zc_name),
	    &zc->zc_inject_record);

	zc->zc_guid = id;

	return (error);
}

static int
zfs_ioc_error_log(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	size_t count = (size_t)zc->zc_nvlist_dst_size;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	error = spa_get_errlog(spa, (void *)(uintptr_t)zc->zc_nvlist_dst,
	    &count);
	if (error == 0)
		zc->zc_nvlist_dst_size = count;
	else
		zc->zc_nvlist_dst_size = spa_get_errlog_size(spa);

	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_clear(zfs_cmd_t *zc)
{
	spa_t *spa;
	vdev_t *vd;
	int error;

	/*
	 * On zpool clear we also fix up missing slogs
	 */
	mutex_enter(&spa_namespace_lock);
	spa = spa_lookup(zc->zc_name);
	if (spa == NULL) {
		mutex_exit(&spa_namespace_lock);
		return (EIO);
	}
	if (spa_get_log_state(spa) == SPA_LOG_MISSING) {
		/* we need to let spa_open/spa_load clear the chains */
		spa_set_log_state(spa, SPA_LOG_CLEAR);
	}
	spa->spa_last_open_failed = 0;
	mutex_exit(&spa_namespace_lock);

	if (zc->zc_cookie & ZPOOL_NORMAL_LOAD) {
		error = spa_open(zc->zc_name, &spa, FTAG);
	} else {
		nvlist_t *policy;
		nvlist_t *config = NULL;

		if (zc->zc_nvlist_src == NULL)
			return (EINVAL);

		if ((error = get_nvlist(zc->zc_nvlist_src,
		    zc->zc_nvlist_src_size, zc->zc_iflags, &policy)) == 0) {
			error = spa_open_policy(zc->zc_name, &spa, FTAG,
			    policy, &config);
			if (config != NULL) {
				int err;

				if ((err = put_nvlist(zc, config)) != 0) {
					if (error == 0)
						spa_close(spa, FTAG);
					error = err;
				}
				nvlist_free(config);
			}
			nvlist_free(policy);
		}
	}

	if (error)
		return (error);

	spa_vdev_state_enter(spa, SCL_NONE);

	if (zc->zc_guid == 0) {
		vd = NULL;
	} else {
		vd = spa_lookup_by_guid(spa, zc->zc_guid, B_TRUE);
		if (vd == NULL) {
			(void) spa_vdev_state_exit(spa, NULL, ENODEV);
			spa_close(spa, FTAG);
			return (ENODEV);
		}
	}

	vdev_clear(spa, vd);

	(void) spa_vdev_state_exit(spa, NULL, 0);

	/*
	 * Resume any suspended I/Os.
	 */
	if (zio_resume(spa) != 0)
		error = EIO;

	spa_close(spa, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name	name of filesystem
 * zc_value	name of origin snapshot
 *
 * outputs:
 * zc_string	name of conflicting snapshot, if there is one
 */
static int
zfs_ioc_promote(zfs_cmd_t *zc)
{
	char *cp;

	/*
	 * We don't need to unmount *all* the origin fs's snapshots, but
	 * it's easier.
	 */
	cp = strchr(zc->zc_value, '@');
	if (cp)
		*cp = '\0';
	(void) dmu_objset_find(zc->zc_value,
	    zfs_unmount_snap, NULL, DS_FIND_SNAPSHOTS);
	return (dsl_dataset_promote(zc->zc_name, zc->zc_string));
}

/*
 * Retrieve a single {user|group}{used|quota}@... property.
 *
 * inputs:
 * zc_name	name of filesystem
 * zc_objset_type zfs_userquota_prop_t
 * zc_value	domain name (eg. "S-1-234-567-89")
 * zc_guid	RID/UID/GID
 *
 * outputs:
 * zc_cookie	property value
 */
static int
zfs_ioc_userspace_one(zfs_cmd_t *zc)
{
	zfsvfs_t *zfsvfs;
	int error;

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (EINVAL);

	error = zfsvfs_hold(zc->zc_name, FTAG, &zfsvfs, B_FALSE);
	if (error)
		return (error);

	error = zfs_userspace_one(zfsvfs,
	    zc->zc_objset_type, zc->zc_value, zc->zc_guid, &zc->zc_cookie);
	zfsvfs_rele(zfsvfs, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_cookie		zap cursor
 * zc_objset_type	zfs_userquota_prop_t
 * zc_nvlist_dst[_size] buffer to fill (not really an nvlist)
 *
 * outputs:
 * zc_nvlist_dst[_size]	data buffer (array of zfs_useracct_t)
 * zc_cookie	zap cursor
 */
static int
zfs_ioc_userspace_many(zfs_cmd_t *zc)
{
	zfsvfs_t *zfsvfs;
	int bufsize = zc->zc_nvlist_dst_size;

	if (bufsize <= 0)
		return (ENOMEM);

	int error = zfsvfs_hold(zc->zc_name, FTAG, &zfsvfs, B_FALSE);
	if (error)
		return (error);

	void *buf = kmem_alloc(bufsize, KM_SLEEP);

	error = zfs_userspace_many(zfsvfs, zc->zc_objset_type, &zc->zc_cookie,
	    buf, &zc->zc_nvlist_dst_size);

	if (error == 0) {
		error = xcopyout(buf,
		    (void *)(uintptr_t)zc->zc_nvlist_dst,
		    zc->zc_nvlist_dst_size);
	}
	kmem_free(buf, bufsize);
	zfsvfs_rele(zfsvfs, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 *
 * outputs:
 * none
 */
static int
zfs_ioc_userspace_upgrade(zfs_cmd_t *zc)
{
	objset_t *os;
	int error = 0;
	zfsvfs_t *zfsvfs;

	if (getzfsvfs(zc->zc_name, &zfsvfs) == 0) {
		if (!dmu_objset_userused_enabled(zfsvfs->z_os)) {
			/*
			 * If userused is not enabled, it may be because the
			 * objset needs to be closed & reopened (to grow the
			 * objset_phys_t).  Suspend/resume the fs will do that.
			 */
			error = zfs_suspend_fs(zfsvfs);
			if (error == 0)
				error = zfs_resume_fs(zfsvfs, zc->zc_name);
		}
		if (error == 0)
			error = dmu_objset_userspace_upgrade(zfsvfs->z_os);
		VFS_RELE(zfsvfs->z_vfs);
	} else {
		/* XXX kind of reading contents without owning */
		error = dmu_objset_hold(zc->zc_name, FTAG, &os);
		if (error)
			return (error);

		error = dmu_objset_userspace_upgrade(os);
		dmu_objset_rele(os, FTAG);
	}

	return (error);
}

ace_t full_access[] = {
	{(uid_t)-1, ACE_ALL_PERMS, ACE_EVERYONE, 0}
};

static int
zfs_ioc_crypto_key_load(zfs_cmd_t *zc)
{
	spa_t *spa;
	zcrypt_key_t *wrappingkey = NULL;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (spa_version(spa) < SPA_VERSION_CRYPTO) {
		spa_close(spa, FTAG);
		return (ENOTSUP);
	}
	error = zcrypt_key_from_ioc(&zc->zc_crypto, &wrappingkey);
	if (error != 0) {
		spa_close(spa, FTAG);
		return (error);
	}
	error = dsl_crypto_key_load(zc->zc_name, wrappingkey);
	if (error == EEXIST)
		zcrypt_key_free(wrappingkey);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_crypto_key_inherit(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (spa_version(spa) < SPA_VERSION_CRYPTO) {
		spa_close(spa, FTAG);
		return (ENOTSUP);
	}

	error = dsl_crypto_key_inherit(zc->zc_name);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_crypto_key_unload(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (spa_version(spa) < SPA_VERSION_CRYPTO) {
		spa_close(spa, FTAG);
		return (ENOTSUP);
	}

	error = dsl_crypto_key_unload(zc->zc_name);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_crypto_key_new(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (spa_version(spa) < SPA_VERSION_CRYPTO) {
		spa_close(spa, FTAG);
		return (ENOTSUP);
	}

	error = dsl_crypto_key_new(zc->zc_name);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_crypto_key_change(zfs_cmd_t *zc)
{
	spa_t *spa;
	zcrypt_key_t *wrappingkey = NULL;
	nvlist_t *props = NULL;
	nvpair_t *pair = NULL;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (spa_version(spa) < SPA_VERSION_CRYPTO) {
		spa_close(spa, FTAG);
		return (ENOTSUP);
	}

	if (zc->zc_nvlist_src_size != 0 && (error =
	    get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))) {
		spa_close(spa, FTAG);
		return (error);
	}

	/*
	 * First check that we are allowed to set the props if
	 * there are any.  Don't use zfs_set_prop_nvlist() because
	 * we must have the properties set in the same txg.
	 * We have to do this here rather than in
	 * zfs_secpolicy_crypto_keychange() because the nvlist needs
	 * copied in and unpacked, if we do it twice there is a risk
	 * of a TOCTTOU vulnerability.
	 *
	 * The salt property is hidden/internal and only gets updated when
	 * we do a ZFS_IOC_CRYPTO_CHANGE_KEY which uses the "keychange"
	 * delegation so no additional check is done for salt. Being
	 * marked as a readonly property it isn't delgatable anyway.
	 *
	 * SALT is a property so it can be read from userland easily,
	 * when being set it comes down over the ioctl interface via
	 * zc_crypto.zic_salt here we turn it into a property so that
	 * it is set atomically in dsl_crypto_key_change() with any other
	 * props and the actual key change.  Note this is slighly different
	 * to what happens on a dataset create where the salt goes in
	 * via the dsl_crypto_ctx_t.
	 */
	if (props != NULL) {
		while ((pair = nvlist_next_nvpair(props, pair)) != NULL) {
			error = zfs_check_settable(zc->zc_name, pair, CRED());
			if (error != 0)
				goto out;
		}
	} else if (props == NULL) {
		VERIFY(nvlist_alloc(&props, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	}

	VERIFY3U(nvlist_add_uint64(props, zfs_prop_to_name(ZFS_PROP_SALT),
	    zc->zc_crypto.zic_salt), ==, 0);

	error = zcrypt_key_from_ioc(&zc->zc_crypto, &wrappingkey);
	if (error == 0)
		error = dsl_crypto_key_change(zc->zc_name, wrappingkey, props);
out:
	spa_close(spa, FTAG);
	nvlist_free(props);
	return (error);
}

/*
 * inputs:
 * zc_name		name of containing filesystem
 * zc_obj		object # beyond which we want next in-use object #
 *
 * outputs:
 * zc_obj		next in-use object #
 */
static int
zfs_ioc_next_obj(zfs_cmd_t *zc)
{
	objset_t *os = NULL;
	int error;

	error = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (error)
		return (error);

	error = dmu_object_next(os, &zc->zc_obj, B_FALSE,
	    os->os_dsl_dataset->ds_phys->ds_prev_snap_txg);

	dmu_objset_rele(os, FTAG);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_value		prefix name for snapshot
 * zc_cleanup_fd	cleanup-on-exit file descriptor for calling process
 *
 * outputs:
 */
static int
zfs_ioc_tmp_snapshot(zfs_cmd_t *zc)
{
	char *snap_name;
	int error;

	snap_name = kmem_asprintf("%s-%016llx", zc->zc_value,
	    (u_longlong_t)ddi_get_lbolt64());

	if (strlen(snap_name) >= MAXNAMELEN) {
		strfree(snap_name);
		return (E2BIG);
	}

	error = dmu_objset_snapshot(zc->zc_name, snap_name, snap_name,
	    NULL, B_FALSE, B_TRUE, zc->zc_cleanup_fd);
	if (error != 0) {
		strfree(snap_name);
		return (error);
	}

	(void) strcpy(zc->zc_value, snap_name);
	strfree(snap_name);
	return (0);
}

/*
 * inputs:
 * zc_name		name of "to" snapshot
 * zc_value		name of "from" snapshot
 * zc_cookie		file descriptor to write diff data on
 *
 * outputs:
 * dmu_diff_record_t's to the file descriptor
 */
static int
zfs_ioc_diff(zfs_cmd_t *zc)
{
	objset_t *fromsnap;
	objset_t *tosnap;
	file_t *fp;
	offset_t off;
	int error;

	error = dmu_objset_hold(zc->zc_name, FTAG, &tosnap);
	if (error)
		return (error);

	if (zc->zc_value[0] == 0) {
		fromsnap = NULL;
	} else {
		error = dmu_objset_hold(zc->zc_value, FTAG, &fromsnap);
		if (error) {
			dmu_objset_rele(tosnap, FTAG);
			return (error);
		}
	}

	fp = getf(zc->zc_cookie);
	if (fp == NULL) {
		if (fromsnap)
			dmu_objset_rele(fromsnap, FTAG);
		dmu_objset_rele(tosnap, FTAG);
		return (EBADF);
	}

	off = fp->f_offset;

	error = dmu_diff(tosnap, fromsnap, fp->f_vnode, &off);

	if (VOP_SEEK(fp->f_vnode, fp->f_offset, &off, NULL) == 0)
		fp->f_offset = off;
	releasef(zc->zc_cookie);

	if (fromsnap)
		dmu_objset_rele(fromsnap, FTAG);
	dmu_objset_rele(tosnap, FTAG);
	return (error);
}

/*
 * Remove all share resource files in shares dir
 */
static int
zfs_share_resource_purge(znode_t *dzp)
{
	zap_cursor_t	zc;
	zap_attribute_t	zap;
	zfsvfs_t *zfsvfs = dzp->z_zfsvfs;
	int error;

	for (zap_cursor_init(&zc, zfsvfs->z_os, dzp->z_id);
	    (error = zap_cursor_retrieve(&zc, &zap)) == 0;
	    zap_cursor_advance(&zc)) {
		if ((error = VOP_REMOVE(ZTOV(dzp), zap.za_name, kcred,
		    NULL, 0)) != 0)
			break;
	}
	zap_cursor_fini(&zc);
	return (error);
}

static int
zfs_ioc_share_resource(zfs_cmd_t *zc)
{
	vnode_t *vp;
	znode_t *dzp;
	vnode_t *resourcevp = NULL;
	znode_t *sharedir;
	zfsvfs_t *zfsvfs;
	nvlist_t *nvlist;
	char *src, *target;
	vattr_t vattr;
	vsecattr_t vsec;
	int error = 0;

	if ((error = lookupname(zc->zc_value, UIO_SYSSPACE,
	    NO_FOLLOW, NULL, &vp)) != 0)
		return (error);

	/* Now make sure mntpnt and dataset are ZFS */

	if (vp->v_vfsp->vfs_fstype != zfsfstype ||
	    (strcmp((char *)refstr_value(vp->v_vfsp->vfs_resource),
	    zc->zc_name) != 0)) {
		VN_RELE(vp);
		return (EINVAL);
	}

	dzp = VTOZ(vp);
	zfsvfs = dzp->z_zfsvfs;
	ZFS_ENTER(zfsvfs);

	/*
	 * Create share dir if its missing.
	 */
	mutex_enter(&zfsvfs->z_lock);
	if (zfsvfs->z_shares_dir == 0) {
		dmu_tx_t *tx;

		tx = dmu_tx_create(zfsvfs->z_os);
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, TRUE,
		    ZFS_SHARES_DIR);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
		} else {
			error = zfs_create_share_dir(zfsvfs, tx);
			dmu_tx_commit(tx);
		}
		if (error) {
			mutex_exit(&zfsvfs->z_lock);
			VN_RELE(vp);
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}
	mutex_exit(&zfsvfs->z_lock);

	ASSERT(zfsvfs->z_shares_dir);
	if ((error = zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &sharedir)) != 0) {
		VN_RELE(vp);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	switch (zc->zc_cookie) {
	case ZFS_SHARE_RESOURCE_ADD:
		vattr.va_mask = AT_MODE|AT_UID|AT_GID|AT_TYPE;
		vattr.va_type = VREG;
		vattr.va_mode = S_IFREG|0777;
		vattr.va_uid = 0;
		vattr.va_gid = 0;

		error = VOP_CREATE(ZTOV(sharedir), zc->zc_string,
		    &vattr, EXCL, 0, &resourcevp, kcred, 0, NULL, NULL);

		if (error == 0) {
			vsec.vsa_mask = VSA_ACE;
			vsec.vsa_aclentp = &full_access;
			vsec.vsa_aclentsz = sizeof (full_access);
			vsec.vsa_aclcnt = 1;
			(void) VOP_SETSECATTR(resourcevp, &vsec, 0,
			    kcred, NULL);
		}

		if (resourcevp)
			VN_RELE(resourcevp);
		break;

	case ZFS_SHARE_RESOURCE_REMOVE:
		error = VOP_REMOVE(ZTOV(sharedir), zc->zc_string, kcred,
		    NULL, 0);
		break;

	case ZFS_SHARE_RESOURCE_RENAME:
		if ((error = get_nvlist(zc->zc_nvlist_src,
		    zc->zc_nvlist_src_size, zc->zc_iflags, &nvlist)) != 0) {
			VN_RELE(vp);
			ZFS_EXIT(zfsvfs);
			return (error);
		}
		if (nvlist_lookup_string(nvlist, ZFS_SHARE_RESOURCE_SRC,
		    &src) ||
		    nvlist_lookup_string(nvlist, ZFS_SHARE_RESOURCE_TARGET,
		    &target)) {
			VN_RELE(vp);
			VN_RELE(ZTOV(sharedir));
			ZFS_EXIT(zfsvfs);
			nvlist_free(nvlist);
			return (error);
		}
		error = VOP_RENAME(ZTOV(sharedir), src, ZTOV(sharedir), target,
		    kcred, NULL, 0);
		nvlist_free(nvlist);
		break;

	case ZFS_SHARE_RESOURCE_PURGE:
		error = zfs_share_resource_purge(sharedir);
		break;

	case ZFS_SHARE_RESOURCE_READ:
	case ZFS_SHARE_RESOURCE_WRITE: {
		struct uio uio;
		struct iovec iov;
		znode_t *zp;
		u_offset_t va_size;

		src = (char *)(uintptr_t)zc->zc_share.z_sharedata;

		error = VOP_LOOKUP(ZTOV(sharedir), zc->zc_string, &resourcevp,
		    NULL, 0, NULL, kcred, NULL, NULL, NULL);

		if (error != 0)
			break;

		zp = VTOZ(resourcevp);
		mutex_enter(&zp->z_lock);
		va_size = zp->z_size;
		mutex_exit(&zp->z_lock);

		iov.iov_base = src;
		iov.iov_len = (ssize_t)zc->zc_share.z_sharemax;

		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_segflg = UIO_USERSPACE;
		uio.uio_loffset = 0;
		uio.uio_resid = (ssize_t)zc->zc_share.z_sharemax;

		if (uio.uio_resid <= 0) {
			error = EINVAL;
			break;
		}

		if (zc->zc_cookie == ZFS_SHARE_RESOURCE_READ) {
			/*
			 * The entire file must be read, otherwise EINVAL.
			 */
			if (uio.uio_resid != va_size) {
				error = EINVAL;
				break;
			}
			uio.uio_extflg = UIO_COPY_CACHED;
			error = VOP_READ(resourcevp, &uio, 0, kcred, NULL);
		} else {
			/*
			 * The supplied data constitutes the entire file,
			 * if the length is less than the current size then
			 * set the new size after the write.
			 */
			if (uio.uio_resid < va_size) {
				va_size = uio.uio_resid;
			} else {
				va_size = 0;
			}
			uio.uio_llimit = MAXOFFSET_T;
			uio.uio_extflg = UIO_COPY_DEFAULT;
			error = VOP_WRITE(resourcevp, &uio, FDSYNC, kcred,
			    NULL);

			if ((va_size > 0) && (error == 0)) {
				error = zfs_freesp(zp, va_size, 0, 0, FALSE);
			}
		}

		VN_RELE(resourcevp);
		break;
	}

	default:
		error = EINVAL;
		break;
	}

	VN_RELE(vp);
	VN_RELE(ZTOV(sharedir));

	ZFS_EXIT(zfsvfs);

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_value		short name of snap
 * zc_string		user-supplied tag for this hold
 * zc_cookie		recursive flag
 * zc_temphold		set if hold is temporary
 * zc_cleanup_fd	cleanup-on-exit file descriptor for calling process
 * zc_sendobj		if non-zero, the objid for zc_name@zc_value
 * zc_createtxg		if zc_sendobj is non-zero, snap must have zc_createtxg
 *
 * outputs:		none
 */
static int
zfs_ioc_hold(zfs_cmd_t *zc)
{
	boolean_t recursive = zc->zc_cookie;
	spa_t *spa;
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	int error;
	minor_t minor = 0;

	if (snapshot_namecheck(zc->zc_value, NULL, NULL) != 0)
		return (EINVAL);

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error)
		return (error);

	dp = spa_get_dsl(spa);
	if (zc->zc_temphold)
		dsl_pool_tmp_uref_barrier(dp);

	if (zc->zc_sendobj == 0) {
		spa_close(spa, FTAG);
		return (dsl_dataset_user_hold(zc->zc_name, zc->zc_value,
		    zc->zc_string, recursive, zc->zc_temphold,
		    zc->zc_cleanup_fd));
	}

	if (recursive) {
		spa_close(spa, FTAG);
		return (EINVAL);
	}

	rw_enter(&dp->dp_config_rwlock, RW_READER);
	error = dsl_dataset_hold_obj(dp, zc->zc_sendobj, FTAG, &ds);
	rw_exit(&dp->dp_config_rwlock);

	if (error) {
		spa_close(spa, FTAG);
		return (error);
	}

	/*
	 * Until we have a hold on this snapshot, it's possible that
	 * zc_sendobj could've been destroyed and reused as part
	 * of a later txg.  Make sure we're looking at the right object.
	 */
	if (zc->zc_createtxg != ds->ds_phys->ds_creation_txg) {
		dsl_dataset_rele(ds, FTAG);
		spa_close(spa, FTAG);
		return (ENOENT);
	}

	if (zc->zc_cleanup_fd != -1 && zc->zc_temphold) {
		error = zfs_onexit_fd_hold(zc->zc_cleanup_fd, &minor);
		if (error) {
			dsl_dataset_rele(ds, FTAG);
			spa_close(spa, FTAG);
			return (error);
		}
	}

	error = dsl_dataset_user_hold_for_send(ds, zc->zc_string,
	    zc->zc_temphold);
	if (minor != 0) {
		if (error == 0) {
			dsl_register_onexit_hold_cleanup(ds, zc->zc_string,
			    minor);
		}
		zfs_onexit_fd_rele(zc->zc_cleanup_fd);
	}
	dsl_dataset_rele(ds, FTAG);
	spa_close(spa, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name	name of dataset from which we're releasing a user hold
 * zc_value	short name of snap
 * zc_string	user-supplied tag for this hold
 * zc_cookie	recursive flag
 *
 * outputs:	none
 */
static int
zfs_ioc_release(zfs_cmd_t *zc)
{
	boolean_t recursive = zc->zc_cookie;
	spa_t *spa;
	int error;

	if (snapshot_namecheck(zc->zc_value, NULL, NULL) != 0)
		return (EINVAL);

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error)
		return (error);
	dsl_pool_tmp_uref_barrier(spa_get_dsl(spa));
	spa_close(spa, FTAG);

	return (dsl_dataset_user_release(zc->zc_name, zc->zc_value,
	    zc->zc_string, recursive));
}

/*
 * inputs:
 * zc_name		name of filesystem
 *
 * outputs:
 * zc_nvlist_src{_size}	nvlist of snapshot holds
 */
static int
zfs_ioc_get_holds(zfs_cmd_t *zc)
{
	nvlist_t *nvp;
	int error;

	if ((error = dsl_dataset_get_holds(zc->zc_name, &nvp)) == 0) {
		error = put_nvlist(zc, nvp);
		nvlist_free(nvp);
	}

	return (error);
}

/*
 * inputs:
 * zc_name		name of dataset
 * zc_cookie		zprop_setflags_t
 * zc_nvlist_src{_size}	nvlist of proposed property settings
 *
 * outputs:
 * zc_nvlist_dst{_size}	nvlist of predicted effective property values
 */
static int
zfs_ioc_predict_prop(zfs_cmd_t *zc)
{
	nvlist_t *invl, *onvl;
	nvlist_t *do_nothing;
	nvpair_t *pair;
	nvpair_t *next_pair;
	zprop_setflags_t setflags = (zprop_setflags_t)zc->zc_cookie;
	spa_t *spa;
	uint64_t version;
	int err;

	err = spa_open(zc->zc_name, &spa, FTAG);
	if (err)
		return (err);

	version = spa_version(spa);
	spa_close(spa, FTAG);

	if ((err = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &invl)) != 0)
		return (err);

	VERIFY(nvlist_alloc(&do_nothing, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	pair = nvlist_next_nvpair(invl, NULL);
	while (pair != NULL) {
		const char *propname = nvpair_name(pair);
		zfs_prop_t prop = zfs_name_to_prop(propname);
		zprop_source_t source;
		boolean_t validate_value;
		nvlist_t *attrs;
		uint64_t intval;

		next_pair = nvlist_next_nvpair(invl, pair);

		if (prop == ZPROP_INVAL && !zfs_prop_user(propname)) {
			VERIFY(0 == nvlist_remove_nvpair(invl, pair));
			goto next;
		}

		err = nvpair_value_nvlist(pair, &attrs);
		if (err != 0)
			break;

		err = nvlist_lookup_int32(attrs, ZPROP_SOURCE,
		    (int32_t *)&source);
		if (err != 0)
			break;

		switch (source) {
		case ZPROP_SRC_NONE:			/* inherit -S */
		case ZPROP_SRC_INHERITED:		/* inherit */
		case (ZPROP_SRC_NONE | ZPROP_SRC_RECEIVED): /* receive */
			validate_value = B_FALSE;
			break;
		case ZPROP_SRC_LOCAL:			/* set */
		case ZPROP_SRC_RECEIVED:		/* receive */
			validate_value = nvlist_exists(attrs, ZPROP_VALUE);
			break;
		default:
			err = EINVAL;
		}

		if (err != 0)
			break;

		if (validate_value) {
			if ((err = zfs_prop_validate(pair, source)) != 0)
				break;
		} else {
			if (strlen(propname) >= ZAP_MAXNAMELEN) {
				err = ENAMETOOLONG;
				break;
			}
		}

		if (prop == ZFS_PROP_VOLSIZE || prop == ZFS_PROP_VERSION) {
			VERIFY(0 == nvlist_remove_nvpair(invl, pair));
			goto next;
		}

		if (version < SPA_VERSION_RECVD_PROPS &&
		    (prop == ZFS_PROP_QUOTA || prop == ZFS_PROP_RESERVATION)) {
			VERIFY(0 == nvlist_remove_nvpair(invl, pair));
			goto next;
		}

		if (validate_value &&
		    zfs_check_settable(zc->zc_name, pair, CRED()) != 0) {
			VERIFY(0 == nvlist_remove_nvpair(invl, pair));
			goto next;
		}

		if (source == ZPROP_SRC_LOCAL &&
		    (setflags & ZPROP_SET_DESCENDANT) &&
		    (prop == ZFS_PROP_QUOTA ||
		    prop == ZFS_PROP_REFQUOTA ||
		    prop == ZFS_PROP_RESERVATION ||
		    prop == ZFS_PROP_REFRESERVATION) &&
		    nvlist_lookup_uint64(attrs, ZPROP_VALUE, &intval) == 0 &&
		    intval != 0) {
			/*
			 * Non-default size properties already apply to the
			 * entire subtree. Since we don't set them recursively,
			 * remove the value. This avoids the prediction (since
			 * we predict that nothing will change) but still gets
			 * the current effective value.
			 */
			(void) nvlist_remove_all(attrs, ZPROP_VALUE);
			VERIFY(0 == nvlist_add_boolean(do_nothing, propname));
		}

next:
		pair = next_pair;
	}

	if (err == 0)
		err = dsl_props_predict(zc->zc_name, invl, setflags, &onvl);

	nvlist_free(invl);

	if (err != 0) {
		nvlist_free(do_nothing);
		return (err);
	}

	/*
	 * If we didn't get a predicted value, it may be that we predicted that
	 * the property will no longer exist (in the case of a user property).
	 * However, if we deliberately avoided asking for a predicted value
	 * because we recognized a case where we would have skipped updating the
	 * property, copy the current effective value and source to the
	 * predicted value and source.
	 */
	pair = NULL;
	while ((pair = nvlist_next_nvpair(do_nothing, pair)) != NULL) {
		const char *propname = nvpair_name(pair);
		nvlist_t *attrs;
		nvpair_t *match;
		nvpair_t *attr;
		char *sourcestr;

		if (nvlist_lookup_nvpair(onvl, propname, &match) != 0)
			continue;
		if (nvpair_value_nvlist(match, &attrs) != 0)
			continue;
		if (nvlist_lookup_nvpair(attrs, ZPROP_VALUE, &attr) != 0)
			continue;
		if (nvpair_type(attr) == DATA_TYPE_STRING) {
			char *valstr;
			if (nvpair_value_string(attr, &valstr) == 0) {
				VERIFY(0 == nvlist_add_string(attrs,
				    ZPROP_PREDICTED_VALUE, valstr));
			}
		} else {
			uint64_t intval;
			if (nvpair_value_uint64(attr, &intval) == 0) {
				VERIFY(0 == nvlist_add_uint64(attrs,
				    ZPROP_PREDICTED_VALUE, intval));
			}
		}
		if (nvlist_lookup_nvpair(attrs, ZPROP_SOURCE, &attr) != 0)
			continue;
		if (nvpair_value_string(attr, &sourcestr) == 0) {
			VERIFY(0 == nvlist_add_string(attrs,
			    ZPROP_PREDICTED_SOURCE, sourcestr));
		}
	}
	nvlist_free(do_nothing);

	if (zc->zc_nvlist_dst != NULL)
		err = put_nvlist(zc, onvl);

	nvlist_free(onvl);
	return (err);
}

/*
 * pool create, destroy, and export don't log the history as part of
 * zfsdev_ioctl, but rather zfs_ioc_pool_create, and zfs_ioc_pool_export
 * do the logging of those commands.
 */
static zfs_ioc_vec_t zfs_ioc_vec[] = {
	{ zfs_ioc_pool_create, zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_pool_destroy,	zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_pool_import, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_pool_export, zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_pool_configs,	zfs_secpolicy_none, NO_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NONE },
	{ zfs_ioc_pool_stats, zfs_secpolicy_read, POOL_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_pool_tryimport, zfs_secpolicy_config, NO_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NONE },
	{ zfs_ioc_pool_scan, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_pool_freeze, zfs_secpolicy_config, NO_NAME, B_FALSE,
	    POOL_CHECK_READONLY, DATASET_ALIAS_NONE },
	{ zfs_ioc_pool_upgrade,	zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_pool_get_history, zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_vdev_add, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_vdev_remove, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_vdev_set_state, zfs_secpolicy_config,	POOL_NAME, B_TRUE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_vdev_attach, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_vdev_detach, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_vdev_setpath,	zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_vdev_setfru,	zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_objset_stats,	zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    POOL_CHECK_SUSPENDED, DATASET_ALIAS_NAME | DATASET_ALIAS_ORIGIN },
	{ zfs_ioc_objset_zplprops, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME | DATASET_ALIAS_ORIGIN },
	{ zfs_ioc_dataset_list_next, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    POOL_CHECK_SUSPENDED, DATASET_ALIAS_NAME | DATASET_ALIAS_ORIGIN },
	{ zfs_ioc_snapshot_list_next, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    POOL_CHECK_SUSPENDED, DATASET_ALIAS_NAME | DATASET_ALIAS_ORIGIN },
	{ zfs_ioc_set_prop, zfs_secpolicy_none, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_create, zfs_secpolicy_create, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY,
	    DATASET_ALIAS_NAME | DATASET_ALIAS_VALUE | DATASET_ALIAS_CRYPTO },
	{ zfs_ioc_destroy, zfs_secpolicy_destroy, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_rollback, zfs_secpolicy_rollback, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_rename, zfs_secpolicy_rename,	DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY,
	    DATASET_ALIAS_NAME | DATASET_ALIAS_VALUE },
	{ zfs_ioc_recv, zfs_secpolicy_receive, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME |
	    DATASET_ALIAS_VALUE | DATASET_ALIAS_TOP_DS | DATASET_ALIAS_STRING },
	{ zfs_ioc_send, zfs_secpolicy_send, DATASET_NAME, B_TRUE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME | DATASET_ALIAS_VALUE },
	{ zfs_ioc_inject_fault,	zfs_secpolicy_inject, NO_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NONE },
	{ zfs_ioc_clear_fault, zfs_secpolicy_inject, NO_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NONE },
	{ zfs_ioc_inject_list_next, zfs_secpolicy_inject, NO_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NONE },
	{ zfs_ioc_error_log, zfs_secpolicy_inject, POOL_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME},
	{ zfs_ioc_clear, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME | DATASET_ALIAS_STRING },
	{ zfs_ioc_promote, zfs_secpolicy_promote, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY,
	    DATASET_ALIAS_NAME | DATASET_ALIAS_VALUE },
	{ zfs_ioc_destroy_snaps, zfs_secpolicy_destroy_snaps, DATASET_NAME,
	    B_TRUE, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY,
	    DATASET_ALIAS_NAME },
	{ zfs_ioc_snapshot, zfs_secpolicy_snapshot, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_dsobj_to_dsname, zfs_secpolicy_diff, POOL_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME | DATASET_ALIAS_VALUE },
	{ zfs_ioc_obj_to_path, zfs_secpolicy_diff, DATASET_NAME, B_FALSE,
	    POOL_CHECK_SUSPENDED, DATASET_ALIAS_NAME },
	{ zfs_ioc_pool_set_props, zfs_secpolicy_config,	POOL_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_pool_get_props, zfs_secpolicy_read, POOL_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_set_fsacl, zfs_secpolicy_fsacl, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_get_fsacl, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_share_resource, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_inherit_prop, zfs_secpolicy_inherit, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_share_resource, zfs_secpolicy_share_resource, DATASET_NAME,
	    B_FALSE, POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_userspace_one, zfs_secpolicy_userspace_one, DATASET_NAME,
	    B_FALSE, POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_userspace_many, zfs_secpolicy_userspace_many, DATASET_NAME,
	    B_FALSE, POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_userspace_upgrade, zfs_secpolicy_userspace_upgrade,
	    DATASET_NAME, B_FALSE, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY,
	    DATASET_ALIAS_NAME },
	{ zfs_ioc_hold, zfs_secpolicy_hold, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_release, zfs_secpolicy_release, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_get_holds, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    POOL_CHECK_SUSPENDED, DATASET_ALIAS_NAME },
	{ zfs_ioc_objset_recvd_props, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_vdev_split, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_next_obj, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME },
	{ zfs_ioc_diff, zfs_secpolicy_diff, DATASET_NAME, B_FALSE,
	    POOL_CHECK_NONE, DATASET_ALIAS_NAME | DATASET_ALIAS_VALUE },
	{ zfs_ioc_tmp_snapshot, zfs_secpolicy_tmp_snapshot, DATASET_NAME,
	    B_FALSE, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY,
	    DATASET_ALIAS_NAME },
	{ zfs_ioc_bulk_obj_to_stats, zfs_secpolicy_diff, DATASET_NAME, B_FALSE,
	    POOL_CHECK_SUSPENDED, DATASET_ALIAS_NAME },
	{ zfs_ioc_crypto_key_load, zfs_secpolicy_crypto_keyuse,
	    DATASET_NAME, B_TRUE, POOL_CHECK_SUSPENDED, DATASET_ALIAS_NAME },
	{ zfs_ioc_crypto_key_unload, zfs_secpolicy_crypto_keyuse,
	    DATASET_NAME, B_TRUE, POOL_CHECK_SUSPENDED, DATASET_ALIAS_NAME },
	{ zfs_ioc_crypto_key_inherit, zfs_secpolicy_crypto_keyuse,
	    DATASET_NAME, B_TRUE, POOL_CHECK_SUSPENDED, DATASET_ALIAS_NAME },
	{ zfs_ioc_crypto_key_change, zfs_secpolicy_crypto_keychange,
	    DATASET_NAME, B_TRUE, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY,
	    DATASET_ALIAS_NAME },
	{ zfs_ioc_crypto_key_new, zfs_secpolicy_crypto_keychange,
	    DATASET_NAME, B_TRUE, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY,
	    DATASET_ALIAS_NAME },
	{ zfs_ioc_predict_prop, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    B_FALSE, DATASET_ALIAS_NAME },
	{ zfs_ioc_set_fsid, zfs_secpolicy_none, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_get_fsid, zfs_secpolicy_none, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME },
	{ zfs_ioc_remove_fsid, zfs_secpolicy_none, DATASET_NAME, B_TRUE,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, DATASET_ALIAS_NAME }
};

int
zfs_pool_status(const char *name, zfs_ioc_poolcheck_t check)
{
	spa_t *spa;
	int error;

	if (check & POOL_CHECK_NONE)
		return (0);

	error = spa_open(name, &spa, FTAG);
	if (error == 0) {
		if ((check & POOL_CHECK_SUSPENDED) && spa_suspended(spa))
			error = EAGAIN;
		else if ((check & POOL_CHECK_READONLY) && !spa_writeable(spa))
			error = EROFS;
		spa_close(spa, FTAG);
	}
	return (error);
}

/*
 * Find a free minor number.
 */
minor_t
zfsdev_minor_alloc(void)
{
	static minor_t last_minor;
	minor_t m;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	for (m = last_minor + 1; m != last_minor; m++) {
		if (m > ZFSDEV_MAX_MINOR)
			m = 1;
		if (ddi_get_soft_state(zfsdev_state, m) == NULL) {
			last_minor = m;
			return (m);
		}
	}

	return (0);
}

static int
zfs_ctldev_init(dev_t *devp)
{
	minor_t minor;
	zfs_soft_state_t *zs;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));
	ASSERT(getminor(*devp) == 0);

	minor = zfsdev_minor_alloc();
	if (minor == 0)
		return (ENXIO);

	if (ddi_soft_state_zalloc(zfsdev_state, minor) != DDI_SUCCESS)
		return (EAGAIN);

	*devp = makedevice(getemajor(*devp), minor);

	zs = ddi_get_soft_state(zfsdev_state, minor);
	zs->zss_type = ZSST_CTLDEV;
	zfs_onexit_create((zfs_onexit_t **)&zs->zss_data);

	return (0);
}

static void
zfs_ctldev_destroy(zfs_onexit_t *zo, minor_t minor)
{
	zfs_onexit_destroy(zo);

	mutex_enter(&zfsdev_state_lock);
	ddi_soft_state_free(zfsdev_state, minor);
	mutex_exit(&zfsdev_state_lock);
}

void *
zfsdev_get_soft_state(minor_t minor, enum zfs_soft_state_type which)
{
	zfs_soft_state_t *zp;

	zp = ddi_get_soft_state(zfsdev_state, minor);
	if (zp == NULL || zp->zss_type != which)
		return (NULL);

	return (zp->zss_data);
}

static int
zfsdev_open(dev_t *devp, int flag, int otyp, cred_t *cr)
{
	int error = 0;

	if (getminor(*devp) != 0)
		return (zvol_open(devp, flag, otyp, cr));

	/* This is the control device. Allocate a new minor if requested. */
	if (flag & FEXCL) {
		mutex_enter(&zfsdev_state_lock);
		error = zfs_ctldev_init(devp);
		mutex_exit(&zfsdev_state_lock);
	}

	return (error);
}

static int
zfsdev_close(dev_t dev, int flag, int otyp, cred_t *cr)
{
	zfs_onexit_t *zo;
	minor_t minor = getminor(dev);

	if (minor == 0)
		return (0);

	mutex_enter(&zfsdev_state_lock);
	zo = zfsdev_get_soft_state(minor, ZSST_CTLDEV);
	if (zo == NULL) {
		mutex_exit(&zfsdev_state_lock);
		return (zvol_close(dev, flag, otyp, cr));
	}
	mutex_exit(&zfsdev_state_lock);

	zfs_ctldev_destroy(zo, minor);

	return (0);
}

static int
zfsdev_ioctl(dev_t dev, int cmd, intptr_t arg, int flag, cred_t *cr, int *rvalp)
{
	zfs_cmd_t *zc;
	uint_t vec;
	int error, rc;
	minor_t minor = getminor(dev);

	if (minor != 0 &&
	    zfsdev_get_soft_state(minor, ZSST_CTLDEV) == NULL)
		return (zvol_ioctl(dev, cmd, arg, flag, cr, rvalp));

	vec = cmd - ZFS_IOC;
	ASSERT3U(getmajor(dev), ==, ddi_driver_major(zfs_dip));

	if (vec >= sizeof (zfs_ioc_vec) / sizeof (zfs_ioc_vec[0]))
		return (EINVAL);

	zc = kmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);

	error = ddi_copyin((void *)arg, zc, sizeof (zfs_cmd_t), flag);
	if (error != 0)
		error = EFAULT;

	zfs_unalias(zc, cr, zfs_ioc_vec[vec].zvec_zc_alias);

	if ((error == 0) && !(flag & FKIOCTL))
		error = zfs_ioc_vec[vec].zvec_secpolicy(zc, cr);

	/*
	 * Ensure that all pool/dataset names are valid before we pass down to
	 * the lower layers.
	 */
	if (error == 0) {
		zc->zc_name[sizeof (zc->zc_name) - 1] = '\0';
		zc->zc_iflags = flag & FKIOCTL;
		zc->zc_cred = (uintptr_t)cr;
		switch (zfs_ioc_vec[vec].zvec_namecheck) {
		case POOL_NAME:
			if (pool_namecheck(zc->zc_name, NULL, NULL) != 0)
				error = EINVAL;
			error = zfs_pool_status(zc->zc_name,
			    zfs_ioc_vec[vec].zvec_pool_check);
			break;

		case DATASET_NAME:
			if (dataset_namecheck(zc->zc_name, NULL, NULL) != 0)
				error = EINVAL;
			error = zfs_pool_status(zc->zc_name,
			    zfs_ioc_vec[vec].zvec_pool_check);
			break;

		case NO_NAME:
			break;
		}
	}

	if (error == 0)
		error = zfs_ioc_vec[vec].zvec_func(zc);

	zfs_alias(zc, cr, zfs_ioc_vec[vec].zvec_zc_alias);

	rc = ddi_copyout(zc, (void *)arg, sizeof (zfs_cmd_t), flag);
	if (error == 0) {
		if (rc != 0)
			error = EFAULT;
		if (zfs_ioc_vec[vec].zvec_his_log)
			zfs_log_history(zc);
	}

	kmem_free(zc, sizeof (zfs_cmd_t));
	return (error);
}

static int
zfs_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_create_minor_node(dip, "zfs", S_IFCHR, 0,
	    DDI_PSEUDO, 0) == DDI_FAILURE)
		return (DDI_FAILURE);

	zfs_dip = dip;

	ddi_report_dev(dip);

	return (DDI_SUCCESS);
}

static int
zfs_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (spa_busy() || zfs_busy() || zvol_busy())
		return (DDI_FAILURE);

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	zfs_dip = NULL;

	ddi_prop_remove_all(dip);
	ddi_remove_minor_node(dip, NULL);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
zfs_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = zfs_dip;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}

/*
 * OK, so this is a little weird.
 *
 * /dev/zfs is the control node, i.e. minor 0.
 * /dev/zvol/[r]dsk/pool/dataset are the zvols, minor > 0.
 *
 * /dev/zfs has basically nothing to do except serve up ioctls,
 * so most of the standard driver entry points are in zvol.c.
 */
static struct cb_ops zfs_cb_ops = {
	zfsdev_open,	/* open */
	zfsdev_close,	/* close */
	zvol_strategy,	/* strategy */
	nodev,		/* print */
	zvol_dump,	/* dump */
	zvol_read,	/* read */
	zvol_write,	/* write */
	zfsdev_ioctl,	/* ioctl */
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* poll */
	ddi_prop_op,	/* prop_op */
	NULL,		/* streamtab */
	D_NEW | D_MP | D_64BIT,		/* Driver compatibility flag */
	CB_REV,		/* version */
	nodev,		/* async read */
	nodev,		/* async write */
};

static struct dev_ops zfs_dev_ops = {
	DEVO_REV,	/* version */
	0,		/* refcnt */
	zfs_info,	/* info */
	nulldev,	/* identify */
	nulldev,	/* probe */
	zfs_attach,	/* attach */
	zfs_detach,	/* detach */
	nodev,		/* reset */
	&zfs_cb_ops,	/* driver operations */
	NULL,		/* no bus operations */
	NULL,		/* power */
	ddi_quiesce_not_needed,	/* quiesce */
};

static struct modldrv zfs_modldrv = {
	&mod_driverops,
	"ZFS storage pool",
	&zfs_dev_ops
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&zfs_modlfs,
	(void *)&zfs_modldrv,
	NULL
};


uint_t zfs_fsyncer_key;
extern uint_t rrw_tsd_key;

int
_init(void)
{
	int error;

	spa_init(FREAD | FWRITE);
	zfs_init();
	zvol_init();

	if ((error = mod_install(&modlinkage)) != 0) {
		zvol_fini();
		zfs_fini();
		spa_fini();
		return (error);
	}

	tsd_create(&zfs_fsyncer_key, NULL);
	tsd_create(&rrw_tsd_key, NULL);

	error = ldi_ident_from_mod(&modlinkage, &zfs_li);
	ASSERT(error == 0);

	sharefs_secpolicy_register(zfsfstype, zfs_secpolicy_share);

	return (0);
}

int
_fini(void)
{
	int error;

	if (spa_busy() || zfs_busy() || zvol_busy() ||
	    zio_injection_enabled || vdev_active_threads)
		return (EBUSY);

	if ((error = mod_remove(&modlinkage)) != 0)
		return (error);

	sharefs_secpolicy_register(zfsfstype, NULL);
	zvol_fini();
	zfs_fini();
	spa_fini();

	tsd_destroy(&zfs_fsyncer_key);
	ldi_ident_release(zfs_li);
	zfs_li = NULL;


	return (error);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

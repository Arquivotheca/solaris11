/*
 *
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <alloca.h>
#include <limits.h>
#include <fm/topo_mod.h>
#include <fm/fmd_fmri.h>
#include <sys/param.h>
#include <sys/systeminfo.h>
#include <sys/fm/protocol.h>
#include <sys/stat.h>

#include <topo_method.h>
#include <topo_subr.h>
#include <libzfs.h>
#include <zfs.h>

static int zfs_enum(topo_mod_t *, tnode_t *, const char *, topo_instance_t,
    topo_instance_t, void *, void *);
static void zfs_rele(topo_mod_t *, tnode_t *);
static int zfs_fmri_nvl2str(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int zfs_fmri_presence_state(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int zfs_fmri_service_state(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);

const topo_method_t zfs_methods[] = {
	{ TOPO_METH_NVL2STR, TOPO_METH_NVL2STR_DESC, TOPO_METH_NVL2STR_VERSION,
	    TOPO_STABILITY_INTERNAL, zfs_fmri_nvl2str },
	{ TOPO_METH_PRESENCE_STATE, TOPO_METH_PRESENCE_STATE_DESC,
	    TOPO_METH_PRESENCE_STATE_VERSION, TOPO_STABILITY_INTERNAL,
	    zfs_fmri_presence_state },
	{ TOPO_METH_SERVICE_STATE, TOPO_METH_SERVICE_STATE_DESC,
	    TOPO_METH_SERVICE_STATE_VERSION, TOPO_STABILITY_INTERNAL,
	    zfs_fmri_service_state },
	{ NULL }
};

static const topo_modops_t zfs_ops =
	{ zfs_enum, zfs_rele };
static const topo_modinfo_t zfs_info =
	{ ZFS, FM_FMRI_SCHEME_ZFS, ZFS_VERSION, &zfs_ops };

typedef struct zfs_topo_hdl {
	libzfs_handle_t *zt_hdlp;
	pthread_mutex_t zt_mutex;
} zfs_topo_hdl_t;

int
zfs_init(topo_mod_t *mod, topo_version_t version)
{
	zfs_topo_hdl_t *ztp;

	/*
	 * Turn on module debugging output
	 */
	if (getenv("TOPOZFSDEBUG"))
		topo_mod_setdebug(mod);

	topo_mod_dprintf(mod, "initializing zfs builtin\n");

	if (version != ZFS_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (topo_mod_register(mod, &zfs_info, TOPO_VERSION) != 0) {
		topo_mod_dprintf(mod, "failed to register zfs: "
		    "%s\n", topo_mod_errmsg(mod));
		return (-1); /* mod errno already set */
	}

	ztp = topo_mod_zalloc(mod, sizeof (zfs_topo_hdl_t));
	if (ztp == NULL) {
		topo_mod_unregister(mod);
		return (-1);
	}
	ztp->zt_hdlp = libzfs_init();
	(void) pthread_mutex_init(&ztp->zt_mutex, NULL);
	topo_mod_setspecific(mod, (void *)ztp);
	return (0);
}

void
zfs_fini(topo_mod_t *mod)
{
	zfs_topo_hdl_t *ztp = (zfs_topo_hdl_t *)topo_mod_getspecific(mod);
	libzfs_handle_t *hdlp = ztp->zt_hdlp;

	topo_mod_free(mod, ztp, sizeof (zfs_topo_hdl_t));
	if (hdlp)
		libzfs_fini(hdlp);
	topo_mod_unregister(mod);
}


/*ARGSUSED*/
int
zfs_enum(topo_mod_t *mod, tnode_t *pnode, const char *name, topo_instance_t min,
    topo_instance_t max, void *notused1, void *notused2)
{
	/*
	 * Methods are registered, but there is no enumeration.  Should
	 * enumeration be added be sure to cater for global vs non-global
	 * zones.
	 */
	(void) topo_method_register(mod, pnode, zfs_methods);
	return (0);
}

/*ARGSUSED*/
static void
zfs_rele(topo_mod_t *mp, tnode_t *node)
{
	topo_method_unregister_all(mp, node);
}

typedef struct cbdata {
	uint64_t	cb_guid;
	zpool_handle_t	*cb_pool;
} cbdata_t;

static int
find_pool(zpool_handle_t *zhp, void *data)
{
	cbdata_t *cbp = data;

	if (zpool_get_prop_int(zhp, ZPOOL_PROP_GUID, NULL) == cbp->cb_guid) {
		cbp->cb_pool = zhp;
		return (1);
	}

	zpool_close(zhp);

	return (0);
}

static ssize_t
fmri_nvl2str(topo_mod_t *mod, nvlist_t *nvl, char *buf, size_t buflen)
{
	uint64_t pool_guid, vdev_guid;
	cbdata_t cb;
	ssize_t len;
	const char *name;
	char guidbuf[64];
	zfs_topo_hdl_t *ztp = (zfs_topo_hdl_t *)topo_mod_getspecific(mod);
	libzfs_handle_t *hdlp = ztp->zt_hdlp;

	(void) nvlist_lookup_uint64(nvl, FM_FMRI_ZFS_POOL, &pool_guid);

	/*
	 * Attempt to convert the pool guid to a name.
	 */
	cb.cb_guid = pool_guid;
	cb.cb_pool = NULL;

	(void) pthread_mutex_lock(&ztp->zt_mutex);
	if (hdlp != NULL && zpool_iter(hdlp, find_pool, &cb) == 1) {
		name = zpool_get_name(cb.cb_pool);
	} else {
		(void) snprintf(guidbuf, sizeof (guidbuf), "%llx", pool_guid);
		name = guidbuf;
	}
	(void) pthread_mutex_unlock(&ztp->zt_mutex);

	if (nvlist_lookup_uint64(nvl, FM_FMRI_ZFS_VDEV, &vdev_guid) == 0)
		len = snprintf(buf, buflen, "%s://pool=%s/vdev=%llx",
		    FM_FMRI_SCHEME_ZFS, name, vdev_guid);
	else
		len = snprintf(buf, buflen, "%s://pool=%s",
		    FM_FMRI_SCHEME_ZFS, name);

	if (cb.cb_pool)
		zpool_close(cb.cb_pool);

	return (len);
}

/*ARGSUSED*/
static int
zfs_fmri_nvl2str(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *nvl, nvlist_t **out)
{
	ssize_t len;
	char *name = NULL;
	nvlist_t *fmristr;
	uint8_t fmversion;

	if (version > TOPO_METH_NVL2STR_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (nvlist_lookup_uint8(nvl, FM_VERSION, &fmversion) != 0 ||
	    fmversion > FM_ZFS_SCHEME_VERSION)
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));

	if ((len = fmri_nvl2str(mod, nvl, NULL, 0)) == 0 ||
	    (name = topo_mod_alloc(mod, len + 1)) == NULL ||
	    fmri_nvl2str(mod, nvl, name, len + 1) == 0) {
		if (name != NULL)
			topo_mod_free(mod, name, len + 1);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}

	if (topo_mod_nvalloc(mod, &fmristr, NV_UNIQUE_NAME) != 0) {
		topo_mod_free(mod, name, len + 1);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}
	if (nvlist_add_string(fmristr, "fmri-string", name) != 0) {
		topo_mod_free(mod, name, len + 1);
		nvlist_free(fmristr);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}
	topo_mod_free(mod, name, len + 1);
	*out = fmristr;

	return (0);
}

static nvlist_t *
find_vdev_iter(nvlist_t *nv, uint64_t search)
{
	uint_t c, children;
	nvlist_t **child;
	uint64_t guid;
	nvlist_t *ret;

	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid);

	if (search == guid)
		return (nv);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return (NULL);

	for (c = 0; c < children; c++)
		if ((ret = find_vdev_iter(child[c], search)) != 0)
			return (ret);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) != 0)
		return (NULL);

	for (c = 0; c < children; c++)
		if ((ret = find_vdev_iter(child[c], search)) != 0)
			return (ret);

	return (NULL);
}

static nvlist_t *
find_vdev(zpool_handle_t *zhp, uint64_t guid)
{
	nvlist_t *config, *nvroot;

	config = zpool_get_config(zhp, NULL);

	(void) nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &nvroot);

	return (find_vdev_iter(nvroot, guid));
}

static int
zfs_get_presence(topo_mod_t *mod, nvlist_t *fmri)
{
	uint64_t pool_guid, vdev_guid;
	cbdata_t cb;
	int ret;
	zfs_topo_hdl_t *ztp = (zfs_topo_hdl_t *)topo_mod_getspecific(mod);
	libzfs_handle_t *hdlp = ztp->zt_hdlp;

	(void) nvlist_lookup_uint64(fmri, FM_FMRI_ZFS_POOL, &pool_guid);
	cb.cb_guid = pool_guid;
	cb.cb_pool = NULL;
	(void) pthread_mutex_lock(&ztp->zt_mutex);
	if (hdlp == NULL || zpool_iter(hdlp, find_pool, &cb) != 1) {
		(void) pthread_mutex_unlock(&ztp->zt_mutex);
		return (FMD_OBJ_STATE_NOT_PRESENT);
	}
	(void) pthread_mutex_unlock(&ztp->zt_mutex);

	if (nvlist_lookup_uint64(fmri, FM_FMRI_ZFS_VDEV, &vdev_guid) != 0) {
		zpool_close(cb.cb_pool);
		return (FMD_OBJ_STATE_STILL_PRESENT);
	}

	ret = (find_vdev(cb.cb_pool, vdev_guid) != NULL) ?
	    FMD_OBJ_STATE_STILL_PRESENT : FMD_OBJ_STATE_NOT_PRESENT;

	zpool_close(cb.cb_pool);
	return (ret);
}

/*ARGSUSED*/
static int
zfs_fmri_presence_state(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	int state;
	uint8_t fmversion;

	if (nvlist_lookup_uint8(in, FM_VERSION, &fmversion) != 0 ||
	    fmversion > FM_ZFS_SCHEME_VERSION)
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));

	if (version > TOPO_METH_PRESENCE_STATE_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	state = zfs_get_presence(mod, in);

	if (topo_mod_nvalloc(mod, out, NV_UNIQUE_NAME) != 0)
		return (topo_mod_seterrno(mod, EMOD_NVL_INVAL));
	if (nvlist_add_uint32(*out, TOPO_METH_PRESENCE_STATE_RET, state) != 0) {
		nvlist_free(*out);
		return (topo_mod_seterrno(mod, EMOD_NVL_INVAL));
	}
	return (0);
}

static void
zfs_get_state(topo_mod_t *mod, nvlist_t *fmri, int *ret)
{
	uint64_t pool_guid, vdev_guid;
	cbdata_t cb;
	nvlist_t *vd;
	zfs_topo_hdl_t *ztp = (zfs_topo_hdl_t *)topo_mod_getspecific(mod);
	libzfs_handle_t *hdlp = ztp->zt_hdlp;

	(void) nvlist_lookup_uint64(fmri, FM_FMRI_ZFS_POOL, &pool_guid);
	cb.cb_guid = pool_guid;
	cb.cb_pool = NULL;
	(void) pthread_mutex_lock(&ztp->zt_mutex);
	if (hdlp == NULL || zpool_iter(hdlp, find_pool, &cb) != 1) {
		(void) pthread_mutex_unlock(&ztp->zt_mutex);
		*ret = FMD_SERVICE_STATE_OK;
		return;
	}
	(void) pthread_mutex_unlock(&ztp->zt_mutex);

	if (nvlist_lookup_uint64(fmri, FM_FMRI_ZFS_VDEV, &vdev_guid) != 0) {
		*ret = (zpool_get_state(cb.cb_pool) == POOL_STATE_UNAVAIL) ?
		    FMD_SERVICE_STATE_UNUSABLE : FMD_SERVICE_STATE_OK;
		zpool_close(cb.cb_pool);
		return;
	}

	vd = find_vdev(cb.cb_pool, vdev_guid);
	if (vd == NULL) {
		*ret = FMD_SERVICE_STATE_OK;
	} else {
		vdev_stat_t *vs;
		uint_t c;

		*ret = FMD_SERVICE_STATE_UNUSABLE;
		if (nvlist_lookup_uint64_array(vd, ZPOOL_CONFIG_VDEV_STATS,
		    (uint64_t **)&vs, &c) == 0) {
			switch (vs->vs_state) {
			case VDEV_STATE_HEALTHY:
				*ret = FMD_SERVICE_STATE_OK;
				break;
			case VDEV_STATE_DEGRADED:
				*ret = FMD_SERVICE_STATE_DEGRADED;
				break;
			}
		}
	}
	zpool_close(cb.cb_pool);
}

/*ARGSUSED*/
static int
zfs_fmri_service_state(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	int state;
	uint8_t fmversion;

	if (nvlist_lookup_uint8(in, FM_VERSION, &fmversion) != 0 ||
	    fmversion > FM_ZFS_SCHEME_VERSION)
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));

	if (version > TOPO_METH_SERVICE_STATE_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	zfs_get_state(mod, in, &state);

	if (topo_mod_nvalloc(mod, out, NV_UNIQUE_NAME) != 0)
		return (topo_mod_seterrno(mod, EMOD_NVL_INVAL));
	if (nvlist_add_uint32(*out, TOPO_METH_SERVICE_STATE_RET, state) != 0) {
		nvlist_free(*out);
		return (topo_mod_seterrno(mod, EMOD_NVL_INVAL));
	}

	return (0);
}

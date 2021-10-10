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

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/spa.h>
#include <sys/zap.h>
#include <sys/fs/zfs.h>
#ifdef _KERNEL
#include <sys/zone.h>
#endif

#include "zfs_prop.h"

#define	ZPROP_INHERIT_SUFFIX "$inherit"
#define	ZPROP_RECVD_SUFFIX "$recvd"

#ifdef	ZFS_DEBUG
static void dsl_prop_check_prediction(struct dsl_dataset *ds,
    dsl_prop_setarg_t *psa);
#define	DSL_PROP_CHECK_PREDICTION(_ds, _psa)	\
	dsl_prop_check_prediction((_ds), (_psa))
#else
#define	DSL_PROP_CHECK_PREDICTION(_ds, _psa)	/* nothing */
#endif

static int dsl_prop_validate(const char *, zprop_source_t, int, int,
    const void *, uint64_t);
static void dsl_prop_setarg_source(dsl_prop_setarg_t *);
static int dsl_prop_preserve_predict(dsl_dataset_t *, dsl_prop_setarg_t *);
static int dsl_prop_preserve_sync(dsl_dataset_t *, dsl_prop_setarg_t *,
    dmu_tx_t *);

static int
dodefault(const char *propname, int intsz, int numints, void *buf)
{
	zfs_prop_t prop;

	/*
	 * The setonce properties are read-only, but they still
	 * have a default value that can be used as the initial
	 * value.
	 */
	if ((prop = zfs_name_to_prop(propname)) == ZPROP_INVAL ||
	    (zfs_prop_readonly(prop) && !zfs_prop_setonce(prop)))
		return (ENOENT);

	/*
	 * The ZAP uses EOVERFLOW to indicate that it found a value and gave you
	 * as much of it as you asked for, which was less than the entire value.
	 * The DSL asks for 0 integers internally when it doesn't care about the
	 * value and only wants to know whether or not the property exists.
	 */
	if (zfs_prop_get_type(prop) == PROP_TYPE_STRING) {
		const char *defaultstr;

		ASSERT((intsz == 0 && numints == 0) ||
		    (intsz == 1 && numints > 0));

		defaultstr = zfs_prop_default_string(prop);
		if (defaultstr == NULL)
			return (ENOENT);
		if (numints == 0)
			return (EOVERFLOW);
		(void) strncpy(buf, defaultstr, numints);
	} else {
		ASSERT((intsz == 0 && numints == 0) ||
		    (intsz == 8 && numints == 1));

		if (numints == 0)
			return (EOVERFLOW);
		*(uint64_t *)buf = zfs_prop_default_numeric(prop);
	}

	return (0);
}

static int
dsl_prop_get_dd_impl(dsl_dir_t *dd, const char *propname,
    int intsz, int numints, void *buf, char *setpoint,
    boolean_t snapshot, dsl_prop_get_valtype_t valtype, zone_t *zone)
{
	int err = ENOENT;
	dsl_dir_t *target = dd;
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	zfs_prop_t prop;
	boolean_t inheritable;
	boolean_t inheriting = B_FALSE;
	boolean_t check_mountpoint = B_FALSE;
	boolean_t free_setpoint = B_FALSE;
	char *inheritstr;
	char *recvdstr;

	ASSERT(RW_LOCK_HELD(&dd->dd_pool->dp_config_rwlock));

	prop = zfs_name_to_prop(propname);
	/*
	 * If we're getting the mount point on behalf of a non-global zone,
	 * take care not to return a mount point that isn't visible to the
	 * zone. Skip to the default mount point rather than inherit from the
	 * global zone.
	 */
	if (!ISGLOBALZONE(zone) && prop == ZFS_PROP_MOUNTPOINT) {
		check_mountpoint = B_TRUE;
		/*
		 * If it is possible that a default value will be forced, also
		 * be sure that setpoint is being captured as it is needed
		 * in the final forced-default checks.
		 */
		if (setpoint == NULL) {
			setpoint = kmem_alloc(MAXNAMELEN, KM_SLEEP);
			free_setpoint = B_TRUE;
		}
	}

	if (setpoint)
		setpoint[0] = '\0';

	inheritable = (prop == ZPROP_INVAL || zfs_prop_inheritable(prop));
	inheritstr = kmem_asprintf("%s%s", propname, ZPROP_INHERIT_SUFFIX);
	recvdstr = kmem_asprintf("%s%s", propname, ZPROP_RECVD_SUFFIX);

	/*
	 * Note: dd may become NULL, therefore we shouldn't dereference it
	 * after this loop.
	 */
	for (; dd != NULL; dd = dd->dd_parent) {
		boolean_t checked_local = B_FALSE;

		ASSERT(RW_LOCK_HELD(&dd->dd_pool->dp_config_rwlock));

		if (dd != target || snapshot) {
			if (!inheritable ||
			    valtype == DSL_PROP_GET_LOCAL ||
			    valtype == DSL_PROP_GET_RECEIVED)
				break;
			inheriting = B_TRUE;
		}

		switch (valtype) {
		case DSL_PROP_GET_INHERITED:
		case DSL_PROP_GET_REVERTED:
			if (!inheriting) {
				if (valtype == DSL_PROP_GET_REVERTED)
					goto check_received;
				break;
			}
			/* FALLTHRU */
		case DSL_PROP_GET_EFFECTIVE:
		case DSL_PROP_GET_LOCAL:
check_local:		/* check for a local value */
			err = zap_lookup(mos,
			    dd->dd_phys->dd_props_zapobj, propname,
			    intsz, numints, buf);
			if (err == ENOENT) {
				if (valtype == DSL_PROP_GET_LOCAL)
					goto inheritance_loop_end;
			} else {
				if (setpoint != NULL && err == 0)
					dsl_dir_name(dd, setpoint);
				goto inheritance_loop_end;
			}
			checked_local = B_TRUE;
			/* FALLTHRU */
		case DSL_PROP_GET_XINHERITED:
			if (inheriting && !checked_local)
				goto check_local;

			/* check for an explicit inheritance entry */
			err = zap_contains(mos, dd->dd_phys->dd_props_zapobj,
			    inheritstr);
			if (err == 0) {
				/*
				 * An explicit inheritance entry overrides a
				 * received value.
				 */
				break;
			} else if (err == ENOENT) {
				/*
				 * If the caller requested the explicitly
				 * inherited value, then if this is the target
				 * dataset and there is no explicit inheritance
				 * entry, avoid returning the default (since it
				 * was not explicitly inherited).
				 */
				if (valtype == DSL_PROP_GET_XINHERITED &&
				    !inheriting) {
					goto out;
				}
			} else {
				goto out;
			}
			/* FALLTHRU */
		case DSL_PROP_GET_RECEIVED:
check_received:		/* check for a received value */
			err = zap_lookup(mos, dd->dd_phys->dd_props_zapobj,
			    recvdstr, intsz, numints, buf);
			if (err != ENOENT) {
				if (setpoint != NULL && err == 0) {
					if (inheriting) {
						dsl_dir_name(dd, setpoint);
					} else {
						(void) strcpy(setpoint,
						    ZPROP_SOURCE_VAL_RECVD);
					}
				}
				goto inheritance_loop_end;
			}
			break;
		default:
			panic("unexpected valtype: %d", valtype);
		}

		/*
		 * If we found an explicit inheritance entry, err is zero even
		 * though we haven't yet found the value, so reinitializing err
		 * at the end of the loop (instead of at the beginning) ensures
		 * that err has a valid post-loop value.
		 */
		err = ENOENT;
	}
inheritance_loop_end:

	if (err == ENOENT && valtype != DSL_PROP_GET_LOCAL &&
	    valtype != DSL_PROP_GET_RECEIVED)
		err = dodefault(propname, intsz, numints, buf);
out:
	strfree(inheritstr);
	strfree(recvdstr);

	if (err == 0 && setpoint != NULL) {
		if (setpoint[0] != '$') {
			(void) zone_dataset_alias(zone, setpoint, setpoint,
			    MAXNAMELEN);
		}
		/*
		 * Skip to the default value rather than inherit from the
		 * global zone.
		 */
		if (check_mountpoint && buf != NULL &&
		    strcmp(setpoint, ZONE_INVISIBLE_SOURCE) == 0) {
			setpoint[0] = '\0';
			err = dodefault(propname, intsz, numints, buf);
		}
	}
	if (free_setpoint)
		kmem_free(setpoint, MAXNAMELEN);

	return (err);
}

int
dsl_prop_get_dd(dsl_dir_t *dd, const char *propname, int intsz, int numints,
    void *buf, char *setpoint, boolean_t snapshot,
    dsl_prop_get_valtype_t valtype)
{
	return (dsl_prop_get_dd_impl(dd, propname, intsz, numints, buf,
	    setpoint, snapshot, valtype, curzone));
}

#ifdef _KERNEL
int
dsl_prop_get_dd_zone(dsl_dir_t *dd, const char *propname, int intsz,
    int numints, void *buf, char *setpoint, boolean_t snapshot,
    dsl_prop_get_valtype_t valtype, zone_t *zone)
{
	return (dsl_prop_get_dd_impl(dd, propname, intsz, numints, buf,
	    setpoint, snapshot, valtype, zone));
}
#endif

int
dsl_prop_get_ds_zone(dsl_dataset_t *ds, const char *propname,
    int intsz, int numints, void *buf, char *setpoint,
    dsl_prop_get_valtype_t valtype, zone_t *zone)
{
	uint64_t zapobj;
	boolean_t snapshot = B_FALSE;

	ASSERT(RW_LOCK_HELD(&DS_CONFIG_RWLOCK(ds)));
	/*
	 * ds_phys is NULL if the dsl_dataset_t is a dummy instance used only to
	 * pass ds_dir and ds_object.
	 */
	if (ds->ds_phys != NULL && dsl_dataset_is_snapshot(ds))
		snapshot = B_TRUE;
	zapobj = (ds->ds_phys == NULL ? 0 : ds->ds_phys->ds_props_obj);

	if (zapobj != 0) {
		objset_t *mos = DS_META_OBJSET(ds);
		int err = ENOENT;

		ASSERT(snapshot);

		switch (valtype) {
		case DSL_PROP_GET_INHERITED:
			break;
		case DSL_PROP_GET_EFFECTIVE:
		case DSL_PROP_GET_LOCAL:
			/* check for a local value */
			err = zap_lookup(mos, zapobj, propname,
			    intsz, numints, buf);
			if (err == ENOENT) {
				if (valtype == DSL_PROP_GET_LOCAL)
					return (err);
			} else {
				if (setpoint != NULL && err == 0) {
					dsl_dataset_name(ds, setpoint);
					(void) zone_dataset_alias(zone,
					    setpoint, setpoint, MAXNAMELEN);
				}
				return (err);
			}
			/* FALLTHRU */
		case DSL_PROP_GET_XINHERITED: {
			/* check for an explicit inheritance entry */
			char *inheritstr = kmem_asprintf("%s%s", propname,
			    ZPROP_INHERIT_SUFFIX);
			err = zap_contains(mos, zapobj, inheritstr);
			strfree(inheritstr);
			if (err == 0) {
				break;
			} else if (err == ENOENT) {
				if (valtype == DSL_PROP_GET_XINHERITED)
					return (err);
			} else {
				return (err);
			}
		}	/* FALLTHRU */
		case DSL_PROP_GET_REVERTED:
		case DSL_PROP_GET_RECEIVED: {
			/* check for a received value */
			char *recvdstr = kmem_asprintf("%s%s", propname,
			    ZPROP_RECVD_SUFFIX);
			err = zap_lookup(mos, zapobj, recvdstr,
			    intsz, numints, buf);
			strfree(recvdstr);
			if (err != ENOENT) {
				if (setpoint != NULL && err == 0) {
					(void) strcpy(setpoint,
					    ZPROP_SOURCE_VAL_RECVD);
				}
				return (err);
			}
			break;
		}
		default:
			panic("unexpected valtype: %d", valtype);
		}

		/*
		 * Leave it to dsl_prop_get_dd() to determine whether the
		 * property is inheritable.
		 */
	}

	return (dsl_prop_get_dd_zone(ds->ds_dir, propname,
	    intsz, numints, buf, setpoint, snapshot, valtype, zone));
}

int
dsl_prop_get_ds(dsl_dataset_t *ds, const char *propname,
    int intsz, int numints, void *buf, char *setpoint,
    dsl_prop_get_valtype_t valtype)
{
	return (dsl_prop_get_ds_zone(ds, propname, intsz, numints, buf,
	    setpoint, valtype, curzone));
}

int
dsl_prop_exists_ds(dsl_dataset_t *ds, const char *name,
    dsl_prop_get_valtype_t valtype)
{
	int err;

	/*
	 * Rely on the fact that zap_lookup() returns ENOENT before it chokes on
	 * zero intsz so we can avoid allocating space for the unused value.
	 */
	err = dsl_prop_get_ds(ds, name, 0, 0, NULL, NULL, valtype);
	if (err == EOVERFLOW || err == EINVAL)
		err = 0; /* found, but skipped reading the value */
	return (err);
}

/*
 * Register interest in the named property.  We'll call the callback
 * once to notify it of the current property value, and again each time
 * the property changes, until this callback is unregistered.
 *
 * Return 0 on success, errno if the prop is not an integer value.
 */
int
dsl_prop_register(dsl_dataset_t *ds, const char *propname,
    dsl_prop_changed_cb_t *callback, void *cbarg)
{
	dsl_dir_t *dd = ds->ds_dir;
	dsl_pool_t *dp = dd->dd_pool;
	uint64_t value;
	dsl_prop_cb_record_t *cbr;
	int err;
	int need_rwlock;

	need_rwlock = !RW_WRITE_HELD(&dp->dp_config_rwlock);
	if (need_rwlock)
		rw_enter(&dp->dp_config_rwlock, RW_READER);

	err = dsl_prop_get_ds(ds, propname, 8, 1, &value, NULL,
	    DSL_PROP_GET_EFFECTIVE);
	if (err != 0) {
		if (need_rwlock)
			rw_exit(&dp->dp_config_rwlock);
		return (err);
	}

	cbr = kmem_alloc(sizeof (dsl_prop_cb_record_t), KM_SLEEP);
	cbr->cbr_ds = ds;
	cbr->cbr_propname = kmem_alloc(strlen(propname)+1, KM_SLEEP);
	(void) strcpy((char *)cbr->cbr_propname, propname);
	cbr->cbr_func = callback;
	cbr->cbr_arg = cbarg;
	mutex_enter(&dd->dd_lock);
	list_insert_head(&dd->dd_prop_cbs, cbr);
	mutex_exit(&dd->dd_lock);

	cbr->cbr_func(cbr->cbr_arg, value);

	if (need_rwlock)
		rw_exit(&dp->dp_config_rwlock);
	return (0);
}

int
dsl_prop_get_zone(const char *dsname, const char *propname,
    int intsz, int numints, void *buf, char *setpoint, zone_t *zone)
{
	dsl_dataset_t *ds;
	int err;

	err = dsl_dataset_hold(dsname, FTAG, &ds);
	if (err)
		return (err);

	rw_enter(&DS_CONFIG_RWLOCK(ds), RW_READER);
	err = dsl_prop_get_ds_zone(ds, propname, intsz, numints, buf, setpoint,
	    DSL_PROP_GET_EFFECTIVE, zone);
	rw_exit(&DS_CONFIG_RWLOCK(ds));

	dsl_dataset_rele(ds, FTAG);
	return (err);
}

int
dsl_prop_get(const char *dsname, const char *propname,
    int intsz, int numints, void *buf, char *setpoint)
{
	return (dsl_prop_get_zone(dsname, propname, intsz, numints, buf,
	    setpoint, curzone));
}

/*
 * Get the current property value.  It may have changed by the time this
 * function returns, so it is NOT safe to follow up with
 * dsl_prop_register() and assume that the value has not changed in
 * between.
 *
 * Return 0 on success, ENOENT if ddname is invalid.
 */
int
dsl_prop_get_integer(const char *ddname, const char *propname,
    uint64_t *valuep, char *setpoint)
{
	return (dsl_prop_get(ddname, propname, 8, 1, valuep, setpoint));
}

static void
dsl_prop_setarg_init(dsl_prop_setarg_t *psa, const char *propname,
    zprop_source_t source, const void *value, zprop_setflags_t flags)
{
	psa->psa_name = propname;
	psa->psa_source = source;
	psa->psa_value = value;
	psa->psa_flags = flags;
	psa->psa_zone = curzone;
	psa->psa_effective_value = NULL;
	psa->psa_effective_setpoint[0] = '\0';
	psa->psa_predicted = B_FALSE;
}

void
dsl_prop_setarg_init_uint64(dsl_prop_setarg_t *psa, const char *propname,
    zprop_source_t source, uint64_t *value, zprop_setflags_t flags)
{
	dsl_prop_setarg_init(psa, propname, source, value, flags);
	psa->psa_intsz = 8;
	psa->psa_numints = 1;
	psa->psa_effective_numints = 1;
}

void
dsl_prop_setarg_init_string(dsl_prop_setarg_t *psa, const char *propname,
    zprop_source_t source, const char *value, zprop_setflags_t flags)
{
	dsl_prop_setarg_init(psa, propname, source, value, flags);
	psa->psa_intsz = 1;
	psa->psa_numints = (value == NULL ? 0 : strlen(value) + 1);
	psa->psa_effective_numints = 0;
}

/*
 * We've determined that the new property value will also be the effective
 * value, so simply copy it to the effective value.
 */
static int
predict_value_is_effective(dsl_prop_setarg_t *psa)
{
	int err = 0;

	if (psa->psa_intsz == 8) {
		*(uint64_t *)psa->psa_effective_value =
		    *(const uint64_t *)psa->psa_value;
	} else if (psa->psa_intsz == 1) {
		(void) strlcpy((char *)psa->psa_effective_value,
		    (const char *)psa->psa_value, psa->psa_numints);
	} else {
		err = EINVAL;
	}

	return (err);
}

/*
 * Predict the effective value of the given property if it were set with the
 * given value and source.
 *
 * Returns 0 on success, a positive error code on failure.
 *
 * The following sources are recognized:
 *
 * ZPROP_SRC_LOCAL	zfs set
 * ZPROP_SRC_RECEIVED	zfs receive
 * ZPROP_SRC_INHERITED	zfs inherit
 * ZPROP_SRC_NONE	zfs inherit -S
 *
 * (ZPROP_SRC_NONE | ZPROP_SRC_RECEIVED):
 *	clear the received value
 * (ZPROP_SRC_NONE | ZPROP_SRC_LOCAL | ZPROP_SRC_RECEIVED):
 *	clear the local value and the received value
 *
 * psa->psa_value is the value to be set or received. The predicted effective
 * value is returned in psa->psa_effective_value, and the source of the
 * predicted value is returned in psa->psa_effective_setpoint. psa->psa_value is
 * ignored if the source is ZPROP_SRC_INHERITED or if it includes
 * ZPROP_SRC_NONE, and it may be left NULL in that case.
 *
 * If no predicted value is found because there is nothing to inherit, the
 * return value is zero (no error), but the non-value is indiciated by setting
 * psa_effective_numints to zero. This is useful for user properties, which have
 * no default value, because the empty string might be a valid value (in which
 * case psa_effective_numints == 1 to hold the terminating NUL). If a predicted
 * value is found and it is a string, psa_effective_numints is set to the length
 * of the string plus one.
 */
int
dsl_prop_predict(dsl_dataset_t *ds, dsl_prop_setarg_t *psa)
{
	const char *propname = psa->psa_name;
	zprop_source_t source = psa->psa_source;
	uint64_t version;
	int err = 0;

	if (psa->psa_effective_value == NULL)
		return (0);

	if (psa->psa_predicted)
		return (0);

	version = spa_version(DS_SPA(ds));
	if (version < SPA_VERSION_RECVD_PROPS) {
		if ((source & ZPROP_SRC_RECEIVED) &&
		    psa->psa_flags & ZPROP_SET_PRESERVE) {
			/*
			 * Before SPA_VERSION_RECVD_PROPS, we preserve the
			 * current effective value by simply filtering the
			 * property out of both the send stream and the list of
			 * existing properties to clear.
			 */
			err = dsl_prop_get_ds_zone(ds, psa->psa_name,
			    psa->psa_intsz, psa->psa_effective_numints,
			    psa->psa_effective_value,
			    psa->psa_effective_setpoint,
			    DSL_PROP_GET_EFFECTIVE, psa->psa_zone);
			if (err == 0)
				psa->psa_predicted = B_TRUE;
			return (err);
		}

		if (source & ZPROP_SRC_NONE)
			source = ZPROP_SRC_NONE;
		else if (source & ZPROP_SRC_RECEIVED)
			source = ZPROP_SRC_LOCAL;
	}

	psa->psa_effective_setpoint[0] = '\0';

	switch (source) {
	case ZPROP_SRC_NONE:
		/* Revert to the received value, if any. */
		err = dsl_prop_get_ds_zone(ds, propname, psa->psa_intsz,
		    psa->psa_effective_numints, psa->psa_effective_value,
		    psa->psa_effective_setpoint, DSL_PROP_GET_REVERTED,
		    psa->psa_zone);
		break;
	case ZPROP_SRC_LOCAL:
		err = predict_value_is_effective(psa);
		dsl_dataset_name(ds, psa->psa_effective_setpoint);
		(void) zone_dataset_alias(psa->psa_zone,
		    psa->psa_effective_setpoint, psa->psa_effective_setpoint,
		    sizeof (psa->psa_effective_setpoint));
		break;
	case ZPROP_SRC_RECEIVED:
		if (psa->psa_flags & ZPROP_SET_PRESERVE) {
			err = dsl_prop_preserve_predict(ds, psa);
			break;
		}
		/*
		 * If there's no local setting or explicit inheritance, then the
		 * new received value will be the effective value.
		 */
		err = dsl_prop_get_ds_zone(ds, propname, psa->psa_intsz,
		    psa->psa_effective_numints, psa->psa_effective_value,
		    psa->psa_effective_setpoint, DSL_PROP_GET_LOCAL,
		    psa->psa_zone);

		if (err == ENOENT) {
			err = dsl_prop_get_ds_zone(ds, propname, psa->psa_intsz,
			    psa->psa_effective_numints,
			    psa->psa_effective_value,
			    psa->psa_effective_setpoint,
			    DSL_PROP_GET_XINHERITED, psa->psa_zone);

			if (err == ENOENT) {
				err = predict_value_is_effective(psa);
				(void) strcpy(psa->psa_effective_setpoint,
				    ZPROP_SOURCE_VAL_RECVD);
			}
		}
		break;
	case (ZPROP_SRC_NONE | ZPROP_SRC_RECEIVED):
		if (psa->psa_flags & ZPROP_SET_PRESERVE) {
			err = dsl_prop_preserve_predict(ds, psa);
			break;
		}
		/*
		 * We're clearing the received value, so the local setting (if
		 * it exists) remains the effective value.
		 */
		err = dsl_prop_get_ds_zone(ds, propname, psa->psa_intsz,
		    psa->psa_effective_numints, psa->psa_effective_value,
		    psa->psa_effective_setpoint, DSL_PROP_GET_LOCAL,
		    psa->psa_zone);
		if (err != ENOENT)
			break;
		/* FALLTHRU */
	case ZPROP_SRC_INHERITED:
	case (ZPROP_SRC_NONE | ZPROP_SRC_LOCAL | ZPROP_SRC_RECEIVED):
		err = dsl_prop_get_ds_zone(ds, propname, psa->psa_intsz,
		    psa->psa_effective_numints, psa->psa_effective_value,
		    psa->psa_effective_setpoint, DSL_PROP_GET_INHERITED,
		    psa->psa_zone);
		break;
	default:
		cmn_err(CE_PANIC, "unexpected property source: %d", source);
	}

	if (err == ENOENT) {
		psa->psa_effective_numints = 0;
		err = 0;
	} else if (psa->psa_intsz == 1) {
		psa->psa_effective_numints =
		    strlen(psa->psa_effective_value) + 1;
	}

	psa->psa_predicted = (err == 0);
	return (err);
}

/*
 * Predict the effective values of the given properties if they were set with
 * the given values and sources. See dsl_prop_predict() for the list of
 * recognized sources and operations they signify. The input props nvlist should
 * specify each property with the following attributes:
 *
 * property name -> sublist:
 *	ZPROP_VALUE -> new value to be set
 *	ZPROP_SOURCE -> zprop_source_t (int32_t)
 *	optional flag names ... (eg ZPROP_PRESERVE)
 *
 * The returned predictions nvlist includes a sublist for each specified
 * property with the following attributes:
 *
 * property name -> sublist:
 *	ZPROP_VALUE -> current effective value (string or uint64)
 *	ZPROP_SOURCE -> source of the current effective value (string)
 *	ZPROP_PREDICTED_VALUE -> predicted effective value
 *	ZPROP_PREDICTED_SOURCE -> source of the predicted effective value
 *
 * If we predict that a user property will no longer exist (or continue to not
 * exist) after the specified update, that is signified by not including
 * ZPROP_PREDICTED_VALUE and ZPROP_PREDICTED_SOURCE in the sublist for that
 * property. If a user property did not already exist (whether or not it will
 * exist after the specified update), that is signified by not including
 * ZPROP_VALUE and ZPROP_SOURCE.
 *
 * If a prediction is not possible because the input source is LOCAL or RECEIVED
 * and no value is given, the currect effective value and source are still
 * assumed to be useful and are returned without a prediction.
 */
int
dsl_props_predict(const char *dsname, nvlist_t *props,
    zprop_setflags_t setflags, nvlist_t **predictions)
{
	dsl_dataset_t *ds;
	nvlist_t *nvl;
	nvlist_t *attrs;
	nvpair_t *pair;
	char *buf;
	int err;

	err = dsl_dataset_hold(dsname, FTAG, &ds);
	if (err)
		return (err);

	VERIFY(nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_alloc(&attrs, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	buf = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);

	rw_enter(&DS_CONFIG_RWLOCK(ds), RW_READER);

	pair = NULL;
	while ((pair = nvlist_next_nvpair(props, pair)) != NULL) {
		const char *propname = nvpair_name(pair);
		zfs_prop_t prop = zfs_name_to_prop(propname);
		zprop_type_t type;
		boolean_t userprop = B_FALSE;
		dsl_prop_setarg_t psa;
		uint64_t effective_value;
		uint64_t intval = -1ULL;
		char setpoint[MAXNAMELEN];
		nvlist_t *iattrs;
		zprop_source_t source;
		boolean_t is_value_ignored = B_FALSE;
		boolean_t is_value;
		nvpair_t *propval;
		zprop_setflags_t flags = setflags;

		err = nvpair_value_nvlist(pair, &iattrs);
		if (err != 0)
			break;

		err = nvlist_lookup_int32(iattrs, ZPROP_SOURCE,
		    (int32_t *)&source);
		if (err != 0)
			break;

		switch (source) {
		case ZPROP_SRC_NONE:
		case ZPROP_SRC_INHERITED:
		case (ZPROP_SRC_NONE | ZPROP_SRC_RECEIVED):
		case (ZPROP_SRC_NONE | ZPROP_SRC_LOCAL | ZPROP_SRC_RECEIVED):
			is_value_ignored = B_TRUE;
			break;
		case ZPROP_SRC_LOCAL:
		case ZPROP_SRC_RECEIVED:
			is_value_ignored = B_FALSE;
			break;
		default:
			err = EINVAL;
		}

		if (err != 0)
			break;

		/*
		 * If the caller specified a value when we were not expecting
		 * one, we ignore it (assume a dummy value). If, however, we
		 * were expecting a value and the caller did not specify one,
		 * then we skip the prediction but still return the current
		 * effective value.
		 */
		is_value = !is_value_ignored;
		if (nvlist_lookup_nvpair(iattrs, ZPROP_VALUE, &propval) != 0)
			is_value = B_FALSE;

		if (prop == ZPROP_INVAL) {
			if (zfs_prop_user(propname)) {
				userprop = B_TRUE;
				type = PROP_TYPE_STRING;
			} else {
				err = EINVAL;
				break;
			}
		} else {
			type = zfs_prop_get_type(prop);
		}

		dsl_prop_decode_flags(pair, &flags);

		if (type == PROP_TYPE_STRING) {
			char *strval = NULL;

			if (is_value && (err = nvpair_value_string(propval,
			    &strval)) != 0)
				break;

			dsl_prop_setarg_init_string(&psa, propname, source,
			    strval, flags);

			buf[0] = '\0';
			psa.psa_effective_value = buf;
			psa.psa_effective_numints = ZAP_MAXVALUELEN;
		} else {
			if (is_value && (err = nvpair_value_uint64(propval,
			    &intval)) != 0)
				break;

			dsl_prop_setarg_init_uint64(&psa, propname, source,
			    (is_value ? &intval : NULL), flags);

			effective_value = -1ULL;
			psa.psa_effective_value = &effective_value;
		}

		dsl_prop_setarg_source(&psa);

		nvlist_clear(attrs);

		/*
		 * Add the predicted value and source only if a prediction is
		 * possible.
		 */
		if (is_value || is_value_ignored) {
			if ((err = dsl_prop_predict(ds, &psa)) != 0)
				break;

			/*
			 * There is no default value for a user property. By
			 * default, it simply doesn't exist. If inheritance
			 * clears a user property (does not find it in a dataset
			 * ancestor), we indicate that by not including a
			 * predicted value and source.
			 */
			if (!userprop || psa.psa_effective_numints > 0) {
				/* add the predicted value and source */
				if (type == PROP_TYPE_STRING) {
					VERIFY(0 == nvlist_add_string(attrs,
					    ZPROP_PREDICTED_VALUE,
					    (char *)psa.psa_effective_value));
				} else {
					VERIFY(0 == nvlist_add_uint64(attrs,
					    ZPROP_PREDICTED_VALUE,
					    *(uint64_t *)
					    psa.psa_effective_value));
				}
				VERIFY(0 == nvlist_add_string(attrs,
				    ZPROP_PREDICTED_SOURCE,
				    psa.psa_effective_setpoint));
			}
		}

		/*
		 * Add the current effective value and source.
		 *
		 * XXX: We could add the rest of the property stack here:
		 *
		 *   ZPROP_LOCAL_VALUE
		 *   ZPROP_RECEIVED_VALUE
		 *   ZPROP_INHERITED_VALUE
		 *   ZPROP_INHERITED_SOURCE
		 *   ZPROP_DEFAULT_VALUE
		 */
		ASSERT(err == 0);
		if (type == PROP_TYPE_STRING) {
			err = dsl_prop_get_ds_zone(ds, propname, 1,
			    ZAP_MAXVALUELEN, buf, setpoint,
			    DSL_PROP_GET_EFFECTIVE, psa.psa_zone);
			if (err == 0) {
				VERIFY(0 == nvlist_add_string(attrs,
				    ZPROP_VALUE, buf));
			}
		} else {
			err = dsl_prop_get_ds_zone(ds, propname, 8, 1, &intval,
			    setpoint, DSL_PROP_GET_EFFECTIVE, psa.psa_zone);
			if (err == 0) {
				VERIFY(0 == nvlist_add_uint64(attrs,
				    ZPROP_VALUE, intval));
			}
		}
		if (err == 0) {
			VERIFY(0 == nvlist_add_string(attrs, ZPROP_SOURCE,
			    setpoint));
		}
		err = 0;

		VERIFY(nvlist_add_nvlist(nvl, propname, attrs) == 0);
	}

	rw_exit(&DS_CONFIG_RWLOCK(ds));
	dsl_dataset_rele(ds, FTAG);

	kmem_free(buf, ZAP_MAXVALUELEN);
	nvlist_free(attrs);

	if (err == 0)
		*predictions = nvl;
	else
		nvlist_free(nvl);

	return (err);
}

#ifdef	ZFS_DEBUG
static void
dsl_prop_check_prediction(dsl_dataset_t *ds, dsl_prop_setarg_t *psa)
{
	char setpoint[MAXNAMELEN];
	int err;

	if (!psa->psa_predicted)
		return;

	if (spa_version(DS_SPA(ds)) < SPA_VERSION_RECVD_PROPS) {
		zfs_prop_t prop = zfs_name_to_prop(psa->psa_name);

		switch (prop) {
		case ZFS_PROP_QUOTA:
		case ZFS_PROP_RESERVATION:
			return;
		}
	}

	if (psa->psa_intsz == 8) {
		uint64_t intval;
		uint64_t effective_value;

		ASSERT(psa->psa_numints == 1);

		err = dsl_prop_get_ds_zone(ds, psa->psa_name, 8, 1, &intval,
		    setpoint, DSL_PROP_GET_EFFECTIVE, psa->psa_zone);
		effective_value = *(uint64_t *)psa->psa_effective_value;

		if (err == 0 && intval != effective_value) {
			cmn_err(CE_PANIC, "%s property, source: %d, "
			    "predicted effective value: %llu, "
			    "actual effective value: %llu (setpoint: %s)",
			    psa->psa_name, psa->psa_source,
			    (unsigned long long)effective_value,
			    (unsigned long long)intval, setpoint);
		}
	} else {
		char *valstr;

		ASSERT(psa->psa_intsz == 1);

		valstr = kmem_alloc(ZAP_MAXVALUELEN,
		    (KM_NOSLEEP | KM_NORMALPRI));
		if (valstr == NULL)
			return;
		err = dsl_prop_get_ds_zone(ds, psa->psa_name, 1,
		    ZAP_MAXVALUELEN, valstr, setpoint, DSL_PROP_GET_EFFECTIVE,
		    psa->psa_zone);
		if (err == 0 && strncmp(valstr, psa->psa_effective_value,
		    ZAP_MAXVALUELEN) != 0) {
			cmn_err(CE_PANIC, "%s property, source: %d, "
			    "predicted effective value: \"%s\", "
			    "actual effective value: \"%s\" (setpoint: %s)",
			    psa->psa_name, psa->psa_source,
			    (char *)psa->psa_effective_value,
			    valstr, setpoint);
		}
		kmem_free(valstr, ZAP_MAXVALUELEN);
	}

	if (err == 0 && strncmp(setpoint, psa->psa_effective_setpoint,
	    MAXNAMELEN) != 0) {
		cmn_err(CE_PANIC, "%s property, source: %d, "
		    "predicted setpoint \"%s\", actual setpoint \"%s\"",
		    psa->psa_name, psa->psa_source,
		    psa->psa_effective_setpoint, setpoint);
	}
}
#endif

/*
 * Unregister this callback.  Return 0 on success, ENOENT if ddname is
 * invalid, ENOMSG if no matching callback registered.
 */
int
dsl_prop_unregister(dsl_dataset_t *ds, const char *propname,
    dsl_prop_changed_cb_t *callback, void *cbarg)
{
	dsl_dir_t *dd = ds->ds_dir;
	dsl_prop_cb_record_t *cbr;

	mutex_enter(&dd->dd_lock);
	for (cbr = list_head(&dd->dd_prop_cbs);
	    cbr; cbr = list_next(&dd->dd_prop_cbs, cbr)) {
		if (cbr->cbr_ds == ds &&
		    cbr->cbr_func == callback &&
		    cbr->cbr_arg == cbarg &&
		    strcmp(cbr->cbr_propname, propname) == 0)
			break;
	}

	if (cbr == NULL) {
		mutex_exit(&dd->dd_lock);
		return (ENOMSG);
	}

	list_remove(&dd->dd_prop_cbs, cbr);
	mutex_exit(&dd->dd_lock);
	kmem_free((void*)cbr->cbr_propname, strlen(cbr->cbr_propname)+1);
	kmem_free(cbr, sizeof (dsl_prop_cb_record_t));

	return (0);
}

/*
 * Return the number of callbacks that are registered for this dataset.
 */
int
dsl_prop_numcb(dsl_dataset_t *ds)
{
	dsl_dir_t *dd = ds->ds_dir;
	dsl_prop_cb_record_t *cbr;
	int num = 0;

	mutex_enter(&dd->dd_lock);
	for (cbr = list_head(&dd->dd_prop_cbs);
	    cbr; cbr = list_next(&dd->dd_prop_cbs, cbr)) {
		if (cbr->cbr_ds == ds)
			num++;
	}
	mutex_exit(&dd->dd_lock);

	return (num);
}

static void
dsl_prop_changed_notify(dsl_pool_t *dp, uint64_t ddobj,
    const char *propname, uint64_t value, int first)
{
	dsl_dir_t *dd;
	dsl_prop_cb_record_t *cbr;
	objset_t *mos = dp->dp_meta_objset;
	int err;

	ASSERT(RW_WRITE_HELD(&dp->dp_config_rwlock));
	err = dsl_dir_open_obj(dp, ddobj, NULL, FTAG, &dd);
	if (err)
		return;

	if (!first) {
		/*
		 * If the prop is set here, then this change is not
		 * being inherited here or below; stop the recursion.
		 */
		err = zap_contains(mos, dd->dd_phys->dd_props_zapobj, propname);
		if (err == 0) {
			dsl_dir_close(dd, FTAG);
			return;
		}
		ASSERT3U(err, ==, ENOENT);
	}

	mutex_enter(&dd->dd_lock);
	for (cbr = list_head(&dd->dd_prop_cbs); cbr;
	    cbr = list_next(&dd->dd_prop_cbs, cbr)) {
		uint64_t zapobj = cbr->cbr_ds->ds_phys->ds_props_obj;

		if (strcmp(cbr->cbr_propname, propname) != 0)
			continue;

		/*
		 * If the property is set on this ds, then it is not
		 * inherited here; don't call the callback.
		 */
		if (zapobj && 0 == zap_contains(mos, zapobj, propname))
			continue;

		cbr->cbr_func(cbr->cbr_arg, value);
	}
	mutex_exit(&dd->dd_lock);

	if (dd->dd_phys->dd_child_dir_zapobj != 0) {
		zap_cursor_t zc;
		zap_attribute_t *za;
		za = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);
		for (zap_cursor_init(&zc, mos,
		    dd->dd_phys->dd_child_dir_zapobj);
		    zap_cursor_retrieve(&zc, za) == 0;
		    zap_cursor_advance(&zc)) {
			dsl_prop_changed_notify(dp, za->za_first_integer,
			    propname, value, FALSE);
		}
		kmem_free(za, sizeof (zap_attribute_t));
		zap_cursor_fini(&zc);
	}
	dsl_dir_close(dd, FTAG);
}

void
dsl_prop_set_sync(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	dsl_prop_setarg_t *psa = arg2;
	objset_t *mos = DS_META_OBJSET(ds);
	uint64_t zapobj, intval, dummy;
	int isint;
	char valbuf[32];
	char *valstr = NULL;
	char *inheritstr;
	char *recvdstr;
	char *tbuf = NULL;
	int err;
	uint64_t version = spa_version(DS_SPA(ds));
	const char *propname = psa->psa_name;
	zprop_source_t source = psa->psa_source;
	zfs_prop_t prop;

#ifdef	ZFS_DEBUG
	(void) dsl_prop_predict(ds, psa);
#endif
	if (version < SPA_VERSION_RECVD_PROPS) {
		prop = zfs_name_to_prop(propname);
		if (prop == ZFS_PROP_QUOTA || prop == ZFS_PROP_RESERVATION)
			return; /* not stored in the ZAP */

		if (source & ZPROP_SRC_NONE)
			source = ZPROP_SRC_NONE;
		else if (source & ZPROP_SRC_RECEIVED)
			source = ZPROP_SRC_LOCAL;
	}

	isint = (psa->psa_intsz == 8 && psa->psa_numints == 1);

	/*
	 * ds_phys is NULL if the dsl_dataset_t is a dummy instance used only to
	 * pass ds_dir and ds_object.
	 */
	if (ds->ds_phys != NULL && dsl_dataset_is_snapshot(ds)) {
		ASSERT(version >= SPA_VERSION_SNAP_PROPS);
		if (ds->ds_phys->ds_props_obj == 0) {
			dmu_buf_will_dirty(ds->ds_dbuf, tx);
			ds->ds_phys->ds_props_obj =
			    zap_create(mos,
			    DMU_OT_DSL_PROPS, DMU_OT_NONE, 0, tx);
		}
		zapobj = ds->ds_phys->ds_props_obj;
	} else {
		zapobj = ds->ds_dir->dd_phys->dd_props_zapobj;
	}

	inheritstr = kmem_asprintf("%s%s", propname, ZPROP_INHERIT_SUFFIX);
	recvdstr = kmem_asprintf("%s%s", propname, ZPROP_RECVD_SUFFIX);

	switch (source) {
	case ZPROP_SRC_NONE:
		/*
		 * revert to received value, if any (inherit -S)
		 * - remove propname
		 * - remove propname$inherit
		 */
		err = zap_remove(mos, zapobj, propname, tx);
		ASSERT(err == 0 || err == ENOENT);
		err = zap_remove(mos, zapobj, inheritstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		break;
	case ZPROP_SRC_LOCAL:
		/*
		 * remove propname$inherit
		 * set propname -> value
		 */
		err = zap_remove(mos, zapobj, inheritstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		VERIFY(0 == zap_update(mos, zapobj, propname,
		    psa->psa_intsz, psa->psa_numints, psa->psa_value, tx));
		break;
	case ZPROP_SRC_INHERITED:
		/*
		 * explicitly inherit
		 * - remove propname
		 * - set propname$inherit
		 */
		ASSERT((prop = zfs_name_to_prop(propname)) == ZPROP_INVAL ||
		    zfs_prop_inheritable(prop));

		err = zap_remove(mos, zapobj, propname, tx);
		ASSERT(err == 0 || err == ENOENT);
		if (version >= SPA_VERSION_RECVD_PROPS &&
		    dsl_prop_get_ds_zone(ds, ZPROP_HASRECVD, 8, 1, &dummy,
		    NULL, DSL_PROP_GET_EFFECTIVE, psa->psa_zone) == 0) {
			dummy = 0;
			err = zap_update(mos, zapobj, inheritstr,
			    8, 1, &dummy, tx);
			ASSERT(err == 0);
		}
		break;
	case ZPROP_SRC_RECEIVED:
		/*
		 * set propname$recvd -> value
		 */
		if (psa->psa_flags & ZPROP_SET_PRESERVE) {
			err = dsl_prop_preserve_sync(ds, psa, tx);
			if (err != 0)
				break;
		}
		err = zap_update(mos, zapobj, recvdstr,
		    psa->psa_intsz, psa->psa_numints, psa->psa_value, tx);
		ASSERT(err == 0);
		break;
	case (ZPROP_SRC_NONE | ZPROP_SRC_LOCAL | ZPROP_SRC_RECEIVED):
		/*
		 * clear local and received settings
		 * - remove propname
		 * - remove propname$inherit
		 * - remove propname$recvd
		 */
		err = zap_remove(mos, zapobj, propname, tx);
		ASSERT(err == 0 || err == ENOENT);
		err = zap_remove(mos, zapobj, inheritstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		/* FALLTHRU */
	case (ZPROP_SRC_NONE | ZPROP_SRC_RECEIVED):
		/*
		 * remove propname$recvd
		 */
		if (psa->psa_flags & ZPROP_SET_PRESERVE) {
			err = dsl_prop_preserve_sync(ds, psa, tx);
			if (err != 0)
				break;
		}
		err = zap_remove(mos, zapobj, recvdstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		break;
	default:
		cmn_err(CE_PANIC, "unexpected property source: %d", source);
	}

	strfree(inheritstr);
	strfree(recvdstr);

	if (isint) {
		VERIFY3U(0, ==, dsl_prop_get_ds_zone(ds, propname, 8, 1,
		    &intval, NULL, DSL_PROP_GET_EFFECTIVE, psa->psa_zone));

		if (ds->ds_phys != NULL && dsl_dataset_is_snapshot(ds)) {
			dsl_prop_cb_record_t *cbr;
			/*
			 * It's a snapshot; nothing can inherit this
			 * property, so just look for callbacks on this
			 * ds here.
			 */
			mutex_enter(&ds->ds_dir->dd_lock);
			for (cbr = list_head(&ds->ds_dir->dd_prop_cbs); cbr;
			    cbr = list_next(&ds->ds_dir->dd_prop_cbs, cbr)) {
				if (cbr->cbr_ds == ds &&
				    strcmp(cbr->cbr_propname, propname) == 0)
					cbr->cbr_func(cbr->cbr_arg, intval);
			}
			mutex_exit(&ds->ds_dir->dd_lock);
		} else {
			dsl_prop_changed_notify(ds->ds_dir->dd_pool,
			    ds->ds_dir->dd_object, propname, intval, TRUE);
		}

		(void) snprintf(valbuf, sizeof (valbuf),
		    "%lld", (longlong_t)intval);
		valstr = valbuf;
	} else {
		if (source == ZPROP_SRC_LOCAL) {
			valstr = (char *)psa->psa_value;
		} else {
			tbuf = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);
			if (dsl_prop_get_ds_zone(ds, propname, 1,
			    ZAP_MAXVALUELEN, tbuf, NULL,
			    DSL_PROP_GET_EFFECTIVE, psa->psa_zone) == 0)
				valstr = tbuf;
		}
	}

	DSL_PROP_CHECK_PREDICTION(ds, psa);

	spa_history_log_internal((source == ZPROP_SRC_NONE ||
	    source == ZPROP_SRC_INHERITED) ? LOG_DS_INHERIT :
	    LOG_DS_PROPSET, ds->ds_dir->dd_pool->dp_spa, tx,
	    "%s=%s dataset = %llu", propname,
	    (valstr == NULL ? "" : valstr), ds->ds_object);

	if (tbuf != NULL)
		kmem_free(tbuf, ZAP_MAXVALUELEN);
}

/*
 * Ensure that the received value of a property does not change the property's
 * effective value. Although we need to save the new received value, we want to
 * preserve the existing behavior as if the received value had been excluded
 * from the send stream. This supports 'zfs receive -x'.
 *
 * If tx is non-NULL, we're actually setting the property. Otherwise, if it's
 * NULL, we're only predicting the source of the preserved effective value after
 * the specified update. When actually setting the property (tx != NULL),
 * passing predict=B_TRUE requests DEBUG checking to verify that the resulting
 * source of the preserved value is what we expected it be.
 */
static int
dsl_prop_preserve_impl(dsl_dataset_t *ds, dsl_prop_setarg_t *psa, dmu_tx_t *tx,
    boolean_t predict)
{
	zprop_source_t source;
	boolean_t only_predicting = (tx == NULL);
	int err;

	if (!(psa->psa_source & ZPROP_SRC_RECEIVED))
		return (EINVAL);
	if (psa->psa_source & ZPROP_SRC_LOCAL)
		return (EINVAL);

	if (only_predicting) {
		ASSERT(predict);

		if ((psa->psa_effective_value == NULL) || psa->psa_predicted) {
			/*
			 * There's nothing to do because either we already
			 * predicted, or else the caller didn't give us a place
			 * to put the prediction.
			 */
			return (0);
		}

		/*
		 * We predict that the current effective value will not change.
		 * As long as we don't need to promote the existing received
		 * value to an explicitly inherited or local value, the existing
		 * setpoint will also not change.
		 */
		err = dsl_prop_get_ds_zone(ds, psa->psa_name, psa->psa_intsz,
		    psa->psa_effective_numints, psa->psa_effective_value,
		    psa->psa_effective_setpoint, DSL_PROP_GET_EFFECTIVE,
		    psa->psa_zone);
		if (err != 0)
			return (err);

		psa->psa_predicted = B_TRUE;
	}

	/*
	 * If there's a local value, then changing the received value will not
	 * change the effective value, so we don't need to do anything.
	 */
	err = dsl_prop_exists_ds(ds, psa->psa_name, DSL_PROP_GET_LOCAL);
	if (err != ENOENT)
		return (err);

	/* Check for an explicit inheritance entry. */
	err = dsl_prop_exists_ds(ds, psa->psa_name, DSL_PROP_GET_XINHERITED);
	if (err != ENOENT)
		return (err);

	if (only_predicting) {
		/*
		 * If there's no local value or explicit inheritance entry, we
		 * can predict that the source of the property will be the newly
		 * received value.
		 */
		if (psa->psa_source == ZPROP_SRC_RECEIVED) {
			(void) strcpy(psa->psa_effective_setpoint,
			    ZPROP_SOURCE_VAL_RECVD);
		}
	}

	/*
	 * Check for a received value. If one exists, promote it to a local
	 * value so it overrides the new received value. Otherwise, inherit so
	 * that the explicit inheritance entry overrides the new received value,
	 * or set the current effective value locally if it can't be inherited.
	 */
	err = dsl_prop_exists_ds(ds, psa->psa_name, DSL_PROP_GET_RECEIVED);
	if (err == ENOENT) {
		zfs_prop_t prop;
		prop = zfs_name_to_prop(psa->psa_name);
		source = (prop == ZPROP_INVAL || zfs_prop_inheritable(prop)) ?
		    ZPROP_SRC_INHERITED : ZPROP_SRC_LOCAL;
	} else {
		source = ZPROP_SRC_LOCAL;
	}

	if (psa->psa_intsz == 8) {
		uint64_t intval;
		uint64_t effective;

		ASSERT(psa->psa_numints == 1);

		/*
		 * If the received value does not differ from the current
		 * effective value, there's nothing to do.
		 */
		err = dsl_prop_get_ds_zone(ds, psa->psa_name, 8, 1, &effective,
		    NULL, DSL_PROP_GET_EFFECTIVE, psa->psa_zone);

		/*
		 * In the case where ZPROP_SRC_NONE leads to clearing the
		 * current received value, we can avoid promoting the received
		 * value to a local value if the effective value will not
		 * change, which we can tell by comparing with the predicted
		 * inherited value.
		 */
		if (err == 0) {
			if (psa->psa_source & ZPROP_SRC_NONE) {
				err = dsl_prop_get_ds_zone(ds, psa->psa_name,
				    8, 1, &intval, psa->psa_effective_setpoint,
				    DSL_PROP_GET_INHERITED, psa->psa_zone);
			} else {
				intval = *(uint64_t *)psa->psa_value;
			}
		}

		if (err == 0 && intval != effective) {
			dsl_prop_setarg_t preserve_psa;
			uint64_t predicted = -1ULL;

			dsl_prop_setarg_init_uint64(&preserve_psa,
			    psa->psa_name, source, &effective, 0);
			preserve_psa.psa_zone = psa->psa_zone;
			if (predict)
				preserve_psa.psa_effective_value = &predicted;

			if (only_predicting) {
				(void) dsl_prop_predict(ds, &preserve_psa);
			} else {
				dsl_prop_set_sync(ds, &preserve_psa, tx);
			}

			if (preserve_psa.psa_predicted &&
			    psa->psa_effective_value != NULL) {
				ASSERT3U(*(uint64_t *)
				    preserve_psa.psa_effective_value, ==,
				    *(uint64_t *)psa->psa_effective_value);
				(void) strlcpy(psa->psa_effective_setpoint,
				    preserve_psa.psa_effective_setpoint,
				    MAXNAMELEN);
			}
		}
	} else {
		char *valstr;
		char *effective;
		char *buf = NULL;

		ASSERT(psa->psa_intsz == 1);

		effective = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);
		err = dsl_prop_get_ds_zone(ds, psa->psa_name, 1,
		    ZAP_MAXVALUELEN, effective, NULL, DSL_PROP_GET_EFFECTIVE,
		    psa->psa_zone);

		if (err == 0) {
			if (psa->psa_source & ZPROP_SRC_NONE) {
				buf = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);
				err = dsl_prop_get_ds_zone(ds, psa->psa_name,
				    1, ZAP_MAXVALUELEN, buf,
				    psa->psa_effective_setpoint,
				    DSL_PROP_GET_INHERITED, psa->psa_zone);
				valstr = buf;
			} else {
				valstr = (char *)psa->psa_value;
			}
		}

		if (err == 0 && strcmp(valstr, effective) != 0) {
			dsl_prop_setarg_t preserve_psa;
			char *predicted = NULL;

			if (predict) {
				/*
				 * If tx is NULL, we're here only for the
				 * prediction; otherwise it's just nice to have
				 * for DEBUG checking.
				 */
				int kmflags = (tx == NULL ? KM_SLEEP :
				    (KM_NOSLEEP | KM_NORMALPRI));
				predicted = kmem_alloc(ZAP_MAXVALUELEN,
				    kmflags);
				if (predicted != NULL)
					predicted[0] = '\0';
			}

			dsl_prop_setarg_init_string(&preserve_psa,
			    psa->psa_name, source, effective, 0);
			preserve_psa.psa_effective_value = predicted;
			preserve_psa.psa_zone = psa->psa_zone;
			if (predicted != NULL) {
				preserve_psa.psa_effective_numints =
				    ZAP_MAXVALUELEN;
			}
			if (only_predicting) {
				(void) dsl_prop_predict(ds, &preserve_psa);
			} else {
				dsl_prop_set_sync(ds, &preserve_psa, tx);
			}

			if (preserve_psa.psa_predicted &&
			    psa->psa_effective_value != NULL) {
				ASSERT(strncmp((char *)
				    preserve_psa.psa_effective_value,
				    (char *)psa->psa_effective_value,
				    ZAP_MAXVALUELEN) == 0);
				(void) strlcpy(psa->psa_effective_setpoint,
				    preserve_psa.psa_effective_setpoint,
				    MAXNAMELEN);
			}

			if (predicted != NULL)
				kmem_free(predicted, ZAP_MAXVALUELEN);
		}

		kmem_free(effective, ZAP_MAXVALUELEN);
		if (buf != NULL)
			kmem_free(buf, ZAP_MAXVALUELEN);
	}

	if (err == ENOENT)
		err = 0;

	return (err);
}

static int
dsl_prop_preserve_sync(dsl_dataset_t *ds, dsl_prop_setarg_t *psa, dmu_tx_t *tx)
{
	ASSERT(tx != NULL);
#ifdef	ZFS_DEBUG
	/*
	 * Verify that the current effective value does not change and that the
	 * source of the preserved value is what we expect it to be. (Changing
	 * the source is often how we preserve the effective value, for example
	 * promoting a received value to a local value.)
	 */
	return (dsl_prop_preserve_impl(ds, psa, tx, B_TRUE));
#else
	return (dsl_prop_preserve_impl(ds, psa, tx, B_FALSE));
#endif
}

static int
dsl_prop_preserve_predict(dsl_dataset_t *ds, dsl_prop_setarg_t *psa)
{
	return (dsl_prop_preserve_impl(ds, psa, NULL, B_TRUE));
}

static void
dsl_prop_setarg_source(dsl_prop_setarg_t *psa)
{
	/*
	 * If we're setting a property recursively, we want to set it locally on
	 * the top level dataset and inherit it in descendant datasets so the
	 * setting is in one place. Treat ZPROP_SRC_LOCAL as ZPROP_SRC_INHERITED
	 * if the property is inheritable and this is a descendant dataset. This
	 * applies not only to 'zfs set -r', but also 'zfs receive -o' if the
	 * send stream is recursive.
	 */
	if ((psa->psa_source == ZPROP_SRC_LOCAL) &&
	    (psa->psa_flags & ZPROP_SET_DESCENDANT)) {
		zfs_prop_t prop = zfs_name_to_prop(psa->psa_name);
		if (prop == ZPROP_INVAL || zfs_prop_inheritable(prop)) {
			psa->psa_source = ZPROP_SRC_INHERITED;
		}
	}
}

void
dsl_props_set_sync(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	dsl_props_arg_t *pa = arg2;
	nvlist_t *props = pa->pa_props;
	nvpair_t *elem = NULL;

	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		nvpair_t *pair = elem;
		dsl_prop_setarg_t psa;
		uint64_t intval;
#ifdef	ZFS_DEBUG
		uint64_t predicted_intval = -1ULL;
		char *predicted_valstr = NULL;
#endif
		dsl_prop_setarg_init(&psa, nvpair_name(pair), pa->pa_source,
		    NULL, pa->pa_flags);
		psa.psa_zone = pa->pa_zone;

		dsl_prop_decode_flags(pair, &psa.psa_flags);
		VERIFY(0 == dsl_prop_decode_value(&pair));

		dsl_prop_setarg_source(&psa);

		if (nvpair_type(pair) == DATA_TYPE_BOOLEAN) {
			zfs_prop_t prop;
			zprop_type_t type;

			ASSERT((psa.psa_source & ZPROP_SRC_NONE) ||
			    psa.psa_source == ZPROP_SRC_INHERITED);

			prop = zfs_name_to_prop(psa.psa_name);
			if (prop == ZPROP_INVAL) {
				type = PROP_TYPE_STRING;
			} else {
				type = zfs_prop_get_type(prop);
			}

			if (type == PROP_TYPE_STRING) {
				psa.psa_intsz = 1;
			} else {
				psa.psa_intsz = 8;
				psa.psa_numints = 1;
			}
		} else if (nvpair_type(pair) == DATA_TYPE_STRING) {
			VERIFY(nvpair_value_string(pair,
			    (char **)&psa.psa_value) == 0);
			psa.psa_intsz = 1;
			psa.psa_numints = strlen(psa.psa_value) + 1;
		} else {
			VERIFY(nvpair_value_uint64(pair, &intval) == 0);
			psa.psa_value = &intval;
			psa.psa_intsz = sizeof (intval);
			psa.psa_numints = 1;
		}

#ifdef	ZFS_DEBUG
		if (psa.psa_intsz == 8) {
			psa.psa_effective_value = &predicted_intval;
			psa.psa_effective_numints = 1;
		} else {
			ASSERT(psa.psa_intsz == 1);
			predicted_valstr = kmem_alloc(ZAP_MAXVALUELEN,
			    (KM_NOSLEEP | KM_NORMALPRI));
			if (predicted_valstr == NULL) {
				psa.psa_effective_numints = 0;
			} else {
				predicted_valstr[0] = '\0';
				psa.psa_effective_numints = ZAP_MAXVALUELEN;
			}
			psa.psa_effective_value = predicted_valstr;
		}
#endif
		dsl_prop_set_sync(ds, &psa, tx);

#ifdef	ZFS_DEBUG
		if (predicted_valstr != NULL)
			kmem_free(predicted_valstr, ZAP_MAXVALUELEN);
#endif
	}
}

void
dsl_dir_prop_set_uint64_sync(dsl_dir_t *dd, const char *name, uint64_t val,
    dmu_tx_t *tx)
{
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	uint64_t zapobj = dd->dd_phys->dd_props_zapobj;

	ASSERT(dmu_tx_is_syncing(tx));

	VERIFY(0 == zap_update(mos, zapobj, name, sizeof (val), 1, &val, tx));

	dsl_prop_changed_notify(dd->dd_pool, dd->dd_object, name, val, TRUE);

	spa_history_log_internal(LOG_DS_PROPSET, dd->dd_pool->dp_spa, tx,
	    "%s=%llu dataset = %llu", name, (u_longlong_t)val,
	    dd->dd_phys->dd_head_dataset_obj);
}

int
dsl_prop_set(const char *dsname, const char *propname, zprop_source_t source,
    int intsz, int numints, const void *buf, zprop_setflags_t flags)
{
	dsl_dataset_t *ds;
	uint64_t version;
	int err;
	dsl_prop_setarg_t psa;
#ifdef	ZFS_DEBUG
	char *valstr = NULL;
	uint64_t intval = -1ULL;
#endif

	err = dsl_dataset_hold(dsname, FTAG, &ds);
	if (err)
		return (err);

	version = spa_version(DS_SPA(ds));

	/*
	 * We must do these checks before we get to the syncfunc, since
	 * it can't fail.
	 */
	if ((err = dsl_prop_validate(propname, source, intsz, numints, buf,
	    version)) != 0) {
		dsl_dataset_rele(ds, FTAG);
		return (err);
	}

	if (dsl_dataset_is_snapshot(ds) &&
	    version < SPA_VERSION_SNAP_PROPS) {
		dsl_dataset_rele(ds, FTAG);
		return (ENOTSUP);
	}

	dsl_prop_setarg_init(&psa, propname, source, buf, flags);
	dsl_prop_setarg_source(&psa);

	if (intsz == 0) {
		zfs_prop_t prop;
		zprop_type_t type;

		ASSERT((source & ZPROP_SRC_NONE) ||
		    source == ZPROP_SRC_INHERITED);

		prop = zfs_name_to_prop(propname);
		if (prop == ZPROP_INVAL) {
			type = PROP_TYPE_STRING;
		} else {
			type = zfs_prop_get_type(prop);
		}

		if (type == PROP_TYPE_STRING) {
			intsz = 1;
		} else {
			intsz = 8;
			numints = 1;
		}
	}

	psa.psa_intsz = intsz;
	psa.psa_numints = numints;

#ifdef	ZFS_DEBUG
	if (intsz == 8) {
		psa.psa_effective_value = &intval;
		psa.psa_effective_numints = 1;
	} else {
		ASSERT(intsz == 1);
		valstr = kmem_alloc(ZAP_MAXVALUELEN,
		    (KM_NOSLEEP | KM_NORMALPRI));
		if (valstr == NULL) {
			psa.psa_effective_numints = 0;
		} else {
			valstr[0] = '\0';
			psa.psa_effective_numints = ZAP_MAXVALUELEN;
		}
		psa.psa_effective_value = valstr;
	}
#endif
	err = dsl_sync_task_do(ds->ds_dir->dd_pool, NULL, dsl_prop_set_sync, ds,
	    &psa, 2);

	dsl_dataset_rele(ds, FTAG);

#ifdef	ZFS_DEBUG
	if (valstr != NULL)
		kmem_free(valstr, ZAP_MAXVALUELEN);
#endif
	return (err);
}

/*
 * A property is a name and a value, so nvlist is a natural choice to represent
 * a list of properties. However, dsl_prop_get_impl() returns the source of a
 * property along with its value, making it necessary to nest the value in a
 * sublist. For convenience, dsl_prop_set_sync() can read a property as a simple
 * name/value pair or in the form returned by dsl_prop_get_impl(), making it
 * easy to specify properties from scratch or to restore properties as reported
 * previously. This function retrieves the property value from either of two
 * nvpair encodings:
 *
 * 1. name -> value
 *
 * 2. name -> sublist:
 *            ZPROP_VALUE  -> value
 *            ZPROP_SOURCE -> source
 *
 * Returns zero on success, ENOENT if a sublist is found but the expected
 * ZPROP_VALUE key is not found. After a successful return, the in-out pair
 * parameter points to either the given nvpair or a nested nvpair keyed by
 * ZPROP_VALUE, whichever contains the value of the property. Note that in the
 * latter case, the name can no longer be obtained from the nvpair.
 */
int
dsl_prop_decode_value(nvpair_t **pair)
{
	if (nvpair_type(*pair) == DATA_TYPE_NVLIST) {
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(*pair, &attrs) == 0);
		if (nvlist_lookup_nvpair(attrs, ZPROP_VALUE, pair) != 0)
			return (ENOENT);
	}
	return (0);
}

/*
 * Attaches a flag to the specified property by moving its value to a sublist
 * (if it isn't already in a sublist) and adding the flag to that sublist:
 *
 * name -> sublist:
 *         ZPROP_VALUE -> value
 *         flag
 *
 * The in-out pair parameter is set to the newly encoded nvpair if the given
 * nvpair needed to be replaced.
 *
 * The flag is intended to modify the behavior of dsl_prop_set_sync(). It is
 * DATA_TYPE_BOOLEAN because it needs no value; the key suffices to indicate the
 * desired behavior. The following flag names are recognized by
 * dsl_prop_set_sync():
 *
 * ZPROP_PRESERVE: Indicates that regardless of what is updated in the
 *                 property's stack of values, the effective value of the
 *                 property must not change.
 */
int
dsl_prop_encode_flag(nvlist_t *props, nvpair_t **pair, const char *flag)
{
	nvlist_t *attrs;

	if (nvpair_type(*pair) != DATA_TYPE_NVLIST) {
		char *name;
		size_t len;
		uint64_t intval;
		char *valstr;

		len = strlen(nvpair_name(*pair));
		name = kmem_alloc(len + 1, KM_SLEEP);
		(void) strlcpy(name, nvpair_name(*pair), len + 1);

		VERIFY(nvlist_alloc(&attrs, NV_UNIQUE_NAME, KM_SLEEP) == 0);

		switch (nvpair_type(*pair)) {
		case DATA_TYPE_UINT64:
			VERIFY(nvpair_value_uint64(*pair, &intval) == 0);
			VERIFY(nvlist_add_uint64(attrs, ZPROP_VALUE,
			    intval) == 0);
			break;
		case DATA_TYPE_STRING:
			VERIFY(nvpair_value_string(*pair, &valstr) == 0);
			VERIFY(nvlist_add_string(attrs, ZPROP_VALUE,
			    valstr) == 0);
			break;
		default:
			nvlist_free(attrs);
			kmem_free(name, len + 1);
			return (EINVAL);
		}

		VERIFY(nvlist_remove_nvpair(props, *pair) == 0);
		VERIFY(nvlist_add_nvlist(props, name, attrs) == 0);
		nvlist_free(attrs);
		VERIFY(nvlist_lookup_nvpair(props, name, pair) == 0);
		kmem_free(name, len + 1);
	}

	VERIFY(nvpair_value_nvlist(*pair, &attrs) == 0);
	VERIFY(nvlist_add_boolean(attrs, flag) == 0);

	return (0);
}

void
dsl_prop_decode_flags(nvpair_t *pair, zprop_setflags_t *flags)
{
	if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(pair, &attrs) == 0);
		if (nvlist_exists(attrs, ZPROP_PRESERVE))
			*flags |= ZPROP_SET_PRESERVE;
	}
}

static int
dsl_prop_validate(const char *propname, zprop_source_t source,
    int intsz, int numints, const void *buf, uint64_t version)
{
	zfs_prop_t prop = zfs_name_to_prop(propname);

	/* validate the name */
	if (strlen(propname) >= ZAP_MAXNAMELEN)
		return (ENAMETOOLONG);

	/* validate the value type */
	if (prop == ZPROP_INVAL) {
		if (!zfs_prop_user(propname) ||
		    !((intsz == 0) || (intsz == 1 && numints >= 1)))
			return (EINVAL);
	}

	/* validate the source */
	if (source == ZPROP_SRC_INHERITED) {
		if (prop != ZPROP_INVAL && !zfs_prop_inheritable(prop))
			return (EINVAL);
	}

	/* validate the value */
	if (intsz == 0 && numints == 0 && buf == NULL) {
		switch (source) {
		case ZPROP_SRC_NONE:
		case ZPROP_SRC_INHERITED:
		case (ZPROP_SRC_NONE | ZPROP_SRC_LOCAL | ZPROP_SRC_RECEIVED):
		case (ZPROP_SRC_NONE | ZPROP_SRC_RECEIVED):
			break; /* no value needed */
		default:
			return (EINVAL);
		}
	} else if (intsz == 1 && numints >= 1) {
		if (prop != ZPROP_INVAL &&
		    zfs_prop_get_type(prop) != PROP_TYPE_STRING)
			return (EINVAL);

		/* numints includes the terminating NUL */
		if (numints > (version < SPA_VERSION_STMF_PROP ?
		    ZAP_OLDMAXVALUELEN : ZAP_MAXVALUELEN))
			return (E2BIG);
	} else if (intsz == 8 && numints == 1) {
		switch (zfs_prop_get_type(prop)) {
		case PROP_TYPE_NUMBER:
			break;
		case PROP_TYPE_STRING:
			return (EINVAL);
		case PROP_TYPE_INDEX: {
			uint64_t intval = *(uint64_t *)buf;
			const char *unused;

			if (zfs_prop_index_to_string(prop, intval,
			    &unused) != 0)
				return (EINVAL);
			break;
		}
		default:
			cmn_err(CE_PANIC, "unknown property type");
		}
	} else {
		return (EINVAL);
	}
	if (prop == ZFS_PROP_RECORDSIZE &&
	    version < SPA_VERSION_ONE_MEG_BLKSZ &&
	    *(uint64_t *)buf > SPA_128KBLOCKSIZE) {
		return (EDOM);
	}

	return (0);
}

static int
dsl_prop_validate_nvpair_impl(nvpair_t *pair, zprop_source_t source,
    uint64_t version)
{
	const char *propname = nvpair_name(pair);
	char *valstr;
	uint64_t intval;
	int intsz;
	int numints;
	const void *buf;
	int err;

	if ((err = dsl_prop_decode_value(&pair)) != 0)
		return (err);

	if (nvpair_type(pair) == DATA_TYPE_BOOLEAN) {
		intsz = 0;
		numints = 0;
		buf = NULL;
	} else if (nvpair_type(pair) == DATA_TYPE_STRING) {
		VERIFY(nvpair_value_string(pair, &valstr) == 0);
		intsz = 1;
		numints = strlen(valstr) + 1;
		buf = valstr;
	} else if (nvpair_type(pair) == DATA_TYPE_UINT64) {
		VERIFY(nvpair_value_uint64(pair, &intval) == 0);
		intsz = 8;
		numints = 1;
		buf = &intval;
	} else {
		return (EINVAL);
	}

	return (dsl_prop_validate(propname, source, intsz, numints, buf,
	    version));
}

int
dsl_prop_validate_nvpair(nvpair_t *pair, zprop_source_t source)
{
	return (dsl_prop_validate_nvpair_impl(pair, source, -1ULL));
}

int
dsl_props_set(const char *dsname, zprop_source_t source, nvlist_t *props,
    zprop_setflags_t flags)
{
	dsl_dataset_t *ds;
	uint64_t version;
	nvpair_t *elem = NULL;
	dsl_props_arg_t pa;
	int err;

	if (err = dsl_dataset_hold(dsname, FTAG, &ds))
		return (err);
	/*
	 * Do these checks before the syncfunc, since it can't fail.
	 */
	version = spa_version(DS_SPA(ds));
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		if ((err = dsl_prop_validate_nvpair_impl(elem, source,
		    version)) != 0) {
			dsl_dataset_rele(ds, FTAG);
			return (err);
		}
	}

	if (dsl_dataset_is_snapshot(ds) &&
	    version < SPA_VERSION_SNAP_PROPS) {
		dsl_dataset_rele(ds, FTAG);
		return (ENOTSUP);
	}

	pa.pa_props = props;
	pa.pa_source = source;
	pa.pa_flags = flags;
	pa.pa_zone = curzone;

	err = dsl_sync_task_do(ds->ds_dir->dd_pool,
	    NULL, dsl_props_set_sync, ds, &pa, 2);

	dsl_dataset_rele(ds, FTAG);
	return (err);
}

static int
dsl_prop_get_all_impl(objset_t *mos, uint64_t zapobj,
    const char *setpoint, boolean_t snapshot, boolean_t inheriting,
    dsl_prop_get_valtype_t valtype, nvlist_t *nv)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	int err = 0;
	char alias[MAXNAMELEN];

	for (zap_cursor_init(&zc, mos, zapobj);
	    (err = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		nvlist_t *propval;
		zfs_prop_t prop;
		char buf[ZAP_MAXNAMELEN];
		char *valstr;
		const char *suffix;
		const char *propname;
		const char *source;

		suffix = strchr(za.za_name, '$');

		if (suffix == NULL) {
			/*
			 * Skip local properties if we only want received
			 * properties.
			 */
			if (valtype == DSL_PROP_GET_RECEIVED)
				continue;

			propname = za.za_name;
			source = setpoint;
		} else if (strcmp(suffix, ZPROP_INHERIT_SUFFIX) == 0) {
			/*
			 * Skip explicit inheritance entries.
			 */
			continue;
		} else if (strcmp(suffix, ZPROP_RECVD_SUFFIX) == 0) {
			if (valtype == DSL_PROP_GET_LOCAL)
				continue;

			(void) strlcpy(buf, za.za_name,
			    (suffix - za.za_name) + 1);
			propname = buf;

			if (valtype != DSL_PROP_GET_RECEIVED) {
				/* Skip if locally overridden. */
				err = zap_contains(mos, zapobj, propname);
				if (err == 0)
					continue;
				if (err != ENOENT)
					break;

				/* Skip if explicitly inherited. */
				valstr = kmem_asprintf("%s%s", propname,
				    ZPROP_INHERIT_SUFFIX);
				err = zap_contains(mos, zapobj, valstr);
				strfree(valstr);
				if (err == 0)
					continue;
				if (err != ENOENT)
					break;
			}

			source = (inheriting ?
			    setpoint : ZPROP_SOURCE_VAL_RECVD);
		} else {
			/*
			 * For backward compatibility, skip suffixes we don't
			 * recognize.
			 */
			continue;
		}

		prop = zfs_name_to_prop(propname);

		/* Skip non-inheritable properties. */
		if (inheriting && prop != ZPROP_INVAL &&
		    !zfs_prop_inheritable(prop))
			continue;

		/* Skip properties not valid for this type. */
		if (snapshot && prop != ZPROP_INVAL &&
		    !zfs_prop_valid_for_type(prop, ZFS_TYPE_SNAPSHOT))
			continue;

		/* Skip properties already defined. */
		if (nvlist_exists(nv, propname))
			continue;

		VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
		if (ZA_PROP_IS_STR(za)) {
			/*
			 * String property
			 */
			char *tmp = kmem_alloc(za.za_num_integers,
			    KM_SLEEP);
			err = zap_lookup(mos, zapobj,
			    za.za_name, 1, za.za_num_integers, tmp);
			if (err != 0) {
				kmem_free(tmp, za.za_num_integers);
				break;
			}
			VERIFY(nvlist_add_string(propval, ZPROP_VALUE,
			    tmp) == 0);
			kmem_free(tmp, za.za_num_integers);
		} else {
			/*
			 * Integer property
			 */
			ASSERT(ZA_PROP_IS_INT(za));
			(void) nvlist_add_uint64(propval, ZPROP_VALUE,
			    za.za_first_integer);
		}

		if (!INGLOBALZONE(curproc) && source[0] != '$') {
			(void) zone_dataset_alias(curzone, source, alias,
			    sizeof (alias));
			/*
			 * If this dataset is a top-level delegated dataset, we
			 * must not look any higher in the inheritance
			 * hierarchy for the mount point property. Skip past
			 * the global zone to the default value rather than
			 * inherit mount point from the global zone.
			 */
			if (prop == ZFS_PROP_MOUNTPOINT &&
			    strcmp(alias, ZONE_INVISIBLE_SOURCE) == 0) {
				VERIFY(nvlist_add_string(propval, ZPROP_VALUE,
				    zfs_prop_default_string(prop)) == 0);
				alias[0] = '\0';
			}
			source = alias;
		}

		VERIFY(nvlist_add_string(propval, ZPROP_SOURCE, source) == 0);
		VERIFY(nvlist_add_nvlist(nv, propname, propval) == 0);
		nvlist_free(propval);
	}
	zap_cursor_fini(&zc);
	if (err == ENOENT)
		err = 0;
	return (err);
}

/*
 * Iterate over all properties for this dataset and return them in an nvlist.
 */
static int
dsl_prop_get_all_ds(dsl_dataset_t *ds, nvlist_t **nvp,
    dsl_prop_get_valtype_t valtype)
{
	dsl_dir_t *dd = ds->ds_dir;
	dsl_pool_t *dp = dd->dd_pool;
	objset_t *mos = dp->dp_meta_objset;
	nvlist_t *props;
	int err = 0;
	char setpoint[MAXNAMELEN];
	boolean_t snapshot = B_FALSE;
	boolean_t inheriting = B_FALSE;

	/* check supported types */
	ASSERT(valtype != DSL_PROP_GET_INHERITED);
	ASSERT(valtype != DSL_PROP_GET_XINHERITED);
	ASSERT(valtype != DSL_PROP_GET_REVERTED);

	VERIFY(nvlist_alloc(&props, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	if (dsl_dataset_is_snapshot(ds))
		snapshot = B_TRUE;

	rw_enter(&dp->dp_config_rwlock, RW_READER);

	if (ds->ds_phys->ds_props_obj != 0) {
		ASSERT(snapshot);
		dsl_dataset_name(ds, setpoint);
		err = dsl_prop_get_all_impl(mos, ds->ds_phys->ds_props_obj,
		    setpoint, snapshot, inheriting, valtype, props);
		if (err)
			goto out;
	}

	for (; dd != NULL; dd = dd->dd_parent) {
		if (dd != ds->ds_dir || snapshot) {
			if (valtype == DSL_PROP_GET_LOCAL ||
			    valtype == DSL_PROP_GET_RECEIVED)
				break;
			inheriting = B_TRUE;
		}
		dsl_dir_name(dd, setpoint);
		err = dsl_prop_get_all_impl(mos, dd->dd_phys->dd_props_zapobj,
		    setpoint, snapshot, inheriting, valtype, props);
		if (err)
			break;
	}
out:
	rw_exit(&dp->dp_config_rwlock);

	if (err)
		nvlist_free(props);
	else
		*nvp = props;
	return (err);
}

static boolean_t
dsl_prop_get_hasrecvd_impl(objset_t *os, dsl_prop_get_valtype_t valtype)
{
	dsl_dataset_t *ds = os->os_dsl_dataset;
	int rc;
	uint64_t intval;

	/*
	 * Because of an earlier bug, a dataset might be inheriting
	 * ZPROP_HASRECVD where it should be set locally.
	 */
	rw_enter(&DS_CONFIG_RWLOCK(ds), RW_READER);
	rc = dsl_prop_get_ds(ds, ZPROP_HASRECVD, 8, 1, &intval, NULL, valtype);
	rw_exit(&DS_CONFIG_RWLOCK(ds));
	ASSERT((valtype == DSL_PROP_GET_EFFECTIVE && rc == 0 &&
	    (intval == -1ULL || (intval == 0 &&
	    spa_version(os->os_spa) >= SPA_VERSION_RECVD_PROPS))) ||
	    (valtype == DSL_PROP_GET_LOCAL && (rc == ENOENT ||
	    (rc == 0 && intval == 0 &&
	    spa_version(os->os_spa) >= SPA_VERSION_RECVD_PROPS))));

	/*
	 * Logically, $hasrecvd has no default value; we consider it unset if we
	 * resorted to the "default" value of -1. From the user perspective, the
	 * property is always 1 (set) or 0 (unset) for all datasets, but
	 * internally, zero is the only value, and we only expect to find it
	 * where the property is set.
	 */
	return (rc == 0 && intval == 0);
}

boolean_t
dsl_prop_get_hasrecvd(objset_t *os)
{
	return (dsl_prop_get_hasrecvd_impl(os, DSL_PROP_GET_EFFECTIVE));
}

/*
 * Adds the given dataset's $hasrecvd property to the given property list. If
 * the given list already has a $hasrecvd entry, the dataset's current value
 * replaces it (or removes it if no current value is found).
 */
static void
dsl_prop_nvlist_add_hasrecvd(dsl_dataset_t *ds, nvlist_t *props)
{
	nvlist_t *attrs;
	char source[MAXNAMELEN];
	uint64_t intval = -1ULL;
	int err;

	rw_enter(&DS_CONFIG_RWLOCK(ds), RW_READER);
	err = dsl_prop_get_ds(ds, ZPROP_HASRECVD, 8, 1, &intval, source,
	    DSL_PROP_GET_EFFECTIVE);
	rw_exit(&DS_CONFIG_RWLOCK(ds));

	VERIFY(err == 0);
	VERIFY(nvlist_alloc(&attrs, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint64(attrs, ZPROP_VALUE,
	    (intval == 0 ? 1 : 0)) == 0);
	VERIFY(nvlist_add_string(attrs, ZPROP_SOURCE, source) == 0);
	VERIFY(nvlist_add_nvlist(props, ZPROP_HASRECVD, attrs) == 0);
	nvlist_free(attrs);
}

static void
dsl_prop_set_hasrecvd_impl(objset_t *os, zprop_source_t source)
{
	dsl_dataset_t *ds = os->os_dsl_dataset;
	uint64_t dummy = 0;
	dsl_prop_setarg_t psa;

	if (spa_version(os->os_spa) < SPA_VERSION_RECVD_PROPS)
		return;

	dsl_prop_setarg_init_uint64(&psa, ZPROP_HASRECVD, source, &dummy, 0);

	(void) dsl_sync_task_do(ds->ds_dir->dd_pool, NULL,
	    dsl_prop_set_sync, ds, &psa, 2);
}

#ifdef	DEBUG
static boolean_t dsl_prop_old_set_hasrecvd;
#endif

/*
 * Call after successfully receiving properties to ensure that only the first
 * receive on or after SPA_VERSION_RECVD_PROPS blows away local properties.
 */
void
dsl_prop_set_hasrecvd(objset_t *os)
{
	dsl_prop_get_valtype_t valtype = DSL_PROP_GET_LOCAL;

#ifdef	DEBUG
	/*
	 * An earlier bug neglected to set the internal $hasrecvd property
	 * recursively. This variable allows us to set it the old way, so we can
	 * verify that renames promote $hasrecvd to a local setting if $hasrecvd
	 * was inherited and the rename would lose the inheritance source.
	 */
	if (dsl_prop_old_set_hasrecvd)
		valtype = DSL_PROP_GET_EFFECTIVE;
#endif
	if (dsl_prop_get_hasrecvd_impl(os, valtype)) {
		ASSERT(spa_version(os->os_spa) >= SPA_VERSION_RECVD_PROPS);
		return;
	}
	dsl_prop_set_hasrecvd_impl(os, ZPROP_SRC_LOCAL);
}

void
dsl_prop_unset_hasrecvd(objset_t *os)
{
	dsl_prop_set_hasrecvd_impl(os, ZPROP_SRC_NONE);
}

/*
 * Adds the given dataset's $share2 property to the given property list. If
 * the given list already has a $share2 entry, the dataset's current value
 * replaces it (or removes it if no current value is found).
 */
static void
dsl_prop_nvlist_add_share2(dsl_dataset_t *ds, nvlist_t *props)
{
	nvlist_t *attrs;
	char source[MAXNAMELEN];
	uint64_t intval = -1ULL;
	int err;

	rw_enter(&DS_CONFIG_RWLOCK(ds), RW_READER);
	err = dsl_prop_get_ds(ds, ZPROP_SHARE2, 8, 1, &intval, source,
	    DSL_PROP_GET_EFFECTIVE);
	rw_exit(&DS_CONFIG_RWLOCK(ds));

	VERIFY(err == 0);
	VERIFY(nvlist_alloc(&attrs, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint64(attrs, ZPROP_VALUE, intval) == 0);
	VERIFY(nvlist_add_string(attrs, ZPROP_SOURCE, source) == 0);
	VERIFY(nvlist_add_nvlist(props, ZPROP_SHARE2, attrs) == 0);
	nvlist_free(attrs);
}

int
dsl_prop_get_all(objset_t *os, nvlist_t **nvp)
{
	int err;

	err = dsl_prop_get_all_ds(os->os_dsl_dataset, nvp,
	    DSL_PROP_GET_EFFECTIVE);

	if (err == 0) {
		dsl_prop_nvlist_add_hasrecvd(os->os_dsl_dataset, *nvp);
		dsl_prop_nvlist_add_share2(os->os_dsl_dataset, *nvp);
	}
	return (err);
}

int
dsl_prop_get_all_raw(objset_t *os, nvlist_t **nvp)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	int err = 0;
	uint64_t zapobj = os->os_dsl_dataset->ds_dir->dd_phys->dd_props_zapobj;
	objset_t *mos = DS_META_OBJSET(os->os_dsl_dataset);

	ASSERT(!dsl_dataset_is_snapshot(os->os_dsl_dataset));
	VERIFY(nvlist_alloc(nvp, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	for (zap_cursor_init(&zc, mos, zapobj);
	    (err = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		if (ZA_PROP_IS_STR(za)) {
			/*
			 * String property
			 */
			char *tmp = kmem_alloc(za.za_num_integers,
			    KM_SLEEP);
			err = zap_lookup(mos, zapobj,
			    za.za_name, 1, za.za_num_integers, tmp);
			if (err != 0) {
				kmem_free(tmp, za.za_num_integers);
				break;
			}
			VERIFY(nvlist_add_string(*nvp, za.za_name, tmp) == 0);
			kmem_free(tmp, za.za_num_integers);
		} else {
			/*
			 * Integer property
			 */
			ASSERT(ZA_PROP_IS_INT(za));
			VERIFY(nvlist_add_uint64(*nvp, za.za_name,
			    za.za_first_integer) == 0);
		}
	}
	zap_cursor_fini(&zc);

	if (err == ENOENT)
		err = 0;
	if (err)
		nvlist_free(*nvp);

	return (err);
}

/*
 * Record property name for callback, but don't include special
 * entries that start with '$'.
 */
static void
dsl_record_propname(nvlist_t *intprops, const char *propname)
{
	char buf[ZAP_MAXNAMELEN];
	const char *suffix;

	if (*propname != '$') {
		suffix = strchr(propname, '$');
		if (suffix != NULL) {
			VERIFY(strlcpy(buf, propname,
			    (suffix - propname) + 1) < ZAP_MAXNAMELEN);
			propname = buf;
		}
		VERIFY(nvlist_add_boolean(intprops, propname) == 0);
	}
}

static void
dsl_prop_set_all_raw_sync(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dir_t *dd = arg1;
	nvlist_t *nv = arg2;
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	uint64_t zapobj = dd->dd_phys->dd_props_zapobj;
	nvpair_t *pair;
	nvlist_t *intprops;
	const char *propname;
	zap_cursor_t zc;
	zap_attribute_t za;

	/*
	 * Prepare to record the names of properties needing a callback after
	 * being restored.
	 */
	VERIFY(nvlist_alloc(&intprops, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	/*
	 * It's not enough to record properties in the recreated ZAP. We also
	 * need to record properties in the previous ZAP, since their effective
	 * values change to the default if they no longer have a ZAP entry, and
	 * that change also requires a callback.
	 */
	for (zap_cursor_init(&zc, mos, zapobj);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {
		if (ZA_PROP_IS_INT(za)) {
			/* Record property name for callback. */
			dsl_record_propname(intprops, za.za_name);
		}
	}
	zap_cursor_fini(&zc);

	/* Clear the ZAP by freeing the ZAP object and recreating it. */
	dmu_buf_will_dirty(dd->dd_dbuf, tx);
	VERIFY(0 == zap_destroy(mos, zapobj, tx));
	dd->dd_phys->dd_props_zapobj = zap_create(mos, DMU_OT_DSL_PROPS,
	    DMU_OT_NONE, 0, tx);
	zapobj = dd->dd_phys->dd_props_zapobj;

	/* Add entries from nvlist. */
	pair = NULL;
	while ((pair = nvlist_next_nvpair(nv, pair)) != NULL) {
		if (nvpair_type(pair) == DATA_TYPE_STRING) {
			char *val;
			VERIFY(nvpair_value_string(pair, &val) == 0);
			VERIFY3U(0, ==, zap_add(mos, zapobj,
			    nvpair_name(pair), 1, strlen(val) + 1, val, tx));
		} else if (nvpair_type(pair) == DATA_TYPE_UINT64) {
			uint64_t val;

			propname = nvpair_name(pair);
			VERIFY(nvpair_value_uint64(pair, &val) == 0);
			VERIFY3U(0, ==, zap_add(mos, zapobj,
			    propname, 8, 1, &val, tx));
			/* Record property name for callback. */
			dsl_record_propname(intprops, propname);
		} else {
			ASSERT(!"invalid nvlist");
		}
	}

	/* Pass the effective values to any required callbacks. */
	pair = NULL;
	while ((pair = nvlist_next_nvpair(intprops, pair)) != NULL) {
		uint64_t intval;

		propname = nvpair_name(pair);
		/* Skip non-settable properties. */
		if (dodefault(propname, 8, 1, &intval) != 0)
			continue;

		VERIFY(0 == dsl_prop_get_dd(dd, propname, 8, 1, &intval, NULL,
		    B_FALSE, DSL_PROP_GET_EFFECTIVE));
		dsl_prop_changed_notify(dd->dd_pool,
		    dd->dd_object, propname, intval, TRUE);
	}

	nvlist_free(intprops);
}

/*
 * The nvlist should come from dsl_prop_get_all_raw.  Any existing
 * properties will be removed, and replaced with the
 * previously-retrieved properties.
 */
int
dsl_prop_set_all_raw(objset_t *os, nvlist_t *nv)
{
	ASSERT(!dsl_dataset_is_snapshot(os->os_dsl_dataset));

	return (dsl_sync_task_do(DS_POOL(os->os_dsl_dataset), NULL,
	    dsl_prop_set_all_raw_sync, os->os_dsl_dataset->ds_dir, nv, 2));
}

/*
 * Adds the quota and reservation properties to the given property list assumed
 * to contain the dataset's local settings obtained from the ZAP. Getting the
 * quota and reservation, which were not stored in the ZAP prior to
 * SPA_VERSION_RECVD_PROPS, ensures that the caller will not neglect to stash
 * them away along with settings from the ZAP before an attempted receive.
 * Otherwise, if quota and reservation were not in the ZAP, the caller would be
 * unable to restore them in case of an error. This situation remains possible
 * after SPA_VERSION_RECVD_PROPS because the properties may not have been set or
 * received since the pool was upgraded. Quota and reservation are obtained
 * regardless of version via dsl_dir_stats(), in case they are not already in
 * the ZAP. If they are in the ZAP, there's no harm in overwriting the property
 * list, since dsl_dir_stats() and the ZAP agree.
 */
static void
dsl_prop_add_stats(objset_t *os, nvlist_t *local)
{
	const char *names[] = { "quota", "reservation" };
	nvlist_t *stats;
	int i;

	VERIFY(nvlist_alloc(&stats, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	if (os->os_dsl_dataset->ds_dir == NULL)
		return;

	dsl_dir_stats(os->os_dsl_dataset->ds_dir, stats);

	for (i = 0; i < 2; i++) {
		nvpair_t *pair;

		if (nvlist_lookup_nvpair(stats, names[i], &pair) == 0) {
			nvpair_t *propval = pair;
			uint64_t intval;

			if (dsl_prop_decode_value(&propval) == 0 &&
			    nvpair_value_uint64(propval, &intval) == 0 &&
			    intval != 0) {
				VERIFY(nvlist_add_nvpair(local, pair) == 0);
			}
		}
	}

	nvlist_free(stats);
}

static int
dsl_prop_get_local(objset_t *os, nvlist_t **nvp)
{
	int err = dsl_prop_get_all_ds(os->os_dsl_dataset, nvp,
	    DSL_PROP_GET_LOCAL);
	if (err == 0)
		dsl_prop_add_stats(os, *nvp);
	return (err);
}

int
dsl_prop_get_received(objset_t *os, nvlist_t **nvp)
{
	/*
	 * Received properties are not distinguishable from local properties
	 * until the dataset has received properties on or after
	 * SPA_VERSION_RECVD_PROPS.
	 */
	if (!dsl_prop_get_hasrecvd(os))
		return (dsl_prop_get_local(os, nvp));

	return (dsl_prop_get_all_ds(os->os_dsl_dataset, nvp,
	    DSL_PROP_GET_RECEIVED));
}

void
dsl_prop_nvlist_add_uint64(nvlist_t *nv, zfs_prop_t prop, uint64_t value)
{
	nvlist_t *propval;
	const char *propname = zfs_prop_to_name(prop);
	uint64_t default_value;

	if (nvlist_lookup_nvlist(nv, propname, &propval) == 0) {
		VERIFY(nvlist_add_uint64(propval, ZPROP_VALUE, value) == 0);
		return;
	}

	VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint64(propval, ZPROP_VALUE, value) == 0);
	/* Indicate the default source if we can. */
	if (dodefault(propname, 8, 1, &default_value) == 0 &&
	    value == default_value) {
		VERIFY(nvlist_add_string(propval, ZPROP_SOURCE, "") == 0);
	}
	VERIFY(nvlist_add_nvlist(nv, propname, propval) == 0);
	nvlist_free(propval);
}

void
dsl_prop_nvlist_add_string(nvlist_t *nv, zfs_prop_t prop, const char *value)
{
	nvlist_t *propval;
	const char *propname = zfs_prop_to_name(prop);

	if (nvlist_lookup_nvlist(nv, propname, &propval) == 0) {
		VERIFY(nvlist_add_string(propval, ZPROP_VALUE, value) == 0);
		return;
	}

	VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_string(propval, ZPROP_VALUE, value) == 0);
	VERIFY(nvlist_add_nvlist(nv, propname, propval) == 0);
	nvlist_free(propval);
}

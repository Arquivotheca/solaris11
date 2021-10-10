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

#ifndef	_SYS_DSL_PROP_H
#define	_SYS_DSL_PROP_H

#include <sys/dmu.h>
#include <sys/dsl_pool.h>
#include <sys/zfs_context.h>
#include <sys/dsl_synctask.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* used with dsl_prop_get_ds() */
typedef enum dsl_prop_get_valtype {
	/*
	 * The effective value of a property is the one that actually determines
	 * the behavior on the system. Also known as the active value, or simply
	 * the value, it's what we normally mean when talking about the value of
	 * a property. The effective value is the topmost value in the following
	 * stack:
	 *
	 * local
	 * received
	 * inherited
	 * default
	 *
	 * Each layer, if present, overrides the one below it. The top two
	 * layers (local and received) are explicit and may be included in a
	 * 'zfs send' stream. A subset of properties can also have a temporary
	 * value associated with a temporary mount point; this temporary value
	 * always acts as the topmost value and overrides all other layers in
	 * the stack.
	 */
	DSL_PROP_GET_EFFECTIVE,

	/*
	 * Sometimes a caller wants to know a specific value in the property
	 * stack whether or not it is currently effective. The following all
	 * allow the caller to receive ENOENT in the case where a value does not
	 * exist at the specified layer. In contrast, calls using
	 * DSL_PROP_GET_EFFECTIVE can only return ENOENT if the property is not
	 * inheritable. A caller requesting explicit inheritance may obtain a
	 * parent dataset's local or received value or resort to the default as
	 * usual, but only if there is an explicit inheritance entry in the
	 * target dataset; otherwise the call returns ENOENT even though an
	 * implicitly inherited value exists. An explicit inheritance entry
	 * overrides a received value and never coeexists with a local value.
	 * The reverted value is the result of clearing the local value with
	 * 'zfs inherit -S', reverting to the received value, if any, and
	 * otherwise implicitly inheriting.
	 */
	DSL_PROP_GET_LOCAL,		/* not inherited, not received */
	DSL_PROP_GET_RECEIVED,		/* received and not inherited */
	DSL_PROP_GET_INHERITED,		/* inherited */
	DSL_PROP_GET_XINHERITED,	/* explicitly inherited */
	DSL_PROP_GET_REVERTED		/* received or inherited */
} dsl_prop_get_valtype_t;

struct dsl_dataset;
struct dsl_dir;

typedef void (dsl_prop_changed_cb_t)(void *arg, uint64_t newval);

typedef struct dsl_prop_cb_record {
	list_node_t cbr_node; /* link on dd_prop_cbs */
	struct dsl_dataset *cbr_ds;
	const char *cbr_propname;
	dsl_prop_changed_cb_t *cbr_func;
	void *cbr_arg;
} dsl_prop_cb_record_t;

typedef struct dsl_props_arg {
	nvlist_t *pa_props;
	zprop_source_t pa_source;
	zprop_setflags_t pa_flags;
	zone_t *pa_zone;
} dsl_props_arg_t;

typedef struct dsl_prop_setarg {
	const char *psa_name;
	zprop_source_t psa_source;
	int psa_intsz;
	int psa_numints;
	const void *psa_value;
	zprop_setflags_t psa_flags;
	zone_t *psa_zone;
	/*
	 * Used to predict the effective value after dsl_prop_set_sync().
	 */
	void *psa_effective_value;
	int psa_effective_numints;
	char psa_effective_setpoint[MAXNAMELEN];
	boolean_t psa_predicted;
} dsl_prop_setarg_t;

int dsl_prop_register(struct dsl_dataset *ds, const char *propname,
    dsl_prop_changed_cb_t *callback, void *cbarg);
int dsl_prop_unregister(struct dsl_dataset *ds, const char *propname,
    dsl_prop_changed_cb_t *callback, void *cbarg);
int dsl_prop_numcb(struct dsl_dataset *ds);

int dsl_prop_get(const char *ddname, const char *propname,
    int intsz, int numints, void *buf, char *setpoint);
int dsl_prop_get_integer(const char *ddname, const char *propname,
    uint64_t *valuep, char *setpoint);
int dsl_prop_get_all(objset_t *os, nvlist_t **nvp);
int dsl_prop_get_received(objset_t *os, nvlist_t **nvp);
int dsl_prop_get_ds(struct dsl_dataset *ds, const char *propname,
    int intsz, int numints, void *buf, char *setpoint,
    dsl_prop_get_valtype_t valtype);
int dsl_prop_get_dd(struct dsl_dir *dd, const char *propname,
    int intsz, int numints, void *buf, char *setpoint,
    boolean_t snapshot, dsl_prop_get_valtype_t valtype);
int dsl_prop_exists_ds(struct dsl_dataset *ds, const char *name,
    dsl_prop_get_valtype_t valtype);
#ifdef _KERNEL
int dsl_prop_get_zone(const char *ddname, const char *propname,
    int intsz, int numints, void *buf, char *setpoint, zone_t *zone);
int dsl_prop_get_ds_zone(struct dsl_dataset *ds, const char *propname,
    int intsz, int numints, void *buf, char *setpoint,
    dsl_prop_get_valtype_t valtype, zone_t *zone);
int dsl_prop_get_dd_zone(struct dsl_dir *dd, const char *propname,
    int intsz, int numints, void *buf, char *setpoint,
    boolean_t snapshot, dsl_prop_get_valtype_t valtype, zone_t *zone);
#endif

dsl_syncfunc_t dsl_props_set_sync;
int dsl_prop_decode_value(nvpair_t **pair);
int dsl_prop_encode_flag(nvlist_t *props, nvpair_t **pair, const char *flag);
void dsl_prop_decode_flags(nvpair_t *pair, zprop_setflags_t *flags);
int dsl_prop_validate_nvpair(nvpair_t *pair, zprop_source_t source);
int dsl_prop_set(const char *ddname, const char *propname,
    zprop_source_t source, int intsz, int numints, const void *buf,
    zprop_setflags_t flags);
int dsl_props_set(const char *dsname, zprop_source_t source, nvlist_t *nvl,
    zprop_setflags_t flags);
void dsl_dir_prop_set_uint64_sync(dsl_dir_t *dd, const char *name, uint64_t val,
    dmu_tx_t *tx);

int dsl_prop_get_all_raw(objset_t *os, nvlist_t **nvp);
int dsl_prop_set_all_raw(objset_t *os, nvlist_t *nv);

void dsl_prop_setarg_init_uint64(dsl_prop_setarg_t *psa, const char *propname,
    zprop_source_t source, uint64_t *value, zprop_setflags_t flags);
void dsl_prop_setarg_init_string(dsl_prop_setarg_t *psa, const char *propname,
    zprop_source_t source, const char *value, zprop_setflags_t flags);
int dsl_prop_predict(struct dsl_dataset *ds, dsl_prop_setarg_t *psa);
int dsl_props_predict(const char *dsname, nvlist_t *props,
    zprop_setflags_t flags, nvlist_t **predictions);

/* flag first receive on or after SPA_VERSION_RECVD_PROPS */
boolean_t dsl_prop_get_hasrecvd(objset_t *os);
void dsl_prop_set_hasrecvd(objset_t *os);
void dsl_prop_unset_hasrecvd(objset_t *os);

void dsl_prop_nvlist_add_uint64(nvlist_t *nv, zfs_prop_t prop, uint64_t value);
void dsl_prop_nvlist_add_string(nvlist_t *nv,
    zfs_prop_t prop, const char *value);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DSL_PROP_H */

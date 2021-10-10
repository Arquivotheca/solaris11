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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains functions related to congestion control.
 * Congestion control algorithms are managed via SMF:
 * each algorithm has a transient SMF service instance.
 * If an algorithm supports private properties, those
 * are also stored in the corresponding SMF instance,
 * rather than in the common ipadm database.
 */

#include <unistd.h>
#include <assert.h>
#include <libscf.h>
#include <libuutil.h>
#include <string.h>
#include <strings.h>
#include <inet/tcpcong.h>
#include "libipadm_impl.h"

#define	IPADM_CONG_SVC		"svc:/network/%s/congestion-control"
#define	IPADM_CONG_SVC_INST	"svc:/network/%s/congestion-control:%s"
#define	IPADM_CONG_PG		"cong"

#define	IPADM_SVC_STATE_TIMEOUT	15	/* in seconds */

typedef struct ipadm_cong_ent {
	uu_list_node_t		node;
	char			name[MAXPROPNAMELEN];
	char			*value;
} ipadm_cong_ent_t;

typedef struct ipadm_scf_state {
	char			*fmri;
	ssize_t			fmri_size;
	scf_handle_t		*h;
	scf_instance_t		*inst;
	scf_iter_t		*inst_iter;
	scf_service_t		*svc;
} ipadm_scf_state_t;

static ipadm_status_t ipadm_svc_is_online(scf_handle_t *, const char *);
static ipadm_status_t ipadm_svc_is_enabled(scf_handle_t *, const char *);

/* ARGSUSED */
static int
i_ipadm_cong_ent_compare(const void *a, const void *b, void *private)
{
	const ipadm_cong_ent_t	*a_ent = a;
	const ipadm_cong_ent_t	*b_ent = b;

	/* built-in always comes first */
	if (strcmp(a_ent->name, TCPCONG_ALG_BUILTIN) == 0)
		return (-1);
	else if (strcmp(b_ent->name, TCPCONG_ALG_BUILTIN) == 0)
		return (1);
	else
		return (strcmp(a_ent->name, b_ent->name));
}

static void
i_ipadm_cong_free_list(uu_list_t *list, uu_list_pool_t *pool)
{
	ipadm_cong_ent_t	*ent;
	void			*cookie = NULL;

	if (list == NULL)
		return;

	while ((ent = uu_list_teardown(list, &cookie)) != NULL) {
		uu_list_node_fini(ent, &(ent->node), pool);
		free(ent->value);
		free(ent);
	}
	uu_list_destroy(list);
}

/*
 * Add algorithm to the list, alpha-sorted.
 */
static ipadm_status_t
i_ipadm_cong_enlist_alg(const char *name, uu_list_t *list, uu_list_pool_t *pool)
{
	uu_list_index_t		idx;
	ipadm_cong_ent_t	*ent;

	ent = calloc(1, sizeof (ipadm_cong_ent_t));
	if (ent == NULL)
		return (IPADM_NO_BUFS);

	(void) strlcpy(ent->name, name, sizeof (ent->name));
	uu_list_node_init(ent, &(ent->node), pool);
	(void) uu_list_find(list, ent, NULL, &idx);
	(void) uu_list_insert(list, ent, idx);

	return (IPADM_SUCCESS);
}

/*
 * Add property to the list.
 */
static ipadm_status_t
i_ipadm_cong_enlist_prop(const char *name, char *value, uu_list_t *list,
    uu_list_pool_t *pool)
{
	uu_list_index_t		idx;
	ipadm_cong_ent_t	*ent;

	ent = calloc(1, sizeof (ipadm_cong_ent_t));
	if (ent == NULL)
		return (IPADM_NO_BUFS);

	(void) strlcpy(ent->name, name, sizeof (ent->name));
	ent->value = strdup(value);
	if (ent->value == NULL) {
		free(ent);
		return (IPADM_NO_BUFS);
	}
	uu_list_node_init(ent, &ent->node, pool);
	(void) uu_list_find(list, ent, NULL, &idx);
	(void) uu_list_insert(list, ent, idx);

	return (IPADM_SUCCESS);
}

/*
 * Make a comma-separated string out of a uu_list.
 */
static char *
i_ipadm_cong_list_to_str(uu_list_t *list)
{
	ipadm_cong_ent_t	*ent;
	int			cnt = 0;
	size_t			buflen = MAXPROPVALLEN;
	char			*buf;

	if ((buf = calloc(1, buflen)) == NULL)
		return (NULL);

	for (ent = uu_list_first(list);
	    ent != NULL;
	    ent = uu_list_next(list, ent)) {
		if (cnt++ > 0)
			(void) strlcat(buf, ",", buflen);
		(void) strlcat(buf, ent->name, buflen);
	}

	return (buf);
}

static void
i_ipadm_scf_destroy(ipadm_scf_state_t *ss)
{
	scf_instance_destroy(ss->inst);
	scf_iter_destroy(ss->inst_iter);
	scf_service_destroy(ss->svc);
	if (ss->h != NULL) {
		(void) scf_handle_unbind(ss->h);
		scf_handle_destroy(ss->h);
	}
	free(ss->fmri);
	bzero(ss, sizeof (*ss));
}

static ipadm_status_t
i_ipadm_scf_create(ipadm_scf_state_t *ss)
{
	ss->fmri_size = scf_limit(SCF_LIMIT_MAX_FMRI_LENGTH) + 1;
	ss->fmri = malloc(ss->fmri_size);
	if (ss->fmri == NULL)
		return (IPADM_NO_BUFS);

	ss->h = NULL;
	ss->svc = NULL;
	ss->inst = NULL;
	ss->inst_iter = NULL;

	if ((ss->h = scf_handle_create(SCF_VERSION)) == NULL ||
	    scf_handle_bind(ss->h) == -1 ||
	    (ss->svc = scf_service_create(ss->h)) == NULL ||
	    (ss->inst_iter = scf_iter_create(ss->h)) == NULL ||
	    (ss->inst = scf_instance_create(ss->h)) == NULL) {
		i_ipadm_scf_destroy(ss);
		return (IPADM_FAILURE);
	} else {
		return (IPADM_SUCCESS);
	}
}

/*
 * Collect congestion control properties from a service instance.
 * Properties are stored on the supplied uu_list.
 */
static ipadm_status_t
i_ipadm_cong_collect_props(scf_handle_t *h, scf_instance_t *inst,
    boolean_t *is_enabled, uu_list_t *prop_list, uu_list_pool_t *pool)
{
	ipadm_status_t		status = IPADM_FAILURE;
	char			pname[MAXPROPNAMELEN];
	char			vstr[MAXPROPVALLEN];
	uint8_t			vbool;
	scf_propertygroup_t	*pg = NULL;
	scf_iter_t		*prop_iter = NULL;
	scf_property_t		*prop = NULL;
	scf_value_t		*val = NULL;

	if ((pg = scf_pg_create(h)) == NULL ||
	    (prop_iter = scf_iter_create(h)) == NULL ||
	    (prop = scf_property_create(h)) == NULL ||
	    (val = scf_value_create(h)) == NULL)
		goto out;

	/* get service enabled property */
	if (scf_instance_get_pg(inst, SCF_PG_GENERAL, pg) < 0 ||
	    scf_pg_get_property(pg, SCF_PROPERTY_ENABLED, prop) < 0 ||
	    scf_property_get_value(prop, val) < 0 ||
	    scf_value_get_boolean(val, &vbool) < 0)
		goto out;

	*is_enabled = (vbool != 0);
	status = IPADM_SUCCESS;

	/* private properties are stored in a separate group */
	if (scf_instance_get_pg(inst, IPADM_CONG_PG, pg) < 0 ||
	    scf_iter_pg_properties(prop_iter, pg) < 0)
		goto out;

	while (scf_iter_next_property(prop_iter, prop) > 0) {
		if (scf_property_get_name(prop, pname, sizeof (pname)) == -1 ||
		    scf_property_get_value(prop, val) == -1 ||
		    scf_value_get_as_string(val, vstr, sizeof (vstr)) == -1)
			continue;
		status = i_ipadm_cong_enlist_prop(pname, vstr, prop_list, pool);
		if (status != IPADM_SUCCESS)
			break;
	}

out:
	scf_value_destroy(val);
	scf_property_destroy(prop);
	scf_iter_destroy(prop_iter);
	scf_pg_destroy(pg);

	return (status);
}

/*
 * Walk congestion control related properties stored in the SMF repository,
 * used mainly as part of *_init_prop() to push properties into the kernel.
 *
 * The complication here is that the 'cong_enabled' property must be set before
 * private properties, so that the congestion control modules get loaded
 * into the kernel first, but we need to walk all service instances before
 * the list of enabled algorithms is complete. Which presents us with
 * two options: either walk the SMF repo twice, or collect everything
 * in one pass and issue callbacks in the right order. The former takes
 * more time, the latter more memory. Given that this code runs at boot and
 * the amount of allocated memory is small, we choose the faster option.
 *
 * Using uu_lists also allows us to easily keep algorithm lists alpha-sorted.
 */
ipadm_status_t
ipadm_cong_walk_db(ipadm_handle_t iph, nvlist_t *filter, cong_db_func_t *cb)
{
	static char		*protos[] = { "tcp", "sctp" };
	char			*proto;
	ipadm_status_t		status;
	int			i;
	boolean_t		is_enabled;
	uu_list_pool_t		*pool;
	uu_list_t		*enabled_list, *prop_list;
	char			alg[MAXPROPVALLEN];
	char			*enabled_str;
	ipadm_cong_ent_t	*ent;
	ipadm_scf_state_t	ss;

	if ((status = i_ipadm_scf_create(&ss)) != IPADM_SUCCESS)
		return (status);

	pool = uu_list_pool_create("cong_pool",
	    sizeof (ipadm_cong_ent_t),
	    offsetof(ipadm_cong_ent_t, node),
	    i_ipadm_cong_ent_compare, 0);

	if (pool == NULL)
		goto out;

	for (i = 0; i < sizeof (protos) / sizeof (*protos); i++) {
		proto = protos[i];
		(void) snprintf(ss.fmri, ss.fmri_size, IPADM_CONG_SVC, proto);

		if (scf_handle_decode_fmri(ss.h, ss.fmri, NULL, ss.svc,
		    NULL, NULL, NULL, SCF_DECODE_FMRI_REQUIRE_NO_INSTANCE) < 0)
			continue;
		if (scf_iter_service_instances(ss.inst_iter, ss.svc) < 0)
			continue;

		/* allocate lists to hold collected data */
		enabled_list = uu_list_create(pool, NULL, UU_LIST_SORTED);
		prop_list = uu_list_create(pool, NULL, 0);

		/* one instance per algorithm */
		while (scf_iter_next_instance(ss.inst_iter, ss.inst) > 0) {
			if (i_ipadm_cong_collect_props(ss.h, ss.inst,
			    &is_enabled, prop_list, pool) != IPADM_SUCCESS ||
			    scf_instance_get_name(ss.inst, alg, sizeof (alg))
			    <= 0)
				continue;

			if (is_enabled)
				if (i_ipadm_cong_enlist_alg(alg, enabled_list,
				    pool) != IPADM_SUCCESS)
					break;
		}

		enabled_str = i_ipadm_cong_list_to_str(enabled_list);
		if (enabled_str == NULL)
			goto next;

		/* callbacks */
		if (!cb(iph, filter, "cong_enabled", enabled_str, proto))
			goto next;
		for (ent = uu_list_first(prop_list);
		    ent != NULL;
		    ent = uu_list_next(prop_list, ent)) {
			if (!cb(iph, filter, ent->name, ent->value, proto))
				break;
		}

	next:
		free(enabled_str);
		i_ipadm_cong_free_list(enabled_list, pool);
		i_ipadm_cong_free_list(prop_list, pool);
	}

out:
	if (pool != NULL)
		uu_list_pool_destroy(pool);
	i_ipadm_scf_destroy(&ss);

	return (status);
}

/*
 * Get a list of algorithms based on SMF instances.
 * If 'enabled' is true, only enabled algorithms are included.
 */
static ipadm_status_t
i_ipadm_cong_get_algs_common(uu_list_pool_t **pool, uu_list_t **alg_list,
    uint_t proto, boolean_t enabled)
{
	char			*proto_str;
	ipadm_status_t		status = IPADM_FAILURE;
	char			alg[MAXPROPVALLEN];
	ipadm_scf_state_t 	ss;

	proto_str = ipadm_proto2str(proto);
	assert(proto_str != NULL);

	if ((status = i_ipadm_scf_create(&ss)) != IPADM_SUCCESS)
		return (status);

	*pool = uu_list_pool_create("cong_pool",
	    sizeof (ipadm_cong_ent_t),
	    offsetof(ipadm_cong_ent_t, node),
	    i_ipadm_cong_ent_compare, 0);

	if (*pool == NULL)
		goto out;

	(void) snprintf(ss.fmri, ss.fmri_size, IPADM_CONG_SVC, proto_str);

	if (scf_handle_decode_fmri(ss.h, ss.fmri, NULL, ss.svc,
	    NULL, NULL, NULL, SCF_DECODE_FMRI_REQUIRE_NO_INSTANCE) < 0)
		goto out;
	if (scf_iter_service_instances(ss.inst_iter, ss.svc) < 0)
		goto out;

	/* we use the list to keep algs alpha-sorted */
	*alg_list = uu_list_create(*pool, NULL, UU_LIST_SORTED);

	/* one instance per algorithm */
	while (scf_iter_next_instance(ss.inst_iter, ss.inst) > 0) {
		if (scf_instance_get_name(ss.inst, alg, sizeof (alg)) <= 0)
			continue;
		if (enabled) {
			(void) snprintf(ss.fmri, ss.fmri_size,
			    IPADM_CONG_SVC_INST, proto_str, alg);
			if (ipadm_svc_is_enabled(ss.h, ss.fmri) !=
			    IPADM_SUCCESS)
				continue;
		}
		if (i_ipadm_cong_enlist_alg(alg, *alg_list, *pool) !=
		    IPADM_SUCCESS)
			break;
	}

	status = IPADM_SUCCESS;

out:
	i_ipadm_scf_destroy(&ss);

	return (status);
}

/*
 * Get comma-separated list of algorithms based on SMF instances.
 * If 'enabled' is true, only enabled algorithms are included.
 */
ipadm_status_t
ipadm_cong_get_algs(char *buf, uint_t *bufsize, uint_t proto, boolean_t enabled)
{
	uu_list_pool_t		*pool = NULL;
	uu_list_t		*alg_list = NULL;
	char			*alg_str = NULL;
	ipadm_status_t		status;

	buf[0] = '\0';

	status = i_ipadm_cong_get_algs_common(&pool, &alg_list, proto, enabled);
	if (status != IPADM_SUCCESS)
		goto out;

	alg_str = i_ipadm_cong_list_to_str(alg_list);
	if (alg_str == NULL) {
		status = IPADM_FAILURE;
		goto out;
	}

	*bufsize = snprintf(buf, *bufsize, "%s", alg_str);
	status = IPADM_SUCCESS;

out:
	free(alg_str);
	i_ipadm_cong_free_list(alg_list, pool);
	if (pool != NULL)
		uu_list_pool_destroy(pool);
	return (status);
}

/*
 * Congestion control private properties are named: _cong_algorithm_property
 * Return B_TRUE if 'pname' is one and return algorithm name as 'alg'.
 * Caller is responsible for freeing 'alg'.
 */
boolean_t
ipadm_cong_is_privprop(const char *pname, char **alg)
{
	static char	pre[] = "_cong_";
	size_t		prelen = sizeof (pre) - 1;
	size_t		alglen, proplen;
	const char	*p, *palg;

	/* starts with the prefix */
	if (strncmp(pname, pre, prelen) != 0)
		return (B_FALSE);

	/* algorithm and property components are divided by underscore */
	palg = pname + prelen;
	if ((p = strchr(palg, '_')) == NULL)
		return (B_FALSE);

	/* algorithm and property components are non-zero length */
	alglen = p - palg;
	proplen = strlen(++p);
	if (alglen == 0 || proplen == 0)
		return (B_FALSE);

	*alg = calloc(alglen + 1, 1);
	bcopy(palg, *alg, alglen);
	return (B_TRUE);
}

/*
 * Retrieve algorithm's private property from its service instance.
 */
ipadm_status_t
ipadm_cong_get_persist_propval(const char *alg, const char *pname,
    char *buf, uint_t *bufsize, const char *proto_str)
{
	ipadm_status_t		status = IPADM_OBJ_NOTFOUND;
	ssize_t			fmri_size;
	char			*fmri;
	scf_simple_prop_t	*prop;
	char			*vstr;

	pname++; /* SMF properties cannot start with '_' */

	fmri_size = scf_limit(SCF_LIMIT_MAX_FMRI_LENGTH) + 1;
	if ((fmri = malloc(fmri_size)) == NULL)
		return (IPADM_NO_BUFS);

	(void) snprintf(fmri, fmri_size, IPADM_CONG_SVC_INST,
	    proto_str, alg);

	prop = scf_simple_prop_get(NULL, fmri, IPADM_CONG_PG, pname);

	/* currently all private properties are strings */
	if (prop != NULL &&
	    scf_simple_prop_type(prop) == SCF_TYPE_ASTRING &&
	    (vstr = scf_simple_prop_next_astring(prop)) != NULL) {
		*bufsize = snprintf(buf, *bufsize, "%s", vstr);
		status = IPADM_SUCCESS;
	}

	scf_simple_prop_free(prop);
	free(fmri);
	return (status);
}

/*
 * Store algorithm's private property in its service instance.
 */
static ipadm_status_t
ipadm_cong_smf_set_prop(const char *alg, const char *pname,
    const char *buf, const char *proto_str)
{
	ipadm_status_t		status = IPADM_FAILURE;
	ipadm_scf_state_t	ss;
	scf_propertygroup_t	*pg = NULL;
	scf_property_t		*prop = NULL;
	scf_value_t		*val = NULL;
	scf_transaction_t	*tx = NULL;
	scf_transaction_entry_t	*ent = NULL;
	boolean_t		create = B_FALSE; /* create property */
	int			err;

	pname++; /* SMF properties cannot start with '_' */

	if ((status = i_ipadm_scf_create(&ss)) != IPADM_SUCCESS)
		return (status);

	(void) snprintf(ss.fmri, ss.fmri_size, IPADM_CONG_SVC_INST,
	    proto_str, alg);

	if ((pg = scf_pg_create(ss.h)) == NULL ||
	    (prop = scf_property_create(ss.h)) == NULL ||
	    (val = scf_value_create(ss.h)) == NULL ||
	    scf_value_set_astring(val, buf) != 0 ||
	    (tx = scf_transaction_create(ss.h)) == NULL ||
	    (ent = scf_entry_create(ss.h)) == NULL)
		goto out;

	if (scf_handle_decode_fmri(ss.h, ss.fmri, NULL, NULL, ss.inst,
	    NULL, NULL, SCF_DECODE_FMRI_REQUIRE_INSTANCE) != 0)
		goto out;

	/* Get property group or create one, if missing */
	if (scf_instance_get_pg(ss.inst, IPADM_CONG_PG, pg) != 0) {
		if (scf_error() != SCF_ERROR_NOT_FOUND)
			goto out;
		if (scf_instance_add_pg(ss.inst, IPADM_CONG_PG,
		    SCF_GROUP_APPLICATION, 0, pg) != 0) {
			if (scf_error() != SCF_ERROR_EXISTS)
				goto out;
		}
	}

	/* Get property or create one, if missing */
	if (scf_pg_get_property(pg, pname, prop) != 0) {
		if (scf_error() != SCF_ERROR_NOT_FOUND)
			goto out;
		create = B_TRUE;
	}

retry:
	if (scf_transaction_start(tx, pg) == -1)
		goto out;

	err = create ?
	    scf_transaction_property_new(tx, ent, pname, SCF_TYPE_ASTRING) :
	    scf_transaction_property_change_type(tx, ent, pname,
	    SCF_TYPE_ASTRING);
	if (err != 0 || scf_entry_add_value(ent, val) != 0)
		goto out;

	err = scf_transaction_commit(tx);
	switch (err) {
	case 1:
		status = IPADM_SUCCESS;
		break;
	case 0:
		scf_transaction_reset(tx);
		if (scf_pg_update(pg) == -1) {
			goto out;
		}
		goto retry;
	default:
		break;
	}
out:
	if (status != IPADM_SUCCESS) {
		switch (scf_error()) {
		case SCF_ERROR_NOT_FOUND:
			status = IPADM_OBJ_NOTFOUND;
			break;
		case SCF_ERROR_PERMISSION_DENIED:
			status = IPADM_PERM_DENIED;
			break;
		}
	}
	scf_entry_destroy(ent);
	scf_transaction_destroy(tx);
	scf_value_destroy(val);
	scf_property_destroy(prop);
	scf_pg_destroy(pg);
	i_ipadm_scf_destroy(&ss);
	return (status);
}

/*
 * Delete algorithm's private property from its service instance.
 */
/* ARGSUSED */
static ipadm_status_t
ipadm_cong_smf_delete_prop(const char *alg, const char *pname,
    const char *buf, const char *proto_str)
{
	ipadm_status_t		status;
	ipadm_scf_state_t	ss;
	scf_propertygroup_t	*pg = NULL;
	scf_transaction_t	*tx = NULL;
	scf_transaction_entry_t	*ent = NULL;
	int			err;

	pname++; /* SMF properties cannot start with '_' */

	if ((status = i_ipadm_scf_create(&ss)) != IPADM_SUCCESS)
		return (status);

	(void) snprintf(ss.fmri, ss.fmri_size, IPADM_CONG_SVC_INST,
	    proto_str, alg);

	if ((pg = scf_pg_create(ss.h)) == NULL ||
	    (tx = scf_transaction_create(ss.h)) == NULL ||
	    (ent = scf_entry_create(ss.h)) == NULL)
		goto out;

	if (scf_handle_decode_fmri(ss.h, ss.fmri, NULL, NULL, ss.inst,
	    NULL, NULL, SCF_DECODE_FMRI_REQUIRE_INSTANCE) != 0)
		goto out;

	if (scf_instance_get_pg(ss.inst, IPADM_CONG_PG, pg) != 0) {
		if (scf_error() == SCF_ERROR_NOT_FOUND)
			status = IPADM_SUCCESS;
		goto out;
	}

retry:
	if (scf_transaction_start(tx, pg) == -1)
		goto out;

	if (scf_transaction_property_delete(tx, ent, pname) != 0) {
		if (scf_error() == SCF_ERROR_NOT_FOUND)
			status = IPADM_SUCCESS;
		goto out;
	}

	err = scf_transaction_commit(tx);
	switch (err) {
	case 1:
		status = IPADM_SUCCESS;
		break;
	case 0:
		scf_transaction_reset(tx);
		if (scf_pg_update(pg) == -1) {
			goto out;
		}
		goto retry;
	default:
		break;
	}
out:
	if (status != IPADM_SUCCESS) {
		switch (scf_error()) {
		case SCF_ERROR_PERMISSION_DENIED:
			status = IPADM_PERM_DENIED;
			break;
		}
	}
	scf_entry_destroy(ent);
	scf_transaction_destroy(tx);
	scf_pg_destroy(pg);
	i_ipadm_scf_destroy(&ss);
	return (status);
}

/*
 * Set/reset algorithm's private property.
 */
ipadm_status_t
ipadm_cong_persist_propval(const char *alg, const char *pname, const char *buf,
    const char *proto_str, uint_t flags)
{
	if (flags & IPADM_OPT_DEFAULT)
		return (ipadm_cong_smf_delete_prop(alg, pname, buf, proto_str));
	else
		return (ipadm_cong_smf_set_prop(alg, pname, buf, proto_str));
}

/*
 * Returns:
 *	IPADM_SUCCESS if service is online
 *	IPADM_FAILURE if service is not online
 *	IPADM_OBJ_NOTFOUND if service does not exist
 */
static ipadm_status_t
ipadm_svc_is_online(scf_handle_t *h, const char *svc)
{
	scf_simple_prop_t	*prop;
	const char		*state;
	ipadm_status_t		status = IPADM_OBJ_NOTFOUND;

	prop = scf_simple_prop_get(h, svc, SCF_PG_RESTARTER,
	    SCF_PROPERTY_STATE);
	if (prop != NULL) {
		state = scf_simple_prop_next_astring(prop);
		if (state != NULL)
			status = strcmp(state, SCF_STATE_STRING_ONLINE) == 0 ?
			    IPADM_SUCCESS : IPADM_FAILURE;
	}
	scf_simple_prop_free(prop);
	return (status);
}

/*
 * Returns:
 *	IPADM_SUCCESS if service is enabled
 *	IPADM_FAILURE if service is not enabled
 *	IPADM_OBJ_NOTFOUND if service does not exist
 */
static ipadm_status_t
ipadm_svc_is_enabled(scf_handle_t *h, const char *svc)
{
	scf_simple_prop_t	*prop;
	uint8_t			*vbool;
	ipadm_status_t		status = IPADM_OBJ_NOTFOUND;

	prop = scf_simple_prop_get(h, svc, SCF_PG_GENERAL,
	    SCF_PROPERTY_ENABLED);
	if (prop != NULL && scf_simple_prop_numvalues(prop) == 1) {
		vbool = scf_simple_prop_next_boolean(prop);
		if (vbool != NULL)
			status = (*vbool != 0) ? IPADM_SUCCESS : IPADM_FAILURE;
	}
	scf_simple_prop_free(prop);
	return (status);
}

/*
 * Enable or disable a congestion control SMF service instance.
 */
ipadm_status_t
ipadm_cong_smf_set_state(const char *alg, uint_t proto, uint_t flags)
{
	boolean_t	enable;
	uint_t		smf_flags;
	ssize_t		fmri_size;
	char		*fmri;
	int		i, err;
	ipadm_status_t	status, ret;

	enable = (flags & IPADM_OPT_APPEND) != 0;
	smf_flags = ((flags & IPADM_OPT_PERSIST) == 0) ? SMF_TEMPORARY : 0;

	fmri_size = scf_limit(SCF_LIMIT_MAX_FMRI_LENGTH) + 1;
	if ((fmri = malloc(fmri_size)) == NULL)
		return (IPADM_NO_BUFS);

	(void) snprintf(fmri, fmri_size, IPADM_CONG_SVC_INST,
	    ipadm_proto2str(proto), alg);

	status = ipadm_svc_is_online(NULL, fmri);
	if (status == IPADM_OBJ_NOTFOUND)
		goto out;
	if (enable == (status == IPADM_SUCCESS)) {
		status = IPADM_SUCCESS;
		goto out;
	}

	err = enable ? smf_enable_instance(fmri, smf_flags) :
	    smf_disable_instance(fmri, smf_flags);
	if (err == -1) {
		if (scf_error() == SCF_ERROR_PERMISSION_DENIED)
			status = IPADM_PERM_DENIED;
		else
			status = IPADM_FAILURE;
		goto out;
	}

	/*
	 * wait for the asynchronous request to complete
	 */
	status = IPADM_FAILURE;
	for (i = 0; i < IPADM_SVC_STATE_TIMEOUT; i++) {
		ret = ipadm_svc_is_online(NULL, fmri);
		if (enable == (ret == IPADM_SUCCESS)) {
			status = IPADM_SUCCESS;
			break;
		}
		(void) sleep(1);
	}

out:
	free(fmri);
	return (status);
}

/*
 * Disable all SMF services except default one.
 */
void
ipadm_cong_smf_disable_nondef(const char *def, uint_t proto, uint_t flags)
{
	uu_list_pool_t		*pool = NULL;
	uu_list_t		*alg_list = NULL;
	ipadm_cong_ent_t	*ent;
	ipadm_status_t		status;

	assert((flags & IPADM_OPT_APPEND) == 0);

	status = i_ipadm_cong_get_algs_common(&pool, &alg_list, proto, B_TRUE);
	if (status != IPADM_SUCCESS)
		return;

	for (ent = uu_list_first(alg_list);
	    ent != NULL;
	    ent = uu_list_next(alg_list, ent)) {
		if (strcmp(ent->name, def) == 0)
			continue;
		(void) ipadm_cong_smf_set_state(ent->name, proto, flags);
	}

	i_ipadm_cong_free_list(alg_list, pool);
	uu_list_pool_destroy(pool);
}

static int
i_ipadm_cong_str_compare(const void *p1, const void *p2)
{
	char *s1 = *((char **)p1);
	char *s2 = *((char **)p2);

	/* built-in always comes first */
	if (strcmp(s1, TCPCONG_ALG_BUILTIN) == 0)
		return (-1);
	else if (strcmp(s2, TCPCONG_ALG_BUILTIN) == 0)
		return (1);
	else
		return (strcmp(s1, s2));
}

static int
i_ipadm_cong_list_tokenize(char *s)
{
	int	cnt = 0;

	if (*s != '\0') {
		cnt++;
		while (*s != '\0') {
			if (*s == ',') {
				*s = '\0';
				cnt++;
			}
			s++;
		}
	}
	return (cnt);
}

/*
 * Sort comma-separated list in-place.
 */
void
ipadm_cong_list_sort(char *list)
{
	char		*s, *p;
	int		cnt, i;
	char		**sp;

	if ((s = strdup(list)) == NULL)
		return;
	cnt = i_ipadm_cong_list_tokenize(s);

	if (cnt < 2 || (sp = malloc(cnt * sizeof (char *))) == NULL) {
		free(s);
		return;
	}
	for (p = s, i = 0; i < cnt; i++, p += strlen(p) + 1) {
		sp[i] = p;
	}
	qsort(sp, cnt, sizeof (char *), i_ipadm_cong_str_compare);

	list[0] = '\0';
	for (i = 0; i < cnt; i++) {
		(void) strcat(list, sp[i]);
		if (i < cnt - 1)
			(void) strcat(list, ",");
	}
	free(s);
	free(sp);
}

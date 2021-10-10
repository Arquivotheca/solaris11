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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * file_object.c - enter objects into and load them from the backend
 *
 * The primary entry points in this layer are object_create(),
 * object_create_pg(), object_check_node(), object_delete() and
 * object_fill_children().  They each take an rc_node_t and use the
 * functions in the object_info_t info array for the node's type.
 *
 * Also, object_bundle_remove() is an entry point that is used to
 * cleanup the bundle references from the repository and all children
 * of the service rc_node_t.
 *
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "configd.h"
#include "repcache_protocol.h"

typedef struct child_info {
	rc_node_t	*ci_parent;
	backend_tx_t	*ci_tx;
	rc_node_lookup_t ci_base_nl;
	int		ignoresnap;
} child_info_t;

typedef struct delete_ent delete_ent_t;
typedef struct delete_stack delete_stack_t;
typedef struct delete_info delete_info_t;

typedef int	delete_cb_func(delete_info_t *, const delete_ent_t *,
	delete_result_t *);

struct delete_ent {
	delete_cb_func	*de_cb;		/* callback */
	uint32_t	de_backend;
	uint32_t	de_id;
	uint32_t	de_gen;
	uint32_t	de_dec;
};

struct delete_stack {
	struct delete_stack *ds_next;
	uint32_t	ds_size;	/* number of elements */
	uint32_t	ds_cur;		/* current offset */
	delete_ent_t	ds_buf[1];	/* actually ds_size */
};
#define	DELETE_STACK_SIZE(x)	offsetof(delete_stack_t, ds_buf[(x)])

struct delete_info {
	backend_tx_t	*di_tx;
	backend_tx_t	*di_np_tx;
	delete_stack_t	*di_stack;
	delete_stack_t	*di_free;
	int		di_action;
	int		di_snapshot;
};

typedef struct object_info {
	uint32_t	obj_type;
	enum id_space	obj_id_space;

	int (*obj_is_in_repo)(backend_tx_t *, rc_node_t *);
	int (*obj_fill_children)(rc_node_t *);
	int (*obj_setup_child_info)(rc_node_t *, uint32_t, child_info_t *);
	int (*obj_query_child)(backend_query_t *, rc_node_lookup_t *,
	    const char *);
	int (*obj_insert_child)(backend_tx_t *, rc_node_lookup_t *,
	    const char *, uint32_t *);
	int (*obj_insert_pg_child)(backend_tx_t *, rc_node_lookup_t *,
	    const char *, const char *, uint32_t, uint32_t, uint32_t *);
	int (*obj_delete_start)(rc_node_t *, delete_info_t *);
	int (*obj_update_gen_id)(backend_tx_t *, rc_node_t *);
} object_info_t;

/*
 * Retrieve decoration flags from the highest layer associated with
 * decoration_id.
 */
static rep_protocol_responseid_t
get_decoration_flags(backend_tx_t *tx, uint32_t decoration_id,
    uint32_t decoration_gen, int ignoresnap, uint32_t *flagsp)
{
	uint32_t flags;
	backend_query_t *q;
	rep_protocol_responseid_t r;

	*flagsp = 0;
	if (decoration_id == 0)
		return (REP_PROTOCOL_SUCCESS);

	assert(tx != NULL);

	/*
	 * If we are processing a snapshot property group set
	 * of decorations then we do not want to ignore the
	 * snapshot only cases.
	 */
	q = backend_query_alloc();
	if (ignoresnap) {
		backend_query_add(q,
		    "SELECT decoration_flags FROM decoration_tbl "
		    "WHERE (decoration_id = %d AND "
		    "    (decoration_flags & %d) = 0 AND "
		    "    decoration_gen_id <= %d) "
		    "ORDER BY decoration_layer DESC LIMIT 1",
		    decoration_id, DECORATION_IN_USE, decoration_gen);
	} else {
		uint32_t l;
		int ignoredelcust = 0;

		backend_query_add(q,
		    "SELECT decoration_layer FROM decoration_tbl "
		    "WHERE (decoration_id = %d AND decoration_gen_id = %d "
		    "    AND (decoration_flags & %d) != 0); ",
		    decoration_id, decoration_gen, DECORATION_DELCUSTED);

		r = backend_tx_run_single_int(tx, q, &l);

		if (r == REP_PROTOCOL_FAIL_NO_RESOURCES) {
			backend_query_free(q);
			return (r);
		}

		if (r == REP_PROTOCOL_SUCCESS && l != REP_PROTOCOL_DEC_ADMIN)
			ignoredelcust = 1;

		backend_query_reset(q);
		backend_query_add(q,
		    "SELECT decoration_flags FROM decoration_tbl "
		    "WHERE (decoration_id = %d AND "
		    "    decoration_layer < %d AND decoration_gen_id <= %d) "
		    "ORDER BY decoration_layer DESC LIMIT 1; ",
		    decoration_id, ignoredelcust ? REP_PROTOCOL_DEC_ADMIN :
		    REP_PROTOCOL_DEC_TOP, decoration_gen);
	}
	r = backend_tx_run_single_int(tx, q, &flags);
	backend_query_free(q);
	if (r == REP_PROTOCOL_SUCCESS)
		*flagsp = flags;
	return (r);
}

void
string_to_id(const char *str, uint32_t *output, const char *fieldname)
{
	if (str == NULL) {
		*output = 0;
		return;
	}
	if (uu_strtouint(str, output, sizeof (*output), 0, 0, 0) == -1)
		backend_panic("invalid integer \"%s\" in field \"%s\"",
		    str, fieldname);
}

static void
string_to_time(const char *str, time_t *output, const char *fieldname)
{
	if (uu_strtouint(str, output, sizeof (*output), 0, 0, INT32_MAX) == -1)
		backend_panic("invalid time \"%s\" in field \"%s\"",
		    str, fieldname);
}

static rep_protocol_value_type_t
string_to_prop_type(const char *pt)
{
	if (pt == NULL)
		return (REP_PROTOCOL_TYPE_INVALID);

	assert(('a' <= pt[0] && 'z' >= pt[0]) ||
	    ('A' <= pt[0] && 'Z' >= pt[0]) &&
	    (pt[1] == 0 || ('a' <= pt[1] && 'z' >= pt[1]) ||
	    ('A' <= pt[1] && 'Z' >= pt[1])));
	return (pt[0] | (pt[1] << 8));
}

#define	NUM_NEEDED	50

static int
delete_stack_push(delete_info_t *dip, uint32_t be, delete_cb_func *cb,
    uint32_t id, uint32_t gen, uint32_t dec)
{
	delete_stack_t *cur = dip->di_stack;
	delete_ent_t *ent;

	if (cur == NULL || cur->ds_cur == cur->ds_size) {
		delete_stack_t *new = dip->di_free;
		dip->di_free = NULL;
		if (new == NULL) {
			new = uu_zalloc(DELETE_STACK_SIZE(NUM_NEEDED));
			if (new == NULL)
				return (REP_PROTOCOL_FAIL_NO_RESOURCES);
			new->ds_size = NUM_NEEDED;
		}
		new->ds_cur = 0;
		new->ds_next = dip->di_stack;
		dip->di_stack = new;
		cur = new;
	}
	assert(cur->ds_cur < cur->ds_size);
	ent = &cur->ds_buf[cur->ds_cur++];

	ent->de_backend = be;
	ent->de_cb = cb;
	ent->de_id = id;
	ent->de_gen = gen;
	ent->de_dec = dec;

	return (REP_PROTOCOL_SUCCESS);
}

static int
delete_stack_pop(delete_info_t *dip, delete_ent_t *out)
{
	delete_stack_t *cur = dip->di_stack;
	delete_ent_t *ent;

	if (cur == NULL)
		return (0);
	assert(cur->ds_cur > 0 && cur->ds_cur <= cur->ds_size);
	ent = &cur->ds_buf[--cur->ds_cur];
	if (cur->ds_cur == 0) {
		dip->di_stack = cur->ds_next;
		cur->ds_next = NULL;

		if (dip->di_free != NULL)
			uu_free(dip->di_free);
		dip->di_free = cur;
	}
	if (ent == NULL)
		return (0);

	*out = *ent;
	return (1);
}

static void
delete_stack_cleanup(delete_info_t *dip)
{
	delete_stack_t *cur;
	while ((cur = dip->di_stack) != NULL) {
		dip->di_stack = cur->ds_next;

		uu_free(cur);
	}

	if ((cur = dip->di_free) != NULL) {
		assert(cur->ds_next == NULL);	/* should only be one */
		uu_free(cur);
		dip->di_free = NULL;
	}
}

struct delete_cb_info {
	delete_info_t	*dci_dip;
	uint32_t	dci_be;
	delete_cb_func	*dci_cb;
	int		dci_result;
};

/*ARGSUSED*/
static int
push_delete_callback(void *data, int columns, char **vals, char **names)
{
	struct delete_cb_info *info = data;

	const char *id_str = *vals++;
	const char *gen_str = *vals++;
	const char *dec_str;

	uint32_t id;
	uint32_t gen;
	uint32_t dec;

	assert(columns == 2 || columns == 3);

	string_to_id(id_str, &id, "id");
	string_to_id(gen_str, &gen, "gen_id");
	if (columns == 3) {
		dec_str = *vals++;
		string_to_id(dec_str, &dec, "dec_id");
	} else {
		dec = 0;
	}

	info->dci_result = delete_stack_push(info->dci_dip, info->dci_be,
	    info->dci_cb, id, gen, dec);

	if (info->dci_result != REP_PROTOCOL_SUCCESS)
		return (BACKEND_CALLBACK_ABORT);
	return (BACKEND_CALLBACK_CONTINUE);
}

static int
value_delete(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	uint32_t be = ent->de_backend;
	int r;

	backend_query_t *q;

	backend_tx_t *tx = (be == BACKEND_TYPE_NORMAL)? dip->di_tx :
	    dip->di_np_tx;

	/*
	 * There is no muting of values, so we always indicate that the
	 * entity was deleted.
	 */
	*delres = DELETE_DELETED;

	/* There are no values to delete */
	if (ent->de_id == 0)
		return (REP_PROTOCOL_SUCCESS);

	q = backend_query_alloc();

	if (be == BACKEND_TYPE_NORMAL) {
		backend_query_add(q,
		    "SELECT 1 FROM prop_lnk_tbl WHERE (lnk_val_id = %d); "
		    "SELECT 1 from decoration_tbl WHERE "
		    "    (decoration_value_id = %d); "
		    "DELETE FROM value_tbl WHERE (value_id = %d); ",
		    ent->de_id, ent->de_id, ent->de_id);
	} else {
		backend_query_add(q,
		    "SELECT 1 FROM prop_lnk_tbl WHERE (lnk_val_id = %d); "
		    "DELETE FROM value_tbl WHERE (value_id = %d); ",
		    ent->de_id, ent->de_id, ent->de_id);
	}
	r = backend_tx_run(tx, q, backend_fail_if_seen, NULL);
	backend_query_free(q);
	if (r == REP_PROTOCOL_DONE)
		return (REP_PROTOCOL_SUCCESS);		/* still in use */
	return (r);
}

static int
prop_lnk_tbl_delete(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	struct delete_cb_info info;
	uint32_t be = ent->de_backend;
	int r;

	backend_query_t *q;

	backend_tx_t *tx = (be == BACKEND_TYPE_NORMAL) ? dip->di_tx :
	    dip->di_np_tx;

	/*
	 * No muting done in this function.
	 */
	*delres = DELETE_DELETED;

	/*
	 * Protect a property that is in use by another property
	 * as the true value.
	 */
	if (be == BACKEND_TYPE_NORMAL) {
		uint32_t decf = 0;

		q = backend_query_alloc();
		backend_query_add(q,
		    "SELECT 1 FROM prop_lnk_tbl WHERE "
		    "    lnk_prop_id != %d AND "
		    "    lnk_val_decoration_key = (SELECT decoration_key "
		    "        FROM decoration_tbl WHERE "
		    "        decoration_id = %d AND decoration_gen_id = %d); ",
		    ent->de_id, ent->de_dec, ent->de_gen);

		r = backend_tx_run(tx, q, backend_fail_if_seen, NULL);

		if (r == REP_PROTOCOL_DONE) {
			backend_query_free(q);
			return (REP_PROTOCOL_SUCCESS); /* still in use */
		}

		if (dip->di_snapshot) {
			backend_query_reset(q);
			backend_query_add(q,
			    "SELECT decoration_flags FROM decoration_tbl "
			    "    WHERE decoration_id = %d AND "
			    "        decoration_gen_id = %d; ",
			    ent->de_dec, ent->de_gen);

			r = backend_tx_run_single_int(tx, q, &decf);
		}
		/*
		 * Also, want to protect the layer from drifting off if it's
		 * the only entry present for this layer (and there are
		 * higher generations).
		 *
		 * This would effectively protect say a profile layer
		 * that is in place under an admin layer.
		 *
		 * Note : the backend_fail_if_seen will return a SUCCESS
		 * and in this case that is what we want to capture to
		 * protect this layer.
		 */
		if ((decf & DECORATION_SNAP_ONLY) == 0) {
			backend_query_reset(q);
			backend_query_add(q,
			    "SELECT pg_id from pg_tbl "
			    "    WHERE pg_id = (SELECT lnk_pg_id FROM "
			    "        prop_lnk_tbl WHERE lnk_prop_id = %d)",
			    ent->de_id);

			r = backend_tx_run(tx, q, backend_fail_if_seen, NULL);

			if (r == REP_PROTOCOL_DONE) {
				backend_query_reset(q);
				backend_query_add(q,
				    "SELECT 1 FROM decoration_tbl "
				    "    WHERE decoration_id = %d AND "
				    "    decoration_layer = "
				    "        (SELECT decoration_layer "
				    "            FROM decoration_tbl "
				    "            WHERE decoration_id = %d AND "
				    "            decoration_gen_id = %d) AND "
				    "            decoration_gen_id > %d ",
				    ent->de_dec, ent->de_dec, ent->de_gen,
				    ent->de_gen);

				r = backend_tx_run(tx, q, backend_fail_if_seen,
				    NULL);
				if (r != REP_PROTOCOL_DONE) {
					backend_query_free(q);
					return (REP_PROTOCOL_SUCCESS);
				}
			}
		}

		backend_query_free(q);
	}

	info.dci_dip = dip;
	info.dci_be = be;
	info.dci_cb = &value_delete;
	info.dci_result = REP_PROTOCOL_SUCCESS;

	q = backend_query_alloc();
	if (be == BACKEND_TYPE_NORMAL) {
		const repcache_client_t *cp = get_active_client();

		/*
		 * Need to select the value_id from the decoration_tbl
		 * that is associated with this property not the
		 * value_id that is associated with the property.
		 *
		 * Also, we will protect a decoration that is not
		 * associated with the bundle that is being supplied
		 * during this work.  Which will in turn protect the
		 * property row from being deleted.
		 *
		 * But if the bundle is null there is no protection
		 * as this is a complete removal of the property.
		 *
		 * Finally, update the decoration row, so that it's
		 * marked as being kept only by a bundle reference,
		 * and can easily be removed without having to check
		 * snapshot reference.
		 */
		backend_query_add(q,
		    "SELECT DISTINCT decoration_value_id, 0 "
		    "FROM decoration_tbl "
		    "    WHERE (decoration_id = %d AND "
		    "    decoration_gen_id = %d); ",
		    ent->de_dec, ent->de_gen);

		if (cp->rc_file) {
			backend_query_add(q,
			    "DELETE FROM decoration_tbl "
			    "    WHERE (decoration_id = %d AND "
			    "    decoration_gen_id = %d AND "
			    "    decoration_bundle_id = (SELECT bundle_id "
			    "        FROM bundle_tbl "
			    "        WHERE bundle_name = '%q')); ",
			    ent->de_dec, ent->de_gen, cp->rc_file);
		} else {
			backend_query_add(q,
			    "DELETE FROM decoration_tbl "
			    "    WHERE (decoration_id = %d AND "
			    "    decoration_gen_id = %d); ",
			    ent->de_dec, ent->de_gen);
		}

		backend_query_add(q,
		    "DELETE FROM prop_lnk_tbl "
		    "    WHERE (lnk_prop_id = %d AND lnk_gen_id NOT IN "
		    "        (SELECT decoration_gen_id FROM decoration_tbl "
		    "            WHERE decoration_id = %d)); ",
		    ent->de_id, ent->de_dec);

		backend_query_add(q, "UPDATE decoration_tbl "
		    "SET decoration_flags = (decoration_flags | %d) "
		    "    WHERE (decoration_id = %d AND "
		    "        decoration_gen_id = %d); ",
		    DECORATION_BUNDLE_ONLY, ent->de_dec, ent->de_gen);
	} else {
		backend_query_add(q,
		    "SELECT DISTINCT lnk_val_id, 0 FROM prop_lnk_tbl "
		    "WHERE "
		    "    (lnk_prop_id = %d AND lnk_val_id NOTNULL AND "
		    "    lnk_val_id != 0); "
		    "DELETE FROM prop_lnk_tbl "
		    "WHERE (lnk_prop_id = %d)",
		    ent->de_id, ent->de_id);
	}

	r = backend_tx_run(tx, q, push_delete_callback, &info);
	backend_query_free(q);

	if (r == REP_PROTOCOL_DONE) {
		assert(info.dci_result != REP_PROTOCOL_SUCCESS);
		return (info.dci_result);
	}
	return (r);
}

static int
pg_lnk_tbl_delete(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	struct delete_cb_info info;
	uint32_t be = ent->de_backend;
	int r;

	backend_query_t *q;

	backend_tx_t *tx = (be == BACKEND_TYPE_NORMAL)? dip->di_tx :
	    dip->di_np_tx;

	/*
	 * No muting or deletion done in this function.  They are handled by
	 * other callback functions that are pushed on the delete stack.
	 */
	*delres = DELETE_NA;

	/*
	 * For non-persistent backends, we could only have one parent, and
	 * he's already been deleted.
	 *
	 * For normal backends, we need to check to see if we're in
	 * a snapshot or are the active generation for the property
	 * group.  If we are, there's nothing to be done.
	 */
	if (be == BACKEND_TYPE_NORMAL) {
		q = backend_query_alloc();
		backend_query_add(q,
		    "SELECT 1 "
		    "FROM pg_tbl "
		    "WHERE (pg_id = %d AND pg_gen_id = %d); "
		    "SELECT 1 "
		    "FROM snaplevel_lnk_tbl "
		    "WHERE (snaplvl_pg_id = %d AND snaplvl_gen_id = %d);",
		    ent->de_id, ent->de_gen,
		    ent->de_id, ent->de_gen);
		r = backend_tx_run(tx, q, backend_fail_if_seen, NULL);

		if (r == REP_PROTOCOL_DONE) {
			backend_query_free(q);
			return (REP_PROTOCOL_SUCCESS);	/* still in use */
		}

		/*
		 * If the parent of the pg is gone in the normal, then
		 * drop all the children of the property group.
		 *
		 * This is done because a property could be protected
		 * when the snapshots were removed before the service
		 * was removed, and since the chain is completely going
		 * away to this property now it should be finally removed
		 * as well.
		 */
		backend_query_reset(q);

		backend_query_add(q,
		    "SELECT 1 FROM service_tbl WHERE svc_id = "
		    "    (SELECT pg_parent_id FROM pg_tbl WHERE pg_id = %d); "
		    "SELECT 1 FROM instance_tbl where instance_id = "
		    "    (SELECT pg_parent_id FROM pg_tbl WHERE pg_id = %d) ",
		    ent->de_id, ent->de_id);

		r = backend_tx_run(tx, q, backend_fail_if_seen, NULL);

		backend_query_reset(q);

		if (r == REP_PROTOCOL_SUCCESS) {
			backend_query_add(q,
			    "SELECT lnk_prop_id, lnk_gen_id, lnk_decoration_id "
			    "    FROM prop_lnk_tbl "
			    "    WHERE (lnk_pg_id = %d)",
			    ent->de_id);
		} else {
			backend_query_add(q,
			    "SELECT lnk_prop_id, lnk_gen_id, lnk_decoration_id "
			    "    FROM prop_lnk_tbl "
			    "    WHERE (lnk_pg_id = %d AND lnk_gen_id = %d)",
			    ent->de_id, ent->de_gen);
		}
	} else {
		q = backend_query_alloc();
		backend_query_add(q,
		    "SELECT lnk_prop_id, lnk_gen_id FROM prop_lnk_tbl "
		    "    WHERE (lnk_pg_id = %d AND lnk_gen_id = %d)",
		    ent->de_id, ent->de_gen);
	}

	info.dci_dip = dip;
	info.dci_be =  be;
	info.dci_cb = &prop_lnk_tbl_delete;
	info.dci_result = REP_PROTOCOL_SUCCESS;

	r = backend_tx_run(tx, q, push_delete_callback, &info);
	backend_query_free(q);

	if (r == REP_PROTOCOL_DONE) {
		assert(info.dci_result != REP_PROTOCOL_SUCCESS);
		return (info.dci_result);
	}
	return (r);
}

/*ARGSUSED*/
static int
prep_propagate_properties(void *data, int columns, char **vals, char **names)
{
	pg_update_bundle_info_t	*pup = data;
	backend_query_t		*q;

	uint32_t	prop_id;
	uint32_t	dec_id;
	int		idx;
	int		r;

	string_to_id(vals[0], &prop_id, names[0]);
	string_to_id(vals[1], &dec_id, names[1]);

	q = backend_query_alloc();

	/*
	 * Are there any other layers below admin?
	 */
	backend_query_add(q, "SELECT 1 FROM decoration_tbl "
	    "WHERE decoration_id = %d AND decoration_layer < %d "
	    "    AND (decoration_flags & %d) = 0 ",
	    dec_id, REP_PROTOCOL_DEC_ADMIN,
	    DECORATION_NOFILE|DECORATION_IN_USE);

	r = backend_tx_run(pup->pub_tx, q, backend_fail_if_seen, NULL);

	backend_query_free(q);
	if (r == REP_PROTOCOL_SUCCESS) {
		idx = pup->pub_dprop_idx;

		pup->pub_dprop_ids[idx] = prop_id;
		pup->pub_dprop_idx++;

		return (BACKEND_CALLBACK_CONTINUE);
	}

	/*
	 * Really, just put the rest on the rollback list so that
	 * it rolls back to the next highest layer below admin.
	 *
	 * On the rollback side mark the admin property as
	 * SNAP ONLY if there are any, before adding the MASK
	 * ADMIN layer.
	 */
	idx = pup->pub_rback_idx;
	pup->pub_rback_ids[idx] = prop_id;
	pup->pub_rback_idx++;

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * For each property in the pg, latest gen check to
 * see if it is one of the following :
 *
 * 1. admin only
 * 2. filebacked with admin change
 * 3. filebacked only
 *
 * If (1) add to the delete list
 * If (2) add to the rollback list >>> although this rollback will be
 * 	a bit different, once we get to the finish.  This rollback will
 * 	rollback to the previous highest layer before the admin layer.
 * If (3) simply leave off the lists for copy.
 *
 * Another thing to check and set is if this generation is part
 * of a snapshot and mark the pup as such.
 */
static int
propagate_properties(backend_tx_t *tx, const delete_ent_t *ent,
    boolean_t custonly)
{
	pg_update_bundle_info_t pup;
	backend_query_t		*q;

	uint32_t	new_gen;
	uint32_t	dcnt;
	int		r;

	q = backend_query_alloc();

	backend_query_add(q, "SELECT count() FROM prop_lnk_tbl "
	    "WHERE lnk_pg_id = %d AND lnk_gen_id = %d ",
	    ent->de_id, ent->de_gen);

	r = backend_tx_run_single_int(tx, q, &dcnt);
	if (r != REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return (r);
	}

	pup.pub_pg_id = ent->de_id;
	pup.pub_gen_id = ent->de_gen;

	pup.pub_setmask = 1;
	pup.pub_dprop_idx = 0;
	pup.pub_rback_idx = 0;
	pup.pub_dprop_ids = uu_zalloc(dcnt * sizeof (uint32_t));
	pup.pub_rback_ids = uu_zalloc(dcnt * sizeof (uint32_t));
	pup.pub_delcust = (custonly) ? 1 : 0;

	backend_query_reset(q);

	backend_query_add(q, "SELECT 1 FROM snaplevel_lnk_tbl "
	    "WHERE snaplvl_pg_id = %d and snaplvl_gen_id = %d ",
	    ent->de_id, ent->de_gen);

	pup.pub_geninsnapshot = 0;
	r = backend_tx_run(tx, q, backend_fail_if_seen, NULL);
	if (r == REP_PROTOCOL_DONE)
		pup.pub_geninsnapshot = 1;

	backend_query_reset(q);

	backend_query_add(q, "SELECT lnk_prop_id, lnk_decoration_id "
	    "FROM prop_lnk_tbl WHERE lnk_pg_id = %d AND lnk_gen_id = %d ",
	    ent->de_id, ent->de_gen);

	pup.pub_tx = tx;
	r = backend_tx_run(tx, q, prep_propagate_properties, &pup);

	if (r != REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return (r);
	}

	return (object_pg_bundle_finish(ent->de_id, &pup, &new_gen, tx));
}


static int
propertygrp_delete(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	uint32_t be = ent->de_backend;
	backend_query_t *pgq;
	uint32_t gen;

	struct timeval ts;

	const repcache_client_t *cp = get_active_client();

	boolean_t filebacked = B_FALSE;
	int r;

	backend_tx_t *tx = (be == BACKEND_TYPE_NORMAL)? dip->di_tx :
	    dip->di_np_tx;

	boolean_t delcust = (dip->di_action == REP_PROTOCOL_ENTITY_DELCUST);

	*delres = DELETE_UNDEFINED;

	/* Skip nonperisstent pgs on a delcust */
	if (be == BACKEND_TYPE_NONPERSIST && delcust) {
		*delres = DELETE_NA;
		return (REP_PROTOCOL_SUCCESS);
	}

	/* For an undelete, reset the masked flag and return. */
	if (be == BACKEND_TYPE_NORMAL &&
	    dip->di_action == REP_PROTOCOL_ENTITY_UNDELETE) {
		r = backend_tx_run_update_changed(dip->di_tx,
		    "UPDATE decoration_tbl "
		    "SET decoration_flags = (decoration_flags & %d) "
		    "    WHERE decoration_id = %d AND "
		    "        decoration_layer = %d AND "
		    "        (decoration_flags & %d) != 0; ",
		    ~DECORATION_MASK, ent->de_dec, cp->rc_layer_id,
		    DECORATION_MASK);

		*delres = DELETE_UNMASKED;
		return (r);
	}

	/* delete and delcust both need to know about file backing */
	if (be == BACKEND_TYPE_NORMAL &&
	    dip->di_action != REP_PROTOCOL_ENTITY_REMOVE) {
		uint32_t layer;
		backend_query_t *lq;

		/*
		 * An ascending layer query gets us the lowest layer.  If that
		 * is lower than admin, then the propertygrp in question is
		 * filebacked.
		 */
		lq = backend_query_alloc();
		backend_query_add(lq,
		    "SELECT decoration_layer FROM decoration_tbl "
		    "    WHERE decoration_id = %d "
		    "    ORDER BY decoration_layer LIMIT 1 ",
		    ent->de_dec);
		r = backend_tx_run_single_int(tx, lq, &layer);
		backend_query_free(lq);

		if (r != REP_PROTOCOL_SUCCESS &&
		    r != REP_PROTOCOL_FAIL_NOT_FOUND)
			return (r);

		/*
		 * Use the current incoming layer to determine filebacking
		 * support to later determine if muting should be attempted.
		 *
		 * This protects the use of scf_pg_delete() in svccfg to
		 * continue to work as desired if done at a lower layer.
		 */
		if (r == REP_PROTOCOL_SUCCESS && layer < cp->rc_layer_id)
			filebacked = B_TRUE;
	}

	/* delete, remove, and delcust all need the gen_id */
	pgq = backend_query_alloc();
	backend_query_add(pgq,
	    "SELECT pg_gen_id FROM pg_tbl WHERE pg_id = %d; ",
	    ent->de_id);

	if (be == BACKEND_TYPE_NORMAL) {
		if (filebacked &&
		    dip->di_action == REP_PROTOCOL_ENTITY_DELETE) {
			backend_query_t *mq;
			uint32_t key;

			*delres = DELETE_MASKED;

			mq = backend_query_alloc();

			/*
			 * Is there an admin layer?
			 */
			backend_query_add(mq,
			    "SELECT 1 FROM decoration_tbl "
			    "    WHERE decoration_id = %d AND "
			    "    decoration_layer = %d LIMIT 1; ",
			    ent->de_dec, cp->rc_layer_id);

			r = backend_tx_run(tx, mq, backend_fail_if_seen, NULL);
			backend_query_free(mq);

			/*
			 * Update the admin decorations if found, if
			 * not then we insert a new decoration.
			 */
			if (r == REP_PROTOCOL_SUCCESS) {
				(void) gettimeofday(&ts, NULL);

				key = backend_new_id(tx,
				    BACKEND_KEY_DECORATION);
				backend_query_add(pgq,
				    "INSERT INTO decoration_tbl "
				    "    (decoration_key, decoration_id, "
				    "    decoration_gen_id, "
				    "    decoration_value_id, "
				    "    decoration_layer, "
				    "    decoration_bundle_id, "
				    "    decoration_type, decoration_flags, "
				    "    decoration_tv_sec, "
				    "    decoration_tv_usec) "
				    "VALUES (%d, %d, %d, 0, %d, 0, %d, %d, "
				    "    %ld, %ld); ",
				    key, ent->de_dec, ent->de_gen,
				    cp->rc_layer_id, DECORATION_TYPE_PG,
				    DECORATION_MASK, ts.tv_sec, ts.tv_usec);
			} else if (r == REP_PROTOCOL_DONE) {
				backend_query_add(pgq,
				    "UPDATE decoration_tbl "
				    "    SET decoration_flags = "
				    "        (decoration_flags | %d) WHERE "
				    "    decoration_id = %d AND "
				    "    decoration_layer = %d; ",
				    DECORATION_MASK, ent->de_dec,
				    cp->rc_layer_id);
			} else /* r == REP_PROTOCOL_NO_RESOURCES */ {
				backend_query_free(pgq);
				return (r);
			}
		}

		if (filebacked &&
		    dip->di_action != REP_PROTOCOL_ENTITY_REMOVE) {

			/*
			 * For a filebacked delcust, remove admin decorations
			 * altogether.  For non-filebacked delcust, this is
			 * covered by the non-filebacked-or-remove clause below.
			 */
			if (delcust) {
				backend_query_add(pgq,
				    "DELETE FROM decoration_tbl "
				    "    WHERE (decoration_id = %d AND "
				    "    decoration_layer = %d);",
				    ent->de_dec, REP_PROTOCOL_DEC_ADMIN);

				/*
				 * We don't have a delete result to match
				 * delcust.  Treat it the same as an unmask.
				 */
				*delres = DELETE_UNMASKED;
			}

			/*
			 * On a filebacked delete or delcust, bump the gen_id
			 * and roll down through the properties so that
			 * admin-only properties can be dropped and all other
			 * filebacked properties can be dealt with
			 * appropriately.
			 *
			 * The delete result should have already been set,
			 * depending on the operation.
			 */
			r = propagate_properties(tx, ent, delcust);
			if (r != REP_PROTOCOL_SUCCESS) {
				backend_query_free(pgq);
				return (r);
			}
		} else {
			/*
			 * If there is no file to back the property group, or
			 * the operation is a remove, get rid of all the
			 * decorations.
			 */
			*delres = DELETE_DELETED;
			backend_query_add(pgq,
			    "DELETE FROM decoration_tbl "
			    "    WHERE decoration_id = %d; ",
			    ent->de_dec);
		}
	}

	/*
	 * !filebacked implies one of the following conditions:
	 *
	 *   1. REMOVE, regardless of backend type or file backing
	 *   2. DELETE or DELCUST of an admin-layer-only property
	 *	group from a persistent backend
	 *   3. DELETE from a nonpersistent backend
	 *
	 * Each of these should result in removal of the property group.
	 */
	if (!filebacked) {
		*delres = DELETE_DELETED;
		backend_query_add(pgq,
		    "DELETE FROM pg_tbl WHERE pg_id = %d", ent->de_id);
	}

	r = backend_tx_run_single_int(tx, pgq, &gen);
	backend_query_free(pgq);

	if (r != REP_PROTOCOL_SUCCESS)
		return (r);

	return (delete_stack_push(dip, be, &pg_lnk_tbl_delete,
	    ent->de_id, gen, 0));
}

static int
snaplevel_lnk_delete(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	uint32_t be = ent->de_backend;
	backend_query_t *q;
	struct delete_cb_info info;

	int r;

	backend_tx_t *tx = (be == BACKEND_TYPE_NORMAL)? dip->di_tx :
	    dip->di_np_tx;

	/*
	 * No muting done in this function.
	 */
	*delres = DELETE_DELETED;

	info.dci_dip = dip;
	info.dci_be = be;
	info.dci_cb = &pg_lnk_tbl_delete;
	info.dci_result = REP_PROTOCOL_SUCCESS;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT snaplvl_pg_id, snaplvl_gen_id "
	    "    FROM snaplevel_lnk_tbl "
	    "    WHERE snaplvl_level_id = %d; "
	    "DELETE FROM decoration_tbl WHERE decoration_id IN "
	    "    (SELECT DISTINCT snaplvl_dec_id FROM snaplevel_lnk_tbl "
	    "        WHERE snaplvl_level_id = %d AND snaplvl_dec_id != 0); "
	    "DELETE FROM snaplevel_lnk_tbl WHERE snaplvl_level_id = %d; ",
	    ent->de_id, ent->de_id, ent->de_id);

	dip->di_snapshot = 1;

	r = backend_tx_run(tx, q, push_delete_callback, &info);
	backend_query_free(q);

	if (r == REP_PROTOCOL_DONE) {
		assert(info.dci_result != REP_PROTOCOL_SUCCESS);
		return (info.dci_result);
	}
	return (r);
}

static int
snaplevel_tbl_delete(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	uint32_t be = ent->de_backend;
	backend_tx_t *tx = (be == BACKEND_TYPE_NORMAL)? dip->di_tx :
	    dip->di_np_tx;

	struct delete_cb_info info;
	backend_query_t *q;
	int r;

	assert(be == BACKEND_TYPE_NORMAL);

	/*
	 * No muting done in this function.
	 */
	*delres = DELETE_DELETED;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT 1 FROM snapshot_lnk_tbl WHERE lnk_snap_id = %d",
	    ent->de_id);
	r = backend_tx_run(tx, q, backend_fail_if_seen, NULL);
	backend_query_free(q);

	if (r == REP_PROTOCOL_DONE)
		return (REP_PROTOCOL_SUCCESS);		/* still in use */

	info.dci_dip = dip;
	info.dci_be = be;
	info.dci_cb = &snaplevel_lnk_delete;
	info.dci_result = REP_PROTOCOL_SUCCESS;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT snap_level_id, 0 FROM snaplevel_tbl WHERE snap_id = %d;"
	    "DELETE FROM snaplevel_tbl WHERE snap_id = %d",
	    ent->de_id, ent->de_id);
	r = backend_tx_run(tx, q, push_delete_callback, &info);
	backend_query_free(q);

	if (r == REP_PROTOCOL_DONE) {
		assert(info.dci_result != REP_PROTOCOL_SUCCESS);
		return (info.dci_result);
	}
	return (r);
}

static int
snapshot_lnk_delete(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	uint32_t be = ent->de_backend;
	backend_tx_t *tx = (be == BACKEND_TYPE_NORMAL)? dip->di_tx :
	    dip->di_np_tx;

	backend_query_t *q;
	uint32_t snapid;
	int r;

	assert(be == BACKEND_TYPE_NORMAL);

	/*
	 * No muting done in this function.
	 */
	*delres = DELETE_DELETED;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT lnk_snap_id FROM snapshot_lnk_tbl WHERE lnk_id = %d; "
	    "DELETE FROM snapshot_lnk_tbl WHERE lnk_id = %d",
	    ent->de_id, ent->de_id);
	r = backend_tx_run_single_int(tx, q, &snapid);
	backend_query_free(q);

	if (r != REP_PROTOCOL_SUCCESS)
		return (r);

	return (delete_stack_push(dip, be, &snaplevel_tbl_delete,
	    snapid, 0, 0));
}

static int
pgparent_delete_add_pgs(delete_info_t *dip, uint32_t parent_id)
{
	struct delete_cb_info info;
	backend_query_t *q;
	int r;

	info.dci_dip = dip;
	info.dci_be = BACKEND_TYPE_NORMAL;
	info.dci_cb = &propertygrp_delete;
	info.dci_result = REP_PROTOCOL_SUCCESS;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT pg_id, pg_gen_id, pg_dec_id FROM pg_tbl "
	    "    WHERE pg_parent_id = %d",
	    parent_id);

	r = backend_tx_run(dip->di_tx, q, push_delete_callback, &info);

	if (r == REP_PROTOCOL_DONE) {
		assert(info.dci_result != REP_PROTOCOL_SUCCESS);
		backend_query_free(q);
		return (info.dci_result);
	}
	if (r != REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return (r);
	}

	if (dip->di_np_tx != NULL) {
		info.dci_be = BACKEND_TYPE_NONPERSIST;

		r = backend_tx_run(dip->di_np_tx, q, push_delete_callback,
		    &info);

		if (r == REP_PROTOCOL_DONE) {
			assert(info.dci_result != REP_PROTOCOL_SUCCESS);
			backend_query_free(q);
			return (info.dci_result);
		}
		if (r != REP_PROTOCOL_SUCCESS) {
			backend_query_free(q);
			return (r);
		}
	}
	backend_query_free(q);
	return (REP_PROTOCOL_SUCCESS);
}

static int
service_delete(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	backend_query_t *q = NULL;
	uint32_t layer;

	struct timeval ts;

	int r;

	const repcache_client_t *cp = get_active_client();
	*delres = DELETE_UNDEFINED;

	/*
	 * If the action is unmask then let's just handle it
	 * here, and remove the mask decoration.
	 */
	if (dip->di_action == REP_PROTOCOL_ENTITY_UNDELETE) {
		r = backend_tx_run_update_changed(dip->di_tx,
		    "UPDATE decoration_tbl "
		    "SET decoration_flags = (decoration_flags & %d) "
		    "    WHERE decoration_id = %d AND "
		    "        decoration_layer = %d AND "
		    "        (decoration_flags & %d) != 0; ",
		    ~DECORATION_MASK, ent->de_dec, cp->rc_layer_id,
		    DECORATION_MASK);
		*delres = DELETE_UNMASKED;

		return (r);
	}

	if (dip->di_action == REP_PROTOCOL_ENTITY_REMOVE) {
		r = backend_tx_run_update_changed(dip->di_tx,
		    "DELETE FROM decoration_tbl WHERE decoration_id = %d; "
		    "DELETE FROM service_tbl WHERE svc_id = %d; "
		    "DELETE FROM bundle_tbl where bundle_id NOT IN "
		    "    (SELECT decoration_bundle_id FROM decoration_tbl); ",
		    ent->de_dec, ent->de_id);

		*delres = DELETE_DELETED;

		goto out;
	}

	if (dip->di_action == REP_PROTOCOL_ENTITY_DELCUST) {
		r = backend_tx_run_update_changed(dip->di_tx,
		    "DELETE FROM decoration_tbl "
		    "    WHERE decoration_id = %d AND "
		    "    decoration_layer = %d;",
		    ent->de_dec, cp->rc_layer_id);

		*delres = DELETE_DELETED;

		/*
		 * If there were no admin-layer decorations to
		 * remove, it's not an error, so reset r to allow
		 * propagation to pgs below.
		 *
		 * If there *were* admin-layer decoriations to remove,
		 * fall through and test for a decoration at a lower
		 * layer; if there are none, then the service was only
		 * defined administratively, and should be removed.
		 */

		if (r != REP_PROTOCOL_SUCCESS) {
			if (r == REP_PROTOCOL_FAIL_NOT_FOUND)
				r = REP_PROTOCOL_SUCCESS;
			goto out;
		}
	}

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT decoration_layer from decoration_tbl "
	    "    WHERE decoration_id = %d "
	    "    ORDER BY decoration_layer LIMIT 1 ",
	    ent->de_dec);

	r = backend_tx_run_single_int(dip->di_tx, q, &layer);
	backend_query_reset(q);

	if (dip->di_action == REP_PROTOCOL_ENTITY_DELCUST) {
		if (r == REP_PROTOCOL_FAIL_NOT_FOUND) {
			r = backend_tx_run_update_changed(dip->di_tx,
			    "DELETE FROM service_tbl WHERE svc_id = %d",
			    ent->de_id);
		}

		goto out;
	}

	if (r != REP_PROTOCOL_SUCCESS)
		goto out;

	if (layer < cp->rc_layer_id) {
		uint32_t key;

		backend_query_add(q,
		    "SELECT 1 FROM decoration_tbl "
		    "    WHERE decoration_id = %d "
		    "    AND decoration_layer = %d LIMIT 1; ",
		    ent->de_dec, cp->rc_layer_id);

		r = backend_tx_run(dip->di_tx, q, backend_fail_if_seen, NULL);
		backend_query_reset(q);

		if (r == REP_PROTOCOL_SUCCESS) {
			key = backend_new_id(dip->di_tx,
			    BACKEND_KEY_DECORATION);

			(void) gettimeofday(&ts, NULL);
			r = backend_tx_run_update_changed(dip->di_tx,
			    "INSERT INTO decoration_tbl "
			    "    (decoration_key, decoration_id, "
			    "    decoration_gen_id, "
			    "    decoration_value_id, "
			    "    decoration_layer, "
			    "    decoration_bundle_id, "
			    "    decoration_type, decoration_flags, "
			    "    decoration_tv_sec, decoration_tv_usec) "
			    "VALUES (%d, %d, 0, 0, %d, 0, %d, %d, %ld, %ld); ",
			    key, ent->de_dec, cp->rc_layer_id,
			    DECORATION_TYPE_SVC, DECORATION_MASK, ts.tv_sec,
			    ts.tv_usec);
			*delres = DELETE_MASKED;
		} else {
			r = backend_tx_run_update_changed(dip->di_tx,
			    "UPDATE decoration_tbl "
			    "    SET decoration_flags = "
			    "        (decoration_flags | %d) WHERE "
			    "    decoration_id = %d AND "
			    "    decoration_layer = %d; ",
			    DECORATION_MASK, ent->de_dec, cp->rc_layer_id);
			*delres = DELETE_MASKED;
		}
	} else if (r == REP_PROTOCOL_SUCCESS) {
		r = backend_tx_run_update_changed(dip->di_tx,
		    "DELETE FROM decoration_tbl WHERE decoration_id = %d; "
		    "DELETE FROM service_tbl WHERE svc_id = %d; "
		    "DELETE FROM bundle_tbl WHERE bundle_id NOT IN "
		    "    (SELECT decoration_bundle_id FROM decoration_tbl); ",
		    ent->de_dec, ent->de_id);

		*delres = DELETE_DELETED;
	}


out:
	backend_query_free(q);

	if (r != REP_PROTOCOL_SUCCESS)
		return (r);

	return (pgparent_delete_add_pgs(dip, ent->de_id));
}

static int
instance_delete(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	struct delete_cb_info info;
	int original_action = dip->di_action;
	backend_query_t *q;
	uint32_t layer;
	int runningonly = 0;
	int r;

	struct timeval ts;

	const repcache_client_t *cp = get_active_client();

	*delres = DELETE_NA;

	/*
	 * If the action is unmask then let's just handle it
	 * here, and remove the mask decoration.
	 */
	if (dip->di_action == REP_PROTOCOL_ENTITY_UNDELETE) {
		r = backend_tx_run_update_changed(dip->di_tx,
		    "UPDATE decoration_tbl "
		    "SET decoration_flags = (decoration_flags & %d) "
		    "    WHERE decoration_id = %d AND "
		    "        decoration_layer = %d AND "
		    "        (decoration_flags & %d) != 0; ",
		    ~DECORATION_MASK, ent->de_dec, cp->rc_layer_id,
		    DECORATION_MASK);
		*delres = DELETE_UNMASKED;

		return (r);
	}

	q = backend_query_alloc();
	if (dip->di_action == REP_PROTOCOL_ENTITY_REMOVE) {
		r = backend_tx_run_update_changed(dip->di_tx,
		    "DELETE FROM decoration_tbl where decoration_id = %d; "
		    "DELETE FROM instance_tbl WHERE instance_id = %d",
		    ent->de_dec, ent->de_id);
		*delres = DELETE_DELETED;

		goto snapshots;
	}

	if (dip->di_action == REP_PROTOCOL_ENTITY_DELCUST) {
		r = backend_tx_run_update_changed(dip->di_tx,
		    "DELETE FROM decoration_tbl "
		    "    WHERE decoration_id = %d AND "
		    "    decoration_layer = %d;",
		    ent->de_dec, cp->rc_layer_id);

		*delres = DELETE_DELETED;

		/*
		 * If there were no admin-layer decorations to
		 * remove, it's not an error, so reset r to allow
		 * propagation to pgs below.
		 *
		 * If there *were* admin-layer decoriations to remove,
		 * fall through and test for a decoration at a lower
		 * layer; if there are none, then the instance was only
		 * defined administratively, and should be removed.
		 */
		if (r != REP_PROTOCOL_SUCCESS) {
			if (r == REP_PROTOCOL_FAIL_NOT_FOUND) {
				r = REP_PROTOCOL_SUCCESS;
				*delres = DELETE_NA;
			}
			goto out;
		}
	}

	backend_query_add(q,
	    "SELECT decoration_layer from decoration_tbl "
	    "    WHERE decoration_id = %d "
	    "    ORDER BY decoration_layer LIMIT 1 ",
	    ent->de_dec);

	r = backend_tx_run_single_int(dip->di_tx, q, &layer);
	backend_query_reset(q);

	if (dip->di_action == REP_PROTOCOL_ENTITY_DELCUST) {
		if (r == REP_PROTOCOL_FAIL_NOT_FOUND) {
			r = backend_tx_run_update_changed(dip->di_tx,
			    "DELETE FROM instance_tbl WHERE instance_id = %d",
			    ent->de_id);
			if (r == REP_PROTOCOL_SUCCESS)
				dip->di_action = REP_PROTOCOL_ENTITY_REMOVE;
			goto snapshots;
		}

		goto out;
	}

	if (r != REP_PROTOCOL_SUCCESS)
		goto out;

	if (layer < cp->rc_layer_id) {
		uint32_t key;

		backend_query_add(q,
		    "SELECT 1 FROM decoration_tbl "
		    "    WHERE decoration_id = %d "
		    "    AND decoration_layer = %d LIMIT 1; ",
		    ent->de_dec, cp->rc_layer_id);

		r = backend_tx_run(dip->di_tx, q, backend_fail_if_seen, NULL);
		backend_query_reset(q);

		*delres = DELETE_MASKED;
		if (r == REP_PROTOCOL_SUCCESS) {
			key = backend_new_id(dip->di_tx,
			    BACKEND_KEY_DECORATION);

			(void) gettimeofday(&ts, NULL);
			r = backend_tx_run_update_changed(dip->di_tx,
			    "INSERT INTO decoration_tbl "
			    "    (decoration_key, decoration_id, "
			    "    decoration_gen_id, "
			    "    decoration_value_id, "
			    "    decoration_layer, "
			    "    decoration_bundle_id, "
			    "    decoration_type, decoration_flags, "
			    "    decoration_tv_sec, decoration_tv_usec) "
			    "VALUES (%d, %d, 0, 0, %d, 0, %d, %d, %ld, %ld); ",
			    key, ent->de_dec, cp->rc_layer_id,
			    DECORATION_TYPE_INST, DECORATION_MASK, ts.tv_sec,
			    ts.tv_usec);
		} else {
			r = backend_tx_run_update_changed(dip->di_tx,
			    "UPDATE decoration_tbl "
			    "    SET decoration_flags = "
			    "        (decoration_flags | %d) WHERE "
			    "    decoration_id = %d AND "
			    "    decoration_layer = %d; ",
			    DECORATION_MASK, ent->de_dec, cp->rc_layer_id);
		}

		/*
		 * Only delete the running snapshot from the masked instance
		 */
		runningonly = 1;
	} else {
		r = backend_tx_run_update_changed(dip->di_tx,
		    "DELETE FROM decoration_tbl where decoration_id = %d; "
		    "DELETE FROM instance_tbl WHERE instance_id = %d",
		    ent->de_dec, ent->de_id);
			*delres = DELETE_DELETED;
	}

snapshots:
	if (r != REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return (r);
	}

	info.dci_dip = dip;
	info.dci_be = BACKEND_TYPE_NORMAL;
	info.dci_cb = &snapshot_lnk_delete;
	info.dci_result = REP_PROTOCOL_SUCCESS;

	backend_query_reset(q);
	if (runningonly) {
		backend_query_add(q,
		    "SELECT lnk_id, 0 FROM snapshot_lnk_tbl "
		    "    WHERE (lnk_inst_id = %d AND "
		    "         lnk_snap_name = 'running'); ", ent->de_id);
	} else {
		backend_query_add(q,
		    "SELECT lnk_id, 0 FROM snapshot_lnk_tbl "
		    "    WHERE lnk_inst_id = %d", ent->de_id);
	}
	r = backend_tx_run(dip->di_tx, q, push_delete_callback, &info);
	dip->di_action = original_action;

	if (r == REP_PROTOCOL_DONE) {
		assert(info.dci_result != REP_PROTOCOL_SUCCESS);
		backend_query_free(q);
		return (info.dci_result);
	}

out:
	backend_query_free(q);
	r = pgparent_delete_add_pgs(dip, ent->de_id);

	return (r);
}

/* ARGSUSED */
static int
brm_pg_clear_conflict(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	backend_clear_pg_conflict(dip->di_tx, ent->de_dec, ent->de_id);

	return (REP_PROTOCOL_SUCCESS);
}

/* ARGSUSED */
static int
decoration_bundle_delete(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	if (ent->de_gen) {
		return (backend_tx_run_update(dip->di_tx,
		    "DELETE FROM decoration_tbl "
		    "WHERE (decoration_id = %d AND "
		    "    decoration_bundle_id = %d AND "
		    "    decoration_gen_id = %d); ",
		    ent->de_dec, ent->de_id, ent->de_gen));
	} else {
		return (backend_tx_run_update(dip->di_tx,
		    "DELETE FROM decoration_tbl "
		    "WHERE (decoration_id = %d AND "
		    "    decoration_bundle_id = %d); ",
		    ent->de_dec, ent->de_id));
	}
}

/* ARGSUSED */
static int
prop_mark_bundle_remove_sno(delete_info_t *dip, const delete_ent_t *ent,
    delete_result_t *delres)
{
	return (backend_tx_run_update(dip->di_tx, "UPDATE decoration_tbl "
	    "SET decoration_flags = (decoration_flags | %d) "
	    "    WHERE (decoration_id = %d AND "
	    "        decoration_gen_id = %d); ",
	    DECORATION_NOFILE|DECORATION_SNAP_ONLY, ent->de_dec, ent->de_gen));
}

/*
 * does a bit-wise OR of all resulting rows of a single column
 *
 * Columns:
 *      0      column to be bit-wise OR'ed
 */
/*ARGSUSED*/
static int
bitwise_or_column(void *data, int columns, char **vals, char **names)
{
	uint32_t *val = data;
	uint32_t column;

	assert(columns == 1);
	string_to_id(vals[0], &column, names[0]);

	*val |= column;

	return (BACKEND_CALLBACK_CONTINUE);
}

static int
get_svc_or_inst_dec_flags(backend_tx_t *tx, uint32_t dec_id,
    uint32_t *dec_flags)
{
	uint32_t flags = 0;
	rep_protocol_responseid_t r;
	backend_query_t *q;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT DISTINCT decoration_flags FROM decoration_tbl "
	    "WHERE decoration_id = %d; ", dec_id);
	r = backend_tx_run(tx, q, bitwise_or_column, &flags);
	backend_query_free(q);
	if (r != REP_PROTOCOL_SUCCESS)
		return (BACKEND_CALLBACK_ABORT);

	*dec_flags = flags;

	return (REP_PROTOCOL_SUCCESS);
}

/*
 * Columns are:
 *	0	entity name
 *	1	repository key
 *	2	decoration ID
 */
static int
fill_child_callback(void *data, int columns, char **vals, char **names)
{
	child_info_t *cp = data;
	rc_node_t *np;
	uint32_t main_id;
	uint32_t decoration_id;
	const char *name;
	uint32_t dec_flags;
	rc_node_lookup_t *lp = &cp->ci_base_nl;

	assert(columns == 3);

	name = *vals;
	string_to_id(vals[1], &main_id, names[1]);
	string_to_id(vals[2], &decoration_id, names[2]);

	if (get_svc_or_inst_dec_flags(cp->ci_tx, decoration_id, &dec_flags) !=
	    REP_PROTOCOL_SUCCESS)
		return (BACKEND_CALLBACK_ABORT);

	lp->rl_main_id = main_id;

	if ((np = rc_node_alloc()) == NULL)
		return (BACKEND_CALLBACK_ABORT);

	np = rc_node_setup(np, lp, name, cp->ci_parent, decoration_id,
	    dec_flags);
	rc_node_rele(np);

	return (BACKEND_CALLBACK_CONTINUE);
}

/*ARGSUSED*/
static int
fill_snapshot_callback(void *data, int columns, char **vals, char **names)
{
	child_info_t *cp = data;
	rc_node_t *np;
	uint32_t main_id;
	uint32_t snap_id;
	const char *name;
	const char *cur;
	const char *snap;
	rc_node_lookup_t *lp = &cp->ci_base_nl;

	assert(columns == 3);

	name = *vals++;
	columns--;

	cur = *vals++;
	columns--;
	snap = *vals++;
	columns--;

	string_to_id(cur, &main_id, "lnk_id");
	string_to_id(snap, &snap_id, "lnk_snap_id");

	lp->rl_main_id = main_id;

	if ((np = rc_node_alloc()) == NULL)
		return (BACKEND_CALLBACK_ABORT);

	np = rc_node_setup_snapshot(np, lp, name, snap_id, cp->ci_parent);
	rc_node_rele(np);

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Values are:
 *
 *	0	PG name
 *	1	PG ID
 *	2	generation ID
 *	3	PG type
 *	4	PG flags
 *	5	PG decoration ID (not provided when filling PGs as children
 *		of snaplevels)
 */
/*ARGSUSED*/
static int
fill_pg_callback(void *data, int columns, char **vals, char **names)
{
	child_info_t *cip = data;
	const char *name;
	const char *type;
	const char *cur;
	uint32_t main_id;
	uint32_t flags;
	uint32_t gen_id;
	uint32_t decoration_id = 0;
	uint32_t dec_flags;

	rc_node_lookup_t *lp = &cip->ci_base_nl;
	rc_node_t *newnode, *pg;

	assert(columns >= 5);
	assert(columns <= 6);

	name = *vals++;		/* pg_name */
	columns--;

	cur = *vals++;		/* pg_id */
	columns--;
	string_to_id(cur, &main_id, "pg_id");

	lp->rl_main_id = main_id;

	cur = *vals++;		/* pg_gen_id */
	columns--;
	string_to_id(cur, &gen_id, "pg_gen_id");

	type = *vals++;		/* pg_type */
	columns--;

	cur = *vals++;		/* pg_flags */
	columns--;
	string_to_id(cur, &flags, "pg_flags");

	if (columns-- > 0) {
		string_to_id(*vals++, &decoration_id, "pg_dec_id");
	}

	if (get_decoration_flags(cip->ci_tx, decoration_id, gen_id,
	    cip->ignoresnap, &dec_flags) != REP_PROTOCOL_SUCCESS) {
		return (BACKEND_CALLBACK_ABORT);
	}
	if ((newnode = rc_node_alloc()) == NULL)
		return (BACKEND_CALLBACK_ABORT);

	pg = rc_node_setup_pg(newnode, lp, name, type, flags, gen_id,
	    decoration_id, dec_flags, cip->ci_parent);
	if (pg == NULL) {
		rc_node_destroy(newnode);
		return (BACKEND_CALLBACK_ABORT);
	}

	rc_node_rele(pg);

	return (BACKEND_CALLBACK_CONTINUE);
}

/*ARGSUSED*/
static int
property_value_cb(void *data, int columns, char **vals, char **names)
{
	rc_value_set_t *vs = data;

	assert(columns == 1);

	if (rc_vs_add_value(vs, vals[0]) !=
	    REP_PROTOCOL_SUCCESS) {
		return (BACKEND_CALLBACK_ABORT);
	}

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Retrieve the bundle name and timestamp from the query columns.  The
 * query data consists of:
 *
 *	0	bundle_name
 *	1	bundle_timestamp
 */
/*ARGSUSED*/
static int
fill_bundle_cb(void *data, int columns, char **vals, char **names)
{
	rc_bundle_t	*b = data;

	assert(columns == 2);

	b->bundle_name = uu_strdup(vals[0]);
	if (b->bundle_name == NULL) {
		return (BACKEND_CALLBACK_ABORT);
	}

	string_to_time(vals[1], &b->bundle_timestamp, names[1]);

	return (BACKEND_CALLBACK_CONTINUE);
}

static int
get_bundle_info(child_info_t *cip, rc_bundle_t *bundle)
{
	backend_query_t *q;
	rep_protocol_responseid_t r;
	int rv = BACKEND_CALLBACK_CONTINUE;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT bundle_name, bundle_timestamp FROM bundle_tbl "
	    "WHERE bundle_id = %d", bundle->bundle_id);
	r = backend_tx_run(cip->ci_tx, q, fill_bundle_cb, bundle);
	backend_query_free(q);
	if (r != REP_PROTOCOL_SUCCESS)
		rv = BACKEND_CALLBACK_ABORT;
	return (rv);
}

/*
 * Read the values from the repository for the specified value ID and store
 * them in a value set.  The address of the value set will be placed at
 * *rvs and it will have a hold on it.
 */
static int
fill_values(child_info_t *cip, rc_value_set_t **rvs, uint32_t value_id)
{
	rep_protocol_responseid_t r;
	backend_type_t backend;
	backend_query_t *q;
	rep_protocol_responseid_t rval;

	backend = cip->ci_base_nl.rl_backend;
	/*
	 * The rc_value_set_t retrieved by rc_vs_get will have a hold
	 * placed on it for us.
	 */
	rval = rc_vs_get(value_id, backend, RC_VALUE_ACCESS_FILL,
	    rvs);
	switch (rval) {
	case REP_PROTOCOL_SUCCESS:
		q = backend_query_alloc();
		backend_query_add(q,
		    "SELECT value_value FROM value_tbl "
		    "WHERE (value_id = %d) ORDER BY value_order",
		    value_id);

		/* Add the values to the value set. */
		r = backend_tx_run(cip->ci_tx, q, property_value_cb, *rvs);
		backend_query_free(q);

		switch (r) {
		case REP_PROTOCOL_SUCCESS:
			break;

		case REP_PROTOCOL_FAIL_NO_RESOURCES:
			rc_vs_fill_failed(*rvs);
			rc_vs_release(*rvs);
			*rvs = NULL;
			return (BACKEND_CALLBACK_ABORT);

		case REP_PROTOCOL_DONE:
		default:
			backend_panic("backend_tx_run() returned %d",
			    r);
		}

		rc_vs_filled(*rvs);
		break;

	case REP_PROTOCOL_FAIL_EXISTS:
		/*
		 * Already have the values.  No need to query.
		 */
		break;

	default:
		return (BACKEND_CALLBACK_ABORT);
	};

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Called with the following columns from the repository:
 *
 *	0	lnk_prop_name
 *	1	lnk_prop_id
 *	2	lnk_prop_type
 *	3	lnk_val_id
 *	4	lnk_gen_id
 *	5	lnk_decoration_id
 */
/*ARGSUSED*/
static int
fill_property_callback(void *data, int columns, char **vals, char **names)
{
	child_info_t *cp = data;
	backend_tx_t *tx = cp->ci_tx;
	uint32_t gen_id;
	uint32_t main_id;
	uint32_t dec_flags;
	const char *name;
	const char *cur;
	rc_value_set_t *vs;
	uint32_t value_id;
	rep_protocol_value_type_t type;
	rc_node_lookup_t *lp = &cp->ci_base_nl;
	uint32_t decoration_id;
	int rc;

	assert(columns == 6);
	assert(tx != NULL);

	vs = NULL;

	name = *vals++;
	names++;

	cur = *vals++;
	string_to_id(cur, &main_id, *names++);

	cur = *vals++;
	names++;
	type = string_to_prop_type(cur);

	lp->rl_main_id = main_id;

	/*
	 * fill in the values, if any
	 */
	string_to_id(*vals++, &value_id, *names);
	if (value_id != 0) {
		/*
		 * fill_values() will return with a hold on the
		 * rc_value_set_t.
		 */
		if (fill_values(cp, &vs, value_id) !=
		    BACKEND_CALLBACK_CONTINUE) {
			return (BACKEND_CALLBACK_ABORT);
		}
	}
	names++;

	/*
	 * Get the generation ID.
	 */
	string_to_id(*vals, &gen_id, *names);
	vals++;
	names++;

	/*
	 * Get the decoration id, if any.
	 *
	 * A property may not have a decoration if the prop_lnk_tbl row is
	 * pointed to only by a previous snapshot.  Or if the database has
	 * not been upgraded yet.
	 */
	string_to_id(*vals, &decoration_id, *names);
	rc = get_decoration_flags(cp->ci_tx, decoration_id, gen_id,
	    cp->ignoresnap, &dec_flags);
	if (rc != REP_PROTOCOL_SUCCESS) {
		rc_vs_release(vs);
		return (BACKEND_CALLBACK_ABORT);
	}

	rc = rc_node_create_property(cp->ci_parent, lp, name, type,
	    vs, gen_id, decoration_id, dec_flags);
	/* Release the hold that fill_values() obtained for us. */
	rc_vs_release(vs);
	if (rc != REP_PROTOCOL_SUCCESS) {
		assert(rc == REP_PROTOCOL_FAIL_NO_RESOURCES);
		return (BACKEND_CALLBACK_ABORT);
	}

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Create a decoration rc_node_t using the data from the query.  The query
 * data consists of:
 *
 *	0	decoration_key
 *	1	decoration_id
 *	2	decoration_value_id
 *	3	decoration_gen_id
 *	4	decoration_layer
 *	5	decoration_bundle_id
 *	6	decoration_type
 *	7	decoration_entity_type
 *	8	decoration_flags
 */
/* ARGSUSED */
static int
fill_decoration_callback(void *data, int columns, char **vals, char **names)
{
	rc_bundle_t bundle;
	child_info_t *cip = data;
	rc_node_t *np;
	uint32_t key;
	uint32_t decoration_id;
	uint32_t decoration_flags;
	uint32_t value_id = 0;
	uint32_t gen_id;
	rep_protocol_decoration_layer_t layer_id;
	decoration_type_t type;
	rc_node_lookup_t *lp;
	rep_protocol_value_type_t prop_type;
	rc_value_set_t *vs;

	assert(columns == 9);

	lp = &cip->ci_base_nl;
	(void) memset(&bundle, 0, sizeof (bundle));

	string_to_id(vals[0], &key, names[0]);
	string_to_id(vals[1], &decoration_id, names[1]);
	string_to_id(vals[2], &value_id, names[2]);
	string_to_id(vals[3], &gen_id, names[3]);
	string_to_id(vals[4], (uint32_t *)&layer_id, names[4]);
	string_to_id(vals[5], &bundle.bundle_id, names[5]);
	string_to_id(vals[6], (uint32_t *)&type, names[6]);
	if (type == DECORATION_TYPE_PROP)
		prop_type = string_to_prop_type(vals[7]);
	else
		prop_type = REP_PROTOCOL_TYPE_INVALID;

	string_to_id(vals[8], &decoration_flags, names[8]);

	/* Read in the values. */
	vs = NULL;
	if (value_id != 0) {
		/*
		 * fill_values() will return with a hold on the
		 * rc_value_set_t.
		 */
		if (fill_values(cip, &vs, value_id) !=
		    BACKEND_CALLBACK_CONTINUE) {
			return (BACKEND_CALLBACK_ABORT);
		}
	}

	/* Read in the bundle information */
	if (bundle.bundle_id != 0) {
		if (get_bundle_info(cip, &bundle) !=
		    BACKEND_CALLBACK_CONTINUE) {
			rc_vs_release(vs);
			return (BACKEND_CALLBACK_ABORT);
		}
	}
	lp->rl_main_id = key;

	np = rc_node_setup_decoration(cip->ci_parent, lp, decoration_id,
	    decoration_flags, prop_type, vs, gen_id, layer_id, &bundle, type);
	/* Release the hold that fill_values() obtained for us. */
	rc_vs_release(vs);
	uu_free((void *)bundle.bundle_name);
	if (np == NULL)
		return (BACKEND_CALLBACK_ABORT);
	rc_node_rele(np);

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * For the selected bundle ID, fill in the decorations that correspond to
 * cip->ci_parent's decoration ID.  cip->ci_parent is a property.  We need
 * to make sure that we do not select any decorations that are newer (have
 * a higher gen_id) than the property.
 *
 * Columns are:
 *	0	bundle ID
 *	1	decoration_layer
 */
/* ARGSUSED */
static int
bundle_id_callback(void *data, int columns, char **vals, char **names)
{
	child_info_t *cip = data;
	uint32_t bundle_id;
	uint32_t layer;
	rc_node_t *np;
	backend_query_t *q;
	int rv;

	assert(columns == 2);
	assert(cip->ci_parent->rn_id.rl_type == REP_PROTOCOL_ENTITY_PROPERTY);

	string_to_id(vals[0], &bundle_id, names[0]);
	string_to_id(vals[1], &layer, names[1]);

	np = cip->ci_parent;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT decoration_key, decoration_id, "
	    "decoration_value_id, decoration_gen_id, decoration_layer, "
	    "decoration_bundle_id, decoration_type, "
	    "decoration_entity_type, decoration_flags "
	    "FROM decoration_tbl "
	    "WHERE (decoration_id = %d) "
	    "AND (decoration_bundle_id = %d) "
	    "AND (decoration_layer = %d) "
	    "AND (decoration_gen_id <= %d) "
	    "ORDER BY decoration_gen_id DESC LIMIT 1; ",
	    np->rn_decoration_id, bundle_id, layer, np->rn_gen_id);
	rv = backend_tx_run(cip->ci_tx, q, fill_decoration_callback, cip);
	backend_query_free(q);

	return (rv);
}

/*
 * We use a two phase approach to get the decorations for a property.
 * First in this function we get the bundle IDs associated with the
 * property's decoration ID.  The bundle_id_callback() uses the bundle IDs
 * to get the most recent applicable decoration.
 */
static int
get_property_decorations(child_info_t *cip, backend_query_t *q)
{
	rc_node_t *np;
	int rv;

	assert(cip->ci_tx != NULL);

	np = cip->ci_parent;

	assert(np->rn_id.rl_type == REP_PROTOCOL_ENTITY_PROPERTY);
	assert(np->rn_id.rl_backend == BACKEND_TYPE_NORMAL);
	assert(np->rn_decoration_id != 0);

	/*
	 * In order to support multiple manifests, we need to consider the
	 * decorations for each unique bundle ID.  Ordering the result in
	 * descending layer order will allow libscf queries to see the
	 * highest level decorations first.
	 */
	backend_query_add(q,
	    "SELECT DISTINCT decoration_bundle_id, decoration_layer "
	    "FROM (select decoration_bundle_id, decoration_layer, "
	    "    decoration_gen_id FROM decoration_tbl "
	    "    WHERE (decoration_id = %d AND "
	    "    (decoration_flags & %d) = 0) "
	    "    ORDER by decoration_gen_id DESC) "
	    "ORDER BY decoration_layer DESC, decoration_gen_id DESC; ",
	    np->rn_decoration_id, DECORATION_IN_USE);
	rv = backend_tx_run(cip->ci_tx, q, bundle_id_callback, cip);

	return (rv);
}

static int
get_decorations(child_info_t *cip)
{
	rc_node_t *np;
	backend_query_t *q;
	backend_tx_t *tx;
	int i;
	int res;

	/*
	 * None of our callers should have the repository locked.
	 */
	assert(cip->ci_tx == NULL);

	/*
	 * Only the persistent repository has decorations.
	 */
	if (cip->ci_base_nl.rl_backend != BACKEND_TYPE_NORMAL)
		return (REP_PROTOCOL_SUCCESS);

	np = cip->ci_parent;

	/*
	 * Don't bother to query if the decoration ID is 0.
	 */
	if (np->rn_decoration_id == 0)
		return (REP_PROTOCOL_SUCCESS);

	/* Take the backend lock for a series of queries. */
	res = backend_tx_begin_ro(np->rn_id.rl_backend, &tx);
	if (res != REP_PROTOCOL_SUCCESS) {
		/*
		 * If the backend didn't exist, we shouldn't get here.
		 */
		assert(res != REP_PROTOCOL_FAIL_BACKEND_ACCESS);
		return (res);
	}
	cip->ci_tx = tx;

	q = backend_query_alloc();

	switch (np->rn_id.rl_type) {
	case REP_PROTOCOL_ENTITY_SERVICE:
	case REP_PROTOCOL_ENTITY_INSTANCE:
	case REP_PROTOCOL_ENTITY_PROPERTYGRP:
		/*
		 * Walk through the layers in reverse order, because libscf
		 * wants the highest numbered ones first.
		 */
		for (i = layer_info_count - 1; i >= 0; i--) {
			backend_query_add(q,
			    "SELECT decoration_key, decoration_id, "
			    "decoration_value_id, decoration_gen_id, "
			    "decoration_layer, "
			    "decoration_bundle_id, decoration_type, "
			    "decoration_entity_type, decoration_flags "
			    "FROM decoration_tbl "
			    "WHERE (decoration_id = %d) "
			    "AND (decoration_layer = %d); ",
			    np->rn_decoration_id, layer_info[i].layer_id);
		}
		res = backend_tx_run(tx, q, fill_decoration_callback, cip);
		if (res == REP_PROTOCOL_DONE)
			res = REP_PROTOCOL_FAIL_NO_RESOURCES;
		break;

	case REP_PROTOCOL_ENTITY_PROPERTY:
		res = get_property_decorations(cip, q);
		break;

	case REP_PROTOCOL_ENTITY_CPROPERTYGRP:
	case REP_PROTOCOL_ENTITY_DECORATION:
	case REP_PROTOCOL_ENTITY_SCOPE:
	case REP_PROTOCOL_ENTITY_SNAPSHOT:
	case REP_PROTOCOL_ENTITY_SNAPLEVEL:
	case REP_PROTOCOL_ENTITY_VALUE:
	default:
		/* None of these entities should be decorated. */
		assert(0);
		break;
	}

	backend_query_free(q);
	backend_tx_end_ro(tx);
	cip->ci_tx = NULL;

	return (res);
}

/*
 * The *_setup_child_info() functions fill in a child_info_t structure with the
 * information for the children of np with type type.
 *
 * They fail with
 *   _TYPE_MISMATCH - object cannot have children of type type
 */

static int
scope_setup_child_info(rc_node_t *np, uint32_t type, child_info_t *cip)
{
	if (type != REP_PROTOCOL_ENTITY_SERVICE)
		return (REP_PROTOCOL_FAIL_TYPE_MISMATCH);

	bzero(cip, sizeof (*cip));
	cip->ci_parent = np;
	cip->ci_base_nl.rl_type = type;
	cip->ci_base_nl.rl_backend = np->rn_id.rl_backend;
	return (REP_PROTOCOL_SUCCESS);
}

static int
service_setup_child_info(rc_node_t *np, uint32_t type, child_info_t *cip)
{
	switch (type) {
	case REP_PROTOCOL_ENTITY_DECORATION:
	case REP_PROTOCOL_ENTITY_INSTANCE:
	case REP_PROTOCOL_ENTITY_PROPERTYGRP:
		break;
	default:
		return (REP_PROTOCOL_FAIL_TYPE_MISMATCH);
	}

	bzero(cip, sizeof (*cip));
	cip->ci_parent = np;
	cip->ci_base_nl.rl_type = type;
	cip->ci_base_nl.rl_backend = np->rn_id.rl_backend;
	cip->ci_base_nl.rl_ids[ID_SERVICE] = np->rn_id.rl_main_id;

	return (REP_PROTOCOL_SUCCESS);
}

static int
instance_setup_child_info(rc_node_t *np, uint32_t type, child_info_t *cip)
{
	switch (type) {
	case REP_PROTOCOL_ENTITY_DECORATION:
	case REP_PROTOCOL_ENTITY_PROPERTYGRP:
	case REP_PROTOCOL_ENTITY_SNAPSHOT:
		break;
	default:
		return (REP_PROTOCOL_FAIL_TYPE_MISMATCH);
	}

	bzero(cip, sizeof (*cip));
	cip->ci_parent = np;
	cip->ci_base_nl.rl_type = type;
	cip->ci_base_nl.rl_backend = np->rn_id.rl_backend;
	cip->ci_base_nl.rl_ids[ID_SERVICE] = np->rn_id.rl_ids[ID_SERVICE];
	cip->ci_base_nl.rl_ids[ID_INSTANCE] = np->rn_id.rl_main_id;

	return (REP_PROTOCOL_SUCCESS);
}

static int
snaplevel_setup_child_info(rc_node_t *np, uint32_t type, child_info_t *cip)
{
	if (type != REP_PROTOCOL_ENTITY_PROPERTYGRP)
		return (REP_PROTOCOL_FAIL_TYPE_MISMATCH);

	bzero(cip, sizeof (*cip));
	cip->ci_parent = np;
	cip->ci_base_nl.rl_type = type;
	cip->ci_base_nl.rl_backend = np->rn_id.rl_backend;
	cip->ci_base_nl.rl_ids[ID_SERVICE] = np->rn_id.rl_ids[ID_SERVICE];
	cip->ci_base_nl.rl_ids[ID_INSTANCE] = np->rn_id.rl_ids[ID_INSTANCE];
	cip->ci_base_nl.rl_ids[ID_NAME] = np->rn_id.rl_ids[ID_NAME];
	cip->ci_base_nl.rl_ids[ID_SNAPSHOT] = np->rn_id.rl_ids[ID_SNAPSHOT];
	cip->ci_base_nl.rl_ids[ID_LEVEL] = np->rn_id.rl_main_id;

	return (REP_PROTOCOL_SUCCESS);
}

static int
propertygrp_setup_child_info(rc_node_t *pg, uint32_t type, child_info_t *cip)
{
	if ((type != REP_PROTOCOL_ENTITY_PROPERTY) &&
	    (type != REP_PROTOCOL_ENTITY_DECORATION)) {
		return (REP_PROTOCOL_FAIL_TYPE_MISMATCH);
	}

	bzero(cip, sizeof (*cip));
	cip->ci_parent = pg;
	cip->ci_base_nl.rl_type = type;
	cip->ci_base_nl.rl_backend = pg->rn_id.rl_backend;
	cip->ci_base_nl.rl_ids[ID_SERVICE] = pg->rn_id.rl_ids[ID_SERVICE];
	cip->ci_base_nl.rl_ids[ID_INSTANCE] = pg->rn_id.rl_ids[ID_INSTANCE];
	cip->ci_base_nl.rl_ids[ID_PG] = pg->rn_id.rl_main_id;
	cip->ci_base_nl.rl_ids[ID_GEN] = pg->rn_gen_id;
	cip->ci_base_nl.rl_ids[ID_NAME] = pg->rn_id.rl_ids[ID_NAME];
	cip->ci_base_nl.rl_ids[ID_SNAPSHOT] = pg->rn_id.rl_ids[ID_SNAPSHOT];
	cip->ci_base_nl.rl_ids[ID_LEVEL] = pg->rn_id.rl_ids[ID_LEVEL];

	return (REP_PROTOCOL_SUCCESS);
}

static int
property_setup_child_info(rc_node_t *prop, uint32_t type, child_info_t *cip)
{
	if (type != REP_PROTOCOL_ENTITY_DECORATION)
		return (REP_PROTOCOL_FAIL_TYPE_MISMATCH);

	bzero(cip, sizeof (*cip));
	cip->ci_parent = prop;
	cip->ci_base_nl.rl_type = type;
	cip->ci_base_nl.rl_backend = prop->rn_id.rl_backend;
	cip->ci_base_nl.rl_ids[ID_SERVICE] = prop->rn_id.rl_ids[ID_SERVICE];
	cip->ci_base_nl.rl_ids[ID_INSTANCE] = prop->rn_id.rl_ids[ID_INSTANCE];
	cip->ci_base_nl.rl_ids[ID_NAME] = prop->rn_id.rl_ids[ID_NAME];
	cip->ci_base_nl.rl_ids[ID_SNAPSHOT] = prop->rn_id.rl_ids[ID_SNAPSHOT];
	cip->ci_base_nl.rl_ids[ID_LEVEL] = prop->rn_id.rl_ids[ID_LEVEL];
	cip->ci_base_nl.rl_ids[ID_PG] = prop->rn_id.rl_ids[ID_PG];
	cip->ci_base_nl.rl_ids[ID_PROPERTY] = prop->rn_id.rl_main_id;

	return (REP_PROTOCOL_SUCCESS);
}

/*
 * The *_is_in_repo() functions check to see if the entity represented by
 * the node still exists in the repository.  This check is needed because
 * muting operations can result in some entities being deleted from the
 * repository.
 *
 * The functions return _SUCCESS if the entity is still represented in the
 * repository and _NOT_FOUND if there is no representation in the
 * repository.
 */

/*
 * We'll use backend_tx_run_single_int() to see if there is a record with
 * the key specified in id in the specified table.  We don't really care
 * about the returned integer, but backend_tx_run_single_int() already does
 * all the bookkeeping that we would have to do otherwise.
 *
 * Returns:
 *	_NO_RESOURCES - out of memory
 *	_NOT_FOUND - the desired record does not exist in the repository
 *	_SUCCESS - the desired record is in the repository
 */
static int
object_is_in_table(backend_tx_t *tx, rc_node_lookup_t *id, const char *table,
    const char *key_field)
{
	backend_query_t *q;
	int rc;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT 1 FROM %s WHERE %s = %d;",
	    table, key_field, id->rl_main_id);
	rc = backend_tx_run(tx, q, backend_fail_if_seen, NULL);
	backend_query_free(q);
	switch (rc) {
	case REP_PROTOCOL_SUCCESS:
		rc = REP_PROTOCOL_FAIL_NOT_FOUND;
		break;
	case REP_PROTOCOL_DONE:
		rc = REP_PROTOCOL_SUCCESS;
		break;

		/* No rc modification required for other cases. */
	}

	return (rc);
}

static int
object_update_gen_id(backend_tx_t *tx, rc_node_t *np, const char *table,
    const char *key_field, const char *gen_field)
{
	backend_query_t *q;
	uint32_t gen_id;
	int rc;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT %s FROM %s WHERE %s = %d;",
	    gen_field, table, key_field, np->rn_id.rl_main_id);
	rc = backend_tx_run_single_int(tx, q, &gen_id);
	backend_query_free(q);
	if (rc == REP_PROTOCOL_SUCCESS) {
		rc_node_set_gen_id(np, gen_id);
	}
	return (rc);
}

/*
 * Scope never has any backing store, so lie and return success.
 */
/* ARGSUSED0 */
static int
scope_is_in_repo(backend_tx_t *unused_tx, rc_node_t *unused_node)
{
	return (REP_PROTOCOL_SUCCESS);
}

static int
service_is_in_repo(backend_tx_t *tx, rc_node_t *np)
{
	return (object_is_in_table(tx, &np->rn_id, "service_tbl", "svc_id"));
}

static int
instance_is_in_repo(backend_tx_t *tx, rc_node_t *np)
{
	return (object_is_in_table(tx, &np->rn_id, "instance_tbl",
	    "instance_id"));
}

static int
snapshot_is_in_repo(backend_tx_t *tx, rc_node_t *np)
{
	return (object_is_in_table(tx, &np->rn_id, "snapshot_lnk_tbl",
	    "lnk_id"));
}

static int
snaplevel_is_in_repo(backend_tx_t *tx, rc_node_t *np)
{
	return (object_is_in_table(tx, &np->rn_id, "snaplevel_tbl",
	    "snap_level_id"));
}

static int
propertygrp_is_in_repo(backend_tx_t *tx, rc_node_t *np)
{
	return (object_is_in_table(tx, &np->rn_id, "pg_tbl", "pg_id"));
}

static int
property_is_in_repo(backend_tx_t *tx, rc_node_t *np)
{
	return (object_is_in_table(tx, &np->rn_id, "prop_lnk_tbl",
	    "lnk_prop_id"));
}

static int
decoration_is_in_repo(backend_tx_t *tx, rc_node_t *np)
{
	return (object_is_in_table(tx, &np->rn_id, "decoration_tbl",
	    "decoration_key"));
}

static int
decoration_update_gen_id(backend_tx_t *tx, rc_node_t *np)
{
	return (object_update_gen_id(tx, np, "decoration_tbl",
	    "decoration_key", "decoration_gen_id"));
}

/*
 * The *_fill_children() functions populate the children of the given rc_node_t
 * by querying the database and calling rc_node_setup_*() functions (usually
 * via a fill_*_callback()).
 *
 * They fail with
 *   _NO_RESOURCES
 */

/*
 * Returns
 *   _TYPE_MISMATCH
 *   _NO_RESOURCES
 *   _SUCCESS
 */
static int
scope_fill_children(rc_node_t *np)
{
	backend_tx_t *tx;
	backend_query_t *q;
	child_info_t ci;
	int res;

	(void) scope_setup_child_info(np, REP_PROTOCOL_ENTITY_SERVICE, &ci);

	res = backend_tx_begin_ro(BACKEND_TYPE_NORMAL, &tx);
	if (res != REP_PROTOCOL_SUCCESS)
		return (res);
	ci.ci_tx = tx;
	q = backend_query_alloc();
	backend_query_append(q, "SELECT svc_name, svc_id, svc_dec_id FROM "
	    "service_tbl");
	res = backend_tx_run(tx, q, fill_child_callback, &ci);
	backend_query_free(q);
	backend_tx_end_ro(tx);

	if (res == REP_PROTOCOL_DONE)
		res = REP_PROTOCOL_FAIL_NO_RESOURCES;
	return (res);
}

/*
 * Returns
 *   _NO_RESOURCES
 *   _SUCCESS
 */
static int
service_fill_children(rc_node_t *np)
{
	backend_query_t *q;
	child_info_t ci;
	backend_tx_t *tx;
	int res;

	assert(np->rn_id.rl_backend == BACKEND_TYPE_NORMAL);

	(void) service_setup_child_info(np, REP_PROTOCOL_ENTITY_INSTANCE, &ci);

	res = backend_tx_begin_ro(BACKEND_TYPE_NORMAL, &tx);
	if (res != REP_PROTOCOL_SUCCESS)
		return (res);
	ci.ci_tx = tx;
	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT instance_name, instance_id, instance_dec_id "
	    "    FROM instance_tbl WHERE (instance_svc = %d)",
	    np->rn_id.rl_main_id);
	res = backend_tx_run(tx, q, fill_child_callback, &ci);
	backend_query_free(q);

	if (res == REP_PROTOCOL_DONE) {
		res = REP_PROTOCOL_FAIL_NO_RESOURCES;
	}
	if (res != REP_PROTOCOL_SUCCESS) {
		backend_tx_end_ro(tx);
		return (res);
	}

	(void) service_setup_child_info(np, REP_PROTOCOL_ENTITY_PROPERTYGRP,
	    &ci);
	ci.ci_tx = tx;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT pg_name, pg_id, pg_gen_id, pg_type, pg_flags, pg_dec_id"
	    "    FROM pg_tbl WHERE (pg_parent_id = %d)",
	    np->rn_id.rl_main_id);

	ci.ignoresnap = 1;
	ci.ci_base_nl.rl_backend = BACKEND_TYPE_NORMAL;
	res = backend_tx_run(tx, q, fill_pg_callback, &ci);
	backend_tx_end_ro(tx);
	ci.ci_tx = NULL;
	if (res == REP_PROTOCOL_SUCCESS) {
		res = backend_tx_begin_ro(BACKEND_TYPE_NONPERSIST, &tx);
		if (res == REP_PROTOCOL_SUCCESS) {
			ci.ci_tx = tx;
			ci.ci_base_nl.rl_backend = BACKEND_TYPE_NONPERSIST;
			res = backend_tx_run(tx, q, fill_pg_callback, &ci);
			backend_tx_end_ro(tx);
			ci.ci_tx = NULL;
		}
		/* nonpersistent database may not exist */
		if (res == REP_PROTOCOL_FAIL_BACKEND_ACCESS)
			res = REP_PROTOCOL_SUCCESS;
	}
	backend_query_free(q);
	if (res == REP_PROTOCOL_DONE)
		res = REP_PROTOCOL_FAIL_NO_RESOURCES;
	if (res != REP_PROTOCOL_SUCCESS)
		return (res);

	/* Read in our decorations */
	res = service_setup_child_info(np, REP_PROTOCOL_ENTITY_DECORATION,
	    &ci);
	if (res != REP_PROTOCOL_SUCCESS)
		return (res);
	res = get_decorations(&ci);

	return (res);
}

/*
 * Returns
 *   _NO_RESOURCES
 *   _SUCCESS
 *   _TYPE_MISMATCH
 */
static int
instance_fill_children(rc_node_t *np)
{
	backend_tx_t *tx;
	backend_query_t *q;
	child_info_t ci;
	int res;

	assert(np->rn_id.rl_backend == BACKEND_TYPE_NORMAL);

	/* Get child property groups */
	(void) instance_setup_child_info(np, REP_PROTOCOL_ENTITY_PROPERTYGRP,
	    &ci);

	res = backend_tx_begin_ro(BACKEND_TYPE_NORMAL, &tx);
	if (res != REP_PROTOCOL_SUCCESS)
		return (res);

	ci.ignoresnap = 1;
	ci.ci_tx = tx;
	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT pg_name, pg_id, pg_gen_id, pg_type, pg_flags, pg_dec_id"
	    "    FROM pg_tbl WHERE (pg_parent_id = %d)",
	    np->rn_id.rl_main_id);
	ci.ci_base_nl.rl_backend = BACKEND_TYPE_NORMAL;
	res = backend_tx_run(tx, q, fill_pg_callback, &ci);
	backend_tx_end_ro(tx);
	ci.ci_tx = NULL;
	if (res == REP_PROTOCOL_SUCCESS) {
		res = backend_tx_begin_ro(BACKEND_TYPE_NONPERSIST, &tx);
		if (res == REP_PROTOCOL_SUCCESS) {
			ci.ci_tx = tx;
			ci.ci_base_nl.rl_backend = BACKEND_TYPE_NONPERSIST;
			res = backend_tx_run(tx, q, fill_pg_callback, &ci);
			backend_tx_end_ro(tx);
			ci.ci_tx = NULL;
		}
		/* nonpersistent database may not exist */
		if (res == REP_PROTOCOL_FAIL_BACKEND_ACCESS)
			res = REP_PROTOCOL_SUCCESS;
	}
	if (res == REP_PROTOCOL_DONE)
		res = REP_PROTOCOL_FAIL_NO_RESOURCES;
	backend_query_free(q);

	if (res != REP_PROTOCOL_SUCCESS)
		return (res);

	/* Get child snapshots */
	(void) instance_setup_child_info(np, REP_PROTOCOL_ENTITY_SNAPSHOT,
	    &ci);

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT lnk_snap_name, lnk_id, lnk_snap_id FROM snapshot_lnk_tbl"
	    "    WHERE (lnk_inst_id = %d)",
	    np->rn_id.rl_main_id);
	res = backend_run(BACKEND_TYPE_NORMAL, q, fill_snapshot_callback, &ci);
	if (res == REP_PROTOCOL_DONE)
		res = REP_PROTOCOL_FAIL_NO_RESOURCES;
	backend_query_free(q);

	/* Get decorations */
	res = instance_setup_child_info(np, REP_PROTOCOL_ENTITY_DECORATION,
	    &ci);
	if (res != REP_PROTOCOL_SUCCESS)
		return (res);
	res = get_decorations(&ci);

	return (res);
}

/*
 * Returns
 *   _NO_RESOURCES
 *   _SUCCESS
 */
static int
snapshot_fill_children(rc_node_t *np)
{
	rc_node_t *nnp;
	rc_snapshot_t *sp, *oldsp;
	rc_snaplevel_t *lvl;
	rc_node_lookup_t nl;
	int r;

	/* Get the rc_snapshot_t (& its rc_snaplevel_t's). */
	(void) pthread_mutex_lock(&np->rn_lock);
	sp = np->rn_snapshot;
	(void) pthread_mutex_unlock(&np->rn_lock);
	if (sp == NULL) {
		r = rc_snapshot_get(np->rn_snapshot_id, &sp);
		if (r != REP_PROTOCOL_SUCCESS) {
			assert(r == REP_PROTOCOL_FAIL_NO_RESOURCES);
			return (r);
		}
		(void) pthread_mutex_lock(&np->rn_lock);
		oldsp = np->rn_snapshot;
		assert(oldsp == NULL || oldsp == sp);
		np->rn_snapshot = sp;
		(void) pthread_mutex_unlock(&np->rn_lock);
		if (oldsp != NULL)
			rc_snapshot_rele(oldsp);
	}

	bzero(&nl, sizeof (nl));
	nl.rl_type = REP_PROTOCOL_ENTITY_SNAPLEVEL;
	nl.rl_backend = np->rn_id.rl_backend;
	nl.rl_ids[ID_SERVICE] = np->rn_id.rl_ids[ID_SERVICE];
	nl.rl_ids[ID_INSTANCE] = np->rn_id.rl_ids[ID_INSTANCE];
	nl.rl_ids[ID_NAME] = np->rn_id.rl_main_id;
	nl.rl_ids[ID_SNAPSHOT] = np->rn_snapshot_id;

	/* Create rc_node_t's for the snapshot's rc_snaplevel_t's. */
	for (lvl = sp->rs_levels; lvl != NULL; lvl = lvl->rsl_next) {
		nnp = rc_node_alloc();
		assert(nnp != NULL);
		nl.rl_main_id = lvl->rsl_level_id;
		nnp = rc_node_setup_snaplevel(nnp, &nl, lvl, np);
		rc_node_rele(nnp);
	}

	return (REP_PROTOCOL_SUCCESS);
}

/*
 * Returns
 *   _NO_RESOURCES
 *   _SUCCESS
 */
static int
snaplevel_fill_children(rc_node_t *np)
{
	rc_snaplevel_t *lvl = np->rn_snaplevel;
	child_info_t ci;
	int res;
	backend_query_t *q;
	backend_tx_t	*tx;

	(void) snaplevel_setup_child_info(np, REP_PROTOCOL_ENTITY_PROPERTYGRP,
	    &ci);

	res = backend_tx_begin_ro(BACKEND_TYPE_NORMAL, &tx);
	if (res != REP_PROTOCOL_SUCCESS)
		return (res);

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT snaplvl_pg_name, snaplvl_pg_id, snaplvl_gen_id, "
	    "    snaplvl_pg_type, snaplvl_pg_flags, snaplvl_dec_id "
	    "    FROM snaplevel_lnk_tbl "
	    "    WHERE (snaplvl_level_id = %d)",
	    lvl->rsl_level_id);

	ci.ci_tx = tx;
	ci.ignoresnap = 0;
	res = backend_tx_run(tx, q, fill_pg_callback, &ci);
	if (res == REP_PROTOCOL_DONE)
		res = REP_PROTOCOL_FAIL_NO_RESOURCES;

	backend_query_free(q);
	backend_tx_end_ro(tx);

	return (res);
}

/*
 * Returns
 *   _NO_RESOURCES
 *   _SUCCESS
 */
static int
propertygrp_fill_children(rc_node_t *np)
{
	backend_query_t *q;
	child_info_t ci;
	int res;
	backend_tx_t *tx;

	backend_type_t backend = np->rn_id.rl_backend;

	(void) propertygrp_setup_child_info(np, REP_PROTOCOL_ENTITY_PROPERTY,
	    &ci);

	res = backend_tx_begin_ro(backend, &tx);
	if (res != REP_PROTOCOL_SUCCESS) {
		/*
		 * If the backend didn't exist, we wouldn't have got this
		 * property group.
		 */
		assert(res != REP_PROTOCOL_FAIL_BACKEND_ACCESS);
		return (res);
	}

	ci.ci_tx = tx;
	if (np->rn_parent->rn_id.rl_type == REP_PROTOCOL_ENTITY_SNAPLEVEL)
		ci.ignoresnap = 0;
	else
		ci.ignoresnap = 1;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT lnk_prop_name, lnk_prop_id, lnk_prop_type, lnk_val_id, "
	    "lnk_gen_id, lnk_decoration_id FROM prop_lnk_tbl "
	    "WHERE (lnk_pg_id = %d AND lnk_gen_id = %d)",
	    np->rn_id.rl_main_id, np->rn_gen_id);
	res = backend_tx_run(tx, q, fill_property_callback, &ci);
	if (res == REP_PROTOCOL_DONE)
		res = REP_PROTOCOL_FAIL_NO_RESOURCES;
	backend_query_free(q);
	backend_tx_end_ro(tx);
	ci.ci_tx = NULL;

	/* Read in our decorations */
	res = propertygrp_setup_child_info(np, REP_PROTOCOL_ENTITY_DECORATION,
	    &ci);
	if (res != REP_PROTOCOL_SUCCESS)
		return (res);
	res = get_decorations(&ci);

	return (res);
}

/*
 * Returns
 *   _NO_RESOURCES
 *   _SUCCESS
 */
static int
property_fill_children(rc_node_t *np)
{
	child_info_t ci;
	int res;

	/*
	 * Some properties may not be decorated.  For instance, properties
	 * in the non-persistent repository are not decorated.
	 */
	if (np->rn_decoration_id == 0)
		return (REP_PROTOCOL_SUCCESS);

	(void) property_setup_child_info(np, REP_PROTOCOL_ENTITY_DECORATION,
	    &ci);
	res = get_decorations(&ci);

	return (res);
}

static int
property_update_gen_id(backend_tx_t *tx, rc_node_t *np)
{
	return (object_update_gen_id(tx, np, "prop_lnk_tbl", "lnk_prop_id",
	    "lnk_gen_id"));
}

/*
 * Fails with
 *   _TYPE_MISMATCH - lp is not for a service
 *   _INVALID_TYPE - lp has invalid type
 *   _BAD_REQUEST - name is invalid
 */
static int
scope_query_child(backend_query_t *q, rc_node_lookup_t *lp, const char *name)
{
	uint32_t type = lp->rl_type;
	int rc;

	if (type != REP_PROTOCOL_ENTITY_SERVICE)
		return (REP_PROTOCOL_FAIL_TYPE_MISMATCH);

	if ((rc = rc_check_type_name(type, name)) != REP_PROTOCOL_SUCCESS)
		return (rc);

	backend_query_add(q,
	    "SELECT svc_id FROM service_tbl "
	    "WHERE svc_name = '%q'",
	    name);

	return (REP_PROTOCOL_SUCCESS);
}

/*
 * Fails with
 *   _NO_RESOURCES - out of memory
 */
static int
scope_insert_child(backend_tx_t *tx, rc_node_lookup_t *lp, const char *name,
    uint32_t *decoration_id)
{
	const repcache_client_t *cp = get_active_client();
	uint32_t bundle_id;
	uint32_t dec_key;
	uint32_t dec_id;

	if (lp->rl_backend == BACKEND_TYPE_NORMAL) {
		struct timeval ts;

		dec_key = backend_new_id(tx, BACKEND_KEY_DECORATION);
		dec_id = backend_new_id(tx, BACKEND_ID_DECORATION);
		if (decoration_id != NULL)
			*decoration_id = dec_id;
		bundle_id = get_bundle_id(tx, cp->rc_file);

		(void) gettimeofday(&ts, NULL);
		return (backend_tx_run_update(tx,
		    "INSERT INTO service_tbl (svc_id, svc_name, svc_dec_id) "
		    "VALUES (%d, '%q', %d); "
		    "INSERT INTO decoration_tbl "
		    "    (decoration_key, decoration_id, decoration_value_id, "
		    "    decoration_gen_id, decoration_layer, "
		    "    decoration_bundle_id, decoration_type, "
		    "    decoration_tv_sec, decoration_tv_usec) "
		    "VALUES (%d, %d, 0, 0, %d, %d, %d, %ld, %ld); ",
		    lp->rl_main_id, name, dec_id,
		    dec_key, dec_id, cp->rc_layer_id, bundle_id,
		    DECORATION_TYPE_SVC, ts.tv_sec, ts.tv_usec));
	} else {
		if (decoration_id != NULL)
			*decoration_id = 0;
		return (backend_tx_run_update(tx,
		    "INSERT INTO service_tbl (svc_id, svc_name) "
		    "VALUES (%d, '%q')",
		    lp->rl_main_id, name));
	}
}

/*
 * Fails with
 *   _TYPE_MISMATCH - lp is not for an instance or property group
 *   _INVALID_TYPE - lp has invalid type
 *   _BAD_REQUEST - name is invalid
 */
static int
service_query_child(backend_query_t *q, rc_node_lookup_t *lp, const char *name)
{
	uint32_t type = lp->rl_type;
	int rc;

	if (type != REP_PROTOCOL_ENTITY_INSTANCE &&
	    type != REP_PROTOCOL_ENTITY_PROPERTYGRP)
		return (REP_PROTOCOL_FAIL_TYPE_MISMATCH);

	if ((rc = rc_check_type_name(type, name)) != REP_PROTOCOL_SUCCESS)
		return (rc);

	switch (type) {
	case REP_PROTOCOL_ENTITY_INSTANCE:
		backend_query_add(q,
		    "SELECT instance_id FROM instance_tbl "
		    "WHERE instance_name = '%q' AND instance_svc = %d",
		    name, lp->rl_ids[ID_SERVICE]);
		break;
	case REP_PROTOCOL_ENTITY_PROPERTYGRP:
		backend_query_add(q,
		    "SELECT pg_id FROM pg_tbl "
		    "    WHERE pg_name = '%q' AND pg_parent_id = %d",
		    name, lp->rl_ids[ID_SERVICE]);
		break;
	default:
		assert(0);
		abort();
	}

	return (REP_PROTOCOL_SUCCESS);
}

/*
 * Fails with
 *   _NO_RESOURCES - out of memory
 */
static int
service_insert_child(backend_tx_t *tx, rc_node_lookup_t *lp, const char *name,
    uint32_t *decoration_id)
{
	const repcache_client_t *cp = get_active_client();
	uint32_t bundle_id;
	uint32_t dec_key;
	uint32_t dec_id;

	if (lp->rl_backend == BACKEND_TYPE_NORMAL) {
		struct timeval ts;

		dec_key = backend_new_id(tx, BACKEND_KEY_DECORATION);
		dec_id = backend_new_id(tx, BACKEND_ID_DECORATION);
		if (decoration_id != NULL)
			*decoration_id = dec_id;
		bundle_id = get_bundle_id(tx, cp->rc_file);

		(void) gettimeofday(&ts, NULL);
		return (backend_tx_run_update(tx,
		    "INSERT INTO instance_tbl "
		    "    (instance_id, instance_name, instance_svc, "
		    "    instance_dec_id) "
		    "VALUES (%d, '%q', %d, %d); "
		    "INSERT INTO decoration_tbl "
		    "    (decoration_key, decoration_id, decoration_value_id, "
		    "    decoration_gen_id, decoration_layer, "
		    "    decoration_bundle_id, decoration_type, "
		    "    decoration_tv_sec, decoration_tv_usec) "
		    "VALUES (%d, %d, 0, 0, %d, %d, %d, %ld, %ld); ",
		    lp->rl_main_id, name, lp->rl_ids[ID_SERVICE], dec_id,
		    dec_key, dec_id, cp->rc_layer_id, bundle_id,
		    DECORATION_TYPE_INST, ts.tv_sec, ts.tv_usec));
	} else {
		if (decoration_id != NULL)
			*decoration_id = 0;
		return (backend_tx_run_update(tx,
		    "INSERT INTO instance_tbl "
		    "    (instance_id, instance_name, instance_svc) "
		    "VALUES (%d, '%q', %d)",
		    lp->rl_main_id, name, lp->rl_ids[ID_SERVICE]));
	}
}

/*
 * Fails with
 *   _NO_RESOURCES - out of memory
 */
static int
instance_insert_child(backend_tx_t *tx, rc_node_lookup_t *lp, const char *name,
    uint32_t *decoration_id)
{
	if (decoration_id != NULL)
		*decoration_id = 0;

	return (backend_tx_run_update(tx,
	    "INSERT INTO snapshot_lnk_tbl "
	    "    (lnk_id, lnk_inst_id, lnk_snap_name, lnk_snap_id) "
	    "VALUES (%d, %d, '%q', 0)",
	    lp->rl_main_id, lp->rl_ids[ID_INSTANCE], name));
}

/*
 * Fails with
 *   _TYPE_MISMATCH - lp is not for a property group or snapshot
 *   _INVALID_TYPE - lp has invalid type
 *   _BAD_REQUEST - name is invalid
 */
static int
instance_query_child(backend_query_t *q, rc_node_lookup_t *lp, const char *name)
{
	uint32_t type = lp->rl_type;
	int rc;

	if (type != REP_PROTOCOL_ENTITY_PROPERTYGRP &&
	    type != REP_PROTOCOL_ENTITY_SNAPSHOT)
		return (REP_PROTOCOL_FAIL_TYPE_MISMATCH);

	if ((rc = rc_check_type_name(type, name)) != REP_PROTOCOL_SUCCESS)
		return (rc);

	switch (type) {
	case REP_PROTOCOL_ENTITY_PROPERTYGRP:
		backend_query_add(q,
		    "SELECT pg_id FROM pg_tbl "
		    "    WHERE pg_name = '%q' AND pg_parent_id = %d",
		    name, lp->rl_ids[ID_INSTANCE]);
		break;
	case REP_PROTOCOL_ENTITY_SNAPSHOT:
		backend_query_add(q,
		    "SELECT lnk_id FROM snapshot_lnk_tbl "
		    "    WHERE lnk_snap_name = '%q' AND lnk_inst_id = %d",
		    name, lp->rl_ids[ID_INSTANCE]);
		break;
	default:
		assert(0);
		abort();
	}

	return (REP_PROTOCOL_SUCCESS);
}

static int
generic_insert_pg_child(backend_tx_t *tx, rc_node_lookup_t *lp,
    const char *name, const char *pgtype, uint32_t flags, uint32_t gen,
    uint32_t *decoration_id)
{
	int parent_id = (lp->rl_ids[ID_INSTANCE] != 0)?
	    lp->rl_ids[ID_INSTANCE] : lp->rl_ids[ID_SERVICE];

	const repcache_client_t *cp = get_active_client();
	uint32_t bundle_id;
	uint32_t dec_key;
	uint32_t dec_id;

	if (lp->rl_backend == BACKEND_TYPE_NORMAL) {
		struct timeval ts;

		dec_key = backend_new_id(tx, BACKEND_KEY_DECORATION);
		dec_id = backend_new_id(tx, BACKEND_ID_DECORATION);
		if (decoration_id != NULL)
			*decoration_id = dec_id;
		bundle_id = get_bundle_id(tx, cp->rc_file);

		(void) gettimeofday(&ts, NULL);
		return (backend_tx_run_update(tx,
		    "INSERT INTO pg_tbl "
		    "    (pg_id, pg_name, pg_parent_id, pg_type, pg_flags, "
		    "    pg_gen_id, pg_dec_id) "
		    "VALUES (%d, '%q', %d, '%q', %d, %d, %d); "
		    "INSERT INTO decoration_tbl "
		    "    (decoration_key, decoration_id, "
		    "    decoration_entity_type, decoration_value_id, "
		    "    decoration_gen_id, decoration_layer, "
		    "    decoration_bundle_id, decoration_type, "
		    "    decoration_tv_sec, decoration_tv_usec) "
		    "VALUES (%d, %d, '%q', 0, %d, %d, %d, %d, %ld, %ld); ",
		    lp->rl_main_id, name, parent_id, pgtype, flags, gen, dec_id,
		    dec_key, dec_id, pgtype, gen, cp->rc_layer_id, bundle_id,
		    DECORATION_TYPE_PG, ts.tv_sec, ts.tv_usec));
	} else {
		if (decoration_id != NULL)
			*decoration_id = 0;

		return (backend_tx_run_update(tx,
		    "INSERT INTO pg_tbl "
		    "    (pg_id, pg_name, pg_parent_id, pg_type, pg_flags, "
		    "    pg_gen_id) "
		    "VALUES (%d, '%q', %d, '%q', %d, %d); ",
		    lp->rl_main_id, name, parent_id, pgtype, flags, gen));
	}
}

static int	instance_delete_start(rc_node_t *, delete_info_t *);

/* ARGSUSED */
static int
check_for_instances(void *data, int columns, char **vals, char **names)
{
	backend_query_t *q = backend_query_alloc();
	struct delete_cb_info *info = data;
	uint32_t f = 0;
	int r;

	/*
	 * vals[1] is unused, but passed in to unify the function prototypes for
	 * check_for_instances() and push_delete_callback() so that
	 * service_delete_start() can use them as callbacks for the same query.
	 */
	const char *inst_id_str = vals[0];
	const char *dec_str = vals[2];

	assert(columns == 3);

	backend_query_add(q,
	    "SELECT decoration_flags FROM decoration_tbl "
	    "    WHERE decoration_id = '%q' "
	    "    ORDER BY decoration_layer DESC LIMIT 1 ",
	    dec_str);

	r = backend_tx_run_single_int(info->dci_dip->di_tx, q, &f);

	if (r == REP_PROTOCOL_SUCCESS && (f & DECORATION_MASK) == 0) {
		info->dci_result = REP_PROTOCOL_DONE;
		r = BACKEND_CALLBACK_ABORT;
	} else {
		/*
		 * The instance is masked... so we need to handle
		 * the instance removal.
		 */
		if (f & DECORATION_MASK) {
			rc_node_t np;

			string_to_id(inst_id_str, &np.rn_id.rl_main_id, *names);
			r = instance_delete_start(&np, info->dci_dip);
			if (r != REP_PROTOCOL_SUCCESS)
				r = BACKEND_CALLBACK_ABORT;
		} else {
			r = BACKEND_CALLBACK_CONTINUE;
		}
	}

	return (r);
}

static int
service_delete_start(rc_node_t *np, delete_info_t *dip)
{
	struct delete_cb_info info;
	backend_query_t *q = backend_query_alloc();
	int r;

	/*
	 * For child instances, we want to propagate a delcust.
	 *
	 * check_for_instances propagates the delete/removal if
	 * an instance is masked.
	 *
	 * An undelete should not propagate at anytime.
	 */

	info.dci_dip = dip;
	info.dci_be = BACKEND_TYPE_NORMAL;
	info.dci_cb = NULL;
	info.dci_result = REP_PROTOCOL_SUCCESS;

	if (dip->di_action != REP_PROTOCOL_ENTITY_UNDELETE) {
		backend_query_add(q,
		    "SELECT instance_id, 0, instance_dec_id FROM instance_tbl "
		    "    WHERE instance_svc = %d ",
		    np->rn_id.rl_main_id);

		switch (dip->di_action) {
		case REP_PROTOCOL_ENTITY_DELCUST:
			info.dci_cb = &instance_delete;
			r = backend_tx_run(dip->di_tx, q, push_delete_callback,
			    &info);
			backend_query_reset(q);
			break;

		case REP_PROTOCOL_ENTITY_DELETE:
		case REP_PROTOCOL_ENTITY_REMOVE:
			r = backend_tx_run(dip->di_tx, q, check_for_instances,
			    &info);
			backend_query_reset(q);
			if (r != REP_PROTOCOL_SUCCESS)
				return (REP_PROTOCOL_FAIL_EXISTS);
		}
	}

	info.dci_cb = &service_delete;
	info.dci_result = REP_PROTOCOL_SUCCESS;

	backend_query_add(q,
	    "SELECT svc_id, 0, svc_dec_id FROM service_tbl "
	    "    WHERE svc_id = %d ",
	    np->rn_id.rl_main_id);

	r = backend_tx_run(dip->di_tx, q, push_delete_callback, &info);
	backend_query_free(q);

	if (r == REP_PROTOCOL_DONE) {
		assert(info.dci_result != REP_PROTOCOL_SUCCESS);
		return (info.dci_result);
	}

	return (r);
}

static int
instance_delete_start(rc_node_t *np, delete_info_t *dip)
{
	struct delete_cb_info info;
	backend_query_t *q;
	int r;

	q = backend_query_alloc();

	info.dci_dip = dip;
	info.dci_be = BACKEND_TYPE_NORMAL;
	info.dci_cb = &instance_delete;
	info.dci_result = REP_PROTOCOL_SUCCESS;

	backend_query_add(q,
	    "SELECT instance_id, 0, instance_dec_id FROM instance_tbl "
	    "    WHERE instance_id = %d ",
	    np->rn_id.rl_main_id);

	r = backend_tx_run(dip->di_tx, q, push_delete_callback, &info);
	backend_query_free(q);

	if (r == REP_PROTOCOL_DONE) {
		assert(info.dci_result != REP_PROTOCOL_SUCCESS);
		return (info.dci_result);
	}

	return (r);
}

static int
snapshot_delete_start(rc_node_t *np, delete_info_t *dip)
{
	return (delete_stack_push(dip, BACKEND_TYPE_NORMAL,
	    &snapshot_lnk_delete, np->rn_id.rl_main_id, 0, 0));
}

static int
propertygrp_delete_start(rc_node_t *np, delete_info_t *dip)
{
	struct delete_cb_info info;
	backend_query_t *q;
	int r;

	q = backend_query_alloc();

	info.dci_dip = dip;
	info.dci_be = np->rn_id.rl_backend;
	info.dci_cb = &propertygrp_delete;
	info.dci_result = REP_PROTOCOL_SUCCESS;

	if (np->rn_id.rl_backend == BACKEND_TYPE_NORMAL) {
		backend_query_add(q,
		    "SELECT pg_id, pg_gen_id, pg_dec_id FROM pg_tbl WHERE "
		    "    pg_id = %d ",
		    np->rn_id.rl_main_id);
		r = backend_tx_run(dip->di_tx, q, push_delete_callback, &info);
	} else {
		backend_query_add(q,
		    "SELECT pg_id, pg_gen_id FROM pg_tbl WHERE pg_id = %d ",
		    np->rn_id.rl_main_id);
		r = backend_tx_run(dip->di_np_tx, q, push_delete_callback,
		    &info);
	}
	backend_query_free(q);

	if (r == REP_PROTOCOL_DONE) {
		assert(info.dci_result != REP_PROTOCOL_SUCCESS);
		return (info.dci_result);
	}

	return (r);
}

static int
propertygrp_update_gen_id(backend_tx_t *tx, rc_node_t *np)
{
	return (object_update_gen_id(tx, np, "pg_tbl", "pg_id", "pg_gen_id"));
}

static object_info_t info[] = {
	{REP_PROTOCOL_ENTITY_NONE},
	{REP_PROTOCOL_ENTITY_SCOPE,
		BACKEND_ID_INVALID,
		scope_is_in_repo,
		scope_fill_children,
		scope_setup_child_info,
		scope_query_child,
		scope_insert_child,
		NULL,
		NULL,
		NULL
	},
	{REP_PROTOCOL_ENTITY_SERVICE,
		BACKEND_ID_SERVICE_INSTANCE,
		service_is_in_repo,
		service_fill_children,
		service_setup_child_info,
		service_query_child,
		service_insert_child,
		generic_insert_pg_child,
		service_delete_start,
		NULL
	},
	{REP_PROTOCOL_ENTITY_INSTANCE,
		BACKEND_ID_SERVICE_INSTANCE,
		instance_is_in_repo,
		instance_fill_children,
		instance_setup_child_info,
		instance_query_child,
		instance_insert_child,
		generic_insert_pg_child,
		instance_delete_start,
		NULL
	},
	{REP_PROTOCOL_ENTITY_SNAPSHOT,
		BACKEND_ID_SNAPNAME,
		snapshot_is_in_repo,
		snapshot_fill_children,
		NULL,
		NULL,
		NULL,
		NULL,
		snapshot_delete_start,
		NULL
	},
	{REP_PROTOCOL_ENTITY_SNAPLEVEL,
		BACKEND_ID_SNAPLEVEL,
		snaplevel_is_in_repo,
		snaplevel_fill_children,
		snaplevel_setup_child_info,
	},
	{REP_PROTOCOL_ENTITY_PROPERTYGRP,
		BACKEND_ID_PROPERTYGRP,
		propertygrp_is_in_repo,
		propertygrp_fill_children,
		NULL,
		NULL,
		NULL,
		NULL,
		propertygrp_delete_start,
		propertygrp_update_gen_id
	},
	/*
	 * We don't do anything here for composed property groups, but the
	 * REP_PROTOCOL_ENTITY_* values are used as indexes into this
	 * array.  Thus, we need a place holder for it.
	 */
	{REP_PROTOCOL_ENTITY_CPROPERTYGRP},
	{REP_PROTOCOL_ENTITY_PROPERTY,
		BACKEND_ID_INVALID,	/* prop_lnk_tbl keys assigned by */
					/* sqlite. */
		property_is_in_repo,
		property_fill_children,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		property_update_gen_id
	},
	/*
	 * Most of the work in creating decorations is done automatically
	 * by the backend.
	 */
	{REP_PROTOCOL_ENTITY_DECORATION,
		BACKEND_KEY_DECORATION,
		decoration_is_in_repo,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		decoration_update_gen_id
	},
	{-1UL}
};
#define	NUM_INFO (sizeof (info) / sizeof (*info))

/*
 * object_fill_children() populates the child list of an rc_node_t by calling
 * the appropriate <type>_fill_children() which runs backend queries that
 * call an appropriate fill_*_callback() which takes a row of results,
 * decodes them, and calls an rc_node_setup*() function in rc_node.c to create
 * a child.
 *
 * Fails with
 *   _NO_RESOURCES
 *   _BAD_REQUEST
 */
int
object_fill_children(rc_node_t *pp)
{
	uint32_t type = pp->rn_id.rl_type;
	assert(type > 0 && type < NUM_INFO);

	if (info[type].obj_fill_children == NULL)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);
	return ((*info[type].obj_fill_children)(pp));
}

static delete_result_t
transition_check(delete_result_t last, delete_result_t cur)
{
	delete_result_t rv;

	assert(cur != DELETE_UNDEFINED);
	if (cur == DELETE_NA)
		return (last);

	switch (last) {
	case DELETE_UNDEFINED:
		/* Our first result. */
		rv = cur;
		break;
	case DELETE_DELETED:
		if ((cur == DELETE_MASKED) || (cur == DELETE_UNMASKED)) {
			/* Remember either of the mask results */
			rv = cur;
		} else {
			rv = last;
		}
		break;
	case DELETE_MASKED:
		assert(cur != DELETE_UNMASKED);
		/* Once masked, we stay masked. */
		rv = last;
		break;
	case DELETE_UNMASKED:
		assert(cur != DELETE_MASKED);
		/* Once unmasked, we stay unmasked. */
		rv = last;
		break;
	case DELETE_NA:
		/* Last is never set to NA */
	default:
		assert(0);
		abort();
	}
	return (rv);
}

int
object_check_node(rc_node_t *np)
{
	uint32_t decoration_flags;
	int rc;
	uint32_t type;
	backend_tx_t *tx;
	int ignoresnap;

	type = np->rn_id.rl_type;
	assert(type > 0 && type < NUM_INFO);

	if (info[type].obj_is_in_repo == NULL)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);

	rc = backend_tx_begin_ro(np->rn_id.rl_backend, &tx);
	if (rc != REP_PROTOCOL_SUCCESS)
		return (rc);

	rc = (*info[type].obj_is_in_repo)(tx, np);
	if (rc != REP_PROTOCOL_SUCCESS)
		goto fail;

	/*
	 * If this node is from a snap shot chain then do not ignore
	 * the snapshot only entries.
	 */
	if (np->rn_id.rl_ids[ID_NAME] != 0)
		ignoresnap = 0;
	else
		ignoresnap = 1;

	switch (type) {
	case REP_PROTOCOL_ENTITY_SERVICE:
	case REP_PROTOCOL_ENTITY_INSTANCE:
		rc = get_svc_or_inst_dec_flags(tx, np->rn_decoration_id,
		    &decoration_flags);
		break;
	default:
		rc = get_decoration_flags(tx, np->rn_decoration_id,
		    np->rn_gen_id, ignoresnap, &decoration_flags);
	}
	if (rc != REP_PROTOCOL_SUCCESS)
		goto fail;

	rc_node_set_decoration_flags(np, decoration_flags);

	if (info[type].obj_update_gen_id != NULL &&
	    np->rn_id.rl_ids[ID_NAME] == 0) {
		rc = (*info[type].obj_update_gen_id)(tx, np);
	}

fail:
	backend_tx_end_ro(tx);

	return (rc);
}

int
object_delete(rc_node_t *pp, int action, delete_result_t *mask_indicator)
{
	int rc;

	delete_result_t delete_result;
	delete_result_t flag_change = DELETE_UNDEFINED;
	delete_info_t di;
	delete_ent_t de;

	uint32_t type = pp->rn_id.rl_type;
	assert(type > 0 && type < NUM_INFO);

	if (info[type].obj_delete_start == NULL)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);

	(void) memset(&di, '\0', sizeof (di));
	rc = backend_tx_begin(BACKEND_TYPE_NORMAL, &di.di_tx);
	if (rc != REP_PROTOCOL_SUCCESS)
		return (rc);

	rc = backend_tx_begin(BACKEND_TYPE_NONPERSIST, &di.di_np_tx);
	if (rc == REP_PROTOCOL_FAIL_BACKEND_ACCESS ||
	    rc == REP_PROTOCOL_FAIL_BACKEND_READONLY)
		di.di_np_tx = NULL;
	else if (rc != REP_PROTOCOL_SUCCESS) {
		backend_tx_rollback(di.di_tx);
		return (rc);
	}

	di.di_snapshot = 0;
	di.di_action = action;
	if ((rc = (*info[type].obj_delete_start)(pp, &di)) !=
	    REP_PROTOCOL_SUCCESS) {
		goto fail;
	}

	while (delete_stack_pop(&di, &de)) {
		rc = (*de.de_cb)(&di, &de, &delete_result);
		if (rc != REP_PROTOCOL_SUCCESS) {
			goto fail;
		}

		flag_change = transition_check(flag_change, delete_result);
	}
	if (flag_change == DELETE_UNDEFINED) {
		/*
		 * Nothing happend.  We'll return not applicable rather
		 * than undefined.
		 */
		flag_change = DELETE_NA;
	}
	*mask_indicator = flag_change;

	rc = backend_tx_commit(di.di_tx);
	if (rc != REP_PROTOCOL_SUCCESS)
		backend_tx_rollback(di.di_np_tx);
	else if (di.di_np_tx)
		(void) backend_tx_commit(di.di_np_tx);

	delete_stack_cleanup(&di);

	return (rc);

fail:
	backend_tx_rollback(di.di_tx);
	if (di.di_np_tx)
		backend_tx_rollback(di.di_np_tx);

	delete_stack_cleanup(&di);
	return (rc);
}

static int
object_do_create(backend_tx_t *tx, child_info_t *cip, rc_node_t *pp,
    uint32_t type, const char *name, rc_node_t **cpp, uint32_t *decoration_id)
{
	uint32_t ptype = pp->rn_id.rl_type;

	backend_query_t *q;
	uint32_t id;
	rc_node_t *np = NULL;
	int rc;
	object_info_t *ip;

	rc_node_lookup_t *lp = &cip->ci_base_nl;

	assert(ptype > 0 && ptype < NUM_INFO);

	ip = &info[ptype];

	if (type == REP_PROTOCOL_ENTITY_PROPERTYGRP)
		return (REP_PROTOCOL_FAIL_NOT_APPLICABLE);

	if (ip->obj_setup_child_info == NULL ||
	    ip->obj_query_child == NULL ||
	    ip->obj_insert_child == NULL)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);

	if ((rc = (*ip->obj_setup_child_info)(pp, type, cip)) !=
	    REP_PROTOCOL_SUCCESS)
		return (rc);

	q = backend_query_alloc();
	if ((rc = (*ip->obj_query_child)(q, lp, name)) !=
	    REP_PROTOCOL_SUCCESS) {
		assert(rc == REP_PROTOCOL_FAIL_BAD_REQUEST);
		backend_query_free(q);
		return (rc);
	}

	rc = backend_tx_run_single_int(tx, q, &id);
	backend_query_free(q);

	if (rc == REP_PROTOCOL_SUCCESS)
		return (REP_PROTOCOL_FAIL_EXISTS);
	else if (rc != REP_PROTOCOL_FAIL_NOT_FOUND)
		return (rc);

	assert(info[type].obj_id_space != BACKEND_ID_INVALID);

	if ((lp->rl_main_id = backend_new_id(tx,
	    info[type].obj_id_space)) == 0) {
		return (REP_PROTOCOL_FAIL_NO_RESOURCES);
	}

	if ((np = rc_node_alloc()) == NULL)
		return (REP_PROTOCOL_FAIL_NO_RESOURCES);

	if ((rc = (*ip->obj_insert_child)(tx, lp, name, decoration_id)) !=
	    REP_PROTOCOL_SUCCESS) {
		rc_node_destroy(np);
		return (rc);
	}

	*cpp = np;
	return (REP_PROTOCOL_SUCCESS);
}

/*
 * Fails with
 *   _NOT_APPLICABLE - type is _PROPERTYGRP
 *   _BAD_REQUEST - cannot create children for this type of node
 *		    name is invalid
 *   _TYPE_MISMATCH - object cannot have children of type type
 *   _NO_RESOURCES - out of memory, or could not allocate new id
 *   _BACKEND_READONLY
 *   _BACKEND_ACCESS
 *   _EXISTS - child already exists
 */
int
object_create(rc_node_t *pp, uint32_t type, const char *name, rc_node_t **cpp)
{
	backend_tx_t *tx;
	rc_node_t *np = NULL;
	child_info_t ci;
	uint32_t decoration_id;
	int rc;

	if ((rc = backend_tx_begin(pp->rn_id.rl_backend, &tx)) !=
	    REP_PROTOCOL_SUCCESS) {
		return (rc);
	}

	if ((rc = object_do_create(tx, &ci, pp, type, name, &np,
	    &decoration_id)) != REP_PROTOCOL_SUCCESS) {
		backend_tx_rollback(tx);
		return (rc);
	}

	rc = backend_tx_commit(tx);
	if (rc != REP_PROTOCOL_SUCCESS) {
		rc_node_destroy(np);
		return (rc);
	}

	/* Masked flag is 0, since we're creating an object. */
	*cpp = rc_node_setup(np, &ci.ci_base_nl, name, ci.ci_parent,
	    decoration_id, 0);

	return (REP_PROTOCOL_SUCCESS);
}

/*ARGSUSED*/
int
object_create_pg(rc_node_t *pp, uint32_t type, const char *name,
    const char *pgtype, uint32_t flags, rc_node_t **cpp)
{
	uint32_t ptype = pp->rn_id.rl_type;
	backend_tx_t *tx_ro, *tx_wr;
	backend_query_t *q;
	uint32_t decoration_id;
	uint32_t id;
	uint32_t gen = 0;
	rc_node_t *np = NULL;
	int rc;
	int rc_wr;
	int rc_ro;
	object_info_t *ip;

	int nonpersist = (flags & SCF_PG_FLAG_NONPERSISTENT);

	child_info_t ci;
	rc_node_lookup_t *lp = &ci.ci_base_nl;

	assert(ptype > 0 && ptype < NUM_INFO);

	if (ptype != REP_PROTOCOL_ENTITY_SERVICE &&
	    ptype != REP_PROTOCOL_ENTITY_INSTANCE)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);

	ip = &info[ptype];

	assert(ip->obj_setup_child_info != NULL &&
	    ip->obj_query_child != NULL &&
	    ip->obj_insert_pg_child != NULL);

	if ((rc = (*ip->obj_setup_child_info)(pp, type, &ci)) !=
	    REP_PROTOCOL_SUCCESS)
		return (rc);

	q = backend_query_alloc();
	if ((rc = (*ip->obj_query_child)(q, lp, name)) !=
	    REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return (rc);
	}

	if (!nonpersist) {
		lp->rl_backend = BACKEND_TYPE_NORMAL;
		rc_wr = backend_tx_begin(BACKEND_TYPE_NORMAL, &tx_wr);
		rc_ro = backend_tx_begin_ro(BACKEND_TYPE_NONPERSIST, &tx_ro);
	} else {
		lp->rl_backend = BACKEND_TYPE_NONPERSIST;
		rc_ro = backend_tx_begin_ro(BACKEND_TYPE_NORMAL, &tx_ro);
		rc_wr = backend_tx_begin(BACKEND_TYPE_NONPERSIST, &tx_wr);
	}

	if (rc_wr != REP_PROTOCOL_SUCCESS) {
		rc = rc_wr;
		goto fail;
	}
	if (rc_ro != REP_PROTOCOL_SUCCESS &&
	    rc_ro != REP_PROTOCOL_FAIL_BACKEND_ACCESS) {
		rc = rc_ro;
		goto fail;
	}

	if (tx_ro != NULL) {
		rc = backend_tx_run_single_int(tx_ro, q, &id);

		if (rc == REP_PROTOCOL_SUCCESS) {
			backend_query_free(q);
			rc = REP_PROTOCOL_FAIL_EXISTS;
			goto fail;
		} else if (rc != REP_PROTOCOL_FAIL_NOT_FOUND) {
			backend_query_free(q);
			goto fail;
		}
	}

	rc = backend_tx_run_single_int(tx_wr, q, &id);
	backend_query_free(q);

	if (rc == REP_PROTOCOL_SUCCESS) {
		rc = REP_PROTOCOL_FAIL_EXISTS;
		goto fail;
	} else if (rc != REP_PROTOCOL_FAIL_NOT_FOUND) {
		goto fail;
	}

	if (tx_ro != NULL)
		backend_tx_end_ro(tx_ro);
	tx_ro = NULL;

	assert(info[type].obj_id_space != BACKEND_ID_INVALID);

	if ((lp->rl_main_id = backend_new_id(tx_wr,
	    info[type].obj_id_space)) == 0) {
		rc = REP_PROTOCOL_FAIL_NO_RESOURCES;
		goto fail;
	}

	if ((np = rc_node_alloc()) == NULL) {
		rc = REP_PROTOCOL_FAIL_NO_RESOURCES;
		goto fail;
	}

	if ((rc = (*ip->obj_insert_pg_child)(tx_wr, lp, name, pgtype, flags,
	    gen, &decoration_id)) != REP_PROTOCOL_SUCCESS) {
		rc_node_destroy(np);
		goto fail;
	}

	rc = backend_tx_commit(tx_wr);
	if (rc != REP_PROTOCOL_SUCCESS) {
		rc_node_destroy(np);
		return (rc);
	}

	/* Masked flag is 0, because we're inserting. */
	*cpp = rc_node_setup_pg(np, lp, name, pgtype, flags, gen,
	    decoration_id, 0, ci.ci_parent);

	return (REP_PROTOCOL_SUCCESS);

fail:
	if (tx_ro != NULL)
		backend_tx_end_ro(tx_ro);
	if (tx_wr != NULL)
		backend_tx_rollback(tx_wr);
	return (rc);
}

/*
 * Given a row of snaplevel number, snaplevel id, service id, service name,
 * instance id, & instance name, create a rc_snaplevel_t & prepend it onto the
 * rs_levels list of the rc_snapshot_t passed in as data.
 * Returns _CONTINUE on success or _ABORT if any allocations fail.
 */
/*ARGSUSED*/
static int
fill_snapshot_cb(void *data, int columns, char **vals, char **names)
{
	rc_snapshot_t *sp = data;
	rc_snaplevel_t *lvl;
	char *num = vals[0];
	char *id = vals[1];
	char *service_id = vals[2];
	char *service = vals[3];
	char *instance_id = vals[4];
	char *instance = vals[5];
	assert(columns == 6);

	lvl = uu_zalloc(sizeof (*lvl));
	if (lvl == NULL)
		return (BACKEND_CALLBACK_ABORT);
	lvl->rsl_parent = sp;
	lvl->rsl_next = sp->rs_levels;
	sp->rs_levels = lvl;

	string_to_id(num, &lvl->rsl_level_num, "snap_level_num");
	string_to_id(id, &lvl->rsl_level_id, "snap_level_id");
	string_to_id(service_id, &lvl->rsl_service_id, "snap_level_service_id");
	if (instance_id != NULL)
		string_to_id(instance_id, &lvl->rsl_instance_id,
		    "snap_level_instance_id");

	lvl->rsl_scope = (const char *)"localhost";
	lvl->rsl_service = strdup(service);
	if (lvl->rsl_service == NULL) {
		uu_free(lvl);
		return (BACKEND_CALLBACK_ABORT);
	}
	if (instance) {
		assert(lvl->rsl_instance_id != 0);
		lvl->rsl_instance = strdup(instance);
		if (lvl->rsl_instance == NULL) {
			free((void *)lvl->rsl_instance);
			uu_free(lvl);
			return (BACKEND_CALLBACK_ABORT);
		}
	} else {
		assert(lvl->rsl_instance_id == 0);
	}

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Populate sp's rs_levels list from the snaplevel_tbl table.
 * Fails with
 *   _NO_RESOURCES
 */
int
object_fill_snapshot(rc_snapshot_t *sp)
{
	backend_query_t *q;
	rc_snaplevel_t *sl;
	int result;
	int i;

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT snap_level_num, snap_level_id, "
	    "    snap_level_service_id, snap_level_service, "
	    "    snap_level_instance_id, snap_level_instance "
	    "FROM snaplevel_tbl "
	    "WHERE snap_id = %d "
	    "ORDER BY snap_level_id DESC",
	    sp->rs_snap_id);

	result = backend_run(BACKEND_TYPE_NORMAL, q, fill_snapshot_cb, sp);
	if (result == REP_PROTOCOL_DONE)
		result = REP_PROTOCOL_FAIL_NO_RESOURCES;
	backend_query_free(q);

	if (result == REP_PROTOCOL_SUCCESS) {
		i = 0;
		for (sl = sp->rs_levels; sl != NULL; sl = sl->rsl_next) {
			if (sl->rsl_level_num != ++i) {
				backend_panic("snaplevels corrupt; expected "
				    "level %d, got %d", i, sl->rsl_level_num);
			}
		}
	}
	return (result);
}

/*
 * This represents a property group in a snapshot.
 */
typedef struct check_snapshot_elem {
	uint32_t cse_parent;
	uint32_t cse_pg_id;
	uint32_t cse_pg_gen;
	char	cse_seen;
} check_snapshot_elem_t;

#define	CSI_MAX_PARENTS		COMPOSITION_DEPTH
typedef struct check_snapshot_info {
	size_t			csi_count;
	size_t			csi_array_size;
	check_snapshot_elem_t	*csi_array;
	size_t			csi_nparents;
	uint32_t		csi_parent_ids[CSI_MAX_PARENTS];
} check_snapshot_info_t;

/*ARGSUSED*/
static int
check_snapshot_fill_cb(void *data, int columns, char **vals, char **names)
{
	check_snapshot_info_t *csip = data;
	check_snapshot_elem_t *cur;
	const char *parent;
	const char *pg_id;
	const char *pg_gen_id;

	if (columns == 1) {
		uint32_t *target;

		if (csip->csi_nparents >= CSI_MAX_PARENTS)
			backend_panic("snaplevel table has too many elements");

		target = &csip->csi_parent_ids[csip->csi_nparents++];
		string_to_id(vals[0], target, "snap_level_*_id");

		return (BACKEND_CALLBACK_CONTINUE);
	}

	assert(columns == 3);

	parent = vals[0];
	pg_id = vals[1];
	pg_gen_id = vals[2];

	if (csip->csi_count == csip->csi_array_size) {
		size_t newsz = (csip->csi_array_size > 0) ?
		    csip->csi_array_size * 2 : 8;
		check_snapshot_elem_t *new = uu_zalloc(newsz * sizeof (*new));

		if (new == NULL)
			return (BACKEND_CALLBACK_ABORT);

		(void) memcpy(new, csip->csi_array,
		    sizeof (*new) * csip->csi_array_size);
		uu_free(csip->csi_array);
		csip->csi_array = new;
		csip->csi_array_size = newsz;
	}

	cur = &csip->csi_array[csip->csi_count++];

	string_to_id(parent, &cur->cse_parent, "snap_level_*_id");
	string_to_id(pg_id, &cur->cse_pg_id, "snaplvl_pg_id");
	string_to_id(pg_gen_id, &cur->cse_pg_gen, "snaplvl_gen_id");
	cur->cse_seen = 0;

	return (BACKEND_CALLBACK_CONTINUE);
}

static int
check_snapshot_elem_cmp(const void *lhs_arg, const void *rhs_arg)
{
	const check_snapshot_elem_t *lhs = lhs_arg;
	const check_snapshot_elem_t *rhs = rhs_arg;

	if (lhs->cse_parent < rhs->cse_parent)
		return (-1);
	if (lhs->cse_parent > rhs->cse_parent)
		return (1);

	if (lhs->cse_pg_id < rhs->cse_pg_id)
		return (-1);
	if (lhs->cse_pg_id > rhs->cse_pg_id)
		return (1);

	if (lhs->cse_pg_gen < rhs->cse_pg_gen)
		return (-1);
	if (lhs->cse_pg_gen > rhs->cse_pg_gen)
		return (1);

	return (0);
}

/*ARGSUSED*/
static int
check_snapshot_check_cb(void *data, int columns, char **vals, char **names)
{
	check_snapshot_info_t *csip = data;
	check_snapshot_elem_t elem;
	check_snapshot_elem_t *cur;

	const char *parent = vals[0];
	const char *pg_id = vals[1];
	const char *pg_gen_id = vals[2];

	assert(columns == 3);

	string_to_id(parent, &elem.cse_parent, "snap_level_*_id");
	string_to_id(pg_id, &elem.cse_pg_id, "snaplvl_pg_id");
	string_to_id(pg_gen_id, &elem.cse_pg_gen, "snaplvl_gen_id");

	if ((cur = bsearch(&elem, csip->csi_array, csip->csi_count,
	    sizeof (*csip->csi_array), check_snapshot_elem_cmp)) == NULL)
		return (BACKEND_CALLBACK_ABORT);

	if (cur->cse_seen)
		backend_panic("duplicate property group reported");
	cur->cse_seen = 1;
	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Check that a snapshot matches up with the latest in the repository.
 * Returns:
 *	REP_PROTOCOL_SUCCESS		if it is up-to-date,
 *	REP_PROTOCOL_DONE		if it is out-of-date, or
 *	REP_PROTOCOL_FAIL_NO_RESOURCES	if we ran out of memory.
 */
static int
object_check_snapshot(uint32_t snap_id)
{
	check_snapshot_info_t csi;
	backend_query_t *q;
	int result;
	size_t idx;

	/* if the snapshot has never been taken, it must be out of date. */
	if (snap_id == 0)
		return (REP_PROTOCOL_DONE);

	(void) memset(&csi, '\0', sizeof (csi));

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT\n"
	    "    CASE snap_level_instance_id\n"
	    "        WHEN 0 THEN snap_level_service_id\n"
	    "        ELSE snap_level_instance_id\n"
	    "    END\n"
	    "FROM snaplevel_tbl\n"
	    "WHERE snap_id = %d;\n"
	    "\n"
	    "SELECT\n"
	    "    CASE snap_level_instance_id\n"
	    "        WHEN 0 THEN snap_level_service_id\n"
	    "        ELSE snap_level_instance_id\n"
	    "    END,\n"
	    "    snaplvl_pg_id,\n"
	    "    snaplvl_gen_id\n"
	    "FROM snaplevel_tbl, snaplevel_lnk_tbl\n"
	    "WHERE\n"
	    "    (snaplvl_level_id = snap_level_id AND\n"
	    "    snap_id = %d);",
	    snap_id, snap_id);

	result = backend_run(BACKEND_TYPE_NORMAL, q, check_snapshot_fill_cb,
	    &csi);
	if (result == REP_PROTOCOL_DONE)
		result = REP_PROTOCOL_FAIL_NO_RESOURCES;
	backend_query_free(q);

	if (result != REP_PROTOCOL_SUCCESS)
		goto fail;

	if (csi.csi_count > 0) {
		qsort(csi.csi_array, csi.csi_count, sizeof (*csi.csi_array),
		    check_snapshot_elem_cmp);
	}

#if COMPOSITION_DEPTH == 2
	if (csi.csi_nparents != COMPOSITION_DEPTH) {
		result = REP_PROTOCOL_DONE;
		goto fail;
	}

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT "
	    "    pg_parent_id, pg_id, pg_gen_id "
	    "FROM "
	    "    pg_tbl "
	    "WHERE (pg_parent_id = %d OR pg_parent_id = %d)",
	    csi.csi_parent_ids[0], csi.csi_parent_ids[1]);

	result = backend_run(BACKEND_TYPE_NORMAL, q, check_snapshot_check_cb,
	    &csi);
#else
#error This code must be updated
#endif
	/*
	 * To succeed, the callback must not have aborted, and we must have
	 * found all of the items.
	 */
	if (result == REP_PROTOCOL_SUCCESS) {
		for (idx = 0; idx < csi.csi_count; idx++) {
			if (csi.csi_array[idx].cse_seen == 0) {
				result = REP_PROTOCOL_DONE;
				goto fail;
			}
		}
	}

fail:
	uu_free(csi.csi_array);
	return (result);
}

/*ARGSUSED*/
static int
object_copy_string(void *data_arg, int columns, char **vals, char **names)
{
	char **data = data_arg;

	assert(columns == 1);

	if (*data != NULL)
		free(*data);
	*data = NULL;

	if (vals[0] != NULL) {
		if ((*data = strdup(vals[0])) == NULL)
			return (BACKEND_CALLBACK_ABORT);
	}

	return (BACKEND_CALLBACK_CONTINUE);
}

struct snaplevel_add_info {
	backend_query_t *sai_q;
	backend_tx_t 	*sai_tx;
	uint32_t	sai_level_id;
	int		sai_used;		/* sai_q has been used */
};

uint32_t
clone_decoration_set(uint32_t dec_id, struct snaplevel_add_info *data)
{
	backend_query_t *q;
	struct timeval	ts;
	uint32_t new_dec_id;
	uint32_t cnt;
	int r;

	new_dec_id = backend_new_id(data->sai_tx, BACKEND_ID_DECORATION);
	if (new_dec_id == 0)
		return (0);

	q = backend_query_alloc();
	(void) gettimeofday(&ts, NULL);
	backend_query_add(q, "SELECT count() FROM decoration_tbl "
	    "    WHERE decoration_id = %d; "
	    "INSERT INTO decoration_tbl "
	    "    (decoration_id, decoration_entity_type, decoration_value_id, "
	    "    decoration_gen_id, decoration_layer, decoration_bundle_id, "
	    "    decoration_type, decoration_flags, decoration_tv_sec, "
	    "    decoration_tv_usec) "
	    "        SELECT %d, decoration_entity_type, decoration_value_id, "
	    "            decoration_gen_id, decoration_layer, "
	    "            decoration_bundle_id, decoration_type, "
	    "            decoration_flags, %d, %d "
	    "            FROM decoration_tbl WHERE decoration_id = %d; ",
	    dec_id, new_dec_id, ts.tv_sec, ts.tv_usec, dec_id);

	r = backend_tx_run_single_int(data->sai_tx, q, &cnt);
	backend_query_free(q);
	if (r != REP_PROTOCOL_SUCCESS)
		return (0);

	if (cnt > 0) {
		r = backend_tx_run_update(data->sai_tx,
		    "UPDATE id_tbl SET id_next = id_next + %d "
		    "    WHERE (id_name = '%q');",
		    cnt, id_space_to_name(BACKEND_KEY_DECORATION));

		if (r != REP_PROTOCOL_SUCCESS)
			return (0);
	}

	return (new_dec_id);
}

/*ARGSUSED*/
static int
object_snaplevel_process_pg(void *data_arg, int columns, char **vals,
    char **names)
{
	struct snaplevel_add_info *data = data_arg;
	uint32_t snplvl_dec_id;
	uint32_t dec_id;

	assert(columns == 6);

	string_to_id(vals[5], &dec_id, names[5]);
	snplvl_dec_id = clone_decoration_set(dec_id, data);

	backend_query_add(data->sai_q,
	    "INSERT INTO snaplevel_lnk_tbl "
	    "    (snaplvl_level_id, snaplvl_pg_id, snaplvl_pg_name, "
	    "    snaplvl_pg_type, snaplvl_pg_flags, snaplvl_gen_id, "
	    "    snaplvl_dec_id)"
	    "VALUES (%d, %s, '%q', '%q', %s, %s, %d); ",
	    data->sai_level_id, vals[0], vals[1], vals[2], vals[3], vals[4],
	    snplvl_dec_id);

	data->sai_used = 1;

	return (BACKEND_CALLBACK_CONTINUE);
}

/*ARGSUSED*/
static int
object_snapshot_add_level(backend_tx_t *tx, uint32_t snap_id,
    uint32_t snap_level_num, uint32_t svc_id, const char *svc_name,
    uint32_t inst_id, const char *inst_name)
{
	struct snaplevel_add_info data;
	backend_query_t *q;
	int result;

	assert((snap_level_num == 1 && inst_name != NULL) ||
	    snap_level_num == 2 && inst_name == NULL);

	data.sai_level_id = backend_new_id(tx, BACKEND_ID_SNAPLEVEL);
	data.sai_tx = tx;
	if (data.sai_level_id == 0) {
		return (REP_PROTOCOL_FAIL_NO_RESOURCES);
	}

	result = backend_tx_run_update(tx,
	    "INSERT INTO snaplevel_tbl "
	    "    (snap_id, snap_level_num, snap_level_id, "
	    "    snap_level_service_id, snap_level_service, "
	    "    snap_level_instance_id, snap_level_instance) "
	    "VALUES (%d, %d, %d, %d, %Q, %d, %Q);",
	    snap_id, snap_level_num, data.sai_level_id, svc_id, svc_name,
	    inst_id, inst_name);

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT pg_id, pg_name, pg_type, pg_flags, pg_gen_id, pg_dec_id "
	    "    FROM pg_tbl WHERE (pg_parent_id = %d);",
	    (inst_name != NULL)? inst_id : svc_id);

	data.sai_q = backend_query_alloc();
	data.sai_used = 0;
	result = backend_tx_run(tx, q, object_snaplevel_process_pg,
	    &data);
	backend_query_free(q);

	if (result == REP_PROTOCOL_SUCCESS && data.sai_used != 0)
		result = backend_tx_run(tx, data.sai_q, NULL, NULL);
	backend_query_free(data.sai_q);

	return (result);
}

/*
 * Fails with:
 *	_NO_RESOURCES - no new id or out of disk space
 *	_BACKEND_READONLY - persistent backend is read-only
 */
static int
object_snapshot_do_take(uint32_t instid, const char *inst_name,
    uint32_t svcid, const char *svc_name,
    backend_tx_t **tx_out, uint32_t *snapid_out)
{
	backend_tx_t *tx;
	backend_query_t *q;
	int result;

	char *svc_name_alloc = NULL;
	char *inst_name_alloc = NULL;
	uint32_t snapid;

	result = backend_tx_begin(BACKEND_TYPE_NORMAL, &tx);
	if (result != REP_PROTOCOL_SUCCESS)
		return (result);

	snapid = backend_new_id(tx, BACKEND_ID_SNAPSHOT);
	if (snapid == 0) {
		result = REP_PROTOCOL_FAIL_NO_RESOURCES;
		goto fail;
	}

	if (svc_name == NULL) {
		q = backend_query_alloc();
		backend_query_add(q,
		    "SELECT svc_name FROM service_tbl "
		    "WHERE (svc_id = %d)", svcid);
		result = backend_tx_run(tx, q, object_copy_string,
		    &svc_name_alloc);
		backend_query_free(q);

		svc_name = svc_name_alloc;

		if (result == REP_PROTOCOL_DONE) {
			result = REP_PROTOCOL_FAIL_NO_RESOURCES;
			goto fail;
		}
		if (result == REP_PROTOCOL_SUCCESS && svc_name == NULL)
			backend_panic("unable to find name for svc id %d\n",
			    svcid);

		if (result != REP_PROTOCOL_SUCCESS)
			goto fail;
	}

	if (inst_name == NULL) {
		q = backend_query_alloc();
		backend_query_add(q,
		    "SELECT instance_name FROM instance_tbl "
		    "WHERE (instance_id = %d)", instid);
		result = backend_tx_run(tx, q, object_copy_string,
		    &inst_name_alloc);
		backend_query_free(q);

		inst_name = inst_name_alloc;

		if (result == REP_PROTOCOL_DONE) {
			result = REP_PROTOCOL_FAIL_NO_RESOURCES;
			goto fail;
		}

		if (result == REP_PROTOCOL_SUCCESS && inst_name == NULL)
			backend_panic(
			    "unable to find name for instance id %d\n", instid);

		if (result != REP_PROTOCOL_SUCCESS)
			goto fail;
	}

	result = object_snapshot_add_level(tx, snapid, 1,
	    svcid, svc_name, instid, inst_name);

	if (result != REP_PROTOCOL_SUCCESS)
		goto fail;

	result = object_snapshot_add_level(tx, snapid, 2,
	    svcid, svc_name, 0, NULL);

	if (result != REP_PROTOCOL_SUCCESS)
		goto fail;

	*snapid_out = snapid;
	*tx_out = tx;

	free(svc_name_alloc);
	free(inst_name_alloc);

	return (REP_PROTOCOL_SUCCESS);

fail:
	backend_tx_rollback(tx);
	free(svc_name_alloc);
	free(inst_name_alloc);
	return (result);
}

/*
 * Fails with:
 *	_TYPE_MISMATCH - pp is not an instance
 *	_NO_RESOURCES - no new id or out of disk space
 *	_BACKEND_READONLY - persistent backend is read-only
 */
int
object_snapshot_take_new(rc_node_t *pp,
    const char *svc_name, const char *inst_name,
    const char *name, rc_node_t **outp)
{
	rc_node_lookup_t *insti = &pp->rn_id;

	uint32_t instid = insti->rl_main_id;
	uint32_t svcid = insti->rl_ids[ID_SERVICE];
	uint32_t decoration_id;
	uint32_t snapid = 0;
	backend_tx_t *tx = NULL;
	child_info_t ci;
	rc_node_t *np;
	int result;

	if (insti->rl_type != REP_PROTOCOL_ENTITY_INSTANCE)
		return (REP_PROTOCOL_FAIL_TYPE_MISMATCH);

	result = object_snapshot_do_take(instid, inst_name, svcid, svc_name,
	    &tx, &snapid);
	if (result != REP_PROTOCOL_SUCCESS)
		return (result);

	if ((result = object_do_create(tx, &ci, pp,
	    REP_PROTOCOL_ENTITY_SNAPSHOT, name, &np, &decoration_id)) !=
	    REP_PROTOCOL_SUCCESS) {
		backend_tx_rollback(tx);
		return (result);
	}

	/*
	 * link the new object to the new snapshot.
	 */
	np->rn_snapshot_id = snapid;

	result = backend_tx_run_update(tx,
	    "UPDATE snapshot_lnk_tbl SET lnk_snap_id = %d WHERE lnk_id = %d;",
	    snapid, ci.ci_base_nl.rl_main_id);
	if (result != REP_PROTOCOL_SUCCESS) {
		backend_tx_rollback(tx);
		rc_node_destroy(np);
		return (result);
	}
	result = backend_tx_commit(tx);
	if (result != REP_PROTOCOL_SUCCESS) {
		rc_node_destroy(np);
		return (result);
	}

	/* Masked flag is 0, because snapshots do not get masked. */
	*outp = rc_node_setup(np, &ci.ci_base_nl, name, ci.ci_parent,
	    decoration_id, 0);
	return (REP_PROTOCOL_SUCCESS);
}

/*
 * Fails with:
 *	_TYPE_MISMATCH - pp is not an instance
 *	_NO_RESOURCES - no new id or out of disk space
 *	_BACKEND_READONLY - persistent backend is read-only
 */
int
object_snapshot_attach(rc_node_lookup_t *snapi, uint32_t *snapid_ptr,
    int takesnap)
{
	delete_result_t delete_result;
	uint32_t svcid = snapi->rl_ids[ID_SERVICE];
	uint32_t instid = snapi->rl_ids[ID_INSTANCE];
	uint32_t snapid = *snapid_ptr;
	uint32_t oldsnapid = 0;
	backend_tx_t *tx = NULL;
	backend_query_t *q;
	int result;

	delete_info_t dip;
	delete_ent_t de;

	if (snapi->rl_type != REP_PROTOCOL_ENTITY_SNAPSHOT)
		return (REP_PROTOCOL_FAIL_TYPE_MISMATCH);

	if (takesnap) {
		/* first, check that we're actually out of date */
		if (object_check_snapshot(snapid) == REP_PROTOCOL_SUCCESS)
			return (REP_PROTOCOL_SUCCESS);

		result = object_snapshot_do_take(instid, NULL,
		    svcid, NULL, &tx, &snapid);
		if (result != REP_PROTOCOL_SUCCESS)
			return (result);
	} else {
		result = backend_tx_begin(BACKEND_TYPE_NORMAL, &tx);
		if (result != REP_PROTOCOL_SUCCESS)
			return (result);
	}

	q = backend_query_alloc();
	backend_query_add(q,
	    "SELECT lnk_snap_id FROM snapshot_lnk_tbl WHERE lnk_id = %d; "
	    "UPDATE snapshot_lnk_tbl SET lnk_snap_id = %d WHERE lnk_id = %d;",
	    snapi->rl_main_id, snapid, snapi->rl_main_id);
	result = backend_tx_run_single_int(tx, q, &oldsnapid);
	backend_query_free(q);

	if (result == REP_PROTOCOL_FAIL_NOT_FOUND) {
		backend_tx_rollback(tx);
		backend_panic("unable to find snapshot id %d",
		    snapi->rl_main_id);
	}
	if (result != REP_PROTOCOL_SUCCESS)
		goto fail;

	/*
	 * Now we use the delete stack to handle the possible unreferencing
	 * of oldsnapid.
	 */
	(void) memset(&dip, 0, sizeof (dip));
	dip.di_tx = tx;
	dip.di_np_tx = NULL;	/* no need for nonpersistent backend */

	if ((result = delete_stack_push(&dip, BACKEND_TYPE_NORMAL,
	    &snaplevel_tbl_delete, oldsnapid, 0, 0)) != REP_PROTOCOL_SUCCESS)
		goto fail;

	while (delete_stack_pop(&dip, &de)) {
		result = (*de.de_cb)(&dip, &de, &delete_result);
		if (result != REP_PROTOCOL_SUCCESS)
			goto fail;
	}

	result = backend_tx_commit(tx);
	if (result != REP_PROTOCOL_SUCCESS)
		goto fail;

	delete_stack_cleanup(&dip);
	*snapid_ptr = snapid;
	return (REP_PROTOCOL_SUCCESS);

fail:
	backend_tx_rollback(tx);
	delete_stack_cleanup(&dip);
	return (result);
}

typedef struct bundle_remove_info {
	backend_query_t *bri_q;
	backend_tx_t	*bri_tx;
	uint32_t	*ignored_decids;
	int		ignored_decids_cnt;
	delete_info_t	bri_dip;
	uint32_t	bri_id;
	uint32_t	bri_gen_id;
	int		nolstchk;
	int		clrconf;
	pg_update_bundle_info_t *bri_cur_pup;
	pg_update_bundle_info_t *bri_pup_list;
	rep_protocol_responseid_t result;
} bundle_remove_info_t;

/*
 * First check the decoration_flags to see this property/decoration
 * row is only present due to bundle only.  If so remove the property
 * row and decoration.
 *
 * vals[0] = lnk_prop_id
 * vals[1] = lnk_gen_id
 * vals[2] = decoration_id
 * vals[3] = decoration_flags
 */
/* ARGSUSED */
static int
prop_bundle_remove(void *data, int columns, char **vals, char **names)
{
	bundle_remove_info_t	*brip = (bundle_remove_info_t *)data;
	backend_query_t		 *q;

	uint32_t	prop_id;
	uint32_t	gen_id;
	uint32_t	dec_id;
	uint32_t	d_flags;

	uint32_t	dcnt;

	string_to_id(vals[0], &prop_id, names[0]);
	string_to_id(vals[1], &gen_id, names[1]);
	string_to_id(vals[2], &dec_id, names[2]);
	string_to_id(vals[3], &d_flags, names[3]);

	/*
	 * If the list is populated check to see dec_id is on
	 * the list, if it is then this property should still
	 * be supported by the bundle.
	 *
	 * Return immediately so that the property is not added
	 * to the delete list so that when the pg generation is
	 * incremented this property will be copied to the new
	 * list.
	 */
	if (brip->ignored_decids) {
		int i;

		for (i = 0; i < brip->ignored_decids_cnt; i++) {
			if (brip->ignored_decids[i] == dec_id)
				break;
		}

		if (i == brip->ignored_decids_cnt)
			return (BACKEND_CALLBACK_CONTINUE);
	}

	/*
	 * Check to see if this property is only supported
	 * by this bundle, if so then just remove the
	 * property.
	 */
	q = backend_query_alloc();
	backend_query_add(q, "SELECT count() FROM decoration_tbl "
	    "WHERE (decoration_id = %d AND decoration_bundle_id != %d AND "
	    "    ((decoration_flags & %d) = 0)) ",
	    dec_id, brip->bri_id, DECORATION_MASK);

	brip->result = backend_tx_run_single_int(brip->bri_tx, q, &dcnt);
	if (brip->result != REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return (BACKEND_CALLBACK_ABORT);
	}

	/*
	 * If the count is 0, there is no bundle support for this property.
	 *
	 * If this is the latest generation row, then delete the property
	 * row and move on.  If not then process the row to keep those
	 * that are referenced by snapshots.
	 *
	 * This will remove the row from the running snapshot as well.
	 */
	if (dcnt == 0 && gen_id == brip->bri_gen_id) {
		int	idx = brip->bri_cur_pup->pub_dprop_idx;

		brip->bri_cur_pup->pub_dprop_ids[idx] = prop_id;
		brip->bri_cur_pup->pub_dprop_idx++;

		backend_query_free(q);
		return (BACKEND_CALLBACK_CONTINUE);
	}

	/*
	 * Note: this cannot be the latest generation and
	 * be kept due to bundle reference only.
	 */
	if (d_flags & DECORATION_BUNDLE_ONLY)
		goto delprop_row;

	/*
	 * There are other bundles that support this property,
	 * check to see if this is the latest generation.  If
	 * so then the previous generation needs to be brought
	 * forward.
	 */
	if (gen_id == brip->bri_gen_id) {
		int idx = brip->bri_cur_pup->pub_rback_idx;

		brip->bri_cur_pup->pub_rback_ids[idx] = prop_id;
		brip->bri_cur_pup->pub_rback_idx++;

		backend_query_free(q);
		return (BACKEND_CALLBACK_CONTINUE);
	}

	/*
	 * Check if there are conflicts in the property. If so, we save its
	 * decoration_id for later processing.
	 */
	backend_query_reset(q);
	backend_query_add(q, "SELECT count() FROM decoration_tbl "
	    "WHERE (decoration_id = %d AND decoration_bundle_id != %d AND "
	    "    ((decoration_flags & %d) != 0)) ",
	    dec_id, brip->bri_id, DECORATION_CONFLICT);
	brip->result = backend_tx_run_single_int(brip->bri_tx, q, &dcnt);
	if (brip->result != REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return (BACKEND_CALLBACK_ABORT);
	}

	/*
	 * We are removing bundle support for a property in conflict,
	 * make sure we go through obejc_prop_check_conflict()
	 */
	if (dcnt != 0) {
		int	idx = brip->bri_cur_pup->pub_dprop_idx;

		brip->bri_cur_pup->pub_dprop_ids[idx] = prop_id;
		brip->bri_cur_pup->pub_dprop_idx++;

		backend_query_free(q);
		return (BACKEND_CALLBACK_CONTINUE);
	}

	backend_query_reset(q);
	backend_query_add(q, "SELECT count() FROM snaplevel_lnk_tbl "
	    "WHERE snaplvl_gen_id = %d AND "
	    "    snaplvl_pg_id = (SELECT lnk_pg_id FROM prop_lnk_tbl "
	    "        WHERE lnk_prop_id = %d); ",
	    gen_id, prop_id);

	brip->result = backend_tx_run_single_int(brip->bri_tx, q, &dcnt);
	if (brip->result != REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return (BACKEND_CALLBACK_ABORT);
	}

	backend_query_free(q);
	if (dcnt != 0) {
		brip->result = delete_stack_push(&brip->bri_dip,
		    BACKEND_TYPE_NORMAL, prop_mark_bundle_remove_sno, 0, gen_id,
		    dec_id);

		if (brip->result != REP_PROTOCOL_SUCCESS)
			return (BACKEND_CALLBACK_ABORT);

	} else {
		goto delprop_row;
	}

	return (BACKEND_CALLBACK_CONTINUE);

delprop_row:
	brip->result = delete_stack_push(&brip->bri_dip, BACKEND_TYPE_NORMAL,
	    prop_lnk_tbl_delete, prop_id, gen_id, dec_id);

	if (brip->result != REP_PROTOCOL_SUCCESS)
		return (BACKEND_CALLBACK_ABORT);

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * The pg is contributed to by the bundle that is
 * being removed.
 *
 * First check the number of decorations associated
 * with pg that are not part of the bundle being
 * removed.  If there are none, then the entire pg is
 * being removed and should remove all the underlying
 * bits.  So let's just drop the whole thing.
 *
 * Other wise remove the decoration associated with the
 * bundle and process the pg's properties.
 */
/* ARGSUSED */
static int
pg_bundle_remove(void *data, int columns, char **vals, char **names)
{
	bundle_remove_info_t	*brip = (bundle_remove_info_t *)data;
	backend_query_t		 *q;
	uint32_t		dcnt;
	uint32_t		dec_id;
	uint32_t		pg_gen;
	uint32_t		pg_id;
	uint32_t		pg_parent_id;

	q = backend_query_alloc();

	string_to_id(vals[0], &pg_id, names[0]);
	string_to_id(vals[1], &pg_gen, names[1]);
	string_to_id(vals[2], &pg_parent_id, names[2]);
	string_to_id(vals[3], &dec_id, names[3]);

	/*
	 * Make sure and not count masked rows.  If the pg is
	 * masked and there are no other supporting manifests
	 * then we are going to just drop support for it
	 * completely.
	 */
	backend_query_add(q, "SELECT count() FROM decoration_tbl "
	    "WHERE (decoration_id = %d AND decoration_bundle_id != %d AND "
	    "    ((decoration_flags & %d) = 0)) ",
	    dec_id, brip->bri_id, DECORATION_MASK);

	brip->result = backend_tx_run_single_int(brip->bri_tx, q, &dcnt);
	if (brip->result != REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return (BACKEND_CALLBACK_ABORT);
	}

	if (dcnt == 0) {
		backend_query_free(q);
		brip->result = delete_stack_push(&brip->bri_dip,
		    BACKEND_TYPE_NORMAL, propertygrp_delete, pg_id, pg_gen,
		    dec_id);

		if (brip->result != REP_PROTOCOL_SUCCESS)
			return (BACKEND_CALLBACK_ABORT);

		return (BACKEND_CALLBACK_CONTINUE);
	} else {
		uint32_t l;
		int r, clr;

		/*
		 * In this case we need to inspect for conflict still
		 * existing, and clear if the bundle being removed
		 * dropped the conflict.
		 */
		backend_query_reset(q);
		backend_query_add(q, "SELECT decoration_layer "
		    "FROM decoration_tbl WHERE "
		    "decoration_id = %d AND (decoration_flags & %d) != 0 AND "
		    "    decoration_bundle_id = %d LIMIT 1; ",
		    dec_id, DECORATION_CONFLICT, brip->bri_id);

		clr = 0;
		r = backend_tx_run_single_int(brip->bri_tx, q, &l);
		if (r == REP_PROTOCOL_SUCCESS) {
			if (object_pg_check_conflict(brip->bri_tx, pg_id,
			    dec_id, brip->bri_id, l) == 0)
				clr = 1;
		}

		brip->result = delete_stack_push(&brip->bri_dip,
		    BACKEND_TYPE_NORMAL, decoration_bundle_delete, brip->bri_id,
		    0, dec_id);

		if (brip->result != REP_PROTOCOL_SUCCESS) {
			backend_query_free(q);
			return (BACKEND_CALLBACK_ABORT);
		}

		if (clr == 1) {
			brip->result = delete_stack_push(&brip->bri_dip,
			    BACKEND_TYPE_NORMAL, brm_pg_clear_conflict, pg_id,
			    0, l);

			if (brip->result != REP_PROTOCOL_SUCCESS) {
				backend_query_free(q);
				return (BACKEND_CALLBACK_ABORT);
			}
		}
	}

	backend_query_reset(q);

	/*
	 * Now process each of the properties for the pg.  We have
	 * to process the properties in older generations in case
	 * they are being kept around due to snapshots only.
	 *
	 * We can process this by passing the unique set of decoration_id's
	 * for this set of prop_lnk_tbl entries with this pg_id.
	 */
	backend_query_add(q, "SELECT count() FROM prop_lnk_tbl "
	    "WHERE lnk_pg_id = %d AND lnk_gen_id = %d ",
	    pg_id, pg_gen);

	brip->result = backend_tx_run_single_int(brip->bri_tx, q, &dcnt);
	if (brip->result != REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return (BACKEND_CALLBACK_ABORT);
	}

	brip->bri_gen_id = pg_gen;
	brip->bri_cur_pup = uu_zalloc(sizeof (pg_update_bundle_info_t));
	brip->bri_cur_pup->pub_dprop_idx = 0;
	brip->bri_cur_pup->pub_rback_idx = 0;
	brip->bri_cur_pup->pub_dprop_ids = uu_zalloc(dcnt * sizeof (uint32_t));
	brip->bri_cur_pup->pub_rback_ids = uu_zalloc(dcnt * sizeof (uint32_t));

	backend_query_reset(q);
	backend_query_add(q,
	    "SELECT lnk_prop_id, lnk_gen_id, decoration_id, decoration_flags "
	    "FROM prop_lnk_tbl INNER JOIN decoration_tbl ON "
	    "    (lnk_decoration_id = decoration_id AND "
	    "        lnk_gen_id = decoration_gen_id) "
	    "    WHERE lnk_pg_id = %d AND decoration_bundle_id = %d; ",
	    pg_id, brip->bri_id);

	brip->result = backend_tx_run(brip->bri_tx, q,
	    prop_bundle_remove, brip);
	if (brip->result != REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return (BACKEND_CALLBACK_ABORT);
	}

	if (brip->bri_cur_pup->pub_dprop_idx ||
	    brip->bri_cur_pup->pub_rback_idx) {
		pg_update_bundle_info_t *pup;
		uint32_t	pid = 0;

		brip->bri_cur_pup->pub_pg_id = pg_id;
		brip->bri_cur_pup->pub_gen_id = pg_gen;

		backend_query_reset(q);
		backend_query_add(q, "SELECT instance_svc FROM instance_tbl "
		    "WHERE instance_id = %d ", pg_parent_id);

		brip->result = backend_tx_run_single_int(brip->bri_tx, q, &pid);

		if (brip->result == REP_PROTOCOL_FAIL_NOT_FOUND) {
			brip->bri_cur_pup->pub_pg_inst = 0;
			brip->bri_cur_pup->pub_pg_svc = pg_parent_id;
		} else if (brip->result == REP_PROTOCOL_SUCCESS) {
			brip->bri_cur_pup->pub_pg_inst = pg_parent_id;
			brip->bri_cur_pup->pub_pg_svc = pid;
		} else {
			backend_query_free(q);
			return (BACKEND_CALLBACK_ABORT);
		}

		if ((pup = brip->bri_pup_list) == NULL) {
			brip->bri_pup_list = brip->bri_cur_pup;
		} else {
			while (pup->pub_next != NULL)
				pup = pup->pub_next;

			pup->pub_next = brip->bri_cur_pup;
		}
	} else {
		uu_free(brip->bri_cur_pup->pub_dprop_ids);
		brip->bri_cur_pup->pub_dprop_ids = NULL;
		uu_free(brip->bri_cur_pup->pub_rback_ids);
		brip->bri_cur_pup->pub_rback_ids = NULL;

		uu_free(brip->bri_cur_pup);
	}

	brip->bri_cur_pup = NULL;
	backend_query_free(q);
	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * This is a case where the properties supported
 * by the bundle given in the property group need
 * to lose their support.
 *
 * If the trans is not NULL there is a transaction
 * that lists the properties that should lose the
 * bundle support, while other properties that may
 * be listed with this bundle are still supported
 * and should be copied into the pg.  This is the
 * case where a bundle is not removed, but properties
 * has been removed from the bundle.
 *
 * If rm_pg is set the remove the bundle support from
 * the property group as well.
 *
 */
int
object_prop_bundle_remove(rc_node_t *np, const char *bname,
    const void *cmds_arg, size_t cmds_sz, int rm_pg_bundle)
{
	backend_query_t		*q;
	bundle_remove_info_t	brip = {0};
	backend_tx_t		*tx;
	delete_result_t		delete_result;
	delete_ent_t		de;

	uint32_t		dcnt;
	int			rc;

	if (np->rn_id.rl_type != REP_PROTOCOL_ENTITY_PROPERTYGRP)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);

	rc = backend_tx_begin(BACKEND_TYPE_NORMAL, &tx);
	if (rc != REP_PROTOCOL_SUCCESS)
		return (rc);

	brip.bri_tx = tx;
	brip.bri_dip.di_tx = tx;
	brip.bri_dip.di_action = REP_PROTOCOL_ENTITY_REMOVE;
	brip.bri_pup_list = NULL;
	brip.result = REP_PROTOCOL_SUCCESS;

	q = backend_query_alloc();
	brip.bri_q = backend_query_alloc();

	backend_query_add(q,
	    "SELECT bundle_id FROM bundle_tbl WHERE bundle_name = '%q'; ",
	    bname);

	if ((rc = backend_tx_run_single_int(brip.bri_tx, q,
	    &brip.bri_id)) != REP_PROTOCOL_SUCCESS)
		goto fail;

	if (rm_pg_bundle) {
		rc = backend_tx_run_update(brip.bri_tx,
		    "DELETE FROM decoration_tbl "
		    "WHERE decoration_bundle_id = %d AND "
		    "    decoration_id = (SELECT pg_dec_id FROM pg_tbl "
		    "        WHERE pg_id = %d LIMIT 1); ",
		    brip.bri_id, np->rn_id.rl_main_id);

		if (rc != REP_PROTOCOL_SUCCESS)
			goto fail;
	}

	backend_query_reset(q);
	backend_query_add(q, "SELECT count() FROM prop_lnk_tbl "
	    "WHERE lnk_pg_id = %d AND lnk_gen_id = %d ",
	    np->rn_id.rl_main_id, np->rn_gen_id);

	rc = backend_tx_run_single_int(brip.bri_tx, q, &dcnt);
	if (rc != REP_PROTOCOL_SUCCESS)
		goto fail;

	/*
	 * If the cmds exists, then get a list of dec_ids for the properties
	 * in the list to be processed.
	 */
	if (cmds_arg) {
		const struct rep_protocol_transaction_cmd *cmds;
		uintptr_t loc;
		uint32_t sz;
		rc_node_t *prop = NULL;
		int cnt = 0;

		brip.ignored_decids = uu_zalloc((dcnt + 1) * sizeof (uint32_t));

		loc = (uintptr_t)cmds_arg;

		(void) pthread_mutex_lock(&np->rn_lock);
		while (cmds_sz > 0) {
			cmds = (struct rep_protocol_transaction_cmd *)loc;

			if (cmds_sz <= REP_PROTOCOL_TRANSACTION_CMD_MIN_SIZE)
				goto fail;

			sz = cmds->rptc_size;
			if (sz > cmds_sz)
				goto fail;

			if (rc_node_find_named_child(np,
			    (const char *)cmds[0].rptc_data,
			    REP_PROTOCOL_ENTITY_PROPERTY, &prop,
			    rc_is_naive_client()) !=
			    REP_PROTOCOL_SUCCESS || prop == NULL)
				goto fail;

			rc_node_rele(prop);

			brip.ignored_decids[cnt] = prop->rn_decoration_id;

			prop = NULL;
			cnt++;
			loc += sz;
			cmds_sz -= sz;
		}

		(void) pthread_mutex_unlock(&np->rn_lock);
		brip.ignored_decids_cnt = cnt;
	}

	brip.bri_gen_id = np->rn_gen_id;
	brip.bri_cur_pup = uu_zalloc(sizeof (pg_update_bundle_info_t));
	brip.bri_cur_pup->pub_dprop_idx = 0;
	brip.bri_cur_pup->pub_rback_idx = 0;
	brip.bri_cur_pup->pub_dprop_ids = uu_zalloc(dcnt * sizeof (uint32_t));
	brip.bri_cur_pup->pub_rback_ids = uu_zalloc(dcnt * sizeof (uint32_t));

	backend_query_reset(q);
	backend_query_add(q,
	    "SELECT lnk_prop_id, lnk_gen_id, decoration_id, decoration_flags "
	    "FROM prop_lnk_tbl INNER JOIN decoration_tbl ON "
	    "    (lnk_decoration_id = decoration_id AND "
	    "        lnk_gen_id = decoration_gen_id) "
	    "    WHERE lnk_pg_id = %d AND decoration_bundle_id = %d; ",
	    np->rn_id.rl_main_id, brip.bri_id);

	rc = backend_tx_run(brip.bri_tx, q, prop_bundle_remove, &brip);
	if (rc != REP_PROTOCOL_SUCCESS)
		goto fail;

	while (delete_stack_pop(&brip.bri_dip, &de)) {
		rc = (*de.de_cb)(&brip.bri_dip, &de, &delete_result);
		if (rc != REP_PROTOCOL_SUCCESS)
			goto fail;
	}

	rc = backend_tx_commit(tx);
	if (rc != REP_PROTOCOL_SUCCESS)
		goto fail;

	if (brip.bri_cur_pup->pub_dprop_idx ||
	    brip.bri_cur_pup->pub_rback_idx) {
		uint32_t	pg_parent_id;
		uint32_t	pid = 0;

		brip.bri_cur_pup->pub_pg_id = np->rn_id.rl_main_id;
		brip.bri_cur_pup->pub_gen_id = np->rn_gen_id;

		rc = backend_tx_begin_ro(BACKEND_TYPE_NORMAL, &tx);
		if (rc != REP_PROTOCOL_SUCCESS)
			goto fail;

		backend_query_reset(q);
		backend_query_add(q, "SELECT pg_parent_id FROM pg_tbl "
		    "WHERE pg_id = %d ", np->rn_id.rl_main_id);

		rc = backend_tx_run_single_int(tx, q, &pg_parent_id);
		if (rc != REP_PROTOCOL_SUCCESS)
			goto fail;

		backend_query_reset(q);
		backend_query_add(q, "SELECT instance_svc FROM instance_tbl "
		    "WHERE instance_id = %d ", pg_parent_id);

		rc = backend_tx_run_single_int(tx, q, &pid);

		backend_tx_end_ro(tx);
		if (rc == REP_PROTOCOL_FAIL_NOT_FOUND) {
			brip.bri_cur_pup->pub_pg_inst = 0;
			brip.bri_cur_pup->pub_pg_svc = pg_parent_id;
		} else if (rc == REP_PROTOCOL_SUCCESS) {
			brip.bri_cur_pup->pub_pg_inst = pg_parent_id;
			brip.bri_cur_pup->pub_pg_svc = pid;
		} else {
			goto fail;
		}

		brip.bri_cur_pup->pub_setmask = 0;
		rc = rc_tx_prop_bundle_remove(brip.bri_cur_pup, np);

		if (rc != REP_PROTOCOL_SUCCESS)
			goto fail;
	}

	rc = REP_PROTOCOL_SUCCESS;
fail:
	if (rc == REP_PROTOCOL_DONE)
		rc = brip.result;

	if (brip.bri_cur_pup) {
		uu_free(brip.bri_cur_pup->pub_dprop_ids);
		uu_free(brip.bri_cur_pup->pub_rback_ids);

		uu_free(brip.bri_cur_pup);
	}

	backend_query_free(q);
	backend_query_free(brip.bri_q);

	if (rc != REP_PROTOCOL_SUCCESS)
		backend_tx_rollback(tx);

	return (rc);
}


/*
 * For the service and each of the service's instances remove the bundle
 * decorations with a simple repo update.  Then pg_bundle_remove is a
 * callback for each pg that references the bundle being removed.  In
 * pg_bundle_remove, a callback to prop_bundle_remove is used
 * for each property in the pg that references the bundle.  For each
 * property found, drop the rows that are kept strictly due to bundle
 * reference or mark those that are snapshot referenced as no bundle
 * reference.
 * For any properties that are part of the latest generation or need
 * to be deleted completely store off for later processing.  After all
 * pgs are processed, process special properties collected above, via
 * rc_tx_prop_bundle_remove, which comes back into object_pg_bundle_finish
 * which bumps the pg generation and process the properties accordingly.
 *
 */
int
object_bundle_remove(rc_node_t *np, const char *bname)
{
	bundle_remove_info_t	brip = {0};
	delete_result_t		delete_result;
	delete_ent_t		de;
	backend_tx_t		*tx;

	int			rc;

	if (np->rn_id.rl_type != REP_PROTOCOL_ENTITY_SERVICE &&
	    np->rn_id.rl_type != REP_PROTOCOL_ENTITY_INSTANCE) {
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);
	}

	rc = backend_tx_begin(BACKEND_TYPE_NORMAL, &tx);
	if (rc != REP_PROTOCOL_SUCCESS)
		return (rc);

	brip.bri_tx = tx;
	brip.bri_dip.di_tx = tx;
	brip.bri_dip.di_action = REP_PROTOCOL_ENTITY_REMOVE;
	brip.bri_pup_list = NULL;
	brip.result = REP_PROTOCOL_SUCCESS;

	brip.bri_q = backend_query_alloc();

	backend_query_add(brip.bri_q,
	    "SELECT bundle_id FROM bundle_tbl WHERE bundle_name = '%q'; ",
	    bname);

	if ((rc = backend_tx_run_single_int(brip.bri_tx, brip.bri_q,
	    &brip.bri_id)) != REP_PROTOCOL_SUCCESS)
		goto fail;

	backend_query_reset(brip.bri_q);

	/*
	 * Now let's remove the decoration that is associated with
	 * this bundle for the service.
	 */
	if (np->rn_id.rl_type == REP_PROTOCOL_ENTITY_SERVICE) {
		if ((rc = backend_tx_run_update(brip.bri_tx,
		    "DELETE FROM decoration_tbl "
		    "WHERE decoration_key = (SELECT decoration_key FROM"
		    " decoration_tbl "
		    "    WHERE decoration_bundle_id = %d AND "
		    "       decoration_id = (SELECT svc_dec_id FROM service_tbl"
		    "       WHERE svc_id = %d LIMIT 1)); ",
		    brip.bri_id, np->rn_id.rl_main_id)) != REP_PROTOCOL_SUCCESS)
			goto fail;
	}

	/*
	 * Now let's remove the decoration that is associated with
	 * this bundle for the instance.
	 */
	if (np->rn_id.rl_type == REP_PROTOCOL_ENTITY_SERVICE) {
		if ((rc = backend_tx_run_update(brip.bri_tx,
		    "DELETE FROM decoration_tbl "
		    "WHERE decoration_key IN (SELECT decoration_key "
		    "    FROM decoration_tbl "
		    "    INNER JOIN instance_tbl on "
		    "        decoration_id = instance_dec_id "
		    "    WHERE (instance_svc = %d and "
		    "        decoration_bundle_id = %d)); ",
		    np->rn_id.rl_main_id, brip.bri_id)) != REP_PROTOCOL_SUCCESS)
			goto fail;
	} else {
		if ((rc = backend_tx_run_update(brip.bri_tx,
		    "DELETE FROM decoration_tbl "
		    "WHERE decoration_key IN (SELECT decoration_key "
		    "    FROM decoration_tbl "
		    "    INNER JOIN instance_tbl on "
		    "        decoration_id = instance_dec_id "
		    "    WHERE (instance_id = %d and "
		    "        decoration_bundle_id = %d)); ",
		    np->rn_id.rl_main_id, brip.bri_id)) != REP_PROTOCOL_SUCCESS)
			goto fail;
	}

	/*
	 * Now process the pgs that are associated with this bundle and are
	 * children of current service and its instances.
	 */
	if (np->rn_id.rl_type == REP_PROTOCOL_ENTITY_SERVICE) {
		backend_query_add(brip.bri_q,
		    "SELECT pg_id, pg_gen_id, pg_parent_id, decoration_id "
		    "    FROM pg_tbl INNER JOIN decoration_tbl "
		    "    ON pg_dec_id = decoration_id "
		    "    WHERE (decoration_bundle_id = %d AND "
		    "    (pg_parent_id = %d OR pg_parent_id IN "
		    "    (SELECT instance_id FROM instance_tbl "
		    "    WHERE instance_svc = %d))); ",
		    brip.bri_id, np->rn_id.rl_main_id, np->rn_id.rl_main_id);
	} else {
		backend_query_add(brip.bri_q,
		    "SELECT pg_id, pg_gen_id, pg_parent_id, decoration_id "
		    "    FROM pg_tbl INNER JOIN decoration_tbl "
		    "        ON pg_dec_id = decoration_id "
		    "    WHERE (decoration_bundle_id = %d AND "
		    "    pg_parent_id = %d);",
		    brip.bri_id, np->rn_id.rl_main_id);
	}

	if ((rc = backend_tx_run(brip.bri_tx, brip.bri_q,
	    pg_bundle_remove, &brip)) != REP_PROTOCOL_SUCCESS)
		goto fail;

	while (delete_stack_pop(&brip.bri_dip, &de)) {
		rc = (*de.de_cb)(&brip.bri_dip, &de, &delete_result);
		if (rc != REP_PROTOCOL_SUCCESS)
			goto fail;
	}

	if ((rc = backend_tx_run_update(brip.bri_tx, "DELETE FROM bundle_tbl "
	    "WHERE bundle_id = %d AND "
	    "    bundle_id NOT IN (SELECT decoration_bundle_id "
	    "        FROM decoration_tbl) ",
	    brip.bri_id)) != REP_PROTOCOL_SUCCESS)
		goto fail;

	/*
	 * Commit the transactions on the higher entities removing the bundles
	 * from those entities.
	 *
	 * Also, property and decoration rows, that are simply kept around due
	 * to bundle only references that are removed.  Finally, those that
	 * are referenced by snapshots (which have to stay are marked as
	 * snapshot only so they can roll off the snapshot lists when the rows
	 * should because there is no bundle reference).
	 *
	 * Then process each of the properties that are special cases, either
	 * delete the property or rollback to a previous bundle support for
	 * each of the pgs in a transaction for each pg.
	 *
	 * In short the previous are simple row updates or deletes.  The last
	 * set of transactions are pg generation transactions.
	 */
	rc = backend_tx_commit(tx);
	if (rc != REP_PROTOCOL_SUCCESS)
		goto fail;

	if (brip.bri_pup_list != NULL) {
		pg_update_bundle_info_t *pup = NULL;
		pg_update_bundle_info_t *ppup = NULL;


		for (pup = brip.bri_pup_list; pup != NULL;
		    pup = pup->pub_next) {
			uu_free(ppup);
			ppup = NULL;

			rc = backend_tx_begin_ro(BACKEND_TYPE_NORMAL, &tx);
			if (rc != REP_PROTOCOL_SUCCESS) {
				backend_query_free(brip.bri_q);
				return (rc);
			}

			/*
			 * Check to see if the pg is still present.
			 *
			 * This could be a pg that was completely removed.
			 */
			backend_query_reset(brip.bri_q);
			backend_query_add(brip.bri_q, "SELECT 1 FROM pg_tbl "
			    "WHERE pg_id = %d ", pup->pub_pg_id);

			rc = backend_tx_run(tx, brip.bri_q,
			    backend_fail_if_seen, NULL);

			backend_tx_end_ro(tx);
			if (rc == REP_PROTOCOL_DONE) {
				pup->pub_setmask = 0;
				pup->pub_bundleid = brip.bri_id;
				rc = rc_tx_prop_bundle_remove(pup, NULL);
				if (rc != REP_PROTOCOL_SUCCESS) {
					break;
				}
			}

			uu_free(pup->pub_dprop_ids);
			pup->pub_dprop_ids = NULL;
			uu_free(pup->pub_rback_ids);
			pup->pub_rback_ids = NULL;

			ppup = pup;
		}

		if (pup != NULL && rc != REP_PROTOCOL_SUCCESS) {
			for (; pup != NULL; pup = pup->pub_next) {
				uu_free(ppup);

				uu_free(pup->pub_dprop_ids);
				pup->pub_dprop_ids = NULL;
				uu_free(pup->pub_rback_ids);
				pup->pub_rback_ids = NULL;

				ppup = pup;
			}

			uu_free(ppup);

			if (rc == REP_PROTOCOL_DONE)
				rc = brip.result;
			backend_query_free(brip.bri_q);
			return (rc);
		}

		uu_free(ppup);
	}

	backend_query_free(brip.bri_q);
	return (REP_PROTOCOL_SUCCESS);
fail:
	if (rc == REP_PROTOCOL_DONE)
		rc = brip.result;

	backend_query_free(brip.bri_q);
	backend_tx_rollback(tx);
	return (rc);
}

static int
chk_pub_list(uint32_t p, uint32_t *lst, uint32_t idx)
{
	int i;

	if (idx == 0)
		return (0);

	for (i = 0; i < idx; i++) {
		if (lst[i] == p)
			return (1);
	}

	return (0);
}

/*
 * Columns process in order :
 *	0 lnk_prop_name
 *	1 lnk_val_decoration_key
 *	2 decoration_id
 *	3 decoration_entity_type
 *	4 decoration_key
 *	5 decoration_value_id
 *	6 decoration_bundle_id
 *	7 decoration_type
 *	8 decoration_flags
 *      9 decoration_layer
 */
/* ARGSUSED */
static int
rollback_and_mask(void *data_arg, int columns, char **vals, char **names)
{
	bundle_remove_info_t	*brip = data_arg;
	pg_update_bundle_info_t	*pup = brip->bri_cur_pup;
	tx_reset_mask_data_t	rmd;
	backend_query_t		*q;

	struct timeval	ts;
	uint32_t new_dec_key;
	uint32_t pdec_key;

	const char *prop_name = vals[0];
	const char *prop_type = vals[3];

	uint32_t dec_id;
	uint32_t lnk_val_id;
	uint32_t dec_bundle_id;
	uint32_t dec_type;
	uint32_t dec_flags;
	uint32_t dec_layer;

	const char *tv_key = NULL;
	const char *dec_key = NULL;

	tv_key = vals[1];
	dec_key = vals[4];

	string_to_id(vals[2], &dec_id, names[2]);
	string_to_id(vals[5], &lnk_val_id, names[5]);
	string_to_id(vals[6], &dec_bundle_id, names[6]);
	string_to_id(vals[7], &dec_type, names[7]);
	string_to_id(vals[8], &dec_flags, names[8]);
	string_to_id(vals[9], &dec_layer, names[9]);

	new_dec_key = backend_new_id(brip->bri_tx, BACKEND_KEY_DECORATION);
	if (strcmp(tv_key, dec_key) != 0)
		string_to_id(tv_key, &pdec_key, names[3]);
	else
		pdec_key = new_dec_key;

	backend_query_add(brip->bri_q,
	    "INSERT INTO prop_lnk_tbl"
	    "    (lnk_pg_id, lnk_gen_id, lnk_prop_name, lnk_prop_type,"
	    "    lnk_val_id, lnk_decoration_id,"
	    "    lnk_val_decoration_key) "
	    "VALUES ( %d, %d, '%q', '%q', %d, %d, %d ); ",
	    pup->pub_pg_id, brip->bri_gen_id, prop_name, prop_type,
	    lnk_val_id, dec_id, pdec_key);

	if (pup->pub_setmask && !pup->pub_delcust) {
		dec_flags |= DECORATION_MASK;
		dec_layer = REP_PROTOCOL_DEC_ADMIN;
		dec_bundle_id = 0;
	}

	if (pup->pub_delcust)
		dec_flags |= DECORATION_DELCUSTED;
	else
		backend_carry_delcust_flag(brip->bri_tx, dec_id, dec_layer,
		    &dec_flags);

	(void) gettimeofday(&ts, NULL);
	backend_query_add(brip->bri_q,
	    "INSERT INTO decoration_tbl"
	    "	(decoration_key, decoration_id, "
	    "    decoration_entity_type, decoration_value_id, "
	    "    decoration_gen_id, decoration_layer, "
	    "    decoration_bundle_id, decoration_type, "
	    "    decoration_flags, decoration_tv_sec, "
	    "    decoration_tv_usec) "
	    "VALUES ( %d, %d, '%q', %d, %d, %d, %d, %d, %d, "
	    "    %ld, %ld ); ",
	    new_dec_key, dec_id, prop_type, lnk_val_id,
	    brip->bri_gen_id, dec_layer,
	    dec_bundle_id, dec_type,
	    dec_flags, ts.tv_sec, ts.tv_usec);

	q = backend_query_alloc();

	backend_query_add(q,
	    "SELECT decoration_key, decoration_id, decoration_gen_id, %d "
	    "FROM decoration_tbl "
	    "WHERE decoration_id = %d AND "
	    "    decoration_layer = %d AND "
	    "    ((decoration_flags & %d) | %d) != 0 AND "
	    "    (decoration_flags & %d) = 0; ",
	    pup->pub_delcust, dec_id, REP_PROTOCOL_DEC_ADMIN, DECORATION_MASK,
	    pup->pub_delcust, DECORATION_IN_USE);

	rmd.rmd_q = brip->bri_q;
	rmd.rmd_tx = brip->bri_tx;

	if (backend_tx_run(brip->bri_tx, q, tx_reset_mask, &rmd) != 0)
		uu_warn(
		    "Unable to reset masking for decoration id %d\n", dec_id);

	backend_query_free(q);

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Columns processed in order :
 * 	lnk_prop_id,
 * 	lnk_prop_name,
 * 	lnk_prop_type,
 * 	lnk_val_id,
 * 	lnk_val_decoration_key,
 * 	decoration_key,
 * 	lnk_decoration_id,
 * 	decoration_value_id,
 * 	decoration_layer,
 * 	decoration_bundle_id,
 * 	decoration_flags,
 * 	decoration_type
 *
 * 	These columns are provide by a inner join on the
 * 	prop_lnk_tbl and the decoration_tbl
 */
/* ARGSUSED */
static int
object_prop_bundle_finish(void *data_arg, int columns,
    char **vals, char **names)
{
	pg_update_bundle_info_t	*pup;
	bundle_remove_info_t	*brip = data_arg;

	struct timeval	ts;
	uint32_t new_dec_key;
	uint32_t pdec_key;
	uint32_t prevgen;
	int	rollback = 0;

	const char *prop_name = vals[1];
	const char *prop_type = vals[2];
	uint32_t	lnk_val_id;
	uint32_t	prop_id;
	const char	*tv_key = NULL;
	const char	*dec_key = NULL;
	uint32_t	dec_id;
	uint32_t	dec_val_id;
	uint32_t	dec_layer = 0;
	uint32_t	dec_bundle_id = 0;
	const char	*dec_type = NULL;
	uint32_t	dec_flags = 0;

	pup = brip->bri_cur_pup;

	string_to_id(vals[0], &prop_id, names[0]);
	tv_key = vals[4];
	dec_key = vals[5];
	string_to_id(vals[6], &dec_id, names[6]);
	if (vals[7] == NULL) {
		dec_val_id = 0;
	} else {
		string_to_id(vals[7], &dec_val_id, names[7]);
	}
	string_to_id(vals[8], &dec_layer, names[8]);
	string_to_id(vals[9], &dec_bundle_id, names[9]);
	string_to_id(vals[10], &dec_flags, names[10]);
	dec_type = vals[11];

	if (vals[3] == NULL) {
		lnk_val_id = 0;
	} else {
		string_to_id(vals[3], &lnk_val_id, names[3]);
	}

	/*
	 * If muting or delcust'ing then skip the prop/decoration and mark it
	 * with SNAPSHOT_ONLY support.  When propagating a mask, the delete list
	 * entries are properties that only have admin support.
	 *
	 * If dropping bundle support, skip the prop/decoration,
	 * but mark the decoration as no bundle support.
	 */
	if (brip->nolstchk == 0 &&
	    chk_pub_list(prop_id, pup->pub_dprop_ids, pup->pub_dprop_idx)) {
		if (pup->pub_geninsnapshot == 0) {
			backend_query_add(brip->bri_q,
			    "DELETE FROM decoration_tbl "
			    "    WHERE decoration_id = %d AND "
			    "        decoration_gen_id = %d; "
			    "DELETE FROM prop_lnk_tbl "
			    "    WHERE lnk_prop_id = %d; ",
			    dec_id, pup->pub_gen_id, prop_id);

			return (BACKEND_CALLBACK_CONTINUE);
		}

		if (pup->pub_delcust == 1)
			dec_flags = DECORATION_SNAP_ONLY|DECORATION_DELCUSTED;
		else
			dec_flags = DECORATION_SNAP_ONLY;

		backend_query_add(brip->bri_q, "UPDATE decoration_tbl "
		    "SET decoration_flags = (decoration_flags | %d) "
		    "WHERE decoration_id = %d AND decoration_gen_id = %d; ",
		    dec_flags, dec_id, pup->pub_gen_id);

		return (BACKEND_CALLBACK_CONTINUE);
	}

	/*
	 * If the property needs to be rolled back to the previous generation
	 * then just collect the information from that generation and fill
	 * the variables and proceed with the next set of inserts.
	 *
	 * And since this is going to ultimately drop right back into here
	 * then just recursively call ourselves with the same query, but
	 * short circuit the chk_pub_list() checks and go straight to copy
	 * property.
	 */
	if (brip->nolstchk == 0 &&
	    chk_pub_list(prop_id, pup->pub_rback_ids, pup->pub_rback_idx))
		rollback = 1;

	if (rollback && (pup->pub_setmask == 1 || pup->pub_delcust == 1)) {
		backend_query_t *q = backend_query_alloc();

		/*
		 * If this is a delete, rollback to the value of the next lowest
		 * layer decoration information and add an admin row with the
		 * mask flag.
		 */
		backend_query_add(q,
		    "SELECT lnk_prop_name, lnk_val_decoration_key, "
		    "    decoration_id, decoration_entity_type, "
		    "    decoration_key, decoration_value_id, "
		    "    decoration_bundle_id, decoration_type, "
		    "    decoration_flags, decoration_layer "
		    "    FROM decoration_tbl "
		    "    INNER JOIN prop_lnk_tbl "
		    "        ON lnk_decoration_id = decoration_id AND "
		    "            lnk_gen_id = decoration_gen_id "
		    "    WHERE decoration_id = %d AND "
		    "        (decoration_flags & %d) = 0 AND "
		    "        decoration_layer < %d "
		    "    ORDER BY decoration_layer DESC, "
		    "        decoration_gen_id DESC LIMIT 1",
		    dec_id,  DECORATION_NOFILE|DECORATION_IN_USE,
		    REP_PROTOCOL_DEC_ADMIN);

		brip->result = backend_tx_run(brip->bri_tx, q,
		    rollback_and_mask, brip);
		if (brip->result != REP_PROTOCOL_SUCCESS) {
			backend_query_free(q);
			return (BACKEND_CALLBACK_ABORT);
		}

		backend_query_free(q);

		return (BACKEND_CALLBACK_CONTINUE);
	}

	/* Undelete or remove due to missing bundle support */
	if (rollback && pup->pub_setmask == 0) {
		backend_query_t *q = backend_query_alloc();

		if (pup->pub_delcust == 0) {
			backend_query_add(brip->bri_q,
			    "UPDATE decoration_tbl SET decoration_flags = "
			    "    (decoration_flags | %d) "
			    "    WHERE decoration_id = %d AND "
			    "        decoration_gen_id = %d; ",
			    DECORATION_NOFILE|DECORATION_SNAP_ONLY, dec_id,
			    pup->pub_gen_id);
		}

		/*
		 * Choose the most recent gen_id to rollback to.
		 */
		backend_query_add(q,
		    "SELECT decoration_gen_id from decoration_tbl "
		    "WHERE decoration_id = %d AND "
		    "    (decoration_flags & %d) = 0 AND "
		    "    decoration_gen_id < %d ORDER BY "
		    "    decoration_gen_id DESC LIMIT 1 ",
		    dec_id, DECORATION_NOFILE|DECORATION_IN_USE,
		    pup->pub_gen_id);

		brip->result = backend_tx_run_single_int(brip->bri_tx, q,
		    &prevgen);
		if (brip->result != REP_PROTOCOL_SUCCESS) {
			backend_query_free(q);
			return (BACKEND_CALLBACK_ABORT);
		}

		brip->nolstchk = 1;
		backend_query_reset(q);
		backend_query_add(q,
		    "SELECT lnk_prop_id, lnk_prop_name, lnk_prop_type, "
		    "    lnk_val_id, lnk_val_decoration_key, "
		    "    decoration_key, lnk_decoration_id, "
		    "    decoration_value_id, decoration_layer, "
		    "    decoration_bundle_id, decoration_flags, "
		    "    decoration_type "
		    "FROM prop_lnk_tbl "
		    "INNER JOIN decoration_tbl "
		    "    ON lnk_decoration_id = decoration_id "
		    "WHERE (lnk_pg_id = %d AND lnk_gen_id = %d AND "
		    "    lnk_decoration_id = %d AND "
		    "    decoration_gen_id = %d); ",
		    pup->pub_pg_id, prevgen, dec_id, prevgen);

		brip->result = backend_tx_run(brip->bri_tx, q,
		    object_prop_bundle_finish, brip);
		if (brip->result != REP_PROTOCOL_SUCCESS) {
			backend_query_free(q);
			return (BACKEND_CALLBACK_ABORT);
		}

		brip->nolstchk = 0;
		brip->clrconf = 0;
		backend_query_free(q);
		return (BACKEND_CALLBACK_CONTINUE);
	}

	/*
	 * Short circuited from above.  Either the property is not on the
	 * rollback list, or it's on the rollback list and we're processing a
	 * delcust or delete.
	 */
	if (pup->pub_delcust == 0) {
		int r;

		r = object_prop_check_conflict(brip->bri_tx, dec_id,
		    brip->bri_id, brip->bri_q, pup->pub_pg_id,
		    prop_name);

		if (r == 0)
			brip->clrconf = 1;
		else if (r < 0)
			return (BACKEND_CALLBACK_ABORT);
	}

	new_dec_key = backend_new_id(brip->bri_tx, BACKEND_KEY_DECORATION);
	if (strcmp(tv_key, dec_key) != 0)
		string_to_id(tv_key, &pdec_key, names[3]);
	else
		pdec_key = new_dec_key;

	backend_query_add(brip->bri_q,
	    "INSERT INTO prop_lnk_tbl"
	    "    (lnk_pg_id, lnk_gen_id, lnk_prop_name, lnk_prop_type,"
	    "    lnk_val_id, lnk_decoration_id,"
	    "    lnk_val_decoration_key) "
	    "VALUES ( %d, %d, '%q', '%q', %d, %d, %d );",
	    pup->pub_pg_id, brip->bri_gen_id, prop_name, prop_type,
	    lnk_val_id, dec_id, pdec_key);

	if (pup->pub_setmask) {
		dec_flags |= DECORATION_MASK;
		dec_layer = REP_PROTOCOL_DEC_ADMIN;
		dec_bundle_id = 0;
	}

	if (pup->pub_delcust)
		dec_flags |= DECORATION_DELCUSTED;
	else
		backend_carry_delcust_flag(brip->bri_tx, dec_id, dec_layer,
		    &dec_flags);

	/*
	 * We have already let the clear work be done when
	 * we checked for the property in conflict.
	 *
	 * Let's ensure that we don't roll the conflict
	 * forward.
	 */
	if (brip->clrconf)
		dec_flags &= ~DECORATION_CONFLICT;

	(void) gettimeofday(&ts, NULL);
	backend_query_add(brip->bri_q,
	    "INSERT INTO decoration_tbl"
	    "	(decoration_key, decoration_id, "
	    "    decoration_entity_type, decoration_value_id, "
	    "    decoration_gen_id, decoration_layer, "
	    "    decoration_bundle_id, decoration_type, "
	    "    decoration_flags, decoration_tv_sec, "
	    "    decoration_tv_usec) "
	    "VALUES ( %d, %d, '%q', %d, %d, %d, %d, %Q, %d, "
	    "    %ld, %ld );",
	    new_dec_key, dec_id, prop_type, dec_val_id, brip->bri_gen_id,
	    dec_layer, dec_bundle_id, dec_type, dec_flags,
	    ts.tv_sec, ts.tv_usec);

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Create a transaction for the work, if one is not provided.
 * Get a new gen id.
 * update the pg with the new gen id
 * select each of the properties and copy them to the new gen
 *
 * do not copy the property if it's part of the dprop list
 *
 * get the previous gen if it's in the rollback list and copy that row.
 *
 * Use the bri structure to pass around a q that we can write the
 * copy inserts to and pass a transaction.
 *
 * If in_tx is set then this is part of a bigger transaction work and
 * that transaction should be used for the changes but not commited
 * when object_pg_bundle_finish is done.
 */
int
object_pg_bundle_finish(uint32_t pgid, pg_update_bundle_info_t *data,
    uint32_t *gen, backend_tx_t *in_tx)
{
	bundle_remove_info_t	brip = {0};
	backend_query_t		*q;
	backend_tx_t		*tx = in_tx;

	uint32_t	cur_gen;
	uint32_t	new_gen;
	int		r = REP_PROTOCOL_SUCCESS;

	if (tx == NULL) {
		r = backend_tx_begin(BACKEND_TYPE_NORMAL, &tx);
		if (r != REP_PROTOCOL_SUCCESS)
			return (r);
	}

	brip.bri_tx = tx;
	brip.bri_cur_pup = data;

	q = backend_query_alloc();
	backend_query_add(q, "SELECT pg_gen_id FROM pg_tbl WHERE (pg_id = %d);",
	    pgid);
	r = backend_tx_run_single_int(tx, q, &cur_gen);

	if (r != REP_PROTOCOL_SUCCESS || cur_gen != data->pub_gen_id) {
		if (r == REP_PROTOCOL_SUCCESS)
			r = REP_PROTOCOL_FAIL_NOT_LATEST;

		if (in_tx == NULL)
			backend_tx_rollback(tx);
		goto end;
	}


	new_gen = backend_new_id(tx, BACKEND_ID_GENERATION);
	if (new_gen == 0) {
		if (in_tx == NULL)
			backend_tx_rollback(tx);
		r = REP_PROTOCOL_FAIL_NO_RESOURCES;

		goto end;
	}

	brip.bri_gen_id = new_gen;

	r = backend_tx_run_update(tx, "UPDATE pg_tbl SET pg_gen_id = %d "
	    "    WHERE (pg_id = %d AND pg_gen_id = %d);",
	    new_gen, data->pub_pg_id, data->pub_gen_id);

	if (r != REP_PROTOCOL_SUCCESS) {
		if (in_tx == NULL)
			backend_tx_rollback(tx);
		goto end;
	}

	backend_query_reset(q);
	backend_query_add(q, "SELECT 1 FROM snaplevel_lnk_tbl "
	    "WHERE snaplvl_pg_id = %d and snaplvl_gen_id = %d ",
	    data->pub_pg_id, data->pub_gen_id);

	r = backend_tx_run(tx, q, backend_fail_if_seen, NULL);
	if (r == REP_PROTOCOL_DONE)
		data->pub_geninsnapshot = 1;

	/*
	 * Ok now that we've done all the prep work, start walking
	 * the properties and copy those that are not part of the lists
	 * and delete or rollback those that are.
	 */
	backend_query_reset(q);

	backend_query_add(q,
	    "SELECT p.lnk_prop_id, p.lnk_prop_name, p.lnk_prop_type, "
	    "    p.lnk_val_id, p.lnk_val_decoration_key, d.decoration_key, "
	    "    p.lnk_decoration_id, d.decoration_value_id, "
	    "    d.decoration_layer, d.decoration_bundle_id, "
	    "    d.decoration_flags, d.decoration_type "
	    "FROM prop_lnk_tbl p, decoration_tbl d "
	    "WHERE (p.lnk_pg_id = %d AND p.lnk_gen_id = %d AND "
	    "    d.decoration_id = p.lnk_decoration_id AND "
	    "    d.decoration_gen_id = %d); ",
	    data->pub_pg_id, data->pub_gen_id, data->pub_gen_id);

	brip.bri_q = backend_query_alloc();
	brip.bri_id = data->pub_bundleid;
	r = backend_tx_run(tx, q, object_prop_bundle_finish, &brip);

	r = backend_tx_run(tx, brip.bri_q, NULL, NULL);
	backend_query_free(brip.bri_q);

	if (in_tx == NULL) {
		r = backend_tx_commit(tx);
		if (r == REP_PROTOCOL_SUCCESS)
			*gen = new_gen;
	}

end:
	backend_query_free(q);
	return (r);
}

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
 * This file only contains the transaction commit logic.
 */

#include <assert.h>
#include <alloca.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/sysmacros.h>
#include "configd.h"

#define	INVALID_OBJ_ID ((uint32_t)-1)
#define	INVALID_TYPE ((uint32_t)-1)

struct tx_cmd {
	const struct rep_protocol_transaction_cmd *tx_cmd;
	const char	*tx_prop;
	uint32_t	*tx_values;
	uint32_t	tx_nvalues;
	uint32_t	tx_orig_nvalues;
	uint32_t	tx_orig_value_id;
	uint32_t	tx_orig_dec_id;
	uint32_t	tx_dec_flags;
	char		tx_found;
	char		tx_processed;
	char		tx_bad;
	char		tx_mask;
};

static int
tx_cmd_compare(const void *key, const void *elem_arg)
{
	const struct tx_cmd *elem = elem_arg;

	return (strcmp((const char *)key, elem->tx_prop));
}

struct tx_commit_data {
	char		*txc_fmri;
	const char	*txc_bundle_name;
	char		*txc_pg_type;
	uint32_t	txc_pg_id;
	uint32_t	txc_pg_flags;
	uint32_t	txc_gen;
	uint32_t	txc_oldgen;
	uint32_t	txc_bundle_id;
	uint32_t	txc_layer;
	int		txc_refresh;
	short		txc_backend;
	backend_tx_t	*txc_tx;
	backend_query_t	*txc_inserts;
	backend_query_t *txc_deletes;
	size_t		txc_count;
	rep_protocol_responseid_t txc_result;
	struct tx_cmd	txc_cmds[1];		/* actually txc_count */
};

struct truevalue_set {
	uint32_t	tv_id;
	uint32_t	tv_key;
	uint8_t		tv_type[3];
};

struct tx_process_property_query {
	const char	*prop_name;
	uint8_t		prop_type[3];
	uint32_t	lnk_val_id;
	uint32_t	tv_key;
	uint32_t	dec_key;
	const char	*dec_id;
	uint32_t	dec_val_id;
	uint32_t	dec_layer;
	uint32_t	dec_bundle_id;
	const char	*dec_type;
	uint32_t	dec_flags;
};

#define	TX_COMMIT_DATA_SIZE(count) \
	offsetof(struct tx_commit_data, txc_cmds[count])

#define	IS_LAYER_BACKED_BY_FILE(l) \
	((l) > REP_PROTOCOL_DEC_INVALID && (l) < REP_PROTOCOL_DEC_ADMIN)

/*ARGSUSED*/
static int
tx_check_genid(void *data_arg, int columns, char **vals, char **names)
{
	tx_commit_data_t *data = data_arg;
	assert(columns == 1);
	if (atoi(vals[0]) != data->txc_oldgen)
		data->txc_result = REP_PROTOCOL_FAIL_NOT_LATEST;
	else
		data->txc_result = REP_PROTOCOL_SUCCESS;
	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Set's the true value in the structure based on the returned
 * values from the database query.
 */
static int
tx_find_truevalue(void *data_arg, int columns, char **vals, char **names)
{
	struct truevalue_set *t = data_arg;

	assert(columns == 3);

	string_to_id(vals[0], &t->tv_id, names[0]);
	string_to_id(vals[1], &t->tv_key, names[1]);
	(void) memcpy(&t->tv_type, vals[2], 3);

	return (BACKEND_CALLBACK_CONTINUE);
}

static int
tx_value_cmp(void *data_arg, int columns, char **vals, char **names)
{
	struct tx_cmd	*e = data_arg;
	const char	*str;
	uint32_t	idx;
	uint32_t	value_len;
	int		i;

	assert(columns == 2);

	string_to_id(vals[0], &idx, names[0]);
	e->tx_orig_nvalues++;

	if (idx >= e->tx_nvalues)
		return (BACKEND_CALLBACK_ABORT);

	/*
	 * Get the value from the element that is at
	 * this order.
	 *
	 * If it's the same as the one here step to the
	 * return continue.
	 */
	str = (char *)e->tx_values;
	for (i = 0; i < idx; i++) {
		/* LINTED alignment */
		value_len = *(uint32_t *)str;
		str += sizeof (uint32_t) + TX_SIZE(value_len);
	}

	str += sizeof (uint32_t);
	if (strcmp(vals[1], str) != 0)
		return (BACKEND_CALLBACK_ABORT);

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Compare a list of command element values to the values for the
 * given value id and if they are the same then return 0
 *
 * Uses the tx_value_cmp() function to make sure that each value
 * in its specific order is the same as the one in the element
 * at its specific order.  Therefore a non-zero will be returned
 * even if the order changes.
 */
static int
value_cmp(tx_commit_data_t *d, struct tx_cmd *e, uint32_t vid,
    const char *ptype)
{
	backend_query_t *q;
	uint8_t	type[3];
	int r;

	type[0] = REP_PROTOCOL_BASE_TYPE(e->tx_cmd->rptc_type);
	type[1] = REP_PROTOCOL_SUBTYPE(e->tx_cmd->rptc_type);
	type[2] = 0;

	if (strcmp(ptype, (char *)type) != 0)
		return (1);

	if (vid == 0) {
		if (e->tx_nvalues == 0)
			return (0);
		else
			return (1);
	}

	q = backend_query_alloc();

	e->tx_orig_nvalues = 0;
	backend_query_add(q, "SELECT value_order, value_value "
	    "FROM value_tbl WHERE value_id = %d "
	    "    ORDER BY value_order", vid);

	r = backend_tx_run(d->txc_tx, q, tx_value_cmp, e);

	backend_query_free(q);

	if (e->tx_orig_nvalues != e->tx_nvalues)
		r = 1;

	return (r);
}


/*
 * Take the decoration id and current layer and find the
 * latest layer that is greater than the current layer and
 * set that as the true value in the tv_set.
 *
 * If ignore_mask is set, include admin level decoration and don't use masked
 * decorations for true values selection. Property delcust/undelete operations
 * excludes the admin decoration for true value selection and is the only caller
 * that sets ignore_mask to 0.
 *
 * Consumers should initialize struct truevalue_set in case the query
 * returns no rows.
 */
static int
get_truevalue(tx_commit_data_t *data, struct tx_cmd *elem,
    struct truevalue_set *tv_set, int ignore_mask)
{
	backend_query_t *q;
	int r = 0;
	uint32_t max_layer = REP_PROTOCOL_DEC_ADMIN;
	uint32_t skip_flags = DECORATION_NOFILE | DECORATION_IN_USE;

	if (ignore_mask) {
		max_layer = REP_PROTOCOL_DEC_TOP;
		skip_flags |= DECORATION_MASK;
	}

	q = backend_query_alloc();

	backend_query_add(q,
	    "SELECT decoration_value_id,decoration_key,decoration_entity_type "
	    "    FROM decoration_tbl "
	    "    WHERE decoration_id = %d AND "
	    "        decoration_layer > %d AND decoration_layer < %d AND "
	    "        (decoration_flags & %d) = 0 "
	    "        ORDER BY decoration_layer DESC, "
	    "            decoration_gen_id DESC LIMIT 1; ",
	    elem->tx_orig_dec_id, data->txc_layer, max_layer, skip_flags);

	r = backend_tx_run(data->txc_tx, q, tx_find_truevalue, tv_set);

	backend_query_free(q);

	return (r);
}

/*
 * Do not call this function without first checking for snapshot reference
 * to the generation to be processed in this code (data->txc_oldgen).
 *
 * The function assumes that the property rows are not referenced by a
 * snapshot.
 */
/* ARGSUSED */
static int
tx_prop_tbl_clear(void *data_arg, int columns, char **vals, char **names)
{
	tx_commit_data_t *data = data_arg;
	backend_query_t *q;

	uint32_t prop_id;
	uint32_t dec_id;
	uint32_t dec_layer;
	uint32_t dec_bundle;

	int r;

	assert(columns == 4);

	string_to_id(vals[0], &prop_id, names[0]);
	string_to_id(vals[1], &dec_id, names[1]);
	string_to_id(vals[2], &dec_layer, names[2]);
	string_to_id(vals[3], &dec_bundle, names[3]);

	/*
	 * If the layer and bundle are the same, then this property
	 * is going to get processed anyway to a new generation so
	 * go ahead and drop it.
	 */
	if (data->txc_layer == dec_layer && data->txc_bundle_id == dec_bundle) {
		backend_query_add(data->txc_deletes,
		    "DELETE FROM prop_lnk_tbl WHERE lnk_prop_id = %d; "
		    "DELETE FROM decoration_tbl WHERE (decoration_id = %d "
		    "    AND decoration_gen_id = %d); ",
		    prop_id, dec_id, data->txc_oldgen);

		return (BACKEND_CALLBACK_CONTINUE);
	}

	q = backend_query_alloc();

	backend_query_add(q, "SELECT 1 FROM decoration_tbl "
	    "WHERE (decoration_id = %d AND decoration_layer = %d AND "
	    "    decoration_gen_id > %d) ",
	    dec_id, dec_layer, data->txc_oldgen);

	r = backend_tx_run(data->txc_tx, q, backend_fail_if_seen, NULL);

	if (r == REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);

		return (BACKEND_CALLBACK_CONTINUE);
	} else if (r != REP_PROTOCOL_DONE) {
		backend_query_free(q);

		return (BACKEND_CALLBACK_ABORT);
	}

	/*
	 * Check to see if the decoration row is still used.
	 * If not, then set for deletion.
	 */
	backend_query_reset(q);
	backend_query_add(q, "SELECT decoration_flags FROM decoration_tbl "
	    "WHERE (decoration_id = %d AND decoration_gen_id = %d AND "
	    "    (decoration_flags & %d) != 0) ",
	    dec_id, data->txc_oldgen, DECORATION_IN_USE);

	r = backend_tx_run(data->txc_tx, q, backend_fail_if_seen, NULL);

	backend_query_free(q);
	if (r == REP_PROTOCOL_SUCCESS) {
		backend_query_add(data->txc_deletes,
		    "DELETE FROM prop_lnk_tbl WHERE lnk_prop_id = %d; "
		    "DELETE FROM decoration_tbl WHERE (decoration_id = %d AND "
		    "    decoration_gen_id = %d); ",
		    prop_id, dec_id, data->txc_oldgen);
	} else if (r != REP_PROTOCOL_DONE) {
		return (BACKEND_CALLBACK_ABORT);
	}

	return (BACKEND_CALLBACK_CONTINUE);
}


/*ARGSUSED*/
static int
tx_delete_prop_nonpersist(void *data_arg, int columns, char **vals,
    char **names)
{
	tx_commit_data_t *data = data_arg;
	backend_query_t *q;
	uint32_t dec_id;
	uint32_t gen_id;
	int r;

	string_to_id(vals[0], &dec_id, names[0]);
	string_to_id(vals[1], &gen_id, names[1]);

	q = backend_query_alloc();

	backend_query_add(q, "SELECT 1 FROM snaplevel_lnk_tbl "
	    "WHERE (snaplvl_pg_id = %d AND snaplvl_gen_id = %d); ",
	    data->txc_pg_id, gen_id);

	r = backend_tx_run(data->txc_tx, q, backend_fail_if_seen, NULL);

	if (r == REP_PROTOCOL_DONE) {
		backend_query_add(data->txc_inserts,
		    "UPDATE decoration_tbl SET decoration_flags = %d "
		    "    WHERE (decoration_id = %d AND "
		    "        decoration_gen_id = %d); ",
		    DECORATION_NOFILE|DECORATION_MASK, dec_id, gen_id);
	} else {
		data->txc_deletes = data->txc_inserts;
		backend_query_reset(q);

		backend_query_add(q,
		    "SELECT lnk_prop_id, lnk_decoration_id, "
		    "decoration_layer, decoration_bundle_id "
		    "    FROM prop_lnk_tbl "
		    "    INNER JOIN decoration_tbl ON "
		    "        (lnk_decoration_id = decoration_id AND "
		    "        lnk_gen_id = decoration_gen_id) "
		    "    WHERE (lnk_pg_id = %d AND lnk_gen_id = %d); ",
		    data->txc_pg_id, data->txc_oldgen);

		r = backend_tx_run(data->txc_tx, q, tx_prop_tbl_clear, data);

		if (r != REP_PROTOCOL_SUCCESS && r != REP_PROTOCOL_DONE) {
			backend_query_free(q);
			return (BACKEND_CALLBACK_ABORT);
		}
	}

	backend_query_free(q);

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * RETURN VALUES :
 * 	0 - property is to be deleted
 * 	1 - property is to be masked
 * 	2 - property is to be copied into the new generation
 *
 * We return 2 in the case that the property is still supported
 * by another manifest and needs to be copied forward to continue
 * that support.
 */
static int
delete_prop_nonpersist(tx_commit_data_t *data, const char *dec_id)
{
	backend_query_t *q;
	uint32_t hl;
	int r;

	q = backend_query_alloc();

	/*
	 * If this is an administrative deletion and there
	 * are lower layers then do not actually delete
	 * the property but mask the property.
	 */
	if (data->txc_layer == REP_PROTOCOL_DEC_ADMIN) {
		backend_query_add(q, "SELECT decoration_layer "
		    "FROM decoration_tbl WHERE decoration_id = %Q "
		    "    ORDER BY decoration_layer LIMIT 1", dec_id);

		r = backend_tx_run_single_int(data->txc_tx, q, &hl);

		backend_query_free(q);
		if (r == REP_PROTOCOL_SUCCESS && hl < REP_PROTOCOL_DEC_ADMIN)
			return (1);

		return (0);
	}

	/*
	 * This will clean up any property rows that were kept around because
	 * they drifted off the snapshot lists, but were at a lower layer than
	 * the admin layer and were kept around due to file backing.  Now that
	 * the properties are no longer backed by a file the properties need
	 * to be removed.
	 */
	backend_query_add(q, "SELECT decoration_id, decoration_gen_id "
	    "FROM decoration_tbl "
	    "    WHERE (decoration_id = '%q' AND decoration_layer = %d); ",
	    dec_id, data->txc_layer);

	r = backend_tx_run(data->txc_tx, q, tx_delete_prop_nonpersist, data);
	if (r == REP_PROTOCOL_SUCCESS || r == REP_PROTOCOL_DONE)
		r = 2;

	backend_query_free(q);

	return (r);
}

/* ARGSUSED */
int
tx_reset_mask(void *data_arg, int columns, char **vals, char **names)
{
	tx_reset_mask_data_t *rmd = data_arg;
	backend_query_t *q;
	const char	*deckey = vals[0];
	const char	*decid = vals[1];
	const char	*decgen = vals[2];
	uint32_t	doing_delcust;
	uint32_t	 newflags = DECORATION_TRUE_VALUE;
	int		 r;

	q = backend_query_alloc();

	string_to_id(vals[3], &doing_delcust, names[3]);

	/*
	 * Is the decoration and property row snapsnot referenced?
	 */
	backend_query_add(q,
	    "SELECT 1 FROM snaplevel_lnk_tbl "
	    "    WHERE snaplvl_gen_id = %Q AND "
	    "    snaplvl_pg_id = (SELECT lnk_pg_id "
	    "        FROM prop_lnk_tbl WHERE lnk_decoration_id = %Q AND "
	    "            lnk_gen_id = %Q) ",
	    decgen, decid, decgen);

	r = backend_tx_run(rmd->rmd_tx, q, backend_fail_if_seen, NULL);

	/*
	 * If a snapshot reference will not keep the row around, check
	 * for a true value reference.
	 */
	if (r == REP_PROTOCOL_SUCCESS) {
		backend_query_reset(q);
		backend_query_add(q, "SELECT 1 FROM prop_lnk_tbl "
		    "WHERE lnk_gen_id != %Q AND "
		    "    lnk_val_decoration_key = %Q ",
		    decgen, deckey);

		r = backend_tx_run(rmd->rmd_tx, q,
		    backend_fail_if_seen, NULL);
	} else {
		if (doing_delcust)
			newflags = DECORATION_SNAP_ONLY|DECORATION_DELCUSTED;
		else
			newflags = DECORATION_SNAP_ONLY;

	}

	if (r == REP_PROTOCOL_DONE) {
		/*
		 * write the update query to data->txc_tx for the deckey
		 */
		backend_query_add(rmd->rmd_q,
		    "UPDATE decoration_tbl "
		    "    SET decoration_flags = (decoration_flags | %d)"
		    "    WHERE decoration_key = %Q; ",
		    newflags, deckey);
	} else {
		/*
		 * write the delete for the property row and the decoration row
		 */
		backend_query_add(rmd->rmd_q,
		    "DELETE FROM prop_lnk_tbl WHERE lnk_decoration_id = %Q AND "
		    "    lnk_gen_id = %Q; "
		    "DELETE FROM decoration_tbl WHERE decoration_id = %Q AND "
		    "    decoration_gen_id = %Q; ",
		    decid, decgen, decid, decgen);
	}

	return (BACKEND_CALLBACK_CONTINUE);
}

/* ARGSUSED */
static int
tx_check_prop_conflict(void *data_arg, int columns, char **vals, char **names)
{
	conflict_t *cinfo = data_arg;
	uint32_t vid;

	string_to_id(vals[1], &vid, names[1]);

	if (value_cmp(cinfo->data, cinfo->e, vid, vals[0]) != 0) {
		cinfo->data->txc_result = REP_PROTOCOL_CONFLICT;
		cinfo->in_conflict++;
	}

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Check the following for each property row that has a different
 * bundle id than the one we are using and is a next generation
 * lower, and is not kept around because of a SNAPSHOT only.
 *
 * 1. Is the type the same?
 * 2. Is the values the same (if there are values either in the
 *    element or in the previous properties).
 *
 * If either of these fail to be true then mark the property in
 * conflict and return.  This is not a fatal failure and only
 * the new incoming row will represent conflict.
 */
static int
check_prop_conflict(tx_commit_data_t *data, struct tx_cmd *elem)
{
	backend_query_t *q;
	conflict_t	cinfo;

	q = backend_query_alloc();

	cinfo.data = data;
	cinfo.e = elem;
	cinfo.in_conflict = 0;

	backend_query_add(q, "SELECT DISTINCT decoration_entity_type, "
	    "decoration_value_id, decoration_bundle_id, decoration_flags "
	    "    FROM decoration_tbl "
	    "        WHERE decoration_layer = %d AND "
	    "        decoration_bundle_id != %d AND decoration_id = %d AND"
	    "        (decoration_flags & %d) == 0",
	    data->txc_layer, data->txc_bundle_id, elem->tx_orig_dec_id,
	    DECORATION_NOFILE);

	(void) backend_tx_run(data->txc_tx, q, tx_check_prop_conflict, &cinfo);

	return (cinfo.in_conflict);
}

/*
 * If there is a higher property than our current layer there is
 * no need to mark the parent in conflict.
 */
static void
mark_prop_conflict(tx_commit_data_t *data, struct tx_cmd *elem)
{
	backend_query_t *q = backend_query_alloc();
	backend_tx_t *tx = data->txc_tx;

	backend_query_add(q, "SELECT 1 FROM decoration_tbl "
	    "WHERE decoration_layer > %d AND "
	    "    decoration_id = (SELECT lnk_decoration_id FROM prop_lnk_tbl "
	    "        WHERE lnk_pg_id = %d AND lnk_prop_name = '%q'); ",
	    data->txc_layer, data->txc_pg_id, elem->tx_prop);

	if (backend_tx_run(tx, q, backend_fail_if_seen, NULL) ==
	    REP_PROTOCOL_DONE) {
		backend_query_free(q);
		return;
	}

	backend_query_free(q);

	data->txc_refresh = 1;

	backend_mark_pg_parent(tx, data->txc_pg_id, data->txc_inserts);
}

/*
 * If there is a higher property than our current layer there is
 * no need to clear the parent in conflict.
 *
 * This function is also called by object_prop_check_conflict() which
 * does not create a full tx_commit_data_t or tx_cmd structure. It
 * fills in the parts that are needed by this function and its
 * children.
 */
static void
clear_prop_conflict(tx_commit_data_t *data, struct tx_cmd *elem)
{
	backend_query_t *q = backend_query_alloc();
	backend_tx_t *tx = data->txc_tx;

	backend_query_add(q, "SELECT 1 FROM decoration_tbl "
	    "WHERE decoration_layer > %d AND "
	    "    decoration_id = (SELECT lnk_decoration_id FROM prop_lnk_tbl "
	    "        WHERE lnk_pg_id = %d AND lnk_prop_name = '%q'); ",
	    data->txc_layer, data->txc_pg_id, elem->tx_prop);

	if (backend_tx_run(tx, q, backend_fail_if_seen, NULL) ==
	    REP_PROTOCOL_DONE) {
		backend_query_free(q);
		return;
	}

	backend_query_free(q);

	data->txc_refresh = 1;

	backend_clear_pg_parent(tx, data->txc_pg_id, data->txc_inserts);
}

/*
 * Copies the prop_lnk_tbl and decoration_tbl entries to the new gen_id
 * for properties not included in the transaction.
 */
static void
copy_property(tx_commit_data_t *data, struct tx_process_property_query *prop)
{
	uint32_t new_dec_key;
	uint32_t pdec_key;
	uint32_t decid;
	struct timeval ts;

	/*
	 * If this is the nonpresistent repository we don't have decorations
	 * to worry about. Just copy and return.
	 */
	if (data->txc_backend == BACKEND_TYPE_NONPERSIST) {
		backend_query_add(data->txc_inserts,
		    "INSERT INTO prop_lnk_tbl "
		    "    (lnk_pg_id, lnk_gen_id, lnk_prop_name, lnk_prop_type, "
		    "    lnk_val_id) "
		    "VALUES ( %d, %d, '%q', '%q', %d );",
		    data->txc_pg_id, data->txc_gen, prop->prop_name,
		    prop->prop_type, prop->lnk_val_id);

		return;
	}

	/*
	 * We also need to make the comparison here to see which
	 * key gets stored with the prop_lnk_row, if the key is the same
	 * as the decorations key then the new, if not then the old.
	 */
	new_dec_key = backend_new_id(data->txc_tx,
	    BACKEND_KEY_DECORATION);
	if (prop->tv_key != prop->dec_key)
		pdec_key = prop->tv_key;
	else
		pdec_key = new_dec_key;

	backend_query_add(data->txc_inserts,
	    "INSERT INTO prop_lnk_tbl"
	    "    (lnk_pg_id, lnk_gen_id, lnk_prop_name, lnk_prop_type,"
	    "    lnk_val_id, lnk_decoration_id,"
	    "    lnk_val_decoration_key) "
	    "VALUES ( %d, %d, '%q', '%q', %d, %Q, %d );",
	    data->txc_pg_id, data->txc_gen, prop->prop_name, prop->prop_type,
	    prop->lnk_val_id, prop->dec_id, pdec_key);

	string_to_id(prop->dec_id, &decid, "decoration_id");
	backend_carry_delcust_flag(data->txc_tx, decid, prop->dec_layer,
	    &prop->dec_flags);

	(void) gettimeofday(&ts, NULL);
	backend_query_add(data->txc_inserts,
	    "INSERT INTO decoration_tbl"
	    "	(decoration_key, decoration_id, "
	    "    decoration_entity_type, decoration_value_id, "
	    "    decoration_gen_id, decoration_layer, "
	    "    decoration_bundle_id, decoration_type, "
	    "    decoration_flags, decoration_tv_sec, "
	    "    decoration_tv_usec) "
	    "VALUES ( %d, %Q, '%q', %d, %d, %d, %d, %Q, %d, "
	    "    %ld, %ld );",
	    new_dec_key, prop->dec_id, prop->prop_type, prop->dec_val_id,
	    data->txc_gen, prop->dec_layer, prop->dec_bundle_id,
	    prop->dec_type, prop->dec_flags, ts.tv_sec, ts.tv_usec);
}

/*
 * tx_process_property() is called once for each property in current
 * property group generation.  Its purpose is threefold:
 *
 *	1. copy properties not mentioned in the transaction over unchanged.
 *	2. mark DELETEd properties as seen (they will be left out of the new
 *	   generation).
 *	3. consistancy-check NEW, CLEAR, and REPLACE commands.
 *
 * Any consistency problems set tx_bad, and seen properties are marked
 * tx_found.  These are used later, in tx_process_cmds().
 *
 * In the persistent repository, the values are:
 *
 *	0	lnk_prop_name
 *	1	lnk_prop_type
 *	2	lnk_val_id
 *	3	lnk_val_decoration_key
 *	4	decoration_key
 *	5	lnk_decoration_id
 *	6	decoration_value_id
 *	7	decoration_layer
 *	8	decoration_bundle_id
 *	9	decoration_flags
 *	10	decoration_type
 *
 * In the volatile repository, the values are:
 *
 *	0	lnk_prop_name
 *	1	lnk_prop_type
 *	2	lnk_val_id
 */
/*ARGSUSED*/
static int
tx_process_property(void *data_arg, int columns, char **vals, char **names)
{
	tx_commit_data_t *data = data_arg;
	struct tx_cmd *elem;
	struct tx_process_property_query prop = {0};

	int r;

	prop.prop_name = vals[0];
	(void) memcpy(&prop.prop_type, vals[1], 3);
	prop.prop_type[2] = '\0'; /* in case vals[1] is only 1 char long */
	if (vals[2] != NULL) {
		string_to_id(vals[2], &prop.lnk_val_id, names[2]);
	}

	if (data->txc_backend != BACKEND_TYPE_NONPERSIST) {
		assert(columns == 11);

		string_to_id(vals[3], &prop.tv_key, names[3]);
		string_to_id(vals[4], &prop.dec_key, names[4]);
		prop.dec_id = vals[5];
		if (vals[6] != NULL) {
			string_to_id(vals[6], &prop.dec_val_id, names[6]);
		}
		string_to_id(vals[7], &prop.dec_layer, names[7]);
		string_to_id(vals[8], &prop.dec_bundle_id, names[8]);
		string_to_id(vals[9], &prop.dec_flags, names[9]);
		prop.dec_type = vals[10];
	} else {
		assert(columns == 3);
	}

	elem = bsearch(prop.prop_name, data->txc_cmds, data->txc_count,
	    sizeof (*data->txc_cmds), tx_cmd_compare);

	/*
	 * On imports the element will always be found because svccfg
	 * pushes all the properties down.
	 */

	if (elem == NULL) {
		copy_property(data, &prop);
		return (BACKEND_CALLBACK_CONTINUE);
	} else {
		backend_query_t *q;
		uint32_t layer_flags = 0;
		uint32_t doing_delcust = 0;

		assert(!elem->tx_found);
		elem->tx_found = 1;

		if (prop.dec_id != NULL) {
			char *endptr;

			errno = 0;
			elem->tx_orig_dec_id =
			    strtoul(prop.dec_id, &endptr, 10);
			if (elem->tx_orig_dec_id == 0 || *endptr != 0 ||
			    errno != 0) {
				return (BACKEND_CALLBACK_ABORT);
			}
		} else {
			elem->tx_orig_dec_id = 0;
		}

		/*
		 * If this is an import or apply layer, then compare the
		 * current value set to the incoming value set in the element.
		 * If they are the same and the property type is the same, then
		 * set the element as processed and go back up to just copy the
		 * row.
		 *
		 * Also if the value cmp does not return and this is a different
		 * bundle id the property is in conflict.  If the value cmp
		 * returns clean, and the property is already in conflict need
		 * to check if the conflict can be cleared.
		 */
		if (data->txc_backend != BACKEND_TYPE_NONPERSIST) {
			q = backend_query_alloc();

			backend_query_add(q, "SELECT decoration_flags "
			    "FROM decoration_tbl WHERE decoration_layer = %d "
			    "    AND decoration_id = %d "
			    "    ORDER BY decoration_gen_id DESC LIMIT 1; ",
			    data->txc_layer, elem->tx_orig_dec_id);

			(void) backend_tx_run_single_int(data->txc_tx, q,
			    &layer_flags);

			backend_query_free(q);
		} else {
			/*
			 * There are no flags in the non-persistent repository.
			 */
			layer_flags = 0;
		}

		if (IS_LAYER_BACKED_BY_FILE(data->txc_layer)) {
			if (prop.dec_bundle_id != 0 &&
			    prop.dec_bundle_id != data->txc_bundle_id &&
			    check_prop_conflict(data, elem) > 0) {
				/*
				 * If the dec_flags are not set and
				 * this is a new conflict then we
				 * need to increment the parent
				 * service or instance count.
				 */

				if ((layer_flags & DECORATION_CONFLICT) == 0)
					mark_prop_conflict(data, elem);

				prop.dec_flags |= DECORATION_CONFLICT;
				data->txc_result = REP_PROTOCOL_CONFLICT;
			} else {
				if ((layer_flags & DECORATION_CONFLICT) &&
				    check_prop_conflict(data, elem) == 0) {
					clear_prop_conflict(data, elem);
					prop.dec_flags &= ~DECORATION_CONFLICT;
				}
			}
		} else if (data->txc_layer == REP_PROTOCOL_DEC_ADMIN) {
			if (prop.dec_flags & DECORATION_CONFLICT)
				clear_prop_conflict(data, elem);

			prop.dec_flags &= ~DECORATION_CONFLICT;
		}
		elem->tx_dec_flags = prop.dec_flags;
		elem->tx_orig_value_id = prop.lnk_val_id;

		switch (elem->tx_cmd->rptc_action) {
		case REP_PROTOCOL_TX_ENTRY_NEW:
			/*
			 * This is only bad if we aren't adding to the admin
			 * layer and weren't masked before.
			 */
			if (IS_LAYER_BACKED_BY_FILE(data->txc_layer) &&
			    (elem->tx_dec_flags & DECORATION_MASK) != 0) {
				elem->tx_bad = 1;
				data->txc_result = REP_PROTOCOL_FAIL_EXISTS;
			}
			break;
		case REP_PROTOCOL_TX_ENTRY_CLEAR:
			if (REP_PROTOCOL_BASE_TYPE(elem->tx_cmd->rptc_type) !=
			    (prop.prop_type)[0] &&
			    REP_PROTOCOL_SUBTYPE(elem->tx_cmd->rptc_type) !=
			    (prop.prop_type)[1]) {
				elem->tx_bad = 1;
				data->txc_result =
				    REP_PROTOCOL_FAIL_TYPE_MISMATCH;
			}
			break;
		case REP_PROTOCOL_TX_ENTRY_REPLACE:
			break;
		case REP_PROTOCOL_TX_ENTRY_REMOVE:
			elem->tx_processed = 1;
			break;
		case REP_PROTOCOL_TX_ENTRY_DELETE:
			if (data->txc_backend != BACKEND_TYPE_NONPERSIST) {
				/*
				 * Need to mask with the true value.
				 */
				r = delete_prop_nonpersist(data, prop.dec_id);
				if (r == 1) {
					struct truevalue_set t;
					uint32_t layerbackup = data->txc_layer;

					/*
					 * Have to trick the layer here.
					 */
					data->txc_layer = 0;

					/*
					 * Need to initialize t
					 */
					t.tv_id = prop.lnk_val_id;
					t.tv_key = prop.tv_key;
					(void) memcpy(&t.tv_type,
					    prop.prop_type, 3);

					r = get_truevalue(data, elem, &t, 1);
					data->txc_layer = layerbackup;
					if (r != REP_PROTOCOL_SUCCESS) {
						uu_warn("Unable to get true "
						    "value for %s\n",
						    elem->tx_prop);
					}

					prop.lnk_val_id = t.tv_id;
					prop.dec_val_id = t.tv_id;
					prop.tv_key = t.tv_key;
					(void) memcpy(prop.prop_type,
					    &t.tv_type, 3);

					elem->tx_mask = 1;
					prop.dec_flags = DECORATION_MASK;
					prop.dec_layer = REP_PROTOCOL_DEC_ADMIN;
					prop.dec_bundle_id = 0;

					copy_property(data, &prop);
					return (BACKEND_CALLBACK_CONTINUE);
				} else if (r == 2) {

					copy_property(data, &prop);
					return (BACKEND_CALLBACK_CONTINUE);
				}
			}
			elem->tx_processed = 1;
			break;
		case REP_PROTOCOL_TX_ENTRY_DELCUST:
			doing_delcust = 1;
			/* FALLTHROUGH */
		case REP_PROTOCOL_TX_ENTRY_UNDELETE:
			if (data->txc_backend != BACKEND_TYPE_NONPERSIST &&
			    data->txc_layer == REP_PROTOCOL_DEC_ADMIN) {
				backend_query_t *q;
				tx_reset_mask_data_t tx_rmd;
				struct truevalue_set t;
				uint32_t layerbackup = data->txc_layer;

				elem->tx_mask = 1;

				/*
				 * For an undelete, drop mask admin decorations.
				 *
				 * For a delcust, drop all admin decorations.
				 *
				 * The doing_delcust flag has no meaning when
				 * bitwise-OR'd with the decoration flags.  It's
				 * used here to match *all* admin decorations on
				 * delcust, but only *mask* admin decorations on
				 * undelete.
				 *
				 * The callback will cause the selected
				 * decorations to NOT be propagated to the next
				 * generation, either by removing them from the
				 * repository or by marking them as _SNAP_ONLY
				 * or _TRUE_VALUE.
				 */
				q = backend_query_alloc();
				backend_query_add(q,
				    "SELECT decoration_key, decoration_id, "
				    "    decoration_gen_id, %d "
				    "FROM decoration_tbl WHERE "
				    "    decoration_id = %Q AND "
				    "    decoration_layer = %d AND "
				    "    ((decoration_flags & %d) | %d) != 0 "
				    "    AND (decoration_flags & %d) = 0 ",
				    doing_delcust, prop.dec_id,
				    REP_PROTOCOL_DEC_ADMIN, DECORATION_MASK,
				    doing_delcust, DECORATION_IN_USE);

				tx_rmd.rmd_q = data->txc_inserts;
				tx_rmd.rmd_tx = data->txc_tx;
				r = backend_tx_run(data->txc_tx, q,
				    tx_reset_mask, &tx_rmd);

				if (r != 0) {
					uu_warn("Unable to reset masking for "
					    "%s\n", elem->tx_prop);
				}

				/*
				 * The layer should be the previous highest
				 * layer below admin.
				 */
				backend_query_reset(q);
				backend_query_add(q, "SELECT decoration_layer "
				    "FROM decoration_tbl "
				    "    WHERE decoration_id = %Q AND "
				    "    decoration_layer < %d AND "
				    "    (decoration_flags & %d) = 0 ORDER BY "
				    "    decoration_layer DESC LIMIT 1",
				    prop.dec_id, REP_PROTOCOL_DEC_ADMIN,
				    DECORATION_IN_USE);

				r = backend_tx_run_single_int(data->txc_tx,
				    q, &prop.dec_layer);

				if (r == REP_PROTOCOL_FAIL_NOT_FOUND)
					goto skip_on_admin_only;

				backend_query_reset(q);
				backend_query_add(q,
				    "SELECT decoration_bundle_id "
				    "FROM decoration_tbl "
				    "    WHERE decoration_id = %Q AND "
				    "    decoration_layer < %d ORDER BY "
				    "    decoration_layer DESC LIMIT 1",
				    prop.dec_id, REP_PROTOCOL_DEC_ADMIN);

				(void) backend_tx_run_single_int(data->txc_tx,
				    q, &prop.dec_bundle_id);

				/*
				 * Have to trick the layer here.
				 */
				data->txc_layer = 0;

				/*
				 * Need to initialize t
				 */
				t.tv_id = prop.lnk_val_id;
				t.tv_key = prop.tv_key;
				(void) memcpy(&t.tv_type,
				    prop.prop_type, 3);

				r = get_truevalue(data, elem, &t, 0);
				data->txc_layer = layerbackup;
				if (r != REP_PROTOCOL_SUCCESS) {
					uu_warn("Unable to get true value for "
					    "%s\n", elem->tx_prop);
				}

				prop.lnk_val_id = t.tv_id;
				prop.dec_val_id = t.tv_id;
				prop.tv_key = t.tv_key;
				(void) memcpy(prop.prop_type,
				    &t.tv_type, 3);

				prop.dec_flags = DECORATION_UNMASK;

				copy_property(data, &prop);

			skip_on_admin_only:
				backend_query_free(q);
				return (BACKEND_CALLBACK_CONTINUE);
			}
			elem->tx_processed = 1;
			break;
		default:
			assert(0);
			break;
		}
	}

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Takes a bundle name and returns the corresponding bundle_id
 * if the bundle exists, if not the creates the bundle_tbl entry
 * and returns the new id.
 */
uint32_t
get_bundle_id(backend_tx_t *tx, const char *bundle_name)
{
	backend_query_t *q;
	uint32_t id = 0;
	int r;

	if (bundle_name == NULL)
		return (0);

	q = backend_query_alloc();
	backend_query_add(q, "SELECT bundle_id FROM bundle_tbl "
	    "WHERE bundle_name = '%q'", bundle_name);

	(void) backend_tx_run_single_int(tx, q, &id);

	backend_query_reset(q);

	if (id == 0) {
		id = backend_new_id(tx, BACKEND_ID_BUNDLE);
		backend_query_add(q, "INSERT INTO bundle_tbl"
		    "    (bundle_id, bundle_name, bundle_timestamp) "
		    "VALUES (%d, '%q', strftime('%%s', 'now')) ",
		    id, bundle_name);

		r = backend_tx_run(tx, q, NULL, NULL);

		if (r != REP_PROTOCOL_SUCCESS) {
			backend_tx_rollback(tx);
			/*
			 * Should we abort rather than just set id to 0?
			 */
			id = 0;
		}
	}

	backend_query_free(q);

	return (id);
}

/*
 * Special case create the complete property
 */
static void
create_complete_object(tx_commit_data_t *data)
{
	const char *pname = SCF_PROPERTY_COMPLETE;
	struct timeval ts;
	backend_query_t *q;
	uint32_t dec_id = 0;
	uint32_t dec_key = 0;
	uint8_t type[3];
	int r;

	/*
	 * If there is a bundle then this is an apply of
	 * a profile at the admin layer and the complete
	 * object will get created internally to that process.
	 */
	if (data->txc_bundle_name != NULL ||
	    data->txc_backend != BACKEND_TYPE_NORMAL)
		return;

	q = backend_query_alloc();

	backend_query_add(q,
	    "SELECT 1 FROM prop_lnk_tbl WHERE "
	    "    (lnk_pg_id = %d AND lnk_gen_id = %d "
	    "    AND lnk_prop_name = '%q') ",
	    data->txc_pg_id, data->txc_gen, pname);

	r = backend_tx_run(data->txc_tx, q, backend_fail_if_seen, NULL);

	if (r != REP_PROTOCOL_SUCCESS)
		return; /* complete property already created */

	backend_query_free(q);

	dec_id = backend_new_id(data->txc_tx, BACKEND_ID_DECORATION);
	dec_key = backend_new_id(data->txc_tx, BACKEND_KEY_DECORATION);
	if (dec_id == 0 || dec_key == 0)
		return;

	type[0] = REP_PROTOCOL_BASE_TYPE(REP_PROTOCOL_TYPE_STRING);
	type[1] = REP_PROTOCOL_SUBTYPE(REP_PROTOCOL_TYPE_STRING);
	type[2] = 0;

	(void) gettimeofday(&ts, NULL);
	r = backend_tx_run_update(data->txc_tx,
	    "INSERT INTO prop_lnk_tbl"
	    "    (lnk_pg_id, lnk_gen_id, lnk_prop_name, lnk_prop_type, "
	    "    lnk_val_id, lnk_decoration_id, lnk_val_decoration_key) "
	    "VALUES ( %d, %d, '%q', '%q', 0, %d, %d); "
	    "INSERT INTO decoration_tbl "
	    "    (decoration_key, decoration_id, decoration_entity_type, "
	    "    decoration_value_id, decoration_gen_id, decoration_layer, "
	    "    decoration_bundle_id, decoration_type, decoration_tv_sec, "
	    "    decoration_tv_usec) "
	    "VALUES ( %d, %d, '%q', 0, %d, %d, 0, %d, %ld, %ld ); ",
	    data->txc_pg_id, data->txc_gen, pname, type, dec_id, dec_key,
	    dec_key, dec_id, type, data->txc_gen, data->txc_layer,
	    DECORATION_TYPE_PROP, ts.tv_sec, ts.tv_usec);
}

/*
 * tx_process_cmds() finishes the job tx_process_property() started:
 *
 *	1. if tx_process_property() marked a command as bad, we skip it.
 *	2. if a DELETE, REPLACE, or CLEAR operated on a non-existant property,
 *	    we mark it as bad.
 *	3. we complete the work of NEW, REPLACE, and CLEAR, by inserting the
 *	    appropriate values into the database.
 *	4. we delete all replaced data, if it is no longer referenced.
 *
 * Finally, we check all of the commands, and fail if anything was marked bad.
 */
static int
tx_process_cmds(tx_commit_data_t *data)
{
	int idx;
	int r;
	int count = data->txc_count;
	struct tx_cmd *elem;
	uint32_t val_id = 0;
	uint32_t dec_id = 0;
	uint32_t dec_key = 0;
	uint32_t dec_layer = data->txc_layer;
	uint32_t bundle_id = data->txc_bundle_id;
	uint32_t dec_flags;
	struct truevalue_set t;
	uint8_t type[3];
	int new_was_masked = 0;

	struct timeval ts;

	backend_query_t *q;
	int do_delete = 1;
	int fnd_bad;

	/*
	 * For persistent pgs, we use backend_fail_if_seen to abort the
	 * deletion if there is a snapshot using our current state.
	 *
	 * All of the deletions in this function are safe, since
	 * rc_tx_commit() guarantees that all the data is in-cache.
	 */
	q = backend_query_alloc();

	if (data->txc_backend != BACKEND_TYPE_NONPERSIST) {
		backend_query_add(q,
		    "SELECT 1 FROM snaplevel_lnk_tbl "
		    "    WHERE (snaplvl_pg_id = %d AND snaplvl_gen_id = %d); ",
		    data->txc_pg_id, data->txc_oldgen);

		r = backend_tx_run(data->txc_tx, q, backend_fail_if_seen, NULL);
		if (r == REP_PROTOCOL_SUCCESS) {
			do_delete = 1;
		} else if (r == REP_PROTOCOL_DONE) {
			do_delete = 0;		/* old gen_id is in use */
		} else {
			backend_query_free(q);
			return (r);
		}

		/*
		 * Need to process each property indiviually to make sure that
		 * we protect the higher layers from drifting off when there
		 * is no snapshot holding them in place.
		 *
		 * If not in use for each property check to see if this
		 * generation is not the latest generation for it's layer,
		 * and if so, do the flags indicate that this was kept
		 * due to snapshot only.
		 *
		 * If the property is not then delete the property, and
		 * decoration.  If yes, but it's only kept due to snapshot
		 * then delete the row set.
		 */
		if (do_delete) {
			data->txc_deletes = backend_query_alloc();

			backend_query_reset(q);
			backend_query_add(q,
			    "SELECT lnk_prop_id, lnk_decoration_id, "
			    "decoration_layer, decoration_bundle_id "
			    "    FROM prop_lnk_tbl "
			    "    INNER JOIN decoration_tbl ON "
			    "        (lnk_decoration_id = decoration_id AND "
			    "        lnk_gen_id = decoration_gen_id) "
			    "    WHERE (lnk_pg_id = %d AND lnk_gen_id = %d); ",
			    data->txc_pg_id, data->txc_oldgen);

			r  = backend_tx_run(data->txc_tx, q, tx_prop_tbl_clear,
			    data);

			if (r != REP_PROTOCOL_SUCCESS &&
			    r != REP_PROTOCOL_DONE) {
				backend_query_free(q);
				backend_query_free(data->txc_deletes);
				return (r);
			}

			r = backend_tx_run(data->txc_tx, data->txc_deletes,
			    NULL, NULL);

			backend_query_free(data->txc_deletes);
			if (r != REP_PROTOCOL_SUCCESS &&
			    r != REP_PROTOCOL_DONE) {
				backend_query_free(q);
				return (r);
			}

			backend_query_free(q);
		}
	} else {
		backend_query_add(q, "DELETE FROM prop_lnk_tbl"
		    "    WHERE (lnk_pg_id = %d AND lnk_gen_id = %d)",
		    data->txc_pg_id, data->txc_oldgen);

		r = backend_tx_run(data->txc_tx, q, backend_fail_if_seen, NULL);
		backend_query_free(q);

		if (r == REP_PROTOCOL_SUCCESS)
			do_delete = 1;
		else if (r == REP_PROTOCOL_DONE)
			do_delete = 0;
		else
			return (r);
	}

	fnd_bad = 0;
	for (idx = 0; idx < count; idx++) {
		elem = &data->txc_cmds[idx];

		if (elem->tx_bad) {
			fnd_bad = 1;
			continue;
		}

		dec_flags = elem->tx_dec_flags;

		switch (elem->tx_cmd->rptc_action) {
		case REP_PROTOCOL_TX_ENTRY_REMOVE:
		case REP_PROTOCOL_TX_ENTRY_DELETE:
		case REP_PROTOCOL_TX_ENTRY_UNDELETE:
		case REP_PROTOCOL_TX_ENTRY_DELCUST:
			if (elem->tx_mask)
				continue;

			if (!elem->tx_found) {
				fnd_bad = elem->tx_bad = 1;
				continue;
			}
			break;
		case REP_PROTOCOL_TX_ENTRY_REPLACE:
		case REP_PROTOCOL_TX_ENTRY_CLEAR:
			if (!elem->tx_found) {
				fnd_bad = elem->tx_bad = 1;
				continue;
			}
			/* Fall through to shared unmask code in ENTRY_NEW. */
			/* FALLTHROUGH */
		case REP_PROTOCOL_TX_ENTRY_NEW:
			/*
			 * We never take the masking flag from previous gen_id.
			 * Masking is only possible from admin or profile layers
			 * but they don't propagate for two reasons:
			 *
			 * 1) the next decoration in same layer would replace
			 *    previous masked decoration.
			 * 2) decoration for lower layers don't inherit
			 *    masking flags from higher layer because that would
			 *    polulte the source of masking.
			 */
			if ((dec_flags & DECORATION_MASK) != 0) {
				dec_flags &= ~DECORATION_MASK;
				if (data->txc_layer == REP_PROTOCOL_DEC_ADMIN) {
					new_was_masked = 1;
				}
			}

			break;
		default:
			assert(0);
			break;
		}

		/*
		 * An un-masked new entry also doesn't need to go through this
		 * path, because it will get a new id, and that won't have
		 * any old values which need to be culled.
		 */
		if (do_delete &&
		    elem->tx_cmd->rptc_action != REP_PROTOCOL_TX_ENTRY_NEW &&
		    elem->tx_orig_value_id != 0) {
			/*
			 * delete the old values, if they are not in use
			 */
			q = backend_query_alloc();
			backend_query_add(q,
			    "SELECT 1 FROM prop_lnk_tbl "
			    "    WHERE (lnk_val_id = %d); "
			    "DELETE FROM value_tbl"
			    "    WHERE (value_id = %d); ",
			    elem->tx_orig_value_id, elem->tx_orig_value_id);
			r = backend_tx_run(data->txc_tx, q,
			    backend_fail_if_seen, NULL);
			backend_query_free(q);
			if (r != REP_PROTOCOL_SUCCESS && r != REP_PROTOCOL_DONE)
				return (r);
		}

		if (elem->tx_cmd->rptc_action == REP_PROTOCOL_TX_ENTRY_DELETE ||
		    elem->tx_cmd->rptc_action == REP_PROTOCOL_TX_ENTRY_REMOVE ||
		    elem->tx_cmd->rptc_action ==
		    REP_PROTOCOL_TX_ENTRY_UNDELETE ||
		    elem->tx_cmd->rptc_action ==
		    REP_PROTOCOL_TX_ENTRY_DELCUST ||
		    elem->tx_processed)
			continue;		/* no further work to do */

		type[0] = REP_PROTOCOL_BASE_TYPE(elem->tx_cmd->rptc_type);
		type[1] = REP_PROTOCOL_SUBTYPE(elem->tx_cmd->rptc_type);
		type[2] = 0;

		/*
		 * If this is a new entry then get a new
		 * decoration_id, otherwise use the decoration_id
		 * from the element.  Must get a new decoration_key
		 * for each new entry.
		 *
		 * If there is a bundle associated with it then get
		 * a new bundle_id to store in the bundle_tbl
		 * and the decoration tbl.
		 */
		if (data->txc_backend != BACKEND_TYPE_NONPERSIST) {
			dec_key = backend_new_id(data->txc_tx,
			    BACKEND_KEY_DECORATION);
			if (elem->tx_cmd->rptc_action ==
			    REP_PROTOCOL_TX_ENTRY_NEW && new_was_masked == 0) {
				dec_id = backend_new_id(data->txc_tx,
				    BACKEND_ID_DECORATION);
			} else {
				dec_id = elem->tx_orig_dec_id;
			}
		}

		if (elem->tx_nvalues == 0) {
			if (data->txc_backend != BACKEND_TYPE_NONPERSIST) {
				/*
				 * In this case there are no values so the
				 * decoration_value_id can be 0.
				 *
				 * Check to see if there is a higher
				 * layer that has a value, and store
				 * the correct data.
				 *
				 * Setting the lnk_val_id to 0 if there
				 * is no higher layer value that is to
				 * be used.
				 */
				(void) memcpy(&t.tv_type, &type, 3);
				t.tv_id = 0;
				t.tv_key = dec_key;

				r = get_truevalue(data, elem, &t, 1);
				if (r != 0)
					uu_warn("Unable to get true value for "
					    " %s\n", elem->tx_prop);

				backend_carry_delcust_flag(data->txc_tx, dec_id,
				    dec_layer, &dec_flags);

				(void) gettimeofday(&ts, NULL);
				r = backend_tx_run_update(data->txc_tx,
				    "INSERT INTO prop_lnk_tbl"
				    "    (lnk_pg_id, lnk_gen_id, "
				    "    lnk_prop_name, lnk_prop_type, "
				    "    lnk_val_id, lnk_decoration_id, "
				    "    lnk_val_decoration_key) "
				    "VALUES ( %d, %d, '%q', '%q', %d, "
				    "    %d, %d); "
				    "INSERT INTO decoration_tbl "
				    "    (decoration_key, decoration_id, "
				    "    decoration_entity_type, "
				    "    decoration_value_id, "
				    "    decoration_gen_id, decoration_layer, "
				    "    decoration_bundle_id, "
				    "    decoration_type, decoration_flags, "
				    "    decoration_tv_sec, "
				    "    decoration_tv_usec) "
				    "VALUES ( %d, %d, '%q', 0, %d, %d, %d, "
				    "    %d, %d, %ld, %ld ); ",
				    data->txc_pg_id, data->txc_gen,
				    elem->tx_prop, t.tv_type, t.tv_id, dec_id,
				    t.tv_key,
				    dec_key, dec_id, type, data->txc_gen,
				    dec_layer, bundle_id, DECORATION_TYPE_PROP,
				    dec_flags, ts.tv_sec, ts.tv_usec);
			} else {
				r = backend_tx_run_update(data->txc_tx,
				    "INSERT INTO prop_lnk_tbl "
				    "    (lnk_pg_id, lnk_gen_id, "
				    "    lnk_prop_name, lnk_prop_type, "
				    "    lnk_val_id) "
				    "VALUES ( %d, %d, '%q', '%q', 0 ); ",
				    data->txc_pg_id, data->txc_gen,
				    elem->tx_prop, type);
			}
		} else {
			uint32_t *v, i = 0;
			const char *str;

			val_id = backend_new_id(data->txc_tx, BACKEND_ID_VALUE);
			if (val_id == 0)
				return (REP_PROTOCOL_FAIL_NO_RESOURCES);

			if (data->txc_backend != BACKEND_TYPE_NONPERSIST) {
				/*
				 * Need to figure out the true value here
				 * before we make the insertion.
				 *
				 * Put the current val_id and dec_key in the
				 * structure and then do the lookup, replacing
				 * what's in the structure if another higher
				 * layer value is found.  Otherwise, the
				 * stored values will be used.
				 */
				(void) memcpy(&t.tv_type, &type, 3);
				t.tv_id = val_id;
				t.tv_key = dec_key;

				r = get_truevalue(data, elem, &t, 1);
				if (r != 0)
					uu_warn("Unable to get true value for "
					    " %s\n", elem->tx_prop);

				backend_carry_delcust_flag(data->txc_tx, dec_id,
				    dec_layer, &dec_flags);

				(void) gettimeofday(&ts, NULL);
				r = backend_tx_run_update(data->txc_tx,
				    "INSERT INTO prop_lnk_tbl "
				    "    (lnk_pg_id, lnk_gen_id, "
				    "    lnk_prop_name, lnk_prop_type, "
				    "    lnk_val_id, lnk_decoration_id, "
				    "    lnk_val_decoration_key) "
				    "VALUES ( %d, %d, '%q', '%q', %d, %d, "
				    "    %d ); "
				    "INSERT INTO decoration_tbl "
				    "    (decoration_key, decoration_id, "
				    "    decoration_entity_type, "
				    "    decoration_value_id, "
				    "    decoration_gen_id, decoration_layer, "
				    "    decoration_bundle_id, "
				    "    decoration_type, decoration_flags, "
				    "    decoration_tv_sec, "
				    "    decoration_tv_usec) "
				    "VALUES ( %d, %d, '%q', %d, %d, %d, %d, "
				    "    %d, %d, %ld, %ld ); ",
				    data->txc_pg_id, data->txc_gen,
				    elem->tx_prop, t.tv_type, t.tv_id, dec_id,
				    t.tv_key,
				    dec_key, dec_id, type, val_id,
				    data->txc_gen, dec_layer, bundle_id,
				    DECORATION_TYPE_PROP, dec_flags,
				    ts.tv_sec, ts.tv_usec);
			} else {
				r = backend_tx_run_update(data->txc_tx,
				    "INSERT INTO prop_lnk_tbl "
				    "    (lnk_pg_id, lnk_gen_id, "
				    "    lnk_prop_name, lnk_prop_type, "
				    "    lnk_val_id) "
				    "VALUES ( %d, %d, '%q', '%q', %d );",
				    data->txc_pg_id, data->txc_gen,
				    elem->tx_prop, type, val_id);
			}

			v = elem->tx_values;

			for (i = 0; i < elem->tx_nvalues; i++) {
				str = (const char *)&v[1];

				/*
				 * Update values in backend,  imposing ordering
				 * via the value_order column.  This ordering is
				 * then used in subsequent value retrieval
				 * operations.  We can safely assume that the
				 * repository schema has been upgraded (and
				 * hence has the value_order column in
				 * value_tbl),  since it is upgraded as soon as
				 * the repository is writable.
				 */
				r = backend_tx_run_update(data->txc_tx,
				    "INSERT INTO value_tbl (value_id, "
				    "value_value, "
				    "value_order) VALUES (%d, "
				    "'%q', '%d');\n",
				    val_id, str, i);

				if (r != REP_PROTOCOL_SUCCESS)
					break;

				/*LINTED alignment*/
				v = (uint32_t *)((caddr_t)str + TX_SIZE(*v));
			}
		}
		if (r != REP_PROTOCOL_SUCCESS)
			return (REP_PROTOCOL_FAIL_UNKNOWN);

		elem->tx_processed = 1;
	}

	if (fnd_bad)
		return (REP_PROTOCOL_FAIL_BAD_TX);

	return (REP_PROTOCOL_SUCCESS);
}

static boolean_t
check_string(uintptr_t loc, uint32_t len, uint32_t sz)
{
	const char *ptr = (const char *)loc;

	if (len == 0 || len > sz || ptr[len - 1] != 0 || strlen(ptr) != len - 1)
		return (0);
	return (1);
}

static int
tx_check_and_setup(tx_commit_data_t *data, const void *cmds_arg,
    uint32_t count)
{
	const repcache_client_t *cp = get_active_client();

	const struct rep_protocol_transaction_cmd *cmds;
	struct tx_cmd *cur;
	struct tx_cmd *prev = NULL;

	uintptr_t loc;
	uint32_t sz, len;
	int idx;

	data->txc_bundle_name = cp->rc_file;
	data->txc_layer = cp->rc_layer_id;

	loc = (uintptr_t)cmds_arg;

	for (idx = 0; idx < count; idx++) {
		cur = &data->txc_cmds[idx];

		cmds = (struct rep_protocol_transaction_cmd *)loc;
		cur->tx_cmd = cmds;

		sz = cmds->rptc_size;

		loc += REP_PROTOCOL_TRANSACTION_CMD_MIN_SIZE;
		sz -= REP_PROTOCOL_TRANSACTION_CMD_MIN_SIZE;

		len = cmds->rptc_name_len;
		if (len <= 1 || !check_string(loc, len, sz)) {
			return (REP_PROTOCOL_FAIL_BAD_REQUEST);
		}
		cur->tx_prop = (const char *)loc;

		len = TX_SIZE(len);
		loc += len;
		sz -= len;

		cur->tx_nvalues = 0;
		cur->tx_values = (uint32_t *)loc;

		while (sz > 0) {
			if (sz < sizeof (uint32_t))
				return (REP_PROTOCOL_FAIL_BAD_REQUEST);

			cur->tx_nvalues++;

			len = *(uint32_t *)loc;
			loc += sizeof (uint32_t);
			sz -= sizeof (uint32_t);

			if (!check_string(loc, len, sz))
				return (REP_PROTOCOL_FAIL_BAD_REQUEST);

			/*
			 * NOTE: we should be checking that the values
			 * match the purported type
			 */

			len = TX_SIZE(len);

			if (len > sz)
				return (REP_PROTOCOL_FAIL_BAD_REQUEST);

			loc += len;
			sz -= len;
		}

		if (prev != NULL && strcmp(prev->tx_prop, cur->tx_prop) >= 0)
			return (REP_PROTOCOL_FAIL_BAD_REQUEST);

		prev = cur;
	}
	return (REP_PROTOCOL_SUCCESS);
}

/*
 * Free the memory associated with a tx_commit_data structure.
 */
void
tx_commit_data_free(tx_commit_data_t *tx_data)
{
	if (tx_data == NULL)
		return;

	free(tx_data->txc_pg_type);

	uu_free(tx_data);
}

/*
 * Parse the data of a REP_PROTOCOL_PROPERTYGRP_TX_COMMIT message into a
 * more useful form.  The data in the message will be represented by a
 * tx_commit_data_t structure which is allocated by this function.  The
 * address of the allocated structure is returned to *tx_data and must be
 * freed by calling tx_commit_data_free().
 *
 * Parameters:
 *	cmds_arg	Address of the commands in the
 *			REP_PROTOCOL_PROPERTYGRP_TX_COMMIT message.
 *
 *	cmds_sz		Number of message bytes at cmds_arg.
 *
 *	tx_data		Points to the place to receive the address of the
 *			allocated memory.
 *
 * Fails with
 *	_BAD_REQUEST
 *	_NO_RESOURCES
 */
int
tx_commit_data_new(const char *pg_type, uint32_t pg_flags, const void *cmds_arg,
    size_t cmds_sz, tx_commit_data_t **tx_data)
{
	const struct rep_protocol_transaction_cmd *cmds;
	tx_commit_data_t *data;
	uintptr_t loc;
	uint32_t count;
	uint32_t sz;
	int ret;

	/*
	 * First, verify that the reported sizes make sense, and count
	 * the number of commands.
	 */
	count = 0;
	loc = (uintptr_t)cmds_arg;

	while (cmds_sz > 0) {
		cmds = (struct rep_protocol_transaction_cmd *)loc;

		if (cmds_sz <= REP_PROTOCOL_TRANSACTION_CMD_MIN_SIZE)
			return (REP_PROTOCOL_FAIL_BAD_REQUEST);

		sz = cmds->rptc_size;
		if (sz <= REP_PROTOCOL_TRANSACTION_CMD_MIN_SIZE)
			return (REP_PROTOCOL_FAIL_BAD_REQUEST);

		sz = TX_SIZE(sz);
		if (sz > cmds_sz)
			return (REP_PROTOCOL_FAIL_BAD_REQUEST);

		loc += sz;
		cmds_sz -= sz;
		count++;
	}

	data = uu_zalloc(TX_COMMIT_DATA_SIZE(count));
	if (data == NULL)
		return (REP_PROTOCOL_FAIL_NO_RESOURCES);

	if (strlen(pg_type) > 0) {
		if ((data->txc_pg_type = strdup(pg_type)) == NULL) {
			uu_free(data);
			return (REP_PROTOCOL_FAIL_NO_RESOURCES);
		}

		data->txc_pg_flags = pg_flags;
	}

	/*
	 * verify that everything looks okay, and set up our command
	 * datastructures.
	 */
	data->txc_count = count;
	ret = tx_check_and_setup(data, cmds_arg, count);
	if (ret == REP_PROTOCOL_SUCCESS) {
		*tx_data = data;
	} else {
		*tx_data = NULL;
		uu_free(data);
	}
	return (ret);
}

/*
 * The following are a set of accessor functions to retrieve data from a
 * tx_commit_data_t that has been allocated by tx_commit_data_new().
 */

/*
 * Return the action of the transaction command whose command number is
 * cmd_no.  The action is placed at *action.
 *
 * Returns:
 *	_FAIL_BAD_REQUEST	cmd_no is out of range.
 */
int
tx_cmd_action(tx_commit_data_t *tx_data, size_t cmd_no,
    enum rep_protocol_transaction_action *action)
{
	struct tx_cmd *cur;

	assert(cmd_no < tx_data->txc_count);
	if (cmd_no >= tx_data->txc_count)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);

	cur = &tx_data->txc_cmds[cmd_no];
	*action = cur->tx_cmd->rptc_action;
	return (REP_PROTOCOL_SUCCESS);
}

/*
 * Return the number of transaction commands held in tx_data.
 */
size_t
tx_cmd_count(tx_commit_data_t *tx_data)
{
	return (tx_data->txc_count);
}

/*
 * Return the number of property values that are associated with the
 * transaction command whose number is cmd_no.  The number of values is
 * returned to *nvalues.
 *
 * Returns:
 *	_FAIL_BAD_REQUEST	cmd_no is out of range.
 */
int
tx_cmd_nvalues(tx_commit_data_t *tx_data, size_t cmd_no, uint32_t *nvalues)
{
	struct tx_cmd *cur;

	assert(cmd_no < tx_data->txc_count);
	if (cmd_no >= tx_data->txc_count)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);

	cur = &tx_data->txc_cmds[cmd_no];
	*nvalues = cur->tx_nvalues;
	return (REP_PROTOCOL_SUCCESS);
}

/*
 * Return a pointer to the property name of the command whose number is
 * cmd_no.  The property name pointer is returned to *pname.
 *
 * Returns:
 *	_FAIL_BAD_REQUEST	cmd_no is out of range.
 */
int
tx_cmd_prop(tx_commit_data_t *tx_data, size_t cmd_no, const char **pname)
{
	struct tx_cmd *cur;

	assert(cmd_no < tx_data->txc_count);
	if (cmd_no >= tx_data->txc_count)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);

	cur = &tx_data->txc_cmds[cmd_no];
	*pname = cur->tx_prop;
	return (REP_PROTOCOL_SUCCESS);
}

/*
 * Return the property type of the property whose command number is
 * cmd_no.  The property type is returned to *ptype.
 *
 * Returns:
 *	_FAIL_BAD_REQUEST	cmd_no is out of range.
 */
int
tx_cmd_prop_type(tx_commit_data_t *tx_data, size_t cmd_no, uint32_t *ptype)
{
	struct tx_cmd *cur;

	assert(cmd_no < tx_data->txc_count);
	if (cmd_no >= tx_data->txc_count)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);

	cur = &tx_data->txc_cmds[cmd_no];
	*ptype = cur->tx_cmd->rptc_type;
	return (REP_PROTOCOL_SUCCESS);
}

/*
 * This function is used to retrieve a property value from the transaction
 * data.  val_no specifies which value is to be retrieved from the
 * transaction command whose number is cmd_no.  A pointer to the specified
 * value is placed in *val.
 *
 * Returns:
 *	_FAIL_BAD_REQUEST	cmd_no or val_no is out of range.
 */
int
tx_cmd_value(tx_commit_data_t *tx_data, size_t cmd_no, uint32_t val_no,
    const char **val)
{
	const char *bp;
	struct tx_cmd *cur;
	uint32_t i;
	uint32_t value_len;

	assert(cmd_no < tx_data->txc_count);
	if (cmd_no >= tx_data->txc_count)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);

	cur = &tx_data->txc_cmds[cmd_no];
	assert(val_no < cur->tx_nvalues);
	if (val_no >= cur->tx_nvalues)
		return (REP_PROTOCOL_FAIL_BAD_REQUEST);

	/* Find the correct value */
	bp = (char *)cur->tx_values;
	for (i = 0; i < val_no; i++) {
		/* LINTED alignment */
		value_len = *(uint32_t *)bp;
		bp += sizeof (uint32_t) + TX_SIZE(value_len);
	}

	/* Bypass the count & return pointer to value. */
	bp += sizeof (uint32_t);
	*val = bp;
	return (REP_PROTOCOL_SUCCESS);
}

/* ARGSUSED */
static int
tx_check_pg_conflict(void *data_arg, int columns, char **vals, char **names)
{
	conflict_t *cinfo = data_arg;
	uint32_t bundle_id;
	uint32_t dflags;

	string_to_id(vals[0], &bundle_id, names[0]);
	string_to_id(vals[1], &dflags, names[1]);

	if (dflags & DECORATION_CONFLICT)
		cinfo->in_conflict++;

	if (bundle_id == cinfo->data->txc_bundle_id)
		return (BACKEND_CALLBACK_CONTINUE);

	if (strcmp(cinfo->data->txc_pg_type, vals[2]) != 0)
		cinfo->data->txc_result = REP_PROTOCOL_CONFLICT;

	return (BACKEND_CALLBACK_CONTINUE);
}

/*
 * Check to see if the property group is in conflict.
 *
 * Walk each decoration, and if the bundle id does not
 * match the current bundle id then check to see if the
 * type is the same.  If the type differs set CONFLICT.
 *
 * Take care to store the current conflict value in case
 * there is a lower order conflict that we do no want
 * to overwrite/clear and drop the underlying result
 * back in place.
 *
 * If we find the pg in conflict then mark it and its
 * parents (service and instance).  This is done with
 * a decoration_flag.
 *
 * morc is mark or clear.  In the case we don't want to
 * do the mark or clear here this should be set to zero
 * by the caller.
 *
 * This function is also called by object_pg_check_conflict()
 * which does not create a full tx_commit_data_t or rc_node_lookup_t
 * structure.  It fills in the parts that are needed by this
 * function and its children.
 */
static void
check_pg_conflict(tx_commit_data_t *data, rc_node_lookup_t *lp, int morc)
{
	rep_protocol_responseid_t cur_result = data->txc_result;
	backend_query_t *q;
	conflict_t cinfo;

	q = backend_query_alloc();

	backend_query_add(q,
	    "SELECT decoration_bundle_id, decoration_flags, "
	    "        decoration_entity_type "
	    "    FROM decoration_tbl WHERE (decoration_layer = %d AND "
	    "        decoration_id = (SELECT pg_dec_id FROM pg_tbl WHERE "
	    "            pg_id = %d)); ",
	    data->txc_layer, lp->rl_main_id);

	data->txc_result = REP_PROTOCOL_SUCCESS;
	cinfo.data = data;
	cinfo.in_conflict = 0;
	if (backend_tx_run(data->txc_tx, q, tx_check_pg_conflict,
	    &cinfo) != REP_PROTOCOL_SUCCESS) {
		backend_query_free(q);
		return;
	}

	if (morc) {
		if (data->txc_result == REP_PROTOCOL_CONFLICT) {
			data->txc_refresh = 1;
			backend_mark_pg_conflict(data->txc_tx,
			    data->txc_layer, lp->rl_main_id);
		} else if (cinfo.in_conflict) {
			data->txc_refresh = 1;
			backend_clear_pg_conflict(data->txc_tx,
			    data->txc_layer, lp->rl_main_id);
		}
	}

	if (cur_result == REP_PROTOCOL_CONFLICT)
		data->txc_result = cur_result;

	backend_query_free(q);
}

static int
add_pg_parent_bundle_support(rc_node_lookup_t *lp, tx_commit_data_t *data,
    backend_query_t *q)
{
	decoration_type_t dectype;
	uint32_t	instid;
	uint32_t	ckey;
	int		r;

	struct timeval ts;

	ckey = backend_new_id(data->txc_tx, BACKEND_KEY_DECORATION);

	backend_query_add(q, "SELECT instance_id FROM instance_tbl "
	    "    WHERE instance_id = (SELECT pg_parent_id FROM "
	    "            pg_tbl WHERE pg_id = %d) ",
	    lp->rl_main_id);

	r = backend_tx_run_single_int(data->txc_tx, q, &instid);
	backend_query_reset(q);

	if (r == REP_PROTOCOL_FAIL_NOT_FOUND)
		dectype = DECORATION_TYPE_SVC;
	else
		dectype = DECORATION_TYPE_INST;

	/*
	 * Put the right decoration type in here
	 * instead of using decoration_pg
	 */
	(void) gettimeofday(&ts, NULL);
	r = backend_tx_run_update(data->txc_tx, "INSERT INTO decoration_tbl "
	    "    (decoration_key, decoration_id, decoration_value_id, "
	    "    decoration_gen_id, decoration_layer, decoration_bundle_id, "
	    "    decoration_type, decoration_tv_sec, decoration_tv_usec) "
	    "VALUES ( %d, (SELECT svc_dec_id FROM service_tbl WHERE svc_id = "
	    "        (SELECT pg_parent_id FROM pg_tbl WHERE pg_id = %d) UNION "
	    "    SELECT instance_dec_id FROM instance_tbl WHERE instance_id = "
	    "            (SELECT pg_parent_id FROM pg_tbl WHERE pg_id = %d)), "
	    "    0, 0, %d, %d, %d, %ld, %ld ) ",
	    ckey, lp->rl_main_id, lp->rl_main_id, data->txc_layer,
	    data->txc_bundle_id, dectype, ts.tv_sec, ts.tv_usec);

	if (dectype == DECORATION_TYPE_INST) {
		dectype = DECORATION_TYPE_SVC;
		backend_query_reset(q);

		backend_query_add(q, "SELECT 1 FROM decoration_tbl "
		    "    WHERE decoration_id = (SELECT svc_dec_id "
		    "        FROM service_tbl WHERE svc_id = (SELECT "
		    "            instance_svc FROM instance_tbl "
		    "                WHERE instance_id = %d)) "
		    "    AND decoration_bundle_id = %d",
		    instid, data->txc_bundle_id);

		r = backend_tx_run(data->txc_tx, q, backend_fail_if_seen, NULL);

		if (r == REP_PROTOCOL_SUCCESS) {
			ckey = backend_new_id(data->txc_tx,
			    BACKEND_KEY_DECORATION);

			(void) gettimeofday(&ts, NULL);
			r = backend_tx_run_update(data->txc_tx,
			    "INSERT INTO decoration_tbl (decoration_key, "
			    "    decoration_id, decoration_value_id, "
			    "    decoration_gen_id, decoration_layer, "
			    "    decoration_bundle_id, decoration_type, "
			    "    decoration_tv_sec, decoration_tv_usec) "
			    "VALUES ( %d, "
			    "    (SELECT svc_dec_id FROM service_tbl "
			    "        WHERE svc_id = (SELECT instance_svc "
			    "            FROM instance_tbl "
			    "                WHERE instance_id = %d)), "
			    "    0, 0, %d, %d, %d, %ld, %ld ) ",
			    ckey, instid, data->txc_layer, data->txc_bundle_id,
			    dectype, ts.tv_sec, ts.tv_usec);
		} else if (r == REP_PROTOCOL_DONE) {
			r = REP_PROTOCOL_SUCCESS;
		}
	}

	backend_query_reset(q);
	return (r);
}

int
object_tx_commit(rc_node_lookup_t *lp, tx_commit_data_t *data, uint32_t *gen,
    char *pg_fmri, int create_complete, int *rc_refresh)
{
	uint32_t new_gen;
	uint32_t ckey;
	int ret;
	rep_protocol_responseid_t r;
	backend_tx_t *tx;
	backend_query_t *q;
	int backend = lp->rl_backend;

	struct timeval ts;

	ret = backend_tx_begin(backend, &tx);
	if (ret != REP_PROTOCOL_SUCCESS)
		return (ret);

	/* Make sure the pg is up-to-date. */
	data->txc_fmri = pg_fmri;
	data->txc_oldgen = *gen;
	data->txc_backend = backend;
	data->txc_result = REP_PROTOCOL_FAIL_NOT_FOUND;

	q = backend_query_alloc();
	backend_query_add(q, "SELECT pg_gen_id FROM pg_tbl WHERE (pg_id = %d);",
	    lp->rl_main_id);
	r = backend_tx_run(tx, q, tx_check_genid, data);

	if (r != REP_PROTOCOL_SUCCESS ||
	    (r = data->txc_result) != REP_PROTOCOL_SUCCESS) {
		backend_tx_rollback(tx);
		goto end;
	}

	/*
	 * If the transacation is empty, cut out early, unless...
	 * the transaction is from an import and the gen is non-zero
	 * which means the pg is empty and the old pg needs to be
	 * processed as such.
	 *
	 * This is to handle the case where a property group is being
	 * updated to have no properties but the property group will
	 * still exist.
	 *
	 * This can also, handle the case where a pg is coming in
	 * from an apply that needs to get a bundle update.
	 */
	if (data->txc_count == 0 && data->txc_pg_type == NULL &&
	    (*gen == 0 || (data->txc_layer != REP_PROTOCOL_DEC_MANIFEST &&
	    (data->txc_layer != REP_PROTOCOL_DEC_SITE_PROFILE &&
	    data->txc_layer != REP_PROTOCOL_DEC_SYSTEM_PROFILE)))) {
		backend_tx_rollback(tx);
		r = REP_PROTOCOL_DONE;
		goto end;
	}

	new_gen = backend_new_id(tx, BACKEND_ID_GENERATION);
	if (new_gen == 0) {
		backend_tx_rollback(tx);
		backend_query_free(q);
		return (REP_PROTOCOL_FAIL_NO_RESOURCES);
	}

	data->txc_pg_id = lp->rl_main_id;
	data->txc_gen = new_gen;
	data->txc_tx = tx;

	backend_query_reset(q);

	if (data->txc_pg_type) {
		r = backend_tx_run_update(tx,
		    "UPDATE pg_tbl SET pg_gen_id = %d, "
		    "    pg_type = '%q', pg_flags = %d "
		    "    WHERE (pg_id = %d AND pg_gen_id = %d);",
		    new_gen, data->txc_pg_type, data->txc_pg_flags,
		    lp->rl_main_id, *gen);
	} else {
		r = backend_tx_run_update(tx,
		    "UPDATE pg_tbl SET pg_gen_id = %d "
		    "    WHERE (pg_id = %d AND pg_gen_id = %d);",
		    new_gen, lp->rl_main_id, *gen);

		backend_query_add(q, "SELECT pg_type FROM pg_tbl "
		    "    WHERE pg_id = %d; ", lp->rl_main_id);

		(void) backend_tx_run_single_str(tx, q, &data->txc_pg_type);

		backend_query_reset(q);

	}

	if (r != REP_PROTOCOL_SUCCESS) {
		backend_tx_rollback(tx);
		goto end;
	}

	if (data->txc_backend != BACKEND_TYPE_NONPERSIST) {
		/*
		 * Prep the bundle_id for this set of commands that is
		 * associated with this transaction.
		 *
		 * Doing the bundle work here, so that we can have the bundle
		 * that is associated with the transaction for comparison in the
		 * processing of the other properties, and properties being
		 * added or deleted in case future work determines a need for
		 * this information.
		 *
		 * Also, since the query and insert (if necessary) are done it
		 * saves the work from later on.
		 *
		 * NOTE: there is a chance that we leave a dangling bundle entry
		 * but a garbage collection query is pretty simple to validate
		 * that there are no decorations referencing a specific bundle
		 * and just delete any of those rows.
		 */
		if (data->txc_bundle_id == 0 && data->txc_bundle_name != NULL) {
			backend_query_add(q, "SELECT bundle_id FROM bundle_tbl "
			    "WHERE bundle_name = '%q'", data->txc_bundle_name);

			(void) backend_tx_run_single_int(tx, q,
			    &data->txc_bundle_id);

			backend_query_reset(q);

			if (data->txc_bundle_id == 0) {
				data->txc_bundle_id = backend_new_id(tx,
				    BACKEND_ID_BUNDLE);
				backend_query_add(q, "INSERT INTO bundle_tbl"
				    "    (bundle_id, bundle_name, "
				    "    bundle_timestamp) "
				    "VALUES (%d, '%q', "
				    "    strftime('%%s', 'now')) ",
				    data->txc_bundle_id, data->txc_bundle_name);

				r = backend_tx_run(tx, q, NULL, NULL);

				if (r != REP_PROTOCOL_SUCCESS) {
					backend_tx_rollback(tx);
					goto end;
				}
			}

			backend_query_reset(q);
		}

		/*
		 * Update the pg's decoration's gen_id, if there is one
		 * that matches this layer, if not then insert a new
		 * decoration into the table for this entry.
		 *
		 * Only if there is a bundle id associated with this change.
		 *
		 * Then update the parent in the same way.
		 */
		if (data->txc_bundle_id != 0) {
			backend_query_add(q,
			"SELECT decoration_key FROM decoration_tbl WHERE "
			    "     decoration_id = (SELECT pg_dec_id "
			    "         FROM pg_tbl WHERE pg_id = %d) AND "
			    "     decoration_layer = %d AND "
			    "     decoration_bundle_id = %d ",
			    lp->rl_main_id, data->txc_layer,
			    data->txc_bundle_id);

			r = backend_tx_run_single_int(data->txc_tx, q, &ckey);
			backend_query_reset(q);

			if (r == REP_PROTOCOL_SUCCESS) {
				/*
				 * Found the decoration at this layer and with a
				 * matching bundle.  So just update the
				 * decoration at the key returned.
				 */
				r = backend_tx_run_update(data->txc_tx,
				    "UPDATE decoration_tbl "
				    "    SET decoration_gen_id = %d, "
				    "    decoration_entity_type = '%q' "
				    "    WHERE decoration_key = %d ",
				    new_gen, data->txc_pg_type, ckey);
			} else if (r == REP_PROTOCOL_FAIL_NOT_FOUND) {
				ckey = backend_new_id(data->txc_tx,
				    BACKEND_KEY_DECORATION);

				/*
				 * decoration not found so this is an insert of
				 * a row to decorate this property group.
				 */
				(void) gettimeofday(&ts, NULL);
				r = backend_tx_run_update(data->txc_tx,
				    "INSERT INTO decoration_tbl "
				    "    (decoration_key, decoration_id, "
				    "    decoration_entity_type, "
				    "    decoration_value_id, "
				    "    decoration_gen_id, decoration_layer, "
				    "    decoration_bundle_id, "
				    "    decoration_type, decoration_tv_sec, "
				    "    decoration_tv_usec) "
				    "VALUES ( %d, (SELECT pg_dec_id "
				    "    FROM pg_tbl WHERE pg_id = %d), "
				    "    '%q', 0, %d, %d, %d, %d, %ld, %ld ) ",
				    ckey, lp->rl_main_id, data->txc_pg_type,
				    new_gen, data->txc_layer,
				    data->txc_bundle_id, DECORATION_TYPE_PG,
				    ts.tv_sec, ts.tv_usec);
			}

			/*
			 * Now let's make sure the parent is decorated with this
			 * operation.
			 */
			backend_query_add(q,
			    "SELECT decoration_key FROM decoration_tbl WHERE "
			    "    decoration_layer = %d AND "
			    "    decoration_bundle_id = %d AND "
			    "    decoration_id = (SELECT svc_dec_id "
			    "        FROM service_tbl WHERE svc_id = (SELECT "
			    "        pg_parent_id FROM pg_tbl "
			    "            WHERE pg_id = %d) UNION "
			    "        SELECT instance_dec_id FROM instance_tbl "
			    "            WHERE instance_id = "
			    "                (SELECT pg_parent_id FROM pg_tbl "
			    "            WHERE pg_id = %d)) ",
			    data->txc_layer, data->txc_bundle_id,
			    lp->rl_main_id, lp->rl_main_id);

			r = backend_tx_run(data->txc_tx, q,
			    backend_fail_if_seen, NULL);
			backend_query_reset(q);

			/*
			 * If seen then we are done.
			 */
			if (r == REP_PROTOCOL_SUCCESS) {
				r = add_pg_parent_bundle_support(lp, data, q);

				if (r != REP_PROTOCOL_SUCCESS) {
					backend_tx_rollback(tx);
					goto end;
				}
			}
		}

		/*
		 * Note: If the decoration_id does not exist in the
		 * prop_lnk_tbl then this will not be called for
		 * properties that are getting updated, and the
		 * tx_found element will not be set and cause a
		 * bad transaction error later down the road in
		 * tx_process_cmds().
		 *
		 * For now leaving this way as this is an error condition
		 * but may want to find a better way to catch this now
		 * as opposed to letting the failure crop up later...
		 *
		 * When detected check if decoration_id is set, if not and
		 * the persistent repository then report the message a bit
		 * clearer on the failure.
		 */
		backend_query_add(q,
		    "SELECT p.lnk_prop_name, p.lnk_prop_type, p.lnk_val_id, "
		    "    p.lnk_val_decoration_key, d.decoration_key, "
		    "    p.lnk_decoration_id, d.decoration_value_id, "
		    "    d.decoration_layer, d.decoration_bundle_id, "
		    "    d.decoration_flags, d.decoration_type "
		    "FROM prop_lnk_tbl p, decoration_tbl d "
		    "WHERE (p.lnk_pg_id = %d AND p.lnk_gen_id = %d AND "
		    "    d.decoration_id = p.lnk_decoration_id AND "
		    "    d.decoration_gen_id = %d); ",
		    lp->rl_main_id, *gen, *gen);
	} else {
		backend_query_add(q,
		    "SELECT lnk_prop_name, lnk_prop_type, lnk_val_id "
		    "FROM prop_lnk_tbl "
		    "WHERE (lnk_pg_id = %d AND lnk_gen_id = %d)",
		    lp->rl_main_id, *gen);
	}

	data->txc_inserts = backend_query_alloc();
	r = backend_tx_run(tx, q, tx_process_property, data);

	if (r == REP_PROTOCOL_DONE)
		r = REP_PROTOCOL_FAIL_UNKNOWN;		/* corruption */

	if (r != REP_PROTOCOL_SUCCESS ||
	    ((r = data->txc_result) != REP_PROTOCOL_SUCCESS &&
	    r != REP_PROTOCOL_CONFLICT)) {
		backend_query_free(data->txc_inserts);
		backend_tx_rollback(tx);
		goto end;
	}

	r = backend_tx_run(tx, data->txc_inserts, NULL, NULL);
	backend_query_free(data->txc_inserts);

	if (r != REP_PROTOCOL_SUCCESS) {
		backend_tx_rollback(tx);
		goto end;
	}

	r = tx_process_cmds(data);
	if (r != REP_PROTOCOL_SUCCESS) {
		backend_tx_rollback(tx);
		goto end;
	}

	if (create_complete)
		create_complete_object(data);

	/*
	 * Now that all is done, let's check the type against
	 * other types from other bundles to see if the property
	 * group is in conflict.
	 *
	 * The types will still be stored and set, but if there
	 * is a conflict then the appropriate flags need to
	 * be set.
	 *
	 * Also, let's mark the service and instance decorations
	 * for ease of tracking when a service/instance is in
	 * conflict.
	 */
	if (data->txc_backend != BACKEND_TYPE_NONPERSIST &&
	    data->txc_layer < REP_PROTOCOL_DEC_ADMIN)
		check_pg_conflict(data, lp, 1);

	r = backend_tx_commit(tx);

	if (r == REP_PROTOCOL_SUCCESS) {
		*gen = new_gen;
		if (data->txc_result != REP_PROTOCOL_SUCCESS)
			r = data->txc_result;
	}

end:
	backend_query_free(q);
	*rc_refresh = data->txc_refresh;
	return (r);
}

int
object_pg_check_conflict(backend_tx_t *tx, uint32_t pgid, uint32_t decid,
    uint32_t bundleid, uint32_t layer)
{
	backend_query_t *q;
	rc_node_lookup_t lp;
	tx_commit_data_t d;
	int r;

	d.txc_pg_type = NULL;
	q = backend_query_alloc();
	backend_query_add(q, "SELECT decoration_entity_type "
	    "FROM decoration_tbl WHERE (decoration_id = %d AND "
	    "    decoration_bundle_id != %d AND decoration_layer = %d) "
	    "        LIMIT 1",
	    decid, bundleid, layer);

	r = backend_tx_run_single_str(tx, q, &d.txc_pg_type);
	backend_query_free(q);

	if (r != REP_PROTOCOL_SUCCESS)
		return (0);

	d.txc_result = REP_PROTOCOL_SUCCESS;
	d.txc_tx = tx;
	d.txc_layer = layer;
	d.txc_bundle_id = bundleid;
	lp.rl_main_id = pgid;

	check_pg_conflict(&d, &lp, 0);

	if (d.txc_pg_type != NULL)
		free(d.txc_pg_type);

	if (d.txc_result == REP_PROTOCOL_CONFLICT)
		return (1);

	return (0);
}

typedef struct check_prop_data {
	backend_tx_t	*cpd_tx;
	char 		*cpd_prop_type;
	char		*cmp_val;
	uint32_t	cpd_base_vid;
	uint32_t	cpd_base_nvals;
	int		in_conflict;
} check_prop_data_t;

/* ARGSUSED */
static int
pcc_row_count(void *data_arg, int columns, char **vals, char **names)
{
	(*(int *)data_arg)++;

	return (BACKEND_CALLBACK_CONTINUE);
}

/* ARGSUSED */
static int
tx_object_prop_check_conflict(void *data_arg, int columns, char **vals,
    char **names)
{
	check_prop_data_t *cpd = data_arg;
	backend_query_t *q;
	uint32_t vid;
	int	cnt;

	if (cpd->cpd_prop_type == NULL) {
		cpd->cpd_prop_type = strdup(vals[0]);
		if (cpd->cpd_prop_type == NULL) {
			cpd->in_conflict = -1;
			return (BACKEND_CALLBACK_ABORT);
		}

		string_to_id(vals[1], &cpd->cpd_base_vid, names[1]);
		if (cpd->cpd_base_vid != 0) {
			q = backend_query_alloc();
			backend_query_add(q, "SELECT count() FROM value_tbl "
			    "WHERE value_id = %d", cpd->cpd_base_vid);

			(void) backend_tx_run_single_int(cpd->cpd_tx, q,
			    &cpd->cpd_base_nvals);

			backend_query_free(q);
		}

		return (BACKEND_CALLBACK_CONTINUE);
	}

	/*
	 * Compare the type.
	 */
	if (strcmp(cpd->cpd_prop_type, vals[0]) != 0) {
		cpd->in_conflict = 1;
		return (BACKEND_CALLBACK_ABORT);
	}

	/*
	 * One has a set of values the next does not.
	 */
	string_to_id(vals[1], &vid, names[1]);
	if ((cpd->cpd_base_vid == 0 && vid != 0) ||
	    (cpd->cpd_base_vid != 0 && vid == 0)) {
		cpd->in_conflict = 1;
		return (BACKEND_CALLBACK_ABORT);
	}

	/*
	 * Ok now compare the values.
	 */
	q = backend_query_alloc();
	backend_query_add(q, "SELECT DISTINCT value_value, value_order "
	    "FROM value_tbl WHERE (value_id = %d OR value_id = %d) ",
	    cpd->cpd_base_vid, vid);

	cnt = 0;
	(void) backend_tx_run(cpd->cpd_tx, q, pcc_row_count, &cnt);
	backend_query_free(q);

	if (cnt != cpd->cpd_base_nvals) {
		cpd->in_conflict = 1;
		return (BACKEND_CALLBACK_ABORT);
	}

	return (BACKEND_CALLBACK_CONTINUE);
}


int
object_prop_check_conflict(backend_tx_t *tx, uint32_t decid, uint32_t bundleid,
    backend_query_t *cbq, uint32_t pgid, const char *propname)
{
	check_prop_data_t cpd;
	tx_commit_data_t data = {0};
	backend_query_t	*q;
	struct tx_cmd e;

	int r;

	q = backend_query_alloc();

	/*
	 * What is the layer of this bundle iff there is a conflict in this
	 * dec_id
	 */
	backend_query_add(q,
	    "SELECT DISTINCT decoration_layer FROM decoration_tbl "
	    "WHERE decoration_id = %d AND (decoration_flags & %d) != 0 AND "
	    "decoration_layer = (SELECT DISTINCT decoration_layer "
	    "FROM decoration_tbl WHERE decoration_bundle_id = %d); ",
	    decid, DECORATION_CONFLICT, bundleid);

	r = backend_tx_run_single_int(tx, q, &data.txc_layer);

	if (r != REP_PROTOCOL_SUCCESS || data.txc_layer == 0) {
		backend_query_free(q);
		return (1);
	}

	/*
	 * Select the property type and value id associated with all
	 * decoration rows at this layer that are still supported
	 * and are not of this bundle id, that we are removing.
	 *
	 * Use a seen variable for incrementation, if the variable
	 * is set to non 0 that is the value id that we will compare
	 * the new incoming value_id set against.  If it is 0, then
	 * there is no value_id set to compare against so we will
	 * just set and return.
	 *
	 * When we set this for the first time we will set the property
	 * type, and value id.  Each following comparison will check
	 * type and then id set.  If a conflict is found abort work
	 * and just simple set the result as conflict and return.
	 *
	 * If no conflict is found then we need to call clear_prop_conflict()
	 * with the supplied q set in tx_commit_data data->txc_inserts
	 * to take on the decoration updates.
	 */
	cpd.cpd_tx = tx;
	cpd.in_conflict = 0;
	cpd.cpd_prop_type = NULL;
	cpd.cpd_base_vid = 0;

	backend_query_reset(q);
	backend_query_add(q,
	    "SELECT decoration_entity_type, decoration_value_id "
	    "    FROM decoration_tbl WHERE decoration_id = %d AND "
	    "    decoration_bundle_id != %d AND decoration_layer = %d AND "
	    "    (decoration_flags & %d) = 0; ",
	    decid, bundleid, data.txc_layer, DECORATION_NOFILE);

	r = backend_tx_run(tx, q, tx_object_prop_check_conflict, &cpd);

	if (r == REP_PROTOCOL_SUCCESS || r == REP_PROTOCOL_DONE)
		r = cpd.in_conflict;

	if (r == 0) {
		data.txc_tx = tx;
		data.txc_inserts = cbq;
		data.txc_pg_id = pgid;
		e.tx_prop = propname;

		clear_prop_conflict(&data, &e);
	}

	if (cpd.cpd_prop_type)
		free(cpd.cpd_prop_type);

	return (r);
}

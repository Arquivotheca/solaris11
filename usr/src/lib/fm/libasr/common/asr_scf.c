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

#include <libscf.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/nvpair.h>
#include <sys/time.h>
#include <sys/types.h>

#include "asr.h"
#include "asr_nvl.h"

#define	ASR_NAME_BUFSIZE	128
#define	MAX_TRIES		10

static void
asr_scf_error()
{
	(void) asr_error(
	    EASR_SCF, "SCF ERROR: %s\n", scf_strerror(scf_error()));
}

/*
 * Stores properties retrieved from SCF in a linked list
 */
typedef struct asr_scf_prop
{
	scf_transaction_entry_t *entry;
	scf_value_t *value;
	struct asr_scf_prop *next;
} asr_scf_prop_t;

/*
 * Creates an allocated property structure along with data for the
 * internal entry and value handles using the supplied SCF handle.
 */
static asr_scf_prop_t *
asr_scf_create_prop(scf_handle_t *scf_hdl)
{
	asr_scf_prop_t *prop;

	if ((prop = (asr_scf_prop_t *)malloc(
	    sizeof (asr_scf_prop_t))) == NULL) {
		(void) asr_set_errno(EASR_NOMEM);
		return (NULL);
	}
	bzero(prop, sizeof (asr_scf_prop_t));

	prop->entry = scf_entry_create(scf_hdl);
	if (prop->entry == NULL) {
		free(prop);
		return (NULL);
	}
	prop->value = scf_value_create(scf_hdl);
	if (prop->value == NULL) {
		free(prop);
		return (NULL);
	}
	return (prop);
}

/*
 * Frees a single SCF property item and returns the next item.
 */
static asr_scf_prop_t *
asr_scf_free_prop_item(asr_scf_prop_t *prop)
{
	asr_scf_prop_t *next;
	if (prop == NULL)
		return (NULL);
	next = prop->next;
	scf_value_destroy(prop->value);
	scf_entry_destroy(prop->entry);
	return (next);
}

/*
 * Frees the entire SCF property list including the SCF allocated internal
 * handles.
 */
static void
asr_scf_free_prop(asr_scf_prop_t *prop)
{
	while (prop != NULL) {
		prop = asr_scf_free_prop_item(prop);
	}
}

/*
 * Adds the name value pairs in the list as SCF properties.
 * NULL is returned if there is an error.  Otherwise the transaction is setup
 * and can be completed.  The returned properties that are part of the
 * transaction need to be released after the transaction is completed.
 */
static asr_scf_prop_t *
asr_addprops(scf_handle_t *scf_hdl, scf_transaction_t *tx,
    char *pgname, nvlist_t *list)
{
	asr_scf_prop_t *props = NULL;
	nvpair_t *nvp;
	int err = 0;

	int pgname_len = strlen(pgname);

	for (nvp = nvlist_next_nvpair(list, NULL);
	    nvp != NULL && err == 0;
	    nvp = nvlist_next_nvpair(list, nvp)) {
		data_type_t type = nvpair_type(nvp);
		char *name = nvpair_name(nvp);
		asr_scf_prop_t *prop = NULL;
		char *property;

		if (strncmp(pgname, name, pgname_len) != 0)
			continue;
		if (name[pgname_len] != '/')
			continue;

		property = name + pgname_len + 1;

		prop = asr_scf_create_prop(scf_hdl);
		if (prop == NULL) {
			err = -1;
			break;
		}

		switch (type) {
		case DATA_TYPE_STRING:
		{
			char *sval;
			(void) nvpair_value_string(nvp, &sval);

			err = scf_value_set_astring(prop->value, sval);
			if (err != 0)
				break;
			err = scf_transaction_property_change(
			    tx, prop->entry, property, SCF_TYPE_ASTRING);
			break;
		}
		case DATA_TYPE_INT64:
		{
			int64_t val;
			(void) nvpair_value_int64(nvp, &val);
			scf_value_set_integer(prop->value, val);
			err = scf_transaction_property_change(
			    tx, prop->entry, property, SCF_TYPE_INTEGER);
			break;
		}
		case DATA_TYPE_UINT64:
		{
			int64_t val;
			(void) nvpair_value_int64(nvp, &val);
			scf_value_set_count(prop->value, val);
			err = scf_transaction_property_change(
			    tx, prop->entry, property, SCF_TYPE_COUNT);
			break;
		}
		case DATA_TYPE_BOOLEAN_VALUE:
		{
			boolean_t val;
			(void) nvpair_value_boolean_value(nvp, &val);
			scf_value_set_boolean(prop->value, val);
			err = scf_transaction_property_change(
			    tx, prop->entry, property, SCF_TYPE_BOOLEAN);
			break;
		}
		case DATA_TYPE_HRTIME:
		{
			hrtime_t val;
			uint32_t nsec;
			int64_t time;

			(void) nvpair_value_hrtime(nvp, &val);
			nsec = val % 1000;
			time = val / 1000;
			if ((err = scf_value_set_time(
			    prop->value, time, nsec)) == 0)
				err = scf_transaction_property_change(
				    tx, prop->entry, property, SCF_TYPE_TIME);
			break;
		}
		default:
			err = -1;
		}

		if (err != 0) {
			asr_scf_free_prop(prop);
			break;
		} else {
			err = scf_entry_add_value(prop->entry, prop->value);
			prop->next = props;
			props = prop;
		}
	}

	if (err) {
		asr_scf_error();
		scf_transaction_reset(tx);
		asr_scf_free_prop(props);
		props = NULL;
	}
	return (props);

}

/*
 * Save the nvlist data to existing SCF properties.
 * Note: No new properties will be created and no old properties will
 * be destoryed.
 */
int
asr_scf_set_props(char *fmri, char *pgname, nvlist_t *data)
{
	int err;
	scf_handle_t *handle = NULL;
	scf_service_t *service = NULL;
	scf_propertygroup_t *pg = NULL;
	scf_transaction_t *tx = NULL;
	asr_scf_prop_t *props = NULL;

	if ((handle = scf_handle_create(SCF_VERSION)) == NULL)
		return (-1);

	service = scf_service_create(handle);

	if ((err = scf_handle_bind(handle)) == -1) {
		asr_scf_error();
		goto finally;
	}

	if ((err = scf_handle_decode_fmri(
	    handle, fmri, NULL, service, NULL, NULL, NULL, 0)) == -1) {
		asr_scf_error();
		goto finally;
	}

	if (NULL == (pg = scf_pg_create(handle)))
		goto finally;

	if ((err = scf_service_get_pg(service, pgname, pg)) == -1) {
		asr_scf_error();
		goto finally;
	}

	if ((tx = scf_transaction_create(handle)) == NULL)
		goto finally;

	if (scf_pg_update(pg) == -1) {
		asr_scf_error();
		goto finally;
	}
	if (scf_transaction_start(tx, pg) == -1) {
		asr_scf_error();
		goto finally;
	}

	props = asr_addprops(handle, tx, pgname, data);
	if (scf_transaction_commit(tx) != 1) {
		err = scf_error();
		asr_scf_error();
	}
	scf_transaction_reset(tx);

finally:
	asr_scf_free_prop(props);
	scf_transaction_destroy(tx);
	scf_pg_destroy(pg);
	scf_service_destroy(service);
	scf_handle_destroy(handle);
	return (err);
}


/*
 * Adds a single SCF property into an nvlist string property.
 */
static int
asr_scf_add(nvlist_t *nvl, const scf_simple_prop_t *prop, ssize_t num)
{
	char *pg = scf_simple_prop_pgname(prop);
	char *name = scf_simple_prop_name(prop);
	scf_type_t type = scf_simple_prop_type(prop);
	char pname[ASR_NAME_BUFSIZE];
	int err;

	if (pg == NULL || name == NULL)
		return (-1);

	if (num == 0)
		return (0);

	if (snprintf(pname, sizeof (pname), "%s/%s", pg, name) >=
	    sizeof (pname)) {
		return (-1);
	}

	switch (type) {

	case SCF_TYPE_BOOLEAN:
	{
		uint8_t *val = scf_simple_prop_next_boolean(
		    (scf_simple_prop_t *)prop);
		err = nvlist_add_boolean_value(nvl, pname, *val);
		break;
	}
	case SCF_TYPE_COUNT:
	{
		uint64_t *val = scf_simple_prop_next_count(
		    (scf_simple_prop_t *)prop);
		err = nvlist_add_uint64(nvl, pname, *val);
		break;
	}
	case SCF_TYPE_INTEGER:
	{
		int64_t *val = scf_simple_prop_next_integer(
		    (scf_simple_prop_t *)prop);
		err = nvlist_add_int64(nvl, pname, *val);
		break;
	}
	case SCF_TYPE_TIME:
	{
		int32_t nsec;
		int64_t *val = scf_simple_prop_next_time(
		    (scf_simple_prop_t *)prop, &nsec);
		hrtime_t time = *val;
		time = (time * 1000) + nsec;
		err = nvlist_add_hrtime(nvl, pname, time);
		break;
	}
	case SCF_TYPE_ASTRING:
	{
		char *val = scf_simple_prop_next_astring(
		    (scf_simple_prop_t *)prop);
		err = nvlist_add_string(nvl, pname, val);
		break;
	}
	case SCF_TYPE_OPAQUE:
	{
		err = -1;
		break;
	}
	default:
	{
		err = -1;
	}
	}

	return (err);
}

/*
 * Loads in ASR configuration name value pairs from the given service FMRI
 * Note: This library configuration doesn't support array values.
 */
int
asr_scf_load(char *fmri, nvlist_t *nvl)
{
	int err = 0;
	const scf_simple_prop_t *prop;
	const scf_simple_app_props_t *app;

	app = scf_simple_app_props_get(NULL, fmri);
	if (app == NULL) {
		asr_scf_error();
		return (ASR_FAILURE);
	}

	prop = scf_simple_app_props_next(app, NULL);
	while (prop) {
		ssize_t numv = scf_simple_prop_numvalues(prop);
		if (numv) {
			err = asr_scf_add(nvl, prop, numv);
			if (err) {
				asr_scf_error();
				break;
			}
		}
		prop = scf_simple_app_props_next(
		    app, (scf_simple_prop_t *)prop);
	}

	scf_simple_app_props_free((scf_simple_app_props_t *)app);
	return (err);
}

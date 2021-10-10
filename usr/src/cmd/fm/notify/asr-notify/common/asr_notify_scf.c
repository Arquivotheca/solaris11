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

#include "asr_notify.h"
#include "asr_notify_scf.h"

#define	PH_NAME_BUFSIZE	128
#define	MAX_TRIES 10


/*
 * This function returns a pointer to the specified SMF property group for the
 * specified SMF service.  The caller is responsible for freeing the property
 * group.  On failure, the function returns NULL.
 */
static scf_propertygroup_t *
ph_scf_get_pg(scf_handle_t *handle, scf_service_t *svc, const char *pgname)
{
	scf_propertygroup_t *pg = NULL, *ret = NULL;

	pg = scf_pg_create(handle);

	if (svc == NULL || pg == NULL) {
		scf_pg_destroy(pg);
		goto get_pg_done;
	}

	if (scf_service_get_pg(svc, pgname, pg) != -1)
		ret = pg;
	else
		scf_pg_destroy(pg);

get_pg_done:
	return (ret);
}

static int
ph_scf_trans_prop(scf_propertygroup_t *pg, char *pgname, const char *pname,
	scf_property_t *prop,
	scf_value_t *value,
	scf_transaction_entry_t *ent,
	scf_transaction_t *tx)
{
	if (scf_pg_get_property(pg, pname, prop) == SCF_SUCCESS) {
		if (scf_transaction_property_change_type(tx, ent, pname,
		    scf_value_type(value)) < 0) {
			(void) fprintf(stderr, "SCF prop change ERROR"
			    "%s/%s (%s)\n", pgname, pname,
			    scf_strerror(scf_error()));
			return (PH_FAILURE);
		}
	} else if (scf_error() == SCF_ERROR_NOT_FOUND ||
	    scf_error() == SCF_ERROR_DELETED) {
		if (scf_transaction_property_new(tx, ent, pname,
		    scf_value_type(value)) < 0) {
			(void) fprintf(stderr, "SCF new prop ERROR"
			    "%s/%s (%s)\n", pgname, pname,
			    scf_strerror(scf_error()));
			return (PH_FAILURE);
		}
	} else {
		(void) fprintf(stderr, "SCF prop change ERROR"
		    "%s/%s (%s)\n", pgname, pname,
		    scf_strerror(scf_error()));
			return (PH_FAILURE);
	}

	if (scf_entry_add_value(ent, value) == -1) {
		(void) fprintf(stderr, "SCF add value %s/%s ERROR "
		    "(%s)\n", pgname, pname,
		    scf_strerror(scf_error()));
		return (PH_FAILURE);
	}
	return (PH_OK);
}

/*
 * Sets a single SCF property.  If the property exists it will be
 * modified and if it doesn't exist then it will be created.
 * The setfunc callback is responsible for converting the data
 * into the scf_value_t used to set the property.
 * Returns non-zero if there is an error
 */
static int
ph_scf_set_prop(const char *fmri, char *pgname, const char *pname,
		int (*setfunc)(scf_value_t *, const void *), const void *data)
{
	int result = PH_FAILURE;
	int i;
	scf_handle_t *handle = NULL;
	scf_service_t *service = NULL;
	scf_propertygroup_t *pg = NULL;
	scf_transaction_t *tx = NULL;
	scf_property_t *prop = NULL;
	scf_transaction_entry_t *ent = NULL;
	scf_value_t *value = NULL;

	if ((handle = scf_handle_create(SCF_VERSION)) == NULL)
		return (PH_FAILURE);

	if (scf_handle_bind(handle) == -1) {
		(void) fprintf(stderr, "Failed to bind SCF handle (%s)\n",
		    scf_strerror(scf_error()));
		goto finally;
	}
	if ((service = scf_service_create(handle)) == NULL) {
		(void) fprintf(stderr, "Failed to create service handle (%s)\n",
		    scf_strerror(scf_error()));
		goto finally;
	}

	if (scf_handle_decode_fmri(
	    handle, fmri, NULL, service, NULL, NULL, NULL, 0) == -1) {
		(void) fprintf(stderr, "Error decoding FMRI %s (%s)\n", fmri,
		    scf_strerror(scf_error()));
		goto finally;
	}

	if ((pg = ph_scf_get_pg(handle, service, pgname)) == NULL) {
		(void) fprintf(stderr, "Error getting property group %s/%s "
		    "(%s)\n", pgname, pname,
		    scf_strerror(scf_error()));
		goto finally;
	}

	if ((ent = scf_entry_create(handle)) == NULL)
		goto finally;
	if (data != NULL) {
		if ((prop = scf_property_create(handle)) == NULL)
			goto finally;
		if ((value = scf_value_create(handle)) == NULL)
			goto finally;
		if (setfunc(value, data) != 0)
			goto finally;
	} else {
		/* Check if property was already deleted. */
		if (scf_pg_get_property(pg, pname, prop) != SCF_SUCCESS &&
		    (scf_error() == SCF_ERROR_NOT_FOUND ||
		    scf_error() == SCF_ERROR_DELETED)) {
			result = PH_OK;
			goto finally;
		}
	}

	if ((tx = scf_transaction_create(handle)) == NULL)
		goto finally;

	for (i = 0; i < MAX_TRIES; i++) {
		int ret;
		if (scf_pg_update(pg) == -1) {
			(void) fprintf(stderr, "Error updating property group:"
			    "%s/%s (%s)\n", pgname, pname,
			    scf_strerror(scf_error()));
			break;
		}
		if (scf_transaction_start(tx, pg) == -1) {
			(void) fprintf(stderr, "Error starting SCF transaction"
			    "%s/%s (%s)\n", pgname, pname,
			    scf_strerror(scf_error()));
			break;
		}

		if (data == NULL) {
			if (scf_transaction_property_delete(
			    tx, ent, pname) == -1) {
				(void) fprintf(stderr,
				    "failed to remove property %s/%s (%s)\n",
				    pgname, pname, scf_strerror(scf_error()));
				break;
			}
		} else if (ph_scf_trans_prop(
		    pg, pgname, pname, prop, value, ent, tx) != 0) {
			break;
		}

		ret = scf_transaction_commit(tx);

		if (ret == 1) {
			result = PH_OK;
			break;
		}
		if (ret == 0) {
			scf_transaction_reset(tx);
			continue;
		}

		(void) fprintf(stderr, "SCF ERROR: %s/%s (%s)\n",
		    pgname, pname,
		    scf_strerror(scf_error()));
		break;
	}

finally:
	scf_property_destroy(prop);
	scf_value_destroy(value);
	scf_transaction_destroy(tx);
	scf_pg_destroy(pg);
	scf_entry_destroy(ent);
	scf_service_destroy(service);
	scf_handle_destroy(handle);

	return (result);
}

/*
 * Copies the program group name from the property to pgname.
 * So if property = "asr/transport" then pgname will contain "asr"
 * returns a pointer to the property;
 */
static const char *
ph_scf_property_pgname(const char *property, char *pgname, size_t n)
{
	int sep;
	char *pname = strstr(property, "/");

	if (pname == NULL) {
		(void) strncpy(pgname, "asr", n);
		return (property);
	}

	sep = pname - property;
	if (sep < n)
		n = sep + 1;
	pgname[n - 1] = '\0';
	(void) strncpy(pgname, property, n - 1);

	return (pname + 1);
}

static int
ph_scf_set_string_cb(scf_value_t *value, const void *data)
{
	return (scf_value_set_astring(value, (char *)data));
}

/*
 * Sets the string value for a service property.
 */
int
ph_scf_set_string(const char *fmri, const char *property, const char *value)
{
	int err;
	char pgname[32];
	const char *pname = ph_scf_property_pgname(
	    property, pgname, sizeof (pgname));

	err = ph_scf_set_prop(fmri, pgname, pname, ph_scf_set_string_cb, value);
	return (err);
}

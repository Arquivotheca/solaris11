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
 * This file contains functions that read/write from/to SCF.
 */

#include <strings.h>
#include "lldp_impl.h"

static void
lldpd_drop_composed(lldp_scf_state_t *sstate)
{
	if (sstate->lss_trans != NULL) {
		scf_transaction_destroy(sstate->lss_trans);
		sstate->lss_trans = NULL;
	}
	if (sstate->lss_tent != NULL) {
		scf_entry_destroy(sstate->lss_tent);
		sstate->lss_tent = NULL;
	}
	scf_property_destroy(sstate->lss_prop);
	sstate->lss_prop = NULL;
	scf_pg_destroy(sstate->lss_pg);
	sstate->lss_pg = NULL;
}

static int
lldpd_get_composed_properties(const char *lpg, lldp_scf_state_t *sstate)
{
	int	err;

	sstate->lss_pg = NULL;
	sstate->lss_prop = NULL;

	/* set up for a series of scf calls */
	err = -1;
	if ((sstate->lss_pg = scf_pg_create(sstate->lss_handle)) == NULL ||
	    (sstate->lss_prop = scf_property_create(sstate->lss_handle)) ==
	    NULL) {
		return (err);
	}
	if ((err = scf_instance_get_pg_composed(sstate->lss_inst, NULL, lpg,
	    sstate->lss_pg)) != 0) {
		if (scf_error() == SCF_ERROR_NOT_FOUND)
			err = ENOENT;
	}
	return (err);
}

static void
lldpd_shutdown_scf(lldp_scf_state_t *sstate)
{
	scf_instance_destroy(sstate->lss_inst);
	(void) scf_handle_unbind(sstate->lss_handle);
	scf_handle_destroy(sstate->lss_handle);
}

/*
 * Start up SCF and bind the requested instance alone.
 */
static int
lldpd_bind_instance(const char *service, const char *instance_name,
    lldp_scf_state_t *sstate)
{
	char *fmri = NULL;

	bzero(sstate, sizeof (*sstate));
	if ((sstate->lss_handle = scf_handle_create(SCF_VERSION)) == NULL)
		return (-1);

	if (scf_handle_bind(sstate->lss_handle) != 0) {
		scf_handle_destroy(sstate->lss_handle);
		return (-1);
	}
	sstate->lss_inst = scf_instance_create(sstate->lss_handle);
	if (sstate->lss_inst == NULL)
		goto fail;

	fmri = lldp_alloc_fmri(service, instance_name);

	if (scf_handle_decode_fmri(sstate->lss_handle, fmri, NULL, NULL,
	    sstate->lss_inst, NULL, NULL,
	    SCF_DECODE_FMRI_REQUIRE_INSTANCE) != 0)
		goto fail;
	free(fmri);
	return (0);

fail:
	free(fmri);
	lldpd_shutdown_scf(sstate);
	return (-1);
}

static int
lldpd_write2scf(lldp_scf_state_t *sstate, const char *pgname, const char *pname,
    void *pval, scf_type_t ptype, uint_t flags)
{
	scf_value_t	*value = NULL;
	boolean_t	new_pg = B_FALSE;
	int		rv, err;

	/* set up for a series of scf calls */
	err = -1;
	sstate->lss_trans = scf_transaction_create(sstate->lss_handle);
	if (sstate->lss_trans == NULL)
		goto out;
	if ((sstate->lss_tent = scf_entry_create(sstate->lss_handle)) == NULL)
		goto out;
	if ((sstate->lss_pg = scf_pg_create(sstate->lss_handle)) == NULL)
		goto out;

	if (!(flags & LLDP_OPT_DEFAULT) &&
	    scf_instance_add_pg(sstate->lss_inst, pgname, SCF_GROUP_APPLICATION,
	    0, sstate->lss_pg) == 0) {
		new_pg = B_TRUE;
	} else if (scf_instance_get_pg(sstate->lss_inst, pgname,
	    sstate->lss_pg) != 0) {
		/*
		 * if its a reset operation and we didn't find the property
		 * group then there is nothing to delete, return Success
		 */
		if ((flags & LLDP_OPT_DEFAULT) &&
		    scf_error() == SCF_ERROR_NOT_FOUND) {
			err = 0;
		}
		goto out;
	}
	if (!(flags & LLDP_OPT_DEFAULT)) {
		value = scf_value_create(sstate->lss_handle);
		if (value == NULL)
			goto out;
		switch (ptype) {
		case SCF_TYPE_INTEGER:
			scf_value_set_integer(value, *(int64_t *)pval);
			break;
		case SCF_TYPE_ASTRING:
			if (scf_value_set_astring(value, (char *)pval) != 0)
				goto out;
			break;
		case SCF_TYPE_BOOLEAN:
			scf_value_set_boolean(value, *(uint8_t *)pval);
			break;
		default:
			assert(0);
			break;
		}
	}
	do {
		if (scf_transaction_start(sstate->lss_trans,
		    sstate->lss_pg) != 0)
			goto out;
		/* reset operation, we have to delete the persistent value */
		if (flags & LLDP_OPT_DEFAULT) {
			if (scf_transaction_property_delete(sstate->lss_trans,
			    sstate->lss_tent, pname) != 0) {
				/*
				 * if the property is already deleted,
				 * ignore the error.
				 */
				if (scf_error() == SCF_ERROR_NOT_FOUND)
					err = 0;
				goto out;
			}
		} else {
			if (scf_transaction_property_new(sstate->lss_trans,
			    sstate->lss_tent, pname, ptype) != 0 &&
			    scf_transaction_property_change(sstate->lss_trans,
			    sstate->lss_tent, pname, ptype) != 0) {
				goto out;
			}

			if (scf_entry_add_value(sstate->lss_tent, value) != 0)
				goto out;
		}
		rv = scf_transaction_commit(sstate->lss_trans);
		scf_transaction_reset(sstate->lss_trans);
		if (rv == 0 && scf_pg_update(sstate->lss_pg) == -1)
			goto out;
	} while (rv == 0);
	if (rv != 1)
		goto out;
	err = 0;
out:
	if (err != 0 && new_pg)
		(void) scf_pg_delete(sstate->lss_pg);
	if (value != NULL)
		scf_value_destroy(value);
	lldpd_drop_composed(sstate);
	return (err);
}

static int
lldpd_persist_value2scf(const char *pgname, const char *pname, scf_type_t ptype,
    void *val, uint_t flags)
{
	lldp_scf_state_t sstate;
	int		 err;

	if ((err = lldpd_bind_instance(LLDP_SVC_NAME, LLDP_SVC_DEFAULT_INSTANCE,
	    &sstate)) != 0) {
		return (err);
	}
	err = lldpd_write2scf(&sstate, pgname, pname, val, ptype, flags);
	lldpd_shutdown_scf(&sstate);
	return (err);
}

static int
i_lldpd_add_config2nvl(const char *pname, scf_value_t *val, nvlist_t *nvl)
{
	int64_t		i64;
	uint64_t	u64;
	char		pval[LLDP_MAXPROPVALLEN];
	uint8_t		u8;

	switch (scf_value_type(val)) {
	case SCF_TYPE_INTEGER:
		if (scf_value_get_integer(val, &i64) != 0 ||
		    nvlist_add_int64(nvl, pname, i64) != 0)
			return (-1);
		break;
	case SCF_TYPE_COUNT:
		if (scf_value_get_count(val, &u64) != 0 ||
		    nvlist_add_uint64(nvl, pname, u64) != 0)
			return (-1);
		break;
	case SCF_TYPE_ASTRING:
		if (scf_value_get_astring(val, pval, sizeof (pval)) == -1 ||
		    nvlist_add_string(nvl, pname, pval) != 0)
			return (-1);
		break;
	case SCF_TYPE_BOOLEAN:
		if (scf_value_get_boolean(val, &u8) != 0 ||
		    nvlist_add_boolean_value(nvl, pname,
		    (u8 == 1 ? B_TRUE : B_FALSE)) != 0)
			return (-1);
		break;
	}
	return (0);
}

int
lldpd_walk_db(nvlist_t *config, const char *cfgname)
{
	lldp_scf_state_t	sstate;
	scf_iter_t		*pg_iter = NULL, *prop_iter = NULL;
	scf_propertygroup_t	*pg = NULL;
	scf_property_t		*prop = NULL;
	scf_value_t		*val = NULL;
	nvlist_t		*nvl = NULL;
	char			pgname[LLDP_STRSIZE];
	char			pname[LLDP_STRSIZE];
	char			*pclassname, *objname, *sub_objname;
	int			err = 0;

	if (lldpd_bind_instance(LLDP_SVC_NAME, LLDP_SVC_DEFAULT_INSTANCE,
	    &sstate) != 0) {
		syslog(LOG_ERR, "Failed to create SCF resources");
		return (-1);
	}
	if ((pg = scf_pg_create(sstate.lss_handle)) == NULL ||
	    (pg_iter = scf_iter_create(sstate.lss_handle)) == NULL ||
	    (prop = scf_property_create(sstate.lss_handle)) == NULL ||
	    (prop_iter = scf_iter_create(sstate.lss_handle)) == NULL ||
	    (val = scf_value_create(sstate.lss_handle)) == NULL ||
	    scf_iter_instance_pgs_typed_composed(pg_iter, sstate.lss_inst,
	    NULL, SCF_GROUP_APPLICATION) == -1) {
		syslog(LOG_ERR, "Failed to create SCF resources");
		err = -1;
		goto out;
	}
	while (scf_iter_next_pg(pg_iter, pg) > 0) {
		if (scf_pg_get_name(pg, pgname, LLDP_STRSIZE) == -1)
			continue;
		if (scf_iter_pg_properties(prop_iter, pg) != 0) {
			syslog(LOG_ERR, "failed to iterate properites of '%s'",
			    pgname);
			continue;
		}
		/*
		 * `pgname' is of the form:
		 *	<propClass[_{laname|tlvname}_{tlvname}]>
		 * For example:
		 *	<lldp>	- contains the protocol properies.
		 *	<globaltlv_<tlvname>> - containsl global tlv properties.
		 *	<agent_<laname>> - contains agent properties.
		 *	<agenttlv_<laname>_<tlvname>> - contains per-agent TLV
		 *		properties.
		 */
		if (cfgname != NULL && strcmp(cfgname, pgname) != 0)
			continue;
		if ((pclassname = strdup(pgname)) == NULL)
			goto out;
		sub_objname = NULL;
		if ((objname = strchr(pclassname, '_')) != NULL) {
			*objname++ = '\0';
			if ((sub_objname = strchr(objname, '_')) != NULL)
				*sub_objname++ = '\0';
		}
		err = lldp_create_nested_nvl(config, pclassname, objname,
		    sub_objname, &nvl);
		free(pclassname);
		if (err != 0)
			goto out;
		while (scf_iter_next_property(prop_iter, prop) > 0) {
			if (scf_property_get_name(prop, pname,
			    LLDP_STRSIZE) == -1)
				continue;
			if (scf_property_get_value(prop, val) != 0 ||
			    i_lldpd_add_config2nvl(pname, val, nvl) != 0) {
				syslog(LOG_ERR, "failed to retrieve value of "
				    "'%s'", pname);
			}
		}
		scf_iter_reset(prop_iter);
	}
out:
	if (prop_iter != NULL)
		scf_iter_destroy(prop_iter);
	if (pg_iter != NULL)
		scf_iter_destroy(pg_iter);
	if (val != NULL)
		scf_value_destroy(val);
	if (prop != NULL)
		scf_property_destroy(prop);
	if (pg != NULL)
		scf_pg_destroy(pg);
	lldpd_shutdown_scf(&sstate);
	return (err);
}

static int
i_lldpd_purge_pg(const char *pgname)
{
	lldp_scf_state_t	sstate;
	int			err;

	/* delete the property group */
	if ((err = lldpd_bind_instance(LLDP_SVC_NAME, LLDP_SVC_DEFAULT_INSTANCE,
	    &sstate)) != 0) {
		return (-1);
	}

	if ((err = lldpd_get_composed_properties(pgname, &sstate)) != 0) {
		if (err == ENOENT)
			err = 0;
	} else {
		err = scf_pg_delete(sstate.lss_pg);
	}
	lldpd_drop_composed(&sstate);
	lldpd_shutdown_scf(&sstate);
	return (err);
}

/*
 * Persist the value in SCF. If persistence fails, we will report an error
 * to the user saying persistence failed. However, the settings would have
 * applied on the active configuration.
 */
int
lldpd_persist_prop(lldp_propclass_t pclass, lldp_proptype_t ptype,
    const char *linkname, void *pval, data_type_t dtype, uint32_t flags)
{
	scf_type_t	stype;
	char		pgname[LLDP_STRSIZE], *pname;
	int64_t		i64;
	uint8_t		u8;
	void		*val;

	if (pclass & LLDP_PROPCLASS_AGENT) {
		(void) snprintf(pgname, sizeof (pgname), "agent_%s", linkname);
	} else if (pclass & LLDP_PROPCLASS_GLOBAL_TLVS) {
		(void) snprintf(pgname, sizeof (pgname), "globaltlv_%s",
		    lldp_pclass2tlvname(pclass));
	} else if (pclass & LLDP_PROPCLASS_AGENT_TLVS) {
		(void) snprintf(pgname, sizeof (pgname), "agenttlv_%s_%s",
		    linkname, lldp_pclass2tlvname(pclass));
	} else {
		assert(0);
	}

	/*
	 * SCF_TYPE_INTEGER is of type int64_t.
	 * SCF_TYPE_BOOLEAN is of type uint8_t
	 */
	switch (dtype) {
	case DATA_TYPE_UINT16:
		stype = SCF_TYPE_INTEGER;
		i64 = *(uint16_t *)pval;
		val = &i64;
		break;
	case DATA_TYPE_UINT32:
		stype = SCF_TYPE_INTEGER;
		i64 = *(uint32_t *)pval;
		val = &i64;
		break;
	case DATA_TYPE_BOOLEAN_VALUE:
		stype = SCF_TYPE_BOOLEAN;
		u8 = ((*(boolean_t *)pval) ? 1 : 0);
		val = &u8;
		break;
	case DATA_TYPE_STRING:
		stype = SCF_TYPE_ASTRING;
		val = pval;
		break;
	default:
		assert(0);
		break;
	}
	pname = lldp_ptype2pname(ptype);
	assert(pname != NULL);

	/* ENODATA would signify an error with persistence */
	/* handle some special cases */
	if (ptype == LLDP_PROPTYPE_MODE) {
		if (*(uint32_t *)pval == LLDP_MODE_DISABLE) {
			/* purge all the information for this agent */
			if (i_lldpd_purge_pg(pgname) != 0)
				return (ENODATA);
			/* purge any DCB feature specific information */
			(void) snprintf(pgname, sizeof (pgname),
			    "agenttlv_%s_%s", linkname,
			    LLDP_8021_APPLN_TLVNAME);
			if (i_lldpd_purge_pg(pgname) != 0)
				return (ENODATA);
			return (0);
		}
	}

	if (lldpd_persist_value2scf(pgname, pname, stype, val, flags) != 0)
		return (ENODATA);
	return (0);
}

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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains high level functions used by multiple utilities.
 */

#include "libscf_impl.h"

#include <assert.h>
#include <libuutil.h>
#include <string.h>
#include <stdlib.h>
#include <sys/systeminfo.h>
#include <sys/uadmin.h>
#include <sys/utsname.h>

#ifdef	__x86
#include <smbios.h>

/*
 * Check whether the platform is on the fastreboot_blacklist.
 * Return 1 if the platform has been blacklisted, 0 otherwise.
 */
static int
scf_is_fb_blacklisted(void)
{
	smbios_hdl_t *shp;
	smbios_system_t sys;
	smbios_info_t info;

	id_t id;
	int err;
	int i;

	scf_simple_prop_t *prop = NULL;
	ssize_t numvals;
	char *platform_name;

	int blacklisted = 0;

	/*
	 * If there's no SMBIOS, assume it's blacklisted.
	 */
	if ((shp = smbios_open(NULL, SMB_VERSION, 0, &err)) == NULL)
		return (1);

	/*
	 * If we can't read system info, assume it's blacklisted.
	 */
	if ((id = smbios_info_system(shp, &sys)) == SMB_ERR ||
	    smbios_info_common(shp, id, &info) == SMB_ERR) {
		blacklisted = 1;
		goto fb_out;
	}

	/*
	 * If we can't read the "platforms" property from property group
	 * BOOT_CONFIG_PG_FBBLACKLIST, assume no platforms have
	 * been blacklisted.
	 */
	if ((prop = scf_simple_prop_get(NULL, FMRI_BOOT_CONFIG,
	    BOOT_CONFIG_PG_FBBLACKLIST, "platforms")) == NULL)
		goto fb_out;

	numvals = scf_simple_prop_numvalues(prop);

	for (i = 0; i < numvals; i++) {
		platform_name = scf_simple_prop_next_astring(prop);
		if (platform_name == NULL)
			break;
		if (strcmp(platform_name, info.smbi_product) == 0) {
			blacklisted = 1;
			break;
		}
	}

fb_out:
	smbios_close(shp);
	scf_simple_prop_free(prop);

	return (blacklisted);
}

/*
 * Add or get a property group given an FMRI.
 * Return SCF_SUCCESS on success, SCF_FAILED on failure.
 */
static int
scf_fmri_pg_get_or_add(const char *fmri, const char *pgname,
    const char *pgtype, uint32_t pgflags, int add)
{
	scf_handle_t	*handle = NULL;
	scf_instance_t	*inst = NULL;
	int		rc = SCF_FAILED;
	int		error;

	if ((handle = scf_handle_create(SCF_VERSION)) == NULL ||
	    scf_handle_bind(handle) != 0 ||
	    (inst = scf_instance_create(handle)) == NULL ||
	    scf_handle_decode_fmri(handle, fmri, NULL, NULL,
	    inst, NULL, NULL, SCF_DECODE_FMRI_EXACT) != SCF_SUCCESS)
		goto scferror;

	if (add) {
		rc = scf_instance_add_pg(inst, pgname, pgtype, pgflags, NULL);
		/*
		 * If the property group already exists, return SCF_SUCCESS.
		 */
		if (rc != SCF_SUCCESS && scf_error() == SCF_ERROR_EXISTS)
			rc = SCF_SUCCESS;
	} else {
		rc = scf_instance_get_pg(inst, pgname, NULL);
	}

scferror:
	if (rc != SCF_SUCCESS)
		error = scf_error();

	scf_instance_destroy(inst);
	if (handle)
		(void) scf_handle_unbind(handle);
	scf_handle_destroy(handle);

	if (rc != SCF_SUCCESS)
		(void) scf_set_error(error);

	return (rc);
}
#endif	/* __x86 */

/*
 * Get config properties from svc:/system/boot-config:default.
 * It prints errors with uu_warn().
 */
void
scf_get_boot_config(uint8_t *boot_config)
{
	assert(boot_config);
	*boot_config = 0;

	{
		/*
		 * Property vector for BOOT_CONFIG_PG_PARAMS property group.
		 */
		scf_propvec_t ua_boot_config[] = {
			{ FASTREBOOT_DEFAULT, NULL, SCF_TYPE_BOOLEAN, B_FALSE,
			    NULL, UA_FASTREBOOT_DEFAULT },
			{ FASTREBOOT_ONPANIC, NULL, SCF_TYPE_BOOLEAN, B_FALSE,
			    NULL, UA_FASTREBOOT_ONPANIC },
			{ NULL }
		};
		scf_propvec_t	*prop;
		uint64_t prop64_val = 0;

		for (prop = ua_boot_config; prop->pv_prop != NULL; prop++)
			prop->pv_ptr = &prop64_val;

		prop = NULL;
		if (scf_read_propvec(FMRI_BOOT_CONFIG, BOOT_CONFIG_PG_PARAMS,
		    B_TRUE, ua_boot_config, &prop) != SCF_FAILED) {

			assert((prop64_val & ~0xFFULL) == 0);
			*boot_config = (uint8_t)prop64_val;
#ifdef	__x86
			/*
			 * Unset both flags if the platform has been
			 * blacklisted.
			 */
			if (scf_is_fb_blacklisted())
				*boot_config &= ~(UA_FASTREBOOT_DEFAULT |
				    UA_FASTREBOOT_ONPANIC);
#endif	/* __x86 */
			return;
		}

		assert((prop64_val & ~0xFFULL) == 0);
		*boot_config = (uint8_t)prop64_val;
#if defined(FASTREBOOT_DEBUG)
		if (prop != NULL) {
			(void) uu_warn("Service %s property '%s/%s' "
			    "not found.\n", FMRI_BOOT_CONFIG,
			    BOOT_CONFIG_PG_PARAMS, prop->pv_prop);
		} else {
			(void) uu_warn("Unable to read service %s "
			    "property '%s': %s\n", FMRI_BOOT_CONFIG,
			    BOOT_CONFIG_PG_PARAMS, scf_strerror(scf_error()));
		}
#endif	/* FASTREBOOT_DEBUG */
	}
}

/*
 * Get or set properties in non-persistent "config_ovr" property group
 * in svc:/system/boot-config:default.
 * It prints errors with uu_warn().
 */
/*ARGSUSED*/
static int
scf_getset_boot_config_ovr(int set, uint8_t *boot_config_ovr)
{
	int rc = SCF_SUCCESS;

	assert(boot_config_ovr);

#ifndef	__x86
	return (rc);
#else
	{
		/*
		 * Property vector for BOOT_CONFIG_PG_OVR property group.
		 */
		scf_propvec_t ua_boot_config_ovr[] = {
			{ FASTREBOOT_DEFAULT, NULL, SCF_TYPE_BOOLEAN, B_FALSE,
			    NULL, UA_FASTREBOOT_DEFAULT },
			{ FASTREBOOT_ONPANIC, NULL, SCF_TYPE_BOOLEAN, B_FALSE,
			    NULL, UA_FASTREBOOT_ONPANIC },
			{ NULL }
		};
		scf_propvec_t	*prop;
		uint64_t prop64_val = *boot_config_ovr;

		rc = scf_fmri_pg_get_or_add(FMRI_BOOT_CONFIG,
		    BOOT_CONFIG_PG_OVR, SCF_GROUP_APPLICATION,
		    SCF_PG_FLAG_NONPERSISTENT, set);

		if (rc != SCF_SUCCESS) {
#if defined(FASTREBOOT_DEBUG)
			if (set)
				(void) uu_warn("Unable to add service %s "
				    "property group '%s'\n",
				    FMRI_BOOT_CONFIG, BOOT_CONFIG_PG_OVR);
#endif	/* FASTREBOOT_DEBUG */
			return (rc);
		}

		for (prop = ua_boot_config_ovr; prop->pv_prop != NULL; prop++)
			prop->pv_ptr = &prop64_val;
		prop = NULL;

		if (set)
			rc = scf_write_propvec(FMRI_BOOT_CONFIG,
			    BOOT_CONFIG_PG_OVR, ua_boot_config_ovr, &prop);
		else
			rc = scf_read_propvec(FMRI_BOOT_CONFIG,
			    BOOT_CONFIG_PG_OVR, B_FALSE, ua_boot_config_ovr,
			    &prop);

		assert((prop64_val & ~0xFFULL) == 0);
		*boot_config_ovr = (uint8_t)prop64_val;

#if defined(FASTREBOOT_DEBUG)
		if (rc != SCF_SUCCESS) {
			if (prop != NULL) {
				(void) uu_warn("Service %s property '%s/%s' "
				    "not found.\n", FMRI_BOOT_CONFIG,
				    BOOT_CONFIG_PG_OVR, prop->pv_prop);
			} else {
				(void) uu_warn("Unable to %s service %s "
				    "property '%s': %s\n", set ? "set" : "get",
				    FMRI_BOOT_CONFIG, BOOT_CONFIG_PG_OVR,
				    scf_strerror(scf_error()));
			}
		}
#endif	/* FASTREBOOT_DEBUG */

		if (set)
			(void) smf_refresh_instance(FMRI_BOOT_CONFIG);

		return (rc);

	}
#endif	/* __x86 */
}

/*
 * Get values of properties in non-persistent "config_ovr" property group.
 */
void
scf_get_boot_config_ovr(uint8_t *boot_config_ovr)
{
	(void) scf_getset_boot_config_ovr(B_FALSE, boot_config_ovr);
}

/*
 * Set value of "config_ovr/fastreboot_default".
 */
int
scf_fastreboot_default_set_transient(boolean_t value)
{
	uint8_t	boot_config_ovr = 0;

	if (value == B_TRUE)
		boot_config_ovr = UA_FASTREBOOT_DEFAULT | UA_FASTREBOOT_ONPANIC;

	return (scf_getset_boot_config_ovr(B_TRUE, &boot_config_ovr));
}

/*
 * Check whether Fast Reboot is the default operating mode.
 * Return 0 if
 *   1. the platform is xVM
 * or
 *   2. svc:/system/boot-config:default service doesn't exist,
 * or
 *   3. property "config/fastreboot_default" doesn't exist,
 * or
 *   4. value of property "config/fastreboot_default" is set to "false"
 *      and "config_ovr/fastreboot_default" is not set to "true",
 * or
 *   5. the platform has been blacklisted.
 * or
 *   6. value of property "config_ovr/fastreboot_default" is set to "false".
 * Return non-zero otherwise.
 */
int
scf_is_fastboot_default(void)
{
	uint8_t	boot_config = 0, boot_config_ovr;
	char procbuf[SYS_NMLN];

	/*
	 * If we are on xVM, do not fast reboot by default.
	 */
	if (sysinfo(SI_PLATFORM, procbuf, sizeof (procbuf)) == -1 ||
	    strcmp(procbuf, "i86xpv") == 0)
		return (0);

	/*
	 * Get property values from "config" property group
	 */
	scf_get_boot_config(&boot_config);

	/*
	 * Get property values from non-persistent "config_ovr" property group
	 */
	boot_config_ovr = boot_config;
	scf_get_boot_config_ovr(&boot_config_ovr);

	return (boot_config_ovr & UA_FASTREBOOT_DEFAULT);
}

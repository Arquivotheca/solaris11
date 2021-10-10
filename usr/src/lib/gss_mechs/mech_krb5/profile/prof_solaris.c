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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * prof_solaris.c:
 * Abstracted contract private interfaces for configuring krb5.conf(4).
 */

#include <ctype.h>
#include <locale.h>
#include "prof_int.h"
#include "k5-int.h"

/*
 * key:
 *	If key is NULL then all entries in the section specified are returned.
 * case_ins:
 *      If set a case insensitive lookup is performed and if found then only one
 *	    entry is returned which only differs by case.
 *      If unset a case sensitive lookup is performed and all entries matching
 *	    the lookup are returned.
 */
errcode_t
k5_profile_iter_name_value(profile_t profile, char *section, char *key,
	char ***retvals, boolean_t case_ins)
{
	const char	*hierarchy[4];
	errcode_t	code, code2;
	char		*name = NULL, *value = NULL, **ret_values = NULL;
	void		*state = NULL;
	struct profile_string_list values;
	boolean_t	found = B_FALSE;

	hierarchy[0] = section;
	hierarchy[1] = NULL;

	if (code = init_list(&values))
		return (code);

	code = profile_iterator_create(profile, hierarchy,
	    PROFILE_ITER_LIST_SECTION, &state);
	while (code == 0) {
		code = profile_iterator(&state, &name, &value);
		if (code == 0 && name != NULL) {

			if (key != NULL && value != NULL) {
				boolean_t ex_match = strcmp(key, value) ?
				    B_FALSE : B_TRUE;
				boolean_t match = strcasecmp(key, value) ?
				    B_FALSE : B_TRUE;

				if (ex_match == 0 && case_ins == B_TRUE &&
				    match == 1) {
					code2 = add_to_list(&values, name);
					if (code2 != 0) {
						end_list(&values, &ret_values);
						profile_free_list(ret_values);
						code = code2;
					}
					end_list(&values, &ret_values);
					goto cleanup;
				} else if (ex_match == 0 || case_ins == B_TRUE)
					goto not_found;
			}
			code2 = add_to_list(&values, name);
			if (code2 != 0) {
				end_list(&values, &ret_values);
				profile_free_list(ret_values);
				code2 = code;
				goto cleanup;
			}
			found = B_TRUE;
		}

not_found:
		if (name != NULL) {
			profile_release_string(name);
			name = NULL;
		}
		if (value != NULL) {
			profile_release_string(value);
			value = NULL;
		}
	}
	code = 0;
	if (found == B_TRUE)
		end_list(&values, &ret_values);

cleanup:
	if (state != NULL)
		profile_iterator_free(&state);
	if (name != NULL)
		profile_release_string(name);
	if (value != NULL)
		profile_release_string(value);

	*retvals = ret_values;

	return (code);
}

errcode_t
k5_profile_get_domain_realm(profile_t profile, char *realm, char ***domains,
    boolean_t case_ins)
{
	if (profile == NULL || realm == NULL || domains == NULL)
		return (EINVAL);

	return (k5_profile_iter_name_value(profile, "domain_realm", realm,
	    domains, case_ins));
}

errcode_t
k5_profile_set_appdefaults(profile_t profile)
{
	const char	*hierarchy[4];
	errcode_t	code;

	if (profile == NULL)
		return (EINVAL);

	hierarchy[0] = "appdefaults";
	hierarchy[1] = "kinit";
	hierarchy[3] = NULL;

	hierarchy[2] = "renewable";

	/*
	 * Not fatal if this fails, continue on.
	 */
	(void) profile_clear_relation(profile, hierarchy);

	code = profile_add_relation(profile, hierarchy, "true");
	if (code != 0)
		return (code);

	hierarchy[2] = "forwardable";

	(void) profile_clear_relation(profile, hierarchy);

	code = profile_add_relation(profile, hierarchy, "true");

	return (code);
}

errcode_t
k5_profile_set_logging(profile_t profile)
{
	const char	*hierarchy[4];
	errcode_t	code;

	if (profile == NULL)
		return (EINVAL);

	hierarchy[0] = "logging";
	hierarchy[2] = NULL;
	hierarchy[3] = NULL;

	hierarchy[1] = "default";

	/*
	 * Not fatal if this fails, continue on.
	 */
	(void) profile_clear_relation(profile, hierarchy);

	code = profile_add_relation(profile, hierarchy,
	    "FILE:/var/krb5/kdc.log");
	if (code != 0)
		return (code);

	hierarchy[1] = "kdc";

	(void) profile_clear_relation(profile, hierarchy);

	code = profile_add_relation(profile, hierarchy,
	    "FILE:/var/krb5/kdc.log");
	if (code != 0)
		return (code);

	hierarchy[1] = "kdc_rotate";

	hierarchy[2] = "period";

	(void) profile_clear_relation(profile, hierarchy);

	code = profile_add_relation(profile, hierarchy, "1d");
	if (code != 0)
		return (code);

	hierarchy[2] = "versions";

	(void) profile_clear_relation(profile, hierarchy);

	code = profile_add_relation(profile, hierarchy, "10");

	return (code);
}

/*
 * errcode_t k5_profile_set_libdefaults(profile_t profile, char *realm)
 * where profile was the pointer passed back by k5_profile_init
 * where realm is the realm name to be added as the default_realm
 */
errcode_t
k5_profile_set_libdefaults(profile_t profile, char *realm)
{
	const char	*hierarchy[4];
	errcode_t	code;

	if (profile == NULL || realm == NULL)
		return (EINVAL);

	hierarchy[0] = "libdefaults";
	hierarchy[1] = "default_realm";
	hierarchy[2] = NULL;

	/*
	 * Not fatal if this fails, continue on.
	 */
	(void) profile_clear_relation(profile, hierarchy);

	code = profile_add_relation(profile, hierarchy, realm);

	return (code);
}

errcode_t
k5_profile_set_kdc(profile_t profile, char *realm, char *kdc,
	boolean_t overwrite)
{
	const char	*hierarchy[4];
	errcode_t	code;

	if (profile == NULL || realm == NULL || kdc == NULL)
		return (EINVAL);

	hierarchy[0] = "realms";
	hierarchy[1] = realm;
	hierarchy[3] = NULL;

	hierarchy[2] = "kdc";

	if (overwrite == B_TRUE) {
		/*
		 * Not fatal if this fails, continue on.
		 */
		(void) profile_clear_relation(profile, hierarchy);
	}

	code = profile_add_relation(profile, hierarchy, kdc);

	return (code);
}

/*
 * errcode_t k5_profile_release(profile_t profile)
 *
 * where profile was the pointer passed back by k5_profile_init
 * Note: used to commit the associated profile to the backing store
 * (e.g. file) and free profile memory
 * Note: that this function returns an error code which profile_release
 * does not.  With the error code, the application can determine if they
 * need to free the resulting profile information in memory
 */
errcode_t
k5_profile_release(profile_t profile)
{
	prf_file_t	p, next;
	errcode_t	code;

	if (profile == NULL || profile->magic != PROF_MAGIC_PROFILE)
		return (EINVAL);

	for (p = profile->first_file; p; p = next) {
		next = p->next;
		if ((code = profile_close_file(p)) != 0)
			return (code);
	}
	profile->magic = 0;
	free(profile);

	return (0);
}

/*
 * void k5_profile_abandon(profile_t profile)
 *
 * where profile was the pointer passed back by k5_profile_init
 * Note: used to free any profile information in memory.  Typically can
 * be used in conjunction with k5_profile_release upon error
 */
void
k5_profile_abandon(profile_t profile)
{
	profile_abandon(profile);
}

/*
 * errcode_t k5_profile_add_domain_mapping(profile_t profile, char *domain,
 *	char *realm)
 *
 * where profile was the pointer passed back by k5_profile_init
 * where domain is the domain name of the associated realm name
 * where realm is the corresponding realm name for the domain
 */
errcode_t
k5_profile_add_domain_mapping(profile_t profile, char *domain, char *realm)
{
	const char	*hierarchy[4];
	errcode_t	code = 0;

	if (profile == NULL || domain == NULL || realm == NULL)
		return (EINVAL);

	hierarchy[0] = "domain_realm";
	hierarchy[1] = domain;
	hierarchy[2] = NULL;

	/*
	 * Not fatal if relation can't be cleared, continue on.
	 */
	(void) profile_clear_relation(profile, hierarchy);

	code = profile_add_relation(profile, hierarchy, realm);

	return (code);
}

/*
 * errcode_t k5_profile_remove_domain_mapping(profile_t profile, char *realm)
 *
 * where profile was the pointer passed back by k5_profile_init
 * where realm is the corresponding realm name for the domain
 * Note: for the remove function, all matching domain - realm mappings
 * will be removed for realm
 */
errcode_t
k5_profile_remove_domain_mapping(profile_t profile, char *realm)
{
	const char	*hierarchy[4];
	errcode_t	code;
	char		**domains = NULL, **domain = NULL;

	if (profile == NULL || realm == NULL)
		return (EINVAL);

	hierarchy[0] = "domain_realm";
	hierarchy[1] = NULL;
	hierarchy[2] = NULL;

	code = k5_profile_get_domain_realm(profile, realm, &domains, B_FALSE);
	if (code == 0 && domains != NULL) {
		for (domain = domains; *domain; domain++) {
			hierarchy[1] = *domain;
			code = profile_clear_relation(profile, hierarchy);
			if (code != 0)
				goto error;
		}
	}

error:
	if (domains != NULL)
		profile_free_list(domains);

	return (code);
}

/*
 * errcode_t k5_profile_get_realm_entry(profile_t profile, char *realm,
 *	char *name, char ***ret_value)
 *
 * where profile was the pointer passed back by k5_profile_init
 * where realm is the target realm for lookup
 * where name is the name in the realm section requested
 * where ret_value is a string array of any matching values assigned to name.
 * The array is terminated with a NULL pointer.
 * Note: if name has not been configured then NULL is returned for ret_value.
 */
errcode_t
k5_profile_get_realm_entry(profile_t profile, char *realm, char *name,
	char ***ret_value)
{
	const char	*hierarchy[4];
	errcode_t	code;

	if (profile == NULL || realm == NULL || name == NULL ||
	    ret_value == NULL)
		return (EINVAL);

	*ret_value = NULL;

	hierarchy[0] = "realms";
	hierarchy[1] = realm;
	hierarchy[2] = name;
	hierarchy[3] = NULL;

	code = profile_get_values(profile, hierarchy, ret_value);

	if (code == PROF_NO_RELATION)
		code = 0;

	return (code);
}

/*
 * errcode_t k5_profile_add_realm_entry(profile_t profile, char *realm,
 *	char *name, char **value)
 *
 * where profile was the pointer passed back by k5_profile_init
 * where realm is the target realm for the name-value pair
 * where name is the name in the realm subsection to add
 * where value is a string array values to assigned to name.  The array is
 * terminated with a NULL pointer.
 * Note: if the realm subsection does no exist then an error is returned
 * Note: if the name already exists the set is overwritten with the values
 * passed
 */
errcode_t
k5_profile_add_realm_entry(profile_t profile, char *realm, char *name,
	char **values)
{
	const char	*hierarchy[4];
	errcode_t	code;
	char		**tvalue = NULL;

	if (profile == NULL || realm == NULL || name == NULL || values == NULL)
		return (EINVAL);

	hierarchy[0] = "realms";
	hierarchy[1] = realm;
	hierarchy[2] = name;
	hierarchy[3] = NULL;

	/*
	 * Not fatal if this fails, continue on.
	 */
	(void) profile_clear_relation(profile, hierarchy);

	for (tvalue = values; *tvalue; tvalue++) {

		code = profile_add_relation(profile, hierarchy, *tvalue);
		if (code != 0)
			return (code);
	}

	return (0);
}

/*
 * errcode_t k5_profile_get_default_realm(profile_t profile, char **realm)
 *
 * where profile was the pointer passed back by k5_profile_init
 * where realm is the default_realm configured for the system
 * Note: if default_realm has not been configured then NULL is returned for
 * realm.
 */
errcode_t
k5_profile_get_default_realm(profile_t profile, char **realm)
{
	errcode_t	code;

	if (profile == NULL || realm == NULL)
		return (EINVAL);

	*realm = NULL;

	code = profile_get_string(profile, "libdefaults", "default_realm", 0, 0,
	    realm);

	if (code == PROF_NO_RELATION)
		code = 0;

	return (code);
}

/*
 * errcode_t k5_profile_get_realms(profile_t profile, char ***realms)
 *
 * where profile was the pointer passed back by k5_profile_init
 * where realms is a string array of realm names currently configured.
 * The array is terminated with a NULL pointer.
 * Note: if realms have not been configured then NULL is returned for realms.
 */
errcode_t
k5_profile_get_realms(profile_t profile, char ***realms)
{

	if (profile == NULL || realms == NULL)
		return (EINVAL);

	*realms = NULL;

	return (k5_profile_iter_name_value(profile, "realms", NULL, realms,
	    B_FALSE));
}

/*
 * errcode_t k5_profile_add_realm(profile_t profile, char *realm,
 *	char *master, char **kdcs, boolean_t set_change, boolean_t
 *	default_realm)
 *
 * where profile was the pointer passed back by k5_profile_init
 * where realm is the realm name associated with the configuration
 * where master is the server that is assigned to admin_server
 * where kdcs is a string array of KDCs used to populate the kdc set.
 * The array is terminated with a NULL pointer.
 * where set_change, if set, will use the SET_CHANGE protocol for password
 * modifications.  RPCSEC_GSS is set by default
 * where default_realm, if set, will assign the realm to default_realm
 * Note: the ordering of kdcs is determined by the server's position in the
 * array
 * Note: kdcs must be assigned a value, even if it is the same value as the
 * master.
 */
errcode_t
k5_profile_add_realm(profile_t profile, char *realm, char *master, char **kdcs,
	boolean_t set_change, boolean_t default_realm)
{
	const char	*hierarchy[4];
	errcode_t	code;
	boolean_t	ow = B_TRUE;
	char		**tkdcs;

	if (profile == NULL || realm == NULL || master == NULL || kdcs == NULL)
		return (EINVAL);

	/*
	 * Sets the default realm to realm if default_realm flag is set.
	 */
	if (default_realm == B_TRUE) {
		if (code = k5_profile_set_libdefaults(profile, realm))
			return (code);
	}

	hierarchy[0] = "realms";
	hierarchy[1] = realm;
	hierarchy[3] = NULL;

	hierarchy[2] = "admin_server";

	/*
	 * Not fatal if this fails, therefore return code is not checked.
	 */
	(void) profile_clear_relation(profile, hierarchy);

	if (code = profile_add_relation(profile, hierarchy, master))
		return (code);

	/*
	 * If not set then defaults to undefined, which defaults to RPCSEC_GSS.
	 */
	if (set_change == B_TRUE) {
		hierarchy[2] = "kpasswd_protocol";

		(void) profile_clear_relation(profile, hierarchy);

		code = profile_add_relation(profile, hierarchy, "SET_CHANGE");
		if (code != 0)
			return (code);
	}

	for (tkdcs = kdcs; *tkdcs; tkdcs++) {
		if (code = k5_profile_set_kdc(profile, realm, *tkdcs, ow))
			return (code);
		ow = B_FALSE;
	}

	code = k5_profile_set_logging(profile);
	if (code != 0)
		return (code);

	code = k5_profile_set_appdefaults(profile);

	return (code);
}

/*
 * errcode_t k5_profile_remove_xrealm_mapping(profile_t profile, char *realm)
 *
 * where profile was the pointer passed back by k5_profile_init
 * where source is the source realm for the capath
 * where target is the target realm for the capath
 * where inter is the intermediate realm between the source and target
 * realms.  If the source and target share x-realm keys then this set to "."
 * Note: for the remove function, all associated source, target, and
 * intermediate entries will be removed matching the realm name
 */
errcode_t
k5_profile_remove_xrealm_mapping(profile_t profile, char *realm)
{
	const char	*hierarchy[4];
	errcode_t	code, code2, code3;
	void		*state = NULL, *state2 = NULL;
	char		*source = NULL, *dummy_val = NULL, *target = NULL;
	char		*inter = NULL;

	if (profile == NULL || realm == NULL)
		return (EINVAL);

	hierarchy[0] = "capaths";
	hierarchy[1] = realm;
	hierarchy[2] = NULL;
	hierarchy[3] = NULL;

	/*
	 * Not fatal if this fails, continue on.
	 */
	code = profile_rename_section(profile, hierarchy, NULL);

	hierarchy[1] = NULL;
	code = profile_iterator_create(profile, hierarchy,
	    PROFILE_ITER_LIST_SECTION, &state);
	while (code == 0) {
		code = profile_iterator(&state, &source, &dummy_val);
		if (code == 0 && source != NULL) {
			hierarchy[1] = source;
			code2 = profile_iterator_create(profile, hierarchy,
			    PROFILE_ITER_LIST_SECTION, &state2);
			while (code2 == 0) {
				code2 = profile_iterator(&state2, &target,
				    &inter);
				if (code2 == 0 && target != NULL &&
				    inter != NULL) {
					if (strcmp(realm, target) == 0 ||
					    strcmp(realm, inter) == 0) {
						hierarchy[2] = target;
						code3 =
						    profile_clear_relation(
						    profile, hierarchy);
						if (code3 != 0) {
							code = code3;
							goto error;
						}
					}
				}
				if (target != NULL) {
					profile_release_string(target);
					target = NULL;
				}
				if (inter != NULL) {
					profile_release_string(inter);
					inter = NULL;
				}
			}
		}
		if (source != NULL) {
			profile_release_string(source);
			source = NULL;
		}
		if (dummy_val != NULL) {
			profile_release_string(dummy_val);
			dummy_val = NULL;
		}
	}
	code = 0;

error:
	if (state != NULL)
		profile_iterator_free(&state);
	if (state2 != NULL)
		profile_iterator_free(&state2);
	if (target != NULL)
		profile_release_string(target);
	if (inter != NULL)
		profile_release_string(inter);
	if (source != NULL)
		profile_release_string(source);
	if (dummy_val != NULL)
		profile_release_string(dummy_val);

	return (code);
}

/*
 * errcode_t k5_profile_remove_realm(profile_t profile, char *realm)
 *
 * where profile was the pointer passed back by k5_profile_init
 * where realm is the target realm for removal
 * Note: the function removes the matching realm in the realms section,
 * the default_realm, relevant domain_realm mappings with the realm name,
 * and matching capaths source realm subsection.
 */
errcode_t
k5_profile_remove_realm(profile_t profile, char *realm)
{
	const char	*hierarchy[4];
	errcode_t	code;
	char		*drealm = NULL;

	if (profile == NULL || realm == NULL)
		return (EINVAL);

	/*
	 * Remove the default realm.
	 */
	hierarchy[0] = "libdefaults";
	hierarchy[1] = "default_realm";
	hierarchy[2] = NULL;

	/*
	 * We are not returning on missing configuration information given that
	 * we are performing a best effort on removing as much realm information
	 * as possible.  Regardless of symmetry from adding.
	 */
	code = k5_profile_get_default_realm(profile, &drealm);
	if (code == 0 && drealm != NULL) {
		if (strcmp(drealm, realm) == 0) {
			code = profile_clear_relation(profile, hierarchy);
			if (code != 0) {
				free(drealm);
				return (code);
			}
		}
		free(drealm);
	}

	hierarchy[0] = "realms";
	hierarchy[1] = realm;
	hierarchy[2] = NULL;

	(void) profile_rename_section(profile, hierarchy, NULL);

	(void) k5_profile_remove_domain_mapping(profile, realm);

	(void) k5_profile_remove_xrealm_mapping(profile, realm);

	/*
	 * Not fatal even if realm wasn't available to remove.
	 */
	return (0);
}

/*
 * errcode_t k5_profile_add_xrealm_mapping(profile_t profile, char *source,
 *	char *target, char *inter)
 *
 * where profile was the pointer passed back by k5_profile_init
 * where source is the source realm for the capath
 * where target is the target realm for the capath
 * where inter is the intermediate realm between the source and target
 * realms.  If the source and target share x-realm keys then this set to "."
 * Note: if the section does not exist one will be created
 */
errcode_t
k5_profile_add_xrealm_mapping(profile_t profile, char *source, char *target,
	char *inter)
{
	const char	*hierarchy[4];
	errcode_t	code;

	if (profile == NULL || source == NULL || target == NULL ||
	    inter == NULL)
		return (EINVAL);

	hierarchy[0] = "capaths";
	hierarchy[1] = source;
	hierarchy[2] = target;
	hierarchy[3] = NULL;

	/*
	 * Not fatal if this fails, continue on.
	 */
	(void) profile_clear_relation(profile, hierarchy);

	code = profile_add_relation(profile, hierarchy, inter);

	return (code);
}

/*
 * errcode_t k5_profile_validate(profile_t profile, char *realm, int *val_err,
 *	char **val)
 *
 * where profile was the pointer passed back by k5_profile_init
 * where realm is the realm name (case sensitive) to be inspected.
 * where val_err is a function specific error code of the following values:
 *	K5_PROFILE_VAL_SUCCESS
 *	    No errors detected in profile
 *	    (val returns realm validated)
 *	K5_PROFILE_VAL_DEF_REALM_CASE
 *	    default realm only differs in case from the realm specified
 *	    (val returns default realm configured)
 *	K5_PROFILE_VAL_REALM_CASE
 *	    realm in realms section differs in case from the realm specified
 *	    (val returns realm configured)
 *	K5_PROFILE_VAL_NO_DEF_IN_REALM
 *	    default realm is not found in realms section
 *	    (val returns realm not found)
 *	K5_PROFILE_VAL_NO_DEF_REALM
 *	    default realm does not exist
 *	    (val returns realm specified)
 *	K5_PROFILE_VAL_NULL_REALM
 *	    no realm found in realms section
 *	    (val returns realm specified)
 *	K5_PROFILE_VAL_NO_DOM_REALM_MAP
 *	    no domain realm mapping entry found from the realm specified
 *	    (val returns realm specified)
 *	K5_PROFILE_VAL_KDC_NO_REALM
 *	    kdc relation-value does not exist for specified realm
 *	    (val returns realm specified)
 *	K5_PROFILE_VAL_ADMIN_NO_REALM
 *	    admin_server relation-value does not exist for specified realm
 *	    (val returns realm specified)
 *	K5_PROFILE_VAL_DOM_REALM_CASE
 *	    the realm name argument differs in case from the realm name argument
 *	    for a domain-realm mapping entry
 *	    (val returns the domain/host name)
 *	K5_PROFILE_VAL_NO_REALM
 *	    realm name specified is not found in realms section
 *	    (val returns the realm specified)
 * where val is the associated errant value, associated with val_err.  This
 * value is returned as is from the profile
 *
 * Note: function infers the following:
 *	1. all required entries are present
 *	2. all relations are defined between default realm, realm, and
 *		domain - realm mappings
 *
 * Note: The return value of this function is based on the error code returned
 * by the framework/mechanism.  The function could return zero with the
 * validation error code set to non-zero if the profile is invalid in any way.
 *
 * Caution: This function could return false positives on valid
 * configurations and should only be used by the CIFS team for
 * specific purposes.
 */
errcode_t
k5_profile_validate(profile_t profile, char *realm, int *val_err, char **val)
{
	errcode_t	code;
	boolean_t	found = B_FALSE, found_realm = B_FALSE;
	char		*default_realm = NULL, **realms = NULL;
	char		**trealms = NULL, **domains = NULL, **ret_vals = NULL;

	if (profile == NULL || realm == NULL || val_err == NULL || val == NULL)
		return (EINVAL);

	*val_err = K5_PROFILE_VAL_SUCCESS;
	*val = NULL;

	code = k5_profile_get_default_realm(profile, &default_realm);
	if (code == 0 && default_realm != NULL) {
		if (strcasecmp(default_realm, realm) == 0) {
			if (strcmp(default_realm, realm) != 0) {
				*val_err = K5_PROFILE_VAL_DEF_REALM_CASE;
				if ((*val = strdup(default_realm)) == NULL)
					code = ENOMEM;
				goto cleanup;
			}
		}
	} else if (code == 0 && default_realm == NULL) {
		*val_err = K5_PROFILE_VAL_NO_DEF_REALM;
		if ((*val = strdup(realm)) == NULL)
			code = ENOMEM;
		goto cleanup;
	} else
		goto cleanup;

	code = k5_profile_get_realms(profile, &realms);
	if (code == 0 && realms != NULL) {
		for (trealms = realms; *trealms; trealms++) {

			if (strcmp(default_realm, *trealms) == 0)
				found = B_TRUE;

			if (strcmp(realm, *trealms) != 0) {
				if (strcasecmp(realm, *trealms) == 0) {
					*val_err = K5_PROFILE_VAL_REALM_CASE;
					*val = strdup(*trealms);
					if (*val == NULL)
						code = ENOMEM;
					goto cleanup;
				} else
					continue;
			}

			found_realm = B_TRUE;
			/*
			 * Perform case insensitive lookup first, followed
			 * by case sensitive search.  This allows detection
			 * of lower case names before an exact match, which
			 * would not match if the name was in lower case.
			 */
			code = k5_profile_get_domain_realm(profile, *trealms,
			    &domains, B_TRUE);
			if (code == 0 && domains != NULL) {
				*val_err = K5_PROFILE_VAL_DOM_REALM_CASE;
				if ((*val = strdup(*domains)) == NULL)
					code = ENOMEM;
				goto cleanup;
			} else if (code == 0 && domains == NULL) {
				code = k5_profile_get_domain_realm(profile,
				    *trealms, &domains, B_FALSE);
				if (code == 0 && domains != NULL) {
					profile_free_list(domains);
					domains = NULL;
				} else if (code == 0 && domains == NULL) {
					*val_err =
					    K5_PROFILE_VAL_NO_DOM_REALM_MAP;
					if ((*val = strdup(realm)) == NULL)
						code = ENOMEM;
					goto cleanup;
				} else
					goto cleanup;
			} else
				goto cleanup;

			code = k5_profile_get_realm_entry(profile, *trealms,
			    "kdc", &ret_vals);
			if (code == 0 && ret_vals != NULL) {
				profile_free_list(ret_vals);
				ret_vals = NULL;
			} else if (code == 0 && ret_vals == NULL) {
				*val_err = K5_PROFILE_VAL_KDC_NO_REALM;
				if ((*val = strdup(realm)) == NULL)
					code = ENOMEM;
				goto cleanup;
			} else
				goto cleanup;

			code = k5_profile_get_realm_entry(profile, *trealms,
			    "admin_server", &ret_vals);
			if (code == 0 && ret_vals != NULL) {
				profile_free_list(ret_vals);
				ret_vals = NULL;
			} else if (code == 0 && ret_vals == NULL) {
				*val_err = K5_PROFILE_VAL_ADMIN_NO_REALM;
				if ((*val = strdup(realm)) == NULL)
					code = ENOMEM;
				goto cleanup;
			} else
				goto cleanup;
		}

		if (found == B_FALSE) {
			*val_err = K5_PROFILE_VAL_NO_DEF_IN_REALM;
			if ((*val = strdup(default_realm)) == NULL)
				code = ENOMEM;
			goto cleanup;
		} else if (found_realm == B_FALSE) {
			*val_err = K5_PROFILE_VAL_NO_REALM;
			if ((*val = strdup(realm)) == NULL)
				code = ENOMEM;
			goto cleanup;
		}
	} else if (code == 0 && realms == NULL) {
		*val_err = K5_PROFILE_VAL_NULL_REALM;
		if ((*val = strdup(realm)) == NULL) {
			code = ENOMEM;
			goto cleanup;
		}
	}
	if ((*val = strdup(realm)) == NULL)
		code = ENOMEM;

cleanup:

	if (realms != NULL)
		profile_free_list(realms);
	if (ret_vals != NULL)
		profile_free_list(ret_vals);
	if (default_realm != NULL)
		profile_release_string(default_realm);
	if (domains != NULL)
		profile_free_list(domains);

	return (code);
}

/*
 * errcode_t k5_profile_init(char *filename, profile_t *profile)
 *
 * where filename is the specified profile location.  If filename is NULL
 * then function uses the system default name, /etc/krb5/krb5.conf
 * where profile is pointer passed to caller upon success
 * Note: if the file does not exist then one will be created
 * Note: if the file does exist then any existing profile information will
 * be in profile
 * Note: profile_release() should be used by the caller to free profile
 */
errcode_t
k5_profile_init(char *filename, profile_t *profile)
{
	profile_filespec_t	*filenames = NULL;
	krb5_error_code		ret = 0;
	errcode_t		code = 0;
	int			err = 0, fd;
	mode_t			mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	if (profile == NULL)
		return (EINVAL);

	if (filename != NULL) {
		filenames = malloc(2 * sizeof (char *));
		if (filenames == NULL)
			return (ENOMEM);
		filenames[0] = strdup(filename);
		if (filenames[0] == NULL) {
			free(filenames);
			return (ENOMEM);
		}
		filenames[1] = NULL;
	} else {
		ret = krb5_get_default_config_files(&filenames);
		if (ret != 0)
			return (ret);
	}

	/*
	 * If file does not exist then create said file.
	 */
	fd = open(*filenames, O_RDWR|O_CREAT|O_NOFOLLOW|O_NOLINKS, mode);
	if (fd < 0) {
		err = errno;
		krb5_free_config_files(filenames);
		return (err);
	} else
		close(fd);

	/*
	 * Specify non-null for specific file (to load any existing profile)
	 */
	code = profile_init((const_profile_filespec_t *)filenames, profile);

	krb5_free_config_files(filenames);

	return (code);
}

/*
 * k5_profile_validate_get_error_msg(profile_t profile, int err, char *val,
 *  char **err_msg)
 *
 * intput arguments:
 *  where profile was pointer passed back by k5_profile_init
 *  where err is the val_err returned by the k5_profile_validate function
 *  where val is the val returned by the k5_profile_validate function
 *
 * output arguments:
 *  where err_msg is the returned error message string
 *
 * Note: Upon success an error string will be passed back by the caller in
 * err_msg else an error code is returned and err_msg is not allocated.
 */
errcode_t
k5_profile_validate_get_error_msg(profile_t profile, int err, char *val,
    char **err_msg)
{
	char				*m;
	const_profile_filespec_t	f;
	errcode_t			code = 0;

	if (profile == NULL || val == NULL || err_msg == NULL)
		return (EINVAL);

	*err_msg = NULL;
	f = profile->first_file->data->filespec;

	switch (err) {
	case K5_PROFILE_VAL_SUCCESS:
		m = dgettext(TEXT_DOMAIN, "No error");
		break;
	case K5_PROFILE_VAL_DEF_REALM_CASE:
		m = dgettext(TEXT_DOMAIN,
		    "default_realm should be in upper-case");
		break;
	case K5_PROFILE_VAL_REALM_CASE:
		m = dgettext(TEXT_DOMAIN,
		    "realm in [realms] section should be in upper-case");
		break;
	case K5_PROFILE_VAL_NO_DEF_IN_REALM:
		m = dgettext(TEXT_DOMAIN,
		    "default_realm is not found in [realms] section");
		break;
	case K5_PROFILE_VAL_NO_DEF_REALM:
		m = dgettext(TEXT_DOMAIN,
		    "default_realm doesn't exist in the [libdefaults] section");
		break;
	case K5_PROFILE_VAL_NULL_REALM:
		m = dgettext(TEXT_DOMAIN,
		    "no realms found in the [realms] section");
		break;
	case K5_PROFILE_VAL_NO_DOM_REALM_MAP:
		m = dgettext(TEXT_DOMAIN,
		    "missing mapping entry in the [domain_realm] "
		    "section for the specified realm");
		break;
	case K5_PROFILE_VAL_KDC_NO_REALM:
		m = dgettext(TEXT_DOMAIN,
		    "no kdc entries found for the specified realm");
		break;
	case K5_PROFILE_VAL_ADMIN_NO_REALM:
		m = dgettext(TEXT_DOMAIN,
		    "no admin_server entry found for the specified realm");
		break;
	case K5_PROFILE_VAL_DOM_REALM_CASE:
		m = dgettext(TEXT_DOMAIN,
		    "mapping entry in [domain_realm] section for "
		    "the specified realm should be in upper-case");
		break;
	case K5_PROFILE_VAL_NO_REALM:
		m = dgettext(TEXT_DOMAIN,
		    "specified realm not found in [realms] section");
		break;
	default:
		m = dgettext(TEXT_DOMAIN, "unknown error");
		break;
	}

	if (asprintf(err_msg, "%s: %s: %s", f, m, val) == -1)
		code = ENOMEM;

	return (code);
}

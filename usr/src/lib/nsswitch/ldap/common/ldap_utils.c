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
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/systeminfo.h>
#include "ldap_common.h"


#ifdef DEBUG
/*
 * Debugging routine for printing the value of a result
 * structure
 */
int
printresult(ns_ldap_result_t *result)
{
	int		i, j, k;
	ns_ldap_entry_t	*curEntry;

	printf("--------------------------------------\n");
	printf("entries_count %d\n", result->entries_count);
	curEntry = result->entry;
	for (i = 0; i < result->entries_count; i++) {
		printf("entry %d has attr_count = %d \n",
		    i, curEntry->attr_count);
		for (j = 0; j < curEntry->attr_count; j++) {
			printf("entry %d has attr_pair[%d] = %s \n",
			    i, j, curEntry->attr_pair[j]->attrname);
			for (k = 0;
			    (k < curEntry->attr_pair[j]->value_count) &&
			    (curEntry->attr_pair[j]->attrvalue[k]);
			    k++)
				printf("entry %d has "
				    "attr_pair[%d]->attrvalue[%d] = %s \n",
				    i, j, k,
				    curEntry->attr_pair[j]->attrvalue[k]);
		}
		printf("\n--------------------------------------\n");
		curEntry = curEntry->next;
	}
	return (1);
}
#endif


/*
 *
 */

ns_ldap_attr_t *
getattr(ns_ldap_result_t *result, int i)
{
	ns_ldap_entry_t	*entry;

#ifdef DEBUG
	(void) fprintf(stdout, "\n[ldap_utils.c: getattr]\n");
#endif /* DEBUG */

	if (result != NULL) {
		entry = result->entry;
	} else {
		return (NULL);
	}
	if (result->entries_count == 0) {
		return (NULL);
	} else {
		return (entry->attr_pair[i]);
	}
}

/*
 * _get_domain_name() passes the dn one level up from cdn, e.g.,
 * a pointer pointing to "ou= ..." for the cdn's listed below:
 * 	dn: cn=hostname+ipHostNumber="109.34.54.76", ou= ...
 *	dn: echo+IpServiceProtocol=udp, ou= ...
 * to __ns_ldap_dn2domain() to retrieve the domain name associated
 * with cdn.
 */

char *
_get_domain_name(char *cdn)
{
	char			**rdns;
	char			*pdn, *domain = NULL;
	int			nrdns;
	int			len = 0;
	const ns_cred_t		*cred = NULL;
	ns_ldap_error_t		*error;

	/* break the cdn into its components */
	rdns = ldap_explode_dn(cdn, 0);
	if (rdns == NULL || *rdns == NULL)
		return (NULL);

	/* construct parent dn */
	for (nrdns = 1; rdns[nrdns]; nrdns++)
		len += strlen(rdns[nrdns]) + 1;
	if (len == 0)
		len = strlen(rdns[0]);
	pdn = (char *)malloc(len + 1);
	if (pdn == NULL) {
		ldap_value_free(rdns);
		return (NULL);
	}

	*pdn = '\0';
	if (nrdns == 1)
		(void) strcat(pdn, rdns[0]);
	else {
		for (nrdns = 1; rdns[nrdns]; nrdns++) {
			(void) strcat(pdn, rdns[nrdns]);
			(void) strcat(pdn, ",");
		}
		/* remove the last ',' */
		pdn[strlen(pdn) - 1] = '\0';
	}
	/* get domain name */
	(void) __ns_ldap_dn2domain(pdn, &domain, cred, &error);

	ldap_value_free(rdns);
	free(pdn);
	return (domain);
}


/*
 * 	"109.34.54.76" -> 109.34.54.76
 */

const char *
_strip_quotes(char *ipaddress)
{
	char	*cp = (char *)NULL;

	/* look for first " */
	if ((cp = strchr(ipaddress, '"')) == NULL)
		return ((char *)ipaddress);
	ipaddress++;
	/* look for last " */
	if ((cp = strchr(ipaddress, '"')) == NULL)
		return ((char *)ipaddress);
	*cp++ = '\0';

	return (ipaddress);
}

/*
 * This function has been copied from lib/libsldap/common/ns_getalias.c
 * and had some DEBUG statemants added.
 *
 * This function builds a new string, as identified by realfilter, by
 * replacing the first occurrence of '%s' in userdata with the string
 * filter provided in the Service Search Descriptor (SSD).
 *
 * It is used as a call back function by __ns_ldap_list() API.
 *
 * As an example, lets assume the caller of __ns_ldap_list() is
 * accessing password information. The userdata filter would previously
 * have been constructed to contain "(&(%s)(userid=fred))". And the SSD
 * filter consists of "dept=sds".  The new filter would result in
 * "(&(dept=sds)(userid=fred))".
 */
int
_merge_SSD_filter(const ns_ldap_search_desc_t *desc,
			char **realfilter,
			const void *userdata)
{
	int	filterlen;
	int	merged = 0;
	char	*realfilterp;
	char	*userdatap = (char *)userdata;

#ifdef DEBUG
	(void) fprintf(stdout, "\n[ldap_utils.c: _merge_SSD_filter]\n");
#endif /* DEBUG */

	/* sanity check */
	if (realfilter == NULL)
		return (NS_LDAP_INVALID_PARAM);
	*realfilter = NULL;

	if (desc == NULL || desc->filter == NULL || userdata == NULL)
		return (NS_LDAP_INVALID_PARAM);

#ifdef DEBUG
	(void) fprintf(stdout, "\n[userdata: %s]\n", (char *)userdata);
	(void) fprintf(stdout, "\n[SSD filter: %s]\n", desc->filter);
#endif /* DEBUG */

	filterlen = strlen(desc->filter);
	*realfilter = (char *)malloc(strlen(userdata) + filterlen + 1);
	if (*realfilter == NULL)
		return (NS_LDAP_MEMORY);

	realfilterp = *realfilter;
	while (*userdatap != '\0') {
		if (!merged && *userdatap == '%' && *(userdatap + 1) == 's') {
			userdatap += 2; /* step over "%s" */
			(void) memcpy(realfilterp, desc->filter, filterlen);
			realfilterp += filterlen;
			merged = 1; /* Continue to copy rest of userdata only */
		} else {
			*realfilterp++ = *userdatap++; /* Copy and advance */
		}
	}
	*realfilterp = '\0';

#ifdef DEBUG
	(void) fprintf(stdout, "\n[new filter: %s]\n", *realfilter);
#endif /* DEBUG */

	return (NS_LDAP_SUCCESS);
}

static char
hex_char(int n)
{
	return ("0123456789abcdef"[n & 0xf]);
}

int
_ldap_filter_name(char *filter_name, const char *name, int filter_name_size)
{
	char *end = filter_name + filter_name_size;
	char c;

	for (; *name; name++) {
		c = *name;
		switch (c) {
			case '*':
			case '(':
			case ')':
			case '\\':
				if (end <= filter_name + 3)
					return (-1);
				*filter_name++ = '\\';
				*filter_name++ = hex_char(c >> 4);
				*filter_name++ = hex_char(c & 0xf);
				break;
			default:
				if (end <= filter_name + 1)
					return (-1);
				*filter_name++ = c;
				break;
		}
	}
	if (end <= filter_name)
		return (-1);
	*filter_name = '\0';
	return (0);
}

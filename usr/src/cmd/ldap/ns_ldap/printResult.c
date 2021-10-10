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
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include "../../../lib/libsldap/common/ns_sldap.h"

void
_printEntry(ns_ldap_entry_t *entry) {
	int	j, k;
	char	*cp;
	for (j = 0; j < entry->attr_count; j++) {
		cp = entry->attr_pair[j]->attrname;
		if (j == 0) {
			(void) fprintf(stdout, "%s: %s\n", cp,
				entry->attr_pair[j]->attrvalue[0]);
		} else {
			for (k = 0; (k < entry->attr_pair[j]->value_count) &&
			    (entry->attr_pair[j]->attrvalue[k]); k++) {
				char *val;
				/* not base64 most of the time */
				char *colon = "";

				val = entry->attr_pair[j]->attrvalue[k];
				/*
				 * Skip the base64 tag, if any. Use
				 * double colons to indicate base64.
				 */
				if (memcmp(val, NS_LDAP_BASE64_TAG,
				    NS_LDAP_BASE64_TAG_LEN) == 0) {
					val = val + NS_LDAP_BASE64_TAG_LEN;
					colon = ":";
				}

				(void) fprintf(stdout, "\t%s:%s %s\n",
				    cp, colon, val);
			}
		}
	}
}

void
_printResult(ns_ldap_result_t *result) {
	ns_ldap_entry_t *curEntry;
	int	i;

	if (result == NULL) {
		return;
	}
	curEntry = result->entry;
	for (i = 0; i < result->entries_count; i++) {
		if (i != 0)
			(void) fprintf(stdout, "\n");
		_printEntry(curEntry);
		curEntry = curEntry->next;
	}
}

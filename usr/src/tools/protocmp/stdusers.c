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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <string.h>
#include "stdusers.h"

/* LINTLIBRARY */

const struct stdlist usernames[] = {
	{ "root", 0 },
	{ "daemon", 1 },
	{ "bin", 2 },
	{ "sys", 3 },
	{ "adm", 4 },
	{ "uucp", 5 },
	{ "nuucp", 9 },
	{ "dladm", 15 },
	{ "netadm", 16 },
	{ "netcfg", 17 },
	{ "smmsp", 25 },
	{ "listen", 37 },
	{ "gdm", 50 },
	{ "lp", 71 },
	{ "mysql", 70 },
	{ "openldap", 75 },
	{ "webservd", 80 },
	{ "ftp", 85 },
	{ "postgres", 90 },
	{ "nobody", 60001 },
	{ "noaccess", 60002 },
	{ "nobody4", 65534 },
	{ NULL, 0 }
};

const struct stdlist groupnames[] = {
	{ "root", 0 },
	{ "other", 1 },
	{ "bin", 2 },
	{ "sys", 3 },
	{ "adm", 4 },
	{ "uucp", 5 },
	{ "mail", 6 },
	{ "tty", 7 },
	{ "lp", 8 },
	{ "nuucp", 9 },
	{ "staff", 10 },
	{ "daemon", 12 },
	{ "sysadmin", 14 },
	{ "games", 20 },
	{ "smmsp", 25 },
	{ "gdm", 50 },
	{ "netadm", 65 },
	{ "mysql", 70 },
	{ "openldap", 75 },
	{ "webservd", 80 },
	{ "ftp", 85 },
	{ "postgres", 90 },
	{ "slocate", 95 },
	{ "nobody", 60001 },
	{ "noaccess", 60002 },
	{ "nogroup", 65534 },
	{ NULL, 0 }
};

int
stdfind(const char *name, const struct stdlist *list)
{
	while (list->name != NULL) {
		if (strcmp(name, list->name) == 0)
			return (list->value);
		list++;
	}
	return (-1);
}

const char *
stdfindbyvalue(int value, const struct stdlist *list)
{
	while (list->name != NULL) {
		if (value == list->value)
			return (list->name);
		list++;
	}
	return (NULL);
}

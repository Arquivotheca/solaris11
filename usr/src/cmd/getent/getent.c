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
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>
#include <nss.h>
#include "getent.h"

static const char *cmdname;

struct table {
	char	*name;			/* name of the table */
	int	(*func)(const char **);	/* function to do the lookup */
};

static struct table t[] = {
	{ "passwd",	dogetpw },
	{ "group",	dogetgr },
	{ "hosts",	dogethost },
	{ "ipnodes",	dogetipnodes },
	{ "services",	dogetserv },
	{ "protocols",	dogetproto },
	{ "ethers",	dogetethers },
	{ "networks",	dogetnet },
	{ "netmasks",	dogetnetmask },
	{ "project",	dogetproject },
	{ "user_attr",	dogetuserattr },
	{ "prof_attr",	dogetprofattr },
	{ "exec_attr",	dogetexecattr },
	{ "auth_attr",	dogetauthattr },
	{ NULL,		NULL }
};

static	void usage(void) __NORETURN;

int
main(int argc, const char **argv)
{
	struct table *p;

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN	"SYS_TEXT"
#endif

	(void) textdomain(TEXT_DOMAIN);

	cmdname = argv[0];

	if (argc < 2)
		usage();

	for (p = t; p->name != NULL; p++) {
		if (strcmp(argv[1], p->name) == 0) {
			int rc;

			rc = (*p->func)(&argv[2]);
			switch (rc) {
			case EXC_SYNTAX:
				(void) fprintf(stderr,
				    gettext("Syntax error\n"));
				break;
			case EXC_ENUM_NOT_SUPPORTED:
				(void) fprintf(stderr,
				    gettext("Enumeration not supported "
				    "on %s\n"), argv[1]);
				break;
			case EXC_NAME_NOT_FOUND:
				break;
			}
			exit(rc);
		}
	}
	(void) fprintf(stderr, gettext("Unknown database: %s\n"), argv[1]);
	usage();
	/* NOTREACHED */

	return (0);
}

static void
usage(void)
{
	(void) fprintf(stderr,
	    gettext("usage: %s database [ key ... ]\n"), cmdname);
	exit(EXC_SYNTAX);
}

/*
 * putkkeyvalue - print key/value pair
 */
void
putkeyvalue(kva_t *attr, FILE *f)
{
	kv_t	*kv_pair;
	char	*key;
	char	*val;
	char	*sep;
	int	i;

	if (attr != NULL && attr->data != NULL) {
		char *val_esc;

		kv_pair = attr->data;
		for (i = 0, sep = ""; i < attr->length; i++) {
			key = kv_pair[i].key;
			val = kv_pair[i].value;
			if (key == NULL || val == NULL) {
				break;
			}
			/* Output escape for special chars */
			val_esc = _escape(val, KV_SPECIAL);
			(void) fprintf(f, "%s%s=%s", sep, key, val_esc);
			free(val_esc);
			sep = ";";
		}
	}
}

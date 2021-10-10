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

#include "lint.h"
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <nss_dbdefs.h>
#include <auth_attr.h>


authattr_t *_authstr2attr(authstr_t *);

/* externs from parse.c */
extern char *_strtok_escape(char *, char *, char **);

static int authattr_stayopen = 0;
/*
 * Unsynchronized, but it affects only
 * efficiency, not correctness
 */

static DEFINE_NSS_DB_ROOT(db_root);
static DEFINE_NSS_GETENT(context);

void
_nss_initf_authattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_AUTHATTR;
	p->default_config = NSS_DEFCONF_AUTHATTR;
}


/*
 * Return values: 0 = success, 1 = parse error, 2 = erange ...
 * The structure pointer passed in is a structure in the caller's space
 * wherein the field pointers would be set to areas in the buffer if
 * need be. instring and buffer should be separate areas.
 */
int
_str2authattr(const char *instr, int lenstr, void *ent, char *buf, int buflen)
{
	char		*last = NULL;
	char		*sep = KV_TOKEN_DELIMIT;
	authstr_t	*auth = ent;

	if (lenstr >= buflen)
		return (NSS_STR_PARSE_ERANGE);

	if (instr != buf)
		(void) memcpy(buf, instr, lenstr);

	/* Terminate the buffer */
	buf[lenstr] = '\0';

	/* quick exit do not entry fill if not needed */
	if (ent == NULL)
		return (NSS_STR_PARSE_SUCCESS);

	auth->name = _strtok_escape(buf, sep, &last);
	auth->res1 = _strtok_escape(NULL, sep, &last);
	auth->res2 = _strtok_escape(NULL, sep, &last);
	auth->short_desc = _strtok_escape(NULL, sep, &last);
	auth->long_desc = _strtok_escape(NULL, sep, &last);
	auth->attr = _strtok_escape(NULL, sep, &last);

	return (0);
}


void
setauthattr(void)
{
	authattr_stayopen = 0;
	nss_setent(&db_root, _nss_initf_authattr, &context);
}


void
endauthattr(void)
{
	authattr_stayopen = 0;
	nss_endent(&db_root, _nss_initf_authattr, &context);
	nss_delete(&db_root);
}


static authstr_t *
_getauthattr(authstr_t *result, char *buffer, int buflen, int *h_errnop)
{
	nss_XbyY_args_t arg;
	nss_status_t res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, _str2authattr);
	res = nss_getent(&db_root, _nss_initf_authattr, &context, &arg);
	arg.status = res;
	*h_errnop = arg.h_errno;
	return (NSS_XbyY_FINI(&arg));
}


static authstr_t *
_getauthnam(const char *name, authstr_t *result, char *buffer, int buflen,
    int *errnop)
{
	nss_XbyY_args_t arg;
	nss_status_t    res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, _str2authattr);
	arg.key.name = name;
	arg.stayopen = authattr_stayopen;
	res = nss_search(&db_root, _nss_initf_authattr,
	    NSS_DBOP_AUTHATTR_BYNAME, &arg);
	arg.status = res;
	*errnop = arg.h_errno;
	return (NSS_XbyY_FINI(&arg));
}


authattr_t *
getauthattr(void)
{
	int		err = 0;
	char		buf[NSS_BUFLEN_AUTHATTR];
	authstr_t	auth;
	authstr_t	*tmp;

	(void) memset(&auth, 0, sizeof (authstr_t));
	tmp = _getauthattr(&auth, buf, NSS_BUFLEN_AUTHATTR, &err);
	return (_authstr2attr(tmp));
}


authattr_t *
getauthnam(const char *name)
{
	int		err = 0;
	char		buf[NSS_BUFLEN_AUTHATTR];
	authstr_t	auth;
	authstr_t	*tmp;

	if (name == NULL) {
		return (NULL);
	}
	(void) memset(&auth, 0, sizeof (authstr_t));
	tmp = _getauthnam(name, &auth, buf, NSS_BUFLEN_AUTHATTR, &err);
	return (_authstr2attr(tmp));
}

void
free_authattr(authattr_t *auth)
{
	if (auth) {
		free(auth->name);
		free(auth->res1);
		free(auth->res2);
		free(auth->short_desc);
		free(auth->long_desc);
		_kva_free(auth->attr);
		free(auth);
	}
}


authattr_t *
_authstr2attr(authstr_t *auth)
{
	authattr_t *newauth;

	if (auth == NULL)
		return (NULL);

	if ((newauth = malloc(sizeof (authattr_t))) == NULL)
		return (NULL);

	newauth->name = _do_unescape(auth->name);
	newauth->res1 = _do_unescape(auth->res1);
	newauth->res2 = _do_unescape(auth->res2);
	newauth->short_desc = _do_unescape(auth->short_desc);
	newauth->long_desc = _do_unescape(auth->long_desc);
	newauth->attr = _str2kva(auth->attr, KV_ASSIGN, KV_DELIMITER);
	return (newauth);
}


#ifdef DEBUG
void
print_authattr(authattr_t *auth)
{
	extern void print_kva(kva_t *);
	char *empty = "empty";

	if (auth == NULL) {
		printf("NULL\n");
		return;
	}

	printf("name=%s\n", auth->name ? auth->name : empty);
	printf("res1=%s\n", auth->res1 ? auth->res1 : empty);
	printf("res2=%s\n", auth->res2 ? auth->res2 : empty);
	printf("short_desc=%s\n", auth->short_desc ? auth->short_desc : empty);
	printf("long_desc=%s\n", auth->long_desc ? auth->long_desc : empty);
	printf("attr=\n");
	print_kva(auth->attr);
	fflush(stdout);
}
#endif  /* DEBUG */

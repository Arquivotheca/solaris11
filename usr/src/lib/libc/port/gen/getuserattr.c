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
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <nss_dbdefs.h>
#include <user_attr.h>
#include <getxby_door.h>
#include <pwd.h>

/* externs from libc */
extern void _nss_XbyY_fgets(FILE *, nss_XbyY_args_t *);

/* externs from parse.c */
extern char *_strtok_escape(char *, char *, char **);

userattr_t *_userstr2attr(userstr_t *);

static int userattr_stayopen;

/*
 * Unsynchronized, but it affects only
 * efficiency, not correctness
 */

static DEFINE_NSS_DB_ROOT(db_root);
static DEFINE_NSS_GETENT(context);


void
_nss_initf_userattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_USERATTR;
	p->config_name    = NSS_DBNAM_PASSWD; /* use config for "passwd" */
	p->default_config = NSS_DEFCONF_USERATTR;
}


/*
 * Return values: 0 = success, 1 = parse error, 2 = erange ...
 * The structure pointer passed in is a structure in the caller's space
 * wherein the field pointers would be set to areas in the buffer if
 * need be. instring and buffer should be separate areas.
 */
int
_str2userattr(const char *instr, int lenstr, void *ent, char *buf, int buflen)
{
	char		*last = NULL;
	char		*sep = KV_TOKEN_DELIMIT;
	userstr_t	*user = ent;

	if (lenstr >= buflen)
		return (NSS_STR_PARSE_ERANGE);

	if (instr != buf)
		(void) memcpy(buf, instr, lenstr);

	/* Terminate the buffer */
	buf[lenstr] = '\0';

	/* quick exit do not entry fill if not needed */
	if (ent == NULL)
		return (NSS_STR_PARSE_SUCCESS);

	user->name = _strtok_escape(buf, sep, &last);
	user->qualifier = _strtok_escape(NULL, sep, &last);
	user->res1 = _strtok_escape(NULL, sep, &last);
	user->res2 = _strtok_escape(NULL, sep, &last);
	user->attr = _strtok_escape(NULL, sep, &last);

	return (0);
}


void
setuserattr(void)
{
	userattr_stayopen = 0;
	nss_setent(&db_root, _nss_initf_userattr, &context);
}


void
enduserattr(void)
{
	userattr_stayopen = 0;
	nss_endent(&db_root, _nss_initf_userattr, &context);
	nss_delete(&db_root);
}


userstr_t *
_getuserattr(userstr_t *result, char *buffer, int buflen, int *h_errnop)
{
	nss_XbyY_args_t arg;
	nss_status_t    res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, _str2userattr);
	res = nss_getent(&db_root, _nss_initf_userattr, &context, &arg);
	arg.status = res;
	*h_errnop = arg.h_errno;
	return (NSS_XbyY_FINI(&arg));
}


static userstr_t *
_fgetuserattr(FILE *f, userstr_t *result, char *buffer, int buflen)
{
	nss_XbyY_args_t arg;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, _str2userattr);
	_nss_XbyY_fgets(f, &arg);
	return (NSS_XbyY_FINI(&arg));
}



static userstr_t *
_getusernam(const char *name, userstr_t *result, char *buffer, int buflen,
    int *errnop)
{
	nss_XbyY_args_t arg;
	nss_status_t    res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, _str2userattr);
	arg.key.name = name;
	arg.stayopen = userattr_stayopen;
	res = nss_search(&db_root, _nss_initf_userattr,
	    NSS_DBOP_USERATTR_BYNAME, &arg);
	arg.status = res;
	*errnop = arg.h_errno;
	return (NSS_XbyY_FINI(&arg));
}


userattr_t *
getuserattr(void)
{
	int		err = 0;
	char		buf[NSS_BUFLEN_USERATTR];
	userstr_t	user;
	userstr_t	*tmp;

	(void) memset(&user, 0, sizeof (userattr_t));
	tmp = _getuserattr(&user, buf, NSS_BUFLEN_USERATTR, &err);
	return (_userstr2attr(tmp));
}


userattr_t *
fgetuserattr(FILE *f)
{
	char		buf[NSS_BUFLEN_USERATTR];
	userstr_t	user;
	userstr_t	*tmp;

	(void) memset(&user, 0, sizeof (userattr_t));
	tmp = _fgetuserattr(f, &user, buf, NSS_BUFLEN_USERATTR);
	return (_userstr2attr(tmp));
}


userattr_t *
getusernam(const char *name)
{
	int		err = 0;
	char		buf[NSS_BUFLEN_USERATTR];
	userstr_t	user;
	userstr_t	*resptr = NULL;

	resptr = _getusernam(name, &user, buf, NSS_BUFLEN_USERATTR, &err);

	return (_userstr2attr(resptr));

}


userattr_t *
getuseruid(uid_t u)
{
	struct	passwd pwd;
	char	buf[NSS_BUFLEN_PASSWD];

	if (getpwuid_r(u, &pwd, buf, NSS_BUFLEN_PASSWD) == NULL)
		return (NULL);
	return (getusernam(pwd.pw_name));
}


void
free_userattr(userattr_t *user)
{
	if (user) {
		free(user->name);
		free(user->qualifier);
		free(user->res1);
		free(user->res2);
		_kva_free(user->attr);
		free(user);
	}
}


userattr_t *
_userstr2attr(userstr_t *user)
{
	userattr_t *newuser;

	if (user == NULL)
		return (NULL);

	if ((newuser = malloc(sizeof (userattr_t))) == NULL)
		return (NULL);

	newuser->name = _do_unescape(user->name);
	newuser->qualifier = _do_unescape(user->qualifier);
	newuser->res1 = _do_unescape(user->res1);
	newuser->res2 = _do_unescape(user->res2);
	newuser->attr = _str2kva(user->attr, KV_ASSIGN, KV_DELIMITER);
	return (newuser);
}


#ifdef DEBUG
void
print_userattr(userattr_t *user)
{
	extern void print_kva(kva_t *);
	char *empty = "empty";

	if (user == NULL) {
		printf("NULL\n");
		return;
	}

	printf("name=%s\n", user->name ? user->name : empty);
	printf("qualifier=%s\n", user->qualifier ? user->qualifier : empty);
	printf("res1=%s\n", user->res1 ? user->res1 : empty);
	printf("res2=%s\n", user->res2 ? user->res2 : empty);
	printf("attr=\n");
	print_kva(user->attr);
	fflush(stdout);
}
#endif  /* DEBUG */

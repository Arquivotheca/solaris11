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
#include <stdlib.h>
#include <sys/types.h>
#include <nss_dbdefs.h>
#include <string.h>
#include <bsm/libbsm.h>
#include <secdb.h>


/* externs from parse.c */
extern char *_strtok_escape(char *, char *, char **);

static int auuser_stayopen;

/*
 * Unsynchronized, but it affects only
 * efficiency, not correctness
 */

static DEFINE_NSS_DB_ROOT(db_root);
static DEFINE_NSS_GETENT(context);


void
_nss_initf_auuser(nss_db_params_t *p)
{
	p->name	= NSS_DBNAM_AUDITUSER;
	p->config_name    = NSS_DBNAM_PASSWD;  /* use config for "passwd" */
	p->default_config = NSS_DEFCONF_AUDITUSER;
}


/*
 * Return values: 0 = success, 1 = parse error, 2 = erange ...
 * The structure pointer passed in is a structure in the caller's space
 * wherein the field pointers would be set to areas in the buffer if
 * need be. instring and buffer should be separate areas.
 */
int
str2auuser(const char *instr, int lenstr, void *ent, char *buffer, int buflen)
{
	char		*last = NULL;
	char		*sep = KV_TOKEN_DELIMIT;
	au_user_str_t	*au_user = ent;

	if (lenstr >= buflen)
		return (NSS_STR_PARSE_ERANGE);

	if (instr != buffer)
		(void) strncpy(buffer, instr, buflen);

	/*
	 * Remove newline that nis (yp_match) puts at the
	 * end of the entry it retrieves from the map.
	 */
	if (buffer[lenstr] == '\n') {
		buffer[lenstr] = '\0';
	}

	/* quick exit do not entry fill if not needed */
	if (ent == NULL)
		return (NSS_STR_PARSE_SUCCESS);

	au_user->au_name = _strtok_escape(buffer, sep, &last);
	au_user->au_always = _strtok_escape(NULL, sep, &last);
	au_user->au_never = _strtok_escape(NULL, sep, &last);

	return (0);
}


void
_setauuser(void)
{
	auuser_stayopen = 0;
	nss_setent(&db_root, _nss_initf_auuser, &context);
}


int
_endauuser(void)
{
	auuser_stayopen = 0;
	nss_endent(&db_root, _nss_initf_auuser, &context);
	nss_delete(&db_root);
	return (0);
}


au_user_str_t *
_getauuserent(au_user_str_t *result, char *buffer, int buflen, int *h_errnop)
{
	nss_XbyY_args_t arg;
	nss_status_t    res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, str2auuser);
	res = nss_getent(&db_root, _nss_initf_auuser, &context, &arg);
	arg.status = res;
	*h_errnop = arg.h_errno;
	return (NSS_XbyY_FINI(&arg));
}


au_user_str_t *
_getauusernam(const char *name, au_user_str_t *result, char *buffer,
    int buflen, int *errnop)
{
	nss_XbyY_args_t arg;
	nss_status_t    res;

	if (result == NULL) {
		*errnop = AUDITUSER_PARSE_ERANGE;
		return (NULL);
	}
	NSS_XbyY_INIT(&arg, result, buffer, buflen, str2auuser);
	arg.key.name = name;
	arg.stayopen = auuser_stayopen;
	arg.h_errno = AUDITUSER_NOT_FOUND;
	res = nss_search(&db_root, _nss_initf_auuser,
	    NSS_DBOP_AUDITUSER_BYNAME, &arg);
	arg.status = res;
	*errnop = arg.h_errno;
	return (NSS_XbyY_FINI(&arg));
}

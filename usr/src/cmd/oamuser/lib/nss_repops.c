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
#include <nssec.h>
#include <stdlib.h>
#include <nsswitch.h>
#include <strings.h>
#include "repops.h"

static int nss_get_pwnam(char *, struct passwd **, nss_XbyY_buf_t *);
static int nss_get_pwid(uid_t, struct passwd **, nss_XbyY_buf_t *);
static struct passwd *nss_get_pwent(nss_XbyY_buf_t *);
static void nss_set_pwent();
static void nss_end_pwent();

static int nss_get_spnam(char *, struct spwd **, nss_XbyY_buf_t *);
static struct spwd *nss_get_spent(nss_XbyY_buf_t *);
static void nss_set_spent();
static void nss_end_spent();


static int nss_get_usernam(char *, userattr_t **, nss_XbyY_buf_t *);
static int nss_get_useruid(uid_t, userattr_t **, nss_XbyY_buf_t *);
static userattr_t *nss_get_userattr(nss_XbyY_buf_t *);
static void nss_set_userattr();
static void nss_end_userattr();

static int nss_get_grnam(char *, struct group **, nss_XbyY_buf_t *);
static int nss_get_grgid(gid_t, struct group **, nss_XbyY_buf_t *);
static struct group *nss_get_group(nss_XbyY_buf_t *);
static void nss_set_group();
static void nss_end_group();


static int nss_get_projnam(char *, struct project **, nss_XbyY_buf_t *);
static int nss_get_projid(projid_t, struct project **, nss_XbyY_buf_t *);
static struct project *nss_get_project(nss_XbyY_buf_t *);
static void nss_set_project();
static void nss_end_project();

static int nss_get_profnam(char *, profattr_t **, nss_XbyY_buf_t *);
static profattr_t *nss_get_profattr(nss_XbyY_buf_t *);
static void nss_set_profattr();
static void nss_end_profattr();

static int nss_get_authnam(char *, authattr_t **, nss_XbyY_buf_t *);
static authattr_t *nss_get_authattr(nss_XbyY_buf_t *);
static void nss_set_authattr();
static void nss_end_authattr();
static execattr_t *nss_get_execprof(char *, char *, char *, int,
    nss_XbyY_buf_t *);
static execattr_t *nss_get_execuser(char *, char *, char *, int,
    nss_XbyY_buf_t *);
static execattr_t *nss_get_execattr();
static void nss_set_execattr();
static void nss_end_execattr();

sec_repops_t sec_nss_rops = {
	nss_get_pwnam,
	nss_get_pwid,
	nss_get_pwent,
	nss_set_pwent,
	nss_end_pwent,
	nss_get_spnam,
	nss_get_spent,
	nss_set_spent,
	nss_end_spent,
	nss_get_usernam,
	nss_get_useruid,
	nss_get_userattr,
	nss_set_userattr,
	nss_end_userattr,
	nss_get_grnam,
	nss_get_grgid,
	NULL,
	nss_get_group,
	nss_set_group,
	nss_end_group,
	nss_get_projnam,
	nss_get_projid,
	NULL,
	nss_get_project,
	nss_set_project,
	nss_end_project,
	nss_get_profnam,
	NULL,
	nss_get_profattr,
	nss_set_profattr,
	nss_end_profattr,
	nss_get_execprof,
	nss_get_execuser,
	NULL,
	nss_get_execattr,
	nss_set_execattr,
	nss_end_execattr,
	nss_get_authnam,
	NULL,
	nss_get_authattr,
	nss_set_authattr,
	nss_end_authattr,
	/* do not have nss routines implemented for tnrhtp. */
	NULL,
	NULL,
	NULL,
	NULL
};

static nss_db_root_t nss_db_root = NSS_DB_ROOT_INIT;
static nss_getent_t nss_context = NSS_GETENT_INIT;
static char *_nsw_search_path = NULL;


void
nss_initf_nss_execattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_EXECATTR;
	p->config_name    = NSS_DBNAM_PROFATTR; /* use config for "prof_attr" */
}

void
nss_initf_nsw_execattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_EXECATTR;
	p->config_name    = NSS_DBNAM_PROFATTR; /* use config for "prof_attr" */
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = _nsw_search_path;
}

void
nss_initf_nsw_profattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_PROFATTR;
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = _nsw_search_path;
}

static void nss_sec_initf_profattr(nss_db_params_t *p)
{
	p->name    = NSS_DBNAM_PROFATTR;
	p->default_config = NSS_DEFCONF_PROFATTR;
}

/*ARGSUSED*/
static int
nss_get_pwnam(char *name, struct passwd **result, nss_XbyY_buf_t *bufpp)
{
	int rc = SEC_REP_NOT_FOUND;

	if (name && result) {
		*result = getpwnam(name);
		if (*result)
			rc = SEC_REP_SUCCESS;
	} else
			rc = SEC_REP_INVALID_ARG;
	return (rc);
}

/*ARGSUSED*/
static int
nss_get_pwid(uid_t uid, struct passwd **result, nss_XbyY_buf_t *bufpp)
{
	int rc = SEC_REP_NOT_FOUND;

	if (result) {
		*result = getpwuid(uid);
		if (*result)
			rc = SEC_REP_SUCCESS;
	} else {
		rc = SEC_REP_INVALID_ARG;
	}
	return (rc);
}

/*ARGSUSED*/
static struct passwd *
nss_get_pwent(nss_XbyY_buf_t *bufpp)
{
	return (getpwent());
}

static void
nss_set_pwent()
{
	(void) setpwent();
}

static void
nss_end_pwent()
{
	(void) endpwent();
}

/*ARGSUSED*/
static int
nss_get_spnam(char *name, struct spwd **result, nss_XbyY_buf_t *bufpp)
{
	int rc = SEC_REP_INVALID_ARG;
	if (name && result) {
		*result = getspnam(name);
		if (*result)
			rc = SEC_REP_SUCCESS;
		else
			rc = SEC_REP_NOT_FOUND;
	}
	return (rc);
}

/*ARGSUSED*/
static struct spwd *
nss_get_spent(nss_XbyY_buf_t *bufpp)
{
	return (getspent());
}

static void
nss_set_spent()
{
	setspent();
}

static void
nss_end_spent()
{
	endspent();
}

/*ARGSUSED*/
static int
nss_get_usernam(char *name, userattr_t **result, nss_XbyY_buf_t *bufpp)
{
	int rc = SEC_REP_INVALID_ARG;
	if (name && result) {
		*result = getusernam(name);
		if (*result)
			rc = SEC_REP_SUCCESS;
		else
			rc = SEC_REP_NOT_FOUND;
	}
	return (rc);
}

/*ARGSUSED*/
static int
nss_get_useruid(uid_t uid, userattr_t **result, nss_XbyY_buf_t *bufpp)
{
	int rc = SEC_REP_INVALID_ARG;
	if (result) {
		*result = getuseruid(uid);
		if (*result)
			rc = SEC_REP_SUCCESS;
		else
			rc = SEC_REP_NOT_FOUND;
	}
	return (rc);
}

/*ARGSUSED*/
static userattr_t *
nss_get_userattr(nss_XbyY_buf_t *bufpp)
{
	return (getuserattr());
}

static void
nss_set_userattr()
{
	setuserattr();
}

static void
nss_end_userattr()
{
	enduserattr();
}

/*ARGSUSED*/
static struct group *
nss_get_group(nss_XbyY_buf_t *bufpp)
{
	return (getgrent());
}

/*ARGSUSED*/
static int
nss_get_grnam(char *name, struct group **result, nss_XbyY_buf_t *bufpp)
{
	int rc = SEC_REP_INVALID_ARG;
	struct group *grp = NULL;
	if (name && result) {
		grp = (struct group *)calloc(1, sizeof (struct group));
		if (!grp)
			return (SEC_REP_NOMEM);
		*result = getgrnam_r(name, grp, bufpp->buffer, bufpp->buflen);
		if (*result != NULL)
			rc = SEC_REP_SUCCESS;
		else
			rc = SEC_REP_NOT_FOUND;
	}
	return (rc);
}

/*ARGSUSED*/
static int
nss_get_grgid(gid_t gid, struct group **result, nss_XbyY_buf_t *bufpp)
{

	int rc = SEC_REP_INVALID_ARG;
	struct group *grp = NULL;

	if (result) {
		grp = (struct group *)calloc(1, sizeof (struct group));
		if (!grp)
			return (SEC_REP_NOMEM);
		*result = getgrgid_r(gid, grp, bufpp->buffer, bufpp->buflen);
		if (*result != NULL)
			rc = SEC_REP_SUCCESS;
		else
			rc = SEC_REP_NOT_FOUND;
	}
	return (rc);
}

static void
nss_set_group()
{
	setgrent();
}

static void
nss_end_group()
{
	endgrent();
}

/*ARGSUSED*/
static int
nss_get_projnam(char *name, struct project **result, nss_XbyY_buf_t *bufpp)
{
	int rc = SEC_REP_INVALID_ARG;
	struct project *proj;

	if (name && result) {
		proj = (struct project *)calloc(1, sizeof (struct project));
		if (!proj)
			return (SEC_REP_NOMEM);
		*result = getprojbyname(name, proj, bufpp->buffer,
		    bufpp->buflen);
		if (*result != NULL)
			rc = SEC_REP_SUCCESS;
		else
			rc = SEC_REP_NOT_FOUND;
	}
	return (rc);
}

/*ARGSUSED*/
static int
nss_get_projid(projid_t projid, struct project **result, nss_XbyY_buf_t *bufpp)
{
	int rc = SEC_REP_INVALID_ARG;
	struct project *proj;

	if (result) {
		proj = (struct project *)calloc(1, sizeof (struct project));
		if (!proj)
			return (SEC_REP_NOMEM);
		*result = getprojbyid(projid, proj, bufpp->buffer,
		    bufpp->buflen);
		if (*result != NULL)
			rc = SEC_REP_SUCCESS;
		else
			rc = SEC_REP_NOT_FOUND;
	}
	return (rc);
}

/*ARGSUSED*/
static struct project *
nss_get_project(nss_XbyY_buf_t *bufpp)
{
	char buffer[NSS_BUFLEN_PROJECT];
	struct project proj;

	return (getprojent(&proj, buffer, NSS_BUFLEN_PROJECT));

}

static void
nss_set_project()
{
	setprojent();
}

static void
nss_end_project()
{
	endprojent();
}

/*ARGSUSED*/
static int
nss_get_profnam(char *name, profattr_t **result, nss_XbyY_buf_t *bufpp)
{
	int rc = SEC_REP_INVALID_ARG;

	if (name && result) {
		*result = getprofnam(name);
		if (*result)
			rc = SEC_REP_SUCCESS;
		else
			rc = SEC_REP_NOT_FOUND;
	}
	return (rc);
}

/*ARGSUSED*/
static profattr_t *
nss_get_profattr(nss_XbyY_buf_t *bufpp)
{
	nss_XbyY_args_t arg;
	profstr_t *prof = NULL;

	NSS_XbyY_INIT(&arg, bufpp->result, bufpp->buffer, bufpp->buflen,
	    _str2profattr);
	(void) nss_getent(&nss_db_root, nss_sec_initf_profattr,
	    &nss_context, &arg);

	prof = (profstr_t *)NSS_XbyY_FINI(&arg);
	return ((profattr_t *)_profstr2attr(prof));
}

static void
nss_set_profattr()
{
	nss_setent(&nss_db_root, nss_sec_initf_profattr, &nss_context);
}

static void
nss_end_profattr()
{
	nss_endent(&nss_db_root, nss_sec_initf_profattr, &nss_context);
	nss_delete(&nss_db_root);
}

/*ARGSUSED*/
static int
nss_get_authnam(char *name, authattr_t **result, nss_XbyY_buf_t *bufpp)
{
	int rc = SEC_REP_INVALID_ARG;

	if (name && result) {
		*result = getauthnam(name);
		if (*result)
			rc = SEC_REP_SUCCESS;
		else
			rc = SEC_REP_NOT_FOUND;
	}
	return (rc);
}

/*ARGSUSED*/
static authattr_t *
nss_get_authattr(nss_XbyY_buf_t *bufpp)
{
	return (getauthattr());
}

static void
nss_set_authattr()
{
	setauthattr();
}

static void
nss_end_authattr()
{
	endauthattr();
}

static execattr_t *
nss_get_execprof(char *profname, char *type, char *id, int search_flag,
    nss_XbyY_buf_t *b)
{

	struct nss_calls nss_calls;

	nss_calls.initf_nss_exec = nss_initf_nss_execattr;
	nss_calls.initf_nsw_prof = nss_initf_nsw_profattr;
	nss_calls.initf_nsw_exec = nss_initf_nsw_execattr;
	nss_calls.pnsw_search_path = &_nsw_search_path;
	return (get_execprof(profname, type, id, search_flag, b, &nss_calls));
}

/*ARGSUSED*/
static execattr_t *
nss_get_execuser(char *username, char *type, char *id,
    int search_flag, nss_XbyY_buf_t *bufpp)
{
	return (getexecuser(username, type, id, search_flag));
}

static execattr_t *nss_get_execattr()
{
	nss_XbyY_buf_t *bufpp = NULL;
	nss_XbyY_args_t arg;
	execstr_t *exc = NULL;

	init_nss_buffer(SEC_REP_DB_EXECATTR, &bufpp);

	NSS_XbyY_INIT(&arg, bufpp->result, bufpp->buffer, bufpp->buflen,
	    _str2execattr);
	(void) nss_getent(&nss_db_root, nss_initf_nss_execattr,
	    &nss_context, &arg);

	exc = (execstr_t *)NSS_XbyY_FINI(&arg);
	return ((execattr_t *)_execstr2attr(exc, exc));
}

static void
nss_set_execattr()
{
	nss_setent(&nss_db_root, nss_initf_nss_execattr, &nss_context);
}

static void
nss_end_execattr()
{
	nss_endent(&nss_db_root, nss_initf_nss_execattr, &nss_context);
	nss_delete(&nss_db_root);
}

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
#include <prof_attr.h>
#include <getxby_door.h>
#include <sys/mman.h>

/* externs from parse.c */
extern char *_strtok_escape(char *, char *, char **);

static int profattr_stayopen;
/*
 * Unsynchronized, but it affects only
 * efficiency, not correctness
 */

static DEFINE_NSS_DB_ROOT(db_root);
static DEFINE_NSS_GETENT(context);

void
_nss_initf_profattr(nss_db_params_t *p)
{
	p->name    = NSS_DBNAM_PROFATTR;
	p->default_config = NSS_DEFCONF_PROFATTR;
}


/*
 * Return values: 0 = success, 1 = parse error, 2 = erange ...
 * The structure pointer passed in is a structure in the caller's space
 * wherein the field pointers would be set to areas in the buffer if
 * need be. instring and buffer should be separate areas.
 */
int
_str2profattr(const char *instr, int lenstr, void *ent, char *buffer,
    int buflen)
{
	char		*last = NULL;
	char		*sep = KV_TOKEN_DELIMIT;
	profstr_t	*prof = ent;

	if (lenstr >= buflen)
		return (NSS_STR_PARSE_ERANGE);

	if (instr != buffer)
		(void) memcpy(buffer, instr, lenstr);

	/* Terminate the buffer */
	buffer[lenstr] = '\0';

	/* quick exit do not entry fill if not needed */
	if (ent == NULL)
		return (NSS_STR_PARSE_SUCCESS);

	prof->name = _strtok_escape(buffer, sep, &last);
	prof->res1 = _strtok_escape(NULL, sep, &last);
	prof->res2 = _strtok_escape(NULL, sep, &last);
	prof->desc = _strtok_escape(NULL, sep, &last);
	prof->attr = _strtok_escape(NULL, sep, &last);

	return (0);
}


void
setprofattr(void)
{
	profattr_stayopen = 0;
	nss_setent(&db_root, _nss_initf_profattr, &context);
}


void
endprofattr(void)
{
	profattr_stayopen = 0;
	nss_endent(&db_root, _nss_initf_profattr, &context);
	nss_delete(&db_root);
}


static profstr_t *
_getprofattr(profstr_t *result, char *buffer, int buflen, int *h_errnop)
{
	nss_XbyY_args_t arg;
	nss_status_t    res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, _str2profattr);
	res = nss_getent(&db_root, _nss_initf_profattr, &context, &arg);
	arg.status = res;
	*h_errnop = arg.h_errno;
	return (NSS_XbyY_FINI(&arg));
}


static profstr_t *
_getprofnam(const char *name, profstr_t *result, char *buffer, int buflen,
    int *errnop)
{
	nss_XbyY_args_t arg;
	nss_status_t    res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, _str2profattr);
	arg.key.name = name;
	arg.stayopen = profattr_stayopen;
	res = nss_search(&db_root, _nss_initf_profattr,
	    NSS_DBOP_PROFATTR_BYNAME, &arg);
	arg.status = res;
	*errnop = arg.h_errno;
	return (NSS_XbyY_FINI(&arg));
}

profattr_t *_profstr2attr(profstr_t *);

profattr_t *
getprofattr(void)
{
	int		err = 0;
	char		buf[NSS_BUFLEN_PROFATTR];
	profstr_t	prof;
	profstr_t	*tmp;

	tmp = _getprofattr(&prof, buf, NSS_BUFLEN_PROFATTR, &err);
	return (_profstr2attr(tmp));
}


profattr_t *
getprofnam(const char *name)
{
	int		err = 0;
	char		buf[NSS_BUFLEN_PROFATTR];
	profstr_t	prof;
	profstr_t	*resptr = NULL;

	(void) memset(&prof, 0, sizeof (profstr_t));

	resptr = _getprofnam(name, &prof, buf, NSS_BUFLEN_PROFATTR, &err);

	return (_profstr2attr(resptr));

}

void
free_profattr(profattr_t *prof)
{
	if (prof) {
		free(prof->name);
		free(prof->res1);
		free(prof->res2);
		free(prof->desc);
		_kva_free(prof->attr);
		free(prof);
	}
}


profattr_t *
_profstr2attr(profstr_t *prof)
{
	profattr_t *newprof;

	if (prof == NULL)
		return (NULL);

	if ((newprof = malloc(sizeof (profattr_t))) == NULL)
		return (NULL);

	newprof->name = _do_unescape(prof->name);
	newprof->res1 = _do_unescape(prof->res1);
	newprof->res2 = _do_unescape(prof->res2);
	newprof->desc = _do_unescape(prof->desc);
	newprof->attr = _str2kva(prof->attr, KV_ASSIGN, KV_DELIMITER);
	return (newprof);
}


extern int _enum_common_p(const char *, int (*)(const char *, kva_t *, void *,
    void *), void *, void *, boolean_t, int *, char *[MAXPROFS]);

/*
 * Given a profile name, gets the list of profiles found from
 * the whole hierarchy, using the given profile as root
 */
void
getproflist(const char *profileName, char **profArray, int *profcnt)
{
	/* There can't be a "," in a profile name. */
	if (strchr(profileName, KV_SEPCHAR) != NULL)
		return;

	(void) _enum_common_p(profileName, NULL, NULL, NULL, B_FALSE,
	    profcnt, profArray);
}

void
free_proflist(char **profArray, int profcnt)
{
	int i;
	for (i = 0; i < profcnt; i++) {
		free(profArray[i]);
	}
}


#ifdef DEBUG
void
print_profattr(profattr_t *prof)
{
	extern void print_kva(kva_t *);
	char *empty = "empty";

	if (prof == NULL) {
		printf("NULL\n");
		return;
	}

	printf("name=%s\n", prof->name ? prof->name : empty);
	printf("res1=%s\n", prof->res1 ? prof->res1 : empty);
	printf("res2=%s\n", prof->res2 ? prof->res2 : empty);
	printf("desc=%s\n", prof->desc ? prof->desc : empty);
	printf("attr=\n");
	print_kva(prof->attr);
	fflush(stdout);
}
#endif  /* DEBUG */

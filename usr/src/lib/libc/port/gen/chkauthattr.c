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
#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pwd.h>
#include <nss_common.h>
#include <nss_dbdefs.h>
#include <deflt.h>
#include <auth_attr.h>
#include <prof_attr.h>
#include <user_attr.h>

#define	COPYTOSTACK(dst, csrc)		{	\
		size_t len = strlen(csrc) + 1;	\
		dst = alloca(len);		\
		(void) memcpy(dst, csrc, len);	\
	}

static kva_t *get_default_attrs(void);
static void free_default_attrs(kva_t *);
static int is_cons_user(uid_t);
static const char solaris[] = "solaris.*";

/*
 * Enumeration functions for auths and profiles; the enumeration functions
 * take a callback with four arguments:
 *	const char *		profile name (or NULL unless wantattr is false)
 *	kva_t *			attributes (or NULL unless wantattr is true)
 *	void *			context
 *	void *			pointer to the result
 * When the call back returns non-zero, the enumeration ends.
 * The function might be NULL but only for profiles as we are always collecting
 * all the profiles.
 * Both the auths and the profiles arguments may be NULL.
 *
 * These should be the only implementation of the algorithm of "finding me
 * all the profiles/authorizations/keywords/etc."
 */

#define	CONSUSER_PROFILE_KW		"consprofile"
#define	DEF_LOCK_AFTER_RETRIES		"LOCK_AFTER_RETRIES="

static struct dfltplcy {
	char *attr;
	const char *defkw;
} dfltply[] = {
	{ CONSUSER_PROFILE_KW,			DEF_CONSUSER},
	{ PROFATTR_AUTHS_KW,			DEF_AUTH},
	{ PROFATTR_PROFS_KW,			DEF_PROF},
	{ USERATTR_LIMPRIV_KW,			DEF_LIMITPRIV},
	{ USERATTR_DFLTPRIV_KW,			DEF_DFLTPRIV},
	{ USERATTR_LOCK_AFTER_RETRIES_KW,	DEF_LOCK_AFTER_RETRIES}
};

#define	NDFLTPLY	(sizeof (dfltply)/sizeof (struct dfltplcy))
#define	GETCONSPROF(a)	(kva_match((a), CONSUSER_PROFILE_KW))
#define	GETPROF(a)	(kva_match((a), PROFATTR_PROFS_KW))

/*
 * Enumerate profiles from listed profiles.
 */
static int _auth_match_noun(const char *, const char *, size_t, const char *);

int
_enum_common_p(const char *cprofiles,
    int (*cb)(const char *, kva_t *, void *, void *),
    void *ctxt, void *pres, boolean_t wantattr,
    int *pcnt, char *profs[MAXPROFS])
{
	char *prof, *last;
	char *profiles;
	profattr_t *pa;
	int i;
	int res = 0;

	if (cprofiles == NULL)
		return (0);

	if (*pcnt > 0 && strcmp(profs[*pcnt - 1], PROFILE_STOP) == NULL)
		return (0);

	COPYTOSTACK(profiles, cprofiles)

	while (prof = strtok_r(profiles, KV_SEPSTR, &last)) {

		profiles = NULL;	/* For next iterations of strtok_r */

		for (i = 0; i < *pcnt; i++)
			if (strcmp(profs[i], prof) == 0)
				goto cont;

		if (*pcnt >= MAXPROFS)		/* oops: too many profs */
			return (-1);

		/* Add it */
		profs[(*pcnt)++] = strdup(prof);

		if (strcmp(profs[*pcnt - 1], PROFILE_STOP) == 0)
			break;

		/* find the profiles for this profile */
		pa = getprofnam(prof);

		if (cb != NULL && (!wantattr || pa != NULL && pa->attr != NULL))
			res = cb(prof, pa ? pa->attr : NULL, ctxt, pres);

		if (pa != NULL) {
			if (res == 0 && pa->attr != NULL) {
				res = _enum_common_p(GETPROF(pa->attr), cb,
				    ctxt, pres, wantattr, pcnt, profs);
			}
			free_profattr(pa);
		}
		if (res != 0)
			return (res);
cont:
		continue;
	}
	return (res);
}

/*
 * Enumerate all attributes associated with a username and the profiles
 * associated with the user.
 */
static int
_enum_common(const char *username,
    int (*cb)(const char *, kva_t *, void *, void *),
    void *ctxt, void *pres, boolean_t wantattr)
{
	userattr_t *ua;
	int res = 0;
	int cnt = 0;
	char *profs[MAXPROFS];
	profattr_t *dp;
	struct passwd pw;
	char pwbuf[NSS_BUFLEN_PASSWD];

	if (cb == NULL)
		return (-1);

	ua = getusernam(username);

	if (ua != NULL) {
		if (ua->attr != NULL) {
			if (wantattr)
				res = cb(NULL, ua->attr, ctxt, pres);
			if (res == 0) {
				res = _enum_common_p(GETPROF(ua->attr),
				    cb, ctxt, pres, wantattr, &cnt, profs);
			}
		}
		free_userattr(ua);
		if (res != 0) {
			free_proflist(profs, cnt);
			return (res);
		}
	}

	/*
	 * Find the default profiles if this is a valid user and we
	 * didn't encounter the Stop profile.
	 */
	if (username != NULL &&
	    (cnt == 0 || strcmp(profs[cnt-1], PROFILE_STOP) != 0) &&
	    getpwnam_r(username, &pw, pwbuf, sizeof (pwbuf)) != NULL &&
	    (dp = getprofnam(AUTH_POLICY)) != NULL) {

		if (is_cons_user(pw.pw_uid)) {
			res = _enum_common_p(GETCONSPROF(dp->attr), cb,
			    ctxt, pres, wantattr, &cnt, profs);
		}

		if (res == 0) {
			res = _enum_common_p(GETPROF(dp->attr), cb, ctxt,
			    pres, wantattr, &cnt, profs);
		}

		if (res == 0 && wantattr)
			res = cb(NULL, dp->attr, ctxt, pres);

		free_profattr(dp);
	}

	free_proflist(profs, cnt);

	return (res);
}

/*
 * Enumerate profiles with a username argument.
 */
int
_enum_profs(const char *username,
    int (*cb)(const char *, kva_t *, void *, void *),
    void *ctxt, void *pres)
{
	return (_enum_common(username, cb, ctxt, pres, B_FALSE));
}

/*
 * Enumerate attributes with a username argument.
 */
int
_enum_attrs(const char *username,
    int (*cb)(const char *, kva_t *, void *, void *),
    void *ctxt, void *pres)
{
	return (_enum_common(username, cb, ctxt, pres, B_TRUE));
}


/*
 * Magic struct and function to allow using the _enum_attrs functions to
 * enumerate the authorizations.  In order to make the system survive
 * bad configuration, we make sure that root always has the "solaris.*"
 * authorization.  This is implemented by first marking "wantdef" to
 * true when the user is root; while we enumerating the auths, we compare
 * and set wantdef to false if "solaris.*" is encountered.  If wantdef
 * remains true and callback hasn't short circuited, call the callback with
 * "solaris.*".
 */
typedef struct ccomm2auth {
	int (*cb)(const char *, void *, void *);
	void *ctxt;
	boolean_t wantdef;
} ccomm2auth;

/*ARGSUSED*/
static int
comm2auth(const char *name, kva_t *attr, void *ctxt, void *pres)
{
	ccomm2auth *ca = ctxt;
	char *cauths;
	char *auth, *last, *auths;
	int res = 0;

	/* Note: PROFATTR_AUTHS_KW is equal to USERATTR_AUTHS_KW */
	cauths = kva_match(attr, PROFATTR_AUTHS_KW);

	if (cauths == NULL)
		return (0);

	COPYTOSTACK(auths, cauths)

	while (auth = strtok_r(auths, KV_SEPSTR, &last)) {
		auths = NULL;		/* For next iterations of strtok_r */

		res = ca->cb(auth, ca->ctxt, pres);

		if (res != 0)
			return (res);

		if (ca->wantdef && strcmp(auth, solaris) == 0)
			ca->wantdef = B_FALSE;
	}
	return (res);
}

/*
 * Enumerate authorizations for username.
 */
int
_enum_auths(const char *username,
    int (*cb)(const char *, void *, void *),
    void *ctxt, void *pres)
{
	ccomm2auth c2a;
	int res;

	if (cb == NULL)
		return (-1);

	c2a.cb = cb;
	c2a.ctxt = ctxt;
	c2a.wantdef = strcmp(username, "root") == 0;

	res = _enum_common(username, comm2auth, &c2a, pres, B_TRUE);

	if (res == 0 && c2a.wantdef)
		res = cb(solaris, ctxt, pres);
	return (res);
}

int
_auth_match_noun(const char *pattern, const char *auth,
    size_t auth_len, const char *auth_noun)
{
	size_t pattern_len;
	char *pattern_noun;
	char *slash;

	pattern_len = strlen(pattern);
	/*
	 * If the specified authorization has a trailing object
	 * and the current authorization we're checking also has
	 * a trailing object, the object names must match.
	 *
	 * If there is no object name failure, then we must
	 * check for an exact match of the two authorizations
	 */
	if (auth_noun != NULL) {
		if ((slash = strchr(pattern, KV_OBJECTCHAR)) != NULL) {
			pattern_noun = slash + 1;
			pattern_len -= strlen(slash);
			if (strcmp(pattern_noun, auth_noun) != 0)
				return (0);
		} else if ((auth_len == pattern_len) &&
		    (strncmp(pattern, auth, pattern_len) == 0)) {
			return (1);
		}
	}

	/*
	 * If the wildcard is not in the last position in the string, don't
	 * match against it.
	 */
	if (pattern[pattern_len-1] != KV_WILDCHAR)
		return (0);

	/*
	 * If the strings are identical up to the wildcard
	 * then we have a match.
	 * For more info see LSARC 2008/332.
	 */
	if (strncmp(pattern, auth, pattern_len - 1) == 0)
		return (1);
	return (0);
}

int
_auth_match(const char *pattern, const char *auth)
{
	return (_auth_match_noun(pattern, auth, strlen(auth), NULL));
}

static int
_is_authorized(const char *auth, void *authname, void *res)
{
	int *resp = res;
	char	*authname_noun;
	char	*slash;
	size_t	auth_len;
	size_t	noun_len;

	auth_len = strlen(authname);
	if ((slash = strchr(authname, KV_OBJECTCHAR)) != NULL) {
		authname_noun = slash + 1;
		noun_len = strlen(slash);
		auth_len -= noun_len;
	} else {
		authname_noun = NULL;
	}

	if (strcmp(authname, auth) == 0) {
		/* exact match, we're done */
		*resp = 1;
		return (1);
	} else if (noun_len || strchr(auth, KV_WILDCHAR) != NULL) {
		if (_auth_match_noun(auth, authname,
		    auth_len, authname_noun)) {
			*resp = 1;
			return (1);
		}
	}

	return (0);
}

int
chkauthattr(const char *authname, const char *username)
{
	int		auth_granted = 0;

	if (authname == NULL || username == NULL)
		return (0);

	(void) _enum_auths(username, _is_authorized, (char *)authname,
	    &auth_granted);

	return (auth_granted);
}

#define	CONSOLE_USER_LINK "/dev/vt/console_user"

static int
is_cons_user(uid_t uid)
{
	struct stat	cons;

	if (stat(CONSOLE_USER_LINK, &cons) == -1)
		return (0);

	return (uid == cons.st_uid);
}

static void
free_default_attrs(kva_t *kva)
{
	int i;

	for (i = 0; i < kva->length; i++)
		free(kva->data[i].value);

	free(kva);
}

/*
 * Return the default attributes; these are ignored when a STOP profile
 * was found.
 */
static kva_t *
get_default_attrs(void)
{
	void *defp;
	kva_t *kva;
	int i;

	kva = malloc(sizeof (kva_t) + sizeof (kv_t) * NDFLTPLY);

	if (kva == NULL)
		return (NULL);

	kva->data = (kv_t *)(void *)&kva[1];
	kva->length = 0;

	if ((defp = defopen_r(AUTH_POLICY)) == NULL)
		goto return_null;

	for (i = 0; i < NDFLTPLY; i++) {
		char *cp = defread_r(dfltply[i].defkw, defp);

		if (cp == NULL)
			continue;
		if ((cp = strdup(cp)) == NULL)
			goto return_null;

		kva->data[kva->length].key = dfltply[i].attr;
		kva->data[kva->length++].value = cp;
	}

	(void) defclose_r(defp);
	return (kva);

return_null:
	if (defp != NULL)
		(void) defclose_r(defp);

	free_default_attrs(kva);
	return (NULL);
}

/*
 * Map the contents of AUTH_POLICY to a profile called
 * AUTH_POLICY.  Even when we're not nscd we just cache
 * the data: it's not that much and usually this code
 * is only run inside nscd.
 */
nss_status_t
__get_default_prof(const char *prof, nss_XbyY_args_t *args)
{
	static struct stat lasttime;
	static char *kvstr;
	static mutex_t defprof = DEFAULTMUTEX;

	struct stat stbuf;
	kva_t *kva;
	char buf[NSS_BUFLEN_PROFATTR];
	size_t len;
	nss_status_t res;

	if (strcmp(prof, AUTH_POLICY) != 0)
		return (NSS_NOTFOUND);

	if (stat(AUTH_POLICY, &stbuf) == -1)
		return (NSS_NOTFOUND);

	(void) mutex_lock(&defprof);

	if (kvstr == NULL || stbuf.st_mtime != lasttime.st_mtime ||
	    stbuf.st_size != lasttime.st_size) {
		int kres;

		kva = get_default_attrs();
		if (kva == NULL) {
			(void) mutex_unlock(&defprof);
			return (NSS_NOTFOUND);
		}

		len = snprintf(buf, sizeof (buf), "%s::::", prof);
		kres = _kva2str(kva, buf + len, sizeof (buf) - len,
		    KV_ASSIGN, KV_DELIMITER);

		free_default_attrs(kva);

		if (kres != 0) {
			(void) mutex_unlock(&defprof);
			return (NSS_NOTFOUND);
		}

		if (kvstr != NULL)
			free(kvstr);

		kvstr = strdup(buf);
		if (kvstr == NULL) {
			(void) mutex_unlock(&defprof);
			return (NSS_NOTFOUND);
		}
		lasttime = stbuf;
	}

	len = strlen(kvstr);
	res = args->str2ent(kvstr, len, args->buf.result,
	    args->buf.buffer, args->buf.buflen);

	(void) mutex_unlock(&defprof);

	if (res != NSS_STR_PARSE_SUCCESS)
		return (NSS_NOTFOUND);

	args->returnval = (args->buf.result != NULL) ?
	    args->buf.result : args->buf.buffer;
	args->returnlen = len;

	return (NSS_SUCCESS);
}

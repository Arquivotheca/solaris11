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

#include "files_common.h"
#include <prof_attr.h>
#include <string.h>
#include <stdlib.h>
#include <alloca.h>
#include <secdb.h>

#define	ISEMPTY(str)	((str) == NULL || *(str) == '\0')

/*
 *    files/getprofattr.c --
 *           "files" backend for nsswitch "prof_attr" database
 */

static files_hash_func hash_prof[1] = { hash_name };

static files_hash_t hashinfo = {
	DEFAULTMUTEX,
	sizeof (profattr_t),
	NSS_LINELEN_PROFATTR,
	sizeof (hash_prof)/sizeof (files_hash_func),
	hash_prof
};

extern nss_status_t __get_default_prof(const char *, nss_XbyY_args_t *);

static nss_status_t
getbyname(files_backend_ptr_t be, void *arg)
{
	nss_XbyY_args_t *a = arg;

	if (a->key.name != NULL && *a->key.name == '/' &&
	    __get_default_prof(a->key.name, a) == NSS_SUCCESS) {
		return (NSS_SUCCESS);
	}
	return (_nss_files_XY_hash(be, a, 1, &hashinfo, 0,
	    _nss_files_check_name_colon));
}

static files_backend_op_t profattr_ops[] = {
	_nss_files_destr,
	_nss_files_endent,
	_nss_files_setent,
	_nss_files_getent_netdb,
	getbyname
};

/*
 * Push all the prof/user_attrs with the same key for later merging.
 * NOTE user_attr and prof_attr are similar structures with the interesting
 * fields, attrs, at the same place.  We define yet another structure,
 * attrstr_t so we can use an index and we can use _str2profattr()
 * to parse this structure as it structures have five char *s.
 */

#define	AIND		4
#define	ATTRSIZE	5

typedef struct attrstr {
	char	*f[ATTRSIZE];
}  attrstr_t;


/* Merge attr2 into attr1; attr1 needs to be freed */
static char *
mergeattr(char *attr1, char *attr2, int flags)
{
	kva_t *kv1 = _str2kva(attr1, KV_ASSIGN, KV_DELIMITER);
	kva_t *kv2 = _str2kva(attr2, KV_ASSIGN, KV_DELIMITER);
	int i;
	int len;
	kv_t *newkv;
	char buf[NSS_BUFLEN_PROFATTR];
	int res;

	/*
	 * Three things to merge for profiles
	 *  PROFATTR_AUTHS_KW PROFATTR_PROFS_KW PROFATTR_PRIVS_KW
	 * No merging for user_attr; we only copy missed key/value
	 * pairs.
	 */
	if (kv1 == NULL || kv2 == NULL) {
		_kva_free(kv1);
		_kva_free(kv2);
		return (NULL);
	}

	/*
	 * Two steps: move the attributes not found in kv1 from kv2;
	 * the kva routines are not very useful here so we need
	 * to do somethings by hand.
	 */

	len = kv1->length + kv2->length;
	newkv = realloc(kv1->data, len * sizeof (kv_t));

	if (newkv == NULL) {
		_kva_free(kv1);
		_kva_free(kv2);
		return (NULL);
	}

	kv1->data = newkv;

	for (i = 0; i < kv2->length; i++) {
		if (kva_match(kv1, kv2->data[i].key) == NULL) {
			kv1->data[kv1->length++] = kv2->data[i];
			kv2->data[i].key = NULL;
			kv2->data[i].value = NULL;
		}
	}

	if ((flags & FC_FLAG_PROFATTR) != 0) {
		for (i = 0; i < kv2->length; i++) {
			char *key = kv2->data[i].key;
			if (key != NULL && kv2->data[i].value != NULL &&
			    (strcmp(key, PROFATTR_AUTHS_KW) == 0 ||
			    strcmp(key, PROFATTR_PROFS_KW) == 0 ||
			    strcmp(key, PROFATTR_PRIVS_KW) == 0)) {
				char *val1 = kva_match(kv1, key);
				int blen;
				char *nbuf;

				/* Same value, ignore it. */
				if (strcmp(val1, kv2->data[i].value) == 0)
					continue;

				blen = strlen(kv2->data[i].value) +
				    strlen(val1) + 2;
				nbuf = alloca(blen);

				(void) snprintf(nbuf, blen, "%s,%s", val1,
				    kv2->data[i].value);

				(void) _insert2kva(kv1, key, nbuf);
			}
		}
	}
	_kva_free(kv2);
	free(attr1);
	res = _kva2str(kv1, buf, sizeof (buf), KV_ASSIGN, KV_DELIMITER);
	_kva_free(kv1);

	if (res == 0)
		return (strdup(buf));
	else
		return (NULL);
}

static void
attrstr_free(attrstr_t *attr)
{
	int i;
	for (i = 0; i < ATTRSIZE; i++)
		free(attr->f[i]);
}

extern int _str2profattr(const char *, int, void *, char *, int);

static nss_status_t
combine_attrs(attrstr_t *at, line_matches_t *pa, nss_XbyY_args_t *args,
    nss_status_t res, int fl)
{
	attrstr_t myat;
	char *buf = alloca(pa->len + 1);
	int i;

	if (pa->next != NULL)
		res = combine_attrs(at, pa->next, args, res, fl);

	if (res != NSS_SUCCESS) {
		free(pa->line);
		free(pa);
		return (res);
	}

	/*
	 * What we have here is a line to merge and earlier entries;
	 * we only merge the auth and profile attributes; the rest
	 * are kept but the first one gets it.  We can't user str2ent
	 * because nscd replaces it with a function which doesn't
	 * parse and doesn't fill myat.
	 */
	if (_str2profattr(pa->line, pa->len, &myat, buf, pa->len + 1)
	    != NSS_STR_PARSE_SUCCESS) {
		free(pa->line);
		free(pa);

		return (NSS_NOTFOUND);
	}

	for (i = 0; i < ATTRSIZE - 1; i++) {
		if (!ISEMPTY(myat.f[i]) && at->f[i] == NULL)
			at->f[i] = strdup(myat.f[i]);
	}
	if (!ISEMPTY(myat.f[AIND])) {
		if (at->f[AIND] == NULL)
			at->f[AIND] = strdup(myat.f[AIND]);
		else
			at->f[AIND] = mergeattr(at->f[AIND], myat.f[AIND], fl);
		/* Failed to allocate, return failure */
		if (at->f[AIND] == NULL)
			res = NSS_NOTFOUND;
	}
	free(pa->line);
	free(pa);

	return (res);
}

nss_status_t
finish_attr(line_matches_t *pa, nss_XbyY_args_t *args, nss_status_t res, int fl)
{
	attrstr_t attr = { 0 };
	char *buf;
	int len;

	if (pa == NULL)
		return (res);

	if (res != NSS_SUCCESS) {
		(void) combine_attrs(&attr, pa, args, res, fl);
		attrstr_free(&attr);
		return (res);
	}

	if (pa->next != NULL) {
		char *strs[ATTRSIZE];
		int i;

		res = combine_attrs(&attr, pa, args, res, fl);
		if (res != NSS_SUCCESS) {
			attrstr_free(&attr);
			return (res);
		}
		for (i = 0; i < ATTRSIZE; i++)
			strs[i] = _escape(attr.f[i] ? attr.f[i] : "", ":");

		len = 5; /* 4 colons and one NUL byte */
		for (i = 0; i < ATTRSIZE; i++)
			len += strlen(strs[i]);

		buf = alloca(len);
		len = snprintf(buf, len, "%s:%s:%s:%s:%s",
		    strs[0], strs[1], strs[2], strs[3], strs[4]);

		for (i = 0; i < ATTRSIZE; i++)
			free(strs[i]);

		/* pa was freed by combine_attrs */
		pa = NULL;
	} else {
		buf = pa->line;
		len = pa->len;
	}

	if (args->str2ent(buf, len, args->buf.result,
	    args->buf.buffer, args->buf.buflen) != NSS_STR_PARSE_SUCCESS) {
		res = NSS_NOTFOUND;
	} else {
		args->returnval = (args->buf.result != NULL) ?
		    args->buf.result : args->buf.buffer;
		args->returnlen = len;
	}

	if (pa != NULL) {
		free(pa->line);
		free(pa);
	}
	attrstr_free(&attr);
	return (res);
}

/*ARGSUSED*/
nss_backend_t *
_nss_files_prof_attr_constr(const char *dummy1,
    const char *dummy2,
    const char *dummy3,
    const char *dummy4,
    const char *dummy5)
{
	return (_nss_files_constr(profattr_ops,
	    sizeof (profattr_ops) / sizeof (profattr_ops[0]),
	    PROFATTR_DIRNAME,
	    NSS_LINELEN_PROFATTR,
	    &hashinfo,
	    FC_FLAG_PROFATTR | FC_FLAG_MERGEATTR));
}

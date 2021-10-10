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

#include <stdlib.h>
#include "files_common.h"
#include <time.h>
#include <exec_attr.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <ctype.h>
#include <synch.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

/*
 * files/getexecattr.c -- "files" backend for nsswitch "exec_attr" database
 *
 * _execattr_files_read_line and _execattr_files_XY_all code based on
 * nss_files_read_line and nss_files_XY_all respectively, from files_common.c
 */


/* externs from libnsl */
extern int _readbufline(char *, int, char *, int, int *);
extern char *_exec_wild_id(char *, const char *);
extern void _exec_cleanup(nss_status_t, nss_XbyY_args_t *);

/*
 * Hashing functions for exec_attr.  We need three.
 *
 * We hash on field 1 (hash_name), field 6 (hash_id) and
 * fields 1 + 6 (nameid).
 */

static uint_t
hash_xname(nss_XbyY_args_t *argp, int keyhash, const char *line,
    int linelen)
{
	if (keyhash) {
		_priv_execattr	*pe = (_priv_execattr *)(argp->key.attrp);
		const char *name = pe->name;
		return (hash_string(name, strlen(name)));
	} else {
		return (hash_field(line, linelen, 1));
	}
}

static uint_t
hash_id(nss_XbyY_args_t *argp, int keyhash, const char *line,
    int linelen)
{
	if (keyhash) {
		_priv_execattr *pe = (_priv_execattr *)(argp->key.attrp);
		const char *id = pe->id;
		return (hash_string(id, strlen(id)));
	} else {
		return (hash_field(line, linelen, 6));
	}
}

static uint_t
hash_nameid(nss_XbyY_args_t *argp, int keyhash, const char *line,
    int linelen)
{
	if (keyhash) {
		_priv_execattr *pe = (_priv_execattr *)(argp->key.attrp);
		const char *name = pe->name;
		const char *id = pe->id;
		return (hash_string(id, strlen(id)) +
		    hash_string(name, strlen(name)));
	} else {
		return (hash_field(line, linelen, 1)  +
		    hash_field(line, linelen, 6));
	}
}

static files_hash_func hash_exec[3] = { hash_xname, hash_id, hash_nameid };

static files_hash_t hashinfo = {
	DEFAULTMUTEX,
	sizeof (execattr_t),
	NSS_LINELEN_EXECATTR,
	sizeof (hash_exec)/sizeof (files_hash_func),
	hash_exec
};


/*
 * check_match: returns 1 if matching entry found, else returns 0.
 */
static int
check_match_i(nss_XbyY_args_t *argp, const char *line, int linelen, int hashop)
{
	const char	*limit, *linep, *keyp;
	_priv_execattr	*pe = (_priv_execattr *)(argp->key.attrp);
	const char	*exec_field[6];
	int		i;

	if (hashop == 0 || hashop == 2)
		exec_field[0] = pe->name; /* name */
	else
		exec_field[0] = NULL;
	exec_field[1] = pe->policy;	/* policy */
	exec_field[2] = pe->type;	/* type */
	exec_field[3] = NULL;			/* res1 */
	exec_field[4] = NULL;			/* res2 */

	if (hashop == 1 || hashop == 2)
		exec_field[5] = pe->id;	/* id */
	else
		exec_field[5] = NULL;

	/* No need to check attr field */

	linep = line;
	limit = line + linelen;

	for (i = 0; i < 6; i++) {
		keyp = exec_field[i];
		if (keyp) {
			/* compare field */
			while (*keyp && linep < limit &&
			    *linep != ':' && *keyp == *linep) {
				keyp++;
				linep++;
			}
			if (*keyp || linep == limit || *linep != ':')
				return (0);
		} else {
			/* skip field */
			while (linep < limit && *linep != ':')
				linep++;
		}
		linep++;
	}
	return (1);
}

static int
check_match_name(nss_XbyY_args_t *argp, const char *line, int linelen)
{
	return (check_match_i(argp, line, linelen, 0));
}

static int
check_match_id(nss_XbyY_args_t *argp, const char *line, int linelen)
{
	return (check_match_i(argp, line, linelen, 1));
}

static int
check_match_nameid(nss_XbyY_args_t *argp, const char *line, int linelen)
{
	return (check_match_i(argp, line, linelen, 2));
}

static files_XY_check_func cfs[3] = {
    check_match_name, check_match_id, check_match_nameid
};


/*
 * If search for exact match for id failed, get_wild checks if we have
 * a wild-card entry for that id.
 */
static nss_status_t
get_wild(files_backend_ptr_t be, nss_XbyY_args_t *argp, int hashop)
{
	const char	*orig_id = NULL;
	char		*old_id = NULL;
	char		*wild_id = NULL;
	nss_status_t	res = NSS_NOTFOUND;
	_priv_execattr	*pe = (_priv_execattr *)(argp->key.attrp);

	orig_id = pe->id;
	old_id = strdup(pe->id);
	wild_id = old_id;
	while ((wild_id = _exec_wild_id(wild_id, pe->type)) != NULL) {
		pe->id = wild_id;
		res = _nss_files_XY_hash(be, argp, 1, &hashinfo, hashop,
		    cfs[hashop]);
		if (res == NSS_SUCCESS)
			break;
	}
	pe->id = orig_id;
	if (old_id)
		free(old_id);

	return (res);
}


static nss_status_t
getbynam(files_backend_ptr_t be, void *a)
{
	nss_status_t	res;
	res = _nss_files_XY_hash(be, a, 1, &hashinfo, 0, check_match_name);

	_exec_cleanup(res, a);

	return (res);
}


static nss_status_t
getbyid(files_backend_ptr_t be, void *a)
{
	nss_status_t	res;

	res = _nss_files_XY_hash(be, a, 1, &hashinfo, 1, check_match_id);

	if (res != NSS_SUCCESS)
		res = get_wild(be, a, 1);

	_exec_cleanup(res, a);

	return (res);
}


static nss_status_t
getbynameid(files_backend_ptr_t be, void *a)
{
	nss_status_t	res;

	res = _nss_files_XY_hash(be, a, 1, &hashinfo, 2, check_match_nameid);

	if (res != NSS_SUCCESS)
		res = get_wild(be, a, 2);

	_exec_cleanup(res, a);

	return (res);
}


static files_backend_op_t execattr_ops[] = {
	_nss_files_destr,
	_nss_files_endent,
	_nss_files_setent,
	_nss_files_getent_netdb,
	getbynam,
	getbyid,
	getbynameid
};

/*ARGSUSED*/
nss_backend_t  *
_nss_files_exec_attr_constr(const char *dummy1,
    const char *dummy2,
    const char *dummy3,
    const char *dummy4,
    const char *dummy5,
    const char *dummy6,
    const char *dummy7)
{
	return (_nss_files_constr(execattr_ops,
	    sizeof (execattr_ops)/sizeof (execattr_ops[0]),
	    EXECATTR_DIRNAME, NSS_LINELEN_EXECATTR,
	    &hashinfo, FC_FLAG_EXECATTR));
}

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

#include <secdb.h>
#include <prof_attr.h>
#include "ldap_common.h"


/* prof_attr attributes filters */
#define	_PROF_NAME		"cn"
#define	_PROF_RES1		"SolarisAttrReserved1"
#define	_PROF_RES2		"SolarisAttrReserved2"
#define	_PROF_DESC		"SolarisAttrLongDesc"
#define	_PROF_ATTRS		"SolarisAttrKeyValue"
/* Negate an exec_attr attribute to exclude exec_attr entries */
#define	_PROF_GETPROFNAME \
"(&(objectClass=SolarisProfAttr)(!(SolarisKernelSecurityPolicy=*))(cn=%s))"
#define	_PROF_GETPROFNAME_SSD	"(&(%%s)(cn=%s))"

static const char *prof_attrs[] = {
	_PROF_NAME,
	_PROF_RES1,
	_PROF_RES2,
	_PROF_DESC,
	_PROF_ATTRS,
	(char *)NULL
};
/*
 * _nss_ldap_prof2str is the data marshaling method for the prof_attr
 * system call getprofattr, getprofnam and getproflist.
 * This method is called after a successful search has been performed.
 * This method will parse the search results into the file format.
 * e.g.
 *
 * All:::Execute any command as the user or role:help=RtAll.html
 *
 */
static int
_nss_ldap_prof2str(ldap_backend_ptr be, nss_XbyY_args_t *argp)
{
	int			nss_result;
	int			buflen = 0;
	unsigned long		len = 0L;
	char			*buffer = NULL;
	ns_ldap_result_t	*result = be->result;
	char			**name, **res1, **res2, **des, **attr;
	char			*res1_str, *res2_str, *des_str, *attr_str;

	if (result == NULL)
		return (NSS_STR_PARSE_PARSE);

	buflen = argp->buf.buflen;
	nss_result = NSS_STR_PARSE_SUCCESS;
	(void) memset(argp->buf.buffer, 0, buflen);

	name = __ns_ldap_getAttr(result->entry, _PROF_NAME);
	if (name == NULL || name[0] == NULL || *name[0] == '\0') {
		nss_result = NSS_STR_PARSE_PARSE;
		goto result_prof2str;
	}
	res1 = __ns_ldap_getAttr(result->entry, _PROF_RES1);
	if (res1 == NULL || res1[0] == NULL || *res1[0] == '\0')
		res1_str = _NO_VALUE;
	else
		res1_str = res1[0];

	res2 = __ns_ldap_getAttr(result->entry, _PROF_RES2);
	if (res2 == NULL || res2[0] == NULL || *res2[0] == '\0')
		res2_str = _NO_VALUE;
	else
		res2_str = res2[0];

	des = __ns_ldap_getAttr(result->entry, _PROF_DESC);
	if (des == NULL || des[0] == NULL || *des[0] == '\0')
		des_str = _NO_VALUE;
	else
		des_str = des[0];

	attr = __ns_ldap_getAttr(result->entry, _PROF_ATTRS);
	if (attr == NULL || attr[0] == NULL || *attr[0] == '\0')
		attr_str = _NO_VALUE;
	else
		attr_str = attr[0];
	/* 5 = 4 ':' + 1 '\0' */
	len = strlen(name[0]) + strlen(res1_str) + strlen(res2_str) +
	    strlen(des_str) + strlen(attr_str) + 6;
	if (len > buflen) {
		nss_result = NSS_STR_PARSE_ERANGE;
		goto result_prof2str;
	}

	if (argp->buf.result != NULL) {
		if ((be->buffer = calloc(1, len)) == NULL) {
			nss_result = NSS_STR_PARSE_PARSE;
			goto result_prof2str;
		}
		buffer = be->buffer;
	} else
		buffer = argp->buf.buffer;
	(void) snprintf(buffer, len, "%s:%s:%s:%s:%s",
	    name[0], res1_str, res2_str, des_str, attr_str);
	/* The front end marshaller doesn't need the trailing null */
	if (argp->buf.result != NULL)
		be->buflen = strlen(be->buffer);

result_prof2str:
	(void) __ns_ldap_freeResult(&be->result);
	return (nss_result);
}

extern nss_status_t __get_default_prof(const char *, nss_XbyY_args_t *);

static nss_status_t
getbyname(ldap_backend_ptr be, void *a)
{
	char		searchfilter[SEARCHFILTERLEN];
	char		userdata[SEARCHFILTERLEN];
	char		name[SEARCHFILTERLEN];
	int		ret;
	nss_XbyY_args_t	*argp = (nss_XbyY_args_t *)a;

	if (argp->key.name != NULL && *argp->key.name == '/' &&
	    __get_default_prof(argp->key.name, argp) == NSS_SUCCESS) {
		return (NSS_SUCCESS);
	}
	if (_ldap_filter_name(name, argp->key.name, sizeof (name)) != 0)
		return ((nss_status_t)NSS_NOTFOUND);

	ret = snprintf(searchfilter, sizeof (searchfilter),
	    _PROF_GETPROFNAME, name);
	if (ret < 0 || ret >= sizeof (searchfilter))
		return ((nss_status_t)NSS_NOTFOUND);

	ret = snprintf(userdata, sizeof (userdata),
	    _PROF_GETPROFNAME_SSD, name);
	if (ret < 0 || ret >= sizeof (userdata))
		return ((nss_status_t)NSS_NOTFOUND);

	return (_nss_ldap_lookup(be, argp,
	    _PROFATTR, searchfilter, NULL, _merge_SSD_filter, userdata));
}


static ldap_backend_op_t profattr_ops[] = {
	_nss_ldap_destr,
	_nss_ldap_endent,
	_nss_ldap_setent,
	_nss_ldap_getent,
	getbyname
};


/*ARGSUSED0*/
nss_backend_t *
_nss_ldap_prof_attr_constr(const char *dummy1,
    const char *dummy2,
    const char *dummy3,
    const char *dummy4,
    const char *dummy5)
{
	return ((nss_backend_t *)_nss_ldap_constr(profattr_ops,
	    sizeof (profattr_ops)/sizeof (profattr_ops[0]), _PROFATTR,
	    prof_attrs, _nss_ldap_prof2str));
}

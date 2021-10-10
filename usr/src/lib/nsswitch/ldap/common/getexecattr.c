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
#include <exec_attr.h>
#include <alloca.h>
#include "ldap_common.h"


/* exec_attr attributes filters */
#define	ISWILD(x)		(x == NULL) ? "*" : x
#define	_EXEC_NAME		"cn"
#define	_EXEC_POLICY		"SolarisKernelSecurityPolicy"
#define	_EXEC_TYPE		"SolarisProfileType"
#define	_EXEC_RES1		"SolarisAttrRes1"
#define	_EXEC_RES2		"SolarisAttrRes2"
#define	_EXEC_ID		"SolarisProfileId"
#define	_EXEC_ATTRS		"SolarisAttrKeyValue"
#define	_EXEC_GETEXECNAME	"(&(objectClass=SolarisExecAttr)(cn=%s)"\
				"(SolarisKernelSecurityPolicy=%s)"\
				"(SolarisProfileType=%s))"
#define	_EXEC_GETEXECNAME_SSD	"(&(%%s)(cn=%s)"\
				"(SolarisKernelSecurityPolicy=%s)"\
				"(SolarisProfileType=%s))"
#define	_EXEC_GETEXECID		"(&(objectClass=SolarisExecAttr)"\
				"(SolarisProfileId=%s)"\
				"(SolarisKernelSecurityPolicy=%s)"\
				"(SolarisProfileType=%s))"
#define	_EXEC_GETEXECID_SSD	"(&(%%s)"\
				"(SolarisProfileId=%s)"\
				"(SolarisKernelSecurityPolicy=%s)"\
				"(SolarisProfileType=%s))"
#define	_EXEC_GETEXECNAMEID	"(&(objectClass=SolarisExecAttr)(cn=%s)"\
				"(SolarisProfileId=%s)"\
				"(SolarisKernelSecurityPolicy=%s)"\
				"(SolarisProfileType=%s))"
#define	_EXEC_GETEXECNAMEID_SSD	"(&(%%s)(cn=%s)"\
				"(SolarisProfileId=%s)"\
				"(SolarisKernelSecurityPolicy=%s)"\
				"(SolarisProfileType=%s))"


/* from libc */
extern int _doexeclist(nss_XbyY_args_t *);
extern char *_exec_wild_id(char *, const char *);
extern void _exec_cleanup(nss_status_t, nss_XbyY_args_t *);


static const char *exec_attrs[] = {
	_EXEC_NAME,
	_EXEC_POLICY,
	_EXEC_TYPE,
	_EXEC_RES1,
	_EXEC_RES2,
	_EXEC_ID,
	_EXEC_ATTRS,
	(char *)NULL
};


#ifdef	DEBUG
static void
_print_execstr(execstr_t *exec)
{

	(void) fprintf(stdout, "      exec-name: [%s]\n", exec->name);
	if (exec->policy != (char *)NULL) {
		(void) fprintf(stdout, "      policy: [%s]\n", exec->policy);
	}
	if (exec->type != (char *)NULL) {
		(void) fprintf(stdout, "      type: [%s]\n", exec->type);
	}
	if (exec->res1 != (char *)NULL) {
		(void) fprintf(stdout, "      res1: [%s]\n", exec->res1);
	}
	if (exec->res2 != (char *)NULL) {
		(void) fprintf(stdout, "      res2: [%s]\n", exec->res2);
	}
	if (exec->id != (char *)NULL) {
		(void) fprintf(stdout, "      id: [%s]\n", exec->id);
	}
	if (exec->attr != (char *)NULL) {
		(void) fprintf(stdout, "      attr: [%s]\n", exec->attr);
	}
	if (exec->next != (execstr_t *)NULL) {
		(void) fprintf(stdout, "      next: [%s]\n", exec->next->name);
		(void) fprintf(stdout, "\n");
		_print_execstr(exec->next);
	}
}
#endif	/* DEBUG */

/*
 * place the results from ldap object structure into the file format
 * returns NSS_STR_PARSE_{SUCCESS, ERANGE, PARSE}
 */
static int
_nss_ldap_exec2str_entry(ns_ldap_entry_t *entry, char *buf, int buflen)
{
	char			**name, **policy, **type;
	char			**res1, **res2, **id, **attr;
	char			*policy_str, *type_str, *res1_str, *res2_str;
	char			*id_str, *attr_str;

	if (entry == NULL)
		return (NSS_STR_PARSE_PARSE);

	name = __ns_ldap_getAttr(entry, _EXEC_NAME);
	if (name == NULL || name[0] == NULL || name[0][0] == '\0')
		return (NSS_STR_PARSE_PARSE);

	policy = __ns_ldap_getAttr(entry, _EXEC_POLICY);

	if (policy == NULL || policy[0] == NULL)
		policy_str = _NO_VALUE;
	else
		policy_str = policy[0];

	type = __ns_ldap_getAttr(entry, _EXEC_TYPE);
	if (type == NULL || type[0] == NULL)
		type_str = _NO_VALUE;
	else
		type_str = type[0];

	res1 = __ns_ldap_getAttr(entry, _EXEC_RES1);
	if (res1 == NULL || res1[0] == NULL)
		res1_str = _NO_VALUE;
	else
		res1_str = res1[0];

	res2 = __ns_ldap_getAttr(entry, _EXEC_RES2);
	if (res2 == NULL || res2[0] == NULL)
		res2_str = _NO_VALUE;
	else
		res2_str = res2[0];

	id = __ns_ldap_getAttr(entry, _EXEC_ID);
	if (id == NULL || id[0] == NULL)
		id_str = _NO_VALUE;
	else
		id_str = id[0];

	attr = __ns_ldap_getAttr(entry, _EXEC_ATTRS);
	if (attr == NULL || attr[0] == NULL)
		attr_str = _NO_VALUE;
	else
		attr_str = attr[0];

	if (snprintf(buf, buflen, "%s:%s:%s:%s:%s:%s:%s",
	    name[0], policy_str, type_str, res1_str,
	    res2_str, id_str, attr_str) >= buflen) {
		return (NSS_STR_PARSE_PARSE);
	}

	return (NSS_STR_PARSE_SUCCESS);
}

/*
 * This is the common ldapexec2str; the string is later consumed by
 * str2ent.  If result == NULL, we return a list of new-line delimited
 * entries; if result != NULL, we also return a list of execstr_ts.
 */
static int
_nss_ldap_exec2str(ldap_backend_ptr be, nss_XbyY_args_t *argp)
{
	int			status = NSS_STR_PARSE_SUCCESS;
	ns_ldap_entry_t		*entry;
	char			*buffer;

	if (be->result == NULL || (entry = be->result->entry) == NULL)
		return (NSS_STR_PARSE_PARSE);

	(void) memset(argp->buf.buffer, 0, argp->buf.buflen);

	if (argp->buf.result != NULL) {
		if ((be->buffer = calloc(1, argp->buf.buflen)) == NULL) {
			status = NSS_STR_PARSE_PARSE;
			goto result_exec2str;
		}
		buffer = be->buffer;
	} else
		buffer = argp->buf.buffer;

	if (_nss_ldap_exec2str_entry(entry, buffer, argp->buf.buflen) != 0)
		status = NSS_STR_PARSE_PARSE;
	else
		be->buflen = strlen(buffer);

result_exec2str:
	(void) __ns_ldap_freeResult(&be->result);
	return (status);
}


static nss_status_t
_exec_process_val(ldap_backend_ptr be, nss_XbyY_args_t *argp)
{
	int 			status;
	char			buffer[NSS_BUFSIZ];
	nss_status_t		nss_stat = NSS_UNAVAIL;
	ns_ldap_attr_t		*attrptr;
	ns_ldap_entry_t		*entry;
	ns_ldap_result_t	*result = be->result;
	_priv_execattr	*_priv_exec = (_priv_execattr *)(argp->key.attrp);

	if (!IS_GET_ALL(_priv_exec->search_flag))
		return (NSS_UNAVAIL);

	argp->returnval = NULL;
	attrptr = getattr(result, 0);
	if (attrptr == NULL)
		goto err;

	(void) memset(argp->buf.buffer, 0, argp->buf.buflen);

	for (entry = be->result->entry; entry != NULL; entry = entry->next) {
		status = _nss_ldap_exec2str_entry(entry, buffer,
		    sizeof (buffer));

		switch (status) {
		case NSS_STR_PARSE_SUCCESS:
			status = argp->str2ent(buffer, strlen(buffer),
			    argp->buf.result,
			    argp->buf.buffer, argp->buf.buflen);
			if (status != NSS_STR_PARSE_SUCCESS) {
				argp->returnval = NULL;
				argp->returnlen = 0;
				nss_stat = NSS_NOTFOUND;
				goto err;
			}
			argp->returnval = argp->buf.result == NULL ?
			    argp->buf.buffer : argp->buf.result;
			argp->returnlen = strlen(argp->buf.buffer);
			nss_stat = NSS_SUCCESS;
			if (_doexeclist(argp) == 0) {
				nss_stat = NSS_UNAVAIL;
				goto err;
			}
			break;
		case NSS_STR_PARSE_ERANGE:
			argp->erange = 1;
			nss_stat = NSS_NOTFOUND;
			goto err;
		case NSS_STR_PARSE_PARSE:
			nss_stat = NSS_NOTFOUND;
			goto err;
		default:
			nss_stat = NSS_UNAVAIL;
			goto err;
		}
	}

err:
	(void) __ns_ldap_freeResult(&be->result);
	return (nss_stat);
}


/*
 * Check if we have either an exact match or a wild-card entry for that id.
 */
static nss_status_t
get_wild(ldap_backend_ptr be, nss_XbyY_args_t *argp, int getby_flag)
{
	char		*dup_id = NULL;
	char		*wild_id;
	char		searchfilter[SEARCHFILTERLEN];
	char		userdata[SEARCHFILTERLEN];
	char		name[SEARCHFILTERLEN];
	char		id[SEARCHFILTERLEN];
	int		ret;
	nss_status_t	nss_stat = NSS_NOTFOUND;
	_priv_execattr	*_priv_exec = (_priv_execattr *)(argp->key.attrp);
	const char	*policy = _priv_exec->policy;
	const char	*type = _priv_exec->type;

	if (strpbrk(policy, "*()\\") != NULL ||
	    type != NULL && strpbrk(type, "*()\\") != NULL)
		return ((nss_status_t)NSS_NOTFOUND);

	if (_priv_exec->id != NULL)
		dup_id = strdup(_priv_exec->id);

	switch (getby_flag) {
	case NSS_DBOP_EXECATTR_BYNAMEID:
		if (_ldap_filter_name(name, _priv_exec->name,
		    sizeof (name)) != 0)
			goto go_out;
		break;
	}

	wild_id = dup_id;
	do {
		if (wild_id != NULL) {
			if (_ldap_filter_name(id, wild_id, sizeof (id)) != 0)
				goto go_out;
		} else
			(void) strlcpy(id, "*", sizeof (id));

		switch (getby_flag) {
		case NSS_DBOP_EXECATTR_BYID:
			ret = snprintf(searchfilter, sizeof (searchfilter),
			    _EXEC_GETEXECID, id, policy, ISWILD(type));
			if (ret >= sizeof (searchfilter) || ret < 0)
				goto go_out;
			ret = snprintf(userdata, sizeof (userdata),
			    _EXEC_GETEXECID_SSD, id, policy, ISWILD(type));
			if (ret >= sizeof (userdata) || ret < 0)
				goto go_out;
			break;

		case NSS_DBOP_EXECATTR_BYNAMEID:
			ret = snprintf(searchfilter, sizeof (searchfilter),
			    _EXEC_GETEXECNAMEID, name, id,
			    policy, ISWILD(type));
			if (ret >= sizeof (searchfilter) || ret < 0)
				goto go_out;
			ret = snprintf(userdata, sizeof (userdata),
			    _EXEC_GETEXECNAMEID_SSD, name, id,
			    policy, ISWILD(type));
			if (ret >= sizeof (userdata) || ret < 0)
				goto go_out;
			break;

		default:
			goto go_out;
		}
		nss_stat = _nss_ldap_nocb_lookup(be, argp, _EXECATTR,
		    searchfilter, NULL, _merge_SSD_filter, userdata);
		if (nss_stat == NSS_SUCCESS)
			break;
	} while ((wild_id = _exec_wild_id(wild_id, type)) != NULL);

go_out:
	free(dup_id);

	return (nss_stat);
}

static nss_status_t
exec_attr_process_val(ldap_backend_ptr be, nss_XbyY_args_t *argp)
{
	_priv_execattr	*_priv_exec = (_priv_execattr *)(argp->key.attrp);
	int		stat, nss_stat = NSS_SUCCESS;

	if (IS_GET_ONE(_priv_exec->search_flag)) {
		/* ns_ldap_entry_t -> file format */
		stat = (*be->ldapobj2str)(be, argp);

		if (stat == NSS_STR_PARSE_SUCCESS) {
			if (argp->buf.result != NULL) {
				/* file format -> execstr_t */
				stat = (*argp->str2ent)(be->buffer,
				    be->buflen, argp->buf.result,
				    argp->buf.buffer, argp->buf.buflen);
				if (stat == NSS_STR_PARSE_SUCCESS) {
					argp->returnval = argp->buf.result;
					argp->returnlen = 1; /* irrelevant */
					nss_stat = NSS_SUCCESS;
				} else {
					argp->returnval = NULL;
					argp->returnlen = 0;
					nss_stat = NSS_NOTFOUND;
				}
			} else {
				/* return file format in argp->buf.buffer */
				argp->returnval = argp->buf.buffer;
				argp->returnlen = strlen(argp->buf.buffer);
				nss_stat = NSS_SUCCESS;
			}
		} else {
			argp->returnval = NULL;
			argp->returnlen = 0;
			nss_stat = NSS_NOTFOUND;
		}
	} else {
		/* GET_ALL */
		nss_stat = _exec_process_val(be, argp);
		_exec_cleanup(nss_stat, argp);
	}

	return (nss_stat);
}

static nss_status_t
getbynam(ldap_backend_ptr be, void *a)
{
	char		searchfilter[SEARCHFILTERLEN];
	char		userdata[SEARCHFILTERLEN];
	char		name[SEARCHFILTERLEN];
	int		ret;
	nss_status_t	nss_stat;
	nss_XbyY_args_t	*argp = (nss_XbyY_args_t *)a;
	_priv_execattr	*_priv_exec = (_priv_execattr *)(argp->key.attrp);
	const char	*policy = _priv_exec->policy;
	const char	*type = _priv_exec->type;

	if (strpbrk(policy, "*()\\") != NULL ||
	    type != NULL && strpbrk(type, "*()\\") != NULL ||
	    _ldap_filter_name(name, _priv_exec->name, sizeof (name)) != 0)
		return ((nss_status_t)NSS_NOTFOUND);
	ret = snprintf(searchfilter, sizeof (searchfilter),
	    _EXEC_GETEXECNAME, name, policy, ISWILD(type));
	if (ret >= sizeof (searchfilter) || ret < 0)
		return ((nss_status_t)NSS_NOTFOUND);
	ret = snprintf(userdata, sizeof (userdata),
	    _EXEC_GETEXECNAME_SSD, name, policy, ISWILD(type));
	if (ret >= sizeof (userdata) || ret < 0)
		return ((nss_status_t)NSS_NOTFOUND);

	nss_stat = _nss_ldap_nocb_lookup(be, argp, _EXECATTR,
	    searchfilter, NULL, _merge_SSD_filter, userdata);

	if (nss_stat ==  NSS_SUCCESS)
		nss_stat = exec_attr_process_val(be, argp);

	return (nss_stat);
}

static nss_status_t
getbyid(ldap_backend_ptr be, void *a)
{
	nss_status_t	nss_stat = NSS_SUCCESS;
	nss_XbyY_args_t	*argp = (nss_XbyY_args_t *)a;

	nss_stat = get_wild(be, argp, NSS_DBOP_EXECATTR_BYID);

	if (nss_stat ==  NSS_SUCCESS)
		nss_stat = exec_attr_process_val(be, argp);

	return (nss_stat);
}


static nss_status_t
getbynameid(ldap_backend_ptr be, void *a)
{
	nss_status_t	nss_stat;
	nss_XbyY_args_t	*argp = (nss_XbyY_args_t *)a;

	nss_stat = get_wild(be, argp, NSS_DBOP_EXECATTR_BYNAMEID);

	if (nss_stat ==  NSS_SUCCESS)
		nss_stat = exec_attr_process_val(be, argp);

	return (nss_stat);
}


static ldap_backend_op_t execattr_ops[] = {
	_nss_ldap_destr,
	_nss_ldap_endent,
	_nss_ldap_setent,
	_nss_ldap_getent,
	getbynam,
	getbyid,
	getbynameid
};


/*ARGSUSED0*/
nss_backend_t *
_nss_ldap_exec_attr_constr(const char *dummy1,
    const char *dummy2,
    const char *dummy3,
    const char *dummy4,
    const char *dummy5,
    const char *dummy6,
    const char *dummy7)
{
#ifdef	DEBUG
	(void) fprintf(stdout,
	    "\n[getexecattr.c: _nss_ldap_exec_attr_constr]\n");
#endif
	return ((nss_backend_t *)_nss_ldap_constr(execattr_ops,
	    sizeof (execattr_ops)/sizeof (execattr_ops[0]), _EXECATTR,
	    exec_attrs, _nss_ldap_exec2str));
}

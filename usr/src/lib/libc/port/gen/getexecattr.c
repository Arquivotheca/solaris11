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
#include "mtlib.h"
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <nss_dbdefs.h>
#include <deflt.h>
#include <secdb.h>
#include <exec_attr.h>
#include <user_attr.h>
#include <auth_attr.h>
#include <prof_attr.h>
#include <getxby_door.h>
#include <nsswitch.h>
#include <sys/mman.h>


extern userstr_t *_getuserattr(userstr_t *, char *, int, int *);
extern void _nss_db_state_destr(struct nss_db_state *, nss_db_root_t *);
extern int _str2profattr(const char *, int, void *, char *, int);

static execattr_t *userprof(const char *, const char *, const char *, int);
static execattr_t *get_tail(execattr_t *);
execattr_t *_execstr2attr(execstr_t *, execstr_t *);

char *_exec_wild_id(char *, const char *);
static execstr_t *_dup_execstr(execstr_t *);
static void _free_execstr(execstr_t *);

static char *_nsw_search_path = NULL;

/*
 * Unsynchronized, but it affects only efficiency, not correctness
 */

static DEFINE_NSS_DB_ROOT(exec_root);
static DEFINE_NSS_GETENT(context);

void
_nss_initf_execattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_EXECATTR;
	p->config_name    = NSS_DBNAM_PROFATTR; /* use config for "prof_attr" */
}

void
_nsw_initf_execattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_EXECATTR;
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = _nsw_search_path;
}

void
_nsw_initf_profattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_PROFATTR;
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = _nsw_search_path;
}

/*
 * Return values: 0 = success, 1 = parse error, 2 = erange ... The structure
 * pointer passed in is a structure in the caller's space wherein the field
 * pointers would be set to areas in the buffer if need be. instring and buffer
 * should be separate areas.
 * When we free execstr_t's, the first one is typically not allocated but
 * the rest must be, including the strings.  That is why we use _dup_execstr.
 */
int
_str2execattr(const char *instr, int lenstr, void *ent, char *buf, int buflen)
{
	char		*last = NULL;
	char		*sep = KV_TOKEN_DELIMIT;
	execstr_t	*exec = ent;
	char		*newline;

	if (lenstr >= buflen)
		return (NSS_STR_PARSE_ERANGE);

	if (instr != buf)
		(void) memcpy(buf, instr, lenstr);

	/* Terminate the buffer */
	buf[lenstr] = '\0';

	/* quick exit do not entry fill if not needed */
	if (ent == NULL)
		return (NSS_STR_PARSE_SUCCESS);

	newline = strchr(buf, '\n');
	exec->next = NULL;

	/*
	 * nscd may return multiple entries separated by newlines; we return
	 * one entry for each line.
	 * Ignore a trailing \n but parse the additional entries.
	 * The new arguments for recursion are chosen so that no copy takes
	 * place and such that lenstr < buflen.
	 */
	if (newline != NULL) {
		newline[0] = '\0';
		if (newline[1] != '\0') {
			execstr_t	nexec;
			char		*nbuf = newline + 1;
			int		nlen = buf + lenstr - nbuf;
			int		res;

			res = _str2execattr(nbuf, nlen, &nexec, nbuf, nlen + 1);
			if (res != 0)
				return (res);

			if ((exec->next = _dup_execstr(&nexec)) == NULL) {
				_free_execstr(nexec.next);
				return (NSS_STR_PARSE_PARSE);
			}
		}
	}

	exec->name = _strtok_escape(buf, sep, &last);
	exec->policy = _strtok_escape(NULL, sep, &last);
	exec->type = _strtok_escape(NULL, sep, &last);
	exec->res1 = _strtok_escape(NULL, sep, &last);
	exec->res2 = _strtok_escape(NULL, sep, &last);
	exec->id = _strtok_escape(NULL, sep, &last);
	exec->attr = _strtok_escape(NULL, sep, &last);

	return (NSS_STR_PARSE_SUCCESS);
}


void
setexecattr(void)
{
	nss_setent(&exec_root, _nss_initf_execattr, &context);
}

void
endexecattr(void)
{
	nss_endent(&exec_root, _nss_initf_execattr, &context);
	nss_delete(&exec_root);
}


static execstr_t *
_getexecattr(execstr_t *result, char *buffer, int buflen, int *errnop)
{
	nss_status_t    res;
	nss_XbyY_args_t arg;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, _str2execattr);
	res = nss_getent(&exec_root, _nss_initf_execattr, &context, &arg);
	arg.status = res;
	*errnop = arg.h_errno;

	return (NSS_XbyY_FINI(&arg));
}

static execstr_t *
_getexecprof(const char *name,
    const char *type,
    const char *id,
    int search_flag,
    execstr_t *result,
    char *buffer,
    int buflen,
    int *errnop)
{
	int		getby_flag;
	char		policy_buf[BUFSIZ];
	const char	*empty = NULL;
	nss_status_t	res = NSS_NOTFOUND;
	nss_XbyY_args_t	arg;
	_priv_execattr	_priv_exec;
	static mutex_t	_nsw_exec_lock = DEFAULTMUTEX;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, _str2execattr);

	_priv_exec.name = (name == NULL) ? empty : name;
	_priv_exec.type = (type == NULL) ? empty : type;
	_priv_exec.id = (id == NULL) ? empty : id;
#ifdef SI_SECPOLICY
	if (sysinfo(SI_SECPOLICY, policy_buf, BUFSIZ) == -1)
#endif	/* SI_SECPOLICY */
	(void) strncpy(policy_buf, DEFAULT_POLICY, BUFSIZ);

retry_policy:
	_priv_exec.policy = IS_SEARCH_ALL(search_flag) ? empty : policy_buf;
	_priv_exec.search_flag = search_flag;
	_priv_exec.head_exec = NULL;
	_priv_exec.prev_exec = NULL;

	if ((name != NULL) && (id != NULL)) {
		getby_flag = NSS_DBOP_EXECATTR_BYNAMEID;
	} else if (name != NULL) {
		getby_flag = NSS_DBOP_EXECATTR_BYNAME;
	} else if (id != NULL) {
		getby_flag = NSS_DBOP_EXECATTR_BYID;
	}

	arg.key.attrp = &(_priv_exec);

	switch (getby_flag) {
	case NSS_DBOP_EXECATTR_BYID:
		res = nss_search(&exec_root, _nss_initf_execattr, getby_flag,
		    &arg);
		break;
	case NSS_DBOP_EXECATTR_BYNAMEID:
	case NSS_DBOP_EXECATTR_BYNAME:
		{
			char			pbuf[NSS_BUFLEN_PROFATTR];
			profstr_t		prof;
			nss_status_t		pres;
			nss_XbyY_args_t		parg;
			enum __nsw_parse_err	pserr;
			struct __nsw_lookup	*lookups = NULL;
			struct __nsw_switchconfig *conf = NULL;

			if (conf = __nsw_getconfig(NSS_DBNAM_PROFATTR, &pserr))
				if ((lookups = conf->lookups) == NULL)
					goto out;
			NSS_XbyY_INIT(&parg, &prof, pbuf, NSS_BUFLEN_PROFATTR,
			    _str2profattr);
			parg.key.name = name;
			do {
				/*
				 * search the exec_attr entry only in the scope
				 * that we find the profile in.
				 * if conf = NULL, search in local files only,
				 * as we were not able to read nsswitch.conf.
				 */
				DEFINE_NSS_DB_ROOT(prof_root);
				if (mutex_lock(&_nsw_exec_lock) != 0)
					goto out;
				_nsw_search_path = (conf == NULL)
				    ? NSS_FILES_ONLY
				    : lookups->service_name;
				pres = nss_search(&prof_root,
				    _nsw_initf_profattr,
				    NSS_DBOP_PROFATTR_BYNAME, &parg);
				if (pres == NSS_SUCCESS) {
					DEFINE_NSS_DB_ROOT(pexec_root);
					res = nss_search(&pexec_root,
					    _nsw_initf_execattr, getby_flag,
					    &arg);
					if (pexec_root.s != NULL)
						_nss_db_state_destr(
						    pexec_root.s,
						    &pexec_root);
				}
				if (prof_root.s != NULL)
					_nss_db_state_destr(prof_root.s,
					    &prof_root);
				(void) mutex_unlock(&_nsw_exec_lock);
				if ((pres == NSS_SUCCESS) || (conf == NULL))
					break;
			} while (lookups && (lookups = lookups->next));
		}
		break;
	default:
		break;
	}

out:
	/*
	 * If we can't find an entry for the current default policy
	 * fall back to the old "suser" policy.  The nameservice is
	 * shared between different OS releases.
	 */
	if (!IS_SEARCH_ALL(search_flag) &&
	    (res == NSS_NOTFOUND && strcmp(policy_buf, DEFAULT_POLICY) == 0)) {
		(void) strlcpy(policy_buf, SUSER_POLICY, BUFSIZ);
		goto retry_policy;
	}

	arg.status = res;
	*errnop = res;
	return (NSS_XbyY_FINI(&arg));
}


/*
 * When we're in nscd, we'll concatenate all the entries and put them in
 * different execattr_t's later in libc. New entries are concatenated by
 * incrementing the buffer pointer saved in pe->head_exec.  When look for
 * duplicates, we search in the buffer from earlier calls.
 *
 * If we're not creating an entry to be returned over the nscd door, i.e.,
 * the str2ent function isn't str2packent, we create a linked list of
 * exec_attrs.
 */

extern int str2packent(const char *, int, void *, char *, int);

int
_doexeclist(nss_XbyY_args_t *argp)
{
	int		status = 1;
	_priv_execattr	*pe = argp->key.attrp;
	char		*result;
	int len;
	char *str;

	if (argp->str2ent != str2packent) {
		execstr_t *exec = argp->buf.result;
		if (pe->head_exec == NULL) {
			if ((pe->head_exec = _dup_execstr(exec)) != NULL)
				pe->prev_exec = pe->head_exec;
			else
				status = 0;
		} else {
			if ((pe->prev_exec->next = _dup_execstr(exec)) != NULL)
				pe->prev_exec = pe->prev_exec->next;
			else
				status = 0;
		}
		(void) memset(argp->buf.buffer, 0, argp->buf.buflen);
		return (status);
	}

	len = strlen(argp->buf.buffer);
	result = (char *)pe->head_exec;

	if (pe->head_exec == NULL) {
		pe->head_exec = (void *)argp->buf.buffer;
	} else if ((str = strnstr(result, argp->buf.buffer,
	    argp->buf.buffer - result)) != NULL) {
		/* Remove duplicate entries */
		if (str == result || str[-1] == '\n')
			return (1);
	}

	if (argp->buf.buffer[len - 1] != '\n' && len < argp->buf.buflen - 1) {
		argp->buf.buffer[len] = '\n';
		argp->buf.buffer[len + 1] = '\0';
		len++;
	}

	argp->buf.buffer += len;
	argp->buf.buflen -= len;

	return (1);
}


/*
 * Converts id to a wildcard string. e.g.:
 *   For type = KV_COMMAND: /usr/ccs/bin/what ---> /usr/ccs/bin/\* ---> \*
 *   For type = KV_ACTION: Dtfile;*;*;*;0 ---> *;*;*;*;*
 *
 * Returns NULL if id is already a wild-card.
 */
char *
_exec_wild_id(char *id, const char *type)
{
	char	c_id = '/';
	char	*pchar = NULL;

	if ((id == NULL) || (type == NULL))
		return (NULL);

	if (strcmp(type, KV_ACTION) == 0) {
		return ((strcmp(id, KV_ACTION_WILDCARD) == 0) ? NULL :
		    KV_ACTION_WILDCARD);
	} else if (strcmp(type, KV_COMMAND) == 0) {
		if ((pchar = strrchr(id, c_id)) == NULL)
			/*
			 * id = \*
			 */
			return (NULL);
		else if (*(++pchar) == KV_WILDCHAR)
			/*
			 * id = /usr/ccs/bin/\*
			 */
			return (pchar);
		/*
		 * id = /usr/ccs/bin/what
		 */
		(void) strcpy(pchar, KV_WILDCARD);
		return (id);
	}

	return (NULL);

}


static execstr_t *
_dup_execstr(execstr_t *old_exec)
{
	execstr_t *new_exec = NULL;

	if (old_exec == NULL)
		return (NULL);
	if ((new_exec = malloc(sizeof (execstr_t))) != NULL) {
		new_exec->name = _strdup_null(old_exec->name);
		new_exec->type = _strdup_null(old_exec->type);
		new_exec->policy = _strdup_null(old_exec->policy);
		new_exec->res1 = _strdup_null(old_exec->res1);
		new_exec->res2 = _strdup_null(old_exec->res2);
		new_exec->id = _strdup_null(old_exec->id);
		new_exec->attr = _strdup_null(old_exec->attr);
		new_exec->next = old_exec->next;
	}
	return (new_exec);
}

static void
_free_execstr(execstr_t *exec)
{
	if (exec != NULL) {
		free(exec->name);
		free(exec->type);
		free(exec->policy);
		free(exec->res1);
		free(exec->res2);
		free(exec->id);
		free(exec->attr);
		_free_execstr(exec->next);
		free(exec);
	}
}

/*
 * At the end of function returning exec_attrs, we either return the
 * head of list of exec_attrs or, in the case of nscd, we compute the
 * full list and return that.
 */
void
_exec_cleanup(nss_status_t res, nss_XbyY_args_t *argp)
{
	_priv_execattr	*pe = argp->key.attrp;
	char *result;

	if (argp->str2ent != str2packent) {
		if (res == NSS_SUCCESS) {
			if (pe->head_exec != NULL) {
				argp->buf.result = pe->head_exec;
				argp->returnval = argp->buf.result;
			}
		} else {
			if (pe->head_exec != NULL)
				_free_execstr(pe->head_exec);
			argp->returnval = NULL;
		}
		return;
	}
	/*
	 * Compute the proper resultlen value from difference between
	 * buffer and result.  Reset the buflen value and make sure
	 * that the final newline is stripped.
	 */
	result = (char *)pe->head_exec;
	if (result != NULL && argp->buf.buffer != result) {
		argp->returnlen = argp->buf.buffer - result;
		argp->buf.buflen += argp->returnlen;
		argp->buf.buffer = argp->buf.result;
		if (argp->returnlen > 0)
			argp->returnlen--;
		argp->buf.result = result;
	}
	if (res != NSS_SUCCESS)
		argp->returnval = NULL;
}

execattr_t *
getexecattr(void)
{
	int		err = 0;
	char		buf[NSS_BUFLEN_EXECATTR];
	execstr_t	exec;
	execstr_t	*tmp;

	tmp = _getexecattr(&exec, buf, NSS_BUFLEN_EXECATTR, &err);

	return (_execstr2attr(tmp, &exec));
}


execattr_t *
getexecprof(const char *name, const char *type, const char *id, int search_flag)
{
	int		err = 0;
	char		unique[NSS_BUFLEN_EXECATTR];
	char		buf[NSS_BUFLEN_EXECATTR];
	execattr_t	*head = NULL;
	execattr_t	*prev = NULL;
	execstr_t	exec;
	execstr_t	*tmp;

	(void) memset(unique, 0, NSS_BUFLEN_EXECATTR);
	(void) memset(&exec, 0, sizeof (execstr_t));

	if (!IS_GET_ONE(search_flag) && !IS_GET_ALL(search_flag)) {
		return (NULL);
	}

	if ((name == NULL) && (type == NULL) && (id == NULL)) {
		setexecattr();
		if (IS_GET_ONE(search_flag)) {
			head = getexecattr();
		} else if (IS_GET_ALL(search_flag)) {
			head = getexecattr();
			prev = head;
			while (prev != NULL) {
				prev->next = getexecattr();
				prev = prev->next;
			};
		} else {
			head = NULL;
		}
		endexecattr();
		return (head);
	}

	tmp = _getexecprof(name,
	    type,
	    id,
	    search_flag,
	    &exec,
	    buf,
	    NSS_BUFLEN_EXECATTR,
	    &err);

	return (_execstr2attr(tmp, &exec));
}

execattr_t *
getexecuser(const char *username, const char *type, const char *id,
    int search_flag)
{
	int		err = 0;
	char		buf[NSS_BUFLEN_USERATTR];
	userstr_t	user;
	userstr_t	*utmp;
	execattr_t	*head = NULL;
	execattr_t	*prev =  NULL;
	execattr_t	*new = NULL;

	if (!IS_GET_ONE(search_flag) && !IS_GET_ALL(search_flag)) {
		return (NULL);
	}

	if (username == NULL) {
		setuserattr();
		/* avoid malloc by calling _getuserattr directly */
		utmp = _getuserattr(&user, buf, NSS_BUFLEN_USERATTR, &err);
		if (utmp == NULL) {
			return (head);
		}
		if (IS_GET_ONE(search_flag)) {
			head = userprof((const char *)(utmp->name), type, id,
			    search_flag);
		} else if (IS_GET_ALL(search_flag)) {
			head = userprof((const char *)(utmp->name), type, id,
			    search_flag);
			if (head != NULL) {
				prev = get_tail(head);
			}
			while ((utmp = _getuserattr(&user,
			    buf, NSS_BUFLEN_USERATTR, &err)) != NULL) {
				if ((new =
				    userprof((const char *)(utmp->name),
				    type, id, search_flag)) != NULL) {
					if (prev != NULL) {
						prev->next = new;
						prev = get_tail(prev->next);
					} else {
						head = new;
						prev = get_tail(head);
					}
				}
			}
		} else {
			head = NULL;
		}
		enduserattr();
	} else {
		head = userprof(username, type, id, search_flag);
	}

	return (head);
}


/*
 * Return the matched execattr from the list.
 * A match is defined as when all of the three parameters
 * match, but if a parameter is NULL or if a parameter is matched
 * to a field with value NULL, it is considered to have matched.
 */
execattr_t *
match_execattr(execattr_t *exec, const char *profname, const char *type,
    const char *id)
{
	execattr_t	*execp;

	for (execp = exec; execp != NULL; execp = execp->next) {
		if ((profname == NULL || execp->name == NULL ||
		    strcmp(profname, execp->name) == 0) &&
		    (type == NULL || execp->type == NULL ||
		    strcmp(type, execp->type) == 0) &&
		    (id == NULL || execp->id == NULL ||
		    strcmp(id, execp->id) == 0)) {
			/* We have a match */
			break;
		}
	}

	return (execp);
}

void
free_execattr(execattr_t *exec)
{
	if (exec != NULL) {
		free(exec->name);
		free(exec->type);
		free(exec->policy);
		free(exec->res1);
		free(exec->res2);
		free(exec->id);
		_kva_free(exec->attr);
		free_execattr(exec->next);
		free(exec);
	}
}

typedef struct call {
	const char	*type;
	const char	*id;
	int		sflag;
} call;

typedef struct result {
	execattr_t *head;
	execattr_t *prev;
} result;

/*ARGSUSED*/
static int
findexecattr(const char *prof, kva_t *kva, void *ctxt, void *res)
{
	execattr_t *exec;
	call *c = ctxt;
	result *r = res;

	if ((exec = getexecprof(prof, c->type, c->id, c->sflag)) != NULL) {
		if (IS_GET_ONE(c->sflag)) {
			r->head = exec;
			return (1);
		} else if (IS_GET_ALL(c->sflag)) {
			if (r->head == NULL) {
				r->head = exec;
				r->prev = get_tail(r->head);
			} else {
				r->prev->next = exec;
				r->prev = get_tail(exec);
			}
		}
	}
	return (0);
}


static execattr_t *
userprof(const char *username, const char *type, const char *id,
    int search_flag)
{

	char		pwdb[NSS_BUFLEN_PASSWD];
	struct passwd	pwd;
	call		call;
	result		result;

	/*
	 * Check if specified username is valid user
	 */
	if (getpwnam_r(username, &pwd, pwdb, sizeof (pwdb)) == NULL) {
		return (NULL);
	}

	result.head = result.prev = NULL;
	call.type = type;
	call.id = id;
	call.sflag = search_flag;

	(void) _enum_profs(username, findexecattr, &call, &result);

	return (result.head);
}


static execattr_t *
get_tail(execattr_t *exec)
{
	execattr_t *i_exec = NULL;
	execattr_t *j_exec = NULL;

	if (exec != NULL) {
		if (exec->next == NULL) {
			j_exec = exec;
		} else {
			for (i_exec = exec->next; i_exec != NULL;
			    i_exec = i_exec->next) {
				j_exec = i_exec;
			}
		}
	}

	return (j_exec);
}

/*
 * The getexecuser/getexecprof can be called with a search_flag
 * GET_ONE or GET_ALL.  The underlying NSS routines return execstr_ts
 * and we need to convert them to execattr_ts.  The NSS interface
 * really allows only one result and so we need to save them.
 * We use this in _doexeclist and at the end of the function we
 * need to run _exec_cleanup.
 * When we convert the execstr_ts to execattr_ts, we need to free the
 * intermediate execstr_ts.
 * The list of execstr_ts is partially allocated but partially pointing
 * to the stack or to (a part of) another buffer.  When we convert we
 * free all memory except if they match the original result pointer;
 * in that case, both the execstr_t and the strings point to memory
 * allocated in another way.
 */
execattr_t *
_execstr2attr(execstr_t *es, execstr_t *orig)
{
	execattr_t	*newexec;

	if (es == NULL) {
		return (NULL);
	}
	if ((newexec = malloc(sizeof (execattr_t))) == NULL) {
		return (NULL);
	}

	newexec->name = _do_unescape(es->name);
	newexec->policy = _do_unescape(es->policy);
	newexec->type = _do_unescape(es->type);
	newexec->res1 =  _do_unescape(es->res1);
	newexec->res2 = _do_unescape(es->res2);
	newexec->id = _do_unescape(es->id);
	newexec->attr = _str2kva(es->attr, KV_ASSIGN, KV_DELIMITER);
	if (es->next) {
		newexec->next = _execstr2attr(es->next, orig);
		es->next = NULL;
	} else {
		newexec->next = NULL;
	}
	if (es != orig)
		_free_execstr(es);

	return (newexec);
}

#ifdef DEBUG
void
print_execattr(execattr_t *exec)
{
	extern void print_kva(kva_t *);
	char *empty = "empty";

	if (exec != NULL) {
		printf("name=%s\n", exec->name ? exec->name : empty);
		printf("policy=%s\n", exec->policy ? exec->policy : empty);
		printf("type=%s\n", exec->type ? exec->type : empty);
		printf("res1=%s\n", exec->res1 ? exec->res1 : empty);
		printf("res2=%s\n", exec->res2 ? exec->res2 : empty);
		printf("id=%s\n", exec->id ? exec->id : empty);
		printf("attr=\n");
		print_kva(exec->attr);
		fflush(stdout);
		if (exec->next) {
			print_execattr(exec->next);
		}
	} else {
		printf("NULL\n");
	}
}
#endif  /* DEBUG */

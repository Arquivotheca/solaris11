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
#include <ns_sldap.h>
#include <sys/stat.h>
#include <ns_internal.h>
#include <libintl.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <nsswitch.h>
#include "repops.h"
#include <userdefs.h>
#include <tsol/label.h>

#define	LDAP_MAX_NAMELEN	1024
#define	MSG_BUFFER_LEN		512
#define	LINESZ			4096

#define	_DN_AUTHATTR		"cn=%s,ou=SolarisAuthAttr,"
#define	_DN_PROFATTR		"cn=%s,ou=SolarisProfAttr,"
#define	_DN_PASSWD		"uid=%s,ou=people,"
#define	_DN_GROUP		"cn=%s,ou=group,"
#define	_DN_PROJECT		"SolarisProjectName=%s,ou=projects,"
#define	_DN_PROFATTR		"cn=%s,ou=SolarisProfAttr,"
#define	_DN_AUTOHOME		"automountKey=%s,automountMapName=%s,"
#define	MEMBER_UID_ATTR		"memberUid"
#define	_DN_EXECATTR		"cn=%s+SolarisKernelSecurityPolicy=solaris+" \
	"SolarisProfileType=cmd+SolarisProfileId=%s,ou=SolarisProfAttr,"
#define	_DN_TNRHDB		"ipTnetNumber=%s,ou=ipTnet,"
#define	_DN_TNRHTP		"ipTnetTemplateName=%s,ou=ipTnet,"

static char *ldap_backend = NSS_REP_LDAP;
static char *automountInformation = "automountInformation";

static int ldap_get_pwnam(char *, struct passwd **, nss_XbyY_buf_t *);
static int ldap_get_pwid(uid_t, struct passwd **, nss_XbyY_buf_t *);
static void ldap_set_pwent();

static int ldap_get_spnam(char *, struct spwd **, nss_XbyY_buf_t *);
static void ldap_set_spent();

static int ldap_get_usernam(char *, userattr_t **, nss_XbyY_buf_t *);
static int ldap_get_useruid(uid_t, userattr_t **, nss_XbyY_buf_t *);
static void ldap_set_userattr();

static int ldap_get_grnam(char *, struct group **, nss_XbyY_buf_t *);
static int ldap_get_grid(gid_t, struct group **, nss_XbyY_buf_t *);
static void ldap_set_group();

static int ldap_get_projnam(char *, struct project **, nss_XbyY_buf_t *);
static int ldap_get_projid(projid_t, struct project **, nss_XbyY_buf_t *);
static void ldap_set_project();

static int ldap_get_profnam(char *, profattr_t **, nss_XbyY_buf_t *);
static void ldap_set_profattr();

static int ldap_get_authnam(char *, authattr_t **, nss_XbyY_buf_t *);
static void ldap_set_authattr();


static int ldap_put_group(struct group *, char *, int);

static int ldap_put_project(struct project *, int);

static int ldap_put_profattr(profattr_t *, int);

static execattr_t *ldap_get_execprof(char *, char *, char *, int,
    nss_XbyY_buf_t *);
static execattr_t *ldap_get_execuser(char *, char *, char *, int,
    nss_XbyY_buf_t *);

static int ldap_put_execattr(execattr_t *, int);
static void ldap_set_execattr();

static int ldap_put_authattr(authattr_t *, int);
static int ldap_edit_groups(char *, char *, struct group_entry **, int);
static int ldap_edit_projects(char *, char *, projid_t *, int);
static int ldap_edit_autohome(char *, char *, char *, int);
static int ldap_get_autohome(char *, char *);
static int ldap_put_tnrhdb(tsol_rhent_t *, int);
static int ldap_get_tnrhtp(const char *, tsol_tpent_t **, nss_XbyY_buf_t *);
static int ldap_put_tnrhtp(tsol_tpent_t *, int);
static void ldap_set_rhent(int);

/*
 * ldap function pointers
 */

sec_repops_t sec_ldap_rops = {
	ldap_get_pwnam,
	ldap_get_pwid,
	get_pwent,
	ldap_set_pwent,
	end_ent,
	ldap_get_spnam,
	get_spent,
	ldap_set_spent,
	end_ent,
	ldap_get_usernam,
	ldap_get_useruid,
	get_userattr,
	ldap_set_userattr,
	end_ent,
	ldap_get_grnam,
	ldap_get_grid,
	ldap_put_group,
	get_group,
	ldap_set_group,
	end_ent,
	ldap_get_projnam,
	ldap_get_projid,
	ldap_put_project,
	get_project,
	ldap_set_project,
	end_ent,
	ldap_get_profnam,
	ldap_put_profattr,
	get_profattr,
	ldap_set_profattr,
	end_ent,
	ldap_get_execprof,
	ldap_get_execuser,
	ldap_put_execattr,
	get_execattr,
	ldap_set_execattr,
	end_ent,
	ldap_get_authnam,
	ldap_put_authattr,
	get_authattr,
	ldap_set_authattr,
	end_ent,
	ldap_edit_groups,
	ldap_edit_projects,
	ldap_edit_autohome,
	ldap_get_autohome,
	ldap_put_tnrhdb,
	ldap_set_rhent,
	get_rhent,
	end_rhent,
	ldap_get_tnrhtp,
	ldap_put_tnrhtp
};

static char *_nsw_search_path = NULL;
static int tsol_rh_stayopen = 0;

static void
nss_sec_initf_execattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_EXECATTR;
	p->config_name    = NSS_DBNAM_PROFATTR; /* use config for "prof_attr" */
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = ldap_backend;
}
static void
nsw_sec_initf_execattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_EXECATTR;
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = ldap_backend;
}

static void
nsw_sec_initf_profattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_PROFATTR;
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = ldap_backend;
}

static int
ldap_get_pwnam(char *name, struct passwd **result, nss_XbyY_buf_t *b)
{
	return (get_db_ent(SEC_REP_DB_PASSWD, ldap_backend, -1, name,
	    (void**) result, b));
}

int
ldap_get_pwid(uid_t uid, struct passwd **result, nss_XbyY_buf_t *b)
{
	return (get_db_ent(SEC_REP_DB_PASSWD, ldap_backend, uid, NULL,
	    (void**)result, b));
}

static void
ldap_set_pwent()
{
	set_ent(SEC_REP_DB_PASSWD, ldap_backend);
}

static int
ldap_get_spnam(char *name, struct spwd **result, nss_XbyY_buf_t *b)
{
	return (get_db_ent(SEC_REP_DB_SHADOW, ldap_backend, -1, name,
	    (void**) result, b));
}

static void
ldap_set_spent()
{
	set_ent(SEC_REP_DB_SHADOW, ldap_backend);
}

static int
ldap_get_usernam(char *name, userattr_t **result, nss_XbyY_buf_t *b)
{
	return (get_usernam(ldap_backend, name, result, b));
}

static int
ldap_get_useruid(uid_t uid, userattr_t **result, nss_XbyY_buf_t *b)
{
	struct passwd *pwd;

	if (result) {
		if (ldap_get_pwid(uid, &pwd, b) == SEC_REP_SUCCESS) {
			if (pwd)
				return (ldap_get_usernam(pwd->pw_name,
				    result, b));
			else
				return (SEC_REP_NOT_FOUND);
		}
	}

	return (SEC_REP_INVALID_ARG);
}

static void
ldap_set_userattr()
{
	set_ent(SEC_REP_DB_USERATTR, ldap_backend);
}

static int
ldap_get_grnam(char *name, struct group **result, nss_XbyY_buf_t *b)
{
	return (get_db_ent(SEC_REP_DB_GROUP, ldap_backend, -1, name,
	    (void**) result, b));
}

static int
ldap_get_grid(gid_t gid, struct group **result, nss_XbyY_buf_t *b)
{
	return (get_db_ent(SEC_REP_DB_GROUP, ldap_backend, gid, NULL,
	    (void**) result, b));
}

static void
ldap_set_group()
{
	set_ent(SEC_REP_DB_GROUP, ldap_backend);
}

static int
ldap_get_projnam(char *name, struct project **result, nss_XbyY_buf_t *b)
{
	return (get_db_ent(SEC_REP_DB_PROJECT, ldap_backend, -1, name,
	    (void**) result, b));
}

static int
ldap_get_projid(projid_t projid, struct project **result, nss_XbyY_buf_t *b)
{
	return (get_db_ent(SEC_REP_DB_PROJECT, ldap_backend, projid, NULL,
	    (void**) result, b));
}

static void
ldap_set_project()
{
	set_ent(SEC_REP_DB_PROJECT, ldap_backend);
}

static int
ldap_get_profnam(char *name, profattr_t **result, nss_XbyY_buf_t *b)
{
	return (get_profnam(ldap_backend, name, result, b));
}

static void
ldap_set_profattr()
{
	set_ent(SEC_REP_DB_PROFATTR, ldap_backend);
}

static int
ldap_get_authnam(char *name, authattr_t **result, nss_XbyY_buf_t *b)
{
	return (get_authnam(ldap_backend, name, result, b));
}

void
ldap_set_authattr()
{
	set_ent(SEC_REP_DB_AUTHATTR, ldap_backend);
}

void
check_ldap_rc(int rc, ns_ldap_error_t *errorp)
{
	char msg[512];
	(void) memset(msg, '\0', sizeof (msg));
	switch (rc) {
	case NS_LDAP_SUCCESS:
		break;
	case NS_LDAP_OP_FAILED:
		(void) snprintf(msg, sizeof (msg),
		    (char *)gettext("ldap: operation failed.\n"));
		break;
	case NS_LDAP_INVALID_PARAM:
		(void) snprintf(msg, sizeof (msg),
		    (char *)gettext("ldap: invalid parameter(s) passed.\n"));
		break;
	case NS_LDAP_NOTFOUND:
		(void) snprintf(msg, sizeof (msg),
		    (char *)gettext("ldap: entry not found.\n"));
		break;
	case NS_LDAP_MEMORY:
		(void) snprintf(msg, sizeof (msg),
		    (char *)gettext("ldap: internal memory \
		    allocation error.\n"));
		break;
	case NS_LDAP_CONFIG:
		(void) snprintf(msg, sizeof (msg),
		    (char *)gettext("LDAP configuration problem.\n"));
		break;
	case NS_LDAP_PARTIAL:
		(void) snprintf(msg, sizeof (msg),
		    (char *)gettext("ldap partial result returned\n"));
		break;
	case NS_LDAP_INTERNAL:
		if (errorp && errorp->status == LDAP_ALREADY_EXISTS)
			(void) snprintf(msg, sizeof (msg),
			    (char *)gettext("ldap object already exists.\n"));

		if (errorp && errorp->status == LDAP_NO_SUCH_OBJECT)
			(void) snprintf(msg, sizeof (msg),
			    (char *)gettext("ldap object does not exist.\n"));

		if (errorp && errorp->status == LDAP_INSUFFICIENT_ACCESS) {
			(void) snprintf(msg, sizeof (msg),
			    (char *)gettext("ldap bind user does not have \
			    permission to add/modify entries\n"));
		}
		if (errorp == NULL) {
			(void) snprintf(msg, sizeof (msg),
			    (char *)gettext("NS_LDAP_INTERNAL error\n"));
		}

		break;
	default:
		if (errorp && errorp->status)
			(void) snprintf(msg, sizeof (msg),
			    gettext("ldap err status = %d \n"),
			    errorp->status);

		if (errorp && errorp->message)
			(void) snprintf(msg, sizeof (msg),
			    gettext("ldap err message = %s \n"),
			    gettext(errorp->message));
	}
	if (errorp != NULL) {
		(void) __ns_ldap_freeError(&errorp);
	}
	if (rc != NS_LDAP_SUCCESS) {
		(void) fprintf(stderr, "%s", msg);
	}
}

static int
ldap_add_entry(void *entry, char *dn, int addflag, const char *type)
{
	int rc = 0;
	ns_ldap_error_t *errorp = NULL;
	ns_cred_t authority;

	(void) memset(&authority, 0, sizeof (authority));
	rc = __ns_ldap_addTypedEntry(type, dn, entry,
	    addflag, &authority,
	    NS_LDAP_FOLLOWREF | NS_LDAP_KEEP_CONN | NS_LDAP_UPDATE_SHADOW,
	    &errorp);

	check_ldap_rc(rc, errorp);
	return (rc);
}

static int
ldap_delete_entry(char *dn, char *type, ns_cred_t *auth)
{
	int rc;
	ns_ldap_error_t *errorp = NULL;

	rc = __ns_ldap_delEntry(type, dn, auth,
	    NS_LDAP_FOLLOWREF | NS_LDAP_KEEP_CONN | NS_LDAP_UPDATE_SHADOW,
	    &errorp);

	check_ldap_rc(rc, errorp);
	return (rc);
}

static char *
ldap_get_basedn(const char *fmt_str, char *value)
{
	char basedn[LDAP_MAX_NAMELEN];
	ns_ldap_error_t *errorp = NULL;
	void **paramValue = NULL;
	int i = 0, rc = 0;
	char *inputbasedn = NULL;
	char *p = NULL;

	rc = __ns_ldap_getParam(NS_LDAP_BINDDN_P, (void***) &paramValue,
	    &errorp);

	if (rc == NS_LDAP_SUCCESS) {
		if (paramValue != NULL && *paramValue != NULL) {
			(void) snprintf(basedn, sizeof (basedn),
			    fmt_str, value);
			p = (char *)strstr((char *)(*paramValue), "dc=");
			if (p != NULL) {
				(void) strlcat(basedn, p, sizeof (basedn));
			}
			i = strlen(basedn);
			if (i) {
				inputbasedn = (char *)malloc(i + 1);
				if (inputbasedn)
					(void) strlcpy(inputbasedn,
					    basedn, i + 1);
			}
			(void) __ns_ldap_freeParam((void***) & paramValue);
		}
	} else {
		(void) __ns_ldap_freeError(&errorp);
	}
	return (inputbasedn);
}

static int
ldap_remove_attr(void *entry, char *attrname, char *value, int type)
{
	int rc = 0;
	char *dn = NULL;
	ns_ldap_attr_t *attrs[2];
	ns_ldap_error_t *errorp = NULL;
	ns_cred_t authority;
	const ns_ldap_attr_t * const *aptr;
	char **values;
	char *service;

	(void) memset(&authority, 0, sizeof (authority));

	if (!entry || !attrname || !value)
		return (SEC_REP_INVALID_ARG);

	switch (type) {
	case 0: /* group */
	{
		dn = ldap_get_basedn(_DN_GROUP,
		    ((struct group *)entry)->gr_name);
		service = "group";
	};
		break;
	case 1: /* project */
	{
		dn = ldap_get_basedn(_DN_PROJECT,
		    ((struct project *)entry)->pj_name);
		service = "project";
	};
		break;
	};
	if (dn == NULL) {
		char *fmt = gettext("Could not get base dn.\n");
		(void) fprintf(stderr, gettext(fmt));
		return (-1);
	}
	attrs[0] = (ns_ldap_attr_t *)calloc(1, sizeof (ns_ldap_attr_t));
	if (attrs[0] == NULL) {
		free(dn);
		return (SEC_REP_NOMEM);
	}
	attrs[0]->attrname = attrname;
	values = (char **)calloc(2, sizeof (char *));
	if (values == NULL) {
		free(dn);
		free(attrs[0]);
		return (SEC_REP_NOMEM);
	}
	values[0] = value;
	values[1] = NULL;
	attrs[0]->attrvalue = values;

	attrs[0]->value_count = 1;
	attrs[1] = NULL;
	aptr = (const ns_ldap_attr_t *const *)attrs;

	rc = __ns_ldap_delAttr(service, dn, aptr, &authority,
	    NS_LDAP_FOLLOWREF | NS_LDAP_UPDATE_SHADOW,
	    &errorp);
	free(dn);
	free(attrs[0]);

	return (rc);
}

static int
ldap_del_group(char *groupname)
{
	int rc = 0;
	if (groupname != NULL) {

		ns_cred_t authority;
		char *inputbasedn = NULL;

		inputbasedn = ldap_get_basedn(_DN_GROUP, groupname);
		if (inputbasedn == NULL) {
			return (SEC_REP_SERVER_ERROR);
		}

		(void) memset(&authority, 0, sizeof (authority));

		rc = ldap_delete_entry(inputbasedn, NS_LDAP_TYPE_GROUP,
		    &authority);

		if (inputbasedn != NULL)
			free(inputbasedn);
	} else {
		return (SEC_REP_INVALID_ARG);
	}
	return (rc);
}

static int
ldap_put_group(struct group *group, char *newgroup, int flags)
{
	int rc = 0;
	int add = 0;

	if (group == NULL)
		return (SEC_REP_INVALID_ARG);

	if (flags & ADD_MASK) {
		rc = ldap_add_entry((void *)group, NULL, 1,
		    NS_LDAP_TYPE_GROUP);

	} else if (flags & MOD_MASK) {
		if (flags & GRP_N_MASK) {

			rc = ldap_del_group(group->gr_name);

			if (rc != SEC_REP_SUCCESS)
				return (rc);

			group->gr_name = newgroup;
			add = 1;
		}

		rc = ldap_add_entry((void *)group, NULL, add,
		    NS_LDAP_TYPE_GROUP);
	} else if (flags & DEL_MASK) {
		rc = ldap_del_group(group->gr_name);
	} else {
		return (SEC_REP_OP_NOT_SUPPORTED);
	}
	return (rc);
}

static int
ldap_put_project(struct project *proj, int flags)
{
	int rc = 0;
	int add = 0;

	if (proj == NULL)
		return (SEC_REP_INVALID_ARG);

	if (flags & ADD_MASK) {
		rc = ldap_add_entry((void *)proj, NULL, 1,
		    NS_LDAP_TYPE_PROJECT);

	} else if (flags & MOD_MASK) {

		rc = ldap_add_entry((void *)proj, NULL, add,
		    NS_LDAP_TYPE_PROJECT);

	}
	return (rc);

}


static execattr_t *
ldap_get_execprof(char *profname, char *type, char *id, int search_flag,
    nss_XbyY_buf_t *b)
{

	struct nss_calls nss_calls;

	nss_calls.initf_nss_exec = nss_sec_initf_execattr;
	nss_calls.initf_nsw_prof = nsw_sec_initf_profattr;
	nss_calls.initf_nsw_exec = nsw_sec_initf_execattr;
	nss_calls.pnsw_search_path = &_nsw_search_path;
	return (get_execprof(profname, type, id, search_flag, b, &nss_calls));
}

/*ARGSUSED*/
static execattr_t *
ldap_get_execuser(char *profname, char *type, char *id, int search_flag,
    nss_XbyY_buf_t *b)
{
	return (NULL);
}



static void
ldap_set_execattr()
{
	set_ent(SEC_REP_DB_EXECATTR, ldap_backend);
}

static int
ldap_del_authattr(char *authname)
{
	int rc = 0;
	if (authname) {

		ns_cred_t authority;
		char *inputbasedn = NULL;

		inputbasedn = ldap_get_basedn(_DN_AUTHATTR, authname);
		if (inputbasedn == NULL) {
			return (SEC_REP_SERVER_ERROR);
}

		(void) memset(&authority, 0, sizeof (authority));

		rc = ldap_delete_entry(inputbasedn, NS_LDAP_TYPE_AUTHATTR,
		    &authority);

		if (inputbasedn)
			free(inputbasedn);
	} else {
		return (SEC_REP_INVALID_ARG);
}
	return (rc);
}

static int
ldap_put_authattr(authattr_t *auth, int flags)
{
	int rc = 0;
	authstr_t auth_st;
	char buf[NSS_BUFLEN_AUTHATTR];
	kv_t *kv_pair;
	char *b = &buf[0];


	if (!auth)
		return (SEC_REP_INVALID_ARG);

	auth_st.name = auth->name;
	auth_st.short_desc = auth->short_desc;
	auth_st.long_desc = auth->long_desc;
	auth_st.res1 = auth->res1;
	auth_st.res2 = auth->res2;
	auth_st.attr = "";

	if ((flags & AUTH_MOD_MASK || flags & AUTH_ADD_MASK) &&
	    auth->attr && auth->attr->data) {
		kv_pair = auth->attr->data;
		make_attr_string(kv_pair, auth->attr->length, buf,
		    sizeof (buf));
		auth_st.attr = b;
		}
	if (flags & AUTH_ADD_MASK) {
		rc = ldap_add_entry((void *)&auth_st, NULL, 1,
		    NS_LDAP_TYPE_AUTHATTR);

	} else if (flags & AUTH_MOD_MASK) {
		rc = ldap_add_entry((void *)&auth_st, NULL, 0,
		    NS_LDAP_TYPE_AUTHATTR);

	} else if (flags & AUTH_DEL_MASK) {
		rc = ldap_del_authattr(auth_st.name);
	}
	return (rc);
}


int
remove_user_from_list(char **memlist, char *login)
{
	int modified = 0;
	char **memptr;
	int login_len;

	if (memlist != NULL) {
		login_len = strlen(login);
		for (memptr = memlist; *memptr;
		    memptr++) {
			if (strncmp(*memptr, login, login_len) == 0) {
				/* Delete this one */
				char **from = memptr + 1;

				do {
					*(from - 1) = *from;
				} while (*from++);
				*(from - 1) = NULL;

				modified++;
				break;
			}
		}
	}
	return (modified);
}

static int
add_user_to_list(char ***memlist, char *login, char *new_login)
{
	int j = 0, k = 0, modified = 0, exist = 0;
	char *user;
	char **temp, **memptr;
	int login_len;

	if (*memlist != NULL) {
		user = (new_login ? new_login : login);
		login_len = strlen(user);
		for (memptr = *memlist; *memptr; memptr++, j++) {
			if (strncmp(*memptr, user, login_len) == 0)
				exist++;
		}

		if (!exist) {
			temp = (char **)calloc((j + 2), sizeof (char *));

			if (temp) {
				memptr = *memlist;
				for (k = 0; k < j; k++) {
					temp[k] = (char *)strdup(memptr[k]);
					if (temp[k] == NULL)
						return (-1);
				}
				temp[j++] = (char *)strdup(user);
				temp[j++] = NULL;
				*memlist = temp;
				modified++;
			} else {
				return (-1);
			}
		}
	}
	return (modified);
}

static int
ldap_edit_groups(char *login, char *new_login, struct group_entry *gids[],
    int overwrite)
{
	struct group *g_ptr; /* group structure from fgetgrent */
	int modified = 0;
	int i;
	nss_XbyY_buf_t *buf = NULL;
	int rc = 0, error = 0;

	init_nss_buffer(SEC_REP_DB_GROUP, &buf);
	ldap_set_group();
	while ((g_ptr = (struct group *)get_group(buf)) != NULL) {

		modified = 0;
		if (overwrite || gids == NULL) {
			/* remove user from group */
			modified = remove_user_from_list(g_ptr->gr_mem, login);
		}

		/* now check to see if group is one to add to */
		if (gids != NULL) {
			for (i = 0; gids[i] != NULL; i++) {
				if (g_ptr->gr_gid == gids[i]->gid) {
					/*
					 * if group name is present
					 * make sure it matches the
					 * /etc/group entry.
					 */
					if (gids[i]->group_name &&
					    strcmp(gids[i]->group_name,
					    g_ptr->gr_name))
						continue;
					error = 0;
					rc = add_user_to_list(&g_ptr->gr_mem,
					    login, new_login);
					if (rc == -1) {
						error = 1;
						break;
					}
					modified += rc;
				}
			}
		}
		if (error)
			break;
		if (modified) {
			if (g_ptr->gr_mem[0]) {
				rc = ldap_put_group(g_ptr, NULL, MOD_MASK);
			} else {
				rc = ldap_remove_attr(g_ptr, MEMBER_UID_ATTR,
				    login, 0);
			}
			if (rc != SEC_REP_SUCCESS) {
				error = 1;
				break;
			}
		}
		if (error)
			break;

	}
	end_ent();
	if (error) {
		char *fmt = gettext("Error updating group %s with %s.\n");
		(void) fprintf(stderr, gettext(fmt), g_ptr->gr_name, login);
		if (g_ptr != NULL)
			free(g_ptr);
		free_nss_buffer(&buf);
		exit(-6);
	}
	if (g_ptr != NULL)
		free(g_ptr);
	free_nss_buffer(&buf);
	return (rc);
}

static int
ldap_edit_projects(char *login, char *new_login, projid_t *projids,
    int overwrite)
{
	struct project *p_ptr;
	int modified = 0;
	int i, rc = 0, error = 0;
	nss_XbyY_buf_t *buf = NULL;

	init_nss_buffer(SEC_REP_DB_PROJECT, &buf);
	ldap_set_project();
	while ((p_ptr = (struct project *)get_project(buf)) != NULL) {
		modified = 0;
		/* first delete the login from the project, if it's there */
		if (overwrite || projids == NULL) {
			if (p_ptr->pj_users != NULL) {
				modified =
				    remove_user_from_list(p_ptr->pj_users,
				    login);
			}
		}

		/* now check to see if project is one to add to */
		if (projids != NULL) {
			for (i = 0; (long)projids[i] != -1; i++) {
				if (p_ptr->pj_projid == (long)projids[i]) {
					error = 0;
					rc = add_user_to_list(
					    &(p_ptr->pj_users), login,
					    new_login);

					if (rc == -1) {
						error = 1;
						break;
					}
					modified += rc;
				}
			}
		}
		if (error)
			break;

		if (modified) {
			if (p_ptr->pj_users[0]) {
				rc = ldap_put_project(p_ptr, MOD_MASK);
			} else {
				rc = ldap_remove_attr(p_ptr, MEMBER_UID_ATTR,
				    login, 1);
			}
			if (rc != SEC_REP_SUCCESS) {
				error = 1;
				break;
			}
		}
		if (error)
			break;

	}
	end_ent();
	if (error) {
		char *fmt = gettext("Error updating project %s with %s.\n");
		(void) fprintf(stderr, gettext(fmt), p_ptr->pj_name, login);
		if (p_ptr != NULL)
			free(p_ptr);
		free_nss_buffer(&buf);
		return (rc);
	}
	if (p_ptr != NULL)
		free(p_ptr);
	free_nss_buffer(&buf);
	return (rc);
}

static int
ldap_get_autohome_entry(struct _ns_automount *data)
{
	char *ldapkey;
	const char *dfltfilter = "(&(objectClass=automount)(automountKey=%s))";
	char filter[256];
	int rc;
	ns_ldap_error_t *errorp = NULL;
	ns_ldap_result_t *result = NULL;
	ns_ldap_entry_t *entry = NULL;
	ns_cred_t authority;

	(void) memset(&authority, 0, sizeof (authority));
	ldapkey = data->key;

	(void) snprintf(filter, sizeof (filter), dfltfilter, ldapkey);
	rc = __ns_ldap_list(get_autohome_db(), filter, NULL, NULL, NULL,
	    0, &result, &errorp, NULL, NULL);

	if (rc == NS_LDAP_SUCCESS) {
		entry = result->entry;
		if (result->entries_count) {
			int i,  ldap_len;
			for (i = 0; i < entry->attr_count; i++) {
				ns_ldap_attr_t *attr;

				attr = entry->attr_pair[i];
				if (strcmp(attr->attrname,
				    automountInformation) == 0) {
					char *attrval;

					attrval = attr->attrvalue[0];
					ldap_len = strlen(attrval);

					/*
					 * so check for the length;
					 * it should be less than
					 * LINESZ
					 */
					if ((ldap_len + 2) > LINESZ) {
						(void) fprintf(stderr,
						    "ldap server map %s, entry "
						    "for %s is too long %d"
						    " (char *)s (max %d)",
						    data->mapname, ldapkey,
						    (ldap_len + 2), LINESZ);

						(void) __ns_ldap_freeResult(
						    &result);
						return (SEC_REP_CALL_ERROR);
					}
					data->value =
					    (char *)malloc(ldap_len + 2);

					if (data->value == NULL) {
						(void) fprintf(stderr,
						    "ldap_match: "
						    "malloc failed");

						(void) __ns_ldap_freeResult(
						    &result);
						return (SEC_REP_CALL_ERROR);
					}

					(void) snprintf(data->value,
					    (ldap_len + 2), "%s", attrval);
					break;
				}
			}
		}
		(void) __ns_ldap_freeError(&errorp);
		(void) __ns_ldap_freeResult(&result);

	}
	if (rc != NS_LDAP_SUCCESS) {
		(void) __ns_ldap_freeError(&errorp);
	}
	return (rc);
}

static int
ldap_del_autohome(char *login)
{
	int rc = 0;
	if (login) {

		ns_cred_t authority;
		char basedn[LDAP_MAX_NAMELEN];
		char *inputbasedn = NULL;

		(void) snprintf(basedn, sizeof (basedn), _DN_AUTOHOME,
		    "%s", get_autohome_db());
		inputbasedn = ldap_get_basedn(basedn, login);
		if (inputbasedn == NULL) {
			return (SEC_REP_SERVER_ERROR);
		}

		(void) memset(&authority, 0, sizeof (authority));

		rc = ldap_delete_entry(inputbasedn, AUTOHOME,
		    &authority);

		free(inputbasedn);
	} else {
		return (SEC_REP_INVALID_ARG);
	}
	return (rc);
}

static int
ldap_edit_autohome(char *login, char *new_login, char *path, int flags)
{
	struct _ns_automount autohome;
	int rc = 0;
	int create = 1;

	autohome.key = login;
	autohome.mapname = get_autohome_db();

	if (flags & DEL_MASK) {
		if (ldap_get_autohome_entry(&autohome) == NS_LDAP_SUCCESS)
		rc = ldap_del_autohome(login);
		else
			rc = 0;
	} else {
		/* check to see if auto_home is present */
		/* if so replace otherwise add */
		/* for logname (char *)nge delete and add a new entry. */
		rc = ldap_get_autohome_entry(&autohome);
		if (rc == NS_LDAP_NOTFOUND && path) {
			autohome.value = path;
			rc = ldap_add_entry(&autohome, NULL, create,
			    get_autohome_db());
		} else if (rc == NS_LDAP_SUCCESS) {
			if (path) {
				autohome.value = path;
			}
			if (new_login) {
				struct _ns_automount ah;
				ah.key = new_login;
				ah.mapname = autohome.mapname;
				/*
				 * checking to see if auto_home entry
				 * exists for new_login.
				 */

				rc = ldap_get_autohome_entry(&ah);
				if (rc == NS_LDAP_SUCCESS) {
					if (ah.value != NULL)
						free(ah.value);
					rc = ldap_del_autohome(new_login);
					if (rc != NS_LDAP_SUCCESS)
						return (rc);
				}
				rc = ldap_del_autohome(login);

				if (rc != NS_LDAP_SUCCESS)
					return (rc);
				autohome.key = new_login;
			} else create = 0;
			rc = ldap_add_entry(&autohome, NULL, create,
			    get_autohome_db());
		}
	}

	return (rc);
}

static int
ldap_get_autohome(char *login, char *path)
{
	int rc;

	if (login) {
		struct _ns_automount autohome;
		autohome.key = login;
		autohome.mapname = get_autohome_db();
		rc = ldap_get_autohome_entry(&autohome);
		if (rc == SEC_REP_SUCCESS) {
			(void) strcpy(path, autohome.value);
		}
	}
	return (rc);
}

static int
ldap_put_profattr(profattr_t *prof, int flags)
{
	int rc = 0;
	profstr_t prof_st;
	char buf[NSS_BUFLEN_PROFATTR];
	kv_t *kv_pair;
	char *b = &buf[0];
	int add_flag = 0;


	if (prof == NULL || prof->name == NULL)
		return (SEC_REP_INVALID_ARG);

	if (flags & ADD_MASK)
		add_flag = 1;
	prof_st.name = prof->name;
	prof_st.desc = prof->desc;
	prof_st.res1 = NULL;
	prof_st.res2 = NULL;
	prof_st.attr = "";

	if ((flags & ADD_MASK) || (flags & MOD_MASK)) {

		if (prof->attr != NULL && prof->attr->data != NULL) {
			kv_pair = prof->attr->data;
			make_attr_string(kv_pair, prof->attr->length, buf,
			    sizeof (buf));
			prof_st.attr = b;
		}
		rc = ldap_add_entry((void *)&prof_st, NULL, add_flag,
		    NS_LDAP_TYPE_PROFILE);
	} else {
		ns_cred_t authority;
		char *inputbasedn = NULL;

		inputbasedn = ldap_get_basedn(_DN_PROFATTR, prof->name);
		if (inputbasedn == NULL) {
			return (SEC_REP_SERVER_ERROR);
		}

		(void) memset(&authority, 0, sizeof (authority));

		rc = ldap_delete_entry(inputbasedn,
		    NS_LDAP_TYPE_PROFILE, &authority);

		if (inputbasedn != NULL)
			free(inputbasedn);
	}
	return (rc);
}


static int
ldap_put_execattr(execattr_t *exec, int flags)
{
	int rc = 0;
	execstr_t exec_st;
	char buf[NSS_BUFLEN_EXECATTR];
	kv_t *kv_pair;
	char *b = &buf[0];
	int add_flag;
	execattr_t *curr;


	if (exec == NULL || exec->name == NULL || exec->id == NULL)
		return (SEC_REP_INVALID_ARG);

	curr = exec;
	exec_st.name = curr->name;
	exec_st.policy = curr->policy;
	exec_st.type = curr->type;
	exec_st.id = curr->id;
	exec_st.res1 = NULL;
	exec_st.res2 = NULL;
	exec_st.attr = "";
	exec_st.next = NULL;

	add_flag =  (flags & ADD_MASK) ? 1:0;

	if (flags & ADD_MASK || flags & MOD_MASK) {
		if (curr->attr && curr->attr->data) {
			kv_pair = curr->attr->data;
			make_attr_string(kv_pair,
			    curr->attr->length, buf, sizeof (buf));
			exec_st.attr = b;
			rc = ldap_add_entry((void *)&exec_st, NULL,
			    add_flag, NS_LDAP_TYPE_EXECATTR);
		} else {
			return (SEC_REP_INVALID_ARG);
		}
	} else if (flags & DEL_MASK) {
		ns_cred_t authority;
		char *inputbasedn = NULL;
		char *exec_key = NULL;
		int len = 0;

		len = strlen(_DN_EXECATTR) + strlen(curr->name) +
		    strlen(curr->id) + 1;

		exec_key = malloc(len);
		if (exec_key == NULL)
			return (SEC_REP_NOMEM);
		(void) snprintf(exec_key, len, (const char *) _DN_EXECATTR,
		    curr->name, curr->id);

		inputbasedn = ldap_get_basedn(exec_key, NULL);
		if (inputbasedn == NULL) {
			return (SEC_REP_SERVER_ERROR);
		}

		(void) memset(&authority, 0, sizeof (authority));

		rc = ldap_delete_entry(inputbasedn,
		    NS_LDAP_TYPE_PROFILE, &authority);

		if (inputbasedn != NULL)
			free(inputbasedn);
		if (exec_key != NULL)
			free(exec_key);
	}

	return (rc);
}

static int
ldap_get_tnrhtp(const char *name, tsol_tpent_t **result, nss_XbyY_buf_t *b)
{
	return (get_tnrhtp(ldap_backend, name, result, b));
}


static void
ldap_set_rhent(int stay)
{
	tsol_rh_stayopen |= stay;
	set_ent(SEC_REP_DB_TNRHDB, ldap_backend);
}

static void
make_tnrhtp_attrs(char *buf, int len, tsol_tpent_t *tp)
{
	char *str;
	const m_label_t *l1, *l2;
	boolean_t first = B_TRUE;
	char *p = buf;
	char *end = buf+len;

	if (buf == NULL || len < NSS_BUFLEN_TSOL_TP || tp == NULL)
		return;

	switch (tp->host_type) {
	case UNLABELED:
		p += snprintf(p, end-p, "%s=%s;%s=%d;",
		    TP_HOSTTYPE, TP_UNLABELED,
		    TP_DOI, tp->tp_doi);

		if (tp->tp_mask_unl & TSOL_MSK_DEF_LABEL) {
			(void) l_to_str(&tp->tp_def_label, &str, M_INTERNAL);
			p += snprintf(p, end-p, "%s=%s;", TP_DEFLABEL, str);
			free(str);
		}
		break;
	case SUN_CIPSO:
		p += snprintf(p, end-p, "%s=%s;%s=%d;",
		    TP_HOSTTYPE, TP_CIPSO,
		    TP_DOI, tp->tp_doi);
		break;
	}
	if (tp->tp_mask_unl & TSOL_MSK_SL_RANGE_TSOL) {
		l_to_str(&tp->tp_gw_sl_range.lower_bound,
		    &str, M_INTERNAL);
		p += snprintf(p, end-p, "%s=%s;", TP_MINLABEL, str);
		free(str);

		l_to_str(&tp->tp_gw_sl_range.upper_bound, &str, M_INTERNAL);
		p += snprintf(p, end-p, "%s=%s;", TP_MAXLABEL, str);
		free(str);

		l1 = (const m_label_t *)&tp->tp_gw_sl_set[0];
		l2 = (const m_label_t *)&tp->tp_gw_sl_set[NSLS_MAX];
		for (; l1 < l2; l1++) {
			if (!BLTYPE(l1, SUN_SL_ID))
				continue;
			l_to_str(l1, &str, M_INTERNAL);
			if (first) {
				first = B_FALSE;
				p += snprintf(p, end-p, "%s=%s", TP_SET, str);
			} else {
				p += snprintf(p, end-p, ",%s", str);
			}
			free(str);
		}
		if (!first)
			p += snprintf(p, end-p, ";");
	}
}

static int
ldap_put_tnrhtp(tsol_tpent_t *tp, int flags)
{
	int rc = 0;
	tsol_tpstr_t tpstr;
	char buf[NSS_BUFLEN_TSOL_TP];
	int add_flag = 0;


	if (tp == NULL || tp->name == NULL)
		return (SEC_REP_INVALID_ARG);

	if (flags & ADD_MASK)
		add_flag = 1;
	tpstr.template = tp->name;
	tpstr.attrs = buf;

	if ((flags & ADD_MASK) || (flags & MOD_MASK)) {
		buf[0] = '\0';
		make_tnrhtp_attrs(buf, sizeof (buf), tp);
		rc = ldap_add_entry((void *)&tpstr, NULL, add_flag,
		    NS_LDAP_TYPE_TNRHTP);
	} else if (flags & DEL_MASK) {
		ns_cred_t authority;
		char *inputbasedn = NULL;

		inputbasedn = ldap_get_basedn(_DN_TNRHTP, tp->name);
		if (inputbasedn == NULL) {
			return (SEC_REP_SERVER_ERROR);
		}

		(void) memset(&authority, 0, sizeof (authority));

		rc = ldap_delete_entry(inputbasedn,
		    NS_LDAP_TYPE_TNRHTP, &authority);

		free(inputbasedn);
	}
	return (rc);
}


static int
ldap_put_tnrhdb(tsol_rhent_t *rhentp, int flags)
{
	int rc = 0;
	tsol_rhstr_t rhstr;
	int add_flag;
	int alen;
	char abuf[INET6_ADDRSTRLEN + 5];
	char *tmp;

	if (rhentp == NULL || rhentp->rh_template == NULL)
		return (SEC_REP_INVALID_ARG);

	(void) translate_inet_addr(rhentp, &alen, abuf, sizeof (abuf));
	tmp = (char *)_escape(abuf, ":");
	rhstr.address = tmp;
	rhstr.template = rhentp->rh_template;

	add_flag =  (flags & ADD_MASK) ? 1:0;

	if (flags & ADD_MASK || flags & MOD_MASK) {

		rc = ldap_add_entry((void *)&rhstr, NULL,
		    add_flag, NS_LDAP_TYPE_TNRHDB);
	} else if (flags & DEL_MASK) {
		ns_cred_t authority;
		char *inputbasedn = NULL;

		inputbasedn = ldap_get_basedn(_DN_TNRHDB, rhstr.address);
		if (inputbasedn == NULL) {
			return (SEC_REP_SERVER_ERROR);
		}

		(void) memset(&authority, 0, sizeof (authority));

		rc = ldap_delete_entry(inputbasedn,
		    NS_LDAP_TYPE_TNRHDB, &authority);

		free(inputbasedn);
	}
	if (tmp != NULL)
		free(tmp);

	return (rc);
}

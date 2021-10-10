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

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/



#include <stdio.h>
#include <sys/types.h>
#include <shadow.h>
#include <pwd.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <fcntl.h>
#include <secdb.h>
#include <user_attr.h>
#include <syslog.h>
#include <userdefs.h>
#include <auth_attr.h>
#include <stddef.h>
#include <priv.h>
#include <prof_attr.h>
#include <tsol/label.h>
#include <auth_list.h>
#include "passmgmt.h"
#include <users.h>
#include <ctype.h>

#define	NO_ATTR_MSG	"Cannot set"
#define	AUTH_SEP	","
#define	PROF_SEP	","
#define	ROLE_SEP	","

typedef struct privattrs
{
	char *privs;
} priv_attr_t;

typedef struct labelattrs
{
	char *label;
} label_attr_t;

typedef struct attrs
{
	char *attrvals[MAXPROFS];
	int attr_cnt;
} attrs_t;

extern kvopts_auth_t ua_opts[];
char err_mesg[CMT_SIZE];
extern int optn_mask;
extern int ua_keys;
extern int is_role;
extern int type_auth_check;
extern char *prognamp;
extern char *scope_type;

void
no_perm(int ret, char *txt)
{
	char msg[CMT_SIZE] = "%s: Permission denied. %s\n";
	(void) fprintf(stderr, gettext(msg), prognamp, gettext(txt));
	syslog(LOG_ERR, gettext(msg), prognamp, gettext(txt));
	exit(ret);
}


int
check_auths_for_passwd(gid_t gid, char *username)
{
	struct group *g_ptr;
	struct group_entry grpentry; /* entry containing value from -g */
	struct group_entry *grplist[2]; /* one null terminated entry */
	char assignauth[NSS_LINELEN_GROUP];

	/* check gid */
	if (G_MASK & optn_mask) {
		if ((g_ptr = getgrgid(gid)) != NULL) {
			grpentry.gid = g_ptr->gr_gid;
			grpentry.group_name = g_ptr->gr_name;
			grplist[0] = &grpentry;
			grplist[1] = NULL;
			(void) snprintf(assignauth, sizeof (assignauth),
			    "%s/%s", GROUP_ASSIGN_AUTH, g_ptr->gr_name);
		}

		/* Check for authorization */
		if (g_ptr == NULL ||
		    (chkauthattr(assignauth, username) == 0 &&
		    (chkauthattr(GROUP_MANAGE_AUTH, username) == 0 ||
		    check_groups_for_user(getuid(), grplist) != 0))) {
			(void) snprintf(err_mesg, sizeof (err_mesg),
			    "%s %s", NO_ATTR_MSG, "primary group.");
			no_perm(PEX_NO_AUTH, err_mesg);
		}
	}

	/* comment, homedir, logname, uid all use same auth */
	if ((C_MASK | H_MASK | L_MASK | U_MASK) & optn_mask) {
		char *auth;
		if (is_role) auth = ROLE_MANAGE_AUTH;
		else auth = USER_MANAGE_AUTH;
		if (chkauthattr(auth, username) == 0) {
			(void) snprintf(err_mesg, sizeof (err_mesg),
			    "%s %s, requires %s authorization.",
			    NO_ATTR_MSG, "passwd fields", auth);
			no_perm(PEX_NO_AUTH, err_mesg);
		}
	}
	if ((M_MASK & optn_mask) && (U_MASK & optn_mask)) {
		if (chkauthattr(PASSWD_ASSIGN_AUTH, username) == 0) {
			(void) snprintf(err_mesg, sizeof (err_mesg),
			    "%s %s, requires %s authorization.",
			    NO_ATTR_MSG, "uid field", PASSWD_ASSIGN_AUTH);
			no_perm(PEX_NO_AUTH, err_mesg);
		}
	}

	return (1);
}

int
check_auths_for_shadow(struct spwd *spwd, char *real_user)
{
	int check;

	if ((A_MASK & optn_mask)) {
		check = 0;
		if ((F_MASK & optn_mask) && (spwd->sp_inact > 0))
			check |= 1;
		if ((E_MASK & optn_mask) && (spwd->sp_expire > -1))
			check |= 1;
	} else check = 1;

	if (((F_MASK | E_MASK) & optn_mask) && check) {
		if (chkauthattr(ACCOUNT_SETPOLICY_AUTH, real_user) == 0) {
			char *field;

			if (F_MASK & optn_mask)
				field = "inactive field";
			else field = "expire field";
			(void) snprintf(err_mesg, sizeof (err_mesg),
			    "%s %s, requires %s authorization.",
			    NO_ATTR_MSG, field, ACCOUNT_SETPOLICY_AUTH);
			no_perm(PEX_NO_AUTH, err_mesg);

		}
	}
	return (1);
}

int
check_authorizations(char *values, char *user)
{
	if (!values || !user) {
		syslog(LOG_DEBUG,
		    gettext("Invalid arguments to check_authorizations"));
		return (0);
	}

	if (values && strlen(values)) {
		char *token, *attr_vals, *lasts;

		if (chkauthattr(AUTHORIZATION_ASSIGN_AUTH, user) == 0) {

			if (chkauthattr(AUTHORIZATION_DELEGATE_AUTH,
			    user) == 0) {
				(void) snprintf(err_mesg, sizeof (err_mesg),
				    "%s %s to %s, requires %s"
				    " authorization.", NO_ATTR_MSG,
				    "auth", values,
				    AUTHORIZATION_DELEGATE_AUTH);
				no_perm(PEX_NO_AUTH, err_mesg);
			}
			attr_vals = strdup(values);

			for (token = strtok_r(attr_vals, ",", &lasts);
			    token != NULL;
			    token = strtok_r(NULL, ",", &lasts)) {
				if (chkauthattr(token, user) == 0) {
					(void) snprintf(err_mesg,
					    sizeof (err_mesg),
					    "%s %s to %s, requires %s"
					    " authorization.", NO_ATTR_MSG,
					    "auth", token, token);
					no_perm(PEX_NO_AUTH, err_mesg);
				}
			}
		}

	}
	return (1);
}

static void
add_attrs(const char *attrname, char **attrArray, int *attrcnt)
{
	int i;
	for (i = 0; i < *attrcnt; i++) {
		if (strcmp(attrname, attrArray[i]) == 0) {
			return; /* already in list */
		}
	}

	/* not in list, add it in */
	attrArray[*attrcnt] = strdup(attrname);
	*attrcnt += 1;
}

/*ARGSUSED*/
static int
get_attrs(const char *name, kva_t *kva, void *ctxt, void *pres)
{
	attrs_t *res = pres;
	char *val;

	if (pres != NULL && ctxt != NULL && res->attrvals != NULL) {
		val = kva_match(kva, (char *)ctxt);
		if (val != NULL) {
			char *vals;
			char *token, *lasts;

			vals = strdup(val);
			for (token = strtok_r(vals, ",", &lasts);
			    token != NULL;
			    token = strtok_r(NULL, ",", &lasts)) {
				add_attrs(token, res->attrvals, &res->attr_cnt);
			}

		}
	}
	return (0);
}


/*ARGSUSED*/
static int
find_priv_attrs(const char *name, kva_t *kva, void *ctxt, void *pres)
{
	priv_attr_t *attrs = pres;
	char *val;

	if (attrs != NULL && ctxt != NULL) {
		val = kva_match(kva, (char *)ctxt);
		if (val != NULL) {
			attrs->privs = strdup(val);
		} else {
			attrs->privs = NULL;
		}
	}
	return (attrs->privs != NULL);
}

static void
free_attrs(attrs_t *attrs)
{
	int i;

	for (i = 0; i < attrs->attr_cnt; i++)
		free(attrs->attrvals[i]);
}

int
check_roles_profiles(char *roles, char *user, char *assign_auth,
    char *delegate_auth, char *type)
{

	if (roles && strlen(roles)) {
		char *token, *attr_vals, *lasts;
		attrs_t my_attrs;
		int i, found;

		if (chkauthattr(assign_auth, user) == 0) {
			if (chkauthattr(delegate_auth, user) == 0) {
				(void) snprintf(err_mesg, sizeof (err_mesg),
				    "%s %s, requires %s authorization.",
				    NO_ATTR_MSG, type, delegate_auth);
				no_perm(PEX_NO_AUTH, err_mesg);

			}
			my_attrs.attr_cnt = 0;

			(void) _enum_attrs(user, get_attrs, type, &my_attrs);

			attr_vals = (char *)strdup(roles);

			for (token = strtok_r(attr_vals, ",", &lasts);
			    token != NULL;
			    token = strtok_r(NULL, ",", &lasts)) {
				if (strcmp(token, DEFROLEPROF) == 0) continue;
				/* check to see if real user has this role. */
				found = 0;
				for (i = 0; i < my_attrs.attr_cnt; i++) {
					if (strcmp(token, my_attrs.attrvals[i])
					    == 0)
						found = 1;
				}
				if (!found) {
					int rc = PEX_NO_ROLE;
					char *txt = "role";

					if (strcmp(type, USERATTR_ROLES_KW)) {
						txt = "profile";
						rc = PEX_NO_PROFILE;
					}
					(void) snprintf(err_mesg,
					    sizeof (err_mesg),
					    "%s %s to %s, requires %s %s.",
					    NO_ATTR_MSG, type, token,
					    token, txt);

					free_attrs(&my_attrs);

					no_perm(rc, err_mesg);
				}
			}
			free_attrs(&my_attrs);
		}

	}
	return (1);
}

int
check_roles_auths(char *roles, char *user)
{
	return (check_roles_profiles(roles, user, ROLE_ASSIGN_AUTH,
	    ROLE_DELEGATE_AUTH, USERATTR_ROLES_KW));
}

int
check_profiles_auths(char *profiles, char *user)
{
	if (strcmp(profiles, DEFROLEPROF) == 0) {
		return (1);
	} else {
		return (check_roles_profiles(profiles, user,
		    PROFILE_ASSIGN_AUTH, PROFILE_DELEGATE_AUTH,
		    USERATTR_PROFILES_KW));
	}
}

projid_t get_projid_for(char *project) {
	sec_repository_t *rep = NULL;
	projid_t projid;
	int rc;
	if (create_repository_handle(scope_type, &rep)
	    != SEC_REP_SUCCESS) {
		bad_usage("Cannot get handle to scope.");
	}

	if (rep != NULL) {
		struct project *pptr;
		nss_XbyY_buf_t *buf = NULL;

		init_nss_buffer(SEC_REP_DB_PROJECT, &buf);
		if (isdigit(*project) == 0) {
			rc = rep->rops->get_projnam(project, &pptr, buf);
		} else {
			char *ptr;

			projid = (projid_t)strtol(project, &ptr, (int)10);
			if (*ptr == NULL) {
				rc = rep->rops->get_projid(projid, &pptr, buf);
			} else {
				(void) snprintf(err_mesg, sizeof (err_mesg),
				    gettext("Cannot get project %s from %s.\n"),
				    project, scope_type);
				(void) fprintf(stderr, err_mesg);
				syslog(LOG_ERR, err_mesg);
				exit(PEX_NO_PROJECT);
			}

		}
		if (rc != SEC_REP_SUCCESS) {
			(void) snprintf(err_mesg, sizeof (err_mesg),
			    gettext("Cannot get project %s from %s.\n"),
			    project, scope_type);
			(void) fprintf(stderr, err_mesg);
			syslog(LOG_ERR, err_mesg);
			exit(PEX_NO_PROJECT);
		}
		projid = pptr->pj_projid;
		free(pptr);
		free_nss_buffer(&buf);
	}
	return (projid);
}
/*ARGSUSED*/
int
check_project_auths(char *project, char *user)
{
	projid_t projid;
	if (project != NULL && user != NULL) {
		if (chkauthattr(PROJECT_ASSIGN_AUTH, user) == 0) {
			/* don't have solaris.project.assign check delegate */
			if (chkauthattr(PROJECT_DELEGATE_AUTH, user) == 0) {
				(void) snprintf(err_mesg, sizeof (err_mesg),
				    "%s %s, requires %s authorization.",
				    NO_ATTR_MSG, "primary project",
				    PROJECT_DELEGATE_AUTH);
				no_perm(PEX_NO_AUTH, err_mesg);
			}

			if ((projid = get_projid_for(project)) != getprojid()) {
				(void) snprintf(err_mesg, sizeof (err_mesg),
				    "%s %s to %d, not a member of project %d",
				    NO_ATTR_MSG, "primary project",
				    projid, projid);
				no_perm(PEX_NO_PROJECT, err_mesg);
			}
		}

	}
	return (1);
}

static void
free_labels(m_range_t *r, m_label_t *l)
{
	m_label_free(r->lower_bound);
	m_label_free(r->upper_bound);
	free(r);
	m_label_free(l);
}

int
check_label_auth(char *labelstr, char *user, char *authname, char *type)
{
	int rc = 0;
	if (is_system_labeled() == 0) {
		(void) snprintf(err_mesg, sizeof (err_mesg),
		    "%s %s: Trusted Extensions not enabled.",
		    NO_ATTR_MSG, type);
		no_perm(PEX_NO_SYSLABEL, err_mesg);
	}
	if (labelstr && strlen(labelstr)) {
		int err;
		m_label_t *user_label = NULL;
		m_range_t *range;

		if (chkauthattr(authname, user) == 0) {
			(void) snprintf(err_mesg, sizeof (err_mesg),
			    "%s %s, requires %s authorization.",
			    NO_ATTR_MSG, type, authname);
			no_perm(PEX_NO_AUTH, err_mesg);
		}

		err = str_to_label(labelstr, &user_label, MAC_LABEL,
		    L_NO_CORRECTION, NULL);

		if (err == -1) {
			(void) snprintf(err_mesg, sizeof (err_mesg),
			    "%s %s:Invalid label %s", NO_ATTR_MSG,
			    type, labelstr);
			no_perm(PEX_NO_LABEL, err_mesg);
		}

		if ((range = getuserrange(user)) == NULL) {
			(void) snprintf(err_mesg, sizeof (err_mesg),
			    "%s %s:getuserrange failure.", NO_ATTR_MSG,
			    type);
			no_perm(PEX_NO_LABEL, err_mesg);
		}

		if (blinrange(user_label, range) == 0) {
			(void) snprintf(err_mesg, sizeof (err_mesg),
			    "%s %s to %s, label not in user's range.",
			    NO_ATTR_MSG, type, labelstr);
			no_perm(PEX_NO_LABEL, err_mesg);
		}
		free_labels(range, user_label);

		rc = 1;
	}
	return (rc);
}

int
check_clearance(char *labelstr, char *user)
{
	return (check_label_auth(labelstr, user,
	    LABEL_DELEGATE_AUTH, USERATTR_CLEARANCE));
}

int
check_minlabel(char *labelstr, char *user)
{
	return check_label_auth(labelstr, user,
	    LABEL_DELEGATE_AUTH, USERATTR_MINLABEL);
}

static int
check_priv_auths(char *privs, char *user, char *priv_type)
{
	if (privs && strlen(privs)) {
		priv_set_t *aset;
		const char *res;
		priv_attr_t priv_attr;
		char *priv_str;
		priv_set_t *uset;

		if (chkauthattr(PRIVILEGE_ASSIGN_AUTH, user) == 0) {
			if (chkauthattr(PRIVILEGE_DELEGATE_AUTH,
			    user) == 0) {
				(void) snprintf(err_mesg, sizeof (err_mesg),
				    "%s %s, requires %s authorization",
				    NO_ATTR_MSG, priv_type,
				    PRIVILEGE_DELEGATE_AUTH);
				no_perm(PEX_NO_AUTH, err_mesg);

			}
			/* convert str privs to priv_set. */
			aset = priv_str_to_set(privs, ",", &res);

			if (aset != NULL) {
				res = NULL;
			} else if (res == NULL) {
				res = strerror(errno);
				(void) snprintf(err_mesg, sizeof (err_mesg),
				    "%s %s to %s:Invalid privilege set:%s",
				    NO_ATTR_MSG, priv_type, privs, res);
				no_perm(PEX_NO_AUTH, err_mesg);
			}

			/* get real user priv_set */
			(void) _enum_attrs(user, &find_priv_attrs, priv_type,
			    &priv_attr);

			if (priv_attr.privs != NULL)
				priv_str = priv_attr.privs;
			else
				priv_str = "basic";

			uset = priv_str_to_set(priv_str, ",", &res);
			if (priv_attr.privs != NULL)
				free(priv_attr.privs);

			if (uset != NULL) {
				priv_inverse(uset);
				priv_intersect(aset, uset);
			} else if (res == NULL) {
				res = strerror(errno);
				(void) snprintf(err_mesg, sizeof (err_mesg),
				    "Cannot set %s for %s:cannot delegate %s.",
				    priv_type, user, privs);
				priv_freeset(aset);
				no_perm(PEX_NO_PRIV, err_mesg);
			}

			if (priv_isemptyset(uset) == B_FALSE) {
				priv_str = priv_set_to_str(uset, ',',
				    PRIV_STR_SHORT);

				(void) snprintf(err_mesg, sizeof (err_mesg),
				    "%s %s to %s, requires %s "
				    "privilege.", NO_ATTR_MSG,
				    priv_type, privs, priv_str);
				priv_freeset(aset);
				priv_freeset(uset);
				free(priv_str);
				no_perm(PEX_NO_PRIV, err_mesg);
			} else {
				priv_freeset(aset);
				priv_freeset(uset);
			}
		}

	}
	return (1);
}

int
check_defaultpriv_auths(char *privs, char *user)
{
	return (check_priv_auths(privs, user, USERATTR_DFLTPRIV_KW));
}

int
check_limitpriv_auths(char *privs, char *user)
{
	return (check_priv_auths(privs, user, USERATTR_LIMPRIV_KW));
}

int
check_type_auths(char *type, char *user)
{

	if ((M_MASK & optn_mask) && type_auth_check) {
		if (chkauthattr(ROLE_ASSIGN_AUTH, user)) {
			return (1);
		} else {
			(void) snprintf(err_mesg, sizeof (err_mesg),
			    "%s type to %s, requires %s authorization.",
			    NO_ATTR_MSG, type, ROLE_ASSIGN_AUTH);
			no_perm(PEX_NO_AUTH, err_mesg);
		}
	}
	return (1);
}

int
check_roleauth(char *type, char *user)
{

	if (strcmp(type, USERATTR_ROLE_AUTH_USER) == 0)  {
		if (chkauthattr(ACCOUNT_SETPOLICY_AUTH, user)) {
			return (1);
		} else {
			(void) snprintf(err_mesg, sizeof (err_mesg),
			    "%s roleauth to %s, requires %s authorization.",
			    NO_ATTR_MSG, type, ACCOUNT_SETPOLICY_AUTH);
			no_perm(PEX_NO_AUTH, err_mesg);
		}
	}
	return (1);
}

int
check_auths_for_userattr(userattr_t *ua, char *real_user)
{
	int i, j;
	kv_t *kv_pair;

	/* Not checking user defaults for user_attr. But may need to. */
	if (ua == NULL || ua->attr == NULL || ua->attr->data == NULL ||
	    real_user == NULL)
		return (0);

	kv_pair = ua->attr->data;
	for (i = 0; i < ua->attr->length; i++) {
		for (j = 0; j < ua_keys; j++) {
			/* find in ua_opts table and do appropriate checks. */
			if (strcmp(kv_pair[i].key, ua_opts[j].key) == 0 &&
			    ua_opts[j].newvalue != NULL) {
				if (ua_opts[j].check_auth != NULL) {
					if (ua_opts[j].check_auth(
					    kv_pair[i].value, real_user) == 0)
						return (0);
				} else if (j < ua_keys &&
				    ua_opts[j].auth_name != NULL) {
					if (chkauthattr(
					    ua_opts[j].auth_name,
					    real_user) == 0) {
						(void) snprintf(err_mesg,
						    sizeof (err_mesg),
						    "%s %s, requires %s"
						    " authorization.",
						    NO_ATTR_MSG,
						    ua_opts[j].key,
						    ua_opts[j].auth_name);
						no_perm(PEX_NO_AUTH,
						    err_mesg);

					}
				}
			}
		}

	}
	return (1);
}
/* check authorizations */

/*
 * In add mode, setting user defaults for passwd, shadow
 * fields does not require any authorization.
 */
int
check_auths(struct passwd *pwd,
	    struct spwd *shadow,
	    userattr_t *userattr)
{
	struct passwd *real_user;
	char *auth;

	real_user = getpwuid(getuid());

	if (real_user == NULL)
		return (1);

	/* for adding  and deleting a user */
	if (optn_mask & (A_MASK | D_MASK)) {
		if (is_role) auth = ROLE_MANAGE_AUTH;
		else auth = USER_MANAGE_AUTH;
		if (chkauthattr(auth, real_user->pw_name) == 0) {
			char *type;

			if (is_role) type = "role";
			else type = "user";

			(void) snprintf(err_mesg, sizeof (err_mesg),
			    "Cannot add or delete %s, requires"
			    " %s authorization.",
			    type, auth);
			no_perm(PEX_NO_AUTH, err_mesg);
		}
		if (optn_mask & D_MASK)
			return (1);
	}

	if (check_auths_for_passwd(pwd->pw_gid,
	    real_user->pw_name) == 0)
		return (0);

	if (check_auths_for_shadow(shadow,
	    real_user->pw_name) == 0)
		return (0);

	if (check_auths_for_userattr(userattr,
	    real_user->pw_name) == 0)
		return (0);

	return (1);
}

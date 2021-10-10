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

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <auth_attr.h>
#include <prof_attr.h>
#include <user_attr.h>
#include <project.h>
#include <secdb.h>
#include <pwd.h>
#include <unistd.h>
#include <priv.h>
#include <errno.h>
#include <ctype.h>
#include <nss.h>
#include <bsm/libbsm.h>
#include <tsol/label.h>
#include "funcs.h"
#include "messages.h"
#undef GROUP
#include "userdefs.h"
#include <auth_list.h>
#include <users.h>


typedef struct ua_key {
	const char *key;
	const char *(*check)(const char *);
	const char *(*check_repository)(const char *, sec_repository_t *);
	const char *errstr;
	char *newvalue;
	/*
	 * added keyword_type to validate keywords for users and roles.
	 * NULL means keyword is valid for user/role.
	 */
	char *keyword_type;
	char opchar;	/* add('+'), remove('-'), no-op(' ') */
} ua_key_t;

/* add/remove operations on the following keys supported */
struct op_key_value {
	char *key;
	char *value;	/* value after add/remove */
};

static const char role[] = "role name";
static const char prof[] = "profile name";
static const char proj[] = "project name";
static const char priv[] = "privilege set";
static const char auth[] = "authorization";
static const char type[] = "user type";
static const char lock[] = "lock_after_retries value";
static const char label[] = "label";
static const char idlecmd[] = "idlecmd value";
static const char idletime[] = "idletime value";
static const char auditflags[] = "audit mask";
static char	  auditerr[256];
static const char roleauth[] = "role authentication type";


static const char *check_auth(const char *, sec_repository_t *);
static const char *check_prof(const char *, sec_repository_t *);
static const char *check_role(const char *, sec_repository_t *);
static const char *check_proj(const char *, sec_repository_t *);
static const char *check_privset(const char *);
static const char *check_type(const char *);
static const char *check_lock_after_retries(const char *);
static const char *check_label(const char *);
static const char *check_idlecmd(const char *);
static const char *check_idletime(const char *);
static const char *check_auditflags(const char *);
static const char *check_roleauth(const char *);

int nkeys;

/* keyword is valid for user */
#define	KEYWORD_TYPE_USER	USERATTR_TYPE_NORMAL_KW
/* keyword is valid for role */
#define	KEYWORD_TYPE_ROLE	USERATTR_TYPE_NONADMIN_KW

static ua_key_t keys[] = {
	/* First entry is always set correctly in main() */
	{ USERATTR_TYPE_KW, check_type, NULL, type, NULL, NULL,
	    OP_REPLACE_CHAR},
	{ USERATTR_AUTHS_KW, NULL, check_auth, auth, NULL, NULL,
	    OP_REPLACE_CHAR},
	{ USERATTR_PROFILES_KW, NULL, check_prof, prof, NULL, NULL,
	    OP_REPLACE_CHAR},
	{ USERATTR_ROLES_KW, NULL, check_role, role, NULL, KEYWORD_TYPE_USER,
	    OP_REPLACE_CHAR},
	{ USERATTR_DEFAULTPROJ_KW, NULL, check_proj, proj, NULL, NULL,
	    OP_REPLACE_CHAR},
	{ USERATTR_LIMPRIV_KW, check_privset, NULL, priv, NULL, NULL,
	    OP_REPLACE_CHAR},
	{ USERATTR_DFLTPRIV_KW, check_privset, NULL, priv, NULL, NULL,
	    OP_REPLACE_CHAR},
	{ USERATTR_LOCK_AFTER_RETRIES_KW, check_lock_after_retries, NULL,
	    lock, NULL, NULL, OP_REPLACE_CHAR},
	{ USERATTR_CLEARANCE, check_label, NULL, label, NULL, NULL,
	    OP_REPLACE_CHAR},
	{ USERATTR_MINLABEL, check_label, NULL, label, NULL, NULL,
	    OP_REPLACE_CHAR},
	{ USERATTR_IDLECMD_KW, check_idlecmd, NULL, idlecmd, NULL, NULL,
	    OP_REPLACE_CHAR},
	{ USERATTR_IDLETIME_KW, check_idletime, NULL, idletime, NULL, NULL,
	    OP_REPLACE_CHAR},
	{ USERATTR_AUDIT_FLAGS_KW, check_auditflags, NULL, auditflags, NULL,
	    NULL, OP_REPLACE_CHAR},
	{ USERATTR_ROLE_AUTH_KW, check_roleauth, NULL, roleauth, NULL,
	    KEYWORD_TYPE_ROLE, OP_REPLACE_CHAR },
};

#define	NKEYS	(sizeof (keys)/sizeof (ua_key_t))

static struct op_key_value opkeyval[] = {
	{USERATTR_AUTHS_KW, NULL},
	{USERATTR_DFLTPRIV_KW, NULL},
	{USERATTR_LIMPRIV_KW, NULL},
	{USERATTR_PROFILES_KW, NULL},
	{USERATTR_ROLES_KW, NULL}
};

#define	NOPKEYS	(sizeof (opkeyval)/sizeof (struct op_key_value))
static int find_opkey(const char *);

/*
 * Check for leading or trailing spaces.
 */
int
check_spaces(char *string)
{
	char *ptr;
	if (string != NULL) {
		/* check leading spaces */
		ptr = string;
		if (*ptr == ' ')
			return (1);
		if (ptr[strlen(string) -1] == ' ')
			return (1);
		return (0);
	}
	return (0);
}

/*
 * Change a key, there are three different call sequences:
 *
 *		key, value	- key with option letter, value.
 *		NULL, value	- -K key=value option.
 */

void
change_key(const char *key, char *value, char *user_type)
{
	int i;
	const char *res;
	int len;
	char opchar = OP_REPLACE_CHAR;
	const char *orig_key = key;
	char *tmpkey;

	if (key == NULL) {
		if (value == NULL) {
			errmsg(M_INVALID_VALUE);
			exit(EX_BADARG);
		}
		tmpkey = value;
		value = strchr(value, '=');
		/* Bad value */
		if (value == NULL) {
			errmsg(M_INVALID_VALUE);
			exit(EX_BADARG);
		}
		*value++ = '\0';

		/* Determine if +/- operation is specified */
		len = strlen(tmpkey);
		opchar = tmpkey[len - 1];
		if (len > 0 &&
		    (opchar == OP_ADD_CHAR || opchar == OP_SUBTRACT_CHAR)) {
			tmpkey[len - 1] = '\0';
			if (find_opkey(tmpkey) == -1) {
				tmpkey[len - 1] = opchar; /* undo the change */
				opchar = OP_REPLACE_CHAR;
			}
		} else {
			opchar = OP_REPLACE_CHAR;
		}
		key = tmpkey;
	}

	for (i = 0; i < NKEYS; i++) {
		if (strcmp(key, keys[i].key) == 0) {
			if (keys[i].newvalue != NULL) {
				/* Can't set a value twice */
				errmsg(M_REDEFINED_KEY, key);
				exit(EX_BADARG);
			}
			if (keys[i].keyword_type != NULL && user_type != NULL) {
				if (strcmp(user_type,
				    keys[i].keyword_type) != 0) {
					errmsg(M_INVALID_KEY, key);
					exit(EX_BADARG);
				}
			}

			/* Determine if +/- operation is needed */
			if (orig_key != NULL) {
				if (find_opkey(keys[i].key) != -1 &&
				    (value[0] == OP_ADD_CHAR ||
				    value[0] == OP_SUBTRACT_CHAR)) {
					opchar = value[0];
					value++; /* skip the +/- */
				} else {
					opchar = OP_REPLACE_CHAR;
				}
			}

			if (keys[i].check != NULL &&
			    (res = keys[i].check(value)) != NULL) {
				errmsg(M_INVALID, res, keys[i].errstr);
				exit(EX_BADARG);
			}


			keys[i].newvalue = value;
			keys[i].opchar = opchar;
			nkeys++;
			return;
		}
	}
	errmsg(M_INVALID_KEY, key);
	exit(EX_BADARG);
}

void process_change_key(sec_repository_t *rep) {
	int i;
	const char *res;

	for (i = 0; i < NKEYS; i++) {
		if (keys[i].newvalue != NULL &&
		    keys[i].check_repository != NULL &&
		    (res =
		    keys[i].check_repository(keys[i].newvalue, rep)) != NULL) {

			errmsg(M_INVALID, res, keys[i].errstr);
			exit(EX_BADARG);
		}
	}
}
/*
 * Add the keys to the argument vector.
 */
void
addkey_args(char **argv, int *index)
{
	int i;

	for (i = 0; i < NKEYS; i++) {
		const char *key = keys[i].key;
		char *val = keys[i].newvalue;
		size_t len;
		char *arg;

		/*
		 * don't need to pass -K options if they
		 * don't have values.
		 */
		if (val == NULL)
			continue;

		len = strlen(key) + strlen(val) + 2;
		arg = malloc(len);

		(void) snprintf(arg, len, "%s=%s", key, val);
		argv[(*index)++] = "-K";
		argv[(*index)++] = arg;
	}
}

/*
 * Propose a default value for a key and get the actual value back.
 * If the proposed default value is NULL, return the actual value set.
 * The key argument is the user_attr key.
 */
char *
getsetdefval(const char *key, char *dflt)
{
	int i;

	for (i = 0; i < NKEYS; i++)
		if (strcmp(keys[i].key, key) == 0)
			if (keys[i].newvalue != NULL)
				return (keys[i].newvalue);
			else
				return (keys[i].newvalue = dflt);
	return (NULL);
}

char *
getusertype(char *cmdname)
{
	static char usertype[MAX_TYPE_LENGTH];
	char *cmd;

	if (cmd = strrchr(cmdname, '/'))
		++cmd;
	else
		cmd = cmdname;

	/* get user type based on the program name */
	if (strncmp(cmd, CMD_PREFIX_USER, strlen(CMD_PREFIX_USER)) == 0)
		strcpy(usertype, USERATTR_TYPE_NORMAL_KW);
	else
		strcpy(usertype, USERATTR_TYPE_NONADMIN_KW);

	return (usertype);
}

int
is_role(char *usertype)
{
	if (strcmp(usertype, USERATTR_TYPE_NONADMIN_KW) == 0)
		return (1);
	/* not a role */
	return (0);
}

/*
 * Verifies the provided list of authorizations are all valid.
 *
 * Returns NULL if all authorization names are valid.
 * Otherwise, returns the invalid authorization name
 *
 */
static const char *
check_auth(const char *auths, sec_repository_t *rep)
{
	char *authname;
	authattr_t *result = NULL;
	char *tmp;
	nss_XbyY_buf_t *b = NULL;

	if (rep == NULL)
		return (NULL);
	tmp = strdup(auths);
	if (tmp == NULL) {
		errmsg(M_MEM_ALLOCATE);
		exit(EX_FAILURE);
	}

	authname = strtok(tmp, AUTH_SEP);
	if (check_spaces(authname))
		return (authname);

	init_nss_buffer(SEC_REP_DB_AUTHATTR, &b);
	while (authname != NULL) {
		char *suffix;
		char *auth;

		/* Remove named object after slash */
		if ((suffix = index(authname, KV_OBJECTCHAR)) != NULL)
			*suffix = '\0';

		/* Find the suffix */
		if ((suffix = rindex(authname, '.')) == NULL)
			return (authname);

		/* Check for existence in auth_attr */

		suffix++;
		if (strcmp(suffix, KV_WILDCARD) == 0) {
			auth = strdup(authname);
			auth[strlen(auth) - 1] = '\0';
		} else {
			auth = authname;
		}

		rep->rops->get_authnam(auth, &result, b);

		if (result == NULL) {
			/* cant find the auth */
			errmsg(M_AUTH_NOT_FOUND, auth);
		} else {
			free_authattr(result);
		}

		authname = strtok(NULL, AUTH_SEP);
		if (check_spaces(authname))
			return (authname);
	}
	free_nss_buffer(&b);
	free(tmp);
	return (NULL);
}

/*
 * Verifies the provided list of profile names are valid.
 *
 * Returns NULL if all profile names are valid.
 * Otherwise, returns the invalid profile name
 *
 */
static const char *
check_prof(const char *profs, sec_repository_t *rep)
{
	char *profname;
	profattr_t *result = NULL;
	char *tmp;
	nss_XbyY_buf_t *b = NULL;

	if (rep == NULL)
		return (NULL);
	tmp = strdup(profs);
	if (tmp == NULL) {
		errmsg(M_MEM_ALLOCATE);
		exit(EX_FAILURE);
	}
	init_nss_buffer(SEC_REP_DB_PROFATTR, &b);
	profname = strtok(tmp, PROF_SEP);
	if (check_spaces(profname))
		return (profname);
	while (profname != NULL) {
		rep->rops->get_profnam(profname, &result, b);
		if (result == NULL) {
			/* can't find the profile */
			return (profname);
		}
		free_profattr(result);
		profname = strtok(NULL, PROF_SEP);
		if (check_spaces(profname))
			return (profname);
		result = NULL;
	}
	free_nss_buffer(&b);
	return (NULL);
}

/*
 * Verifies the provided list of role names are valid.
 *
 * Returns NULL if all role names are valid.
 * Otherwise, returns the invalid role name
 *
 */
static const char *
check_role(const char *roles, sec_repository_t *rep)
{
	char *rolename;
	userattr_t *result = NULL;
	char *utype;
	char *tmp;
	nss_XbyY_buf_t *b = NULL;

	if (rep == NULL)
		return (NULL);
	tmp = strdup(roles);
	if (tmp == NULL) {
		errmsg(M_MEM_ALLOCATE);
		exit(EX_FAILURE);
	}
	init_nss_buffer(SEC_REP_DB_USERATTR, &b);
	rolename = strtok(tmp, ROLE_SEP);
	if (check_spaces(rolename))
		return (rolename);
	while (rolename != NULL) {
		rep->rops->get_usernam(rolename, &result, b);
		if (result == NULL) {
			/* can't find the rolename */
			return (rolename);
		}
		/* Now, make sure it is a role */
		utype = kva_match(result->attr, USERATTR_TYPE_KW);
		if (utype == NULL) {
			/* no user type defined. not a role */
			free_userattr(result);
			return (rolename);
		}
		if (strcmp(utype, USERATTR_TYPE_NONADMIN_KW) != 0) {
			free_userattr(result);
			return (rolename);
		}
		free_userattr(result);
		rolename = strtok(NULL, ROLE_SEP);
		if (check_spaces(rolename))
			return (rolename);
		result = NULL;
	}
	free_nss_buffer(&b);
	free(tmp);
	return (NULL);
}

static const char *
check_proj(const char *project, sec_repository_t *rep)
{
	struct project *proj = NULL;
	nss_XbyY_buf_t *b = NULL;
	int warning;
	int rc = 0;

	if (rep == NULL)
		return (NULL);
	init_nss_buffer(SEC_REP_DB_PROJECT, &b);
	rc = valid_project_check((char *)project, &proj, &warning, rep, b);
	free(proj);
	free_nss_buffer(&b);
	if (rc != NOTUNIQUE)
		return (project);
	else
		return (NULL);

}

static const char *
check_privset(const char *pset)
{
	priv_set_t *tmp;
	const char *res;

	tmp = priv_str_to_set(pset, ",", &res);

	if (tmp != NULL) {
		res = NULL;
		priv_freeset(tmp);
	} else if (res == NULL)
		res = strerror(errno);

	return (res);
}

static const char *
check_type(const char *type)
{
	if (strcmp(type, USERATTR_TYPE_NONADMIN_KW) != 0 &&
	    strcmp(type, USERATTR_TYPE_NORMAL_KW) != 0)
		return (type);

	return (NULL);
}

static const char *
check_roleauth(const char *auth)
{
	if ((strcmp(auth, USERATTR_ROLE_AUTH_USER) != 0) &&
	    (strcmp(auth, USERATTR_ROLE_AUTH_ROLE) != 0)) {
		return (auth);
	}

	return (NULL);
}

static const char *
check_lock_after_retries(const char *keyval)
{
	if (keyval != NULL) {
		if ((strcasecmp(keyval, "no") != 0) &&
		    (strcasecmp(keyval, "yes") != 0) &&
		    (*keyval != '\0')) {
			return (keyval);
		}
	}
	return (NULL);
}

static const char *
check_label(const char *labelstr)
{
	int err;
	m_label_t *lbl = NULL;

	if (!is_system_labeled())
		return (NULL);

	err = str_to_label(labelstr, &lbl, USER_CLEAR, L_NO_CORRECTION, NULL);
	m_label_free(lbl);

	if (err == -1)
		return (labelstr);

	return (NULL);
}

static const char *
check_idlecmd(const char *cmd)
{
	if ((strcmp(cmd, USERATTR_IDLECMD_LOCK_KW) != 0) &&
	    (strcmp(cmd, USERATTR_IDLECMD_LOGOUT_KW) != 0)) {
		return (cmd);
	}

	return (NULL);
}

static const char *
check_idletime(const char *time)
{
	int c;
	unsigned char *up = (unsigned char *) time;

	c = *up;
	while (c != '\0') {
		if (!isdigit(c))
			return (time);
		c = *++up;
	}

	return (NULL);
}

static const char *
check_auditflags(const char *auditflags)
{
	au_mask_t mask;
	char	*flags;
	char	*last = NULL;
	char	*err = "NULL";

	/* if deleting audit_flags */
	if (*auditflags == '\0') {
		return (NULL);
	}

	if ((flags = _strdup_null((char *)auditflags)) == NULL) {
		errmsg(M_MEM_ALLOCATE);
		exit(EX_FAILURE);
	}

	if (!__chkflags(_strtok_escape(flags, KV_AUDIT_DELIMIT, &last), &mask,
	    B_FALSE, &err)) {
		(void) snprintf(auditerr, sizeof (auditerr),
		    "always mask \"%s\"", err);
		free(flags);
		return (auditerr);
	}
	if (!__chkflags(_strtok_escape(NULL, KV_AUDIT_DELIMIT, &last), &mask,
	    B_FALSE, &err)) {
		(void) snprintf(auditerr, sizeof (auditerr),
		    "never mask \"%s\"", err);
		free(flags);
		return (auditerr);
	}
	if (last != NULL) {
		(void) snprintf(auditerr, sizeof (auditerr), "\"%s\"",
		    auditflags);
		free(flags);
		return (auditerr);
	}
	free(flags);

	return (NULL);
}

int
check_user_authorized()
{
	struct passwd *ruser;

	ruser = getpwuid(getuid());
	if (ruser == NULL) {
		return (-1);
	}
	if (chkauthattr(PASSWD_ASSIGN_AUTH, ruser->pw_name) == 0) {
		return (-1);
	}
	return (0);
}

/*
 * Does the key match with any of the keys that
 * support +/- operation?
 * returns index - if found
 *	   -1    - if not found
 */
static int
find_opkey(const char *key)
{
	int i;

	for (i = 0; i < NOPKEYS; i++) {
		if (strncmp(opkeyval[i].key, key,
		    strlen(opkeyval[i].key)) == 0) {
			return (i);
		}
	}

	return (-1);
}

/*
 * process add(+)/remove(-) operation
 */
int
process_add_remove(userattr_t *ua)
{
	char *curval = NULL;
	char *newval;
	int i;
	int j;

	for (i = 0; i < NKEYS; i++) {
		if (keys[i].opchar == OP_REPLACE_CHAR ||
		    (j = find_opkey(keys[i].key)) == -1) {
			continue; /* no operation needed */
		}

		/* Allocate storage for new string */
		if ((newval = (char *)malloc(NSS_BUFLEN_ATTRDB)) == NULL) {
			return (-1);
		} else {
			opkeyval[j].value = newval;
		}

		if (ua != NULL && ua->attr != NULL) {
			curval = kva_match(ua->attr, opkeyval[j].key);
		}

		if (keys[i].opchar == OP_ADD_CHAR) {
			newval = attr_add((keys[i].newvalue), curval,
			    newval, NSS_BUFLEN_ATTRDB, ",");
		} else {
			newval = attr_remove(curval, (keys[i].newvalue),
			    newval, NSS_BUFLEN_ATTRDB, ",");
		}

		if (newval == NULL) {
			errmsg(M_INVALID, keys[i].newvalue, "argument");
			return (-1);
		}

		keys[i].newvalue = newval;
	}

	return (0);
}

void
free_add_remove()
{
	int i;

	for (i = 0; i < NOPKEYS; i++) {
		if (opkeyval[i].value != NULL) {
			free(opkeyval[i].value);
			opkeyval[i].value = NULL;
		}
	}
}

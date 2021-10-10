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
#include <thread.h>
#include <ns_sldap.h>
#include <string.h>
#include <libintl.h>
#include <nsswitch.h>
#include <assert.h>
#include <sys/wait.h>
#include <libtsnet.h>
#include <zone.h>

#define	MAXATTRS	4096

thread_key_t ns_dbname = THR_ONCE_KEY;
static char mapname[ZONENAME_MAX + sizeof (AUTOHOME)] = "";

static int
get_rep(char *rep)
{
	if (rep) {
		if (strcmp(rep, NSS_REP_FILES) == 0)
			return (SEC_REP_FILES);
		else if (strcmp(rep, NSS_REP_LDAP) == 0)
			return (SEC_REP_LDAP);
		else
			return (SEC_REP_NOREP);
	} else
		return (SEC_REP_NSS);
}

static void
ns_key_cleanup(void *key)
{
	struct tsd_data *t;
	if (key) {
		t = (struct tsd_data *)key;
		free(t);
	}
}

static int
nss_init_tsd()
{
	struct tsd_data *tsd;
	int rc = 0;

	if ((int)ns_dbname == -1 &&
	    thr_keycreate_once(&ns_dbname, ns_key_cleanup) == 0) {

		tsd = (void*)calloc(1, sizeof (struct tsd_data));
		if (tsd == NULL)
			rc = -1;
		else
			rc = thr_setspecific(ns_dbname, (void*)tsd);
	}
	return (rc); /* cannot crate key */
}

int
nss_set_tsd(int dbname, char *backend)
{
	int rc;

	void *tsd;

	rc = thr_getspecific(ns_dbname, &tsd);
	if (rc == 0 && tsd != NULL) {
		((struct tsd_data *)tsd)->dbname = dbname;
		((struct tsd_data *)tsd)->source = backend;
		rc = thr_setspecific(ns_dbname, tsd);

		(void) thr_getspecific(ns_dbname, &tsd);
		return (rc);
	}
	return (-1);
}

int
execute_cmd_str(char *cmd, char *retval, int size)
{
	FILE *fptr;
	char *p;
	int status;
	uid_t ruid, euid;
	char retbuf[1024];
	boolean_t restore = B_FALSE;

	if (!cmd || (retval && !size) || (!retval && size))
		return (-1);
	/* save the real and effective uids. */
	if ((ruid = getuid()) != (euid = geteuid()) &&
	    (setuid(euid) == 0))
		restore = B_TRUE;
	fptr = popen(cmd, "r");
	/* restore the real and effective uids. */
	if (restore)
		(void) setreuid(ruid, euid);
	if (fptr == NULL)
		return (-1);
	if (size) {
		(void) fgets(retval, size, fptr);
	} else {
		while (fgets(retbuf, sizeof (retbuf), fptr) != NULL)
			(void) fprintf(stderr, "%s", retbuf);
	}
	status = pclose(fptr);
	if (WIFSIGNALED(status)) {
		(void) fprintf(stderr, gettext("Command %s exited rc=%d\n"),
		    cmd, status);
		return (-1);
	}
	if (retval) {
		if (strlen(retval) > 0) {
			p = strchr(retval, '\n');
			if (p != NULL)
				*p = '\0';
		}
	}
	assert(WIFEXITED(status));
	return (WEXITSTATUS(status));
}

int
get_repository_handle(char *rep_type, sec_repository_t **rep_handle)
{
	int rc = SEC_REP_SUCCESS;
	int scope;
	sec_repository_t *handle = NULL;

	scope = get_rep(rep_type);
	if (scope == SEC_REP_NOREP) {
		(void) fprintf(stderr, gettext("ERROR: Invalid repository"
		    " option - %s. Valid options are \"files\" or \"ldap\"."
		    "\n"), rep_type);
		rc = scope;
		*rep_handle = handle;
		return (rc);
	}
	handle = (sec_repository_t *)malloc(sizeof (sec_repository_t));
	if (handle == NULL) {
		(void) fprintf(stderr, gettext("ERROR: Unable to allocate"
		    " memory for repository handle.\n"), rep_type);
		return (SEC_REP_NOMEM);
	}
	switch (scope) {
	case SEC_REP_FILES:
		handle->type = SEC_REP_FILES;
		handle->rops = &sec_files_rops;
		break;
	case SEC_REP_LDAP:
	{
		char *cmd_str = "svcs -H -o state ldap/client";
		char status[32];

		handle->type = SEC_REP_LDAP;
		handle->rops = &sec_ldap_rops;

		/* check status of ldap/client service. */
		if (execute_cmd_str(cmd_str, status, sizeof (status)) == 0) {
			if (status[0] != '\0' &&
			    strcmp(status, "online") != 0) {
				(void) fprintf(stderr, gettext("\nERROR:ldap"
				    " client not configured. Unable to access"
				    " the ldap repository.\n"));
				return (SEC_REP_SYSTEM_ERROR);
			}
		} else {
			(void) fprintf(stderr, gettext("\nERROR:ldap"
			    " client not configured. Unable to access"
			    " the ldap repository.\n"));
			return (SEC_REP_SYSTEM_ERROR);
		}
	}
	break;
	case SEC_REP_NSS:
		handle->type = SEC_REP_NSS;
		handle->rops = &sec_nss_rops;
		break;
	}
	*rep_handle = handle;

	if (handle != NULL) {
		if ((rc = nss_init_tsd()) != 0) {
			(void) fprintf(stderr, gettext("ERROR: Cannot access %s"
			    " repository.\n"), rep_type);
		}
	} else {
		rc = -1;
		(void) fprintf(stderr, gettext("ERROR: Cannot access %s"
		    " repository.\n"), rep_type);
	}
	return (rc);
}

void
free_repository_handle(sec_repository_t *rep)
{
	if (rep)
		free(rep);
}

void
free_nss_buffer(nss_XbyY_buf_t **bufpp)
{
	NSS_XbyY_FREE(bufpp);
	*bufpp = NULL;
}

void
init_nss_buffer(int dbname, nss_XbyY_buf_t **bufpp)
{
	if (dbname) {
		switch (dbname) {
		case SEC_REP_DB_PASSWD:
			if (*bufpp == NULL) {
				*bufpp =
				    _nss_XbyY_buf_alloc(sizeof (struct passwd),
				    NSS_BUFLEN_PASSWD);
			}

			break;
		case SEC_REP_DB_SHADOW:
			if (*bufpp == NULL) {
				*bufpp =
				    _nss_XbyY_buf_alloc(sizeof (struct spwd),
				    NSS_BUFLEN_SHADOW);
			}
			break;
		case SEC_REP_DB_USERATTR:
			if (*bufpp == NULL) {
				*bufpp =
				    _nss_XbyY_buf_alloc(sizeof (userattr_t),
				    NSS_BUFLEN_USERATTR);
			}
			break;
		case SEC_REP_DB_GROUP:
			if (*bufpp == NULL) {
				*bufpp =
				    _nss_XbyY_buf_alloc(sizeof (struct group),
				    NSS_BUFLEN_GROUP);
			}
			break;
		case SEC_REP_DB_PROJECT:
			if (*bufpp == NULL) {
				*bufpp =
				    _nss_XbyY_buf_alloc(sizeof (struct project),
				    NSS_BUFLEN_PROJECT);
			}
			break;
		case SEC_REP_DB_PROFATTR:
			if (*bufpp == NULL) {
				*bufpp =
				    _nss_XbyY_buf_alloc(sizeof (profattr_t),
				    NSS_BUFLEN_PROFATTR);
			}
			break;
		case SEC_REP_DB_EXECATTR:
			if (*bufpp == NULL) {
				*bufpp =
				    _nss_XbyY_buf_alloc(sizeof (execattr_t),
				    NSS_BUFLEN_EXECATTR);
			}
			break;
		case SEC_REP_DB_AUTHATTR:
			if (*bufpp == NULL) {
				*bufpp =
				    _nss_XbyY_buf_alloc(sizeof (authattr_t),
				    NSS_BUFLEN_AUTHATTR);
			}
			break;
		case SEC_REP_DB_TNRHDB:
			if (*bufpp == NULL) {
				*bufpp =
				    _nss_XbyY_buf_alloc(sizeof (tsol_rhent_t),
				    NSS_BUFLEN_TSOL_RH);
			}
			break;
		case SEC_REP_DB_TNRHTP:
			if (*bufpp == NULL) {
				*bufpp =
				    _nss_XbyY_buf_alloc(sizeof (tsol_tpent_t),
				    NSS_BUFLEN_TSOL_TP);
			}
			break;
		}
	}
}

void
make_attr_string(kv_t *kvp, int len, char *buffer, int blen)
{
	char *key, *val;
	int i, j;

	if (blen) {
		(void) memset(buffer, 0, blen);

		for (i = j = 0; i < len; i++) {
			key = kvp[i].key;
			val = kvp[i].value;
			if ((key == NULL) || (val == NULL))
				continue;
			if (strlen(val) == 0 ||
			    (strcmp(key, USERATTR_TYPE_KW) == 0 &&
			    strcmp(val, USERATTR_TYPE_NORMAL_KW) == 0))
				continue;

			if (j > 0)
				(void) strlcat(buffer, KV_DELIMITER, blen);
			(void) strlcat(buffer, key, blen);
			(void) strlcat(buffer, "=", blen);
			(void) strlcat(buffer, val, blen);
			j++;
		}
	}
}

int check_profattr(char *name, sec_repository_t **rep, char *service_name) {
	nss_XbyY_buf_t *buf = NULL;
	void *ptr = NULL;
	init_nss_buffer(SEC_REP_DB_PROFATTR, &buf);
	if ((*rep)->rops->get_profnam(name, (profattr_t **)&ptr, buf) == 0 &&
	    ptr != NULL) {
		(void) fprintf(stderr,
		    gettext("Found profile in %s repository.\n"),
		    service_name);
		free(ptr);
		(void) free_nss_buffer(&buf);
		return (0);
	}
	(void) free_nss_buffer(&buf);
	return (-1);
}

int check_passwd(char *name, sec_repository_t **rep, char *service_name) {
	nss_XbyY_buf_t *buf = NULL;
	void *ptr = NULL;
	init_nss_buffer(SEC_REP_DB_PASSWD, &buf);
	if ((*rep)->rops->get_pwnam(name, (struct passwd **)&ptr, buf) == 0 &&
	    ptr != NULL) {
		(void) fprintf(stderr,
		    gettext("Found user in %s repository.\n"),
		    service_name);
		free(ptr);
		(void) free_nss_buffer(&buf);
		return (0);
	}
	(void) free_nss_buffer(&buf);
	return (-1);
}

int check_group(char *name, sec_repository_t **rep, char *service_name) {
	nss_XbyY_buf_t *buf = NULL;
	void *ptr = NULL;
	init_nss_buffer(SEC_REP_DB_GROUP, &buf);
	if ((*rep)->rops->get_grnam(name, (struct group **)&ptr, buf) == 0 &&
	    ptr != NULL) {
		(void) fprintf(stderr,
		    gettext("Found group in %s repository.\n"),
		    service_name);
		free(ptr);
		(void) free_nss_buffer(&buf);
		return (0);
	}
	(void) free_nss_buffer(&buf);
	return (-1);
}

int check_tnrhtp(char *name, sec_repository_t **rep, char *service_name) {
	nss_XbyY_buf_t *buf = NULL;
	void *ptr = NULL;
	init_nss_buffer(SEC_REP_DB_TNRHTP, &buf);
	if ((*rep)->rops->get_tnrhtp(name, (tsol_tpent_t **)&ptr, buf) == 0 &&
	    ptr != NULL) {
		(void) fprintf(stderr,
		    gettext("Found trusted networking template in %s"
		    " repository.\n"), service_name);
		free(ptr);
		(void) free_nss_buffer(&buf);
		return (0);
	}
	(void) free_nss_buffer(&buf);
	return (-1);
}

char *get_db(int dbname) {

	switch (dbname) {
		case SEC_REP_DB_PASSWD:
			return (NSS_DBNAM_PASSWD);
		case SEC_REP_DB_GROUP:
			return (NSS_DBNAM_GROUP);
		case SEC_REP_DB_PROFATTR:
			return (NSS_DBNAM_PROFATTR);
		case SEC_REP_DB_TNRHTP:
			return (NSS_DBNAM_TSOL_TP);
		default:
			return (NULL);
	}
}

int find_in_nss(int dbname, char *name, sec_repository_t **rep) {
	struct __nsw_switchconfig *conf = NULL;
	enum __nsw_parse_err pserr;
	conf = __nsw_getconfig(get_db(dbname), &pserr);
	if (conf == NULL || conf->lookups == NULL) {
		(void) fprintf(stdout, gettext("Cannot find config in"
		    " nsswitch.\n"));
		return (SEC_REP_SYSTEM_ERROR);
	} else {
		struct __nsw_lookup *lkup;
		int i;
		int status = -1;
		lkup = conf->lookups;
		for (i = 0; i < conf->num_lookups; i++) {
			char *sname = lkup->service_name;
			if (strcmp(sname, NSS_REP_FILES) == 0) {
				status = get_repository_handle(NSS_REP_FILES,
				    rep);
			} else if (strcmp(sname, NSS_REP_LDAP) == 0) {
				status = get_repository_handle(NSS_REP_LDAP,
				    rep);
			} else if (strcmp(sname, NSS_REP_COMPAT) == 0) {
				status = get_repository_handle(NSS_REP_FILES,
				    rep);
				sname = NSS_REP_FILES;
			}
			if (status == 0 && *rep != NULL) {
				if (dbname == SEC_REP_DB_PROFATTR) {
					status = check_profattr(name,
					    rep, sname);
				} else if (dbname == SEC_REP_DB_PASSWD) {
					status = check_passwd(name, rep, sname);
				} else if (dbname == SEC_REP_DB_GROUP) {
					status = check_group(name, rep, sname);
				} else if (dbname == SEC_REP_DB_TNRHTP) {
					status = check_tnrhtp(name, rep, sname);
				}
				if (status < 0) {
					(void) free_repository_handle(*rep);
					*rep = NULL;
				} else if (status == 0)
					break;
			}
			lkup = lkup->next;
		}
		if (*rep == NULL)
			return (SEC_REP_NOT_FOUND);
	}
	return (0);
}

/*
 * Search for member in the null terminated
 * list. Returns B_TRUE or B_FALSE
 */
boolean_t
list_contains(char **list, char *member)
{
	while (*list != NULL) {
		if (strcmp(*list, member) == 0) {
			return (B_TRUE);
		}
		list++;
	}

	return (B_FALSE);
}

/*
 * Add two strings of attributes separated by separator
 * old_attr - existing attributes
 * attrs - attributes to be added to old_attr
 * new_attr - new attributes after the operation
 * attrsize - size of new_attr
 *
 * Returns NULL -   error
 *	  new_str - successful
 */

char *
attr_add(char *old_attr, char *attrs, char *new_attr, int attrsize,
	char *separator)
{
	char *attr_list[MAXATTRS];
	char *cur_attr;
	int count = 0;
	int i;
	char *lasts;
	boolean_t first = B_TRUE;

	/* if old_attr is empty, just copy the attrs to new_attr */
	if (old_attr == NULL || *old_attr == '\0') {
		if (attrs == NULL) {
			*new_attr = '\0';
		} else {
			(void) strlcpy(new_attr, attrs, attrsize);
		}

		return (new_attr);
	}

	/* if attrs is empty, just copy the old_attr to new_attr */
	if (attrs == NULL || *attrs == '\0') {
		(void) strlcpy(new_attr, old_attr, attrsize);
		return (new_attr);
	}

	/* Split attrs to  a list */
	cur_attr = strtok_r(attrs, separator, &lasts);
	while (cur_attr != NULL) {
		attr_list[count++] = cur_attr;
		if (count == MAXATTRS) {
			/* list too big */
			return (NULL);
		}
		cur_attr = strtok_r(NULL, separator, &lasts);
	}
	attr_list[count] = NULL; /* null terminated list */

	/* Copy old_attr to new_attr skipping any duplicates */
	cur_attr = strtok_r(old_attr, separator, &lasts);
	*new_attr = '\0';
	while (cur_attr != NULL) {
		if (list_contains(attr_list, cur_attr)) {
			/* skip it */
			cur_attr = strtok_r(NULL, separator, &lasts);
			continue;
		}
		if (first) {
			first = B_FALSE;
		} else {
			(void) strlcat(new_attr, separator, attrsize);
		}
		(void) strlcat(new_attr, cur_attr, attrsize);
		cur_attr = strtok_r(NULL, separator, &lasts);
	}

	/* Append the remaining attrs */
	if (*new_attr == '\0') {
		first = B_TRUE;
	} else {
		first = B_FALSE;
	}
	for (i = 0; i < count; i++) {
		if (first) {
			first = B_FALSE;
		} else {
			(void) strlcat(new_attr, separator, attrsize);
		}
		(void) strlcat(new_attr, attr_list[i], attrsize);
	}

	return (new_attr);
}

/*
 * Remove attributes list attrs from the old_attr list
 * old_attr - existing attributes
 * attrs - attributes to be removed from old_attr
 * new_attr - new attributes after the operation
 * attrsize - size of new_attr
 *
 * Returns NULL -   error
 *	  new_str - successful
 */

char *
attr_remove(char *old_attr, char *attrs, char *new_attr,
	int attrsize, char *separator)
{
	char *attr_list[MAXATTRS];
	char *cur_attr;
	int count = 0;
	int deletecnt = 0; /* delete count */
	char *lasts;
	boolean_t first = B_TRUE;

	/* If old_attr is empty, nothing to remove */
	if (old_attr == NULL || *old_attr == '\0' || attrs == NULL) {
		return (NULL);
	}

	/* Split attrs to  a list */
	cur_attr = strtok_r(attrs, separator, &lasts);
	while (cur_attr != NULL) {
		attr_list[count++] = cur_attr;
		if (count == MAXATTRS) {
			/* list too big */
			return (NULL);
		}
		cur_attr = strtok_r(NULL, separator, &lasts);
	}

	/* Copy old_attr to new_attr after removing matching ones */
	cur_attr = strtok_r(old_attr, separator, &lasts);
	*new_attr = '\0';
	while (cur_attr != NULL) {
		if (list_contains(attr_list, cur_attr)) {
			/* remove it */
			cur_attr = strtok_r(NULL, separator, &lasts);
			deletecnt++;
			continue;
		}
		if (first) {
			first = B_FALSE;
		} else {
			(void) strlcat(new_attr, separator, attrsize);
		}
		(void) strlcat(new_attr, cur_attr, attrsize);
		cur_attr = strtok_r(NULL, separator, &lasts);
	}

	/* Not everything in attrs got deleted */
	if (deletecnt != count) {
		return (NULL);
	}

	return (new_attr);
}

char *
get_autohome_db()
{
	zoneid_t zoneid;
	char zonename[ZONENAME_MAX];

	if (mapname[0] == '\0') {
		if (is_system_labeled()) {
			zoneid = getzoneid();
			if (getzonenamebyid(zoneid, zonename,
			    sizeof (zonename)) < 0) {
				(void) fprintf(stderr,
				    gettext("Could not get zone name.\n"));
				exit(SEC_REP_SYSTEM_ERROR);
			}
			(void) snprintf(mapname, sizeof (mapname),
			    "%s_%s", AUTOHOME, zonename);
		} else {
			(void) snprintf(mapname, sizeof (mapname),
			    "%s", AUTOHOME);
		}
	}
	return (mapname);
}

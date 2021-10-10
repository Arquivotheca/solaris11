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

#include <strings.h>
#include <sys/stat.h>
#include <pthread.h>
#include <thread.h>
#include <syslog.h>
#include <userdefs.h>
#include <libintl.h>
#include <stdlib.h>
#include <nsswitch.h>
#include <tsol/label.h>
#include <arpa/inet.h>
#include <libtsnet.h>
#include <nssec.h>
#include "repops.h"
#include <grp.h>

#define	USERATTR_TEMP	"/etc/uatmp"
#define	GRPTMP	"/etc/grptmp"
#define	AUTHTMP	"/etc/security/authtmp"
#define	PROJTMP	"/etc/projtmp"
#define	AUTOHOMETMP	"/etc/auto_hometmp"
#define	EXECTMP	"/etc/security/exectmp"
#define	PROFTMP	"/etc/security/proftmp"
#define	TNRHDB_TMP	"/etc/security/tsol/tnrhdbtmp"
#define	TNRHTP_TMP	"/etc/security/tsol/tnrhtptmp"

static char *files_backend = NSS_REP_FILES;

static nss_sec_conv_t nss_dbinfo[] = {
	{ NULL, 0, 0, NULL, -1, -1},
	{NSS_DBNAM_PASSWD, sizeof (struct passwd), NSS_BUFLEN_PASSWD,
		str2passwd, NSS_DBOP_PASSWD_BYNAME, NSS_DBOP_PASSWD_BYUID},
	{NSS_DBNAM_SHADOW, sizeof (struct spwd), NSS_BUFLEN_SHADOW,
		str2spwd, NSS_DBOP_SHADOW_BYNAME, -1},
	{NSS_DBNAM_USERATTR, sizeof (userstr_t), NSS_BUFLEN_USERATTR,
		_str2userattr, NSS_DBOP_USERATTR_BYNAME, -1},
	{NSS_DBNAM_GROUP, sizeof (struct group), NSS_BUFLEN_GROUP,
		str2group, NSS_DBOP_GROUP_BYNAME, NSS_DBOP_GROUP_BYGID},
	{NSS_DBNAM_PROJECT, sizeof (struct project), NSS_BUFLEN_PROJECT,
		_str2project, NSS_DBOP_PROJECT_BYNAME, NSS_DBOP_PROJECT_BYID},
	{NSS_DBNAM_PROFATTR, sizeof (profstr_t), NSS_BUFLEN_PROFATTR,
		_str2profattr, NSS_DBOP_PROFATTR_BYNAME, -1},
	{NSS_DBNAM_EXECATTR, sizeof (execstr_t), NSS_BUFLEN_EXECATTR,
		_str2execattr, NSS_DBOP_EXECATTR_BYNAME,
		NSS_DBOP_EXECATTR_BYID},
	{NSS_DBNAM_AUTHATTR, sizeof (authstr_t), NSS_BUFLEN_AUTHATTR,
		_str2authattr, NSS_DBOP_AUTHATTR_BYNAME, -1},
	{NULL, 0, 0, NULL, -1, -1}, /* for AUTOMOUNT */
	{NSS_DBNAM_TSOL_RH, sizeof (tsol_rhstr_t), NSS_BUFLEN_TSOL_RH,
		str_to_rhstr, NSS_DBOP_TSOL_RH_BYADDR, -1},
	{NSS_DBNAM_TSOL_TP, sizeof (tsol_tpstr_t), NSS_BUFLEN_TSOL_TP,
		str_to_tpstr, NSS_DBOP_TSOL_TP_BYNAME, -1},
	{NULL, 0, 0, NULL, -1, -1},
};



static int files_get_pwnam(char *, struct passwd **, nss_XbyY_buf_t *);
static int files_get_pwid(uid_t, struct passwd **, nss_XbyY_buf_t *);
struct passwd *get_pwent(nss_XbyY_buf_t *);
static void files_set_pwent();
void end_ent();

static int files_get_spnam(char *, struct spwd **, nss_XbyY_buf_t *);
struct spwd *get_spent(nss_XbyY_buf_t *);
static void files_set_spent();

static int files_get_usernam(char *, userattr_t **, nss_XbyY_buf_t *);
static int files_get_useruid(uid_t, userattr_t **, nss_XbyY_buf_t *);
userattr_t *get_userattr(nss_XbyY_buf_t *);
static void files_set_userattr();

static int files_get_grnam(char *, struct group **, nss_XbyY_buf_t *);
static int files_get_grid(gid_t, struct group **, nss_XbyY_buf_t *);
struct group *get_group(nss_XbyY_buf_t *);
static void files_set_group();

static int files_get_projnam(char *, struct project **, nss_XbyY_buf_t *);
static int files_get_projid(projid_t, struct project **, nss_XbyY_buf_t *);
struct project *get_project(nss_XbyY_buf_t *);
static void files_set_project();

static int files_get_profnam(char *, profattr_t **, nss_XbyY_buf_t *);
profattr_t *get_profattr(nss_XbyY_buf_t *);
static void files_set_profattr();

static int files_get_authnam(char *, authattr_t **, nss_XbyY_buf_t *);
authattr_t *get_authattr(nss_XbyY_buf_t *);
static void files_set_authattr();

static int files_put_group(struct group *, char *, int);

static int files_put_project(struct project *, int);

static int files_put_profattr(profattr_t *, int);

static execattr_t *files_get_execprof(char *, char *, char *, int,
    nss_XbyY_buf_t *);
static execattr_t *files_get_execuser(char *, char *, char *, int,
    nss_XbyY_buf_t *);
static int files_put_execattr(execattr_t *, int);

execattr_t *get_execattr(nss_XbyY_buf_t *);
static void files_set_execattr();

static int files_put_authattr(authattr_t *, int);
static int files_edit_autohome(char *, char *, char *, int);
static int files_get_autohome(char *, char *);
static int files_put_tnrhdb(tsol_rhent_t *, int);
static int files_get_tnrhtp(const char *, tsol_tpent_t **, nss_XbyY_buf_t *);
static int files_put_tnrhtp(tsol_tpent_t *, int);
static void files_set_rhent(int);
void end_rhent(void);
tsol_rhent_t *get_rhent(nss_XbyY_buf_t *);

/*
 * files function pointers
 */

sec_repops_t sec_files_rops = {
	files_get_pwnam,
	files_get_pwid,
	get_pwent,
	files_set_pwent,
	end_ent,
	files_get_spnam,
	get_spent,
	files_set_spent,
	end_ent,
	files_get_usernam,
	files_get_useruid,
	get_userattr,
	files_set_userattr,
	end_ent,
	files_get_grnam,
	files_get_grid,
	files_put_group,
	get_group,
	files_set_group,
	end_ent,
	files_get_projnam,
	files_get_projid,
	files_put_project,
	get_project,
	files_set_project,
	end_ent,
	files_get_profnam,
	files_put_profattr,
	get_profattr,
	files_set_profattr,
	end_ent,
	files_get_execprof,
	files_get_execuser,
	files_put_execattr,
	get_execattr,
	files_set_execattr,
	end_ent,
	files_get_authnam,
	files_put_authattr,
	get_authattr,
	files_set_authattr,
	end_ent,
	files_edit_groups,
	files_edit_projects,
	files_edit_autohome,
	files_get_autohome,
	files_put_tnrhdb,
	files_set_rhent,
	get_rhent,
	end_rhent,
	files_get_tnrhtp,
	files_put_tnrhtp
};


static nss_db_root_t files_db_root = NSS_DB_ROOT_INIT;
static nss_getent_t files_context = NSS_GETENT_INIT;
static char *_nsw_search_path = NULL;
static int tsol_tp_stayopen =  0;
static int tsol_rh_stayopen =  0;

static void
nss_sec_initf_execattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_EXECATTR;
	p->config_name    = NSS_DBNAM_PROFATTR; /* use config for "prof_attr" */
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = files_backend;
}
static void
nsw_sec_initf_execattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_EXECATTR;
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = files_backend;
}

static void
nsw_sec_initf_profattr(nss_db_params_t *p)
{
	p->name = NSS_DBNAM_PROFATTR;
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = files_backend;
}

static void
nss_sec_init_tsd(nss_db_params_t *p)
{
	int rc;

	void *tsd;
	struct tsd_data *t;

	rc = thr_getspecific(ns_dbname, &tsd);
	if (rc == 0 && tsd != NULL) {
		t = (struct tsd_data *)tsd;
		p->name = (char *)nss_dbinfo[t->dbname].database;
		p->flags |= NSS_USE_DEFAULT_CONFIG;
		p->default_config = t->source;
	}
}

int
get_db_ent(int dbname, char *backend, int uid, char *name,
    void **result, nss_XbyY_buf_t *buf)
{
	nss_XbyY_args_t arg;
	void *value;
	int dbop;
	nss_db_root_t db_root = NSS_DB_ROOT_INIT;

	if (result == NULL)
		return (SEC_REP_INVALID_ARG);

	if (buf == NULL || buf->buffer == NULL ||
	    buf->buflen != nss_dbinfo[dbname].db_nss_buf_len)
		return (SEC_REP_NOMEM);

	if (nss_set_tsd(dbname, backend) == -1)
		return (SEC_REP_NOMEM);

	value = (void *)calloc(1, nss_dbinfo[dbname].db_struct_size);

	NSS_XbyY_INIT(&arg, value, buf->buffer, buf->buflen,
	    nss_dbinfo[dbname].conv_func);

	if (name) {
		arg.key.name = name;
		dbop = nss_dbinfo[dbname].db_op_bynam;
		if (dbname == SEC_REP_DB_TNRHTP &&
		    dbop == NSS_DBOP_TSOL_TP_BYNAME) {
			arg.stayopen = tsol_tp_stayopen;
			arg.h_errno = TSOL_NOT_FOUND;
		}

	} else if (uid) {
		arg.key.uid = uid;
		dbop = nss_dbinfo[dbname].db_op_byid;
	}

	(void) nss_search(&db_root, nss_sec_init_tsd, dbop, &arg);

	*result = (void *)NSS_XbyY_FINI(&arg);

	if (*result == NULL)
		return (SEC_REP_NOT_FOUND);

	return (SEC_REP_SUCCESS);

}

void
set_ent(int dbname, char *backend)
{
	if (nss_set_tsd(dbname, backend) != -1) {
		nss_setent(&files_db_root, nss_sec_init_tsd, &files_context);
	}
}

static void *
get_ent(nss_XbyY_buf_t *b)
{
	nss_XbyY_args_t arg;
	struct tsd_data *tsd;

	if ((thr_getspecific(ns_dbname, (void**) &tsd) == 0) &&
	    tsd != NULL) {
		NSS_XbyY_INIT(&arg, b->result, b->buffer, b->buflen,
		    nss_dbinfo[tsd->dbname].conv_func);
		(void) nss_getent(&files_db_root, nss_sec_init_tsd,
		    &files_context, &arg);

		return (void *)NSS_XbyY_FINI(&arg);
	}
	return (NULL);
}

void
end_ent()
{
	nss_endent(&files_db_root, nss_sec_init_tsd, &files_context);
	nss_delete(&files_db_root);
}

static int
files_get_pwnam(char *name, struct passwd ** result, nss_XbyY_buf_t *b)
{
	return get_db_ent(SEC_REP_DB_PASSWD, files_backend, -1, name,
	    (void**)result, b);
}


static int
files_get_pwid(uid_t uid, struct passwd ** result, nss_XbyY_buf_t *b)
{
	return get_db_ent(SEC_REP_DB_PASSWD, files_backend, uid, NULL,
	    (void**)result, b);
}


static void
files_set_pwent()
{
	set_ent(SEC_REP_DB_PASSWD, files_backend);
}

struct passwd *
get_pwent(nss_XbyY_buf_t *b)
{
	nss_XbyY_args_t arg;
	char *nam;

	do {
		NSS_XbyY_INIT(&arg, b->result, b->buffer,
		    b->buflen, str2passwd);
		/* No key to fill in */
		(void) nss_getent(&files_db_root, nss_sec_init_tsd,
		    &files_context, &arg);
	} while (arg.returnval != 0 &&
	    (nam = ((struct passwd *)arg.returnval)->pw_name) != 0 &&
	    (*nam == '+' || *nam == '-'));

	return ((struct passwd *)NSS_XbyY_FINI(&arg));

}

static int
files_get_spnam(char *name, struct spwd **result, nss_XbyY_buf_t *b)
{
	return get_db_ent(SEC_REP_DB_SHADOW, files_backend, -1,
	    name, (void**)result, b);
}

void
files_set_spent()
{
	set_ent(SEC_REP_DB_SHADOW, files_backend);
}

struct spwd *
get_spent(nss_XbyY_buf_t *b)
{
	nss_XbyY_args_t arg;

	char *nam;
	/*
	 * In getXXent_r(), protect the unsuspecting caller
	 * from +/- entries
	 */
	do {
		NSS_XbyY_INIT(&arg, b->result, b->buffer, b->buflen, str2spwd);
		/* No key to fill in */
		(void) nss_getent(&files_db_root, nss_sec_init_tsd,
		    &files_context, &arg);
	} while (arg.returnval != 0 &&
	    (nam = ((struct spwd *)arg.returnval)->sp_namp) != 0 &&
	    (*nam == '+' || *nam == '-'));

	return ((struct spwd *)NSS_XbyY_FINI(&arg));
}


int
get_usernam(char *backend, char *name, userattr_t **result, nss_XbyY_buf_t *b)
{
	userstr_t *user;
	int rc;

	rc = get_db_ent(SEC_REP_DB_USERATTR, backend, -1, name,
	    (void**) &user, b);

	if (rc == SEC_REP_SUCCESS) {
		*result = (userattr_t *)_userstr2attr(user);
		if (*result == NULL)
			return (SEC_REP_NOT_FOUND);
	}
	return (rc);
}

static int
files_get_usernam(char *name, userattr_t **result, nss_XbyY_buf_t *b)
{
	return (get_usernam(files_backend, name, result, b));
}

static int
files_get_useruid(uid_t uid, userattr_t **result, nss_XbyY_buf_t *b)
{
	struct passwd *pwd;

	if (result) {
		if (files_get_pwid(uid, &pwd, b) == SEC_REP_SUCCESS) {
			if (pwd)
				return (files_get_usernam(pwd->pw_name,
				    result, b));
			else
				return (SEC_REP_NOT_FOUND);
		}
	}

	return (SEC_REP_INVALID_ARG);
}

userattr_t *
get_userattr(nss_XbyY_buf_t *b)
{
	userstr_t *user = NULL;

	user = (userstr_t *)get_ent(b);

	return ((userattr_t *)_userstr2attr(user));

}

static void
files_set_userattr()
{
	set_ent(SEC_REP_DB_USERATTR, files_backend);
}

static int
files_get_grnam(char *name, struct group **result, nss_XbyY_buf_t *b)
{
	return (get_db_ent(SEC_REP_DB_GROUP, files_backend, -1, name,
	    (void**)result, b));
}

static int
files_get_grid(gid_t gid, struct group ** result, nss_XbyY_buf_t *b)
{
	return (get_db_ent(SEC_REP_DB_GROUP, files_backend, gid, NULL,
	    (void**)result, b));
}

struct group *
get_group(nss_XbyY_buf_t *b)
{
	return ((struct group *)get_ent(b));
}

static void
files_set_group()
{
	set_ent(SEC_REP_DB_GROUP, files_backend);
}

static int
files_get_projnam(char *name, struct project **result,
	    nss_XbyY_buf_t *b)
{
	return (get_db_ent(SEC_REP_DB_PROJECT, files_backend, -1, name,
	    (void**)result, b));
}

static int
files_get_projid(projid_t projid, struct project **result, nss_XbyY_buf_t *b)
{
	return (get_db_ent(SEC_REP_DB_PROJECT, files_backend, projid, NULL,
	    (void**)result, b));
}

struct project *
get_project(nss_XbyY_buf_t *b)
{
	return (get_ent(b));
}

static void
files_set_project()
{
	set_ent(SEC_REP_DB_PROJECT, files_backend);
}

int
get_profnam(char *backend, char *name, profattr_t **result, nss_XbyY_buf_t *b)
{
	int rc;
	profstr_t *prof;

	rc = get_db_ent(SEC_REP_DB_PROFATTR, backend, -1, name,
	    (void**) &prof, b);

	if (rc == SEC_REP_SUCCESS) {

		*result = (profattr_t *)_profstr2attr(prof);

		if (*result == NULL)
			return (SEC_REP_NOT_FOUND);
	}
	return (rc);
}

static int
files_get_profnam(char *name, profattr_t **result, nss_XbyY_buf_t *b)
{
	return (get_profnam(files_backend, name, result, b));
}

profattr_t *
get_profattr(nss_XbyY_buf_t *b)
{
	profstr_t *prof = NULL;

	prof = (profstr_t *)get_ent(b);

	return ((profattr_t *)_profstr2attr(prof));
}

static void
files_set_profattr()
{
	set_ent(SEC_REP_DB_PROFATTR, files_backend);
}

int
get_authnam(char *backend, char *name, authattr_t **result, nss_XbyY_buf_t *b)
{
	int rc;
	authstr_t *auth;

	rc = get_db_ent(SEC_REP_DB_AUTHATTR, backend, -1, name,
	    (void**) &auth, b);

	if (rc == SEC_REP_SUCCESS) {

		*result = (authattr_t *)_authstr2attr(auth);

		if (*result == NULL)
			return (SEC_REP_NOT_FOUND);
	}
	return (rc);
}

static int
files_get_authnam(char *name, authattr_t **result, nss_XbyY_buf_t *b)
{
	return (get_authnam(files_backend, name, result, b));
}

authattr_t *
get_authattr(nss_XbyY_buf_t *b)
{
	authstr_t *auth = NULL;

	auth = (authstr_t *)get_ent(b);
	return ((authattr_t *)_authstr2attr(auth));
}

static void
files_set_authattr()
{
	set_ent(SEC_REP_DB_AUTHATTR, files_backend);
}


static int
files_add_group(struct group *grp)
{
	FILE *etcgrp; /* /etc/group file */
	FILE *etctmp; /* temp file */
	int o_mask; /* old umask value */
	int newdone = 0; /* set true when new entry done */
	struct stat sb; /* stat buf to copy modes */
	char buf[NSS_BUFLEN_GROUP];
	char grpentry[NSS_BUFLEN_GROUP];
	char **userlist = grp->gr_mem;


	if ((etcgrp = fopen(GROUP, "r")) == NULL) {
		return (EX_UPDATE);
	}

	if (fstat(fileno(etcgrp), &sb) < 0) {
		/* If we can't get mode, take a default */
		sb.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	}

	o_mask = umask(077);
	etctmp = fopen(GRPTMP, "w+");
	(void) umask(o_mask);

	if (etctmp == NULL) {
		(void) fclose(etcgrp);
		return (EX_UPDATE);
	}

	if (fchmod(fileno(etctmp), sb.st_mode) != 0 ||
	    fchown(fileno(etctmp), sb.st_uid, sb.st_gid) != 0 ||
	    lockf(fileno(etctmp), F_LOCK, 0) != 0) {
		(void) fclose(etcgrp);
		(void) fclose(etctmp);
		(void) unlink(GRPTMP);
		return (EX_UPDATE);
	}

	/* Format the group entry */
	(void) snprintf(grpentry, sizeof (grpentry), "%s::%u:",
	    grp->gr_name, grp->gr_gid);

	/* Add the first user if any */
	if (userlist != NULL && *userlist != NULL) {
		(void) strcat(grpentry, *userlist);
		userlist++;

		/* Append the remaining users */
		while (*userlist != NULL) {
			(void) strcat(grpentry, ",");
			(void) strcat(grpentry, *userlist);
			userlist++;
		}
	}

	while (fgets(buf, NSS_BUFLEN_GROUP, etcgrp) != NULL) {
		/* Check for NameService reference */
		if (!newdone && (buf[0] == '+' || buf[0] == '-')) {
			(void) fprintf(etctmp, "%s\n", grpentry);
			newdone = 1;
		}

		(void) fputs(buf, etctmp);
	}


	(void) fclose(etcgrp);

	if (!newdone) {
		(void) fprintf(etctmp, "%s\n", grpentry);
	}

	if (rename(GRPTMP, GROUP) < 0) {
		(void) fclose(etctmp);
		(void) unlink(GRPTMP);
		return (EX_UPDATE);
	}

	(void) fclose(etctmp);


	return (EX_SUCCESS);
}

/* Modify group to new gid and/or new name */
static int
files_modify_group(struct group *grp, char *newgroup, int flag)
{
	int modified = 0, deleted = 0;
	int o_mask;
	FILE *e_fptr, *t_fptr;
	struct group *g_ptr;
	struct stat sbuf;
	boolean_t haserr;
	int line = 1;

	if ((e_fptr = fopen(GROUP, "r")) == NULL)
		return (EX_UPDATE);

	if (fstat(fileno(e_fptr), &sbuf) < 0) {
		/* If we can't get mode, take a default */
		sbuf.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	}

	o_mask = umask(077);
	t_fptr = fopen(GRPTMP, "w+");
	(void) umask(o_mask);

	if (t_fptr == NULL) {
		(void) fclose(e_fptr);
		return (EX_UPDATE);
	}

	if (fchmod(fileno(t_fptr), sbuf.st_mode) != 0 ||
	    fchown(fileno(t_fptr), sbuf.st_uid, sbuf.st_gid) != 0 ||
	    lockf(fileno(t_fptr), F_LOCK, 0) != 0) {
		(void) fclose(e_fptr);
		(void) fclose(t_fptr);
		(void) unlink(GRPTMP);
		return (EX_UPDATE);
	}

	while ((g_ptr = fgetgrent(e_fptr)) != NULL) {
		/* check to see if group is one to modify */
		if (strcmp(g_ptr->gr_name, grp->gr_name) == 0) {
			if (flag & MOD_MASK) {
				if (newgroup != NULL) {
					g_ptr->gr_name = newgroup;
				}

				if (grp->gr_gid != (gid_t)-1) {
					g_ptr->gr_gid = grp->gr_gid;
				}

				if (grp->gr_mem != NULL) {
					g_ptr->gr_mem = grp->gr_mem;
				}

				modified++;
			} else if (flag & DEL_MASK) {
				deleted = 1;
				continue;
			}
		}
		putgrent(g_ptr, t_fptr);
		line++;
	}

	haserr = !feof(e_fptr);
	if (haserr) {
		const char *err_msg = gettext("ERROR: Syntax error in group"
		    "file at line %d.\n");
		(void) fprintf(stderr, err_msg, line);
	}

	(void) fclose(e_fptr);

	if (fclose(t_fptr) != 0 || haserr) {
		/* GROUP file contains bad entries or write failed. */
		(void) unlink(GRPTMP);
		return (EX_UPDATE);
	}

	if (modified || deleted) {
		if (rename(GRPTMP, GROUP) != 0) {
			(void) unlink(GRPTMP);
			return (EX_UPDATE);
		}
		return (EX_SUCCESS);
	} else {
		(void) unlink(GRPTMP);
		return (EX_NAME_NOT_EXIST);
	}
}


static int
files_put_group(struct group *grp, char *newname, int flags)
{
	int rc;

	if (flags & ADD_MASK) {
		rc = files_add_group(grp);
	} else {
		rc = files_modify_group(grp, newname, flags);
	}
	return (rc);
}

/*ARGSUSED*/
static int
files_put_project(struct project *proj, int flags)
{
	return (SEC_REP_OP_NOT_SUPPORTED);
}



execattr_t *
get_execprof(char *profname, char *type, char *id, int search_flag,
    nss_XbyY_buf_t *b, struct nss_calls *pnss_calls) {
	int		getby_flag;
	char		policy_buf[BUFSIZ];
	const char	*empty = NULL;
	nss_status_t	res = NSS_NOTFOUND;
	nss_XbyY_args_t	arg;
	_priv_execattr	_priv_exec;
	static mutex_t	_nsw_exec_lock = DEFAULTMUTEX;
	execstr_t	*result;

	if (pnss_calls == NULL)
		return (NULL);

	NSS_XbyY_INIT(&arg, b->result, b->buffer, b->buflen,
	    _str2execattr);

	_priv_exec.name = (profname == NULL) ? empty : (const char *)profname;
	_priv_exec.type = (type == NULL) ? empty : (const char *)type;
	_priv_exec.id = (id == NULL) ? empty : (const char *)id;
	(void) strncpy(policy_buf, DEFAULT_POLICY, BUFSIZ);

	_priv_exec.policy = policy_buf;
	_priv_exec.search_flag = search_flag;
	_priv_exec.head_exec = NULL;
	_priv_exec.prev_exec = NULL;

	if ((profname != NULL) && (id != NULL)) {
		getby_flag = NSS_DBOP_EXECATTR_BYNAMEID;
	} else if (profname != NULL) {
		getby_flag = NSS_DBOP_EXECATTR_BYNAME;
	} else if (id != NULL) {
		getby_flag = NSS_DBOP_EXECATTR_BYID;
	} else
		return (NULL);

	arg.key.attrp = &(_priv_exec);

	switch (getby_flag) {
	case NSS_DBOP_EXECATTR_BYID:
		res = nss_search(&files_db_root, pnss_calls->initf_nss_exec,
		    getby_flag, &arg);
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
			parg.key.name = profname;

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
				*(pnss_calls->pnsw_search_path) = (conf == NULL)
				    ? NSS_FILES_ONLY : lookups->service_name;

				pres = nss_search(&prof_root,
				    pnss_calls->initf_nsw_prof,
				    NSS_DBOP_PROFATTR_BYNAME, &parg);
				if (pres == NSS_SUCCESS) {
					DEFINE_NSS_DB_ROOT(pexec_root);
					res = nss_search(&pexec_root,
					    pnss_calls->initf_nsw_exec,
					    getby_flag, &arg);
					if (pexec_root.s != NULL)
						_nss_db_state_destr(
						    pexec_root.s);
				}
				if (prof_root.s != NULL)
					_nss_db_state_destr(prof_root.s);
				(void) mutex_unlock(&_nsw_exec_lock);
				if ((pres == NSS_SUCCESS) || (conf == NULL))
					break;
			} while ((lookups = lookups->next) != NULL);
		}
		break;
	default:
		break;
	}

	out:
	arg.status = res;
	result = ((execstr_t *)NSS_XbyY_FINI(&arg));
	return ((execattr_t *)_execstr2attr(result, result));
}

static execattr_t *
files_get_execprof(char *profname, char *type, char *id, int search_flag,
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
files_get_execuser(char *username, char *type, char *id, int search_flag,
    nss_XbyY_buf_t *b) {
	return (NULL);
}


execattr_t *
get_execattr(nss_XbyY_buf_t *b)
{
	execstr_t *exec;

	exec = (execstr_t *)get_ent(b);

	return ((execattr_t *)_execstr2attr(exec, exec));

}

static void
files_set_execattr()
{
	set_ent(SEC_REP_DB_EXECATTR, files_backend);

}

void
fprint_attr(FILE *out, kva_t *attr)
{
	int i, j;
	char *key;
	char *val;
	kv_t *kv_pair;

	if (attr != NULL && out != NULL) {
		kv_pair = attr->data;
		for (i = j = 0; i < attr->length; i++) {
			key = kv_pair[i].key;
			val = kv_pair[i].value;
			if ((key == NULL) || (val == NULL) ||
			    (strlen(val) == 0))
				continue;
			if (j > 0)
				(void) fprintf(out, KV_DELIMITER);
			(void) fprintf(out, "%s=%s", key, val);
			j++;
		}
		(void) fprintf(out, "\n");
	}
}

static int
files_put_authattr(authattr_t *auth, int flags)
{
	FILE *etcauth; /* /etc/security/auth file */
	FILE *etctmp; /* temp file */
	int o_mask; /* old umask value */
	int added = 0, deleted = 0, modified = 0;
	struct stat sb; /* stat buf to copy modes */
	char buf[NSS_BUFLEN_AUTHATTR];
	char tmpbuf[NSS_BUFLEN_AUTHATTR];
	char *last = NULL;
	char *sep = KV_TOKEN_DELIMIT;
	int haserr = 0, line = 1;


	if (auth == NULL || auth->name == NULL || auth->name[0] == '\0') {
		return (SEC_REP_INVALID_ARG);
	}

	if ((etcauth = fopen(AUTHATTR_FILENAME, "r")) == NULL) {
		return (EX_UPDATE);
	}

	if (fstat(fileno(etcauth), &sb) < 0) {
		/* If we can't get mode, take a default */
		sb.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	}

	o_mask = umask(077);
	etctmp = fopen(AUTHTMP, "w+");
	(void) umask(o_mask);

	if (etctmp == NULL) {
		(void) fclose(etcauth);
		return (EX_UPDATE);
	}

	if (fchmod(fileno(etctmp), sb.st_mode) != 0 ||
	    fchown(fileno(etctmp), sb.st_uid, sb.st_gid) != 0 ||
	    lockf(fileno(etctmp), F_LOCK, 0) != 0) {
		(void) fclose(etcauth);
		(void) fclose(etctmp);
		(void) unlink(AUTHTMP);
		return (EX_UPDATE);
	}

	while (fgets(buf, NSS_BUFLEN_AUTHATTR, etcauth) != NULL &&
	    !haserr) {
		char *name;

		if (buf[0] == '#' || buf[0] == '\n') {
			(void) fputs(buf, etctmp);
			line++;
			continue;
		}
		(void) strncpy(tmpbuf, buf, NSS_BUFLEN_AUTHATTR);
		name = (char *)_strtok_escape(tmpbuf, sep, &last);

		if (name != NULL && name[0] != '\0' &&
		    (strcmp(name, auth->name) == 0)) {
			if (flags & AUTH_ADD_MASK) {
				const char *err_msg = gettext("ERROR: "
				    "Duplicate entry in auth file"
				    " at line %d.\n");
				(void) fprintf(stderr, err_msg, line);
				break;
			}
			if (flags & AUTH_DEL_MASK) {
				deleted = 1;
				continue;
			} else { /* MODIFY */
				(void) fprintf(etctmp, "%s:::%s::",
				    auth->name, auth->short_desc);
				fprint_attr(etctmp, auth->attr);
				modified = 1;
				continue;
			}
		}
		(void) fputs(buf, etctmp);
		line++;
	}

	haserr = !feof(etcauth);
	if (haserr) {
		const char *err_msg = gettext("ERROR: Syntax error in auth file"
		    " at line %d.\n");
		(void) fprintf(stderr, err_msg, line);
	}

	if (fclose(etcauth) != 0 || haserr) {
		(void) unlink(AUTHTMP);
		return (EX_UPDATE);
	}

	if (flags & AUTH_ADD_MASK) {
		(void) fprintf(etctmp, "%s:::%s::",
		    auth->name, auth->short_desc);
		fprint_attr(etctmp, auth->attr);
		added = 1;
	}
	if (added || modified || deleted) {
		if (rename(AUTHTMP, AUTHATTR_FILENAME) < 0) {
			(void) fclose(etctmp);
			(void) unlink(AUTHTMP);
			return (EX_UPDATE);
		}
	} else {
		(void) fclose(etctmp);
		(void) unlink(AUTHTMP);
		return (EX_NAME_NOT_EXIST);
	}

	(void) fclose(etctmp);

	return (EX_SUCCESS);
}


static void
replace_tab2space(char *str)
{
	int i = 0;

	while ((str) && (str[i])) {
		if (str[i] == '\t')
			str[i] = ' ';
		i++;
	}
}

static int
files_edit_autohome(char *login, char *new_login, char *path, int flags)
{
	FILE *e_fptr; /* /etc/auto_home file */
	FILE *t_fptr; /* temp file */
	int o_mask; /* old umask value */
	int deleted = 0, modified = 0;
	struct stat sbuf; /* stat buf to copy modes */
	char buf[NSS_BUFSIZ + 1];
	char tmpbuf[NSS_BUFSIZ + 1];
	char *key;
	int haserr = 0, line = 1;
	char db_path[PATH_MAX + 1];

	(void) snprintf(db_path, sizeof (db_path), "/etc/%s",
	    get_autohome_db());
	e_fptr = fopen(db_path, "r");
	if (e_fptr == NULL || fstat(fileno(e_fptr), &sbuf) < 0) {
		/* If we can't get mode, take a default */
		sbuf.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
		sbuf.st_uid = 0; /* root */
		sbuf.st_gid = 2; /* bin */
	}

	o_mask = umask(077);
	t_fptr = fopen(AUTOHOMETMP, "w+");
	(void) umask(o_mask);

	if (t_fptr == NULL) {
		if (e_fptr != NULL)
			(void) fclose(e_fptr);
		return (EX_UPDATE);
	}

	/*
	 * Get ownership and permissions correct
	 */
	if (fchmod(fileno(t_fptr), sbuf.st_mode) != 0 ||
	    fchown(fileno(t_fptr), sbuf.st_uid, sbuf.st_gid) != 0 ||
	    lockf(fileno(t_fptr), F_LOCK, 0) != 0) {
		if (e_fptr != NULL)
			(void) fclose(e_fptr);
		(void) fclose(t_fptr);
		(void) unlink(AUTOHOMETMP);
		return (EX_UPDATE);
	}


	while (e_fptr != NULL && fgets(buf, NSS_BUFSIZ, e_fptr) != NULL) {
		char *p;
		char *location;
		if (buf[0] == '#' || buf[0] == '+') {
			(void) fputs(buf, t_fptr);
			line++;
			continue;
		} else if (buf[0] == '\n')
			continue;


		replace_tab2space(buf);
		(void) strncpy(tmpbuf, buf, NSS_BUFSIZ +1);
		p = tmpbuf;
		while (*p == ' ') p++;
		key = p;
		while (*p != ' ') p++;
		*p++ = '\0';


		if (strcmp(key, login) == 0) {
			if (flags & DEL_MASK) {
				deleted = 1;
			} else {
				if (path) {
					(void) fprintf(t_fptr, "%s %s\n",
					    (new_login ? new_login : login),
					    path);
					modified = 1;
				} else {
					while (*p == ' ') p++;
					location = p;
					while (*p != ' ') p++;
					*p++ = '\0';

					if (location) {
						(void) fprintf(t_fptr,
						    "%s %s\n",
						    (new_login ?
						    new_login : login),
						    location);
						modified = 1;
					}
				}
			}

		} else {
			(void) fputs(buf, t_fptr);
		}
		line++;
	}
	if (!(flags & DEL_MASK) && !modified && !haserr) {
		(void) fprintf(t_fptr, "%s %s\n",
		    (new_login ? new_login : login), path);
		modified = 1;
	}

	if (haserr == 0 && e_fptr != NULL)
		haserr = !feof(e_fptr);
	if (haserr == 1) {
		(void) fprintf(stderr, gettext("ERROR: Failed to read"
		    " /etc/auto_home due to invalid entry or read error.\n"));
	}

	if ((e_fptr != NULL && fclose(e_fptr) != 0) || haserr == 1) {
		(void) unlink(AUTOHOMETMP);
		return (EX_UPDATE);
	}

	if (modified || deleted) {
		if (rename(AUTOHOMETMP, db_path) < 0) {
			(void) fclose(t_fptr);
			(void) unlink(AUTOHOMETMP);
			return (EX_UPDATE);
		}
	} else {
		(void) unlink(AUTOHOMETMP);
		return (EX_SUCCESS);
	}

	(void) fclose(t_fptr);

	return (EX_SUCCESS);
}

static int
files_get_autohome(char *login, char *path)
{
	FILE *e_fptr; /* /etc/auto_home file */
	char buf[NSS_BUFSIZ + 1];
	char *key;
	int found = 0;
	char db_path[PATH_MAX + 1];

	(void) snprintf(db_path, sizeof (db_path), "/etc/%s",
	    get_autohome_db());
	if ((e_fptr = fopen(db_path, "r")) == NULL) {
		return (EX_UPDATE);
	}

	while (fgets(buf, NSS_BUFSIZ, e_fptr) != NULL && !found) {
		char *p;
		char *location;
		char *next;
		if (buf[0] == '#' || buf[0] == '+' || buf[0] == '\n')
			continue;

		replace_tab2space(buf);
		p = &buf[0];
		while (*p == ' ') p++;
		key = p;
		while (*p != ' ') p++;
		next = p+1;
		*p++ = '\0';

		if (strcmp(key, login) == 0) {
			found = 1;
			p = next;
			while (*p == ' ') p++;
			location = p;
			while (*p != ' ' && *p != '\n') p++;
			*p++ = '\0';

			(void) strcpy(path, location);
		}
	}

	if (fclose(e_fptr)) {
		return (EX_FAILURE);
	}

	return (EX_SUCCESS);
}

static int
files_put_execattr(execattr_t *exec, int flags)
{
	FILE *etcexec; /* /etc/security/exec file */
	FILE *etctmp; /* temp file */
	int o_mask; /* old umask value */
	int added = 0, deleted = 0, modified = 0;
	struct stat sb; /* stat buf to copy modes */
	char buf[NSS_BUFLEN_EXECATTR];
	char tmpbuf[NSS_BUFLEN_EXECATTR];
	char *last = NULL;
	char *sep = KV_TOKEN_DELIMIT;
	int haserr = 0, line = 1;

	if (exec == NULL || exec->name == NULL || exec->name[0] == '\0') {
		return (SEC_REP_INVALID_ARG);
	}

	if ((etcexec = fopen(EXECATTR_FILENAME, "r")) == NULL) {
		return (EX_UPDATE);
	}

	if (fstat(fileno(etcexec), &sb) < 0) {
		/* If we can't get mode, take a default */
		sb.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	}

	o_mask = umask(077);
	etctmp = fopen(EXECTMP, "w+");
	(void) umask(o_mask);

	if (etctmp == NULL) {
		(void) fclose(etcexec);
		return (EX_UPDATE);
	}

	if (fchmod(fileno(etctmp), sb.st_mode) != 0 ||
	    fchown(fileno(etctmp), sb.st_uid, sb.st_gid) != 0 ||
	    lockf(fileno(etctmp), F_LOCK, 0) != 0) {
		(void) fclose(etcexec);
		(void) fclose(etctmp);
		(void) unlink(EXECTMP);
		return (EX_UPDATE);
	}

	while (fgets(buf, NSS_BUFLEN_EXECATTR, etcexec) != NULL &&
	    !haserr) {
		char *profname;
		char *type;
		char *id;

		if (buf[0] == '#' || buf[0] == '\n') {
			(void) fputs(buf, etctmp);
			line++;
			continue;
		}
		(void) strncpy(tmpbuf, buf, NSS_BUFLEN_EXECATTR);
		profname = (char *)_strtok_escape(tmpbuf, sep, &last);
		(void) _strtok_escape(NULL, sep, &last); /* policy */
		type = (char *)_strtok_escape(NULL, sep, &last);
		(void) _strtok_escape(NULL, sep, &last); /* res1 */
		(void) _strtok_escape(NULL, sep, &last); /* res2 */
		id = (char *)_strtok_escape(NULL, sep, &last);

		if ((profname && exec->name &&
		    (strcmp(profname, exec->name) == 0)) &&
		    (type && exec->type && (strcmp(type, exec->type) == 0)) &&
		    (id && exec->id && (strcmp(id, exec->id) == 0))) {
			if (flags & ADD_MASK) {
				const char *err_msg = gettext("ERROR: "
				    "Duplicate entry in exec file"
				    " at line %d.\n");
				(void) fprintf(stderr, err_msg, line);
				break;
			}
			if (flags & DEL_MASK) {
				if (strcmp(exec->res1, "RO") != 0) {
					deleted = 1;
					line++;
					continue;
				}
			} else { /* MODIFY */
				(void) fprintf(etctmp, "%s:%s:%s:::%s:",
				    exec->name, exec->policy,
				    exec->type, exec->id);
				fprint_attr(etctmp, exec->attr);
				modified = 1;
				line++;
				continue;
			}
		}
		(void) fputs(buf, etctmp);
		line++;
	}

	haserr = !feof(etcexec);
	if (haserr) {
		const char *err_msg = gettext("ERROR: Syntax error in exec file"
		    " at line %d.\n");
		(void) fprintf(stderr, err_msg, line);
	}

	if (fclose(etcexec) != 0 || haserr) {
		(void) unlink(EXECTMP);
		return (EX_UPDATE);
	}

	if (flags & ADD_MASK) {
		(void) fprintf(etctmp, "%s:%s:%s:::%s:",
		    exec->name, exec->policy,
		    exec->type, exec->id);
		fprint_attr(etctmp, exec->attr);
		added = 1;
	}
	if (added || modified || deleted) {
		if (rename(EXECTMP, EXECATTR_FILENAME) < 0) {
			(void) fclose(etctmp);
			(void) unlink(EXECTMP);
			return (EX_UPDATE);
		}
	} else  {
		(void) unlink(EXECTMP);
		if (flags & (ADD_MASK | MOD_MASK))
			return (EX_NAME_NOT_EXIST);
	}

	(void) fclose(etctmp);

	return (EX_SUCCESS);
}

static int
files_put_profattr(profattr_t *prof, int flags)
{
	FILE *etcprof; /* /etc/security/prof file */
	FILE *etctmp; /* temp file */
	int o_mask; /* old umask value */
	int added = 0, deleted = 0, modified = 0;
	struct stat sb; /* stat buf to copy modes */
	char buf[NSS_BUFLEN_PROFATTR];
	char tmpbuf[NSS_BUFLEN_PROFATTR];
	char *last = NULL;
	char *sep = KV_TOKEN_DELIMIT;
	int haserr = 0, line = 1;

	if (prof == NULL || prof->name == NULL || prof->name[0] == '\0') {
		return (SEC_REP_INVALID_ARG);
	}

	if ((etcprof = fopen(PROFATTR_FILENAME, "r")) == NULL) {
		return (EX_UPDATE);
	}

	if (fstat(fileno(etcprof), &sb) < 0) {
		/* If we can't get mode, take a default */
		sb.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	}

	o_mask = umask(077);
	etctmp = fopen(PROFTMP, "w+");
	(void) umask(o_mask);

	if (etctmp == NULL) {
		(void) fclose(etcprof);
		return (EX_UPDATE);
	}

	if (fchmod(fileno(etctmp), sb.st_mode) != 0 ||
	    fchown(fileno(etctmp), sb.st_uid, sb.st_gid) != 0 ||
	    lockf(fileno(etctmp), F_LOCK, 0) != 0) {
		(void) fclose(etcprof);
		(void) fclose(etctmp);
		(void) unlink(PROFTMP);
		return (EX_UPDATE);
	}

	while (fgets(buf, NSS_BUFLEN_PROFATTR, etcprof) != NULL) {
		char *name;

		if (buf[0] == '#' || buf[0] == '\n') {
			(void) fputs(buf, etctmp);
			line++;
			continue;
		}

		(void) strncpy(tmpbuf, buf, NSS_BUFLEN_PROFATTR);
		name = (char *)_strtok_escape(tmpbuf, sep, &last);

		if (name != NULL && name[0] != '\0' &&
		    (strcmp(name, prof->name) == 0)) {
			if (flags & ADD_MASK) {
				const char *err_msg = gettext("ERROR: "
				    "Duplicate entry in prof file"
				    " at line %d.\n");
				(void) fprintf(stderr, err_msg, line);
				break;
			}
			if (flags & DEL_MASK) {
				deleted = 1;
				line++;
				continue;
			} else { /* MODIFY */
				(void) fprintf(etctmp, "%s:::%s:",
				    prof->name, prof->desc);
				fprint_attr(etctmp, prof->attr);
				modified = 1;
				line++;
				continue;
			}
		}
		(void) fputs(buf, etctmp);
		line++;
	}

	haserr = !feof(etcprof);
	if (haserr) {
		const char *err_msg = gettext("ERROR: Syntax error in prof file"
		    " at line %d.\n");
		(void) fprintf(stderr, err_msg, line);
	}

	if (fclose(etcprof) != 0 || haserr) {
		(void) unlink(PROFTMP);
		return (EX_UPDATE);
	}

	if (flags & ADD_MASK) {
		(void) fprintf(etctmp, "%s:::%s:",
		    prof->name, prof->desc);
		fprint_attr(etctmp, prof->attr);
		added = 1;
	} else if ((flags & MOD_MASK) && modified == 0) {
		if (strcmp(prof->res1, "RO") != 0) {
			(void) fprintf(etctmp, "%s:::%s:",
			    prof->name, prof->desc);
			fprint_attr(etctmp, prof->attr);
			modified = 1;
		} else {
			(void) fprintf(stderr,
			    gettext("Can't update read-only profile\n"));
		}
	}

	(void) fclose(etctmp);
	if (added || modified || deleted) {
		if (rename(PROFTMP, PROFATTR_FILENAME) < 0) {
			(void) unlink(PROFTMP);
			return (EX_UPDATE);
		}
	} else {
		(void) unlink(PROFTMP);
		if (flags & (ADD_MASK | MOD_MASK))
			return (EX_NAME_NOT_EXIST);
	}

	return (EX_SUCCESS);
}

void
l_to_str(const m_label_t *l, char **str, int ltype)
{
	if (label_to_str(l, str, ltype, DEF_NAMES) != 0)
		*str = strdup(gettext("label translation failed"));
}

/*
 * Produce ascii format of address and prefix length
 */
void
translate_inet_addr(tsol_rhent_t *rhentp, int *alen, char abuf[], int abuflen)
{
	void *aptr;
	char tmpbuf[20];	/* long enoigh for prefix */

	(void) snprintf(tmpbuf, sizeof (tmpbuf), "/%d", rhentp->rh_prefix);

	if (rhentp->rh_address.ta_family == AF_INET6) {
		aptr = &(rhentp->rh_address.ta_addr_v6);
	} else {
		aptr = &(rhentp->rh_address.ta_addr_v4);
	}
	(void) inet_ntop(rhentp->rh_address.ta_family, aptr, abuf,
	    abuflen);
	if (strlcat(abuf, tmpbuf, abuflen) >= abuflen)
		(void) fprintf(stderr, gettext(
		    "tnctl: buffer overflow detected: %s\n"),
		    abuf);
	*alen = strlen(abuf);
}

static int
files_put_tnrhdb(tsol_rhent_t *rhentp, int flags)
{
	FILE *tsolrhdb; /* /etc/security/tsol file */
	FILE *tmprhdb; /* temp file */
	int o_mask; /* old umask value */
	int added = 0, deleted = 0;
	struct stat sb; /* stat buf to copy modes */
	int haserr = 0, linenum = 0;
	char line[80];	/* address + template name */
	tsol_rhent_t *file_rhentp;
	tsol_rhstr_t rhstr;
	int err;
	char *errstr;
	char buf[NSS_BUFLEN_TSOL_RH];

	if (!rhentp || !rhentp->rh_template || !strlen(rhentp->rh_template)) {
		return (SEC_REP_INVALID_ARG);
	}

	if ((tsolrhdb = fopen(TNRHDB_PATH, "r")) == NULL) {
		return (EX_UPDATE);
	}

	if (fstat(fileno(tsolrhdb), &sb) < 0) {
		/* If we can't get mode, take a default */
		sb.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	}

	o_mask = umask(077);
	tmprhdb = fopen(TNRHDB_TMP, "w+");
	(void) umask(o_mask);

	if (tmprhdb == NULL) {
		(void) fclose(tsolrhdb);
		return (EX_UPDATE);
	}

	if (fchmod(fileno(tmprhdb), sb.st_mode) != 0 ||
	    fchown(fileno(tmprhdb), sb.st_uid, sb.st_gid) != 0 ||
	    lockf(fileno(tmprhdb), F_LOCK, 0) != 0) {
		(void) fclose(tsolrhdb);
		(void) fclose(tmprhdb);
		(void) unlink(TNRHDB_TMP);
		return (EX_UPDATE);
	}

	while (fgets(line, sizeof (line), tsolrhdb) != NULL) {
		linenum++;
		if (line[0] == '#' ||line[0] == '\n') {
			(void) fputs(line, tmprhdb);
			continue;
		}
		(void) str_to_rhstr(line, strlen(line), &rhstr, buf,
		    sizeof (buf));
		file_rhentp = rhstr_to_ent(&rhstr, &err, &errstr);
		if (file_rhentp == NULL) {
			if (err == LTSNET_EMPTY) {
				(void) fputs(line, tmprhdb);
				continue;
			}
			(void) fprintf(stderr,
			    gettext("line %1$d: %2$s: %.32s\n"), linenum,
			    tsol_strerror(err, errno), errstr);
			break;
		}

		if ((strncmp(file_rhentp->rh_template, rhentp->rh_template,
		    TNTNAMSIZ) == 0) &&
		    (TNADDR_EQ(&file_rhentp->rh_address,
		    &rhentp->rh_address))) {
			if (flags & ADD_MASK) {
				const char *err_msg = gettext("ERROR: "
				    "Duplicate entry in tnrhdb file"
				    " at line %d.\n");

				(void) fprintf(stderr, err_msg, linenum);
				tsol_freerhent(file_rhentp);
				break;
			}
			if (flags & DEL_MASK) {
				deleted = 1;
				tsol_freerhent(file_rhentp);
				continue;
			}
		}
		(void) fputs(line, tmprhdb);
		tsol_freerhent(file_rhentp);
	}

	haserr = !feof(tsolrhdb);
	if (haserr) {
		const char *err_msg = gettext("ERROR: Syntax error in tnrhdb "
		    "file at line %d.\n");
		(void) fprintf(stderr, err_msg, linenum);
	}

	if (fclose(tsolrhdb) != 0 || haserr) {
		(void) fclose(tmprhdb);
		(void) unlink(TNRHDB_TMP);
		return (EX_UPDATE);
	}

	if (flags & ADD_MASK) {
		int alen;
		char abuf[INET6_ADDRSTRLEN + 5];
		char *tmp;

		(void) translate_inet_addr(rhentp, &alen, abuf, sizeof (abuf));
		tmp = (char *)_escape(abuf, ":");
		(void) fprintf(tmprhdb, "%s:%s\n", tmp, rhentp->rh_template);
		free(tmp);
		added = 1;
	}
	(void) fclose(tmprhdb);
	if (added || deleted) {
		if (rename(TNRHDB_TMP, TNRHDB_PATH) < 0) {
			(void) unlink(TNRHDB_TMP);
			return (EX_UPDATE);
		}
	} else {
		(void) unlink(TNRHDB_TMP);
		return (EX_NAME_NOT_EXIST);
	}


	return (EX_SUCCESS);
}

static void
fprint_tnrhdb(FILE *out, tsol_tpent_t *tp)
{
	char *str;
	const m_label_t *l1, *l2;
	boolean_t first = B_TRUE;

	(void) fprintf(out, "%s:", tp->name);
	switch (tp->host_type) {
	case UNLABELED:
		(void) fprintf(out, "%s=%s;%s=%d;",
		    TP_HOSTTYPE, TP_UNLABELED,
		    TP_DOI, tp->tp_doi);

		if (tp->tp_mask_unl & TSOL_MSK_DEF_LABEL) {
			(void) l_to_str(&tp->tp_def_label, &str, M_INTERNAL);
			(void) fprintf(out, "%s=%s;", TP_DEFLABEL, str);
			free(str);
		}
		break;
	case SUN_CIPSO:
		(void) fprintf(out, "%s=%s;%s=%d;",
		    TP_HOSTTYPE, TP_CIPSO,
		    TP_DOI, tp->tp_doi);
		break;
	}
	if (tp->tp_mask_unl & TSOL_MSK_SL_RANGE_TSOL) {
		l_to_str(&tp->tp_gw_sl_range.lower_bound,
		    &str, M_INTERNAL);
		(void) fprintf(out, "%s=%s;",
		    TP_MINLABEL, str);
		free(str);

		l_to_str(&tp->tp_gw_sl_range.upper_bound,
		    &str, M_INTERNAL);
		(void) fprintf(out, "%s=%s;",
		    TP_MAXLABEL, str);
		free(str);

		l1 = (const m_label_t *)&tp->tp_gw_sl_set[0];
		l2 = (const m_label_t *)&tp->tp_gw_sl_set[NSLS_MAX];
		for (; l1 < l2; l1++) {
			if (!BLTYPE(l1, SUN_SL_ID))
				continue;
			l_to_str(l1, &str, M_INTERNAL);
			if (first) {
				first = B_FALSE;
				(void) fprintf(out, "%s=%s",
				    TP_SET, str);
			} else {
				(void) fprintf(out, ",%s",
				    str);
				}
			free(str);
		}
		if (!first)
			(void) fprintf(out, ";");
	}
	(void) fprintf(out, "\n");
}

static int
files_put_tnrhtp(tsol_tpent_t *tp, int flags)
{
	FILE *tsolrhtp; /* /etc/security/tsol file */
	FILE *tmprhtp; /* temp file */
	int o_mask; /* old umask value */
	int added = 0, modified = 0, deleted = 0;
	struct stat sb; /* stat buf to copy modes */
	int haserr = 0, linenum = 0;
	char line[1024];
	tsol_tpent_t *file_tpentp;
	tsol_tpstr_t tpstr;
	int err;
	char *errstr;
	char buf[NSS_BUFLEN_TSOL_TP];

	if (!tp || !tp->name || !strlen(tp->name)) {
		return (SEC_REP_INVALID_ARG);
	}

	if ((tsolrhtp = fopen(TNRHTP_PATH, "r")) == NULL) {
		return (EX_UPDATE);
	}

	if (fstat(fileno(tsolrhtp), &sb) < 0) {
		/* If we can't get mode, take a default */
		sb.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	}

	o_mask = umask(077);
	tmprhtp = fopen(TNRHTP_TMP, "w+");
	(void) umask(o_mask);

	if (tmprhtp == NULL) {
		(void) fclose(tsolrhtp);
		return (EX_UPDATE);
	}

	if (fchmod(fileno(tmprhtp), sb.st_mode) != 0 ||
	    fchown(fileno(tmprhtp), sb.st_uid, sb.st_gid) != 0 ||
	    lockf(fileno(tmprhtp), F_LOCK, 0) != 0) {
		(void) fclose(tsolrhtp);
		(void) fclose(tmprhtp);
		(void) unlink(TNRHTP_TMP);
		return (EX_UPDATE);
	}

	while (fgets(line, NSS_BUFLEN_TSOL_TP, tsolrhtp) != NULL &&
	    !haserr) {
		linenum++;

		if (line[0] == '#' ||line[0] == '\n') {
			(void) fputs(line, tmprhtp);
			continue;
		}
		(void) str_to_tpstr(line, strlen(line), &tpstr, buf,
		    sizeof (buf));
		file_tpentp = tpstr_to_ent(&tpstr, &err, &errstr);
		if (file_tpentp == NULL) {
			if (err == LTSNET_EMPTY) {
				(void) fputs(line, tmprhtp);
				continue;
			}
			(void) fprintf(stderr,
			    gettext("line %1$d: %2$s: %.32s\n"), linenum,
			    tsol_strerror(err, errno), errstr);
			break;
		}

		if (file_tpentp->name && strlen(file_tpentp->name) &&
		    (strcmp(file_tpentp->name, tp->name) == 0)) {
			if (flags & ADD_MASK) {
				const char *err_msg = gettext("ERROR: "
				    "Duplicate entry in tnrhtp file"
				    " at line %d.\n");
				(void) fprintf(stderr, err_msg, linenum);
				break;
			}
			if (flags & DEL_MASK) {
				deleted = 1;
				continue;
			}
			if (flags & MOD_MASK) {
				fprint_tnrhdb(tmprhtp, tp);
				modified = 1;
				continue;
			}
		}
		(void) fputs(line, tmprhtp);
	}

	haserr = !feof(tsolrhtp);
	if (haserr) {
		const char *err_msg = gettext("ERROR: Syntax error in tnrhtp "
		    "file at line %d.\n");
		(void) fprintf(stderr, err_msg, linenum);
	}

	if (fclose(tsolrhtp) != 0 || haserr) {
		(void) fclose(tmprhtp);
		(void) unlink(TNRHTP_TMP);
		return (EX_UPDATE);
	}

	if (flags & ADD_MASK) {
		fprint_tnrhdb(tmprhtp, tp);
		added = 1;
	}
	(void) fclose(tmprhtp);
	if (added || modified || deleted) {
		if (rename(TNRHTP_TMP, TNRHTP_PATH) < 0) {
			(void) unlink(TNRHTP_TMP);
			return (EX_UPDATE);
		}
	} else {
		(void) unlink(TNRHTP_TMP);
		return (EX_NAME_NOT_EXIST);
	}


	return (EX_SUCCESS);
}

int
get_tnrhtp(char *backend, const char *name, tsol_tpent_t **result,
    nss_XbyY_buf_t *b)
{
	tsol_tpstr_t *tpstr = NULL;
	int rc;
	int err = 0;
	char *errstr = NULL;

	rc = get_db_ent(SEC_REP_DB_TNRHTP, backend, -1, (char *)name,
	    (void**) &tpstr, b);

	if (rc == SEC_REP_SUCCESS) {
		*result = (tsol_tpent_t *)tpstr_to_ent(tpstr, &err, &errstr);
		if (*result == NULL)
			return (SEC_REP_NOT_FOUND);
	}
	return (rc);
}

int
files_get_tnrhtp(const char *name, tsol_tpent_t **result, nss_XbyY_buf_t *b)
{
	return (get_tnrhtp(files_backend, name, result, b));
}


void
files_set_rhent(int stay)
{
	tsol_rh_stayopen |= stay;
	set_ent(SEC_REP_DB_TNRHDB, files_backend);
}

void
end_rhent(void)
{
	tsol_rh_stayopen = 0;
	end_ent();
}

tsol_rhent_t *
get_rhent(nss_XbyY_buf_t *b)
{
	tsol_rhstr_t	*rhstrp;
	int	err = 0;
	char	*errstr = NULL;

	rhstrp = (tsol_rhstr_t *)get_ent(b);

	if (rhstrp == NULL)
		return (NULL);

	return (rhstr_to_ent(rhstrp, &err, &errstr));
}

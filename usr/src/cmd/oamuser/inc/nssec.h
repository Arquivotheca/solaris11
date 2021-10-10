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

#ifndef _NSSEC_H
#define	_NSSEC_H

#ifdef	__cplusplus
extern "C" {
#endif
#include <pwd.h>
#include <shadow.h>
#include <user_attr.h>
#include <grp.h>
#include <project.h>
#include <prof_attr.h>
#include <exec_attr.h>
#include <auth_attr.h>
#include <nss_dbdefs.h>
#include <ns_sldap.h>
#include <secdb.h>
#include <sys/tsol/tndb.h>

#define	NSS_REP_FILES	"files"
#define	NSS_REP_LDAP	"ldap"
#define	NSS_REP_COMPAT	"compat"
#define	AUTOHOME	"auto_home"

#define	SEC_REP_FILES	0x0001
#define	SEC_REP_LDAP	0x0002
#define	SEC_REP_NIS	0x0003
#define	SEC_REP_NSS	0x0004
#define	SEC_REP_LAST	SEC_REP_NSS
#define	SEC_REP_NOREP	0x0008

/*
 * Error / return codes
 */
#define	SEC_REP_SUCCESS		0	/* update succeeded */
#define	SEC_REP_BUSY		-1	/* Password database busy */
#define	SEC_REP_STAT_FAILED	-2	/* stat of password file failed */
#define	SEC_REP_OPEN_FAILED	-3	/* password file open failed */
#define	SEC_REP_WRITE_FAILED	-4	/* can't write to password file */
#define	SEC_REP_CLOSE_FAILED	-5	/* close returned error */
#define	SEC_REP_NOT_FOUND	-6	/* user not found in database */
#define	SEC_REP_UPDATE_FAILED	-7	/* couldn't update password file */
#define	SEC_REP_NOMEM		-8	/* Not enough memory */
#define	SEC_REP_SERVER_ERROR	-9	/* server errors */
#define	SEC_REP_SYSTEM_ERROR	-10	/* local configuration problem */
#define	SEC_REP_DENIED		-11	/* update denied */
#define	SEC_REP_NO_CHANGE	-12 	/* Data hasn't changed */
#define	SEC_REP_CALL_ERROR	-13 	/* Cannot call repository */
#define	SEC_REP_INVALID_ARG	-14 	/* invalid args passed */
#define	SEC_REP_OP_NOT_SUPPORTED	-15	/* operation not supported */

/* Masks for the optn_mask variable for grp cmds */
#define	GRP_N_MASK	0x10

/* Masks for the optn_mask variable for auth cmd */
#define	AUTH_ADD_MASK	0x1
#define	AUTH_MOD_MASK	0x2
#define	AUTH_DEL_MASK	0x4

/* Masks for the optn_mask variable for user, profiles, tncfg cmds */
#define	ADD_MASK	0x1
#define	MOD_MASK	0x2
#define	DEL_MASK	0x4

typedef enum {
	SEC_REP_DB_PASSWD = 1,
	SEC_REP_DB_SHADOW,
	SEC_REP_DB_USERATTR,
	SEC_REP_DB_GROUP,
	SEC_REP_DB_PROJECT,
	SEC_REP_DB_PROFATTR,
	SEC_REP_DB_EXECATTR,
	SEC_REP_DB_AUTHATTR,
	SEC_REP_DB_AUTOMOUNT,
	SEC_REP_DB_TNRHDB,
	SEC_REP_DB_TNRHTP
} sec_db_names_type;

struct group_entry {
	gid_t	gid;
	char	*group_name;
};

typedef struct sec_repops {
	/* passwd ops */
	int (*get_pwnam)(char *, struct passwd **, nss_XbyY_buf_t *);
	int (*get_pwid)(uid_t, struct passwd **, nss_XbyY_buf_t *);
	struct passwd *(*get_pwent)(nss_XbyY_buf_t *);
	void (*set_pwent)();
	void (*end_pwent)();

	/* shadow ops */
	int (*get_spnam)(char *, struct spwd **, nss_XbyY_buf_t *);
	struct spwd *(*get_spent)(nss_XbyY_buf_t *);
	void (*set_spent)();
	void (*end_spent)();

	/* user_attr ops */
	int (*get_usernam)(char *, userattr_t **, nss_XbyY_buf_t *);
	int (*get_useruid)(uid_t, userattr_t **, nss_XbyY_buf_t *);
	userattr_t *(*get_userattr)(nss_XbyY_buf_t *);
	void (*set_userattr)();
	void (*end_userattr)();

	/* group ops */
	int (*get_grnam)(char *, struct group **, nss_XbyY_buf_t *);
	int (*get_grid)(gid_t, struct group **, nss_XbyY_buf_t *);
	int (*put_group)(struct group *, char *, int);
	struct group *(*get_group)(nss_XbyY_buf_t *);
	void (*set_group)();
	void (*end_group)();

	/* project ops */
	int (*get_projnam)(char *, struct project **, nss_XbyY_buf_t *);
	int (*get_projid)(projid_t, struct project **, nss_XbyY_buf_t *);
	int (*put_project)(struct project *, int);
	struct project *(*get_project)(nss_XbyY_buf_t *);
	void (*set_project)();
	void (*end_project)();

	/* prof_attr ops */
	int (*get_profnam)(char *, profattr_t **, nss_XbyY_buf_t *);
	int (*put_profattr)(profattr_t *, int);
	profattr_t *(*get_profattr)(nss_XbyY_buf_t *);
	void (*set_profattr)();
	void (*end_profattr)();

	/* exec_attr ops */
	execattr_t *(*get_execprof)(char *, char *, char *, int,
	    nss_XbyY_buf_t *);
	execattr_t *(*get_execuser)(char *, char *, char *, int,
	    nss_XbyY_buf_t *);
	int (*put_execattr)(execattr_t *, int);
	execattr_t *(*get_execattr)(nss_XbyY_buf_t *);
	void (*set_execattr)();
	void (*end_execattr)();

	/* auth_attr ops */
	int (*get_authnam)(char *, authattr_t **, nss_XbyY_buf_t *);
	int (*put_authattr)(authattr_t *, int);
	authattr_t *(*get_authattr)(nss_XbyY_buf_t *);
	void (*set_authattr)();
	void (*end_authattr)();

	/* edit multiple groups, add user/change existing user logname. */
	int (*edit_groups)(char *, char *, struct group_entry **, int);
	/* edit multiple projects, add user/ change existing user logname. */
	int (*edit_projects)(char *, char *, projid_t *, int);
	int (*edit_autohome)(char *, char *, char *, int);
	int (*get_autohome)(char *, char *);

	/* tnrhdb ops */
	int (*put_tnrhdb)(tsol_rhent_t *, int);
	void (*set_rhent)(int);
	tsol_rhent_t *(*get_rhent)(nss_XbyY_buf_t *);
	void (*end_rhent)(void);

	/* tnrhtp ops */
	int (*get_tnrhtp)(const char *, tsol_tpent_t **, nss_XbyY_buf_t *);
	int (*put_tnrhtp)(tsol_tpent_t *, int);

} sec_repops_t;

typedef struct {
	char *database;
	uint_t db_struct_size;
	uint_t db_nss_buf_len;
	nss_str2ent_t conv_func;
	int db_op_bynam;
	int db_op_byid;
} nss_sec_conv_t;

typedef struct {
	int type;
	sec_repops_t *rops;
} sec_repository_t;

struct tsd_data {
	uint_t dbname;
	char *source;
};

extern int get_repository_handle(char *, sec_repository_t **);
extern char *get_autohome_db();
extern void init_nss_buffer(int, nss_XbyY_buf_t **);
extern void free_nss_buffer(nss_XbyY_buf_t **);
extern void free_repository_handle(sec_repository_t *);

extern sec_repops_t sec_files_rops;
extern sec_repops_t sec_nss_rops;
extern sec_repops_t sec_ldap_rops;
extern void check_ldap_rc(int, ns_ldap_error_t *);
extern void make_attr_string(kv_t *, int, char *, int);
extern void fprint_attr(FILE *out, kva_t *attr);
extern int find_in_nss(int, char *, sec_repository_t **);

#ifdef	__cplusplus
}
#endif

#endif	/* _NSSEC_H */

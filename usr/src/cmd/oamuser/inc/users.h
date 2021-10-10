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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#ifndef _USERS_H
#define	_USERS_H

#include <sys/stat.h>
#include "nssec.h"


/* max number of projects that can be specified when adding a user */
#define	NPROJECTS_MAX	1024

/* validation returns */
#define	NOTUNIQUE	0	/* not unique */
#define	RESERVED	1	/* reserved */
#define	UNIQUE		2	/* is unique */
#define	TOOBIG		3	/* number too big */
#define	INVALID		4

/*
 * Note: constraints checking for warning (release 2.6),
 * and these may be enforced in the future releases.
 */
#define	WARN_NAME_TOO_LONG	0x1
#define	WARN_BAD_GROUP_NAME	0x2
#define	WARN_BAD_LOGNAME_CHAR	0x4
#define	WARN_BAD_LOGNAME_FIRST	0x8
#define	WARN_NO_LOWERCHAR	0x10
#define	WARN_BAD_PROJ_NAME	0x20
#define	WARN_LOGGED_IN		0x40

/* Exit codes from passmgmt */
#define	PEX_SUCCESS	0
#define	PEX_NO_PERM	1
#define	PEX_SYNTAX	2
#define	PEX_BADARG	3
#define	PEX_BADUID	4
#define	PEX_HOSED_FILES	5
#define	PEX_FAILED	6
#define	PEX_MISSING	7
#define	PEX_BUSY	8
#define	PEX_BADNAME	9
/* codes match user/role cmds codes. */
#define	PEX_NO_AUTH	18
#define	PEX_NO_ROLE	19
#define	PEX_NO_PROFILE	20
#define	PEX_NO_PRIV	21
#define	PEX_NO_LABEL	22
#define	PEX_NO_GROUP	23
#define	PEX_NO_SYSLABEL	24
#define	PEX_NO_PROJECT	25

#define	REL_PATH(x)	(x && *x != '/')

/*
 * list Operation characters
 */
#define	OP_ADD_CHAR		'+'	/* Add operation */
#define	OP_SUBTRACT_CHAR	'-'	/* subtract operation */
#define	OP_REPLACE_CHAR		' '	/* Replace operation */

/*
 * interfaces available from the library
 */
extern int isalldigit(char *);
extern int valid_gname(char *, struct group **, int *);
extern int valid_project_check(char *, struct project **, int *,
    sec_repository_t *, nss_XbyY_buf_t *);
extern void warningmsg(int, char *);
extern void putgrent(struct group *, FILE *);
extern int check_perm(struct stat, uid_t, gid_t, mode_t);
extern int valid_expire(char *string, time_t *expire);
extern int valid_group_id(gid_t, struct group **, sec_repository_t *,
    nss_XbyY_buf_t *);
extern int valid_group_check(char *, struct group **, int *,
    sec_repository_t *, nss_XbyY_buf_t *);
extern int valid_login_check(char *, struct passwd **, int *,
    sec_repository_t *, nss_XbyY_buf_t *);
extern int valid_uid_check(uid_t, struct passwd **, sec_repository_t *,
    nss_XbyY_buf_t *);
extern int valid_group_name(char *, struct group **, int *, sec_repository_t *,
    nss_XbyY_buf_t *);
extern int check_groups_for_user(uid_t, struct group_entry **);
extern int valid_proj_byid(projid_t, struct project **, sec_repository_t *,
    nss_XbyY_buf_t *);
extern int valid_proj_byname(char *, struct project **, int *,
    sec_repository_t *, nss_XbyY_buf_t *, void *, size_t);
extern int execute_cmd_str(char *, char *, int);
extern char *check_users(char *, sec_repository_t *, nss_XbyY_buf_t *, char **);
extern int group_authorize(char *, char *, boolean_t);
extern char *appendedauth(char *);
extern char *attr_add(char *, char *, char *, int, char *);
extern char *attr_remove(char *, char *, char *, int, char *);
extern boolean_t list_contains(char **, char *);

/* passmgmt */
#define	PASSMGMT	"/usr/lib/passmgmt";
#endif	/* _USERS_H */

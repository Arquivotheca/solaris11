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

#ifndef _PASSMGMT_H
#define	_PASSMGMT_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <ns_sldap.h>
#include <nssec.h>

#define	CMT_SIZE	(128+1)	/* Argument sizes + 1 (for '\0') */
#define	DIR_SIZE	(256+1)
#define	SHL_SIZE	(256+1)
#define	ENTRY_LENGTH	512	/* Max length of an /etc/passwd entry */
#define	UID_MIN		100	/* Lower bound of default UID */

#define	M_MASK		01	/* Masks for the optn_mask variable */
#define	L_MASK		02	/* It keeps track of which options   */
#define	C_MASK		04	/* have been entered */
#define	H_MASK		010
#define	U_MASK		020
#define	G_MASK		040
#define	S_MASK		0100
#define	O_MASK		0200
#define	A_MASK		0400
#define	D_MASK		01000
#define	F_MASK		02000
#define	E_MASK		04000

#define	UATTR_MASK	010000

/* flags for info_mask */
#define	LOGNAME_EXIST	01		/* logname exists */
#define	BOTH_FILES	02		/* touch both password files */
#define	WRITE_P_ENTRY	04		/* write out password entry */
#define	WRITE_S_ENTRY	010		/* write out shadow entry */
#define	NEED_DEF_UID	020		/* need default uid */
#define	FOUND		040		/* found the entry in password file */
#define	LOCKED		0100		/* did we lock the password file */
#define	UATTR_FILE	0200		/* touch user_attr file */
#define	BAD_ENT_MESSAGE	"%s: Bad entry found in /etc/passwd.  Run pwconv.\n"
#define	NO_REP_HANDLE	"%s: Could not get repository handle.\n"
#define	FILES_SCOPE	0
#define	LDAP_SCOPE	1

#define	DATMSK "DATEMSK=/etc/datemsk"
#define	OUSERATTR_FILENAME	"/etc/ouser_attr"
#define	USERATTR_TEMP		"/etc/uatmp"
#define	UID_MESSAGE	"Cannot modify uid, user with uid already exists"
#define	UID_RESERVED	"Cannot modify account, this is a reserved account."
#define	ADD_OPERATION	"add"
#define	MOD_OPERATION	"modify"
#define	DEL_OPERATION	"delete"

typedef struct kvopts_auth {
    const char option;
    const char *key;
    char *newvalue;
    int (*check_auth)(char *, char *);
    char *auth_name;
} kvopts_auth_t;

extern int check_authorizations(char *, char *);
extern int check_profiles_auths(char *, char *);
extern int check_roles_auths(char *, char *);
extern int check_project_auths(char *, char *);
extern int check_limitpriv_auths(char *, char *);
extern int check_defaultpriv_auths(char *, char *);
extern int check_minlabel(char *, char *);
extern int check_clearance(char *, char *);
extern int check_type_auths(char *, char *);
extern int check_auths(struct passwd *, struct spwd *, userattr_t *);
extern void check_ldap_rc(int, ns_ldap_error_t *);
extern int create_repository_handle(char *backend, sec_repository_t **rep);
extern void bad_usage(char *);
extern int check_roleauth(char *, char *);


#ifdef	__cplusplus
}
#endif

#endif	/* _PASSMGMT_H */

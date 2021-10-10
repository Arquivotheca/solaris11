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


#ifndef	_FUNCS_H
#define	_FUNCS_H
#include <nssec.h>
#ifdef	__cplusplus
extern "C" {
#endif

#define	CMD_PREFIX_USER	"user"

#define	AUTH_SEP	","
#define	PROF_SEP	","
#define	ROLE_SEP	","

#define	MAX_TYPE_LENGTH	64

extern char *getusertype(char *cmdname);
extern int is_role(char *usertype);
extern void change_key(const char *, char *, char *);
extern void addkey_args(char **, int *);
extern char *getsetdefval(const char *, char *);
extern void process_change_key(sec_repository_t *);
extern int process_add_remove(userattr_t *);
extern void free_add_remove();

extern int nkeys;

#ifdef	__cplusplus
}
#endif

#endif	/* _FUNCS_H */

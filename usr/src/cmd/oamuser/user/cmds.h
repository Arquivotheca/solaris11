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


#ifndef	_CMDS_H
#define	_CMDS_H

#include "nssec.h"

#ifdef	__cplusplus
extern "C" {
#endif

extern struct userdefs *getusrdef();
extern void dispusrdef();
extern uid_t find_next_avail_uid(sec_repository_t *, nss_XbyY_buf_t *);
extern int putusrdef(struct userdefs *, char *);
extern int call_passmgmt(char *[]);
extern struct group_entry **valid_lgroup(char *, gid_t, sec_repository_t *,
    nss_XbyY_buf_t *);
extern projid_t *valid_lproject(char *, sec_repository_t *, nss_XbyY_buf_t *);
extern int update_def(struct userdefs *);
extern void import_def(struct userdefs *);
extern int valid_dir_input(char *, char **);
extern int isbusy(char *);
extern int create_home_dir(char *, char *, char *, uid_t, gid_t);
extern int move_dir(char *, char *, char *, int);
extern int check_user_authorized();
extern int try_remount(char *, char *);
extern int rm_files(char *);
extern int remove_home_dir(char *, uid_t, gid_t, sec_repository_t *);
extern int update_gids(struct group_entry **, char *, char);

#ifdef	__cplusplus
}
#endif

#endif	/* _CMDS_H */

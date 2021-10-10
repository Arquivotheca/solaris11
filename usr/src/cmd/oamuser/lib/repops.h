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

#ifndef _REPOPS_H
#define	_REPOPS_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <thread.h>
#include <sys/tsol/tndb.h>
#include <sys/tsol/label.h>
struct nss_calls {
	nss_db_initf_t initf_nss_exec;
	nss_db_initf_t initf_nsw_prof;
	nss_db_initf_t initf_nsw_exec;
	char ** pnsw_search_path;
};
extern execattr_t *get_execprof(char *, char *, char *, int, nss_XbyY_buf_t *,
	struct nss_calls *);
extern int files_edit_groups(char *, char *, struct group_entry **, int);
extern int files_edit_projects(char *, char *, projid_t *, int);
extern thread_key_t ns_dbname;
extern int str2passwd(const char *, int, void *, char *, int);
extern int str2spwd(const char *, int, void *, char *, int);
extern int _str2userattr(const char *, int, void *, char *, int);
extern int str2group(const char *, int, void *, char *, int);
extern int _str2project(const char *, int, void *, char *, int);
extern int _str2profattr(const char *, int, void *, char *, int);
extern int _str2execattr(const char *, int, void *, char *, int);
extern int _str2authattr(const char *, int, void *, char *, int);
extern char *_strtok_escape(char *, char *, char **);
extern int nss_set_tsd(int, char *);
extern userattr_t *_userstr2attr(userstr_t *);
extern profattr_t *_profstr2attr(profstr_t *);
extern authattr_t *_authstr2attr(authstr_t *);
extern execattr_t *_execstr2attr(execstr_t *, execstr_t *);
extern void putgrent(struct group *, FILE *);
extern void putprojent(struct project *, FILE *);
extern void _nss_db_state_destr(struct nss_db_state *);
extern void end_ent();
extern void set_ent(int, char *);
extern struct spwd *get_spent(nss_XbyY_buf_t *);
extern struct passwd *get_pwent(nss_XbyY_buf_t *);
extern userattr_t *get_userattr(nss_XbyY_buf_t *);
extern int get_usernam(char *, char *, userattr_t **, nss_XbyY_buf_t *);
extern struct group *get_group(nss_XbyY_buf_t *);
extern struct project *get_project(nss_XbyY_buf_t *);
extern profattr_t *get_profattr(nss_XbyY_buf_t *);
extern int get_profnam(char *, char *, profattr_t **, nss_XbyY_buf_t *);
extern authattr_t *get_authattr(nss_XbyY_buf_t *);
extern int get_authnam(char *, char *, authattr_t **, nss_XbyY_buf_t *);
extern execattr_t *get_execattr(nss_XbyY_buf_t *);
extern int get_db_ent(int, char *, int, char *, void **, nss_XbyY_buf_t *);
extern void translate_inet_addr(tsol_rhent_t *, int *, char [], int);
extern tsol_rhent_t *get_rhent(nss_XbyY_buf_t *);
extern void end_rhent(void);
extern int get_tnrhtp(char *, const char *, tsol_tpent_t **, nss_XbyY_buf_t *);
extern void l_to_str(const m_label_t *, char **, int);


#ifdef	__cplusplus
}
#endif

#endif	/* _REPOPS_H */

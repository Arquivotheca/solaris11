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
 * Copyright (c) 2003, 2006, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_UCRED_H_
#define	_UCRED_H_

#include <sys/types.h>
#include <sys/priv.h>
#include <sys/tsol/label.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct ucred_s ucred_t;

/*
 * library functions prototype.
 */
#if	defined(__STDC__)

extern ucred_t *ucred_get(pid_t pid);

extern void ucred_free(ucred_t *);

extern uid_t ucred_geteuid(const ucred_t *);
extern uid_t ucred_getruid(const ucred_t *);
extern uid_t ucred_getsuid(const ucred_t *);
extern gid_t ucred_getegid(const ucred_t *);
extern gid_t ucred_getrgid(const ucred_t *);
extern gid_t ucred_getsgid(const ucred_t *);
extern int   ucred_getgroups(const ucred_t *, const gid_t **);

extern const priv_set_t *ucred_getprivset(const ucred_t *, priv_ptype_t);
extern uint_t ucred_getpflags(const ucred_t *, uint_t);

extern pid_t ucred_getpid(const ucred_t *); /* for door_cred compatibility */

extern size_t ucred_size(void);

extern int getpeerucred(int, ucred_t **);

extern zoneid_t ucred_getzoneid(const ucred_t *);

extern bslabel_t *ucred_getlabel(const ucred_t *);

extern projid_t ucred_getprojid(const ucred_t *);

#else	/* Non ANSI */

extern ucred_t *ucred_get(/* pid_t pid */);

extern void ucred_free(/* ucred_t * */);

extern uid_t ucred_geteuid(/* ucred_t * */);
extern uid_t ucred_getruid(/* ucred_t * */);
extern uid_t ucred_getsuid(/* ucred_t * */);
extern gid_t ucred_getegid(/* ucred_t * */);
extern gid_t ucred_getrgid(/* ucred_t * */);
extern gid_t ucred_getsgid(/* ucred_t * */);
extern int   ucred_getgroups(/* ucred_t *, gid_t ** */);

extern priv_set_t *ucred_getprivset(/* ucred_t *, priv_ptype_t */);
extern uint_t ucred_getpflags(/* ucred_t *, uint_t */);

extern pid_t ucred_getpid(/* ucred_t * */);

extern size_t ucred_size(/* void */);

extern int getpeerucred(/* int, ucred_t ** */);

extern zoneid_t ucred_getzoneid(/* ucred_t * */);

extern bslabel_t *ucred_getlabel(/* const ucred_t * */);

extern projid_t ucred_getprojid(/* ucred_t * */);

#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _UCRED_H_ */

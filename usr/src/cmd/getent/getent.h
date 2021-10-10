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
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_GETENT_H
#define	_GETENT_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <secdb.h>
#include <libsocket_priv.h>

#define	TRUE	1
#define	FALSE	0

#define	EXC_SUCCESS		0
#define	EXC_SYNTAX		1
#define	EXC_NAME_NOT_FOUND	2
#define	EXC_ENUM_NOT_SUPPORTED	3

extern int dogetpw(const char **);
extern int dogetgr(const char **);
extern int dogethost(const char **);
extern int dogetipnodes(const char **);
extern int dogetserv(const char **);
extern int dogetnet(const char **);
extern int dogetproto(const char **);
extern int dogetethers(const char **);
extern int dogetnetmask(const char **);
extern int dogetproject(const char **);
extern int dogetuserattr(const char **);
extern int dogetprofattr(const char **);
extern int dogetexecattr(const char **);
extern int dogetauthattr(const char **);
extern void putkeyvalue(kva_t *, FILE *);

#ifdef	__cplusplus
}
#endif

#endif	/* _GETENT_H */

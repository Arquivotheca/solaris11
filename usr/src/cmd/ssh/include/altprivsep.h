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
 *
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_ALTPRIVSEP_H
#define	_ALTPRIVSEP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include "auth.h"
#include "kex.h"

#define	APS_MSG_NEWKEYS_REQ	0
#define	APS_MSG_NEWKEYS_REP	1
#define	APS_MSG_RECORD_LOGIN	2
#define	APS_MSG_RECORD_LOGOUT	3
#define	APS_MSG_START_REKEX	4
#define	APS_MSG_AUTH_CONTEXT	5

void	altprivsep_start_and_do_monitor(int use_engine, int inetd, int newsock,
		int statup_pipe);
int	altprivsep_get_pipe_fd(void);
pid_t	altprivsep_get_child_pid(void);
void	altprivsep_set_socket(int socket);
int	altprivsep_get_socket(void);

/* child-side handler of re-key packets */
void	altprivsep_rekey(int type, u_int32_t seq, void *ctxt);

/* Calls _to_ monitor from unprivileged process */
void	altprivsep_process_input(fd_set *rset);
void	altprivsep_record_login(pid_t pid, const char *ttyname);
void	altprivsep_record_logout(pid_t pid);
void	altprivsep_start_rekex(void);
void	altprivsep_send_auth_context(Authctxt *authctxt);

/* Functions for use in the monitor */
void	aps_input_altpriv_msg(int type, u_int32_t seq, void *ctxt);

#ifdef __cplusplus
}
#endif

#endif /* _ALTPRIVSEP_H */

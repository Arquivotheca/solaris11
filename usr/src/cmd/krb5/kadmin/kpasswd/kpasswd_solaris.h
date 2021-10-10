/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
 */
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
 * kadmin/passwd/kpasswd.h
 *
 * Copyright 2001 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 *
 * Prototypes for the kpasswd program callback functions.
 */

#ifndef __KPASSWD_SOLARIS_H__
#define __KPASSWD_SOLARIS_H__

int change_kpassword_solaris(krb5_context context,
                           char *whoami,
                           krb5_principal princ);

long read_old_password(krb5_context context,
                       char *password, 
                       unsigned int *pwsize);

long read_new_password(void *server_handle,
                       char *password, 
                       unsigned int *pwsize,
                       char *msg_ret, 
                       int msg_len,
                       krb5_principal princ);

void display_intro_message(const char *whoami,
                           const char *fmt_string,
                           const char *arg_string);

#endif /* __KPASSWD_SOLARIS_H__ */



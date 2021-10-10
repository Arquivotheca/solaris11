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
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/


#ifndef	_CRYPT_H
#define	_CRYPT_H

#include <pwd.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* Password and file encryption functions */

#define	CRYPT_MAXCIPHERTEXTLEN	512

#if defined(__STDC__)
extern char *crypt(const char *, const char *);
extern char *crypt_gensalt(const char *, const struct passwd *);
extern char *crypt_genhash_impl(char *, size_t, const char *,
    const char *, const char **);
extern char *crypt_gensalt_impl(char *, size_t, const char *,
    const struct passwd *, const char **);
extern char *des_crypt(const char *, const char *);
extern void des_encrypt(char *, int);
extern void des_setkey(const char *);
extern void encrypt(char *, int);
#else
extern char *crypt();
extern char *crypt_gensalt();
extern char *crypt_genhash_impl();
extern char *crytp_gensalt_impl();
extern char *des_crypt();
extern void des_encrypt();
extern void des_setkey();
extern void encrypt();
extern void setkey();
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _CRYPT_H */

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

#ifndef _ASR_SSL_H
#define	_ASR_SSL_H

#ifdef	__cplusplus
extern "C"
{
#endif

#include <openssl/ssl.h>

#define	ASR_SSL_EXPONENT	0x10001L	/* RSA keygen exponent */
#define	ASR_SSL_AES_KEYLEN	16

/* RSA key functions */
extern RSA *asr_ssl_rsa_keygen(size_t keylen);
extern char *asr_ssl_rsa_private_pem(RSA *rsa);
extern char *asr_ssl_rsa_public_pem(RSA *rsa);

/* Message signing functions */
char *asr_ssl_sign_pre(char *privkey,
    const unsigned char *prefix, unsigned int prelen,
    const unsigned char *msg, unsigned int msglen,
    unsigned int *siglen);
extern char *asr_ssl_sign(char *privkey, const unsigned char *msg,
    unsigned int msglen, unsigned int *siglen);
extern char *asr_ssl_sign64(char *privkey,
    const char *msg, unsigned int msglen);

/* AES encrypt functions */
int asr_ssl_write_aes_config_names(
    char *key_path, char *data_path, nvlist_t *nvl, const char *filter);

int asr_ssl_read_aes(char *key_path, char *data_path, char **data, int *dlen);
int asr_ssl_read_aes_nvl(char *key_path, char *data_path, nvlist_t **outlist);

#ifdef	__cplusplus
}
#endif

#endif	/* _ASR_SSL_H */

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

#ifndef	_KMF_H
#define	_KMF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <kmfapi.h>
#include "key.h"

/*
 * Gets length and data of a certificate stored in a Key structure. Makes the
 * code easier to read.
 */
#define	CERT_LEN(k) (k->kmf_key->cert.certificate.Length)
#define	CERT_DATA(k) (k->kmf_key->cert.certificate.Data)

/* See key_load_public() for more information. */
#define	SSH_KMF_IGNORE_CERT		0
#define	SSH_KMF_LOAD_CERT		1

/* See key_load_private() for more information. */
#define	SSH_KMF_DONT_ASK_FOR_TOKEN_PIN	0
#define	SSH_KMF_ASK_FOR_TOKEN_PIN	1

/*
 * Set of common return codes for new SSH functions added to implement the X.509
 * part of the pubkey method. We may also use these codes from functions that
 * worked only with pubkeys before. In such case, we added a new output argument
 * and the code was assigned to it.
 */
#define	SSH_KMF_NOT_PKCS11_URI		0
#define	SSH_KMF_SELF_SIGNED_CERT	1
#define	SSH_KMF_NOT_SELF_SIGNED_CERT	2
#define	SSH_KMF_CORRUPTED_CERT		3
#define	SSH_KMF_CERT_VALIDATION_ERROR	4
#define	SSH_KMF_CERT_VALIDATED		5
#define	SSH_KMF_MISSING_TA		6
#define	SSH_KMF_NOT_CERT		7
#define	SSH_KMF_CERT_FOUND		8
#define	SSH_KMF_INVALID_CERT		9
#define	SSH_KMF_CERT_BUT_NO_KMF_SESS	10
#define	SSH_KMF_PRIV_KEY_OK		11
#define	SSH_KMF_INVALID_PKCS11_URI	12
#define	SSH_KMF_PKCS11_URI_OK		13
#define	SSH_KMF_MISSING_TOKEN_OR_OBJ	14
#define	SSH_KMF_CANNOT_LOAD_OBJECT	15
#define	SSH_KMF_CANNOT_LOAD_PRIV_KEY	16
#define	SSH_KMF_USE_KNOWN_HOSTS		17

const char *ssh_pk11_err(int);

int ssh_kmf_check_uri(const char *, pkcs11_uri_t **);
void ssh_kmf_error(KMF_HANDLE_T, char *, KMF_RETURN);
void ssh_kmf_debug(KMF_HANDLE_T, char *, KMF_RETURN);

int ssh_kmf_init(char *, char *, char *, kmf_key_t *);
int ssh_kmf_configure_keystore(KMF_HANDLE_T, char *);

void ssh_set_x509_key_type(Key *);

int ssh_kmf_key_load_private(const char *, Key **, int, char *);
Key *ssh_kmf_load_certificate(Key *, pkcs11_uri_t *, char *);
int ssh_kmf_key_from_blob(uchar_t *, int, Key **);
Key *ssh_kmf_get_x509_key(pkcs11_uri_t *, const char *, int, char *);

int ssh_kmf_sign_data(Key *, uchar_t **, uint_t *, uchar_t *, uint_t);
int ssh_kmf_verify_data(Key *, uchar_t *, uint_t, uchar_t *, uint_t);
int ssh_kmf_validate_cert(kmf_key_t *, char *);
int ssh_kmf_compare_certs(const Key *, const Key *);
int ssh_kmf_is_cert_self_signed(KMF_HANDLE_T, KMF_DATA *, char **, char **);

#ifdef __cplusplus
}
#endif

#endif /* _KMF_H */

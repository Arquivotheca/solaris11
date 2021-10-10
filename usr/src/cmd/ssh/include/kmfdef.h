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

#ifndef	_KMFDEF_H
#define	_KMFDEF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <kmfapi.h>
#include <cryptoutil.h>

/*
 * A structure containing all needed information on the X.509 host key or the
 * X.509 user identity, including its private key handle, certificate data, a
 * parsed PKCS#11 URI, and a KMF session.
 */
typedef struct kmf_key {
	/*
	 * KMF session handle. Each key has its own one since each key can be
	 * stored in a different keystore.
	 */
	KMF_HANDLE_T		h;
	/*
	 * URI in a string, exactly the same as set in a config file or on a
	 * command line.
	 */
	char			*uri_str;
	/* Parsed URI. */
	pkcs11_uri_t		*pkcs11_uri;
	/*
	 * PIN. If read from the terminal when accessing the private key, we
	 * need to store it here for ssh-add which sends it over to ssh-agent.
	 */
	char			*pin;
	/* KMF private key handle. */
	KMF_KEY_HANDLE		privh;
	/* X.509 certificate, corresponding to the private key. */
	KMF_X509_DER_CERT	cert;
	/*
	 * If the certificate is read from a blob we can not call
	 * kmf_free_kmf_cer() since the plugin pointer is zero the right
	 * function would not be called at all.
	 */
	int			from_blob;
} kmf_key_t;

/*
 * This is mostly needed when validating the certificate of the peer. It is
 * enough to have one KMF session for that. Note that every host key or user
 * identify has its own KMF session since every key can be stored in a different
 * PKCS#11 token and one KMF session supports only one. If not used for a
 * certificate validation the global KMF session can be also used for extracting
 * certificate data from a blob. For example, ssh-agent gets a certificate from
 * the ssh-add command in order to remove a private key and needs a KMF handle
 * to process the received certificate even though it will not be validating it.
 *
 * See also a comment in key_free().
 */
extern KMF_HANDLE_T	kmf_global_handle;
extern char		*kmf_ta_keystore;

#ifdef __cplusplus
}
#endif

#endif /* _KMFDEF_H */

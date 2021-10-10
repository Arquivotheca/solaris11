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

#include <assert.h>
#include <openssl/x509.h>
#include "ssh.h"
#include "log.h"
#include "xmalloc.h"
#include "kmf.h"
#include "kmf_openssl.h"

/*
 * Set common attributes needed for data verification using a certificate in a
 * file.
 */
int
kmf_openssl_set_verify_attrs(KMF_ATTRIBUTE *attrlist, KMF_KEYSTORE_TYPE *pktype)
{
	int num = 0;

	*pktype = KMF_KEYSTORE_OPENSSL;
	kmf_set_attr_at_index(attrlist, num++, KMF_KEYSTORE_TYPE_ATTR,
	    pktype, sizeof (KMF_KEYSTORE_TYPE));

	return (num);
}

/*
 * Set attributes needed for validating a certificate stored in file.
 */
int
kmf_openssl_set_validate_attrs(KMF_ATTRIBUTE *attrlist,
    KMF_KEYSTORE_TYPE *pktype)
{
	int num = 0;

	*pktype = KMF_KEYSTORE_OPENSSL;
	kmf_set_attr_at_index(attrlist, num++, KMF_KEYSTORE_TYPE_ATTR,
	    pktype, sizeof (KMF_KEYSTORE_TYPE));
	if (kmf_ta_keystore == NULL)
		fatal("kmf_openssl_set_validate_attrs: kmf_ta_keystore NULL");
	debug("kmf_openssl_set_validate_attrs: kmf_ta_keystore is '%s'.",
	    kmf_ta_keystore);
	kmf_set_attr_at_index(attrlist, num++, KMF_DIRPATH_ATTR,
	    kmf_ta_keystore, strlen(kmf_ta_keystore));

	return (num);
}

/*
 * When working with the known_hosts file we need to convert the KMF certificate
 * structure into an OpenSSH Key structure. Returned structure must be freed via
 * key_free() when done. The function returns NULL on failure.
 */
Key *
kmf_openssl_get_key_from_cert(KMF_DATA *cert)
{
	Key *key = NULL;
	X509 *xcert = NULL;
	EVP_PKEY *pkey = NULL;
	uchar_t *outbuf_p = NULL, *outbuf = NULL;

	/* Copy cert data to outbuf. */
	outbuf = (uchar_t *)xmalloc(cert->Length);
	(void) memcpy(outbuf, cert->Data, cert->Length);

	/* Use a temp pointer; required by OpenSSL. */
	outbuf_p = outbuf;
	xcert = d2i_X509(NULL, (const uchar_t **)&outbuf_p, cert->Length);
	xfree(outbuf);
	if (xcert == NULL)
		goto err;

	if ((pkey = X509_get_pubkey(xcert)) == NULL)
		goto err2;

	/*
	 * You must use RSA_free() or DSA_free() on the returned pointers when
	 * done. That is done automatically in key_free().
	 */
	if (pkey->type == EVP_PKEY_RSA) {
		key = key_new_empty(KEY_RSA);
		key->rsa = EVP_PKEY_get1_RSA(pkey);
	} else if (pkey->type == EVP_PKEY_DSA) {
		key = key_new_empty(KEY_DSA);
		key->dsa = EVP_PKEY_get1_DSA(pkey);
	} else
		goto err3;

	/*
	 * We do not need this struct anymore. Note that the inner RSA/DSA
	 * structure is not freed here since the get1 function bumped up the
	 * reference count inside of them. The structures will be freed later
	 * when key_free() is called on 'key'.
	 */
	EVP_PKEY_free(pkey);
	X509_free(xcert);
	return (key);
err3:
	EVP_PKEY_free(pkey);
err2:
	X509_free(xcert);
err:
	return (key);
}

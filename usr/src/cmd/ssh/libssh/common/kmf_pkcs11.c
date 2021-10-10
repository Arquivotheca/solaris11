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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "xmalloc.h"
#include "ssh.h"
#include "log.h"
#include "key.h"
#include "kmf_pkcs11.h"
#include "kmfdef.h"
#include "kmf.h"
#include "readpass.h"
#include "misc.h"

/*
 * Get the PIN. The PIN may be provided as a parameter, from the 'pinfile'
 * attribute, or read from a terminal. 'ask_for_pin' tells whether we should
 * read it from the terminal if both previous methods "fail".
 *
 * The caller must free the PIN.
 */
static char *
ssh_kmf_get_pin(char *pin, kmf_key_t *kmf_key, int ask_for_pin)
{
	/*
	 * This should be enough for the prompt. Note we use snprintf so at
	 * worse the prompt would be truncated.
	 */
	char prompt[128];
	char *tmp_pin = NULL;

	if (pin == NULL) {
		if (kmf_key->pkcs11_uri->pinfile == NULL) {
			/*
			 * The client will ask for a PIN if the pinfile
			 * attribute is not present.
			 */
			if (ask_for_pin == SSH_KMF_ASK_FOR_TOKEN_PIN) {
				debug("No 'pinfile' attribute in URI.");
				snprintf(prompt, sizeof (prompt),
				    gettext("Enter PIN for '%s': "),
				    kmf_key->pkcs11_uri->token);
				tmp_pin = read_passphrase(prompt, 0);
			/* Server never asks for a PIN. */
			} else {
				error("No 'pinfile' attribute in URI.");
				return (NULL);
			}
		} else
			tmp_pin = read_pinfile(kmf_key->pkcs11_uri->pinfile);
	} else
		return (xstrdup(pin));

	/*
	 * Could not read the PIN neither from the PIN file nor from the
	 * terminal.
	 */
	if (tmp_pin == NULL) {
		if (kmf_key->pkcs11_uri->pinfile != NULL)
			error("Could not read the PIN file for '%s'.",
			    kmf_key->pkcs11_uri->token);
		else
			error("Could not read the PIN for '%s'.",
			    kmf_key->pkcs11_uri->token);
		return (NULL);
	}

	if (pin == NULL)
		kmf_key->pin = xstrdup(tmp_pin);
	return (tmp_pin);
}

/*
 * Find a private key handle to an object specified by kmf_key. Return 1 on
 * success, 0 on failure.
 */
int
pkcs11_get_privkey(kmf_key_t *kmf_key, int ask_for_pin, char *pin)
{
	int num;
	KMF_RETURN rv;
	uint32_t count_attr;
	KMF_KEY_CLASS kclass;
	KMF_CREDENTIAL kcred;
	KMF_KEYSTORE_TYPE ktype;
	/* We need to find out if there is more than 1 key. */
	KMF_KEY_HANDLE khandle[2];
	/*
	 * KMF_KEYSTORE_TYPE_ATTR, KMF_COUNT_ATTR, KMF_KEYLABEL_ATTR,
	 * KMF_KEY_HANDLE_ATTR, KMF_TOKEN_BOOL_ATTR, KMF_CREDENTIAL_ATTR,
	 * KMF_KEYCLASS_ATTR.
	 */
	KMF_ATTRIBUTE attrlist[7];
	boolean_t is_token = B_TRUE;

	kcred.cred = ssh_kmf_get_pin(pin, kmf_key, ask_for_pin);
	if (kcred.cred == NULL)
		return (0);
	kcred.credlen = strlen(kcred.cred);

	/*
	 * kmf_find_key() never returns more keys than in KMF_COUNT_ATTR so we
	 * must use 2 in order to find out if there are more keys of the same
	 * label.
	 */
	count_attr = 2;
	num = pkcs11_set_find_attrs(attrlist, &ktype, &count_attr);

	/*
	 * The object attribute is non-NULL. That was enforced in
	 * ssh_kmf_check_uri().
	 */
	kmf_set_attr_at_index(attrlist, num++, KMF_KEYLABEL_ATTR,
	    kmf_key->pkcs11_uri->object,
	    strlen((char *)kmf_key->pkcs11_uri->object));
	kmf_set_attr_at_index(attrlist, num++, KMF_KEY_HANDLE_ATTR,
	    khandle, sizeof (KMF_KEY_HANDLE));
	kmf_set_attr_at_index(attrlist, num++, KMF_TOKEN_BOOL_ATTR,
	    &is_token, sizeof (boolean_t));
	kmf_set_attr_at_index(attrlist, num++, KMF_CREDENTIAL_ATTR,
	    &kcred, sizeof (KMF_CREDENTIAL));
	kclass = KMF_ASYM_PRI;
	kmf_set_attr_at_index(attrlist, num++, KMF_KEYCLASS_ATTR,
	    &kclass, sizeof (KMF_KEY_CLASS));

	if ((rv = kmf_find_key(kmf_key->h, num, attrlist)) != KMF_OK) {
		if (rv == KMF_ERR_AUTH_FAILED)
			error("Incorrect PIN for: %s", kmf_key->uri_str);
		else
			ssh_kmf_debug(kmf_key->h, "kmf_find_key", rv);
		memset(kcred.cred, 0, strlen(kcred.cred));
		xfree(kcred.cred);
		return (0);
	}

	if (count_attr != 1) {
		memset(kcred.cred, 0, strlen(kcred.cred));
		xfree(kcred.cred);
		error("1 private key expected (found %d) for: %s",
		    count_attr, kmf_key->uri_str);
		return (0);
	}
	kmf_key->privh = khandle[0];
	debug("kmf_find_key: key algorithm type %d.", kmf_key->privh.keyalg);

	/*
	 * We have our private key handle now so get rid of the PIN from the
	 * memory, we won't need it again.
	 */
	memset(kcred.cred, 0, strlen(kcred.cred));
	xfree(kcred.cred);
	return (1);
}

/*
 * Load a certificate in a DER form specified by kmf_key.
 */
int
pkcs11_load_certificate(kmf_key_t *kmf_key, char *pin)
{
	KMF_RETURN rv;
	int num;
	/*
	 * If there are more certificates of the same label, we need to know
	 * that and return an error.
	 */
	uint32_t count_attr = 2;
	KMF_X509_DER_CERT certder[2];
	KMF_KEYSTORE_TYPE ktype;
	/*
	 * KMF_KEYSTORE_TYPE_ATTR, KMF_COUNT_ATTR, KMF_CREDENTIAL_ATTR,
	 * KMF_CERT_LABEL_ATTR, KMF_X509_DER_CERT_ATTR.
	 */
	KMF_ATTRIBUTE attrlist[5];
	KMF_CREDENTIAL kcred;
	int auth = 0;

	/* This should not happen but let's fall in style in case of trouble. */
	if (kmf_key->h == NULL)
		fatal("pkcs11_load_certificate: KMF handle NULL in the key.");

	rv = token_auth_needed(kmf_key->h, (char *)kmf_key->pkcs11_uri->token,
	    &auth);
	if (rv != KMF_OK) {
		ssh_kmf_error(kmf_key->h, "token_auth_needed", rv);
		return (0);
	}

	/*
	 * Note that we do not have the 'ask_for_pin' parameter in the
	 * pkcs11_load_certificate() function. That is because we either already
	 * got the PIN through since we accessed the private key before hand or
	 * we need only the certificate. In the latter case, only ssh-keygen and
	 * ssh-add may read only a cert in which case we can always try to read
	 * the PIN from a terminal.
	 *
	 * More specifically, if this function is called from the server then we
	 * know that we already have the PIN. If we could not get it we would
	 * not reach this place at all. That is why we can safely use
	 * SSH_KMF_ASK_FOR_TOKEN_PIN here.
	 */
	if (auth == 1) {
		if (pin != NULL)
			kcred.cred = ssh_kmf_get_pin(pin, kmf_key,
			    SSH_KMF_ASK_FOR_TOKEN_PIN);
		else {
			/*
			 * If we loaded a private key before loading a
			 * certificate which happens for example in the SSH
			 * client, we may already have the PIN in the kmf_key
			 * structure.
			 */
			kcred.cred = ssh_kmf_get_pin(kmf_key->pin, kmf_key,
			    SSH_KMF_ASK_FOR_TOKEN_PIN);
		}
		if (kcred.cred == NULL)
			return (0);
		kcred.credlen = strlen(kcred.cred);
	}

	num = pkcs11_set_find_attrs(attrlist, &ktype, &count_attr);

	if (auth == 1) {
		kmf_set_attr_at_index(attrlist, num++, KMF_CREDENTIAL_ATTR,
		    &kcred, sizeof (KMF_CREDENTIAL));
	}

	kmf_set_attr_at_index(attrlist, num++, KMF_CERT_LABEL_ATTR,
	    kmf_key->pkcs11_uri->object,
	    strlen((char *)kmf_key->pkcs11_uri->object));
	kmf_set_attr_at_index(attrlist, num++, KMF_X509_DER_CERT_ATTR,
	    certder, sizeof (KMF_X509_DER_CERT));

	rv = kmf_find_cert(kmf_key->h, num, attrlist);
	debug("pkcs11_load_certificate: kmf_find_cert returned %d", rv);
	if (rv != KMF_OK) {
		ssh_kmf_debug(kmf_key->h, "kmf_find_cert", rv);
		goto err;
	}

	if (count_attr != 1) {
		error("1 certificate expected (found %d) for: %s",
		    count_attr, kmf_key->uri_str);
		goto err;
	}
	/* Copy the certificate into our KMF structure. */
	kmf_key->cert = certder[0];
	/* Make sure we will have no false memory leak warnings. */
	memset(certder, 0, sizeof (KMF_X509_DER_CERT) * 2);
	/* Let's be sure we have something. */
	if (kmf_key->cert.certificate.Length == 0) {
		error("pkcs11_load_certificate: certificate length is zero.");
		goto err;
	}

	if (auth == 1) {
		memset(kcred.cred, 0, strlen(kcred.cred));
		xfree(kcred.cred);
	}
	return (1);

err:
	if (auth == 1) {
		memset(kcred.cred, 0, strlen(kcred.cred));
		xfree(kcred.cred);
	}
	return (0);
}

/*
 * Set attributes needed for looking up the object. We need a keystore type and
 * a count.
 */
int
pkcs11_set_find_attrs(KMF_ATTRIBUTE *attrlist, KMF_KEYSTORE_TYPE *pktype,
    uint32_t *pcount_attr)
{
	int num = 0;

	*pktype = KMF_KEYSTORE_PK11TOKEN;
	kmf_set_attr_at_index(attrlist, num++, KMF_KEYSTORE_TYPE_ATTR,
	    pktype, sizeof (KMF_KEYSTORE_TYPE));
	kmf_set_attr_at_index(attrlist, num++, KMF_COUNT_ATTR,
	    pcount_attr, sizeof (uint32_t));

	return (num);
}

/*
 * Set attributes needed for signing using a key in PKCS#11 keystore. Caller
 * must ensure that alist is large enough.
 */
int
pkcs11_set_sign_attrs(KMF_ATTRIBUTE *attrlist, KMF_KEYSTORE_TYPE *pktype,
    kmf_key_t *kmf_key)
{
	int num = 0;
	KMF_KEY_HANDLE *kh = &kmf_key->privh;

	*pktype = KMF_KEYSTORE_PK11TOKEN;
	kmf_set_attr_at_index(attrlist, num++, KMF_KEYSTORE_TYPE_ATTR,
	    pktype, sizeof (KMF_KEYSTORE_TYPE));
	kmf_set_attr_at_index(attrlist, num++, KMF_KEY_HANDLE_ATTR,
	    kh, sizeof (KMF_KEY_HANDLE));

	debug("pkcs11_set_sign_attrs: key alg is %d.", (*kh).keyalg);

	return (num);
}

/*
 * Some keystores must be configured prior to be used. We only care for the
 * token label, other token attributes (manufacturer, serial) are silently
 * ignored.
 */
int
pkcs11_configure_keystore(KMF_HANDLE_T handle, uchar_t *token_label)
{
	int num;
	KMF_RETURN rv;
	/* KMF_KEYSTORE_TYPE_ATTR, KMF_TOKEN_LABEL_ATTR */
	KMF_ATTRIBUTE attrlist[2];
	KMF_KEYSTORE_TYPE ktype = KMF_KEYSTORE_PK11TOKEN;

	num = 0;
	kmf_set_attr_at_index(attrlist, num++, KMF_KEYSTORE_TYPE_ATTR,
	    &ktype, sizeof (KMF_KEYSTORE_TYPE));

	debug("pkcs11_configure_keystore: token label '%.200s'", token_label);
	kmf_set_attr_at_index(attrlist, num++, KMF_TOKEN_LABEL_ATTR,
	    token_label, strlen((char *)token_label));

	/*
	 * Note that this is not the function that calls C_Login() on the
	 * PKCS#11 token, that is done in pkcs11_get_privkey().
	 */
	rv = kmf_configure_keystore(handle, num, attrlist);
	if (rv != KMF_OK) {
		if (rv == KMF_ERR_TOKEN_NOT_PRESENT)
			debug("Could not find token: %s", token_label);
		else
			ssh_kmf_debug(handle, "pkcs11_configure_keystore", rv);
		return (0);
	}

	return (1);
}

/*
 * Borrowed from pktool's code. There is actually a bug filed which should
 * expose this function in the KMF API. Note that this function is a reason why
 * we need to link SSH with libpkcs11. C_GetTokenInfo() is the only PKCS#11 API
 * function we call from SSH directly.
 */
KMF_RETURN
token_auth_needed(KMF_HANDLE_T handle, char *tokenlabel, int *auth)
{
	CK_TOKEN_INFO info;
	CK_SLOT_ID slot;
	CK_RV ckrv;
	KMF_RETURN rv;

	*auth = 0;
	rv = kmf_pk11_token_lookup(handle, tokenlabel, &slot);
	if (rv != KMF_OK)
		return (rv);

	ckrv = C_GetTokenInfo(slot, &info);
	if (ckrv != KMF_OK)
		return (KMF_ERR_INTERNAL);

	if (info.flags & CKF_LOGIN_REQUIRED)
		*auth = 1;

	return (KMF_OK);
}

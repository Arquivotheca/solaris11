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

/*
 * This file (kmf.c) is the main file for the X.509 support in the pubkey
 * authentication method. It is implemented according to the
 * draft-saarenmaa-ssh-x509-00.txt IETF draft which is what other vendors
 * implemented.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cryptoutil.h>

#include "xmalloc.h"
#include "ssh.h"
#include "bufaux.h"
#include "log.h"
#include "kmf_pkcs11.h"
#include "kmf_openssl.h"
#include "key.h"
#include "kmf.h"

/* A global KMF session handle for certificate validations. */
KMF_HANDLE_T kmf_global_handle = NULL;
char *kmf_ta_keystore = NULL;

/*
 * Some common error messages related to X.509 pubkey auth and used from various
 * places.
 */
const char *
ssh_pk11_err(int code)
{
	switch (code) {
	case SSH_KMF_CANNOT_LOAD_OBJECT:
		return (gettext("Could not access URI"));
	case SSH_KMF_INVALID_PKCS11_URI:
		return (gettext("Invalid URI for key"));
	case SSH_KMF_MISSING_TOKEN_OR_OBJ:
		return (gettext("Missing object/token attribute in URI"));
	}

	/* This should not happen but just in case it's better than NULL. */
	return (gettext("Other X.509 or PKCS#11 error."));
}

/*
 * Auxiliary function to get newly allocated copy of the data field from the
 * KMF_DATA structure.
 */
static uchar_t *
ssh_kmf_data_dup(KMF_DATA *data)
{
	uchar_t *str = (uchar_t *)xmalloc(data->Length);
	memcpy(str, data->Data, data->Length);
	return (str);
}

/*
 * This function looks up and prints out the exact KMF error message, be it a
 * generic one, from a plugin, or from a mapper. It uses error() or debug() for
 * output depending on the "err" parameter.
 */
static void
ssh_kmf_process_error(KMF_HANDLE_T h, int err, char *fn_name, KMF_RETURN rv)
{
	char *msg = NULL;
	KMF_RETURN rv2;
	void (*fn)(const char *fmt, ...);

	if (err == 1)
		fn = error;
	else
		fn = debug;

	if (rv != KMF_ERR_INTERNAL) {
		if ((rv2 = kmf_get_kmf_error_str(rv, &msg)) != KMF_OK) {
			fn("ssh_kmf_process_error: kmf_get_kmf_error_str "
			    "failed (%d) for KMF error code %d.", rv2, rv);
			kmf_free_str(msg);
			return;
		}
	} else {
		if (h == NULL) {
			fn("ssh_kmf_process_error: KMF handle NULL.");
			return;
		}

		/* Let's see if it comes from a plugin or a mapper. */
		debug("Got KMF_ERR_INTERNAL, looking for plugin/mapper error "
		    "string.");
		rv2 = kmf_get_plugin_error_str(h, &msg);
		if (rv2 != KMF_ERR_MISSING_ERRCODE && rv2 != KMF_OK) {
			fn("ssh_kmf_process_error: kmf_get_plugin_error_str "
			    "failed for internal (%d).", rv2);
			return;
		/* May not be a plugin error, let's try a mapper. */
		} else if (rv2 == KMF_ERR_MISSING_ERRCODE) {
			if ((rv2 = kmf_get_mapper_error_str(h, &msg))
			    != KMF_OK) {
				fn("ssh_kmf_process_error: "
				    "kmf_get_mapper_error_str failed (%d)"
				    " for KMF error code %d.", rv2, rv);
				kmf_free_str(msg);
				return;
			}
		}
	}

	fn("%.200s: %.200s (%d)", fn_name, msg, rv);
	kmf_free_str(msg);
}

/*
 * Find the KMF error and print it out as an error message.
 */
void
ssh_kmf_error(KMF_HANDLE_T h, char *fn_name, KMF_RETURN rv)
{
	ssh_kmf_process_error(h, 1, fn_name, rv);
}

/*
 * Find the KMF error and print it out as a debug message.
 */
void
ssh_kmf_debug(KMF_HANDLE_T h, char *fn_name, KMF_RETURN rv)
{
	ssh_kmf_process_error(h, 0, fn_name, rv);
}

/*
 * Initialize the KMF framework. If the policy database and its name are NULL,
 * KMF will use the library defaults. If the KMF key structure is non-NULL its
 * keystore will be configured as well. Note that every X.509 cert/key can be
 * stored in a different keystore so we must initialize a separate KMF session
 * for each of them.
 *
 * If kmf_key is NULL it means we are initializing KMF to validate certificates
 * in which case we work with a global KMF handle. If the TA keystore name is
 * non-NULL and we initialize the global session, the global variable for such
 * keystore is set. It will be used in various places when validating peers'
 * certificates.
 *
 * Returns 1 on success, 0 on failure.
 */
int
ssh_kmf_init(char *db, char *name, char *ta_keystore, kmf_key_t *kmf_key)
{
	KMF_RETURN rv;
	KMF_HANDLE_T *h;

	/*
	 * If kmf_key is NULL it means the session is for certificate validation
	 * and not for signing.
	 */
	if (kmf_key == NULL) {
		debug("Creating a global KMF session.");
		h = &kmf_global_handle;
		kmf_ta_keystore = ta_keystore;
	} else
		h = &kmf_key->h;

	/*
	 * If kmf_initialize() fails it is not a show stopper, we expect the
	 * caller to cope with that and skip the X.509 auth method.
	 */
	if ((rv = kmf_initialize(h, db, name)) != KMF_OK) {
		ssh_kmf_debug(NULL, "kmf_initialize", rv);
		if (db == NULL && name == NULL)
			error("Could not initialize KMF with a default "
			    "policy.");
		else
			error("Could not initialize KMF with a non-default "
			    "policy.");
		return (0);
	}

	if (kmf_key != NULL) {
		/*
		 * For now, we support PKCS#11 keystores only for host keys and
		 * user identities. If the keystore configuration fails it is
		 * not a fatal problem, it is like using a non-existent host key
		 * filename. Server will just skip those.
		 */
		debug("Configuring the PKCS#11 keystore.");
		if (kmf_key->pkcs11_uri != NULL)
			return (pkcs11_configure_keystore(*h,
			    kmf_key->pkcs11_uri->token));
		else {
			error("Could not configure the keystore.");
			return (0);
		}
	}

	return (1);
}

/*
 * The original OpenSSH code uses 0 for success and -1 for failure so we must as
 * well.
 */
int
ssh_kmf_sign_data(Key *key, uchar_t **sigp, uint_t *slenp, uchar_t *data,
    uint_t dlen)
{
	Buffer b;
	int num, sig_size;
	KMF_RETURN rv;
	KMF_KEYSTORE_TYPE ktype;
	/*
	 * KMF_KEYSTORE_TYPE_ATTR, KMF_KEY_HANDLE_ATTR, KMF_DATA_ATTR,
	 * KMF_ALGORITHM_INDEX_ATTR, KMF_OUT_DATA_ATTR.
	 */
	KMF_ATTRIBUTE attrlist[5];
	KMF_ALGORITHM_INDEX alg_idx;
	KMF_DATA kmf_data, kmf_out_data;

	/* Let's be sure about the key type. */
	if (key == NULL || key->kmf_key == NULL ||
	    (key->type != KEY_X509_RSA && key->type != KEY_X509_DSS)) {
		fatal("ssh_kmf_sign_data: key not of the X.509 type.");
	}

	/* For now, we support PKCS#11 keystores only. */
	if (key->kmf_key->pkcs11_uri != NULL)
		num = pkcs11_set_sign_attrs(attrlist, &ktype, key->kmf_key);
	else
		return (-1);

	debug("ssh_kmf_sign_data: will sign %d bytes of data.", dlen);
	kmf_data.Length = dlen;
	kmf_data.Data = data;
	kmf_set_attr_at_index(attrlist, num++, KMF_DATA_ATTR, &kmf_data,
	    sizeof (KMF_DATA));

	/* Use of SHA-1 is mandatory in draft-saarenmaa-ssh-x509-00.txt. */
	if (key->type == KEY_X509_RSA)
		alg_idx = KMF_ALGID_SHA1WithRSA;
	else
		alg_idx = KMF_ALGID_SHA1WithDSA;
	kmf_set_attr_at_index(attrlist, num++, KMF_ALGORITHM_INDEX_ATTR,
	    &alg_idx, sizeof (KMF_ALGORITHM_INDEX));

	/*
	 * Maximum size of the RSA signature is size of its modulus. Maximum
	 * size of DSA signature with SHA-1 (the only hash used in X.509 pubkey
	 * auth method) is 320 bits. Note that if the length was not
	 * sufficient kmf_sign_data() would just fail since C_Sign() would fail
	 * with the CKR_BUFFER_TOO_SMALL return code.
	 */
	sig_size = key_size(key);
	kmf_out_data.Length = sig_size < 320 ? 320 / 8 : sig_size / 8;
	kmf_out_data.Data = calloc(1, kmf_out_data.Length);
	kmf_set_attr_at_index(attrlist, num++, KMF_OUT_DATA_ATTR,
	    &kmf_out_data, sizeof (KMF_DATA));

	debug("ssh_kmf_sign_data: keyalg is %d.", key->kmf_key->privh.keyalg);
	if ((rv = kmf_sign_data(key->kmf_key->h, num, attrlist)) != KMF_OK) {
		ssh_kmf_debug(key->kmf_key->h, "kmf_sign_data", rv);
		return (-1);
	}
	debug("ssh_kmf_sign_data: kmf_sign_data passed, returned siglen is %d",
	    kmf_out_data.Length);

	buffer_init(&b);
	if (key->type == KEY_X509_RSA)
		buffer_put_cstring(&b, "x509v3-sign-rsa");
	else if (key->type == KEY_X509_DSS)
		buffer_put_cstring(&b, "x509v3-sign-dss");
	else
		fatal("Key not of X.509 RSA or DSA type (%d).", key->type);

	buffer_put_string(&b, kmf_out_data.Data, kmf_out_data.Length);
	*slenp = buffer_len(&b);
	debug("Data signature put to the packet buffer.");

	*sigp = xmalloc(*slenp);
	memcpy(*sigp, buffer_ptr(&b), *slenp);
	buffer_free(&b);
	kmf_free_data(&kmf_out_data);

	return (0);
}

/*
 * The function verifies a data signature. Used in key_verify() for X.509 keys.
 * Returns 0 on verification error, 1 on success, and -1 on non-verification
 * error. That is how original functions ssh_rsa_verify and ssh_dss_verify work
 * so we honor these return codes.
 */
int
ssh_kmf_verify_data(Key *key, uchar_t *sig, uint_t slen, uchar_t *data,
    uint_t dlen)
{
	Buffer b;
	int num;
	uint_t len;
	KMF_RETURN rv;
	char *str = NULL;
	KMF_KEYSTORE_TYPE ktype;
	/*
	 * KMF_KEYSTORE_TYPE_ATTR, KMF_ALGORITHM_INDEX_ATTR, KMF_DATA_ATTR,
	 * KMF_IN_SIGN_ATTR, KMF_SIGNER_CERT_DATA_ATTR.
	 */
	KMF_ATTRIBUTE attrlist[5];
	KMF_ALGORITHM_INDEX alg_idx;
	KMF_DATA kmf_data, kmf_sig_data;

	if (key == NULL || key->kmf_key == NULL)
		fatal("ssh_kmf_verify_data: key or key->kmf_key is NULL.");

	/*
	 * kmf_verify_data() requires KMF_KEYSTORE_TYPE_ATTR even for
	 * a certificate.
	 */
	num = kmf_openssl_set_verify_attrs(attrlist, &ktype);

	/*
	 * We need to get the signature string out of the blob. Note that there
	 * is a bug in draft-saarenmaa-ssh-x509-00. It says the signature string
	 * should be "ssh-rsa" or "ssh-dss" but in fact it is "x509v3-sign-rsa"
	 * or "x509v3-sign-dss". This is how it is implemented in existing SSH
	 * implementations, even in those written by the I-D authors.
	 */
	buffer_init(&b);
	buffer_append(&b, sig, slen);
	str = buffer_get_string(&b, &len);
	if (strcmp("x509v3-sign-rsa", str) != 0 &&
	    strcmp("x509v3-sign-dss", str) != 0) {
		error("ssh_kmf_verify_data: cannot handle type %s", str);
		buffer_free(&b);
		xfree(str);
		return (-1);
	}

	/*
	 * draft-saarenmaa-ssh-x509-00.txt explicitly requires SHA-1. There is
	 * no option for MD5 or any other hash algorithm.
	 */
	if (strcmp("x509v3-sign-rsa", str) == 0)
		alg_idx = KMF_ALGID_SHA1WithRSA;
	else
		alg_idx = KMF_ALGID_SHA1WithDSA;
	kmf_set_attr_at_index(attrlist, num++, KMF_ALGORITHM_INDEX_ATTR,
	    &alg_idx, sizeof (KMF_ALGORITHM_INDEX));

	xfree(str);
	str = buffer_get_string(&b, &len);
	debug("Extracted signature is %d bytes long.", len);
	debug("Data to be verified is %d bytes long.", dlen);
	buffer_free(&b);

	kmf_data.Length = dlen;
	kmf_data.Data = data;
	kmf_set_attr_at_index(attrlist, num++, KMF_DATA_ATTR,
	    &kmf_data, sizeof (KMF_DATA));

	kmf_sig_data.Length = len;
	kmf_sig_data.Data = (uchar_t *)str;
	kmf_set_attr_at_index(attrlist, num++, KMF_IN_SIGN_ATTR,
	    &kmf_sig_data, sizeof (KMF_DATA));

	/* We are verifying data so we give the function a certificate. */
	kmf_set_attr_at_index(attrlist, num++, KMF_SIGNER_CERT_DATA_ATTR,
	    &key->kmf_key->cert.certificate, sizeof (KMF_DATA));

	rv = kmf_verify_data(kmf_global_handle, num, attrlist);
	if (rv != KMF_OK) {
		xfree(str);
		ssh_kmf_debug(kmf_global_handle, "kmf_verify_data", rv);
		return (0);
	}
	debug("kmf_verify_data: data verification passed..");

	xfree(str);
	return (1);
}

/*
 * This function validates a certificate. It returns
 * SSH_KMF_CERT_VALIDATION_ERROR, SSH_KMF_CERT_VALIDATED, or SSH_KMF_MISSING_TA
 * if validation failed because of the KMF_CERT_VALIDATE_ERR_TA return flag.
 */
int
ssh_kmf_validate_cert(kmf_key_t *kmf_key, char *trusted_anchor_keystore)
{
	KMF_RETURN rv;
	int num, result;
	KMF_KEYSTORE_TYPE ktype;
	/*
	 * KMF_KEYSTORE_TYPE_ATTR, KMF_DIRPATH_ATTR, KMF_CERT_DATA_ATTR,
	 * KMF_VALIDATE_RESULT_ATTR.
	 */
	KMF_ATTRIBUTE attrlist[4];

	debug("ssh_kmf_validate_cert: validating a certificate.");

	/*
	 * kmf_set_attr_at_index() just uses a pointer to the variable, it does
	 * not copy its value. We must pass a pointer into the function since we
	 * cannot use a local variable there.
	 */
	num = kmf_openssl_set_validate_attrs(attrlist, &ktype);
	kmf_set_attr_at_index(attrlist, num++, KMF_CERT_DATA_ATTR,
	    &kmf_key->cert.certificate, sizeof (KMF_DATA));
	kmf_set_attr_at_index(attrlist, num++, KMF_VALIDATE_RESULT_ATTR,
	    &result, sizeof (int));

	rv = kmf_validate_cert(kmf_global_handle, num, attrlist);
	if (rv != KMF_OK) {
		/*
		 * If we can not find the TA cert and there is no other
		 * problem with the certificate (not expired, for example), we
		 * return SSH_KMF_MISSING_TA so that the caller can proceed with
		 * searching for the key in the known_hosts database.
		 */
		if (rv == KMF_ERR_CERT_VALIDATION) {
			if (result != KMF_CERT_VALIDATE_ERR_TA) {
				/* Warn about some usual problems we can hit. */
				if (result & KMF_CERT_VALIDATE_ERR_SIGNATURE)
					error("Bad user or host certificate "
					    "signature.");
				if (result & KMF_CERT_VALIDATE_ERR_TIME)
					error("Expired user or host "
					    "certificate.");
				return (SSH_KMF_CERT_VALIDATION_ERROR);
			} else
				return (SSH_KMF_MISSING_TA);
		} else {
			ssh_kmf_error(kmf_global_handle, "kmf_verify_data", rv);
			return (SSH_KMF_CERT_VALIDATION_ERROR);
		}
	}

	if (result != KMF_CERT_VALIDATE_OK) {
		debug("kmf_validate_cert: %d (expecting "
		    "KMF_CERT_VALIDATE_OK).", result);
		return (SSH_KMF_CERT_VALIDATION_ERROR);
	}

	return (SSH_KMF_CERT_VALIDATED);
}

/*
 * Return a freshly allocated Key structure for X.509 cert/key.
 */
static Key *
x509_key_new(int type, kmf_key_t *kmf_key)
{
	Key *key;

	key = key_new(type);
	key->kmf_key = kmf_key;
	return (key);
}

/*
 * Load a private key from a PKCS#11 keystore. The parsed specification of the
 * private key is in pkcs11_uri. Returned NULL indicates an error.
 *
 * This function is used only if you need to authenticate yourself to the other
 * side.
 */
Key *
ssh_kmf_get_x509_key(pkcs11_uri_t *pkcs11_uri, const char *uri_str,
    int ask_for_pin, char *pin)
{
	kmf_key_t *kmf_key = (kmf_key_t *)xcalloc(1, sizeof (kmf_key_t));

	kmf_key->pkcs11_uri = pkcs11_uri;
	/* We need that for getpassphrase prompt later. */
	kmf_key->uri_str = xstrdup(uri_str);
	if (ssh_kmf_init(NULL, NULL, NULL, kmf_key) != 1)
		goto err;

	/* For now, we support PKCS#11 keystores only. */
	if (pkcs11_uri != NULL) {
		if (pkcs11_get_privkey(kmf_key, ask_for_pin, pin) != 1)
			goto err;
	} else
		goto err;

	/*
	 * We do not care about the private key type now, we get it when
	 * processing the certificate. Note that we always need a certificate if
	 * we work with a X.509 private key. See ssh_kmf_load_certificate().
	 */
	return (x509_key_new(KEY_UNSPEC, kmf_key));
err:
	if (kmf_key->h != NULL)
		kmf_finalize(kmf_key->h);
	xfree(kmf_key->uri_str);
	if (kmf_key->pin != NULL)
		xfree(kmf_key->pin);
	xfree(kmf_key);
	return (NULL);
}

/*
 * Verify that the blob contains a DER encoded certificate and get the public
 * key type (RSA, DSA). Function returns SSH_KMF_INVALID_CERT,
 * SSH_KMF_CERT_FOUND, or SSH_KMF_NOT_CERT.
 */
static int
ssh_kmf_get_cert_pubkey_alg(KMF_HANDLE_T kmfh, KMF_DATA *data, int *type)
{
	KMF_RETURN rv;
	char *alg = NULL;
	KMF_ENCODE_FORMAT format;

	/*
	 * If the data is DER encoded certificate we must find out what a public
	 * key algorithm is used in it.
	 */
	if (kmf_is_cert_data(data, &format) == KMF_OK) {
		if (format == KMF_FORMAT_ASN1) {
			rv = kmf_get_cert_pubkey_alg_str(kmfh, data, &alg);
			if (rv != KMF_OK) {
				ssh_kmf_debug(kmfh,
				    "kmf_get_cert_pubkey_alg_str", rv);
				return (SSH_KMF_INVALID_CERT);
			}

			if (strcmp(alg, "rsaEncryption") == 0)
				*type = KEY_X509_RSA;
			else {
				if (strcmp(alg, "dsaEncryption") == 0)
					*type = KEY_X509_DSS;
				else
					return (SSH_KMF_INVALID_CERT);
			}

			debug("ssh_kmf_get_cert_pubkey_alg: alg string from "
			    "cert is %s", alg);
			xfree(alg);
			return (SSH_KMF_CERT_FOUND);
		} else
			return (SSH_KMF_INVALID_CERT);
	} else
		return (SSH_KMF_NOT_CERT);
}

/*
 * Find a certificate in the keystore and put it into a Key structure that may
 * be provided by the caller. This is the function that is called from
 * key_load_public()->key_try_load_public().
 *
 * Note that the 'key' will usually be already set with a private key handle but
 * it might not. For example, when removing a key from the agent, the ssh-add(1)
 * loads just the public key and the agent then uses it to reference the private
 * key.
 *
 * On success, the function returns the new alloated Key structure or "key" if
 * non-NULL. It returns NULL on failure.
 *
 * Some tokens might require a PIN even for certificates. Sometimes we already
 * know the PIN (if we accessed its corresponding private key before) so the
 * caller might provide it via the "pin" parameter.
 */
Key *
ssh_kmf_load_certificate(Key *key, pkcs11_uri_t *pkcs11_uri, char *pin)
{
	int ret, type;
	Key *orig_key;
	kmf_key_t *kmf_key = NULL;

	orig_key = key;
	/* If key is non-NULL we already read a private key. */
	if (key != NULL)
		kmf_key = key->kmf_key;
	else
		kmf_key = (kmf_key_t *)xcalloc(1, sizeof (kmf_key_t));

	kmf_key->pkcs11_uri = pkcs11_uri;

	/*
	 * If 'key' is NULL we also do not have a KMF session handle for this
	 * key yet. Also, we do not need a KMF policy since this certificate
	 * will not be validated by us - the peer will be validating it because
	 * this is our certificate.
	 */
	if (key == NULL) {
		debug("ssh_kmf_load_certificate: will init KMF.");
		if (ssh_kmf_init(NULL, NULL, NULL, kmf_key) != 1)
			goto err;
	}

	if (pkcs11_load_certificate(kmf_key, pin) != 1)
		goto err;

	/*
	 * First, find out if we have an RSA or a DSA key. Even if we loaded the
	 * private key before calling this function, we must now set the key
	 * type which we would not get from the private key. Also see
	 * ssh_kmf_load_privkey().
	 */
	ret = ssh_kmf_get_cert_pubkey_alg(kmf_key->h,
	    &kmf_key->cert.certificate, &type);
	if (ret != SSH_KMF_CERT_FOUND)
		goto err;
	/*
	 * As mentioned above, in some situations we load only the certificate.
	 * In that case, we must create a new Key structure.
	 */
	if (key == NULL)
		key = x509_key_new(type, kmf_key);
	else
		key->type = type;

	debug("ssh_kmf_load_certificate returns success.");
	return (key);
err:
	if (orig_key == NULL)
		xfree(kmf_key);
	return (NULL);
}

/*
 * Check if a string contains a PKCS#11 URI. Returns SSH_KMF_PKCS11_URI_OK,
 * SSH_KMF_INVALID_PKCS11_URI, SSH_KMF_MISSING_TOKEN_OR_OBJ, or
 * SSH_KMF_NOT_PKCS11_URI. It returns a newly allocated PKCS#11 URI structure if
 * it returns SSH_KMF_PKCS11_URI_OK. The pkcs11_uri parameter must not be NULL.
 *
 * *pkcs11_uri will be assigned NULL if the function returns anything else than
 * SSH_KMF_PKCS11_URI_OK.
 */
int
ssh_kmf_check_uri(const char *uri_str, pkcs11_uri_t **pkcs11_uri)
{
	int ret;

	debug("ssh_kmf_check_uri: %s", uri_str);
	*pkcs11_uri = (pkcs11_uri_t *)xcalloc(1, sizeof (pkcs11_uri_t));

	if ((ret = pkcs11_parse_uri(uri_str, *pkcs11_uri)) == PK11_URI_OK) {
		/* The object and token attributes are mandatory. */
		if ((*pkcs11_uri)->object == NULL ||
		    (*pkcs11_uri)->token == NULL) {
			pkcs11_free_uri(*pkcs11_uri);
			xfree(*pkcs11_uri);
			*pkcs11_uri = NULL;
			return (SSH_KMF_MISSING_TOKEN_OR_OBJ);
		}
		return (SSH_KMF_PKCS11_URI_OK);
	}

	/* We better warn the user if we detect a wrongly specified URI. */
	if (ret == PK11_URI_INVALID || ret == PK11_URI_VALUE_OVERFLOW) {
		xfree(*pkcs11_uri);
		*pkcs11_uri = NULL;
		return (SSH_KMF_INVALID_PKCS11_URI);
	}

	/* xmalloc function fatal()s as well so it is OK to fatal() here. */
	if (ret == PK11_MALLOC_ERROR)
		fatal("malloc: out of memory when processing PKCS#11 URI.");

	/* Not a PKCS#11 URI at all. */
	xfree(*pkcs11_uri);
	*pkcs11_uri = NULL;
	return (SSH_KMF_NOT_PKCS11_URI);
}

/*
 * Compares two certificates stored in two Key structures. Since we have the
 * certs in DER format, we just compare them byte by byte. The function is used
 * in key_equal() for X.509 keys.
 */
int
ssh_kmf_compare_certs(const Key *a, const Key *b)
{
	if (CERT_LEN(a) == CERT_LEN(b) &&
	    memcmp(CERT_DATA(a), CERT_DATA(b), CERT_LEN(a)) == 0) {
		return (1);
	} else
		return (0);
}

/*
 * Get a private key handle for the X.509 key. This function works with host
 * keys only. A newly allocated Key structure is returned in 'key' (or NULL).
 *
 * The 'pin' attribute may contain a PIN (or NULL). We need the PIN in the agent
 * that cannot ask for a PIN from a terminal. So, if there is no 'pinfile'
 * attribute in the URI, the ssh-add command must send the PIN over to the
 * agent. Agent then uses the 'pin' parameter in this function.
 *
 * Function returns SSH_KMF_NOT_PKCS11_URI, SSH_KMF_CANNOT_LOAD_PRIV_KEY, or
 * SSH_KMF_PRIV_KEY_OK.
 */
int
ssh_kmf_key_load_private(const char *uri_str, Key **key, int ask_for_pin,
    char *pin)
{
	int ret;
	Key *x509_key = NULL;
	pkcs11_uri_t *pk11_uri = NULL;

	/* Let's be explicit and properly initialize the output parameter. */
	*key = NULL;

	/* First, let's check if the filename contains a PKCS#11 URI. */
	switch (ssh_kmf_check_uri(uri_str, &pk11_uri)) {
	case SSH_KMF_NOT_PKCS11_URI:
		return (SSH_KMF_NOT_PKCS11_URI);
	case SSH_KMF_PKCS11_URI_OK:
		/* We have a PKCS#11 URI. First, load a private key. */
		if ((x509_key = ssh_kmf_get_x509_key(pk11_uri, uri_str,
		    ask_for_pin, pin)) == NULL) {
			ret = SSH_KMF_CANNOT_LOAD_PRIV_KEY;
			goto err;
		}
		*key = x509_key;
		/*
		 * With public/private key pairs we do not need to load a public
		 * key since it is generated from the private part. With X.509
		 * certificates the situation is different though, we can not
		 * generate a certificate from a private key as we can with a
		 * public key. We need both the certificate and the private key.
		 */
		if (ssh_kmf_load_certificate(*key, pk11_uri, pin) == NULL) {
			ret = SSH_KMF_CANNOT_LOAD_PRIV_KEY;
			goto err;
		}
		break;
	case SSH_KMF_INVALID_PKCS11_URI:
		error("%s: %s.",
		    ssh_pk11_err(SSH_KMF_INVALID_PKCS11_URI), uri_str);
		ret = SSH_KMF_CANNOT_LOAD_PRIV_KEY;
		goto err;
	case SSH_KMF_MISSING_TOKEN_OR_OBJ:
		error("%s: %s.",
		    ssh_pk11_err(SSH_KMF_MISSING_TOKEN_OR_OBJ), uri_str);
		ret = SSH_KMF_CANNOT_LOAD_PRIV_KEY;
		goto err;
	}

	return (SSH_KMF_PRIV_KEY_OK);
err:
	if (x509_key != NULL)
		key_free(x509_key);
	else {
		if (pk11_uri != NULL) {
			pkcs11_free_uri(pk11_uri);
			xfree(pk11_uri);
		}
	}
	return (ret);
}

/*
 * Convert a certificate from the blob into a Key structure. This function is
 * used to process data we get from the peer which means that we will be
 * validating the certificate. Because of that, we use the global
 * kmf_global_handle as a KMF session.
 *
 * Note that key_from_blob() calls this function as well when processing client
 * RSA/DSA keys from files during a start up. However, we know that
 * at that time kmf_global_handle is still NULL (it is set in ssh_kex2) and we
 * also know that such blobs does not contain an X.509 certificate (we use
 * PKCS#11 tokens for X.509 certificates, not files). In that case, we return
 * right away with SSH_KMF_NOT_CERT.
 *
 * Returns SSH_KMF_CERT_FOUND, SSH_KMF_CERT_BUT_NO_KMF_SESS, or
 * SSH_KMF_NOT_CERT.
 */
int
ssh_kmf_key_from_blob(uchar_t *blob, int blen, Key **key)
{
	int ret, type;
	KMF_RETURN rv;
	KMF_DATA kblob;
	KMF_ENCODE_FORMAT format;
	kmf_key_t *kmf_key = NULL;

	debug("ssh_kmf_key_from_blob: blob length is %d.", blen);

	kblob.Length = blen;
	kblob.Data = blob;

	if (kmf_global_handle == (KMF_HANDLE_T)NULL) {
		/*
		 * We can find out whether the blob contains a certificate even
		 * without the KMF handle but for anything else we really need
		 * it.
		 */
		if (kmf_is_cert_data(&kblob, &format) != KMF_OK)
			return (SSH_KMF_NOT_CERT);
		else {
			error("Cert in blob but no global KMF handle to "
			    "continue.");
			*key = NULL;
			return (SSH_KMF_CERT_BUT_NO_KMF_SESS);
		}
	}

	ret = ssh_kmf_get_cert_pubkey_alg(kmf_global_handle, &kblob, &type);
	if (ret == SSH_KMF_CERT_FOUND) {
		char *subj;

		/*
		 * Uninitialized fields must be NULL so that we do not try to
		 * free the non-existent URI string in key_free(), for example.
		 */
		kmf_key = xcalloc(1, sizeof (kmf_key_t));
		*key = x509_key_new(type, kmf_key);
		/*
		 * We need the handle if we print the key. That can happen in
		 * ssh-keygen, for example.
		 */
		(*key)->kmf_key->h = kmf_global_handle;
		(*key)->kmf_key->cert.certificate.Length = blen;
		(*key)->kmf_key->cert.certificate.Data =
		    ssh_kmf_data_dup(&kblob);
		(*key)->kmf_key->from_blob = 1;

		rv = kmf_get_cert_subject_str(kmf_global_handle,
		    &(*key)->kmf_key->cert.certificate, &subj);
		/*
		 * If we do not get the subject we will take care of that later,
		 * here it's just for debugging.
		 */
		if (rv == KMF_OK) {
			debug("Certificate subject from blob is '%s'.", subj);
			free(subj);
		}
	}

	return (ret);
}

/*
 * The function returns SSH_KMF_NOT_SELF_SIGNED_CERT, SSH_KMF_SELF_SIGNED_CERT,
 * or SSH_KMF_CORRUPTED_CERT if there is any error while processing the
 * certificate. Non-NULL user_issuer and/or user_subject will contain newly
 * allocated strings unless SSH_KMF_CORRUPTED_CERT is returned.
 */
int
ssh_kmf_is_cert_self_signed(KMF_HANDLE_T h, KMF_DATA *pcert,
    char **user_subject, char **user_issuer)
{
	KMF_RETURN rv;
	int ret = SSH_KMF_CORRUPTED_CERT;
	char *subj = NULL, *issuer = NULL;
	KMF_X509_NAME user_issuerDN, user_subjectDN;

	/* Get the subject information from the input certficate. */
	if ((rv = kmf_get_cert_subject_str(h, pcert, &subj)) != KMF_OK) {
		ssh_kmf_debug(h, "kmf_get_cert_subject_str", rv);
		return (ret);
	} else if ((rv = kmf_dn_parser(subj, &user_subjectDN)) != KMF_OK) {
		ssh_kmf_debug(h, "kmf_dn_parser", rv);
		goto err;
	}

	/* Get the issuer information from the input certficate. */
	if ((rv = kmf_get_cert_issuer_str(h, pcert, &issuer)) != KMF_OK) {
		ssh_kmf_debug(h, "kmf_get_cert_issuer_str", rv);
		goto err2;
	} else if ((rv = kmf_dn_parser(issuer, &user_issuerDN)) != KMF_OK) {
		ssh_kmf_debug(h, "kmf_dn_parser", rv);
		goto err3;
	}

	debug("ssh_kmf_is_cert_self_signed: '%s' vs. '%s'", subj, issuer);
	/* If subject and issuer are the same it's a self-signed certificate. */
	if (kmf_compare_rdns(&user_issuerDN, &user_subjectDN) == 0)
		ret = SSH_KMF_SELF_SIGNED_CERT;
	else
		ret = SSH_KMF_NOT_SELF_SIGNED_CERT;

	kmf_free_dn(&user_subjectDN);
	kmf_free_dn(&user_issuerDN);
	/* Fill out output parameters if non-NULL. */
	if (user_subject != NULL)
		*user_subject = subj;
	else
		free(subj);
	if (user_issuer != NULL)
		*user_issuer = issuer;
	else
		free(issuer);
	return (ret);
err3:
	free(issuer);
err2:
	kmf_free_dn(&user_subjectDN);
err:
	free(subj);
	return (ret);
}

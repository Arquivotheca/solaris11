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

#include <openssl/err.h>
#include <fcntl.h>
#include <stdio.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/nvpair.h>
#include <unistd.h>

#include <fm/libasr.h>

#include "asr_base64.h"
#include "asr_ssl.h"

/*
 * Handles an SSL error by getting the SSL error message and setting it to
 * the ASR error message.
 */
static void
asr_ssl_set_err()
{
	char ebuf[128];
	unsigned long e = ERR_get_error();
	ERR_error_string_n(e, ebuf, sizeof (ebuf));

	(void) asr_error(EASR_SSL_LIBSSL, ebuf);
}

/*
 * Generates a new RSA key with the given key length.
 */
RSA *
asr_ssl_rsa_keygen(size_t keylen)
{
	RSA *rsa;

	if ((rsa = RSA_generate_key(
	    keylen, ASR_SSL_EXPONENT, NULL, NULL)) == NULL) {
		asr_ssl_set_err();
		return (NULL);
	}

	return (rsa);
}

/*
 * Creates an RSA key from a text encoded private key
 */
static RSA *
asr_ssl_get_key(char *privkey)
{
	BIO *bp = NULL;
	RSA *rsa = NULL;

	if (privkey == NULL) {
		(void) asr_error(EASR_SSL_LIBSSL, "Private key is null");
		return (NULL);
	}
	bp = BIO_new_mem_buf(privkey, -1);
	if (bp == NULL) {
		asr_ssl_set_err();
		return (NULL);
	}
	rsa = PEM_read_bio_RSAPrivateKey(bp, NULL, NULL, NULL);
	(void) BIO_free(bp);
	if (rsa == NULL) {
		asr_ssl_set_err();
		return (NULL);
	}
	return (rsa);
}

/*
 * Creates a SHA1 RSA encoded signature
 */
static char *
asr_ssl_rsa_sign(RSA *rsa, const unsigned char *msg,
		unsigned int msglen, unsigned int *siglen)
{
	unsigned char *sig;
	unsigned char digest[SHA_DIGEST_LENGTH];

	(void) SHA1(msg, msglen, digest);

	if ((sig = malloc(RSA_size(rsa))) == NULL)
		return (NULL);

	if (RSA_sign(NID_sha1, digest, sizeof (digest), sig, siglen, rsa)
	    != 1) {
		asr_ssl_set_err();
		free(sig);
		return (NULL);
	}

	return ((char *)sig);
}

/*
 * Creates a text encoded version of the RSA private key.
 */
char *
asr_ssl_rsa_private_pem(RSA *rsa)
{
	BIO *bio;
	char *bdata, *pem;
	long sz;

	if ((bio = BIO_new(BIO_s_mem())) == NULL) {
		(void) asr_error(EASR_SSL_LIBSSL, "failed to alloc BIO");
		return (NULL);
	}

	if (PEM_write_bio_RSAPrivateKey(
	    bio, rsa, NULL, NULL, 0, NULL, NULL) != 1) {
		BIO_vfree(bio);
		(void) asr_error(EASR_SSL_LIBSSL, "failed to write RSA key");
		return (NULL);
	}

	sz = BIO_get_mem_data(bio, &bdata);
	if ((pem = malloc(sz + 1)) == NULL) {
		(void) asr_error(EASR_SSL_LIBSSL, "failed to alloc pem");
		(BIO_vfree(bio));
		return (NULL);
	}
	bcopy(bdata, pem, sz);
	pem[sz] = '\0';

	BIO_vfree(bio);

	return (pem);
}

/*
 * Creates a text encoded version of the RSA public key.
 */
char *
asr_ssl_rsa_public_pem(RSA *rsa)
{
	BIO *bio;
	char *bdata, *pem;
	long sz;

	if ((bio = BIO_new(BIO_s_mem())) == NULL) {
		asr_ssl_set_err();
		return (NULL);
	}

	/*
	 * Do not confuse PEM_write_bio_RSA_PUBKEY with
	 * PEM_write_bio_RSAPublicKey. Both write out the public key in formats
	 * that appear similar, but are decidedly not compatible.
	 */
	if (PEM_write_bio_RSA_PUBKEY(bio, rsa) != 1) {
		BIO_vfree(bio);
		asr_ssl_set_err();
		return (NULL);
	}

	sz = BIO_get_mem_data(bio, &bdata);
	if ((pem = malloc(sz + 1)) == NULL) {
		(void) asr_set_errno(EASR_NOMEM);
		return (NULL);
	}

	bcopy(bdata, pem, sz);
	pem[sz] = '\0';

	BIO_vfree(bio);

	return (pem);
}

/*
 * Creates a SHA1 RSA encoded signature.
 */
char *
asr_ssl_sign(char *priv_key, const unsigned char *msg,
		unsigned int msglen, unsigned int *siglen)
{
	RSA *key = asr_ssl_get_key(priv_key);
	char *sig;
	if (key == NULL) {
		return (NULL);
	}
	sig = asr_ssl_rsa_sign(key, msg, msglen, siglen);
	RSA_free(key);
	return (sig);
}

/*
 * Creates a SHA1 RSA Base 64 encoded signature.
 * If there is an error then NULL is returned.  The returned buffer will
 * have to be released when no longer needed.
 */
char *
asr_ssl_sign64(char *priv_key, const char *msg, unsigned int msglen)
{
	char *sig64;
	unsigned int siglen;
	char *sig = asr_ssl_sign(
	    priv_key, (unsigned char *)msg, msglen, &siglen);

	if (sig == NULL)
		return (NULL);
	sig64 = asr_b64_encode(sig, siglen);
	free(sig);
	return (sig64);
}

/*
 * Creates a SHA1 RSA signature from a prefix buffer and a full message
 * buffer.
 */
char *
asr_ssl_sign_pre(char *priv_key,
    const unsigned char *prefix, unsigned int prelen,
    const unsigned char *msg, unsigned int msglen,
    unsigned int *siglen)
{
	SHA_CTX ctx;
	unsigned char *sig = NULL;
	unsigned char digest[SHA_DIGEST_LENGTH];
	RSA *key = asr_ssl_get_key(priv_key);

	if (key == NULL) {
		return (NULL);
	}

	if (SHA1_Init(&ctx) != 1) {
		asr_ssl_set_err();
		goto finally;
	}
	if (SHA1_Update(&ctx, prefix, prelen) != 1) {
		asr_ssl_set_err();
		(void) SHA1_Final(digest, &ctx);
		goto finally;
	}
	if (SHA1_Update(&ctx, msg, msglen) != 1) {
		asr_ssl_set_err();
		(void) SHA1_Final(digest, &ctx);
		goto finally;
	}
	if (SHA1_Final(digest, &ctx) != 1) {
		asr_ssl_set_err();
		goto finally;
	}
	if ((sig = malloc(RSA_size(key))) == NULL)
		goto finally;

	if (RSA_sign(NID_sha1, digest, sizeof (digest), sig, siglen, key)
	    != 1) {
		asr_ssl_set_err();
		free(sig);
		sig = NULL;
		goto finally;
	}

finally:
	if (key != NULL)
		RSA_free(key);
	return ((char *)sig);
}

/*
 * Creates a new AES key
 */
static int
asr_ssl_populate_aes_key(unsigned char *key, int key_len,
    unsigned char *iv, int iv_len)
{
	int err = ASR_OK;
	FILE *random = fopen("/dev/random", "r");

	if (random == NULL)
		return (ASR_FAILURE);

	if (fread(key, 1, key_len, random) != key_len)
		err = ASR_FAILURE;

	if (err == ASR_OK && fread(iv, 1, iv_len, random) != iv_len)
		err = ASR_FAILURE;

	(void) fclose(random);
	return (err);

}

/*
 * Writes out an AES key to the given file.
 */
static int
asr_ssl_write_aes_key(char *path, unsigned char *key, int key_len,
    unsigned char *iv, int iv_len)
{
	int err = ASR_OK;
	FILE *out = fopen(path, "w");
	if (out == NULL)
		return (ASR_FAILURE);

	if (fwrite(key, 1, key_len, out) != key_len)
		err = ASR_FAILURE;
	if (fwrite(iv, 1, iv_len, out) != iv_len)
		err = ASR_FAILURE;
	(void) fclose(out);
	if (chmod(path, 0600) != 0) {
		err = ASR_FAILURE;
	}

	return (err);
}

/*
 * Reads in an AES key from the given file.
 */
static int
asr_ssl_read_aes_key(char *key_path, unsigned char *key, int key_len,
    unsigned char *iv, int iv_len)
{
	FILE *key_file;
	int result = ASR_OK;

	key_file = fopen(key_path, "r");
	if (key_file == NULL)
		return (ASR_FAILURE);

	if (fread(key, 1, key_len, key_file) != key_len ||
	    fread(iv, 1, iv_len, key_file) != iv_len)
		result = ASR_FAILURE;

	(void) fclose(key_file);
	return (result);
}

/*
 * Reads in encrypted data from a file and returns a buffer and length value
 */
static int
asr_ssl_read_data(char *data_path, unsigned char **data, int *dlen)
{
	FILE *f = fopen(data_path, "r");
	*data = NULL;

	if (f == NULL)
		return (ASR_FAILURE);

	if (fseek(f, 0, SEEK_END) != 0) {
		(void) fclose(f);
		return (ASR_FAILURE);
	}
	*dlen = ftell(f);
	if (*dlen <= 0) {
		(void) fclose(f);
		return (ASR_FAILURE);
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		(void) fclose(f);
		return (ASR_FAILURE);
	}

	*data = malloc(*dlen);
	if (*data == NULL) {
		(void) fclose(f);
		return (ASR_FAILURE);
	}

	if (*dlen != fread(*data, 1, *dlen, f)) {
		free(*data);
		*data = NULL;
		(void) fclose(f);
		return (ASR_FAILURE);
	}

	(void) fclose(f);
	return (ASR_OK);
}

/*
 * Reads in sensitive properties that have been obfuscated and adds them to
 * the name value list as strings.
 */
int
asr_ssl_read_aes(char *key_path, char *data_path, char **data, int *dlen)
{
	EVP_CIPHER_CTX ctx;
	unsigned char key[ASR_SSL_AES_KEYLEN], iv[ASR_SSL_AES_KEYLEN];
	int enc_len, ptxt_len, final_len;
	unsigned char *enc_data, *plaintext;
	int err = ASR_OK;

	*data = NULL;
	*dlen = 0;

	/* read key, iv */
	if (asr_ssl_read_aes_key(key_path, key,
	    ASR_SSL_AES_KEYLEN, iv, ASR_SSL_AES_KEYLEN) != ASR_OK)
		return (ASR_FAILURE);

	if (asr_ssl_read_data(data_path, &enc_data, &enc_len) != ASR_OK)
		return (ASR_FAILURE);

	if ((plaintext = malloc(enc_len)) == NULL) {
		free(enc_data);
		return (ASR_FAILURE);
	}

	EVP_CIPHER_CTX_init(&ctx);
	if (EVP_DecryptInit_ex(&ctx, EVP_aes_128_cbc(), NULL, key, iv) == 0)
		err = ASR_FAILURE;
	if (err != ASR_FAILURE && EVP_DecryptUpdate(
	    &ctx, plaintext, &ptxt_len, enc_data, enc_len) == 0)
		err = ASR_FAILURE;
	if (err != ASR_FAILURE && EVP_DecryptFinal_ex(
	    &ctx, plaintext + ptxt_len, &final_len) == 0)
		err = ASR_FAILURE;
	if (EVP_CIPHER_CTX_cleanup(&ctx) == 0)
		err = ASR_FAILURE;

	if (err != ASR_OK) {
		asr_ssl_set_err();
		free(plaintext);
	} else {
		*data = (char *)plaintext;
		*dlen = final_len + ptxt_len;
	}
	free(enc_data);

	return (err);
}

/*
 * Reads in an AES encrypted and XDR encoded data steam into an nvlist
 */
int
asr_ssl_read_aes_nvl(char *key_path, char *data_path, nvlist_t **outlist)
{
	char *data;
	int dlen;

	*outlist = NULL;
	if (asr_ssl_read_aes(key_path, data_path, &data, &dlen) != ASR_OK)
		return (ASR_FAILURE);

	if (nvlist_unpack(data, dlen, outlist, 0) != 0) {
		free(data);
		(void) asr_set_errno(EASR_NVLIST);
		return (ASR_FAILURE);
	}

	free(data);
	return (ASR_OK);
}

/*
 * Writes the already encrypted data to a file.
 */
static int
asr_ssl_write_data(char *data_path, unsigned char *data, int dlen)
{
	int err = ASR_OK;
	FILE *out = fopen(data_path, "w");
	if (out == NULL)
		return (ASR_FAILURE);

	if (fwrite(data, 1, dlen, out) != dlen)
		err = ASR_FAILURE;

	if (fclose(out) != 0)
		err = ASR_FAILURE;
	return (err);
}

/*
 * Writes out name value properties that need to be obfuscated.
 * A key file stores the AES key and the encrypted values are written
 * out to a file defined by the data_path.
 * Only properties in the names list will be saved, unless names is NULL, in
 * which case all values will be saved.
 * The function returns 0 on success and -1 on error.
 */
static int
asr_ssl_write_aes(char *key_path, char *data_path,
    unsigned char *data, int dlen)
{
	EVP_CIPHER_CTX ctx;
	unsigned char key[ASR_SSL_AES_KEYLEN], iv[ASR_SSL_AES_KEYLEN];
	unsigned char *cipher_text = NULL;
	int cipher_len, final_len, blen;
	int err = ASR_OK;

	/* read/create key, iv */
	if (ASR_OK != asr_ssl_read_aes_key(
	    key_path, key, ASR_SSL_AES_KEYLEN, iv, ASR_SSL_AES_KEYLEN)) {
		if (ASR_FAILURE == asr_ssl_populate_aes_key(
		    key, ASR_SSL_AES_KEYLEN, iv, ASR_SSL_AES_KEYLEN) ||
		    ASR_FAILURE == asr_ssl_write_aes_key(
		    key_path, key, ASR_SSL_AES_KEYLEN, iv, ASR_SSL_AES_KEYLEN))
			return (ASR_FAILURE);
	}


	/* Encrypt the config data into cipher_text */
	EVP_CIPHER_CTX_init(&ctx);

	if (EVP_EncryptInit_ex(
	    &ctx, EVP_aes_128_cbc(), NULL, key, iv) == 0)
		err = ASR_FAILURE;

	blen = EVP_CIPHER_CTX_block_size(&ctx);
	cipher_len = dlen + blen;
	if ((cipher_text = malloc(cipher_len)) == NULL) {
		(void) asr_set_errno(EASR_NOMEM);
		return (ASR_FAILURE);
	}

	if (err != ASR_FAILURE && EVP_EncryptUpdate(
	    &ctx, cipher_text, &cipher_len, data, dlen) == 0)
		err = ASR_FAILURE;

	if (err != ASR_FAILURE && EVP_EncryptFinal_ex(
	    &ctx, cipher_text + cipher_len, &final_len) == 0)
		err = ASR_FAILURE;

	(void) EVP_CIPHER_CTX_cleanup(&ctx);

	/* write the data to the file */
	if (err == ASR_OK)
		err = asr_ssl_write_data(
		    data_path, cipher_text, cipher_len + final_len);

	if (cipher_text != NULL)
		free(cipher_text);
	return (err);
}

/*
 * Writes out the nvlist to an XDR encoded and AES encrypted file
 */
int
asr_ssl_write_aes_config(char *key_path, char *data_path, nvlist_t *nvl)
{
	size_t nvsize;
	unsigned char *nvbuf;
	int err;

	/* Write XDR encoded properties to buffer */
	(void) nvlist_size(nvl, &nvsize, NV_ENCODE_XDR);
	if ((nvbuf = malloc(nvsize)) == NULL)
		return (ASR_FAILURE);
	(void) nvlist_pack(nvl, (char **)&nvbuf, &nvsize, NV_ENCODE_XDR, 0);

	/* Write encrypted XDR buffer to file */
	err = asr_ssl_write_aes(key_path, data_path, nvbuf, nvsize);
	free(nvbuf);
	return (err);
}

/*
 * Writes out the nvlist items that start with the given filter
 * to an XDR encoded and AES encrypted file
 */
int
asr_ssl_write_aes_config_names(char *key_path, char *data_path, nvlist_t *nvl,
    const char *filter)
{
	nvpair_t *nvp;
	nvlist_t *list = NULL;
	int err = 0;
	int flen = filter == NULL ? 0 : strlen(filter);

	if (nvlist_alloc(&list, NV_UNIQUE_NAME, 0) != 0)
		return (ASR_FAILURE);

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL && err == 0;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		char *name = nvpair_name(nvp);

		if (flen == 0 || strncmp(filter, name, flen) != 0)
			continue;

		if (nvpair_type(nvp) == DATA_TYPE_STRING) {
			char *val;
			(void) nvpair_value_string(nvp, &val);
			if (nvlist_add_string(list, name, val) != 0) {
				err = ASR_FAILURE;
				goto cleanup;
			}
		}
	}
	err = asr_ssl_write_aes_config(key_path, data_path, list);
cleanup:
	if (list != NULL)
		nvlist_free(list);
	return (err);
}

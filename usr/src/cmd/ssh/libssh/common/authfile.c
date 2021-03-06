/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * This file contains functions for reading and writing identity files, and
 * for reading the passphrase from the user.
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 *
 *
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "includes.h"
RCSID("$OpenBSD: authfile.c,v 1.50 2002/06/24 14:55:38 markus Exp $");

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <cryptoutil.h>

#include "cipher.h"
#include "xmalloc.h"
#include "buffer.h"
#include "bufaux.h"
#include "key.h"
#include "ssh.h"
#include "log.h"
#include "authfile.h"
#include "rsa.h"
#include "kmf_pkcs11.h"
#include "kmf.h"

/* Version identification string for SSH v1 identity files. */
static const char authfile_id_string[] =
    "SSH PRIVATE KEY FILE FORMAT 1.1\n";

/*
 * Saves the authentication (private) key in a file, encrypting it with
 * passphrase.  The identification of the file (lowest 64 bits of n) will
 * precede the key to provide identification of the key without needing a
 * passphrase.
 */

static int
key_save_private_rsa1(Key *key, const char *filename, const char *passphrase,
    const char *comment)
{
	Buffer buffer, encrypted;
	u_char buf[100], *cp;
	int fd, i, cipher_num;
	CipherContext ciphercontext;
	Cipher *cipher;
	u_int32_t rand;

	/*
	 * If the passphrase is empty, use SSH_CIPHER_NONE to ease converting
	 * to another cipher; otherwise use SSH_AUTHFILE_CIPHER.
	 */
	cipher_num = (strcmp(passphrase, "") == 0) ?
	    SSH_CIPHER_NONE : SSH_AUTHFILE_CIPHER;
	if ((cipher = cipher_by_number(cipher_num)) == NULL)
		fatal("save_private_key_rsa: bad cipher");

	/* This buffer is used to built the secret part of the private key. */
	buffer_init(&buffer);

	/* Put checkbytes for checking passphrase validity. */
	rand = arc4random();
	buf[0] = rand & 0xff;
	buf[1] = (rand >> 8) & 0xff;
	buf[2] = buf[0];
	buf[3] = buf[1];
	buffer_append(&buffer, buf, 4);

	/*
	 * Store the private key (n and e will not be stored because they
	 * will be stored in plain text, and storing them also in encrypted
	 * format would just give known plaintext).
	 */
	buffer_put_bignum(&buffer, key->rsa->d);
	buffer_put_bignum(&buffer, key->rsa->iqmp);
	buffer_put_bignum(&buffer, key->rsa->q);	/* reverse from SSL p */
	buffer_put_bignum(&buffer, key->rsa->p);	/* reverse from SSL q */

	/* Pad the part to be encrypted until its size is a multiple of 8. */
	while (buffer_len(&buffer) % 8 != 0)
		buffer_put_char(&buffer, 0);

	/* This buffer will be used to contain the data in the file. */
	buffer_init(&encrypted);

	/* First store keyfile id string. */
	for (i = 0; authfile_id_string[i]; i++)
		buffer_put_char(&encrypted, authfile_id_string[i]);
	buffer_put_char(&encrypted, 0);

	/* Store cipher type. */
	buffer_put_char(&encrypted, cipher_num);
	buffer_put_int(&encrypted, 0);	/* For future extension */

	/* Store public key.  This will be in plain text. */
	buffer_put_int(&encrypted, BN_num_bits(key->rsa->n));
	buffer_put_bignum(&encrypted, key->rsa->n);
	buffer_put_bignum(&encrypted, key->rsa->e);
	buffer_put_cstring(&encrypted, comment);

	/* Allocate space for the private part of the key in the buffer. */
	cp = buffer_append_space(&encrypted, buffer_len(&buffer));

	cipher_set_key_string(&ciphercontext, cipher, passphrase,
	    CIPHER_ENCRYPT);
	cipher_crypt(&ciphercontext, cp,
	    buffer_ptr(&buffer), buffer_len(&buffer));
	cipher_cleanup(&ciphercontext);
	memset(&ciphercontext, 0, sizeof(ciphercontext));

	/* Destroy temporary data. */
	memset(buf, 0, sizeof(buf));
	buffer_free(&buffer);

	fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		error("open %s failed: %s.", filename, strerror(errno));
		return 0;
	}
	if (write(fd, buffer_ptr(&encrypted), buffer_len(&encrypted)) !=
	    buffer_len(&encrypted)) {
		error("write to key file %s failed: %s", filename,
		    strerror(errno));
		buffer_free(&encrypted);
		close(fd);
		unlink(filename);
		return 0;
	}
	close(fd);
	buffer_free(&encrypted);
	return 1;
}

/* save SSH v2 key in OpenSSL PEM format */
static int
key_save_private_pem(Key *key, const char *filename, const char *_passphrase,
    const char *comment)
{
	FILE *fp;
	int fd;
	int success = 0;
	int len = strlen(_passphrase);
	u_char *passphrase = (len > 0) ? (u_char *)_passphrase : NULL;
	const EVP_CIPHER *cipher = (len > 0) ? EVP_aes_128_cbc() : NULL;

	if (len > 0 && len <= 4) {
		error("passphrase too short: have %d bytes, need > 4", len);
		return 0;
	}
	fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		error("open %s failed: %s.", filename, strerror(errno));
		return 0;
	}
	fp = fdopen(fd, "w");
	if (fp == NULL ) {
		error("fdopen %s failed: %s.", filename, strerror(errno));
		close(fd);
		return 0;
	}
	switch (key->type) {
	case KEY_DSA:
		success = PEM_write_DSAPrivateKey(fp, key->dsa,
		    cipher, passphrase, len, NULL, NULL);
		break;
	case KEY_RSA:
		success = PEM_write_RSAPrivateKey(fp, key->rsa,
		    cipher, passphrase, len, NULL, NULL);
		break;
	}
	fclose(fp);
	return success;
}

int
key_save_private(Key *key, const char *filename, const char *passphrase,
    const char *comment)
{
	switch (key->type) {
	case KEY_RSA1:
		return key_save_private_rsa1(key, filename, passphrase,
		    comment);
	case KEY_DSA:
	case KEY_RSA:
		return key_save_private_pem(key, filename, passphrase,
		    comment);
	default:
		break;
	}
	error("key_save_private: cannot save key type %d", key->type);
	return 0;
}

/*
 * Loads the public part of the ssh v1 key file.  Returns NULL if an error was
 * encountered (the file does not exist or is not readable), and the key
 * otherwise.
 */

static Key *
key_load_public_rsa1(int fd, const char *filename, char **commentp)
{
	Buffer buffer;
	Key *pub;
	char *cp;
	int i;
	off_t len;

	len = lseek(fd, (off_t) 0, SEEK_END);
	lseek(fd, (off_t) 0, SEEK_SET);

	buffer_init(&buffer);
	cp = buffer_append_space(&buffer, len);

	if (read(fd, cp, (size_t) len) != (size_t) len) {
		debug("Read from key file %.200s failed: %.100s", filename,
		    strerror(errno));
		buffer_free(&buffer);
		return NULL;
	}

	/* Check that it is at least big enough to contain the ID string. */
	if (len < sizeof(authfile_id_string)) {
		debug3("Not a RSA1 key file %.200s.", filename);
		buffer_free(&buffer);
		return NULL;
	}
	/*
	 * Make sure it begins with the id string.  Consume the id string
	 * from the buffer.
	 */
	for (i = 0; i < sizeof(authfile_id_string); i++)
		if (buffer_get_char(&buffer) != authfile_id_string[i]) {
			debug3("Not a RSA1 key file %.200s.", filename);
			buffer_free(&buffer);
			return NULL;
		}
	/* Skip cipher type and reserved data. */
	(void) buffer_get_char(&buffer);	/* cipher type */
	(void) buffer_get_int(&buffer);		/* reserved */

	/* Read the public key from the buffer. */
	(void) buffer_get_int(&buffer);
	pub = key_new(KEY_RSA1);
	buffer_get_bignum(&buffer, pub->rsa->n);
	buffer_get_bignum(&buffer, pub->rsa->e);
	if (commentp)
		*commentp = buffer_get_string(&buffer, NULL);
	/* The encrypted private part is not parsed by this function. */

	buffer_free(&buffer);
	return pub;
}

/* load public key from private-key file, works only for SSH v1 */
Key *
key_load_public_type(int type, const char *filename, char **commentp)
{
	Key *pub;
	int fd;

	if (type == KEY_RSA1) {
		fd = open(filename, O_RDONLY);
		if (fd < 0)
			return NULL;
		pub = key_load_public_rsa1(fd, filename, commentp);
		close(fd);
		return pub;
	}
	return NULL;
}

/*
 * Loads the private key from the file.  Returns 0 if an error is encountered
 * (file does not exist or is not readable, or passphrase is bad). This
 * initializes the private key.
 * Assumes we are called under uid of the owner of the file.
 */

static Key *
key_load_private_rsa1(int fd, const char *filename, const char *passphrase,
    char **commentp)
{
	int i, check1, check2, cipher_type;
	off_t len;
	Buffer buffer, decrypted;
	u_char *cp;
	CipherContext ciphercontext;
	Cipher *cipher;
	Key *prv = NULL;

	len = lseek(fd, (off_t) 0, SEEK_END);
	lseek(fd, (off_t) 0, SEEK_SET);

	buffer_init(&buffer);
	cp = buffer_append_space(&buffer, len);

	if (read(fd, cp, (size_t) len) != (size_t) len) {
		debug("Read from key file %.200s failed: %.100s", filename,
		    strerror(errno));
		buffer_free(&buffer);
		close(fd);
		return NULL;
	}

	/* Check that it is at least big enough to contain the ID string. */
	if (len < sizeof(authfile_id_string)) {
		debug3("Not a RSA1 key file %.200s.", filename);
		buffer_free(&buffer);
		close(fd);
		return NULL;
	}
	/*
	 * Make sure it begins with the id string.  Consume the id string
	 * from the buffer.
	 */
	for (i = 0; i < sizeof(authfile_id_string); i++)
		if (buffer_get_char(&buffer) != authfile_id_string[i]) {
			debug3("Not a RSA1 key file %.200s.", filename);
			buffer_free(&buffer);
			close(fd);
			return NULL;
		}

	/* Read cipher type. */
	cipher_type = buffer_get_char(&buffer);
	(void) buffer_get_int(&buffer);	/* Reserved data. */

	/* Read the public key from the buffer. */
	(void) buffer_get_int(&buffer);
	prv = key_new_private(KEY_RSA1);

	buffer_get_bignum(&buffer, prv->rsa->n);
	buffer_get_bignum(&buffer, prv->rsa->e);
	if (commentp)
		*commentp = buffer_get_string(&buffer, NULL);
	else
		xfree(buffer_get_string(&buffer, NULL));

	/* Check that it is a supported cipher. */
	cipher = cipher_by_number(cipher_type);
	if (cipher == NULL) {
		debug("Unsupported cipher %d used in key file %.200s.",
		    cipher_type, filename);
		buffer_free(&buffer);
		goto fail;
	}
	/* Initialize space for decrypted data. */
	buffer_init(&decrypted);
	cp = buffer_append_space(&decrypted, buffer_len(&buffer));

	/* Rest of the buffer is encrypted.  Decrypt it using the passphrase. */
	cipher_set_key_string(&ciphercontext, cipher, passphrase,
	    CIPHER_DECRYPT);
	cipher_crypt(&ciphercontext, cp,
	    buffer_ptr(&buffer), buffer_len(&buffer));
	cipher_cleanup(&ciphercontext);
	memset(&ciphercontext, 0, sizeof(ciphercontext));
	buffer_free(&buffer);

	check1 = buffer_get_char(&decrypted);
	check2 = buffer_get_char(&decrypted);
	if (check1 != buffer_get_char(&decrypted) ||
	    check2 != buffer_get_char(&decrypted)) {
		if (strcmp(passphrase, "") != 0)
			debug("Bad passphrase supplied for key file %.200s.",
			    filename);
		/* Bad passphrase. */
		buffer_free(&decrypted);
		goto fail;
	}
	/* Read the rest of the private key. */
	buffer_get_bignum(&decrypted, prv->rsa->d);
	buffer_get_bignum(&decrypted, prv->rsa->iqmp);		/* u */
	/* in SSL and SSH v1 p and q are exchanged */
	buffer_get_bignum(&decrypted, prv->rsa->q);		/* p */
	buffer_get_bignum(&decrypted, prv->rsa->p);		/* q */

	/* calculate p-1 and q-1 */
	rsa_generate_additional_parameters(prv->rsa);

	buffer_free(&decrypted);
	close(fd);
	return prv;

fail:
	if (commentp)
		xfree(*commentp);
	close(fd);
	key_free(prv);
	return NULL;
}

Key *
key_load_private_pem(int fd, int type, const char *passphrase,
    char **commentp)
{
	FILE *fp;
	EVP_PKEY *pk = NULL;
	Key *prv = NULL;
	char *name = "<no key>";

	fp = fdopen(fd, "r");
	if (fp == NULL) {
		error("fdopen failed: %s", strerror(errno));
		close(fd);
		return NULL;
	}
	pk = PEM_read_PrivateKey(fp, NULL, NULL, (char *)passphrase);
	if (pk == NULL) {
		debug("PEM_read_PrivateKey failed");
		(void)ERR_get_error();
	} else if (pk->type == EVP_PKEY_RSA &&
	    (type == KEY_UNSPEC||type==KEY_RSA)) {
		prv = key_new(KEY_UNSPEC);
		prv->rsa = EVP_PKEY_get1_RSA(pk);
		prv->type = KEY_RSA;
		name = "rsa w/o comment";
#ifdef DEBUG_PK
		RSA_print_fp(stderr, prv->rsa, 8);
#endif
	} else if (pk->type == EVP_PKEY_DSA &&
	    (type == KEY_UNSPEC||type==KEY_DSA)) {
		prv = key_new(KEY_UNSPEC);
		prv->dsa = EVP_PKEY_get1_DSA(pk);
		prv->type = KEY_DSA;
		name = "dsa w/o comment";
#ifdef DEBUG_PK
		DSA_print_fp(stderr, prv->dsa, 8);
#endif
	} else {
		error("PEM_read_PrivateKey: mismatch or "
		    "unknown EVP_PKEY save_type %d", pk->save_type);
	}
	fclose(fp);
	if (pk != NULL)
		EVP_PKEY_free(pk);
	if (prv != NULL && commentp)
		*commentp = xstrdup(name);
	debug("read PEM private key done: type %s",
	    prv ? key_type(prv) : "<unknown>");
	return prv;
}

static int
key_perm_ok(int fd, const char *filename)
{
	struct stat st;

	if (fstat(fd, &st) < 0)
		return 0;
	/*
	 * if a key owned by the user is accessed, then we check the
	 * permissions of the file. if the key owned by a different user,
	 * then we don't care.
	 */
#ifdef HAVE_CYGWIN
	if (check_ntsec(filename))
#endif
	if ((st.st_uid == getuid()) && (st.st_mode & 077) != 0) {
		error("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
		error("@         WARNING: UNPROTECTED PRIVATE KEY FILE!          @");
		error("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
		error("Permissions 0%3.3o for '%s' are too open.",
		    (int)(st.st_mode & 0777), filename);
		error("It is recommended that your private key files are NOT accessible by others.");
		error("This private key will be ignored.");
		return 0;
	}
	return 1;
}

Key *
key_load_private_type(int type, const char *filename, const char *passphrase,
    char **commentp)
{
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;
	if (!key_perm_ok(fd, filename)) {
		error("bad permissions: ignore key: %s", filename);
		close(fd);
		return NULL;
	}
	switch (type) {
	case KEY_RSA1:
		return key_load_private_rsa1(fd, filename, passphrase,
		    commentp);
		/* closes fd */
	case KEY_DSA:
	case KEY_RSA:
	case KEY_UNSPEC:
		return key_load_private_pem(fd, type, passphrase, commentp);
		/* closes fd */
	default:
		close(fd);
		break;
	}
	return NULL;
}

/*
 * The function is used by the SSH server and ssh-(add|keygen) commands but not
 * by the SSH client. For user identities used in ssh(1) there is
 * load_identity_file() function.
 *
 * The 'ask_for_pin' parameter tells whether we want to read the PIN from a
 * terminal in case the 'pinfile' attribute is missing from the URI. For
 * example, the server never reads the PIN from a terminal. On the other hand,
 * ssh-add needs the PIN after it verified that the private key did exist since
 * the PIN would be sent to the SSH agent then.
 */
Key *
key_load_private(const char *filename, const char *passphrase,
    char **commentp, int ask_for_pin)
{
	Key *pub, *prv;
	int fd, ret;

	debug("key_load_private: loading %s", filename);

	/* First, see if the filename contains a PKCS#11 URI. */
	ret = ssh_kmf_key_load_private(filename, &prv, ask_for_pin, NULL);
	if (ret == SSH_KMF_PRIV_KEY_OK)
		return (prv);
	else if (ret != SSH_KMF_NOT_PKCS11_URI)
		return (NULL);

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;
	if (!key_perm_ok(fd, filename)) {
		error("bad permissions: ignore key: %s", filename);
		close(fd);
		return NULL;
	}
	pub = key_load_public_rsa1(fd, filename, commentp);
	lseek(fd, (off_t) 0, SEEK_SET);		/* rewind */
	if (pub == NULL) {
		/* closes fd */
		prv = key_load_private_pem(fd, KEY_UNSPEC, passphrase, NULL);
		/* use the filename as a comment for PEM */
		if (commentp && prv)
			*commentp = xstrdup(filename);
	} else {
		/* it's a SSH v1 key if the public key part is readable */
		key_free(pub);
		/* closes fd */
		prv = key_load_private_rsa1(fd, filename, passphrase, NULL);
	}
	return prv;
}

static int
key_try_load_public(Key *k, const char *filename, char **commentp)
{
	FILE *f;
	char *cp, line[4096];

	f = fopen(filename, "r");
	if (f != NULL) {
		while (fgets(line, sizeof(line), f)) {
			line[sizeof(line)-1] = '\0';
			cp = line;
			switch (*cp) {
			case '#':
			case '\n':
			case '\0':
				continue;
			}
			/* Skip leading whitespace. */
			for (; *cp && (*cp == ' ' || *cp == '\t'); cp++)
				;
			if (*cp) {
				if (key_read(k, &cp) == 1) {
					if (commentp)
						*commentp=xstrdup(filename);
					fclose(f);
					return 1;
				}
			}
		}
		fclose(f);
	}
	return 0;
}

/*
 * Load the public key from ssh v1 private or any pubkey file. Identity names
 * for v2 can be private key filenames. In that case, the first call of
 * key_try_load_public() fails so we add the ".pub" suffix and try again. Note
 * that we never read a private key in this function.
 *
 * If 'load_x509' parameter is non-zero, it loads a certificate if 'filename' is
 * a PKCS#11 URI. Reason for having the argument is that there is an
 * undocumented feature of the SSH client which is that if public key files do
 * exist in ~/.ssh, the client first sends an SSH_MSG_USERAUTH_REQUEST packet
 * without a signature, waiting for the server to see if it accepts the key at
 * all. It was designed to save CPU time since the sign operation is relatively
 * expensive. However, given that today's cycles are cheaper we do not feel any
 * need to support such behaviour for X.509 keys/certs. That means we will load
 * certificates in the client later, when they are really needed and we always
 * send a signature in the 1st user auth packet. Note that we do not change the
 * behaviour for keys in plain files.
 *
 * Parameter 'ret', if non-NULL, serves as an optional return parameter to
 * notify the caller what was the exact result when working with X.509 certs.
 */
Key *
key_load_public(const char *filename, char **commentp, int load_x509, int *ret)
{
	int rc;
	Key *pub;
	char file[MAXPATHLEN];
	pkcs11_uri_t *pkcs11_uri = NULL;

	/* First, check if the filename string contains a PKCS#11 URI spec. */
	rc = ssh_kmf_check_uri(filename, &pkcs11_uri);
	/* Initialize our optional return parameter. */
	if (ret != NULL)
		*ret = rc;
	if (rc == SSH_KMF_NOT_PKCS11_URI)
		goto non_pkcs11;
	else if (rc != SSH_KMF_PKCS11_URI_OK)
		return (NULL);

	/*
	 * As suggested in the main comment, we do not load certificates in the
	 * SSH client in this function. However, we do load certs in ssh-add and
	 * ssh-keygen. It's up to the caller to use the 'load_x509' param
	 * properly.
	 */
	if (load_x509 == SSH_KMF_LOAD_CERT) {
		/* Valid URI. Load a certificate. */
		pub = ssh_kmf_load_certificate(NULL, pkcs11_uri, NULL);
		if (pub == NULL) {
			if (ret != NULL)
				*ret = SSH_KMF_CANNOT_LOAD_OBJECT;
			goto free_and_finish;
		}
		/* We need this later for user messages. */
		pub->kmf_key->uri_str = xstrdup(filename);
		if (commentp != NULL)
			*commentp = xstrdup(filename);
		return (pub);
	}
	debug("We have a PKCS#11 URI identity but will load it later.");

free_and_finish:
	pkcs11_free_uri(pkcs11_uri);
	xfree(pkcs11_uri);
	return (NULL);

non_pkcs11:
	pub = key_load_public_type(KEY_RSA1, filename, commentp);
	if (pub != NULL)
		return pub;

	pub = key_new(KEY_UNSPEC);
	if (key_try_load_public(pub, filename, commentp) == 1)
		return pub;
	if ((strlcpy(file, filename, sizeof file) < sizeof(file)) &&
	    (strlcat(file, ".pub", sizeof file) < sizeof(file)) &&
	    (key_try_load_public(pub, file, commentp) == 1))
		return pub;
	key_free(pub);
	return NULL;
}

/*
 * Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Thread setup portions of this code derived from
 * OpenSSL 0.9.x file mt/mttest.c examples
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libintl.h>
#include <synch.h>
#include <thread.h>
#include <dlfcn.h>
#include <openssl/lhash.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "ipsec_util.h"

/* OpenSSL function pointers */
static X509_NAME *(*d2i_X509_NAME_fn)() = NULL;
static int (*X509_NAME_print_ex_fp_fn)() = NULL;
static char *(*ERR_get_error_fn)() = NULL;
static char *(*ERR_error_string_fn)() = NULL;
static void (*SSL_load_error_strings_fn)() = NULL;
static void (*ERR_free_strings_fn)() = NULL;
static void (*CRYPTO_set_locking_callback_fn)() = NULL;
static void (*CRYPTO_set_id_callback_fn)() = NULL;
static void (*X509_NAME_free_fn)() = NULL;
static int (*CRYPTO_num_locks_fn)() = NULL;
static void *(*OPENSSL_malloc_fn)() = NULL;
static void (*OPENSSL_free_fn)() = NULL;

static void solaris_locking_callback(int, int, char *, int);
static unsigned long solaris_thread_id(void);
static boolean_t thread_setup(void);
/* LINTED E_STATIC_UNUSED */
static void thread_cleanup(void);

mutex_t init_lock = DEFAULTMUTEX;
static mutex_t *lock_cs;
static long *lock_count;

static boolean_t libssl_loaded = B_FALSE;
static boolean_t libcrypto_loaded = B_FALSE;

void
libssl_load()
{
	void *dldesc;

	(void) mutex_lock(&init_lock);
	if (libssl_loaded) {
		(void) mutex_unlock(&init_lock);
		return;
	}

	dldesc = dlopen(LIBSSL, RTLD_LAZY);
	if (dldesc != NULL) {
		d2i_X509_NAME_fn = (X509_NAME*(*)())dlsym(dldesc,
		    "d2i_X509_NAME");
		if (d2i_X509_NAME_fn == NULL)
			goto libssl_err;

		X509_NAME_print_ex_fp_fn = (int(*)())dlsym(dldesc,
		    "X509_NAME_print_ex_fp");
		if (X509_NAME_print_ex_fp_fn == NULL)
			goto libssl_err;

		ERR_get_error_fn = (char *(*)())dlsym(dldesc,
		    "ERR_get_error");
		if (ERR_get_error_fn == NULL)
			goto libssl_err;

		ERR_error_string_fn = (char *(*)())dlsym(dldesc,
		    "ERR_error_string");
		if (ERR_error_string_fn == NULL)
			goto libssl_err;

		SSL_load_error_strings_fn = (void(*)())dlsym(dldesc,
		    "SSL_load_error_strings");
		if (SSL_load_error_strings_fn == NULL)
			goto libssl_err;

		ERR_free_strings_fn = (void(*)())dlsym(dldesc,
		    "ERR_free_strings");
		if (ERR_free_strings_fn == NULL)
			goto libssl_err;

		CRYPTO_set_locking_callback_fn = (void(*)())dlsym(dldesc,
		    "CRYPTO_set_locking_callback");
		if (CRYPTO_set_locking_callback_fn == NULL)
			goto libssl_err;

		CRYPTO_set_id_callback_fn = (void(*)())dlsym(dldesc,
		    "CRYPTO_set_id_callback");
		if (CRYPTO_set_id_callback_fn == NULL)
			goto libssl_err;

		X509_NAME_free_fn = (void(*)())dlsym(dldesc,
		    "X509_NAME_free");
		if (X509_NAME_free_fn == NULL)
			goto libssl_err;

		if (thread_setup() == B_FALSE)
			goto libssl_err;

		libssl_loaded = B_TRUE;
	}
	(void) mutex_unlock(&init_lock);
	return;
libssl_err:
	(void) dlclose(dldesc);
	(void) mutex_unlock(&init_lock);
}

void
libcrypto_load()
{
	void *dldesc;

	(void) mutex_lock(&init_lock);
	if (libcrypto_loaded) {
		(void) mutex_unlock(&init_lock);
		return;
	}

	dldesc = dlopen(LIBCRYPTO, RTLD_LAZY);
	if (dldesc != NULL) {
		CRYPTO_num_locks_fn = (int(*)())dlsym(dldesc,
		    "CRYPTO_num_locks");
		if (CRYPTO_num_locks_fn == NULL)
			goto libcrypto_err;

		/*
		 * OPENSSL_free is really a macro, so we
		 * need to reference the actual symbol,
		 * which is CRYPTO_free.
		 */
		OPENSSL_free_fn = (void(*)())dlsym(dldesc,
		    "CRYPTO_free");
		if (OPENSSL_free_fn == NULL)
			goto libcrypto_err;

		/*
		 * OPENSSL_malloc is really a macro, so we
		 * need to reference the actual symbol,
		 * which is CRYPTO_malloc.
		 */
		OPENSSL_malloc_fn = (void *(*)())dlsym(dldesc,
		    "CRYPTO_malloc");
		if (OPENSSL_malloc_fn == NULL)
			goto libcrypto_err;

		libcrypto_loaded = B_TRUE;
	}
	(void) mutex_unlock(&init_lock);
	return;
libcrypto_err:
	(void) dlclose(dldesc);
	(void) mutex_unlock(&init_lock);
}

static boolean_t
thread_setup(void)
{
	int i;

	if ((lock_cs = OPENSSL_malloc_fn(CRYPTO_num_locks_fn() *
	    sizeof (mutex_t))) == NULL)
		return (B_FALSE);
	if ((lock_count = OPENSSL_malloc_fn(CRYPTO_num_locks_fn() *
	    sizeof (long))) == NULL) {
		OPENSSL_free_fn(lock_cs);
		return (B_FALSE);
	}

	for (i = 0; i < CRYPTO_num_locks_fn(); i++) {
		lock_count[i] = 0;
		(void) mutex_init(&(lock_cs[i]), USYNC_THREAD, NULL);
	}

	CRYPTO_set_id_callback_fn((unsigned long (*)())solaris_thread_id);
	CRYPTO_set_locking_callback_fn((void (*)())solaris_locking_callback);
	return (B_TRUE);
}

static void
thread_cleanup(void)
{
	int i;

	(void) mutex_lock(&init_lock);
	CRYPTO_set_locking_callback_fn(NULL);
	CRYPTO_set_id_callback_fn(NULL);
	for (i = 0; i < CRYPTO_num_locks_fn(); i++)
		(void) mutex_destroy(&(lock_cs[i]));
	OPENSSL_free_fn(lock_cs);
	OPENSSL_free_fn(lock_count);
	(void) mutex_unlock(&init_lock);
}

/* ARGSUSED */
static void
solaris_locking_callback(int mode, int type, char *file, int line)
{
	if (mode & CRYPTO_LOCK) {
		(void) mutex_lock(&(lock_cs[type]));
		lock_count[type]++;
	} else {
		(void) mutex_unlock(&(lock_cs[type]));
	}
}

static unsigned long
solaris_thread_id(void)
{
	unsigned long ret;

	ret = (unsigned long)thr_self();
	return (ret);
}

void
print_asn1_name(FILE *file, const unsigned char *buf, long buflen)
{
	libcrypto_load();
	if (libcrypto_loaded)
		libssl_load();

	if (libssl_loaded && libcrypto_loaded) {
		X509_NAME *x509name = NULL;
		const unsigned char *p;

		/* Make an effort to decode the ASN1 encoded name */
		SSL_load_error_strings_fn();

		/*
		 * Temporary variable is mandatory per d2i_X509(3). Upcoming
		 * call to d2i_X509_NAME_fn() will change the 'p' pointer.
		 */
		p = buf;

		x509name = d2i_X509_NAME_fn(NULL, &p, buflen);
		if (x509name != NULL) {
			(void) X509_NAME_print_ex_fp_fn(file, x509name, 0,
			    (ASN1_STRFLGS_RFC2253 | ASN1_STRFLGS_ESC_QUOTE |
			    XN_FLAG_SEP_CPLUS_SPC | XN_FLAG_FN_SN));
			X509_NAME_free_fn(x509name);
			(void) fprintf(file, "\n");
		} else {
			char errbuf[80];

			(void) fprintf(file, "\n# %s\n",
			    ERR_error_string_fn(ERR_get_error_fn(), errbuf));
			(void) fprintf(file, dgettext(TEXT_DOMAIN,
			    "<cannot interpret>\n"));
		}
		ERR_free_strings_fn();
	} else {
		(void) fprintf(file, dgettext(TEXT_DOMAIN, "<cannot print>\n"));
	}
}

/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*---------------------------------------------------------------------------
 * Module:            k_setupssl.c
 * Operating System:  Linux, Win32
 *
 * Description:
 * This is the C Implementation file for setting up OpenSSL muti-threading environment
 *
 *-------------------------------------------------------------------------*/

#ifndef WIN32
#include <signal.h>
#include <openssl/evp.h>	/* UNIX */
#include <openssl/engine.h>
#endif

#include "k_setupssl.h"
#include "stdsoap2.h"
#include <openssl/crypto.h>

#ifdef KMSUSERPKCS12
#include <fcntl.h>
#include <openssl/pkcs12.h>
#endif

#if defined(WIN32)

#include <windows.h>
#define MUTEX_TYPE              HANDLE
#define MUTEX_SETUP(x)          (x) = CreateMutex(NULL, FALSE, NULL)
#define MUTEX_CLEANUP(x)        CloseHandle(x)
#define MUTEX_LOCK(x)           WaitForSingleObject((x), INFINITE)
#define MUTEX_UNLOCK(x)         ReleaseMutex(x)
#define THREAD_ID               GetCurrentThreadId()

#else

#include <pthread.h>

# define MUTEX_TYPE             pthread_mutex_t
# define MUTEX_SETUP(x)         pthread_mutex_init(&(x), NULL)
# define MUTEX_CLEANUP(x)       pthread_mutex_destroy(&(x))
# define MUTEX_LOCK(x)          pthread_mutex_lock(&(x))
# define MUTEX_UNLOCK(x)        pthread_mutex_unlock(&(x))
# define THREAD_ID              pthread_self()

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
#include <unistd.h>
#include <fcntl.h>

MUTEX_TYPE	init_ssl_mutex = PTHREAD_MUTEX_INITIALIZER;
static		int	ssl_initialized = 0;
#endif
#endif

#if OPENSSL_VERSION_NUMBER >= 0x1000004fL
/*
 * OpenSSL 1.0.0 put CRYPTO_set_id_callback and CRYPTO_get_id_callback into a
 * deprecated state.  This file uses them though.
 */
void CRYPTO_set_id_callback(unsigned long (*func)(void));
unsigned long (*CRYPTO_get_id_callback(void))(void);
#endif

struct CRYPTO_dynlock_value
{ MUTEX_TYPE mutex;
};

void sigpipe_handle(int x)
{
}

static MUTEX_TYPE *mutex_buf;

static struct CRYPTO_dynlock_value *dyn_create_function(const char *file, int line)
{ struct CRYPTO_dynlock_value *value;
  value = (struct CRYPTO_dynlock_value*)malloc(sizeof(struct CRYPTO_dynlock_value));
  if (value)
    MUTEX_SETUP(value->mutex);
  return value;
}

static void dyn_lock_function(int mode, struct CRYPTO_dynlock_value *l, const char *file, int line)
{ if (mode & CRYPTO_LOCK)
    MUTEX_LOCK(l->mutex);
  else
    MUTEX_UNLOCK(l->mutex);
}

static void dyn_destroy_function(struct CRYPTO_dynlock_value *l, const char *file, int line)
{ MUTEX_CLEANUP(l->mutex);
  free(l);
}

void kms_locking_function(int mode, int n, const char *file, int line)
{ if (mode & CRYPTO_LOCK)
    MUTEX_LOCK(mutex_buf[n]);
  else
    MUTEX_UNLOCK(mutex_buf[n]);
}


unsigned long id_function(void )
{ return (unsigned long)THREAD_ID;
}

#ifdef WIN32
void OpenSSL_add_all_ciphers(void);	// UNIX
void OpenSSL_add_all_digests(void);
#endif

#ifdef K_HPUX_PLATFORM
extern void allow_unaligned_data_access();
#endif

// gSOAP 2.7e:
//   The function ssl_init is defined in stdsoap2.cpp and is not exported by
//   default by gSOAP.
// gSOAP 2.7.12:
//   The function soap_ssl_init is defined in stdsoap2.cpp.  It replaces
//   ssl_init and is exported by gSOAP.  gSOAP 2.7.13 also supports a new
//   SOAP_SSL_SKIP_HOST_CHECK flag.
#ifndef SOAP_SSL_SKIP_HOST_CHECK
extern int ssl_init();
#endif

int K_SetupSSL()
{ int i;
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
	MUTEX_LOCK(init_ssl_mutex);
	if (ssl_initialized) {
		MUTEX_UNLOCK(init_ssl_mutex);
		return 1;
	}
#endif
  mutex_buf = (MUTEX_TYPE*)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(MUTEX_TYPE));
  if (!mutex_buf) {
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
	MUTEX_UNLOCK(init_ssl_mutex);
#endif
    return 0;
  }
  for (i = 0; i < CRYPTO_num_locks(); i++)
    MUTEX_SETUP(mutex_buf[i]);
  if (CRYPTO_get_id_callback() == NULL)
	CRYPTO_set_id_callback(id_function);
  if (CRYPTO_get_locking_callback() == NULL)
	CRYPTO_set_locking_callback(kms_locking_function);

  CRYPTO_set_dynlock_create_callback(dyn_create_function);
  CRYPTO_set_dynlock_lock_callback(dyn_lock_function);
  CRYPTO_set_dynlock_destroy_callback(dyn_destroy_function);

#ifndef WIN32
  /* Need SIGPIPE handler on Unix/Linux systems to catch broken pipes: */
  signal(SIGPIPE, sigpipe_handle);
#endif
#ifdef K_HPUX_PLATFORM
//  signal(SIGBUS, sigpipe_handle);
    allow_unaligned_data_access();
#endif
  OpenSSL_add_all_ciphers();
  OpenSSL_add_all_digests();

  // call gSOAP's OpenSSL initialization, which initializes SSL algorithms and seeds RAND

  // gSOAP 2.7e:
  //   The function ssl_init is defined in stdsoap2.cpp and is not exported by
  //   default by gSOAP.
  // gSOAP 2.7.13:
  //   The function soap_ssl_init is defined in stdsoap2.cpp.  It replaces
  //   ssl_init and is exported by gSOAP.  gSOAP 2.7.13 also supports a new
  //   SOAP_SSL_SKIP_HOST_CHECK flag.
#ifdef SOAP_SSL_SKIP_HOST_CHECK
  soap_ssl_init();
#else
  ssl_init();
#endif

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
	ssl_initialized = 1;
	MUTEX_UNLOCK(init_ssl_mutex);
#endif

  return 1;
}

void K_CleanupSSL()
{ int i;
  if (!mutex_buf)
    return;
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
  {
	unsigned long (*id_func)();

	if ((id_func = CRYPTO_get_id_callback()) == id_function) {
  		ENGINE_cleanup();
		ERR_free_strings();
		CRYPTO_set_id_callback(NULL);
		CRYPTO_set_locking_callback(NULL);
	}
  }
#endif
  CRYPTO_set_dynlock_create_callback(NULL);
  CRYPTO_set_dynlock_lock_callback(NULL);
  CRYPTO_set_dynlock_destroy_callback(NULL);
  for (i = 0; i < CRYPTO_num_locks(); i++)
    MUTEX_CLEANUP(mutex_buf[i]);
  OPENSSL_free(mutex_buf);
  mutex_buf = NULL;
}

// TODO: what should 'struct soap' really be?
int K_SetupCallbacks( struct soap *i_pSoap )
{
    return 1;
}

static int
K_ssl_password(char *buf, int num, int rwflag, void *userdata)
{
	if (num < (int) strlen((char *)userdata) + 1)
		return 0;

	return (int) strlen(strcpy(buf, (char *)userdata));
}

void *K_SetupSSLCtx(
    char *i_pCACert, void *i_pCert, void *i_pKey, unsigned short i_iFlags)
{
	SSL_CTX		*ctx = NULL;
	X509		*cert = NULL;
	EVP_PKEY	*pkey = NULL;
	int		st;

	ctx = SSL_CTX_new(SSLv23_method());

	if (ctx == NULL) {
		return (NULL);
	}
	cert = (X509 *)i_pCert;
	pkey = (EVP_PKEY *)i_pKey;

	st = SSL_CTX_load_verify_locations(ctx, i_pCACert, NULL);
	if (st == 1) {
		if (i_iFlags & SOAP_SSL_REQUIRE_CLIENT_AUTHENTICATION) {
			SSL_CTX_set_client_CA_list(ctx, 
				SSL_load_client_CA_file(i_pCACert));
		}
	}

	if (st == 1) {
		if (!(i_iFlags & SOAP_SSL_NO_DEFAULT_CA_PATH)) {
			st = SSL_CTX_set_default_verify_paths(ctx);
		}
	}

	if (i_iFlags == SOAP_SSL_REQUIRE_CLIENT_AUTHENTICATION) {
		if (cert) {
			st = SSL_CTX_use_certificate(ctx, cert);
			if ((st == 1) && pkey) {
				st = SSL_CTX_use_PrivateKey(ctx, pkey);
			}
		}
	}

	if (st != 1) {
		SSL_CTX_free(ctx);
		ctx = NULL;
	}

	return ((void *)ctx);
}

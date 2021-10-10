/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*---------------------------------------------------------------------------
 * Module:            k_setupssl.h
 * Operating System:  Linux, Win32
 *
 * Description:
 * This is the header file of setting up OpenSSL
 */

#ifndef _K_SETUP_SSL_H
#define _K_SETUP_SSL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef METAWARE
#include "stdsoap2.h"
/**
 *  set up gSoap I/O callback functions for environments that need to customize
 *  the I/O functions, e.g. embedded agents.
 */
int K_SetupCallbacks( struct soap *i_pSoap );

int K_ssl_client_context(struct soap *i_pSoap,
                            int flags,
                            const char *keyfile,  /* NULL - SERVER */
                            const char *password, /* NULL - SERVER */
                            const char *cafile,
                            const char *capath,   /* ALWAYS NULL */
                            const char *randfile);
#endif

#ifdef KMSUSERPKCS12
void *K_SetupSSLCtx(
    char *i_pCACert, void *i_pCert, void *i_pKey, unsigned short i_iFlags);
#endif

int K_SetupSSL();
void K_CleanupSSL();

#ifdef __cplusplus
}
#endif

#endif

/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/**
 * \file KMSAgentPKICertOpenSSL.cpp
 */

#include <stdio.h>
#include <openssl/bio.h>
#include <openssl/pem.h>

#include "SYSCommon.h"
#include "KMSAgentPKIimpl.h"

typedef struct X509control
{
    X509*   pX509;
} X509control;

void * InitializeCertImpl()
{
    X509control *pX509Control = (X509control *) malloc(sizeof(X509control));

    if ( pX509Control != NULL )
    {
        pX509Control->pX509 = NULL;
    }

    return pX509Control;
}

/**
 * export the Cert to a memory BIO, if error, return NULL
 */
BIO* SaveCertToMemoryBIO( X509control* i_pX509control )
{
    BIO *pMemBio = NULL;
    int iReturn;

    // create memory BIO
    pMemBio = BIO_new(BIO_s_mem());

    if(pMemBio == NULL)
    {
        //fixme: log -- no memory
        return NULL;
    }

    //iReturn = PEM_write_bio_X509(pMemBio, m_pNative);
    iReturn = PEM_write_bio_X509(pMemBio, i_pX509control->pX509);

    if(!iReturn) // return 0: means error occurs
    {
        //fixme: log -- could not export private key
        BIO_free(pMemBio);
        return NULL;
    }

    return pMemBio;
}

bool SaveX509CertTofile( 
                        void* const i_pImplResource,
                        FILE* i_pcFile)
{
    FATAL_ASSERT( i_pImplResource != NULL && i_pcFile );

    X509control* pX509control = (X509control*)i_pImplResource;
    int status;

    status = PEM_write_X509(i_pcFile, pX509control->pX509);
    if (status != 1)
    {
	    return false;
    }

    return true;
}

bool SaveX509CertToBuffer(
                        void* const             i_pImplResource,
                        unsigned char * const   i_pcBuffer,
                        int                     i_iBufferLength,
                        int * const             o_pActualLength )
{
    FATAL_ASSERT( i_pImplResource != NULL && 
                  i_pcBuffer && 
                  o_pActualLength &&
                  i_iBufferLength > 0 );

    X509control* pX509control = (X509control*)i_pImplResource;

    BIO *pMemBio = NULL;
    char *pData = NULL;
    int iLength;

    // create memory BIO
    pMemBio = SaveCertToMemoryBIO( pX509control );

    if( pMemBio == NULL )
    {
        //fixme: log -- no memory
        return false;
    }

    iLength = BIO_get_mem_data( pMemBio, &pData );

    // If the output buffer is a string, it needs to be NULL terminated
    // So always append a NULL to the output
    if(iLength + 1 > i_iBufferLength)
    {
        //fixme: log -- buffer too small
        BIO_free(pMemBio);
        return false;
    }
    // copy the data to given buffer
    memcpy(i_pcBuffer, pData, iLength);
    // NULL terminate the string
    i_pcBuffer[iLength] = '\0';
    *o_pActualLength = iLength;

    // free memory
    BIO_free(pMemBio);

    return true;
}

/**
 * import the Cert from a BIO, if error, return NULL
 */
bool LoadCertFromBIO(X509control* i_pX509control, BIO *i_pBio)
{
    X509 *pRequest = NULL;

    if (i_pX509control == NULL) return false;

    if(i_pBio == NULL) return false;

    //if(m_pNative != NULL) return false; // do not allow overwrite
    if (i_pX509control->pX509 != NULL ) return false;

    pRequest=PEM_read_bio_X509(i_pBio, NULL, NULL, NULL);

    if (pRequest == NULL)
    {
        // fixme: log: invalid certificate format
        return false;
    }
    //m_pNative = pRequest;
    i_pX509control->pX509 = pRequest;

    return true;
}

bool LoadX509CertFromFile( 
                            void* const i_pImplResource,
                            FILE* i_pcFile)

{
    X509control* pX509control = (X509control*) i_pImplResource;
    if ((i_pcFile == NULL) || (pX509control == NULL))
    {
        return false;
    }

    pX509control->pX509 = PEM_read_X509(i_pcFile, NULL, NULL, NULL);
    if (pX509control->pX509 == NULL)
    {
        //fixme: log -- no memory
        return false;
    }

    return true;
}


bool LoadX509CertFromBuffer(
                           void* const i_pImplResource,
                           void* const i_pX509Cert,
                           int         i_iLength)
 {
    X509control* pX509control = (X509control*)i_pImplResource;

    if(pX509control == NULL)
    {
        return false;
    }

    BIO *pMemBio;
    bool bReturn;
    // create a mem bio from the given buffer
    // Note that BIO_new_mem_buf() creates a BIO which never destroy the memory
    //    attached to it.
    pMemBio = BIO_new_mem_buf(i_pX509Cert, i_iLength);
    if (pMemBio == NULL)
    {
        //fixme: log -- no memory
        return false;
    }
    bReturn = LoadCertFromBIO(pX509control, pMemBio);

    BIO_free(pMemBio);

    return bReturn;
}

void FinalizeCertImpl( void* i_pImplResource )
{
    if ( i_pImplResource != NULL )
    {
        free(i_pImplResource);
    }
}

bool PrintX509Cert( void* const i_pImplResource )
{
    BIO *pMemBio;
    char *pData;
    int iLength,i;
    X509control* pX509control = (X509control*)i_pImplResource;
    pMemBio = BIO_new(BIO_s_mem());
    if(pMemBio == NULL)
    {
        return false;
    }

    //X509_print(pMemBio,m_pNative);
    X509_print(pMemBio, pX509control->pX509);

    iLength = BIO_get_mem_data(pMemBio, &pData);

    for(i = 0; i < iLength; i++)
    {
        printf("%c", pData[i]);
    }

    BIO_free(pMemBio);

    return true;

}
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
void *GetCert(void* i_pImplResource )
{
	X509control* pX509control = (X509control*)i_pImplResource;
	return ((void *)pX509control->pX509);
}

void SetCert(void* i_pImplResource, void *cert)
{
	X509control* pX509control = (X509control*)i_pImplResource;
	pX509control->pX509 = (X509 *)cert;
	return;
}
#endif

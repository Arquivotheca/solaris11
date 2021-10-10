/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/**
 * \file KMSAgentPKICommon.cpp
 */
#include <stdio.h>

#include "SYSCommon.h"
#include "KMSAgentPKICommon.h"
#include "KMSAgentStringUtilities.h"

#include "KMSAgent_direct.h"


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CPKI::CPKI()
{
   m_iKeyLength = DEFAULT_KEY_SIZE;
   
   // used for CA
   m_pCACertificate = NULL;
   m_pCAPrivateKey = NULL;
}

// BEN - make these
// global lengths
int iLength1 = 0;
int iLength2 = 0;

// THIS CAN'T BE STACK DATA - TOO BIG
static unsigned char aTempBuffer[MAX_CERT_SIZE + MAX_KEY_SIZE];
#ifdef METAWARE
static char aNotherTempBuffer[50];
#endif

// used by StoreAgentPKI - KMSAgentStorage.cpp

bool CPKI::ExportCertAndKeyToFile(
   CCertificate* const         i_pCertificate,  
   CPrivateKey*  const         i_pPrivateKey,
   FILE* 		       i_pcFile,
   const char* const           i_sPassphrase,
   EnumPKIFileFormat           i_eFileFormat )
{
   FATAL_ASSERT( i_pCertificate && i_pPrivateKey && i_pcFile );
   
   memset( aTempBuffer, 0, MAX_CERT_SIZE + MAX_KEY_SIZE );

#ifdef KMSUSERPKCS12
    if ( i_eFileFormat == FILE_FORMAT_PKCS12 )
    {
        if ( !i_pCertificate->SavePKCS12(aTempBuffer,
                                MAX_CERT_SIZE,
                                &iLength1,
                                i_pPrivateKey,
                                (char*)i_sPassphrase ) )
        {
            return false;
        }
    } else {
#endif
   
   // Overloaded Save method implemented in KMSAgentPKICert.cpp
   // this method saves Certificate to the temporary buffer, not a file
   // but a side effect is to get the actual file length
   if ( !i_pCertificate->Save(aTempBuffer, 
                              MAX_CERT_SIZE, 
                              &iLength1,          /* returned - actual length
                                                     written */
                              i_eFileFormat) )
   {
      return false;
   }
   
   // Overloaded Save method implemented in KMSAgentPKIKey.cpp
   // this method saves keys to the temporary buffer, not a file,
   // but a side effect is to get the actual file length
   if ( !i_pPrivateKey->Save(aTempBuffer + iLength1, 
                             MAX_KEY_SIZE, 
                             &iLength2,          /* returned - actual length
                                                    written */
                             i_sPassphrase, 
                             i_eFileFormat) )
   {
      return false;
   }
   
#ifdef KMSUSERPKCS12
	}
#endif

   // now write the temporary buffer to a file
#ifdef METAWARE
   // write out the two file lengths
   snprintf(aNotherTempBuffer, sizeof(aNotherTempBuffer), "iLength1=%x\n", iLength1);
   fputs((const char*)aNotherTempBuffer, i_pcFile);
   
   snprintf(aNotherTempBuffer, sizeof(aNotherTempBuffer), "iLength2=%x\n", iLength2);
   fputs((const char*)aNotherTempBuffer, i_pcFile);
#endif

   int iBytesWritten = fwrite( (const char*)aTempBuffer,  // from
                               1,                         // size
                               iLength1+iLength2,         // actual file length
                               i_pcFile );                // to-file
   
   return ( iBytesWritten == (iLength1+iLength2) );
}


CPKI::~CPKI()
{
   // empty
}


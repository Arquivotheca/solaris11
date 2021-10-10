/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "KMSAgentSoapUtilities.h"
#include "KMSAgentStringUtilities.h"
#include "ApplianceParameters.h"

#include "stdsoap2.h"

/**
 * Get the peer's network address
 */
void GetPeerNetworkAddress (char* const o_psPeerNetworkAddress,
                            struct soap* i_pSoap)
{
    FATAL_ASSERT(o_psPeerNetworkAddress);

    if (strlen(i_pSoap->host) > 0)
    {
        // IPv4 addresses can appear as ::ffff:a.b.c.d, strip off the prefix
        if (strncmp(i_pSoap->host, "::ffff:", 7) == 0)
        {
            strncpy(o_psPeerNetworkAddress, &i_pSoap->host[7], g_iMAX_PEER_NETWORK_ADDRESS_LENGTH);
            o_psPeerNetworkAddress[g_iMAX_PEER_NETWORK_ADDRESS_LENGTH-1] = '\0';
            return;
        }
        strncpy(o_psPeerNetworkAddress, i_pSoap->host, g_iMAX_PEER_NETWORK_ADDRESS_LENGTH);
        o_psPeerNetworkAddress[g_iMAX_PEER_NETWORK_ADDRESS_LENGTH-1] = '\0';
        return;
    }

    // i_pSoap->ip == 0 could not represent a valid 
    // Peer Network Address (IPv4), check i_pSoap->session_host
    // for an IPv6 address
    if (i_pSoap->ip == 0)
    {
#ifndef METAWARE
        if (strlen(i_pSoap->session_host) > 0)
        {
            // IPv4 addresses can appear as ::ffff:a.b.c.d, strip off the
            // prefix

            if (strncmp(i_pSoap->session_host, "::ffff:", 7) == 0)
            {
                strncpy(o_psPeerNetworkAddress, &i_pSoap->session_host[7], g_iMAX_PEER_NETWORK_ADDRESS_LENGTH);
                o_psPeerNetworkAddress[g_iMAX_PEER_NETWORK_ADDRESS_LENGTH-1] = '\0';
                return;
            }

            strncpy(o_psPeerNetworkAddress, i_pSoap->session_host, g_iMAX_PEER_NETWORK_ADDRESS_LENGTH);
            o_psPeerNetworkAddress[g_iMAX_PEER_NETWORK_ADDRESS_LENGTH-1] = '\0';
            return;
        }
#endif
        strcpy(o_psPeerNetworkAddress, "");
        return;
    }
    
    K_snprintf(o_psPeerNetworkAddress, 
            g_iMAX_PEER_NETWORK_ADDRESS_LENGTH,
            "%d.%d.%d.%d",
            (int) (i_pSoap->ip >> 24)&0xFF,
            (int) (i_pSoap->ip >> 16)&0xFF,
            (int) (i_pSoap->ip >> 8)&0xFF,
            (int) (i_pSoap->ip)&0xFF);

    return;
}

/**
 * Get the soap fault code and print it 
 */
void GetSoapFault(char* o_psFaultMessage, 
                  struct soap *i_pstSoap) 
{
   FATAL_ASSERT( i_pstSoap );
   
   strncpy (o_psFaultMessage, " SoapFaultCode=",g_iMAX_SOAP_FAULT_MESSAGE_LENGTH);
   o_psFaultMessage[g_iMAX_SOAP_FAULT_MESSAGE_LENGTH-1] = '\0';
   strncat (o_psFaultMessage, GET_SOAP_FAULTCODE(i_pstSoap),
           g_iMAX_SOAP_FAULT_MESSAGE_LENGTH-strlen(o_psFaultMessage));
   strncat (o_psFaultMessage, " SoapFaultString=",
           g_iMAX_SOAP_FAULT_MESSAGE_LENGTH-strlen(o_psFaultMessage));
   strncat (o_psFaultMessage, GET_SOAP_FAULTSTRING(i_pstSoap),
           g_iMAX_SOAP_FAULT_MESSAGE_LENGTH-strlen(o_psFaultMessage));
   strncat (o_psFaultMessage, " SoapFaultDetail=",
           g_iMAX_SOAP_FAULT_MESSAGE_LENGTH-strlen(o_psFaultMessage));
   strncat (o_psFaultMessage, GET_SOAP_FAULTDETAIL(i_pstSoap),
           g_iMAX_SOAP_FAULT_MESSAGE_LENGTH-strlen(o_psFaultMessage));
   
   return;
}

bool PutBinaryIntoSoapBinary(
        struct soap* i_pSoap,
        const unsigned char* i_pBinary,
        int i_iBinarySize,
        unsigned char*& o_pSoapBinary,
        int& o_iSoapBinarySize )
{
    FATAL_ASSERT( i_pSoap );

    o_pSoapBinary = 0;
    o_iSoapBinarySize = 0;

    if ( i_iBinarySize > 0 )
    {
        o_pSoapBinary =
                    (unsigned char*)soap_malloc(
                        i_pSoap,
                        sizeof(unsigned char) * i_iBinarySize);

        if ( !o_pSoapBinary )
        {
            // No log for out of memory condition

            return false;
        }

        o_iSoapBinarySize = i_iBinarySize;

        memcpy(o_pSoapBinary, i_pBinary, i_iBinarySize);
    }

    return true;
}

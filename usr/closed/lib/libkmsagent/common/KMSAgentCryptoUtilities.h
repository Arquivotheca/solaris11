/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/**
 * \file KMSAgentCryptoUtilities.h
 */

#ifndef KMSAgentCryptoUtilities_H
#define KMSAgentCryptoUtilities_H

#ifdef WIN32
#pragma warning(disable: 4786)
#endif

#define HASH_LENGTH     20
#define HMAC_LENGTH     20

/**
 *  Generates a random set of bytes of the specified length.
 *
 *  @return boolean success indicator
 */
bool GetPseudorandomBytes(
        int i_iNumBytes,
        unsigned char* o_pBytes );

/**
 *  computes SHA-1 hash of the buffer
 *  @param i_pBufferToHash
 *  @param i_iBufferToHashSize
 *  @param o_pHashedBuffer buffer to recieve the SHA-1 hash and must be 
 *          #HASH_LENGTH bytes
 *  @return boolean success indicator
 */
bool HashBuffer( 
        const unsigned char* i_pBufferToHash,
        int i_iBufferToHashSize,
        unsigned char* o_pHashedBuffer );

#ifdef METAWARE

// implemented in KMSAgentCryptoUtilitiesTreckHmac.c
extern "C" int HMACBuffers(
   int i_iBufferCount,
   const unsigned char** i_pBufferToHMAC,
   int* i_pBufferToHMACSize,
   const unsigned char* i_pHMACKey,
   int i_iHMACKeySize,
   unsigned char* o_pHMACBuffer );

#else
/**
 *  computes HMAC on the supplied buffers using SHA-1
 *  hashing and the key supplied.  No logging is performed since this
 *  functions must execute in a Known Answer Test prior to 
 *  #KMSAgent_InitializeLibrary.
 *  @param i_iBufferCount number of buffers provided in #i_pBufferToHMAC
 *  @param i_pBufferToHMAC array of buffers
 *  @param i_pBufferToHMACSize array of sizes corresponding to buffers in 
 *      #i_pBufferToHMAC
 *  @param i_pHMACKey secret key
 *  @param i_iHMACKeySize  length of the key in bytes 
 *  @param o_pHMACBuffer  buffer to contain the HMAC, this buffer must be
 *      #HASH_LENGTH bytes
 *  @return boolean success indicator
 */
bool HMACBuffers(
        int i_iBufferCount,
        const unsigned char** i_pBufferToHMAC,
        int* i_pBufferToHMACSize,
        const unsigned char* i_pHMACKey,
        int i_iHMACKeySize,
        unsigned char* o_pHMACBuffer );
#endif



#endif //KMSAgentCryptoUtilities_H

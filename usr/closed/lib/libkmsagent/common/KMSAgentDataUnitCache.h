/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/** @file             KMSAgentDataUnitCache.h 
 *  @defgroup         EncryptionAgent Encryption Agent API
 *
 */
#ifndef KMSAGENT_DATA_UNIT_CACHE_H
#define KMSAGENT_DATA_UNIT_CACHE_H

#define DATA_UNIT_CACHE_MAX_SIZE 128

typedef struct DataUnitCacheEntry
{
    utf8char m_wsApplianceNetworkAddress[KMS_MAX_NETWORK_ADDRESS+1];
    unsigned char m_aDataUnitID[KMS_DATA_UNIT_ID_SIZE];
    unsigned char m_aDataUnitKeyID[KMS_KEY_ID_SIZE];

} DataUnitCacheEntry;

/**
 *  Maintains an affinity list between KMAs and DUs and KeyIDs. 
 */
class CDataUnitCache
{

public:
    CDataUnitCache(int i_iMaxSize = DATA_UNIT_CACHE_MAX_SIZE);
    ~CDataUnitCache();

    /**
     *   insert a new DataUnitCacheEntry into the cache list, either i_pDataUnitID or
     *   i_pDataUnitKeyID must be specified for affinity with the specified i_wsApplianceNetworkAddress
     *   @param i_pDataUnitID optional, specifies a DU ID cache entry if specified
     *   @param i_iDataUnitIDMaxLen ignored if i_pDataUnitID not specified, otherwise
     *      specifies the length of i_pDataUnitID
     *   @param i_pDataUnitKeyID optional, specifies a Key ID cache entry if specified
     *   @param i_iDataUnitKeyIDMaxLen ignored if i_pDataUnitKeyID is not specified, 
     *      otherwise specifies the length of i_pDataUnitKeyID
     *   @param i_wsApplianceNetworkAddress required and specifies the KMA affiliated 
     *      with the DU ID or Key ID
     *   @return True if successfully inserted into the cache
     */
    bool Insert(
                const unsigned char* const i_pDataUnitID,
                int i_iDataUnitIDMaxLen, 
                const unsigned char* const i_pDataUnitKeyID ,
                int i_iDataUnitKeyIDMaxLen,
                const utf8char* const i_wsApplianceNetworkAddress );

    bool GetApplianceByDataUnitID(
                const unsigned char* const i_pDataUnitID,
                int i_iDataUnitIDMaxLen,
                utf8char* const o_wsApplianceNetworkAddress,
                int i_iMaxApplianceNetworkAddressLen );

    bool GetApplianceByDataUnitKeyID(
                const unsigned char* const i_pDataUnitKeyID,
                int i_iDataUnitKeyIDMaxLen,
                utf8char* const o_wsApplianceNetworkAddress,
                int i_iMaxApplianceNetworkAddressLen );

protected:
    void Lock();
    void Unlock();

private:
    K_MUTEX_HANDLE m_Lock;

    int m_iIndex;
    int m_iSize;
    int m_iMaxSize;
    DataUnitCacheEntry *m_pCache;

};

#endif

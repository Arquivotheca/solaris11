/****************************************************************************
 * Copyright(c) 2005 Broadcom Corporation, all rights reserved
 * Proprietary and Confidential Information.
 *
 * This source file is the property of Broadcom Corporation, and
 * may not be copied or distributed in any isomorphic form without 
 * the prior written consent of Broadcom Corporation. 
 *
 * Name:        keyfile.h
 *
 * Description: Definition of license key file format.
 * 
 * Created:     07/01/2005 skeung
 *
 ****************************************************************************/

#ifndef _KEYFILE_H
#define _KEYFILE_H

#include "bcmtype.h"

/*
 * 2. The 'LICENSE_KEY_FILE_HEADER' will be in network byte order 
 *    (big endian).
 * 4. 'length' will include from 'crc' (byte 0) to the end of the actual 
 *    'key' data. For example, if the actual key data is 52 bytes, the 
 *    length will 64 bytes.
 * 5. 'version' is the version of the 'LICENSE_KEY_FILE_HEADER'.
 * 6. 'key' represents the actual key data with variable length which can 
 *    be derivde from 'length' field.
 * 7. 'crc' is the CRC value calculated from 'signature' (byte 4) to the 
 *    end of actual key data (last byte).
 */

#if !defined(LITTLE_ENDIAN) && !defined(BIG_ENDIAN)
    #error "Missing either LITTLE_ENDIAN or BIG_ENDIAN definition."
#endif

typedef struct _multikey_fhdr_v1_t
{
    u16_t signature;
        #define MULTIKEY_SIGNATURE      (('M' << 8) | ('K' << 0))
    u8_t  version;
        #define MULTIKEY_FHDR_VERSION_1        1
    u8_t  record_size;

    u8_t first_mac_suffix[4];
    u8_t last_mac_suffix[4];

} multikey_fhdr_v1_t;

typedef struct _multikey_fhdr_t
{
    u16_t signature;
        #define MULTIKEY_SIGNATURE      (('M' << 8) | ('K' << 0))
    u8_t  version;
        #define MULTIKEY_FHDR_VERSION_LATEST   2
    u8_t  record_size;

    u8_t first_mac_suffix[4];
    u8_t last_mac_suffix[4];

    /* Additional fields since version 1 */
    u8_t stride;
    u8_t unused_a[7];

} multikey_fhdr_t;


#ifdef LINUX
#pragma pack(push, 1)
#else
#pragma pack(push)
#pragma pack(1)
#endif

/*
 * keyfile_b definition
 */

typedef struct _keyfile_b
{
    u32_t crc;
    u32_t signature;
        #define KEYFILE_SIGNATURE      (('S' << 0) | ('F' << 8) | \
                                        ('K' << 16) | ('L' << 24))

    u16_t version;
    u16_t byte_cnt;
        #define KEYFILE_HDR_VERSION    1

    u8_t  key[1];
} keyfile_b_t;


/*
 * keyfile_l definition
 */

typedef struct _keyfile_l
{
    u32_t crc;
    u32_t signature;
        #define KEYFILE_SIGNATURE      (('S' << 0) | ('F' << 8) | \
                                        ('K' << 16) | ('L' << 24))

    u16_t byte_cnt;
    u16_t version;
        #define KEYFILE_HDR_VERSION    1

    u8_t  key[1];
} keyfile_l_t;

#pragma pack(pop)

#if defined(BIG_ENDIAN)
    typedef keyfile_b_t                          keyfile_t;
#elif defined(LITTLE_ENDIAN)
    typedef keyfile_l_t                          keyfile_t;
#endif


#endif /* _KEYFILE_H */

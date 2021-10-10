/*
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _SCU_VIIT_DATA_HEADER_
#define _SCU_VIIT_DATA_HEADER_

/**
 * @file
 *  
 * @brief This file contains the constants and structures for the SCU hardware 
 *        VIIT table entries.
 */

#include "sci_types.h"

#define SCU_VIIT_ENTRY_ID_MASK         (0xC0000000)
#define SCU_VIIT_ENTRY_ID_SHIFT        (30)

#define SCU_VIIT_ENTRY_FUNCTION_MASK   (0x0FF00000)
#define SCU_VIIT_ENTRY_FUNCTION_SHIFT  (20)

#define SCU_VIIT_ENTRY_IPPTMODE_MASK   (0x0001F800)
#define SCU_VIIT_ENTRY_IPPTMODE_SHIFT  (12)

#define SCU_VIIT_ENTRY_LPVIE_MASK      (0x00000F00)
#define SCU_VIIT_ENTRY_LPVIE_SHIFT     (8)

#define SCU_VIIT_ENTRY_STATUS_MASK     (0x000000FF)
#define SCU_VIIT_ENTRY_STATUS_SHIFT    (0)

#define SCU_VIIT_ENTRY_ID_INVALID   (0 << SCU_VIIT_ENTRY_ID_SHIFT)
#define SCU_VIIT_ENTRY_ID_VIIT      (1 << SCU_VIIT_ENTRY_ID_SHIFT)
#define SCU_VIIT_ENTRY_ID_IIT       (2 << SCU_VIIT_ENTRY_ID_SHIFT)
#define SCU_VIIT_ENTRY_ID_VIRT_EXP  (3 << SCU_VIIT_ENTRY_ID_SHIFT)

#define SCU_VIIT_IPPT_SSP_INITIATOR (0x01 << SCU_VIIT_ENTRY_IPPTMODE_SHIFT)
#define SCU_VIIT_IPPT_SMP_INITIATOR (0x02 << SCU_VIIT_ENTRY_IPPTMODE_SHIFT)
#define SCU_VIIT_IPPT_STP_INITIATOR (0x04 << SCU_VIIT_ENTRY_IPPTMODE_SHIFT)
#define SCU_VIIT_IPPT_INITIATOR     \
   (                                \
       SCU_VIIT_IPPT_SSP_INITIATOR  \
     | SCU_VIIT_IPPT_SMP_INITIATOR  \
     | SCU_VIIT_IPPT_STP_INITIATOR  \
   )

#define SCU_VIIT_STATUS_RNC_VALID      (0x01 << SCU_VIIT_ENTRY_STATUS_SHIFT)
#define SCU_VIIT_STATUS_ADDRESS_VALID  (0x02 << SCU_VIIT_ENTRY_STATUS_SHIFT)
#define SCU_VIIT_STATUS_RNI_VALID      (0x04 << SCU_VIIT_ENTRY_STATUS_SHIFT)
#define SCU_VIIT_STATUS_ALL_VALID      \
   (                                   \
       SCU_VIIT_STATUS_RNC_VALID       \
     | SCU_VIIT_STATUS_ADDRESS_VALID   \
     | SCU_VIIT_STATUS_RNI_VALID       \
   )

#define SCU_VIIT_IPPT_SMP_TARGET    (0x10 << SCU_VIIT_ENTRY_IPPTMODE_SHIFT)

/**
 * @struct SCU_VIIT_ENTRY 
 *  
 * @brief This is the SCU Virtual Initiator Table Entry 
 */
typedef struct SCU_VIIT_ENTRY
{
   /**
    * This must be encoded as to the type of initiator that is being constructed
    * for this port.
    */
   U32  status;
   
   /**
    * Virtual initiator high SAS Address
    */
   U32  initiator_sas_address_hi;

   /**
    * Virtual initiator low SAS Address
    */
   U32  initiator_sas_address_lo;

   /**
    * This must be 0
    */
   U32  reserved;

} SCU_VIIT_ENTRY_T;


// IIT Status Defines
#define SCU_IIT_ENTRY_ID_MASK                (0xC0000000)
#define SCU_IIT_ENTRY_ID_SHIFT               (30)

#define SCU_IIT_ENTRY_STATUS_UPDATE_MASK     (0x20000000)
#define SCU_IIT_ENTRY_STATUS_UPDATE_SHIFT    (29)

#define SCU_IIT_ENTRY_LPI_MASK               (0x00000F00)
#define SCU_IIT_ENTRY_LPI_SHIFT              (8)

#define SCU_IIT_ENTRY_STATUS_MASK            (0x000000FF)
#define SCU_IIT_ENTRY_STATUS_SHIFT           (0)

// IIT Remote Initiator Defines
#define SCU_IIT_ENTRY_REMOTE_TAG_MASK  (0x0000FFFF)
#define SCU_IIT_ENTRY_REMOTE_TAG_SHIFT (0)

#define SCU_IIT_ENTRY_REMOTE_RNC_MASK  (0x0FFF0000)
#define SCU_IIT_ENTRY_REMOTE_RNC_SHIFT (16)

#define SCU_IIT_ENTRY_ID_INVALID   (0 << SCU_IIT_ENTRY_ID_SHIFT)
#define SCU_IIT_ENTRY_ID_VIIT      (1 << SCU_IIT_ENTRY_ID_SHIFT)
#define SCU_IIT_ENTRY_ID_IIT       (2 << SCU_IIT_ENTRY_ID_SHIFT)
#define SCU_IIT_ENTRY_ID_VIRT_EXP  (3 << SCU_IIT_ENTRY_ID_SHIFT)

/**
 * @struct SCU_IIT_ENTRY 
 *
 * @brief This will be implemented later when we support virtual functions 
 */
typedef struct SCU_IIT_ENTRY
{
   U32  status;
   U32  remote_initiator_sas_address_hi;
   U32  remote_initiator_sas_address_lo;
   U32  remote_initiator;

} SCU_IIT_ENTRY_T;

#endif // _SCU_VIIT_DATA_HEADER_

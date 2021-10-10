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
/**
 * @file
 *
 * @brief This field defines the SCU format of an unsolicited frame (UF).  A  
 *        UF is a frame received by the SCU for which there is no known 
 *        corresponding task context (TC).
 */

#ifndef _SCU_UNSOLICITED_FRAME_H_
#define _SCU_UNSOLICITED_FRAME_H_

#include "sci_types.h"

/**
 * This constant defines the number of DWORDS found the unsolicited frame
 * header data member.
 */
#define SCU_UNSOLICITED_FRAME_HEADER_DATA_DWORDS 15

/**
 * @struct SCU_UNSOLICITED_FRAME_HEADER
 *
 * This structure delineates the format of an unsolicited frame header.
 * The first DWORD are UF attributes defined by the silicon architecture.
 * The data depicts actual header information received on the link.
 */
typedef struct SCU_UNSOLICITED_FRAME_HEADER
{
   /**
    * This field indicates if there is an Initiator Index Table entry with
    * which this header is associated.
    */
   U32 iit_exists : 1;

   /**
    * This field simply indicates the protocol type (i.e. SSP, STP, SMP).
    */
   U32 protocol_type : 3;

   /**
    * This field indicates if the frame is an address frame (IAF or OAF)
    * or if it is a information unit frame.
    */
   U32 is_address_frame : 1;

   /**
    * This field simply indicates the connection rate at which the frame
    * was received.
    */
   U32 connection_rate : 4;

   U32 reserved : 23;

   /**
    * This field represents the actual header data received on the link.
    */
   U32 data[SCU_UNSOLICITED_FRAME_HEADER_DATA_DWORDS];

} SCU_UNSOLICITED_FRAME_HEADER_T;

#endif // _SCU_UNSOLICITED_FRAME_H_

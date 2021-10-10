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

#ifndef _DRIVER_INCLUDE_TYPES_H_
#define _DRIVER_INCLUDE_TYPES_H_

/**
 * @file 
 *  
 * @brief This file contains the basic data types it redefines the operating 
 *        system data types so the driver code can use a common definition
 *        instead of a specific definition for each and every operating
 *        system.
 *
 * @note POINTER_UINT is for use with virtual pointers only.  It is
 *       not guaranteed to be the large enough (if PAE is in use for
 *       example).
 */

#include "environment.h"

#ifdef MAX_MACHINE_WORD_SIZE
#error "MAX_MACHINE_WORD_SIZE is deprecated.  Please use: OS_ARCHITECTURE, MAX_COMPILER_WORD_SIZE"
#endif // MAX_MACHINE_WORD_SIZE

#if    (MAX_COMPILER_WORD_SIZE == 32) \
    && ((OS_ARCHITECTURE == 32) || (OS_ARCHITECTURE == 64))

typedef U32 POINTER_UINT;
//typedef U32 PHYSICAL_POINTER_UINT;

#elif (MAX_COMPILER_WORD_SIZE == 64) && (OS_ARCHITECTURE == 32)

typedef U32 POINTER_UINT;
//typedef U64 PHYSICAL_POINTER_UINT;  // U64 is used to ensure systems with
                                    // PAE (Physical Address Extensions)
                                    // support operate successfully.

#elif (MAX_COMPILER_WORD_SIZE == 64) && (OS_ARCHITECTURE == 64)

typedef U64 POINTER_UINT;
//typedef U64 PHYSICAL_POINTER_UINT;

#else

#error "Unsupported configuration of: MAX_COMPILER_WORD_SIZE & OS_ARCHITECTURE"

#endif // MAX_COMPILER_WORD_SIZE && OS_ARCHITECTURE

#ifndef NULL
#define NULL ((void *)0)
#endif  // NULL

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

#ifndef BOOL
typedef U32 BOOL;
#endif

#define __STATIC_ASSERT(X,M) extern U32 __dummy[(X)?1:-1]

__STATIC_ASSERT( sizeof(U8)==1, "Error: U8 must represent 8 bits!" );
__STATIC_ASSERT( sizeof(S8)==1, "Error: S8 must represent 8 bits!" );
__STATIC_ASSERT( sizeof(U16)==2, "Error: U16 must represent 16 bits!" );
__STATIC_ASSERT( sizeof(S16)==2, "Error: S16 must represent 16 bits!" );
__STATIC_ASSERT( sizeof(U32)==4, "Error: U32 must represent 32 bits!" );
__STATIC_ASSERT( sizeof(S32)==4, "Error: S32 must represent 32 bits!" );

#if (MAX_COMPILER_WORD_SIZE == 64)
__STATIC_ASSERT( sizeof(U64)==8, "Error: U64 must represent 64 bits!" );
__STATIC_ASSERT( sizeof(S64)==8, "Error: S64 must represent 64 bits!" );
#endif // (MAX_COMPILER_WORD_SIZE == 64)

// SCI_PHYSICAL_ADDRESS must be 8 bytes.  The lower 32-bits must be
// the lower 32-bits of a 64-bit value and the uper 32-bits the upper
// 32 for a 64-bit value.
__STATIC_ASSERT( sizeof(SCI_PHYSICAL_ADDRESS)==8, "Error: SCI_PHYSICAL_ADDRESS must represent 64 bits!" );

#endif // _DRIVER_INCLUDE_TYPES_H_

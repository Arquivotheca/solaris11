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

#ifndef _SCI_LIBRARY_H_
#define _SCI_LIBRARY_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods that can be called
 *        by an SCI user on a library object.  The library is the container
 *        of all other objects being managed (i.e. controllers, target devices,
 *        sas ports, etc.).
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "sci_types.h"


/**
 * @brief This method will return the major revision level for the entire
 *        SCI library.
 * @note  Format: Major.Minor.Build.
 * 
 * @return Return an integer value indicating the major revision level.
 */
U32 sci_library_get_major_version(
   void
);

/**
 * @brief This method will return the minor revision level for the entire
 *        SCI library.
 * @note  Format: Major.Minor.Build.
 * 
 * @return Return an integer value indicating the minor revision level.
 */
U32 sci_library_get_minor_version(
   void
);

/**
 * @brief This method will return the build revision level for the entire
 *        SCI library.
 * @note  Format: Major.Minor.Build.
 * 
 * @return Return an integer value indicating the build revision level.
 */
U32 sci_library_get_build_version(
   void
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCI_LIBRARY_H_


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
 * @brief This file contains all of the method imlementations for the
 *        SCI_BASE_LIBRARY object.
 */

#include "sci_base_library.h"

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

PLACEMENT_HINTS((TBD))
U32 sci_library_get_major_version(
   void
)
{
   // Return the 32-bit value representing the major version for this SCI
   // binary.
   return __SCI_LIBRARY_MAJOR_VERSION__;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
U32 sci_library_get_minor_version(
   void
)
{
   // Return the 32-bit value representing the minor version for this SCI
   // binary.
   return __SCI_LIBRARY_MINOR_VERSION__;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
U32 sci_library_get_build_version(
   void
)
{
   // Return the 32-bit value representing the build version for this SCI
   // binary.
   return __SCI_LIBRARY_BUILD_VERSION__;
}

//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************


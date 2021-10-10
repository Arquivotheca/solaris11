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
 * @brief This file contains all of the method implementations for the
 *        SCI_BASE_OBJECT object.
 */

#include "sci_status.h"
#include "sci_types.h"
#include "sci_base_object.h"

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

#if defined(SCI_OBJECT_USE_ASSOCIATION_FUNCTIONS)
PLACEMENT_HINTS((ALWAYS_RESIDENT))
void * sci_object_get_association(
   SCI_OBJECT_HANDLE_T object
)
{
   return ((SCI_BASE_OBJECT_T *) object)->associated_object;
}
#endif

// ---------------------------------------------------------------------------

#if defined(SCI_OBJECT_USE_ASSOCIATION_FUNCTIONS)
PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_STATUS sci_object_set_association(
   SCI_OBJECT_HANDLE_T   object,
   void                * associated_object
)
{
   ((SCI_BASE_OBJECT_T *)object)->associated_object = associated_object;
   return SCI_SUCCESS;
}
#endif

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
void sci_base_object_construct(
   SCI_BASE_OBJECT_T      * base_object,
   struct SCI_BASE_LOGGER * logger
)
{
#if defined(SCI_LOGGING)
   base_object->logger = logger;
#endif // defined(SCI_LOGGING)
   base_object->associated_object = NULL;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((LOGGING))
SCI_LOGGER_HANDLE_T sci_object_get_logger(
   SCI_OBJECT_HANDLE_T object
)
{
#if defined(SCI_LOGGING)
   return sci_base_object_get_logger(object);
#else // defined(SCI_LOGGING)
   return NULL;
#endif // defined(SCI_LOGGING)
}


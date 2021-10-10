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
#ifndef _SCI_BASE_STATE_H_
#define _SCI_BASE_STATE_H_

/**
 * @file
 *
 * @brief This file contains all of the structures, constants, and methods
 *        common to all base object definitions.
 */


#include "sci_base_object.h"

typedef void (*SCI_BASE_STATE_HANDLER_T)(
   void
);

typedef void (*SCI_STATE_TRANSITION_T)(
   SCI_BASE_OBJECT_T *base_object
);

/**
 * @struct SCI_BASE_STATE
 *
 * @brief The base state object abstracts the fields common to all state
 *        objects defined in SCI.
 */
typedef struct SCI_BASE_STATE
{
   /**
    * This field indicates the defined value for this state.  After
    * initialization this field should not change.
    */
   U32  value;

   /**
    * This field is a function pointer that defines the method to be
    * invoked when the state is entered.
    */
   SCI_STATE_TRANSITION_T  enter_state;

   /**
    * This field is a function pointer that defines the method to be
    * invoked when the state is exited.
    */
   SCI_STATE_TRANSITION_T  exit_state;

} SCI_BASE_STATE_T;

#endif // _SCI_BASE_STATE_H_

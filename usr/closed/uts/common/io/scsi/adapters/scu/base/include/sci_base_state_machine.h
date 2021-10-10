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
#ifndef _SCI_BASE_STATE_MACHINE_H_
#define _SCI_BASE_STATE_MACHINE_H_

/**
 * @file
 *
 * @brief This file contains all structures, constants, or method declarations
 *        common to all state machines defined in SCI.
 */


#include "sci_base_subject.h"
#include "sci_base_state.h"


/**
 * This macro simply provides simplified retrieval of an objects state
 * handler.
 */
#define SET_STATE_HANDLER(object, table, state) \
   (object)->state_handlers = &(table)[(state)]

/**
 * @struct SCI_BASE_STATE_MACHINE
 *
 * @brief This structure defines the fields common to all state machines.
 */
typedef struct SCI_BASE_STATE_MACHINE
{
#if defined(SCI_LOGGING)
   /**
    * The state machine object participates in the observer design pattern.
    * Thus, the SCI_BASE_SUBJECT is the parent object, which allows a
    * state machine to be easily monitored by a user.
    */
   SCI_BASE_SUBJECT_T parent;
#endif // defined(SCI_LOGGING)

   /**
    * This field points to the start of the state machine's state table.
    */
   SCI_BASE_STATE_T * state_table;

   /**
    * This field points to the object to which this state machine is
    * associated.  It serves as a cookie to be provided to the state
    * enter/exit methods.
    */
   SCI_BASE_OBJECT_T * state_machine_owner;

   /**
    * This field simply indicates the state value for the state machine's
    * initial state.
    */
   U32  initial_state_id;

   /**
    * This field indicates the current state of the state machine.
    */
   U32  current_state_id;

   /**
    * This field indicates the previous state of the state machine.
    */
   U32  previous_state_id;

} SCI_BASE_STATE_MACHINE_T;

//******************************************************************************
//* P R O T E C T E D    M E T H O D S
//******************************************************************************

void sci_base_state_machine_construct(
   SCI_BASE_STATE_MACHINE_T *this_state_machine,
   SCI_BASE_OBJECT_T        *state_machine_owner,
   SCI_BASE_STATE_T         *state_table,
   U32                       initial_state
);

void sci_base_state_machine_start(
   SCI_BASE_STATE_MACHINE_T *this_state_machine
);

void sci_base_state_machine_stop(
   SCI_BASE_STATE_MACHINE_T *this_state_machine
);

void sci_base_state_machine_change_state(
   SCI_BASE_STATE_MACHINE_T *this_state_machine,
   U32                       next_state
);

U32 sci_base_state_machine_get_state(
   SCI_BASE_STATE_MACHINE_T *this_state_machine
);

#endif // _SCI_BASE_STATE_MACHINE_H_

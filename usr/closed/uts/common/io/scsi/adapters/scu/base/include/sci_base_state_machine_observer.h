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
#ifndef _SCI_BASE_STATE_MACHINE_OBSERVER_H_
#define _SCI_BASE_STATE_MACHINE_OBSERVER_H_

/**
 * @file
 *
 * @brief This file contains all of the structures, constants, and methods
 *        common to all state machine observer object definitions.
 */


#include "sci_base_observer.h"
#include "sci_base_subject.h"

#if defined(SCI_LOGGING)

/**
 * @struct SCI_BASE_STATE_MACHINE_OBSERVER
 *
 * @brief The base state machine observer structure defines the fields
 *        necessary for a user that wishes to observe the behavior (i.e.
 *        state changes) of a state machine.
 */
typedef struct SCI_BASE_STATE_MACHINE_OBSERVER
{
   /**
    * The field specifies that the parent object for the base state
    * machine observer is the base observer itself.
    */
   SCI_BASE_OBSERVER_T parent;

   /**
    * This field contains the state recorded during the last state machine
    * update.
    */
   U32  subject_state;

} SCI_BASE_STATE_MACHINE_OBSERVER_T;

/**
 * @brief This method provides default behavior for a state machine observer.
 *        This method records the state of the subject (i.e. the state
 *        machine) and returns.
 *
 * @param[in]  this_observer This parameter specifes the state machine
 *             observer in which to record the state change from the subject.
 * @param[in]  the_subject This parameter evaluates to the state machine
 *             object under observation.
 *
 * @return none
 */
void sci_base_state_machine_observer_default_update(
   SCI_BASE_OBSERVER_T *this_observer, 
   SCI_BASE_SUBJECT_T  *the_subject
);

/**
 * @brief This method constructs the supplied state machine observer.
 *
 * @param[in]  this_observer This parameter specifes the state machine
 *             observer to be constructed.
 *
 * @return none
 */
void sci_base_state_machine_observer_construct(
   SCI_BASE_STATE_MACHINE_OBSERVER_T *this_observer
);

#else // defined(SCI_LOGGING)

typedef U8 SCI_BASE_STATE_MACHINE_OBSERVER_T;

#define sci_base_state_machine_observer_default_update
#define sci_base_state_machine_observer_construct

#endif // defined(SCI_LOGGING)

#endif // _SCI_BASE_STATE_MACHINE_OBSERVER_H_

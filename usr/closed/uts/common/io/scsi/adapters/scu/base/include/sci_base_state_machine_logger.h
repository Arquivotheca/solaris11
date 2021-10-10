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
#ifndef _SCI_BASE_STATE_MACHINE_LOGGER_H_
#define _SCI_BASE_STATE_MACHINE_LOGGER_H_

/**
 * @file
 *  
 * @brief This file provides the structures and function prototypes for the 
 *        state machine logger.  The functions provided are only implemented
 *        if the SCI_LOGGING flag is enabled.
 */

#include "sci_types.h"
#include "sci_base_object.h"
#include "sci_base_state_machine.h"
#include "sci_base_state_machine_observer.h"

/**
 * This type is defined so we can pass either a core or framework logging 
 * function to the state machine logger since both have the same prototypes 
 * and this base state machine logger does not actually know which component 
 * will be doing the logging. 
 */
typedef void (*SCI_BASE_STATE_MACHINE_LOGGER_LOG_HANDLER_T)(
   SCI_LOGGER_HANDLE_T, U32, char *, ... 
);

#if defined(SCI_LOGGING)

typedef struct SCI_BASE_STATE_MACHINE_LOGGER
{
   SCI_BASE_STATE_MACHINE_OBSERVER_T parent;

   SCI_BASE_OBJECT_T                           * log_object;
   SCI_BASE_STATE_MACHINE_LOGGER_LOG_HANDLER_T   log_function;
   char                                        * log_object_name;
   char                                        * log_state_machine_name;
   U32                                           log_mask;

} SCI_BASE_STATE_MACHINE_LOGGER_T;


void sci_base_state_machine_logger_construct(
   SCI_BASE_STATE_MACHINE_LOGGER_T             * this_observer,
   SCI_BASE_OBJECT_T                           * the_object,
   SCI_BASE_STATE_MACHINE_LOGGER_LOG_HANDLER_T   the_log_function,
   char                                        * log_object_name,
   char                                        * log_state_machine_name,
   U32                                           log_object_mask
);

void sci_base_state_machine_logger_initialize(
   SCI_BASE_STATE_MACHINE_LOGGER_T             * this_observer,
   SCI_BASE_STATE_MACHINE_T                    * the_state_machine,
   SCI_BASE_OBJECT_T                           * the_object,
   SCI_BASE_STATE_MACHINE_LOGGER_LOG_HANDLER_T   the_log_function,
   char                                        * log_object_name,
   char                                        * log_state_machine_name,
   U32                                           log_object_mask
);

void sci_base_state_machine_logger_deinitialize(
   SCI_BASE_STATE_MACHINE_LOGGER_T * this_observer,
   SCI_BASE_STATE_MACHINE_T        * the_state_machine
);

#else // SCI_LOGGING

typedef U8 SCI_BASE_STATE_MACHINE_LOGGER_T;

#define sci_base_state_machine_logger_construct(this_observer,the_object,the_log_function,log_object_name,log_state_machine_name,log_object_mask)
#define sci_base_state_machine_logger_initialize(this_observer,the_state_machine,the_object,the_log_function,log_object_name,log_state_machine_name,log_object_mask)
#define sci_base_state_machine_logger_deinitialize(this_observer, the_state_machine)

#endif // SCI_LOGGING

#endif // _SCI_BASE_STATE_MACHINE_LOGGER_H_

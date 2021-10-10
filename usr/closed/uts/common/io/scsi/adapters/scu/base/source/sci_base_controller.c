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
 * @brief This file contains the base controller method implementations and
 *        any constants or structures private to the base controller object
 *        or common to all controller derived objects.
 */

#include "sci_base_controller.h"

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

PLACEMENT_HINTS((INITIALIZATION))
SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T sci_controller_get_memory_descriptor_list_handle(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   SCI_BASE_CONTROLLER_T * this_controller = (SCI_BASE_CONTROLLER_T*)controller;
   return (SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T) &this_controller->mdl;
}

//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************

PLACEMENT_HINTS((INITIALIZATION))
void sci_base_controller_construct(
   SCI_BASE_CONTROLLER_T               * this_controller,
   SCI_BASE_LOGGER_T                   * logger,
   SCI_BASE_STATE_T                    * state_table,
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T    * mdes,
   U32                                   mde_count,
   SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T   next_mdl
)
{
   sci_base_object_construct((SCI_BASE_OBJECT_T *)this_controller, logger);

   sci_base_state_machine_construct(
      &this_controller->state_machine,
      &this_controller->parent,
      state_table,
      SCI_BASE_CONTROLLER_STATE_INITIAL
   );

   sci_base_mdl_construct(&this_controller->mdl, mdes, mde_count, next_mdl);

   sci_base_state_machine_start(&this_controller->state_machine);
}


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
 * @brief This file contains the entrance and exit methods for the starting
 *        sub-state machine states (AWAIT COMPELTE).  The starting sub-state
 *        machine manages the steps necessary to initialize and configure
 *        a remote device.
 */

#include "scif_sas_remote_device.h"
#include "scif_sas_domain.h"
#include "scif_sas_logger.h"


//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************

/**
 * @brief This method implements the actions taken when entering the
 *        STARTING AWAIT COMPLETE substate.  This includes setting the
 *        state handler methods.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
PLACEMENT_HINTS((TBD))
void scif_sas_remote_device_starting_await_complete_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      fw_device,
      scif_sas_remote_device_starting_substate_handler_table,
      SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_AWAIT_COMPLETE
   );

   fw_device->domain->device_start_in_progress_count++;
   fw_device->domain->device_start_count++;
}

/**
 * @brief This method implements the actions taken when entering the
 *        STARTING COMPLETE substate.  This includes setting the
 *        state handler methods.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
PLACEMENT_HINTS((TBD))
void scif_sas_remote_device_starting_complete_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      fw_device,
      scif_sas_remote_device_starting_substate_handler_table,
      SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_AWAIT_READY
   );
}


PLACEMENT_HINTS((TBD))
SCI_BASE_STATE_T
scif_sas_remote_device_starting_substate_table
[SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_MAX_STATES] =
{
   {
      SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_AWAIT_COMPLETE,
      scif_sas_remote_device_starting_await_complete_substate_enter,
      NULL
   },
   {
      SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_AWAIT_READY,
      scif_sas_remote_device_starting_complete_substate_enter,
      NULL
   }
};


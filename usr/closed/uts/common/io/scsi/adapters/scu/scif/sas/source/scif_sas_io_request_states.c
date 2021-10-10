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
 * @brief This file contains all of the SCIF_SAS_IO_REQUEST object
 *        state entrance and exit method implementations.
 */

#include "scic_controller.h"

#include "scif_sas_io_request.h"
#include "scif_sas_domain.h"
#include "scif_sas_controller.h"
#include "scif_sas_logger.h"

//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************

/**
 * @brief This method implements the actions taken when entering the
 *        INITIAL state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_IO_REQUEST object in the method implementation.
 *
 * @return none
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
void scif_sas_io_request_initial_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *)object;

   SET_STATE_HANDLER(
      &fw_io->parent,
      scif_sas_io_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_INITIAL
   );

   // Initial state is a transitional state to the constructed state
   sci_base_state_machine_change_state(
      &fw_io->parent.parent.state_machine, SCI_BASE_REQUEST_STATE_CONSTRUCTED
   );
}

/**
 * @brief This method implements the actions taken when entering the
 *        CONSTRUCTED state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_IO_REQUEST object in the method implementation.
 *
 * @return none
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
void scif_sas_io_request_constructed_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *)object;

   SET_STATE_HANDLER(
      &fw_io->parent,
      scif_sas_io_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_CONSTRUCTED
   );
}

/**
 * @brief This method implements the actions taken when entering the
 *        STARTED state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_IO_REQUEST object in the method implementation.
 *
 * @return none
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
void scif_sas_io_request_started_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *)object;

   SET_STATE_HANDLER(
      &fw_io->parent,
      scif_sas_io_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_STARTED
   );
}

/**
 * @brief This method implements the actions taken when entering the
 *        COMPLETED state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_IO_REQUEST object in the method implementation.
 *
 * @return none
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
void scif_sas_io_request_completed_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *)object;

   SET_STATE_HANDLER(
      &fw_io->parent,
      scif_sas_io_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_COMPLETED
   );
}

/**
 * @brief This method implements the actions taken when entering the
 *        ABORTING state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_IO_REQUEST object in the method implementation.
 *
 * @return none
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
void scif_sas_io_request_aborting_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *)object;

   SCIF_LOG_WARNING((
      sci_base_object_get_logger(fw_io),
      SCIF_LOG_OBJECT_IO_REQUEST | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "Domain:0x%p Device:0x%p IORequest:0x%p terminating\n",
      fw_io->parent.device->domain, fw_io->parent.device, fw_io
   ));

   SET_STATE_HANDLER(
      &fw_io->parent,
      scif_sas_io_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_ABORTING
   );

   fw_io->parent.status = scif_sas_request_terminate_start(
                             &fw_io->parent, fw_io->parent.core_object
                          );
}

/**
 * @brief This method implements the actions taken when exiting the
 *        ABORTING state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_IO_REQUEST object in the method implementation.
 *
 * @return none
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
void scif_sas_io_request_aborting_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REQUEST_T * fw_request = (SCIF_SAS_REQUEST_T *)object;
   scif_sas_request_terminate_complete(fw_request);
}

/**
 * @brief This method implements the actions taken when entering the
 *        FINAL state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_IO_REQUEST object in the method implementation.
 *
 * @return none
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
void scif_sas_io_request_final_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *)object;

   SET_STATE_HANDLER(
      &fw_io->parent,
      scif_sas_io_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_FINAL
   );
}

PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_BASE_STATE_T scif_sas_io_request_state_table[SCI_BASE_REQUEST_MAX_STATES] =
{
   {
      SCI_BASE_REQUEST_STATE_INITIAL,
      scif_sas_io_request_initial_state_enter,
      NULL
   },
   {
      SCI_BASE_REQUEST_STATE_CONSTRUCTED,
      scif_sas_io_request_constructed_state_enter,
      NULL
   },
   {
      SCI_BASE_REQUEST_STATE_STARTED,
      scif_sas_io_request_started_state_enter,
      NULL
   },
   {
      SCI_BASE_REQUEST_STATE_COMPLETED,
      scif_sas_io_request_completed_state_enter,
      NULL
   },
   {
      SCI_BASE_REQUEST_STATE_ABORTING,
      scif_sas_io_request_aborting_state_enter,
      scif_sas_io_request_aborting_state_exit
   },
   {
      SCI_BASE_REQUEST_STATE_FINAL,
      scif_sas_io_request_final_state_enter,
      NULL
   },
};


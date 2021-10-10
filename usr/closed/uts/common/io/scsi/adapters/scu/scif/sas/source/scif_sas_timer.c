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
 * @brief This file contains core callback notificiations implemented by
 *        the framework for timers.
 */

#include "sci_types.h"
#include "scif_user_callback.h"

#include "scif_sas_controller.h"

PLACEMENT_HINTS((TBD))
void * scic_cb_timer_create(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCI_TIMER_CALLBACK_T      timer_callback,
   void                    * cookie
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T *)
                                         sci_object_get_association(controller);

   return scif_cb_timer_create(fw_controller, timer_callback, cookie);
}

// -----------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
void scic_cb_timer_destroy(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * timer
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T *)
                                         sci_object_get_association(controller);

   scif_cb_timer_destroy(fw_controller, timer);
}

// -----------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
void scic_cb_timer_start(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * timer,
   U32                       milliseconds
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T *)
                                         sci_object_get_association(controller);

   scif_cb_timer_start(fw_controller, timer, milliseconds);
}

// -----------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
void scic_cb_timer_stop(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * timer
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T *)
                                         sci_object_get_association(controller);

   scif_cb_timer_stop(fw_controller, timer);
}


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
#include "sati.h"
#include "scif_sas_logger.h"
#include "scif_sas_task_request.h"
#include "scif_sas_controller.h"
#include "scif_sas_remote_device.h"
#include "scif_sas_stp_task_request.h"
#include "scic_task_request.h"

/**
 * @brief This method provides SATA/STP STARTED state specific handling for
 *        when the user attempts to complete the supplied IO request.
 *        It will perform data/response translation and free NCQ tags 
 *        if necessary.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be started.
 *
 * @return This method returns a value indicating if the IO request was
 *         successfully completed or not.
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_STATUS scif_sas_stp_core_cb_task_request_complete_handler(
   SCIF_SAS_CONTROLLER_T    * fw_controller,
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REQUEST_T       * fw_request,
   SCI_STATUS               * completion_status
)
{
#if !defined(DISABLE_SATI_TASK_MANAGEMENT)
   SCIF_SAS_TASK_REQUEST_T * fw_task = (SCIF_SAS_TASK_REQUEST_T *) fw_request;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_stp_core_cb_task_request_complete_handler(0x%p, 0x%p, 0x%p, 0x%x) enter\n",
      fw_controller, fw_device, fw_request, *completion_status
   ));

   // Translating the response is only necessary if some sort of error
   // occurred resulting in having the error bit set in the ATA status
   // register and values to decode in the ATA error register.
   if (  (*completion_status == SCI_SUCCESS)
      || (*completion_status == SCI_FAILURE_IO_RESPONSE_VALID) )
   {
      SATI_STATUS sati_status = sati_translate_task_response(
                                   &fw_task->parent.stp.sequence,
                                   fw_task,
                                   fw_task
                                );

      if (sati_status == SATI_COMPLETE)
         *completion_status = SCI_SUCCESS;
      else if (sati_status == SATI_FAILURE_CHECK_RESPONSE_DATA)
         *completion_status = SCI_FAILURE_IO_RESPONSE_VALID;
      else if (sati_status == SATI_SEQUENCE_INCOMPLETE)
      {
         // The translation indicates that additional SATA requests are
         // necessary to finish the original SCSI request.  As a result,
         // do not complete the IO and begin the next stage of the
         // translation.
         /// @todo multiple ATA commands are required, but not supported yet.
         return SCI_FAILURE;
      }
      else
      {
         // Something unexpected occurred during translation.  Fail the
         // IO request to the user.
         *completion_status = SCI_FAILURE;
      }
   }
   return SCI_SUCCESS;
#else // !defined(DISABLE_SATI_TASK_MANAGEMENT)
   return SCI_FAILURE;
#endif // !defined(DISABLE_SATI_TASK_MANAGEMENT)
}

/**
 * @file
 *
 * @brief This file contains the method implementations for the
 *        SCIF_SAS_STP_TASK_REQUEST object.  The contents will implement
 *        SATA/STP specific functionality.
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_STATUS scif_sas_stp_task_request_construct(
   SCIF_SAS_TASK_REQUEST_T * fw_task
)
{
   SCI_STATUS                 sci_status = SCI_FAILURE;

#if !defined(DISABLE_SATI_TASK_MANAGEMENT)
   SATI_STATUS                sati_status;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device  = fw_task->parent.device;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_task),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_stp_task_request_construct(0x%p) enter\n",
      fw_task
   ));

   // The translator will indirectly invoke core methods to set the fields
   // of the ATA register FIS inside of this method.
   sati_status = sati_translate_task_management(
                    &fw_task->parent.stp.sequence,
                    &fw_device->protocol_device.stp_device.sati_device,
                    fw_task,
                    fw_task
                 );

   if (sati_status == SATI_SUCCESS)
   {
      sci_status = scic_task_request_construct_sata(fw_task->parent.core_object);
      //fw_task->parent.state_handlers = &stp_io_request_constructed_handlers;
      fw_task->parent.protocol_complete_handler = 
         scif_sas_stp_core_cb_task_request_complete_handler;
   }
   else
   {
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_task),
         SCIF_LOG_OBJECT_TASK_MANAGEMENT,
         "Task 0x%p received unexpected SAT translation failure 0x%x\n",
         fw_task, sati_status
      ));
   }
#endif // !defined(DISABLE_SATI_TASK_MANAGEMENT)

   return sci_status;
}


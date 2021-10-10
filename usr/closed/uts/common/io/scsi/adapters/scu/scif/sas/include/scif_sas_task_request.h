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
#ifndef _SCIF_SAS_TASK_REQUEST_H_
#define _SCIF_SAS_TASK_REQUEST_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_TASK_REQUEST object.  
 */

#include "sati_translator_sequence.h"
#include "scif_task_request.h"
#include "sci_base_request.h"
#include "scif_sas_request.h"
#include "scif_sas_internal_io_request.h"
#include "intel_sas.h"

/**
 * @struct SCIF_SAS_TASK_REQUEST
 *
 * @brief The SCI SAS Framework Task request object abstracts the SAS task
 *        management behavior for the framework component.  Additionally,
 *        it provides a higher level of abstraction for the core task
 *        request object.
 */
typedef struct SCIF_SAS_TASK_REQUEST
{
   /**
    * The SCIF_SAS_REQUEST is the parent object for the
    * SCIF_SAS_TASK_REQUEST object.
    */
   SCIF_SAS_REQUEST_T  parent;

   /**
    * This field contains the number of current requests affected by 
    * this task management request.  This number indicates all of the
    * requests terminated in the silicon (including previous task requests).
    */
   U16  affected_request_count;

   /**
    * This field specifies the tag for the IO request or the tag to be
    * managed for a task management request.
    * This field is utilized during internal IO requests.
    */
   U16  io_tag_to_manage;

   /**
    * This field will be utilized to specify the task management function
    * of this task request.
    */
   SCI_SAS_TASK_MGMT_FUNCTION_T function;

} SCIF_SAS_TASK_REQUEST_T;

extern SCI_BASE_STATE_T scif_sas_task_request_state_table[];
extern SCI_BASE_REQUEST_STATE_HANDLER_T
   scif_sas_task_request_state_handler_table[];

void scif_sas_task_request_operation_complete(
   SCIF_SAS_TASK_REQUEST_T * fw_task
);

U8 scif_sas_task_request_get_function(
   SCIF_SAS_TASK_REQUEST_T * fw_task
);

SCI_STATUS scif_sas_internal_task_request_construct(
   SCI_CONTROLLER_HANDLE_T      scif_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scif_remote_device,
   U16                          io_tag,
   void                       * task_request_memory,
   SCI_TASK_REQUEST_HANDLE_T  * scif_task_request,
   U8                           task_function
);

void scif_sas_internal_task_request_destruct(
   SCIF_SAS_TASK_REQUEST_T * fw_internal_task
);

#endif // _SCIF_SAS_TASK_REQUEST_H_


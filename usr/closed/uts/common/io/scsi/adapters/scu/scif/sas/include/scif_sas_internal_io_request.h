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
#ifndef _SCIF_SAS_INTERNAL_REQUEST_H_
#define _SCIF_SAS_INTERNAL_REQUEST_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_INTERNAL_REQUEST object.
 */


#include "scif_io_request.h"
#include "sci_base_request.h"
#include "scif_sas_request.h"
#include "scif_sas_io_request.h"
#include "scif_sas_remote_device.h"
#include "intel_sas.h"
#include "sci_controller_constants.h"

struct SCIF_SAS_CONTROLLER;

/**
 * This constant dictates the number of internal framework IO request
 * objects.  
 */
#define SCIF_SAS_INTERNAL_REQUEST_COUNT (SCI_MAX_DOMAINS*4)

/*
 * This constant indicates the timeout value of an internal IO request
 * in mili-seconds.
 */
#define SCIF_SAS_INTERNAL_REQUEST_TIMEOUT   500

/**
 * @struct SCIF_SAS_INTERNAL_IO_REQUEST
 *
 * @brief The SCIF_SAS_INTERNAL_IO_REQUEST object represents the internal SAS
 *        IO request behavior for the framework component.
 */
typedef struct SCIF_SAS_INTERNAL_IO_REQUEST
{
   /**
    * The SCIF_SAS_IO_REQUEST is the parent object for the
    * SCIF_SAS_INTERNAL_IO_REQUEST object.
    */
   SCIF_SAS_IO_REQUEST_T  parent;
   
   /**
    * This field will be utilized only by internal IO to handle timeout
    * situation.
    */
   void * internal_io_timer;

}SCIF_SAS_INTERNAL_IO_REQUEST_T;


extern SCI_BASE_STATE_T scif_sas_io_request_state_table[];
extern SCI_BASE_REQUEST_STATE_HANDLER_T
   scif_sas_io_request_state_handler_table[];


U32 scif_sas_internal_request_get_object_size(
   void
);

SCI_STATUS scif_sas_internal_io_request_construct_smp(
   struct SCIF_SAS_CONTROLLER  * fw_controller,
   SCIF_SAS_REMOTE_DEVICE_T    * fw_remote_device,
   void                        * internal_io_memory,
   U16                           io_tag,
   SMP_REQUEST_T               * smp_command
);

SCI_STATUS scif_sas_internal_io_request_construct_stp(
   SCIF_SAS_INTERNAL_IO_REQUEST_T * fw_io
);

void scif_sas_internal_io_request_timeout_handler(
   void * fw_io
);

void scif_sas_internal_io_request_complete(
   struct SCIF_SAS_CONTROLLER     * fw_controller,
   SCIF_SAS_INTERNAL_IO_REQUEST_T * fw_io,
   SCI_STATUS                       completion_status
);

void scif_sas_internal_io_request_destruct(
   struct SCIF_SAS_CONTROLLER     * fw_controller,
   SCIF_SAS_INTERNAL_IO_REQUEST_T * fw_internal_io
);

#endif // _SCIF_SAS_IO_REQUEST_H_


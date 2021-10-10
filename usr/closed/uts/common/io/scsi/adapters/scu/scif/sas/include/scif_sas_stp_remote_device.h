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
#ifndef _SCIF_SAS_STP_REMOTE_DEVICE_H_
#define _SCIF_SAS_STP_REMOTE_DEVICE_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_STP_REMOTE_DEVICE object.  
 */

#include "sati_device.h"

#define SCIF_SAS_INVALID_NCQ_TAG 0xFF

/**
 * @struct SCIF_SAS_STP_REMOTE_DEVICE
 *
 * @brief The SCI SAS STP Framework remote device object abstracts the SAS
 *        SATA/STP remote device level behavior for the framework component.
 */
typedef struct SCIF_SAS_STP_REMOTE_DEVICE
{
   /**
    * This field contains all of the data utilized by the SCSI-to-ATA
    * Translation Implementation (SATI).
    */
   SATI_DEVICE_T  sati_device;

   /**
    * This field contains a list of free NCQ tags available for use in
    * SATA Native Command Queuing (NCQ) requests.
    */
   U32 s_active;

} SCIF_SAS_STP_REMOTE_DEVICE_T;

struct SCIF_SAS_REMOTE_DEVICE;
void scif_sas_stp_remote_device_construct(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

U8 scif_sas_stp_remote_device_allocate_ncq_tag(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_stp_remote_device_free_ncq_tag(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   U8                              ncq_tag
);

struct SCIF_SAS_REQUEST *
scif_sas_stp_remote_device_get_request_by_ncq_tag(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   U8                              ncq_tag
);

#endif // _SCIF_SAS_STP_REMOTE_DEVICE_H_


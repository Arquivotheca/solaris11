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

#ifndef _SATI_MODE_SENSE_H_
#define _SATI_MODE_SENSE_H_

/**
 * @file
 * @brief This file contains the method declarations and type defintions
 *        common to translations of the SCSI mode sense (6 and 10-byte)
 *        commands.
 */

#include "sati_types.h"
#include "sati_translator_sequence.h"
#include "intel_ata.h"

U16 sati_mode_sense_calculate_page_header(
   void * scsi_io,
   U8     cdb_size
);

SATI_STATUS sati_mode_sense_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U8                           cdb_length
);

U32 sati_mode_sense_build_std_block_descriptor(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
);

U32 sati_mode_sense_copy_initial_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U32                          page_start,
   U8                           page_control,
   U8                           page_code
);

U32 sati_mode_sense_caching_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
);

U32 sati_mode_sense_informational_excp_control_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
);

U32 sati_mode_sense_all_pages_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
);

U32 sati_mode_sense_read_write_error_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
);

U32 sati_mode_sense_control_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
);

U32 sati_mode_sense_disconnect_reconnect_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
);

U32 sati_mode_sense_power_condition_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
);


#endif // _SATI_MODE_SENSE_H_


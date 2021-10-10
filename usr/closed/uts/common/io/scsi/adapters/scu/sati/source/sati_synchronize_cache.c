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
 * @brief This file contains the method implementations for translating
 *        the SCSI SYNCHRONIZE_CACHE (10, 16-byte) commands.
 */

#if !defined(DISABLE_SATI_SYNCHRONIZE_CACHE)

#include "sati_synchronize_cache.h"
#include "sati_callbacks.h"
#include "sati_util.h"
#include "sati_translator_sequence.h"

#include "intel_ata.h"
#include "intel_scsi.h"
#include "intel_sat.h"

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method performs the SCSI SYNCHRONIZE_CACHE 10 and 16 command translation
 *        functionality common to all SYNCHRONIZE_CACHE command sizes.
 *        This includes:
 *        - setting the command register
 *        - setting the device head register
 *        - filling in fields in the SATI_TRANSLATOR_SEQUENCE object.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the method was successfully completed.
 * @retval SATI_SUCCESS This is returned in all other cases.
 */
PLACEMENT_HINTS((SATI_SYNCHRONIZE_CACHE))
SATI_STATUS sati_synchronize_cache_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);
   
   sequence->type           = SATI_SEQUENCE_SYNCHRONIZE_CACHE;
   sequence->protocol       = SAT_PROTOCOL_NON_DATA;
   sequence->data_direction = SATI_DATA_DIRECTION_NONE;

   if (sati_get_cdb_byte(cdb, 1) & SCSI_SYNCHRONIZE_CACHE_IMMED_ENABLED)
   {
      //currently we ignore immed bit. 
      ;
   }

   // Ensure the device supports the 48 bit feature set.
   if (sequence->device->capabilities & SATI_DEVICE_CAP_48BIT_ENABLE)
      sati_set_ata_command(register_fis, ATA_FLUSH_CACHE_EXT);
   else
      sati_set_ata_command(register_fis, ATA_FLUSH_CACHE);

   return SATI_SUCCESS;
}

#endif // !defined(DISABLE_SATI_SYNCHRONIZE_CACHE)


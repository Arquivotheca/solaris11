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
 * @brief This file contains the method implementations required to 
 *        translate the SCSI lun reset command.
 */

#if !defined(DISABLE_SATI_TASK_MANAGEMENT)

#include "sati_callbacks.h"
#include "sati_util.h"
#include "intel_ata.h"
#include "intel_scsi.h"
#include "intel_sat.h"

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

/**
 * @brief This method will translate the lun reset SCSI task request into an 
 *        ATA SOFT RESET command. For more information on the parameters
 *        passed to this method, please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 *  
 * @note It is up to the user of the sata translator to set the command bit 
 *       and clear the control softreset bit and send the second register fis.
 */
PLACEMENT_HINTS((SATI_TASK_MANAGEMENT))
SATI_STATUS sati_lun_reset_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8* register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_NOP);
   sati_set_ata_control(register_fis, ATA_CONTROL_REG_SOFT_RESET_BIT);

   //set all other fields to zero.
   sati_clear_sata_command_flag(register_fis);
   sati_set_ata_features(register_fis, 0);
   sati_set_ata_features_exp(register_fis, 0);
   sati_set_ata_sector_count(register_fis, 0);
   sati_set_ata_sector_count_exp(register_fis, 0);
   sati_set_ata_lba_low(register_fis, 0);
   sati_set_ata_lba_mid(register_fis, 0);
   sati_set_ata_lba_high(register_fis, 0);
   sati_set_ata_lba_low_exp(register_fis, 0);
   sati_set_ata_lba_mid_exp(register_fis, 0);
   sati_set_ata_lba_high_exp(register_fis, 0);
   sati_set_ata_device_head(register_fis, 0);
   
   sequence->type                = SATI_SEQUENCE_LUN_RESET;
   sequence->data_direction      = SATI_DATA_DIRECTION_NONE;
   sequence->protocol            = SAT_PROTOCOL_SOFT_RESET;
   sequence->ata_transfer_length = 0;

   return SATI_SUCCESS;
}

#endif // !defined(DISABLE_SATI_TASK_MANAGEMENT)


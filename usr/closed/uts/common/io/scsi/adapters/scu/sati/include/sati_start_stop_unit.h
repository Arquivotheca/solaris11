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

#ifndef _SATI_START_STOP_UNIT_H_
#define _SATI_START_STOP_UNIT_H_

/**
 * @file
 * @brief This file contains the method declarations and type definitions
 *        required to translate the SCSI start_stop_unit commands.
 */

#include "intel_scsi.h"
#include "sati_types.h"
#include "sati_translator_sequence.h"

#define SATI_START_STOP_UNIT_POWER_CONDITION(cdb) \
   (( sati_get_cdb_byte(cdb, 4) & SCSI_START_STOP_UNIT_POWER_CONDITION_MASK ) \
    >> SCSI_START_STOP_UNIT_POWER_CONDITION_SHIFT)

#define SATI_START_STOP_UNIT_START_BIT(cdb) \
   (( sati_get_cdb_byte(cdb, 4) & SCSI_START_STOP_UNIT_START_BIT_MASK ) \
    >> SCSI_START_STOP_UNIT_START_BIT_SHIFT)	

#define SATI_START_STOP_UNIT_LOEJ_BIT(cdb) \
   (( sati_get_cdb_byte(cdb, 4) & SCSI_START_STOP_UNIT_LOEJ_BIT_MASK ) \
    >> SCSI_START_STOP_UNIT_LOEJ_BIT_SHIFT)	

#define SATI_START_STOP_UNIT_NO_FLUSH_BIT(cdb) \
   (( sati_get_cdb_byte(cdb, 4) & SCSI_START_STOP_UNIT_NO_FLUSH_MASK ) \
    >> SCSI_START_STOP_UNIT_NO_FLUSH_SHIFT)	   

#define SATI_START_STOP_UNIT_IMMED_BIT(cdb) \
   (( sati_get_cdb_byte(cdb, 1) & SCSI_START_STOP_UNIT_IMMED_MASK ) \
    >> SCSI_START_STOP_UNIT_IMMED_SHIFT)  
    
#define SATI_START_STOP_UNIT_POWER_CONDITION_MODIFIER(cdb) \
   (( sati_get_cdb_byte(cdb, 3) & SCSI_START_STOP_UNIT_POWER_CONDITION_MODIFIER_MASK) \
   >> SCSI_START_STOP_UNIT_POWER_CONDITION_MODIFIER_SHIFT)

SATI_STATUS sati_start_stop_unit_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
);

SATI_STATUS sati_start_stop_unit_translate_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
);
#endif // _SATI_START_STOP_UNIT_H_

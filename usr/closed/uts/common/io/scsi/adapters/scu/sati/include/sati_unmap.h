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

#ifndef _SATI_UNMAP_H_
#define _SATI_UNMAP_H_

/**
 * @file
 * @brief This file contains the method implementations required to 
 *        translate the SCSI reassign blocks command.
 */

#if !defined(DISABLE_SATI_UNMAP)
 
#include "sati_types.h"
#include "sati_translator_sequence.h"

#define SATI_DSM_MAX_SECTOR_COUNT                     0xFFFF
#define SATI_DSM_MAX_SECTOR_ADDRESS                   0xFFFFFFFFFFFF
#define SATI_DSM_MAX_BUFFER_SIZE                      4096

#define SATI_UNMAP_SIZEOF_SCSI_UNMAP_BLOCK_DESCRIPTOR 16
#define SATI_UNMAP_SIZEOF_SCSI_UNMAP_PARAMETER_LIST   8

typedef struct _TRIM_PAIR
{
    U64 sector_address:48;
    U64 sector_count:16;
} TRIM_PAIR, *PTRIM_PAIR;

U32 sati_unmap_calculate_dsm_blocks(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U32                          dsm_descriptor_count
);

SATI_STATUS sati_unmap_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U32                          sector_count
);

SATI_STATUS sati_unmap_load_next_descriptor(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io
);

U32 sati_unmap_get_max_buffer_size_in_blocks(
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

SATI_STATUS sati_unmap_initial_processing(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io 
);

SATI_STATUS sati_unmap_process(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io 
);

SATI_STATUS sati_unmap_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
);

SATI_STATUS sati_unmap_translate_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
);

void sati_unmap_terminate(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
);
#else // !defined(DISABLE_SATI_UNMAP)
#define sati_unmap_terminate(sequence,scsi_io,ata_io)
#endif // !defined(DISABLE_SATI_UNMAP)

#endif // _SATI_UNMAP_H_

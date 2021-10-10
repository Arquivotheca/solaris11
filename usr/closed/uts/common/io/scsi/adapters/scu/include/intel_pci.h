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

#ifndef _INTEL_PCI_H_
#define _INTEL_PCI_H_

/**
 * @file
 *
 * @brief This file contains all of the definitions relating to structures,
 *        constants, etc. defined by the PCI specification.
 */

#include "types.h"

#define SCIC_SDS_PCI_REVISION_A0 0
#define SCIC_SDS_PCI_REVISION_A2 2
#define SCIC_SDS_PCI_REVISION_B0 4

typedef struct sci_pci_common_header
{
   // Offset 0x00
   U16 vendor_id;
   U16 device_id;

   // Offset 0x04
   U16 command;
   U16 status;

   // Offset 0x08
   U8  revision;
   U8  program_interface;
   U8  sub_class;
   U8  base_class;

   // Offset 0x0C
   U8  cache_line_size;
   U8  master_latency_timer;
   U8  header_type;
   U8  built_in_self_test;

} SCI_PCI_COMMON_HEADER_T;

#endif // _INTEL_PCI_H_

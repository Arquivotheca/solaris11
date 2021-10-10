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
#ifndef _SCIC_SDS_PCI_H_
#define _SCIC_SDS_PCI_H_

/**
 * @file
 *
 * @brief This file contains the prototypes/macros utilized in writing
 *        out PCI data for the SCI core.
 */

#include "sci_types.h"

#define PATSBURG_SMU_BAR       0
#define PATSBURG_SCU_BAR       1
#define PATSBURG_IO_SPACE_BAR0 2
#define PATSBURG_IO_SPACE_BAR1 3

enum SCU_CONTROLLER_PCI_REVISION_CODE
{
   SCU_PBG_HBA_REV_A0 = 1,
   SCU_PBG_HBA_REV_A2 = 3,
   SCU_PBG_HBA_REV_B0 = 4
};

struct SCIC_SDS_CONTROLLER;

void scic_sds_pci_bar_initialization(
   struct SCIC_SDS_CONTROLLER * this_controller
);

#if !defined(ENABLE_PCI_IO_SPACE_ACCESS) || defined(ARLINGTON_BUILD)

#define scic_sds_pci_read_smu_dword  scic_cb_pci_read_dword
#define scic_sds_pci_write_smu_dword scic_cb_pci_write_dword
#define scic_sds_pci_read_scu_dword  scic_cb_pci_read_dword
#define scic_sds_pci_write_scu_dword scic_cb_pci_write_dword

#else // !defined(ENABLE_PCI_IO_SPACE_ACCESS)

// These two registers form the Data/Index pair equivalent in the
// SCU. They are only used for access registers in BAR 1, not BAR 0.
#define SCU_MMR_ADDRESS_WINDOW_OFFSET 0xA0
#define SCU_MMR_DATA_WINDOW_OFFSET    0xA4

U32 scic_sds_pci_read_smu_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * address
);

void scic_sds_pci_write_smu_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * address,
   U32                       write_value
);

U32 scic_sds_pci_read_scu_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * address
);

void scic_sds_pci_write_scu_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * address,
   U32                       write_value
);

#endif // !defined(ENABLE_PCI_IO_SPACE_ACCESS)

#endif // _SCIC_SDS_PCI_H_

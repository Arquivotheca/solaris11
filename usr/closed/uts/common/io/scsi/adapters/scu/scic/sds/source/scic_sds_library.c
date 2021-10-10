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
 * @brief This file contains the implementation of the public methods for a 
 *        SCIC_SDS_LIBRARY object.
 */

#include "scic_sds_library.h"
#include "scic_sds_controller.h"
#include "scic_sds_request.h"
#include "scic_sds_remote_device.h"
#include "intel_pci.h"
#include "scic_sds_pci.h"
#include "scu_constants.h"

struct SCIC_SDS_CONTROLLER;

#define SCIC_LIBRARY_CONTROLLER_MEMORY_START(library) \
   ((char *)(library) + sizeof(SCIC_SDS_LIBRARY_T))

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
U32 scic_library_get_object_size(
   U8 max_controller_count
)
{
   return   sizeof(SCIC_SDS_LIBRARY_T)
          + scic_sds_controller_get_object_size() * max_controller_count;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
SCI_LIBRARY_HANDLE_T scic_library_construct(
   void                    * library_memory,
   U8                        max_controller_count
)
{
   SCI_STATUS status;
   SCIC_SDS_LIBRARY_T *this_library;

   this_library = (SCIC_SDS_LIBRARY_T *)library_memory;

   this_library->max_controller_count = max_controller_count;

   this_library->controllers =
      (SCIC_SDS_CONTROLLER_T *)((char *)library_memory + sizeof(SCIC_SDS_LIBRARY_T));

   SCI_BASE_LIBRARY_CONSTRUCT(this_library,
                              &this_library->parent,
                              max_controller_count,
                              struct SCIC_SDS_CONTROLLER,
                              status);
   return this_library;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
void scic_library_set_pci_info(
   SCI_LIBRARY_HANDLE_T      library,
   SCI_PCI_COMMON_HEADER_T * pci_header
)
{
   SCIC_SDS_LIBRARY_T *this_library;
   this_library = (SCIC_SDS_LIBRARY_T *)library;

   this_library->pci_device   = pci_header->device_id;

#if defined(PBG_HBA_A0_BUILD)
   this_library->pci_revision = SCIC_SDS_PCI_REVISION_A0;
#elif defined(PBG_HBA_A2_BUILD)
   this_library->pci_revision = SCIC_SDS_PCI_REVISION_A2;
#elif defined(PBG_HBA_BETA_BUILD)
   this_library->pci_revision = SCIC_SDS_PCI_REVISION_B0;
#elif defined(PBG_BUILD)
   // The SCU PCI function revision ID for A0/A2 is not populated
   // properly.  As a result, we force the revision ID to A2 for
   // this situation.  Therefore, the standard PBG build will not
   // work for A0.
   if (pci_header->revision == SCIC_SDS_PCI_REVISION_A0)
      this_library->pci_revision = SCIC_SDS_PCI_REVISION_A2;
   else
      this_library->pci_revision = pci_header->revision;
#endif
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION)) 
SCI_STATUS scic_library_allocate_controller(
   SCI_LIBRARY_HANDLE_T    library,
   SCI_CONTROLLER_HANDLE_T *new_controller
)
{
   SCI_STATUS status;
   SCIC_SDS_LIBRARY_T *this_library;

   this_library = (SCIC_SDS_LIBRARY_T *)library;

   if (
         (  (this_library->pci_device >= 0x1D60) 
         && (this_library->pci_device <= 0x1D62)
         )
      || (  (this_library->pci_device >= 0x1D64) 
         && (this_library->pci_device <= 0x1D65)
         )
      || (  (this_library->pci_device >= 0x1D68) 
         && (this_library->pci_device <= 0x1D6F)
         )
      )
   {
      SCI_BASE_LIBRARY_ALLOCATE_CONTROLLER(
         this_library, new_controller, &status);
   }
   else
      status = SCI_FAILURE_UNSUPPORTED_PCI_DEVICE_ID;

   return status;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
SCI_STATUS scic_library_free_controller(
   SCI_LIBRARY_HANDLE_T library,
   SCI_CONTROLLER_HANDLE_T controller
)
{
   SCI_STATUS status;
   SCIC_SDS_LIBRARY_T *this_library;
   this_library = (SCIC_SDS_LIBRARY_T *)library;

   SCI_BASE_LIBRARY_FREE_CONTROLLER(
      this_library, controller, struct SCIC_SDS_CONTROLLER, &status);

   return status;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION)) 
U8 scic_library_get_pci_device_controller_count(
   SCI_LIBRARY_HANDLE_T library
)
{
   SCIC_SDS_LIBRARY_T *this_library;
   U16 device_id;

   this_library = (SCIC_SDS_LIBRARY_T *)library;
   device_id = this_library->pci_device;
   
   //Check if we are on a b0 which has 2 controllers
   if (((device_id != 0x1D61) && (device_id != 0x1D65) && 
       (device_id != 0x1D69) && (device_id != 0x1D6B)) &&
       (this_library->pci_revision == SCU_PBG_HBA_REV_B0))
      return 2;
   else
      return 1;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
U32 scic_library_get_max_sge_size(
   SCI_LIBRARY_HANDLE_T library
)
{
   return SCU_IO_REQUEST_MAX_SGE_SIZE;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
U32 scic_library_get_max_sge_count(
   SCI_LIBRARY_HANDLE_T library
)
{
   return SCU_IO_REQUEST_SGE_COUNT;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
U32 scic_library_get_max_io_length(
   SCI_LIBRARY_HANDLE_T library
)
{
   return SCU_IO_REQUEST_MAX_TRANSFER_LENGTH;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
U16 scic_library_get_min_timer_count(void)
{
   return (U16) (scic_sds_controller_get_min_timer_count()
               + scic_sds_remote_device_get_min_timer_count()
               + scic_sds_request_get_min_timer_count());
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
U16 scic_library_get_max_timer_count(void)
{
   return (U16) (scic_sds_controller_get_max_timer_count()
               + scic_sds_remote_device_get_max_timer_count()
               + scic_sds_request_get_max_timer_count());
}

/**
 *
 */
U8 scic_sds_library_get_controller_index(
   SCIC_SDS_LIBRARY_T    * library,
   SCIC_SDS_CONTROLLER_T * controller
)
{
   U8 index;

   for (index = 0; index < library->max_controller_count; index++)
   {
      if (controller == &library->controllers[index])
      {
         return index;
      }
   }

   return 0xff;
}


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
 * @brief This file contains the base implementation for the memory
 *        descriptor list decorator.  The decorator adds additional
 *        functionality that may be used by SCI users to help
 *        offload MDE processing.
 */

#include "sci_memory_descriptor_list_decorator.h"

PLACEMENT_HINTS((INITIALIZATION))
U32 sci_mdl_decorator_get_memory_size(
   SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T mdl,
   U32                                 attributes
)
{
   U32                                size = 0;
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T * mde;
   
   sci_mdl_first_entry(mdl);
   mde = sci_mdl_get_current_entry(mdl);
   while (mde != NULL)
   {
      if (  (mde->constant_memory_attributes == attributes)
         || (attributes == 0) )
         size += (mde->constant_memory_size + mde->constant_memory_alignment);

      sci_mdl_next_entry(mdl);
      mde = sci_mdl_get_current_entry(mdl);
   }

   return size;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
void sci_mdl_decorator_assign_memory(
   SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T mdl,
   U32                                 attributes,
   POINTER_UINT                        virtual_address,
   SCI_PHYSICAL_ADDRESS                sci_physical_address
)
{
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T * mde;
   U64  physical_address;

   physical_address
      =   ((U64) sci_cb_physical_address_lower(sci_physical_address))
        | (((U64) sci_cb_physical_address_upper(sci_physical_address)) << 32);

   sci_mdl_first_entry(mdl);
   mde = sci_mdl_get_current_entry(mdl);
   while (mde != NULL)
   {
      // As long as the memory attribute for this MDE is equivalent to
      // those supplied by the caller, then fill out the appropriate
      // MDE fields.
      if (  (mde->constant_memory_attributes == attributes)
         || (attributes == 0) )
      {
         // Ensure the virtual address alignment rules are met.
         if ((virtual_address % mde->constant_memory_alignment) != 0)
         {
            virtual_address
               += (mde->constant_memory_alignment -
                   (virtual_address % mde->constant_memory_alignment));
         }

         // Ensure the physical address alignment rules are met.
         if ((physical_address % mde->constant_memory_alignment) != 0)
         {
            physical_address
               += (mde->constant_memory_alignment -
                   (physical_address % mde->constant_memory_alignment));
         }

         // Update the MDE with properly aligned address values.
         mde->virtual_address  = (void *)virtual_address;
         sci_cb_make_physical_address(
            mde->physical_address,
            (U32) (physical_address >> 32),
            (U32) (physical_address & 0xFFFFFFFF)
         );

         virtual_address  += mde->constant_memory_size;
         physical_address += mde->constant_memory_size;
      }

      // Move on to the next MDE
      sci_mdl_next_entry(mdl);
      mde = sci_mdl_get_current_entry (mdl);
   }
}


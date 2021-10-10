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
#ifndef _SCI_BASE_MEMORY_DESCRIPTOR_LIST_H_
#define _SCI_BASE_MEMORY_DESCRIPTOR_LIST_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants
 *        and interface methods for the SCI_BASE_MEMORY_DESCRIPTOR_LIST
 *        object.
 */


#include "sci_types.h"
#include "sci_memory_descriptor_list.h"


/**
 * @struct SCI_BASE_MEMORY_DESCRIPTOR_LIST
 *
 * @brief This structure contains all of the fields necessary to implement
 *        a simple stack for managing the list of available controller indices.
 */
typedef struct SCI_BASE_MEMORY_DESCRIPTOR_LIST
{
   /**
    * This field indicates the length of the memory descriptor entry array.
    */
   U32  length;

   /**
    * This field is utilized to provide iterator pattern functionality.
    * It indicates the index of the next memory descriptor in the iteration.
    */
   U32  next_index;

   /**
    * This field will point to the list of memory descriptors.
    */
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T * mde_array;

   /**
    * This field simply allows a user to chain memory descriptor lists
    * together if desired.  This field will be initialized to
    * SCI_INVALID_HANDLE.
    */
   SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T  next_mdl;

} SCI_BASE_MEMORY_DESCRIPTOR_LIST_T;

/**
 * @brief This method is invoked to construct an memory descriptor list.
 *        It initializes the fields of the MDL.
 *
 * @param[in]  mdl This parameter specifies the memory descriptor list
 *             to be constructed.
 * @param[in]  mde_array This parameter specifies the array of memory
 *             descriptor entries to be managed by this list.
 * @param[in]  mde_array_length This parameter specifies the size of the
 *             array of entries.
 * @param[in]  next_mdl This parameter specifies a subsequent MDL object
 *             to be managed by this MDL object.
 *
 * @return none.
 */
void sci_base_mdl_construct(
   SCI_BASE_MEMORY_DESCRIPTOR_LIST_T * mdl,
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T  * mde_array,
   U32                                 mde_array_length,
   SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T next_mdl
);

/**
 * This macro constructs an memory descriptor entry with the given
 * alignment and size
 */
#define sci_base_mde_construct(mde, alignment, size, attributes) \
{ \
   (mde)->constant_memory_alignment  = (alignment); \
   (mde)->constant_memory_size       = (size); \
   (mde)->constant_memory_attributes = (attributes); \
}

/**
 * @brief This method validates that the memory descriptor is correctly
 *        filled out by the SCI User
 *
 * @param[in] mde This parameter is the mde entry to validate
 * @param[in] alignment This parameter specifies the expected alignment of
 *            the memory for the mde.
 * @param[in] size This parameter specifies the memory size expected for
 *            the mde its value should not have been changed by the SCI
 *            User.
 * @param[in] attributes This parameter specifies the attributes for the
 *            memory descriptor provided.
 *
 * @return BOOL This method returns an indication as to whether the
 *              supplied MDE is valid or not.
 * @retval TRUE The MDE is valid.
 * @retval FALSE The MDE is not valid.
 */
BOOL sci_base_mde_is_valid(
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T *mde,
   U32                               alignment,
   U32                               size,
   U16                               attributes
);

#endif // _SCI_BASE_MEMORY_DESCRIPTOR_LIST_H_

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
 * @brief This file contains all of the method implementations for the
 *        SCIF_SAS_LIBRARY object.
 */


#include "scic_library.h"
#include "sci_pool.h"

#include "scif_sas_library.h"
#include "scif_sas_logger.h"
#include "scif_sas_controller.h"


/**
 * This macro simply calculates the size of the framework library.  This
 * includes the memory for each controller object.
 */
#define SCIF_LIBRARY_SIZE(max_controllers)                             \
(                                                                      \
   sizeof(SCIF_SAS_LIBRARY_T) +                                        \
   (sizeof(SCIF_SAS_CONTROLLER_T) * (max_controllers))                 \
)


//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************


PLACEMENT_HINTS((INITIALIZATION))
U32 scif_library_get_object_size(
   U8 max_controller_count
)
{
   return ( SCIF_LIBRARY_SIZE(max_controller_count) +
            scic_library_get_object_size(max_controller_count) );
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
SCI_LIBRARY_HANDLE_T scif_library_construct(
   void * library_memory,
   U8     max_controller_count
)
{
   SCI_STATUS status;
   SCIF_SAS_LIBRARY_T * fw_library = (SCIF_SAS_LIBRARY_T *) library_memory;

   // Just clear out the memory of the structure to be safe.
   memset(fw_library, 0, scif_library_get_object_size(max_controller_count));

   // Invoke the parent object constructor.
   SCI_BASE_LIBRARY_CONSTRUCT(fw_library,
                              &fw_library->parent,
                              max_controller_count,
                              struct SCIF_SAS_CONTROLLER,
                              status);

   // The memory for the framework controller objects start immediately
   // after the library object.
   fw_library->controllers = (SCIF_SAS_CONTROLLER_T*)
                             ((U8*)library_memory + sizeof(SCIF_SAS_LIBRARY_T));

   // Construct the core library.
   fw_library->core_object = scic_library_construct(
                                (U8 *)library_memory +
                                SCIF_LIBRARY_SIZE(max_controller_count),
                                max_controller_count
                             );

   // Ensure construction completed successfully for the core.
   if (fw_library->core_object != SCI_INVALID_HANDLE)
   {
      // Set the association in the core library to this framework library.
      sci_object_set_association(
         (SCI_OBJECT_HANDLE_T) fw_library->core_object,
         (void *) fw_library
      );

      return fw_library;
   }

   return SCI_INVALID_HANDLE;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
SCI_STATUS scif_library_allocate_controller(
   SCI_LIBRARY_HANDLE_T      library,
   SCI_CONTROLLER_HANDLE_T * new_controller
)
{
   SCI_STATUS  status;

   // Ensure the user supplied a valid library handle.
   if (library != SCI_INVALID_HANDLE)
   {
      SCIF_SAS_LIBRARY_T * fw_library = (SCIF_SAS_LIBRARY_T *) library;

      // Allocate the framework library.
      SCI_BASE_LIBRARY_ALLOCATE_CONTROLLER(fw_library, new_controller, &status);
      if (status == SCI_SUCCESS)
      {
         SCIF_SAS_CONTROLLER_T * fw_controller;

         // Allocate the core controller and save the handle in the framework
         // controller object.
         fw_controller = (SCIF_SAS_CONTROLLER_T*) *new_controller;

         // Just clear out the memory of the structure to be safe.
         memset(fw_controller, 0, sizeof(SCIF_SAS_CONTROLLER_T));

         status = scic_library_allocate_controller(
                     fw_library->core_object, &(fw_controller->core_object)
                  );

         // Free the framework controller if the core controller allocation
         // failed.
         if (status != SCI_SUCCESS)
            scif_library_free_controller(library, fw_controller);
      }

      if (status != SCI_SUCCESS)
      {
         SCIF_LOG_WARNING((
            sci_base_object_get_logger(fw_library),
            SCIF_LOG_OBJECT_LIBRARY,
            "Library:0x%p Status:0x%x controller allocation failed\n",
            fw_library, status
         ));
      }
   }
   else
      status = SCI_FAILURE_INVALID_PARAMETER_VALUE;

   return status;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((SHUTDOWN))
SCI_STATUS scif_library_free_controller(
   SCI_LIBRARY_HANDLE_T     library,
   SCI_CONTROLLER_HANDLE_T  controller
)
{
   SCI_STATUS  status;

   if ( (library != SCI_INVALID_HANDLE) && (controller != SCI_INVALID_HANDLE) )
   {
      SCI_STATUS              core_status;
      SCIF_SAS_LIBRARY_T    * fw_library    = (SCIF_SAS_LIBRARY_T*) library;
      SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

      core_status = scic_library_free_controller(
                       fw_library->core_object, fw_controller->core_object
                    );

      scif_sas_controller_destruct(fw_controller);

      SCI_BASE_LIBRARY_FREE_CONTROLLER(
         (SCIF_SAS_LIBRARY_T *) library,
         controller,
         SCIF_SAS_CONTROLLER_T,
         &status
      );

      if ( (status == SCI_SUCCESS) && (core_status != SCI_SUCCESS) )
         status = core_status;

      if (status != SCI_SUCCESS)
      {
         SCIF_LOG_WARNING((
            sci_base_object_get_logger(fw_library),
            SCIF_LOG_OBJECT_LIBRARY,
            "Library:0x%p Controller:0x%p Status:0x%x free controller failed\n",
            fw_library, fw_controller, status
         ));
      }
   }
   else
      status = SCI_FAILURE_INVALID_PARAMETER_VALUE;

   return status;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
SCI_LIBRARY_HANDLE_T scif_library_get_scic_handle(
   SCI_LIBRARY_HANDLE_T   scif_library
)
{
   SCIF_SAS_LIBRARY_T * fw_library = (SCIF_SAS_LIBRARY_T*) scif_library;

   return fw_library->core_object;
}

// ---------------------------------------------------------------------------

#define SCIF_SAS_LIBRARY_MAX_TIMERS 32

PLACEMENT_HINTS((INITIALIZATION))
U16 scif_library_get_max_timer_count(
   void
)
{
   /// @todo Need to calculate the exact maximum number of timers needed.
   return SCIF_SAS_LIBRARY_MAX_TIMERS + scic_library_get_max_timer_count();
}

//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************



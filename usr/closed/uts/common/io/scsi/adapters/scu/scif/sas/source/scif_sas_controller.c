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
 * @brief This file contains the implementation of the SCIF_SAS_CONTROLLER
 *        object.
 */


#include "sci_status.h"
#include "sci_controller.h"
#include "scic_controller.h"
#include "scif_user_callback.h"

#include "scif_sas_controller.h"
#include "scif_sas_library.h"
#include "scif_sas_logger.h"


//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

PLACEMENT_HINTS((INITIALIZATION))
SCI_STATUS scif_controller_construct(
   SCI_LIBRARY_HANDLE_T      library,
   SCI_CONTROLLER_HANDLE_T   controller
)
{
   SCI_STATUS              status        = SCI_SUCCESS;
   SCIF_SAS_LIBRARY_T    * fw_library    = (SCIF_SAS_LIBRARY_T*) library;
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(library),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_INITIALIZATION,
      "scif_controller_construct(0x%p, 0x%p) enter\n",
      library, controller
   ));

   // Validate the user supplied parameters.
   if ((library == SCI_INVALID_HANDLE) || (controller == SCI_INVALID_HANDLE))
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;

   // Construct the base controller.  As part of constructing the base
   // controller we ask it to also manage the MDL iteration for the Core.
   sci_base_controller_construct(
      &fw_controller->parent,
      sci_base_object_get_logger(fw_library),
      scif_sas_controller_state_table,
      fw_controller->mdes,
      SCIF_SAS_MAX_MEMORY_DESCRIPTORS,
      sci_controller_get_memory_descriptor_list_handle(fw_controller->core_object)
   );

   scif_sas_controller_initialize_state_logging(fw_controller);

   status = scic_controller_construct(
               fw_library->core_object, fw_controller->core_object
            );

   // If the core controller was successfully constructed, then
   // finish construction of the framework controller.
   if (status == SCI_SUCCESS)
   {
      // Set the association in the core controller to this framework
      // controller.
      sci_object_set_association(
         (SCI_OBJECT_HANDLE_T) fw_controller->core_object, fw_controller
      );

      sci_base_state_machine_change_state(
         &fw_controller->parent.state_machine,
         SCI_BASE_CONTROLLER_STATE_RESET
      );
   }

   return status;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
SCI_STATUS scif_controller_initialize(
   SCI_CONTROLLER_HANDLE_T   controller
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_INITIALIZATION,
      "scif_controller_initialize(0x%p) enter\n",
      controller
   ));

   // Validate the user supplied parameters.
   if (controller == SCI_INVALID_HANDLE)
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;

   return fw_controller->state_handlers->initialize_handler(
             &fw_controller->parent
          );
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
U32 scif_controller_get_suggested_start_timeout(
   SCI_CONTROLLER_HANDLE_T  controller
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   // Validate the user supplied parameters.
   if (controller == SCI_INVALID_HANDLE)
      return 0;

   // Currently we aren't adding any additional time into the suggested
   // timeout value for the start operation.  Simply utilize the core
   // value.
   return scic_controller_get_suggested_start_timeout(fw_controller->core_object);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
SCI_STATUS scif_controller_start(
   SCI_CONTROLLER_HANDLE_T  controller,
   U32                      timeout
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_INITIALIZATION,
      "scif_controller_start(0x%p, 0x%x) enter\n",
      controller, timeout
   ));

   // Validate the user supplied parameters.
   if (controller == SCI_INVALID_HANDLE)
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;

   return fw_controller->state_handlers->
          start_handler(&fw_controller->parent, timeout);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((SHUTDOWN))
SCI_STATUS scif_controller_stop(
   SCI_CONTROLLER_HANDLE_T  controller,
   U32                      timeout
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_SHUTDOWN,
      "scif_controller_stop(0x%p, 0x%x) enter\n",
      controller, timeout
   ));

   // Validate the user supplied parameters.
   if (controller == SCI_INVALID_HANDLE)
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;

   return fw_controller->state_handlers->
          stop_handler(&fw_controller->parent, timeout);

}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((SHUTDOWN))
SCI_STATUS scif_controller_reset(
   SCI_CONTROLLER_HANDLE_T  controller
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_CONTROLLER_RESET,
      "scif_controller_reset(0x%p) enter\n",
      controller
   ));

   // Validate the user supplied parameters.
   if (controller == SCI_INVALID_HANDLE)
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;

   return fw_controller->state_handlers->
          reset_handler(&fw_controller->parent);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
SCI_CONTROLLER_HANDLE_T scif_controller_get_scic_handle(
   SCI_CONTROLLER_HANDLE_T   controller
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   return fw_controller->core_object;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_IO_STATUS scif_controller_start_io(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     io_request,
   U16                         io_tag
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_controller_start_io(0x%p, 0x%p, 0x%p, 0x%x) enter\n",
      controller, remote_device, io_request, io_tag
   ));

   if (
         sci_pool_empty(fw_controller->hprq.pool)
      || scif_sas_controller_sufficient_resource(controller)
      )
   {
      return fw_controller->state_handlers->start_io_handler(
                (SCI_BASE_CONTROLLER_T*) controller,
                (SCI_BASE_REMOTE_DEVICE_T*) remote_device,
                (SCI_BASE_REQUEST_T*) io_request,
                io_tag
             );
   }
   else
      return SCI_FAILURE_INSUFFICIENT_RESOURCES;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_TASK_STATUS scif_controller_start_task(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_TASK_REQUEST_HANDLE_T   task_request,
   U16                         io_tag
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_controller_start_task(0x%p, 0x%p, 0x%p, 0x%x) enter\n",
      controller, remote_device, task_request, io_tag
   ));

   // Validate the user supplied parameters.
   if (  (controller == SCI_INVALID_HANDLE)
      || (remote_device == SCI_INVALID_HANDLE)
      || (task_request == SCI_INVALID_HANDLE) )
   {
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;
   }

   if (scif_sas_controller_sufficient_resource(controller))
   {
      return fw_controller->state_handlers->start_task_handler(
             (SCI_BASE_CONTROLLER_T*) controller,
             (SCI_BASE_REMOTE_DEVICE_T*) remote_device,
             (SCI_BASE_REQUEST_T*) task_request,
             io_tag
          );
   }
   else
      return SCI_FAILURE_INSUFFICIENT_RESOURCES;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_STATUS scif_controller_complete_io(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     io_request
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_controller_complete_io(0x%p, 0x%p, 0x%p) enter\n",
      controller, remote_device, io_request
   ));

   return fw_controller->state_handlers->complete_io_handler(
             (SCI_BASE_CONTROLLER_T*) controller,
             (SCI_BASE_REMOTE_DEVICE_T*) remote_device,
             (SCI_BASE_REQUEST_T*) io_request
          );
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_STATUS scif_controller_complete_task(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_TASK_REQUEST_HANDLE_T   task_request
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_controller_complete_task(0x%p, 0x%p, 0x%p) enter\n",
      controller, remote_device, task_request
   ));

   // Validate the user supplied parameters.
   if (  (controller == SCI_INVALID_HANDLE)
      || (remote_device == SCI_INVALID_HANDLE)
      || (task_request == SCI_INVALID_HANDLE) )
   {
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;
   }

   return fw_controller->state_handlers->complete_task_handler(
             (SCI_BASE_CONTROLLER_T*) controller,
             (SCI_BASE_REMOTE_DEVICE_T*) remote_device,
             (SCI_BASE_REQUEST_T*) task_request
          );
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
SCI_STATUS scif_controller_get_domain_handle(
   SCI_CONTROLLER_HANDLE_T   controller,
   U8                        port_index,
   SCI_DOMAIN_HANDLE_T     * domain_handle
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   // Validate the user supplied parameters.
   if (controller == SCI_INVALID_HANDLE)
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;

   // Retrieve the domain handle if the supplied index is legitimate.
   if (port_index < SCI_MAX_PORTS)
   {
      *domain_handle = &fw_controller->domains[port_index];
      return SCI_SUCCESS;
   }

   return SCI_FAILURE_INVALID_PORT;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
SCI_STATUS scif_controller_set_mode(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCI_CONTROLLER_MODE       mode
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   // Currently, the framework doesn't change any configurations for
   // speed or size modes.  Default to speed mode basically.
   return scic_controller_set_mode(fw_controller->core_object, mode);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
U32 scif_controller_get_sat_compliance_version(
   void
)
{
   /// @todo Fix return of SAT compliance version.
   return 0;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
U32 scif_controller_get_sat_compliance_version_revision(
   void
)
{
   /// @todo Fix return of SAT compliance revision.
   return 0;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
SCI_STATUS scif_user_parameters_set(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCIF_USER_PARAMETERS_T  * scif_parms
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   //validate all the registry entries before overwriting the default parameter 
   //values.
   if (scif_parms->sas.is_sata_ncq_enabled != 1 && scif_parms->sas.is_sata_ncq_enabled != 0)
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;   

   if (scif_parms->sas.max_ncq_depth < 1 && scif_parms->sas.max_ncq_depth > 32)
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;

   if (scif_parms->sas.is_sata_standby_timer_enabled != 1 
       && scif_parms->sas.is_sata_standby_timer_enabled != 0)
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;

   if (scif_parms->sas.is_non_zero_buffer_offsets_enabled != 1
       && scif_parms->sas.is_non_zero_buffer_offsets_enabled != 0)
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;

   if (scif_parms->sas.reset_type != SCI_SAS_ABORT_TASK
       && scif_parms->sas.reset_type != SCI_SAS_ABORT_TASK_SET
       && scif_parms->sas.reset_type != SCI_SAS_CLEAR_TASK_SET
       && scif_parms->sas.reset_type != SCI_SAS_LOGICAL_UNIT_RESET
       && scif_parms->sas.reset_type != SCI_SAS_I_T_NEXUS_RESET
       && scif_parms->sas.reset_type != SCI_SAS_CLEAR_ACA
       && scif_parms->sas.reset_type != SCI_SAS_QUERY_TASK
       && scif_parms->sas.reset_type != SCI_SAS_QUERY_TASK_SET
       && scif_parms->sas.reset_type != SCI_SAS_QUERY_ASYNCHRONOUS_EVENT
       && scif_parms->sas.reset_type != SCI_SAS_HARD_RESET)
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;
       
   if (scif_parms->sas.clear_affiliation_during_controller_stop != 1
       && scif_parms->sas.clear_affiliation_during_controller_stop !=0)
       return SCI_FAILURE_INVALID_PARAMETER_VALUE;
   
   memcpy((&fw_controller->user_parameters), scif_parms, sizeof(*scif_parms));

   // In the future more could be done to prevent setting parameters at the
   // wrong time, but for now we'll simply set the values even if it is too
   // late for them to take affect.
   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

#if !defined(DISABLE_INTERRUPTS)

/**
 * @brief This routine check each domain of the controller to see if
 *           any domain is overriding interrupt coalescence.
 *
 * @param[in] fw_controller frame controller
 * @param[in] fw_smp_phy The smp phy to be freed.
 *
 * @return none
 */
PLACEMENT_HINTS((INTERRUPTS))
BOOL scif_sas_controller_is_overriding_interrupt_coalescence(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   U8 index;

   for(index = 0; index < SCI_MAX_DOMAINS; index++)
   {
      if(fw_controller->domains[index].parent.state_machine.current_state_id == 
            SCI_BASE_DOMAIN_STATE_DISCOVERING)
         return TRUE;
   }
 
   return FALSE;
}

PLACEMENT_HINTS((INTERRUPTS))
SCI_STATUS scif_controller_set_interrupt_coalescence(
   SCI_CONTROLLER_HANDLE_T controller, 
   U32                     coalesce_number, 
   U32                     coalesce_timeout
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T * )controller;
   
   ///when framework is in the middle of temporarily overriding the interrupt 
   ///coalescence values, user's request of setting interrupt coalescence
   ///will be saved. As soon as the framework done the temporary overriding,
   ///it will serve user's request to set new interrupt coalescence.
   if (scif_sas_controller_is_overriding_interrupt_coalescence(fw_controller))
   {
      U32 curr_coalesce_number;
      U32 curr_coalesce_timeout;
      SCI_STATUS core_status;

      // save current interrupt coalescence info.
      scic_controller_get_interrupt_coalescence (
         fw_controller->core_object, &curr_coalesce_number, &curr_coalesce_timeout);

      //try user's request out in the core, but immediately restore core's
      //current setting. 
      core_status = scic_controller_set_interrupt_coalescence(
                       fw_controller->core_object, coalesce_number, coalesce_timeout);

      if ( core_status == SCI_SUCCESS )
      {
         fw_controller->saved_interrupt_coalesce_number = (U16)coalesce_number;
         fw_controller->saved_interrupt_coalesce_timeout = coalesce_timeout;
      }

       //restore current interrupt coalescence.
      scic_controller_set_interrupt_coalescence(
         fw_controller->core_object, curr_coalesce_number, curr_coalesce_timeout);
      
      return core_status;
   }
   else
   {
      ///If framework is not internally overriding the interrupt coalescence,
      ///serve user's request immediately by passing the reqeust to core.
      return scic_controller_set_interrupt_coalescence(
                fw_controller->core_object, coalesce_number, coalesce_timeout);
   }
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INTERRUPTS))
void scif_controller_get_interrupt_coalescence(
   SCI_CONTROLLER_HANDLE_T controller, 
   U32                   * coalesce_number, 
   U32                   * coalesce_timeout
)
{
   SCIF_SAS_CONTROLLER_T * scif_controller = (SCIF_SAS_CONTROLLER_T * )controller;
      
   scic_controller_get_interrupt_coalescence(
      scif_controller->core_object, coalesce_number, coalesce_timeout);
}

/**
 * @brief This method will save the interrupt coalescence values.  If
 *        the interrupt coalescence values have already been saved,
 *        then this method performs no operations.
 *
 * @param[in,out] fw_controller This parameter specifies the controller
 *                for which to save the interrupt coalescence values.
 *
 * @return none
 */
PLACEMENT_HINTS((INTERRUPTS))
void scif_sas_controller_save_interrupt_coalescence(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   if ( !scif_sas_controller_is_overriding_interrupt_coalescence(fw_controller))
   {
      // Override core's interrupt coalescing settings during SMP
      // DISCOVER process cause' there is only 1 outstanding SMP
      // request per domain is allowed. 
      scic_controller_get_interrupt_coalescence(
         fw_controller->core_object,
         (U32*)&(fw_controller->saved_interrupt_coalesce_number),
         &(fw_controller->saved_interrupt_coalesce_timeout)
      );

      // Temporarily disable the interrupt coalescing.
      scic_controller_set_interrupt_coalescence(fw_controller->core_object,0,0);
   }
}

/**
 * @brief This method will restore the interrupt coalescence values.  If
 *        the interrupt coalescence values have not already been saved,
 *        then this method performs no operations.
 *
 * @param[in,out] fw_controller This parameter specifies the controller
 *                for which to restore the interrupt coalescence values.
 *
 * @return none
 */
PLACEMENT_HINTS((INTERRUPTS))
void scif_sas_controller_restore_interrupt_coalescence(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   if ( !scif_sas_controller_is_overriding_interrupt_coalescence(fw_controller))
      scic_controller_set_interrupt_coalescence(
         fw_controller->core_object, 
         fw_controller->saved_interrupt_coalesce_number,
         fw_controller->saved_interrupt_coalesce_timeout
      );
}

#endif // !defined(DISABLE_INTERRUPTS)

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
void scic_cb_controller_start_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_STATUS               completion_status
)
{
   SCIF_SAS_CONTROLLER_T *fw_controller = (SCIF_SAS_CONTROLLER_T*)
                                         sci_object_get_association(controller);

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_INITIALIZATION,
      "scic_cb_controller_start_complete(0x%p, 0x%x) enter\n",
      controller, completion_status
   ));

   if (completion_status == SCI_SUCCESS)
   {
      // Initialization of the core failed, thus we should transition
      // to the failed state.
      sci_base_state_machine_change_state(
         &fw_controller->parent.state_machine,
         SCI_BASE_CONTROLLER_STATE_READY
      );
   }

   scif_cb_controller_start_complete(fw_controller, completion_status);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((SHUTDOWN))
void scic_cb_controller_stop_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_STATUS               completion_status
)
{
   SCIF_SAS_CONTROLLER_T *fw_controller = (SCIF_SAS_CONTROLLER_T*)
                                         sci_object_get_association(controller);   

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_SHUTDOWN,
      "scic_cb_controller_stop_complete(0x%p, 0x%x) enter\n",
      controller, completion_status
   ));

   if (completion_status == SCI_SUCCESS)
   {
      sci_base_state_machine_change_state(
         &fw_controller->parent.state_machine,
         SCI_BASE_CONTROLLER_STATE_STOPPED
      );
   }
   else
   {
      sci_base_state_machine_change_state(
         &fw_controller->parent.state_machine,
         SCI_BASE_CONTROLLER_STATE_FAILED
      );
   }

   scif_cb_controller_stop_complete(fw_controller, completion_status);
}


// ---------------------------------------------------------------------------

PLACEMENT_HINTS((SHUTDOWN))
void scic_cb_controller_error(
   SCI_CONTROLLER_HANDLE_T  controller
)
{
   SCIF_SAS_CONTROLLER_T *fw_controller = (SCIF_SAS_CONTROLLER_T*)
                                         sci_object_get_association(controller);   

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_SHUTDOWN,
      "scic_cb_controller_not_ready(0x%p) enter\n",
      controller
   ));

   sci_base_state_machine_change_state(
      &fw_controller->parent.state_machine,
      SCI_BASE_CONTROLLER_STATE_FAILED
   );
}

//******************************************************************************
//* P R O T E C T E D    M E T H O D S
//******************************************************************************

/**
 * @brief This method is utilized to continue an internal IO operation
 *        on the controller.  This method is utilized for SAT translated
 *        requests that generate multiple ATA commands in order to fulfill
 *        the original SCSI request.
 *
 * @param[in]  controller This parameter specifies the controller on which
 *             to continue an internal IO request.
 * @param[in]  remote_device This parameter specifies the remote device
 *             on which to continue an internal IO request.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             continue.
 *
 * @return Indicate if the continue operation was successful.
 * @retval SCI_SUCCESS This value is returned if the operation succeeded.
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scif_sas_controller_continue_io(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     io_request
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T*) controller;

   return fw_controller->state_handlers->continue_io_handler(
             (SCI_BASE_CONTROLLER_T*) controller,
             (SCI_BASE_REMOTE_DEVICE_T*) remote_device,
             (SCI_BASE_REQUEST_T*) io_request
          );
}

/**
 * @brief This method will attempt to destruct a framework controller.
 *        This includes free any resources retreived from the user (e.g.
 *        timers).
 *
 * @param[in]  fw_controller This parameter specifies the framework
 *             controller to destructed.
 *
 * @return none
 */
PLACEMENT_HINTS((SHUTDOWN))
void scif_sas_controller_destruct(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_SHUTDOWN,
      "scif_sas_controller_destruct(0x%p) enter\n",
      fw_controller
   ));
}

//-----------------------------------------------------------------------------
// INTERNAL REQUEST RELATED METHODS
//-----------------------------------------------------------------------------

/**
 * @brief This routine is to allocate the memory for creating a new internal
 *        request.
 *
 * @param[in] scif_controller handle to frame controller
 *
 * @return void* address to internal request memory
 */
PLACEMENT_HINTS((TBD))
void * scif_sas_controller_allocate_internal_request(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   POINTER_UINT internal_io_address;

   if( !sci_pool_empty(fw_controller->internal_request_memory_pool) )
   {
      sci_pool_get(
         fw_controller->internal_request_memory_pool, internal_io_address
      );

      //clean the memory.
      memset((char*)internal_io_address, 0, scif_sas_internal_request_get_object_size());

      return (void *) internal_io_address;
   }
   else
      return NULL;
}

/**
 * @brief This routine is to free the memory for a completed internal request.
 *
 * @param[in] scif_controller handle to frame controller
 * @param[in] fw_internal_io The internal IO to be freed.
 *
 * @return none
 */
PLACEMENT_HINTS((TBD))
void scif_sas_controller_free_internal_request(
   SCIF_SAS_CONTROLLER_T * fw_controller,
   void                  * fw_internal_request_buffer
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_controller_free_internal_request(0x%p, 0x%p) enter\n",
      fw_controller, fw_internal_request_buffer
   ));

   //return the memory to to pool.
   if( !sci_pool_full(fw_controller->internal_request_memory_pool) )
   {
      sci_pool_put(
         fw_controller->internal_request_memory_pool,
         (POINTER_UINT) fw_internal_request_buffer
      );
   }
}


/**
 * @brief this routine is called by OS' DPC to start io requests from internal
 *        high priority request queue
 * @param[in] fw_controller The framework controller.
 *
 * @return none
 */
PLACEMENT_HINTS((TBD))
void scif_sas_controller_start_high_priority_io(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   POINTER_UINT            io_address;
   SCIF_SAS_IO_REQUEST_T * fw_io;
   SCI_STATUS              status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_controller_start_high_priority_io(0x%p) enter\n",
      fw_controller
   ));

   while ( !sci_pool_empty(fw_controller->hprq.pool) )
   {
      sci_pool_get(fw_controller->hprq.pool, io_address);

      fw_io = (SCIF_SAS_IO_REQUEST_T *)io_address;

      status = fw_controller->state_handlers->start_high_priority_io_handler(
         (SCI_BASE_CONTROLLER_T*) fw_controller,
         (SCI_BASE_REMOTE_DEVICE_T*) fw_io->parent.device,
         (SCI_BASE_REQUEST_T*) fw_io,
         SCI_CONTROLLER_INVALID_IO_TAG
      );
   }
}

/**
 * @brief This method will check how many outstanding IOs currently and number
 * of IOs in high priority queue, if the overall number exceeds the max_tc,
 * return FALSE.
 *
 * @param[in] fw_controller The framework controller.
 *
 * @return BOOL Indicate whether there is sufficient resource to start an IO.
 * @retvalue TRUE The controller has sufficient resource.
 * @retvalue FALSE There is not sufficient resource available.
 */
PLACEMENT_HINTS((TBD))
BOOL scif_sas_controller_sufficient_resource(
   SCIF_SAS_CONTROLLER_T *fw_controller
)
{
   SCIF_SAS_DOMAIN_T * fw_domain;
   U32 domain_index;
   U32 outstanding_io_count = 0;
   U32 high_priority_io_count = 0;

   for(domain_index = 0; domain_index < SCI_MAX_DOMAINS; domain_index++)
   {
      fw_domain = &fw_controller->domains[domain_index];
      outstanding_io_count += fw_domain->request_list.element_count;
   }

   high_priority_io_count = sci_pool_count(fw_controller->hprq.pool);

   if ( (outstanding_io_count + high_priority_io_count) > SCI_MAX_IO_REQUESTS )
      return FALSE;

   return TRUE;
}


/**
 * @brief This method is the starting point to complete high prority io for a
 *        controller then down to domain, device.
 *
 * @param[in] fw_controller The framework controller
 * @param[in] remote_device  The framework remote device.
 * @param[in] io_request The high priority io request to be completed.
 *
 * @return SCI_STATUS indicate the completion status from framework down to the
 *         core.
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scif_sas_controller_complete_high_priority_io(
   SCIF_SAS_CONTROLLER_T    *fw_controller,
   SCIF_SAS_REMOTE_DEVICE_T *remote_device,
   SCIF_SAS_REQUEST_T       *io_request
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_controller_complete_high_priority_io(0x%p, 0x%p, 0x%p) enter\n",
      fw_controller, remote_device, io_request
   ));

   //call controller's new added complete_high_priority_io_handler
   return fw_controller->state_handlers->complete_high_priority_io_handler(
             (SCI_BASE_CONTROLLER_T*) fw_controller,
             (SCI_BASE_REMOTE_DEVICE_T*) remote_device,
             (SCI_BASE_REQUEST_T*) io_request
          );
}

/**

 * @brief This routine is to allocate the memory for creating a smp phy object.
 *
 * @param[in] scif_controller handle to frame controller
 *
 * @return SCIF_SAS_SMP_PHY_T * An allocated space for smp phy. If failed to allocate,
 *            return NULL.
 */
PLACEMENT_HINTS((TBD))
SCIF_SAS_SMP_PHY_T * scif_sas_controller_allocate_smp_phy(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   SCIF_SAS_SMP_PHY_T * smp_phy;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "scif_controller_allocate_smp_phy(0x%p) enter\n",
      fw_controller
   ));

   if( !sci_fast_list_is_empty(&fw_controller->smp_phy_memory_list) )
   {
      smp_phy = (SCIF_SAS_SMP_PHY_T *)
         sci_fast_list_remove_head(&fw_controller->smp_phy_memory_list);

      //clean the memory.
      memset((char*)smp_phy, 
             0, 
             sizeof(SCIF_SAS_SMP_PHY_T)
            );

      return smp_phy;
   }
   else
      return NULL;
}

/**
 * @brief This routine is to free the memory for a released smp phy.
 *
 * @param[in] fw_controller The framework controller, a smp phy is released 
 *                to its memory.
 * @param[in] fw_smp_phy The smp phy to be freed.
 *
 * @return none
 */
PLACEMENT_HINTS((TBD))
void scif_sas_controller_free_smp_phy(
   SCIF_SAS_CONTROLLER_T * fw_controller,
   SCIF_SAS_SMP_PHY_T    * smp_phy
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "scif_controller_free_smp_phy(0x%p, 0x%p) enter\n",
      fw_controller, smp_phy
   ));

   //return the memory to the list.
   sci_fast_list_insert_tail(
      &fw_controller->smp_phy_memory_list, 
      &smp_phy->list_element
   );  
}


/**
 * @brief This method clear affiliation for all the EA SATA devices associated 
 *        to this controller.
 *
 * @param[in] fw_controller This parameter specifies the framework
 *            controller object for whose remote devices are to be stopped.
 *
 * @return This method returns a value indicating if the operation completed.
 * @retval SCI_COMPLETE This value indicates that all the EA SATA devices' 
 *         affiliation was cleared.
 * @retval SCI_INCOMPLETE This value indicates clear affiliation activity is 
 *         yet to be completed. 
 */
PLACEMENT_HINTS((SHUT_DOWN))
SCI_STATUS scif_sas_controller_clear_affiliation(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   U8 index;
   SCI_STATUS status;
   SCIF_SAS_DOMAIN_T * fw_domain;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "scif_sas_controller_clear_affiliation(0x%p) enter\n",
      fw_controller
   ));
  
   index = fw_controller->current_domain_to_clear_affiliation;
   
   if (index < SCI_MAX_DOMAINS)
   {  
      fw_domain = &fw_controller->domains[index];

      //Need to stop all the on-going smp activities before clearing affiliation.
      scif_sas_domain_cancel_smp_activities(fw_domain);

      scif_sas_domain_start_clear_affiliation(fw_domain);

      status = SCI_WARNING_SEQUENCE_INCOMPLETE;
   }
   else
   {  //the controller has done clear affiliation work to all its domains.
      scif_sas_controller_continue_to_stop(fw_controller);
      status = SCI_SUCCESS;
   }

   return status;
}


/**
 * @brief This method sets SCIF user parameters to
 *        default values.  Users can override these values utilizing
 *        the sciF_user_parameters_set() methods.
 *
 * @param[in] controller This parameter specifies the controller for
 *            which to set the configuration parameters to their
 *            default values.
 *
 * @return none
 */
PLACEMENT_HINTS((INITIALIZATION))
void scif_sas_controller_set_default_config_parameters(
   SCIF_SAS_CONTROLLER_T * this_controller
)
{
   SCIF_USER_PARAMETERS_T * scif_parms = &(this_controller->user_parameters);

   scif_parms->sas.is_sata_ncq_enabled = TRUE;
   scif_parms->sas.max_ncq_depth = 32;
   scif_parms->sas.is_sata_standby_timer_enabled = FALSE;
   scif_parms->sas.is_non_zero_buffer_offsets_enabled = FALSE;
   scif_parms->sas.reset_type = SCI_SAS_LOGICAL_UNIT_RESET;
   scif_parms->sas.clear_affiliation_during_controller_stop = TRUE;
}


/**
 * @brief This method releases resource for framework controller and associated
 *        objects.
 *
 * @param[in] fw_controller This parameter specifies the framework
 *            controller and associated objects whose resources are to be released.
 *
 * @return This method returns a value indicating if the operation succeeded.
 * @retval SCI_SUCCESS This value indicates that resource release succeeded.
 * @retval SCI_FAILURE This value indicates certain failure during the process
 *            of resource release.
 */
SCI_STATUS scif_sas_controller_release_resource(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   U8 index;
   SCIF_SAS_DOMAIN_T * fw_domain;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "scif_sas_controller_release_resource(0x%p) enter\n",
      fw_controller
   ));

   //currently the only resource to be released is domain's timer.
   for (index = 0; index < SCI_MAX_DOMAINS; index++)
   { 
      fw_domain = &fw_controller->domains[index];

      scif_sas_domain_release_resource(fw_controller, fw_domain);
   }

   return SCI_SUCCESS;
}


#ifdef SCI_LOGGING
/**
 * This method will start state transition logging for the framework
 * controller object.
 *
 * @param[in] fw_controller The framework controller object on which to
 *       observe state changes.
 *
 * @return none
 */
PLACEMENT_HINTS((LOGGING))
void scif_sas_controller_initialize_state_logging(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   sci_base_state_machine_logger_initialize(
      &fw_controller->parent.state_machine_logger,
      &fw_controller->parent.state_machine,
      &fw_controller->parent.parent,
      scif_cb_logger_log_states,
      "SCIF_SAS_CONTROLLER_T", "base state machine",
      SCIF_LOG_OBJECT_CONTROLLER
   );
}

/**
 * This method will remove the logging of state transitions from the framework
 * controller object.
 *
 * @param[in] fw_controller The framework controller to change.
 *
 * @return none
 */
PLACEMENT_HINTS((LOGGING))
void scif_sas_controller_deinitialize_state_logging(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   sci_base_state_machine_logger_deinitialize(
      &fw_controller->parent.state_machine_logger,
      &fw_controller->parent.state_machine
   );
}
#endif // SCI_LOGGING

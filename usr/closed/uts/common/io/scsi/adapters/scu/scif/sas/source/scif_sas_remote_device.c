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
 * @brief This file contains the implementation of the SCIF_SAS_REMOTE_DEVICE
 *        object.
 */


#include "scic_remote_device.h"
#include "scic_port.h"

#include "scif_sas_logger.h"
#include "scif_sas_remote_device.h"
#include "scif_sas_stp_remote_device.h"
#include "scif_sas_domain.h"
#include "scif_sas_controller.h"
#include "sci_controller.h"


//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

PLACEMENT_HINTS((INITIALIZATION))
U32 scif_remote_device_get_object_size(
   void
)
{
   return ( sizeof(SCIF_SAS_REMOTE_DEVICE_T)
          + scic_remote_device_get_object_size() );
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
void scif_remote_device_construct(
   SCI_DOMAIN_HANDLE_T          domain,
   void                       * remote_device_memory,
   SCI_REMOTE_DEVICE_HANDLE_T * new_scif_remote_device_handle
)
{
   SCIF_SAS_DOMAIN_T        * fw_domain = (SCIF_SAS_DOMAIN_T *) domain;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                          remote_device_memory;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "scif_remote_device_construct(0x%p, 0x%p, 0x%p) enter\n",
      domain, remote_device_memory, new_scif_remote_device_handle
   ));

   memset(remote_device_memory, 0, sizeof(SCIF_SAS_REMOTE_DEVICE_T));

   // The user's handle to the remote device evaluates to the memory
   // address where the remote device object is stored.
   *new_scif_remote_device_handle = remote_device_memory;

   fw_device->domain                = fw_domain;
   fw_device->destruct_when_stopped = FALSE;
   //fw_device->parent.is_failed      = FALSE;
   fw_device->operation_status      = SCI_SUCCESS;
   fw_device->request_count         = 0;
   fw_device->task_request_count    = 0;
   fw_device->is_currently_discovered = TRUE;
   fw_device->containing_device       = NULL;
   fw_device->expander_phy_identifier = 0;

   // Construct the base object first in order to ensure logging can
   // function.
   sci_base_remote_device_construct(
      &fw_device->parent,
      sci_base_object_get_logger(fw_domain),
      scif_sas_remote_device_state_table
   );

   sci_base_state_machine_construct(
      &fw_device->starting_substate_machine,
      &fw_device->parent.parent,
      scif_sas_remote_device_starting_substate_table,
      SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_AWAIT_COMPLETE
   );

   sci_base_state_machine_construct(
      &fw_device->ready_substate_machine,
      &fw_device->parent.parent,
      scif_sas_remote_device_ready_substate_table,
      SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_OPERATIONAL
   );

   scif_sas_remote_device_initialize_state_logging(fw_device);

   scic_remote_device_construct(
      fw_domain->core_object,
      ((U8*) remote_device_memory) + sizeof(SCIF_SAS_REMOTE_DEVICE_T),
      &fw_device->core_object
   );

   // Set the association in the core object, so that we are able to
   // determine our framework remote device object from the core remote
   // device.
   sci_object_set_association(fw_device->core_object, fw_device);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
SCI_STATUS scif_remote_device_da_construct(
   SCI_REMOTE_DEVICE_HANDLE_T                   remote_device,
   SCI_SAS_ADDRESS_T                          * sas_address,
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T * protocols
)
{
   SCI_STATUS                 status    = SCI_SUCCESS;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                          remote_device;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "scif_remote_device_da_construct(0x%p, 0x%p, 0x%p) enter\n",
      remote_device, sas_address, protocols
   ));

   // Make sure the device hasn't already been constructed and added
   // to the domain.
   if (scif_domain_get_device_by_sas_address(fw_device->domain, sas_address)
       == SCI_INVALID_HANDLE)
   {
      SCIC_PORT_PROPERTIES_T  properties;

      scic_port_get_properties(fw_device->domain->core_object, &properties);

      // Check to see if this is the direct attached device.
      if (  (sas_address->low == properties.remote.sas_address.low)
         && (sas_address->high == properties.remote.sas_address.high) )
      {
         status = scic_remote_device_da_construct(fw_device->core_object);
      }
      else
         // Don't allow the user to construct a direct attached device
         // if it's not a direct attached device.
         status = SCI_FAILURE_UNSUPPORTED_PROTOCOL;
   }
   else
      status = SCI_FAILURE_DEVICE_EXISTS;

   if (status == SCI_SUCCESS)
   {
      // Add the device to the domain list.
      sci_abstract_list_pushback(
         &fw_device->domain->remote_device_list, fw_device
      );

      // If a SATA/STP device is connected, then construct it.
      if (protocols->u.bits.stp_target)
         scif_sas_stp_remote_device_construct(fw_device);
      else if (protocols->u.bits.smp_target)
         scif_sas_smp_remote_device_construct(fw_device);

      SCIF_LOG_INFO((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Domain:0x%p SasAddress:0x%x,0x%x remote device constructed\n",
         fw_device->domain, sas_address->low, sas_address->high
      ));

      status = fw_device->state_handlers->parent.start_handler(
                  &fw_device->parent
               );
   }
   else
   {
      SCIF_LOG_WARNING((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Domain:0x%p SasAddress:0x%x,0x%x Status:0x%x remote device construct failure\n",
         fw_device->domain, sas_address->low, sas_address->high, status
      ));
   }

   return status;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
SCI_STATUS scif_remote_device_ea_construct(
   SCI_REMOTE_DEVICE_HANDLE_T   remote_device,
   SCI_REMOTE_DEVICE_HANDLE_T   containing_device,
   SMP_RESPONSE_DISCOVER_T    * smp_response
)
{
   SCI_SAS_ADDRESS_T        * sas_address;
   SCI_STATUS                 status        = SCI_SUCCESS;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device     = (SCIF_SAS_REMOTE_DEVICE_T *)
                                              remote_device;
   SCIF_SAS_REMOTE_DEVICE_T * fw_smp_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                              containing_device;

   fw_device->containing_device = fw_smp_device;
   fw_device->expander_phy_identifier = 
      fw_smp_device->protocol_device.smp_device.current_activity_phy_index;

   sas_address = &smp_response->attached_sas_address;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "scif_remote_device_ea_construct(0x%p, 0x%p) enter\n",
      remote_device, smp_response
   ));

   // Make sure the device hasn't already been constructed and added
   // to the domain.
   if (scif_domain_get_device_by_sas_address(fw_device->domain, sas_address)
       == SCI_INVALID_HANDLE)
   {
      //for sata device, we need another routine. likely
      //scif_remote_device_ea_sata_construct.
      status = scic_remote_device_ea_construct(fw_device->core_object, smp_response);
   }
   else
      status = SCI_FAILURE_DEVICE_EXISTS;

   if (status == SCI_SUCCESS)
   {
      // Add the device to the domain list.
      sci_abstract_list_pushback(
         &fw_device->domain->remote_device_list, fw_device
      );

      if (smp_response->protocols.u.bits.attached_smp_target)
         scif_sas_smp_remote_device_construct(fw_device);
      else if (smp_response->protocols.u.bits.attached_stp_target)    
         scif_sas_stp_remote_device_construct(fw_device);   

      SCIF_LOG_INFO((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Domain:0x%p SasAddress:0x%x,0x%x remote device constructed\n",
         fw_device->domain, sas_address->low, sas_address->high
      ));

      //only start the device if the device is not a SATA disk on SPINUP_HOLD state.
      if ( scic_remote_device_get_connection_rate(fw_device->core_object) != 
              SCI_SATA_SPINUP_HOLD )
      {
          status = fw_device->state_handlers->parent.start_handler(
                      &fw_device->parent 
                   );
      }
   }
   else
   {
      SCIF_LOG_WARNING((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Domain:0x%p SasAddress:0x%x,0x%x Status:0x%x remote device construct failure\n",
         fw_device->domain, sas_address->low, sas_address->high, status
      ));
   }

   return status;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
SCI_STATUS scif_remote_device_destruct(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "scif_remote_device_destruct(0x%p) enter\n",
      remote_device
   ));

   //remove the device from domain's remote_device_list
   fw_device->domain->state_handlers->device_destruct_handler(
      &fw_device->domain->parent, &fw_device->parent
   );

   // The destruct process may not complete immediately, since the core
   // remote device likely needs to be stopped first.  However, the user
   // is not given a callback notification for destruction.
   return fw_device->state_handlers->parent.destruct_handler(
             &fw_device->parent
          );
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
SCI_REMOTE_DEVICE_HANDLE_T scif_remote_device_get_scic_handle(
   SCI_REMOTE_DEVICE_HANDLE_T  scif_remote_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          scif_remote_device;

   if ( (fw_device != NULL) && (fw_device->core_object != SCI_INVALID_HANDLE) )
      return fw_device->core_object;

   SCIF_LOG_WARNING((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%p no associated core device found\n",
      fw_device
   ));

   return SCI_INVALID_HANDLE;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
void scic_cb_remote_device_start_complete(
   SCI_CONTROLLER_HANDLE_T    controller,
   SCI_REMOTE_DEVICE_HANDLE_T remote_device,
   SCI_STATUS                 completion_status
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                      sci_object_get_association(remote_device);

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_REMOTE_DEVICE_CONFIG,
      "scic_cb_remote_device_start_complete(0x%p, 0x%p, 0x%x) enter\n",
      controller, remote_device, completion_status
   ));

   fw_device->state_handlers->start_complete_handler(
      fw_device, completion_status
   );
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
void scic_cb_remote_device_stop_complete(
   SCI_CONTROLLER_HANDLE_T    controller,
   SCI_REMOTE_DEVICE_HANDLE_T remote_device,
   SCI_STATUS                 completion_status
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                      sci_object_get_association(remote_device);

   fw_device->state_handlers->stop_complete_handler(
      fw_device, completion_status
   );
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
void scic_cb_remote_device_ready(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                      sci_object_get_association(remote_device);

   fw_device->state_handlers->ready_handler(fw_device);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
void scic_cb_remote_device_not_ready(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   U32                         reason_code
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                      sci_object_get_association(remote_device);

   fw_device->state_handlers->not_ready_handler(fw_device,reason_code);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
U16 scif_remote_device_get_max_queue_depth(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;
   SMP_DISCOVER_RESPONSE_PROTOCOLS_T  protocols;

   scic_remote_device_get_protocols(fw_device->core_object, &protocols);

   // If the target is a SATA/STP target, then determine the queue depth
   // for either NCQ or for UDMA.
   if (protocols.u.bits.attached_stp_target)
   {
      if (fw_device->protocol_device.stp_device.sati_device.capabilities
          & SATI_DEVICE_CAP_NCQ_SUPPORTED_ENABLE)
      {
         return fw_device->protocol_device.stp_device.sati_device.ncq_depth;
      }
      else
      {
         // At the moment, we only allow a single UDMA request to be queued.
         return 1;
      }
   }

   // For SSP devices return a no maximum queue depth supported.
   return SCIF_REMOTE_DEVICE_NO_MAX_QUEUE_DEPTH;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
SCI_STATUS scif_remote_device_get_containing_device(
   SCI_REMOTE_DEVICE_HANDLE_T          remote_device,
   SCI_REMOTE_DEVICE_HANDLE_T        * containing_device
)
{
   SCI_STATUS                 status      = SCI_FAILURE;
   SCIF_SAS_REMOTE_DEVICE_T * this_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                            remote_device;

   if ( (this_device != NULL) && (containing_device != NULL) )
   {
      *containing_device = (SCI_REMOTE_DEVICE_HANDLE_T)(this_device->containing_device);
      if (*containing_device != NULL)
      {
         status = SCI_SUCCESS;
      }
   }
   
   return status;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
U32 scif_remote_device_get_started_io_count(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * this_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                            remote_device;
                                            
   return this_device->request_count - this_device->task_request_count;
}
//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************

/*
void scif_sas_remote_device_failure(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   fw_device->parent.is_failed = TRUE;
   sci_base_state_machine_change_state(
      &fw_device->parent.state_machine, SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
   );
}
*/


/**
 * @brief This method retrieves info from Report Phy Sata response and 
 *        save the additional data for a SATA remote device, if necessary.
 *
 * @param[in] report_phy_sata_response SMP Report Phy Sata response
 * 
 * @return none
 */
PLACEMENT_HINTS((HOT_PLUG))
void scif_sas_remote_device_save_report_phy_sata_information(
   SMP_RESPONSE_REPORT_PHY_SATA_T * report_phy_sata_response   
)
{
   //do nothing currently. Later, if needed, we will search the existed 
   //remote device by stp_sas_address, then save more information for
   //that device off the report_phy_sata_response. This assumes the 
   //stp_sas_address from report_phy_sata response is the same sas address
   //from discover response.
   
   return;  
}

/**
 * @brief This method does target reset for DA or EA remote device.
 * 
 * @param[in] fw_controller, the controller object the target device belongs
 *            to.
 * @param[in] fw_device, the target device to be hard reset.
 * @param[in] fw_request, the scif task request object that asked for this
 *            target reset.
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
void scif_sas_remote_device_target_reset(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device, 
   SCIF_SAS_REQUEST_T       * fw_request
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = fw_device->domain->controller;

   SCIF_LOG_INFO((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "scif_sas_remote_device_target_reset! fw_device:0x%p fw_request:0x%p\n",
      fw_device, fw_request
   ));
   
   if (fw_device->containing_device == NULL)
   {
      SCI_PORT_HANDLE_T port;

      port = scif_domain_get_scic_port_handle(fw_device->domain);

      //Direct attached device target reset.
      //calling core to do port reset. The fw_request will not be used here.
      scic_port_hard_reset(
         port, 
         scic_remote_device_get_suggested_reset_timeout(fw_device->core_object)
      );
   }
   else
   {  //Expander attached device target reset.    
      
      //set current_activity and current_smp_request to expander device.   
      fw_device->containing_device->protocol_device.smp_device.current_activity = 
         SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_TARGET_RESET; 
      fw_device->containing_device->protocol_device.smp_device.current_smp_request =
         SMP_FUNCTION_PHY_CONTROL;    
      fw_device->containing_device->protocol_device.smp_device.current_activity_phy_index =
         fw_device->expander_phy_identifier; 
      
      //A Phy Control smp request has been constructed towards parent device. 
      //Walk the high priority io path.
      fw_controller->state_handlers->start_high_priority_io_handler(
         (SCI_BASE_CONTROLLER_T*) fw_controller,
         (SCI_BASE_REMOTE_DEVICE_T*) fw_device->containing_device,
         (SCI_BASE_REQUEST_T*) fw_request,
         SCI_CONTROLLER_INVALID_IO_TAG
      ); 
   }

   scic_remote_device_reset(fw_device->core_object);
}


/**
 * @brief This method completes target reset for DA or EA remote device.
 * 
 * @param[in] fw_device, the target device to be hard reset.
 * @param[in] fw_request, the scif task request object that asked for this
 *            target reset.
 * @param[in] completion_status
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
void scif_sas_remote_device_target_reset_complete(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device, 
   SCIF_SAS_REQUEST_T       * fw_request,
   SCI_STATUS                 completion_status
)
{
   SCIF_LOG_INFO((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "scif_sas_remote_device_target_reset_complete! "
      "fw_device:0x%p fw_request:0x%p completion_status 0x%x\n",
      fw_device, fw_request, completion_status
   ));

   scif_cb_task_request_complete(
      fw_device->domain->controller,
      fw_device,
      fw_request,
      (SCI_TASK_STATUS) completion_status
   );
   
   if (fw_device->containing_device != NULL)
      //reset all the activity information of this smp device.
      scif_sas_smp_remote_device_clear(fw_device->containing_device); 
   
   scic_remote_device_reset_complete(fw_device->core_object);

   //Tell driver to kick off Discover process.
   scif_cb_domain_change_notification(
      fw_device->domain->controller, fw_device->domain );
}


#ifdef SCI_LOGGING
PLACEMENT_HINTS((LOGGING))
void scif_sas_remote_device_initialize_state_logging(
   SCIF_SAS_REMOTE_DEVICE_T * remote_device
)
{
   sci_base_state_machine_logger_initialize(
      &remote_device->parent.state_machine_logger,
      &remote_device->parent.state_machine,
      &remote_device->parent.parent,
      scif_cb_logger_log_states,
      "SCIF_SAS_REMOTE_DEVICE_T", "base_state_machine",
      SCIF_LOG_OBJECT_REMOTE_DEVICE
   );

   sci_base_state_machine_logger_initialize(
      &remote_device->starting_substate_machine_logger,
      &remote_device->starting_substate_machine,
      &remote_device->parent.parent,
      scif_cb_logger_log_states,
      "SCIF_SAS_REMOTE_DEVICE_T", "starting substate machine",
      SCIF_LOG_OBJECT_REMOTE_DEVICE
   );

   sci_base_state_machine_logger_initialize(
      &remote_device->ready_substate_machine_logger,
      &remote_device->ready_substate_machine,
      &remote_device->parent.parent,
      scif_cb_logger_log_states,
      "SCIF_SAS_REMOTE_DEVICE_T", "ready substate machine",
      SCIF_LOG_OBJECT_REMOTE_DEVICE
   );
}

PLACEMENT_HINTS((LOGGING))
void scif_sas_remote_device_deinitialize_state_logging(
   SCIF_SAS_REMOTE_DEVICE_T * remote_device
)
{
   sci_base_state_machine_logger_deinitialize(
      &remote_device->parent.state_machine_logger,
      &remote_device->parent.state_machine
   );

   sci_base_state_machine_logger_deinitialize(
      &remote_device->starting_substate_machine_logger,
      &remote_device->starting_substate_machine
   );

   sci_base_state_machine_logger_deinitialize(
      &remote_device->ready_substate_machine_logger,
      &remote_device->ready_substate_machine
   );
}
#endif // SCI_LOGGING


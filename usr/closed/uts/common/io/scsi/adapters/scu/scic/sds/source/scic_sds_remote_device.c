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
 * @brief This file contains the implementation of remote device, it's
 *        methods and state machine.
 */

#include "intel_sas.h"
#include "sci_util.h"
#include "scic_port.h"
#include "scic_phy.h"
#include "scic_remote_device.h"
#include "scic_sds_port.h"
#include "scic_sds_phy.h"
#include "scic_sds_remote_device.h"
#include "scic_sds_request.h"
#include "scic_sds_controller.h"
#include "scic_sds_logger.h"
#include "scic_user_callback.h"
#include "scic_controller.h"
#include "scic_sds_logger.h"
#include "scic_sds_remote_node_context.h"
#include "scu_event_codes.h"

#define SCIC_SDS_REMOTE_DEVICE_RESET_TIMEOUT  (1000)

//*****************************************************************************
//*  CORE REMOTE DEVICE PUBLIC METHODS
//*****************************************************************************

PLACEMENT_HINTS((INITIALIZATION))
U32 scic_remote_device_get_object_size(void)
{
   return   sizeof(SCIC_SDS_REMOTE_DEVICE_T)
          + sizeof(SCIC_SDS_REMOTE_NODE_CONTEXT_T);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
void scic_remote_device_construct(
   SCI_PORT_HANDLE_T            port,
   void                       * remote_device_memory,
   SCI_REMOTE_DEVICE_HANDLE_T * new_remote_device_handle
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T*)
                                           remote_device_memory;
   SCIC_SDS_PORT_T          *the_port    = (SCIC_SDS_PORT_T*) port;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(the_port),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_construct(0x%p, 0x%p, 0x%p) enter\n",
      port, remote_device_memory, new_remote_device_handle
   ));
   
   memset(remote_device_memory, 0, sizeof(SCIC_SDS_REMOTE_DEVICE_T));

   *new_remote_device_handle          = this_device;
   this_device->owning_port           = the_port;
   this_device->started_request_count = 0;
   this_device->rnc = (SCIC_SDS_REMOTE_NODE_CONTEXT_T *)
      ((char *)this_device + sizeof(SCIC_SDS_REMOTE_DEVICE_T));

   sci_base_remote_device_construct(
      &this_device->parent,
      sci_base_object_get_logger(the_port),
      scic_sds_remote_device_state_table
   );

   scic_sds_remote_node_context_construct(
      this_device,
      this_device->rnc,
      SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX
   );

   sci_object_set_association(this_device->rnc, this_device);

   scic_sds_remote_device_initialize_state_logging(this_device);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
SCI_STATUS scic_remote_device_da_construct(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
)
{
   SCI_STATUS                status;
   U16                       remote_node_index;
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T*)
                                           remote_device;
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T  protocols;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device->owning_port),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_da_construct(0x%p) enter\n",
      remote_device
   ));

   // This information is request to determine how many remote node context
   // entries will be needed to store the remote node.
   scic_sds_port_get_attached_protocols(this_device->owning_port,&protocols);
   this_device->target_protocols.u.all = protocols.u.all;
   this_device->is_direct_attached = TRUE;
#if !defined(DISABLE_ATAPI)
   this_device->is_atapi = scic_sds_remote_device_is_atapi(this_device);
#endif

   status = scic_sds_controller_allocate_remote_node_context(
               this_device->owning_port->owning_controller,
               this_device,
               &remote_node_index
            );

   if (status == SCI_SUCCESS)
   {
      scic_sds_remote_node_context_set_remote_node_index(
         this_device->rnc, remote_node_index
      );

      scic_sds_port_get_attached_sas_address(
         this_device->owning_port, &this_device->device_address
      );

      if (this_device->target_protocols.u.bits.attached_ssp_target)
      {
         this_device->has_ready_substate_machine = FALSE;
      }
      else if (this_device->target_protocols.u.bits.attached_stp_target)
      {
         this_device->has_ready_substate_machine = TRUE;

         sci_base_state_machine_construct(
            &this_device->ready_substate_machine,
            &this_device->parent.parent,
            scic_sds_stp_remote_device_ready_substate_table,
            SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE
         );
      }
      else if (this_device->target_protocols.u.bits.attached_smp_target)
      {
         this_device->has_ready_substate_machine = TRUE;

         //add the SMP ready substate machine construction here
         sci_base_state_machine_construct(
            &this_device->ready_substate_machine,
            &this_device->parent.parent,
            scic_sds_smp_remote_device_ready_substate_table,
            SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE
         );
      }

      this_device->connection_rate = scic_sds_port_get_max_allowed_speed(
                                        this_device->owning_port
                                     );

      /// @todo Should I assign the port width by reading all of the phys on the port?
      this_device->device_port_width = 1;
   }

   return status;
}


// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
void scic_sds_remote_device_get_info_from_smp_discover_response(
   SCIC_SDS_REMOTE_DEVICE_T    * this_device,
   SMP_RESPONSE_DISCOVER_T     * discover_response
)
{
   // decode discover_response to set sas_address to this_device.
   this_device->device_address.high =
      discover_response->attached_sas_address.high;

   this_device->device_address.low =
      discover_response->attached_sas_address.low;

   this_device->target_protocols.u.all = discover_response->protocols.u.all;
}


// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
SCI_STATUS scic_remote_device_ea_construct(
   SCI_REMOTE_DEVICE_HANDLE_T    remote_device,
   SMP_RESPONSE_DISCOVER_T     * discover_response
)
{
   SCI_STATUS status;

   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   SCIC_SDS_CONTROLLER_T    *the_controller;

   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device->owning_port),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_ea_sas_construct0x%p, 0x%p) enter\n",
      remote_device, discover_response
   ));

   the_controller = scic_sds_port_get_controller(this_device->owning_port);

   scic_sds_remote_device_get_info_from_smp_discover_response(
      this_device, discover_response
   ); 

   status = scic_sds_controller_allocate_remote_node_context(
               the_controller, 
               this_device, 
               &this_device->rnc->remote_node_index
            );

   if (status == SCI_SUCCESS)
   {
      if (this_device->target_protocols.u.bits.attached_ssp_target)
      {
         this_device->has_ready_substate_machine = FALSE;
      }
      else if (this_device->target_protocols.u.bits.attached_smp_target)
      {
         this_device->has_ready_substate_machine = TRUE;

         //add the SMP ready substate machine construction here
         sci_base_state_machine_construct(
            &this_device->ready_substate_machine,
            &this_device->parent.parent,
            scic_sds_smp_remote_device_ready_substate_table,
            SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE
         );
      }
      else if (this_device->target_protocols.u.bits.attached_stp_target)
      {
         this_device->has_ready_substate_machine = TRUE;

         sci_base_state_machine_construct(
            &this_device->ready_substate_machine,
            &this_device->parent.parent,
            scic_sds_stp_remote_device_ready_substate_table,
            SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE
         );
      }

      // For SAS-2 the physical link rate is actually a logical link
      // rate that incorporates multiplexing.  The SCU doesn't
      // incorporate multiplexing and for the purposes of the
      // connection the logical link rate is that same as the
      // physical.  Furthermore, the SAS-2 and SAS-1.1 fields overlay
      // one another, so this code works for both situations.
      this_device->connection_rate = MIN(
         scic_sds_port_get_max_allowed_speed( this_device->owning_port), 
         discover_response->u2.sas1_1.negotiated_physical_link_rate
         );  

      /// @todo Should I assign the port width by reading all of the phys on the port?
      this_device->device_port_width = 1;
   }

   return status;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
SCI_STATUS scic_remote_device_destruct(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_destruct(0x%p) enter\n",
      remote_device
   ));

   return this_device->state_handlers->parent.destruct_handler(&this_device->parent);
}

// ---------------------------------------------------------------------------

#if !defined(DISABLE_WIDE_PORTED_TARGETS)

PLACEMENT_HINTS((HOT_PLUG))
SCI_STATUS scic_remote_device_da_add_phy(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_PHY_HANDLE_T            phy
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   SCIC_SDS_PHY_T           *the_phy;

   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;
   the_phy     = (SCIC_SDS_PHY_T           *)phy;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_da_add_phy(0x%p, 0x%p) enter\n",
      remote_device, phy
   ));

   if (scic_sds_phy_get_port(the_phy)
       == scic_sds_remote_device_get_port(this_device))
   {
      this_device->device_port_width++;

      return SCI_SUCCESS;
   }

   return SCI_FAILURE;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
SCI_STATUS scic_remote_device_ea_add_phy(
   SCI_REMOTE_DEVICE_HANDLE_T   remote_device,
   SMP_RESPONSE_DISCOVER_T    * discover_response
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_ea_add_phy(0x%p, 0x%p) enter\n",
      remote_device, discover_response
   ));

    /// @todo set device_port_width to 1 for now. Implement it after 1.3.1.
   // need to read discover_response and detect identical attached sas_address,
   // then decide device port width.
   this_device->device_port_width = 1;

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
SCI_STATUS scic_remote_device_remove_phy(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_remove_phy(0x%p) enter\n",
      remote_device
   ));

   /// @todo Implement this function
   return SCI_FAILURE;
}

#endif // !defined(DISABLE_WIDE_PORTED_TARGETS)

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
SCI_STATUS scic_remote_device_start(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   U32                         timeout
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_start(0x%p, 0x%x) enter\n",
      remote_device, timeout
   ));

   return this_device->state_handlers->parent.start_handler(&this_device->parent);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
SCI_STATUS scic_remote_device_stop(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   U32                         timeout
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_stop(0x%p, 0x%x) enter\n",
      remote_device, timeout
   ));

   return this_device->state_handlers->parent.stop_handler(&this_device->parent);
}

/**
 * This method invokes the remote device reset handler.
 * 
 * @param[in] this_device The remote device for which the reset is being 
 *       requested.
 * 
 * @return SCI_STATUS 
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_remote_device_reset(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_reset(0x%p) enter\n",
      remote_device
   ));

   return this_device->state_handlers->parent.reset_handler(&this_device->parent);
}

/**
 * This method invokes the remote device reset handler.
 * 
 * @param[in] this_device The remote device for which the reset is being 
 *       requested.
 * 
 * @return SCI_STATUS 
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_STATUS scic_remote_device_reset_complete(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_reset_complete(0x%p) enter\n",
      remote_device
   ));

   return this_device->state_handlers->parent.reset_complete_handler(&this_device->parent);
}

/**
 * This method invokes the remote device reset handler.
 * 
 * @param[in] this_device The remote device for which the reset is being 
 *       requested.
 * 
 * @return SCI_STATUS 
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
U32 scic_remote_device_get_suggested_reset_timeout(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_get_suggested_reset_timeout(0x%p) enter\n",
      remote_device
   ));

   if (this_device->target_protocols.u.bits.attached_stp_target)
   {
      return SCIC_SDS_SIGNATURE_FIS_TIMEOUT;
   }

   return SCIC_SDS_REMOTE_DEVICE_RESET_TIMEOUT;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((HOT_PLUG))
SCI_STATUS scic_remote_device_set_max_connection_rate(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_SAS_LINK_RATE           connection_rate
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_set_max_connection_rate(0x%p, 0x%x) enter\n",
      remote_device, connection_rate
   ));

   this_device->connection_rate = connection_rate;

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
SCI_SAS_LINK_RATE scic_remote_device_get_connection_rate(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_get_connection_rate(0x%p) enter\n",
      remote_device
   ));

   return this_device->connection_rate;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
void scic_remote_device_get_protocols(
   SCI_REMOTE_DEVICE_HANDLE_T          remote_device,
   SMP_DISCOVER_RESPONSE_PROTOCOLS_T * protocols
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device = (SCIC_SDS_REMOTE_DEVICE_T *)
                                            remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_get_protocols(0x%p) enter\n",
      remote_device
   ));

   protocols->u.all = this_device->target_protocols.u.all;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((TBD))
void scic_remote_device_get_sas_address(
   SCI_REMOTE_DEVICE_HANDLE_T   remote_device,
   SCI_SAS_ADDRESS_T          * sas_address
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
      "scic_remote_device_get_sas_address(0x%p, 0x%p) enter\n",
      remote_device, sas_address
   ));

   sas_address->low = this_device->device_address.low;
   sas_address->high = this_device->device_address.high;
}

// ---------------------------------------------------------------------------
#if !defined(DISABLE_ATAPI)
PLACEMENT_HINTS((TBD))
BOOL scic_remote_device_is_atapi(
   SCI_REMOTE_DEVICE_HANDLE_T device_handle
)
{
  return ((SCIC_SDS_REMOTE_DEVICE_T *)device_handle)->is_atapi;
}
#endif


//*****************************************************************************
//*  SCU DRIVER STANDARD (SDS) REMOTE DEVICE IMPLEMENTATIONS
//*****************************************************************************

/**
 * Remote device timer requirements
 */
#define SCIC_SDS_REMOTE_DEVICE_MINIMUM_TIMER_COUNT (0)
#define SCIC_SDS_REMOTE_DEVICE_MAXIMUM_TIMER_COUNT (SCI_MAX_REMOTE_DEVICES)

/**
 * @brief This method returns the minimum number of timers required for all
 *        remote devices.
 *
 * @return U32
 */
PLACEMENT_HINTS((INITIALIZATION))
U32 scic_sds_remote_device_get_min_timer_count(void)
{
   return SCIC_SDS_REMOTE_DEVICE_MINIMUM_TIMER_COUNT;
}

/**
 * @brief This method returns the maximum number of timers requried for all
 *        remote devices.
 *
 * @return U32
 */
PLACEMENT_HINTS((INITIALIZATION))
U32 scic_sds_remote_device_get_max_timer_count(void)
{
   return SCIC_SDS_REMOTE_DEVICE_MAXIMUM_TIMER_COUNT;
}

// ---------------------------------------------------------------------------

#ifdef SCI_LOGGING
/**
 * This method will enable and turn on state transition logging for the remote
 * device object.
 *
 * @param[in] this_device The device for which state transition logging is to
 *       be enabled.
 *
 * @return Nothing
 */
PLACEMENT_HINTS((LOGGING))
void scic_sds_remote_device_initialize_state_logging(
   SCIC_SDS_REMOTE_DEVICE_T *this_device
)
{
   sci_base_state_machine_logger_initialize(
      &this_device->parent.state_machine_logger,
      &this_device->parent.state_machine,
      &this_device->parent.parent,
      scic_cb_logger_log_states,
      "SCIC_SDS_REMOTE_DEVICE_T", "base state machine",
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET
   );

   if (this_device->has_ready_substate_machine)
   {
      sci_base_state_machine_logger_initialize(
         &this_device->ready_substate_machine_logger,
         &this_device->ready_substate_machine,
         &this_device->parent.parent,
         scic_cb_logger_log_states,
         "SCIC_SDS_REMOTE_DEVICE_T", "ready substate machine",
         SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
         SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
         SCIC_LOG_OBJECT_STP_REMOTE_TARGET
      );
   }
}

/**
 * This method will stop the state machine logging for this object and should
 * be called before the object is destroyed.
 *
 * @param[in] this_device The device on which to stop logging state
 *       transitions.
 *
 * @return Nothing
 */
PLACEMENT_HINTS((LOGGING))
void scic_sds_remote_device_deinitialize_state_logging(
   SCIC_SDS_REMOTE_DEVICE_T *this_device
)
{
   sci_base_state_machine_logger_deinitialize(
      &this_device->parent.state_machine_logger,
      &this_device->parent.state_machine
   );

   if (this_device->has_ready_substate_machine)
   {
      sci_base_state_machine_logger_deinitialize(
         &this_device->ready_substate_machine_logger,
         &this_device->ready_substate_machine
      );
   }
}
#endif

/**
 * This method invokes the remote device suspend state handler.
 *
 * @param[in] this_device The remote device for which the suspend is being
 *       requested.
 *
 * @return SCI_STATUS
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_suspend(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       suspend_type
)
{
   return this_device->state_handlers->suspend_handler(this_device, suspend_type);
}

/**
 * This method invokes the remote device resume state handler.
 *
 * @param[in] this_device The remote device for which the resume is being
 *       requested.
 *
 * @return SCI_STATUS
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_resume(
   SCIC_SDS_REMOTE_DEVICE_T *this_device
)
{
   return this_device->state_handlers->resume_handler(this_device);
}

/**
 * This method invokes the frame handler for the remote device state machine
 *
 * @param[in] this_device The remote device for which the event handling is
 *       being requested.
 * @param[in] frame_index This is the frame index that is being processed.
 *
 * @return SCI_STATUS
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_frame_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       frame_index
)
{
   return this_device->state_handlers->frame_handler(this_device, frame_index);
}

/**
 * This method invokes the remote device event handler.
 *
 * @param[in] this_device The remote device for which the event handling is
 *       being requested.
 * @param[in] event_code This is the event code that is to be processed.
 *
 * @return SCI_STATUS
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_event_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       event_code
)
{
   return this_device->state_handlers->event_handler(this_device, event_code);
}

/**
 * This method invokes the remote device start io handler.
 *
 * @param[in] controller The controller that is starting the io request.
 * @param[in] this_device The remote device for which the start io handling is
 *       being requested.
 * @param[in] io_request The io request that is being started.
 *
 * @return SCI_STATUS
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_STATUS scic_sds_remote_device_start_io(
   SCIC_SDS_CONTROLLER_T    *controller,
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   SCIC_SDS_REQUEST_T       *io_request
)
{
   return this_device->state_handlers->parent.start_io_handler(
                                 &this_device->parent, &io_request->parent);
}

/**
 * This method invokes the remote device complete io handler.
 *
 * @param[in] controller The controller that is completing the io request.
 * @param[in] this_device The remote device for which the complete io handling
 *       is being requested.
 * @param[in] io_request The io request that is being completed.
 *
 * @return SCI_STATUS
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_STATUS scic_sds_remote_device_complete_io(
   SCIC_SDS_CONTROLLER_T    *controller,
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   SCIC_SDS_REQUEST_T       *io_request
)
{
   return this_device->state_handlers->parent.complete_io_handler(
                                 &this_device->parent, &io_request->parent);
}

/**
 * This method invokes the remote device start task handler.
 *
 * @param[in] controller The controller that is starting the task request.
 * @param[in] this_device The remote device for which the start task handling
 *       is being requested.
 * @param[in] io_request The task request that is being started.
 *
 * @return SCI_STATUS
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_STATUS scic_sds_remote_device_start_task(
   SCIC_SDS_CONTROLLER_T    *controller,
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   SCIC_SDS_REQUEST_T       *io_request
)
{
   return this_device->state_handlers->parent.start_task_handler(
                                  &this_device->parent, &io_request->parent);
}

/**
 * This method invokes the remote device complete task handler.
 *
 * @param[in] controller The controller that is completing the task request.
 * @param[in] this_device The remote device for which the complete task
 *       handling is being requested.
 * @param[in] io_request The task request that is being completed.
 *
 * @return SCI_STATUS
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_STATUS scic_sds_remote_device_complete_task(
   SCIC_SDS_CONTROLLER_T    *controller,
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   SCIC_SDS_REQUEST_T       *io_request
)
{
   return this_device->state_handlers->parent.complete_task_handler(
                                 &this_device->parent, &io_request->parent);
}

/**
 * This method takes the request and bulids an appropriate SCU context for the
 * request and then requests the controller to post the request.
 *
 * @param[in] this_device
 * @param[in] request
 *
 * @return none
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
void scic_sds_remote_device_post_request(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   U32                        request
)
{
   U32 context;

   context = scic_sds_remote_device_build_command_context(this_device, request);

   scic_sds_controller_post_request(
      scic_sds_remote_device_get_controller(this_device),
      context
   );
}

#if !defined(DISABLE_ATAPI)
/**
 * This method check the signature fis of a stp device to decide whether
 * a device is atapi or not.
 *
 * @param[in] this_device The device to be checked.
 *
 * @return TRUE if a device is atapi device. False if a device is not atapi.
 */
PLACEMENT_HINTS((TBD))
BOOL scic_sds_remote_device_is_atapi(
   SCIC_SDS_REMOTE_DEVICE_T * this_device
)
{
   if (!this_device->target_protocols.u.bits.attached_stp_target)
      return FALSE;
   else if (this_device->is_direct_attached)
   {
      SCIC_SDS_PHY_T * phy;
      SCIC_SATA_PHY_PROPERTIES_T properties;   
      SATA_FIS_REG_D2H_T * signature_fis;
      phy = scic_sds_port_get_a_connected_phy(this_device->owning_port);
      scic_sata_phy_get_properties(phy, &properties);
   
      //decode the signature fis.
      signature_fis = &(properties.signature_fis);  

      if (   (signature_fis->sector_count  == 0x01)
          && (signature_fis->lba_low       == 0x01)
          && (signature_fis->lba_mid       == 0x14)
          && (signature_fis->lba_high      == 0xEB)
          && ( (signature_fis->device & 0x5F) == 0x00)
         )
      {
         // An ATA device supporting the PACKET command set.
         return TRUE;
      }   
      else 
         return FALSE;
   }
   else
   {
      //Expander supported ATAPI device is not currently supported.
      return FALSE;  
   }
}

#endif // !defined(DISABLE_ATAPI)

//******************************************************************************
//* REMOTE DEVICE STATE MACHINE
//******************************************************************************

/**
 * This method is called once the remote node context is ready to be 
 * freed.  The remote device can now report that its stop operation is 
 * complete. 
 * 
 * @param[in] user_parameter This is cast to a remote device object.
 *  
 * @return none 
 */
PLACEMENT_HINTS((HOT_PLUG))
void scic_sds_cb_remote_device_rnc_destruct_complete(
   void * user_parameter
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)user_parameter;

   ASSERT(this_device->started_request_count == 0);

   sci_base_state_machine_change_state(
      scic_sds_remote_device_get_base_state_machine(this_device),
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPED
   );
}

/**
 * This method is called once the remote node context has transisitioned to a 
 * ready state.  This is the indication that the remote device object can also 
 * transition to ready. 
 * 
 * @param[in] user_parameter This is cast to a remote device object. 
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_resume_complete_handler(
   void * user_parameter
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)user_parameter;

   if (
         sci_base_state_machine_get_state(&this_device->parent.state_machine)
      != SCI_BASE_REMOTE_DEVICE_STATE_READY
      )
   {
      sci_base_state_machine_change_state(
         &this_device->parent.state_machine,
         SCI_BASE_REMOTE_DEVICE_STATE_READY
      );
   }
}

/**
 * This method will perform the STP request start processing common
 * to IO requests and task requests of all types.
 *
 * @param[in] device This parameter specifies the device for which the
 *            request is being started.
 * @param[in] request This parameter specifies the request being started.
 * @param[in] status This parameter specifies the current start operation
 *            status.
 *
 * @return none
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
void scic_sds_remote_device_start_request(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   SCIC_SDS_REQUEST_T       * the_request,
   SCI_STATUS                 status
)
{
   // We still have a fault in starting the io complete it on the port
   if (status == SCI_SUCCESS) 
      scic_sds_remote_device_increment_request_count(this_device);
   else
   {
      this_device->owning_port->state_handlers->complete_io_handler(
         this_device->owning_port, this_device, the_request
      );
   }
}


/**
 * This method will continue to post tc for a STP request. This method usually
 * serves as a callback when RNC gets resumed during a task management sequence.
 *
 * @param[in] request This parameter specifies the request being continued.
 *
 * @return none
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_continue_request(
   SCIC_SDS_REMOTE_DEVICE_T * this_device
)
{ 
   // we need to check if this request is still valid to continue.
   if (this_device->working_request != NULL)
   {  
      SCIC_SDS_REQUEST_T * this_request = this_device->working_request;
         
      this_request->owning_controller->state_handlers->parent.continue_io_handler(
         &this_request->owning_controller->parent, 
         &this_request->target_device->parent, 
         &this_request->parent
      );   
   }
}

/**
 * This method is called once the remote node context has reached a suspended 
 * state. The remote device can now report that its suspend operation is 
 * complete. 
 * 
 * @param[in] user_parameter This is cast to a remote device object.
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_suspend_complete_handler(
   void * user_parameter
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)user_parameter;

   // @todo Does the core notify anyone that the remote device has suspended?
}

/**
 * @brief This method will terminate all of the IO requests in the
 *        controllers IO request table that were targeted for this
 *        device.
 *
 * @param[in]  this_device This parameter specifies the remote device
 *             for which to attempt to terminate all requests.
 *
 * @return This method returns an indication as to whether all requests
 *         were successfully terminated.  If a single request fails to
 *         be terminated, then this method will return the failure.
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_STATUS scic_sds_remote_device_terminate_requests(
   SCIC_SDS_REMOTE_DEVICE_T *this_device
)
{
   SCI_STATUS          status           = SCI_SUCCESS;
   SCI_STATUS          terminate_status = SCI_SUCCESS;
   SCIC_SDS_REQUEST_T *the_request;
   U32                 index;
   U32                 request_count    = this_device->started_request_count;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET | SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "scic_sds_remote_device_terminate_requests(0x%p) enter\n",
      this_device
   ));

   for (index = 0;
        (index < SCI_MAX_IO_REQUESTS) && (request_count > 0);
        index++)
   {
      the_request = this_device->owning_port->owning_controller->io_request_table[index];

      if ((the_request != NULL) && (the_request->target_device == this_device))
      {
         terminate_status = scic_controller_terminate_request(
                               this_device->owning_port->owning_controller,
                               this_device,
                               the_request
                            );

         if (terminate_status != SCI_SUCCESS)
            status = terminate_status;

         request_count--;
      }
   }

   return status;
}

//*****************************************************************************
//*  DEFAULT STATE HANDLERS
//*****************************************************************************

/**
 * This method is the default start handler.  It logs a warning and returns a 
 * failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_start_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REMOTE_DEVICE_T *)device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to start while in wrong state %d\n",
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine((SCIC_SDS_REMOTE_DEVICE_T *)device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default stop handler.  It logs a warning and returns a 
 * failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REMOTE_DEVICE_T *)device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to stop while in wrong state %d\n",
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine((SCIC_SDS_REMOTE_DEVICE_T *)device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default fail handler.  It logs a warning and returns a 
 * failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_fail_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REMOTE_DEVICE_T *)device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to fail while in wrong state %d\n",
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine((SCIC_SDS_REMOTE_DEVICE_T *)device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default destruct handler.  It logs a warning and returns
 * a failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_destruct_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REMOTE_DEVICE_T *)device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to destroy while in wrong state %d\n",
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine((SCIC_SDS_REMOTE_DEVICE_T *)device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default reset handler.  It logs a warning and returns a 
 * failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_reset_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REMOTE_DEVICE_T *)device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to reset while in wrong state %d\n",
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine((SCIC_SDS_REMOTE_DEVICE_T *)device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default reset complete handler.  It logs a warning and 
 * returns a failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_reset_complete_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REMOTE_DEVICE_T *)device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to complete reset while in wrong state %d\n",
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine((SCIC_SDS_REMOTE_DEVICE_T *)device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default suspend handler.  It logs a warning and returns 
 * a failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_suspend_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       suspend_type
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device 0x%p requested to suspend %d while in wrong state %d\n",
      this_device, suspend_type,
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine(this_device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default resume handler.  It logs a warning and returns a
 * failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_resume_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to resume while in wrong state %d\n",
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine(this_device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

#if defined(SCI_LOGGING)
/** 
 *  This is a private method for emitting log messages related to events reported 
 *  to the remote device from the controller object.
 *  
 *  @param [in] this_device This is the device object that is receiving the
 *         event.
 *  @param [in] event_code The event code to process.
 *  
 *  @return None
 */
PLACEMENT_HINTS((LOGGING))
static void scic_sds_emit_event_log_message(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   U32                        event_code,
   char *                     message_guts,
   BOOL                       ready_state
   )
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote device 0x%p (state %d) received %s 0x%x while in the %sready %s%d\n",
      this_device, 
      sci_base_state_machine_get_state(
               scic_sds_remote_device_get_base_state_machine(this_device)),
      message_guts, event_code, 
      (ready_state)
        ? ""
        : "not ",
      (this_device->has_ready_substate_machine)
        ? "substate "
        : "",
      (this_device->has_ready_substate_machine)
        ? sci_base_state_machine_get_state(&this_device->ready_substate_machine)
        : 0
   ));
}
#else // defined(SCI_LOGGING)
#define scic_sds_emit_event_log_message(device, event_code, message, state)
#endif // defined(SCI_LOGGING)

/**
 * This method is the default event handler.  It will call the RNC state
 * machine handler for any RNC events otherwise it will log a warning and
 * returns a failure.
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * @param[in] event_code The event code that the SCIC_SDS_CONTROLLER wants the 
 *       device object to process.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS  scic_sds_remote_device_core_event_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       event_code,
   BOOL                      is_ready_state
)
{
   SCI_STATUS status;

   switch (scu_get_event_type(event_code))
   {
   case SCU_EVENT_TYPE_RNC_OPS_MISC:
   case SCU_EVENT_TYPE_RNC_SUSPEND_TX:
   case SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX:
      status = scic_sds_remote_node_context_event_handler(this_device->rnc, event_code);
   break;
   case SCU_EVENT_TYPE_PTX_SCHEDULE_EVENT:
       
       if( scu_get_event_code(event_code) == SCU_EVENT_IT_NEXUS_TIMEOUT )
       {
           status = SCI_SUCCESS;
	   
           // Suspend the associated RNC
           scic_sds_remote_node_context_suspend( this_device->rnc, 
                                                 SCI_SOFTWARE_SUSPENSION,
                                                 NULL, NULL );

           scic_sds_emit_event_log_message( 
	           this_device, event_code, 
                   (is_ready_state)
		      ? "I_T_Nexus_Timeout event"
		      : "I_T_Nexus_Timeout event in wrong state", 
                   is_ready_state );
		   
           break;
       }
       // Else, fall through and treat as unhandled...

   default:
      scic_sds_emit_event_log_message( this_device, event_code,
                                       (is_ready_state)
                                          ? "unexpected event"
                                          : "unexpected event in wrong state",
                                       is_ready_state );
      status = SCI_FAILURE_INVALID_STATE;
   break;
   }

   return status;
}
/**
 * This method is the default event handler.  It will call the RNC state
 * machine handler for any RNC events otherwise it will log a warning and
 * returns a failure.
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * @param[in] event_code The event code that the SCIC_SDS_CONTROLLER wants the 
 *       device object to process.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS  scic_sds_remote_device_default_event_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       event_code
)
{
   return scic_sds_remote_device_core_event_handler( this_device,
                                                     event_code,
                                                     FALSE );
}

/**
 * This method is the default unsolicited frame handler.  It logs a warning, 
 * releases the frame and returns a failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * @param[in] frame_index The frame index for which the SCIC_SDS_CONTROLLER 
 *       wants this device object to process.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_frame_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       frame_index
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to handle frame %x while in wrong state %d\n",
      frame_index,
      sci_base_state_machine_get_state(&this_device->parent.state_machine)
   ));

   // Return the frame back to the controller
   scic_sds_controller_release_frame(
      scic_sds_remote_device_get_controller(this_device), frame_index
   );

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default start io handler.  It logs a warning and returns
 * a failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * @param[in] request The SCI_BASE_REQUEST which is then cast into a 
 *       SCIC_SDS_IO_REQUEST to start.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_start_request_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REMOTE_DEVICE_T *)device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to start io request 0x%p while in wrong state %d\n",
      request,
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine((SCIC_SDS_REMOTE_DEVICE_T *)device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default complete io handler.  It logs a warning and 
 * returns a failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * @param[in] request The SCI_BASE_REQUEST which is then cast into a 
 *       SCIC_SDS_IO_REQUEST to complete.
 * 
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_complete_request_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REMOTE_DEVICE_T *)device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to complete io_request 0x%p while in wrong state %d\n",
      request,
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine((SCIC_SDS_REMOTE_DEVICE_T *)device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default continue io handler.  It logs a warning and 
 * returns a failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * @param[in] request The SCI_BASE_REQUEST which is then cast into a 
 *       SCIC_SDS_IO_REQUEST to continue.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_continue_request_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REMOTE_DEVICE_T *)device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to continue io request 0x%p while in wrong state %d\n",
      request,
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine((SCIC_SDS_REMOTE_DEVICE_T *)device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default complete task handler.  It logs a warning and 
 * returns a failure. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * @param[in] request The SCI_BASE_REQUEST which is then cast into a 
 *       SCIC_SDS_IO_REQUEST to complete.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_default_complete_task_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REMOTE_DEVICE_T *)device),
      SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
      SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
      "SCIC Remote Device requested to complete task 0x%p while in wrong state %d\n",
      request,
      sci_base_state_machine_get_state(
         scic_sds_remote_device_get_base_state_machine((SCIC_SDS_REMOTE_DEVICE_T *)device))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

//*****************************************************************************
//*  NORMAL STATE HANDLERS
//*****************************************************************************

/**
 * This method is a general ssp frame handler.  In most cases the device 
 * object needs to route the unsolicited frame processing to the io request 
 * object.  This method decodes the tag for the io request object and routes 
 * the unsolicited frame to that object. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is then cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * @param[in] frame_index The frame index for which the SCIC_SDS_CONTROLLER 
 *       wants this device object to process.
 * 
 * @return SCI_STATUS 
 * @retval SCI_FAILURE_INVALID_STATE
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_general_frame_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       frame_index
)
{
   SCI_STATUS result;
   SCI_SSP_FRAME_HEADER_T *frame_header;
   SCIC_SDS_REQUEST_T     *io_request;

   result = scic_sds_unsolicited_frame_control_get_header(
      &(scic_sds_remote_device_get_controller(this_device)->uf_control),
      frame_index,
      (void **)&frame_header
   );

   if (SCI_SUCCESS == result)
   {
      io_request = scic_sds_controller_get_io_request_from_tag(
         scic_sds_remote_device_get_controller(this_device), frame_header->tag);

      if (  (io_request == SCI_INVALID_HANDLE)
         || (io_request->target_device != this_device) )
      {
         // We could not map this tag to a valid IO request
         // Just toss the frame and continue
         scic_sds_controller_release_frame(
            scic_sds_remote_device_get_controller(this_device), frame_index
         );
      }
      else
      {
         // The IO request is now in charge of releasing the frame
         result = io_request->state_handlers->frame_handler(
                                                    io_request, frame_index);
      }
   }

   return result;
}

/** 
 *  This is a common method for handling events reported to the remote device
 *  from the controller object.
 *  
 *  @param [in] this_device This is the device object that is receiving the
 *         event.
 *  @param [in] event_code The event code to process.
 *  
 *  @return SCI_STATUS
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_general_event_handler(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   U32                        event_code
)
{
   return scic_sds_remote_device_core_event_handler( this_device,
                                                     event_code,
                                                     TRUE );
}

//*****************************************************************************
//*  STOPPED STATE HANDLERS
//*****************************************************************************

/**
 * This method takes the SCIC_SDS_REMOTE_DEVICE from a stopped state and 
 * attempts to start it.   The RNC buffer for the device is constructed and 
 * the device state machine is transitioned to the 
 * SCIC_BASE_REMOTE_DEVICE_STATE_STARTING. 
 * 
 * @param[in] device 
 * 
 * @return SCI_STATUS 
 * @retval SCI_SUCCESS if there is an RNC buffer available to construct the 
 *         remote device.
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES if there is no RNC buffer 
 *         available in which to construct the remote device.
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_stopped_state_start_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCI_STATUS status;
   SCIC_SDS_REMOTE_DEVICE_T  *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   status = scic_sds_remote_node_context_resume(
               this_device->rnc,
               scic_sds_remote_device_resume_complete_handler,
               this_device
            );

   if (status == SCI_SUCCESS)
   {
      sci_base_state_machine_change_state(
         scic_sds_remote_device_get_base_state_machine(this_device),
         SCI_BASE_REMOTE_DEVICE_STATE_STARTING
      );
   }

   return status;
}

/**
 * This method will stop a SCIC_SDS_REMOTE_DEVICE that is already in a stopped 
 * state.  This is not considered an error since the device is already 
 * stopped. 
 * 
 * @param[in] this_device The SCI_BASE_REMOTE_DEVICE which is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return SCI_STATUS 
 * @retval SCI_SUCCESS 
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_stopped_state_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T *this_device
)
{
   return SCI_SUCCESS;
}

/**
 * This method will destruct a SCIC_SDS_REMOTE_DEVICE that is in a stopped 
 * state.  This is the only state from which a destruct request will succeed. 
 * The RNi for this SCIC_SDS_REMOTE_DEVICE is returned to the free pool and 
 * the device object transitions to the SCI_BASE_REMOTE_DEVICE_STATE_FINAL. 
 * 
 * @param[in] this_device The SCI_BASE_REMOTE_DEVICE which is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return SCI_STATUS 
 * @retval SCI_SUCCESS 
 */
PLACEMENT_HINTS((HOT_PLUG))
SCI_STATUS scic_sds_remote_device_stopped_state_destruct_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   scic_sds_controller_free_remote_node_context(
      scic_sds_remote_device_get_controller(this_device),
      this_device,
      this_device->rnc->remote_node_index
   );

   scic_sds_remote_node_context_set_remote_node_index(
      this_device->rnc, 
      SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX
   );

   sci_base_state_machine_change_state(
      scic_sds_remote_device_get_base_state_machine(this_device),
      SCI_BASE_REMOTE_DEVICE_STATE_FINAL
   );

   scic_sds_remote_device_deinitialize_state_logging(this_device);

   return SCI_SUCCESS;
}

//*****************************************************************************
//*  STARTING STATE HANDLERS
//*****************************************************************************

PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_starting_state_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   /*
    * This device has not yet started so there had better be no IO requests
    */
   ASSERT(this_device->started_request_count == 0);

   /*
    * Destroy the remote node context
    */
   scic_sds_remote_node_context_destruct(
      this_device->rnc,
      scic_sds_cb_remote_device_rnc_destruct_complete,
      this_device
   );

   /*
    * Transition to the stopping state and wait for the remote node to 
    * complete being posted and invalidated.
    */
   sci_base_state_machine_change_state(
      scic_sds_remote_device_get_base_state_machine(this_device),
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
   );

   return SCI_SUCCESS;
}

//*****************************************************************************
//*  INITIALIZING STATE HANDLERS
//*****************************************************************************

/* There is nothing to do here for SSP devices */

//*****************************************************************************
//*  READY STATE HANDLERS
//*****************************************************************************

/**
 * This method is the resume handler for the SCIC_SDS_REMOTE_DEVICE object. 
 * It will post an RNC resume to the SCU hardware. 
 * 
 * @param[in] this_device The SCIC_SDS_REMOTE_DEVICE object to be suspended.
 * 
 * @return SCI_STATUS 
 * @retval SCI_SUCCESS
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_ready_state_resume_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device
)
{
   SCI_STATUS status;

   status = scic_sds_remote_node_context_resume(
               this_device->rnc, 
               scic_sds_remote_device_resume_complete_handler, 
               this_device
            );

   return status;
}

/**
 * This method is the default stop handler for the SCIC_SDS_REMOTE_DEVICE 
 * ready substate machine. It will stop the current substate machine and 
 * transition the base state machine to SCI_BASE_REMOTE_DEVICE_STATE_STOPPING. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE object which is cast to a 
 *       SCIC_SDS_REMOTE_DEVICE object.
 * 
 * @return SCI_STATUS 
 * @retval SCI_SUCCESS 
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_ready_state_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;
   SCI_STATUS                status      = SCI_SUCCESS;

   // Request the parent state machine to transition to the stopping state
   sci_base_state_machine_change_state(
      scic_sds_remote_device_get_base_state_machine(this_device),
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
   );

   if (this_device->started_request_count == 0)
   {
      scic_sds_remote_node_context_destruct(
         this_device->rnc,
         scic_sds_cb_remote_device_rnc_destruct_complete,
         this_device
      );
   }
   else
      status = scic_sds_remote_device_terminate_requests(this_device);

   return status;
}

/**
 * This is the ready state device reset handler 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE object which is cast to a 
 *       SCIC_SDS_REMOTE_DEVICE object.
 * 
 * @return SCI_STATUS 
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_ready_state_reset_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   // Request the parent state machine to transition to the stopping state
   sci_base_state_machine_change_state(
      scic_sds_remote_device_get_base_state_machine(this_device),
      SCI_BASE_REMOTE_DEVICE_STATE_RESETTING
   );

   return SCI_SUCCESS;
}

/**
 * This is the default fail handler for the SCIC_SDS_REMOTE_DEVICE ready 
 * substate machine.  It will stop the current ready substate and transition 
 * the remote device object to the SCI_BASE_REMOTE_DEVICE_STATE_FAILED. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE object which is cast to a 
 *       SCIC_SDS_REMOTE_DEVICE object.
 * 
 * @return SCI_STATUS 
 * @retval SCI_SUCCESS 
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_ready_state_fail_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   // Request the parent state machine to transition to the failed state
   sci_base_state_machine_change_state(
      scic_sds_remote_device_get_base_state_machine(this_device),
      SCI_BASE_REMOTE_DEVICE_STATE_FAILED
   );

   return SCI_SUCCESS;
}

/**
 * This method will attempt to start a task request for this device object. 
 * The remote device object will issue the start request for the task and if 
 * successful it will start the request for the port object then increment its 
 * own requet count. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is cast to a 
 *       SCIC_SDS_REMOTE_DEVICE for which the request is to be started.
 * @param[in] request The SCI_BASE_REQUEST which is cast to a 
 *       SCIC_SDS_IO_REQUEST that is to be started.
 * 
 * @return SCI_STATUS 
 * @retval SCI_SUCCESS if the task request is started for this device object. 
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES if the io request object could 
 *         not get the resources to start.
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_STATUS scic_sds_remote_device_ready_state_start_task_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
)
{
   SCI_STATUS result;
   SCIC_SDS_REMOTE_DEVICE_T *this_device  = (SCIC_SDS_REMOTE_DEVICE_T *)device;
   SCIC_SDS_REQUEST_T       *task_request = (SCIC_SDS_REQUEST_T       *)request;

   // See if the port is in a state where we can start the IO request
   result = scic_sds_port_start_io(
      scic_sds_remote_device_get_port(this_device), this_device, task_request);

   if (result == SCI_SUCCESS)
   {
      result = scic_sds_remote_node_context_start_task(
                  this_device->rnc, task_request
               );

      if (result == SCI_SUCCESS)
      {
         result = scic_sds_request_start(task_request);
      }

      scic_sds_remote_device_start_request(this_device, task_request, result);
   }

   return result;
}

/**
 * This method will attempt to start an io request for this device object. The 
 * remote device object will issue the start request for the io and if 
 * successful it will start the request for the port object then increment its 
 * own requet count. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is cast to a 
 *       SCIC_SDS_REMOTE_DEVICE for which the request is to be started.
 * @param[in] request The SCI_BASE_REQUEST which is cast to a 
 *       SCIC_SDS_IO_REQUEST that is to be started.
 * 
 * @return SCI_STATUS 
 * @retval SCI_SUCCESS if the io request is started for this device object. 
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES if the io request object could 
 *         not get the resources to start.
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_STATUS scic_sds_remote_device_ready_state_start_io_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
)
{
   SCI_STATUS result;
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;
   SCIC_SDS_REQUEST_T       *io_request  = (SCIC_SDS_REQUEST_T       *)request;

   // See if the port is in a state where we can start the IO request
   result = scic_sds_port_start_io(
      scic_sds_remote_device_get_port(this_device), this_device, io_request);

   if (result == SCI_SUCCESS)
   {
      result = scic_sds_remote_node_context_start_io(
                  this_device->rnc, io_request
               );

      if (result == SCI_SUCCESS)
      {
         result = scic_sds_request_start(io_request);
      }

      scic_sds_remote_device_start_request(this_device, io_request, result);
   }

   return result;
}

/**
 * This method will complete the request for the remote device object.  The
 * method will call the completion handler for the request object and if 
 * successful it will complete the request on the port object then decrement 
 * its own started_request_count. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is cast to a 
 *       SCIC_SDS_REMOTE_DEVICE for which the request is to be completed.
 * @param[in] request The SCI_BASE_REQUEST which is cast to a 
 *       SCIC_SDS_IO_REQUEST that is to be completed.
 * 
 * @return SCI_STATUS 
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_STATUS scic_sds_remote_device_ready_state_complete_request_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
)
{
   SCI_STATUS result;
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;
   SCIC_SDS_REQUEST_T       *the_request = (SCIC_SDS_REQUEST_T       *)request;

   result = scic_sds_request_complete(the_request);

   if (result == SCI_SUCCESS)
   {
      // See if the port is in a state where we can start the IO request
      result = scic_sds_port_complete_io(
         scic_sds_remote_device_get_port(this_device), this_device, the_request);

      if (result == SCI_SUCCESS)
      {
         scic_sds_remote_device_decrement_request_count(this_device);
      }
   }

   return result;
}

//*****************************************************************************
//*  STOPPING STATE HANDLERS
//*****************************************************************************

/**
 * This method will stop a SCIC_SDS_REMOTE_DEVICE that is already in the 
 * SCI_BASE_REMOTE_DEVICE_STATE_STOPPING state. This is not considered an 
 * error since we allow a stop request on a device that is alreay stopping or 
 * stopped. 
 * 
 * @param[in] this_device The SCI_BASE_REMOTE_DEVICE which is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return SCI_STATUS 
 * @retval SCI_SUCCESS 
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_stopping_state_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   // All requests should have been terminated, but if there is an
   // attempt to stop a device already in the stopping state, then
   // try again to terminate.
   return scic_sds_remote_device_terminate_requests(
             (SCIC_SDS_REMOTE_DEVICE_T*)device);
}


/**
 * This method completes requests for this SCIC_SDS_REMOTE_DEVICE while it is 
 * in the SCI_BASE_REMOTE_DEVICE_STATE_STOPPING state. This method calls the 
 * complete method for the request object and if that is successful the port 
 * object is called to complete the task request. Then the device object 
 * itself completes the task request. If SCIC_SDS_REMOTE_DEVICE 
 * started_request_count goes to 0 and the invalidate RNC request has 
 * completed the device object can transition to the 
 * SCI_BASE_REMOTE_DEVICE_STATE_STOPPED. 
 * 
 * @param[in] device The device object for which the request is completing.
 * @param[in] request The task request that is being completed.
 * 
 * @return SCI_STATUS 
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_stopping_state_complete_request_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
)
{
   SCI_STATUS                status = SCI_SUCCESS;
   SCIC_SDS_REQUEST_T       *this_request = (SCIC_SDS_REQUEST_T   *)request;
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   status = scic_sds_request_complete(this_request);
   if (status == SCI_SUCCESS)
   {
      status = scic_sds_port_complete_io(
                  scic_sds_remote_device_get_port(this_device),
                  this_device,
                  this_request
               );

      if (status == SCI_SUCCESS)
      {
         scic_sds_remote_device_decrement_request_count(this_device);

         if (scic_sds_remote_device_get_request_count(this_device) == 0)
         {
            scic_sds_remote_node_context_destruct(
               this_device->rnc,
               scic_sds_cb_remote_device_rnc_destruct_complete,
               this_device
            );
         }
      }
   }

   return status;
}

//*****************************************************************************
//*  RESETTING STATE HANDLERS
//*****************************************************************************

/**
 * This method will complete the reset operation when the device is in the 
 * resetting state. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is to be cast into a 
 *       SCIC_SDS_REMOTE_DEVICE object.
 * 
 * @return SCI_STATUS 
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_resetting_state_reset_complete_handler(
   SCI_BASE_REMOTE_DEVICE_T * device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   sci_base_state_machine_change_state(
      &this_device->parent.state_machine,
      SCI_BASE_REMOTE_DEVICE_STATE_READY
   );

   return SCI_SUCCESS;
}

/**
 * This method will stop the remote device while in the resetting state.
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is to be cast into a 
 *       SCIC_SDS_REMOTE_DEVICE object.
 * 
 * @return SCI_STATUS 
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_resetting_state_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T * device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   sci_base_state_machine_change_state(
      &this_device->parent.state_machine,
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
   );

   return SCI_SUCCESS;
}

/**
 * This method completes requests for this SCIC_SDS_REMOTE_DEVICE while it is 
 * in the SCI_BASE_REMOTE_DEVICE_STATE_RESETTING state. This method calls the 
 * complete method for the request object and if that is successful the port 
 * object is called to complete the task request. Then the device object 
 * itself completes the task request. 
 * 
 * @param[in] device The device object for which the request is completing.
 * @param[in] request The task request that is being completed.
 * 
 * @return SCI_STATUS 
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_resetting_state_complete_request_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
)
{
   SCI_STATUS status = SCI_SUCCESS;
   SCIC_SDS_REQUEST_T       *this_request = (SCIC_SDS_REQUEST_T   *)request;
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   status = scic_sds_request_complete(this_request);

   if (status == SCI_SUCCESS)
   {
      status = scic_sds_port_complete_io(
         scic_sds_remote_device_get_port(this_device), this_device, this_request);

      if (status == SCI_SUCCESS)
      {
         scic_sds_remote_device_decrement_request_count(this_device);
      }
   }

   return status;
}

//*****************************************************************************
//*  FAILED STATE HANDLERS
//*****************************************************************************

/**
 * This method handles the remove request for a failed SCIC_SDS_REMOTE_DEVICE 
 * object. The method will transition the device object to the 
 * SCIC_BASE_REMOTE_DEVICE_STATE_STOPPING. 
 * 
 * @param[in] device The SCI_BASE_REMOTE_DEVICE which is to be cast into a 
 *       SCIC_SDS_REMOTE_DEVICE object.
 * 
 * @return SCI_STATUS 
 * @retval SCI_SUCCESS 
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scic_sds_remote_device_failed_state_remove_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   sci_base_state_machine_change_state(
      scic_sds_remote_device_get_base_state_machine(this_device),
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
   );

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER_T
   scic_sds_remote_device_state_handler_table[SCI_BASE_REMOTE_DEVICE_MAX_STATES] =
{
   // SCI_BASE_REMOTE_DEVICE_STATE_INITIAL
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_default_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_default_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_default_event_handler,
      scic_sds_remote_device_default_frame_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_STOPPED
   {
      {
         scic_sds_remote_device_stopped_state_start_handler,
         scic_sds_remote_device_stopped_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_stopped_state_destruct_handler,
         scic_sds_remote_device_default_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_default_event_handler,
      scic_sds_remote_device_default_frame_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_STARTING
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_starting_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_default_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_general_event_handler,
      scic_sds_remote_device_default_frame_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_READY
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_ready_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_ready_state_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_remote_device_ready_state_start_io_handler,
         scic_sds_remote_device_ready_state_complete_request_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_remote_device_ready_state_start_task_handler,
         scic_sds_remote_device_ready_state_complete_request_handler
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_general_event_handler,
      scic_sds_remote_device_general_frame_handler,
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_stopping_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_default_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_stopping_state_complete_request_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_stopping_state_complete_request_handler
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_general_event_handler,
      scic_sds_remote_device_general_frame_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_FAILED
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_default_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_default_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_default_event_handler,
      scic_sds_remote_device_general_frame_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_RESETTING
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_resetting_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_default_reset_handler,
         scic_sds_remote_device_resetting_state_reset_complete_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_resetting_state_complete_request_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_resetting_state_complete_request_handler
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_default_event_handler,
      scic_sds_remote_device_general_frame_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_FINAL
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_default_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_default_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_default_event_handler,
      scic_sds_remote_device_default_frame_handler
   }
};

/**
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_INITIAL it 
 * immediatly transitions the remote device object to the stopped state. 
 * 
 * @param[in] object This is the SCI_BASE_OBJECT that is cast into a 
 *            SCIC_SDS_REMOTE_DEVICE.
 * 
 * @return none
 */
PLACEMENT_HINTS((HOT_PLUG))
void scic_sds_remote_device_initial_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      this_device, 
      scic_sds_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_INITIAL
   );

   // Initial state is a transitional state to the stopped state
   sci_base_state_machine_change_state(
      scic_sds_remote_device_get_base_state_machine(this_device),
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPED
   );
}

/**
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_INITIAL it 
 * sets the stopped state handlers and if this state is entered from the 
 * SCI_BASE_REMOTE_DEVICE_STATE_STOPPING then the SCI User is informed that 
 * the device stop is complete. 
 * 
 * @param[in] object This is the SCI_BASE_OBJECT that is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_stopped_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      this_device, 
      scic_sds_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPED
   );

   // If we are entering from the stopping state let the SCI User know that
   // the stop operation has completed.
   if (this_device->parent.state_machine.previous_state_id
       == SCI_BASE_REMOTE_DEVICE_STATE_STOPPING)
   {
      scic_cb_remote_device_stop_complete(
         scic_sds_remote_device_get_controller(this_device),
         this_device,
         SCI_SUCCESS
      );
   }

   scic_sds_controller_remote_device_stopped(
      scic_sds_remote_device_get_controller(this_device),
      this_device
   );
}

/**
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_STARTING it 
 * sets the starting state handlers, sets the device not ready, and posts the 
 * remote node context to the hardware. 
 * 
 * @param[in] object This is the SCI_BASE_OBJECT that is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_starting_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T    * the_controller;
   SCIC_SDS_REMOTE_DEVICE_T * this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   the_controller = scic_sds_remote_device_get_controller(this_device);

   SET_STATE_HANDLER(
      this_device, 
      scic_sds_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_STARTING
   );

   scic_cb_remote_device_not_ready(
      the_controller, 
      this_device,
      SCIC_REMOTE_DEVICE_NOT_READY_START_REQUESTED
   );
}

/**
 * This is the exit method for the SCI_BASE_REMOTE_DEVICE_STATE_STARTING it 
 * reports that the device start is complete.
 * 
 * @param[in] object This is the SCI_BASE_OBJECT that is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_starting_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   /// @todo Check the device object for the proper return code for this
   ///       callback
   scic_cb_remote_device_start_complete(
      scic_sds_remote_device_get_controller(this_device),
      this_device,
      SCI_SUCCESS
   );

   scic_sds_controller_remote_device_started(
      scic_sds_remote_device_get_controller(this_device),
      this_device
   );
}

/**
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_READY it sets
 * the ready state handlers, and starts the ready substate machine. 
 * 
 * @param[in] object This is the SCI_BASE_OBJECT that is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_ready_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T    * the_controller;
   SCIC_SDS_REMOTE_DEVICE_T * this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   the_controller = scic_sds_remote_device_get_controller(this_device);

   SET_STATE_HANDLER(
      this_device, 
      scic_sds_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_READY
   );

   the_controller->remote_device_sequence[this_device->rnc->remote_node_index]++;

   if (this_device->has_ready_substate_machine)
   {
      sci_base_state_machine_start(&this_device->ready_substate_machine);
   }
   else
   {
      scic_cb_remote_device_ready(the_controller, this_device);
   }
}

/**
 * This is the exit method for the SCI_BASE_REMOTE_DEVICE_STATE_READY it does 
 * nothing. 
 * 
 * @param[in] object This is the SCI_BASE_OBJECT that is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_ready_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T    * the_controller;
   SCIC_SDS_REMOTE_DEVICE_T * this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   the_controller = scic_sds_remote_device_get_controller(this_device);

   if (this_device->has_ready_substate_machine)
   {
      sci_base_state_machine_stop(&this_device->ready_substate_machine);
   }
   else
   {
      scic_cb_remote_device_not_ready(
         the_controller, 
         this_device,
         SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED
      );
   }
}

/**
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_STOPPING it 
 * sets the stopping state handlers and posts an RNC invalidate request to the 
 * SCU hardware. 
 * 
 * @param[in] object This is the SCI_BASE_OBJECT that is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_stopping_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      this_device, 
      scic_sds_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
   );
}

/**
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_FAILED it 
 * sets the stopping state handlers. 
 * 
 * @param[in] object This is the SCI_BASE_OBJECT that is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_failed_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      this_device, 
      scic_sds_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_FAILED
   );
}

/**
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_RESETTING it 
 * sets the resetting state handlers. 
 * 
 * @param[in] object This is the SCI_BASE_OBJECT that is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_resetting_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      this_device, 
      scic_sds_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_RESETTING
   );

   scic_sds_remote_node_context_suspend(
      this_device->rnc, SCI_SOFTWARE_SUSPENSION, NULL, NULL);
}

/**
 * This is the exit method for the SCI_BASE_REMOTE_DEVICE_STATE_RESETTING it 
 * does nothing. 
 * 
 * @param[in] object This is the SCI_BASE_OBJECT that is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_resetting_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   scic_sds_remote_node_context_resume(this_device->rnc, NULL, NULL);
}

/**
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_FINAL it sets
 * the final state handlers. 
 * 
 * @param[in] object This is the SCI_BASE_OBJECT that is cast into a 
 *       SCIC_SDS_REMOTE_DEVICE.
 *  
 * @return none 
 */
PLACEMENT_HINTS((TBD))
void scic_sds_remote_device_final_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      this_device, 
      scic_sds_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_FINAL
   );
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_BASE_STATE_T
   scic_sds_remote_device_state_table[SCI_BASE_REMOTE_DEVICE_MAX_STATES] =
{
   {
      SCI_BASE_REMOTE_DEVICE_STATE_INITIAL,
      scic_sds_remote_device_initial_state_enter,
      NULL
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPED,
      scic_sds_remote_device_stopped_state_enter,
      NULL
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_STARTING,
      scic_sds_remote_device_starting_state_enter,
      scic_sds_remote_device_starting_state_exit
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_READY,
      scic_sds_remote_device_ready_state_enter,
      scic_sds_remote_device_ready_state_exit
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPING,
      scic_sds_remote_device_stopping_state_enter,
      NULL
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_FAILED,
      scic_sds_remote_device_failed_state_enter,
      NULL
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_RESETTING,
      scic_sds_remote_device_resetting_state_enter,
      scic_sds_remote_device_resetting_state_exit
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_FINAL,
      scic_sds_remote_device_final_state_enter,
      NULL
   }
};


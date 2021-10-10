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
 * @brief This file contains the methods for the SCIF_SAS_SMP_REMMOTE object's
 *           clear affiliation activity.
 */
#include "sci_controller.h"
#include "scif_sas_controller.h"
#include "scif_sas_remote_device.h"
#include "scif_sas_logger.h"

#include "scif_sas_smp_remote_device.h"
#include "scif_sas_smp_io_request.h"
#include "intel_sas.h"
#include "scic_io_request.h"
#include "scic_remote_device.h"
#include "scif_sas_smp_phy.h"

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method finds the next smp phy (from the anchor_phy) that link to
 *           a SATA end device.
 * 
 * @param[in] fw_device the framework SMP device that is clearing affiliation for
 *               its remote SATA devices' 
 *
 * @return SCIF_SAS_SMP_PHY_T a smp phy, to which clear affiliation phy control command
 *            is to be sent.
 */ 
SCIF_SAS_SMP_PHY_T * scif_sas_smp_remote_device_find_next_smp_phy_link_to_sata(
   SCIF_SAS_SMP_PHY_T * anchor_phy
)
{
   SCI_FAST_LIST_ELEMENT_T  * element = &anchor_phy->list_element;
   SCIF_SAS_SMP_PHY_T * curr_smp_phy = NULL; 
  
   while (element != NULL)
   {
      curr_smp_phy = (SCIF_SAS_SMP_PHY_T*) sci_fast_list_get_object(element);
      element = sci_fast_list_get_next(element);

      if (curr_smp_phy->attached_device_type == SMP_END_DEVICE_ONLY 
          && curr_smp_phy->u.end_device != NULL)
      {  
         SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;
         scic_remote_device_get_protocols(
            curr_smp_phy->u.end_device->core_object, &dev_protocols);

         if (dev_protocols.u.bits.attached_stp_target)
            return curr_smp_phy;
      }
   }

   return NULL;
}

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

/**
 * @brief This method starts the clear affiliation activity for a 
 *           smp device's remote SATA devices. 
 * 
 * @param[in] fw_device the framework SMP device that is clearing affiliation for
 *               its remote SATA devices.
 *
 * @return none
 */ 
PLACEMENT_HINTS((SHUT_DOWN))
void scif_sas_smp_remote_device_start_clear_affiliation(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_SMP_REMOTE_DEVICE_T * smp_device = 
      &fw_device->protocol_device.smp_device;

   SCIF_SAS_SMP_PHY_T * phy_to_clear_affiliation = 
      scif_sas_smp_remote_device_find_next_smp_phy_link_to_sata(
         (SCIF_SAS_SMP_PHY_T *)smp_device->smp_phy_list.list_head->object);

   if (phy_to_clear_affiliation != NULL)
   {
      smp_device->curr_clear_affiliation_phy = phy_to_clear_affiliation;

      //set current activity
      fw_device->protocol_device.smp_device.current_activity = 
         SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CLEAR_AFFILIATION;

      //Set current_smp_request to PHY CONTROL.
      fw_device->protocol_device.smp_device.current_smp_request = 
         SMP_FUNCTION_PHY_CONTROL;

      //reset discover_to_start flag.
      fw_device->protocol_device.smp_device.scheduled_activity = 
         SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_NONE;

      //build PHY Control (clear affiliation) to the phy.
      scif_sas_smp_request_construct_phy_control(
         fw_device->domain->controller,
         fw_device,
         PHY_OPERATION_CLEAR_AFFILIATION,
         phy_to_clear_affiliation->phy_identifier,
         NULL,
         NULL
      );

      //issue DPC to start this request. 
      scif_cb_start_internal_io_task_schedule(
         fw_device->domain->controller,
         (FUNCPTR)scif_sas_controller_start_high_priority_io, 
         fw_device->domain->controller
      );
   }
   else
      scif_sas_smp_remote_device_finish_clear_affiliation(fw_device);
}


/**
 * @brief This method continues the clear affiliation activity for a 
 *           smp device's remote SATA devices. 
 * 
 * @param[in] fw_device the framework SMP device that is clearing affiliation for
 *               its remote SATA devices.
 *
 * @return none
 */ 
PLACEMENT_HINTS((SHUT_DOWN))
void scif_sas_smp_remote_device_continue_clear_affiliation(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_SMP_REMOTE_DEVICE_T * smp_device = 
      &fw_device->protocol_device.smp_device;

   //search from next immediate smp phy.
   SCIF_SAS_SMP_PHY_T * phy_to_clear_affiliation = 
      scif_sas_smp_remote_device_find_next_smp_phy_link_to_sata(
         smp_device->curr_clear_affiliation_phy->list_element.next->object);

   if (phy_to_clear_affiliation != NULL)
   {
      smp_device->curr_clear_affiliation_phy = phy_to_clear_affiliation;

      //build PHY Control (clear affiliation) to the phy.
      scif_sas_smp_request_construct_phy_control(
         fw_device->domain->controller,
         fw_device,
         PHY_OPERATION_CLEAR_AFFILIATION,
         phy_to_clear_affiliation->phy_identifier,
         NULL,
         NULL
      );
   }
   else
      scif_sas_smp_remote_device_finish_clear_affiliation(fw_device);
}


/**
 * @brief This method finishes the clear affiliation activity for a 
 *           smp device's remote SATA devices. It then notify the domain it fihishes
 *           the clear affiliation activity. 
 * 
 * @param[in] fw_device the framework SMP device that is clearing affiliation for
 *               its remote SATA devices.
 *
 * @return none
 */ 
void scif_sas_smp_remote_device_finish_clear_affiliation(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = fw_device->domain;

   scif_sas_smp_remote_device_clear(fw_device);

   //let domain continue to clear affiliation on other smp devices.
   scif_sas_domain_continue_clear_affiliation(fw_domain);
}



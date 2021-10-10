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
* @brief This file contains the implementation of the SGPIO register inteface
*        methods.
*/

#include "scic_sgpio.h"
#include "scic_sds_controller_registers.h"
#include "scic_user_callback.h"


void scic_sgpio_set_vendor_code(
   SCI_CONTROLLER_HANDLE_T controller,
   U8 vendor_specific_sequence
)
{
   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;
   
   scu_sgpio_peg0_register_write(
      core_controller, vendor_specific_code, vendor_specific_sequence);
}

void scic_sgpio_set_blink_patterns(
   SCI_CONTROLLER_HANDLE_T controller,
   U8 pattern_a_low,
   U8 pattern_a_high,
   U8 pattern_b_low,
   U8 pattern_b_high
)
{
   U32 value;
   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;
   
   value = (pattern_b_high << 12) + (pattern_b_low << 8) + (pattern_a_high << 4) + pattern_a_low;
   
   scu_sgpio_peg0_register_write(
      core_controller, blink_rate, value);   
}

void scic_sgpio_set_functionality(
   SCI_CONTROLLER_HANDLE_T controller,
   BOOL sgpio_mode
)
{
   U32 value = DISABLE_SGPIO_FUNCTIONALITY;
   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;
   
   if(sgpio_mode)
   {
      value = ENABLE_SGPIO_FUNCTIONALITY;
   }
   
   scu_sgpio_peg0_register_write(
      core_controller, interface_control, value);
}

void scic_sgpio_set_led_state(
   SCI_CONTROLLER_HANDLE_T controller,
   SCI_PORT_HANDLE_T port_handle,
   BOOL error,
   BOOL locate,
   BOOL activity
)
{
   U32 phy_mask;
   U32 output_value;
   U32 sgpio_mode;
   
   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;
   SCIC_SDS_PORT_T * port = (SCIC_SDS_PORT_T *) port_handle;
      
   phy_mask = scic_sds_port_get_phys(port);
      
   output_value = 0x00000000;    
   
   if(!error)
   {  //turn off error LED
      output_value += 0x00000400;     
   }
   if(!locate)
   {  //turn off locate LED
      output_value += 0x00000040;      
   }
   if(!activity)
   {  //turn off activity LED
      output_value += 0x00000004;      
   }
      
   //If phy 0
   if(phy_mask & PHY_0_MASK)
   {    
      sgpio_mode = scu_sgpio_peg0_register_read(core_controller, output_data_select[0]);
      
      if(sgpio_mode != SGPIO_HARDWARE_CONTROL)
      {
         //send led ioctl to bay 0 or bay 4
         scu_sgpio_peg0_register_write(
            core_controller, output_data_select[0], output_value);
      }      
   }
   //If phy 1
   if(phy_mask & PHY_1_MASK)
   {
      sgpio_mode = scu_sgpio_peg0_register_read(core_controller, output_data_select[1]);
      
      if(sgpio_mode != SGPIO_HARDWARE_CONTROL)
      {
         //send led ioctl to bay 1 or bay 5
         scu_sgpio_peg0_register_write(
            core_controller, output_data_select[1], output_value);
      } 
   }
   //If phy 2
   if(phy_mask & PHY_2_MASK)
   {
      sgpio_mode = scu_sgpio_peg0_register_read(core_controller, output_data_select[2]);
      
      if(sgpio_mode != SGPIO_HARDWARE_CONTROL)
      {
         //send led ioctl to bay 2 or bay 6
         scu_sgpio_peg0_register_write(
            core_controller, output_data_select[2], output_value);
      } 
   }
   //If phy 3
   if(phy_mask & PHY_3_MASK)
   {
      sgpio_mode = scu_sgpio_peg0_register_read(core_controller, output_data_select[3]);
      
      if(sgpio_mode != SGPIO_HARDWARE_CONTROL)
      {
         //send led ioctl to bay 3 or bay 7
         scu_sgpio_peg0_register_write(
            core_controller, output_data_select[3], output_value);
      } 
   }
}

void scic_sgpio_set_to_hardware_control(
   SCI_CONTROLLER_HANDLE_T controller,
   BOOL is_hardware_controlled
)
{
   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;
   U8 i;        
   U32 output_value;  

   //turn on hardware control for LED's
   if(is_hardware_controlled)
   {
      output_value = SGPIO_HARDWARE_CONTROL;        
   }
   else //turn off hardware control
   {
      output_value = SGPIO_SOFTWARE_CONTROL;      
   }   
   
   for(i = 0; i < SCI_MAX_PHYS; i++)
   {
      scu_sgpio_peg0_register_write(
         core_controller, output_data_select[i], output_value);
   }   
}

U32 scic_sgpio_read(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;
   U32 data_in = 0;
   
   data_in = scu_sgpio_peg0_register_read(core_controller, serial_input_lower);  
      
   return data_in;
}

void scic_sgpio_initialize(
   SCI_CONTROLLER_HANDLE_T controller
)
{ 
   scic_sgpio_set_functionality(controller, TRUE);
   scic_sgpio_set_to_hardware_control(controller, TRUE);
   scic_sgpio_set_vendor_code(controller, 0x00); 
}

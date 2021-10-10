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
#ifndef _SCIC_SDS_PORT_REGISTERS_H_
#define _SCIC_SDS_PORT_REGISTERS_H_

/**
 * @file
 *  
 * @brief This file contains a set of macros that assist in reading the SCU 
 * hardware registers. 
 */

/**
 * Macro to read the port task scheduler register associated with this port
 * object
 */
#define scu_port_task_scheduler_read(port, reg) \
   scu_register_read( \
      scic_sds_port_get_controller(port), \
      (port)->port_task_scheduler_registers->reg \
   )

/**
 * Macro to write the port task scheduler register associated with this
 * port object
 */
#define scu_port_task_scheduler_write(port, reg, value) \
   scu_register_write( \
      scic_sds_port_get_controller(port), \
      (port)->port_task_scheduler_registers->reg, \
      (value) \
   )

#define scu_port_viit_register_write(port, reg, value) \
   scu_register_write( \
      scic_sds_port_get_controller(port), \
      (port)->viit_registers->reg, \
      (value) \
   )

//****************************************************************************
//* Port Task Scheduler registers controlled by the port object
//****************************************************************************

/**
 * Macro to read the port task scheduler control register
 */
#define SCU_PTSxCR_READ(port) \
   scu_port_task_scheduler_read(port, control)

/**
 * Macro to write the port task scheduler control regsister
 */
#define SCU_PTSxCR_WRITE(port, value) \
   scu_port_task_scheduler_write(port, control, value)

//****************************************************************************
//* Port PE Configuration registers
//****************************************************************************

/**
 * Macro to write the PE Port Configuration Register
 */
#define SCU_PCSPExCR_WRITE(port, phy_id, value) \
   scu_register_write( \
      scic_sds_port_get_controller(port), \
      (port)->port_pe_configuration_register[phy_id], \
      (value) \
   )

/**
 * Macro to read the PE Port Configuration Regsiter
 */
#define SCU_PCSPExCR_READ(port, phy_id) \
   scu_register_read( \
      scic_sds_port_get_controller(port), \
      (port)->port_pe_configuration_register[phy_id] \
   )

#endif // _SCIC_SDS_PORT_REGISTERS_H_

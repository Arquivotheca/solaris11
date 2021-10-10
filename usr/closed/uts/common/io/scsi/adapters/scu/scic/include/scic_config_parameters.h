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
#ifndef _SCIC_SDS_USER_PARAMETERS_H_
#define _SCIC_SDS_USER_PARAMETERS_H_

/**
 * @file
 *
 * @brief This file contains all of the structure definitions and interface
 *        methods that can be called by a SCIC user on the SCU Driver
 *        Standard (SCIC_SDS_USER_PARAMETERS_T) user parameter block.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "sci_types.h"
#include "sci_status.h"
#include "intel_sas.h"
#include "sci_controller_constants.h"
#include "scu_bios_definitions.h"

/**
 * @name SCIC_SDS_PARM_PHY_SPEED
 *
 * These constants define the speeds utilized for a phy/port.
 */
/*@{*/
#define SCIC_SDS_PARM_NO_SPEED   0

/**
 * This value of 1 indicates generation 1 (i.e. 1.5 Gb/s).
 */
#define SCIC_SDS_PARM_GEN1_SPEED 1

/**
 * This value of 2 indicates generation 2 (i.e. 3.0 Gb/s).
 */
#define SCIC_SDS_PARM_GEN2_SPEED 2

/**
 * This value of 3 indicates generation 3 (i.e. 6.0 Gb/s).
 */
#define SCIC_SDS_PARM_GEN3_SPEED 3

/**
 * For range checks, the max speed generation
 */
#define SCIC_SDS_PARM_MAX_SPEED SCIC_SDS_PARM_GEN3_SPEED
/*@}*/

/**
 * @struct SCIC_SDS_USER_PARAMETERS
 *
 * @brief This structure delineates the various user parameters that can be
 *        changed by the core user.
 */
typedef struct SCIC_SDS_USER_PARAMETERS
{
   struct
   {
      /**
       * This field specifies the NOTIFY (ENABLE SPIN UP) primitive
       * insertion frequency for this phy index.
       */
      U32  notify_enable_spin_up_insertion_frequency;

      /**
       * This method specifies the number of transmitted DWORDs within which
       * to transmit a single ALIGN primitive.  This value applies regardless
       * of what type of device is attached or connection state.  A value of
       * 0 indicates that no ALIGN primitives will be inserted.
       */
      U16  align_insertion_frequency;

      /**
       * This method specifies the number of transmitted DWORDs within which
       * to transmit 2 ALIGN primitives.  This applies for SAS connections
       * only.  A minimum value of 3 is required for this field.
       */
      U16  in_connection_align_insertion_frequency;

      /**
       * This field indicates the maximum speed generation to be utilized
       * by phys in the supplied port.
       * - A value of 1 indicates generation 1 (i.e. 1.5 Gb/s).
       * - A value of 2 indicates generation 2 (i.e. 3.0 Gb/s).
       * - A value of 3 indicates generation 3 (i.e. 6.0 Gb/s).
       */
      U8 max_speed_generation;

   } phys[SCI_MAX_PHYS];


   /**
    * This field specifies the number of seconds to allow a phy to consume
    * power before yielding to another phy.
    *
    */
   U8  phy_spin_up_delay_interval;

   /**
   * These timer values specifies how long a link will remain open with no
   * activity in increments of a microsecond, it can be in increments of
   * 100 microseconds if the upper most bit is set.
   *
   */
   U16 stp_inactivity_timeout;
   U16 ssp_inactivity_timeout;

   /**
   * These timer values specifies how long a link will remain open in increments
   * of 100 microseconds.
   *
   */
   U16 stp_max_occupancy_timeout;
   U16 ssp_max_occupancy_timeout;

   /**
   * This timer value specifies how long a link will remain open with no
   * outbound traffic in increments of a microsecond.
   *
   */
   U8 no_outbound_task_timeout;

} SCIC_SDS_USER_PARAMETERS_T;

/**
 * @union SCIC_USER_PARAMETERS
 * @brief This structure/union specifies the various different user
 *        parameter sets available.  Each type is specific to a hardware
 *        controller version.
 */
typedef union SCIC_USER_PARAMETERS
{
   /**
    * This field specifies the user parameters specific to the
    * Storage Controller Unit (SCU) Driver Standard (SDS) version
    * 1.
    */
   SCIC_SDS_USER_PARAMETERS_T sds1;

} SCIC_USER_PARAMETERS_T;


/**
 * @name SCIC_SDS_OEM_PHY_MASK
 *
 * These constants define the valid values for phy_mask
 */
/*@{*/

/**
 * This is the min value assignable to a port's phy mask
 */
#define SCIC_SDS_PARM_PHY_MASK_MIN 0x0

/**
 * This is the max value assignable to a port's phy mask
 */
#define SCIC_SDS_PARM_PHY_MASK_MAX 0xF
/*@}*/

typedef SCI_BIOS_OEM_PARAM_ELEMENT_T SCIC_SDS_OEM_PARAMETERS_T;

#define MAX_CONCURRENT_DEVICE_SPIN_UP_COUNT 4
/**
 * @union SCIC_OEM_PARAMETERS
 *
 * @brief This structure/union specifies the various different OEM
 *        parameter sets available.  Each type is specific to a hardware
 *        controller version.
 */
typedef union SCIC_OEM_PARAMETERS
{
   /**
    * This field specifies the OEM parameters specific to the
    * Storage Controller Unit (SCU) Driver Standard (SDS) version
    * 1.
    */
   SCIC_SDS_OEM_PARAMETERS_T sds1;

} SCIC_OEM_PARAMETERS_T;

/**
 * @brief This method allows the user to attempt to change the user
 *        parameters utilized by the controller.
 *
 * @param[in] controller This parameter specifies the controller on which
 *            to set the user parameters.
 * @param[in] user_parameters This parameter specifies the USER_PARAMETERS
 *            object containing the potential new values.
 *
 * @return Indicate if the update of the user parameters was successful.
 * @retval SCI_SUCCESS This value is returned if the operation succeeded.
 * @retval SCI_FAILURE_INVALID_STATE This value is returned if the attempt
 *         to change the user parameter failed, because changing one of
 *         the parameters is not currently allowed.
 * @retval SCI_FAILURE_INVALID_PARAMETER_VALUE This value is returned if the
 *         user supplied an invalid interrupt coalescence time, spin up
 *         delay interval, etc.
 */
SCI_STATUS scic_user_parameters_set(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCIC_USER_PARAMETERS_T  * user_parameters
);

/**
 * @brief This method allows the user to retrieve the user parameters
 *        utilized by the controller.
 *
 * @param[in] controller This parameter specifies the controller on which
 *            to set the user parameters.
 * @param[in] user_parameters This parameter specifies the USER_PARAMETERS
 *            object into which the framework shall save it's parameters.
 *
 * @return none
 */
void scic_user_parameters_get(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCIC_USER_PARAMETERS_T  * user_parameters
);

/**
 * @brief This method allows the user to attempt to change the OEM
 *        parameters utilized by the controller.
 *
 * @param[in] controller This parameter specifies the controller on which
 *            to set the user parameters.
 * @param[in] oem_parameters This parameter specifies the OEM parameters
 *            object containing the potential new values.
 *
 * @return Indicate if the update of the user parameters was successful.
 * @retval SCI_SUCCESS This value is returned if the operation succeeded.
 * @retval SCI_FAILURE_INVALID_STATE This value is returned if the attempt
 *         to change the user parameter failed, because changing one of
 *         the parameters is not currently allowed.
 * @retval SCI_FAILURE_INVALID_PARAMETER_VALUE This value is returned if the
 *         user supplied an unsupported value for one of the OEM parameters.
 */
SCI_STATUS scic_oem_parameters_set(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCIC_OEM_PARAMETERS_T   * oem_parameters
);

/**
 * @brief This method allows the user to retreive the OEM
 *        parameters utilized by the controller.
 *
 * @param[in]  controller This parameter specifies the controller on which
 *             to set the user parameters.
 * @param[out] oem_parameters This parameter specifies the OEM parameters
 *             object in which to write the core's OEM parameters.
 *
 * @return none
 */
void scic_oem_parameters_get(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCIC_OEM_PARAMETERS_T   * oem_parameters
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_USER_PARAMETERS_H_


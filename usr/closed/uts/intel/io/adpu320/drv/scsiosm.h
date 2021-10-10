/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*$Header: Z:\u320chim\src\chim\chimhdr\scsiosm.h_t   \main\90   Sat Nov 23 13:21:21 2002   mat22316 $*/
/*****************************************************************************
*                                                                            *
* Copyright 1995,1996,1997,1998,1999,2000,2001,2002 Adaptec, Inc.,           *
* All Rights Reserved.                                                       *
*                                                                            *
* This software contains the valuable trade secrets of Adaptec.  The         *
* software is protected under copyright laws as an unpublished work of       *
* Adaptec.  Notice is for informational purposes only and does not imply     *
* publication.  The user of this software may make copies of the software    *
* for use with parts manufactured by Adaptec or under license from Adaptec   *
* and for no other use.                                                      *
*                                                                            *
******************************************************************************/

#ifndef _SCSIOSM_H
#define _SCSIOSM_H

/*****************************************************************************
*
*  Module Name:   SCSIOSM.H
*
*  Description:   SCSI implementation specific customization
*
*  Owners:        ECX IC Firmware Team
*
******************************************************************************/

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/* Customization unique to implementation                                    */
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

/*----------------------------------------------------------------------------

 The table below show supported combinations of SCSI hardware & firmware:

                       AICU320 (Rev A) AICU320 (Rev B) 
                       --------------  ---------------
STANDARD_U320                X               X
STANDARD_ENHANCED_U320                       X

 For example, if we'll like to enable support for AICU320 (Rev A) only then use
   #define SCSI_AICU320                1
   #define SCSI_STANDARD_U320_MODE     1
 The rest of AIC & firmware options can be set to 0 to reduce code size.

 ----------------------------------------------------------------------------*/

/* SCSI hardware supported --------------------------------------------------*/
/*   This section can be customized to a specific AIC if desired. Code size  */
/*   expands according to numbers of enabled AIC support.                    */

#define SCSI_AICU320                1        /* Support U320 type device     */
                                             /*   AIC-7902A4                 */
                                             /*   ASC-39320                  */
                                             /*   ASC-39320D                 */
                                             /*   ASC-29320                  */
                                             /*   ASC-29320B                 */
                                             /*   ASC-29320LP                */
                                             /*   AIC-7901A4                 */

/* Special SCSI hardware supported ------------------------------------------*/


/* Firmware mode supported --------------------------------------------------*/
/*   This section can be customized to a specific firmware if desired.       */
/*   Code size expands according to numbers of enabled firmware support.     */
#define SCSI_STANDARD_U320_MODE     1        /* Enable standard mode         */
                                             /*  for SCSI_AICU320            */
#define SCSI_STANDARD_ENHANCED_U320_MODE  1  /* Enable standard enhanced     */
                                             /* mode for SCSI_AICU320        */

/* For DCH SCSI Core Only                       -----------------------------*/                                            

#define SCSI_DCH_U320_MODE          0       /* incorporate DCH_SCSI changes: */
                                            /* DCH HW, SCB, RBI & Common SGL */
/****************************************************************************/
/* Configuration for DCH support is as follows:                              */
/*       SCSI_STANDARD_U320_MODE  0                                          */
/*       SCSI_STANDARD_ENHANCED_U320_MODE  0                                 */
/*       SCSI_FAST_PAC  1                                                    */
/*       SCSI_AUTO_TERMINATION  0                                            */
/*       SCSI_UPDATE_TERMINATION_ENABLE 0                                    */
/*       SCSI_PCI_COMMAND_REG_CHECK  0                                       */
/*       SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING  1                             */
/*       SCSI_SEEPROM_ACCESS 0 NOTE:  This will change as the requirements   */
/*                                    for SEEProm support are needed.        */ 
/****************************************************************************/

/* SCSI hardware operation mode supported -----------------------------------*/
#define SCSI_INITIATOR_OPERATION    1        /* Initiator operation mode     */
#define SCSI_TARGET_OPERATION       0        /* Target operation mode        */

/* Domain validation method supported ---------------------------------------*/
#define SCSI_DOMAIN_VALIDATION_BASIC    1    /*Enable Basic Domain Validation*/
#define SCSI_DOMAIN_VALIDATION_ENHANCED 1    /*Enable Enhanced Domain Validation*/
#define SCSI_DV_MULTIINITIATOR          0    /* Force multi-initiator compliance, 
                                              * when used with one of the modes.
                                              * When Mode 0 is used, this
                                              * switch must be set to '0'.
                                              * When 1 EBOS bit in REBD checked.
                                              */

#define SCSI_DV_MULTI_INITIATOR_MODE    1    /* multi-initiator support mode 
                                               may assume the following 
                                               values: */

/*****************************************************************************/
/* SCSI_DV_STANDARD_MODE          0 - SM DV, no header or linking (STANDARD) */
/* SCSI_DV_TARGET_COLLISION_MODE  1 - SM DV, no header or linking (STANDARD) */
/* SCSI_DV_LOOSE_HEADER_MODE      2 - SM DV, with loose header               */
/* SCSI_DV_STRICT_HEADER_MODE     3 - SM DV, with strict header              */
/*****************************************************************************/

#define  SCSI_DV_TIMEOUT_VALUE    0x40       /*DV Timer data phase timeout   */
                                             /*initialize value.             */ 
                                             /*40h = appx. 30ms              */
#define SCSI_DV_REPORT_ALL_OSMEVENTS    0    /* When set to 1, the HIM will
                                              * report all OSMEvents that occur 
                                              * during Domain Validation.
                                              * When set to 0, the HIM will
                                              * suppress OSMEvents that do not
                                              * result in Domain Validation
                                              * terminating (currently, only
                                              * HIM_EVENT_HA_FAILED).
                                              * When this compile option is set
                                              * to 1 the OSM shall not perform
                                              * any of the required Event Handling
                                              * steps described in the CHIM
                                              * specification until the I/O
                                              * request associated with the 
                                              * Domain Validation process has
                                              * completed. However, if 
                                              * SCSI_FAST_PAC is set to 1 the
                                              * OSM may issue the HIM_RESET_ADAPTER
                                              * and PAC IOBs while the DV I/O 
                                              * request is active. This is useful
                                              * to quickly re-enable target mode
                                              * operation.
                                              */
/* Optimization options -----------------------------------------------------*/
#define SCSI_STREAMLINE_QIOPATH     1        /* Streamline the normal qiob & */
                                             /*  command complete path       */
#define SCSI_IO_SYNCHRONIZATION_GUARANTEED 0 /* PIO's are guaranteed to be   */
                                             /*  in order and not posted     */
/* General options ----------------------------------------------------------*/
#define SCSI_FAST_PAC               0        /* Skip SCSI bus scan to search */
                                             /*  for new targets during PAC  */
#define SCSI_PPR_ENABLE             1        /* Parallel Protocol Request    */
                                             /* Enable support of Parallel 
                                              *  Protocol Request message. 
                                              *  When set to 1 the CHIM will 
                                              *  issue and respond to Parallel 
                                              *  Protocol Request message 
                                              *  during negotiation if the 
                                              *  device  supports this 
                                              *  message. This option need 
                                              *  only be set if using 
                                              *  SCSI_AICU320 hardware.
                                              */
#define SCSI_PACKETIZED_IO_SUPPORT  1        /* Enable all the logic to       */
                                             /* support packetized operation  */

#define SCSI_MAXIMUM_SE_SPEED_10    0        /* Maximum SE speed is 10MHZ     */
#define SCSI_AUTO_TERMINATION       1        /* Auto termination en/disable   */
#define SCSI_UPDATE_TERMINATION_ENABLE  1    /* Update termination setting    */
#define SCSI_SEEPROM_ACCESS         1        /* Support SEEPROM access        */
#define SCSI_BIOS_ASPI8DOS          1        /* Access to scratch ram that    */
                                             /*  BIOS/ASPI8DOS have           */
#define SCSI_SCSISELECT_SUPPORT     1        /* SCSISelect information exist  */
#define SCSI_SAVE_RESTORE_STATE     1        /* Save/restore state            */


#define POWER_MANAGEMENT_SAVE_RESTORE  0     /* Save/restore for Power Mgmt   */
#define SCSI_PROFILE_INFORMATION    1        /* Support CHIM adapter and      */
                                             /*  target profiles              */
#define SCSI_DISABLE_ADJUST_TARGET_PROFILE 0 /* Disable code associated with  */
                                             /*  HIMAdjustTargetProfile       */

#define SCSI_BACKENDISR_OUTSIDE_INTRCONTEXT 0 /* Defer HIMBackEndISR call     */
                                              /* outside of interrupt context */
#define SCSI_PCI_COMMAND_REG_CHECK  1        /* Enable checking I/O, Memory   */
                                             /*  and Bus Master               */
#define SCSI_RESET_DELAY_DEFAULT    2000     /* The default value for adapter */
                                             /*  profile field  AP_ResetDelay */
                                             /*  in milliseconds when         */
                                             /*  AP_InitiatorMode is HIM_TRUE */
#define SCSI_CHANNEL_TIMEOUT_DEFAULT  15000  /* The default value for adapter */
                                             /*  profile field                */
                                             /*  AP_IOChannelFailureTimeout   */
                                             /*  in milliseconds.             */  
#define SCSI_REGISTER_TIMEOUT_DEFAULT 2000   /* The default value (in         */
                                             /* milliseconds) for deciding    */
                                             /* to time out an operation that */
                                             /* is waiting on a SCSI signal   */
                                             /* change or an ASIC register    */
                                             /* state change.                 */

#define SCSI_DV_REGISTER_TIMEOUT_COUNTER SCSI_REGISTER_TIMEOUT_DEFAULT
                                             /* The default value (in
                                              * milliseconds) for deciding to
                                              * to time out an operation that
                                              * is waiting on a SCSI signal
                                              * change or an ASIC register
                                              * state change in SCSIhDVTimeout()
                                              */


#define SCSI_DISABLE_INTERRUPTS     0        /* Does not enable the HA interrupt */
                                             /* when SCSIInitialize() is called. */
#define SCSI_DISABLE_PROBE_SUPPORT  1        /* Disable support of HIM_PROBE  */
                                             /*  IOB function                 */

#define SCSI_PROTOCOL_OPTION_MASK_ENABLE 1   /* Enable protocol option mask   */
                                             /* field for all PPR protocol    */
                                             /* options for profile parameters*/
                                             /* else use encoded              */
                                             /* SCSIDefaultProtocolOption     */
                                             /* for QAS, IU and DT.           */

#define SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING 0  
                                             /* Disables PCI or PCI-X error  */
                                             /* handling and reporting.      */

#define SCSI_CLEAR_FREEZE_ERROR_BUSY    0    /* Clear SCSI_FREEZE_ERRORBUSY in freeze map */
                                             /* when a BadSeq is exercised */
#define SCSI_INTERNAL_PARITY_CHECK  0        /* Data Parity Check en/disable */

#define SCSI_REPORT_OSMEVENT_INFO   0        /* When set to 1, the caller ID */
                                             /* of the code that caused an   */
                                             /* OSMEvent call when the event */
                                             /* is HA_FAILED or              */
                                             /* IO_CHANNEL_FAILED is returned*/
                                             /* as the fourth OSMEvent       */
                                             /* parameter,                   */
                                             /* HIM_UINT32 eventInfo.        */

/* Specific customer customization options ----------------------------------*/
#define SCSI_OEM1_SUPPORT           0        /* Support OEM1 customer        */
#define SCSI_NULL_SELECTION         0        /* When set to 1 the CHIM will 
                                              * issue a SCSI NULL Selection 
                                              * (peforming a selection using 
                                              * only the host adapter ID) 
                                              * during a bus scan (PAC). Also 
                                              * allows the scsiID field in the
                                              * transportSpecific area of the 
                                              * IOB to be set to the host 
                                              * adapter SCSI ID.  
                                              */
#define SCSI_CUSTOM_TERMINATION     0        /* Custom termination code for  */
                                             /*  an internal customer        */
#define SCSI_PAC_SEND_SSU           0        /* During PAC, send start/stop cmd
                                              * to drive, if it's not spin up.
                                              */
#define SCSI_TEST_OSMEVENTS         0        /* Support OSM interface to test
                                              * OSMEvent handling. The OSM 
                                              * requests the event by 
                                              * assigning the OSMEvent value
                                              * to the sortTag field of an
                                              * HIM_INITIATE_TASK IOB.
                                              * The HIM will perform event
                                              * handling logic during the
                                              * completion of the IOB.
                                              * The following OSMEvents are
                                              * supported:
                                              *   HIM_EVENT_IO_CHANNEL_RESET
                                              *   HIM_EVENT_HA_FAILED
                                              *   HIM_EVENT_IO_CHANNEL_FAILED
                                              * This option requires 
                                              * SCSI_STREAMLINE_QIOPATH to be
                                              * set to 1. 
                                              */

/* Target Mode Specific Options [SCSI_TARGET_OPERATION must be 1]------------*/
#define SCSI_ESTABLISH_CONNECTION_SCBS  0    /* This field only has meaning when
                                              * SCSI_TARGET_OPERATION = 1 and
                                              * target mode is enabled for an
                                              * adapter. This value represents
                                              * the default number of internal
                                              * I/O blocks(SCBS) reserved for
                                              * receiving new target mode
                                              * requests from initiators.
                                              * If this value is 0 an adapter
                                              * specific default will be used.
                                              */
#define SCSI_LOOPBACK_OPERATION     0        /* Support loopback operation   */
#define SCSI_ESS_SUPPORT            0        /* Support ESS design */

#define SCSI_ELEC_PROFILE           0        /* Support Profiles for setting */
                                             /* Slew Rate, Amplitude, Precomp*/
                                             /* and Write Bias Cancelation   */

/* RAID1 --------------------------------------------------------------------*/
#define	SCSI_IROC		1		/* Support Iroc HW	*/
#define SCSI_RAID1                  0        /* Support RAID 1 Sequencer     */

/* Internal use options. Do not modify --------------------------------------*/
                                                              
#define SCSI_RESOURCE_MANAGEMENT    1        /* Enable resource management    */

#define SCSI_DOWNSHIFT_U320_MODE    0        /* Enable/Disable downshift U320 */
                                             /* mode                          */

#define SCSI_DOWNSHIFT_ENHANCED_U320_MODE 0  /* Enable/Disable downshift      */
                                             /* enhanced U320 mode            */
                                                                  
#define SCSI_BIOS_SUPPORT           0        /* Enable/Disable BIOS environment */

#define SCSI_EFI_BIOS_SUPPORT       0        /* Enable/Disable EFI BIOS       */
                                             /* environment                   */
#define SCSI_ASPI_SUPPORT           0        /* Enable/Disable Aspi support   */
#define SCSI_ASPI_SUPPORT_GROUP1    0        /* Enable/Disable Aspi first grouping    */

#define SCSI_ENHANCED_U320_SNAPSHOT 0        /* Enable 54 word shadow snapshot*/
                                             /*  for paced streaming transfers*/
                                             
#define SCSI_ENHANCED_U320_SLOWCRC  1        /* Enable 4 word pre and post CRC*/
                                             /*  pause if paced transfer or   */  
                                             /*   enable CRC ACKS occur at 1/2*/
                                             /*    speed for transfer <=160MB */


#if (SCSI_BIOS_SUPPORT || SCSI_EFI_BIOS_SUPPORT)
#define SCSI_BIOS_MODE_SUPPORT      1
#else
#define SCSI_BIOS_MODE_SUPPORT      0
#endif

#define SCSI_SCBBFR_BUILTIN         1        /* Scb buffer built in hardware  */
                                             /*  management                   */

#define SCSI_TASK_SWITCH_SUPPORT    1        /* 0 - Enable pre-allocate       */
                                             /* reserved memory to save the   */
                                             /* sense data for non-auto sense */
                                             /* command in packetized mode    */ 
                                             /* and CHIM will check the sense */
                                             /* command to deliver the sense  */
                                             /* data to the OSM.              */ 
                                             /* 1 - Enable to support         */
                                             /* switching from packetized mode*/
                                             /* to non-packetized mode when   */
                                             /* non-auto sense command is     */
                                             /* detected                      */                                            
                                             /* 2 - Enable pre-allocate       */
                                             /* reserved memory to save the   */
                                             /* sense data for non-auto sense */
                                             /* command in packetized mode    */ 
                                             /* and Enable freeze on error    */
                                             /* support to deliver the sense  */
                                             /* data to the OSM.              */

#define SCSI_SEQBREAKPOINT_WORKAROUND  0     /* Enable/Disable the sequencer
                                              * workaround code that was seen
                                              * on the Trident
                                              */
                                                                                           
#define SCSI_SEQ_LOAD_USING_PIO     0        /* 1 - Enable loading of         */
                                             /*     sequencer using PIO       */
                                             /* 0 - Enable loading of         */
                                             /*     sequencer using the HOST  */
                                             /*     OVERLAY DMA feature       */

#define SCSI_CURRENT_SENSING        0        /* Enable/disable current sensing*/
                                             /* for termination logic.        */
                                             /* Valid for specific HA only.   */

#define SCSI_SIMULATION_SUPPORT     0        /* Enable/Disable CHIM compile   */
                                             /* for simulation environment.   */

#define SCSI_CRC_NOTIFICATION       0        /* Differentiate parity from CRC */
                                             /* in iob->residual field        */

#define SCSI_MULTIPLEID_SUPPORT     0        /* Support Multiple IDs for target
                                              *  mode
                                              */ 
#define SCSI_TRIDENT_WORKAROUND     0        /* U160 ASIC Target Mode workarounds
                                              * Should be 0 for now.
                                              */
#define SCSI_PARITY_PER_IOB         0        /* Enable/disable parity per iob */
#define SCSI_SELTO_PER_IOB          0        /* allow the OSM to define a SELTO */
                                             /* period on a per-IOB basis       */
#define SCSI_NEGOTIATION_PER_IOB    0        /* Enable/disable controling     */
                                             /*  negotiation through IOB      */

#define SCSI_DATA_IN_RETRY_DETECTION  0      /* When set to 1 the CHIM will
                                              * return an error for data-in
                                              * (read) requests if a target
                                              * retry is detected by the
                                              * sequencer. The sequencer
                                              * verifies that there are no
                                              * data-in disconnects without
                                              * a save data pointer message
                                              * (except the last transfer) and
                                              * also that no restore pointers
                                              * is issued during data-in. 
                                              * If target data-in retry is 
                                              * detected a HIM_IOB_PARITY_ERROR
                                              * is returned when the IOB 
                                              * completes. 
                                              * Note that; the I/O is not 
                                              * terminated early but is
                                              * allowed to continue through
                                              * status phase. 
                                              * Note that; currently
                                              * SCSI_TARGET_OPERATION must be 0
                                              * when using this option.
                                              */ 

#define SCSI_FORCE_PRECOMP            0      /* Force Precomp if not enabled */
                                             /* by target */
                                             


/* Hardware Simulation Only. Do not modify -----------------------------------*/
#if SCSI_SIMULATION_SUPPORT

/* Always undefine the option before attempting to assign a simulation value  */

/* SIM: General options ------------------------------------------------------*/
#ifdef SCSI_AUTO_TERMINATION
#undef SCSI_AUTO_TERMINATION
#define SCSI_AUTO_TERMINATION       0        /* Auto termination en/disable   */
#endif /* SCSI_AUTO_TERMINATION */

#ifdef SCSI_UPDATE_TERMINATION_ENABLE
#undef SCSI_UPDATE_TERMINATION_ENABLE
#define SCSI_UPDATE_TERMINATION_ENABLE  0    /* Update termination setting    */
#endif /* SCSI_UPDATE_TERMINATION_ENABLE */

#ifdef SCSI_SEEPROM_ACCESS
#undef SCSI_SEEPROM_ACCESS
#define SCSI_SEEPROM_ACCESS         0        /* Support SEEPROM access        */
#endif /* SCSI_SEEPROM_ACCESS */

#ifdef SCSI_BIOS_ASPI8DOS
#undef SCSI_BIOS_ASPI8DOS
#define SCSI_BIOS_ASPI8DOS          0        /* Access to scratch ram that    */
                                             /*  BIOS/ASPI8DOS have           */
#endif /* SCSI_BIOS_ASPI8DOS */

#ifdef SCSI_SCSISELECT_SUPPORT
#undef SCSI_SCSISELECT_SUPPORT
#define SCSI_SCSISELECT_SUPPORT     0        /* SCSISelect information exist  */
#endif /* SCSI_SCSISELECT_SUPPORT */

#ifdef SCSI_SAVE_RESTORE_STATE
#undef SCSI_SAVE_RESTORE_STATE
#define SCSI_SAVE_RESTORE_STATE     0        /* Save/restore state in         */
                                             /* SEEPROM.                      */
#endif /* SCSI_SAVE_RESTORE_STATE */

#ifdef SCSI_CHANNEL_TIMEOUT_DEFAULT
#undef SCSI_CHANNEL_TIMEOUT_DEFAULT
#define SCSI_CHANNEL_TIMEOUT_DEFAULT  1000   /* The default value for adapter */
                                             /*  profile field                */
                                             /*  AP_IOChannelFailureTimeout   */
                                             /*  in milliseconds.             */  
#endif /* SCSI_CHANNEL_TIMEOUT_DEFAULT */


#define SCSI_SIMULATION_MAXDEVICE_COUNT 2    /* This provides a device count  */
                                             /* for simulating an Initiator.  */  
                                             /* The default value assures at  */
                                             /* least 2 devices are tested.   */
                                             /* If a greater number is needed */
                                             /* it may be necessary to edit   */
                                             /* the *.vr stimulus test file.  */

#endif /* SCSI_SIMULATION_SUPPORT */

/* Configuration DV Option Validity ------------------------------------------*/

#if (SCSI_DV_MULTI_INITIATOR_MODE > 0) && (! (SCSI_DOMAIN_VALIDATION_BASIC \
                                            || SCSI_DOMAIN_VALIDATION_ENHANCED))
#error  At least Basic DV must be enabled to enable Multinitiator DV support
#endif

#if (SCSI_DV_MULTI_INITIATOR_MODE > 3)
#error SCSI_DV_MULTI_INITIATOR_MODE Unsupported value.
#endif

#if (SCSI_DCH_U320_MODE == 1) && ((SCSI_STANDARD_U320_MODE == 1) || \
                                  (SCSI_STANDARD_ENHANCED_U320_MODE == 1))
#error SCSI_DCH_U320_MODE enabled!! 
#endif

#if (SCSI_RAID1 == 1) && (SCSI_STREAMLINE_QIOPATH == 0)
#error RAID support only with SCSI_STREAMLINE_QIOPATH=1 
#endif

#endif /* _SCSIOSM_H */

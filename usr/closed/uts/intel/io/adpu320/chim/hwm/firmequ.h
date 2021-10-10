/*$Header: /vobs/u320chim/src/chim/hwm/firmequ.h   /main/131   Sun May 18 19:23:11 2003   spal3094 $*/

/***************************************************************************
*                                                                          *
* Copyright 1995-2003 Adaptec, Inc.  All Rights Reserved.                  *
*                                                                          *
* This software contains the valuable trade secrets of Adaptec or its      *
* licensors.  The software is protected under international copyright      *
* laws and treaties.  This software may only be used in accordance with    *
* terms of its accompanying license agreement.                             *
*                                                                          *
***************************************************************************/

/***************************************************************************
*
*  Module Name:   FIRMEQU.H
*
*  Description:   Definitions for firmware equates
*    
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         1. This file is referenced by hardware management layer
*                    only
*
***************************************************************************/

/* define selected modes */

#define SCSI_MULTI_MODE (SCSI_STANDARD_U320_MODE+\
                         SCSI_STANDARD_ENHANCED_U320_MODE) > 1


#define SCSI_MULTI_DOWNSHIFT_MODE (SCSI_DOWNSHIFT_U320_MODE+\
                         SCSI_DOWNSHIFT_ENHANCED_U320_MODE) > 1

#define SCSI_STANDARD_MODE    (SCSI_STANDARD_U320_MODE+\
                               SCSI_STANDARD_ENHANCED_U320_MODE+\
                               SCSI_DCH_U320_MODE)

#define SCSI_DOWNSHIFT_MODE   (SCSI_DOWNSHIFT_U320_MODE+\
                               SCSI_DOWNSHIFT_ENHANCED_U320_MODE)

#define SCSI_SWAPPING_MODE (0)

/****************************************************************************
* Version number
****************************************************************************/
#define  SCSI_SOFTWARE_VERSION   0

/****************************************************************************
* Miscellaneous
****************************************************************************/
#define  SCSI_NOT_APPLIED       -1             /* entry not applied       */
#define  SCSI_NULL_SCB          ((SCSI_UEXACT8) -1)   /* null scb pointer        */
#define  SCSI_SET_NULL_SCB(scb) ((scb) = 0xFF00)
#define  SCSI_IS_NULL_SCB(scb)  (((scb) & 0xFF00) == 0xFF00)
#define  SCSI_NULL_ENTRY        0xff           /* invalid entry indicator */

#define  SCSI_ENTRY_IDLE_LOOP0_BREAK   0x01    /* bit to set/clear idle loop0 break point */
#define  SCSI_ENTRY_EXPANDER_BREAK     0x02    /* bit to set/clear expander break point */

#define  SCSI_SEQLOADER_SIZE    20             /* Sequencer Loader code size     */

/****************************************************************************
* General usage of Scratch RAM for all firmware modes.
****************************************************************************/
#define SCSI_NEGO_ON_MODE_SWITCH         0x0148    /* 2-bytes bitmap,         */
#define SCSI_NEGO_ON_MODE_SWITCH1        0x0149    /* negotiate flag on mode  */
                                                   /* context switch for BIOS */
                                                   /* and Real mode driver    */

#if (SCSI_STANDARD_MODE && (!SCSI_DCH_U320_MODE))
/****************************************************************************
* Definitions for standard mode firmware
* This section must be checked whenever there is any sequencer code update
*
* These macros are shared by both standard and standard enhanced mode.
* May want to change the names to be SCSI_S_... to indicate they are common.  
****************************************************************************/
#define SCSI_SU320_SIZE_SCB              64
#define SCSI_SU320_ALIGNMENT_SCB         0x003F
#define SCSI_SU320_FIRMWARE_VERSION      0
#define SCSI_SU320_BTATABLE              0x01BF
#define SCSI_SU320_BTASIZE               256        /* Only supports 16 
                                                     * Luns currently.
                                                     */
#define SCSI_SU320_RESCNT_BASE           0x0188     /* Register address of
                                                     * residual in SCB.
                                                     */
#define SCSI_SU320_IDLE_LOOP_ENTRY       0x0000
#define SCSI_SU320_START_LINK_CMD_ENTRY  0x0004
#define SCSI_SU320_SIOSTR3_ENTRY         0x0008
#define SCSI_SU320_RETRY_IDENTIFY_ENTRY  0x0010
#define SCSI_SU320_SIO204_ENTRY          0x000C
#define SCSI_SU320_IDLE_LOOP             SCSI_NULL_ENTRY   /* Not used */
#if SCSI_TARGET_OPERATION
#define SCSI_SU320_IDLE_LOOP_TOP         0x0020
#else
#define SCSI_SU320_IDLE_LOOP_TOP         0x0018
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_TARGET_OPERATION   
/* Sequencer entry points specific to target mode */     
#define SCSI_SU320_TARGET_DATA_ENTRY     0x0014
#define SCSI_SU320_TARGET_HELD_BUS_ENTRY 0x0018
#endif /* SCSI_TARGET_OPERATION */

#define SCSI_SU320_RET_ADDR              0x00F8
#define SCSI_SU320_SG_STATUS             0x00FE
#define   SCSI_SU320_HAVE_SG_CACHE       0x10
#define SCSI_SU320_ACTIVE_SCB            0x00FC

#define SCSI_SU320_Q_NEW_POINTER         0x0100
#define SCSI_SU320_PASS_TO_DRIVER        0x0115
#define SCSI_SU320_Q_EXE_HEAD            0x011C
#define SCSI_SU320_Q_EXE_TAIL            0x011E 
#define SCSI_SU320_Q_EXETARG_TAIL        0x0120 
#define SCSI_SU320_Q_DONE_PASS           0x0114 
#define SCSI_SU320_Q_DONE_BASE           0x0108
#define SCSI_SU320_Q_DONE_POINTER        0x0110
#define SCSI_SU320_Q_DONE_HEAD           0x0140
#define SCSI_SU320_SEQ_OPTIONS           0x0158
/* SCSI_SU320_SEQ_OPTIONS bits definition */
#if (OSD_BUS_ADDRESS_SIZE == 64)
#define SCSI_SU320_SGSEGMENT_32          0x01      /* flag sequencer to check for */
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */
#define SCSI_SU320_MIRROR_RAID1          0x04      /* Enable mirror commands in seq */

#define SCSI_SU320_HARPOON2_REVB_ASIC    0x08      /* 0=Rev A, 1=Rev B Harpoon2 chip*/


#define SCSI_SU320_ENT_PT_BITMAP         SCSI_NULL_ENTRY            

#define SCSI_SU320_INTR_THRSH_TIMEOUT1   0x154
#define SCSI_SU320_INTR_THRSH_TIMEOUT2   0x155
#define SCSI_SU320_INTR_THRSH_MAXCNT1    0x156
#define SCSI_SU320_INTR_THRSH_MAXCNT2    0x157

#if SCSI_DATA_IN_RETRY_DETECTION
#define SCSI_SU320_DATA_IN_RETRY_DETECTION SCSI_NULL_ENTRY /* Report target data-in */ 
                                                     /* retry occurred. This    */
                                                     /* option checks for       */
                                                     /* missing save data       */
                                                     /* pointers on disconnect  */
                                                     /* restore pointers. This  */
                                                     /* is required by some XOR */
                                                     /* implementations that    */
                                                     /* read data into XOR      */
                                                     /* memory.                 */ 
#endif /* SCSI_DATA_IN_RETRY_DETECTION */

#if SCSI_TARGET_OPERATION
#define SCSI_SU320_DISCONNECT_DELAY      SCSI_NULL_ENTRY  /* Disconnect delay   */
#else
#define SCSI_SU320_DISCONNECT_DELAY      SCSI_NULL_ENTRY  /* Disconnect delay   */  
#endif /* SCSI_TARGET_OPERATION */

#define SCSI_SU320_ISR                   0x1000

#if !SCSI_SEQ_LOAD_USING_PIO
#define SCSI_SU320_LOAD_ADDR             0x176
#define SCSI_SU320_LOAD_COUNT            0x17e
#endif /* !SCSI_SEQ_LOAD_USING_PIO */

/* In the legacy CHIM, we can send only one non-tag command to multi-LUN */
/* target due to the scratch RAM limitation on Swapping mode design.     */
/* The busy target table used for non-tag command is allocated from      */
/* scratch RAM and it can be addressed up to 16 targets.                 */
/* In the U320 CHIM, the sequencer is running Standard mode.  With the   */
/* design of Standard mode that has the Execution queue, we can resolve  */
/* the one non-tag command to multi-LUN target issue by increasing the   */
/* maximum non-tag SCBs to be higher than one.  We will increase it to   */
/* 16.  Because the busy target table has been allocated from SCB array. */
/* And the busy target table size can support up to 16 different LUNs    */
/* with each LUN having one non-tag command running.                     */
#define SCSI_SU320_MAX_NONTAG_SCBS       16

#define SCSI_SU320_TARG_LUN_MASK0        SCSI_NULL_ENTRY   /* future for RAID */ 
#define SCSI_SU320_ARRAY_PARTITION0      SCSI_NULL_ENTRY   /* future for RAID */

#if SCSI_TARGET_OPERATION   
/* Scratch RAM entries specific to target mode */
#define SCSI_SU320_HESCB_QOFF            SCSI_NULL_ENTRY   /* host establish scb
                                                            * queue offset
                                                            */
#define SCSI_SU320_SESCB_QOFF            SCSI_NULL_ENTRY   /* sequencer establish scb queue
                                                            * offset
                                                            */
#define SCSI_SU320_EST_OPTIONS           0x160             /* Establish connection operating
                                                            * parameters.
                                                            */
/* EST_OPTIONS defines */
#define  SCSI_SU320_SCSI1_SEL_ENABLE  0x20  /* SCSI-1 Selection Enable
                                             * SCSI_HF_targetEnableScsi1Selection
                                             */
#define  SCSI_SU320_DISCON_ENABLE     0x10  /* Disconnect enable
                                             * SCSI_HF_targetDisconnectAllowed
                                             */ 
#define  SCSI_SU320_SCSI3_ENABLE      0x08  /* SCSI-3 enable
                                             * SCSI_HF_targetHostTargetVersion = 
                                             *
                                             */
#define  SCSI_SU320_TAG_ENABLE        0x04  /* Tagged commands enable
                                             * SCSI_HF_targetTagEnable
                                             */
#define  SCSI_SU320_CHK_RSVD_BITS     0x02  /* Check reserved bits in
                                             * identify message
                                             * SCSI_HF_targetScsi2IdentifyMsgRsv
                                             */ 
#define  SCSI_SU320_LUNTAR_ENABLE     0x01  /* LunTar bit in identify 
                                             * message enable
                                             * SCSI_HF_targetScsi2RejectLuntar
                                             */
#define SCSI_SU320_VU_GC6_SIZE           0x161             /* Vendor Unique group code 6
                                                            * scratch location. Contains
                                                            * either
                                                            * a) the # CDB bytes - 2
                                                            *    to be processed for
                                                            *    a Vendor Unique group
                                                            *    code 6 CDB
                                                            * or
                                                            * b) the value 
                                                            *    SCSI_W160M_HANDLE_VU_IN_HIM
                                                            *    that indicates group code 6
                                                            *    CDBs are to be processed
                                                            *    by the HIM. 
                                                            */
#define SCSI_SU320_VU_GC7_SIZE           0x162             /* Vendor Unique group code 7
                                                            * scratch location. Contains
                                                            * either
                                                            * a) the # CDB bytes - 2
                                                            *    to be processed for
                                                            *    a Vendor Unique group
                                                            *    code 7 CDB
                                                            * or
                                                            * b) the value 
                                                            *    SCSI_W160M_HANDLE_VU_IN_HIM
                                                            *    that indicates group code 7
                                                            *    CDBs are to be processed
                                                            *    by the HIM. 
                                                            */
#define SCSI_SU320_HANDLE_VU_IN_HIM      0xFF              /* Constant used in scratch 
                                                            * locations:
                                                            * SCSI_W160M_VU_GC6_SIZE 
                                                            * SCSI_W160M_VU_GC7_SIZE
                                                            * to indicate CDB processing 
                                                            * is to be handled in the HIM.
                                                            */
#define SCSI_SU320_MIN_VU_CDB_SIZE          3              /* The minimum number of CDB
                                                            * bytes that can be processed
                                                            * by the sequencer for Vendor
                                                            * Unique commands (GC 6 & 7).
                                                            * For smaller CDB sizes the
                                                            * HIM handles the processing
                                                            * due to the current CDB
                                                            * processing algorithm used 
                                                            * by the sequencer.
                                                            */
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_PACKETIZED_IO_SUPPORT
#define SCSI_SU320_SLUN_OFFSET           48
#define SCSI_SU320_SCDB_LENGTH_OFFSET    23

#define SCSI_SU320_TASK_ATTRIBUTE_OFFSET 35

#define SCSI_SU320_TASK_MANAGEMENT_OFFSET 36
#define SCSI_SU320_SCDB_OFFSET           8
#define SCSI_SU320_Q_EXETARG_NEXT_OFFSET 0
#define SCSI_SU320_SCONTROL_OFFSET       38

#define SCSI_SU320_SLUN                  SCSI_SCB_BASE+SCSI_SU320_SLUN_OFFSET
#define SCSI_SU320_SCDB_LENGTH           SCSI_SCB_BASE+SCSI_SU320_SCDB_LENGTH_OFFSET
#define SCSI_SU320_TASK_ATTRIBUTE        SCSI_SCB_BASE+SCSI_SU320_TASK_ATTRIBUTE_OFFSET
#define SCSI_SU320_TASK_MANAGEMENT       SCSI_SCB_BASE+SCSI_SU320_TASK_MANAGEMENT_OFFSET
#define SCSI_SU320_SCDB                  SCSI_SCB_BASE+SCSI_SU320_SCDB_OFFSET
#define SCSI_SU320_Q_EXETARG_NEXT        SCSI_SCB_BASE+SCSI_SU320_Q_EXETARG_NEXT_OFFSET
#define SCSI_SU320_SCONTROL              SCSI_SCB_BASE+SCSI_SU320_SCONTROL_OFFSET
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#define SCSI_SU320_SCONTROL_ABORT        4         /* Abort bit is in bit 4 */

#endif /* (SCSI_STANDARD_MODE && (!SCSI_DCH_U320_MODE)) */

#if SCSI_STANDARD_U320_MODE
/****************************************************************************
* Definitions for standard Ultra 320 mode firmware
* This section must be checked whenever there is any sequencer code update
****************************************************************************/

#define SCSI_SU320_DMAABLE_SIZE_SCB      61

#if SCSI_TARGET_OPERATION
#define SCSI_SU320_IDLE_LOOP0            0x0050
#else
#define SCSI_SU320_IDLE_LOOP0            0x0048
#endif /* SCSI_TARGET_OPERATION */

#define SCSI_SU320_DV_TIMERLO            0x150
#define SCSI_SU320_DV_TIMERHI            0x151

#define SCSI_SU320_FIRST_SCB             0x148

#define SCSI_SU320_SCRATCH_FLAGS         0x14A
#define   SCSI_SU320_SG_CACHE_AVAIL      0x10        /* sg_cache_avail bit      */
                                                     /* position in the         */
                                                     /* scratch_flags register. */

#if (OSD_BUS_ADDRESS_SIZE == 32)
#if SCSI_TARGET_OPERATION
#define SCSI_SU320_EXPANDER_BREAK        0x0548
#else /* !SCSI_TARGET_OPERATION */
#define SCSI_SU320_EXPANDER_BREAK        0x04DC
#endif /* SCSI_TARGET_OPERATION */
#else /* !(OSD_BUS_ADDRESS_SIZE == 32) */
#if SCSI_TARGET_OPERATION
#define SCSI_SU320_EXPANDER_BREAK        0x0550
#else /* !SCSI_TARGET_OPERATION */
#define SCSI_SU320_EXPANDER_BREAK        0x04E4
#endif /* SCSI_TARGET_OPERATION */
#endif /* (OSD_BUS_ADDRESS_SIZE == 32) */

#define SCSI_SU320_ARPINTVALID_REG       SCSI_SU320_SEQ_OPTIONS
#define SCSI_SU320_ARPINTVALID_BIT       0x02

#if SCSI_TARGET_OPERATION   
/* Scratch RAM entries specific to target mode */
#define SCSI_SU320_REG0                  0x146             /* REG0 - used
                                                            * for passing 
                                                            * information.
                                                            */
#define SCSI_SU320_Q_EST_HEAD            0x15C             /* host est scb
                                                            * queue head
                                                            */
#define SCSI_SU320_Q_EST_TAIL            0x15E             /* host est scb
                                                            * queue tail
                                                            */
#define SCSI_SU320_Q_EXE_TARG_HEAD       0x15A             /* Target Mode Execution queue */
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_PACKETIZED_IO_SUPPORT
#define SCSI_SU320_SLUN_LENGTH            6    /* Number of SCB bytes for LUN */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

#endif /* SCSI_STANDARD_U320_MODE */

#if SCSI_STANDARD_ENHANCED_U320_MODE
/****************************************************************************
* Definitions for standard Enhanced Ultra 320 mode firmware
* This section must be checked whenever there is any sequencer code update
****************************************************************************/
#define SCSI_SEU320_DMAABLE_SIZE_SCB     55
#if SCSI_TARGET_OPERATION
#define SCSI_SEU320_IDLE_LOOP0           0x0038
#else
#define SCSI_SEU320_IDLE_LOOP0           0x0030
#endif /* SCSI_TARGET_OPERATION */

#define SCSI_SEU320_DV_TIMERLO           0x15A
#define SCSI_SEU320_DV_TIMERHI           0x15B

#define SCSI_SEU320_FIRST_SCB            0x150
#if (OSD_BUS_ADDRESS_SIZE == 32)
#if SCSI_TARGET_OPERATION
#define SCSI_SEU320_EXPANDER_BREAK        0x04A8
#else /* !SCSI_TARGET_OPERATION */
#define SCSI_SEU320_EXPANDER_BREAK        0x0394
#endif /* SCSI_TARGET_OPERATION */
#else /* !(OSD_BUS_ADDRESS_SIZE == 32) */
#if SCSI_TARGET_OPERATION
#define SCSI_SEU320_EXPANDER_BREAK        0x04B0
#else /* !SCSI_TARGET_OPERATION */
#define SCSI_SEU320_EXPANDER_BREAK        0x039C
#endif /* SCSI_TARGET_OPERATION */
#endif /* (OSD_BUS_ADDRESS_SIZE == 32) */

#define SCSI_SEU320_ARPINTVALID_REG       SCSI_SU320_SEQ_OPTIONS
#define SCSI_SEU320_ARPINTVALID_BIT       0x02

#if SCSI_TARGET_OPERATION   
/* Scratch RAM entries specific to target mode */
#define SCSI_SEU320_REG0                 0x14E             /* REG0 - used
                                                            * for passing 
                                                            * information.
                                                            */
#define SCSI_SEU320_Q_EST_HEAD           0x146             /* host est scb
                                                            * queue head
                                                            */
#define SCSI_SEU320_Q_EST_TAIL           0x148             /* host est scb
                                                            * queue tail
                                                            */
#define SCSI_SEU320_Q_EXE_TARG_HEAD      0x144             /* Target Mode Execution queue */
#define SCSI_SEU320_Q_WAITING_TAIL       0x14A             /* Target mode waiting queue */
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_PACKETIZED_IO_SUPPORT
#define SCSI_SEU320_SLUN_LENGTH    SCSI_ISINGLE_LEVEL_LUN /* Number of SCB bytes for LUN
                                                           * This value means store in
                                                           * byte 5 of SPI L_Q packet to
                                                           * conform with single level
                                                           * lun format.
                                                           */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/****************************************************************************
* Definitions for downshift  mode firmware
* This section must be checked whenever there is any sequencer code update
****************************************************************************/

#if SCSI_DOWNSHIFT_MODE

#define SCSI_DU320_SIZE_SCB              64

#if SCSI_BIOS_SUPPORT
#define SCSI_DU320_BIOS_MIN_SCB_NUMBER    0         /* Minimum SCB number 
                                                     * value that may be
                                                     * used by a BIOS OSM.
                                                     */
#define SCSI_DU320_BIOS_MAX_SCBNUMBER  0xFE         /* Maximum SCB number 
                                                     * value that may be
                                                     * used by a BIOS OSM.
                                                     */
#endif /* SCSI_BIOS_SUPPORT */
#define SCSI_DU320_ALIGNMENT_SCB         0x003F
#define SCSI_DU320_FIRMWARE_VERSION      0
#define SCSI_DU320_BTATABLE              0x016F
#define SCSI_DU320_BTASIZE               256        /* Only supports 16 
                                                     * Luns currently.
                                                     */
#define SCSI_DU320_RESCNT_BASE           0x0188     /* Register address of
                                                     * residual in SCB.
                                                     */
#define SCSI_DU320_IDLE_LOOP_ENTRY       0x0000
#define SCSI_DU320_IDLE_LOOP_TOP         0x0018
#define SCSI_DU320_START_LINK_CMD_ENTRY  0x0004
#define SCSI_DU320_SIOSTR3_ENTRY         0x0008
#define SCSI_DU320_RETRY_IDENTIFY_ENTRY  0x0010
#define SCSI_DU320_SIO204_ENTRY          0x000C
#define SCSI_DU320_IDLE_LOOP             SCSI_NULL_ENTRY   /* Not used */

#define SCSI_DU320_RET_ADDR              0x00F8
#define SCSI_DU320_SG_STATUS             0x00FE
#define   SCSI_DU320_HAVE_SG_CACHE       0x10
#define SCSI_DU320_ACTIVE_SCB            0x00FC

#define SCSI_DU320_Q_NEW_POINTER         0x0100
#define SCSI_DU320_PASS_TO_DRIVER        0x0115
#define SCSI_DU320_Q_EXE_HEAD            0x011C
#define SCSI_DU320_Q_EXE_TAIL            0x011E 
#define SCSI_DU320_Q_EXETARG_TAIL        0x0120 
#define SCSI_DU320_Q_DONE_PASS           0x0114 
#define SCSI_DU320_Q_DONE_BASE           0x0108
#define SCSI_DU320_Q_DONE_POINTER        0x0110
#define SCSI_DU320_Q_DONE_HEAD           0x0140

#define SCSI_DU320_ARPINTVALID_REG       SCSI_DU320_ASPI_BIOS_COMMAND
#define SCSI_DU320_ARPINTVALID_BIT       0x04
#define SCSI_DU320_TARGET_RESET          0x10
/* Location indicating command requestor type -
 * either BIOS or ASPI/Real Mode.
 */
#define SCSI_DU320_ASPI_BIOS_COMMAND     0x0158
/* Command requestor types */
#define SCSI_DU320_BIOS_COMMAND   1          
#define SCSI_DU320_ASPI_COMMAND   2
#define SCSI_DU320_HARPOON2_REVB_ASIC    0x08      /* 0=Rev A, 1=Rev B Harpoon2 chip*/
#define SCSI_DU320_TARGETRESET_BIT 0x10
                                                     /* scratch_flags register. */

#define SCSI_DU320_ENT_PT_BITMAP         SCSI_NULL_ENTRY            
#define SCSI_DU320_OPTIONS               SCSI_NULL_ENTRY /* Sequencer run-time    */
                                                         /* options.              */
#if SCSI_DATA_IN_RETRY_DETECTION
#define SCSI_DU320_DATA_IN_RETRY_DETECTION SCSI_NULL_ENTRY /* Report target data-in */ 
                                                     /* retry occurred. This    */
                                                     /* option checks for       */
                                                     /* missing save data       */
                                                     /* pointers on disconnect  */
                                                     /* restore pointers. This  */
                                                     /* is required by some XOR */
                                                     /* implementations that    */
                                                     /* read data into XOR      */
                                                     /* memory.                 */ 
#endif /* SCSI_DATA_IN_RETRY_DETECTION */

#define SCSI_DU320_DISCONNECT_DELAY      SCSI_NULL_ENTRY  /* Disconnect delay   */  

#define SCSI_DU320_EXPANDER_BREAK        SCSI_NULL_ENTRY
#define SCSI_DU320_ISR                   SCSI_NULL_ENTRY

#if !SCSI_SEQ_LOAD_USING_PIO
#define SCSI_DU320_LOAD_ADDR             0x176
#define SCSI_DU320_LOAD_COUNT            0x17e
#endif /* !SCSI_SEQ_LOAD_USING_PIO */

/*****************************************************
   These are HHCB fields that must be stored locally 
   due to the need to change or update them during 
   command operations while compiled under the BIOS 
   option. In the past SCB 255 was used for those 
   fields. The U320 code will now use ScratchRam. The 
   structure below contains all changeable fields. 
   Should it become necessary to add or remove a
   parameter, the structure must be updated to allow 
   the starting address of scratch to be dyanamically
   adjusted. 

   NOTE: Comment has been added in the sequencer code
         to mention how many bytes are used in the
         scratch.  Therefore, whenever there is a
         change to this structure the comment in the
         sequencer should be updated and any
         overlapping usage in the sequencer should be
         corrected.
******************************************************/   


typedef struct SCSI_DOWNSHIFT_SCRATCH0_HHCB_
{
    SCSI_UEXACT32  SCSIActiveBiosHIOB;
    SCSI_UEXACT8   SCSIBiosInstat; 
    SCSI_UEXACT8   SCSIBiosHcntrl;  
    SCSI_UEXACT16  SCSIDownshiftHnscbQoff;
    SCSI_UEXACT8   SCSIBiosOriginalMode;
} SCSI_DOWNSHIFT_SCRATCH0_HHCB;    

#define SCSI_DOWNSHIFT_SCRATCH_START_ADRS     (SCSI_SCRATCH0_SIZE - sizeof(struct SCSI_DOWNSHIFT_SCRATCH0_HHCB_))
#define SCSI_BIOS_ACTIVE_HIOB            0
#define SCSI_BIOS_INTSTAT                4
#define SCSI_BIOS_HCNTRL                 5
#define SCSI_DOWNSHIFT_HNSCBQOFF         6
#define SCSI_BIOS_ORIGINALMODE           8



#define SCSI_DU320_MAX_NONTAG_SCBS       2

#define SCSI_DU320_SCONTROL_ABORT        4         /* Abort bit is in bit 4 */

#define SCSI_DU320_IDLE_LOOP0            SCSI_NULL_ENTRY

#define SCSI_DU320_SCRATCH_FLAGS         0x014A
#define   SCSI_DU320_SG_CACHE_AVAIL      0x10        /* sg_cache_avail bit      */

#endif
#if SCSI_DOWNSHIFT_U320_MODE

/****************************************************************************
* Definitions for downshift Ultra 320 mode firmware
* This section must be checked whenever there is any sequencer code update
****************************************************************************/

#define SCSI_DU320_DMAABLE_SIZE_SCB      61

#endif /* SCSI_DOWNSHIFT_U320_MODE */


#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
/****************************************************************************
* Definitions for downshift Enhanced Ultra 320 mode firmware
* This section must be checked whenever there is any sequencer code update
****************************************************************************/

#define SCSI_DEU320_DMAABLE_SIZE_SCB      55

#define SCSI_DEU320_ARPINTVALID_REG       SCSI_DU320_ASPI_BIOS_COMMAND
#define SCSI_DEU320_ARPINTVALID_BIT       0x04
#define SCSI_DEU320_TARGET_RESET          0x10

#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */

#if SCSI_DCH_U320_MODE
/****************************************************************************
* Definitions for DCH Ultra 320 mode firmware
* This section must be checked whenever there is any sequencer code update
****************************************************************************/
#define SCSI_DCHU320_SIZE_SCB              64
#define SCSI_DCHU320_DMAABLE_SIZE_SCB      62
#define SCSI_DCHU320_ALIGNMENT_SCB         0x003F
#define SCSI_DCHU320_FIRMWARE_VERSION      0
#define SCSI_DCHU320_BTATABLE              0x01BF
#define SCSI_DCHU320_BTASIZE               256        /* Only supports 16 
                                                     * Luns currently.
                                                     */
#define SCSI_DCHU320_RESCNT_BASE           0x0188     /* Register address of
                                                     * residual in SCB.
                                                     */
#define SCSI_DCHU320_IDLE_LOOP_ENTRY       0x0000
#define SCSI_DCHU320_START_LINK_CMD_ENTRY  0x0004
#define SCSI_DCHU320_SIOSTR3_ENTRY         0x0008
#define SCSI_DCHU320_RETRY_IDENTIFY_ENTRY  0x0010
#define SCSI_DCHU320_SIO204_ENTRY          0x000C
#define SCSI_DCHU320_IDLE_LOOP             SCSI_NULL_ENTRY   /* Not used */
#if SCSI_TARGET_OPERATION
#define SCSI_DCHU320_IDLE_LOOP_TOP         0x0020
#define SCSI_DCHU320_IDLE_LOOP0            0x0030
#else
#define SCSI_DCHU320_IDLE_LOOP_TOP         0x0018
#define SCSI_DCHU320_IDLE_LOOP0            0x0028
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_TARGET_OPERATION   
/* Sequencer entry points specific to target mode */     
#define SCSI_DCHU320_TARGET_DATA_ENTRY     0x0014
#define SCSI_DCHU320_TARGET_HELD_BUS_ENTRY 0x0018
#endif /* SCSI_TARGET_OPERATION */

#define SCSI_DCHU320_RET_ADDR              0x01C0
#define SCSI_DCHU320_SG_STATUS             0x01C2
#define SCSI_DCHU320_HAVE_SG_CACHE         0x10
#define SCSI_DCHU320_ACTIVE_SCB            0x01C4

#define SCSI_DCHU320_Q_NEW_POINTER         0x0100
#define SCSI_DCHU320_PASS_TO_DRIVER        0x0115
#define SCSI_DCHU320_Q_EXE_HEAD            0x011C
#define SCSI_DCHU320_Q_EXE_TAIL            0x011E 
#define SCSI_DCHU320_Q_EXETARG_TAIL        0x0120 
#define SCSI_DCHU320_Q_DONE_PASS           0x0114 
#define SCSI_DCHU320_Q_DONE_BASE           0x0108
#define SCSI_DCHU320_Q_DONE_POINTER        0x0110
#define SCSI_DCHU320_Q_DONE_HEAD           0x0140
#define SCSI_DCHU320_SEQ_OPTIONS           0x0158

#define SCSI_DCHU320_ARPINTVALID_REG         SCSI_NULL_ENTRY
#define SCSI_DCHU320_ARPINTVALID_BIT         SCSI_NULL_ENTRY

/* SCSI_DCHU320_SEQ_OPTIONS bits definition */
#if (OSD_BUS_ADDRESS_SIZE == 64)
#define SCSI_DCHU320_SGSEGMENT_32          0x01      /* flag sequencer to check for */
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */
#define SCSI_DCHU320_MIRROR_RAID1          0x04      /* Enable mirror commands in seq */
#define SCSI_DCHU320_HARPOON2_REVB_ASIC    0x08      /* 0=Rev A, 1=Rev B Harpoon2 chip*/


#define SCSI_DCHU320_ENT_PT_BITMAP         SCSI_NULL_ENTRY            
             
#define SCSI_DCHU320_DV_TIMERLO            0x15A
#define SCSI_DCHU320_DV_TIMERHI            0x15B
             
#define SCSI_DCHU320_FIRST_SCB             0x150

#define SCSI_DCHU320_INTR_THRSH_TIMEOUT1   0x154
#define SCSI_DCHU320_INTR_THRSH_TIMEOUT2   0x155
#define SCSI_DCHU320_INTR_THRSH_MAXCNT1    0x156
#define SCSI_DCHU320_INTR_THRSH_MAXCNT2    0x157
             
/* #define SCSI_DCHU320_SCRATCH_FLAGS         0x163 */
/* #define SCSI_DCHU320_SG_CACHE_AVAIL        0x10*/ /* sg_cache_avail bit      */
                                                     /* position in the         */
                                                     /* scratch_flags register. */
             
#if SCSI_DATA_IN_RETRY_DETECTION
#define SCSI_DCHU320_DATA_IN_RETRY_DETECTION SCSI_NULL_ENTRY /* Report target data-in */ 
                                                     /* retry occurred. This    */
                                                     /* option checks for       */
                                                     /* missing save data       */
                                                     /* pointers on disconnect  */
                                                     /* restore pointers. This  */
                                                     /* is required by some XOR */
                                                     /* implementations that    */
                                                     /* read data into XOR      */
                                                     /* memory.                 */ 
#endif /* SCSI_DATA_IN_RETRY_DETECTION */

#if SCSI_TARGET_OPERATION
#define SCSI_DCHU320_DISCONNECT_DELAY      SCSI_NULL_ENTRY  /* Disconnect delay   */
#else
#define SCSI_DCHU320_DISCONNECT_DELAY      SCSI_NULL_ENTRY  /* Disconnect delay   */  
#endif /* SCSI_TARGET_OPERATION */

#if (OSD_BUS_ADDRESS_SIZE == 32)
#if SCSI_TARGET_OPERATION
#define SCSI_DCHU320_EXPANDER_BREAK        0x46C
#else /* !SCSI_TARGET_OPERATION */
#define SCSI_DCHU320_EXPANDER_BREAK        0x37C
#endif /* SCSI_TARGET_OPERATION */
#else /* !(OSD_BUS_ADDRESS_SIZE == 32) */
#if SCSI_TARGET_OPERATION
#define SCSI_DCHU320_EXPANDER_BREAK        0x46C
#else /* !SCSI_TARGET_OPERATION */
#define SCSI_DCHU320_EXPANDER_BREAK        0x37C
#endif /* SCSI_TARGET_OPERATION */
#endif /* (OSD_BUS_ADDRESS_SIZE == 32) */

#define SCSI_DCHU320_ISR                   0x1000

#if !SCSI_SEQ_LOAD_USING_PIO
#define SCSI_DCHU320_LOAD_ADDR             0x176
#define SCSI_DCHU320_LOAD_COUNT            0x17e
#endif /* !SCSI_SEQ_LOAD_USING_PIO */

/* In the legacy CHIM, we can send only one non-tag command to multi-LUN */
/* target due to the scratch RAM limitation on Swapping mode design.     */
/* The busy target table used for non-tag command is allocated from      */
/* scratch RAM and it can be addressed up to 16 targets.                 */
/* In the U320 CHIM, the sequencer is running Standard mode.  With the   */
/* design of Standard mode that has the Execution queue, we can resolve  */
/* the one non-tag command to multi-LUN target issue by increasing the   */
/* maximum non-tag SCBs to be higher than one.  We will increase it to   */
/* 16.  Because the busy target table has been allocated from SCB array. */
/* And the busy target table size can support up to 16 different LUNs    */
/* with each LUN having one non-tag command running.                     */
#define SCSI_DCHU320_MAX_NONTAG_SCBS       16

#define SCSI_DCHU320_TARG_LUN_MASK0        SCSI_NULL_ENTRY   /* future for RAID */ 
#define SCSI_DCHU320_ARRAY_PARTITION0      SCSI_NULL_ENTRY   /* future for RAID */

#if SCSI_TARGET_OPERATION   
/* Scratch RAM entries specific to target mode */
#define SCSI_DCHU320_REG0                  0x14E           /* REG0 - used
                                                            * for passing 
                                                            * information.
                                                            */
#define SCSI_DCHU320_Q_EST_HEAD            0x15C             /* host est scb
                                                            * queue head
                                                            */
#define SCSI_DCHU320_Q_EST_TAIL            0x148           /* host est scb
                                                            * queue tail
                                                            */
#define SCSI_DCHU320_HESCB_QOFF            SCSI_NULL_ENTRY   /* host establish scb
                                                            * queue offset
                                                            */
#define SCSI_DCHU320_SESCB_QOFF            SCSI_NULL_ENTRY   /* sequencer establish scb queue
                                                            * offset
                                                            */
#define SCSI_DCHU320_Q_EXE_TARG_HEAD       0x144            /* Target Mode Execution queue */
#define SCSI_DCHU320_EST_OPTIONS           0x160             /* Establish connection operating
                                                            * parameters.
                                                            */
#define SCSI_DCHU320_Q_WAITING_TAIL        0x14A            /* Target mode waiting queue */

/* EST_OPTIONS defines */
#define  SCSI_DCHU320_SCSI1_SEL_ENABLE  0x20  /* SCSI-1 Selection Enable
                                             * SCSI_HF_targetEnableScsi1Selection
                                             */
#define  SCSI_DCHU320_DISCON_ENABLE     0x10  /* Disconnect enable
                                             * SCSI_HF_targetDisconnectAllowed
                                             */ 
#define  SCSI_DCHU320_SCSI3_ENABLE      0x08  /* SCSI-3 enable
                                             * SCSI_HF_targetHostTargetVersion = 
                                             *
                                             */
#define  SCSI_DCHU320_TAG_ENABLE        0x04  /* Tagged commands enable
                                             * SCSI_HF_targetTagEnable
                                             */
#define  SCSI_DCHU320_CHK_RSVD_BITS     0x02  /* Check reserved bits in
                                             * identify message
                                             * SCSI_HF_targetScsi2IdentifyMsgRsv
                                             */ 
#define  SCSI_DCHU320_LUNTAR_ENABLE     0x01  /* LunTar bit in identify 
                                             * message enable
                                             * SCSI_HF_targetScsi2RejectLuntar
                                             */
#define  SCSI_DCHU320_VU_GC6_SIZE       0x161             /* Vendor Unique group code 6
                                                            * scratch location. Contains
                                                            * either
                                                            * a) the # CDB bytes - 2
                                                            *    to be processed for
                                                            *    a Vendor Unique group
                                                            *    code 6 CDB
                                                            * or
                                                            * b) the value 
                                                            *    SCSI_W160M_HANDLE_VU_IN_HIM
                                                            *    that indicates group code 6
                                                            *    CDBs are to be processed
                                                            *    by the HIM. 
                                                            */
#define SCSI_DCHU320_VU_GC7_SIZE           0x162             /* Vendor Unique group code 7
                                                            * scratch location. Contains
                                                            * either
                                                            * a) the # CDB bytes - 2
                                                            *    to be processed for
                                                            *    a Vendor Unique group
                                                            *    code 7 CDB
                                                            * or
                                                            * b) the value 
                                                            *    SCSI_W160M_HANDLE_VU_IN_HIM
                                                            *    that indicates group code 7
                                                            *    CDBs are to be processed
                                                            *    by the HIM. 
                                                            */
#define SCSI_DCHU320_HANDLE_VU_IN_HIM      0xFF              /* Constant used in scratch 
                                                            * locations:
                                                            * SCSI_W160M_VU_GC6_SIZE 
                                                            * SCSI_W160M_VU_GC7_SIZE
                                                            * to indicate CDB processing 
                                                            * is to be handled in the HIM.
                                                            */
#define SCSI_DCHU320_MIN_VU_CDB_SIZE          3              /* The minimum number of CDB
                                                            * bytes that can be processed
                                                            * by the sequencer for Vendor
                                                            * Unique commands (GC 6 & 7).
                                                            * For smaller CDB sizes the
                                                            * HIM handles the processing
                                                            * due to the current CDB
                                                            * processing algorithm used 
                                                            * by the sequencer.
                                                            */
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_PACKETIZED_IO_SUPPORT
#define SCSI_DCHU320_SLUN_OFFSET           48
#define SCSI_DCHEU320_SLUN_LENGTH  SCSI_ISINGLE_LEVEL_LUN /* Number of SCB bytes for LUN
                                                           * This value means store in
                                                           * byte 5 of SPI L_Q packet to
                                                           * conform with single level
                                                           * lun format.
                                                           */
#define SCSI_DCHU320_SCDB_LENGTH_OFFSET    23

#define SCSI_DCHU320_TASK_ATTRIBUTE_OFFSET 39

#define SCSI_DCHU320_TASK_MANAGEMENT_OFFSET 36
#define SCSI_DCHU320_SCDB_OFFSET           8
#define SCSI_DCHU320_Q_EXETARG_NEXT_OFFSET 0
#define SCSI_DCHU320_SCONTROL_OFFSET       38
             
#define SCSI_DCHU320_SLUN                  SCSI_SCB_BASE+SCSI_DCHU320_SLUN_OFFSET
#define SCSI_DCHU320_SCDB_LENGTH           SCSI_SCB_BASE+SCSI_DCHU320_SCDB_LENGTH_OFFSET
#define SCSI_DCHU320_TASK_ATTRIBUTE        SCSI_SCB_BASE+SCSI_DCHU320_TASK_ATTRIBUTE_OFFSET
#define SCSI_DCHU320_TASK_MANAGEMENT       SCSI_SCB_BASE+SCSI_DCHU320_TASK_MANAGEMENT_OFFSET
#define SCSI_DCHU320_SCDB                  SCSI_SCB_BASE+SCSI_DCHU320_SCDB_OFFSET
#define SCSI_DCHU320_Q_EXETARG_NEXT        SCSI_SCB_BASE+SCSI_DCHU320_Q_EXETARG_NEXT_OFFSET
#define SCSI_DCHU320_SCONTROL              SCSI_SCB_BASE+SCSI_DCHU320_SCONTROL_OFFSET
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

#define SCSI_DCHU320_SCONTROL_ABORT        4         /* Abort bit is in bit 4 */

#endif /* SCSI_DCH_U320_MODE */




/*$Header: /vobs/u320chim/src/chim/hwm/scsidef.h   /main/150   Thu Aug 21 17:31:15 2003   luu $*/
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
*  Module Name:   SCSIDEF.H
*
*  Description:   Definitions as interface to HIM internal layers
*    
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         1. This file is referenced by all three layers of Common
*                    HIM implementation. 
*
***************************************************************************/

/***************************************************************************
* Miscellaneous
***************************************************************************/
#ifndef SCSI_ACCESS_RAM
#define SCSI_ACCESS_RAM SCSI_BIOS_ASPI8DOS
#endif /* SCSI_ACCESS_RAM */
#if SCSI_SIMULATION_SUPPORT
#define  SCSI_MAXLUN       1
#else
#define  SCSI_MAXLUN       64
#endif /* SCSI_SIMULATION_SUPPORT */

#ifndef SCSI_DOMAIN_VALIDATION   
#error SCSI_DOMAIN_VALIDATION Should be defined here
#endif

/***************************************************************************
* SCSI_AF_dvLevel (per device)
***************************************************************************/
#define  SCSI_DV_DISABLE            0        /* disable domain validation  */
#define  SCSI_DV_BASIC              1        /* enable Domain Validation level 1 */
#define  SCSI_DV_ENHANCED           2        /* enable Domain Validation level 2  */

/***************************************************************************
* SCSI_DEVICE structures definitions
***************************************************************************/
#define SCSI_SIZEWWID   32

/* Defined for xferRate in Device Table */
#if (SCSI_MULTIPLEID_SUPPORT && SCSI_TARGET_OPERATION)

#define  SCSI_TARGMODE_ID0_ENTRY    0
#define  SCSI_TARGMODE_ID1_ENTRY    1
#define  SCSI_TARGMODE_ID2_ENTRY    2
#define  SCSI_TARGMODE_ID3_ENTRY    3
#define  SCSI_TARGMODE_ID4_ENTRY    4
#define  SCSI_TARGMODE_ID5_ENTRY    5
#define  SCSI_TARGMODE_ID6_ENTRY    6
#define  SCSI_TARGMODE_ID7_ENTRY    7
#define  SCSI_TARGMODE_ID8_ENTRY    8
#define  SCSI_TARGMODE_ID9_ENTRY    9
#define  SCSI_TARGMODE_ID10_ENTRY   10
#define  SCSI_TARGMODE_ID11_ENTRY   11
#define  SCSI_TARGMODE_ID12_ENTRY   12
#define  SCSI_TARGMODE_ID13_ENTRY   13
#define  SCSI_TARGMODE_ID14_ENTRY   14
#define  SCSI_TARGMODE_ID15_ENTRY   15
#define  SCSI_CURRENT_ENTRY         16
#define  SCSI_LAST_NEGO_ENTRY       17
#define  SCSI_HARDCODE_ENTRY        18
#define  SCSI_NVRAM_ENTRY           19
#define  SCSI_BIOS_REALMODE_ENTRY   20
#define  SCSI_TARGET_CAP_ENTRY      21
#define  SCSI_TARGET_PROFILE_ENTRY  22
#define  SCSI_DV_ENTRY              23
#define  SCSI_TEMP_ENTRY            24

#define  SCSI_MAX_XFERRATE_ENTRIES  25 /* Update when new entry added */

#else /* !(SCSI_MULTIPLEID_SUPPORT && SCSI_TARGET_OPERATION) */

#if !SCSI_ASYNC_NARROW_MODE

#if (SCSI_DOWNSHIFT_MODE)
#define  SCSI_CURRENT_ENTRY         0
#define  SCSI_LAST_NEGO_ENTRY       1
#define  SCSI_HARDCODE_ENTRY        2
#define  SCSI_NVRAM_ENTRY           3
#define  SCSI_BIOS_REALMODE_ENTRY   4
#define  SCSI_TARGET_CAP_ENTRY      5
#define  SCSI_TEMP_ENTRY            6

#define  SCSI_MAX_XFERRATE_ENTRIES  7  /* Update when new entry added */

#else /* !(SCSI_DOWNSHIFT_MODE) */

#define  SCSI_CURRENT_ENTRY         0
#define  SCSI_LAST_NEGO_ENTRY       1
#define  SCSI_HARDCODE_ENTRY        2
#define  SCSI_NVRAM_ENTRY           3
#define  SCSI_BIOS_REALMODE_ENTRY   4
#define  SCSI_TARGET_CAP_ENTRY      5
#define  SCSI_TARGET_PROFILE_ENTRY  6
#define  SCSI_DV_ENTRY              7
#define  SCSI_TEMP_ENTRY            8

#define  SCSI_MAX_XFERRATE_ENTRIES  9  /* Update when new entry added */

#endif /* (SCSI_DOWNSHIFT_MODE) */

#endif /* !SCSI_ASYNC_NARROW_MODE */

#endif /* (SCSI_MULTIPLEID_SUPPORT && SCSI_TARGET_OPERATION) */

#define  SCSI_XFER_PERIOD           0
#define  SCSI_XFER_OFFSET           1
#define  SCSI_XFER_PTCL_OPT         2
#define  SCSI_XFER_MISC_OPT         3

#define  SCSI_MAX_XFER_PARAMETERS   4  /* Update when new parameter added */

#define  SCSI_ASYNC_XFER_PERIOD     0x32

struct SCSI_DEVICE_
{
   struct
   {
      SCSI_UEXACT16 disconnectEnable:1;      /* disconnect enable             */
      SCSI_UEXACT16 behindExp:1;             /* target is behind bus expander */
      SCSI_UEXACT16 resetSCSI:1;             /* target been reset?            */
      SCSI_UEXACT16 suppressNego:1;          /* suppress negotiation          */
      SCSI_UEXACT16 needNego:1;              /* negotiation is needed         */
      SCSI_UEXACT16 negotiateDtc:1;          /* DTC negotiation allowed       */
      SCSI_UEXACT16 hostManaged:1;           /* host managed (NTC)            */
      SCSI_UEXACT16 switchPkt:1;             /* packetized switch to non-packetized */
      SCSI_UEXACT16 negoOnModeSwitch:1;      /* negotiation due to mode switch */
      SCSI_UEXACT16 ptclOptMaskEnable:1;     /* Use profile protocol option mask */
      SCSI_UEXACT16 reserved:6;              /* for future use                */

   } flags;

   struct
   {
      SCSI_UEXACT16 precomp:3;               /* Precomp cutback level         */
      SCSI_UEXACT16 slewrate:4;              /* slew rate level               */
      SCSI_UEXACT16 userSlewRate:1;          /* flag set if OSM specificed    */
                                             /* the slew rate                 */
      SCSI_UEXACT16 amplitude:3;             /* amplitude level               */
      SCSI_UEXACT16 reserved2:5;
   } elecParameters;

   SCSI_UEXACT8 maxXferIndex;                /* index for max xfer rate       */
   SCSI_UEXACT8 defaultXferIndex;            /* index for default xfer rate   */
   SCSI_UEXACT8 negoXferIndex;               /* index for nego rate           */
   SCSI_UEXACT8 preDVXferIndex;              /* index for default xfer rate   */
                                             /* before DV.                    */

                                             /* these fields use for adapter/ */
                                             /* target profile requesting     */
                                             /* without pausing sequencer     */
#if SCSI_PROFILE_INFORMATION   
   SCSI_UEXACT8 annexdatPerDevice0;          /* annexdat for per-device byte 0*/
   SCSI_UEXACT8 annexdatPerDevice1;          /* annexdat for per-device byte 1*/
   SCSI_UEXACT8 annexdatPerDevice2;          /* annexdat for per-device byte 2*/
   SCSI_UEXACT8 annexdatPerDevice3;          /* annexdat for per-device byte 3*/
#endif /* SCSI_PROFILE_INFORMATION */

                                             /* SCSI transfer rates table     */
   SCSI_UEXACT8 xferRate[SCSI_MAX_XFERRATE_ENTRIES][SCSI_MAX_XFER_PARAMETERS];

#if SCSI_NEGOTIATION_PER_IOB
   SCSI_UEXACT8 origOffset;
   struct
   {
      SCSI_UEXACT16 forceSync:1;
      SCSI_UEXACT16 forceAsync:1;
      SCSI_UEXACT16 forceWide:1;
      SCSI_UEXACT16 forceNarrow:1;
      SCSI_UEXACT16 reserved:12;             /* for future use                */
   } negoFlags;
   SCSI_UEXACT8 reserved4[1];                /* padded for alignment          */
#endif /* SCSI_NEGOTIATION_PER_IOB */
};

#define  SCSI_DF_disconnectEnable   flags.disconnectEnable
#define  SCSI_DF_behindExp          flags.behindExp
#define  SCSI_DF_resetSCSI          flags.resetSCSI
#define  SCSI_DF_suppressNego       flags.suppressNego
#define  SCSI_DF_needNego           flags.needNego
#define  SCSI_DF_negotiateDtc       flags.negotiateDtc
#define  SCSI_DF_hostManaged        flags.hostManaged
#define  SCSI_DF_switchPkt          flags.switchPkt
#define  SCSI_DF_negoOnModeSwitch   flags.negoOnModeSwitch
#define  SCSI_DF_ptlOptMaskEnable   flags.ptclOptMaskEnable

#define  SCSI_DEP_precomp           elecParameters.precomp
#define  SCSI_DEP_slewrate          elecParameters.slewrate
#define  SCSI_DEP_amplitude         elecParameters.amplitude
#define  SCSI_DEP_userSlewRate      elecParameters.userSlewRate



#if SCSI_NEGOTIATION_PER_IOB
#define  SCSI_DNF_forceSync         negoFlags.forceSync
#define  SCSI_DNF_forceAsync        negoFlags.forceAsync
#define  SCSI_DNF_forceNarrow       negoFlags.forceNarrow
#define  SCSI_DNF_forceWide         negoFlags.forceWide
#endif /* SCSI_NEGOTIATION_PER_IOB */

#define  SCSI_CURRENT_XFER(deviceTable) \
              (deviceTable)->xferRate[SCSI_CURRENT_ENTRY]
#define  SCSI_LAST_NEGO_XFER(deviceTable) \
              (deviceTable)->xferRate[SCSI_LAST_NEGO_ENTRY]
#define  SCSI_HARDCODE_XFER(deviceTable) \
              (deviceTable)->xferRate[SCSI_HARDCODE_ENTRY]
#define  SCSI_NVRAM_XFER(deviceTable) \
              (deviceTable)->xferRate[SCSI_NVRAM_ENTRY]
#define  SCSI_BIOS_REALMODE_XFER(deviceTable) \
              (deviceTable)->xferRate[SCSI_BIOS_REALMODE_ENTRY]
#define  SCSI_TARGET_CAP_XFER(deviceTable) \
              (deviceTable)->xferRate[SCSI_TARGET_CAP_ENTRY]
#define  SCSI_TARGET_PROFILE_XFER(deviceTable) \
              (deviceTable)->xferRate[SCSI_TARGET_PROFILE_ENTRY]
#define  SCSI_DV_XFER(deviceTable) \
              (deviceTable)->xferRate[SCSI_DV_ENTRY]
#define  SCSI_TEMP_XFER(deviceTable) \
              (deviceTable)->xferRate[SCSI_TEMP_ENTRY]

#define  SCSI_MAX_XFER(deviceTable) \
              (deviceTable)->xferRate[(deviceTable)->maxXferIndex]
#define  SCSI_DEFAULT_XFER(deviceTable) \
              (deviceTable)->xferRate[(deviceTable)->defaultXferIndex]
#define  SCSI_NEGO_XFER(deviceTable) \
              (deviceTable)->xferRate[(deviceTable)->negoXferIndex]

/***************************************************************************
* SCSI_UINT_HANDLE structures definitions
***************************************************************************/
typedef union SCSI_UNIT_HANDLE_
{
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit;   /* as target unit handle   */
   SCSI_HHCB SCSI_HPTR initiatorUnit;        /* as hhcb                 */
   SCSI_HIOB SCSI_IPTR relatedHiob;          /* related (aborted) hiob  */
#if SCSI_TARGET_OPERATION
   SCSI_NEXUS SCSI_XPTR nexus;               /* as nexus (target mode)  */
#endif /* SCSI_TARGET_OPERATION */
} SCSI_UNIT_HANDLE;

/***************************************************************************
* SCSI_MEMORY_TABLE structures definitions
***************************************************************************/

#if !SCSI_BIOS_SUPPORT
typedef struct SCSI_MEMORY_TABLE_
{
   struct
   {
      SCSI_UEXACT8 memoryCategory;           /* memory category         */
      SCSI_UEXACT8 memoryAlignment;          /* alignment offset        */
      SCSI_UEXACT8 memoryType;               /* memory type             */
      SCSI_UEXACT8 granularity;              /* alignment               */
      SCSI_UEXACT16 memorySize;              /* size of memory required */
      SCSI_UEXACT16 minimumSize;             /* size of memory required */
      union
      {
         void SCSI_HPTR hPtr;          
         void SCSI_IPTR iPtr;
         void SCSI_SPTR sPtr;
         void SCSI_FPTR fPtr;
         void SCSI_LPTR lPtr;
         void SCSI_DPTR dPtr;
         void SCSI_UPTR uPtr;
#if SCSI_TARGET_OPERATION
         void SCSI_XPTR xPtr;
         void SCSI_NPTR nPtr;
#endif /* SCSI_TARGET_OPERATION */
      } ptr;
   } memory[SCSI_MAX_MEMORY];
} SCSI_MEMORY_TABLE;

/* memory types */
#define  SCSI_MT_HPTR   0                    /* ptr to hhcb */
#define  SCSI_MT_IPTR   1                    /* ptr to hiob */
#define  SCSI_MT_SPTR   2                    /* ptr to stack */
#define  SCSI_MT_FPTR   3                    /* ptr to function space */
#define  SCSI_MT_LPTR   4                    /* ptr to local data */
#define  SCST_MT_DPTR   5                    /* ptr to device table */
#define  SCSI_MT_UPTR   6                    /* ptr to target unit */
#if SCSI_TARGET_OPERATION 
#define  SCSI_MT_XPTR   7                    /* ptr to nexus memory (target mode) */
#define  SCSI_MT_NPTR   8                    /* ptr to node memory (target mode)  */ 
#endif /* SCSI_TARGET_OPERATION */
#endif /* !SCSI_BIOS_SUPPORT */

/* Cover Quotient - X is offset and Y is the number of bytes in quantity */
/* Here it's used to ensure that each item in the MORE LOCKED and UNLOCKED */
/* memory areas is aligned on the appropriate void pointer boundary. */  
#define  SCSI_COVQ(X,Y)             ((((X) + (Y)) - 1) & (~((Y)-1)))

/***************************************************************************
* Target Mode specific HHCB structures definitions
***************************************************************************/
#if SCSI_TARGET_OPERATION
typedef struct SCSI_HHCB_TARGET_MODE_
{
#if SCSI_RESOURCE_MANAGEMENT
   /* Target Mode fields */
   SCSI_UEXACT16 numberNexusHandles;      /* number of nexus task set handles          */
   SCSI_UEXACT8 numberNodeHandles;        /* number of node task set handles           */
   SCSI_UEXACT8 reserved1;                /* alignment                                 */
   SCSI_UEXACT16 nexusThreshold;          /* nexus threshold for generating an event   */
   SCSI_UEXACT8 reserved2[2];             /* alignment                                 */
   SCSI_UEXACT16 numberEstScbs;           /* number of Establish Connection Scbs       */
   SCSI_UEXACT16 nexusQueueCnt;           /* number of available nexus handles         */
   SCSI_NEXUS SCSI_XPTR nexusQueue;       /* head of nexus queue                       */  
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT   
   SCSI_NEXUS SCSI_XPTR nexusMemory;      /* Pointer to start of nexus memory area     */    
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */   
   SCSI_UEXACT16 hiobQueueCnt;            /* number of Establish Connection HIOBs
                                           * queued
                                           */
   SCSI_UEXACT16 hiobQueueThreshold;      /* hiob threshold for generating an event    */
   SCSI_HIOB SCSI_IPTR hiobQueue;         /* Establish Connection HIOB available queue */ 
   SCSI_NODE SCSI_NPTR nodeQueue;         /* Node available queue                      */
#endif /* SCSI_RESOURCE_MANAGEMENT */
   SCSI_UEXACT8  group6CDBsz;             /* # CDB bytes for a group 6 vendor unique   */
                                          /* command                                   */
   SCSI_UEXACT8  group7CDBsz;             /* # CDB bytes for a group 7 vendor unique   */
                                          /* command                                   */
   SCSI_UEXACT8  hostTargetVersion;       /* ANSI version for target mode operation.   */
                                          /* SCSI_2 or SCSI_3                          */
   union
   {
      SCSI_UEXACT8 u8;
      struct
      {
         SCSI_UEXACT8  abortTask:1;       /* Abort Tag/Task message supported          */ 
         SCSI_UEXACT8  clearTaskSet:1;    /* Clear Queue/Task Set message supported    */
         SCSI_UEXACT8  terminateTask:1;   /* Terminate I/O Process/Task message        */
                                          /* supported                                 */
         SCSI_UEXACT8  clearACA:1;        /* Clear ACA message supported               */
         SCSI_UEXACT8  logicalUnitReset:1;/* Logical Unit Reset message supported      */
      } bits;
   } targetTaskMngtOpts;
   union
   {
      SCSI_UEXACT16 u16;
      struct
      {
         SCSI_UEXACT16  disconnectAllowed:1;       /* Disconnect Allowed enable/disable*/
         SCSI_UEXACT16  tagEnable:1;               /* Tagged requests enable/disable   */
         SCSI_UEXACT16  outOfOrderTransfers:1;     /* Out of order transfers enable/   */
                                                   /* disable                          */
         SCSI_UEXACT16  scsi2IdentifyMsgRsv:1;     /* Generate message reject when     */
                                                   /* reserved bit set in identify msg */ 
         SCSI_UEXACT16  scsi2RejectLuntar:1;       /* Generate message reject when     */
                                                   /* LunTar bit set in identify msg   */ 
         SCSI_UEXACT16  ignoreWideResidueMsg:1;    /* Generate Ignore Wide Residue     */
                                                   /* on wide odd transfer             */ 
         SCSI_UEXACT16  scsiBusHeld:1;             /* SCSI bus is currently held       */
         SCSI_UEXACT16  initNegotiation:1;         /* Target initiated negotiation     */
                                                   /* enabled/disabled                 */
         SCSI_UEXACT16  scsi1Selection:1;          /* Accept a SCSI-1 selection        */
         SCSI_UEXACT16  holdEstHiobs:1;            /* Don't queue establish connection */
                                                   /* HIOBs to HWM layer               */  
      } bits;
   } targetFlags;
   SCSI_UEXACT16 targetIDMask;            /* mask of enabled SCSI IDs when operating   */
                                          /* as a target                               */
   SCSI_UEXACT16 targetDGCRCInterval;     /* CRC interval (in bytes) when operating at */
                                          /* DT Datagroup transfer rates.              */
#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
   SCSI_UEXACT16 targetIUCRCInterval;     /* CRC interval (in bytes) when operating at */
                                          /* IU DT transfer rates.                     */
   SCSI_UEXACT16 targetErrorScb;          /* SCB # to be used for packetized target    */
                                          /* mode error handling.                      */
   SCSI_UEXACT16 lqCrcErrorCount;         /* Count of LQ CRC errors detected.          */
   SCSI_UEXACT16 cmndCrcErrorCount;       /* Count of CMND IU CRC errors detected.     */
   SCSI_UEXACT16 badLqTypeErrorCount;     /* Count of Bad LQ TYPE field errors         */
                                          /* detected.                                 */
   SCSI_UEXACT16 reserved3;               /* alignment                                 */             
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
   SCSI_UEXACT8 targetNumIDs;             /* number of IDs supported when operating    */
                                          /* as a target                               */
   SCSI_UEXACT8 reserved4[1];             /* alignment                                 */
} SCSI_HHCB_TARGET_MODE;

/* Definitions for target HHCB fields */
#if SCSI_RESOURCE_MANAGEMENT
#define  SCSI_HF_targetNumberNexusHandles   targetMode.numberNexusHandles
#define  SCSI_HF_targetNumberNodeHandles    targetMode.numberNodeHandles
#define  SCSI_HF_targetNumberEstScbs        targetMode.numberEstScbs
#define  SCSI_HF_targetNexusQueueCnt        targetMode.nexusQueueCnt
#define  SCSI_HF_targetNexusThreshold       targetMode.nexusThreshold
#define  SCSI_HF_targetNexusQueue           targetMode.nexusQueue
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT   
#define  SCSI_HF_targetNexusMemory          targetMode.nexusMemory
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */   
#define  SCSI_HF_targetHiobQueueCnt         targetMode.hiobQueueCnt
#define  SCSI_HF_targetHiobQueueThreshold   targetMode.hiobQueueThreshold
#define  SCSI_HF_targetHiobQueue            targetMode.hiobQueue
#define  SCSI_HF_targetNodeQueue            targetMode.nodeQueue
#endif /* SCSI_RESOURCE_MANAGEMENT */

#define  SCSI_HF_targetGroup6CDBsz          targetMode.group6CDBsz
#define  SCSI_HF_targetGroup7CDBsz          targetMode.group7CDBsz 
#define  SCSI_HF_targetHostTargetVersion    targetMode.hostTargetVersion
#define  SCSI_HF_targetTaskMngtOpts         targetMode.targetTaskMngtOpts.u8
#define  SCSI_HF_targetNumIDs               targetMode.targetNumIDs
#define  SCSI_HF_targetAdapterIDMask        targetMode.targetIDMask
#define  SCSI_HF_targetDGCRCInterval        targetMode.targetDGCRCInterval
#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
#define  SCSI_HF_targetIUCRCInterval        targetMode.targetIUCRCInterval
#define  SCSI_HF_targetErrorScb             targetMode.targetErrorScb 
#define  SCSI_HF_targetLqCrcErrorCount      targetMode.lqCrcErrorCount
#define  SCSI_HF_targetCmndCrcErrorCount    targetMode.cmndCrcErrorCount
#define  SCSI_HF_targetBadLqTypeErrorCount  targetMode.badLqTypeErrorCount
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */ 
/* Definitions for target HHCB Flags bits */
#define  SCSI_HF_targetDisconnectAllowed   targetMode.targetFlags.bits.disconnectAllowed
#define  SCSI_HF_targetTagEnable           targetMode.targetFlags.bits.tagEnable 
#define  SCSI_HF_targetOutOfOrderTransfers targetMode.targetFlags.bits.outOfOrderTransfers
#define  SCSI_HF_targetScsi2IdentifyMsgRsv targetMode.targetFlags.bits.scsi2IdentifyMsgRsv
#define  SCSI_HF_targetIgnoreWideResidMsg  targetMode.targetFlags.bits.ignoreWideResidueMsg
#define  SCSI_HF_targetScsiBusHeld         targetMode.targetFlags.bits.scsiBusHeld
#define  SCSI_HF_targetInitNegotiation     targetMode.targetFlags.bits.initNegotiation
#define  SCSI_HF_targetEnableScsi1Selection targetMode.targetFlags.bits.scsi1Selection 
#define  SCSI_HF_targetScsi2RejectLuntar   targetMode.targetFlags.bits.scsi2RejectLuntar
#define  SCSI_HF_targetHoldEstHiobs        targetMode.targetFlags.bits.holdEstHiobs

/* Definitions for target HHCB Task Management Flags bits */
#define  SCSI_HF_targetAbortTask   targetMode.targetTaskMngtOpts.bits.abortTask
#define  SCSI_HF_targetClearTaskSet   targetMode.targetTaskMngtOpts.bits.clearTaskSet
#define  SCSI_HF_targetTerminateTask   targetMode.targetTaskMngtOpts.bits.terminateTask
#define  SCSI_HF_targetClearACA   targetMode.targetTaskMngtOpts.bits.clearACA
#define  SCSI_HF_targetLogicalUnitReset   targetMode.targetTaskMngtOpts.bits.logicalUnitReset

/* Definitions for default values */
#define  SCSI_Group6CDBDefaultSize      12
#define  SCSI_Group7CDBDefaultSize      12

/* Definitions for hostTargetVersion */
#define  SCSI_VERSION_2                 2
#define  SCSI_VERSION_3                 3

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
/* Definition for maximum number of packetized commands to receive
 * per connection.
 */
#define  MAX_TM_CMNDS_PER_CONNECTION    1     
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */

/***************************************************************************
* HHCB structures definitions
***************************************************************************/
struct SCSI_HHCB_
{
   SCSI_UEXACT16 deviceID;                   /* device id                     */
   SCSI_UEXACT16 productID;                  /* product id                    */
   SCSI_UEXACT8 hardwareRevision;            /* hardware revision             */
   SCSI_UEXACT8 firmwareVersion;             /* firmware version              */
   SCSI_UEXACT8 softwareVersion;             /* software version              */
#if !SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 badSeqCallerId;              /* SCSIhBadSeq callerId parameter */     
#else
   SCSI_UEXACT8 alignment1;                  /* padded for alignment          */
#endif /* !SCSI_BIOS_SUPPORT */
   SCSI_UEXACT16 numberScbs;                 /* number of scbs configured     */
   SCSI_UEXACT16 totalDoneQElements;         /* number of DONE Q elements     */
   SCSI_REGISTER scsiRegister;               /* register access               */
   union
   {
      SCSI_UEXACT16 u16;
      struct 
      {
         SCSI_UEXACT16 scsiParity:1;         /* scsi parity check enable      */
         SCSI_UEXACT16 resetSCSI:1;          /* reset SCSI at initialization  */
         SCSI_UEXACT16 multiTaskLun:1;       /* multitasking target/lun       */
         SCSI_UEXACT16 primaryTermLow:1;     /* pri termination low enable    */
         SCSI_UEXACT16 primaryTermHigh:1;    /* pri termination high enable   */
         SCSI_UEXACT16 primaryAutoTerm:1;    /* pri auto termination          */
         SCSI_UEXACT16 secondaryTermLow:1;   /* sec termination low enable    */
         SCSI_UEXACT16 secondaryTermHigh:1;  /* sec termination high enable   */
         SCSI_UEXACT16 secondaryAutoTerm:1;  /* sec auto termination          */
         SCSI_UEXACT16 selTimeout:2;         /* selection time out            */
         SCSI_UEXACT16 expSupport:1;         /* bus expander supported        */
         SCSI_UEXACT16 cacheThEn:1;          /* cache threshold enable state  */
         SCSI_UEXACT16 SgBusAddress32:1;     /* sglists segments at 32 bits   */
         SCSI_UEXACT16 multiFunction:1;      /* multi-function ASIC           */
         SCSI_UEXACT16 clearConfigurationStatus:1; /* clear Configuration Status */
      } bits;
   } flags;
   union
   {
      SCSI_UEXACT16 u16;
      struct 
      {
         SCSI_UEXACT16 chipReset:1;             /* chiprst bit in HCNTRL reg.    */
         SCSI_UEXACT16 separateRWThreshold:1;   /* hardware supports separate    */
                                                /* FIFO thresholds for read and  */
                                                /* write.                        */
         SCSI_UEXACT16 separateRWThresholdEnable:1; /* separate read/write       */
                                                /* threshold enable              */
         SCSI_UEXACT16 initiatorMode:1;         /* initiator Mode of operation   */
         SCSI_UEXACT16 targetMode:1;            /* target Mode of operation      */
         SCSI_UEXACT16 OverrideOSMNVRAMRoutines:1; /* override NVRAM routines    */
         SCSI_UEXACT16 dontSaveSeqInState:1;    /* dont save/restore the sequencer      */
                                                /* in/from SCSI_STATE. Note this is     */
                                                /* a "dontSave" to maintain consistency */
                                                /* with existing HWM layer interface    */
         SCSI_UEXACT16 clusterEnable:1;         /* cluster enable bit from NVRAM (NTC)  */
         SCSI_UEXACT16 terminationLevel:1;      /* termination level                    */
         SCSI_UEXACT16 SuppressNegoSaveRestoreState:1;
                                                /* no renegotiate after RestoreState    */
#if SCSI_DOMAIN_VALIDATION
         SCSI_UEXACT16 dvThrottleOSMReset:1;    /* throttle down after OSM reset for DV */
#else /* SCSI_DOMAIN_VALIDATION */
#if SCSI_BIOS_SUPPORT
         SCSI_UEXACT16 biosRunTimeEnabled:1;    /* Bios initialization complete.        */
#else /* SCSI_BIOS_SUPPORT */
         SCSI_UEXACT16 reserved:1;              /* reserved for future use              */
#endif /* SCSI_BIOS_SUPPORT */         
#endif /* SCSI_DOMAIN_VALIDATION */
         SCSI_UEXACT16 ForceSTPWLEVELForEmbedded:1; /* force STPWLEVEL bit to one       */
                                                    /* in DEVCONFIG PCI config register */
         SCSI_UEXACT16 busHung:1;               /* Set by CHIM. 1 = bus is hung  */
                                                /* after a reset.                */
         SCSI_UEXACT16 switchHappened:1;        /* Packetized to Non-Packetized  */
                                                /* switched, and vice versa.     */
         SCSI_UEXACT16 chipInitialized:1;       /* chip already initialized      */
                                                /* before enter CHIM driver.     */
         SCSI_UEXACT16 autoTermDetected:1;      /* auto termination detected */
    } bits;
   } flags2;

   SCSI_UEXACT8 busRelease;                  /* bus release                   */
   SCSI_UEXACT8 threshold;                   /* threshold                     */
   SCSI_UEXACT8 firmwareMode;                /* firmware mode of operation    */
   SCSI_UEXACT8 hardwareMode;                /* hardware mode of operation    */
   SCSI_UEXACT8 OEMHardware;                 /* OEM hardware support flag     */
   SCSI_UEXACT8 indexWithinGroup;            /* index within group            */
#if SCSI_RESOURCE_MANAGEMENT
   SCSI_UEXACT8 maxNonTagScbs;               /* maximum non tagged scbs       */
   SCSI_UEXACT8 maxTagScbs;                  /* maximum tagged scbs           */
#else
   SCSI_UEXACT8 alignment2[2];               /* padded for alignment          */
#endif /* SCSI_RESOURCE_MANAGEMENT */
   SCSI_UEXACT8 maxDevices;                  /* max number of devices allowed */
   SCSI_UEXACT8 maxLU;                       /* max lun allowed               */
   SCSI_UEXACT8 hostScsiID;                  /* host scsi id                  */
   SCSI_UEXACT8 disconnectDelay;             /* disconnect delay value in usecs */
   SCSI_UEXACT32 resetDelay;                 /* number of milliseconds to     */
                                             /* delay after a SCSI reset      */
   SCSI_UEXACT32 ioChannelFailureTimeout;    /* maximum delay before declaring */
                                             /* I/O channel hung - in         */
                                             /* millisecs.                    */
   SCSI_UEXACT16 numberHWScbs;               /* number of hw scbs present     */
   SCSI_UEXACT16 multipleLun;                /* multiple lun support          */

   SCSI_UEXACT8 cmdCompleteFactorThreshold;  /* cmnd complete interrupt factor threshold */
   SCSI_UEXACT8 cmdCompleteThresholdCount;   /* cmnd complete interrupt threshold count  */

   SCSI_UEXACT8 domainValidationMethod;      /* specifies domain validation   */
                                             /* method currently used.        */
   SCSI_UEXACT8 freezeEventReason;           /* reason for generating freeze  */
                                             /* event. Uses the event value   */
                                             /* defines.                      */ 
   SCSI_DEVICE deviceTable[SCSI_MAXDEV];     /* device configuration table    */
   SCSI_HHCB_RESERVE hhcbReserve;            /* reserved for internal usage   */
   void SCSI_HPTR osdHandle;                 /* handle for OSD's convenience  */

#if SCSI_TARGET_OPERATION
   SCSI_HHCB_TARGET_MODE targetMode;         /* Specific for Target Mode      */
#endif /* SCSI_TARGET_OPERATION */ 
   SCSI_UEXACT8 wrtBiasCtl;                  /* Write Bias Cancelation Control */
                                             /* This is a blind write register */
                                             /* software must maintain state   */
#if SCSI_OEM1_SUPPORT
   union
   {
      SCSI_UEXACT8 u8;
      struct 
      {
         SCSI_UEXACT8 adapterQASDisable:1;      /* No QAS for this adapter*/
         SCSI_UEXACT8 reserved:7;              /* reserved for future use              */
      } bits;
   } flags3;   
   
   SCSI_UEXACT8 alignment3[2];
#else
   SCSI_UEXACT8 alignment3[3];
#endif /* SCSI_OEM1_SUPPORT */
#if SCSI_ASPI_SUPPORT
   SCSI_BUS_ADDRESS busAddress;
#endif /* SCSI_ASPI_SUPPORT */
#if SCSI_BEF_SUPPORT
   SCSI_UEXACT8 OSM_ha_num;
#endif
};

#define  SCSI_DEVICE_TABLE(hhcb)   (hhcb)->deviceTable

/* definitions for firmware mode */
#define  SCSI_FMODE_STANDARD_U320      0     /* standard ultra 320 scb mode   */
#define  SCSI_FMODE_DOWNSHIFT_U320     1     /* downshift ultra 320 scb mode  */
#define  SCSI_FMODE_STANDARD_ENHANCED_U320 2 /* standard enhanced ultra 320   */
#define  SCSI_FMODE_DOWNSHIFT_ENHANCED_U320  3  /* downshift enhanced ultra 320     */
#define  SCSI_FMODE_DCH_U320           4     /* DCH ultra 320 scb mode        */
#define  SCSI_FMODE_NOT_ASSIGNED       5
/* definitions for hardware mode */
#define  SCSI_HMODE_AICU320            0     /* U320 hardware                 */

/* definitions for OEM customized hardware support flag */
#define  SCSI_OEMHARDWARE_NONE   0           /* Not applicable                */
#define  SCSI_OEMHARDWARE_OEM1   1           /* OEM hardware support          */

/* definitions for cmdCompleteFactorThreshold and cmdCompleteThresholdCount   */
/* Default cmd complete factor threshold and threshold count value.           */
/* Each sequencer's instruction execution time is 50-100ns (10/20 MIPS). */
/* We want to wait for approximately 5us in the sequencer idle loop      */
/* before generating command complete interrupt. There are 17            */
/* instructions repetitively executed in this idle loop if there is no   */
/* I/O activity. Take that into the account and pick the 50ns for each   */
/* instruction.  We come up with the interrupt threshold timeout value   */
/* to be 6. After running the benchmark, the value was changed to 24.    */
#define  SCSI_CMD_COMPLETE_FACTOR_THRESHOLD    24
/* With Benchmarking result, we find out if we reach the command count        */
/* to 3 then we generate interrupt, we get best performance for heavy         */
/* sequential data transfers                                                  */
#define  SCSI_CMD_COMPLETE_THRESHOLD_COUNT     3

/* definitions for the selection timeout delay */
#define  SCSI_256MS_SELTO_DELAY          0
#define  SCSI_128MS_SELTO_DELAY          1 
#define  SCSI_64MS_SELTO_DELAY           2
#define  SCSI_32MS_SELTO_DELAY           3


#if SCSI_EFI_BIOS_SUPPORT
#define  SCSI_NULL_HHCB         ((SCSI_HHCB SCSI_HPTR) NULL)
#else
#define  SCSI_NULL_HHCB         ((SCSI_HHCB SCSI_HPTR) -1)
#endif /* SCSI_EFI_BIOS_SUPPORT */


#define  SCSI_HF_scsiParity            flags.bits.scsiParity
#define  SCSI_HF_resetSCSI             flags.bits.resetSCSI
#define  SCSI_HF_multiTaskLun          flags.bits.multiTaskLun
#define  SCSI_HF_primaryTermLow        flags.bits.primaryTermLow
#define  SCSI_HF_primaryTermHigh       flags.bits.primaryTermHigh
#define  SCSI_HF_primaryAutoTerm       flags.bits.primaryAutoTerm
#define  SCSI_HF_secondaryTermLow      flags.bits.secondaryTermLow
#define  SCSI_HF_secondaryTermHigh     flags.bits.secondaryTermHigh
#define  SCSI_HF_secondaryAutoTerm     flags.bits.secondaryAutoTerm
#define  SCSI_HF_selTimeout            flags.bits.selTimeout
#define  SCSI_HF_expSupport            flags.bits.expSupport
#define  SCSI_HF_cacheThEn             flags.bits.cacheThEn
#define  SCSI_HF_SgBusAddress32        flags.bits.SgBusAddress32
#define  SCSI_HF_multiFunction         flags.bits.multiFunction
#define  SCSI_HF_chipReset             flags2.bits.chipReset
#define  SCSI_HF_separateRWThreshold   flags2.bits.separateRWThreshold
#define  SCSI_HF_separateRWThresholdEnable flags2.bits.separateRWThresholdEnable
#define  SCSI_HF_OverrideOSMNVRAMRoutines  flags2.bits.OverrideOSMNVRAMRoutines
#define  SCSI_HF_dvThrottleOSMReset    flags2.bits.dvThrottleOSMReset
#define  SCSI_HF_dontSaveSeqInState    flags2.bits.dontSaveSeqInState
#define  SCSI_HF_clusterEnable         flags2.bits.clusterEnable
#define  SCSI_HF_targetMode            flags2.bits.targetMode
#define  SCSI_HF_initiatorMode         flags2.bits.initiatorMode
#define  SCSI_HF_terminationLevel      flags2.bits.terminationLevel
#define  SCSI_HF_SuppressNegoSaveRestoreState  flags2.bits.SuppressNegoSaveRestoreState
#define  SCSI_HF_ClearConfigurationStatus  flags.bits.clearConfigurationStatus
#define  SCSI_HF_ForceSTPWLEVELforEmbeddedChips  flags2.bits.ForceSTPWLEVELForEmbedded
#define  SCSI_HF_busHung               flags2.bits.busHung
#define  SCSI_HF_switchHappened        flags2.bits.switchHappened
#define  SCSI_HF_chipInitialized       flags2.bits.chipInitialized
#define  SCSI_HF_autoTermDetected      flags2.bits.autoTermDetected

#if SCSI_OEM1_SUPPORT
#define  SCSI_HF_adapterQASDisable     flags3.bits.adapterQASDisable
#endif /* SCSI_OEM1_SUPPORT */
#if SCSI_BIOS_SUPPORT

#define  SCSI_HF_biosRunTimeEnabled    flags2.bits.biosRunTimeEnabled
#define  SCSI_SET_BIOS_RUN_TIME(hhcb,value)  (hhcb)->SCSI_HF_biosRunTimeEnabled=(value)
#else
#define  SCSI_SET_BIOS_RUN_TIME(hhcb,value)         
#endif /* SCSI_BIOS_SUPPORT */         

/***************************************************************************
* HIOB structures definitions
***************************************************************************/
struct SCSI_HIOB_
{
   SCSI_UNIT_HANDLE unitHandle;        /* device task set handle        */
   SCSI_UEXACT8 cmd;                   /* command code                  */
   SCSI_UEXACT8 stat;                  /* state                         */
   SCSI_UEXACT8 haStat;                /* host adapter status           */
   SCSI_UEXACT8 trgStatus;             /* target status                 */
   SCSI_UEXACT16 scbNumber;            /* scb number assigned           */
   union
   {
      SCSI_UEXACT16 u16;
      struct
      {
         SCSI_UEXACT16 noUnderrun:1;
         SCSI_UEXACT16 tagEnable:1;
         SCSI_UEXACT16 disallowDisconnect:1;
         SCSI_UEXACT16 autoSense:1;  
         SCSI_UEXACT16 freezeOnError:1;
#if SCSI_PACKETIZED_IO_SUPPORT
         SCSI_UEXACT16 tmfValid:1;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_TARGET_OPERATION  
         SCSI_UEXACT16 taskManagementResponse:1;
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_NEGOTIATION_PER_IOB
         SCSI_UEXACT16 forceSync:1;
         SCSI_UEXACT16 forceAsync:1;
         SCSI_UEXACT16 forceWide:1;
         SCSI_UEXACT16 forceNarrow:1;
         SCSI_UEXACT16 forceReqSenseNego:1;
#endif /* SCSI_NEGOTIATION_PER_IOB */
#if SCSI_PARITY_PER_IOB
         SCSI_UEXACT16 parityEnable:1;
#endif /* SCSI_PARITY_PER_IOB */

#if SCSI_DOMAIN_VALIDATION
         SCSI_UEXACT16 dvIOB:1;
#endif /* SCSI_DOMAIN_VALIDATION */
#if SCSI_RAID1
         SCSI_UEXACT16  raid1:1;
         SCSI_UEXACT16  raid1Primary:1;
#endif /* SCSI_RAID1 */
      } bits;
   } flags;
#if SCSI_BIOS_MODE_SUPPORT
   SCSI_BUS_ADDRESS snsBuffer;
#else
   void SCSI_IPTR snsBuffer;              /* sense buffer                  */
#endif /* SCSI_BIOS_MODE_SUPPORT */
   SCSI_UEXACT8 snsLength;                /* sense length                  */
   SCSI_UEXACT8 snsResidual;              /* sense residual                */
#if SCSI_TARGET_OPERATION  
/* Protect by compile option so that size of initiator mode HIOB is not    */
/* increased unnecessarily.                                                */  
/* Target Mode fields */ 
   SCSI_UEXACT8 initiatorStatus;          /* scsi status for initiator     */
                                          /* reestablish and complete      */
                                          /* target mode I/O.              */
   SCSI_UEXACT8 reserved2[3];             /* reserved for alignment        */
   SCSI_UEXACT16 snsBufferSize;           /* size of the snsBuffer, which  */
                                          /* is used to store the command  */
                                          /* for target mode establish     */
                                          /* connection Iobs.              */  
   SCSI_UEXACT32 ioLength;                /* length of the I/O request.    */
                                          /* For target mode this is the   */
                                          /* length of the I/O for this    */ 
                                          /* SCB only.                     */ 
   SCSI_UEXACT32 relativeOffset;          /* offset (in bytes) from the    */
                                          /* start of the client buffer    */ 
#else                                            
   SCSI_UEXACT8 reserved2[2];             /* reserved for alignment        */
#endif /* SCSI_TARGET_OPERATION */   
#if SCSI_RAID1 
   SCSI_HIOB  SCSI_IPTR mirrorHiob;       /* sister Hiob used for error handling */
#endif /* SCSI_RAID1 */

   SCSI_UEXACT32 residualLength;          /* residual length               */
#if !SCSI_BIOS_SUPPORT
   SCSI_SCB_DESCRIPTOR SCSI_HPTR scbDescriptor; /* scb descriptor          */
#endif /* !SCSI_BIOS_SUPPORT */
#if SCSI_RESOURCE_MANAGEMENT
   SCSI_HIOB SCSI_IPTR queueNext;         /* pointer to next in queue      */
#endif /* SCSI_RESOURCE_MANAGEMENT */
   SCSI_HIOB_RESERVE hiobReserve;         /* reserved for internal usage   */
#if !SCSI_BIOS_SUPPORT
   SCSI_UINT8 priority;
#endif /* !SCSI_BIOS_SUPPORT */
#if SCSI_SELTO_PER_IOB
   SCSI_UEXACT8 seltoPeriod;              /* in milliseconds */
#endif /* SCSI_SELTO_PER_IOB */                                    
#if SCSI_PACKETIZED_IO_SUPPORT
   SCSI_UEXACT8 tmfStatus;                /* TMF status, error info byte   */                                          
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
};

/* definitions for flags */
#define  SCSI_IF_noUnderrun         flags.bits.noUnderrun  
#define  SCSI_IF_tagEnable          flags.bits.tagEnable   
#define  SCSI_IF_disallowDisconnect flags.bits.disallowDisconnect
#define  SCSI_IF_autoSense          flags.bits.autoSense   
#define  SCSI_IF_freezeOnError      flags.bits.freezeOnError
#if SCSI_PACKETIZED_IO_SUPPORT
#define  SCSI_IF_tmfValid           flags.bits.tmfValid
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_TARGET_OPERATION
/* This flag is used to indicate if the SCSI_CMD_REESTABLISH_AND_COMPLETE HIOB
   is a response to a task management function. Responses to TMF's do NOT
   result in an interrupt (no SCB used - the scsi bus is simply released)
   therefore this flag indcates that the IOB post routine must be called
   receipt of the HIOB from the HWM. */
#define  SCSI_IF_taskManagementResponse flags.bits.taskManagementResponse
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_NEGOTIATION_PER_IOB
#define  SCSI_IF_forceSync          flags.bits.forceSync   
#define  SCSI_IF_forceAsync         flags.bits.forceAsync   
#define  SCSI_IF_forceWide          flags.bits.forceWide
#define  SCSI_IF_forceNarrow        flags.bits.forceNarrow
#define  SCSI_IF_forceReqSenseNego  flags.bits.forceReqSenseNego
#endif /* SCSI_NEGOTIATION_PER_IOB */
#if SCSI_PARITY_PER_IOB
#define  SCSI_IF_parityEnable       flags.bits.parityEnable
#endif /* SCSI_PARITY_PER_IOB */
#if SCSI_DOMAIN_VALIDATION
#define  SCSI_IF_dvIOB              flags.bits.dvIOB
#endif /* SCSI_DOMAIN_VALIDATION */
#if SCSI_RAID1
#define  SCSI_IF_raid1              flags.bits.raid1
#define  SCSI_IF_raid1Primary       flags.bits.raid1Primary
#endif /* SCSI_RAID1 */

#define  SCSI_TARGET_UNIT(hiob)     (hiob)->unitHandle.targetUnit
#define  SCSI_INITIATOR_UNIT(hiob)  (hiob)->unitHandle.initiatorUnit
#define  SCSI_RELATED_HIOB(hiob)    (hiob)->unitHandle.relatedHiob
#if SCSI_TARGET_OPERATION
#define  SCSI_NEXUS_UNIT(hiob)      (hiob)->unitHandle.nexus
#endif /* SCSI_TARGET_OPERATION */

/* Use snsBuffer to store the target unit for the active HIOB for
 * Bus Reset request.
 */
#define  SCSI_ACTIVE_TARGET(hiob)   ((hiob)->snsBuffer)
#if  SCSI_TARGET_OPERATION 
#define  SCSI_CMD_BUFFER(hiob)      ((hiob)->snsBuffer)
#define  SCSI_CMD_LENGTH(hiob)      ((hiob)->snsLength)
#define  SCSI_CMD_SIZE(hiob)        ((hiob)->snsBufferSize)
#define  SCSI_DATA_IOLENGTH(hiob)   ((hiob)->ioLength)
#define  SCSI_DATA_OFFSET(hiob)     ((hiob)->relativeOffset)
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_EFI_BIOS_SUPPORT
#define  SCSI_NULL_HIOB             ((SCSI_HIOB SCSI_IPTR) NULL)
#else
#define  SCSI_NULL_HIOB             ((SCSI_HIOB SCSI_IPTR) -1)
#endif /* SCSI_EFI_BIOS_SUPPORT */


/****************************************************************************
* definitions for cmd of HIOB 
****************************************************************************/
#define  SCSI_CMD_INITIATE_TASK     0  /* Standard SCSI command               */
#define  SCSI_CMD_ABORT_TASK        1  /* abort task                          */
#define  SCSI_CMD_RESET_BUS         2  /* reset protocol channel/bus          */
#define  SCSI_CMD_RESET_TARGET      3  /* reset target device                 */
#define  SCSI_CMD_RESET_HARDWARE    4  /* reset hardware                      */
#define  SCSI_CMD_PROTO_AUTO_CFG    6  /* protocol auto config                */
#define  SCSI_CMD_ABORT_TASK_SET    7  /* abort task set                      */
#define  SCSI_CMD_RESET_BOARD       8  /* reset board                         */
#define  SCSI_CMD_UNFREEZE_QUEUE    9  /* unfreeze device queue               */
#if SCSI_TARGET_OPERATION
#define  SCSI_CMD_ESTABLISH_CONNECTION 10      /* establish connection iob    */
#define  SCSI_CMD_REESTABLISH_INTERMEDIATE 11  /* reestablish intermediate    */
#define  SCSI_CMD_REESTABLISH_AND_COMPLETE 12  /* reestablish and complete    */
#define  SCSI_CMD_ABORT_NEXUS      13  /* abort nexus                         */  
#if SCSI_MULTIPLEID_SUPPORT
#define  SCSI_CMD_ENABLE_ID        14  /* enable target id(s)                 */        
#define  SCSI_CMD_DISABLE_ID       15  /* disable target id(s)                */
#endif /* SCSI_MULTIPLEID_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */
#define  SCSI_CMD_LOGICAL_UNIT_RESET 16 /* logical unit reset                 */

/****************************************************************************
* definitions for stat of HIOB
****************************************************************************/
#define  SCSI_SCB_PENDING           0  /* SCSI request in progress            */
#define  SCSI_SCB_COMP              1  /* SCSI request completed no error     */
#define  SCSI_SCB_ABORTED           2  /* SCSI request aborted                */
#define  SCSI_SCB_ERR               3  /* SCSI request completed with error   */
#define  SCSI_SCB_INV_CMD           4  /* Invalid SCSI request                */
#define  SCSI_SCB_FROZEN            5  /* SCSI request pulled out from Queue  */
#if SCSI_TARGET_OPERATION
#define  SCSI_TASK_CMD_COMP         6  /* Task Command Complete               */
#endif /* SCSI_TARGET_OPERATION */

/***************************************************************************
* definitions for haStat of HIOB
****************************************************************************/
#define  SCSI_HOST_NO_STATUS    0x00   /* No adapter status available         */
#define  SCSI_HOST_ABT_HOST     0x04   /* Command aborted by host             */
#define  SCSI_HOST_ABT_HA       0x05   /* Command aborted by host adapter     */
#define  SCSI_HOST_ABT_BUS_RST  0x06   /* Command aborted by bus reset        */
#define  SCSI_HOST_ABT_3RD_RST  0x07   /* Command aborted by 3rd party reset  */
#define  SCSI_HOST_ABT_TRG_RST  0x08   /* Command aborted by target reset     */
#define  SCSI_HOST_ABT_IOERR    0x09   /* Command aborted by io error         */
#define  SCSI_HOST_ABT_LUN_RST  0x0A   /* Command aborted by target reset     */

#define  SCSI_HOST_ABT_PACKNONPACK 0x10 /* Command aborted by pack to non pack switch */
#define  SCSI_HOST_SEL_TO       0x11   /* Selection timeout                   */
#define  SCSI_HOST_DU_DO        0x12   /* Data overrun/underrun error         */
#define  SCSI_HOST_BUS_FREE     0x13   /* Unexpected bus free                 */
#define  SCSI_HOST_PHASE_ERR    0x14   /* Target bus phase sequence error     */
#define  SCSI_HOST_ABORTED_PCI_ERROR  0x15  /* PCI error detected             */
#define  SCSI_HOST_ABORTED_PCIX_SPLIT_ERROR 0x16 /* PCI-X Split error detected */
#define  SCSI_HOST_INV_LINK     0x17   /* Invalid SCSI linking operation      */
#define  SCSI_HOST_PCI_OR_PCIX_ERROR 0x18 /* Active during PCI or PCI-X error */

#define  SCSI_HOST_SNS_FAIL     0x1b   /* Auto-request sense failed           */
#define  SCSI_HOST_TAG_REJ      0x1c   /* Tagged Queuing rejected by target   */
#define  SCSI_HOST_HW_ERROR     0x20   /* Host adpater hardware error         */
#define  SCSI_HOST_ABT_FAIL     0x21   /* Target did'nt respond to ATN (RESET)*/
#define  SCSI_HOST_RST_HA       0x22   /* SCSI bus reset by host adapter      */
#define  SCSI_HOST_RST_OTHER    0x23   /* SCSI bus reset by other device      */
#define  SCSI_HOST_ABT_NOT_FND  0x24   /* Command to be aborted not found     */
#define  SCSI_HOST_ABT_STARTED  0x25   /* Abort is in progress                */
#define  SCSI_HOST_ABT_CMDDONE  0x26   /* Command to be aborted has already   */
                                       /* completed on the I/O channel        */
#if SCSI_DATA_IN_RETRY_DETECTION
#define  SCSI_HOST_READ_RETRY   0x27   /* The target peformed some kind of    */
                                       /* automatic retry on a data-in I/O.   */
                                       /* For example, an intermediate        */
                                       /* disconnect without a save data      */
                                       /* pointers message.                   */   
#endif /* SCSI_DATA_IN_RETRY_DETECTION */     
#define  SCSI_HOST_NOAVL_INDEX  0x30   /* SCSI bus reset by other device      */
#define  SCSI_HOST_DETECTED_ERR 0x48   /* Initiator detected error (parity)   */

#if SCSI_TARGET_OPERATION
#define  SCSI_INITIATOR_PARITY_MSG  0x49  /* Initiator message parity message    */ 
#define  SCSI_INITIATOR_PARITY_ERR_MSG  0x4a  /* Initiator detected parity message     */ 
#define  SCSI_INITIATOR_INVALID_MSG 0x4b  /* Invalid message recieved from initiator */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_HOST_IU_STATE_CHANGE  0x4C  /* Initiator selection resulted in  */
                                          /* target IU state being changed,   */
                                          /* either enabled or disabled.      */
#define  SCSI_HOST_LQ_CRC_ERR       0x4D  /* CRC error on LQ IU was detected  */
                                          /* by the ASIC.                     */
#define  SCSI_HOST_CMD_CRC_ERR      0x4E  /* CRC error on CMD IU received was */
                                          /* detected by the ASIC.            */
#define  SCSI_HOST_BAD_LQ_TYPE_ERR  0x4F  /* Bad LQ TYPE field.               */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */ 
#define  SCSI_HOST_MSG_REJECT       0x50  /* Initiator message reject         */ 
#define  SCSI_HOST_IDENT_RSVD_BITS  0x51  /* Reserved bits set in Identify    */
                                          /* message - msg reject issued      */
#define  SCSI_HOST_TRUNC_CMD        0x52  /* Truncated SCSI Command           */
#define  SCSI_HOST_ABORT_TASK       0x53  /* Abort Task message recieved      */
#define  SCSI_HOST_TERMINATE_TASK   0x54  /* Terminate I/O  message recieved  */
#define  SCSI_HOST_ABORT_TASK_SET   0x55  /* Abort Task Set message recieved  */
#define  SCSI_HOST_LUN_RESET        0x56  /* Reset Port message recieved      */
#define  SCSI_HOST_CLEAR_ACA        0x57  /* Clear ACA message recieved       */ 
#define  SCSI_HOST_TARGET_RESET     0x58  /* Reset Target message recieved    */
#define  SCSI_HOST_CLEAR_TASK_SET   0x59  /* Clear Task Set message recieved  */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_HOST_ABT_IU_REQ_CHANGE 0x5A /* Aborted due to IU_REQ change     */
#define  SCSI_HOST_IU_SEL_ATN       0x5B  /* Initiator selection with ATN     */
                                          /* occurred while IUs were enabled. */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#define  SCSI_HOST_IDENT_LUNTAR_BIT 0x60  /* Luntar bit set in identify       */
                                          /* message - msg reject issued      */ 
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_HOST_ABT_CHANNEL_FAILED  0x61 /* Aborted due to unrecoverable SCSI bus failure */
#if SCSI_TARGET_OPERATION
#define  SCSI_HOST_FREE_RESOURCES   0x62    /* Catch all - means host should
                                             * ignore this request and free any
                                             * resources, for example, nexus & 
                                             * scb. HWM went to bus free.
                                             */
#endif /* SCSI_TARGET_OPERATION */
#define  SCSI_HOST_PROTOCOL_ERROR   0x71    /* Initiator detects protocol error on target    */

/****************************************************************************
* Definitions for target status
****************************************************************************/
#define  SCSI_UNIT_GOOD      0x00         /* Good status or none available    */
#define  SCSI_UNIT_CHECK     0x02         /* Check Condition                  */
#define  SCSI_UNIT_MET       0x04         /* Condition met                    */
#define  SCSI_UNIT_BUSY      0x08         /* Target busy                      */
#define  SCSI_UNIT_INTERMED  0x10         /* Intermediate command good        */
#define  SCSI_UNIT_INTMED_GD 0x14         /* Intermediate condition met       */
#define  SCSI_UNIT_RESERV    0x18         /* Reservation conflict             */
#define  SCSI_UNIT_QUEFULL   0x28         /* Queue Full                       */
#define  SCSI_UNIT_ACA_ACTIVE 0x30        /* ACA Active                       */
#define  SCSI_UNIT_TASK_ABTED 0x40        /* Task Aborted by another          */
                                          /* initiator and Control mode page  */
                                          /* TAS bit is a 1.                  */

/****************************************************************************
* Definitions for Task Attribute field of Command IU
****************************************************************************/
#define  SCSI_SIMPLE_TASK_ATTRIBUTE         0x00  /* Simple Task */  
#define  SCSI_HEAD_OF_QUEUE_TASK_ATTRIBUTE  0x01  /* Head of Queue Task */
#define  SCSI_ORDERED_TASK_ATTRIBUTE        0x02  /* Ordered Task */
#define  SCSI_ACA_TASK_ATTRIBUTE            0x04  /* ACA Task */

/****************************************************************************
*  Definitions for task managment flags of Command IU
****************************************************************************/
#define  SCSI_NO_TASK_REQ     0x00        /* no task management requests      */
#define  SCSI_ABORT_TASK      0x01        /* abort the task                   */
#define  SCSI_ABORT_TASK_SET  0x02        /* abort the task set               */
#define  SCSI_CLEAR_TASK_SET  0x04        /* clear the task set               */
#define  SCSI_LUN_RESET       0x08        /* logical unit reset               */
#define  SCSI_TARGET_RESET    0x20        /* target reset                     */
#define  SCSI_CLEAR_ACA       0x40        /* clear ACA                        */

/****************************************************************************
* Definitions for SPI Status IU
****************************************************************************/
#define  SCSI_MAX_STATUS_IU_SIZE            268   /* Maximum number of 
                                                   * bytes for SPI status.
                                                   * Calculated as follows:
                                                   * 12 bytes of standard info
                                                   * 252 - SENSE DATA LIST LENGTH
                                                   * 4 - PACKETIZED FAILURES LIST 
                                                   *     LENGTH. 
                                                   */
/* Packetized Failure Code Field */
#define  SCSI_PFC_NO_FAILURE                 0x00  /* No Failure            */
#define  SCSI_PFC_SPI_CMD_IU_FIELDS_INVALID  0x02  /* SPI Command Information Unit
                                                    * fields invalid
                                                    */
#define  SCSI_PFC_TMF_NOT_SUPPORTED          0x04  /* Task Management Function
                                                    * Not Supported
                                                    */
#define  SCSI_PFC_TMF_FAILED                 0x05  /* Task Management Function
                                                    * failed
                                                    */
#define  SCSI_PFC_INV_TYPE_CODE_IN_LQ        0x06  /* Invalid Type Code field 
                                                    * received in SPI L_Q
                                                    * Information Unit
                                                    */
#define  SCSI_PFC_ILLEGAL_REQUEST_IN_LQ      0x07  /* Illegal Request received 
                                                    * in SPI L_Q Informantion Unit
                                                    */
                                                     
#if SCSI_TARGET_OPERATION
/****************************************************************************
* Definitions for SCSI messages
****************************************************************************/
#define  SCSI_QUEUE_TAG_MASK                 0xDF  /* Mask of Queue tag type */

/****************************************************************************
* Definitions for SCSI CDB Fields and masks
****************************************************************************/
#define  SCSI_CDB_LUNFIELD_MASK   0xE0    /* Mask for byte 1 Lun field        */

#endif /* SCSI_TARGET_OPERATION */

/****************************************************************************
* SCSI_HW_INFORMATION structure
****************************************************************************/
struct SCSI_HW_INFORMATION_
{
   SCSI_UEXACT8 WWID[SCSI_SIZEWWID];         /* World Wide ID                 */
   SCSI_UEXACT8 hostScsiID;                  /* Host adapter SCSI ID          */
   SCSI_UEXACT8 threshold;                   /* Host adapter DFIFO threshold  */
   SCSI_UEXACT8 writeThreshold;              /* FIFO write data threshold     */
   SCSI_UEXACT8 readThreshold;               /* FIFO read data threshold      */
   union
   {
      SCSI_UEXACT16 u16;
      struct 
      {
         SCSI_UEXACT16 wideSupport:1;          /* Adapter supports wide xfers    */
         SCSI_UEXACT16 scsiParity:1;           /* SCSI parity error              */
         SCSI_UEXACT16 selTimeout:2;           /* Selection timeout              */
         SCSI_UEXACT16 transceiverMode:2;      /* Transceiver mode               */
         SCSI_UEXACT16 separateRWThresholdEnable:1; /* Separate threshold enable */
         SCSI_UEXACT16 separateRWThreshold:1;  /* Adapter supports separate FIFO */
                                               /* thresholds                     */ 
         SCSI_UEXACT16 cacheThEn:1;            /* Current state of CACHETEN bit  */
         SCSI_UEXACT16 disconnectDelaySupport:1;  /* Firmware supports request   */
                                               /* to delay prior to ACKing       */
                                               /* disconnect/command complete    */
                                               /* messages.                      */ 
         SCSI_UEXACT16 adjustTargetOnly:1;     /* Indicates that only the target */
                                               /* related fields are to be       */
                                               /* updated. Non-target releated   */
                                               /* fields are ignored.            */
         SCSI_UEXACT16 intrThresholdSupport:1; /* Interrupt threshold is         */
                                               /* supported.                     */
      } bits;
   } flags;
   struct
   {
      SCSI_UINT16 maxTagScbs;                  /* Max tagged scbs per target     */
      SCSI_UEXACT8 xferRate[SCSI_MAX_XFER_PARAMETERS]; /* Transfer Rate for the target */
      union
      {
         SCSI_UEXACT16 u16;
         struct 
         {
            SCSI_UEXACT16 scsiOptChanged:1;  /* Scsi option changed using adjust profile */
            SCSI_UEXACT16 dtcEnable:1;       /* DTC with CRC xfer enable */
            SCSI_UEXACT16 ptclOptMaskEnable:1;/*Use protocol option mask field */
            SCSI_UEXACT16 adapterPreCompEn:1;/*Target requested Adapter PPR PCOMP */
#if SCSI_ELEC_PROFILE
            SCSI_UEXACT16 elecOptChanged:1;  /* Elec option changed with adapter profile */
#endif /* SCSI_ELEC_PROFILE */
         } bits;
      } flags;
      SCSI_UEXACT16 SCSIMaxSpeed;            /* Maximum speed for profile  */
      SCSI_UEXACT16 SCSICurrentSpeed;        /* Current speed for profile  */
      SCSI_UEXACT16 SCSIDefaultSpeed;        /* Default speed for profile  */
      SCSI_UEXACT8 SCSIMaxOffset;            /* Maximum offset for profile */
      SCSI_UEXACT8 SCSICurrentOffset;        /* Current offset for profile */
      SCSI_UEXACT8 SCSIDefaultOffset;        /* Default offset for profile */
      SCSI_UEXACT8 SCSIMaxWidth;             /* Maximum width for profile  */
      SCSI_UEXACT8 SCSICurrentWidth;         /* Current width for profile  */
      SCSI_UEXACT8 SCSIDefaultWidth;         /* Default width for profile  */
      SCSI_UEXACT8 SCSICurrentProtocolOption;/* Current Protocol for profile */
      SCSI_UEXACT8 SCSIDefaultProtocolOption;/* Default Protocol for profile */
      SCSI_UEXACT8 SCSIMirrorTarget;         /* Current Mirror Target */
      SCSI_UINT32  SCSIDefaultProtocolOptionMask;
      SCSI_UINT32  SCSICurrentProtocolOptionMask;
      SCSI_UINT32  SCSIMaxProtocolOptionMask;
#if SCSI_ELEC_PROFILE
      SCSI_UEXACT8 SCSIMaxSlewRate;
      SCSI_UEXACT8 SCSIMaxAmplitudeLevel;
#endif /* SCSI_ELEC_PROFILE */

   } targetInfo[SCSI_MAXDEV];   
   SCSI_UEXACT16 minimumSyncSpeed;           /* Minimum speed for sync xfer */
   SCSI_UEXACT8 intrFactorThreshold;         /* Factor threshold for        */
                                             /* interrupt posting.          */
   SCSI_UEXACT8 intrThresholdCount;          /* Threshold count for         */
                                             /* interrupt posting.          */
   SCSI_UEXACT8 disconnectDelay;             /* Disconnect delay in usecs   */
#if SCSI_TARGET_OPERATION
   SCSI_UEXACT16 targetIDMask;               /* Mask of SCSI IDs to which   */
                                             /* this adapter will respond   */
                                             /* as a target.                */
   SCSI_UEXACT8 group6CDBSize;               /* CDB group code 6 - Vendor   */
                                             /* Unique Command              */
   SCSI_UEXACT8 group7CDBSize;               /* CDB group code 7 - Vendor   */
                                             /* Unique Command              */
   SCSI_UEXACT16 dataGroupCrcInterval;       /* Data Group CRC Interval     */
#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
   SCSI_UEXACT16 iuCrcInterval;              /* IU CRC Interval             */
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
   union
   {
      SCSI_UEXACT16 u16;
      struct 
      {
         SCSI_UEXACT16 disconnectAllowed:1;   /* Disconnect after receiving */
                                              /* a new request if           */
                                              /* discPriv = 1.              */     
         SCSI_UEXACT16 enableScsi1Selection:1;/* Accept a SCSI-1 selection  */ 
                                              /* i.e. no identify message.  */
         SCSI_UEXACT16 identifyMsgRsv:1;      /* HIM responds with a message*/ 
                                              /* reject when identify       */
                                              /* message contains a non-zero*/
                                              /* value in reserved fields   */
                                              /* if operating as a SCSI-2   */
                                              /* target.                    */ 
         SCSI_UEXACT16 rejectLunTar:1;        /* HIM responds with a        */
                                              /* message reject when LUNTAR */
                                              /* bit set in identify        */
                                              /* message if operating as a  */
                                              /* SCSI-2 target.             */ 
         SCSI_UEXACT16 tagEnable:1;           /* HIM accepts tagged         */
                                              /* requests, otherwise,       */
                                              /* generate a message reject  */
                                              /* on receipt of a queue tag  */ 
                                              /* message.                   */ 
         SCSI_UEXACT16 reserved:10;           /* Reserved for expansion.    */
      } bits;
   } targetModeFlags;
   SCSI_UEXACT8 hostTargetAnsiVersion;        /* ANSI version for target    */
                                              /* mode operation.            */
                                              /* SCSI_2 or SCSI_3           */
#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
   SCSI_UEXACT8  alignment[1];                /* Padded for alignment       */ 
#else
   SCSI_UEXACT8  alignment[3];                /* Padded for alignment       */ 
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
#endif /* SCSI_TARGET_OPERATION */
};

#define  SCSI_PF_wideSupport                 flags.bits.wideSupport
#define  SCSI_PF_scsiParity                  flags.bits.scsiParity
#define  SCSI_PF_selTimeout                  flags.bits.selTimeout
#define  SCSI_PF_transceiverMode             flags.bits.transceiverMode 
#define  SCSI_PF_separateRWThresholdEnable   flags.bits.separateRWThresholdEnable
#define  SCSI_PF_separateRWThreshold         flags.bits.separateRWThreshold
#define  SCSI_PF_cacheThEn                   flags.bits.cacheThEn
#define  SCSI_PF_disconnectDelaySupport      flags.bits.disconnectDelaySupport
#define  SCSI_PF_adjustTargetOnly            flags.bits.adjustTargetOnly
#define  SCSI_PF_intrThresholdSupport        flags.bits.intrThresholdSupport

#define  SCSI_TF_scsiOptChanged              flags.bits.scsiOptChanged
#define  SCSI_TF_dtcEnable                   flags.bits.dtcEnable
#define  SCSI_TF_ptclOptMaskEnable           flags.bits.ptclOptMaskEnable
#define  SCSI_TF_adapterPreCompEn            flags.bits.adapterPreCompEn
#if SCSI_ELEC_PROFILE
#define  SCSI_TF_elecOptChanged              flags.bits.elecOptChanged
#endif /* SCSI_ELEC_PROFILE */

#if SCSI_TARGET_OPERATION
#define  SCSI_PF_disconnectAllowed           targetModeFlags.bits.disconnectAllowed
#define  SCSI_PF_enableScsi1Selection        targetModeFlags.bits.enableScsi1Selection
#define  SCSI_PF_identifyMsgRsv              targetModeFlags.bits.identifyMsgRsv
#define  SCSI_PF_rejectLunTar                targetModeFlags.bits.rejectLunTar
#define  SCSI_PF_tagEnable                   targetModeFlags.bits.tagEnable
#endif /* SCSI_TARGET_OPERATION */
/****************************************************************************
* Definitions for transceiverMode
****************************************************************************/
#define  SCSI_UNKNOWN_MODE       0           /* Mode unknown              */
#define  SCSI_LVD_MODE           1           /* Low Voltage Differential  */
#define  SCSI_SE_MODE            2           /* Single Ended              */
#define  SCSI_HVD_MODE           3           /* High Voltage Differential */

/****************************************************************************
* Definitions for Protocol Option
****************************************************************************/
#define  SCSI_PROTOCOL_OPTION_UNKNOWN  0
#define  SCSI_NO_PROTOCOL_OPTION       1
#define  SCSI_ST_DATA                  2
#define  SCSI_DT_DATA_WITH_CRC         3
#define  SCSI_DT_DATA_WITH_IU          4
#define  SCSI_DT_DATA_WITH_CRC_AND_QAS 5
#define  SCSI_DT_DATA_WITH_IU_AND_QAS  6

/****************************************************************************
* Definitions for Asynchronous Event
****************************************************************************/
#define  SCSI_AE_3PTY_RESET  1           /* 3rd party SCSI bus reset         */
#define  SCSI_AE_HAID_CHANGE 2           /* Host ID change due to SCAM       */
#define  SCSI_AE_HA_RESET    3           /* Host Adapter SCSI bus reset      */
#define  SCSI_AE_SELECTED    4           /* Host Adapter selected as target  */
#define  SCSI_AE_SCAM_SELD   5           /* SCAM selection detected          */
#define  SCSI_AE_OSMFREEZE   6           /* OSM Freeze                       */
#define  SCSI_AE_OSMUNFREEZE 7           /* OSM Unfreeze                     */
#define  SCSI_AE_IOERROR     8           /* IO Error                         */
#define  SCSI_AE_ABORTED_PCI_ERROR   10  /* detected PCI sbort error         */
#define  SCSI_AE_ABORTED_PCIX_SPLIT_ERROR 11 /* detected PCI parity error    */
#if SCSI_TARGET_OPERATION   
/* Target Mode Event defines */
#define  SCSI_AE_NEXUS_TSH_THRESHOLD 12  /* Target mode event. NexusTsh      */    
                                         /* threshold exceeded.              */  
#define  SCSI_AE_EC_IOB_THRESHOLD    13  /* Target mode event. Establish     */
                                         /* connection threshold exceeded.   */ 
#endif /* SCSI_TARGET_OPERATION */
#define  SCSI_AE_IO_CHANNEL_FAILED   14  /* Unrecoverable SCSI bus failure   */
/* SPLTINT EVENTs */

#if SCSI_TARGET_OPERATION
/***************************************************************************
* SCSI_NEXUS structures definitions
***************************************************************************/
struct SCSI_NEXUS_
{
   SCSI_HHCB SCSI_HPTR hhcb;                 /* host control block          */
   SCSI_NODE SCSI_NPTR node;                 /* node handle associated with */ 
                                             /* this nexus                  */ 
   SCSI_UEXACT8 scsiID;                      /* scsi id of initiator        */
   SCSI_UEXACT8 lunID;                       /* target lun id               */
   SCSI_UEXACT8 queueType;                   /* type of queuing             */
#if SCSI_MULTIPLEID_SUPPORT
   SCSI_UEXACT8 selectedID;                  /* the scsi id that we were    */ 
                                             /* selected as a target - used */
                                             /* when our adapter/chip is    */
                                             /* set up to respond to        */
                                             /* selection as multiple scsi  */
                                             /* ids.                        */     
#else
   SCSI_UEXACT8 reserved;                    /* padded for alignment        */
#endif /* SCSI_MULTIPLEID_SUPPORT */
   union
   {
      SCSI_UEXACT16 u16;
      struct 
      {  
         SCSI_UEXACT16 lunTar:1;              /* value of LunTar bit in      */
                                              /* identify message received   */
         SCSI_UEXACT16 disconnectAllowed:1;   /* value of discPriv bit in    */
                                              /* identify message received   */
         SCSI_UEXACT16 busHeld:1;             /* if 1, the SCSI bus is held  */
         SCSI_UEXACT16 tagRequest:1;          /* if 1, this is a tagged      */
                                              /* request                     */
         SCSI_UEXACT16 lastResource:1;        /* if 1 the last resource for  */
                                              /* receiving new requests has  */
                                              /* been used.                  */
         SCSI_UEXACT16 scsi1Selection:1;      /* if 1, this is a SCSI-1      */
                                              /* selection. I.e. there was   */
                                              /* no identify message         */
                                              /* received with the request   */     
#if SCSI_RESOURCE_MANAGEMENT
         SCSI_UEXACT16 available:1;           /* nexus is in the available   */ 
                                              /* pool */
#endif /* SCSI_RESOURCE_MANAGEMENT */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
         SCSI_UEXACT16 packetized:1;          /* packetized request          */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
      } bits;
   } nexusAttributes;
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
   SCSI_UEXACT16 queueTag;                   /* queue tag                   */
#else
   SCSI_UEXACT8 queueTag;                    /* queue tag                   */
   SCSI_UEXACT8 reserved1;                   /* padded for alignment        */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
   SCSI_NEXUS_RESERVE nexusReserve;
#if SCSI_RESOURCE_MANAGEMENT
   SCSI_NEXUS SCSI_XPTR nextNexus;           /* next nexus pointer          */  
#endif /* SCSI_RESOURCE_MANAGEMENT */
};   

#define  SCSI_XF_lunTar             nexusAttributes.bits.lunTar
#define  SCSI_XF_disconnectAllowed  nexusAttributes.bits.disconnectAllowed
#define  SCSI_XF_busHeld            nexusAttributes.bits.busHeld
#define  SCSI_XF_tagRequest         nexusAttributes.bits.tagRequest
#define  SCSI_XF_lastResource       nexusAttributes.bits.lastResource
#define  SCSI_XF_available          nexusAttributes.bits.available
#define  SCSI_XF_scsi1Selection     nexusAttributes.bits.scsi1Selection
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_XF_packetized         nexusAttributes.bits.packetized
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

#if SCSI_EFI_BIOS_SUPPORT
#define  SCSI_NULL_NEXUS    ((SCSI_NEXUS SCSI_XPTR)NULL)
#else
#define  SCSI_NULL_NEXUS    ((SCSI_NEXUS SCSI_XPTR)-1)
#endif /* SCSI_EFI_BIOS_SUPPORT */

#define  SCSI_GET_NODE(nexusHandle)   ((nexusHandle)->node)

/***************************************************************************
* SCSI_NODE structures definitions
***************************************************************************/
struct SCSI_NODE_
{
   SCSI_DEVICE SCSI_DPTR deviceTable;
   void SCSI_NPTR osmContext;          /* OSM set context        */ 
   SCSI_HHCB SCSI_HPTR hhcb;           /* host control block     */
   SCSI_UEXACT8 scsiID;                /* node scsi id           */
   SCSI_UEXACT8 reserved0[3];          /* alignment              */   
   SCSI_NODE_RESERVE nodeReserve;   
#if SCSI_RESOURCE_MANAGEMENT
   SCSI_NODE SCSI_NPTR nextNode;       /* next link              */ 
#endif /* SCSI_RESOURCE_MANAGEMENT */
 };

#if SCSI_EFI_BIOS_SUPPORT
#define  SCSI_NULL_NODE                ((SCSI_NODE SCSI_NPTR) NULL)
#else
#define  SCSI_NULL_NODE                ((SCSI_NODE SCSI_NPTR) -1)
#endif /* SCSI_EFI_BIOS_SUPPORT */

#define  SCSI_NODE_TABLE(hhcb)   (hhcb)->SCSI_HP_nodeTable;
 
#endif /* SCSI_TARGET_OPERATION */

/****************************************************************************
* Definitions for Asynchronous Event for command
****************************************************************************/
#define  SCSI_AE_FREEZEONERROR_START  1   /* H/W queue is frozen for target   */
#define  SCSI_AE_FREEZEONERROR_END    2   /* H/W queue is frozen for target   */
#if SCSI_PACKETIZED_IO_SUPPORT
#define SCSI_AE_PACKTONONPACK_DETECTED 3  /* pack to non packetize detected   */
#define SCSI_AE_PACKTONONPACK_END     4   /* pack to non packetize end freeze */
#define SCSI_AE_NONPACKTOPACK_END     5   /* non packetized to pack end freeze */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

/***************************************************************************
* SCSIhBadSeq Caller Identities
***************************************************************************/
#define  SCSI_BADSEQ_CALLID00      0   /* Used in BackEndISR of xlm/hwm       */
#define  SCSI_BADSEQ_CALLID01      1   /* Used in BackEndISR of xlm/hwm       */
#define  SCSI_BADSEQ_CALLID02      2   /* Used in BackEndISR of xlm/hwm       */
#define  SCSI_BADSEQ_CALLID03      3   /* Used in BackEndISR of xlm/hwm       */
#define  SCSI_BADSEQ_CALLID04      4   /* Used in BackEndISR of xlm and       */
                                       /* SCSIhCmdComplete of hwm             */
#define  SCSI_BADSEQ_CALLID05      5
#define  SCSI_BADSEQ_CALLID06      6
#define  SCSI_BADSEQ_CALLID07      7
#define  SCSI_BADSEQ_CALLID08      8
#define  SCSI_BADSEQ_CALLID09      9
#define  SCSI_BADSEQ_CALLID10     10
#define  SCSI_BADSEQ_CALLID11     11
#define  SCSI_BADSEQ_CALLID12     12
#define  SCSI_BADSEQ_CALLID13     13
#define  SCSI_BADSEQ_CALLID14     14
#define  SCSI_BADSEQ_CALLID15     15
#define  SCSI_BADSEQ_CALLID16     16
#define  SCSI_BADSEQ_CALLID17     17
#define  SCSI_BADSEQ_CALLID18     18
#define  SCSI_BADSEQ_CALLID19     19
#define  SCSI_BADSEQ_CALLID20     20
#define  SCSI_BADSEQ_CALLID21     21
#define  SCSI_BADSEQ_CALLID22     22
#define  SCSI_BADSEQ_CALLID23     23
#define  SCSI_BADSEQ_CALLID24     24
#define  SCSI_BADSEQ_CALLID25     25
#define  SCSI_BADSEQ_CALLID26     26
#define  SCSI_BADSEQ_CALLID27     27
#define  SCSI_BADSEQ_CALLID28     28
#define  SCSI_BADSEQ_CALLID29     29
#define  SCSI_BADSEQ_CALLID30     30
#define  SCSI_BADSEQ_CALLID31     31
#define  SCSI_BADSEQ_CALLID32     32
#define  SCSI_BADSEQ_CALLID33     33
#define  SCSI_BADSEQ_CALLID34     34  
#define  SCSI_BADSEQ_CALLID35     35
#define  SCSI_BADSEQ_CALLID36     36
#define  SCSI_BADSEQ_CALLID37     37
#define  SCSI_BADSEQ_CALLID38     38   /* Used in BackEndISR of xlm/hwm       */
#define  SCSI_BADSEQ_CALLID39     39
#define  SCSI_BADSEQ_CALLID40     40
#define  SCSI_BADSEQ_CALLID41     41
#define  SCSI_BADSEQ_CALLID42     42
#define  SCSI_BADSEQ_CALLID43     43
#define  SCSI_BADSEQ_CALLID44     44   /* Used in BackEndISR of xlm/hwm       */
#define  SCSI_BADSEQ_CALLID45     45
#define  SCSI_BADSEQ_CALLID46     46
#define  SCSI_BADSEQ_CALLID47     47
#define  SCSI_BADSEQ_CALLID48     48
#define  SCSI_BADSEQ_CALLID49     49
#define  SCSI_BADSEQ_CALLID50     50
#define  SCSI_BADSEQ_CALLID51     51
#define  SCSI_BADSEQ_CALLID52     52
#define  SCSI_BADSEQ_CALLID53     53
#define  SCSI_BADSEQ_CALLID54     54
#define  SCSI_BADSEQ_CALLID55     55
#define  SCSI_BADSEQ_CALLID56     56
#define  SCSI_BADSEQ_CALLID57     57
#define  SCSI_BADSEQ_CALLID58     58
#define  SCSI_BADSEQ_CALLID59     59
#define  SCSI_BADSEQ_CALLID60     60
#define  SCSI_BADSEQ_CALLID61     61
#define  SCSI_BADSEQ_CALLID62     62
#define  SCSI_BADSEQ_CALLID63     63
#define  SCSI_BADSEQ_CALLID64     64
#define  SCSI_BADSEQ_CALLID65     65
#define  SCSI_BADSEQ_CALLID66     66
#define  SCSI_BADSEQ_CALLID67     67
#define  SCSI_BADSEQ_CALLID68     68
#define  SCSI_BADSEQ_CALLID69     69
#define  SCSI_BADSEQ_CALLID70     70
#define  SCSI_BADSEQ_CALLID71     71
#define  SCSI_BADSEQ_CALLID72     72
#define  SCSI_BADSEQ_CALLID73     73
#define  SCSI_BADSEQ_CALLID74     74
#define  SCSI_BADSEQ_CALLID75     75
#define  SCSI_BADSEQ_CALLID76     76
#define  SCSI_BADSEQ_CALLID77     77
#define  SCSI_BADSEQ_CALLID78     78
#define  SCSI_BADSEQ_CALLID79     79
#define  SCSI_BADSEQ_CALLID80     80
#define  SCSI_BADSEQ_CALLID81     81
#define  SCSI_BADSEQ_CALLID82     82
#define  SCSI_BADSEQ_CALLID83     83
#define  SCSI_BADSEQ_CALLID84     84
#define  SCSI_BADSEQ_CALLID_NULL  255  /* Special value to indicate no callid */

/****************************************************************************/
/* specialized return codes and parameters                                  */
/****************************************************************************/
/* General results */
#define  SCSI_SUCCESS      0    
#define  SCSI_FAILURE      1

/* SCSI initialize */
#define  SCSI_SEQUENCER_FAILURE  1
#define  SCSI_TIMEOUT_FAILURE    2
#define  SCSI_BUS_HUNG_FAILURE   3
#define  SCSI_PCI_ERROR          4

/* SEEPROM rd/wr results */
#define  SCSI_UNSUPPORTED  1

/* Results from FrontEndISR */
#define  SCSI_NOTHING_PENDING    0
#define  SCSI_INTERRUPT_PENDING  1
#define  SCSI_LONG_INTERRUPT_PENDING   2

/* Test for Adapter/Target idle */
#define  SCSI_IDLE         1    
#define  SCSI_NOT_IDLE     0

/* Real mode driver exist */
#define  SCSI_EXIST        1
#define  SCSI_NOT_EXIST    0

/* Bios state */
#define  SCSI_BIOS_NOT_PRESENT 0 /* Bios is not present on the host adapter */
#define  SCSI_BIOS_ACTIVE      1 /* Bios scan scsi bus and hooked int13     */
#define  SCSI_BIOS_SCAN        2 /* Bios scan scsi bus but not hooked int3  */
#define  SCSI_BIOS_NOT_SCAN    3 /* Bios does not scan scsi bus             */

/* Precomp Cutback Percentage Levels Supported */
#define SCSI_PCOMP_37             37
#define SCSI_PCOMP_29             29
#define SCSI_PCOMP_17             17
#define SCSI_PCOMP_OFF             0 

/****************************************************************************
* Misc. Definitions 
****************************************************************************/
/* Define used to indicate the ASIC has failed in some way. Will
 * normally result in a SCSIhBadSeq call.
 */
#define  SCSI_ASIC_FAILURE  ((SCSI_UEXACT8)-1)

/* Minimum number of micro seconds used for delays */
#define  SCSI_MINIMUM_DELAY_COUNT  5     /* Currently set to 5 microseconds */

/* General define representing a 2 second delay before deciding to time out an
 * operation that is waiting on a SCSI signal change or an ASIC register state
 * change.
 */ 
#if SCSI_SIMULATION_SUPPORT
/* Reduce the delay as we expect faster signal changes. */
#define SCSI_REGISTER_TIMEOUT_COUNTER  250
#else
#define  SCSI_REGISTER_TIMEOUT_COUNTER (((SCSI_UINT32)SCSI_REGISTER_TIMEOUT_DEFAULT*1000)/SCSI_MINIMUM_DELAY_COUNT)
#endif /* SCSI_SIMULATION_SUPPORT */

/****************************************************************************
* Definitions as generic SCSI HIM interface
****************************************************************************/
#if SCSI_RESOURCE_MANAGEMENT

#define  SCSI_GET_CONFIGURATION(hhcb)  SCSIRGetConfiguration((hhcb))
#define  SCSI_GET_MEMORY_TABLE(firmwareMode,numberScbs,numberNexusHandles,numberNodeHandles,memoryHandle) \
              SCSIRGetMemoryTable((firmwareMode),(numberScbs),(numberNexusHandles),(numberNodeHandles),(memoryHandle))
#define  SCSI_APPLY_MEMORY_TABLE(hhcb,memoryTable) SCSIRApplyMemoryTable((hhcb),(memoryTable))
#define  SCSI_INITIALIZE(hhcb)         SCSIRInitialize((hhcb))
#define  SCSI_SET_UNIT_HANDLE(hhcb,unitControl,scsiID,luNumber) \
              SCSIRSetUnitHandle((hhcb),(unitControl),(scsiID),(luNumber))
#define  SCSI_FREE_UNIT_HANDLE(unitControl) SCSIRFreeUnitHandle((unitControl))
#define  SCSI_QUEUE_HIOB(hiob)         SCSIRQueueHIOB((hiob))
#define  SCSI_QUEUE_SPECIAL_HIOB(hiob) SCSIRQueueSpecialHIOB((hiob))
#define  SCSI_BACK_END_ISR(hhcb)       SCSIRBackEndISR((hhcb))
#define  SCSI_COMPLETE_HIOB(hiob)      SCSIrCompleteHIOB((hiob))
#define  SCSI_COMPLETE_SPECIAL_HIOB(hiob)       SCSIrCompleteSpecialHIOB((hiob))
#define  SCSI_ASYNC_EVENT(hhcb,event)  SCSIrAsyncEvent((hhcb),(event))
#define  SCSI_POWER_MANAGEMENT(hhcb,powerMode)  SCSIRPowerManagement((hhcb),(powerMode))
#define  SCSI_CHECK_HOST_IDLE(hhcb)    SCSIRCheckHostIdle((hhcb))
#define  SCSI_CHECK_DEVICE_IDLE(hhcb,targetID)  SCSIRCheckDeviceIdle((hhcb),(targetID))
#define  SCSI_GET_HW_INFORMATION(hwInformation,hhcb) SCSIRGetHWInformation((hwInformation),(hhcb))

#if !SCSI_DISABLE_ADJUST_TARGET_PROFILE
#define  SCSI_PUT_HW_INFORMATION(hwInformation,hhcb) SCSIRPutHWInformation((hwInformation),(hhcb))
#else
#define  SCSI_PUT_HW_INFORMATION(hwInformation,hhcb) SCSIHPutHWInformation((hwInformation),(hhcb))
#endif /* !SCSI_DISABLE_ADJUST_TARGET_PROFILE */

#define  SCSI_ASYNC_EVENT_COMMAND(hhcb,hiob,event)  SCSIrAsyncEventCommand((hhcb),(hiob),(event))

#if SCSI_DOMAIN_VALIDATION
#define  SCSI_SEARCH_DONEQ_FOR_DV_REQUEST(hhcb)  SCSIRSearchDoneQForDVRequest((hhcb))
#endif /* SCSI_DOMAIN_VALIDATION */

#if SCSI_TARGET_OPERATION
#define  SCSI_CLEAR_NEXUS(nexus,hhcb)  SCSIrClearNexus((nexus),(hhcb))
#define  SCSI_ALLOCATE_NODE(hhcb)      SCSIrAllocateNode((hhcb))
#define  SCSI_TARGET_GETCONFIG(hhcb)   SCSI_rTARGETGETCONFIG((hhcb))
#else
#define  SCSI_TARGET_GETCONFIG(hhcb)
#endif /* SCSI_TARGET_OPERATION */     

#else /* !SCSI_RESOURCE_MANAGEMENT */

#if !SCSI_BIOS_SUPPORT
#define  SCSI_GET_MEMORY_TABLE(firmwareMode,numberScbs,memoryTable)   SCSIHGetMemoryTable((firmwareMode),(numberScbs),(memoryTable))
#define  SCSI_APPLY_MEMORY_TABLE(hhcb,memoryTable) SCSIHApplyMemoryTable((hhcb),(memoryTable))
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
#define  SCSI_POWER_MANAGEMENT(hhcb,powerMode)  SCSIHPowerManagement((hhcb),(powerMode))
#define  SCSI_CHECK_HOST_IDLE(hhcb)    SCSIHCheckHostIdle((hhcb))
#define  SCSI_CHECK_DEVICE_IDLE(hhcb,targetID)  SCSIHCheckDeviceIdle((hhcb),(targetID))
#else
#define  SCSI_CHECK_DEVICE_IDLE(hhcb,targetID)  
#define  SCSI_POWER_MANAGEMENT(hhcb,powerMode)
#define  SCSI_CHECK_HOST_IDLE(hhcb)
#endif  /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */
#else
#define  SCSI_POWER_MANAGEMENT(hhcb,powerMode)
#define  SCSI_CHECK_HOST_IDLE(hhcb)    
#define  SCSI_CHECK_DEVICE_IDLE(hhcb,targetID)  
#define  SCSI_GET_MEMORY_TABLE(firmwareMode,numberScbs,memoryTable)   
#define  SCSI_APPLY_MEMORY_TABLE(hhcb,memoryTable) 
#endif /* !SCSI_BIOS_SUPPORT */

#define  SCSI_GET_CONFIGURATION(hhcb)  SCSIHGetConfiguration((hhcb))
#define  SCSI_INITIALIZE(hhcb)         SCSIHInitialize((hhcb))
#define  SCSI_SET_UNIT_HANDLE(hhcb,unitControl,scsiID,luNumber) SCSIHSetUnitHandle((hhcb),(unitControl),(scsiID),(luNumber))
#if !SCSI_EFI_BIOS_SUPPORT
#define  SCSI_FREE_UNIT_HANDLE(unitControl)     SCSIHFreeUnitHandle((unitControl))
#else
#define  SCSI_FREE_UNIT_HANDLE(unitControl) 
#endif /* !SCSI_EFI_BIOS_SUPPORT */
#define  SCSI_QUEUE_HIOB(hiob)         SCSIHQueueHIOB((hiob))
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
#define  SCSI_QUEUE_SPECIAL_HIOB(hiob) SCSIHQueueSpecialHIOB((hiob))
#else
#define  SCSI_QUEUE_SPECIAL_HIOB(hiob) 
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */
#define  SCSI_BACK_END_ISR(hhcb)       SCSIHBackEndISR((hhcb))
#define  SCSI_COMPLETE_HIOB(hiob)      OSD_COMPLETE_HIOB((hiob))
#define  SCSI_COMPLETE_SPECIAL_HIOB(hiob)       OSD_COMPLETE_SPECIAL_HIOB((hiob))
#define  SCSI_ASYNC_EVENT(hhcb,event)  OSD_ASYNC_EVENT((hhcb),(event))

#define  SCSI_GET_HW_INFORMATION(hwInformation,hhcb) SCSIHGetHWInformation((hwInformation),(hhcb))
#define  SCSI_PUT_HW_INFORMATION(hwInformation,hhcb) SCSIHPutHWInformation((hwInformation),(hhcb))
#if SCSI_SCBBFR_BUILTIN
#define  SCSI_GET_FREE_HEAD(hhcb)
#else
#define  SCSI_GET_FREE_HEAD(hhcb)      OSD_GET_FREE_HEAD((hhcb))
#endif /* SCSI_SCBBFR_BUILTIN */
#define  SCSI_ASYNC_EVENT_COMMAND(hhcb,hiob,event)  OSD_ASYNC_EVENT_COMMAND((hhcb),(hiob),(event))

#if SCSI_DOMAIN_VALIDATION
#define  SCSI_SEARCH_DONEQ_FOR_DV_REQUEST(hhcb)
#endif /* SCSI_DOMAIN_VALIDATION */

#if SCSI_TARGET_OPERATION
#define  SCSI_CLEAR_NEXUS(nexus,hhcb)  SCSIHTargetClearNexus((nexus),(hhcb))
#define  SCSI_ALLOCATE_NODE(hhcb)      OSD_ALLOCATE_NODE((hhcb))
#endif /* SCSI_TARGET_OPERATION */  
#define  SCSI_TARGET_GETCONFIG(hhcb)

#endif /* SCSI_RESOURCE_MANAGEMENT */

#if !SCSI_BIOS_SUPPORT
#if !SCSI_BEF_SUPPORT
#define  SCSI_SIZE_SEEPROM(hhcb)       SCSIHSizeSEEPROM((hhcb))
#define  SCSI_SUPPRESS_NEGOTIATION(unitHandle)  SCSIHSuppressNegotiation((unitHandle))
#define  SCSI_FORCE_NEGOTIATION(unitHandle)     SCSIHForceNegotiation((unitHandle))
#else
#define  SCSI_SIZE_SEEPROM(hhcb)
#define  SCSI_FORCE_NEGOTIATION(unitHandle)  
#define  SCSI_SUPPRESS_NEGOTIATION(unitHandle)  
#endif /* !SCSI_BEF_SUPPORT */
#define  SCSI_APPLY_NVDATA(hhcb)       SCSIHApplyNVData((hhcb))
#else
#define  SCSI_FORCE_NEGOTIATION(unitHandle)  
#define  SCSI_SUPPRESS_NEGOTIATION(unitHandle)  
#define  SCSI_APPLY_NVDATA(hhcb)       
#define  SCSI_SIZE_SEEPROM(hhcb)       
#endif /* !SCSI_BIOS_SUPPORT */
                                       
#define  SCSI_ENABLE_IRQ(hhcb)         SCSIHEnableIRQ((hhcb))
#define  SCSI_DISABLE_IRQ(hhcb)        SCSIHDisableIRQ((hhcb))
#define  SCSI_POLL_IRQ(hhcb)           SCSIHPollIRQ((hhcb))
#define  SCSI_FRONT_END_ISR(hhcb)      SCSIHFrontEndISR((hhcb))
#define  SCSI_SAVE_STATE(hhcb,state)      SCSIHSaveState((hhcb),(state))
#define  SCSI_RESTORE_STATE(hhcb,state)   SCSIHRestoreState((hhcb),(state))

#define  SCSI_READ_SCB_RAM(hhcb,dataBuffer,scbNumber,scbOffset,byteLength) \
              SCSIHReadScbRAM ((hhcb),(dataBuffer),(scbNumber),(scbOffset),(byteLength))
#define  SCSI_WRITE_SCB_RAM(hhcb,scbNumber,scbOffset,byteLength,dataBuffer) \
              SCSIHWriteScbRAM ((hhcb),(scbNumber),(scbOffset),(byteLength),(dataBuffer))

#define  SCSI_UPDATE_DEVICE_TABLE(hhcb)   SCSIHUpdateDeviceTable((hhcb))
#define  SCSI_BIOS_STATE(hhcb)         SCSIHBiosState((hhcb))
#define  SCSI_WRITE_SEEPROM(hhcb,wordAddress,wordLength,dataBuffer) \
              SCSIHWriteSEEPROM((hhcb),(wordAddress),(wordLength),(dataBuffer))


#if (!SCSI_ASPI_SUPPORT && !SCSI_BEF_SUPPORT)
#define  SCSI_REALMODE_EXIST(hhcb)     SCSIHRealModeExist((hhcb))
#else
#define  SCSI_REALMODE_EXIST(hhcb)     
#endif /* (!SCSI_ASPI_SUPPORT && !SCSI_BEF_SUPPORT) */

#if !SCSI_ASPI_REVA_SUPPORT
#define  SCSI_READ_SCRATCH_RAM(hhcb,dataBuffer,byteOffset,byteLength) \
              SCSIHReadScratchRam((hhcb),(dataBuffer),(byteOffset),(byteLength))
#define  SCSI_WRITE_SCRATCH_RAM(hhcb,byteOffset,byteLength,dataBuffer) \
              SCSIHWriteScratchRam((hhcb),(byteOffset),(byteLength),(dataBuffer))
#else
#define  SCSI_READ_SCRATCH_RAM(hhcb,dataBuffer,byteOffset,byteLength) 
#define  SCSI_WRITE_SCRATCH_RAM(hhcb,byteOffset,byteLength,dataBuffer) 
#endif /* !SCSI_ASPI_REVA_SUPPORT */

#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
#define  SCSI_READ_SEEPROM(hhcb,dataBuffer,wordAddress,wordLength) \
            SCSIHReadSEEPROM((hhcb),(dataBuffer),(wordAddress),(wordLength))
#define  SCSI_ENABLE_EXP_STATUS(hhcb)  SCSIHEnableExpStatus((hhcb))
#define  SCSI_DISABLE_EXP_STATUS(hhcb) SCSIHDisableExpStatus((hhcb))
#else
#define  SCSI_READ_SEEPROM(hhcb,dataBuffer,wordAddress,wordLength) 
#define  SCSI_ENABLE_EXP_STATUS(hhcb)  
#define  SCSI_DISABLE_EXP_STATUS(hhcb) 
#endif  /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */

#define  SCSI_SETUP_HARDWARE(hhcb)     SCSIHSetupHardware((hhcb))
#define  SCSI_DATA_BUS_HANG(hhcb)      SCSIHDataBusHang((hhcb))

#if SCSI_TARGET_OPERATION
#define SCSI_DISABLE_SELECTION_IN(hhcb)                  SCSIHTargetModeDisable((hhcb))
#define SCSI_CHECK_SELECTION_IN_INTERRUPT_PENDING(hhcb)  SCSIHTargetSelInPending((hhcb))
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_CRC_NOTIFICATION
#define SCSI_PARITY_CDB_ERR          0xFFFFFF01
#define SCSI_PARITY_DATA_PARITY_ERR  0xFFFFFF02
#define SCSI_PARITY_CRC_ERR          0xFFFFFF03
#endif /* SCSI_CRC_NOTIFICATION */






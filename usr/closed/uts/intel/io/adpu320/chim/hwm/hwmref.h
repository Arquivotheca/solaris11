 /*$Header: /vobs/u320chim/src/chim/hwm/hwmref.h   /main/189   Mon Jun 16 19:04:13 2003   quan $*/

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
*  Module Name:   HWMREF.H
*
*  Description:   Definitions internal to Hardware management layer
*
*  Owners:        ECX IC Firmware Team
*
*  Notes:         1. This file should only be referenced by hardware
*                    management layer.
*
***************************************************************************/


/***************************************************************************
*  SCSI_NVM_LAYOUT definitions
*     This structures must be 64 bytes in length exactly for backward
*     compatible reason. 
***************************************************************************/
typedef struct SCSI_NVM_LAYOUT_
{
   union
   {
      SCSI_UEXACT16 u320TC[SCSI_MAXDEV];
   } targetControl;
   SCSI_UEXACT16 biosControl;
   SCSI_UEXACT16 generalControl;
   SCSI_UEXACT16 word19;                     /* id information             */
   SCSI_UEXACT16 word20;               
   SCSI_UEXACT16 reserved[10];               /* reserved and padding       */
   SCSI_UEXACT16 signature;                  /* signature = 0250           */
   SCSI_UEXACT16 checkSum;                   /* check sum value            */
} SCSI_NVM_LAYOUT;

/* Bitmap for U320TC[] */
#define SCSI_U320TC_ASYNCRATEBITS    (SCSI_UEXACT16)0x003F /* Async mode if == 3F       */

    /* *************************  Sync Rate Table  *********************************** */
    /*                                                                                 */
    /*  SCSI_U320TC_INITIATEWIDE == 0 :                                                */
    /*                                  All rates are divided by 2 (Narrow mode) with  */
    /*                                  160MBRATE and 320MBRATE meaningless.           */
    /*                                                                                 */
    /* ******************************************************************************* */

#define SCSI_U320TC_10MBRATE         (SCSI_UEXACT16)0x0032 /* 10 mbyte Sync mode        */
#define SCSI_U320TC_20MBRATE         (SCSI_UEXACT16)0x0019 /* 20 mbyte Sync mode        */
#define SCSI_U320TC_33MBRATE         (SCSI_UEXACT16)0x000D /* 33.3 Mbyte Sync mode      */
#define SCSI_U320TC_40MBRATE         (SCSI_UEXACT16)0x000C /* 40 Mbyte Sync mode        */
#define SCSI_U320TC_66MBRATE         (SCSI_UEXACT16)0x000B /* 66.6 Mbyte Sync mode      */
#define SCSI_U320TC_80MBRATE         (SCSI_UEXACT16)0x000A /* 80 Mbyte Sync mode        */
#define SCSI_U320TC_160MBRATE        (SCSI_UEXACT16)0x0009 /* 160 Mbyte Sync mode       */
#define SCSI_U320TC_320MBRATE        (SCSI_UEXACT16)0x0008 /* 320 Mbyte Sync mode       */

#define SCSI_U320TC_QASSUPPORT       (SCSI_UEXACT16)0x0040 /* QAS bit support           */
#define SCSI_U320TC_PACKETIZED       (SCSI_UEXACT16)0x0080 /* Packetized support        */
#define SCSI_U320TC_STARTUNITCOMMAND (SCSI_UEXACT16)0x0100 /* start unit command        */
#define SCSI_U320TC_BIOSSCAN         (SCSI_UEXACT16)0x0200 /* include BIOS scan         */
#define SCSI_U320TC_DISCONNECTENABLE (SCSI_UEXACT16)0x0400 /* disconnect enable         */
#define SCSI_U320TC_MULTIPLELUN      (SCSI_UEXACT16)0x0800 /* multiple lun enable       */
#define SCSI_U320TC_INITIATEWIDE     (SCSI_UEXACT16)0x1000 /* initiate wide nego        */
#define SCSI_U320TC_DONOTCARE13_14   (SCSI_UEXACT16)0x6000 /* reserved                  */
#define SCSI_U320TC_HOSTMANAGED      (SCSI_UEXACT16)0x8000 /* NTC Host managed          */
/* Bitmap for biosControl (new for U320) */

#define SCSI_BIOSCONTROL_DONOTCARE0_3     (SCSI_UEXACT16)0x000F  /* don't care          */
#define SCSI_BIOSCONTROL_DOMAINVALIDATION (SCSI_UEXACT16)0x0010  /* domain validation   */
#define SCSI_BIOSCONTROL_DONOTCARE5       (SCSI_UEXACT16)0x0020  /* don't care          */
#define SCSI_BIOSCONTROL_PARITYENABLE     (SCSI_UEXACT16)0x0040  /* parity enabled      */
#define SCSI_BIOSCONTROL_DONOTCARE7_10    (SCSI_UEXACT16)0x0780  /* don't care          */
#define SCSI_BIOSCONTROL_RESETSCSI        (SCSI_UEXACT16)0x0800  /* reset SCSI bus      */
#define SCSI_BIOSCONTROL_DONOTCARE12_15   (SCSI_UEXACT16)0xF000  /* don't care          */

/* Bitmap for generalControl (new for U320) */

#define SCSI_GENERALCONTROL_CABLESENSINGPRIMARY (SCSI_UEXACT16)0x0001  /* cable sensing prime */
#define SCSI_GENERALCONTROL_TERMINATIONLOW      (SCSI_UEXACT16)0x0002  /* primary term low    */
#define SCSI_GENERALCONTROL_TERMINATIONHIGH     (SCSI_UEXACT16)0x0004  /* primary term wide   */
#define SCSI_GENERALCONTROL_CABLESENSINGSECOND  (SCSI_UEXACT16)0x0008  /* cable sensing 2nd   */
#define SCSI_GENERALCONTROL_SECONDARYTERMLOW    (SCSI_UEXACT16)0x0010  /* secondary term NAR  */
#define SCSI_GENERALCONTROL_SECONDARYTERMHIGH   (SCSI_UEXACT16)0x0020  /* secondary term wide */
#define SCSI_GENERALCONTROL_STPWLEVEL           (SCSI_UEXACT16)0x0040  /* term power level    */
#define SCSI_GENERALCONTROL_AUTOTERMDETECTED    (SCSI_UEXACT16)0x0080  /* autoterminatin detection */
#define SCSI_GENERALCONTROL_LEGACYCONNECTOR     (SCSI_UEXACT16)0x0200  /* legacy connector */
#define SCSI_GENERALCONTROL_DONOTCARE7_14       (SCSI_UEXACT16)0x7c00  /* don't care          */
#define SCSI_GENERALCONTROL_CLUSTERENABLE       (SCSI_UEXACT16)0x8000  /* NTC Cluster Enabled */

/* Bitmap for word19 (new for U320) */

#define SCSI_WORD19_SCSIID        (SCSI_UEXACT16)0x000F        /* host scsi id  */
#define SCSI_WORD19_DONOTCARE4_15 (SCSI_UEXACT16)0xFFF0        /* don't care    */

/* Bitmap for word20 (new for U320) */

#define SCSI_WORD20_MAXTARGETS   (SCSI_UEXACT16)0x00FF /* maximum targets     */
#define SCSI_WORD20_BOOTLUN      (SCSI_UEXACT16)0x0F00 /* boot lun id         */
#define SCSI_WORD20_BOOTID       (SCSI_UEXACT16)0xF000 /* boot device scsi id */

/* Bitmap for word31 - (new for U320) */

#define SCSI_NVM_SIGNATURE       (SCSI_UEXACT16)0x0190         /* signature     */


/***************************************************************************
* Macros for general purposes
***************************************************************************/

/* SCSI_MODE_PTR register manipulation macros */
/* These macros must be used for all SCSI_MODE_PTR register manipulation.
 * There are separate macros for reading and setting the SCSI_MODE_PTR register
 * The aim being to prevent unnecessary PIOs if the register is already in
 * the required mode. 
 * Two hhcb variables, SCSI_HP_originalMode and SCSI_HP_currentMode, 
 * provide MODE_PTR state information.
 * Note the HWM BIOS interface requires scratch/scb memory to be used
 * for writable hhcb fields. 
 */
#if !SCSI_BIOS_SUPPORT
#define  SCSI_hGETMODEPTR(hhcb,scsiRegister) \
            SCSIhGetModeRegister((hhcb))            
#define  SCSI_hSETMODEPTR(hhcb,scsiRegister,mode) \
            ((hhcb)->SCSI_HP_currentMode == (SCSI_UEXACT8)(mode)) ? 0 :\
                         SCSIhSetModeRegister((hhcb),(SCSI_UEXACT8)(mode))
#define  SCSI_hLASTNEGOENTRYASSIGN(hhcb,deviceTable) \
                SCSIhLastNegoEntryAssign((deviceTable))
#else
/* Single tasking BIOS simply reads the MODE_PTR register */ 
#define  SCSI_hGETMODEPTR(hhcb,scsiRegister) \
            OSD_INEXACT8(SCSI_AICREG(SCSI_MODE_PTR))
/* For HWM BIOS Interface just reads and writes the register */
#define  SCSI_hSETMODEPTR(hhcb,scsiRegister,mode) \
            SCSIhSetModeRegister((hhcb),(SCSI_UEXACT8)(mode))
/*Cannot update device table entries in BIOS runtime op*/ 
#define  SCSI_hLASTNEGOENTRYASSIGN(hhcb,deviceTable) \
            if (!(hhcb)->SCSI_HF_biosRunTimeEnabled) \
            { \
                SCSIhLastNegoEntryAssign((deviceTable));\
            }

#endif /* !SCSI_BIOS_SUPPORT */

/***************************************************************************
* Macros to select HHCB fields that are written at run-time
* Required to handle HWM compiles with SCSI_BIOS_SUPPORT
***************************************************************************/
#if SCSI_BIOS_SUPPORT
#define  SCSI_hGETORIGINALMODE(hhcb) \
            OSD_INEXACT8(SCSI_AICREG(SCSI_hBIOS_ORIGINAL_MODE((hhcb))))
#define  SCSI_hSETORIGINALMODE(hhcb,value) \
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_hBIOS_ORIGINAL_MODE((hhcb))), (value));
#else
#define  SCSI_hGETORIGINALMODE(hhcb)     (hhcb)->SCSI_HP_originalMode
#define  SCSI_hSETORIGINALMODE(hhcb,value)  \
            (hhcb)->SCSI_HP_originalMode = (SCSI_UEXACT8)(value)
#endif /* SCSI_BIOS_SUPPORT */

/***************************************************************************
* Macros for Negotiation Needed Check
***************************************************************************/

/* Check wide from the indexing xferRate entry and the current xferRate entry */
#define  SCSI_hWIDENEGONEEDED(deviceTable) \
            ((((deviceTable)->xferRate[(deviceTable)->negoXferIndex][SCSI_XFER_MISC_OPT] & SCSI_WIDE) && \
              ((deviceTable)->xferRate[(deviceTable)->defaultXferIndex][SCSI_XFER_MISC_OPT] & SCSI_WIDE)) || \
             ((deviceTable)->xferRate[SCSI_LAST_NEGO_ENTRY][SCSI_XFER_MISC_OPT] & SCSI_WIDE))

/* Check sync (non-zero offset) from the indexing xferRate entry */
/* and the current xferRate entry */
#define  SCSI_hSYNCNEGONEEDED(deviceTable) \
            (((deviceTable)->xferRate[(deviceTable)->negoXferIndex][SCSI_XFER_OFFSET] && \
              (deviceTable)->xferRate[(deviceTable)->defaultXferIndex][SCSI_XFER_OFFSET]) || \
             (deviceTable)->xferRate[SCSI_LAST_NEGO_ENTRY][SCSI_XFER_OFFSET])

/* Check wide or sync (non-zero offset) from the indexing xferRate entry */
/* and the current xferRate entry */
#define  SCSI_hWIDEORSYNCNEGONEEDED(deviceTable) \
            ((((deviceTable)->xferRate[(deviceTable)->negoXferIndex][SCSI_XFER_MISC_OPT] & SCSI_WIDE) && \
              ((deviceTable)->xferRate[(deviceTable)->defaultXferIndex][SCSI_XFER_MISC_OPT] & SCSI_WIDE)) || \
             ((deviceTable)->xferRate[(deviceTable)->negoXferIndex][SCSI_XFER_OFFSET] && \
              (deviceTable)->xferRate[(deviceTable)->defaultXferIndex][SCSI_XFER_OFFSET]) || \
             ((deviceTable)->xferRate[SCSI_LAST_NEGO_ENTRY][SCSI_XFER_MISC_OPT] & SCSI_WIDE) || \
             (deviceTable)->xferRate[SCSI_LAST_NEGO_ENTRY][SCSI_XFER_OFFSET])


/* Use Parallel Protocol Request message for negotiation only if     */
/* - the indexing xferRate entry or current xferRate entry was set   */
/*   to do DT clocking AND                                           */
/* - the bus is LVD bus: ENAB20 is not set AND EXP_ACTIVE is not set */
#define  SCSI_hPPRNEGONEEDED(deviceTable,scsiRegister) \
            (((((deviceTable)->xferRate[(deviceTable)->negoXferIndex][SCSI_XFER_PTCL_OPT] & SCSI_DUALEDGE) && \
               ((deviceTable)->xferRate[(deviceTable)->defaultXferIndex][SCSI_XFER_PTCL_OPT] & SCSI_DUALEDGE)) || \
              ((deviceTable)->xferRate[SCSI_LAST_NEGO_ENTRY][SCSI_XFER_PTCL_OPT] & SCSI_DUALEDGE)) && \
             (!((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT2)) & SCSI_EXP_ACTIVE) || \
                (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) & SCSI_ENAB20))))



/***************************************************************************
* Macros for Packetized I/O support
***************************************************************************/
#if SCSI_PACKETIZED_IO_SUPPORT
#define  SCSI_hISPACKETIZED(deviceTable) \
            ((deviceTable)->xferRate[SCSI_CURRENT_ENTRY][SCSI_XFER_PTCL_OPT] & SCSI_PACKETIZED)
#if SCSI_INITIATOR_OPERATION
#define  SCSI_hCHECKCONDITION(hhcb,hiob) \
            if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))\
            {\
               SCSIhPacketizedCheckCondition((hhcb),(hiob));\
            }\
            else\
            {\
               SCSIhCheckCondition((hhcb),(hiob));\
            }
#else
#define   SCSI_hCHECKCONDITION(hhcb,hiob)
#endif /* SCSI_INITIATOR_OPERATION */
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_INITIATOR_OPERATION
#define  SCSI_hCHECKCONDITION(hhcb,hiob) SCSIhCheckCondition((hhcb),(hiob))
#else
#define   SCSI_hCHECKCONDITION(hhcb,hiob)
#endif /* SCSI_INITIATOR_OPERATION */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

/***************************************************************************
* Macro for Harpoon Rev A and/or Rev B checking for the workaround support
***************************************************************************/
#if SCSI_AICU320
#define SCSI_hHARPOON_REV_A_AND_B(hhcb) \
           (((((hhcb)->deviceID & SCSI_ID_MASK) == SCSI_HARPOON2_BASED_ID) && \
             ((hhcb)->hardwareRevision >= SCSI_HARPOON2_REV_2_CHIP)) || \
            ((((hhcb)->deviceID & SCSI_ID_MASK) == SCSI_HARPOON1_BASED_ID) && \
             ((hhcb)->hardwareRevision >= SCSI_HARPOON1_REV_3_CHIP)))

#define SCSI_hHARPOON_REV_A(hhcb) \
           (((((hhcb)->deviceID & SCSI_ID_MASK) == SCSI_HARPOON2_BASED_ID) && \
             ((hhcb)->hardwareRevision >= SCSI_HARPOON2_REV_2_CHIP) && \
             ((hhcb)->hardwareRevision < SCSI_HARPOON2_REV_10_CHIP)) || \
            ((((hhcb)->deviceID & SCSI_ID_MASK) == SCSI_HARPOON1_BASED_ID) && \
             ((hhcb)->hardwareRevision >= SCSI_HARPOON1_REV_3_CHIP) && \
             ((hhcb)->hardwareRevision < SCSI_HARPOON1_REV_10_CHIP)))

#define SCSI_hHARPOON_REV_B(hhcb) \
           (((((hhcb)->deviceID & SCSI_ID_MASK) == SCSI_HARPOON2_BASED_ID) && \
             ((hhcb)->hardwareRevision >= SCSI_HARPOON2_REV_10_CHIP)) || \
            ((((hhcb)->deviceID & SCSI_ID_MASK) == SCSI_HARPOON1_BASED_ID) && \
             ((hhcb)->hardwareRevision >= SCSI_HARPOON1_REV_10_CHIP)))

#define SCSI_hHARPOON_REV_1B(hhcb) \
           ((((hhcb)->deviceID & SCSI_ID_MASK) == SCSI_HARPOON1_BASED_ID) && \
            ((hhcb)->hardwareRevision >= SCSI_HARPOON1_REV_10_CHIP))

#if SCSI_DCH_U320_MODE
#define SCSI_hDCH_SCSI_CORE(hhcb) (1)
#else
#define SCSI_hDCH_SCSI_CORE(hhcb) (0)
#endif /* SCSI_DCH_U320_MODE */

#endif /* SCSI_AICU320 */


/***************************************************************************
* Macros for Target Mode Multiple ID support
***************************************************************************/
#if SCSI_MULTIPLEID_SUPPORT
/* To be added. */
#endif /* SCSI_MULTIPLEID_SUPPORT */             

#if SCSI_MULTI_MODE
/***************************************************************************
* Macros for all modes
***************************************************************************/
#define  SCSI_hSIZE_OF_SCB_BUFFER(mode) SCSIFirmware[(mode)]->ScbSize
#define  SCSI_hALIGNMENTSCBBUFFER(mode) SCSI_hSIZE_OF_SCB_BUFFER((mode))-1
#define  SCSI_hSETUPFIRMWAREDESC(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor = \
                                    SCSIFirmware[(hhcb)->firmwareMode]

#define  SCSI_hFIRMWARE_VERSION(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->firmwareVersion
#define  SCSI_hSIOSTR3_ENTRY(hhcb)   (hhcb)->SCSI_HP_firmwareDescriptor->Siostr3Entry      
#define  SCSI_hRETRY_IDENTIFY_ENTRY(hhcb)   (hhcb)->SCSI_HP_firmwareDescriptor->RetryIdentifyEntry      
#define  SCSI_hSTART_LINK_CMD_ENTRY(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->StartLinkCmdEntry
#define  SCSI_hIDLE_LOOP_ENTRY(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->IdleLoopEntry
#define  SCSI_hIDLE_LOOP_TOP(hhcb)   (hhcb)->SCSI_HP_firmwareDescriptor->IdleLoopTop
#define  SCSI_hIDLE_LOOP0(hhcb)      (hhcb)->SCSI_HP_firmwareDescriptor->IdleLoop0
#define  SCSI_hSIO204_ENTRY(hhcb)    (hhcb)->SCSI_HP_firmwareDescriptor->Sio204Entry
#define  SCSI_hEXPANDER_BREAK(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->ExpanderBreak
#define  SCSI_hISR(hhcb)             (hhcb)->SCSI_HP_firmwareDescriptor->Isr
#if SCSI_TARGET_OPERATION
#define SCSI_hTARGET_DATA_ENTRY(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->TargetDataEntry
#define SCSI_hTARGET_HELD_BUS_ENTRY(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->TargetHeldBusEntry
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_hBTATABLE(hhcb)        (hhcb)->SCSI_HP_firmwareDescriptor->BtaTable
#define  SCSI_hBTASIZE(hhcb)         (hhcb)->SCSI_HP_firmwareDescriptor->BtaSize 
#if !SCSI_SEQ_LOAD_USING_PIO
#define  SCSI_hLOAD_SIZE(hhcb)       (hhcb)->SCSI_HP_firmwareDescriptor->LoadSize
#define  SCSI_hLOAD_CODE(hhcb)       (hhcb)->SCSI_HP_firmwareDescriptor->LoadCode
#define  SCSI_hLOAD_ADDR(hhcb)       (hhcb)->SCSI_HP_firmwareDescriptor->LoadAddr
#define  SCSI_hLOAD_COUNT(hhcb)      (hhcb)->SCSI_HP_firmwareDescriptor->LoadCount
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
#define  SCSI_hPASS_TO_DRIVER(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->PassToDriver
#define  SCSI_hACTIVE_SCB(hhcb)      (hhcb)->SCSI_HP_firmwareDescriptor->ActiveScb
#define  SCSI_hRET_ADDR(hhcb)        (hhcb)->SCSI_HP_firmwareDescriptor->RetAddr
#define  SCSI_hQ_NEW_POINTER(hhcb)   (hhcb)->SCSI_HP_firmwareDescriptor->QNewPointer
#define  SCSI_hENT_PT_BITMAP(hhcb)   (hhcb)->SCSI_HP_firmwareDescriptor->EntPtBitmap
#define  SCSI_hSG_STATUS(hhcb)       (hhcb)->SCSI_HP_firmwareDescriptor->SgStatus
#define  SCSI_hFIRST_SCB(hhcb)       (hhcb)->SCSI_HP_firmwareDescriptor->FirstScb
#define  SCSI_hDVTIMER_LO(hhcb)      (hhcb)->SCSI_HP_firmwareDescriptor->DvTimerLo
#define  SCSI_hDVTIMER_HI(hhcb)      (hhcb)->SCSI_HP_firmwareDescriptor->DvTimerHi
#if SCSI_PACKETIZED_IO_SUPPORT
#define  SCSI_hSLUN_LENGTH(hhcb)     (hhcb)->SCSI_HP_firmwareDescriptor->SlunLength
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

#if SCSI_TARGET_OPERATION
#define  SCSI_hREG0(hhcb)            (hhcb)->SCSI_HP_firmwareDescriptor->Reg0    
#define  SCSI_hQ_EST_HEAD(hhcb)      (hhcb)->SCSI_HP_firmwareDescriptor->QEstHead
#define  SCSI_hQ_EST_TAIL(hhcb)      (hhcb)->SCSI_HP_firmwareDescriptor->QEstTail
#define  SCSI_hQ_EXE_TARG_HEAD(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->QExeTargHead
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_hARPINTVALID_REG(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->ArpintValidReg
#define  SCSI_hARPINTVALID_BIT(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->ArpintValidBit
#define  SCSI_hSETUPSEQUENCER(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetupSequencer((hhcb))
#define  SCSI_hRESETSOFTWARE(hhcb)   (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhResetSoftware((hhcb))
#define  SCSI_hDELIVERSCB(hhcb,hiob) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhDeliverScb((hhcb),(hiob))
#define  SCSI_hSETUPATNLENGTH(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetupAtnLength((hhcb),(hiob));

#define  SCSI_hTARGETCLEARBUSY(hhcb,hiob) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetClearBusy((hhcb),(hiob))
#define  SCSI_hREQUESTSENSE(hhcb,hiob) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhRequestSense((hhcb),(hiob))
#define  SCSI_hRESETBTA(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhResetBTA((hhcb))
#define  SCSI_hGETCONFIGURATION(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhGetConfig((hhcb))
#if SCSI_SCBBFR_BUILTIN
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetupAssignScbBuffer)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetupAssignScbBuffer((hhcb));\
            }
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhAssignScbDescriptor)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhAssignScbDescriptor((hhcb),(hiob));\
            }
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhFreeScbDescriptor)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhFreeScbDescriptor((hhcb),(hiob));\
            }
#endif /* SCSI_SCBBFR_BUILTIN */

#define  SCSI_hXFERRATEASSIGN(hhcb,i,xferRate) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhXferRateAssign((hhcb),(i),(xferRate))

#define  SCSI_hGETNEGOXFERRATE(hhcb,i,xferRate) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhGetNegoXferRate((hhcb),(i),(xferRate))

#define  SCSI_hABORTCHANNEL(hhcb,haStatus) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhAbortChannel((hhcb),(haStatus))

#if (!SCSI_ASPI_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1)

#define  SCSI_hSEARCHEXEQ(hhcb,hiob,haStatus,postHiob) \
            SCSIhStandardSearchExeQ((hhcb),(hiob),(haStatus),(postHiob))

#define  SCSI_hSEARCHSEQDONEQ(hhcb,hiob) \
            SCSIhStandardSearchSeqDoneQ((hhcb),(hiob))

#define  SCSI_hABORTHIOB(hhcb,hiob,haStatus) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhAbortHIOB((hhcb),(hiob),(haStatus))
#define  SCSI_hENQUEHEADTR(hhcb,hiob)  SCSIhStandardQHeadTR((hhcb),(hiob))

#define  SCSI_hREMOVEACTIVEABORT(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhRemoveActiveAbort((hhcb),(hiob))

#define  SCSI_hACTIVEABORT(hhcb,hiob,haStatus) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhActiveAbort((hhcb),(hiob),(haStatus))
#define  SCSI_hPACKACTIVEABORT(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhPackActiveAbort((hhcb),(hiob))
#define  SCSI_hNONPACKACTIVEABORT(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhNonPackActiveAbort((hhcb),(hiob))

#define  SCSI_hUPDATEABORTBITHOSTMEM(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhUpdateAbortBitHostMem((hhcb),(hiob))

#else 
#define  SCSI_hUPDATEABORTBITHOSTMEM(hhcb,hiob)
#define  SCSI_hSEARCHSEQDONEQ(hhcb,hiob) 
#define  SCSI_hSEARCHEXEQ(hhcb,hiob,haStatus,postHiob)
#define  SCSI_hABORTHIOB(hhcb,hiob,haStatus) 
#define  SCSI_hENQUEHEADTR(hhcb,hiob)  
#define  SCSI_hREMOVEACTIVEABORT(hhcb,hiob) 
#define  SCSI_hACTIVEABORT(hhcb,hiob,haStatus) 
#define  SCSI_hPACKACTIVEABORT(hhcb,hiob)
#define  SCSI_hNONPACKACTIVEABORT(hhcb,hiob)
#endif /* (!SCSI_ASPI_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1) */
       
#define  SCSI_hSETELECINFORMATION(hhcb,targetID,xferRate) SCSIhStandardSetElecInformation((hhcb),(targetID),(xferRate))

#define  SCSI_hUPDATENEXTSCBADDRESS(hhcb,hiob,ScbBusAddress) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhUpdateNextScbAddress)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhUpdateNextScbAddress((hhcb),(hiob),(ScbBusAddress));\
            }
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
#define  SCSI_hSETUPTARGETRESET(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetupTR((hhcb),(hiob))

#define  SCSI_hSETBREAKPOINT(hhcb,entryBitMap) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetBreakPoint((hhcb),(entryBitMap))

#define  SCSI_hCLEARBREAKPOINT(hhcb,entryBitMap) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhClearBreakPoint((hhcb),(entryBitMap))

#else

#define  SCSI_hCLEARBREAKPOINT(hhcb,entryBitMap) 
#define  SCSI_hSETBREAKPOINT(hhcb,entryBitMap) 
#define  SCSI_hSETUPTARGETRESET(hhcb,hiob) 
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT */

#define  SCSI_hRESIDUECALC(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhResidueCalc((hhcb),(hiob))

#define  SCSI_hIGNOREWIDERESIDUECALC(hhcb,hiob,ignoreXfer) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhIgnoreWideResidueCalc((hhcb),(hiob),(ignoreXfer))

#define  SCSI_hEVENIOLENGTH(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhEvenIOLength((hhcb),(hiob))

#define  SCSI_hUNDERRUN(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhUnderrun((hhcb))

#define  SCSI_hFREEZEHWQUEUE(hhcb,hiob,event) \
            SCSIhStandardFreezeHWQueue((hhcb),(hiob),(event))

#define  SCSI_hPACKNONPACKQUEUEHIOB(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhPackNonPackQueueHIOB)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhPackNonPackQueueHIOB((hhcb),(hiob));\
            }

#define  SCSI_hSETINTRFACTORTHRESHOLD(hhcb,Factorthreshold) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetIntrFactorThreshold)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetIntrFactorThreshold((hhcb),(Factorthreshold));\
            }
            
#define  SCSI_hSETINTRTHRESHOLDCOUNT(hhcb,thresholdCount) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetIntrThresholdCount)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetIntrThresholdCount((hhcb),(thresholdCount));\
            }

#define  SCSI_hUPDATEEXEQATNLENGTH(hhcb,targetID) \
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhUpdateExeQAtnLength((hhcb),(targetID))

#define  SCSI_hUPDATENEWQATNLENGTH(hhcb,targetID) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhUpdateNewQAtnLength)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhUpdateNewQAtnLength((hhcb),(targetID));\
            }

#define  SCSI_hQHEADPNPSWITCHSCB(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhQHeadPNPSwitchSCB)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhQHeadPNPSwitchSCB((hhcb),(hiob));\
            }

#if (OSD_BUS_ADDRESS_SIZE == 64)
#define  SCSI_hCOMPAREBUSSGLISTADDRESS(hhcb,busAddress1,busAddress2) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhCompareBusSGListAddress((hhcb),(busAddress1),(busAddress2))

#define  SCSI_hPUTBUSADDRESSSG(hhcb,dmaStruct,offset,value) \
            if (((hhcb)->SCSI_HF_SgBusAddress32 == 1) && \
                (((hhcb)->firmwareMode == SCSI_FMODE_STANDARD_U320) || \
                 ((hhcb)->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)))\
            {\
               SCSI_PUT_LITTLE_ENDIAN64SG((hhcb),(dmaStruct),(offset),(value));\
            }\
            else\
            {\
               SCSI_PUT_LITTLE_ENDIAN64((hhcb),(dmaStruct),(offset),(value));\
            }

#define  SCSI_hGETBUSADDRESSSGPAD(hhcb,pValue,dmaStruct,offset) \
            if (((hhcb)->SCSI_HF_SgBusAddress32 == 1) && \
                (((hhcb)->firmwareMode == SCSI_FMODE_STANDARD_U320) || \
                 ((hhcb)->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)))\
            {\
               SCSI_GET_LITTLE_ENDIAN64SGPAD((hhcb),(pValue),(dmaStruct),(offset));\
            }\
            else\
            {\
               SCSI_GET_LITTLE_ENDIAN64((hhcb),(pValue),(dmaStruct),(offset));\
            }
#else
#define  SCSI_hCOMPAREBUSSGLISTADDRESS(hhcb,busAddress1,busAddress2) \
            SCSIhCompareBusAddress((busAddress1),(busAddress2))
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */

#define  SCSI_hSETDISCONNECTDELAY(hhcb) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetDisconnectDelay)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetDisconnectDelay((hhcb));\
            }

#define  SCSI_hUPDATEEXEQ(hhcb,hiob) \
               SCSIhStandardUpdateExeQ((hhcb),(hiob))

#define  SCSI_hQEXETARGNEXTOFFSET(hhcb) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhQExeTargNextOffset()

#define  SCSI_hQNEXTOFFSET(hhcb) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhQNextOffset()

#define  SCSI_hTARGETRESETSOFTWARE(hhcb) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetResetSoftware)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetResetSoftware((hhcb));\
            }

#if SCSI_TARGET_OPERATION 
#define  SCSI_hTARGETSETIGNOREWIDEMSG(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetSetIgnoreWideMsg)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetSetIgnoreWideMsg((hhcb),(hiob));\
            }

#define  SCSI_hTARGETSENDHIOBSPECIAL(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetSendHiobSpecial)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetSendHiobSpecial((hhcb),(hiob));\
            }

#define  SCSI_hTARGETGETESTSCBFIELDS(hhcb,hiob,flag) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetGetEstScbFields)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetGetEstScbFields((hhcb),(hiob),(flag));\
            }

#define  SCSI_hTARGETDELIVERESTSCB(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetDeliverEstScb)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetDeliverEstScb((hhcb),(hiob));\
            }

#define  SCSI_hTARGETSETFIRMWAREPROFILE(hhcb) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetSetFirmwareProfile)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetSetFirmwareProfile((hhcb));\
            }

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_hTARGETSELINTYPE(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetGetSelectionInType((hhcb),(hiob))

#define  SCSI_hTARGETIUSELIN(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetIUSelectionIn)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetIUSelectionIn((hhcb),(hiob));\
            }

#define  SCSI_hTARGETBUILDSTATUSIU(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetBuildStatusIU)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetBuildStatusIU((hhcb),(hiob));\
            }

#define  SCSI_hTARGETASSIGNERRORSCBDESCRIPTOR(hhcb) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetAssignErrorScbDescriptor((hhcb))

#define  SCSI_hTARGETINITERRORHIOB(hhcb) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetInitErrorHIOB)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetInitErrorHIOB((hhcb));\
            }

#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

#endif /* SCSI_TARGET_OPERATION */

#else /* !SCSI_MULTI_MODE */


#if SCSI_MULTI_DOWNSHIFT_MODE
/***************************************************************************
* Macros for all Downshift  modes
***************************************************************************/
#define  SCSI_hSIZE_OF_SCB_BUFFER(mode) SCSIFirmware[(mode)]->ScbSize
#define  SCSI_hALIGNMENTSCBBUFFER(mode) SCSI_hSIZE_OF_SCB_BUFFER((mode))-1
#define  SCSI_hSETUPFIRMWAREDESC(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor = \
                                    SCSIFirmware[(hhcb)->firmwareMode]

#define  SCSI_hFIRMWARE_VERSION(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->firmwareVersion
#define  SCSI_hSIOSTR3_ENTRY(hhcb)   (hhcb)->SCSI_HP_firmwareDescriptor->Siostr3Entry      
#define  SCSI_hRETRY_IDENTIFY_ENTRY(hhcb)   (hhcb)->SCSI_HP_firmwareDescriptor->RetryIdentifyEntry      
#define  SCSI_hSTART_LINK_CMD_ENTRY(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->StartLinkCmdEntry
#define  SCSI_hIDLE_LOOP_ENTRY(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->IdleLoopEntry
#define  SCSI_hIDLE_LOOP_TOP(hhcb)   (hhcb)->SCSI_HP_firmwareDescriptor->IdleLoopTop
#define  SCSI_hIDLE_LOOP0(hhcb)      (hhcb)->SCSI_HP_firmwareDescriptor->IdleLoop0
#define  SCSI_hSIO204_ENTRY(hhcb)    (hhcb)->SCSI_HP_firmwareDescriptor->Sio204Entry
#define  SCSI_hEXPANDER_BREAK(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->ExpanderBreak


#define  SCSI_hBTATABLE(hhcb)        (hhcb)->SCSI_HP_firmwareDescriptor->BtaTable
#define  SCSI_hBTASIZE(hhcb)         (hhcb)->SCSI_HP_firmwareDescriptor->BtaSize 
#if !SCSI_SEQ_LOAD_USING_PIO
#define  SCSI_hLOAD_SIZE(hhcb)       (hhcb)->SCSI_HP_firmwareDescriptor->LoadSize
#define  SCSI_hLOAD_CODE(hhcb)       (hhcb)->SCSI_HP_firmwareDescriptor->LoadCode
#define  SCSI_hLOAD_ADDR(hhcb)       (hhcb)->SCSI_HP_firmwareDescriptor->LoadAddr
#define  SCSI_hLOAD_COUNT(hhcb)      (hhcb)->SCSI_HP_firmwareDescriptor->LoadCount
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
#define  SCSI_hPASS_TO_DRIVER(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->PassToDriver
#define  SCSI_hACTIVE_SCB(hhcb)      (hhcb)->SCSI_HP_firmwareDescriptor->ActiveScb
#define  SCSI_hRET_ADDR(hhcb)        (hhcb)->SCSI_HP_firmwareDescriptor->RetAddr
#define  SCSI_hQ_NEW_POINTER(hhcb)   (hhcb)->SCSI_HP_firmwareDescriptor->QNewPointer
#define  SCSI_hSG_STATUS(hhcb)       (hhcb)->SCSI_HP_firmwareDescriptor->SgStatus

#define  SCSI_hARPINTVALID_REG(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->ArpintValidReg
#define  SCSI_hARPINTVALID_BIT(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->ArpintValidBit
#define  SCSI_hSETUPSEQUENCER(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetupSequencer((hhcb))
#define  SCSI_hRESETSOFTWARE(hhcb)   (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhResetSoftware((hhcb))
#define  SCSI_hDELIVERSCB(hhcb,hiob) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhDeliverScb((hhcb),(hiob))
#define  SCSI_hSETUPATNLENGTH(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetupAtnLength((hhcb),(hiob));

#define  SCSI_hTARGETCLEARBUSY(hhcb,hiob) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhTargetClearBusy((hhcb),(hiob))
#define  SCSI_hREQUESTSENSE(hhcb,hiob) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhRequestSense((hhcb),(hiob))
#define  SCSI_hRESETBTA(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhResetBTA((hhcb))
#define  SCSI_hGETCONFIGURATION(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhGetConfig((hhcb))
#if SCSI_SCBBFR_BUILTIN
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetupAssignScbBuffer)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhSetupAssignScbBuffer((hhcb));\
            }
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhAssignScbDescriptor)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhAssignScbDescriptor((hhcb),(hiob));\
            }
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhFreeScbDescriptor)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhFreeScbDescriptor((hhcb),(hiob));\
            }
#endif /* SCSI_SCBBFR_BUILTIN */

#define  SCSI_hXFERRATEASSIGN(hhcb,i,xferRate) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhXferRateAssign((hhcb),(i),(xferRate))

#define  SCSI_hGETNEGOXFERRATE(hhcb,i,xferRate) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhGetNegoXferRate((hhcb),(i),(xferRate))

#define  SCSI_hABORTCHANNEL(hhcb,haStatus) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhAbortChannel((hhcb),(haStatus))

#define  SCSI_hENQUEHEADTR(hhcb,hiob)  

#define  SCSI_hABORTHIOB(hhcb,hiob,haStatus)
#define  SCSI_hACTIVEABORT(hhcb,hiob,haStatus)
#define  SCSI_hREMOVEACTIVEABORT(hhcb,hiob)
#define  SCSI_hSEARCHSEQDONEQ(hhcb,hiob)
#define  SCSI_hSEARCHEXEQ(hhcb,hiob,haStatus,postHiob)
#define  SCSI_hUPDATEABORTBITHOSTMEM(hhcb,hiob)
#define  SCSI_hUPDATENEXTSCBADDRESS(hhcb,hiob,ScbBusAddress)
#define  SCSI_hSETUPTARGETRESET(hhcb,hiob)

#define  SCSI_hSETBREAKPOINT(hhcb,entryBitMap)
#define  SCSI_hCLEARBREAKPOINT(hhcb,entryBitMap)
#define  SCSI_hFREEZEHWQUEUE(hhcb,hiob,event) 
#define  SCSI_hPACKNONPACKQUEUEHIOB(hhcb,hiob) 
#define  SCSI_hSETINTRTHRESHOLDCOUNT(hhcb,threshold)
#define  SCSI_hSETINTRFACTORTHRESHOLD(hhcb,Factorthreshold) 


#define  SCSI_hRESIDUECALC(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhResidueCalc((hhcb),(hiob))

#define  SCSI_hIGNOREWIDERESIDUECALC(hhcb,hiob,ignoreXfer) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhIgnoreWideResidueCalc((hhcb),(hiob),(ignoreXfer))

#define  SCSI_hEVENIOLENGTH(hhcb,hiob) \
            (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhEvenIOLength((hhcb),(hiob))

#define  SCSI_hUNDERRUN(hhcb)  (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhUnderrun((hhcb))

#if SCSI_BIOS_SUPPORT
#define  SCSI_hUPDATEEXEQATNLENGTH(hhcb,targetID)
#define  SCSI_hUPDATENEWQATNLENGTH(hhcb,targetID)
#else /* !SCSI_BIOS_SUPPORT */
#define  SCSI_hUPDATEEXEQATNLENGTH(hhcb,targetID) \
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhUpdateExeQAtnLength((hhcb),(targetID))

#define  SCSI_hUPDATENEWQATNLENGTH(hhcb,targetID) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhUpdateNewQAtnLength)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhUpdateNewQAtnLength((hhcb),(targetID));\
            }
#endif /* SCSI_BIOS_SUPPORT */



#define  SCSI_hQHEADPNPSWITCHSCB(hhcb,hiob) \
            if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIhQHeadPNPSwitchSCB)\
            {\
               (hhcb)->SCSI_HP_firmwareDescriptor->SCSIhQHeadPNPSwitchSCB((hhcb),(hiob));\
            }

            
#define  SCSI_hSETDISCONNECTDELAY(hhcb)

#define  SCSI_hUPDATEEXEQ(hhcb,hiob) \
            SCSIhDownshiftU320UpdateExeQ((hhcb),(hiob))

#define  SCSI_hSETCOMMANDREQUESTOR(hhcb,hiob)\
            SCSIhDownShiftU320SetCommandRequestor((hhcb),(hiob))

#define  SCSI_hSETELECINFORMATION(hhcb,targetID,xferRate) SCSIhDownshiftSetElecInformation((hhcb),(targetID),(xferRate))

#else /* !SCSI_MULTI_DOWNSHIFT_MODE */

#if SCSI_STANDARD_U320_MODE
/***************************************************************************
* Macros for standard ultra 320 mode exclusively
***************************************************************************/
#define  SCSI_hSIZE_OF_SCB_BUFFER(mode) SCSI_SU320_SIZE_SCB
#define  SCSI_hALIGNMENTSCBBUFFER(mode) SCSI_hSIZE_OF_SCB_BUFFER((mode))-1
#define  SCSI_hSETUPFIRMWAREDESC(hhcb)

#define  SCSI_hFIRMWARE_VERSION(hhcb)        SCSI_SU320_FIRMWARE_VERSION
#define  SCSI_hSIOSTR3_ENTRY(hhcb)           SCSI_SU320_SIOSTR3_ENTRY
#define  SCSI_hRETRY_IDENTIFY_ENTRY(hhcb)    SCSI_SU320_RETRY_IDENTIFY_ENTRY
#define  SCSI_hSTART_LINK_CMD_ENTRY(hhcb)    SCSI_SU320_START_LINK_CMD_ENTRY
#define  SCSI_hIDLE_LOOP_ENTRY(hhcb)         SCSI_SU320_IDLE_LOOP_ENTRY
#define  SCSI_hIDLE_LOOP_TOP(hhcb)           SCSI_SU320_IDLE_LOOP_TOP
#define  SCSI_hIDLE_LOOP0(hhcb)              SCSI_SU320_IDLE_LOOP0
#define  SCSI_hSIO204_ENTRY(hhcb)            SCSI_SU320_SIO204_ENTRY
#define  SCSI_hEXPANDER_BREAK(hhcb)          SCSI_SU320_EXPANDER_BREAK
#define  SCSI_hISR(hhcb)                     SCSI_SU320_ISR
#if SCSI_TARGET_OPERATION 
#define  SCSI_hTARGET_DATA_ENTRY(hhcb)       SCSI_SU320_TARGET_DATA_ENTRY
#define  SCSI_hTARGET_HELD_BUS_ENTRY(hhcb)   SCSI_SU320_TARGET_HELD_BUS_ENTRY
#endif /* SCSI_TARGET_OPERATION */
#define  SCSI_hBTATABLE(hhcb)                SCSI_SU320_BTATABLE
#define  SCSI_hBTASIZE(hhcb)                 SCSI_SU320_BTASIZE
#if !SCSI_SEQ_LOAD_USING_PIO
#define  SCSI_hLOAD_SIZE(hhcb)               (SCSI_UEXACT16)sizeof(Seqload)
#define  SCSI_hLOAD_CODE(hhcb)               Seqload
#define  SCSI_hLOAD_ADDR(hhcb)               SCSI_SU320_LOAD_ADDR
#define  SCSI_hLOAD_COUNT(hhcb)              SCSI_SU320_LOAD_COUNT
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
#define  SCSI_hPASS_TO_DRIVER(hhcb)          SCSI_SU320_PASS_TO_DRIVER
#define  SCSI_hACTIVE_SCB(hhcb)              SCSI_SU320_ACTIVE_SCB
#define  SCSI_hRET_ADDR(hhcb)                SCSI_SU320_RET_ADDR
#define  SCSI_hQ_NEW_POINTER(hhcb)           SCSI_SU320_Q_NEW_POINTER
#define  SCSI_hENT_PT_BITMAP(hhcb)           SCSI_SU320_ENT_PT_BITMAP
#define  SCSI_hSG_STATUS(hhcb)               SCSI_SU320_SG_STATUS
#define  SCSI_hFIRST_SCB(hhcb)               SCSI_SU320_FIRST_SCB
#define  SCSI_hDVTIMER_LO(hhcb)              SCSI_SU320_DV_TIMERLO
#define  SCSI_hDVTIMER_HI(hhcb)              SCSI_SU320_DV_TIMERHI
#if SCSI_PACKETIZED_IO_SUPPORT
#define  SCSI_hSLUN_LENGTH(hhcb)             SCSI_SU320_SLUN_LENGTH
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_TARGET_OPERATION
#define  SCSI_hREG0(hhcb)                    SCSI_SU320_REG0
#define  SCSI_hQ_EST_HEAD(hhcb)              SCSI_SU320_Q_EST_HEAD
#define  SCSI_hQ_EST_TAIL(hhcb)              SCSI_SU320_Q_EST_TAIL
#define  SCSI_hQ_EXE_TARG_HEAD(hhcb)         SCSI_SU320_Q_EXE_TARG_HEAD
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_hARPINTVALID_REG(hhcb)         SCSI_SU320_ARPINTVALID_REG
#define  SCSI_hARPINTVALID_BIT(hhcb)         SCSI_SU320_ARPINTVALID_BIT

#define  SCSI_hSETUPSEQUENCER(hhcb) SCSIhStandardU320SetupSequencer((hhcb))
#define  SCSI_hRESETSOFTWARE(hhcb) SCSIhStandardU320ResetSoftware((hhcb))
#define  SCSI_hDELIVERSCB(hhcb,hiob) SCSIhStandardDeliverScb((hhcb),(hiob))
#define  SCSI_hSETUPATNLENGTH(hhcb,hiob) SCSIhStandardU320SetupAtnLength((hhcb),(hiob))

#define  SCSI_hSETELECINFORMATION(hhcb,targetID,xferRate) SCSIhStandardSetElecInformation((hhcb),(targetID),(xferRate))

#define  SCSI_hTARGETCLEARBUSY(hhcb,hiob) SCSIhStandardU320TargetClearBusy((hhcb),(hiob))
#define  SCSI_hREQUESTSENSE(hhcb,hiob) SCSIhStandardU320RequestSense((hhcb),(hiob))
#define  SCSI_hRESETBTA(hhcb) SCSIhStandardU320ResetBTA((hhcb))
#define  SCSI_hGETCONFIGURATION(hhcb) SCSIhStandardGetConfig((hhcb))

#if SCSI_SCBBFR_BUILTIN
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb) SCSIhSetupAssignScbBuffer((hhcb))
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob) SCSIhAssignScbDescriptor((hhcb),(hiob))
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob) SCSIhFreeScbDescriptor((hhcb),(hiob))
#else
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb)
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob)
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob)
#endif /* SCSI_SCBBFR_BUILTIN */

#define  SCSI_hXFERRATEASSIGN(hhcb,i,xferRate) \
            SCSIhXferRateAssign((hhcb),(i),(xferRate))

#define  SCSI_hGETNEGOXFERRATE(hhcb,i,xferRate) \
            SCSIhGetNegoXferRate((hhcb),(i),(xferRate))

#define  SCSI_hABORTCHANNEL(hhcb,haStatus) \
            SCSIhStandardAbortChannel((hhcb),(haStatus))

#if (!SCSI_ASPI_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1)

#define  SCSI_hABORTHIOB(hhcb,hiob,haStatus) \
            SCSIhStandardAbortHIOB((hhcb),(hiob),(haStatus))
#define  SCSI_hENQUEHEADTR(hhcb,hiob)  SCSIhStandardQHeadTR((hhcb),(hiob))

#define  SCSI_hREMOVEACTIVEABORT(hhcb,hiob) \
            SCSIhStandardRemoveActiveAbort((hhcb),(hiob))
#define  SCSI_hACTIVEABORT(hhcb,hiob,haStatus) \
            SCSIhStandardU320ActiveAbort((hhcb),(hiob),(haStatus))
#define  SCSI_hPACKACTIVEABORT(hhcb,hiob) \
            SCSIhStandardU320PackActiveAbort((hhcb),(hiob))
#define  SCSI_hNONPACKACTIVEABORT(hhcb,hiob) \
            SCSIhStandardU320NonPackActiveAbort((hhcb),(hiob))

#define  SCSI_hSEARCHEXEQ(hhcb,hiob,haStatus,postHiob) \
            SCSIhStandardSearchExeQ((hhcb),(hiob),(haStatus),(postHiob))
#define  SCSI_hSEARCHSEQDONEQ(hhcb,hiob) \
            SCSIhStandardSearchSeqDoneQ((hhcb),(hiob))
#define  SCSI_hUPDATEABORTBITHOSTMEM(hhcb,hiob) \
		    SCSIhStandardU320UpdateAbortBitHostMem((hhcb),(hiob))
#else
#define  SCSI_hUPDATEABORTBITHOSTMEM(hhcb,hiob) 
#define  SCSI_hSEARCHSEQDONEQ(hhcb,hiob) 
#define  SCSI_hSEARCHEXEQ(hhcb,hiob,haStatus,postHiob) 
#define  SCSI_hABORTHIOB(hhcb,hiob,haStatus) 
#define  SCSI_hENQUEHEADTR(hhcb,hiob) 
#define  SCSI_hACTIVEABORT(hhcb,hiob,haStatus) 
#define  SCSI_hREMOVEACTIVEABORT(hhcb,hiob) 
#define  SCSI_hPACKACTIVEABORT(hhcb,hiob)
#define  SCSI_hNONPACKACTIVEABORT(hhcb,hiob)
#endif /* (!SCSI_ASPI_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1) */
       
#define  SCSI_hUPDATENEXTSCBADDRESS(hhcb,hiob,ScbBusAddress) \
           SCSIhStandardU320UpdateNextScbAddress((hhcb),(hiob),(ScbBusAddress))
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
#define  SCSI_hSETUPTARGETRESET(hhcb,hiob) SCSIhStandardU320SetupTR((hhcb),(hiob))
#define  SCSI_hSETBREAKPOINT(hhcb,entryBitMap) \
            SCSIhStandardSetBreakPoint((hhcb),(entryBitMap))
#define  SCSI_hCLEARBREAKPOINT(hhcb,entryBitMap) \
            SCSIhStandardClearBreakPoint((hhcb),(entryBitMap))
#else
#define  SCSI_hSETBREAKPOINT(hhcb,entryBitMap) 
#define  SCSI_hCLEARBREAKPOINT(hhcb,entryBitMap) 
#define  SCSI_hSETUPTARGETRESET(hhcb,hiob) 
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */
#define  SCSI_hRESIDUECALC(hhcb,hiob) SCSIhStandardU320ResidueCalc((hhcb),(hiob))

#define  SCSI_hIGNOREWIDERESIDUECALC(hhcb,hiob,ignoreXfer) \
            SCSIhStandardU320IgnoreWideResidueCalc((hhcb),(hiob),(ignoreXfer))

#define  SCSI_hEVENIOLENGTH(hhcb,hiob) \
            SCSIhEvenIOLength((hhcb),(hiob))

#define  SCSI_hUNDERRUN(hhcb)  SCSIhStandardU320Underrun((hhcb))

#define  SCSI_hFREEZEHWQUEUE(hhcb,hiob,event) \
            SCSIhStandardFreezeHWQueue((hhcb),(hiob),(event))
#define  SCSI_hPACKNONPACKQUEUEHIOB(hhcb,hiob) \
            SCSIhStandardPackNonPackQueueHIOB((hhcb),(hiob))
            
#define  SCSI_hUPDATEEXEQATNLENGTH(hhcb,targetID) \
            SCSIhStandardU320UpdateExeQAtnLength((hhcb),(targetID))
#define  SCSI_hUPDATENEWQATNLENGTH(hhcb,targetID) \
            SCSIhStandardUpdateNewQAtnLength((hhcb),(targetID))

#define  SCSI_hQHEADPNPSWITCHSCB(hhcb,hiob) \
            SCSIhStandardU320QHeadPNPSwitchSCB((hhcb),(hiob))
            
#define  SCSI_hSETDISCONNECTDELAY(hhcb)

#define  SCSI_hUPDATEEXEQ(hhcb,hiob) \
            SCSIhStandardUpdateExeQ((hhcb),(hiob))

#define  SCSI_hQEXETARGNEXTOFFSET(hhcb) \
            (SCSI_UEXACT8)OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_q_exetarg_next)

#define  SCSI_hQNEXTOFFSET(hhcb) \
            (SCSI_UEXACT8)OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_q_next)

#if SCSI_TARGET_OPERATION 
#define  SCSI_hTARGETSETIGNOREWIDEMSG(hhcb,hiob) \
            SCSIhTargetStandardU320SetIgnoreWideMsg((hhcb),(hiob)) 
#define  SCSI_hTARGETGETESTSCBFIELDS(hhcb,hiob,flag) \
            SCSIhTargetStandardU320GetEstScbFields((hhcb),(hiob),(flag))
#define  SCSI_hTARGETSETFIRMWAREPROFILE(hhcb) \
            SCSIhTargetStandardU320SetFirmwareProfile((hhcb))
#define  SCSI_hTARGETDELIVERESTSCB(hhcb,hiob) \
            SCSIhTargetStandardU320DeliverEstScb((hhcb),(hiob))
#define  SCSI_hTARGETRESETSOFTWARE(hhcb) \
               SCSIhTargetStandardU320ResetSoftware((hhcb))
#define  SCSI_hTARGETSENDHIOBSPECIAL(hhcb,hiob) \
            SCSIhTargetStandardSendHiobSpecial((hhcb),(hiob))

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT            
#define  SCSI_hTARGETSELINTYPE(hhcb,hiob)   SCSI_LEGACY_SELECTION_IN   
#define  SCSI_hTARGETIUSELIN(hhcb,hiob)
#define  SCSI_hTARGETBUILDSTATUSIU(hhcb,hiob)
#define  SCSI_hTARGETASSIGNERRORSCBDESCRIPTOR(hhcb) SCSI_SUCCESS
#define  SCSI_hTARGETINITERRORHIOB(hhcb)
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

#else
#define  SCSI_hTARGETRESETSOFTWARE(hhcb)
#endif /* SCSI_TARGET_OPERATION */

#if (OSD_BUS_ADDRESS_SIZE == 64)
#define  SCSI_hCOMPAREBUSSGLISTADDRESS(hhcb,busAddress1,busAddress2) \
            SCSIhCompareBusSGListAddress((hhcb),(busAddress1),(busAddress2))  

#define  SCSI_hPUTBUSADDRESSSG(hhcb,dmaStruct,offset,value) \
            if (((hhcb)->SCSI_HF_SgBusAddress32 == 1) && \
                ((hhcb)->firmwareMode == SCSI_FMODE_STANDARD_U320))\
            {\
               SCSI_PUT_LITTLE_ENDIAN64SG((hhcb),(dmaStruct),(offset),(value));\
            }\
            else\
            {\
               SCSI_PUT_LITTLE_ENDIAN64((hhcb),(dmaStruct),(offset),(value));\
            }
#define  SCSI_hGETBUSADDRESSSGPAD(hhcb,pValue,dmaStruct,offset) \
            if (((hhcb)->SCSI_HF_SgBusAddress32 == 1) && \
                ((hhcb)->firmwareMode == SCSI_FMODE_STANDARD_U320))\
            {\
               SCSI_GET_LITTLE_ENDIAN64SGPAD((hhcb),(pValue),(dmaStruct),(offset));\
            }\
            else\
            {\
               SCSI_GET_LITTLE_ENDIAN64((hhcb),(pValue),(dmaStruct),(offset));\
            }
#else
#define  SCSI_hCOMPAREBUSSGLISTADDRESS(hhcb,busAddress1,busAddress2) \
            SCSIhCompareBusAddress((busAddress1),(busAddress2))
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */

#define  SCSI_hSETINTRFACTORTHRESHOLD(hhcb,Factorthreshold) \
            SCSIhStandardU320SetIntrFactorThreshold((hhcb),(Factorthreshold))

#define  SCSI_hSETINTRTHRESHOLDCOUNT(hhcb,thresholdCount) \
            SCSIhStandardU320SetIntrThresholdCount((hhcb),(thresholdCount))

#endif /* SCSI_STANDARD_U320_MODE */

#if SCSI_STANDARD_ENHANCED_U320_MODE
/***************************************************************************
* Macros for standard enhanced ultra 320 mode exclusively
***************************************************************************/
#define  SCSI_hSIZE_OF_SCB_BUFFER(mode) SCSI_SU320_SIZE_SCB
#define  SCSI_hALIGNMENTSCBBUFFER(mode) SCSI_hSIZE_OF_SCB_BUFFER((mode))-1
#define  SCSI_hSETUPFIRMWAREDESC(hhcb)

#define  SCSI_hFIRMWARE_VERSION(hhcb)        SCSI_SU320_FIRMWARE_VERSION
#define  SCSI_hSIOSTR3_ENTRY(hhcb)           SCSI_SU320_SIOSTR3_ENTRY
#define  SCSI_hRETRY_IDENTIFY_ENTRY(hhcb)    SCSI_SU320_RETRY_IDENTIFY_ENTRY
#define  SCSI_hSTART_LINK_CMD_ENTRY(hhcb)    SCSI_SU320_START_LINK_CMD_ENTRY
#define  SCSI_hIDLE_LOOP_ENTRY(hhcb)         SCSI_SU320_IDLE_LOOP_ENTRY
#define  SCSI_hIDLE_LOOP_TOP(hhcb)           SCSI_SU320_IDLE_LOOP_TOP
#define  SCSI_hIDLE_LOOP0(hhcb)              SCSI_SEU320_IDLE_LOOP0
#define  SCSI_hSIO204_ENTRY(hhcb)            SCSI_SU320_SIO204_ENTRY
#define  SCSI_hEXPANDER_BREAK(hhcb)          SCSI_SEU320_EXPANDER_BREAK
#define  SCSI_hISR(hhcb)                     SCSI_SU320_ISR
#if SCSI_TARGET_OPERATION 
#define  SCSI_hTARGET_DATA_ENTRY(hhcb)       SCSI_SU320_TARGET_DATA_ENTRY
#define  SCSI_hTARGET_HELD_BUS_ENTRY(hhcb)   SCSI_SU320_TARGET_HELD_BUS_ENTRY
#endif /* SCSI_TARGET_OPERATION */
#define  SCSI_hBTATABLE(hhcb)                SCSI_SU320_BTATABLE
#define  SCSI_hBTASIZE(hhcb)                 SCSI_SU320_BTASIZE
#if !SCSI_SEQ_LOAD_USING_PIO
#define  SCSI_hLOAD_SIZE(hhcb)               (SCSI_UEXACT16)sizeof(Seqeload)
#define  SCSI_hLOAD_CODE(hhcb)               Seqeload
#define  SCSI_hLOAD_ADDR(hhcb)               SCSI_SU320_LOAD_ADDR
#define  SCSI_hLOAD_COUNT(hhcb)              SCSI_SU320_LOAD_COUNT
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
#define  SCSI_hPASS_TO_DRIVER(hhcb)          SCSI_SU320_PASS_TO_DRIVER
#define  SCSI_hACTIVE_SCB(hhcb)              SCSI_SU320_ACTIVE_SCB
#define  SCSI_hRET_ADDR(hhcb)                SCSI_SU320_RET_ADDR
#define  SCSI_hQ_NEW_POINTER(hhcb)           SCSI_SU320_Q_NEW_POINTER
#define  SCSI_hENT_PT_BITMAP(hhcb)           SCSI_SU320_ENT_PT_BITMAP
#define  SCSI_hSG_STATUS(hhcb)               SCSI_SU320_SG_STATUS
#define  SCSI_hFIRST_SCB(hhcb)               SCSI_SEU320_FIRST_SCB
#define  SCSI_hDVTIMER_LO(hhcb)              SCSI_SEU320_DV_TIMERLO
#define  SCSI_hDVTIMER_HI(hhcb)              SCSI_SEU320_DV_TIMERHI
#if SCSI_PACKETIZED_IO_SUPPORT
#define  SCSI_hSLUN_LENGTH(hhcb)             SCSI_SEU320_SLUN_LENGTH
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_TARGET_OPERATION
#define  SCSI_hREG0(hhcb)                    SCSI_SEU320_REG0
#define  SCSI_hQ_EST_HEAD(hhcb)              SCSI_SEU320_Q_EST_HEAD
#define  SCSI_hQ_EST_TAIL(hhcb)              SCSI_SEU320_Q_EST_TAIL
#define  SCSI_hQ_EXE_TARG_HEAD(hhcb)         SCSI_SEU320_Q_EXE_TARG_HEAD
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_hARPINTVALID_REG(hhcb)         SCSI_SEU320_ARPINTVALID_REG
#define  SCSI_hARPINTVALID_BIT(hhcb)         SCSI_SEU320_ARPINTVALID_BIT

#define  SCSI_hSETUPSEQUENCER(hhcb) SCSIhStandardEnhU320SetupSequencer((hhcb))
#define  SCSI_hRESETSOFTWARE(hhcb) SCSIhStandardU320ResetSoftware((hhcb))
#define  SCSI_hDELIVERSCB(hhcb,hiob) SCSIhStandardDeliverScb((hhcb),(hiob))
#define  SCSI_hSETUPATNLENGTH(hhcb,hiob) SCSIhStandardEnhU320SetupAtnLength((hhcb),(hiob))



#define  SCSI_hTARGETCLEARBUSY(hhcb,hiob) SCSIhStandardU320TargetClearBusy((hhcb),(hiob))
#define  SCSI_hREQUESTSENSE(hhcb,hiob) SCSIhStandardEnhU320RequestSense((hhcb),(hiob))
#define  SCSI_hRESETBTA(hhcb) SCSIhStandardU320ResetBTA((hhcb))
#define  SCSI_hGETCONFIGURATION(hhcb) SCSIhStandardGetConfig((hhcb))

#if SCSI_SCBBFR_BUILTIN
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb) SCSIhSetupAssignScbBuffer((hhcb))
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob) SCSIhAssignScbDescriptor((hhcb),(hiob))
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob) SCSIhFreeScbDescriptor((hhcb),(hiob))
#else
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb)
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob)
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob)
#endif /* SCSI_SCBBFR_BUILTIN */

#define  SCSI_hXFERRATEASSIGN(hhcb,i,xferRate) \
            SCSIhXferRateAssign((hhcb),(i),(xferRate))

#define  SCSI_hGETNEGOXFERRATE(hhcb,i,xferRate) \
            SCSIhGetNegoXferRate((hhcb),(i),(xferRate))

#define  SCSI_hABORTCHANNEL(hhcb,haStatus) \
            SCSIhStandardAbortChannel((hhcb),(haStatus))

#if (!SCSI_ASPI_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1)
#define  SCSI_hABORTHIOB(hhcb,hiob,haStatus) \
            SCSIhStandardAbortHIOB((hhcb),(hiob),(haStatus))
#define  SCSI_hENQUEHEADTR(hhcb,hiob) SCSIhStandardQHeadTR((hhcb),(hiob))
#define  SCSI_hREMOVEACTIVEABORT(hhcb,hiob) \
            SCSIhStandardRemoveActiveAbort((hhcb),(hiob))

#define  SCSI_hACTIVEABORT(hhcb,hiob,haStatus) \
            SCSIhStandardEnhU320ActiveAbort((hhcb),(hiob),(haStatus))
#define  SCSI_hPACKACTIVEABORT(hhcb,hiob) \
            SCSIhStandardEnhU320PackActiveAbort((hhcb),(hiob))
#define  SCSI_hNONPACKACTIVEABORT(hhcb,hiob) \
            SCSIhStandardEnhU320NonPackActiveAbort((hhcb),(hiob))

#define  SCSI_hSEARCHEXEQ(hhcb,hiob,haStatus,postHiob) \
            SCSIhStandardSearchExeQ((hhcb),(hiob),(haStatus),(postHiob))
#define  SCSI_hSEARCHSEQDONEQ(hhcb,hiob) \
            SCSIhStandardSearchSeqDoneQ((hhcb),(hiob))
#define  SCSI_hUPDATEABORTBITHOSTMEM(hhcb,hiob) \
            SCSIhStandardEnhU320UpdateAbortBitHostMem((hhcb),(hiob))

#else 
#define  SCSI_hUPDATEABORTBITHOSTMEM(hhcb,hiob) 
#define  SCSI_hSEARCHSEQDONEQ(hhcb,hiob) 
#define  SCSI_hSEARCHEXEQ(hhcb,hiob,haStatus,postHiob) 
#define  SCSI_hABORTHIOB(hhcb,hiob,haStatus) 
#define  SCSI_hENQUEHEADTR(hhcb,hiob) 
#define  SCSI_hREMOVEACTIVEABORT(hhcb,hiob) 
#define  SCSI_hACTIVEABORT(hhcb,hiob,haStatus) 
#define  SCSI_hPACKACTIVEABORT(hhcb,hiob)
#define  SCSI_hNONPACKACTIVEABORT(hhcb,hiob)
#endif /* (!SCSI_ASPI_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1) */

            
#define  SCSI_hUPDATENEXTSCBADDRESS(hhcb,hiob,ScbBusAddress) \
           SCSIhStandardEnhU320UpdateNextScbAddress((hhcb),(hiob),(ScbBusAddress))
#if !SCSI_ASPI_SUPPORT_GROUP1
#define  SCSI_hSETUPTARGETRESET(hhcb,hiob) SCSIhStandardEnhU320SetupTR((hhcb),(hiob))

#define  SCSI_hSETBREAKPOINT(hhcb,entryBitMap) \
            SCSIhStandardSetBreakPoint((hhcb),(entryBitMap))
#define  SCSI_hCLEARBREAKPOINT(hhcb,entryBitMap) \
            SCSIhStandardClearBreakPoint((hhcb),(entryBitMap))
#else
#define  SCSI_hSETBREAKPOINT(hhcb,entryBitMap) 
#define  SCSI_hCLEARBREAKPOINT(hhcb,entryBitMap) 
#define  SCSI_hSETUPTARGETRESET(hhcb,hiob) 
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
#define  SCSI_hRESIDUECALC(hhcb,hiob) SCSIhStandardEnhU320ResidueCalc((hhcb),(hiob))
#define  SCSI_hSETELECINFORMATION(hhcb,targetID,xferRate) SCSIhStandardSetElecInformation((hhcb),(targetID),(xferRate))
#define  SCSI_hIGNOREWIDERESIDUECALC(hhcb,hiob,ignoreXfer) \
            SCSIhStandardEnhU320IgnoreWideResidueCalc((hhcb),(hiob),(ignoreXfer))

#define  SCSI_hEVENIOLENGTH(hhcb,hiob) \
            SCSIhEvenIOLength((hhcb),(hiob))

#define  SCSI_hUNDERRUN(hhcb)  SCSIhStandardEnhU320Underrun((hhcb))

#define  SCSI_hFREEZEHWQUEUE(hhcb,hiob,event) \
            SCSIhStandardFreezeHWQueue((hhcb),(hiob),(event))
#define  SCSI_hPACKNONPACKQUEUEHIOB(hhcb,hiob) \
            SCSIhStandardPackNonPackQueueHIOB((hhcb),(hiob))
            
#define  SCSI_hUPDATEEXEQATNLENGTH(hhcb,targetID) \
            SCSIhStandardEnhU320UpdateExeQAtnLength((hhcb),(targetID))
#define  SCSI_hUPDATENEWQATNLENGTH(hhcb,targetID) \
            SCSIhStandardUpdateNewQAtnLength((hhcb),(targetID))

#define  SCSI_hQHEADPNPSWITCHSCB(hhcb,hiob) \
            SCSIhStandardEnhU320QHeadPNPSwitchSCB((hhcb),(hiob))
            
#define  SCSI_hSETDISCONNECTDELAY(hhcb)

#define  SCSI_hUPDATEEXEQ(hhcb,hiob) \
            SCSIhStandardUpdateExeQ((hhcb),(hiob))

#define  SCSI_hQEXETARGNEXTOFFSET(hhcb) \
            (SCSI_UEXACT8)OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,\
                                      SCSI_SEU320_q_exetarg_next)

#define  SCSI_hQNEXTOFFSET(hhcb) \
            (SCSI_UEXACT8)OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,\
                                      SCSI_SEU320_q_next)

#if SCSI_TARGET_OPERATION 
#define  SCSI_hTARGETSETIGNOREWIDEMSG(hhcb,hiob) \
            SCSIhTargetStandardEnhU320SetIgnoreWideMsg((hhcb),(hiob)) 
#define  SCSI_hTARGETGETESTSCBFIELDS(hhcb,hiob,flag) \
            SCSIhTargetStandardEnhU320GetEstScbFields((hhcb),(hiob),(flag))
#define  SCSI_hTARGETSETFIRMWAREPROFILE(hhcb) \
            SCSIhTargetStandardEnhU320SetFirmwareProfile((hhcb))
#define  SCSI_hTARGETDELIVERESTSCB(hhcb,hiob) \
            SCSIhTargetStandardU320DeliverEstScb((hhcb),(hiob))
#define  SCSI_hTARGETRESETSOFTWARE(hhcb) \
               SCSIhTargetStandardEnhU320ResetSoftware((hhcb))
#define  SCSI_hTARGETSENDHIOBSPECIAL(hhcb,hiob) \
            SCSIhTargetStandardSendHiobSpecial((hhcb),(hiob))

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT            
#define  SCSI_hTARGETSELINTYPE(hhcb,hiob)   SCSIhTargetStandardEnhU320SelInType((hhcb),(hiob)) 
#define  SCSI_hTARGETIUSELIN(hhcb,hiob)     SCSIhTargetStandardEnhU320IUSelIn((hhcb),(hiob))
#define  SCSI_hTARGETBUILDSTATUSIU(hhcb,hiob) \
            SCSIhTargetStandardEnhU320BuildStatusIU((hhcb),(hiob))
#define  SCSI_hTARGETASSIGNERRORSCBDESCRIPTOR(hhcb) \
            SCSIhTargetStandardEnhU320AssignErrorScbDescriptor((hhcb))
#define  SCSI_hTARGETINITERRORHIOB(hhcb)   SCSIhTargetStandardEnhU320InitErrorHIOB((hhcb))
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
            
#else
#define  SCSI_hTARGETRESETSOFTWARE(hhcb)
#endif /* SCSI_TARGET_OPERATION */

#if (OSD_BUS_ADDRESS_SIZE == 64)
#define  SCSI_hCOMPAREBUSSGLISTADDRESS(hhcb,busAddress1,busAddress2) \
            SCSIhCompareBusSGListAddress((hhcb),(busAddress1),(busAddress2))  

#define  SCSI_hPUTBUSADDRESSSG(hhcb,dmaStruct,offset,value) \
            if (((hhcb)->SCSI_HF_SgBusAddress32 == 1) && \
                ((hhcb)->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320))\
            {\
               SCSI_PUT_LITTLE_ENDIAN64SG((hhcb),(dmaStruct),(offset),(value));\
            }\
            else\
            {\
               SCSI_PUT_LITTLE_ENDIAN64((hhcb),(dmaStruct),(offset),(value));\
            }
#define  SCSI_hGETBUSADDRESSSGPAD(hhcb,pValue,dmaStruct,offset) \
            if (((hhcb)->SCSI_HF_SgBusAddress32 == 1) && \
                ((hhcb)->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320))\
            {\
               SCSI_GET_LITTLE_ENDIAN64SGPAD((hhcb),(pValue),(dmaStruct),(offset));\
            }\
            else\
            {\
               SCSI_GET_LITTLE_ENDIAN64((hhcb),(pValue),(dmaStruct),(offset));\
            }
#else
#define  SCSI_hCOMPAREBUSSGLISTADDRESS(hhcb,busAddress1,busAddress2) \
            SCSIhCompareBusAddress((busAddress1),(busAddress2))
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */

#define  SCSI_hSETINTRFACTORTHRESHOLD(hhcb,Factorthreshold) \
            SCSIhStandardU320SetIntrFactorThreshold((hhcb),(Factorthreshold))

#define  SCSI_hSETINTRTHRESHOLDCOUNT(hhcb,thresholdCount) \
            SCSIhStandardU320SetIntrThresholdCount((hhcb),(thresholdCount))

#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

#if SCSI_DOWNSHIFT_U320_MODE
/***************************************************************************
* Macros for downshift ultra 320 mode exclusively
***************************************************************************/
#define  SCSI_hSIZE_OF_SCB_BUFFER(mode) SCSI_DU320_SIZE_SCB
#define  SCSI_hALIGNMENTSCBBUFFER(mode) SCSI_hSIZE_OF_SCB_BUFFER((mode))-1
#define  SCSI_hSETUPFIRMWAREDESC(hhcb)

#define  SCSI_hFIRMWARE_VERSION(hhcb)        SCSI_DU320_FIRMWARE_VERSION
#define  SCSI_hSIOSTR3_ENTRY(hhcb)           SCSI_DU320_SIOSTR3_ENTRY
#define  SCSI_hRETRY_IDENTIFY_ENTRY(hhcb)    SCSI_DU320_RETRY_IDENTIFY_ENTRY
#define  SCSI_hSTART_LINK_CMD_ENTRY(hhcb)    SCSI_DU320_START_LINK_CMD_ENTRY
#define  SCSI_hIDLE_LOOP_ENTRY(hhcb)         SCSI_DU320_IDLE_LOOP_ENTRY
#define  SCSI_hIDLE_LOOP_TOP(hhcb)           SCSI_DU320_IDLE_LOOP_TOP
#define  SCSI_hIDLE_LOOP0(hhcb)              SCSI_DU320_IDLE_LOOP0
#define  SCSI_hSIO204_ENTRY(hhcb)            SCSI_DU320_SIO204_ENTRY
#define  SCSI_hEXPANDER_BREAK(hhcb)          SCSI_DU320_EXPANDER_BREAK

#define  SCSI_hBTATABLE(hhcb)                SCSI_DU320_BTATABLE
#define  SCSI_hBTASIZE(hhcb)                 SCSI_DU320_BTASIZE
#if !SCSI_SEQ_LOAD_USING_PIO
#define  SCSI_hLOAD_SIZE(hhcb)               (SCSI_UEXACT16)sizeof(Seqload)
#define  SCSI_hLOAD_CODE(hhcb)               Seqload
#define  SCSI_hLOAD_ADDR(hhcb)               SCSI_DU320_LOAD_ADDR
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
#define  SCSI_hLOAD_COUNT(hhcb)              SCSI_DU320_LOAD_COUNT
#define  SCSI_hPASS_TO_DRIVER(hhcb)          SCSI_DU320_PASS_TO_DRIVER
#define  SCSI_hACTIVE_SCB(hhcb)              SCSI_DU320_ACTIVE_SCB
#define  SCSI_hRET_ADDR(hhcb)                SCSI_DU320_RET_ADDR
#define  SCSI_hQ_NEW_POINTER(hhcb)           SCSI_DU320_Q_NEW_POINTER
#define  SCSI_hENT_PT_BITMAP(hhcb)           SCSI_DU320_ENT_PT_BITMAP
#define  SCSI_hSG_STATUS(hhcb)               SCSI_DU320_SG_STATUS

#if SCSI_BIOS_SUPPORT
#define  SCSI_hBIOS_MIN_SCB_NUMBER(mode)     SCSI_DU320_BIOS_MIN_SCB_NUMBER
#define  SCSI_hBIOS_MAX_SCB_NUMBER(mode)     SCSI_DU320_BIOS_MAX_SCB_NUMBER
#endif /* SCSI_BIOS_SUPPORT */

#define  SCSI_hARPINTVALID_REG(hhcb)         SCSI_DU320_ARPINTVALID_REG
#define  SCSI_hARPINTVALID_BIT(hhcb)         SCSI_DU320_ARPINTVALID_BIT

#define  SCSI_hSETUPSEQUENCER(hhcb) SCSIhDownshiftU320SetupSequencer((hhcb))
#define  SCSI_hRESETSOFTWARE(hhcb) SCSIhDownshiftU320ResetSoftware((hhcb))
#define  SCSI_hDELIVERSCB(hhcb,hiob) SCSIhDownshiftDeliverScb((hhcb),(hiob))
#define  SCSI_hSETUPATNLENGTH(hhcb,hiob) SCSIhDownshiftU320SetupAtnLength((hhcb),(hiob))

#define  SCSI_hSETCOMMANDREQUESTOR(hhcb,hiob)\
            SCSIhDownShiftU320SetCommandRequestor((hhcb),(hiob))

#define  SCSI_hSETELECINFORMATION(hhcb,targetID,xferRate) SCSIhDownshiftSetElecInformation((hhcb),(targetID),(xferRate))

#define  SCSI_hENQUEHEADTR(hhcb,hiob)
#define  SCSI_hTARGETCLEARBUSY(hhcb,hiob) SCSIhDownshiftU320TargetClearBusy((hhcb),(hiob))
#define  SCSI_hREQUESTSENSE(hhcb,hiob) SCSIhDownshiftU320RequestSense((hhcb),(hiob))
#define  SCSI_hRESETBTA(hhcb) SCSIhDownshiftU320ResetBTA((hhcb))
#define  SCSI_hGETCONFIGURATION(hhcb) SCSIhDownshiftGetConfig((hhcb))

#if (SCSI_SCBBFR_BUILTIN && (!SCSI_BIOS_SUPPORT))
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb) SCSIhSetupAssignScbBuffer((hhcb))
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob) SCSIhAssignScbDescriptor((hhcb),(hiob))
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob) SCSIhFreeScbDescriptor((hhcb),(hiob))
#else /* !(SCSI_SCBBFR_BUILTIN && (!SCSI_BIOS_SUPPORT)) */
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb)
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob)
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob)
#endif /* (SCSI_SCBBFR_BUILTIN && (!SCSI_BIOS_SUPPORT)) */

#define  SCSI_hXFERRATEASSIGN(hhcb,i,xferRate) \
            SCSIhXferRateAssign((hhcb),(i),(xferRate))

#define  SCSI_hGETNEGOXFERRATE(hhcb,i,xferRate) \
            SCSIhGetNegoXferRate((hhcb),(i),(xferRate))

#define  SCSI_hABORTCHANNEL(hhcb,haStatus) \
            SCSIhDownshiftAbortChannel((hhcb),(haStatus))
			
#define  SCSI_hABORTHIOB(hhcb,hiob,haStatus)
#define  SCSI_hACTIVEABORT(hhcb,hiob,haStatus)
#define  SCSI_hREMOVEACTIVEABORT(hhcb,hiob)
#define  SCSI_hSEARCHSEQDONEQ(hhcb,hiob)
#define  SCSI_hSEARCHEXEQ(hhcb,hiob,haStatus,postHiob)
#define  SCSI_hUPDATEABORTBITHOSTMEM(hhcb,hiob)
#define  SCSI_hUPDATENEXTSCBADDRESS(hhcb,hiob,ScbBusAddress)
#define  SCSI_hSETUPTARGETRESET(hhcb,hiob)


#define  SCSI_hRESIDUECALC(hhcb,hiob) SCSIhDownshiftU320ResidueCalc((hhcb),(hiob))

#define  SCSI_hIGNOREWIDERESIDUECALC(hhcb,hiob,ignoreXfer) \
            SCSIhDownshiftU320IgnoreWideResidueCalc((hhcb),(hiob),(ignoreXfer))

#define  SCSI_hEVENIOLENGTH(hhcb,hiob) \
            SCSIhEvenIOLength((hhcb),(hiob))

#define  SCSI_hUNDERRUN(hhcb)  SCSIhDownshiftU320Underrun((hhcb))

#define  SCSI_hSETBREAKPOINT(hhcb,entryBitMap)
#define  SCSI_hCLEARBREAKPOINT(hhcb,entryBitMap)
#define  SCSI_hFREEZEHWQUEUE(hhcb,hiob,event) 
#define  SCSI_hPACKNONPACKQUEUEHIOB(hhcb,hiob) 
#define  SCSI_hSETINTRTHRESHOLDCOUNT(hhcb,threshold)
#define  SCSI_hSETINTRFACTORTHRESHOLD(hhcb,Factorthreshold) 

            
#if SCSI_BIOS_SUPPORT
#define  SCSI_hUPDATEEXEQATNLENGTH(hhcb,targetID)
#define  SCSI_hUPDATENEWQATNLENGTH(hhcb,targetID)
#else /* !SCSI_BIOS_SUPPORT */
#define  SCSI_hUPDATEEXEQATNLENGTH(hhcb,targetID) \
            SCSIhDownshiftU320UpdateExeQAtnLength((hhcb),(targetID))
#define  SCSI_hUPDATENEWQATNLENGTH(hhcb,targetID) \
            SCSIhDownshiftUpdateNewQAtnLength((hhcb),(targetID))
#endif /* SCSI_BIOS_SUPPORT */

#define  SCSI_hQHEADPNPSWITCHSCB(hhcb,hiob) \
            SCSIhDownshiftU320QHeadPNPSwitchSCB((hhcb),(hiob))
            
#define  SCSI_hSETDISCONNECTDELAY(hhcb)

#define  SCSI_hUPDATEEXEQ(hhcb,hiob) \
            SCSIhDownshiftU320UpdateExeQ((hhcb),(hiob))

#endif /* SCSI_DOWNSHIFT_U320_MODE */


#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
/***************************************************************************
* Macros for downshift enhanced  ultra 320 mode exclusively
***************************************************************************/
#define  SCSI_hSIZE_OF_SCB_BUFFER(mode) SCSI_DU320_SIZE_SCB
#define  SCSI_hALIGNMENTSCBBUFFER(mode) SCSI_hSIZE_OF_SCB_BUFFER((mode))-1
#define  SCSI_hSETUPFIRMWAREDESC(hhcb)

#define  SCSI_hFIRMWARE_VERSION(hhcb)        SCSI_DU320_FIRMWARE_VERSION
#define  SCSI_hSIOSTR3_ENTRY(hhcb)           SCSI_DU320_SIOSTR3_ENTRY
#define  SCSI_hRETRY_IDENTIFY_ENTRY(hhcb)    SCSI_DU320_RETRY_IDENTIFY_ENTRY
#define  SCSI_hSTART_LINK_CMD_ENTRY(hhcb)    SCSI_DU320_START_LINK_CMD_ENTRY
#define  SCSI_hIDLE_LOOP_ENTRY(hhcb)         SCSI_DU320_IDLE_LOOP_ENTRY
#define  SCSI_hIDLE_LOOP_TOP(hhcb)           SCSI_DU320_IDLE_LOOP_TOP
#define  SCSI_hIDLE_LOOP0(hhcb)              SCSI_DU320_IDLE_LOOP0
#define  SCSI_hSIO204_ENTRY(hhcb)            SCSI_DU320_SIO204_ENTRY
#define  SCSI_hEXPANDER_BREAK(hhcb)          SCSI_DU320_EXPANDER_BREAK

#define  SCSI_hBTATABLE(hhcb)                SCSI_DU320_BTATABLE
#define  SCSI_hBTASIZE(hhcb)                 SCSI_DU320_BTASIZE
#if !SCSI_SEQ_LOAD_USING_PIO
#define  SCSI_hLOAD_SIZE(hhcb)               (SCSI_UEXACT16)sizeof(Seqeload)
#define  SCSI_hLOAD_CODE(hhcb)               Seqeload
#define  SCSI_hLOAD_ADDR(hhcb)               SCSI_DU320_LOAD_ADDR
#define  SCSI_hLOAD_COUNT(hhcb)              SCSI_DU320_LOAD_COUNT
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
#define  SCSI_hPASS_TO_DRIVER(hhcb)          SCSI_DU320_PASS_TO_DRIVER
#define  SCSI_hACTIVE_SCB(hhcb)              SCSI_DU320_ACTIVE_SCB
#define  SCSI_hRET_ADDR(hhcb)                SCSI_DU320_RET_ADDR
#define  SCSI_hQ_NEW_POINTER(hhcb)           SCSI_DU320_Q_NEW_POINTER
#define  SCSI_hENT_PT_BITMAP(hhcb)           SCSI_DU320_ENT_PT_BITMAP
#define  SCSI_hSG_STATUS(hhcb)               SCSI_DU320_SG_STATUS

#if SCSI_BIOS_SUPPORT
#define  SCSI_hBIOS_MIN_SCB_NUMBER(mode)     SCSI_DU320_BIOS_MIN_SCB_NUMBER
#define  SCSI_hBIOS_MAX_SCB_NUMBER(mode)     SCSI_DU320_BIOS_MAX_SCB_NUMBER
#endif /* SCSI_BIOS_SUPPORT */

#define  SCSI_hARPINTVALID_REG(hhcb)         SCSI_DEU320_ARPINTVALID_REG
#define  SCSI_hARPINTVALID_BIT(hhcb)         SCSI_DEU320_ARPINTVALID_BIT

#define  SCSI_hSETUPSEQUENCER(hhcb) SCSIhDownshiftU320SetupSequencer((hhcb))
#define  SCSI_hRESETSOFTWARE(hhcb) SCSIhDownshiftU320ResetSoftware((hhcb))
#define  SCSI_hDELIVERSCB(hhcb,hiob) SCSIhDownshiftDeliverScb((hhcb),(hiob))
#define  SCSI_hSETUPATNLENGTH(hhcb,hiob) SCSIhDownshiftU320SetupAtnLength((hhcb),(hiob))

#define  SCSI_hSETCOMMANDREQUESTOR(hhcb,hiob)\
            SCSIhDownShiftU320SetCommandRequestor((hhcb),(hiob))

#define  SCSI_hENQUEHEADTR(hhcb,hiob)
#define  SCSI_hTARGETCLEARBUSY(hhcb,hiob) SCSIhDownshiftU320TargetClearBusy((hhcb),(hiob))
#define  SCSI_hREQUESTSENSE(hhcb,hiob) SCSIhDownshiftEnhU320RequestSense((hhcb),(hiob))
#define  SCSI_hRESETBTA(hhcb) SCSIhDownshiftU320ResetBTA((hhcb))
#define  SCSI_hGETCONFIGURATION(hhcb) SCSIhDownshiftGetConfig((hhcb))

#if (SCSI_SCBBFR_BUILTIN && (!SCSI_BIOS_SUPPORT))
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb) SCSIhSetupAssignScbBuffer((hhcb))
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob) SCSIhAssignScbDescriptor((hhcb),(hiob))
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob) SCSIhFreeScbDescriptor((hhcb),(hiob))
#else /* !(SCSI_SCBBFR_BUILTIN && (!SCSI_BIOS_SUPPORT)) */
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb)
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob)
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob)
#endif /* (SCSI_SCBBFR_BUILTIN && (!SCSI_BIOS_SUPPORT)) */

#define  SCSI_hXFERRATEASSIGN(hhcb,i,xferRate) \
            SCSIhXferRateAssign((hhcb),(i),(xferRate))

#define  SCSI_hSETELECINFORMATION(hhcb,targetID,xferRate) SCSIhDownshiftSetElecInformation((hhcb),(targetID),(xferRate))

#define  SCSI_hGETNEGOXFERRATE(hhcb,i,xferRate) \
            SCSIhGetNegoXferRate((hhcb),(i),(xferRate))

#define  SCSI_hABORTCHANNEL(hhcb,haStatus) \
            SCSIhDownshiftAbortChannel((hhcb),(haStatus))
#define  SCSI_hABORTHIOB(hhcb,hiob,haStatus)
#define  SCSI_hACTIVEABORT(hhcb,hiob,haStatus)
#define  SCSI_hREMOVEACTIVEABORT(hhcb,hiob)
#define  SCSI_hSEARCHSEQDONEQ(hhcb,hiob)
#define  SCSI_hSEARCHEXEQ(hhcb,hiob,haStatus,postHiob)
#define  SCSI_hUPDATEABORTBITHOSTMEM(hhcb,hiob)
#define  SCSI_hUPDATENEXTSCBADDRESS(hhcb,hiob,ScbBusAddress)
#define  SCSI_hSETUPTARGETRESET(hhcb,hiob)

#define  SCSI_hRESIDUECALC(hhcb,hiob) SCSIhDownshiftU320ResidueCalc((hhcb),(hiob))

#define  SCSI_hIGNOREWIDERESIDUECALC(hhcb,hiob,ignoreXfer) \
            SCSIhDownshiftU320IgnoreWideResidueCalc((hhcb),(hiob),(ignoreXfer))

#define  SCSI_hEVENIOLENGTH(hhcb,hiob) \
            SCSIhEvenIOLength((hhcb),(hiob))

#define  SCSI_hUNDERRUN(hhcb)  SCSIhDownshiftEnhU320Underrun((hhcb))

#define  SCSI_hSETBREAKPOINT(hhcb,entryBitMap)
#define  SCSI_hCLEARBREAKPOINT(hhcb,entryBitMap)
#define  SCSI_hFREEZEHWQUEUE(hhcb,hiob,event) 
#define  SCSI_hPACKNONPACKQUEUEHIOB(hhcb,hiob) 
#define  SCSI_hSETINTRTHRESHOLDCOUNT(hhcb,threshold)
#define  SCSI_hSETINTRFACTORTHRESHOLD(hhcb,Factorthreshold) 
            
#if SCSI_BIOS_SUPPORT
#define  SCSI_hUPDATEEXEQATNLENGTH(hhcb,targetID)
#define  SCSI_hUPDATENEWQATNLENGTH(hhcb,targetID)
#else /* !SCSI_BIOS_SUPPORT */
#define  SCSI_hUPDATEEXEQATNLENGTH(hhcb,targetID) \
            SCSIhDownshiftU320UpdateExeQAtnLength((hhcb),(targetID))
#define  SCSI_hUPDATENEWQATNLENGTH(hhcb,targetID) \
            SCSIhDownshiftUpdateNewQAtnLength((hhcb),(targetID))
#endif /* SCSI_BIOS_SUPPORT */

#define  SCSI_hQHEADPNPSWITCHSCB(hhcb,hiob) \
            SCSIhDownshiftEnhU320QHeadPNPSwitchSCB((hhcb),(hiob))
            
#define  SCSI_hSETDISCONNECTDELAY(hhcb)

#define  SCSI_hUPDATEEXEQ(hhcb,hiob) \
            SCSIhDownshiftU320UpdateExeQ((hhcb),(hiob))

#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */

#if SCSI_DCH_U320_MODE
/***************************************************************************
* Macros for DCH ultra 320 mode exclusively
***************************************************************************/
#define  SCSI_hSIZE_OF_SCB_BUFFER(mode) SCSI_DCHU320_SIZE_SCB
#define  SCSI_hALIGNMENTSCBBUFFER(mode) SCSI_hSIZE_OF_SCB_BUFFER((mode))-1
#define  SCSI_hSETUPFIRMWAREDESC(hhcb)

#define  SCSI_hFIRMWARE_VERSION(hhcb)        SCSI_DCHU320_FIRMWARE_VERSION
#define  SCSI_hSIOSTR3_ENTRY(hhcb)           SCSI_DCHU320_SIOSTR3_ENTRY
#define  SCSI_hRETRY_IDENTIFY_ENTRY(hhcb)    SCSI_DCHU320_RETRY_IDENTIFY_ENTRY
#define  SCSI_hSTART_LINK_CMD_ENTRY(hhcb)    SCSI_DCHU320_START_LINK_CMD_ENTRY
#define  SCSI_hIDLE_LOOP_ENTRY(hhcb)         SCSI_DCHU320_IDLE_LOOP_ENTRY
#define  SCSI_hIDLE_LOOP_TOP(hhcb)           SCSI_DCHU320_IDLE_LOOP_TOP
#define  SCSI_hIDLE_LOOP0(hhcb)              SCSI_DCHU320_IDLE_LOOP0
#define  SCSI_hSIO204_ENTRY(hhcb)            SCSI_DCHU320_SIO204_ENTRY
#define  SCSI_hEXPANDER_BREAK(hhcb)          SCSI_DCHU320_EXPANDER_BREAK
/* #define  SCSI_hISR(hhcb)                     SCSI_DCHU320_ISR */
#if SCSI_TARGET_OPERATION 
#define  SCSI_hTARGET_DATA_ENTRY(hhcb)       SCSI_DCHU320_TARGET_DATA_ENTRY
#define  SCSI_hTARGET_HELD_BUS_ENTRY(hhcb)   SCSI_DCHU320_TARGET_HELD_BUS_ENTRY
#endif /* SCSI_TARGET_OPERATION */
#define  SCSI_hBTATABLE(hhcb)                SCSI_DCHU320_BTATABLE
#define  SCSI_hBTASIZE(hhcb)                 SCSI_DCHU320_BTASIZE
#if !SCSI_SEQ_LOAD_USING_PIO
#define  SCSI_hLOAD_SIZE(hhcb)               (SCSI_UEXACT16)sizeof(Dchload)
#define  SCSI_hLOAD_CODE(hhcb)               Dchload
#define  SCSI_hLOAD_ADDR(hhcb)               SCSI_DCHU320_LOAD_ADDR
#define  SCSI_hLOAD_COUNT(hhcb)              SCSI_DCHU320_LOAD_COUNT
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
#define  SCSI_hPASS_TO_DRIVER(hhcb)          SCSI_DCHU320_PASS_TO_DRIVER
#define  SCSI_hACTIVE_SCB(hhcb)              SCSI_DCHU320_ACTIVE_SCB
#define  SCSI_hRET_ADDR(hhcb)                SCSI_DCHU320_RET_ADDR
#define  SCSI_hQ_NEW_POINTER(hhcb)           SCSI_DCHU320_Q_NEW_POINTER
#define  SCSI_hENT_PT_BITMAP(hhcb)           SCSI_DCHU320_ENT_PT_BITMAP
#define  SCSI_hSG_STATUS(hhcb)               SCSI_DCHU320_SG_STATUS
#define  SCSI_hFIRST_SCB(hhcb)               SCSI_DCHU320_FIRST_SCB
#define  SCSI_hDVTIMER_LO(hhcb)              SCSI_DCHU320_DV_TIMERLO
#define  SCSI_hDVTIMER_HI(hhcb)              SCSI_DCHU320_DV_TIMERHI
#if SCSI_PACKETIZED_IO_SUPPORT
#define  SCSI_hSLUN_LENGTH(hhcb)             SCSI_DCHEU320_SLUN_LENGTH
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_TARGET_OPERATION
#define  SCSI_hREG0(hhcb)                    SCSI_DCHU320_REG0
#define  SCSI_hQ_EST_HEAD(hhcb)              SCSI_DCHU320_Q_EST_HEAD
#define  SCSI_hQ_EST_TAIL(hhcb)              SCSI_DCHU320_Q_EST_TAIL
#define  SCSI_hQ_EXE_TARG_HEAD(hhcb)         SCSI_DCHU320_Q_EXE_TARG_HEAD
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_hARPINTVALID_REG(hhcb)         SCSI_DCHU320_ARPINTVALID_REG
#define  SCSI_hARPINTVALID_BIT(hhcb)         SCSI_DCHU320_ARPINTVALID_BIT

#define  SCSI_hSETUPSEQUENCER(hhcb) SCSIhDchU320SetupSequencer((hhcb))
#define  SCSI_hRESETSOFTWARE(hhcb) SCSIhDchU320ResetSoftware((hhcb))
#define  SCSI_hDELIVERSCB(hhcb,hiob) SCSIhStandardDeliverScb((hhcb),(hiob))
#define  SCSI_hSETUPATNLENGTH(hhcb,hiob) SCSIhDchU320SetupAtnLength((hhcb),(hiob))


#define  SCSI_hENQUEHEADTR(hhcb,hiob) SCSIhDchU320QHeadTR((hhcb),(hiob))

#define  SCSI_hTARGETCLEARBUSY(hhcb,hiob) SCSIhStandardU320TargetClearBusy((hhcb),(hiob))
#define  SCSI_hREQUESTSENSE(hhcb,hiob) SCSIhDchU320RequestSense((hhcb),(hiob))
#define  SCSI_hRESETBTA(hhcb) SCSIhStandardU320ResetBTA((hhcb))
#define  SCSI_hGETCONFIGURATION(hhcb) SCSIhStandardGetConfig((hhcb))

#if SCSI_SCBBFR_BUILTIN
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb) SCSIhSetupAssignScbBuffer((hhcb))
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob) SCSIhAssignScbDescriptor((hhcb),(hiob))
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob) SCSIhFreeScbDescriptor((hhcb),(hiob))
#else
#define  SCSI_hSETUPASSIGNSCBBUFFER(hhcb)
#define  SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob)
#define  SCSI_hFREESCBDESCRIPTOR(hhcb,hiob)
#endif /* SCSI_SCBBFR_BUILTIN */

#define  SCSI_hXFERRATEASSIGN(hhcb,i,xferRate) \
            SCSIhXferRateAssign((hhcb),(i),(xferRate))

#define  SCSI_hGETNEGOXFERRATE(hhcb,i,xferRate) \
            SCSIhGetNegoXferRate((hhcb),(i),(xferRate))

#define  SCSI_hABORTCHANNEL(hhcb,haStatus) \
            SCSIhStandardAbortChannel(hhcb,haStatus)

#define  SCSI_hABORTHIOB(hhcb,hiob,haStatus) \
            SCSIhStandardAbortHIOB((hhcb),(hiob),(haStatus))
#define  SCSI_hSETUPTARGETRESET(hhcb,hiob) SCSIhDchU320SetupTR((hhcb),(hiob))
#define  SCSI_hACTIVEABORT(hhcb,hiob,haStatus) \
            SCSIhDchU320ActiveAbort((hhcb),(hiob),(haStatus))

#define  SCSI_hREMOVEACTIVEABORT(hhcb,hiob) \
            SCSIhStandardRemoveActiveAbort(hhcb,hiob)
#define  SCSI_hPACKACTIVEABORT(hhcb,hiob) \
            SCSIhDchU320PackActiveAbort((hhcb),(hiob))
#define  SCSI_hNONPACKACTIVEABORT(hhcb,hiob) \
            SCSIhDchU320NonPackActiveAbort((hhcb),(hiob))

#define  SCSI_hSEARCHSEQDONEQ(hhcb,hiob) \
            SCSIhDchU320SearchSeqDoneQ((hhcb),(hiob))

#define  SCSI_hSEARCHEXEQ(hhcb,hiob,haStatus,postHiob) \
            SCSIhDchU320SearchExeQ((hhcb),(hiob),(haStatus),(postHiob))

#define  SCSI_hUPDATEABORTBITHOSTMEM(hhcb,hiob) \
            SCSIhDchU320UpdateAbortBitHostMem(hhcb,hiob)
#define  SCSI_hUPDATENEXTSCBADDRESS(hhcb,hiob,ScbBusAddress) \
           SCSIhDchU320UpdateNextScbAddress(hhcb,hiob,ScbBusAddress)


#define  SCSI_hRESIDUECALC(hhcb,hiob) SCSIhDchU320ResidueCalc(hhcb,hiob)

#define  SCSI_hSETELECINFORMATION(hhcb,targetID,xferRate) SCSIhStandardSetElecInformation((hhcb),(targetID),(xferRate))

#define  SCSI_hIGNOREWIDERESIDUECALC(hhcb,hiob,ignoreXfer) \
            SCSIhDchU320IgnoreWideResidueCalc(hhcb,hiob,ignoreXfer)

#define  SCSI_hEVENIOLENGTH(hhcb,hiob) \
            SCSIhDchEvenIOLength((hhcb),(hiob))

#define  SCSI_hUNDERRUN(hhcb)  SCSIhDchU320Underrun((hhcb))

#define  SCSI_hSETBREAKPOINT(hhcb,entryBitMap) \
            SCSIhStandardSetBreakPoint((hhcb),(entryBitMap))
#define  SCSI_hCLEARBREAKPOINT(hhcb,entryBitMap) \
            SCSIhStandardClearBreakPoint((hhcb),(entryBitMap))

#define  SCSI_hFREEZEHWQUEUE(hhcb,hiob,event) \
            SCSIhDchU320FreezeHWQueue(hhcb,hiob,event)
#define  SCSI_hPACKNONPACKQUEUEHIOB(hhcb,hiob) \
            SCSIhStandardPackNonPackQueueHIOB(hhcb,hiob)
            
#define  SCSI_hUPDATEEXEQATNLENGTH(hhcb,targetID) \
            SCSIhDchU320UpdateExeQAtnLength(hhcb,targetID)
#define  SCSI_hUPDATENEWQATNLENGTH(hhcb,targetID) \
            SCSIhStandardUpdateNewQAtnLength(hhcb,targetID)

#define  SCSI_hQHEADPNPSWITCHSCB(hhcb,hiob) \
            SCSIhDchU320QHeadPNPSwitchSCB(hhcb,hiob)
            
#define  SCSI_hSETDISCONNECTDELAY(hhcb)

#define  SCSI_hUPDATEEXEQ(hhcb,hiob) \
            SCSIhDchU320UpdateExeQ((hhcb),(hiob))

#if SCSI_TARGET_OPERATION 
#define  SCSI_hTARGETSETIGNOREWIDEMSG(hhcb,hiob) \
            SCSIhTargetDchU320SetIgnoreWideMsg((hhcb),(hiob)) 
#define  SCSI_hTARGETGETESTSCBFIELDS(hhcb,hiob,flag) \
            SCSIhTargetDchU320GetEstScbFields((hhcb),(hiob),(flag))
#define  SCSI_hTARGETSETFIRMWAREPROFILE(hhcb) \
            SCSIhTargetDchU320SetFirmwareProfile((hhcb))
#define  SCSI_hTARGETDELIVERESTSCB(hhcb,hiob) \
            SCSIhTargetStandardU320DeliverEstScb((hhcb),(hiob))
#define  SCSI_hTARGETRESETSOFTWARE(hhcb) \
               SCSIhTargetDchU320ResetSoftware((hhcb))
#define  SCSI_hTARGETSENDHIOBSPECIAL(hhcb,hiob) \
            SCSIhTargetStandardSendHiobSpecial((hhcb),(hiob))

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT            
#define  SCSI_hTARGETSELINTYPE(hhcb,hiob)   SCSI_LEGACY_SELECTION_IN   
#define  SCSI_hTARGETIUSELIN(hhcb,hiob)
#define  SCSI_hTARGETBUILDSTATUSIU(hhcb,hiob)
#define  SCSI_hTARGETASSIGNERRORSCBDESCRIPTOR(hhcb) SCSI_SUCCESS
#define  SCSI_hTARGETINITERRORHIOB(hhcb)
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */            

#else
#define  SCSI_hTARGETRESETSOFTWARE(hhcb)
#endif /* SCSI_TARGET_OPERATION */

#if (OSD_BUS_ADDRESS_SIZE == 64) 
#define  SCSI_hCOMPAREBUSSGLISTADDRESS(hhcb,busAddress1,busAddress2) \
            SCSIhCompareBusSGListAddress((hhcb),(busAddress1),(busAddress2))  

#define  SCSI_hPUTBUSADDRESSSG(hhcb,dmaStruct,offset,value) \
            if (((hhcb)->SCSI_HF_SgBusAddress32 == 1) && \
                ((hhcb)->firmwareMode == SCSI_FMODE_DCH_U320))\
            {\
               SCSI_PUT_LITTLE_ENDIAN64SG((hhcb),(dmaStruct),(offset),(value));\
            }\
            else\
            {\
               SCSI_PUT_LITTLE_ENDIAN64((hhcb),(dmaStruct),(offset),(value));\
            }
#define  SCSI_hGETBUSADDRESSSGPAD(hhcb,pValue,dmaStruct,offset) \
            if (((hhcb)->SCSI_HF_SgBusAddress32 == 1) && \
                ((hhcb)->firmwareMode == SCSI_FMODE_DCH_U320)) \
            {\
               SCSI_GET_LITTLE_ENDIAN64SGPAD((hhcb),(pValue),(dmaStruct),(offset));\
            }\
            else\
            {\
               SCSI_GET_LITTLE_ENDIAN64((hhcb),(pValue),(dmaStruct),(offset));\
            }
#else 
#define  SCSI_hCOMPAREBUSSGLISTADDRESS(hhcb,busAddress1,busAddress2) \
            SCSIhCompareBusAddress((busAddress1),(busAddress2))
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */

#define  SCSI_hSETINTRFACTORTHRESHOLD(hhcb,Factorthreshold) \
            SCSIhDchU320SetIntrFactorThreshold(hhcb,Factorthreshold)

#define  SCSI_hSETINTRTHRESHOLDCOUNT(hhcb,thresholdCount) \
            SCSIhDchU320SetIntrThresholdCount(hhcb,thresholdCount)

#endif /* SCSI_DCH_U320_MODE */
#endif
#endif /* SCSI_MULTI_MODE */

/***************************************************************************
* Macros independent on disable/enable firmware
***************************************************************************/
#define  SCSI_hSETDEFAULTAUTOTERM(hhcb)   \
            hhcb->SCSI_HF_primaryTermLow = hhcb->SCSI_HF_primaryTermHigh = 1; \
            hhcb->SCSI_HF_primaryAutoTerm = hhcb->SCSI_HF_autoTermDetected = 1; \
            hhcb->SCSI_HF_secondaryTermLow = 0; \
            hhcb->SCSI_HF_secondaryTermHigh = 0; \
            hhcb->SCSI_HF_secondaryAutoTerm = 0;

#if (OSD_BUS_ADDRESS_SIZE == 32)
#if SCSI_BIOS_MODE_SUPPORT
#define  SCSI_hSETADDRESSSCB(hhcb,scbAddress,physicalAddress) \
              SCSIhSetAddressScbBA32((hhcb),(scbAddress),(physicalAddress))
#if SCSI_EFI_BIOS_SUPPORT
#define  SCSI_hSETADDRESSSCRATCH(hhcb,scratchAddress,physicalAddress) \
              SCSIhSetAddressScratchBA32((hhcb),(scratchAddress),\
                                                (physicalAddress))
#endif /* SCSI_EFI_BIOS_SUPPORT */                  
#else
#define  SCSI_hSETADDRESSSCRATCH(hhcb,scratchAddress,virtualAddress) \
              SCSIhSetAddressScratchBA32((hhcb),(scratchAddress),\
                           (void SCSI_HPTR) (virtualAddress))
                                                   
#define  SCSI_hSETADDRESSSCB(hhcb,scbAddress,virtualAddress) \
              SCSIhSetAddressScbBA32((hhcb),(scbAddress),(virtualAddress))
#if SCSI_DCH_U320_MODE
#define  SCSI_hDchSETADDRESSSCB(hhcb,scbAddress,virtualAddress) \
              SCSIhSetAddressScbBA64((hhcb),(scbAddress),(virtualAddress))
#endif /* SCSI_DCH_U320_MODE */
#endif /* SCSI_BIOS_MODE_SUPPORT */

#if !SCSI_BIOS_SUPPORT
#define  SCSI_hGETADDRESSSCRATCH(hhcb,scratchAddress) \
              SCSIhGetAddressScratchBA32((hhcb),(scratchAddress))
#endif /* !SCSI_BIOS_SUPPORT */
#define  SCSI_hGETADDRESSSCB(hhcb,scbAddress) \
              SCSIhGetAddressScbBA32((hhcb),(scbAddress))
#define  SCSI_hPUTBUSADDRESS(hhcb,dmaStruct,offset,value) \
              SCSI_PUT_LITTLE_ENDIAN32((hhcb),(dmaStruct),(offset),(value))
#define  SCSI_hGETBUSADDRESS(hhcb,pValue,dmaStruct,offset) \
              SCSI_GET_LITTLE_ENDIAN32((hhcb),(pValue),(dmaStruct),(offset))
#else /* !(OSD_BUS_ADDRESS_SIZE == 32) */

#if SCSI_BIOS_MODE_SUPPORT
#define  SCSI_hSETADDRESSSCB(hhcb,scbAddress,physicalAddress) \
              SCSIhSetAddressScbBA64((hhcb),(scbAddress),(physicalAddress))
#if SCSI_EFI_BIOS_SUPPORT
#define  SCSI_hSETADDRESSSCRATCH(hhcb,scratchAddress,physicalAddress) \
              SCSIhSetAddressScratchBA64((hhcb),(scratchAddress),\
                                         (physicalAddress))
#endif /* SCSI_EFI_BIOS_SUPPORT */
#else
#define  SCSI_hSETADDRESSSCRATCH(hhcb,scratchAddress,virtualAddress) \
              SCSIhSetAddressScratchBA64((hhcb),(scratchAddress),\
                           (void SCSI_HPTR) (virtualAddress))

#define  SCSI_hSETADDRESSSCB(hhcb,scbAddress,virtualAddress) \
              SCSIhSetAddressScbBA64((hhcb),(scbAddress),(virtualAddress))
#endif /* SCSI_BIOS_MODE_SUPPORT */
#if !SCSI_BIOS_SUPPORT
#define  SCSI_hGETADDRESSSCRATCH(hhcb,scratchAddress) \
              SCSIhGetAddressScratchBA64((hhcb),(scratchAddress))
#endif /* !SCSI_BIOS_SUPPORT */
#define  SCSI_hGETADDRESSSCB(hhcb,scbAddress) \
              SCSIhGetAddressScbBA64((hhcb),(scbAddress))

#define  SCSI_hPUTBUSADDRESS(hhcb,dmaStruct,offset,value) \
              SCSI_PUT_LITTLE_ENDIAN64((hhcb),(dmaStruct),(offset),(value))
#define  SCSI_hGETBUSADDRESS(hhcb,pValue,dmaStruct,offset) \
              SCSI_GET_LITTLE_ENDIAN64((hhcb),(pValue),(dmaStruct),(offset))
#endif /* (OSD_BUS_ADDRESS_SIZE == 32) */


/***************************************************************************
* Macros dependent on IDs
***************************************************************************/
#define  SCSI_hWRITE_HCNTRL(hhcb,value) SCSIhWriteHCNTRL((hhcb),(SCSI_UEXACT8)(value))

/* Include SCSI_HWERRINT, SCSI_SPLTINT,a SCSI_PCIINT as if
 * FAILDIS = 0 then we can also receive these interrupts.
 */
#if SCSI_DCH_U320_MODE
#define  SCSI_HSTINTSTAT_MASK  (SCSI_BRKADRINT | SCSI_SCSIINT | SCSI_ARPINT |\
                                SCSI_HDMAINT | SCSI_HWERRINT)
#else                                
#define  SCSI_HSTINTSTAT_MASK  (SCSI_BRKADRINT | SCSI_SCSIINT | SCSI_ARPINT |\
                                SCSI_PCIINT | SCSI_SPLTINT | SCSI_HWERRINT)
#endif /* SCSI_DCH_U320_MODE */
                                

#if SCSI_MULTI_HARDWARE
#define  SCSI_hSETUPHARDWAREDESC(hhcb) (hhcb)->SCSI_HP_hardwareDescriptor = \
                                    SCSIHardware[(hhcb)->hardwareMode]

#define  SCSI_hPROCESSAUTOTERM(hhcb) \
            if ((hhcb)->SCSI_HP_hardwareDescriptor->SCSIhProcessAutoTerm)\
            {\
               (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhProcessAutoTerm((hhcb));\
            }
#define  SCSI_hUPDATEEXTTERM(hhcb) \
            if ((hhcb)->SCSI_HP_hardwareDescriptor->SCSIhUpdateExtTerm)\
            {\
               (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhUpdateExtTerm((hhcb));\
            }
#define SCSI_hGETCURRENTSENSING(hhcb, channel_select) \
            if ((hhcb)->SCSI_HP_hardwareDescriptor->SCSIhGetCurrentSensing)\
            {\
               (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhGetCurrentSensing((hhcb),(channel_select));\
            }
#define  SCSI_hSETDATAFIFOTHRSHDEFAULT(hhcb) \
            (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhSetDataFifoThrshDefault((hhcb))
#define  SCSI_hGETDATAFIFOTHRSH(hhcb) \
            (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhGetDataFifoThrsh((hhcb))
#define  SCSI_hUPDATEDATAFIFOTHRSH(hhcb) \
            (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhUpdateDataFifoThrsh((hhcb))
#define  SCSI_hGETCACHETHEN(hhcb) \
            (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhGetCacheThEn((hhcb))
#define  SCSI_hUPDATECACHETHEN(hhcb) \
            (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhUpdateCacheThEn((hhcb))
#define  SCSI_hGETPROFILEPARAMETERS(hwInfo,hhcb) \
            (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhGetProfileParameters((hwInfo), (hhcb))
#define  SCSI_hPUTPROFILEPARAMETERS(hwInfo,hhcb) \
            (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhPutProfileParameters((hwInfo), (hhcb))

#if (!SCSI_DCH_U320_MODE && ((SCSI_CURRENT_SENSING) || (SCSI_UPDATE_TERMINATION_ENABLE) || (SCSI_AUTO_TERMINATION))
#define  SCSI_hCHECKFLEXPORTACCESS(hhcb) \
            (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhCheckFlexPortAccess((hhcb))
#else
#define  SCSI_hCHECKFLEXPORTACCESS(hhcb)
#endif /* (!SCSI_DCH_U320_MODE && ((SCSI_CURRENT_SENSING) || (SCSI_UPDATE_TERMINATION_ENABLE) || (SCSI_AUTO_TERMINATION))*/

#define  SCSI_hWAITFORBUSSTABLE(hhcb)\
               (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhWaitForBusStable((hhcb))
#define  SCSI_hLOADTARGETIDS(hhcb) \
            if ((hhcb)->SCSI_HP_hardwareDescriptor->SCSIhTargetLoadTargetIds)\
            {\
               (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhTargetLoadTargetIds((hhcb));\
            }            
#define  SCSI_hRESETNEGODATA(hhcb) \
            (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhResetNegoData((hhcb))

#if SCSI_LOOPBACK_OPERATION
/* Prevent loading of rate registers when loopback enabled. */
#define  SCSI_hLOADNEGODATA(hhcb,targetID,xferRate)
#else
#define  SCSI_hLOADNEGODATA(hhcb,targetID,xferRate) \
             (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhLoadNegoData((hhcb),(targetID),(xferRate))
#endif /* SCSI_LOOPBACK_OPERATION */

#define  SCSI_hGETNEGODATA(hhcb,targetID,xferRate) \
             (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhGetNegoData((hhcb),(targetID),(xferRate))
#define  SCSI_hCHANGEXFEROPTION(hiob) \
            if (SCSI_TARGET_UNIT((hiob))->hhcb->SCSI_HP_hardwareDescriptor->SCSIhChangeXferOption)\
            {\
               SCSI_TARGET_UNIT((hiob))->hhcb->SCSI_HP_hardwareDescriptor->SCSIhChangeXferOption((hiob));\
            }            
#define  SCSI_hGETTIMERCOUNT(hhcb,usecs) \
            (hhcb)->SCSI_HP_hardwareDescriptor->SCSIhGetTimerCount((hhcb),(usecs));

#else /* !SCSI_MULTI_HARDWARE */


#if SCSI_AICU320
#define  SCSI_hSETUPHARDWAREDESC(hhcb)
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
#define  SCSI_hHANDLEHWERRINTERRUPT(hhcb) SCSIhHandleHwErrInterrupt(hhcb)
#else
#define  SCSI_hHANDLEHWERRINTERRUPT(hhcb) 
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */
#if SCSI_BIOS_SUPPORT
#define  SCSI_hCDBABORT(hhcb,hiob) SCSIhBadSeq(hhcb,0)
#define  SCSI_hBREAKINTERRUPT(hhcb,hiob)
#define  SCSI_hSETBUSHUNG(hhcb,value)
#define  SCSI_hSETCURRENTMODE(hhcb,value)
#else
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
#define  SCSI_hBREAKINTERRUPT(hhcb,hiob) SCSIhBreakInterrupt((hhcb),(hiob))
#define  SCSI_hCDBABORT(hhcb,hiob) SCSIhCdbAbort((hhcb),(hiob))
#else
#define  SCSI_hCDBABORT(hhcb,hiob) SCSIhBadSeq(hhcb,0)
#define  SCSI_hBREAKINTERRUPT(hhcb,hiob) 
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */

#define  SCSI_hSETBUSHUNG(hhcb,value)  (hhcb)->SCSI_HF_busHung = (value)
#define  SCSI_hSETCURRENTMODE(hhcb,value)  (hhcb)->SCSI_HP_currentMode = (value)  
#endif /* SCSI_BIOS_SUPPORT */

#if (SCSI_BIOS_MODE_SUPPORT || SCSI_SEQ_LOAD_USING_PIO)
#define  SCSI_hLOADSEQUENCER(hhcb,seqCode,seqSize) \
            SCSIhPIOLoadSequencer((hhcb),(SCSI_UEXACT8 SCSI_LPTR)(seqCode),(SCSI_UEXACT32)(seqSize))
#else
#define  SCSI_hLOADSEQUENCER(hhcb,seqCode,seqSize) \
            SCSIhDMALoadSequencer((hhcb),(SCSI_UEXACT8 SCSI_LPTR)(seqCode),(SCSI_UEXACT32)(seqSize))
#endif /* (SCSI_BIOS_MODE_SUPPORT || SCSI_SEQ_LOAD_USING_PIO) */
#if SCSI_AUTO_TERMINATION
#define  SCSI_hPROCESSAUTOTERM(hhcb) SCSIhProcessAutoTerm((hhcb))
#else
#define  SCSI_hPROCESSAUTOTERM(hhcb)
#endif /* SCSI_AUTO_TERMINATION */

#if SCSI_UPDATE_TERMINATION_ENABLE
#define  SCSI_hUPDATEEXTTERM(hhcb) SCSIhUpdateExtTerm((hhcb))
#else
#define  SCSI_hUPDATEEXTTERM(hhcb)
#endif /* SCSI_UPDATE_TERMINATION_ENABLE */

#if SCSI_CURRENT_SENSING
#define  SCSI_hGETCURRENTSENSING(hhcb,channel_select) SCSIhGetCurrentSensing((hhcb),(channel_select))
#else
#define  SCSI_hGETCURRENTSENSING(hhcb,channel_select)
#endif /* SCSI_CURRENT_SENSING */

#define  SCSI_hSETDATAFIFOTHRSHDEFAULT(hhcb) SCSIhSetDataFifoThrshDefault((hhcb))  
#define  SCSI_hGETDATAFIFOTHRSH(hhcb) SCSIhGetDataFifoThrsh((hhcb))
#define  SCSI_hUPDATEDATAFIFOTHRSH(hhcb) SCSIhUpdateDataFifoThrsh((hhcb))
#define  SCSI_hGETCACHETHEN(hhcb) SCSIhGetCacheThEn((hhcb))
#define  SCSI_hUPDATECACHETHEN(hhcb) SCSIhUpdateCacheThEn((hhcb))
#define  SCSI_hGETPROFILEPARAMETERS(hwInfo,hhcb) SCSIhGetProfileParameters((hwInfo),(hhcb))
#define  SCSI_hPUTPROFILEPARAMETERS(hwInfo,hhcb) SCSIhPutProfileParameters((hwInfo),(hhcb))
#if (!SCSI_DCH_U320_MODE && (SCSI_CURRENT_SENSING || SCSI_UPDATE_TERMINATION_ENABLE || SCSI_AUTO_TERMINATION))
#define  SCSI_hCHECKFLEXPORTACCESS(hhcb) SCSIhCheckFlexPortAccess((hhcb))
#else
#define  SCSI_hCHECKFLEXPORTACCESS(hhcb)
#endif /* (!SCSI_DCH_U320_MODE && (SCSI_CURRENT_SENSING || SCSI_UPDATE_TERMINATION_ENABLE || SCSI_AUTO_TERMINATION))*/
#define  SCSI_hWAITFORBUSSTABLE(hhcb) SCSIhWaitForBusStable((hhcb))

#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
#define  SCSI_hLOADTARGETIDS(hhcb) SCSIhTargetLoadTargetIDs((hhcb))
#else
#define  SCSI_hLOADTARGETIDS(hhcb)
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */

#define  SCSI_hRESETNEGODATA(hhcb) SCSIhResetNegoData((hhcb))

#if SCSI_LOOPBACK_OPERATION
/* Prevent loading of rate registers when loopback enabled. */
#define  SCSI_hLOADNEGODATA(hhcb,targetID,xferRate)
#else
#define  SCSI_hLOADNEGODATA(hhcb,targetID,xferRate) SCSIhLoadNegoData((hhcb),(targetID),(xferRate))
#endif /* SCSI_LOOPBACK_OPERATION */

#define  SCSI_hGETNEGODATA(hhcb,targetID,xferRate) SCSIhGetNegoData((hhcb),(targetID),(xferRate))

#if SCSI_NEGOTIATION_PER_IOB
#define  SCSI_hCHANGEXFEROPTION(hiob) SCSIhChangeXferOption((hiob))
#else
#define  SCSI_hCHANGEXFEROPTION(hiob)
#endif /* SCSI_NEGOTIATION_PER_IOB */

#define  SCSI_hGETTIMERCOUNT(hhcb,usecs) SCSIhGetTimerCount((hhcb),(usecs))

#endif /* SCSI_AICU320 */

#endif /* SCSI_MULTI_HARDWARE */

#if SCSI_RESOURCE_MANAGEMENT
#define  SCSI_hFRESH_HIOB(hiob)
#if SCSI_TARGET_OPERATION
#define  SCSI_hFRESH_NEXUS(nexus) (nexus)->nexusReserve.nexusFlags.u8 = 0;
#define  SCSI_hTARGETMODE_HIOB(hiob)
#endif /* SCSI_TARGET_OPERATION */
#else
#if SCSI_TARGET_OPERATION
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_hFRESH_NEXUS(nexus) (nexus)->SCSI_XF_lunTar = 0;\
                                  (nexus)->SCSI_XF_disconnectAllowed = 0;\
                                  (nexus)->SCSI_XF_busHeld = 0;\
                                  (nexus)->SCSI_XF_tagRequest = 0;\
                                  (nexus)->SCSI_XF_lastResource = 0;\
                                  (nexus)->SCSI_XF_scsi1Selection = 0;\
                                  (nexus)->SCSI_XF_packetized = 0;\
                                  (nexus)->nexusReserve.nexusFlags.u8 = 0;
#else
#define  SCSI_hFRESH_NEXUS(nexus) (nexus)->SCSI_XF_lunTar = 0;\
                                  (nexus)->SCSI_XF_disconnectAllowed = 0;\
                                  (nexus)->SCSI_XF_busHeld = 0;\
                                  (nexus)->SCSI_XF_tagRequest = 0;\
                                  (nexus)->SCSI_XF_lastResource = 0;\
                                  (nexus)->SCSI_XF_scsi1Selection = 0;\
                                  (nexus)->nexusReserve.nexusFlags.u8 = 0;
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#define  SCSI_hTARGETMODE_HIOB(hiob)   (hiob)->SCSI_IP_targetMode = 1;

#define  SCSI_hFRESH_HIOB(hiob)  (hiob)->stat = 0;\
                                 (hiob)->haStat = 0;\
                                 (hiob)->trgStatus = 0;\
                                 (hiob)->SCSI_IP_targetMode = 0; \
                                 (hiob)->SCSI_IP_mgrStat = 0; \
                                 (hiob)->SCSI_IP_negoState = 0; 
#else /* !SCSI_TARGET_OPERATION */
#if SCSI_PACKETIZED_IO_SUPPORT
#define  SCSI_hFRESH_HIOB(hiob)  (hiob)->stat = 0;\
                                 (hiob)->haStat = 0;\
                                 (hiob)->trgStatus = 0;\
                                 (hiob)->tmfStatus = 0;\
                                 (hiob)->SCSI_IP_mgrStat = 0;\
                                 (hiob)->SCSI_IP_negoState = 0;
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
#define  SCSI_hFRESH_HIOB(hiob)  (hiob)->stat = 0;\
                                 (hiob)->haStat = 0;\
                                 (hiob)->trgStatus = 0;\
                                 (hiob)->SCSI_IP_mgrStat = 0;\
                                 (hiob)->SCSI_IP_negoState = 0;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */                                 

#endif /* SCSI_RESOURCE_MANAGEMENT */

/***************************************************************************
* Macros dependent on SCSI_TARGET_OPERATION
***************************************************************************/
#if SCSI_TARGET_OPERATION
#if SCSI_MULTIPLEID_SUPPORT
#define  SCSI_hTARGETMODEENABLE(hhcb) \
           if (((hhcb)->SCSI_HF_targetMode) &&\
               ((hhcb)->SCSI_HF_targetAdapterIDMask != 0))\
           {\
              SCSIhTargetModeEnable((hhcb));\
           } 
#else
#define  SCSI_hTARGETMODEENABLE(hhcb) \
            if ((hhcb)->SCSI_HF_targetMode)\
            {\
               SCSIhTargetModeEnable((hhcb));\
            } 
#endif /* SCSI_MULTIPLEID_SUPPORT */

#define  SCSI_hTARGETMODEHWINITIALIZE(hhcb) \
            if ((hhcb)->SCSI_HF_targetMode)\
            {\
               SCSIhTargetModeHWInitialize((hhcb));\
            }
#define  SCSI_hTARGETMODESWINITIALIZE(hhcb) \
            if ((hhcb)->SCSI_HF_targetMode)\
            {\
               SCSIhTargetModeSWInitialize((hhcb));\
            }

/* RSM allowed to issue Establish Connection HIOBs */
#define  SCSI_hTARGETHOLDESTHIOBS(hhcb,value) \
            if ((hhcb)->SCSI_HF_targetMode)\
            {\
               (hhcb)->SCSI_HF_targetHoldEstHiobs = (value);\
            }
#define  SCSI_hGETTARGETMODEPROFILEFIELDS(hwinfo,hhcb) \
            if ((hhcb)->SCSI_HF_targetMode)\
            {\
               SCSIhGetTargetModeProfileFields((hwInfo),(hhcb));\
            }

#define  SCSI_hSETTARGETMODEPROFILEFIELDS(hwinfo,hhcb) \
            if ((hhcb)->SCSI_HF_targetMode)\
            {\
               SCSIhSetTargetModeProfileFields((hwInfo),(hhcb));\
            }

#define  SCSI_hNEGOTIATE(hhcb,hiob) \
            if ((hiob) != SCSI_NULL_HIOB && (hiob)->SCSI_IP_targetMode)\
            {\
               SCSIhTargetNegotiate((hhcb),(hiob),1);\
            }\
            else\
            {\
               SCSIhNegotiate((hhcb),(hiob));\
            }
#define  SCSI_hTERMINATECOMMAND(hiob) \
            if ((hiob)->SCSI_IP_targetMode)\
            {\
               SCSIhTerminateTargetCommand((hiob));\
            }\
            else\
            {\
               SCSIhTerminateCommand((hiob));\
            }
#define  SCSI_hINTSELTO(hhcb,hiob) \
            if ((hhcb)->SCSI_HF_targetMode && ((hiob) != SCSI_NULL_HIOB))\
            {\
               if ((hiob)->SCSI_IP_targetMode)\
               {\
                  SCSIhTargetIntSelto((hhcb),(hiob));\
               }\
               else\
               {\
                  SCSIhIntSelto((hhcb),(hiob));\
               }\
            }\
            else\
            {\
               SCSIhIntSelto((hhcb),(hiob));\
            }

#define  SCSI_hSETNEGOTIATEDSTATE(hhcb,i,value) \
            if (hhcb->SCSI_HF_targetMode) \
            {\
               SCSIhTargetSetNegotiatedState((hhcb),(i),(value));\
            }
#define  SCSI_hGETDEVICETABLEPTR(hiob,deviceTable) \
            if ((hiob)->SCSI_IP_targetMode)\
            {\
               deviceTable = SCSI_GET_NODE(SCSI_NEXUS_UNIT(hiob))->deviceTable;\
            }\
            else\
            {\
               deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;\
            }

               
#if SCSI_MULTIPLEID_SUPPORT
#define  SCSI_hTARGETDISABLEID(hhcb) \
            if ((hhcb)->SCSI_HF_targetMode)\
            {\
               SCSIhTargetDisableId((hhcb));\
            }
#else
#define  SCSI_hTARGETDISABLEID(hhcb)

#endif /* SCSI_MULTIPLEID_SUPPORT */
#else
#define  SCSI_hTARGETMODEHWINITIALIZE(hhcb)
#define  SCSI_hTARGETMODESWINITIALIZE(hhcb)
#define  SCSI_hTARGETMODEENABLE(hhcb)
#define  SCSI_hTARGETHOLDESTHIOBS(hhcb,value)
#define  SCSI_hTARGETDISABLEID(hhcb)
#define  SCSI_hGETTARGETMODEPROFILEFIELDS(hwinfo,hhcb)
#define  SCSI_hSETTARGETMODEPROFILEFIELDS(hwinfo,hhcb)
#define  SCSI_hNEGOTIATE(hhcb,hiob)     SCSIhNegotiate((hhcb),(hiob))
#define  SCSI_hTERMINATECOMMAND(hiob)   SCSIhTerminateCommand((hiob))
#define  SCSI_hINTSELTO(hhcb,hiob)      SCSIhIntSelto((hhcb),(hiob))
#define  SCSI_hSETNEGOTIATEDSTATE(hhcb,i,value)
#define  SCSI_hGETDEVICETABLEPTR(hiob,deviceTable)\
            deviceTable = SCSI_TARGET_UNIT((hiob))->deviceTable
#endif /* SCSI_TARGET_OPERATION */

/***************************************************************************
* Function prototypes for Common HIM internal reference
***************************************************************************/
/* HWMTASK.C */
#if !SCSI_ASPI_SUPPORT_GROUP1
void SCSIhAbortTask(SCSI_HIOB SCSI_IPTR);
void SCSIhAbortTaskSet(SCSI_HIOB SCSI_IPTR);
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING      
void SCSIhPCIReset(SCSI_HHCB SCSI_HPTR);
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */


/* HWMDLVR.C */
void SCSIhStandardDeliverScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftDeliverScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320SetupAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardEnhU320SetupAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDchU320SetupAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320SetupAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);

#if SCSI_DOWNSHIFT_MODE
void SCSIhDownShiftU320SetCommandRequestor(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_DOWNSHIFT_MODE */
void SCSIhStandardQHead(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDchU320QHead(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320QHead(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT16 SCSIhRetrieveScb(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhQoutCount(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardQHeadTR(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDchU320QHeadTR(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhStandardU320QExeTargNextOffset(void);
SCSI_UEXACT8 SCSIhStandardEnhU320QExeTargNextOffset(void);
SCSI_UEXACT8 SCSIhStandardU320QNextOffset(void);
SCSI_UEXACT8 SCSIhStandardEnhU320QNextOffset(void);

               
/* HWMDIAG.C */


/* HWMUTIL.C */
#if ((SCSI_STANDARD_MODE && (OSD_BUS_ADDRESS_SIZE == 64)) || SCSI_DCH_U320_MODE)
SCSI_UINT8 SCSIhCompareBusSGListAddress(SCSI_HHCB SCSI_HPTR, SCSI_BUS_ADDRESS,SCSI_BUS_ADDRESS); 
#endif /* (SCSI_STANDARD_MODE && (OSD_BUS_ADDRESS_SIZE == 64)) */
void SCSIhPauseAndWait(SCSI_HHCB SCSI_HPTR);
#if (SCSI_STANDARD_MODE || SCSI_SAVE_RESTORE_STATE)
SCSI_UEXACT16 SCSIhGetArpNewSCBQOffset(SCSI_HHCB SCSI_HPTR);
#endif /* (SCSI_STANDARD_MODE || SCSI_SAVE_RESTORE_STATE) */
#if !SCSI_BIOS_SUPPORT
SCSI_UEXACT8 SCSIhGetModeRegister(SCSI_HHCB SCSI_HPTR);
#endif /* !SCSI_BIOS_SUPPORT */
void SCSIhUnPause(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhSetModeRegister(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhDisableDataChannel(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhDisableBothDataChannels(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhWait4Req(SCSI_HHCB SCSI_HPTR);
void SCSIhDelayCount25us(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT32);
#if SCSI_SCBBFR_BUILTIN
void SCSIhSetupAssignScbBuffer(SCSI_HHCB SCSI_HPTR);
void SCSIhAssignScbDescriptor(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhFreeScbDescriptor(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_SCBBFR_BUILTIN */
#if (!SCSI_BIOS_SUPPORT && SCSI_SCSISELECT_SUPPORT)
SCSI_UINT8 SCSIhCheckSigSCBFF(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8 SCSI_SPTR);
#endif /* (!SCSI_BIOS_SUPPORT && SCSI_SCSISELECT_SUPPORT) */

SCSI_UEXACT8 SCSIhWriteHCNTRL(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhReadHSTINTSTAT(SCSI_HHCB SCSI_HPTR);
#if SCSI_BIOS_MODE_SUPPORT
void SCSIhSetAddressScbBA32(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16,SCSI_BUS_ADDRESS);
void SCSIhSetAddressScbBA64(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16,SCSI_BUS_ADDRESS);
#else
SCSI_UEXACT8 SCSIhDetermineValidNVData(SCSI_NVM_LAYOUT SCSI_LPTR);
void SCSIhSetAddressScbBA32(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16,void SCSI_HPTR);
void SCSIhSetAddressScbBA64(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16,void SCSI_HPTR);
#endif /* SCSI_BIOS_MODE_SUPPORT */

SCSI_UEXACT32 SCSIhGetEntity32FromScb(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16); 
#if SCSI_ASPI_SUPPORT
void SCSIhGetAddressScbBA64(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16);
void SCSIhGetAddressScbBA32(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16);
#else
SCSI_BUS_ADDRESS SCSIhGetAddressScbBA32(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16);
SCSI_BUS_ADDRESS SCSIhGetAddressScbBA64(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16);
#endif /* SCSI_ASPI_SUPPORT */
SCSI_UINT8 SCSIhCompareBusAddress(SCSI_BUS_ADDRESS,SCSI_BUS_ADDRESS); 
void SCSIhEnableIOErr(SCSI_HHCB SCSI_HPTR);
void SCSIhResetNegoData(SCSI_HHCB SCSI_HPTR);
void SCSIhLoadNegoData(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);

SCSI_UEXACT8 SCSIhClearDataFifo(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhResetDataChannel(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);

void SCSIhSetSeltoDelay(SCSI_HHCB SCSI_HPTR);
void SCSIhResetCCControl(SCSI_HHCB SCSI_HPTR);
void SCSIhInitCCHostAddress(SCSI_HHCB SCSI_HPTR);

SCSI_UEXACT16 SCSIhGetSpeed(SCSI_UEXACT8 SCSI_LPTR,SCSI_HHCB SCSI_HPTR);
void SCSIhCalcScsiOption(SCSI_UEXACT16,SCSI_UEXACT8,SCSI_UEXACT8,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhGetProfileParameters(SCSI_HW_INFORMATION SCSI_LPTR,SCSI_HHCB SCSI_HPTR);
void SCSIhPutProfileParameters(SCSI_HW_INFORMATION SCSI_LPTR,SCSI_HHCB SCSI_HPTR);

SCSI_UEXACT32 SCSIhGetTimerCount(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT32);
void SCSIhSetModeToCurrentFifo(SCSI_HHCB SCSI_HPTR);
SCSI_HIOB SCSI_IPTR SCSIhGetHIOBFromCurrentFifo(SCSI_HHCB SCSI_HPTR);
#if !SCSI_ASPI_SUPPORT_GROUP1
void SCSIhStandardSetBreakPoint(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhStandardClearBreakPoint(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
void SCSIhStandardU320SetIntrFactorThreshold(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhDchU320SetIntrFactorThreshold(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhStandardU320SetIntrThresholdCount(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhDchU320SetIntrThresholdCount(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhStandardU320SetDisconnectDelay(SCSI_HHCB SCSI_HPTR);
void SCSIhDownshiftSetElecInformation(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhStandardSetElecInformation(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);

#if SCSI_NEGOTIATION_PER_IOB
void SCSIhChangeXferOption(SCSI_HIOB SCSI_IPTR);
void SCSIhChangeNegotiation(SCSI_UNIT_CONTROL SCSI_UPTR);
#endif /* SCSI_NEGOTIATION_PER_IOB */


/* HWMPTCL.C */
void SCSIhNegotiate(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhNegotiateWideOrSync(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhExtMsgi(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhInitiatePPR(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhVerifyPPRResponse(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhInitiateWide(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhVerifyWideResponse(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhInitiateSync(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhVerifySyncResponse(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhRespondToWide(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhRespondToSync(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhSendMessage(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,int);
SCSI_UEXACT8 SCSIhReceiveMessage(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhHandleMsgOut(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhHandleMsgIn(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhAbortConnection(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
#if SCSI_PACKETIZED_IO_SUPPORT
SCSI_UEXACT8 SCSIhPackAndNonPackSwitched(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
SCSI_UEXACT8 SCSIhPackSwitchedMsgReject(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftU320QHeadPNPSwitchSCB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftEnhU320QHeadPNPSwitchSCB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#else
void SCSIhStandardU320QHeadPNPSwitchSCB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardEnhU320QHeadPNPSwitchSCB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDchU320QHeadPNPSwitchSCB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_DOWNSHIFT_MODE */
void SCSIhGetNegoXferRate(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhCheckSyncNego(SCSI_HHCB SCSI_HPTR);
void SCSIhUpdateNeedNego(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhAsyncNarrowXferRateAssign(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhXferRateAssign(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
SCSI_UEXACT8 SCSIhStandardU320UpdateExeQAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhStandardEnhU320UpdateExeQAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhDchU320UpdateExeQAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhDownshiftU320UpdateExeQAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);

void SCSIhStandardUpdateNewQAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhDownshiftUpdateNewQAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhLastNegoEntryAssign(SCSI_DEVICE SCSI_DPTR);
void SCSIhModifyDataPtr(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);

SCSI_UEXACT8 SCSIhEvenIOLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhDchEvenIOLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320IgnoreWideResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhStandardEnhU320IgnoreWideResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhDchU320IgnoreWideResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhDownshiftU320IgnoreWideResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);

#if SCSI_LOOPBACK_OPERATION
void SCSIhSetXferRate(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
#endif /* SCSI_LOOPBACK_OPERATION */

#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT && SCSI_STANDARD_ENHANCED_U320_MODE) 
SCSI_UEXACT16 SCSIhMultipleHostIdGetOption(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8);
void SCSIhMultipleHostIdXferOptAssign(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8,SCSI_UEXACT16); 
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT && SCSI_STANDARD_ENHANCED_U320_MODE) */ 


/* HWMHRST.C */
void SCSIhResetChannelHardware(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardAbortChannel(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhDownshiftAbortChannel(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhStandardU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhDownshiftU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhDchU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhCommonResetSoftware(SCSI_HHCB SCSI_HPTR);
#if SCSI_EFI_BIOS_SUPPORT
void SCSIhSetAddressScratchBA32(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16, SCSI_BUS_ADDRESS);
void SCSIhSetAddressScratchBA64(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16, SCSI_BUS_ADDRESS);
#else
void SCSIhSetAddressScratchBA32(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16,void SCSI_HPTR);
void SCSIhSetAddressScratchBA64(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16,void SCSI_HPTR);
#endif /* SCSI_EFI_BIOS_SUPPORT */
#if !SCSI_BIOS_SUPPORT
#if SCSI_ASPI_SUPPORT
void SCSIhGetAddressScratchBA64(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16);
void SCSIhGetAddressScratchBA32(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16);
#else
SCSI_BUS_ADDRESS SCSIhGetAddressScratchBA32(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16);
SCSI_BUS_ADDRESS SCSIhGetAddressScratchBA64(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16);
#endif /* SCSI_ASPI_SUPPORT */
#endif /* !SCSI_BIOS_SUPPORT */
void SCSIhStandardU320ResetBTA(SCSI_HHCB SCSI_HPTR);
void SCSIhDownshiftU320ResetBTA(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardU320TargetClearBusy(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320TargetClearBusy(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320IndexClearBusy(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhResetScsi(SCSI_HHCB SCSI_HPTR);




#if !SCSI_ASPI_BIOS_SUPPORT_GROUP1
void SCSIhStandardU320SetupTR(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardEnhU320SetupTR(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#if !SCSI_BEF_SUPPORT
void SCSIhScsiBusReset(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetReset(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* !SCSI_BEF_SUPPORT */
void SCSIhStandardAbortHIOB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhStandardSearchSeqDoneQ(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhStandardSearchDoneQ(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhStandardSearchNewQ(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhStandardU320UpdateAbortBitHostMem(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardEnhU320UpdateAbortBitHostMem(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhStandardSearchExeQ(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8,SCSI_UEXACT8);
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
void SCSIhStandardU320UpdateNextScbAddress(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_BUS_ADDRESS);
void SCSIhStandardEnhU320UpdateNextScbAddress(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_BUS_ADDRESS);
void SCSIhDchU320UpdateNextScbAddress(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_BUS_ADDRESS);
SCSI_UEXACT8 SCSIhDchU320SearchSeqDoneQ(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhDchU320SearchExeQ(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8,SCSI_UEXACT8);
void SCSIhDchU320UpdateAbortBitHostMem(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDchU320SetupTR(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardFreezeHWQueue(SCSI_HHCB SCSI_HPTR, SCSI_HIOB SCSI_IPTR, SCSI_UINT16);
void SCSIhDchU320FreezeHWQueue(SCSI_HHCB SCSI_HPTR, SCSI_HIOB SCSI_IPTR, SCSI_UINT16);
#if SCSI_PACKETIZED_IO_SUPPORT
void SCSIhStandardPackNonPackQueueHIOB(SCSI_HHCB SCSI_HPTR, SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */


/* HWMINIT.C */

void SCSIhSetupEnvironment(SCSI_HHCB SCSI_HPTR);
void SCSIhPrepareConfig(SCSI_HHCB SCSI_HPTR);
void SCSIhGetCapability(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardGetConfig(SCSI_HHCB SCSI_HPTR);
void SCSIhDownshiftGetConfig(SCSI_HHCB SCSI_HPTR);
void SCSIhNonStandardGetConfig(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT16 SCSIhGetMaxHardwareScbs(SCSI_HHCB SCSI_HPTR);
void SCSIhGetCommonConfig(SCSI_HHCB SCSI_HPTR);
void SCSIhGetHardwareConfiguration(SCSI_HHCB SCSI_HPTR);
void SCSIhGetIDBasedConfiguration(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhStandardU320SetupSequencer(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhStandardEnhU320SetupSequencer(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhDownshiftU320SetupSequencer(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhDchU320SetupSequencer(SCSI_HHCB SCSI_HPTR hhcb);

#if (SCSI_BIOS_MODE_SUPPORT || SCSI_SEQ_LOAD_USING_PIO)
SCSI_UEXACT8 SCSIhPIOLoadSequencer(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8 SCSI_LPTR,SCSI_UEXACT32);
#else
SCSI_UEXACT8 SCSIhDMALoadSequencer(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8 SCSI_LPTR,SCSI_UEXACT32);
#endif /* (SCSI_BIOS_MODE_SUPPORT || SCSI_SEQ_LOAD_USING_PIO) */
void SCSIhInitializeHardware(SCSI_HHCB SCSI_HPTR);
void SCSIhStartToRunSequencer(SCSI_HHCB SCSI_HPTR);
void SCSIhProcessAutoTerm(SCSI_HHCB SCSI_HPTR);
void SCSIhUpdateExtTerm(SCSI_HHCB SCSI_HPTR);
/* THE ROUTINES LISTED BELOW ARE INITIALIZATION ROUTINES ONLY FOR THE
   SCSI_BIOS_SUPPORT MODE, FOR OTHER OEMS, THEY ARE RUNTIME ROUTINES */
SCSI_UEXACT8 SCSIhCableSense(SCSI_HHCB SCSI_HPTR);
void SCSIhGetNegoData(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
SCSI_UEXACT8 SCSIhGetCacheThEn(SCSI_HHCB SCSI_HPTR);
void SCSIhUpdateCacheThEn(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhGetSeltoDelay (SCSI_HHCB SCSI_HPTR);
#if (!SCSI_DCH_U320_MODE && (SCSI_CURRENT_SENSING || SCSI_UPDATE_TERMINATION_ENABLE || SCSI_AUTO_TERMINATION))
void SCSIhCheckFlexPortAccess(SCSI_HHCB SCSI_HPTR);
#endif /* (!SCSI_DCH_U320_MODE && (SCSI_CURRENT_SENSING || SCSI_UPDATE_TERMINATION_ENABLE || SCSI_AUTO_TERMINATION))*/
SCSI_UEXACT8 SCSIhWaitForBusStable(SCSI_HHCB SCSI_HPTR);
#if SCSI_CURRENT_SENSING
SCSI_UEXACT8 SCSIhGetCurrentSensing(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
#endif /* SCSI_CURRENT_SENSING */
#if SCSI_AUTO_TERMINATION
SCSI_UEXACT8 SCSIhReadFlexRegister(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
#endif /* SCSI_AUTO_TERMINATION */
#if (SCSI_CURRENT_SENSING || SCSI_UPDATE_TERMINATION_ENABLE || SCSI_AUTO_TERMINATION)
void SCSIhWriteFlexRegister(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8);
#endif /* (SCSI_CURRENT_SENSING || SCSI_UPDATE_TERMINATION_ENABLE || SCSI_AUTO_TERMINATION) */
SCSI_UEXACT8 SCSIhSetDataFifoThrshDefault(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhGetDataFifoThrsh(SCSI_HHCB SCSI_HPTR);
void SCSIhUpdateDataFifoThrsh(SCSI_HHCB SCSI_HPTR);

                  
/* HWMINTR.C */
int SCSIhCmdComplete(SCSI_HHCB SCSI_HPTR);
void SCSIhIntClrDelay (SCSI_HHCB SCSI_HPTR);
void SCSIhIntSrst(SCSI_HHCB SCSI_HPTR);
void SCSIhIntIOError(SCSI_HHCB SCSI_HPTR);
void SCSIhCheckLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhStandardU320Underrun(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhStandardEnhU320Underrun(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhDchU320Underrun(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhDownshiftU320Underrun(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhDownshiftEnhU320Underrun(SCSI_HHCB SCSI_HPTR);

#if SCSI_DOMAIN_VALIDATION
void SCSIhDVTimeout(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_DOMAIN_VALIDATION */
void SCSIhCdbAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhCheckCondition(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhBadSeq(SCSI_HHCB SCSI_HPTR, SCSI_UEXACT8);
void SCSIhAbortTarget(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhIntSelto(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhIntFree(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhPhysicalError(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320RequestSense(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); 
void SCSIhStandardEnhU320RequestSense(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDchU320RequestSense(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); 
void SCSIhDownshiftU320RequestSense(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftEnhU320RequestSense(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTerminateCommand(SCSI_HIOB SCSI_IPTR);
void SCSIhSetStat(SCSI_HIOB SCSI_IPTR);
void SCSIhBreakInterrupt(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);

#if !SCSI_ASPI_SUPPORT
void SCSIhStandardU320PackActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardEnhU320PackActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDchU320PackActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardRemoveActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320NonPackActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardEnhU320NonPackActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDchU320NonPackActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320ActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhStandardEnhU320ActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhDchU320ActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
#endif /* !SCSI_ASPI_SUPPORT */
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
void SCSIhPacketizedCheckCondition(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhPacketizedGetSenseData(SCSI_HIOB SCSI_IPTR);
void SCSIhPacketizedPhysicalError(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhSimulateBitBucket(SCSI_HHCB SCSI_HPTR);
#if !SCSI_ASPI_REVA_SUPPORT
void SCSIhLQIAbortOrLQIBadLQI(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);                              
void SCSIhLQOErrors(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);                              
#endif /* !SCSI_ASPI_REVA_SUPPORT */
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */
SCSI_UEXACT32 SCSIhStandardU320ResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT32 SCSIhStandardEnhU320ResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT32 SCSIhDchU320ResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT32 SCSIhDownshiftU320ResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhBusWasDead(SCSI_HHCB SCSI_HPTR);

#if SCSI_DCH_U320_MODE
void SCSIhHandleRBIError(SCSI_HHCB SCSI_HPTR);
#else
void SCSIhHandlePCIError(SCSI_HHCB SCSI_HPTR);
void SCSIhHandleSplitInterrupt(SCSI_HHCB SCSI_HPTR);
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
void SCSIhHandleHwErrInterrupt(SCSI_HHCB SCSI_HPTR);
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */
void SCSIhDataParity(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhGetPcixStatus(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhClearPCIorPCIXError(SCSI_HHCB SCSI_HPTR);
#endif /* SCSI_DCH_U320_MODE */

void SCSIhStandardUpdateExeQ(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320UpdateExeQ(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDchU320UpdateExeQ(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);


/* HWMSE2.C */
SCSI_UEXACT16 SCSIhReadE2Register(SCSI_UINT16,SCSI_REGISTER,SCSI_UINT);
void SCSIhWriteE2Register(SCSI_UINT16,SCSI_UINT16,SCSI_REGISTER,SCSI_UINT);
void SCSIhSendE2Address(SCSI_UINT16,SCSI_INT,SCSI_REGISTER);
void SCSIhWait2usec(SCSI_UEXACT8,SCSI_REGISTER );
#if SCSI_AICU320
SCSI_INT SCSIhU320ReadSEEPROM (SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8 SCSI_SPTR,SCSI_SINT16,SCSI_SINT16);
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
SCSI_INT SCSIhU320WriteSEEPROM (SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8 SCSI_SPTR,SCSI_SINT16,SCSI_SINT16);
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */
#endif /* SCSI_AICU320 */


/* HWMTRGT.C */
#if SCSI_TARGET_OPERATION
void SCSIhTargetIntSelIn(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetATNIntr(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhTargetSendMsgByte(SCSI_UEXACT8,SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhTargetCmdPhase(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8); 
void  SCSIhSetTargetMode(SCSI_HIOB SCSI_IPTR); 
SCSI_UEXACT8 SCSIhTargetHandleATN(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhTargetDecodeMsgs(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8 SCSI_SPTR,SCSI_UEXACT8);
void SCSIhTargetSendHiob(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);   
SCSI_UEXACT8 SCSIhTargetRespondToWide(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8 SCSI_SPTR);
void  SCSIhTargetModeEnable(SCSI_HHCB SCSI_HPTR);                               
SCSI_UEXACT8 SCSIhTargetRespondToSync(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8 SCSI_SPTR);
SCSI_UEXACT8 SCSIhTargetSendMessage(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8 SCSI_SPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhTargetGetMessage(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8 SCSI_SPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhTargetGetMsgBytes(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8 SCSI_SPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhTargetGetOneMsgByte(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhWaitForSPIORDY(SCSI_HHCB SCSI_HPTR);                             
SCSI_NODE SCSI_NPTR SCSIhTargetGetNODE(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhTargetScsiBusFree(SCSI_HHCB SCSI_HPTR);
void SCSIhTerminateTargetCommand(SCSI_HIOB SCSI_IPTR);
void SCSIhTargetScsiBusHeld(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetBusDeviceReset(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhTargetResetTarget(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhTargetCompareNexus(SCSI_NEXUS SCSI_XPTR,SCSI_NEXUS SCSI_XPTR);
SCSI_UEXACT16 SCSIhTargetAbortNexus(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhTargetHoldOnToScsiBus(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8,SCSI_UEXACT8); 
void SCSIhTargetIntSelto(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetParityError(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhTargetTerminateDMA(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetAbortNexusSet(SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhTargetGoToPhase(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8);
void SCSIhTargetGoToPhaseManualPIO(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8);
void SCSIhTargetSetStat(SCSI_HIOB SCSI_IPTR);
void SCSIhTargetSetTaskMngtFunction(SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhTargetRedirectSequencer(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhTargetGetOneCmdByte(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetInitializeNodeTbl(SCSI_HHCB  SCSI_HPTR); 
void SCSIhTargetInitiateWide(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetInitiateSync(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetVerifyWideResponse(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8 SCSI_SPTR);
void SCSIhTargetVerifySyncResponse(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8 SCSI_SPTR);
void SCSIhTargetNegotiate(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhTargetEnableId(SCSI_HIOB SCSI_IPTR);
void SCSIhTargetSetupDisableId(SCSI_HIOB SCSI_IPTR);
void SCSIhTargetDisableId(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhTargetRespondToPPR(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8 SCSI_SPTR);
void SCSIhTargetVerifyPPRResponse(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8 SCSI_SPTR);
/* may no longer need these as only used in hwmtrgt.c */
SCSI_UEXACT8 SCSIhTargetCommandState(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_HPTR, SCSI_NODE SCSI_NPTR);
SCSI_UEXACT8 SCSIhTargetDisconnectState(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_HPTR,SCSI_NODE SCSI_NPTR);
SCSI_UEXACT8 SCSIhTargetNoOp(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_HPTR,SCSI_NODE SCSI_NPTR);
SCSI_UEXACT8 SCSIhTargetSelectionState(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_HPTR,SCSI_NODE SCSI_NPTR);
SCSI_UEXACT8 SCSIhTargetIdentifyState(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_HPTR,SCSI_NODE SCSI_NPTR);
SCSI_UEXACT8 SCSIhTargetMessageOutState(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_HPTR,SCSI_NODE SCSI_NPTR); 
SCSI_UEXACT8 SCSIhTargetNegotiated(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhTargetSetNegotiatedState(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8);
void SCSIhTargetForceTargetNegotiation(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhTargetStandardSendHiobSpecial(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
/* Standard U320 routines */
void SCSIhTargetStandardCommonResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetStandardU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetStandardEnhU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetDchU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetStandardU320SetIgnoreWideMsg(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardEnhU320SetIgnoreWideMsg(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetDchU320SetIgnoreWideMsg(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardU320DeliverEstScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardU320GetEstScbFields(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhTargetStandardEnhU320GetEstScbFields(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhTargetDchU320GetEstScbFields(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhTargetStandardU320SetFirmwareProfile(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetStandardEnhU320SetFirmwareProfile(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetDchU320SetFirmwareProfile(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetStandardU320EstActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhTargetStandardEnhU320EstActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhTargetDchU320EstActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
#if SCSI_PROFILE_INFORMATION
void SCSIhGetTargetModeProfileFields(SCSI_HW_INFORMATION SCSI_LPTR,SCSI_HHCB SCSI_HPTR);
void SCSIhSetTargetModeProfileFields(SCSI_HW_INFORMATION SCSI_LPTR,SCSI_HHCB SCSI_HPTR);
#endif /* SCSI_PROFILE_INFORMATION */
void SCSIhTargetModeHWInitialize(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetModeSWInitialize(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetSetDGCRCInterval(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhTargetDetermineCDBLength(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhTargetHandleLegacySelIn(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_NEXUS SCSI_IPTR);
#if (SCSI_PACKETIZED_IO_SUPPORT && (SCSI_STANDARD_ENHANCED_U320_MODE || SCSI_DCH_U320_MODE))
void SCSIhTargetSetIUCRCInterval(SCSI_HHCB SCSI_HPTR);
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && (SCSI_STANDARD_ENHANCED_U320_MODE || SCSI_DCH_U320_MODE)) */
#if SCSI_MULTIPLEID_SUPPORT
void SCSIhTargetLoadTargetIDs(SCSI_HHCB SCSI_HPTR);
#endif /* SCSI_MULTIPLEID_SUPPORT */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
SCSI_UEXACT8 SCSIhTargetStandardU320SelInType(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhTargetStandardEnhU320SelInType(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardEnhU320IUSelIn(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetLQOErrors(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetLQIErrors(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetInitSPIStatusBuffers (SCSI_HHCB SCSI_HPTR);
void SCSIhTargetStandardEnhU320BuildStatusIU(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetCompleteAutoResponse(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhTargetHandleIUSelIn(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_NEXUS SCSI_IPTR);
void SCSIhTargetStandardEnhU320InitErrorHIOB(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetBuildErrorHIOB(SCSI_HHCB SCSI_HPTR);
SCSI_UINT8 SCSIhTargetStandardU320AssignErrorScbDescriptor(SCSI_HHCB SCSI_HPTR);
SCSI_UINT8 SCSIhTargetStandardEnhU320AssignErrorScbDescriptor(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetStandardEnhU320InitErrorHIOB(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhTargetSelATNIUsEnabled(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_NODE SCSI_NPTR);
void SCSIhTargetRetrain(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhTargetStandardEnhU320LqoAtnLq(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetRetryRequest(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */



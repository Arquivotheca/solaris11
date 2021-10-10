/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/* $Header: /vobs/u320chim/src/chim/xlm/xlmdef.h   /main/58
 * Tue Jul 15 11:11:07 2003   rog22390 $
 */
/*
 *                                                                          *
 * Copyright 1995-2003 Adaptec, Inc.  All Rights Reserved.                  *
 *                                                                          *
 * This software contains the valuable trade secrets of Adaptec or its      *
 * licensors.  The software is protected under international copyright      *
 * laws and treaties.  This software may only be used in accordance with    *
 * terms of its accompanying license agreement.                             *
 *                                                                          *
 */

#ifndef	_XLMDEF_H
#define	_XLMDEF_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/***************************************************************************
*
*  Module Name:   XLMDEF.H
*
*  Description:   Definitions for Translation Layer data structures.
*    
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         1. This file is referenced by all three layers of Common
*                    HIM implementation. 
*
***************************************************************************/

#ifndef SCSI_DOMAIN_VALIDATION   
#error SCSI_DOMAIN_VALIDATION Should be defined here
#endif

/***************************************************************************
* Macros for accessing OSM routines
***************************************************************************/
#define  OSMxMapIOHandle       SCSI_OSMFP(adapterTSH).OSMMapIOHandle
#define  OSMxReleaseIOHandle   SCSI_OSMFP(adapterTSH).OSMReleaseIOHandle
#define  OSMxEvent             SCSI_OSMFP(adapterTSH).OSMEvent
#define  OSMxGetBusAddress     SCSI_OSMFP(adapterTSH).OSMGetBusAddress
#define  OSMxAdjustBusAddress  SCSI_OSMFP(adapterTSH).OSMAdjustBusAddress
#define  OSMxGetNVSize         SCSI_OSMFP(adapterTSH).OSMGetNVSize
#define  OSMxPutNVData         SCSI_OSMFP(adapterTSH).OSMPutNVData
#define  OSMxGetNVData         SCSI_OSMFP(adapterTSH).OSMGetNVData
#define  OSMxReadUExact8       SCSI_OSMFP(adapterTSH).OSMReadUExact8
#define  OSMxReadUExact16      SCSI_OSMFP(adapterTSH).OSMReadUExact16
#define  OSMxReadUExact32      SCSI_OSMFP(adapterTSH).OSMReadUExact32
#define  OSMxReadStringUExact8 SCSI_OSMFP(adapterTSH).OSMReadStringUExact8
#define  OSMxReadStringUExact16 SCSI_OSMFP(adapterTSH).OSMReadStringUExact16
#define  OSMxReadStringUExact32 SCSI_OSMFP(adapterTSH).OSMReadStringUExact32
#define  OSMxWriteUExact8      SCSI_OSMFP(adapterTSH).OSMWriteUExact8
#define  OSMxWriteUExact16     SCSI_OSMFP(adapterTSH).OSMWriteUExact16
#define  OSMxWriteUExact32     SCSI_OSMFP(adapterTSH).OSMWriteUExact32
#define  OSMxWriteStringUExact8 SCSI_OSMFP(adapterTSH).OSMWriteStringUExact8
#define  OSMxWriteStringUExact16 SCSI_OSMFP(adapterTSH).OSMWriteStringUExact16
#define  OSMxWriteStringUExact32 SCSI_OSMFP(adapterTSH).OSMWriteStringUExact32
#define  OSMxSynchronizeRange  SCSI_OSMFP(adapterTSH).OSMSynchronizeRange
#define  OSMxWatchdog          SCSI_OSMFP(adapterTSH).OSMWatchdog
#define  OSMxSaveInterruptState   SCSI_OSMFP(adapterTSH).OSMSaveInterruptState
#define  OSMxSetInterruptState SCSI_OSMFP(adapterTSH).OSMSetInterruptState
#define  OSMxReadPCIConfigurationDword  SCSI_OSMFP(adapterTSH).OSMReadPCIConfigurationDword
#define  OSMxReadPCIConfigurationWord  SCSI_OSMFP(adapterTSH).OSMReadPCIConfigurationWord
#define  OSMxReadPCIConfigurationByte  SCSI_OSMFP(adapterTSH).OSMReadPCIConfigurationByte
#define  OSMxWritePCIConfigurationDword  SCSI_OSMFP(adapterTSH).OSMWritePCIConfigurationDword
#define  OSMxWritePCIConfigurationWord  SCSI_OSMFP(adapterTSH).OSMWritePCIConfigurationWord
#define  OSMxWritePCIConfigurationByte  SCSI_OSMFP(adapterTSH).OSMWritePCIConfigurationByte

#define  OSMxDelay                      SCSI_OSMFP(adapterTSH).OSMDelay              

/***************************************************************************
* Definitions for type of handle
***************************************************************************/
#define  SCSI_TSCB_TYPE_INITIALIZATION  0
#define  SCSI_TSCB_TYPE_ADAPTER         1
#define  SCSI_TSCB_TYPE_TARGET          2

/***************************************************************************
* SCSI_INITIALIZATION_TSCB structures definitions
***************************************************************************/
typedef struct SCSI_INITIALIZATION_TSCB_
{
   SCSI_UINT8 typeTSCB;                      /* type of handle           */
   SCSI_MEMORY_TABLE memoryTable[2];         /* requirement of HW layer  */
   HIM_CONFIGURATION configuration;          /* area for init HIM config */
} SCSI_INITIALIZATION_TSCB;

/***************************************************************************
* HIM_TARGET_TSCB structure definition
***************************************************************************/
struct SCSI_TARGET_TSCB_
{
   SCSI_UINT8 typeTSCB;                      /* type of handle           */
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH;     /* adapter task set handle  */
   SCSI_UINT8 scsiID;                        /* device scsi id           */
   SCSI_UINT8 lunID;                         /* target lun id            */
   struct
   {
      SCSI_UINT8 tagEnable:1;                /* tag enable               */
   } targetAttributes;
   SCSI_UNIT_CONTROL unitControl;            /* SCSI target unit control */
   struct SCSI_TARGET_TSCB HIM_PTR nextTargetTSCB; /* next target TSCB   */
};

/***************************************************************************
* HIM_ADAPTER_TSCB structure definition
***************************************************************************/
struct SCSI_ADAPTER_TSCB_
{
   SCSI_UINT8 typeTSCB;                      /* type of handle             */
   HIM_HOST_ID productID;                    /* host id                    */
   HIM_HOST_ADDRESS hostAddress;             /* host address               */
   SCSI_HHCB   hhcb;                         /* hhcb associated            */
#if SCSI_BIOS_ASPI8DOS
   SCSI_BIOS_INFORMATION biosInformation;    /* bios related information   */
#endif /* SCSI_BIOS_ASPI8DOS */
   SCSI_MEMORY_TABLE memoryTable;            /* requirement of HW layer    */
   HIM_OSM_FUNC_PTRS osmFuncPtrs;            /* functional pointers to OSM */
   void HIM_PTR osmAdapterContext;           /* osm adapter context ptr    */
   SCSI_IO_HANDLE scsiHandle;                /* io handle for reg access   */
                                             /* routine to build SCB       */
   void (*OSDBuildSCB)(SCSI_HIOB HIM_PTR,
                       HIM_IOB HIM_PTR,
                       SCSI_TARGET_TSCB HIM_PTR,
                       SCSI_ADAPTER_TSCB HIM_PTR);   
#if SCSI_TARGET_OPERATION  
                                             /* Pointer to routine to      */
                                             /* build a Target SCB         */    
   void (*OSDBuildTargetSCB)(SCSI_HIOB HIM_PTR,
                             HIM_IOB HIM_PTR,
                             SCSI_NEXUS SCSI_XPTR,
                             SCSI_ADAPTER_TSCB HIM_PTR);
#endif /* SCSI_TARGET_OPERATION */ 
   SCSI_UEXACT8 NumberLuns[SCSI_MAXDEV];     /* number of luns to scan in prot auto conf */
   SCSI_UEXACT8 lunExist[SCSI_MAXDEV*SCSI_MAXLUN/8]; /* lun exist table    */
   SCSI_UEXACT8 tshExist[SCSI_MAXDEV*SCSI_MAXLUN/8]; /* tsh exist table    */
   SCSI_UEXACT16 scsi1OrScsi2Device;                 /* the Inquiry data   */
                                                     /* indicates this is  */
                                                     /* SCSI2 or earlier   */
                                                     /* device. Bit map    */
                                                     /* indexed by SCSI ID */

   SCSI_UEXACT16 tagEnable;                  /* tag enable flags           */
   void HIM_PTR moreLocked;                  /* more locked memory         */
   void HIM_PTR moreUnlocked;                /* more unlocked memory       */
   SCSI_TARGET_TSCB targetTSCBBusScan;       /* target TSCB for bus scan   */
   HIM_IOB HIM_PTR iobProtocolAutoConfig;    /* iob with protocol auto cfg */
   SCSI_UINT16 retryBusScan;                 /* current retry count        */
   HIM_UINT16  pacIobStatus;                 /* Temporary storage for PAC  */
                                             /*  IOB taskStatus.           */
#if SCSI_DOMAIN_VALIDATION
   union
   {
      SCSI_UINT16 u16;
      struct 
      {
         SCSI_UINT8 dvLevel:2;               /* level of domain validation */
         SCSI_UINT8 dvState:5;               /* executing inquiry          */
         SCSI_UINT8 dvPassed:1;              /* device passed domain validation */
         SCSI_UINT8 dvThrottle:3;            /* throttle state             */
         SCSI_UINT8 dvFallBack:1;            /* fell back to lower xfer rate */
         SCSI_UINT8 dvThrottleOSMReset:1;    /* will throtlle if get a OSM bus reset */

      } bits;
   } dvFlags[SCSI_MAXDEV];
#if (SCSI_DOMAIN_VALIDATION)
   SCSI_UEXACT8  dvDataPatternPresent;       /* OSM-defined patter is present */
   SCSI_UEXACT8  retryDVBufferConflict;      
   SCSI_UEXACT8  dvBufferSize;
   SCSI_UEXACT8  dvReportedBufferSize;
   SCSI_UEXACT16 dvBufferOffset;
   SCSI_UEXACT8  reserved1[2];               /* keep alignment             */
#endif 
   
   SCSI_UEXACT16 domainValidationIDMask;     /* specifies the mask of scsi Ids */
                                             /* on which DV is to be performed.*/
#endif /* SCSI_DOMAIN_VALIDATION */
   SCSI_UINT8 allocBusAddressSize;           /* max bus address size for   */
                                             /* locked memory allocated in */
                                             /* HIMSetMemoryPointer()      */
#if !SCSI_DISABLE_PROBE_SUPPORT
   SCSI_UINT8 lastScsiIDProbed;              /* SCSI ID from last HIM_PROBE*/
   SCSI_UINT8 lastScsiLUNProbed;             /* LUN from last HIM_PROBE    */
#endif /* !SCSI_DISABLE_PROBE_SUPPORT */   
   HIM_BOOLEAN initializeIOBus;              /* Indicates whether SCSI bus */
                                             /* reset is to be issued at   */
                                             /* initialization during      */
                                             /* HIMInitialize.             */     
   union
   {
      SCSI_UINT8 u8;
      struct 
      {
         SCSI_UINT8 initialized:1;           /* HIMInitialize has been called */
         SCSI_UINT8 osmFrozen:1;             /* OSM has been frozen by OSMEvent */
#if SCSI_DOMAIN_VALIDATION
         SCSI_UINT8 dvInProgress:1;          /* DV in progress state       */              
         SCSI_UINT8 dvFrozen:1;              /* DV frozen due to OSM Freeze */  
#endif /* SCSI_DOMAIN_VALIDATION */
#if SCSI_PAC_SEND_SSU
         SCSI_UINT8 ssuInProgress:1;         /* SSU command has been done or not */
#endif /* SCSI_PAC_SEND_SSU */
      } bits;
   } flags;
#if (SCSI_STANDARD_U320_MODE && SCSI_PACKETIZED_IO_SUPPORT)
   SCSI_UEXACT8 zeros[6];                    /* A small array of zeros for
                                              * optimization purposes.
                                              */
#endif /* (SCSI_STANDARD_U320_MODE && SCSI_PACKETIZED_IO_SUPPORT) */
};
#define  SCSI_AF_initialized                 flags.bits.initialized 
#define  SCSI_AF_osmFrozen                   flags.bits.osmFrozen
#if SCSI_DOMAIN_VALIDATION
#define  SCSI_AF_dvInProgress                flags.bits.dvInProgress              
#define  SCSI_AF_dvFrozen                    flags.bits.dvFrozen
#endif /* SCSI_DOMAIN_VALIDATION */
#if SCSI_PAC_SEND_SSU
#define  SCSI_AF_ssuInProgress               flags.bits.ssuInProgress
#endif /* SCSI_PAC_SEND_SSU */

#if SCSI_DOMAIN_VALIDATION
#define  SCSI_AF_dvLevel(targetID)           dvFlags[(targetID)].bits.dvLevel
#define  SCSI_AF_dvState(targetID)           dvFlags[(targetID)].bits.dvState
#define  SCSI_AF_dvPassed(targetID)          dvFlags[(targetID)].bits.dvPassed
#define  SCSI_AF_dvThrottle(targetID)        dvFlags[(targetID)].bits.dvThrottle
#define  SCSI_AF_dvFallBack(targetID)        dvFlags[(targetID)].bits.dvFallBack
#define  SCSI_AF_dvThrottleOSMReset(targetID)  dvFlags[(targetID)].bits.dvThrottleOSMReset
#endif /* SCSI_DOMAIN_VALIDATION */

/***************************************************************************
* SCSI_IOB_RESERVE structure definition
***************************************************************************/
#define  SCSI_MAX_CDBLEN   32
typedef struct SCSI_IOB_RESERVE_
{
   HIM_UEXACT8 cdb[SCSI_MAX_CDBLEN];         /* cdb buffer              */
   SCSI_HIOB hiob;                           /* hiob embedded           */
   HIM_IOB HIM_PTR iob;                      /* pointer to iob          */
}  SCSI_IOB_RESERVE;

/***************************************************************************
* More memory definitions for translation management layer
***************************************************************************/
#define  SCSI_INDEX_MORE_LOCKED     SCSI_MAX_MEMORY
#define  SCSI_INDEX_MORE_UNLOCKED   SCSI_MAX_MEMORY+1

/* SCSI_MORE_LOCKED area defines */
/* The following are defines for the size of each area */
#define  SCSI_SIZE_INQUIRY          64
#if (SCSI_DOMAIN_VALIDATION)
#define  SCSI_SIZE_RWB              252   /* maximum buffer size    */
#else
#define  SCSI_SIZE_RWB              128   /* maximum buffer size    */
#endif /*(SCSI_DOMAIN_VALIDATION)*/

#define  SCSI_SIZE_DEFAULT_RWB      128   /* When REBD doesn't work */

#define  SCSI_SIZE_REBD              4    /* size of read buffer (echo     */
                                          /* buffer descriptor mode. Note; */
                                          /* this is not used to calculate */
                                          /* size of area.                 */  
#define  SCSI_SIZE_SENSE_DATA       32
#if SCSI_DOMAIN_VALIDATION
#define  SCSI_SIZE_INQUIRY_DVBASIC  36
#endif /* SCSI_DOMAIN_VALIDATION */ 
/* The following are the offsets of the beginning of each area */
/* Warning make sure start of each area is on a pointer size */
/* bounary by using SCSI_COVQ as some O.S. require cast of area */
/* to a void pointer to start on boundary. */  
#define  SCSI_MORE_INQSG            0
#define  SCSI_MORE_RWBSG            0
/* Change for Common SG support which always uses 16 byte SG Element sizes. */
#if (SCSI_DCH_U320_MODE && (OSM_BUS_ADDRESS_SIZE == 32))
#define  SCSI_MORE_INQDATA          SCSI_COVQ(sizeof(SCSI_BUS_ADDRESS)*4,sizeof(void HIM_PTR))
#else
#define  SCSI_MORE_INQDATA          SCSI_COVQ(sizeof(SCSI_BUS_ADDRESS)*2,sizeof(void HIM_PTR))
#endif /* ((OSM_BUS_ADDRESS_SIZE == 32) && SCSI_DCH_U320_MODE) */
#define  SCSI_MORE_RWBDATA          SCSI_MORE_INQDATA


#if (SCSI_DOMAIN_VALIDATION_ENHANCED)&&(SCSI_DOMAIN_VALIDATION)
#define  SCSI_MORE_SNSDATA          SCSI_MORE_INQDATA+\
                                    SCSI_COVQ(SCSI_SIZE_RWB,sizeof(void HIM_PTR))
#else
#define  SCSI_MORE_SNSDATA          SCSI_MORE_INQDATA+\
                                    SCSI_COVQ(SCSI_SIZE_INQUIRY,sizeof(void HIM_PTR))
#endif /* SCSI_DOMAIN_VALIDATION_ENHANCED */

#define  SCSI_MORE_IOBRSV           SCSI_MORE_SNSDATA+\
                                    SCSI_COVQ(SCSI_SIZE_SENSE_DATA,sizeof(void HIM_PTR))
#define  SCSI_MORE_LOCKED           SCSI_MORE_IOBRSV+\
                                    SCSI_COVQ(sizeof(SCSI_IOB_RESERVE),sizeof(void HIM_PTR))

/* SCSI_MORE_UNLOCKED area defines */
/* The following are defines for the size of each area */
#define  SCSI_TUR_CDB_SIZE          6  /* TUR CDB size */
#define  SCSI_INQ_CDB_SIZE          6  /* INQ CDB size */
#define  SCSI_SSU_CDB_SIZE          6  /* START/STOP UNIT CDB size */
#define  SCSI_RB_WB_CDB_SIZE       10  /* READ BUFFER/WRITE BUFFER CDB size */

/* The following are the offsets of the beginning of each area */
/* Warning make sure start of each area is on a pointer size */
/* bounary by using SCSI_COVQ as some O.S. require cast of area */
/* to a void pointer to start on boundary. */ 
#define  SCSI_MORE_IOB              0
#define  SCSI_MORE_SPECIAL_IOB      SCSI_COVQ(sizeof(HIM_IOB),sizeof(void HIM_PTR))
#define  SCSI_MORE_INQCDB           SCSI_MORE_SPECIAL_IOB+SCSI_COVQ(sizeof(HIM_IOB),sizeof(void HIM_PTR))

#if SCSI_DOMAIN_VALIDATION
#if SCSI_DOMAIN_VALIDATION_ENHANCED
#define  SCSI_MORE_RWBCDB           SCSI_MORE_INQCDB
#define  SCSI_MORE_REFDATA          SCSI_MORE_INQCDB+\
                                    SCSI_COVQ(SCSI_RB_WB_CDB_SIZE,sizeof(void HIM_PTR))
#else
#define  SCSI_MORE_REFDATA          SCSI_MORE_INQCDB+\
                                    SCSI_COVQ(SCSI_INQ_CDB_SIZE,sizeof(void HIM_PTR))
#endif   /* SCSI_DOMAIN_VALIDATION_ENHANCED */

#define  SCSI_MORE_TRANSPORT_SPECIFIC  SCSI_MORE_REFDATA+\
                                       SCSI_COVQ(16*SCSI_SIZE_INQUIRY,sizeof(void HIM_PTR))
#else    /* !SCSI_DOMAIN_VALIDATION */

#define  SCSI_MORE_TRANSPORT_SPECIFIC  SCSI_MORE_INQCDB+\
                                       SCSI_COVQ(SCSI_INQ_CDB_SIZE,sizeof(void HIM_PTR))
#endif   /* SCSI_DOMAIN_VALIDATION */


#define  SCSI_MORE_UNLOCKED         SCSI_MORE_TRANSPORT_SPECIFIC+\
                                    SCSI_COVQ(sizeof(HIM_TS_SCSI),sizeof(void HIM_PTR))

#define  SCSI_MORE_LOCKED_ALIGN     2*sizeof(SCSI_BUS_ADDRESS)-1
#define  SCSI_MORE_UNLOCKED_ALIGN   2*sizeof(SCSI_BUS_ADDRESS)-1

/***************************************************************************
* SCSI_AF_dvState (per device)
***************************************************************************/

/* Mode 0 States */
#define  SCSI_DV_READY              0        /* ready to exercise          */
#define  SCSI_DV_INQUIRY            1        /* executing inquiry          */
#define  SCSI_DV_WRITEBUFFER        2        /* executing write buffer     */
#define  SCSI_DV_READBUFFER         3        /* executing read buffer      */
#define  SCSI_DV_DONE               4        /* done with DV exercise      */
#define  SCSI_DV_READBUFFER_DESCRIPTOR 5     /* executing read descriptor  */

/* Mode 1+ States */
#define  SCSI_DV_STATE_SEND_ASYNC_INQUIRY 0  /* send async inquiry         */
#define  SCSI_DV_STATE_SEND_INQUIRY       1  /* send inquiry               */
#define  SCSI_DV_STATE_SEND_REBD          2  /* send read echo buffer desc */
#define  SCSI_DV_STATE_SEND_WEB           3  /* send write echo buffer     */
#define  SCSI_DV_STATE_SEND_REB           4  /* send read echo buffer      */

#define  SCSI_DV_STATE_ASYNC_INQ_OUTSTANDING 5 /* Async inquiry outstanding*/
#define  SCSI_DV_STATE_INQ_OUTSTANDING       6 /* inquiry outstandin       */
#define  SCSI_DV_STATE_REBD_OUTSTANDING      7 /* read echo buf desc out.  */
#define  SCSI_DV_STATE_WEB_OUTSTANDING       8 /* write echo buf outs.     */
#define  SCSI_DV_STATE_REB_OUTSTANDING       9 /* read echo buf outs.      */
#define  SCSI_DV_STATE_GET_NEXT_DEVICE      10 /* get next device          */
#define  SCSI_DV_STATE_DONE                 11 /* dv done                  */

#define  SCSI_DV_STATE() (adapterTSH->SCSI_AF_dvState(targetTSCB->scsiID))
#define  SCSI_POSTDV_STATE() ( SCSI_DV_STATE() >= SCSI_DV_STATE_ASYNC_INQ_OUTSTANDING )

/***************************************************************************
* DV Miscelaneous Definitions
***************************************************************************/

#define  SCSI_DV_BENIGN_RETRIES  0x02
#define  SCSI_DV_BUFFER_CONFLICT_RETRIES 0x0A
#define  SCSI_DV_DATA_PATTERN(index) (((SCSI_UEXACT8 HIM_PTR) transportSpecific->dvPattern.virtualAddress)[index])
#define  SCSI_DV_PATTERN_LENGTH      (transportSpecific->dvPattern.bufferSize)

#define  SCSI_DV_HEADER_SIZE    0x04
#define  SCSI_DV_ADPT_SIGNATURE (SCSI_UEXACT8)(0xCA)

#define  SCSI_DV_HEADER_MISCOMPARE 0x01
#define  SCSI_DV_DATA_MISCOMPARE   0x02

#define  SCSI_DV_EBOS 0x01

/***************************************************************************
* DV Sense Key, ASC and ASCQ Definitions
***************************************************************************/

#define  SCSI_SENSEKEY_RECOVERED_ERROR 0x01
#define  SCSI_SENSEKEY_NOT_READY       0x02
#define  SCSI_SENSEKEY_ILLEGAL_REQUEST 0x05
#define  SCSI_SENSEKEY_UNIT_ATTENTION  0x06
#define  SCSI_SENSEKEY_ABORTED_COMMAND 0x0B

#define  SCSI_ASCQ_ECHO_BUFFER_OVERWRITTEN 0x0F 

/***************************************************************************
* DV Exceptions - used in PostDomainValidationSM
***************************************************************************/

#define  SCSI_DV_NO_EXCEPTION                         0x0000
#define  SCSI_DV_ILLEGAL_REQUEST_EXCEPTION            0x0001
#define  SCSI_DV_BENIGN_CHECK_EXCEPTION               0x0002
#define  SCSI_DV_THROTTLING_CHECK_EXCEPTION           0x0004
#define  SCSI_DV_RESERVATION_CONFLICT_EXCEPTION       0x0008
#define  SCSI_DV_IOB_BUSY_EXCEPTION                   0x0010
#define  SCSI_DV_CATASTROPHIC_EXCEPTION               0x0020
#define  SCSI_DV_DATA_ERROR_EXCEPTION                 0x0040
#define  SCSI_DV_ECHO_BUFFER_OVERWRITTEN_EXCEPTION    0x0080
#define  SCSI_DV_DATA_UNDERRUN_EXCEPTION              0x0100
#define  SCSI_DV_OTHER_EXCEPTION                      0x0200

/***************************************************************************
* SCSI_AF_dvThrottle
***************************************************************************/
#define SCSI_DE_WIDE                0        /* DE/wide stage (initial)    */
#define SCSI_SE_WIDE                1        /* SE/wide stage              */
#define SCSI_SE_NARROW              2        /* SE/narrow stage            */
#define SCSI_SE_WIDE_REPEAT         3        /* SE/wide stage repeat (initial) */
#define SCSI_SE_NARROW_REPEAT       4        /* SE/narrow stage repeat (initial) */
#define SCSI_ASYNC_NARROW           5        /* async/narrow               */

/***************************************************************************
*  CHIM SCSI interface function prototypes
***************************************************************************/
/* XLMINIT.C */
void SCSIGetFunctionPointers(HIM_FUNC_PTRS HIM_PTR,HIM_UINT16);
HIM_HOST_ID SCSIGetNextHostDeviceType(HIM_UINT16,HIM_UINT8 HIM_PTR,HIM_HOST_ID HIM_PTR);
HIM_TASK_SET_HANDLE SCSICreateInitializationTSCB(void HIM_PTR);
void SCSIGetConfiguration (HIM_TASK_SET_HANDLE,HIM_CONFIGURATION HIM_PTR,HIM_HOST_ID);
HIM_UINT8 SCSISetConfiguration (HIM_TASK_SET_HANDLE,HIM_CONFIGURATION HIM_PTR,HIM_HOST_ID);
HIM_UINT32 SCSISizeAdapterTSCB (HIM_TASK_SET_HANDLE,HIM_HOST_ID);
HIM_TASK_SET_HANDLE SCSICreateAdapterTSCB (HIM_TASK_SET_HANDLE,void HIM_PTR,void HIM_PTR,HIM_HOST_ADDRESS,HIM_HOST_ID);
HIM_UINT8 SCSISetupAdapterTSCB(HIM_TASK_SET_HANDLE,HIM_OSM_FUNC_PTRS HIM_PTR,HIM_UINT16);
HIM_UINT32 SCSICheckMemoryNeeded(HIM_TASK_SET_HANDLE, HIM_TASK_SET_HANDLE, 
		HIM_HOST_ID, HIM_UINT16, HIM_UINT8 HIM_PTR, HIM_UINT32 HIM_PTR, 
		HIM_UINT32 HIM_PTR, HIM_ULONG HIM_PTR);
HIM_UINT8 SCSISetMemoryPointer(HIM_TASK_SET_HANDLE,HIM_UINT16,HIM_UINT8,void HIM_PTR,HIM_UINT32);
HIM_UINT8 SCSIVerifyAdapter(HIM_TASK_SET_HANDLE);
HIM_UINT8 SCSIInitialize (HIM_TASK_SET_HANDLE);
HIM_UINT32 SCSISizeTargetTSCB(HIM_TASK_SET_HANDLE);
HIM_UINT8 SCSICheckTargetTSCBNeeded(HIM_TASK_SET_HANDLE,HIM_UINT16);
HIM_TASK_SET_HANDLE SCSICreateTargetTSCB(HIM_TASK_SET_HANDLE,HIM_UINT16,void HIM_PTR);

/* XLMTASK.C */
void SCSIDisableIRQ (HIM_TASK_SET_HANDLE);
void SCSIEnableIRQ (HIM_TASK_SET_HANDLE);
HIM_UINT8 SCSIPollIRQ (HIM_TASK_SET_HANDLE);
HIM_UINT8 SCSIFrontEndISR (HIM_TASK_SET_HANDLE);
void SCSIBackEndISR (HIM_TASK_SET_HANDLE);
void SCSIQueueIOB (HIM_IOB HIM_PTR);
HIM_UINT8 SCSIPowerEvent (HIM_TASK_SET_HANDLE,HIM_UINT8);
HIM_UINT8 SCSIValidateTargetTSH (HIM_TASK_SET_HANDLE);
HIM_UINT8 SCSIxNonScamValidateLun(SCSI_TARGET_TSCB HIM_PTR);
HIM_UINT8 SCSIClearTargetTSH (HIM_TASK_SET_HANDLE);
void SCSISaveState (HIM_TASK_SET_HANDLE,void HIM_PTR);
void SCSIRestoreState (HIM_TASK_SET_HANDLE,void HIM_PTR);
HIM_UINT8 SCSIProfileAdapter (HIM_TASK_SET_HANDLE,HIM_ADAPTER_PROFILE HIM_PTR);
HIM_UINT8 SCSIReportAdjustableAdapterProfile (HIM_TASK_SET_HANDLE,HIM_ADAPTER_PROFILE HIM_PTR);
HIM_UINT8 SCSIAdjustAdapterProfile (HIM_TASK_SET_HANDLE,HIM_ADAPTER_PROFILE HIM_PTR);
HIM_UINT8 SCSIProfileTarget(HIM_TASK_SET_HANDLE,HIM_TARGET_PROFILE HIM_PTR);
HIM_UINT8 SCSIReportAdjustableTargetProfile(HIM_TASK_SET_HANDLE,HIM_TARGET_PROFILE HIM_PTR);
HIM_UINT8 SCSIAdjustTargetProfile(HIM_TASK_SET_HANDLE,HIM_TARGET_PROFILE HIM_PTR);
HIM_UINT32 SCSIGetNVSize(HIM_TASK_SET_HANDLE);
HIM_UINT8 SCSIGetNVOSMSegment(HIM_TASK_SET_HANDLE,HIM_UINT32 HIM_PTR,HIM_UINT32 HIM_PTR);
HIM_UINT8 SCSIPutNVData(HIM_TASK_SET_HANDLE,HIM_UINT32,void HIM_PTR,HIM_UINT32);
HIM_UINT8 SCSIGetNVData(HIM_TASK_SET_HANDLE,void HIM_PTR,HIM_UINT32,HIM_UINT32);
HIM_UINT8 SCSIProfileNexus(HIM_TASK_SET_HANDLE,HIM_NEXUS_PROFILE HIM_PTR);
HIM_UINT8 SCSIClearNexusTSH(HIM_TASK_SET_HANDLE);
HIM_UINT8 SCSIProfileNode(HIM_TASK_SET_HANDLE,HIM_NODE_PROFILE HIM_PTR);
HIM_UINT8 SCSIReportAdjustableNodeProfile(HIM_TASK_SET_HANDLE,HIM_NODE_PROFILE HIM_PTR);
HIM_UINT8 SCSIAdjustNodeProfile(HIM_TASK_SET_HANDLE,HIM_NODE_PROFILE HIM_PTR);
HIM_UINT8 SCSISetOSMNodeContext(HIM_TASK_SET_HANDLE,void HIM_PTR);

#if defined(SCSI_XLM_DEFINE)
/***************************************************************************
*  SCSI_HOST_TYPE table setup
***************************************************************************/
SCSI_HOST_TYPE SCSIHostType[] =
{
#if SCSI_AICU320
#if SCSI_IROC
   { 0x809F9005, 0xFFFFFFFF },      /* iROC on AIC-7902                   */
   { 0x80909005, 0xFFFFFFFF },      /* iROC on ASC-39320                  */
   { 0x80919005, 0xFFFFFFFF },      /* iROC on ASC-39320D                 */
   { 0x80929005, 0xFFFFFFFF },      /* iROC on ASC-29320                  */
   { 0x80939005, 0xFFFFFFFF },      /* iROC on ASC-29320B                 */
   { 0x80949005, 0xFFFFFFFF },      /* iROC on ASC-29320LP                */
   { 0x809E9005, 0xFFFFFFFF },      /* iROC on AIC-7901A, repackaged      */
                                    /*   AIC-7902 Rev 4                   */

                                    /* IDs for Harpoon 2 Rev B            */
   { 0x809D9005, 0xFFFFFFFF },      /* iROC on AIC-7902B                  */
   { 0x809C9005, 0xFFFFFFFF },      /* iROC on ASC-39320D                 */
   { 0x80959005, 0xFFFFFFFF },      /* iROC on ASC-39320                  */
   { 0x80969005, 0xFFFFFFFF },      /* iROC on ASC-39320A                 */

                                    /* IDs for Harpoon 1 Rev B            */
   { 0x808F9005, 0xFFFFFFFF },      /* iROC on AIC-7901                   */
   { 0x80809005, 0xFFFFFFFF },      /* iROC on ASC-29320A                 */
   { 0x80979005, 0xFFFFFFFF },      /* iROC on ASC-29320ALP               */

#endif /* SCSI_IROC */
#if SCSI_DCH_U320_MODE
   /* ID for DCH_SCSI                                                     */
   { 0x802F9005, 0xFFFFFFFF },      /* Rocket 1 ASIC: AIC-7942            */
#else /* !SCSI_DCH_U320_MODE */
   { 0x801F9005, 0xFFFFFFFF },      /* Harpoon 2 ASIC: AIC-7902           */
   { 0x80169005, 0xFFFFFFFF },      /* Harpoon 2 ASIC: ASC-39320A         */
   { 0x80109005, 0xFFFFFFFF },      /* Harpoon 2 HBA: ASC-39320           */
   { 0x80119005, 0xFFFFFFFF },      /* Harpoon 2 HBA: ASC-39320D          */
   { 0x80129005, 0xFFFFFFFF },      /* Harpoon 2 HBA: ASC-29320           */
   { 0x80139005, 0xFFFFFFFF },      /* Harpoon 2 HBA: ASC-29320B          */
   { 0x80149005, 0xFFFFFFFF },      /* Harpoon 2 HBA: ASC-29320LP         */
   { 0x801E9005, 0xFFFFFFFF },      /* Harpoon 1A4 ASIC: AIC-7901A,       */
                                    /*   repackaged Harpoon 2A4           */

   /* Contigency IDs for Harpoon 2 Rev B if it is not backward compatible */
   /* to H2A4 software */
   { 0x801D9005, 0xFFFFFFFF },      /* Harpoon 2 ASIC: AIC-7902           */
   { 0x801C9005, 0xFFFFFFFF },      /* Harpoon 2 HBA: ASC-39320D          */
   { 0x80159005, 0xFFFFFFFF },      /* Harpoon 2 HBA: ASC-39320B          */

   /* ID for Harpoon 1 Rev B                                              */
   { 0x800F9005, 0xFFFFFFFF },      /* Harpoon 1 ASIC: AIC-7901           */
   { 0x80009005, 0xFFFFFFFF },      /* Harpoon 1 HBA: ASC-29320A          */
   { 0x80179005, 0xFFFFFFFF },      /* Harpoon 1 HBA: ASC-29320ALP        */

#endif /* SCSI_DCH_U320_MODE */
#endif /* SCSI_AICU320 */
   { 0x0, 0x0 }            /* This initializer serves as the delimeter id for */
                           /* the scsi host type.  And for C syntax, the last */
                           /* initializer of the initializer-list does not    */
                           /* need to have a comma.                           */
};

/* Sub System Sub Vendor ID's for Harpoon type that we want to exclude  */
/* for support.  For Harpoon device, the subsystem ID is programable    */
/* according to the new PCI device ID spec.  This subsystem ID value    */
/* can be various combination depending on the feature of the hardware. */
/* We use the following table to filter out a device in which we don't  */
/* want to support this hardware.  Note that the extern reference to    */
/* this array must be updated whenever adding an id not to support here.*/
SCSI_HOST_TYPE SCSIU320SubSystemSubVendorType[] =
{
#if (!SCSI_OEM1_SUPPORT)
   { 0x00000E11, 0x0000FFFF },                  /* Exclude OEM1's SubVendor ID*/
#endif /* (!SCSI_OEM1_SUPPORT) */
   {        0x0,        0x0 }                   /* delimeter id         */
};

/* Macros to identify which device vendor id's require the subsystem id */
/* double checking above. */

/***************************************************************************
*  CHIM SCSI interface function pointer table setup
***************************************************************************/
HIM_FUNC_PTRS SCSIFuncPtrs =
{
   HIM_VERSION_FUNC_PTRS,                                 /* version number */
   SCSIGetNextHostDeviceType,
   SCSICreateInitializationTSCB,
   SCSIGetConfiguration,
   SCSISetConfiguration,
   SCSISizeAdapterTSCB,
   SCSICreateAdapterTSCB,
   SCSISetupAdapterTSCB,
   SCSICheckMemoryNeeded,
   SCSISetMemoryPointer,
   SCSIVerifyAdapter,
   SCSIInitialize,
   SCSISizeTargetTSCB,
   SCSICheckTargetTSCBNeeded,
   SCSICreateTargetTSCB,
   SCSIDisableIRQ,
   SCSIEnableIRQ,
   SCSIPollIRQ,
   SCSIFrontEndISR,
   SCSIBackEndISR,
   SCSIQueueIOB,
   SCSIPowerEvent,
   SCSIValidateTargetTSH,
   SCSIClearTargetTSH,
   SCSISaveState,
   SCSIRestoreState,
   SCSIProfileAdapter,
   SCSIReportAdjustableAdapterProfile,
   SCSIAdjustAdapterProfile,
   SCSIProfileTarget,
   SCSIReportAdjustableTargetProfile,
   SCSIAdjustTargetProfile,
   SCSIGetNVSize,
   SCSIGetNVOSMSegment,
   SCSIPutNVData,
   SCSIGetNVData,
   SCSIProfileNexus,
   SCSIClearNexusTSH,
   SCSIProfileNode,
   SCSIReportAdjustableNodeProfile,
   SCSIAdjustNodeProfile,
   SCSISetOSMNodeContext
};

/***************************************************************************
*  Default configuration setup
***************************************************************************/
HIM_CONFIGURATION SCSIConfiguration =
{
   5,                                     /* versionNumber              */
   SCSI_MAXSCBS,                          /* maxInternalIOBlocks        */
   SCSI_MAXDEV,                           /* maxTargets                 */
   0xFFFFFFFF,                            /* maxSGDescriptors           */
   0xFFFFFFFF,                            /* maxTransferSize            */
   HIM_IOSPACE,                           /* memoryMapped               */
#if SCSI_MULTIPLEID_SUPPORT
   HIM_MAX_SCSI_ADAPTER_IDS,              /* targetNumIDs               */    
#else
   1,                                     /* targetNumIDs               */
#endif /* SCSI_MULTIPLEID_SUPPORT */
#if SCSI_TARGET_OPERATION
   SCSI_MAXNEXUSHANDLES,                  /* targetNumNexusTaskSetHandles */
   SCSI_MAXNODES,                         /* targetNumNodeTaskSetHandles  */
#else
   0,                                     /* targetNumNexusTaskSetHandles */
   0,                                     /* targetNumNodeTaskSetHandles  */
#endif /* SCSI_TARGET_OPERATION */ 
#if SCSI_INITIATOR_OPERATION
   HIM_TRUE,                              /* initiatorMode              */
#else
   HIM_FALSE,                             /* initiatorMode              */
#endif /* SCSI_INITIATOR_OPERATION */
#if SCSI_TARGET_OPERATION
   HIM_TRUE,                              /* targetMode                 */
#else
   HIM_FALSE,                             /* targetMode                 */
#endif /* SCSI_TARGET_OPERATION */
   sizeof(SCSI_IOB_RESERVE),              /* iobReserveSize             */
   sizeof(SCSI_STATE),                    /* State Size                 */
   2,                                     /* maxIOHandles               */
   HIM_TRUE,                              /* virtualDataAccess          */
   HIM_TRUE,                              /* needPhysicalAddr           */
   HIM_TRUE,                              /* busMaster                  */
#if (OSM_BUS_ADDRESS_SIZE == 32)
   32                                     /* allocBusAddressSize        */
#else
   64                                     /* allocBusAddressSize        */
#endif /* (OSM_BUS_ADDRESS_SIZE == 32) */
};

/***************************************************************************
*  Default adapter profile setup
***************************************************************************/
HIM_ADAPTER_PROFILE SCSIDefaultAdapterProfile =
{
   HIM_VERSION_ADAPTER_PROFILE,           /* AP_Version                 */
   HIM_TRANSPORT_SCSI,                    /* AP_Transport               */
   {
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    /* AP_WorldWideID             */
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
   },
   HIM_MAX_WWID_SIZE,                     /* AP_WWIDLength              */
   1,                                     /* AP_NumBuses                */
   HIM_TRUE,                              /* AP_VirtualDataAccess       */
   HIM_TRUE,                              /* AP_NeedPhysicalAddr        */
   HIM_TRUE,                              /* AP_BusMaster               */
   64-1,                                  /* AP_AlignmentMask           */
   OSM_BUS_ADDRESS_SIZE,                  /* AP_AddressableRange        */
   0,                                     /* AP_GroupNum                */
   15,                                    /* AP_AutoConfigTimeout       */
   2,                                     /* AP_MaxIOHandles            */
#if SCSI_TARGET_OPERATION
   HIM_TRUE,                              /* AP_TargetMode              */
#else
   HIM_FALSE,                             /* AP_TargetMode              */
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_INITIATOR_OPERATION
   HIM_TRUE,                              /* AP_InitiatorMode           */
#else
   HIM_FALSE,                             /* AP_InitiatorMode           */
#endif /* SCSI_INITIATOR_OPERATION */
   HIM_TRUE,                              /* AP_CleanSG                 */
   HIM_FALSE,                             /* AP_Graphing                */
   HIM_TRUE,                              /* AP_CannotCross4G           */
   HIM_FALSE,                             /* AP_BiosActive              */
   HIM_BV_FORMAT_UNKNOWN,                 /* AP_BiosVersionFormat       */
   {
      {
         {
            0,                            /* AP_MajorNumber             */
            0,                            /* AP_MinorNumber             */ 
            0                             /* AP_SubMinorNumber          */
         }
      }   
   },   
   HIM_FALSE,                             /* AP_CacheLineStreaming      */
   HIM_EXTENDED_TRANS,                    /* AP_ExtendedTrans           */
   HIM_IOSPACE,                           /* AP_MemoryMapped            */
   SCSI_MAXDEV,                           /* AP_MaxTargets              */
   SCSI_MAXSCBS,                          /* AP_MaxInternalIOBlocks     */
   0xFFFFFFFF,                            /* AP_MaxSGDescriptors        */
   0xFFFFFFFF,                            /* AP_MaxTransferSize         */
   sizeof(SCSI_STATE),                    /* AP_StateSize               */
   sizeof(SCSI_IOB_RESERVE),              /* AP_IOBReserveSize          */
   HIM_TRUE,                              /* AP_FIFOSeparateRWThreshold */
   HIM_FALSE,                             /* AP_FIFOSeparateRWThresholdEnable*/
   100,                                   /* AP_FIFOWriteThreshold      */
   100,                                   /* AP_FIFOReadThreshold       */
   100,                                   /* AP_FIFOThreshold           */
   SCSI_RESET_DELAY_DEFAULT,              /* AP_ResetDelay              */
   0,                                     /* AP_HIMVersion              */
   0,                                     /* AP_HardwareVersion         */
   0,                                     /* AP_HardwareVariant         */
   0,                                     /* AP_LowestScanTarget        */
   32,                                    /* AP_AllocBusAddressSize     */
   0,                                     /* AP_indexWithinGroup        */
   HIM_FALSE,                             /* AP_CmdCompleteIntrThresholdSupport*/
   HIM_TRUE,                              /* AP_SaveRestoreSequencer    */
   HIM_FALSE,                             /* AP_ClusterEnabled          */  
   HIM_TRUE,                              /* AP_InitializeIOBus         */
   HIM_FALSE,                             /* AP_OverrideOSMNVRAMRoutines*/
   HIM_FALSE,                             /* AP_SGBusAddress32          */
   SCSI_CHANNEL_TIMEOUT_DEFAULT,          /* AP_IOChannelFailureTimeout */
   HIM_TRUE,                              /* AP_ClearConfigurationStatus    */
   0,                                     /* AP_TargetNumNexusTaskSetHandles  */
   0,                                     /* AP_TargetNumNodeTaskSetHandles   */
   HIM_TRUE,                              /* AP_TargetDisconnectAllowed       */
   HIM_TRUE,                              /* AP_TargetTagEnable               */
   HIM_FALSE,                             /* AP_OutOfOrderTransfers           */
   0,                                     /* AP_NexusHandleThreshold          */
   0,                                     /* AP_EC_IOBThreshold               */
   0,                                     /* AP_TargetAvailableEC_IOBCount    */
   0,                                     /* AP_TargetAvailableNexusCount     */
   {
      HIM_TRUE,                           /* AP_SCSITargetAbortTask           */
      HIM_TRUE,                           /* AP_SCSITargetClearTaskSet        */
      HIM_FALSE,                          /* AP_SCSITargetTerminateTask       */
      HIM_FALSE,                          /* AP_SCSI3TargetClearACA           */
      HIM_FALSE                           /* AP_SCSI3TargetLogicalUnitReset   */
   },
#if SCSI_TARGET_OPERATION
   1,                                     /* AP_TargetNumIDs            */   
#else
   0,                                     /* AP_TargetNumIDs            */
#endif /* SCSI_TARGET_OPERATION */
   0,                                     /* AP_TargetInternalEstablishConnectionIOBlocks */
   {
      {
         0,                                  /* AP_SCSIForceWide           */
         0,                                  /* AP_SCSIForceNoWide         */
         0,                                  /* AP_SCSIForceSynch          */
         0,                                  /* AP_SCSIForceNoSynch        */
         7,                                  /* AP_SCSIAdapterID           */
         HIM_SCSI_NORMAL_SPEED,              /* AP_SCSISpeed               */
         8,                                  /* AP_SCSIWidth               */
         {                                   /* AP_SCSINumberLuns[32]      */
            SCSI_MAXLUN,SCSI_MAXLUN,SCSI_MAXLUN,SCSI_MAXLUN,
            SCSI_MAXLUN,SCSI_MAXLUN,SCSI_MAXLUN,SCSI_MAXLUN,
            SCSI_MAXLUN,SCSI_MAXLUN,SCSI_MAXLUN,SCSI_MAXLUN,
            SCSI_MAXLUN,SCSI_MAXLUN,SCSI_MAXLUN,SCSI_MAXLUN,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
         },                        
         HIM_FALSE,                          /* AP_SCSIDisableParityErrors */
         256,                                /* AP_SCSISelectionTimeout    */
         HIM_SCSI_UNKNOWN_MODE,              /* AP_SCSITransceiverMode     */
         HIM_SCSI_DISABLE_DOMAIN_VALIDATION, /* AP_SCSIDomainValidationMethod */
         0,                                  /* AP_SCSIDomainValidationIDMask */
         HIM_FALSE,                          /* AP_SCSIOSMResetBusUseThrottledDownSpeedforDV  */
#if SCSI_PPR_ENABLE
         HIM_TRUE,                           /* AP_SCSIPPRSupport          */                  
#else
         HIM_FALSE,                          /* AP_SCSIPPRSupport          */                  
#endif /* SCSI_PPR_ENABLE */
         HIM_TRUE,                           /* AP_SCSIQASSupport          */
#if SCSI_PACKETIZED_IO_SUPPORT
         HIM_TRUE,                           /* AP_SCSIIUSupport           */
         HIM_FALSE,                          /* AP_SCSIRTISupport          */
         HIM_TRUE,                           /* AP_SCSIWriteFlowSupport    */
         HIM_TRUE,                           /* AP_SCSIReadStreamingSupport */
         HIM_TRUE,                           /* AP_SCSIPreCompSupport      */
         HIM_TRUE,                           /* AP_SCSIHoldMCSSupport      */
#else
         HIM_FALSE,                          /* AP_SCSIIUSupport           */
         HIM_FALSE,                          /* AP_SCSIRTISupport          */
         HIM_FALSE,                          /* AP_SCSIWriteFlowSupport    */
         HIM_FALSE,                          /* AP_SCSIReadStreamingSupport */
         HIM_FALSE,                          /* AP_SCSIPreCompSupport      */
         HIM_FALSE,                          /* AP_SCSIHoldMCSSupport      */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_PPR_ENABLE
         HIM_SCSI_ST_DT_CLOCKING,            /* AP_SCSITransitionClocking  */ 
#else
         HIM_SCSI_ST_CLOCKING,               /* AP_SCSITransitionClocking  */ 
#endif /* SCSI_PPR_ENABLE */
         HIM_FALSE,                          /* AP_SCSIExpanderDetection   */
         HIM_SCSI_INVALID_TERMINATION,       /* AP_SCSICurrentSensingStat_PL */
         HIM_SCSI_INVALID_TERMINATION,       /* AP_SCSICurrentSensingStat_PH */
         HIM_SCSI_INVALID_TERMINATION,       /* AP_SCSICurrentSensingStat_SL */
         HIM_SCSI_INVALID_TERMINATION,       /* AP_SCSICurrentSensingStat_SH */
         HIM_FALSE,                          /* AP_SCSISuppressNegotiationWithSaveStateRestoreState */
         HIM_FALSE,                          /* AP_SCSIForceSTPWLEVELtoOneForEmbeddedChips */
#if SCSI_SELTO_PER_IOB
         HIM_TRUE,                           /* AP_SCSISelectionTimeoutPerIOB */         
#else
         HIM_FALSE,                          /* AP_SCSISelectionTimeoutPerIOB */         
#endif /* SCSI_SELTO_PER_IOB */
/* Target mode specific fields */
         HIM_SCSI_2,                         /* AP_SCSIHostTargetVersion      */
         HIM_TRUE,                           /* AP_SCSI2_IdentifyMsgRsv       */
         HIM_FALSE,                          /* AP_SCSI2_TargetRejectLuntar   */
         12,                                 /* AP_SCSIGroup6CDBSize          */
         12,                                 /* AP_SCSIGroup7CDBSize          */
         HIM_TRUE,                           /* AP_SCSITargetIgnoreWideResidue */
         HIM_FALSE,                          /* AP_SCSITargetEnableSCSI1Selection */
         HIM_FALSE,                          /* AP_SCSITargetInitNegotiation  */
         200,                                /* AP_SCSITargetMaxSpeed         */
         200,                                /* AP_SCSITargetDefaultSpeed     */
         15,                                 /* AP_SCSITargetMaxOffset        */
         15,                                 /* AP_SCSITargetDefaultOffset    */
         16,                                 /* AP_SCSITargetMaxWidth         */
         16,                                 /* AP_SCSITargetDefaultWidth     */
#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
         (HIM_SCSI_QAS_REQ | HIM_SCSI_DT_REQ | /* AP_SCSITargetMaxProtocolOptionMask */
          HIM_SCSI_IU_REQ | HIM_SCSI_RTI | HIM_SCSI_PCOMP_EN),
         (HIM_SCSI_QAS_REQ | HIM_SCSI_DT_REQ | /* AP_SCSITargetDefaultProtocolOptionMask */
          HIM_SCSI_IU_REQ | HIM_SCSI_RTI | HIM_SCSI_PCOMP_EN),
#else
         (HIM_SCSI_QAS_REQ | HIM_SCSI_DT_REQ),/* AP_SCSITargetMaxProtocolOptionMask */
         (HIM_SCSI_QAS_REQ | HIM_SCSI_DT_REQ),/* AP_SCSITargetDefaultProtocolOptionMask */
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
         0,                                  /* AP_SCSITargetAdapterIDMask    */
         0,                                  /* AP_SCSITargetDGCRCInterval    */
         0,                                  /* AP_SCSITargetIUCRCInterval    */
         0,                                  /* AP_SCSIMaxSlewRate */
         0,                                  /* AP_SCSISlewRate */
         0,                                  /* AP_SCSIPrecompLevel */
         0,                                  /* AP_SCSIMaxAmplitudeLevel */
         0,                                  /* AP_SCSIAmplitudeLevel */
         0,                                  /* AP_SCSIMaxWriteBiasControl */
         0                                   /* AP_SCSIWriteBiasControl */

      }
   },
   {
      {
         {
            0                                /* AP_SCSIDisconnectDelay        */
         }
      }
   }
};

/***************************************************************************
*  Adjustable adapter profile mask setup
***************************************************************************/
HIM_ADAPTER_PROFILE SCSIAdjustableAdapterProfile =
{
   0,                                     /* AP_Version                 */
   0,                                     /* AP_Transport               */
   {
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    /* AP_WorldWideID             */
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
   },
   0,                                     /* AP_WWIDLength              */
   0,                                     /* AP_NumBuses                */
   HIM_FALSE,                             /* AP_VirtualDataAccess       */
   HIM_FALSE,                             /* AP_NeedPhysicalAddr        */
   HIM_FALSE,                             /* AP_BusMaster               */
   0,                                     /* AP_AlignmentMask           */
   0,                                     /* AP_AddressableRange        */
   0,                                     /* AP_GroupNum                */
   0,                                     /* AP_AutoConfigTimeout       */
   0,                                     /* AP_MaxIOHandles            */
   HIM_FALSE,                             /* AP_TargetMode              */
   HIM_FALSE,                             /* AP_InitiatorMode           */
   HIM_FALSE,                             /* AP_CleanSG                 */
   HIM_FALSE,                             /* AP_Graphing                */
   HIM_FALSE,                             /* AP_CannotCross4G           */
   HIM_FALSE,                             /* AP_BiosActive              */
   0,                                     /* AP_BiosVersionFormat       */
   {
      {
         {
            0,                            /* AP_MajorNumber             */
            0,                            /* AP_MinorNumber             */ 
            0                             /* AP_SubMinorNumber          */
         }
      }   
   },
   HIM_TRUE,                              /* AP_CacheLineStreaming      */
   0,                                     /* AP_ExtendedTrans           */
   0,                                     /* AP_MemoryMapped            */
   0,                                     /* AP_MaxTargets              */
   0,                                     /* AP_MaxInternalIOBlocks     */
   0,                                     /* AP_MaxSGDescriptors        */
   0,                                     /* AP_MaxTransferSize         */
   0,                                     /* AP_StateSize               */
   0,                                     /* AP_IOBReserveSize          */
   HIM_FALSE,                             /* AP_FIFOSeparateRWThreshold */
   HIM_TRUE,                              /* AP_FIFOSeparateRWThresholdEnable */
   1,                                     /* AP_FIFOWriteThreshold      */
   1,                                     /* AP_FIFOReadThreshold       */ 
   1,                                     /* AP_FIFOThreshold           */
   1,                                     /* AP_ResetDelay              */
   0,                                     /* AP_HIMVersion              */
   0,                                     /* AP_HardwareVersion         */
   0,                                     /* AP_HardwareVariant         */
   0,                                     /* AP_LowestScanTarget        */
   0,                                     /* AP_AllocBusAddressSize     */
   0,                                     /* AP_indexWithinGroup        */
   HIM_TRUE,                              /* AP_CmdCompleteIntrThresholdSupport*/
   HIM_TRUE,                              /* AP_SaveRestoreSequencer    */
   HIM_FALSE,                             /* AP_ClusterEnabled          */
   HIM_TRUE,                              /* AP_InitializeIOBus;        */
   HIM_TRUE,                              /* AP_OverrideOSMNVRAMRoutines*/
#if SCSI_STANDARD_U320_MODE
   HIM_TRUE,                              /* AP_SGBusAddress32          */
#else
   HIM_FALSE,                             /* AP_SGBusAddress32          */
#endif /* SCSI_STANDARD_U320_MODE */
   1,                                     /* AP_IOChannelFailureTimeout */
   HIM_TRUE,                              /* AP_ClearConfigurationStatus    */
   0,                                     /* AP_TargetNumNexusTaskSetHandles  */
   0,                                     /* AP_TargetNumNodeTaskSetHandles   */
   HIM_TRUE,                              /* AP_TargetDisconnectAllowed       */
   HIM_TRUE,                              /* AP_TargetTagEnable               */
   HIM_FALSE,                             /* AP_OutOfOrderTransfers           */
   0,                                     /* AP_NexusHandleThreshold          */
   0,                                     /* AP_EC_IOBThreshold               */
   0,                                     /* AP_TargetAvailableEC_IOBCount    */
   0,                                     /* AP_TargetAvailableNexusCount     */
   {
      HIM_TRUE,                           /* AP_SCSITargetAbortTask           */
      HIM_TRUE,                           /* AP_SCSITargetClearTaskSet        */
      HIM_TRUE,                           /* AP_SCSITargetTerminateTask       */
      HIM_TRUE,                           /* AP_SCSI3TargetClearACA           */
      HIM_TRUE                            /* AP_SCSI3TargetLogicalUnitReset   */
   },
   0,                                     /* AP_TargetNumIDs            */
   1,                                     /* AP_TargetInternalEstablishConnectionIOBlocks */
   {
      {
         0,                               /* AP_SCSIForceWide           */
         0,                               /* AP_SCSIForceNoWide         */
         0,                               /* AP_SCSIForceSynch          */
         0,                               /* AP_SCSIForceNoSynch        */
         1,                               /* AP_SCSIAdapterID           */
         0,                               /* AP_SCSISpeed               */
         0,                               /* AP_SCSIWidth               */
         {                                /* AP_SCSINumberLuns[32]      */
            1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
         },                        
         HIM_TRUE,                        /* AP_SCSIDisableParityErrors */
         1,                               /* AP_SCSISelectionTimeout    */
         0,                               /* AP_SCSITransceiverMode     */ 
#if SCSI_DOMAIN_VALIDATION
         1,                               /* AP_SCSIDomainValidationMethod */
         1,                               /* AP_SCSIDomainValidationIDMask */
         HIM_TRUE,                        /* AP_SCSIOSMResetBusUseThrottledDownSpeedforDV  */
#else
         0,                               /* AP_SCSIDomainValidationMethod */
         0,                               /* AP_SCSIDomainValidationIDMask */
         HIM_FALSE,                       /* AP_SCSIOSMResetBusUseThrottledDownSpeedforDV  */
#endif /* SCSI_DOMAIN_VALIDATION */
         HIM_FALSE,                       /* AP_SCSIPPRSupport          */
         HIM_FALSE,                       /* AP_SCSIQASSuport           */
         HIM_FALSE,                       /* AP_SCSIIUSupport           */
         HIM_FALSE,                       /* AP_SCSIRTISupport          */
         HIM_FALSE,                       /* AP_SCSIWriteFlowSupport    */
         HIM_FALSE,                       /* AP_SCSIReadStreamingSupport */
         HIM_FALSE,                       /* AP_SCSIPreCompSupport      */
         HIM_FALSE,                       /* AP_SCSIHoldMCSSupport      */
         0,                               /* AP_SCSITransitionClocking  */                  
         HIM_FALSE,                       /* AP_SCSIExpanderDetection   */
         0,                               /* AP_SCSICurrentSensingStat_PL */
         0,                               /* AP_SCSICurrentSensingStat_PH */
         0,                               /* AP_SCSICurrentSensingStat_SL */
         0,                               /* AP_SCSICurrentSensingStat_SH */
         HIM_TRUE,                        /* AP_SCSISuppressNegotiationWithSaveStateRestoreState */
         HIM_TRUE,                        /* AP_SCSIForceSTPWLEVELtoOneForEmbeddedChips */
         HIM_FALSE,                       /* AP_SCSISelectionTimeoutPerIOB */                  
         1,                               /* AP_SCSIHostTargetVersion   */
         HIM_TRUE,                        /* AP_SCSI2_IdentifyMsgRsv    */
         HIM_TRUE,                        /* AP_SCSI2_TargetRejectLuntar*/
         1,                               /* AP_SCSIGroup6CDBSize       */
         1,                               /* AP_SCSIGroup7CDBSize       */
         HIM_TRUE,                        /* AP_SCSITargetIgnoreWideResidue */
         HIM_TRUE,                        /* AP_SCSITargetEnableSCSI1Selection */
         HIM_FALSE,                       /* AP_SCSITargetInitNegotiation  */
         0,                               /* AP_SCSITargetMaxSpeed         */
         0,                               /* AP_SCSITargetDefaultSpeed     */
         0,                               /* AP_SCSITargetMaxOffset        */
         0,                               /* AP_SCSITargetDefaultOffset    */
         0,                               /* AP_SCSITargetMaxWidth         */
         0,                               /* AP_SCSITargetDefaultWidth     */
         0,                               /* AP_SCSITargetMaxProtocolOptionMask */ 
         0,                               /* AP_SCSITargetDefaultProtocolOptionMask */ 
#if SCSI_MULTIPLEID_SUPPORT
         1,                               /* AP_SCSITargetAdapterIDMask    */
#else
         0,                               /* AP_SCSITargetAdapterIDMask    */
#endif /* SCSI_MULTIPLEID_SUPPORT */
         1,                               /* AP_SCSITargetDGCRCInterval    */
         0,                               /* AP_SCSITargetIUCRCInterval    */
         0,                               /* AP_SCSIMaxSlewRate */
         0,                               /* AP_SCSISlewRate */
         0,                               /* AP_SCSIPrecompLevel */
         0,                               /* AP_SCSIMaxAmplitudeLevel */
         0,                               /* AP_SCSIAmplitudeLevel */
         0,                               /* AP_SCSIMaxWriteBiasControl */
         0                                /* AP_SCSIWriteBiasControl */

      }
   },
   {
      {
         {
            0                             /* AP_SCSIDisconnectDelay        */
         }
      }
   }
};

/***************************************************************************
*  Default target profile mask setup
***************************************************************************/
HIM_TARGET_PROFILE SCSIDefaultTargetProfile =
{
   HIM_VERSION_TARGET_PROFILE,      /* TP_Version                    */
   HIM_TRANSPORT_SCSI,              /* TP_Transport                  */
   HIM_PROTOCOL_SCSI,               /* TP_Protocol                   */
   {                                /* TP_WorldWideID                */
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
   },
   HIM_MAX_WWID_SIZE,               /* TP_WWIDLength                 */
   0,                               /* TP_ScanOrder                  */
   0,                               /* TP_BusNumber                  */
   0,                               /* TP_SortMethod                 */
   2,                               /* TP_MaxActiveCommands          */
   HIM_TRUE,                        /* TP_MaxActiveShared            */
   HIM_FALSE,                       /* TP_TaggedQueuing              */
   HIM_TRUE,                        /* TP_HostManaged                */
   {
      {
         0,                         /* TP_SCSI_ID                    */
         0,                         /* TP_SCSILun                    */
         HIM_SCAM_INTOLERANT,       /* TP_SCSIScamSupport            */
         200,                       /* TP_SCSIMaxSpeed               */
         200,                       /* TP_SCSIDefaultSpeed           */
         0,                         /* TP_SCSICurrentSpeed           */
         15,                        /* TP_SCSIMaxOffset              */
         15,                        /* TP_SCSIDefaultOffset          */
         0,                         /* TP_SCSICurrentOffset          */
         16,                        /* TP_SCSIMaxWidth               */
         8,                         /* TP_SCSIDefaultWidth           */
         8,                         /* TP_SCSICurrentWidth           */
         HIM_TRUE,                  /* TP_SCSIDisconnectAllowed      */
         HIM_SCSI_DISABLE_DOMAIN_VALIDATION, /* TP_SCSIDomainValidationMethod */
         HIM_FALSE,                 /* TP_SCSIDomainValidationFallBack */
         HIM_FALSE,                 /* TP_SCSIQASSupport */
         HIM_FALSE,                 /* TP_SCSIIUSupport */
         HIM_SCSI_ST_CLOCKING,      /* TP_SCSITransitionClocking     */
         HIM_SCSI_NO_PROTOCOL_OPTION,     /* TP_SCSIDefaultProtocolOption */
         HIM_SCSI_PROTOCOL_OPTION_UNKNOWN & 0xFF, /* TP_SCSICurrentProtocolOption */
#if SCSI_PROTOCOL_OPTION_MASK_ENABLE/* Enable profile protocol options from mask */
         HIM_TRUE,                  /* TP_SCSIProtocolOptionMaskEnable */  
#else
         HIM_FALSE,                 /* TP_SCSIProtocolOptionMaskEnable */  
#endif /* SCSI_PROTOCOL_OPTION_MASK_ENABLE */
         0,                         /* TP_SCSIDefaultProtocolOptionMask*/
         HIM_SCSI_PROTOCOL_OPTION_UNKNOWN,   /* TP_SCSICurrentProtocolOptionMask */
#if SCSI_PACKETIZED_IO_SUPPORT
         HIM_SCSI_MAXPROTOCOL_MASK_DEFAULT,  /* TP_SCSIMaxProtocolOptionMask  */
#else         
         HIM_SCSI_DT_REQ,           /* TP_SCSIMaxProtocolOptionMask  */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */        
         HIM_FALSE,                  /* TP_SCSIAdapterPreCompEnabled  */
         HIM_FALSE,                  /* TP_SCSIConnectedViaExpander   */
         0,                          /* TP_SCSIMaxSlewRate */
         0,                          /* TP_SCSISlewRate */
         0,                          /* TP_SCSIPrecompLevel */
         0,                          /* TP_SCSIMaxAmplitudeLevel */
         0                           /* TP_SCSIAmplitudeLevel */

      }
   }
};

/***************************************************************************
*  Adjust target profile mask setup
***************************************************************************/
HIM_TARGET_PROFILE SCSIAdjustableTargetProfile =
{
   0,                               /* TP_Version                    */
   0,                               /* TP_Transport                  */
   0,                               /* TP_Protocol                   */
   {                                /* TP_WorldWideID                */
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
   },
   0,                               /* TP_WWIDLength                 */
   0,                               /* TP_ScanOrder                  */
   0,                               /* TP_BusNumber                  */
   0,                               /* TP_SortMethod                 */
   1,                               /* TP_MaxActiveCommands          */
   HIM_FALSE,                       /* TP_MaxActiveShared            */
   HIM_TRUE,                        /* TP_TaggedQueuing              */
   HIM_FALSE,                       /* TP_HostManaged                */ 
   {
      {
         0,                         /* TP_SCSI_ID                    */
         0,                         /* TP_SCSILun                    */
         0,                         /* TP_SCSIScamSupport            */
         0,                         /* TP_SCSIMaxSpeed               */
         1,                         /* TP_SCSIDefaultSpeed           */
         0,                         /* TP_SCSICurrentSpeed           */
         0,                         /* TP_SCSIMaxOffset              */
         1,                         /* TP_SCSIDefaultOffset          */
         0,                         /* TP_SCSICurrentOffset          */
         0,                         /* TP_SCSIMaxWidth               */
         1,                         /* TP_SCSIDefaultWidth           */
         0,                         /* TP_SCSICurrentWidth           */
         HIM_TRUE,                  /* TP_SCSIDisconnectAllowed      */
         0,                         /* TP_SCSIDomainValidationMethod */
         HIM_FALSE,                 /* TP_SCSIDomainValidationFallBack */
         HIM_FALSE,                 /* TP_SCSIQASSupport */
         HIM_FALSE,                 /* TP_SCSIIUSupport */ 
         0,                         /* TP_SCSITransitionClocking     */
         1,                         /* TP_SCSIDefaultProtocolOption  */
         0,                         /* TP_SCSICurrentProtocolOption  */
         HIM_TRUE,                  /* TP_SCSIProtocolOptionMaskEnable */  
         1,                         /* TP_SCSIDefaultProtocolOptionMask */
         0,                         /* TP_SCSICurrentProtocolOptionMask */
         0,                         /* TP_SCSIMaxProtocolOptionMask  */
         HIM_FALSE,                 /* TP_SCSIAdapterPreCompEnabled  */
         HIM_FALSE,                 /* TP_SCSIConnectedViaExpander   */
         0,                         /* TP_SCSIMaxSlewRate */
         1,                         /* TP_SCSISlewRate */
         1,                         /* TP_SCSIPrecompLevel */
         0,                         /* TP_SCSIMaxAmplitudeLevel */
         1                          /* TP_SCSIAmplitudeLevel */

      }
   }
};

/***************************************************************************
*  Default node profile mask setup
***************************************************************************/
HIM_NODE_PROFILE SCSIDefaultNodeProfile =
{
   1,                               /* NP_Version                    */
   HIM_TRANSPORT_SCSI,              /* NP_Transport                  */
   HIM_PROTOCOL_SCSI,               /* NP_Protocol                   */
   0,                               /* NP_BusNumber                  */
   {
      {
         0,                         /* NP_SCSI_ID                    */
         200,                       /* NP_SCSIMaxSpeed               */
         200,                       /* NP_SCSIDefaultSpeed           */
         0,                         /* NP_SCSICurrentSpeed           */
         15,                        /* NP_SCSIMaxOffset              */
         15,                        /* NP_SCSIDefaultOffset          */
         0,                         /* NP_SCSICurrentOffset          */
         16,                        /* NP_SCSIMaxWidth               */
         8,                         /* NP_SCSIDefaultWidth           */
         8,                         /* NP_SCSICurrentWidth           */
#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
         (HIM_SCSI_QAS_REQ | HIM_SCSI_DT_REQ | /* NP_SCSIMaxProtocolOptionMask */
          HIM_SCSI_IU_REQ | HIM_SCSI_RTI | HIM_SCSI_PCOMP_EN),
         (HIM_SCSI_QAS_REQ | HIM_SCSI_DT_REQ | /* NP_SCSIDefaultProtocolOptionMask */
          HIM_SCSI_IU_REQ | HIM_SCSI_RTI | HIM_SCSI_PCOMP_EN),
#else
         (HIM_SCSI_QAS_REQ | HIM_SCSI_DT_REQ), /* NP_SCSIMaxProtocolOptionMask */
         (HIM_SCSI_QAS_REQ | HIM_SCSI_DT_REQ), /* NP_SCSIDefaultProtocolOptionMask */
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
         0,                         /* NP_SCSICurrentProtocolOptionMask */
         HIM_FALSE                  /* NP_SCSIAdapterPreCompEnabled  */ 
      }
   }
};

/***************************************************************************
*  Adjust node profile mask setup
***************************************************************************/
HIM_NODE_PROFILE SCSIAdjustableNodeProfile =
{
   0,                               /* NP_Version                    */
   0,                               /* NP_Transport                  */
   0,                               /* NP_Protocol                   */
   0,                               /* NP_BusNumber                  */
   {
      {
         0,                         /* NP_SCSI_ID                    */
         0,                         /* NP_SCSIMaxSpeed               */
         0,                         /* NP_SCSIDefaultSpeed           */
         0,                         /* NP_SCSICurrentSpeed           */
         0,                         /* NP_SCSIMaxOffset              */
         0,                         /* NP_SCSIDefaultOffset          */
         0,                         /* NP_SCSICurrentOffset          */
         0,                         /* NP_SCSIMaxWidth               */
         0,                         /* NP_SCSIDefaultWidth           */
         0,                         /* NP_SCSICurrentWidth           */
         0,                         /* NP_SCSIMaxProtocolOptionMask  */
         0,                         /* NP_SCSIDefaultProtocolOptionMask */
         0,                         /* NP_SCSICurrentProtocolOptionMask */
         HIM_FALSE                  /* NP_SCSIAdapterPreCompEnabled  */
      }
   }
};

#if ((HIM_TASK_SIMPLE == 0) && (HIM_TASK_ORDERED == 1) \
      && (HIM_TASK_HEAD_OF_QUEUE == 2) && (HIM_TASK_RECOVERY == 3))
SCSI_UEXACT8 SCSIscontrol[4] =
{
   SCSI_TAGENB | SCSI_SIMPLETAG,
   SCSI_TAGENB | SCSI_ORDERTAG,
   SCSI_TAGENB | SCSI_HEADTAG,
   0
};
/* Mapping of iob->taskAttribute to SPI Command IU Task Attribute Field. */
SCSI_UEXACT8 SCSITaskAttribute[4] =
{
   SCSI_SIMPLE_TASK_ATTRIBUTE,        /* HIM_TASK_SIMPLE - Simple Task */
   SCSI_ORDERED_TASK_ATTRIBUTE,       /* HIM_TASK_ORDERED - Ordered Task */
   SCSI_HEAD_OF_QUEUE_TASK_ATTRIBUTE, /* HIM_TASK_HEAD_OF_QUEUE -
                                       *    Head of Queue Task
                                       */
   0
};
#else
      ****** Inconsistant task attribute translation table ******
#endif

#else /* !defined(SCSI_XLM_DEFINE) */

extern HIM_ADAPTER_PROFILE SCSIDefaultAdapterProfile;
extern HIM_TARGET_PROFILE SCSIDefaultTargetProfile;
extern HIM_ADAPTER_PROFILE SCSIAdjustableAdapterProfile;
extern HIM_TARGET_PROFILE SCSIAdjustableTargetProfile;
extern HIM_NODE_PROFILE SCSIAdjustableNodeProfile;
extern SCSI_UEXACT8 SCSIscontrol[4];
extern SCSI_UEXACT8 SCSITaskAttribute[4];

#if SCSI_OEM1_SUPPORT
extern SCSI_HOST_TYPE SCSIU320SubSystemSubVendorType[1];
#else /* !SCSI_OEM1_SUPPORT */
extern SCSI_HOST_TYPE SCSIU320SubSystemSubVendorType[2];
#endif /* SCSI_OEM1_SUPPORT */

#endif /* defined(SCSI_XLM_DEFINE) */

#ifdef	__cplusplus
}
#endif

#endif /* _XLMDEF_H */

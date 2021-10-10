/*$Header: /vobs/u320chim/src/chim/hwm/hwmdef.h   /main/36   Thu Mar 20 21:15:25 2003   quan $*/

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
*  Module Name:   HWMDEF.H
*
*  Description:   Definitions for hwarware management layer
*    
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         1. This file is referenced by hardware management layer
*                    and application to hardware management layer
*
***************************************************************************/

#ifndef SCSI_DOMAIN_VALIDATION   
#error SCSI_DOMAIN_VALIDATION Should be defined here
#endif

/***************************************************************************
* Function prototype as interface to hardware management layer 
***************************************************************************/
/* HWMINIT.C */
void SCSIHGetConfiguration(SCSI_HHCB SCSI_HPTR);

#if !SCSI_BIOS_SUPPORT
void SCSIHGetMemoryTable(SCSI_UEXACT8,SCSI_UEXACT16,SCSI_MEMORY_TABLE SCSI_HPTR);
SCSI_UINT8 SCSIHApplyMemoryTable(SCSI_HHCB SCSI_HPTR,SCSI_MEMORY_TABLE SCSI_HPTR);
#endif /* !SCSI_BIOS_SUPPORT */

SCSI_UINT8 SCSIHInitialize(SCSI_HHCB SCSI_HPTR);
void SCSIHSetupHardware(SCSI_HHCB SCSI_HPTR);

/* HWMTASK.C */
void SCSIHSetUnitHandle(SCSI_HHCB SCSI_HPTR,SCSI_UNIT_CONTROL SCSI_UPTR,SCSI_UEXACT8,SCSI_UEXACT8);
#if !SCSI_EFI_BIOS_SUPPORT
void SCSIHFreeUnitHandle(SCSI_UNIT_HANDLE);
#endif /* !SCSI_EFI_BIOS_SUPPORT */
void SCSIHDisableIRQ(SCSI_HHCB SCSI_HPTR);
void SCSIHEnableIRQ(SCSI_HHCB SCSI_HPTR);
SCSI_UINT8 SCSIHPollIRQ(SCSI_HHCB SCSI_HPTR);
void SCSIHQueueHIOB(SCSI_HIOB SCSI_IPTR);
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
void SCSIHQueueSpecialHIOB(SCSI_HIOB SCSI_IPTR);
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */
void SCSIHSaveState(SCSI_HHCB SCSI_HPTR,SCSI_STATE SCSI_HPTR);
void SCSIHRestoreState(SCSI_HHCB SCSI_HPTR,SCSI_STATE SCSI_HPTR);

#if !SCSI_BIOS_SUPPORT
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
SCSI_UINT8 SCSIHPowerManagement(SCSI_HHCB SCSI_HPTR,SCSI_UINT);
SCSI_UINT8 SCSIHCheckHostIdle(SCSI_HHCB SCSI_HPTR);
SCSI_UINT8 SCSIHCheckDeviceIdle(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */
#if !SCSI_BEF_SUPPORT
SCSI_UINT8 SCSIHSuppressNegotiation(SCSI_UNIT_CONTROL SCSI_UPTR);
SCSI_UINT8 SCSIHForceNegotiation(SCSI_UNIT_CONTROL SCSI_UPTR);
#endif /* !SCSI_BEF_SUPPORT */
#endif /* !SCSI_BIOS_SUPPORT */
SCSI_UINT8 SCSIHReadNVRAM(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8 SCSI_SPTR,SCSI_SINT16,SCSI_SINT16);
SCSI_UINT8 SCSIHWriteNVRAM(SCSI_HHCB SCSI_HPTR,SCSI_SINT16,SCSI_UEXACT8 SCSI_SPTR,SCSI_SINT16);
int SCSIHApplyDeviceTable(SCSI_HHCB SCSI_HPTR);
void SCSIHGetHWInformation(SCSI_HW_INFORMATION SCSI_LPTR,SCSI_HHCB SCSI_HPTR);
void SCSIHPutHWInformation(SCSI_HW_INFORMATION SCSI_LPTR,SCSI_HHCB SCSI_HPTR);
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
void SCSIHEnableExpStatus(SCSI_HHCB SCSI_HPTR);
void SCSIHDisableExpStatus(SCSI_HHCB SCSI_HPTR);
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT */
/* HWMINTR.C */                        
SCSI_UINT8 SCSIHFrontEndISR(SCSI_HHCB SCSI_HPTR);
void SCSIHBackEndISR(SCSI_HHCB SCSI_HPTR);

/* HWMSE2.C */
SCSI_UINT32 SCSIHSizeSEEPROM(SCSI_HHCB SCSI_HPTR);
SCSI_UINT8 SCSIHReadSEEPROM(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8 SCSI_SPTR,SCSI_SINT16,SCSI_SINT16);
SCSI_UINT8 SCSIHWriteSEEPROM( SCSI_HHCB SCSI_HPTR,SCSI_SINT16,SCSI_SINT16,SCSI_UEXACT8 SCSI_SPTR);
                   
/* HWMUTIL.C */
void SCSIHReadScbRAM(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8 SCSI_SPTR,SCSI_UEXACT16,SCSI_UEXACT8,SCSI_UEXACT8);
void SCSIHWriteScbRAM(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16,SCSI_UEXACT8,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_SPTR);
#if !SCSI_ASPI_REVA_SUPPORT
void SCSIHReadScratchRam(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8 SCSI_SPTR,SCSI_UEXACT16,SCSI_UEXACT16);
void SCSIHWriteScratchRam(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16,SCSI_UEXACT16,SCSI_UEXACT8 SCSI_SPTR);
#endif /* !SCSI_ASPI_REVA_SUPPORT */
#if (!ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
SCSI_INT SCSIHHardwareInResetState(SCSI_HHCB SCSI_HPTR);
#endif /* (!ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */
#if SCSI_DOMAIN_VALIDATION
SCSI_UEXACT16 SCSIHDataBusHang(SCSI_HHCB SCSI_HPTR);
#endif /* SCSI_DOMAIN_VALIDATION */

#if !SCSI_BIOS_SUPPORT 
SCSI_UINT8 SCSIHApplyNVData(SCSI_HHCB SCSI_HPTR);
SCSI_UINT8 SCSIHBiosState(SCSI_HHCB SCSI_HPTR);
void SCSIHUpdateDeviceTable(SCSI_HHCB SCSI_HPTR);
#endif /* !SCSI_BIOS_SUPPORT  */

#if (!SCSI_BIOS_SUPPORT && !SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT && !SCSI_BEF_SUPPORT)
SCSI_UINT8 SCSIHRealModeExist(SCSI_HHCB SCSI_HPTR);
#endif /* (!SCSI_BIOS_SUPPORT && !SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT && !SCSI_BEF_SUPPORT) */

#if SCSI_PACKETIZED_IO_SUPPORT
void SCSIHSetupNegoInPacketized(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

#if SCSI_TARGET_OPERATION
/* HWMTRGT.C */
void  SCSIHTargetModeDisable(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIHTargetSelInPending(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIHTargetClearNexus(SCSI_NEXUS SCSI_XPTR,SCSI_HHCB SCSI_HPTR);
#endif /* SCSI_TARGET_OPERATION */


/* RSM*.C */                     
void SCSIrCompleteHIOB(SCSI_HIOB SCSI_IPTR);
void SCSIrCompleteSpecialHIOB(SCSI_HIOB SCSI_IPTR);
void SCSIrAsyncEvent(SCSI_HHCB SCSI_HPTR,SCSI_UINT16,...);
SCSI_SCB_DESCRIPTOR SCSI_HPTR SCSIRGetFreeHead(SCSI_HHCB SCSI_HPTR);
void SCSIrAsyncEventCommand(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UINT16,...);

#if SCSI_TARGET_OPERATION
SCSI_NODE SCSI_NPTR SCSIrAllocateNode(SCSI_HHCB SCSI_HPTR);
#endif /* SCSI_TARGET_OPERATION */




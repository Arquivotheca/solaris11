/*$Header: /vobs/u320chim/src/chim/rsm/rsmdef.h   /main/7   Mon Mar 17 18:23:50 2003   quan $*/

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
*  Module Name:   RSMDEF.H
*
*  Description:   Definitions for resource management layer
*    
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         1. This file must is referenced by resource
*                    management layer and applications to resource
*                    management layer
*
***************************************************************************/

/***************************************************************************
* Function prototype for tools referenced in RSM
***************************************************************************/

/***************************************************************************
* Function prototype as interface to resource management layer 
***************************************************************************/
/* RSMINIT.C */
int SCSIRGetConfiguration(SCSI_HHCB SCSI_HPTR);
void SCSIRGetMemoryTable(SCSI_UEXACT8,SCSI_UEXACT16,SCSI_UEXACT16,SCSI_UEXACT8,SCSI_MEMORY_TABLE SCSI_HPTR);
SCSI_UINT8 SCSIRApplyMemoryTable(SCSI_HHCB SCSI_HPTR,SCSI_MEMORY_TABLE SCSI_HPTR);
SCSI_UINT8 SCSIRInitialize(SCSI_HHCB SCSI_HPTR);

/* RSMTASK.C */
void SCSIRSetUnitHandle(SCSI_HHCB SCSI_HPTR,SCSI_UNIT_CONTROL SCSI_UPTR,SCSI_UEXACT8,SCSI_UEXACT8);
void SCSIRFreeUnitHandle(SCSI_UNIT_HANDLE);
void SCSIRQueueHIOB(SCSI_HIOB SCSI_IPTR);
void SCSIRQueueSpecialHIOB(SCSI_HIOB SCSI_IPTR);
int SCSIRBackEndISR(SCSI_HHCB SCSI_HPTR);
SCSI_UINT8 SCSIRCheckHostIdle(SCSI_HHCB SCSI_HPTR);
SCSI_UINT8 SCSIRCheckDeviceIdle(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UINT8 SCSIRPowerManagement(SCSI_HHCB SCSI_HPTR,SCSI_UINT);
void SCSIRGetHWInformation(SCSI_HW_INFORMATION SCSI_LPTR,SCSI_HHCB SCSI_HPTR);
void SCSIRPutHWInformation(SCSI_HW_INFORMATION SCSI_LPTR,SCSI_HHCB SCSI_HPTR);
void SCSIrResetHardware(SCSI_HIOB SCSI_IPTR);
SCSI_HIOB SCSI_IPTR SCSIRSearchDoneQForDVRequest(SCSI_HHCB SCSI_HPTR);
                                 
                        
                        
                              
                              
                           
                        



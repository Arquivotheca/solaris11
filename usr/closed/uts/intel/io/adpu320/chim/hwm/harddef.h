/*$Header: /vobs/u320chim/src/chim/hwm/harddef.h   /main/21   Mon Mar 17 18:22:35 2003   quan $*/

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
*  Module Name:   HARDDEF.H
*
*  Description:   Definitions for hardware device definitions which
*                 are devices specific (e.g. cross Lance, Sabre and
*                 Mace)
*    
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         
*
***************************************************************************/

#if SCSI_MULTI_HARDWARE
typedef struct SCSI_HARDWARE_DESCRIPTOR_
{  
   
   void (*SCSIhProcessAutoTerm)(SCSI_HHCB SCSI_HPTR); /* process auto termination */  
   void (*SCSIhUpdateExtTerm)(SCSI_HHCB SCSI_HPTR);   /* update external termination */
   SCSI_UEXACT8 (*SCSIhGetCurrentSensing)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
   SCSI_UEXACT8 (*SCSIhSetDataFifoThrshDefault)(SCSI_HHCB SCSI_HPTR);/* set default fifo threshold */ 
   SCSI_UEXACT8 (*SCSIhGetDataFifoThrsh)(SCSI_HHCB SCSI_HPTR); /* get fifo threshold */
   void (*SCSIhUpdateDataFifoThrsh)(SCSI_HHCB SCSI_HPTR);   /* update fifo threshold */   
   SCSI_UEXACT8 (*SCSIhGetCacheThEn)(SCSI_HHCB SCSI_HPTR);  /* get cachethen */
   void (*SCSIhUpdateCacheThEn)(SCSI_HHCB SCSI_HPTR); /* update cachethen */
   void (*SCSIhGetProfileParameters)(SCSI_HW_INFORMATION SCSI_LPTR,SCSI_HHCB SCSI_HPTR); /* get profile parameters from hwm layer into hw_info structure */
   void (*SCSIhPutProfileParameters)(SCSI_HW_INFORMATION SCSI_LPTR,SCSI_HHCB SCSI_HPTR); /* put profile parameters from hw_info structure into hwm layer */
   void (*SCSIhCheckFlexPortAccess)(SCSI_HHCB SCSI_HPTR);   /* check flex port access */
   SCSI_UEXACT8 (*SCSIhWaitForBusStable)(SCSI_HHCB SCSI_HPTR); /* wait for SCSI bus stable */
   void (*SCSIhTargetLoadTargetIds)(SCSI_HHCB SCSI_HPTR); /* load Target IDs */
   void (*SCSIhResetNegoData)(SCSI_HHCB SCSI_HPTR); /* reset negodata registers */
   void (*SCSIhLoadNegoData)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR); /* load value into negodata registers */
   void (*SCSIhGetNegoData)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR); /* get value from negodata registers */
   void (*SCSIhChangeXferOption)(SCSI_HIOB SCSI_IPTR);
   SCSI_UEXACT16 (*SCSIhGetTimerCount)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT16);
} SCSI_HARDWARE_DESCRIPTOR;

/***************************************************************************
*  SCSI_HARDWARE_DESCRIPTOR for AIC-U320 devices
***************************************************************************/
#if (SCSI_AICU320 && defined(SCSI_DATA_DEFINE))
void SCSIhProcessAutoTerm(SCSI_HHCB SCSI_HPTR);
void SCSIhUpdateExtTerm(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhGetCurrentSensing320(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhSetDataFifoThrshDefault(SCSI_HHCB SCSI_HPTR);  
SCSI_UEXACT8 SCSIhGetDataFifoThrsh(SCSI_HHCB SCSI_HPTR);
void SCSIhUpdateDataFifoThrsh(SCSI_HHCB SCSI_HPTR);   
SCSI_UEXACT8 SCSIhGetCacheThEn(SCSI_HHCB SCSI_HPTR);
void SCSIhUpdateCacheThEn(SCSI_HHCB SCSI_HPTR);
void SCSIhGetProfileParameters(SCSI_HW_INFORMATION SCSI_LPTR, SCSI_HHCB SCSI_HPTR);
void SCSIhPutProfileParameters(SCSI_HW_INFORMATION SCSI_LPTR, SCSI_HHCB SCSI_HPTR);
void SCSIhCheckFlexPortAccess(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhWaitForBusStable(SCSI_HHCB SCSI_HPTR);
void SCSIhResetNegoData(SCSI_HHCB SCSI_HPTR);
void SCSIhLoadNegoData(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhGetNegoData(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhChangeXferOption(SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT32 SCSIhGetTimerCount(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT32);
SCSI_HARDWARE_DESCRIPTOR SCSIHardwareAICU320 = 
{
#if SCSI_AUTO_TERMINATION
   SCSIhProcessAutoTerm,               /* process auto termination            */
#else
   0,                                  /* process auto termination            */
#endif /* SCSI_AUTO_TERMINATION */
#if SCSI_UPDATE_TERMINATION_ENABLE
   SCSIhUpdateExtTerm,                 /* update external termination         */
#else
   0,                                  /* update external termination         */
#endif /* SCSI_UPDATE_TERMINATION_ENABLE */
#if SCSI_CURRENT_SENSING
   SCSIhGetCurrentSensing,             /* get current sensing term. status    */
#else
   0,                                  /* get current sensing term. status    */
#endif /* SCSI_CURRENT_SENSING */
   SCSIhSetDataFifoThrshDefault,       /* set data fifo default               */
   SCSIhGetDataFifoThrsh,              /* get data fifo threshold             */
   SCSIhUpdateDataFifoThrsh,           /* update data fifo threshold          */
   SCSIhGetCacheThEn,                  /* get cachethen                       */
   SCSIhUpdateCacheThEn,               /* update cachethen                    */
   SCSIhGetProfileParameters,          /* get profile parameters              */
   SCSIhPutProfileParameters,          /* put profile parameters              */
   SCSIhCheckFlExtPortAccess,          /* check flex port access              */
   SCSIhWaitForBusStable,              /* wait for SCSI bus stable            */
   0,                                  /* load TARGIDL/H registers            */
   SCSIhResetNegoData,                 /* reset negodata registers            */
   SCSIhLoadNegoData,                  /* load value into negodata registers  */
   SCSIhGetNegoData,                   /* get value from negodata registers   */
#if SCSI_NEGOTIATION_PER_IOB
   SCSIhChangeXferOption,              /* Change xfer option form IOB         */
#else
   0,                                  /* Change xfer option form IOB         */
#endif /* SCSI_NEGOTIATION_PER_IOB */
   SCSIhGetTimerCount                  /* obtain atn timer count value        */
};
#endif /* (SCSI_AICU320 && defined(SCSI_DATA_DEFINE)) */


/***************************************************************************
*  SCSI_HARDWARE_DESCRIPTOR table for indexing
***************************************************************************/
#define  SCSI_MAX_HW    1
#if  defined(SCSI_DATA_DEFINE)
SCSI_HARDWARE_DESCRIPTOR SCSI_LPTR SCSIHardware[SCSI_MAX_HW] =
{
#if SCSI_AICU320
   &SCSIHardwareAICU320
#else  /* !SCSI_AICU320 */
   (SCSI_HARDWARE_DESCRIPTOR SCSI_LPTR) 0
#endif /* SCSI_AICU320 */
};
#else
extern SCSI_HARDWARE_DESCRIPTOR SCSI_LPTR SCSIHardware[SCSI_MAX_HW];
#endif
#endif





/*$Header: /vobs/u320chim/src/chim/hwm/hwmdlvr.c   /main/78   Wed Mar 19 21:45:12 2003   spal3094 $*/

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
*  Module Name:   HWMDLVR.C
*
*  Description:
*                 Codes to implement delivery mechanism for hardware
*                 management module
*
*  Owners:        ECX IC Firmware Team
*
*  Notes:         NONE
*
***************************************************************************/

#include "scsi.h"
#include "hwm.h"

/*********************************************************************
*
*  SCSIhDeliverScb
*
*     Deliver scb for standard mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
/* @@@@ do later - additional inline code needed to do this #if !SCSI_STREAMLINE_QIOPATH */
#if SCSI_STANDARD_MODE
void SCSIhStandardDeliverScb (SCSI_HHCB SCSI_HPTR hhcb,
                              SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   /* setup scb buffer */
#if SCSI_TARGET_OPERATION
   if (hiob->SCSI_IP_targetMode)
   {
      /* setup scb buffer */
      OSD_BUILD_TARGET_SCB(hiob);
      /* Determine if an ignore wide message is required */
      if (hhcb->SCSI_HF_targetIgnoreWideResidMsg &&
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
          (!(SCSI_NEXUS_UNIT(hiob)->SCSI_XF_packetized)) &&
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
          (SCSI_DATA_IOLENGTH(hiob) & 0x1))  /* odd data transfer length? */
      {
         SCSI_hTARGETSETIGNOREWIDEMSG(hhcb,hiob);
      }
   }
   else
   {
     OSD_BUILD_SCB(hiob);
   }
#else

   OSD_BUILD_SCB(hiob);

#endif  /* SCSI_TARGET_OPERATION */

   SCSI_hSETUPATNLENGTH(hhcb,hiob);

   /* bump host new SCB queue offset */
   hhcb->SCSI_HP_hnscbQoff++;

   OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF),hhcb->SCSI_HP_hnscbQoff);
}
#endif /* SCSI_STANDARD_MODE */
/* @@@@ do later - additional inline code needed to do this #endif !SCSI_STREAMLINE_QIOPATH */

/*********************************************************************
*
*  SCSIhDownshiftDeliverScb
*
*     Deliver scb for downshift mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftDeliverScb (SCSI_HHCB SCSI_HPTR hhcb,
                               SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 downshift_hnscbQoff;

   /* setup scb buffer */
   OSD_BUILD_SCB(hiob);

   SCSI_hSETCOMMANDREQUESTOR(hhcb,hiob);

   SCSI_hSETUPATNLENGTH(hhcb,hiob);

   /* bump host new SCB queue offset for BIOS */
   SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &downshift_hnscbQoff,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_DOWNSHIFT_HNSCBQOFF),
                       (SCSI_UEXACT16) 2);

   downshift_hnscbQoff++;

   SCSIHWriteScratchRam(hhcb,
                        (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_DOWNSHIFT_HNSCBQOFF),
                        (SCSI_UEXACT16) 2,
                        (SCSI_UEXACT8 SCSI_IPTR) &downshift_hnscbQoff);

   OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF),downshift_hnscbQoff);
}
#endif /* SCSI_DOWNSHIFT_MODE */

/*********************************************************************
*
*  SCSIhStandardU320SetupAtnLength
*
*     Setup atn_length field in a scb for standard U320 sequencer
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if SCSI_STANDARD_U320_MODE
void SCSIhStandardU320SetupAtnLength (SCSI_HHCB SCSI_HPTR hhcb,
                                      SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_SCB_STANDARD_U320 SCSI_IPTR scbBuffer =
      (SCSI_SCB_STANDARD_U320 SCSI_IPTR )hiob->scbDescriptor->scbBuffer.virtualAddress;
#if SCSI_TARGET_OPERATION
   SCSI_NEXUS SCSI_XPTR nexusHandle;
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_TARGET_OPERATION
   if (hiob->SCSI_IP_targetMode)
   {
      nexusHandle = SCSI_NEXUS_UNIT(hiob);
      deviceTable = SCSI_GET_NODE(nexusHandle)->deviceTable;
   }
   else
#endif /* SCSI_TARGET_OPERATION */
   {
      deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   }

#if SCSI_LOOPBACK_OPERATION
   /* Ensure no negotiation. */
   deviceTable->SCSI_DF_needNego = 0;
#endif /* SCSI_LOOPBACK_OPERATION */

      /* IMPORTANT NOTE:                                                */
      /* THE BELOW WORKAROUND APPLIES TO HARPOON2 REV A ONLY.  IF THIS  */
      /* ROUTINE EVER COMBINES WITH STANDARD ENHANCED U320, THEN WE     */
      /* SHOULD HAVE A RUN-TIME CHECK FOR HARPOON 2 REV A OR STANDARD   */
      /* U320 FIRMWARE MODE.  THE LATER CHECK MIGHT HAVE A CONDITIONAL  */
      /* COMPILE OPTION, SCSI_STANDARD_U320_MODE, TO SAVE CODE SPACE.   */

   /****************************************************************************
    * test for REV A CHIP, Packetize active and nego needed. If true set a flag
    * the sequencer will recognize. This prevents an invalid message byte being
    * sent to a Target that is currently in a PACKETIZE mode.  Razor issue #396.
    ***************************************************************************/
   /* H2A4 WORKAROUND START */  
   
   /* This is also an indicator to not send the identify message after  */
   /* a select with attention.  In some cases, such as a bus scan, CHIM */
   /* may desire to send an identify message even if the HW has the     */
   /* packetized bit set*/
   
   if ((SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_PACKETIZED) &&
       (deviceTable->SCSI_DF_needNego))
   {
      /* Let the Rev A standard U320 Sequencer know we wish to negotiation */
      /*  while in Packetized mode */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
           OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length),0x05);

      deviceTable->SCSI_DF_needNego = 0;
   }
   /* H2A4 WORKAROUND END */
   
   /* normal behavior for non_packetize or post A0/A1 Revs */
   else
   {
      if (hiob->SCSI_IF_tagEnable)
      {
         if (deviceTable->SCSI_DF_needNego)
         {
            /* negotiation needed for tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length),0x04);

            deviceTable->SCSI_DF_needNego = 0;
         }
         else
         {
            /* negotiation not needed for tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length),0x03);
         }
      }
      else
      {
         if (deviceTable->SCSI_DF_needNego)
         {
            /* negotiation needed for non-tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length),0x02);

            deviceTable->SCSI_DF_needNego = 0;
         }
         else
         {
            /* negotiation not needed for non-tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length),0x01);
         }
      }

   }
   /* Flush the cache */
   SCSI_FLUSH_CACHE(scbBuffer, sizeof(SCSI_SCB_STANDARD_U320));
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardEnhU320SetupAtnLength
*
*     Setup atn_length field in a scb for standard enhanced U320 sequencer
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
void SCSIhStandardEnhU320SetupAtnLength (SCSI_HHCB SCSI_HPTR hhcb,
                                         SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_SCB_STANDARD_ENH_U320 SCSI_IPTR scbBuffer =
      (SCSI_SCB_STANDARD_ENH_U320 SCSI_IPTR )hiob->scbDescriptor->scbBuffer.virtualAddress;
#if SCSI_TARGET_OPERATION
   SCSI_NEXUS SCSI_XPTR nexusHandle;
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_TARGET_OPERATION
   if (hiob->SCSI_IP_targetMode)
   {
      nexusHandle = SCSI_NEXUS_UNIT(hiob);
      deviceTable = SCSI_GET_NODE(nexusHandle)->deviceTable;
   }
   else
#endif /* SCSI_TARGET_OPERATION */
   {
      deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   }

#if SCSI_LOOPBACK_OPERATION
   /* Ensure no negotiation. */
   deviceTable->SCSI_DF_needNego = 0;
#endif /* SCSI_LOOPBACK_OPERATION */
   
   /* CHIM sets atn length of 5 as an indicator to the sequencer to not */
   /* send the identify message after a select with attention.  In some */
   /* cases, such as a bus scan, CHIM may desire to send an identify    */
   /* message even if the HW has the packetized bit set. If the identify*/
   /* message is desired then CHIM will clear the SCSI_PACKETIZED bit in*/
   /* SCSI_CURRENT_XFER table.  */
   
   if ((SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_PACKETIZED) &&
       (deviceTable->SCSI_DF_needNego))
   {
      /* Let the Rev A standard U320 Sequencer know we wish to negotiation */
      /*  while in Packetized mode */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length),0x05);

      deviceTable->SCSI_DF_needNego = 0;
   }
   /* normal behavior for non_packetize or send identify message protocol*/
   else
   {
      if (hiob->SCSI_IF_tagEnable)
      {
         if (deviceTable->SCSI_DF_needNego)
         {
            /* negotiation needed for tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length),0x04);

            deviceTable->SCSI_DF_needNego = 0;
         }
         else
         {
            /* negotiation not needed for tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length),0x03);
         }
      }
      else
      {
         if (deviceTable->SCSI_DF_needNego)
         {
            /* negotiation needed for non-tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length),0x02);

            deviceTable->SCSI_DF_needNego = 0;
         }
         else
         {
            /* negotiation not needed for non-tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length),0x01);
         }
      }
   }
   
   /* Flush the cache */
   SCSI_FLUSH_CACHE(scbBuffer, sizeof(SCSI_SCB_STANDARD_ENH_U320));
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhDchU320SetupAtnLength
*
*     Setup atn_length field in a scb for DCH U320 sequencer
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320SetupAtnLength (SCSI_HHCB SCSI_HPTR hhcb,
                                      SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_SCB_DCH_U320 SCSI_IPTR scbBuffer =
      (SCSI_SCB_DCH_U320 SCSI_IPTR )hiob->scbDescriptor->scbBuffer.virtualAddress;
#if SCSI_TARGET_OPERATION
   SCSI_NEXUS SCSI_XPTR nexusHandle;
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_TARGET_OPERATION
   if (hiob->SCSI_IP_targetMode)
   {
      nexusHandle = SCSI_NEXUS_UNIT(hiob);
      deviceTable = SCSI_GET_NODE(nexusHandle)->deviceTable;
   }
   else
#endif /* SCSI_TARGET_OPERATION */
   {
      deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   }
   
   /* CHIM sets atn length of 5 as an indicator to the sequencer to not */
   /* send the identify message after a select with attention.  In some */
   /* cases, such as a bus scan, CHIM may desire to send an identify    */
   /* message even if the HW has the packetized bit set. If the identify*/
   /* message is desired then CHIM will clear the SCSI_PACKETIZED bit in*/
   /* SCSI_CURRENT_XFER table.  */
   
   if ((SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_PACKETIZED) &&
       (deviceTable->SCSI_DF_needNego))
   {
      /* Let the Rev A standard U320 Sequencer know we wish to negotiation */
      /*  while in Packetized mode */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
           OSDoffsetof(SCSI_SCB_DCH_U320,atn_length),0x05);

      deviceTable->SCSI_DF_needNego = 0;
   }
   else
   {
      if (hiob->SCSI_IF_tagEnable)
      {
         if (deviceTable->SCSI_DF_needNego)
         {
            /* negotiation needed for tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_DCH_U320,atn_length),0x04);

            deviceTable->SCSI_DF_needNego = 0;
         }
         else
         {
            /* negotiation not needed for tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_DCH_U320,atn_length),0x03);
         }
      }
      else
      {
         if (deviceTable->SCSI_DF_needNego)
         {
            /* negotiation needed for non-tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_DCH_U320,atn_length),0x02);

            deviceTable->SCSI_DF_needNego = 0;
         }
         else
         {
            /* negotiation not needed for non-tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer,
                 OSDoffsetof(SCSI_SCB_DCH_U320,atn_length),0x01);
         }
      }
   }
   /* Flush the cache */
   SCSI_FLUSH_CACHE(scbBuffer, sizeof(SCSI_SCB_DCH_U320));
}
#endif /* SCSI_DCH_U320_MODE */


/*********************************************************************
*
*  SCSIhDownshiftU320SetupAtnLength
*
*     Setup atn_length field in a scb for downshift U320 mode
*     sequencer
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftU320SetupAtnLength (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
#if SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 databuffer;
#else /* !SCSI_BIOS_SUPPORT */
#if SCSI_DOWNSHIFT_U320_MODE
   SCSI_SCB_DOWNSHIFT_U320 SCSI_IPTR scbBuffer320;
#endif /* SCSI_DOWNSHIFT_U320_MODE */
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
   SCSI_SCB_DOWNSHIFT_ENH_U320 SCSI_IPTR scbBufferEnh320;
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */
#endif /* SCSI_BIOS_SUPPORT */
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 negoOnModeSwitch;
   SCSI_UEXACT8 modeSave;
#if !SCSI_BIOS_SUPPORT
#if SCSI_DOWNSHIFT_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
      scbBuffer320 = (SCSI_SCB_DOWNSHIFT_U320 SCSI_IPTR)hiob->scbDescriptor->scbBuffer.virtualAddress;
#endif /* SCSI_DOWNSHIFT_U320_MODE */
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
      scbBufferEnh320 = (SCSI_SCB_DOWNSHIFT_ENH_U320 SCSI_IPTR)hiob->scbDescriptor->scbBuffer.virtualAddress;
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */
#endif /* SCSI_BIOS_SUPPORT */
   /* Save the HCNTRL value */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* Pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   /* For the downshift U320 mode, get the specific target's negoOnModeSwitch */
   /* flag stored in scratch RAM, SCSI_NEGO_ON_MODE_SWITCH.  Most likely, the */
   /* flag is updated during the mode context switch (switch to Real mode).   */
   if (SCSI_TARGET_UNIT(hiob)->scsiID < 8)
   {
      negoOnModeSwitch = 
         (SCSI_UEXACT8) (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH)) &
                         (1 << SCSI_TARGET_UNIT(hiob)->scsiID));
   }
   else
   {
      negoOnModeSwitch = 
         (SCSI_UEXACT8) (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH+1)) &
                         (1 << (SCSI_TARGET_UNIT(hiob)->scsiID - 8)));
   }

   if (negoOnModeSwitch)
   {
      /* Save the current mode and set to mode 3 */
      modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      /* Offset to location in auto-rate-offset table by specified target ID */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGOADDR), SCSI_TARGET_UNIT(hiob)->scsiID);

      /* Check if HBA currently runs in Packetized mode because            */
      /* negoOnModeSwitch flag is set.  The Packetized mode is most likely */
      /* left over from the context switch (Protected mode to Real mode).  */
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA2)) & SCSI_PACKETIZED)
      {
         /* In Harpoon2 A0/A1 chip, setting ENAUTOATNO bit won't assert ATN  */
         /* when the chip completes Selection out in Packetized mode.  Razor */ 
         /* issue #396.  The workaround is CHIM will turn off all protocol   */
         /* option bits (SPI4, QAS, DUALEDGE, and PACKETIZED) in NEGODAT2    */
         /* register.                                                        */
         if (SCSI_hHARPOON_REV_A(hhcb))
         {  
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA2), 0);
         }
   
         /* Turn on Select with ATN out by setting the SCSI_ENAUTOATNO bit */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA3),
             (OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA3)) | SCSI_ENAUTOATNO));
      }

      /* Restore mode pointer */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
   }

   /* Restore HCNTRL if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }

#if SCSI_BIOS_SUPPORT
   /* During mode context switching (Protected mode to Real mode), we will */
   /* setup negotiation needed in SCB if the negoOnModeSwitch flag set.    */
   if (deviceTable->SCSI_DF_needNego || negoOnModeSwitch)
   {
      /* negotiation needed for non-tagged queue command */
      databuffer = 0x02;
#if SCSI_DOWNSHIFT_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
      {
         SCSIHWriteScbRAM(hhcb,
                          hiob->scbNumber,
                         (SCSI_UEXACT8) OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,
                                                    atn_length),
                         (SCSI_UEXACT8) 1,
                         (SCSI_UEXACT8 SCSI_IPTR) &databuffer);
      }
#endif /* SCSI_DOWNSHIFT_U320_MODE */      
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
      {
         SCSIHWriteScbRAM(hhcb,
                          hiob->scbNumber,
                         (SCSI_UEXACT8) OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,
                                                    atn_length),
                         (SCSI_UEXACT8) 1,
                         (SCSI_UEXACT8 SCSI_IPTR) &databuffer);
      }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */     
      if (!hhcb->SCSI_HF_biosRunTimeEnabled)
      {
         deviceTable->SCSI_DF_needNego = 0;
      }      
   }
   else
   {
      /* negotiation not needed for non-tagged queue command */
      databuffer = 0x01;
#if SCSI_DOWNSHIFT_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
      {
         SCSIHWriteScbRAM(hhcb,
                         hiob->scbNumber,
                         (SCSI_UEXACT8) OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,
                                                    atn_length),
                         (SCSI_UEXACT8) 1,
                         (SCSI_UEXACT8 SCSI_IPTR) &databuffer);
      }
#endif /* SCSI_DOWNSHIFT_U320_MODE */
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
      {
         SCSIHWriteScbRAM(hhcb,
                         hiob->scbNumber,
                         (SCSI_UEXACT8) OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,
                                                    atn_length),
                         (SCSI_UEXACT8) 1,
                         (SCSI_UEXACT8 SCSI_IPTR) &databuffer);
      }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */     
   }

#else /* !SCSI_BIOS_SUPPORT */

   if (hiob->SCSI_IF_tagEnable)
   {
      /* During mode context switching (Protected mode to Real mode), we will */
      /* setup negotiation needed in SCB if the negoOnModeSwitch flag set.    */
      if (deviceTable->SCSI_DF_needNego || negoOnModeSwitch)
      {
#if SCSI_DOWNSHIFT_U320_MODE
         if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
         {
            /* negotiation needed for tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer320,
                 OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,atn_length),0x04);
         }
#endif /* SCSI_DOWNSHIFT_U320_MODE */
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
         if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
         {
            /* negotiation needed for tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBufferEnh320,
                 OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,atn_length),0x04);
         }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */    
         deviceTable->SCSI_DF_needNego = 0;
      }
      else
      {
#if SCSI_DOWNSHIFT_U320_MODE
         /* negotiation not needed for tagged queue command */
         if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
         {
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer320,
                 OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,atn_length),0x03);
         }
#endif /* SCSI_DOWNSHIFT_U320_MODE */        
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
         if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
         {
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBufferEnh320,
                 OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,atn_length),0x03);
         }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */    
      }
   }
   else
   {
      /* During mode context switching (Protected mode to Real mode), we will */
      /* setup negotiation needed in SCB if the negoOnModeSwitch flag set.    */
      if (deviceTable->SCSI_DF_needNego || negoOnModeSwitch)
      {
#if SCSI_DOWNSHIFT_U320_MODE
         if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
         {
            /* negotiation needed for non-tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer320,
               OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,atn_length),0x02);
         }
#endif /* SCSI_DOWNSHIFT_U320_MODE */        
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE	
         if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
         {
            /* negotiation needed for non-tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBufferEnh320,
               OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,atn_length),0x02);
         }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */    
         deviceTable->SCSI_DF_needNego = 0;
      }
      else
      {
#if SCSI_DOWNSHIFT_U320_MODE
         if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
         {        
            /* negotiation not needed for non-tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBuffer320,
               OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,atn_length),0x01);
         }
#endif /* SCSI_DOWNSHIFT_U320_MODE */        
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE	
         if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
         {
            /* negotiation not needed for non-tagged queue command */
            SCSI_PUT_LITTLE_ENDIAN8(hhcb,scbBufferEnh320,
               OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,atn_length),0x01);
         }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */    
      }
   }

   /* Flush the cache */
   SCSI_FLUSH_CACHE(scbBuffer, sizeof(SCSI_SCB_DOWNSHIFT_U320));
#endif /* SCSI_BIOS_SUPPORT */
}
#endif /* SCSI_DOWNSHIFT_MODE */

/*********************************************************************
*
*  SCSIhDownShiftU320SetCommandRequestor
*
*     Apply Delivery mode for downshift U320 mode
*
*  Return Value:  None
*
*  Parameters:    hhcb
*
*  Remarks:       Sets the scratch location indicating the
*                 command requestor (BIOS or ASPI/Real Mode).
*                 Called when delivering a command to the
*                 Downshift sequencer. Note; only one
*                 command may be executed at a time.
*                 When the requestor is the BIOS the mode
*                 2 SCBPTR register is set to hiob->scbNumber
*                 the array site containing the SCB.
*
*********************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownShiftU320SetCommandRequestor (SCSI_HHCB SCSI_HPTR hhcb,
                                            SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;
#if SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 savedMode;
#endif /* SCSI_BIOS_SUPPORT */
   
   /* Save the HCNTRL value. */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }
   
#if SCSI_BIOS_SUPPORT

      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND)) | SCSI_DU320_BIOS_COMMAND));


      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND)) & ~SCSI_DU320_ASPI_COMMAND));

   
   /* Additionally set the mode 2 SCBPTR to the scb number of the
    * site that the BIOS specified for delivery.
    */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);
   /* Load mode 2 scb pointer with array_site */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                 (SCSI_UEXACT8)hiob->scbNumber);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                 (SCSI_UEXACT8)(hiob->scbNumber >> 8));
   /* Restore Mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);

#else /* !SCSI_BIOS_SUPPORT */
   /* Must be ASPI or REAL MODE */
  

      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND)) | SCSI_DU320_ASPI_COMMAND));


      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND)) & ~SCSI_DU320_BIOS_COMMAND));

#endif /* SCSI_BIOS_SUPPORT */

   /* Restore HCNTRL, if necessary. */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}
#endif /* SCSI_DOWNSHIFT_MODE */

/*********************************************************************
*
*  SCSIhStandardQHead
*
*     Enqueue at head of execution queue for standard mode sequencers
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumes the sequencer is paused prior to routine
*                 entry. According to ASIC Programmers Guide, SCBPTR
*                 access is only completely safe in Mode 3. Therefore,
*                 only manipuate the mode 3 SCBPTR register.
*                 MODE_PTR register saved on entry and restored on
*                 exit.
*
**********************************************************************/
#if (SCSI_STANDARD_MODE && (!SCSI_DCH_U320_MODE))
void SCSIhStandardQHead (SCSI_HHCB SCSI_HPTR hhcb,
                         SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_HIOB SCSI_IPTR hiobSelecting;
   SCSI_UEXACT16 currentScb;
   SCSI_UEXACT16 nextScb, previousScb;
   SCSI_UEXACT16 waitingScb;
   SCSI_UEXACT16 savedScbptr;
   SCSI_UEXACT16 targTail;
   SCSI_UEXACT8 scsiseq;
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT8 targetID;
   SCSI_UEXACT16 qExeTargNextScbArray;
   SCSI_UEXACT16 qNextScbArray;
   
   /* Save the current mode for now. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for SCBPTR access. Also
    * other register accesses in this routine require mode 3.
    */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Save SCBPTR */
   savedScbptr =
      (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
      ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1)) << 8);

   /* Assign the register mapped SCB field (SCBARRAY) variables. */  
   qExeTargNextScbArray =
      (SCSI_UEXACT16)(SCSI_SCB_BASE + SCSI_hQEXETARGNEXTOFFSET(hhcb));
   qNextScbArray =
      (SCSI_UEXACT16)(SCSI_SCB_BASE + SCSI_hQNEXTOFFSET(hhcb));
   
   /* To handle the check condition received during the reselection  */
   /* at one HIOB while a selection is starting from another HIOB.   */
   /* Both HIOBs are for the same target.                            */ 
   if (((scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0))) & SCSI_ENSELO) &&
#if SCSI_TARGET_OPERATION
       (!hiob->SCSI_IP_targetMode) &&
#endif /* SCSI_TARGET_OPERATION */
       (hiob->trgStatus == SCSI_UNIT_CHECK))
   {
      waitingScb =
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1)))<< 8) |
         (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

      /* Get the HIOB that was attempting to perform selection and 
       * check if its target ID matches with the one of the current
       * HIOB. If it does then we need to ensure that this SCB is
       * not executed before the one associated with the HIOB passed
       * to this routine. As the Request Sense must be executed
       * prior to the waiting SCB.
       */

      hiobSelecting = SCSI_ACTPTR[waitingScb];

#if SCSI_TARGET_OPERATION
      if (hiobSelecting->SCSI_IP_targetMode)
      {
         /* OK for target mode request to same device to go out
          * on bus before Request Sense.
          * Indicate no waiting SCB to be requeued to execution queue.
          */
         SCSI_SET_NULL_SCB(waitingScb);
      }
      else
#endif /* SCSI_TARGET_OPERATION */
      if ((SCSI_TARGET_UNIT(hiobSelecting)->hhcb == hhcb) &&
          (SCSI_TARGET_UNIT(hiobSelecting)->scsiID ==
           SCSI_TARGET_UNIT(hiob)->scsiID))
      {
         /* Waiting SCB must be requeued to the execution queue, so
          * that it does not go out on the bus before the Request Sense
          * I/O. The waiting SCB will be requeued when the SCB
          * associated with the HIOB is queued (later in the routine). 
          * Selection must be disabled to prevent the waiting SCB 
          * from executing when the sequencer is unpaused.
          */

         scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0),
                       (scsiseq & ~(SCSI_TEMODEO+SCSI_ENSELO)));

         /* Clear target busy entry */ 
         SCSI_hTARGETCLEARBUSY(hhcb,hiobSelecting);

         /* To flush the cache */
         scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0));
      }
      else
      {
         /* Indicate no waiting SCB to be requeued to execution queue. */
         SCSI_SET_NULL_SCB(waitingScb);
      }
   }
   else
   {
      /* Indicate no waiting SCB to be requeued to execution queue. */
      SCSI_SET_NULL_SCB(waitingScb);
   }
   
   /* Insert the given SCB at the right place which should be either       */
   /* a. at the head of common exe queue, if there are no SCBs in the      */
   /*    target specific exe, or                                           */
   /* b. at the head of the target specific exe queue.                     */

   /* We do this as a 2 step process:                                      */
   /*    Step #1                                                           */
   /*    -------                                                           */
   /* Build the target specific queue (ie. our SCB should be at the head   */
   /* and the Q_EXETARG_TAIL should be initialized appropriately to point  */
   /* to the tail of the target specific queue). Also, setup 'currentScb', */
   /* 'previousScb' and 'nextScb' appropriately so that the newly built    */
   /* target specific exe queue can be inserted at the right place in the  */
   /* common exe queue.                                                    */
   /*    Step #2                                                           */
   /*    -------                                                           */
   /* Insert the new target specific exe queue that was built in step #1 at*/
   /* the appropriate place in the common exe queue.                       */

   /* **** Start of Step #1 **** */

   targetID = SCSI_TARGET_UNIT(hiob)->scsiID;

   targTail =
      (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID)) |
       ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID+1))) << 8);

   SCSI_SET_NULL_SCB(previousScb);

   /* Is there a target specific queue for our target? */
   if (SCSI_IS_NULL_SCB(targTail))
   {
      /* No. Finish the step #1 process mentioned above. */
      SCSI_SET_NULL_SCB(currentScb);
      nextScb =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1))) << 8);

      /* Set EXETARGT_TAIL to correct value. */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID),
                         (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID+1),
                         (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1),
                         (SCSI_UEXACT8)(SCSI_NULL_SCB));

      /* We have built our new target specific execution queue.  We can */
      /* proceed to put this queue into the common execution queue. */ 
   }

   /* Yes there is a target specific queue.  But this queue may have been   */
   /* delinked from the common exe queue by the sequencer if it has already */
   /* started a selection process for the same target. */

   /* Has the target specific queue been delinked from the common exe queue? */
   else if (!(SCSI_IS_NULL_SCB(waitingScb)))
   {
      /* Yes.  Finish the step #1 process mentioned above and proceed to step*/
      /* #2. */
      SCSI_SET_NULL_SCB(currentScb);
      nextScb =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1))) << 8);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray),
                         (SCSI_UEXACT8)waitingScb);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1),
                         (SCSI_UEXACT8)(waitingScb >> 8));

      /* We have built our new target specific execution queue.  We can */
      /* proceed to put this queue into the common execution queue. */ 
   }
   else
   {
      /* No.  There is a target specific exe queue for the given target, and */
      /* it is also present in the common exe queue. Search the common exe   */
      /* queue to get the target specific exe queue and then finish the step */
      /* #1. */
      SCSI_SET_NULL_SCB(previousScb);
      SCSI_SET_NULL_SCB(nextScb);

      currentScb =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1))) << 8);

      /* Keep searching the common exe queue until the SCB for the given   */
      /* target is found.                                                  */
      while (!SCSI_IS_NULL_SCB(currentScb))
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)currentScb);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(currentScb >> 8));

         nextScb = 
            (SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(qNextScbArray))) |
            ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1))) << 8);

         /* Found the scb matching target id in the common exe queue ?     */
         if ((SCSI_ACTPTR[currentScb] != SCSI_NULL_HIOB) &&
             (SCSI_TARGET_UNIT(SCSI_ACTPTR[currentScb])->scsiID == targetID))
         {
            /* Yes. Put our SCB at the head of the target specific queue.   */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

            OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray),
                               (SCSI_UEXACT8)currentScb);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1),
                               (SCSI_UEXACT8)(currentScb >> 8));

            /* We have built our new target specific execution queue.  We can*/
            /* proceed to put this queue into the common execution queue. */ 
            break;
         }
         else
         {
            /* No. Keep searching the common exe queue */
            previousScb = currentScb;
            currentScb = nextScb;
         }
      }
   }
         
   /* **** End of Step #1 **** */

   /* When we reach here the following statements are true:                  */
   /*    a) The new target specific exe queue (with our inserted SCB at the  */
   /*       head) has been completely built.                                 */
   /*    b) SCBPTR register is pointing our inserted SCB.                    */

   /* **** Start of Step #2 **** */

   /* Put the newly build target specific exe queue into the common exe      */
   /* queue. */

   /* Is the common exe queue empty? */
   if (SCSI_IS_NULL_SCB(currentScb))
   {
      /* Yes.  Put the given SCB at head of the common exe queue   */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD),
                         (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1),
                         (SCSI_UEXACT8)(hiob->scbNumber >> 8));
   }
   else
   {
      /* No.  Do the following:                                      */
      /* a. link it to the previous SCB if the target specific queue */
      /*    is not at the head of the common exe queue               */
      /*    (i.e. previous SCB is not NULL), or                      */
      /* b. put it at the Q_EXE_HEAD if the target specific queue    */
      /*    is at the head of the common exe queue                   */
      /*    (ie. previous SCB is NULL)                               */
      if (SCSI_IS_NULL_SCB(previousScb))
      {
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD),
                            (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1),
                            (SCSI_UEXACT8)(hiob->scbNumber >> 8));
      }
      else
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)previousScb);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(previousScb >> 8));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray),
                            (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1),
                            (SCSI_UEXACT8)(hiob->scbNumber >> 8));

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                 (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                 (SCSI_UEXACT8)(hiob->scbNumber >> 8));
      }
   }

   /* At this point, nextScb points to the SCB to which our inserted SCB     */
   /* need to be linked to, to complete the mending of the common exe queue. */

   /* If the SCB we inserted was at the tail of the common exe queue ...   */
   if (SCSI_IS_NULL_SCB(nextScb))
   {
      /* ... then make the q_next point to NULL, and point Q_EXE_TAIL to   */
      /* the inserted SCB                                                  */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1),
                         (SCSI_UEXACT8)SCSI_NULL_SCB);

      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL),
                         (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL+1),
                         (SCSI_UEXACT8)(hiob->scbNumber >> 8));
   }
   else
   {
      /* ... else simply point q_next to the nextScb */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray),
                         (SCSI_UEXACT8)nextScb);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1),
                         (SCSI_UEXACT8)(nextScb >> 8));
   }

   /* **** End of Step #2 **** */

   /* Restore SCBPTR */   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)savedScbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(savedScbptr >> 8));

   /* Restore MODE_PTR */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
}
#endif /* (SCSI_STANDARD_MODE && (!SCSI_DCH_U320_MODE)) */


/*********************************************************************
*
*  SCSIhDCHU320QHead
*
*     Enqueue at head of execution queue for standard U320 mode
*     sequencer
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumes the sequencer is paused prior to routine
*                 entry. According to ASIC Programmers Guide, SCBPTR
*                 access is only completely safe in Mode 3. Therefore,
*                 only manipuate the mode 3 SCBPTR register.
*                 MODE_PTR register saved on entry and restored on
*                 exit.
*
**********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320QHead (SCSI_HHCB SCSI_HPTR hhcb,
                        SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_HIOB SCSI_IPTR hiobSelecting;
   SCSI_UEXACT16 currentScb;
   SCSI_UEXACT16 nextScb, previousScb;
   SCSI_UEXACT16 waitingScb;
   SCSI_UEXACT16 savedScbptr;
   SCSI_UEXACT16 targTail;
   SCSI_UEXACT8 scsiseq;
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT8 targetID;
   
   /* Save the current mode for now. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for SCBPTR access. Also
    * other register accesses in this routine require mode 3.
    */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Save SCBPTR */
   savedScbptr =
      (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
      ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1)) << 8);

   /* To handle the check condition received during the reselection  */
   /* at one HIOB while a selection is starting from another HIOB.   */
   /* Both HIOBs are for the same target.                            */ 
   if (((scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0))) & SCSI_ENSELO) &&
#if SCSI_TARGET_OPERATION
       (!hiob->SCSI_IP_targetMode) &&
#endif /* SCSI_TARGET_OPERATION */
       (hiob->trgStatus == SCSI_UNIT_CHECK))
   {
      waitingScb =
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
         (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

      /* Get the HIOB that was attempting to perform selection and 
       * check if its target ID matches with the one of the current
       * HIOB. If it does then we need to ensure that this SCB is
       * not executed before the one associated with the HIOB passed
       * to this routine. As the Request Sense must be executed
       * prior to the waiting SCB.
       */

      hiobSelecting = SCSI_ACTPTR[waitingScb];

#if SCSI_TARGET_OPERATION
      if (hiobSelecting->SCSI_IP_targetMode)
      {
         /* OK for target mode request to same device to go out
          * on bus before Request Sense.
          * Indicate no waiting SCB to be requeued to execution queue.
          */
         SCSI_SET_NULL_SCB(waitingScb);
      }
      else
#endif /* SCSI_TARGET_OPERATION */
      if ((SCSI_TARGET_UNIT(hiobSelecting)->hhcb == hhcb) &&
          (SCSI_TARGET_UNIT(hiobSelecting)->scsiID == 
           SCSI_TARGET_UNIT(hiob)->scsiID))
      {
         /* Waiting SCB must be requeued to the execution queue, so
          * that it does not go out on the bus before the Request Sense
          * I/O. The waiting SCB will be requeued when the SCB
          * associated with the HIOB is queued (later in the routine). 
          * Selection must be disabled to prevent the waiting SCB 
          * from executing when the sequencer is unpaused.
          */

         scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0),
                       (scsiseq & ~(SCSI_TEMODEO+SCSI_ENSELO)));

         /* Clear target busy entry */ 
         SCSI_hTARGETCLEARBUSY(hhcb,hiobSelecting);

         /* To flush the cache */
         scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0));
      }
      else
      {
         /* Indicate no waiting SCB to be requeued to execution queue. */
         SCSI_SET_NULL_SCB(waitingScb);
      }
   }
   else
   {
      /* Indicate no waiting SCB to be requeued to execution queue. */
      SCSI_SET_NULL_SCB(waitingScb);
   }
   
   /* Insert the given SCB at the right place which should be either       */
   /* a. at the head of common exe queue, if there are no SCBs in the      */
   /*    target specific exe, or                                           */
   /* b. at the head of the target specific exe queue.                     */

   /* We do this as a 2 step process:                                      */
   /*    Step #1                                                           */
   /*    -------                                                           */
   /* Build the target specific queue (ie. our SCB should be at the head   */
   /* and the Q_EXETARG_TAIL should be initialized appropriately to point  */
   /* to the tail of the target specific queue). Also, setup 'currentScb', */
   /* 'previousScb' and 'nextScb' appropriately so that the newly built    */
   /* target specific exe queue can be inserted at the right place in the  */
   /* common exe queue.                                                    */
   /*    Step #2                                                           */
   /*    -------                                                           */
   /* Insert the new target specific exe queue that was built in step #1 at*/
   /* the appropriate place in the common exe queue.                       */

   /* **** Start of Step #1 **** */

   targetID = SCSI_TARGET_UNIT(hiob)->scsiID;

   targTail =
      (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID)) |
       ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID+1))) << 8);

   SCSI_SET_NULL_SCB(previousScb);

   /* Is there a target specific queue for our target? */
   if (SCSI_IS_NULL_SCB(targTail))
   {
      /* No. Finish the step #1 process mentioned above. */
      SCSI_SET_NULL_SCB(currentScb);
      nextScb =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1))) << 8);

      /* Set EXETARGT_TAIL to correct value. */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID),
                         (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID+1),
                         (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                            OSDoffsetof(SCSI_SCB_DCH_U320,
                                        SCSI_DCHU320_q_exetarg_next)+1),
                         (SCSI_UEXACT8)(SCSI_NULL_SCB));

      /* We have built our new target specific execution queue.  We can */
      /* proceed to put this queue into the common execution queue. */ 
   }

   /* Yes there is a target specific queue.  But this queue may have been   */
   /* delinked from the common exe queue by the sequencer if it has already */
   /* started a selection process for the same target. */

   /* Has the target specific queue been delinked from the common exe queue? */
   else if (!(SCSI_IS_NULL_SCB(waitingScb)))
   {
      /* Yes.  Finish the step #1 process mentioned above and proceed to step*/
      /* #2. */
      SCSI_SET_NULL_SCB(currentScb);
      nextScb =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1))) << 8);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                            OSDoffsetof(SCSI_SCB_DCH_U320,
                                        SCSI_DCHU320_q_exetarg_next)),
                         (SCSI_UEXACT8)waitingScb);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                            OSDoffsetof(SCSI_SCB_DCH_U320,
                                        SCSI_DCHU320_q_exetarg_next)+1),
                         (SCSI_UEXACT8)(waitingScb >> 8));

      /* We have built our new target specific execution queue.  We can */
      /* proceed to put this queue into the common execution queue. */ 
   }
   else
   {
      /* No.  There is a target specific exe queue for the given target, and */
      /* it is also present in the common exe queue. Search the common exe   */
      /* queue to get the target specific exe queue and then finish the step */
      /* #1. */
      SCSI_SET_NULL_SCB(previousScb);
      SCSI_SET_NULL_SCB(nextScb);

      currentScb =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1))) << 8);

      /* Keep searching the common exe queue until the SCB for the given   */
      /* target is found.                                                  */
      while (!SCSI_IS_NULL_SCB(currentScb))
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)currentScb);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(currentScb >> 8));

         nextScb = 
            (SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
               OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)))) |
            ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
               OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1))) << 8);

         /* Found the scb matching target id in the common exe queue ?     */
         if ((SCSI_ACTPTR[currentScb] != SCSI_NULL_HIOB) &&
             (SCSI_TARGET_UNIT(SCSI_ACTPTR[currentScb])->scsiID == targetID))
         {
            /* Yes. Put our SCB at the head of the target specific queue.   */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                            OSDoffsetof(SCSI_SCB_DCH_U320,
                                        SCSI_DCHU320_q_exetarg_next)),
                         (SCSI_UEXACT8)currentScb);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                            OSDoffsetof(SCSI_SCB_DCH_U320,
                                        SCSI_DCHU320_q_exetarg_next)+1),
                         (SCSI_UEXACT8)(currentScb >> 8));

            /* We have built our new target specific execution queue.  We can*/
            /* proceed to put this queue into the common execution queue. */ 
            break;
         }
         else
         {
            /* No. Keep searching the common exe queue */
            previousScb = currentScb;
            currentScb = nextScb;
         }
      }
   }
         
   /* **** End of Step #1 **** */

   /* When we reach here the following statements are true:                  */
   /*    a) The new target specific exe queue (with our inserted SCB at the  */
   /*       head) has been completely built.                                 */
   /*    b) SCBPTR register is pointing our inserted SCB.                    */

   /* **** Start of Step #2 **** */

   /* Put the newly build target specific exe queue into the common exe      */
   /* queue. */

   /* Is the common exe queue empty? */
   if (SCSI_IS_NULL_SCB(currentScb))
   {
      /* Yes.  Put the given SCB at head of the common exe queue   */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD),
                         (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1),
                         (SCSI_UEXACT8)(hiob->scbNumber >> 8));
   }
   else
   {
      /* No.  Do the following:                                      */
      /* a. link it to the previous SCB if the target specific queue */
      /*    is not at the head of the common exe queue               */
      /*    (i.e. previous SCB is not NULL), or                      */
      /* b. put it at the Q_EXE_HEAD if the target specific queue    */
      /*    is at the head of the common exe queue                   */
      /*    (ie. previous SCB is NULL)                               */
      if (SCSI_IS_NULL_SCB(previousScb))
      {
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD),
                            (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1),
                            (SCSI_UEXACT8)(hiob->scbNumber >> 8));
      }
      else
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)previousScb);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(previousScb >> 8));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)),
                        (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1),
                        (SCSI_UEXACT8)(hiob->scbNumber >> 8));

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                 (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                 (SCSI_UEXACT8)(hiob->scbNumber >> 8));
      }
   }

   /* At this point, nextScb points to the SCB to which our inserted SCB     */
   /* need to be linked to, to complete the mending of the common exe queue. */

   /* If the SCB we inserted was at the tail of the common exe queue ...   */
   if (SCSI_IS_NULL_SCB(nextScb))
   {
      /* ... then make the q_next point to NULL, and point Q_EXE_TAIL to   */
      /* the inserted SCB                                                  */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_q_next)+1),
                      (SCSI_UEXACT8)SCSI_NULL_SCB);

      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL),
                         (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL+1),
                         (SCSI_UEXACT8)(hiob->scbNumber >> 8));
   }
   else
   {
      /* ... else simply point q_next to the nextScb */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)),
                      (SCSI_UEXACT8)nextScb);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1),
                      (SCSI_UEXACT8)(nextScb >> 8));
   }

   /* **** End of Step #2 **** */

   /* Restore SCBPTR */   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)savedScbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(savedScbptr >> 8));

   /* Restore MODE_PTR */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhDownshiftU320QHead
*
*     Enqueue at head of execution queue for downshift U320 mode
*     sequencer
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumes the sequencer is paused prior to routine
*                 entry. According to ASIC Programmers Guide, SCBPTR
*                 access is only completely safe in Mode 3. Therefore,
*                 only manipuate the mode 3 SCBPTR register.
*                 MODE_PTR register saved on entry and restored on
*                 exit.
*
**********************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftU320QHead (SCSI_HHCB SCSI_HPTR hhcb,
                              SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 nextScb;
   SCSI_UEXACT16 savedScbptr;
   SCSI_UEXACT16 targTail;
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT8 targetID;

   /* Save the current mode for now. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for SCBPTR access. Also
    * other register accesses in this routine require mode 3.
    */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Save SCBPTR */
   savedScbptr =
      (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
      ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1)) << 8);

   /* Make sure we point SCBPTR to the given SCB to get the q_next
    * pointer to link the nextSCB.
    */
    
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                 (SCSI_UEXACT8)hiob->scbNumber);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                 (SCSI_UEXACT8)(hiob->scbNumber >> 8));

   targetID = SCSI_TARGET_UNIT(hiob)->scsiID;

   /* put the given SCB at head of the common exe queue   */
   nextScb =
      (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_EXE_HEAD)) |
      ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_EXE_HEAD+1))) << 8);

   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_EXE_HEAD),
                      (SCSI_UEXACT8)hiob->scbNumber);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_EXE_HEAD+1),
                      (SCSI_UEXACT8)(hiob->scbNumber >> 8));
                      
   /* No waiting for SCB to be queued. Make the given SCB the only 
    * SCB in the target exe queue by writing SCSI_NULL_SCB to 
    * 'q_exetarg_next' field of the given SCB.
    */
#if SCSI_DOWNSHIFT_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)    
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                            OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,
                                       SCSI_DU320_q_exetarg_next)+1),
                         (SCSI_UEXACT8)(SCSI_NULL_SCB));
   }
#endif /* SCSI_DOWNSHIFT_U320_MODE */
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
   {   
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                            OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,
                                       SCSI_DEU320_q_exetarg_next)+1),
                         (SCSI_UEXACT8)(SCSI_NULL_SCB));
   }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */
   /* Set EXETARGT_TAIL to given SCB. */
   targTail = hiob->scbNumber;

   /* Set EXETARGT_TAIL to correct value. */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_EXETARG_TAIL+2*targetID),
                      (SCSI_UEXACT8)targTail);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_EXETARG_TAIL+2*targetID+1),
                      (SCSI_UEXACT8)(targTail >> 8));
                      
   /* point q_next to the nextScb */
#if SCSI_DOWNSHIFT_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
   {
   
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320, SCSI_DU320_q_next)),
                      (SCSI_UEXACT8)nextScb);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320, SCSI_DU320_q_next)+1),
                      (SCSI_UEXACT8)(nextScb >> 8));
   
   }
#endif /* SCSI_DOWNSHIFT_U320_MODE */
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
   
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320, SCSI_DEU320_q_next)),
                      (SCSI_UEXACT8)nextScb);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320, SCSI_DEU320_q_next)+1),
                      (SCSI_UEXACT8)(nextScb >> 8));
   }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */
   /* Restore SCBPTR */   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                 (SCSI_UEXACT8)savedScbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                 (SCSI_UEXACT8)(savedScbptr >> 8));

   /* Restore MODE_PTR */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
}
#endif /* SCSI_DOWNSHIFT_MODE */


/*********************************************************************
*
*  SCSIhRetrieveScb
*
*     Retrieve scb for standard mode
*
*  Return Value:  scb number retrieved
*
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
/* @@@@ do later - additional inline code needed to do this #if !SCSI_STREAMLINE_QIOPATH */
#if SCSI_STANDARD_MODE
SCSI_UEXACT16 SCSIhRetrieveScb (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_QOUTFIFO_NEW SCSI_HPTR qOutPtr =
                SCSI_QOUT_PTR_ARRAY_NEW(hhcb) + hhcb->SCSI_HP_qDoneElement;
   SCSI_UEXACT16 scbNumber = 0;
   SCSI_UEXACT8 qPassCnt;

   /* invalidate cache */
   SCSI_INVALIDATE_CACHE(((SCSI_UEXACT8 SCSI_HPTR)SCSI_QOUT_PTR_ARRAY_NEW(hhcb)) +
      ((hhcb->SCSI_HP_qDoneElement * sizeof(SCSI_QOUTFIFO_NEW)) /
      OSD_DMA_SWAP_WIDTH) * OSD_DMA_SWAP_WIDTH, OSD_DMA_SWAP_WIDTH);

   /* check if there is any valid entry */
   SCSI_GET_LITTLE_ENDIAN8(hhcb,&qPassCnt,qOutPtr,
      OSDoffsetof(SCSI_QOUTFIFO_NEW,quePassCnt));
   if (qPassCnt == hhcb->SCSI_HP_qDonePass)
   {
      /* found valid entry, get scb number and update */
      SCSI_GET_LITTLE_ENDIAN16(hhcb,&scbNumber,qOutPtr,
         OSDoffsetof(SCSI_QOUTFIFO_NEW,scbNumber));

      if (++hhcb->SCSI_HP_qDoneElement == hhcb->totalDoneQElements)
      {
         hhcb->SCSI_HP_qDoneElement = 0;
         ++hhcb->SCSI_HP_qDonePass;                  
      }
   }
   else
   {
      /* set scb number to null */
      SCSI_SET_NULL_SCB(scbNumber);
   }

   return(scbNumber);
}
#endif /* SCSI_STANDARD_MODE */

/*********************************************************************
*
*  SCSIhQoutCount
*
*     Check if the QOUTFIFO is empty or not for standard mode
*
*  Return Value:  number of entries not checked in QOUTFIFO
*
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
/* @@@@ do later - additional inline code needed to do this #if !SCSI_STREAMLINE_QIOPATH */
#if SCSI_STANDARD_MODE
SCSI_UEXACT8 SCSIhQoutCount (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UEXACT8 n = 0;
   SCSI_UEXACT8 qDonePass = hhcb->SCSI_HP_qDonePass;
   SCSI_UEXACT16 qDoneElement = hhcb->SCSI_HP_qDoneElement;
   SCSI_QOUTFIFO_NEW SCSI_HPTR qOutPtr = SCSI_QOUT_PTR_ARRAY_NEW(hhcb) +
                                          qDoneElement;
   SCSI_UEXACT8 quePassCnt;

   while(1)
   {
      /* invalidate cache */
      SCSI_INVALIDATE_CACHE(((SCSI_UEXACT8 SCSI_HPTR)SCSI_QOUT_PTR_ARRAY_NEW(hhcb)) +
         ((qDoneElement * sizeof(SCSI_QOUTFIFO_NEW)) /
         OSD_DMA_SWAP_WIDTH) * OSD_DMA_SWAP_WIDTH, OSD_DMA_SWAP_WIDTH);

      /* check if there is any valid entry */
      SCSI_GET_LITTLE_ENDIAN8(hhcb,&quePassCnt,qOutPtr,
         OSDoffsetof(SCSI_QOUTFIFO_NEW,quePassCnt));

      if (quePassCnt == qDonePass)
      {
         /* bump the number of outstanding entries in qoutfifo */
         ++n;

         /* increment index */
         if (++qDoneElement != hhcb->totalDoneQElements)
         {
            ++qOutPtr;
         }
         else
         {
            /* wrap around */
            ++qDonePass;
            qDoneElement = 0;
            qOutPtr = SCSI_QOUT_PTR_ARRAY_NEW(hhcb);
         }
      }
      else
      {
         break;
      }
   }

   return(n);
}
#endif /* SCSI_STANDARD_MODE */

/*********************************************************************
*
*  SCSIhStandardQHeadTR
*
*     Put Target Reset SCB at head of target's execution queue
*     for standard mode sequencers
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       sequencer must be paused when this routine
*                 get referenced.
*
*********************************************************************/
#if !SCSI_DCH_U320_MODE
#if (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1)
void SCSIhStandardQHeadTR (SCSI_HHCB SCSI_HPTR hhcb,
                           SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT16 nextScbTargQ;
   SCSI_UEXACT16 nextScbComQ;
   SCSI_UEXACT16 prevScb;
   SCSI_UEXACT16 savedScbptr;
   SCSI_UEXACT8 targetID = SCSI_TARGET_UNIT(hiob)->scsiID;
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT16 qExeTargNextScbArray;
   SCSI_UEXACT16 qNextScbArray;
   SCSI_BOOLEAN mendTEQ = SCSI_FALSE;  /* Assume we won't need to mend TEQ */

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for SCBPTR access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Save mode 3 SCBPTR value. */
   savedScbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                 ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1)) << 8);

   /* Assign the register mapped SCB field (SCBARRAY) variables. */  
   qExeTargNextScbArray =
      (SCSI_UEXACT16)(SCSI_SCB_BASE + SCSI_hQEXETARGNEXTOFFSET(hhcb));
   qNextScbArray =
      (SCSI_UEXACT16)(SCSI_SCB_BASE + SCSI_hQNEXTOFFSET(hhcb));

   /* If target's execution queue (TEQ) of Target Reset SCB is  */
   /* in selection process, either selecting or selection done, */
   /* CHIM will queue Target Reset SCB next to the waiting_SCB. */
   if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SELOID)) == targetID) &&
       ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) ||
        (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_SELDO)))
   {
      /* Get the waiting_SCB */
      scbNumber =
         (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)) |
         ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1)) << 8);

      /* Point to waiting_SCB to get the next SCB in TEQ */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbNumber >> 8));

      /* Get next SCB in TEQ */
      nextScbTargQ =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
         ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1)) << 8);

      /* If TEQ's next SCB is Target Reset SCB, we do nothing because it    */
      /* will be at the head of TEQ after sequencer handled selection done. */
      if (nextScbTargQ != hiob->scbNumber)
      {
         /* TEQ's next SCB is not Target Reset SCB.                  */
         /* Put Target Reset SCB next to waiting_SCB.  It will be at */
         /* the head of TEQ after sequencer handled selection done.  */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray),
                            (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1),
                            (SCSI_UEXACT8)(hiob->scbNumber >> 8));

         /* Rebuild the TEQ by linking the remaining SCB(s) in original TEQ */
         /* back to Target Reset SCB.                                       */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(hiob->scbNumber >> 8));

         OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray),
                            (SCSI_UEXACT8)nextScbTargQ);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1),
                            (SCSI_UEXACT8)(nextScbTargQ >> 8));

         /* Set flag that requires to remove Target Reset SCB at the end of */
         /* TEQ because the SCB was appended by sequencer after DAMed in.   */
         mendTEQ = SCSI_TRUE;
      }
   }

   /* Else, go through common execution queue (CEQ) and find target */
   /* execution queue (TEQ) for Target Reset SCB.  Put Target Reset */
   /* SCB at the head of TEQ and mending the queue.                 */
   else
   {
      prevScb = scbNumber =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD)) |
         ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1)) << 8);

      /* Keep searching CEQ until the SCB for the given target is found */
      while (!SCSI_IS_NULL_SCB(scbNumber))
      {
         /* Point to the head of a target's execution queue */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbNumber >> 8));

         if (scbNumber == hiob->scbNumber)
         {
            /* Do nothing, Target Reset SCB is already at the head of TEQ */
            break;
         }

         /* Found the scb matching target id in the common execution queue? */
         if ((SCSI_ACTPTR[scbNumber] != SCSI_NULL_HIOB) &&
             (SCSI_TARGET_UNIT(SCSI_ACTPTR[scbNumber])->scsiID == targetID))
         {
            /* Queue the Target Reset SCB at the head of TEQ and */
            /* need to mend the common execution queue.          */

            nextScbComQ = 
               (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qNextScbArray)) |
               ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1)) << 8);

            /* TEQ for Target Reset SCB is at the CEQ head? */
            if (prevScb == scbNumber)
            {
               /* Yes. Make q_exe_head entry point to Target Reset SCB */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD),
                   (SCSI_UEXACT8)hiob->scbNumber);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1),
                   (SCSI_UEXACT8)(hiob->scbNumber >> 8));
            }
            else
            {
               /* No. Make q_next entry in prevScb point to Target Reset SCB */
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                   (SCSI_UEXACT8)prevScb);
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                   (SCSI_UEXACT8)(prevScb >> 8));

               OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray),
                   (SCSI_UEXACT8)hiob->scbNumber);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1),
                   (SCSI_UEXACT8)(hiob->scbNumber >> 8));
            }
         
            /* Link back other TEQ(s) if any after TEQ of Target Reset SCB */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                (SCSI_UEXACT8)hiob->scbNumber);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                (SCSI_UEXACT8)(hiob->scbNumber >> 8));

            /* Update q_next of Target Reset SCB to nextScbComQ */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray),
                (SCSI_UEXACT8)nextScbComQ);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1),
                (SCSI_UEXACT8)(nextScbComQ >> 8));

            /* If the TEQ of Target Reset SCB was at the tail of the CEQ ... */
            if (SCSI_IS_NULL_SCB(nextScbComQ))
            {
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL),
                                  (SCSI_UEXACT8)hiob->scbNumber);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL+1),
                                  (SCSI_UEXACT8)(hiob->scbNumber >> 8));
            }

            /* Put Target Reset SCB at the head of the TEQ */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray),
                               (SCSI_UEXACT8)scbNumber);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1),
                               (SCSI_UEXACT8)(scbNumber >> 8));

            /* Set flag that requires to remove Target Reset SCB at the end of*/
            /* TEQ because the SCB was appended by sequencer after DAMed in.  */
            mendTEQ = SCSI_TRUE;
            
            break;
         }

         /* Keep previous SCB */
         prevScb = scbNumber;

         /* Advance to next SCB in common execution queue */
         scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qNextScbArray)) |
                     ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(
                                         SCSI_AICREG(qNextScbArray+1))) << 8);
      } /* while loop */
   }

   if (mendTEQ == SCSI_TRUE)
   {
      /* Target Reset SCB must be queued at the end of TEQ after   */
      /* sequencer DMAed it into SCB array.  We need to remove it. */
      /* Search through TEQ for Target Reset SCB.                  */
      /* When it is found, update 'q_exetarg_next' entry in SCB    */
      /* that points to Target Reset SCB to NULL and update TEQ    */
      /* tail to point to this SCB.                                */

      /* Point to Target Reset SCB to get the next SCB in TEQ */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
          (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
          (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      /* Get next SCB in TEQ */
      prevScb = scbNumber =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(
                             SCSI_AICREG(qExeTargNextScbArray+1))) << 8);

      /* Search through TEQ until we find Target Reset SCB */
      while (scbNumber != hiob->scbNumber)
      {
         /* Keep previous SCB */
         prevScb = scbNumber;

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbNumber >> 8));

         /* Advance to next SCB in TEQ */
         scbNumber =
            (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
            ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1))) << 8);
      }

      /* Update the entry in q_exetarg_next to NULL */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1),
          (SCSI_UEXACT8)(SCSI_NULL_SCB));

      /* Update the entry in q_exetarg_tail table */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID),
          (SCSI_UEXACT8)prevScb);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID+1),
          (SCSI_UEXACT8)(prevScb >> 8));
   }

   /* Set Target_Reset state */
   hiob->SCSI_IP_mgrStat = SCSI_MGR_TR;

   /* Restore mode 3 SCBPTR */   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)savedScbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(savedScbptr >> 8));

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* (SCSI_STANDARD_ENHANCED_U320_MODE && !SCSI_ASPI_SUPPORT_GROUP1) */
#endif /* !SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhDchU320QHeadTR
*
*     Put Target Reset SCB at head of target's execution queue
*     for DCH U320 mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       sequencer must be paused when this routine
*                 get referenced.
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320QHeadTR (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT16 nextScbTargQ;
   SCSI_UEXACT16 nextScbComQ;
   SCSI_UEXACT16 prevScb;
   SCSI_UEXACT16 savedScbptr;
   SCSI_UEXACT8 targetID = SCSI_TARGET_UNIT(hiob)->scsiID;
   SCSI_UEXACT8 savedMode;
   SCSI_BOOLEAN mendTEQ = SCSI_FALSE;  /* Assume we won't need to mend TEQ */

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for SCBPTR access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Save mode 3 SCBPTR value. */
   savedScbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                 ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1)) << 8);

   /* If target's execution queue (TEQ) of Target Reset SCB is  */
   /* in selection process, either selecting or selection done, */
   /* CHIM will queue Target Reset SCB next to the waiting_SCB. */
   if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SELOID)) == targetID) &&
       ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) ||
        (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_SELDO)))
   {
      /* Get the waiting_SCB */
      scbNumber =
         (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)) |
         ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1)) << 8);

      /* Point to waiting_SCB to get the next SCB in TEQ */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbNumber >> 8));

      /* Get next SCB in TEQ */
      nextScbTargQ =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next))) |
         ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1)) << 8);

      /* If TEQ's next SCB is Target Reset SCB, we do nothing because it    */
      /* will be at the head of TEQ after sequencer handled selection done. */
      if (nextScbTargQ != hiob->scbNumber)
      {
         /* TEQ's next SCB is not Target Reset SCB.                  */
         /* Put Target Reset SCB next to waiting_SCB.  It will be at */
         /* the head of TEQ after sequencer handled selection done.  */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)),
             (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1),
             (SCSI_UEXACT8)(hiob->scbNumber >> 8));

         /* Rebuild the TEQ by linking the remaining SCB(s) in original TEQ */
         /* back to Target Reset SCB.                                       */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(hiob->scbNumber >> 8));

         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)),
             (SCSI_UEXACT8)nextScbTargQ);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1),
             (SCSI_UEXACT8)(nextScbTargQ >> 8));

         /* Set flag that requires to remove Target Reset SCB at the end of */
         /* TEQ because the SCB was appended by sequencer after DAMed in.   */
         mendTEQ = SCSI_TRUE;
      }
   }

   /* Else, go through common execution queue (CEQ) and find target */
   /* execution queue (TEQ) for Target Reset SCB.  Put Target Reset */
   /* SCB at the head of TEQ and mending the queue.                 */
   else
   {
      prevScb = scbNumber =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD)) |
         ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1)) << 8);

      /* Keep searching CEQ until the SCB for the given target is found */
      while (!SCSI_IS_NULL_SCB(scbNumber))
      {
         /* Point to the head of a target's execution queue */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbNumber >> 8));

         if (scbNumber == hiob->scbNumber)
         {
            /* Do nothing, Target Reset SCB is already at the head of TEQ */
            break;
         }

         /* Found the scb matching target id in the common execution queue? */
         if ((SCSI_ACTPTR[scbNumber] != SCSI_NULL_HIOB) &&
             (SCSI_TARGET_UNIT(SCSI_ACTPTR[scbNumber])->scsiID == targetID))
         {
            /* Queue the Target Reset SCB at the head of TEQ and */
            /* need to mend the common execution queue.          */

            nextScbComQ = 
               (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next))) |
               ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1)) << 8);

            /* TEQ for Target Reset SCB is at the CEQ head? */
            if (prevScb == scbNumber)
            {
               /* Yes. Make q_exe_head entry point to Target Reset SCB */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD),
                   (SCSI_UEXACT8)hiob->scbNumber);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1),
                   (SCSI_UEXACT8)(hiob->scbNumber >> 8));
            }
            else
            {
               /* No. Make q_next entry in prevScb point to Target Reset SCB */
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                   (SCSI_UEXACT8)prevScb);
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                   (SCSI_UEXACT8)(prevScb >> 8));

               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                   OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)),
                   (SCSI_UEXACT8)hiob->scbNumber);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                   OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1),
                   (SCSI_UEXACT8)(hiob->scbNumber >> 8));
            }
         
            /* Link back other TEQ(s) if any after TEQ of Target Reset SCB */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                (SCSI_UEXACT8)hiob->scbNumber);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                (SCSI_UEXACT8)(hiob->scbNumber >> 8));

            /* Update q_next of Target Reset SCB to nextScbComQ */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)),
                (SCSI_UEXACT8)nextScbComQ);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1),
                (SCSI_UEXACT8)(nextScbComQ >> 8));

            /* If the TEQ of Target Reset SCB was at the tail of the CEQ ... */
            if (SCSI_IS_NULL_SCB(nextScbComQ))
            {
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL),
                   (SCSI_UEXACT8)hiob->scbNumber);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL+1),
                   (SCSI_UEXACT8)(hiob->scbNumber >> 8));
            }

            /* Put Target Reset SCB at the head of the TEQ */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)),
                (SCSI_UEXACT8)scbNumber);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1),
                (SCSI_UEXACT8)(scbNumber >> 8));

            /* Set flag that requires to remove Target Reset SCB at the end of*/
            /* TEQ because the SCB was appended by sequencer after DAMed in.  */
            mendTEQ = SCSI_TRUE;
            
            break;
         }

         /* Keep previous SCB */
         prevScb = scbNumber;

         /* Advance to next SCB in common execution queue */
         scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                        OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next))) |
                     ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                        OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1))) << 8);
      } /* while loop */
   }

   if (mendTEQ == SCSI_TRUE)
   {
      /* Target Reset SCB must be queued at the end of TEQ after   */
      /* sequencer DMAed it into SCB array.  We need to remove it. */
      /* Search through TEQ for Target Reset SCB.                  */
      /* When it is found, update 'q_exetarg_next' entry in SCB    */
      /* that points to Target Reset SCB to NULL and update TEQ    */
      /* tail to point to this SCB.                                */

      /* Point to Target Reset SCB to get the next SCB in TEQ */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
          (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
          (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      /* Get next SCB in TEQ */
      prevScb = scbNumber =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next))) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1))) << 8);

      /* Search through TEQ until we find Target Reset SCB */
      while (scbNumber != hiob->scbNumber)
      {
         /* Keep previous SCB */
         prevScb = scbNumber;

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbNumber >> 8));

         /* Advance to next SCB in TEQ */
         scbNumber =
            (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
               OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next))) |
            ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
               OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1))) << 8);
      }

      /* Update the entry in q_exetarg_next to NULL */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1),
          (SCSI_UEXACT8)(SCSI_NULL_SCB));

      /* Update the entry in q_exetarg_tail table */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID),
          (SCSI_UEXACT8)prevScb);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID+1),
          (SCSI_UEXACT8)(prevScb >> 8));
   }

   /* Set Target_Reset state */
   hiob->SCSI_IP_mgrStat = SCSI_MGR_TR;

   /* Restore mode 3 SCBPTR */   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)savedScbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(savedScbptr >> 8));

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardU320QExeTargNextOffset
*
*     Return the byte offset of SCSI_SU320_q_exetarg_next SCB field
*
*  Return Value:  SCSI_SU320_q_exetarg_next SCB field offset
*                  
*  Parameters:    none
*
*  Remarks:
*                  
*********************************************************************/
#if SCSI_STANDARD_U320_MODE
SCSI_UEXACT8 SCSIhStandardU320QExeTargNextOffset (void)
{
   return ((SCSI_UEXACT8)OSDoffsetof(SCSI_SCB_STANDARD_U320,
                                     SCSI_SU320_q_exetarg_next));
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardEnhU320QExeTargNextOffset
*
*     Return the byte offset of SCSI_SUE320_q_exetarg_next SCB field
*
*  Return Value:  SCSI_SUE320_q_exetarg_next SCB field offset
*                  
*  Parameters:    none
*
*  Remarks:
*                  
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
SCSI_UEXACT8 SCSIhStandardEnhU320QExeTargNextOffset (void)
{
   return ((SCSI_UEXACT8)OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                     SCSI_SEU320_q_exetarg_next));
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardU320QNextOffset
*
*     Return the byte offset of SCSI_SU320_q_next SCB field
*
*  Return Value:  SCSI_SU320_q_next SCB field offset
*                  
*  Parameters:    none
*
*  Remarks:
*                  
*********************************************************************/
#if SCSI_STANDARD_U320_MODE
SCSI_UEXACT8 SCSIhStandardU320QNextOffset (void)
{
   return ((SCSI_UEXACT8)OSDoffsetof(SCSI_SCB_STANDARD_U320,
                                     SCSI_SU320_q_next));
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardEnhU320QNextOffset
*
*     Return the byte offset of SCSI_SUE320_q_next SCB field
*
*  Return Value:  SCSI_SUE320_q_next SCB field offset
*                  
*  Parameters:    none
*
*  Remarks:
*                  
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
SCSI_UEXACT8 SCSIhStandardEnhU320QNextOffset (void)
{
   return ((SCSI_UEXACT8)OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                     SCSI_SEU320_q_next));
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */





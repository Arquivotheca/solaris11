/*$Header: /vobs/u320chim/src/chim/hwm/hwmtask.c   /main/117   Thu Aug 21 17:31:08 2003   luu $*/

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
*  Module Name:   HWMTASK.C
*
*  Description:
*                 Codes to implement task handler for hardware management 
*                 module
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         NONE
*
*  Entry Point(s):
*                 SCSIHSetUnitHandle
*                 SCSIHFreeUnitHandle
*                 SCSIHDisableIRQ
*                 SCSIHEnableIRQ
*                 SCSIHPollIRQ
*                 SCSIHQueueHIOB
*                 SCSIHQueueSpecialHIOB
*                 SCSIHSaveState
*                 SCSIHRestoreState
*                 SCSIHPowerManagement
*                 SCSIHCheckHostIdle
*                 SCSIHCheckDeviceIdle
*                 SCSIHSuppressNegotiation
*                 SCSIHForceNegotiation
*                 SCSIHGetHWInformation
*                 SCSIHPutHWInformation
*                 SCSIHEnableExpStatus
*                 SCSIHDisableExpStatus
*
***************************************************************************/

#include "scsi.h"
#include "hwm.h"

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/* Modules as interface to hardware management layer                      */ 
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

/*********************************************************************
*
*  SCSIHSetUnitHandle
*
*     Validate unit handle for specified device control block
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 target unit control block
*                 scsi id
*                 logical unit number
*
*  Remarks:       Validated device handle is used to indicate HIOB
*                 destination. After protocol automatic configuration
*                 OSD may examine device table (embedded in SCSI_HHCB)
*                 and find out the device/unit it interested in. This
*                 routine is for OSD to acquire a unit handle which
*                 is required fir building SCSI_HIOB. After protocol
*                 automatic configuration all unit handles become
*                 inlidated automatically.
*                  
*********************************************************************/
void SCSIHSetUnitHandle (SCSI_HHCB SCSI_HPTR hhcb,
                         SCSI_UNIT_CONTROL SCSI_UPTR targetUnit,
                         SCSI_UEXACT8 scsiID,
                         SCSI_UEXACT8 lunID)
{
   /* associate target unit id with particular unit */
   targetUnit->hhcb = hhcb;
   targetUnit->scsiID = scsiID;
   targetUnit->lunID = lunID;
   targetUnit->deviceTable = &SCSI_DEVICE_TABLE(hhcb)[scsiID];
}

/*********************************************************************
*
*  SCSIHFreeUnitHandle
*
*     Invalidate specified unit handle
*
*  Return Value:  none
*                  
*  Parameters:    unit handle
*
*  Remarks:       After invalidated the unit handle should not
*                 be used in HIOB. Whenever there is a validated unit
*                 handle which is not in OSD's interest it can
*                 invalidate unit handle through this routine.
*                  
*********************************************************************/
#if !SCSI_EFI_BIOS_SUPPORT
void SCSIHFreeUnitHandle (SCSI_UNIT_HANDLE unitHandle)
{
   /* nothing need to be done for now */
}
#endif /* !SCSI_EFI_BIOS_SUPPORT */
/*********************************************************************
*
*  SCSIHDisableIRQ
*
*     Disable device hardware interrupt
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIHDisableIRQ (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

#if !SCSI_DCH_U320_MODE
   if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
   {
      /* Harpoon2 Rev A has a problem after writing CHIPRST bit to HCNTRL  */
      /* register to generate chip reset.  The problem is that Harpoon2's  */
      /* parity check logic is still running which causes the chip to      */
      /* drive wrong PERR# or SERR# to pci bus.  The details of this       */
      /* hardware problem can be found in the Razor database entry #630.   */
      /* The workaround is to make sure not to write CHIPRST bit to        */
      /* HCNTRL register here.                                             */

      /* program hardware to disable interrupt */
      SCSI_hWRITE_HCNTRL(hhcb, OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & ~(SCSI_INTEN | SCSI_CHIPRST));
   }
   else
   {
      /* program hardware to disable interrupt */
      SCSI_hWRITE_HCNTRL(hhcb, OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & ~SCSI_INTEN);
   }
#else      

   SCSI_UEXACT8 hcntrl;
   /* 
      For now just turn everything off. Later we may wish to create an interrupt 
      mask to control each interrupt.
    */  
    
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);      
   }

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_HSTINTSTATEN), SCSI_HSTINTSTATMASKOFF);
   
   SCSIhUnPause(hhcb);
   
#endif /* !SCSI_DCH_U320_MODE */

   OSD_SYNCHRONIZE_IOS(hhcb);
}

/*********************************************************************
*
*  SCSIHEnableIRQ
*
*     Enable device hardware interrupt
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIHEnableIRQ (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;

#if !SCSI_DCH_U320_MODE
   if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
   {
      /* Harpoon2 Rev A has a problem after writing CHIPRST bit to HCNTRL  */
      /* register to generate chip reset.  The problem is that Harpoon2's  */
      /* parity check logic is still running which causes the chip to      */
      /* drive wrong PERR# or SERR# to pci bus.  The details of this       */
      /* hardware problem can be found in the Razor database entry #630.   */
      /* The workaround is to make sure not to write CHIPRST bit to        */
      /* HCNTRL register here.                                             */

      hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));
      hcntrl &= ~SCSI_CHIPRST;
      hcntrl |= SCSI_INTEN;

      /* program hardware to enable interrupt */
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
   else
   {
      /* program hardware to enable interrupt */
      SCSI_hWRITE_HCNTRL(hhcb, OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) | SCSI_INTEN);
   }
#else    

   /* 
      For now just turn everything on. Later we may wish to create an interrupt 
      mask to control each interrupt.
    */  
    
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);      
   }

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_HSTINTSTATEN), SCSI_HSTINTSTATMASK);
   
   SCSIhUnPause(hhcb);
  
#endif /* !SCSI_DCH_U320_MODE */

   OSD_SYNCHRONIZE_IOS(hhcb);
}

/*********************************************************************
*
*  SCSIHPollIRQ
*
*     Poll if interrupt pending for associated device hardware
*
*  Return Value:  0        - nothing pending 
*                 non-Zero - interrupt pending
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
SCSI_UINT8 SCSIHPollIRQ (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   /* poll hardware interrupt status */
   return((SCSI_UINT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT)) &
                       (SCSI_HSTINTSTAT_MASK | SCSI_CMDCMPLT)));
}

/*********************************************************************
*
*  SCSIHQueueHIOB 
*
*     Queue iob to sequencer/hardware queue
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:
*                  
*********************************************************************/
/* @@@@ do later - additional inline code needed to do this #if !SCSI_STREAMLINE_QIOPATH */
void SCSIHQueueHIOB (SCSI_HIOB SCSI_IPTR hiob)
{

#if SCSI_BIOS_SUPPORT
   register SCSI_REGISTER scsiRegister;
#endif /* SCSI_BIOS_SUPPORT */

   SCSI_HHCB SCSI_HPTR hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;

   /* fresh the hiob status and working area */
   SCSI_hFRESH_HIOB(hiob);

#if SCSI_STANDARD_MODE
   /* Set up active pointer. */
   
   SCSI_ACTPTR[hiob->scbNumber] = hiob;
   
   /* Assign scb buffer descriptor when necessary */
   SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob);
   
#elif !SCSI_BIOS_SUPPORT
   SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET] = hiob;
   /* Assign scb buffer descriptor when necessary */
   SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob);

#else  

   scsiRegister = hhcb->scsiRegister;
   
   SCSIHWriteScratchRam ((SCSI_HHCB SCSI_HPTR) hhcb,
     (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ACTIVE_HIOB),
     (SCSI_UEXACT16) 4,
     (SCSI_UEXACT8 SCSI_IPTR) &hiob);

#endif /* SCSI_STANDARD_MODE */

   /* deliver it to sequencer */
   SCSI_hDELIVERSCB(hhcb, hiob);

   OSD_SYNCHRONIZE_IOS(hhcb);
}
/* @@@@ do later - additional inline code needed to do this #endif !SCSI_STREAMLINE_QIOPATH */

/*********************************************************************
*
*  SCSIHQueueSpecialHIOB
*
*     Queue special iob to head of sequencer/hardware queue
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine will process all special request
*                 through HIOB.
*                  
*********************************************************************/
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
void SCSIHQueueSpecialHIOB (SCSI_HIOB SCSI_IPTR hiob)
{
#if SCSI_EFI_BIOS_SUPPORT
   register SCSI_REGISTER scsiRegister;
   SCSI_UEXACT8 hcntrl;
#endif /* SCSI_EFI_BIOS_SUPPORT */
   SCSI_HHCB SCSI_HPTR hhcb;

#if SCSI_TARGET_OPERATION
   OSDDebugPrint(8,("\n Entered SCSIHQueueSpecialHIOB "));
   OSDDebugPrint(8,("\n hiob->cmd = %x ",hiob->cmd));
#endif /* SCSI_TARGET_OPERATION */

   /* fresh the hiob status and working area */
   SCSI_hFRESH_HIOB(hiob);

   switch(hiob->cmd)
   {
#if (SCSI_DOWNSHIFT_MODE)
#if (!SCSI_BIOS_SUPPORT)

      case SCSI_CMD_ABORT_TASK:
         hhcb = SCSI_TARGET_UNIT(SCSI_RELATED_HIOB(hiob))->hhcb;
         hiob->stat = SCSI_SCB_INV_CMD;
         SCSI_COMPLETE_SPECIAL_HIOB(hiob);
         break;

      case SCSI_CMD_ABORT_TASK_SET:
#if !SCSI_EFI_BIOS_SUPPORT
      case SCSI_CMD_RESET_TARGET:
      case SCSI_CMD_LOGICAL_UNIT_RESET:
#endif /* !SCSI_EFI_BIOS_SUPPORT */
         hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;
         hiob->stat = SCSI_SCB_INV_CMD;
         SCSI_COMPLETE_SPECIAL_HIOB(hiob);
         break;
#if SCSI_EFI_BIOS_SUPPORT


      case SCSI_CMD_RESET_TARGET:
      case SCSI_CMD_LOGICAL_UNIT_RESET:

       hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;
       SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET] = hiob;

       scsiRegister = hhcb->scsiRegister;

       hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

       if (!(hcntrl & SCSI_PAUSEACK))
       {
          SCSIhPauseAndWait(hhcb);      
       }

      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND)) | SCSI_DU320_TARGETRESET_BIT));

      /* Restore HCNTRL if necessary. */
      if (!(hcntrl & SCSI_PAUSEACK))
      {
         SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
      } 
       
	   break;
#endif /* SCSI_EFI_BIOS_SUPPORT */
#endif /* !SCSI_BIOS_SUPPORT */
#else /* !SCSI_DOWNSHIFT_MODE */

      case SCSI_CMD_ABORT_TASK:
#if SCSI_TARGET_OPERATION
         if (SCSI_RELATED_HIOB(hiob)->SCSI_IP_targetMode)
         {
            /* Must be a target mode Establish Connection being
             * aborted.
             */
            hhcb = SCSI_NEXUS_UNIT(SCSI_RELATED_HIOB(hiob))->hhcb;
         }
         else
#endif /* SCSI_TARGET_OPERATION */
         {
            hhcb = SCSI_TARGET_UNIT(SCSI_RELATED_HIOB(hiob))->hhcb;
         } 
         SCSIhAbortTask(hiob);
         break;

      case SCSI_CMD_ABORT_TASK_SET:
         hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;
         SCSIhAbortTaskSet(hiob);
         break;

      case SCSI_CMD_RESET_TARGET:
      case SCSI_CMD_LOGICAL_UNIT_RESET:
         /* Perform Target_Reset or Logical Unit request */
         hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;

         /* Process Target Reset HIOB.                                    */
         /* For non-packetized mode:                                      */
         /*   The related TargetID HIOB(s) will be aborted when sequencer */
         /*   informed CHIM that the Target Reset message (0x0C) is ready */
         /*   to send to the target.  This is done in SCSIhAbortTarget(). */
         /* For packetized mode:                                          */
         /*   The related TargetID HIOB(s) will be aborted when sequencer */
         /*   delivered the completion of Target Reset HIOB.  This is     */
         /*   done in SCSIhTerminateCommand().  */
         SCSI_hSETUPTARGETRESET(hhcb, hiob);
         break;
#endif /* SCSI_DOWNSHIFT_MODE */

      case SCSI_CMD_RESET_BUS:
         /* Perform Ssci_Bus_Reset request */
         hhcb = SCSI_INITIATOR_UNIT(hiob);
         SCSIhScsiBusReset(hhcb,hiob);

         /* Post reset bus done */
         SCSI_COMPLETE_SPECIAL_HIOB(hiob);
         break;
#if !SCSI_BIOS_SUPPORT
      case SCSI_CMD_RESET_HARDWARE:
#if (!SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING && !SCSI_DCH_U320_MODE)     
         /* Check for PCI or PCI-X recovery inprogress */
         hhcb = SCSI_INITIATOR_UNIT(hiob);

         if (hhcb->SCSI_HP_hstintstat & (SCSI_PCIINT | SCSI_SPLTINT))
         {
            SCSIhPCIReset(hhcb);
         }
#else /* SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */
         hhcb = SCSI_NULL_HHCB;
#endif /* (!SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING && !SCSI_DCH_U320_MODE) */

         /* Set success status and post back. */
         hiob->stat = SCSI_SCB_COMP;
         SCSI_COMPLETE_SPECIAL_HIOB(hiob);
         break;

#if SCSI_TARGET_OPERATION
      /* Target mode only commands. */       
      case SCSI_CMD_PROTO_AUTO_CFG:
         /* As this is not a performance critical
          * request we can afford a check on
          * target mode enabled. Also, this request
          * used to be supported in initiator mode.
          */
         hhcb = SCSI_INITIATOR_UNIT(hiob);
         if (hhcb->SCSI_HF_targetMode)
         {
            hhcb = SCSI_INITIATOR_UNIT(hiob);
            SCSI_hTARGETMODEENABLE(hhcb);
            hiob->stat = SCSI_SCB_COMP;
         }
         else
         {
            /* This request is invalid if target 
             * mode is not enabled.
             */
            hiob->stat = SCSI_SCB_INV_CMD;
         }
         SCSI_COMPLETE_SPECIAL_HIOB(hiob);
         break;
         
      case SCSI_CMD_ESTABLISH_CONNECTION:
         hhcb = SCSI_NEXUS_UNIT(hiob)->hhcb;
         
         /* Initialize certain nexus fields */
         SCSI_hFRESH_NEXUS(SCSI_NEXUS_UNIT(hiob));

         /* setup active pointer */
         SCSI_ACTPTR[hiob->scbNumber] = hiob;

         /* Set target HIOB indicator */
         SCSI_hTARGETMODE_HIOB(hiob);

         /* Assign scb buffer descriptor when necessary */
         SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob);
   
         /* Deliver Establish SCB */
         SCSI_hTARGETDELIVERESTSCB(hhcb, hiob);
         break; 

      case SCSI_CMD_REESTABLISH_INTERMEDIATE:
         hhcb = SCSI_NEXUS_UNIT(hiob)->hhcb;
         SCSIhTargetSendHiob(hhcb,hiob);
         break; 

      case SCSI_CMD_REESTABLISH_AND_COMPLETE:
         hhcb = SCSI_NEXUS_UNIT(hiob)->hhcb;
         if (SCSI_NEXUS_UNIT(hiob)->SCSI_XP_taskManagementFunction)
         {  
             SCSI_hFRESH_HIOB(hiob);         
#if SCSI_SCBBFR_BUILTIN
             /* Assign the bus held scb descriptor so that it may be
              * freed correctly.
              */
             hiob->scbDescriptor = hhcb->SCSI_HP_busHeldScbDescriptor;
             hhcb->SCSI_HP_busHeldScbDescriptor = SCSI_NULL_SCB_DESCRIPTOR;
             /* Free the SCB descriptor. */
             SCSI_hFREESCBDESCRIPTOR(hhcb,hiob);
#endif /* SCSI_SCBBFR_BUILTIN */

             /* Update the NEXUS info */ 
             SCSI_NEXUS_UNIT(hiob)->SCSI_XF_busHeld = 0;

             if ((SCSI_NEXUS_UNIT(hiob)->SCSI_XP_deviceResetInProgress) &&
                 (!hhcb->SCSI_HF_initiatorMode) &&
                 (hhcb->SCSI_HF_targetNumIDs <= 1))            
             {  /* if target reset then we should re-initialize the
                 * hardware as long as initiator mode is not enabled
                 * and we're active as multiple Ids for this adapter
                 */
                SCSIhTargetResetTarget(hhcb);
             }      
             else
             {
                SCSIhTargetScsiBusFree(hhcb);
                SCSIhTargetRedirectSequencer(hhcb); 
             }
  
             /* set the correct status */
             SCSIhTargetSetStat(hiob);
             /* Complete the request */
             SCSI_COMPLETE_SPECIAL_HIOB(hiob);
         }       
         else
         {
            SCSIhTargetSendHiob(hhcb,hiob); 
         }
         break; 
         
     case SCSI_CMD_ABORT_NEXUS:
         hhcb = SCSI_NEXUS_UNIT(hiob)->hhcb;
         SCSIhTargetAbortNexusSet(hiob);
         break;
         
#if SCSI_MULTIPLEID_SUPPORT
      case SCSI_CMD_ENABLE_ID:
         hhcb = SCSI_INITIATOR_UNIT(hiob);
         SCSIhTargetEnableId(hiob);
         break;

      case SCSI_CMD_DISABLE_ID:
         hhcb = SCSI_INITIATOR_UNIT(hiob); 
         SCSIhTargetSetupDisableId(hiob);
         break; 
#endif /* SCSI_MULTIPLEID_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */    
#endif /* !SCSI_BIOS_SUPPORT */
      default:
         hhcb = SCSI_NULL_HHCB;
         return;
   }

   /* flush the register access */
   OSD_SYNCHRONIZE_IOS(hhcb);


}
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */

#if !SCSI_DCH_U320_MODE
/*********************************************************************
*
*  SCSIHSaveState
*
*     Save current hardware/sequencer state 
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 state
*
*  Remarks:       This routine will save whatever current harwdare
*                 into the state structure. It's caller's responbility
*                 to make sure state saving is necessary.
*                  
*********************************************************************/
#if SCSI_SAVE_RESTORE_STATE
void SCSIHSaveState (SCSI_HHCB SCSI_HPTR hhcb,
                     SCSI_STATE SCSI_HPTR state)
{

   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   SCSI_UINT16 i,j;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT16 savedScbptr;
   SCSI_UEXACT8 scbsize;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 seqctl;
   SCSI_UEXACT8 savedMode;

   /* save the HCNTRL value */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

#if POWER_MANAGEMENT_SAVE_RESTORE   
   state->hcntrl = hcntrl;
#endif /* POWER_MANAGEMENT_SAVE_RESTORE */  

   /* pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      state->SCSI_SF_chipPause = 0;
      SCSIhPauseAndWait(hhcb);
   }
   else
   {
      state->SCSI_SF_chipPause = 1;
   }

   if ((SCSIHBiosState(hhcb) == SCSI_BIOS_ACTIVE) ||
       (SCSIHRealModeExist(hhcb) == SCSI_EXIST))
   {
      state->SCSI_SF_realMode = 1;
   }
   else
   {
      state->SCSI_SF_realMode = 0;
   }

#if !POWER_MANAGEMENT_SAVE_RESTORE   
   if (!(hcntrl & SCSI_INTEN))
   {
      state->SCSI_SF_interruptEnable = 0;
   }
   else
   {
      state->SCSI_SF_interruptEnable = 1;
   }
#endif /* !POWER_MANAGEMENT_SAVE_RESTORE */

   /* Save the MODE_PTR register */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for most IO accesses. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

#if SCSI_TARGET_OPERATION  
   /* Save state of the enable selection for target mode */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ1)) & SCSI_ENSELI) 
   {
      state->SCSI_SF_selectionEnable = 1;
   }
   else
   {
      state->SCSI_SF_selectionEnable = 0;
   }
#endif /* SCSI_TARGET_OPERATION */   

   /* Save independent scratch ram area into the state structure */
   for (i = 0; i < SCSI_SCRATCH0_SIZE; i++)
   {
      state->scratch0[i] = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCRATCH0+i));
   }

   /* Save dependent scratch ram 1 & 2 area into the state structure */
   for (i = 0; i < 4; i++)
   {
      /* Switch the mode to 0, 1, 2 and 3 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,(SCSI_UEXACT8)((i << 4) | i));

      /* Save dependent scratch ram 1 area into the state structure */
      for (j = 0; j < SCSI_SCRATCH1_SIZE; j++)
      {
         state->scratch1[i][j] =
            OSD_INEXACT8(SCSI_AICREG((SCSI_UEXACT8)(SCSI_SCRATCH1+j)));
      }

      /* Save dependent scratch ram 2 area into the state structure */
      for (j = 0; j < SCSI_SCRATCH2_SIZE; j++)
      {
         state->scratch2[i][j] =
            OSD_INEXACT8(SCSI_AICREG((SCSI_UEXACT8)(SCSI_SCRATCH2+j)));
      }
   }

   /* Switch to mode 4 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   /* Save the optionmode register */
   state->optionMode = OSD_INEXACT8(SCSI_AICREG(SCSI_OPTIONMODE));

#if SCSI_PACKETIZED_IO_SUPPORT
   /* These registers are only required for packetized operation. */
   state->lunptr = OSD_INEXACT8(SCSI_AICREG(SCSI_LUNPTR));
   state->cdblenptr = OSD_INEXACT8(SCSI_AICREG(SCSI_CDBLENPTR));
   state->attrptr = OSD_INEXACT8(SCSI_AICREG(SCSI_ATTRPTR));
   state->flagptr = OSD_INEXACT8(SCSI_AICREG(SCSI_FLAGPTR));
   state->cdbptr = OSD_INEXACT8(SCSI_AICREG(SCSI_CDBPTR));
   state->qnextptr = OSD_INEXACT8(SCSI_AICREG(SCSI_QNEXTPTR));
   state->abrtbyteptr = OSD_INEXACT8(SCSI_AICREG(SCSI_ABRTBYTEPTR));
   state->abrtbitptr = OSD_INEXACT8(SCSI_AICREG(SCSI_ABRTBITPTR));
   state->lunlen = OSD_INEXACT8(SCSI_AICREG(SCSI_LUNLEN));
   state->maxcmd = OSD_INEXACT8(SCSI_AICREG(SCSI_MAXCMD));
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

   /* Save SCB auto pointer */
   state->scbautoptr = OSD_INEXACT8(SCSI_AICREG(SCSI_SCBAUTOPTR));

   /* Save ARP Interrupt #1 Vector */
   state->intvect1Addr0 = OSD_INEXACT8(SCSI_AICREG(SCSI_INTVECT1_ADDR0));
   state->intvect1Addr1 = OSD_INEXACT8(SCSI_AICREG(SCSI_INTVECT1_ADDR1));

   /* Save command channel RAM BIST - contains SG element size */
   state->cmcRamBist = OSD_INEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST));

#if SCSI_STANDARD_U320_MODE
   /* Since the definition of LQOSTOP0 has changed for Harpoon2 Rev B     */
   /* ASIC, the STANDARD_U320 mode sequencer will not run in Rev B.  In   */
   /* Rev A, the LQOSTOP0 bit was cleared when LQOBUSFREE1 interrupt is   */
   /* generated by the ASIC, but the bit is left ON in the case of Rev B. */
   /* This breaks 'select_done' logic in the STANDARD_U320 mode sequencer.*/
   /* To make the logic work, we need to put the Harpoon2 Rev B ASIC in   */
   /* the backward compatible mode. */
   if ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) &&
       (SCSI_hHARPOON_REV_B(hhcb)))
   {
      /* Save LQO SCSI control - LQOSCSCTL */
      state->lqoScsCtl = OSD_INEXACT8(SCSI_AICREG(SCSI_LQOSCSCTL));
   }
#endif /* SCSI_STANDARD_U320_MODE */

   /* When running on Harpoon 2 Rev B, sequencer generates data overrun */
   /* at the end of the command if there is a Wide Residue byte left in */
   /* the data FIFO and the target does not issue ignore wide residue   */
   /* message.  The behavior is due to the H2B change where ASIC does   */
   /* not report the emptied fifo in this condition.                    */
   /* To make the logic work, we need to put the Harpoon2 Rev B ASIC in */
   /* the backward compatible mode. */
   if (SCSI_hHARPOON_REV_B(hhcb))
   {
      /* Save SCSCHKN */
      state->scsChkn = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSCHKN));
   }

   /* Switch to mode 2 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);

   /* Save the queue offset control & status register */
   state->qoffCtlsta = OSD_INEXACT8(SCSI_AICREG(SCSI_QOFF_CTLSTA));

   /* Save the arp new SCB queue offset registers */
   state->snscbQoff = SCSIhGetArpNewSCBQOffset(hhcb);

   /* Save the arp done SCB queue offset register */
   /* NOTE : The upper byte of the register should be read first because if  */
   /* the register had the value of, say, 0x4FF and we read the low byte     */
   /* first (which is 0xFF), the hardware automatically increments the value */
   /* to 0x500 before we could read the upper byte.  So, the value we would  */
   /* get when we read the upper byte would be 0x5, resulting in a value of  */
   /* 0x5FF, instead of the correct value of 0x4FF.  */
   state->sdscbQoff =
      (((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF+1))) << 8);
   state->sdscbQoff += (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF));

   /* As the SDSCB_QOFF register auto increments on a read, we need to */
   /* write back what we read (this will adjust the SDSCB_ROLLOVER bit */
   /* in the QOFF_CTLSTA register automatically, if needed).           */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF),(SCSI_UEXACT8)state->sdscbQoff);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF+1),(SCSI_UEXACT8)(state->sdscbQoff >> 8));

   /* Switch to mode 3 - a known mode for most IO accesses. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Save the host new SCB queue offset register */
   state->hnscbQoff = OSD_INEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF));

   if ((!state->SCSI_SF_realMode) && (hhcb->SCSI_HF_chipReset))
   {
      /* Save initiator own id and target own id, has not been initialized */
      state->iownid = hhcb->hostScsiID; 
      state->townid = hhcb->hostScsiID; 
   }
   else
   {
      /* Save initiator own id and target own id */
      state->iownid = (OSD_INEXACT8(SCSI_AICREG(SCSI_IOWNID)) & SCSI_IOID);
      state->townid = (OSD_INEXACT8(SCSI_AICREG(SCSI_TOWNID)) & SCSI_TOID);
   }

   /* Save PCI configuration space register information */
   state->statusCommand = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_STATUS_CMD_REG);
   state->cacheLatencyHeader = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_CACHE_LAT_HDR_REG);
   state->devConfig0 = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_DEVCONFIG0_REG);
   state->devConfig1 = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_DEVCONFIG1_REG);

   /* Save device space register information */

   /* Switch to mode 4 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   state->simode0 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE0));
   state->simode1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE1));
   state->simode2 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE2));
   state->simode3 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE3));
   state->lqimode0 = OSD_INEXACT8(SCSI_AICREG(SCSI_LQIMODE0));
   state->lqimode1 = OSD_INEXACT8(SCSI_AICREG(SCSI_LQIMODE1));
   state->lqctl0 = OSD_INEXACT8(SCSI_AICREG(SCSI_LQCTL0));
   state->dffthrsh = OSD_INEXACT8(SCSI_AICREG(SCSI_DFF_THRSH));
   state->dscommand0 = OSD_INEXACT8(SCSI_AICREG(SCSI_DSCOMMAND0));
   state->pcixctl = OSD_INEXACT8(SCSI_AICREG(SCSI_PCIXCTL));

   /* Switch to mode 3 - a known mode for most IO accesses. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   state->lqctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_LQCTL1));
   state->lqctl2 = OSD_INEXACT8(SCSI_AICREG(SCSI_LQCTL2));
   state->scsiseq0 = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0));
   state->scsiseq1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ1));
   state->sxfrctl0 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL0));
   state->sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
   state->seqctl0 = OSD_INEXACT8(SCSI_AICREG(SCSI_SEQCTL0));
   state->seqctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SEQCTL1));
   state->seqintctl = OSD_INEXACT8(SCSI_AICREG(SCSI_SEQINTCTL));
   state->intctl = OSD_INEXACT8(SCSI_AICREG(SCSI_INTCTL));
   state->sblkctl = OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL));

   seqctl = OSD_INEXACT8(SCSI_AICREG(SCSI_SEQCTL0));  /* Save SEQCTL0 */
   
   /* Save the sequencer ram into the state structure */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_OVLYADDR0), 00);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_OVLYADDR1), 00);

   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0),
                    (SCSI_FASTMODE + SCSI_PERRORDIS + SCSI_LOADRAM));
   }
   else
   {
      /* FASTMODE and LOADRAM has been removed in Harpoon 2 Rev B */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0), SCSI_PERRORDIS);
   }

   if (!hhcb->SCSI_HF_dontSaveSeqInState)
   {
      /* only save the sequencer if requested */ 
      for (i = 0; i < SCSI_SEQCODE_SIZE; i++)
      {
         state->seqRam[i] = OSD_INEXACT8(SCSI_AICREG(SCSI_SEQRAM));
      }
   }

   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0), (SCSI_FASTMODE + SCSI_PERRORDIS));
   }

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0), seqctl);  /* Restore SEQCTL */

#if SCSI_STANDARD_MODE
   /* we are running standard mode, so we need to save busy target array. */
   if ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) ||
       (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320))
   {
      /* Save mode 3 SCBPTR value. */
      savedScbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                    ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

      for (scbptr = 0; scbptr < SCSI_hBTASIZE(hhcb); scbptr++)
      {
         /* Set SCBPTR to scbptr. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(scbptr >> 8));

         /* Save busy target entry. */
         state->Bta[scbptr] =
            OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_hBTATABLE(hhcb)));
      }

      /* Restore mode 3 SCBPTR. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)savedScbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(savedScbptr >> 8));
   }
   else
   if (hhcb->firmwareMode == SCSI_FMODE_DCH_U320)
   {
      ;/* undetermined if the save and restore will be used with DCH platform */   
   }   
#endif /* SCSI_STANDARD_MODE */

   /* By default the scbsize is 64 bytes. */
   scbsize = 64;

   /* Save scb #FF information. */

   /* Save mode 3 SCBPTR value. */
   savedScbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                 ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);
   
   /* Load scb ptr with 0xFF */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)SCSI_U320_SCBFF_PTR);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(SCSI_U320_SCBFF_PTR >> 8));

   for (i = 0; i < scbsize; i++)
   {                 
      state->scbFF[i] = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+i));
   }

   /* Restore mode 3 SCBPTR. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)savedScbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(savedScbptr >> 8));

   /* Reset Sequencer Start address */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0), 
                 (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1), 
                 (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  

   /* restore HCNTRL if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
   
   OSD_SYNCHRONIZE_IOS(hhcb);
}
#endif /* SCSI_SAVE_RESTORE_STATE */

/*********************************************************************
*
*  SCSIHRestoreState
*
*     Apply state structure to restore hardware/sequencer state 
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 state
*
*  Remarks:       This routine will restore state specified by
*                 state structure to harwdare/sequencer.
*                 It's caller's responbility to make sure state 
*                 saving is necessary.
*                  
*********************************************************************/
#if SCSI_SAVE_RESTORE_STATE
void SCSIHRestoreState (SCSI_HHCB SCSI_HPTR hhcb,
                        SCSI_STATE SCSI_HPTR state)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT16 savedScbptr;
   SCSI_UEXACT16 negoOnModeSwitch = 0;
#if SCSI_IROC
   SCSI_UEXACT16 savedSwitchPkt = 0;
#endif /* SCSI_IROC */
   SCSI_UEXACT8 i, j;
   SCSI_UEXACT8 scbSize;
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT8 hcntrl;

   /* save the HCNTRL value */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* For Cardbus (APA-1480x) we need to turn off the Power Down mode */
   /* before we can access to any chip's registers.                   */
   /* For power management support, we need to clear Chip Reset bit.  */
   /* Otherwise, the SCSIhPauseAndWait() might reset the chip again.  */
   if ((hcntrl & SCSI_POWRDN) || (hcntrl & SCSI_CHIPRST))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL),(hcntrl & ~(SCSI_POWRDN | SCSI_CHIPRST)));
   }

   /* Pause sequencer */
   SCSIhPauseAndWait(hhcb);

   if (SCSI_hHARPOON_REV_B(hhcb))
   {
      /* The following is a workaround for Rev B of Harpoon2. This is to   */
      /* resolve a power management problem under Win2K environment.       */
      /* The problem happens when the system is put into standby mode.     */
      /* When the chip is in power management state transition from D3     */
      /* (power down state) to D0 (normal state), Harpoon2 RevB ASIC       */
      /* does not set CHIPRST bit in HCNTRL register.  This causes CHIM    */
      /* code not to set SCSI_HF_chipReset correctly and does not          */
      /* initialize some chip registers as they should be.  The details of */
      /* this hardware issue can be found in the Razor database entry #913.*/

      /* A workaround is implemented to check PCIERRGENDIS bit in          */
      /* PCIERRGEN register to help to decide if chip reset has happened.  */

      if (!(hcntrl & SCSI_CHIPRST))
      {
         /* Save the current mode and set to mode 4 */
         savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

         if (!(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIERRGEN_REG)) & SCSI_PCIERRGENDIS))
         {
            hcntrl |= SCSI_CHIPRST;
         }

         /* Restore mode pointer */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
      }
   }

#if SCSI_IROC
   /* The following logic fixes the iROC Windows 2000 Standby issue.     */
   /* Two issues have been addressed in this fix.                        */
   /* Problem #1:                                                        */
   /*   During resume from Standby mode, ATN signal asserts right after  */
   /*   target reselects intiator and before identify message on the     */
   /*   first IO.                                                        */
   /*   Problem details: after the resume from Standby mode, the scsi    */
   /*   port sends down Start Unit command (this command is tagged       */
   /*   command).  The target disconnects.  On reselection, the target   */
   /*   goes to Message Out phase right after Identify message because   */
   /*   the ATN asserted.  Sequencer detects invalid scsi phase (it      */
   /*   expects Message In phase for tag message) and generates          */
   /*   phase_error interrupt. This result in scsi bus reset.            */
   /*   The ATN assertion is caused by the chip is not setup properly as */
   /*   the chip is in reset state after Standby mode.  If CHIM just     */
   /*   restore the HW state is not enough in this case. Thereby, CHIM   */
   /*   have to re-initialize the chip first then restore the chip state.*/
   /*   To re-initialize the chip, CHIM calls SCSIhResetChannelHardware()*/
   /*   directly.  This will invoke all hardware workarounds.            */
   /*   That seems to fix the problem for the single boot drive.  But it */
   /*   fails with the multiple boot drives (RAID 0) which is the problem*/
   /*   #2 below.                                                        */
   /*   Note: there is no Razor# for this problem as CHIM should         */
   /*   initialize Harpoon 2 chip after it has been reset.  Fortunately, */
   /*   the state restore process has been worked in the previous chips  */
   /*   for standby mode is probably not a lot of hardware workarounds   */
   /*   requirement during intialization.                                */
   /*                                                                    */
   /* Problem #2:                                                        */
   /*   With the fix in problem #1 and during resume from Standby mode,  */
   /*   we see the "false" REQ's triggered by chip to indicate that      */
   /*   target requests for a byte during negotiation message if the     */
   /*   previous connection was a packetized mode.                       */
   /*   This problem is similar to Harpoon2B Razor #714.                 */
   /*   Problem details:  after the resume from Standby mode, the scsi   */
   /*   port sends down Start Unit command (this command is tagged       */
   /*   command with autoSense off) to TID0 and TID1. Both commands      */
   /*   complete normal.  On the next command to TID0 is tagged command  */
   /*   with autoSense on, CHIM will negotiate for packetized mode.      */
   /*   Target takes the command then disconnects.  When the command to  */
   /*   TID1 sent down (it is also tagged command with autoSense on),    */
   /*   CHIM will negotiate for packetized mode.  But the packetized     */
   /*   negotiation never complete.  The SCSI state trace shows that the */
   /*   extended message (0x01) after the queue tag number and follows by*/
   /*   a message of 0x7F.  Target issues message reject and the scsi bus*/
   /*   reset.  From the SCSI timing trace, it shows the data pattern    */
   /*   between the REQ/ACK of 0x01 and 0x7F message bytes as partial    */
   /*   data bytes of PPR message were put on SCSI bus, i.e. 06 04 08 00.*/
   /*   If we're single step through the code then the problem gone away.*/
   /*   After trial for couple solutions, we have came up with the final */
   /*   solution is to call SCSIHIntialize routine and do not restore    */
   /*   the saved hardware state at all if the chip is in reset state.   */
   /*   Two more things we did add into this fix are:                    */
   /*   1. Save deviceTable flag, SCSI_DF_switchPkt, of all targets      */
   /*      before the call to SCSIHInitialize and restore them after.    */
   /*      Because the flag might get set during Standby process.  And   */
   /*      they are initialized to 0 in software reset routine.          */
   /*   2. Set hhcb->SCSI_HF_chipReset to 1 so that the init process will*/
   /*      wait for the bus to stable.                                   */
   if (hcntrl & SCSI_CHIPRST)
   {
      for (i = 0; i < (SCSI_UEXACT8)hhcb->maxDevices; i++)
      {
         deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

         if (deviceTable->SCSI_DF_switchPkt)
         {
            savedSwitchPkt |= (SCSI_UEXACT16)(1 << i);
         }
      }
      hhcb->SCSI_HF_chipReset = 1;
      SCSIHInitialize(hhcb);
      hhcb->SCSI_HF_chipReset = 0;
      for (i = 0; i < (SCSI_UEXACT8)hhcb->maxDevices; i++)
      {
         deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

         if (savedSwitchPkt & ((SCSI_UEXACT16)(1 << i)))
         {
            deviceTable->SCSI_DF_switchPkt = 1;
         }
      }
      return;
   }
#endif /* SCSI_IROC */

   /* Save the MODE_PTR register */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for most IO accesses. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   if (hhcb->SCSI_HF_dontSaveSeqInState)
   {
      /* Restore sequencer from initialization array */
      SCSI_hSETUPSEQUENCER(hhcb);
   }
   else
   {
      /* Restore the sequencer ram from the state structure */
      /* Since there is loader code to be loaded to run DMA */
      /* the size to be loaded into sequencer ram is up to  */
      /* loader code.                                       */
      /* This needs to be updated if we have different      */
      /* loader code or different scheme.                   */
      SCSI_hLOADSEQUENCER(hhcb,state->seqRam,
                          SCSI_SEQCODE_SIZE-SCSI_SEQLOADER_SIZE);
   } 

   /* Restore independent scratch ram area from the state structure */
   for (i = 0; i < SCSI_SCRATCH0_SIZE; i++)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCRATCH0+i),state->scratch0[i]);
   }

   /* Restore dependent scratch ram 1 & 2 area from the state structure */
   for (i = 0; i < 4; i++)
   {
      /* Switch the mode to 0, 1, 2 and 3 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,(SCSI_UEXACT8)((i << 4) | i));

      /* Restore dependent scratch ram 1 area into the state structure */
      for (j = 0; j < SCSI_SCRATCH1_SIZE; j++)
      {
         OSD_OUTEXACT8(SCSI_AICREG((SCSI_UEXACT8)(SCSI_SCRATCH1+j)),
               state->scratch1[i][j]);
      }

      /* Restore dependent scratch ram 2 area into the state structure */
      for (j = 0; j < SCSI_SCRATCH2_SIZE; j++)
      {
         OSD_OUTEXACT8(SCSI_AICREG((SCSI_UEXACT8)(SCSI_SCRATCH2+j)),
               state->scratch2[i][j]);
      }
   }

   /* Switch to mode 4 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   /* Restore optionmode register */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_OPTIONMODE),state->optionMode);

#if SCSI_PACKETIZED_IO_SUPPORT
   /* These registers are only required for packetized operation. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LUNPTR),state->lunptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CDBLENPTR),state->cdblenptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_ATTRPTR),state->attrptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_FLAGPTR),state->flagptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CDBPTR),state->cdbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_QNEXTPTR),state->qnextptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_ABRTBYTEPTR),state->abrtbyteptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_ABRTBITPTR),state->abrtbitptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LUNLEN),state->lunlen);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_MAXCMD),state->maxcmd);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

   /* Restore SCB auto pointer */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBAUTOPTR),state->scbautoptr);

   /* Restore ARP Interrupt #1 Vector */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_INTVECT1_ADDR0),state->intvect1Addr0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_INTVECT1_ADDR1),state->intvect1Addr1);

   /* Restore command channel RAM BIST - contains SG element size */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST),state->cmcRamBist);

#if SCSI_STANDARD_U320_MODE
   /* Since the definition of LQOSTOP0 has changed for Harpoon2 Rev B     */
   /* ASIC, the STANDARD_U320 mode sequencer will not run in Rev B.  In   */
   /* Rev A, the LQOSTOP0 bit was cleared when LQOBUSFREE1 interrupt is   */
   /* generated by the ASIC, but the bit is left ON in the case of Rev B. */
   /* This breaks 'select_done' logic in the STANDARD_U320 mode sequencer.*/
   /* To make the logic work, we need to put the Harpoon2 Rev B ASIC in   */
   /* the backward compatible mode. */
   if ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) &&
       (SCSI_hHARPOON_REV_B(hhcb)))
   {
      /* Restore LQO SCSI control - LQOSCSCTL */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQOSCSCTL),state->lqoScsCtl);
   }
#endif /* SCSI_STANDARD_U320_MODE */

   /* When running on Harpoon 2 Rev B, sequencer generates data overrun */
   /* at the end of the command if there is a Wide Residue byte left in */
   /* the data FIFO and the target does not issue ignore wide residue   */
   /* message.  The behavior is due to the H2B change where ASIC does   */
   /* not report the emptied fifo in this condition.                    */
   /* To make the logic work, we need to put the Harpoon2 Rev B ASIC in */
   /* the backward compatible mode. */
   if (SCSI_hHARPOON_REV_B(hhcb))
   {
      /* Restore SCSCHKN */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSCHKN),state->scsChkn);
   }

   /* Switch to mode 2 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);

   /* Restore the arp new SCB queue offset register */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF),(SCSI_UEXACT8)state->snscbQoff);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF+1),
                 (SCSI_UEXACT8)(state->snscbQoff >> 8));

   /* Restore the arp done SCB queue offset register */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF),(SCSI_UEXACT8)state->sdscbQoff);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF+1),
                 (SCSI_UEXACT8)(state->sdscbQoff >> 8));

   /* Restore the queue offset control & status register */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_QOFF_CTLSTA),state->qoffCtlsta);

   /* Switch to mode 3 - a known mode for most IO accesses. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Restore the host new SCB queue offset register */
   OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF), state->hnscbQoff);

   /* Restore initiator own id and target own id */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_IOWNID),state->iownid);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_TOWNID),state->townid);
   
   /* Restore PCI configuration space register information */
   OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_STATUS_CMD_REG,state->statusCommand);
   OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_CACHE_LAT_HDR_REG,state->cacheLatencyHeader);
   OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_DEVCONFIG0_REG,state->devConfig0);
   OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_DEVCONFIG1_REG,state->devConfig1);

   /* Restore device space register information */

   /* Switch to mode 4 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE0),state->simode0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE1),state->simode1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE2),state->simode2);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE3),state->simode3);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQIMODE0),state->lqimode0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQIMODE1),state->lqimode1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL0),state->lqctl0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFF_THRSH),state->dffthrsh);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSCOMMAND0),state->dscommand0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCIXCTL),state->pcixctl);

   /* Switch to mode 3 - a known mode for most IO accesses. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL1),state->lqctl1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL2),state->lqctl2);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0),state->scsiseq0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ1),state->scsiseq1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL0),state->sxfrctl0);

   /* It was discovered that the STPWCTL logic used by the BIOS and driver  */
   /* in controlling the on-board SCSI terminators was not properly         */
   /* initialized following a chip reset.  The key here is 'Chip Reset'.    */
   /* Due to the ambiguity of the data book, it was not clear that the      */
   /* STPWCTL output signal can become active only after first writing a '1'*/
   /* to the STPWEN bit (bit 0 of SXFRCTL1 reg.) following a chip reset.    */
   /* Writing the '1' to STPWEN enables the output logic of STPWCTL signal. */
   /* Otherwise the output of STPWCTL remains tri-stated and does not       */
   /* reflect the state of STPWEN as programmed by BIOS and driver.  This   */
   /* might result in over termination problem on the SCSI Bus because the  */
   /* BIOS and driver cannot disable on-board SCSI termination.             */
   /* Therefore, CHIM must write a '1' first to STPWEN bit of SXFRCTL1 reg. */
   /* to enable the tri-state output buffer before setting the state of the */
   /* termination control following a chip reset.                           */
   /* Check if the chip's reset ('hcntrl' value was obtained from the HW    */
   /* at the beginning of the routine.                                      */
   if (hcntrl & SCSI_CHIPRST)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1),SCSI_STPWEN);
   }

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1),state->sxfrctl1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0),state->seqctl0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL1),state->seqctl1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQINTCTL),state->seqintctl);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_INTCTL),state->intctl);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SBLKCTL),state->sblkctl);

#if SCSI_UPDATE_TERMINATION_ENABLE
   /* update termination that controlled by external hardware logic */
   SCSI_hUPDATEEXTTERM(hhcb);
#endif /* SCSI_UPDATE_TERMINATION_ENABLE */

   /* Since the real mode drivers (BIOS, ASPI and Solaris boot driver) will  */
   /* run at maximum scsi transfer rate of 80MB/s and non-packetized.  While */
   /* the protected mode drivers will run at 320MB/s, QAS and Packetized.    */
   /* Therefore, when the driver switches to Real mode from Protected mode   */
   /* and vice versa, the initiator must bring the target back to the correct*/
   /* scsi transfer rate in that mode.  To do that, the CHIM will be         */
   /* implemented to do the following:                                       */
   /*   1. When switch to Real mode, CHIM will update the corresponding bit  */
   /*      for a specific target in scratch RAM, SCSI_NEGO_ON_MODE_SWITCH.   */
   /*      The update is based on either the Real mode driver or Protected   */
   /*      mode driver was running Wide or Sync.                             */
   /*   2. When switch to Protected mode, CHIM will update SCSI_DF_needNego  */
   /*      flag in device table structure.  This SCSI_DF_needNego flag will  */
   /*      tell CHIM to renegotiate on the first I/O for a target.           */
   /*      The update is based on either the Real mode driver or Protected   */
   /*      mode driver was running Wide or Sync.                             */
   if (state->SCSI_SF_realMode)
   {
      /* real mode: set specific target's negoOnModeSwitch bit in scratch */
      /*            RAM, SCSI_NEGO_ON_MODE_SWITCH.                        */
      for (i = 0; i < hhcb->maxDevices; i++)
      {
         deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

         /* We only update the negoOnModeSwitch flag if either the Real mode */
         /* driver or the Protected mode driver was running Wide or Sync.    */
         if ((SCSI_BIOS_REALMODE_XFER(deviceTable)[SCSI_XFER_OFFSET] != 0) ||
             (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_OFFSET] != 0) ||
             (SCSI_BIOS_REALMODE_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE) ||
             (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE))
         {
            /* Need to re-negotiate on mode switch */
            negoOnModeSwitch |= (SCSI_UEXACT16)(1 << i);
         }
      }

      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH),
                                    (SCSI_UEXACT8)negoOnModeSwitch);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH+1),
                                    (SCSI_UEXACT8)(negoOnModeSwitch >> 8));
   }
   else
   {
      /* Force negotiation if HF_SuppressNegoSaveRestoreState is not set */
      if (!(hhcb->SCSI_HF_SuppressNegoSaveRestoreState))
      {
         /* protected mode: set the SCSI_DF_needNego flag and              */
         /*                 SCSI_DF_negoOnModeSwitch flag in device table. */
         for (i = 0; i < hhcb->maxDevices; i++)
         {
            deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

            /* We only update the negoOnModeSwitch flag if either the Real  */
            /* mode driver or the Protected mode driver was running Wide or */
            /* Sync.                                                        */
            if ((SCSI_BIOS_REALMODE_XFER(deviceTable)[SCSI_XFER_OFFSET] != 0) ||
                (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_OFFSET] != 0) ||
                (SCSI_BIOS_REALMODE_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE) ||
                (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE))
            {
               /* Set negotiation needed flag */
               deviceTable->SCSI_DF_needNego = 1;

               /* Set negoOnModeSwitch flag so that CHIM will renegotiate for */
               /* wide even though the transfer rate entries do not support.  */
               /* The reason is to cover the case where the protected mode    */
               /* run at slower rate than real mode.                          */
               deviceTable->SCSI_DF_negoOnModeSwitch = 1;

               /* Offset to location in auto-rate-offset table by */
               /* specified target ID */
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGOADDR), i);

               /* Save the scsi Protocol Option parameters in hardware    */
               /* register into the device table's current xfer entry.    */
               /* The information (such as Packetized bit) will be needed */
               /* when we check if the switch from non-packetized mode to */
               /* packetized mode ever occur during re-negotiation for    */
               /* the protected mode driver.                              */
               SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] =
                                    OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA2));
            }
         }
      }
   }

   /* Restore scb #FF */
   scbSize = 64;

   /* Save mode 3 SCBPTR value. */
   savedScbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                 ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);
   
   /* Load scb ptr with 0xFF */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)SCSI_U320_SCBFF_PTR);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(SCSI_U320_SCBFF_PTR >> 8));

   /* Restore scb #FF */ 
   for (i = 0; i < scbSize; i++)
   {                 
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+i),state->scbFF[i]);
   }
   
   /* Restore mode 3 SCBPTR. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)savedScbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(savedScbptr >> 8));

#if SCSI_STANDARD_MODE
   /* we are running standard mode, so we need to restore busy target array. */
   if ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) ||
       (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320))
   {
      /* Save mode 3 SCBPTR value. */
      savedScbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                    ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

      for (scbptr = 0; scbptr < SCSI_hBTASIZE(hhcb); scbptr++)
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(scbptr >> 8));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hBTATABLE(hhcb)),
                            state->Bta[scbptr]);
      }
 
      /* Restore mode 3 SCBPTR. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)savedScbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(savedScbptr >> 8));
   }
#endif /* SCSI_STANDARD_MODE */

   /* Reset Sequencer Start address */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0), 
                 (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1), 
                 (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  

#if SCSI_TARGET_OPERATION  
   /* restore the selection enable */
   if (state->SCSI_SF_selectionEnable)
   {
      /* selection-in enable */
      SCSIhTargetModeEnable(hhcb);
   }
   else
   {
      /* reset selection-in enable */
      SCSIHTargetModeDisable(hhcb);
   }
#endif /* SCSI_TARGET_OPERATION */ 

#if !POWER_MANAGEMENT_SAVE_RESTORE

   if (hcntrl & SCSI_CHIPRST)
   {
      hhcb->SCSI_HF_chipReset = 1;
      hcntrl &= ~SCSI_CHIPRST;
   }
    
   /* Restore pause bit and inten bit of HCNTRL */
   if (state->SCSI_SF_interruptEnable)
   {
      hcntrl |= SCSI_INTEN;
   }
   else
   {
      hcntrl &= ~SCSI_INTEN;
   }
   if (state->SCSI_SF_chipPause)
   {
      hcntrl |= SCSI_PAUSEACK;
   }
   else
   {
      hcntrl &= ~SCSI_PAUSEACK;
   }

   SCSI_hWRITE_HCNTRL(hhcb, hcntrl);

#else /* POWER_MANAGEMENT_SAVE_RESTORE */

   if (state->hcntrl & SCSI_CHIPRST)
   {     
      hhcb->SCSI_HF_chipReset = 1;
      state->hcntrl &= ~SCSI_CHIPRST;
   }   

   /* Due to the LVD HW that needs to wait a total of 200ms for the scsi bus */
   /* to stable after the chip reset we must wait for the scsi bus to become */
   /* stable based on bit ENAB20, ENAB40, and XCVR that is set by the HW     */
   /* after the bus becomes stable. */
   if (hcntrl & SCSI_CHIPRST)
   {
      SCSI_hWAITFORBUSSTABLE(hhcb);           
   }

   SCSI_hWRITE_HCNTRL(hhcb, state->hcntrl);

#endif /* !POWER_MANAGEMENT_SAVE_RESTORE */

   OSD_SYNCHRONIZE_IOS(hhcb);
}
#endif /* SCSI_SAVE_RESTORE_STATE */ 

#endif /* !SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIHPowerManagement
*
*     Execute power management function
*
*  Return Value:  0 - function execute successfully
*                 1 - function not supported
*                 others, to be defined
*                 
*                  
*  Parameters:    hhcb
*                 powerMode
*
*  Remarks:       There should be no active hiob when this routine
*                 get called.
*                  
*********************************************************************/
#if (!SCSI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
SCSI_UINT8 SCSIHPowerManagement (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UINT powerMode)
{
   if (SCSIHCheckHostIdle(hhcb) == SCSI_NOT_IDLE)
   {
      /* indicate host device busy (no power management allowed) */
      return(2);
   }
   else
   {
      /* this function is not supported at the moment */
      return(1);
   }
}
#endif /* (!SCSI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */

/*********************************************************************
*
*  SCSIHCheckHostIdle
*
*     Get the Check host device is idle
*
*  Return Value:  SCSI_IDLE     - host device is idle
*                 SCSI_NOT_IDLE - host device is not idle
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine will actually handle interrupt event
*                 though it was cleared by SCSIHFrontEndISR.
*
*********************************************************************/
#if (!SCSI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
SCSI_UINT8 SCSIHCheckHostIdle (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UEXACT16 i;
   SCSI_HIOB SCSI_IPTR hiob;

   /* check if there is any outstanding request in active */
   /* pointer array */
   for (i = 0; i < hhcb->numberScbs; i++)
   {
#if SCSI_STANDARD_MODE
      hiob = SCSI_ACTPTR[i];
   
#elif !SCSI_BIOS_SUPPORT
      hiob = SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET];
      
#else
      SCSIHReadScratchRam ((SCSI_HHCB SCSI_HPTR) hhcb,
         (SCSI_UEXACT8 SCSI_SPTR) &hiob,
         (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ACTIVE_HIOB),
         (SCSI_UEXACT16) 4);
      
#endif /* SCSI_STANDARD_MODE */

      if (hiob != SCSI_NULL_HIOB)
      {
         return(SCSI_NOT_IDLE);
      }
   }

   return(SCSI_IDLE);
}
#endif /* (!SCSI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT && !SCSI_BEF_SUPPORT) */

/*********************************************************************
*
*  SCSIHCheckDeviceIdle
*
*     Check device is idle
*
*  Return Value:  SCSI_IDLE     - device is idle
*                 SCSI_NOT_IDLE - device is not idle 
*                  
*  Parameters:    hhcb
*                 targetID
*
*  Remarks:       
*
*********************************************************************/
#if (!SCSI_BIOS_SUPPORT && !SCSI_BEF_SUPPORT)
SCSI_UINT8 SCSIHCheckDeviceIdle (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UEXACT8 targetID)
{
   SCSI_UEXACT16 i;
   SCSI_HIOB SCSI_IPTR hiobNew;

   /* check if there is any outstanding request in active */
   /* pointer array */
   for (i = 0; i < hhcb->numberScbs; i++)
   {
   
#if SCSI_STANDARD_MODE
      hiobNew = SCSI_ACTPTR[i];
   
#elif !SCSI_BIOS_SUPPORT
      hiobNew = SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET];
      
#else
      SCSIHReadScratchRam ((SCSI_HHCB SCSI_HPTR) hhcb,
         (SCSI_UEXACT8 SCSI_SPTR) &hiobNew,
         (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ACTIVE_HIOB),
         (SCSI_UEXACT16) 4);
      
#endif /* SCSI_STANDARD_MODE */
   
      if (hiobNew != SCSI_NULL_HIOB)
      {
#if SCSI_TARGET_OPERATION
         if (hiobNew->SCSI_IP_targetMode)
         {
            /* target mode hiobs do not affect idless of
             * device - so skip over them.
             */
            continue;
         }
#endif /* SCSI_TARGET_OPERATION */

         if (((SCSI_UNIT_CONTROL SCSI_UPTR) 
               SCSI_TARGET_UNIT(hiobNew))->scsiID == targetID)
         {
            return(SCSI_NOT_IDLE);
         }
      }
   }

   return(SCSI_IDLE);
}
#endif /* (!SCSI_BIOS_SUPPORT && !SCSI_BEF_SUPPORT) */

/*********************************************************************
*
*  SCSIhAbortTask
*
*     Abort task associated
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:       There is no guarantee the aborted hiob is still
*                 in hardware management layer.
*
*********************************************************************/
#if (!SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT_GROUP1)
void SCSIhAbortTask (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_HIOB SCSI_IPTR hiobAbort = SCSI_RELATED_HIOB(hiob);
   SCSI_HHCB SCSI_HPTR hhcb;
   SCSI_UEXACT16 i;
#if SCSI_RAID1
   SCSI_UEXACT16 abortFound = 0;
#endif /* SCSI_RAID1 */

#if SCSI_TARGET_OPERATION
   if (hiobAbort->SCSI_IP_targetMode)
   {
      /* Must be a target mode Establish Connection being
       * aborted.
       */
      hhcb = SCSI_NEXUS_UNIT(hiobAbort)->hhcb;
   }
   else
#endif /* SCSI_TARGET_OPERATION */
   {
      hhcb = SCSI_TARGET_UNIT(hiobAbort)->hhcb;
   }

   /* Check all HIOBs in the active pointer array */
   /* for the match of aborted HIOB. */
   for (i = 0; i < hhcb->numberScbs; i++)
   {
      if (hiobAbort == SCSI_ACTPTR[i])
      {

#if SCSI_RAID1
         abortFound++;
         if (hiobAbort->SCSI_IF_raid1)
         {
            /* Is the SCB for the primary */
            /* Or the mirror target */
            if (i != hiobAbort->scbNumber)
            {
               if (i == hiobAbort->mirrorHiob->scbNumber)
               {
                  /* Update the relevant fields in the mirror hiob */
                  hiobAbort->mirrorHiob->cmd = hiobAbort->cmd; 
                  hiobAbort->mirrorHiob->flags = hiobAbort->flags;

                  /* Use the mirror hiob */
                  hiobAbort = hiobAbort->mirrorHiob;
                  hiobAbort->SCSI_IF_raid1 = 1;
               }
               else
               {
                  /* BadSeq? */
               }
            }
         }
#endif /* SCSI_RAID1 */

         SCSI_hABORTHIOB(hhcb, hiobAbort, SCSI_HOST_ABT_HOST);

         /* If aborted HIOB is in Done queue (already completed) ... */
         if (hiobAbort->SCSI_IP_mgrStat == SCSI_MGR_DONE)
         {
            /* then the ABORT_TASK's hiob will return with status of */
            /* SCSI_HOST_ABT_CMDDONE. */
            hiob->stat = SCSI_SCB_ERR;
            hiob->haStat = SCSI_HOST_ABT_CMDDONE;
         }
         else
         {
            /* Update ABORT_TASK's hiob with status SCSI_HOST_ABT_STARTED */
            hiob->stat = SCSI_SCB_PENDING;
            hiob->haStat = SCSI_HOST_ABT_STARTED;
         }
#if SCSI_RAID1
         if (abortFound > 1 )
         {
            break;
         }
#else
         break;
#endif /* SCSI_RAID1 */

      }
   }

   /* If the search for the aborted HIOB in ACTPTR has been exhausted, */
   /* then we will return status to indicate it is not found.          */
#if SCSI_RAID1
   if ((i == hhcb->numberScbs) && (abortFound == 0))
#else
   if (i == hhcb->numberScbs)
#endif /* SCSI_RAID1 */
   {
      /* Update ABORT_TASK's hiob with status SCSI_HOST_ABT_NOT_FND */
      hiob->stat = SCSI_SCB_ERR;
      hiob->haStat = SCSI_HOST_ABT_NOT_FND;
   }

   /* Post back special HIOB to the upper layer code */
   SCSI_COMPLETE_SPECIAL_HIOB(hiob);
}
#endif /* (!SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT_GROUP1) */

/*********************************************************************
*
*  SCSIhAbortTaskSet
*
*     Abort task set associated
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:       There is no guarantee the aborted hiob is still
*                 in hardware management layer.
*
*********************************************************************/
#if (!SCSI_DOWNSHIFT_MODE  && !SCSI_ASPI_SUPPORT_GROUP1)
void SCSIhAbortTaskSet (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_HHCB SCSI_HPTR hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;
   SCSI_HIOB SCSI_IPTR hiobAbort;
   SCSI_UEXACT8 scsiID = SCSI_TARGET_UNIT(hiob)->scsiID;
   SCSI_UEXACT16 i;

   /* Check all HIOBs in the active pointer array for the match */
   /* of target SCSI ID. If so, abort each HIOB separately. */
   for (i = 0; i < hhcb->numberScbs; i++)
   {
      if ((hiobAbort = SCSI_ACTPTR[i]) != SCSI_NULL_HIOB)
      {
         if ((SCSI_TARGET_UNIT(hiobAbort)->hhcb == hhcb) &&
             (SCSI_TARGET_UNIT(hiobAbort)->scsiID == scsiID))
         {
            /* Abort individual HIOB */
            SCSI_hABORTHIOB(hhcb, hiobAbort, SCSI_HOST_ABT_HOST);
         }
      }
   }

   hiob->stat = SCSI_SCB_COMP;

   /* Post back special HIOB to the upper layer code */
   SCSI_COMPLETE_SPECIAL_HIOB(hiob);
}
#endif /* (!SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT_GROUP1) */

/*********************************************************************
*
*  SCSIhPCIReset
*
*     Reset hardware and software after PCI/PCI-X error
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This is part of the PCI/PCI-X error recovery. The OSM
*                 must issue a RESET_HARDWARE task to complete the 
*                 recovery. A special check for clearing the PCI Config
*                 space register(s) is included. Total recovery for such an 
*                 error can only be completed if the PCI Status register(s)
*                 are cleaned. If the Adapter Profile prevents this action, 
*                 the OSM would have been required to have cleared the 
*                 register(s) prior to issuing this task. If for some reason  
*                 that was not executed or was unsuccessful the HIM can not 
*                 guarantee the success of this operation.
*
*********************************************************************/
#if (!SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING && !SCSI_DCH_U320_MODE)     
void SCSIhPCIReset (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 configSpaceErrorStatus;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT8 hcntrl;
   
   /* Save the HCNTRL value. */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }
   
   /* Save original mode */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* 
    *  Determine if HF_ClearConfigurationStatus is set. If HIM_FALSE the OSM 
    *  accepts responsibility for clearing status before attempting
    *  to continue the recovery process
    */
  
   if (hhcb->SCSI_HF_ClearConfigurationStatus)
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      
      if (hhcb->SCSI_HP_hstintstat & SCSI_PCIINT)
      {
         configSpaceErrorStatus = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_STATUS_HI_REG));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_STATUS_HI_REG), configSpaceErrorStatus);
      }
      /* 
         Test all possible PCI-X config status registers and clear if error
         status found. 
       */  
      else if (hhcb->SCSI_HP_hstintstat & SCSI_SPLTINT)
      {
         /* Check all Config Space registers that could contain error status */
         if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIX_STATUS3)) & SCSI_RSCEM)
         {
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIX_STATUS3), SCSI_RSCEM);
         }

         if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIX_STATUS2)) & SCSI_UNEXPSC)
         {
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIX_STATUS2), SCSI_UNEXPSC);
         }

         if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_SLVSPLTSTAT)) & SCSI_PCIX_SLAVE_ERRORS)
         {
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_SLVSPLTSTAT), SCSI_PCIX_SLAVE_ERRORS);
         }

         /* clear split interrupt */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRSPLTINT);
      }
   }

   /* reset the channel */
   SCSIhResetChannelHardware(hhcb);
   
   SCSI_hRESETSOFTWARE(hhcb);
   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                 (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                 (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));
                 
   /* restore original MODE value */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
   
    /* Clearing either one of these flags indicates the HIM 
       has executed the SCSI_CMD_RESET_HARDWARE HIOB */
   if (hhcb->SCSI_HP_hstintstat & SCSI_PCIINT)
   {
      hhcb->SCSI_HP_hstintstat &= ~SCSI_PCIINT;
   }
   else if (hhcb->SCSI_HP_hstintstat & SCSI_SPLTINT)
   {
      hhcb->SCSI_HP_hstintstat &= ~SCSI_SPLTINT;
   }

   if (!hhcb->SCSI_HP_inISRContext)
   {
      /* unpause sequencer only if not inside interrupt context */
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl & ~SCSI_PAUSE);
      
      OSD_SYNCHRONIZE_IOS(hhcb);
   }
   
}
#endif /* (!SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING && !SCSI_DCH_U320_MODE) */    

/*********************************************************************
*
*  SCSIHSuppressNegotiation
*
*     Suppress wide/sync negotiation initiation
*
*  Return Value:  SCSI_SUCCESS - Suppress negotiation setup.
*                 SCSI_FAILURE - Target device was not idle.
*                  
*  Parameters:    targetUnit
*
*  Remarks:       Suppress negotiation will be applied only if the
*                 HIM is in a state ready to initiate negotiation.
*
*                 This routine should only be called when device
*                 is idle
*
*********************************************************************/
#if (!SCSI_ASYNC_NARROW_MODE && !SCSI_BIOS_SUPPORT && !SCSI_BEF_SUPPORT)
SCSI_UINT8 SCSIHSuppressNegotiation (SCSI_UNIT_CONTROL SCSI_UPTR targetUnit)
{
   /* device must be idle */
   if (SCSIHCheckDeviceIdle(targetUnit->hhcb,targetUnit->scsiID))
   {
      if (targetUnit->deviceTable->SCSI_DF_needNego)
      {
         /* turn off the negotiation initiation */
         targetUnit->deviceTable->SCSI_DF_needNego = 0;
      }

      /* Set suppress negotiation flag */
      targetUnit->deviceTable->SCSI_DF_suppressNego = 1;
    
#if SCSI_PACKETIZED_IO_SUPPORT
      /* Restore hardware packetized mode depending on current rate.
       * SCSIHForceNegotiation will change the negotiation rate
       * registers when packetized is currently negotiated with the
       * device to force renegotiation. This is done by setting the
       * SCSI_ENAUTOATNO of NEGODATA3 to force selection with ATN.
       * If a SCSIHSuppressNegotiation call occurs immediately after
       * a SCSIHForceNegotiation the negotiation table entries must
       * be restored to prevent negotiation. This is achieved by
       * reloading the negotiation table with the current negotiated
       * transfer rates. Only needs to be done for packetized mode.
       */
      if (SCSI_CURRENT_XFER(targetUnit->deviceTable)[SCSI_XFER_PTCL_OPT] &
          SCSI_PACKETIZED) 
      { 
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
         if (SCSI_hHARPOON_REV_1B(targetUnit->hhcb) &&
             targetUnit->hhcb->SCSI_HF_targetMode &&
             (!targetUnit->hhcb->SCSI_HF_initiatorMode))
         {
            /* Eventually a for loop of SCSI enabled IDs would be required. */
            SCSI_hLOADNEGODATA(targetUnit->hhcb,
                               (SCSI_UEXACT8)(targetUnit->scsiID &
                                              (targetUnit->hhcb->hostScsiID << 4)),
                               SCSI_CURRENT_XFER(targetUnit->deviceTable));
         }
         else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
         {
            SCSI_hLOADNEGODATA(targetUnit->hhcb,
                               targetUnit->scsiID,
                               SCSI_CURRENT_XFER(targetUnit->deviceTable));
         }
      }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
      
      return(SCSI_SUCCESS);
   }
   return(SCSI_FAILURE);
}
#endif /* !SCSI_ASYNC_NARROW_MODE  && SCSI_BIOS_SUPPORT && !SCSI_BEF_SUPPORT */

/*********************************************************************
*
*  SCSIHForceNegotiation
*
*     Force wide/sync negotiation initiation
*
*  Return Value:  status 
*                 SCSI_SUCCESS - Negotiation setup for next cmd.
*                 SCSI_FAILURE - Target device was not idle.
*                  
*  Parameters:    targetUnit
*
*  Remarks:       This routine should only be called when device
*                 is idle
*
*********************************************************************/
#if ((!SCSI_ASYNC_NARROW_MODE) && (!SCSI_BIOS_SUPPORT) && (!SCSI_BEF_SUPPORT))
SCSI_UINT8 SCSIHForceNegotiation (SCSI_UNIT_CONTROL SCSI_UPTR targetUnit)
{

#if SCSI_BIOS_SUPPORT
   /* Check for runtime setting nego needed which would not get cleared */
   if ((SCSIHCheckDeviceIdle(targetUnit->hhcb,targetUnit->scsiID)) &&
         !(targetUnit->hhcb->SCSI_HF_biosRunTimeEnabled))
#else
   /* device must be idle */
   if (SCSIHCheckDeviceIdle(targetUnit->hhcb,targetUnit->scsiID))
#endif /* SCSI_BIOS_SUPPORT */
   {
      /* turn on the negotiation initiation */
      if (SCSI_hWIDEORSYNCNEGONEEDED(targetUnit->deviceTable))
      {
#if SCSI_PACKETIZED_IO_SUPPORT
         /* Setup hardware to re-negotiate in packetized mode */
         SCSIHSetupNegoInPacketized(targetUnit->hhcb,targetUnit->scsiID);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
         targetUnit->deviceTable->SCSI_DF_needNego = 1;
      }
      else
      {
         targetUnit->deviceTable->SCSI_DF_needNego = 0;
      }

      /* Clear suppress negotiation flag */
      targetUnit->deviceTable->SCSI_DF_suppressNego = 0;
      return(SCSI_SUCCESS);
   }
   return(SCSI_FAILURE);
}
#endif /* ((!SCSI_ASYNC_NARROW_MODE) && (!SCSI_BIOS_SUPPORT) && (!SCSI_BEF_SUPPORT)) */

/*********************************************************************
*
*  SCSIHGetHWInformation
*
*     Updates the SCSI_HW_INFORMATION structure with information from
*     the HA hardware.  An example usage is to get adapter/target
*     profile information for the CHIM interface.
*
*  Return Value:  none.
*                  
*  Parameters:    hwInfo - SCSI_HW_INFORMATION structure.
*                 hhcb   - HHCB structure for the adapter.
*
*  Remarks:       Sequencer is not paused during getting
*                 adapter/target profile information.
*                 The reason for the change to not pause sequencer
*                 is IO might time-out if OSM frequently requests
*                 adapter/target profile information under Harpoon 2
*                 Rev A.  The Harpoon 2 Rev A sequencer has a work
*                 around for a drive hang issue, ww19.  When driver
*                 code requests profile information the sequencer
*                 is paused while HW registers are read.  If this
*                 pause occurs while sequencer is in ww19 code,
*                 a hang on the SCSI bus may occur.
*                 
*                 All previous required information that need to
*                 access the hardware registers are now stored in
*                 the host memory.  With this change, there might
*                 be a potential of inaccurate profile information
*                 report to OSM.
*  
*********************************************************************/
#if SCSI_PROFILE_INFORMATION
void SCSIHGetHWInformation (SCSI_HW_INFORMATION SCSI_LPTR hwInfo, 
                            SCSI_HHCB SCSI_HPTR hhcb)                  
{
   SCSI_HOST_ADDRESS SCSI_LPTR hostAddress;
#if !SCSI_DCH_U320_MODE
   SCSI_UEXACT32 baseAddress;
   SCSI_UEXACT8 i;
   SCSI_UEXACT8 j;
#endif /* !SCSI_DCH_U320_MODE */
   SCSI_UEXACT8 busNumber;
   SCSI_UEXACT8 deviceNumber;

   /* Calculate WWID */
   hwInfo->WWID[0] = 
      (hhcb->SCSI_HP_sblkctl & SCSI_SELWIDE) ? SCSI_UPTO_0F : SCSI_UPTO_07;

   hwInfo->WWID[0] |= SCSI_SNA;
   hwInfo->WWID[0] |= SCSI_VALID_BUT_UNASSIGN;

   /* set the current ID entry */
   hwInfo->WWID[1] |= hhcb->hostScsiID;

   /* set vendor identification to ADAPTEC */
   hwInfo->WWID[2] = 'A';
   hwInfo->WWID[3] = 'D';
   hwInfo->WWID[4] = 'A';
   hwInfo->WWID[5] = 'P';
   hwInfo->WWID[6] = 'T';
   hwInfo->WWID[7] = 'E';
   hwInfo->WWID[8] = 'C';
   hwInfo->WWID[9] = ' ';
 
   /* Generating the remaining portion of the WWID.               */
   hwInfo->WWID[10] = 'A';
   hwInfo->WWID[11] = 'I';
   hwInfo->WWID[12] = 'C';

   hwInfo->WWID[13] =
      (SCSI_UEXACT8)((hhcb->deviceID & 0xf000) >> 12) + '0';
   hwInfo->WWID[14] =
      (SCSI_UEXACT8)((hhcb->deviceID & 0x0f00) >> 8) + '0';
   hwInfo->WWID[15] =
      (SCSI_UEXACT8)((hhcb->deviceID & 0x00f0) >> 4) + '0';
   hwInfo->WWID[16] =
      (SCSI_UEXACT8)(hhcb->deviceID & 0x000f) + '0';

   /* PCI bus number is scanned from 0 to 255, so the boot/primary */
   /* host adapter will have lowest bus number.  But the id string */
   /* should start with higher value i.e. from 255 to 0 */
   hostAddress = OSD_GET_HOST_ADDRESS(hhcb);
   busNumber = (SCSI_UEXACT8)(0xff - hostAddress->pciAddress.busNumber);

   hwInfo->WWID[19] = (SCSI_UEXACT8)((busNumber % 10) + '0');
   busNumber /= 10;
   hwInfo->WWID[18] = (SCSI_UEXACT8)((busNumber % 10) + '0');
   hwInfo->WWID[17] = (SCSI_UEXACT8)((busNumber / 10) + '0');

   deviceNumber = (SCSI_UEXACT8)(0x1f - hostAddress->pciAddress.deviceNumber);
   hwInfo->WWID[21] = (SCSI_UEXACT8)((deviceNumber % 10) + '0');
   hwInfo->WWID[20] = (SCSI_UEXACT8)((deviceNumber / 10) + '0');

#if !SCSI_DCH_U320_MODE
   /* Get base address from configuration space */
   baseAddress = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_BASE_ADDR_REG) & 0xFFFFFFFE;

   for (i = 0; i < 8; i++)
   {
      j = ((SCSI_UEXACT8)(baseAddress >> (28 - i * 4))) & 0x0f;

      /* convert to ASCII character */
      hwInfo->WWID[22 + i] = (j >= 9) ? (j - 10) + 'A' : j + '0';
   }

   hwInfo->WWID[30] = hwInfo->WWID[31] = ' ';
#endif /* !SCSI_DCH_U320_MODE */

   hwInfo->hostScsiID = hhcb->hostScsiID;
   hwInfo->SCSI_PF_scsiParity = hhcb->SCSI_HF_scsiParity;
   hwInfo->SCSI_PF_selTimeout = hhcb->SCSI_HF_selTimeout;

   hwInfo->SCSI_PF_separateRWThreshold = hhcb->SCSI_HF_separateRWThreshold;
   hwInfo->SCSI_PF_separateRWThresholdEnable = hhcb->SCSI_HF_separateRWThresholdEnable;

   if (hwInfo->SCSI_PF_separateRWThresholdEnable)
   { 
      hwInfo->readThreshold = hhcb->threshold & 0x0F;
      hwInfo->writeThreshold = hhcb->threshold >> 4;
   }
   else 
   {
      /* assume both 4 bit fields are the same */
     hwInfo->threshold = hhcb->threshold & 0x0F; 
   }   
      
   if (hhcb->SCSI_HP_sblkctl & SCSI_SELWIDE)
   {
      hwInfo->SCSI_PF_wideSupport = 1;
   }
   else
   {
      hwInfo->SCSI_PF_wideSupport = 0;
   }

   SCSI_hGETPROFILEPARAMETERS(hwInfo, hhcb);

   SCSI_hGETTARGETMODEPROFILEFIELDS(hwInfo,hhcb);

   hwInfo->SCSI_PF_intrThresholdSupport = 
      hhcb->SCSI_HF_cmdCompleteThresholdSupport;
   hwInfo->intrFactorThreshold = hhcb->cmdCompleteFactorThreshold;
   hwInfo->intrThresholdCount = hhcb->cmdCompleteThresholdCount;

#if !SCSI_DCH_U320_MODE
   hwInfo->SCSI_PF_cacheThEn = hhcb->SCSI_HF_cacheThEn;
#endif /* !SCSI_DCH_U320_MODE */
   
   /* Not supported. */
   hwInfo->disconnectDelay = 0;
   hwInfo->SCSI_PF_disconnectDelaySupport = 0;

   /* Ensure adjustTargetOnly is 0 */ 
   hwInfo->SCSI_PF_adjustTargetOnly = 0;
}
#endif /* SCSI_PROFILE_INFORMATION */

/*********************************************************************
*
*  SCSIHPutHWInformation
*
*     Updates the the hardware with information from the
*     SCSI_HW_INFORMATION structure.  An example usage is to update
*     adapter/target profile information for the CHIM interface.
*
*  Return Value:  none.
*                  
*  Parameters:    hwInfo - SCSI_HW_INFORMATION structure.
*                 hhcb   - HHCB structure for the adapter.
*
*  Remarks:       We cannot assume any mode when we enter this routine
*                 because the sequencer is paused at an arbitrary
*                 location.
*                 
*********************************************************************/
#if SCSI_PROFILE_INFORMATION
void SCSIHPutHWInformation (SCSI_HW_INFORMATION SCSI_LPTR hwInfo, 
                            SCSI_HHCB SCSI_HPTR hhcb)                  
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 modeSave;
#if SCSI_ELEC_PROFILE
   SCSI_UEXACT8 i;
#endif /* SCSI_ELEC_PROFILE */

   /* save the HCNTRL value */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   /* Save the incoming mode */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   if (!hwInfo->SCSI_PF_adjustTargetOnly)
   {
      /* Safe to update general hardware registers. */
      /* Target only adjustments must not update general */
      /* adapter registers as I/Os may be active. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_IOWNID), hwInfo->hostScsiID);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_TOWNID), hwInfo->hostScsiID);
      hhcb->hostScsiID = hwInfo->hostScsiID;

      /* Update hhcb separate threshold status. */ 
      hhcb->SCSI_HF_separateRWThresholdEnable = hwInfo->SCSI_PF_separateRWThresholdEnable;

      if (hwInfo->SCSI_PF_separateRWThresholdEnable)
      { 
        hhcb->threshold = hwInfo->writeThreshold;
        hhcb->threshold = (hhcb->threshold << 4)| hwInfo->readThreshold; 
      }
      else 
      {
         /* Both read and write threshold values are the same */ 
         hhcb->threshold = hwInfo->threshold & 0x0F;  /* use lower 4 bits */ 
         hhcb->threshold = (hhcb->threshold << 4)| hwInfo->threshold;
      }   

      /* Update hardware register */ 
      SCSI_hUPDATEDATAFIFOTHRSH(hhcb);
   
      /* Update CacheLineStreaming feature */
#if !SCSI_DCH_U320_MODE
      hhcb->SCSI_HF_cacheThEn = hwInfo->SCSI_PF_cacheThEn;
      SCSI_hUPDATECACHETHEN(hhcb);
#endif /* !SCSI_DCH_U320_MODE */

      /* Parity and CRC checking are two separated control bits: ENSPCHK and */
      /* CRCVALCHKEN respectively.  CHIM will disable/enable parity and CRC  */
      /* checking based on scsiParity flag. */
      if (hwInfo->SCSI_PF_scsiParity)
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL),
             (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_CRCCONTROL)) |
                            SCSI_CRCVALCHKEN));
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1),
             (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1)) |
                            SCSI_ENSPCHK));
      }
      else
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL),
             (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_CRCCONTROL)) &
                            ~SCSI_CRCVALCHKEN));
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1),
             (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1)) &
                            ~SCSI_ENSPCHK));
      }
      
      hhcb->SCSI_HF_scsiParity = hwInfo->SCSI_PF_scsiParity;
      hhcb->SCSI_HF_selTimeout = hwInfo->SCSI_PF_selTimeout;


#if SCSI_ELEC_PROFILE
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      for(i = 0; i < 0x14; i++ )
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSPSELECT), i );
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_WRTBIASCTL), hhcb->wrtBiasCtl );
      }
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
 #endif /* SCSI_ELEC_PROFILE */

      /* Selection timeout delay set separately. */   
      SCSIhSetSeltoDelay(hhcb);

      /* load target id registers */
      SCSI_hLOADTARGETIDS(hhcb); 

      /* Set target mode specific profile fields. */
      SCSI_hSETTARGETMODEPROFILEFIELDS(hwInfo,hhcb);

      if (hhcb->SCSI_HF_cmdCompleteThresholdSupport)
      {         
         hhcb->cmdCompleteFactorThreshold = hwInfo->intrFactorThreshold;
         hhcb->cmdCompleteThresholdCount = hwInfo->intrThresholdCount;

         SCSI_hSETINTRFACTORTHRESHOLD(hhcb, hwInfo->intrFactorThreshold);
         SCSI_hSETINTRTHRESHOLDCOUNT(hhcb, hwInfo->intrThresholdCount);
      }

      /* disconnect delay profiling not supported */ 
      /* set value from profile into hhcb */
      hhcb->disconnectDelay = hwInfo->disconnectDelay;

      /* now update scratch RAM locations */ 
      SCSI_hSETDISCONNECTDELAY(hhcb);
   }

#if ((OSD_BUS_ADDRESS_SIZE == 64) && SCSI_STANDARD_MODE)
   if ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) ||
       (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320))
   {
      /* Update sgsegment_32 flag */
      if (hhcb->SCSI_HF_SgBusAddress32)
      {
         /* 32-bit s/g list format */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS),
            (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS)) | SCSI_SU320_SGSEGMENT_32));
      }
      else
      {
         /* 64-bit s/g list format */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS),
            (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS)) & ~SCSI_SU320_SGSEGMENT_32));
      }
   }
#endif /* ((OSD_BUS_ADDRESS_SIZE == 64) && SCSI_STANDARD_MODE) */

   SCSI_hPUTPROFILEPARAMETERS(hwInfo, hhcb);

   /* Restore the original mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   /* restore HCNTRL if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}
#endif /* SCSI_PROFILE_INFORMATION */

/*********************************************************************
*
*  SCSIHEnableExpStatus
*
*     Enable expander status examination
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       * This routine should only be called when device
*                   is idle.
*                 * This routine will enable the break point
*                   for bus expander status examination.
*                 * After this call all following requests will cause
*                   the device table updated per bus expander status
*                   examined from bus.
*
*                 * To be implemented for U320 ASIC.
*
*********************************************************************/
#if (!SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
void SCSIHEnableExpStatus (SCSI_HHCB SCSI_HPTR hhcb)
{
   if (hhcb->SCSI_HF_expSupport)
   {
      /* Assert expander status request. */
      hhcb->SCSI_HP_expRequest = 1;

      /* Enable break point interrupt for expander status
       * examination.
       */
 
      /* Enable and program the expander break address. */
      SCSI_hSETBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_EXPANDER_BREAK);
   }
}
#endif /* (!SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */

/*********************************************************************
*
*  SCSIHDisableExpStatus
*
*     Disable expander status examination
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be called when device
*                 is idle
*                 This routine will disable the break point
*                 for bus expander status examination
*
*                 To be implemented for U320 ASIC.
*
*********************************************************************/
#if (!SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
void SCSIHDisableExpStatus (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;

   if (hhcb->SCSI_HF_expSupport)
   {
      /* Clear expander status request */
      hhcb->SCSI_HP_expRequest = 0;

      /* Save the HCNTRL value. */
      hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));
   
      /* Pause the chip if necessary. */
      if (!(hcntrl & SCSI_PAUSEACK))
      {
         SCSIhPauseAndWait(hhcb);
      }

      /* Clear expander break point. */
      SCSI_hCLEARBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_EXPANDER_BREAK);

      /* Restore HCNTRL if necessary. */
      if (!(hcntrl & SCSI_PAUSEACK))
      {
         SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
      } 

   }
}
#endif /* (!SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */


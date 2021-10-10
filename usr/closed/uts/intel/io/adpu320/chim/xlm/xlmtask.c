/*$Header: /vobs/u320chim/src/chim/xlm/xlmtask.c   /main/185   Fri Aug 22 16:18:29 2003   hu11135 $*/
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
*  Module Name:   XLMTASK.C
*
*  Description:
*                 Codes to implement run time Common HIM interface 
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         NONE
*
*  Entry Point(s):
*                 SCSIDisableIRQ
*                 SCSIEnableIRQ
*                 SCSIPollIRQ
*                 SCSIFrontEndISR
*                 SCSIBackEndISR
*                 SCSIQueueIOB
*                 SCSIPowerEvent
*                 SCSIValidateTargetTSH
*                 SCSIClearTargetTSH
*                 SCSISaveState
*                 SCSIRestoreState
*                 SCSIProfileAdapter
*                 SCSIReportAdjustableAdapterProfile
*                 SCSIAdjustAdapterProfile
*                 SCSIProfileTarget
*                 SCSIReportAdjustableTargetProfile
*                 SCSIAdjustTargetProfile
*                 SCSIGetNVSize
*                 SCSIGetNVOSMSegment
*                 SCSIPutNVData
*                 SCSIGetNVData
*                 SCSIClearNexusTSH 
*                 SCSIProfileNexus 
*                 SCSIProfileNode
*                 SCSIReportAdjustableNodeProfile
*                 SCSIAdjustNodeProfile
*                 SCSISetOSMNodeContext
*
***************************************************************************/

#include "scsi.h"
#include "xlm.h"

/*********************************************************************
*
*  SCSIDisableIRQ
*
*     Disable hardware interrupt
*
*  Return Value:  none
*                  
*  Parameters:    adapter TSH
*
*  Remarks:
*
*********************************************************************/
void SCSIDisableIRQ (HIM_TASK_SET_HANDLE adapterTSH)
{
   SCSI_DISABLE_IRQ(&SCSI_ADPTSH(adapterTSH)->hhcb);
}

/*********************************************************************
*
*  SCSIEnableIRQ
*
*     Enable hardware interrupt
*
*  Return Value:  none
*                  
*  Parameters:    adapter TSH
*
*  Remarks:
*
*********************************************************************/
void SCSIEnableIRQ (HIM_TASK_SET_HANDLE adapterTSH)
{
   SCSI_ENABLE_IRQ(&SCSI_ADPTSH(adapterTSH)->hhcb);
}

/*********************************************************************
*
*  SCSIPollIRQ
*
*     Poll hardware interrupt
*
*  Return Value:  HIM_NOTHING_PENDING, interrupt not asserted
*                 HIM_INTERRUPT_PENDING, interrupt asserted
*                  
*  Parameters:    adapter TSH
*
*  Remarks:
*
*********************************************************************/
HIM_UINT8 SCSIPollIRQ (HIM_TASK_SET_HANDLE adapterTSH)
{
   if (SCSI_POLL_IRQ(&SCSI_ADPTSH(adapterTSH)->hhcb))
   {
      return((HIM_UINT8)(HIM_INTERRUPT_PENDING));
   }
   else
   {
      return((HIM_UINT8)(HIM_NOTHING_PENDING));
   }
}

/*********************************************************************
*
*  SCSIFrontEndISR
*
*     Execute front end interrupt service rutine
*
*  Return Value:  HIM_NOTHING_PENDING, no interrupt pending
*                 HIM_INTERRUPT_PENDING, interrupt pending
*                 HIM_LONG_INTERRUPT_PENDING, lengthy interrupt pending
*                  
*  Parameters:    adapter TSH
*
*  Remarks:       This routine should be very fast in execution.
*                 The lengthy processing of interrupt should be
*                 handled in SCSIBackEndISR.
*
*********************************************************************/
#if !SCSI_STREAMLINE_QIOPATH
HIM_UINT8 SCSIFrontEndISR (HIM_TASK_SET_HANDLE adapterTSH)
{
   if (SCSI_FRONT_END_ISR(&SCSI_ADPTSH(adapterTSH)->hhcb))
   {
      return((HIM_UINT8)(HIM_INTERRUPT_PENDING));
   }
   else
   {
      return((HIM_UINT8)(HIM_NOTHING_PENDING));
   }
}
#endif /* !SCSI_STREAMLINE_QIOPATH */

/*********************************************************************
*
*  SCSIBackEndISR
*
*     Execute back end interrupt service rutine
*
*  Return Value:  none
*                  
*  Parameters:    adapter TSH
*
*  Remarks:       It's OSM's decision to execute this routine at
*                 interrupt context or not.
*
*********************************************************************/
#if !SCSI_STREAMLINE_QIOPATH
void SCSIBackEndISR (HIM_TASK_SET_HANDLE adapterTSH)
{
   SCSI_BACK_END_ISR(&SCSI_ADPTSH(adapterTSH)->hhcb);
}
#endif /* !SCSI_STREAMLINE_QIOPATH */

/*********************************************************************
*
*  SCSIQueueIOB
*
*     Queue the IOB for execution
*
*  Return Value:  void
*                  
*  Parameters:    adapter TSH
*
*  Remarks:       It's OSM's decision to execute this routine at
*                 interrupt context or not.
*
*********************************************************************/
#if !SCSI_STREAMLINE_QIOPATH
void SCSIQueueIOB (HIM_IOB HIM_PTR iob)
{
   HIM_TASK_SET_HANDLE taskSetHandle = iob->taskSetHandle;
   SCSI_HIOB HIM_PTR hiob = &SCSI_IOBRSV(iob)->hiob;
   HIM_TS_SCSI HIM_PTR transportSpecific = (HIM_TS_SCSI HIM_PTR)iob->transportSpecific;
#if SCSI_TARGET_OPERATION 
   SCSI_UEXACT8 HIM_PTR scsiStatus;
#endif /* SCSI_TARGET_OPERATION */

#if (SCSI_DOMAIN_VALIDATION + SCSI_TARGET_OPERATION)
   SCSI_HHCB HIM_PTR hhcb; 
#endif /* (SCSI_DOMAIN_VALIDATION + SCSI_TARGET_OPERATION) */
#if SCSI_PACKETIZED_IO_SUPPORT 
          
#if (SCSI_TASK_SWITCH_SUPPORT == 0)
   SCSI_UEXACT8 HIM_PTR uexact8Pointer = (SCSI_UEXACT8 HIM_PTR) iob->targetCommand;
#endif /* SCSI_TASK_SWITCH_SUPPORT == 0 */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

   /* setup pointer to iob for translation from hiob to iob */
   /* it will be referenced when building SCB and posting to OSM */
   SCSI_IOBRSV(iob)->iob = iob;
                               
   if (iob->function == HIM_INITIATE_TASK)
   {
      /* translate iob into hiob */
      hiob->unitHandle.targetUnit = &SCSI_TRGTSH(taskSetHandle)->unitControl;

      hiob->cmd = SCSI_CMD_INITIATE_TASK;
      hiob->SCSI_IF_noUnderrun = 0;

#if SCSI_SELTO_PER_IOB      
      hiob->seltoPeriod = 0;
#endif /* SCSI_SELTO_PER_IOB */

      if (transportSpecific)
      {
         hiob->SCSI_IF_tagEnable = ((SCSI_TRGTSH(taskSetHandle)->targetAttributes.tagEnable) &&
                                    (transportSpecific->forceUntagged == HIM_FALSE) &&
                                    /* HQ
                                    (transportSpecific->disallowDisconnect == HIM_FALSE) &&
                                    */
                                    (iob->taskAttribute != HIM_TASK_RECOVERY));
         hiob->SCSI_IF_disallowDisconnect =
               (transportSpecific->disallowDisconnect == HIM_TRUE) ? 1 : 0;
#if SCSI_NEGOTIATION_PER_IOB
         hiob->SCSI_IF_forceSync =
               (transportSpecific->forceSync == HIM_TRUE) ? 1 : 0;
         hiob->SCSI_IF_forceAsync =
               (transportSpecific->forceAsync == HIM_TRUE) ? 1 : 0;
         hiob->SCSI_IF_forceWide =
               (transportSpecific->forceWide == HIM_TRUE) ? 1 : 0;
         hiob->SCSI_IF_forceNarrow =
               (transportSpecific->forceNarrow == HIM_TRUE) ? 1 : 0;
         hiob->SCSI_IF_forceReqSenseNego =
               (transportSpecific->forceReqSenseNego == HIM_TRUE) ? 1 : 0;
#endif /* SCSI_NEGOTIATION_PER_IOB */
#if SCSI_PARITY_PER_IOB
         hiob->SCSI_IF_parityEnable = 
               (transportSpecific->parityEnable == HIM_TRUE) ? 1 : 0;
#endif /* SCSI_PARITY_PER_IOB */

#if SCSI_SELTO_PER_IOB     
         hiob->seltoPeriod = transportSpecific->selectionTimeout;
#endif /* SCSI_SELTO_PER_IOB */                              

#if SCSI_DOMAIN_VALIDATION
         if (transportSpecific->dvIOB == HIM_TRUE)
         {
            if (transportSpecific ==
                ((void HIM_PTR) 
                 (((SCSI_UEXACT8 HIM_PTR) ((SCSI_TRGTSH(taskSetHandle)->adapterTSH)->moreUnlocked)) +
                   SCSI_MORE_TRANSPORT_SPECIFIC)))
            {
               /* This INITIATE_TASK IOB was built from HIM_PROBE or
                * HIM_PROTOCOL_AUTO_CONFIG. Therefore, set the
                * dvIOB flag.
                */
               hiob->SCSI_IF_dvIOB = 1;
            }
            else
            {
               hiob->SCSI_IF_dvIOB = 0;
            }
         }
         else
         {
            hiob->SCSI_IF_dvIOB = 0;
         }
#endif /* SCSI_DOMAIN_VALIDATION */
      }
      else
      {
         hiob->SCSI_IF_tagEnable = ((SCSI_TRGTSH(taskSetHandle)->targetAttributes.tagEnable) &&
                                    (iob->taskAttribute != HIM_TASK_RECOVERY));
         hiob->SCSI_IF_disallowDisconnect = 0;
#if SCSI_NEGOTIATION_PER_IOB
         hiob->SCSI_IF_forceSync = 0;
         hiob->SCSI_IF_forceAsync = 0;
         hiob->SCSI_IF_forceWide = 0;
         hiob->SCSI_IF_forceNarrow = 0;
         hiob->SCSI_IF_forceReqSenseNego = 0;
#endif /* SCSI_NEGOTIATION_PER_IOB */
#if SCSI_PARITY_PER_IOB
         hiob->SCSI_IF_parityEnable = 0;
#endif /* SCSI_PARITY_PER_IOB */

#if SCSI_DOMAIN_VALIDATION
         hiob->SCSI_IF_dvIOB = 0;
#endif /* SCSI_DOMAIN_VALIDATION */
      }

      hiob->SCSI_IF_autoSense = iob->flagsIob.autoSense;
      hiob->SCSI_IF_freezeOnError = iob->flagsIob.freezeOnError;
      hiob->priority = iob->priority;
      hiob->snsBuffer = iob->errorData;
#if SCSI_PACKETIZED_IO_SUPPORT
      hiob->SCSI_IF_tmfValid = 0;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

      if (iob->errorDataLength <= SCSI_HIOB_MAX_SENSE_LENGTH)
      {
         hiob->snsLength = (SCSI_UEXACT8) iob->errorDataLength;
      }
      else
      {
         hiob->snsLength = (SCSI_UEXACT8) SCSI_HIOB_MAX_SENSE_LENGTH;
      }
#if SCSI_PACKETIZED_IO_SUPPORT           
#if (SCSI_TASK_SWITCH_SUPPORT == 0)
      if ((uexact8Pointer[0] == 0x03) && 
           SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))   
      {
         OSDmemcpy ((SCSI_UEXACT8 HIM_PTR) hiob->snsBuffer,
                     (SCSI_UEXACT8 HIM_PTR) SCSI_TARGET_UNIT(hiob)->senseBuffer, 
                      hiob->snsLength );
                           
         /* process successful status */
         iob->taskStatus = HIM_IOB_GOOD;           
         iob->postRoutine(iob); 
         return;                         
      }            
#endif /* SCSI_TASK_SWITCH_SUPPORT == 0 */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

      /* send request to internal HIM */
      SCSI_QUEUE_HIOB(hiob);
   }
   else
   {
      /* process other special iob */
      switch(iob->function)
      {
         case HIM_TERMINATE_TASK:
         case HIM_ABORT_TASK:
            if (SCSIxIsInvalidRelatedIob(iob) == HIM_TRUE)
            {
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
               return ;
            }
            hiob->cmd = SCSI_CMD_ABORT_TASK;
            hiob->unitHandle.relatedHiob = &SCSI_IOBRSV(iob->relatedIob)->hiob;
            break;

         case HIM_ABORT_TASK_SET:
            hiob->cmd = SCSI_CMD_ABORT_TASK_SET;
            hiob->unitHandle.targetUnit = &SCSI_TRGTSH(taskSetHandle)->unitControl;
            break;

         case HIM_RESET_BUS_OR_TARGET:
            if (SCSI_TRGTSH(taskSetHandle)->typeTSCB == SCSI_TSCB_TYPE_TARGET)
            {
               /* reset target */
               hiob->cmd =  SCSI_CMD_RESET_TARGET;
               hiob->unitHandle.targetUnit = &SCSI_TRGTSH(taskSetHandle)->unitControl;
            }
            else if (SCSI_TRGTSH(taskSetHandle)->typeTSCB == SCSI_TSCB_TYPE_ADAPTER)
            {
               /* reset scsi bus */
               hiob->cmd = SCSI_CMD_RESET_BUS;
               hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
#if SCSI_DOMAIN_VALIDATION
               hhcb = &SCSI_ADPTSH(taskSetHandle)->hhcb;
               
               /* determine if data bus hang condition and Adapter profile */
               /* AP_SCSIOSMResetBusUseThrottledDownSpeedforDV is enabled. */
               SCSIxHandledvThrottleOSMReset(hhcb);
#endif /* SCSI_DOMAIN_VALIDATION */
            }
            else
            {
               /* illegal request */
               iob->taskStatus = HIM_IOB_TSH_INVALID;
               iob->postRoutine(iob);
               return ;
            }
            break;

         case HIM_RESET_HARDWARE:
            hiob->cmd = SCSI_CMD_RESET_HARDWARE;
            hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
            break;

         case HIM_PROTOCOL_AUTO_CONFIG:
            hiob->cmd = SCSI_CMD_PROTO_AUTO_CFG;
            hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
            break;

#if !SCSI_DISABLE_PROBE_SUPPORT
         case HIM_PROBE:
            if ((transportSpecific) &&
#if !SCSI_NULL_SELECTION
                (transportSpecific->scsiID != 
                 SCSI_ADPTSH(taskSetHandle)->hhcb.hostScsiID) &&
#endif /* !SCSI_NULL_SELECTION */
                !(transportSpecific->LUN[0]))
            {
               SCSI_ADPTSH(iob->taskSetHandle)->iobProtocolAutoConfig = iob;
               SCSIxSetupLunProbe(SCSI_ADPTSH(iob->taskSetHandle),
                  transportSpecific->scsiID,
                  transportSpecific->LUN[1]);
               SCSIxQueueBusScan(SCSI_ADPTSH(iob->taskSetHandle));
               SCSI_ADPTSH(iob->taskSetHandle)->lastScsiIDProbed = 
                  transportSpecific->scsiID;
               SCSI_ADPTSH(iob->taskSetHandle)->lastScsiLUNProbed = 
                  transportSpecific->LUN[1];
            }
            else
            {
               /* invalid probe */
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
            }
            return;
#endif /* !SCSI_DISABLE_PROBE_SUPPORT */

         case HIM_SUSPEND:
         case HIM_QUIESCE:
            if (SCSI_CHECK_HOST_IDLE(&SCSI_ADPTSH(taskSetHandle)->hhcb) == SCSI_IDLE)
            {
               /* host device is idle and nothing has to be done */
               iob->taskStatus = HIM_IOB_GOOD;
            }
            else
            {
               /* indicate host adapter is not idle */
               iob->taskStatus = HIM_IOB_ADAPTER_NOT_IDLE;
            }

#if SCSI_TARGET_OPERATION
            hhcb = &SCSI_ADPTSH(taskSetHandle)->hhcb;
            
            if (hhcb->SCSI_HF_targetMode)
            {
               /* Disable selection-in */
               SCSI_DISABLE_SELECTION_IN(hhcb);
               
               /* Need to check if the SCSI bus is held or */
               /* any pending selection-in interrupts      */
               if ((hhcb->SCSI_HF_targetScsiBusHeld) ||
                   (SCSI_CHECK_SELECTION_IN_INTERRUPT_PENDING(hhcb))) 
               {
                  /* indicate that adapter is not idle */ 
                  iob->taskStatus = HIM_IOB_ADAPTER_NOT_IDLE;
               }
            }
#endif /* SCSI_TARGET_OPERATION */
            iob->postRoutine(iob);
            return ;

         case HIM_RESUME:
#if SCSI_TARGET_OPERATION
            /* Target Mode enabled then this function
             * enables selection-in
             */
            hhcb = &SCSI_ADPTSH(taskSetHandle)->hhcb;
            SCSI_hTARGETMODEENABLE(hhcb);
#endif /* SCSI_TARGET_OPERATION */            
            /* just return good status and nothing has to be done */
            iob->taskStatus = HIM_IOB_GOOD;
            iob->postRoutine(iob);
            return ;

         case HIM_CLEAR_XCA:
            /* not supported for this implementation */
            iob->taskStatus = HIM_IOB_UNSUPPORTED;
            iob->postRoutine(iob);
            return ;

         case HIM_UNFREEZE_QUEUE:
            /* unfreeze device queue */
            hiob->cmd = SCSI_CMD_UNFREEZE_QUEUE;
            hiob->unitHandle.targetUnit = &SCSI_TRGTSH(taskSetHandle)->unitControl;
            break;

#if SCSI_TARGET_OPERATION
         case HIM_ESTABLISH_CONNECTION:
#if SCSI_RESOURCE_MANAGEMENT
            hiob->cmd = SCSI_CMD_ESTABLISH_CONNECTION;
            hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
            /* snsBuffer contains the address of where the 
               received command or task management request 
               is to be stored. */
            hiob->snsBuffer = iob->targetCommand;
            hiob->snsBufferSize = (SCSI_UEXACT16) iob->targetCommandBufferSize;
            break;
#else
            iob->taskStatus = HIM_IOB_INVALID;
            iob->postRoutine(iob);
            return;
  
#endif /* SCSI_RESOURCE_MANAGEMENT */
         case HIM_REESTABLISH_AND_COMPLETE:
            if ( (iob->targetCommandLength != 1) || 
                 (iob->flagsIob.outOfOrderTransfer) ) 
            {
               /* SCSI protocol expects one byte of status */
               /* outOfOrderTransfer not supported */ 
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
               return; 
            }
            hiob->cmd = SCSI_CMD_REESTABLISH_AND_COMPLETE;
            SCSI_NEXUS_UNIT(hiob) = SCSI_NEXUSTSH(taskSetHandle);
            hiob->ioLength = iob->ioLength;
            hiob->snsBufferSize = (SCSI_UEXACT16) iob->targetCommandBufferSize;
            scsiStatus = (SCSI_UEXACT8 HIM_PTR) iob->targetCommand;
            hiob->initiatorStatus = scsiStatus[0];
            if (iob->flagsIob.targetRequestType == HIM_REQUEST_TYPE_TMF)
            {
               hiob->SCSI_IF_taskManagementResponse = 1;
            }
            else
            {
               hiob->SCSI_IF_taskManagementResponse = 0;
            }  
            break;
             
         case HIM_REESTABLISH_INTERMEDIATE:
            if (iob->flagsIob.outOfOrderTransfer) 
            {
               /* Not supported */
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
               return; 
            }
            hiob->cmd = SCSI_CMD_REESTABLISH_INTERMEDIATE;
            SCSI_NEXUS_UNIT(hiob) = SCSI_NEXUSTSH(taskSetHandle);
            hiob->ioLength = iob->ioLength;
            /* hiob->snsBuffer = iob->errorData;  */
            hiob->snsBufferSize = (SCSI_UEXACT16)iob->targetCommandBufferSize;
            if (transportSpecific)
            {
               hiob->SCSI_IF_disallowDisconnect = transportSpecific->disallowDisconnect;
            }
            else
            {
               hiob->SCSI_IF_disallowDisconnect = 0; 
            }
            break;

         case HIM_ABORT_NEXUS:
            hiob->cmd = SCSI_CMD_ABORT_NEXUS;
            SCSI_NEXUS_UNIT(hiob) = SCSI_NEXUSTSH(taskSetHandle);
            break;

#if SCSI_MULTIPLEID_SUPPORT
         case HIM_ENABLE_ID:
            if (!SCSIxEnableID(iob)) 
            {
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
               return;
            }
            else
            {
               hiob->cmd = SCSI_CMD_ENABLE_ID;
               hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
               hiob->snsBufferSize =
                  ((HIM_TS_ID_SCSI HIM_PTR) iob->transportSpecific)->scsiIDMask;
            }
            break;

         case HIM_DISABLE_ID:
            if (!SCSIxDisableID(iob))
            {
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
               return;
            }
            else
            {
               hiob->cmd = SCSI_CMD_DISABLE_ID;
               hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
               hiob->snsBufferSize =
                  ((HIM_TS_ID_SCSI HIM_PTR) iob->transportSpecific)->scsiIDMask;
            }
            break;
          
#endif /* SCSI_MULTIPLEID_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */

         case HIM_LOGICAL_UNIT_RESET:
            hiob->cmd = SCSI_CMD_LOGICAL_UNIT_RESET;
            hiob->unitHandle.targetUnit = &SCSI_TRGTSH(taskSetHandle)->unitControl;
            break;
            
         default:
            /* indicate iob invalid and post back immediately */
            iob->taskStatus = HIM_IOB_INVALID;
            iob->postRoutine(iob);
            return ;
      }

      /* send request to internal HIM */
      SCSI_QUEUE_SPECIAL_HIOB(hiob);
   }
}
#endif /* !SCSI_STREAMLINE_QIOPATH */

/*********************************************************************
*
*  SCSIPowerEvent
*
*     Queue the IOB for execution
*
*  Return Value:  0, no outstanding tasks. hibernation successful
*                 others, outstanding tasks. hibernation uncessful
*                  
*  Parameters:    adapter TSH
*                 severity
*
*  Remarks:       The implementation of this routine wil just
*                 call SCSIHPowerManagement routine to handle the
*                 actual power management function.
*                 
*
*********************************************************************/
HIM_UINT8 SCSIPowerEvent (HIM_TASK_SET_HANDLE adapterTSH,
                          HIM_UINT8 severity)
{
   return((HIM_UINT8)SCSI_POWER_MANAGEMENT(&SCSI_ADPTSH(adapterTSH)->hhcb,
            (SCSI_UINT) severity));
}

/*********************************************************************
*
*  SCSIValidateTargetTSH
*
*     Validate if the specified target TSH is still valid. 
*
*  Return Value:  HIM_TARGET_VALID, target TSH is still valid
*                 HIM_TARGET_CHANGED, target TSH is still valid 
*                                     but the scsi id changed
*                 HIM_TARGET_INVALID, target TSH is not valid any more
*                  
*  Parameters:    target TSH
*
*  Remarks:       This routine will have to remember what the device 
*                 table was and compare it with the device table looks
*                 like after the protocol automatic configuration.
*
*********************************************************************/
HIM_UINT8 SCSIValidateTargetTSH (HIM_TASK_SET_HANDLE targetTSH)
{
#if SCSI_INITIATOR_OPERATION
   return(SCSI_xVALIDATE_LUN((SCSI_TARGET_TSCB HIM_PTR) targetTSH));
#else
   return((HIM_UINT8)(HIM_TARGET_INVALID));
#endif /* SCSI_INITIATOR_OPERATION */ 
}

/*********************************************************************
*
*  SCSIxNonScamValidateLun
*
*     Validate if the specified target TSH is still valid with
*     scam disabled. 
*
*  Return Value:  HIM_TARGET_VALID, target TSH is still valid
*                 HIM_TARGET_CHANGED, target TSH is still valid 
*                                     but the scsi id changed
*                 HIM_TARGET_INVALID, target TSH is not valid any more
*                  
*  Parameters:    target TSH
*
*  Remarks:
*
*********************************************************************/
HIM_UINT8 SCSIxNonScamValidateLun (SCSI_TARGET_TSCB HIM_PTR targetTSH)
{
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH =
               ((SCSI_TARGET_TSCB HIM_PTR)targetTSH)->adapterTSH;
   
   /* for non scam device we just assume it's valid if */
   /* device exist */
   if (SCSIxChkLunExist(adapterTSH->lunExist,targetTSH->scsiID,0))
   {
      return((HIM_UINT8)(HIM_TARGET_VALID));
   }
   else
   {
      return((HIM_UINT8)(HIM_TARGET_INVALID));
   }
}


/*********************************************************************
*
*  SCSIClearTargetTSH
*
*     Invalidate target TSH
*
*  Return Value:  HIM_SUCCESS, target TSH is cleared and the 
*                              associated TSCB memory can be freed
*                 HIM_FAILURE, target TSH can not be invalidated
*                              (e.g. initiator mode not enabled) 
*                 HIM_TARGET_NOT_IDLE, target TSH is not idle and 
*                                      can not be invalidated
*                  
*  Parameters:    target TSH
*
*  Remarks:       OSM can call this routine to free unused target TSH
*                 for other usage at any time after the call to
*                 execute protocol automatic configuration.
*
*********************************************************************/
HIM_UINT8 SCSIClearTargetTSH (HIM_TASK_SET_HANDLE targetTSH)
{
#if SCSI_INITIATOR_OPERATION
    
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH =
               ((SCSI_TARGET_TSCB HIM_PTR)targetTSH)->adapterTSH;
   SCSI_HHCB HIM_PTR hhcb = &((SCSI_ADAPTER_TSCB HIM_PTR) adapterTSH)->hhcb;
   SCSI_UNIT_CONTROL HIM_PTR targetUnit = &SCSI_TRGTSH(targetTSH)->unitControl;
   
   if (!hhcb->SCSI_HF_initiatorMode) 
   {
      return((HIM_UINT8)(HIM_FAILURE));
   }

   /* check if associated target is busy */
   if (SCSI_CHECK_DEVICE_IDLE(&adapterTSH->hhcb,
                (SCSI_UEXACT8)SCSI_TRGTSH(targetTSH)->scsiID) == SCSI_NOT_IDLE)
   {
      return((HIM_UINT8)(HIM_TARGET_NOT_IDLE));
   }

   /* invalidate task set handle (and associated lun) */
   SCSIxClearLunExist(adapterTSH->lunExist,SCSI_TRGTSH(targetTSH)->scsiID,
      SCSI_TRGTSH(targetTSH)->lunID);
   SCSIxClearLunExist(adapterTSH->tshExist,SCSI_TRGTSH(targetTSH)->scsiID,
      SCSI_TRGTSH(targetTSH)->lunID);

   /* free the unit handle */
   SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));

   /* indicate target cleared for now */
   return((HIM_UINT8)(HIM_SUCCESS));
#else
   return((HIM_UINT8)(HIM_FAILURE));
#endif /* SCSI_INITIATOR_OPERATION */
}

/*********************************************************************
*
*  SCSISaveState
*
*     Save current hardware state
*
*  Return Value:  none
*                  
*  Parameters:    target TSH
*                 pointer to state memory
*
*  Remarks:       OSM must make enough memory get allocated for
*                 state saving. The size of state memory requirement
*                 can be found through adapter profile.
*
*********************************************************************/
void SCSISaveState (HIM_TASK_SET_HANDLE adapterTSH,
                    void HIM_PTR pState)
{
#if SCSI_SAVE_RESTORE_STATE
   SCSI_SAVE_STATE(&SCSI_ADPTSH(adapterTSH)->hhcb,(SCSI_STATE HIM_PTR) pState);
#endif /* SCSI_SAVE_RESTORE_STATE */
}

/*********************************************************************
*
*  SCSIRestoreState
*
*     Restore state from the state memory
*
*  Return Value:  none
*                  
*  Parameters:    target TSH
*                 pointer to state memory
*
*  Remarks:       Typically the state memory has information saved
*                 with previous call to SCSISave.
*
*********************************************************************/
void SCSIRestoreState (HIM_TASK_SET_HANDLE adapterTSH,
                       void HIM_PTR pState)
{
#if SCSI_SAVE_RESTORE_STATE
   SCSI_RESTORE_STATE(&SCSI_ADPTSH(adapterTSH)->hhcb,(SCSI_STATE HIM_PTR) pState);
#endif /* SCSI_SAVE_RESTORE_STATE */
}

/*********************************************************************
*
*  SCSIProfileAdapter
*
*     Get adapter profile information
*
*  Return Value:  HIM_SUCCESS, normal return
*                 HIM_FAILURE, operation failure
*                  
*  Parameters:    adapter TSH
*                 profile memory
*
*  Remarks:       Some of the profile information are transport
*                 specific and OSM should either avoid or isolate
*                 accessing profile information if possible.
*                 Pretty much CHIM will have to maintain the
*                 profile information itself.
*
*********************************************************************/
HIM_UINT8 SCSIProfileAdapter (HIM_TASK_SET_HANDLE adapterTSH,
                              HIM_ADAPTER_PROFILE HIM_PTR profile)         
{
#if SCSI_PROFILE_INFORMATION
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb;
   SCSI_UINT16 i;
   SCSI_UINT16 maxSpeed;
   SCSI_HW_INFORMATION hwInformation;
#if SCSI_TARGET_OPERATION 
   SCSI_DEVICE SCSI_DPTR deviceTable; 
#endif /* SCSI_TARGET_OPERATION */

   SCSI_GET_HW_INFORMATION(&hwInformation, hhcb);
   
   /* copy the default profile for now */
   OSDmemcpy(profile,&SCSIDefaultAdapterProfile,sizeof(HIM_ADAPTER_PROFILE));

   /* update fields which are adapter specific */
   /* must calculate the WWID for the HA because there is no guarantee that   */
   /* protocol auto config has been called yet which would leave the prev     */
   /* calculated WWID for the HA invalid.                                     */
   for (i = 0; i <profile->AP_WWIDLength; i++)
   {
      profile->AP_WorldWideID[i] = hwInformation.WWID[i];
   }
#if SCSI_BIOS_ASPI8DOS
   profile->AP_BiosActive =
            (SCSI_ADPTSH(adapterTSH)->biosInformation.biosFlags.biosActive) ?
             HIM_TRUE : HIM_FALSE;
   profile->AP_ExtendedTrans = SCSI_ADPTSH(adapterTSH)->
               biosInformation.biosFlags.extendedTranslation;
   if (SCSI_ADPTSH(adapterTSH)->biosInformation.versionFormat == HIM_BV_FORMAT1)
   {
      profile->AP_BiosVersionFormat = HIM_BV_FORMAT1;

      profile->AP_BiosVersion.himu.BV_FORMAT1.AP_MajorNumber = 
         (HIM_UINT8)SCSI_ADPTSH(adapterTSH)->biosInformation.majorNumber;
      profile->AP_BiosVersion.himu.BV_FORMAT1.AP_MinorNumber = 
         (HIM_UINT8)SCSI_ADPTSH(adapterTSH)->biosInformation.minorNumber;
      profile->AP_BiosVersion.himu.BV_FORMAT1.AP_SubMinorNumber = 
         (HIM_UINT8)SCSI_ADPTSH(adapterTSH)->biosInformation.subMinorNumber;
   }
   else
   {
      profile->AP_BiosVersionFormat = HIM_BV_FORMAT_UNKNOWN;
   }
#endif /* SCSI_BIOS_ASPI8DOS */
               
   profile->AP_MaxTargets = (HIM_UINT32)hhcb->maxDevices;               
   profile->AP_MaxInternalIOBlocks = (HIM_UINT32)hhcb->numberScbs;

   profile->AP_InitiatorMode = (hhcb->SCSI_HF_initiatorMode) ? HIM_TRUE : HIM_FALSE;
   profile->AP_TargetMode = (hhcb->SCSI_HF_targetMode) ? HIM_TRUE : HIM_FALSE;
   profile->AP_OverrideOSMNVRAMRoutines = (hhcb->SCSI_HF_OverrideOSMNVRAMRoutines) ? HIM_TRUE : HIM_FALSE;
  
#if SCSI_TARGET_OPERATION
   if (hhcb->SCSI_HF_targetMode)
   {
      profile->AP_TargetNumNexusTaskSetHandles =
         (HIM_UINT32)hhcb->SCSI_HF_targetNumberNexusHandles; 
      profile->AP_TargetNumNodeTaskSetHandles =
         (HIM_UINT32)hhcb->SCSI_HF_targetNumberNodeHandles;
      profile->AP_TargetDisconnectAllowed = 
         (hwInformation.SCSI_PF_disconnectAllowed) ? HIM_TRUE : HIM_FALSE;
      profile->AP_TargetTagEnable =
         (hwInformation.SCSI_PF_tagEnable) ? HIM_TRUE : HIM_FALSE;
      /* profile->AP_OutofOrderTransfers = (hhcb->SCSI_HF_targetOutOfOrderTransfers) ? HIM_TRUE : HIM_FALSE;  */
      profile->AP_TargetNumIDs = (HIM_UINT8)hhcb->SCSI_HF_targetNumIDs;
      profile->AP_TargetInternalEstablishConnectionIOBlocks =
         (HIM_UINT32)hhcb->SCSI_HF_targetNumberEstScbs;
#if SCSI_RESOURCE_MANAGEMENT
/* May move into SCSI_GET_HW_INFORMATION */
      profile->AP_NexusHandleThreshold =
         (HIM_UINT32)hhcb->SCSI_HF_targetNexusThreshold;
      profile->AP_EC_IOBThreshold =
         (HIM_UINT32)hhcb->SCSI_HF_targetHiobQueueThreshold;      
      profile->AP_TargetAvailableEC_IOBCount =
         (HIM_UINT32)hhcb->SCSI_HF_targetHiobQueueCnt; 
      profile->AP_TargetAvailableNexusCount =
         (HIM_UINT32)hhcb->SCSI_HF_targetNexusQueueCnt;
#endif /* SCSI_RESOURCE_MANAGEMENT */

      profile->AP_SCSITargetOptTmFunctions.AP_SCSITargetAbortTask =
         (hhcb->SCSI_HF_targetAbortTask) ? HIM_TRUE : HIM_FALSE;  
      profile->AP_SCSITargetOptTmFunctions.AP_SCSITargetClearTaskSet =
         (hhcb->SCSI_HF_targetClearTaskSet) ? HIM_TRUE : HIM_FALSE;
      profile->AP_SCSITargetOptTmFunctions.AP_SCSITargetTerminateTask =
         (hhcb->SCSI_HF_targetTerminateTask) ? HIM_TRUE : HIM_FALSE;
      profile->AP_SCSITargetOptTmFunctions.AP_SCSI3TargetClearACA =
         (hhcb->SCSI_HF_targetClearACA) ? HIM_TRUE : HIM_FALSE;
      profile->AP_SCSITargetOptTmFunctions.AP_SCSI3TargetLogicalUnitReset =
         (hhcb->SCSI_HF_targetLogicalUnitReset) ? HIM_TRUE : HIM_FALSE;

      profile->himu.TS_SCSI.AP_SCSIHostTargetVersion =
         (HIM_UINT8)hwInformation.hostTargetAnsiVersion;
      profile->himu.TS_SCSI.AP_SCSI2_IdentifyMsgRsv = 
         (hwInformation.SCSI_PF_identifyMsgRsv) ? HIM_TRUE : HIM_FALSE;
      profile->himu.TS_SCSI.AP_SCSI2_TargetRejectLuntar = 
         (hwInformation.SCSI_PF_rejectLunTar) ? HIM_TRUE : HIM_FALSE;
      profile->himu.TS_SCSI.AP_SCSIGroup6CDBSize =
         (HIM_UINT8)hwInformation.group6CDBSize;
      profile->himu.TS_SCSI.AP_SCSIGroup7CDBSize = 
         (HIM_UINT8)hwInformation.group7CDBSize;
  
      profile->himu.TS_SCSI.AP_SCSITargetInitNegotiation =
         (hhcb->SCSI_HF_targetInitNegotiation) ? HIM_TRUE : HIM_FALSE;
      profile->himu.TS_SCSI.AP_SCSITargetIgnoreWideResidue =
         (hhcb->SCSI_HF_targetIgnoreWideResidMsg) ? HIM_TRUE : HIM_FALSE;
      profile->himu.TS_SCSI.AP_SCSITargetEnableSCSI1Selection =
         (hwInformation.SCSI_PF_enableScsi1Selection) ? HIM_TRUE : HIM_FALSE;

      /* get device table entry for this adapter's SCSI ID */ 
      deviceTable =  &SCSI_DEVICE_TABLE(hhcb)[(HIM_UINT8) hwInformation.hostScsiID];
      /* Note that; if NVRAM/EEPROM was not present all device table entries          */
      /* get SCSI_DF_ultraEnable set to the same value - the max supported by the h/w */  
   
    
      /* Revisit this area */ 
      profile->himu.TS_SCSI.AP_SCSITargetMaxSpeed = 
         (HIM_UINT16)hwInformation.targetInfo[hwInformation.hostScsiID].SCSIMaxSpeed;
      profile->himu.TS_SCSI.AP_SCSITargetDefaultSpeed =
         (HIM_UINT16)hwInformation.targetInfo[hwInformation.hostScsiID].SCSIDefaultSpeed;

      profile->himu.TS_SCSI.AP_SCSITargetMaxOffset = 
         (HIM_UINT8)hwInformation.targetInfo[hwInformation.hostScsiID].SCSIMaxOffset;
      profile->himu.TS_SCSI.AP_SCSITargetDefaultOffset =
         (HIM_UINT8)hwInformation.targetInfo[hwInformation.hostScsiID].SCSIDefaultOffset;

      profile->himu.TS_SCSI.AP_SCSITargetMaxWidth = 
         (HIM_UINT8)hwInformation.targetInfo[hwInformation.hostScsiID].SCSIMaxWidth;
      profile->himu.TS_SCSI.AP_SCSITargetDefaultWidth = 
         (HIM_UINT8)hwInformation.targetInfo[hwInformation.hostScsiID].SCSIDefaultWidth;

      profile->himu.TS_SCSI.AP_SCSITargetMaxProtocolOptionMask = 
         (HIM_UINT8)
            hwInformation.targetInfo[hwInformation.hostScsiID].SCSIMaxProtocolOptionMask;
      profile->himu.TS_SCSI.AP_SCSITargetDefaultProtocolOptionMask = 
         (HIM_UINT8)
            hwInformation.targetInfo[hwInformation.hostScsiID].SCSIDefaultProtocolOptionMask;

      profile->himu.TS_SCSI.AP_SCSITargetAdapterIDMask =
         (HIM_UEXACT16)hwInformation.targetIDMask;

      profile->himu.TS_SCSI.AP_SCSITargetDGCRCInterval =
         (HIM_UINT32)hwInformation.dataGroupCrcInterval;

#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
      profile->himu.TS_SCSI.AP_SCSITargetIUCRCInterval =
         (HIM_UINT32)hwInformation.iuCrcInterval;
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
   }  /* end of hhcb->SCSI_HF_targetMode */
   
   
#endif /* SCSI_TARGET_OPERATION */

   /* Getting the threshold is from the hhcb and changing it changes */
   /* the hhcb and the HA hardware if the host is idle. */
   profile->AP_FIFOSeparateRWThreshold = 
      (hwInformation.SCSI_PF_separateRWThreshold) ? HIM_TRUE : HIM_FALSE;      
   profile->AP_FIFOSeparateRWThresholdEnable = 
      (hwInformation.SCSI_PF_separateRWThresholdEnable) ? HIM_TRUE : HIM_FALSE;      

   if ((hwInformation.SCSI_PF_separateRWThresholdEnable))
   {
      /* Determine AP_FIFOWriteThreshold value */
      switch (hwInformation.writeThreshold)
      {
         case 0x00:
            profile->AP_FIFOWriteThreshold = 16;
            break;
         case 0x01:
            profile->AP_FIFOWriteThreshold = 25;
            break;
         case 0x02:
            profile->AP_FIFOWriteThreshold = 50;
            break;
         case 0x03:
            profile->AP_FIFOWriteThreshold = 62;
            break;
         case 0x04:
            profile->AP_FIFOWriteThreshold = 75;
            break;
         case 0x05:
            profile->AP_FIFOWriteThreshold = 85;
            break;
         case 0x06:
            profile->AP_FIFOWriteThreshold = 90;
            break;
         case 0x07:
            profile->AP_FIFOWriteThreshold = 100;
         default:
            break;
      }

      /* Determine AP_FIFOReadThreshold value */
      switch (hwInformation.readThreshold)
      {
         case 0x00:
            profile->AP_FIFOReadThreshold = 16;
            break;
         case 0x01:
            profile->AP_FIFOReadThreshold = 25;
            break;
         case 0x02:
            profile->AP_FIFOReadThreshold = 50;
            break;
         case 0x03:
            profile->AP_FIFOReadThreshold = 62;
            break;
         case 0x04:
            profile->AP_FIFOReadThreshold = 75;
            break;
         case 0x05:
            profile->AP_FIFOReadThreshold = 85;
            break;
         case 0x06:
            profile->AP_FIFOReadThreshold = 90;
            break;
         case 0x07:
            profile->AP_FIFOReadThreshold = 100;
        default:
            break;
      }
   }
   else 
   {
      /* Determine AP_FIFOThreshold field value */ 
      switch (hwInformation.threshold)
      {
         case 0x00:
            profile->AP_FIFOThreshold = 16;
            break;
         case 0x01:
            profile->AP_FIFOThreshold = 25;
            break;
         case 0x02:
            profile->AP_FIFOThreshold = 50;
            break;
         case 0x03:
            profile->AP_FIFOThreshold = 62;
            break;
         case 0x04:
            profile->AP_FIFOThreshold = 75;
            break;
         case 0x05:
            profile->AP_FIFOThreshold = 85;
            break;
         case 0x06:
            profile->AP_FIFOThreshold = 90;
            break;
         case 0x07:
            profile->AP_FIFOThreshold = 100;
         default:
            break;
      }
   }
   
#if SCSI_BIOS_ASPI8DOS   
   /* Get the first drive supported by the bios for this adapter */
   if (SCSI_ADPTSH(adapterTSH)->biosInformation.biosFlags.biosActive)
   {
      profile->AP_LowestScanTarget = 
         (HIM_UINT16) SCSI_ADPTSH(adapterTSH)->biosInformation.firstBiosDrive;
   }
   else
#endif /* SCSI_BIOS_ASPI8DOS */  
   {
      profile->AP_LowestScanTarget = (HIM_UINT16) 0xffff;
   }

   profile->AP_AllocBusAddressSize =
      SCSI_ADPTSH(adapterTSH)->allocBusAddressSize; 

   profile->AP_ResetDelay = (HIM_UINT32)hhcb->resetDelay;

   profile->AP_IOChannelFailureTimeout =
      (HIM_UINT32)hhcb->ioChannelFailureTimeout;

   /* added for PCI error recovery. */
   profile->AP_ClearConfigurationStatus = 
                   (hhcb->SCSI_HF_ClearConfigurationStatus) ? HIM_TRUE : HIM_FALSE;


   /* Not supported at this time
   profile->himu.TS_SCSI.AP_CleanSG = ??;
   profile->himu.TS_SCSI.AP_SCSIForceWide = ??;
   profile->himu.TS_SCSI.AP_SCSIForceNoWide = ??;
   profile->himu.TS_SCSI.AP_SCSIForceSynch = ??;
   profile->himu.TS_SCSI.AP_SCSIForceNoSynch = ??;*/

   /* Getting the Adapter SCSI ID */
   profile->himu.TS_SCSI.AP_SCSIAdapterID = (HIM_UINT8) hwInformation.hostScsiID;

   /* Report the speed supported by the adapter */
   maxSpeed = 50;

   for (i = 0; i < SCSI_MAXDEV; i++)
   {
   
#if SCSI_PACKETIZED_IO_SUPPORT
      if (SCSI_hHARPOON_REV_B(hhcb) || SCSI_hDCH_SCSI_CORE(hhcb))
      {
         profile->himu.TS_SCSI.AP_SCSIRTISupport = HIM_TRUE;
      }   
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

      if (hwInformation.targetInfo[i].SCSIMaxSpeed == 1600)
      {
         profile->himu.TS_SCSI.AP_SCSISpeed = HIM_SCSI_ULTRA320_SPEED;
         break;
      }
      else
      if ((hwInformation.targetInfo[i].SCSIMaxSpeed == 800) &&
          (maxSpeed < 800))
      {
         profile->himu.TS_SCSI.AP_SCSISpeed = HIM_SCSI_ULTRA160M_SPEED;
         maxSpeed = 800; 
      }
      else
      if ((hwInformation.targetInfo[i].SCSIMaxSpeed == 400) &&
          (maxSpeed < 400))
      {
         profile->himu.TS_SCSI.AP_SCSISpeed = HIM_SCSI_ULTRA2_SPEED;
         maxSpeed = 400;
      }
      else
      if ((hwInformation.targetInfo[i].SCSIMaxSpeed == 200) &&
          (maxSpeed < 200))
      {
         profile->himu.TS_SCSI.AP_SCSISpeed = HIM_SCSI_ULTRA_SPEED;
         maxSpeed = 200;
      }
      else
      if ((hwInformation.targetInfo[i].SCSIMaxSpeed == 100) &&
          (maxSpeed < 100))
      {
         profile->himu.TS_SCSI.AP_SCSISpeed = HIM_SCSI_FAST_SPEED;
         maxSpeed = 100;
      }
   }
   
   if (hwInformation.SCSI_PF_wideSupport)
   {
      profile->himu.TS_SCSI.AP_SCSIWidth = 16;
   }
   else
   {
      profile->himu.TS_SCSI.AP_SCSIWidth = 8;
   }
      
   for (i = 0; i < SCSI_MAXDEV; i++)
   {
      /* Currently, our HAs only support up to 16 SCSI devices */
      profile->himu.TS_SCSI.AP_SCSINumberLuns[i] = (HIM_UINT8)SCSI_ADPTSH(adapterTSH)->NumberLuns[i];
   }

   profile->AP_CacheLineStreaming = 
      (hwInformation.SCSI_PF_cacheThEn) ? HIM_TRUE : HIM_FALSE;   
            
   if (hhcb->SCSI_HF_expSupport)
   {
      profile->himu.TS_SCSI.AP_SCSIExpanderDetection = HIM_TRUE;
   }
   else
   {
      profile->himu.TS_SCSI.AP_SCSIExpanderDetection = HIM_FALSE;
   }
   
   /* Get the current address mapped: I/O or memory */
   profile->AP_MemoryMapped = 
      SCSI_ADPTSH(adapterTSH)->scsiHandle.memoryMapped;

   profile->himu.TS_SCSI.AP_SCSIDisableParityErrors =
      (hwInformation.SCSI_PF_scsiParity) ? HIM_FALSE : HIM_TRUE;

   /* Selection Timeout will be either 256, 128, 64 or 32 and    */
   /* equivalent to binary number value as 00b, 01b, 10b or 11b. */
   profile->himu.TS_SCSI.AP_SCSISelectionTimeout =
      ((HIM_UINT16) 256) >> hwInformation.SCSI_PF_selTimeout;

   switch (hwInformation.SCSI_PF_transceiverMode)
   {
      case SCSI_UNKNOWN_MODE:
         profile->himu.TS_SCSI.AP_SCSITransceiverMode = HIM_SCSI_UNKNOWN_MODE;
         break;

      case SCSI_SE_MODE:
         profile->himu.TS_SCSI.AP_SCSITransceiverMode = HIM_SCSI_SE_MODE; 
         break;

      case SCSI_LVD_MODE:
         profile->himu.TS_SCSI.AP_SCSITransceiverMode = HIM_SCSI_LVD_MODE;
         break;

      case SCSI_HVD_MODE:
         profile->himu.TS_SCSI.AP_SCSITransceiverMode = HIM_SCSI_HVD_MODE;
         break;       
   }

#if SCSI_DOMAIN_VALIDATION
   profile->himu.TS_SCSI.AP_SCSIDomainValidationMethod =
      (HIM_UINT8)hhcb->domainValidationMethod;
   profile->himu.TS_SCSI.AP_SCSIDomainValidationIDMask =
      SCSI_ADPTSH(adapterTSH)->domainValidationIDMask;
   profile->himu.TS_SCSI.AP_SCSIOSMResetBusUseThrottledDownSpeedforDV =
      (hhcb->SCSI_HF_dvThrottleOSMReset) ? HIM_TRUE : HIM_FALSE;
#endif /* SCSI_DOMAIN_VALIDATION */

#if SCSI_CURRENT_SENSING
   /* Get current sensing termination status */
   profile->himu.TS_SCSI.AP_SCSICurrentSensingStat_PL = SCSI_hGETCURRENTSENSING(hhcb, 0);
   profile->himu.TS_SCSI.AP_SCSICurrentSensingStat_PH = SCSI_hGETCURRENTSENSING(hhcb, 1);
   profile->himu.TS_SCSI.AP_SCSICurrentSensingStat_SL = SCSI_hGETCURRENTSENSING(hhcb, 2);
   profile->himu.TS_SCSI.AP_SCSICurrentSensingStat_SH = SCSI_hGETCURRENTSENSING(hhcb, 3);
#endif /* SCSI_CURRENT_SENSING */

   profile->himu.TS_SCSI.AP_SCSISuppressNegotiationWithSaveStateRestoreState =
             (hhcb->SCSI_HF_SuppressNegoSaveRestoreState) ? HIM_TRUE : HIM_FALSE;
   profile->himu.TS_SCSI.AP_SCSIForceSTPWLEVELtoOneForEmbeddedChips =
             (hhcb->SCSI_HF_ForceSTPWLEVELforEmbeddedChips) ? HIM_TRUE : HIM_FALSE;

   /* OEM1 Disconnect delay */  
   profile->AP_OEM1Specific.himu.TS_SCSI.AP_SCSIDisconnectDelay =
      (HIM_UINT8)hwInformation.disconnectDelay;

   profile->AP_indexWithinGroup = (HIM_UINT8) hhcb->indexWithinGroup;

   if ((!hwInformation.SCSI_PF_intrThresholdSupport) ||
       ((hwInformation.intrFactorThreshold == 0) &&
        (hwInformation.intrThresholdCount  == 0)))
   {
      /* CmdCmpltIntr reduction logic is NOT enabled in these 2 cases:  */
      /*  a) F/W does not support it (hwinfo.intrThresholdSupport==0)   */
      /*  b) Current hwinfo.intrFactorThreshold and                     */ 
      /*     hwminfo.ntrThresholdCount values are both set to zero.     */
      profile->AP_CmdCompleteIntrThresholdSupport = HIM_FALSE;
   }
   else
   {
      profile->AP_CmdCompleteIntrThresholdSupport = HIM_TRUE;
   }
                  
   profile->AP_SaveRestoreSequencer = 
             (hhcb->SCSI_HF_dontSaveSeqInState) ? HIM_FALSE : HIM_TRUE; 

   if (profile->AP_SaveRestoreSequencer == HIM_FALSE)
   {
      /* adjust AP_StateSize as we will not save sequencer in SCSI_STATE */ 
      profile->AP_StateSize = (HIM_UINT32)OSMoffsetof(SCSI_STATE,seqRam[0]);
   }

   profile->AP_ClusterEnabled =
             (hhcb->SCSI_HF_clusterEnable) ? HIM_TRUE : HIM_FALSE; 

   profile->AP_InitializeIOBus = SCSI_ADPTSH(adapterTSH)->initializeIOBus;

#if SCSI_ELEC_PROFILE
   /* Set Precomp cutback level, the same across all hardware */
   profile->himu.TS_SCSI.AP_SCSIPrecompLevel = SCSI_PCOMP_29;
   profile->himu.TS_SCSI.AP_SCSIMaxWriteBiasControl = SCSI_MAX_WRTBIASCTL;

   /* Manual Write Bias Setting */
   if(hhcb->wrtBiasCtl & SCSI_AUTOEXBCDIS)
   {
      profile->himu.TS_SCSI.AP_SCSIWriteBiasControl = (HIM_UINT8)
         ((hhcb->wrtBiasCtl  >> 1 ) & SCSI_MAX_WRTBIASCTL);
   }
   else
   {
      /* Auto Write Bias Control */
      profile->himu.TS_SCSI.AP_SCSIWriteBiasControl = HIM_AUTO_BIAS_CONTROL;
   }

   /* Set Slew/Amplitude specific to hardware */
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      profile->himu.TS_SCSI.AP_SCSIMaxSlewRate = SCSI_MAX_HARPOON2A_SLEWLEVEL;
      profile->himu.TS_SCSI.AP_SCSIMaxAmplitudeLevel = SCSI_MAX_HARPOON2A_AMPLITUDE;
      profile->himu.TS_SCSI.AP_SCSIAmplitudeLevel = SCSI_MAX_HARPOON2A_AMPLITUDE;

#if SCSI_OEM1_SUPPORT
      if (hhcb->OEMHardware == SCSI_OEMHARDWARE_OEM1)
      {
         /* Note: This is not necessary the setting the hardware */
         /* Is using.  For Harpoon2A, the slew rate is different */
         /* For Legacy Devices */
         profile->himu.TS_SCSI.AP_SCSISlewRate = SCSI_MAX_HARPOON2A_SLEWLEVEL;
      }
      else
#endif /* SCSI_OEM1_SUPPORT */
      {
         /* OEM1 will use slow setting for U320 devices */
         profile->himu.TS_SCSI.AP_SCSISlewRate = SCSI_SLEWRATE_SLOW;
      }
   }
   else
   {
      profile->himu.TS_SCSI.AP_SCSIMaxSlewRate = SCSI_MAX_HARPOON2B_SLEWLEVEL;
      profile->himu.TS_SCSI.AP_SCSIMaxAmplitudeLevel = SCSI_MAX_HARPOON2B_AMPLITUDE;
#if SCSI_OEM1_SUPPORT
      if (hhcb->OEMHardware == SCSI_OEMHARDWARE_OEM1)
      {
         profile->himu.TS_SCSI.AP_SCSIAmplitudeLevel = SCSI_DEFAULT_OEM1_HARPOON2B_AMPLITUDE;
         profile->himu.TS_SCSI.AP_SCSISlewRate = SCSI_DEFAULT_OEM1_HARPOON2B_SLEWRATE;
      }
      else
#endif /* SCSI_OEM1_SUPPORT */
      {
         profile->himu.TS_SCSI.AP_SCSIAmplitudeLevel = SCSI_DEFAULT_HARPOON2B_AMPLITUDE;
         profile->himu.TS_SCSI.AP_SCSISlewRate = SCSI_DEFAULT_HARPOON2B_SLEWRATE;
      }
   }
#endif /* SCSI_ELEC_PROFILE */

   return((HIM_UINT8)(HIM_SUCCESS));
#else
   return((HIM_UINT8)(HIM_FAILURE));

#endif /* SCSI_PROFILE_INFORMATION */
}

/*********************************************************************
*
*  SCSIReportAdjustableAdapterProfile
*
*     Get information about which adapter profile information are
*     adjustable
*
*  Return Value:  HIM_SUCCESS, normal return
*                 HIM_FAILURE, operation failure
*                  
*  Parameters:    adapter TSH
*                 profile adjust mask memory
*
*  Remarks:       
*                 
*********************************************************************/
HIM_UINT8 SCSIReportAdjustableAdapterProfile (HIM_TASK_SET_HANDLE adapterTSH,
                                     HIM_ADAPTER_PROFILE HIM_PTR profileMask)
{
#if SCSI_PROFILE_INFORMATION
    SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb; 
    SCSI_HW_INFORMATION hwInformation;    

   /* copy the adjustable adapter profile for now */
   OSDmemcpy(profileMask,&SCSIAdjustableAdapterProfile,sizeof(HIM_ADAPTER_PROFILE));

   SCSI_GET_HW_INFORMATION(&hwInformation, hhcb);

#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
#if SCSI_STANDARD_ENHANCED_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)
   {
      profileMask->himu.TS_SCSI.AP_SCSITargetIUCRCInterval = 1;
   }
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

#if SCSI_DCH_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DCH_U320)
   {
      profileMask->himu.TS_SCSI.AP_SCSITargetIUCRCInterval = 1;
   }
#endif /* SCSI_DCH_U320_MODE */
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */

   if (hwInformation.SCSI_PF_disconnectDelaySupport)
   {
      profileMask->AP_OEM1Specific.himu.TS_SCSI.AP_SCSIDisconnectDelay = 1;
   }
   
   /* Interrupt reduction logic is not supported in legacy firmware */
   if (!hhcb->SCSI_HF_cmdCompleteThresholdSupport)
   {
      profileMask->AP_CmdCompleteIntrThresholdSupport = HIM_FALSE;
   }

   return((HIM_UINT8)(HIM_SUCCESS));
#else
   return((HIM_UINT8)(HIM_FAILURE));

#endif /* SCSI_PROFILE_INFORMATION */
}

/*********************************************************************
*
*  SCSIAdjustAdapterProfile
*
*     Apply the adjusted adapter profile information
*
*  Return Value:  HIM_SUCCESS, normal return
*                 HIM_FAILURE, HIM was unable to make the legal changes
*                 requested by the OSM.
*                 HIM_ILLEGAL_CHANGE, illegal change
*                 HIM_ADAPTER_NOT_IDLE, profile adjusted is not allowed
*                  
*  Parameters:    adapter TSH
*                 profile memory
*
*  Remarks:       The profile mask should be acquired before
*                 adjusting adapter profile.
*                 
*********************************************************************/
HIM_UINT8 SCSIAdjustAdapterProfile (HIM_TASK_SET_HANDLE adapterTSH,
                                    HIM_ADAPTER_PROFILE HIM_PTR profile)
{                              
#if SCSI_PROFILE_INFORMATION
   SCSI_UINT16 i;
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb;
   SCSI_HW_INFORMATION hwInformation;
   SCSI_UEXACT8 hostScsiID; 
#if SCSI_TARGET_OPERATION 
#if SCSI_MULTIPLEID_SUPPORT
   SCSI_UEXACT16 targetAdapterIDMask;
   SCSI_UINT8 count;
#endif /* SCSI_MULTIPLEID_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_ELEC_PROFILE
   SCSI_UINT8 precomp = 0;
#endif /* SCSI_ELEC_PROFILE */

   /* Return if the host adapter's already initialized and not idle */
   if ((SCSI_ADPTSH(adapterTSH)->SCSI_AF_initialized) &&
       (SCSI_CHECK_HOST_IDLE(hhcb) == SCSI_NOT_IDLE))
   {
      return((HIM_UINT8)(HIM_ADAPTER_NOT_IDLE));
   }

   SCSI_GET_HW_INFORMATION(&hwInformation, hhcb);

   /* Not supported at this time
   ?? = profile->himu.TS_SCSI.AP_CleanSG;
   ?? = profile->himu.TS_SCSI.AP_SCSIForceWide;
   ?? = profile->himu.TS_SCSI.AP_SCSIForceNoWide;
   ?? = profile->himu.TS_SCSI.AP_SCSIForceSynch;
   ?? = profile->himu.TS_SCSI.AP_SCSIForceNoSynch;*/

   hhcb->SCSI_HF_OverrideOSMNVRAMRoutines = (SCSI_UEXACT16) profile->AP_OverrideOSMNVRAMRoutines;
   
   /* Only support AP_ResetDelay values up to SCSI_MAX_RESET_DELAY */ 

   if ((SCSI_UEXACT32)profile->AP_ResetDelay >
       (SCSI_UEXACT32)SCSI_MAX_RESET_DELAY)
   {
      hhcb->resetDelay = (SCSI_UEXACT32)SCSI_MAX_RESET_DELAY;
   }
   else
   { 
      hhcb->resetDelay = (SCSI_UEXACT32)profile->AP_ResetDelay;
   } 

   if ((SCSI_UEXACT32)profile->AP_IOChannelFailureTimeout >
       (SCSI_UEXACT32)SCSI_MAX_RESET_DELAY)
   {
      hhcb->ioChannelFailureTimeout = (SCSI_UEXACT32)SCSI_MAX_RESET_DELAY;
   }
   else
   { 
      hhcb->ioChannelFailureTimeout =
         (SCSI_UEXACT32)profile->AP_IOChannelFailureTimeout;
   } 

   /* added for enhanced error support */
   hhcb->SCSI_HF_ClearConfigurationStatus = 
                  (profile->AP_ClearConfigurationStatus == HIM_TRUE) ? 1 : 0;

   hostScsiID = hwInformation.hostScsiID;

   if ((SCSI_ADPTSH(adapterTSH)->SCSI_AF_initialized &&
        ((hostScsiID != (SCSI_UEXACT8)profile->himu.TS_SCSI.AP_SCSIAdapterID) ||
         (SCSI_ADPTSH(adapterTSH)->initializeIOBus != profile->AP_InitializeIOBus))) ||
       (profile->AP_FIFOSeparateRWThreshold == HIM_FALSE &&
        profile->AP_FIFOSeparateRWThresholdEnable == HIM_TRUE))
   {
      return((HIM_UINT8)(HIM_ILLEGAL_CHANGE)); 
   }
   
   if (!SCSI_ADPTSH(adapterTSH)->SCSI_AF_initialized)
   {
      /* Can only change these fields prior to HIMInitialize */ 
      hhcb->hostScsiID = (SCSI_UEXACT8)profile->himu.TS_SCSI.AP_SCSIAdapterID;
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
      if (hhcb->SCSI_HF_targetMode)
      {
         if (hhcb->SCSI_HF_targetNumIDs == 1)
         {
            /* If supporting one ID only then set ID mask to host SCSI ID. */
            targetAdapterIDMask = (SCSI_UEXACT16)(1 << hhcb->hostScsiID);
         }
         else
         {
            targetAdapterIDMask = profile->himu.TS_SCSI.AP_SCSITargetAdapterIDMask;
         }

         if ((hhcb->SCSI_HF_targetAdapterIDMask !=
              targetAdapterIDMask) ||
             (!((SCSI_UEXACT16)(1 << hhcb->hostScsiID) & hhcb->SCSI_HF_targetAdapterIDMask)))
         {
            /* Mask different or hostScsiID not in current mask */
            count = 0;
            for (i = 0; i < 16; i++)
            {
               /* count the mask bits */
               if (targetAdapterIDMask & (1 << i))
               {
                  count++;
               }
            }

            /* If the number of bits set in the mask is greater 
             * than the number of IDs supported or if the adapter
             * SCSI ID bit is not set in the mask when the mask is
             * non-zero return an error.
             */ 
            if ((count > hhcb->SCSI_HF_targetNumIDs) ||
                (((targetAdapterIDMask & 
                   (1 << hhcb->hostScsiID)) == 0) && 
                 (targetAdapterIDMask != 0)))
            {
               return((HIM_UINT8)(HIM_ILLEGAL_CHANGE)); 
            }
         }

         /* update mask */
         hhcb->SCSI_HF_targetAdapterIDMask = targetAdapterIDMask;
      }
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
   }    

#if SCSI_TARGET_OPERATION
   if (hhcb->SCSI_HF_targetMode)
   {
      if (profile->AP_TargetDisconnectAllowed == HIM_TRUE) 
      {
         hwInformation.SCSI_PF_disconnectAllowed = 1;
      }
      else
      {
         hwInformation.SCSI_PF_disconnectAllowed = 0;
      }

      if (profile->AP_TargetTagEnable == HIM_TRUE)
      {
         hwInformation.SCSI_PF_tagEnable = 1;
      }
      else
      {
         hwInformation.SCSI_PF_tagEnable = 0;
      } 
      
      hwInformation.hostTargetAnsiVersion =
         (SCSI_UEXACT8)profile->himu.TS_SCSI.AP_SCSIHostTargetVersion;

      if ((profile->himu.TS_SCSI.AP_SCSI2_IdentifyMsgRsv == HIM_TRUE) &&
          (profile->himu.TS_SCSI.AP_SCSIHostTargetVersion == HIM_SCSI_2))
      {
         /* Only adjustable when operating as a SCSI-2 target. */
         hwInformation.SCSI_PF_identifyMsgRsv = 1;
      }
      else
      {
         hwInformation.SCSI_PF_identifyMsgRsv = 0;
      } 

      if ((profile->himu.TS_SCSI.AP_SCSI2_TargetRejectLuntar == HIM_TRUE) &&
          (profile->himu.TS_SCSI.AP_SCSIHostTargetVersion == HIM_SCSI_2))
      {
         /* Only adjustable when operating as a SCSI-2 target. */
         hwInformation.SCSI_PF_rejectLunTar = 1;
      }
      else
      {
         hwInformation.SCSI_PF_rejectLunTar = 0;
      }

      hwInformation.group6CDBSize = 
         (SCSI_UEXACT8)profile->himu.TS_SCSI.AP_SCSIGroup6CDBSize;
      hwInformation.group7CDBSize =
         (SCSI_UEXACT8)profile->himu.TS_SCSI.AP_SCSIGroup7CDBSize;

      hhcb->SCSI_HF_targetNexusThreshold = 
         (SCSI_UEXACT16)profile->AP_NexusHandleThreshold;
      hhcb->SCSI_HF_targetHiobQueueThreshold = 
         (SCSI_UEXACT16)profile->AP_EC_IOBThreshold;      

      if (profile->himu.TS_SCSI.AP_SCSITargetInitNegotiation == HIM_TRUE)
      {
         hhcb->SCSI_HF_targetInitNegotiation = 1;
      }
      else
      {
         hhcb->SCSI_HF_targetInitNegotiation = 0;
      }
      
      if (profile->himu.TS_SCSI.AP_SCSITargetIgnoreWideResidue == HIM_TRUE)
      {
         hhcb->SCSI_HF_targetIgnoreWideResidMsg = 1;
      }
      else
      {
         hhcb->SCSI_HF_targetIgnoreWideResidMsg = 0;
      }   

      if (profile->himu.TS_SCSI.AP_SCSITargetEnableSCSI1Selection == HIM_TRUE)
      {
         hwInformation.SCSI_PF_enableScsi1Selection = 1;
      }
      else
      {
         hwInformation.SCSI_PF_enableScsi1Selection = 0;
      }
   
      if (profile->AP_SCSITargetOptTmFunctions.AP_SCSITargetAbortTask ==
          HIM_TRUE)
      {
         hhcb->SCSI_HF_targetAbortTask = 1;
      }
      else
      {
         hhcb->SCSI_HF_targetAbortTask = 0;
      }

      if (profile->AP_SCSITargetOptTmFunctions.AP_SCSITargetClearTaskSet ==
          HIM_TRUE)
      {
         hhcb->SCSI_HF_targetClearTaskSet = 1;
      }
      else
      {
         hhcb->SCSI_HF_targetClearTaskSet = 0;
      }
      
      if (profile->AP_SCSITargetOptTmFunctions.AP_SCSITargetTerminateTask ==
          HIM_TRUE)
      {    
         hhcb->SCSI_HF_targetTerminateTask = 1;
      }
      else
      {
         hhcb->SCSI_HF_targetTerminateTask = 0;
      } 
         
      if (hhcb->SCSI_HF_targetHostTargetVersion == HIM_SCSI_3) 
      {
         if (profile->AP_SCSITargetOptTmFunctions.AP_SCSI3TargetClearACA ==
             HIM_TRUE)
         {
            hhcb->SCSI_HF_targetClearACA = 1;
         }
         else
         {
            hhcb->SCSI_HF_targetClearACA = 0;
         }

         if (profile->AP_SCSITargetOptTmFunctions.AP_SCSI3TargetLogicalUnitReset ==
             HIM_TRUE)               
         { 
            hhcb->SCSI_HF_targetLogicalUnitReset = 1;
         }
         else
         {
             hhcb->SCSI_HF_targetLogicalUnitReset = 0;
         }
      }
      
      if (profile->AP_TargetInternalEstablishConnectionIOBlocks == 0 || 
          profile->AP_TargetInternalEstablishConnectionIOBlocks >= 
          (HIM_UINT32)(hhcb->numberScbs - 3))
      {
         /* Make sure we have a valid number of establish connection SCBs */
         /* We need some scbs for initiator mode and/or reestablish SCBs */
         /* Note; at some point this test may become firmware mode specific */   
         return((HIM_UINT8)(HIM_ILLEGAL_CHANGE));
      }
      else
      {
         hhcb->SCSI_HF_targetNumberEstScbs = 
            (SCSI_UEXACT8)profile->AP_TargetInternalEstablishConnectionIOBlocks;
      }

      if (profile->himu.TS_SCSI.AP_SCSITargetDGCRCInterval <= SCSI_MAX_CRC_VALUE)
      { 
         hwInformation.dataGroupCrcInterval = 
            (SCSI_UEXACT16)profile->himu.TS_SCSI.AP_SCSITargetDGCRCInterval;
      }
      else
      {
         return((HIM_UINT8)(HIM_ILLEGAL_CHANGE));
      }

#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
      if (profile->himu.TS_SCSI.AP_SCSITargetIUCRCInterval <= SCSI_MAX_CRC_VALUE)
      { 
         hwInformation.iuCrcInterval = 
            (SCSI_UEXACT16)profile->himu.TS_SCSI.AP_SCSITargetIUCRCInterval;
      }
      else
      {
         return((HIM_UINT8)(HIM_ILLEGAL_CHANGE));
      }
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
      
      /* Target Mode Negotiation rates */ 
      /* For now assume only changeable before HIMInitialize */ 
      /* Significant changes would be required to this code  */
      /* if these fields are changable after HimInitialize.  */ 
      if (!SCSI_ADPTSH(adapterTSH)->SCSI_AF_initialized)
      {
         /********************************************************
          * WARNING: THIS HAS NOT BEEN IMPLEMENTED YET
          ********************************************************/
         /* Check if change in Target negotiation rates requested */
         /* If so, change all device table entries.               */      
         /* Get a current device table entry - use host adapter's */
         /* ID entry.                                             */ 

      } /* !SCSI_ADPTSH(adapterTSH)->SCSI_AF_initialized */
  
      hwInformation.targetIDMask = hhcb->SCSI_HF_targetAdapterIDMask;
   } /* hhcb->SCSI_HF_targetMode */
#endif /* SCSI_TARGET_OPERATION */

#if (OSM_BUS_ADDRESS_SIZE == 64)
   /* Getting SgBusAddress32 flag */
    hhcb->SCSI_HF_SgBusAddress32 = HIM_FALSE;
    if ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) ||
        (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320))
         hhcb->SCSI_HF_SgBusAddress32 = 
         (profile->AP_SGBusAddress32 == HIM_TRUE) ? 1 : 0;
#endif /* (OSM_BUS_ADDRESS_SIZE == 64) */

   for (i = 0; i < SCSI_MAXDEV; i++)
   {
      /* Update to the maximum SCSI devices currently support by HIM */
      SCSI_ADPTSH(adapterTSH)->NumberLuns[i] = 
           (SCSI_UEXACT8)profile->himu.TS_SCSI.AP_SCSINumberLuns[i];
   }

   /* Getting the threshold is from the hardware and changing it */
   /* changes the hhcb and the HA hardware if the host is idle. */
   hwInformation.SCSI_PF_separateRWThresholdEnable = (SCSI_UEXACT16)
      profile->AP_FIFOSeparateRWThresholdEnable;
   
   /* Separate read/write thresholds */
   if (profile->AP_FIFOSeparateRWThresholdEnable)
   {
      /* write threshold */
      if (profile->AP_FIFOWriteThreshold < 20)       /* 16% */
      {
         hwInformation.writeThreshold = 0x00;
      } 
      else if (profile->AP_FIFOWriteThreshold < 37)  /* 25% */
      {
         hwInformation.writeThreshold = 0x01;
      }
      else if (profile->AP_FIFOWriteThreshold < 56)  /* 50% */
      {
         hwInformation.writeThreshold = 0x02;
      } 
      else if (profile->AP_FIFOWriteThreshold < 69)  /* 62.5% */
      {
         hwInformation.writeThreshold = 0x03;
      } 
      else if (profile->AP_FIFOWriteThreshold < 80)  /* 75% */
      {
         hwInformation.writeThreshold = 0x04;
      }
      else if (profile->AP_FIFOWriteThreshold < 88)  /* 85% */
      {
         hwInformation.writeThreshold = 0x05;
      } 
      else if (profile->AP_FIFOWriteThreshold < 95)  /* 90% */
      {
         hwInformation.writeThreshold = 0x06;
      } 
      else if (profile->AP_FIFOWriteThreshold >=95)  /* 100% */
      {
         hwInformation.writeThreshold = 0x07;
      }

      /* read threshold */
      if (profile->AP_FIFOReadThreshold < 20)       /* 16% */
      {
         hwInformation.readThreshold = 0x00;
      }
      else if (profile->AP_FIFOReadThreshold < 37)  /* 25% */
      {
         hwInformation.readThreshold = 0x01;
      } 
      else if (profile->AP_FIFOReadThreshold < 56)  /* 50% */
      {
         hwInformation.readThreshold = 0x02;
      }
      else if (profile->AP_FIFOReadThreshold < 69)  /* 62.5% */
      {
         hwInformation.readThreshold = 0x03;
      }
      else if (profile->AP_FIFOReadThreshold < 80)  /* 75% */
      {
         hwInformation.readThreshold = 0x04;
      }
      else if (profile->AP_FIFOReadThreshold < 88)  /* 85% */
      {
         hwInformation.readThreshold = 0x05;
      }
      else if (profile->AP_FIFOReadThreshold < 95)  /* 90% */
      {
         hwInformation.readThreshold = 0x06;
      }
      else if (profile->AP_FIFOReadThreshold >=95)  /* 100% */
      {
         hwInformation.readThreshold = 0x07;
      } 
   }
   else 
   {
      /* Use AP_FIFOThreshold field */
      if (profile->AP_FIFOThreshold < 20)       /* 16% */
      {
         hwInformation.threshold = 0x00;
      } 
      else if (profile->AP_FIFOThreshold < 37)  /* 25% */
      {
         hwInformation.threshold = 0x01;
      } 
      else if (profile->AP_FIFOThreshold < 56)  /* 50% */
      {
         hwInformation.threshold = 0x02;
      }
      else if (profile->AP_FIFOThreshold < 69)  /* 62.5% */
      {
         hwInformation.threshold = 0x03;
      }
      else if (profile->AP_FIFOThreshold < 80)  /* 75% */
      {
         hwInformation.threshold = 0x04;
      }
      else if (profile->AP_FIFOThreshold < 88)  /* 85% */
      { 
         hwInformation.threshold = 0x05;
      }
      else if (profile->AP_FIFOThreshold < 95)  /* 90% */
      {
         hwInformation.threshold = 0x06;
      } 
      else if (profile->AP_FIFOThreshold >=95)  /* 100% */
      {
         hwInformation.threshold = 0x07;
      }
   }

   /* Getting the Adapter (SCSI) ID and changes the hhcb */
   /* as well as the HA hardware if the host is idle. */
   hwInformation.hostScsiID = (SCSI_UEXACT8)profile->himu.TS_SCSI.AP_SCSIAdapterID;

   /* Getting Parity Error flag */
   hwInformation.SCSI_PF_scsiParity = 
      (profile->himu.TS_SCSI.AP_SCSIDisableParityErrors == HIM_TRUE) ? 0 : 1;

   /* Getting selection timeout by converting the decimal value to */
   /* a binary number value that will be programmed into HW register. */
   if (profile->himu.TS_SCSI.AP_SCSISelectionTimeout > 192)       /* 256 ms */
   {
      hwInformation.SCSI_PF_selTimeout = SCSI_256MS_SELTO_DELAY;
   }
   else if (profile->himu.TS_SCSI.AP_SCSISelectionTimeout > 96)   /* 128 ms */
   {
      hwInformation.SCSI_PF_selTimeout = SCSI_128MS_SELTO_DELAY;
   }
   else if (profile->himu.TS_SCSI.AP_SCSISelectionTimeout > 48)   /* 64 ms */
   {
      hwInformation.SCSI_PF_selTimeout = SCSI_64MS_SELTO_DELAY;
   }
   else                                                           /* 32 ms */
   {
      hwInformation.SCSI_PF_selTimeout = SCSI_32MS_SELTO_DELAY;
   }

#if SCSI_DOMAIN_VALIDATION
   if (profile->himu.TS_SCSI.AP_SCSIDomainValidationMethod >
       HIM_SCSI_ENHANCED_DOMAIN_VALIDATION)
   {
      return((HIM_UINT8)(HIM_ILLEGAL_CHANGE));
   }
   else
   {
#if SCSI_DOMAIN_VALIDATION_ENHANCED
      hhcb->domainValidationMethod =
         (SCSI_UEXACT8)profile->himu.TS_SCSI.AP_SCSIDomainValidationMethod;
#else /* SCSI_DOMAIN_VALIDATION_ENHANCED */
      if (profile->himu.TS_SCSI.AP_SCSIDomainValidationMethod > 
          HIM_SCSI_BASIC_DOMAIN_VALIDATION)
      {
         return((HIM_UINT8)(HIM_ILLEGAL_CHANGE));
      }
      else
      {
         hhcb->domainValidationMethod =
            (SCSI_UEXACT8)profile->himu.TS_SCSI.AP_SCSIDomainValidationMethod;
      }
#endif /* SCSI_DOMAIN_VALIDATION_ENHANCED */
      SCSI_ADPTSH(adapterTSH)->domainValidationIDMask =
         profile->himu.TS_SCSI.AP_SCSIDomainValidationIDMask;
         
      hhcb->SCSI_HF_dvThrottleOSMReset = 
       (profile->himu.TS_SCSI.AP_SCSIOSMResetBusUseThrottledDownSpeedforDV == HIM_TRUE) ? 1 : 0;

   }
#else /* SCSI_DOMAIN_VALIDATION */
   if (profile->himu.TS_SCSI.AP_SCSIDomainValidationMethod ||
       profile->himu.TS_SCSI.AP_SCSIDomainValidationIDMask ||
       (profile->himu.TS_SCSI.AP_SCSIOSMResetBusUseThrottledDownSpeedforDV !=
        HIM_FALSE))
   {
      return((HIM_UINT8)(HIM_ILLEGAL_CHANGE));
   }
#endif /* SCSI_DOMAIN_VALIDATION */

   if (hwInformation.SCSI_PF_intrThresholdSupport)
   {
      /* This is only for f/w that support CmdCmpltIntr reduction logic */ 

      if (profile->AP_CmdCompleteIntrThresholdSupport == HIM_TRUE)
      {
         /* Set the Command Complete factor threshold and threshold count to the default value */
         hwInformation.intrFactorThreshold =
            (SCSI_UEXACT8)SCSI_CMD_COMPLETE_FACTOR_THRESHOLD;
         hwInformation.intrThresholdCount =
            (SCSI_UEXACT8)SCSI_CMD_COMPLETE_THRESHOLD_COUNT;
      }
      else                                        
      {
         /* Disable Command Complete Interrupt Reduction logic */
         hwInformation.intrFactorThreshold = 0; 
         hwInformation.intrThresholdCount = 0; 
      }
   }


   if (hwInformation.SCSI_PF_disconnectDelaySupport)
   {
      hwInformation.disconnectDelay =
         (SCSI_UEXACT8)profile->AP_OEM1Specific.himu.TS_SCSI.AP_SCSIDisconnectDelay;
   }


   if (profile->himu.TS_SCSI.AP_SCSISuppressNegotiationWithSaveStateRestoreState == HIM_TRUE)
   {
      hhcb->SCSI_HF_SuppressNegoSaveRestoreState = 1;
   }
   else
   {
      hhcb->SCSI_HF_SuppressNegoSaveRestoreState = 0;
   }
   
   
   if (profile->himu.TS_SCSI.AP_SCSIForceSTPWLEVELtoOneForEmbeddedChips == HIM_TRUE)
   {
      hhcb->SCSI_HF_ForceSTPWLEVELforEmbeddedChips = 1;
   }
   else
   {
      hhcb->SCSI_HF_ForceSTPWLEVELforEmbeddedChips = 0;
   }
   
   if (profile->AP_SaveRestoreSequencer == HIM_TRUE)
   {
      hhcb->SCSI_HF_dontSaveSeqInState = 0;
   }
   else
   {
      hhcb->SCSI_HF_dontSaveSeqInState = 1;
   }  

   hwInformation.SCSI_PF_cacheThEn =
      (profile->AP_CacheLineStreaming == HIM_TRUE) ? 1 : 0;

   if (!SCSI_ADPTSH(adapterTSH)->SCSI_AF_initialized)
   {

      /* Handle AP_InitializeIOBus adjustment */
      if (profile->AP_InitializeIOBus == HIM_TRUE)
      {
         hhcb->SCSI_HF_resetSCSI = 1;
      }
      else
      {
         hhcb->SCSI_HF_resetSCSI = 0;
      }

      /* Now set device table entry based on resetSCSI */
      for (i = 0; i < SCSI_MAXDEV; i++)
      {
         SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DF_resetSCSI = hhcb->SCSI_HF_resetSCSI;
      }

      SCSI_ADPTSH(adapterTSH)->initializeIOBus = profile->AP_InitializeIOBus;

      /* End AP_InitializeIOBus adjustment */
   }
#if SCSI_ELEC_PROFILE
      /* Set Write Bias Cancelation Control */
      if(profile->himu.TS_SCSI.AP_SCSIWriteBiasControl <= 
            profile->himu.TS_SCSI.AP_SCSIMaxWriteBiasControl)
      {
         hhcb->wrtBiasCtl = 
            (HIM_UEXACT8) ( (profile->himu.TS_SCSI.AP_SCSIWriteBiasControl  << 1 )| 
                            SCSI_AUTOEXBCDIS | SCSI_BIASPOLARITY );
      }
      else if(profile->himu.TS_SCSI.AP_SCSIWriteBiasControl == HIM_AUTO_BIAS_CONTROL)
      {
         hhcb->wrtBiasCtl = (HIM_UEXACT8)SCSI_AUTO_WRTBIASCTL;
      }

      /* Translate Precomp cutback percentage to bit setting */
      if (profile->himu.TS_SCSI.AP_SCSIPrecompLevel >= 
               SCSI_PCOMP_37)
      {
         precomp = (SCSI_UEXACT8)(SCSI_PCOMP2 | SCSI_PCOMP1 | SCSI_PCOMP0);
      }
      else if (profile->himu.TS_SCSI.AP_SCSIPrecompLevel >= 
         SCSI_PCOMP_29)
      {
         precomp = (SCSI_UEXACT8)(SCSI_PCOMP2 | SCSI_PCOMP1);
      }
      else if (profile->himu.TS_SCSI.AP_SCSIPrecompLevel >= 
         SCSI_PCOMP_17)
      {
          precomp = (SCSI_UEXACT8)(SCSI_PCOMP2);
      }
      else
      {
          precomp = (SCSI_UEXACT8)0;
      }
      for (i = 0; i < SCSI_MAXDEV; i++)
      {
        
         /* Adjust Slew Rate */
         if ((profile->himu.TS_SCSI.AP_SCSISlewRate <= 
              profile->himu.TS_SCSI.AP_SCSIMaxSlewRate) &&
             (profile->himu.TS_SCSI.AP_SCSISlewRate !=
              (SCSI_UEXACT8)SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DEP_slewrate ))
         {

            SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DEP_slewrate  = 
               (SCSI_UEXACT8)profile->himu.TS_SCSI.AP_SCSISlewRate;

            SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DEP_userSlewRate = 1;
            hwInformation.targetInfo[i].SCSI_TF_elecOptChanged = 1;
         }

         /* Adjust Amplitude */
         if ((profile->himu.TS_SCSI.AP_SCSIAmplitudeLevel <= 
              profile->himu.TS_SCSI.AP_SCSIMaxAmplitudeLevel) &&
             (profile->himu.TS_SCSI.AP_SCSIAmplitudeLevel !=
              (SCSI_UEXACT8)SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DEP_amplitude ))
         {
            SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DEP_amplitude = 
               (SCSI_UEXACT8)profile->himu.TS_SCSI.AP_SCSIAmplitudeLevel;

            hwInformation.targetInfo[i].SCSI_TF_elecOptChanged = 1;
         }

         /* Adjust Precomp cutback */
         /* Default Precomp cutback for both RevA and RevB is 29 % */
         if (precomp != SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DEP_precomp)
         {
            SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DEP_precomp =  
               (SCSI_UEXACT8) precomp;
   
            hwInformation.targetInfo[i].SCSI_TF_elecOptChanged = 1;
         }

      }
#endif /* SCSI_ELEC_PROFILE */

   /* indicate that this is not a target only adjustment */
   hwInformation.SCSI_PF_adjustTargetOnly = 0;
   SCSI_PUT_HW_INFORMATION(&hwInformation, hhcb);

   return((HIM_UINT8)(HIM_SUCCESS));
#else
   return((HIM_UINT8)(HIM_FAILURE));

#endif /* SCSI_PROFILE_INFORMATION */
}

/*********************************************************************
*
*  SCSIProfileTarget
*
*     Get target profile information
*
*  Return Value:  HIM_SUCCESS, normal return
*                 HIM_FAILURE, operation failure
*                  
*  Parameters:    target TSH
*                 profile memory
*
*  Remarks:       Some of the profile information are transport
*                 specific and OSM should either avoid or isolate
*                 accessing profile information if possible.
*                 Pretty much CHIM will have to maintain the
*                 profile information itself.
*                 
*********************************************************************/
HIM_UINT8 SCSIProfileTarget (HIM_TASK_SET_HANDLE targetTSH,
                             HIM_TARGET_PROFILE HIM_PTR profile)
{
#if (SCSI_PROFILE_INFORMATION && SCSI_INITIATOR_OPERATION)
#if SCSI_BIOS_ASPI8DOS
   SCSI_UINT16 i;
#endif /* SCSI_BIOS_ASPI8DOS */
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH =
               ((SCSI_TARGET_TSCB HIM_PTR)targetTSH)->adapterTSH;
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb;
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8)SCSI_TRGTSH(targetTSH)->scsiID;
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[scsiID];
   SCSI_HW_INFORMATION hwInformation;

   if (!hhcb->SCSI_HF_initiatorMode)
   {
      return((HIM_UINT8)(HIM_FAILURE));
   }

   SCSI_GET_HW_INFORMATION(&hwInformation, hhcb);
   
   /* copy the default target profile */
   OSDmemcpy(profile,&SCSIDefaultTargetProfile,sizeof(HIM_TARGET_PROFILE));
   
   /* update fields which are adapter/target specific */
                                
   /* Get the scan order from the bios information. Only if BIOS is active */
   profile->TP_ScanOrder = 0xffff;
#if SCSI_BIOS_ASPI8DOS
   if (SCSI_ADPTSH(adapterTSH)->biosInformation.biosFlags.biosActive)
   {
      for (i = 0; i<SCSI_ADPTSH(adapterTSH)->biosInformation.numberDrives; i++)
      {
         if (SCSI_ADPTSH(adapterTSH)->biosInformation.biosDrive[i].targetID == scsiID)
         {
            profile->TP_ScanOrder = i + 
                  (HIM_UINT16) SCSI_ADPTSH(adapterTSH)->biosInformation.firstBiosDrive;
         }
      }
   }
#endif /* SCSI_BIOS_ASPI8DOS */
   
   /* MaxActiveCommands calculated from hwInformation structure which gets */
   /* the parameter from the unit control structure internally. The field */
   /* used is maxTagScbs since it is not possible to know tagged or */
   /* nontagged since tagged is set on an IOB basis. */
   profile->TP_MaxActiveCommands = hwInformation.targetInfo[scsiID].maxTagScbs;

   profile->TP_TaggedQueuing = 
            (SCSI_TRGTSH(targetTSH)->targetAttributes.tagEnable) ?
             HIM_TRUE : HIM_FALSE;

   profile->TP_HostManaged =
            (deviceTable->SCSI_DF_hostManaged) ? HIM_TRUE : HIM_FALSE;

   profile->himu.TS_SCSI.TP_SCSI_ID = scsiID;
   profile->himu.TS_SCSI.TP_SCSILun = SCSI_TRGTSH(targetTSH)->lunID;

   profile->himu.TS_SCSI.TP_SCSIMaxSpeed = hwInformation.targetInfo[scsiID].SCSIMaxSpeed;
   profile->himu.TS_SCSI.TP_SCSIDefaultSpeed = hwInformation.targetInfo[scsiID].SCSIDefaultSpeed;
   profile->himu.TS_SCSI.TP_SCSICurrentSpeed = hwInformation.targetInfo[scsiID].SCSICurrentSpeed;
   profile->himu.TS_SCSI.TP_SCSIDefaultOffset = hwInformation.targetInfo[scsiID].SCSIDefaultOffset;
   profile->himu.TS_SCSI.TP_SCSICurrentOffset = hwInformation.targetInfo[scsiID].SCSICurrentOffset;
   profile->himu.TS_SCSI.TP_SCSIMaxOffset = hwInformation.targetInfo[scsiID].SCSIMaxOffset;
   profile->himu.TS_SCSI.TP_SCSIMaxWidth = hwInformation.targetInfo[scsiID].SCSIMaxWidth;
   profile->himu.TS_SCSI.TP_SCSIDefaultWidth = hwInformation.targetInfo[scsiID].SCSIDefaultWidth;
   profile->himu.TS_SCSI.TP_SCSICurrentWidth = hwInformation.targetInfo[scsiID].SCSICurrentWidth;
         
#if SCSI_PPR_ENABLE
   /* Retrieve device TransitionClocking capability */
   /* Need to add logic for DT & ST support */
   if (deviceTable->xferRate[SCSI_TARGET_CAP_ENTRY][SCSI_XFER_PTCL_OPT] & SCSI_DUALEDGE)
   {
      profile->himu.TS_SCSI.TP_SCSITransitionClocking = HIM_SCSI_DT_CLOCKING;  /* DT only */
   }
   else
   {
      profile->himu.TS_SCSI.TP_SCSITransitionClocking = HIM_SCSI_ST_CLOCKING;  /* ST only */
   }

   if (deviceTable->xferRate[SCSI_TARGET_CAP_ENTRY][SCSI_XFER_PTCL_OPT] &
       SCSI_QUICKARB)
   {
      profile->himu.TS_SCSI.TP_SCSIQASSupport = HIM_TRUE;
   }

#if SCSI_PACKETIZED_IO_SUPPORT

   if (deviceTable->xferRate[SCSI_TARGET_CAP_ENTRY][SCSI_XFER_PTCL_OPT] &
       SCSI_PACKETIZED)
   {
      profile->himu.TS_SCSI.TP_SCSIIUSupport = HIM_TRUE;
   }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#else
   profile->himu.TS_SCSI.TP_SCSITransitionClocking = HIM_SCSI_ST_CLOCKING;  /* ST only */
#endif /* SCSI_PPR_ENABLE */ 

   profile->himu.TS_SCSI.TP_SCSIDefaultProtocolOption =
      hwInformation.targetInfo[scsiID].SCSIDefaultProtocolOption;


   profile->himu.TS_SCSI.TP_SCSICurrentProtocolOption =
      hwInformation.targetInfo[scsiID].SCSICurrentProtocolOption;

   profile->himu.TS_SCSI.TP_SCSIDefaultProtocolOptionMask =
      hwInformation.targetInfo[scsiID].SCSIDefaultProtocolOptionMask;

   profile->himu.TS_SCSI.TP_SCSICurrentProtocolOptionMask =
      hwInformation.targetInfo[scsiID].SCSICurrentProtocolOptionMask;
      
   profile->himu.TS_SCSI.TP_SCSIMaxProtocolOptionMask =
      hwInformation.targetInfo[scsiID].SCSIMaxProtocolOptionMask;
      
   profile->himu.TS_SCSI.TP_SCSIProtocolOptionMaskEnable =
      (hwInformation.targetInfo[scsiID].SCSI_TF_ptclOptMaskEnable) ?
             HIM_TRUE : HIM_FALSE;
             
   profile->himu.TS_SCSI.TP_SCSIAdapterPreCompEnabled =
      (hwInformation.targetInfo[scsiID].SCSI_TF_adapterPreCompEn) ?
             HIM_TRUE : HIM_FALSE;
      
#if SCSI_ELEC_PROFILE
   profile->himu.TS_SCSI.TP_SCSIMaxSlewRate =
      hwInformation.targetInfo[scsiID].SCSIMaxSlewRate;
   profile->himu.TS_SCSI.TP_SCSISlewRate =
      (SCSI_UEXACT8) deviceTable->SCSI_DEP_slewrate;
   profile->himu.TS_SCSI.TP_SCSIMaxAmplitudeLevel =
      hwInformation.targetInfo[scsiID].SCSIMaxAmplitudeLevel;
   profile->himu.TS_SCSI.TP_SCSIAmplitudeLevel =
      (SCSI_UEXACT8) deviceTable->SCSI_DEP_amplitude;

   /* Default to Precomp Off */
   profile->himu.TS_SCSI.TP_SCSIPrecompLevel = SCSI_PCOMP_OFF;

   /* Qualify Precomp Level */
#if !SCSI_FORCE_PRECOMP
   if (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_PCOMP_EN)
#endif /* SCSI_FORCE_PRECOMP */
   {  
      if (deviceTable->SCSI_DEP_precomp == 
         ((SCSI_PCOMP0 | SCSI_PCOMP1 | SCSI_PCOMP2)))
      {
         profile->himu.TS_SCSI.TP_SCSIPrecompLevel = SCSI_PCOMP_37;
      }
      else if (deviceTable->SCSI_DEP_precomp == 
         ((SCSI_PCOMP1 | SCSI_PCOMP2)))
      {
         profile->himu.TS_SCSI.TP_SCSIPrecompLevel = SCSI_PCOMP_29;
      }
      else if (deviceTable->SCSI_DEP_precomp == 
         ((SCSI_PCOMP2)))
      {
         profile->himu.TS_SCSI.TP_SCSIPrecompLevel = SCSI_PCOMP_17;
      }
   }  
#endif /* SCSI_ELEC_PROFILE */

   /* double check if currently needs nego then return unknown */
   if (deviceTable->SCSI_DF_needNego)
   {
      profile->himu.TS_SCSI.TP_SCSICurrentSpeed  = HIM_SCSI_SPEED_UNKNOWN;
      profile->himu.TS_SCSI.TP_SCSICurrentOffset = HIM_SCSI_OFFSET_UNKNOWN;
      profile->himu.TS_SCSI.TP_SCSICurrentWidth  = HIM_SCSI_WIDTH_UNKNOWN;
      profile->himu.TS_SCSI.TP_SCSICurrentProtocolOption = 
                     (HIM_SCSI_PROTOCOL_OPTION_UNKNOWN & 0xFF);      
      profile->himu.TS_SCSI.TP_SCSICurrentProtocolOptionMask =
                           HIM_SCSI_PROTOCOL_OPTION_UNKNOWN;      
   }
   profile->himu.TS_SCSI.TP_SCSIDisconnectAllowed =
            (deviceTable->SCSI_DF_disconnectEnable) ? HIM_TRUE : HIM_FALSE;

#if SCSI_DOMAIN_VALIDATION
   if (adapterTSH->SCSI_AF_dvFallBack(scsiID))
   {
      profile->himu.TS_SCSI.TP_SCSIDomainValidationFallBack = HIM_TRUE;
   }
   else
   {
      profile->himu.TS_SCSI.TP_SCSIDomainValidationFallBack = HIM_FALSE;
   }
   profile->himu.TS_SCSI.TP_SCSIDomainValidationMethod = 
         adapterTSH->SCSI_AF_dvLevel(scsiID);
#else /* SCSI_DOMAIN_VALIDATION */
   profile->himu.TS_SCSI.TP_SCSIDomainValidationFallBack = HIM_FALSE;
   profile->himu.TS_SCSI.TP_SCSIDomainValidationMethod = SCSI_DV_DISABLE;
#endif /* SCSI_DOMAIN_VALIDATION */

   if (hhcb->SCSI_HF_expSupport)
   {
      profile->himu.TS_SCSI.TP_SCSIConnectedViaExpander = 
               (deviceTable->SCSI_DF_behindExp) ? HIM_TRUE : HIM_FALSE;
   }
   else
   {
      profile->himu.TS_SCSI.TP_SCSIConnectedViaExpander = HIM_FALSE;
   }

   return((HIM_UINT8)(HIM_SUCCESS));
#else
   return((HIM_UINT8)(HIM_FAILURE));

#endif /* (SCSI_PROFILE_INFORMATION && SCSI_INITIATOR_OPERATION) */   
}

/*********************************************************************
*
*  SCSIReportAdjustableTargetProfile
*
*     Get information about which target profile information are
*     adjustable
*
*  Return Value:  HIM_SUCCESS, normal return
*                 HIM_FAILURE, operation failure
*                  
*  Parameters:    target TSH
*                 profile adjust mask memory
*
*  Remarks:       
*                 
*********************************************************************/
HIM_UINT8 SCSIReportAdjustableTargetProfile (HIM_TASK_SET_HANDLE targetTSH,
                                    HIM_TARGET_PROFILE HIM_PTR profileMask)
{

#if (SCSI_PROFILE_INFORMATION && SCSI_INITIATOR_OPERATION && (!SCSI_DISABLE_ADJUST_TARGET_PROFILE))

   /* copy the adjustable target profile */
   OSDmemcpy(profileMask,&SCSIAdjustableTargetProfile,sizeof(HIM_TARGET_PROFILE));
   
   return((HIM_UINT8)(HIM_SUCCESS));
#else
   return((HIM_UINT8)(HIM_FAILURE));

#endif /* (SCSI_PROFILE_INFORMATION && SCSI_INITIATOR_OPERATION && (!SCSI_DISABLE_ADJUST_TARGET_PROFILE)) */
}


#if (SCSI_PROFILE_INFORMATION && SCSI_INITIATOR_OPERATION && (!SCSI_DISABLE_ADJUST_TARGET_PROFILE))
/*********************************************************************
*
*  SCSIxAdjustTransferOption
*
*     Validate requested change on default width, speed, offset
*     and protocol option. Automatically adjust to acceptable
*     values the same or less than the requested change.
*     HardwareInformation structure which was passed in by caller
*     will be updated by this routine.
*
*  Return Value:  none
*
*  Parameters:    targetTSH      - target task set handle
*                 profile        - target profile information
*                 phwInformation - ptr to current H/W information
*                                  This structure shall be pre-filled
*                                  by caller first. It will be updated
*                                  accordingly.
*
*  Remarks:       This is a helper routine for SCSIAdjustTargetProfile
*                 
*********************************************************************/
void SCSIxAdjustTransferOption (HIM_TASK_SET_HANDLE targetTSH,
                                HIM_TARGET_PROFILE HIM_PTR profile,
                                SCSI_HW_INFORMATION SCSI_LPTR phwInformation)
{
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH =
               ((SCSI_TARGET_TSCB HIM_PTR)targetTSH)->adapterTSH;
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb;
   SCSI_UEXACT8 scsiID    = (SCSI_UEXACT8)SCSI_TRGTSH(targetTSH)->scsiID;
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[scsiID];

   HIM_UINT8     scsiOptChanged = 0;

   SCSI_UEXACT8  currDefaultWidth, newDefaultWidth;
   SCSI_UEXACT8  currDefaultProtocol, newDefaultProtocol;
   SCSI_UEXACT16 currDefaultSpeed, newDefaultSpeed; 
   SCSI_UEXACT8  currDefaultOffset, newDefaultOffset;
   SCSI_UEXACT8  currMaxWidth;
   SCSI_UEXACT8  currDefaultProtocolMask, newDefaultProtocolMask;
   SCSI_UEXACT8  currDefaultptclOptMaskEnable;
   SCSI_UEXACT8  newDefaultptclOptMaskEnable;
#if SCSI_ELEC_PROFILE
   SCSI_UEXACT8  currDefaultSlew, newDefaultSlew;
   SCSI_UEXACT8  currDefaultPrecomp, newDefaultPrecomp;
   SCSI_UEXACT8  currDefaultAmplitude, newDefaultAmplitude;
   SCSI_UEXACT8  currMaxSlew, currMaxAmplitude;
   
   newDefaultSlew      = (SCSI_UEXACT8)profile->himu.TS_SCSI.TP_SCSISlewRate;
   newDefaultAmplitude = (SCSI_UEXACT8)profile->himu.TS_SCSI.TP_SCSIAmplitudeLevel;

   if (profile->himu.TS_SCSI.TP_SCSIPrecompLevel >= SCSI_PCOMP_37)
   {
      newDefaultPrecomp = (SCSI_UEXACT8)(SCSI_PCOMP2 | SCSI_PCOMP1 | SCSI_PCOMP0);
   }
   else if (profile->himu.TS_SCSI.TP_SCSIPrecompLevel >= SCSI_PCOMP_29)
   {
      newDefaultPrecomp = (SCSI_UEXACT8) (SCSI_PCOMP2 | SCSI_PCOMP1);
   }
   else if (profile->himu.TS_SCSI.TP_SCSIPrecompLevel >= SCSI_PCOMP_17)
   {
      newDefaultPrecomp = (SCSI_UEXACT8)(SCSI_PCOMP2);
   }
   else
   {
      newDefaultPrecomp =(SCSI_UEXACT8)0;
   }
   currDefaultSlew      =  (SCSI_UEXACT8) deviceTable->SCSI_DEP_slewrate;
   currDefaultAmplitude =  (SCSI_UEXACT8) deviceTable->SCSI_DEP_amplitude;
   currDefaultPrecomp   =  (SCSI_UEXACT8) deviceTable->SCSI_DEP_precomp;
   currMaxSlew          = phwInformation->targetInfo[scsiID].SCSIMaxSlewRate;
   currMaxAmplitude     = phwInformation->targetInfo[scsiID].SCSIMaxAmplitudeLevel;


   if ((newDefaultSlew != currDefaultSlew) && (newDefaultSlew <= currMaxSlew))
   {
      deviceTable->SCSI_DEP_slewrate = (SCSI_UEXACT8) newDefaultSlew;

      /* Let the hardware layer know OSM requests this */
      deviceTable->SCSI_DEP_userSlewRate = 1;

      scsiOptChanged = 1;
   }

   if ((newDefaultAmplitude != currDefaultAmplitude) && 
         (newDefaultAmplitude <= currMaxAmplitude))
   {
      deviceTable->SCSI_DEP_amplitude = (SCSI_UEXACT8) newDefaultAmplitude;

      scsiOptChanged = 1;
   }

   if (newDefaultPrecomp != currDefaultPrecomp)
   {
      deviceTable->SCSI_DEP_precomp = (SCSI_UEXACT8) newDefaultPrecomp;

      scsiOptChanged = 1;
   }
#endif /* SCSI_ELEC_PROFILE */

   /* Target Profile information */
   newDefaultWidth    = (SCSI_UEXACT8)profile->himu.TS_SCSI.TP_SCSIDefaultWidth;   
   newDefaultProtocol = (SCSI_UEXACT8)profile->himu.TS_SCSI.TP_SCSIDefaultProtocolOption; 
   newDefaultSpeed    = (SCSI_UEXACT16)profile->himu.TS_SCSI.TP_SCSIDefaultSpeed;
   newDefaultOffset   = (SCSI_UEXACT8)profile->himu.TS_SCSI.TP_SCSIDefaultOffset;
   newDefaultProtocolMask=(SCSI_UEXACT8)profile->himu.TS_SCSI.TP_SCSIDefaultProtocolOptionMask;
   newDefaultptclOptMaskEnable =
       (profile->himu.TS_SCSI.TP_SCSIProtocolOptionMaskEnable == HIM_TRUE) ? 1 : 0;           

   /* Get current default parameters */
   currDefaultWidth   = phwInformation->targetInfo[scsiID].SCSIDefaultWidth;
   currDefaultProtocol= phwInformation->targetInfo[scsiID].SCSIDefaultProtocolOption;
   currDefaultSpeed   = phwInformation->targetInfo[scsiID].SCSIDefaultSpeed;
   currDefaultOffset  = phwInformation->targetInfo[scsiID].SCSIDefaultOffset;
   currDefaultProtocolMask=
           (HIM_UEXACT8)phwInformation->targetInfo[scsiID].SCSIDefaultProtocolOptionMask;
   currDefaultptclOptMaskEnable=
           (HIM_UEXACT8)phwInformation->targetInfo[scsiID].SCSI_TF_ptclOptMaskEnable;

   /* Get current maximum width */
   currMaxWidth = phwInformation->targetInfo[scsiID].SCSIMaxWidth;

   if (newDefaultOffset == 0)
   {
      /*
       *  Based on SCSI specification, when SCSI offset is 0, new speed parameter
       *  is ignored. SCSI Transfer mode will be set to Async Mode.
       */
      newDefaultProtocol = SCSI_ST_DATA;
      newDefaultSpeed    = 0;
      newDefaultProtocolMask = 0;
   }
   
   if (!(SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE))
   {
      /*
       * For Narrow Device, defaultWidth is not changeable.
       */
      newDefaultWidth = 8;
   }
   
   if (newDefaultWidth < 16)
   {
      /* 
       * In narrow SCSI bus mode, only accept Single Transition transfer mode
       */
      newDefaultProtocol = SCSI_ST_DATA;
      newDefaultProtocolMask = 0;
      
      if (newDefaultSpeed >= 800)
      {  
         newDefaultSpeed = 400;  /* Speed >= 800 requires DT mode */
      }
   }
   
   if (newDefaultSpeed >= 1600 && (!newDefaultptclOptMaskEnable))
   {
      /*
       * Automatically set correct protocol option to achieve the 
       * requested speed.
       */
         switch (profile->himu.TS_SCSI.TP_SCSIDefaultProtocolOption)
         {
           case HIM_SCSI_DT_DATA_WITH_IU_AND_QAS:
              newDefaultProtocol = SCSI_DT_DATA_WITH_IU_AND_QAS;
              break;

           case HIM_SCSI_DT_DATA_WITH_IU:
              newDefaultProtocol = SCSI_DT_DATA_WITH_IU;
              break;

           case HIM_SCSI_DT_DATA_WITH_CRC_AND_QAS:
              newDefaultProtocol = SCSI_DT_DATA_WITH_CRC_AND_QAS;
              newDefaultSpeed = 800;
              break;

           default:
              newDefaultProtocol = SCSI_DT_DATA_WITH_CRC;
              newDefaultSpeed = 800;
              break;
         }
      
   }
   else
   if (newDefaultSpeed >= 800 && (!newDefaultptclOptMaskEnable))
   {
      /*
       * Automatically set correct protocol option to achieve the 
       * requested speed.
       */
      switch (profile->himu.TS_SCSI.TP_SCSIDefaultProtocolOption)
      {
        case HIM_SCSI_DT_DATA_WITH_IU_AND_QAS:
           newDefaultProtocol = SCSI_DT_DATA_WITH_IU_AND_QAS;
           break;

        case HIM_SCSI_DT_DATA_WITH_CRC_AND_QAS:
           newDefaultProtocol = SCSI_DT_DATA_WITH_CRC_AND_QAS;
           break;
           
        case HIM_SCSI_DT_DATA_WITH_IU:
           newDefaultProtocol = SCSI_DT_DATA_WITH_IU;
           break;

        default:
           newDefaultProtocol = SCSI_DT_DATA_WITH_CRC;
           break;
      }
   }
   else
   {
      if (newDefaultSpeed < 100)
      {
         /*
          * Some speeds are both supported in DT or ST mode.
          * Speeds < 100 are only supported in ST mode.
          */
         newDefaultProtocol = SCSI_ST_DATA;
         newDefaultProtocolMask = 0;
      }
   }

   if (newDefaultSpeed < phwInformation->minimumSyncSpeed)
   {
      /* Set to Async mode */
      newDefaultOffset = 0;
      newDefaultSpeed  = 0;
   }
   if ((newDefaultProtocol != SCSI_ST_DATA) &&
       (newDefaultProtocol != SCSI_DT_DATA_WITH_CRC) &&
       (newDefaultProtocol != SCSI_DT_DATA_WITH_IU) &&
       (newDefaultProtocol != SCSI_DT_DATA_WITH_CRC_AND_QAS) &&
       (newDefaultProtocol != SCSI_DT_DATA_WITH_IU_AND_QAS))
   {
      /* set to ST mode if the newDefaultProtocol option is invalid */
      newDefaultProtocol=SCSI_ST_DATA;
   }

   /*
    *  At this point newDefaultWidth, newDefaultSpeed,
    *  newDefaultOffset, newDefaultProtocol have been validated.
    *  Now harwareInformation can be adjusted appropriately.
    */

   /* Change defaultWidth if necessary */
   /* Also check if the width OSM try to modify is greater than currMaxWidth */
   if ((newDefaultWidth != currDefaultWidth) && (newDefaultWidth <= currMaxWidth))
   {
      scsiOptChanged = 1;  /* Indicate force negotiation is needed */
   
      if (newDefaultWidth >= 16)
      {
         newDefaultWidth = 16;

         /* Set Wide bit */
         SCSI_TARGET_PROFILE_XFER(deviceTable)[SCSI_XFER_MISC_OPT] |= SCSI_WIDE;
      }
      else
      {
         newDefaultWidth = 8; /* Assume target is running in narrow mode */

         /* Clear Wide bit */
         SCSI_TARGET_PROFILE_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
      }
   }

   /* We should not allow changes to speed, offset or protocol option  */
   /* of a device if it does not support synchronous xfer.             */
   if (SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_OFFSET])
   {
      /* Do nothing if the speed, offset & protocol are the same */
      /* Do nothing for protocol mask change if mask changed but not enabled or*/
      /*  if encoded protocol change but mask enabled*/
      if ((newDefaultSpeed    != currDefaultSpeed)  ||
          (newDefaultOffset   != currDefaultOffset) ||
          (newDefaultptclOptMaskEnable!=currDefaultptclOptMaskEnable) ||
          ((newDefaultProtocolMask != currDefaultProtocolMask) && newDefaultptclOptMaskEnable) ||
          ((newDefaultProtocol != currDefaultProtocol)&& !newDefaultptclOptMaskEnable))
      {
         scsiOptChanged = 1;  /* Indicate force negotiation is needed */
         
         if (newDefaultptclOptMaskEnable)
         {
            deviceTable->SCSI_DF_ptlOptMaskEnable = 1;
         }
         else   
         {
            deviceTable->SCSI_DF_ptlOptMaskEnable = 0;
         }
         
         if (newDefaultOffset)
         {
            SCSI_TARGET_PROFILE_XFER(deviceTable)[SCSI_XFER_OFFSET] = newDefaultOffset;
         }
         else
         {
            /* Clear offset */
            SCSI_TARGET_PROFILE_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;

            /*
             * At this point actual value of newDefaultSpeed has no real 
             * meaning. However, for consistency we'll set the speed to 
             * minimum sync speed.
             */
            newDefaultSpeed = phwInformation->minimumSyncSpeed;
         }
      }
   }


   /* If the scsiOption has been changed then we should force negotiation. */
   if (scsiOptChanged)
   {
      /* Put the changed xfer options back into the hwInformation structure */
      /* and indicate the change to the hardware layer (using the           */
      /* scsiOptChanged flag).                                              */
      phwInformation->targetInfo[scsiID].SCSI_TF_scsiOptChanged = 1;
      phwInformation->targetInfo[scsiID].SCSIDefaultSpeed = newDefaultSpeed;
      phwInformation->targetInfo[scsiID].SCSIDefaultOffset= newDefaultOffset;
      phwInformation->targetInfo[scsiID].SCSIDefaultWidth = newDefaultWidth;
      phwInformation->targetInfo[scsiID].SCSIDefaultProtocolOption = newDefaultProtocol;
      phwInformation->targetInfo[scsiID].SCSIDefaultProtocolOptionMask = newDefaultProtocolMask;
      phwInformation->targetInfo[scsiID].SCSI_TF_ptclOptMaskEnable = newDefaultptclOptMaskEnable;
   }
}
#endif /* (SCSI_PROFILE_INFORMATION && SCSI_INITIATOR_OPERATION && (!SCSI_DISABLE_ADJUST_TARGET_PROFILE)) */

/*********************************************************************
*
*  SCSIAdjustTargetProfile
*
*     Apply the adjusted adapter profile information
*
*  Return Value:  HIM_SUCCESS, normal return
*                 HIM_FAILURE, operation failure
*                 HIM_ILLEGAL_CHANGE, illegal change
*                 HIM_TARGET_NOT_IDLE, profile adjusted is not allowed
*                  
*  Parameters:    target TSH
*                 profile memory
*
*  Remarks:       The profile mask should be acquired before
*                 adjusting target profile.
*                 
*********************************************************************/
HIM_UINT8 SCSIAdjustTargetProfile (HIM_TASK_SET_HANDLE targetTSH,
                                   HIM_TARGET_PROFILE HIM_PTR profile)    
                                    
{
#if (SCSI_PROFILE_INFORMATION && SCSI_INITIATOR_OPERATION && (!SCSI_DISABLE_ADJUST_TARGET_PROFILE))
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH =
               ((SCSI_TARGET_TSCB HIM_PTR)targetTSH)->adapterTSH;
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb;
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8)SCSI_TRGTSH(targetTSH)->scsiID;
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[scsiID];
   SCSI_HW_INFORMATION hwInformation;
   
   /* Return if initiator mode not enabled */
   if (!hhcb->SCSI_HF_initiatorMode)
   {
      return ((HIM_UINT8)(HIM_FAILURE));
   }  
 
   /* Return if the device's not idle */
   if (SCSI_CHECK_DEVICE_IDLE(hhcb, scsiID) == SCSI_NOT_IDLE)
   {
      return((HIM_UINT8)(HIM_TARGET_NOT_IDLE));
   }

   SCSI_GET_HW_INFORMATION(&hwInformation, hhcb);

   SCSIxAdjustTransferOption(targetTSH, profile, &hwInformation);

#if SCSI_NULL_SELECTION
   if (hwInformation.hostScsiID != scsiID)
#endif /* SCSI_NULL_SELECTION */
   {
      /* If the target SCSI ID = the host adapter SCSI ID then this */
      /* must be a target TSCB for a "NULL selection device therefore */
      /* don't allow disconnect allowed to be set. */      
      deviceTable->SCSI_DF_disconnectEnable = 
            (profile->himu.TS_SCSI.TP_SCSIDisconnectAllowed == HIM_TRUE) ? 1 : 0;
   }

   /* The target's tagEnable flag is on / off will depend on the OSM set */
   /* TP_TaggedQueueing field to TRUE / FALSE (assume target supports tag). */
   if (profile->TP_TaggedQueuing == HIM_TRUE)
   {
      /* The target's tagEnable flag is further qualified with the */
      /* disconnectEnable flag is on / off and the target itself */
      /* supports tag or not. */
      SCSI_TRGTSH(targetTSH)->targetAttributes.tagEnable =
                  (((deviceTable->SCSI_DF_disconnectEnable << scsiID) &
                   SCSI_ADPTSH(adapterTSH)->tagEnable) != 0);
   }
   else
   {
      /* OSM request tag queueing is disable */
      SCSI_TRGTSH(targetTSH)->targetAttributes.tagEnable = 0;
   }
         
   /* MaxActiveCommands calculated from hwInformation structure which */
   /* gets the parameter from the unit control structure internally. */
   /* The field used is maxTagScbs since it is not possible to know */
   /* tagged or nontagged since tagged is set on an IOB basis. */
   hwInformation.targetInfo[scsiID].maxTagScbs = profile->TP_MaxActiveCommands;

   /* indicate that this is a target only adjustment */
   hwInformation.SCSI_PF_adjustTargetOnly = 1;


   SCSI_PUT_HW_INFORMATION(&hwInformation, hhcb);

   return((HIM_UINT8)(HIM_SUCCESS));
#else

   return((HIM_UINT8)(HIM_FAILURE));

#endif /* (SCSI_PROFILE_INFORMATION && SCSI_INITIATOR_OPERATION && (!SCSI_DISABLE_ADJUST_TARGET_PROFILE) */
}

/*********************************************************************
*
*  SCSIGetNVSize
*
*     Get size of NVRAM associated with the specified adapter TSH
*
*  Return Value:  size of NVRAM associated
*                  
*  Parameters:    adapter TSH
*
*  Remarks:
*                 
*********************************************************************/
HIM_UINT32 SCSIGetNVSize (HIM_TASK_SET_HANDLE adapterTSH)
{
   SCSI_UINT32 size = 0;
#if SCSI_SEEPROM_ACCESS
   SCSI_UINT32 seepromSize;
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb;
#endif /* SCSI_SEEPROM_ACCESS */
#if !SCSI_SEEPROM_ACCESS
      size = OSMxGetNVSize(SCSI_ADPTSH(adapterTSH)->osmAdapterContext);
#else

   if (!hhcb->SCSI_HF_OverrideOSMNVRAMRoutines)

      size = OSMxGetNVSize(SCSI_ADPTSH(adapterTSH)->osmAdapterContext);

   if (size == 0)
   {
      /* OSM NVM not available */
      /* read from SEEPROM attached to host device */
      
      /* seepromSize is in word */
      seepromSize = SCSI_SIZE_SEEPROM(hhcb);

      if (!hhcb->SCSI_HF_multiFunction)
      {
         /* SCSI_ONE_CHNL: */
         size = (seepromSize * 2);
         return (size);
      }
      else
      {
         /* SCSI_TWO_CHNL: */
         size = (seepromSize * 4);
         return (size);
      }
   }

   if ((size == 0) && (hhcb->SCSI_HF_OverrideOSMNVRAMRoutines))
      size = OSMxGetNVSize(SCSI_ADPTSH(adapterTSH)->osmAdapterContext);
   
#endif /* SCSI_SEEPROM_ACCESS */

   return(size);
   
}

/*********************************************************************
*
*  SCSIGetNVOSMSegment
*
*     Get segment information for the NVRAM controlled by OSM. 
*
*  Return Value:  HIM_SUCCESS, normal completion
*                 HIM_FAILURE, hardware or other failure
*                 HIM_NO_OSM_SEGMENT, no OSM segment available
*                  
*  Parameters:    adapter TSH
*                 osmOffset
*                 osmCount
*
*  Remarks:
*                 
*********************************************************************/
HIM_UINT8 SCSIGetNVOSMSegment (HIM_TASK_SET_HANDLE adapterTSH, 
                               HIM_UINT32 HIM_PTR osmOffset, 
                               HIM_UINT32 HIM_PTR osmCount)
{
#if SCSI_SEEPROM_ACCESS
   SCSI_UINT32 seepromSize;
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb;
   SCSI_NVM_LAYOUT nvmData;
   SCSI_UINT16 seepromBase;

   /* OSM NVM not available */
   /* read from SEEPROM attached to host device */

   /* figure out the section base within the seeprom */
   seepromBase = 0;

   if (hhcb->SCSI_HF_multiFunction)
   {
       /* SCSI_TWO_CHNL */
       if (SCSI_ADPTSH(adapterTSH)->hostAddress.pciAddress.functionNumber != 0)
       {
          seepromBase = sizeof(SCSI_NVM_LAYOUT) / 2;
       }
   }
   else
   {
       if (SCSI_ADPTSH(adapterTSH)->hostAddress.pciAddress.deviceNumber != 4)
       {
          seepromBase = sizeof(SCSI_NVM_LAYOUT) / 2;
       }
   }

   /* read from seeprom */
   if (SCSI_READ_SEEPROM(hhcb,(SCSI_UEXACT8 SCSI_SPTR) &nvmData,
       (SCSI_SINT16) seepromBase,(SCSI_SINT16) sizeof(SCSI_NVM_LAYOUT) / 2))
   {
      /* seeprom not available */
      return((HIM_UINT8)(HIM_NO_OSM_SEGMENT)); 
   }

   /* seepromSize is in word */
   seepromSize = SCSI_SIZE_SEEPROM(hhcb);

#if SCSI_SCSISELECT_SUPPORT
   if (!hhcb->SCSI_HF_multiFunction)
   {
      /* SCSI_ONE_CHNL */
      *osmOffset = sizeof(SCSI_NVM_LAYOUT);
      *osmCount = (seepromSize * 2) / 2;
      return((HIM_UINT8)(HIM_SUCCESS));
   }
   else
   {
      /* SCSI_TWO_CHNL */
      *osmOffset = 0;
      *osmCount = 0;
      return((HIM_UINT8)(HIM_NO_OSM_SEGMENT));
   }

#else /* !SCSI_SCSISELECT_SUPPORT */
   if (!hhcb->SCSI_HF_multiFunction)
   {
      /* SCSI_ONE_CHNL */
      *osmOffset = (seepromBase * 2);
      *osmCount = (seepromSize * 2);
      return((HIM_UINT8)(HIM_SUCCESS));
   }
   else
   {
      /* SCSI_TWO_CHNL */
      *osmOffset = (seepromBase * 2);
      *osmCount = (seepromSize * 2) / 2;
      return((HIM_UINT8)(HIM_SUCCESS));
   }

#endif /* SCSI_SCSISELECT_SUPPORT */

#else /* !SCSI_SEEPROM_ACCESS */
   
   return((HIM_UINT8)(HIM_NO_OSM_SEGMENT));
#endif /* SCSI_SEEPROM_ACCESS */
}

/*********************************************************************
*
*  SCSIPutNVData
*
*     Write to NVRAM
*
*  Return Value:  HIM_SUCCESS, write successfully
*                 HIM_FAILURE, write failed
*                 HIM_WRITE_NOT_SUPPORTED, write to NVRAM not supported
*                 HIM_WRITE_PROTECTED, NVRAM is write-protected
*                  
*  Parameters:    adapter TSH
*                 destination NVRAM offset
*                 source of memory buffer
*                 length to be written to NVRAM
*
*  Remarks:       
*                 
*********************************************************************/
HIM_UINT8 SCSIPutNVData (HIM_TASK_SET_HANDLE adapterTSH,
                         HIM_UINT32 destinationOffset,
                         void HIM_PTR source,
                         HIM_UINT32 length) 
{
   HIM_UINT32 OSMReturn;
#if SCSI_SEEPROM_ACCESS
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb;
#endif /* SCSI_SEEPROM_ACCESS */

#if !SCSI_SEEPROM_ACCESS
   
      OSMReturn = OSMxPutNVData(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                                destinationOffset,source,length);
      return((HIM_UINT8) OSMReturn);

#else  /* SCSI_SEEPROM_ACCESS */

   if (!hhcb->SCSI_HF_OverrideOSMNVRAMRoutines)
   {
      OSMReturn = OSMxPutNVData(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                                destinationOffset,source,length);

      if (OSMReturn != HIM_WRITE_NOT_SUPPORTED)
      {
         return((HIM_UINT8) OSMReturn);
      }
   }
   
   if (SCSI_WRITE_SEEPROM(hhcb,(SCSI_SINT16) (destinationOffset/2),
       (SCSI_SINT16) (length/2),(SCSI_UEXACT8 SCSI_SPTR) source) == 0)
   {
      return((HIM_UINT8)(HIM_SUCCESS)); 
   }
   else
   {
      if (!hhcb->SCSI_HF_OverrideOSMNVRAMRoutines)
      {
         return((HIM_UINT8)(HIM_FAILURE));
      }
      else
      {
         OSMReturn = OSMxPutNVData(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                                   destinationOffset,source,length);
         return((HIM_UINT8) OSMReturn);
      }
   }

#endif /* !SCSI_SEEPROM_ACCESS */

}

/*********************************************************************
*
*  SCSIGetNVData
*
*     Retrieve data from NVRAM
*
*  Return Value:  HIM_SUCCESS, NVRAM retrieval succussful
*                 HIM_FAILURE, NVRAM retrieval failed
*                  
*  Parameters:    adapter TSH
*                 destination buffer
*                 source offset of NVRAM
*                 length to be read from NVRAM
*
*  Remarks:       
*                 
*********************************************************************/
HIM_UINT8 SCSIGetNVData (HIM_TASK_SET_HANDLE adapterTSH,
                         void HIM_PTR destination,
                         HIM_UINT32 sourceOffset,
                         HIM_UINT32 length)
{
   HIM_UINT32 OSMReturn;
#if SCSI_SEEPROM_ACCESS
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb;
#endif /* SCSI_SEEPROM_ACCESS */

#if !SCSI_SEEPROM_ACCESS

   OSMReturn = OSMxGetNVData(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                             destination,sourceOffset,length);
   return((HIM_UINT8) OSMReturn); 

#else

   if (!hhcb->SCSI_HF_OverrideOSMNVRAMRoutines)
   {
      OSMReturn = OSMxGetNVData(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                                destination,sourceOffset,length);

      if (OSMReturn == HIM_SUCCESS)
      {
         return((HIM_UINT8)(HIM_SUCCESS)); 
      }
   }

   if (SCSI_READ_SEEPROM(hhcb,(SCSI_UEXACT8 SCSI_SPTR) destination,
         (SCSI_SINT16) (sourceOffset/2),(SCSI_SINT16) (length/2)) == 0)
   {
      return((HIM_UINT8)(HIM_SUCCESS)); 
   }
   else
   {
   
      if (!hhcb->SCSI_HF_OverrideOSMNVRAMRoutines)
         return((HIM_UINT8)(HIM_FAILURE));
      else
      {
         OSMReturn = OSMxGetNVData(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                                   destination,sourceOffset,length);
         return((HIM_UINT8)OSMReturn); 
      }
   }

#endif /* !SCSI_SEEPROM_ACCESS */
}

/*********************************************************************
*
*  SCSIProfileNode
*
*     Get node profile information
*
*  Return Value:  HIM_SUCCESS, normal return
*                 HIM_FAILURE, target mode was not enabled
*                  
*  Parameters:    node TSH
*                 profile memory
*
*  Remarks:       Some of the profile information are transport
*                 specific and OSM should either avoid or isolate
*                 accessing profile information if possible.
*                 Pretty much CHIM will have to maintain the
*                 profile information itself.
*                 
*********************************************************************/
HIM_UINT8 SCSIProfileNode (HIM_TASK_SET_HANDLE nodeTSH,
                           HIM_NODE_PROFILE HIM_PTR profile)         
{
#if (SCSI_TARGET_OPERATION && SCSI_PROFILE_INFORMATION)
   SCSI_NODE SCSI_NPTR node = SCSI_NODETSH(nodeTSH);
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR)node->hhcb;
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8)node->scsiID;
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[scsiID];
   SCSI_HW_INFORMATION hwInformation;

   /* Check target mode enabled */
   if (!hhcb->SCSI_HF_targetMode)
   {
      return ((HIM_UINT8)(HIM_FAILURE));
   }
   profile->NP_Version = HIM_VERSION_NODE_PROFILE;
   profile->NP_Transport = HIM_TRANSPORT_SCSI;
   profile->NP_Protocol = HIM_PROTOCOL_SCSI;
   profile->NP_BusNumber = 0;
   
   profile->himu.TS_SCSI.NP_SCSI_ID = node->scsiID;
   
   /* Max speed etc is obtained from adapter information */

   SCSI_GET_HW_INFORMATION(&hwInformation, hhcb);
      
   profile->himu.TS_SCSI.NP_SCSIMaxSpeed =
      hwInformation.targetInfo[scsiID].SCSIMaxSpeed;
   profile->himu.TS_SCSI.NP_SCSIDefaultSpeed = 
      hwInformation.targetInfo[scsiID].SCSIDefaultSpeed;
   profile->himu.TS_SCSI.NP_SCSICurrentSpeed = 
      hwInformation.targetInfo[scsiID].SCSICurrentSpeed;
   
   profile->himu.TS_SCSI.NP_SCSIMaxOffset =
      hwInformation.targetInfo[scsiID].SCSIMaxOffset;
   profile->himu.TS_SCSI.NP_SCSIDefaultOffset = 
      hwInformation.targetInfo[scsiID].SCSIDefaultOffset;
   profile->himu.TS_SCSI.NP_SCSICurrentOffset = 
      hwInformation.targetInfo[scsiID].SCSICurrentOffset; 
   
   profile->himu.TS_SCSI.NP_SCSIMaxWidth = 
      hwInformation.targetInfo[scsiID].SCSIMaxWidth;
   profile->himu.TS_SCSI.NP_SCSIDefaultWidth =
      hwInformation.targetInfo[scsiID].SCSIDefaultWidth;
   profile->himu.TS_SCSI.NP_SCSICurrentWidth =
      hwInformation.targetInfo[scsiID].SCSICurrentWidth;
  
   profile->himu.TS_SCSI.NP_SCSIMaxProtocolOptionMask =
      (HIM_UINT32)hwInformation.targetInfo[scsiID].SCSIMaxProtocolOptionMask;
   profile->himu.TS_SCSI.NP_SCSIDefaultProtocolOptionMask =
      (HIM_UINT32)hwInformation.targetInfo[scsiID].SCSIDefaultProtocolOptionMask;
   profile->himu.TS_SCSI.NP_SCSICurrentProtocolOptionMask =
      (HIM_UINT32)hwInformation.targetInfo[scsiID].SCSICurrentProtocolOptionMask;

   profile->himu.TS_SCSI.NP_SCSIAdapterPreCompEnabled =
      (hwInformation.targetInfo[scsiID].SCSI_TF_adapterPreCompEn) ?
             HIM_TRUE : HIM_FALSE;

   /* double check current neg. rates if SCSI_NEEDNEGO then return unknown */
   /* should probably return narrow, async etc */
   if (deviceTable->SCSI_DF_needNego)
   {
      profile->himu.TS_SCSI.NP_SCSICurrentSpeed = 
         (HIM_UINT16)HIM_SCSI_SPEED_UNKNOWN;
      profile->himu.TS_SCSI.NP_SCSICurrentOffset = 
         (HIM_UINT8)HIM_SCSI_OFFSET_UNKNOWN;
      profile->himu.TS_SCSI.NP_SCSICurrentWidth =
         (HIM_UINT8)HIM_SCSI_WIDTH_UNKNOWN;
      profile->himu.TS_SCSI.NP_SCSICurrentProtocolOptionMask =
         (HIM_UINT32)HIM_SCSI_PROTOCOL_OPTION_UNKNOWN;
   }

   return((HIM_UINT8)(HIM_SUCCESS));
#else

   return ((HIM_UINT8)(HIM_FAILURE)); 
#endif /* (SCSI_TARGET_OPERATION && SCSI_PROFILE_INFORMATION) */
   
}

/*********************************************************************
*
*  SCSIReportAdjustableNodeProfile
*
*     Get information about which node profile information are
*     adjustable
*
*  Return Value:  HIM_SUCCESS, normal return
*                 HIM_FAILURE, target mode was not enabled
*                  
*  Parameters:    node TSH
*                 profile adjust mask memory
*
*  Remarks:       This is for target mode operation only
*                 
*********************************************************************/
HIM_UINT8 SCSIReportAdjustableNodeProfile (HIM_TASK_SET_HANDLE nodeTSH,
                                     HIM_NODE_PROFILE HIM_PTR profileMask)
{
#if (SCSI_TARGET_OPERATION && SCSI_PROFILE_INFORMATION)
   /* copy the adjustable node profile */
   OSDmemcpy(profileMask,&SCSIAdjustableNodeProfile,sizeof(HIM_NODE_PROFILE));
  
   return((HIM_UINT8)(HIM_SUCCESS));
#else
   return((HIM_UINT8)(HIM_FAILURE));
#endif /* (SCSI_TARGET_OPERATION && SCSI_PROFILE_OPERATION) */
}

/*********************************************************************
*
*  SCSIAdjustNodeProfile
*
*     Apply the adjusted node profile information
*
*  Return Value:  HIM_SUCCESS, normal return
*                 HIM_FAILURE, operation failure
*                 HIM_ILLEGAL_CHANGE, illegal change
*                 HIM_NODE_NOT_IDLE, node TSH is not idle
*                  
*  Parameters:    node TSH
*                 profile memory
*
*  Remarks:       This is for target mode operation only
*                 
*********************************************************************/
HIM_UINT8 SCSIAdjustNodeProfile (HIM_TASK_SET_HANDLE nodeTSH,
                                 HIM_NODE_PROFILE HIM_PTR profile)

{
#if (SCSI_TARGET_OPERATION && SCSI_PROFILE_INFORMATION)   
    /* Currently none of the profile fields are adjustable */
   return((HIM_UINT8)(HIM_ILLEGAL_CHANGE));

#else
   return((HIM_UINT8)(HIM_FAILURE));   
#endif /* (SCSI_TARGET_OPERATION && SCSI_PROFILE_INFORMATION) */

}

/*********************************************************************
*
*  SCSISetOSMNodeContext
*
*     Set OSM context for a node TSH
*
*  Return Value:  HIM_SUCCESS, normal return
*                 HIM_FAILURE, target mode was not enabled
*                  
*  Parameters:    node TSH
*                 OSM context
*  Remarks:       The osmContext is the OSM's handle to the same node
*                 as nodeTSH. It typically points to an OSM structure 
*                 associated with this node. This osmContext handle is 
*                 passed to the OSM via the nexus profile. It is possible
*                 that OSM processing of requests is dependent on the state 
*                 of the node (initiator). 
*                 
*********************************************************************/
HIM_UINT8 SCSISetOSMNodeContext (HIM_TASK_SET_HANDLE nodeTSH,
                                 void HIM_PTR osmContext)    
                                    
{
#if SCSI_TARGET_OPERATION 

   SCSI_NODE SCSI_NPTR node = SCSI_NODETSH(nodeTSH);
   SCSI_HHCB SCSI_HPTR hhcb = node->hhcb;

   /* Check if target mode enabled */
   if (!hhcb->SCSI_HF_targetMode)
   {
      return ((HIM_UINT8)(HIM_FAILURE));
   }
   node->osmContext = osmContext;
   return((HIM_UINT8)(HIM_SUCCESS));
#else
   return((HIM_UINT8)(HIM_FAILURE));
#endif /* SCSI_TARGET_OPERATION */      
} 

/*********************************************************************
*
*  SCSIProfileNexus
*
*     Get nexus profile information
*
*  Return Value:  HIM_SUCCESS, normal return
*                 HIM_FAILURE, target mode is not enabled
*                  
*  Parameters:    nexus TSH
*                 profile memory
*
*  Remarks:       Some of the profile information are transport
*                 specific and OSM should either avoid or isolate
*                 accessing profile information if possible.
*                 Pretty much CHIM will have to maintain the
*                 profile information itself.
*                 
*********************************************************************/
HIM_UINT8 SCSIProfileNexus (HIM_TASK_SET_HANDLE nexusTSH,
                            HIM_NEXUS_PROFILE HIM_PTR profile)
{
#if SCSI_TARGET_OPERATION
   SCSI_NEXUS SCSI_XPTR nexus = SCSI_NEXUSTSH(nexusTSH); 
   SCSI_HHCB SCSI_HPTR hhcb = nexus->hhcb; 
#if !SCSI_ESS_SUPPORT
   SCSI_NODE SCSI_NPTR node = nexus->node;
#endif /* SCSI_ESS_SUPPORT */
   SCSI_UEXACT8 queueType;
   
   /* Check if target mode enabled */
   if (!hhcb->SCSI_HF_targetMode)
   {
      return ((HIM_UINT8)(HIM_FAILURE));
   }

#if !SCSI_ESS_SUPPORT
   /* ESS does not require these fields */
   profile->XP_Version = HIM_VERSION_NEXUS_PROFILE;
   profile->XP_Transport = HIM_TRANSPORT_SCSI;
   profile->XP_Protocol = HIM_PROTOCOL_SCSI;
   profile->XP_BusNumber = 0;
   profile->XP_NodeTSH = node;
   profile->XP_OSMNodeContext = node->osmContext;
#endif /* !SCSI_ESS_SUPPORT */

   profile->XP_AdapterTSH = SCSI_GETADP(hhcb);    
   profile->XP_LastResource = (nexus->SCSI_XF_lastResource) ? HIM_TRUE : HIM_FALSE;
   profile->himu.TS_SCSI.XP_SCSI_ID = nexus->scsiID;
   profile->himu.TS_SCSI.XP_SCSILun = nexus->lunID; 

   if (nexus->SCSI_XF_tagRequest)
   {
      /* Tagged queuing */
      queueType = nexus->queueType;

#if (SCSI_PACKETIZED_TARGET_MODE_SUPPORT && SCSI_PACKETIZED_IO_SUPPORT)
      if (!nexus->SCSI_XF_packetized)
#endif /* (SCSI_PACKETIZED_TARGET_MODE_SUPPORT && SCSI_PACKETIZED_IO_SUPPORT) */
      {
         /* Apply Queue Task Type Mask for legacy (non-packetized) requests */
         queueType  &= SCSI_QUEUE_TAG_MASK;
      }
      
      switch (queueType)   
      {
         case SCSI_SIMPLE_TASK_ATTRIBUTE:
            profile->himu.TS_SCSI.XP_SCSIQueueType = HIM_TASK_SIMPLE;
            break; 

         case SCSI_HEAD_OF_QUEUE_TASK_ATTRIBUTE:
            profile->himu.TS_SCSI.XP_SCSIQueueType = HIM_TASK_HEAD_OF_QUEUE;
            break;

         case SCSI_ORDERED_TASK_ATTRIBUTE:
            profile->himu.TS_SCSI.XP_SCSIQueueType = HIM_TASK_ORDERED;
            break;

         case SCSI_ACA_TASK_ATTRIBUTE:
            profile->himu.TS_SCSI.XP_SCSIQueueType = HIM_TASK_ACA;
            break;

         default:
            profile->himu.TS_SCSI.XP_SCSIQueueType = HIM_TASK_UNKNOWN;
            break;
      }
 
      profile->himu.TS_SCSI.XP_SCSIQueueTag = (HIM_UINT16)nexus->queueTag;
   }           
   else
   {/* No tag */
      profile->himu.TS_SCSI.XP_SCSIQueueType = HIM_TASK_NO_TAG;
   }

   profile->himu.TS_SCSI.XP_SCSILunTar = (nexus->SCSI_XF_lunTar) ? HIM_TRUE : HIM_FALSE;

#if !SCSI_ESS_SUPPORT
   profile->himu.TS_SCSI.XP_SCSIBusHeld = (nexus->SCSI_XF_busHeld) ? HIM_TRUE : HIM_FALSE;
   profile->himu.TS_SCSI.XP_SCSI1Selection = (nexus->SCSI_XF_scsi1Selection) ? HIM_TRUE : HIM_FALSE;   
   profile->himu.TS_SCSI.XP_SCSIDisconnectAllowed = (nexus->SCSI_XF_disconnectAllowed) ? HIM_TRUE : HIM_FALSE;
#endif /* ! SCSI_ESS_SUPPORT */

#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
   profile->himu.TS_SCSI.XP_SCSIIUSelection = (nexus->SCSI_XF_packetized) ? HIM_TRUE : HIM_FALSE;   
#else
   profile->himu.TS_SCSI.XP_SCSIIUSelection = HIM_FALSE;
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */

#if SCSI_MULTIPLEID_SUPPORT
   profile->himu.TS_SCSI.XP_SCSISelectedID = nexus->selectedID;  
#endif /* SCSI_MULTIPLEID_SUPPORT */

   return((HIM_UINT8)(HIM_SUCCESS)); 
#else
   return((HIM_UINT8)(HIM_FAILURE));
#endif /* SCSI_TARGET_OPERATION */   
}

/*********************************************************************
*
*  SCSIClearNexusTSH
*
*     Invalidate nexus TSH and return to available Nexus pool 
*
*  Return Value:  HIM_SUCCESS, nexus TSH is cleared and the associated TSCB 
*                 memory is returned to nexus available pool.
*
*                 HIM_NEXUS_IOBUS_HELD, nexus TSH cannot be cleared due to
*                 the I/O bus being held by this nexus. OSM must issue a
*                 HIM_CLEAR_NEXUS or HIM_REESTABLISH_INTERMEDIATE IOB function
*                 with no data to release the bus.  
*
*                 HIM_NEXUS_NOT_IDLE, nexus TSH cannot be cleared due to 
*                 pending IOBs for this nexus. OSM must issue a HIM_CLEAR_NEXUS
*                 IOB function or await completion of pending IOB(s).
*
*                 HIM_FAILURE, some undefined error occured or 
*                              target mode not enabled.  
*
*                 Other non-zero return status values may be 
*                 added later for future expansion.
*                  
*  Parameters:    nexus TSH
*
*  Remarks:       Whenever the OSM has finished with a particular nexus handle,
*                 it can invalidate the handle by calling this routine. After
*                 the handle is invalidated, the HIM returns the handle and
*                 it's storage space to the pool of available nexus handles.
*                 If the OSM calls this routine while the HIM is processing
*                 an IOB for this nexus OR if the I/O bus is held for this nexus,
*                 the call is rejected.
*
*                 It is not an error to invoke HIMClearNexusTSH for a nexus
*                 which has been returned to the available nexus pool, if and only if, 
*                 the call which release the nexus and subsequent calls are made
*                 within the same HIMBackEndISR context or while selection (IN)
*                 is disabled.
*
*********************************************************************/
HIM_UINT8 SCSIClearNexusTSH (HIM_TASK_SET_HANDLE nexusTSH)
{
#if SCSI_TARGET_OPERATION   
   SCSI_NEXUS HIM_PTR nexus = SCSI_NEXUSTSH(nexusTSH);
   SCSI_HHCB SCSI_XPTR hhcb = nexus->hhcb;
   HIM_UINT8 result;
   
   /* Check if target mode enabled */
   if (!hhcb->SCSI_HF_targetMode)
   {
      return ((HIM_UINT8)(HIM_FAILURE));
   } 

   result = (HIM_UINT8)SCSI_CLEAR_NEXUS(nexus,hhcb);
   if (!result)
   {
      return((HIM_UINT8)(HIM_SUCCESS));
   }
   else if (result == 1)
   {
      return((HIM_UINT8)(HIM_NEXUS_HOLDING_IOBUS));
   }
   else if (result == 2)
   {
      return((HIM_UINT8)(HIM_NEXUS_NOT_IDLE));     
   }
   else
   {         
      return ((HIM_UINT8)(HIM_FAILURE));
   }
   return((HIM_UINT8)(HIM_SUCCESS));
#else
   return((HIM_UINT8)(HIM_FAILURE));
#endif /* SCSI_TARGET_OPERATION */
}


/*********************************************************************
*
*  SCSIxIsInvalidRelatedIob
*
*     Check relatedIob fields of a given IOB for obvious 
*     invalid fields.
*
*  Return Value:  HIM_TRUE  - one or more relatedIob field is invalid
*                 HIM_FALSE - no obvious indication of invalid
*                             relatedIob.
*                  
*  Parameters:    IOB
*
*********************************************************************/
HIM_BOOLEAN SCSIxIsInvalidRelatedIob (HIM_IOB HIM_PTR iob)
{
   HIM_IOB HIM_PTR   relatedIob = iob->relatedIob;
   
   if (relatedIob == 0)
   {
      return(HIM_TRUE);
   }

   if (relatedIob->iobReserve.virtualAddress == 0) 
   {
      return(HIM_TRUE);
   }
      
   if (relatedIob->iobReserve.bufferSize == 0) 
   {
      return(HIM_TRUE);
   }
   
   if (relatedIob != SCSI_IOBRSV(relatedIob)->iob)
   {
      return(HIM_TRUE);
   }

#if SCSI_TARGET_OPERATION
   if ((relatedIob->function == HIM_REESTABLISH_AND_COMPLETE) ||
       (relatedIob->function == HIM_REESTABLISH_INTERMEDIATE))
   {
      /* Can't abort these IOB function types. */
      /* May want to add others to this. */ 
      return(HIM_TRUE);
   }   
#endif /* SCSI_TARGET_OPERATION */
   return(HIM_FALSE);
}


/*********************************************************************
*
*  SCSIQueueIOB
*
*     Queue the IOB for execution
*
*  Return Value:  void
*                  
*  Parameters:    adapter TSH
*
*  Remarks:       It's OSM's decision to execute this routine at
*                 interrupt context or not.
*
*********************************************************************/
#if SCSI_STREAMLINE_QIOPATH
void SCSIQueueIOB (HIM_IOB HIM_PTR iob)
{
   HIM_TASK_SET_HANDLE taskSetHandle = iob->taskSetHandle;
   SCSI_HIOB HIM_PTR hiob = &SCSI_IOBRSV(iob)->hiob;
   HIM_TS_SCSI HIM_PTR transportSpecific = (HIM_TS_SCSI HIM_PTR)iob->transportSpecific;
#if SCSI_TARGET_OPERATION 
   SCSI_UEXACT8 HIM_PTR scsiStatus;
#endif /* SCSI_TARGET_OPERATION */

/************************************************
   new locals start 
*************************************************/
   SCSI_HHCB SCSI_HPTR hhcb;  /* SCSIRQueueHIOB,SCSIHQueueHIOB,SCSIrAllocateScb */
                              /* SCSIrGetScb, SCSIDeliverScb */
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl; /* SCSIrCheckCriteria */
   SCSI_UINT16 maxScbs;  /* SCSIrCheckCriteria */
   register SCSI_REGISTER scsiRegister; /* SCSIHQueueHIOB,SCSIhDeliverScb */
#if SCSI_PACKETIZED_IO_SUPPORT
#if (SCSI_TASK_SWITCH_SUPPORT == 0)
   SCSI_UEXACT8 HIM_PTR uexact8Pointer = (SCSI_UEXACT8 HIM_PTR) iob->targetCommand;
#endif /* SCSI_TASK_SWITCH_SUPPORT == 0 */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

/************************************************
   new locals end 
*************************************************/

   /* setup pointer to iob for translation from hiob to iob */
   /* it will be referenced when building SCB and posting to OSM */
   SCSI_IOBRSV(iob)->iob = iob;
                               
   if (iob->function == HIM_INITIATE_TASK)
   {
      /* translate iob into hiob */
      hiob->unitHandle.targetUnit = &SCSI_TRGTSH(taskSetHandle)->unitControl;

      hiob->cmd = SCSI_CMD_INITIATE_TASK;
      hiob->SCSI_IF_noUnderrun = 0; 
      
#if SCSI_SELTO_PER_IOB      
      hiob->seltoPeriod = 0;
#endif /* SCSI_SELTO_PER_IOB */

      if (transportSpecific)
      {
         hiob->SCSI_IF_tagEnable = ((SCSI_TRGTSH(taskSetHandle)->targetAttributes.tagEnable) &&
                                    (transportSpecific->forceUntagged == HIM_FALSE) &&
                                    /*HQ
                              (transportSpecific->disallowDisconnect == HIM_FALSE) &&
                                     */
                             (iob->taskAttribute != HIM_TASK_RECOVERY));
         hiob->SCSI_IF_disallowDisconnect =
               (transportSpecific->disallowDisconnect == HIM_TRUE) ? 1 : 0;
#if SCSI_NEGOTIATION_PER_IOB
         hiob->SCSI_IF_forceSync =
               (transportSpecific->forceSync == HIM_TRUE) ? 1 : 0;
         hiob->SCSI_IF_forceAsync =
               (transportSpecific->forceAsync == HIM_TRUE) ? 1 : 0;
         hiob->SCSI_IF_forceWide =
               (transportSpecific->forceWide == HIM_TRUE) ? 1 : 0;
         hiob->SCSI_IF_forceNarrow =
               (transportSpecific->forceNarrow == HIM_TRUE) ? 1 : 0;
         hiob->SCSI_IF_forceReqSenseNego =
               (transportSpecific->forceReqSenseNego == HIM_TRUE) ? 1 : 0;
#endif /* SCSI_NEGOTIATION_PER_IOB */
#if SCSI_PARITY_PER_IOB
         hiob->SCSI_IF_parityEnable = 
               (transportSpecific->parityEnable == HIM_TRUE) ? 1 : 0;
#endif /* SCSI_PARITY_PER_IOB */

#if SCSI_SELTO_PER_IOB     
         hiob->seltoPeriod = transportSpecific->selectionTimeout;
#endif /* SCSI_SELTO_PER_IOB */                              

#if SCSI_DOMAIN_VALIDATION
         if (transportSpecific->dvIOB == HIM_TRUE)
         {
            if (transportSpecific ==
                ((void HIM_PTR) 
                 (((SCSI_UEXACT8 HIM_PTR) ((SCSI_TRGTSH(taskSetHandle)->adapterTSH)->moreUnlocked)) +
                   SCSI_MORE_TRANSPORT_SPECIFIC)))
            {
               /* This INITIATE_TASK IOB was built from HIM_PROBE or
                * HIM_PROTOCOL_AUTO_CONFIG. Therefore, set the
                * dvIOB flag.
                */
               hiob->SCSI_IF_dvIOB = 1;
            }
            else
            {
               hiob->SCSI_IF_dvIOB = 0;
            }
         }
         else
         {
            hiob->SCSI_IF_dvIOB = 0;
         }
#endif /* SCSI_DOMAIN_VALIDATION */
      }
      else
      {
         hiob->SCSI_IF_tagEnable = ((SCSI_TRGTSH(taskSetHandle)->targetAttributes.tagEnable) &&
                                    (iob->taskAttribute != HIM_TASK_RECOVERY));
         hiob->SCSI_IF_disallowDisconnect = 0;
#if SCSI_NEGOTIATION_PER_IOB
         hiob->SCSI_IF_forceSync = 0;
         hiob->SCSI_IF_forceAsync = 0;
         hiob->SCSI_IF_forceWide = 0;
         hiob->SCSI_IF_forceNarrow = 0;
         hiob->SCSI_IF_forceReqSenseNego = 0;
#endif /* SCSI_NEGOTIATION_PER_IOB */
#if SCSI_PARITY_PER_IOB
         hiob->SCSI_IF_parityEnable = 0;
#endif /* SCSI_PARITY_PER_IOB */

#if SCSI_DOMAIN_VALIDATION
         hiob->SCSI_IF_dvIOB = 0;
#endif /* SCSI_DOMAIN_VALIDATION */
      }

      hiob->SCSI_IF_autoSense = (SCSI_UEXACT16) iob->flagsIob.autoSense;
      hiob->SCSI_IF_freezeOnError = (SCSI_UEXACT16) iob->flagsIob.freezeOnError;
      hiob->priority = iob->priority;
      hiob->snsBuffer = iob->errorData;
#if SCSI_PACKETIZED_IO_SUPPORT
      hiob->SCSI_IF_tmfValid = 0;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

#if SCSI_RAID1
      hiob->SCSI_IF_raid1 = (SCSI_UEXACT16) iob->flagsIob.raid1;
#endif /* SCSI_RAID1 */

      if (iob->errorDataLength <= SCSI_HIOB_MAX_SENSE_LENGTH)
      {
         hiob->snsLength = (SCSI_UEXACT8) iob->errorDataLength;
      }
      else
      {
         hiob->snsLength = (SCSI_UEXACT8) SCSI_HIOB_MAX_SENSE_LENGTH;
      }
#if SCSI_PACKETIZED_IO_SUPPORT           
#if (SCSI_TASK_SWITCH_SUPPORT == 0)
      if ((uexact8Pointer[0] == 0x03) && 
           SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))   
      {
         OSDmemcpy ((SCSI_UEXACT8 HIM_PTR) hiob->snsBuffer,
                     (SCSI_UEXACT8 HIM_PTR) SCSI_TARGET_UNIT(hiob)->senseBuffer, 
                      hiob->snsLength );
                           
         /* process successful status */
         iob->taskStatus = HIM_IOB_GOOD;           
         iob->postRoutine(iob); 
         return;                         
      }            
#endif /* SCSI_TASK_SWITCH_SUPPORT == 0 */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

      /* send request to internal HIM */
      /* call to SCSI_QUEUE_HIOB(hiob); */
/************************************************
   SCSIRQueueHIOB() start 
*************************************************/
      hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;

      /* fresh the hiob status and working area */
      SCSI_rFRESH_HIOB(hiob);

      /* start with checking criteria (max tags/nontags) */
/************************************************
   SCSIrCheckCriteria() start 
*************************************************/
      /* call to SCSIrCheckCriteria(hhcb,hiob); */
      targetControl = SCSI_TARGET_UNIT(hiob)->targetControl;

      /* get the criteria factor */
      maxScbs = ((hiob->SCSI_IF_tagEnable) ? targetControl->maxTagScbs :
                                          targetControl->maxNonTagScbs);

#if SCSI_NEGOTIATION_PER_IOB
      /* If HIOB sets the flags to force negotiation, try to do the  */
      /* negotiation */
      if (hiob->SCSI_IF_forceSync || hiob->SCSI_IF_forceAsync ||
          hiob->SCSI_IF_forceWide || hiob->SCSI_IF_forceNarrow)
      {
         if (SCSI_CHECK_DEVICE_IDLE(hhcb, SCSI_TARGET_UNIT(hiob)->scsiID) ==
                                                               SCSI_NOT_IDLE)
         {
            maxScbs = 0;
         }
         else
         {
            SCSI_hCHANGEXFEROPTION(hiob);
         }
      }

      /* If a negotiation is pending, queue the incoming hiob into the    */
      /* device queue, so that the command associated with this hiob will */
      /* be executed after the negotiation. */
      if (targetControl->pendingNego)
      {
         maxScbs = 0;
      }
#endif /* SCSI_NEGOTIATION_PER_IOB */

#if SCSI_PACKETIZED_IO_SUPPORT

      /* Check for tagged command and auto-sense flag is enable and     */
      /* suppressNego flag is disable and either switch packetized      */
      /* progress flag is on or initially negotiate for packetized flag */
      /* is on then set flag for target to switch to packetized mode.   */
      /* Check only if not frozen (maxScbs >0)                          */
      if (hiob->SCSI_IF_tagEnable && maxScbs &&
          (!SCSI_TARGET_UNIT(hiob)->deviceTable->SCSI_DF_suppressNego) &&
          (SCSI_TARGET_UNIT(hiob)->deviceTable->SCSI_DF_switchPkt)
#if (SCSI_TASK_SWITCH_SUPPORT == 1)
          && hiob->SCSI_IF_autoSense
#endif /* SCSI_TASK_SWITCH_SUPPORT == 1 */         
         )
      {   
         if (!SCSIrSwitchToPacketized(hiob->unitHandle.targetUnit))
         {
            /* Freeze if not idle */
            SCSIrFreezeDeviceQueue(targetControl,SCSI_FREEZE_PACKSWITCH);
            maxScbs = 0;
         }                                                              
         else
         {
            /* Check to see if this iob is DV iob. If it's not,      */
            /* need to set negoXferIndex to SCSI_TARGET_CAP_ENTRY.   */
#if SCSI_DOMAIN_VALIDATION
            if (hiob->SCSI_IF_dvIOB == 0)
#endif /* SCSI_DOMAIN_VALIDATION */
            {
               SCSI_TARGET_UNIT(hiob)->deviceTable->negoXferIndex = SCSI_TARGET_CAP_ENTRY;
            }
         }
      }

      /* Check for target in packetized mode and non-tagged command or auto- */
      /* sense is disable, then set flag to switch to non-packetized mode.   */  
      if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable) && maxScbs &&
          (!hiob->SCSI_IF_tagEnable          
#if (SCSI_TASK_SWITCH_SUPPORT == 1)                    
          || !hiob->SCSI_IF_autoSense 
#endif /* SCSI_TASK_SWITCH_SUPPORT */          
          ))
      { 
         if (!SCSIrSwitchToNonPacketized (hiob->unitHandle.targetUnit))
         {
            /* Freeze if not idle */
            SCSIrFreezeDeviceQueue(targetControl,SCSI_FREEZE_PACKSWITCH);
            maxScbs = 0;
         }
      }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */ 
      if (++targetControl->activeScbs <= maxScbs)
      {
         /* criteria met, continue with allocating scb */
         /* call to SCSIrAllocateScb(hhcb,hiob); */
/************************************************
   SCSIrAllocateScb() start 
*************************************************/
/************************************************
   SCSIrGetScb() start 
*************************************************/
         SCSI_SET_NULL_SCB(hiob->scbNumber);
#if SCSI_TARGET_OPERATION
         if ((!hhcb->SCSI_HF_targetMode) || hhcb->SCSI_NON_EST_SCBS_AVAIL)
         {
            /* Attempt to obtain an SCB number. */ 
            /* Note; didn't want to indent code further. */   
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_PACKETIZED_IO_SUPPORT
         /* Allocate the SCB number from the packetized stack if it exists    */
         /* and the I/O is for packetized target. The packetized stack exists */
         /* only if the total number of SCBs allocated is more than 256.      */
         if ((hhcb->numberScbs > (SCSI_UEXACT16)256) &&
             SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
         {
            hiob->scbNumber =
               hhcb->SCSI_FREE_PACKETIZED_STACK[hhcb->SCSI_FREE_PACKETIZED_PTR];
            SCSI_SET_NULL_SCB(
               hhcb->SCSI_FREE_PACKETIZED_STACK[hhcb->SCSI_FREE_PACKETIZED_PTR]);
            hhcb->SCSI_FREE_PACKETIZED_PTR =
               (SCSI_UEXACT8)
               (((SCSI_UEXACT16)(++(hhcb->SCSI_FREE_PACKETIZED_PTR))) %
               (hhcb->numberScbs - (SCSI_UEXACT16)256));
         }


         /* If the packetized stack is empty or does not exist, or the I/O */
         /* is for non-packetized target, allocate the SCB number from the */
         /* non-packetized stack. */
         if (SCSI_IS_NULL_SCB(hiob->scbNumber))
         {
            hiob->scbNumber = 
               hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR];
            SCSI_SET_NULL_SCB(
               hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR]);
            hhcb->SCSI_FREE_NON_PACKETIZED_PTR = (SCSI_UEXACT8)
               (((SCSI_UEXACT16)(++(hhcb->SCSI_FREE_NON_PACKETIZED_PTR))) %
               hhcb->numberScbs);
         }
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
         /* Allocate the SCB number from the non-packetized stack. */
         hiob->scbNumber = 
            hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR];
         SCSI_SET_NULL_SCB(
            hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR]);
         hhcb->SCSI_FREE_NON_PACKETIZED_PTR = (SCSI_UEXACT8)
            (((SCSI_UEXACT16)(++(hhcb->SCSI_FREE_NON_PACKETIZED_PTR))) %
            hhcb->numberScbs);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_RAID1
         if (hiob->SCSI_IF_raid1)
         {
            hiob->SCSI_IF_raid1Primary = 1;
            hiob->mirrorHiob = ((SCSI_HIOB HIM_PTR) SCSI_IOBRSV(SCSI_GET_MIRROR_IOB(hiob)));
            /* mirrorHiob for the mirrored hiob points back to */
            /* primary hiob which is queued.                   */
            /* mirrorHiob is only used on completion for       */
            /* status, or in case of interrupt.                */
            hiob->mirrorHiob->mirrorHiob = hiob;

            /* The next two assignments can be moved back to   */
            /* SCSI_UPDATE_MIRROR_HIOB macro, only used        */
            /* in exception cases                              */
            hiob->mirrorHiob->snsBuffer = iob->relatedIob->errorData;
            hiob->mirrorHiob->snsLength = hiob->snsLength;          
            hiob->mirrorHiob->unitHandle.targetUnit  = 
               &SCSI_TRGTSH(iob->relatedIob->taskSetHandle)->unitControl;
            SCSI_SET_NULL_SCB(hiob->mirrorHiob->scbNumber);

#if SCSI_PACKETIZED_IO_SUPPORT
            if ((hhcb->numberScbs > (SCSI_UEXACT16)256) &&
                   SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob->mirrorHiob)->deviceTable))
            {
               hiob->mirrorHiob->scbNumber =
                  hhcb->SCSI_FREE_PACKETIZED_STACK[hhcb->SCSI_FREE_PACKETIZED_PTR];
               SCSI_SET_NULL_SCB(
                  hhcb->SCSI_FREE_PACKETIZED_STACK[hhcb->SCSI_FREE_PACKETIZED_PTR]);
               hhcb->SCSI_FREE_PACKETIZED_PTR =
                  (SCSI_UEXACT8)(((SCSI_UEXACT16)(++(hhcb->SCSI_FREE_PACKETIZED_PTR))) %
                  (hhcb->numberScbs - (SCSI_UEXACT16)256));
            }
            if (SCSI_IS_NULL_SCB(hiob->mirrorHiob->scbNumber))
            {
               hiob->mirrorHiob->scbNumber =
                  hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR];
               SCSI_SET_NULL_SCB(
                  hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR]);
               hhcb->SCSI_FREE_NON_PACKETIZED_PTR = (SCSI_UEXACT8)
                  (((SCSI_UEXACT16)(++(hhcb->SCSI_FREE_NON_PACKETIZED_PTR))) %
                  hhcb->numberScbs);
            }
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
            hiob->mirrorHiob->scbNumber = 
                hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR];
            SCSI_SET_NULL_SCB(
               hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR]);
            hhcb->SCSI_FREE_NON_PACKETIZED_PTR = (SCSI_UEXACT8)
                (((SCSI_UEXACT16)(++(hhcb->SCSI_FREE_NON_PACKETIZED_PTR))) %
                hhcb->numberScbs);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
         }
#endif /* SCSI_RAID1 */
#if SCSI_TARGET_OPERATION
         }
#endif /* SCSI_TARGET_OPERATION */

/************************************************
   SCSIrGetScb() end 
*************************************************/
         if (!SCSI_IS_NULL_SCB(hiob->scbNumber))
         {
            /* Decrement available SCB count if target mode.
             * Note that; this should occur within SCSIrGetScb 
             * context. However, more efficient to do it here.
             */
            SCSI_rDECREMENT_NON_EST_SCBS_AVAIL(hhcb);

#if !SCSI_SCBBFR_BUILTIN
            /* If successful in allocating an SCB number, get the next
             * SCB buffer from the free queue and attach the SCB number
             * to the buffer descriptor.
             */
            hiob->scbDescriptor = hhcb->SCSI_FREE_HEAD;
            hhcb->SCSI_FREE_HEAD = hhcb->SCSI_FREE_HEAD->queueNext;
            hiob->scbDescriptor->scbNumber = hiob->scbNumber;
#endif /* !SCSI_SCBBFR_BUILTIN */ 
            /* scb available, queue to hardware management layer */
            /* call SCSIHQueueHIOB(hiob); */
/************************************************
   SCSIHQueueHIOB() start 
*************************************************/
            scsiRegister = hhcb->scsiRegister;

            /* fresh the hiob status and working area */
            SCSI_hFRESH_HIOB(hiob);

            /* setup active pointer */
            SCSI_ACTPTR[hiob->scbNumber] = hiob;

#if SCSI_RAID1
            if (hiob->SCSI_IF_raid1)
            {    
               hiob->mirrorHiob->stat = 0;
               hiob->mirrorHiob->haStat = 0;
               hiob->mirrorHiob->trgStatus = 0;
               hiob->mirrorHiob->SCSI_IP_mgrStat = 0;
               hiob->mirrorHiob->SCSI_IP_negoState = 0;
               SCSI_ACTPTR[hiob->mirrorHiob->scbNumber] = hiob;
            }
#endif /* SCSI_RAID1 */

#if SCSI_SCBBFR_BUILTIN
           /* Obtain the SCB buffer descriptor from the free queue and
            * assign the SCB number to the buffer descriptor.
            */
            hiob->scbDescriptor = hhcb->SCSI_FREE_HEAD;
            hhcb->SCSI_FREE_HEAD = hhcb->SCSI_FREE_HEAD->queueNext;
            hiob->scbDescriptor->scbNumber = hiob->scbNumber;
#endif /* SCSI_SCBBFR_BUILTIN */

            /* deliver it to sequencer */
            /* call to SCSI_hDELIVERSCB(hhcb, hiob); */
/************************************************
   SCSIhDeliverScb() start 
*************************************************/
            /* setup scb buffer */
            OSD_BUILD_SCB(hiob);  /* mode dependent so can't inline */

            /* setup atn_length - negotiation needed or not */
            SCSI_hSETUPATNLENGTH(hhcb,hiob); /* mode dependent so can't inline */

            /* bump host new SCB queue offset */
            hhcb->SCSI_HP_hnscbQoff++;
            OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF),hhcb->SCSI_HP_hnscbQoff);

/************************************************
   SCSIhDeliverScb() end
*************************************************/

            OSD_SYNCHRONIZE_IOS(hhcb);
/************************************************
   SCSIHQueueHIOB() end 
*************************************************/
         }
         else
         {
            /* queue it to host queue */
            SCSIrHostQueueAppend(hhcb,hiob);
         }
/************************************************
   SCSIrAllocateScb() end
*************************************************/
      }
      else if ((targetControl->freezeMap) && (hiob->priority >= 0x80))
      {
#if SCSI_PACKETIZED_IO_SUPPORT           
#if (SCSI_TASK_SWITCH_SUPPORT == 2)
         if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable) && 
             ((SCSI_UEXACT8)OSDGetTargetCmd(hiob) == (SCSI_UEXACT8) 0x03)) 
         {         
            OSDmemcpy ((SCSI_UEXACT8 HIM_PTR) hiob->snsBuffer,
                      (SCSI_UEXACT8 HIM_PTR) SCSI_TARGET_UNIT(hiob)->senseBuffer, 
                        hiob->snsLength );
                           
            /* process successful status */
            hiob->stat = SCSI_SCB_COMP;            
            OSD_COMPLETE_HIOB(hiob);
            return;                         
         }                        
#endif /* SCSI_TASK_SWITCH_SUPPORT == 2 */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
     
         targetControl->activeHiPriScbs++;
         targetControl->freezeMap |= SCSI_FREEZE_HIPRIEXISTS;
         SCSIrAllocateScb(hhcb,hiob);
      }
      else
      {
         /* criteria not met, just append it to device queue */
         SCSIrDeviceQueueAppend(targetControl,hiob);
      }
/************************************************
   SCSIrCheckCriteria() end 
*************************************************/
/************************************************
   SCSIRQueueHIOB() end 
*************************************************/
   }
   else
   {

#if SCSI_RAID1
      /* These commands are not supported as */
      /* Mirror commands */
      hiob->SCSI_IF_raid1 =  0;
#endif /* SCSI_RAID1 */
      /* process other special iob */
      switch(iob->function)
      {
         case HIM_TERMINATE_TASK:
         case HIM_ABORT_TASK:
            if (SCSIxIsInvalidRelatedIob(iob) == HIM_TRUE)
            {
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
               return;
            }
            hiob->cmd = SCSI_CMD_ABORT_TASK;
            hiob->unitHandle.relatedHiob = &SCSI_IOBRSV(iob->relatedIob)->hiob;
            break;

         case HIM_ABORT_TASK_SET:
            hiob->cmd = SCSI_CMD_ABORT_TASK_SET;
            hiob->unitHandle.targetUnit = &SCSI_TRGTSH(taskSetHandle)->unitControl;
            break;

         case HIM_RESET_BUS_OR_TARGET:
            if (SCSI_TRGTSH(taskSetHandle)->typeTSCB == SCSI_TSCB_TYPE_TARGET)
            {
               /* reset target */
               hiob->cmd =  SCSI_CMD_RESET_TARGET;
               hiob->unitHandle.targetUnit = &SCSI_TRGTSH(taskSetHandle)->unitControl;
            }
            else if (SCSI_TRGTSH(taskSetHandle)->typeTSCB == SCSI_TSCB_TYPE_ADAPTER)
            {
               /* reset scsi bus */
               hiob->cmd = SCSI_CMD_RESET_BUS;
               hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
#if SCSI_DOMAIN_VALIDATION

               hhcb = &SCSI_ADPTSH(taskSetHandle)->hhcb;

               /* determine if data hang condition and also Adapter profile */
               /* AP_SCSIOSMResetBusUseThrottledDownSpeedforDV is enabled.  */
               SCSIxHandledvThrottleOSMReset(hhcb);

#endif /* SCSI_DOMAIN_VALIDATION */
            }
            else
            {
               /* illegal request */
               iob->taskStatus = HIM_IOB_TSH_INVALID;
               iob->postRoutine(iob);
               return ;
            }
            break;

         case HIM_RESET_HARDWARE:
            hiob->cmd = SCSI_CMD_RESET_HARDWARE;
            hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
            break;

         case HIM_PROTOCOL_AUTO_CONFIG:
            hiob->cmd = SCSI_CMD_PROTO_AUTO_CFG;
            hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
            break;

#if !SCSI_DISABLE_PROBE_SUPPORT
         case HIM_PROBE:
            if ((transportSpecific) &&
#if !SCSI_NULL_SELECTION
                (transportSpecific->scsiID != 
                 SCSI_ADPTSH(taskSetHandle)->hhcb.hostScsiID) &&

#endif /* !SCSI_NULL_SELECTION */
                !(transportSpecific->LUN[0]))
            {
               SCSI_ADPTSH(iob->taskSetHandle)->iobProtocolAutoConfig = iob;
               SCSIxSetupLunProbe(SCSI_ADPTSH(iob->taskSetHandle),
                  transportSpecific->scsiID,
                  transportSpecific->LUN[1]);
               SCSIxQueueBusScan(SCSI_ADPTSH(iob->taskSetHandle));
               SCSI_ADPTSH(iob->taskSetHandle)->lastScsiIDProbed = 
                  transportSpecific->scsiID;
               SCSI_ADPTSH(iob->taskSetHandle)->lastScsiLUNProbed = 
                  transportSpecific->LUN[1];
            }
            else
            {
               /* invalid probe */
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
            }
            return;
#endif /* !SCSI_DISABLE_PROBE_SUPPORT */

         case HIM_SUSPEND:
         case HIM_QUIESCE:
            if (SCSI_CHECK_HOST_IDLE(&SCSI_ADPTSH(taskSetHandle)->hhcb) == SCSI_IDLE)
            {
               /* host device is idle and nothing has to be done */
               iob->taskStatus = HIM_IOB_GOOD;
            }
            else
            {
               /* indicate host adapter is not idle */
               iob->taskStatus = HIM_IOB_ADAPTER_NOT_IDLE;
            }
#if SCSI_TARGET_OPERATION
            hhcb = &SCSI_ADPTSH(taskSetHandle)->hhcb;
            if (hhcb->SCSI_HF_targetMode)
            {
               /* Disable selection-in */
               SCSI_DISABLE_SELECTION_IN(hhcb);
               
               /* Need to check if the SCSI bus is held or */
               /* any pending selection-in interrupts      */
               if ((hhcb->SCSI_HF_targetScsiBusHeld) ||
                   (SCSI_CHECK_SELECTION_IN_INTERRUPT_PENDING(hhcb))) 
               {
                  /* indicate lost adapter is not idle */ 
                  iob->taskStatus = HIM_IOB_ADAPTER_NOT_IDLE;
               }
            }
#endif /* SCSI_TARGET_OPERATION */
            iob->postRoutine(iob);
            return ;

         case HIM_RESUME:
#if SCSI_TARGET_OPERATION
            /* Target Mode enabled then this function
             * enables selection-in
             */
            hhcb = &SCSI_ADPTSH(taskSetHandle)->hhcb;
            SCSI_hTARGETMODEENABLE(hhcb);
#endif /* SCSI_TARGET_OPERATION */            
            /* just return good status and nothing has to be done */
            iob->taskStatus = HIM_IOB_GOOD;
            iob->postRoutine(iob);
            return ;

         case HIM_CLEAR_XCA:
            /* not supported for this implementation */
            iob->taskStatus = HIM_IOB_UNSUPPORTED;
            iob->postRoutine(iob);
            return ;

         case HIM_UNFREEZE_QUEUE:
            /* unfreeze device queue */
            hiob->cmd = SCSI_CMD_UNFREEZE_QUEUE;
            hiob->unitHandle.targetUnit = &SCSI_TRGTSH(taskSetHandle)->unitControl;
            break;

#if SCSI_TARGET_OPERATION
         case HIM_ESTABLISH_CONNECTION:
#if SCSI_RESOURCE_MANAGEMENT
            hiob->cmd = SCSI_CMD_ESTABLISH_CONNECTION;
            hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
            /* snsBuffer contains the address of where the 
               received command or task management request 
               is to be stored. */
            hiob->snsBuffer = iob->targetCommand;
            hiob->snsBufferSize = (SCSI_UEXACT16) iob->targetCommandBufferSize;
            break;
#else
            iob->taskStatus = HIM_IOB_INVALID;
            iob->postRoutine(iob);
            return;
  
#endif /* SCSI_RESOURCE_MANAGEMENT */
         case HIM_REESTABLISH_AND_COMPLETE:
            if ( (iob->targetCommandLength != 1) || 
                 (iob->flagsIob.outOfOrderTransfer) ) 
            {
               /* SCSI protocol expects one byte of status */
               /* outOfOrderTransfer not supported */ 
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
               return; 
            }
            hiob->cmd = SCSI_CMD_REESTABLISH_AND_COMPLETE;
            SCSI_NEXUS_UNIT(hiob) = SCSI_NEXUSTSH(taskSetHandle);
            hiob->ioLength = iob->ioLength;
            hiob->snsBufferSize = (SCSI_UEXACT16) iob->targetCommandBufferSize;
            scsiStatus = (SCSI_UEXACT8 HIM_PTR) iob->targetCommand;
            hiob->initiatorStatus = scsiStatus[0];
            if (iob->flagsIob.targetRequestType == HIM_REQUEST_TYPE_TMF)
            {
               hiob->SCSI_IF_taskManagementResponse = 1;
            }
            else
            {
               hiob->SCSI_IF_taskManagementResponse = 0;
            }  
            break;
             
         case HIM_REESTABLISH_INTERMEDIATE:
            if (iob->flagsIob.outOfOrderTransfer) 
            {
               /* Not supported */
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
               return; 
            }
            hiob->cmd = SCSI_CMD_REESTABLISH_INTERMEDIATE;
            SCSI_NEXUS_UNIT(hiob) = SCSI_NEXUSTSH(taskSetHandle);
            hiob->ioLength = iob->ioLength;
            /* hiob->snsBuffer = iob->errorData;  */
            hiob->snsBufferSize = (SCSI_UEXACT16)iob->targetCommandBufferSize;
            if (transportSpecific)
            {
               hiob->SCSI_IF_disallowDisconnect = transportSpecific->disallowDisconnect;
            }
            else
            {
               hiob->SCSI_IF_disallowDisconnect = 0; 
            }
            break;

         case HIM_ABORT_NEXUS:
            hiob->cmd = SCSI_CMD_ABORT_NEXUS;
            SCSI_NEXUS_UNIT(hiob) = SCSI_NEXUSTSH(taskSetHandle);
            break;

#if SCSI_MULTIPLEID_SUPPORT
         case HIM_ENABLE_ID:
            if (!SCSIxEnableID(iob)) 
            {
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
               return;
            }
            else
            {
               hiob->cmd = SCSI_CMD_ENABLE_ID;
               hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
               hiob->snsBufferSize =
                  ((HIM_TS_ID_SCSI HIM_PTR) iob->transportSpecific)->scsiIDMask;
            }
            break;

         case HIM_DISABLE_ID:
            if (!SCSIxDisableID(iob))
            {
               iob->taskStatus = HIM_IOB_INVALID;
               iob->postRoutine(iob);
               return;
            }
            else
            {
               hiob->cmd = SCSI_CMD_DISABLE_ID;
               hiob->unitHandle.initiatorUnit = &SCSI_ADPTSH(taskSetHandle)->hhcb;
               hiob->snsBufferSize =
                  ((HIM_TS_ID_SCSI HIM_PTR) iob->transportSpecific)->scsiIDMask;
            }
            break;
          
#endif /* SCSI_MULTIPLEID_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */

         case HIM_LOGICAL_UNIT_RESET:
            hiob->cmd = SCSI_CMD_LOGICAL_UNIT_RESET;
            hiob->unitHandle.targetUnit = &SCSI_TRGTSH(taskSetHandle)->unitControl;
            break;
            
         default:
            /* indicate iob invalid and post back immediately */
            iob->taskStatus = HIM_IOB_INVALID;
            iob->postRoutine(iob);
            return ;
      }

      /* send request to internal HIM */
      SCSI_QUEUE_SPECIAL_HIOB(hiob);
   }
}
#endif /* SCSI_STREAMLINE_QIOPATH */

/*********************************************************************
*
*  SCSIFrontEndISR
*
*     Execute front end interrupt service rutine
*
*  Return Value:  HIM_NOTHING_PENDING, no interrupt pending
*                 HIM_INTERRUPT_PENDING, interrupt pending
*                 HIM_LONG_INTERRUPT_PENDING, lengthy interrupt pending
*                  
*  Parameters:    adapter TSH
*
*  Remarks:       This routine should be very fast in execution.
*                 The lengthy processing of interrupt should be
*                 handled in SCSIBackEndISR.
*
*********************************************************************/
#if SCSI_STREAMLINE_QIOPATH
HIM_UINT8 SCSIFrontEndISR (HIM_TASK_SET_HANDLE adapterTSH)
{
/************************************************
   new locals start 
*************************************************/
   SCSI_HHCB SCSI_HPTR hhcb; /* SCSIHFrontEndISR,SCSIhStandardQoutcnt */
   register SCSI_REGISTER scsiRegister; /* SCSIHFrontEndISR */
   SCSI_UEXACT8 hstintstat; /* SCSIHFrontEndISR */
   int i; /* SCSIHFrontEndISR */
   SCSI_UEXACT8 qoutcnt; /* SCSIHFrontEndISR */
   SCSI_UEXACT8 qDonePass; /* SCSIhStandardQoutcnt */
   SCSI_UEXACT16 qDoneElement; /* SCSIhStandardQoutcnt */
   SCSI_QOUTFIFO_NEW SCSI_HPTR qOutPtr; /* SCSIhStandardQoutcnt */
   SCSI_UEXACT8 quePassCnt; /* SCSIhStandardQoutcnt */
/************************************************
   new locals end 
*************************************************/

/************************************************
   SCSIHFrontEndISR() start 
*************************************************/
   hhcb = &SCSI_ADPTSH(adapterTSH)->hhcb;
   scsiRegister = hhcb->scsiRegister;

   /* check if there is any command complete interrupt */
   for (i = 0;; i++)
   {
/************************************************
   SCSIhStandardQoutcnt() start 
*************************************************/
      /* SCSI_UEXACT8 n = 0; */
      qoutcnt = 0; /* use qoutcnt since cant return(n) */
      qDonePass = hhcb->SCSI_HP_qDonePass;
      qDoneElement = hhcb->SCSI_HP_qDoneElement;
      qOutPtr = SCSI_QOUT_PTR_ARRAY_NEW(hhcb) + qDoneElement;
                                       
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
            /* ++n; */
            ++qoutcnt; /* use qoutcnt since cant return(n) */

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

      /* return(n); */
/************************************************
   SCSIhStandardQoutcnt() end 
*************************************************/
      /* qoutcnt = SCSI_hQOUTCNT(hhcb); - must delete since no function call */
      if (hhcb->SCSI_HP_qoutcnt != qoutcnt)
      {
         /* remember the interrupt status (command complete) */
         hhcb->SCSI_HP_qoutcnt = qoutcnt;
         hhcb->SCSI_HP_hstintstat |= (hstintstat = SCSI_CMDCMPLT);

         /* clear the interrupt and assume this is the only */
         /* interrupt we have so far */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRCMDINT);
         break;
      }
      else
      {
         /* just read interrupt status from hardware */
         hstintstat = OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT));

         /* For Cardbus (APA-1480x), the instat register value is 0xFF */
         /* when the card is hot removed from the bus.  For this case, */
         /* CHIM returns with no interrupt pending.  This value is not */
         /* going to happen on the scsi host adapter. */
         if (hstintstat == 0xFF)
         {
            hhcb->SCSI_HP_hstintstat = hstintstat = 0;
            break;
         }
         
         if (hstintstat & SCSI_CMDCMPLT)
         {
            if (i)
            {
               /* clear the processed but not cleared */
               /* command complete interrupt */
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRCMDINT);
               /*go back to check after just clearng the INT*/
               continue;
            }
            else
            {
               /* go back to check if it's processed but not cleared */
               /* command complete interrupt */
               continue;
            }
         }

         if (hstintstat & SCSI_HSTINTSTAT_MASK)
         {
            /* keep interrupt status */
            hhcb->SCSI_HP_hstintstat |= (hstintstat &
                                         (SCSI_HSTINTSTAT_MASK | SCSI_CMDCMPLT));

            hhcb->SCSI_HP_hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

            /* disable hardware interrupt with pause */
            SCSI_hWRITE_HCNTRL(hhcb, SCSI_PAUSE);
         }

         /* there is no point to record spurrious interrupt */
         hstintstat &= (SCSI_HSTINTSTAT_MASK | SCSI_CMDCMPLT);
         break;
      }
   }

   OSD_SYNCHRONIZE_IOS(hhcb);

   if (hstintstat)
   {
      /* if the interrupt is only command complete interrupt */
      if ((hstintstat & ~SCSI_CMDCMPLT) == 0)
      {
         /* then return normal interrupt pending */
         return(HIM_INTERRUPT_PENDING);
      }
      else
      {
         /* at least one exception interrupt pending */
         return(HIM_LONG_INTERRUPT_PENDING);
      }
   }
   else
   {
      return(HIM_NOTHING_PENDING);
   }
/************************************************
   SCSIHFrontEndISR() end
*************************************************/
}
#endif /* SCSI_STREAMLINE_QIOPATH */

/*********************************************************************
*
*  SCSIBackEndISR
*
*     Execute back end interrupt service rutine
*
*  Return Value:  none
*                  
*  Parameters:    adapter TSH
*
*  Remarks:       It's OSM's decision to execute this routine at
*                 interrupt context or not.
*
*********************************************************************/
#if SCSI_STREAMLINE_QIOPATH
void SCSIBackEndISR (HIM_TASK_SET_HANDLE adapterTSH)
{

/************************************************
   new locals start 
*************************************************/
   SCSI_HHCB SCSI_HPTR hhcb = &SCSI_ADPTSH(adapterTSH)->hhcb; /* SCSIHBackEndISR, */
      /* SCSIhCmdComplete,SCSIhStandardRetrieveScb,SCSIhTerminateCommand, */
      /* SCSIrCompleteHIOB,SCSIrFreeScb,SCSIrReturnScb,SCSIrFreeCriteria */
      /* SCSIrPostDoneQueue */
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister; /* SCSIHBackEndISR */
      /* SCSIhCmdComplete */
   SCSI_HIOB SCSI_IPTR hiob; /* SCSIHBackEndISR,SCSIhCmdComplete,SCSIhTerminateCommand, */
      /* SCSIhSetStat,SCSIrCompleteHIOB,SCSIrFreeScb,SCSIrReturnScb,SCSIrFreeCriteria, */
      /* OSDCompleteHIOB,SCSIxTranslateError,SCSIrPostDoneQueue */
   SCSI_UEXACT8 hstintstat; /* SCSIHBackEndISR */
   SCSI_UEXACT8 sstat1; /* SCSIHBackEndISR */
   SCSI_UEXACT16 scbNumber; /* SCSIHBackEndISR,SCSIhCmdComplete,SCSIhStandardRetrieveScb */
   SCSI_UEXACT8 arpintcode; /* SCSIHBackEndISR */
   SCSI_QOUTFIFO_NEW SCSI_HPTR qOutPtr; /* SCSIhStandardRetrieveScb */
   SCSI_UEXACT8 qPassCnt; /* SCSIhStandardRetrieveScb */
   SCSI_UNIT_CONTROL SCSI_IPTR targetUnit; /* SCSIhTerminateCommand,SCSIrFreeCriteria */
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl; /* SCSIrCompleteHIOB,SCSIrFreeCriteria */
   SCSI_HIOB SCSI_IPTR hiobNew; /* SCSIrFreeScb,SCSIrFreeCriteria */
   SCSI_UINT16 maxScbs; /* SCSIrFreeCriteria */
   HIM_IOB HIM_PTR iob; /* OSDCompleteHIOB,SCSIxTranslateError */
   SCSI_UEXACT8 hcntrl; /* SCSIhCmdComplete */
   SCSI_UEXACT8 exceptionCount; /* SCSIHBackEndISR */
   SCSI_UEXACT8 simode0; /* SCSIHBackEndISR */
   SCSI_UEXACT8 sstat0; /* SCSIHBackEndISR */
#if !SCSI_DCH_U320_MODE
   HIM_UEXACT32 statCmd;
   SCSI_UEXACT16 prgmcnt; /* SCSIHBackEndISR */
#endif /* !SCSI_DCH_U320_MODE */
   
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
   SCSI_BOOLEAN pausePCIorPCIXFlag = SCSI_FALSE; /* SCSIHBackEndISR */
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

#if SCSI_TEST_OSMEVENTS
   SCSI_UEXACT8 fakeReset = 0;
   SCSI_UEXACT8 fakeHaFail = 0;
   SCSI_UEXACT8 fakeIoBusHang = 0;
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
   SCSI_UEXACT8 fakePciPERR = 0;
   SCSI_UEXACT8 fakePciError = 0;
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */
#endif /* SCSI_TEST_OSMEVENTS */
#if SCSI_RAID1
   SCSI_UEXACT8 raid1State = SCSI_RAID1_PRIMARY_COMPLETE;
   SCSI_UEXACT8 autoSense;
#endif /* SCSI_RAID1 */
   SCSI_UEXACT8 saved_hcntrl = 0;

/************************************************
   new locals end 
*************************************************/

#if SCSI_TARGET_OPERATION
   OSDDebugPrint(8,("\nEntered SCSIBackEndISR "));
#endif /* SCSI_TARGET_OPERATION */

   /* call to SCSI_BACK_END_ISR(&SCSI_ADPTSH(adapterTSH)->hhcb); */
/************************************************
   SCSIRBackEndISR() start
*************************************************/

/************************************************
   SCSIHBackEndISR() start
*************************************************/
   /* call to SCSIHBackEndISR(hhcb); */

   hstintstat = hhcb->SCSI_HP_hstintstat;
   hhcb->SCSI_HP_hstintstat = 0;

   if (hstintstat & SCSI_CMDCMPLT)
   {
      /* handle command complete interrupt */
      /* call to SCSIhCmdComplete(hhcb); */
/************************************************
   SCSIhCmdComplete() start
*************************************************/
      /* int retValue = 0; dont need a return value */

      while (1)
      {
         /* When the hardware is run under memory mapped I/O configuration,  */
         /* the I/O write to clear command complete interrupt (which is done */
         /* in FrontEndISR() and in BackEndISR()) may get delayed for a long */
         /* time before it reaches the hardware due to one or more PCI(X)    */
         /* bridges on a system.  It might be long enough so that the        */
         /* sequencer might have posted another done queue element and       */
         /* generated another the CMDCMPLT interrupt, before our clear gets  */
         /* to the hardware.  Two problems occur due to this:                */
         /*    a) the clear CMDCMPLT would clear the second CMDCMPLT int, and*/
         /*    b) the done q element that was posted by the sequencer may not*/
         /*       make it to the memory in time making us not able to see the*/
         /*       element in the memory when we scan the done q below.       */
         /* This would cause the system to lose the second CMDCMPLT          */
         /* interrupt and lose the done q element that was posted, causing   */
         /* a timeout/hang.  At this time it is not known why this problem   */
         /* would not happen in an I/O mapped I/O configuration.  To be safe */
         /* we will cover both the configuration.                            */
         /* The fix is to read any register from the hardware which will     */
         /* make all the intermediate bridges flush any posted writes pending*/
         /* for the hardware. For more details see PCI 2.2 specification,    */
         /* section 3.2.5 (the problem is summarized in the last paragraph   */
         /* on page 43.                                                      */
         /* NOTE:  We have chosen to read HCNTRL register, because the value */
         /*        can be used for the workaround below for the harpoon2 rev */
         /*        B hardware problem detailed in razor issue# 904. */
         saved_hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

         /* service all command complete outstanding */

         /* dont call in while loop - */
         /* while ((scbNumber = (SCSI_UEXACT8) SCSI_hRETRIEVESCB(hhcb)) != SCSI_NULL_SCB) */
         /* while (!SCSI_IS_NULL_SCB(scbNumber = (SCSI_UEXACT8) SCSI_hRETRIEVESCB(hhcb))) */
         while (1)
         {
/************************************************
   SCSIhStandardRetrieveScb() start
*************************************************/
            qOutPtr = SCSI_QOUT_PTR_ARRAY_NEW(hhcb) + hhcb->SCSI_HP_qDoneElement;

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
               /*scbNumber = SCSI_NULL_SCB;*/
               /*SCSI_SET_NULL_SCB(scbNumber);*/
               break; /* break, instead of while(scbNumber != NULL_SCB) */
            }

            /* return(scbNumber); dont need to return a value */ 
/************************************************
   SCSIhStandardRetrieveScb() end
*************************************************/
            /* get associated hiob */
            hiob = SCSI_ACTPTR[scbNumber];

            /* If there is no associated hiob, assume it as fatal */
            if (hiob == SCSI_NULL_HIOB)
            {
               /* Something is wrong with the device or sequencer */
               /* if there is no hiob reference */
               /* save the HCNTRL value */
               hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

#if !SCSI_DCH_U320_MODE
               /* The following workaround is to handle conditions that when Harpoon      */
               /* generates exceptional interrupt while ARP (sequencer) hangs during      */
               /* executing a sequencer instruction and sequencer will never get paused.  */
               /* HSTINTSTAT register indicates it's an exceptional interrupt and CHIM    */
               /* code in SCSIFrontEndISR also cannot pause sequencer when CHIM writes    */
               /* SCSI_PAUSE to HCNTRL register to disable INTEN bit.                     */
               /* One case is that 3rd party reset happens on SCSI bus while sequencer    */
               /* accesses DFCNTRL register that causes indefinite stretch condition.     */
               /* The chip generates SCSI interrupt but sequencer cannot be paused.       */
               /* Previous CHIM logic does not detect this kind of ARP hang condition     */
               /* and would continue to execute the normal error handling codes.          */
               /* For Harpoon RevA ASIC design, when the software access any registers    */
               /* residing on CIO bus without pausing sequencer first, Harpoon generates  */
               /* PCI Target Abort.  This causes NMI in Vulcan environment.               */

               if ((!(hcntrl & SCSI_PAUSEACK)) && (OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT)) & SCSI_HSTINTSTAT_MASK))
               {
                  if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
                  {
                     /* Harpoon2 Rev A has a problem after writing CHIPRST bit to HCNTRL  */
                     /* register to generate chip reset.  The problem is that Harpoon2's  */
                     /* parity check logic is still running which causes the chip to      */
                     /* drive wrong PERR# or SERR# to pci bus.  The details of this       */
                     /* hardware problem can be found in the Razor database entry #630.   */
                     /* The workaround is to disable SERRESPEN and PERRESPEN bits in      */
                     /* Command registers of PCI config space before writing CHIPRST bit  */
                     /* to HCNTRL register.                                               */

                     /* Cannot use Harpoon's backdoor function to access PCI config space */
                     /* since need to access MODE_PTR register while MODE_PTR register    */
                     /* cannot be accessed because of the ARP hang problem.               */

                     /* get status and command from hardware */
                     statCmd = OSMxReadPCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                                       SCSI_STATUS_CMD_REG);
                     OSMxWritePCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                              SCSI_STATUS_CMD_REG, (statCmd & (~SCSI_SERRESPEN | ~SCSI_PERRESPEN)));
                  }

                  /* To get out of sequencer hang condition, the first thing  */
                  /* to do is to issue chip reset.                            */
                  /* Reset the chip */
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL), SCSI_CHIPRST);

                  if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
                  {
                     /* Harpoon2 Rev A chip has a problem wherein a register access can be  */
                     /* started only after about three PCI clock cycles after a chip reset. */
                     /* The reason is that the chip takes some amount of time to initialize */
                     /* PCI Host Slave hardware module, and if an access to a register      */
                     /* happens during this time the chip would not respond.  The faster    */
                     /* the PCI clock, the chances are higher to fit a register access cycle*/
                     /* within this time window.  So, the problem is more prominent when the*/
                     /* PCI clock speed is higher.  The workaround is to induce a delay by  */
                     /* calling the OSM's delay routine. There was no Razor database entry  */
                     /* when this workaround was implemented. */
                     OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
                  }

                  if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
                  {
                     /* Harpoon2 Rev A has a problem after writing CHIPRST bit to HCNTRL  */
                     /* register to generate chip reset.  The problem is that Harpoon2's  */
                     /* parity check logic is still running which causes the chip to      */
                     /* drive wrong PERR# or SERR# to pci bus.  The details of this       */
                     /* hardware problem can be found in the Razor database entry #630.   */
                     /* The workaround is after chip reset to clear possible pci errors   */
                     /* in  Status register and restore back Command registers in         */
                     /* PCI config space                                                  */

                     /* Set Mode 4 - to access PCI Configuration registers */
                     SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_STATUS_HI_REG), SCSI_PCIERR_MASK);

                     /* Set back to Mode 3 */
                     SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

                     OSMxWritePCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                              SCSI_STATUS_CMD_REG, statCmd);
                  }
               }
#endif /* !SCSI_DCH_U320_MODE */

               /* pause the chip if necessary */
               if (!(hcntrl & SCSI_PAUSEACK))
               {
                  SCSIhPauseAndWait(hhcb);
               }

               SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID04);

               /* restore HCNTRL if necessary */
               if (!(hcntrl & SCSI_PAUSEACK))
               {
                  SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
               }
               break;
            }

            /* Due to the active abort implementation, a HIOB to be aborted */
            /* might completed before the active abort message has ever */
            /* started.  Therefore, we will not post back any HIOB that */
            /* associated with the abort already in progress status. */
            if (hiob->SCSI_IP_mgrStat != SCSI_MGR_ABORTINPROG)
            {
               /* indicate command complete was active */
               /* ++retValue; dont need return value */

               /* terminate the command */
#if SCSI_TARGET_OPERATION
               if ((hiob)->SCSI_IP_targetMode)
               {
                  /* call to SCSIhTerminateTargetCommand */
                  SCSIhTerminateTargetCommand(hiob);
               }
               else
               {
#endif /* SCSI_TARGET_OPERATION */
                  /* call to SCSIhTerminateCommand(hiob); */
/************************************************
   SCSIhTerminateCommand() start
*************************************************/
                  targetUnit = SCSI_TARGET_UNIT(hiob);

                  /* free the associated entry in active pointer array */
                  SCSI_ACTPTR[hiob->scbNumber] = SCSI_NULL_HIOB;
#if SCSI_RAID1
                  if (hiob->SCSI_IF_raid1)
                  {    
                      SCSI_ACTPTR[hiob->mirrorHiob->scbNumber] = SCSI_NULL_HIOB;
                  }
#endif /* SCSI_RAID1 */
#if SCSI_TEST_OSMEVENTS
                  iob = SCSI_GETIOB(hiob);  
                  if (iob->sortTag == (HIM_UINT32)HIM_EVENT_IO_CHANNEL_RESET)
                  {
                     /* fake 3rd party reset */
                     fakeReset = 1;
                  }
                  else
                  if (iob->sortTag == (HIM_UINT32)HIM_EVENT_HA_FAILED)
                  {
                     /* fake host adapter failed */
                     fakeHaFail = 1;
                  }
                  else
                  if (iob->sortTag == (HIM_UINT32)HIM_EVENT_IO_CHANNEL_FAILED)
                  {
                     /* fake I/O channel hang */
                     fakeIoBusHang = 1;
                  }
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
                  else
                  if (iob->sortTag == (HIM_UINT32)HIM_EVENT_PCI_ERROR)
                  {
                     /* fake PCI target abort error */
                     fakePciPERR = 1;
                     /* Use softwareVersion as parameter to 
                      * SCSIhClearPCIError to indicate which event
                      * to generate.
                      */
                     hhcb->softwareVersion = (HIM_UEXACT8)SCSI_AE_ABORTED_PCI_ERROR;
                  }
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

#endif /* SCSI_TEST_OSMEVENTS */
                  /* make sure proper status get passed back */
/************************************************
   SCSIhSetStat() start
*************************************************/
                  /* call to SCSIhSetStat(hiob); */
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
                  /* If the target is in packetized mode, and 'trgStatus' is */
                  /* is SCSI_UNIT_CHECK, then get the Request Sense data from*/
                  /* the internal buffer. */
                  if ((hiob->trgStatus == SCSI_UNIT_CHECK) &&
                      SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
                  {
                     SCSIhPacketizedGetSenseData(hiob);
                  }
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */

                  if (hiob->haStat || ((hiob->trgStatus != SCSI_UNIT_GOOD) &&
                                       (hiob->trgStatus != SCSI_UNIT_MET) &&
                                       (hiob->trgStatus != SCSI_UNIT_INTERMED) &&
                                       (hiob->trgStatus != SCSI_UNIT_INTMED_GD)))
                  {
                     if (hiob->stat != SCSI_SCB_ABORTED)
                     {
                        /* The HIOB is really completed.  But the haStat was set during */
                        /* active abort process.  And again check for the trgStatus. */
                        if (((hiob->haStat == SCSI_HOST_ABT_HOST) ||
                             (hiob->haStat == SCSI_HOST_ABT_LUN_RST) ||
                             (hiob->haStat == SCSI_HOST_ABT_TRG_RST)) &&
                            ((hiob->trgStatus == SCSI_UNIT_GOOD) ||
                             (hiob->trgStatus == SCSI_UNIT_MET) ||
                             (hiob->trgStatus == SCSI_UNIT_INTERMED) ||
                             (hiob->trgStatus == SCSI_UNIT_INTMED_GD)))
                        {
                           hiob->stat = SCSI_SCB_COMP;   /* HIOB completed without error */
                        }
                        else
                        {
                           hiob->stat = SCSI_SCB_ERR;    /* HIOB completed with error */
                        }
                     }
                     else
                     {
                        /* Check for the terminate HIOB due to the selection time-out, */
                        /* unexpected busfree, or other target errors: queue full etc. */
                        if ((hiob->haStat != SCSI_HOST_ABT_HOST) &&
                            (hiob->haStat != SCSI_HOST_ABT_TRG_RST) &&
                            (hiob->haStat != SCSI_HOST_ABT_LUN_RST))
                        {
                           hiob->stat = SCSI_SCB_ERR;    /* HIOB completed with error */
                        }
                        else if ((hiob->trgStatus != SCSI_UNIT_GOOD) &&
                                 (hiob->trgStatus != SCSI_UNIT_MET) &&
                                 (hiob->trgStatus != SCSI_UNIT_INTERMED) &&
                                 (hiob->trgStatus != SCSI_UNIT_INTMED_GD))
                        {
                           /* Need to clear haStat because the error is the trgStatus */
                           hiob->haStat = 0;
                           hiob->stat = SCSI_SCB_ERR;    /* HIOB completed with error */
                        }

                     }
                  }
                  else
                  {
                     hiob->stat = SCSI_SCB_COMP;         /* Update status */
                  }

#if SCSI_RAID1
                  if (hiob->SCSI_IF_raid1)
                  {
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
                     /* If the target is in packetized mode, and 'trgStatus' is */
                     /* is SCSI_UNIT_CHECK, then get the Request Sense data from*/
                     /* the internal buffer. */
                     if ((hiob->mirrorHiob->trgStatus == SCSI_UNIT_CHECK) &&
                         SCSI_hISPACKETIZED(hiob->mirrorHiob->unitHandle.targetUnit->deviceTable))
                     {
                        SCSIhPacketizedGetSenseData(hiob->mirrorHiob);
                     }
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */
                     if (hiob->mirrorHiob->haStat || ((hiob->mirrorHiob->trgStatus != SCSI_UNIT_GOOD) &&
                                       (hiob->mirrorHiob->trgStatus != SCSI_UNIT_MET) &&
                                       (hiob->mirrorHiob->trgStatus != SCSI_UNIT_INTERMED) &&
                                       (hiob->mirrorHiob->trgStatus != SCSI_UNIT_INTMED_GD)))
                     {
                        if (hiob->mirrorHiob->stat != SCSI_SCB_ABORTED)
                        {
                           /* The hiob->mirrorHiob is really completed.  But the haStat was set during */
                           /* active abort process.  And again check for the trgStatus. */
                           if (((hiob->mirrorHiob->haStat == SCSI_HOST_ABT_HOST) ||
                                (hiob->mirrorHiob->haStat == SCSI_HOST_ABT_LUN_RST) ||
                                (hiob->mirrorHiob->haStat == SCSI_HOST_ABT_TRG_RST)) &&
                               ((hiob->mirrorHiob->trgStatus == SCSI_UNIT_GOOD) ||
                                (hiob->mirrorHiob->trgStatus == SCSI_UNIT_MET) ||
                                (hiob->mirrorHiob->trgStatus == SCSI_UNIT_INTERMED) ||
                                (hiob->mirrorHiob->trgStatus == SCSI_UNIT_INTMED_GD)))
                           {
                              /* hiob->mirrorHiob completed without error */
                              hiob->mirrorHiob->stat = SCSI_SCB_COMP;
                           }
                           else
                           {
                              /* HIOB completed with error */
                              hiob->mirrorHiob->stat = SCSI_SCB_ERR;
                           }
                        }
                        else
                        {
                           /* Check for the terminate hiob->mirrorHiob due   */
                           /* to the selection time-out, unexpected busfree, */
                           /* or other target errors: queue full etc.        */
                           if ((hiob->mirrorHiob->haStat != SCSI_HOST_ABT_HOST) &&
                               (hiob->mirrorHiob->haStat != SCSI_HOST_ABT_LUN_RST) &&
                               (hiob->mirrorHiob->haStat != SCSI_HOST_ABT_TRG_RST))
                           {
                              /* hiob->mirrorHiob completed with error */
                              hiob->mirrorHiob->stat = SCSI_SCB_ERR;
                           }
                           else if ((hiob->mirrorHiob->trgStatus != SCSI_UNIT_GOOD) &&
                                 (hiob->mirrorHiob->trgStatus != SCSI_UNIT_MET) &&
                                 (hiob->mirrorHiob->trgStatus != SCSI_UNIT_INTERMED) &&
                                 (hiob->mirrorHiob->trgStatus != SCSI_UNIT_INTMED_GD))
                           {
                              /* Need to clear haStat because the error is the trgStatus */
                              hiob->mirrorHiob->haStat = 0;

                              /* hiob->mirrorHiob completed with error */
                              hiob->mirrorHiob->stat = SCSI_SCB_ERR;
                           }

                        }
                     }
                     else
                     {
                        /* Update status */
                        hiob->mirrorHiob->stat = SCSI_SCB_COMP;
                     }
                  }
#endif /* SCSI_RAID1 */

/************************************************
   SCSISetStat() end
*************************************************/

                  if (hiob->cmd == SCSI_CMD_INITIATE_TASK)
                  {
                     /* post it back to upper layer code */
                     /* call SCSI_COMPLETE_HIOB(hiob); */
/************************************************
   SCSIrCompleteHIOB() start
*************************************************/
                     if (hiob->stat != SCSI_SCB_FROZEN)
                     { 
                        if (hiob->trgStatus == SCSI_UNIT_QUEFULL)
                        {
                           targetControl = SCSI_TARGET_UNIT(hiob)->targetControl; 
                           /* The queue has already been frozen by the */
                           /* hardware layer; just set the             */
                           /* SCSI_FREEZE_QFULL bit in the freezemap   */
                           /* variable                                 */
                           SCSIrFreezeDeviceQueue(targetControl, SCSI_FREEZE_QFULL);
                        }
                     }
                     /* free scb associated */
                     /* call to SCSIrFreeScb(hhcb,hiob); */
/************************************************
   SCSIrFreeScb() start
*************************************************/
                     /* Check if Host Q needing service */
                     if ((!hhcb->SCSI_WAIT_FREEZEMAP) && 
                         (hhcb->SCSI_WAIT_HEAD != SCSI_NULL_HIOB))
                     {

                        /* First return the SCB buffer to the free queue */
                        hhcb->SCSI_FREE_TAIL->queueNext = hiob->scbDescriptor;
                        hhcb->SCSI_FREE_TAIL = hiob->scbDescriptor;

                        /* Nullify scbNumber of the returned SCB buffer as it is no longer */
                        /* associated with this SCB buffer */
                        SCSI_SET_NULL_SCB(hhcb->SCSI_FREE_TAIL->scbNumber);

                        /* Return completed SCB # to the appropriate pool */
                        SCSI_rRETURNSCB(hhcb,hiob);

                        /* The return value of SCSI_rGETSCB must be checked
                         * in case the SCB returned was > 255 and the request 
                         * to be dequeued from the Host Q was for a target operating
                         * at non-packetized transfer rate. We could scan
                         * the HOST Q for a suitable request but this would
                         * be time consuming. Just use the Host Q head and  
                         * only dequeue when SCB is available.
                         */
                        hiobNew = hhcb->SCSI_WAIT_HEAD; 
                        hiobNew->scbNumber = SCSI_rGETSCB(hhcb,hiobNew);
                        if (!SCSI_IS_NULL_SCB(hiobNew->scbNumber))
                        {
                           /* Remove HIOB from Host Queue now */
                           hiobNew = SCSIrHostQueueRemove(hhcb);

                           /* There are some special HIOBs, e.g. Bus Device Reset, */
                           /* are queueing just like a normal HIOB */
                           if (hiobNew->cmd == SCSI_CMD_INITIATE_TASK)
                           {
                              SCSIHQueueHIOB(hiobNew);
                           }
                           else
                           {
                              SCSIHQueueSpecialHIOB(hiobNew);
                           }
                        }
                     }
                     else
                     {
                        /* return it to free pool */
/************************************************
   SCSIrReturnScb() start
*************************************************/
#if SCSI_RAID1 
#if SCSI_PACKETIZED_IO_SUPPORT
                        if (hiob->SCSI_IF_raid1)
                        {
                           if (hiob->mirrorHiob->scbNumber >= (SCSI_UEXACT16)256)
                           {
                              if ((--hhcb->SCSI_FREE_PACKETIZED_PTR) == (SCSI_UEXACT8)0xFF)
                              {
                                 hhcb->SCSI_FREE_PACKETIZED_PTR =
                                    (SCSI_UEXACT8)((hhcb->numberScbs - 1) - (SCSI_UEXACT16)256);
                              }
                              hhcb->SCSI_FREE_PACKETIZED_STACK[hhcb->SCSI_FREE_PACKETIZED_PTR] =
                                 hiob->mirrorHiob->scbNumber;
                           }
                           else
                           {
                              if (((--hhcb->SCSI_FREE_NON_PACKETIZED_PTR) == (SCSI_UEXACT8)0xFF) &&
                               (hhcb->numberScbs < (SCSI_UEXACT16)256))
                              {
                                 hhcb->SCSI_FREE_NON_PACKETIZED_PTR =
                                    (SCSI_UEXACT8)(hhcb->numberScbs - 1);
                              }
                              hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR]=
                                 hiob->mirrorHiob->scbNumber;
                           }
                        }
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
                        if (hiob->SCSI_IF_raid1)
                        {
                           /* Return the SCB number to the non-packetized STACK */
                           if (((--hhcb->SCSI_FREE_NON_PACKETIZED_PTR) == (SCSI_UEXACT8)0xFF) &&
                               (hhcb->numberScbs < (SCSI_UEXACT16)256))
                           {
                              hhcb->SCSI_FREE_NON_PACKETIZED_PTR =
                                 (SCSI_UEXACT8)(hhcb->numberScbs - 1);
                           }
                           hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR] =
                              hiob->mirrorHiob->scbNumber;
                        }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#endif /* SCSI_RAID1 */

#if SCSI_PACKETIZED_IO_SUPPORT
                        /* Return the SCB number to the appropriate STACK */
                        if (hiob->scbDescriptor->scbNumber >= (SCSI_UEXACT16)256)
                        {
                           if ((--hhcb->SCSI_FREE_PACKETIZED_PTR) == (SCSI_UEXACT8)0xFF)
                           {
                              hhcb->SCSI_FREE_PACKETIZED_PTR =
                                 (SCSI_UEXACT8)((hhcb->numberScbs - 1) - (SCSI_UEXACT16)256);
                           }
                           hhcb->SCSI_FREE_PACKETIZED_STACK[hhcb->SCSI_FREE_PACKETIZED_PTR] =
                              hiob->scbDescriptor->scbNumber;
                        }
                        else
                        {
                           if (((--hhcb->SCSI_FREE_NON_PACKETIZED_PTR) == (SCSI_UEXACT8)0xFF) &&
                               (hhcb->numberScbs < (SCSI_UEXACT16)256))
                           {
                              hhcb->SCSI_FREE_NON_PACKETIZED_PTR =
                                 (SCSI_UEXACT8)(hhcb->numberScbs - 1);
                           }
                           hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR]=
                              hiob->scbDescriptor->scbNumber;
                        }
#else /* !SCSI_PACKETIZED_IO_SUPPORT */

                        /* Return the SCB number to the non-packetized STACK */
                        if (((--hhcb->SCSI_FREE_NON_PACKETIZED_PTR) == (SCSI_UEXACT8)0xFF) &&
                            (hhcb->numberScbs < (SCSI_UEXACT16)256))
                        {
                           hhcb->SCSI_FREE_NON_PACKETIZED_PTR =
                              (SCSI_UEXACT8)(hhcb->numberScbs - 1);
                        }
                        hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR]=
                           hiob->scbDescriptor->scbNumber;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

                        /* Increment non Establish SCB available count */
                        SCSI_rINCREMENT_NON_EST_SCBS_AVAIL(hhcb);

                        /* Return the SCB buffer to the free queue */
                        hhcb->SCSI_FREE_TAIL->queueNext = hiob->scbDescriptor;
                        hhcb->SCSI_FREE_TAIL = hiob->scbDescriptor;

                        /* Nullify scbNumber of the returned SCB buffer as it is no longer */
                        /* associated with this SCB buffer */
                        SCSI_SET_NULL_SCB(hhcb->SCSI_FREE_TAIL->scbNumber);
/************************************************
   SCSIrReturnScb() end
*************************************************/
                     }

                     /* continue to free criteria */
                     /* call to SCSIrFreeCriteria(hiob); */
/************************************************
   SCSIrFreeCriteria() start
*************************************************/
                     targetUnit = SCSI_TARGET_UNIT(hiob);
                     targetControl = targetUnit->targetControl;

                     /* get checking criteria */
                     maxScbs = ((hiob->SCSI_IF_tagEnable) ? 
                                 targetControl->maxTagScbs :
                                 targetControl->maxNonTagScbs);

                     /* To check IU switch criteria ignore freeze on switch */
                     if (!(targetControl->freezeMap & ~SCSI_FREEZE_PACKSWITCH))
                     {
#if SCSI_NEGOTIATION_PER_IOB
                        /* Some hiobs are left in the device queue if there are    */
                        /* more scbs waiting in the rsm layer than maximum allowed */
                        /* tags, OR there is negotiation pending; so just check to */
                        /* see if any HIOBs are sitting in the device queue. */
                        targetControl->activeScbs--;
                        while (targetControl->deviceQueueHiobs)
#else /* SCSI_NEGOTIATION_PER_IOB */
                        if (--targetControl->activeScbs >= maxScbs)
#endif /* SCSI_NEGOTIATION_PER_IOB */
                        {
                           /* some one must be waiting for criteria free */
                           /* dont have ret value if inline */
                           /* if ((hiobNew = SCSIrDeviceQueueRemove(targetControl)) != */
                           /*     SCSI_NULL_HIOB) */
/************************************************
   SCSIrDeviceQueueRemove() start
*************************************************/
                           hiobNew = targetControl->queueHead;

                           if (hiobNew != SCSI_NULL_HIOB)
                           {
                              /* Get the criteria factor */
                              maxScbs = ((hiobNew->SCSI_IF_tagEnable) ?
                                          targetControl->maxTagScbs :
                                             targetControl->maxNonTagScbs);                                           

#if SCSI_PACKETIZED_IO_SUPPORT                                          
                              /* Check for auto-sense and tagEnable and     */
                              /* packetized switching progress flag are on; */
                              /* set to switch to packetiized.              */ 
                              if (hiobNew->SCSI_IF_tagEnable && 
                                  SCSI_TARGET_UNIT(hiobNew)->deviceTable->SCSI_DF_switchPkt
#if (SCSI_TASK_SWITCH_SUPPORT == 1)          
                                  && hiobNew->SCSI_IF_autoSense
#endif /* SCSI_TASK_SWITCH_SUPPORT == 1 */         
                                 )
                              {
                                 if (!SCSIrSwitchToPacketized (targetUnit))
                                 {
                                    maxScbs = 0;
                                 }                                  
                                  else 
                                 {
                                   SCSIrUnFreezeDeviceQueue(targetControl,SCSI_FREEZE_PACKSWITCH);
                                    /* Get the local criteria factor */
                                   maxScbs = ((hiobNew->SCSI_IF_tagEnable) ?
                                          targetControl->maxTagScbs :
                                              targetControl->maxNonTagScbs);                                           
#if SCSI_DOMAIN_VALIDATION
                                    if (hiob->SCSI_IF_dvIOB == 0)
#endif /* SCSI_DOMAIN_VALIDATION */
                                    {
                                       SCSI_TARGET_UNIT(hiob)->deviceTable->negoXferIndex = 
                                             SCSI_TARGET_CAP_ENTRY;
                                    }
                                       
                                 } 
                              }

                              /* Check for target in packetized mode and    */
                              /* auto-sense or tag flag is disable;         */
                              /* set flag to switch to non-packetized mode. */ 
                              if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiobNew)->deviceTable) && 
                                  (!hiobNew->SCSI_IF_tagEnable
          
#if (SCSI_TASK_SWITCH_SUPPORT == 1)                    
                                  || !hiobNew->SCSI_IF_autoSense 
#endif /* SCSI_TASK_SWITCH_SUPPORT == 1 */          
                                 ))                              
                              {   
                                 if (!SCSIrSwitchToNonPacketized (targetUnit))
                                 {
                                    maxScbs = 0;
                                 }                                   
                                  else 
                                 {
                                    SCSIrUnFreezeDeviceQueue(targetControl,SCSI_FREEZE_PACKSWITCH);
                                    /* Get the local criteria factor */
                                    maxScbs = ((hiobNew->SCSI_IF_tagEnable) ?
                                          targetControl->maxTagScbs :
                                             targetControl->maxNonTagScbs);                                           
                                 } 
                              }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
                              /* If criteria is not met, simply return NULL_HIOB */
                              if (maxScbs <= (targetControl->activeScbs -
                                              targetControl->deviceQueueHiobs))
                              {
                                 hiobNew = SCSI_NULL_HIOB;
                              }
                              else
                              {
#if SCSI_NEGOTIATION_PER_IOB
                                 /* If HIOB sets the flags to force negotiation, */
                                 /* try to do the negotiation */
                                 if (hiobNew->SCSI_IF_forceSync ||
                                     hiobNew->SCSI_IF_forceAsync ||
                                     hiobNew->SCSI_IF_forceWide ||
                                     hiobNew->SCSI_IF_forceNarrow)
                                 {
                                    /* If the device is not idle, do not take the   */
                                    /* hiob out of the device queue as more commands*/
                                    /* are pending with the device, else change the */
                                    /* xfer option parameters to do the negotiation */
                                    /* on the next command. */
                                    if (SCSI_CHECK_DEVICE_IDLE(hhcb,
                                          targetUnit->scsiID) == SCSI_NOT_IDLE)
                                    {
                                       hiobNew = SCSI_NULL_HIOB;
                                    }
                                    else
                                    {
                                       SCSI_hCHANGEXFEROPTION(hiobNew);
                                       targetControl->pendingNego--;
                                    }
                                 }

                                 /* Remove the hiob from the device queue only if */
                                 /* allowed to */
                                 if (hiobNew != SCSI_NULL_HIOB)
                                 {
                                    if (targetControl->queueHead == 
                                        targetControl->queueTail)
                                    {
                                       /* queue empty */
                                       targetControl->queueHead = targetControl->queueTail = SCSI_NULL_HIOB;
                                    }
                                    else
                                    {
                                       /* queue won't be empty */
                                       targetControl->queueHead = targetControl->queueHead->queueNext;
                                    }

                                    targetControl->deviceQueueHiobs--;
                                 }
#else /* SCSI_NEGOTIATION_PER_IOB */
                                 if (targetControl->queueHead ==
                                     targetControl->queueTail)
                                 {
                                    /* queue empty */
                                    targetControl->queueHead =
                                       targetControl->queueTail = SCSI_NULL_HIOB;
                                 }
                                 else
                                 {
                                    /* queue won't be empty */
                                    targetControl->queueHead =
                                       targetControl->queueHead->queueNext;
                                 }

                                 targetControl->deviceQueueHiobs--;
#endif /* SCSI_NEGOTIATION_PER_IOB */
                              }
                           }

                           /* return(hiobNew); */
/************************************************
   SCSIrDeviceQueueRemove() end
*************************************************/
                           if (hiobNew != SCSI_NULL_HIOB) /* new way since
                                                             dont have a ret value */
                           {
                              /* continue hiob processing */
                              SCSIrAllocateScb(targetUnit->hhcb,hiobNew);
                           }
#if SCSI_NEGOTIATION_PER_IOB
                           else
                           {
                              break;
                           }
#endif /* SCSI_NEGOTIATION_PER_IOB */
                        }
                     }
                     else
                     {                                 
                        /* Free the criteria */
                        --targetControl->activeScbs;
                  
                        /* Does a Queue Full condition exist for the target? */
                        if (targetControl->freezeMap & SCSI_FREEZE_QFULL)
                        {
                           /* Yes.  We need to decrement the original maximum*/
                           /* number of queue tags (ie. the number at the    */
                           /* time the freeze happened) for each SCB that    */
                           /* finishes with a QUEUE FULL, until an unfreeze  */
                           /* is done. */

                           /* Can an Unfreeze be done? (This can be done only*/
                           /* when the target completes all the pending SCBs)*/
                           if (SCSIHCheckDeviceIdle(hhcb, targetUnit->scsiID)
                                 == SCSI_IDLE)
                           {
                              /* Yes, an unfreeze can be done. */
                              SCSIrFinishQueueFull(hiob);
                           }
                           else
                           {
                              /* No, an unfreeze cannot be done.  Keep       */
                              /* decrementing the original max tags for each */
                              /* queue full (including the first queue full) */
                              if (hiob->trgStatus == SCSI_UNIT_QUEFULL)
                              {
                                 if (targetControl->origMaxTagScbs > 1)
                                 {
                                    targetControl->origMaxTagScbs--;
                                 }
                              }
                           }
                        }
                        else if (targetControl->freezeMap &
                                              SCSI_FREEZE_ERRORBUSY)
                        {
                           /* No. A queue full condition does not exist. But */
                           /* a freeze has still happened.  We need to find  */
                           /* out if the freeze is genuine or not (ie. the   */
                           /* freeze has been notified to the upper layer or */
                           /* not).  It will not be genuine if a packetized  */
                           /* target has generated a check condition, and the*/
                           /* check condition is not due to BUSY or QUEUE    */
                           /* FULL condition.  Is this the case? */
                           if (!(hiob->SCSI_IF_freezeOnError) &&
                               !(SCSI_TARGET_UNIT(hiob)->targetControl->freezeMap &
                               SCSI_FREEZE_HIPRIEXISTS) &&
                               ((hiob->trgStatus != 0) &&
                                (hiob->trgStatus != SCSI_UNIT_BUSY) &&
                                (hiob->trgStatus != SCSI_UNIT_QUEFULL)))
                           {
                              /* Yes. Unfreeze the device queue and dispatch */
                              /* any queued HIOBs if the queue is not frozen.*/
                              SCSIrUnFreezeDeviceQueue(SCSI_TARGET_UNIT(hiob)->targetControl,
                                                       SCSI_FREEZE_ERRORBUSY);
                              SCSIrDispatchQueuedHIOB(hiob);
                           }
                        }

                        if (hiob->priority >= 0x80)
                        {
                           --targetControl->activeHiPriScbs;
                           if (!targetControl->activeHiPriScbs)
                           {
                              /* Unfreeze the device queue and dispatch any queued */
                              /* HIOBs if the queue is not frozen. */
                              SCSIrUnFreezeDeviceQueue(
                                 SCSI_TARGET_UNIT(hiob)->targetControl,
                                 SCSI_FREEZE_HIPRIEXISTS);
                              SCSIrDispatchQueuedHIOB(hiob);
                           }
                        }
                     }
/************************************************
   SCSIrFreeCriteria() end
*************************************************/

/************************************************
   SCSIrFreeScb() end
*************************************************/
                     if (hiob->stat == SCSI_SCB_FROZEN)
                     {
                        targetControl = SCSI_TARGET_UNIT(hiob)->targetControl; 

                        /* Record the fact that the target has one more */
                        /* command active. */
                        targetControl->activeScbs++;

                        /* Queue the hiob back in the Device Queue */
                        SCSIrDeviceQueueAppend(targetControl,hiob);
                     }
                     else
                     {
                        /* Put HIOB into Done queue - Add element at tail */
                        hiob->queueNext = SCSI_NULL_HIOB;      
                        if (hhcb->SCSI_DONE_TAIL == SCSI_NULL_HIOB)
                        {
                           /* This is the 1st element. Init SCSI_DONE_HEAD accordingly */
                           hhcb->SCSI_DONE_HEAD = hiob;
                        }
                        else
                        {
                           /* DoneQ already have at least 1 element */
                           hhcb->SCSI_DONE_TAIL->queueNext = hiob;
                        }
                        hhcb->SCSI_DONE_TAIL = hiob;
                     }
/************************************************
   SCSIrCompletHIOB() end
*************************************************/
                  }
                  else
                  {
                     /* Check and set if scsi negotiation is needed for */
                     /* a particular target after the bus device reset executed */
                     /* Renego not required for LUN reset but some targets do  reset 
                        transfer agreement*/                     
                     if ((hiob->cmd == SCSI_CMD_RESET_TARGET)||
                           (hiob->cmd == SCSI_CMD_LOGICAL_UNIT_RESET))
                     {
#if !SCSI_NEGOTIATION_PER_IOB
                        /* save the HCNTRL value */
                        hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));
  
                        /* pause the chip if necessary */
                        if (!(hcntrl & SCSI_PAUSEACK))
                        {
                           SCSIhPauseAndWait(hhcb);
                        }

                        /* At the completion of a Target Reset HIOB in        */
                        /* packetized mode, CHIM must abort any HIOBs pending */
                        /* at the target.  These HIOBs are in the Active      */
                        /* Pointer Array.  But, they are not in Sequencer Done*/
                        /* queue, Done queue, New queue and Target's Execution*/
                        /* queue.  The reason for New queue and Target's      */
                        /* Execution queue do not have any regular HIOBs is   */
                        /* because all HIOBs are queued up in Device queue in */
                        /* RSM layer.                                         */
#if (SCSI_PACKETIZED_IO_SUPPORT)
                        if (SCSI_hISPACKETIZED(targetUnit->deviceTable))
                        {
                           SCSIhTargetReset(hhcb, hiob);
                        }
#endif /* (SCSI_PACKETIZED_IO_SUPPORT) */

                        /* At the completion of a Target Reset HIOB, the host */
                        /* adapter should run in async/narrow mode to the     */
                        /* reset target.  Hence, CHIM must reset xfer rate    */
                        /* parameters in the current rate entry and in        */
                        /* hardware rate table for this target.               */
                        SCSIhAsyncNarrowXferRateAssign(hhcb,targetUnit->scsiID);

                        if (SCSI_hWIDEORSYNCNEGONEEDED(targetUnit->deviceTable))
                        {
                           /*  Set negotiation needed */
                           SCSIhUpdateNeedNego(hhcb, targetUnit->scsiID);
                        }
                        else
                        {
                           /* No negotiation */
                           targetUnit->deviceTable->SCSI_DF_needNego = 0;
                        }

                        /* restore HCNTRL if necessary */
                        if (!(hcntrl & SCSI_PAUSEACK))
                        {
                           SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
                        }
#endif /* !SCSI_NEGOTIATION_PER_IOB */
                     }

#if SCSI_SCBBFR_BUILTIN
                     /* Return the SCB buffer to the free queue */
                     hhcb->SCSI_FREE_TAIL->queueNext = hiob->scbDescriptor;
                     hhcb->SCSI_FREE_TAIL = hiob->scbDescriptor;

                     /* Nullify scbNumber of the returned SCB buffer as it is */
                     /* no longer associated with this SCB buffer.            */
                     SCSI_SET_NULL_SCB(hhcb->SCSI_FREE_TAIL->scbNumber);
#endif /* SCSI_SCBBFR_BUILTIN */

                     /* post back special HIOB to upper layer code */
                     SCSI_COMPLETE_SPECIAL_HIOB(hiob);
                  }
/************************************************
   SCSIhTerminateCommand() end
*************************************************/
#if SCSI_TARGET_OPERATION
               }
#endif /* SCSI_TARGET_OPERATION */
            }

            --hhcb->SCSI_HP_qoutcnt;

#if SCSI_BACKENDISR_OUTSIDE_INTRCONTEXT
            /* There is a problem when the BackEndISR() was called outside    */
            /* interrupt context.  The problem would occur when the sequencer */
            /* post more completed SCBs in the done queue between the time    */
            /* the FrontEndISR() executed and the last completed SCB          */
            /* (accounted by HIM in FrontEndISR()) was post back to upper     */
            /* layer in this routine.  CHIM will continues to post back all   */
            /* new completed SCBs and cleared the CMDCMPLT bit.  However, the */
            /* hardware interrupt is still pending out there.  Then when the  */
            /* pending interrupt interrupted, the FrontEndISR() will report   */
            /* back a value of 0 to the OSM saying this interrupt wasn't for  */
            /* us.  This will create a spurious interrupt and for some OSes   */
            /* like OS/2, will disable that interrupt in the PIC until the    */
            /* next system boot. */
            if (!hhcb->SCSI_HP_qoutcnt)
            {
               /* return(retValue); cant return because inline */
               break; /* break instead of return because inline */
            }
#endif /* SCSI_BACKENDISR_OUTSIDE_INTRCONTEXT */
         }

         if (hhcb->SCSI_HP_qoutcnt)
         {
            /* we have serviced more command complete than we should */
            /* better clear the clear command interrupt */
            scsiRegister = hhcb->scsiRegister;
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRCMDINT);
            hhcb->SCSI_HP_qoutcnt = 0;
         }
         else
         {
            break;
         }
      }

      /* return(retValue); dont need a return value */
/************************************************
   SCSIhCmdComplete() end
*************************************************/
   }

#if SCSI_TEST_OSMEVENTS
   if (fakeReset || fakeHaFail || fakeIoBusHang)
   {
      hstintstat |= SCSI_SCSIINT;  /* fake interrupt */
      /* pause the chip if necessary */
      if (!(OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & SCSI_PAUSEACK))
      {
         /* If sequencer is not paused then HCNTRL was not saved in the
          * hhcb - this needs to be done for the restore HCNTRL code
          * to work correctly.
          */
         hhcb->SCSI_HP_hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));
         SCSIhPauseAndWait(hhcb);
      }
   }
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
   if (fakePciError || fakePciRTA)
   {
      hstintstat |= SCSI_PCIINT;  /* fake interrupt */
      /* pause the chip if necessary */
      if (!(OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & SCSI_PAUSEACK))
      {
         /* If sequencer is not paused then HCNTRL was not saved in the
          * hhcb - this needs to be done for the restore HCNTRL code
          * to work correctly.
          */
         hhcb->SCSI_HP_hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));
         SCSIhPauseAndWait(hhcb);
      }
   }
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */
#endif /* SCSI_TEST_OSMEVENTS */

   /* The following workaround is to handle conditions that when Harpoon      */
   /* generates exceptional interrupt while ARP (sequencer) hangs during      */
   /* executing a sequencer instruction and sequencer will never get paused.  */
   /* HSTINTSTAT register indicates it's an exceptional interrupt and CHIM    */
   /* code in SCSIFrontEndISR also cannot pause sequencer when CHIM writes    */
   /* SCSI_PAUSE to HCNTRL register to disable INTEN bit.                     */
   /* One case is that 3rd party reset happens on SCSI bus while sequencer    */
   /* accesses DFCNTRL register that causes indefinite stretch condition.     */
   /* The chip generates SCSI interrupt but sequencer cannot be paused.       */
   /* Previous CHIM logic does not detect this kind of ARP hang condition     */
   /* and would continue to execute the normal error handling codes.          */
   /* For Harpoon RevA ASIC design, when the software access any registers    */
   /* residing on CIO bus without pausing sequencer first, Harpoon generates  */
   /* PCI Target Abort.  This causes NMI in Vulcan environment.               */

   if ((hstintstat & SCSI_HSTINTSTAT_MASK) && (!(OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & SCSI_PAUSEACK)))
   {
#if !SCSI_DCH_U320_MODE
      if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
      {
         /* Harpoon2 Rev A has a problem after writing CHIPRST bit to HCNTRL  */
         /* register to generate chip reset.  The problem is that Harpoon2's  */
         /* parity check logic is still running which causes the chip to      */
         /* drive wrong PERR# or SERR# to pci bus.  The details of this       */
         /* hardware problem can be found in the Razor database entry #630.   */
         /* The workaround is to disable SERRESPEN and PERRESPEN bits in      */
         /* Command registers of PCI config space before writing CHIPRST bit  */
         /* to HCNTRL register.                                               */

         /* Cannot use Harpoon's backdoor function to access PCI config space */
         /* since need to access MODE_PTR register while MODE_PTR register    */
         /* cannot be accessed because of the ARP hang problem.               */

         /* get status and command from hardware */
         statCmd = OSMxReadPCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                           SCSI_STATUS_CMD_REG);
         OSMxWritePCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                  SCSI_STATUS_CMD_REG, (statCmd & (~SCSI_SERRESPEN | ~SCSI_PERRESPEN)));
      }

      /* To get out of sequencer hang condition, the first thing  */
      /* to do is to issue chip reset.                            */
      /* Reset the chip */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL), SCSI_CHIPRST);

      if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
      {
         /* Harpoon2 Rev A chip has a problem wherein a register access can be  */
         /* started only after about three PCI clock cycles after a chip reset. */
         /* The reason is that the chip takes some amount of time to initialize */
         /* PCI Host Slave hardware module, and if an access to a register      */
         /* happens during this time the chip would not respond.  The faster    */
         /* the PCI clock, the chances are higher to fit a register access cycle*/
         /* within this time window.  So, the problem is more prominent when the*/
         /* PCI clock speed is higher.  The workaround is to induce a delay by  */
         /* calling the OSM's delay routine. There was no Razor database entry  */
         /* when this workaround was implemented. */
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }

      if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
      {
         /* Harpoon2 Rev A has a problem after writing CHIPRST bit to HCNTRL  */
         /* register to generate chip reset.  The problem is that Harpoon2's  */
         /* parity check logic is still running which causes the chip to      */
         /* drive wrong PERR# or SERR# to pci bus.  The details of this       */
         /* hardware problem can be found in the Razor database entry #630.   */
         /* The workaround is after chip reset to clear possible pci errors   */
         /* in  Status register and restore back Command registers in         */
         /* PCI config space                                                  */

         /* Set Mode 4 - to access PCI Configuration registers */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_STATUS_HI_REG), SCSI_PCIERR_MASK);

         /* Set back to Mode 3 */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

         OSMxWritePCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                  SCSI_STATUS_CMD_REG, statCmd);
      }

      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID81);
      hstintstat = 0;

      /* unpause chip (restore interrupt enable bit) */
      SCSI_hWRITE_HCNTRL(hhcb, (SCSI_UEXACT8)(hhcb->SCSI_HP_hcntrl & SCSI_INTEN));

#endif /* !SCSI_DCH_U320_MODE */
   }

   /* Harpoon2 Rev B has a problem where in if any interrupt condition is    */
   /* cleared (by writing the corresponding bit in CLRHSTINTSTAT register)   */
   /* at the same time when any edge triggered interrupt is raised by the    */
   /* hardware or the sequencer (CMDCMPLT, ARPINT, BRKADDRINT, SWTMRINT, and */
   /* HWERRINT are edge triggered interrupts), the hardware gives priority   */
   /* to the clearing of interrupt and forgets to set the interrupt.  This   */
   /* causes a hang.  The case that was seen on the bench was the CMDCMPLT   */
   /* interrupt was cleared by the host while the sequencer was generating   */
   /* an ARPINT to report underrun; the ARPINT does not get set due to the   */
   /* above problem and the system hangs as the sequencer is paused but the  */
   /* CHIM does not get the interrupt ending up in a hang situation on the   */
   /* SCSI bus.  The following combinations of interrupts have been          */
   /* identified to end up in this problem and therefore need a workaround:  */
   /*    1) CMDCMPLT is cleared by host when any ARPINTCODE is written by    */
   /*       the sequencer,                                                   */
   /*    2) CMDCMPLT is cleared by host when a BRKADRINT occurs,             */
   /*    3) CMDCMPLT is cleared by host when a HWERRINT occurs, and          */
   /*    4) CMDCMPLT is cleared by host when a CMDCMPLT occurs, and          */
   /*    5) CMDCMPLT is cleared by host when a SWTMRINT occurs.              */
   /*    6) SWTMRINT is cleared by host when a CMDCMPLT int occurs.          */
   /* Workarounds are done for each of these cases as below:                 */
   /*    Case 1 : Before the sequencer writes the ARPINTCODE register it     */
   /*             writes a code in a scratch location to indicate that it is */
   /*             trying to generate ARPINT interrupt.  If, due to the       */
   /*             hardware problem, this interrupt gets lost, we can look    */
   /*             at this code (and clear it to complete the handshake) to   */
   /*             know the interrupt happened.  Note that since we are doing */
   /*             this in the command complete path we cannot look at the    */
   /*             scratch memory location directly as the sequencer may or   */
   /*             may not have been paused depending on whether it has       */
   /*             generated the ARPINT or not.  Therefore, we need to look   */
   /*             HCNTRL register (which can be read irrespective of whether */
   /*             the sequencer is paused or not) first to make sure that    */
   /*             the sequencer is paused before we look at the scratch      */
   /*             location.                                                  */
   /*    Case 2 : This is similar to case 1 except that instead of looking   */
   /*             at a scratch location to find out if the condition we can  */
   /*             simply compare the BRKADDR register and the PRGMCNT        */
   /*             registers to figure it out.                                */
   /*    Case 3 : Since the hardware will not pause the sequencer in this    */
   /*             case, the sequencer polls the ERROR register in its idle   */
   /*             loop and generates another ARPINT which would pause the    */
   /*             sequencer.  The ARPINTCODE for this is SCSI_HWERR_DETECTED.*/
   /*    Case 4 : This case is already covered in the BackEndISR.  Since we  */
   /*             always look at the DONE Q after we clear the CMDCMPLT int, */
   /*             even if we lose a CMDCMPLT interrupt from the sequencer    */
   /*             (which might happen due to the clearing of CMDCMPLT int by */
   /*             us)  the DONE Q scan after the clear would pick up the     */
   /*             DONE Q elements.                                           */
   /*    Case 5 : Currenly this is not used and therefore, we do not have to */
   /*             worry about this case.                                     */
   /*    Case 6 : Same as Case 5.                                            */
   /* The details of the hardware problem can be found in the razor database */
   /* entry# 904. */

   if (!(hstintstat & SCSI_HSTINTSTAT_MASK) &&
        ((saved_hcntrl & SCSI_PAUSEACK) || (OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & SCSI_PAUSEACK)))
   {
#if !SCSI_DCH_U320_MODE
      if (SCSI_hHARPOON_REV_B(hhcb))
      {
         if (SCSI_hARPINTVALID_REG(hhcb) != SCSI_NULL_ENTRY)
         {
            if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb))) &
                SCSI_hARPINTVALID_BIT(hhcb))
            {
               arpintcode = OSD_INEXACT8(SCSI_AICREG(SCSI_ARPINTCODE));

               /* If !(hstintstat & SCSI_HSTINTSTAT_MASK) then in frontEndISR
                * HCNTRL was not saved in the hhcb - this needs to be done
                * for the restore HCNTRL code to work correctly.
                */
               hhcb->SCSI_HP_hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

               if (arpintcode == SCSI_HWERR_DETECTED)
               {
                  /* Case 3.... */
                  hstintstat |= SCSI_HWERRINT;
               }
               else
               {
                  /* Case 1.... */
                  hstintstat |= SCSI_ARPINT;
               }

               /* Note that the ARPINTVALID_BIT has to be cleared in the     */
               /* place where we actually process the ARPINT and HWERRINT    */
               /* interrupts, because we may never reach here if the         */
               /* 'saved_hcntrl' variable never had the PAUSEACK bit ON      */
               /* (which will be the case if we never got a command complete */
               /* interrupt which would have read the HCNTRL). */
            }
         }

         if ((((((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_BRKADDR1)))<<8) |
                 (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_BRKADDR0)))+2) ==
             ((((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_PRGMCNT1)))<<8) |
                 (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_PRGMCNT0))))
         {
            /* If !(hstintstat & SCSI_HSTINTSTAT_MASK) then in frontEndISR
             * HCNTRL was not saved in the hhcb - this needs to be done
             * for the restore HCNTRL code to work correctly.
             */
            hhcb->SCSI_HP_hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

            /* Case 2.... */
            hstintstat |= SCSI_BRKADRINT;
         }
      }
#endif /* !SCSI_DCH_U320_MODE */
   }

   /* check if there was any exceptional interrupt */
   if (hstintstat & SCSI_HSTINTSTAT_MASK)
   {
      /* Save the original mode */ 
      hhcb->SCSI_HP_originalMode =
         SCSI_hGETMODEPTR(hhcb,scsiRegister);   

      exceptionCount = 0;

#if SCSI_TEST_OSMEVENTS
      while ((((hstintstat |= OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT))) &
              SCSI_HSTINTSTAT_MASK)) ||
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
             fakePciError || fakePciRTA ||
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */
             fakeReset || fakeHaFail || fakeIoBusHang)
#else
      while ((hstintstat |= OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT))) &
             SCSI_HSTINTSTAT_MASK)
#endif /* SCSI_TEST_OSMEVENTS */
      {
         /* Set MODE 3 */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

         /* Clear the hstintstat indicator. */
         hhcb->SCSI_HP_hstIntsCleared = 0;

         /* If the number of exceptions exceeds 10, then assume that it is */
         /* spurious and recover from it. */
         exceptionCount++;
         if (exceptionCount > 10)
         {
            exceptionCount = 0;
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID00);
            break;
         }

         /* For Cardbus (APA-1480x), the instat register value is 0xFF */
         /* when the card is hot removed from the bus.  For this case, */
         /* CHIM must break out the loop.  This value is not going to  */
         /* happen on the scsi host adapter. */
         if (hstintstat == 0xFF)
         {
            break;
         }
      
#if !SCSI_DCH_U320_MODE
      
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING

         if (hstintstat & SCSI_PCIINT)
         {
            /* restore hstintstat for proper recovery */
            hhcb->SCSI_HP_hstintstat = hstintstat;
            hstintstat &= ~SCSI_PCIINT;
            hhcb->SCSI_HP_inISRContext = 1;
            SCSIhHandlePCIError(hhcb);
            /* clear flag for hardware reset if yet to be done */
            hhcb->SCSI_HP_inISRContext = 0;
            
            /* If the OSM has chosen to issue the HIM_RESET_HARDWARE
             * IOB during the PCI error handler the Sequencer can be
             * unpaused. 
             */
            if (hhcb->SCSI_HP_hstintstat & SCSI_PCIINT) 
            {
               /* Do not attempt to Unpause the sequencer
                * due to extended cleanup required by the
                * OSM.
                */
               pausePCIorPCIXFlag = SCSI_TRUE;
            }   
            hhcb->SCSI_HP_hstintstat = 0; /* clear all ints status */
            break;
         }
         /* handle Split interrupts (PCI-X) */
         if (hstintstat & SCSI_SPLTINT)
         {
            /* restore hstintstat for proper recovery */
            hhcb->SCSI_HP_hstintstat = hstintstat;
            hstintstat &= ~SCSI_SPLTINT;
            hhcb->SCSI_HP_inISRContext = 1;
            SCSIhHandleSplitInterrupt(hhcb);
            /* clear flag for hardware reset if yet to be done */
            hhcb->SCSI_HP_inISRContext = 0;
            
            /* If the OSM has chosen to issue the HIM_RESET_HARDWARE
             * IOB during the PCI error handler the Sequencer can be
             * unpaused. 
             */
            if (hhcb->SCSI_HP_hstintstat & SCSI_SPLTINT) 
            {
               /* Do not attempt to Unpause the sequencer
                * due to extended cleanup required by the
                * OSM.
                */
               pausePCIorPCIXFlag = SCSI_TRUE;
            }   
            hhcb->SCSI_HP_hstintstat = 0; /* clear all ints status */

            /* clear spltint */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRSPLTINT);
            break;
         }

#else /* SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

         /* If a PCI or Split interrupt has occurred the HA can no longer
          * function until the registers that generated this status are
          * cleared. We will call a generic utility that just clears
          * all of the status registers. No Event or taskStatus is
          * returned indicating any such error was detected. (The OSM
          * has selected no PCI Error services).
          */  
         if (hstintstat & (SCSI_PCIINT | SCSI_SPLTINT))   
         {
            if (!(SCSIhClearPCIorPCIXError(hhcb)) && (hstintstat & SCSI_SPLTINT))
            {
               /* Use the CLRHSTINT to clear SPLTINT */
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRSPLTINT);
            }
            hstintstat &= ~(SCSI_PCIINT | SCSI_SPLTINT);
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID38);
            break;  
         }
         
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

         /* handle HWERRINT */
         if (hstintstat & SCSI_HWERRINT)
         {
            hstintstat &= ~SCSI_HWERRINT;

            /* Due the Harpoon2 Rev B hardware problem of loosing HWERRINT   */
            /* (razor issue# 904) the sequencer was setting a scratch RAM bit*/
            /* to redundantly indicate that an HWERRINT has happened.  We    */
            /* need to clear that bit here so that we will not falsly detect */
            /* HWERRINT again. */
            if (SCSI_hARPINTVALID_REG(hhcb) != SCSI_NULL_ENTRY)
            {
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb)),
                  OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb))) &
                  ~(SCSI_hARPINTVALID_BIT(hhcb)));

               /* Increment the value in the CURADDR registers and update the resulting */
               /* value into the PRGMCNT registers, so that it will skip */
               /* either the second ARPINT generating instruction or   */
               /* the NOP instruction in the sequencer. */
             
               prgmcnt =
                  (((SCSI_UEXACT16)
                    OSD_INEXACT8(SCSI_AICREG(SCSI_CURADDR1)))<<8) |
                    (SCSI_UEXACT16)
                    OSD_INEXACT8(SCSI_AICREG(SCSI_CURADDR0));
               prgmcnt++;
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                  (SCSI_UEXACT8)prgmcnt); 
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                  (SCSI_UEXACT8)(prgmcnt>>8));
            }

            SCSIhHandleHwErrInterrupt(hhcb);
            break;
         }

#else
         if (hstintstat & SCSI_HDMAINT)
         {
            hstintstat &= ~SCSI_HDMAINT;

            SCSIhHandleRBIError(hhcb);
            break;  /* or continue ??? */
         }

#endif /* !SCSI_DCH_U320_MODE */
         /* Since there is exceptional interrupt, CHIM might already process  */
         /* all completed HIOB.  It is good idea to clear the SCB_DONE_AVAIL  */
         /* bit. So, the sequencer should not generate the cmdcmplt interrupt.*/
         /* Note that this is meaningful only for swapping_advanced_enhanced  */
         /* sequencer. */
         SCSI_hSETINTRFACTORTHRESHOLD(hhcb, hhcb->cmdCompleteFactorThreshold);
         SCSI_hSETINTRTHRESHOLDCOUNT(hhcb, hhcb->cmdCompleteThresholdCount);

#if SCSI_TEST_OSMEVENTS
         if (fakeReset)
         {
            fakeReset = 0;
            SCSIhIntSrst(hhcb);
            SCSI_SET_NULL_SCB(scbNumber);
            /* Clear local hstintstat variable */
            hstintstat = 0;
            continue;
         }
         else
         if (fakeHaFail)
         {
            fakeHaFail = 0;
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID34);
            SCSI_SET_NULL_SCB(scbNumber);
            /* Clear local hstintstat variable */
            hstintstat = 0;
            continue;
         }
         else
         if (fakeIoBusHang)
         {
            fakeIoBusHang = 0;
            hhcb->softwareVersion =
               (SCSI_UEXACT8) ~SCSI_SOFTWARE_VERSION;
            SCSIhIntSrst(hhcb);
            SCSI_SET_NULL_SCB(scbNumber);
            /* Clear local hstintstat variable */
            hstintstat = 0;
            continue;
         }
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
         else
         if (fakePciError || fakePciRTA)
         {
            SCSIhHandlePCIError(hhcb,fromPCICheckErr);
            SCSI_SET_NULL_SCB(scbNumber);
            /* Clear local hstintstat variable */
            hstintstat = 0;
            continue;
         }
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */
#endif /* SCSI_TEST_OSMEVENTS */

         if ((sstat1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1))) &
             SCSI_SCSIRSTI)
         {
            /* scsi reset interrupt */
            SCSIhIntSrst(hhcb);
            SCSI_SET_NULL_SCB(scbNumber);
            
            /*Only loop once for a stuck reset condition*/
            /*If bus hung not cleared then reset stuck.*/
            if (hhcb->SCSI_HF_busHung)
            {
               SCSI_hSETBUSHUNG(hhcb,0);
               break;
            }      
            
            /* Clear local hstintstat variable */
            hstintstat = 0;
            continue;
         }
         else
         if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_IOERR)
         {
            /* IO Operating mode change */
            SCSIhIntIOError(hhcb);
            SCSI_SET_NULL_SCB(scbNumber);
            /* Clear local hstintstat variable */
            hstintstat = 0;
            continue;
         }
         else
         {
            if (sstat1 & SCSI_SELTIMO) /* handle SCSI selection timout */
            {
               scbNumber = ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
                            (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));
            }
            else
            {
                              /* restore original MODE value */
               SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

               scbNumber =
                  ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1))) << 8) |
                   (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb))));

               /* Set MODE 3 */
               SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);             
            }
         }

         if (SCSI_IS_NULL_SCB(scbNumber))
         {
            hiob = SCSI_NULL_HIOB;
         }
         else
         {
            hiob = SCSI_ACTPTR[scbNumber];
         }

         if (hstintstat & SCSI_ARPINT)
         {
            /* Process sequencer interrupt */
            hstintstat &= ~SCSI_ARPINT;

            /* Due the Harpoon2 Rev B hardware problem of loosing ARPINT     */
            /* (razor issue# 904) the sequencer was setting a scratch RAM bit*/
            /* to redundantly indicate that an ARPINT has happened.  We need */
            /* to clear that bit here so that we will not falsly detect      */
            /* ARPINT again. */
#if !SCSI_DCH_U320_MODE
            if (SCSI_hARPINTVALID_REG(hhcb) != SCSI_NULL_ENTRY)
            {
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb)),
                  OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb))) &
                  ~(SCSI_hARPINTVALID_BIT(hhcb)));

               /* Increment the value in the CURADDR registers and update the resulting */
               /* value into the PRGMCNT registers, so that it will skip */
               /* either the second ARPINT generating instruction or   */
               /* the NOP instruction in the sequencer. */
               
               
               prgmcnt =
                  (((SCSI_UEXACT16)
                    OSD_INEXACT8(SCSI_AICREG(SCSI_CURADDR1)))<<8) |
                    (SCSI_UEXACT16)
                    OSD_INEXACT8(SCSI_AICREG(SCSI_CURADDR0));
               prgmcnt++;
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                  (SCSI_UEXACT8)prgmcnt); 
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                  (SCSI_UEXACT8)(prgmcnt>>8));
            }
#endif /* !SCSI_DCH_U320_MODE */
            arpintcode = OSD_INEXACT8(SCSI_AICREG(SCSI_ARPINTCODE));
            if ((SCSI_IS_NULL_SCB(scbNumber)) || (hiob == SCSI_NULL_HIOB))
            {
               /* If hiob is SCSI_NULL_HIOB then don't enter any routine  */
               /* which doesn't have protection on hiob parameter.        */
               /* Abort only if it is not a break point interrupt at idle */
               /* loop, as an scb can be null in this case                */
#if SCSI_DOMAIN_VALIDATION
               if ((arpintcode != SCSI_IDLE_LOOP_BREAK) && (arpintcode != SCSI_DV_TIMEOUT))
#else
               if (arpintcode != SCSI_IDLE_LOOP_BREAK)
#endif /*SCSI_DOMAIN_VALIDATION*/
               {
                  arpintcode = SCSI_ABORT_TARGET;
               }
            }
#if SCSI_RAID1
            if ((hiob != SCSI_NULL_HIOB) && (hiob->SCSI_IF_raid1))
            {
               /* Is the SCB for the primary */		
               /* Or the mirror target */
               if (scbNumber != hiob->scbNumber)
               {
                  if (scbNumber == hiob->mirrorHiob->scbNumber)
                  {
                     /* Update the relevant fields in the mirror hiob */
                     SCSI_UPDATE_MIRROR_HIOB(hiob);
                     /* Use the mirror hiob */
                     hiob = hiob->mirrorHiob;
                  }
                  else
                  {
                     /* BadSeq? */
                  }
               }
            }
#endif /* SCSI_RAID1 */

            /* process all the sequencer interrupts */
            switch (arpintcode)
            {
               case SCSI_DATA_OVERRUN:         /* data overrun/underrun */
               case SCSI_DATA_OVERRUN_BUSFREE:
                  SCSIhCheckLength(hhcb,hiob);
                  break;

               case SCSI_CDB_XFER_PROBLEM:     /* cdb bad transfer */
                  SCSIhCdbAbort(hhcb,hiob);
                  break;

               case SCSI_HANDLE_MSG_OUT:       /* send msg out */
                  SCSIhHandleMsgOut(hhcb,hiob);
                  break;

               case SCSI_SYNC_NEGO_NEEDED:
                  SCSI_hNEGOTIATE(hhcb,hiob);
                  break;

               case SCSI_CHECK_CONDX:
                  SCSI_hCHECKCONDITION(hhcb,hiob);
                  break;

               case SCSI_PHASE_ERROR:
                  SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID01);
                  break;

               case SCSI_EXTENDED_MSG:
                  SCSIhExtMsgi(hhcb,hiob);
                  break;

               case SCSI_UNKNOWN_MSG:
                  SCSIhHandleMsgIn(hhcb,hiob);
                  break;

               case SCSI_ABORT_TARGET:
                  SCSIhAbortTarget(hhcb,hiob);
                  break;

               case SCSI_NO_ID_MSG:
                  SCSIhAbortTarget(hhcb,hiob);
                  break;

               case SCSI_SPECIAL_FUNCTION:
#if SCSI_PACKETIZED_IO_SUPPORT
                  /* At this time, we do not handle special_function,  */
                  /* in this case is target reset, in packetized mode. */
                  if (!SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
                  {
                     SCSI_hENQUEHEADTR(hhcb, hiob);
                  }
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
                  SCSI_hENQUEHEADTR(hhcb, hiob);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
                  break;

#if SCSI_SEQBREAKPOINT_WORKAROUND
               case SCSI_IDLE_LOOP_BREAK:
               case SCSI_EXPANDER_BREAK:
                  SCSIhBreakInterrupt(hhcb,hiob);
                  break;
#endif /* SCSI_SEQBREAKPOINT_WORKAROUND */

#if SCSI_TARGET_OPERATION  
               case SCSI_TARGET_SELECTION_IN:
                  SCSIhTargetIntSelIn(hhcb,hiob);
                  break;

               case SCSI_TARGET_BUS_HELD:
                  SCSIhTargetScsiBusHeld(hhcb,hiob);
                  break;

               case SCSI_TARGET_ATN_DETECTED:
                  /* Initiator asserted ATN during a data, message-in
                   * or status phase of a reestablish connection.
                   */
                  SCSIhTargetATNIntr(hhcb,hiob);
                  break;
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_DOMAIN_VALIDATION
               case SCSI_DV_TIMEOUT: 
                  SCSIhDVTimeout(hhcb,hiob);
                  break;
#endif /* SCSI_DOMAIN_VALIDATION */

               default:
                  /* if we obtain an unknown sequencer interrupt
                   * assume something wrong.
                   */
                  SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID02);
                  break; 
            }                    /* end of sequencer interrupt handling */

            /* clear SEQ interrupt */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRARPINT);
         }

         /* process scsi interrupt */
         else
         if (hstintstat & SCSI_SCSIINT)
         {
            hstintstat &= ~SCSI_SCSIINT;

            SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
            simode0 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE0));
            SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
            sstat0 = OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)); 
#if SCSI_RAID1
            if ((hiob != SCSI_NULL_HIOB) && (hiob->SCSI_IF_raid1))
            {
               /* Is the SCB for the primary */		
               /* Or the mirror target */
               if (scbNumber != hiob->scbNumber)
               {
                  if (scbNumber == hiob->mirrorHiob->scbNumber)
                  {
                     /* Update the relevant fields in the mirror hiob */
                     SCSI_UPDATE_MIRROR_HIOB(hiob);
                     hiob->mirrorHiob->SCSI_IF_raid1 = 1;
                     /* Use the mirror hiob */
                     hiob = hiob->mirrorHiob;
                  }
                  else
                  {
                     /* BadSeq? */
                  }
               }
            }
#endif /* SCSI_RAID1 */
#if SCSI_TARGET_OPERATION
            if ((sstat0 & SCSI_TARGET) && (sstat1 & SCSI_ATNTARG) &&
                (OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE1)) & SCSI_ENATNTARG))
            {
               /* Should not come through this path any longer */
               /* Get active HIOB from the current fifo pointer */
               hiob = SCSIhGetHIOBFromCurrentFifo(hhcb);
               SCSIhTargetATNIntr(hhcb,hiob);
            } 
            else 
#endif /* SCSI_TARGET_OPERATION */
            if (sstat1 & SCSI_SELTIMO)
            {
               SCSI_hINTSELTO(hhcb,hiob);
            } 
            else
            if (sstat1 & SCSI_SCSIPERR)
            {
               /* Get active HIOB from the current fifo pointer */
               hiob = SCSIhGetHIOBFromCurrentFifo(hhcb);
#if SCSI_TARGET_OPERATION
#if (SCSI_PACKETIZED_TARGET_MODE_SUPPORT  && SCSI_STANDARD_ENHANCED_U320_MODE)
               if ((sstat0 & SCSI_TARGET) &&
                   (OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT0)) & SCSI_LQICRCT1) &&
                   SCSI_hHARPOON_REV_1B(hhcb))
               {
                  /* Packetized target mode LQICRCT1 exception */
                  /* Harpoon1 Rev B ASIC only */
                  /* No SCB or HIOB associated with this exception */
                  hiob = SCSI_NULL_HIOB;
                  SCSIhTargetLQIErrors(hhcb,hiob);
               }
               else
               if ((sstat0 & SCSI_TARGET) &&
                   (OSD_INEXACT8(SCSI_AICREG(SCSI_LQOSTAT0)) & SCSI_LQOTCRC) &&
                   SCSI_hHARPOON_REV_1B(hhcb))
               {
                  /* Packetized target mode SCSI_LQOTCRC exception */
                  /* Harpoon1 Rev B ASIC only */
                  /* CRC error on Data IU - HIOB should be valid (got from
                   * current FIFO).
                   */
                  SCSIhTargetLQOErrors(hhcb,hiob);
               }
               else
#endif /* (SCSI_PACKETIZED_TARGET_MODE_SUPPORT  && SCSI_STANDARD_ENHANCED_U320_MODE) */
               if (hhcb->SCSI_HF_targetMode && (hiob != SCSI_NULL_HIOB))
               {
                  if (hiob->SCSI_IP_targetMode)
                  {
                     SCSIhTargetParityError(hhcb,hiob);
                  }
                  else
                  {
                     SCSIhPhysicalError(hhcb,hiob);
                  }
               }
               else
               {
                  SCSIhPhysicalError(hhcb,hiob);
               }
#else
               SCSIhPhysicalError(hhcb,hiob);
#endif /* SCSI_TARGET_OPERATION */
            }
            else
            if (sstat1 & SCSI_BUSFREE)
            {
               SCSIhIntFree(hhcb,hiob); /* unexpected BUSFREE interrupt */
            }
            else
            if (((sstat0 & SCSI_SELDO) &&
                 (simode0 & SCSI_ENSELDO)
#if SCSI_TARGET_OPERATION
                     ||
                 ((sstat0 & SCSI_TARGET) &&
                  (sstat0 & SCSI_SELDI) && 
                  (simode0 & SCSI_ENSELDI) &&
                  (hhcb->SCSI_HF_targetMode))
#endif /* SCSI_TARGET_OPERATION */
                ) &&
                SCSI_hHARPOON_REV_A_AND_B(hhcb))
            {
               /* Harpoon2 Rev A chip has the following problem in the I/O    */
               /* cell:                                                       */
               /*    When the SCSI bus is in LVD mode the I/O cell sampling   */
               /*    module does not get initialized properly.  The           */
               /*    workaround is to enable the bypass after a power-on chip */
               /*    reset and on an IOERR interrupt and disable the bypass   */
               /*    after the first selection done or selection timeout. The */
               /*    details of this hardware problem can be found in the     */
               /*    Razor database entry #491.                               */
               /* Here we check if selection done interrupt has occurred.  If */
               /* so we disable the bypass and also the selection done        */
               /* interrupt enable so that no more selection done interrupts  */
               /* will occur.                                                 */
               /* If operating in target mode then a selection-in may occur   */
               /* first. Therefore, similar logic is applied to ENSELDI       */
               /* interrupt.                                                  */ 
               SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
         
               simode0 &= ~SCSI_ENSELDO;

#if SCSI_TARGET_OPERATION
               if (hhcb->SCSI_HF_targetMode)
               {
                  /* Disable selection-in interrupt also (SELDI). */
                  simode0 &= ~SCSI_ENSELDI;
               }
#endif /* SCSI_TARGET_OPERATION */
            
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE0),simode0);

               SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);             

               if (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) & SCSI_ENAB40)
               {
                  SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSPDATACTL),
                      OSD_INEXACT8(SCSI_AICREG(SCSI_DSPDATACTL)) &
                                  ~SCSI_BYPASSENAB);

                  SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);             
               }
            }
#if SCSI_PACKETIZED_IO_SUPPORT
#if SCSI_INITIATOR_OPERATION
            else
            if (OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT1)) &
                  (SCSI_LQIPHASE1 | SCSI_LQIPHASE2 | SCSI_LQIOVERI1))
            {

               /* concerns about DCH core and whether or not this is */
               /* applicable. $$$ DCH $$$                            */

               if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
               {
                  /* Harpoon2 Rev A0, A1 and A2 chips can fall into a        */
                  /* situation wherein the internal REQ/ACK offset count may */
                  /* be left non-zero when any of these three interrupts     */
                  /* happen.  The only way to bring the chip back to a       */
                  /* functional state is to do a CHIPRST.  Since the         */
                  /* SCSIhBadSeq() is currently doing a CHIPRST as a part of */
                  /* workaround for the razor issue #474, we can blindly call*/
                  /* SCSIhBadSeq().  If the workaround for #474 is ever      */
                  /* removed, we need to revisit this call, as there will not*/
                  /* be a CHIPRST anymore. */
                  SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID44);
               }
               else
               {
                  /* Handler needs to be incorporated for these interrupts. */
               }
            }
            else
            if (OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT1)) &
                  (SCSI_LQIABORT | SCSI_LQIBADLQI))
            {
               /* concerns about DCH core and whether or not this is */
               /* applicable. $$$ DCH $$$                            */

               if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
               {
                  /* Harpoon2 Rev A0, A1 and A2 chips can fall into a   */
                  /* wherein, if the target is following all the rules, */
                  /* the SCSI bus can be hung with the last ACK of the  */
                  /* LQ packet pending                                  */
                  SCSIhLQIAbortOrLQIBadLQI(hhcb,hiob);
               }
            }
            else
            if (OSD_INEXACT8(SCSI_AICREG(SCSI_LQOSTAT1)) &
                  (SCSI_LQOBADQAS | SCSI_LQOPHACHG1))
            {
               /* concerns about DCH core and whether or not this is */
               /* applicable. $$$ DCH $$$                            */

               if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
               {
                  /* Harpoon2 Rev A0, A1 and A2 chips can fall into a   */
                  /* wherein, if the target is following all the rules, */
                  /* the LQO manager can get hung due to an exception   */
                  SCSIhLQOErrors(hhcb,hiob);
               }
            }
#endif /* SCSI_INITIATOR_OPERATION */
#if (SCSI_TARGET_OPERATION && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
#if SCSI_STANDARD_ENHANCED_U320_MODE
            else
            if ((OSD_INEXACT8(SCSI_AICREG(SCSI_LQOSTAT0))) &&
                SCSI_hHARPOON_REV_1B(hhcb))
            {
               /* Harpoon1 Rev B ASIC only */
               SCSIhTargetLQOErrors(hhcb,hiob);
            }
            else
            if ((OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT0))) &&
                SCSI_hHARPOON_REV_1B(hhcb))
            {
               /* Harpoon1 Rev B ASIC only */
               SCSIhTargetLQIErrors(hhcb,hiob);
            }          
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
#endif /* (SCSI_TARGET_OPERATION && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
            else
            if (sstat1 & SCSI_PHASEMIS)
            {
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO),
                   (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) &
                                  SCSI_BUSPHASE));
               SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID03);
            }

            /* clear SCSI interrupt */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRSCSIINT);
         }
         /* process sequencer breakpoint */
         else
         if (hstintstat & SCSI_BRKADRINT)
         {
            hstintstat &= ~SCSI_BRKADRINT;

            /* interrupt might be for expander active, active abort */
            /* or target reset. */
            SCSIhBreakInterrupt(hhcb,hiob);

            /* clear break address interrupt */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRBRKADRINT);
         }

         if (hhcb->SCSI_HP_hstIntsCleared)
         {
            /* Interrupts were cleared by call such as SCSIhBadSeq, 
             * therefore clear the local hstintstat variable.
             */
            hstintstat = 0;
         }
      } /* End of while loop */

      SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);
#if !SCSI_DCH_U320_MODE
               
      /* unpause chip (restore interrupt enable bit) */
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
      /* For PCI/PCIX error handling, we will not unpause the sequencer */
      /* if the HIM_RESET_HARDWARE IOB has not been issued yet.         */
      if (pausePCIorPCIXFlag == SCSI_FALSE)
      {
         SCSI_hWRITE_HCNTRL(hhcb,(SCSI_UEXACT8)(hhcb->SCSI_HP_hcntrl & SCSI_INTEN));
      }
#else      
      
      SCSI_hWRITE_HCNTRL(hhcb, (SCSI_UEXACT8)(hhcb->SCSI_HP_hcntrl & SCSI_INTEN));

#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */
#else
      /* DCH_SCSI has multiple int enables */    
      SCSI_hWRITE_HCNTRL(hhcb, (SCSI_UEXACT8)(hhcb->SCSI_HP_hcntrl & ~(SCSI_PAUSE | SCSI_PAUSEREQ)));
#endif /* !SCSI_DCH_U320_MODE */
   }

   OSD_SYNCHRONIZE_IOS(hhcb);
/************************************************
   SCSIHBackEndISR() end
*************************************************/

   /* call to SCSIrPostDoneQueue(hhcb); */
/************************************************
   SCSIrPostDoneQueue() start
*************************************************/
   /* Post all HIOB(s) in DoneQ */
#if SCSI_RAID1
   while ((hhcb->SCSI_DONE_HEAD != SCSI_NULL_HIOB) ||
          (raid1State == SCSI_RAID1_MIRROR_COMPLETE))
#else
   while (hhcb->SCSI_DONE_HEAD != SCSI_NULL_HIOB)
#endif /* SCSI_RAID1 */
   {

#if SCSI_RAID1
      if (raid1State == SCSI_RAID1_MIRROR_COMPLETE)
      {
         iob = iob->relatedIob;
         hiob = hiob->mirrorHiob;
      }
      else
      {
         hiob = hhcb->SCSI_DONE_HEAD;
         hhcb->SCSI_DONE_HEAD = hiob->queueNext;
         iob = SCSI_GETIOB(hiob);
         if (iob->flagsIob.autoSense)
         {
            autoSense = HIM_TRUE;
         }
      }

#else /* !SCSI_RAID1 */

      hiob = hhcb->SCSI_DONE_HEAD;
      hhcb->SCSI_DONE_HEAD = hiob->queueNext;

      /* Moved above SCSI_TARGET_OPERATION for SCSI_RAID1 */
      iob = SCSI_GETIOB(hiob);
#endif /* SCSI_RAID1 */

#if SCSI_TARGET_OPERATION
      if (hhcb->SCSI_HF_targetMode)
      {
         /* reset the hiobActive flag for the nexus */
         if (hiob->SCSI_IP_targetMode)
         {
            SCSI_NEXUS_UNIT(hiob)->SCSI_XP_hiobActive = 0;
         }
      }
#endif /* SCSI_TARGET_OPERATION */

      /* call to OSD_COMPLETE_HIOB(hiob); */
/************************************************
   OSDCompleteHIOB() start
*************************************************/

      /* process normal command first */
      if (hiob->stat == SCSI_SCB_COMP)
      {
         /* process successful status */
         iob->taskStatus = HIM_IOB_GOOD;
      }
      else
      {
         /* translate error status */
         /* call to - SCSIxTranslateError(iob,hiob); */
/************************************************
   SCSIxTranslateError() start
*************************************************/
         /* interpret stat of hiob */ 
         switch(hiob->stat)
         {
            case SCSI_SCB_ERR:
               /* interpret haStat of hiob */
               switch(hiob->haStat)
               {
                  case SCSI_HOST_SEL_TO:
                     iob->taskStatus = HIM_IOB_NO_RESPONSE;
                     break;

                  case SCSI_HOST_BUS_FREE:
                     iob->taskStatus = HIM_IOB_CONNECTION_FAILED;
                     break;

                  case SCSI_HOST_PHASE_ERR:
                  case SCSI_HOST_HW_ERROR:    
                     iob->taskStatus = HIM_IOB_HOST_ADAPTER_FAILURE;
                     break;

                  case SCSI_HOST_SNS_FAIL:
                     iob->taskStatus = HIM_IOB_ERRORDATA_FAILED;
                     break;

                  case SCSI_HOST_DETECTED_ERR:
#if SCSI_CRC_NOTIFICATION
                     iob->residual = hiob->residualLength;
#endif /* SCSI_CRC_NOTIFICATION */
#if SCSI_DATA_IN_RETRY_DETECTION
                  case SCSI_HOST_READ_RETRY:
                     /* a data-in I/O request (read) completed successfully */
                     /* after target retry. E.g. an intermediate data       */
                     /* transfer was terminated without a save data         */
                     /* pointers message and the data was resent on the     */
                     /* next reconnect.                                     */
#endif /* SCSI_DATA_IN_RETRY_DETECTION */
                     iob->taskStatus = HIM_IOB_PARITY_ERROR;
                     break;

                  case SCSI_HOST_DU_DO:
                  case SCSI_HOST_NO_STATUS:
                     /* interpret trgStatus of hiob */
                     switch(hiob->trgStatus)
                     {
                        case SCSI_UNIT_CHECK:
#if SCSI_RAID1
                           if (autoSense)
#else
                           if (iob->flagsIob.autoSense)
#endif /* SCSI_RAID1 */
                           {
                              /* Set return status */
                              iob->taskStatus = HIM_IOB_ERRORDATA_VALID;

                              /* Get residual sense length */
                              iob->residualError = (HIM_UEXACT32) hiob->snsResidual;

                              if (iob->errorDataLength > SCSI_HIOB_MAX_SENSE_LENGTH)
                              {
                                 iob->residualError += (iob->errorDataLength - SCSI_HIOB_MAX_SENSE_LENGTH);
                              }
                           }
                           else
                           {
                              iob->taskStatus = HIM_IOB_ERRORDATA_REQUIRED;
                           }

                           if (hiob->haStat == SCSI_HOST_DU_DO)
                           {
                              iob->residual = hiob->residualLength;
                           }

#if SCSI_PACKETIZED_IO_SUPPORT
                           if (hiob->SCSI_IF_tmfValid != 0)   
                           {
                              /* Interpret Packetized Failure code */
                              switch (hiob->tmfStatus)
                              {
                                 case SCSI_PFC_NO_FAILURE:         /* No failure */
                                    if (hiob->haStat == SCSI_HOST_DU_DO)
                                    {
                                       /* iob->residual is already assigned */
                                       iob->taskStatus = HIM_IOB_DATA_OVERUNDERRUN;
                                    }
                                    else
                                    {
                                       iob->taskStatus = HIM_IOB_GOOD;
                                    }
                                    break;

                                 case SCSI_PFC_SPI_CMD_IU_FIELDS_INVALID:
                                    /* SPI Command Information Unit fields  invalid */
                                 case SCSI_PFC_TMF_FAILED:
                                    /* Task management function failed */
                                 case SCSI_PFC_INV_TYPE_CODE_IN_LQ:
                                    /* Invalid type code received in SPI L_Q IU */
                                 case SCSI_PFC_ILLEGAL_REQUEST_IN_LQ:
                                    /* Illegal request received in SPI L_Q IU */
                                    iob->taskStatus = HIM_IOB_REQUEST_FAILED;         
                                    break;

                                 case SCSI_PFC_TMF_NOT_SUPPORTED:
                                    /* Task management function not support */
                                    iob->taskStatus = HIM_IOB_UNSUPPORTED_REQUEST;         
                                    break;

                                 default:
                                    /* Reserved code */
                                    iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
                                    break;
                              }

                              /* If transportSpecific defined then fill in protocolStatus
                               * field with Packetized Failure Code.
                               */
                              if (iob->transportSpecific)
                              {
                                 ((HIM_TS_SCSI HIM_PTR)
                                     (iob->transportSpecific))->protocolStatus =
                                        (HIM_UEXACT8) hiob->tmfStatus;
                              }
                           }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
                           break;

                        case SCSI_UNIT_BUSY:
                           iob->taskStatus = HIM_IOB_BUSY;
                           break;

                        case SCSI_UNIT_RESERV:
                           iob->taskStatus = HIM_IOB_TARGET_RESERVED;
                           break;

                        case SCSI_UNIT_QUEFULL:
                           iob->taskStatus = HIM_IOB_TASK_SET_FULL;
                           break;

                        case SCSI_UNIT_GOOD:
                        case SCSI_UNIT_MET:
                        case SCSI_UNIT_INTERMED:
                        case SCSI_UNIT_INTMED_GD:
                           if (hiob->haStat == SCSI_HOST_DU_DO)
                           {
                              iob->taskStatus = HIM_IOB_DATA_OVERUNDERRUN;
                              iob->residual = hiob->residualLength;
                           }
                           else
                           {
                              iob->taskStatus = HIM_IOB_GOOD;
                           }
                           break;

                       case SCSI_UNIT_ACA_ACTIVE:
                       case SCSI_UNIT_TASK_ABTED:
                       default:
                          /* should never come to here */
                          iob->taskStatus = HIM_IOB_TRANSPORT_SPECIFIC;
                          /* If transportSpecific defined then fill in protocolStatus
                           * SCSI status value.
                           */
                          if (iob->transportSpecific)
                          {
                             ((HIM_TS_SCSI HIM_PTR)(iob->transportSpecific))->protocolStatus =
                                (HIM_UEXACT8) hiob->trgStatus;
                          }                     
                          break;
                     }
                     break;

                  case SCSI_HOST_TAG_REJ:
                  case SCSI_HOST_ABT_FAIL:
                  case SCSI_HOST_RST_HA:   
                  case SCSI_HOST_RST_OTHER:
                     /* not implemented error */
                     /* should never come to here */
                     iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
                     break;

                  case SCSI_HOST_ABT_NOT_FND:
                     iob->taskStatus = HIM_IOB_ABORT_NOT_FOUND;
                     break;

                  case SCSI_HOST_ABT_CMDDONE:
                     iob->taskStatus = HIM_IOB_ABORT_ALREADY_DONE;
                     break;

                  case SCSI_HOST_NOAVL_INDEX: 
                  case SCSI_HOST_INV_LINK:
                     /* not implemented error */
                     /* should never come to here */
                     iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
                     break;

#if SCSI_TARGET_OPERATION
                  case SCSI_INITIATOR_PARITY_MSG:
                     /* Initiator message parity message */ 
                     iob->taskStatus = HIM_IOB_INITIATOR_DETECTED_PARITY_ERROR;
                     break;
                
                  case SCSI_INITIATOR_PARITY_ERR_MSG:
                     /* Initiator detected error message*/ 
                     iob->taskStatus = HIM_IOB_INITIATOR_DETECTED_ERROR;
                     break;        

                  case SCSI_HOST_MSG_REJECT: 
                     /* Initiator rejected a message which we expected to be OK. */
                     iob->taskStatus = HIM_IOB_INVALID_MESSAGE_REJECT;
                     break; 
                                         
                  case SCSI_INITIATOR_INVALID_MSG:
                     /* Invalid message recieved from initiator. After the */
                     /* HIM issued a Message Reject message in response    */
                     /* to an unrecognized or unsupported message the      */
                     /* initiator released the ATN signal.                 */ 
                     iob->taskStatus = HIM_IOB_INVALID_MESSAGE_RCVD;
                     break;
                            
                  case SCSI_HOST_IDENT_RSVD_BITS:
                     /* Identify Message has reserved bits set. XLM should */
                     /* never get this error code. HWM uses this code to   */
                     /* indicate that a reserved bit is set in the         */
                     /* Identify message and the adapter profile field,    */
                     /* AP_SCSI2_IdentifyMsgRsv, is set to 0, indicating   */
                     /* that the HWM is to issue a message reject. Only    */
                     /* applies to SCSI2.                                  */
                     iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
                     break;

                  case SCSI_HOST_IDENT_LUNTAR_BIT:
                     /* Identify Message has the luntar bit set. XLM       */
                     /* should never get this error code. HWM uses this    */
                     /* code to indicate that the luntar bit is set in the */
                     /* Identify message and the adapter profile field,    */
                     /* AP_SCSI2_TargetRejectLuntar, is set to 1,          */
                     /* indicating that the HWM is to issue a message      */
                     /* reject. Only applies to SCSI2.                     */
                     iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
                     break; 

                  case SCSI_HOST_TRUNC_CMD: 
                     /* Truncated SCSI Command */
                     iob->taskStatus = HIM_IOB_TARGETCOMMANDBUFFER_OVERRUN; 
                     break;
#endif /* SCSI_TARGET_OPERATION */

                  case SCSI_HOST_ABT_CHANNEL_FAILED:
                     iob->taskStatus = HIM_IOB_ABORTED_CHANNEL_FAILED;
                     break;

                  case SCSI_HOST_PROTOCOL_ERROR:
                     iob->taskStatus = HIM_IOB_PROTOCOL_ERROR;
                     break;

                  default:
                     /* should never come to here */
                     iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
                     break;
               }
               break;

            case SCSI_SCB_ABORTED:
               /* interpret haStat of hiob */
               switch(hiob->haStat)
               {
                  case SCSI_HOST_ABT_HOST:
                     iob->taskStatus = HIM_IOB_ABORTED_ON_REQUEST;
                     break;

                  case SCSI_HOST_ABT_HA:
                     iob->taskStatus = HIM_IOB_ABORTED_CHIM_RESET;
                     break;

                  case SCSI_HOST_ABT_BUS_RST:
                     iob->taskStatus = HIM_IOB_ABORTED_REQ_BUS_RESET;
                     break;

                  case SCSI_HOST_ABT_3RD_RST:
                     iob->taskStatus = HIM_IOB_ABORTED_3RD_PARTY_RESET;
                     break;

                  case SCSI_HOST_ABT_IOERR:
                     iob->taskStatus = HIM_IOB_ABORTED_TRANSPORT_MODE_CHANGE;
                     break;

                  case SCSI_HOST_ABT_TRG_RST:
                     iob->taskStatus = HIM_IOB_ABORTED_REQ_TARGET_RESET;
                     break;

                  case SCSI_HOST_ABT_LUN_RST:
                     iob->taskStatus = HIM_IOB_ABORTED_REQ_LUR;
                     break;
                     
                  case SCSI_HOST_PHASE_ERR:
                  case SCSI_HOST_HW_ERROR:    
                     iob->taskStatus = HIM_IOB_HOST_ADAPTER_FAILURE;
                     break;

#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING

                  /* PCIINT interrupt status for IOB */
                  case SCSI_HOST_ABORTED_PCI_ERROR:   
                     iob->taskStatus = HIM_IOB_ABORTED_PCI_ERROR;
                     break;

                  /* SPLTINT interrupt status for IOB */
                  case SCSI_HOST_ABORTED_PCIX_SPLIT_ERROR:
                     iob->taskStatus = HIM_IOB_ABORTED_PCIX_SPLIT_ERROR;
                     break;

                  /* Interrupt indicates IOB active during PCI/PCI-X error */      
                  case SCSI_HOST_PCI_OR_PCIX_ERROR:
                     iob->taskStatus = HIM_IOB_PCI_OR_PCIX_ERROR;
                     break;

#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

#if SCSI_TARGET_OPERATION
                  /* Target mode HIOB Abort reasons */
                  case SCSI_HOST_TARGET_RESET:
                     iob->taskStatus = HIM_IOB_ABORTED_TR_RECVD;
                     break;

                  case SCSI_HOST_ABORT_TASK:
                     iob->taskStatus = HIM_IOB_ABORTED_ABT_RECVD;
                     break;

                  case SCSI_HOST_ABORT_TASK_SET:
                     iob->taskStatus = HIM_IOB_ABORTED_ABTS_RECVD;
                     break;

                  case SCSI_HOST_CLEAR_TASK_SET:
                     iob->taskStatus = HIM_IOB_ABORTED_CTS_RECVD;
                     break;

                  case SCSI_HOST_TERMINATE_TASK:
                     iob->taskStatus = HIM_IOB_ABORTED_TT_RECVD;
                     break;

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
                  case SCSI_HOST_ABT_IU_REQ_CHANGE:
                     iob->taskStatus = HIM_IOB_ABORTED_IU_REQ_CHANGE;
                     break;
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

#endif /* SCSI_TARGET_OPERATION */

                  default:
                     /* should never come to here */
                     iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
                     break;
               }
               break;

            case SCSI_SCB_PENDING:
               if (hiob->haStat == SCSI_HOST_ABT_STARTED)
               {
                  iob->taskStatus = HIM_IOB_ABORT_STARTED;
               }
               else
               {
                  iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
               }
               break;
               
            default:
               /* SCSI_SCB_COMP is not processed here */
               /* SCSI_TASK_CMD_COMP is not processed here */
               /* SCSI_SCB_INV_CMD should never happen for this implementation */
               /* we should never get to here */
               iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
               break;
         }
/************************************************
   SCSIxTranslateError() end
*************************************************/

      }

#if SCSI_RAID1
   if (raid1State == SCSI_RAID1_MIRROR_COMPLETE)
   {
      raid1State = SCSI_RAID1_PRIMARY_COMPLETE;
      /* This points back to the primary IOB */
      hiob = hiob->mirrorHiob;
      iob = SCSI_GETIOB(hiob);
      iob->postRoutine(iob);
   }
   else
   {
      if (hiob->SCSI_IF_raid1)
      {
         raid1State = SCSI_RAID1_MIRROR_COMPLETE;
      }
      else
      {
         iob->postRoutine(iob);
      }
   }

#else
      iob->postRoutine(iob);
#endif /* SCSI_RAID1 */
/************************************************
   OSDCompleteHIOB() end
*************************************************/
   }
   /* All DoneQ element have been posted back. The Q is empty now */
   hhcb->SCSI_DONE_TAIL=SCSI_NULL_HIOB;
   
/************************************************
   SCSIrPostDoneQueue() end
*************************************************/
/************************************************
   SCSIRBackEndISR() end
*************************************************/

}
#endif /* SCSI_STREAMLINE_QIOPATH */





